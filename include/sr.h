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
/* Week 4: Selective Repeat ARQ */

/* Receiver buffering for out-of-order packets */
#define SR_RECEIVER_BUFFER_SIZE SR_WINDOW_SIZE

/* Individual acknowledgement support */
#define SR_INDIVIDUAL_ACK 1

/* Selective retransmission support */
#define SR_SELECTIVE_RETRANSMISSION 1

#endif
