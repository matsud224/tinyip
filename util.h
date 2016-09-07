#pragma once

#define LITTLE_ENDIAN

#include <kernel.h>
#include <t_syslog.h>
#include <t_stdlib.h>
#include "syssvc/serial.h"
#include "syssvc/syslog.h"
#include "kernel_cfg.h"
#include <stdint.h>

#include "syssvc/logtask.h"

#define LOG(...) syslog(LOG_NOTICE, __VA_ARGS__)

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

char *macaddr2str(uint8_t ma[]);
char *ipaddr2str(uint8_t ia[]);

uint64_t macaddr2uint64(const uint8_t mac[]);
uint32_t ipaddr2uint32(const uint8_t ip[]);
