#include <kernel.h>
#include <t_syslog.h>
#include <t_stdlib.h>
#include "syssvc/serial.h"
#include "syssvc/syslog.h"
#include "kernel_cfg.h"
#include "syssvc/logtask.h"
#include "arduino_app.h"
#include "mbed.h"

#include <time.h>

#include "ethernet.h"
#include "util.h"
#include "ip.h"
#include "netconf.h"
#include "netlib.h"
#include "sntpclient.h"


InterruptIn user_button0(USER_BUTTON0);

/*
 *  Error check of service calls
 */
Inline void
svc_perror(const char *file, int_t line, const char *expr, ER ercd)
{
	if (ercd < 0) {
		t_perror(LOG_ERROR, file, line, expr, ercd);
	}
}

#define	SVC_PERROR(expr)	svc_perror(__FILE__, __LINE__, #expr, (expr))

void user_button0_push() {
	mcled_change(COLOR_OFF);
	iwup_tsk(USER_TASK);
    return;
}

/*
 *  Main task
 */
void main_task(intptr_t exinf) {
	/* configure syslog */
	SVC_PERROR(syslog_msk_log(LOG_UPTO(LOG_INFO), LOG_UPTO(LOG_EMERG)));
	syslog(LOG_NOTICE, "Sample program starts (exinf = %d).", (int_t) exinf);

	//Ethernetコントローラ割り込みの設定
	ethernet_initialize();

	syslog(LOG_NOTICE, "MAC ADDR: %s",macaddr2str(MACADDR));
	syslog(LOG_NOTICE, "IP ADDR: %s",ipaddr2str(IPADDR));

	/* add your code here */
	user_button0.fall(user_button0_push);
	sta_cyc(CYCHDR1);

	act_tsk(ETHERRECV_TASK);
	act_tsk(TIMEOUT_10SEC_TASK);
	sta_cyc(TIMEOUT_10SEC_CYC);

	act_tsk(USER_TASK);
}

void user_task(intptr_t exinf){
	LOG("user task start");
	/*
	int s = socket(SOCK_DGRAM,  USER_TASK, USER_DRSEM, USER_DSSEM, USER_SRSEM, USER_SSSEM);
	bind(s, 10000);
	static char buf[2048];
	uint8_t fromaddr[IP_ADDR_LEN];
	uint16_t fromport;
	while(true){
		int len = recvfrom(s, buf, 2048, 0, fromaddr, &fromport);
		LOG("%s : %d --- %d bytes received.", ipaddr2str(fromaddr), fromport, len);
		slp_tsk();
	}
	*/
	/*
	int s = socket(SOCK_DGRAM, USER_DRSEM, USER_SRSEM, USER_SSSEM);
	static char buf[2345];
	static char pat[4] = {0xDE, 0xAD, 0xBE, 0xEF};
	for(int i=0;i<2340;i+=4){
		memcpy(buf+i, pat, 4);
	}
	buf[2344] = 0x12;
	uint8_t to_addr[IP_ADDR_LEN] = {192, 168, 0, 5};
	while(true){
		slp_tsk();
		int len = sendto(s, buf, sizeof(buf), 0, to_addr, 1234);
	}
	*/

	LOG("sntp client start");
	uint8_t serveraddr[IP_ADDR_LEN] = {192, 168, 0, 5};
    timestamp t;
    int err;
    while(true){
		slp_tsk();
		LOG("sending...");
		if((err = sntp_gettime(serveraddr, &t, USER_DRSEM, 3000)) < 0){
			mcled_change(COLOR_RED);
			LOG("error(%d)",err);
		}else{
			mcled_change(COLOR_LIGHTBLUE);
			//NTPタイムスタンプは1900年からの経過秒,エポックタイムは1970年からなので変換
			//ついでに日本時間に直す
			uint32_t convt = t.sec - 2208988800 + 32400;
			LOG("%s", ctime((time_t*)(&convt)));
		}
		LOG("finished.");
    }
}


/*
 *  Cyclic handler
 */
void cyclic_handler(intptr_t exinf)
{
	/* add your code here */
	static bool led_state = false;
	if(led_state)
		redled_off();
	else
		redled_on();
	led_state = !led_state;
}
