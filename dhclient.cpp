#include "arduino_app.h"
#include "mbed.h"
#include "TextLCD.h"

#include <cstring>

#include "util.h"
#include "errnolist.h"
#include "netconf.h"
#include "netlib.h"
#include "dhclient.h"

struct dhcp_msg{
	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint32_t xid;
	uint16_t secs;
	uint16_t flags;
	uint8_t ciaddr[IP_ADDR_LEN];
	uint8_t yiaddr[IP_ADDR_LEN];
	uint8_t siaddr[IP_ADDR_LEN];
	uint8_t giaddr[IP_ADDR_LEN];
	uint8_t chaddr[16];
	uint8_t sname[64];
	uint8_t file[128];
};

struct dhcp_options{
	uint8_t msgtype;
	uint8_t subnetmask[IP_ADDR_LEN];
	uint8_t defaultgw[IP_ADDR_LEN];
	uint8_t dnsserver[IP_ADDR_LEN];
	uint8_t ntpserver[IP_ADDR_LEN];
	uint8_t serverid[IP_ADDR_LEN];
	uint32_t addrtime;
	uint32_t renewaltime;
	uint32_t rebindtime;
};

#define DHCLIENT_STATE_INIT 0
#define DHCLIENT_STATE_SELECTING 1
#define DHCLIENT_STATE_REQUESTING 2
#define DHCLIENT_STATE_BOUND 3
#define DHCLIENT_STATE_RENEWING 4
#define DHCLIENT_STATE_REBINDING 5

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY 2

#define DHCP_HTYPE_ETHERNET 1

#define DHCP_OPT_PAD 0
#define DHCP_OPT_SUBNETMASK 1
#define DHCP_OPT_DEFAULTGW 3
#define DHCP_OPT_DNSSERVER 6
#define DHCP_OPT_NTPSERVER 42
#define DHCP_OPT_ADDRREQ 50
#define DHCP_OPT_ADDRTIME 51
#define DHCP_OPT_MSGTYPE 53
#define DHCP_OPT_SERVERID 54
#define DHCP_OPT_PARAMREQLIST 55
#define DHCP_OPT_RENEWALTIME 58
#define DHCP_OPT_REBINDTIME 59
#define DHCP_OPT_END 255

#define DHCP_MSGTYPE_DISCOVER 1
#define DHCP_MSGTYPE_OFFER 2
#define DHCP_MSGTYPE_REQUEST 3
#define DHCP_MSGTYPE_DECLINE 4
#define DHCP_MSGTYPE_ACK 5
#define DHCP_MSGTYPE_NAK 6
#define DHCP_MSGTYPE_RELEASE 7
#define DHCP_MSGTYPE_INFORM 8
#define DHCP_MSGTYPE_FORCERENEW 9

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define FLG_TIMEOUT 1

#define MIN(x,y) ((x)<(y)?(x):(y))

const uint8_t DHCP_MAGIC_COOKIE[] = {99, 130, 83, 99};


static uint32_t next_xid = 0;
static uint32_t dhclient_start_time;

static TextLCD lcd(D0, D1, D2, D3, D4, D5);

void dhclient_alarm_start(uint32_t time);
void dhclient_alarm_restart(void);
void dhclient_alarm_stop(void);

void start_dhclient(){
	act_tsk(DHCLIENT_TASK);
}

static uint32_t get_xid(){
	SYSTIM tim;
	get_tim(&tim);
	next_xid += tim;
	return next_xid;
}

static bool is_zeroaddr(uint8_t addr[]){
	for(int i=0; i<IP_ADDR_LEN; i++)
		if(addr[i] != 0) return false;
	return true;
}

static void make_dhcpmsg_header(char msgbuf[], int len, uint32_t xid, uint8_t *ciaddr){
	memset(msgbuf, 0, len);

	dhcp_msg *msg = (dhcp_msg*)msgbuf;
	msg->op = DHCP_OP_BOOTREQUEST;
	msg->htype = DHCP_HTYPE_ETHERNET;
	msg->hlen = ETHER_ADDR_LEN;
	msg->xid = hton32(xid);
	SYSTIM tim; get_tim(&tim);
	msg->secs = hton16((tim-dhclient_start_time)/1000);
	if(ciaddr != NULL) memcpy(msg->ciaddr, ciaddr, IP_ADDR_LEN);
	memcpy(msg->chaddr, MACADDR, ETHER_ADDR_LEN);
}

