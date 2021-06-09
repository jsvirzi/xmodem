#include <sys/select.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

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
        printf("waiting for %d ...\n", args->fd);
        int res = select (args->fd + 1, &fds, NULL, NULL, NULL);
        printf("and it came and it went ...\n");
        if (res && FD_ISSET(args->fd, &fds)) {
            unsigned int room;
            if (q->head >= q->tail) { room = 1 + q->mask - q->head; } /* how much fits to top of queue */
            else { room = q->tail - q->head - 1; }
            if (room) {
                int n_read = read(args->fd, &q->buff[q->head], room);
                printf("and i saw %d (room = %d)...\n", n_read, room);
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
