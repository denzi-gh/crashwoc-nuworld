#include "platform/http.h"

#include <SDL3/SDL.h>

#define NW_HTTP_MAX (1 << 20)

typedef struct NwHttpBody {
    char *data;
    size_t size, cap;
} NwHttpBody;

static bool nw_http_append(NwHttpBody *b, const void *p, size_t n)
{
    if (n > NW_HTTP_MAX - b->size) {
        return SDL_SetError("the answer is over %d bytes", NW_HTTP_MAX);
    }
    if (b->size + n + 1 > b->cap) {
        size_t cap = SDL_max(b->cap * 2, b->size + n + 1);
        char *data = SDL_realloc(b->data, cap);
        if (!data) {
            return false;
        }
        b->data = data;
        b->cap = cap;
    }
    SDL_memcpy(b->data + b->size, p, n);
    b->size += n;
    b->data[b->size] = '\0';
    return true;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

static wchar_t *nw_http_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? SDL_malloc(n * sizeof(wchar_t)) : NULL;
    if (w && !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) {
        SDL_free(w);
        w = NULL;
    }
    return w;
}

static bool nw_http_fail(const char *what)
{
    return SDL_SetError("%s: Windows error %lu", what, (unsigned long)GetLastError());
}

typedef struct NwWinHttp {
    HINTERNET session, connection, request;
} NwWinHttp;

