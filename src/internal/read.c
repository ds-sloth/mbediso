#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "j9660.h"

int utf16be_to_utf8(uint8_t* restrict dest, ptrdiff_t capacity, const uint8_t* restrict src, size_t bytes)
{
    if(bytes & 1)
        return -1;

    size_t offset = 0;

    while(offset + 1 < bytes)
    {
        uint32_t codepoint = 0;

        uint32_t ucs2 = (uint32_t)src[offset] * 256 + (uint32_t)src[offset + 1];
        offset += 2;

        // a low surrogate without a preceding high surrogate is invalid
        if(ucs2 >= 0xDC00 && ucs2 < 0xE000)
            return -1;
        // if high surrogate, also read low surrogate
        else if(ucs2 >= 0xD800 && ucs2 < 0xDC00)
        {
            if(offset + 2 > bytes)
                return -1;

            // store high surrogate
            codepoint = (ucs2 - 0xD800) * 0x0400;

            // read low surrogate
            uint32_t low_surrogate = (uint32_t)src[offset] * 256 + (uint32_t)src[offset + 1];
            offset += 2;

            // add low surrogate if valid
            if(low_surrogate >= 0xDC00 && low_surrogate < 0xE000)
                codepoint += low_surrogate - 0xDC00;
            else
                return -1;
        }
        // otherwise, we have a BMP codepoint
        else
            codepoint = ucs2;

        // output codepoint as UTF8
        if(codepoint < 0x80)
        {
            capacity -= 1;
            if(capacity <= 0)
                return -1;

            *(dest++) = (uint8_t)codepoint;
        }
        else if(codepoint < 0x800)
        {
            capacity -= 2;
            if(capacity <= 0)
                return -1;

            *(dest++) = (uint8_t)(0xC0 | (codepoint / 0x40));
            *(dest++) = (uint8_t)(0x80 | (codepoint & 0x3F));
        }
        else if(codepoint < 0x10000)
        {
            capacity -= 3;
            if(capacity <= 0)
                return -1;

            *(dest++) = (uint8_t)(0xE0 | (codepoint / 0x1000));
            *(dest++) = (uint8_t)(0x80 | ((codepoint / 0x40) & 0x3F));
            *(dest++) = (uint8_t)(0x80 | (codepoint & 0x3F));
        }
        else if(codepoint < 0x110000)
        {
            capacity -= 4;
            if(capacity <= 0)
                return -1;

            *(dest++) = (uint8_t)(0xF0 | (codepoint / 0x40000));
            *(dest++) = (uint8_t)(0x80 | ((codepoint / 0x1000) & 0x3F));
            *(dest++) = (uint8_t)(0x80 | ((codepoint / 0x40) & 0x3F));
            *(dest++) = (uint8_t)(0x80 | (codepoint & 0x3F));
        }
        else
            return -1;
    }

    // add null terminator
    *dest = 0;

    return 0;
}

/**
 * \brief function to read a single directory entry
 *
 * \param entry Raw entry object to output filename, location, and directory flags to. In partial failure (unreadable entry), sets filename to an empty string.
 * \param buffer Input buffer (generally a portion of a pre-loaded sector)
 * \param buffer_size Number of bytes that may be legally read from buffer (bytes remaining in sector)
 *
 * \returns On success or partial success, number of bytes of buffer consumed (at least 33). On total failure (the parent directory should be abandoned), returns -1.
 **/
int read_dir_entry(struct j9660_raw_entry* entry, const uint8_t* buffer, int buffer_size)
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
    else if(utf16be_to_utf8(entry->filename.buffer, (ptrdiff_t)sizeof(struct j9660_filename), buffer + 33, filename_length_bytes))
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

struct j9660_read_stack_frame
{
    uint32_t dir_index;
    uint32_t sector;
    uint32_t offset;
    uint32_t length;
};

