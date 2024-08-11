#pragma once

#include <stdio.h>
#include <stdint.h>

struct mbediso_lz4_header;

struct mbediso_io
{
    uint8_t tag;
};

struct mbediso_io* mbediso_io_from_file(FILE* file, struct mbediso_lz4_header* header);
void mbediso_io_close(struct mbediso_io* io);

const uint8_t* mbediso_io_read_sector(struct mbediso_io* io, uint32_t sector);
size_t mbediso_io_read_direct(struct mbediso_io* io, uint8_t* dest, uint64_t offset, size_t bytes);
