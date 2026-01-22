// tex_dvi_out.cpp - DVI Output Generation Implementation
//
// Reference: TeXBook Appendix A, DVI format specification

#include "tex_dvi_out.hpp"
#include "tex_tfm.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// DVI Opcodes (from dvi_parser.hpp)
// ============================================================================

enum DVIOpcode : uint8_t {
    DVI_SET_CHAR_0   = 0,
    DVI_SET_CHAR_127 = 127,
    DVI_SET1 = 128,
    DVI_SET2 = 129,
    DVI_SET3 = 130,
    DVI_SET4 = 131,
    DVI_SET_RULE = 132,
    DVI_PUT1 = 133,
    DVI_PUT2 = 134,
    DVI_PUT3 = 135,
    DVI_PUT4 = 136,
    DVI_PUT_RULE = 137,
    DVI_NOP = 138,
    DVI_BOP = 139,
    DVI_EOP = 140,
    DVI_PUSH = 141,
    DVI_POP = 142,
    DVI_RIGHT1 = 143,
    DVI_RIGHT2 = 144,
    DVI_RIGHT3 = 145,
    DVI_RIGHT4 = 146,
    DVI_W0 = 147,
    DVI_W1 = 148,
    DVI_W2 = 149,
    DVI_W3 = 150,
    DVI_W4 = 151,
    DVI_X0 = 152,
    DVI_X1 = 153,
    DVI_X2 = 154,
    DVI_X3 = 155,
    DVI_X4 = 156,
    DVI_DOWN1 = 157,
    DVI_DOWN2 = 158,
    DVI_DOWN3 = 159,
    DVI_DOWN4 = 160,
    DVI_Y0 = 161,
    DVI_Y1 = 162,
    DVI_Y2 = 163,
    DVI_Y3 = 164,
    DVI_Y4 = 165,
    DVI_Z0 = 166,
    DVI_Z1 = 167,
    DVI_Z2 = 168,
    DVI_Z3 = 169,
    DVI_Z4 = 170,
    DVI_FNT_NUM_0  = 171,
    DVI_FNT_NUM_63 = 234,
    DVI_FNT1 = 235,
    DVI_FNT2 = 236,
    DVI_FNT3 = 237,
    DVI_FNT4 = 238,
    DVI_XXX1 = 239,
    DVI_XXX2 = 240,
    DVI_XXX3 = 241,
    DVI_XXX4 = 242,
    DVI_FNT_DEF1 = 243,
    DVI_FNT_DEF2 = 244,
    DVI_FNT_DEF3 = 245,
    DVI_FNT_DEF4 = 246,
    DVI_PRE = 247,
    DVI_POST = 248,
    DVI_POST_POST = 249,
};

// ============================================================================
// Font Encoding Translation
// ============================================================================
// TeX CM fonts use different character encodings than ASCII/Unicode.
// This translates Unicode/ASCII codepoints to font-specific positions.

// CMMI (Computer Modern Math Italic) encoding for punctuation
// In cmmi10: position 58='.', 59=',', 60='<', 61='/', 62='>', 63='*'
static int32_t unicode_to_cmmi(int32_t cp) {
    // Punctuation remapping (key differences from ASCII)
    if (cp == ',') return 59;   // ASCII 44 -> cmmi 59
    if (cp == '.') return 58;   // ASCII 46 -> cmmi 58
    if (cp == '<') return 60;   // same
    if (cp == '/') return 61;   // ASCII 47 -> cmmi 61
    if (cp == '>') return 62;   // same
    if (cp == '*') return 63;   // ASCII 42 -> cmmi 63
    
    // Letters and digits are at same positions
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return cp;
    if (cp >= '0' && cp <= '9') return cp - '0' + 48;  // digits at 48-57
    
    // Greek uppercase (positions 0-10)
    // Greek lowercase (positions 11-33)
    // These are typically set via symbol lookup, not here
    
    return cp;  // default: pass through
}

// CMR (Computer Modern Roman) encoding - mostly matches ASCII
static int32_t unicode_to_cmr(int32_t cp) {
    // CMR is mostly ASCII-compatible for printable characters
    // Some special characters:
    if (cp == 0x2018) return 96;   // left single quote
    if (cp == 0x2019) return 39;   // right single quote  
    if (cp == 0x201C) return 92;   // left double quote
    if (cp == 0x201D) return 34;   // right double quote
    if (cp == 0x2013) return 123;  // en-dash
    if (cp == 0x2014) return 124;  // em-dash
    
    return cp;  // mostly pass through for ASCII
}

