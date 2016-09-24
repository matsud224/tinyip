#pragma once

#include <kernel.h>

#include <stdint.h>


#define SOCK_UNUSED 0
#define SOCK_STREAM 1 //TCP
#define SOCK_DGRAM 2 //UDP

#define EBADF -1
#define EAGAIN -2
#define ETIMEOUT -3
#define EMSGSIZE -4
#define ECONNEXIST -5
#define ECONNNOTEXIST -6
#define ECONNCLOSING -7
#define ENOTLISITENING -8
#define ECONNRESET -9
#define ECONNREFUSED -10

#define TIMEOUT_NOTUSE TMO_FEVR


int socket(int type, ID recvsem, ID sendsem, ID infosem);
int bind(int s, uint16_t my_port);
int close(int s);
int sendto(int s, const char *msg, uint32_t len, int flags, uint8_t to_addr[], uint16_t to_port);
int recvfrom(int s, char *buf, uint32_t len, int flags, uint8_t from_addr[], uint16_t *from_port, TMO timeout);
int connect(int s, uint8_t to_addr[], uint16_t to_port, TMO timeout);
int listen(int s, int backlog);
int accept(int s, uint8_t client_addr[], uint16_t *client_port, TMO timeout);
int send(int s, char *msg, uint32_t len, int flags, TMO timeout);
int recv(int s, char *buf, uint32_t len, int flags, TMO timeout);

int find_unusedsocket();
void copy_socket(int src, int dest);
