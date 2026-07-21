#include "css_symbol_hook.h"

static CssSymbolResolveFn g_symbol_resolver = nullptr;

void css_symbol_resolve_register(CssSymbolResolveFn resolver) {
    g_symbol_resolver = resolver;
}

CssSymbolResolution css_symbol_resolve(const char* name, size_t length) {
    return g_symbol_resolver ? g_symbol_resolver(name, length)
        : CssSymbolResolution{CSS_SYMBOL_UNKNOWN, nullptr};
}
