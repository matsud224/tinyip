#include "arduino_app.h"
#include "mbed.h"
#include "TextLCD.h"

#include <cstring>

#include "tcp.h"
#include "ip.h"
#include "protohdr.h"
#include "util.h"
#include "netconf.h"
#include "netlib.h"
#include "sockif.h"

#define MIN(x,y) ((x)<(y)?(x):(y))
#define MOD(x,y) ((x) % (y))

using namespace std;

struct tcp_timer_t;

static TextLCD lcd(D0, D1, D2, D3, D4, D5);

struct tcp_ctrlblock{
	ID rbufsem;
	ID sbufsem;
	ID infosem; //その他の情報のためのセマフォ

	socket_t *sock;

	uint32_t send_unack_seq; //未確認のシーケンス番号で、一番若いもの
	uint32_t send_unack; //unackの配列でのインデックス
	uint32_t send_next_seq; //次のデータ転送で使用できるシーケンス番号
	uint32_t send_next;
	uint16_t send_window; //送信ウィンドウサイズ
	uint32_t send_wl1; //直近のウィンドウ更新に使用されたセグメントのシーケンス番号
	uint32_t send_wl2; //直近のウィンドウ更新に使用されたセグメントの確認番号
	uint32_t send_used_len; //送信バッファに入っているデータのバイト数
	uint32_t iss; //初期送信シーケンス番号
	char *send_buf;
	bool send_waiting;

	uint32_t recv_next_seq; //次のデータ転送で使用できるシーケンス番号
	uint32_t recv_next;
	uint16_t recv_window; //受信ウィンドウサイズ
	uint32_t irs; //初期受信シーケンス番号
	char *recv_buf;
	bool recv_waiting;

	int myfin_state;
#define FIN_NOTREQUESTED 0
#define FIN_REQUESTED 1
#define FIN_ACKED 2
	uint32_t myfin_seq; //自身が送信したFINのシーケンス番号（ACKが来たか確かめる用に覚えとく）

	int state;
	uint16_t rtt;
	uint16_t mss; //相手との交渉で決まった値
	int opentype;
#define ACTIVE 0
#define PASSIVE 1
	int errno;

	bool establish_waiting;
	bool accept_waiting;

	int *sockqueue; //ソケットキュー(ソケット番号の配列,-1が入ってた場合は空とみなす)
	int backlog; //ソケットキューのサイズ
	int sockqueue_head; //先頭のインデックス
	int sockqueue_len; //格納されている要素数
};

#define TCP_STATE_CLOSED 0
#define TCP_STATE_LISTEN 1
#define TCP_STATE_SYN_RCVD 2
#define TCP_STATE_SYN_SENT 3
#define TCP_STATE_ESTABLISHED 4
#define TCP_STATE_FIN_WAIT_1 5
#define TCP_STATE_FIN_WAIT_2 6
#define TCP_STATE_CLOSING 7
#define TCP_STATE_TIME_WAIT 8
#define TCP_STATE_CLOSE_WAIT 9
#define TCP_STATE_LAST_ACK 10

#define TCP_OPT_END_OF_LIST 0
#define TCP_OPT_NOP 1
#define TCP_OPT_MSS 2
#define TCP_OPT_WIN_SCALE 3
#define TCP_OPT_S_ACK_PERM 4
#define TCP_OPT_S_ACK 5
#define TCP_OPT_TIMESTAMP 6

struct tcp_timer_option{
	union{
		struct{
			uint32_t start_index;
			uint32_t end_index;
			uint32_t start_seq;
			uint32_t end_seq;
		} resend;
	} option;
};

struct tcp_timer_t{
	tcp_timer_t *next; //連結リストにして、時間切れになる順につなぐ
	int type;
#define TCP_TIMER_REMOVED 0 //削除済みの印
#define TCP_TIMER_TYPE_FINACK 1
#define TCP_TIMER_TYPE_RESEND 2
#define TCP_TIMER_TYPE_TIMEWAIT 3
#define TCP_TIMER_TYPE_DELAYACK 4
	int remaining; //前に繋がれている要素の秒数の累計＋remainingで発動
	int sec; //タイマの設定秒数
	socket_t *sock;
	tcp_timer_option *option;
};

tcp_timer_t *tcptimer = NULL;


static void tcp_timer_add(socket_t *sock, int sec, int type, tcp_timer_option *opt);
static void tcp_timer_add_locked(socket_t *sock, int sec, int type, tcp_timer_option *opt);
static void tcp_timer_remove_all(socket_t *sock);

static bool between_le_lt(uint32_t a, uint32_t b, uint32_t c);
static bool between_lt_le(uint32_t a, uint32_t b, uint32_t c);
static bool between_le_le(uint32_t a, uint32_t b, uint32_t c);
static bool between_lt_lt(uint32_t a, uint32_t b, uint32_t c);
static void tcb_reset(tcp_ctrlblock *tcb);

tcp_ctrlblock *tcb_newcb(socket_t *sock, ID recvsem, ID sendsem);
static void tcb_allocsockq(tcp_ctrlblock *tcb, int backlog);
static void tcb_allocbuf(tcp_ctrlblock *tcb);
void tcb_dispose(tcp_ctrlblock *tcb);

static uint16_t tcp_checksum_recv(ip_hdr *iphdr, tcp_hdr *thdr);
static uint16_t tcp_checksum_send(hdrstack *seg, uint8_t ip_src[], uint8_t ip_dst[]);
static uint32_t tcp_geninitseq();
static hdrstack *make_tcpopt(bool contain_mss, uint16_t mss);
static uint16_t get_tcpopt_mss(tcp_hdr *thdr);
static void tcp_send_ctrlseg(uint32_t seq, uint32_t ack, uint16_t win, uint8_t flags, hdrstack *opt, uint8_t to_addr[], uint16_t to_port, uint16_t my_port);
static hdrstack *sendbuf_to_hdrstack(tcp_ctrlblock *tcb, uint32_t from, uint32_t len, int *next_from);
static void tcp_send_from_buf(tcp_ctrlblock *tcb);
static bool tcp_resend_from_buf(tcp_ctrlblock *tcb, uint32_t start_index, uint32_t end_index, uint32_t start_seq, uint32_t end_seq);

void tcp_process(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr);
static void tcp_process_closed(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len);
static void tcp_process_listen(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb, int sock);
static void tcp_process_synsent(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb);
static void tcp_process_otherwise(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb);

static void tcp_write_to_recvbuf(tcp_ctrlblock *tcb, char *data, uint32_t len);
static int tcp_write_to_sendbuf(tcp_ctrlblock *tcb, char *data, uint32_t len, TMO timeout);
static int tcp_read_from_recvbuf(tcp_ctrlblock *tcb, char *data, uint32_t len, TMO timeout);

