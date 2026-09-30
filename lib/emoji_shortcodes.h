#ifndef EMOJI_SHORTCODES_H
#define EMOJI_SHORTCODES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Look up a Markdown emoji name without its surrounding colons.
const char* emoji_shortcode_lookup(const char* name, size_t length);

#ifdef __cplusplus
}
#endif

#endif
