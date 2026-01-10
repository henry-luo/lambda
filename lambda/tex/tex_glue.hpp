// tex_glue.hpp - TeX Glue and related spacing structures
//
// Glue is the heart of TeX's flexible spacing system. It consists of:
// - Natural size: the preferred size
// - Stretch: how much it can grow (with order for infinite stretch)
// - Shrink: how much it can shrink (with order for infinite shrink)
//
// Reference: TeXBook Chapter 12

#ifndef LAMBDA_TEX_GLUE_HPP
#define LAMBDA_TEX_GLUE_HPP

#include <cmath>
#include <algorithm>

namespace tex {

// ============================================================================
// Unit Conversions
// ============================================================================

// TeX uses points as the base unit (72.27 per inch)
// We convert to CSS pixels (96 per inch) for rendering

constexpr float PT_PER_INCH = 72.27f;       // TeX points per inch
constexpr float BP_PER_INCH = 72.0f;        // Big points (PDF/PostScript)
constexpr float CSS_PX_PER_INCH = 96.0f;    // CSS reference pixels per inch

constexpr float PT_TO_PX = CSS_PX_PER_INCH / PT_PER_INCH;  // ~1.3281
constexpr float PX_TO_PT = PT_PER_INCH / CSS_PX_PER_INCH;  // ~0.7528
constexpr float BP_TO_PX = CSS_PX_PER_INCH / BP_PER_INCH;  // ~1.3333

// Math unit (mu) = 1/18 em
constexpr float MU_PER_EM = 18.0f;

inline float pt_to_px(float pt) { return pt * PT_TO_PX; }
inline float px_to_pt(float px) { return px * PX_TO_PT; }
inline float bp_to_px(float bp) { return bp * BP_TO_PX; }
inline float mu_to_px(float mu, float em_size) { return mu / MU_PER_EM * em_size; }

// Convert various TeX units to CSS pixels
inline float tex_unit_to_px(float value, const char* unit, float em_size, float ex_size) {
    if (!unit || unit[0] == '\0') return value; // assume pixels

    if (unit[0] == 'p' && unit[1] == 't') return pt_to_px(value);
    if (unit[0] == 'b' && unit[1] == 'p') return bp_to_px(value);
    if (unit[0] == 'i' && unit[1] == 'n') return value * CSS_PX_PER_INCH;
    if (unit[0] == 'c' && unit[1] == 'm') return value * CSS_PX_PER_INCH / 2.54f;
    if (unit[0] == 'm' && unit[1] == 'm') return value * CSS_PX_PER_INCH / 25.4f;
    if (unit[0] == 'e' && unit[1] == 'm') return value * em_size;
    if (unit[0] == 'e' && unit[1] == 'x') return value * ex_size;
    if (unit[0] == 'p' && unit[1] == 'c') return pt_to_px(value * 12.0f); // pica = 12pt
    if (unit[0] == 'd' && unit[1] == 'd') return pt_to_px(value * 1.07f); // didot
    if (unit[0] == 'c' && unit[1] == 'c') return pt_to_px(value * 12.84f); // cicero
    if (unit[0] == 's' && unit[1] == 'p') return pt_to_px(value / 65536.0f); // scaled point
    if (unit[0] == 'm' && unit[1] == 'u') return mu_to_px(value, em_size);
    if (unit[0] == 'p' && unit[1] == 'x') return value; // CSS pixel

    return value; // unknown unit, assume pixels
}

// ============================================================================
// Glue Order - For infinite stretch/shrink
// ============================================================================

// Glue can have "infinite" stretch/shrink at different orders:
// - 0: finite (normal)
// - 1: fil  (first level of infinity)
// - 2: fill (second level, infinitely larger than fil)
// - 3: filll (third level, infinitely larger than fill)
enum class GlueOrder : uint8_t {
    Normal = 0,     // Finite stretch/shrink
    Fil = 1,        // \hfil, \vfil
    Fill = 2,       // \hfill, \vfill
    Filll = 3,      // \hfilll (rarely used)
};

// ============================================================================
// Glue Structure
// ============================================================================

struct Glue {
    float space;            // Natural size (CSS pixels)
    float stretch;          // Stretch amount
    float shrink;           // Shrink amount
    GlueOrder stretch_order; // Order of stretch infinity
    GlueOrder shrink_order;  // Order of shrink infinity

