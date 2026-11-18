#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "platform/crash.h"

#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform/log.h"

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#include <dbghelp.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#define NW_CRASH_FRAMES 24
#define NW_CRASH_FAR 0x100000
#define NW_CRASH_RECENT (48 * 1024)

typedef struct NwCrashSymbol {
    uintptr_t at;
    uint32_t name;
} NwCrashSymbol;

#ifdef _WIN32
typedef BOOL(WINAPI *NwWriteDump)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                  PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
#endif

static struct {
    NwCrashSymbol *syms;
    size_t nsyms;
    char *names;
    uintptr_t base;
    const void *exe;
    intptr_t file;
    bool to_stderr;
    size_t (*state)(char *out, size_t size);
    const char *volatile phase;
    volatile unsigned phase_i, phase_n;
    SDL_AtomicInt busy;
    SDL_ThreadID owner;
    bool test;
    char line[1024];
    char where[512];
    char text[8192];
    char recent[NW_CRASH_RECENT];
#ifdef _WIN32
    LPTOP_LEVEL_EXCEPTION_FILTER next;
    void (*next_abort)(int);
    NwWriteDump write_dump;
    WCHAR dump_dir[MAX_PATH];
    char dump_shown[MAX_PATH];
#else
    struct sigaction next[5];
#endif
} nw_crash = { .file = NW_CRASH_NO_FILE, .to_stderr = true };

static const char *nw_crash_file_name(const char *path)
{
    const char *name = path;
    for (const char *c = path; *c; c++) {
        if (*c == '/' || *c == '\\') {
            name = c + 1;
        }
    }
    return name;
}

static void nw_crash_out(const char *text, size_t n)
{
#ifdef _WIN32
    DWORD written;
    if (nw_crash.to_stderr) {
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, (DWORD)n, &written, NULL);
    }
    if (nw_crash.file != NW_CRASH_NO_FILE) {
        WriteFile((HANDLE)nw_crash.file, text, (DWORD)n, &written, NULL);
    }
#else
    ssize_t r = 0;
    if (nw_crash.to_stderr) {
        r = write(STDERR_FILENO, text, n);
    }
    if (nw_crash.file != NW_CRASH_NO_FILE) {
        r = write((int)nw_crash.file, text, n);
    }
    (void)r;
#endif
}

static void nw_crash_line(const char *fmt, ...)
{
    size_t size = sizeof(nw_crash.line) - 1;
    uint64_t ms = nw_log_ms();
    int n = SDL_snprintf(nw_crash.line, size, "[%02llu:%02llu.%03llu] ", (unsigned long long)(ms / 60000),
                         (unsigned long long)(ms / 1000 % 60), (unsigned long long)(ms % 1000));
    va_list args;
    va_start(args, fmt);
    n += SDL_vsnprintf(nw_crash.line + n, size - (size_t)n, fmt, args);
    va_end(args);
    n = SDL_min(n, (int)size - 1);
    nw_crash.line[n++] = '\n';
    nw_crash_out(nw_crash.line, (size_t)n);
}

const char *nw_crash_symbol(const void *addr, uintptr_t *offset)
{
    uintptr_t at = (uintptr_t)addr - nw_crash.base;
    size_t lo = 0, hi = nw_crash.nsyms;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (nw_crash.syms[mid].at <= at) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0 || at - nw_crash.syms[lo - 1].at > NW_CRASH_FAR) {
        return NULL;
    }
    *offset = at - nw_crash.syms[lo - 1].at;
    return nw_crash.names + nw_crash.syms[lo - 1].name;
}

static const char *nw_crash_where(const void *addr, bool caller)
{
    const char *module = "?";
    const void *base = NULL;
#ifdef _WIN32
    HMODULE m = NULL;
    static char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &m) &&
        GetModuleFileNameA(m, path, sizeof(path))) {
        module = nw_crash_file_name(path);
        base = m;
    }
#else
    Dl_info dl = { 0 };
    if (dladdr(addr, &dl) && dl.dli_fname) {
        module = nw_crash_file_name(dl.dli_fname);
        base = dl.dli_fbase;
    }