// CMSY (Computer Modern Symbol) encoding
static int32_t unicode_to_cmsy(int32_t cp) {
    // Most symbols are set via explicit code lookup, but handle some cases
    if (cp == '-') return 0;    // minus sign at position 0
    if (cp == 0x2212) return 0; // Unicode minus
    
    // Arrows
    if (cp == 0x2190) return 32;  // leftarrow (space position)
    if (cp == 0x2192) return 33;  // rightarrow/to (! position)
    if (cp == 0x2191) return 34;  // uparrow (double quote position)
    if (cp == 0x2193) return 35;  // downarrow (# position)
    if (cp == 0x2194) return 36;  // leftrightarrow ($ position)
    if (cp == 0x2197) return 37;  // nearrow (% position)
    if (cp == 0x2198) return 38;  // searrow (& position)
    if (cp == 0x21D0) return 40;  // Leftarrow (( position)
    if (cp == 0x21D2) return 41;  // Rightarrow () position)
    if (cp == 0x21D1) return 42;  // Uparrow (* position)
    if (cp == 0x21D3) return 43;  // Downarrow (+ position)
    if (cp == 0x21D4) return 44;  // Leftrightarrow (, position)
    if (cp == 0x21CC) return 29;  // rightleftharpoons
    
    // Infinity
    if (cp == 0x221E) return 49;  // infinity (1 position)
    
    // Delimiters - cmsy uses different positions than ASCII
    if (cp == '{') return 102;  // left brace at 102 (shows as 'f')
    if (cp == '}') return 103;  // right brace at 103 (shows as 'g')
    if (cp == '|') return 106;  // vertical bar at 106 (shows as 'j')
    // Note: backslash/setminus is at 110, but should come from symbol lookup, not ASCII translation
    
    return cp;
}

// CMEX (Computer Modern Extension) encoding  
static int32_t unicode_to_cmex(int32_t cp) {
    // Large delimiters - mostly set via explicit lookup
    return cp;
}

// Main translation function - dispatches based on font name
static int32_t translate_to_font_encoding(const char* font_name, int32_t cp) {
    if (!font_name) return cp;
    
    int32_t result = cp;
    
    // Check font family
    if (strncmp(font_name, "cmmi", 4) == 0) {
        result = unicode_to_cmmi(cp);
    } else if (strncmp(font_name, "cmr", 3) == 0) {
        result = unicode_to_cmr(cp);
    } else if (strncmp(font_name, "cmsy", 4) == 0) {
        result = unicode_to_cmsy(cp);
    } else if (strncmp(font_name, "cmex", 4) == 0) {
        result = unicode_to_cmex(cp);
    }
    
    // Debug: log translations for punctuation
    if (cp != result) {
        log_debug("tex_dvi_out: font_encoding %s: %d -> %d", font_name, cp, result);
    }
    
    return result;
}

// ============================================================================
// Low-Level Writing Helpers
// ============================================================================

static void write_u8(DVIWriter& w, uint8_t v) {
    fputc(v, w.file);
    w.byte_count++;
}

static void write_u16(DVIWriter& w, uint16_t v) {
    write_u8(w, (v >> 8) & 0xFF);
    write_u8(w, v & 0xFF);
}

static void write_u24(DVIWriter& w, uint32_t v) {
    write_u8(w, (v >> 16) & 0xFF);
    write_u8(w, (v >> 8) & 0xFF);
    write_u8(w, v & 0xFF);
}

static void write_u32(DVIWriter& w, uint32_t v) {
    write_u8(w, (v >> 24) & 0xFF);
    write_u8(w, (v >> 16) & 0xFF);
    write_u8(w, (v >> 8) & 0xFF);
    write_u8(w, v & 0xFF);
}

static void write_i32(DVIWriter& w, int32_t v) {
    write_u32(w, (uint32_t)v);
}

// Write a signed value using minimum bytes needed
static void write_signed(DVIWriter& w, int32_t v, int base_opcode) {
    if (v >= -128 && v <= 127) {
        write_u8(w, base_opcode);
        write_u8(w, (uint8_t)(int8_t)v);
    } else if (v >= -32768 && v <= 32767) {
        write_u8(w, base_opcode + 1);
        write_u16(w, (uint16_t)(int16_t)v);
    } else if (v >= -8388608 && v <= 8388607) {
        write_u8(w, base_opcode + 2);
        write_u24(w, (uint32_t)v);
    } else {
        write_u8(w, base_opcode + 3);
        write_i32(w, v);
    }
}

// Write an unsigned value using minimum bytes needed
static void write_unsigned(DVIWriter& w, uint32_t v, int base_opcode) {
    if (v <= 255) {
        write_u8(w, base_opcode);
        write_u8(w, v);
    } else if (v <= 65535) {
        write_u8(w, base_opcode + 1);
        write_u16(w, v);
    } else if (v <= 16777215) {
        write_u8(w, base_opcode + 2);
        write_u24(w, v);
    } else {
        write_u8(w, base_opcode + 3);
        write_u32(w, v);
    }
}

// ============================================================================
// File Management
// ============================================================================

