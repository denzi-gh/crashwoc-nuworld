#ifndef NUWORLD_UPDATER_ARCHIVE_H
#define NUWORLD_UPDATER_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>

#define NW_ARCHIVE_ROOT "CrashWOC-NuWorld/"

bool nw_archive_unpack(const void *data, size_t size, const char *dir);

#endif
