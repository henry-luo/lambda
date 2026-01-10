// tex_dvi_out.hpp - DVI Output Generation for TeX Typesetting
//
// Generates DVI (DeVice Independent) files from TexNode trees.
// DVI is the standard output format from TeX and can be processed
// by various drivers (dvips, dvipdfm, xdvi, etc.)
//
// Reference: TeXBook Appendix A, DVI format specification

#ifndef TEX_DVI_OUT_HPP
#define TEX_DVI_OUT_HPP

#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "tex_vlist.hpp"
#include "tex_pagebreak.hpp"
#include "../../lib/arena.h"
#include <cstdint>
#include <cstdio>

namespace tex {

// ============================================================================
// DVI Output Parameters
// ============================================================================

struct DVIParams {
    // Document identification
    const char* comment;          // Comment string in preamble (max 255 chars)

    // Units: numerator/denominator define DVI units
    // Default: num=25400000, den=473628672 gives 1 DVI unit = 1sp
    uint32_t numerator;
    uint32_t denominator;

    // Magnification (1000 = normal, 2000 = double size)
    uint32_t magnification;

    // Maximum stack depth (for push/pop)
    uint16_t max_stack_depth;

    // Create with TeX default values
    static DVIParams defaults() {
        DVIParams p = {};
        p.comment = "Lambda Script TeX Output";
        p.numerator = 25400000;      // Standard TeX numerator
        p.denominator = 473628672;   // Standard TeX denominator
        p.magnification = 1000;      // Normal size
        p.max_stack_depth = 100;     // Should be plenty
        return p;
    }
};

// ============================================================================
// DVI Font Entry
// ============================================================================

struct DVIFontEntry {
    uint32_t font_num;            // Font number in DVI file
    const char* name;             // Font name (e.g., "cmr10")
    float size_pt;                // Size in points
    uint32_t checksum;            // TFM checksum (0 if unknown)
    uint32_t scale;               // Scale factor (scaled points)
    uint32_t design_size;         // Design size (scaled points)
};

// ============================================================================
// DVI Writer Context
// ============================================================================

struct DVIWriter {
    Arena* arena;
    FILE* file;
    DVIParams params;

    // Current position in DVI units (scaled points)
    int32_t h, v;                 // Horizontal, vertical position
    int32_t w, x, y, z;           // Movement registers
    uint32_t current_font;        // Current font number

    // Stack for push/pop
    struct State {
        int32_t h, v, w, x, y, z;
        uint32_t f;
    };
    State* stack;
    int stack_depth;

    // Font tracking
    DVIFontEntry* fonts;
    int font_count;
    int font_capacity;

    // Page tracking
    int page_count;
    int32_t* bop_offsets;         // File offset of each BOP
    int bop_capacity;

    // Statistics
    int32_t max_h, max_v;         // Maximum positions seen
    uint16_t max_push;            // Maximum stack depth used
    long post_offset;             // Offset of POST command

    // Byte counter
    long byte_count;

    DVIWriter(Arena* a) : arena(a), file(nullptr), h(0), v(0), w(0), x(0), y(0), z(0),
        current_font(0), stack(nullptr), stack_depth(0), fonts(nullptr),
        font_count(0), font_capacity(0), page_count(0), bop_offsets(nullptr),
        bop_capacity(0), max_h(0), max_v(0), max_push(0), post_offset(0), byte_count(0) {}
};

// ============================================================================
// Main DVI Output API
// ============================================================================

// Open DVI file for writing
bool dvi_open(DVIWriter& writer, const char* filename, const DVIParams& params);

// Close DVI file (writes postamble and finalizes)
bool dvi_close(DVIWriter& writer);

// Write a complete document (array of PageContent from page breaking)
bool dvi_write_document(
    DVIWriter& writer,
    PageContent* pages,
    int page_count,
    TFMFontManager* fonts
);

// Write a single page
bool dvi_write_page(
    DVIWriter& writer,
    TexNode* page_vlist,
    int page_number,
    TFMFontManager* fonts
);

// ============================================================================
// Low-Level DVI Commands
// ============================================================================

// Preamble/Postamble
void dvi_write_preamble(DVIWriter& writer);
void dvi_write_postamble(DVIWriter& writer);

// Page commands
void dvi_begin_page(DVIWriter& writer, int32_t c0, int32_t c1 = 0, int32_t c2 = 0,
                    int32_t c3 = 0, int32_t c4 = 0, int32_t c5 = 0,
                    int32_t c6 = 0, int32_t c7 = 0, int32_t c8 = 0, int32_t c9 = 0);
void dvi_end_page(DVIWriter& writer);

// Font commands
uint32_t dvi_define_font(DVIWriter& writer, const char* name, float size_pt,
                         uint32_t checksum = 0);
void dvi_select_font(DVIWriter& writer, uint32_t font_num);

// Character output
void dvi_set_char(DVIWriter& writer, int32_t c);    // Set char and advance
void dvi_put_char(DVIWriter& writer, int32_t c);    // Put char without advance

// Rules
void dvi_set_rule(DVIWriter& writer, int32_t height, int32_t width);  // Set and advance
void dvi_put_rule(DVIWriter& writer, int32_t height, int32_t width);  // Put without advance

// Movement commands
void dvi_right(DVIWriter& writer, int32_t b);       // Move right by b
void dvi_down(DVIWriter& writer, int32_t a);        // Move down by a
void dvi_set_h(DVIWriter& writer, int32_t h);       // Set h directly
void dvi_set_v(DVIWriter& writer, int32_t v);       // Set v directly

// Stack commands
void dvi_push(DVIWriter& writer);
void dvi_pop(DVIWriter& writer);

// Special commands (for extensions like color, hyperlinks)
void dvi_special(DVIWriter& writer, const char* str, int len);

// ============================================================================
// Node Tree Traversal
// ============================================================================

// Output a VList (vertical list) - main page content
void dvi_output_vlist(DVIWriter& writer, TexNode* vlist, TFMFontManager* fonts);

// Output an HList/HBox (horizontal list)
void dvi_output_hlist(DVIWriter& writer, TexNode* hlist, TFMFontManager* fonts);

// Output a single node
void dvi_output_node(DVIWriter& writer, TexNode* node, TFMFontManager* fonts);

// ============================================================================
// Unit Conversion
// ============================================================================

// Convert points to DVI scaled points
inline int32_t pt_to_sp(float pt) {
    return (int32_t)(pt * 65536.0f);
}

// Convert DVI scaled points to points
inline float sp_to_pt(int32_t sp) {
    return sp / 65536.0f;
}

// ============================================================================
// Convenience Functions
// ============================================================================

// Write a complete document to a DVI file
bool write_dvi_file(
    const char* filename,
    PageContent* pages,
    int page_count,
    TFMFontManager* fonts,
    Arena* arena,
    const DVIParams& params = DVIParams::defaults()
);

// Write a single VList (e.g., one page) to a DVI file
bool write_dvi_page(
    const char* filename,
    TexNode* vlist,
    TFMFontManager* fonts,
    Arena* arena,
    const DVIParams& params = DVIParams::defaults()
);

// ============================================================================
// Debugging
// ============================================================================

void dump_dvi_writer_state(const DVIWriter& writer);

} // namespace tex

#endif // TEX_DVI_OUT_HPP
