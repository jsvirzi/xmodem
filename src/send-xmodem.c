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

/*
 * sends a file over xmodem protocol
 * runs as either TCP server or UART
 */

int main(int argc, char **argv) {
    XmodemOptions options;
    GenericDevice i_device, o_device;

    o_device.name[0] = 0;
    i_device.name[0] = 0;

    /* read from file */
    i_device.recv = recv_from_file;
    i_device.size = size_from_file;

    /* and send out to port */
    o_device.recv = recv_from_desc;
    o_device.send = send_over_desc;
    o_device.getc = getc_from_desc;
    o_device.putc = putc_over_desc;

    int verbose = 0;
    int port = 0;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-i") == 0) {
            snprintf(i_device.name, sizeof (i_device.name), "%s", argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0) {
            snprintf(o_device.name, sizeof (o_device.name), "%s", argv[++i]);
        } else if (
            (strcmp(argv[i], "-p") == 0) ||
            (strcmp(argv[i], "--port") == 0)) {
            port = atoi(argv[++i]);
        }
    }

    /* open device */
    TcpServerInfo tcp_server_info;
    tcp_server_info.infrastructure.fd = 0;
    o_device.fd = 0;

    if (strlen(i_device.name)) {
        i_device.fd = open(i_device.name, S_IREAD, O_RDWR);
    }

    if (strlen(o_device.name)) {
        o_device.fd = initialize_serial_port(o_device.name, 115200, 0, 0, 0);
    } else if (port) {
        initialize_tcp_server_info(&tcp_server_info);
        initialize_server_socket(&tcp_server_info, port);
        server_task(&tcp_server_info);
    } else {
        printf("specify either device (/dev/ttyUSB0) or port number for TCP\n");
        return 1;
    }

    /*** receive machine ***/
    RxLooperArgs rx_looper_args;
    Queue rx_queue;
    memset(&rx_looper_args, 0, sizeof(RxLooperArgs));

    unsigned int run = 1;
    rx_looper_args.queue = &rx_queue;
    rx_looper_args.run = &run;
    rx_looper_args.verbose = verbose;
    rx_looper_args.fd = o_device.fd ? o_device.fd : tcp_server_info.infrastructure.fd;

    pthread_t rx_thread;

    pthread_create(&rx_thread, NULL, rx_looper, (void *) &rx_looper_args); /* create thread */

    int errors = 0;
    options.timeout_ms = 100000;
    options.max_retries = 25000;
    options.max_retransmissions = 25000;
    options.packet_size_code = XMODEM_CCC;
    options.packet_size = 1024;
    const char *start_command = "<xmodem r RADIO9.BIN\r";
    write(o_device.fd, start_command, sizeof (start_command) - 1);
    xmodem_send(&o_device, &i_device, &options, &errors);

    pthread_join(rx_thread, NULL);

    return 0;
}
