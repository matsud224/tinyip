#include "ip.h"
#include "arduino_app.h"

//192.168.0.10
uint8_t ipaddr[IP_ADDR_LEN]={0xC0,0xA8,0x00,0x0A};

uint8_t* get_ipaddr(){
	return ipaddr;
}

void ip_task(intptr_t exinf) {

}

void ip_receive(ether_flame *flm){

}
