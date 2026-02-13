#include "strbuf.h"
#include "str.h"
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "log.h"

#define INITIAL_CAPACITY 32

StrBuf* _strbuf_new_cap(size_t size) {
    StrBuf *sb = (StrBuf*) malloc(sizeof(StrBuf));
    if (!sb) return NULL;
    sb->str = (char*) malloc(size);
    if (!sb->str) { free(sb);  return NULL; }
    sb->str[0] = '\0';  sb->length = 0;
    sb->capacity = size;
    return sb;
}

StrBuf* strbuf_new_cap(size_t size) {
    return _strbuf_new_cap(size);
}

StrBuf* strbuf_new() {
    return strbuf_new_cap(INITIAL_CAPACITY);
}


StrBuf* strbuf_create(const char *str) {
    size_t str_len = strlen(str);
    StrBuf *sbuf = strbuf_new_cap(str_len + 1);
    if (!sbuf) return NULL;
    str_copy(sbuf->str, str_len + 1, str, str_len);
    sbuf->length = str_len;
    return sbuf;
}

void strbuf_free(StrBuf *sb) {
    if (sb->str) free(sb->str);
    free(sb);
}

void strbuf_reset(StrBuf *sb) {
    if (sb->str) {
        sb->str[0] = '\0';
        sb->length = 0;
    }
}

