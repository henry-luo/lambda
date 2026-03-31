/**
 * @file mime.hpp
 * @brief MIME type detection — integrated from lib/mime-detect.h
 *
 * Detects content type via:
 *   1. Filename extension (glob patterns)
 *   2. File content (magic number patterns)
 *   3. Text heuristic (UTF-8 / ASCII detection)
 */

#pragma once

#include <stddef.h>

// ============================================================================
// MIME pattern types (magic number and glob)
// ============================================================================

struct MimePattern {
    const char *pattern;        // byte pattern to match
    size_t pattern_len;         // pattern length
    int offset;                 // byte offset in file content
    int priority;               // match priority (higher = preferred)
    const char *mime_type;      // resulting MIME type
};

struct MimeGlob {
    const char *pattern;        // glob pattern (e.g., "*.html")
    const char *mime_type;      // resulting MIME type
};

// ============================================================================
// MIME Detector
// ============================================================================

struct MimeDetector {
    MimePattern *magic_patterns;
    size_t magic_count;
    MimeGlob *glob_patterns;
    size_t glob_count;
};

// create detector with built-in pattern tables
MimeDetector* mime_detector_create(void);
void          mime_detector_destroy(MimeDetector *detector);

// detect MIME type from filename and/or content. returns static string.
// tries filename first, then content, then text heuristic.
const char* mime_detect(MimeDetector *detector, const char *filename,
                        const char *data, size_t data_len);

// detect from filename only
const char* mime_detect_from_filename(MimeDetector *detector, const char *filename);

// detect from content only
const char* mime_detect_from_content(MimeDetector *detector, const char *data, size_t data_len);

// reverse lookup: content-type → common extension (e.g., "text/html" → ".html")
const char* mime_extension_for_type(const char *content_type);

// ============================================================================
// Low-level helpers
// ============================================================================

int mime_match_glob(const char *pattern, const char *string);
int mime_match_magic(const char *pattern, size_t pattern_len,
                     const char *data, size_t data_len, int offset);
