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

#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/logging.h>

#define MIN(x,y) ((x)<(y)?(x):(y))

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
		lcd.cls(); lcd.printf("No link...");
		dly_tsk(1000);
	}

	LOG("macaddress: %s", macaddr2str(MACADDR));

	dly_tsk(1000); //Ethernetコントローラの準備待ち

	user_button0.fall(user_button0_fall);
	user_button0.rise(user_button0_rise);

	sta_cyc(CYCHDR1);

	register_arptable(IPADDR_TO_UINT32(IPBROADCAST), ETHERBROADCAST, true);

	start_ip();

	#ifdef USE_DHCP
		FLGPTN ptn;
		start_dhclient();
		twai_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_BOUND, TWF_ORW, &ptn, 5000); //BOUND状態に遷移するまで待つ
	#endif //USE_DHCP

	#ifdef USE_SNTP
		start_sntpclient();
	#endif //USE_SNTP

	start_tcp();
	start_httpd();
	act_tsk(USER_TASK);
	//act_tsk(MORSE_TASK);
}


static int sock_recv_callback(WOLFSSL* ssl, char *buf, int sz, void *ctx)
{
	return recv(*(int*)ctx, buf, sz, 0, TIMEOUT_NOTUSE);
}

static int sock_send_callback(WOLFSSL* ssl, char *buf, int sz, void *ctx)
{
	return send(*(int*)ctx, buf, sz, 0, TIMEOUT_NOTUSE);
}

static void wolfssl_logging_callback(const int loglevel, const char *const msg){
	LOG("%s", msg);
	logtask_flush(0);
	return;
}


void user_task(intptr_t exinf){
	//LOG("usertask start");

	dly_tsk(2000); //時刻取得待ち
/*
	int sock;
	uint8_t server_addr[IP_ADDR_LEN] = {192, 168, 0, 5};
	uint16_t server_port = 11111;
	sock = socket(SOCK_STREAM);
	if(connect(sock, server_addr, server_port, TIMEOUT_NOTUSE) < 0){
		LOG("connect() failed");
	}

	char buf[256];
	int n;
	int counter;
	counter = 0;
	while(true){
		dly_tsk(1000);
		sprintf(buf, "%d %d %d", counter, counter, counter);
		counter++;
		send(sock, buf, strlen(buf)+1, 0, TIMEOUT_NOTUSE);

		n=recv(sock, buf, 256, 0, TIMEOUT_NOTUSE);
		LOG("usertask: received %d bytes", n);
		buf[n] = '\0';
		lcd.printf("%s", buf);
	}

	return;
*/

	addrinfo *addr_list;
	if(getaddrinfo("google.com", &addr_list) != 0)
		LOG("getaddrinfo() failed.");

	uint8_t server_addr[IP_ADDR_LEN] = {192, 168, 0, 5};
	uint16_t server_port = 11111;

	WOLFSSL_CTX *ctx;
	WOLFSSL *ssl;

	wolfSSL_Init();

	//wolfSSL_Debugging_ON();
	wolfSSL_SetLoggingCb(wolfssl_logging_callback);

	if((ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method())) == NULL){
		LOG("wolfSSL_CTX_new error");
		goto exit;
	}

	int err;
	if((err=wolfSSL_CTX_load_verify_locations(ctx, "/sd/certs/ca-cert.pem", NULL)) != SSL_SUCCESS){
		LOG("Can't load CA certificates(%d).", err);
		wolfSSL_CTX_free(ctx);
		goto exit;
	}
	//wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);

	wolfSSL_SetIORecv(ctx, sock_recv_callback);
	wolfSSL_SetIOSend(ctx, sock_send_callback);


	LOG("wolfSSL initialized.");

	int sock;
	sock = socket(SOCK_STREAM);
	if((err=connect(sock, server_addr, server_port, TIMEOUT_NOTUSE)) < 0){
		LOG("connect() failed(%d).", err);
		wolfSSL_CTX_free(ctx);
		goto exit;
	}

	if((ssl = wolfSSL_new(ctx)) == NULL){
		LOG("wolfSSL_new error");
		close(sock);
		wolfSSL_CTX_free(ctx);
		goto exit;
	}

	wolfSSL_SetIOReadCtx(ssl, (void*)&sock);
	wolfSSL_SetIOWriteCtx(ssl, (void*)&sock);

	wolfSSL_set_fd(ssl, sock);


	char buf[256];
	int n;
	int counter;
	counter = 0;
	while(true){
		sprintf(buf, "%d %d %d", counter, counter, counter);
		counter++;
		if(wolfSSL_write(ssl, buf, strlen(buf)) != strlen(buf)){
			LOG("wolfSSL_write() failed.");
			break;
		}

		if((n=wolfSSL_read(ssl, buf, 256)) <= 0){
			LOG("wolfSSL_read() failed.");
			break;
		}
		buf[n] = '\0';
		LOG("%s", buf);
		dly_tsk(1000);
	}

	wolfSSL_free(ssl);
	close(sock);
	wolfSSL_CTX_free(ctx);
exit:
	wolfSSL_Cleanup();

	/*
	char input;

	while(true){
		rcv_dtq(INPUTCHAR_DTQ, (intptr_t*)(&input));
		lcd.printf("%c", input);
	}
	*/
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
