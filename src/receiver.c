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
#endif

#include "../include/protocol.h"
#include "packet.h"

#define SERVER_PORT 9000

int main()
{
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif

    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);

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

    if (bind(sockfd,
             (struct sockaddr *)&receiver_addr,
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

    printf("Receiver started on port %d...\n",
           SERVER_PORT);

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

    while (1)
    {
        struct sockaddr_in sender_addr;

#ifdef _WIN32
        int sender_len = sizeof(sender_addr);
#else
        socklen_t sender_len = sizeof(sender_addr);
#endif

        int received = recvfrom(
            sockfd,
            (char *)receive_buffer,
            sizeof(receive_buffer),
            0,
            (struct sockaddr *)&sender_addr,
            &sender_len);

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

        if (parse_packet(receive_buffer,
                         received,
                         &packet) != 0)
        {
            printf("Invalid packet received\n");
            continue;
        }

        /*
         * Verify checksum.
         */
        if (!verify_checksum(&packet))
        {
            printf("Checksum verification failed\n");
            continue;
        }

        /*
         * DATA packet.
         */
        if (packet.type == PACKET_DATA)
        {
            printf("Received DATA packet %u\n",
                   packet.sequence_number);

            /*
             * Correct packet.
             */
            if (packet.sequence_number == expected_sequence)
            {
                size_t written = fwrite(
                    packet.payload,
                    1,
                    packet.payload_length,
                    file);

                if (written != packet.payload_length)
                {
                    printf("Error writing file\n");
                    break;
                }

                printf("Accepted DATA packet %u\n",
                       packet.sequence_number);

                /*
                 * Create ACK.
                 */
                Packet ack_packet;

                initialize_packet(&ack_packet);

                ack_packet.type = PACKET_ACK;
                ack_packet.flags = FLAG_NONE;
                ack_packet.sequence_number = 0;
                ack_packet.acknowledgement_number =
                    packet.sequence_number;
                ack_packet.payload_length = 0;

                ack_packet.checksum =
                    calculate_checksum(&ack_packet);

                uint8_t ack_buffer[
                    14 + MAX_PAYLOAD_SIZE];

                int ack_size = serialize_packet(
                    &ack_packet,
                    ack_buffer,
                    sizeof(ack_buffer));

                sendto(
                    sockfd,
                    (const char *)ack_buffer,
                    ack_size,
                    0,
                    (struct sockaddr *)&sender_addr,
                    sender_len);

                printf("Sent ACK for packet %u\n",
                       packet.sequence_number);

                expected_sequence++;
            }
            else
            {
                /*
                 * Duplicate packet.
                 *
                 * Send ACK again, but don't write
                 * the payload a second time.
                 */
                if (packet.sequence_number <
                    expected_sequence)
                {
                    printf("Duplicate DATA packet %u\n",
                           packet.sequence_number);

                    Packet ack_packet;

                    initialize_packet(&ack_packet);

                    ack_packet.type = PACKET_ACK;
                    ack_packet.flags = FLAG_NONE;
                    ack_packet.sequence_number = 0;

                    ack_packet.acknowledgement_number =
                        packet.sequence_number;

                    ack_packet.payload_length = 0;

                    ack_packet.checksum =
                        calculate_checksum(&ack_packet);

                    uint8_t ack_buffer[
                        14 + MAX_PAYLOAD_SIZE];

                    int ack_size = serialize_packet(
                        &ack_packet,
                        ack_buffer,
                        sizeof(ack_buffer));

                    sendto(
                        sockfd,
                        (const char *)ack_buffer,
                        ack_size,
                        0,
                        (struct sockaddr *)&sender_addr,
                        sender_len);

                    printf("Re-sent ACK for packet %u\n",
                           packet.sequence_number);
                }
            }
        }

        /*
         * FIN packet.
         */
        else if (packet.type == PACKET_FIN)
        {
            if (packet.flags != FLAG_FIN ||
                packet.payload_length != 0)
            {
                printf("Invalid FIN packet\n");
                continue;
            }

            printf("Received FIN packet %u\n",
                   packet.sequence_number);

            printf("File transfer completed.\n");

            break;
        }

        /*
         * ACK packets are not expected at receiver.
         */
        else if (packet.type == PACKET_ACK)
        {
            printf("Unexpected ACK packet received\n");
        }
    }

    fclose(file);

#ifdef _WIN32
    closesocket(sockfd);
    WSACleanup();
#else
    close(sockfd);
#endif

    printf("Receiver stopped.\n");

    return 0;
}