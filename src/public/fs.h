#pragma once

struct mbediso_fs;

struct mbediso_fs* mbediso_openfs_file(const char* name);
void mbediso_closefs(struct mbediso_fs* fs);
