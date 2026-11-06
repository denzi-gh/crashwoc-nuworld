/* Discord Rich Presence over the Discord client's local IPC: a worker thread finds the client, hands it the newest
   activity at most once per send_ms and tries again every retry_ms while it is not running. */
#ifndef NUWORLD_PLATFORM_DISCORD_H
#define NUWORLD_PLATFORM_DISCORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NW_DISCORD_TEXT 128

/* "" leaves a field out. */
typedef struct NwActivity {
    char name[NW_DISCORD_TEXT]; /* "Playing <name>"; "" the application's own, which allows no ':' */
    char details[NW_DISCORD_TEXT], state[NW_DISCORD_TEXT];
    char large_image[32], large_text[NW_DISCORD_TEXT];
    char small_image[32], small_text[NW_DISCORD_TEXT];
    int64_t start_ms; /* the elapsed timer's start, ms since 1970; 0 none */
    struct {
        char label[33], url[128];
    } buttons[2];
    int nbuttons;
} NwActivity;

typedef struct NwDiscordConfig {
    const char *app_id;   /* the Developer Portal application's ID */
    const char *endpoint; /* one pipe or socket path instead of the client's discord-ipc-0..9 (tests) */
    uint32_t send_ms;     /* 0: 5000, inside Discord's 5 updates per 20 s */
    uint32_t retry_ms;    /* 0: 15000 */
} NwDiscordConfig;

typedef enum NwDiscordOp {
    NW_DISCORD_HANDSHAKE,
    NW_DISCORD_FRAME,
    NW_DISCORD_CLOSE,
    NW_DISCORD_PING,
    NW_DISCORD_PONG,
} NwDiscordOp;

/* Starts the worker, disabled; false with SDL_GetError(). */
bool nw_discord_start(const NwDiscordConfig *config);
/* Off closes the connection, which takes the presence off the profile. */
void nw_discord_enable(bool on);
/* Any thread; the newest wins. */
void nw_discord_set(const NwActivity *activity);
/* Waits up to 500 ms for the worker, then leaves it. */
void nw_discord_stop(void);
/* Connected and past the handshake. */
bool nw_discord_ready(void);

/* The wire format, for the tests. A frame is op and length (little endian) and the JSON. */
size_t nw_discord_frame(uint8_t *out, size_t cap, NwDiscordOp op, const char *json, size_t len);
/* A whole frame's size, 0 for more bytes, -1 for a length over 64 KiB. */
int nw_discord_unframe(const uint8_t *in, size_t n, uint32_t *op, uint32_t *len);
bool nw_discord_handshake(char *out, size_t cap, const char *app_id);
bool nw_discord_activity(char *out, size_t cap, const NwActivity *activity, uint64_t pid, uint32_t nonce);
/* The string value of key's first occurrence in json. */
bool nw_discord_field(const char *json, const char *key, char *out, size_t cap);

#endif
