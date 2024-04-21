#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "j9660.h"

int main(int argc, char** argv)
{
    FILE* f = fopen("thextech-super-talking-time-bros-1n2-v1-5.iso", "rb");
    if(!f)
        return 1;

    struct j9660_fs fs;
    j9660_fs_ctor(&fs);

    struct j9660_io* io = j9660_io_from_file(f);
    if(!io)
    {
        fclose(f);
        return 1;
    }

    uint32_t offset, length;
    char req_fn[1024];

    req_fn[0] = '\0';

    if(find_joliet_root(&fs, io) != 0 || scan_dir(&fs, io, fs.root_dir_entry.sector, fs.root_dir_entry.length) != 0)
    {
        printf("Failed to load structure\n");
        return 1;
    }

    // mark root directory as loaded at index 0
    fs.root_dir_entry.sector = 0;
    fs.root_dir_entry.length = 0;

    while(scanf(" %1023[^\n]", req_fn) == 1)
    {
        const struct j9660_dir_entry* found = j9660_fs_lookup(&fs, req_fn, strlen(req_fn));

        if(!found)
            printf("  NOT FOUND\n");
        else if(!found->directory)
            printf("File at offset %x, length %x\n", found->sector * 2048, found->length);
        else if(found->length != 0 || found->sector >= fs.directory_count)
            printf("  NOT FULLY LOADED\n");
        else
        {
            const struct j9660_directory* dir = &fs.directories[found->sector];

            printf("Directory listing:\n");
            for(size_t e = 0; e < dir->entry_count; e++)
            {
                const struct j9660_dir_entry* real_entry = &dir->entries[e];

                uint8_t filename_buffer[1024];
                if(j9660_string_diff_reconstruct(filename_buffer, 1024, dir->stringtable, dir->entries, dir->entry_count, sizeof(struct j9660_dir_entry), e))
                    filename_buffer[0] = '\0';
                printf("  %d offset %x, length %x, filename [%s]\n", (int)real_entry->directory, real_entry->sector * 2048, real_entry->length, filename_buffer);
            }
        }

        req_fn[0] = '\0';
    }

    j9660_io_close(io);

    j9660_fs_dtor(&fs);

    return 0;
}
