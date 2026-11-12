#include "save/saveio.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef NW_SAVE_TESTING
static int nw_saveio_fault = -1;
void nw_saveio_fail_after(int operations) { nw_saveio_fault = operations; }
static bool nw_saveio_checkpoint(void)
{
    if (nw_saveio_fault == 0) {
        nw_saveio_fault = -1;
        return SDL_SetError("injected save I/O failure");
    }
    if (nw_saveio_fault > 0) nw_saveio_fault--;
    return true;
}
#else
#define nw_saveio_checkpoint() true
#endif

bool nw_saveio_write(const char *path, const void *data, size_t size)
{
    if (!nw_saveio_checkpoint()) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        return false;
    }
    bool ok = SDL_WriteIO(io, data, size) == size && nw_saveio_checkpoint() && SDL_FlushIO(io);
    if (ok) {
        SDL_PropertiesID props = SDL_GetIOProperties(io);
#ifdef _WIN32
        HANDLE handle = SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER, NULL);
        ok = handle && FlushFileBuffers(handle);
        if (!ok) {
            SDL_SetError("%s: cannot synchronize save (%lu)", path, GetLastError());
        }
#else
        int fd = (int)SDL_GetNumberProperty(props, SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, -1);
        FILE *file = SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, NULL);
        if (fd < 0 && file) {
            fd = fileno(file);
        }
        ok = fd >= 0 && fsync(fd) == 0;
        if (!ok) {
            SDL_SetError("%s: cannot synchronize save: %s", path, strerror(errno));
        }
#endif
    }
    char why[512];
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    bool closed = SDL_CloseIO(io);
    if (!ok) {
        SDL_SetError("%s", why);
    }
    return ok && closed && nw_saveio_checkpoint();
}

bool nw_saveio_replace(const char *from, const char *to)
{
    if (!nw_saveio_checkpoint()) return false;
#ifdef _WIN32
    wchar_t *a = (wchar_t *)SDL_iconv_string("UTF-16LE", "UTF-8", from, SDL_strlen(from) + 1);
    wchar_t *b = (wchar_t *)SDL_iconv_string("UTF-16LE", "UTF-8", to, SDL_strlen(to) + 1);
    bool ok = false;
    DWORD error = ERROR_NOT_ENOUGH_MEMORY;
    /* A scanner or another reader can briefly deny replacement on Windows. Keep the old file intact and retry
       the same atomic operation for at most 200 ms; never fall back to deleting the destination first. */
    if (a && b) {
        for (int attempt = 0; attempt <= 20; attempt++) {
            if (MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                ok = true;
                break;
            }
            error = GetLastError();
            if (attempt == 20 || (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION &&
                                  error != ERROR_ACCESS_DENIED)) break;
            SDL_Delay(10);
        }
    }
    if (!ok) {
        SDL_SetError("%s: cannot replace save (%lu)", to, error);
    }
    SDL_free(a);
    SDL_free(b);
    return ok;
#else
    if (rename(from, to) != 0) {
        return SDL_SetError("%s: cannot replace save: %s", to, strerror(errno));
    }
    char *parent = SDL_strdup(to);
    if (!parent) {
        return false;
    }
    char *slash = SDL_strrchr(parent, '/');
    if (slash) {
        slash[slash == parent ? 1 : 0] = 0;
    } else {
        SDL_strlcpy(parent, ".", SDL_strlen(to) + 1);
    }
    int fd = open(parent, O_RDONLY | O_DIRECTORY);
    bool ok = fd >= 0 && fsync(fd) == 0;
    if (!ok) {
        SDL_SetError("%s: cannot synchronize save folder: %s", parent, strerror(errno));
    }
    if (fd >= 0) {
        close(fd);
    }
    SDL_free(parent);
    return ok;
#endif
}

bool nw_saveio_copy(const char *from, const char *to)
{
    size_t size = 0;
    void *data = SDL_LoadFile(from, &size);
    char *tmp = NULL;
    bool ok = data && SDL_asprintf(&tmp, "%s.tmp", to) >= 0 && nw_saveio_write(tmp, data, size) &&
              nw_saveio_replace(tmp, to);
    if (!ok && tmp) {
        char why[512];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        SDL_RemovePath(tmp);
        SDL_SetError("%s", why);
    }
    SDL_free(tmp);
    SDL_free(data);
    return ok;
}
