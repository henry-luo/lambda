#include "jube_node_zlib_codec.hpp"

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

#define NODE_ZLIB_SYNC_CODEC_WRAPPER(name, mode) \
bool name(const uint8_t* data, int length, NodeZlibBytes* out_bytes) { \
    return node_zlib_sync_codec(mode, data, length, out_bytes); \
}
NODE_ZLIB_SYNC_CODEC_WRAPPER(node_zlib_gzip_encode, NODE_ZLIB_CODEC_GZIP)
NODE_ZLIB_SYNC_CODEC_WRAPPER(node_zlib_gunzip_decode, NODE_ZLIB_CODEC_GUNZIP)
NODE_ZLIB_SYNC_CODEC_WRAPPER(node_zlib_deflate_encode, NODE_ZLIB_CODEC_DEFLATE)
NODE_ZLIB_SYNC_CODEC_WRAPPER(node_zlib_inflate_decode, NODE_ZLIB_CODEC_INFLATE)
NODE_ZLIB_SYNC_CODEC_WRAPPER(node_zlib_deflate_raw_encode, NODE_ZLIB_CODEC_DEFLATE_RAW)
NODE_ZLIB_SYNC_CODEC_WRAPPER(node_zlib_inflate_raw_decode, NODE_ZLIB_CODEC_INFLATE_RAW)
NODE_ZLIB_SYNC_CODEC_WRAPPER(node_zlib_unzip_decode, NODE_ZLIB_CODEC_UNZIP)
#undef NODE_ZLIB_SYNC_CODEC_WRAPPER

void node_zlib_bytes_free(NodeZlibBytes* bytes) {
    if (!bytes) return;
    free(bytes->data);
    bytes->data = NULL;
    bytes->length = 0;
}

struct NodeZlibStreamState {
    z_stream stream;
    int mode;
    int window_bits;
    bool initialized;
    bool finished;
    bool is_deflate;
};

static bool node_zlib_stream_is_deflate(int mode) {
    return mode == NODE_ZLIB_CODEC_GZIP || mode == NODE_ZLIB_CODEC_DEFLATE ||
        mode == NODE_ZLIB_CODEC_DEFLATE_RAW;
}

static bool node_zlib_stream_starts_gzip_member(const uint8_t* data, int length) {
    return data && length > 0 && data[0] == 0x1f &&
        (length == 1 || data[1] == 0x8b);
}

static bool node_zlib_stream_should_reset_member(const NodeZlibStreamState* state,
                                                 const uint8_t* data, int length) {
    if (!state || state->is_deflate) return false;
    if (state->mode == NODE_ZLIB_CODEC_GUNZIP) return true;
    return state->mode == NODE_ZLIB_CODEC_UNZIP &&
        node_zlib_stream_starts_gzip_member(data, length);
}

static int node_zlib_stream_reset_inflate_member(NodeZlibStreamState* state) {
    Bytef* next_input = state->stream.next_in;
    uInt available_input = state->stream.avail_in;
    int status = inflateReset2(&state->stream, state->window_bits);
    state->stream.next_in = next_input;
    state->stream.avail_in = available_input;
    if (status == Z_OK) state->finished = false;
    return status;
}

bool node_zlib_stream_init(enum NodeZlibCodecMode mode, int window_bits, int level,
                           int mem_level, int strategy, void** out_state,
                           int* out_status) {
    if (out_state) *out_state = NULL;
    if (out_status) *out_status = Z_STREAM_ERROR;
    if (!out_state) return false;

    NodeZlibStreamState* state = (NodeZlibStreamState*)calloc(1, sizeof(NodeZlibStreamState));
    if (!state) {
        if (out_status) *out_status = Z_MEM_ERROR;
        return false;
    }
    state->mode = mode;
    state->window_bits = window_bits;
    state->is_deflate = node_zlib_stream_is_deflate(mode);
    int status = state->is_deflate
        ? deflateInit2(&state->stream, level, Z_DEFLATED, window_bits, mem_level, strategy)
        : inflateInit2(&state->stream, window_bits);
    if (status != Z_OK) {
        free(state);
        if (out_status) *out_status = status;
        return false;
    }
    state->initialized = true;
    *out_state = state;
    if (out_status) *out_status = Z_OK;
    return true;
}

