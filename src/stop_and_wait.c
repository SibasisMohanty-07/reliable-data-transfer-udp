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

    if (packet_size <= 0) {
        printf("Packet serialization failed\n");
        return -1;
    }

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {

        int sent = sendto(
            sockfd,
            (const char *)buffer,
            packet_size,
            0,
            (struct sockaddr *)receiver_addr,
            sizeof(*receiver_addr)
        );

        if (sent < 0) {
            printf("Failed to send DATA packet\n");
            return -1;
        }

        printf(
            "Sent DATA packet %u (attempt %d)\n",
            packet->sequence_number,
            attempt
        );

        /*
         * Keep listening after stale or duplicate ACKs.
         * Only retransmit when the ACK timeout expires.
         */
        time_t deadline = time(NULL) + TIMEOUT_SEC;

        while (1) {
            time_t now = time(NULL);
            time_t remaining = deadline - now;

            if (remaining <= 0) {
                printf(
                    "Timeout for packet %u\n",
                    packet->sequence_number
                );
                break;
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);

            struct timeval timeout;
            timeout.tv_sec = (long)remaining;
            timeout.tv_usec = 0;

            int result = select(
                (int)sockfd + 1,
                &readfds,
                NULL,
                NULL,
                &timeout
            );

            if (result == 0) {
                printf(
                    "Timeout for packet %u\n",
                    packet->sequence_number
                );
                break;
            }

            if (result < 0) {
                printf("select() failed\n");
                return -1;
            }

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

            if (received < 0) {
                printf("Failed to receive ACK; continuing\n");
                continue;
            }

            Packet ack;

            if (parse_packet(
                    ack_buffer,
                    (size_t)received,
                    &ack) != 0) {
                printf("Invalid ACK packet; ignoring\n");
                continue;
            }

            if (!verify_checksum(&ack)) {
                printf("Invalid ACK checksum; ignoring\n");
                continue;
            }

            if (ack.type != PACKET_ACK) {
                printf("Non-ACK packet received; ignoring\n");
                continue;
            }

            if (ack.acknowledgement_number ==
                packet->sequence_number) {

                printf(
                    "Received ACK for DATA packet %u\n",
                    packet->sequence_number
                );

                return 0;
            }

            /*
             * A duplicate or stale ACK must not consume a retry.
             * Continue waiting for the correct ACK until the
             * original timeout expires.
             */
            printf(
                "Ignoring stale ACK %u while waiting for ACK %u\n",
                ack.acknowledgement_number,
                packet->sequence_number
            );
        }

        printf("Retransmitting DATA packet %u\n",
               packet->sequence_number);
    }

    printf(
        "Maximum retries reached for packet %u\n",
        packet->sequence_number
    );

    return -1;
}
