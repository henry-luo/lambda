#include "mime.hpp"

MimeDetector* mime_detector_create(void) {
    return mime_detector_init_serve();
}

const char* mime_detect(MimeDetector* detector, const char* filename,
                        const char* data, size_t data_len) {
    return detector ? detect_mime_type(detector, filename, data, data_len)
                    : "application/octet-stream";
}

const char* mime_detect_from_filename(MimeDetector* detector, const char* filename) {
    return detect_mime_from_filename(detector, filename);
}

const char* mime_detect_from_content(MimeDetector* detector, const char* data, size_t data_len) {
    return detect_mime_from_content(detector, data, data_len);
}

const char* mime_extension_for_type(const char* content_type) {
    // Serve's legacy API returns .bin for missing or unknown response types.
    const char* ext = mime_extension_from_content_type_serve(content_type);
    return ext ? ext : ".bin";
}

int mime_match_glob(const char* pattern, const char* string) {
    return match_glob(pattern, string);
}

int mime_match_magic(const char* pattern, size_t pattern_len,
                     const char* data, size_t data_len, int offset) {
    return match_magic(pattern, pattern_len, data, data_len, offset);
}