#endif
    uintptr_t off = 0;
    const char *name = base && base == nw_crash.exe ? nw_crash_symbol((const char *)addr - caller, &off) : NULL;
    if (name) {
        SDL_snprintf(nw_crash.where, sizeof(nw_crash.where), "%s+0x%llx (%s+0x%llx)", name,
                     (unsigned long long)(off + caller), module,
                     (unsigned long long)((uintptr_t)addr - (uintptr_t)base));
    } else {
        SDL_snprintf(nw_crash.where, sizeof(nw_crash.where), "%s+0x%llx", module,
                     (unsigned long long)((uintptr_t)addr - (uintptr_t)base));
    }
    return nw_crash.where;
}

static bool nw_crash_enter(void)
{
    SDL_ThreadID self = SDL_GetCurrentThreadID();
    while (!SDL_CompareAndSwapAtomicInt(&nw_crash.busy, 0, 1)) {
        if (nw_crash.owner == self) {
            return false;
        }
        SDL_Delay(10);
    }
    nw_crash.owner = self;
    return true;
}

static void nw_crash_leave(void)
{
    nw_crash.owner = 0;
    SDL_SetAtomicInt(&nw_crash.busy, 0);
}

static void nw_crash_tail(const char *dump)
{
    if (nw_crash.state) {
        size_t n = nw_crash.state(nw_crash.text, sizeof(nw_crash.text));
        for (size_t i = 0; i < n;) {
            size_t end = i;
            while (end < n && nw_crash.text[end] != '\n') {
                end++;
            }
            nw_crash_line("%.*s", (int)(end - i), nw_crash.text + i);
            i = end + 1;
        }
    }
    const char *phase = nw_crash.phase;
    if (phase && nw_crash.phase_n) {
        nw_crash_line("gpu: %s, %u of %u", phase, nw_crash.phase_i + 1, nw_crash.phase_n);
    } else if (phase) {
        nw_crash_line("gpu: %s", phase);
    }
    if (dump && *dump) {
        nw_crash_line("minidump: %s", dump);
    }
    size_t n = nw_log_recent(nw_crash.recent, sizeof(nw_crash.recent));
    nw_crash_line("last log lines:");
    for (size_t i = 0; i < n;) {
        size_t end = i;
        while (end < n && nw_crash.recent[end] != '\n') {
            end++;
        }
        nw_crash_out("  ", 2);
        nw_crash_out(nw_crash.recent + i, end - i + (end < n));
        i = end + 1;
    }
}

static uint16_t nw_crash_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t nw_crash_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool nw_crash_read(SDL_IOStream *io, uint64_t at, void *buf, size_t n)
{
    return SDL_SeekIO(io, (Sint64)at, SDL_IO_SEEK_SET) == (Sint64)at && SDL_ReadIO(io, buf, n) == n;
}

typedef struct NwCrashTable {
    NwCrashSymbol *syms;
    size_t n, cap;
    char *names;
    size_t len, size;
} NwCrashTable;

static bool nw_crash_add(NwCrashTable *t, uintptr_t at, const char *name, size_t len)
{
    if (t->n == t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 4096;
        NwCrashSymbol *grown = SDL_realloc(t->syms, cap * sizeof(*grown));
        if (!grown) {
            return false;
        }
        t->syms = grown;
        t->cap = cap;
    }
    if (t->len + len + 1 > t->size) {
        size_t size = t->size ? t->size * 2 : 1 << 16;
        while (size < t->len + len + 1) {
            size *= 2;
        }
        char *grown = SDL_realloc(t->names, size);
        if (!grown) {
            return false;
        }
        t->names = grown;
        t->size = size;
    }
    t->syms[t->n++] = (NwCrashSymbol){ at, (uint32_t)t->len };
    SDL_memcpy(t->names + t->len, name, len);
    t->names[t->len + len] = '\0';
    t->len += len + 1;
    return true;
}

static int nw_crash_cmp(const void *a, const void *b)
{
    uintptr_t x = ((const NwCrashSymbol *)a)->at, y = ((const NwCrashSymbol *)b)->at;
    return x < y ? -1 : x > y;
}

