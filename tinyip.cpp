#include <kernel.h>
#include <t_syslog.h>
#include <t_stdlib.h>
#include "syssvc/serial.h"
#include "syssvc/syslog.h"
#include "kernel_cfg.h"
#include "syssvc/logtask.h"
#include "arduino_app.h"
#include "mbed.h"

#include "ethernet.h"
#include "util.h"
#include "ip.h"
#include "netconf.h"
#include "netlib.h"


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

	act_tsk(TIMEOUT_10SEC_TASK);
	sta_cyc(TIMEOUT_10SEC_CYC);
	act_tsk(ETHERRECV_TASK);

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
	int s = socket(SOCK_DGRAM, USER_TASK, USER_DRSEM, USER_DSSEM, USER_SRSEM, USER_SSSEM);
	static char buf[] = "The quick brown fox jumps over the lazy dog.";
	uint8_t to_addr[IP_ADDR_LEN] = {192, 168, 0, 5};
	while(true){
		slp_tsk();
		int len = sendto(s, buf, sizeof(buf), 0, to_addr, 1234);
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
