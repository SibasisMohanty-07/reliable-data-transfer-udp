#ifndef GBN_H
#define GBN_H

#include <stdint.h>
#include <stdbool.h>

#define GBN_WINDOW_SIZE 4

typedef struct
{
    uint32_t base;
    uint32_t next_seq;
    uint32_t window_size;
    bool timer_running;
    uint64_t retransmissions;
} GBNState;

#endif