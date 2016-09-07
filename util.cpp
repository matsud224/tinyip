#include <stdint.h>
#include <util.h>
#include <cstdio>
#include "util.h"
#include "ip.h"

using namespace std;

char *macaddr2str(uint8_t ma[]){
	static char str[18];
	sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
		ma[0], ma[1], ma[2], ma[3], ma[4], ma[5]);
	return str;
}

char *ipaddr2str(uint8_t ia[]){
	static char str[16];
	sprintf(str, "%d.%d.%d.%d",
		ia[0], ia[1], ia[2], ia[3]);
	return str;
}

uint64_t macaddr2uint64(const uint8_t mac[]){
	uint64_t val = 0;
	for(int i=ETHER_ADDR_LEN-1, j=8; i>=0; i--, j*=2){
		val |= ((uint64_t)mac[i]) << j;
	}
	return val;
}

uint32_t ipaddr2uint32(const uint8_t ip[]){
	uint64_t val = 0;
	for(int i=IP_ADDR_LEN-1, j=8; i>=0; i--, j*=2){
		val |= ((uint64_t)ip[i]) << j;
	}
	return val;
}
