#ifndef PACKET_H
#define PACKET_H

#include "../include/protocol.h"

void initialize_packet(Packet *packet);

uint16_t calculate_checksum(const Packet *packet);

int verify_checksum(const Packet *packet);

#endif