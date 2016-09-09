#include "arduino_app.h"

#include <cstring>

#include "udp.h"
#include "protohdr.h"
#include "sockif.h"
#include "netlib.h"
#include "util.h"
#include "netconf.h"

struct udp_pseudo_hdr{
	uint8_t up_src[IP_ADDR_LEN];
	uint8_t up_dst[IP_ADDR_LEN];
	uint8_t up_void;
	uint8_t up_type;
	uint16_t up_len;
};

void udp_task(intptr_t exinf) {

}

uint16_t udp_checksum(ip_hdr *iphdr, udp_hdr *uhdr){
	udp_pseudo_hdr pseudo;
	memcpy(pseudo.up_src, iphdr->ip_src, IP_ADDR_LEN);
	memcpy(pseudo.up_dst, iphdr->ip_dst, IP_ADDR_LEN);
	pseudo.up_type = 17;
	pseudo.up_void = 0;
	pseudo.up_len = uhdr->uh_ulen; //UDPヘッダ+UDPペイロードの長さ

	return checksum2((uint16_t*)(&pseudo), (uint16_t*)uhdr, sizeof(udp_pseudo_hdr), ntoh16(uhdr->uh_ulen));
}

void udp_process(ether_flame *flm, ip_hdr *iphdr, udp_hdr *uhdr){
	//LOG("src:%s",ipaddr2str(iphdr->ip_src));
	//LOG("dst:%s",ipaddr2str(iphdr->ip_dst));
	//LOG("sport:%d, dport:%d",ntoh16(uhdr->uh_sport),ntoh16(uhdr->uh_dport));
	//LOG("len:%d, sum:%d",ntoh16(uhdr->uh_ulen),ntoh16(uhdr->sum));
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
	LOG("udp received");


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
	wai_sem(sock->ownersem);
	sock->dgram_recv_queue[sock->recv_front] = flm;
	sock->recv_front++;
	if(sock->recv_front == DGRAM_RECV_QUEUE) sock->recv_front=0;
	if(sock->recv_front == sock->recv_back){
		//キューがいっぱいなので、古いものから消す
		delete sock->dgram_recv_queue[sock->recv_front];
		sock->recv_back++;
		if(sock->recv_back == DGRAM_RECV_QUEUE) sock->recv_back=0;
	}
	LOG("received udp datagram (queue %d/%d)", sock->recv_front, sock->recv_back);
	sig_sem(sock->ownersem);
	wup_tsk(sock->ownertsk);

	return;
exit:
	delete flm;
	return;
}
