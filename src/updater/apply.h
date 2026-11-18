#ifndef NUWORLD_UPDATER_APPLY_H
#define NUWORLD_UPDATER_APPLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vfs/sha256.h"

bool nw_updater_signed(const uint8_t (*keys)[32], size_t count, const void *message, size_t size,
                       const void *signature, size_t signature_size);
bool nw_updater_listed(const char *sums, const char *name, char hex[NW_SHA256_HEX]);
bool nw_updater_file_hash(const char *path, char hex[NW_SHA256_HEX]);
bool nw_updater_swap(const char *from, const char *install, char **replaced);

#endif
