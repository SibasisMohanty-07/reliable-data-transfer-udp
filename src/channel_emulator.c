#include "../include/channel_emulator.h"
#include <stdlib.h>
#include <limits.h>

/* Generate a random probability between 0.0 and 1.0 */
static double channel_random_value(void)
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

/* Check whether a probability is valid */
static int channel_probability_valid(double rate)
{
    return rate >= 0.0 && rate <= 1.0;
}

/* Initialize channel configuration */
void channel_config_init(ChannelConfig *config, uint32_t seed)
{
    if (config == NULL)
    {
        return;
    }

    config->packet_loss_rate = 0.0;
    config->delay_ms = 0;
    config->jitter_ms = 0;
    config->duplication_rate = 0.0;
    config->reordering_rate = 0.0;
    config->corruption_rate = 0.0;
    config->random_seed = seed;
}

/* Initialize channel statistics */
void channel_stats_init(ChannelStats *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->packets_received = 0;
    stats->packets_dropped = 0;
    stats->packets_duplicated = 0;
    stats->packets_reordered = 0;
    stats->packets_corrupted = 0;
    stats->packets_delivered = 0;
    stats->total_delay_ms = 0;
}

/* Initialize the random-number generator */
void channel_random_init(uint32_t seed)
{
    srand((unsigned int)seed);
}

/* Check whether a packet should be dropped */
int channel_should_drop(const ChannelConfig *config)
{
    if (config == NULL ||
        !channel_probability_valid(config->packet_loss_rate))
    {
        return 0;
    }

    if (config->packet_loss_rate == 1.0)
    {
        return 1;
    }

    return channel_random_value() < config->packet_loss_rate;
}

/* Check whether a packet should be duplicated */
int channel_should_duplicate(const ChannelConfig *config)
{
    if (config == NULL ||
        !channel_probability_valid(config->duplication_rate))
    {
        return 0;
    }

    if (config->duplication_rate == 1.0)
    {
        return 1;
    }

    return channel_random_value() < config->duplication_rate;
}

/* Check whether a packet should be reordered */
int channel_should_reorder(const ChannelConfig *config)
{
    if (config == NULL ||
        !channel_probability_valid(config->reordering_rate))
    {
        return 0;
    }

    if (config->reordering_rate == 1.0)
    {
        return 1;
    }

    return channel_random_value() < config->reordering_rate;
}

/* Check whether a packet should be corrupted */
int channel_should_corrupt(const ChannelConfig *config)
{
    if (config == NULL ||
        !channel_probability_valid(config->corruption_rate))
    {
        return 0;
    }

    if (config->corruption_rate == 1.0)
    {
        return 1;
    }

    return channel_random_value() < config->corruption_rate;
}

/* Calculate packet delay with configurable jitter */
unsigned int channel_get_delay_ms(const ChannelConfig *config)
{
    if (config == NULL)
    {
        return 0;
    }

    unsigned int jitter = 0;

    if (config->jitter_ms > 0)
    {
        jitter = (unsigned int)(
            rand() % ((unsigned long)config->jitter_ms + 1UL)
        );
    }

    if (config->delay_ms > UINT_MAX - jitter)
    {
        return UINT_MAX;
    }

    return config->delay_ms + jitter;
}

/* Flip one random bit in serialized packet data */
int channel_corrupt_packet(uint8_t *data, size_t length)
{
    if (data == NULL || length == 0)
    {
        return -1;
    }

    size_t byte_index = (size_t)rand() % length;
    unsigned int bit_index = (unsigned int)rand() % 8U;

    data[byte_index] ^= (uint8_t)(1U << bit_index);

    return 0;
}