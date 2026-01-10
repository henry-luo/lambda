// tex_radiant_bridge.hpp - Bridge between TeX typesetting and Radiant rendering
//
// Provides conversion between tex::TexBox (typesetting result) and
// radiant::MathBox (rendering representation), enabling the TeX engine
// to integrate with Radiant's layout and rendering pipeline.

#ifndef TEX_RADIANT_BRIDGE_HPP
#define TEX_RADIANT_BRIDGE_HPP

#include "tex_box.hpp"
#include "tex_radiant_font.hpp"
#include "../../radiant/math_box.hpp"
#include "../../radiant/math_context.hpp"
#include "../../radiant/view.hpp"
#include "../../lib/arena.h"

namespace tex {

// Forward declaration
class RadiantFontProvider;

// ============================================================================
// TexBox to MathBox Conversion
// ============================================================================

struct ConversionContext {
    Arena* arena;                      // Arena for radiant::MathBox allocation
    RadiantFontProvider* font_provider; // For getting FT_Face references
    float base_size;                   // Base font size in pixels
    float scale;                       // Current scale factor (for scripts)
};

// Convert a tex::TexBox tree to a radiant::MathBox tree
radiant::MathBox* convert_tex_to_math_box(
    const TexBox* tex_box,
    ConversionContext* ctx
);

// ============================================================================
// MathBox to TexBox Conversion (for editing/selection)
// ============================================================================

// Convert a radiant::MathBox back to a tex::TexBox
// Used when editing operations need to work on the TeX representation
TexBox* convert_math_box_to_tex(
    const radiant::MathBox* math_box,
    Arena* arena,
    RadiantFontProvider* font_provider
);

// ============================================================================
// Style Conversion
// ============================================================================

// Convert tex::MathStyle to radiant::MathStyle
inline radiant::MathStyle tex_to_radiant_style(MathStyle tex_style) {
    switch (tex_style) {
        case MathStyle::Display:          return radiant::MathStyle::Display;
        case MathStyle::DisplayCramped:   return radiant::MathStyle::DisplayCramped;
        case MathStyle::Text:             return radiant::MathStyle::Text;
        case MathStyle::TextCramped:      return radiant::MathStyle::TextCramped;
        case MathStyle::Script:           return radiant::MathStyle::Script;
        case MathStyle::ScriptCramped:    return radiant::MathStyle::ScriptCramped;
        case MathStyle::Scriptscript:     return radiant::MathStyle::Scriptscript;
        case MathStyle::ScriptscriptCramped: return radiant::MathStyle::ScriptscriptCramped;
        default:                          return radiant::MathStyle::Text;
    }
}

// Convert radiant::MathStyle to tex::MathStyle
inline MathStyle radiant_to_tex_style(radiant::MathStyle radiant_style) {
    switch (radiant_style) {
        case radiant::MathStyle::Display:          return MathStyle::Display;
        case radiant::MathStyle::DisplayCramped:   return MathStyle::DisplayCramped;
        case radiant::MathStyle::Text:             return MathStyle::Text;
        case radiant::MathStyle::TextCramped:      return MathStyle::TextCramped;
        case radiant::MathStyle::Script:           return MathStyle::Script;
        case radiant::MathStyle::ScriptCramped:    return MathStyle::ScriptCramped;
        case radiant::MathStyle::Scriptscript:     return MathStyle::Scriptscript;
        case radiant::MathStyle::ScriptscriptCramped: return MathStyle::ScriptscriptCramped;
        default:                                   return MathStyle::Text;
    }
}

// ============================================================================
// Type Conversion
// ============================================================================

// Convert tex::AtomType to radiant::MathBoxType
inline radiant::MathBoxType tex_to_radiant_type(AtomType tex_type) {
    switch (tex_type) {
        case AtomType::Ord:    return radiant::MathBoxType::Ord;
        case AtomType::Op:     return radiant::MathBoxType::Op;
        case AtomType::Bin:    return radiant::MathBoxType::Bin;
        case AtomType::Rel:    return radiant::MathBoxType::Rel;
        case AtomType::Open:   return radiant::MathBoxType::Open;
        case AtomType::Close:  return radiant::MathBoxType::Close;
        case AtomType::Punct:  return radiant::MathBoxType::Punct;
        case AtomType::Inner:  return radiant::MathBoxType::Inner;
        case AtomType::Acc:    return radiant::MathBoxType::Ord;  // accents treated as ord
        case AtomType::Rad:    return radiant::MathBoxType::Inner;
        case AtomType::Vcent:  return radiant::MathBoxType::Ord;
        case AtomType::Over:   return radiant::MathBoxType::Ord;
        case AtomType::Under:  return radiant::MathBoxType::Ord;
        default:               return radiant::MathBoxType::Ord;
    }
}

// Convert radiant::MathBoxType to tex::AtomType
inline AtomType radiant_to_tex_type(radiant::MathBoxType radiant_type) {
    switch (radiant_type) {
        case radiant::MathBoxType::Ord:    return AtomType::Ord;
        case radiant::MathBoxType::Op:     return AtomType::Op;
        case radiant::MathBoxType::Bin:    return AtomType::Bin;
        case radiant::MathBoxType::Rel:    return AtomType::Rel;
        case radiant::MathBoxType::Open:   return AtomType::Open;
        case radiant::MathBoxType::Close:  return AtomType::Close;
        case radiant::MathBoxType::Punct:  return AtomType::Punct;
        case radiant::MathBoxType::Inner:  return AtomType::Inner;
        case radiant::MathBoxType::Ignore: return AtomType::Ord;
        case radiant::MathBoxType::Lift:   return AtomType::Ord;
        default:                           return AtomType::Ord;
    }
}

// ============================================================================
// Radiant Math Layout Integration
// ============================================================================

// Layout a math node using the TeX typesetting engine instead of Radiant's
// existing math layout. This provides higher-quality typesetting.
radiant::MathBox* layout_math_with_tex(
    Item math_node,           // Lambda math node tree
    radiant::MathContext* ctx, // Radiant math context
    Arena* arena,             // For allocation
    RadiantFontProvider* font_provider
);

// ============================================================================
// Rendering Utilities
// ============================================================================

// Render a tex::TexBox directly using Radiant's rendering context
// (alternative to conversion when we just need to draw)
void render_tex_box(
    const TexBox* box,
    float x,
    float y,
    radiant::RenderContext* render_ctx,
    RadiantFontProvider* font_provider
);

// Calculate bounding box for a tex::TexBox
radiant::Rect tex_box_bounds(const TexBox* box);

// ============================================================================
// Debug Utilities
// ============================================================================

// Dump a tex::TexBox tree for debugging
void dump_tex_box(const TexBox* box, int indent = 0);

// Compare tex::TexBox and radiant::MathBox trees for debugging
bool compare_box_trees(const TexBox* tex, const radiant::MathBox* radiant, float tolerance = 0.5f);

} // namespace tex

#endif // TEX_RADIANT_BRIDGE_HPP
