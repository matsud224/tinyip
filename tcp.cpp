#include "arduino_app.h"

#include "tcp.h"
#include "protohdr.h"
#include "util.h"
#include "netconf.h"


struct tcp_ctrlblock{
	ID rbufsem;
	ID sbufsem;
	int recv_front,recv_back;
	int send_front,send_back;
	char *recv_buf;
	char *send_buf;
	bool recv_waiting;
};

void tcp_task(intptr_t exinf) {

}


void tcp_process(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr){
//	//ブロードキャスト/マルチキャストアドレスは不許可
//	if(memcmp(iphdr->ip_dst, IPADDR, IP_ADDR_LEN) != 0){
//		LOG("udp packet discarded(bad address).");
//		goto exit;
//	}
//	//ヘッダ検査
//	if(flm->size < sizeof(ether_hdr)+(iphdr->ip_hl*4)+sizeof(udp_hdr) ||
//		flm->size != sizeof(ether_hdr)+(iphdr->ip_hl*4)+ntoh16(uhdr->uh_ulen)){
//		LOG("udp packet discarded(length error).");
//		goto exit;
//	}
//	if(uhdr->sum != 0 && udp_checksum(iphdr, uhdr) != 0){
//		LOG("udp packet discarded(checksum error).");
//		goto exit;
//	}
}

tcp_ctrlblock *tcp_newcb(ID recvsem, ID sendsem){
	tcp_ctrlblock *tcb = new tcp_ctrlblock;
	tcb->rbufsem = recvsem;
	tcb->sbufsem = sendsem;

	tcb->recv_buf=new char[STREAM_RECV_BUF];
	tcb->send_buf=new char[STREAM_SEND_BUF];

	tcb->recv_front=0; tcb->recv_back=0;
	tcb->send_front=0; tcb->send_back=0;
	tcb->recv_waiting=false;

	return tcb;
}

void tcp_disposecb(tcp_ctrlblock *tcb){
	delete tcb;
	return;
}
