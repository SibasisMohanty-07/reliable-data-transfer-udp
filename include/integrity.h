#ifndef INTEGRITY_H
#define INTEGRITY_H

#include <stdint.h>
#include <stddef.h>

uint64_t calculate_file_hash(const char *filename);

#endif