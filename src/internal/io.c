/*
 * mbediso - a minimal library to load data from compressed ISO archives
 *
 * Copyright (c) 2024 ds-sloth
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <lz4.h>

#include "mbediso.h"

#include "internal/io.h"
#include "internal/io_priv.h"
#include "internal/lz4_header.h"

static uint32_t s_swap_endian(uint32_t r)
{
    return ((uint8_t)(r >> 24) << 0) + ((uint8_t)(r >> 16) << 8) + ((uint8_t)(r >> 8) << 16) + ((uint8_t)(r >> 0) << 24);
}

static void s_fix_le(uint32_t* buffer)
{
    if(!(bool)MBEDISO_BIG_ENDIAN)
        return;

    *buffer = s_swap_endian(*buffer);
}

static struct mbediso_io* s_mbediso_io_from_file_unc(FILE* file)
{
    struct mbediso_io_unc* io = malloc(sizeof(struct mbediso_io_unc));

    if(!io)
        return NULL;

    io->tag = MBEDISO_IO_TAG_UNC;
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

static struct mbediso_io* s_mbediso_io_from_file_lz4(FILE* file, struct mbediso_lz4_header* header)
{
    struct mbediso_io_lz4* io = malloc(sizeof(struct mbediso_io_lz4));

    if(!io)
        return NULL;

    io->tag = MBEDISO_IO_TAG_LZ4;
    io->file = file;
    io->header = header;

    io->file_pos = -1;

    io->file_buffer_length = header->block_size + 4;
    io->file_buffer_pos = 0 - io->file_buffer_length;

    io->buffer_logical_pos = -1;
    io->buffer_length = 0;

    io->file_buffer = malloc(io->file_buffer_length);
    io->decompression_buffer = malloc(header->block_size);
    io->public_buffer = io->decompression_buffer;

    if(!io->file_buffer || !io->decompression_buffer)
    {
        free(io->file_buffer);
        free(io->decompression_buffer);
        free(io);
        return NULL;
    }

    return (struct mbediso_io*)io;
}

struct mbediso_io* mbediso_io_from_file(FILE* file, struct mbediso_lz4_header* header)
{
    if(!header)
        return s_mbediso_io_from_file_unc(file);
    else
        return s_mbediso_io_from_file_lz4(file, header);
}

static bool s_mbediso_io_lz4_prepare(struct mbediso_io_lz4* io, uint32_t logical_pos)
{
    if(logical_pos > io->buffer_logical_pos)
    {
        if(logical_pos < io->buffer_logical_pos + io->buffer_length)
            return true;

        // check for the case where we are on the last block and a position past the end was requested
        if(logical_pos < io->buffer_logical_pos + io->header->block_size)
            return false;
    }


    uint32_t block = logical_pos / io->header->block_size;
    if(block >= io->header->block_count)
        return false;

    uint32_t read_start = io->header->block_offsets[block];
    uint32_t to_read = 4 + io->header->block_size;
    if(block + 1 < io->header->block_count)
        to_read = io->header->block_offsets[block + 1] - read_start;

    if(to_read > io->file_buffer_length)
        to_read = io->file_buffer_length;

    // read data from file
    if(read_start != io->file_pos)
        fseek(io->file, read_start, SEEK_SET);

    io->file_pos = -1;

    int did_read = fread(io->file_buffer, 1, to_read, io->file);
    io->file_pos = read_start + did_read;

    io->file_buffer_pos = read_start;

    if(did_read < 4)
        return false;

    uint32_t compressed_length = *(uint32_t*)io->file_buffer;

    s_fix_le(&compressed_length);

    bool is_uncompressed = (compressed_length & 0x80000000);
    compressed_length &= ~(uint32_t)0x80000000;

    if(compressed_length > io->header->block_size)
        return false;

    int raw_data_bytes = did_read - 4;
    if(raw_data_bytes < (int)compressed_length)
        return false;

    const uint8_t* raw_data = io->file_buffer + 4;
    uint32_t decompressed_length = 0;

    if(!is_uncompressed)
    {
        decompressed_length = LZ4_decompress_safe((const char*)raw_data, (char*)io->decompression_buffer, compressed_length, io->header->block_size);
        io->public_buffer = io->decompression_buffer;
    }
    else
    {
        decompressed_length = compressed_length;
        io->public_buffer = raw_data;
    }

    if(decompressed_length == 0)
        return false;

    io->buffer_logical_pos = block * io->header->block_size;
    io->buffer_length = decompressed_length;

    // check that the block is not underlong
    if(logical_pos > io->buffer_logical_pos + io->buffer_length)
        return false;

    return true;
}

const uint8_t* mbediso_io_read_sector(struct mbediso_io* _io, uint32_t sector)
{
    if(!_io)
        return NULL;

    if(_io->tag == MBEDISO_IO_TAG_LZ4)
    {
        struct mbediso_io_lz4* io = (struct mbediso_io_lz4*)_io;

        size_t offset = sector * 2048;
        if(!s_mbediso_io_lz4_prepare(io, offset))
            return NULL;

        // printf("seeking %lx...\n", offset);

        if(io->buffer_logical_pos + io->buffer_length < offset + 2048)
            return NULL;

        return io->public_buffer + (offset - io->buffer_logical_pos);
    }
    else if(_io->tag == MBEDISO_IO_TAG_UNC)
    {
        struct mbediso_io_unc* io = (struct mbediso_io_unc*)_io;

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
            // printf("read failed...\n");

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

    if(_io->tag == MBEDISO_IO_TAG_LZ4)
    {
        struct mbediso_io_lz4* io = (struct mbediso_io_lz4*)_io;

        const size_t bytes_wanted = bytes;

        while(bytes > 0)
        {
            if(!s_mbediso_io_lz4_prepare(io, offset))
                return bytes_wanted - bytes;

            size_t can_read = (io->buffer_logical_pos + io->buffer_length) - offset;
            size_t start = offset - io->buffer_logical_pos;

            if(can_read > bytes)
                can_read = bytes;

            memcpy(dest, io->public_buffer + start, can_read);

            dest += can_read;
            bytes -= can_read;
            offset += can_read;
        }

        return bytes_wanted - bytes;
    }
    else if(_io->tag == MBEDISO_IO_TAG_UNC)
    {
        struct mbediso_io_unc* io = (struct mbediso_io_unc*)_io;

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

    if(_io->tag == MBEDISO_IO_TAG_LZ4)
    {
        struct mbediso_io_lz4* io = (struct mbediso_io_lz4*)_io;

        fclose(io->file);

        free(io->file_buffer);
        free(io->decompression_buffer);

        free(io);
    }
    else if(_io->tag == MBEDISO_IO_TAG_UNC)
    {
        struct mbediso_io_unc* io = (struct mbediso_io_unc*)_io;

        fclose(io->file);

        free(io->buffer);

        free(io);
    }
}
