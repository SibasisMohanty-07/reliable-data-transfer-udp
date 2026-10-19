
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../include/channel_emulator.h"

int main(void)
{
    ChannelConfig config;
    uint8_t packet[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t original[sizeof(packet)];

    channel_config_init(&config, 42);
    channel_random_init(config.random_seed);

    /* Test 1: No duplication at 0% */
    config.duplication_rate = 0.0;
    if (channel_should_duplicate(&config))
    {
        printf("FAIL: Duplication at 0%%\n");
        return 1;
    }
    printf("PASS: Duplication disabled at 0%%\n");

    /* Test 2: Always duplicate at 100% */
    config.duplication_rate = 1.0;
    if (!channel_should_duplicate(&config))
    {
        printf("FAIL: Duplication at 100%%\n");
        return 1;
    }
    printf("PASS: Duplication enabled at 100%%\n");

    /* Test 3: Reordering decision */
    config.reordering_rate = 0.0;
    if (channel_should_reorder(&config))
    {
        printf("FAIL: Reordering at 0%%\n");
        return 1;
    }

    config.reordering_rate = 1.0;
    if (!channel_should_reorder(&config))
    {
        printf("FAIL: Reordering at 100%%\n");
        return 1;
    }
    printf("PASS: Reordering decisions\n");

    /* Test 4: Corruption decision */
    config.corruption_rate = 0.0;
    if (channel_should_corrupt(&config))
    {
        printf("FAIL: Corruption at 0%%\n");
        return 1;
    }

    config.corruption_rate = 1.0;
    if (!channel_should_corrupt(&config))
    {
        printf("FAIL: Corruption at 100%%\n");
        return 1;
    }
    printf("PASS: Corruption decisions\n");

    /* Test 5: Corrupt packet bytes */
    memcpy(original, packet, sizeof(packet));

    if (channel_corrupt_packet(packet, sizeof(packet)) != 0)
    {
        printf("FAIL: Could not corrupt packet\n");
        return 1;
    }

    if (memcmp(original, packet, sizeof(packet)) == 0)
    {
        printf("FAIL: Packet bytes did not change\n");
        return 1;
    }
    printf("PASS: Packet bytes changed\n");

    /* Test 6: Delay calculation */
    config.delay_ms = 100;
    config.jitter_ms = 0;

    if (channel_get_delay_ms(&config) != 100)
    {
        printf("FAIL: Incorrect fixed delay\n");
        return 1;
    }
    printf("PASS: Fixed delay calculation\n");

    printf("\nAll channel-emulator feature tests passed.\n");
    return 0;
}
