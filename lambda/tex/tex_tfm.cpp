// tex_tfm.cpp - TFM File Parser Implementation
//
// Parses TeX Font Metrics files and provides built-in fallback metrics
// for Computer Modern fonts.

#include "tex_tfm.hpp"
#include "lib/log.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

namespace tex {

// ============================================================================
// TFMFont Query Implementation
// ============================================================================

float TFMFont::char_width(int c) const {
    if (!has_char(c)) return 0;
    int idx = char_info[c - first_char].width_index;
    return (idx < nw) ? widths[idx] : 0;
}

float TFMFont::char_height(int c) const {
    if (!has_char(c)) return 0;
    int idx = char_info[c - first_char].height_index;
    return (idx < nh) ? heights[idx] : 0;
}

float TFMFont::char_depth(int c) const {
    if (!has_char(c)) return 0;
    int idx = char_info[c - first_char].depth_index;
    return (idx < nd) ? depths[idx] : 0;
}

float TFMFont::char_italic(int c) const {
    if (!has_char(c)) return 0;
    int idx = char_info[c - first_char].italic_index;
    return (idx < ni) ? italics[idx] : 0;
}

float TFMFont::get_kern(int left, int right) const {
    if (!has_char(left)) return 0;

    const TFMCharInfo& info = char_info[left - first_char];
    if (info.tag != TFM_TAG_LIGKERN) return 0;

    // Follow the ligkern program
    int i = info.remainder;
    if (i >= nl) return 0;

    // Handle possible indirection
    if (lig_kern[i].skip_byte > 128) {
        // Indirect reference
        i = 256 * lig_kern[i].op_byte + lig_kern[i].remainder;
        if (i >= nl) return 0;
    }

    while (i < nl) {
        const LigKernStep& step = lig_kern[i];

        if (step.next_char == right) {
            if (step.op_byte >= 128) {
                // This is a kern (op_byte >= 128 indicates kern operation)
                int kern_idx = 256 * (step.op_byte - 128) + step.remainder;
                if (kern_idx < nk) {
                    return kerns[kern_idx];
                }
            }
            return 0;  // Ligature, not kern
        }

        if (step.skip_byte >= 128) break;  // End of list
        i += step.skip_byte + 1;
    }

    return 0;
}

int TFMFont::get_ligature(int left, int right) const {
    if (!has_char(left)) return 0;

    const TFMCharInfo& info = char_info[left - first_char];
    if (info.tag != TFM_TAG_LIGKERN) return 0;

    int i = info.remainder;
    if (i >= nl) return 0;

    // Handle indirection
    if (lig_kern[i].skip_byte > 128) {
        i = 256 * lig_kern[i].op_byte + lig_kern[i].remainder;
        if (i >= nl) return 0;
    }

    while (i < nl) {
        const LigKernStep& step = lig_kern[i];

        if (step.next_char == right) {
            if (step.skip_byte <= 128) {
                // This is a ligature
                // op_byte determines ligature type, remainder is result char
                return step.remainder;
            }
            return 0;  // Kern, not ligature
        }

        if (step.skip_byte >= 128) break;
        i += step.skip_byte + 1;
    }

    return 0;
}

int TFMFont::get_next_larger(int c) const {
    if (!has_char(c)) return 0;

    const TFMCharInfo& info = char_info[c - first_char];
    if (info.tag != TFM_TAG_CHAIN) return 0;

    return info.remainder;
}

const ExtensibleRecipe* TFMFont::get_extensible(int c) const {
    if (!has_char(c)) return nullptr;

    const TFMCharInfo& info = char_info[c - first_char];
    if (info.tag != TFM_TAG_EXTENS) return nullptr;
    if (info.remainder >= ne) return nullptr;

    return &extensibles[info.remainder];
}

bool TFMFont::has_extensible(int c) const {
    if (!has_char(c)) return false;
    const TFMCharInfo& info = char_info[c - first_char];
    return info.tag == TFM_TAG_EXTENS && info.remainder < ne;
}

// ============================================================================
// TFM File Loading
// ============================================================================

// Read a 4-byte big-endian integer
static uint32_t read_u32(FILE* f) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

// Read a 2-byte big-endian integer
static uint16_t read_u16(FILE* f) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

// Convert TFM fixword to float (16.16 fixed point, in design-size units)
static float fixword_to_float(uint32_t fw) {
    // TFM uses 2^20 as the design size unit
    return (float)((int32_t)fw) / (float)(1 << 20);
}

TFMFont* load_tfm_file(const char* path, Arena* arena) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        // Don't log - caller will try multiple paths
        return nullptr;
    }

    fprintf(stderr, "[DEBUG] tex_tfm: loading %s\n", path);

    // Read header lengths (first 6 halfwords)
    uint16_t lf = read_u16(f);  // File length in words
    uint16_t lh = read_u16(f);  // Header length
    uint16_t bc = read_u16(f);  // First character code
    uint16_t ec = read_u16(f);  // Last character code
    uint16_t nw = read_u16(f);  // Number of widths
    uint16_t nh = read_u16(f);  // Number of heights
    uint16_t nd = read_u16(f);  // Number of depths
    uint16_t ni = read_u16(f);  // Number of italics
    uint16_t nl = read_u16(f);  // Lig/kern program length
    uint16_t nk = read_u16(f);  // Number of kerns
    uint16_t ne = read_u16(f);  // Number of extensible recipes
    uint16_t np = read_u16(f);  // Number of parameters

    log_debug("tex_tfm: loading %s: bc=%d ec=%d nw=%d nh=%d nd=%d",
              path, bc, ec, nw, nh, nd);

    // Validate
    if (ec < bc || nw == 0) {
        log_error("tex_tfm: invalid TFM file %s", path);
        fclose(f);
        return nullptr;
    }

    // Allocate font structure
    TFMFont* font = (TFMFont*)arena_alloc(arena, sizeof(TFMFont));
    memset(font, 0, sizeof(TFMFont));

    font->first_char = bc;
    font->last_char = ec;
    font->nw = nw;
    font->nh = nh;
    font->nd = nd;
    font->ni = ni;
    font->nl = nl;
    font->nk = nk;
    font->ne = ne;
    font->np = np;

    // Read header
    if (lh >= 2) {
        font->checksum = read_u32(f);
        uint32_t ds = read_u32(f);
        font->design_size = fixword_to_float(ds);  // Already in points (fixword is design size in points)
        // Skip rest of header
        fseek(f, (lh - 2) * 4, SEEK_CUR);
    }

    // Allocate and read character info
    int nc = ec - bc + 1;
    font->char_info = (TFMCharInfo*)arena_alloc(arena, nc * sizeof(TFMCharInfo));
    for (int i = 0; i < nc; i++) {
        uint8_t b[4];
        if (fread(b, 1, 4, f) != 4) break;
        font->char_info[i].width_index = b[0];
        font->char_info[i].height_index = (b[1] >> 4) & 0x0F;
        font->char_info[i].depth_index = b[1] & 0x0F;
        font->char_info[i].italic_index = (b[2] >> 2) & 0x3F;
        font->char_info[i].tag = b[2] & 0x03;
        font->char_info[i].remainder = b[3];
    }

    // Read width table
    font->widths = (float*)arena_alloc(arena, nw * sizeof(float));
    for (int i = 0; i < nw; i++) {
        font->widths[i] = fixword_to_float(read_u32(f)) * font->design_size;
    }

    // Read height table
    font->heights = (float*)arena_alloc(arena, nh * sizeof(float));
    for (int i = 0; i < nh; i++) {
        font->heights[i] = fixword_to_float(read_u32(f)) * font->design_size;
    }

    // Read depth table
    font->depths = (float*)arena_alloc(arena, nd * sizeof(float));
    for (int i = 0; i < nd; i++) {
        font->depths[i] = fixword_to_float(read_u32(f)) * font->design_size;
    }

    // Read italic table
    font->italics = (float*)arena_alloc(arena, ni * sizeof(float));
    for (int i = 0; i < ni; i++) {
        font->italics[i] = fixword_to_float(read_u32(f)) * font->design_size;
    }

    // Read lig/kern program
    if (nl > 0) {
        font->lig_kern = (LigKernStep*)arena_alloc(arena, nl * sizeof(LigKernStep));
        for (int i = 0; i < nl; i++) {
            uint8_t b[4];
            if (fread(b, 1, 4, f) != 4) break;
            font->lig_kern[i].skip_byte = b[0];
            font->lig_kern[i].next_char = b[1];
            font->lig_kern[i].op_byte = b[2];
            font->lig_kern[i].remainder = b[3];
        }
    }

    // Read kern table
    if (nk > 0) {
        font->kerns = (float*)arena_alloc(arena, nk * sizeof(float));
        for (int i = 0; i < nk; i++) {
            font->kerns[i] = fixword_to_float(read_u32(f)) * font->design_size;
        }
    }

    // Read extensible table
    if (ne > 0) {
        font->extensibles = (ExtensibleRecipe*)arena_alloc(arena, ne * sizeof(ExtensibleRecipe));
        for (int i = 0; i < ne; i++) {
            uint8_t b[4];
            if (fread(b, 1, 4, f) != 4) break;
            font->extensibles[i].top = b[0];
            font->extensibles[i].mid = b[1];
            font->extensibles[i].bot = b[2];
            font->extensibles[i].rep = b[3];
        }
    }

    // Read parameters
    font->params = (float*)arena_alloc(arena, np * sizeof(float));
    for (int i = 0; i < np; i++) {
        font->params[i] = fixword_to_float(read_u32(f)) * font->design_size;
    }

    // Cache commonly used values
    font->space = font->get_param(TFM_PARAM_SPACE);
    font->space_stretch = font->get_param(TFM_PARAM_SPACE_STRETCH);
    font->space_shrink = font->get_param(TFM_PARAM_SPACE_SHRINK);
    font->x_height = font->get_param(TFM_PARAM_X_HEIGHT);
    font->quad = font->get_param(TFM_PARAM_QUAD);

    fclose(f);

    log_debug("tex_tfm: loaded %s: design_size=%.2f space=%.3f quad=%.3f",
              path, font->design_size, font->space, font->quad);

    return font;
}

