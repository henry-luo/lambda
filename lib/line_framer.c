// line_framer.c - see line_framer.h
#include "line_framer.h"

#include <string.h>

bool line_framer_init(LineFramer* framer, size_t initial_capacity,
                      MemCategory category) {
    if (!framer) return false;
    memset(framer, 0, sizeof(*framer));
    return byte_builder_init(&framer->bytes, initial_capacity, category, true);
}

bool line_framer_append(LineFramer* framer, const void* data, size_t length) {
    if (!framer) return false;
    if (framer->offset > 0) {
        size_t unread = framer->bytes.length - framer->offset;
        if (unread > 0) memmove(framer->bytes.data, framer->bytes.data + framer->offset, unread);
        framer->bytes.length = unread;
        framer->offset = 0;
        if (framer->bytes.nul_terminated && framer->bytes.data) {
            framer->bytes.data[unread] = '\0';
        }
    }
    return byte_builder_append(&framer->bytes, data, length);
}

const char* line_framer_peek(LineFramer* framer, size_t* out_length) {
    if (!framer || framer->offset >= framer->bytes.length) return NULL;
    size_t unread = framer->bytes.length - framer->offset;
    const char* start = (const char*)framer->bytes.data + framer->offset;
    const char* newline = (const char*)memchr(start, '\n', unread);
    if (!newline) return NULL;
    if (out_length) *out_length = (size_t)(newline - start);
    return start;
}

bool line_framer_consume(LineFramer* framer, size_t length) {
    if (!framer || length > framer->bytes.length - framer->offset) return false;
    framer->offset += length;
    if (framer->offset == framer->bytes.length) {
        framer->offset = 0;
        framer->bytes.length = 0;
        if (framer->bytes.nul_terminated && framer->bytes.data) framer->bytes.data[0] = '\0';
    }
    return true;
}

void line_framer_destroy(LineFramer* framer) {
    if (!framer) return;
    byte_builder_destroy(&framer->bytes);
    framer->offset = 0;
}
