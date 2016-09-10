#include "arduino_app.h"

#include <cstring>

#include "udp.h"
#include "protohdr.h"
#include "sockif.h"
#include "netlib.h"
#include "util.h"
#include "netconf.h"

void udp_process(ether_flame *flm, ip_hdr *iphdr, udp_hdr *uhdr){
	//ブロードキャスト/マルチキャストアドレスは不許可
	if(memcmp(iphdr->ip_dst, IPADDR, IP_ADDR_LEN) != 0){
		LOG("udp packet discarded(bad address).");
		goto exit;
	}
	//ヘッダ検査
	if(flm->size < sizeof(ether_hdr)+(iphdr->ip_hl*4)+sizeof(udp_hdr) ||
		flm->size != sizeof(ether_hdr)+(iphdr->ip_hl*4)+ntoh16(uhdr->uh_ulen)){
		LOG("udp packet discarded(length error).");
		goto exit;
	}
	if(uhdr->sum != 0 && udp_checksum(iphdr, uhdr) != 0){
		LOG("udp packet discarded(checksum error).");
		goto exit;
	}
	//LOG("udp received");


	int s;
	socket_t *sock;
	wai_sem(SOCKTBL_SEM);
	for(s=0;s<MAX_SOCKET;s++){
		sock = &sockets[s];
		if(sock->type==SOCK_DGRAM && sock->my_port==ntoh16(uhdr->uh_dport))
			break;
	}
	sig_sem(SOCKTBL_SEM);
	if(s==MAX_SOCKET){
		goto exit;
	}

	//キューに入れる
	wai_sem(sock->drsem);
	sock->dgram_recv_queue[sock->recv_front] = flm;
	sock->recv_front++;
	if(sock->recv_front == DGRAM_RECV_QUEUE) sock->recv_front=0;
	if(sock->recv_front == sock->recv_back){
		//キューがいっぱいなので、古いものから消す
		delete sock->dgram_recv_queue[sock->recv_front];
		sock->recv_back++;
		if(sock->recv_back == DGRAM_RECV_QUEUE) sock->recv_back=0;
	}
	//LOG("received udp datagram (queue %d/%d)", sock->recv_front, sock->recv_back);
	sig_sem(sock->drsem);
	if(sock->recv_waiting) wup_tsk(sock->ownertsk);

	return;
exit:
	delete flm;
	return;
}
