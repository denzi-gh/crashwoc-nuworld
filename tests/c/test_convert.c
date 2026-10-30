/* gen/nw_convert.c on random bytes for every record: nw_xr(nw_x(b)) == b, host values, bitfields, nw_xoff and
   nw_xoff_host per leaf, the path of a fatal error; a converter with a fatal hook only has to fail. */
#include <inttypes.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

#include "nw_convert.h"

enum { NW_LEAF_BYTES, NW_LEAF_BIT, NW_LEAF_PTR, NW_LEAF_FN, NW_LEAF_ADDR, NW_LEAF_REC, NW_LEAF_HOOK };

typedef struct NwTestLeaf {
    const char *site;
    unsigned kind;
    uint32_t ps2_off, host_off;   /* bits for NW_LEAF_BIT */
    uint32_t ps2_size, host_size; /* of one element; bits for NW_LEAF_BIT */
    uint32_t count;
    int array;
    int is_signed;
    const NwType *type; /* PTR: pointee; REC: element */
} NwTestLeaf;

typedef struct NwTestType {
    const NwType *type;
    const NwTestLeaf *leaves;
    unsigned nleaves;
} NwTestType;

#include "nw_convert_test.inc"

#define HOST_BASE UINT64_C(0x100000000)
#define DATA_LO 0x00400000u
#define DATA_SLOTS 65536u
#define CODE_LO 0x00200000u
#define POISON 0xdeadbee0u
#define ROUNDS 8

static volatile int fake_hits;
static void fake0(void) { fake_hits = 0; }
static void fake1(void) { fake_hits = 1; }
static void fake2(void) { fake_hits = 2; }
static void fake3(void) { fake_hits = 3; }

static const NwFuncmapEntry fake_entries[] = {
    { CODE_LO + 0, NW_PROV_STUB, "fake0", fake0 },
    { CODE_LO + 8, NW_PROV_STUB, "fake1", fake1 },
    { CODE_LO + 16, NW_PROV_STUB, "fake2", fake2 },
    { CODE_LO + 24, NW_PROV_STUB, "fake3", fake3 },
};
static const NwFuncmapUnit fake_unit = { "fake", fake_entries, 4 };
const NwFuncmapUnit *const nw_funcmap_units[] = { &fake_unit };
const unsigned nw_funcmap_unit_count = 1;

static int failures;
static uint64_t rng = 0x9e3779b97f4a7c15u;
static uint32_t next_data;
static const NwType *data_types[DATA_SLOTS];
static jmp_buf fatal_jump;
static char fatal_message[512];

#define FAIL(...) \
    do { \
        if (failures++ < 50) { \
            fprintf(stderr, __VA_ARGS__); \
            fputc('\n', stderr); \
        } \
    } while (0)

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}

static void *ptr_in(NwXCtx *ctx, uint32_t addr, const NwType *type, const char *site)
{
    if (addr >= 0xd0000000u) {
        nw_xfatal(ctx, site, "unresolved pointer 0x%08" PRIx32, addr);
    }
    data_types[(addr - DATA_LO) / 16 % DATA_SLOTS] = type;
    return (void *)(uintptr_t)(HOST_BASE + addr);
}

static uint32_t ptr_out(NwXCtx *ctx, const void *p, const NwType *type, const char *site)
{
    uint32_t addr = (uint32_t)((uintptr_t)p - HOST_BASE);
    if (data_types[(addr - DATA_LO) / 16 % DATA_SLOTS] != type) {
        nw_xfatal(ctx, site, "ptr_out with %s, ptr_in had another type", type->name);
    }
    return addr;
}

static uintptr_t addr_value(uint32_t v, int is_signed)
{
    if (v >= DATA_LO && v < DATA_LO + DATA_SLOTS * 16) {
        return (uintptr_t)(HOST_BASE + v);
    }
    return is_signed ? (uintptr_t)(intptr_t)(int32_t)v : v;
}

