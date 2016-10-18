#include <kernel.h>
#include "mbed.h"
#include "arduino_app.h"

#include "sntpclient.h"
#include "netlib.h"
#include "util.h"
#include "envdep.h"
#include "netconf.h"
#include "nameresolver.h"

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

#define SNTP_SERVER_PORT 123

#define SNTP_MAX_RETRY 5

const char *NTPSERVER_FQDN = "ntp.nict.jp";

void start_sntpclient(){
	act_tsk(SNTPCLIENT_TASK);
}

int sntp_gettime(uint8_t ipaddr[], timestamp *ts, TMO timeout){
	sntp_hdr sntpmsg = {0};
	sntpmsg.vn = 4;
	sntpmsg.mode = SNTP_MODE_CLIENT;
	//自身が時計を持っていないから、送信時刻は記入しない

	int sock = socket(SOCK_DGRAM);
	//bind(sock, 123);
	int err;
	if((err = sendto(sock, (char*)&sntpmsg, sizeof(sntpmsg), 0, ipaddr, SNTP_SERVER_PORT)) < 0){
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
	LOG("sntp client start");

	addrinfo *dnsres = NULL;
	if(getaddrinfo(NTPSERVER_FQDN, &dnsres)!=0 || dnsres==NULL){
		LOG("SNTP: host not found");
		return;
	}

	memcpy(NTPSERVER, dnsres->addr, IP_ADDR_LEN);

    timestamp t;
    for(int i=SNTP_MAX_RETRY; i>0; i--){
		if(sntp_gettime(NTPSERVER, &t, 1000) == 0){
			//NTPタイムスタンプは1900年からの経過秒,エポックタイムは1970年からなので変換
			//ついでに日本時間に直す
			time_t convt = (time_t)(t.sec - 2208988800u + 32400u);
			LOG("%s", ctime(&convt));
			set_time(convt); //時刻を設定
			srand((unsigned int)convt);
			sig_sem(SNTP_SEM);
			break;
		}
    }
	return;
}
