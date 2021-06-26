#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/fcntl.h>

/* threads */
#include <sched.h>
#include <pthread.h>

/* time */
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

/* serial communications */
#include <termios.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>

#include "xmodem.h"
#include "ports.h"
#include "stream.h"

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

enum {
    DirectionTx = 0,
    DirectionSend = DirectionTx,
    DirectionRx,
    DirectionRecv = DirectionRx,
    Directions
};

enum {
    TcpModeServer = 0,
    TcpModeClient,
    TcpModes
};

int main(int argc, char **argv) {
    XmodemOptions options;
    GenericDevice i_device, o_device;

    o_device.name[0] = 0;
    i_device.name[0] = 0;

    i_device.recv = recv_from_file;
    i_device.size = size_from_file;

    o_device.recv = recv_from_desc;
    o_device.send = send_over_desc;
    o_device.getc = getc_from_desc;
    o_device.putc = putc_over_desc;

    int verbose = 0;
    int direction = Directions; /* invalid value */
    int mode = TcpModes;
    int port = 0;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-verbose") == 0) {
            verbose = 1;
        } else if ((strcmp(argv[i], "-s") == 0) || (strcmp(argv[i], "--send") == 0)) {
            direction = DirectionSend;
        } else if ((strcmp(argv[i], "-r") == 0) || (strcmp(argv[i], "--receive") == 0)) {
            direction = DirectionRecv;
        } else if (strcmp(argv[i], "-i") == 0) {
            snprintf(i_device.name, sizeof (i_device.name), "%s", argv[++i]);
            i_device.fd = open(i_device.name, S_IREAD, O_RDWR);
        } else if (strcmp(argv[i], "-d") == 0) {
            snprintf(o_device.name, sizeof (o_device.name), "%s", argv[++i]);
        } else if (strcmp(argv[i], "-server") == 0) {
            mode = TcpModeServer;
        } else if (strcmp(argv[i], "-client") == 0) {
            mode = TcpModeClient;
        } else if (
            (strcmp(argv[i], "-p") == 0) ||
            (strcmp(argv[i], "--port") == 0)) {
            port = atoi(argv[++i]);
        }
    }

    if (direction == Directions) {
        printf("must specify -r(eceive) or -s(end)\n");
        return -1;
    }

    /* open device */
    TcpServerInfo tcp_server_info;
    TcpClientInfo tcp_client_info;

    if (strlen(o_device.name)) {
        o_device.fd = initialize_serial_port(o_device.name, 115200, 0, 0, 0);
    } else if (mode == TcpModeServer) {
        initialize_tcp_server_info(&tcp_server_info);
        initialize_server_socket(&tcp_server_info, port);
        server_task(&tcp_server_info);
    } else if (mode == TcpModeClient) {
        initialize_tcp_client_info(&tcp_client_info);
        tcp_client_info.portno = port;
        client_task(&tcp_client_info);
    }

    /*** receive machine ***/
    RxLooperArgs rx_looper_args;
    Queue rx_queue;
    memset(&rx_looper_args, 0, sizeof(RxLooperArgs));

    unsigned int run = 1;
    rx_looper_args.queue = &rx_queue;
    rx_looper_args.run = &run;
    rx_looper_args.verbose = verbose;
    if (mode == TcpModeClient) {
        rx_looper_args.fd = tcp_client_info.client_fd;
    } else if (mode == TcpModeServer) {
        rx_looper_args.fd = tcp_server_info.client_fd;
    }
    pthread_t rx_thread;

    pthread_create(&rx_thread, NULL, rx_looper, (void *) &rx_looper_args); /* create thread */

#if 0
    if (direction == DirectionSend) {
        const char *start_command = "<xmodem r RADIO9.BIN\r";
        write(o_device.fd, start_command, sizeof (start_command) - 1);
    } else if (direction == DirectionRecv) {
        const char *start_command = "<xmodem s RADIO9.BIN\r";
        write(o_device.fd, start_command, sizeof (start_command) - 1);
    }

    int errors = 0;
    options.timeout_ms = 100000;
    options.max_retries = 25000;
    options.max_retransmissions = 25000;
    options.packet_size_code = XMODEM_CCC;
    options.packet_size = 1024;
    // xmodem_recv(&o_device, &i_device, &options, &errors);
#endif

    while (1) {
        int n;
        char buff[64];
        sleep(1);
        if (mode == TcpModeClient) {
            printf("\nreceived: ");
            n = recv_from_desc(&rx_queue, buff, sizeof (buff), 0, 0);
            for (int i = 0; i < n; ++i) {
                printf("%2.2x %c", buff[i], buff[i]);
            }
        } else if (mode == TcpModeServer) {
            const char *str = "hello, world\n";
            send_over_desc(&tcp_server_info.client_fd, str, sizeof (str), 0);
        }
    }

    pthread_join(rx_thread, NULL);

    return 0;
}
