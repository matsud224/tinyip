#include "ethernet.h"

struct arp_hdr{
	uint16_t ar_hrd;
	uint16_t ar_pro;
	uint8_t	ar_hln;
	uint8_t ar_pln;
	uint16_t ar_op;
};

struct ether_arp{
	struct arp_hdr ea_hdr;
	uint8_t arp_sha[ETHER_ADDR_LEN];
	uint8_t arp_spa[4];
	uint8_t arp_tha[ETHER_ADDR_LEN];
	uint8_t arp_tpa[4];
};