static uintptr_t addr_in(NwXCtx *ctx, uint32_t value, int is_signed, const char *site)
{
    (void)ctx, (void)site;
    return addr_value(value, is_signed);
}

static uint32_t addr_out(NwXCtx *ctx, uintptr_t value, const char *site)
{
    (void)ctx, (void)site;
    return value >= HOST_BASE ? (uint32_t)(value - HOST_BASE) : (uint32_t)value;
}

static void fatal(NwXCtx *ctx, const char *message)
{
    (void)ctx;
    snprintf(fatal_message, sizeof fatal_message, "%s", message);
    longjmp(fatal_jump, 1);
}

static NwXCtx ctx = { .ptr_in = ptr_in, .ptr_out = ptr_out, .addr_in = addr_in, .addr_out = addr_out,
                      .fatal = fatal };

static const NwTestType *find(const NwType *type)
{
    for (const NwTestType *t = nw_test_types; t->type; t++) {
        if (t->type == type) {
            return t;
        }
    }
    return NULL;
}

static int hooked(const NwTestType *t)
{
    for (unsigned i = 0; i < t->nleaves; i++) {
        const NwTestLeaf *l = &t->leaves[i];
        if (l->kind == NW_LEAF_HOOK || (l->kind == NW_LEAF_REC && hooked(find(l->type)))) {
            return 1;
        }
    }
    return 0;
}

