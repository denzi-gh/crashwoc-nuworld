/* Tables of the data image (tools/gen_dataimage.py -> gen/dataimage.c and gen/<unit>.data.inc). No
   includes and no typedef'd integers: every decomp wrapper ends with a .data.inc, after the unit's own
   declarations. */
#ifndef NUWORLD_DATA_IMAGE_TABLES_H
#define NUWORLD_DATA_IMAGE_TABLES_H

struct NwType;

/* NwImageRoot.kind: where a root's host bytes live. */
enum {
    NW_ROOT_MIRROR,  /* plain data at its PS2 offset in the mirror of the retail image */
    NW_ROOT_COPY,    /* plain data the host declaration needs aligned better than its PS2 address */
    NW_ROOT_CONVERT, /* data with pointer slots, converted element by element */
    NW_ROOT_ALIAS,   /* inside another root or a C-defined object (no storage of its own) */
    NW_ROOT_VU       /* a VU1 microcode package: its token (NwVuProg) */
};

typedef struct NwImageRoot {
    const char *name;
    unsigned int addr;
    unsigned int ps2_size; /* 0: unsized (mirror only) */
    void *host;
    unsigned int host_size;
    const struct NwType *elem; /* CONVERT: the element type; else NULL */
    unsigned int count;        /* CONVERT: elements */
    unsigned int kind;
} NwImageRoot;

/* NwDataObj.flags */
#define NW_DATA_PLAIN 1u /* the same bytes on both sides */

/* Data a compiled unit defines in C, at its retail address: pointers into it resolve to the host object. */
typedef struct NwDataObj {
    const char *name;
    unsigned int addr;
    unsigned int ps2_size;
    void *host;
    unsigned int host_size;
    const struct NwType *type; /* element type; NULL: plain, or only offset 0 maps */
    unsigned int count;
    unsigned int flags;
} NwDataObj;

typedef struct NwDataUnit {
    const char *unit;
    const NwDataObj *objs;
    unsigned int count;
} NwDataUnit;

/* A VU1 microcode package in .vutext. The EE code only takes its address (mtl->rs.mpg = vu_load_tri_mpg) for
   DMA tags to call, so on the host the package's name is this token, never its bytes; the renderer maps it to
   its own behaviour. Data addresses of a package start resolve to the same token. */
typedef struct NwVuProg {
    const char *name;
    unsigned int addr, size; /* the retail package */
} NwVuProg;

typedef struct NwImageTables {
    unsigned int mirror_base, mirror_size; /* PS2 range of the mirror */
    unsigned char *mirror;
    unsigned int text_base, text_end; /* .text: code addresses, never data */
    unsigned int vutext_base, vutext_end; /* .vutext: microcode packages, tokens on the host */
    const NwVuProg *const *vu_progs;      /* address order */
    unsigned int nvu_progs;
    const NwImageRoot *roots;         /* address order */
    unsigned int nroots;
    const NwDataUnit *const *units;
    unsigned int nunits;
    const unsigned int (*registry)[2]; /* sized retail data symbols {address, size}, address order */
    unsigned int nregistry;
    const unsigned int *bounds; /* every known object start, range and section boundary */
    unsigned int nbounds;
} NwImageTables;

extern const NwImageTables nw_image_tables;

#endif
