#include "ethernet.h"
#include "ip.h"
#include "arp.h"
#include "arduino_app.h"
#include "mbed.h"
#include "led.h"
#include "util.h"

#include "ethernet_api.h"
#include "ethernetext_api.h"

#include <kernel.h>
#include "kernel_cfg.h"

uint8_t macaddr[ETHER_ADDR_LEN];

uint8_t* get_macaddr(){
	return macaddr;
}



static void ethrcv_callback(){
    isig_sem(ETHERRECV_SEM);
}

void init_ethernet(){
	//lwipドライバ(rza1_emac.c)を参考にした
	ethernet_cfg_t ethcfg;

	ethcfg.int_priority = 6;
	ethcfg.recv_cb = &ethrcv_callback;
	ethcfg.ether_mac = NULL;
	ethernetext_init(&ethcfg);
	ethernet_set_link(-1,0);

	ethernet_address((char *)macaddr);
}

void etherrecv_task(intptr_t exinf) {
	mcled_change(COLOR_WHITE);
	wait(1);
    while(true){
		twai_sem(ETHERRECV_SEM, 10);
		for(int i=0; i<16; i++){
			int size = ethernet_receive();
			if(size > sizeof(ether_hdr)){
				char *buf = new char[size];
				ethernet_read(buf, size);
				ether_hdr *ehdr = (ether_hdr*)buf;
				ether_flame *flm = new ether_flame;
				flm->size = size;
				flm->buf = buf;
				switch(ntoh16(ehdr->ether_type)){
				case ETHERTYPE_IP:
					mcled_change(COLOR_LIGHTBLUE);
					ip_receive(flm);
					break;
				case ETHERTYPE_ARP:
					mcled_change(COLOR_RED);
					arp_receive(flm);
					break;
				default:
					delete [] buf;
					delete flm;
					break;
				}
			}
		}
    }
}

void send_ethernet(ether_flame *flm){
	ethernet_write(flm->buf, flm->size);
	ethernet_send();
	return;
}
