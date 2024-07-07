#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "internal/directory.h"

struct mbediso_fs
{
    /* Will soon be more flexible: path to give fopen when creating a new io. If non-null, owned by the mbediso_fs object. */
    char* archive_path;

    /* total memory usage and budget of the filesystem */
    uint32_t mem_usage;
    uint32_t mem_capacity;

    /* nulled if directories own their own stringtables */
    uint8_t* stringtable;
    uint32_t stringtable_size;
    uint32_t stringtable_capacity;

    /* stores either all directories, or the currently loaded directories (owned by the pathcache) */
    struct mbediso_directory* directories;
    uint32_t directory_count;
    uint32_t directory_capacity;

    struct mbediso_dir_entry root_dir_entry;

    /* fixme: add a directory free list */

    /* stores references to directories by path; may own these references */
    // struct mbediso_pathcache_entry** pathcache_list;
    // uint32_t pathcache_list_size;
    // uint32_t pathcache_list_capacity;
};


bool mbediso_fs_ctor(struct mbediso_fs* fs);
void mbediso_fs_dtor(struct mbediso_fs* fs);

uint32_t mbediso_fs_alloc_directory(struct mbediso_fs* fs);
void mbediso_fs_free_directory(struct mbediso_fs* fs, uint32_t dir_index);

const struct mbediso_dir_entry* mbediso_fs_lookup(const struct mbediso_fs* fs, const char* path, uint32_t path_length);

struct mbediso_io* mbediso_fs_reserve_io(struct mbediso_fs* fs);
void mbediso_fs_release_io(struct mbediso_fs* fs, struct mbediso_io* io);

int mbediso_fs_full_scan(struct mbediso_fs* fs, struct mbediso_io* io);
