#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include "sem.h"

#define PORT 5666
#define buf_size 1024
#define MAX_SEAT 20
#define MAX_CLIENTS 5

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

struct bus_info_result
{
    int book_status;
    int seat[MAX_SEAT];
};

sem_t mutex;
pthread_t threads[MAX_CLIENTS];
int sem_id;

int *client_handler(void *arg);
void client_login(int client_fd);
void client_register(int client_fd);
int booking_menu_handler(int client_fd, int client_id);
void book_ticket(int client_fd, int client_id);
struct bus_info_result update_bus_info(int bus_id, int seat_id, int book_status, int client_id);

int get_file_fd(char *file_name)
{
    int user_fd = open("user_info", O_RDWR);
    if (user_fd == -1)
    {
        perror("open error");
        exit(1);
    }

    int bus_fd = open("bus_info", O_RDWR);
    if (bus_fd == -1)
    {
        perror("open error");
        exit(1);
    }

    int booking_fd = open("booking_info", O_RDWR);
    if (booking_fd == -1)
    {
        perror("open error");
        exit(1);
    }
}

int all_seats_are_empty(int seat[]) {
    for (int i = 0; i < MAX_SEAT; i++) {
        if (seat[i] != 0) {
            return 0;
        }
    }
    return 1;
}

void get_pipe_bus_info(int pipefd[2])
{
    struct bus_info bus;
    int latest_bus_info = open("bus_info", O_RDWR);
    char buf[buf_size];
    int seats_available;

    while (read(latest_bus_info, &bus, sizeof(struct bus_info)) > 0)
    {
        seats_available = 0;

        for (int i = 0; i < MAX_SEAT; i++)
        {
            if (bus.seat[i] == 0)
            {
                seats_available++;
            }
        }
        //printf("Bus %d has %d seats available, %d seats are booked\n", bus.id, seats_available, MAX_SEAT - seats_available);

        sprintf(buf, "Bus %d has %d seats available, %d seats are booked", bus.id, seats_available, MAX_SEAT - seats_available);
        usleep(1000); // sleep for 1ms
        write(pipefd[1], buf, strlen(buf) + 1);
    }
    close(latest_bus_info);
}

int main(int argc, char *argv[])
{
    int pipefd[2];
    char buf[buf_size];
    pid_t pid;

    // check if '--reset' argument is passed
    if (argc > 1 && strcmp(argv[1], "--reset") == 0)
    {
        execl("./dummy_bus_generator && ./server", NULL);
    }

    if (pipe(pipefd) == -1)
    {
        perror("Unable to create pipe");
        return 1;
    }

    // create a child process to read the bus info file and display the bus info
    pid = fork();
    if (pid == 0)
    {
        // child process
        close(pipefd[0]);
        get_pipe_bus_info(pipefd);
        close(pipefd[1]);
    }
    else
    {   
        // parent process
        close(pipefd[1]);
        while (read(pipefd[0], buf, buf_size) > 0)
        {
            printf("%s\n", buf);
        }
        close(pipefd[0]);

        int server_fd, client_fd;
        int clients[MAX_CLIENTS] = {0};
        struct sockaddr_in server_addr, client_addr;
        int client_addr_size;
        fd_set readfds; // set of file descriptors to be monitored for reading
        int max_fd;     // maximum file descriptor value

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

        // Initialize the semaphore
        key_t semkey = 0x200;
        sem_id = initsem(semkey);
        sem_init(&mutex, 0, 1);

        printf("Server is running...\n");
        while (1)
        {
            // initialize the read file descriptor set
            FD_ZERO(&readfds);
            FD_SET(server_fd, &readfds);
            max_fd = server_fd;

            // add client sockets to the read file descriptor set
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                client_fd = clients[i];
                if (client_fd > 0)
                {
                    FD_SET(client_fd, &readfds);
                }
                max_fd = (client_fd > max_fd) ? client_fd : max_fd;
            }

            // wait for activity on any of the file descriptors
            int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
            if ((activity < 0) && (errno != EINTR))
            {
                perror("select error");
                exit(1);
            }

            // if there is activity on the server socket, it means a new client is trying to connect
            if (FD_ISSET(server_fd, &readfds))
            {
                client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
                if (client_fd == -1)
                {
                    perror("accept error");
                    exit(1);
                }
                printf("New client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (clients[i] == 0)
                    {
                        clients[i] = client_fd;
                        printf("Adding client to list of sockets as %d\n", i);

                        int *arg = malloc(sizeof(*arg));
                        *arg = client_fd;
                        if (pthread_create(&threads[i], NULL, client_handler, arg) != 0)
                        {
                            perror("Error creating thread");
                            exit(1);
                        }
                        break;
                    }
                }
            }

            // check for activity on the client sockets
            // for (int i = 0; i < MAX_CLIENTS; i++)
            // {
            //     client_fd = clients[i];
            //     if (FD_ISSET(client_fd, &readfds))
            //     {
            //         if (user_choice == 0)
            //         {
            //             printf("Client disconnected: %d\n", i);
            //             close(client_fd);
            //             clients[i] = 0;
            //             pthread_cancel(threads[i]);
            //         }

            //     }
            // }
        }

        semctl(sem_id, 0, IPC_RMID, 0);
        sem_destroy(&mutex);

        close(server_fd);

        return 0;
    }
}

