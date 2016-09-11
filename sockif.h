#pragma once

#include <kernel.h>

#include <stdint.h>

#include "protohdr.h"
#include "netconf.h"

struct udp_ctrlblock;
struct tcp_ctrlblock;

struct socket_t{
    int type;
    ID ownertsk;
    uint16_t my_port;

    union{
		udp_ctrlblock *ucb;
		tcp_ctrlblock *tcb;
    } ctrlblock;
};

extern socket_t sockets[MAX_SOCKET];
