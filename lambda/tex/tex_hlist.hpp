// tex_hlist.hpp - Horizontal List Builder
//
// Converts parsed text into horizontal lists with proper:
// - Character nodes with font metrics
// - Ligatures (fi, fl, ff, ffi, ffl)
// - Kerning between character pairs
// - Inter-word glue
//
// Reference: TeXBook Chapters 4, 12

#ifndef LAMBDA_TEX_HLIST_HPP
#define LAMBDA_TEX_HLIST_HPP

#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "lib/arena.h"

namespace tex {

// ============================================================================
// HList Builder Context
// ============================================================================

struct HListContext {
    Arena* arena;
    TFMFontManager* fonts;

    // Current state
    FontSpec current_font;
    TFMFont* current_tfm;

    // Options
    bool apply_ligatures;
    bool apply_kerning;

    HListContext(Arena* a, TFMFontManager* fm)
        : arena(a), fonts(fm),
          current_tfm(nullptr),
          apply_ligatures(true), apply_kerning(true) {}
};

// ============================================================================
// Text to HList Conversion
// ============================================================================

// Convert a UTF-8 string to an HList
TexNode* text_to_hlist(
    const char* text,
    size_t len,
    HListContext& ctx
);

// Convert a single word to nodes (no spaces)
// Returns first node; linked via next_sibling
TexNode* word_to_nodes(
    const char* word,
    size_t len,
    HListContext& ctx
);

// ============================================================================
// Ligature Processing
// ============================================================================

// Standard TeX ligatures
struct LigatureRule {
    char first;
    char second;
    int result;         // Ligature character code
    const char* name;   // For debugging
};

// Get standard ligature rules
const LigatureRule* get_standard_ligatures(int* count);

// Apply ligatures to a list of character nodes
// Modifies the list in place
void apply_ligatures(TexNode* first, HListContext& ctx);

// ============================================================================
// Kerning
// ============================================================================

// Insert kern nodes between characters where needed
void apply_kerning(TexNode* first, HListContext& ctx);

// ============================================================================
// Font Selection
// ============================================================================

// Set the current font by name
bool set_font(HListContext& ctx, const char* font_name, float size_pt);

// Get metrics for a character in current font
void get_char_metrics(
    HListContext& ctx,
    int32_t codepoint,
    float* width,
    float* height,
    float* depth,
    float* italic
);

// ============================================================================
// Utility Functions
// ============================================================================

// Check if character is a space
inline bool is_space(int32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Check if character can participate in ligatures
inline bool can_ligate(int32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Create inter-word glue node
TexNode* make_interword_glue(HListContext& ctx);

// ============================================================================
// HList Measurement
// ============================================================================

// Compute natural dimensions of an HList
struct HListDimensions {
    float width;
    float height;
    float depth;
    float total_stretch[4];     // By glue order
    float total_shrink[4];
};

HListDimensions measure_hlist(TexNode* hlist);

// ============================================================================
// HList to HBox Conversion
// ============================================================================

// Set glue in an HList to achieve target width
// Returns the glue set ratio (positive = stretch, negative = shrink)
float set_hlist_glue(TexNode* hlist, float target_width);

// Convert HList to HBox with specified width
TexNode* hlist_to_hbox(TexNode* hlist, float width, Arena* arena);

} // namespace tex

#endif // LAMBDA_TEX_HLIST_HPP
