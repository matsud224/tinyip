#include "arduino_app.h"

#include <cstring>
#include <limits>
#include <list>
#include <stdint.h>

#include "ethernet.h"
#include "ip.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "util.h"
#include "netconf.h"
#include "protohdr.h"


using namespace std;

#define INF 0xffff
#define MIN(x,y) ((x)<(y)?(x):(y))

struct hole{
	hole *next;
	uint16_t first;
	uint16_t last;
};

struct fragment{
	fragment *next;
	uint16_t first;
	uint16_t last;
	ether_flame *flm;
	~fragment(){
		if(next!=NULL)
			delete next;
		delete flm;
	}
};

struct reasminfo{
	reasminfo *next;
	struct{
		uint8_t ip_src[IP_ADDR_LEN];
		uint8_t ip_dst[IP_ADDR_LEN];
		uint8_t ip_pro;
		uint16_t ip_id;
	} id;
	hole *holelist;
	fragment *fragmentlist;
	ether_flame *beginningflame;
	uint8_t headerlen; //etherヘッダ込
	uint16_t datalen;
	int16_t timeout; //タイムアウトまでのカウント

	~reasminfo(){
		hole *hptr=holelist;
		while(hptr!=NULL){
			hole *tmp = hptr;
			hptr=hptr->next;
			delete tmp;
		}
		fragment *fptr=fragmentlist;
		while(fptr!=NULL){
			fragment *tmp = fptr;
			fptr=fptr->next;
			delete tmp;
		}
	}
};

reasminfo *reasm_ongoing = NULL;

void timeout_10sec_task(intptr_t exinf) {
	//10sec周期で動き、タイムアウトを管理するタスク
	while(true){
		//IPフラグメント組み立てタイムアウト
		wai_sem(TIMEOUT_10SEC_SEM);
		reasminfo **pp = &reasm_ongoing;
		while(*pp!=NULL){
			if(--((*pp)->timeout) <= 0 || (*pp)->holelist==NULL){
				reasminfo *tmp=*pp;
				*pp=(*pp)->next;
				delete tmp;
			}
			pp=&((*pp)->next);
		}
		sig_sem(TIMEOUT_10SEC_SEM);

		//ARPテーブル
		wai_sem(ARPTBL_SEM);
		for(int i=0; i<MAX_ARPTABLE; i++){
			if(arptable[i].timeout>0){
				arptable[i].timeout--;
				if(arptable[i].timeout == 0){
					LOG("ARP entry: time out!");
					if(arptable[i].pending!=NULL)
						delete arptable[i].pending;
					arptable[i].pending = NULL;
				}else{
					if(arptable[i].pending!=NULL){
						ether_flame *request = make_arprequest_flame((uint8_t*)(&arptable[i].ipaddr));
						ethernet_send(request);
						delete request;
					}
				}
			}
		}
		sig_sem(ARPTBL_SEM);

		slp_tsk();
	}
}

void timeout_10sec_cyc(intptr_t exinf){
	iwup_tsk(TIMEOUT_10SEC_TASK);
}

static reasminfo *get_reasminfo(uint8_t ip_src[], uint8_t ip_dst[], uint8_t ip_pro, uint16_t ip_id){
	// already locked.
	reasminfo *ptr = reasm_ongoing;
	while(ptr!=NULL){
		if(memcmp(ptr->id.ip_src,ip_src,IP_ADDR_LEN)==0 &&
			memcmp(ptr->id.ip_dst,ip_dst,IP_ADDR_LEN)==0 &&
			ptr->id.ip_pro==ip_pro && ptr->id.ip_id==ip_id &&
			ptr->timeout > 0){
			return ptr;
		}
	}
	reasminfo *info = new reasminfo;
	memcpy(info->id.ip_src, ip_src, IP_ADDR_LEN);
	memcpy(info->id.ip_dst, ip_dst, IP_ADDR_LEN);
	info->id.ip_pro=ip_pro; info->id.ip_id=ip_id;
	info->timeout = IPFRAG_TIMEOUT_CLC;
	info->next = reasm_ongoing;
	hole *newh = new hole;
	newh->first=0;
	newh->last=INF;
	newh->next=NULL;
	info->holelist = newh;
	info->fragmentlist=NULL;
	info->beginningflame=NULL;
	reasm_ongoing = info;
	return info;
}

static void show_holelist(hole *holelist){
	LOG("---");
	while(holelist!=NULL){
		LOG("  %d ~ %d",holelist->first,holelist->last);
		holelist=holelist->next;
	}
	LOG("---");
}

