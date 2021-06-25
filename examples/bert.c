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

int getc_from_uart(void *handle, uint8_t *byte, unsigned int timeout) {
    return recv_from_uart(handle, byte, 1, 0, timeout);
}

int send_over_uart(void *handle, uint8_t *b, unsigned int n, unsigned int timeout) {
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

int putc_over_uart(void *handle, uint8_t byte, unsigned int timeout) {
    return send_over_uart(handle, &byte, 1, timeout);
}

uint8_t analysis_buff[1024 * 1024];

int main(int argc, char **argv) {
    XmodemOptions options;
    GenericDevice device;

    device.recv = recv_from_uart;
    device.send = send_over_uart;
    device.getc = getc_from_uart;
    device.putc = putc_over_uart;

    int verbose = 0;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            snprintf(device.name, sizeof (device.name), "%s", argv[++i]);
            device.fd = initialize_serial_port(device.name, 230400, 0, 0, 0);
        }
    }

    const char command[] = "<diagnostics -bert 1\r";
    write(device.fd, command, sizeof (command) - 1);

    /* analysis variables */
    uint8_t pattern_buff[1024];
    Queue analysis_queue;
    memset(&analysis_queue, 0, sizeof (analysis_queue));
    analysis_queue.buff = analysis_buff;
    analysis_queue.mask = sizeof (analysis_buff) - 1;
    uint32_t error_hist[9];
    memset(error_hist, 0, sizeof (error_hist));

    RxLooperArgs rx_looper_args;
    Queue rx_queue;
    memset(&rx_looper_args, 0, sizeof(RxLooperArgs));

    uint8_t rx_buff[512];
    rx_queue.buff = rx_buff;
    rx_queue.mask = sizeof (rx_buff);
    unsigned int run = 1;
    rx_looper_args.queue = &analysis_queue;
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
        Queue *q = &analysis_queue;
        unsigned int room = (q->mask + q->tail - q->head) & q->mask;
        // if (q->head >= q->tail) { room = 1 + q->mask - q->head; } /* how much fits to top of queue */

        int n_read = (q->head - q->tail) & q->mask;
        total_bytes_read += n_read;
        unsigned int test_start = (trigger_start + 2 * trigger_level);
        unsigned int test_start_index = test_start & q->mask;
        if (total_bytes_read < test_start) {
            q->tail = (q->tail + n_read) & q->mask;
            printf("%d bytes read\n", total_bytes_read);
            sleep(1);
            continue;
        }

        if (pattern_trapped == 0) { /* trap expected pattern */
            q->tail = trigger_start & q->mask;
            for (int i = 0; i < trigger_level; ++i) {
                pattern_buff[i] = q->buff[q->tail];
                q->tail = (q->tail + 1) & q->mask;
            }
            pattern_trapped = 1;
        }

        while (q->tail != q->mask) {
            uint8_t byte1 = q->buff[q->tail & q->mask];
            uint8_t byte2 = pattern_buff[index];
            index = (index + 1) & 1023; /* JSV TODO DEBUG */
            q->tail = (q->tail + 1) & q->mask;
            uint8_t diff = byte1 ^ byte2;
            int n_errors = 0;
            uint8_t mask = 0x01;
            printf("mask, diff = %x %x = %x ^ %x\n", mask, diff, byte1, byte2);
            for (int j = 0; j < 8; ++j, mask <<= 1) {
                if (mask & diff) { ++n_errors; }
            }
            ++error_hist[n_errors];
            bit_errors += n_errors;
            if (n_errors) { ++byte_errors; }

            time_t now = time(0);
            if (now && (now > next_time)) {
                next_time = now + 1;
                printf("statistics: total bytes read %6d. byte errors = %6d. bit errors = %6d => %5d %5d %5d %5d %5d %5d %5d %5d %5d\n",
                       total_bytes_read, byte_errors, bit_errors, error_hist[0],
                       error_hist[1], error_hist[2], error_hist[3], error_hist[4], error_hist[5], error_hist[6], error_hist[7], error_hist[8]);
            }

        }

#if 0
        /* accumulate statistics */
        for (int i = 0; i < n_read; ++i) {
            uint8_t byte1 = q->buff[(q->head + i - 2 * trigger_level) & q->mask];
            uint8_t byte2 = q->buff[(q->head + i - 1 * trigger_level) & q->mask];
            uint8_t diff = byte1 ^ byte2;
            int n_errors = 0;
            uint8_t mask = 0x01;
            printf("mask, diff = %x %x = %x ^ %x\n", mask, diff, byte1, byte2);
            for (int j = 0; j < 8; ++j, mask <<= 1) {
                if (mask & diff) { ++n_errors; }
            }
            ++error_hist[n_errors];
            bit_errors += n_errors;
            if (n_errors) { ++byte_errors; }
        }
        q->tail = (q->tail + n_read) & q->mask;
        // printf("%d characters read thus far\n", q->head);
#endif

    }

    pthread_join(rx_thread, NULL);

    return 0;
}
