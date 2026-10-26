#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

#include "../include/protocol.h"
#include "packet.h"

#define SERVER_IP "127.0.0.1"
#define PROXY_PORT 9001

/* Implemented in src/stop_and_wait.c. */
int stop_and_wait_send(SOCKET sockfd,
                       struct sockaddr_in *receiver_addr,
                       Packet *packet);

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    FILE *input = fopen(argv[1], "rb");
    if (!input) {
        perror("Cannot open input file");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed\n");
        fclose(input);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    struct sockaddr_in proxy;
    memset(&proxy, 0, sizeof(proxy));
    proxy.sin_family = AF_INET;
    proxy.sin_port = htons(PROXY_PORT);
    proxy.sin_addr.s_addr = inet_addr(SERVER_IP);

    uint32_t seq = 0;
    char payload[MAX_PAYLOAD_SIZE];
    int ok = 1;

    printf("Stop-and-Wait sender started.\n");

    for (;;) {
        size_t n = fread(payload, 1, sizeof(payload), input);
        if (n == 0) {
            if (ferror(input)) {
                perror("File read failed");
                ok = 0;
            }
            break;
        }

        Packet packet;
        if (create_data_packet(&packet, seq, payload, (uint16_t)n) != 0) {
            fprintf(stderr, "Failed to create DATA packet %u\n", seq);
            ok = 0;
            break;
        }

        if (stop_and_wait_send(sock, &proxy, &packet) != 0) {
            fprintf(stderr, "Stop-and-Wait failed at DATA packet %u\n", seq);
            ok = 0;
            break;
        }
        seq++;
    }

    if (ok) {
        Packet fin;
        uint8_t wire[14 + MAX_PAYLOAD_SIZE];
        if (create_fin_packet(&fin, seq) != 0) {
            fprintf(stderr, "Failed to create FIN packet\n");
            ok = 0;
        } else {
            int length = serialize_packet(&fin, wire, sizeof(wire));
            if (length <= 0 ||
                sendto(sock, (const char *)wire, length, 0,
                       (struct sockaddr *)&proxy, sizeof(proxy)) == SOCKET_ERROR) {
                fprintf(stderr, "Failed to send FIN packet\n");
                ok = 0;
            } else {
                printf("Sent FIN packet %u\n", seq);
            }
        }
    }

    if (ok)
        printf("Stop-and-Wait file transmission completed successfully.\n");
    else
        printf("Stop-and-Wait file transmission failed.\n");

    fclose(input);
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return ok ? 0 : 1;
}
