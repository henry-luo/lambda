// math_context.hpp - Math typesetting context and font metrics
//
// Provides MathContext for managing math style state during layout,
// and MathFontMetrics for TeX-compatible font metric constants.
//
// Based on TeXBook Appendix G and MathLive's architecture.

#ifndef RADIANT_MATH_CONTEXT_HPP
#define RADIANT_MATH_CONTEXT_HPP

#include "view.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/math_node.hpp"

namespace radiant {

// ============================================================================
// Math Style (TeXBook styles)
// ============================================================================

enum class MathStyle {
    Display = 0,          // D  - displaystyle (large operators, wide fractions)
    DisplayCramped = 1,   // D' - displaystyle cramped (exponents lowered)
    Text = 2,             // T  - textstyle (inline math)
    TextCramped = 3,      // T' - textstyle cramped
    Script = 4,           // S  - scriptstyle (sub/superscripts)
    ScriptCramped = 5,    // S' - scriptstyle cramped
    ScriptScript = 6,     // SS - scriptscriptstyle (2nd level scripts)
    ScriptScriptCramped = 7, // SS' - scriptscriptstyle cramped
};

// ============================================================================
// Math Font Metrics (TeXBook sigma/xi constants)
// ============================================================================

struct MathFontMetrics {
    // All dimensions in em (relative to current font size)

    // Basic metrics
    float x_height;           // σ5 - height of lowercase 'x'
    float quad;               // σ6 - 1em width
    float axis_height;        // σ22 - math axis height (center of operations)

    // Fraction positioning
    float num1;               // σ8 - numerator shift (display style)
    float num2;               // σ9 - numerator shift (text style, with bar)
    float num3;               // σ10 - numerator shift (text style, no bar)
    float denom1;             // σ11 - denominator shift (display style)
    float denom2;             // σ12 - denominator shift (text style)

    // Super/subscript positioning
    float sup1;               // σ13 - superscript shift (display style)
    float sup2;               // σ14 - superscript shift (text style)
    float sup3;               // σ15 - superscript shift (cramped style)
    float sub1;               // σ16 - subscript shift
    float sub2;               // σ17 - subscript shift (with superscript present)
    float sup_drop;           // σ18 - superscript baseline drop
    float sub_drop;           // σ19 - subscript baseline drop

    // Delimiter sizing
    float delim1;             // σ20 - delimiter size (display style)
    float delim2;             // σ21 - delimiter size (text style)

    // Rules and spacing
    float default_rule_thickness;  // ξ8 - fraction bar thickness
    float big_op_spacing1;    // ξ9 - space above/below big op limits
    float big_op_spacing2;    // ξ10 - minimum space above big op limit
    float big_op_spacing3;    // ξ11 - minimum space below big op limit
    float big_op_spacing4;    // ξ12 - extra space above limits
    float big_op_spacing5;    // ξ13 - extra space below limits

    // Radical parameters
    float radical_vertical_gap;      // gap between radicand and rule
    float radical_display_style_vertical_gap;
    float radical_rule_thickness;
    float radical_extra_ascender;
    float radical_kern_before_degree;
    float radical_kern_after_degree;
    float radical_degree_bottom_raise_percent;

    // Script factors
    float script_percent_scale_down;       // ~70%
    float script_script_percent_scale_down; // ~50%

    // Default constructor with typical values for Computer Modern
    MathFontMetrics() {
        // Basic metrics
        x_height = 0.430;          // typical x-height
        quad = 1.0;
        axis_height = 0.250;       // math axis at 0.25em above baseline

        // Fraction positioning (following TeX defaults)
        num1 = 0.676;              // display numerator shift up
        num2 = 0.394;              // text numerator shift up
        num3 = 0.444;              // atop numerator shift up
        denom1 = 0.686;            // display denominator shift down
        denom2 = 0.345;            // text denominator shift down

        // Super/subscript (based on TeX constants)
        sup1 = 0.413;              // display superscript
        sup2 = 0.363;              // text superscript
        sup3 = 0.289;              // cramped superscript
        sub1 = 0.150;              // subscript shift down
        sub2 = 0.247;              // subscript with superscript
        sup_drop = 0.386;          // sup baseline drop
        sub_drop = 0.050;          // sub baseline drop

        // Delimiters
        delim1 = 2.390;            // display delimiter
        delim2 = 1.010;            // text delimiter

        // Rules
        default_rule_thickness = 0.04;
        big_op_spacing1 = 0.111;
        big_op_spacing2 = 0.167;
        big_op_spacing3 = 0.200;
        big_op_spacing4 = 0.600;
        big_op_spacing5 = 0.100;

        // Radical
        radical_vertical_gap = 0.05;
        radical_display_style_vertical_gap = 0.10;
        radical_rule_thickness = 0.04;
        radical_extra_ascender = 0.10;
        radical_kern_before_degree = 0.277;
        radical_kern_after_degree = -0.5;
        radical_degree_bottom_raise_percent = 0.65;

        // Script factors
        script_percent_scale_down = 0.70;
        script_script_percent_scale_down = 0.50;
    }
};

// Pre-computed metrics tables (one for each size class)
extern const MathFontMetrics MATH_METRICS_NORMAL;
extern const MathFontMetrics MATH_METRICS_SCRIPT;
extern const MathFontMetrics MATH_METRICS_SCRIPTSCRIPT;

// ============================================================================
// MathContext - Layout context for math typesetting
// ============================================================================

struct MathContext {
    MathContext* parent;

    // Current style
    MathStyle style;

