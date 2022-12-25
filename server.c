#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>

#define PORT 5666
#define buf_size 1024
#define MAX_SEAT 20

struct account
{
    int id;
    char name[10];
    char pass[30];
};

struct booking_info
{
    int id;
    int client_id;
    int bus_id;
    char date[10];
    int seat[MAX_SEAT];
};

struct bus_info
{
    int id;
    char name[10];
    char date[10];
    char time[10];
    int seat[MAX_SEAT];
};

int client_handler(int client_fd);
void client_login(int client_fd);
void client_register(int client_fd);

int main()
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_size;
    char buf[1024];
    int len;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("socket error");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // set the socket option to reuse the address
    int option = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind error");
        exit(1);
    }

    if (listen(server_fd, 5) == -1)
    {
        perror("listen error");
        exit(1);
    }

    printf("Server is running...\n");
    for (;;)
    {
        client_fd = accept(server_fd, (struct sockaddr *)NULL, NULL);
        if (client_fd == -1)
        {
            perror("accept error");
            exit(1);
        }
        // fork a child process to handle the client
        if (fork() == 0)
            client_handler(client_fd);
    }

    close(server_fd);

    return 0;
}

int client_handler(int client_fd)
{
    int user_choice;
    printf("Log: Client connected\n");

    while (1)
    {
        // read the user's choice from the client
        // read(client_fd, &user_choice, sizeof(user_choice));
        recv(client_fd, &user_choice, sizeof(user_choice), 0);
        user_choice = (user_choice > 0 && user_choice < 4) ? user_choice : 3;
        printf("Log: User choice: %d \n", user_choice);

        switch (user_choice)
        {
        case 1:
            printf("Log: User Login\n");
            client_login(client_fd);
            break;
        case 2:
            printf("Log: User Register\n");
            client_register(client_fd);
            break;
        case 3:
            return 0;
        }
    }

    close(client_fd);
}

void client_login(int client_fd)
{
    char username[buf_size], password[buf_size];
    struct account db_acc;
    int fd, client_id;

    recv(client_fd, &username, sizeof(username), 0);
    recv(client_fd, &password, sizeof(password), 0);

    fd = open("user_info", O_RDONLY);

    if (fd < 0)
    {
        perror("Error opening user.txt");
        return;
    }

    int valid = 0;

    while (read(fd, &db_acc, sizeof(db_acc)) > 0)
    {
        if (strcmp(db_acc.name, username) == 0 && strcmp(db_acc.pass, password) == 0)
        {
            valid = 1;
            client_id = db_acc.id;
            break;
        }
    }

    if (valid)
    {
        printf("Log: %s user logged in\n", username);
        send(client_fd, &valid, sizeof(valid), 0);
        send(client_fd, &client_id, sizeof(client_id), 0);
        booking_menu_handler(client_fd);
    }
    else
    {
        printf("Log: Login failed\n");
        valid = 0;
        send(client_fd, &valid, sizeof(valid), 0);
    }

    close(fd);
}

void client_register(int client_fd)
{

    char username[buf_size], password[buf_size];
    struct account db_acc;
    int fd;

    recv(client_fd, &username, sizeof(username), 0);
    recv(client_fd, &password, sizeof(password), 0);

    fd = open("user_info", O_RDWR);

    if (fd < 0)
    {
        perror("Error opening user.txt");
        return;
    }

    int file_pointer = lseek(fd, 0, SEEK_END);

    if (file_pointer == 0)
    { // 1st signup
        db_acc.id = 1;
        strcpy(db_acc.name, username);
        strcpy(db_acc.pass, password);

        write(fd, &db_acc, sizeof(db_acc));

        send(client_fd, &db_acc.name, sizeof(db_acc.name), 0);
    }
    else
    {
        // move and get last record
        file_pointer = lseek(fd, -1 * sizeof(struct account), SEEK_END);
        read(fd, &db_acc, sizeof(db_acc));

        db_acc.id++;
        strcpy(db_acc.name, username);
        strcpy(db_acc.pass, password);

        write(fd, &db_acc, sizeof(db_acc));

        send(client_fd, &db_acc.name, sizeof(db_acc.name), 0);
    }
    printf("Log: %s user registered\n", username);

    // retrerive all records
    // lseek(fd, 0, SEEK_SET);
    // while (read(fd, &db_acc, sizeof(db_acc)) > 0)
    // {
    //     printf("ID: %d, Name: %s, Pass: %s  \n", db_acc.id, db_acc.name, db_acc.pass);
    // }

    close(fd);
}

void booking_menu_handler(int client_fd)
{
    int user_choice;
    recv(client_fd, &user_choice, sizeof(user_choice), 0);
    user_choice = (user_choice > 0 && user_choice < 5) ? user_choice : 4;

    switch (user_choice)
    {
    case 1:
        printf("Log: book ticket");
        book_ticket(client_fd);
    case 2:
        // printf("Log: view ticket");
        // client_register(client_fd);
    case 3:
        // exit
        return 0;
    }
}

