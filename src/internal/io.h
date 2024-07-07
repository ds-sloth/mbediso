#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

struct mbediso_io
{
    uint8_t tag;
};

struct mbediso_io_file
{
    uint8_t tag;

    FILE* file;

    uint64_t filepos;

    // must be 2048
    uint8_t* buffer;
};

struct mbediso_io* mbediso_io_from_file(FILE* file);
void mbediso_io_close(struct mbediso_io* io);

const uint8_t* mbediso_io_read_sector(struct mbediso_io* io, uint32_t sector);
