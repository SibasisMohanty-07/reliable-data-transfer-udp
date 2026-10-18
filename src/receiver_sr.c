
#include "packet.h"
#include "../include/sr.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#endif

#define SERVER_PORT 9000
#define OUTPUT_FILE "received_sr.txt"
#define SIMULATE_LOST_ACK_SEQ 2

static int send_individual_ack(socket_t sock,
                               const struct sockaddr_in *client,
                               uint32_t sequence)
{
    /* Simulate losing the first ACK for sequence 2. */
    static int lost_ack_simulated = 0;

    if (sequence == SIMULATE_LOST_ACK_SEQ &&
        !lost_ack_simulated)
    {
        lost_ack_simulated = 1;
        printf("SIMULATION: Dropped ACK for seq=%u\n",
               sequence);
        return 0;
    }

    Packet ack;
    uint8_t buffer[14 + MAX_PAYLOAD_SIZE];

    initialize_packet(&ack);
    ack.type = PACKET_ACK;
    ack.flags = FLAG_NONE;
    ack.sequence_number = 0;
    ack.acknowledgement_number = sequence;
    ack.payload_length = 0;
    ack.checksum = calculate_checksum(&ack);

    int length = serialize_packet(&ack, buffer, sizeof(buffer));

    if (length < 0)
        return -1;

    if (sendto(sock, (const char *)buffer, length, 0,
               (const struct sockaddr *)client,
               sizeof(*client)) < 0)
        return -1;

    printf("Sent individual ACK for seq=%u\n", sequence);
    return 0;
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        fprintf(stderr, "Winsock initialization failed\n");
        return 1;
    }
#endif

    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);

#ifdef _WIN32
    if (sock == INVALID_SOCKET)
#else
    if (sock < 0)
#endif
    {
        perror("Socket creation failed");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(SERVER_PORT);

    if (bind(sock, (struct sockaddr *)&server,
             sizeof(server)) < 0)
    {
        perror("Bind failed");
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    FILE *output = fopen(OUTPUT_FILE, "wb");

    if (!output)
    {
        perror("Cannot open output file");
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    Packet buffer[SR_WINDOW_SIZE];
    int occupied[SR_WINDOW_SIZE] = {0};
    uint32_t expected = 0;

    printf("Selective Repeat receiver listening on port %d\n",
           SERVER_PORT);

    for (;;)
    {
        uint8_t raw[14 + MAX_PAYLOAD_SIZE];
        struct sockaddr_in client;

#ifdef _WIN32
        int client_len = sizeof(client);
#else
        socklen_t client_len = sizeof(client);
#endif

        int received = recvfrom(
            sock, (char *)raw, sizeof(raw), 0,
            (struct sockaddr *)&client, &client_len);

        if (received < 0)
        {
            perror("Receive failed");
            continue;
        }

        Packet packet;

        if (parse_packet(raw, (size_t)received, &packet) != 0 ||
            !verify_checksum(&packet))
        {
            printf("Discarded invalid packet\n");
            continue;
        }

        if (packet.type == PACKET_FIN &&
            packet.flags == FLAG_FIN &&
            packet.payload_length == 0)
        {
            if (packet.sequence_number == expected)
            {
                printf("FIN received. File transfer complete.\n");
                break;
            }

            printf("FIN received before all data; ignored\n");
            continue;
        }

        if (packet.type != PACKET_DATA)
            continue;

        uint32_t seq = packet.sequence_number;

        /* Already delivered: acknowledge the duplicate again. */
        if (seq < expected)
        {
            printf("Duplicate DATA seq=%u\n", seq);
            send_individual_ack(sock, &client, seq);
            continue;
        }

        /* Accept packets inside the receiver's window. */
        if (seq >= expected &&
            seq < expected + SR_WINDOW_SIZE)
        {
            unsigned int index = seq % SR_WINDOW_SIZE;

            if (!occupied[index])
            {
                buffer[index] = packet;
                occupied[index] = 1;

                printf("Buffered DATA seq=%u\n", seq);
            }
            else if (buffer[index].sequence_number != seq)
            {
                printf("Buffer slot conflict for seq=%u\n", seq);
                continue;
            }

            /* ACK each accepted packet individually. */
            if (send_individual_ack(sock, &client, seq) != 0)
                perror("ACK send failed");

            /* Deliver buffered packets only in sequence order. */
            while (occupied[expected % SR_WINDOW_SIZE] &&
                   buffer[expected % SR_WINDOW_SIZE].sequence_number
                       == expected)
            {
                Packet *ready =
                    &buffer[expected % SR_WINDOW_SIZE];

                if (fwrite(ready->payload, 1,
                           ready->payload_length, output)
                    != ready->payload_length)
                {
                    perror("File write failed");
                    fclose(output);
                    CLOSE_SOCKET(sock);
#ifdef _WIN32
                    WSACleanup();
#endif
                    return 1;
                }

                printf("Delivered DATA seq=%u to file", expected);
                printf("\n");

                occupied[expected % SR_WINDOW_SIZE] = 0;
                expected++;
            }

            fflush(output);
        }
        else
        {
            printf("DATA seq=%u outside receiver window; ignored\n",
                   seq);
        }
    }

    fclose(output);
    CLOSE_SOCKET(sock);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}