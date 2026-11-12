/* Durable writes and atomic replacement, on the same filesystem as the destination. */
#ifndef NUWORLD_SAVE_SAVEIO_H
#define NUWORLD_SAVE_SAVEIO_H

#include <stdbool.h>
#include <stddef.h>

bool nw_saveio_write(const char *path, const void *data, size_t size);
bool nw_saveio_replace(const char *from, const char *to);
bool nw_saveio_copy(const char *from, const char *to);

#ifdef NW_SAVE_TESTING
/* Deterministic I/O failure injection; compiled only into the standalone storage tests. -1 disables it. */
void nw_saveio_fail_after(int operations);
#endif

#endif
