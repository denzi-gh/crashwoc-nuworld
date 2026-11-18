#ifndef NUWORLD_PLATFORM_CRASHPAD_H
#define NUWORLD_PLATFORM_CRASHPAD_H

#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

bool nw_crashpad_start(const wchar_t *handler, const wchar_t *database, const char *version, const char *commit);
void nw_crashpad_prune(const wchar_t *database, unsigned keep);
void nw_crashpad_dump(void *exception);

#ifdef __cplusplus
}
#endif

#endif
