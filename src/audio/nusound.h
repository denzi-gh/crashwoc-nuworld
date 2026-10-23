/* NUSOUND.IRX, the IOP side of the sound RPC 0x12345 that decomp nusound.c calls: the command batches run as the
   module's AdpcmSetParam runs them (stream.c, ported from the disc's SYS/NUSOUND.IRX), the reply is its status
   block `ret`. */
#ifndef NUWORLD_AUDIO_NUSOUND_H
#define NUWORLD_AUDIO_NUSOUND_H

#include <stddef.h>

#define NW_NUSOUND_RPC 0x12345
#define NW_NUSOUND_REPLY 0x200

/* The module as loaded. */
void nw_nusound_init(void);

/* ProcessEECommand: function 0 runs the batch in send; every call copies the reply into recv. */
void nw_nusound_rpc(unsigned fno, const void *send, size_t ssize, void *recv, size_t rsize);

#endif
