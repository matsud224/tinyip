#include "arduino_app.h"


struct dhcp_msg{
	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint32_t xid;
	uint16_t secs;
	uint16_t flags;
	uint32_t ciaddr;
	uint32_t yiaddr;
	uint32_t siaddr;
	uint32_t giaddr;
	uint8_t chaddr[16];
	uint8_t sname[64];
	uint8_t file[128];
	uint8_t options[1];
};


void dhclient_task(intptr_t exinf){
	/*
	sntp_hdr dhcpmsg = {0};
	sntpmsg.vn = 4;
	sntpmsg.mode = SNTP_MODE_CLIENT;
	//自身が時計を持っていないから、送信時刻は記入しない

	int sock = socket(SOCK_DGRAM); //TCPは使わないので、セマフォの指定を省略
	//bind(sock, 123);
	int err;
	if((err = sendto(sock, (char*)&sntpmsg, sizeof(sntpmsg), 0, ipaddr, 123)) < 0){
		close(sock);
		return err;
	}
	if((err = recvfrom(sock, (char*)&sntpmsg, sizeof(sntpmsg), 0, NULL, NULL, timeout)) < 0){
		close(sock);
		return err;
	}
	ts->sec = ntoh32(sntpmsg.trantime.sec);
	ts->pico = ntoh32(sntpmsg.trantime.pico);
	close(sock);
	return 0;
	*/
}
