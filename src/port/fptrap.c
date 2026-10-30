/* NUWORLD_FP_TRAPS: trap invalid operations (a NaN, which the EE FPU never makes) and divides by zero (FLT_MAX on the
   EE); 1 exits on the first, all reports each trapping instruction once (llvm-symbolizer offsets) and steps past it. */
#ifndef _WIN32
#define _GNU_SOURCE /* dladdr, REG_EFL */
#endif
#include "port/fptrap.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>

#define NW_FP_TRAP_MASK 0x280u /* MXCSR IM | ZM */
#define NW_FP_TRAP_FLAG 0x100u /* EFLAGS TF */

static bool nw_fp_all;
static SDL_AtomicInt nw_fp_nseen;
static uintptr_t nw_fp_seen[256];

/* Ends the program from a trap handler, past atexit (exit code 3). */
static _Noreturn void nw_fp_stop(void);

/* Prints the trap at at the first time; false to stop. */
static bool nw_fp_report(void *at, bool invalid, const char *module, uintptr_t base)
{
    int n = SDL_GetAtomicInt(&nw_fp_nseen);
    for (int i = 0; i < n && i < (int)SDL_arraysize(nw_fp_seen); i++) {
        if (nw_fp_seen[i] == (uintptr_t)at) {
            return true;
        }
    }
    if (n < (int)SDL_arraysize(nw_fp_seen)) {
        nw_fp_seen[n] = (uintptr_t)at;
        SDL_SetAtomicInt(&nw_fp_nseen, n + 1);
    }
    fprintf(stderr, "fp trap: %s at %p (%s+0x%llx)\n", invalid ? "invalid operation" : "divide by zero", at, module,
            (unsigned long long)((uintptr_t)at - base));
    fflush(stderr);
    return nw_fp_all;
}

#ifdef _WIN32
#include <windows.h>

static _Noreturn void nw_fp_stop(void)
{
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) {
    }
}

static LONG CALLBACK nw_fp_trap(EXCEPTION_POINTERS *e)
{
    DWORD code = e->ExceptionRecord->ExceptionCode;
    CONTEXT *c = e->ContextRecord;
    if (code == EXCEPTION_SINGLE_STEP && nw_fp_all && (c->MxCsr & NW_FP_TRAP_MASK) == NW_FP_TRAP_MASK) {
        c->MxCsr = c->FltSave.MxCsr = (c->MxCsr & ~0x3Fu) & ~NW_FP_TRAP_MASK;
        c->EFlags &= ~NW_FP_TRAP_FLAG;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != STATUS_FLOAT_MULTIPLE_TRAPS && code != EXCEPTION_FLT_INVALID_OPERATION &&
        code != EXCEPTION_FLT_DIVIDE_BY_ZERO) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    void *at = e->ExceptionRecord->ExceptionAddress;
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, at,
                       &module);
    GetModuleFileNameA(module, name, sizeof(name));
    if (!nw_fp_report(at, c->MxCsr & 1, name, (uintptr_t)module)) {
        nw_fp_stop();
    }
    c->MxCsr = c->FltSave.MxCsr = (c->MxCsr & ~0x3Fu) | NW_FP_TRAP_MASK;
    c->EFlags |= NW_FP_TRAP_FLAG;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void nw_fp_trap_install(void)
{
    AddVectoredExceptionHandler(1, nw_fp_trap);
}
#else
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>

static _Noreturn void nw_fp_stop(void)
{
    _Exit(3);
}

static void nw_fp_trap(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uc = ctx;
    if (sig == SIGTRAP) {
        uc->uc_mcontext.fpregs->mxcsr = (uc->uc_mcontext.fpregs->mxcsr & ~0x3Fu) & ~NW_FP_TRAP_MASK;
        uc->uc_mcontext.gregs[REG_EFL] &= ~(greg_t)NW_FP_TRAP_FLAG;
        return;
    }
    Dl_info dl = { 0 };
    dladdr(info->si_addr, &dl);
    if (!nw_fp_report(info->si_addr, info->si_code != FPE_FLTDIV, dl.dli_fname ? dl.dli_fname : "?",
                      (uintptr_t)dl.dli_fbase)) {
        nw_fp_stop();
    }
    uc->uc_mcontext.fpregs->mxcsr = (uc->uc_mcontext.fpregs->mxcsr & ~0x3Fu) | NW_FP_TRAP_MASK;
    uc->uc_mcontext.gregs[REG_EFL] |= NW_FP_TRAP_FLAG;
}

static void nw_fp_trap_install(void)
{
    struct sigaction sa = { 0 };
    sa.sa_sigaction = nw_fp_trap;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGFPE, &sa, NULL);
    if (nw_fp_all) {
        sigaction(SIGTRAP, &sa, NULL);
    }
}
#endif

void nw_fp_traps_enter(void)
{
    static SDL_InitState once;
    static bool on;
    if (SDL_ShouldInit(&once)) {
        const char *env = SDL_getenv("NUWORLD_FP_TRAPS");
        on = env && *env && SDL_strcmp(env, "0") != 0;
        nw_fp_all = on && SDL_strcmp(env, "all") == 0;
        if (on) {
            nw_fp_trap_install();
        }
        SDL_SetInitialized(&once, true);
    }
    if (on) {
        _mm_setcsr((_mm_getcsr() & ~0x3Fu) & ~NW_FP_TRAP_MASK);
    }
}
#else
void nw_fp_traps_enter(void)
{
}
#endif
