#include "arduino_app.h"
#include "mbed.h"
#include "TextLCD.h"

#include <cstring>

#include "tcp.h"
#include "ip.h"
#include "protohdr.h"
#include "util.h"
#include "netconf.h"
#include "errnolist.h"

#define MIN(x,y) ((x)<(y)?(x):(y))
#define MOD(x,y) ((x) % (y))

#define SEQ2IDX_SEND(seq, tcb) MOD((seq)-((tcb)->iss+1), (tcb)->send_buf_size)
#define SEQ2IDX_RECV(seq, tcb) MOD((seq)-((tcb)->irs+1), (tcb)->recv_buf_size)

//#define SEM_DEBUG

#ifdef SEM_DEBUG
#define SEMINFO(s) LOG(s)
#else
#define SEMINFO(s) ;
#endif

using namespace std;

struct tcp_timer_t;

struct tcp_arrival{
	tcp_arrival *next;
	uint32_t start_seq;
	uint32_t end_seq;
	~tcp_arrival(){
		if(this->next!=NULL) delete this->next;
	}
};

struct tcp_ctrlblock{
	uint32_t send_unack_seq; //未確認のシーケンス番号で、一番若いもの
	uint32_t send_next_seq; //次のデータ転送で使用できるシーケンス番号
	uint16_t send_window; //送信ウィンドウサイズ
	uint32_t send_wl1; //直近のウィンドウ更新に使用されたセグメントのシーケンス番号
	uint32_t send_wl2; //直近のウィンドウ更新に使用されたセグメントの確認番号
	uint32_t send_buf_used_len; //送信バッファに入っているデータのバイト数
	uint32_t send_buf_size;
	bool send_persisttim_enabled; //持続タイマが起動中か
	uint32_t iss; //初期送信シーケンス番号
	char *send_buf;
	bool send_waiting;

	uint32_t recv_next_seq; //次のデータ転送で使用できるシーケンス番号
	uint16_t recv_window; //受信ウィンドウサイズ
	uint32_t recv_buf_size; //受信バッファサイズ
	uint32_t recv_ack_counter; //ACKを送るごとに1づつカウントアップしていく
	uint32_t recv_fin_seq; //FINを受信後にセットされる、FINのシーケンス番号
	uint32_t irs; //初期受信シーケンス番号
	char *recv_buf;
	bool recv_waiting;
	tcp_arrival *arrival_list;

	int myfin_state;
#define FIN_NOTREQUESTED 0
#define FIN_REQUESTED 1
#define FIN_SENT 2
#define FIN_ACKED 3
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

	tcp_ctrlblock **tcbqueue; //tcp_ctrlblock*の配列
	int backlog; //ソケットキューのサイズ
	int tcbqueue_head; //先頭のインデックス
	int tcbqueue_len; //格納されている要素数

	bool is_userclosed; //close()が呼ばれたか。trueのときCLOSE状態に遷移したらソケット解放を行う

	ID ownertsk;
	transport_addr addr;
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
			uint32_t start_seq;
			uint32_t end_seq;
			uint8_t flags;
			bool has_mss;
			bool is_zerownd_probe;
			transport_addr addr; //send_ctrlsegで送った場合にこちらが優先される（tcbにアドレス未登録の場合があるから）
		} resend;
		struct{
			uint32_t seq;
		} delayack;
	} option;
#define resend_start_seq option.resend.start_seq
#define resend_end_seq option.resend.end_seq
#define resend_flags option.resend.flags
#define resend_has_mss option.resend.has_mss
#define resend_is_zerownd_probe option.resend.is_zerownd_probe
#define resend_addr option.resend.addr
#define delayack_seq option.delayack.seq
};

struct tcp_timer_t{
	tcp_timer_t *next; //連結リストにして、時間切れになる順につなぐ
	int type;
#define TCP_TIMER_REMOVED 0 //削除済みの印
#define TCP_TIMER_TYPE_FINACK 1
#define TCP_TIMER_TYPE_RESEND 2
#define TCP_TIMER_TYPE_TIMEWAIT 3
#define TCP_TIMER_TYPE_DELAYACK 4
	uint32_t remaining; //前に繋がれている要素の秒数の累計＋remainingで発動
	uint32_t msec; //タイマの設定時間（ミリ秒, TCP_TIMER_UNITの倍数）
	tcp_ctrlblock *tcb;
	tcp_timer_option *option;
};

struct tcb_list{
	tcb_list *next;
	tcp_ctrlblock *tcb;
};

tcp_timer_t *tcptimer = NULL;
tcb_list *tcblist = NULL;

static void tcp_timer_add(tcp_ctrlblock *tcb, uint32_t sec, int type, tcp_timer_option *opt);
static void tcp_timer_remove_all(tcp_ctrlblock *tcb);

static bool between_le_lt(uint32_t a, uint32_t b, uint32_t c);
static bool between_lt_le(uint32_t a, uint32_t b, uint32_t c);
static bool between_le_le(uint32_t a, uint32_t b, uint32_t c);
static bool between_lt_lt(uint32_t a, uint32_t b, uint32_t c);

tcp_ctrlblock *tcb_new();
static void tcb_alloc_queue(tcp_ctrlblock *tcb, int backlog);
static void tcb_alloc_buf(tcp_ctrlblock *tcb);
static void tcb_reset(tcp_ctrlblock *tcb);
static void tcblist_add(tcp_ctrlblock *tcb);
static void tcblist_add_lock(tcp_ctrlblock *tcb);
static void tcblist_remove(tcp_ctrlblock *tcb);
static void tcblist_remove_lock(tcp_ctrlblock *tcb);

static void tcp_abort(tcp_ctrlblock *tcb);

uint16_t tcp_get_unusedport();
static uint16_t tcp_checksum_recv(ip_hdr *iphdr, tcp_hdr *thdr);
static uint16_t tcp_checksum_send(hdrstack *seg, uint8_t ip_src[], uint8_t ip_dst[]);
static uint32_t tcp_geninitseq();
static hdrstack *make_tcpopt(bool contain_mss, uint16_t mss);
static uint16_t get_tcpopt_mss(tcp_hdr *thdr);
static void tcp_send_ctrlseg(uint32_t seq, uint32_t ack, uint16_t win, uint8_t flags, hdrstack *opt,
							 uint8_t to_addr[], uint16_t to_port, uint16_t my_port, bool use_resend, tcp_ctrlblock *tcb);
static hdrstack *sendbuf_to_hdrstack(tcp_ctrlblock *tcb, uint32_t from_index, uint32_t len);
static void tcp_send_from_buf(tcp_ctrlblock *tcb);
static bool tcp_resend_from_buf(tcp_ctrlblock *tcb, tcp_timer_option *opt);

void tcp_process(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr);
static void tcp_process_closed(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len);
static void tcp_process_listen(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb);
static void tcp_process_synsent(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb);
static void tcp_process_otherwise(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb);

static void tcp_write_to_recvbuf(tcp_ctrlblock *tcb, char *data, uint32_t start_seq, uint32_t end_seq);
static int tcp_write_to_sendbuf(tcp_ctrlblock *tcb, const char *data, uint32_t len, TMO timeout);
static int tcp_read_from_recvbuf(tcp_ctrlblock *tcb, char *data, uint32_t len, TMO timeout);

