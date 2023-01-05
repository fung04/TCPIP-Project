#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_SEAT 20
#define QUEUE_KEY 1234

struct bus_info
{
    int id;
    char name[10];
    char date[10];
    char time[10];
    int seat[MAX_SEAT];
};

struct bus_message_q
{
    long type;
    int num_buses;
};

int main()
{
    // create a message queue
    int queue_id = msgget(QUEUE_KEY, 0666);
    if (queue_id < 0)
    {
        perror("Error getting message queue");
        return 1;
    }

    // receive the message
    struct bus_message_q msg;
    if (msgrcv(queue_id, &msg, sizeof(struct bus_message_q), 1, 0) < 0)
    {
        perror("Error receiving message");
        return 1;
    }
    int num_buses = msg.num_buses;

    // Create an array of bus_info structures
    struct bus_info buses[num_buses];


    // Use a loop to fill in the values for each element of the array
    for (int i = 0; i < num_buses; i++)
    {
        buses[i].id = i + 1;
        sprintf(buses[i].name, "Bus %d", i + 1);
        sprintf(buses[i].date, "01/01/2022");
        sprintf(buses[i].time, "12:00");

        // Fill in the seat array with default values
        for (int j = 0; j < MAX_SEAT; j++)
        {
            buses[i].seat[j] = 0;
        }

        buses[i].seat[9] = 1;
    }

    // save the data to a file
        // Open the file for writing
    int fd = open("bus_info", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        perror("Error opening file");
        return 1;
    }

    // Write the array of buses to the file
    if (write(fd, buses, sizeof(struct bus_info) * num_buses) < 0)
    {
        perror("Error writing to file");
        return 1;
    }
    else
    {
        printf("Bus info reset successfully\n");
    }

    // to destroy the message queue
    msgctl(queue_id, IPC_RMID, NULL);
    execl("./server",NULL);
    return 0;
}


