#include <cstring>

#include "util.h"
#include "errnolist.h"
#include "netconf.h"
#include "netlib.h"
#include "envdep.h"
#include "nameresolver.h"

struct dns_hdr {
	uint16_t id;
	#ifdef BIG_ENDIAN
		unsigned qr:1, opcode:4, aa:1, tc:1, rd:1;
		unsigned ra:1, z:3, rcode:4;
	#endif //BIG_ENDIAN
	#ifdef LITTLE_ENDIAN
		unsigned rd:1, tc:1, aa:1, opcode:4, qr:1;
		unsigned rcode:4, z:3, ra:1;
	#endif // LITTLE_ENDIAN
	uint16_t qdcount;
	uint16_t ancount;
	uint16_t nscount;
	uint16_t arcount;
};

#define DNS_SERVER_PORT 53
#define DNS_TIMEOUT 1000

#define DNS_BUF_LEN 2048

#define DNS_QR_QUERY 0
#define DNS_QR_RESPONSE 1

#define DNS_OPCODE_STDQUERY 0
#define DNS_OPCODE_INVQUERY 1
#define DNS_OPCODE_SVRSTREQUEST 2

#define DNS_RCODE_NOERR 0
#define DNS_RCODE_FORMATERR 1
#define DNS_RCODE_SVRERR 2
#define DNS_RCODE_NAMEERR 3
#define DNS_RCODE_NOTIMPL 4
#define DNS_RCODE_DENIED 5

#define DNS_TYPE_A 1
#define DNS_TYPE_NS 2
#define DNS_TYPE_CNAME 5
#define DNS_TYPE_PTR 12
#define DNS_TYPE_MX 15
#define DNS_TYPE_AAAA 28
#define DNS_TYPE_ANY 255

#define DNS_CLASS_INET 1


struct dns_query {
	uint16_t q_type;
	uint16_t q_class;
};

struct dns_rrecord {
	uint16_t r_type;
	uint16_t r_class;
	uint32_t r_ttl;
	uint16_t r_resourcelen;
};


static uint16_t next_id = 0;

static uint16_t get_id(){
	SYSTIM tim;
	get_tim(&tim);
	next_id += tim;
	return next_id;
}

//slenはNULLを含まない
//reslenは返り値の長さ
char *transform_hostname(const char *host, int slen, int *reslen){
	*reslen = slen+2;
	char *data = new char[*reslen];
	memcpy(data+1, host, slen+1);
	for(int i=1; i<slen+1; i++){
		if(data[i]=='.') data[i]=0;
	}
	for(int i=0; i<slen+1; ){
		data[i] = strnlen(data+i+1, slen-i);
		i+=data[i]+1;
	}

	return data; //dataはNULL終端しない
}

//ptrから始まる照会名の次を返す
char *skip_queryname(char *ptr, char *limit){
	int len;
	while((len = *ptr++) != 0){
		if(ptr>=limit) return limit;
		if((len&0xC0)==0xC0)
			return ptr+1; //メッセージ圧縮
		else
			ptr+=len;
	}
	return ptr;
}

int getaddrinfo(const char *node, struct addrinfo **res){
	static char buf[DNS_BUF_LEN];
	dns_hdr *dhdr = (dns_hdr*)buf;
	int sock = socket(SOCK_DGRAM);

	*res = NULL;

	int result = 0, err;
	uint16_t id;
	int qnlen;
	char *queryname = transform_hostname(node, strlen(node), &qnlen);

	memset(dhdr, 0, sizeof(dns_hdr));
	id = dhdr->id = hton16(get_id());
	dhdr->rd = 1;
	dhdr->qdcount = hton16(1);

	memcpy((char*)(dhdr+1), queryname, qnlen);
	delete [] queryname;
	dns_query *query = 	(dns_query*)((char*)(dhdr+1) + qnlen);
	query->q_type = hton16(DNS_TYPE_A);
	query->q_class = hton16(DNS_CLASS_INET);

	if((err = sendto(sock, buf, sizeof(dns_hdr)+qnlen+sizeof(dns_query), 0, DNSSERVER, DNS_SERVER_PORT)) < 0){
		result = err;
		goto exit;
	}

	if((err = recvfrom(sock, buf, DNS_BUF_LEN, 0, NULL, NULL, DNS_TIMEOUT)) < 0){
		result = err;
		goto exit;
	}

	if(dhdr->id != id || dhdr->qr!=1 || ntoh16(dhdr->qdcount)!=1 || dhdr->ancount==0){
		result = EAGAIN;
		goto exit;
	}
	if(dhdr->rcode!=0){
		result = EFAIL;
		goto exit;
	}

	char *limit;
	limit = buf+DNS_BUF_LEN;
    //回答レコードまで進める
    char *ptr;
    ptr = (char*)(dhdr+1);
    ptr = skip_queryname(ptr, limit);
    ptr += sizeof(dns_query);

    dns_rrecord *rcd;
    //LOG("answer count = %d", ntoh16(dhdr->ancount));
	for(int i=ntoh16(dhdr->ancount); i>0&&ptr<limit; i--){
		ptr = skip_queryname(ptr, limit);
		if(ptr+sizeof(dns_rrecord) > limit) break;
		rcd = (dns_rrecord*)ptr;
		if(ntoh16(rcd->r_type) == DNS_TYPE_A && ntoh16(rcd->r_class) == DNS_CLASS_INET &&
			 ntoh16(rcd->r_resourcelen) == IP_ADDR_LEN){
			addrinfo *info = new addrinfo;
			memcpy(info->addr, ((char*)rcd)+10, IP_ADDR_LEN);
			//つなぐ
			info->next = *res;
			*res = info;
		}
		ptr+=10; //dns_rrecordのサイズ分加算（sizeofだとパディングはいる）
		ptr+=ntoh16(rcd->r_resourcelen);
	}

exit:
	close(sock);
	return result;
}
