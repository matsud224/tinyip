#pragma once

#include "ethernet.h"
#include "ip.h"

struct arp_hdr{
	uint16_t ar_hrd;
	uint16_t ar_pro;
	uint8_t	ar_hln;
	uint8_t ar_pln;
	uint16_t ar_op;
};

#define arp_hrd ea_hdr.ar_hrd
#define arp_pro ea_hdr.ar_pro
#define arp_hln ea_hdr.ar_hln
#define arp_pln ea_hdr.ar_pln
#define arp_op ea_hdr.ar_op

struct ether_arp{
	struct arp_hdr ea_hdr;
	uint8_t arp_sha[ETHER_ADDR_LEN];
	uint8_t arp_spa[IP_ADDR_LEN];
	uint8_t arp_tha[ETHER_ADDR_LEN];
	uint8_t arp_tpa[IP_ADDR_LEN];
};

#define ARPHRD_ETHER 1
#define ETHERTYPE_IP 0x0800

#define ARPOP_REQUEST 1
#define ARPOP_REPLY 2

void arp_receive(ether_flame *flm);
