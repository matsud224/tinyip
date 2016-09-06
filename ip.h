#pragma once

#include <stdint.h>

#define LITTLE_ENDIAN

struct in_addr{
	uint32_t s_addr;
};

struct ip_hdr{
#ifdef BIG_ENDIAN
	unsigned ip_v:4, ip_hl:4;
#endif
#ifdef LITTLE_ENDIAN
	unsigned ip_hl:4, ip_v:4;
#endif // BIG_ENDIAN
	uint8_t ip_tos;
	uint16_t ip_len;
	uint16_t ip_id;
	uint16_t ip_off;
	uint8_t ip_ttl;
	uint8_t ip_p;
	uint16_t ip_sum;
	struct in_addr ip_src;
	struct in_addr ip_dst;
};

#define IP_RF 0x8000
#define IP_DE 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff


void ip_receive(int size, char *buf);

