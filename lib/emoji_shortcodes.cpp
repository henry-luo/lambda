#include "emoji_shortcodes.h"
#include <string.h>

#include "emoji_shortcodes_table.inc"

const EmojiShortcode* emoji_shortcode_find(const char* name, size_t length) {
    if (!name || !length) return nullptr;
    size_t lower = 0, upper = sizeof(emoji_table) / sizeof(emoji_table[0]);
    while (lower < upper) {
        size_t middle = lower + (upper - lower) / 2;
        const EmojiShortcode* entry = &emoji_table[middle];
        size_t entry_length = strlen(entry->name);
        size_t common = length < entry_length ? length : entry_length;
        int order = memcmp(name, entry->name, common);
        if (!order) order = (length > entry_length) - (length < entry_length);
        if (!order) return entry;
        if (order < 0) upper = middle;
        else lower = middle + 1;
    }
    return nullptr;
}

const EmojiShortcode* emoji_shortcode_match(const char* source, size_t length, size_t* consumed) {
    if (consumed) *consumed = 0;
    if (!source || length < 3 || source[0] != ':') return nullptr;
    size_t end = 1;
    while (end < length) {
        unsigned char c = (unsigned char)source[end];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '+' || c == '-')) break;
        end++;
    }
    if (end == length || source[end] != ':' || end == 1) return nullptr;
    const EmojiShortcode* entry = emoji_shortcode_find(source + 1, end - 1);
    if (entry && consumed) *consumed = end + 1;
    return entry;
}

const char* emoji_shortcode_lookup(const char* name, size_t length) {
    const EmojiShortcode* entry = emoji_shortcode_find(name, length);
    return entry ? entry->utf8 : nullptr;
}
