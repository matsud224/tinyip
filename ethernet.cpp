#include "ethernet.h"
#include "ip.h"
#include "arp.h"
#include "arduino_app.h"
#include "mbed.h"
#include "led.h"
#include "util.h"

#include <kernel.h>
#include <t_syslog.h>
#include <t_stdlib.h>
#include "syssvc/serial.h"
#include "syssvc/syslog.h"
#include "kernel_cfg.h"
#include "syssvc/logtask.h"


Ethernet eth;


void ethernet_task(intptr_t exinf) {
	wait(1);
    while(true){
		int size = eth.receive();
		char *buf = new char[size];
		if(size > sizeof(ether_hdr)){
			mcled_change(COLOR_GREEN);
			eth.read(buf, size);
			ether_hdr *ehdr = (ether_hdr*)buf;
			switch(ntoh16(ehdr->ether_type)){
			case ETHERTYPE_IP:
				mcled_change(COLOR_LIGHTBLUE);
				ip_receive(size, buf);
				break;
			case ETHERTYPE_ARP:
				mcled_change(COLOR_RED);
				arp_receive(size, buf);
				break;
			}
		}
    }
}
