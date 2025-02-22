#include "strbuf.h"

#define INITIAL_CAPACITY 32

StrBuf* strbuf_new_cap(size_t size) {
    StrBuf *sb = (StrBuf*)malloc(sizeof(StrBuf));
    if (!sb) return NULL;
    
    sb->s = (char*)malloc(size);
    if (!sb->s) {
        free(sb);
        return NULL;
    }
    sb->s[0] = '\0';
    sb->length = 0;
    sb->capacity = size;
    return sb;
}

StrBuf* strbuf_new() {
    return strbuf_new_cap(INITIAL_CAPACITY);
}

StrBuf* strbuf_create(const char *str) {
    size_t str_len = strlen(str);
    StrBuf *sbuf = strbuf_new(str_len + 1);
    if (!sbuf) return NULL;
    memcpy(sbuf->s, str, str_len);
    sbuf->s[sbuf->length = str_len] = '\0';
    return sbuf;
}

void strbuf_free(StrBuf *sb) {
    if (sb->s) free(sb->s);
    free(sb);
}

void strbuf_reset(StrBuf *sb) {
    if (sb->s) {
        sb->s[0] = '\0';
        sb->length = 0;
    }
}

bool strbuf_ensure_cap(StrBuf *sb, size_t min_capacity) {
    if (!sb->s) return false;
    if (min_capacity <= sb->capacity) return true;
    
    size_t new_capacity = sb->capacity ? sb->capacity : INITIAL_CAPACITY;
    while (new_capacity < min_capacity) {
        new_capacity *= 2;
    }

    char *new_s = (char*)realloc(sb->s, new_capacity);
    if (!new_s) return false;
    sb->s = new_s;
    sb->capacity = new_capacity;
    return true;
}

void strbuf_append_str(StrBuf *sb, const char *str) {
    if (!str) return;
    size_t str_len = strlen(str);
    if (!strbuf_ensure_cap(sb, sb->length + str_len + 1)) return;
    memcpy(sb->s + sb->length, str, str_len + 1);
    sb->length += str_len;
}

// Copy N characters from a character array to the end of this StrBuf
// strlen(__str) must be >= __n
void strbuf_append_str_n(StrBuf *sb, const char *str, size_t len) {
    if (!str) return;
    if (!strbuf_ensure_cap(sb, sb->length + len + 1)) return;
    memcpy(sb->s + sb->length, str, len);
    sb->length += len;
    sb->s[sb->length] = '\0';
}

void strbuf_append_char(StrBuf *sb, char c) {
    if (!strbuf_ensure_cap(sb, sb->length + 2)) return;
    sb->s[sb->length] = c;
    sb->s[sb->length + 1] = '\0';
    sb->length++;
}

// append char `c` `n` times
void strbuf_append_char_n(StrBuf *buf, char c, size_t n) {
    if (!strbuf_ensure_cap(buf, buf->length + n + 1)) return;
    memset(buf->s + buf->length, c, n);
    buf->length += n;
    buf->s[buf->length] = '\0';
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
    
    size = vsnprintf(sb->s + sb->length, sb->capacity - sb->length, format, args);
    if (size < 0) return;
    sb->length += size;
}

// Resize the buffer to have capacity to hold a string of length new_len
// (+ a null terminating character).  Can also be used to downsize the buffer's
// memory usage.  Returns true on success, false on failure.
bool strbuf_resize(StrBuf *sbuf, size_t new_len) {
    size_t capacity = ROUNDUP2POW(new_len + 1);
    char *new_buff = realloc(sbuf->s, capacity * sizeof(char));
    if (!new_buff) return false;

    sbuf->s = new_buff;
    sbuf->capacity = capacity;
    if (sbuf->length > new_len) {
        // Buffer was shrunk - re-add null byte
        sbuf->length = new_len;
        sbuf->s[sbuf->length] = '\0';
    }
    return true;
}

void strbuf_trim_to_length(StrBuf *sb) {
    if (!sb->s) return;
    char *new_s = (char*)realloc(sb->s, sb->length + 1);
    if (new_s) {
        sb->s = new_s;
        sb->capacity = sb->length + 1;
    }
}

void strbuf_copy(StrBuf *dst, const StrBuf *src) {
    if (!dst || !src) return;
    strbuf_reset(dst);
    if (!strbuf_ensure_cap(dst, src->length + 1)) return;
    memcpy(dst->s, src->s, src->length + 1);
    dst->length = src->length;
}

StrBuf* strbuf_dup(const StrBuf *sb) {
    StrBuf *new_sb = strbuf_new_cap(sb->length + 1);
    if (!new_sb) return NULL;
    strbuf_copy(new_sb, sb);
    return new_sb;
}

void strbuf_chomp(StrBuf *sb) {
    if (!sb->s || sb->length == 0) return;
    int removed = 0;
    while (sb->length > 0) {
        char last = sb->s[sb->length - 1];
        if (last != '\n' && last != '\r') break;
        sb->s[--sb->length] = '\0';
        removed++;
    }
}

bool strbuf_append_file(StrBuf *sb, FILE *file) {
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size < 0) return false;
    if (!strbuf_ensure_cap(sb, sb->length + size + 1)) return false;
    
    size_t read = fread(sb->s + sb->length, 1, size, file);
    sb->length += read;
    sb->s[sb->length] = '\0';
    return (read >= 0);
}

bool strbuf_append_file_head(StrBuf *sb, FILE *file, size_t n) {
    if (!file) return false;
    if (!strbuf_ensure_cap(sb, sb->length + n + 1)) return false;
    
    size_t read = fread(sb->s + sb->length, 1, n, file);
    sb->length += read;
    sb->s[sb->length] = '\0';
    return (read >= 0);
}