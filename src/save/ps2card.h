/* Files of a PS2 memory card image (PCSX2's .ps2: 528-byte pages with their ECC, or 512-byte pages without), read
   and written through the card's own file system: the superblock, the indirect FAT and the 512-byte directory
   entries. A card is never formatted here: PCSX2 (or the PS2) does that. */
#ifndef NUWORLD_SAVE_PS2CARD_H
#define NUWORLD_SAVE_PS2CARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NW_PS2CARD_MAGIC "Sony PS2 Memory Card Format "

/* Whether image starts with a card superblock (an unformatted card does not). */
bool nw_ps2card_is_card(const void *image, size_t size);
/* The file /dir/name of the card, SDL_malloc'd with its size in *len; NULL with an SDL error, and *missing set
   when the card reads fine but has no such file. */
void *nw_ps2card_file(const void *image, size_t size, const char *dir, const char *name, size_t *len, bool *missing);

typedef struct NwPs2CardFile {
    const char *name;
    const void *data;
    size_t len;
    bool keep; /* a file of the name already there stays as it is */
} NwPs2CardFile;

/* The files into the folder /dir of image (a formatted card, written in place), in their order; the folder is made
   when missing (as the PS2 makes one), a file there of the same name replaced unless keep; other files stay. time
   (an SDL_Time) dates what changes, in the PS2's Japan time. false with an SDL error (the card full, a name in the
   way), and the image may then be part-written: keep the file it came from until this succeeds. */
bool nw_ps2card_put(void *image, size_t size, const char *dir, const NwPs2CardFile *files, int count, int64_t time);

#endif
