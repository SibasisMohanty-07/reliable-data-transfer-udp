#include "../include/channel_emulator.h"
#include "../include/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#define SOCKET_ERROR_VALUE INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#define SOCKET_ERROR_VALUE (-1)
#endif

#define PROXY_PORT 9001
#define RECEIVER_PORT 9000
#define MAX_DATAGRAM (14 + MAX_PAYLOAD_SIZE)
#define MAX_PENDING 256
#define REORDER_EXTRA_DELAY_MS 100U
#define LOG_PATH "experiments/channel_proxy.log"

typedef struct
{
    int used;
    int direction; /* 0: sender -> receiver, 1: receiver -> sender */
    int length;
    uint8_t data[MAX_DATAGRAM];
    struct sockaddr_in destination;
    uint64_t send_at_ms;
} PendingPacket;

static PendingPacket pending[MAX_PENDING];
static FILE *log_file = NULL;

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

static void log_message(const char *message)
{
    if (log_file != NULL)
    {
        fprintf(log_file, "%llu ms: %s\n",
                (unsigned long long)now_ms(), message);
        fflush(log_file);
    }
}

static int send_datagram(socket_t sock,
                         const uint8_t *data,
                         int length,
                         const struct sockaddr_in *dest)
{
#ifdef _WIN32
    int sent = sendto(sock, (const char *)data, length, 0,
                      (const struct sockaddr *)dest,
                      (int)sizeof(*dest));
#else
    ssize_t sent = sendto(sock, data, (size_t)length, 0,
                          (const struct sockaddr *)dest,
                          (socklen_t)sizeof(*dest));
#endif

    return sent == length ? 0 : -1;
}

static int parse_rate(const char *text, double *rate)
{
    char *end = NULL;
    double value;

    if (text == NULL || rate == NULL || *text == '\0')
        return -1;

    errno = 0;
    value = strtod(text, &end);

    if (errno != 0 || end == text || *end != '\0' ||
        value < 0.0 || value > 1.0)
        return -1;

    *rate = value;
    return 0;
}

static int parse_delay(const char *text, unsigned int *delay)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || delay == NULL || *text == '\0' || *text == '-')
        return -1;

    errno = 0;
    value = strtoul(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || value > UINT_MAX)
        return -1;

    *delay = (unsigned int)value;
    return 0;
}

static int enqueue_packet(const uint8_t *data,
                          int length,
                          int direction,
                          const struct sockaddr_in *destination,
                          unsigned int delay_ms)
{
    size_t i;
    uint64_t current = now_ms();

    for (i = 0; i < MAX_PENDING; i++)
    {
        if (!pending[i].used)
        {
            pending[i].used = 1;
            pending[i].direction = direction;
            pending[i].length = length;
            memcpy(pending[i].data, data, (size_t)length);
            pending[i].destination = *destination;
            pending[i].send_at_ms = current + delay_ms;
            return 0;
        }
    }

    log_message("ERROR: pending queue full; packet could not be queued");
    return -1;
}

static void flush_ready_packets(socket_t proxy_sock,
                                socket_t upstream_sock,
                                ChannelStats *stats)
{
    uint64_t current = now_ms();
    size_t i;

    for (i = 0; i < MAX_PENDING; i++)
    {
        socket_t output_sock;

        if (!pending[i].used || pending[i].send_at_ms > current)
            continue;

        output_sock = pending[i].direction == 0
                    ? upstream_sock : proxy_sock;

        if (send_datagram(output_sock,
                          pending[i].data,
                          pending[i].length,
                          &pending[i].destination) == 0)
        {
            stats->packets_delivered++;
            printf("Forwarded %s\n",
                   pending[i].direction == 0
                   ? "sender -> receiver" : "receiver -> sender");
        }
        else
        {
            fprintf(stderr, "Packet forwarding failed\n");
            log_message("ERROR: packet forwarding failed");
        }

        pending[i].used = 0;
    }
}

