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

#include "ethernet.h"
#include "util.h"
#include "ip.h"
#include "netconf.h"
#include "netlib.h"
#include "sntpclient.h"

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

void user_button0_push() {
	mcled_change(COLOR_OFF);
	isig_sem(USER_BTNSEM);
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

	dly_tsk(2000); //Ethernetコントローラの準備待ち

	/* add your code here */
	user_button0.fall(user_button0_push);
	sta_cyc(CYCHDR1);

	act_tsk(ETHERRECV_TASK);
	act_tsk(TIMEOUT_10SEC_TASK);
	sta_cyc(TIMEOUT_10SEC_CYC);
	act_tsk(TCP_SEND_TASK);
	//act_tsk(TCP_TIMER_TASK);
	//sta_cyc(TCP_TIMER_CYC);

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
	/*
	uint8_t serveraddr[IP_ADDR_LEN] = {192, 168, 0, 5};
    timestamp t;
    SYSTIM systim;
    uint32_t localt;
    int err;
    while(true){
		if((err = sntp_gettime(serveraddr, &t, USER_DRSEM, 3000)) < 0){
			//mcled_change(COLOR_RED);
			lcd.locate(0,1);
			lcd.printf("                "); //1行消す
			lcd.locate(0,1);
			lcd.printf("error(%d)",err);
		}else{
			//mcled_change(COLOR_LIGHTBLUE);
			//NTPタイムスタンプは1900年からの経過秒,エポックタイムは1970年からなので変換
			//ついでに日本時間に直す
			localt = t.sec - 2208988800 + 32400;
			get_tim(&systim);
			break;
		}
    }

    while(true){
		lcd.cls();
		lcd.printf("%s", ctime((time_t*)(&localt)));
		localt++;
		dly_tsk(1000);
    }
    */
    uint8_t clientaddr[IP_ADDR_LEN];
    uint16_t clientport;
    static char buf[256];
    int s = socket(SOCK_STREAM, USER_SRSEM, USER_SSSEM);
    LOG("Current socket = %d", s);
    uint16_t my_port = 10000;
    bind(s, my_port);

    lcd.cls();
	lcd.printf("%s\nPort:%u",ipaddr2str(IPADDR),my_port);

    listen(s, 3);
    int s2;
    if((s2=accept(s, clientaddr, &clientport, TIMEOUT_NOTUSE))>=0){
		mcled_change(COLOR_GREEN);
		lcd.cls();
		lcd.printf("Connection from\n%s", ipaddr2str(clientaddr));
    }
    /*
    LOG("receive start");
    int recvlen;
    while((recvlen=recv(s2, buf, 256, 0, TIMEOUT_NOTUSE))>0){
		buf[recvlen] = '\0';
		lcd.cls();
		lcd.printf("%s", buf);
    }
    */

    LOG("send start");
    static char msg[] = "hello,world.";
		LOG("sending...");
		send(s2, msg, sizeof(msg), 0, TIMEOUT_NOTUSE);
		wai_sem(USER_BTNSEM);
		close(s2);
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
