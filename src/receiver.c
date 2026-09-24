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

    /* Create UDP socket */
    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);

#ifdef _WIN32
    if (sockfd == INVALID_SOCKET)
#else
    if (sockfd < 0)
#endif
    {
#ifdef _WIN32
        printf("Socket creation failed. WSA error: %d\n",
               WSAGetLastError());
        WSACleanup();
#else
        perror("Socket creation failed");
#endif
        return 1;
    }

    /* Receiver address */
    struct sockaddr_in receiver_addr;

    memset(&receiver_addr, 0, sizeof(receiver_addr));

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    receiver_addr.sin_port = htons(SERVER_PORT);

    /* Bind socket */
    if (bind(sockfd,
             (struct sockaddr *)&receiver_addr,
             sizeof(receiver_addr)) < 0)
    {
#ifdef _WIN32
        printf("bind failed. WSA error: %d\n",
               WSAGetLastError());

        closesocket(sockfd);
        WSACleanup();
#else
        perror("bind failed");
        close(sockfd);
#endif
        return 1;
    }

    printf("Receiver started on port %d...\n",
           SERVER_PORT);

    /* Open output file */
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

    /*
     * Maximum possible serialized packet:
     *
     * Header = 14 bytes
     * Payload = MAX_PAYLOAD_SIZE
     */
    uint8_t receive_buffer[14 + MAX_PAYLOAD_SIZE];

    while (1)
    {
        struct sockaddr_in sender_addr;

#ifdef _WIN32
        int sender_len = sizeof(sender_addr);
#else
        socklen_t sender_len = sizeof(sender_addr);
#endif

        /* Receive packet */
        int received = recvfrom(
            sockfd,
            (char *)receive_buffer,
            sizeof(receive_buffer),
            0,
            (struct sockaddr *)&sender_addr,
            &sender_len
        );

#ifdef _WIN32
        if (received == SOCKET_ERROR)
        {
            printf("recvfrom failed. WSA error: %d\n",
                   WSAGetLastError());
            break;
        }
#else
        if (received < 0)
        {
            perror("recvfrom failed");
            break;
        }
#endif

        printf("Received %d bytes from sender\n",
               received);

        /* Parse packet */
        Packet packet;

        int result = parse_packet(
            receive_buffer,
            received,
            &packet
        );

        if (result != 0)
        {
            printf("Invalid packet received\n");
            continue;
        }

        /*
         * Check packet type before processing it.
         */
        if (packet.type != PACKET_DATA &&
            packet.type != PACKET_ACK &&
            packet.type != PACKET_FIN)
        {
            printf("Unknown packet type: %d\n",
                   packet.type);
            continue;
        }

        /*
         * Verify checksum.
         */
        if (!verify_checksum(&packet))
        {
            printf("Checksum verification failed "
                   "for packet %u\n",
                   packet.sequence_number);
            continue;
        }

        /*
         * DATA packet
         */
        if (packet.type == PACKET_DATA)
        {
            printf("Received DATA packet %u "
                   "(payload: %u bytes)\n",
                   packet.sequence_number,
                   packet.payload_length);

            size_t written = fwrite(
                packet.payload,
                1,
                packet.payload_length,
                file
            );

            if (written != packet.payload_length)
            {
                printf("Error writing payload to file\n");
                break;
            }
        }

        /*
         * ACK packet
         *
         * ACK is defined in the protocol but is not
         * used yet in the current project step.
         */
        else if (packet.type == PACKET_ACK)
        {
            printf("Received ACK packet %u\n",
                   packet.sequence_number);

            continue;
        }

        /*
         * FIN packet
         */
        else if (packet.type == PACKET_FIN)
        {
            /*
             * FIN must have:
             * - FIN flag
             * - zero payload
             */
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