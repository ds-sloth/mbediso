#pragma once

#include <stdint.h>
#include <stdbool.h>


/* string for a string diff */
struct mbediso_string_diff
{
    // info for the filename
    unsigned last_effective_entry : 19;
    bool     clip_end : 1;
    unsigned subst_table_offset : 24;
    unsigned subst_begin : 10;
    unsigned subst_end : 10;
};

int mbediso_string_diff_reconstruct(uint8_t* buffer, size_t buffer_size, const uint8_t* stringtable, const void* entries, size_t entry_count, size_t entry_size, size_t top_entry);
int mbediso_string_diff_compact(uint8_t** stringtable, uint32_t* stringtable_size, void* entries, size_t entry_count, size_t entry_size);
