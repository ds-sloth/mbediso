#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mbediso.h"

#include "internal/util.h"
#include "internal/fs.h"
#include "internal/io.h"

bool mbediso_fs_ctor(struct mbediso_fs* fs)
{
    if(!fs)
        return false;

    fs->archive_path = NULL;

    fs->directories = NULL;
    fs->directory_count = 0;
    fs->directory_capacity = 0;

    // WARNING, root_dir_entry.filename is undefined
    fs->root_dir_entry.sector = 0;
    fs->root_dir_entry.length = 800;
    fs->root_dir_entry.directory = true;

    return true;
}

void mbediso_fs_dtor(struct mbediso_fs* fs)
{
    if(!fs)
        return;

    if(fs->archive_path)
    {
        free(fs->archive_path);
        fs->archive_path = NULL;
    }
}

bool mbediso_fs_init_from_path(struct mbediso_fs* fs, const char* path)
{
    if(fs->archive_path)
        return false;

    int len = strlen(path);
    if(!len)
        return false;

    fs->archive_path = malloc(len + 1);
    if(!fs->archive_path)
        return false;

    strncpy(fs->archive_path, path, len + 1);

    return true;
}

uint32_t mbediso_fs_alloc_directory(struct mbediso_fs* fs)
{
    // make sure there is capacity for directory
    if(fs->directory_count + 1 > fs->directory_capacity)
    {
        size_t new_capacity = mbediso_util_first_pow2(fs->directory_capacity + 1);
        struct mbediso_directory* new_directories = realloc(fs->directories, new_capacity * sizeof(struct mbediso_directory));
        if(new_directories)
        {
            fs->directories = new_directories;
            fs->directory_capacity = new_capacity;
        }
    }

    if(fs->directory_count + 1 > fs->directory_capacity)
        return MBEDISO_NULL_REF;


    // constructor directory
    struct mbediso_directory* dir = &fs->directories[fs->directory_count];

    if(!mbediso_directory_ctor(dir))
        return MBEDISO_NULL_REF;

    fs->directory_count++;

    return fs->directory_count - 1;
}

void mbediso_fs_free_directory(struct mbediso_fs* fs, uint32_t dir_index)
{
    mbediso_directory_dtor(&fs->directories[dir_index]);
    // TODO: free and remove references to dir
}

const struct mbediso_dir_entry* mbediso_fs_lookup(const struct mbediso_fs* fs, const char* path, uint32_t path_length)
{
    const char* segment_start = path;
    const char* const path_end = path + path_length;

    // currently assuming that fs is fully loaded with root at first directory index
    const struct mbediso_dir_entry* cur_dir = &fs->root_dir_entry;

    while(segment_start < path_end)
    {
        const char* segment_end = segment_start;

        while(*segment_end != '/' && segment_end < path_end)
            segment_end++;

        // seek the segment
        if(segment_start != segment_end)
        {
            const struct mbediso_dir_entry* found = mbediso_directory_lookup(&fs->directories[cur_dir->sector], segment_start, segment_end - segment_start);

            if(!found)
                return NULL;

            if(segment_end == path_end)
                return found;

            // fail if got a non-directory, or if not fully loaded
            if(!found->directory || found->length != 0 || found->sector >= fs->directory_count)
                return NULL;

            cur_dir = found;
        }

        segment_start = segment_end + 1;
    }

    return cur_dir;
}

struct mbediso_fs_scan_stack_frame
{
    uint32_t dir_index;
    uint32_t sector;
    uint32_t length;
    uint32_t recurse_child; // which child to expand in this step
};

int mbediso_fs_full_scan(struct mbediso_fs* fs, struct mbediso_io* io)
{
    if(!fs->root_dir_entry.directory || fs->root_dir_entry.length == 0 || fs->directory_count != 0)
        return -1;

    struct mbediso_fs_scan_stack_frame stack[16];
    uint32_t stack_level = 0;

    stack[stack_level].dir_index = mbediso_fs_alloc_directory(fs);
    stack[stack_level].sector = fs->root_dir_entry.sector;
    stack[stack_level].length = fs->root_dir_entry.length;
    stack[stack_level].recurse_child = MBEDISO_NULL_REF;

    if(stack[stack_level].dir_index == MBEDISO_NULL_REF)
        return -1;

    while(stack_level < 16)
    {
        struct mbediso_fs_scan_stack_frame* const cur_frame = &stack[stack_level];

        struct mbediso_directory* dir = &fs->directories[cur_frame->dir_index];

        // read directory itself on first step
        if(cur_frame->recurse_child == MBEDISO_NULL_REF)
        {
            mbediso_directory_load(dir, io, cur_frame->sector, cur_frame->length);

            // now, start expanding actual children
            cur_frame->recurse_child = 0;
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
            stack[stack_level].recurse_child = MBEDISO_NULL_REF;
            stack[stack_level].sector = cur_entry->sector;
            stack[stack_level].length = cur_entry->length;

            // add reference to child directory index
            cur_entry->length = 0;
            cur_entry->sector = new_dir_index;
        }
    }

    // success: add reference to root directory index
    fs->root_dir_entry.length = 0;
    fs->root_dir_entry.sector = stack[0].dir_index;

    return 0;
}

struct mbediso_io* mbediso_fs_reserve_io(struct mbediso_fs* fs)
{
    if(!fs || !fs->archive_path)
        return NULL;

    FILE* f = fopen(fs->archive_path, "rb");
    if(!f)
        return NULL;

    struct mbediso_io* io = mbediso_io_from_file(f);

    if(!io)
        fclose(f);

    return io;
}

void mbediso_fs_release_io(struct mbediso_fs* fs, struct mbediso_io* io)
{
    if(!fs || !io)
        return;

    mbediso_io_close(io);
}
