/* platform/discord.c: the frame and JSON wire format, and the worker against a local stand-in for the client:
   handshake, READY, the newest activity, PING, reconnects, switching off and the client closing. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform/discord.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* The client's end, one connection at a time. */
typedef struct Server {
#ifdef _WIN32
    HANDLE pipe;
#else
    int listener, fd;
#endif
    uint8_t in[70000];
    size_t have;
} Server;

#ifdef _WIN32
static bool server_open(Server *s, const char *path)
{
    s->pipe = CreateNamedPipeA(path, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1,
                               65536, 65536, 0, NULL);
    return s->pipe != INVALID_HANDLE_VALUE;
}

static bool server_accept(Server *s, int ms)
{
    s->have = 0;
    for (uint64_t until = SDL_GetTicks() + ms; SDL_GetTicks() < until; SDL_Delay(5)) {
        if (ConnectNamedPipe(s->pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            return true;
        }
    }
    return false;
}

/* Bytes read, 0 none yet, -1 closed. */
static int server_recv(Server *s)
{
    DWORD got = 0;
    if (ReadFile(s->pipe, s->in + s->have, (DWORD)(sizeof(s->in) - s->have), &got, NULL)) {
        return (int)got;
    }
    return GetLastError() == ERROR_NO_DATA ? 0 : -1;
}

static bool server_send(Server *s, const uint8_t *p, size_t n)
{
    DWORD wrote = 0;
    return WriteFile(s->pipe, p, (DWORD)n, &wrote, NULL) && wrote == n;
}

static void server_drop(Server *s)
{
    DisconnectNamedPipe(s->pipe);
}

static void server_close(Server *s)
{
    CloseHandle(s->pipe);
}
#else
static bool server_open(Server *s, const char *path)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    SDL_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));
    unlink(path);
    s->fd = -1;
    s->listener = socket(AF_UNIX, SOCK_STREAM, 0);
    return s->listener >= 0 && bind(s->listener, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
           listen(s->listener, 1) == 0;
}

static bool server_accept(Server *s, int ms)
{
    struct pollfd p = { .fd = s->listener, .events = POLLIN };
    s->have = 0;
    if (poll(&p, 1, ms) != 1) {
        return false;
    }
    s->fd = accept(s->listener, NULL, NULL);
    return s->fd >= 0;
}

static int server_recv(Server *s)
{
    struct pollfd p = { .fd = s->fd, .events = POLLIN };
    if (poll(&p, 1, 0) != 1) {
        return 0;
    }
    ssize_t got = recv(s->fd, s->in + s->have, sizeof(s->in) - s->have, 0);
    return got > 0 ? (int)got : -1;
}

static bool server_send(Server *s, const uint8_t *p, size_t n)
{
    return send(s->fd, p, n, 0) == (ssize_t)n;
}

static void server_drop(Server *s)
{
    close(s->fd);
    s->fd = -1;
}

static void server_close(Server *s)
{
    if (s->fd >= 0) {
        close(s->fd);
    }
    close(s->listener);
}
#endif

/* The next frame within ms; false when none came or the client closed. */
static bool server_read(Server *s, uint32_t *op, char *json, size_t cap, int ms)
{
    for (uint64_t until = SDL_GetTicks() + ms;; SDL_Delay(5)) {
        uint32_t len;
        int size = nw_discord_unframe(s->in, s->have, op, &len);
        if (size > 0) {
            size_t n = SDL_min((size_t)len, cap - 1);
            SDL_memcpy(json, s->in + 8, n);
            json[n] = '\0';
            SDL_memmove(s->in, s->in + size, s->have - (size_t)size);
            s->have -= (size_t)size;
            return true;
        }
        int got = server_recv(s);
        if (got < 0 || size < 0 || SDL_GetTicks() >= until) {
            return false;
        }
        s->have += (size_t)got;
    }
}

static bool server_write(Server *s, NwDiscordOp op, const char *json)
{
    uint8_t frame[1024];
    size_t n = nw_discord_frame(frame, sizeof(frame), op, json, SDL_strlen(json));
    return n && server_send(s, frame, n);
}

/* The client closed within ms. */
static bool server_closed(Server *s, int ms)
{
    for (uint64_t until = SDL_GetTicks() + ms; SDL_GetTicks() < until; SDL_Delay(5)) {
        int got = server_recv(s);
        if (got < 0) {
            return true;
        }
        s->have = 0;
    }
    return false;
}

