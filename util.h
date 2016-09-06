#ifdef BYTE_ORDER == BIG_ENDIAN
#define hton16(val) val
#define ntoh16(val) val
#define hton32(val) val
#define ntoh32(val) val

#else
#define hton16(val) ((uint16_t) ( \
    ((val) << 8) | ((val) >> 8) ))

#define ntoh16(val) ((uint16_t) ( \
    ((val) << 8) | ((val) >> 8) ))

#define hton32(val) ((uin32_t) ( \
    (((val) & 0x000000ff) << 24) | \
    (((val) & 0x0000ff00) <<  8) | \
    (((val) & 0x00ff0000) >>  8) | \
    (((val) & 0xff000000) >> 24) ))

#define ntoh32(val) ((uint32_t) ( \
    (((val) & 0x000000ff) << 24) | \
    (((val) & 0x0000ff00) <<  8) | \
    (((val) & 0x00ff0000) >>  8) | \
    (((val) & 0xff000000) >> 24) ))
#endif // BIG_ENDIAN
