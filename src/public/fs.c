#include <stdlib.h>
#include <string.h>

#include "mbediso/fs.h"
#include "internal/io.h"
#include "internal/fs.h"
#include "internal/read.h"

struct mbediso_fs* mbediso_openfs_file(const char* name)
{
    struct mbediso_fs* fs = malloc(sizeof(struct mbediso_fs));
    if(!fs)
        return NULL;

    if(!mbediso_fs_ctor(fs))
    {
        free(fs);
        return NULL;
    }

    if(!mbediso_fs_init_from_path(fs, name))
    {
        mbediso_fs_dtor(fs);
        free(fs);
        return NULL;
    }

    struct mbediso_io* io = mbediso_fs_reserve_io(fs);
    if(!io)
    {
        mbediso_fs_dtor(fs);
        free(fs);
        return NULL;
    }

    if(mbediso_read_find_joliet_root(fs, io) != 0 || mbediso_fs_full_scan(fs, io) != 0)
    {
        mbediso_fs_release_io(fs, io);
        mbediso_fs_dtor(fs);
        free(fs);
        return NULL;
    }

    mbediso_fs_release_io(fs, io);

    return fs;
}

void mbediso_closefs(struct mbediso_fs* fs)
{
    if(!fs)
        return;

    mbediso_fs_dtor(fs);
    free(fs);
}
