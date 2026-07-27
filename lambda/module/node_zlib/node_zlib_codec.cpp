#include "node_zlib_codec.hpp"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool node_zlib_starts_gzip_member(const Bytef* data, uInt length) {
    return data && length >= 2 && data[0] == 0x1f && data[1] == 0x8b;
}

static bool node_zlib_validate_codec_input(const uint8_t* data, int length,
                                           NodeZlibBytes* out_bytes) {
    if (!out_bytes || length < 0 || (length > 0 && !data)) return false;
    out_bytes->data = NULL;
    out_bytes->length = 0;
    out_bytes->status = Z_OK;
    if ((uLong)length > UINT_MAX) {
        out_bytes->status = Z_BUF_ERROR;
        return false;
    }
    return true;
}

static bool node_zlib_deflate_codec(const uint8_t* data, int length, int window_bits,
                                    NodeZlibBytes* out_bytes) {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    int status = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, window_bits,
                              8, Z_DEFAULT_STRATEGY);
    if (status != Z_OK) {
        out_bytes->status = status;
        return false;
    }
    uLong bound = compressBound((uLong)length);
    if (bound > (uLong)INT_MAX - 32U) {
        deflateEnd(&stream);
        out_bytes->status = Z_BUF_ERROR;
        return false;
    }
    int capacity = (int)bound + 32;
    uint8_t* output = (uint8_t*)malloc((size_t)capacity);
    if (!output) {
        deflateEnd(&stream);
        out_bytes->status = Z_MEM_ERROR;
        return false;
    }
    stream.next_in = (Bytef*)data;
    stream.avail_in = (uInt)length;
    stream.next_out = output;
    stream.avail_out = (uInt)capacity;
    status = deflate(&stream, Z_FINISH);
    if (status != Z_STREAM_END || stream.total_out > (uLong)INT_MAX) {
        free(output);
        deflateEnd(&stream);
        out_bytes->status = status == Z_STREAM_END ? Z_BUF_ERROR : status;
        return false;
    }
    deflateEnd(&stream);
    out_bytes->data = output;
    out_bytes->length = (int)stream.total_out;
    return true;
}

static bool node_zlib_inflate_codec(const uint8_t* data, int length, int window_bits,
                                    bool concatenate_gzip, NodeZlibBytes* out_bytes) {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    int status = inflateInit2(&stream, window_bits);
    if (status != Z_OK) {
        out_bytes->status = status;
        return false;
    }
    size_t capacity = (size_t)length * 4U;
    if (capacity < 4096U) capacity = 4096U;
    uint8_t* output = (uint8_t*)malloc(capacity);
    if (!output) {
        inflateEnd(&stream);
        out_bytes->status = Z_MEM_ERROR;
        return false;
    }
    stream.next_in = (Bytef*)data;
    stream.avail_in = (uInt)length;
    size_t total = 0;
    while (true) {
        if (total >= capacity) {
            if (capacity > (size_t)INT_MAX / 2U) {
                status = Z_BUF_ERROR;
                break;
            }
            capacity *= 2U;
            uint8_t* resized = (uint8_t*)realloc(output, capacity);
            if (!resized) {
                status = Z_MEM_ERROR;
                break;
            }
            output = resized;
        }
        size_t available = capacity - total;
        stream.next_out = output + total;
        stream.avail_out = (uInt)available;
        status = inflate(&stream, Z_NO_FLUSH);
        total += available - stream.avail_out;
        if (status == Z_STREAM_END) {
            bool continue_member = stream.avail_in > 0 && (concatenate_gzip ||
                node_zlib_starts_gzip_member(stream.next_in, stream.avail_in));
            if (!continue_member) break;
            Bytef* next_input = stream.next_in;
            uInt available_input = stream.avail_in;
            status = inflateReset2(&stream, window_bits);
            if (status != Z_OK) break;
            stream.next_in = next_input;
            stream.avail_in = available_input;
            continue;
        }
        if (status != Z_OK) break;
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END || total > (size_t)INT_MAX) {
        free(output);
        out_bytes->status = status == Z_STREAM_END ? Z_BUF_ERROR : status;
        return false;
    }
    out_bytes->data = output;
    out_bytes->length = (int)total;
    return true;
}

static bool node_zlib_sync_codec(enum NodeZlibCodecMode mode, const uint8_t* data,
                                 int length, NodeZlibBytes* out_bytes) {
    if (!node_zlib_validate_codec_input(data, length, out_bytes)) return false;
    switch (mode) {
    case NODE_ZLIB_CODEC_GZIP:
        return node_zlib_deflate_codec(data, length, 15 + 16, out_bytes);
    case NODE_ZLIB_CODEC_DEFLATE:
        return node_zlib_deflate_codec(data, length, 15, out_bytes);
    case NODE_ZLIB_CODEC_DEFLATE_RAW:
        return node_zlib_deflate_codec(data, length, -15, out_bytes);
    case NODE_ZLIB_CODEC_GUNZIP:
        return node_zlib_inflate_codec(data, length, 15 + 16, true, out_bytes);
    case NODE_ZLIB_CODEC_INFLATE:
        return node_zlib_inflate_codec(data, length, 15, false, out_bytes);
    case NODE_ZLIB_CODEC_INFLATE_RAW:
        return node_zlib_inflate_codec(data, length, -15, false, out_bytes);
    case NODE_ZLIB_CODEC_UNZIP:
        return node_zlib_inflate_codec(data, length, 15 + 32, false, out_bytes);
    }
    out_bytes->status = Z_STREAM_ERROR;
    return false;
}

bool node_zlib_gzip_encode(const uint8_t* data, int length, NodeZlibBytes* out_bytes) {
    return node_zlib_sync_codec(NODE_ZLIB_CODEC_GZIP, data, length, out_bytes);
}

bool node_zlib_gunzip_decode(const uint8_t* data, int length, NodeZlibBytes* out_bytes) {
    return node_zlib_sync_codec(NODE_ZLIB_CODEC_GUNZIP, data, length, out_bytes);
}

bool node_zlib_deflate_encode(const uint8_t* data, int length, NodeZlibBytes* out_bytes) {
    return node_zlib_sync_codec(NODE_ZLIB_CODEC_DEFLATE, data, length, out_bytes);
}

bool node_zlib_inflate_decode(const uint8_t* data, int length, NodeZlibBytes* out_bytes) {
    return node_zlib_sync_codec(NODE_ZLIB_CODEC_INFLATE, data, length, out_bytes);
}

bool node_zlib_deflate_raw_encode(const uint8_t* data, int length, NodeZlibBytes* out_bytes) {
    return node_zlib_sync_codec(NODE_ZLIB_CODEC_DEFLATE_RAW, data, length, out_bytes);
}

bool node_zlib_inflate_raw_decode(const uint8_t* data, int length, NodeZlibBytes* out_bytes) {
    return node_zlib_sync_codec(NODE_ZLIB_CODEC_INFLATE_RAW, data, length, out_bytes);
}

bool node_zlib_unzip_decode(const uint8_t* data, int length, NodeZlibBytes* out_bytes) {
    return node_zlib_sync_codec(NODE_ZLIB_CODEC_UNZIP, data, length, out_bytes);
}

void node_zlib_bytes_free(NodeZlibBytes* bytes) {
    if (!bytes) return;
    free(bytes->data);
    bytes->data = NULL;
    bytes->length = 0;
}