void strbuf_full_reset(StrBuf *sb) {
    sb->str = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

bool strbuf_ensure_cap(StrBuf *sb, size_t min_capacity) {
    if (min_capacity <= sb->capacity) return true;

    // Check for unreasonable allocation sizes to prevent overflow
    // Use >= instead of > to catch SIZE_MAX/2 exactly
    if (min_capacity >= SIZE_MAX / 2) {
        return false; // Refuse to allocate more than half of address space
    }

    size_t new_capacity = sb->capacity ? sb->capacity : INITIAL_CAPACITY;

    while (new_capacity < min_capacity) {
        // Check for overflow before doubling
        if (new_capacity > SIZE_MAX / 2) {
            printf("DEBUG: Overflow detected in doubling loop - new_capacity=%zu, min_capacity=%zu\n",
                   new_capacity, min_capacity);
            new_capacity = min_capacity; // Use minimum required instead of doubling
            break;
        }
        // log_debug("doubling strbuf new_capacity: %zu -> %zu", new_capacity, new_capacity * 2);
        new_capacity *= 2;
    }
    char *new_s = (char*)realloc(sb->str, new_capacity);
    if (!new_s) return false;
    sb->str = new_s;  sb->capacity = new_capacity;
    return true;
}

void strbuf_append_str(StrBuf *sb, const char *str) {
    if (!str) return;
    size_t str_len = strlen(str);
    if (!strbuf_ensure_cap(sb, sb->length + str_len + 1)) return;
    str_copy(sb->str + sb->length, str_len + 1, str, str_len);
    sb->length += str_len;
}

// Copy N characters from a character array to the end of this StrBuf
// strlen(__str) must be >= __n
void strbuf_append_str_n(StrBuf *sb, const char *str, size_t len) {
    if (!str) return;
    if (!strbuf_ensure_cap(sb, sb->length + len + 1)) return;
    str_copy(sb->str + sb->length, len + 1, str, len);
    sb->length += len;
}
void strbuf_append_char(StrBuf *sb, char c) {
    if (!strbuf_ensure_cap(sb, sb->length + 2)) return;
    sb->str[sb->length] = c;
    sb->str[sb->length + 1] = '\0';
    sb->length++;
}

// append char `c` `n` times
void strbuf_append_char_n(StrBuf *buf, char c, size_t n) {
    if (!strbuf_ensure_cap(buf, buf->length + n + 1)) return;
    str_fill(buf->str + buf->length, n, c);
    buf->length += n;
    buf->str[buf->length] = '\0';
}

void strbuf_append_all(StrBuf *sb, int num_args, ...) {
    va_list args;
    va_start(args, num_args);
    strbuf_vappend(sb, num_args, args);
    va_end(args);
}

void strbuf_vappend(StrBuf *sb, int num_args, va_list args) {
    for (int i = 0; i < num_args; i++) {
        const char *str = va_arg(args, const char*);
        if (str) {
            strbuf_append_str(sb, str);
        }
    }
}

void strbuf_append_format(StrBuf *sb, const char *format, ...) {
    va_list args;
    va_start(args, format);
    strbuf_vappend_format(sb, format, args);
    va_end(args);
}

void strbuf_vappend_format(StrBuf *sb, const char *format, va_list args) {
    if (!format) return;
    va_list args_copy;
    va_copy(args_copy, args);

    int size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (size < 0) return;
    if (!strbuf_ensure_cap(sb, sb->length + size + 1)) return;

    size = vsnprintf(sb->str + sb->length, sb->capacity - sb->length, format, args);
    if (size < 0) return;
    sb->length += size;
    sb->str[sb->length] = '\0';  // Ensure null termination
}

void strbuf_copy(StrBuf *dst, const StrBuf *src) {
    if (!dst || !src) return;
    strbuf_reset(dst);
    if (!strbuf_ensure_cap(dst, src->length + 1)) return;
    str_copy(dst->str, src->length + 1, src->str, src->length);
    dst->length = src->length;
}

StrBuf* strbuf_dup(const StrBuf *sb) {
    StrBuf *new_sb = strbuf_new_cap(sb->length + 1);
    if (!new_sb) return NULL;
    strbuf_copy(new_sb, sb);
    return new_sb;
}

/*
 * Integer to string functions adapted from:
 *   https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920
 */

 #define P01 10
 #define P02 100
 #define P03 1000
 #define P04 10000
 #define P05 100000
 #define P06 1000000
 #define P07 10000000
 #define P08 100000000
 #define P09 1000000000
 #define P10 10000000000
 #define P11 100000000000
 #define P12 1000000000000

/**
 * Return number of digits required to represent `num` in base 10.
 * Uses binary search to find number.
 * Examples:
 *   num_of_digits(0)   = 1
 *   num_of_digits(1)   = 1
 *   num_of_digits(10)  = 2
 *   num_of_digits(123) = 3
 */
static inline size_t num_of_digits(uint64_t v) {
  if(v < P01) return 1;
  if(v < P02) return 2;
  if(v < P03) return 3;
  if(v < P12) {
    if(v < P08) {
      if(v < P06) {
        if(v < P04) return 4;
        return 5 + (v >= P05);
      }
      return 7 + (v >= P07);
    }
    if(v < P10) {
      return 9 + (v >= P09);
    }
    return 11 + (v >= P11);
  }
  return 12 + num_of_digits(v / P12);
}

void strbuf_append_uint64(StrBuf *buf, uint64_t value) {
    // Append two digits at a time
    static const char digits[201] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";

    size_t num_digits = num_of_digits(value);
    size_t pos = num_digits - 1;

    if (!strbuf_ensure_cap(buf, buf->length + num_digits + 1)) {
        // failed to allocate enough memory, early return
        log_error("strbuf_append_uint64: Memory allocation failed: %d, %" PRIu64, buf->length + num_digits + 1, value);
        return;
    }
    char *dst = buf->str + buf->length;

    while(value >= 100) {
        uint64_t v = value % 100;
        value /= 100;
        dst[pos] = digits[v * 2 + 1];
        dst[pos - 1] = digits[v * 2];
        pos -= 2;
    }

    // Handle last 1-2 digits
    if (value < 10) {
        dst[pos] = '0' + value;
    } else {
        dst[pos] = digits[value * 2 + 1];
        dst[pos - 1] = digits[value * 2];
    }
    buf->length += num_digits;
    buf->str[buf->length] = '\0';
}

void strbuf_append_int(StrBuf *buf, int value) {
    strbuf_append_int64(buf, value);
}

void strbuf_append_int64(StrBuf *buf, int64_t value) {
    if (value < 0) {
        strbuf_append_char(buf, '-');
        // Handle INT64_MIN correctly to avoid overflow
        if (value == INT64_MIN) {
            strbuf_append_uint64(buf, (uint64_t)INT64_MAX + 1);
        } else {
            strbuf_append_uint64(buf, (uint64_t)(-value));
        }
    } else {
        strbuf_append_uint64(buf, (uint64_t)value);
    }
}

// Deprecated - use strbuf_append_int64 instead
void strbuf_append_long(StrBuf *buf, long value) {
    strbuf_append_int64(buf, (int64_t)value);
}

// Deprecated - use strbuf_append_uint64 instead
void strbuf_append_ulong(StrBuf *buf, unsigned long value) {
    strbuf_append_uint64(buf, (uint64_t)value);
}

bool strbuf_append_file(StrBuf *sb, FILE *file) {
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) return false;
    if (!strbuf_ensure_cap(sb, sb->length + size + 1)) return false;

    size_t read = fread(sb->str + sb->length, 1, size, file);
    sb->length += read;
    sb->str[sb->length] = '\0';
    return (read >= 0);
}

bool strbuf_append_file_head(StrBuf *sb, FILE *file, size_t n) {
    if (!file) return false;
    if (!strbuf_ensure_cap(sb, sb->length + n + 1)) return false;

    size_t read = fread(sb->str + sb->length, 1, n, file);
    sb->length += read;
    sb->str[sb->length] = '\0';
    return (read >= 0);
}
