#include "stringbuf.h"
#include "str.h"
#include <string.h>
#include "log.h"

#define INITIAL_CAPACITY 32

StringBuf* stringbuf_new_cap(Pool *pool, size_t capacity) {
    if (!pool) return NULL;

    StringBuf *sb = (StringBuf*)pool_alloc(pool, sizeof(StringBuf));
    if (!sb) return NULL;

    sb->pool = pool;
    sb->str = NULL;
    sb->length = 0;
    sb->capacity = 0;

    // Pre-allocate capacity if requested
    if (capacity > 0) {
        if (!stringbuf_ensure_cap(sb, capacity)) {
            pool_free(pool, sb);
            return NULL;
        }
    }

    return sb;
}

StringBuf* stringbuf_new(Pool *pool) {
    return stringbuf_new_cap(pool, INITIAL_CAPACITY);
}

void stringbuf_free(StringBuf *sb) {
    if (!sb) return;

    if (sb->str) {
        pool_free(sb->pool, sb->str);
    }
    pool_free(sb->pool, sb);
}

void stringbuf_reset(StringBuf *sb) {
    if (!sb) return;

    if (sb->str && sb->capacity > 0) {
        // Reset the String structure
        sb->str->len = 0;
        sb->str->ref_cnt = 0;
        // Null-terminate the chars array
        if (sb->capacity > sizeof(String)) {
            sb->str->chars[0] = '\0';
        }
    }
    sb->length = 0;
}

void stringbuf_full_reset(StringBuf *sb) {
    if (!sb) return;

    if (sb->str) {
        pool_free(sb->pool, sb->str);
    }
    sb->str = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

bool stringbuf_ensure_cap(StringBuf *sb, size_t min_capacity) {
    if (!sb) return false;

    // Calculate required capacity including String header
    size_t required_capacity = sizeof(String) + min_capacity;

    if (required_capacity <= sb->capacity) {
        return true;
    }

    // Check for unreasonable allocation sizes to prevent overflow
    if (required_capacity >= SIZE_MAX / 2) {
        return false;
    }

    size_t new_capacity = sb->capacity ? sb->capacity : sizeof(String) + INITIAL_CAPACITY;

    while (new_capacity < required_capacity) {
        // Check for overflow before doubling
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = required_capacity;
            break;
        }
        size_t old_capacity = new_capacity;
        new_capacity *= 2;
        // log_debug("doubling stringbuf new_capacity: %zu -> %zu", old_capacity, new_capacity);
    }

    String *new_str;
    if (!sb->str) {
        // First allocation
        new_str = (String*)pool_alloc(sb->pool, new_capacity);
        if (!new_str) {
            return false;
        }

        // Initialize String structure
        new_str->len = 0;
        new_str->ref_cnt = 0;
        new_str->chars[0] = '\0';
    } else {
        // Realloc existing buffer - copy String header + actual string data
        size_t string_header_size = sizeof(String);
        size_t data_to_copy = string_header_size + sb->length + 1; // +1 for null terminator

        new_str = (String*)pool_realloc(sb->pool, sb->str, new_capacity);
        if (!new_str) {
            return false;
        }
    }

    sb->str = new_str;
    sb->capacity = new_capacity;
    return true;
}

void stringbuf_append_str(StringBuf *sb, const char *str) {
    if (!sb || !str) return;

    size_t str_len = strlen(str);
    size_t new_length = sb->length + str_len;

    // Check for 22-bit len field overflow (max 4,194,303 chars)
    if (new_length > 0x3FFFFF) {
        return; // Silently fail to prevent corruption
    }

    if (!stringbuf_ensure_cap(sb, new_length + 1)) return;

    // Perform the copy
    str_copy(sb->str->chars + sb->length, str_len + 1, str, str_len);
    sb->length = new_length;
    sb->str->len = sb->length;
}

void stringbuf_append_str_n(StringBuf *sb, const char *str, size_t len) {
    if (!sb || !str) return;

    size_t new_length = sb->length + len;

    // Check for 22-bit len field overflow (max 4,194,303 chars)
    if (new_length > 0x3FFFFF) {
        return; // Silently fail to prevent corruption
    }

    if (!stringbuf_ensure_cap(sb, new_length + 1)) return;

    str_copy(sb->str->chars + sb->length, len + 1, str, len);
    sb->length = new_length;
    sb->str->len = sb->length;
}

void stringbuf_append_char(StringBuf *sb, char c) {
    if (!sb) return;

    size_t new_length = sb->length + 1;

    // Check for 22-bit len field overflow (max 4,194,303 chars)
    if (new_length > 0x3FFFFF) {
        return; // Silently fail to prevent corruption
    }

    if (!stringbuf_ensure_cap(sb, new_length + 1)) return;

    sb->str->chars[sb->length] = c;
    sb->length = new_length;
    sb->str->chars[sb->length] = '\0';
    sb->str->len = sb->length;
}

