#include "text_breaks.hpp"
#include "../lib/hash.h"
#include "../lib/memtrack.h"
#include <limits.h>
#include <string.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>

static constexpr size_t TEXT_BREAK_CACHE_BYTES = 256;

struct NativeTextBreakEntry {
    uint32_t hash;
    size_t length;
    bool valid;
    char text[TEXT_BREAK_CACHE_BYTES];
    uint8_t boundaries[TEXT_BREAK_CACHE_BYTES + 1];
};

struct NativeTextBreakCache {
    CFLocaleRef locale = nullptr;
    NativeTextBreakEntry entries[64] = {};
    ~NativeTextBreakCache() { if (locale) CFRelease(locale); }
    NativeTextBreakEntry* find(size_t length, uint32_t hash) {
        if (length > TEXT_BREAK_CACHE_BYTES) return nullptr;
        CFLocaleRef current = CFLocaleCopyCurrent();
        if (!current) return nullptr;
        if (!locale || !CFEqual(locale, current)) {
            for (auto& entry : entries) entry.valid = false;
            if (locale) CFRelease(locale);
            locale = current;
        } else CFRelease(current);
        return &entries[hash % (sizeof(entries) / sizeof(entries[0]))];
    }
};
#endif

LayoutTextBreaks::LayoutTextBreaks(const char* text, size_t length, bool enabled) {
#ifdef __APPLE__
    if (!enabled || !text || !length || length > LONG_MAX) return;
    bool non_ascii = false;
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char)text[i] >= 0x80) { non_ascii = true; break; }
    }
    if (!non_ascii) return;
    // reflow changes widths/fonts without changing Unicode break opportunities.
    // cache owned bytes/maps, then copy into this probe so nested probes cannot
    // invalidate its boundaries through eviction (D4.1.4v5 / D4.5.1v4).
    static thread_local NativeTextBreakCache cache;
    uint32_t hash = length <= TEXT_BREAK_CACHE_BYTES ? hash_fnv1a_32(text, length) : 0;
    NativeTextBreakEntry* cached = cache.find(length, hash);
    if (cached && cached->valid && cached->hash == hash && cached->length == length &&
        memcmp(cached->text, text, length) == 0) {
        boundaries_ = (uint8_t*)mem_alloc(length + 1, MEM_CAT_LAYOUT);
        if (boundaries_) {
            memcpy(boundaries_, cached->boundaries, length + 1);
            length_ = length;
        }
        return;
    }
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
    if (cached && boundaries_) {
        memcpy(cached->text, text, length);
        memcpy(cached->boundaries, boundaries_, length + 1);
        cached->hash = hash;
        cached->length = length;
        cached->valid = true;
    }
#else
    (void)text; (void)length; (void)enabled;
#endif
}

LayoutTextBreaks::~LayoutTextBreaks() { mem_free(boundaries_); }


