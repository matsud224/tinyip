#pragma once

#include <kernel.h>

#include <stdint.h>

#include "protohdr.h"
#include "sockif.h"

struct tcp_ctrlblock;


tcp_ctrlblock *tcb_new(socket_t *sock, ID recvsem, ID sendsem, ID infosem);
void tcb_dispose(tcp_ctrlblock *tcb);
void tcp_process(ether_flame *flm, ip_hdr *iphdr, tcp_hdr *thdr);
int tcp_connect(tcp_ctrlblock *tcb, uint8_t to_addr[], uint16_t to_port, uint16_t my_port, TMO timeout);
int tcp_listen(tcp_ctrlblock *tcb, int backlog);
int tcp_accept(int s, tcp_ctrlblock *tcb, uint8_t client_addr[], uint16_t *client_port, TMO timeout);
int tcp_send(tcp_ctrlblock *tcb, char *msg, uint32_t len, TMO timeout);
int tcp_receive(tcp_ctrlblock *tcb, char *buf, uint32_t len, TMO timeout);
int tcp_close(socket_t *sock, tcp_ctrlblock *tcb);
