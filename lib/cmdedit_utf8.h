#ifndef CMDEDIT_UTF8_H
#define CMDEDIT_UTF8_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// UTF-8 character structure for cmdedit
typedef struct {
    char bytes[4];      // UTF-8 bytes (max 4 bytes per character)
    int byte_length;    // Number of bytes used (1-4)
    int display_width;  // Display width (0, 1, or 2 for East Asian wide chars)
} utf8_char_t;

// UTF-8 string utilities using utf8proc
int cmdedit_utf8_char_count(const char* str, size_t byte_len);
int cmdedit_utf8_display_width(const char* str, size_t byte_len);
int cmdedit_utf8_byte_to_char_offset(const char* str, size_t byte_len, size_t byte_offset);
int cmdedit_utf8_char_to_byte_offset(const char* str, size_t byte_len, int char_offset);

// Get UTF-8 character at specific byte position
bool cmdedit_utf8_get_char_at_byte(const char* str, size_t byte_len, size_t byte_offset, utf8_char_t* out_char);

// Get display width of a single UTF-8 character at byte position
int cmdedit_utf8_char_display_width_at(const char* str, size_t byte_len, size_t byte_offset);

// Move cursor by characters (returns new byte offset)
size_t cmdedit_utf8_move_cursor_left(const char* str, size_t byte_len, size_t current_byte_offset);
size_t cmdedit_utf8_move_cursor_right(const char* str, size_t byte_len, size_t current_byte_offset);

// Find word boundaries (for word movement)
size_t cmdedit_utf8_find_word_start(const char* str, size_t byte_len, size_t current_byte_offset);
size_t cmdedit_utf8_find_word_end(const char* str, size_t byte_len, size_t current_byte_offset);

// Validate UTF-8 string
bool utf8_is_valid(const char* str, size_t byte_len);

#ifdef __cplusplus
}
#endif

#endif // CMDEDIT_UTF8_H
