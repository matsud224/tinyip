#pragma once

#include <kernel.h>

#include <stdint.h>

#include "protohdr.h"

struct tcp_ctrlblock;

void tcp_process(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr);
tcp_ctrlblock *tcp_newcb(ID recvsem, ID sendsem); //Trasmission Control Blockの生成
void tcp_disposecb(tcp_ctrlblock *tcb);
