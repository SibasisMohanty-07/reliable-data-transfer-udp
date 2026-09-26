#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

uint16_t calculate_data_checksum(const void *data, size_t length);

#endif
