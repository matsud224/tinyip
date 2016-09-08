#include <cstring>

#include "netlib.h"
#include "protohdr.h"
#include "netconf.h"
#include "sockif.h"
#include "util.h"

#define MIN(x,y) ((x)<(y)?(x):(y))

socket_t sockets[MAX_SOCKET];

int socket(int type, ID ownertsk, ID ownersem){
	wai_sem(SOCKTBL_SEM);
	for(int i=0;i<MAX_SOCKET;i++){
		if(sockets[i].type==SOCK_UNUSED){
			sockets[i].type=type;
			sockets[i].ownertsk = ownertsk;
			sockets[i].ownersem = ownersem;
			switch(sockets[i].type){
			case SOCK_DGRAM:
				sockets[i].dgram_recv_queue=new ether_flame*[DGRAM_RECV_QUEUE];
				sockets[i].dgram_send_queue=new ether_flame*[DGRAM_SEND_QUEUE];
				break;
			case SOCK_STREAM:
				sockets[i].stream_recv_buf=new char[STREAM_RECV_BUF];
				sockets[i].stream_send_buf=new char[STREAM_SEND_BUF];
				break;
			}
			sockets[i].recv_front=0; sockets[i].recv_back=0;
			sockets[i].send_front=0; sockets[i].send_back=0;
			sockets[i].my_port=0;
			memset(sockets[i].dest_ipaddr, 0, IP_ADDR_LEN);
			sockets[i].dest_port=0;
			sig_sem(SOCKTBL_SEM);
			return i;
		}
	}
	sig_sem(SOCKTBL_SEM);
	return -1;
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
	sockets[s].type=SOCK_UNUSED;
	switch(sockets[s].type){
	case SOCK_DGRAM:
		delete [] sockets[s].dgram_recv_queue;
		delete [] sockets[s].dgram_send_queue;
		break;
	case SOCK_STREAM:
		delete [] sockets[s].stream_recv_buf;
		delete [] sockets[s].stream_send_buf;
		break;
	default:
		return -1;
	}
	sig_sem(SOCKTBL_SEM);
	return 0;
}

static int sendto_udp(int s, const char *msg, uint32_t len, int flags, uint8_t to_addr[], uint16_t to_port){
	return -1;
}

static char *udp_analyze(ether_flame *flm, uint16_t *datalen, uint8_t srcaddr[], uint16_t *srcport){
	ether_hdr *ehdr=(ether_hdr*)(flm->buf);
	ip_hdr *iphdr=(ip_hdr*)(ehdr+1);
	udp_hdr *udphdr=(udp_hdr*)(((uint8_t*)iphdr)+(iphdr->ip_hl*4));
	*datalen = ntoh16(udphdr->uh_ulen)-sizeof(udp_hdr);
	memcpy(srcaddr, iphdr->ip_src, IP_ADDR_LEN);
	*srcport = ntoh16(udphdr->uh_sport);
	return (char*)(udphdr+1);
}

static uint32_t recvfrom_udp(int s, char *buf, uint32_t len, int flags, uint8_t from_addr[], uint16_t *from_port){
	socket_t *sock=&sockets[s];
	memcpy(sock->dest_ipaddr, from_addr, IP_ADDR_LEN);

	while(true){
		wai_sem(sock->ownersem);
		if(sock->recv_front==sock->recv_back){
			sig_sem(sock->ownersem);
			LOG("user task zzz...");
            slp_tsk();
		}else{
			uint16_t datalen;
			char *data = udp_analyze(sock->dgram_recv_queue[sock->recv_back], &datalen, from_addr, from_port);
			memcpy(buf, data, MIN(len,datalen));
			delete sock->dgram_recv_queue[sock->recv_back];
			sock->recv_back++;
			if(sock->recv_back==DGRAM_RECV_QUEUE) sock->recv_back=0;
			sig_sem(sock->ownersem);
			return MIN(len,datalen);
		}
		LOG("retry...");
	}
}

int sendto(int s, const char *msg, uint32_t len, int flags, uint8_t to_addr[], uint16_t to_port){
	if(sockets[s].my_port==0)
		if((sockets[s].my_port=find_unusedport()) == 0)
			return -1;
	switch(sockets[s].type){
	case SOCK_DGRAM:
		sendto_udp(s, msg, len, flags, to_addr, to_port);
	case SOCK_STREAM:
		return -1;
	default:
		return -1;
	}
}

uint32_t recvfrom(int s, char *buf, uint32_t len, int flags, uint8_t from_addr[], uint16_t *from_port){
	if(sockets[s].my_port==0)
		if((sockets[s].my_port=find_unusedport()) == 0)
			return -1;
	switch(sockets[s].type){
	case SOCK_DGRAM:
		return recvfrom_udp(s, buf, len, flags, from_addr, from_port);
	case SOCK_STREAM:
		return -1;
	default:
		return -1;
	}
}
