#pragma once

#include <stddef.h>

// Symbol lookup belongs to the content layer; radiant may register richer
// decoding without exposing its view-owned SymbolResolution representation.
typedef enum CssSymbolKind {
    CSS_SYMBOL_UNKNOWN,
    CSS_SYMBOL_EMOJI,
    CSS_SYMBOL_HTML_ENTITY,
} CssSymbolKind;

typedef struct CssSymbolResolution {
    CssSymbolKind kind;
    const char* utf8;
} CssSymbolResolution;

typedef CssSymbolResolution (*CssSymbolResolveFn)(const char* name, size_t length);

void css_symbol_resolve_register(CssSymbolResolveFn resolver);
CssSymbolResolution css_symbol_resolve(const char* name, size_t length);
