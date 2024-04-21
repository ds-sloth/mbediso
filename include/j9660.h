#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define J9660_NULL_REF (uint32_t)(0xFFFFFFFFU)

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

extern int j9660_string_diff_reconstruct(uint8_t* buffer, size_t buffer_size, const uint8_t* stringtable, const void* entries, size_t entry_count, size_t entry_size, size_t top_entry);
extern int j9660_string_diff_compact(uint8_t** stringtable, uint32_t* stringtable_size, void* entries, size_t entry_count, size_t entry_size);

/* struct for a single filename */
struct j9660_filename
{
    uint8_t buffer[334];
};

/* struct for a raw directory entry */
struct j9660_raw_entry
{
    struct j9660_filename filename;
    uint32_t sector;
    uint32_t length;
    bool directory;
};

/* struct for a directory entry suitable for long-term storage */
struct j9660_dir_entry
{
    struct string_diff filename;
    uint32_t sector;
    uint32_t length;
    bool directory;
};

/* represents the contents of a single directory */
struct j9660_directory
{
    /* if null, the directory strings have been permanently consolidated to the global stringtable */
    uint8_t* stringtable;
    uint32_t stringtable_size;
    uint32_t stringtable_capacity;

    struct j9660_dir_entry* entries;
    uint32_t entry_count;
    uint32_t entry_capacity;

    /* tracks whether the directory is utf8-sorted */
    bool utf8_sorted;
};

struct j9660_pathtable
{

};

// struct j9660_pathcache_entry
// {

// };

struct j9660_io
{
    uint8_t tag;
};

struct j9660_io_file
{
    uint8_t tag;

    FILE* file;

    uint64_t filepos;

    uint8_t* buffer[2];
};

extern struct j9660_io* j9660_io_from_file(FILE* file);
extern void j9660_io_close(struct j9660_io* io);

extern const uint8_t* j9660_io_read_sector(struct j9660_io* io, uint32_t sector, bool use_secondary_buffer);

extern bool j9660_directory_ctor(struct j9660_directory* dir);
extern void j9660_directory_dtor(struct j9660_directory* dir);

extern int j9660_directory_push(struct j9660_directory* dir, const struct j9660_raw_entry* entry);
extern const struct j9660_dir_entry* j9660_directory_lookup(const struct j9660_directory* dir, const char* filename, uint32_t filename_length);

/* finish the load process for a directory */
extern int j9660_directory_finish(struct j9660_directory* dir);

extern size_t j9660_util_first_pow2(size_t capacity);


struct j9660_fs
{
    /* total memory usage and budget of the filesystem */
    uint32_t mem_usage;
    uint32_t mem_capacity;

    /* nulled if directories own their own stringtables */
    uint8_t* stringtable;
    uint32_t stringtable_size;
    uint32_t stringtable_capacity;

    /* stores either all directories, or the currently loaded directories (owned by the pathcache) */
    struct j9660_directory* directories;
    uint32_t directory_count;
    uint32_t directory_capacity;

    struct j9660_dir_entry root_dir_entry;

    /* fixme: add a directory free list */

    /* stores references to directories by path; may own these references */
    struct j9660_pathcache_entry** pathcache_list;
    uint32_t pathcache_list_size;
    uint32_t pathcache_list_capacity;
};


extern bool j9660_fs_ctor(struct j9660_fs* fs);
extern void j9660_fs_dtor(struct j9660_fs* fs);

extern uint32_t j9660_fs_alloc_directory(struct j9660_fs* fs);
extern void j9660_fs_free_directory(struct j9660_fs* fs, uint32_t dir_index);

extern const struct j9660_dir_entry* j9660_fs_lookup(const struct j9660_fs* fs, const char* path, uint32_t path_length);

extern struct j9660_io* j9660_fs_reserve_io(struct j9660_fs* fs);
extern void j9660_fs_free_io(struct j9660_fs* fs, struct j9660_io* io);
