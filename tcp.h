#pragma once

#include <stdint.h>

#define LITTLE_ENDIAN

struct tcp_hdr{
	uint16_t th_sport;
	uint16_t th_dport;
	uint32_t th_seq;
	uint32_t th_ack;
#ifdef BIG_ENDIAN
	unsigned th_off:4,th_x2:4;
#endif
#ifdef LITTLE_ENDIAN
	unsigned th_x2:4,th_off:4;
#endif // BYTE_ORDER
	uint8_t th_flags;
	uint16_t th_win;
	uint16_t th_sum;
	uint16_t th_urp;
};

#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PUSH 0x08
#define TH_ACK 0x10
#define TH_URG 0x20
