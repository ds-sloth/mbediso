#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mbediso.h"

#include "internal/io.h"

struct mbediso_io* mbediso_io_from_file(FILE* file)
{
    struct mbediso_io_file* io = malloc(sizeof(struct mbediso_io_file));

    if(!io)
        return NULL;

    io->tag = 1;
    io->file = file;
    io->filepos = -1;

    // eventually, figure out sector size here...
    io->buffer = malloc(2048);

    if(!io->buffer)
    {
        free(io);
        return NULL;
    }

    return (struct mbediso_io*)io;
}

const uint8_t* mbediso_io_read_sector(struct mbediso_io* _io, uint32_t sector)
{
    if(!_io)
        return NULL;

    if(_io->tag == 1)
    {
        struct mbediso_io_file* io = (struct mbediso_io_file*)_io;

        uint64_t target_pos = sector * 2048;

        if(io->filepos != target_pos)
        {
            // printf("seeking %lx...\n", target_pos);

            if(fseek(io->file, target_pos, SEEK_SET))
            {
                io->filepos = -1;
                return NULL;
            }
        }

        if(fread(io->buffer, 1, 2048, io->file) != 2048)
        {
            printf("read failed...\n");

            io->filepos = -1;
            return NULL;
        }

        io->filepos = (sector + 1) * 2048;

        return io->buffer;
    }

    return NULL;
}

size_t mbediso_io_read_direct(struct mbediso_io* _io, uint8_t* dest, uint64_t offset, size_t bytes)
{
    if(!_io)
        return 0;

    if(_io->tag == 1)
    {
        struct mbediso_io_file* io = (struct mbediso_io_file*)_io;

        if(io->filepos != offset)
        {
            // printf("seeking %lx...\n", offset);

            if(fseek(io->file, offset, SEEK_SET))
            {
                io->filepos = -1;
                return 0;
            }
        }

        io->filepos = offset;

        while(bytes > 0)
        {
            size_t got = fread(dest, 1, bytes, io->file);
            if(got == 0)
                return io->filepos - offset;

            dest += got;
            bytes -= got;
            io->filepos += got;
        }

        return io->filepos - offset;
    }

    return false;
}

void mbediso_io_close(struct mbediso_io* _io)
{
    if(!_io)
        return;

    if(_io->tag == 1)
    {
        struct mbediso_io_file* io = (struct mbediso_io_file*)_io;

        fclose(io->file);

        free(io->buffer);

        free(io);
    }
}
