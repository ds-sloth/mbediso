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

#include "mbediso.h"

#include "internal/util.h"
#include "internal/fs.h"
#include "internal/io.h"
#include "internal/lz4_header.h"
#include "internal/mutex/mutex.h"

bool mbediso_fs_ctor(struct mbediso_fs* fs)
{
    if(!fs)
        return false;

    fs->archive_path = NULL;
    fs->lz4_header = NULL;

    fs->directories = NULL;
    fs->directory_count = 0;
    fs->directory_capacity = 0;

    fs->root_dir_entry.sector = 0;
    fs->root_dir_entry.length = 800;
    fs->root_dir_entry.directory = false;

    fs->fully_scanned = false;

    /* tracks the allocated and used IO instances */
    fs->io_pool = NULL;
    fs->io_pool_used = 0;
    fs->io_pool_size = 0;
    fs->io_pool_capacity = 0;

    /* allocate mutexes */
    fs->io_pool_mutex = mbediso_mutex_alloc();
    if(!fs->io_pool_mutex)
        return false;

    fs->lookup_mutex = mbediso_mutex_alloc();
    if(!fs->lookup_mutex)
    {
        mbediso_mutex_free(fs->io_pool_mutex);
        fs->io_pool_mutex = NULL;
        return false;
    }

    return true;
}

void mbediso_fs_dtor(struct mbediso_fs* fs)
{
    if(!fs)
        return;

    if(fs->directories)
    {
        for(uint32_t i = 0; i < fs->directory_count; i++)
            mbediso_directory_dtor(&fs->directories[i]);

        free(fs->directories);
        fs->directories = NULL;
    }

    if(fs->archive_path)
    {
        free(fs->archive_path);
        fs->archive_path = NULL;
    }

    if(fs->io_pool)
    {
        if(fs->io_pool_used > 0)
        {
            // big problem
        }

        for(uint32_t i = 0; i < fs->io_pool_size; i++)
            mbediso_io_close(fs->io_pool[i]);

        free(fs->io_pool);
        fs->io_pool = NULL;
    }

    if(fs->lz4_header)
    {
        mbediso_lz4_header_free(fs->lz4_header);
        fs->lz4_header = NULL;
    }

    if(fs->io_pool_mutex)
    {
        mbediso_mutex_free(fs->io_pool_mutex);
        fs->io_pool_mutex = NULL;
    }

    if(fs->lookup_mutex)
    {
        mbediso_mutex_free(fs->lookup_mutex);
        fs->lookup_mutex = NULL;
    }
}

static void s_mbediso_fs_adopt_fp(struct mbediso_fs* fs, FILE* fp);

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

    /* detect lz4 archive */
    FILE* f = fopen(fs->archive_path, "rb");
    if(f)
    {
        fs->lz4_header = mbediso_lz4_header_load(f);
        s_mbediso_fs_adopt_fp(fs, f);
    }

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


    // construct directory
    struct mbediso_directory* dir = &fs->directories[fs->directory_count];

    if(!mbediso_directory_ctor(dir))
        return MBEDISO_NULL_REF;

    fs->directory_count++;

    return fs->directory_count - 1;
}

void mbediso_fs_free_directory(struct mbediso_fs* fs, uint32_t dir_index)
{
    mbediso_directory_dtor(&fs->directories[dir_index]);

    if(dir_index == fs->directory_count - 1)
        fs->directory_count--;
    else
    {
        // TODO: create free list
    }
}

static bool s_mbediso_fs_load_location(struct mbediso_fs* fs, struct mbediso_io* io, struct mbediso_location* location)
{
    uint32_t new_dir_index = mbediso_fs_alloc_directory(fs);

    if(new_dir_index == MBEDISO_NULL_REF)
        return false;

    if(mbediso_directory_load(&fs->directories[new_dir_index], io, location->sector, location->length) != 0)
    {
        mbediso_fs_free_directory(fs, new_dir_index);
        return false;
    }

    // now save the loaded directory to its location (in its parent)
    location->sector = new_dir_index;
    location->length = 0;
    return true;
}

static bool s_mbediso_check_path_segments(const char* path, bool* skip_segment, bool* skip_segment_end)
{
    const int path_part_bound = skip_segment_end - skip_segment;

    // check which paths to skip (`.`, victims of `..`, and invalid `..`)
    const char* segment_start = path;
    int path_part = 0;

    while(*segment_start != '\0' && path_part < path_part_bound)
    {
        const char* segment_end = segment_start;

        while(*segment_end != '/' && *segment_end != '\0')
            segment_end++;

        // empty segment
        if(segment_start == segment_end)
            skip_segment[path_part] = true;
        // normal segment
        else if(*segment_start != '.')
            skip_segment[path_part] = false;
        // `.`
        else if(segment_end == segment_start + 1)
            skip_segment[path_part] = true;
        // `..`
        else if(segment_end == segment_start + 2 && *(segment_start + 1) == '.')
        {
            // skip `..` itself
            skip_segment[path_part] = true;

            // cancel the most recent non-skipped segment
            int i;
            for(i = path_part - 1; i >= 0; i--)
            {
                if(!skip_segment[i])
                {
                    skip_segment[i] = true;
                    break;
                }
            }

            // invalid `..`
            if(i < 0)
                return false;
        }
        // normal segment starting with `.`
        else
            skip_segment[path_part] = false;

        // immediately return when reaching end of string
        if(*segment_end == '\0')
            return true;

        // begin next segment if not at end of string
        segment_start = segment_end + 1;
        path_part++;
    }

    // more than allowed segment count, invalid path
    if(*segment_start != '\0')
        return false;

    return true;
}

