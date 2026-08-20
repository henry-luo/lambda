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

// Keep the host provider's seed and unsigned-result semantics identical to
// zlib while the Node-facing namespace remains in the dynamic module.
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

bool node_zlib_stream_init(enum NodeZlibCodecMode mode, int window_bits, int level,
                           int mem_level, int strategy, void** out_state,
                           int* out_status);
bool node_zlib_stream_run(void* state, const uint8_t* data, int length, int flush,
                          NodeZlibBytes* out_bytes);
void node_zlib_stream_free(void* state);