int tcp_connect(tcp_ctrlblock *tcb, uint8_t to_addr[], uint16_t to_port, uint16_t my_port, TMO timeout);
int tcp_listen(tcp_ctrlblock *tcb, int backlog);
int tcp_accept(int s, tcp_ctrlblock *tcb, uint8_t client_addr[], uint16_t *client_port);
int tcp_send(tcp_ctrlblock *tcb, char *msg, uint32_t len, TMO timeout);
int tcp_receive(tcp_ctrlblock *tcb, char *buf, uint32_t len, TMO timeout);
int tcp_close(socket_t *sock, tcp_ctrlblock *tcb);



static void tcp_timer_add(socket_t *sock, int sec, int type, tcp_timer_option *opt){
	wai_sem(TCP_TIMER_SEM);

	tcp_timer_add_locked(sock, sec, type, opt);

	sig_sem(TCP_TIMER_SEM);
	return;
}

static void tcp_timer_add_locked(socket_t *sock, int sec, int type, tcp_timer_option *opt){
	//already locked.
	tcp_timer_t *tim = new tcp_timer_t;
	tim->type = type;
	tim->sock = sock;
	tim->option = opt;
	tim->sec = sec;

	LOG("new timer: sec=%d, type=%d", sec, type);

	tcp_timer_t **pp = &tcptimer;
	while(*pp != NULL){
		if(sec > (*pp)->remaining){
			sec -= (*pp)->remaining;
			pp = &((*pp)->next);
		}else{
			break;
		}
	}
	tim->next = *pp;
	*pp = tim;
	tim->remaining = sec;
	pp = &tim->next;
	while(*pp!=NULL){
		(*pp)->remaining -= sec;
		pp = &((*pp)->next);
	}

	return;
}

static void tcp_timer_remove_all(socket_t *sock){
	tcp_timer_t *ptr = tcptimer;

	while(ptr!=NULL){
		if(ptr->sock == sock)
			ptr->type = TCP_TIMER_REMOVED;
		ptr = ptr->next;
	}

	return;
}

static bool between_le_lt(uint32_t a, uint32_t b, uint32_t c){
	//mod 2^32で計算
	if(a < c)
		return (a<=b) && (b<c);
	else if(a > c)
		return (a<=b) || (b<c);
	else
		return false;
}

static bool between_lt_le(uint32_t a, uint32_t b, uint32_t c){
	//mod 2^32で計算
	if(a < c)
		return (a<b) && (b<=c);
	else if(a > c)
		return (a<b) || (b<=c);
	else
		return false;
}

static bool between_le_le(uint32_t a, uint32_t b, uint32_t c){
	//mod 2^32で計算
	if(a < c)
		return (a<=b) && (b<=c);
	else if(a > c)
		return (a<=b) || (b<=c);
	else
		return a==b;
}

static bool between_lt_lt(uint32_t a, uint32_t b, uint32_t c){
	//mod 2^32で計算
	if(a < c)
		return (a<b) && (b<c);
	else if(a > c)
		return (a<b) || (b<c);
	else
		return false;
}

static void tcb_reset(tcp_ctrlblock *tcb){
	if(tcb->backlog>0){
		int idx = tcb->sockqueue_head;
		while(tcb->sockqueue_len>0){
			close(tcb->sockqueue[idx]);
			idx = (idx+1) % tcb->backlog;
			tcb->sockqueue_len--;
		}
		delete [] tcb->sockqueue;
	}
	if(tcb->recv_buf!=NULL) delete [] tcb->recv_buf;
	if(tcb->send_buf!=NULL) delete [] tcb->send_buf;

	tcb->send_waiting = false;
	tcb->recv_waiting = false;
	tcb->establish_waiting = false;
	tcb->accept_waiting = false;

	tcb->state = TCP_STATE_CLOSED;
	tcb->rtt = 3;
	tcb->myfin_state = FIN_NOTREQUESTED;

	tcb->backlog = 0;
	tcb->sockqueue_head = 0;
	tcb->sockqueue_len = 0;
	tcb->sockqueue = NULL;

	tcb->send_unack_seq = 0;
	tcb->send_unack = 0;
	tcb->send_next_seq = 0;
	tcb->send_next =0;
	tcb->send_used_len = 0;
	tcb->send_window = 0;
	tcb->send_wl1 = 0;
	tcb->send_wl2 = 0;

	tcb->recv_next_seq = 0;
	tcb->recv_next = 0 ;
	tcb->recv_window = STREAM_RECV_BUF;

	return;
}

//メモリ節約のため、接続を確立するまでバッファの領域確保は行わない
//確保は、後からtcp_allocbuf()で行う
tcp_ctrlblock *tcb_new(socket_t *sock, ID recvsem, ID sendsem){
	tcp_ctrlblock *tcb = new tcp_ctrlblock;
	tcb->recv_buf = NULL;
	tcb->send_buf = NULL;
	tcb->sockqueue = NULL;
	tcb->backlog = 0;
	tcb->rbufsem = recvsem;
	tcb->sbufsem = sendsem;
	tcb->sock = sock;

	tcb_reset(tcb);

	return tcb;
}

static void tcb_allocsockq(tcp_ctrlblock *tcb, int backlog){
	if(tcb->sockqueue != NULL)
		delete [] tcb->sockqueue;
	if(backlog > 0){
		tcb->backlog = backlog;
		tcb->sockqueue_head = 0;
		tcb->sockqueue_len = 0;
		tcb->sockqueue = new int[backlog];
	}else{
		tcb->backlog = 0;
		tcb->sockqueue_head = 0;
		tcb->sockqueue_len = 0;
		tcb->sockqueue = NULL;
	}
	return;
}

//ESTABLISHEDになってはじめてバッファを確保する
static void tcb_allocbuf(tcp_ctrlblock *tcb){
	tcb->recv_buf = new char[STREAM_RECV_BUF];
	tcb->send_buf = new char[STREAM_SEND_BUF];
	tcb->send_used_len = 0;
	tcb->send_wl1 = 0;
	tcb->send_wl2 = 0;
	tcb->recv_window = STREAM_RECV_BUF;

	return;
}

void tcb_dispose(tcp_ctrlblock *tcb){
	tcb_reset(tcb);
	delete tcb;
	return;
}


//初期シーケンス番号を生成する
static uint32_t tcp_geninitseq(){
	SYSTIM systim;
	get_tim(&systim);
	return systim + 64000;
}

static uint16_t tcp_checksum_recv(ip_hdr *iphdr, tcp_hdr *thdr){
	tcp_pseudo_hdr pseudo;
	memcpy(pseudo.tp_src, iphdr->ip_src, IP_ADDR_LEN);
	memcpy(pseudo.tp_dst, iphdr->ip_dst, IP_ADDR_LEN);
	pseudo.tp_type = 6;
	pseudo.tp_void = 0;
	pseudo.tp_len = hton16(ntoh16(iphdr->ip_len) - iphdr->ip_hl*4); //TCPヘッダ+TCPペイロードの長さ

	return checksum2((uint16_t*)(&pseudo), (uint16_t*)thdr, sizeof(tcp_pseudo_hdr), ntoh16(iphdr->ip_len) - iphdr->ip_hl*4);
}

