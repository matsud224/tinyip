#pragma once

#include <stdint.h>
#include "protohdr.h"

#define MTU 1500
#define MSS (MTU-40)
#define IP_TTL 64
#define MAX_SOCKET 256
#define MAX_ARPTABLE 1024
#define ARBTBL_TIMEOUT_CLC 720 //10sec * 720 = 2hours
#define IPFRAG_TIMEOUT_CLC 6 //10sec * 6 = 1min
#define DGRAM_RECV_QUEUE 255
#define DGRAM_SEND_QUEUE 255
#define STREAM_RECV_BUF 2048
#define STREAM_SEND_BUF 2048

#define ETHER_SEND_SKIP 10 //設定した回数の送信ごとにフレームをスキップする(負の値で無効)
#define ETHER_RECV_SKIP 10

extern uint8_t IPADDR[IP_ADDR_LEN]; //192.168.0.10/24
extern uint8_t NETMASK[IP_ADDR_LEN];
extern uint8_t DEFAULT_GATEWAY[IP_ADDR_LEN]; //192.168.0.1
extern uint8_t MACADDR[ETHER_ADDR_LEN];
