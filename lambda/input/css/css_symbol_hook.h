#pragma once

#include <stddef.h>

// symbol lookup belongs to the content layer and works without UI initialization.
typedef enum CssSymbolKind {
    CSS_SYMBOL_UNKNOWN,
    CSS_SYMBOL_EMOJI,
    CSS_SYMBOL_HTML_ENTITY,
} CssSymbolKind;

typedef struct CssSymbolResolution {
    CssSymbolKind kind;
    const char* utf8;
} CssSymbolResolution;

CssSymbolResolution css_symbol_resolve(const char* name, size_t length);