    // Default constructor - zero glue
    Glue() : space(0), stretch(0), shrink(0),
             stretch_order(GlueOrder::Normal), shrink_order(GlueOrder::Normal) {}

    // Fixed glue (no stretch or shrink)
    static Glue fixed(float s) {
        Glue g;
        g.space = s;
        return g;
    }

    // Flexible glue
    static Glue flexible(float s, float st, float sh) {
        Glue g;
        g.space = s;
        g.stretch = st;
        g.shrink = sh;
        return g;
    }

    // Infinite stretch glue
    static Glue fil(float s, float st_amount = 1.0f) {
        Glue g;
        g.space = s;
        g.stretch = st_amount;
        g.stretch_order = GlueOrder::Fil;
        return g;
    }

    static Glue fill(float s, float st_amount = 1.0f) {
        Glue g;
        g.space = s;
        g.stretch = st_amount;
        g.stretch_order = GlueOrder::Fill;
        return g;
    }

    static Glue filll(float s, float st_amount = 1.0f) {
        Glue g;
        g.space = s;
        g.stretch = st_amount;
        g.stretch_order = GlueOrder::Filll;
        return g;
    }

    // Glue arithmetic
    Glue operator+(const Glue& other) const {
        Glue result;
        result.space = space + other.space;

        // Add stretch (higher order dominates)
        if (stretch_order == other.stretch_order) {
            result.stretch = stretch + other.stretch;
            result.stretch_order = stretch_order;
        } else if (stretch_order > other.stretch_order) {
            result.stretch = stretch;
            result.stretch_order = stretch_order;
        } else {
            result.stretch = other.stretch;
            result.stretch_order = other.stretch_order;
        }

        // Add shrink (higher order dominates)
        if (shrink_order == other.shrink_order) {
            result.shrink = shrink + other.shrink;
            result.shrink_order = shrink_order;
        } else if (shrink_order > other.shrink_order) {
            result.shrink = shrink;
            result.shrink_order = shrink_order;
        } else {
            result.shrink = other.shrink;
            result.shrink_order = other.shrink_order;
        }

        return result;
    }

    Glue operator*(float scale) const {
        Glue result = *this;
        result.space *= scale;
        result.stretch *= scale;
        result.shrink *= scale;
        return result;
    }

    // Check if glue can stretch/shrink
    bool can_stretch() const { return stretch > 0; }
    bool can_shrink() const { return shrink > 0; }
    bool is_finite() const {
        return stretch_order == GlueOrder::Normal && shrink_order == GlueOrder::Normal;
    }
};

// ============================================================================
// Standard LaTeX Glues (in CSS pixels, for 10pt base)
// ============================================================================

// Interword space (from font metrics, these are typical values)
inline Glue interword_space(float em) {
    return Glue::flexible(
        em * 0.333f,    // 1/3 em natural
        em * 0.166f,    // 1/6 em stretch
        em * 0.111f     // 1/9 em shrink
    );
}

// Math spacing (thin, med, thick in mu)
inline Glue thin_muskip(float em) {
    return Glue::fixed(mu_to_px(3.0f, em));
}

inline Glue med_muskip(float em) {
    return Glue::flexible(
        mu_to_px(4.0f, em),     // 4mu natural
        mu_to_px(2.0f, em),     // 2mu stretch
        mu_to_px(4.0f, em)      // 4mu shrink
    );
}

inline Glue thick_muskip(float em) {
    return Glue::flexible(
        mu_to_px(5.0f, em),     // 5mu natural
        mu_to_px(5.0f, em),     // 5mu stretch
        0                        // no shrink
    );
}

// Paragraph spacing
inline Glue parskip_default() {
    return Glue::flexible(0, pt_to_px(1.0f), 0);  // 0pt plus 1pt
}

inline Glue baselineskip(float size) {
    return Glue::fixed(size * 1.2f);  // 12pt for 10pt font
}

// Fill glues
inline Glue hfil() { return Glue::fil(0); }
inline Glue hfill() { return Glue::fill(0); }
inline Glue hfilneg() {
    Glue g = Glue::fil(0, -1.0f);
    return g;
}

inline Glue vfil() { return Glue::fil(0); }
inline Glue vfill() { return Glue::fill(0); }

// Standard skips
inline Glue smallskip() { return Glue::flexible(pt_to_px(3.0f), pt_to_px(1.0f), pt_to_px(1.0f)); }
inline Glue medskip() { return Glue::flexible(pt_to_px(6.0f), pt_to_px(2.0f), pt_to_px(2.0f)); }
inline Glue bigskip() { return Glue::flexible(pt_to_px(12.0f), pt_to_px(4.0f), pt_to_px(4.0f)); }

// ============================================================================
// Glue Setting - How glue is set in a box
// ============================================================================

struct GlueSetInfo {
    float ratio;            // Stretch or shrink ratio
    GlueOrder order;        // Which order of infinity was used
    bool is_stretching;     // True if stretching, false if shrinking