bool dvi_open(DVIWriter& writer, const char* filename, const DVIParams& params) {
    writer.file = fopen(filename, "wb");
    if (!writer.file) {
        log_error("tex_dvi_out: cannot open file %s for writing", filename);
        return false;
    }

    writer.params = params;
    writer.h = writer.v = 0;
    writer.w = writer.x = writer.y = writer.z = 0;
    writer.current_font = UINT32_MAX;  // No font selected yet
    writer.stack_depth = 0;
    writer.page_count = 0;
    writer.max_h = writer.max_v = 0;
    writer.max_push = 0;
    writer.byte_count = 0;

    // Allocate stack
    int stack_cap = params.max_stack_depth;
    writer.stack = (DVIWriter::State*)arena_alloc(writer.arena, stack_cap * sizeof(DVIWriter::State));
    writer.stack_depth = 0;

    // Allocate BOP offset array
    writer.bop_capacity = 64;
    writer.bop_offsets = (int32_t*)arena_alloc(writer.arena, writer.bop_capacity * sizeof(int32_t));

    // Allocate fonts array
    writer.font_capacity = 16;
    writer.fonts = (DVIFontEntry*)arena_alloc(writer.arena, writer.font_capacity * sizeof(DVIFontEntry));
    writer.font_count = 0;

    // Write preamble
    dvi_write_preamble(writer);

    return true;
}

bool dvi_close(DVIWriter& writer) {
    if (!writer.file) return false;

    // Write postamble
    dvi_write_postamble(writer);

    fclose(writer.file);
    writer.file = nullptr;

    log_debug("tex_dvi_out: wrote %ld bytes, %d pages", writer.byte_count, writer.page_count);
    return true;
}

// ============================================================================
// Preamble and Postamble
// ============================================================================

void dvi_write_preamble(DVIWriter& writer) {
    // PRE i[1] num[4] den[4] mag[4] k[1] x[k]
    write_u8(writer, DVI_PRE);
    write_u8(writer, 2);  // DVI format version 2
    write_u32(writer, writer.params.numerator);
    write_u32(writer, writer.params.denominator);
    write_u32(writer, writer.params.magnification);

    // Comment string
    const char* comment = writer.params.comment ? writer.params.comment : "";
    int len = strlen(comment);
    if (len > 255) len = 255;
    write_u8(writer, len);
    for (int i = 0; i < len; ++i) {
        write_u8(writer, comment[i]);
    }
}

void dvi_write_postamble(DVIWriter& writer) {
    writer.post_offset = writer.byte_count;

    // POST p[4] num[4] den[4] mag[4] l[4] u[4] s[2] t[2]
    write_u8(writer, DVI_POST);

    // Pointer to last BOP (or -1 if no pages)
    int32_t last_bop = writer.page_count > 0 ? writer.bop_offsets[writer.page_count - 1] : -1;
    write_i32(writer, last_bop);

    write_u32(writer, writer.params.numerator);
    write_u32(writer, writer.params.denominator);
    write_u32(writer, writer.params.magnification);

    // Maximum page height and width (in DVI units)
    write_u32(writer, writer.max_v);
    write_u32(writer, writer.max_h);

    // Maximum stack depth and total pages
    write_u16(writer, writer.max_push);
    write_u16(writer, writer.page_count);

    // Write font definitions again in postamble
    for (int i = 0; i < writer.font_count; ++i) {
        DVIFontEntry& f = writer.fonts[i];

        // FNT_DEF1-4 k[1-4] c[4] s[4] d[4] a[1] l[1] n[a+l]
        write_unsigned(writer, f.font_num, DVI_FNT_DEF1);
        write_u32(writer, f.checksum);
        write_u32(writer, f.scale);
        write_u32(writer, f.design_size);

        // Area (directory) - empty for standard fonts
        write_u8(writer, 0);

        // Font name
        int name_len = strlen(f.name);
        write_u8(writer, name_len);
        for (int j = 0; j < name_len; ++j) {
            write_u8(writer, f.name[j]);
        }
    }

    // POST_POST q[4] i[1] 223...223
    write_u8(writer, DVI_POST_POST);
    write_u32(writer, (uint32_t)writer.post_offset);
    write_u8(writer, 2);  // DVI format version

    // Pad to 4-byte boundary with 223s
    while (writer.byte_count % 4 != 0) {
        write_u8(writer, 223);
    }
    // Add at least 4 more 223s
    for (int i = 0; i < 4; ++i) {
        write_u8(writer, 223);
    }
}

// ============================================================================
// Page Commands
// ============================================================================

void dvi_begin_page(DVIWriter& writer, int32_t c0, int32_t c1, int32_t c2,
                    int32_t c3, int32_t c4, int32_t c5,
                    int32_t c6, int32_t c7, int32_t c8, int32_t c9) {
    // Record BOP offset
    if (writer.page_count >= writer.bop_capacity) {
        // Grow array
        int new_cap = writer.bop_capacity * 2;
        int32_t* new_offsets = (int32_t*)arena_alloc(writer.arena, new_cap * sizeof(int32_t));
        memcpy(new_offsets, writer.bop_offsets, writer.page_count * sizeof(int32_t));
        writer.bop_offsets = new_offsets;
        writer.bop_capacity = new_cap;
    }
    writer.bop_offsets[writer.page_count] = (int32_t)writer.byte_count;

    // BOP c0[4] ... c9[4] p[4]
    write_u8(writer, DVI_BOP);
    write_i32(writer, c0);
    write_i32(writer, c1);
    write_i32(writer, c2);
    write_i32(writer, c3);
    write_i32(writer, c4);
    write_i32(writer, c5);
    write_i32(writer, c6);
    write_i32(writer, c7);
    write_i32(writer, c8);
    write_i32(writer, c9);

    // Pointer to previous BOP
    int32_t prev_bop = writer.page_count > 0 ? writer.bop_offsets[writer.page_count - 1] : -1;
    write_i32(writer, prev_bop);

    // Reset state for new page
    writer.h = writer.v = 0;
    writer.w = writer.x = writer.y = writer.z = 0;
    writer.stack_depth = 0;

    writer.page_count++;
}

