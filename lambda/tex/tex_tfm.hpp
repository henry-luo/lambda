// tex_tfm.hpp - TeX Font Metrics (TFM) Parser
//
// Parses TFM files to extract character metrics, ligature tables,
// and kerning information needed for typesetting.
//
// TFM format reference: TeX: The Program, Part 30
// Also see: https://www.tug.org/TUGboat/tb06-1/tb11knut.pdf

#ifndef LAMBDA_TEX_TFM_HPP
#define LAMBDA_TEX_TFM_HPP

#include "lib/arena.h"
#include <cstdint>
#include <cstdio>

namespace tex {

// ============================================================================
// TFM Constants
// ============================================================================

// Maximum values from TeX
constexpr int TFM_MAX_CHARS = 256;
constexpr int TFM_MAX_WIDTHS = 256;
constexpr int TFM_MAX_HEIGHTS = 16;
constexpr int TFM_MAX_DEPTHS = 16;
constexpr int TFM_MAX_ITALICS = 64;
constexpr int TFM_MAX_KERNS = 256;
constexpr int TFM_MAX_PARAMS = 30;

// Font parameter indices (fontdimen)
constexpr int TFM_PARAM_SLANT = 1;
constexpr int TFM_PARAM_SPACE = 2;
constexpr int TFM_PARAM_SPACE_STRETCH = 3;
constexpr int TFM_PARAM_SPACE_SHRINK = 4;
constexpr int TFM_PARAM_X_HEIGHT = 5;
constexpr int TFM_PARAM_QUAD = 6;
constexpr int TFM_PARAM_EXTRA_SPACE = 7;

// Math symbol font parameters (fontdimen 8-22)
constexpr int TFM_PARAM_NUM1 = 8;
constexpr int TFM_PARAM_NUM2 = 9;
constexpr int TFM_PARAM_NUM3 = 10;
constexpr int TFM_PARAM_DENOM1 = 11;
constexpr int TFM_PARAM_DENOM2 = 12;
constexpr int TFM_PARAM_SUP1 = 13;
constexpr int TFM_PARAM_SUP2 = 14;
constexpr int TFM_PARAM_SUP3 = 15;
constexpr int TFM_PARAM_SUB1 = 16;
constexpr int TFM_PARAM_SUB2 = 17;
constexpr int TFM_PARAM_SUP_DROP = 18;
constexpr int TFM_PARAM_SUB_DROP = 19;
constexpr int TFM_PARAM_DELIM1 = 20;
constexpr int TFM_PARAM_DELIM2 = 21;
constexpr int TFM_PARAM_AXIS_HEIGHT = 22;

// Math extension font parameters
constexpr int TFM_PARAM_DEFAULT_RULE = 8;

// ============================================================================
// Ligature/Kern Program Commands
// ============================================================================

struct LigKernStep {
    uint8_t skip_byte;      // >128 means this is kern, else lig
    uint8_t next_char;      // Character to match
    uint8_t op_byte;        // Operation (ligature or kern index)
    uint8_t remainder;      // Ligature char or kern table offset
};

// ============================================================================
// Character Info
// ============================================================================

struct TFMCharInfo {
    uint8_t width_index;
    uint8_t height_index;   // 4 bits actually
    uint8_t depth_index;    // 4 bits
    uint8_t italic_index;   // 6 bits
    uint8_t tag;            // 2 bits
    uint8_t remainder;      // 8 bits (ligkern index or next larger char)
};

// Tag values
constexpr int TFM_TAG_NONE = 0;
constexpr int TFM_TAG_LIGKERN = 1;
constexpr int TFM_TAG_CHAIN = 2;    // Next larger character
constexpr int TFM_TAG_EXTENS = 3;   // Extensible recipe

// ============================================================================
// Extensible Recipe (for large delimiters)
// ============================================================================

struct ExtensibleRecipe {
    uint8_t top;            // Top piece character code (0 = none)
    uint8_t mid;            // Middle piece character code (0 = none)
    uint8_t bot;            // Bottom piece character code (0 = none)
    uint8_t rep;            // Repeated piece character code
};

// ============================================================================
// TFM File Data Structure
// ============================================================================

struct TFMFont {
    // Identification
    const char* name;       // Font name (e.g., "cmr10")
    uint32_t checksum;
    float design_size;      // In points

