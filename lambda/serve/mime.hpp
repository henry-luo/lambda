#pragma once

#include "../../lib/mime-detect.h"

// Serve keeps its API while sharing the lib detector and pattern database.
MimeDetector* mime_detector_create(void);
const char* mime_detect(MimeDetector* detector, const char* filename,
                        const char* data, size_t data_len);
const char* mime_detect_from_filename(MimeDetector* detector, const char* filename);
const char* mime_detect_from_content(MimeDetector* detector, const char* data, size_t data_len);
const char* mime_extension_for_type(const char* content_type);
int mime_match_glob(const char* pattern, const char* string);
int mime_match_magic(const char* pattern, size_t pattern_len,
                     const char* data, size_t data_len, int offset);
