/**
 * @file mime.cpp
 * @brief MIME type detection implementation
 *
 * Migrated and integrated from lib/mime-detect.c.
 * Combines glob (filename) and magic (content) pattern matching
 * with text/binary heuristic fallback.
 */

#include "mime.hpp"
#include "serve_utils.hpp"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ============================================================================
// Built-in glob patterns (filename → MIME type)
// ============================================================================

static MimeGlob builtin_globs[] = {
    // web
    {"*.html",  "text/html"},
    {"*.htm",   "text/html"},
    {"*.css",   "text/css"},
    {"*.js",    "application/javascript"},
    {"*.mjs",   "application/javascript"},
    {"*.json",  "application/json"},
    {"*.xml",   "application/xml"},
    {"*.svg",   "image/svg+xml"},
    {"*.wasm",  "application/wasm"},

    // text
    {"*.txt",   "text/plain"},
    {"*.csv",   "text/csv"},
    {"*.md",    "text/markdown"},
    {"*.yaml",  "text/yaml"},
    {"*.yml",   "text/yaml"},
    {"*.toml",  "application/toml"},
    {"*.ini",   "text/plain"},
    {"*.log",   "text/plain"},
    {"*.rtf",   "application/rtf"},
    {"*.tex",   "application/x-latex"},
    {"*.ls",    "text/x-lambda"},

    // images
    {"*.png",   "image/png"},
    {"*.jpg",   "image/jpeg"},
    {"*.jpeg",  "image/jpeg"},
    {"*.gif",   "image/gif"},
    {"*.bmp",   "image/bmp"},
    {"*.webp",  "image/webp"},
    {"*.ico",   "image/x-icon"},
    {"*.tiff",  "image/tiff"},
    {"*.tif",   "image/tiff"},
    {"*.avif",  "image/avif"},

    // fonts
    {"*.woff",  "font/woff"},
    {"*.woff2", "font/woff2"},
    {"*.ttf",   "font/ttf"},
    {"*.otf",   "font/otf"},
    {"*.eot",   "application/vnd.ms-fontobject"},

    // audio
    {"*.mp3",   "audio/mpeg"},
    {"*.wav",   "audio/wav"},
    {"*.ogg",   "audio/ogg"},
    {"*.flac",  "audio/flac"},
    {"*.aac",   "audio/aac"},
    {"*.m4a",   "audio/mp4"},

    // video
    {"*.mp4",   "video/mp4"},
    {"*.webm",  "video/webm"},
    {"*.avi",   "video/x-msvideo"},
    {"*.mkv",   "video/x-matroska"},
    {"*.mov",   "video/quicktime"},

    // archives
    {"*.zip",   "application/zip"},
    {"*.gz",    "application/gzip"},
    {"*.tar",   "application/x-tar"},
    {"*.bz2",   "application/x-bzip2"},
    {"*.xz",    "application/x-xz"},
    {"*.7z",    "application/x-7z-compressed"},
    {"*.rar",   "application/vnd.rar"},

    // documents
    {"*.pdf",   "application/pdf"},
    {"*.doc",   "application/msword"},
    {"*.docx",  "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"*.xls",   "application/vnd.ms-excel"},
    {"*.xlsx",  "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {"*.ppt",   "application/vnd.ms-powerpoint"},
    {"*.pptx",  "application/vnd.openxmlformats-officedocument.presentationml.presentation"},

    // programming
    {"*.c",     "text/x-c"},
    {"*.cpp",   "text/x-c++"},
    {"*.h",     "text/x-c"},
    {"*.hpp",   "text/x-c++"},
    {"*.py",    "text/x-python"},
    {"*.rb",    "text/x-ruby"},
    {"*.java",  "text/x-java"},
    {"*.go",    "text/x-go"},
    {"*.rs",    "text/x-rust"},
    {"*.sh",    "application/x-sh"},
    {"*.bat",   "application/x-msdos-program"},

    {NULL, NULL}
};

// ============================================================================
// Built-in magic patterns (content byte signatures)
// ============================================================================

static MimePattern builtin_magic[] = {
    // images
    {"\x89PNG\r\n\x1a\n",  8, 0, 80, "image/png"},
    {"\xff\xd8\xff",        3, 0, 80, "image/jpeg"},
    {"GIF87a",              6, 0, 80, "image/gif"},
    {"GIF89a",              6, 0, 80, "image/gif"},
    {"BM",                  2, 0, 40, "image/bmp"},
    {"RIFF",                4, 0, 30, "image/webp"},     // needs subtype check

    // audio/video
    {"ID3",                 3, 0, 70, "audio/mpeg"},
    {"\xff\xfb",            2, 0, 50, "audio/mpeg"},
    {"OggS",                4, 0, 70, "audio/ogg"},
    {"fLaC",                4, 0, 70, "audio/flac"},
    {"RIFF",                4, 0, 30, "audio/wav"},      // needs subtype check

    // archives
    {"PK\x03\x04",          4, 0, 70, "application/zip"},
    {"\x1f\x8b",            2, 0, 70, "application/gzip"},
    {"Rar!\x1a\x07",        6, 0, 70, "application/vnd.rar"},
    {"\xfd""7zXZ\0",        6, 0, 70, "application/x-xz"},
    {"7z\xbc\xaf\x27\x1c",  6, 0, 70, "application/x-7z-compressed"},

    // documents
    {"%PDF",                4, 0, 90, "application/pdf"},

    // executables
    {"\x7f""ELF",           4, 0, 90, "application/x-elf"},
    {"\xfe\xed\xfa",        3, 0, 90, "application/x-mach-binary"},
    {"MZ",                  2, 0, 60, "application/x-dosexec"},

    // web assembly
    {"\0asm",               4, 0, 90, "application/wasm"},

    {NULL, 0, 0, 0, NULL}
};

// ============================================================================
// Reverse lookup table (content-type → extension)
// ============================================================================

struct MimeExtMap {
    const char *content_type;
    const char *extension;
};

static MimeExtMap ext_map[] = {
    {"text/html",                ".html"},
    {"text/css",                 ".css"},
    {"application/javascript",   ".js"},
    {"application/json",         ".json"},
    {"application/xml",          ".xml"},
    {"text/plain",               ".txt"},
    {"text/csv",                 ".csv"},
    {"text/markdown",            ".md"},
    {"text/yaml",                ".yaml"},
    {"image/png",                ".png"},
    {"image/jpeg",               ".jpg"},
    {"image/gif",                ".gif"},
    {"image/svg+xml",            ".svg"},
    {"image/webp",               ".webp"},
    {"application/pdf",          ".pdf"},
    {"application/zip",          ".zip"},
    {"application/gzip",         ".gz"},
    {"font/woff2",               ".woff2"},
    {"font/woff",                ".woff"},
    {"audio/mpeg",               ".mp3"},
    {"video/mp4",                ".mp4"},
    {"application/wasm",         ".wasm"},
    {"application/octet-stream", ".bin"},
    {NULL, NULL}
};

// ============================================================================
// Glob matching
// ============================================================================

int mime_match_glob(const char *pattern, const char *string) {
    if (!pattern || !string) return 0;

    while (*pattern && *string) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return 1; // trailing * matches everything
            while (*string) {
                if (mime_match_glob(pattern, string)) return 1;
                string++;
            }
            return 0;
        }
        if (*pattern == '?') {
            pattern++;
            string++;
        } else {
            if (tolower((unsigned char)*pattern) != tolower((unsigned char)*string))
                return 0;
            pattern++;
            string++;
        }
    }

    // consume trailing wildcards
    while (*pattern == '*') pattern++;
    return (*pattern == '\0' && *string == '\0');
}

