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
        info->infrastructure.fd = accept(info->server_fd, (struct sockaddr *) &cli_addr, &clilen);

        if (info->infrastructure.fd < 0) {
            perror("ERROR on accept");
            continue;
        }

    } while (0);
}

void *client_task(void *ext) {
    TcpClientInfo *info = (TcpClientInfo *) ext;

    /* First call to socket() function */
    info->infrastructure.fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(info->infrastructure.portno);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return 0;
    }

    if (connect(info->infrastructure.fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return 0;
    }

}

int recv_from_file(void *handle, uint8_t *b, unsigned int n, unsigned int offset, unsigned int timeout) {
    int fd = * (int *) handle;
    unsigned int remaining = n;
    if (offset) { printf("read from offset not supported\n"); }
    do {
        int n_read = read(fd, b, remaining);
        remaining -= n_read;
        b += n_read;
    } while (remaining >= 0);
}

int size_from_file(void *handle, unsigned int timeout) {
    int fd = * (int *) handle;
    unsigned int size = lseek(fd, 0, SEEK_END);
    close(fd);
    return size;
}

/* @brief returns 1 if current time exceeds the time specified by timeout. 0 otherwise */
static int timeout_expired(struct timespec const * const timeout) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec != timeout->tv_sec) {
        return ((now.tv_sec > timeout->tv_sec) ? 1 : 0);
    }
    return ((now.tv_nsec >= timeout->tv_nsec) ? 1 : 0);
}

int recv_from_desc(void *handle, uint8_t *b, unsigned int n, unsigned int offset, unsigned int timeout) {

    /* calculate timeout */
    struct timespec spec, expiry;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    expiry.tv_sec = spec.tv_sec;
    expiry.tv_nsec = spec.tv_nsec + timeout * 1e6;
    if (expiry.tv_nsec > 1e9) {
        expiry.tv_nsec -= 1e9;
        ++expiry.tv_sec;
    }

    /* read (max) n bytes from queue before timeout */
    Queue *q = (Queue *) handle;
    unsigned int index = 0;
    while ((q->head != q->tail) && (index <= n) && (timeout_expired(&expiry) == 0)) {
        b[index] = q->buff[q->tail];
        q->tail = (q->tail + 1) & q->mask;
        ++index;
    }

    return index; /* how many were read */
}

int getc_from_desc(void *handle, uint8_t *byte, unsigned int timeout) {
    return recv_from_desc(handle, byte, 1, 0, timeout);
}

#define ONE_BILLION (1000000000L)
int send_over_desc(void *handle, uint8_t const * const b, unsigned int n, unsigned int timeout) {
    int fd = * (int *) handle;

    /* calculate timeout */
    struct timespec spec, expiry;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    expiry.tv_sec = spec.tv_sec;
    expiry.tv_nsec = spec.tv_nsec + timeout * 1e6;
    if (expiry.tv_nsec > ONE_BILLION) {
        expiry.tv_nsec -= ONE_BILLION;
        ++expiry.tv_sec;
    }

    unsigned int index = 0;
    unsigned int remaining = n;
    do {
        int n_write = write(fd, &b[index], remaining);
        remaining -= n_write;
        index += n_write;
    } while ((index <= n) && (timeout_expired(&expiry) == 0));

    return index; /* how many went out */
}

int putc_over_desc(void *handle, uint8_t byte, unsigned int timeout) {
    return send_over_desc(handle, &byte, 1, timeout);
}
