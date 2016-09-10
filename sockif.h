#pragma once

#include <kernel.h>

#include <stdint.h>

#include "protohdr.h"
#include "netconf.h"


struct socket_t{
    int type;
    ID ownertsk;
    ID drsem; //データグラム受信キュー用
    //ID dssem; //データグラム送信キュー用
    ID srsem; //ストリーム受信バッファ用
    ID sssem; //ストリーム送信バッファ用
    uint16_t my_port;
	uint8_t dest_ipaddr[IP_ADDR_LEN];
    uint16_t dest_port;

    //キュー・バッファ共通
    //frontが次の書き込み位置、backが次の読み出し位置を指す
    int recv_front,recv_back;
    int send_front,send_back;

    bool recv_waiting; //受信待機中か

    union{
    	struct{
    		ether_flame **recv; //「ether_flameのポインタ」の配列
			//hdrstack **send;
    	} dgram_queue;
    	struct{
			char *recv;
			char *send;
    	} stream_buf;
    } pending;
#define dgram_recv_queue pending.dgram_queue.recv
//#define dgram_send_queue pending.dgram_queue.send
#define stream_recv_buf pending.stream_buf.recv
#define stream_send_buf pending.stream_buf.send
};

extern socket_t sockets[MAX_SOCKET];
