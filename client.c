#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#define PORT 5666
#define MAX_SEAT 20
#define buf_size 1024

struct bus_info
{
    int id;
    char name[10];
    char date[10];
    char time[10];
    int seat[MAX_SEAT];
};

int booking_system(int client_fd);

int main(int argc, char **argv)
{
    int client_fd;
    struct sockaddr_in server_addr;

    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1)
    {
        perror("socket error");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("connect error");
        exit(1);
    }

    // keep running a booking_system() function
    printf("Connection established.\n");
    while (1)
    {
        booking_system(client_fd);
    }

    close(client_fd);

    return 0;
}

int booking_system(int client_fd)
{
    int len;
    int user_choice;
    char buf[buf_size];

    printf("\n\t\t\t******WELCOME TO BUS BOOKING SYSTEM******\n");
    printf("Please choose one of the following options:\n");
    printf("1. Log In\n");
    printf("2. Signup\n");
    printf("3. Exit\n");

    // get the user's choice
    printf("Your choice: ");
    scanf("%d", &user_choice);
    user_choice = (user_choice > 0 && user_choice < 4) ? user_choice : 3;
    send(client_fd, &user_choice, sizeof(user_choice), 0);

    switch (user_choice)
    {
    case 1:
        int valid = 0;

        printf("\nPlease enter your username: ");
        scanf("%s", buf);
        send(client_fd, &buf, sizeof(buf), 0);
        memset(buf, 0, sizeof(buf));

        printf("Please enter your password: ");
        scanf("%s", buf);
        send(client_fd, &buf, sizeof(buf), 0);
        memset(buf, 0, sizeof(buf));

        // Read response from server
        recv(client_fd, &valid, sizeof(valid), 0);

        if (valid == 1)
        {
            system("clear");
            printf("You had successfully logged in.\n");
            booking_menu(client_fd);
        }
        else
        {
            printf("Invalid username or password.\n");
        }
        break;
    case 2:
        char name[buf_size];
        printf("\nPlease enter your username: ");
        scanf("%s", buf);
        send(client_fd, &buf, sizeof(buf), 0);
        memset(buf, 0, sizeof(buf));

        printf("Please enter your password: ");
        scanf("%s", buf);
        send(client_fd, &buf, sizeof(buf), 0);
        memset(buf, 0, sizeof(buf));

        // Read response from server
        recv(client_fd, &name, sizeof(name), 0);
        printf("Hi %s, you had successfully registered\n", name);

        break;
    case 3:
        printf("Thank you for using our system.\n");
        exit(0);
        break;
    default:
        printf("Invalid choice.\n");
        break;
    }
}

void booking_menu(int client_fd)
{
    int user_choice;
    printf("\n\t\t\t******Booking Menu******\n");
    printf("Please choose one of the following options:\n");
    printf("1. Book a ticket\n");
    printf("2. View all tickets\n");
    printf("3. Log out\n");

    // get the user's choice
    printf("Your choice: ");
    scanf("%d", &user_choice);
    user_choice = (user_choice > 0 && user_choice < 5) ? user_choice : 4;

    send(client_fd, &user_choice, sizeof(user_choice), 0);

    switch (user_choice)
    {
    case 1:
        book_ticket(client_fd);
        break;
    case 2:
        // cancel_ticket(client_fd);
        break;
    case 3:
        // view_tickets(client_fd);
        break;
    case 4:
        printf("You have successfully logged out.\n");
        break;
    default:
        printf("Invalid choice.\n");
        break;
    }
}

struct bus_info *get_bus_info(int client_fd, int num_bus) {
    // allocate memory for the bus list
    struct bus_info *db_bus_list = malloc(num_bus * sizeof(struct bus_info));

    // receive the bus information from the server
    int num_buses = 0;
    struct bus_info db_bus;
    while (num_buses < num_bus && recv(client_fd, &db_bus, sizeof(db_bus), 0) > 0)
    {
        db_bus_list[num_buses] = db_bus;
        num_buses++;
    }

    return db_bus_list;
}

void bus_seat_plotter(int seats[])
{
    printf("Current seat status:\n");

    for (int i = 0; i < MAX_SEAT; i++)
    {
        if (i % 2 == 0)
        {
            printf("\n"); // Start a new line every two seats
        }

        printf("%c%d ", 'A' + (i / 2), i % 2 + 1); // Print the column and row of the seat
        if (seats[i] == 0)
        {
            printf("[ ]  "); // Print an empty square for an available seat
        }
        else
        {
            printf("[X]  "); // Print an X for an occupied seat
        }
    }
    printf("\n");

}

int seat_converter(char *seat_id) {
    // Initialize a string with the label for a seat
    char *label = "B1";

    // Parse the column and row number from the label
    char column = seat_id[0];
    int row = seat_id[1] - '0';

    // Calculate the corresponding array index
    int index = (column - 'A') * 2 + (row - 1);

    return index;
}


void book_ticket(int client_fd)
{
    
    int num_bus, bus_id, seat_id; 
    char seat_label[2];
    // receive bus num from server
    recv(client_fd, &num_bus, sizeof(num_bus),0);
    
    printf("Number of bus: %d\n", num_bus);
    struct bus_info *db_bus_list = get_bus_info(client_fd, num_bus);

    for (int i = 0; i < num_bus; i++)
    {
        printf("ID: %d, Name: %s, Date: %s  ,Time: %s\n", db_bus_list[i].id, db_bus_list[i].name, db_bus_list[i].date, db_bus_list[i].time);
    }

    printf("Please select a bus: \n");
    scanf("%d", &bus_id);

    bus_seat_plotter(db_bus_list[bus_id].seat);

    printf("Please select a seat [A1]: \n");
    scanf("%s", &seat_label);
    seat_id = seat_converter(seat_label);

    // send the bus id and seat id to the server
    send(client_fd, &bus_id, sizeof(bus_id), 0);
    send(client_fd, &seat_id, sizeof(seat_id), 0);

    // get book status from server
    int book_status;
    recv(client_fd, &book_status, sizeof(book_status), 0);

    if(book_status == 1) {
        printf("You had successfully booked a ticket.\n");
    } else {
        printf("Sorry, the seat is already booked.\n");
    }

}