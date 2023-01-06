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
#include <sys/ipc.h>
#include <sys/msg.h>
#include "sem.h"
#include <time.h>

#define PORT 5666
#define buf_size 1024
#define MAX_SEAT 20
#define QUEUE_KEY 1234

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
    char date[20];
    char time[20];
    int seat[MAX_SEAT];
};

struct bus_info_result
{
    int book_status;
    int seat[MAX_SEAT];
};

struct bus_message_q
{
    long type;
    int num_buses;
};

int sem_id;
int user_choice;
int num_clients = 0;
int *client_fds = NULL;
pthread_t *threads = NULL;

int *client_handler(void *arg);
void client_login(int client_fd);
void client_register(int client_fd);
int booking_menu_handler(int client_fd, int client_id);
void book_ticket(int client_fd, int client_id);
int update_bus_info(int bus_id, int seat_id, int book_status, int client_id);

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
        /* Validate info get in child process */
        //printf("Bus %d has %d seats available, %d seats are booked\n", bus.id, seats_available, MAX_SEAT - seats_available);

        sprintf(buf, "Bus %d has %d seats available, %d seats are booked", bus.id, seats_available, MAX_SEAT - seats_available);
        usleep(1000);// sleep to wait for 1ms, else buffer will be filled up and the child process will be blocked
        write(pipefd[1], buf, strlen(buf) + 1);
    }
    close(latest_bus_info);
}

void dissconnect_client(int client_fd)
{   
    // remove the client from the client_fds array and threads array
    for (int i = 0; i < num_clients; i++)
    {
        if (client_fds[i] == client_fd)
        {
            memmove(&client_fds[i], &client_fds[i + 1], (num_clients - i - 1) * sizeof(*client_fds));
            memmove(&threads[i], &threads[i + 1], (num_clients - i - 1) * sizeof(*threads));
            num_clients--;
            break;
        }
    }
}

char *format_date(char *date_str)
{
    char *day = strtok(date_str, " ");
    char *month = strtok(NULL, " ");
    char *day_of_month = strtok(NULL, " ");

    static char date[20];
    sprintf(date, "%s %s %s", month, day_of_month, day);
    return date;
}

