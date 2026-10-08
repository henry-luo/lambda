#include "css_symbol_hook.h"
#include "../../../lib/emoji_shortcodes.h"
#include "../../../lib/html_entities.h"

CssSymbolResolution css_symbol_resolve(const char* name, size_t length) {
    // emoji take precedence over entities with the same name, such as heart.
    if (const char* utf8 = emoji_shortcode_lookup(name, length)) {
        return {CSS_SYMBOL_EMOJI, utf8};
    }
    if (const char* utf8 = html_entity_lookup(name, length)) {
        return {CSS_SYMBOL_HTML_ENTITY, utf8};
    }
    return {CSS_SYMBOL_UNKNOWN, nullptr};
}
