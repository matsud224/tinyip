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
    return;
}

/*
 *  Main task
 */
void
main_task(intptr_t exinf) {
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

	act_tsk(IPFRAG_TIMEOUT_TASK);
	sta_cyc(IPFRAG_TIMEOUT_CYC);
	act_tsk(ETHERRECV_TASK);
	act_tsk(ARP_TASK);
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