int main(int argc, char *argv[])
{
    ChannelConfig config;
    ChannelStats stats;
    struct sockaddr_in proxy_addr;
    struct sockaddr_in receiver_addr;
    struct sockaddr_in sender_addr;
    int have_sender = 0;
    socket_t proxy_sock = SOCKET_ERROR_VALUE;
    socket_t upstream_sock = SOCKET_ERROR_VALUE;
    int exit_code = 1;

    /*
       Usage:
       ./channel_proxy.exe [loss] [delay_ms] [duplication]
                           [corruption] [reordering] [jitter_ms]

       Rates are decimals from 0.0 to 1.0.
       A selected packet gets an additional 100 ms delay to allow a
       later packet to overtake it.
    */
    if (argc > 7)
    {
        fprintf(stderr,
                "Usage: %s [loss] [delay_ms] [duplication] "
                "[corruption] [reordering] [jitter_ms]\n",
                argv[0]);
        return 1;
    }

    channel_config_init(&config, 42);
    channel_stats_init(&stats);
    memset(pending, 0, sizeof(pending));

    if (argc > 1 &&
        parse_rate(argv[1], &config.packet_loss_rate) != 0)
    {
        fprintf(stderr, "Invalid loss rate: use 0.0 to 1.0\n");
        return 1;
    }

    if (argc > 2 && parse_delay(argv[2], &config.delay_ms) != 0)
    {
        fprintf(stderr, "Invalid delay: use a non-negative integer\n");
        return 1;
    }

    if (argc > 3 &&
        parse_rate(argv[3], &config.duplication_rate) != 0)
    {
        fprintf(stderr, "Invalid duplication rate: use 0.0 to 1.0\n");
        return 1;
    }

    if (argc > 4 &&
        parse_rate(argv[4], &config.corruption_rate) != 0)
    {
        fprintf(stderr, "Invalid corruption rate: use 0.0 to 1.0\n");
        return 1;
    }

    if (argc > 5 &&
        parse_rate(argv[5], &config.reordering_rate) != 0)
    {
        fprintf(stderr, "Invalid reordering rate: use 0.0 to 1.0\n");
        return 1;
    }

    if (argc > 6 && parse_delay(argv[6], &config.jitter_ms) != 0)
    {
        fprintf(stderr, "Invalid jitter: use a non-negative integer\n");
        return 1;
    }

#ifdef _WIN32
    {
        WSADATA wsa;

        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            fprintf(stderr, "Winsock initialization failed\n");
            return 1;
        }
    }
