#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define MBEDISO_NULL_REF (uint32_t)(0xFFFFFFFFU)

/* string for a string diff */
struct string_diff
{
    // info for the filename
    unsigned last_effective_entry : 19;
    bool     clip_end : 1;
    unsigned subst_table_offset : 24;
    unsigned subst_begin : 10;
    unsigned subst_end : 10;
};

extern int mbediso_string_diff_reconstruct(uint8_t* buffer, size_t buffer_size, const uint8_t* stringtable, const void* entries, size_t entry_count, size_t entry_size, size_t top_entry);
extern int mbediso_string_diff_compact(uint8_t** stringtable, uint32_t* stringtable_size, void* entries, size_t entry_count, size_t entry_size);

/* struct for a single filename */
struct mbediso_filename
{
    uint8_t buffer[334];
};

/* struct for a raw directory entry */
struct mbediso_raw_entry
{
    struct mbediso_filename filename;
    uint32_t sector;
    uint32_t length;
    bool directory;
};

/* struct for a directory entry suitable for long-term storage */
struct mbediso_dir_entry
{
    struct string_diff filename;
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

struct mbediso_pathtable
{

};

// struct mbediso_pathcache_entry
// {

// };

struct mbediso_io
{
    uint8_t tag;
};

struct mbediso_io_file
{
    uint8_t tag;

    FILE* file;

    uint64_t filepos;

    uint8_t* buffer[2];
};

extern struct mbediso_io* mbediso_io_from_file(FILE* file);
extern void mbediso_io_close(struct mbediso_io* io);

extern const uint8_t* mbediso_io_read_sector(struct mbediso_io* io, uint32_t sector, bool use_secondary_buffer);

extern bool mbediso_directory_ctor(struct mbediso_directory* dir);
extern void mbediso_directory_dtor(struct mbediso_directory* dir);

extern int mbediso_directory_push(struct mbediso_directory* dir, const struct mbediso_raw_entry* entry);
extern const struct mbediso_dir_entry* mbediso_directory_lookup(const struct mbediso_directory* dir, const char* filename, uint32_t filename_length);

/* finish the load process for a directory */
extern int mbediso_directory_finish(struct mbediso_directory* dir);

extern size_t mbediso_util_first_pow2(size_t capacity);


struct mbediso_fs
{
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
    struct mbediso_pathcache_entry** pathcache_list;
    uint32_t pathcache_list_size;
    uint32_t pathcache_list_capacity;
};


extern bool mbediso_fs_ctor(struct mbediso_fs* fs);
extern void mbediso_fs_dtor(struct mbediso_fs* fs);

extern uint32_t mbediso_fs_alloc_directory(struct mbediso_fs* fs);
extern void mbediso_fs_free_directory(struct mbediso_fs* fs, uint32_t dir_index);

extern const struct mbediso_dir_entry* mbediso_fs_lookup(const struct mbediso_fs* fs, const char* path, uint32_t path_length);

extern struct mbediso_io* mbediso_fs_reserve_io(struct mbediso_fs* fs);
extern void mbediso_fs_free_io(struct mbediso_fs* fs, struct mbediso_io* io);
