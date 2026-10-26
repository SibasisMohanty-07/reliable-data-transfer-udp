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

#include "../include/protocol.h"
#include "packet.h"

#define SERVER_PORT 9000
#define OUTPUT_FILE "received_sw.txt"

static int send_ack(socket_t sock, const struct sockaddr_in *client,
                    int client_len, uint32_t seq)
{
    Packet ack;
    uint8_t wire[14 + MAX_PAYLOAD_SIZE];

    initialize_packet(&ack);
    ack.type = PACKET_ACK;
    ack.flags = FLAG_NONE;
    ack.sequence_number = 0;
    ack.acknowledgement_number = seq;
    ack.payload_length = 0;
    ack.checksum = calculate_checksum(&ack);

    int length = serialize_packet(&ack, wire, sizeof(wire));
    if (length <= 0) return -1;

    int sent = sendto(sock, (const char *)wire, length, 0,
                      (const struct sockaddr *)client, client_len);
    return sent == length ? 0 : -1;
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (sock == INVALID_SOCKET) {
#else
    if (sock < 0) {
#endif
        fprintf(stderr, "Socket creation failed\n");
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

    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        fprintf(stderr, "Bind failed on port %d\n", SERVER_PORT);
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    FILE *output = fopen(OUTPUT_FILE, "wb");
    if (!output) {
        perror("Cannot open output file");
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    uint32_t expected = 0;
    uint8_t wire[14 + MAX_PAYLOAD_SIZE];
    printf("Stop-and-Wait receiver listening on port %d\n", SERVER_PORT);

    for (;;) {
        struct sockaddr_in client;
#ifdef _WIN32
        int client_len = sizeof(client);
#else
        socklen_t client_len = sizeof(client);
#endif
        int n = recvfrom(sock, (char *)wire, sizeof(wire), 0,
                         (struct sockaddr *)&client, &client_len);
        if (n < 0) {
            fprintf(stderr, "Receive failed\n");
            continue;
        }

        Packet packet;
        if (parse_packet(wire, (size_t)n, &packet) != 0 ||
            !verify_checksum(&packet)) {
            printf("Discarded invalid packet\n");
            continue;
        }

        if (packet.type == PACKET_DATA) {
            uint32_t seq = packet.sequence_number;
            if (seq == expected) {
                size_t written = fwrite(packet.payload, 1,
                                        packet.payload_length, output);
                if (written != packet.payload_length) {
                    fprintf(stderr, "File write failed\n");
                    break;
                }
                fflush(output);
                printf("Accepted DATA packet %u\n", seq);
                expected++;
                if (send_ack(sock, &client, client_len, seq) != 0)
                    printf("Failed to send ACK for %u\n", seq);
                else
                    printf("Sent ACK for packet %u\n", seq);
            } else if (seq < expected) {
                /* Duplicate from a retransmission: don't write twice. */
                printf("Duplicate DATA packet %u; re-ACKing\n", seq);
                if (send_ack(sock, &client, client_len, seq) != 0)
                    printf("Failed to re-send ACK for %u\n", seq);
            } else {
                printf("Out-of-order DATA packet %u; expected %u\n",
                       seq, expected);
            }
        } else if (packet.type == PACKET_FIN &&
                   packet.flags == FLAG_FIN &&
                   packet.payload_length == 0) {
            if (packet.sequence_number == expected) {
                printf("FIN received; file transfer complete.\n");
                break;
            }
            printf("FIN sequence mismatch: got %u, expected %u\n",
                   packet.sequence_number, expected);
        }
    }

    fclose(output);
    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
