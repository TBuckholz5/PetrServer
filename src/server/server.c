
// Trent Buckholz tbuckhol
// Nisha Sandhu nishaks

#include "server.h"
#include <pthread.h>
#include <signal.h>

char buffer[BUFFER_SIZE];
pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;
char *audit_filename;
FILE *audit_file;
pthread_mutex_t audit_lock = PTHREAD_MUTEX_INITIALIZER;

int listen_fd;

sbuf_t *jobbuffer;

void sigint_handler(int sig)
{
    printf("shutting down server\n");
    close(listen_fd);
    //close fds, free userlist, free roomlist
    node_t *iter = userlist.head;
    while (iter)
    {
        logout(((user *)(iter->value)), true);
        iter = iter->next;
    }
    sbuf_deinit(jobbuffer);
    exit(0);
}

int server_init(int server_port)
{
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Socket successfully created\n");

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA *)&servaddr, sizeof(servaddr))) != 0)
    {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Socket successfully binded\n");

    // Now server is ready to listen and verification
    if ((listen(sockfd, 1)) != 0)
    {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Server listening on port: %d.. Waiting for connection\n", server_port);

    return sockfd;
}

void *process_job(void *vargp)
{
    pthread_detach(pthread_self());

    while (1)
    {
        if (jobbuffer->front != jobbuffer->rear)
        {
            job *nextjob = sbuf_remove(jobbuffer);

            switch (nextjob->msg_type)
            {
            case RMCREATE:
                rmcreate(nextjob->sender, nextjob->roomrec);
                break;
            case RMDELETE:
                rmdelete(nextjob->msg);
                break;
            case RMLIST:
                rmlist(nextjob->sender);
                break;
            case RMJOIN:
                rmjoin(nextjob->sender, nextjob->msg);
                break;
            case RMLEAVE:
                rmleave(nextjob->sender, nextjob->msg);
                break;
            case RMSEND:
                rmsend(nextjob->sender, nextjob->roomrec, nextjob->msg);
                break;
            case USRSEND:
                usrsend(nextjob->sender, nextjob->userrec, nextjob->msg);
                break;
            case USRLIST:
                usrlist(nextjob->sender);
                break;
            }
            free(nextjob);
        }
    }

    return NULL;
}

//Function running in thread
void *process_client(void *clientinfo)
{
    user *newuser = (user *)clientinfo;
    pthread_detach(pthread_self());

    int received_size;
    petr_header header;
    bool terminated;

    while (1)
    {
        terminated = false;
        header.msg_len = 0;
        header.msg_type = 0;
        pthread_mutex_lock(&buffer_lock);

        bzero(buffer, BUFFER_SIZE);
        pthread_mutex_unlock(&buffer_lock);

        if ((rd_msgheader(newuser->connfd, &header)) < 0)
        {
            header.msg_len = 0;
            header.msg_type = ESERV;

            pthread_mutex_lock(&buffer_lock);
            wr_msg(newuser->connfd, &header, buffer);
            pthread_mutex_unlock(&buffer_lock);

            pthread_mutex_lock(&audit_lock);
            fprintf(audit_file, "Error on rd_msgheader\n");
            pthread_mutex_unlock(&audit_lock);
            continue;
        }
        else if (header.msg_type == 255 || header.msg_type == 0)
        {
            terminated = true;
            break;
            //if client terminated, log them out
        }
        else if ((header.msg_len == 0) && (header.msg_type != RMLIST) && (header.msg_type != USRLIST) && (header.msg_type != LOGOUT))
        {
            continue;
        }
        pthread_mutex_lock(&buffer_lock);
        received_size = read(newuser->connfd, buffer, header.msg_len);
        pthread_mutex_unlock(&buffer_lock);

        if (received_size < 0)
        {
            pthread_mutex_lock(&audit_lock);
            fprintf(audit_file, "Receiving failed\n");
            pthread_mutex_unlock(&audit_lock);
            header.msg_len = 0;
            header.msg_type = ESERV;
            pthread_mutex_lock(&buffer_lock);
            wr_msg(newuser->connfd, &header, buffer);
            pthread_mutex_unlock(&buffer_lock);

            break;
        }
        else if ((received_size == 0) && (header.msg_len != 0) && (header.msg_type != RMLIST) && (header.msg_type != USRLIST))
        {
            header.msg_len = 0;
            header.msg_type = ESERV;
            pthread_mutex_lock(&buffer_lock);
            wr_msg(newuser->connfd, &header, buffer);
            pthread_mutex_unlock(&buffer_lock);

            continue;
        }

        char message[header.msg_len];
        pthread_mutex_lock(&buffer_lock);
        strcpy(message, buffer);
        pthread_mutex_unlock(&buffer_lock);

        // Create job struct
        job *newjob = malloc(sizeof(job));
        strcpy(newjob->msg, message);
        newjob->msg_type = header.msg_type;
        newjob->sender = newuser;

        //SET JOB FIELDS
        if (newjob->msg_type == LOGOUT)
        {
            break;
        }
        else if (newjob->msg_type == RMCREATE)
        {
            // Check if room exists
            sem_wait(&roommutex);
            roomcnt++;
            if (roomcnt == 1)
            {
                sem_wait(&roomw);
            }
            sem_post(&roommutex);

            node_t *iter = roomlist.head;
            while (iter)
            {
                if (strcmp(((room *)iter->value)->name, message) == 0)
                {
                    // If the room exists  send ermexists
                    header.msg_len = 0;
                    header.msg_type = ERMEXISTS;

                    pthread_mutex_lock(&buffer_lock);
                    wr_msg(newuser->connfd, &header, buffer);
                    pthread_mutex_unlock(&buffer_lock);

                    pthread_mutex_lock(&audit_lock);
                    fprintf(audit_file, "ERMEXISTS: Room already exists\n");
                    pthread_mutex_unlock(&audit_lock);
                    break;
                }
                iter = iter->next;
            }
            if (iter != NULL)
            {
                sem_wait(&roommutex);
                roomcnt--;
                if (roomcnt == 0)
                {
                    sem_post(&roomw);
                }
                sem_post(&roommutex);
                continue;
            }
            strcpy(newjob->roomrec, newjob->msg);

            sem_wait(&roommutex);
            roomcnt--;
            if (roomcnt == 0)
            {
                sem_post(&roomw);
            }
            sem_post(&roommutex);
        }
        else if (newjob->msg_type == RMDELETE)
        {
            //check if room exists
            sem_wait(&roommutex);
            roomcnt++;
            if (roomcnt == 1)
                sem_wait(&roomw);
            sem_post(&roommutex);

            node_t *iter = roomlist.head;
            while (iter)
            {
                if (strcmp(((room *)iter->value)->name, message) == 0)
                {
                    break;
                }
                iter = iter->next;
            }

            //if room doesnt exist
            if (iter == NULL)
            {
                header.msg_len = 0;
                header.msg_type = ERMNOTFOUND;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(newuser->connfd, &header, buffer);
                pthread_mutex_unlock(&buffer_lock);

                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "ERMNOTFOUND ERROR: Room does not exist\n");
                pthread_mutex_unlock(&audit_lock);

                sem_wait(&roommutex);
                roomcnt--;
                if (roomcnt == 0)
                    sem_post(&roomw);
                sem_post(&roommutex);

                continue;
            }
            else
            {
                if (strcmp(((room *)iter->value)->creator->name, newjob->sender->name) != 0)
                {
                    header.msg_len = 0;
                    header.msg_type = ERMDENIED;

                    pthread_mutex_lock(&buffer_lock);
                    wr_msg(newuser->connfd, &header, buffer);
                    pthread_mutex_unlock(&buffer_lock);

                    pthread_mutex_lock(&audit_lock);
                    fprintf(audit_file, "ERMDENIED ERROR: Only creators can delete rooms\n");
                    pthread_mutex_unlock(&audit_lock);

                    sem_wait(&roommutex);
                    roomcnt--;
                    if (roomcnt == 0)
                        sem_post(&roomw);
                    sem_post(&roommutex);

                    continue;
                }
            }

            strcpy(newjob->roomrec, newjob->msg);

            sem_wait(&roommutex);
            roomcnt--;
            if (roomcnt == 0)
                sem_post(&roomw);
            sem_post(&roommutex);
        }
        else if (newjob->msg_type == RMJOIN)
        {
            sem_wait(&roommutex);
            roomcnt++;
            if (roomcnt == 1)
                sem_wait(&roomw);
            sem_post(&roommutex);

            //check if room exists
            node_t *iter = roomlist.head;
            while (iter)
            {
                if (strcmp(((room *)iter->value)->name, message) == 0)
                {
                    break;
                }
                iter = iter->next;
            }
            if (iter == NULL)
            {
                header.msg_len = 0;
                header.msg_type = ERMNOTFOUND;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(newuser->connfd, &header, buffer);
                pthread_mutex_unlock(&buffer_lock);

                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "ERMNOTFOUND: Room does not exist\n");
                pthread_mutex_unlock(&audit_lock);
                sem_wait(&roommutex);
                roomcnt--;
                if (roomcnt == 0)
                    sem_post(&roomw);
                sem_post(&roommutex);
                continue;
            }
            strcpy(newjob->roomrec, newjob->msg);

            sem_wait(&roommutex);
            roomcnt--;
            if (roomcnt == 0)
                sem_post(&roomw);
            sem_post(&roommutex);
        }
        else if (newjob->msg_type == RMLEAVE)
        {

            sem_wait(&roommutex);
            roomcnt++;
            if (roomcnt == 1)
                sem_wait(&roomw);
            sem_post(&roommutex);

            sem_wait(&usermutex);
            usercnt++;
            if (usercnt == 1)
                sem_wait(&userw);
            sem_post(&usermutex);

            //check if room exists
            node_t *iter = roomlist.head;
            while (iter)
            {
                if (strcmp(((room *)iter->value)->name, message) == 0)
                {
                    break;
                }
                iter = iter->next;
            }
            if (iter == NULL)
            {
                header.msg_len = 0;
                header.msg_type = ERMNOTFOUND;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(newuser->connfd, &header, buffer);
                pthread_mutex_unlock(&buffer_lock);

                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "ERMNOTFOUND: Room does not exist\n");
                pthread_mutex_unlock(&audit_lock);
                continue;
            }
            else
            {

                // Checking if user is in the room
                node_t *iter2 = ((room *)iter->value)->userlist.head;
                while (iter2)
                {
                    if (strcmp(((user *)iter2->value)->name, newuser->name) == 0)
                    {
                        break;
                    }
                    iter2 = iter2->next;
                }
                // If user is not in the room
                if (iter2 == NULL)
                {
                    header.msg_len = 0;
                    header.msg_type = OK;

                    pthread_mutex_lock(&buffer_lock);
                    wr_msg(newuser->connfd, &header, buffer);
                    pthread_mutex_unlock(&buffer_lock);

                    pthread_mutex_lock(&audit_lock);
                    fprintf(audit_file, "User is not in this room, so you have already left :)\n");
                    pthread_mutex_unlock(&audit_lock);

                    sem_wait(&usermutex);
                    usercnt--;
                    if (usercnt == 0)
                        sem_post(&userw);
                    sem_post(&usermutex);

                    sem_wait(&roommutex);
                    roomcnt--;
                    if (roomcnt == 0)
                        sem_post(&roomw);
                    sem_post(&roommutex);
                    continue;
                }
                else
                {
                    // Check if the user is the creator
                    if (strcmp(((room *)iter->value)->creator->name, newuser->name) == 0)
                    {
                        header.msg_len = 0;
                        header.msg_type = ERMDENIED;

                        pthread_mutex_lock(&buffer_lock);
                        wr_msg(newuser->connfd, &header, buffer);
                        pthread_mutex_unlock(&buffer_lock);

                        pthread_mutex_lock(&audit_lock);
                        fprintf(audit_file, "ERMDENIED: Creator cannot leave room\n");
                        pthread_mutex_unlock(&audit_lock);

                        sem_wait(&usermutex);
                        usercnt--;
                        if (usercnt == 0)
                            sem_post(&userw);
                        sem_post(&usermutex);

                        sem_wait(&roommutex);
                        roomcnt--;
                        if (roomcnt == 0)
                            sem_post(&roomw);
                        sem_post(&roommutex);
                        continue;
                    }
                }
            }
            strcpy(newjob->roomrec, newjob->msg);

            sem_wait(&usermutex);
            usercnt--;
            if (usercnt == 0)
                sem_post(&userw);
            sem_post(&usermutex);

            sem_wait(&roommutex);
            roomcnt--;
            if (roomcnt == 0)
                sem_post(&roomw);
            sem_post(&roommutex);

            strcpy(newjob->roomrec, newjob->msg);
        }
        else if (newjob->msg_type == USRSEND)
        {
            //check user exists
            sem_wait(&usermutex);
            usercnt++;
            if (usercnt == 1)
                sem_wait(&userw);
            sem_post(&usermutex);

            char *recipient = strtok(message, "\r\n");
            char *message2 = strtok(NULL, "\0");
            message2 = message2 + 1;

            strcpy(newjob->msg, message2);

            //check sender is dif then recipient
            if (strcmp(recipient, newjob->sender->name) == 0)
            {
                header.msg_len = 0;
                header.msg_type = ESERV;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(newuser->connfd, &header, buffer);
                pthread_mutex_unlock(&buffer_lock);

                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "Error in USRSEND: ESERV sender is same as recipient(%s)\n", message);
                pthread_mutex_unlock(&audit_lock);

                sem_wait(&usermutex);
                usercnt--;
                if (usercnt == 0)
                    sem_post(&userw);
                sem_post(&usermutex);

                continue;
            }

            // Check if recipient exists
            node_t *iter = userlist.head;
            while (iter)
            {
                if (strcmp(((user *)(iter->value))->name, recipient) == 0)
                {
                    break;
                }
                iter = iter->next;
            }
            if (iter == NULL)
            {
                //if user doesnt exist
                header.msg_len = 0;
                header.msg_type = EUSRNOTFOUND;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(newuser->connfd, &header, buffer);
                pthread_mutex_unlock(&buffer_lock);

                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "Error in USRSEND: EUSRNOTFOUND user not found(%s)\n", message);
                pthread_mutex_unlock(&audit_lock);

                sem_wait(&usermutex);
                usercnt--;
                if (usercnt == 0)
                    sem_post(&userw);
                sem_post(&usermutex);

                continue;
            }
            strcpy(newjob->userrec, recipient);
            sem_wait(&usermutex);
            usercnt--;
            if (usercnt == 0)
                sem_post(&userw);
            sem_post(&usermutex);
        }
        else if (newjob->msg_type == RMSEND)
        {
            sem_wait(&roommutex);
            roomcnt++;
            if (roomcnt == 1)
                sem_wait(&roomw);
            sem_post(&roommutex);

            // Split arguments
            char *roomname = strtok(message, "\r\n");
            char *message2 = strtok(NULL, "\0");
            message2 = message2 + 1;

            strcpy(newjob->msg, message2);

            node_t *iter = roomlist.head;

            //check if room exists
            while (iter)
            {
                if (strcmp(((room *)iter->value)->name, roomname) == 0)
                {
                    break;
                }
                iter = iter->next;
            }
            if (iter == NULL)
            {
                header.msg_len = 0;
                header.msg_type = ERMNOTFOUND;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(newuser->connfd, &header, buffer);
                pthread_mutex_unlock(&buffer_lock);

                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "Error in RMSEND: ERMNOTFOUND room not found(%s)\n", message2);
                pthread_mutex_unlock(&audit_lock);
                sem_wait(&roommutex);
                roomcnt--;
                if (roomcnt == 0)
                    sem_post(&roomw);
                sem_post(&roommutex);
                continue;
            }
            strcpy(newjob->roomrec, roomname);

            sem_wait(&roommutex);
            roomcnt--;
            if (roomcnt == 0)
                sem_post(&roomw);
            sem_post(&roommutex);
        }
        else if ((newjob->msg_type == USRLIST) || (newjob->msg_type == RMLIST))
        {
        }
        else
        { //invalid msgtype
            header.msg_len = 0;
            header.msg_type = ESERV;

            pthread_mutex_unlock(&buffer_lock);
            wr_msg(newuser->connfd, &header, buffer);

            pthread_mutex_lock(&audit_lock);
            fprintf(audit_file, "Error: ESERV invalid msg type(%s)\n", message);
            pthread_mutex_unlock(&audit_lock);
            continue;
        }

        //insert job into queue
        sbuf_insert(jobbuffer, newjob);
        pthread_mutex_lock(&audit_lock);
        fprintf(audit_file, "Job added to buffer with (msg_type=%x), (clientfd=%d), (msg=%s), (tid=%ld)\n", newjob->msg_type,
                newjob->sender->connfd, newjob->msg, pthread_self());
        pthread_mutex_unlock(&audit_lock);
    }

    pthread_mutex_lock(&audit_lock);
    fprintf(audit_file, "Client thread terminated (tid=%ld), (username=%s), (fd=%d)\n", pthread_self(), newuser->name, newuser->connfd);
    pthread_mutex_unlock(&audit_lock);
    // Close the socket at the end
    logout(newuser, terminated);
    return NULL;
}

