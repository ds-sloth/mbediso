#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mbediso.h"

int main(int argc, char** argv)
{
    const char* fn = (argc > 1) ? argv[1] : "thextech-super-talking-time-bros-1n2-v1-5.iso";
    if(!fn)
        return 1;

    struct mbediso_fs* fs = mbediso_openfs_file(fn);

    if(!fs)
    {
        printf("Failed to load structure\n");
        return 1;
    }

    char req_fn[1024];

    req_fn[0] = '\0';

    while(scanf(" %1023[^\n]", req_fn) == 1)
    {
        struct mbediso_file* f = mbediso_fopen(fs, req_fn);
        if(f)
        {
            printf("Found file with length %lld\n", (long long)mbediso_fsize(f));
            mbediso_fclose(f);
            goto cont;
        }

        struct mbediso_dir* dir = mbediso_opendir(fs, req_fn);
        if(dir)
        {
            printf("Directory listing:\n");
            for(const struct mbediso_dirent* p = mbediso_readdir(dir); p != NULL; p = mbediso_readdir(dir))
                printf("  %d name [%s]\n", (int)(p->d_type == MBEDISO_DT_DIR), (const char*)p->d_name);

            mbediso_closedir(dir);
            goto cont;
        }

        printf("Could not find.\n");

cont:
        req_fn[0] = '\0';
    }

    mbediso_closefs(fs);
    fs = NULL;

    return 0;
}