void dvi_end_page(DVIWriter& writer) {
    write_u8(writer, DVI_EOP);
}

// ============================================================================
// Font Commands
// ============================================================================

uint32_t dvi_define_font(DVIWriter& writer, const char* name, float size_pt, uint32_t checksum) {
    // Check if font already defined
    for (int i = 0; i < writer.font_count; ++i) {
        if (strcmp(writer.fonts[i].name, name) == 0 &&
            fabs(writer.fonts[i].size_pt - size_pt) < 0.01f) {
            return writer.fonts[i].font_num;
        }
    }

    // Grow array if needed
    if (writer.font_count >= writer.font_capacity) {
        int new_cap = writer.font_capacity * 2;
        DVIFontEntry* new_fonts = (DVIFontEntry*)arena_alloc(writer.arena, new_cap * sizeof(DVIFontEntry));
        memcpy(new_fonts, writer.fonts, writer.font_count * sizeof(DVIFontEntry));
        writer.fonts = new_fonts;
        writer.font_capacity = new_cap;
    }

    uint32_t font_num = writer.font_count;
    DVIFontEntry& f = writer.fonts[writer.font_count++];

    f.font_num = font_num;
    f.name = name;  // Assume name is arena-allocated or static
    f.size_pt = size_pt;
    f.checksum = checksum;

    // Scale and design size in scaled points
    // Design size is typically 10pt for CM fonts
    float design_pt = 10.0f;
    f.design_size = pt_to_sp(design_pt);
    f.scale = pt_to_sp(size_pt);

    // Write font definition
    // FNT_DEF1-4 k[1-4] c[4] s[4] d[4] a[1] l[1] n[a+l]
    write_unsigned(writer, font_num, DVI_FNT_DEF1);
    write_u32(writer, f.checksum);
    write_u32(writer, f.scale);
    write_u32(writer, f.design_size);

    // Area (directory) - empty
    write_u8(writer, 0);

    // Font name
    int name_len = strlen(name);
    write_u8(writer, name_len);
    for (int j = 0; j < name_len; ++j) {
        write_u8(writer, name[j]);
    }

    return font_num;
}

void dvi_select_font(DVIWriter& writer, uint32_t font_num) {
    if (font_num == writer.current_font) return;

    if (font_num < 64) {
        write_u8(writer, DVI_FNT_NUM_0 + font_num);
    } else {
        write_unsigned(writer, font_num, DVI_FNT1);
    }
    writer.current_font = font_num;
}

// ============================================================================
// Character Output
// ============================================================================

void dvi_set_char(DVIWriter& writer, int32_t c) {
    if (c >= 0 && c <= 127) {
        write_u8(writer, c);  // SET_CHAR_0 to SET_CHAR_127
    } else {
        write_unsigned(writer, (uint32_t)c, DVI_SET1);
    }
    // Note: Character width advancement should be handled by caller
}

void dvi_put_char(DVIWriter& writer, int32_t c) {
    write_unsigned(writer, (uint32_t)c, DVI_PUT1);
}

// ============================================================================
// Rules
// ============================================================================

void dvi_set_rule(DVIWriter& writer, int32_t height, int32_t width) {
    write_u8(writer, DVI_SET_RULE);
    write_i32(writer, height);
    write_i32(writer, width);
    // Advances h by width
}

void dvi_put_rule(DVIWriter& writer, int32_t height, int32_t width) {
    write_u8(writer, DVI_PUT_RULE);
    write_i32(writer, height);
    write_i32(writer, width);
}

// ============================================================================
// Movement Commands
// ============================================================================

void dvi_right(DVIWriter& writer, int32_t b) {
    if (b == 0) return;
    write_signed(writer, b, DVI_RIGHT1);
    writer.h += b;
    if (writer.h > writer.max_h) writer.max_h = writer.h;
}

void dvi_down(DVIWriter& writer, int32_t a) {
    if (a == 0) return;
    write_signed(writer, a, DVI_DOWN1);
    writer.v += a;
    if (writer.v > writer.max_v) writer.max_v = writer.v;
}

void dvi_set_h(DVIWriter& writer, int32_t h) {
    int32_t delta = h - writer.h;
    if (delta != 0) {
        dvi_right(writer, delta);
    }
}

void dvi_set_v(DVIWriter& writer, int32_t v) {
    int32_t delta = v - writer.v;
    if (delta != 0) {
        dvi_down(writer, delta);
    }
}

// ============================================================================
// Stack Commands
// ============================================================================

