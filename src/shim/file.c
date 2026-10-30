/* sce file I/O for the nufile layer, on the game's file system (src/vfs/vfs.c): cdrom0: reads the imported disc,
   host0: the writable <data>/host0/. Descriptors are 0..15: nufile.c indexes its file_info[16] with them. */
#include <stdio.h>

#include "audio/nusound.h"
#include "shim/shim.h"
#include "vfs/vfs.h"

#define NW_SCE_FDS 16
#define NW_SCE_MODE_MASK 0x3 /* SCE_RDONLY 1, SCE_WRONLY 2, SCE_RDWR 3 */
#define NW_SCE_APPEND 0x100
#define NW_SCE_EBADF 9
#define NW_SCE_EIO 5
#define NW_SCE_ENOENT 2
#define NW_SCE_EMFILE 24

static const NwVfs *nw_shim_vfs;
static NwVfsFile *nw_shim_fds[NW_SCE_FDS];

void nw_shim_file_init(const NwVfs *vfs)
{
    for (int i = 0; i < NW_SCE_FDS; i++) {
        nw_vfs_close(nw_shim_fds[i]);
        nw_shim_fds[i] = NULL;
    }
    nw_shim_vfs = vfs;
}

const NwVfs *nw_shim_file_vfs(void)
{
    return nw_shim_vfs;
}

static NwVfsFile *nw_shim_fd(int fd)
{
    return fd >= 0 && fd < NW_SCE_FDS ? nw_shim_fds[fd] : NULL;
}

/* Retail retries sceClose and sceLseek while they fail: a bad descriptor is reported once and succeeds. */
static int nw_shim_bad_fd(const char *call, int fd)
{
    static unsigned reported;
    if (!reported++) {
        fprintf(stderr, "%s: no open file %d\n", call, fd);
    }
    return 0;
}

int sceOpen(char *name, int flag, ...)
{
    int fd = 0;
    while (fd < NW_SCE_FDS && nw_shim_fds[fd]) {
        fd++;
    }
    if (fd == NW_SCE_FDS) {
        return -NW_SCE_EMFILE;
    }
    NwVfsMode mode = (flag & NW_SCE_MODE_MASK) == 1 ? NW_VFS_READ
                     : flag & NW_SCE_APPEND         ? NW_VFS_APPEND
                                                    : NW_VFS_WRITE;
    nw_shim_fds[fd] = nw_shim_vfs ? nw_vfs_open(nw_shim_vfs, name, mode) : NULL;
    return nw_shim_fds[fd] ? fd : -NW_SCE_ENOENT;
}

int sceClose(int fd)
{
    NwVfsFile *f = nw_shim_fd(fd);
    if (!f) {
        return nw_shim_bad_fd("sceClose", fd);
    }
    nw_vfs_close(f);
    nw_shim_fds[fd] = NULL;
    return 0;
}

int sceRead(int fd, void *addr, int size)
{
    NwVfsFile *f = nw_shim_fd(fd);
    if (!f) {
        return -NW_SCE_EBADF;
    }
    nw_ee_yield();
    nw_nusound_cd_moved();
    int64_t n = size > 0 ? nw_vfs_read(f, addr, (size_t)size) : 0;
    return n < 0 ? -NW_SCE_EIO : (int)n;
}

int sceWrite(int fd, void *addr, int size)
{
    NwVfsFile *f = nw_shim_fd(fd);
    if (!f) {
        return -NW_SCE_EBADF;
    }
    int64_t n = size > 0 ? nw_vfs_write(f, addr, (size_t)size) : 0;
    return n < 0 ? -NW_SCE_EIO : (int)n;
}

int sceLseek(int fd, int offset, int how)
{
    NwVfsFile *f = nw_shim_fd(fd);
    if (!f) {
        return nw_shim_bad_fd("sceLseek", fd);
    }
    int64_t pos = nw_vfs_seek(f, offset, how);
    return pos < 0 ? -NW_SCE_EIO : (int)pos;
}

/* Request 1 is the async status NuFileStatus polls: every transfer has already finished. */
int sceIoctl(int fd, int req, void *arg)
{
    (void)fd;
    if (req == 1 && arg) {
        *(int *)arg = 0;
    }
    return 0;
}
