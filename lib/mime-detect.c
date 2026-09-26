// Define _GNU_SOURCE before any includes for memmem
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "mime-detect.h"
#include "memtrack.h"
#include "str.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// Declare and define memmem if not available on some platforms
#if !defined(__GLIBC__) && !defined(__APPLE__) && !defined(__FreeBSD__)
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);

void* memmem(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len) {
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

// Helper function to match glob patterns (case-insensitive; '*' any run, '?' one char).
// A '*' must be able to backtrack: the first place its successor matches is not
// always the right one. "*.xml" against ".../.claude/x/test.xml" first meets the
// '.' of ".claude", and without backtracking every file under a dotted directory
// lost its extension-based type.
int match_glob(const char* pattern, const char* string) {
    if (!pattern || !string) return 0;

    const char* p = pattern;
    const char* s = string;
    const char* star_p = NULL;  // pattern just after the last '*'
    const char* star_s = NULL;  // string position that '*' currently extends to

    while (*s) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 1;  // a trailing '*' matches the rest
            star_p = p;
            star_s = s;
        } else if (*p && (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*s))) {
            p++;
            s++;
        } else if (star_p) {
            // mismatch after a '*': let it absorb one more character and retry
            p = star_p;
            s = ++star_s;
        } else {
            return 0;
        }
    }
    // the string is consumed: only '*'s may remain in the pattern
    while (*p == '*') p++;
    return *p == '\0';
}

// Helper function to match magic patterns
int match_magic(const char* pattern, size_t pattern_len, const char* data, size_t data_len, int offset) {
    if (!pattern || !data) return 0;
    if (offset < 0 || (size_t)offset + pattern_len > data_len) return 0;
    
    return memcmp(data + offset, pattern, pattern_len) == 0;
}

// Helper function to check if data looks like text
static int is_text_data(const char* data, size_t len, int serve_profile) {
    if (!data || len == 0) return !serve_profile;
    
    size_t check_len = len > 1024 ? 1024 : len; // Check first 1KB
    size_t text_chars = 0;
    size_t total_chars = 0;
    
    for (size_t i = 0; i < check_len; i++) {
        unsigned char c = (unsigned char)data[i];
        total_chars++;
        
        // Count printable ASCII, common whitespace, and UTF-8 continuation bytes
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r' ||
            (c >= 0x80 && (serve_profile || c <= 0xBF))) {
            text_chars++;
        } else if (c == 0) {
            // Null bytes are strong indicators of binary data
            return 0;
        }
    }
    
    // If at least 70% of characters are text-like, consider it text
    return serve_profile ? (text_chars * 100 / total_chars) > 70
                         : (text_chars * 100 / total_chars) >= 70;
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
            while (trimmed_len > 0 && str_char_is_ascii_space(*trimmed)) {
                trimmed++;
                trimmed_len--;
            }
            
            if (trimmed_len > 0 && (*trimmed == '{' || *trimmed == '[')) {
                return "application/json";
            }
        }
        return is_text_data(data, data_len, 0) ? "text/plain" : "application/octet-stream";
    }
    
    return base_type;
}

// Initialize MIME detector
MimeDetector* mime_detector_init(void) {
    MimeDetector* detector = mem_alloc(sizeof(MimeDetector), MEM_CAT_TEMP);
    if (!detector) return NULL;
    
    detector->magic_patterns = magic_patterns;
    detector->magic_patterns_count = MAGIC_PATTERNS_COUNT;
    detector->glob_patterns = glob_patterns;
    detector->glob_patterns_count = GLOB_PATTERNS_COUNT;
    detector->serve_profile = 0;
    
    return detector;
}

MimeDetector* mime_detector_init_serve(void) {
    MimeDetector* detector = mime_detector_init();
    if (detector) detector->serve_profile = 1;
    return detector;
}

// Destroy MIME detector
void mime_detector_destroy(MimeDetector* detector) {
    if (detector) {
        mem_free(detector);
    }
}

