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
#include "../include/gbn.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9001

#define TIMEOUT_MS 1000
#define MAX_RETRIES 5

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif

    FILE *file = fopen(argv[1], "rb");

    if (file == NULL)
    {
        perror("Error opening input file");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

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
        fclose(file);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(SERVER_PORT);
    receiver_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (receiver_addr.sin_addr.s_addr == INADDR_NONE)
    {
        printf("Invalid IP address\n");
        fclose(file);
#ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
        return 1;
    }

    printf("Sender started.\n");

    GBNState gbn;
    gbn.base = 0;
    gbn.next_seq = 0;
    gbn.window_size = GBN_WINDOW_SIZE;
    gbn.timer_running = false;
    gbn.retransmissions = 0;

    Packet window[GBN_WINDOW_SIZE];
    int valid[GBN_WINDOW_SIZE];
    memset(valid, 0, sizeof(valid));

    char buffer[MAX_PAYLOAD_SIZE];
    uint8_t serialized_buffer[14 + MAX_PAYLOAD_SIZE];
    uint8_t ack_buffer[14 + MAX_PAYLOAD_SIZE];

    uint32_t sequence_number = 0;
    int eof = 0;
    int transfer_success = 1;

    while (!eof || gbn.base < gbn.next_seq)
    {
        while (!eof &&
               gbn.next_seq < gbn.base + gbn.window_size)
        {
            size_t bytes_read = fread(
                buffer, 1, MAX_PAYLOAD_SIZE, file);

            if (bytes_read == 0)
            {
                eof = 1;
                break;
            }

            Packet packet;

            if (create_data_packet(
                    &packet, sequence_number, buffer,
                    (uint16_t)bytes_read) != 0)
            {
                printf("Failed to create DATA packet\n");
                transfer_success = 0;
                break;
            }

            int index = sequence_number % gbn.window_size;
            window[index] = packet;
            valid[index] = 1;

            int serialized_size = serialize_packet(
                &packet, serialized_buffer, sizeof(serialized_buffer));

            if (serialized_size <= 0)
            {
                printf("Failed to serialize DATA packet\n");
                transfer_success = 0;
                break;
            }

            int sent = sendto(
                sockfd, (const char *)serialized_buffer,
                serialized_size, 0,
                (struct sockaddr *)&receiver_addr,
                sizeof(receiver_addr));

#ifdef _WIN32
            if (sent == SOCKET_ERROR)
#else
            if (sent < 0)
#endif
            {
                printf("sendto failed\n");
                transfer_success = 0;
                break;
            }

            printf("Sent DATA packet %u\n", sequence_number);
            gbn.next_seq++;
            sequence_number++;
        }

        if (!transfer_success)
            break;

        if (gbn.base == gbn.next_seq)
        {
            gbn.timer_running = false;
            continue;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval tv;
        tv.tv_sec = TIMEOUT_MS / 1000;
        tv.tv_usec = (TIMEOUT_MS % 1000) * 1000;

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &tv);

#ifdef _WIN32
        if (ready == SOCKET_ERROR)
#else
        if (ready < 0)
#endif
        {
            printf("select failed\n");
            transfer_success = 0;
            break;
        }

        if (ready == 0)
        {
            gbn.retransmissions++;
            printf("ACK timeout\n");

            if (gbn.retransmissions > MAX_RETRIES)
            {
                printf("Maximum retransmissions reached\n");
                transfer_success = 0;
                break;
            }

            printf("Go-Back-N: retransmitting from packet %u\n",
                   gbn.base);

            for (uint32_t seq = gbn.base;
                 seq < gbn.next_seq; seq++)
            {
                int index = seq % gbn.window_size;

                if (!valid[index])
                    continue;

                Packet *packet = &window[index];

                int packet_size = serialize_packet(
                    packet, serialized_buffer,
                    sizeof(serialized_buffer));

                if (packet_size > 0)
                {
                    sendto(
                        sockfd, (const char *)serialized_buffer,
                        packet_size, 0,
                        (struct sockaddr *)&receiver_addr,
                        sizeof(receiver_addr));

                    printf("Retransmitted DATA packet %u\n",
                           packet->sequence_number);
                }
            }

            continue;
        }

        if (FD_ISSET(sockfd, &readfds))
        {
            struct sockaddr_in ack_addr;
#ifdef _WIN32
            int ack_addr_len = sizeof(ack_addr);
#else
            socklen_t ack_addr_len = sizeof(ack_addr);
#endif

            int received = recvfrom(
                sockfd, (char *)ack_buffer, sizeof(ack_buffer), 0,
                (struct sockaddr *)&ack_addr, &ack_addr_len);

#ifdef _WIN32
            if (received == SOCKET_ERROR)
#else
            if (received < 0)
#endif
            {
                printf("recvfrom failed\n");
                continue;
            }

            Packet ack_packet;

            if (parse_packet(
                    ack_buffer, (size_t)received,
                    &ack_packet) == 0 &&
                verify_checksum(&ack_packet) &&
                ack_packet.type == PACKET_ACK)
            {
                uint32_t ack_number =
                    ack_packet.acknowledgement_number;

                if (ack_number >= gbn.base &&
                    ack_number < gbn.next_seq)
                {
                    gbn.base = ack_number + 1;

                    printf("Received cumulative ACK for packet %u\n",
                           ack_number);

                    gbn.retransmissions = 0;
                    gbn.timer_running = (gbn.base != gbn.next_seq);
                }
            }
        }
    }

    if (ferror(file))
    {
        printf("Error reading input file\n");
        transfer_success = 0;
    }

    /*
     * FIN handshake:
     * Send FIN and wait for an ACK that acknowledges its sequence number.
     * Retry FIN on timeout or an invalid/unrelated ACK.
     */
    if (transfer_success)
    {
        Packet fin_packet;

        if (create_fin_packet(&fin_packet, sequence_number) != 0)
        {
            printf("Failed to create FIN packet\n");
            transfer_success = 0;
        }
        else
        {
            int fin_size = serialize_packet(
                &fin_packet,
                serialized_buffer,
                sizeof(serialized_buffer));

            if (fin_size <= 0)
            {
                printf("Failed to serialize FIN packet\n");
                transfer_success = 0;
            }
            else
            {
                int fin_ack_received = 0;

                for (int attempt = 1;
                     attempt <= MAX_RETRIES && !fin_ack_received;
                     attempt++)
                {
                    int sent = sendto(
                        sockfd,
                        (const char *)serialized_buffer,
                        fin_size,
                        0,
                        (struct sockaddr *)&receiver_addr,
                        sizeof(receiver_addr));

#ifdef _WIN32
                    if (sent == SOCKET_ERROR)
#else
                    if (sent < 0)
#endif
                    {
                        printf("Failed to send FIN\n");
                        transfer_success = 0;
                        break;
                    }

                    printf("Sent FIN packet %u (attempt %d)\n",
                           sequence_number, attempt);

                    fd_set fin_readfds;
                    FD_ZERO(&fin_readfds);
                    FD_SET(sockfd, &fin_readfds);

                    struct timeval fin_tv;
                    fin_tv.tv_sec = TIMEOUT_MS / 1000;
                    fin_tv.tv_usec = (TIMEOUT_MS % 1000) * 1000;

                    int fin_ready = select(
                        sockfd + 1, &fin_readfds, NULL, NULL, &fin_tv);

#ifdef _WIN32
                    if (fin_ready == SOCKET_ERROR)
#else
                    if (fin_ready < 0)
#endif
                    {
                        printf("select failed while waiting for FIN ACK\n");
                        transfer_success = 0;
                        break;
                    }

                    if (fin_ready == 0)
                    {
                        printf("FIN ACK timeout\n");
                        continue;
                    }

                    struct sockaddr_in fin_ack_addr;
#ifdef _WIN32
                    int fin_ack_addr_len = sizeof(fin_ack_addr);
#else
                    socklen_t fin_ack_addr_len = sizeof(fin_ack_addr);
#endif

                    int ack_received = recvfrom(
                        sockfd,
                        (char *)ack_buffer,
                        sizeof(ack_buffer),
                        0,
                        (struct sockaddr *)&fin_ack_addr,
                        &fin_ack_addr_len);

#ifdef _WIN32
                    if (ack_received == SOCKET_ERROR)
#else
                    if (ack_received < 0)
#endif
                    {
                        printf("Failed to receive FIN ACK\n");
                        continue;
                    }

                    Packet fin_ack_packet;

                    if (parse_packet(
                            ack_buffer, (size_t)ack_received,
                            &fin_ack_packet) == 0 &&
                        verify_checksum(&fin_ack_packet) &&
                        fin_ack_packet.type == PACKET_ACK &&
                        fin_ack_packet.acknowledgement_number ==
                            sequence_number)
                    {
                        fin_ack_received = 1;
                        printf("Received FIN ACK for packet %u\n",
                               sequence_number);
                    }
                    else
                    {
                        printf("Invalid or unrelated ACK received\n");
                    }
                }

                if (!fin_ack_received)
                {
                    printf("FIN handshake failed: no valid FIN ACK received\n");
                    transfer_success = 0;
                }
            }
        }
    }

    if (transfer_success)
        printf("File transmission completed successfully.\n");
    else
        printf("File transmission failed.\n");

    fclose(file);

#ifdef _WIN32
    closesocket(sockfd);
    WSACleanup();
#else
    close(sockfd);
#endif

    return transfer_success ? 0 : 1;
}