void stringbuf_append_char_n(StringBuf *sb, char c, size_t n) {
    if (!sb) return;

    size_t new_length = sb->length + n;

    // Check for 22-bit len field overflow (max 4,194,303 chars)
    if (new_length > 0x3FFFFF) {
        return; // Silently fail to prevent corruption
    }

    if (!stringbuf_ensure_cap(sb, new_length + 1)) return;

    str_fill(sb->str->chars + sb->length, n, c);
    sb->length = new_length;
    sb->str->chars[sb->length] = '\0';
    sb->str->len = sb->length;
}

void stringbuf_append_all(StringBuf *sb, int num_args, ...) {
    if (!sb) return;

    va_list args;
    va_start(args, num_args);
    stringbuf_vappend(sb, num_args, args);
    va_end(args);
}

void stringbuf_vappend(StringBuf *sb, int num_args, va_list args) {
    if (!sb) return;

    for (int i = 0; i < num_args; i++) {
        const char *str = va_arg(args, const char*);
        if (str) {
            stringbuf_append_str(sb, str);
        }
    }
}

void stringbuf_append_format(StringBuf *sb, const char *format, ...) {
    if (!sb || !format) return;

    va_list args;
    va_start(args, format);
    stringbuf_vappend_format(sb, format, args);
    va_end(args);
}

void stringbuf_vappend_format(StringBuf *sb, const char *format, va_list args) {
    if (!sb || !format) return;

    va_list args_copy;
    va_copy(args_copy, args);

    int size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (size < 0) return;

    size_t new_length = sb->length + size;

    // Check for 22-bit len field overflow (max 4,194,303 chars)
    if (new_length > 0x3FFFFF) {
        return; // Silently fail to prevent corruption
    }

    if (!stringbuf_ensure_cap(sb, new_length + 1)) return;

    size = vsnprintf(sb->str->chars + sb->length, (sb->capacity - sizeof(String)) - sb->length, format, args);
    if (size < 0) return;

    sb->length = new_length;
    sb->str->chars[sb->length] = '\0';
    sb->str->len = sb->length;
}

void stringbuf_copy(StringBuf *dst, const StringBuf *src) {
    if (!dst || !src) return;

    stringbuf_reset(dst);
    if (!stringbuf_ensure_cap(dst, src->length + 1)) return;

    str_copy(dst->str->chars, src->length + 1, src->str->chars, src->length);
    dst->length = src->length;
    dst->str->len = dst->length;
}

StringBuf* stringbuf_dup(const StringBuf *sb) {
    if (!sb) return NULL;

    StringBuf *new_sb = stringbuf_new_cap(sb->pool, sb->length + 1);
    if (!new_sb) return NULL;

    stringbuf_copy(new_sb, sb);
    return new_sb;
}

/*
 * Integer to string functions adapted from strbuf.c
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

static inline size_t num_of_digits(unsigned long v) {
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

void stringbuf_append_ulong(StringBuf *sb, unsigned long value) {
    if (!sb) return;

    static const char digits[201] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";

    size_t num_digits = num_of_digits(value);
    size_t new_length = sb->length + num_digits;

    // Check for 22-bit len field overflow (max 4,194,303 chars)
    if (new_length > 0x3FFFFF) {
        return; // Silently fail to prevent corruption
    }

    if (!stringbuf_ensure_cap(sb, new_length + 1)) return;

    size_t pos = num_digits - 1;
    char *dst = sb->str->chars + sb->length;    while(value >= 100) {
        size_t v = value % 100;
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

    sb->length = new_length;
    sb->str->chars[sb->length] = '\0';
    sb->str->len = sb->length;
}

void stringbuf_append_int(StringBuf *sb, int value) {
    stringbuf_append_long(sb, value);
}

void stringbuf_append_long(StringBuf *sb, long value) {
    if (!sb) return;

    if (value < 0) {
        // Check if adding the minus sign would cause overflow
        if (sb->length >= 0x3FFFFF) {
            return; // Silently fail to prevent corruption
        }
        stringbuf_append_char(sb, '-');
        value = -value;
    }
    stringbuf_append_ulong(sb, value);
}

bool stringbuf_append_file(StringBuf *sb, FILE *file) {
    if (!sb || !file) return false;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) return false;
    if (!stringbuf_ensure_cap(sb, sb->length + size + 1)) return false;

    size_t read = fread(sb->str->chars + sb->length, 1, size, file);
    sb->length += read;
    sb->str->chars[sb->length] = '\0';
    sb->str->len = sb->length;
    return (read >= 0);
}

bool stringbuf_append_file_head(StringBuf *sb, FILE *file, size_t n) {
    if (!sb || !file) return false;

    if (!stringbuf_ensure_cap(sb, sb->length + n + 1)) return false;

    size_t read = fread(sb->str->chars + sb->length, 1, n, file);
    sb->length += read;
    sb->str->chars[sb->length] = '\0';
    sb->str->len = sb->length;
    return (read >= 0);
}

String* stringbuf_to_string(StringBuf *sb) {
    if (!sb || !sb->str) return NULL;

    // Ensure the String structure is properly set up
    sb->str->len = sb->length;
    sb->str->ref_cnt = 0;

    // Return the String and reset the StringBuf
    String *result = sb->str;
    sb->str = NULL;
    sb->length = 0;
    sb->capacity = 0;

    return result;
}