#ifdef _WIN32
static bool nw_crash_symbols(SDL_IOStream *io, NwCrashTable *t)
{
    uint8_t dos[64], coff[24];
    if (!nw_crash_read(io, 0, dos, sizeof(dos)) || !nw_crash_read(io, nw_crash_u32(dos + 0x3C), coff, sizeof(coff)) ||
        SDL_memcmp(coff, "PE\0\0", 4) != 0) {
        return false;
    }
    uint32_t nsec = nw_crash_u16(coff + 6), symoff = nw_crash_u32(coff + 12), nsym = nw_crash_u32(coff + 16);
    uint64_t secoff = (uint64_t)nw_crash_u32(dos + 0x3C) + 24 + nw_crash_u16(coff + 20);
    uint8_t len4[4];
    if (!symoff || !nsym || !nsec || nsym > (1u << 24) ||
        !nw_crash_read(io, (uint64_t)symoff + (uint64_t)nsym * 18, len4, sizeof(len4))) {
        return false;
    }
    uint32_t strsize = nw_crash_u32(len4);
    uint8_t *sec = SDL_malloc((size_t)nsec * 40), *sym = SDL_malloc((size_t)nsym * 18);
    char *str = strsize >= 4 && strsize < (1u << 28) ? SDL_malloc(strsize + 1) : NULL;
    bool ok = sec && sym && str && nw_crash_read(io, secoff, sec, (size_t)nsec * 40) &&
              nw_crash_read(io, symoff, sym, (size_t)nsym * 18) &&
              nw_crash_read(io, (uint64_t)symoff + (uint64_t)nsym * 18, str, strsize);
    for (uint32_t i = 0; ok && i < nsym; i += 1 + sym[i * 18 + 17]) {
        const uint8_t *s = sym + (size_t)i * 18;
        int number = (int16_t)nw_crash_u16(s + 12);
        uint16_t type = nw_crash_u16(s + 14);
        if (number < 1 || (uint32_t)number > nsec || !(nw_crash_u32(sec + (number - 1) * 40 + 36) & 0x20) ||
            (type != 0x20 && s[16] != 2)) {
            continue;
        }
        const char *name = (const char *)s;
        size_t len = 0;
        if (nw_crash_u32(s) == 0) {
            uint32_t at = nw_crash_u32(s + 4);
            if (at >= strsize) {
                continue;
            }
            name = str + at;
            len = SDL_strnlen(name, strsize - at);
        } else {
            len = SDL_strnlen(name, 8);
        }
        if (len && name[0] != '.') {
            ok = nw_crash_add(t, nw_crash_u32(sec + (number - 1) * 40 + 12) + nw_crash_u32(s + 8), name, len);
        }
    }
    SDL_free(sec);
    SDL_free(sym);
    SDL_free(str);
    return ok;
}

static void nw_crash_load_symbols(NwCrashTable *t)
{
    WCHAR wide[MAX_PATH];
    char path[MAX_PATH * 3];
    DWORD n = GetModuleFileNameW(NULL, wide, MAX_PATH);
    if (!n || n == MAX_PATH || !WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, sizeof(path), NULL, NULL)) {
        return;
    }
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (io && nw_crash_symbols(io, t)) {
        nw_crash.exe = GetModuleHandleW(NULL);
        nw_crash.base = (uintptr_t)nw_crash.exe;
    }
    SDL_CloseIO(io);
}
#else
static uint64_t nw_crash_u64(const uint8_t *p)
{
    return (uint64_t)nw_crash_u32(p) | (uint64_t)nw_crash_u32(p + 4) << 32;
}