TFMFont* load_tfm_by_name(const char* name, Arena* arena) {
    // Try common paths
    char path[512];

    // Try current directory
    snprintf(path, sizeof(path), "%s.tfm", name);
    TFMFont* font = load_tfm_file(path, arena);
    if (font) {
        font->name = name;
        return font;
    }

    // Try texmf paths (common on TeX installations)
    // Search both CM fonts and AMS fonts
    const char* search_paths[] = {
        // Computer Modern fonts
        "/usr/share/texmf/fonts/tfm/public/cm",
        "/usr/share/texlive/texmf-dist/fonts/tfm/public/cm",
        "/opt/homebrew/share/texmf-dist/fonts/tfm/public/cm",
        "/usr/local/texlive/texmf-dist/fonts/tfm/public/cm",
        "/usr/local/texlive/2025basic/texmf-dist/fonts/tfm/public/cm",
        "/usr/local/texlive/2024/texmf-dist/fonts/tfm/public/cm",
        "/usr/local/texlive/2023/texmf-dist/fonts/tfm/public/cm",
        "~/.texlive/texmf-dist/fonts/tfm/public/cm",
        // AMS fonts (msbm10, msam10, etc.)
        "/usr/share/texmf/fonts/tfm/public/amsfonts/symbols",
        "/usr/share/texlive/texmf-dist/fonts/tfm/public/amsfonts/symbols",
        "/opt/homebrew/share/texmf-dist/fonts/tfm/public/amsfonts/symbols",
        "/usr/local/texlive/texmf-dist/fonts/tfm/public/amsfonts/symbols",
        "/usr/local/texlive/2025basic/texmf-dist/fonts/tfm/public/amsfonts/symbols",
        "/usr/local/texlive/2024/texmf-dist/fonts/tfm/public/amsfonts/symbols",
        "/usr/local/texlive/2023/texmf-dist/fonts/tfm/public/amsfonts/symbols",
        "~/.texlive/texmf-dist/fonts/tfm/public/amsfonts/symbols",
        // LaTeX fonts (lasy10 for latexsym symbols)
        "/usr/share/texmf/fonts/tfm/public/latex-fonts",
        "/usr/share/texlive/texmf-dist/fonts/tfm/public/latex-fonts",
        "/opt/homebrew/share/texmf-dist/fonts/tfm/public/latex-fonts",
        "/usr/local/texlive/texmf-dist/fonts/tfm/public/latex-fonts",
        "/usr/local/texlive/2025basic/texmf-dist/fonts/tfm/public/latex-fonts",
        "/usr/local/texlive/2024/texmf-dist/fonts/tfm/public/latex-fonts",
        "/usr/local/texlive/2023/texmf-dist/fonts/tfm/public/latex-fonts",
        "~/.texlive/texmf-dist/fonts/tfm/public/latex-fonts",
        nullptr
    };

    for (int i = 0; search_paths[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s.tfm", search_paths[i], name);
        font = load_tfm_file(path, arena);
        if (font) {
            log_info("tex_tfm: loaded font %s from %s", name, path);
            font->name = name;
            return font;
        }
    }

    // Use built-in fallback
    log_info("tex_tfm: using builtin fallback for %s", name);
    if (strcmp(name, "cmr10") == 0) return get_builtin_cmr10(arena);
    if (strcmp(name, "cmmi10") == 0) return get_builtin_cmmi10(arena);
    if (strcmp(name, "cmsy10") == 0) return get_builtin_cmsy10(arena);
    if (strcmp(name, "cmex10") == 0) return get_builtin_cmex10(arena);

    log_error("tex_tfm: cannot find font %s", name);
    return nullptr;
}

// ============================================================================
// Delimiter Selection (TeX spec: TeXBook p.152, Appendix G Rule 19)
// ============================================================================

// Delimiter code table: maps ASCII delimiter to (small_family, small_pos, large_family, large_pos)
// Format: small form in family 0 (cmr10), large form in family 3 (cmex10)
// Based on Plain TeX \delcode assignments (TeXBook p.345, 427, 432)
struct DelimCode {
    int small_family;    // Family for small form (0=text, 2=cmsy, 3=cmex)
    int small_pos;       // Character position in small font
    int large_family;    // Family for large form (3=cmex)
    int large_pos;       // Starting character position in cmex10
};

static const DelimCode DELIM_CODES[128] = {
    // Default: invalid
    [0 ... 127] = {-1, 0, -1, 0},
    
    // Standard TeX delimiter codes from Plain TeX
    ['('] = {0, '(', 3, 0},      // cmr10 '(' -> cmex10 pos 0
    [')'] = {0, ')', 3, 1},      // cmr10 ')' -> cmex10 pos 1
    ['['] = {0, '[', 3, 2},      // cmr10 '[' -> cmex10 pos 2
    [']'] = {0, ']', 3, 3},      // cmr10 ']' -> cmex10 pos 3
    ['{'] = {2, 102, 3, 8},      // cmsy10 pos 102 ('{') -> cmex10 pos 8
    ['}'] = {2, 103, 3, 9},      // cmsy10 pos 103 ('}') -> cmex10 pos 9
    ['|'] = {2, 106, 3, 12},     // cmsy10 pos 106 -> cmex10 pos 12
    ['<'] = {2, 104, 3, 10},     // cmsy10 langle -> cmex10 pos 10
    ['>'] = {2, 105, 3, 11},     // cmsy10 rangle -> cmex10 pos 11
    ['/'] = {0, '/', 3, 14},     // cmr10 '/' -> cmex10 pos 14
    ['\\'] = {0, '\\', 3, 15},   // cmr10 '\\' -> cmex10 pos 15
};

// Get family font name
static const char* get_family_font(int family) {
    switch (family) {
        case 0: return "cmr10";
        case 1: return "cmmi10";
        case 2: return "cmsy10";
        case 3: return "cmex10";
        default: return "cmr10";
    }
}

DelimiterSelection select_delimiter(TFMFontManager* fonts, int delim_char,
                                    float target_size, float font_size_pt) {
    DelimiterSelection result = {};
    result.font_name = "cmr10";
    result.codepoint = delim_char;
    result.height = 0;
    result.depth = 0;
    result.is_extensible = false;
    memset(&result.recipe, 0, sizeof(result.recipe));
    
    // Apply TeX delimiter sizing formula (TeXBook p.152)
    // Required size = max(target * delimiterfactor / 1000, target - delimitershortfall)
    // Default: delimiterfactor = 901, delimitershortfall = 5pt
    const float delimiter_factor = 901.0f / 1000.0f;
    const float delimiter_shortfall = 5.0f;
    float required_size = fmaxf(target_size * delimiter_factor, target_size - delimiter_shortfall);
    
    log_debug("tex_tfm: select_delimiter '%c' target=%.2f required=%.2f (factor=%.3f shortfall=%.1f)",
              delim_char, target_size, required_size, delimiter_factor, delimiter_shortfall);
    
    // Get delimiter code
    if (delim_char < 0 || delim_char >= 128) {
        log_debug("tex_tfm: select_delimiter: invalid delim_char %d", delim_char);
        return result;
    }
    
    const DelimCode& dc = DELIM_CODES[delim_char];
    if (dc.small_family < 0) {
        log_debug("tex_tfm: select_delimiter: no delcode for '%c' (%d)", delim_char, delim_char);
        return result;
    }
    
    log_debug("tex_tfm: select_delimiter '%c' small=(%d,%d) large=(%d,%d)",
              delim_char, dc.small_family, dc.small_pos, dc.large_family, dc.large_pos);
    
    // First try small form from text/symbol font
    const char* small_font_name = get_family_font(dc.small_family);
    TFMFont* small_font = fonts ? fonts->get_font(small_font_name) : nullptr;
    
    if (small_font && small_font->has_char(dc.small_pos)) {
        float h = small_font->scaled_height(dc.small_pos, font_size_pt);
        float d = small_font->scaled_depth(dc.small_pos, font_size_pt);
        float total = h + d;
        
        log_debug("tex_tfm: small form '%s' pos %d: h=%.2f d=%.2f total=%.2f (required=%.2f)",
                  small_font_name, dc.small_pos, h, d, total, required_size);
        
        if (total >= required_size) {
            // Small form is sufficient
            result.font_name = small_font_name;
            result.codepoint = dc.small_pos;
            result.height = h;
            result.depth = d;
            log_debug("tex_tfm: selected small form %s pos %d", small_font_name, dc.small_pos);
            return result;
        }
    }
    
    // Try cmex10 chain for larger sizes
    TFMFont* cmex = fonts ? fonts->get_font("cmex10") : nullptr;
    if (!cmex) {
        log_debug("tex_tfm: select_delimiter: cmex10 not available, using small form");
        result.font_name = small_font_name;
        result.codepoint = dc.small_pos;
        return result;
    }
    
    // Walk the "next larger" chain starting from large_pos
    int current = dc.large_pos;
    int best_char = -1;  // -1 means no valid char found yet
    float best_total = 0;
    
    // Maximum chain depth to prevent infinite loops
    const int MAX_CHAIN = 16;
    
    // Note: position 0 is valid in cmex10 (left paren), so we use -1 as sentinel
    for (int i = 0; i < MAX_CHAIN && current >= 0 && cmex->has_char(current); i++) {
        float h = cmex->scaled_height(current, font_size_pt);
        float d = cmex->scaled_depth(current, font_size_pt);
        float total = h + d;
        
        log_debug("tex_tfm: chain[%d] pos %d: h=%.2f d=%.2f total=%.2f (required=%.2f)",
                  i, current, h, d, total, required_size);
        
        if (total > best_total) {
            best_char = current;
            best_total = total;
        }
        
        // Check if this glyph is large enough
        if (total >= required_size) {
            result.font_name = "cmex10";
            result.codepoint = current;
            result.height = h;
            result.depth = d;
            log_debug("tex_tfm: selected cmex10 pos %d (chain)", current);
            return result;
        }
        
        // Check for extensible recipe
        if (cmex->has_extensible(current)) {
            const ExtensibleRecipe* ext = cmex->get_extensible(current);
            if (ext && ext->rep != 0) {
                // This character has an extensible recipe
                result.font_name = "cmex10";
                result.codepoint = current;  // Use this as base
                result.is_extensible = true;
                result.recipe = *ext;
                result.height = required_size * 0.6f;  // Approximate
                result.depth = required_size * 0.4f;
                log_debug("tex_tfm: selected cmex10 pos %d (extensible: top=%d mid=%d bot=%d rep=%d)",
                          current, ext->top, ext->mid, ext->bot, ext->rep);
                return result;
            }
        }
        
        // Move to next larger (0 means no next larger, but 0 could also be a valid char)
        // TFM uses 0 as "no next char" - but cmex10 position 0 is the small left paren
        // So we need to track if we're on the first iteration separately
        int next = cmex->get_next_larger(current);
        if (next == current) break;  // Self-reference means end of chain
        if (next == 0 && current != 0) break;  // 0 as "next" means end (unless we're at 0)
        if (next == 0 && i > 0) break;  // We've passed position 0 already
        current = next;
    }
    
    // Use best found glyph even if smaller than target
    if (best_char >= 0) {
        result.font_name = "cmex10";
        result.codepoint = best_char;
        result.height = cmex->scaled_height(best_char, font_size_pt);
        result.depth = cmex->scaled_depth(best_char, font_size_pt);
        log_debug("tex_tfm: selected cmex10 pos %d (best available)", best_char);
    }
    
    return result;
}

// ============================================================================
// Built-in CMR10 Metrics (Fallback)
// ============================================================================

// CMR10 character widths (in points at 10pt design size)
// Data extracted from cmr10.tfm
static const float CMR10_WIDTHS[128] = {
    // ASCII 32-127 (printable characters)
    // These are approximate values for testing
    [' '] = 3.33,   // space
    ['!'] = 2.78,
    ['"'] = 5.00,
    ['#'] = 8.33,
    ['$'] = 5.00,
    ['%'] = 8.33,
    ['&'] = 7.78,
    ['\''] = 2.78,
    ['('] = 3.89,
    [')'] = 3.89,
    ['*'] = 5.00,
    ['+'] = 7.78,
    [','] = 2.78,
    ['-'] = 3.33,
    ['.'] = 2.78,
    ['/'] = 5.00,
    ['0'] = 5.00,
    ['1'] = 5.00,
    ['2'] = 5.00,
    ['3'] = 5.00,
    ['4'] = 5.00,
    ['5'] = 5.00,
    ['6'] = 5.00,
    ['7'] = 5.00,
    ['8'] = 5.00,
    ['9'] = 5.00,
    [':'] = 2.78,
    [';'] = 2.78,
    ['<'] = 7.78,
    ['='] = 7.78,
    ['>'] = 7.78,
    ['?'] = 4.72,
    ['@'] = 7.78,
    ['A'] = 7.50,
    ['B'] = 7.08,
    ['C'] = 7.22,
    ['D'] = 7.64,
    ['E'] = 6.81,
    ['F'] = 6.53,
    ['G'] = 7.85,
    ['H'] = 7.50,
    ['I'] = 3.61,
    ['J'] = 5.14,
    ['K'] = 7.78,
    ['L'] = 6.25,
    ['M'] = 9.17,
    ['N'] = 7.50,
    ['O'] = 7.78,
    ['P'] = 6.81,
    ['Q'] = 7.78,
    ['R'] = 7.36,
    ['S'] = 5.56,
    ['T'] = 7.22,
    ['U'] = 7.50,
    ['V'] = 7.50,
    ['W'] = 10.28,
    ['X'] = 7.50,
    ['Y'] = 7.50,
    ['Z'] = 6.11,
    ['['] = 2.78,
    ['\\'] = 5.00,
    [']'] = 2.78,
    ['^'] = 5.00,
    ['_'] = 3.00,
    ['`'] = 2.78,
    ['a'] = 5.00,
    ['b'] = 5.56,
    ['c'] = 4.44,
    ['d'] = 5.56,
    ['e'] = 4.44,
    ['f'] = 3.06,
    ['g'] = 5.00,
    ['h'] = 5.56,
    ['i'] = 2.78,
    ['j'] = 3.06,
    ['k'] = 5.28,
    ['l'] = 2.78,
    ['m'] = 8.33,
    ['n'] = 5.56,
    ['o'] = 5.00,
    ['p'] = 5.56,
    ['q'] = 5.28,
    ['r'] = 3.92,
    ['s'] = 3.94,
    ['t'] = 3.89,
    ['u'] = 5.56,
    ['v'] = 5.28,
    ['w'] = 7.22,
    ['x'] = 5.28,
    ['y'] = 5.28,
    ['z'] = 4.44,
    ['{'] = 5.00,
    ['|'] = 10.00,
    ['}'] = 5.00,
    ['~'] = 5.00,
};

static const float CMR10_HEIGHTS[] = {
    // Most lowercase letters
    [0] = 4.31,     // x-height
    // Uppercase and tall lowercase
    [1] = 6.83,     // cap height
    // Descenders
    [2] = 6.94,     // ascender height
};

static const float CMR10_DEPTHS[] = {
    [0] = 0.0,      // No descender
    [1] = 1.94,     // Descender (g, j, p, q, y)
};

TFMFont* get_builtin_cmr10(Arena* arena) {
    TFMFont* font = (TFMFont*)arena_alloc(arena, sizeof(TFMFont));
    memset(font, 0, sizeof(TFMFont));

    font->name = "cmr10";
    font->design_size = 10.0f;
    font->first_char = 0;
    font->last_char = 127;

    // Allocate tables
    int nc = 128;
    font->char_info = (TFMCharInfo*)arena_alloc(arena, nc * sizeof(TFMCharInfo));
    memset(font->char_info, 0, nc * sizeof(TFMCharInfo));

    font->nw = 128;
    font->nh = 4;
    font->nd = 4;
    font->ni = 1;
    font->np = 7;

    font->widths = (float*)arena_alloc(arena, font->nw * sizeof(float));
    font->heights = (float*)arena_alloc(arena, font->nh * sizeof(float));
    font->depths = (float*)arena_alloc(arena, font->nd * sizeof(float));
    font->italics = (float*)arena_alloc(arena, font->ni * sizeof(float));
    font->params = (float*)arena_alloc(arena, font->np * sizeof(float));

    // Copy width data
    for (int i = 0; i < 128; i++) {
        font->widths[i] = CMR10_WIDTHS[i];
        font->char_info[i].width_index = (CMR10_WIDTHS[i] > 0) ? i : 0;

        // Set height index based on character
        if (i >= 'A' && i <= 'Z') {
            font->char_info[i].height_index = 1;  // Cap height
        } else if (i >= 'a' && i <= 'z') {
            if (i == 'b' || i == 'd' || i == 'f' || i == 'h' ||
                i == 'k' || i == 'l' || i == 't') {
                font->char_info[i].height_index = 2;  // Ascender
            } else {
                font->char_info[i].height_index = 0;  // x-height
            }
        } else if (i >= '0' && i <= '9') {
            font->char_info[i].height_index = 1;
        }

        // Set depth index for descenders
        if (i == 'g' || i == 'j' || i == 'p' || i == 'q' || i == 'y') {
            font->char_info[i].depth_index = 1;
        }
    }

    // Height table
    font->heights[0] = 4.31f;   // x-height
    font->heights[1] = 6.83f;   // cap height
    font->heights[2] = 6.94f;   // ascender
    font->heights[3] = 0.0f;

    // Depth table
    font->depths[0] = 0.0f;
    font->depths[1] = 1.94f;    // descender
    font->depths[2] = 0.0f;
    font->depths[3] = 0.0f;

    // Italic corrections
    font->italics[0] = 0.0f;

    // Font parameters
    font->params[0] = 0.0f;     // slant
    font->params[1] = 3.33f;    // space
    font->params[2] = 1.67f;    // space stretch
    font->params[3] = 1.11f;    // space shrink
    font->params[4] = 4.31f;    // x-height
    font->params[5] = 10.0f;    // quad (1em)
    font->params[6] = 1.11f;    // extra space

    // Cache values
    font->space = 3.33f;
    font->space_stretch = 1.67f;
    font->space_shrink = 1.11f;
    font->x_height = 4.31f;
    font->quad = 10.0f;

    log_debug("tex_tfm: created builtin cmr10");
    return font;
}

TFMFont* get_builtin_cmmi10(Arena* arena) {
    // Math italic - similar to CMR10 but with different metrics
    TFMFont* font = get_builtin_cmr10(arena);
    font->name = "cmmi10";

    // Add italic corrections for all characters
    for (int i = 0; i < 128; i++) {
        if (font->char_info[i].width_index > 0) {
            font->char_info[i].italic_index = 0;
        }
    }
    font->italics[0] = 0.5f;  // Small italic correction

    // Slant
    font->params[0] = 0.25f;

    return font;
}

TFMFont* get_builtin_cmsy10(Arena* arena) {
    // Math symbols - minimal implementation
    TFMFont* font = (TFMFont*)arena_alloc(arena, sizeof(TFMFont));
    memset(font, 0, sizeof(TFMFont));

    font->name = "cmsy10";
    font->design_size = 10.0f;
    font->first_char = 0;
    font->last_char = 127;

    int nc = 128;
    font->char_info = (TFMCharInfo*)arena_alloc(arena, nc * sizeof(TFMCharInfo));
    memset(font->char_info, 0, nc * sizeof(TFMCharInfo));

    font->nw = 8;
    font->nh = 4;
    font->nd = 4;
    font->ni = 1;
    font->np = 22;  // Math symbol font has 22 params

    font->widths = (float*)arena_alloc(arena, font->nw * sizeof(float));
    font->heights = (float*)arena_alloc(arena, font->nh * sizeof(float));
    font->depths = (float*)arena_alloc(arena, font->nd * sizeof(float));
    font->italics = (float*)arena_alloc(arena, font->ni * sizeof(float));
    font->params = (float*)arena_alloc(arena, font->np * sizeof(float));

    // Standard widths
    font->widths[0] = 0.0f;
    font->widths[1] = 5.0f;
    font->widths[2] = 7.78f;
    font->widths[3] = 10.0f;

    font->heights[0] = 0.0f;
    font->heights[1] = 4.31f;
    font->heights[2] = 6.83f;

    font->depths[0] = 0.0f;
    font->depths[1] = 1.94f;

    // Math symbol parameters (fontdimen 8-22)
    font->params[TFM_PARAM_NUM1 - 1] = 6.76f;      // num1
    font->params[TFM_PARAM_NUM2 - 1] = 3.94f;      // num2
    font->params[TFM_PARAM_NUM3 - 1] = 4.43f;      // num3
    font->params[TFM_PARAM_DENOM1 - 1] = 6.86f;    // denom1
    font->params[TFM_PARAM_DENOM2 - 1] = 3.45f;    // denom2
    font->params[TFM_PARAM_SUP1 - 1] = 4.13f;      // sup1
    font->params[TFM_PARAM_SUP2 - 1] = 3.63f;      // sup2
    font->params[TFM_PARAM_SUP3 - 1] = 2.89f;      // sup3
    font->params[TFM_PARAM_SUB1 - 1] = 1.50f;      // sub1
    font->params[TFM_PARAM_SUB2 - 1] = 2.47f;      // sub2
    font->params[TFM_PARAM_SUP_DROP - 1] = 3.86f;  // sup_drop
    font->params[TFM_PARAM_SUB_DROP - 1] = 0.50f;  // sub_drop
    font->params[TFM_PARAM_DELIM1 - 1] = 23.9f;    // delim1
    font->params[TFM_PARAM_DELIM2 - 1] = 10.1f;    // delim2
    font->params[TFM_PARAM_AXIS_HEIGHT - 1] = 2.5f; // axis_height

    font->x_height = 4.31f;
    font->quad = 10.0f;

    return font;
}

// ============================================================================
// CMEX10 Delimiter Chain Data (from actual TFM file analysis)
// ============================================================================
// cmex10 character layout for delimiters:
//   0-15:   Small delimiters (first size)
//   16-31:  Second size
//   32-47:  Third size (some empty)
//   48-63:  Large operators (display size)
//   64-79:  Small operators (text size)
//   80-95:  More operators and accents
//   96-111: Extensible pieces (tops, bottoms, middles)
//   112-127: More extensible pieces
//
// Character chains (next larger):
//   ( : 0 -> 16 -> 18 -> 32 -> 48 (extensible at 96)
//   ) : 1 -> 17 -> 19 -> 33 -> 49 (extensible at 97)
//   [ : 2 -> 20 -> 34 -> 50 (extensible at 104)
//   ] : 3 -> 21 -> 35 -> 51 (extensible at 105)
//   { : 8 -> 26 -> 40 -> 56 (extensible at 110)
//   } : 9 -> 27 -> 41 -> 57 (extensible at 111)

// Height data for cmex10 (in points at design size 10pt)
static const struct { int pos; float height; float depth; int tag; int remainder; } CMEX10_CHARS[] = {
    // Left parenthesis chain: 0 -> 16 -> 18 -> 32
    { 0,  4.00f, 3.00f, TFM_TAG_CHAIN, 16 },   // ( small
    {16,  6.00f, 4.50f, TFM_TAG_CHAIN, 18 },   // ( medium-small
    {18,  7.50f, 5.60f, TFM_TAG_CHAIN, 32 },   // ( medium
    {32, 10.00f, 7.50f, TFM_TAG_EXTENS, 0 },   // ( large (has extensible recipe)
    
    // Right parenthesis chain: 1 -> 17 -> 19 -> 33
    { 1,  4.00f, 3.00f, TFM_TAG_CHAIN, 17 },   // ) small
    {17,  6.00f, 4.50f, TFM_TAG_CHAIN, 19 },   // ) medium-small
    {19,  7.50f, 5.60f, TFM_TAG_CHAIN, 33 },   // ) medium
    {33, 10.00f, 7.50f, TFM_TAG_EXTENS, 1 },   // ) large
    
    // Left bracket chain: 2 -> 20 -> 34 -> 50
    { 2,  4.58f, 3.58f, TFM_TAG_CHAIN, 20 },   // [ small
    {20,  6.87f, 5.38f, TFM_TAG_CHAIN, 34 },   // [ medium-small
    {34,  9.17f, 7.17f, TFM_TAG_CHAIN, 50 },   // [ medium
    {50, 11.46f, 8.96f, TFM_TAG_EXTENS, 2 },   // [ large (extensible at recipe 2)
    
    // Right bracket chain: 3 -> 21 -> 35 -> 51
    { 3,  4.58f, 3.58f, TFM_TAG_CHAIN, 21 },   // ] small
    {21,  6.87f, 5.38f, TFM_TAG_CHAIN, 35 },   // ] medium-small
    {35,  9.17f, 7.17f, TFM_TAG_CHAIN, 51 },   // ] medium
    {51, 11.46f, 8.96f, TFM_TAG_EXTENS, 3 },   // ] large
    
    // Floor: 4, 5 chains
    { 4,  4.58f, 3.58f, TFM_TAG_CHAIN, 22 },   // floor_left
    { 5,  4.58f, 3.58f, TFM_TAG_CHAIN, 23 },   // floor_right
    {22,  6.87f, 5.38f, TFM_TAG_CHAIN, 36 },
    {23,  6.87f, 5.38f, TFM_TAG_CHAIN, 37 },
    {36,  9.17f, 7.17f, TFM_TAG_CHAIN, 52 },
    {37,  9.17f, 7.17f, TFM_TAG_CHAIN, 53 },
    {52, 11.46f, 8.96f, TFM_TAG_NONE, 0 },
    {53, 11.46f, 8.96f, TFM_TAG_NONE, 0 },
    
    // Ceiling: 6, 7 chains
    { 6,  4.58f, 3.58f, TFM_TAG_CHAIN, 24 },   // ceil_left
    { 7,  4.58f, 3.58f, TFM_TAG_CHAIN, 25 },   // ceil_right
    {24,  6.87f, 5.38f, TFM_TAG_CHAIN, 38 },
    {25,  6.87f, 5.38f, TFM_TAG_CHAIN, 39 },
    {38,  9.17f, 7.17f, TFM_TAG_CHAIN, 54 },
    {39,  9.17f, 7.17f, TFM_TAG_CHAIN, 55 },
    {54, 11.46f, 8.96f, TFM_TAG_NONE, 0 },
    {55, 11.46f, 8.96f, TFM_TAG_NONE, 0 },
    
    // Braces: 8, 9 chains
    { 8,  4.00f, 3.00f, TFM_TAG_CHAIN, 26 },   // { small
    { 9,  4.00f, 3.00f, TFM_TAG_CHAIN, 27 },   // } small
    {26,  6.00f, 4.50f, TFM_TAG_CHAIN, 40 },
    {27,  6.00f, 4.50f, TFM_TAG_CHAIN, 41 },
    {40,  8.00f, 6.00f, TFM_TAG_CHAIN, 56 },
    {41,  8.00f, 6.00f, TFM_TAG_CHAIN, 57 },
    {56, 10.00f, 7.50f, TFM_TAG_EXTENS, 4 },   // { extensible
    {57, 10.00f, 7.50f, TFM_TAG_EXTENS, 5 },   // } extensible
    
    // Angle brackets: 10, 11 chains
    {10,  4.00f, 3.00f, TFM_TAG_CHAIN, 28 },   // < langle
    {11,  4.00f, 3.00f, TFM_TAG_CHAIN, 29 },   // > rangle
    {28,  6.00f, 4.50f, TFM_TAG_CHAIN, 42 },
    {29,  6.00f, 4.50f, TFM_TAG_CHAIN, 43 },
    {42,  8.00f, 6.00f, TFM_TAG_CHAIN, 58 },
    {43,  8.00f, 6.00f, TFM_TAG_CHAIN, 59 },
    {58, 10.00f, 7.50f, TFM_TAG_NONE, 0 },
    {59, 10.00f, 7.50f, TFM_TAG_NONE, 0 },
    
    // Vertical bar: 12 chain
    {12,  4.31f, 0.0f, TFM_TAG_CHAIN, 30 },    // | small
    {30,  6.50f, 0.0f, TFM_TAG_CHAIN, 44 },
    {44,  8.60f, 0.0f, TFM_TAG_CHAIN, 60 },
    {60, 10.70f, 0.0f, TFM_TAG_EXTENS, 6 },    // | extensible
    
    // Double vertical bar: 13 chain
    {13,  4.31f, 0.0f, TFM_TAG_CHAIN, 31 },    // || small
    {31,  6.50f, 0.0f, TFM_TAG_CHAIN, 45 },
    {45,  8.60f, 0.0f, TFM_TAG_CHAIN, 61 },
    {61, 10.70f, 0.0f, TFM_TAG_EXTENS, 7 },    // || extensible
    
    // Slashes: 14, 15
    {14,  4.31f, 3.06f, TFM_TAG_CHAIN, 46 },   // /
    {15,  4.31f, 3.06f, TFM_TAG_CHAIN, 47 },   // backslash
    {46,  6.50f, 4.59f, TFM_TAG_CHAIN, 62 },
    {47,  6.50f, 4.59f, TFM_TAG_CHAIN, 63 },
    {62,  8.60f, 6.13f, TFM_TAG_NONE, 0 },
    {63,  8.60f, 6.13f, TFM_TAG_NONE, 0 },
    
    // Extensible pieces (positions 96-127)
    // These are the pieces used to build extensible delimiters
    {96,  0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ( top piece
    {97,  0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ) top piece
    {98,  0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ( bottom piece
    {99,  0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ) bottom piece
    {100, 0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ( middle/repeater
    {101, 0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ) middle/repeater
    {102, 0.40f, 0.0f, TFM_TAG_NONE, 0 },      // [ top piece
    {103, 0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ] top piece
    {104, 0.40f, 0.0f, TFM_TAG_NONE, 0 },      // [ repeater (also used as standalone for larger bracket)
    {105, 0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ] repeater
    {106, 0.40f, 0.0f, TFM_TAG_NONE, 0 },      // [ bottom piece
    {107, 0.40f, 0.0f, TFM_TAG_NONE, 0 },      // ] bottom piece
    
    // Terminator
    {-1, 0, 0, 0, 0}
};

// Extensible recipes for cmex10
static const ExtensibleRecipe CMEX10_EXTENSIBLES[] = {
    // Recipe 0: left parenthesis ( - top=96, mid=0, bot=98, rep=100
    { 96, 0, 98, 100 },
    // Recipe 1: right parenthesis ) - top=97, mid=0, bot=99, rep=101
    { 97, 0, 99, 101 },
    // Recipe 2: left bracket [ - top=102, mid=0, bot=106, rep=104
    { 102, 0, 106, 104 },
    // Recipe 3: right bracket ] - top=103, mid=0, bot=107, rep=105
    { 103, 0, 107, 105 },
    // Recipe 4: left brace { - uses three-piece recipe
    { 56, 62, 58, 60 },  // top, mid, bot, rep (approximation)
    // Recipe 5: right brace }
    { 57, 63, 59, 61 },
    // Recipe 6: vertical bar |
    { 0, 0, 0, 12 },     // just repeat position 12
    // Recipe 7: double vertical bar ||
    { 0, 0, 0, 13 },
};

TFMFont* get_builtin_cmex10(Arena* arena) {
    // Math extension font - large operators and extensibles
    TFMFont* font = (TFMFont*)arena_alloc(arena, sizeof(TFMFont));
    memset(font, 0, sizeof(TFMFont));

    font->name = "cmex10";
    font->design_size = 10.0f;
    font->first_char = 0;
    font->last_char = 127;

    int nc = 128;
    font->char_info = (TFMCharInfo*)arena_alloc(arena, nc * sizeof(TFMCharInfo));
    memset(font->char_info, 0, nc * sizeof(TFMCharInfo));

    // We'll use direct height/depth values instead of indices for simplicity
    font->nw = 16;
    font->nh = 32;
    font->nd = 16;
    font->ne = 8;   // 8 extensible recipes
    font->np = 13;

    font->widths = (float*)arena_alloc(arena, font->nw * sizeof(float));
    font->heights = (float*)arena_alloc(arena, font->nh * sizeof(float));
    font->depths = (float*)arena_alloc(arena, font->nd * sizeof(float));
    font->extensibles = (ExtensibleRecipe*)arena_alloc(arena, font->ne * sizeof(ExtensibleRecipe));
    font->params = (float*)arena_alloc(arena, font->np * sizeof(float));

    // Initialize widths (delimiters are typically narrow)
    font->widths[0] = 0.0f;
    for (int i = 1; i < font->nw; i++) {
        font->widths[i] = 4.58f;  // typical delimiter width
    }
    
    // Initialize heights (indexed by height_index)
    font->heights[0] = 0.0f;
    font->heights[1] = 4.00f;
    font->heights[2] = 4.31f;
    font->heights[3] = 4.58f;
    font->heights[4] = 6.00f;
    font->heights[5] = 6.50f;
    font->heights[6] = 6.87f;
    font->heights[7] = 7.50f;
    font->heights[8] = 8.00f;
    font->heights[9] = 8.60f;
    font->heights[10] = 9.17f;
    font->heights[11] = 10.00f;
    font->heights[12] = 10.70f;
    font->heights[13] = 11.46f;
    
    // Initialize depths
    font->depths[0] = 0.0f;
    font->depths[1] = 3.00f;
    font->depths[2] = 3.06f;
    font->depths[3] = 3.58f;
    font->depths[4] = 4.50f;
    font->depths[5] = 4.59f;
    font->depths[6] = 5.38f;
    font->depths[7] = 5.60f;
    font->depths[8] = 6.00f;
    font->depths[9] = 6.13f;
    font->depths[10] = 7.17f;
    font->depths[11] = 7.50f;
    font->depths[12] = 8.96f;
    
    // Copy extensible recipes
    memcpy(font->extensibles, CMEX10_EXTENSIBLES, sizeof(CMEX10_EXTENSIBLES));
    
    // Set up char_info from the CMEX10_CHARS table
    // We need to map height/depth values to indices
    for (int i = 0; CMEX10_CHARS[i].pos >= 0; i++) {
        int pos = CMEX10_CHARS[i].pos;
        if (pos >= 0 && pos < nc) {
            TFMCharInfo& ci = font->char_info[pos];
            ci.width_index = 1;  // All delimiters have similar width
            
            // Find height index (simple linear search)
            float h = CMEX10_CHARS[i].height;
            ci.height_index = 0;
            for (int j = 0; j < font->nh; j++) {
                if (font->heights[j] >= h - 0.1f && font->heights[j] <= h + 0.1f) {
                    ci.height_index = j;
                    break;
                }
            }
            
            // Find depth index
            float d = CMEX10_CHARS[i].depth;
            ci.depth_index = 0;
            for (int j = 0; j < font->nd; j++) {
                if (font->depths[j] >= d - 0.1f && font->depths[j] <= d + 0.1f) {
                    ci.depth_index = j;
                    break;
                }
            }
            
            ci.tag = CMEX10_CHARS[i].tag;
            ci.remainder = CMEX10_CHARS[i].remainder;
        }
    }

    // Extension params
    font->params[TFM_PARAM_DEFAULT_RULE - 1] = 0.4f;  // default rule thickness
    font->quad = 10.0f;

    log_debug("tex_tfm: built cmex10 builtin with %d chars, %d extensible recipes", nc, font->ne);
    
    return font;
}

// ============================================================================
// Font Manager Implementation
// ============================================================================

TFMFontManager* create_font_manager(Arena* arena) {
    TFMFontManager* mgr = (TFMFontManager*)arena_alloc(arena, sizeof(TFMFontManager));
    mgr->arena = arena;
    mgr->font_count = 0;
    mgr->font_capacity = 16;
    mgr->fonts = (TFMFont**)arena_alloc(arena, mgr->font_capacity * sizeof(TFMFont*));
    mgr->font_names = (const char**)arena_alloc(arena, mgr->font_capacity * sizeof(const char*));
    return mgr;
}

TFMFont* TFMFontManager::get_font(const char* name) {
    // Check if already loaded
    for (int i = 0; i < font_count; i++) {
        if (strcmp(font_names[i], name) == 0) {
            return fonts[i];
        }
    }

    // Load new font
    TFMFont* font = load_tfm_by_name(name, arena);
    if (font) {
        register_font(name, font);
    }
    return font;
}

void TFMFontManager::register_font(const char* name, TFMFont* font) {
    if (font_count >= font_capacity) {
        // Grow arrays
        int new_cap = font_capacity * 2;
        TFMFont** new_fonts = (TFMFont**)arena_alloc(arena, new_cap * sizeof(TFMFont*));
        const char** new_names = (const char**)arena_alloc(arena, new_cap * sizeof(const char*));
        memcpy(new_fonts, fonts, font_count * sizeof(TFMFont*));
        memcpy(new_names, font_names, font_count * sizeof(const char*));
        fonts = new_fonts;
        font_names = new_names;
        font_capacity = new_cap;
    }

    fonts[font_count] = font;
    font_names[font_count] = name;
    font_count++;
}

} // namespace tex
