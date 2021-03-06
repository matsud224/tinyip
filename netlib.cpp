#include <kernel.h>

#include <cstring>

#include "netlib.h"
#include "protohdr.h"
#include "netconf.h"
#include "sockif.h"
#include "util.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "errnolist.h"


socket_t sockets[MAX_SOCKET];

int socket(int type){
	ID ownertsk;
	get_tid(&ownertsk);

	wai_sem(SOCKTBL_SEM);
	int i;
	for(i=0;i<MAX_SOCKET;i++){
		if(sockets[i].type==SOCK_UNUSED){
			break;
		}
	}
	if(i == MAX_SOCKET){
		sig_sem(SOCKTBL_SEM);
		return -1;
	}

	sockets[i].ownertsk = ownertsk;
	sockets[i].addr.my_port=0;
	sockets[i].addr.partner_port = 0;
	memset(sockets[i].addr.partner_addr, 0, IP_ADDR_LEN);

	switch(type){
	case SOCK_DGRAM:
		sockets[i].ctrlblock.ucb = ucb_new(ownertsk);
		break;
	case SOCK_STREAM:
		sockets[i].ctrlblock.tcb = NULL;
		break;
	}

	sockets[i].type=type;

	sig_sem(SOCKTBL_SEM);
	return i;
}

bool is_usedport(uint16_t port){
	//already locked.
	for(int i=0;i<MAX_SOCKET;i++){
		if(sockets[i].type!=SOCK_UNUSED && sockets[i].type!=SOCK_RESERVED && sockets[i].addr.my_port==port){
			return true;
		}
	}
	return false;
}

uint16_t find_unusedport(){
	//already locked.
	for(uint16_t p=49152;p<65535;p++){
		if(!is_usedport(p))
			return p;
	}
	return 0;
}

int bind(int s, uint16_t my_port){
	wai_sem(PORTNO_SEM);
	if(my_port!=0 && is_usedport(my_port)){
		return -1; //使用中ポート
	}
	if(my_port==0 && (my_port=find_unusedport()) == 0){
		return -1;
	}
	sig_sem(PORTNO_SEM);

	switch(sockets[s].type){
	case SOCK_DGRAM:
		wai_sem(UDP_SEM);
		sockets[s].addr.my_port=my_port;
		sig_sem(UDP_SEM);
		break;
	case SOCK_STREAM:
		sockets[s].addr.my_port=my_port;
		break;
	default:
		return -1;
	}
	return 0;
}

int close(int s){
	switch(sockets[s].type){
	case SOCK_DGRAM:
		wai_sem(UDP_SEM);
		udp_disposecb(sockets[s].ctrlblock.ucb);
		sockets[s].type=SOCK_UNUSED;
		sig_sem(UDP_SEM);
		break;
	case SOCK_STREAM:
		tcp_close(sockets[s].ctrlblock.tcb);
		sockets[s].ctrlblock.tcb = NULL;
		sockets[s].type = SOCK_UNUSED;
		break;
	}
	return 0;
}

int sendto(int s, const char *msg, uint32_t len, int flags, uint8_t to_addr[], uint16_t to_port){
	if(sockets[s].addr.my_port==0 && bind(s, 0) != 0){
		return EAGAIN;
	}
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return udp_sendto(msg, len, flags, to_addr, to_port, sockets[s].addr.my_port);
	default:
		return EBADF;
	}
}

int recvfrom(int s, char *buf, uint32_t len, int flags, uint8_t from_addr[], uint16_t *from_port, TMO timeout){
	if(sockets[s].addr.my_port==0 && bind(s, 0) != 0){
		return EAGAIN;
	}
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return udp_recvfrom(sockets[s].ctrlblock.ucb, buf, len, flags, from_addr, from_port, timeout);
	default:
		return EBADF;
	}
}