int main(int argc, char *argv[])
{
    int pipefd[2];
    char buf[buf_size];
    pid_t pid;

    printf("[use ./server --reset for reseting the bus info]\n");

    // check if '--reset' argument is passed
    if (argc > 1 && strcmp(argv[1], "--reset") == 0)
    {
        int num_buses;

        // create the message queue
        int queue_id = msgget(QUEUE_KEY, 0666 | IPC_CREAT);
        if (queue_id < 0)
        {
            perror("Error creating message queue");
            return 1;
        }

        printf("Enter the number of buses: ");
        scanf("%d", &num_buses);

        // fill in the message queue struct
        struct bus_message_q msg;
        msg.type = 1;
        msg.num_buses = num_buses;

        // send the message queue to the dummy bus generator
        if (msgsnd(queue_id, &msg, sizeof(struct bus_message_q), 0) < 0)
        {
            perror("Error sending message");
            return 1;
        }

        execl("./dummy_bus_generator", NULL);
    }

    if (pipe(pipefd) == -1)
    {
        perror("Unable to create pipe");
        return 1;
    }

    /* child process to read bus info then pass to parent */
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

        /* server code */
        int server_fd, client_fd;
        struct sockaddr_in server_addr, client_addr;

        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
        {
            perror("socket error");
            exit(1);
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
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

        /* Initialize the semaphore */
        key_t semkey = 0x200;
        sem_id = initsem(semkey);

        /* variables for select() */
        int client_addr_size;
        int max_fd;
        int *client_fd_ptr;
        fd_set readfds; 
        pthread_t *thread_ptr;

        printf("Server is running...\n");
        while (1)
        {
            // initialize the read file descriptor set
            FD_ZERO(&readfds);
            FD_SET(server_fd, &readfds);
            max_fd = server_fd;

            // add client sockets to the read file descriptor set
            for (int i = 0; i < num_clients; i++)
            {
                client_fd = client_fds[i];
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
                perror("select dissconnect_client error");
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
                printf("Log: New client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                // add the new client to the list of connected clients
                num_clients++;
                client_fds = realloc(client_fds, num_clients * sizeof(*client_fds));
                client_fds[num_clients - 1] = client_fd;
                threads = realloc(threads, num_clients * sizeof(*threads));

                // create a new thread to handle the new connected client
                client_fd_ptr = malloc(sizeof(*client_fd_ptr));
                *client_fd_ptr = client_fd;
                // pass the client socket file descriptor to the client handler thread
                if (pthread_create(&threads[num_clients - 1], NULL, client_handler, client_fd_ptr) != 0)
                {
                    perror("Error creating thread");
                    exit(1);
                }
            }
        }

        semctl(sem_id, 0, IPC_RMID, 0);
        close(server_fd);
        return 0;
    }
}

int *client_handler(void *arg)
{
    int client_fd = *((int *)arg);
    free(arg);

    while (1)
    {
        recv(client_fd, &user_choice, sizeof(user_choice), 0);
        user_choice = (user_choice > 0 && user_choice < 4) ? user_choice : 3;
        printf("Log: User choice: %d \n", user_choice);

        switch (user_choice)
        {
        case 1:
            printf("Log: User Login\n");
            client_login(client_fd);
            user_choice = 3;
            break;
        case 2:
            printf("Log: User Register\n");
            client_register(client_fd);
            break;
        case 3:
            dissconnect_client(client_fd);
            printf("Log: User Logout\n");
            close(client_fd);
            return NULL;
        case 0:
            dissconnect_client(client_fd);
            printf("Log: User Exit\n");
            close(client_fd);
            return NULL;
        }
    }
}

void client_login(int client_fd)
{
    char username[buf_size], password[buf_size];
    struct account db_acc;
    int user_fd, client_id, nbytes;

    nbytes = recv(client_fd, &username, sizeof(username), 0);
    nbytes = recv(client_fd, &password, sizeof(password), 0);

    // check if the client has disconnected
    if(nbytes < 0)
    {
        perror("Error reading username");
        return;
    }
    else if(nbytes == 0)
    {
        printf("Log: Client disconnected\n");
        user_choice = 3;
        return NULL;
    }
    
    // enter critical section, read user file
    p(sem_id);
    user_fd = open("user_info", O_RDONLY);
    if (user_fd < 0)
    {
        perror("Error opening user.txt");
        return;
    }

    int valid = 0;
    while (read(user_fd, &db_acc, sizeof(db_acc)) > 0)
    {
        if (strcmp(db_acc.name, username) == 0 && strcmp(db_acc.pass, password) == 0)
        {
            valid = 1;
            client_id = db_acc.id;
            break;
        }
    }
    v(sem_id);
    // exit critical section

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
        client_id = 0;
        send(client_fd, &valid, sizeof(valid), 0);
        send(client_fd, &client_id, sizeof(client_id), 0);
        return NULL;
    }
}

