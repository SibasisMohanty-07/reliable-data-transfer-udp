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

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000

int main(int argc, char *argv[])
{
    /*
     * Check command-line arguments.
     */
    if (argc != 2)
    {
        printf("Usage: %s <input_file>\n",
               argv[0]);
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

    /*
     * Open input file.
     */
    FILE *file = fopen(argv[1], "rb");

    if (file == NULL)
    {
        perror("Error opening input file");

#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /*
     * Create UDP socket.
     */
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

        fclose(file);
        WSACleanup();
#else
        perror("Socket creation failed");

        fclose(file);
#endif
        return 1;
    }

    /*
     * Receiver address.
     */
    struct sockaddr_in receiver_addr;

    memset(&receiver_addr, 0, sizeof(receiver_addr));

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &receiver_addr.sin_addr) <= 0)
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

    /*
     * File reading buffer.
     */
    char buffer[MAX_PAYLOAD_SIZE];

    /*
     * Maximum serialized packet size:
     *
     * 14-byte header + 1024-byte payload
     */
    uint8_t serialized_buffer[14 + MAX_PAYLOAD_SIZE];

    uint32_t sequence_number = 0;

    size_t bytes_read;

    int transfer_success = 1;

    /*
     * Read and send DATA packets.
     */
    while ((bytes_read = fread(
                buffer,
                1,
                MAX_PAYLOAD_SIZE,
                file)) > 0)
    {
        Packet packet;

        /*
         * Create DATA packet.
         */
        int result = create_data_packet(
            &packet,
            sequence_number,
            buffer,
            (uint16_t)bytes_read
        );

        if (result != 0)
        {
            printf("Failed to create DATA packet %u\n",
                   sequence_number);

            transfer_success = 0;
            break;
        }

        /*
         * Serialize packet.
         */
        int serialized_size = serialize_packet(
            &packet,
            serialized_buffer,
            sizeof(serialized_buffer)
        );

        if (serialized_size < 0)
        {
            printf("Failed to serialize DATA packet %u\n",
                   sequence_number);

            transfer_success = 0;
            break;
        }

        /*
         * Send DATA packet.
         */
        int sent = sendto(
            sockfd,
            (const char *)serialized_buffer,
            serialized_size,
            0,
            (struct sockaddr *)&receiver_addr,
            sizeof(receiver_addr)
        );

#ifdef _WIN32
        if (sent == SOCKET_ERROR)
        {
            printf("sendto failed. WSA error: %d\n",
                   WSAGetLastError());

            transfer_success = 0;
            break;
        }
#else
        if (sent < 0)
        {
            perror("sendto failed");

            transfer_success = 0;
            break;
        }
#endif

        /*
         * UDP should send the complete datagram.
         */
        if (sent != serialized_size)
        {
            printf("Incomplete DATA packet sent\n");

            transfer_success = 0;
            break;
        }

        printf("Sent DATA packet %u "
               "(payload: %zu bytes, serialized: %d bytes)\n",
               sequence_number,
               bytes_read,
               serialized_size);

        sequence_number++;
    }

    /*
     * Check file read error.
     */
    if (ferror(file))
    {
        printf("Error reading input file\n");
        transfer_success = 0;
    }

    /*
     * Send FIN only if all DATA packets were
     * successfully processed.
     */
    if (transfer_success)
    {
        Packet fin_packet;

        int fin_result = create_fin_packet(
            &fin_packet,
            sequence_number
        );

        if (fin_result != 0)
        {
            printf("Failed to create FIN packet\n");
            transfer_success = 0;
        }
        else
        {
            int fin_size = serialize_packet(
                &fin_packet,
                serialized_buffer,
                sizeof(serialized_buffer)
            );

            if (fin_size < 0)
            {
                printf("Failed to serialize FIN packet\n");
                transfer_success = 0;
            }
            else
            {
                int sent = sendto(
                    sockfd,
                    (const char *)serialized_buffer,
                    fin_size,
                    0,
                    (struct sockaddr *)&receiver_addr,
                    sizeof(receiver_addr)
                );

#ifdef _WIN32
                if (sent == SOCKET_ERROR)
                {
                    printf("sendto FIN failed. "
                           "WSA error: %d\n",
                           WSAGetLastError());

                    transfer_success = 0;
                }
#else
                if (sent < 0)
                {
                    perror("sendto FIN failed");
                    transfer_success = 0;
                }
#endif
                else if (sent != fin_size)
                {
                    printf("Incomplete FIN packet sent\n");
                    transfer_success = 0;
                }
                else
                {
                    printf("Sent FIN packet %u\n",
                           sequence_number);
                }
            }
        }
    }
    else
    {
        printf("DATA transmission failed. "
               "FIN packet will not be sent.\n");
    }

    /*
     * Final status.
     */
    if (transfer_success)
    {
        printf("File transmission completed successfully.\n");
    }
    else
    {
        printf("File transmission failed.\n");
    }

    fclose(file);

#ifdef _WIN32
    closesocket(sockfd);
    WSACleanup();
#else
    close(sockfd);
#endif

    return transfer_success ? 0 : 1;
}