static bool wait_ready(bool ready, int ms)
{
    for (uint64_t until = SDL_GetTicks() + ms; SDL_GetTicks() < until; SDL_Delay(5)) {
        if (nw_discord_ready() == ready) {
            return true;
        }
    }
    return false;
}

static NwActivity activity(const char *details)
{
    NwActivity a;
    SDL_zero(a);
    SDL_strlcpy(a.details, details, sizeof(a.details));
    return a;
}

static void test_wire(void)
{
    uint8_t frame[16];
    uint32_t op, len;
    CHECK(nw_discord_frame(frame, sizeof(frame), NW_DISCORD_FRAME, "{}", 2) == 10);
    CHECK(memcmp(frame, "\x01\0\0\0\x02\0\0\0{}", 10) == 0);
    CHECK(nw_discord_frame(frame, 9, NW_DISCORD_FRAME, "{}", 2) == 0);
    CHECK(nw_discord_unframe(frame, 9, &op, &len) == 0);
    CHECK(nw_discord_unframe(frame, 7, &op, &len) == 0);
    CHECK(nw_discord_unframe(frame, 10, &op, &len) == 10 && op == NW_DISCORD_FRAME && len == 2);
    CHECK(nw_discord_unframe((const uint8_t *)"\x03\0\0\0\x01\0\x01\0", 8, &op, &len) == -1);

    char json[1024];
    CHECK(nw_discord_handshake(json, sizeof(json), "1234") && strcmp(json, "{\"v\":1,\"client_id\":\"1234\"}") == 0);
    CHECK(!nw_discord_handshake(json, 20, "1234"));

    NwActivity a = activity("Say \"hi\"\\\n");
    CHECK(nw_discord_activity(json, sizeof(json), &a, 7, 3));
    CHECK(strcmp(json, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":7,\"activity\":{\"type\":0,\"details\":\"Say "
                       "\\\"hi\\\"\\\\\\u000a\"}},\"nonce\":\"3\"}") == 0);
    SDL_strlcpy(a.name, "CrashWOC: NuWorld", sizeof(a.name));
    SDL_strlcpy(a.details, "Arctic Antics", sizeof(a.details));
    SDL_strlcpy(a.state, "Crystal \xC2\xB7 2 Gems", sizeof(a.state));
    a.start_ms = 1762400000123LL;
    SDL_strlcpy(a.large_image, "level_07", sizeof(a.large_image));
    SDL_strlcpy(a.large_text, "NuWorld 0.7.0", sizeof(a.large_text));
    SDL_strlcpy(a.small_image, "crash", sizeof(a.small_image));
    SDL_strlcpy(a.buttons[0].label, "Get NuWorld", sizeof(a.buttons[0].label));
    SDL_strlcpy(a.buttons[0].url, "https://example.org/a", sizeof(a.buttons[0].url));
    SDL_strlcpy(a.buttons[1].label, "B", sizeof(a.buttons[1].label));
    SDL_strlcpy(a.buttons[1].url, "https://example.org/b", sizeof(a.buttons[1].url));
    a.nbuttons = 2;
    CHECK(nw_discord_activity(json, sizeof(json), &a, 99, 12));
    CHECK(strcmp(json, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":99,\"activity\":{\"type\":0,"
                       "\"name\":\"CrashWOC: NuWorld\",\"details\":\"Arctic Antics\","
                       "\"state\":\"Crystal \xC2\xB7 2 Gems\",\"timestamps\":{\"start\":1762400000123},"
                       "\"assets\":{\"large_image\":\"level_07\",\"large_text\":\"NuWorld 0.7.0\","
                       "\"small_image\":\"crash\"},\"buttons\":[{\"label\":\"Get NuWorld\",\"url\":"
                       "\"https://example.org/a\"},{\"label\":\"B\",\"url\":\"https://example.org/b\"}]}},"
                       "\"nonce\":\"12\"}") == 0);
    CHECK(!nw_discord_activity(json, 100, &a, 99, 12));

    char value[32];
    CHECK(nw_discord_field("{\"cmd\":\"DISPATCH\",\"evt\":\"READY\"}", "evt", value, sizeof(value)) &&
          strcmp(value, "READY") == 0);
    CHECK(!nw_discord_field("{\"evt\":null,\"nonce\":\"1\"}", "evt", value, sizeof(value)));
    CHECK(!nw_discord_field("{\"cmd\":\"DISPATCH\"}", "evt", value, sizeof(value)));
    CHECK(nw_discord_field("{\"message\" : \"a \\\"b\\\"\"}", "message", value, sizeof(value)) &&
          strcmp(value, "a \"b\"") == 0);
    CHECK(nw_discord_field("{\"message\":\"0123456789\"}", "message", value, 5) && strcmp(value, "0123") == 0);
}

