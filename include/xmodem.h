#ifndef XMODEM_H
#define XMODEM_H

#include <stdint.h>

typedef struct {
    int fd;
    int (*recv)(void *handle, uint8_t *dst, unsigned int n, unsigned int offset, unsigned int timeout);
    int (*send)(void *handle, uint8_t *src, unsigned int n, unsigned int timeout);
    int (*getc)(void *handle, uint8_t *ch, unsigned int timeout);
    int (*putc)(void *handle, uint8_t ch, unsigned int timeout);
    int (*size)(void *handle, unsigned int timeout);
    const char *name;
    void *handle;
} GenericDevice;

typedef struct {
    unsigned int packet_size_code; /* 1 = 128-byte packet, 2 = 1024-byte packet */
    unsigned int packet_size;
    unsigned int crc_checksum; /* 0 = crc, 1 = checksum */
    unsigned int max_retries;
    unsigned int max_retransmissions;
    unsigned int timeout_ms;
} XmodemOptions;

enum {
    CHECKSUM_OPTION_UNK = 0,
    CHECKSUM_OPTION_CRC,
    CHECKSUM_OPTION_SUM,
    CHECKSUM_OPTIONS
};

int xmodem_send(GenericDevice *src, GenericDevice *dst, XmodemOptions *options, int *errors);

#endif