int *client_handler(void *arg)
{
    int client_fd = *((int *)arg);
    free(arg);

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
           exit(0);
        }
    }
    printf("Log: Client disconnected\n");
    close(client_fd);
    pthread_exit(NULL);
}

void client_login(int client_fd)
{
    char username[buf_size], password[buf_size];
    struct account db_acc;
    int user_fd, client_id;

    recv(client_fd, &username, sizeof(username), 0);
    recv(client_fd, &password, sizeof(password), 0);

    user_fd = open("user_info", O_RDONLY);

    if (user_fd < 0)
    {
        perror("Error opening user.txt");
        return;
    }

    int valid = 0;

    p(sem_id);
    while (read(user_fd, &db_acc, sizeof(db_acc)) > 0)
    {
        if (strcmp(db_acc.name, username) == 0 && strcmp(db_acc.pass, password) == 0)
        {
            valid = 1;
            client_id = db_acc.id;
            break;
        }
        printf("Log: %s %s %d\n", db_acc.name, db_acc.pass, db_acc.id);
    }
    v(sem_id);

    if (valid)
    {
        printf("Log: %s user logged in\n", username);
        send(client_fd, &valid, sizeof(valid), 0);
        send(client_fd, &client_id, sizeof(client_id), 0);
        booking_menu_handler(client_fd, client_id);
    }
    else
    {
        printf("Log: Login failed\n");
        valid = 0;
        send(client_fd, &valid, sizeof(valid), 0);
    }
    

    //close(user_fd);
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

int booking_menu_handler(int client_fd, int client_id)
{
    int user_choice = 0;
    recv(client_fd, &user_choice, sizeof(user_choice), 0);
    user_choice = (user_choice > 0 && user_choice < 5) ? user_choice : 4;

    switch (user_choice)
    {
    case 1:
        printf("Log: book ticket\n");
        book_ticket(client_fd, client_id);
    case 2:
        printf("Log: view ticket\n");
        view_ticket(client_fd, client_id);
    case 3:
        // exit
        return 0;
    }
}

void book_ticket(int client_fd, int client_id)
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
    printf("Log: Total Bus is %d\n", num_buses);

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
    close(bus_fd);
    printf("Log: Sent bus info\n");

    // get bus id, client_id and seat number from client
    int bus_id, seat_id, book_status = 1;
    if (recv(client_fd, &bus_id, sizeof(bus_id), 0) > 0)
    {
        recv(client_fd, &seat_id, sizeof(seat_id), 0);
        recv(client_fd, &client_id, sizeof(client_id), 0);

        // update the bus_info file
        struct bus_info_result result = update_bus_info(bus_id, seat_id, book_status, client_id);
        book_status = result.book_status;
        update_booking_info(client_id, bus_id, seat_id, book_status);


        // send the booking status bus_idto the client
        send(client_fd, &book_status, sizeof(int), 0);
        printf("Log: Bus info updated\n");
    }

    booking_menu_handler(client_fd, client_id);
}