    // Compute the actual size of a glue given this setting
    float compute_size(const Glue& glue) const {
        float size = glue.space;
        if (is_stretching) {
            if (glue.stretch_order == order) {
                size += ratio * glue.stretch;
            }
        } else {
            if (glue.shrink_order == order) {
                size -= ratio * glue.shrink;
            }
        }
        return size;
    }
};

// ============================================================================
// Badness Calculation - TeXBook Chapter 14
// ============================================================================

// Badness measures how much glue is stretched or shrunk
// 0 = perfect, 100 = maximum acceptable, 10000 = infinitely bad

inline int compute_badness(float excess, float total_stretch_or_shrink) {
    if (total_stretch_or_shrink <= 0) {
        return (excess > 0.1f) ? 10000 : 0;  // Infinitely bad if excess with no flexibility
    }

    float ratio = std::abs(excess) / total_stretch_or_shrink;

    // TeXBook formula: badness = 100 * ratio^3
    // But capped at 10000
    if (ratio > 1.0f) {
        return 10000;  // Overfull or underfull
    }

    int badness = (int)(100.0f * ratio * ratio * ratio + 0.5f);
    return std::min(badness, 10000);
}

// ============================================================================
// Penalty Values - Standard values from TeXBook
// ============================================================================

namespace penalty {
    constexpr int FORCE_BREAK = -10000;     // Force a break here
    constexpr int FORBID_BREAK = 10000;     // Never break here
    constexpr int HYPHEN_PENALTY = 50;      // Default hyphenation penalty
    constexpr int EX_HYPHEN_PENALTY = 50;   // Explicit hyphen penalty
    constexpr int BIN_OP_PENALTY = 700;     // After binary operator
    constexpr int REL_PENALTY = 500;        // After relation
    constexpr int CLUB_PENALTY = 150;       // Widow/club line penalty
    constexpr int WIDOW_PENALTY = 150;
    constexpr int DISPLAY_WIDOW_PENALTY = 50;
    constexpr int BROKEN_PENALTY = 100;     // After hyphenated line
    constexpr int PRE_DISPLAY_PENALTY = 10000;  // Before display math
    constexpr int POST_DISPLAY_PENALTY = 0;     // After display math
}

// ============================================================================
// Demerits Calculation - TeXBook Chapter 14
// ============================================================================

inline int compute_demerits(int badness, int penalty, int line_penalty, bool flagged) {
    int demerits;

    if (penalty >= 0) {
        demerits = (line_penalty + badness) * (line_penalty + badness) + penalty * penalty;
    } else if (penalty > penalty::FORCE_BREAK) {
        demerits = (line_penalty + badness) * (line_penalty + badness) - penalty * penalty;
    } else {
        demerits = (line_penalty + badness) * (line_penalty + badness);
    }

    // Additional demerits for consecutive flagged lines (hyphenated)
    if (flagged) {
        demerits += 10000;  // double_hyphen_demerits
    }

    return demerits;
}

} // namespace tex

#endif // LAMBDA_TEX_GLUE_HPP
