#include "arduino_app.h"

#include "udp.h"
#include "protohdr.h"

struct udp_pseudo_hdr{
	uint8_t up_src[IP_ADDR_LEN];
	uint8_t up_dst[IP_ADDR_LEN];
	uint8_t up_void;
	uint8_t up_type;
	uint8_t up_len;
};

void udp_task(intptr_t exinf) {

}

void udp_process(ether_flame *flm, udp_hdr *uhdr){

}
