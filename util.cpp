#include "mbed.h"

#include <stdint.h>
#include <cstdio>

#include "util.h"
#include "protohdr.h"

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

uint16_t checksum(uint16_t *data, int len){
	uint32_t sum = 0;
	for(; len>1;len-=2){
		sum+=*data++;
		if(sum &0x80000000)
			sum=(sum&0xffff)+(sum>>16);
	}
	if(len == 1){
		uint16_t i=0;
		*(uint8_t*)(&i)= *(uint8_t*)data;
		sum+=i;
	}
	while(sum>>16)
		sum=(sum&0xffff)+(sum>>16);

	return ~sum;
}

void ipaddr_hostpart(uint8_t *dst, uint8_t *addr, uint8_t *mask){
	for(int i=0; i<IP_ADDR_LEN; i++)
		dst[i] = addr[i] & ~mask[i];
}

void ipaddr_networkpart(uint8_t *dst, uint8_t *addr, uint8_t *mask){
	for(int i=0; i<IP_ADDR_LEN; i++)
		dst[i] = addr[i] & mask[i];
}

DigitalOut led_blue(LED_BLUE);
DigitalOut led_red(LED_RED);
DigitalOut led_green(LED_GREEN);
DigitalOut led_user(LED_USER);

void mcled_change(int color){
	led_blue = color & 0x1;
	led_green = color & 0x2;
	led_red = color & 0x4;
}

void redled_off(){
	led_user = 1;
}

void redled_on(){
	led_user = 0;
}
