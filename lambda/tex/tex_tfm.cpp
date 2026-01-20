// tex_tfm.cpp - TFM File Parser Implementation
//
// Parses TeX Font Metrics files and provides built-in fallback metrics
// for Computer Modern fonts.

#include "tex_tfm.hpp"
#include "lib/log.h"
#include <cstring>
#include <cstdlib>

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
        font->design_size = fixword_to_float(ds) * 16.0f;  // Convert to points
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

    font->nw = 8;
    font->nh = 8;
    font->nd = 8;
    font->np = 13;  // Math extension font params

    font->widths = (float*)arena_alloc(arena, font->nw * sizeof(float));
    font->heights = (float*)arena_alloc(arena, font->nh * sizeof(float));
    font->depths = (float*)arena_alloc(arena, font->nd * sizeof(float));
    font->params = (float*)arena_alloc(arena, font->np * sizeof(float));

    // Large operator widths
    font->widths[1] = 10.0f;   // Sum-like
    font->widths[2] = 8.33f;   // Integral

    // Extension params
    font->params[TFM_PARAM_DEFAULT_RULE - 1] = 0.4f;  // default rule thickness

    font->quad = 10.0f;

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
