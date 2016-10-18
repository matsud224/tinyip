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
#include "SDFileSystem.h"

#include <time.h>
#include <stdlib.h>

#include "netconf.h"
#include "netlib.h"
#include "sntpclient.h"
#include "util.h"
#include "morse.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "httpd.h"
#include "nameresolver.h"
#include "dhclient.h"
#include "tweet.h"

#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/logging.h>

#define MIN(x,y) ((x)<(y)?(x):(y))

#define TEXT_LEN 32 //16x2 LCD
static TextLCD lcd(D0, D1, D2, D3, D4, D5);
SDFileSystem sd(P8_5, P8_6, P8_3, P8_4, "sd");
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
	iset_flg(BUTTON_FLG, PTN_FALL);
    return;
}

void user_button0_rise() {
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

	lcd.cls(); lcd.printf("Please wait...");

	//Ethernetコントローラ割り込みの設定
	ethernet_initialize();

	while(!ethernet_link()){
		dly_tsk(100);
		lcd.cls(); lcd.printf("No link...");
	}

	lcd.cls(); lcd.printf("Please wait...");
	LOG("macaddress: %s", macaddr2str(MACADDR));

	sta_cyc(CYCHDR1);

	register_arptable(IPADDR_TO_UINT32(IPBROADCAST), ETHERBROADCAST, true);

	start_ip();

	#ifdef USE_DHCP
		FLGPTN ptn;
		start_dhclient();
		if(twai_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_BOUND, TWF_ORW, &ptn, 30000)==E_TMOUT){ //BOUND状態に遷移するまで待つ
			lcd.cls(); lcd.printf("DHCP: failed");
		}else{
			lcd.cls(); lcd.printf("DHCP: OK");
		}
	#endif //USE_DHCP

	#ifdef USE_SNTP
		start_sntpclient();
		twai_sem(SNTP_SEM, 5000);
	#endif //USE_SNTP

	start_tcp();
	start_httpd();

	user_button0.fall(user_button0_fall);
	user_button0.rise(user_button0_rise);
	start_morse();

	act_tsk(USER_TASK);
}

static void index2pos(int index, int *col, int *row){
	*col=index%lcd.columns();
	*row=index/lcd.columns();
}

static int pos2index(int col, int row){
	return col+row*lcd.columns();
}

void user_task(intptr_t exinf){
	LOG("usertask start");

	lcd.cls();

	char text[TEXT_LEN+1];
	char utf8text[TEXT_LEN*3+1];
	int index=0; text[index]='\0';
	int col,row;
	char c;
	while(true){
		switch(c=morse_getc()){
		case CHAR_SEND:
			//送信
			{
				mcled_change(COLOR_LIGHTBLUE);
				char *w=utf8text;
				for(char *r=text; *r!='\0'; r++){
					if(is_halfkana(*r)){
						memcpy(w, halfkana_sjis_to_utf8(*r), 3);
						w+=3;
					}else{
						*w=*r; w++;
					}
				}
				*w='\0';
				if(tweet(text)==0)
					mcled_change(COLOR_OFF);
				else
					mcled_change(COLOR_RED);
			}
		case CHAR_CLEAR:
			//クリア
			index=0;
			text[0]='\0';
			lcd.cls();
			break;
		case '\b':
			while(index>0){
				index--;
				if(text[index]!=' ')
					break;
			}
			text[index]='\0';
			index2pos(index, &col, &row);
			lcd.locate(col, row);
			lcd.putc(' ');
			lcd.locate(col, row);
			break;
		default:
			if(index<TEXT_LEN){
				text[index]=c;
				lcd.putc(c);
				text[++index]='\0';
			}
			break;
		}
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
