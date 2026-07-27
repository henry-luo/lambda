#pragma once

#include <stdint.h>
#include <zlib.h>

typedef struct NodeZlibBytes {
    uint8_t* data;
    int length;
    int status;
} NodeZlibBytes;

enum NodeZlibCodecMode {
    NODE_ZLIB_CODEC_GZIP,
    NODE_ZLIB_CODEC_GUNZIP,
    NODE_ZLIB_CODEC_DEFLATE,
    NODE_ZLIB_CODEC_INFLATE,
    NODE_ZLIB_CODEC_DEFLATE_RAW,
    NODE_ZLIB_CODEC_INFLATE_RAW,
    NODE_ZLIB_CODEC_UNZIP,
};

// This primitive is shared by the static checkpoint and the hosted module so
// both paths use zlib's exact seed and unsigned-result semantics.
static inline uint32_t node_zlib_crc32_bytes(const uint8_t* data, int length, uint32_t seed) {
    static const uint8_t empty_data = 0;
    const uint8_t* input = data ? data : &empty_data;
    return (uint32_t)crc32((uLong)seed, (const Bytef*)input, (uInt)length);
}

bool node_zlib_gzip_encode(const uint8_t* data, int length, NodeZlibBytes* out_bytes);
bool node_zlib_gunzip_decode(const uint8_t* data, int length, NodeZlibBytes* out_bytes);
bool node_zlib_deflate_encode(const uint8_t* data, int length, NodeZlibBytes* out_bytes);
bool node_zlib_inflate_decode(const uint8_t* data, int length, NodeZlibBytes* out_bytes);
bool node_zlib_deflate_raw_encode(const uint8_t* data, int length, NodeZlibBytes* out_bytes);
bool node_zlib_inflate_raw_decode(const uint8_t* data, int length, NodeZlibBytes* out_bytes);
bool node_zlib_unzip_decode(const uint8_t* data, int length, NodeZlibBytes* out_bytes);
void node_zlib_bytes_free(NodeZlibBytes* bytes);
