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

/* @brief returns 1 if current time exceeds the time specified by timeout. 0 otherwise */
static int timeout_expired(struct timespec const * const timeout) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec != timeout->tv_sec) {
        return ((now.tv_sec > timeout->tv_sec) ? 1 : 0);
    }
    return ((now.tv_nsec >= timeout->tv_nsec) ? 1 : 0);
}

int recv_from_uart(void *handle, uint8_t *b, unsigned int n, unsigned int offset, unsigned int timeout) {

    /* calculate timeout */
    struct timespec spec, expiry;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    expiry.tv_sec = spec.tv_sec;
    expiry.tv_nsec = spec.tv_nsec + timeout * 1e6;
    if (expiry.tv_nsec > 1000000000) {
        expiry.tv_nsec -= 1000000000;
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

int send_over_uart(void *handle, uint8_t const *b, unsigned int n, unsigned int timeout) {
    int fd = * (int *) handle;

    /* calculate timeout */
    struct timespec spec, expiry;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    expiry.tv_sec = spec.tv_sec;
    expiry.tv_nsec = spec.tv_nsec + timeout * 1e6;
    if (expiry.tv_nsec > 1000000000) {
        expiry.tv_nsec -= 1000000000;
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

int main(int argc, char **argv) {
    GenericDevice device;

    device.recv = recv_from_uart;
    device.send = send_over_uart;

    int verbose = 0;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            snprintf(device.name, sizeof (device.name), "%s", argv[++i]);
            device.fd = initialize_serial_port(device.name, 230400, 0, 0, 0);
        }
    }

#if 0
    const char command[] = {
        0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        '<', 'e', 'c', 'h', 'o', ' ',
        'h', 'e', 'l', 'l', 'o', ' ',
        'c', 'r', 'u', 'e', 'l', ' ',
        'w', 'o', 'r', 'l', 'd', '!'
    };
#endif
    char command[] = {
            0x7e, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            'v', 'e', 'r', 0x0d
    };

//    write(device.fd, command, sizeof (command));

    RxLooperArgs rx_looper_args;
    Queue rx_queue;
    memset(&rx_queue, 0, sizeof (Queue));
    memset(&rx_looper_args, 0, sizeof(RxLooperArgs));

    uint8_t rx_buff[512];
    memset(rx_buff, 0, sizeof (rx_buff));
    rx_queue.buff = rx_buff;
    rx_queue.mask = sizeof (rx_buff);
    unsigned int run = 1;
    rx_looper_args.queue = &rx_queue;
    rx_looper_args.run = &run;
    rx_looper_args.verbose = verbose;
    rx_looper_args.fd = device.fd;
    pthread_t rx_thread;

    pthread_create(&rx_thread, NULL, rx_looper, (void *) &rx_looper_args); /* create thread */

    unsigned int trigger_level = 16; /* effective size of test */
    uint32_t total_bytes_read = 0;
    unsigned int trigger_start = 64;
    uint32_t byte_errors = 0;
    uint32_t bit_errors = 0;
    time_t next_time = 0;
    unsigned int pattern_trapped = 0;
    int index;

    while (1) {
        Queue *q = &rx_queue;

        int n_read = (q->head - q->tail) & q->mask;
        total_bytes_read += n_read;
        printf("%d bytes read\nBEGIN\n", total_bytes_read);

        for (int i = 0; i < n_read; ++i) {
            uint8_t byte = q->buff[q->tail];
            if (byte == 0x0d) { byte = 0x0a; }
            printf("%c", byte);
            q->tail = (q->tail + 1) & q->mask;
        }
        printf("\nDONE\n");

        command[4] = sizeof (command) - 12;
        device.send(&device.fd, command, sizeof (command), 4000);

        sleep(1);
    }

    pthread_join(rx_thread, NULL);

    return 0;
}
