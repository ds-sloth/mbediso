#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mbediso.h"

#include "internal/io.h"
#include "internal/fs.h"
#include "internal/util.h"

int mbediso_read_dir_entry(struct mbediso_raw_entry* entry, const uint8_t* buffer, int buffer_size)
{
    if(buffer_size < 33 || buffer[0] < 33 || buffer[0] > buffer_size)
        return -1;

    // check that filename is stored successfully and safely
    // note: be permissive about filename_length, in practice allowing up to 111 UTF16 characters
    uint8_t filename_length_bytes = buffer[32];

    if(buffer[0] < 33 + filename_length_bytes || filename_length_bytes > 222)
        return -1;

    // test for all unsupported features
    uint8_t flags = buffer[25];
    uint8_t extended_attr = buffer[1];
    uint8_t unit_size = buffer[26];
    uint8_t interleave_gap = buffer[27];
    uint8_t volume_low = buffer[28];
    uint8_t volume_high = buffer[29];

    if((flags & 0xFC) || extended_attr || unit_size || interleave_gap || volume_low != 1 || volume_high)
    {
        entry->filename.buffer[0] = '\0';
        return buffer[0];
    }


    // convert filename
    if(filename_length_bytes == 1)
    {
        entry->filename.buffer[0] = '.';
        entry->filename.buffer[1] = (buffer[33]) ? '.' : '\0';
        entry->filename.buffer[2] = '\0';
    }
    else if(mbediso_util_utf16be_to_utf8(entry->filename.buffer, (ptrdiff_t)sizeof(struct mbediso_filename), buffer + 33, filename_length_bytes))
    {
        entry->filename.buffer[0] = '\0';
        return buffer[0];
    }


    // set directory flag
    entry->directory = flags & 0x02;

    // use little-endian copies of sector and length
    entry->sector = buffer[ 2] * 0x00000001 + buffer[ 3] * 0x00000100 + buffer[ 4] * 0x00010000 + buffer[ 5] * 0x01000000;
    entry->length = buffer[10] * 0x00000001 + buffer[11] * 0x00000100 + buffer[12] * 0x00010000 + buffer[13] * 0x01000000;


    return buffer[0];
}

struct mbediso_read_stack_frame
{
    uint32_t dir_index;
    uint32_t sector;
    uint32_t length;
    uint32_t recurse_child; // which child to expand in this step
};

int scan_dir(struct mbediso_fs* fs, struct mbediso_io* io, uint32_t sector, uint32_t length)
{
    int old_size = 0;
    int total_size = 0;

    struct mbediso_raw_entry entry[2];

    struct mbediso_read_stack_frame stack[16];
    uint32_t stack_level = 0;

    stack[stack_level].dir_index = mbediso_fs_alloc_directory(fs);
    stack[stack_level].sector = sector;
    stack[stack_level].length = length;
    stack[stack_level].recurse_child = 0;

    if(stack[stack_level].dir_index == MBEDISO_NULL_REF)
        return -1;

    while(stack_level < 16)
    {
        struct mbediso_read_stack_frame* const cur_frame = &stack[stack_level];

        struct mbediso_directory* dir = &fs->directories[cur_frame->dir_index];

        // read directory itself on first step
        if(cur_frame->recurse_child < 2)
        {
            mbediso_directory_load(dir, io, cur_frame->sector, cur_frame->length);

            // now, start expanding actual children
            cur_frame->recurse_child = 2;
        }

        // done expanding children
        if(cur_frame->recurse_child >= dir->entry_count)
        {
            stack_level--;
            continue;
        }

        struct mbediso_dir_entry* cur_entry = &dir->entries[cur_frame->recurse_child];
        cur_frame->recurse_child++;

        // consider opening a new frame
        if(cur_entry->directory)
        {
            // check for loops
            uint32_t loop_level;
            for(loop_level = 0; loop_level <= stack_level; loop_level++)
            {
                if(stack[loop_level].sector == cur_entry->sector)
                    break;
            }

            // don't expand a loop...!
            if(loop_level <= stack_level)
            {
                // instead, mark it properly... "SOON".
                // should be something like setting the length of cur_entry to zero and the sector to the dir_index of the loop_level
                continue;
            }

            // avoid overflowing the stack
            if(stack_level + 1 >= 16)
            {
                // this should be a total failure
                continue;
            }

            // check that we can alloc the directory
            uint32_t new_dir_index = mbediso_fs_alloc_directory(fs);
            if(new_dir_index == MBEDISO_NULL_REF)
            {
                // this should be a total failure
                continue;
            }

            // (directory pointer got invalidated by allocation above)
            dir = &fs->directories[cur_frame->dir_index];

            // okay, we can expand.
            stack_level++;

            stack[stack_level].dir_index = new_dir_index;
            stack[stack_level].recurse_child = 0;
            stack[stack_level].sector = cur_entry->sector;
            stack[stack_level].length = cur_entry->length;

            // add reference to child directory index
            cur_entry->length = 0;
            cur_entry->sector = new_dir_index;
        }
    }

    printf("Total size: %d (non-diff %d)\n", total_size, old_size);

    return 0;
}

int find_joliet_root(struct mbediso_fs* fs, struct mbediso_io* io)
{
    struct mbediso_raw_entry entry;

    uint32_t try_sector = 16;

    // if 255, more than 16 sectors tried, or out of I/O, fail
    const uint8_t* buffer = NULL;

    // find the Joliet sector
    while(true)
    {
        buffer = mbediso_io_read_sector(io, try_sector);

        if(!buffer || buffer[0] == 255)
            return -1;

        if(buffer[0] == 2 // supplementary volume descriptor / enhanced volume descriptor
            && buffer[1] == 'C' && buffer[2] == 'D' && buffer[3] == '0' && buffer[4] == '0' && buffer[5] == '1' // ISO magic numbers
            && buffer[88] == 0x25 && buffer[89] == 0x2F && buffer[90] == 0x45 // UCS-2 level 3 (UTF-16) escape sequence
            && buffer[881] == 1 // supplementary volume descriptor
            )
            break;

        if(try_sector == 31)
            return -1;

        try_sector++;
    }

    // app range, begin 883 end 1395
    // root directory, begin 157 end 190
    int bytes_read = mbediso_read_dir_entry(&entry, &buffer[156], 190 - 156);
    if(bytes_read < 33)
        return -2;

    if(!entry.directory)
        return -3;

    fs->root_dir_entry.sector = entry.sector;
    fs->root_dir_entry.length = entry.length;
    fs->root_dir_entry.directory = true;

    return 0;
}
