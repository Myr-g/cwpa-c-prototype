#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

typedef struct genre
{
    char name[64];
    char prompt[200];
} genre_t;

typedef struct genre_list
{
    genre_t *data;
    int count;
    int capacity;
} genre_list_t;

typedef struct session_client
{
    int fd;
    char username[64];
    struct session_client *next;
} session_client_t;

typedef struct session
{
    char name[64];
    genre_t *genre;
    char story[20000];
    pthread_mutex_t lock;
    FILE *log_fp;
    int participant_count;
    session_client_t *clients;
    struct session *next;
} session_t;

genre_list_t genre_list;
session_t *sessions_head = NULL;
pthread_mutex_t sessions_lock = PTHREAD_MUTEX_INITIALIZER;

void load_genres()
{
    genre_list.count = 0;
    genre_list.capacity = 16;
    genre_list.data = malloc(sizeof(genre_t) * genre_list.capacity);

    FILE *file = fopen("data/genres.txt", "r");
    if (!file)
    {
        fprintf(stderr, "Error: genres.txt not found.\n");
        exit(1);
    }

    char line[1024];

    while (fgets(line, sizeof(line), file))
    {
        line[strcspn(line, "\n")] = '\0';

        char *delimiter = strchr(line, '|');

        if(delimiter == NULL)
        {
            fprintf(stderr, "Error: malformed line in file.\n");
            exit(1);

        }
        *delimiter = '\0';
        char *name = line;
        char *prompt = delimiter + 1;

        if(genre_list.count >= genre_list.capacity)
        {
            genre_list.capacity *= 2;
            genre_list.data = realloc(genre_list.data, sizeof(genre_t) * genre_list.capacity);
        }

        genre_t *genre = &genre_list.data[genre_list.count++];

        if(name[0] == '\0')
        {
            fprintf(stderr, "Error: no associated genre for prompt '%s'.\n", prompt);
            exit(1);

        }

        strncpy(genre->name, name, sizeof(genre->name) - 1);
        genre->name[sizeof(genre->name) - 1] = '\0';

        if(prompt[0] == '\0')
        {
            snprintf(genre->prompt, sizeof(genre->prompt), "You may begin writing a %s story.", genre->name);
        }

        else
        {
            strncpy(genre->prompt, prompt, sizeof(genre->prompt) - 1);
        }

        genre->prompt[sizeof(genre->prompt) - 1] = '\0';
    }

    if(genre_list.count == 0)
    {
        fprintf(stderr, "Error: genres.txt is empty.\n");
        exit(1);
    }

    fclose(file);
}

session_t *create_session(const char *name, genre_t *genre)
{
    pthread_mutex_lock(&sessions_lock);

    // Check if session name already exists
    session_t *current = sessions_head;

    while (current != NULL) 
    {
        if (strcmp(current->name, name) == 0) 
        {
            pthread_mutex_unlock(&sessions_lock);
            return NULL;
        }

        current = current->next;
    }

    // Allocate new session
    session_t *new_session = malloc(sizeof(session_t));

    if (!new_session) 
    {
        pthread_mutex_unlock(&sessions_lock);
        return NULL;
    }

    // Initialize fields
    strncpy(new_session->name, name, sizeof(new_session->name) - 1);
    new_session->name[sizeof(new_session->name) - 1] = '\0';

    new_session->genre = genre;
    new_session->story[0] = '\0';
    new_session->participant_count = 0;
    new_session->clients = NULL;
    new_session->log_fp = NULL;
    pthread_mutex_init(&new_session->lock, NULL);

    char filename[128];
    snprintf(filename, sizeof(filename), "logs/%s.txt", new_session->name);
    FILE *fp = fopen(filename, "a");

    if(fp == NULL)
    {
        new_session->log_fp = NULL;
    }

    else
    {
        new_session->log_fp = fp;
    }

    // Insert into linked list (head insertion)
    new_session->next = sessions_head;
    sessions_head = new_session;

    pthread_mutex_unlock(&sessions_lock);
    return new_session;
}

session_t *find_session(const char *name) 
{
    pthread_mutex_lock(&sessions_lock);
    session_t *current = sessions_head;

    while(current != NULL) 
    {
        if(strcmp(current->name, name) == 0) 
        {
            pthread_mutex_unlock(&sessions_lock);
            return current;
        }

        current = current->next;
    }

    pthread_mutex_unlock(&sessions_lock);
    return NULL;
}

