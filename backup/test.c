void client_register(int client_fd)
{

    char username[buf_size];
    char password[buf_size];

    read(client_fd, &username, sizeof(username));
    read(client_fd, &password, sizeof(password));

    FILE *user_file = fopen("user.txt", "r");
    if (user_file == NULL)
    {
        perror("Error opening user.txt");
        return;
    }
    
    char line[buf_size];
    while (fgets(line, sizeof(line), user_file) != NULL)
    {
        printf("line: %s", line);
        if (strstr(line, username) != NULL)
        {
            printf("Username is already taken.\n");
            return;
        }
    }
    fclose(user_file);
}