static void nw_crash_load_symbols(NwCrashTable *t)
{
    SDL_IOStream *io = SDL_IOFromFile("/proc/self/exe", "rb");
    uint8_t eh[64];
    if (!io || !nw_crash_read(io, 0, eh, sizeof(eh)) || SDL_memcmp(eh, "\x7f" "ELF", 4) != 0 || eh[4] != 2 ||
        eh[5] != 1 || nw_crash_u16(eh + 0x3A) != 64) {
        SDL_CloseIO(io);
        return;
    }
    uint64_t shoff = nw_crash_u64(eh + 0x28);
    uint32_t shnum = nw_crash_u16(eh + 0x3C);
    uint8_t *sh = shnum ? SDL_malloc((size_t)shnum * 64) : NULL;
    uint8_t *sym = NULL;
    char *str = NULL;
    uint64_t self = 0;
    if (sh && nw_crash_read(io, shoff, sh, (size_t)shnum * 64)) {
        for (uint32_t i = 0; i < shnum && !sym; i++) {
            const uint8_t *s = sh + (size_t)i * 64;
            if (nw_crash_u32(s + 4) != 2 || nw_crash_u32(s + 40) >= shnum) {
                continue;
            }
            const uint8_t *link = sh + (size_t)nw_crash_u32(s + 40) * 64;
            uint64_t size = nw_crash_u64(s + 32), strsize = nw_crash_u64(link + 32);
            if (size > (1u << 28) || strsize > (1u << 28)) {
                continue;
            }
            sym = SDL_malloc(size);
            str = SDL_malloc(strsize + 1);
            if (!sym || !str || !nw_crash_read(io, nw_crash_u64(s + 24), sym, size) ||
                !nw_crash_read(io, nw_crash_u64(link + 24), str, strsize)) {
                break;
            }
            str[strsize] = '\0';
            for (uint64_t at = 0; at + 24 <= size; at += 24) {
                const uint8_t *e = sym + at;
                uint32_t name = nw_crash_u32(e);
                uint64_t value = nw_crash_u64(e + 8);
                if ((e[4] & 0xF) != 2 || !value || !nw_crash_u16(e + 6) || name >= strsize) {
                    continue;
                }
                if (SDL_strcmp(str + name, "nw_crash_install") == 0) {
                    self = value;
                }
                if (!nw_crash_add(t, value, str + name, SDL_strlen(str + name))) {
                    break;
                }
            }
        }
    }
    Dl_info dl = { 0 };
    if (self && dladdr((void *)(uintptr_t)&nw_crash_install, &dl)) {
        nw_crash.base = (uintptr_t)&nw_crash_install - self;
        nw_crash.exe = dl.dli_fbase;
    } else {
        t->n = 0;
    }
    SDL_free(sh);
    SDL_free(sym);
    SDL_free(str);
    SDL_CloseIO(io);
}
#endif

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
static void nw_crash_stack(CONTEXT *c)
{
    nw_crash_line("stack:");
    for (int i = 0; i < NW_CRASH_FRAMES && c->Rip; i++) {
        nw_crash_line("  %s", nw_crash_where((const void *)c->Rip, i > 0));
        DWORD64 image;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c->Rip, &image, NULL);
        if (fn) {
            PVOID data;
            DWORD64 frame;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c->Rip, fn, c, &data, &frame, NULL);
        } else {
            c->Rip = *(DWORD64 *)c->Rsp;
            c->Rsp += 8;
        }
    }
}

static void nw_crash_here(void)
{
    CONTEXT c;
    RtlCaptureContext(&c);
    nw_crash_stack(&c);
}
#elif defined(_WIN32)
static void nw_crash_here(void)
{
}
#else
static void nw_crash_frames(void *const *frames, int n, int first)
{
    nw_crash_line("stack:");
    for (int i = first; i < n && i < first + NW_CRASH_FRAMES; i++) {
        nw_crash_line("  %s", nw_crash_where(frames[i], i > first));
    }
}

static void nw_crash_here(void)
{
    void *frames[NW_CRASH_FRAMES];
    nw_crash_frames(frames, backtrace(frames, NW_CRASH_FRAMES), 0);
}
#endif

