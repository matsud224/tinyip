#include "mbed.h"

#include <stdint.h>
#include <cstdio>

#include "util.h"
#include "protohdr.h"


#define MIN(x,y) ((x)<(y)?(x):(y))

using namespace std;


const char *halfkana_utf8_table[] = {
	"\xEF\xBD\xA1",
	"\xEF\xBD\xA2",
	"\xEF\xBD\xA3",
	"\xEF\xBD\xA4",
	"\xEF\xBD\xA5",
	"\xEF\xBD\xA6",
	"\xEF\xBD\xA7",
	"\xEF\xBD\xA8",
	"\xEF\xBD\xA9",
	"\xEF\xBD\xAA",
	"\xEF\xBD\xAB",
	"\xEF\xBD\xAC",
	"\xEF\xBD\xAD",
	"\xEF\xBD\xAE",
	"\xEF\xBD\xAF",
	"\xEF\xBD\xB0",
	"\xEF\xBD\xB1",
	"\xEF\xBD\xB2",
	"\xEF\xBD\xB3",
	"\xEF\xBD\xB4",
	"\xEF\xBD\xB5",
	"\xEF\xBD\xB6",
	"\xEF\xBD\xB7",
	"\xEF\xBD\xB8",
	"\xEF\xBD\xB9",
	"\xEF\xBD\xBA",
	"\xEF\xBD\xBB",
	"\xEF\xBD\xBC",
	"\xEF\xBD\xBD",
	"\xEF\xBD\xBE",
	"\xEF\xBD\xBF",
	"\xEF\xBE\x80",
	"\xEF\xBE\x81",
	"\xEF\xBE\x82",
	"\xEF\xBE\x83",
	"\xEF\xBE\x84",
	"\xEF\xBE\x85",
	"\xEF\xBE\x86",
	"\xEF\xBE\x87",
	"\xEF\xBE\x88",
	"\xEF\xBE\x89",
	"\xEF\xBE\x8A",
	"\xEF\xBE\x8B",
	"\xEF\xBE\x8C",
	"\xEF\xBE\x8D",
	"\xEF\xBE\x8E",
	"\xEF\xBE\x8F",
	"\xEF\xBE\x90",
	"\xEF\xBE\x91",
	"\xEF\xBE\x92",
	"\xEF\xBE\x93",
	"\xEF\xBE\x94",
	"\xEF\xBE\x95",
	"\xEF\xBE\x96",
	"\xEF\xBE\x97",
	"\xEF\xBE\x98",
	"\xEF\xBE\x99",
	"\xEF\xBE\x9A",
	"\xEF\xBE\x9B",
	"\xEF\xBE\x9C",
	"\xEF\xBE\x9D",
	"\xEF\xBE\x9E",
	"\xEF\xBE\x9F",
};

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
	int len = len1 + len2;
	uint16_t *data = data1;
	int complen = 0;
	for(; len>1;len-=2){
		if(complen == len1-1){
			//data1側がのこり1byte
			sum+= ((uint8_t)*data) | ((*data2)<<8);
			complen = len1+1;
			data = &(data2[1]);
		}else{
			sum+=*data++;
		}
		complen+=2;
		if(sum &0x80000000)
			sum=(sum&0xffff)+(sum>>16);
		if(complen == len1)
			data = data2;
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

//もし途中に長さが奇数の部分があっても、次の領域にまたがって計算するため問題ない
uint16_t checksum_hdrstack(hdrstack *hs){
	uint32_t len = hdrstack_totallen(hs);
	uint32_t sum = 0;
	uint32_t thisstack_len = 0;
	uint16_t *data = (uint16_t*)hs->buf;
	for(; len>1;len-=2){
		if(thisstack_len == hs->size-1){
			//のこり1byte
			sum+= ((uint8_t)(*data)) | ((hs->next->buf[0])<<8);
			hs = hs->next;
			data = (uint16_t*)(&(hs->buf[1]));
			thisstack_len = 1;
		}else{
			sum+=*data++;
			thisstack_len+=2;
		}

		if(thisstack_len == hs->size){
			hs = hs->next;
			data = (uint16_t*)hs->buf;
			thisstack_len = 0;
		}
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


uint32_t hdrstack_totallen(hdrstack *target){
	uint32_t len=0;
	while(target!=NULL){
		len+=target->size;
		target=target->next;
	}
	return len;
}

void hdrstack_cpy(char *dst, hdrstack *src, uint32_t start, uint32_t len){
	uint32_t remain = start+1;
	hdrstack *ptr = src;

	while(remain > ptr->size){
		remain -= ptr->size;
		ptr = ptr->next;
	}
	//開始位置が分かった(ptr->buf+(remain-1))

	uint32_t remain_copy = len; //コピーの残りオクテット数
	uint32_t offset = 0;
	while(remain_copy > 0){
		memcpy(dst+offset, ptr->buf+(remain-1), MIN(remain_copy, ptr->size-(remain-1)));
		remain_copy -= MIN(remain_copy, ptr->size-(remain-1));
		offset += MIN(remain_copy, ptr->size-(remain-1));
		ptr = ptr->next;
		remain = 1;
	}
	return;
}

const char *halfkana_sjis_to_utf8(char c){
	if(c<0xA1 || c>0xDF)
		return NULL;
	else
		return halfkana_utf8_table[c-0xA1];
}

bool is_halfkana(char c){
	return c>=0xA1 && c<=0xDF;
}