//データ領域のサイズが分かったので、last=無限大(0xffff)のホールを修正
static void modify_inf_holelist(hole **holepp, uint16_t newsize){
	for(;*holepp!=NULL;holepp = &((*holepp)->next)){
		if((*holepp)->last==INF){
			(*holepp)->last=newsize;
			if((*holepp)->first==(*holepp)->last){
				hole *tmp=*holepp;
				*holepp = (*holepp)->next;
				delete tmp;
			}
			return;
		}
	}
}

void ip_process(ether_flame *flm, ip_hdr *iphdr){
	//正しいヘッダかチェック
	if(flm->size < sizeof(ether_hdr)+sizeof(ip_hdr) ||
		flm->size != sizeof(ether_hdr)+iphdr->ip_len ||
		iphdr->ip_v != 4 || iphdr->ip_hl < 5 ||
		checksum((uint16_t*)iphdr, iphdr->ip_hl*4) != 0 ){
		LOG("broken packet...");
		goto exit;
	}
	//自分宛てかチェック
	if(memcmp(iphdr->ip_dst, IPADDR, IP_ADDR_LEN) != 0){
		uint32_t addr=ntoh32(*(uint32_t*)IPADDR),
					mask=ntoh32(*(uint32_t*)NETMASK),broad;
		broad = hton32((addr & mask) | (~(mask)));
		if(memcmp(iphdr->ip_dst, &broad, IP_ADDR_LEN) != 0){
			LOG("ipaddress invalid...");
			goto exit;
		}
	}

	if(!((ntoh16(iphdr->ip_off) & IP_OFFMASK) == 0 && (ntoh16(iphdr->ip_off) & IP_MF) == 0)){
		//フラグメント
		wai_sem(TIMEOUT_10SEC_SEM);
		uint16_t ffirst = (ntoh16(iphdr->ip_off) & IP_OFFMASK)*8;
		uint16_t flast = ffirst + (ntoh16(iphdr->ip_len) - iphdr->ip_hl*4) - 1;

		reasminfo *info = get_reasminfo(iphdr->ip_src,iphdr->ip_dst,iphdr->ip_p,ntoh16(iphdr->ip_id));
		for(hole **holepp = &(info->holelist);*holepp!=NULL;){
			uint16_t hfirst=(*holepp)->first;
			uint16_t hlast=(*holepp)->last;
			//現holeにかぶっているか
			if(ffirst>hlast || flast<hfirst){
				holepp = &((*holepp)->next);
				continue;
			}
			//holeを削除
			hole *tmp=*holepp;
			*holepp = (*holepp)->next;
			delete tmp;
			//タイムアウトを延長
			info->timeout=IPFRAG_TIMEOUT_CLC;
			//現holeにかぶっているか
			if(ffirst>hfirst){
				//holeを追加
				hole *newh = new hole;
				newh->first=hfirst;
				newh->last=ffirst-1;
				newh->next=*holepp;
				*holepp=newh;
				holepp=&(newh->next);
			}
			if(flast<hlast){
				//holeを追加
				hole *newh = new hole;
				newh->first=flast+1;
				newh->last=hlast;
				newh->next=*holepp;
				*holepp=newh;
				holepp=&(newh->next);
			}
			//fragmentを追加
			fragment *newf = new fragment;
			newf->next=info->fragmentlist;
			info->fragmentlist=newf;
			newf->first=ffirst; newf->last=flast;
			newf->flm = flm;
			//more fragment がOFF
			if((ntoh16(iphdr->ip_off) & IP_MF) == 0){
				info->datalen=flast+1;
				modify_inf_holelist(&(info->holelist),info->datalen);
			}
			//はじまりのフラグメント
			if(ffirst==0){
				info->beginningflame = flm;
				info->headerlen=sizeof(ether_hdr)+iphdr->ip_hl*4;
			}

		}
		if(info->holelist==NULL){
			//holeがなくなり、おしまい
			//パケットを構築
			flm=new ether_flame;
			flm->size = info->headerlen+info->datalen;
			LOG("total %d/%d",info->headerlen,info->datalen);
			flm->buf = new char[flm->size];
			memcpy(flm->buf,info->beginningflame->buf,info->headerlen);
			char *origin = flm->buf + info->headerlen;
			int total=0;
            for(fragment *fptr=info->fragmentlist;fptr!=NULL;fptr=fptr->next){
				LOG("frag %d/%d %02X",fptr->first,fptr->last,*(((uint8_t*)(fptr->flm->buf))+info->headerlen));
				memcpy(origin+fptr->first,((uint8_t*)(fptr->flm->buf))+info->headerlen,fptr->last-fptr->first+1);
				total+=fptr->last-fptr->first+1;
            }
            //flmを上書きしたので、iphdrも修正必要
            iphdr = (ip_hdr*)(flm->buf+sizeof(ether_hdr));
            info->timeout=0;
		}else{
			sig_sem(TIMEOUT_10SEC_SEM);
			return;
		}
		sig_sem(TIMEOUT_10SEC_SEM);
	}

	switch(iphdr->ip_p){
	case IPTYPE_ICMP:
		icmp_process(flm, iphdr, (icmp*)(((uint8_t*)iphdr)+(iphdr->ip_hl*4)));
		break;
	case IPTYPE_TCP:
		tcp_process(flm, iphdr, (tcp_hdr*)(((uint8_t*)iphdr)+(iphdr->ip_hl*4)));
		break;
	case IPTYPE_UDP:
		udp_process(flm, iphdr, (udp_hdr*)(((uint8_t*)iphdr)+(iphdr->ip_hl*4)));
		break;
	}
	return;

exit:
	delete flm;
	return;
}

