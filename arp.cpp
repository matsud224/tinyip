#include "arp.h"
#include "ethernet.h"
#include "arduino_app.h"


void arp_receive(int size, char *buf){
	ether_hdr *ehdr = (ether_hdr*)buf;
	ether_arp *earp = (ether_arp*)(ehdr+1);

	//正しいヘッダかチェック
	if(size < sizeof(ether_hdr)+sizeof(ether_arp)
		earp->arp_hrd != ARPHRD_ETHER ||
		earp->arp_pro != ETHERTYPE_IP ||
		earp->arp_hln != ETHER_ADDR_LEN || earp->arp_pln != 4 ||
		(earp->arp_op != ARPOP_REQUEST && earp->arp_op !=ARPOP_REPLY) ){
		delete [] buf;
		return;
	}

	switch(earp->arp_op){
	case ARPOP_REQUEST:

		break;
	case ARPOP_REPLY:

		break;
	}
}

void arp_task(intptr_t exinf) {

}
