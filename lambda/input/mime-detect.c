#include "mime-detect.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// Define _GNU_SOURCE for memmem on Linux, not needed on macOS
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

// Define memmem if not available on some older platforms
#if !defined(__GLIBC__) && !defined(__APPLE__) && !defined(__FreeBSD__)
static void* memmem(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len) {
    if (needle_len == 0) return (void*)haystack;
    if (needle_len > haystack_len) return NULL;
    
    const char* h = (const char*)haystack;
    const char* n = (const char*)needle;
    
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(h + i, n, needle_len) == 0) {
            return (void*)(h + i);
        }
    }
    return NULL;
}
#endif

// Helper function to match glob patterns
int match_glob(const char* pattern, const char* string) {
    if (!pattern || !string) return 0;
    
    const char* p = pattern;
    const char* s = string;
    
    while (*p && *s) {
        if (*p == '*') {
            // Skip multiple asterisks
            while (*p == '*') p++;
            if (!*p) return 1; // Pattern ends with *, matches everything
            
            // Find the next character after *
            while (*s && *s != *p) s++;
            if (!*s) return 0;
        } else if (*p == '?') {
            // ? matches any single character
            p++;
            s++;
        } else if (tolower(*p) == tolower(*s)) {
            p++;
            s++;
        } else {
            return 0;
        }
    }
    
    // Skip trailing asterisks in pattern
    while (*p == '*') p++;
    
    return (*p == '\0' && *s == '\0');
}

// Helper function to match magic patterns
int match_magic(const char* pattern, size_t pattern_len, const char* data, size_t data_len, int offset) {
    if (!pattern || !data) return 0;
    if (offset < 0 || (size_t)offset + pattern_len > data_len) return 0;
    
    return memcmp(data + offset, pattern, pattern_len) == 0;
}

// Helper function to check if data looks like text
static int is_text_data(const char* data, size_t len) {
    if (!data || len == 0) return 1;
    
    size_t check_len = len > 1024 ? 1024 : len; // Check first 1KB
    size_t text_chars = 0;
    size_t total_chars = 0;
    
    for (size_t i = 0; i < check_len; i++) {
        unsigned char c = (unsigned char)data[i];
        total_chars++;
        
        // Count printable ASCII, common whitespace, and UTF-8 continuation bytes
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r' || (c >= 0x80 && c <= 0xBF)) {
            text_chars++;
        } else if (c == 0) {
            // Null bytes are strong indicators of binary data
            return 0;
        }
    }
    
    // If at least 70% of characters are text-like, consider it text
    return (text_chars * 100 / total_chars) >= 70;
}

