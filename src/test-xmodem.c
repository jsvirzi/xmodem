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

typedef struct {
    unsigned int head, tail, mask;
    uint8_t *buff;
} Queue;

typedef struct RxLooperArgs {
    Queue *queue;
    unsigned int *run;
    int fd;
    unsigned int verbose, debug;
} RxLooperArgs;

/* parity = 0 (no parity), = 1 odd parity, = 2 even parity */
int initialize_serial_port(const char *dev, unsigned int baud, unsigned int canonical, int parity, int min_chars) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(fd < 0) { return fd; }
    fcntl(fd, F_SETFL, 0);
    struct termios *settings, current_settings;

    memset(&current_settings, 0, sizeof(current_settings));
    tcgetattr(fd, &current_settings);

    /* effect new settings */
    settings = &current_settings;
    cfmakeraw(settings);
    if (parity == 0) {
        settings->c_cflag &= ~(CSIZE | CRTSCTS | CSTOPB | PARENB); /* no parity, one stop bit, no cts/rts, clear size */
        settings->c_cflag |= CS8; /* eight bits */
    } else if (parity == 1) {
        settings->c_cflag &= ~(CSIZE | CRTSCTS | CSTOPB); /* no parity, one stop bit, no cts/rts, clear size */
        settings->c_cflag |= (CS8 | PARENB | PARODD); /* eight bits, odd parity */
    } else if (parity == 2) {
        settings->c_cflag &= ~(CSIZE | CRTSCTS | CSTOPB); /* no parity, one stop bit, no cts/rts, clear size */
        settings->c_cflag |= (CS8 | PARENB); /* eight bits, odd parity is clear for even parity */
    }
    settings->c_cflag |= (CLOCAL | CREAD); /* ignore carrier detect. enable receiver */
    settings->c_iflag &= ~(IXON | IXOFF | IXANY | IGNPAR | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    settings->c_iflag |= ( IGNPAR | IGNBRK);
    settings->c_lflag &= ~(ECHOK | ECHOCTL | ECHOKE);
    if (canonical) { settings->c_lflag |= ICANON; } /* set canonical */
    else { settings->c_lflag &= ~ICANON; } /* or clear it */
    settings->c_oflag &= ~(OPOST | ONLCR);
    settings->c_cc[VMIN] = min_chars;
    settings->c_cc[VTIME] = 1; /* 200ms timeout */

    cfsetispeed(settings, baud);
    cfsetospeed(settings, baud);

    tcsetattr(fd, TCSANOW, settings); /* apply settings */
    tcflush(fd, TCIOFLUSH);

    return fd;
}

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
        sigset_t orig_mask; /* TODO huh? */
        int res = pselect (args->fd + 1, &fds, NULL, NULL, NULL, &orig_mask);
        if (FD_ISSET(args->fd, &fds)) {
            unsigned int fit = (q->mask + q->tail - q->head) & q->mask;
            if (q->head >= q->tail) { fit = 1 + q->mask - q->head; } /* how much fits to top of queue */
            else { fit = q->tail - q->head - 1; }
            if (fit) {
                int n_read = read(args->fd, &q->buff[q->head], fit);
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
    if (expiry.tv_nsec > 1e9) {
        expiry.tv_nsec -= 1e9;
        ++expiry.tv_sec;
    }

    unsigned int index = 0;
    unsigned int remaining = n;
    while ((index <= n) && (timeout_expired(&expiry) == 0)) {
        int n_write = write(fd, &b[index], remaining);
        remaining -= n_write;
        index += n_write;
    }

    return index; /* how many went out */
}

int putc_over_uart(void *handle, uint8_t byte, unsigned int timeout) {
    return send_over_uart(handle, &byte, 1, timeout);
}

int main(int argc, char **argv) {
    XmodemOptions options;
    GenericDevice i_device, o_device;
    i_device.recv = recv_from_file;
    i_device.size = size_from_file;

    o_device.recv = recv_from_uart;
    o_device.send = send_over_uart;
    o_device.getc = getc_from_uart;
    o_device.putc = putc_over_uart;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0) {
            i_device.name = argv[++i];
            i_device.fd = open(i_device.name, S_IREAD, O_RDWR);
        } else if (strcmp(argv[i], "-d")) {
            o_device.name = argv[++i];
            o_device.fd = initialize_serial_port(o_device.name, 115200, 0, 0, 0);
        }
    }

    RxLooperArgs rx_looper_args;
    Queue rx_queue;
    memset(&rx_looper_args, 0, sizeof(RxLooperArgs));

    unsigned int run = 1;
    rx_looper_args.queue = &rx_queue;
    rx_looper_args.run = &run;
    pthread_t rx_thread;

    pthread_create(&rx_thread, NULL, rx_looper, (void *) &rx_looper_args); /* create thread */

    int errors = 0;
    xmodem_send(&i_device, &o_device, &options, &errors);

    while (1) {
        sleep(1);
    }

    pthread_join(rx_thread, NULL);

    return 0;
}
