#include "strbuf.h"

#define INITIAL_CAPACITY 32

StrBuf* _strbuf_new_cap(size_t size, VariableMemPool *pool) {
    StrBuf *sb;
    if (pool) {
        MemPoolError err = pool_variable_alloc(pool, sizeof(StrBuf), (void**)&sb);
        if (err != MEM_POOL_ERR_OK) { return NULL; }
        sb->pool = pool;
        sb->length = sb->capacity = 0;  sb->str = NULL; 
    } else {
        sb = (StrBuf*) malloc(sizeof(StrBuf));
        if (!sb) return NULL;
        sb->str = malloc(size);
        if (!sb->str) { free(sb);  return NULL; }
        sb->str[0] = '\0';  sb->length = 0;
        sb->capacity = size;  sb->pool = NULL;
    }
    return sb;
}

StrBuf* strbuf_new_cap(size_t size) {
    return _strbuf_new_cap(size, NULL);
}

StrBuf* strbuf_new() {
    return strbuf_new_cap(INITIAL_CAPACITY);
}

StrBuf* strbuf_new_pooled(VariableMemPool *pool) {
    return _strbuf_new_cap(INITIAL_CAPACITY, pool);
}

StrBuf* strbuf_create(const char *str) {
    size_t str_len = strlen(str);
    StrBuf *sbuf = strbuf_new_cap(str_len + 1);
    if (!sbuf) return NULL;
    memcpy(sbuf->str, str, str_len);
    sbuf->str[sbuf->length = str_len] = '\0';
    return sbuf;
}

void strbuf_free(StrBuf *sb) {
    if (sb->pool) {
        if (sb->str) { pool_variable_free(sb->pool, sb->str);}
        pool_variable_free(sb->pool, sb);
    } else {
        if (sb->str) free(sb->str);
        free(sb);
    }
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
    size_t new_capacity = sb->capacity ? sb->capacity : INITIAL_CAPACITY;
    while (new_capacity < min_capacity) { new_capacity *= 2; }

    printf("strbuf_expand_cap: %zu -> %zu, %p\n", sb->capacity, new_capacity, (void*)sb->str);
    char *new_s;
    if (!sb->pool) new_s = (char*)realloc(sb->str, new_capacity);
    else {
        if (!sb->length) { // first allocation
            MemPoolError err = pool_variable_alloc(sb->pool, new_capacity, (void**)&new_s);
            if (err != MEM_POOL_ERR_OK) return false;
            sb->length = sizeof(uint32_t);  // reserved for prepended String length 
        }
        else { // realloc existing buffer
            new_s = (char*)pool_variable_realloc(sb->pool, sb->str, (sb->length+1), new_capacity);
            printf("strbuf expanded: %p -> %p\n", (void*)sb->str, (void*)new_s);
        }
    }
    if (!new_s) return false;
    sb->str = new_s;  sb->capacity = new_capacity;
    return true;
}

void strbuf_append_str(StrBuf *sb, const char *str) {
    if (!str) return;
    size_t str_len = strlen(str);
    if (!strbuf_ensure_cap(sb, sb->length + str_len + 1)) return;
    memcpy(sb->str + sb->length, str, str_len + 1);
    sb->length += str_len;
}

// Copy N characters from a character array to the end of this StrBuf
// strlen(__str) must be >= __n
void strbuf_append_str_n(StrBuf *sb, const char *str, size_t len) {
    if (!str) return;
    if (!strbuf_ensure_cap(sb, sb->length + len + 1)) return;
    memcpy(sb->str + sb->length, str, len);
    sb->length += len;
    sb->str[sb->length] = '\0';
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
    memset(buf->str + buf->length, c, n);
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
}

void strbuf_copy(StrBuf *dst, const StrBuf *src) {
    if (!dst || !src) return;
    strbuf_reset(dst);
    if (!strbuf_ensure_cap(dst, src->length + 1)) return;
    memcpy(dst->str, src->str, src->length + 1);
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

void strbuf_append_ulong(StrBuf *buf, unsigned long value) {
    // Append two digits at a time
    static const char digits[201] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";

    size_t num_digits = num_of_digits(value);
    size_t pos = num_digits - 1;

    strbuf_ensure_cap(buf, buf->length + num_digits);
    char *dst = buf->str + buf->length; // Updated to use buf->s instead of buf->b

    while(value >= 100) {
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
    buf->length += num_digits;
    buf->str[buf->length] = '\0'; // Updated to use buf->str instead of buf->b
}

void strbuf_append_int(StrBuf *buf, int value) {
    // strbuf_sprintf(buf, "%i", value);
    strbuf_append_long(buf, value);
}

void strbuf_append_long(StrBuf *buf, long value) {
    // strbuf_sprintf(buf, "%li", value);
    if (value < 0) { strbuf_append_char(buf, '-'); value = -value; }
    strbuf_append_ulong(buf, value);
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