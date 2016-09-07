#include <kernel.h>
#include "arp.h"
#include "ethernet.h"
#include "ip.h"
#include "util.h"
#include "arduino_app.h"
#include <cstring>
#include <map>
#include "kernel_cfg.h"

using namespace std;

map<uint32_t, uint64_t> arptable;

void arp_receive(ether_flame *flm){
	ether_hdr *ehdr = (ether_hdr*)flm->buf;
	ether_arp *earp = (ether_arp*)(ehdr+1);
	char *p=(char*)(earp->arp_tpa);
	//正しいヘッダかチェック
	if(flm->size < sizeof(ether_hdr)+sizeof(ether_arp) ||
		ntoh16(earp->arp_hrd) != ARPHRD_ETHER ||
		ntoh16(earp->arp_pro) != ETHERTYPE_IP ||
		earp->arp_hln != ETHER_ADDR_LEN || earp->arp_pln != 4 ||
		(ntoh16(earp->arp_op) != ARPOP_REQUEST && ntoh16(earp->arp_op) !=ARPOP_REPLY) ){
		goto exit;
	}

	switch(ntoh16(earp->arp_op)){
	case ARPOP_REQUEST:
		LOG("ARP REQUEST for %s", ipaddr2str(earp->arp_tpa));
		for(int i=0;i<4;i++)
			LOG("%02X",*p++);
		if(memcmp(earp->arp_tpa, get_ipaddr(),IP_ADDR_LEN)==0){
			//相手のIPアドレスとMACアドレスを登録
			arptable[ipaddr2uint32(earp->arp_spa)] = macaddr2uint64(earp->arp_sha);
            LOG("arp entry registered:\n");
            LOG("\t%s -> %s\n", ipaddr2str(earp->arp_spa), macaddr2str(earp->arp_sha));
			//パケットを改変
			memcpy(earp->arp_tha, earp->arp_sha, ETHER_ADDR_LEN);
			memcpy(earp->arp_tpa, earp->arp_spa, IP_ADDR_LEN);
			memcpy(earp->arp_sha, get_macaddr(), ETHER_ADDR_LEN);
			memcpy(earp->arp_spa, get_ipaddr(), IP_ADDR_LEN);
            earp->arp_op = hton16(ARPOP_REPLY);
            memcpy(ehdr->ether_dhost, ehdr->ether_shost, ETHER_ADDR_LEN);
            memcpy(ehdr->ether_shost, get_macaddr(), ETHER_ADDR_LEN);
            //送り返す
			send_ethernet(flm);
		}
		break;
	case ARPOP_REPLY:
		arptable[ipaddr2uint32(earp->arp_spa)] = macaddr2uint64(earp->arp_sha);
		break;
	}

exit:
	delete [] flm;
	return;
}

void arp_task(intptr_t exinf) {

}