int scan_dir(struct j9660_fs* fs, struct j9660_io* io, uint32_t sector, uint32_t length)
{
    int old_size = 0;
    int total_size = 0;

    struct j9660_raw_entry entry[2];
    uint32_t entry_index = 0;

    // add a buffer for the full path currently being constructed, use this to recover the directory's name for sort detection when needed...
    uint8_t full_path_buffer[1024];

    full_path_buffer[0] = '/';
    uint16_t full_path_buffer_size = 1;

    struct j9660_read_stack_frame stack[16];
    uint32_t stack_level = 0;

    stack[stack_level].dir_index = j9660_fs_alloc_directory(fs);
    stack[stack_level].offset = 0;
    stack[stack_level].sector = sector;
    stack[stack_level].length = length;

    if(stack[stack_level].dir_index == J9660_NULL_REF)
        return -1;


    const uint8_t* buffer[2] = {NULL, NULL};
    bool buffer_dirty[2] = {true, true};

    while(stack_level < 16)
    {
        struct j9660_read_stack_frame* const cur_frame = &stack[stack_level];

        struct j9660_directory* dir = &fs->directories[cur_frame->dir_index];

        // done reading a directory
        if(cur_frame->offset >= cur_frame->length)
        {
            old_size += dir->stringtable_size;

            j9660_directory_finish(dir);

            total_size += dir->stringtable_size;

            // printf("In directory [%s] (%d):\n", full_path_buffer, (int)dir->utf8_sorted);
            // for(size_t e = 0; e < dir->entry_count; e++)
            // {
                // const struct j9660_dir_entry* real_entry = &dir->entries[e];
                // if(j9660_string_diff_reconstruct(filename_buffer, 1024, dir->stringtable, dir->entries, dir->entry_count, sizeof(struct j9660_dir_entry), e))
                    // filename_buffer[0] = '\0';
                // printf("  %d offset %x, length %x, filename [%s]\n", (int)real_entry->directory, real_entry->sector * 2048, real_entry->length, filename_buffer);
            // }

            // j9660_fs_free_directory(fs, dir);

            // add reference to the child directory!!!

            // invalidate buffer used for this directory
            buffer_dirty[stack_level & 1] = true;

            // reduce the stack level
            stack_level--;

            // prepare to resume reading
            if(stack_level < 16)
            {
                entry_index = 3;

                // reduce the full path buffer
                uint16_t old_full_path_buffer_size = full_path_buffer_size;

                while(full_path_buffer_size > 1)
                {
                    full_path_buffer_size--;

                    if(full_path_buffer[full_path_buffer_size - 1] == '/')
                        break;
                }

                // copy the current directory name to the previous entry for the parent to continue checking sorting
                memcpy(entry[0].filename.buffer, &full_path_buffer[full_path_buffer_size], old_full_path_buffer_size - full_path_buffer_size - 1);
                entry[0].filename.buffer[old_full_path_buffer_size - full_path_buffer_size - 1] = '\0';

                full_path_buffer[full_path_buffer_size] = '\0';
            }

            continue;
        }

        if(buffer_dirty[stack_level & 1])
        {
            buffer[stack_level & 1] = j9660_io_read_sector(io, cur_frame->sector + (cur_frame->offset / 2048), stack_level & 1);
            buffer_dirty[stack_level & 1] = false;
        }

        if(!buffer[stack_level & 1])
            return -1;

        struct j9660_raw_entry* const cur_entry = &entry[entry_index & 1];

        int ret = read_dir_entry(cur_entry, buffer[stack_level & 1] + (cur_frame->offset % 2048), 2048 - (cur_frame->offset % 2048));

        // printf("%d %p\n", (int)offset, buffer[stack_level & 1] + (offset % 2048));

        if(ret < 33)
        {
            // any cleanup needed?
            return -1;
        }

        buffer_dirty[stack_level & 1] = ((cur_frame->offset % 2048) + ret >= 2048);
        cur_frame->offset += ret;

        if(!buffer_dirty[stack_level & 1] && buffer[stack_level & 1][cur_frame->offset % 2048] == '\0')
        {
            buffer_dirty[stack_level & 1] = true;
            cur_frame->offset += 2048 - (cur_frame->offset % 2048);
        }

        // skip on partial failure
        if(cur_entry->filename.buffer[0] == '\0')
            continue;

        if(j9660_directory_push(dir, cur_entry))
        {
            // think about how to cleanly handle full failure... if no resources are owned by stack (ideal), can simply cleanup after failure
            // j9660_fs_free_directory(fs, dir);
            return -1;
        }

        // printf("%d offset %x, length %x, filename %s\n", (int)entry[entry_index & 1].directory, entry[entry_index & 1].sector * 2048, entry[entry_index & 1].length, entry[entry_index & 1].filename.buffer);

        if(entry_index > 2)
        {
            const uint8_t* prev_fn = entry[!(entry_index & 1)].filename.buffer;
            const uint8_t* this_fn = cur_entry->filename.buffer;

            // check sort order
            int sort_order = strncmp((const char*)prev_fn, (const char*)this_fn, sizeof(struct j9660_filename));
            if(sort_order >= 0)
            {
                printf("Unsorted at [%s] [%s]\n", prev_fn, this_fn);
                dir->utf8_sorted = false;
            }
        }

        entry_index++;

        // consider opening a new frame
        if(cur_entry->directory && entry_index > 2)
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
                // instead, mark it properly
                continue;
            }

            // avoid overflowing the stack
            if(stack_level + 1 >= 16)
            {
                // this should be a total failure
                continue;
            }

            // check that we can alloc the directory
            uint32_t new_dir_index = j9660_fs_alloc_directory(fs);
            if(new_dir_index == J9660_NULL_REF)
            {
                // this should be a total failure
                continue;
            }

            // (directory pointer got invalidated by allocation above)
            dir = &fs->directories[cur_frame->dir_index];

            // add reference to child directory index
            dir->entries[dir->entry_count - 1].length = 0;
            dir->entries[dir->entry_count - 1].sector = new_dir_index;

            // okay, we can expand.
            stack_level++;
            entry_index = 0;

            buffer_dirty[stack_level & 1] = true;

            stack[stack_level].dir_index = new_dir_index;
            stack[stack_level].offset = 0;
            stack[stack_level].sector = cur_entry->sector;
            stack[stack_level].length = cur_entry->length;

            // expand the full path buffer
            size_t fn_len = strlen((const char*)cur_entry->filename.buffer);
            if(full_path_buffer_size + fn_len + 2 < 1024)
            {
                memcpy(&full_path_buffer[full_path_buffer_size], cur_entry->filename.buffer, fn_len);
                full_path_buffer_size += fn_len + 1;
                full_path_buffer[full_path_buffer_size - 1] = '/';
                full_path_buffer[full_path_buffer_size] = '\0';
            }
        }
    }

    printf("Total size: %d (non-diff %d)\n", total_size, old_size);

    return 0;
}

int find_joliet_root(struct j9660_fs* fs, struct j9660_io* io)
{
    struct j9660_raw_entry entry;

    uint32_t try_sector = 16;

    // if 255, more than 16 sectors tried, or out of I/O, fail
    const uint8_t* buffer = NULL;

    // find the Joliet sector
    while(true)
    {
        buffer = j9660_io_read_sector(io, try_sector, false);

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
    int bytes_read = read_dir_entry(&entry, &buffer[156], 190 - 156);
    if(bytes_read < 33)
        return -2;

    if(!entry.directory)
        return -3;

    fs->root_dir_entry.sector = entry.sector;
    fs->root_dir_entry.length = entry.length;
    fs->root_dir_entry.directory = true;

    return 0;
}
