#pragma once

#include <stdint.h>

#define MBEDISO_SEEK_SET 0       /**< Seek from the beginning of data */
#define MBEDISO_SEEK_CUR 1       /**< Seek relative to current read point */
#define MBEDISO_SEEK_END 2       /**< Seek relative to the end of data */

struct mbediso_io;
struct mbediso_fs;

struct mbediso_file
{
    struct mbediso_io* io;
    struct mbediso_fs* fs;
    uint32_t start;
    uint32_t end;
    uint32_t offset;
};

struct mbediso_file* mbediso_fopen(struct mbediso_fs* fs, const char* pathname);

size_t mbediso_fread(struct mbediso_file* file, void* ptr, size_t size, size_t maxnum);

int64_t mbediso_fseek(struct mbediso_file* file, int64_t offset, int whence);

int64_t mbediso_fsize(struct mbediso_file* file);

void mbediso_fclose(struct mbediso_file* file);
