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
#include <sys/time.h>
#endif

#include "../include/protocol.h"
#include "packet.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000

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

    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);

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

    /*
     * Set receive timeout.
     */
#ifdef _WIN32
    DWORD timeout = TIMEOUT_MS;

    if (setsockopt(sockfd,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   (const char *)&timeout,
                   sizeof(timeout)) < 0)
    {
        printf("Failed to set timeout\n");
    }
#else
    struct timeval timeout;

    timeout.tv_sec = 0;
    timeout.tv_usec = TIMEOUT_MS * 1000;

    setsockopt(sockfd,
               SOL_SOCKET,
               SO_RCVTIMEO,
               &timeout,
               sizeof(timeout));
#endif

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

    char buffer[MAX_PAYLOAD_SIZE];

    uint8_t serialized_buffer[14 + MAX_PAYLOAD_SIZE];

    uint32_t sequence_number = 0;

    size_t bytes_read;

    int transfer_success = 1;

    /*
     * Stop-and-Wait DATA transmission.
     */
    while ((bytes_read = fread(buffer,
                               1,
                               MAX_PAYLOAD_SIZE,
                               file)) > 0)
    {
        Packet packet;

        if (create_data_packet(&packet,
                               sequence_number,
                               buffer,
                               (uint16_t)bytes_read) != 0)
        {
            printf("Failed to create DATA packet\n");
            transfer_success = 0;
            break;
        }

        int serialized_size = serialize_packet(
            &packet,
            serialized_buffer,
            sizeof(serialized_buffer));

        if (serialized_size < 0)
        {
            printf("Failed to serialize DATA packet\n");
            transfer_success = 0;
            break;
        }

        int acknowledged = 0;
        int retry_count = 0;

        /*
         * Stop-and-Wait:
         * Send one packet and wait for its ACK.
         */
        while (!acknowledged && retry_count < MAX_RETRIES)
        {
            int sent = sendto(
                sockfd,
                (const char *)serialized_buffer,
                serialized_size,
                0,
                (struct sockaddr *)&receiver_addr,
                sizeof(receiver_addr));

#ifdef _WIN32
            if (sent == SOCKET_ERROR)
#else
            if (sent < 0)
#endif
            {
                printf("sendto failed\n");
                retry_count++;
                continue;
            }

            printf("Sent DATA packet %u\n",
                   sequence_number);

            /*
             * Wait for ACK.
             */
            uint8_t ack_buffer[14 + MAX_PAYLOAD_SIZE];

            struct sockaddr_in ack_addr;

#ifdef _WIN32
            int ack_addr_len = sizeof(ack_addr);
#else
            socklen_t ack_addr_len = sizeof(ack_addr);
#endif

            int received = recvfrom(
                sockfd,
                (char *)ack_buffer,
                sizeof(ack_buffer),
                0,
                (struct sockaddr *)&ack_addr,
                &ack_addr_len);

#ifdef _WIN32
            if (received == SOCKET_ERROR)
            {
                printf("Timeout. Retransmitting DATA packet %u\n",
                       sequence_number);

                retry_count++;
                continue;
            }
#else
            if (received < 0)
            {
                printf("Timeout. Retransmitting DATA packet %u\n",
                       sequence_number);

                retry_count++;
                continue;
            }
#endif

            Packet ack_packet;

            if (parse_packet(ack_buffer,
                             received,
                             &ack_packet) != 0)
            {
                printf("Invalid ACK packet\n");
                continue;
            }

            /*
             * Verify ACK checksum.
             */
            if (!verify_checksum(&ack_packet))
            {
                printf("Invalid ACK checksum\n");
                continue;
            }

            /*
             * Check ACK packet.
             */
            if (ack_packet.type == PACKET_ACK &&
                ack_packet.acknowledgement_number == sequence_number)
            {
                printf("Received ACK for DATA packet %u\n",
                       sequence_number);

                acknowledged = 1;
            }
            else
            {
                printf("Wrong ACK received\n");
            }
        }

        if (!acknowledged)
        {
            printf("Maximum retries reached for packet %u\n",
                   sequence_number);

            transfer_success = 0;
            break;
        }

        /*
         * Move to next sequence number only
         * after receiving correct ACK.
         */
        sequence_number++;
    }

    if (ferror(file))
    {
        printf("Error reading input file\n");
        transfer_success = 0;
    }

    /*
     * Send FIN after all DATA packets are acknowledged.
     */
    if (transfer_success)
    {
        Packet fin_packet;

        if (create_fin_packet(&fin_packet,
                              sequence_number) == 0)
        {
            int fin_size = serialize_packet(
                &fin_packet,
                serialized_buffer,
                sizeof(serialized_buffer));

            if (fin_size > 0)
            {
                int sent = sendto(
                    sockfd,
                    (const char *)serialized_buffer,
                    fin_size,
                    0,
                    (struct sockaddr *)&receiver_addr,
                    sizeof(receiver_addr));

#ifdef _WIN32
                if (sent != SOCKET_ERROR)
#else
                if (sent >= 0)
#endif
                {
                    printf("Sent FIN packet %u\n",
                           sequence_number);
                }
                else
                {
                    printf("Failed to send FIN\n");
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