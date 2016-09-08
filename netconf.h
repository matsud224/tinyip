#pragma once

#include <stdint.h>
#include "protohdr.h"

#define IPFRAG_TIMEOUT_CLC 6 //10秒周期で6カウント
#define DGRAM_RECV_QUEUE 255
#define DGRAM_SEND_QUEUE 255
#define STREAM_RECV_BUF 2048
#define STREAM_SEND_BUF 2048

extern uint8_t IPADDR[IP_ADDR_LEN]; //192.168.0.10/24
extern uint8_t NETMASK[IP_ADDR_LEN];
extern uint8_t GATEWAY[IP_ADDR_LEN]; //192.168.0.1
extern uint8_t MACADDR[ETHER_ADDR_LEN];