#ifdef _WIN32
static void nw_crash_minidump(EXCEPTION_POINTERS *e, char *shown, size_t size)
{
    if (!nw_crash.write_dump || !nw_crash.dump_dir[0]) {
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    char name[64];
    SDL_snprintf(name, sizeof(name), "nuworld-%04u%02u%02u-%02u%02u%02u.dmp", t.wYear, t.wMonth, t.wDay, t.wHour,
                 t.wMinute, t.wSecond);
    WCHAR path[MAX_PATH];
    size_t dir = wcslen(nw_crash.dump_dir);
    SDL_memcpy(path, nw_crash.dump_dir, dir * sizeof(WCHAR));
    if (!MultiByteToWideChar(CP_UTF8, 0, name, -1, path + dir, (int)(MAX_PATH - dir))) {
        return;
    }
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info = { GetCurrentThreadId(), e, FALSE };
    BOOL ok = nw_crash.write_dump(GetCurrentProcess(), GetCurrentProcessId(), f,
                                  (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo |
                                                  MiniDumpWithIndirectlyReferencedMemory |
                                                  MiniDumpWithUnloadedModules),
                                  &info, NULL, NULL);
    CloseHandle(f);
    if (ok) {
        SDL_snprintf(shown, size, "%s%s", nw_crash.dump_shown, name);
    } else {
        DeleteFileW(path);
    }
}

static LONG WINAPI nw_crash_exception(EXCEPTION_POINTERS *e)
{
    EXCEPTION_RECORD *r = e->ExceptionRecord;
    if (!nw_crash_enter()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char what[96];
    switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
        SDL_snprintf(what, sizeof(what), "access violation %s 0x%llx",
                     r->ExceptionInformation[0] == 1 ? "writing" : r->ExceptionInformation[0] == 8 ? "executing"
                                                                                                  : "reading",
                     (unsigned long long)r->ExceptionInformation[1]);
        break;
    case EXCEPTION_STACK_OVERFLOW:
        SDL_strlcpy(what, "stack overflow", sizeof(what));
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        SDL_strlcpy(what, "illegal instruction", sizeof(what));
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        SDL_strlcpy(what, "integer divide by zero", sizeof(what));
        break;
    default:
        SDL_snprintf(what, sizeof(what), "exception 0x%08lx", (unsigned long)r->ExceptionCode);
    }
    nw_crash_line("CRASH: %s at %s on thread %s", what, nw_crash_where(r->ExceptionAddress, false),
                  nw_log_thread_name());
    char dump[MAX_PATH * 3] = "";
    if (r->ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
#if defined(_M_X64) || defined(__x86_64__)
        CONTEXT c = *e->ContextRecord;
        nw_crash_stack(&c);
#endif
        nw_crash_minidump(e, dump, sizeof(dump));
    }
    nw_crash_tail(dump);
    if (nw_crash.test) {
        TerminateProcess(GetCurrentProcess(), 3);
    }
    return nw_crash.next ? nw_crash.next(e) : EXCEPTION_CONTINUE_SEARCH;
}

static void nw_crash_abort(int sig)
{
    if (nw_crash_enter()) {
        nw_crash_line("CRASH: abort on thread %s", nw_log_thread_name());
        nw_crash_here();
        nw_crash_tail(NULL);
    }
    signal(sig, nw_crash.next_abort);
}

static void nw_crash_invalid_parameter(const wchar_t *expr, const wchar_t *fn, const wchar_t *file, unsigned line,
                                       uintptr_t reserved)
{
    (void)expr;
    (void)fn;
    (void)file;
    (void)line;
    (void)reserved;
    if (nw_crash_enter()) {
        nw_crash_line("CRASH: invalid parameter to a C runtime function on thread %s", nw_log_thread_name());
        nw_crash_here();
        nw_crash_tail(NULL);
    }
    TerminateProcess(GetCurrentProcess(), 3);
}

static int nw_crash_strcmp(const void *a, const void *b)
{
    return SDL_strcmp(*(char *const *)a, *(char *const *)b);
}
#else
static const int nw_crash_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };

static void nw_crash_signal(int sig, siginfo_t *info, void *ctx)
{
    if (nw_crash_enter()) {
        void *at = NULL;
#if defined(__linux__) && defined(__x86_64__)
        at = (void *)((ucontext_t *)ctx)->uc_mcontext.gregs[REG_RIP];
#else
        (void)ctx;
#endif
        char what[96];
        switch (sig) {
        case SIGSEGV:
            SDL_snprintf(what, sizeof(what), "segmentation fault accessing %p", info->si_addr);
            break;
        case SIGBUS:
            SDL_snprintf(what, sizeof(what), "bus error accessing %p", info->si_addr);
            break;
        case SIGILL:
            SDL_strlcpy(what, "illegal instruction", sizeof(what));
            break;
        case SIGFPE:
            SDL_strlcpy(what, "arithmetic exception", sizeof(what));
            break;
        default:
            SDL_strlcpy(what, "abort", sizeof(what));
        }
        if (at) {
            nw_crash_line("CRASH: %s at %s on thread %s", what, nw_crash_where(at, false), nw_log_thread_name());
        } else {
            nw_crash_line("CRASH: %s on thread %s", what, nw_log_thread_name());
        }
        void *frames[NW_CRASH_FRAMES + 8];
        int n = backtrace(frames, (int)SDL_arraysize(frames)), first = 0;
        for (int i = 0; i < n; i++) {
            if (frames[i] == at) {
                first = i;
                break;
            }
        }
        nw_crash_frames(frames, n, first);
        nw_crash_tail(NULL);
        if (nw_crash.test) {
            _exit(3);
        }
    }
    for (size_t i = 0; i < SDL_arraysize(nw_crash_signals); i++) {
        if (nw_crash_signals[i] == sig) {
            sigaction(sig, &nw_crash.next[i], NULL);
        }
    }
    if (info->si_code <= 0) {
        raise(sig);
    }
}
#endif

