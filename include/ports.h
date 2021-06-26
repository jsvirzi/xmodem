#ifndef PORTS_H
#define PORTS_H

int initialize_serial_port(const char *dev, unsigned int baud, unsigned int canonical, int parity, int min_chars);

typedef struct {
    int server_fd;
    unsigned int max_clients;
    int client_fd;
    int *client_fds;
    int portno;
} TcpServerInfo;

typedef struct {
    int portno;
    int client_fd;
} TcpClientInfo;

void initialize_tcp_server_info(TcpServerInfo *info);
void initialize_tcp_client_info(TcpClientInfo *info);
int initialize_server_socket(TcpServerInfo *info, unsigned int portno);
int initialize_client_socket(const char *addr, unsigned int portno);

#endif
