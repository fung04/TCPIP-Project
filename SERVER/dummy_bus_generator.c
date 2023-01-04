#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

// Define the number of buses
#define NUM_BUSES 5
#define MAX_SEAT 20

struct bus_info
{
    int id;
    char name[10];
    char date[10];
    char time[10];
    int seat[MAX_SEAT];
};

int main()
{
    // Create an array of NUM_BUSES bus_info structures
    struct bus_info buses[NUM_BUSES];

    // Use a loop to fill in the values for each element of the array
    for (int i = 0; i < NUM_BUSES; i++)
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
    if (write(fd, buses, sizeof(struct bus_info) * NUM_BUSES) < 0)
    {
        perror("Error writing to file");
        return 1;
    }
    else
    {
        printf("File written successfully\n");
    }


    return 0;
}