void destroy_session(session_t *session)
{
    if(!session)
    {
        return;
    }

    if(session->log_fp)
    {
        fclose(session->log_fp);
    }

    session_client_t *current = session->clients;

    while(current) 
    {
        session_client_t *temp = current;
        current = current->next;
        free(temp);
    }

    pthread_mutex_destroy(&session->lock);
    free(session);
}

void remove_session(session_t *session)
{
    pthread_mutex_lock(&sessions_lock);

    session_t *prev = NULL;
    session_t *current = sessions_head;

    while(current)
    {
        if(current == session)
        {
            if(prev)
            {
                prev->next = current->next;
            }

            else
            {
                sessions_head = current->next;
            }

            pthread_mutex_unlock(&sessions_lock);
            destroy_session(current);
            return;
        }

        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&sessions_lock);
}

int add_client_to_session(session_t *session, int fd, const char *username)
{
    pthread_mutex_lock(&session->lock);

    session_client_t *new_client = malloc(sizeof(session_client_t));

    if(new_client == NULL)
    {
        pthread_mutex_unlock(&session->lock);
        return -1;
    }

    new_client->fd = fd;
    strncpy(new_client->username, username, sizeof(new_client->username) - 1);
    new_client->username[sizeof(new_client->username) - 1] = '\0';

    new_client->next = session->clients;
    session->clients = new_client;
                
    session->participant_count += 1;

    pthread_mutex_unlock(&session->lock);

    return 0;
}

int remove_client_from_session(session_t *session, int fd)
{
    pthread_mutex_lock(&session->lock);

    session_client_t *curr = session->clients;
    session_client_t *prev = NULL;

    while(curr != NULL)
    {
        if(curr->fd == fd)
        {
            break;
        }

        prev = curr;
        curr = curr->next;
    }

    // Client not found
    if(curr == NULL)
    {
        pthread_mutex_unlock(&session->lock);
        return -1;
    }

    // Removing the head
    if(prev == NULL)
    {
        session->clients = curr->next;
    }

    // Removing from middle or end
    else
    {
        prev->next = curr->next;
    }

    free(curr);
    session->participant_count -= 1;
    pthread_mutex_unlock(&session->lock);

    return 0;
}

