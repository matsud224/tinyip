#include "ethernet_api.h"
#include "ethernetext_api.h"
#include <kernel.h>
#include "kernel_cfg.h"
#include "mbed.h"
#include "arduino_app.h"

#include "ethernet.h"
#include "ip.h"
#include "arp.h"
#include "util.h"
#include "netconf.h"
#include "protohdr.h"


static void ethrcv_callback(){
    isig_sem(ETHERRECV_SEM);
}

void ethernet_initialize(){
	//lwipドライバ(rza1_emac.c)を参考にした
	ethernet_cfg_t ethcfg;

	ethcfg.int_priority = 6;
	ethcfg.recv_cb = &ethrcv_callback;
	ethcfg.ether_mac = NULL;
	ethernetext_init(&ethcfg);
	ethernet_set_link(-1,0);

	ethernet_address((char *)MACADDR);
}

void etherrecv_task(intptr_t exinf) {
	wait(1);
    while(true){
		twai_sem(ETHERRECV_SEM, 10);
		for(int i=0; i<16; i++){
			int size = ethernet_receive();
			if(size > sizeof(ether_hdr)){
				LOG("--FLAME RECEIVED--");
				char *buf = new char[size];
				ethernet_read(buf, size);
				ether_hdr *ehdr = (ether_hdr*)buf;
				ether_flame *flm = new ether_flame;
				flm->size = size;
				flm->buf = buf;
				switch(ntoh16(ehdr->ether_type)){
				case ETHERTYPE_IP:
					LOG("IP packet received");
					ip_process(flm, (ip_hdr*)(ehdr+1));
					break;
				case ETHERTYPE_ARP:
					LOG("ARP packet received");
					arp_process(flm, (ether_arp*)(ehdr+1));
					break;
				default:
					delete flm;
					break;
				}
			}
		}
    }
}

void ethernet_send(ether_flame *flm){
	ethernet_write(flm->buf, flm->size);
	ethernet_send();
	return;
}
