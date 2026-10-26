
#ifndef CHANNEL_EMULATOR_H
#define CHANNEL_EMULATOR_H

#include <stdint.h>
#include <stddef.h>

/* Channel impairment configuration */
typedef struct
{
    double packet_loss_rate;
    unsigned int delay_ms;
    unsigned int jitter_ms;
    double duplication_rate;
    double reordering_rate;
    double corruption_rate;
    uint32_t random_seed;
} ChannelConfig;

/* Channel statistics */
typedef struct
{
    uint64_t packets_received;
    uint64_t packets_dropped;
    uint64_t packets_duplicated;
    uint64_t packets_reordered;
    uint64_t packets_corrupted;
    uint64_t packets_delivered;
    uint64_t total_delay_ms;
} ChannelStats;

/* Initialization */
void channel_config_init(ChannelConfig *config, uint32_t seed);
void channel_stats_init(ChannelStats *stats);
void channel_random_init(uint32_t seed);

/* Impairment decisions */
int channel_should_drop(const ChannelConfig *config);
int channel_should_duplicate(const ChannelConfig *config);
int channel_should_reorder(const ChannelConfig *config);
int channel_should_corrupt(const ChannelConfig *config);

/* Delay calculation */
unsigned int channel_get_delay_ms(const ChannelConfig *config);

/* Apply corruption to serialized packet bytes */
int channel_corrupt_packet(uint8_t *data, size_t length);

#endif
