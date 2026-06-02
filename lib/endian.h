// lib/endian.h - portable byte-order read/write and byteswap helpers (header-only).
//
// Reads pull multi-byte integers out of a raw byte buffer with an explicit
// endianness, independent of the host byte order — the form binary parsers
// (font tables, PDF, woff2, network) need. Previously every font table parser
// hand-rolled its own `rd16`/`rd32` shift-and-OR helpers.

#ifndef LIB_ENDIAN_H
#define LIB_ENDIAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── big-endian reads (network / OpenType order) ──
static inline uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static inline int16_t read_be16s(const uint8_t* p) {
    return (int16_t)read_be16(p);
}
static inline uint32_t read_be24(const uint8_t* p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}
static inline uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}
static inline int32_t read_be32s(const uint8_t* p) {
    return (int32_t)read_be32(p);
}
static inline uint64_t read_be64(const uint8_t* p) {
    return ((uint64_t)read_be32(p) << 32) | (uint64_t)read_be32(p + 4);
}

// ── little-endian reads ──
static inline uint16_t read_le16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}
static inline int16_t read_le16s(const uint8_t* p) {
    return (int16_t)read_le16(p);
}
static inline uint32_t read_le32(const uint8_t* p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8)  | (uint32_t)p[0];
}
static inline int32_t read_le32s(const uint8_t* p) {
    return (int32_t)read_le32(p);
}
static inline uint64_t read_le64(const uint8_t* p) {
    return ((uint64_t)read_le32(p + 4) << 32) | (uint64_t)read_le32(p);
}

// ── big-endian writes ──
static inline void write_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static inline void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

// ── little-endian writes ──
static inline void write_le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static inline void write_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;        p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// ── portable byteswap (swap byte order of a value already in registers) ──
static inline uint16_t bswap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}
static inline uint32_t bswap32(uint32_t v) {
    return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8) |
           ((v & 0x0000FF00u) << 8)  | ((v & 0x000000FFu) << 24);
}
static inline uint64_t bswap64(uint64_t v) {
    return ((uint64_t)bswap32((uint32_t)v) << 32) | (uint64_t)bswap32((uint32_t)(v >> 32));
}

#ifdef __cplusplus
}
#endif

#endif // LIB_ENDIAN_H