bool mbediso_fs_lookup(struct mbediso_fs* fs, const char* path, struct mbediso_location* out)
{
    // check which paths to skip (`.`, victims of `..`, and invalid `..`)
    // array initialized by callee
    bool skip_segment[16];

    // check that path is valid, and which path segments to skip
    if(!s_mbediso_check_path_segments(path, skip_segment + 0, skip_segment + 16))
        return false;


    struct mbediso_io* io = NULL;
    struct mbediso_location* cur_loc = &fs->root_dir_entry;

    mbediso_mutex_lock(fs->lookup_mutex);

    const char* segment_start = path;
    int path_part = 0;

    while(*segment_start != '\0')
    {
        const char* segment_end = segment_start;

        while(*segment_end != '/' && *segment_end != '\0')
            segment_end++;

        // seek the segment
        if(!skip_segment[path_part])
        {
            // check for directory that is partially / incorrectly loaded
            if(cur_loc->length == 0 && cur_loc->sector >= fs->directory_count)
            {
                mbediso_fs_release_io(fs, io);
                mbediso_mutex_unlock(fs->lookup_mutex);
                return false;
            }
            // loaded directory
            else if(cur_loc->length == 0)
            {
                if(!mbediso_directory_lookup(&fs->directories[cur_loc->sector], segment_start, segment_end - segment_start, &cur_loc))
                {
                    mbediso_fs_release_io(fs, io);
                    mbediso_mutex_unlock(fs->lookup_mutex);
                    return false;
                }
            }
            // unloaded directory
            else
            {
                if(!io)
                    io = mbediso_fs_reserve_io(fs);

                if(!io)
                {
                    mbediso_mutex_unlock(fs->lookup_mutex);
                    return false;
                }

                // try to load to RAM (unless we failed earlier)
                if(cur_loc != out && s_mbediso_fs_load_location(fs, io, cur_loc))
                {
                    // try loading again
                    continue;
                }

                // do the rest of the lookup straight from disk
                cur_loc = out;

                if(!mbediso_directory_lookup_unloaded(io, cur_loc->sector, cur_loc->length, segment_start, segment_end - segment_start, out))
                {
                    mbediso_fs_release_io(fs, io);
                    mbediso_mutex_unlock(fs->lookup_mutex);
                    return false;
                }
            }

            if(*segment_end == '\0')
            {
                // we're done!
                break;
            }

            // fail if got a non-directory, or if not fully loaded
            if(!cur_loc->directory)
            {
                mbediso_fs_release_io(fs, io);
                mbediso_mutex_unlock(fs->lookup_mutex);
                return false;
            }
        }

        segment_start = segment_end + 1;
        path_part++;
    }

    // prefer to load a directory before returning it
    if(cur_loc != out && cur_loc->directory && cur_loc->length != 0)
    {
        if(!io)
            io = mbediso_fs_reserve_io(fs);

        s_mbediso_fs_load_location(fs, io, cur_loc);
    }

    mbediso_fs_release_io(fs, io);
    mbediso_mutex_unlock(fs->lookup_mutex);

    *out = *cur_loc;
    return true;
}

struct mbediso_fs_scan_stack_frame
{
    // this is currently safe, but should become an index if the directory entries become allocated in a single vector
    struct mbediso_location* location;
    // could track sector here, if loaded during this scan
    uint32_t recurse_child; // which child to expand in this step
};

