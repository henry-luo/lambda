#ifndef RADIANT_TEXT_BREAKS_HPP
#define RADIANT_TEXT_BREAKS_HPP

#include <stddef.h>
#include <stdint.h>

// pass-local UTF-8 boundary map from the platform's Unicode line-break resource.
class LayoutTextBreaks {
    uint8_t* boundaries_ = nullptr;
    size_t length_ = 0;
public:
    LayoutTextBreaks(const char* text, size_t length, bool enabled);
    ~LayoutTextBreaks();
    LayoutTextBreaks(const LayoutTextBreaks&) = delete;
    LayoutTextBreaks& operator=(const LayoutTextBreaks&) = delete;
    bool allows_after(size_t byte_offset) const {
        return boundaries_ && byte_offset > 0 && byte_offset < length_ &&
            boundaries_[byte_offset] != 0;
    }
};
bool layout_utf8_next_codepoint(const char** cursor, const char* end,
                                uint32_t* codepoint);
bool layout_utf8_next_codepoint(const unsigned char** cursor,
                                const unsigned char* end,
                                uint32_t* codepoint);

#endif
