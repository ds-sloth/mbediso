#pragma once

#include <stddef.h>
#include <stdint.h>

extern size_t mbediso_util_first_pow2(size_t capacity);

extern int mbediso_util_utf16be_to_utf8(uint8_t* restrict dest, ptrdiff_t capacity, const uint8_t* restrict src, size_t bytes);
