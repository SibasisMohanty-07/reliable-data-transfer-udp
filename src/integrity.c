#include "../include/integrity.h"
#include <stdio.h>

uint64_t calculate_file_hash(const char *filename)
{
    FILE *file;
    unsigned char buffer[1024];
    size_t bytes_read;

    uint64_t hash = 14695981039346656037ULL;

    file = fopen(filename, "rb");

    if (file == NULL)
    {
        return 0;
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        for (size_t i = 0; i < bytes_read; i++)
        {
            hash ^= buffer[i];
            hash *= 1099511628211ULL;
        }
    }

    fclose(file);

    return hash;
}