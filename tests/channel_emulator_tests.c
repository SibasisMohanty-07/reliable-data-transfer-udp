
#include <stdio.h>
#include "../include/channel_emulator.h"

int main(void)
{
    ChannelConfig config;
    int dropped = 0;
    int total_packets = 10000;

    channel_config_init(&config, 42);
    channel_random_init(config.random_seed);

    /* Test 1: No packet loss */
    config.packet_loss_rate = 0.0;

    for (int i = 0; i < 100; i++)
    {
        if (channel_should_drop(&config))
        {
            printf("FAIL: Packet dropped with 0%% loss\n");
            return 1;
        }
    }

    printf("PASS: No packets dropped at 0%% loss\n");

    /* Test 2: 100%% packet loss */
    config.packet_loss_rate = 1.0;

    for (int i = 0; i < 100; i++)
    {
        if (!channel_should_drop(&config))
        {
            printf("FAIL: Packet not dropped with 100%% loss\n");
            return 1;
        }
    }

    printf("PASS: All packets dropped at 100%% loss\n");

    /* Test 3: Approximately 10%% packet loss */
    config.packet_loss_rate = 0.10;
    channel_random_init(config.random_seed);

    for (int i = 0; i < total_packets; i++)
    {
        if (channel_should_drop(&config))
        {
            dropped++;
        }
    }

    printf("Packets tested: %d\n", total_packets);
    printf("Packets dropped: %d\n", dropped);
    printf("Observed loss rate: %.2f%%\n",
           (double)dropped * 100.0 / total_packets);

    if (dropped > 0 && dropped < total_packets)
    {
        printf("PASS: Random packet-loss test completed\n");
    }
    else
    {
        printf("FAIL: Unexpected result for 10%% loss\n");
        return 1;
    }

    return 0;
}