// ============================================================================
// Magic matching
// ============================================================================

int mime_match_magic(const char *pattern, size_t pattern_len,
                     const char *data, size_t data_len, int offset) {
    if (!pattern || !data) return 0;
    if (offset < 0) return 0;
    if ((size_t)offset + pattern_len > data_len) return 0;
    return memcmp(data + offset, pattern, pattern_len) == 0;
}

// ============================================================================
// Text heuristic
// ============================================================================

static int is_text_data(const char *data, size_t len) {
    if (!data || len == 0) return 0;

    // sample first 1KB
    size_t check_len = len < 1024 ? len : 1024;
    int printable = 0;
    int total = 0;

    for (size_t i = 0; i < check_len; i++) {
        unsigned char c = (unsigned char)data[i];
        total++;
        // printable ASCII, whitespace, or UTF-8 continuation bytes
        if ((c >= 0x20 && c <= 0x7E) || c == '\n' || c == '\r' ||
            c == '\t' || c >= 0x80) {
            printable++;
        } else if (c == 0x00) {
            return 0; // null byte — binary
        }
    }

    return total > 0 && (printable * 100 / total) > 70;
}

// ============================================================================
// RIFF subtype detection (WebP vs WAV)
// ============================================================================

static const char* detect_riff_subtype(const char *data, size_t data_len) {
    if (data_len >= 12) {
        if (memcmp(data + 8, "WEBP", 4) == 0) return "image/webp";
        if (memcmp(data + 8, "WAVE", 4) == 0) return "audio/wav";
        if (memcmp(data + 8, "AVI ", 4) == 0) return "video/x-msvideo";
    }
    return NULL;
}

