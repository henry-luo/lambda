// line_framer.h - newline-delimited frames over allocator-aware byte storage.
#ifndef LIB_LINE_FRAMER_H
#define LIB_LINE_FRAMER_H

#include <stdbool.h>
#include <stddef.h>

#include "byte_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LineFramer {
    ByteBuilder bytes;
    size_t offset;
} LineFramer;

bool line_framer_init(LineFramer* framer, size_t initial_capacity,
                      MemCategory category);
bool line_framer_append(LineFramer* framer, const void* data, size_t length);
const char* line_framer_peek(LineFramer* framer, size_t* out_length);
bool line_framer_consume(LineFramer* framer, size_t length);
void line_framer_destroy(LineFramer* framer);

#ifdef __cplusplus
}
#endif

#endif
