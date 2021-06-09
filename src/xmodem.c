#include <stdint.h>
#include <string.h>

#include "xmodem.h"

static uint8_t hex_ascii_lut[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

static uint8_t pretty_buff[(1024 + 5 + 128) * 2]; /* x2 to accommodate purdy and some formatting characters */

static unsigned int pretty_buffer(uint8_t * dst, uint8_t const * const src, unsigned int n) {
    unsigned int index = 0;
    for (unsigned int i = 0; i < n; ++i) {
        dst[index++] = hex_ascii_lut[(src[i] >> 4) & 0xf];
        dst[index++] = hex_ascii_lut[(src[i] >> 0) & 0xf];
        if (i && ((i & 0x1f) == 0)) { dst[index++] = '\n'; dst[index++] = '\r'; }
    }
    return index;
}

static uint16_t crc16(uint8_t const * const ptr, int count) {
    const uint16_t polynomial = 0x1021;
    uint16_t crc = 0;
    for (int j = 0; j < count; ++j) {
        crc = crc ^ (int) ptr[j] << 8; /* loop 8x or just expand inline */
        if (crc & 0x8000) { crc = (crc << 1) ^ polynomial; } else { crc = (crc << 1); }
        if (crc & 0x8000) { crc = (crc << 1) ^ polynomial; } else { crc = (crc << 1); }
        if (crc & 0x8000) { crc = (crc << 1) ^ polynomial; } else { crc = (crc << 1); }
        if (crc & 0x8000) { crc = (crc << 1) ^ polynomial; } else { crc = (crc << 1); }
        if (crc & 0x8000) { crc = (crc << 1) ^ polynomial; } else { crc = (crc << 1); }
        if (crc & 0x8000) { crc = (crc << 1) ^ polynomial; } else { crc = (crc << 1); }
        if (crc & 0x8000) { crc = (crc << 1) ^ polynomial; } else { crc = (crc << 1); }
        if (crc & 0x8000) { crc = (crc << 1) ^ polynomial; } else { crc = (crc << 1); }
    }
    return crc;
}

#if 0
    int i;
    while (--count >= 0)
    {
        crc = crc ^ (int) *ptr++ << 8;
        int i = 8;
        do {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ polynomial;
            } else {
                crc = (crc << 1);
            }
        } while(--i);
    }
    return (crc);
}

#endif

#ifdef DEBUG
#define XMODEM_SOH        ('1')
#define XMODEM_STX        ('2')
#define XMODEM_EOT        ('E')
#define XMODEM_ACK        ('A')
#define XMODEM_NAK        ('N')
#define XMODEM_CAN        ('c')
#define XMODEM_CTRLZ      ('Z')
#define XMODEM_NULL       ('0')
#define XMODEM_C          ('C')
#endif

/* Delays/Timeouts */
#define XMODEM_DELAY_TOKEN (100)
#define XMODEM_DELAY_1S (1000)

/* Max Retransmissions and Retries */
#ifndef DEBUG
#define XMODEM_MAX_RETRANS     (25)
#define XMODEM_MAX_RETRY       (15)
#endif

#ifdef DEBUG
#define XMODEM_MAX_RETRANS     (25000)
#define XMODEM_MAX_RETRY       (15000)
#endif

#define XMODEM_1K_BUFF_SIZE	(1024)
#define XMODEM_BUFF_SIZE (128)
#define XMODEM_MAX_PACKET_SIZE (XMODEM_1K_BUFF_SIZE + 5)

/* Xmodem Streambuffer size and trigger level for RX INT */
#define XMODEM_STREAM_BUFF_SIZE	         (XMODEM_MAX_PACKET_SIZE*2)
#define XMODEM_STREAM_BUFF_TRIGGER_LEVEL (1)

/* Receiver timeout value in baud */
#define XMODEM_RTO_VALUE                     (100)

/* Buffer to receive/transmit file */
static uint8_t xmodem_holding_buffer[XMODEM_MAX_PACKET_SIZE];

static uint16_t buffer_index = 0;

#ifndef DEBUG
static int read_from_file(char const * const filename, uint32_t offset, void *p, unsigned int n)
{
	static int packet_sequence = 0;
	memset(p, 0x41 + packet_sequence, n);
	packet_sequence = (packet_sequence + 1) & 0xf; /* repeat pattern after 16 */
	return n;
}
#endif

#ifdef DEBUG
static int read_from_file(GenericDevice *dev, size_t offset, void *p, unsigned int n)
{
    dev->recv(dev->fd, p, n);
    return n;
}
#endif