#endif

    /*
       Create experiments/ before running the proxy.
       If the directory is missing, logging is disabled but forwarding
       can still proceed.
    */
    log_file = fopen(LOG_PATH, "a");
    if (log_file == NULL)
    {
        fprintf(stderr,
                "Warning: cannot open %s; continuing without file logging\n",
                LOG_PATH);
    }

    channel_random_init(config.random_seed);

    proxy_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (proxy_sock == SOCKET_ERROR_VALUE)
    {
        fprintf(stderr, "Proxy socket creation failed\n");
        goto cleanup;
    }

    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    proxy_addr.sin_port = htons(PROXY_PORT);

    if (bind(proxy_sock, (struct sockaddr *)&proxy_addr,
#ifdef _WIN32
             (int)sizeof(proxy_addr)
#else
             (socklen_t)sizeof(proxy_addr)
#endif
             ) < 0)
    {
        fprintf(stderr, "Could not bind proxy to port %d\n", PROXY_PORT);
        goto cleanup;
    }

    upstream_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (upstream_sock == SOCKET_ERROR_VALUE)
    {
        fprintf(stderr, "Upstream socket creation failed\n");
        goto cleanup;
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(RECEIVER_PORT);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &receiver_addr.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid receiver address\n");
        goto cleanup;
    }

    printf("Channel proxy listening on port %d\n", PROXY_PORT);
    printf("Receiver address: 127.0.0.1:%d\n", RECEIVER_PORT);
    printf("Loss=%.2f Delay=%u ms Duplication=%.2f Corruption=%.2f "
           "Reordering=%.2f Jitter=%u ms\n",
           config.packet_loss_rate,
           config.delay_ms,
           config.duplication_rate,
           config.corruption_rate,
           config.reordering_rate,
           config.jitter_ms);

    if (log_file != NULL)
    {
        fprintf(log_file,
                "\n--- Proxy started: loss=%.3f delay_ms=%u "
                "duplication=%.3f corruption=%.3f "
                "reordering=%.3f jitter_ms=%u ---\n",
                config.packet_loss_rate,
                config.delay_ms,
                config.duplication_rate,
                config.corruption_rate,
                config.reordering_rate,
                config.jitter_ms);
        fflush(log_file);
    }

    for (;;)
    {
        fd_set readfds;
        struct timeval timeout;
        int ready;

        /* Send any queued packets whose scheduled delivery time has arrived. */
        flush_ready_packets(proxy_sock, upstream_sock, &stats);

        FD_ZERO(&readfds);
        FD_SET(proxy_sock, &readfds);
        FD_SET(upstream_sock, &readfds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; /* 10 ms */

#ifdef _WIN32
        ready = select(0, &readfds, NULL, NULL, &timeout);
#else
        {
            int max_fd = proxy_sock > upstream_sock
                       ? (int)proxy_sock : (int)upstream_sock;
            ready = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
        }
#endif

        if (ready < 0)
        {
            fprintf(stderr, "select() failed; stopping proxy\n");
            log_message("ERROR: select() failed; stopping proxy");
            exit_code = 1;
            break;
        }

        if (ready == 0)
            continue;

        for (int direction = 0; direction < 2; direction++)
        {
            socket_t input_sock = direction == 0
                                ? proxy_sock : upstream_sock;
            uint8_t data[MAX_DATAGRAM];
            struct sockaddr_in source;
#ifdef _WIN32
            int source_len = (int)sizeof(source);
#else
            socklen_t source_len = (socklen_t)sizeof(source);
#endif
            int length;
            const struct sockaddr_in *destination;
            unsigned int delay;
            int reorder;

            if (!FD_ISSET(input_sock, &readfds))
                continue;

            length = recvfrom(input_sock, (char *)data, (int)sizeof(data), 0,
                              (struct sockaddr *)&source, &source_len);

            if (length < 0)
            {
#ifdef _WIN32
                int error_code = WSAGetLastError();

                /*
                 * WSAECONNRESET can be reported for an unconnected UDP
                 * socket after an ICMP port-unreachable response. For
                 * example, the receiver may have been stopped. Log it
                 * and continue listening instead of terminating the proxy.
                 */
                if (error_code == WSAEINTR)
                    continue;

                if (error_code == WSAECONNRESET)
                {
                    log_message(
                        "UDP receive reported connection reset; "
                        "check that the receiver is running");
                    continue;
                }

                fprintf(stderr,
                        "recvfrom() failed; Winsock error: %d\n",
                        error_code);

                if (log_file != NULL)
                {
                    fprintf(log_file,
                            "%llu ms: ERROR: recvfrom() failed; "
                            "Winsock error: %d\n",
                            (unsigned long long)now_ms(), error_code);
                    fflush(log_file);
                }
#else
                if (errno == EINTR)
                    continue;

                fprintf(stderr, "recvfrom() failed: %s\n", strerror(errno));

                if (log_file != NULL)
                {
                    fprintf(log_file,
                            "%llu ms: ERROR: recvfrom() failed: %s\n",
                            (unsigned long long)now_ms(), strerror(errno));
                    fflush(log_file);
                }
#endif
                continue;
            }

            if (length == 0)
                continue;

            /*
               Direction 0: sender -> receiver
               Direction 1: receiver -> sender
            */
            if (direction == 0)
            {
                sender_addr = source;
                have_sender = 1;
            }
            else if (!have_sender)
            {
                continue;
            }

            stats.packets_received++;

            if (channel_should_drop(&config))
            {
                stats.packets_dropped++;
                printf("Dropped packet (%s)\n",
                       direction == 0
                       ? "sender -> receiver" : "receiver -> sender");
                log_message("Packet dropped");
                continue;
            }

            if (channel_should_corrupt(&config) &&
                channel_corrupt_packet(data, (size_t)length) == 0)
            {
                stats.packets_corrupted++;
                printf("Corrupted packet\n");
                log_message("Packet corrupted");
            }

            destination = direction == 0 ? &receiver_addr : &sender_addr;
            delay = channel_get_delay_ms(&config);
            reorder = channel_should_reorder(&config);

            if (reorder)
            {
                if (delay <= UINT_MAX - REORDER_EXTRA_DELAY_MS)
                    delay += REORDER_EXTRA_DELAY_MS;

                stats.packets_reordered++;
                printf("Reordering delay applied (+%u ms)\n",
                       REORDER_EXTRA_DELAY_MS);
                log_message("Reordering delay applied");
            }

            if (enqueue_packet(data, length, direction,
                               destination, delay) != 0)
            {
                fprintf(stderr, "Could not queue packet; treating as dropped\n");
                stats.packets_dropped++;
                continue;
            }

            if (delay > 0)
            {
                stats.total_delay_ms += delay;
                printf("Queued packet with %u ms delay\n", delay);
            }

            if (channel_should_duplicate(&config))
            {
                if (enqueue_packet(data, length, direction,
                                   destination, delay) == 0)
                {
                    stats.packets_duplicated++;
                    printf("Duplicated packet\n");
                    log_message("Packet duplicated");
                }
            }

            printf("Received=%llu Dropped=%llu Delivered=%llu "
                   "Duplicated=%llu Reordered=%llu Corrupted=%llu\n",
                   (unsigned long long)stats.packets_received,
                   (unsigned long long)stats.packets_dropped,
                   (unsigned long long)stats.packets_delivered,
                   (unsigned long long)stats.packets_duplicated,
                   (unsigned long long)stats.packets_reordered,
                   (unsigned long long)stats.packets_corrupted);
        }
    }

    cleanup:


    if (log_file != NULL)
    {
        fprintf(log_file,
                "Final stats: received=%llu dropped=%llu delivered=%llu "
                "duplicated=%llu reordered=%llu corrupted=%llu "
                "total_queued_delay_ms=%llu\n",
                (unsigned long long)stats.packets_received,
                (unsigned long long)stats.packets_dropped,
                (unsigned long long)stats.packets_delivered,
                (unsigned long long)stats.packets_duplicated,
                (unsigned long long)stats.packets_reordered,
                (unsigned long long)stats.packets_corrupted,
                (unsigned long long)stats.total_delay_ms);
        fflush(log_file);
        fclose(log_file);
    }

    if (upstream_sock != SOCKET_ERROR_VALUE)
        CLOSE_SOCKET(upstream_sock);

    if (proxy_sock != SOCKET_ERROR_VALUE)
        CLOSE_SOCKET(proxy_sock);

#ifdef _WIN32
    WSACleanup();
#endif

    return exit_code;
}
