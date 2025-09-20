#include "cmdedit_utf8.h"
#include <utf8proc.h>
#include <string.h>
#include <ctype.h>

// Get UTF-8 character count using utf8proc
int utf8_char_count(const char* str, size_t byte_len) {
    if (!str || byte_len == 0) return 0;
    
    int char_count = 0;
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read;
    utf8proc_ssize_t pos = 0;
    
    while (pos < (utf8proc_ssize_t)byte_len) {
        bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), 
                                     byte_len - pos, &codepoint);
        if (bytes_read <= 0) {
            // Invalid UTF-8, treat as single byte
            pos++;
        } else {
            pos += bytes_read;
            char_count++;
        }
    }
    
    return char_count;
}

// Get display width using utf8proc
int utf8_display_width(const char* str, size_t byte_len) {
    if (!str || byte_len == 0) return 0;
    
    int total_width = 0;
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read;
    utf8proc_ssize_t pos = 0;
    
    while (pos < (utf8proc_ssize_t)byte_len) {
        bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), 
                                     byte_len - pos, &codepoint);
        if (bytes_read <= 0) {
            // Invalid UTF-8, treat as single width character
            total_width++;
            pos++;
        } else {
            int char_width = utf8proc_charwidth(codepoint);
            if (char_width < 0) char_width = 0;  // Control characters
            total_width += char_width;
            pos += bytes_read;
        }
    }
    
    return total_width;
}

// Convert byte offset to character offset
int utf8_byte_to_char_offset(const char* str, size_t byte_len, size_t byte_offset) {
    if (!str || byte_offset > byte_len) return 0;
    
    int char_offset = 0;
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read;
    utf8proc_ssize_t pos = 0;
    
    while (pos < (utf8proc_ssize_t)byte_offset && pos < (utf8proc_ssize_t)byte_len) {
        bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), 
                                     byte_len - pos, &codepoint);
        if (bytes_read <= 0) {
            // Invalid UTF-8, treat as single byte
            pos++;
        } else {
            pos += bytes_read;
        }
        char_offset++;
    }
    
    return char_offset;
}

// Convert character offset to byte offset
int utf8_char_to_byte_offset(const char* str, size_t byte_len, int char_offset) {
    if (!str || char_offset <= 0) return 0;
    
    int current_char = 0;
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read;
    utf8proc_ssize_t pos = 0;
    
    while (current_char < char_offset && pos < (utf8proc_ssize_t)byte_len) {
        bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), 
                                     byte_len - pos, &codepoint);
        if (bytes_read <= 0) {
            // Invalid UTF-8, treat as single byte
            pos++;
        } else {
            pos += bytes_read;
        }
        current_char++;
    }
    
    return (int)pos;
}

// Get UTF-8 character at specific byte position
bool utf8_get_char_at_byte(const char* str, size_t byte_len, size_t byte_offset, utf8_char_t* out_char) {
    if (!str || !out_char || byte_offset >= byte_len) return false;
    
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + byte_offset), 
                                                  byte_len - byte_offset, &codepoint);
    
    if (bytes_read <= 0) {
        // Invalid UTF-8, treat as single byte
        out_char->bytes[0] = str[byte_offset];
        out_char->byte_length = 1;
        out_char->display_width = 1;
        return false;
    }
    
    // Copy bytes
    out_char->byte_length = (int)bytes_read;
    memcpy(out_char->bytes, str + byte_offset, bytes_read);
    
    // Get display width
    int width = utf8proc_charwidth(codepoint);
    out_char->display_width = (width < 0) ? 0 : width;
    
    return true;
}

// Get display width of character at byte position
int utf8_char_display_width_at(const char* str, size_t byte_len, size_t byte_offset) {
    if (!str || byte_offset >= byte_len) return 0;
    
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + byte_offset), 
                                                  byte_len - byte_offset, &codepoint);
    
    if (bytes_read <= 0) {
        // Invalid UTF-8, treat as single width
        return 1;
    }
    
    int width = utf8proc_charwidth(codepoint);
    return (width < 0) ? 0 : width;
}

