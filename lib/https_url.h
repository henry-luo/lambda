#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "str.h"

enum { HTTPS_URL_TEXT_LIMIT = 4096 };
static inline bool https_url_unreserved(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '.' || c == '_' || c == '-' || c == '~';
}
static inline int https_url_hex(unsigned char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
// Opaque metadata labels stay in one record, never in an OS pathname. Preserve
// UTF-8 identity without filesystem normalization or alias folding.
static inline bool https_url_name_bytes_valid(const char* text, size_t size) {
    if (!size || !str_utf8_valid(text, size)) return false;
    for (size_t i = 0; i < size; i++) {
        unsigned char c = text[i];
        if (c < 32 || c == 127 || (c == 0xC2 && i + 1 < size && (unsigned char)text[i + 1] <= 0x9F)) return false;
    }
    return true;
}
static inline bool https_url_opaque_name_valid(const char* text, size_t capacity) {
    if (!text) return false;
    size_t size = strnlen(text, capacity);
    return size < capacity && https_url_name_bytes_valid(text, size);
}
static inline bool https_url_dot_segments_valid(const char* path) {
    for (const char* segment = path; segment && *segment;) {
        const char* end = strchr(segment, '/'); size_t size = end ? (size_t)(end - segment) : strlen(segment);
        if ((size == 1 && segment[0] == '.') || (size == 2 && !memcmp(segment, "..", 2))) return false;
        segment = end ? end + 1 : NULL;
    }
    return true;
}
// Uppercase escapes and literal unreserved bytes give one wire spelling.
// Decode once for UTF-8/control validation; encoded separators stay data.
static inline bool https_url_path_bytes_valid(const char* path, bool escaped, bool iri) {
    char decoded[HTTPS_URL_TEXT_LIMIT]; size_t size = 0;
    for (size_t i = 0; path[i]; i++) {
        unsigned char c = path[i];
        if (c == '%') {
            if (!escaped || !path[i + 1] || !path[i + 2]) return false;
            int high = https_url_hex(path[i + 1]), low = https_url_hex(path[i + 2]);
            if (high < 0 || low < 0) return false;
            c = (unsigned char)(high * 16 + low); i += 2;
            if (https_url_unreserved(c)) return false;
        } else if (c <= 32 || c >= 127 || strchr("\\@?#", c)) {
            if (!iri || (c != 32 && c < 128)) return false;
        }
        if (size == sizeof(decoded)) return false;
        decoded[size++] = (char)c;
    }
    return (!size || https_url_name_bytes_valid(decoded, size)) && https_url_dot_segments_valid(path);
}
// Validate before URL parsing can normalize it. Encoded paths are an explicit
// updater transport profile; admitted origins and base directories stay plain.
static inline bool https_url_reference_valid_ex(const char* text, size_t capacity, bool escaped) {
    if (!text) return false;
    size_t length = strnlen(text, capacity);
    if (!length || length == capacity || length >= HTTPS_URL_TEXT_LIMIT) return false;
    const char* authority = strstr(text, "://");
    const char* path = authority ? strchr(authority + 3, '/') :
        !strncmp(text, "//", 2) ? strchr(text + 2, '/') : text;
    if (!path) path = text + length;
    for (const char* p = text; p < path; p++) {
        unsigned char c = *p;
        if (c <= 32 || c >= 127 || strchr("\\%@?#", c)) return false;
    }
    return https_url_path_bytes_valid(path, escaped, false);
}
static inline bool https_url_reference_valid(const char* text, size_t capacity) {
    return https_url_reference_valid_ex(text, capacity, false);
}
// Canonical encoded repository-relative names, never OS pathnames or URLs.
static inline bool https_url_public_path_valid(const char* text, size_t capacity) {
    if (!https_url_reference_valid_ex(text, capacity, true) || *text == '/' || text[strlen(text) - 1] == '/' || strstr(text, "//")) return false;
    for (const char* p = text; *p; p++) if (!https_url_unreserved((unsigned char)*p) && *p != '/' && *p != '%') return false;
    return true;
}
// Admitted target references may contain UTF-8/space. Existing URI escapes are
// preserved exactly once; raw URL controls and traversal remain excluded.
static inline bool https_url_target_name_valid(const char* text, size_t capacity) {
    if (!text) return false;
    size_t size = strnlen(text, capacity);
    return size && size < capacity && size < HTTPS_URL_TEXT_LIMIT && text[0] != '/' && text[size - 1] != '/' &&
        !strstr(text, "//") && https_url_path_bytes_valid(text, true, true);
}
