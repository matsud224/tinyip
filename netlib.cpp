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


socket_t sockets[MAX_SOCKET];

int socket(int type, ID recvsem, ID sendsem, ID infosem){
	ID ownertsk;
	get_tid(&ownertsk);

	wai_sem(SOCKTBL_SEM);
	int i = find_unusedsocket();
	if(i == -1){
		sig_sem(SOCKTBL_SEM);
		return -1;
	}

	sockets[i].type=type;
	sockets[i].ownertsk = ownertsk;
	sockets[i].my_port=0;
	sockets[i].partner_port = 0;
	memset(sockets[i].partner_addr, 0, IP_ADDR_LEN);
	switch(sockets[i].type){
	case SOCK_DGRAM:
		sockets[i].ctrlblock.ucb = udp_newcb(recvsem);
		break;
	case SOCK_STREAM:
		sockets[i].ctrlblock.tcb = tcb_new(&sockets[i], recvsem, sendsem, infosem);
		break;
	}

	sig_sem(SOCKTBL_SEM);
	return i;
}

int find_unusedsocket(){
	//already locked.
	for(int i=0;i<MAX_SOCKET;i++){
		if(sockets[i].type==SOCK_UNUSED){
			sig_sem(SOCKTBL_SEM);
			return i;
		}
	}
	return -1;
}

void copy_socket(int src, int dest){
	//already locked.
	sockets[dest] = sockets[src];
}

bool is_usedport(uint16_t port){
	wai_sem(SOCKTBL_SEM);
	for(int i=0;i<MAX_SOCKET;i++){
		if(sockets[i].type!=SOCK_UNUSED && sockets[i].my_port==port){
			sig_sem(SOCKTBL_SEM);
			return true;
		}
	}
	sig_sem(SOCKTBL_SEM);
	return false;
}

int find_unusedport(){
	for(int p=49152;p<65535;p++){
		if(!is_usedport(p))
			return p;
	}
	return 0;
}

int bind(int s, uint16_t my_port){
	if(my_port!=0 && is_usedport(my_port))
		return -1; //使用中ポート
	if(my_port==0)
		if((my_port=find_unusedport()) == 0)
			return -1;
	switch(sockets[s].type){
	case SOCK_DGRAM:
	case SOCK_STREAM:
		sockets[s].my_port=my_port;
		break;
	default:
		return -1;
	}
	return 0;
}

int close(int s){
	wai_sem(SOCKTBL_SEM);
	switch(sockets[s].type){
	case SOCK_DGRAM:
		udp_disposecb(sockets[s].ctrlblock.ucb);
		sockets[s].type=SOCK_UNUSED;
		break;
	case SOCK_STREAM:
		tcp_close(&sockets[s], sockets[s].ctrlblock.tcb);
		break;
	}
	sig_sem(SOCKTBL_SEM);
	return 0;
}

int sendto(int s, const char *msg, uint32_t len, int flags, uint8_t to_addr[], uint16_t to_port){
	if(sockets[s].my_port==0)
		if((sockets[s].my_port=find_unusedport()) == 0)
			return EAGAIN;
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return udp_sendto(msg, len, flags, to_addr, to_port, sockets[s].my_port);
	case SOCK_STREAM:
		return EBADF;
	default:
		return EBADF;
	}
}

int recvfrom(int s, char *buf, uint32_t len, int flags, uint8_t from_addr[], uint16_t *from_port, TMO timeout){
	if(sockets[s].my_port==0)
		if((sockets[s].my_port=find_unusedport()) == 0)
			return EAGAIN;
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return udp_recvfrom(sockets[s].ctrlblock.ucb, buf, len, flags, from_addr, from_port, timeout);
	case SOCK_STREAM:
		return EBADF;
	default:
		return EBADF;
	}
}

int connect(int s, uint8_t to_addr[], uint16_t to_port, TMO timeout){
	switch(sockets[s].type){
	case SOCK_DGRAM:
		memcpy(sockets[s].partner_addr, to_addr, IP_ADDR_LEN);
		sockets[s].partner_port = to_port;
		return 0;
	case SOCK_STREAM:
		memcpy(sockets[s].partner_addr, to_addr, IP_ADDR_LEN);
		sockets[s].partner_port = to_port;
		return tcp_connect(sockets[s].ctrlblock.tcb, to_addr, to_port, sockets[s].my_port, timeout);
	default:
		return EBADF;
	}
}

int listen(int s, int backlog){
	switch(sockets[s].type){
	case SOCK_STREAM:
		return tcp_listen(sockets[s].ctrlblock.tcb, backlog);
	default:
		return EBADF;
	}
}

int accept(int s, uint8_t client_addr[], uint16_t *client_port, TMO timeout){
	switch(sockets[s].type){
	case SOCK_STREAM:
		return tcp_accept(s, sockets[s].ctrlblock.tcb, client_addr, client_port, timeout);
	default:
		return EBADF;
	}
}

int send(int s, char *msg, uint32_t len, int flags, TMO timeout){
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return udp_sendto(msg, len, flags, sockets[s].partner_addr, sockets[s].partner_port, sockets[s].my_port);
	case SOCK_STREAM:
		return tcp_send(sockets[s].ctrlblock.tcb, msg, len, timeout);
	default:
		return EBADF;
	}
}

int recv(int s, char *buf, uint32_t len, int flags, TMO timeout){
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return udp_recvfrom(sockets[s].ctrlblock.ucb, buf, len, flags, sockets[s].partner_addr, &(sockets[s].partner_port), timeout);
	case SOCK_STREAM:
		return tcp_receive(sockets[s].ctrlblock.tcb, buf, len, timeout);
	default:
		return EBADF;
	}
}
