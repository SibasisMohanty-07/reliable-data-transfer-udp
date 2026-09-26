 #include <stdio.h>

int main()
{
    int packet = 0;
    int ack;

    printf("Stop-and-Wait ARQ Test\n\n");

    while (packet < 3)
    {
        printf("SEND Packet %d\n", packet);
        printf("WAIT for ACK %d\n", packet);

        printf("Enter ACK number: ");
        scanf("%d", &ack);

        if (ack == packet)
        {
            printf("RECEIVE ACK %d\n", ack);
            printf("Packet %d delivered successfully.\n\n", packet);

            packet++;
        }
        else
        {
            printf("Wrong ACK!\n");
            printf("Retransmitting Packet %d...\n\n", packet);
        }
    }

    printf("All packets transmitted successfully.\n");

    return 0;
}