int connect(int s, uint8_t to_addr[], uint16_t to_port, TMO timeout){
	if(sockets[s].addr.my_port==0 && bind(s, 0) != 0){
		return EAGAIN;
	}
	switch(sockets[s].type){
	case SOCK_DGRAM:
		memcpy(sockets[s].addr.partner_addr, to_addr, IP_ADDR_LEN);
		sockets[s].addr.partner_port = to_port;
		return 0;
	case SOCK_STREAM:
		if(sockets[s].ctrlblock.tcb == NULL){
			sockets[s].ctrlblock.tcb = tcb_new();
		}
		memcpy(sockets[s].addr.partner_addr, to_addr, IP_ADDR_LEN);
		sockets[s].addr.partner_port = to_port;
		tcb_setaddr_and_owner(sockets[s].ctrlblock.tcb, &(sockets[s].addr), sockets[s].ownertsk);
		return tcp_connect(sockets[s].ctrlblock.tcb, timeout);
	default:
		return EBADF;
	}
}

int listen(int s, int backlog){
	switch(sockets[s].type){
	case SOCK_STREAM:
		if(sockets[s].ctrlblock.tcb == NULL){
			sockets[s].ctrlblock.tcb = tcb_new();
		}
		tcb_setaddr_and_owner(sockets[s].ctrlblock.tcb, &(sockets[s].addr), sockets[s].ownertsk);
		return tcp_listen(sockets[s].ctrlblock.tcb, backlog);
	default:
		return EBADF;
	}
}

static int accepted_tcb_to_socket(tcp_ctrlblock *listening_tcb, uint8_t client_addr[], uint16_t *client_port, TMO timeout){
	ID ownertsk;
	get_tid(&ownertsk);

	wai_sem(SOCKTBL_SEM);
	int i;
	for(i=0;i<MAX_SOCKET;i++){
		if(sockets[i].type==SOCK_UNUSED){
			break;
		}
	}
	if(i == MAX_SOCKET){
		sig_sem(SOCKTBL_SEM);
		return -1;
	}
	sockets[i].type=SOCK_RESERVED;
	sig_sem(SOCKTBL_SEM);

	tcp_ctrlblock *accepted = tcp_accept(listening_tcb, client_addr, client_port, timeout);

	wai_sem(SOCKTBL_SEM);
	if(accepted == NULL){
		sockets[i].type = SOCK_UNUSED;
		sig_sem(SOCKTBL_SEM);
		return -1;
	}
	sockets[i].ctrlblock.tcb = accepted;
	sockets[i].ownertsk = tcb_getowner(accepted);
	sockets[i].addr = *tcb_getaddr(accepted);


	sockets[i].type=SOCK_STREAM;

	sig_sem(SOCKTBL_SEM);
	return i;
}

int accept(int s, uint8_t client_addr[], uint16_t *client_port, TMO timeout){
	int result;
	switch(sockets[s].type){
	case SOCK_STREAM:
		result = accepted_tcb_to_socket(sockets[s].ctrlblock.tcb, client_addr, client_port, timeout);
		if(result < 0)
			return EAGAIN;
		else
			return result;
	default:
		return EBADF;
	}
}

int send(int s, const char *msg, uint32_t len, int flags, TMO timeout){
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return udp_sendto(msg, len, flags, sockets[s].addr.partner_addr, sockets[s].addr.partner_port, sockets[s].addr.my_port);
	case SOCK_STREAM:
		return tcp_send(sockets[s].ctrlblock.tcb, msg, len, timeout);
	default:
		return EBADF;
	}
}

int recv(int s, char *buf, uint32_t len, int flags, TMO timeout){
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return udp_recvfrom(sockets[s].ctrlblock.ucb, buf, len, flags, sockets[s].addr.partner_addr, &(sockets[s].addr.partner_port), timeout);
	case SOCK_STREAM:
		return tcp_receive(sockets[s].ctrlblock.tcb, buf, len, timeout);
	default:
		return EBADF;
	}
}

//1行読む（最大len）。NULL終端される。
int recv_line(int s, char *buf, uint32_t len, int flags, TMO timeout){
	char c;
	int err, rlen=0;
	char *ptr=buf;
	switch(sockets[s].type){
	case SOCK_STREAM:
		while(true){
			if((err=recv(s, &c, 1, flags, timeout))<0)
				return err;
			*ptr=c;
			if(c=='\n' || c=='\r' || rlen==len-1){
				*ptr=NULL;
				break;
			}else{
				rlen++;
			}
			ptr++;
		}
		return rlen;
	default:
		return EBADF;
	}
}