void dvi_push(DVIWriter& writer) {
    if (writer.stack_depth >= writer.params.max_stack_depth) {
        log_error("tex_dvi_out: stack overflow");
        return;
    }

    DVIWriter::State& s = writer.stack[writer.stack_depth++];
    s.h = writer.h;
    s.v = writer.v;
    s.w = writer.w;
    s.x = writer.x;
    s.y = writer.y;
    s.z = writer.z;
    s.f = writer.current_font;

    if (writer.stack_depth > writer.max_push) {
        writer.max_push = writer.stack_depth;
    }

    write_u8(writer, DVI_PUSH);
}

void dvi_pop(DVIWriter& writer) {
    if (writer.stack_depth == 0) {
        log_error("tex_dvi_out: stack underflow");
        return;
    }

    DVIWriter::State& s = writer.stack[--writer.stack_depth];
    writer.h = s.h;
    writer.v = s.v;
    writer.w = s.w;
    writer.x = s.x;
    writer.y = s.y;
    writer.z = s.z;
    writer.current_font = s.f;

    write_u8(writer, DVI_POP);
}

// ============================================================================
// Special Commands
// ============================================================================

void dvi_special(DVIWriter& writer, const char* str, int len) {
    if (len <= 0) return;
    write_unsigned(writer, (uint32_t)len, DVI_XXX1);
    for (int i = 0; i < len; ++i) {
        write_u8(writer, str[i]);
    }
}

// ============================================================================
// Node Tree Traversal
// ============================================================================