void *handle_client(void *arg) 
{
    int client_fd = *(int*)arg;
    free(arg);

    char username[64] = {0};
    genre_t *selected_genre = NULL;
    session_t *current_session = NULL;

    char buffer[BUFFER_SIZE];

    while(1)
    {
        memset(buffer, 0, BUFFER_SIZE);

        if(read(client_fd, buffer, BUFFER_SIZE) <= 0)
        {
            break;
        }

        else
        {
            if(strncasecmp(buffer, "HELP", 4) == 0)
            {
                char *reply = "Out of Session:\n"
                "  JOIN <username>           - Registers your username with the server.\n"
                "  SESSION CREATE <name> <genre>     - Creates a new collaborative writing session with the chosen genre.\n"
                "  SESSION JOIN <name>       - Joins an existing session.\n"
                "  LIST SESSIONS             - Lists all active sessions.\n"
                "  QUIT                      - Disconnects from the server.\n"
                "\n"
                "In Session:\n"
                "  VIEW                      - Displays the current story.\n"
                "  WRITE <text>              - Adds text to the shared story.\n"
                "  EXIT SESSION              - Leaves the current session.\n\n";

                send(client_fd, reply, strlen(reply), 0);
                continue;
            }

            else if(strncasecmp(buffer, "JOIN ", 5) == 0)
            {
                if(username[0] != '\0')
                {
                    char reply[128];
                    snprintf(reply, sizeof(reply), "ERROR; You have already joined as %s.\n", username);
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                char name_buffer[64];
                strncpy(name_buffer, buffer + 5, sizeof(name_buffer) - 1);
                name_buffer[sizeof(name_buffer)-1] = '\0';

                name_buffer[strcspn(name_buffer, "\r\n")] = '\0';

                if(name_buffer[0] == '\0')
                {
                    char *reply = "ERROR; Username cannot be empty.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                else
                {
                    strncpy(username, name_buffer, sizeof(username) - 1);
                    username[sizeof(username) - 1] = '\0';

                    char reply[128];
                    snprintf(reply, sizeof(reply), "Welcome %s.\n", username);
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }
            }

            else if(strncasecmp(buffer, "SESSION CREATE ", 15) == 0)
            {
                if(current_session != NULL)
                {
                    char *reply = "ERROR; You are already in a session.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue; 
                }

                char session_buffer[128];
                strncpy(session_buffer, buffer + 15, sizeof(session_buffer) - 1);
                session_buffer[sizeof(session_buffer)-1] = '\0';

                // Trim newline
                session_buffer[strcspn(session_buffer, "\r\n")] = '\0';

                char session_name[64];
                char genre_name[64];

                // Tokenize
                char *t = strtok(session_buffer, " \t\n\r");
                if(t)
                { 
                    strncpy(session_name, t, sizeof(session_name) - 1); 
                    session_name[sizeof(session_name) - 1] = '\0'; 
                }

                else 
                { 
                    char *reply = "ERROR; Missing session name.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue; 
                }

                t = strtok(NULL, " \t\n\r");
                if(t) 
                { 
                    strncpy(genre_name, t, sizeof(genre_name) - 1); 
                    genre_name[sizeof(genre_name) - 1] = '\0'; 
                }

                else 
                { 
                    char *reply = "ERROR; Missing genre.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue; 
                }

                // Check if user has registered themselves
                if(username[0] == '\0')
                {
                    char *reply = "ERROR; You must JOIN before attempting to create a session.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                if(strcmp(genre_name, "RANDOM") != 0)
                {
                    // Check if the user has chosen a valid genre
                    int valid_genre = 0;

                    for(int i = 0; i < genre_list.count; i++)
                    {
                        if(strcasecmp(genre_list.data[i].name, genre_name) == 0)
                        {
                            valid_genre = 1;
                            selected_genre = &genre_list.data[i];
                            break;
                        }
                    }

                    if(!valid_genre)
                    {
                        char *reply = "ERROR; Invalid genre.\n";
                        send(client_fd, reply, strlen(reply), 0);
                        continue;
                    }
                }

                else
                {
                    int rand_index = rand() % genre_list.count;
                    selected_genre = &genre_list.data[rand_index];
                }

                session_t *new_session = create_session(session_name, selected_genre);

                if(new_session == NULL)
                {
                    char *reply = "ERROR; Session name already exists.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                current_session = new_session;

                if(add_client_to_session(new_session, client_fd, username) < 0)
                {
                    char *reply = "ERROR; Server memory allocation failed.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                char reply[128];
                snprintf(reply, sizeof(reply), "SESSION CREATED: %s\nGENRE: %s\n", session_name, new_session->genre->name);
                send(client_fd, reply, strlen(reply), 0);

                char prompt[200];
                snprintf(prompt, sizeof(prompt), "Prompt: %s\n", new_session->genre->prompt);
                send(client_fd, prompt, strlen(prompt), 0);

                continue;
            }

            else if(strncasecmp(buffer, "SESSION JOIN ", 13) == 0)
            {
                if(current_session != NULL)
                {
                    char *reply = "ERROR; You are already in a session.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue; 
                }

                char session_name[64];
                strncpy(session_name, buffer + 13, sizeof(session_name) - 1);
                session_name[sizeof(session_name)-1] = '\0';

                // Trim newline
                session_name[strcspn(session_name, "\r\n")] = '\0';

                if(username[0] == '\0')
                {
                    send(client_fd, "ERROR; You must JOIN first.\n", 29, 0);
                    continue;
                }

                // Check if session name already exists
                session_t *current = find_session(session_name);

                if(current == NULL)
                {
                    char reply[128];
                    snprintf(reply, sizeof(reply), "ERROR; SESSION '%s' does not exist.\n", session_name);
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                else
                {
                    current_session = current;

                    if(add_client_to_session(current, client_fd, username) < 0)
                    {
                        char *reply = "ERROR; Server memory allocation failed.\n";
                        send(client_fd, reply, strlen(reply), 0);
                        continue;
                    }

                    char reply[128];
                    snprintf(reply, sizeof(reply), "JOINED SESSION: '%s'.\nGENRE: %s\n", session_name, current_session->genre->name);
                    send(client_fd, reply, strlen(reply), 0);

                    char prompt[200];
                    snprintf(prompt, sizeof(prompt), "Prompt: %s\n", current_session->genre->prompt);
                    send(client_fd, prompt, strlen(prompt), 0);

                    continue;
                }
            }

            else if(strncasecmp(buffer, "LIST SESSIONS", 13) == 0)
            {
                if(current_session != NULL)
                {
                    char *reply = "To view the list of ongoing sessions, please do EXIT SESSION first.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                pthread_mutex_lock(&sessions_lock);

                if(!sessions_head)
                {
                    char *reply = "No active sessions.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    pthread_mutex_unlock(&sessions_lock);
                    continue;
                }

                char reply[2048];
                session_t *current = sessions_head;

                snprintf(reply, sizeof(reply), "Active Sessions:\n");

                while(current != NULL)
                {
                    int next_index = strlen(reply);
                    snprintf(reply + next_index, sizeof(reply) - next_index, "- %s (%d participants, Genre: %s)\n", current->name, current->participant_count, current->genre->name);

                    current = current->next;
                }

                pthread_mutex_unlock(&sessions_lock);
                send(client_fd, reply, strlen(reply), 0);
                continue;
            }

            else if(strncasecmp(buffer, "VIEW", 4) == 0)
            {
                if (current_session == NULL)
                {
                    char *reply = "ERROR; You are not in a SESSION.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                pthread_mutex_lock(&current_session->lock);
                send(client_fd, current_session->story, strlen(current_session->story), 0);
                pthread_mutex_unlock(&current_session->lock);
            }

            else if(strncasecmp(buffer, "WRITE ", 6) == 0)
            {
                if(current_session == NULL)
                {
                    char *reply = "ERROR; You are not in a SESSION.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                char story_copy[10000] = {0};
                strncpy(story_copy, buffer + 6, sizeof(story_copy) - 1);
                story_copy[sizeof(story_copy) - 1] = '\0';

                int fds[64];
                int fd_count = 0;

                pthread_mutex_lock(&current_session->lock);

                strcat(current_session->story, story_copy);

                if(current_session->log_fp != NULL)
                {
                    fprintf(current_session->log_fp, "<%s>: %s", username, story_copy);
                    fflush(current_session->log_fp);
                }

                session_client_t *sc = current_session->clients;

                while (sc != NULL && fd_count < 64)
                {
                    fds[fd_count++] = sc->fd;
                    sc = sc->next;
                }

                pthread_mutex_unlock(&current_session->lock);

                char broadcast[10000];
                snprintf(broadcast, sizeof(broadcast), "<%s>: %s", username, story_copy);

                for (int i = 0; i < fd_count; i++)
                {
                    if (fds[i] != client_fd)
                    {
                        send(fds[i], broadcast, strlen(broadcast), 0);
                    }
                }
            }

            else if(strncasecmp(buffer, "EXIT SESSION", 12) == 0)
            {
                if(current_session == NULL)
                {
                    char *reply = "ERROR; You are not in a SESSION.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }

                else
                {
                    if(remove_client_from_session(current_session, client_fd) < 0)
                    {
                        char *reply = "Error. Client not found\n";
                        send(client_fd, reply, strlen(reply), 0);
                        continue;
                    }

                    session_t *left_session = current_session;
                    current_session = NULL;

                    if(left_session->participant_count == 0)
                    {
                        remove_session(left_session);
                    }

                    char *reply = "Exiting current SESSION.\n";
                    send(client_fd, reply, strlen(reply), 0);
                    continue;
                }
            }

            else if(strncasecmp(buffer, "QUIT", 4) == 0)
            {
                if(current_session != NULL)
                {
                    if(remove_client_from_session(current_session, client_fd) < 0)
                    {
                        char *reply = "Error. Client not found\n";
                        send(client_fd, reply, strlen(reply), 0);
                        continue;
                    }

                    session_t *left_session = current_session;
                    current_session = NULL;

                    if(left_session->participant_count == 0)
                    {
                        remove_session(left_session);
                    }
                }
                
                char *reply = "Goodbye.";
                send(client_fd, reply, strlen(reply), 0);
                break;
            }
        }
    }

    close(client_fd);
    return NULL;
}


int main() 
{
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    srand(time(NULL));
    load_genres();

    // Create socket
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind socket to port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;   // Listen on all interfaces
    address.sin_port = htons(PORT);

    if(bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) 
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if(listen(server_fd, 1) < 0) 
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server running on port %d... waiting for connection.\n", PORT);

    while(1) 
    {
        int client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);

        if (client_fd < 0) 
        {
            perror("accept");
            continue;
        }

        printf("New client connected!\n");

        pthread_t thread;
        int *pclient = malloc(sizeof(int));
        *pclient = client_fd;

        pthread_create(&thread, NULL, handle_client, pclient);
        pthread_detach(thread);
    }
}
