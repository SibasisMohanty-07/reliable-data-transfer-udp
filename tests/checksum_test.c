#include <stdio.h>
#include "../include/protocol.h"
#include "../src/packet.h"

int main()
{
    Packet packet;

    create_data_packet(&packet, 0, "Hello UDP", 9);

    printf("Checksum Test\n");
    printf("--------------\n");

    printf("Original checksum : %u\n", packet.checksum);

    if (verify_checksum(&packet))
    {
        printf("CHECKSUM VERIFIED\n");
    }
    else
    {
        printf("CHECKSUM FAILED\n");
    }

    /* Modify data to test corruption detection */
    packet.payload[0] = 'X';

    if (verify_checksum(&packet))
    {
        printf("ERROR: Corruption was not detected\n");
    }
    else
    {
        printf("CORRUPTION DETECTED SUCCESSFULLY\n");
    }

    return 0;
}