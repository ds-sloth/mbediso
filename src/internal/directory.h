#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "internal/string_diff.h"

struct mbediso_io;

/* struct for a single filename */
struct mbediso_name
{
    uint8_t buffer[334];
};

/* struct for a raw directory entry */
struct mbediso_raw_entry
{
    struct mbediso_name name;
    uint32_t sector;
    uint32_t length;
    bool directory;
};

/* struct for a directory entry suitable for long-term storage */
struct mbediso_dir_entry
{
    struct mbediso_string_diff name_frag;
    uint32_t sector;
    uint32_t length;
    bool directory;
};

/* represents the contents of a single directory */
struct mbediso_directory
{
    /* if null, the directory strings have been permanently consolidated to the global stringtable */
    uint8_t* stringtable;
    uint32_t stringtable_size;
    uint32_t stringtable_capacity;

    struct mbediso_dir_entry* entries;
    uint32_t entry_count;
    uint32_t entry_capacity;

    /* tracks whether the directory is utf8-sorted */
    bool utf8_sorted;
};

bool mbediso_directory_ctor(struct mbediso_directory* dir);
void mbediso_directory_dtor(struct mbediso_directory* dir);

int mbediso_directory_push(struct mbediso_directory* dir, const struct mbediso_raw_entry* entry);
const struct mbediso_dir_entry* mbediso_directory_lookup(const struct mbediso_directory* dir, const char* name, uint32_t name_length);

/* load a directory's entries from the filesystem and prepare the directory for use */
int mbediso_directory_load(struct mbediso_directory* dir, struct mbediso_io* io, uint32_t sector, uint32_t length);