static bool nw_http_request(NwWinHttp *h, const wchar_t *url, const wchar_t *agent, int timeout_ms, NwHttpBody *body,
                            int *status)
{
    wchar_t host[256];
    URL_COMPONENTS parts;
    SDL_zero(parts);
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = SDL_arraysize(host);
    parts.dwUrlPathLength = (DWORD)-1; /* points into url: the path and the query after it */
    if (!WinHttpCrackUrl(url, 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        return SDL_SetError("not an https URL");
    }
    h->session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h->session || !WinHttpSetTimeouts(h->session, timeout_ms, timeout_ms, timeout_ms, timeout_ms)) {
        return nw_http_fail("WinHttpOpen");
    }
    h->connection = WinHttpConnect(h->session, host, parts.nPort, 0);
    if (!h->connection) {
        return nw_http_fail("WinHttpConnect");
    }
    h->request = WinHttpOpenRequest(h->connection, L"GET", parts.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!h->request ||
        !WinHttpSendRequest(h->request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(h->request, NULL)) {
        return nw_http_fail("WinHttpSendRequest");
    }
    DWORD code = 0, len = sizeof(code);
    if (!WinHttpQueryHeaders(h->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX)) {
        return nw_http_fail("WinHttpQueryHeaders");
    }
    *status = (int)code;
    for (;;) {
        char buf[16384];
        DWORD got = 0;
        if (!WinHttpReadData(h->request, buf, sizeof(buf), &got)) {
            return nw_http_fail("WinHttpReadData");
        }
        if (got == 0) {
            return true;
        }
        if (!nw_http_append(body, buf, got)) {
            return false;
        }
    }
}

static bool nw_http_fetch(const char *url, const char *agent, int timeout_ms, NwHttpBody *body, int *status)
{
    wchar_t *wurl = nw_http_wide(url), *wagent = nw_http_wide(agent);
    NwWinHttp h = { 0 };
    bool ok = wurl && wagent ? nw_http_request(&h, wurl, wagent, timeout_ms, body, status)
                             : SDL_SetError("cannot convert the URL");
    if (h.request) {
        WinHttpCloseHandle(h.request);
    }
    if (h.connection) {
        WinHttpCloseHandle(h.connection);
    }
    if (h.session) {
        WinHttpCloseHandle(h.session);
    }
    SDL_free(wurl);
    SDL_free(wagent);
    return ok;
}
#else
/* libcurl's numbers are part of its stable ABI, so building needs no curl headers. */
enum {
    NW_CURLOPT_WRITEDATA = 10001,
    NW_CURLOPT_URL = 10002,
    NW_CURLOPT_USERAGENT = 10018,
    NW_CURLOPT_WRITEFUNCTION = 20011,
    NW_CURLOPT_FOLLOWLOCATION = 52,
    NW_CURLOPT_NOSIGNAL = 99,
    NW_CURLOPT_TIMEOUT_MS = 155,
    NW_CURLINFO_RESPONSE_CODE = 0x200002,
};

typedef struct NwCurl {
    void *(*init)(void);
    int (*setopt)(void *, int, ...);
    int (*perform)(void *);
    int (*getinfo)(void *, int, ...);
    const char *(*strerror)(int);
    void (*cleanup)(void *);
} NwCurl;

static size_t nw_http_write(char *p, size_t size, size_t n, void *user)
{
    return nw_http_append(user, p, size * n) ? size * n : 0;
}

static bool nw_http_curl(const NwCurl *c, const char *url, const char *agent, int timeout_ms, NwHttpBody *body,
                         int *status)
{
    void *curl = c->init();
    if (!curl) {
        return SDL_SetError("curl_easy_init failed");
    }
    c->setopt(curl, NW_CURLOPT_URL, url);
    c->setopt(curl, NW_CURLOPT_USERAGENT, agent);
    c->setopt(curl, NW_CURLOPT_FOLLOWLOCATION, 1L);
    c->setopt(curl, NW_CURLOPT_NOSIGNAL, 1L);
    c->setopt(curl, NW_CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    c->setopt(curl, NW_CURLOPT_WRITEFUNCTION, nw_http_write);
    c->setopt(curl, NW_CURLOPT_WRITEDATA, body);
    int result = c->perform(curl);
    long code = 0;
    if (result == 0) {
        c->getinfo(curl, NW_CURLINFO_RESPONSE_CODE, &code);
        *status = (int)code;
    } else {
        SDL_SetError("libcurl: %s", c->strerror(result));
    }
    c->cleanup(curl);
    return result == 0;
}

static bool nw_http_fetch(const char *url, const char *agent, int timeout_ms, NwHttpBody *body, int *status)
{
    static const char *const names[] = { "libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so" };
    static SDL_SharedObject *lib; /* kept: curl's resolver thread can outlive a timed-out request */
    for (size_t i = 0; i < SDL_arraysize(names) && !lib; i++) {
        lib = SDL_LoadObject(names[i]);
    }
    if (!lib) {
        return SDL_SetError("no libcurl");
    }
    NwCurl c = {
        .init = (void *(*)(void))SDL_LoadFunction(lib, "curl_easy_init"),
        .setopt = (int (*)(void *, int, ...))SDL_LoadFunction(lib, "curl_easy_setopt"),
        .perform = (int (*)(void *))SDL_LoadFunction(lib, "curl_easy_perform"),
        .getinfo = (int (*)(void *, int, ...))SDL_LoadFunction(lib, "curl_easy_getinfo"),
        .strerror = (const char *(*)(int))SDL_LoadFunction(lib, "curl_easy_strerror"),
        .cleanup = (void (*)(void *))SDL_LoadFunction(lib, "curl_easy_cleanup"),
    };
    return c.init && c.setopt && c.perform && c.getinfo && c.strerror && c.cleanup
               ? nw_http_curl(&c, url, agent, timeout_ms, body, status)
               : SDL_SetError("libcurl lacks curl_easy_*");
}
#endif

bool nw_http_get(const char *url, const char *agent, int timeout_ms, char **body, int *status)
{
    NwHttpBody b = { 0 };
    *status = 0;
    bool ok = nw_http_fetch(url, agent, timeout_ms, &b, status) && nw_http_append(&b, "", 0);
    if (!ok) {
        SDL_free(b.data);
        b.data = NULL;
    }
    *body = b.data;
    return ok;
}
