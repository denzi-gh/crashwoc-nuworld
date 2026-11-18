#ifndef NUWORLD_PLATFORM_CRASH_H
#define NUWORLD_PLATFORM_CRASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NW_CRASH_NO_FILE ((intptr_t)-1)

void nw_crash_install(void);
void nw_crash_sink(intptr_t file, bool to_stderr);
void nw_crash_dumps(const char *dir);
void nw_crash_arm(void);
void nw_crash_on_state(size_t (*fn)(char *out, size_t size));
void nw_crash_phase(const char *what, unsigned i, unsigned n);

void nw_crash_fatal(const char *message);
_Noreturn void nw_crash_test(const char *kind);

const char *nw_crash_symbol(const void *addr, uintptr_t *offset);

#endif
