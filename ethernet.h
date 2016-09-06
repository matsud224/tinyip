#include <stdint.h>
#define ETHER_ADDR_LEN 6

struct ether_hdr{
	uint8_t ether_dhost[8];
	uint8_t ether_shost[8];
	uint16_t ether_type;
};

#define ETHERTYPE_IP	0x0800
#define ETHERTYPE_ARP	0x0806