void book_ticket(int client_fd)
{
    // Use the stat function to get the size of the file
    struct stat st;
    if (stat("bus_info", &st) != 0)
    {
        perror("Error getting file size");
        return;
    }

    // Calculate the number of buses by dividing the file size by the size of the bus_info structure
    int num_buses = st.st_size / sizeof(struct bus_info);
    printf("Log:Total Bus is %d\n", num_buses);

    // send the number of buses to the client
    send(client_fd, &num_buses, sizeof(num_buses), 0);

    // get bus info
    struct bus_info db_bus;
    int bus_fd = open("bus_info", O_RDWR);
    if (bus_fd < 0)
    {
        perror("Error opening bus_info");
        return;
    }
    lseek(bus_fd, 0, SEEK_SET);

    while (read(bus_fd, &db_bus, sizeof(db_bus)) > 0)
    {
        // printf("ID: %d, Name: %s, Date: %s  ,Time: %s\n", db_bus.id, db_bus.name, db_bus.date, db_bus.time);
        send(client_fd, &db_bus, sizeof(db_bus), 0);
    }
    printf("Log: Sent bus info\n");

    // get bus id, client_id and seat number from client
    int bus_id, seat_id, client_id, book_status;
    recv(client_fd, &bus_id, sizeof(bus_id), 0);
    recv(client_fd, &seat_id, sizeof(seat_id), 0);
    recv(client_fd, &client_id, sizeof(client_id), 0);

    lseek(bus_fd, 0, SEEK_SET);
    while (read(bus_fd, &db_bus, sizeof(db_bus)) > 0)
    {
        if (db_bus.id == bus_id)
        {
            if (db_bus.seat[seat_id] == 0)
            {
                db_bus.seat[seat_id] = 1;
                // update the bus_info file
                printf("Log: Seat booked\n");
                update_booking_info(client_id, bus_id, db_bus.seat);
                book_status = 1;
            }
            else
            {
                printf("Log: Seat already booked\n");
                book_status = 0;
            }
        }
        // save the bus info
        lseek(bus_fd, -1 * sizeof(struct bus_info), SEEK_CUR);
        write(bus_fd, &db_bus, sizeof(db_bus));
    }
    // send the booking status bus_idto the client
    send(client_fd, &book_status, sizeof(int), 0);
    printf("Log: Bus info updated\n");

    close(bus_fd);
    booking_menu_handler(client_fd);
}

void update_booking_info(int client_id, int bus_id, int *seat_info)
{
    // create booking_info filebus_id
    struct booking_info db_booking;
    int booking_fd = open("booking_info", O_RDWR);
    if (booking_fd < 0)
    {
        perror("Error opening booking_info");
        return;
    }

    int file_pointer = lseek(booking_fd, 0, SEEK_END);

    if (file_pointer == 0)
    {
        db_booking.id = 1;
        lseek(booking_fd, 0, SEEK_END);
        printf("Log: Booking info created\n");
    }
    else
    {
        lseek(booking_fd, -1 * sizeof(struct booking_info), SEEK_END);
        read(booking_fd, &db_booking, sizeof(db_booking));
        db_booking.id++;
        printf("Log: Booking info updated\n");
    };
    // write the booking info to the file
    db_booking.client_id = client_id;
    db_booking.bus_id = bus_id;
    strcpy(db_booking.date, "dd");

    for (int i = 0; i < MAX_SEAT; i++)
    {
        db_booking.seat[i] = seat_info[i];
    }

    write(booking_fd, &db_booking, sizeof(db_booking));

    // reterive all records
    lseek(booking_fd, 0, SEEK_SET);
    while (read(booking_fd, &db_booking, sizeof(db_booking)) > 0)
    {
        printf("\nID: %d, Client ID: %d, Bus ID: %d, Date: %s Seat status:\n", db_booking.id, db_booking.client_id, db_booking.bus_id, db_booking.date);
        for (int i = 0; i < MAX_SEAT; i++)
        {
            printf("%d", db_booking.seat[i]);
        }
    }

    close(booking_fd);
}
// void view_ticket(int client_fd)
// {
//     // Use the stat function to get the size of the file
//     struct stat st;
//     if (stat("bus_info", &st) != 0)
//     {
//         perror("Error getting file size");
//         return;
//     }

//     // Calculate the number of buses by dividing the file size by the size of the bus_info structure
//     int num_buses = st.sYou had successfully booked a ticket.\nt_size / sizeof(struct bus_info);

//     // get bus info
//     struct bus_info db_bus;
//     int fd = open("bus_info", O_RDONLY);
//     if (fd < 0)
//     {
//         perror("Error opening bus_info");
//         return;
//     }
//     lseek(fd, 0, SEEK_SET);

//     // get client's name
//     char client_name[100];
//     recv(client_fd, client_name, sizeof(client_name), 0);

//     // search for booked tickets
//     int num_tickets = 0;
//     struct bus_info tickets[100];
//     while (read(fd, &db_bus, sizeof(db_bus)) > 0)
//     {
//         for (int i = 0; i < NUM_SEATS; i++)
//         {
//             if (db_bus.seat[i] == client_name)
//             {
//                 tickets[num_tickets] = db_bus;
//                 num_tickets++;
//             }
//         }
//     }

//     // send number of booked tickets
//     send(client_fd, &num_tickets, sizeof(num_tickets), 0);

//     // send booked tickets to client
//     for (int i = 0; i < num_tickets; i++)
//     {
//         send(client_fd, &tickets[i], sizeof(tickets[i]), 0);
//     }

//     close(fd);
// }
