#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#endif

#include "../include/protocol.h"
#include "packet.h"

#define SERVER_PORT 9000
#define LOST_ACK_1 2
#define LOST_ACK_2 3
#define FIN_LINGER_MS 7000

static int send_ack(
    int sockfd,
    const struct sockaddr_in *sender_addr,
#ifdef _WIN32
    int sender_len,
#else
    socklen_t sender_len,
#endif
    uint32_t acknowledgement_number)
{
    Packet ack_packet;
    initialize_packet(&ack_packet);

    ack_packet.type = PACKET_ACK;
    ack_packet.flags = FLAG_NONE;
    ack_packet.sequence_number = 0;
    ack_packet.acknowledgement_number = acknowledgement_number;
    ack_packet.payload_length = 0;
    ack_packet.checksum = calculate_checksum(&ack_packet);

    uint8_t ack_buffer[14 + MAX_PAYLOAD_SIZE];

    int ack_size = serialize_packet(
        &ack_packet, ack_buffer, sizeof(ack_buffer));

    if (ack_size <= 0)
    {
        printf("Failed to serialize ACK\n");
        return -1;
    }

    int sent = sendto(
        sockfd,
        (const char *)ack_buffer,
        ack_size,
        0,
        (const struct sockaddr *)sender_addr,
        sender_len);

#ifdef _WIN32
    if (sent == SOCKET_ERROR)
#else
    if (sent < 0)
#endif
    {
        printf("Failed to send ACK\n");
        return -1;
    }

    return 0;
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif

#ifdef _WIN32
    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
#else
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
#endif

#ifdef _WIN32
    if (sockfd == INVALID_SOCKET)
#else
    if (sockfd < 0)
#endif
    {
        printf("Socket creation failed\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    receiver_addr.sin_port = htons(SERVER_PORT);

    if (bind(sockfd, (struct sockaddr *)&receiver_addr,
             sizeof(receiver_addr)) < 0)
    {
        printf("bind failed\n");
#ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
        return 1;
    }

    printf("Receiver started on port %d...\n", SERVER_PORT);

    FILE *file = fopen("received.txt", "wb");
    if (file == NULL)
    {
        perror("Error opening output file");
#ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
        return 1;
    }

    uint8_t receive_buffer[14 + MAX_PAYLOAD_SIZE];
    uint32_t expected_sequence = 0;
    int transfer_complete = 0;
    int fin_linger = 0;

    while (1)
    {
        struct sockaddr_in sender_addr;

#ifdef _WIN32
        int sender_len = sizeof(sender_addr);
#else
        socklen_t sender_len = sizeof(sender_addr);
#endif

        /*
         * After FIN, listen for a limited time to acknowledge a
         * retransmitted FIN if the original FIN ACK was lost.
         */
        if (fin_linger)
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);

            struct timeval tv;
            tv.tv_sec = FIN_LINGER_MS / 1000;
            tv.tv_usec = (FIN_LINGER_MS % 1000) * 1000;

            int ready = select(
                (int)sockfd + 1, &readfds, NULL, NULL, &tv);

            if (ready == 0)
            {
                printf("FIN linger period expired\n");
                break;
            }

#ifdef _WIN32
            if (ready == SOCKET_ERROR)
#else
            if (ready < 0)
#endif
            {
                printf("select failed during FIN linger\n");
                break;
            }
        }

        int received = recvfrom(
            sockfd, (char *)receive_buffer, sizeof(receive_buffer),
            0, (struct sockaddr *)&sender_addr, &sender_len);

#ifdef _WIN32
        if (received == SOCKET_ERROR)
#else
        if (received < 0)
#endif
        {
            printf("recvfrom failed\n");
            break;
        }

        Packet packet;

        if (parse_packet(
                receive_buffer, (size_t)received, &packet) != 0)
        {
            printf("Invalid packet received\n");
            continue;
        }

        if (!verify_checksum(&packet))
        {
            printf("Checksum verification failed\n");
            continue;
        }

        if (packet.type == PACKET_DATA)
        {
            if (fin_linger)
            {
                printf("Ignoring DATA packet during FIN linger\n");
                continue;
            }

            printf("Received DATA packet %u\n", packet.sequence_number);

            if (packet.sequence_number == expected_sequence)
            {
                size_t written = fwrite(
                    packet.payload, 1, packet.payload_length, file);

                if (written != packet.payload_length)
                {
                    printf("Error writing file\n");
                    break;
                }

                printf("Accepted DATA packet %u\n",
                       packet.sequence_number);

                expected_sequence++;

                if (packet.sequence_number == LOST_ACK_1 ||
                    packet.sequence_number == LOST_ACK_2)
                {
                    printf("Simulated lost ACK for packet %u\n",
                           packet.sequence_number);
                }
                else if (send_ack(
                             sockfd, &sender_addr, sender_len,
                             expected_sequence - 1) == 0)
                {
                    printf("Sent cumulative ACK for packet %u\n",
                           expected_sequence - 1);
                }
            }
            else if (packet.sequence_number < expected_sequence)
            {
                printf("Duplicate DATA packet %u\n",
                       packet.sequence_number);

                if (send_ack(
                        sockfd, &sender_addr, sender_len,
                        expected_sequence - 1) == 0)
                {
                    printf("Re-sent cumulative ACK for packet %u\n",
                           expected_sequence - 1);
                }
            }
            else
            {
                printf("Out-of-order DATA packet %u\n",
                       packet.sequence_number);

                if (expected_sequence > 0 &&
                    send_ack(
                        sockfd, &sender_addr, sender_len,
                        expected_sequence - 1) == 0)
                {
                    printf("Sent cumulative ACK for packet %u\n",
                           expected_sequence - 1);
                }
            }
        }
        else if (packet.type == PACKET_FIN)
        {
            if (packet.flags != FLAG_FIN ||
                packet.payload_length != 0 ||
                packet.sequence_number != expected_sequence)
            {
                printf("Invalid FIN packet\n");
                continue;
            }

            printf("Received FIN packet %u\n", packet.sequence_number);

            if (fflush(file) != 0)
            {
                perror("Failed to flush received file");
                break;
            }

            if (send_ack(
                    sockfd, &sender_addr, sender_len,
                    packet.sequence_number) != 0)
            {
                /* Continue listening; sender may retry FIN. */
                continue;
            }

            printf("Sent FIN ACK for packet %u\n",
                   packet.sequence_number);

            if (!transfer_complete)
            {
                printf("File transfer completed.\n");
                transfer_complete = 1;
            }

            /* Stay available to acknowledge retransmitted FIN packets. */
            fin_linger = 1;
        }
        else if (packet.type == PACKET_ACK)
        {
            printf("Unexpected ACK packet received\n");
        }
        else
        {
            printf("Unknown packet type received\n");
        }
    }

    if (fclose(file) != 0)
    {
        perror("Error closing output file");
        transfer_complete = 0;
    }

#ifdef _WIN32
    closesocket(sockfd);
    WSACleanup();
#else
    close(sockfd);
#endif

    printf("Receiver stopped.\n");
    return transfer_complete ? 0 : 1;
}