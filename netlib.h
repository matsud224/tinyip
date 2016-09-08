#pragma once

#include <kernel.h>

#include <stdint.h>


#define SOCK_UNUSED 0
#define SOCK_STREAM 1 //TCP
#define SOCK_DGRAM 2 //UDP

int socket(int type, ID ownertsk, ID ownersem);
int bind(int s, uint16_t my_port);
int close(int s);
int sendto(int s, const char *msg, uint32_t len, int flags, uint8_t to_addr[], uint16_t to_port);
uint32_t recvfrom(int s, char *buf, uint32_t len, int flags, uint8_t from_addr[], uint16_t *from_port);