// Move cursor left by one character
size_t utf8_move_cursor_left(const char* str, size_t byte_len, size_t current_byte_offset) {
    if (!str || current_byte_offset == 0) return 0;
    
    // Move back byte by byte until we find a valid UTF-8 character start
    size_t pos = current_byte_offset;
    while (pos > 0) {
        pos--;
        // Check if this is a valid UTF-8 character start
        unsigned char byte = (unsigned char)str[pos];
        if ((byte & 0x80) == 0 || (byte & 0xC0) == 0xC0) {
            // This is either ASCII or UTF-8 sequence start
            utf8proc_int32_t codepoint;
            utf8proc_ssize_t bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), 
                                                          byte_len - pos, &codepoint);
            if (bytes_read > 0 && pos + bytes_read <= current_byte_offset) {
                return pos;
            }
        }
    }
    return 0;
}

// Move cursor right by one character
size_t utf8_move_cursor_right(const char* str, size_t byte_len, size_t current_byte_offset) {
    if (!str || current_byte_offset >= byte_len) return byte_len;
    
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + current_byte_offset), 
                                                  byte_len - current_byte_offset, &codepoint);
    
    if (bytes_read <= 0) {
        // Invalid UTF-8, move by one byte
        return current_byte_offset + 1;
    }
    
    return current_byte_offset + bytes_read;
}

// Find word start (moving left)
size_t utf8_find_word_start(const char* str, size_t byte_len, size_t current_byte_offset) {
    if (!str || current_byte_offset == 0) return 0;
    
    size_t pos = current_byte_offset;
    bool found_non_space = false;
    
    while (pos > 0) {
        pos = utf8_move_cursor_left(str, byte_len, pos);
        
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), 
                                                      byte_len - pos, &codepoint);
        
        if (bytes_read <= 0) {
            // Invalid UTF-8, treat as non-space
            if (found_non_space && isspace((unsigned char)str[pos])) {
                return utf8_move_cursor_right(str, byte_len, pos);
            }
            found_non_space = true;
        } else {
            utf8proc_category_t category = utf8proc_category(codepoint);
            bool is_space = (category == UTF8PROC_CATEGORY_ZS || 
                           category == UTF8PROC_CATEGORY_ZL || 
                           category == UTF8PROC_CATEGORY_ZP);
            
            if (found_non_space && is_space) {
                return utf8_move_cursor_right(str, byte_len, pos);
            }
            if (!is_space) {
                found_non_space = true;
            }
        }
    }
    
    return 0;
}

// Find word end (moving right)
size_t utf8_find_word_end(const char* str, size_t byte_len, size_t current_byte_offset) {
    if (!str || current_byte_offset >= byte_len) return byte_len;
    
    size_t pos = current_byte_offset;
    bool found_non_space = false;
    
    while (pos < byte_len) {
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), 
                                                      byte_len - pos, &codepoint);
        
        if (bytes_read <= 0) {
            // Invalid UTF-8, treat as non-space
            if (found_non_space && isspace((unsigned char)str[pos])) {
                return pos;
            }
            found_non_space = true;
            pos++;
        } else {
            utf8proc_category_t category = utf8proc_category(codepoint);
            bool is_space = (category == UTF8PROC_CATEGORY_ZS || 
                           category == UTF8PROC_CATEGORY_ZL || 
                           category == UTF8PROC_CATEGORY_ZP);
            
            if (found_non_space && is_space) {
                return pos;
            }
            if (!is_space) {
                found_non_space = true;
            }
            pos += bytes_read;
        }
    }
    
    return byte_len;
}

// Validate UTF-8 string
bool utf8_is_valid(const char* str, size_t byte_len) {
    if (!str) return false;
    
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes_read;
    utf8proc_ssize_t pos = 0;
    
    while (pos < (utf8proc_ssize_t)byte_len) {
        bytes_read = utf8proc_iterate((const utf8proc_uint8_t*)(str + pos), 
                                     byte_len - pos, &codepoint);
        if (bytes_read <= 0 || codepoint < 0) {
            return false;
        }
        pos += bytes_read;
    }
    
    return true;
}
