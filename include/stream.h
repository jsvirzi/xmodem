#ifndef STREAM_H
#define STREAM_H

#include <stdint.h>

typedef struct {
    unsigned int head, tail, mask;
    uint8_t *buff;
} Queue;

typedef struct RxLooperArgs {
    Queue *queue; /* circular queue used to hold/extract data from the stream */
    char const *name; /* name of device or file */
    unsigned int *run;
    int fd; /* relevant file descriptors */
    unsigned int verbose, debug;
    unsigned int loop_pace; /* milliseconds to pause between thread loops */
} RxLooperArgs;

void *rx_looper(void *ext);
void *server_task(void *arg);
void *client_task(void *arg);
int recv_from_file(void *handle, uint8_t *b, unsigned int n, unsigned int offset, unsigned int timeout);
int size_from_file(void *handle, unsigned int timeout);
int recv_from_desc(void *handle, uint8_t *b, unsigned int n, unsigned int offset, unsigned int timeout);
int getc_from_desc(void *handle, uint8_t *byte, unsigned int timeout);
int send_over_desc(void *handle, uint8_t const * const b, unsigned int n, unsigned int timeout);
int putc_over_desc(void *handle, uint8_t byte, unsigned int timeout);

#endif
