/* Files of a PS2 memory card image (PCSX2's .ps2: 528-byte pages with their ECC, or 512-byte pages without), read
   through the card's own file system: the superblock, the indirect FAT and the 512-byte directory entries. */
#ifndef NUWORLD_SAVE_PS2CARD_H
#define NUWORLD_SAVE_PS2CARD_H

#include <stdbool.h>
#include <stddef.h>

#define NW_PS2CARD_MAGIC "Sony PS2 Memory Card Format "

/* Whether image starts with a card superblock (an unformatted card does not). */
bool nw_ps2card_is_card(const void *image, size_t size);
/* The file /dir/name of the card, SDL_malloc'd with its size in *len; NULL with an SDL error, and *missing set
   when the card reads fine but has no such file. */
void *nw_ps2card_file(const void *image, size_t size, const char *dir, const char *name, size_t *len, bool *missing);

#endif