struct bus_info_result update_bus_info(int bus_id, int seat_id, int book_status, int client_id)
{
    int bus_fd = get_file_fd("bus_info");
    lseek(bus_fd, 0, SEEK_SET);

    struct bus_info_result result;

    struct bus_info db_bus;
    while (read(bus_fd, &db_bus, sizeof(db_bus)) > 0)
    {
        if (db_bus.id == bus_id)
        {
            // if (db_bus.seat[seat_id] == 0)
            // {
            //     db_bus.seat[seat_id] = 1;
            //     printf("Log: Seat booked\n");

            //     result.book_status = 1;

            //     printf("Log: BUS seat status:\n");
            //     for (int i = 0; i < MAX_SEAT; i++)
            //     {
            //         result.seat[i] = db_bus.seat[i];
            //         printf("%d ", result.seat[i]);
            //     }
            //     printf("\n");
            // }
            // else
            // {
            //     printf("Log: Seat already booked\n");
            //     result.book_status = 0;
            // }

            if (db_bus.seat[seat_id] == 0 && book_status == 1)
            {
                db_bus.seat[seat_id] = 1;
                printf("Log: Seat booked\n");
                result.book_status = 1;
                // update result.seat and print the seat status
            }
            else if (db_bus.seat[seat_id] == 1 && book_status == 0)
            {
                db_bus.seat[seat_id] = 0;
                printf("Log: Seat unbooked\n");
                result.book_status = 0;
                // update result.seat and print the seat status
            }
            else
            {
                printf("Log: Seat already booked/unbooked\n");
                result.book_status = 0;
            }
        }
        // save the bus info
        lseek(bus_fd, -1 * sizeof(struct bus_info), SEEK_CUR);
        write(bus_fd, &db_bus, sizeof(db_bus));
    }

    return result;
}

void update_booking_info(int client_id, int bus_id, int seat_id, int book_status)
{
    // create booking_info filebus_id
    int booking_fd = get_file_fd("booking_info");
    struct booking_info db_booking;

    int file_pointer = lseek(booking_fd, 0, SEEK_END);
    int found = 0;

    // search for a record with the same client ID
    lseek(booking_fd, 0, SEEK_SET);
    while (read(booking_fd, &db_booking, sizeof(db_booking)) > 0)
    {
        if (db_booking.client_id == client_id && db_booking.bus_id == bus_id)
        {
            found = 1;
            break;
        }
    }

    // if a record with the same client ID and Bus ID was found, update it
    if (found)
    {
        printf("Log: Booking info updated\n");
        lseek(booking_fd, -1 * sizeof(struct booking_info), SEEK_CUR);
    }
    // otherwise, create a new record
    else
    {
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
            printf("Log: Booking info created\n");
        }
    }

    // write the booking info to the file
    if (found)
    {
        db_booking.seat[seat_id] = book_status;    {
        db_booking.client_id = client_id;
        db_booking.bus_id = bus_id;
        strcpy(db_booking.date, "dd");
        for (int i = 0; i < MAX_SEAT; i++)
        {
            db_booking.seat[i] = 0;
        }
        db_booking.seat[seat_id] = book_status;
    }
        strcpy(db_booking.date, "dd");
    }
    else
    {
        db_booking.client_id = client_id;
        db_booking.bus_id = bus_id;
        strcpy(db_booking.date, "dd");
        for (int i = 0; i < MAX_SEAT; i++)
        {
            db_booking.seat[i] = 0;
        }
        db_booking.seat[seat_id] = book_status;
    }

    write(booking_fd, &db_booking, sizeof(db_booking));

    // reterive all records
    lseek(booking_fd, 0, SEEK_SET);
    while (read(booking_fd, &db_booking, sizeof(db_booking)) > 0)
    {
        if (all_seats_are_empty(db_booking.seat) == 1)
        {
            // delete the record by overwriting it with the next record
            struct booking_info next_record;
            if (read(booking_fd, &next_record, sizeof(next_record)) > 0)
            {
                lseek(booking_fd, -2 * sizeof(struct booking_info), SEEK_CUR);
                write(booking_fd, &next_record, sizeof(next_record));
            }
            else
            {
                // reached the end of the file, truncate it to remove the last record
                ftruncate(booking_fd, lseek(booking_fd, 0, SEEK_END) - sizeof(struct booking_info));
            }
        }
        else
        {
            // print the record
            printf("\nID: %d, Client ID: %d, Bus ID: %d, Date: %s \nSeat status:\n", db_booking.id, db_booking.client_id, db_booking.bus_id, db_booking.date);
            for (int i = 0; i < MAX_SEAT; i++)
            {
                printf("%d ", db_booking.seat[i]);
            }
            printf("\n");
        }
    }

    close(booking_fd);
}

