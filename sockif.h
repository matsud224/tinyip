#pragma once

#include <kernel.h>

#include <stdint.h>

#include "protohdr.h"
#include "netconf.h"

struct udp_ctrlblock;
struct tcp_ctrlblock;

struct transport_addr{
    uint16_t my_port;
    uint16_t partner_port;
    uint8_t partner_addr[IP_ADDR_LEN];
};

struct socket_t{
    int type;

    union{
		udp_ctrlblock *ucb;
		tcp_ctrlblock *tcb;
    } ctrlblock;

    //以下の２つはctrlblockにコピーされる
    ID ownertsk;
    transport_addr addr;
};

extern socket_t sockets[MAX_SOCKET];
