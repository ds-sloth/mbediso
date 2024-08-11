#include <stdlib.h>
#include <string.h>

#include "mbediso/file.h"
#include "internal/io.h"
#include "internal/fs.h"

struct mbediso_file* mbediso_fopen(struct mbediso_fs* fs, const char* filename)
{
    struct mbediso_location loc;
    if(!mbediso_fs_lookup(fs, filename, &loc))
        return NULL;

    if(loc.directory)
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
    f->start = loc.sector * 2048;
    f->end = f->start + loc.length;
    f->offset = 0;

    return f;
}

size_t mbediso_fread(struct mbediso_file* file, void* ptr, size_t size, size_t maxnum)
{
    size_t bytes = size * maxnum;

    if(file->offset >= file->end - file->start)
        return 0;

    if(size * maxnum == 0)
        return 0;

    if(bytes > file->end - (file->start + file->offset))
        bytes = file->end - (file->start + file->offset);

    size_t ret = mbediso_io_read_direct(file->io, ptr, file->offset + file->start, bytes);
    // ignore incompletely-read members
    ret -= ret % size;
    file->offset += ret;

    return ret / size;
}

int64_t mbediso_fseek(struct mbediso_file* file, int64_t offset, int whence)
{
    int64_t try_offset = -1;
    if(whence == MBEDISO_SEEK_SET)
        try_offset = offset;
    else if(whence == MBEDISO_SEEK_CUR)
        try_offset = file->offset + offset;
    else if(whence == MBEDISO_SEEK_END)
        try_offset = (file->end - file->start) + offset;

    if(try_offset < 0 || try_offset > file->end - file->start)
        return -1;

    file->offset = try_offset;

    return file->offset;
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

struct mbediso_fs* mbediso_file_fs(struct mbediso_file* file)
{
    return file->fs;
}