void view_ticket(int client_fd, int client_id)
{   
    
    // Declare a pointer to an array of booking_info structures
    struct booking_info *booking_list = NULL;
    free(booking_list);
    int size = 0; // Keep track of the size of the array

    // Open the booking_info file for reading
    int fd = open("booking_info", O_RDONLY);
    if (fd < 0)
    {
        perror("Error opening booking_info file");
        return;
    }

    // Search for a booking with a matching client_id
    struct booking_info booking;
    while (read(fd, &booking, sizeof(booking)) > 0)
    {
        if (booking.client_id == client_id)
        {
            // Allocate memory for an additional element in the array
            size++;
            booking_list = realloc(booking_list, size * sizeof(struct booking_info));
            if (booking_list == NULL)
            {
                perror("Error allocating memory for booking list");
                return;
            }

            // store the booking data in the next available element of the array
            booking_list[size - 1].id = booking.id;
            booking_list[size - 1].client_id = booking.client_id;
            booking_list[size - 1].bus_id = booking.bus_id;
            strcpy(booking_list[size - 1].date, booking.date);
            for (int i = 0; i < MAX_SEAT; i++)
            {
                booking_list[size - 1].seat[i] = booking.seat[i];
            }
        }
    }
    bzero(&booking, sizeof(booking));
    memset(&booking, 0, sizeof(booking));

    // Send the number of bookings to the client
    send(client_fd, &size, sizeof(size), 0);
    bzero(&size, sizeof(size));
    printf("Log: %d booking(s) found\n", size);

    if (size == 0)
    {
        printf("Log: No booking found\n");
        booking_menu_handler(client_fd, client_id);
    }
    else
    {
        printf("Log: Booking list created\n");
    }

    // Now you can access the booking_list array and iterate through the bookings
    for (int i = 0; i < size; i++)
    {
        // do something with the booking_list[i] element
        printf("ID: %d\n", booking_list[i].id);
        send(client_fd, &booking_list[i], sizeof(struct booking_info), 0);
    }

    // get user option
    int user_option;
    recv(client_fd, &user_option, sizeof(user_option), 0);
    if(user_option != 1 && user_option != 2)
    {
        printf("Log: Invalid option\n");
        booking_menu_handler(client_fd, client_id);
    }

    // get seat_id and order_id from the client
    int seat_id, order_id;
    int update_seat[MAX_SEAT];
    struct bus_info_result bus_result;
    recv(client_fd, &order_id, sizeof(order_id), 0);
    recv(client_fd, &seat_id, sizeof(seat_id), 0);

    switch (user_option)
    {
    case 1:
        // Cancel the booking
        printf("client_id: %d, seat_id: %d\n", client_id, seat_id);

        for (int i = 0; i < size; i++)
        {
            printf("Log: Searching for booking\n");
            printf("Log: bus ID: %d\n", booking_list[i].bus_id);
            printf("Log: order ID: %d\n", booking_list[i].id);
            if (booking_list[i].id == order_id)
            {
                printf("Log: Booking found\n");
                update_booking_info(client_id, booking_list[i].bus_id, seat_id, 0);
                bus_result = update_bus_info(booking_list[i].bus_id, seat_id, 0, client_id);
            }
        }
        printf("BUS RESULT: %d", bus_result.book_status);

        if(bus_result.book_status == 0)
        {
            printf("\nLog: Booking cancelled\n");
            send(client_fd, &bus_result, sizeof(bus_result), 0);
        }
        else
        {
            printf("\nLog: Booking not cancelled\n");
            send(client_fd, &bus_result, sizeof(bus_result), 0);
        }
        free(booking_list);

        //view_ticket(client_fd, client_id);

        break;
    case 2:
        // Update the booking
        printf("client_id: %d, seat_id: %d\n", client_id, seat_id);

        for (int i = 0; i < size; i++)
        {
            printf("Log: Searching for booking\n");
            printf("Log: bus ID: %d\n", booking_list[i].bus_id);
            printf("Log: order ID: %d\n", booking_list[i].id);
            if (booking_list[i].id == order_id)
            {
                printf("Log: Booking found\n");
                update_booking_info(client_id, booking_list[i].bus_id, seat_id, 1);
                bus_result = update_bus_info(booking_list[i].bus_id, seat_id, 1, client_id);
            }
        }

        if(bus_result.book_status == 1)
        {
            printf("Log: Booking updated\n");
            send(client_fd, &bus_result, sizeof(bus_result), 0);
        }
        else
        {
            printf("Log: Booking update failed\n");
            send(client_fd, &bus_result, sizeof(bus_result), 0);
        }
        //view_ticket(client_fd, client_id);
        free(booking_list);


        break;
    default:
        // exit
        booking_menu_handler(client_fd, client_id);
        break;
    }
    close(fd);

    // free the memory allocated for the array
    booking_menu_handler(client_fd, client_id);


}

