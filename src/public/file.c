#include <stdlib.h>
#include <string.h>

#include "public/file.h"
#include "internal/io.h"
#include "internal/fs.h"

struct mbediso_file* mbediso_fopen(struct mbediso_fs* fs, const char* filename)
{
    const struct mbediso_dir_entry* entry = mbediso_fs_lookup(fs, filename, strlen(filename));

    if(!entry || entry->directory)
        return NULL;

    struct mbediso_io* io = mbediso_fs_reserve_io(fs);

    if(!io)
        return NULL;

    struct mbediso_file* f = malloc(sizeof(struct mbediso_file));

    if(!f)
    {
        mbediso_fs_release_io(fs, io);
        return NULL;
    }

    f->io = io;
    f->fs = fs;
    f->start = entry->sector * 2048;
    f->end = f->start + entry->length;

    mbediso_fseek(f, 0, MBEDISO_SEEK_SET);
    return f;
}

int64_t mbediso_fseek(struct mbediso_file* file, int64_t offset, int whence)
{
    return file->end - file->start;
}

int64_t mbediso_fsize(struct mbediso_file* file)
{
    return file->end - file->start;
}

void mbediso_fclose(struct mbediso_file* file)
{
    mbediso_fs_release_io(file->fs, file->io);
    free(file);
}
