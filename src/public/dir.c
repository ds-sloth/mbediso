#include <stdlib.h>
#include <string.h>

#include "mbediso/dir.h"
#include "internal/io.h"
#include "internal/fs.h"
#include "internal/directory.h"

struct mbediso_dir* mbediso_opendir(struct mbediso_fs* fs, const char* name)
{
    const struct mbediso_dir_entry* dir_entry  = mbediso_fs_lookup(fs, name, strlen(name));
    if (!dir_entry
        || !dir_entry->directory
        || dir_entry->length != 0
        || dir_entry->sector >= fs->directory_count)
    {
        return NULL;
    }

    const struct mbediso_directory* directory = &fs->directories[dir_entry->sector];

    struct mbediso_dir* dir = malloc(sizeof(struct mbediso_dir));
    if (!dir)
        return NULL;

    dir->fs = fs;
    dir->directory = directory;
    dir->entry_index = 0;
    return dir;
}

int mbediso_closedir(struct mbediso_dir* dir)
{
    free(dir);
    return 0;
}

const struct mbediso_dirent* mbediso_readdir(struct mbediso_dir* dir)
{
    if (dir->entry_index >= dir->directory->entry_count)
        return NULL;

    const struct mbediso_dir_entry* entry = &dir->directory->entries[dir->entry_index];

    dir->dirent.d_type = entry->directory ? MBEDISO_DT_DIR : MBEDISO_DT_REG;

    int name_res = mbediso_string_diff_reconstruct(
        dir->dirent.d_name,
        sizeof(dir->dirent.d_name),
        dir->directory->stringtable,
        dir->directory->entries,
        dir->directory->entry_count,
        sizeof(struct mbediso_dir_entry),
        dir->entry_index
    );
    if (name_res != 0)
    {
        // This should be unreachable
        return NULL;
    }

    dir->entry_index++;
    return &dir->dirent;
}
