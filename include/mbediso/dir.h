#pragma once

#include <stdint.h>

#define MBEDISO_DT_UKNOWN 1
#define MBEDISO_DT_REG 2
#define MBEDISO_DT_DIR 3

struct mbediso_fs;
struct mbediso_directory;

struct mbediso_dirent
{
    uint8_t d_name[334];
    int d_type;
};

struct mbediso_dir
{
    struct mbediso_fs* fs;
    const struct mbediso_directory* directory;
    struct mbediso_dirent dirent;
    uint32_t entry_index;
};

struct mbediso_dir* mbediso_opendir(struct mbediso_fs* fs, const char* name);

int mbediso_closedir(struct mbediso_dir* dir);

const struct mbediso_dirent* mbediso_readdir(struct mbediso_dir* dir);