void client_register(int client_fd)
{

    char username[buf_size], password[buf_size];
    struct account db_acc;
    int fd, nbytes, valid = 0;

    nbytes = recv(client_fd, &username, sizeof(username), 0);
    nbytes = recv(client_fd, &password, sizeof(password), 0);

    // check if the client has disconnected
    if(nbytes < 0)
    {
        perror("Error reading username");
        return;
    }
    else if(nbytes == 0)
    {
        printf("Log: Client disconnected\n");
        user_choice = 3;
        return NULL;
    }
    
    // enter critical section, read user file
    p(sem_id);
    fd = open("user_info", O_RDWR);

    if (fd < 0)
    {
        perror("Error opening user.txt");
        return;
    }

    // Check if the file is empty
    int file_pointer = lseek(fd, 0, SEEK_END);

    if (file_pointer == 0)
    {
        // File is empty, create a new user record
        valid = 1;
        db_acc.id = 1;
        strcpy(db_acc.name, username);
        strcpy(db_acc.pass, password);

        write(fd, &db_acc, sizeof(db_acc));
        printf("Log: New user created\n");

        send(client_fd, &username, sizeof(username), 0);
        send(client_fd, &valid, sizeof(valid), 0);
    }
    else
    {
        // File is not empty, search for a matching username
        valid = 1;

        // Move the file pointer back to the beginning of the file
        lseek(fd, 0, SEEK_SET);

        // Read the data from the file until the end is reached
        while (read(fd, &db_acc, sizeof(db_acc)) > 0)
        {
            if (strcmp(db_acc.name, username) == 0)
            {
                // if username is already taken
                valid = 0;
                printf("Log: Username is already taken\n");
                break;
            }
        }

        if (valid)
        {
            // valid = 1, username is not taken
            // Move the file pointer to the end of the file
            file_pointer = lseek(fd, 0, SEEK_END);

            // Get the last record in the file
            lseek(fd, -1 * sizeof(struct account), SEEK_END);
            read(fd, &db_acc, sizeof(db_acc));

            // Create a new user record
            db_acc.id++;
            strcpy(db_acc.name, username);
            strcpy(db_acc.pass, password);

            // Write the new user record to the file
            write(fd, &db_acc, sizeof(db_acc));
            printf("Log: New user created\n");

            send(client_fd, &username, sizeof(username), 0);
            send(client_fd, &valid, sizeof(valid), 0);
        }
        else
        {
            // valid = 0, username is taken
            send(client_fd, &username, sizeof(username), 0);
            send(client_fd, &valid, sizeof(valid), 0);
            printf("Log: %s user registration failed\n", username);
        }
    }

    close(fd);
    v(sem_id);
    // exit critical section

    /* Uncomment belowe code retrerive all records */
    // lseek(fd, 0, SEEK_SET);
    // while (read(fd, &db_acc, sizeof(db_acc)) > 0)
    // {
    //     printf("ID: %d, Name: %s, Pass: %s  \n", db_acc.id, db_acc.name, db_acc.pass);
    // }
    // close(fd);
}

int booking_menu_handler(int client_fd, int client_id)
{
    /* After client login will pass to here*/
    int user_choice = 0, nbytes;
    nbytes = recv(client_fd, &user_choice, sizeof(user_choice), 0);
    user_choice = (user_choice > 0 && user_choice < 4) ? user_choice : 3;

    // check if the client has disconnected
    if(nbytes < 0)
    {
        perror("Error reading username");
        return;
    }
    else if(nbytes == 0)
    {
        printf("Log: Client disconnected\n");
        user_choice = 3;
        return NULL;
    }

    switch (user_choice)
    {
    case 1:
        printf("Log: Book ticket\n");
        book_ticket(client_fd, client_id);
        break;
    case 2:
        printf("Log: View ticket\n");
        view_ticket(client_fd, client_id);
        break;
    case 3:
        close(client_fd);
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

    // calculate the number of buses by dividing the file size by the size of the bus_info structure
    int num_buses = st.st_size / sizeof(struct bus_info);
    printf("Log: Total Bus is %d\n", num_buses);

    // send the number of buses to the client
    send(client_fd, &num_buses, sizeof(num_buses), 0);

    // reterive bus info
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
        /* uncomment below code to retrive all records */
        // printf("ID: %d, Name: %s, Date: %s  ,Time: %s\n", db_bus.id, db_bus.name, db_bus.date, db_bus.time);
        send(client_fd, &db_bus, sizeof(db_bus), 0);
    }
    close(bus_fd);
    printf("Log: Sent bus info\n");

    // receive bus id, client_id and seat number from client
    int bus_id, seat_id, book_status = 1;
    int nybtes = recv(client_fd, &bus_id, sizeof(bus_id), 0);
    if (nybtes > 0)
    {
        recv(client_fd, &seat_id, sizeof(seat_id), 0);
        recv(client_fd, &client_id, sizeof(client_id), 0);

        // enter critical section, update bus info and booking info
        p(sem_id);
        book_status = update_bus_info(bus_id, seat_id, book_status, client_id);
        update_booking_info(client_id, bus_id, seat_id, book_status);
        v(sem_id);
        // exit critical section


        // send the booking status bus_idto the client
        send(client_fd, &book_status, sizeof(int), 0);
        printf("Log: Bus info updated\n");
    }
    else if (nybtes == 0)
    {
        printf("Log: Client disconnected\n");
        user_choice = 3;
        return NULL;
    }
    else
    {
        printf("Log: Error reading bus_id\n");
        return;
    }

    booking_menu_handler(client_fd, client_id);
}

