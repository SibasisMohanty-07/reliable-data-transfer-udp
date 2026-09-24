#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_PAYLOAD_SIZE 1024

/* Packet types */
#define PACKET_DATA 1
#define PACKET_ACK  2
#define PACKET_FIN  3

/* Flags */
#define FLAG_NONE 0
#define FLAG_SYN  1
#define FLAG_FIN  2

/* Common packet format */
typedef struct {
    uint8_t  type;
    uint8_t  flags;
    uint32_t sequence_number;
    uint32_t acknowledgement_number;
    uint16_t payload_length;
    uint16_t checksum;
    char payload[MAX_PAYLOAD_SIZE];
} Packet;

#endif