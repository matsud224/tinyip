#pragma once

#include <kernel.h>
#include <t_syslog.h>
#include <t_stdlib.h>
#include "syssvc/serial.h"
#include "syssvc/syslog.h"
#include "kernel_cfg.h"
#include "syssvc/logtask.h"

#include <stdint.h>

#include "envdep.h"
#include "protohdr.h"

extern const char* MSG;

#define LOG(...) syslog(LOG_NOTICE, __VA_ARGS__)

void mcled_change(int color);
void redled_off(void);
void redled_on(void);

#define COLOR_OFF			0x0
#define COLOR_BLUE			0x1
#define COLOR_GREEN			0x2
#define COLOR_LIGHTBLUE		0x3
#define COLOR_RED			0x4
#define COLOR_PINK			0x5
#define COLOR_YELLOW		0x6
#define COLOR_WHITE			0x7

#ifdef BIG_ENDIAN
#define hton16(val) val
#define ntoh16(val) val
#define hton32(val) val
#define ntoh32(val) val

#endif // BIG_ENDIAN
#ifdef LITTLE_ENDIAN
#define hton16(val) ((uint16_t) ( \
    ((val) << 8) | ((val) >> 8) ))

#define ntoh16(val) ((uint16_t) ( \
    ((val) << 8) | ((val) >> 8) ))

#define hton32(val) ((uint32_t) ( \
    (((val) & 0x000000ff) << 24) | \
    (((val) & 0x0000ff00) <<  8) | \
    (((val) & 0x00ff0000) >>  8) | \
    (((val) & 0xff000000) >> 24) ))

#define ntoh32(val) ((uint32_t) ( \
    (((val) & 0x000000ff) << 24) | \
    (((val) & 0x0000ff00) <<  8) | \
    (((val) & 0x00ff0000) >>  8) | \
    (((val) & 0xff000000) >> 24) ))
#endif // LITTLE_ENDIAN

#define IPADDR_TO_UINT32(val) (*((uint32_t*)(val)))


char *macaddr2str(uint8_t ma[]);
char *ipaddr2str(uint8_t ia[]);

/*
uint64_t macaddr2uint64(const uint8_t mac[]);
uint32_t ipaddr2uint32(const uint8_t ip[]);
*/

uint16_t checksum(uint16_t *data, int len);
uint16_t checksum2(uint16_t *data1, uint16_t *data2, int len1, int len2);
uint16_t checksum_hdrstack(hdrstack *hs);

void ipaddr_hostpart(uint8_t *dst, uint8_t *addr, uint8_t *mask);
void ipaddr_networkpart(uint8_t *dst, uint8_t *addr, uint8_t *mask);

uint32_t hdrstack_totallen(hdrstack *target);
void hdrstack_cpy(char *dst, hdrstack *src, uint32_t start, uint32_t len);

