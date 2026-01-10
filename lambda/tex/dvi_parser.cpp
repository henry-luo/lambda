// dvi_parser.cpp - DVI File Parser Implementation
//
// Parses DVI files for comparison with Lambda's typesetting output.

#include "dvi_parser.hpp"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"

#include <cstring>
#include <cstdlib>

namespace tex {
namespace dvi {

// ============================================================================
// DVIParser Implementation
// ============================================================================

DVIParser::DVIParser(Arena* arena)
    : arena_(arena)
    , data_(nullptr)
    , size_(0)
    , pos_(0)
    , fonts_(nullptr)
    , font_count_(0)
    , font_capacity_(0)
    , pages_(nullptr)
    , page_count_(0)
    , page_capacity_(0)
    , error_(nullptr)
    , state_stack_(nullptr)
    , stack_depth_(0)
    , stack_capacity_(0)
{
    memset(&preamble_, 0, sizeof(preamble_));
    memset(&state_, 0, sizeof(state_));
}

DVIParser::~DVIParser() {
    // arena handles memory cleanup
}

bool DVIParser::parse_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        set_error("Cannot open file");
        return false;
    }

    // get file size
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // read entire file
    uint8_t* data = (uint8_t*)arena_alloc(arena_, fsize);
    if (fread(data, 1, fsize, f) != fsize) {
        fclose(f);
        set_error("Failed to read file");
        return false;
    }
    fclose(f);

    return parse(data, fsize);
}

bool DVIParser::parse(const uint8_t* data, size_t size) {
    data_ = data;
    size_ = size;
    pos_ = 0;

    if (size < 15) {
        set_error("File too small to be valid DVI");
        return false;
    }

    // parse preamble
    if (!parse_preamble()) {
        return false;
    }

    // find and parse postamble (to get font definitions)
    if (!parse_postamble()) {
        return false;
    }

    // parse pages
    if (!parse_pages()) {
        return false;
    }

    log_info("DVI parser: parsed %d pages, %d fonts", page_count_, font_count_);
    return true;
}

const DVIPage* DVIParser::page(int index) const {
    if (index < 0 || index >= page_count_) return nullptr;
    return &pages_[index];
}