void dvi_output_node(DVIWriter& writer, TexNode* node, TFMFontManager* fonts) {
    if (!node) return;

    switch (node->node_class) {
        case NodeClass::Char: {
            // Ensure correct font is selected
            const char* font_name = node->content.ch.font.name;
            float font_size = node->content.ch.font.size_pt;
            if (font_name) {
                uint32_t font_num = dvi_define_font(writer, font_name, font_size);
                dvi_select_font(writer, font_num);
            }

            // Translate Unicode/ASCII to font-specific encoding
            int32_t font_cp = translate_to_font_encoding(font_name, node->content.ch.codepoint);
            dvi_set_char(writer, font_cp);

            // Advance by character width
            int32_t width_sp = pt_to_sp(node->width);
            writer.h += width_sp;
            if (writer.h > writer.max_h) writer.max_h = writer.h;
            break;
        }

        case NodeClass::Ligature: {
            const char* font_name = node->content.lig.font.name;
            float font_size = node->content.lig.font.size_pt;
            if (font_name) {
                uint32_t font_num = dvi_define_font(writer, font_name, font_size);
                dvi_select_font(writer, font_num);
            }

            // Translate Unicode/ASCII to font-specific encoding
            int32_t font_cp = translate_to_font_encoding(font_name, node->content.lig.codepoint);
            dvi_set_char(writer, font_cp);

            int32_t width_sp = pt_to_sp(node->width);
            writer.h += width_sp;
            if (writer.h > writer.max_h) writer.max_h = writer.h;
            break;
        }

        case NodeClass::MathChar: {
            // Math character - similar to regular Char but uses MathChar font
            const char* font_name = node->content.math_char.font.name;
            float font_size = node->content.math_char.font.size_pt;
            if (font_name) {
                uint32_t font_num = dvi_define_font(writer, font_name, font_size);
                dvi_select_font(writer, font_num);
            }

            // Translate Unicode/ASCII to font-specific encoding
            int32_t orig_cp = node->content.math_char.codepoint;
            int32_t font_cp = translate_to_font_encoding(font_name, orig_cp);
            log_debug("tex_dvi_out: MathChar font=%s orig=%d (0x%02x '%c') -> %d",
                      font_name ? font_name : "null", orig_cp, orig_cp, 
                      (orig_cp >= 32 && orig_cp < 127) ? orig_cp : '?', font_cp);
            dvi_set_char(writer, font_cp);

            int32_t width_sp = pt_to_sp(node->width);
            writer.h += width_sp;
            if (writer.h > writer.max_h) writer.max_h = writer.h;
            break;
        }

        case NodeClass::MathOp: {
            // Large operator - similar to MathChar
            const char* font_name = node->content.math_op.font.name;
            float font_size = node->content.math_op.font.size_pt;
            if (font_name) {
                uint32_t font_num = dvi_define_font(writer, font_name, font_size);
                dvi_select_font(writer, font_num);
            }

            // Translate Unicode/ASCII to font-specific encoding
            int32_t font_cp = translate_to_font_encoding(font_name, node->content.math_op.codepoint);
            dvi_set_char(writer, font_cp);

            int32_t width_sp = pt_to_sp(node->width);
            writer.h += width_sp;
            if (writer.h > writer.max_h) writer.max_h = writer.h;
            break;
        }

        case NodeClass::Glue: {
            // In DVI, glue becomes fixed space after layout
            int32_t width_sp = pt_to_sp(node->width);
            if (width_sp != 0) {
                dvi_right(writer, width_sp);
            }
            break;
        }

        case NodeClass::Kern: {
            int32_t amount_sp = pt_to_sp(node->content.kern.amount);
            if (amount_sp != 0) {
                dvi_right(writer, amount_sp);
            }
            break;
        }

        case NodeClass::Rule: {
            int32_t w_sp = pt_to_sp(node->width);
            int32_t h_sp = pt_to_sp(node->height + node->depth);
            dvi_set_rule(writer, h_sp, w_sp);
            break;
        }

        case NodeClass::HList:
        case NodeClass::HBox: {
            dvi_output_hlist(writer, node, fonts);
            break;
        }

        case NodeClass::VList:
        case NodeClass::VBox: {
            dvi_output_vlist(writer, node, fonts);
            break;
        }

        case NodeClass::Scripts: {
            // Scripts node contains nucleus, subscript, superscript as children
            // Each child has x,y offsets relative to the scripts node origin
            // Output each child at its positioned location
            int child_count = 0;
            for (TexNode* child = node->first_child; child; child = child->next_sibling) {
                child_count++;
                // Save position
                int32_t save_h = writer.h;
                int32_t save_v = writer.v;

                // Move to child position (y is baseline-relative, positive = up)
                writer.h = save_h + pt_to_sp(child->x);
                writer.v = save_v - pt_to_sp(child->y);  // DVI y increases downward

                log_debug("tex_dvi_out: Scripts child %d: class=%d x=%.1f y=%.1f",
                         child_count, (int)child->node_class, child->x, child->y);

                // Output child
                dvi_output_node(writer, child, fonts);

                // Restore position
                writer.h = save_h;
                writer.v = save_v;
            }
            log_debug("tex_dvi_out: Scripts node with %d children, width=%.1f",
                     child_count, node->width);
            // Advance by total width
            writer.h += pt_to_sp(node->width);
            break;
        }

        case NodeClass::Radical: {
            // Radical node: output degree (if any) + sqrt sign + radicand
            // TeX outputs in order: degree, radical sign, radicand
            // The radical sign is character 112 ('p') in cmsy10
            int32_t save_h = writer.h;
            int32_t save_v = writer.v;

            log_debug("tex_dvi_out: Radical node width=%.1f, has_degree=%d, has_radicand=%d",
                     node->width,
                     node->content.radical.degree ? 1 : 0,
                     node->content.radical.radicand ? 1 : 0);

            // Determine radical sign position
            // If degree is present, radical sign comes after the degree
            TexNode* degree = node->content.radical.degree;
            float rad_sign_offset = 0.0f;
            
            // First output the degree (root index) if present
            // The degree appears BEFORE the radical sign in TeX's glyph order
            if (degree) {
                int32_t deg_h = save_h + pt_to_sp(degree->x);
                int32_t deg_v = save_v - pt_to_sp(degree->y);

                log_debug("tex_dvi_out: Radical degree x=%.1f y=%.1f width=%.1f",
                         degree->x, degree->y, degree->width);

                writer.h = deg_h;
                writer.v = deg_v;
                dvi_output_node(writer, degree, fonts);

                // Radical sign comes after the degree
                rad_sign_offset = degree->x + degree->width;
                
                log_debug("tex_dvi_out: Radical rad_sign_offset=%.1f", rad_sign_offset);
                
                // Restore position
                writer.h = save_h;
                writer.v = save_v;
            }

            // Output the radical sign - select font and glyph based on total radical height
            // TeX uses cmsy10 position 112 for small radicals
            // and cmex10 positions 112-118 for larger ones (higher position = larger glyph)
            // The height includes the radicand plus the rule and clearance above it
            float total_height = node->height + node->depth;  // Total radical height
            
            // Determine font and glyph based on radical size
            const char* radical_font = "cmsy10";
            int radical_glyph = 112;  // Default: cmsy10 surd (smallest)
            
            // TeX threshold for switching to cmex10 is approximately 8pt total height
            // The cmex10 radical glyphs scale with the content:
            // Thresholds tuned to match TeX's glyph selection behavior
            if (total_height > 8.0f) {
                radical_font = "cmex10";
                // Select glyph based on total height (tuned thresholds):
                // 112 = smallest cmex10 surd (~8-9pt)
                // 113 = (~9-10pt)
                // 114 = (~10-11pt)
                // 115 = (~11-12pt)
                // 116 = (~12-14pt)
                // 117 = (~14-16pt)
                // 118 = (~16pt and above)
                if (total_height > 16.0f) radical_glyph = 118;
                else if (total_height > 14.0f) radical_glyph = 117;
                else if (total_height > 12.0f) radical_glyph = 116;
                else if (total_height > 11.0f) radical_glyph = 115;
                else if (total_height > 10.0f) radical_glyph = 114;
                else if (total_height > 9.0f) radical_glyph = 113;
                else radical_glyph = 112;
            }
            
            log_debug("tex_dvi_out: Radical total_height=%.1f, font=%s, glyph=%d", 
                     total_height, radical_font, radical_glyph);

            float size = node->height * 10.0f;  // Approximate size
            if (size < 5.0f) size = 10.0f;  // Default to 10pt

            uint32_t font_num = dvi_define_font(writer, radical_font, size);
            dvi_select_font(writer, font_num);

            // Move to radical sign position
            writer.h = save_h + pt_to_sp(rad_sign_offset);
            
            log_debug("tex_dvi_out: Outputting radical sign at offset=%.1f", rad_sign_offset);
            
            // Output radical sign character
            dvi_set_char(writer, radical_glyph);

            // Output radicand
            TexNode* radicand = node->content.radical.radicand;
            if (radicand) {
                // Position radicand at its x position from the radical node origin
                int32_t rad_h = save_h + pt_to_sp(radicand->x);
                int32_t rad_v = save_v - pt_to_sp(radicand->y);

                log_debug("tex_dvi_out: Radicand x=%.1f y=%.1f", radicand->x, radicand->y);

                writer.h = rad_h;
                writer.v = rad_v;
                dvi_output_node(writer, radicand, fonts);
            }

            // Advance by total width
            writer.h = save_h + pt_to_sp(node->width);
            break;
        }

        case NodeClass::Delimiter: {
            // Output a delimiter character using TFM-based selection (TeXBook p.152)
            int32_t cp = node->content.delim.codepoint;
            float target_size = node->content.delim.target_size;
            
            log_debug("tex_dvi_out: Delimiter codepoint=%d '%c' size=%.1f", cp, cp, target_size);
            
            float font_size = 10.0f;
            const char* font_name = "cmr10";
            int32_t output_cp = cp;
            
            // Use TFM-based delimiter selection if font manager available
            if (fonts) {
                DelimiterSelection sel = select_delimiter(fonts, cp, target_size, font_size);
                font_name = sel.font_name;
                output_cp = sel.codepoint;
                
                log_debug("tex_dvi_out: TFM selected font=%s pos=%d (h=%.1f d=%.1f ext=%d)",
                          font_name, output_cp, sel.height, sel.depth, sel.is_extensible);
                
                // For extensible delimiters, we'd need to output multiple pieces
                // For now, just use the base character (extensible building is TODO)
                if (sel.is_extensible) {
                    log_debug("tex_dvi_out: extensible recipe: top=%d mid=%d bot=%d rep=%d",
                              sel.recipe.top, sel.recipe.mid, sel.recipe.bot, sel.recipe.rep);
                    // TODO: Build extensible from pieces for very large delimiters
                }
            } else {
                // Fallback: use simple hardcoded mapping for small delimiters
                switch (cp) {
                    case '{':
                        font_name = "cmsy10";
                        output_cp = 102;
                        break;
                    case '}':
                        font_name = "cmsy10";
                        output_cp = 103;
                        break;
                    case '|':
                        if (target_size > 10.0f) {
                            font_name = "cmex10";
                            output_cp = 12;
                        } else {
                            font_name = "cmsy10";
                            output_cp = 106;
                        }
                        break;
                    default:
                        // Parens, brackets: use cmex10 for larger sizes
                        if (target_size > 10.0f) {
                            font_name = "cmex10";
                            // Map to cmex10 small positions
                            if (cp == '(') output_cp = 0;
                            else if (cp == ')') output_cp = 1;
                            else if (cp == '[') output_cp = 2;
                            else if (cp == ']') output_cp = 3;
                        }
                        break;
                }
            }
            
            uint32_t font_num = dvi_define_font(writer, font_name, font_size);
            dvi_select_font(writer, font_num);
            dvi_set_char(writer, output_cp);
            
            writer.h += pt_to_sp(node->width);
            break;
        }

        case NodeClass::Accent: {
            // Math accent - output accent first, then base (for correct text extraction order)
            TexNode* base = node->content.accent.base;
            int32_t accent_char = node->content.accent.accent_char;
            FontSpec accent_font = node->content.accent.font;
            
            int32_t save_h = writer.h;
            int32_t save_v = writer.v;
            
            // Calculate base metrics
            float base_width = base ? base->width : 5.0f;
            float accent_width = 5.0f;  // Approximate accent width
            
            // Select accent font (typically cmsy10 or cmmi10)
            const char* font_name = accent_font.name ? accent_font.name : "cmmi10";
            float font_size = accent_font.size_pt > 0 ? accent_font.size_pt : 10.0f;
            
            uint32_t font_num = dvi_define_font(writer, font_name, font_size);
            dvi_select_font(writer, font_num);
            
            // Map accent character to font encoding
            int32_t output_cp = accent_char;
            if (accent_char == 0x2192) {
                // Vector arrow - use cmmi10 vector accent at position 126
                output_cp = 126;  // ~
            } else if (accent_char == '^') {
                output_cp = 94;   // circumflex/hat
            } else if (accent_char == '-') {
                output_cp = 22;   // macron/bar
            } else if (accent_char == '~') {
                output_cp = 126;  // tilde
            } else if (accent_char == '.') {
                output_cp = 95;   // overdot
            }
            
            // Output accent FIRST (positioned above base)
            int32_t accent_h = save_h + pt_to_sp((base_width - accent_width) / 2.0f);
            int32_t accent_v = save_v - pt_to_sp(base ? base->height : 5.0f);
            
            writer.h = accent_h;
            writer.v = accent_v;
            dvi_set_char(writer, output_cp);
            
            // Restore position for base output
            writer.h = save_h;
            writer.v = save_v;
            
            // Output the base
            if (base) {
                dvi_output_node(writer, base, fonts);
            }
            
            // Position after full width
            writer.h = save_h + pt_to_sp(node->width);
            break;
        }

        case NodeClass::Penalty:
            // Penalties are invisible
            break;

        default:
            // Skip other node types for now
            log_debug("tex_dvi_out: unhandled node class %d", (int)node->node_class);
            break;
    }
}