// Helper function to detect specific subtypes
static const char* detect_subtype(const char* base_type, const char* data, size_t data_len) {
    if (!base_type || !data || data_len == 0) return base_type;
    
    if (strcmp(base_type, "application/zip") == 0) {
        // Check for Office Open XML documents
        if (data_len > 50) {
            // Look for the mimetype entry in ZIP files
            const char* mimetype_pos = memmem(data, data_len > 512 ? 512 : data_len, "mimetype", 8);
            if (mimetype_pos) {
                if (memmem(data, data_len > 512 ? 512 : data_len, "application/vnd.openxmlformats-officedocument.wordprocessingml.document", 72)) {
                    return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
                }
                if (memmem(data, data_len > 512 ? 512 : data_len, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", 67)) {
                    return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
                }
                if (memmem(data, data_len > 512 ? 512 : data_len, "application/vnd.openxmlformats-officedocument.presentationml.presentation", 73)) {
                    return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
                }
                if (memmem(data, data_len > 512 ? 512 : data_len, "application/epub+zip", 20)) {
                    return "application/epub+zip";
                }
            }
        }
    } else if (strcmp(base_type, "image/webp") == 0) {
        // Validate WebP format
        if (data_len > 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
            return "image/webp";
        }
        return "application/octet-stream"; // Not actually WebP
    } else if (strcmp(base_type, "audio/wav") == 0) {
        // Validate WAV format
        if (data_len > 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0) {
            return "audio/wav";
        }
        return "application/octet-stream"; // Not actually WAV
    } else if (strcmp(base_type, "application/json") == 0) {
        // Simple JSON validation
        if (data_len > 0) {
            const char* trimmed = data;
            size_t trimmed_len = data_len;
            
            // Skip leading whitespace
            while (trimmed_len > 0 && isspace(*trimmed)) {
                trimmed++;
                trimmed_len--;
            }
            
            if (trimmed_len > 0 && (*trimmed == '{' || *trimmed == '[')) {
                return "application/json";
            }
        }
        return is_text_data(data, data_len) ? "text/plain" : "application/octet-stream";
    }
    
    return base_type;
}

// Initialize MIME detector
MimeDetector* mime_detector_init(void) {
    MimeDetector* detector = malloc(sizeof(MimeDetector));
    if (!detector) return NULL;
    
    detector->magic_patterns = magic_patterns;
    detector->magic_patterns_count = MAGIC_PATTERNS_COUNT;
    detector->glob_patterns = glob_patterns;
    detector->glob_patterns_count = GLOB_PATTERNS_COUNT;
    
    return detector;
}

// Destroy MIME detector
void mime_detector_destroy(MimeDetector* detector) {
    if (detector) {
        free(detector);
    }
}

// Detect MIME type from filename
const char* detect_mime_from_filename(MimeDetector* detector, const char* filename) {
    if (!detector || !filename) return NULL;
    
    // Convert to lowercase for comparison
    char* lower_filename = malloc(strlen(filename) + 1);
    if (!lower_filename) return NULL;
    
    for (size_t i = 0; filename[i]; i++) {
        lower_filename[i] = tolower(filename[i]);
    }
    lower_filename[strlen(filename)] = '\0';
    
    // Check glob patterns
    for (size_t i = 0; i < detector->glob_patterns_count; i++) {
        if (match_glob(detector->glob_patterns[i].pattern, lower_filename)) {
            free(lower_filename);
            return detector->glob_patterns[i].mime_type;
        }
    }
    
    free(lower_filename);
    return NULL;
}

// Detect MIME type from content
const char* detect_mime_from_content(MimeDetector* detector, const char* data, size_t data_len) {
    if (!detector || !data || data_len == 0) return NULL;
    
    const char* best_match = NULL;
    int best_priority = -1;
    
    // Check magic patterns sorted by priority
    for (size_t i = 0; i < detector->magic_patterns_count; i++) {
        MimePattern* pattern = &detector->magic_patterns[i];
        
        if (match_magic(pattern->pattern, pattern->pattern_len, data, data_len, pattern->offset)) {
            if (pattern->priority > best_priority) {
                best_match = pattern->mime_type;
                best_priority = pattern->priority;
            }
        }
    }
    
    if (best_match) {
        return detect_subtype(best_match, data, data_len);
    }
    
    // Fallback: check if it's text data
    if (is_text_data(data, data_len)) {
        return "text/plain";
    }
    
    return "application/octet-stream";
}

// Main MIME type detection function
const char* detect_mime_type(MimeDetector* detector, const char* filename, const char* data, size_t data_len) {
    if (!detector) return NULL;
    
    const char* filename_mime = NULL;
    const char* content_mime = NULL;
    
    // Try filename detection first
    if (filename) {
        filename_mime = detect_mime_from_filename(detector, filename);
    }
    
    // Try content detection
    if (data && data_len > 0) {
        content_mime = detect_mime_from_content(detector, data, data_len);
    }
    
    // Priority logic:
    // 1. If we have both detections and content is high priority (like PDF magic), use content
    // 2. If we have filename detection, prefer it (more specific)
    // 3. Otherwise use content detection
    
    if (content_mime && filename_mime) {
        // Check if content detection is high-priority (like PDF, binary formats)
        if (strstr(content_mime, "pdf") || 
            strstr(content_mime, "image/") ||
            strstr(content_mime, "application/zip")) {
            return detect_subtype(content_mime, data, data_len);
        }
        // For text-based formats, prefer filename
        return filename_mime;
    }
    
    // Return whichever one we have
    if (filename_mime) {
        return filename_mime;
    }
    
    if (content_mime) {
        return detect_subtype(content_mime, data, data_len);
    }
    
    return "application/octet-stream";
}
