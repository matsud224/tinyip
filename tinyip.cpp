#include <kernel.h>
#include <t_syslog.h>
#include <t_stdlib.h>
#include "syssvc/serial.h"
#include "syssvc/syslog.h"
#include "kernel_cfg.h"
#include <stdint.h>

#include "syssvc/logtask.h"
#include "arduino_app.h"
#include "mbed.h"

#include "led.h"
#include "ethernet.h"
#include "util.h"
#include "ip.h"


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

int led_state = 0;
void user_button0_push() {
	led_state=(led_state+1)%16;
	mcled_change(led_state);
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
	init_ethernet();

	syslog(LOG_NOTICE, "MAC ADDR: %s",macaddr2str(get_macaddr()));
	syslog(LOG_NOTICE, "IP ADDR: %s",ipaddr2str(get_ipaddr()));

	/* add your code here */
	user_button0.fall(user_button0_push);

	//sta_cyc(CYCHDR1);
	act_tsk(ETHERRECV_TASK);
	act_tsk(IP_TASK);
	act_tsk(ARP_TASK);
}


/*
 *  Cyclic handler
 */
void cyclic_handler(intptr_t exinf)
{
	/* add your code here */

}
