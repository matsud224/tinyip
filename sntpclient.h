#pragma once

#include <kernel.h>

#include <stdint.h>

struct timestamp{
	uint32_t sec;
	uint32_t pico;
};

void start_sntpclient(void);
int sntp_gettime(uint8_t ipaddr[], timestamp *ts, TMO timeout);
