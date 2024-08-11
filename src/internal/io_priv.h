#pragma once

#include <stdio.h>
#include <stdint.h>

#define MBEDISO_IO_TAG_UNC 1
#define MBEDISO_IO_TAG_LZ4 2

struct mbediso_io_unc
{
    uint8_t tag;

    FILE* file;

    uint64_t filepos;

    // must be 2048
    uint8_t* buffer;
};

struct mbediso_io_lz4
{
    uint8_t tag;

    FILE* file;
    struct mbediso_lz4_header* header;

    uint32_t file_pos;
    uint32_t buffer_logical_pos;
    uint32_t buffer_length;

    // must be larger than 2048
    uint8_t* compressed_buffer;
    uint8_t* decompressed_buffer;
};

