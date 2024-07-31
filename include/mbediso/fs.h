#pragma once

#include <stdbool.h>

struct mbediso_fs;

struct mbediso_fs* mbediso_openfs_file(const char* name, bool full_scan);
void mbediso_closefs(struct mbediso_fs* fs);
