#include "arduino_app.h"

#include <cstring>
#include <limits>
#include <list>
#include <stdint.h>

#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "util.h"
#include "netconf.h"
#include "protohdr.h"


using namespace std;

#define INF 0xffff

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
	uint8_t timeout; //タイムアウトまでのカウント

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

void ipfrag_timeout_task(intptr_t exinf) {
	while(true){
		wai_sem(IPFRAG_TIMEOUT_SEM);
		reasminfo **pp = &reasm_ongoing;
		while(*pp!=NULL){
			if(--((*pp)->timeout) == 0 || (*pp)->holelist==NULL){
				reasminfo *tmp=*pp;
				*pp=(*pp)->next;
				delete tmp;
			}
			pp=&((*pp)->next);
		}
		sig_sem(IPFRAG_TIMEOUT_SEM);
		slp_tsk();
	}
}

void ipfrag_timeout_cyc(intptr_t exinf){
	iwup_tsk(IPFRAG_TIMEOUT_TASK);
}

static reasminfo *get_reasminfo(uint8_t ip_src[], uint8_t ip_dst[], uint8_t ip_pro, uint16_t ip_id){
	// already locked.
	reasminfo *ptr = reasm_ongoing;
	while(ptr!=NULL){
		if(memcmp(ptr->id.ip_src,ip_src,IP_ADDR_LEN)==0 &&
			memcmp(ptr->id.ip_dst,ip_dst,IP_ADDR_LEN)==0 &&
			ptr->id.ip_pro==ip_pro && ptr->id.ip_id==ip_id){
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
		wai_sem(IPFRAG_TIMEOUT_SEM);
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
			flm->buf = new char[flm->size];
			memcpy(flm->buf,info->beginningflame,info->headerlen);
			char *origin = flm->buf + info->headerlen;
            for(fragment *fptr=info->fragmentlist;fptr!=NULL;fptr=fptr->next)
				memcpy(origin+fptr->first,fptr->flm+info->headerlen,fptr->last-fptr->first+1);
		}else{
			sig_sem(IPFRAG_TIMEOUT_SEM);
			return;
		}
		sig_sem(IPFRAG_TIMEOUT_SEM);
	}

	switch(iphdr->ip_p){
	case IPTYPE_ICMP:
		mcled_change(COLOR_YELLOW);
		icmp_process(flm, iphdr, (icmp*)(((uint8_t*)iphdr)+(iphdr->ip_hl*4)));
		break;
	case IPTYPE_TCP:
		mcled_change(COLOR_LIGHTBLUE);
		tcp_process(flm, iphdr, (tcp_hdr*)(((uint8_t*)iphdr)+(iphdr->ip_hl*4)));
		break;
	case IPTYPE_UDP:
		mcled_change(COLOR_PINK);
		udp_process(flm, iphdr, (udp_hdr*)(((uint8_t*)iphdr)+(iphdr->ip_hl*4)));
		break;
	}
	return;

exit:
	delete flm;
	return;
}