int update_bus_info(int bus_id, int seat_id, int book_status, int client_id)
{
    int status = 0;
    int bus_fd = open("bus_info", O_RDWR);
    lseek(bus_fd, 0, SEEK_SET);
    
    /* uncomment below code to check the received data */
    // printf("bus_id: %d, seat_id: %d, book_status: %d, client_id: %d", bus_id, seat_id, book_status, client_id);
    
    // verify the seat availability and update the bus_info file
    struct bus_info db_bus;
    while (read(bus_fd, &db_bus, sizeof(db_bus)) > 0)
    {
        if (db_bus.id == bus_id)
        {
            if (db_bus.seat[seat_id] == 0 && book_status == 1)
            {
                db_bus.seat[seat_id] = 1;
                printf("Log: Seat booked\n");
                status = 1;
            }
            else if (db_bus.seat[seat_id] == 1 && book_status == 0)
            {
                db_bus.seat[seat_id] = 0;
                printf("Log: Seat unbooked\n");
                status = 0;
            }
            else if (db_bus.seat[seat_id] == 1 && book_status == 1)
            {
                printf("Log: Seat booked by other client\n");
                status = 0;
            }
            else
            {
                printf("Log: Seat already booked/unbooked\n");
                status = 0;
            }
        }
        // save the bus info
        lseek(bus_fd, -1 * sizeof(struct bus_info), SEEK_CUR);
        write(bus_fd, &db_bus, sizeof(db_bus));
    }

    return status;
}

void update_booking_info(int client_id, int bus_id, int seat_id, int book_status)
{
    int booking_fd = get_file_fd("booking_info");
    struct booking_info db_booking;

    int file_pointer = lseek(booking_fd, 0, SEEK_END);
    int found = 0;

    // search for a record with the same client ID and Bus ID
    lseek(booking_fd, 0, SEEK_SET);
    while (read(booking_fd, &db_booking, sizeof(db_booking)) > 0)
    {
        if (db_booking.client_id == client_id && db_booking.bus_id == bus_id)
        {
            found = 1;
            break;
        }
    }

    // if a record with the same client ID and Bus ID was found,
    // move the file pointer to the match record
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
            // if the file is empty, create a new record with id = 1
            db_booking.id = 1;
            lseek(booking_fd, 0, SEEK_END);
            printf("Log: Booking info created\n");
        }
        else
        {
            // else create a new record with id = last record id + 1
            lseek(booking_fd, -1 * sizeof(struct booking_info), SEEK_END);
            read(booking_fd, &db_booking, sizeof(db_booking));
            db_booking.id++;
            printf("Log: Booking info created\n");
        }
    }
    
    // mark the seat as booked/unbooked for booking info
    if (found)
    {
        time_t mytime;
        mytime = time(NULL);
        char *date_str = ctime(&mytime);
        char *formatted_date = format_date(date_str);

        db_booking.seat[seat_id] = book_status;
        strcpy(db_booking.date, formatted_date);
    }
    else
    {
        time_t mytime;
        mytime = time(NULL);
        char *date_str = ctime(&mytime);
        char *formatted_date = format_date(date_str);

        db_booking.client_id = client_id;
        db_booking.bus_id = bus_id;
        strcpy(db_booking.date, formatted_date);
        for (int i = 0; i < MAX_SEAT; i++)
        {
            db_booking.seat[i] = 0;
        }
        db_booking.seat[seat_id] = book_status;
    }
    // save the booking info
    write(booking_fd, &db_booking, sizeof(db_booking));

    // reterive all records to remove the records with all seats empty
    // happen when a client book unbook all seats
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
        // uncomment code below to view all booking info
        // else
        // {
            
        //     printf("\nID: %d, Client ID: %d, Bus ID: %d, Date: %s \nSeat status:\n", db_booking.id, db_booking.client_id, db_booking.bus_id, db_booking.date);
        //     for (int i = 0; i < MAX_SEAT; i++)
        //     {
        //         printf("%d ", db_booking.seat[i]);
        //     }
        //     printf("\n");
        // }
    }

    close(booking_fd);
}

