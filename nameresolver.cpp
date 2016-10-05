#include <cstring>

#include "util.h"
#include "errnolist.h"
#include "netconf.h"
#include "netlib.h"
#include "envdep.h"

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

struct dns_responce {
	uint16_t r_type;
	uint16_t r_class;
	uint32_t r_ttl;
	uint32_t r_resourcelen;
	uint8_t r_resourcedata[1];
};

//slenはNULLを含まない
char *transform_hostname(char *host, int slen){
	char *data = new char[slen+1];
	memcpy(data+1, host, slen);
	for(int i=1; i<slen+1; i++){
		if(data[i]=='.') data[i]=0;
	}
	for(int i=0; i<slen+1; ){
		data[i] = strnlen(data+i+1, slen-i);
		i+=data[i]+1;
	}
	return data; //dataはNULL終端しない
}

int getaddrinfo(const char *node, struct addrinfo **res){
	int sock = socket(SOCK_DGRAM);

}
