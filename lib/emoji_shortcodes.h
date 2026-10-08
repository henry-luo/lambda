#ifndef EMOJI_SHORTCODES_H
#define EMOJI_SHORTCODES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmojiShortcode {
    const char* name;
    const char* utf8;
    const char* image_url;
} EmojiShortcode;

// entries have static storage; exactly one of utf8 and image_url is non-null.
const EmojiShortcode* emoji_shortcode_find(const char* name, size_t length);

// match :name: within length bytes, stopping at NUL; consumed includes the colons.
const EmojiShortcode* emoji_shortcode_match(const char* source, size_t length, size_t* consumed);

// look up a Unicode emoji name; custom image entries return null.
const char* emoji_shortcode_lookup(const char* name, size_t length);

#ifdef __cplusplus
}
#endif

#endif
