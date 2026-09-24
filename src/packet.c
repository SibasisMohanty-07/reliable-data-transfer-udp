#include "packet.h"
#include <string.h>
#include <winsock2.h>

void initialize_packet(Packet *packet)
{
    if (packet == NULL)
    {
        return;
    }

    memset(packet, 0, sizeof(Packet));
}

int create_data_packet(Packet *packet,
                       uint32_t sequence_number,
                       const char *payload,
                       uint16_t payload_length)
{
    if (packet == NULL || payload == NULL)
    {
        return -1;
    }

    /* Payload too large */
    if (payload_length > MAX_PAYLOAD_SIZE)
    {
        return -1;
    }

    initialize_packet(packet);

    packet->type = PACKET_DATA;
    packet->flags = FLAG_NONE;
    packet->sequence_number = sequence_number;
    packet->acknowledgement_number = 0;
    packet->payload_length = payload_length;

    memcpy(packet->payload,
           payload,
           payload_length);

    packet->checksum = calculate_checksum(packet);

    return 0;
}

int create_fin_packet(Packet *packet,
                      uint32_t sequence_number)
{
    if (packet == NULL)
    {
        return -1;
    }

    initialize_packet(packet);

    packet->type = PACKET_FIN;
    packet->flags = FLAG_FIN;
    packet->sequence_number = sequence_number;
    packet->acknowledgement_number = 0;
    packet->payload_length = 0;

    packet->checksum = calculate_checksum(packet);

    return 0;
}

int serialize_packet(const Packet *packet,
                     uint8_t *buffer,
                     size_t buffer_size)
{
    uint32_t temp32;
    uint16_t temp16;
    size_t total_size;

    if (packet == NULL || buffer == NULL)
    {
        return -1;
    }

    /* Payload too large */
    if (packet->payload_length > MAX_PAYLOAD_SIZE)
    {
        return -1;
    }

    total_size = 14 + packet->payload_length;

    if (buffer_size < total_size)
    {
        return -1;
    }

    buffer[0] = packet->type;
    buffer[1] = packet->flags;

    temp32 = htonl(packet->sequence_number);
    memcpy(buffer + 2, &temp32, sizeof(uint32_t));

    temp32 = htonl(packet->acknowledgement_number);
    memcpy(buffer + 6, &temp32, sizeof(uint32_t));

    temp16 = htons(packet->payload_length);
    memcpy(buffer + 10, &temp16, sizeof(uint16_t));

    temp16 = htons(packet->checksum);
    memcpy(buffer + 12, &temp16, sizeof(uint16_t));

    if (packet->payload_length > 0)
    {
        memcpy(buffer + 14,
               packet->payload,
               packet->payload_length);
    }

    return (int)total_size;
}

int parse_packet(const uint8_t *buffer,
                 size_t buffer_length,
                 Packet *packet)
{
    uint16_t payload_length;
    uint32_t temp32;
    uint16_t temp16;

    if (buffer == NULL || packet == NULL)
    {
        return -1;
    }

    /* Invalid packet: header is incomplete */
    if (buffer_length < 14)
    {
        return -1;
    }

    initialize_packet(packet);

    packet->type = buffer[0];
    packet->flags = buffer[1];

    memcpy(&temp32, buffer + 2, sizeof(uint32_t));
    packet->sequence_number = ntohl(temp32);

    memcpy(&temp32, buffer + 6, sizeof(uint32_t));
    packet->acknowledgement_number = ntohl(temp32);

    memcpy(&temp16, buffer + 10, sizeof(uint16_t));
    payload_length = ntohs(temp16);

    memcpy(&temp16, buffer + 12, sizeof(uint16_t));
    packet->checksum = ntohs(temp16);

    /* Payload too large */
    if (payload_length > MAX_PAYLOAD_SIZE)
    {
        return -1;
    }

    /*
     * Invalid payload length.
     *
     * The received UDP datagram must contain exactly:
     * 14-byte header + declared payload length.
     */
    if (buffer_length != (size_t)(14 + payload_length))
    {
        return -1;
    }

    packet->payload_length = payload_length;

    if (payload_length > 0)
    {
        memcpy(packet->payload,
               buffer + 14,
               payload_length);
    }

    return 0;
}

uint16_t calculate_checksum(const Packet *packet)
{
    uint32_t sum = 0;
    uint16_t i;

    if (packet == NULL)
    {
        return 0;
    }

    sum += packet->type;
    sum += packet->flags;

    sum += (packet->sequence_number & 0xFFFF);
    sum += (packet->sequence_number >> 16);

    sum += (packet->acknowledgement_number & 0xFFFF);
    sum += (packet->acknowledgement_number >> 16);

    sum += packet->payload_length;

    for (i = 0; i < packet->payload_length; i++)
    {
        sum += (unsigned char)packet->payload[i];
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

int verify_checksum(const Packet *packet)
{
    uint16_t calculated_checksum;

    if (packet == NULL)
    {
        return 0;
    }

    calculated_checksum = calculate_checksum(packet);

    if (calculated_checksum == packet->checksum)
    {
        return 1;
    }

    return 0;
}