#pragma once

#include <stdint.h>
#include "ethernet.h"

#define LITTLE_ENDIAN

#define IP_ADDR_LEN 4

uint8_t* get_ipaddr();


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
	uint32_t ip_src;
	uint32_t ip_dst;
};

#define IP_RF 0x8000
#define IP_DE 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff


void ip_receive(ether_flame *flm);

