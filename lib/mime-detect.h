#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// MIME type detection structure
typedef struct MimePattern {
    const char* pattern;
    size_t pattern_len;
    int offset;
    int priority;
    const char* mime_type;
} MimePattern;

typedef struct MimeGlob {
    const char* pattern;
    const char* mime_type;
} MimeGlob;

typedef struct MimeDetector {
    MimePattern* magic_patterns;
    size_t magic_patterns_count;
    MimeGlob* glob_patterns;
    size_t glob_patterns_count;
} MimeDetector;

// MIME detection functions
MimeDetector* mime_detector_init(void);
void mime_detector_destroy(MimeDetector* detector);
const char* detect_mime_type(MimeDetector* detector, const char* filename, const char* data, size_t data_len);
const char* detect_mime_from_filename(MimeDetector* detector, const char* filename);
const char* detect_mime_from_content(MimeDetector* detector, const char* data, size_t data_len);

// Helper functions
int match_glob(const char* pattern, const char* string);
int match_magic(const char* pattern, size_t pattern_len, const char* data, size_t data_len, int offset);

// External declarations for MIME type data
extern MimePattern magic_patterns[];
extern MimeGlob glob_patterns[];

// External declarations for counts
extern const size_t MAGIC_PATTERNS_COUNT;
extern const size_t GLOB_PATTERNS_COUNT;

#ifdef __cplusplus
}
#endif
