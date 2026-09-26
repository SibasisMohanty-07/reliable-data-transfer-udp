#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#include "packet.h"
#include "../include/protocol.h"

#define TIMEOUT_SEC 2
#define MAX_RETRIES 5

/*
 * Send one DATA packet and wait for its ACK.
 * If ACK is not received within the timeout,
 * retransmit the packet.
 */
int stop_and_wait_send(
    SOCKET sockfd,
    struct sockaddr_in *receiver_addr,
    Packet *packet)
{
    uint8_t buffer[14 + MAX_PAYLOAD_SIZE];

    int packet_size = serialize_packet(
        packet,
        buffer,
        sizeof(buffer)
    );

    if (packet_size < 0)
    {
        printf("Packet serialization failed\n");
        return -1;
    }

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++)
    {
        /* Send DATA packet */
        int sent = sendto(
            sockfd,
            (const char *)buffer,
            packet_size,
            0,
            (struct sockaddr *)receiver_addr,
            sizeof(*receiver_addr)
        );

        if (sent < 0)
        {
            printf("Failed to send DATA packet\n");
            return -1;
        }

        printf(
            "Sent DATA packet %u (attempt %d)\n",
            packet->sequence_number,
            attempt
        );

        /* Wait for ACK */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = TIMEOUT_SEC;
        timeout.tv_usec = 0;

        int result = select(
            (int)sockfd + 1,
            &readfds,
            NULL,
            NULL,
            &timeout
        );

        /* Timeout */
        if (result == 0)
        {
            printf(
                "Timeout for packet %u\n",
                packet->sequence_number
            );

            printf("Retransmitting...\n");
            continue;
        }

        /* Error */
        if (result < 0)
        {
            printf("select() failed\n");
            return -1;
        }

        /* ACK received */
        if (FD_ISSET(sockfd, &readfds))
        {
            uint8_t ack_buffer[14 + MAX_PAYLOAD_SIZE];

            struct sockaddr_in ack_addr;

#ifdef _WIN32
            int ack_len = sizeof(ack_addr);
#else
            socklen_t ack_len = sizeof(ack_addr);
#endif

            int received = recvfrom(
                sockfd,
                (char *)ack_buffer,
                sizeof(ack_buffer),
                0,
                (struct sockaddr *)&ack_addr,
                &ack_len
            );

            if (received < 0)
            {
                printf("Failed to receive ACK\n");
                continue;
            }

            Packet ack;

            if (parse_packet(
                    ack_buffer,
                    received,
                    &ack) != 0)
            {
                printf("Invalid ACK packet\n");
                continue;
            }

            if (ack.type == PACKET_ACK &&
                ack.sequence_number ==
                    packet->sequence_number)
            {
                printf(
                    "Received ACK for DATA packet %u\n",
                    packet->sequence_number
                );

                return 0;
            }

            printf("Wrong ACK received\n");
        }
    }

    printf(
        "Maximum retries reached for packet %u\n",
        packet->sequence_number
    );

    return -1;
}