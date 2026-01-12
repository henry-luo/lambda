// input-math.cpp - Math expression parser dispatcher
//
// Routes math parsing to appropriate parser based on flavor:
// - LaTeX/Typst: Uses tex_math_bridge (TeX pipeline)
// - ASCII: Custom parser (input-math-ascii.cpp)
//
// This wraps the existing TeX pipeline math parser for standalone use.

#include "input.hpp"
#include "../lambda-data.hpp"
#include "../tex/tex_math_bridge.hpp"
#include "../tex/tex_tfm.hpp"
#include "../mark_builder.hpp"
#include "../../lib/log.h"
#include "../../lib/arena.h"
#include "../../lib/mempool.h"
#include <string.h>

using namespace tex;

// Global TFM font manager for standalone math parsing
// Lazily initialized on first use
static TFMFontManager* g_math_font_manager = nullptr;
static Pool* g_math_pool = nullptr;
static Arena* g_math_arena = nullptr;

// Initialize the global math context (lazily)
static TFMFontManager* get_math_font_manager() {
    if (!g_math_font_manager) {
        g_math_pool = pool_create();
        g_math_arena = arena_create_default(g_math_pool);
        g_math_font_manager = create_font_manager(g_math_arena);
    }
    return g_math_font_manager;
}

// Parse LaTeX math string and return TexNode tree
// This wraps typeset_latex_math() from tex_math_bridge
TexNode* parse_latex_math_to_texnode(const char* math_string, size_t len, float font_size_pt) {
    if (!math_string || len == 0) {
        return nullptr;
    }

    TFMFontManager* fonts = get_math_font_manager();
    if (!fonts) {
        log_error("parse_latex_math: failed to create font manager");
        return nullptr;
    }

    // Create arena for this parse (caller should manage lifetime)
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Create math context with default settings
    MathContext ctx = MathContext::create(arena, fonts, font_size_pt);
    ctx.style = MathStyle::Text;  // Default to inline math style

    // Parse and typeset
    TexNode* result = typeset_latex_math(math_string, len, ctx);

    if (!result) {
        log_debug("parse_latex_math: parsing failed for '%.*s'", (int)len, math_string);
        arena_destroy(arena);
        pool_destroy(pool);
        return nullptr;
    }

    log_debug("parse_latex_math: success, result width=%.2f height=%.2f depth=%.2f",
              result->width, result->height, result->depth);

    // Note: arena is leaked here - caller needs to manage memory
    // TODO: Add arena to result or use a different memory model
    return result;
}

// Parse LaTeX math string and return TexNode tree with custom arena
TexNode* parse_latex_math_with_arena(const char* math_string, size_t len,
                                      float font_size_pt, Arena* arena) {
    if (!math_string || len == 0 || !arena) {
        return nullptr;
    }

    TFMFontManager* fonts = get_math_font_manager();
    if (!fonts) {
        log_error("parse_latex_math: failed to create font manager");
        return nullptr;
    }

    // Create math context
    MathContext ctx = MathContext::create(arena, fonts, font_size_pt);
    ctx.style = MathStyle::Text;

    // Parse and typeset
    return typeset_latex_math(math_string, len, ctx);
}

// Parse math expression string and set input root
// This is the Lambda Input interface
void parse_math(Input* input, const char* math_string, const char* flavor_str) {
    log_debug("parse_math called with: '%s', flavor: '%s'",
              math_string, flavor_str ? flavor_str : "null");

    if (!math_string || !*math_string) {
        input->root = ItemNull;
        return;
    }

    // ASCII math uses separate parser
    if (flavor_str && (strcmp(flavor_str, "ascii") == 0 || strcmp(flavor_str, "asciimath") == 0)) {
        log_debug("parse_math: routing to ASCII math parser");
        Item result = input_ascii_math(input, math_string);
        if (result.item == ITEM_ERROR || result.item == ITEM_NULL) {
            input->root = ItemNull;
            return;
        }
        input->root = result;
        return;
    }

    // LaTeX math: parse to TexNode, then convert to Lambda Item representation
    // For now, we store the raw string and parse it when needed for rendering
    // TODO: Convert TexNode to Lambda Item structure if needed for inspection
    log_debug("parse_math: LaTeX math - storing source for TeX pipeline rendering");

    // Create a simple map with the math source for later rendering
    MarkBuilder builder(input);
    MapBuilder mb = builder.map();
    mb.put("type", builder.createSymbol("latex-math"));
    mb.put("source", builder.createString(math_string));
    mb.put("display", builder.createBool(false));  // inline by default
    input->root = mb.final();
}

// Cleanup function (call at program exit if needed)
void cleanup_math_parser() {
    if (g_math_arena) {
        arena_destroy(g_math_arena);
        g_math_arena = nullptr;
    }
    if (g_math_pool) {
        pool_destroy(g_math_pool);
        g_math_pool = nullptr;
    }
    g_math_font_manager = nullptr;
}