static uint16_t tcp_checksum_send(hdrstack *seg, uint8_t ip_src[], uint8_t ip_dst[]){
	int segsize = hdrstack_totallen(seg);
	tcp_pseudo_hdr pseudo;
	memcpy(pseudo.tp_src, ip_src, IP_ADDR_LEN);
	memcpy(pseudo.tp_dst, ip_dst, IP_ADDR_LEN);
	pseudo.tp_type = 6;
	pseudo.tp_void = 0;
	pseudo.tp_len = hton16(segsize); //TCPヘッダ+TCPペイロードの長さ

	hdrstack *pseudo_hdr = new hdrstack(false);
	pseudo_hdr->buf = (char*)&pseudo;
	pseudo_hdr->size = sizeof(tcp_pseudo_hdr);
	pseudo_hdr->next = seg;
	uint16_t result = checksum_hdrstack(pseudo_hdr);
	pseudo_hdr->next = NULL;
	delete pseudo_hdr;
	return result;
}

//他のオプションは順次対応
static hdrstack *make_tcpopt(bool contain_mss, uint16_t mss){
	int optlen = 0;
	if(contain_mss) optlen += 4;

	hdrstack *opt = new hdrstack(true);
	opt->size = optlen;
	opt->next = NULL;
	opt->buf = new char[opt->size];

	char *opt_next = opt->buf;
	if(contain_mss){
		opt_next[0] = TCP_OPT_MSS;
		opt_next[1] = 4;
		*(opt_next+2) = hton16(mss);
		opt_next += 4;
	}

	return opt;
}

//見つからければデフォルト値を返す
static uint16_t get_tcpopt_mss(tcp_hdr *thdr){
	uint8_t *optptr = ((uint8_t*)(thdr+1));
	uint8_t *datastart =  ((uint8_t*)thdr) + thdr->th_off*4;
	while(optptr++ < datastart){
		switch(*optptr){
		case TCP_OPT_END_OF_LIST:
			return -1;
		case TCP_OPT_NOP:
			break;
		case TCP_OPT_MSS:
			return *((uint16_t*)(optptr+1));
		default:
			optptr += (*optptr)-2;
			break;
		}
	}
	return 536;
}


//制御用のセグメント（ペイロードなし）を送信
static void tcp_send_ctrlseg(uint32_t seq, uint32_t ack, uint16_t win, uint8_t flags, hdrstack *opt, uint8_t to_addr[], uint16_t to_port, uint16_t my_port){
	ip_hdr iphdr;
	memcpy(iphdr.ip_src, IPADDR, IP_ADDR_LEN);
	memcpy(iphdr.ip_dst, to_addr, IP_ADDR_LEN);

	tcp_hdr *thdr = new tcp_hdr;
	thdr->th_sport = hton16(my_port);
	thdr->th_dport = hton16(to_port);
	thdr->th_seq = hton32(seq);
	thdr->th_ack = hton32(ack);
	thdr->th_flags = flags;
	thdr->th_x2 = 0;
	thdr->th_win = hton16(win);
	thdr->th_sum = 0;
	thdr->th_urp = 0;

	hdrstack *tcpseg = new hdrstack(true);
	tcpseg->size = sizeof(tcp_hdr);
	tcpseg->buf = (char*)thdr;

	tcpseg->next = opt;

	thdr->th_off = (sizeof(tcp_hdr) + opt->size) / 4;
	thdr->th_sum = tcp_checksum_send(tcpseg, IPADDR, to_addr);

	ip_send(tcpseg, to_addr, IPTYPE_TCP);

}


