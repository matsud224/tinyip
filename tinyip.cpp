#include <kernel.h>
#include <t_syslog.h>
#include <t_stdlib.h>
#include "syssvc/serial.h"
#include "syssvc/syslog.h"
#include "kernel_cfg.h"
#include "syssvc/logtask.h"
#include "arduino_app.h"
#include "mbed.h"
#include "TextLCD.h"
#include "ethernet_api.h"
#include "ethernetext_api.h"

#include <time.h>
#include <stdlib.h>

#include "netconf.h"
#include "netlib.h"
#include "sntpclient.h"
#include "ethernet.h"
#include "util.h"
#include "morse.h"
#include "arp.h"

#define MIN(x,y) ((x)<(y)?(x):(y))

static TextLCD lcd(D0, D1, D2, D3, D4, D5);
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

void user_button0_fall() {
	mcled_change(COLOR_LIGHTBLUE);
	iset_flg(BUTTON_FLG, PTN_FALL);
    return;
}

void user_button0_rise() {
	mcled_change(COLOR_YELLOW);
	iset_flg(BUTTON_FLG, PTN_RISE);
    return;
}


/*
 *  Main task
 */
void main_task(intptr_t exinf) {
	/* configure syslog */
	SVC_PERROR(syslog_msk_log(LOG_UPTO(LOG_INFO), LOG_UPTO(LOG_EMERG)));
	syslog(LOG_NOTICE, "Sample program starts (exinf = %d).", (int_t) exinf);

	//乱数のシード値を未使用のアナログ入力から取得
	AnalogIn ain(A0);
	srand(ain.read_u16());

	//IPアドレスを更新
	IPADDR[3] = rand()%254+1;

	lcd.cls();
	lcd.printf("Please wait...");

	//Ethernetコントローラ割り込みの設定
	ethernet_initialize();

	while(!ethernet_link()){
		lcd.cls();
		lcd.printf("No link...");
		dly_tsk(1000);
	}

	LOG("macaddress = %s", macaddr2str(MACADDR));
	LOG("ipaddress = %s", ipaddr2str(IPADDR));

	lcd.cls();
	lcd.printf("%s\n",ipaddr2str(IPADDR));

	dly_tsk(2000); //Ethernetコントローラの準備待ち

	/* add your code here */
	user_button0.fall(user_button0_fall);
	user_button0.rise(user_button0_rise);

	sta_cyc(CYCHDR1);

	register_arptable(IPADDR_TO_UINT32(IPBROADCAST), ETHERBROADCAST);
	act_tsk(ETHERRECV_TASK);
	act_tsk(TCP_SEND_TASK);
	act_tsk(HTTPD_TASK);
	//act_tsk(MORSE_TASK);
	act_tsk(DHCLIENT_TASK);
}



void user_task(intptr_t exinf){
	LOG("usertask start");

	char input;

	while(true){
		rcv_dtq(INPUTCHAR_DTQ, (intptr_t*)(&input));
		lcd.printf("%c", input);
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