void run_server(int server_port)
{
    listen_fd = server_init(server_port); // Initiate server and start listening on specified port
    int client_fd;
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int received_size;

    pthread_t tid;
    petr_header userloginhead;
    while (1)
    {
        // Wait and Accept the connection from client
        printf("Wait for new client connection\n");
        int client_fd;
        client_fd = accept(listen_fd, (SA *)&client_addr, (socklen_t *)&client_addr_len);
        if (client_fd < 0)
        {
            printf("server acccept failed\n");
            exit(EXIT_FAILURE);
        }
        else
        {

            printf("Client connection accepted\n");
            // Read header from client
            userloginhead.msg_len = 0;
            if ((rd_msgheader(client_fd, &userloginhead)) < 0)
            {
                userloginhead.msg_len = 0;
                userloginhead.msg_type = ESERV;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(client_fd, &userloginhead, buffer);
                pthread_mutex_unlock(&buffer_lock);

                close(client_fd);
                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "Error on rd_msgheader\n");
                pthread_mutex_unlock(&audit_lock);
                continue;
            }
            // Check that message type is login
            if (userloginhead.msg_type != LOGIN)
            {
                userloginhead.msg_len = 0;
                userloginhead.msg_type = ESERV;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(client_fd, &userloginhead, buffer);
                pthread_mutex_unlock(&buffer_lock);

                close(client_fd);
                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "User denied : protocol not followed (fd=%d)\n", client_fd);
                pthread_mutex_unlock(&audit_lock);
                continue;
            }
            pthread_mutex_lock(&buffer_lock);
            // Read username into buffer from client
            received_size = read(client_fd, buffer, sizeof(buffer));

            char username[userloginhead.msg_len];
            strcpy(username, buffer);
            pthread_mutex_unlock(&buffer_lock);

            if ((received_size == 0) && (userloginhead.msg_len = 0))
            {
                continue;
            }

            if (received_size <= 0)
            {
                userloginhead.msg_len = 0;
                userloginhead.msg_type = ESERV;

                pthread_mutex_lock(&buffer_lock);
                wr_msg(client_fd, &userloginhead, buffer);
                pthread_mutex_unlock(&buffer_lock);

                close(client_fd);
                pthread_mutex_lock(&audit_lock);
                fprintf(audit_file, "User denied : protocol not followed (name=%s), (fd=%d)\n", username, client_fd);
                pthread_mutex_unlock(&audit_lock);

                continue;
            }
            // User read start
            sem_wait(&usermutex);
            usercnt++;
            if (usercnt == 1)
                sem_wait(&userw);
            sem_post(&usermutex);
            // Check if username already exists in userlist
            node_t *iter = userlist.head;
            while (iter)
            {
                if (strcmp(((user *)(iter->value))->name, username) == 0)
                {
                    userloginhead.msg_len = 0;
                    userloginhead.msg_type = EUSREXISTS;

                    pthread_mutex_lock(&buffer_lock);
                    wr_msg(client_fd, &userloginhead, buffer);
                    pthread_mutex_unlock(&buffer_lock);

                    close(client_fd);
                    pthread_mutex_lock(&audit_lock);
                    fprintf(audit_file, "User denied : already exists (name=%s), (fd=%d)\n", username, client_fd);
                    pthread_mutex_unlock(&audit_lock);

                    sem_wait(&usermutex);
                    usercnt--;
                    if (usercnt == 0)
                        sem_post(&userw);
                    sem_post(&usermutex);

                    break;
                }
                iter = iter->next;
            }
            if (iter != NULL)
            {
                continue;
            }
            // Send OK to client
            userloginhead.msg_len = 0;

            userloginhead.msg_type = OK;
            pthread_mutex_lock(&buffer_lock);
            wr_msg(client_fd, &userloginhead, buffer);
            pthread_mutex_unlock(&buffer_lock);
            // User read end
            sem_wait(&usermutex);
            usercnt--;
            if (usercnt == 0)
                sem_post(&userw);
            sem_post(&usermutex);
            // Write user accepted to audit file, add time and date?
            pthread_mutex_lock(&audit_lock);
            fprintf(audit_file, "User added with (name=%s), (fd=%d)\n", username, client_fd);
            pthread_mutex_unlock(&audit_lock);
            // Create user struct and create thread
            user *client = malloc(sizeof(user));
            client->connfd = client_fd;
            strcpy(client->name, username);
            // Insert the client into userlist
            login(client);
            pthread_create(&tid, NULL, process_client, (void *)client);
            pthread_mutex_lock(&audit_lock);
            fprintf(audit_file, "Client thread created (tid=%ld), (name=%s), (fd=%d)\n", tid, username, client_fd);
            pthread_mutex_unlock(&audit_lock);

            pthread_mutex_lock(&buffer_lock);
            bzero(buffer, BUFFER_SIZE);
            pthread_mutex_unlock(&buffer_lock);
        }
    }
    pthread_mutex_lock(&buffer_lock);
    bzero(buffer, BUFFER_SIZE);
    pthread_mutex_unlock(&buffer_lock);
    close(listen_fd);
    return;
}