void dvi_output_hlist(DVIWriter& writer, TexNode* hlist, TFMFontManager* fonts) {
    if (!hlist) return;

    dvi_push(writer);

    // Process children left to right
    for (TexNode* child = hlist->first_child; child; child = child->next_sibling) {
        dvi_output_node(writer, child, fonts);
    }

    dvi_pop(writer);

    // Advance by box width
    int32_t width_sp = pt_to_sp(hlist->width);
    writer.h += width_sp;
    if (writer.h > writer.max_h) writer.max_h = writer.h;
}

void dvi_output_vlist(DVIWriter& writer, TexNode* vlist, TFMFontManager* fonts) {
    if (!vlist) return;

    dvi_push(writer);

    // Process children top to bottom
    for (TexNode* child = vlist->first_child; child; child = child->next_sibling) {
        // Move down by child's height
        if (child->height > 0) {
            dvi_down(writer, pt_to_sp(child->height));
        }

        if (child->node_class == NodeClass::Glue) {
            // Vertical glue becomes fixed space
            int32_t glue_sp = pt_to_sp(child->content.glue.spec.space);
            if (glue_sp > 0) {
                dvi_down(writer, glue_sp);
            }
        } else if (child->node_class == NodeClass::Kern) {
            int32_t kern_sp = pt_to_sp(child->content.kern.amount);
            if (kern_sp != 0) {
                dvi_down(writer, kern_sp);
            }
        } else if (child->node_class == NodeClass::HBox || child->node_class == NodeClass::HList) {
            // Output horizontal content
            dvi_push(writer);
            for (TexNode* item = child->first_child; item; item = item->next_sibling) {
                dvi_output_node(writer, item, fonts);
            }
            dvi_pop(writer);

            // Move down by depth
            if (child->depth > 0) {
                dvi_down(writer, pt_to_sp(child->depth));
            }
        } else if (child->node_class == NodeClass::Rule) {
            int32_t w_sp = pt_to_sp(child->width);
            int32_t h_sp = pt_to_sp(child->height + child->depth);
            dvi_put_rule(writer, h_sp, w_sp);
            dvi_down(writer, pt_to_sp(child->depth));
        } else {
            // Other node types
            dvi_output_node(writer, child, fonts);
            if (child->depth > 0) {
                dvi_down(writer, pt_to_sp(child->depth));
            }
        }
    }

    dvi_pop(writer);
}

