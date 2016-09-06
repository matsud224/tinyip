#include <stdint.h>

struct udp_hdr{
	uint16_t uh_sport;
	uint16_t uh_dport;
	uint16_t uh_ulen;
	uint16_t sum;
};
