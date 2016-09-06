#pragma once

#include <stdint.h>
#include "ip.h"

struct icmp{
	uint8_t imcp_type;
	uint8_t icmp_code;
	uint16_t icmp_cksum;
	union{
		uint8_t ih_pptr;
		struct in_addr ih_gwaddr;
		struct ih_idseq{
			int16_t icd_id;
			int16_t icd_seq;
		} ih_idseq;
		uint32_t ih_void;
		struct ih_pmtu{
			uint16_t ipm_void;
			uint16_t ipm_nextmtu;
		} ih_pmtu;

		struct ih_rtradv{
			uint8_t irt_num_addrs;
			uint8_t irt_wpa;
			uint16_t irt_lifetime;
		} ih_rtradv;
	} icmp_hun;

#define icmp_pptr imcp_hun.ih_pptr
#define icmp_gwaddr icmp_hun.ih_gwaddr
#define icmp_id icmp_hun.ih_idseq.icd_id
#define icmp_seq icmp_hun.ih_idseq.icd_seq
#define icmp_void icmp_hun.ih_void
#define icmp_pmvoid icmp_hun.ih_pmtu.ipm_void
#define icmp_nextmtu icmp_hun.ih_pmtu.ipm_nextmtu
#define icmp_num_addrs icmp_hun.ih_rtradv.irt_num_addrs
#define icmp_wpa icmp_hun.ih_rtradv.irt_wpa
#define icmp_lifetime icmp_hun.ih_rtradv.irt_lifetime

	union{
		struct id_ts{
			uint32_t its_otime;
			uint32_t its_rtime;
			uint32_t its_ttime;
		} id_ts;
		struct id_ip{
			struct ip_hdr idi_ip;
		} id_ip;
		struct icmp_ra_addr{
			uint32_t ira_addr;
			uint32_t ira_preference;
		} ip_radv;;
		uint32_t id_mask;
		uint8_t id_data[1];
	} icmp_dun;

#define icmp_otime icmp_dun.id_ts.its_otime
#define icmp_rtime icmp_dun.id_ts.its_rtime
#define icmp_ttime icmp_dun.id_ts.its_ttime
#define icmp_ip icmp_dun.id_ip.idi_ip
#define icmp_radv icmp_dun.id_radv
#define icmp_mask icmp_dun.id_mask
#define icmp_data icmp_dun.id_data

};
