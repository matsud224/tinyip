#include "mbed.h"

#include <stdint.h>
#include <cstdio>

#include "util.h"
#include "protohdr.h"

#define MIN(x,y) ((x)<(y)?(x):(y))

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

/*
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
*/

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

uint16_t checksum2(uint16_t *data1, uint16_t *data2, int len1, int len2){
	uint32_t sum = 0;
	for(; len1>1;len1-=2){
		sum+=*data1++;
		if(sum &0x80000000)
			sum=(sum&0xffff)+(sum>>16);
	}
	for(; len2>1;len2-=2){
		sum+=*data2++;
		if(sum &0x80000000)
			sum=(sum&0xffff)+(sum>>16);
	}
	if(len2 == 1){
		uint16_t i=0;
		*(uint8_t*)(&i)= *(uint8_t*)data2;
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

uint16_t udp_checksum(ip_hdr *iphdr, udp_hdr *uhdr){
	udp_pseudo_hdr pseudo;
	memcpy(pseudo.up_src, iphdr->ip_src, IP_ADDR_LEN);
	memcpy(pseudo.up_dst, iphdr->ip_dst, IP_ADDR_LEN);
	pseudo.up_type = 17;
	pseudo.up_void = 0;
	pseudo.up_len = uhdr->uh_ulen; //UDPヘッダ+UDPペイロードの長さ

	return checksum2((uint16_t*)(&pseudo), (uint16_t*)uhdr, sizeof(udp_pseudo_hdr), ntoh16(uhdr->uh_ulen));
}

uint32_t hdrstack_totallen(hdrstack *target){
	uint32_t len=0;
	while(target!=NULL){
		len+=target->size;
		target=target->next;
	}
	return len;
}

void hdrstack_cpy(char *dst, hdrstack *src, int start, int len){
	int remain = start+1;
	hdrstack *ptr = src;

	while(remain > ptr->size){
		remain -= ptr->size;
		ptr = ptr->next;
	}
	//開始位置が分かった(ptr->buf+(remain-1))

	int remain_copy = len; //コピーの残りオクテット数
	int offset = 0;
	while(remain_copy > 0){
		memcpy(dst+offset, ptr->buf+(remain-1), MIN(remain_copy, ptr->size-(remain-1)));
		remain_copy -= MIN(remain_copy, ptr->size-(remain-1));
		offset += MIN(remain_copy, ptr->size-(remain-1));
		ptr = ptr->next;
		remain = 1;
	}
	return;
}