//bufは314オクテットであると仮定
static void make_dhcpmsg_option(char buf[], int msgtype, uint8_t *request_addr, uint8_t *server_addr, uint8_t paramreqlist[], int paramreqlen){
	char *ptr = buf;
	memcpy(ptr, DHCP_MAGIC_COOKIE, 4);
	ptr+=4;
	ptr[0] = DHCP_OPT_MSGTYPE;
	ptr[1] = 1;
	ptr[2] = msgtype;
	ptr+=3;
	if(request_addr != NULL){
		ptr[0] = DHCP_OPT_ADDRREQ;
		ptr[1] = 4;
		memcpy(ptr+2, request_addr, IP_ADDR_LEN);
		ptr+=6;
	}
	if(server_addr != NULL){
		ptr[0] = DHCP_OPT_SERVERID;
		ptr[1] = 4;
		memcpy(ptr+2, server_addr, IP_ADDR_LEN);
		ptr+=6;
	}
	if(paramreqlen>0){
		ptr[0] = DHCP_OPT_PARAMREQLIST;
		ptr[1] = paramreqlen;
		memcpy(ptr+2, paramreqlist, paramreqlen);
		ptr+= 2+paramreqlen;
	}
	ptr[0] = DHCP_OPT_END;
	ptr++;
	for(; ptr<=buf+314; ptr++){
		*ptr = DHCP_OPT_PAD;
	}
}

static int get_dhcpmsg_options(char buf[], int len, dhcp_options *opts){
	bool has_msgtype = false;
	memset(opts, sizeof(dhcp_options), 0);
	char *ptr = buf;
	if(memcmp(ptr, DHCP_MAGIC_COOKIE, 4) != 0){
		return -1;
	}
	ptr+=4;
	while(ptr < buf+len){
		switch(*ptr){
		case DHCP_OPT_PAD:
			ptr++;
			continue;
		case DHCP_OPT_SUBNETMASK:
			if(ptr[1] == 4)
				memcpy(opts->subnetmask, ptr+2, IP_ADDR_LEN);
			break;
		case DHCP_OPT_DEFAULTGW:
			if(ptr[1] == 4)
				memcpy(opts->defaultgw, ptr+2, IP_ADDR_LEN);
			break;
		case DHCP_OPT_DNSSERVER:
			if(ptr[1] == 4)
				memcpy(opts->dnsserver, ptr+2, IP_ADDR_LEN);
			break;
		case DHCP_OPT_NTPSERVER:
			if(ptr[1] == 4)
				memcpy(opts->ntpserver, ptr+2, IP_ADDR_LEN);
			break;
		case DHCP_OPT_ADDRTIME:
			if(ptr[1] == 4)
				opts->addrtime = ntoh32(*((uint32_t*)(ptr+2)));
			break;
		case DHCP_OPT_MSGTYPE:
			if(ptr[1] == 1){
				opts->msgtype = *((uint8_t*)(ptr+2));
				has_msgtype = true;
			}
			break;
		case DHCP_OPT_SERVERID:
			if(ptr[1] == 4)
				memcpy(opts->serverid, ptr+2, IP_ADDR_LEN);
			break;
		case DHCP_OPT_RENEWALTIME:
			if(ptr[1] == 4)
				opts->renewaltime = ntoh32(*((uint32_t*)(ptr+2)));
			break;
		case DHCP_OPT_REBINDTIME:
			if(ptr[1] == 4)
				opts->rebindtime = ntoh32(*((uint32_t*)(ptr+2)));
			break;
		case DHCP_OPT_END:
			return has_msgtype?opts->msgtype:-1;
		}
		ptr+= 2+ptr[1];
	}
}