#ifndef DEBUG
static int get_filesize(char const * const filename)
{
	return 4096;
}
#endif

#ifdef DEBUG
static int get_filesize(char const * const filename)
{
    FIL xFileDesc;
    if(f_open(&xFileDesc, filename, FA_OPEN_EXISTING | FA_READ) != FR_OK) { return ERROR_CODE_FILE_ACCESS; }
    int file_size = f_size(&xFileDesc); /* there are no error codes */
    if(f_close(&xFileDesc) != FR_OK) { return ERROR_CODE_FILE_ACCESS; }
    return file_size;
}
#endif

int xmodem_recv(GenericDevice *src, GenericDevice *dst, XmodemOptions *options, int *errors)
{
    src->putc(&src->fd, options->packet_size_code, options->timeout_ms);

    const unsigned int header_size = 3;
    const unsigned int footer_size = (options->crc_checksum == CHECKSUM_OPTION_CRC) ? 2 : 1;
    uint8_t expected_packet_id = 1;

    const unsigned int expected_packet_size = options->packet_size + header_size + footer_size;
    unsigned int n_read, n_write;
    int success, done = 0;
    do {
        while (1) { /* loop over incoming packets */
            success = 0;
            n_read = 0;
            int remaining = expected_packet_size;
            int index = 0;
            do {
                n_read = src->recv(&src->fd, &xmodem_holding_buffer[index], remaining, 0, options->timeout_ms);
                remaining -= n_read;
                index += n_read;
            } while ((remaining > 0) && (1)); /* TODO timeout */
            uint8_t packet_id = xmodem_holding_buffer[1];
            do {
                if ((n_read == 1) && (xmodem_holding_buffer[0] == XMODEM_EOT)) {
                    n_write = src->putc(&src->fd, XMODEM_CAN, options->timeout_ms);
                    n_write = src->putc(&src->fd, XMODEM_CAN, options->timeout_ms);
                    n_write = src->putc(&src->fd, XMODEM_CAN, options->timeout_ms);
                    done = 1;
                    break;
                }
                if (n_read != expected_packet_size) { break; }
                if (packet_id != expected_packet_id) { break; }
                if (xmodem_holding_buffer[2] != ~packet_id) { break; }
                uint16_t expected_crc = crc16(&xmodem_holding_buffer[3], options->packet_size);
                uint8_t crc_hi = xmodem_holding_buffer[3 + options->packet_size + 0];
                uint8_t crc_lo = xmodem_holding_buffer[3 + options->packet_size + 1];
                uint16_t crc = (crc_hi << 8) | (crc_lo);
                if (crc != expected_crc) { break; }
                success = 1;
            } while (0);
            expected_packet_id = packet_id + 1;
            src->putc(&src->fd, success ? XMODEM_ACK : XMODEM_NAK, options->timeout_ms);
        }
    } while (success && (done == 0));
}

/*
 * @param
 *     options->packet_size_code = { XMODEM_SOH (128-byte packets), XMODEM_STX (1024-byte packets)
 */
