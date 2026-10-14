#include "text_breaks.hpp"
#include "../lib/memtrack.h"
#include <limits.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

LayoutTextBreaks::LayoutTextBreaks(const char* text, size_t length, bool enabled) {
#ifdef __APPLE__
    if (!enabled || !text || !length || length > LONG_MAX) return;
    bool non_ascii = false;
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char)text[i] >= 0x80) { non_ascii = true; break; }
    }
    if (!non_ascii) return;
    CFStringRef source = CFStringCreateWithBytes(kCFAllocatorDefault,
        (const UInt8*)text, (CFIndex)length, kCFStringEncodingUTF8, false);
    if (!source) return;
    CFStringTokenizerRef tokenizer = CFStringTokenizerCreate(kCFAllocatorDefault,
        source, CFRangeMake(0, CFStringGetLength(source)),
        kCFStringTokenizerUnitLineBreak, nullptr);
    if (tokenizer) {
        boundaries_ = (uint8_t*)mem_calloc(length + 1, sizeof(uint8_t), MEM_CAT_LAYOUT);
        if (boundaries_) {
            length_ = length;
            const unsigned char* cursor = (const unsigned char*)text;
            const unsigned char* end = cursor + length;
            CFIndex utf16_offset = 0;
            while (CFStringTokenizerAdvanceToNextToken(tokenizer) !=
                   kCFStringTokenizerTokenNone) {
                CFRange range = CFStringTokenizerGetCurrentTokenRange(tokenizer);
                CFIndex boundary = range.location + range.length;
                // the resource uses UTF-16; advance the source map once so
                // dictionary boundaries remain valid UTF-8/grapheme edges.
                while (cursor < end && utf16_offset < boundary) {
                    uint32_t cp = 0;
                    if (!layout_utf8_next_codepoint(&cursor, end, &cp)) break;
                    utf16_offset += cp > 0xFFFF ? 2 : 1;
                }
                if (utf16_offset == boundary) boundaries_[cursor - (const unsigned char*)text] = 1;
            }
        }
        CFRelease(tokenizer);
    }
    CFRelease(source);
#else
    (void)text; (void)length; (void)enabled;
#endif
}

LayoutTextBreaks::~LayoutTextBreaks() { mem_free(boundaries_); }