void nw_crash_install(void)
{
    static bool installed;
    if (installed) {
        return;
    }
    installed = true;
    NwCrashTable t = { 0 };
    nw_crash_load_symbols(&t);
    if (t.n) {
        SDL_qsort(t.syms, t.n, sizeof(*t.syms), nw_crash_cmp);
        nw_crash.syms = t.syms;
        nw_crash.nsyms = t.n;
        nw_crash.names = t.names;
    } else {
        SDL_free(t.syms);
        SDL_free(t.names);
    }
#ifdef _WIN32
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (dbghelp) {
        nw_crash.write_dump = (NwWriteDump)(void *)GetProcAddress(dbghelp, "MiniDumpWriteDump");
    }
    nw_crash.next = SetUnhandledExceptionFilter(nw_crash_exception);
    nw_crash.next_abort = signal(SIGABRT, nw_crash_abort);
    _set_invalid_parameter_handler(nw_crash_invalid_parameter);
#else
    void *prime[1];
    backtrace(prime, 1);
    struct sigaction sa = { 0 };
    sa.sa_sigaction = nw_crash_signal;
    sa.sa_flags = SA_SIGINFO;
    for (size_t i = 0; i < SDL_arraysize(nw_crash_signals); i++) {
        sigaction(nw_crash_signals[i], &sa, &nw_crash.next[i]);
    }
#endif
}

void nw_crash_sink(intptr_t file, bool to_stderr)
{
    nw_crash.file = file;
    nw_crash.to_stderr = to_stderr;
}

void nw_crash_dumps(const char *dir)
{
#ifdef _WIN32
    nw_crash.dump_dir[0] = 0;
    nw_crash.dump_shown[0] = '\0';
    char *full = NULL;
    char sep = dir && SDL_strchr(dir, '\\') ? '\\' : '/';
    if (!dir || !nw_crash.write_dump || !SDL_CreateDirectory(dir) || SDL_asprintf(&full, "%s%c", dir, sep) < 0 ||
        !MultiByteToWideChar(CP_UTF8, 0, full, -1, nw_crash.dump_dir, MAX_PATH)) {
        nw_crash.dump_dir[0] = 0;
        SDL_free(full);
        return;
    }
    char *shown = nw_log_redact(full, NULL);
    SDL_strlcpy(nw_crash.dump_shown, shown ? shown : full, sizeof(nw_crash.dump_shown));
    SDL_free(shown);
    SDL_free(full);
    int count = 0;
    char **found = SDL_GlobDirectory(dir, "nuworld-*.dmp", 0, &count);
    if (found) {
        SDL_qsort(found, (size_t)count, sizeof(*found), nw_crash_strcmp);
        for (int i = 0; i + 2 < count; i++) {
            char *path = NULL;
            if (SDL_asprintf(&path, "%s%c%s", dir, sep, found[i]) >= 0) {
                SDL_RemovePath(path);
            }
            SDL_free(path);
        }
        SDL_free(found);
    }
#else
    (void)dir;
#endif
}

void nw_crash_on_state(size_t (*fn)(char *out, size_t size))
{
    nw_crash.state = fn;
}

void nw_crash_phase(const char *what, unsigned i, unsigned n)
{
    nw_crash.phase_i = i;
    nw_crash.phase_n = n;
    nw_crash.phase = what;
}

void nw_crash_fatal(const char *message)
{
    char *shown = nw_log_redact(message, NULL);
    if (nw_crash_enter()) {
        nw_crash_line("FATAL: %s on thread %s", shown ? shown : message, nw_log_thread_name());
        nw_crash_here();
        nw_crash_tail(NULL);
        nw_crash_leave();
    }
    SDL_free(shown);
}

static __attribute__((noinline)) void nw_crash_test_fault(void)
{
    *(volatile int *)(uintptr_t)16 = 0;
}

_Noreturn void nw_crash_test(const char *kind)
{
    nw_crash.test = true;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    nw_log_info(NW_LOG_APP, "crash test: %s", kind);
    nw_log_debug(NW_LOG_APP, "crash test: a debug line the report's last lines hold");
    if (SDL_strcmp(kind, "fatal") == 0) {
        nw_crash_fatal("crash test");
        exit(1);
    }
    if (SDL_strcmp(kind, "abort") != 0) {
        nw_crash_test_fault();
    }
    abort();
}
