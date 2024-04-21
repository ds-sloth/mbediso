#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "j9660.h"

bool j9660_fs_ctor(struct j9660_fs* fs)
{
    if(!fs)
        return false;

    fs->directories = NULL;
    fs->directory_count = 0;
    fs->directory_capacity = 0;

    // WARNING, root_dir_entry.filename is undefined
    fs->root_dir_entry.sector = 0;
    fs->root_dir_entry.length = 800;
    fs->root_dir_entry.directory = true;

    return true;
}

void j9660_fs_dtor(struct j9660_fs* fs)
{
    if(!fs)
        return;
}

uint32_t j9660_fs_alloc_directory(struct j9660_fs* fs)
{
    // make sure there is capacity for directory
    if(fs->directory_count + 1 > fs->directory_capacity)
    {
        size_t new_capacity = j9660_util_first_pow2(fs->directory_capacity + 1);
        struct j9660_directory* new_directories = realloc(fs->directories, new_capacity * sizeof(struct j9660_directory));
        if(new_directories)
        {
            fs->directories = new_directories;
            fs->directory_capacity = new_capacity;
        }
    }

    if(fs->directory_count + 1 > fs->directory_capacity)
        return J9660_NULL_REF;


    // constructor directory
    struct j9660_directory* dir = &fs->directories[fs->directory_count];

    if(!j9660_directory_ctor(dir))
        return J9660_NULL_REF;

    fs->directory_count++;

    return fs->directory_count - 1;
}

void j9660_fs_free_directory(struct j9660_fs* fs, uint32_t dir_index)
{
    j9660_directory_dtor(&fs->directories[dir_index]);
    // TODO: free and remove references to dir
}

const struct j9660_dir_entry* j9660_fs_lookup(const struct j9660_fs* fs, const char* path, uint32_t path_length)
{
    const char* segment_start = path;
    const char* const path_end = path + path_length;

    // currently assuming that fs is fully loaded with root at first directory index
    const struct j9660_dir_entry* cur_dir = &fs->root_dir_entry;

    while(segment_start < path_end)
    {
        const char* segment_end = segment_start;

        while(*segment_end != '/' && segment_end < path_end)
            segment_end++;

        // seek the segment
        if(segment_start != segment_end)
        {
            const struct j9660_dir_entry* found = j9660_directory_lookup(&fs->directories[cur_dir->sector], segment_start, segment_end - segment_start);

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
