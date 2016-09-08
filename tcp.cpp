#include "arduino_app.h"

#include "tcp.h"
#include "protohdr.h"

struct tcp_pseudo_hdr{
	uint8_t tp_src[IP_ADDR_LEN];
	uint8_t tp_dst[IP_ADDR_LEN];
	uint8_t tp_void;
	uint8_t tp_type;
	uint8_t tp_len;
};

void tcp_task(intptr_t exinf) {

}


void tcp_process(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr){

}
