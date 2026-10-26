
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
#define SERVER_PORT 9001
#define RECEIVE_TIMEOUT_MS 100

#define INITIAL_RTO_MS 1000.0
#define MIN_RTO_MS 200.0
#define MAX_RTO_MS 60000.0

typedef struct
{
    int initialized;
    double srtt_ms;
    double rttvar_ms;
    double rto_ms;
} RttEstimator;

/* Millisecond monotonic clock for retransmission deadlines. */
static uint64_t now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000ULL +
           (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

/* High-resolution monotonic clock for RTT measurements. */
static uint64_t now_us(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;

    if (!QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&counter))
        return 0;

    return (uint64_t)(
        ((long double)counter.QuadPart * 1000000.0L) /
        (long double)frequency.QuadPart);
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000000ULL +
           (uint64_t)ts.tv_nsec / 1000ULL;
#endif
}

static void rtt_init(RttEstimator *est)
{
    est->initialized = 0;
    est->srtt_ms = 0.0;
    est->rttvar_ms = 0.0;
    est->rto_ms = INITIAL_RTO_MS;
}

/* Update the adaptive retransmission timeout from a valid RTT sample. */
static void rtt_update(RttEstimator *est, double sample_ms)
{
    if (sample_ms <= 0.0)
        return;

    if (!est->initialized)
    {
        est->srtt_ms = sample_ms;
        est->rttvar_ms = sample_ms / 2.0;
        est->initialized = 1;
    }
    else
    {
        /* RTTVAR must use the old SRTT. */
        double error = est->srtt_ms - sample_ms;

        if (error < 0.0)
            error = -error;

        est->rttvar_ms =
            0.75 * est->rttvar_ms + 0.25 * error;

        est->srtt_ms =
            0.875 * est->srtt_ms + 0.125 * sample_ms;
    }

    est->rto_ms =
        est->srtt_ms + 4.0 * est->rttvar_ms;

    if (est->rto_ms < MIN_RTO_MS)
        est->rto_ms = MIN_RTO_MS;

    if (est->rto_ms > MAX_RTO_MS)
        est->rto_ms = MAX_RTO_MS;

    printf("RTT sample: %.3f ms | SRTT: %.3f ms | "
           "RTTVAR: %.3f ms | RTO: %.3f ms\n",
           sample_ms, est->srtt_ms,
           est->rttvar_ms, est->rto_ms);
}

static int send_packet(socket_t sock,
                       const struct sockaddr_in *server,
                       const Packet *packet)
{
    uint8_t buffer[14 + MAX_PAYLOAD_SIZE];

    int length = serialize_packet(
        packet, buffer, sizeof(buffer));

    if (length < 0)
    {
        fprintf(stderr, "Packet serialization failed\n");
        return -1;
    }

    int sent = sendto(
        sock,
        (const char *)buffer,
        length,
        0,
        (const struct sockaddr *)server,
        sizeof(*server));

    if (sent < 0)
    {
#ifdef _WIN32
        fprintf(stderr, "sendto() failed: %d\n",
                WSAGetLastError());
#else
        perror("sendto");
#endif
        return -1;
    }

    if (sent != length)
    {
        fprintf(stderr, "Incomplete UDP datagram send\n");
        return -1;
    }

    return 0;
}

