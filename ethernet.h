#pragma once

#include <stdint.h>
#define ETHER_ADDR_LEN 6

struct ether_hdr{
	uint8_t ether_dhost[ETHER_ADDR_LEN];
	uint8_t ether_shost[ETHER_ADDR_LEN];
	uint16_t ether_type;
};

#define ETHERTYPE_IP	0x0800
#define ETHERTYPE_ARP	0x0806

