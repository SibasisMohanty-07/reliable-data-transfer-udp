#include "packet.h"
#include "../include/sr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#endif

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000
#define RECEIVE_TIMEOUT_MS 100

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL +
           (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

static int send_packet(socket_t sock,
                       const struct sockaddr_in *server,
                       const Packet *packet)
{
    uint8_t buffer[14 + MAX_PAYLOAD_SIZE];
    int length = serialize_packet(packet, buffer, sizeof(buffer));

    if (length < 0)
        return -1;

    if (sendto(sock, (const char *)buffer, length, 0,
               (const struct sockaddr *)server,
               sizeof(*server)) < 0)
        return -1;

    return 0;
}

static int send_acknowledged_data(socket_t sock,
                                  const struct sockaddr_in *server,
                                  FILE *input,
                                  uint32_t *total_packets)
{
    SRSlot slots[SR_WINDOW_SIZE] = {0};
    uint32_t base = 0;
    uint32_t next_seq = 0;
    int eof = 0;

    while (!eof || base < next_seq)
    {
        while (!eof && next_seq < base + SR_WINDOW_SIZE)
        {
            char payload[MAX_PAYLOAD_SIZE];
            size_t n = fread(payload, 1, sizeof(payload), input);

            if (n == 0)
            {
                if (ferror(input))
                    return -1;
                eof = 1;
                break;
            }

            SRSlot *slot = &slots[next_seq % SR_WINDOW_SIZE];

            memset(slot, 0, sizeof(*slot));

            if (create_data_packet(&slot->packet, next_seq,
                                   payload, (uint16_t)n) != 0)
                return -1;

            slot->valid = true;
            slot->acknowledged = false;
            slot->retries = 0;

            if (send_packet(sock, server, &slot->packet) != 0)
                return -1;

            slot->last_sent_ms = now_ms();

            printf("Sent DATA seq=%u (%zu bytes)\n",
                   next_seq, n);

            next_seq++;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = RECEIVE_TIMEOUT_MS * 1000;

        int ready = select((int)(sock + 1), &readfds,
                           NULL, NULL, &tv);

        if (ready > 0 && FD_ISSET(sock, &readfds))
        {
            uint8_t buffer[14 + MAX_PAYLOAD_SIZE];
            struct sockaddr_in from;
#ifdef _WIN32
            int from_len = sizeof(from);
#else
            socklen_t from_len = sizeof(from);
#endif
            int received = recvfrom(sock, (char *)buffer,
                                    sizeof(buffer), 0,
                                    (struct sockaddr *)&from,
                                    &from_len);

            Packet ack;

            if (received >= 0 &&
                parse_packet(buffer, (size_t)received, &ack) == 0 &&
                verify_checksum(&ack) &&
                ack.type == PACKET_ACK)
            {
                uint32_t seq = ack.acknowledgement_number;

                if (seq >= base && seq < next_seq)
                {
                    SRSlot *slot = &slots[seq % SR_WINDOW_SIZE];

                    if (slot->valid &&
                        slot->packet.sequence_number == seq)
                    {
                        slot->acknowledged = true;
                        printf("Received individual ACK for seq=%u\n",
                               seq);
                    }
                }

                while (base < next_seq)
                {
                    SRSlot *first =
                        &slots[base % SR_WINDOW_SIZE];

                    if (!first->valid ||
                        first->packet.sequence_number != base ||
                        !first->acknowledged)
                        break;

                    first->valid = false;
                    base++;
                }
            }
        }

        uint64_t now = now_ms();

        for (uint32_t seq = base; seq < next_seq; seq++)
        {
            SRSlot *slot = &slots[seq % SR_WINDOW_SIZE];

            if (slot->valid && !slot->acknowledged &&
                now - slot->last_sent_ms >= SR_TIMEOUT_MS)
            {
                if (slot->retries >= SR_MAX_RETRIES)
                {
                    fprintf(stderr,
                            "Maximum retries reached for seq=%u\n",
                            seq);
                    return -1;
                }

                if (send_packet(sock, server, &slot->packet) != 0)
                    return -1;

                slot->last_sent_ms = now;
                slot->retries++;

                printf("Timeout: retransmitting only seq=%u "
                       "(retry %u)\n",
                       seq, slot->retries);
            }
        }
    }
*total_packets = next_seq;
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    FILE *input = fopen(argv[1], "rb");

    if (!input)
    {
        perror("Cannot open input file");
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        fprintf(stderr, "Winsock initialization failed\n");
        fclose(input);
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
        fclose(input);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server address\n");
        CLOSE_SOCKET(sock);
        fclose(input);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("Selective Repeat sender started.\n");

    uint32_t total_packets = 0;
    int result = send_acknowledged_data(
    sock, &server, input, &total_packets);

              if (result == 0)
    {
        Packet fin;

        if (create_fin_packet(&fin, total_packets) != 0 ||
            send_packet(sock, &server, &fin) != 0)
        {
            fprintf(stderr, "Failed to send FIN packet\n");
            result = -1;
        }
        else
        {
            printf("FIN sent. Data transfer finished.\n");
        }
    }

    fclose(input);
    CLOSE_SOCKET(sock);

#ifdef _WIN32
    WSACleanup();
#endif

    return result == 0 ? 0 : 1;
}
