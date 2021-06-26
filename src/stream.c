#include <sys/select.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include "ports.h"
#include "stream.h"

#if 0

void *server_looper(void *ext) {


    /* If connection is established then start communicating */
    char buffer[256];
    bzero(buffer, sizeof (buffer));
    int n = read(info->client_fd, buffer, sizeof (buffer) - 1);

    if (n < 0) {
        perror("ERROR reading from socket");
        exit(1);
    }

    printf("Here is the message: %s\n",buffer);

    /* Write a response to the client */
    n = write(info->client_fd,"I got your message",18);

    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }

}

#endif

void *rx_looper(void *ext) {
    RxLooperArgs *args = (RxLooperArgs *) ext;
    Queue *q = args->queue;
    uint8_t buffer[512];
    q->buff = buffer;
    q->mask = sizeof (buffer) - 1;
    while (*args->run) {
        fd_set fds;
        FD_ZERO (&fds);
        FD_SET (args->fd, &fds);
        // printf("waiting for %d ...\n", args->fd);
        int res = select (args->fd + 1, &fds, NULL, NULL, NULL);
        // printf("and it came and it went ...\n");
        if (res && FD_ISSET(args->fd, &fds)) {
            unsigned int room;
            if (q->head >= q->tail) { room = 1 + q->mask - q->head; } /* how much fits to top of queue */
            else { room = q->tail - q->head - 1; }
            if (room) {
                int n_read = read(args->fd, &q->buff[q->head], room);
                // printf("and i saw %d (room = %d)...\n", n_read, room);
                if (args->verbose) {
                    for (int i = 0; i < n_read; ++i) {
                        uint8_t byte = q->buff[(q->head + i) & q->mask];
                        if (args->verbose) { printf("%2.2x ", byte); }
                    }
                    if (n_read > 0) { printf("\n"); }
                }
                q->head = (q->head + n_read) & q->mask;
            }
        }
#ifdef SLEEP_NOT_SELECT
        struct timespec remaining, request = {0, 1000000};
        nanosleep(&request, &remaining);
#endif
    }

    return NULL;
}

void *server_task(void *arg)
{
    TcpServerInfo *info = (TcpServerInfo *) arg;

    do {

        struct sockaddr_in cli_addr;
        int clilen = sizeof(cli_addr);

        /* accept connection from client */
        info->client_fd = accept(info->server_fd, (struct sockaddr *) &cli_addr, &clilen);

        if (info->client_fd < 0) {
            perror("ERROR on accept");
            continue;
        }

    } while (0);
}

void *client_task(void *ext) {
    TcpClientInfo *info = (TcpClientInfo *) ext;

    /* First call to socket() function */
    info->client_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(info->portno);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return 0;
    }

    if (connect(info->client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return 0;
    }

}