static void tcp_process_closed(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len){
	if(!(thdr->th_flags & TH_RST)){
		if(thdr->th_flags & TH_ACK)
			tcp_send_ctrlseg(0, ntoh32(thdr->th_seq)+payload_len, 0, TH_ACK|TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
		else
			tcp_send_ctrlseg(ntoh32(thdr->th_ack), 0, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
	}
	delete flm;
}

static void tcp_process_listen(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb, int sock){
	if(thdr->th_flags & TH_RST){
		goto exit;
	}
	if(thdr->th_flags & TH_ACK){
		tcp_send_ctrlseg(ntoh32(thdr->th_ack), 0, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
		goto exit;
	}
	if(thdr->th_flags & TH_SYN){
		LOG("connection requested...");
		//ソケットキューに入れる
		int newsock;
		if(tcb->backlog > tcb->sockqueue_len){
			//空き
			wai_sem(SOCKTBL_SEM);
			if((newsock=find_unusedsocket()) == -1){
				LOG("Error: Socket table is full.");
				sig_sem(SOCKTBL_SEM);
				goto exit;
			}
			tcb->sockqueue[(tcb->sockqueue_head+tcb->sockqueue_len)%tcb->backlog] = newsock;
			tcb->sockqueue_len++;
			tcb->sock = &sockets[newsock];
		}else{
			LOG("Error: Socket queue is full.");
			goto exit;
		}
        copy_socket(sock, newsock);
        tcp_ctrlblock *newtcb = tcb_new(&sockets[newsock], tcb->rbufsem, tcb->sbufsem);
        sockets[newsock].ctrlblock.tcb = newtcb;
        memcpy(sockets[newsock].partner_addr, iphdr->ip_src, IP_ADDR_LEN);
        sockets[newsock].partner_port = ntoh16(thdr->th_sport);
		newtcb->recv_next_seq = ntoh32(thdr->th_seq)+1;
		newtcb->recv_next = 0;
		newtcb->irs = ntoh32(thdr->th_seq);
		newtcb->mss = get_tcpopt_mss(thdr);
		if(tcb->accept_waiting) wup_tsk(tcb->sock->ownertsk);
		sig_sem(SOCKTBL_SEM);
	}

exit:
	delete flm;
	return;
}

static void tcp_process_synsent(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb){
	if(thdr->th_flags & TH_ACK){
		if(ntoh32(thdr->th_ack) != tcb->iss+1){
			if(!(thdr->th_flags & TH_RST)){
				tcp_send_ctrlseg(ntoh32(thdr->th_ack), 0, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
			}
			goto exit;
		}
	}
	if(thdr->th_flags & TH_RST){
		if(between_le_le(tcb->send_unack_seq, ntoh32(thdr->th_ack), tcb->send_next_seq)){
			tcb->state = TCP_STATE_CLOSED;
			if(tcb->establish_waiting) wup_tsk(tcb->sock->ownertsk);
		}
		goto exit;
	}
	if(thdr->th_flags & TH_SYN){
		tcb->recv_next_seq = ntoh32(thdr->th_seq)+1;
		tcb->irs = ntoh32(thdr->th_seq);
		if(thdr->th_flags & TH_ACK)
			tcb->send_unack_seq = ntoh32(thdr->th_ack);
		if(tcb->send_unack_seq == tcb->iss){
			tcb->state = TCP_STATE_SYN_RCVD;
			tcp_send_ctrlseg(tcb->iss, tcb->recv_next_seq, 0, TH_SYN|TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
		}else{
			tcb->state = TCP_STATE_ESTABLISHED;
			tcb_allocbuf(tcb);
			tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, 0, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
			if(tcb->establish_waiting) wup_tsk(tcb->sock->ownertsk);
		}
	}

exit:
	delete flm;
	return;
}

static void tcp_write_to_recvbuf(tcp_ctrlblock *tcb, char *data, uint32_t len){
	wai_sem(tcb->rbufsem);
	uint32_t remain = len;
	while(tcb->recv_window > 0 && remain > 0){
		tcb->recv_buf[tcb->recv_next] = *data++;
		tcb->recv_next = (tcb->recv_next+1)%STREAM_RECV_BUF;
		tcb->recv_next_seq++;
		tcb->recv_window--;
		remain--;
	}
	if(tcb->recv_waiting) wup_tsk(tcb->sock->ownertsk);
	sig_sem(tcb->rbufsem);
	LOG("received %d byte(s)", len);
}

//fromインデックスからlenバイトの区間をhdrstackとして返す（配列が循環していることを考慮して）
static hdrstack *sendbuf_to_hdrstack(tcp_ctrlblock *tcb, uint32_t from, uint32_t len, int *next_from){
	hdrstack *payload1 = new hdrstack(false);
	hdrstack *payload2 = NULL;

	if(STREAM_SEND_BUF - from >= len){
		//折り返さない
		payload1->size = len;
		payload1->next = NULL;
		payload1->buf = &(tcb->send_buf[from]);
		*next_from = (from+len) % STREAM_SEND_BUF;
		return payload1;
	}else{
		//折り返す
		payload1->size = STREAM_SEND_BUF-from;
		payload1->next = payload2;
		payload1->buf = &(tcb->send_buf[from]);
		payload2 = new hdrstack(false);
		payload2->size = len - payload1->size;
		payload2->next = NULL;
		payload2->buf = tcb->send_buf;
		*next_from = payload2->size;
		return payload1;
	}
}


static void tcp_send_from_buf(tcp_ctrlblock *tcb){
	int sendbuf_tail = (tcb->send_unack+tcb->send_used_len) % STREAM_SEND_BUF -1;
	uint32_t send_start = tcb->send_next;
	uint32_t send_last = MIN(tcb->send_next+tcb->send_window-1, sendbuf_tail);

	if(!between_le_lt(tcb->send_unack, tcb->send_next, MOD(tcb->send_unack+tcb->send_used_len, STREAM_SEND_BUF))){
		LOG("No data to send. (send_unack=%d, send_next=%d, tail=%d)",
				tcb->send_unack, tcb->send_next,sendbuf_tail);
		goto sendfin; //新たに送信可能なものはない
	}

	uint32_t remaining;
	LOG("send start=%d, last=%d(send_unack=%d, send_next=%d, tail=%d)", send_start, send_last,
		tcb->send_unack, tcb->send_next,sendbuf_tail);
	if(send_start < send_last){
		remaining = send_last - send_start + 1;
	}else{
		remaining = STREAM_SEND_BUF - send_last;
		remaining += send_start + 1;
	}

	if(remaining == 0)
		goto sendfin;

	tcp_hdr *tcphdr_template;
	tcphdr_template = new tcp_hdr;
	tcphdr_template->th_dport = hton16(tcb->sock->partner_port);
	tcphdr_template->th_flags = TH_ACK;
	tcphdr_template->th_off = sizeof(tcp_hdr)/4;
	tcphdr_template->th_sport = hton16(tcb->sock->my_port);
	tcphdr_template->th_sum = 0;
	tcphdr_template->th_urp = 0;
	tcphdr_template->th_x2 = 0;

	while(remaining > 0){
		int next_start;
		int payload_len = MIN(tcb->mss, remaining);
		hdrstack *payload = sendbuf_to_hdrstack(tcb, send_start, payload_len, &next_start);

		tcp_timer_option *opt;
		opt = new tcp_timer_option;
		opt->option.resend.start_index = send_start;
		opt->option.resend.end_index = (tcb->send_next+payload_len-1)%STREAM_SEND_BUF;
		opt->option.resend.start_seq = tcb->send_next_seq;
		opt->option.resend.end_seq = tcb->send_next_seq + payload_len -1;

		remaining -= payload_len;
		send_start = next_start;

		hdrstack *tcpseg = new hdrstack(true);
		tcpseg->size = sizeof(tcp_hdr);
		tcpseg->buf = new char[sizeof(tcp_hdr)];
		memcpy(tcpseg->buf, tcphdr_template, sizeof(tcp_hdr));
		tcpseg->next = payload;

		tcp_hdr *thdr = (tcp_hdr*)tcpseg->buf;
		thdr->th_seq = hton32(tcb->send_next_seq);
		thdr->th_ack = hton32(tcb->recv_next_seq);
		tcb->send_next_seq += payload_len;
		tcb->send_next = (tcb->send_next+payload_len)%STREAM_SEND_BUF;
		thdr->th_win = hton16(tcb->recv_window);
		thdr->th_sum = tcp_checksum_send(tcpseg, IPADDR, tcb->sock->partner_addr);

		LOG("tcp segment was sent (len=%d)", payload_len);
		ip_send(tcpseg, tcb->sock->partner_addr, IPTYPE_TCP);


		tcp_timer_add(tcb->sock, tcb->rtt, TCP_TIMER_TYPE_RESEND, opt);
	}

sendfin:
	if(tcb->myfin_state == FIN_REQUESTED){
		tcp_timer_option *opt;
		opt = new tcp_timer_option;
		opt->option.resend.start_index = 0;
		opt->option.resend.end_index = 0;
		opt->option.resend.start_seq = tcb->myfin_seq;
		opt->option.resend.end_seq = tcb->myfin_seq;

		tcb->send_next_seq++;
		tcb->send_next = (tcb->send_next+1)%STREAM_SEND_BUF;

		tcp_send_ctrlseg(tcb->myfin_seq, tcb->recv_next_seq, tcb->recv_window, TH_FIN|TH_ACK, NULL, tcb->sock->partner_addr, tcb->sock->partner_port, tcb->sock->my_port);
		LOG("FIN sent. (send_unack=%d, send_next=%d, tail=%d, myfin_seq)",
				tcb->send_unack, tcb->send_next,sendbuf_tail, tcb->myfin_seq);
		tcp_timer_add(tcb->sock, tcb->rtt, TCP_TIMER_TYPE_RESEND, opt);
	}

	return;
}

static bool tcp_resend_from_buf(tcp_ctrlblock *tcb, uint32_t start_index, uint32_t end_index, uint32_t start_seq, uint32_t end_seq){
	LOG("resend start...");
	uint32_t send_start = start_index, send_last = end_index;
	uint32_t send_next_seq = start_seq, send_next = start_index;
	int remaining;

	if(tcb->myfin_state == FIN_REQUESTED && start_seq == tcb->myfin_seq && end_seq == tcb->myfin_seq){
		//FINの再送
		tcp_send_ctrlseg(tcb->myfin_seq, tcb->recv_next_seq, tcb->recv_window, TH_FIN|TH_ACK, NULL, tcb->sock->partner_addr, tcb->sock->partner_port, tcb->sock->my_port);
		LOG("FIN resend");
		return true;
	}
	if(!between_le_lt(tcb->send_unack, end_index, MOD(tcb->send_unack+tcb->send_used_len, STREAM_SEND_BUF))){
		LOG("no resend");
		return false; //送信可能なものはない
	}

	if(send_start < send_last){
		remaining = send_last - send_start + 1;
	}else{
		remaining = STREAM_SEND_BUF - send_last;
		remaining += send_start + 1;
	}

	if(remaining == 0){
		LOG("no resend: remaining=0");
		return false;
	}else{
		LOG("resend %d byte(s)", remaining);
	}

	tcp_hdr *tcphdr_template;
	tcphdr_template = new tcp_hdr;
	tcphdr_template->th_dport = hton16(tcb->sock->partner_port);
	tcphdr_template->th_flags = TH_ACK;
	tcphdr_template->th_off = sizeof(tcp_hdr)/4;
	tcphdr_template->th_sport = hton16(tcb->sock->my_port);
	tcphdr_template->th_sum = 0;
	tcphdr_template->th_urp = 0;
	tcphdr_template->th_x2 = 0;

	while(remaining > 0){
		int next_start;
		int payload_len = MIN(tcb->mss, remaining);
		hdrstack *payload = sendbuf_to_hdrstack(tcb, send_start, payload_len, &next_start);
		remaining -= payload_len;
		send_start = next_start;

		hdrstack *tcpseg = new hdrstack(true);
		tcpseg->size = sizeof(tcp_hdr);
		tcpseg->buf = new char[sizeof(tcp_hdr)];
		memcpy(tcpseg->buf, tcphdr_template, sizeof(tcp_hdr));
		tcpseg->next = payload;

		tcp_hdr *thdr = (tcp_hdr*)tcpseg->buf;
		thdr->th_seq = hton32(send_next_seq);
		thdr->th_ack = hton32(tcb->recv_next_seq);
		send_next_seq += payload_len;
		send_next = (send_next+payload_len)%STREAM_SEND_BUF;
		thdr->th_win = hton16(tcb->recv_window);
		thdr->th_sum = tcp_checksum_send(tcpseg, IPADDR, tcb->sock->partner_addr);

		LOG("sending... remaining=%d, total=%d", remaining, hdrstack_totallen(tcpseg));
		ip_send(tcpseg, tcb->sock->partner_addr, IPTYPE_TCP);
		mcled_change(COLOR_LIGHTBLUE);
	}

	return true;
}

static int tcp_write_to_sendbuf(tcp_ctrlblock *tcb, char *data, uint32_t len, TMO timeout){
	wai_sem(tcb->sbufsem);
	tcb->send_waiting = true;
	uint32_t remain = len;
	while(remain > 0){
		if(tcb->send_used_len < tcb->send_window){
			tcb->send_buf[(tcb->send_unack+tcb->send_used_len)%STREAM_SEND_BUF] = *data++;
			tcb->send_used_len++;
			remain--;
			//LOG("1byte wrote to sendbuf");
		}else{
			sig_sem(tcb->sbufsem);
			if(tslp_tsk(timeout) == E_TMOUT){
				wai_sem(tcb->sbufsem);
				tcb->send_waiting = false;
				sig_sem(tcb->sbufsem);
				return len-remain;
			}
			wai_sem(tcb->sbufsem);
		}
	}
	tcb->send_waiting = false;
	sig_sem(tcb->sbufsem);
	//LOG("completed(%d byte(s) wrote.)", len - remain);
	return len - remain;
}

static int tcp_read_from_recvbuf(tcp_ctrlblock *tcb, char *data, uint32_t len, TMO timeout){
	LOG("[entered]");
	wai_sem(tcb->rbufsem);
	LOG("[start]");
	tcb->recv_waiting = true;
	uint32_t remain = len;
	uint32_t head = (tcb->recv_next+tcb->recv_window)%STREAM_RECV_BUF;
	while(true){
		while(STREAM_RECV_BUF > tcb->recv_window && remain > 0){
			LOG("read from recvbuf!");
			*data++ = tcb->recv_buf[head];
			tcb->recv_window++;
			remain--;
			head = (head+1) % STREAM_RECV_BUF;
		}
		//最低1byteは読む
		if(remain == len){
			sig_sem(tcb->rbufsem);
			if(tslp_tsk(timeout) == E_TMOUT){
				wai_sem(tcb->rbufsem);
				tcb->recv_waiting = false;
				sig_sem(tcb->rbufsem);
				return ETIMEOUT;
			}
		}else{
			tcb->recv_waiting = false;
			sig_sem(tcb->rbufsem);
			return len - remain;
		}
	}
}


static void tcp_process_otherwise(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb){
	if(payload_len == 0 && tcb->recv_window == 0){
		if(ntoh32(thdr->th_seq) != tcb->recv_next_seq)
			goto cantrecv;
	}else if(payload_len == 0 && tcb->recv_window > 0){
		if(!between_le_lt(tcb->recv_next_seq, ntoh32(thdr->th_seq), tcb->recv_next_seq+tcb->recv_window))
			goto cantrecv;
	}else if(payload_len > 0 && tcb->recv_window == 0){
		goto cantrecv;
	}else if(payload_len > 0 && tcb->recv_window > 0){
		if(!(between_le_lt(tcb->recv_next_seq, ntoh32(thdr->th_seq), tcb->recv_next_seq+tcb->recv_window) ||
				between_le_lt(tcb->recv_next_seq, ntoh32(thdr->th_seq)+payload_len-1, tcb->recv_next_seq+tcb->recv_window)))
			goto cantrecv;
	}

	if(thdr->th_flags & TH_RST){
		switch(tcb->state){
		case TCP_STATE_SYN_RCVD:
			if(tcb->opentype == PASSIVE){
				tcb->state = TCP_STATE_LISTEN;
			}else{
				tcb->state = TCP_STATE_CLOSED;
				tcb->errno = ECONNREFUSED;
				if(tcb->establish_waiting) wup_tsk(tcb->sock->ownertsk);
			}
			goto exit;
			break;
		case TCP_STATE_ESTABLISHED:
		case TCP_STATE_FIN_WAIT_1:
		case TCP_STATE_FIN_WAIT_2:
		case TCP_STATE_CLOSE_WAIT:
			tcb_reset(tcb);
			tcb->errno = ECONNRESET;
			if(tcb->recv_waiting || tcb->send_waiting) wup_tsk(tcb->sock->ownertsk);
			goto exit;
			break;
		case TCP_STATE_CLOSING:
		case TCP_STATE_LAST_ACK:
		case TCP_STATE_TIME_WAIT:
			tcb_reset(tcb);
			goto exit;
			break;
		}
	}

	if(thdr->th_flags & TH_SYN){
		switch(tcb->state){
		case TCP_STATE_SYN_RCVD:
			if(tcb->opentype == PASSIVE){
				tcb->state = TCP_STATE_LISTEN;
				goto exit;
			}
		case TCP_STATE_ESTABLISHED:
		case TCP_STATE_FIN_WAIT_1:
		case TCP_STATE_FIN_WAIT_2:
		case TCP_STATE_CLOSE_WAIT:
		case TCP_STATE_CLOSING:
		case TCP_STATE_LAST_ACK:
		case TCP_STATE_TIME_WAIT:
			tcb_reset(tcb);
			tcb->errno = ECONNRESET;
			if(tcb->recv_waiting || tcb->send_waiting) wup_tsk(tcb->sock->ownertsk);
			tcp_send_ctrlseg(ntoh32(thdr->th_ack), tcb->recv_next_seq, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
			goto exit;
			break;
		}
	}

	if(thdr->th_flags & TH_ACK){
		switch(tcb->state){
		case TCP_STATE_SYN_RCVD:
			if(between_le_le(tcb->send_unack_seq, hton32(thdr->th_ack), tcb->send_next_seq)){
				mcled_change(COLOR_BLUE);
				tcb->send_window = ntoh16(thdr->th_win);
				tcb->send_wl1 = ntoh32(thdr->th_seq);
				tcb->send_wl2 = ntoh32(thdr->th_ack);
				tcb->state = TCP_STATE_ESTABLISHED;
				tcb_allocbuf(tcb);
				if(tcb->establish_waiting) wup_tsk(tcb->sock->ownertsk);
			}else{
				tcp_send_ctrlseg(ntoh32(thdr->th_ack), tcb->recv_next_seq, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
				goto exit;
			}
		case TCP_STATE_ESTABLISHED:
		case TCP_STATE_FIN_WAIT_1:
		case TCP_STATE_FIN_WAIT_2:
		case TCP_STATE_CLOSE_WAIT:
		case TCP_STATE_CLOSING:
			if(between_lt_le(tcb->send_unack_seq, ntoh32(thdr->th_ack), tcb->send_next_seq)){
				tcb->send_unack_seq = ntoh32(thdr->th_ack);
				uint32_t unack_before = tcb->send_unack;
				tcb->send_unack = MOD(tcb->send_unack_seq - tcb->iss - 1, STREAM_SEND_BUF);
				tcb->send_used_len -= tcb->send_unack - unack_before;
			}else if(ntoh32(thdr->th_ack) != tcb->send_unack_seq){
				LOG("ACKSEQ received = %d, tcb = %d", ntoh32(thdr->th_ack), tcb->send_unack_seq);
				tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
				goto exit;
			}
			if(between_le_le(tcb->send_unack_seq, ntoh32(thdr->th_ack), tcb->send_next_seq)){
				if(between_lt_le(tcb->send_wl1, ntoh32(thdr->th_seq), tcb->recv_next_seq+tcb->recv_window) ||
					 (tcb->send_wl1==ntoh32(thdr->th_seq)
						&& between_le_le(tcb->send_wl2, ntoh32(thdr->th_ack), tcb->send_next_seq))){
					tcb->send_window = ntoh16(thdr->th_win);
					tcb->send_wl1 = ntoh32(thdr->th_seq);
					tcb->send_wl2 = ntoh32(thdr->th_ack);
				}
			}
			if(tcb->state == TCP_STATE_FIN_WAIT_1){
				if(ntoh32(thdr->th_ack)-1 == tcb->myfin_seq){
					tcb->state = TCP_STATE_FIN_WAIT_2;
					tcb->myfin_state = FIN_ACKED;
				}
			}
			/*if(tcb->state == TCP_STATE_FIN_WAIT_2){
				if(tcb->send_used_len == 0){

				}
			}*/
			if(tcb->state == TCP_STATE_CLOSING){
				if(ntoh32(thdr->th_ack)-1 == tcb->myfin_seq){
					tcb->state = TCP_STATE_TIME_WAIT;
					tcb->myfin_state = FIN_ACKED;
				}else{
					goto exit;
				}
			}
			break;
		case TCP_STATE_LAST_ACK:
			if(ntoh32(thdr->th_ack)-1 == tcb->myfin_seq){
				tcb->myfin_state = FIN_ACKED;
				tcb_reset(tcb);
				goto exit;
			}
			break;
		case TCP_STATE_TIME_WAIT:

			break;
		}
	}else{
		goto exit;
	}

	if(payload_len > 0){
		switch(tcb->state){
		case TCP_STATE_ESTABLISHED:
		case TCP_STATE_FIN_WAIT_1:
		case TCP_STATE_FIN_WAIT_2:
			tcp_write_to_recvbuf(tcb, ((char*)thdr)+thdr->th_off*4, ntoh16(iphdr->ip_len)-iphdr->ip_hl*4-thdr->th_off*4);
			//遅延ACKタイマ開始
			//tcp_timer_add(tcb->sock, )
			tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
			break;
		}
	}

	if(thdr->th_flags & TH_FIN){
		switch(tcb->state){
		case TCP_STATE_CLOSED:
		case TCP_STATE_LISTEN:
		case TCP_STATE_SYN_SENT:
			goto exit;
			break;
		}

		tcb->recv_next_seq++;
		LOG("ACK for fin(recv_next_seq=%d)", tcb->recv_next_seq);
		tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));

		switch(tcb->state){
		case TCP_STATE_SYN_RCVD:
		case TCP_STATE_ESTABLISHED:
			tcb->state = TCP_STATE_CLOSE_WAIT;
			break;
		case TCP_STATE_FIN_WAIT_1:
			if(ntoh32(thdr->th_ack)-1 == tcb->myfin_seq){
				tcb->myfin_state = FIN_ACKED;
				tcb->state = TCP_STATE_TIME_WAIT;
				tcp_timer_remove_all(tcb->sock);
				tcp_timer_add(tcb->sock, 600000, TCP_TIMER_TYPE_TIMEWAIT, NULL);
			}else{
				tcb->state = TCP_STATE_CLOSING;
			}
			break;
		case TCP_STATE_FIN_WAIT_2:
			tcb->state = TCP_STATE_TIME_WAIT;
			tcp_timer_remove_all(tcb->sock);
			tcp_timer_add(tcb->sock, 600000, TCP_TIMER_TYPE_TIMEWAIT, NULL);
			break;
		case TCP_STATE_TIME_WAIT:
			tcp_timer_remove_all(tcb->sock);
			tcp_timer_add(tcb->sock, 600000, TCP_TIMER_TYPE_TIMEWAIT, NULL);
			break;
		}
	}

	goto exit;

cantrecv:
	//ACK送信
	if(!(thdr->th_flags & TH_RST))
		tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport));
exit:
	delete flm;
	return;
}

void tcp_process(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr){
	tcp_ctrlblock *tcb;
	uint16_t payload_len;

	//LOG("TCP segment received!");

	//ブロードキャスト/マルチキャストアドレスは不許可
	if(memcmp(iphdr->ip_dst, IPADDR, IP_ADDR_LEN) != 0){
		LOG("tcp packet discarded(bad address).");
		goto exit;
	}
	//ヘッダ検査
	if(flm->size < sizeof(ether_hdr)+(iphdr->ip_hl*4)+sizeof(tcp_hdr) ||
		flm->size < sizeof(ether_hdr)+(iphdr->ip_hl*4)+(thdr->th_off*4)){
		LOG("tcp packet discarded(length error).");
		goto exit;
	}


	if(tcp_checksum_recv(iphdr, thdr) != 0){
		LOG("tcp packet discarded(checksum error).");
		goto exit;
	}

	payload_len = ntoh16(iphdr->ip_len) - iphdr->ip_hl*4 - thdr->th_off*4;

	int s;
	s = -1;
	wai_sem(SOCKTBL_SEM);
	for(int i=0;i<MAX_SOCKET;i++){
		socket_t *sock = &sockets[i];
		if(sock->type==SOCK_STREAM && sock->my_port==ntoh16(thdr->th_dport)){
			if(sock->partner_port == ntoh16(thdr->th_sport) &&
				memcmp(sock->partner_addr, iphdr->ip_src, IP_ADDR_LEN) == 0){
				s = i; break;
			}else{
				//listen中
				s=i;
			}
		}
	}
	sig_sem(SOCKTBL_SEM);

	if(s==-1 || sockets[s].ctrlblock.tcb->state == TCP_STATE_CLOSED){
		tcp_process_closed(flm, iphdr, thdr, payload_len);
		return;
	}

	tcb = sockets[s].ctrlblock.tcb;
	//LOG("sock(%d) type = %d", s,tcb->state);

	switch(tcb->state){
	case TCP_STATE_LISTEN:
		tcp_process_listen(flm, iphdr, thdr, payload_len, tcb, s);
		break;
	case TCP_STATE_SYN_SENT:
		tcp_process_synsent(flm, iphdr, thdr, payload_len, tcb);
		break;
	default:
		tcp_process_otherwise(flm, iphdr, thdr, payload_len, tcb);
		break;
	}

	return;
exit:
	delete flm;
	return;
}


int tcp_connect(tcp_ctrlblock *tcb, uint8_t to_addr[], uint16_t to_port, uint16_t my_port, TMO timeout){
	switch(tcb->state){
	case TCP_STATE_CLOSED:
		break;
	case TCP_STATE_LISTEN:
		if(tcb->state == TCP_STATE_LISTEN){
			tcb->backlog = 0;
			if(tcb->sockqueue != NULL)
				delete [] tcb->sockqueue;
		}
		break;
	default:
		return ECONNEXIST;
	}

	tcb->iss = tcp_geninitseq();
	tcp_send_ctrlseg(tcb->iss, 0, STREAM_RECV_BUF, TH_SYN, make_tcpopt(true, MSS), to_addr, to_port, my_port);
	tcb->send_unack_seq = tcb->iss;
	tcb->send_unack = 0;
	tcb->send_next_seq = tcb->iss+1;
	tcb->send_next = 0;
	tcb->state = TCP_STATE_SYN_SENT;
	tcb->opentype = ACTIVE;
	tcb->establish_waiting = true;
	while(true){
		if(tslp_tsk(timeout) == E_TMOUT){
			tcb->establish_waiting = false;
			tcp_close(tcb->sock, tcb);
			return ETIMEOUT;
		}
		if(tcb->state == TCP_STATE_ESTABLISHED){
			tcb->establish_waiting = false;
			return 0;
		}else if(tcb->state == TCP_STATE_CLOSED){
			return tcb->errno;
		}
	}
}

int tcp_listen(tcp_ctrlblock *tcb, int backlog){
	switch(tcb->state){
	case TCP_STATE_CLOSED:
	case TCP_STATE_LISTEN:
		break;
	default:
		return ECONNEXIST;
	}
	tcb->state = TCP_STATE_LISTEN;
	tcb->opentype = PASSIVE;
	tcb_allocsockq(tcb, backlog);
	return 0;
}

int tcp_accept(int s, tcp_ctrlblock *tcb, uint8_t client_addr[], uint16_t *client_port, TMO timeout){
	socket_t *sock = &sockets[s];
	tcp_ctrlblock *pending;
	int pending_id;
	switch(tcb->state){
	case TCP_STATE_LISTEN:
		tcb->accept_waiting = true;
		while(true){
			if(tcb->sockqueue_len > 0){
				pending_id = tcb->sockqueue[tcb->sockqueue_head];
				pending = sockets[pending_id].ctrlblock.tcb;
				tcb->sockqueue_len--;
				tcb->sockqueue_head = (tcb->sockqueue_head+1)%tcb->backlog;
				pending->iss = tcp_geninitseq();
				pending->send_next_seq = pending->iss+1;
				pending->send_next = 0;
				pending->send_unack_seq = pending->iss;
				pending->send_unack = 0;

				hdrstack *opt = make_tcpopt(true, MSS);
				tcp_send_ctrlseg(pending->iss, pending->recv_next_seq, STREAM_RECV_BUF, TH_SYN|TH_ACK, opt, pending->sock->partner_addr, pending->sock->partner_port, sock->my_port);
				delete opt;
				pending->state = TCP_STATE_SYN_RCVD;

				tcb->accept_waiting = false;

				memcpy(client_addr, pending->sock->partner_addr, IP_ADDR_LEN);
				*client_port = pending->sock->partner_port;
				break;
			}else{
				LOG("sockqueue empty(sleep)");
				if(tslp_tsk(timeout) == E_TMOUT){
					tcb->accept_waiting = false;
					return ETIMEOUT;
				}
			}
		}

		pending->establish_waiting = true;
		while(true){
			if(pending->state == TCP_STATE_ESTABLISHED){
				pending->establish_waiting = false;
				return pending_id;
			}else if(pending->state == TCP_STATE_CLOSED){
				return tcb->errno;
			}
			LOG("accept: trying...(state:%d)", pending->state);
			if(tslp_tsk(timeout) == E_TMOUT){
				pending->establish_waiting = false;
				tcp_close(pending->sock, pending);
				return ETIMEOUT;
			}
			//tslp_tsk(1000);
		}
	default:
		return ENOTLISITENING;
	}
}

int tcp_send(tcp_ctrlblock *tcb, char *msg, uint32_t len, TMO timeout){
	switch(tcb->state){
	case TCP_STATE_CLOSED:
	case TCP_STATE_LISTEN:
	case TCP_STATE_SYN_SENT:
	case TCP_STATE_SYN_RCVD:
		return ECONNNOTEXIST;
		break;
	case TCP_STATE_ESTABLISHED:
	case TCP_STATE_CLOSE_WAIT:
		{
			int retval = tcp_write_to_sendbuf(tcb, msg, len, timeout);
			LOG("wuptsk err = %d", wup_tsk(TCP_SEND_TASK));
			return retval;
		}
	case TCP_STATE_FIN_WAIT_1:
	case TCP_STATE_FIN_WAIT_2:
	case TCP_STATE_CLOSING:
	case TCP_STATE_LAST_ACK:
	case TCP_STATE_TIME_WAIT:
		return ECONNCLOSING;
		break;
	}
}

int tcp_receive(tcp_ctrlblock *tcb, char *buf, uint32_t len, TMO timeout){
	switch(tcb->state){
	case TCP_STATE_CLOSED:
	case TCP_STATE_LISTEN:
	case TCP_STATE_SYN_SENT:
	case TCP_STATE_SYN_RCVD:
		return ECONNNOTEXIST;
		break;
	case TCP_STATE_ESTABLISHED:
	case TCP_STATE_FIN_WAIT_1:
	case TCP_STATE_FIN_WAIT_2:
		return tcp_read_from_recvbuf(tcb, buf, len, timeout);
		break;
	case TCP_STATE_CLOSE_WAIT:
		if(tcb->recv_window == STREAM_RECV_BUF){
			//バッファが空の時...これ以上受信されることはない
			return ECONNCLOSING;
		}
		return tcp_read_from_recvbuf(tcb, buf, len, timeout);
		break;
	case TCP_STATE_CLOSING:
	case TCP_STATE_LAST_ACK:
	case TCP_STATE_TIME_WAIT:
		return ECONNCLOSING;
		break;
	}
}

int tcp_close(socket_t *sock, tcp_ctrlblock *tcb){
	switch(tcb->state){
	case TCP_STATE_CLOSED:
		return ECONNNOTEXIST;
		break;
	case TCP_STATE_LISTEN:
		tcb_reset(tcb);
		break;
	case TCP_STATE_SYN_SENT:
		tcb_reset(tcb);
		break;
	case TCP_STATE_SYN_RCVD:
		tcb->myfin_seq = tcb->send_next_seq;
		tcb->myfin_state = FIN_REQUESTED;
		tcb->state = TCP_STATE_FIN_WAIT_1;
		wup_tsk(TCP_SEND_TASK);
		break;
	case TCP_STATE_ESTABLISHED:
		tcb->myfin_seq = tcb->send_next_seq;
		tcb->myfin_state = FIN_REQUESTED;
		LOG("fin requested!");
		tcb->state = TCP_STATE_FIN_WAIT_1;
		wup_tsk(TCP_SEND_TASK);
		break;
	case TCP_STATE_FIN_WAIT_1:
	case TCP_STATE_FIN_WAIT_2:
		return ECONNCLOSING;
		break;
	case TCP_STATE_CLOSE_WAIT:
		tcb->myfin_seq = tcb->send_next_seq;
		tcb->send_next_seq++;
		tcb->send_next = MOD(tcb->send_next+1, STREAM_SEND_BUF);
		tcb->myfin_state = FIN_REQUESTED;
		wup_tsk(TCP_SEND_TASK);
		tcp_timer_add(tcb->sock, 6000, TCP_TIMER_TYPE_FINACK, NULL);
		break;
	case TCP_STATE_CLOSING:
	case TCP_STATE_LAST_ACK:
	case TCP_STATE_TIME_WAIT:
		return ECONNCLOSING;
		break;
	}
}


void tcp_timer_task(intptr_t exinf) {
	while(true){
		wai_sem(TCP_TIMER_SEM);
		if(tcptimer!=NULL){
			tcptimer->remaining--;
			LOG("[timer] %d", tcptimer->remaining);
		}
		while(tcptimer!=NULL && tcptimer->remaining<=0){
			LOG("[timer] timeout! type:%d", tcptimer->type);
			switch(tcptimer->type){
			case TCP_TIMER_REMOVED:
				if(tcptimer->option==NULL)
					delete tcptimer->option;
				break;
			case TCP_TIMER_TYPE_FINACK:
				tcb_reset(tcptimer->sock->ctrlblock.tcb);
				if(tcptimer->option==NULL)
					delete tcptimer->option;
				break;
			case TCP_TIMER_TYPE_RESEND:
				if(tcp_resend_from_buf(tcptimer->sock->ctrlblock.tcb, tcptimer->option->option.resend.start_index,
														 tcptimer->option->option.resend.end_index,
														 tcptimer->option->option.resend.start_seq,
														 tcptimer->option->option.resend.end_seq)){
					tcp_timer_add_locked(tcptimer->sock, tcptimer->sec*2, TCP_TIMER_TYPE_RESEND, tcptimer->option);
				}
				break;
			case TCP_TIMER_TYPE_TIMEWAIT:
				tcb_reset(tcptimer->sock->ctrlblock.tcb);
				if(tcptimer->option==NULL)
					delete tcptimer->option;
				break;
			case TCP_TIMER_TYPE_DELAYACK:
				{
					tcp_ctrlblock *tcb = tcptimer->sock->ctrlblock.tcb;
					tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL,
											tcb->sock->partner_addr, tcb->sock->partner_port, tcb->sock->my_port);
					if(tcptimer->option==NULL)
						delete tcptimer->option;
					break;
				}
			}

			tcp_timer_t *temp = tcptimer;
			tcptimer = tcptimer->next;
			delete temp;
		}
		sig_sem(TCP_TIMER_SEM);

		slp_tsk();
	}
}

void tcp_timer_cyc(intptr_t exinf) {
	iwup_tsk(TCP_TIMER_TASK);
}

void tcp_send_task(intptr_t exinf){
	while(true){
		wai_sem(SOCKTBL_SEM);
		LOG("[send task start]");
		for(int i=0;i<MAX_SOCKET;i++){
			socket_t *sock = &sockets[i];
			if(sock->type==SOCK_STREAM && sock->ctrlblock.tcb!=NULL){
				switch(sock->ctrlblock.tcb->state){
				case TCP_STATE_ESTABLISHED:
				case TCP_STATE_CLOSE_WAIT:
				case TCP_STATE_FIN_WAIT_1:
				case TCP_STATE_CLOSING:
				case TCP_STATE_LAST_ACK:
					LOG("**send_from_buf**");
					tcp_send_from_buf(sock->ctrlblock.tcb);
					break;
				}
			}
		}
		sig_sem(SOCKTBL_SEM);
		slp_tsk();
	}
}

void tcp_send_cyc(intptr_t exinf) {
	iwup_tsk(TCP_SEND_TASK);
}