const DVIFont* DVIParser::font(uint32_t font_num) const {
    for (int i = 0; i < font_count_; i++) {
        if (fonts_[i].font_num == font_num) {
            return &fonts_[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Reading Helpers
// ============================================================================

uint8_t DVIParser::read_u8() {
    if (pos_ >= size_) return 0;
    return data_[pos_++];
}

int8_t DVIParser::read_i8() {
    return (int8_t)read_u8();
}

uint16_t DVIParser::read_u16() {
    uint16_t b0 = read_u8();
    uint16_t b1 = read_u8();
    return (b0 << 8) | b1;
}

int16_t DVIParser::read_i16() {
    return (int16_t)read_u16();
}

uint32_t DVIParser::read_u24() {
    uint32_t b0 = read_u8();
    uint32_t b1 = read_u8();
    uint32_t b2 = read_u8();
    return (b0 << 16) | (b1 << 8) | b2;
}

int32_t DVIParser::read_i24() {
    int32_t val = read_u24();
    // sign extend from 24 bits
    if (val & 0x800000) {
        val |= 0xFF000000;
    }
    return val;
}

uint32_t DVIParser::read_u32() {
    uint32_t b0 = read_u8();
    uint32_t b1 = read_u8();
    uint32_t b2 = read_u8();
    uint32_t b3 = read_u8();
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

int32_t DVIParser::read_i32() {
    return (int32_t)read_u32();
}

char* DVIParser::read_string(int len) {
    if (len <= 0) return nullptr;
    char* str = (char*)arena_alloc(arena_, len + 1);
    for (int i = 0; i < len && pos_ < size_; i++) {
        str[i] = data_[pos_++];
    }
    str[len] = '\0';
    return str;
}

void DVIParser::set_error(const char* msg) {
    error_ = (char*)arena_alloc(arena_, strlen(msg) + 1);
    strcpy(error_, msg);
    log_error("DVI parser: %s at position %zu", msg, pos_);
}

// ============================================================================
// Preamble Parsing
// ============================================================================

bool DVIParser::parse_preamble() {
    pos_ = 0;

    uint8_t opcode = read_u8();
    if (opcode != DVI_PRE) {
        set_error("Expected PRE opcode at start");
        return false;
    }

    preamble_.id = read_u8();
    if (preamble_.id != 2) {
        set_error("Unsupported DVI format version");
        return false;
    }

    preamble_.num = read_u32();
    preamble_.den = read_u32();
    preamble_.mag = read_u32();

    uint8_t k = read_u8();
    preamble_.comment = read_string(k);

    log_debug("DVI preamble: num=%u, den=%u, mag=%u, comment='%s'",
              preamble_.num, preamble_.den, preamble_.mag,
              preamble_.comment ? preamble_.comment : "");

    return true;
}

// ============================================================================
// Postamble Parsing (for font definitions)
// ============================================================================

bool DVIParser::parse_postamble() {
    // find POST opcode by scanning backwards from end
    // The last bytes are: POST_POST, id, 223 padding (4-7 bytes)

    size_t end_pos = size_ - 1;

    // skip 223 padding bytes
    while (end_pos > 0 && data_[end_pos] == 223) {
        end_pos--;
    }

    // should now be at id byte
    if (end_pos < 5) {
        set_error("Invalid DVI file ending");
        return false;
    }

    uint8_t id = data_[end_pos--];
    if (id != 2) {
        set_error("Invalid DVI format ID in postamble");
        return false;
    }

    // POST_POST opcode
    if (data_[end_pos--] != DVI_POST_POST) {
        set_error("Expected POST_POST opcode");
        return false;
    }

    // read pointer to POST (4 bytes before POST_POST)
    uint32_t post_ptr = (data_[end_pos-3] << 24) | (data_[end_pos-2] << 16) |
                        (data_[end_pos-1] << 8) | data_[end_pos];

    // jump to postamble
    pos_ = post_ptr;

    if (read_u8() != DVI_POST) {
        set_error("Expected POST opcode at postamble");
        return false;
    }

    // skip postamble header
    read_u32();  // pointer to last BOP
    read_u32();  // num
    read_u32();  // den
    read_u32();  // mag
    read_u32();  // max height + depth
    read_u32();  // max width
    read_u16();  // stack depth
    read_u16();  // total pages

    // parse font definitions
    while (pos_ < size_) {
        uint8_t opcode = read_u8();

        if (opcode == DVI_POST_POST) {
            break;
        }

        if (opcode == DVI_NOP) {
            continue;
        }

        if (opcode >= DVI_FNT_DEF1 && opcode <= DVI_FNT_DEF4) {
            // font definition
            int k = opcode - DVI_FNT_DEF1 + 1;
            uint32_t font_num = 0;
            for (int i = 0; i < k; i++) {
                font_num = (font_num << 8) | read_u8();
            }

            // grow fonts array if needed
            if (font_count_ >= font_capacity_) {
                font_capacity_ = font_capacity_ ? font_capacity_ * 2 : 16;
                DVIFont* new_fonts = (DVIFont*)arena_alloc(arena_,
                    font_capacity_ * sizeof(DVIFont));
                if (fonts_) {
                    memcpy(new_fonts, fonts_, font_count_ * sizeof(DVIFont));
                }
                fonts_ = new_fonts;
            }

            DVIFont* f = &fonts_[font_count_++];
            f->font_num = font_num;
            f->checksum = read_u32();
            f->scale = read_u32();
            f->design_size = read_u32();

            uint8_t a = read_u8();  // area length
            uint8_t l = read_u8();  // name length
            f->area = read_string(a);
            f->name = read_string(l);

            log_debug("DVI font %u: %s (scale=%u, design=%u)",
                      f->font_num, f->name, f->scale, f->design_size);
        }
    }

    return true;
}

// ============================================================================
// Page Parsing
// ============================================================================

bool DVIParser::parse_pages() {
    // start after preamble, find BOP commands
    pos_ = 0;

    // skip preamble
    read_u8();  // PRE
    read_u8();  // id
    read_u32(); // num
    read_u32(); // den
    read_u32(); // mag
    uint8_t k = read_u8();
    pos_ += k;  // skip comment

    // scan for BOP commands
    while (pos_ < size_) {
        uint8_t opcode = data_[pos_];

        if (opcode == DVI_POST) {
            break;  // reached postamble
        }

        if (opcode == DVI_BOP) {
            // grow pages array if needed
            if (page_count_ >= page_capacity_) {
                page_capacity_ = page_capacity_ ? page_capacity_ * 2 : 8;
                DVIPage* new_pages = (DVIPage*)arena_alloc(arena_,
                    page_capacity_ * sizeof(DVIPage));
                if (pages_) {
                    memcpy(new_pages, pages_, page_count_ * sizeof(DVIPage));
                }
                pages_ = new_pages;
            }

            DVIPage* page = &pages_[page_count_++];
            memset(page, 0, sizeof(DVIPage));

            if (!process_page(page)) {
                return false;
            }
        } else {
            // skip other commands outside pages
            pos_++;
        }
    }

    return true;
}

bool DVIParser::process_page(DVIPage* page) {
    // reset state
    memset(&state_, 0, sizeof(state_));
    stack_depth_ = 0;

    // read BOP
    if (read_u8() != DVI_BOP) {
        set_error("Expected BOP");
        return false;
    }

    // read page counters
    for (int i = 0; i < 10; i++) {
        page->count[i] = read_i32();
    }
    page->prev_bop = read_i32();

    // process page content
    while (pos_ < size_) {
        uint8_t opcode = read_u8();

        // set_char_0 to set_char_127
        if (opcode <= DVI_SET_CHAR_127) {
            add_glyph(page, opcode);
            // advance h by character width (we don't have TFM, so skip)
            continue;
        }

        switch (opcode) {
            case DVI_SET1:
                add_glyph(page, read_u8());
                break;
            case DVI_SET2:
                add_glyph(page, read_u16());
                break;
            case DVI_SET3:
                add_glyph(page, read_u24());
                break;
            case DVI_SET4:
                add_glyph(page, read_i32());
                break;

            case DVI_SET_RULE: {
                int32_t a = read_i32();  // height
                int32_t b = read_i32();  // width
                add_rule(page, b, a);
                state_.h += b;
                break;
            }

            case DVI_PUT1:
                add_glyph(page, read_u8());
                break;
            case DVI_PUT2:
                add_glyph(page, read_u16());
                break;
            case DVI_PUT3:
                add_glyph(page, read_u24());
                break;
            case DVI_PUT4:
                add_glyph(page, read_i32());
                break;

            case DVI_PUT_RULE: {
                int32_t a = read_i32();
                int32_t b = read_i32();
                add_rule(page, b, a);
                // no advance for PUT
                break;
            }

            case DVI_NOP:
                break;

            case DVI_EOP:
                return true;  // end of page

            case DVI_PUSH:
                push_state();
                break;

            case DVI_POP:
                pop_state();
                break;

            case DVI_RIGHT1:
                state_.h += read_i8();
                break;
            case DVI_RIGHT2:
                state_.h += read_i16();
                break;
            case DVI_RIGHT3:
                state_.h += read_i24();
                break;
            case DVI_RIGHT4:
                state_.h += read_i32();
                break;

            case DVI_W0:
                state_.h += state_.w;
                break;
            case DVI_W1:
                state_.w = read_i8();
                state_.h += state_.w;
                break;
            case DVI_W2:
                state_.w = read_i16();
                state_.h += state_.w;
                break;
            case DVI_W3:
                state_.w = read_i24();
                state_.h += state_.w;
                break;
            case DVI_W4:
                state_.w = read_i32();
                state_.h += state_.w;
                break;

            case DVI_X0:
                state_.h += state_.x;
                break;
            case DVI_X1:
                state_.x = read_i8();
                state_.h += state_.x;
                break;
            case DVI_X2:
                state_.x = read_i16();
                state_.h += state_.x;
                break;
            case DVI_X3:
                state_.x = read_i24();
                state_.h += state_.x;
                break;
            case DVI_X4:
                state_.x = read_i32();
                state_.h += state_.x;
                break;

            case DVI_DOWN1:
                state_.v += read_i8();
                break;
            case DVI_DOWN2:
                state_.v += read_i16();
                break;
            case DVI_DOWN3:
                state_.v += read_i24();
                break;
            case DVI_DOWN4:
                state_.v += read_i32();
                break;

            case DVI_Y0:
                state_.v += state_.y;
                break;
            case DVI_Y1:
                state_.y = read_i8();
                state_.v += state_.y;
                break;
            case DVI_Y2:
                state_.y = read_i16();
                state_.v += state_.y;
                break;
            case DVI_Y3:
                state_.y = read_i24();
                state_.v += state_.y;
                break;
            case DVI_Y4:
                state_.y = read_i32();
                state_.v += state_.y;
                break;

            case DVI_Z0:
                state_.v += state_.z;
                break;
            case DVI_Z1:
                state_.z = read_i8();
                state_.v += state_.z;
                break;
            case DVI_Z2:
                state_.z = read_i16();
                state_.v += state_.z;
                break;
            case DVI_Z3:
                state_.z = read_i24();
                state_.v += state_.z;
                break;
            case DVI_Z4:
                state_.z = read_i32();
                state_.v += state_.z;
                break;

            case DVI_XXX1: {
                uint8_t k = read_u8();
                pos_ += k;  // skip special string
                break;
            }
            case DVI_XXX2: {
                uint16_t k = read_u16();
                pos_ += k;
                break;
            }
            case DVI_XXX3: {
                uint32_t k = read_u24();
                pos_ += k;
                break;
            }
            case DVI_XXX4: {
                uint32_t k = read_u32();
                pos_ += k;
                break;
            }

            default:
                // font selection
                if (opcode >= DVI_FNT_NUM_0 && opcode <= DVI_FNT_NUM_63) {
                    state_.f = opcode - DVI_FNT_NUM_0;
                } else if (opcode >= DVI_FNT1 && opcode <= DVI_FNT4) {
                    int k = opcode - DVI_FNT1 + 1;
                    state_.f = 0;
                    for (int i = 0; i < k; i++) {
                        state_.f = (state_.f << 8) | read_u8();
                    }
                } else if (opcode >= DVI_FNT_DEF1 && opcode <= DVI_FNT_DEF4) {
                    // font definition (can appear in page, skip it)
                    int k = opcode - DVI_FNT_DEF1 + 1;
                    pos_ += k;      // font number
                    pos_ += 12;     // checksum, scale, design_size
                    uint8_t a = read_u8();
                    uint8_t l = read_u8();
                    pos_ += a + l;  // area + name
                } else {
                    log_warn("DVI parser: unknown opcode %d at %zu", opcode, pos_ - 1);
                }
                break;
        }
    }

    set_error("Unexpected end of file in page");
    return false;
}

void DVIParser::add_glyph(DVIPage* page, int32_t codepoint) {
    // grow array if needed
    if (page->glyph_count >= page->glyph_capacity) {
        page->glyph_capacity = page->glyph_capacity ? page->glyph_capacity * 2 : 256;
        PositionedGlyph* new_glyphs = (PositionedGlyph*)arena_alloc(arena_,
            page->glyph_capacity * sizeof(PositionedGlyph));
        if (page->glyphs) {
            memcpy(new_glyphs, page->glyphs, page->glyph_count * sizeof(PositionedGlyph));
        }
        page->glyphs = new_glyphs;
    }

    PositionedGlyph* g = &page->glyphs[page->glyph_count++];
    g->codepoint = codepoint;
    g->h = state_.h;
    g->v = state_.v;
    g->font_num = state_.f;
}

void DVIParser::add_rule(DVIPage* page, int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;  // invisible rule

    if (page->rule_count >= page->rule_capacity) {
        page->rule_capacity = page->rule_capacity ? page->rule_capacity * 2 : 32;
        PositionedRule* new_rules = (PositionedRule*)arena_alloc(arena_,
            page->rule_capacity * sizeof(PositionedRule));
        if (page->rules) {
            memcpy(new_rules, page->rules, page->rule_count * sizeof(PositionedRule));
        }
        page->rules = new_rules;
    }

    PositionedRule* r = &page->rules[page->rule_count++];
    r->h = state_.h;
    r->v = state_.v;
    r->width = width;
    r->height = height;
}

void DVIParser::push_state() {
    if (stack_depth_ >= stack_capacity_) {
        stack_capacity_ = stack_capacity_ ? stack_capacity_ * 2 : 32;
        State* new_stack = (State*)arena_alloc(arena_, stack_capacity_ * sizeof(State));
        if (state_stack_) {
            memcpy(new_stack, state_stack_, stack_depth_ * sizeof(State));
        }
        state_stack_ = new_stack;
    }
    state_stack_[stack_depth_++] = state_;
}

void DVIParser::pop_state() {
    if (stack_depth_ > 0) {
        state_ = state_stack_[--stack_depth_];
    }
}

// ============================================================================
// Debug Output
// ============================================================================

void dump_dvi(const DVIParser& parser, FILE* out) {
    const DVIPreamble* pre = parser.preamble();
    fprintf(out, "DVI File: format=%d, num=%u, den=%u, mag=%u\n",
            pre->id, pre->num, pre->den, pre->mag);
    fprintf(out, "Comment: %s\n\n", pre->comment ? pre->comment : "");

    fprintf(out, "Fonts (%d):\n", parser.font_count());
    for (int i = 0; i < parser.font_count(); i++) {
        const DVIFont* f = &parser.fonts()[i];
        fprintf(out, "  [%u] %s (scale=%u, design=%u)\n",
                f->font_num, f->name, f->scale, f->design_size);
    }
    fprintf(out, "\n");

    fprintf(out, "Pages (%d):\n", parser.page_count());
    for (int i = 0; i < parser.page_count(); i++) {
        fprintf(out, "\n=== Page %d ===\n", i + 1);
        dump_dvi_page(parser.page(i), parser, out);
    }
}

void dump_dvi_page(const DVIPage* page, const DVIParser& parser, FILE* out) {
    fprintf(out, "Counters: [%d][%d][%d][%d][%d][%d][%d][%d][%d][%d]\n",
            page->count[0], page->count[1], page->count[2], page->count[3],
            page->count[4], page->count[5], page->count[6], page->count[7],
            page->count[8], page->count[9]);

    fprintf(out, "Glyphs (%d):\n", page->glyph_count);
    for (int i = 0; i < page->glyph_count && i < 100; i++) {  // limit output
        const PositionedGlyph* g = &page->glyphs[i];
        const DVIFont* f = parser.font(g->font_num);
        char ch = (g->codepoint >= 32 && g->codepoint < 127) ? g->codepoint : '?';
        fprintf(out, "  [%d] char=%d '%c' h=%.2fpt v=%.2fpt font=%s\n",
                i, g->codepoint, ch,
                DVIParser::sp_to_pt(g->h),
                DVIParser::sp_to_pt(g->v),
                f ? f->name : "?");
    }
    if (page->glyph_count > 100) {
        fprintf(out, "  ... (%d more glyphs)\n", page->glyph_count - 100);
    }

    if (page->rule_count > 0) {
        fprintf(out, "Rules (%d):\n", page->rule_count);
        for (int i = 0; i < page->rule_count; i++) {
            const PositionedRule* r = &page->rules[i];
            fprintf(out, "  [%d] h=%.2fpt v=%.2fpt w=%.2fpt h=%.2fpt\n",
                    i, DVIParser::sp_to_pt(r->h), DVIParser::sp_to_pt(r->v),
                    DVIParser::sp_to_pt(r->width), DVIParser::sp_to_pt(r->height));
        }
    }
}

} // namespace dvi
} // namespace tex
