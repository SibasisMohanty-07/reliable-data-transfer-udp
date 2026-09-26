#include <stdio.h>
#include "../include/integrity.h"

int main()
{
    uint64_t hash1;
    uint64_t hash2;

    hash1 = calculate_file_hash("test.txt");
    hash2 = calculate_file_hash("received.txt");

    printf("Original file hash : %llu\n",
           (unsigned long long)hash1);

    printf("Received file hash : %llu\n",
           (unsigned long long)hash2);

    if (hash1 == hash2)
    {
        printf("INTEGRITY VERIFIED\n");
    }
    else
    {
        printf("INTEGRITY FAILED\n");
    }

    return 0;
}