/* void view_ticket(int client_fd, int client_id)
{   
    // use the stat function to get the size of the file
    struct stat st;
    if (stat("booking_info", &st) < 0)
    {
        perror("Error getting file size");
        return;
    }

    // calculate the number of bookings in the file
    int num_bookings = st.st_size / sizeof(struct booking_info);
    printf("Number of bookings: %d", num_bookings);

    // send the number of bookings to the client
    send(client_fd, &num_bookings, sizeof(num_bookings), 0);

    // Open the booking_info file for reading
    int booking_fd = open("booking_info", O_RDWR);
    if (booking_fd < 0)
    {
        perror("Error opening booking_info file");
        return;
    }

    struct booking_info *db_booking_list = get_booking_info(num_bookings);

    for (int i = 0; i < num_bookings; i++)
    {
        printf("Client ID: %d", client_id);
        if (db_booking_list[i].client_id == client_id)
        {
            printf("\nID: %d, Client ID: %d, Bus ID: %d, Date: %s \nSeat status:\n", db_booking_list[i].id, db_booking_list[i].client_id, db_booking_list[i].bus_id, db_booking_list[i].date);
            for (int j = 0; j < MAX_SEAT; j++)
            {
                printf("%d ", db_booking_list[i].seat[j]);
            }
            printf("\n");
            send(client_fd, &db_booking_list[i], sizeof(db_booking_list[i]), 0);
        }
    }
    // while (read(booking_fd, &booking, sizeof(booking)) > 0)
    // {
    //     if (booking.client_id == client_id)
    //     {
    //         // Allocate memory for an additional element in the array
    //         size++;
    //         booking_list = realloc(booking_list, size * sizeof(struct booking_info));
    //         if (booking_list == NULL)
    //         {
    //             perror("Error allocating memory for booking list");
    //             return;
    //         }

    //         // store the booking data in the next available element of the array
    //         booking_list[size - 1].id = booking.id;
    //         booking_list[size - 1].client_id = booking.client_id;
    //         booking_list[size - 1].bus_id = booking.bus_id;
    //         strcpy(booking_list[size - 1].date, booking.date);
    //         for (int i = 0; i < MAX_SEAT; i++)
    //         {
    //             booking_list[size - 1].seat[i] = booking.seat[i];
    //         }
    //     }
    // }

    if (num_bookings == 0)
    {
        printf("Log: No booking found\n");
        booking_menu_handler(client_fd, client_id);
    }
    else
    {
        printf("Log: Booking list created\n");
    }

    for (int i = 0; i < num_bookings; i++)
    {
        printf("ID: %d\n", db_booking_list[i].id);
        send(client_fd, &db_booking_list[i], sizeof(struct booking_info), 0);
    }

    // get user option
    int user_option;
    recv(client_fd, &user_option, sizeof(user_option), 0);
    if(user_option != 1 && user_option != 2)
    {
        printf("Log: Invalid option\n");
        booking_menu_handler(client_fd, client_id);
    }

    // get seat_id and order_id from the client
    int seat_id, order_id;
    int update_seat[MAX_SEAT];
    struct bus_info_result bus_result;
    recv(client_fd, &order_id, sizeof(order_id), 0);
    recv(client_fd, &seat_id, sizeof(seat_id), 0);

    switch (user_option)
    {
    case 1:
        // Cancel the booking
        printf("client_id: %d, seat_id: %d\n", client_id, seat_id);

        for (int i = 0; i < num_bookings; i++)
        {
            printf("Log: Searching for booking\n");
            printf("Log: bus ID: %d\n", db_booking_list[i].bus_id);
            printf("Log: order ID: %d\n", db_booking_list[i].id);
            if (db_booking_list[i].id == order_id)
            {
                printf("Log: Booking found\n");
                update_booking_info(client_id, db_booking_list[i].bus_id, seat_id, 0);
                bus_result = update_bus_info(db_booking_list[i].bus_id, seat_id, 0, client_id);
            }
        }
        //view_ticket(client_fd, client_id);


        printf("BUS RESULT: %d", bus_result.book_status);

        if(bus_result.book_status == 0)
        {
            printf("\nLog: Booking cancelled\n");
            send(client_fd, &bus_result, sizeof(bus_result), 0);
        }
        else
        {
            printf("\nLog: Booking not cancelled\n");
            send(client_fd, &bus_result, sizeof(bus_result), 0);
        }

        //view_ticket(client_fd, client_id);

        break;
    case 2:
        // Update the booking
        printf("client_id: %d, seat_id: %d\n", client_id, seat_id);

        for (int i = 0; i < num_bookings; i++)
        {
            printf("Log: Searching for booking\n");
            printf("Log: bus ID: %d\n", db_booking_list[i].bus_id);
            printf("Log: order ID: %d\n", db_booking_list[i].id);
            if (db_booking_list[i].id == order_id)
            {
                printf("Log: Booking found\n");
                update_booking_info(client_id, db_booking_list[i].bus_id, seat_id, 1);
                bus_result = update_bus_info(db_booking_list[i].bus_id, seat_id, 1, client_id);
            }
        }

        if(bus_result.book_status == 1)
        {
            printf("Log: Booking updated\n");
            send(client_fd, &bus_result, sizeof(bus_result), 0);
        }
        else
        {
            printf("Log: Booking update failed\n");
            send(client_fd, &bus_result, sizeof(bus_result), 0);
        }
        //view_ticket(client_fd, client_id);


        break;
    default:
        // exit
        booking_menu_handler(client_fd, client_id);
        break;
    }
    printf("Log: Booking list freed\n");

    // free the memory allocated for the array
    booking_menu_handler(client_fd, client_id);


} */

// struct bus_info *get_booking_info(int num_order)
// {
//     // allocate memory for the bus list
//     struct booking_info *db_booking_list = malloc(num_order * sizeof(struct booking_info));

//     // open the booking_info file
//     int booking_fd = get_file_fd("booking_info");

//     // receive the bus information from the server
//     int num_orders = 0;
//     struct booking_info db_booking;
//     while (num_orders < num_order && read(booking_fd, &db_booking, sizeof(db_booking)) > 0)
//     {
//         db_booking_list[num_orders] = db_booking;
//         num_orders++;
//     }

//     return db_booking_list;
// }