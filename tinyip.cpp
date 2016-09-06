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

#define BYTE_ORDER LITTLE_ENDIAN

DigitalOut led_blue(LED_BLUE);
DigitalOut led_red(LED_RED);
DigitalOut led_green(LED_GREEN);
DigitalOut led_user(LED_USER);
InterruptIn user_button0(USER_BUTTON0);

Ethernet eth;

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


int led_user_status = 1;
void user_button0_push() {
    led_user_status = !led_user_status;
    led_user = led_user_status;
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
	char mac[6];
	eth.address(mac);
	syslog(LOG_NOTICE, "mac address = %02X:%02X:%02X:%02X:%02X:%02X\n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	union
    {
        uint32_t b4 ;     /* 4byte    */
        uint16_t b2 [2] ; /* 2byte×2 */
        uint8_t b1 [4] ;  /* 1byte×4 */
    } bytes ;

    bytes.b4 = 0x12345678 ;
    syslog(LOG_NOTICE,"bytes.b4: %08X\n", bytes.b4) ;
    syslog(LOG_NOTICE,"bytes.b2: %04X, %04X\n", bytes.b2[0], bytes.b2[1]) ;
    syslog(LOG_NOTICE,"bytes.b1: %02X, %02X, %02X, %02X\n", bytes.b1[0], bytes.b1[1], bytes.b1[2], bytes.b1[3]) ;

	/* add your code here */
	user_button0.fall(user_button0_push);
	sta_cyc(CYCHDR1);
	act_tsk(ETHERNET_TASK);
	act_tsk(IP_TASK);
	act_tsk(ARP_TASK);
}


/*
 *  Cyclic handler
 */
int led_blue_status = 0;
void cyclic_handler(intptr_t exinf)
{
	/* add your code here */
	led_blue_status = !led_blue_status;
	led_blue = led_blue_status;

}
