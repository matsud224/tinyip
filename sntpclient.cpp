#include <kernel.h>

#include "sntpclient.h"
#include "netlib.h"
#include "util.h"
#include "envdep.h"

struct sntp_hdr{
#ifdef BIG_ENDIAN
	unsigned li :2;
	unsigned vn :3;
	unsigned mode :3;
#endif
#ifdef LITTLE_ENDIAN
	unsigned mode :3;
	unsigned vn :3;
	unsigned li :2;
#endif // BYTE_ORDER
	uint8_t strat;
	uint8_t poll;
	uint8_t prec;
	uint32_t rootdly;
	uint32_t rootdisp;
	uint8_t refid[4];
	timestamp reftime;
	timestamp origtime;
	timestamp recvtime;
	timestamp trantime;
	//uint32_t keyid;
	//uint8_t msgdig[16];
};

#define SNTP_MODE_CLIENT 3
#define SNTP_MODE_SERVER 4

int sntp_gettime(uint8_t ipaddr[], timestamp *ts, TMO timeout){
	sntp_hdr sntpmsg = {0};
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
}

void sntpclient_task(intptr_t exinf){

	return;
}