int xmodem_send(GenericDevice *src, GenericDevice *dst, XmodemOptions *options, int *errors)
{
    uint8_t packet_id = 1;
    uint32_t file_size;
    unsigned int payload_size; /* amount of true payload for current packet */

    const uint8_t packet_size = (options->packet_size_code == XMODEM_STX) ? XMODEM_1K_BUFF_SIZE: XMODEM_BUFF_SIZE;

    if ((file_size = get_filesize(src->name)) == 0) { return -1; }

    options->crc_checksum = CHECKSUM_OPTION_UNK;
    uint8_t byte;
    unsigned int failure = 0;

	/* NAK = no crc, C = CRC. setting valid for whole session */
    for (int retry = 0; (options->max_retries == 0) || (retry < options->max_retries); ++retry)
    {
        if (dst->getc(&dst->fd, &byte, options->timeout_ms)) {
            switch (byte)
            {
                case XMODEM_CCC: { options->crc_checksum = CHECKSUM_OPTION_CRC; }
                break;

                case XMODEM_NAK: { options->crc_checksum = CHECKSUM_OPTION_SUM; }
                break;

                case XMODEM_CAN: {
                    if (dst->getc(&dst->fd, &byte, options->timeout_ms)) {
                        if (byte == XMODEM_CAN) {
                            dst->putc(&dst->fd, XMODEM_ACK, options->timeout_ms);
                            failure = 1;
                        }
                    }
                }
                break;

                default:
                    break;
            }
        }
        if (options->crc_checksum) { break; }
    }

    if (failure) {
    }

    if ((options->crc_checksum != CHECKSUM_OPTION_CRC) && (options->crc_checksum != CHECKSUM_OPTION_SUM)) {
        for (int i = 0; i < 3; ++i) { dst->putc(&dst->fd, XMODEM_CAN, options->timeout_ms); }
        failure = 1;
    }

    if (failure) {
    }

    uint8_t * const header = &xmodem_holding_buffer[0];
    uint8_t * const payload = &xmodem_holding_buffer[3];
    uint8_t * const footer = &xmodem_holding_buffer[options->packet_size + 3];
    const unsigned int n_bytes = packet_size + 4 + ((options->crc_checksum == CHECKSUM_OPTION_CRC) ? 1 : 0);
    unsigned int total_retries = 0;
    uint32_t bytes_sent = 0;
    for (;;)
    {
        header[0] = options->packet_size_code;
        header[1] = packet_id;
        header[2] = ~packet_id;

        /* how much payload to send this packet. packet size always 1024 */
        payload_size = file_size - bytes_sent;
        if (payload_size > options->packet_size) { payload_size = options->packet_size; }

        if (payload_size == 0) { /* we're done sending whole packets. finish, clean up and go home */
            byte = XMODEM_NAK; /* set to decoy invalid value */
            for (int retry = 0; retry < options->max_retries; ++retry) {
                dst->putc(&dst->fd, XMODEM_EOT, options->timeout_ms);
                if (dst->getc(&dst->fd, &byte, options->timeout_ms)) {
                    if (byte == XMODEM_ACK) { break; }
                }
            }
            return (byte == XMODEM_ACK) ? 0 : -1;
        }

       	int local_data_read = 0;
       	int remaining = payload_size - local_data_read;
        do {
            int n_read = read_from_file(src->name, bytes_sent, &payload[local_data_read], remaining);
            if (n_read < 0)
            {
                for (int i = 0; i < 3; ++i) { dst->putc(&dst->fd, XMODEM_CAN, options->timeout_ms); }
                failure = 1;
            }
            remaining -= n_read;
            local_data_read += n_read;
        } while (local_data_read < payload_size);

        if (failure) {
        }

        if (payload_size < packet_size) { /* pad to 1024 bytes with 0x1a */
        	memset(&payload[payload_size], XMODEM_CTZ, packet_size - payload_size);
        }

        /* crc or checksum at end depends on global NACK or C at beginning of session */
        if (options->crc_checksum == 1) {
            uint16_t crc = crc16(payload, options->packet_size);
            footer[0] = (crc >> 8) & 0xff;
            footer[1] = crc & 0xff;
        } else {
            uint8_t checksum = 0;
            for (int i = 0; i < options->packet_size; ++i) { checksum += payload[i]; }
            footer[0] = checksum;
        }

        unsigned int success = 0;
        unsigned int retries = 0;

        for (int retry = 0; retry < options->max_retransmissions; ++retry)
        {
#ifdef DEBUG
            pretty(pretty_buff, ucBuff, n_bytes);
            _Xmodem_OutBytes(pretty_buff, 2 * n_bytes);
#endif

#ifndef DEBUG
            while (dst->getc(&dst->fd, &byte, options->timeout_ms)) { ; } /* flush away bytes in rx queue */

            dst->send(&dst->fd, xmodem_holding_buffer, n_bytes, options->timeout_ms); /* send packet */
#endif

            byte = 0;
            if (dst->getc(&dst->fd, &byte, options->timeout_ms)) /* wait for confirm (ACK) or retry */
            {
                switch (byte)
                {
                case XMODEM_ACK:
                    ++packet_id;
                    bytes_sent += payload_size;
                    success = 1;
                    break;

                case XMODEM_CAN: /* received one cancel. need another to confirm */
                    if (dst->getc(&dst->fd, &byte, options->timeout_ms) && (byte == XMODEM_CAN)) {
                        dst->putc(&dst->fd, XMODEM_ACK, options->timeout_ms);
                        failure = 1;
                    }
                    break;

               case XMODEM_NAK: /* resubmit on anything but an ACK or CANCEL */
               default:
            	   ++retries;
                   break;
               }
            }

            if (success || failure) { break; }

        } /* retry sending packet */

        total_retries += retries;

        if (success == 0) {
            for (int i = 0; i < 3; ++i) { dst->putc(&dst->fd, XMODEM_CAN, options->timeout_ms); }
            failure = 1;
        }

        if (failure) { break; }
    }

    if (errors) { *errors = total_retries; }

    return (*errors);
}