void view_ticket(int client_fd, int client_id)
{   
    
    // Declare a pointer to an array of booking_info structures
    struct booking_info *booking_list = NULL;
    int size = 0; // Keep track of the size of the array

    // Open the booking_info file for reading
    int booking_fd = open("booking_info", O_RDONLY);
    if (booking_fd < 0)
    {
        perror("Error opening booking_info file");
        return;
    }
    lseek(booking_fd, 0, SEEK_SET);

    // enter critical section
    /* search for a booking with a matching client_id */
    p(sem_id);
    struct booking_info booking;
    free(booking_list);

    while (read(booking_fd, &booking, sizeof(booking)) > 0)
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
    v(sem_id);
    /* end of searching */
    // exit critical section

    // Send the number of bookings to the client
    send(client_fd, &size, sizeof(size), 0);
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

    // access the booking_list array and iterate through the bookings
    for (int i = 0; i < size; i++)
    {   
        /* uncomment code below to view all booking info id */
        // printf("ID: %d\n", booking_list[i].id);
        send(client_fd, &booking_list[i], sizeof(struct booking_info), 0);
    }

    // get user option
    int user_option;
    recv(client_fd, &user_option, sizeof(user_option), 0);
    if(user_option != 1 && user_option != 2)
    {
        printf("Log: Invalid option receive\n");
        booking_menu_handler(client_fd, client_id);
    }

    // receive seat_id and order_id from the client
    int seat_id, order_id, book_status;
    int update_seat[MAX_SEAT];
    recv(client_fd, &order_id, sizeof(order_id), 0);
    recv(client_fd, &seat_id, sizeof(seat_id), 0);

    switch (user_option)
    {
    case 1:
        // cancel the booking
        for (int i = 0; i < size; i++)
        {
            if (booking_list[i].id == order_id)
            {
                printf("Log: Booking record found\n");
                book_status = update_bus_info(booking_list[i].bus_id, seat_id, 0, client_id);
                if(book_status = 0)
                    update_booking_info(client_id, booking_list[i].bus_id, seat_id, book_status);
            }
        }
        if(book_status == 0)
        {
            printf("\nLog: Booking cancelled\n");
            send(client_fd, &book_status, sizeof(book_status), 0);
        }
        else
        {
            printf("\nLog: Booking not cancelled\n");
            send(client_fd, &book_status, sizeof(book_status), 0);
        }
        free(booking_list);
        break;
    case 2:
        // update the booking
        for (int i = 0; i < size; i++)
        {
            if (booking_list[i].id == order_id)
            {
                printf("Log: Booking record found\n");
                book_status = update_bus_info(booking_list[i].bus_id, seat_id, 1, client_id);
                if(book_status == 1)
                    update_booking_info(client_id, booking_list[i].bus_id, seat_id, book_status);
            }
        }

        if(book_status == 1)
        {
            printf("Log: Booking updated\n");
            send(client_fd, &book_status, sizeof(book_status), 0);
        }
        else
        {
            printf("Log: Booking update failed\n");
            send(client_fd, &book_status, sizeof(book_status), 0);
        }
        free(booking_list);
        break;
    }
    close(booking_fd);
    booking_menu_handler(client_fd, client_id);
}