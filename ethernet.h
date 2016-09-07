#pragma once

#include <stdint.h>
#define ETHER_ADDR_LEN 6



struct ether_hdr{
	uint8_t ether_dhost[ETHER_ADDR_LEN];
	uint8_t ether_shost[ETHER_ADDR_LEN];
	uint16_t ether_type;
};

struct ether_flame{
	int size;
	char *buf;
};

#define ETHERTYPE_IP	0x0800
#define ETHERTYPE_ARP	0x0806

uint8_t* get_macaddr(void);
void init_ethernet(void);
void send_ethernet(ether_flame *flm);