static uint16_t ip_id = 0;

static void prep_iphdr(ip_hdr *iphdr, uint16_t len, uint16_t id,
					bool mf, uint16_t offset, uint8_t proto, uint8_t dstaddr[]){
    iphdr->ip_v = 4;
    iphdr->ip_hl = sizeof(ip_hdr)/4;
    iphdr->ip_tos = 0x80;
    iphdr->ip_len = hton16(len);
    iphdr->ip_id = hton16(id);
    iphdr->ip_off = hton16((offset/8) | (mf?IP_MF:0));
    iphdr->ip_ttl = IP_TTL;
    iphdr->ip_p = proto;
    iphdr->ip_sum = 0;
    memcpy(iphdr->ip_src, IPADDR, IP_ADDR_LEN);
    memcpy(iphdr->ip_dst, dstaddr, IP_ADDR_LEN);

    iphdr->ip_sum = hton16(checksum((uint16_t*)iphdr, sizeof(ip_hdr)));
    return;
}

//dstaddrは書き換わるかもしれない
void ip_routing(uint8_t dstaddr[]){
	uint32_t myaddr=ntoh32(*(uint32_t*)IPADDR);
	uint32_t mymask=ntoh32(*(uint32_t*)NETMASK);
	uint32_t dst = ntoh32(*(uint32_t*)IPADDR);
	if((myaddr&mymask)!=(dst&mymask)){
		//同一のネットワークでない->デフォルトゲートウェイに流す
		memcpy(dstaddr, DEFAULT_GATEWAY, IP_ADDR_LEN);
	}
}

void ip_send(hdrstack *data, uint8_t *dstaddr, uint8_t proto){
	uint32_t datalen = hdrstack_totallen(data); //IPペイロード長
	uint32_t remainlen = datalen;
    uint16_t currentid; //今回のパケットに付与する識別子
    wai_sem(IPID_SEM);
    currentid = ip_id; ip_id++;
    sig_sem(IPID_SEM);

    //複数のフラグメントを送信する際、iphdr_itemはつなぐ先と内容を変えながら使い回せそうに思える
    //でも、送信はすぐに行われないかもしれない(MACアドレス解決待ち)ので使い回しはダメ
    LOG("total : %d",sizeof(ip_hdr)+remainlen);
    if(sizeof(ip_hdr)+remainlen <= MTU){
		hdrstack *iphdr_item = new hdrstack;
		iphdr_item->size = sizeof(ip_hdr);
		iphdr_item->buf = new char[sizeof(ip_hdr)];
		prep_iphdr((ip_hdr*)iphdr_item->buf, sizeof(ip_hdr)+remainlen, currentid, false, 0, proto, dstaddr);
		iphdr_item->next = data;
		arp_send(iphdr_item, dstaddr, ETHERTYPE_IP);
    }else{
		//フラグメント化必要
		//フラグメント化に際して、IPペイロードを分割後のパケットにコピーしないといけない
		while(remainlen > 0){
			uint32_t thispkt_totallen = MIN(remainlen+sizeof(ip_hdr), MTU);
			uint32_t thispkt_datalen = thispkt_totallen - sizeof(ip_hdr);
			uint16_t offset = datalen - remainlen;
			hdrstack *ippkt = new hdrstack;
			ippkt->next = NULL;
			remainlen -= thispkt_datalen;
			ippkt->size = thispkt_totallen;
			ippkt->buf = new char[ippkt->size];
			prep_iphdr((ip_hdr*)ippkt->buf, thispkt_totallen, currentid, (remainlen>0)?true:false
						, offset, proto, dstaddr);
			hdrstack_cpy((char*)(((ip_hdr*)ippkt->buf)+1), data, offset, thispkt_datalen);

			//ルーティング(dstaddrは書き換えられる可能性有)
			ip_routing(dstaddr);

			arp_send(ippkt, dstaddr, ETHERTYPE_IP);
		}
    }
}
