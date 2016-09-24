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
	sta_cyc(TCP_SEND_CYC);
	act_tsk(TCP_TIMER_TASK);
	sta_cyc(TCP_TIMER_CYC);

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
    static char txt1[] = "The Transmission Control Protocol provides a communication service at an intermediate level between an application program and the Internet Protocol. It provides host-to-host connectivity at the Transport Layer of the Internet model. An application does not need to know the particular mechanisms for sending data via a link to another host, such as the required packet fragmentation on the transmission medium. At the transport layer, the protocol handles all \"handshaking\" and transmission details and presents an abstraction of the network connection to the application. At the lower levels of the protocol stack, due to network congestion, traffic load balancing, or other unpredictable network behavior, IP packets may be lost, duplicated, or delivered out of order. TCP detects these problems, requests retransmission of lost data, rearranges out-of-order data, and even helps minimize network congestion to reduce the occurrence of the other problems. If the data still remains undelivered, its source is notified of this failure. Once the TCP receiver has reassembled the sequence of octets originally transmitted, it passes them to the receiving application. Thus, TCP abstracts the application's communication from the underlying networking details. TCP is used extensively by many applications available by internet, including the World Wide Web (WWW), E-mail, File Transfer Protocol, Secure Shell, peer-to-peer file sharing, and many streaming media applications. TCP is optimized for accurate delivery rather than timely delivery, and therefore, TCP sometimes incurs relatively long delays (on the order of seconds) while waiting for out-of-order messages or retransmissions of lost messages. It is not particularly suitable for real-time applications such as Voice over IP. For such applications, protocols like the Real-time Transport Protocol (RTP) operating by means of the User Datagram Protocol (UDP) are usually recommended instead.[2] TCP is a reliable stream delivery service which guarantees that all bytes received will be identical with bytes sent and in the correct order. Since packet transfer by many networks is not reliable, a technique known as positive acknowledgment with retransmission is used to guarantee reliability of packet transfers. This fundamental technique requires the receiver to respond with an acknowledgment message as it receives the data. The sender keeps a record of each packet it sends. The sender also maintains a timer from when the packet was sent, and retransmits a packet if the timer expires before the message has been acknowledged. The timer is needed in case a packet gets lost or corrupted.[2] While IP handles actual delivery of the data, TCP keeps track of the individual units of data transmission, called segments, that a message is divided into for efficient routing through the network. For example, when an HTML file is sent from a web server, the TCP software layer of that server divides the sequence of octets of the file into segments and forwards them individually to the IP software layer (Internet Layer). The Internet Layer encapsulates each TCP segment into an IP packet by adding a header that includes (among other data) the destination IP address. When the client program on the destination computer receives them, the TCP layer (Transport Layer) reassembles the individual segments, and ensures they are correctly ordered and error free as it streams them to an application.";
	static char txt2[] = "After a storm comes a calm.\nWhen you are in Rome do as the Romans do.\nLittle and often fills the purse.\n";
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
    static char msg[] = "The quick brown fox jumps over the lazy dog.";
	LOG("sending...");
	int txt = 1;
	while(true){
		if(txt == 1){
			send(s2, txt1, sizeof(txt1), 0, TIMEOUT_NOTUSE);
			txt=2;
		}else{
			send(s2, txt2, sizeof(txt2), 0, TIMEOUT_NOTUSE);
			txt=1;
		}
		wai_sem(USER_BTNSEM);
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
