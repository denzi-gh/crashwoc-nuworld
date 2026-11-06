#include "platform/discord.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define NW_DISCORD_MAX (64 * 1024)
#define NW_DISCORD_JSON 4096
#define NW_DISCORD_SHAKE_MS 5000
#define NW_DISCORD_POLL_MS 100

static void nw_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t nw_get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

size_t nw_discord_frame(uint8_t *out, size_t cap, NwDiscordOp op, const char *json, size_t len)
{
    if (len > NW_DISCORD_MAX || cap < 8 + len) {
        return 0;
    }
    nw_put32(out, (uint32_t)op);
    nw_put32(out + 4, (uint32_t)len);
    SDL_memcpy(out + 8, json, len);
    return 8 + len;
}

int nw_discord_unframe(const uint8_t *in, size_t n, uint32_t *op, uint32_t *len)
{
    if (n < 8) {
        return 0;
    }
    *op = nw_get32(in);
    *len = nw_get32(in + 4);
    if (*len > NW_DISCORD_MAX) {
        return -1;
    }
    return n >= 8 + (size_t)*len ? 8 + (int)*len : 0;
}

typedef struct NwJson {
    char *out;
    size_t cap, len;
    bool ok;
} NwJson;

static NwJson nw_json(char *out, size_t cap)
{
    if (cap) {
        out[0] = '\0';
    }
    return (NwJson){ out, cap, 0, cap > 0 };
}

static void nw_json_raw(NwJson *j, const char *s)
{
    size_t n = SDL_strlen(s);
    if (!j->ok || j->len + n >= j->cap) {
        j->ok = false;
        return;
    }
    SDL_memcpy(j->out + j->len, s, n + 1);
    j->len += n;
}

static void nw_json_string(NwJson *j, const char *s)
{
    nw_json_raw(j, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char esc[8] = { (char)c };
        if (c == '"' || c == '\\') {
            esc[0] = '\\';
            esc[1] = (char)c;
        } else if (c < 0x20) {
            SDL_snprintf(esc, sizeof(esc), "\\u%04x", c);
        }
        nw_json_raw(j, esc);
    }
    nw_json_raw(j, "\"");
}

/* "key":"value" after a comma unless first; nothing for "". */
static void nw_json_field(NwJson *j, bool *first, const char *key, const char *value)
{
    if (!value[0]) {
        return;
    }
    nw_json_raw(j, *first ? "\"" : ",\"");
    *first = false;
    nw_json_raw(j, key);
    nw_json_raw(j, "\":");
    nw_json_string(j, value);
}

/* "key": after a comma unless first. */
static void nw_json_key(NwJson *j, bool *first, const char *key)
{
    nw_json_raw(j, *first ? "\"" : ",\"");
    *first = false;
    nw_json_raw(j, key);
    nw_json_raw(j, "\":");
}

bool nw_discord_handshake(char *out, size_t cap, const char *app_id)
{
    NwJson j = nw_json(out, cap);
    nw_json_raw(&j, "{\"v\":1,\"client_id\":");
    nw_json_string(&j, app_id);
    nw_json_raw(&j, "}");
    return j.ok;
}

bool nw_discord_activity(char *out, size_t cap, const NwActivity *a, uint64_t pid, uint32_t nonce)
{
    NwJson j = nw_json(out, cap);
    char num[32];
    bool first = true;
    SDL_snprintf(num, sizeof(num), "%llu", (unsigned long long)pid);
    nw_json_raw(&j, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":");
    nw_json_raw(&j, num);
    nw_json_raw(&j, ",\"activity\":{");
    nw_json_field(&j, &first, "details", a->details);
    nw_json_field(&j, &first, "state", a->state);
    if (a->start_ms > 0) {
        nw_json_key(&j, &first, "timestamps");
        SDL_snprintf(num, sizeof(num), "%lld", (long long)a->start_ms);
        nw_json_raw(&j, "{\"start\":");
        nw_json_raw(&j, num);
        nw_json_raw(&j, "}");
    }
    if (a->large_image[0] || a->large_text[0] || a->small_image[0] || a->small_text[0]) {
        bool inner = true;
        nw_json_key(&j, &first, "assets");
        nw_json_raw(&j, "{");
        nw_json_field(&j, &inner, "large_image", a->large_image);
        nw_json_field(&j, &inner, "large_text", a->large_text);
        nw_json_field(&j, &inner, "small_image", a->small_image);
        nw_json_field(&j, &inner, "small_text", a->small_text);
        nw_json_raw(&j, "}");
    }
    if (a->nbuttons > 0) {
        nw_json_key(&j, &first, "buttons");
        nw_json_raw(&j, "[");
        for (int i = 0; i < SDL_min(a->nbuttons, 2); i++) {
            bool inner = true;
            nw_json_raw(&j, i ? ",{" : "{");
            nw_json_field(&j, &inner, "label", a->buttons[i].label);
            nw_json_field(&j, &inner, "url", a->buttons[i].url);
            nw_json_raw(&j, "}");
        }
        nw_json_raw(&j, "]");
    }
    SDL_snprintf(num, sizeof(num), "%u", (unsigned)nonce);
    nw_json_raw(&j, "}},\"nonce\":\"");
    nw_json_raw(&j, num);
    nw_json_raw(&j, "\"}");
    return j.ok;
}

