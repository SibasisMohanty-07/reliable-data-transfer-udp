#ifndef SR_H
#define SR_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

#define SR_WINDOW_SIZE 4
#define SR_TIMEOUT_MS 1000
#define SR_MAX_RETRIES 5

typedef struct
{
    Packet packet;
    bool valid;
    bool acknowledged;
    uint64_t last_sent_ms;
    uint32_t retries;
} SRSlot;

typedef struct
{
    uint32_t base;
    uint32_t next_seq;
    uint32_t window_size;
} SRState;

#endif
