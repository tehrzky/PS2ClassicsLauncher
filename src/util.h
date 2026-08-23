#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>

static inline void append_le_uint16(uint8_t *buf, uint16_t val) {
    buf[0] = (uint8_t)(val);
    buf[1] = (uint8_t)(val >> 8);
}

static inline void append_le_uint32(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)(val);
    buf[1] = (uint8_t)(val >> 8);
    buf[2] = (uint8_t)(val >> 16);
    buf[3] = (uint8_t)(val >> 24);
}

static inline void append_le_uint64(uint8_t *buf, uint64_t val) {
    append_le_uint32(buf,     (uint32_t)(val));
    append_le_uint32(buf + 4, (uint32_t)(val >> 32));
}

static inline uint16_t read_le_uint16(const uint8_t *buf) {
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

static inline uint32_t read_le_uint32(const uint8_t *buf) {
    return (uint32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
}

static inline uint64_t read_le_uint64(const uint8_t *buf) {
    return (uint64_t)read_le_uint32(buf) | ((uint64_t)read_le_uint32(buf + 4) << 32);
}

#endif /* !_UTIL_H_ */