bool node_zlib_stream_run(void* state_ptr, const uint8_t* data, int length, int flush,
                          NodeZlibBytes* out_bytes) {
    if (!out_bytes) return false;
    out_bytes->data = NULL;
    out_bytes->length = 0;
    out_bytes->status = Z_OK;
    NodeZlibStreamState* state = (NodeZlibStreamState*)state_ptr;
    if (!state || !state->initialized || length < 0 || (length > 0 && !data)) {
        out_bytes->status = Z_STREAM_ERROR;
        return false;
    }
    if ((uLong)length > UINT_MAX) {
        out_bytes->status = Z_BUF_ERROR;
        return false;
    }
    if (state->finished) {
        if (length > 0 && node_zlib_stream_should_reset_member(state, data, length)) {
            int status = node_zlib_stream_reset_inflate_member(state);
            if (status != Z_OK) {
                out_bytes->status = status;
                return false;
            }
        } else if (!state->is_deflate && state->mode == NODE_ZLIB_CODEC_UNZIP && length > 0) {
            return true;
        } else if (flush == Z_FINISH || length == 0) {
            return true;
        } else {
            out_bytes->status = Z_STREAM_END;
            return false;
        }
    }

    size_t capacity = (size_t)length * 2U + 16384U;
    if (capacity < 16384U) capacity = 16384U;
    uint8_t* output = (uint8_t*)malloc(capacity);
    if (!output) {
        out_bytes->status = Z_MEM_ERROR;
        return false;
    }

    state->stream.next_in = (Bytef*)data;
    state->stream.avail_in = (uInt)length;
    size_t total = 0;
    int status = Z_OK;
    bool done = false;
    while (!done) {
        if (total >= capacity) {
            size_t next_capacity = capacity * 2U;
            uint8_t* resized = (uint8_t*)realloc(output, next_capacity);
            if (!resized) {
                // retain the original allocation until realloc succeeds so an OOM does not leak it.
                free(output);
                out_bytes->status = Z_MEM_ERROR;
                return false;
            }
            output = resized;
            capacity = next_capacity;
        }

        size_t available = capacity - total;
        state->stream.next_out = output + total;
        state->stream.avail_out = (uInt)available;
        status = state->is_deflate ? deflate(&state->stream, flush) :
            inflate(&state->stream, flush);
        total += available - state->stream.avail_out;

        if (status == Z_STREAM_END) {
            state->finished = true;
            if (!state->is_deflate && state->stream.avail_in > 0 &&
                    node_zlib_stream_should_reset_member(state,
                        (const uint8_t*)state->stream.next_in,
                        (int)state->stream.avail_in)) {
                status = node_zlib_stream_reset_inflate_member(state);
                if (status != Z_OK) {
                    free(output);
                    out_bytes->status = status;
                    return false;
                }
            }
            done = true;
        } else if (state->is_deflate) {
            if (status != Z_OK) {
                free(output);
                out_bytes->status = status;
                return false;
            }
            if (flush == Z_NO_FLUSH) {
                done = state->stream.avail_in == 0 && state->stream.avail_out != 0;
            } else if (flush != Z_FINISH) {
                done = state->stream.avail_out != 0;
            }
        } else {
            if (status == Z_BUF_ERROR && flush != Z_FINISH) {
                status = Z_OK;
                done = true;
            } else if (status != Z_OK) {
                free(output);
                out_bytes->status = status;
                return false;
            } else if (flush == Z_NO_FLUSH) {
                done = state->stream.avail_in == 0 && state->stream.avail_out != 0;
            } else if (flush != Z_FINISH) {
                done = state->stream.avail_out != 0;
            } else if (state->stream.avail_in == 0 && state->stream.avail_out != 0) {
                free(output);
                out_bytes->status = Z_BUF_ERROR;
                return false;
            }
        }
    }

    if (total > (size_t)INT_MAX) {
        free(output);
        out_bytes->status = Z_BUF_ERROR;
        return false;
    }
    if (total > 0) {
        out_bytes->data = output;
        out_bytes->length = (int)total;
    } else {
        free(output);
    }
    out_bytes->status = status;
    return true;
}

void node_zlib_stream_free(void* state_ptr) {
    NodeZlibStreamState* state = (NodeZlibStreamState*)state_ptr;
    if (!state) return;
    if (state->initialized) {
        if (state->is_deflate) deflateEnd(&state->stream);
        else inflateEnd(&state->stream);
        state->initialized = false;
    }
    free(state);
}