    // Character range
    int first_char;         // First character code
    int last_char;          // Last character code

    // Tables (arena-allocated)
    TFMCharInfo* char_info; // Character info [last_char - first_char + 1]
    float* widths;          // Width table [nw]
    float* heights;         // Height table [nh]
    float* depths;          // Depth table [nd]
    float* italics;         // Italic correction table [ni]
    float* kerns;           // Kern table [nk]
    float* params;          // Font parameters [np]
    LigKernStep* lig_kern;  // Ligature/kern program [nl]
    ExtensibleRecipe* extensibles; // Extensible recipes [ne]

    int nw, nh, nd, ni, nk, np, nl, ne;

    // Cached computed values
    float space;            // Normal interword space
    float space_stretch;
    float space_shrink;
    float x_height;
    float quad;             // 1em width

    // ========================================
    // Query functions
    // ========================================

    // Check if character exists
    bool has_char(int c) const {
        return c >= first_char && c <= last_char &&
               char_info[c - first_char].width_index != 0;
    }

    // Get character metrics (in design units, multiply by size/design_size)
    float char_width(int c) const;
    float char_height(int c) const;
    float char_depth(int c) const;
    float char_italic(int c) const;

    // Get kerning between two characters (0 if none)
    float get_kern(int left, int right) const;

    // Get ligature for character pair (0 if none)
    int get_ligature(int left, int right) const;

    // Get next larger character (for delimiters)
    int get_next_larger(int c) const;

    // Get extensible recipe (for delimiters)
    const ExtensibleRecipe* get_extensible(int c) const;

    // Get font parameter
    float get_param(int index) const {
        if (index < 1 || index > np) return 0;
        return params[index - 1];
    }

    // ========================================
    // Scaled metrics (for actual font size)
    // ========================================

    float scaled_width(int c, float size_pt) const {
        return char_width(c) * size_pt / design_size;
    }

    float scaled_height(int c, float size_pt) const {
        return char_height(c) * size_pt / design_size;
    }

    float scaled_depth(int c, float size_pt) const {
        return char_depth(c) * size_pt / design_size;
    }

    float scaled_italic(int c, float size_pt) const {
        return char_italic(c) * size_pt / design_size;
    }

    float scaled_kern(int l, int r, float size_pt) const {
        return get_kern(l, r) * size_pt / design_size;
    }

    float scaled_param(int index, float size_pt) const {
        return get_param(index) * size_pt / design_size;
    }
};

// ============================================================================
// TFM Loading Functions
// ============================================================================

// Load a TFM file from disk
TFMFont* load_tfm_file(const char* path, Arena* arena);

// Load a TFM font by name (searches standard paths)
TFMFont* load_tfm_by_name(const char* name, Arena* arena);

// ============================================================================
// Built-in Font Metrics (Fallback)
// ============================================================================

// Get built-in CMR10 metrics (no file needed)
TFMFont* get_builtin_cmr10(Arena* arena);

// Get built-in CMMI10 metrics (math italic)
TFMFont* get_builtin_cmmi10(Arena* arena);

// Get built-in CMSY10 metrics (math symbols)
TFMFont* get_builtin_cmsy10(Arena* arena);

// Get built-in CMEX10 metrics (math extensions)
TFMFont* get_builtin_cmex10(Arena* arena);

// ============================================================================
// Font Manager
// ============================================================================

struct TFMFontManager {
    Arena* arena;
    TFMFont** fonts;
    const char** font_names;
    int font_count;
    int font_capacity;

    // Get or load a font
    TFMFont* get_font(const char* name);

    // Register a font with a name
    void register_font(const char* name, TFMFont* font);
};

// Create a font manager
TFMFontManager* create_font_manager(Arena* arena);

} // namespace tex

#endif // LAMBDA_TEX_TFM_HPP