// ============================================================================
// JSON content detection
// ============================================================================

static int looks_like_json(const char *data, size_t len) {
    // skip whitespace
    size_t i = 0;
    while (i < len && isspace((unsigned char)data[i])) i++;
    if (i >= len) return 0;
    return data[i] == '{' || data[i] == '[';
}

// ============================================================================
// Public API
// ============================================================================

MimeDetector* mime_detector_create(void) {
    MimeDetector *d = (MimeDetector *)serve_calloc(1, sizeof(MimeDetector));
    if (!d) return NULL;

    // count builtins
    d->magic_patterns = builtin_magic;
    d->magic_count = 0;
    while (builtin_magic[d->magic_count].pattern) d->magic_count++;

    d->glob_patterns = builtin_globs;
    d->glob_count = 0;
    while (builtin_globs[d->glob_count].pattern) d->glob_count++;

    return d;
}

void mime_detector_destroy(MimeDetector *detector) {
    if (!detector) return;
    // builtin tables are static, nothing to free
    serve_free(detector);
}

const char* mime_detect_from_filename(MimeDetector *detector, const char *filename) {
    if (!detector || !filename) return NULL;

    // extract basename for matching
    const char *base = filename;
    const char *p = filename;
    while (*p) {
        if (*p == '/' || *p == '\\') base = p + 1;
        p++;
    }

    for (size_t i = 0; i < detector->glob_count; i++) {
        if (mime_match_glob(detector->glob_patterns[i].pattern, base)) {
            return detector->glob_patterns[i].mime_type;
        }
    }
    return NULL;
}

const char* mime_detect_from_content(MimeDetector *detector, const char *data, size_t data_len) {
    if (!detector || !data || data_len == 0) return NULL;

    const char *best_type = NULL;
    int best_priority = -1;

    for (size_t i = 0; i < detector->magic_count; i++) {
        MimePattern *mp = &detector->magic_patterns[i];
        if (mime_match_magic(mp->pattern, mp->pattern_len, data, data_len, mp->offset)) {
            if (mp->priority > best_priority) {
                best_priority = mp->priority;
                best_type = mp->mime_type;
            }
        }
    }

    // RIFF subtype refinement
    if (best_type && data_len >= 4 && memcmp(data, "RIFF", 4) == 0) {
        const char *riff_type = detect_riff_subtype(data, data_len);
        if (riff_type) best_type = riff_type;
    }

    return best_type;
}

const char* mime_detect(MimeDetector *detector, const char *filename,
                        const char *data, size_t data_len) {
    if (!detector) return "application/octet-stream";

    // try filename first (more reliable for text formats)
    const char *by_name = mime_detect_from_filename(detector, filename);

    // try content magic
    const char *by_content = mime_detect_from_content(detector, data, data_len);

    // prefer content for high-priority binary types (image, pdf)
    if (by_content) {
        // check if content-based detection is high priority (images, pdf, etc)
        for (size_t i = 0; i < detector->magic_count; i++) {
            MimePattern *mp = &detector->magic_patterns[i];
            if (mp->mime_type == by_content && mp->priority >= 80) {
                return by_content;
            }
        }
    }

    // prefer filename for text/code types
    if (by_name) return by_name;
    if (by_content) return by_content;

    // fallback: text heuristic
    if (data && data_len > 0) {
        if (looks_like_json(data, data_len)) return "application/json";
        if (is_text_data(data, data_len)) return "text/plain";
    }

    return "application/octet-stream";
}

const char* mime_extension_for_type(const char *content_type) {
    if (!content_type) return ".bin";

    for (int i = 0; ext_map[i].content_type; i++) {
        if (serve_strcasecmp(ext_map[i].content_type, content_type) == 0) {
            return ext_map[i].extension;
        }
    }
    return ".bin";
}
