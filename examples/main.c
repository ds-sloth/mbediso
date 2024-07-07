#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mbediso.h"

#include "internal/io.h"
#include "internal/fs.h"
#include "internal/read.h"
#include "public/file.h"

int scan_dir(struct mbediso_fs* fs, struct mbediso_io* io, uint32_t sector, uint32_t length);
int find_joliet_root(struct mbediso_fs* fs, struct mbediso_io* io);

int main(int argc, char** argv)
{
    const char* fn = (argc > 1) ? argv[1] : "thextech-super-talking-time-bros-1n2-v1-5.iso";
    if(!fn)
        return 1;

    struct mbediso_fs fs;
    mbediso_fs_ctor(&fs);
    mbediso_fs_init_from_path(&fs, fn);

    struct mbediso_io* io = mbediso_fs_reserve_io(&fs);
    if(!io)
    {
        printf("Failed to open file\n");
        mbediso_fs_dtor(&fs);
        return 1;
    }

    uint32_t offset, length;
    char req_fn[1024];

    req_fn[0] = '\0';

    if(mbediso_read_find_joliet_root(&fs, io) != 0 || mbediso_fs_full_scan(&fs, io) != 0)
    {
        printf("Failed to load structure\n");
        mbediso_fs_release_io(&fs, io);
        mbediso_fs_dtor(&fs);
        return 1;
    }

    mbediso_fs_release_io(&fs, io);

    while(scanf(" %1023[^\n]", req_fn) == 1)
    {
        const struct mbediso_dir_entry* found = mbediso_fs_lookup(&fs, req_fn, strlen(req_fn));

        if(!found)
            printf("  NOT FOUND\n");
        else if(!found->directory)
            printf("File at offset %x, length %x\n", found->sector * 2048, found->length);
        else if(found->length != 0 || found->sector >= fs.directory_count)
            printf("  NOT FULLY LOADED\n");
        else
        {
            const struct mbediso_directory* dir = &fs.directories[found->sector];

            printf("Directory listing:\n");
            for(size_t e = 0; e < dir->entry_count; e++)
            {
                const struct mbediso_dir_entry* real_entry = &dir->entries[e];

                uint8_t filename_buffer[1024];
                if(mbediso_string_diff_reconstruct(filename_buffer, 1024, dir->stringtable, dir->entries, dir->entry_count, sizeof(struct mbediso_dir_entry), e))
                    filename_buffer[0] = '\0';
                printf("  %d offset %x, length %x, filename [%s]\n", (int)real_entry->directory, real_entry->sector * 2048, real_entry->length, filename_buffer);
            }
        }

        req_fn[0] = '\0';
    }

    mbediso_fs_dtor(&fs);

    return 0;
}