// ============================================================================
// High-Level API
// ============================================================================

bool dvi_write_page(
    DVIWriter& writer,
    TexNode* page_vlist,
    int page_number,
    TFMFontManager* fonts
) {
    if (!page_vlist) return false;

    dvi_begin_page(writer, page_number);

    // Start at top-left with 1-inch margins (72 points = 4736286 sp at 1000x mag)
    int32_t margin_sp = pt_to_sp(72.0f);
    dvi_right(writer, margin_sp);
    dvi_down(writer, margin_sp);

    dvi_output_vlist(writer, page_vlist, fonts);

    dvi_end_page(writer);

    return true;
}

bool dvi_write_document(
    DVIWriter& writer,
    PageContent* pages,
    int page_count,
    TFMFontManager* fonts
) {
    for (int i = 0; i < page_count; ++i) {
        if (!dvi_write_page(writer, pages[i].vlist, i + 1, fonts)) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Convenience Functions
// ============================================================================

bool write_dvi_file(
    const char* filename,
    PageContent* pages,
    int page_count,
    TFMFontManager* fonts,
    Arena* arena,
    const DVIParams& params
) {
    DVIWriter writer(arena);

    if (!dvi_open(writer, filename, params)) {
        return false;
    }

    bool success = dvi_write_document(writer, pages, page_count, fonts);

    dvi_close(writer);

    return success;
}

bool write_dvi_page(
    const char* filename,
    TexNode* vlist,
    TFMFontManager* fonts,
    Arena* arena,
    const DVIParams& params
) {
    DVIWriter writer(arena);

    if (!dvi_open(writer, filename, params)) {
        return false;
    }

    bool success = dvi_write_page(writer, vlist, 1, fonts);

    dvi_close(writer);

    return success;
}

// ============================================================================
// Debugging
// ============================================================================

void dump_dvi_writer_state(const DVIWriter& writer) {
    log_debug("DVI Writer State:");
    log_debug("  Position: h=%d v=%d", writer.h, writer.v);
    log_debug("  Registers: w=%d x=%d y=%d z=%d", writer.w, writer.x, writer.y, writer.z);
    log_debug("  Font: %u", writer.current_font);
    log_debug("  Stack depth: %d", writer.stack_depth);
    log_debug("  Pages: %d", writer.page_count);
    log_debug("  Fonts defined: %d", writer.font_count);
    log_debug("  Max h=%d v=%d push=%d", writer.max_h, writer.max_v, writer.max_push);
    log_debug("  Bytes written: %ld", writer.byte_count);
}

} // namespace tex