/* Handshake to READY; false when the worker did not come. */
static bool serve_handshake(Server *s)
{
    uint32_t op;
    char json[1024];
    if (!server_accept(s, 3000)) {
        return false;
    }
    CHECK(server_read(s, &op, json, sizeof(json), 3000) && op == NW_DISCORD_HANDSHAKE &&
          strcmp(json, "{\"v\":1,\"client_id\":\"42\"}") == 0);
    CHECK(!nw_discord_ready());
    CHECK(server_write(s, NW_DISCORD_FRAME, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}"));
    return true;
}

static void test_worker(void)
{
    char path[128];
#ifdef _WIN32
    SDL_snprintf(path, sizeof(path), "\\\\.\\pipe\\nuworld-test-discord-%lu", (unsigned long)GetCurrentProcessId());
#else
    SDL_snprintf(path, sizeof(path), "/tmp/nuworld-test-discord-%ld", (long)getpid());
#endif
    static Server s;
    CHECK(server_open(&s, path));
    CHECK(nw_discord_start(&(NwDiscordConfig){ .app_id = "42", .endpoint = path, .send_ms = 1000, .retry_ms = 50 }));
    CHECK(!nw_discord_start(&(NwDiscordConfig){ .app_id = "42", .endpoint = path }));
    NwActivity a = activity("First");
    nw_discord_set(&a);
    CHECK(!server_accept(&s, 200)); /* off until enabled */
    nw_discord_enable(true);

    uint32_t op;
    char json[1024];
    CHECK(serve_handshake(&s));
    CHECK(server_read(&s, &op, json, sizeof(json), 3000) && op == NW_DISCORD_FRAME &&
          SDL_strstr(json, "\"cmd\":\"SET_ACTIVITY\"") && SDL_strstr(json, "\"details\":\"First\""));
    CHECK(wait_ready(true, 1000));

    /* Inside send_ms only the newest goes out. */
    a = activity("Second");
    nw_discord_set(&a);
    a = activity("Third");
    nw_discord_set(&a);
    CHECK(server_read(&s, &op, json, sizeof(json), 3000) && SDL_strstr(json, "\"details\":\"Third\""));
    nw_discord_set(&a);
    CHECK(!server_read(&s, &op, json, sizeof(json), 1300)); /* the same activity is not sent again */

    CHECK(server_write(&s, NW_DISCORD_PING, "{\"p\":7}"));
    CHECK(server_read(&s, &op, json, sizeof(json), 3000) && op == NW_DISCORD_PONG && strcmp(json, "{\"p\":7}") == 0);

    /* The client went away: found again, and told the activity again. */
    server_drop(&s);
    CHECK(serve_handshake(&s));
    CHECK(server_read(&s, &op, json, sizeof(json), 3000) && SDL_strstr(json, "\"details\":\"Third\""));

    nw_discord_enable(false);
    CHECK(server_closed(&s, 3000));
    CHECK(wait_ready(false, 1000));
    server_drop(&s);

    /* The client turns the handshake down. */
    nw_discord_enable(true);
    CHECK(server_accept(&s, 3000));
    CHECK(server_read(&s, &op, json, sizeof(json), 3000) && op == NW_DISCORD_HANDSHAKE);
    CHECK(server_write(&s, NW_DISCORD_CLOSE, "{\"code\":4000,\"message\":\"Invalid Client ID\"}"));
    CHECK(server_closed(&s, 3000));
    CHECK(!nw_discord_ready());

    uint64_t stop = SDL_GetTicks();
    nw_discord_stop();
    CHECK(SDL_GetTicks() - stop < 600);
    nw_discord_stop();
    server_close(&s);
#ifndef _WIN32
    unlink(path);
#endif
}

int main(void)
{
    test_wire();
    test_worker();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