static uint32_t get32(const unsigned char *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

static int bit(const unsigned char *p, uint32_t i)
{
    return (p[i / 8] >> (i % 8)) & 1;
}

static void fill(const NwTestType *t, unsigned char *ps2)
{
    for (unsigned i = 0; i < t->nleaves; i++) {
        const NwTestLeaf *l = &t->leaves[i];
        for (uint32_t e = 0; e < l->count; e++) {
            unsigned char *p = ps2 + l->ps2_off + e * l->ps2_size;
            uint32_t v = 0;
            switch (l->kind) {
            case NW_LEAF_BYTES:
                for (uint32_t b = 0; b < l->ps2_size; b++) {
                    p[b] = (unsigned char)rnd();
                }
                continue;
            case NW_LEAF_BIT:
                for (uint32_t b = l->ps2_off; b < l->ps2_off + l->ps2_size; b++) {
                    ps2[b / 8] |= (unsigned char)((rnd() & 1) << (b % 8));
                }
                continue;
            case NW_LEAF_PTR:
                v = rnd() % 4 ? DATA_LO + 16 * (next_data++ % DATA_SLOTS) : 0;
                break;
            case NW_LEAF_FN:
                v = rnd() % 3 ? CODE_LO + 8 * (rnd() % 4) : 0;
                break;
            case NW_LEAF_ADDR:
                v = rnd() % 2 ? DATA_LO + 16 * (next_data++ % DATA_SLOTS) : rnd();
                break;
            case NW_LEAF_REC:
                fill(find(l->type), p);
                continue;
            default:
                continue;
            }
            memcpy(p, &v, sizeof v);
        }
    }
}

static void verify(const NwTestType *t, const unsigned char *ps2, const unsigned char *host)
{
    const char *name = t->type->name;
    for (unsigned i = 0; i < t->nleaves; i++) {
        const NwTestLeaf *l = &t->leaves[i];
        if (l->kind == NW_LEAF_BYTES && memcmp(ps2 + l->ps2_off, host + l->host_off, l->ps2_size)) {
            FAIL("%s.%s: the host bytes differ", name, l->site);
        }
        for (uint32_t b = 0; l->kind == NW_LEAF_BIT && b < l->ps2_size; b++) {
            if (bit(ps2, l->ps2_off + b) != bit(host, l->host_off + b)) {
                FAIL("%s.%s: bit %u differs on the host", name, l->site, (unsigned)b);
                break;
            }
        }
        for (uint32_t e = 0; l->kind >= NW_LEAF_PTR && l->kind <= NW_LEAF_REC && e < l->count; e++) {
            const unsigned char *p = ps2 + l->ps2_off + e * l->ps2_size;
            const unsigned char *h = host + l->host_off + e * l->host_size;
            uint32_t v = get32(p);
            uintptr_t got;
            memcpy(&got, h, sizeof got);
            if (l->kind == NW_LEAF_REC) {
                verify(find(l->type), p, h);
            } else if (l->kind == NW_LEAF_PTR && got != (v ? (uintptr_t)(HOST_BASE + v) : 0)) {
                FAIL("%s.%s[%u]: 0x%08" PRIx32 " became 0x%" PRIxPTR, name, l->site, (unsigned)e, v, got);
            } else if (l->kind == NW_LEAF_PTR && v && data_types[(v - DATA_LO) / 16] != l->type) {
                FAIL("%s.%s[%u]: ptr_in saw another pointee type", name, l->site, (unsigned)e);
            } else if (l->kind == NW_LEAF_FN && got != (v ? (uintptr_t)fake_entries[(v - CODE_LO) / 8].fn : 0)) {
                FAIL("%s.%s[%u]: code address 0x%08" PRIx32 " is not its fake function", name, l->site,
                     (unsigned)e, v);
            } else if (l->kind == NW_LEAF_ADDR && got != addr_value(v, l->is_signed)) {
                FAIL("%s.%s[%u]: nuintptr 0x%08" PRIx32 " became 0x%" PRIxPTR, name, l->site, (unsigned)e, v,
                     got);
            }
        }
    }
}

/* Marks the host bits the leaves of t own. */
static void cover(const NwTestType *t, unsigned char *mask)
{
    for (unsigned i = 0; i < t->nleaves; i++) {
        const NwTestLeaf *l = &t->leaves[i];
        if (l->kind == NW_LEAF_BIT) {
            for (uint32_t b = l->host_off; b < l->host_off + l->host_size; b++) {
                mask[b / 8] |= (unsigned char)(1u << (b % 8));
            }
            continue;
        }
        for (uint32_t e = 0; e < l->count; e++) {
            if (l->kind == NW_LEAF_REC) {
                cover(find(l->type), mask + l->host_off + e * l->host_size);
            } else {
                memset(mask + l->host_off + e * l->host_size, 0xff, l->host_size);
            }
        }
    }
}

static void check_off(const NwType *type, uint32_t ps2_off, uint32_t want, const char *site)
{
    uint32_t got = 0, back = 0;
    if (!nw_xoff(type, ps2_off, &got) || got != want) {
        FAIL("%s.%s: nw_xoff(%u) is %u, the host offset is %u", type->name, site, (unsigned)ps2_off,
             (unsigned)got, (unsigned)want);
    }
    if (!nw_xoff_host(type, want, &back) || back != ps2_off) {
        FAIL("%s.%s: nw_xoff_host(%u) is %u, the PS2 offset is %u", type->name, site, (unsigned)want,
             (unsigned)back, (unsigned)ps2_off);
    }
}

static void check_offsets(const NwTestType *t)
{
    uint32_t dummy;
    check_off(t->type, t->type->ps2_size, t->type->host_size, "<end>");
    for (unsigned i = 0; i < t->nleaves; i++) {
        const NwTestLeaf *l = &t->leaves[i];
        if (l->kind == NW_LEAF_BYTES) {
            check_off(t->type, l->ps2_off, l->host_off, l->site);
            check_off(t->type, l->ps2_off + l->ps2_size - 1, l->host_off + l->host_size - 1, l->site);
        } else if (l->kind == NW_LEAF_BIT) {
            check_off(t->type, l->ps2_off / 8, l->host_off / 8, l->site);
        } else if (l->kind != NW_LEAF_HOOK) {
            for (uint32_t e = 0; e < l->count; e++) {
                check_off(t->type, l->ps2_off + e * l->ps2_size, l->host_off + e * l->host_size, l->site);
            }
            if (l->kind != NW_LEAF_REC && nw_xoff(t->type, l->ps2_off + 1, &dummy)) {
                FAIL("%s.%s: nw_xoff maps the inside of a pointer", t->type->name, l->site);
            }
            if (l->kind != NW_LEAF_REC && nw_xoff_host(t->type, l->host_off + 1, &dummy)) {
                FAIL("%s.%s: nw_xoff_host maps the inside of a pointer", t->type->name, l->site);
            }
        }
    }
}

/* Poisons the first data pointer under t, depth first; path gets its expected field path. */
static int poison(const NwTestType *t, unsigned char *ps2, char *path, size_t size)
{
    size_t n = strlen(path);
    for (unsigned i = 0; i < t->nleaves; i++) {
        const NwTestLeaf *l = &t->leaves[i];
        if (l->kind == NW_LEAF_PTR) {
            uint32_t v = POISON;
            memcpy(ps2 + l->ps2_off, &v, sizeof v);
            snprintf(path + n, size - n, ".%s: unresolved pointer 0x%08x", l->site, (unsigned)POISON);
            return 1;
        }
        if (l->kind == NW_LEAF_REC) {
            snprintf(path + n, size - n, ".%s%s", l->site, l->array ? "[0]" : "");
            if (poison(find(l->type), ps2 + l->ps2_off, path, size)) {
                return 1;
            }
            path[n] = '\0';
        }
    }
    return 0;
}

int main(void)
{
    unsigned types = 0, round_trips = 0, hooks = 0, paths = 0;
    for (const NwTestType *t = nw_test_types; t->type; t++, types++) {
        const NwType *type = t->type;
        unsigned char *ps2 = calloc(1, type->ps2_size + 1), *back = calloc(1, type->ps2_size + 1);
        unsigned char *host = calloc(1, type->host_size + 1), *mask = calloc(1, type->host_size + 1);
        if (!ps2 || !back || !host || !mask) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        check_offsets(t);
        cover(t, mask);
        for (int round = 0; round < ROUNDS; round++) {
            memset(ps2, 0, type->ps2_size);
            next_data = 0;
            fill(t, ps2);
            if (setjmp(fatal_jump)) {
                if (!hooked(t)) {
                    FAIL("%s: %s", type->name, fatal_message);
                }
                hooks += hooked(t) && round == 0;
                break;
            }
            memset(host, 0x5a, type->host_size);
            nw_x(&ctx, type, host, ps2, type->name);
            verify(t, ps2, host);
            for (uint32_t b = 0; b < type->host_size; b++) {
                if (host[b] & ~mask[b]) {
                    FAIL("%s: host byte %u is not cleared", type->name, (unsigned)b);
                    break;
                }
            }
            memset(back, 0xa5, type->ps2_size);
            nw_xr(&ctx, type, back, host, type->name);
            if (memcmp(ps2, back, type->ps2_size)) {
                FAIL("%s: nw_xr(nw_x(b)) != b", type->name);
            }
            round_trips += round == 0;
        }

        char path[512];
        snprintf(path, sizeof path, "%s", type->name);
        memset(ps2, 0, type->ps2_size);
        if (poison(t, ps2, path, sizeof path)) {
            if (setjmp(fatal_jump) == 0) {
                nw_x(&ctx, type, host, ps2, type->name);
                FAIL("%s: the poisoned pointer converted", type->name);
            } else if (strcmp(fatal_message, path) == 0) {
                paths++;
            } else if (!hooked(t)) {
                FAIL("%s: the error is \"%s\", expected \"%s\"", type->name, fatal_message, path);
            }
        }
        free(ps2);
        free(back);
        free(host);
        free(mask);
    }
    printf("%u types: %u round trips, %u stop at a hook, %u error paths, %d failures\n", types, round_trips,
           hooks, paths, failures);
    return failures != 0;
}
