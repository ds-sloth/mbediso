#include <stdlib.h>
#include <string.h>

#include "mbediso/dir.h"
#include "internal/io.h"
#include "internal/fs.h"
#include "internal/directory.h"

struct mbediso_dir* mbediso_opendir(struct mbediso_fs* fs, const char* name)
{
    struct mbediso_location loc;
    if(!mbediso_fs_lookup(fs, name, &loc))
        return NULL;

    if(!loc.directory)
        return NULL;

    if(loc.length == 0 && loc.sector >= fs->directory_count)
        return NULL;

    struct mbediso_dir* dir = malloc(sizeof(struct mbediso_dir));
    if(!dir)
        return NULL;

    // preloaded directory
    if(loc.length == 0 && loc.sector < fs->directory_count)
    {
        dir->directory = &fs->directories[loc.sector];
        dir->on_heap = false;
    }
    // need to load
    else
    {
        dir->directory = malloc(sizeof(struct mbediso_directory));
        dir->on_heap = true;

        struct mbediso_io* io = NULL;

        if(!dir->directory
            || !mbediso_directory_ctor(dir->directory)
            || (io = mbediso_fs_reserve_io(fs)) == NULL
            || mbediso_directory_load(dir->directory, io, loc.sector, loc.length) != 0
        )
        {
            mbediso_fs_release_io(fs, io);
            mbediso_closedir(dir);
            return NULL;
        }

        // loaded successfully, just need to clean up here
        mbediso_fs_release_io(fs, io);
    }

    dir->fs = fs;
    dir->entry_index = 0;
    return dir;
}

int mbediso_closedir(struct mbediso_dir* dir)
{
    if(!dir)
        return 0;

    if(dir->on_heap)
    {
        mbediso_directory_dtor(dir->directory);
        free(dir->directory);
    }

    free(dir);
    return 0;
}

const struct mbediso_dirent* mbediso_readdir(struct mbediso_dir* dir)
{
    if(dir->entry_index >= dir->directory->entry_count)
        return NULL;

    const struct mbediso_dir_entry* entry = &dir->directory->entries[dir->entry_index];

    dir->dirent.d_type = entry->l.directory ? MBEDISO_DT_DIR : MBEDISO_DT_REG;

    int name_res = mbediso_string_diff_reconstruct(
        dir->dirent.d_name,
        sizeof(dir->dirent.d_name),
        dir->directory->stringtable,
        dir->directory->entries,
        dir->directory->entry_count,
        sizeof(struct mbediso_dir_entry),
        dir->entry_index
    );

    if(name_res != 0)
    {
        // This should be unreachable
        return NULL;
    }

    dir->entry_index++;
    return &dir->dirent;
}
