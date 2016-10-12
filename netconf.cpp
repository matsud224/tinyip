#include "netconf.h"
#include "protohdr.h"

uint8_t IPBROADCAST[IP_ADDR_LEN]={255, 255, 255, 255};
uint8_t IPADDR[IP_ADDR_LEN]={192, 168, 0, 21};
uint8_t NETMASK[IP_ADDR_LEN]={255, 255, 255, 0};
uint8_t DEFAULT_GATEWAY[IP_ADDR_LEN]={192, 168, 0, 5};
uint8_t DNSSERVER[IP_ADDR_LEN]={192, 168, 0, 5};
uint8_t NTPSERVER[IP_ADDR_LEN]={192, 168, 0, 5};
uint8_t MACADDR[ETHER_ADDR_LEN];
uint8_t ETHERBROADCAST[ETHER_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