static uint32_t make_dhcpmsg(char buf[], int buflen, int msgtype, uint8_t request_addr[], uint8_t server_addr[]){
	static uint8_t paramreq_list[] = { 	DHCP_OPT_SUBNETMASK,
										DHCP_OPT_DEFAULTGW,
										DHCP_OPT_DNSSERVER,
										DHCP_OPT_NTPSERVER,
										DHCP_OPT_ADDRTIME,
										DHCP_OPT_RENEWALTIME,
										DHCP_OPT_REBINDTIME };

	int xid;
	switch(msgtype){
	case DHCP_MSGTYPE_DISCOVER:
		make_dhcpmsg_header(buf, buflen, (xid=get_xid()), NULL);
		make_dhcpmsg_option(buf+sizeof(dhcp_msg), DHCP_MSGTYPE_DISCOVER, NULL, NULL, NULL, 0);
		break;
	case DHCP_MSGTYPE_REQUEST:
		make_dhcpmsg_header(buf, buflen, (xid=get_xid()), request_addr);
		make_dhcpmsg_option(buf+sizeof(dhcp_msg), DHCP_MSGTYPE_REQUEST,
							 request_addr, server_addr, paramreq_list, sizeof(paramreq_list));
		break;
	case DHCP_MSGTYPE_RELEASE:
		make_dhcpmsg_header(buf, buflen, (xid=get_xid()), request_addr);
		make_dhcpmsg_option(buf+sizeof(dhcp_msg), DHCP_MSGTYPE_RELEASE, NULL, server_addr, NULL, 0);
		break;
	case DHCP_MSGTYPE_DECLINE:
		make_dhcpmsg_header(buf, buflen, (xid=get_xid()), NULL);
		make_dhcpmsg_option(buf+sizeof(dhcp_msg), DHCP_MSGTYPE_DECLINE, request_addr, server_addr, NULL, 0);
		break;
	}

	return xid;
}

static int dhcpmsg_send_and_wait_reply(int s, char buf[], int len, uint32_t xid,
									 uint8_t to_addr[], bool is_watch_alarm){
	uint32_t current_resend_waittime = DHCLIENT_WAITTIME_INIT;
retry:
	int result;
	if((result = sendto(s, buf, len, 0, to_addr, DHCP_SERVER_PORT)) < 0)
		return result;

	if((result = recvfrom(s, buf, len, 0, NULL, NULL, current_resend_waittime)) == ETIMEOUT){
		FLGPTN ptn;
		if(is_watch_alarm && pol_flg(DHCLIENT_ALM_FLG, FLG_TIMEOUT, TWF_ORW, &ptn)==E_OK){
			return ETIMEOUT;
		}
		current_resend_waittime *= 2;
		if(current_resend_waittime > DHCLIENT_WAITTIME_MAX){
			if(is_watch_alarm){
				current_resend_waittime = DHCLIENT_WAITTIME_MAX;
				goto retry;
			}else{
				return ETIMEOUT;
			}
		}else{
			goto retry;
		}
	}else if(result >= sizeof(dhcp_msg)){
		dhcp_msg *dm = (dhcp_msg*)buf;
		if(ntoh32(dm->xid) != xid) goto retry;
		return result;
	}else{
		goto retry;
	}
}


static int dhcpmsg_send(int s, char buf[], int len, uint8_t to_addr[]){
	return sendto(s, buf, len, 0, to_addr, DHCP_SERVER_PORT);
}

static void tslp_tsk_long(uint32_t timeout){
	while(timeout>0){
		tslp_tsk(1000);
		timeout -= 1;
	}
}


int dhcpmsg_analyze(char buf[], int len, uint8_t *client_addr, uint8_t *server_addr, dhcp_options *opts){
    dhcp_msg *dm = (dhcp_msg*)buf;
	if(len <= sizeof(dhcp_msg)+4/*magic cookie*/){
		return -1;
	}
    if(!is_zeroaddr(dm->yiaddr))
		memcpy(client_addr, dm->yiaddr, IP_ADDR_LEN);

	int result = get_dhcpmsg_options(buf+sizeof(dhcp_msg), len, opts);
	if(result>=0)
		memcpy(server_addr, opts->serverid, IP_ADDR_LEN);
	return result;
}

void clear_config(){
	//memset(DEFAULT_GATEWAY, 0, IP_ADDR_LEN);
	//memset(DNSSERVER, 0, IP_ADDR_LEN);
	//memset(NETMASK, 0, IP_ADDR_LEN);
	//memset(NTPSERVER, 0, IP_ADDR_LEN);
	memset(IPADDR, 0, IP_ADDR_LEN);
}

