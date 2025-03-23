#ifndef STRING_BUFFER_H
#define STRING_BUFFER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "strview.h"

typedef struct {
    StrView; // extends StrView
    size_t capacity;
} StrBuf;

#ifndef ROUNDUP2POW
  #define ROUNDUP2POW(x) _rndup2pow64(x)
  static inline size_t _rndup2pow64(unsigned long long x) {
    // long long >=64 bits guaranteed in C99
    --x; x|=x>>1; x|=x>>2; x|=x>>4; x|=x>>8; x|=x>>16; x|=x>>32; ++x;
    return x;
  }
#endif

StrBuf* strbuf_new();
StrBuf* strbuf_new_cap(size_t size);
StrBuf* strbuf_create(const char *str);
StrBuf* strbuf_dup(const StrBuf *sb);
void strbuf_free(StrBuf *sb);
void strbuf_reset(StrBuf *sb);
bool strbuf_ensure_cap(StrBuf *sb, size_t min_capacity);
bool strbuf_resize(StrBuf *sb, size_t new_size);
void strbuf_append_str(StrBuf *sb, const char *str);
// append string of given length n
void strbuf_append_str_n(StrBuf *sb, const char *str, size_t n);
void strbuf_append_char(StrBuf *sb, char c);
// append character c n times
void strbuf_append_char_n(StrBuf *sb, char c, size_t n);
void strbuf_append_int(StrBuf *buf, int value);
void strbuf_append_long(StrBuf *buf, long value);
void strbuf_append_ulong(StrBuf *buf, unsigned long value);
void strbuf_append_all(StrBuf *sb, int num_args, ...);
void strbuf_vappend(StrBuf *sb, int num_args, va_list args);
void strbuf_append_format(StrBuf *sb, const char *format, ...);
void strbuf_vappend_format(StrBuf *sb, const char *format, va_list args);
void strbuf_trim_to_length(StrBuf *sb);
void strbuf_copy(StrBuf *dst, const StrBuf *src);
bool strbuf_append_file(StrBuf *sb, FILE *file);
bool strbuf_append_file_head(StrBuf *sb, FILE *file, size_t n);

#endif