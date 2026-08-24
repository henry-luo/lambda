#pragma once

#include <stdint.h>

// Parser-neutral source coordinates. Every byte range is half-open over the
// original UTF-8 source buffer.
typedef struct SourceSpan {
    uint32_t start_byte;
    uint32_t end_byte;
} SourceSpan;

typedef struct LambdaSourcePoint {
    uint32_t row;
    uint32_t column;
} LambdaSourcePoint;

static inline uint32_t lambda_source_span_length(SourceSpan span) {
    return span.end_byte >= span.start_byte ? span.end_byte - span.start_byte : 0;
}

static inline LambdaSourcePoint lambda_source_point_at(const char* source,
        uint32_t byte_offset) {
    LambdaSourcePoint point = {0, 0};
    if (!source) return point;
    for (uint32_t i = 0; i < byte_offset; i++) {
        if (source[i] == '\n') {
            point.row++;
            point.column = 0;
        } else {
            point.column++;
        }
    }
    return point;
}

static inline LambdaSourcePoint lambda_source_span_start_point(const char* source,
        SourceSpan span) {
    return lambda_source_point_at(source, span.start_byte);
}

static inline LambdaSourcePoint lambda_source_span_end_point(const char* source,
        SourceSpan span) {
    return lambda_source_point_at(source, span.end_byte);
}