int mbediso_fs_full_scan(struct mbediso_fs* fs, struct mbediso_io* io)
{
    // already scanned? then there's nothing to do
    if(fs->fully_scanned)
        return 0;

    // make sure root has been found
    if(!fs->root_dir_entry.directory)
        return -1;

    struct mbediso_fs_scan_stack_frame stack[16];
    uint32_t stack_level = 0;

    stack[stack_level].location = &fs->root_dir_entry;
    stack[stack_level].recurse_child = 0;

    while(stack_level < 16)
    {
        struct mbediso_fs_scan_stack_frame* const cur_frame = &stack[stack_level];

        // need to load directory
        if(cur_frame->location->length != 0)
        {
            if(!s_mbediso_fs_load_location(fs, io, cur_frame->location))
            {
                // this could be a total failure
                stack_level--;
                continue;
            }
        }

        // check that directory index is valid
        if(cur_frame->location->sector >= fs->directory_count)
            return -1;

        struct mbediso_directory* const dir = &fs->directories[cur_frame->location->sector];

        // done expanding children
        if(cur_frame->recurse_child >= dir->entry_count)
        {
            stack_level--;
            continue;
        }

        struct mbediso_dir_entry* cur_entry = &dir->entries[cur_frame->recurse_child];
        cur_frame->recurse_child++;

        // skip non-directories
        if(!cur_entry->l.directory)
            continue;

        // open a new frame for the child

        // disabled loop checking for now
#if 0
        // check for loops
        uint32_t loop_level;
        for(loop_level = 0; loop_level <= stack_level; loop_level++)
        {
            if(stack[loop_level].sector == cur_entry->l.sector)
                break;
        }

        // don't expand a loop...!
        if(loop_level <= stack_level)
        {
            // instead, mark it properly... "SOON".
            // should be something like setting the length of cur_entry to zero and the sector to the dir_index of the loop_level
            continue;
        }
#endif

        // avoid overflowing the stack
        if(stack_level + 1 >= 16)
        {
            // this should be a total failure
            continue;
        }

        // okay, we can expand.
        stack_level++;

        stack[stack_level].location = &cur_entry->l;
        stack[stack_level].recurse_child = 0;
    }

    // success: mark filesystem as scanned
    fs->fully_scanned = true;

    return 0;
}

static struct mbediso_io* s_mbediso_fs_construct_io(struct mbediso_fs* fs, FILE* f)
{
    if(!fs || !fs->archive_path)
        return NULL;

    FILE* f_to_close = NULL;
    if(!f)
    {
        f = fopen(fs->archive_path, "rb");
        f_to_close = f;
    }

    if(!f)
        return NULL;

    struct mbediso_io* io = mbediso_io_from_file(f, fs->lz4_header);

    if(!io && f_to_close)
        fclose(f_to_close);

    return io;
}

static struct mbediso_io* s_mbediso_fs_reserve_io_fp(struct mbediso_fs* fs, FILE* fp)
{
    if(!fs)
        return NULL;

    mbediso_mutex_lock(fs->io_pool_mutex);

    if(fs->io_pool_size > fs->io_pool_used)
    {
        struct mbediso_io* ret = fs->io_pool[fs->io_pool_used++];
        mbediso_mutex_unlock(fs->io_pool_mutex);
        return ret;
    }

    if(fs->io_pool_size + 1 > fs->io_pool_capacity)
    {
        size_t new_capacity = mbediso_util_first_pow2(fs->io_pool_capacity + 1);
        struct mbediso_io** new_io_pool = realloc(fs->io_pool, new_capacity * sizeof(struct mbediso_io*));
        if(new_io_pool)
        {
            fs->io_pool = new_io_pool;
            fs->io_pool_capacity = new_capacity;
        }
    }

    if(fs->io_pool_size + 1 > fs->io_pool_capacity)
    {
        mbediso_mutex_unlock(fs->io_pool_mutex);
        return NULL;
    }

    struct mbediso_io* io = s_mbediso_fs_construct_io(fs, fp);
    if(!io)
    {
        mbediso_mutex_unlock(fs->io_pool_mutex);
        return NULL;
    }

    fs->io_pool[fs->io_pool_size++] = io;
    fs->io_pool_used++;

    mbediso_mutex_unlock(fs->io_pool_mutex);

    return io;
}

struct mbediso_io* mbediso_fs_reserve_io(struct mbediso_fs* fs)
{
    return s_mbediso_fs_reserve_io_fp(fs, NULL);
}

void mbediso_fs_release_io(struct mbediso_fs* fs, struct mbediso_io* io)
{
    if(!fs || !io)
        return;

    mbediso_mutex_lock(fs->io_pool_mutex);

    for(uint32_t i = 0; i < fs->io_pool_used; i++)
    {
        if(fs->io_pool[i] == io)
        {
            fs->io_pool_used--;
            fs->io_pool[i] = fs->io_pool[fs->io_pool_used];
            fs->io_pool[fs->io_pool_used] = io;
            mbediso_mutex_unlock(fs->io_pool_mutex);
            return;
        }
    }

    // if we got here, this is a big problem
    mbediso_mutex_unlock(fs->io_pool_mutex);
}

/* the purpose of this function is to adopt the file pointer used for lz4 detection and initialization into the IO pool */
static void s_mbediso_fs_adopt_fp(struct mbediso_fs* fs, FILE* fp)
{
    if(!fp)
        return;

    struct mbediso_io* io = s_mbediso_fs_reserve_io_fp(fs, fp);
    if(io)
        mbediso_fs_release_io(fs, io);
    else
        fclose(fp);
}
