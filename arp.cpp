#include "arduino_app.h"

#include <cstring>
#include <map>

#include "arp.h"
#include "ethernet.h"
#include "util.h"
#include "netconf.h"
#include "protohdr.h"

using namespace std;

map<uint32_t, uint64_t> arptable;

void arp_process(ether_flame *flm, ether_arp *earp){
	ether_hdr *ehdr = (ether_hdr*)flm->buf;
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
		if(memcmp(earp->arp_tpa, IPADDR,IP_ADDR_LEN)==0){
			//相手のIPアドレスとMACアドレスを登録
			arptable[ipaddr2uint32(earp->arp_spa)] = macaddr2uint64(earp->arp_sha);
            LOG("arp entry registered:\n");
            LOG("\t%s -> %s\n", ipaddr2str(earp->arp_spa), macaddr2str(earp->arp_sha));
			//パケットを改変
			memcpy(earp->arp_tha, earp->arp_sha, ETHER_ADDR_LEN);
			memcpy(earp->arp_tpa, earp->arp_spa, IP_ADDR_LEN);
			memcpy(earp->arp_sha, MACADDR, ETHER_ADDR_LEN);
			memcpy(earp->arp_spa, IPADDR, IP_ADDR_LEN);
            earp->arp_op = hton16(ARPOP_REPLY);
            memcpy(ehdr->ether_dhost, ehdr->ether_shost, ETHER_ADDR_LEN);
            memcpy(ehdr->ether_shost, MACADDR, ETHER_ADDR_LEN);
            //送り返す
			ethernet_send(flm);
		}
		break;
	case ARPOP_REPLY:
		arptable[ipaddr2uint32(earp->arp_spa)] = macaddr2uint64(earp->arp_sha);
		break;
	}

exit:
	delete flm;
	return;
}

void resolve_arp(){

}

void arp_task(intptr_t exinf) {

}