static int send_acknowledged_data(
    socket_t sock,
    const struct sockaddr_in *server,
    FILE *input,
    uint32_t *total_packets)
{
    SRSlot slots[SR_WINDOW_SIZE] = {0};

    /*
     * first_sent_us stores the first successful transmission time.
     * It is never overwritten by retransmissions.
     *
     * retransmitted prevents RTT sampling when an ACK's timing
     * is ambiguous, implementing Karn's algorithm.
     */
    uint64_t first_sent_us[SR_WINDOW_SIZE] = {0};
    int retransmitted[SR_WINDOW_SIZE] = {0};

    uint32_t base = 0;
    uint32_t next_seq = 0;
    int eof = 0;

    RttEstimator estimator;
    rtt_init(&estimator);

    while (!eof || base < next_seq)
    {
        /* Fill the Selective Repeat sending window. */
        while (!eof && next_seq < base + SR_WINDOW_SIZE)
        {
            char payload[MAX_PAYLOAD_SIZE];

            size_t n = fread(
                payload, 1, sizeof(payload), input);

            if (n == 0)
            {
                if (ferror(input))
                {
                    perror("Failed to read input file");
                    return -1;
                }

                eof = 1;
                break;
            }

            uint32_t index = next_seq % SR_WINDOW_SIZE;
            SRSlot *slot = &slots[index];

            memset(slot, 0, sizeof(*slot));
            first_sent_us[index] = 0;
            retransmitted[index] = 0;

            if (create_data_packet(
                    &slot->packet,
                    next_seq,
                    payload,
                    (uint16_t)n) != 0)
            {
                fprintf(stderr,
                        "Failed to create DATA seq=%u\n",
                        next_seq);
                return -1;
            }

            slot->valid = true;
            slot->acknowledged = false;
            slot->retries = 0;

            /*
             * Send first, then record the successful-send timestamp.
             * Use the same monotonic clock for timeout comparisons.
             */
            if (send_packet(sock, server, &slot->packet) != 0)
            {
                fprintf(stderr,
                        "Failed to send DATA seq=%u\n",
                        next_seq);
                return -1;
            }

            uint64_t sent_us = now_us();
            uint64_t sent_ms = now_ms();

            if (sent_us == 0 || sent_ms == 0)
            {
                fprintf(stderr, "Monotonic clock failed\n");
                return -1;
            }

            first_sent_us[index] = sent_us;
            slot->last_sent_ms = sent_ms;

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

#ifdef _WIN32
        int ready = select(0, &readfds, NULL, NULL, &tv);
#else
        int ready = select(sock + 1, &readfds, NULL, NULL, &tv);
#endif

        if (ready < 0)
        {
#ifdef _WIN32
            int error_code = WSAGetLastError();

            if (error_code == WSAEINTR)
                continue;

            fprintf(stderr, "select() failed: %d\n",
                    error_code);
#else
            if (errno == EINTR)
                continue;

            perror("select");
#endif
            return -1;
        }

        if (ready > 0 && FD_ISSET(sock, &readfds))
        {
            uint8_t buffer[14 + MAX_PAYLOAD_SIZE];
            struct sockaddr_in from;

#ifdef _WIN32
            int from_len = (int)sizeof(from);
#else
            socklen_t from_len = (socklen_t)sizeof(from);
#endif

            int received = recvfrom(
                sock,
                (char *)buffer,
                sizeof(buffer),
                0,
                (struct sockaddr *)&from,
                &from_len);

            if (received < 0)
            {
#ifdef _WIN32
                int error_code = WSAGetLastError();

                if (error_code == WSAEINTR)
                    continue;

                fprintf(stderr, "recvfrom() failed: %d\n",
                        error_code);
#else
                if (errno == EINTR)
                    continue;

                perror("recvfrom");
#endif
                continue;
            }

            Packet ack;

            if (received > 0 &&
                parse_packet(
                    buffer, (size_t)received, &ack) == 0 &&
                verify_checksum(&ack) &&
                ack.type == PACKET_ACK)
            {
                uint32_t seq = ack.acknowledgement_number;

                if (seq >= base && seq < next_seq)
                {
                    uint32_t index = seq % SR_WINDOW_SIZE;
                    SRSlot *slot = &slots[index];

                    if (slot->valid &&
                        slot->packet.sequence_number == seq &&
                        !slot->acknowledged)
                    {
                        uint64_t ack_us = now_us();

                        /*
                         * Karn's algorithm:
                         * only measure RTT for a packet that was
                         * transmitted once and has valid timestamps.
                         */
                        if (slot->retries == 0 &&
                            !retransmitted[index] &&
                            first_sent_us[index] != 0 &&
                            ack_us >= first_sent_us[index])
                        {
                            double sample_ms =
                                (double)(ack_us -
                                         first_sent_us[index]) /
                                1000.0;

                            rtt_update(&estimator, sample_ms);
                        }
                        else
                        {
                            printf(
                                "Ignoring RTT sample for seq=%u "
                                "(retransmitted or invalid timing)\n",
                                seq);
                        }

                        slot->acknowledged = true;

                        printf("Received individual ACK for seq=%u\n",
                               seq);
                    }
                }

                /*
                 * Advance the sending-window base only over
                 * consecutive acknowledged packets.
                 */
                while (base < next_seq)
                {
                    uint32_t index = base % SR_WINDOW_SIZE;
                    SRSlot *first = &slots[index];

                    if (!first->valid ||
                        first->packet.sequence_number != base ||
                        !first->acknowledged)
                    {
                        break;
                    }

                    first->valid = false;
                    base++;
                }
            }
        }

        uint64_t now = now_ms();

        if (now == 0)
        {
            fprintf(stderr, "Monotonic clock failed\n");
            return -1;
        }

        int timeout_occurred = 0;

        /*
         * Selectively retransmit only expired, unacknowledged packets.
         */
        for (uint32_t seq = base; seq < next_seq; seq++)
        {
            uint32_t index = seq % SR_WINDOW_SIZE;
            SRSlot *slot = &slots[index];

            if (!slot->valid || slot->acknowledged)
                continue;

            /* Avoid unsigned subtraction if the clock is invalid. */
            if (now < slot->last_sent_ms)
                continue;

            double elapsed_ms =
                (double)(now - slot->last_sent_ms);

            if (elapsed_ms >= estimator.rto_ms)
            {
                if (slot->retries >= SR_MAX_RETRIES)
                {
                    fprintf(stderr,
                            "Maximum retries reached for seq=%u\n",
                            seq);
                    return -1;
                }

                if (send_packet(sock, server, &slot->packet) != 0)
                {
                    fprintf(stderr,
                            "Failed to retransmit DATA seq=%u\n",
                            seq);
                    return -1;
                }

                /*
                 * The packet is now ambiguous for RTT measurement.
                 * Keep first_sent_us unchanged for diagnostics, but
                 * never use it for an RTT sample after retransmission.
                 */
                retransmitted[index] = 1;
                slot->retries++;

                uint64_t retransmit_ms = now_ms();

                if (retransmit_ms == 0)
                {
                    fprintf(stderr, "Monotonic clock failed\n");
                    return -1;
                }

                slot->last_sent_ms = retransmit_ms;
                timeout_occurred = 1;

                printf("Timeout: retransmitting seq=%u "
                       "(retry %u)\n",
                       seq, slot->retries);
            }
        }

        /*
         * Exponential backoff after a timeout.
         * A later eligible RTT sample recalculates the RTO.
         */
        if (timeout_occurred)
        {
            estimator.rto_ms *= 2.0;

            if (estimator.rto_ms > MAX_RTO_MS)
                estimator.rto_ms = MAX_RTO_MS;

            printf("Timeout backoff: new RTO = %.3f ms\n",
                   estimator.rto_ms);
        }
    }

    *total_packets = next_seq;

    printf("\nRTT estimation summary:\n");

    if (estimator.initialized)
    {
        printf("Final SRTT: %.3f ms\n", estimator.srtt_ms);
        printf("Final RTTVAR: %.3f ms\n", estimator.rttvar_ms);
    }
    else
    {
        printf("No eligible RTT samples were collected.\n");
    }

    printf("Final RTO: %.3f ms\n", estimator.rto_ms);

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

    if (input == NULL)
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
        fprintf(stderr, "Socket creation failed\n");
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
    printf("Sending through channel proxy at %s:%d\n",
           SERVER_IP, SERVER_PORT);

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
