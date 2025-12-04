// CSS Parser - Uses new CSS engine
#include "input.hpp"
#include "css/css_engine.hpp"
#include "css/css_formatter.hpp"
#include "../../lib/log.h"

// Parse CSS using the new CSS engine
void parse_css(Input* input, const char* css_string) {
    printf("css_parse (stylesheet) - using new CSS engine\n");

    // Use the new CSS engine for parsing
    CssEngine* engine = css_engine_create(input->pool);
    if (!engine) {
        log_error("Failed to create CSS engine");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Parse using the new CSS engine
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css_string, nullptr);
    if (!stylesheet) {
        log_error("Failed to parse CSS stylesheet");
        css_engine_destroy(engine);
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Store the CssStylesheet pointer directly as raw pointer
    // We use a special marker in the high bits to identify this as a CssStylesheet
    // Format: [8-bit marker=0xCC][56-bit pointer]
    uint64_t marker = 0xCC; // CSS stylesheet marker
    input->root = {.item = (marker << 56) | ((uint64_t)stylesheet & 0x00FFFFFFFFFFFFFF)};

    printf("CSS parsing complete: %zu rules\n", stylesheet->rule_count);
}
