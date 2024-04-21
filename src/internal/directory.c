#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "j9660.h"

bool j9660_directory_ctor(struct j9660_directory* dir)
{
    if(!dir)
        return false;

    dir->stringtable = malloc(32);
    if(!dir->stringtable)
        return false;

    dir->stringtable_size = 0;
    dir->stringtable_capacity = 32;

    dir->entries = NULL;
    dir->entry_count = 0;
    dir->entry_capacity = 0;

    dir->utf8_sorted = true;

    return true;
}

void j9660_directory_dtor(struct j9660_directory* dir)
{
    if(!dir)
        return;

    free(dir->stringtable);
    free(dir->entries);
}

int j9660_directory_push(struct j9660_directory* dir, const struct j9660_raw_entry* raw_entry)
{
    if(!dir || !raw_entry)
        return -1;

    size_t fn_len = strlen((const char*)raw_entry->filename.buffer);
    if(fn_len > 333)
        return -1;

    // make sure there is capacity for entry
    if(dir->entry_count + 1 > dir->entry_capacity)
    {
        size_t new_capacity = j9660_util_first_pow2(dir->entry_capacity + 1);
        struct j9660_dir_entry* new_entries = realloc(dir->entries, new_capacity * sizeof(struct j9660_dir_entry));
        if(new_entries)
        {
            dir->entries = new_entries;
            dir->entry_capacity = new_capacity;
        }
    }

    if(dir->entry_count + 1 > dir->entry_capacity)
        return -1;

    // make sure there is capacity for filename
    if(dir->stringtable_size + fn_len > dir->stringtable_capacity)
    {
        size_t new_capacity = j9660_util_first_pow2(dir->stringtable_capacity + fn_len);
        uint8_t* new_stringtable = realloc(dir->stringtable, new_capacity);
        if(new_stringtable)
        {
            dir->stringtable = new_stringtable;
            dir->stringtable_capacity = new_capacity;
        }
    }

    if(dir->stringtable_size + fn_len > dir->stringtable_capacity)
        return -1;

    // function must succeed at this point, begin allocating and copying data

    struct j9660_dir_entry* entry = &dir->entries[dir->entry_count];
    dir->entry_count++;

    // copy information from raw_entry
    entry->sector = raw_entry->sector;
    entry->length = raw_entry->length;
    entry->directory = raw_entry->directory;

    // copy filename information
    entry->filename.last_effective_entry = -1;
    entry->filename.subst_table_offset = dir->stringtable_size;
    entry->filename.subst_begin = 0;
    entry->filename.subst_end = fn_len;
    entry->filename.clip_end = true;

    // copy filename buffer
    memcpy(dir->stringtable + dir->stringtable_size, raw_entry->filename.buffer, fn_len);
    dir->stringtable_size += fn_len;

    return 0;
}

static const struct j9660_directory* s_sort_dir = NULL;

static int s_directory_entry_cmp_PRECOMPACT(const void* m1, const void* m2)
{
    const struct j9660_dir_entry* e1 = (const struct j9660_dir_entry*)m1;
    const struct j9660_dir_entry* e2 = (const struct j9660_dir_entry*)m2;

    int len_cmp = 0;
    if(e1->filename.subst_end < e2->filename.subst_end)
        len_cmp = -1;
    else if(e1->filename.subst_end > e2->filename.subst_end)
        len_cmp = 1;

    size_t min_len = (len_cmp <= 0) ? e1->filename.subst_end : e2->filename.subst_end;

    int ret = strncmp((const char*)&s_sort_dir->stringtable[e1->filename.subst_table_offset], (const char*)&s_sort_dir->stringtable[e2->filename.subst_table_offset], min_len);

    if(ret == 0)
        ret = len_cmp;

    // printf("Compared [%s] [%s], %d\n", (const char*)&s_sort_dir->stringtable[e1->filename.subst_table_offset], (const char*)&s_sort_dir->stringtable[e1->filename.subst_table_offset], ret);

    return ret;
}

static void s_directory_sort_PRECOMPACT(struct j9660_directory* dir)
{
    if(dir->entry_count > 2)
    {
        // WARNING: need to deadlock file open with multiple threads in cache mode
        s_sort_dir = dir;
        qsort(dir->entries + 2, dir->entry_count - 2, sizeof(struct j9660_dir_entry), s_directory_entry_cmp_PRECOMPACT);
        s_sort_dir = NULL;
    }

    dir->utf8_sorted = true;
}

const struct j9660_dir_entry* j9660_directory_lookup(const struct j9660_directory* dir, const char* filename, uint32_t filename_length)
{
    struct j9660_filename temp_filename;

    if(filename_length > sizeof(temp_filename.buffer))
        return NULL;

    // perform binary search on directory's entries
    uint32_t begin = 2;
    uint32_t end = dir->entry_count;

    while(begin != end)
    {
        uint32_t mid = begin + (end - begin) / 2;

        if(j9660_string_diff_reconstruct(temp_filename.buffer, sizeof(temp_filename.buffer), dir->stringtable, dir->entries, dir->entry_count, sizeof(struct j9660_dir_entry), mid))
            return NULL;

        int cmp_ret = strncmp((const char*)temp_filename.buffer, (const char*)filename, filename_length);

        if(cmp_ret == 0 && (filename_length == sizeof(temp_filename.buffer) || temp_filename.buffer[filename_length] == '\0'))
            return &dir->entries[mid];
        else if(cmp_ret < 0)
            begin = mid + 1;
        else
            end = mid;
    }

    return NULL;
}


int j9660_directory_finish(struct j9660_directory* dir)
{
    if(!dir->utf8_sorted)
        s_directory_sort_PRECOMPACT(dir);

    int ret = j9660_string_diff_compact(&dir->stringtable, &dir->stringtable_size, dir->entries, dir->entry_count, sizeof(struct j9660_dir_entry));
    if(ret)
        return ret;

    // shrink stringtable to fit
    uint8_t* shrunken_stringtable = realloc(dir->stringtable, dir->stringtable_size);
    if(shrunken_stringtable)
    {
        dir->stringtable = shrunken_stringtable;
        dir->stringtable_capacity = dir->stringtable_size;
    }

    // shrink entry list to fit
    struct j9660_dir_entry* new_entries = realloc(dir->entries, dir->entry_count * sizeof(struct j9660_dir_entry));
    if(new_entries)
    {
        dir->entries = new_entries;
        dir->entry_capacity = dir->entry_count;
    }

    return 0;
}