int tcp_connect(tcp_ctrlblock *tcb, TMO timeout);
int tcp_listen(tcp_ctrlblock *tcb, int backlog);
tcp_ctrlblock *tcp_accept(tcp_ctrlblock *tcb, uint8_t client_addr[], uint16_t *client_port, TMO timeout);
int tcp_send(tcp_ctrlblock *tcb, const char *msg, uint32_t len, TMO timeout);
int tcp_receive(tcp_ctrlblock *tcb, char *buf, uint32_t len, TMO timeout);
int tcp_close(tcp_ctrlblock *tcb);


void start_tcp(){
	act_tsk(TCP_SEND_TASK);
	sta_cyc(TCP_SEND_CYC);
	act_tsk(TCP_TIMER_TASK);
	sta_cyc(TCP_TIMER_CYC);
}

static void tcp_timer_add(tcp_ctrlblock *tcb, uint32_t sec, int type, tcp_timer_option *opt){
	//already locked.
	tcp_timer_t *tim = new tcp_timer_t;
	tim->type = type;
	tim->tcb = tcb;
	tim->option = opt;
	tim->msec = sec;

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
	if(*pp!=NULL){
		(*pp)->remaining -= sec;
	}

	return;
}

static void tcp_timer_remove_all(tcp_ctrlblock *tcb){
	tcp_timer_t *ptr = tcptimer;

	while(ptr!=NULL){
		if(ptr->tcb == tcb)
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

static void tcp_abort(tcp_ctrlblock *tcb){
	//already locked.
	switch(tcb->state){
	case TCP_STATE_SYN_RCVD:
	case TCP_STATE_ESTABLISHED:
	case TCP_STATE_FIN_WAIT_1:
	case TCP_STATE_FIN_WAIT_2:
	case TCP_STATE_CLOSE_WAIT:
		tcp_send_ctrlseg(tcb->send_next_seq, 0, 0, TH_RST, NULL,
						 tcb->addr.partner_addr, tcb->addr.partner_port, tcb->addr.my_port, false, NULL);
	}
	tcb_reset(tcb);
}

void tcb_init(tcp_ctrlblock *tcb){
	ID owner = tcb->ownertsk;
	bool rw = tcb->recv_waiting, sw = tcb->send_waiting,
	 aw = tcb->accept_waiting, ew = tcb->establish_waiting;
	int errno = tcb->errno;
	memset(tcb, 0, sizeof(tcp_ctrlblock));

	//以下は、リセット後に使用される　かつ　残っていても影響が無いため復元
	tcb->ownertsk = owner;
	tcb->recv_waiting = rw; tcb->send_waiting = sw;
	tcb->accept_waiting = aw; tcb->establish_waiting = ew;
	tcb->errno = errno;

	tcb->is_userclosed = false;
	tcb->send_persisttim_enabled = false;

	tcb->state = TCP_STATE_CLOSED;
	tcb->rtt = TCP_RTT_INIT;
	tcb->myfin_state = FIN_NOTREQUESTED;

	//まだメモリは確保していないけど、あらかじめサイズを相手方に通知しないといけないのでサイズだけ代入しておく
	tcb->recv_buf_size = STREAM_RECV_BUF;
	tcb->send_buf_size = STREAM_SEND_BUF;
}

static void tcb_reset(tcp_ctrlblock *tcb){
	if(tcb->backlog>0){
		int idx = tcb->tcbqueue_head;
		while(tcb->tcbqueue_len>0){
			tcb->tcbqueue[idx]->is_userclosed = true;
			tcp_abort(tcb->tcbqueue[idx]);
			idx = (idx+1) % tcb->backlog;
			tcb->tcbqueue_len--;
		}
		delete [] tcb->tcbqueue;
	}
	if(tcb->recv_buf!=NULL){
		delete [] tcb->recv_buf;
	}
	if(tcb->send_buf!=NULL){
		delete [] tcb->send_buf;
	}

	if(tcb->arrival_list != NULL){
		delete tcb->arrival_list;
	}

	tcp_timer_remove_all(tcb);

	tcblist_remove(tcb);

	if(tcb->is_userclosed){
		delete tcb;
	}else{
		tcb_init(tcb);
	}
	return;
}

//メモリ節約のため、接続を確立するまでバッファの領域確保は行わない
//確保は、後からtcp_allocbuf()で行う
tcp_ctrlblock *tcb_new(){
	tcp_ctrlblock *tcb = new tcp_ctrlblock;
	tcb_init(tcb);

	return tcb;
}



void tcb_setaddr_and_owner(tcp_ctrlblock *tcb, transport_addr *addr, ID owner){
	tcb->addr = *addr;
	tcb->ownertsk = owner;
}

transport_addr *tcb_getaddr(tcp_ctrlblock *tcb){
	return &(tcb->addr);
}

ID tcb_getowner(tcp_ctrlblock *tcb){
	return tcb->ownertsk;
}

static void tcb_alloc_queue(tcp_ctrlblock *tcb, int backlog){
	if(tcb->tcbqueue != NULL){
		delete [] tcb->tcbqueue;
		tcb->tcbqueue = NULL;
	}
	if(backlog > 0){
		tcb->backlog = backlog;
		tcb->tcbqueue_head = 0;
		tcb->tcbqueue_len = 0;
		tcb->tcbqueue = new tcp_ctrlblock*[backlog];
	}else{
		tcb->backlog = 0;
		tcb->tcbqueue_head = 0;
		tcb->tcbqueue_len = 0;
		tcb->tcbqueue = NULL;
	}
	return;
}

//ESTABLISHEDになってはじめてバッファを確保する
static void tcb_alloc_buf(tcp_ctrlblock *tcb){
	//2MSL待ちが長いと、メモリ不足で以下のnewで例外発生の可能性がある（今のところ対策はしていない）
	tcb->recv_buf = new char[tcb->recv_buf_size];
	tcb->send_buf = new char[tcb->send_buf_size];
	tcb->send_wl1 = 0;
	tcb->send_wl2 = 0;
	tcb->recv_window = tcb->recv_buf_size;
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
		*((uint16_t*)(opt_next+2)) = hton16(mss);
		opt_next += 4;
	}

	return opt;
}

//見つからければデフォルト値を返す
static uint16_t get_tcpopt_mss(tcp_hdr *thdr){
	uint8_t *optptr = ((uint8_t*)(thdr+1));
	uint8_t *datastart =  ((uint8_t*)thdr) + thdr->th_off*4;
	while(optptr < datastart){
		switch(*optptr){
		case TCP_OPT_END_OF_LIST:
			return -1;
		case TCP_OPT_NOP:
			optptr++;
			break;
		case TCP_OPT_MSS:
			return ntoh16(*((uint16_t*)(optptr+2)));
		default:
			optptr += (*(optptr+1))-2;
			break;
		}
	}
	return 536;
}


//制御用のセグメント（ペイロードなし）を送信
static void tcp_send_ctrlseg(uint32_t seq, uint32_t ack, uint16_t win, uint8_t flags, hdrstack *opt,
							 uint8_t to_addr[], uint16_t to_port, uint16_t my_port, bool use_resend, tcp_ctrlblock *tcb){
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

	tcp_timer_option *timer_opt;
	if(use_resend){
		timer_opt = new tcp_timer_option;
		timer_opt->resend_start_seq = seq;
		timer_opt->resend_end_seq = seq;
		timer_opt->resend_flags = flags;
		timer_opt->resend_has_mss = !(opt==NULL);
		timer_opt->resend_is_zerownd_probe = false;
		timer_opt->resend_addr.my_port = my_port;
		timer_opt->resend_addr.partner_port = to_port;
		memcpy(timer_opt->resend_addr.partner_addr, to_addr, IP_ADDR_LEN);
	}

	ip_send(tcpseg, to_addr, IPTYPE_TCP);

	if(use_resend)
		tcp_timer_add(tcb, tcb->rtt*TCP_TIMER_UNIT, TCP_TIMER_TYPE_RESEND, timer_opt);
}


static void tcp_process_closed(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len){
	if(!(thdr->th_flags & TH_RST)){
		if(thdr->th_flags & TH_ACK){
			tcp_send_ctrlseg(0, ntoh32(thdr->th_seq)+payload_len, 0, TH_ACK|TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
		}else
			tcp_send_ctrlseg(ntoh32(thdr->th_ack), 0, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
	}
	delete flm;
}

static void tcp_process_listen(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb){
	if(thdr->th_flags & TH_RST){
		goto exit;
	}
	if(thdr->th_flags & TH_ACK){
		tcp_send_ctrlseg(ntoh32(thdr->th_ack), 0, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
		goto exit;
	}
	if(thdr->th_flags & TH_SYN){
		//キューに入れる
		tcp_ctrlblock *newtcb;
		if(tcb->backlog > tcb->tcbqueue_len){
			//空き
			newtcb = tcb_new();
			tcb->tcbqueue[(tcb->tcbqueue_head+tcb->tcbqueue_len)%tcb->backlog] = newtcb;
			tcb_setaddr_and_owner(newtcb, &(tcb->addr), tcb->ownertsk);
			tcb->tcbqueue_len++;
		}else{
			if(tcb->backlog>0) LOG("Error: tcbqueue is full.");
			goto exit;
		}
        memcpy(newtcb->addr.partner_addr, iphdr->ip_src, IP_ADDR_LEN);

        newtcb->state = TCP_STATE_LISTEN;

        newtcb->addr.partner_port = ntoh16(thdr->th_sport);
		newtcb->recv_next_seq = ntoh32(thdr->th_seq)+1;
		newtcb->irs = ntoh32(thdr->th_seq);

		newtcb->send_window = MIN(ntoh16(thdr->th_win), tcb->send_buf_size);
		newtcb->send_wl1 = ntoh32(thdr->th_seq);
		newtcb->send_wl2 = ntoh32(thdr->th_ack);

		newtcb->mss = MIN(get_tcpopt_mss(thdr), MSS);
		tcblist_add(newtcb);
		if(tcb->accept_waiting) wup_tsk(tcb->ownertsk);
	}

exit:
	delete flm;
	return;
}

static void tcp_process_synsent(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr, uint16_t payload_len, tcp_ctrlblock *tcb){
	if(thdr->th_flags & TH_ACK){
		if(ntoh32(thdr->th_ack) != tcb->iss+1){
			if(!(thdr->th_flags & TH_RST)){
				tcp_send_ctrlseg(ntoh32(thdr->th_ack), 0, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
			}
			goto exit;
		}else{
			tcb->send_unack_seq++;
		}
	}
	if(thdr->th_flags & TH_RST){
		if(between_le_le(tcb->send_unack_seq, ntoh32(thdr->th_ack), tcb->send_next_seq)){
			tcb_reset(tcb);
			if(tcb->establish_waiting) wup_tsk(tcb->ownertsk);
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
			tcp_send_ctrlseg(tcb->iss, tcb->recv_next_seq, 0, TH_SYN|TH_ACK, make_tcpopt(true, MSS), iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), true, tcb);
			tcb->recv_ack_counter++;
		}else{
			tcb->send_window = MIN(ntoh16(thdr->th_win), tcb->send_buf_size);
			tcb->send_wl1 = ntoh32(thdr->th_seq);
			tcb->send_wl2 = ntoh32(thdr->th_ack);
			tcb->mss = MIN(get_tcpopt_mss(thdr), MSS);
			tcb_alloc_buf(tcb);
			tcb->state = TCP_STATE_ESTABLISHED;
			tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
			tcb->recv_ack_counter++;
			if(tcb->establish_waiting) wup_tsk(tcb->ownertsk);
		}
	}

exit:
	delete flm;
	return;
}


static void tcp_arrival_add(tcp_ctrlblock *tcb, uint32_t start_seq, uint32_t end_seq){
	tcp_arrival *newarrival = new tcp_arrival;
	newarrival->start_seq = start_seq;
	newarrival->end_seq = end_seq;

	tcp_arrival **pp = &(tcb->arrival_list);
	while((*pp)!=NULL){
		bool is_remove = false;
        if(between_le_le(tcb->recv_next_seq, (*pp)->start_seq-1, newarrival->end_seq)){
			//末尾が重なっているor連続している
			newarrival->end_seq = (*pp)->end_seq;
			is_remove = true;
        }
		if(between_le_le(tcb->recv_next_seq, newarrival->start_seq, (*pp)->end_seq+1)){
			//先頭が重なっているor連続している
			newarrival->start_seq = (*pp)->start_seq;
			is_remove = true;
		}
		if(is_remove){
			tcp_arrival *temp = *pp;
			*pp = (*pp)->next;
			delete temp;
		}
	}

	newarrival->next = tcb->arrival_list;
	tcb->arrival_list = newarrival;
}

//アプリケーションに引き渡せる連続したデータのバイト数を返す
static uint32_t tcp_arrival_handover(tcp_ctrlblock *tcb){
	tcp_arrival **pp = &(tcb->arrival_list);
	while((*pp)!=NULL){
        if(tcb->recv_next_seq == (*pp)->start_seq){
			uint32_t len = (*pp)->end_seq - (*pp)->start_seq + 1;
			tcb->recv_next_seq += len;
			tcb->recv_window -= len;
			//削除
			tcp_arrival *temp = *pp;
			*pp = (*pp)->next;
			delete temp;
			return len;
        }
	}
	LOG("---");
	return 0;
}

static void tcp_write_to_recvbuf(tcp_ctrlblock *tcb, char *data, uint32_t len, uint32_t start_seq){
	uint32_t start_index = SEQ2IDX_RECV(start_seq, tcb);
	uint32_t end_seq;
	uint32_t end_index;
	if(between_le_lt(start_seq, start_seq+len-1, tcb->recv_next_seq+tcb->recv_window))
		end_seq = start_seq+len-1;
	else
		end_seq = tcb->recv_next_seq+tcb->recv_window-1;

	end_index = SEQ2IDX_RECV(end_seq, tcb);

	uint32_t end_next_index = MOD(end_index+1, tcb->recv_buf_size);
	for(uint32_t i=start_index; i!=end_next_index; i=MOD(i+1, tcb->recv_buf_size)){
		tcb->recv_buf[i] = *data++;
	}

	tcp_arrival_add(tcb, start_seq, end_seq);
	if(tcb->recv_next_seq == start_seq){
		tcp_arrival_handover(tcb);
		if(tcb->recv_waiting) wup_tsk(tcb->ownertsk);
	}
}

//fromインデックスからlenバイトの区間をhdrstackとして返す（配列が循環していることを考慮して）
static hdrstack *sendbuf_to_hdrstack(tcp_ctrlblock *tcb, uint32_t from_index, uint32_t len){
	hdrstack *payload1 = new hdrstack(false);
	hdrstack *payload2 = NULL;
	if(tcb->send_buf_size - from_index >= len){
		//折り返さない
		payload1->size = len;
		payload1->next = NULL;
		payload1->buf = &(tcb->send_buf[from_index]);
		return payload1;
	}else{
		//折り返す
		payload1->size = tcb->send_buf_size-from_index;
		payload1->buf = &(tcb->send_buf[from_index]);
		payload2 = new hdrstack(false);
		payload1->next = payload2;
		payload2->size = len - payload1->size;
		payload2->next = NULL;
		payload2->buf = tcb->send_buf;
		return payload1;
	}
}


static void tcp_send_from_buf(tcp_ctrlblock *tcb){
	bool is_zerownd_probe = false;

	static tcp_hdr tcphdr_template;
	tcphdr_template.th_dport = hton16(tcb->addr.partner_port);
	tcphdr_template.th_flags = TH_ACK;
	tcphdr_template.th_off = sizeof(tcp_hdr)/4;
	tcphdr_template.th_sport = hton16(tcb->addr.my_port);
	tcphdr_template.th_sum = 0;
	tcphdr_template.th_urp = 0;
	tcphdr_template.th_x2 = 0;

	if(tcb->send_persisttim_enabled == false && tcb->send_window == 0){
		if(between_le_lt(tcb->send_unack_seq, tcb->send_next_seq, tcb->send_unack_seq+tcb->send_buf_used_len)){
			//1byte以上の送信可能なデータが存在
			is_zerownd_probe = true;
			tcb->send_persisttim_enabled = true;
		}
	}

	while(is_zerownd_probe ||
		(tcb->send_buf_used_len>0 &&
		!between_le_lt(tcb->send_unack_seq,
						tcb->send_unack_seq+tcb->send_buf_used_len-1, tcb->send_next_seq))){
		int payload_len_all;
		if(between_le_lt(tcb->send_next_seq, tcb->send_unack_seq+tcb->send_buf_used_len, tcb->send_next_seq+tcb->send_window))
			payload_len_all = tcb->send_unack_seq+tcb->send_buf_used_len - tcb->send_next_seq;
		else
			payload_len_all = tcb->send_next_seq+tcb->send_window - tcb->send_next_seq;
		int payload_len = MIN(payload_len_all, tcb->mss);

		if(is_zerownd_probe) payload_len = 1;
		if(payload_len == 0) break;

		hdrstack *payload = sendbuf_to_hdrstack(tcb, SEQ2IDX_SEND(tcb->send_next_seq, tcb), payload_len);

		tcp_timer_option *opt;
		opt = new tcp_timer_option;
		opt->option.resend.start_seq = tcb->send_next_seq;
		opt->option.resend.end_seq = tcb->send_next_seq + payload_len -1;
		opt->option.resend.flags = tcphdr_template.th_flags;
		opt->option.resend.has_mss = false;
		opt->option.resend.is_zerownd_probe = is_zerownd_probe;

		hdrstack *tcpseg = new hdrstack(true);
		tcpseg->size = sizeof(tcp_hdr);
		tcpseg->buf = new char[sizeof(tcp_hdr)];
		memcpy(tcpseg->buf, &tcphdr_template, sizeof(tcp_hdr));
		tcpseg->next = payload;

		tcp_hdr *thdr = (tcp_hdr*)tcpseg->buf;
		thdr->th_seq = hton32(tcb->send_next_seq);
		thdr->th_ack = hton32(tcb->recv_next_seq);
		thdr->th_win = hton16(tcb->recv_window);
		thdr->th_sum = tcp_checksum_send(tcpseg, IPADDR, tcb->addr.partner_addr);

		if(!is_zerownd_probe){
			tcb->send_next_seq += payload_len;
			tcb->send_window -= payload_len;
		}

		ip_send(tcpseg, tcb->addr.partner_addr, IPTYPE_TCP);
		tcb->recv_ack_counter++;


		tcp_timer_add(tcb, tcb->rtt*TCP_TIMER_UNIT, TCP_TIMER_TYPE_RESEND, opt);

		is_zerownd_probe = false; //1回だけ

		//送信タスク（これ）が長時間実行されることを防ぐために、オーバランハンドラで制限をかけたほうが良さそう
	}

	if(tcb->send_window > 0 && tcb->myfin_state == FIN_REQUESTED){
		//送信ウィンドウが0でなく、かつここまで降りてきてFIN送信要求が出ているということは送信すべきものがもう無いのでFINを送って良い
		tcb->myfin_state = FIN_SENT;
		tcb->myfin_seq = tcb->send_next_seq;
		tcb->send_next_seq++;

		tcp_send_ctrlseg(tcb->myfin_seq, tcb->recv_next_seq, tcb->recv_window, TH_FIN|TH_ACK, NULL, tcb->addr.partner_addr, tcb->addr.partner_port, tcb->addr.my_port, true, tcb);
		tcb->recv_ack_counter++;

		if(tcb->state == TCP_STATE_CLOSE_WAIT)
			tcb->state = TCP_STATE_LAST_ACK;
	}

	return;
}

static bool tcp_resend_from_buf(tcp_ctrlblock *tcb, tcp_timer_option *opt){
	if(!between_le_lt(tcb->send_unack_seq, opt->resend_end_seq, tcb->send_next_seq+tcb->send_window)){
		return false; //送信可能なものはない
	}

	//send_ctrlsegで送ったものか（データ無し）
	if(opt->resend_start_seq == opt->resend_end_seq){
		tcp_send_ctrlseg(opt->resend_start_seq, tcb->recv_next_seq, tcb->recv_window, opt->resend_flags,
						 opt->resend_has_mss?make_tcpopt(true, MSS):NULL, opt->resend_addr.partner_addr,
						 opt->resend_addr.partner_port, opt->resend_addr.my_port, false, NULL);
		if(opt->resend_flags & TH_ACK)
			tcb->recv_ack_counter++;
		return true;
	}

	int payload_len;
	if(opt->resend_is_zerownd_probe)
		payload_len = 1;
	else
		payload_len = opt->resend_end_seq - opt->resend_start_seq + 1;

	hdrstack *payload = sendbuf_to_hdrstack(tcb, SEQ2IDX_SEND(opt->resend_start_seq, tcb), payload_len);

	hdrstack *tcpseg = new hdrstack(true);
	tcpseg->size = sizeof(tcp_hdr);
	tcpseg->buf = new char[sizeof(tcp_hdr)];
	tcpseg->next = payload;

	tcp_hdr *thdr = (tcp_hdr*)tcpseg->buf;
	thdr->th_dport = hton16(tcb->addr.partner_port);
	thdr->th_flags = TH_ACK;
	thdr->th_off = sizeof(tcp_hdr)/4;
	thdr->th_sport = hton16(tcb->addr.my_port);
	thdr->th_sum = 0;
	thdr->th_urp = 0;
	thdr->th_x2 = 0;
	thdr->th_seq = hton32(opt->resend_start_seq);
	thdr->th_ack = hton32(tcb->recv_next_seq);
	thdr->th_win = hton16(tcb->recv_window);
	thdr->th_sum = tcp_checksum_send(tcpseg, IPADDR, tcb->addr.partner_addr);

	ip_send(tcpseg, tcb->addr.partner_addr, IPTYPE_TCP);
	tcb->recv_ack_counter++;

	return true;
}

static int tcp_write_to_sendbuf(tcp_ctrlblock *tcb, const char *data, uint32_t len, TMO timeout){
	//already locked.
	tcb->send_waiting = true;
	uint32_t remain = len;

	while(remain > 0){
		if(tcb->send_buf_used_len < tcb->send_buf_size){
			tcb->send_buf[SEQ2IDX_SEND(tcb->send_unack_seq+tcb->send_buf_used_len, tcb)] = *data++;
			remain--;
			tcb->send_buf_used_len++;
		}else{
			//already locked.
			wup_tsk(TCP_SEND_TASK);
			mcled_change(COLOR_PINK);
			sig_sem(TCP_SEM);
			if(tslp_tsk(timeout) == E_TMOUT){
				wai_sem(TCP_SEM);
				tcb->send_waiting = false;
				//ロックされている状態でtcp_sendにreturn
				return len-remain;
			}
			wai_sem(TCP_SEM);
			mcled_change(COLOR_GREEN);
			switch(tcb->state){
			case TCP_STATE_CLOSED:
			case TCP_STATE_LISTEN:
			case TCP_STATE_SYN_SENT:
			case TCP_STATE_SYN_RCVD:
				return ECONNNOTEXIST;
			case TCP_STATE_FIN_WAIT_1:
			case TCP_STATE_FIN_WAIT_2:
			case TCP_STATE_CLOSING:
			case TCP_STATE_LAST_ACK:
			case TCP_STATE_TIME_WAIT:
				return ECONNCLOSING;
			}
		}
	}
	tcb->send_waiting = false;
	return len - remain;
}

static int tcp_read_from_recvbuf(tcp_ctrlblock *tcb, char *data, uint32_t len, TMO timeout){
	//already locked.
	tcb->recv_waiting = true;
	uint32_t remain = len;
	uint32_t head_index = SEQ2IDX_RECV(tcb->recv_next_seq-(tcb->recv_buf_size-tcb->recv_window), tcb);
	while(true){
		if(tcb->recv_window == tcb->recv_buf_size){
			//バッファが空
			switch(tcb->state){
			case TCP_STATE_ESTABLISHED:
			case TCP_STATE_FIN_WAIT_1:
			case TCP_STATE_FIN_WAIT_2:
				break;
			default:
				//FINを受信していて、かつバッファが空
				if(tcb->recv_fin_seq==tcb->recv_next_seq){
					if(tcb->recv_buf!=NULL){
						delete [] tcb->recv_buf;
						tcb->recv_buf = NULL;
					}
				}
				return ECONNCLOSING;
			}
		}
		while(tcb->recv_window != tcb->recv_buf_size && remain > 0){
			*data++ = tcb->recv_buf[head_index];
			tcb->recv_window++;
			remain--;
			head_index = MOD(head_index+1, tcb->recv_buf_size);
		}
		//最低1byteは読む
		if(remain == len){
			//already locked.
			sig_sem(TCP_SEM);
			if(tslp_tsk(timeout) == E_TMOUT){
				wai_sem(TCP_SEM);
				tcb->recv_waiting = false;
				//ロック状態でreturn
				return ETIMEOUT;
			}
			wai_sem(TCP_SEM);

			switch(tcb->state){
			case TCP_STATE_CLOSED:
			case TCP_STATE_LISTEN:
			case TCP_STATE_SYN_SENT:
			case TCP_STATE_SYN_RCVD:
				return ECONNNOTEXIST;
			case TCP_STATE_CLOSING:
			case TCP_STATE_LAST_ACK:
			case TCP_STATE_TIME_WAIT:
				return ECONNCLOSING;
			}
		}else{
			tcb->recv_waiting = false;
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
				tcb_reset(tcb);
				tcb->errno = ECONNREFUSED;
				if(tcb->establish_waiting) wup_tsk(tcb->ownertsk);
			}
			goto exit;
			break;
		case TCP_STATE_ESTABLISHED:
		case TCP_STATE_FIN_WAIT_1:
		case TCP_STATE_FIN_WAIT_2:
		case TCP_STATE_CLOSE_WAIT:
			tcb_reset(tcb);
			tcb->errno = ECONNRESET;
			if(tcb->recv_waiting || tcb->send_waiting) wup_tsk(tcb->ownertsk);
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
			if(tcb->recv_waiting || tcb->send_waiting) wup_tsk(tcb->ownertsk);
			tcp_send_ctrlseg(ntoh32(thdr->th_ack), tcb->recv_next_seq, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
			goto exit;
			break;
		}
	}

	if(thdr->th_flags & TH_ACK){
		switch(tcb->state){
		case TCP_STATE_SYN_RCVD:
			if(between_le_le(tcb->send_unack_seq, hton32(thdr->th_ack), tcb->send_next_seq)){
				tcb->send_window = MIN(ntoh16(thdr->th_win), tcb->send_buf_size);
				tcb->send_wl1 = ntoh32(thdr->th_seq);
				tcb->send_wl2 = ntoh32(thdr->th_ack);
				tcb->send_unack_seq = hton32(thdr->th_ack);

				tcb_alloc_buf(tcb);

				tcb->state = TCP_STATE_ESTABLISHED;
				if(tcb->establish_waiting) wup_tsk(tcb->ownertsk);
			}else{
				tcp_send_ctrlseg(ntoh32(thdr->th_ack), tcb->recv_next_seq, 0, TH_RST, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
				goto exit;
			}
		case TCP_STATE_ESTABLISHED:
		case TCP_STATE_FIN_WAIT_1:
		case TCP_STATE_FIN_WAIT_2:
		case TCP_STATE_CLOSE_WAIT:
		case TCP_STATE_CLOSING:
			if(between_lt_le(tcb->send_unack_seq, ntoh32(thdr->th_ack), tcb->send_next_seq)){
				uint32_t unack_seq_before = tcb->send_unack_seq;
				tcb->send_unack_seq = ntoh32(thdr->th_ack);

				if(tcb->send_unack_seq >= unack_seq_before)
					tcb->send_buf_used_len -= tcb->send_unack_seq - unack_seq_before;
				else
					tcb->send_buf_used_len = (0xffffffff - unack_seq_before + 1) + tcb->send_unack_seq;
			}else if(ntoh32(thdr->th_ack) != tcb->send_unack_seq){
				tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
				tcb->recv_ack_counter++;
				goto exit;
			}
			if(between_le_le(tcb->send_unack_seq, ntoh32(thdr->th_ack), tcb->send_next_seq)){
				if(between_lt_le(tcb->send_wl1, ntoh32(thdr->th_seq), tcb->recv_next_seq+tcb->recv_window) ||
					 (tcb->send_wl1==ntoh32(thdr->th_seq)
						&& between_le_le(tcb->send_wl2, ntoh32(thdr->th_ack), tcb->send_next_seq))){
					tcb->send_window = MIN(ntoh16(thdr->th_win), tcb->send_buf_size);
					tcb->send_wl1 = ntoh32(thdr->th_seq);
					tcb->send_wl2 = ntoh32(thdr->th_ack);
				}
			}
			if(tcb->state == TCP_STATE_FIN_WAIT_1){
				if(tcb->myfin_state== FIN_ACKED || (tcb->myfin_state == FIN_SENT && ntoh32(thdr->th_ack)-1 == tcb->myfin_seq)){
					tcb->state = TCP_STATE_FIN_WAIT_2;
					tcb->myfin_state = FIN_ACKED;
				}
			}
			/*if(tcb->state == TCP_STATE_FIN_WAIT_2){
				if(tcb->send_used_len == 0){

				}
			}*/
			if(tcb->state == TCP_STATE_CLOSING){
				if(tcb->myfin_state== FIN_ACKED || (tcb->myfin_state == FIN_SENT && ntoh32(thdr->th_ack)-1 == tcb->myfin_seq)){
					tcb->state = TCP_STATE_TIME_WAIT;
					tcb->myfin_state = FIN_ACKED;
				}else{
					goto exit;
				}
			}
			break;
		case TCP_STATE_LAST_ACK:
			if(tcb->myfin_state== FIN_ACKED || (tcb->myfin_state == FIN_SENT && ntoh32(thdr->th_ack)-1 == tcb->myfin_seq)){
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
			tcp_write_to_recvbuf(tcb, ((char*)thdr)+thdr->th_off*4, payload_len, ntoh32(thdr->th_seq));
			//遅延ACKタイマ開始
			tcp_timer_option *opt = new tcp_timer_option;
			opt->option.delayack.seq = tcb->recv_ack_counter;
			tcp_timer_add(tcb, TCP_DELAYACK_TIME, TCP_TIMER_TYPE_DELAYACK, opt);
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

		/*
		if(ntoh32(thdr->th_seq) != tcb->recv_next_seq)
			goto exit;
		*/

		//相手からFINが送られてきたということは、こちらに全てのセグメントが到着している
		tcb->recv_fin_seq = ntoh32(thdr->th_seq);
		tcb->recv_next_seq = tcb->recv_fin_seq+1;
		tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq,
						 tcb->recv_window, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
		tcb->recv_ack_counter++;

		switch(tcb->state){
		case TCP_STATE_SYN_RCVD:
		case TCP_STATE_ESTABLISHED:
			tcb->state = TCP_STATE_CLOSE_WAIT;
			if(tcb->recv_waiting)
				wup_tsk(tcb->ownertsk);
			break;
		case TCP_STATE_FIN_WAIT_1:
			if(tcb->myfin_state== FIN_ACKED || (tcb->myfin_state == FIN_SENT && ntoh32(thdr->th_ack)-1 == tcb->myfin_seq)){
				tcb->state = TCP_STATE_TIME_WAIT;
				tcb->myfin_state = FIN_ACKED;
				tcp_timer_remove_all(tcb);
				tcp_timer_add(tcb, TCP_TIMEWAIT_TIME, TCP_TIMER_TYPE_TIMEWAIT, NULL);
			}else{
				tcb->state = TCP_STATE_CLOSING;
			}
			break;
		case TCP_STATE_FIN_WAIT_2:
			tcb->state = TCP_STATE_TIME_WAIT;
			tcp_timer_remove_all(tcb);
			tcp_timer_add(tcb, TCP_TIMEWAIT_TIME, TCP_TIMER_TYPE_TIMEWAIT, NULL);
			break;
		case TCP_STATE_TIME_WAIT:
			tcp_timer_remove_all(tcb);
			tcp_timer_add(tcb, TCP_TIMEWAIT_TIME, TCP_TIMER_TYPE_TIMEWAIT, NULL);
			break;
		}
	}



	goto exit;

cantrecv:
	//ACK送信
	if(!(thdr->th_flags & TH_RST)){
		tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL, iphdr->ip_src, ntoh16(thdr->th_sport), ntoh16(thdr->th_dport), false, NULL);
		tcb->recv_ack_counter++;
	}
exit:

	delete flm;
	return;
}

void tcp_process(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr){
	tcp_ctrlblock *tcb;
	uint16_t payload_len;

	//ブロードキャスト/マルチキャストアドレスは不許可
	if(memcmp(iphdr->ip_dst, IPADDR, IP_ADDR_LEN) != 0){
		goto exit;
	}
	//ヘッダ検査
	if(flm->size < sizeof(ether_hdr)+(iphdr->ip_hl*4)+sizeof(tcp_hdr) ||
		flm->size < sizeof(ether_hdr)+(iphdr->ip_hl*4)+(thdr->th_off*4)){
		goto exit;
	}


	if(tcp_checksum_recv(iphdr, thdr) != 0){
		goto exit;
	}

	payload_len = ntoh16(iphdr->ip_len) - iphdr->ip_hl*4 - thdr->th_off*4;

	SEMINFO("proctask waiting");
	wai_sem(TCP_SEM);
	SEMINFO("proctask get");

	tcb = NULL;
	for(tcb_list *ptr=tcblist; ptr!=NULL; ptr=ptr->next){
		if(ptr->tcb->addr.my_port==ntoh16(thdr->th_dport)){
			if(ptr->tcb->addr.partner_port == ntoh16(thdr->th_sport) &&
				memcmp(ptr->tcb->addr.partner_addr, iphdr->ip_src, IP_ADDR_LEN) == 0){
				tcb = ptr->tcb;
				break;
			}else{
				if(ptr->tcb->state == TCP_STATE_LISTEN)
					tcb = ptr->tcb;
			}
		}
	}

	if(tcb==NULL || tcb->state == TCP_STATE_CLOSED){
		tcp_process_closed(flm, iphdr, thdr, payload_len);
		sig_sem(TCP_SEM);
		return;
	}

	switch(tcb->state){
	case TCP_STATE_LISTEN:
		tcp_process_listen(flm, iphdr, thdr, payload_len, tcb);
		break;
	case TCP_STATE_SYN_SENT:
		tcp_process_synsent(flm, iphdr, thdr, payload_len, tcb);
		break;
	default:
		tcp_process_otherwise(flm, iphdr, thdr, payload_len, tcb);
		break;
	}

	sig_sem(TCP_SEM);

	return;
exit:
	delete flm;
	return;
}

static void tcblist_add(tcp_ctrlblock *tcb){
	tcb_list *entry = new tcb_list;
	entry->next = tcblist;
	entry->tcb = tcb;
	tcblist = entry;
}

static void tcblist_add_lock(tcp_ctrlblock *tcb){
	SEMINFO("add waiting");
	wai_sem(TCP_SEM);
	SEMINFO("add get");
	tcblist_add(tcb);
	sig_sem(TCP_SEM);
}

static void tcblist_remove(tcp_ctrlblock *tcb){
	tcb_list **pp = &tcblist;
	while((*pp)!=NULL){
		if((*pp)->tcb == tcb){
			tcb_list *temp = *pp;
			*pp = (*pp)->next;
			delete temp;
			break;
		}
		pp = &((*pp)->next);
	}
}

static void tcblist_remove_lock(tcp_ctrlblock *tcb){
	SEMINFO("remove waiting");
	wai_sem(TCP_SEM);
	SEMINFO("remove get");
	tcblist_remove(tcb);
	sig_sem(TCP_SEM);
}

int tcp_connect(tcp_ctrlblock *tcb, TMO timeout){
	switch(tcb->state){
	case TCP_STATE_CLOSED:
		break;
	case TCP_STATE_LISTEN:
		tcb->backlog = 0;
		if(tcb->tcbqueue != NULL){
			delete [] tcb->tcbqueue;
			tcb->tcbqueue = NULL;
		}
		break;
	default:
		return ECONNEXIST;
	}

	tcb->iss = tcp_geninitseq();
	tcp_send_ctrlseg(tcb->iss, 0, STREAM_RECV_BUF, TH_SYN, make_tcpopt(true, MSS), tcb->addr.partner_addr, tcb->addr.partner_port, tcb->addr.my_port, true, tcb);
	tcb->send_unack_seq = tcb->iss;
	tcb->send_next_seq = tcb->iss+1;
	tcb->state = TCP_STATE_SYN_SENT;
	tcb->opentype = ACTIVE;
	tcb->establish_waiting = true;

	tcblist_add_lock(tcb);
SEMINFO("connect waiting");
	wai_sem(TCP_SEM);
SEMINFO("connect get");
	while(true){
		if(tcb->state == TCP_STATE_ESTABLISHED){
			tcb->establish_waiting = false;
			sig_sem(TCP_SEM);
			return 0;
		}else if(tcb->state == TCP_STATE_CLOSED){
			int err = tcb->errno;
			sig_sem(TCP_SEM);
			return EAGAIN;
		}else{
			sig_sem(TCP_SEM);
			if(tslp_tsk(timeout) == E_TMOUT){
				wai_sem(TCP_SEM);
				tcb->establish_waiting = false;
				tcp_abort(tcb);
				sig_sem(TCP_SEM);
				return ETIMEOUT;
			}
			wai_sem(TCP_SEM);
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
	tcb_alloc_queue(tcb, backlog);
	tcb->state = TCP_STATE_LISTEN;
	tcb->opentype = PASSIVE;

	tcblist_add_lock(tcb);

	return 0;
}

tcp_ctrlblock *tcp_accept(tcp_ctrlblock *tcb, uint8_t client_addr[], uint16_t *client_port, TMO timeout){
	SEMINFO("accept waiting");
	wai_sem(TCP_SEM);
	SEMINFO("accept get");
	tcp_ctrlblock *pending;

	switch(tcb->state){
	case TCP_STATE_LISTEN:
retry:
		tcb->accept_waiting = true;
		while(true){
			if(tcb->tcbqueue_len > 0){
				pending = tcb->tcbqueue[tcb->tcbqueue_head];
				tcb->tcbqueue_len--;
				tcb->tcbqueue_head = (tcb->tcbqueue_head+1)%tcb->backlog;

				pending->iss = tcp_geninitseq();
				pending->send_next_seq = pending->iss+1;
				pending->send_unack_seq = pending->iss;

				tcp_send_ctrlseg(pending->iss, pending->recv_next_seq, STREAM_RECV_BUF, TH_SYN|TH_ACK, make_tcpopt(true, MSS),
									 pending->addr.partner_addr, pending->addr.partner_port, tcb->addr.my_port, true, pending);

				tcb->recv_ack_counter++;
				pending->state = TCP_STATE_SYN_RCVD;

				tcb->accept_waiting = false;

				memcpy(client_addr, pending->addr.partner_addr, IP_ADDR_LEN);
				*client_port = pending->addr.partner_port;

				//tcblist_add(pending);

				break;
			}else{
				//already locked.
				sig_sem(TCP_SEM);
				if(tslp_tsk(timeout) == E_TMOUT){
					wai_sem(TCP_SEM);
					tcb->accept_waiting = false;
					tcb->errno = ETIMEOUT;
					sig_sem(TCP_SEM);
					return NULL;
				}
				SEMINFO("accept2 waiting");
				wai_sem(TCP_SEM);
				SEMINFO("accept2 get");
			}
		}

		pending->establish_waiting = true;
		while(true){
			if(pending->state == TCP_STATE_ESTABLISHED){
				pending->establish_waiting = false;
				sig_sem(TCP_SEM);
				return pending;
			}else if(pending->state == TCP_STATE_CLOSED){
				pending->establish_waiting = false;
				pending->is_userclosed = true;
				tcb_reset(pending);
				goto retry;
			}

			//already locked.
			sig_sem(TCP_SEM);

			if(tslp_tsk(timeout) == E_TMOUT){
				SEMINFO("accept4 waiting");
				wai_sem(TCP_SEM);
				SEMINFO("accept4 get");
				pending->establish_waiting = false;
				pending->is_userclosed = true;
				tcp_abort(pending);
				tcb->errno = ETIMEOUT;
				sig_sem(TCP_SEM);
				return NULL;
			}
			SEMINFO("accept3 waiting");
			wai_sem(TCP_SEM);
			SEMINFO("accept3 get");
		}
		break;
	default:
		tcb->errno = ENOTLISITENING;
		sig_sem(TCP_SEM);
		return NULL;
	}
}

int tcp_send(tcp_ctrlblock *tcb, const char *msg, uint32_t len, TMO timeout){
	SEMINFO("send waiting");
	wai_sem(TCP_SEM);
	SEMINFO("send get");
	switch(tcb->state){
	case TCP_STATE_CLOSED:
	case TCP_STATE_LISTEN:
	case TCP_STATE_SYN_SENT:
	case TCP_STATE_SYN_RCVD:
		sig_sem(TCP_SEM);
		return ECONNNOTEXIST;
	case TCP_STATE_ESTABLISHED:
	case TCP_STATE_CLOSE_WAIT:
		{
			int retval = tcp_write_to_sendbuf(tcb, msg, len, timeout);
			sig_sem(TCP_SEM);
			wup_tsk(TCP_SEND_TASK);
			return retval;
		}
	case TCP_STATE_FIN_WAIT_1:
	case TCP_STATE_FIN_WAIT_2:
	case TCP_STATE_CLOSING:
	case TCP_STATE_LAST_ACK:
	case TCP_STATE_TIME_WAIT:
		sig_sem(TCP_SEM);
		return ECONNCLOSING;
	default:
		return -1;
	}
}

int tcp_receive(tcp_ctrlblock *tcb, char *buf, uint32_t len, TMO timeout){
	int result;
	SEMINFO("recv waiting");
	wai_sem(TCP_SEM);
	SEMINFO("recv get");
	switch(tcb->state){
	case TCP_STATE_CLOSED:
	case TCP_STATE_LISTEN:
	case TCP_STATE_SYN_SENT:
	case TCP_STATE_SYN_RCVD:
		sig_sem(TCP_SEM);
		return ECONNNOTEXIST;
	case TCP_STATE_ESTABLISHED:
	case TCP_STATE_FIN_WAIT_1:
	case TCP_STATE_FIN_WAIT_2:
	case TCP_STATE_CLOSE_WAIT:
		result = tcp_read_from_recvbuf(tcb, buf, len, timeout);
		sig_sem(TCP_SEM);
		return result;
	case TCP_STATE_CLOSING:
	case TCP_STATE_LAST_ACK:
	case TCP_STATE_TIME_WAIT:
		sig_sem(TCP_SEM);
		return ECONNCLOSING;
	default:
		return -1;
	}
}

int tcp_close(tcp_ctrlblock *tcb){
	SEMINFO("close waiting");
	wai_sem(TCP_SEM);
	SEMINFO("close get");
	int result = 0;
	tcb->is_userclosed = true;
	switch(tcb->state){
	case TCP_STATE_CLOSED:
		tcb_reset(tcb);
		result = ECONNNOTEXIST;
		break;
	case TCP_STATE_LISTEN:
		tcb_reset(tcb);
		break;
	case TCP_STATE_SYN_SENT:
		tcb_reset(tcb);
		break;
	case TCP_STATE_SYN_RCVD:
		tcb->myfin_state = FIN_REQUESTED;
		tcb->state = TCP_STATE_FIN_WAIT_1;
		wup_tsk(TCP_SEND_TASK);
		break;
	case TCP_STATE_ESTABLISHED:
		tcb->myfin_state = FIN_REQUESTED;
		tcb->state = TCP_STATE_FIN_WAIT_1;
		wup_tsk(TCP_SEND_TASK);
		break;
	case TCP_STATE_FIN_WAIT_1:
	case TCP_STATE_FIN_WAIT_2:
		result = ECONNCLOSING;
		break;
	case TCP_STATE_CLOSE_WAIT:
		tcb->myfin_state = FIN_REQUESTED;
		tcp_timer_add(tcb, TCP_FINWAIT_TIME, TCP_TIMER_TYPE_FINACK, NULL);
		wup_tsk(TCP_SEND_TASK);
		break;
	case TCP_STATE_CLOSING:
	case TCP_STATE_LAST_ACK:
	case TCP_STATE_TIME_WAIT:
		result = ECONNCLOSING;
		break;
	}

	sig_sem(TCP_SEM);
	return result;
}


void tcp_timer_task(intptr_t exinf) {
	while(true){
		SEMINFO("timertask waiting");
		wai_sem(TCP_SEM);
		SEMINFO("timertask get");

		if(tcptimer!=NULL){
			if(tcptimer->remaining!=0) tcptimer->remaining -= TCP_TIMER_UNIT;
		}
		while(tcptimer!=NULL && tcptimer->remaining==0){
			switch(tcptimer->type){
			case TCP_TIMER_REMOVED:
				if(tcptimer->option!=NULL)
					delete tcptimer->option;
				break;
			case TCP_TIMER_TYPE_FINACK:
				tcb_reset(tcptimer->tcb);
				if(tcptimer->option!=NULL)
					delete tcptimer->option;
				break;
			case TCP_TIMER_TYPE_RESEND:
				if(tcp_resend_from_buf(tcptimer->tcb, tcptimer->option)){
					if(tcptimer->option->option.resend.is_zerownd_probe){
						//ゼロウィンドウ・プローブ（持続タイマ）
						tcp_ctrlblock *tcb = tcptimer->tcb;
						if(tcb->send_window == 0){
							tcp_timer_add(tcptimer->tcb, MIN(tcptimer->msec*2, TCP_PERSIST_WAIT_MAX), TCP_TIMER_TYPE_RESEND, tcptimer->option);
						}else{
							tcb->send_persisttim_enabled = false;
						}
					}else{
						//通常の再送
						if(tcptimer->msec > TCP_RESEND_WAIT_MAX){
							tcp_ctrlblock *tcb = tcptimer->tcb;
							tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_RST, NULL,
												tcb->addr.partner_addr, tcb->addr.partner_port, tcb->addr.my_port, false, NULL);
							tcb_reset(tcb);
						}else{
							tcp_timer_add(tcptimer->tcb, tcptimer->msec*2, TCP_TIMER_TYPE_RESEND, tcptimer->option);
						}
					}
				}
				break;
			case TCP_TIMER_TYPE_TIMEWAIT:
				tcb_reset(tcptimer->tcb);
				if(tcptimer->option!=NULL)
					delete tcptimer->option;
				break;
			case TCP_TIMER_TYPE_DELAYACK:
				{
					tcp_ctrlblock *tcb = tcptimer->tcb;
					if(tcb->recv_ack_counter == tcptimer->option->option.delayack.seq){
						tcp_send_ctrlseg(tcb->send_next_seq, tcb->recv_next_seq, tcb->recv_window, TH_ACK, NULL,
											tcb->addr.partner_addr, tcb->addr.partner_port, tcb->addr.my_port, false, NULL);
						tcb->recv_ack_counter++;
					}
					if(tcptimer->option!=NULL)
						delete tcptimer->option;
					break;
				}
			}

			tcp_timer_t *temp = tcptimer;
			tcptimer = tcptimer->next;
			delete temp;
		}

		sig_sem(TCP_SEM);

		slp_tsk();
	}
}

void tcp_timer_cyc(intptr_t exinf) {
	iwup_tsk(TCP_TIMER_TASK);
}

void tcp_send_task(intptr_t exinf){
	while(true){
		SEMINFO("sendtask waiting");
		wai_sem(TCP_SEM);
		SEMINFO("sendtask get");
		for(tcb_list *ptr=tcblist; ptr!=NULL; ptr=ptr->next){
			switch(ptr->tcb->state){
			case TCP_STATE_ESTABLISHED:
			case TCP_STATE_CLOSE_WAIT:
			case TCP_STATE_FIN_WAIT_1:
			case TCP_STATE_CLOSING:
			case TCP_STATE_LAST_ACK:
				tcp_send_from_buf(ptr->tcb);
				if(ptr->tcb->send_waiting){
					wup_tsk(ptr->tcb->ownertsk);
				}
				break;
			}
		}

		sig_sem(TCP_SEM);
		slp_tsk();
	}
}

void tcp_send_cyc(intptr_t exinf) {
	iwup_tsk(TCP_SEND_TASK);
}
