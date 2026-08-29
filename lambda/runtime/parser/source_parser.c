#include "source_parser.h"

void parser_source_init(ParserSource* source, const char* text, size_t length,
        uint32_t first_line) {
    if (!source) return;
    source->source = text ? text : "";
    source->length = text ? length : 0;
    source->offset = 0;
    source->line = first_line;
    source->column = 0;
}

bool parser_source_has(const ParserSource* source, size_t count) {
    return source && source->offset <= source->length &&
        count <= source->length - source->offset;
}

unsigned char parser_source_peek(const ParserSource* source, size_t ahead) {
    return parser_source_has(source, ahead + 1)
        ? (unsigned char)source->source[source->offset + ahead] : 0;
}

void parser_source_advance_byte(ParserSource* source) {
    if (!source || source->offset >= source->length) return;
    source->offset++;
    source->column++;
}

bool parser_source_is_newline(const ParserSource* source) {
    unsigned char ch = parser_source_peek(source, 0);
    return ch == '\r' || ch == '\n';
}

void parser_source_advance_newline(ParserSource* source) {
    if (!source) return;
    if (parser_source_peek(source, 0) == '\r') {
        source->offset++;
        if (parser_source_peek(source, 0) == '\n') source->offset++;
    } else if (parser_source_peek(source, 0) == '\n') {
        source->offset++;
    } else {
        return;
    }
    source->line++;
    source->column = 0;
}
