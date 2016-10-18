#pragma once

#include <kernel.h>

#include <stdint.h>

#include "protohdr.h"

#define SOCK_UNUSED 0
#define SOCK_RESERVED 1
#define SOCK_STREAM 2 //TCP
#define SOCK_DGRAM 3 //UDP

#define TIMEOUT_NOTUSE TMO_FEVR


int socket(int type);
int bind(int s, uint16_t my_port);
int close(int s);
int sendto(int s, const char *msg, uint32_t len, int flags, uint8_t to_addr[], uint16_t to_port);
int recvfrom(int s, char *buf, uint32_t len, int flags, uint8_t from_addr[], uint16_t *from_port, TMO timeout);
int connect(int s, uint8_t to_addr[], uint16_t to_port, TMO timeout);
int listen(int s, int backlog);
int accept(int s, uint8_t client_addr[], uint16_t *client_port, TMO timeout);
int send(int s, const char *msg, uint32_t len, int flags, TMO timeout);
int recv(int s, char *buf, uint32_t len, int flags, TMO timeout);
int recv_line(int s, char *buf, uint32_t len, int flags, TMO timeout);

int find_unusedsocket(void);