int main(int argc, char *argv[])
{
    int opt;
    unsigned int port = 0;
    int numThreads = 2;

    jobbuffer = malloc(sizeof(sbuf_t));
    sbuf_init(jobbuffer, 100);
    protocolfuncsinit();

    // Initialize userlist and roomlist
    userlist.length = 0;
    roomlist.length = 0;
    userlist.head = NULL;
    roomlist.head = NULL;

    while ((opt = getopt(argc, argv, "hj:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            printf(USAGE);
            return EXIT_SUCCESS;
            break;
        case 'j':
            numThreads = atoi(optarg);
            break;
        default: /* '?' */
            fprintf(stderr, USAGE);
            exit(EXIT_FAILURE);
        }
    }
    if ((argv[optind + 1] != NULL) && (argv[optind] != NULL))
    {
        audit_filename = argv[optind + 1];
        port = atoi(argv[optind]);
    }
    else
    {
        fprintf(stderr, USAGE);
        exit(EXIT_FAILURE);
    }

    if (port == 0)
    {
        fprintf(stderr, "ERROR: Port number for server to listen is not given\n");
        fprintf(stderr, USAGE);
        exit(EXIT_FAILURE);
    }

    if (signal(SIGINT, sigint_handler) == SIG_ERR)
    {
        perror("Failed to set signal handler");
        exit(EXIT_FAILURE);
    }

    audit_file = fopen(audit_filename, "w");

    pthread_t tid;

    // Create job threads
    int i;
    for (i = 0; i < numThreads; ++i)
    {
        pthread_create(&tid, NULL, process_job, NULL);
    }

    run_server(port);

    return 0;
}
