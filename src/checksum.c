#include "checksum.h"

uint16_t calculate_data_checksum(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t sum = 0;

    for (size_t i = 0; i < length; i++)
    {
        sum += bytes[i];

        if (sum > 0xFFFF)
            sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}
