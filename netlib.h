#pragma once

#include <kernel.h>

#include <stdint.h>


#define SOCK_UNUSED 0
#define SOCK_STREAM 1 //TCP
#define SOCK_DGRAM 2 //UDP

#define EBADF -1
#define EAGAIN -2
#define ETIMEOUT -3

#define TIMEOUT_NOTUSE TMO_FEVR


int socket(int type, ID drsem, ID srsem, ID sssem);
int bind(int s, uint16_t my_port);
int close(int s);
int sendto(int s, const char *msg, uint32_t len, int flags, uint8_t to_addr[], uint16_t to_port);
int32_t recvfrom(int s, char *buf, uint32_t len, int flags, uint8_t from_addr[], uint16_t *from_port, TMO timeout);