// Detect MIME type from filename
const char* detect_mime_from_filename(MimeDetector* detector, const char* filename) {
    if (!detector || !filename) return NULL;

    // A dotted directory must not consume a filename extension glob.
    const char* base = filename;
    for (const char* p = filename; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    for (size_t i = 0; i < detector->glob_patterns_count; i++) {
        MimeGlob* glob = &detector->glob_patterns[i];
        const char* type = detector->serve_profile ? glob->serve_mime_type : glob->mime_type;
        if (type && match_glob(glob->pattern, base)) return type;
    }
    return NULL;
}

// Match the shared signature table using the caller's existing priority set.
static const char* detect_magic(MimeDetector* detector, const char* data, size_t data_len,
                                int* matched_priority) {
    const char* best_match = NULL;
    int best_priority = -1;
    for (size_t i = 0; i < detector->magic_patterns_count; i++) {
        MimePattern* pattern = &detector->magic_patterns[i];
        int priority = detector->serve_profile ? pattern->serve_priority : pattern->priority;
        const char* type = detector->serve_profile ? pattern->serve_mime_type : pattern->mime_type;
        if (!type || priority < 0) continue;
        if (match_magic(pattern->pattern, pattern->pattern_len, data, data_len, pattern->offset)) {
            if (priority > best_priority) {
                best_match = type;
                best_priority = priority;
            }
        }
    }
    if (matched_priority) *matched_priority = best_priority;
    if (detector->serve_profile && best_match && data_len >= 12 &&
        memcmp(data, "RIFF", 4) == 0) {
        if (memcmp(data + 8, "WEBP", 4) == 0) return "image/webp";
        if (memcmp(data + 8, "WAVE", 4) == 0) return "audio/wav";
        if (memcmp(data + 8, "AVI ", 4) == 0) return "video/x-msvideo";
    }
    return best_match;
}

// Detect MIME type from content
const char* detect_mime_from_content(MimeDetector* detector, const char* data, size_t data_len) {
    if (!detector || !data || data_len == 0) return NULL;
    const char* best_match = detect_magic(detector, data, data_len, NULL);
    if (detector->serve_profile) return best_match;
    if (best_match) {
        return detect_subtype(best_match, data, data_len);
    }
    // Fallback: check if it's text data
    if (is_text_data(data, data_len, 0)) {
        return "text/plain";
    }
    
    return "application/octet-stream";
}

// Main MIME type detection function
const char* detect_mime_type(MimeDetector* detector, const char* filename, const char* data, size_t data_len) {
    if (!detector) return NULL;

    if (detector->serve_profile) {
        const char* filename_mime = detect_mime_from_filename(detector, filename);
        int priority = -1;
        const char* content_mime = data && data_len ? detect_magic(detector, data, data_len, &priority) : NULL;
        if (content_mime && priority >= 80) return content_mime;
        if (filename_mime) return filename_mime;
        if (content_mime) return content_mime;
        if (data && data_len) {
            size_t i = 0;
            while (i < data_len && isspace((unsigned char)data[i])) i++;
            if (i < data_len && (data[i] == '{' || data[i] == '[')) return "application/json";
            if (is_text_data(data, data_len, 1)) return "text/plain";
        }
        return "application/octet-stream";
    }
    
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

// ---------------------------------------------------------------------------
// Content-Type header to file extension mapping
// ---------------------------------------------------------------------------

static int mime_ieq(const char* a, size_t alen, const char* b) {
    size_t blen = strlen(b);
    if (alen != blen) return 0;
    for (size_t i = 0; i < alen; i++) {
        char ca = a[i]; if (ca >= 'A' && ca <= 'Z') ca += 32;
        char cb = b[i]; if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return 1;
}

static const char* mime_extension_lookup(const char* content_type, int serve_profile) {
    if (!content_type) return NULL;

    // Input accepts header parameters; serve's former lookup required an exact type.
    const char* semi = serve_profile ? NULL : strchr(content_type, ';');
    size_t len = semi ? (size_t)(semi - content_type) : strlen(content_type);

    if (!serve_profile) {
        while (len > 0 && (content_type[len-1] == ' ' || content_type[len-1] == '\t')) len--;
    }

    // lookup table — ordered roughly by frequency
    static const struct { const char* mime; const char* ext; int input_only; } table[] = {
        {"text/html",                ".html"},
        {"application/xhtml+xml",    ".html", 1},
        {"text/plain",               ".txt"},
        {"text/css",                 ".css"},
        {"text/javascript",          ".js", 1},
        {"application/javascript",   ".js"},
        {"application/json",         ".json"},
        {"text/xml",                 ".xml", 1},
        {"application/xml",          ".xml"},
        {"text/markdown",            ".md"},
        {"text/x-markdown",          ".md", 1},
        {"application/pdf",          ".pdf"},
        {"image/svg+xml",            ".svg"},
        {"image/png",                ".png"},
        {"image/jpeg",               ".jpg"},
        {"image/gif",                ".gif"},
        {"image/webp",               ".webp"},
        {"application/x-latex",      ".tex", 1},
        {"text/x-tex",               ".tex", 1},
        {"application/x-yaml",       ".yaml", 1},
        {"text/yaml",                ".yaml"},
        {"application/toml",         ".toml", 1},
        {"text/csv",                 ".csv"},
        // Serve's response types share this reverse lookup with input_http.
        {"application/zip",         ".zip"},
        {"application/gzip",        ".gz"},
        {"font/woff2",              ".woff2"},
        {"font/woff",               ".woff"},
        {"audio/mpeg",              ".mp3"},
        {"video/mp4",               ".mp4"},
        {"application/wasm",        ".wasm"},
        {"application/octet-stream", ".bin"},
    };
    int n = (int)(sizeof(table) / sizeof(table[0]));
    for (int i = 0; i < n; i++) {
        if ((!serve_profile || !table[i].input_only) &&
            mime_ieq(content_type, len, table[i].mime)) return table[i].ext;
    }
    return NULL;
}

const char* mime_extension_from_content_type(const char* content_type) {
    return mime_extension_lookup(content_type, 0);
}

const char* mime_extension_from_content_type_serve(const char* content_type) {
    return mime_extension_lookup(content_type, 1);
}
