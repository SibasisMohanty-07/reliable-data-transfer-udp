#ifndef PACKET_H
#define PACKET_H

#include "../include/protocol.h"
#include <stdint.h>
#include <stddef.h>

/* Initialize packet */
void initialize_packet(Packet *packet);

/* Create DATA packet */
int create_data_packet(Packet *packet,
                       uint32_t sequence_number,
                       const char *payload,
                       uint16_t payload_length);

/* Create FIN packet */
int create_fin_packet(Packet *packet,
                      uint32_t sequence_number);

/* Serialize packet */
int serialize_packet(const Packet *packet,
                     uint8_t *buffer,
                     size_t buffer_size);

/* Parse packet */
int parse_packet(const uint8_t *buffer,
                 size_t buffer_length,
                 Packet *packet);

/* Checksum */
uint16_t calculate_checksum(const Packet *packet);

/* Verify checksum */
int verify_checksum(const Packet *packet);

#endif