bool nw_discord_field(const char *json, const char *key, char *out, size_t cap)
{
    char pattern[40];
    if (!cap || SDL_snprintf(pattern, sizeof(pattern), "\"%s\"", key) >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = SDL_strstr(json, pattern);
    if (!p) {
        return false;
    }
    p += SDL_strlen(pattern);
    while (*p == ' ') {
        p++;
    }
    if (*p++ != ':') {
        return false;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p++ != '"') {
        return false;
    }
    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            c = *p++;
        }
        if (n + 1 < cap) {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return *p == '"';
}

#ifdef _WIN32
typedef HANDLE NwIpc;
#define NW_IPC_NONE INVALID_HANDLE_VALUE

static NwIpc nw_ipc_open(const char *path)
{
    return CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static bool nw_ipc_write(NwIpc ipc, const uint8_t *p, size_t n)
{
    while (n) {
        DWORD wrote;
        if (!WriteFile(ipc, p, (DWORD)n, &wrote, NULL)) {
            return false;
        }
        p += wrote;
        n -= wrote;
    }
    return true;
}

/* Bytes read, 0 for none waiting, -1 once closed. */
static int nw_ipc_read(NwIpc ipc, uint8_t *p, size_t cap)
{
    DWORD avail = 0, got = 0;
    if (!PeekNamedPipe(ipc, NULL, 0, NULL, &avail, NULL)) {
        return -1;
    }
    if (!avail || !cap) {
        return 0;
    }
    return ReadFile(ipc, p, (DWORD)SDL_min((size_t)avail, cap), &got, NULL) ? (int)got : -1;
}

static void nw_ipc_close(NwIpc ipc)
{
    CloseHandle(ipc);
}

static uint64_t nw_ipc_pid(void)
{
    return GetCurrentProcessId();
}
#else
typedef int NwIpc;
#define NW_IPC_NONE (-1)
#ifdef MSG_NOSIGNAL
#define NW_IPC_SEND MSG_NOSIGNAL
#else
#define NW_IPC_SEND 0 /* SO_NOSIGPIPE instead */
#endif

static NwIpc nw_ipc_open(const char *path)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    if (SDL_strlcpy(addr.sun_path, path, sizeof(addr.sun_path)) >= sizeof(addr.sun_path)) {
        return -1;
    }
#ifdef SOCK_CLOEXEC
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
    if (fd < 0) {
        return -1;
    }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool nw_ipc_write(NwIpc ipc, const uint8_t *p, size_t n)
{
    while (n) {
        ssize_t wrote = send(ipc, p, n, NW_IPC_SEND);
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote <= 0) {
            return false;
        }
        p += wrote;
        n -= (size_t)wrote;
    }
    return true;
}

static int nw_ipc_read(NwIpc ipc, uint8_t *p, size_t cap)
{
    if (!cap) {
        return 0;
    }
    ssize_t got = recv(ipc, p, cap, MSG_DONTWAIT);
    if (got > 0) {
        return (int)got;
    }
    return got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

static void nw_ipc_close(NwIpc ipc)
{
    close(ipc);
}

static uint64_t nw_ipc_pid(void)
{
    return (uint64_t)getpid();
}
#endif

/* The client's pipe or socket: endpoint, else discord-ipc-0..9 where Discord, its Flatpak and its Snap put it. */
static NwIpc nw_ipc_connect(const char *endpoint)
{
    if (endpoint[0]) {
        return nw_ipc_open(endpoint);
    }
    char path[512];
#ifdef _WIN32
    for (int n = 0; n < 10; n++) {
        SDL_snprintf(path, sizeof(path), "\\\\.\\pipe\\discord-ipc-%d", n);
        NwIpc ipc = nw_ipc_open(path);
        if (ipc != NW_IPC_NONE) {
            return ipc;
        }
    }
#else
    static const char *const vars[] = { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" };
    static const char *const subdirs[] = { "", "app/com.discordapp.Discord/", "snap.discord/" };
    for (size_t v = 0; v <= SDL_arraysize(vars); v++) {
        const char *dir = v < SDL_arraysize(vars) ? SDL_getenv(vars[v]) : "/tmp";
        if (!dir || !dir[0]) {
            continue;
        }
        size_t len = SDL_strlen(dir);
        const char *slash = dir[len - 1] == '/' ? "" : "/";
        for (size_t s = 0; s < SDL_arraysize(subdirs); s++) {
            for (int n = 0; n < 10; n++) {
                SDL_snprintf(path, sizeof(path), "%s%s%sdiscord-ipc-%d", dir, slash, subdirs[s], n);
                NwIpc ipc = nw_ipc_open(path);
                if (ipc != NW_IPC_NONE) {
                    return ipc;
                }
            }
        }
    }
#endif
    return NW_IPC_NONE;
}

typedef struct NwDiscordWorker {
    SDL_Thread *thread;
    SDL_Mutex *lock;
    SDL_Condition *wake;
    SDL_Semaphore *done;
    char app_id[32], endpoint[256];
    uint32_t send_ms, retry_ms;
    SDL_AtomicInt ready;
    /* lock */
    bool quit, enabled, finished, orphan, has_activity;
    NwActivity activity;
} NwDiscordWorker;

/* The worker's connection. */
typedef struct NwLink {
    NwIpc ipc;
    bool ready, missing_told, sent_any;
    uint64_t retry_at, shake_until, sent_at;
    uint32_t nonce;
    NwActivity sent;
    char told[NW_DISCORD_TEXT]; /* the last error logged */
    char json[NW_DISCORD_JSON];
    size_t have;
    uint8_t in[8 + NW_DISCORD_MAX + 1];
    uint8_t out[8 + NW_DISCORD_MAX];
} NwLink;

static NwDiscordWorker *nw_discord;

static void nw_discord_free(NwDiscordWorker *w)
{
    SDL_DestroySemaphore(w->done);
    SDL_DestroyCondition(w->wake);
    SDL_DestroyMutex(w->lock);
    SDL_free(w);
}

static bool nw_activity_equal(const NwActivity *a, const NwActivity *b)
{
    if (SDL_strcmp(a->details, b->details) || SDL_strcmp(a->state, b->state) ||
        SDL_strcmp(a->large_image, b->large_image) || SDL_strcmp(a->large_text, b->large_text) ||
        SDL_strcmp(a->small_image, b->small_image) || SDL_strcmp(a->small_text, b->small_text) ||
        a->start_ms != b->start_ms || a->nbuttons != b->nbuttons) {
        return false;
    }
    for (int i = 0; i < SDL_min(a->nbuttons, 2); i++) {
        if (SDL_strcmp(a->buttons[i].label, b->buttons[i].label) || SDL_strcmp(a->buttons[i].url, b->buttons[i].url)) {
            return false;
        }
    }
    return true;
}

/* message once per distinct text. */
static void nw_link_tell(NwLink *l, const char *message)
{
    if (SDL_strcmp(l->told, message) != 0) {
        SDL_strlcpy(l->told, message, sizeof(l->told));
        SDL_Log("discord: %s", message);
    }
}

static void nw_link_close(NwDiscordWorker *w, NwLink *l, const char *why)
{
    if (l->ipc != NW_IPC_NONE) {
        nw_ipc_close(l->ipc);
        l->ipc = NW_IPC_NONE;
        if (why) {
            nw_link_tell(l, why);
        }
    }
    l->ready = false;
    l->sent_any = false;
    l->have = 0;
    l->retry_at = SDL_GetTicks() + w->retry_ms;
    SDL_SetAtomicInt(&w->ready, 0);
}

static bool nw_link_send(NwLink *l, NwDiscordOp op, const char *json, size_t len)
{
    size_t n = nw_discord_frame(l->out, sizeof(l->out), op, json, len);
    return n && nw_ipc_write(l->ipc, l->out, n);
}

static void nw_link_connect(NwDiscordWorker *w, NwLink *l)
{
    l->ipc = nw_ipc_connect(w->endpoint);
    if (l->ipc == NW_IPC_NONE) {
        if (!l->missing_told) {
            SDL_Log("discord: no Discord client running; looking again every %u s", (unsigned)(w->retry_ms / 1000));
            l->missing_told = true;
        }
        l->retry_at = SDL_GetTicks() + w->retry_ms;
        return;
    }
    l->shake_until = SDL_GetTicks() + NW_DISCORD_SHAKE_MS;
    if (!nw_discord_handshake(l->json, sizeof(l->json), w->app_id) ||
        !nw_link_send(l, NW_DISCORD_HANDSHAKE, l->json, SDL_strlen(l->json))) {
        nw_link_close(w, l, "the handshake failed");
    }
}

static void nw_link_frame(NwDiscordWorker *w, NwLink *l, uint32_t op, const char *json, size_t len)
{
    char evt[32], message[NW_DISCORD_TEXT];
    if (op == NW_DISCORD_PING) {
        if (!nw_link_send(l, NW_DISCORD_PONG, json, len)) {
            nw_link_close(w, l, "lost the connection");
        }
    } else if (op == NW_DISCORD_CLOSE) {
        nw_link_close(w, l, nw_discord_field(json, "message", message, sizeof(message)) ? message : "closed");
    } else if (op == NW_DISCORD_FRAME && nw_discord_field(json, "evt", evt, sizeof(evt))) {
        if (SDL_strcmp(evt, "READY") == 0 && !l->ready) {
            l->ready = true;
            l->missing_told = false;
            l->told[0] = '\0';
            SDL_SetAtomicInt(&w->ready, 1);
            SDL_Log("discord: connected");
        } else if (SDL_strcmp(evt, "ERROR") == 0) {
            nw_link_tell(l, nw_discord_field(json, "message", message, sizeof(message)) ? message : "error");
        }
    }
}

/* Every frame waiting, handled. */
static void nw_link_drain(NwDiscordWorker *w, NwLink *l)
{
    while (l->ipc != NW_IPC_NONE) {
        int got = nw_ipc_read(l->ipc, l->in + l->have, sizeof(l->in) - 1 - l->have);
        if (got < 0) {
            nw_link_close(w, l, l->ready ? "lost the connection" : "closed during the handshake");
            return;
        }
        if (got == 0) {
            return;
        }
        l->have += (size_t)got;
        uint32_t op, len;
        int size;
        while (l->ipc != NW_IPC_NONE && (size = nw_discord_unframe(l->in, l->have, &op, &len)) > 0) {
            uint8_t after = l->in[8 + len];
            l->in[8 + len] = '\0';
            nw_link_frame(w, l, op, (const char *)l->in + 8, len);
            if (l->ipc == NW_IPC_NONE) {
                return;
            }
            l->in[8 + len] = after;
            SDL_memmove(l->in, l->in + size, l->have - (size_t)size);
            l->have -= (size_t)size;
        }
        if (size < 0) {
            nw_link_close(w, l, "a frame too long");
            return;
        }
    }
}

/* The newest activity once send_ms since the last; the ms to wait. */
static Sint32 nw_link_update(NwDiscordWorker *w, NwLink *l, const NwActivity *a)
{
    uint64_t now = SDL_GetTicks();
    if (l->sent_any && nw_activity_equal(a, &l->sent)) {
        return NW_DISCORD_POLL_MS;
    }
    if (l->sent_any && now - l->sent_at < w->send_ms) {
        return (Sint32)SDL_min(w->send_ms - (now - l->sent_at), NW_DISCORD_POLL_MS);
    }
    if (!nw_discord_activity(l->json, sizeof(l->json), a, nw_ipc_pid(), ++l->nonce) ||
        !nw_link_send(l, NW_DISCORD_FRAME, l->json, SDL_strlen(l->json))) {
        nw_link_close(w, l, "lost the connection");
        return NW_DISCORD_POLL_MS;
    }
    l->sent = *a;
    l->sent_any = true;
    l->sent_at = now;
    return NW_DISCORD_POLL_MS;
}

static int SDLCALL nw_discord_main(void *data)
{
    NwDiscordWorker *w = data;
    NwLink *l = SDL_calloc(1, sizeof(*l));
    if (l) {
        l->ipc = NW_IPC_NONE;
    }
    SDL_LockMutex(w->lock);
    while (l && !w->quit) {
        bool enabled = w->enabled, has = w->has_activity;
        NwActivity activity = w->activity;
        SDL_UnlockMutex(w->lock);
        Sint32 wait = -1;
        if (!enabled) {
            nw_link_close(w, l, NULL);
            l->retry_at = 0;
            l->missing_told = false;
        } else {
            if (l->ipc == NW_IPC_NONE && SDL_GetTicks() >= l->retry_at) {
                nw_link_connect(w, l);
            }
            nw_link_drain(w, l);
            if (l->ipc == NW_IPC_NONE) {
                uint64_t now = SDL_GetTicks();
                wait = l->retry_at > now ? (Sint32)(l->retry_at - now) : 0;
            } else if (!l->ready) {
                if (SDL_GetTicks() >= l->shake_until) {
                    nw_link_close(w, l, "no answer to the handshake");
                }
                wait = NW_DISCORD_POLL_MS;
            } else {
                wait = has ? nw_link_update(w, l, &activity) : NW_DISCORD_POLL_MS;
            }
        }
        SDL_LockMutex(w->lock);
        if (!w->quit && wait != 0) {
            SDL_WaitConditionTimeout(w->wake, w->lock, wait);
        }
    }
    SDL_UnlockMutex(w->lock);
    if (l) {
        nw_link_close(w, l, NULL);
        SDL_free(l);
    }
    SDL_LockMutex(w->lock);
    bool orphan = w->orphan;
    w->finished = true;
    SDL_UnlockMutex(w->lock);
    if (orphan) {
        nw_discord_free(w);
    } else {
        SDL_SignalSemaphore(w->done);
    }
    return 0;
}

bool nw_discord_start(const NwDiscordConfig *config)
{
    if (nw_discord) {
        return SDL_SetError("discord: already started");
    }
    NwDiscordWorker *w = SDL_calloc(1, sizeof(*w));
    if (!w) {
        return false;
    }
    SDL_strlcpy(w->app_id, config->app_id ? config->app_id : "", sizeof(w->app_id));
    SDL_strlcpy(w->endpoint, config->endpoint ? config->endpoint : "", sizeof(w->endpoint));
    w->send_ms = config->send_ms ? config->send_ms : 5000;
    w->retry_ms = config->retry_ms ? config->retry_ms : 15000;
    w->lock = SDL_CreateMutex();
    w->wake = SDL_CreateCondition();
    w->done = SDL_CreateSemaphore(0);
    if (!w->lock || !w->wake || !w->done) {
        nw_discord_free(w);
        return false;
    }
    w->thread = SDL_CreateThread(nw_discord_main, "discord", w);
    if (!w->thread) {
        nw_discord_free(w);
        return false;
    }
    nw_discord = w;
    return true;
}

void nw_discord_enable(bool on)
{
    NwDiscordWorker *w = nw_discord;
    if (w) {
        SDL_LockMutex(w->lock);
        w->enabled = on;
        SDL_SignalCondition(w->wake);
        SDL_UnlockMutex(w->lock);
    }
}

void nw_discord_set(const NwActivity *activity)
{
    NwDiscordWorker *w = nw_discord;
    if (w) {
        SDL_LockMutex(w->lock);
        w->activity = *activity;
        w->has_activity = true;
        SDL_SignalCondition(w->wake);
        SDL_UnlockMutex(w->lock);
    }
}

bool nw_discord_ready(void)
{
    return nw_discord && SDL_GetAtomicInt(&nw_discord->ready);
}

void nw_discord_stop(void)
{
    NwDiscordWorker *w = nw_discord;
    if (!w) {
        return;
    }
    nw_discord = NULL;
    SDL_LockMutex(w->lock);
    w->quit = true;
    SDL_SignalCondition(w->wake);
    SDL_UnlockMutex(w->lock);
    if (!SDL_WaitSemaphoreTimeout(w->done, 500)) {
        /* Stuck in a write: the worker frees itself once it gets out. */
        SDL_LockMutex(w->lock);
        bool finished = w->finished;
        w->orphan = !finished;
        SDL_UnlockMutex(w->lock);
        if (!finished) {
            SDL_DetachThread(w->thread);
            return;
        }
    }
    SDL_WaitThread(w->thread, NULL);
    nw_discord_free(w);
}
