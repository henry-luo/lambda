#include "html_encoder.hpp"
#include <string.h>  // for strlen

namespace html {

void HtmlEncoder::escape(StrBuf* sb, const char* text, size_t len) {
    if (!sb || !text) return;
    if (len == 0) len = strlen(text);
    
    if (!needs_escaping(text, len)) {
        strbuf_append_str_n(sb, text, len);
        return;
    }
    
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        switch (c) {
            case '&':
                strbuf_append_str(sb, "&amp;");
                break;
            case '<':
                strbuf_append_str(sb, "&lt;");
                break;
            case '>':
                strbuf_append_str(sb, "&gt;");
                break;
            case '"':
                strbuf_append_str(sb, "&quot;");
                break;
            default:
                strbuf_append_char(sb, c);
        }
    }
}

void HtmlEncoder::escape_attribute(StrBuf* sb, const char* text, size_t len) {
    if (!sb || !text) return;
    if (len == 0) len = strlen(text);
    
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        switch (c) {
            case '&':
                strbuf_append_str(sb, "&amp;");
                break;
            case '<':
                strbuf_append_str(sb, "&lt;");
                break;
            case '>':
                strbuf_append_str(sb, "&gt;");
                break;
            case '"':
                strbuf_append_str(sb, "&quot;");
                break;
            case '\'':
                strbuf_append_str(sb, "&#39;");
                break;
            default:
                strbuf_append_char(sb, c);
        }
    }
}

bool HtmlEncoder::needs_escaping(const char* text, size_t len) {
    if (!text) return false;
    if (len == 0) len = strlen(text);
    
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '&' || c == '<' || c == '>' || c == '"') {
            return true;
        }
    }
    return false;
}

} // namespace html