    // Font settings
    float base_font_size;      // base font size in pixels (from parent context)
    const char* font_family;   // math font family name

    // Colors
    uint32_t color;
    uint32_t background_color;

    // UI context for font loading
    UiContext* ui_context;

    // Pool for memory allocation
    Pool* pool;

    // ========================================================================
    // Constructors
    // ========================================================================

    MathContext()
        : parent(nullptr), style(MathStyle::Text),
          base_font_size(16.0f), font_family("Latin Modern Math"),
          color(0x000000FF), background_color(0x00000000),
          ui_context(nullptr), pool(nullptr) {}

    MathContext(UiContext* uicon, Pool* p, float font_size, bool is_display = false)
        : parent(nullptr),
          style(is_display ? MathStyle::Display : MathStyle::Text),
          base_font_size(font_size),
          font_family("Latin Modern Math"),
          color(0x000000FF), background_color(0x00000000),
          ui_context(uicon), pool(p) {}

    // ========================================================================
    // Style queries
    // ========================================================================

    // Get scaling factor for current style
    float scaling_factor() const {
        const MathFontMetrics& m = metrics();
        switch (style) {
            case MathStyle::Display:
            case MathStyle::DisplayCramped:
            case MathStyle::Text:
            case MathStyle::TextCramped:
                return 1.0f;
            case MathStyle::Script:
            case MathStyle::ScriptCramped:
                return m.script_percent_scale_down;
            case MathStyle::ScriptScript:
            case MathStyle::ScriptScriptCramped:
                return m.script_script_percent_scale_down;
        }
        return 1.0f;
    }

    // Get actual font size for current style
    float font_size() const {
        return base_font_size * scaling_factor();
    }

    // Is this a display style?
    bool is_display_style() const {
        return style == MathStyle::Display || style == MathStyle::DisplayCramped;
    }

    // Is this a cramped style?
    bool is_cramped() const {
        return (int)style % 2 == 1; // odd styles are cramped
    }

    // Is this a tight (script/scriptscript) style?
    bool is_tight() const {
        return (int)style >= (int)MathStyle::Script;
    }

    // ========================================================================
    // Style transitions (TeXBook rules)
    // ========================================================================

    // Style for superscripts (TeXBook Rule 18a)
    MathStyle sup_style() const {
        switch (style) {
            case MathStyle::Display:
            case MathStyle::Text:
                return MathStyle::Script;
            case MathStyle::DisplayCramped:
            case MathStyle::TextCramped:
                return MathStyle::ScriptCramped;
            case MathStyle::Script:
            case MathStyle::ScriptScript:
                return MathStyle::ScriptScript;
            case MathStyle::ScriptCramped:
            case MathStyle::ScriptScriptCramped:
                return MathStyle::ScriptScriptCramped;
        }
        return MathStyle::Script;
    }

    // Style for subscripts
    MathStyle sub_style() const {
        // Subscripts are always cramped
        switch (style) {
            case MathStyle::Display:
            case MathStyle::DisplayCramped:
            case MathStyle::Text:
            case MathStyle::TextCramped:
                return MathStyle::ScriptCramped;
            case MathStyle::Script:
            case MathStyle::ScriptCramped:
            case MathStyle::ScriptScript:
            case MathStyle::ScriptScriptCramped:
                return MathStyle::ScriptScriptCramped;
        }
        return MathStyle::ScriptCramped;
    }

    // Style for fraction numerator (TeXBook Rule 15)
    MathStyle frac_num_style() const {
        switch (style) {
            case MathStyle::Display:
                return MathStyle::Text;
            case MathStyle::DisplayCramped:
                return MathStyle::TextCramped;
            default:
                return sup_style(); // use superscript style
        }
    }

    // Style for fraction denominator
    MathStyle frac_den_style() const {
        switch (style) {
            case MathStyle::Display:
            case MathStyle::DisplayCramped:
                return MathStyle::TextCramped;
            default:
                return sub_style(); // use subscript style
        }
    }

    // Get cramped version of current style
    MathStyle cramped_style() const {
        if (is_cramped()) return style;
        return (MathStyle)((int)style + 1);
    }

    // ========================================================================
    // Font metrics access
    // ========================================================================

    const MathFontMetrics& metrics() const {
        // Return appropriate metrics based on style
        if ((int)style >= (int)MathStyle::ScriptScript) {
            return MATH_METRICS_SCRIPTSCRIPT;
        } else if ((int)style >= (int)MathStyle::Script) {
            return MATH_METRICS_SCRIPT;
        }
        return MATH_METRICS_NORMAL;
    }

    // ========================================================================
    // Create child context with new style
    // ========================================================================

    MathContext derive(MathStyle new_style) const {
        MathContext child;
        child.parent = const_cast<MathContext*>(this);
        child.style = new_style;
        child.base_font_size = base_font_size;
        child.font_family = font_family;
        child.color = color;
        child.background_color = background_color;
        child.ui_context = ui_context;
        child.pool = pool;
        return child;
    }

    // Convenience: derive with superscript style
    MathContext derive_sup() const { return derive(sup_style()); }

    // Convenience: derive with subscript style
    MathContext derive_sub() const { return derive(sub_style()); }

    // Convenience: derive with fraction numerator style
    MathContext derive_frac_num() const { return derive(frac_num_style()); }

    // Convenience: derive with fraction denominator style
    MathContext derive_frac_den() const { return derive(frac_den_style()); }
};

} // namespace radiant

#endif // RADIANT_MATH_CONTEXT_HPP
