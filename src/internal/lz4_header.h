#pragma once

#include <stdio.h>
#include <stdint.h>

struct mbediso_lz4_header
{
    uint32_t block_size;
    uint32_t block_count;
    uint32_t* block_offsets;
};

struct mbediso_lz4_header* mbediso_lz4_header_load(FILE* file);
void mbediso_lz4_header_free(struct mbediso_lz4_header* header);