static void dhclient_bound_event(){
	LOG("DHCP: %s",ipaddr2str(IPADDR));
	lcd.cls(); lcd.printf("%s", ipaddr2str(IPADDR));
}

static void setconf(dhcp_options *opts){
	if(!is_zeroaddr(opts->defaultgw)) memcpy(DEFAULT_GATEWAY, opts->defaultgw, IP_ADDR_LEN);
	if(!is_zeroaddr(opts->dnsserver)) memcpy(DNSSERVER, opts->dnsserver, IP_ADDR_LEN);
	if(!is_zeroaddr(opts->subnetmask)) memcpy(NETMASK, opts->subnetmask, IP_ADDR_LEN);
	if(!is_zeroaddr(opts->ntpserver)) memcpy(NTPSERVER, opts->ntpserver, IP_ADDR_LEN);
}

void dhclient_task(intptr_t exinf){
	SYSTIM tim; get_tim(&tim);
	dhclient_start_time = tim;

	int state = DHCLIENT_STATE_INIT;
	clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_INIT);

	int buflen = sizeof(dhcp_msg) + 314;
	char *buf = new char[buflen];
	int result;
	dhcp_options opts;
	uint32_t xid;

	uint8_t client_addr[IP_ADDR_LEN];
	uint8_t server_addr[IP_ADDR_LEN];
	uint32_t t1,t2,lease;
	t1 = t2 = lease = 0;

	int sock = socket(SOCK_DGRAM);
	bind(sock, DHCP_CLIENT_PORT);

	while(true){
		switch(state){
		case DHCLIENT_STATE_INIT:
			xid = make_dhcpmsg(buf, buflen, DHCP_MSGTYPE_DISCOVER, NULL, NULL);
			result=dhcpmsg_send_and_wait_reply(sock, buf, buflen, xid, IPBROADCAST, false);
			state = DHCLIENT_STATE_SELECTING;
			clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_SELECTING);
			break;
		case DHCLIENT_STATE_SELECTING:
			int val;
			if((val=dhcpmsg_analyze(buf, result, client_addr, server_addr, &opts)) == DHCP_MSGTYPE_OFFER){
				//LOG("offered: %s", ipaddr2str(client_addr));
				LOG("server: %s", ipaddr2str(server_addr));
				xid = make_dhcpmsg(buf, buflen, DHCP_MSGTYPE_REQUEST, client_addr, server_addr);
				result=dhcpmsg_send_and_wait_reply(sock, buf, buflen, xid, IPBROADCAST, false);
				state = DHCLIENT_STATE_REQUESTING;
				clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_REQUESTING);
			}else{
				state = DHCLIENT_STATE_INIT;
				clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_INIT);
			}
			break;
		case DHCLIENT_STATE_REQUESTING:
			if(result == ETIMEOUT){
				make_dhcpmsg(buf, buflen, DHCP_MSGTYPE_DECLINE, client_addr, server_addr);
				dhcpmsg_send(sock, buf, buflen, server_addr);
				state = DHCLIENT_STATE_INIT;
				clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_INIT);
			}else{
				switch(dhcpmsg_analyze(buf, result, client_addr, server_addr, &opts)){
				case DHCP_MSGTYPE_ACK:
					lease = opts.addrtime;
					t1 = opts.renewaltime;
					t2 = opts.rebindtime;
					if(!t1) t1 = lease/2;
					if(!t2) t2 = lease*0.8;

					setconf(&opts);
					if(!is_zeroaddr(client_addr))
						memcpy(IPADDR, client_addr, IP_ADDR_LEN);
					state = DHCLIENT_STATE_BOUND;
					clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_BOUND);
					dhclient_bound_event();
					break;
				case DHCP_MSGTYPE_NAK:
					state = DHCLIENT_STATE_INIT;
					clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_INIT);
					break;
				default:
					xid = make_dhcpmsg(buf, buflen, DHCP_MSGTYPE_REQUEST, client_addr, server_addr);
					result=dhcpmsg_send_and_wait_reply(sock, buf, buflen, xid, IPBROADCAST, false);
					break;
				}
			}
			break;
		case DHCLIENT_STATE_BOUND:
			tslp_tsk_long(t1);
			xid = make_dhcpmsg(buf, buflen, DHCP_MSGTYPE_REQUEST, client_addr, server_addr);
			dhclient_alarm_start(t2-t1);
			state = DHCLIENT_STATE_RENEWING;
			clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_RENEWING);
			result=dhcpmsg_send_and_wait_reply(sock, buf, buflen, xid, server_addr, true);
			break;
		case DHCLIENT_STATE_RENEWING:
			dhclient_alarm_stop();
			if(result == ETIMEOUT){
				xid = make_dhcpmsg(buf, buflen, DHCP_MSGTYPE_REQUEST, client_addr, server_addr);
				dhclient_alarm_start(lease-t2);
				state = DHCLIENT_STATE_REBINDING;
				clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_REBINDING);
				result=dhcpmsg_send_and_wait_reply(sock, buf, buflen, xid, IPBROADCAST, true);
			}else{
				switch(dhcpmsg_analyze(buf, result, client_addr, server_addr, &opts)){
				case DHCP_MSGTYPE_ACK:
					lease = opts.addrtime;
					t1 = opts.renewaltime;
					t2 = opts.rebindtime;
					if(!t1) t1 = lease/2;
					if(!t2) t2 = lease*0.8;
					setconf(&opts);
					if(!is_zeroaddr(client_addr))
						memcpy(IPADDR, client_addr, IP_ADDR_LEN);
					state = DHCLIENT_STATE_BOUND;
					clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_BOUND);
					dhclient_bound_event();
					break;
				case DHCP_MSGTYPE_NAK:
					clear_config();
					state = DHCLIENT_STATE_INIT;
					clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_INIT);
					break;
				default:
					dhclient_alarm_restart();
					xid = make_dhcpmsg(buf, buflen, DHCP_MSGTYPE_REQUEST, client_addr, server_addr);
					result=dhcpmsg_send_and_wait_reply(sock, buf, buflen, xid, IPBROADCAST, true);
				}
			}
			break;
		case DHCLIENT_STATE_REBINDING:
			dhclient_alarm_stop();
			if(result == ETIMEOUT){
				clear_config();
				state = DHCLIENT_STATE_INIT;
				clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_INIT);
			}else{
				switch(dhcpmsg_analyze(buf, result, client_addr, server_addr, &opts)){
				case DHCP_MSGTYPE_ACK:
					lease = opts.addrtime;
					t1 = opts.renewaltime;
					t2 = opts.rebindtime;
					if(!t1) t1 = lease/2;
					if(!t2) t2 = lease*0.8;
					setconf(&opts);
					if(!is_zeroaddr(client_addr))
						memcpy(IPADDR, client_addr, IP_ADDR_LEN);
					state = DHCLIENT_STATE_BOUND;
					clr_flg(DHCLIENT_FLG, 0); set_flg(DHCLIENT_FLG, DHCLIENT_TRANSION_BOUND);
					break;
				default:
					dhclient_alarm_restart();
					xid = make_dhcpmsg(buf, buflen, DHCP_MSGTYPE_REQUEST, client_addr, server_addr);
					result=dhcpmsg_send_and_wait_reply(sock, buf, buflen, xid, IPBROADCAST, true);
					break;
				}
			}
			break;
		}
	}

	delete buf;
	close(sock);
}


static uint32_t remaining_time;
void dhclient_alarm_start(uint32_t time){
	dhclient_alarm_stop();
	remaining_time = time;
	clr_flg(DHCLIENT_ALM_FLG, 0);
	sta_alm(DHCLIENT_ALM, MIN(remaining_time, 6)*1000);
	remaining_time -= MIN(remaining_time, 6);
}

void dhclient_alarm_restart(){
	dhclient_alarm_stop();
	clr_flg(DHCLIENT_ALM_FLG, 0);
	sta_alm(DHCLIENT_ALM, MIN(remaining_time, 6)*1000);
	remaining_time -= MIN(remaining_time, 6);
}

void dhclient_alarm_stop(){
	stp_alm(DHCLIENT_ALM);
}

void dhclient_alarm_handler(intptr_t exinf)
{
	if(remaining_time == 0){
		iset_flg(DHCLIENT_ALM_FLG, FLG_TIMEOUT);
	}else{
		ista_alm(DHCLIENT_ALM, MIN(remaining_time, 6)*1000);
		remaining_time -= MIN(remaining_time, 6);
	}
}
