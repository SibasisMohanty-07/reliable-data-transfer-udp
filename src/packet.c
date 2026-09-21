#include "packet.h"
#include <string.h>

void initialize_packet(Packet *packet)
{
    memset(packet, 0, sizeof(Packet));
}

uint16_t calculate_checksum(const Packet *packet)
{
    /* Checksum implementation will be added next */
    return 0;
}

int verify_checksum(const Packet *packet)
{
    /* Verification implementation will be added next */
    return 1;
}