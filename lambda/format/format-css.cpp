// CSS Formatter - Routes to new CSS formatter
#include "format.h"
#include "../input/css/css_formatter.hpp"
#include "../input/css/css_style.hpp"
#include "../../lib/stringbuf.h"
#include <string.h>

// Helper to check if item is a CssStylesheet (has 0xCC marker)
static bool is_css_stylesheet_item(Item item) {
    uint64_t marker = (item.item >> 56) & 0xFF;
    return marker == 0xCC;
}

// Helper to extract CssStylesheet* from item
static CssStylesheet* get_css_stylesheet(Item item) {
    return (CssStylesheet*)(item.item & 0x00FFFFFFFFFFFFFF);
}

// Main CSS formatting function
String* format_css(Pool *pool, Item item) {
    // Check if this is a CssStylesheet from the new parser
    if (is_css_stylesheet_item(item)) {
        CssStylesheet* stylesheet = get_css_stylesheet(item);
        if (stylesheet) {
            // Use the new CSS formatter
            CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_EXPANDED);
            if (formatter) {
                const char* result_str = css_format_stylesheet(formatter, stylesheet);
                if (result_str) {
                    size_t len = strlen(result_str);
                    String* result = (String*)pool_alloc(pool, sizeof(String) + len + 1);
                    if (result) {
                        result->len = len;
                        result->ref_cnt = 1;
                        memcpy(result->chars, result_str, len + 1);
                        css_formatter_destroy(formatter);
                        return result;
                    }
                }
                css_formatter_destroy(formatter);
            }
        }
    }

    // Return empty string for unsupported input
    String* empty = (String*)pool_alloc(pool, sizeof(String) + 1);
    if (empty) {
        empty->len = 0;
        empty->ref_cnt = 1;
        empty->chars[0] = '\0';
    }
    return empty;
}
