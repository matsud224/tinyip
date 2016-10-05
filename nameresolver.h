#pragma once

#include "protohdr.h"

struct addrinfo {
	addrinfo *next;
	uint8_t addr[IP_ADDR_LEN];
};

int getaddrinfo(const char *node, struct addrinfo **res);
