#ifndef STRING_BUF_H
#define STRING_BUF_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "string.h"
#include "mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-anon-tag"
typedef struct {
    String* str;          // pointer to String data (may or may-not be null-terminated)
    size_t length;        // length excluding null terminator
    size_t capacity;      // capacity in bytes
    Pool *pool; // memory pool for the string buffer
} StringBuf;
#pragma clang diagnostic pop

#ifndef ROUNDUP2POW
  #define ROUNDUP2POW(x) _rndup2pow64(x)
  static inline size_t _rndup2pow64(unsigned long long x) {
    // long long >=64 bits guaranteed in C99
    --x; x|=x>>1; x|=x>>2; x|=x>>4; x|=x>>8; x|=x>>16; x|=x>>32; ++x;
    return x;
  }
#endif

// Core StringBuf functions
StringBuf* stringbuf_new(Pool *pool);
StringBuf* stringbuf_new_cap(Pool *pool, size_t capacity);
void stringbuf_free(StringBuf *sb);
void stringbuf_reset(StringBuf *sb);
void stringbuf_full_reset(StringBuf *sb);
bool stringbuf_ensure_cap(StringBuf *sb, size_t min_capacity);

// Append functions
void stringbuf_append_str(StringBuf *sb, const char *str);
void stringbuf_append_str_n(StringBuf *sb, const char *str, size_t n);
void stringbuf_append_char(StringBuf *sb, char c);
void stringbuf_append_char_n(StringBuf *sb, char c, size_t n);
void stringbuf_append_int(StringBuf *sb, int value);
void stringbuf_append_long(StringBuf *sb, long value);
void stringbuf_append_ulong(StringBuf *sb, unsigned long value);
void stringbuf_append_all(StringBuf *sb, int num_args, ...);
void stringbuf_vappend(StringBuf *sb, int num_args, va_list args);
void stringbuf_append_format(StringBuf *sb, const char *format, ...);
void stringbuf_vappend_format(StringBuf *sb, const char *format, va_list args);

// Template emit functions (document formatting oriented)
// Format specifiers: %s (C string), %S (String*), %d (int), %l (int64_t),
// %f (double), %c (char), %n (newline), %i (indent N*2 spaces),
// %r (repeat char N times), %% (literal %)
void stringbuf_emit(StringBuf *sb, const char *fmt, ...);
void stringbuf_vemit(StringBuf *sb, const char *fmt, va_list args);

// Utility functions
void stringbuf_copy(StringBuf *dst, const StringBuf *src);
StringBuf* stringbuf_dup(const StringBuf *sb);
bool stringbuf_append_file(StringBuf *sb, FILE *file);
bool stringbuf_append_file_head(StringBuf *sb, FILE *file, size_t n);

// String conversion
String* stringbuf_to_string(StringBuf *sb);

#ifdef __cplusplus
}
#endif

#endif
