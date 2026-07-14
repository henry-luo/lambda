#include "layout.hpp"
#include "view.hpp"
#include "rdt_video.h"
#include "event.hpp"
#include "render.hpp"
#include "../lib/str.h"
#include "../lib/strview.h"
#include "../lib/memtrack.h"
#include "../lib/color.h"
#include "../lib/tagged.hpp"
#include <cstdlib>  // for strtol
#include <new>      // for placement new


static void media_state_changed(RdtVideo* video, RdtVideoState state, void* userdata) {
    (void)video;
    DocState* doc_state = (DocState*)userdata;
    if (doc_state && state == RDT_VIDEO_STATE_PLAYING) {
        doc_state->has_active_video = true;
    }
    radiant_video_notify_frame_ready(doc_state);
}

static void media_frame_ready(RdtVideo* video, void* userdata) {
    (void)video;
    radiant_video_notify_frame_ready((DocState*)userdata);
}

static void media_duration_known(RdtVideo* video, double seconds, void* userdata) {
    (void)video;
    (void)seconds;
    radiant_video_notify_frame_ready((DocState*)userdata);
}

static void media_video_size_known(RdtVideo* video, int width, int height, void* userdata) {
    (void)video;
    (void)width;
    (void)height;
    radiant_video_notify_frame_ready((DocState*)userdata);
}

static RdtVideoCallbacks media_callbacks = {
    media_state_changed,
    media_frame_ready,
    media_duration_known,
    media_video_size_known
};

static FormControlProp* ensure_form_control_prop(LayoutContext* lycon, ViewBlock* block,
                                                 FormControlType control_type,
                                                 bool* out_created) {
    if (out_created) *out_created = false;
    if (!lycon || !block) return nullptr;
    if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form) {
        return block->form;
    }

    float flex_grow = 0.0f;
    float flex_shrink = 1.0f;
    float flex_basis = -1.0f;
    bool flex_basis_is_percent = false;
    if (block->item_prop_type == DomElement::ITEM_PROP_FLEX && block->fi) {
        flex_grow = block->fi->flex_grow;
        flex_shrink = block->fi->flex_shrink;
        flex_basis = block->fi->flex_basis;
        flex_basis_is_percent = block->fi->flex_basis_is_percent;
    }
    GridItemProp* grid_item = nullptr;
    if (block->item_prop_type == DomElement::ITEM_PROP_GRID && block->gi) {
        grid_item = block->gi;
    }

    FormControlProp* form = (FormControlProp*)alloc_prop(lycon, sizeof(FormControlProp));
    form_control_prop_init(form);
    form->control_type = control_type;
    form->flex_grow = flex_grow;
    form->flex_shrink = flex_shrink;
    form->flex_basis = flex_basis;
    form->flex_basis_is_percent = flex_basis_is_percent;
    form->grid_item = grid_item;

    block->item_prop_type = DomElement::ITEM_PROP_FORM;
    block->form = form;
    if (out_created) *out_created = true;
    return form;
}

// Parse HTML color attribute (e.g., "#ff6600" or "ff6600" or named colors like "red")
static Color parse_html_color(const char* color_str) {
    Color result;
    result.r = 0; result.g = 0; result.b = 0; result.a = 255;  // default black, opaque
    if (!color_str || !*color_str) return result;

    // hex form (with or without leading '#'); digit handling lives in lib/color.h
    uint8_t r, g, b, a;
    if (color_parse_hex(color_str, &r, &g, &b, &a)) {
        result.r = r; result.g = g; result.b = b; result.a = a;
    }
    // TODO: add named color support (red, blue, green, etc.)
    return result;
}

struct HtmlDimensionAttr {
    float value;
    bool is_percent;
};

static bool parse_html_dimension_attr(const char* attr, bool allow_percent,
                                      bool require_digit_start,
                                      HtmlDimensionAttr* out_dim) {
    if (!attr || !out_dim) return false;
    size_t attr_len = strlen(attr);
    if (attr_len == 0) return false;

    out_dim->value = -1.0f;
    out_dim->is_percent = false;
    if (allow_percent && attr[attr_len - 1] == '%') {
        StrView view = strview_init(attr, attr_len - 1);
        float percent = (float)strview_to_int(&view);
        if (percent < 0.0f) return false;
        out_dim->value = percent;
        out_dim->is_percent = true;
        return true;
    }

    if (require_digit_start && (attr[0] < '0' || attr[0] > '9')) return false;
    StrView view = strview_init(attr, attr_len);
    float value = (float)strview_to_int(&view);
    if (value < 0.0f) return false;
    out_dim->value = value;
    return true;
}

static BlockProp* ensure_html_block_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->blk) { block->blk = alloc_block_prop(lycon); }
    return block->blk;
}

static FontProp* ensure_html_block_font(LayoutContext* lycon, ViewBlock* block) {
    if (!block->font) { block->font = alloc_font_prop(lycon); }
    return block->font;
}

static FontProp* ensure_html_span_font(LayoutContext* lycon, ViewSpan* span) {
    if (!span->font) { span->font = alloc_font_prop(lycon); }
    return span->font;
}

static BackgroundProp* ensure_html_background_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
    if (!block->bound->background) { block->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp)); }
    return block->bound->background;
}

static BorderProp* ensure_html_border_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
    if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
    return block->bound->border;
}

static void apply_html_background_color(LayoutContext* lycon, ViewBlock* block, Color color) {
    // HTML bgcolor hints and form UA defaults share one allocation invariant.
    ensure_html_background_prop(lycon, block)->color = color;
}

static void apply_html_font_weight_bold(FontProp* font) {
    font->font_weight = CSS_VALUE_BOLD;
    font->font_weight_numeric = 700;
}

static void apply_html_font_weight_normal(FontProp* font) {
    font->font_weight = CSS_VALUE_NORMAL;
    font->font_weight_numeric = 400;
}

static void apply_html_font_size(FontProp* font, float size, bool from_medium) {
    font->font_size = size;
    font->font_size_from_medium = from_medium;
}

static void apply_html_span_bold(LayoutContext* lycon, ViewSpan* span) {
    apply_html_font_weight_bold(ensure_html_span_font(lycon, span));
}

static void apply_html_span_font_style(LayoutContext* lycon, ViewSpan* span, CssEnum style) {
    ensure_html_span_font(lycon, span)->font_style = style;
}

static void apply_html_span_text_deco(LayoutContext* lycon, ViewSpan* span, CssEnum text_deco) {
    ensure_html_span_font(lycon, span)->text_deco = text_deco;
}

static void apply_html_span_font_size(LayoutContext* lycon, ViewSpan* span, float size,
                                      bool from_medium) {
    apply_html_font_size(ensure_html_span_font(lycon, span), size, from_medium);
}

static void apply_html_block_monospace_font(LayoutContext* lycon, ViewBlock* block) {
    radiant_retain_font_family(ensure_html_block_font(lycon, block), lam::GcPtr<char>((char*)"monospace"));
}

static void apply_html_span_monospace_font(LayoutContext* lycon, ViewSpan* span) {
    radiant_retain_font_family(ensure_html_span_font(lycon, span), lam::GcPtr<char>((char*)"monospace"));
}

static void apply_html_form_control_font(LayoutContext* lycon, ViewBlock* block) {
    // Chrome UA form controls share this Arial medium-derived override.
    FontProp* font = ensure_html_block_font(lycon, block);
    apply_html_font_size(font, 13.3333f, false);
    radiant_retain_font_family(font, lam::GcPtr<char>((char*)"Arial"));
}

static void apply_html_textarea_font(LayoutContext* lycon, ViewBlock* block) {
    // Textarea keeps its own monospace/normal-weight UA font policy.
    FontProp* font = ensure_html_block_font(lycon, block);
    radiant_retain_font_family(font, lam::GcPtr<char>((char*)"monospace"));
    apply_html_font_size(font, 13.333333f, true);
    font->font_style = CSS_VALUE_NORMAL;
    apply_html_font_weight_normal(font);
}

static BorderProp* apply_html_uniform_border_base(LayoutContext* lycon, ViewBlock* block,
                                                  float width, CssEnum style,
                                                  bool set_specificity) {
    BorderProp* border = ensure_html_border_prop(lycon, block);
    border->width.top = border->width.right =
        border->width.bottom = border->width.left = width;
    if (set_specificity) {
        border->width.top_specificity = border->width.right_specificity =
            border->width.bottom_specificity = border->width.left_specificity = -1;
    }
    border->top_style = border->right_style =
        border->bottom_style = border->left_style = style;
    return border;
}

static void apply_html_uniform_border(LayoutContext* lycon, ViewBlock* block,
                                      float width, CssEnum style, Color color) {
    // HTML UA borders differ on specificity, so callers preserve their original policy.
    BorderProp* border = apply_html_uniform_border_base(lycon, block, width, style, true);
    border->top_color = border->right_color =
        border->bottom_color = border->left_color = color;
}

static void apply_html_uniform_border_no_specificity(LayoutContext* lycon, ViewBlock* block,
                                                     float width, CssEnum style, Color color) {
    BorderProp* border = apply_html_uniform_border_base(lycon, block, width, style, false);
    border->top_color = border->right_color =
        border->bottom_color = border->left_color = color;
}

static void apply_html_uniform_border_style(LayoutContext* lycon, ViewBlock* block,
                                            float width, CssEnum style) {
    apply_html_uniform_border_base(lycon, block, width, style, true);
}

static void apply_html_width_px(LayoutContext* lycon, ViewBlock* block, float width) {
    BlockProp* blk = ensure_html_block_prop(lycon, block);
    lycon->block.given_width = width;
    blk->given_width = width;
    blk->given_width_percent = NAN;
}

static void apply_html_height_px(LayoutContext* lycon, ViewBlock* block, float height) {
    BlockProp* blk = ensure_html_block_prop(lycon, block);
    lycon->block.given_height = height;
    blk->given_height = height;
    blk->given_height_percent = NAN;
}

static void apply_html_width_percent(LayoutContext* lycon, ViewBlock* block,
                                     float percent, float container_width) {
    BlockProp* blk = ensure_html_block_prop(lycon, block);
    // keep percentage hints in BlockProp so replaced-layout passes can resolve them later.
    blk->given_width_percent = percent;
    blk->given_width = -1.0f;
    if (container_width > 0.0f) {
        lycon->block.given_width = container_width * percent / 100.0f;
    }
}

static void apply_html_height_percent(LayoutContext* lycon, ViewBlock* block,
                                      float percent, float container_height) {
    BlockProp* blk = ensure_html_block_prop(lycon, block);
    // keep percentage hints in BlockProp so replaced-layout passes can resolve them later.
    blk->given_height_percent = percent;
    blk->given_height = -1.0f;
    if (container_height > 0.0f) {
        lycon->block.given_height = container_height * percent / 100.0f;
    }
}

static void apply_html_deferred_width_percent(LayoutContext* lycon, ViewBlock* block, float percent) {
    BlockProp* blk = ensure_html_block_prop(lycon, block);
    blk->given_width_percent = percent;
    lycon->block.given_width = -1.0f;
}

static void apply_html_deferred_height_percent(LayoutContext* lycon, ViewBlock* block, float percent) {
    BlockProp* blk = ensure_html_block_prop(lycon, block);
    blk->given_height_percent = percent;
    lycon->block.given_height = -1.0f;
}

static void apply_html_context_default_size(LayoutContext* lycon, float width, float height) {
    lycon->block.given_width = width;
    lycon->block.given_height = height;
}

// Get the border attribute value from the parent TABLE element
// Returns -1 if no border attribute is found, otherwise returns the pixel value
// Per WHATWG 15.3.10: table[border] td, table[border] th { border-width: 1px; border-style: inset; border-color: grey; }
static float get_parent_table_border(DomNode* elmt) {
    // Traverse up to find the TABLE element (TD -> TR -> TBODY/THEAD/TFOOT -> TABLE, or TD -> TR -> TABLE)
    DomNode* node = elmt->parent;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->tag_id == HTM_TAG_TABLE) {
                const char* border_attr = elem->get_attribute("border");
                if (border_attr) {
                    StrView b_view = strview_init(border_attr, strlen(border_attr));
                    float border_val = strview_to_int(&b_view);
                    if (border_val >= 0) {
                        return border_val;
                    }
                }
                return -1;
            }
        }
        node = node->parent;
    }
    return -1;
}

// Get the cellpadding attribute value from the parent TABLE element
// Returns -1 if no cellpadding attribute is found, otherwise returns the pixel value (CSS logical pixels)
// The HTML spec says: cellpadding on TABLE maps to padding on TD/TH cells
static float get_parent_table_cellpadding(DomNode* elmt) {
    // Traverse up to find the TABLE element (TD -> TR -> TBODY/THEAD/TFOOT -> TABLE, or TD -> TR -> TABLE)
    DomNode* node = elmt->parent;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->tag_id == HTM_TAG_TABLE) {
                const char* cellpadding_attr = elem->get_attribute("cellpadding");
                if (cellpadding_attr) {
                    StrView cp_view = strview_init(cellpadding_attr, strlen(cellpadding_attr));
                    float cellpadding = strview_to_int(&cp_view);
                    if (cellpadding >= 0) {
                        log_debug("[HTML] TABLE cellpadding attribute: %.0fpx", cellpadding);
                        return cellpadding;  // CSS logical pixels
                    }
                }
                // Found parent table but no cellpadding attribute
                return -1;
            }
        }
        node = node->parent;
    }
    // No parent table found
    return -1;
}

static float html_font_size_for_level(int level) {
    if (level < 1) level = 1;
    if (level > 7) level = 7;
    switch (level) {
        case 1: return 10.0f;
        case 2: return 13.0f;
        case 3: return 16.0f;
        case 4: return 18.0f;
        case 5: return 24.0f;
        case 6: return 32.0f;
        case 7: return 48.0f;
    }
    return 16.0f;
}

static int html_font_level_for_size(float current) {
    if (current <= 10.0f) return 1;
    if (current <= 13.0f) return 2;
    if (current <= 16.0f) return 3;
    if (current <= 18.0f) return 4;
    if (current <= 24.0f) return 5;
    if (current <= 32.0f) return 6;
    return 7;
}

static void apply_table_cell_width_attribute(DomElement* elmt, ViewBlock* block) {
    if (!elmt || !block) return;
    const char* width_attr = elmt->get_attribute("width");
    if (!width_attr) return;

    if (!block->blk) return;

    HtmlDimensionAttr width;
    if (!parse_html_dimension_attr(width_attr, true, true, &width)) return;
    if (width.is_percent) {
        block->blk->given_width = -1.0f;
        block->blk->given_width_percent = width.value;
        log_debug("[HTML] TABLE CELL width attribute: %.0f%%", width.value);
    } else {
        block->blk->given_width = width.value;
        block->blk->given_width_percent = NAN;
        log_debug("[HTML] TABLE CELL width attribute: %.0fpx", width.value);
    }
}

static void apply_table_cell_height_attribute(DomElement* elmt, ViewBlock* block) {
    if (!elmt || !block) return;
    const char* height_attr = elmt->get_attribute("height");
    if (!height_attr) return;

    if (!block->blk) return;

    HtmlDimensionAttr height;
    if (!parse_html_dimension_attr(height_attr, false, true, &height)) return;
    block->blk->given_height = height.value;
    block->blk->given_height_percent = NAN;
    log_debug("[HTML] TABLE CELL height attribute: %.0fpx", height.value);
}

// Get the rules attribute from the parent TABLE element.
// HTML rules presentational hints affect table border conflict resolution and
// per-cell borders (e.g. rules="cols" creates vertical cell rules).
static const char* get_parent_table_rules(DomNode* elmt) {
    DomNode* node = elmt->parent;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->tag_id == HTM_TAG_TABLE) {
                return elem->get_attribute("rules");
            }
        }
        node = node->parent;
    }
    return nullptr;
}

static void apply_html_table_rules_cell_border(LayoutContext* lycon, ViewBlock* block,
                                               const char* rules_attr, bool is_header) {
    if (!rules_attr) return;

    size_t rules_len = strlen(rules_attr);
    bool rules_cols = str_ieq_const(rules_attr, rules_len, "cols");
    bool rules_rows = str_ieq_const(rules_attr, rules_len, "rows");
    bool rules_all = str_ieq_const(rules_attr, rules_len, "all");
    if (!rules_cols && !rules_rows && !rules_all) return;

    BorderProp* border = ensure_html_border_prop(lycon, block);
    Color grey = {0};
    grey.r = 128;
    grey.g = 128;
    grey.b = 128;
    grey.a = 255;
    if (rules_cols || rules_all) {
        border->width.left = border->width.right = 1.0f;
        border->width.left_specificity = border->width.right_specificity = -1;
        border->left_style = border->right_style = CSS_VALUE_SOLID;
        border->left_color = border->right_color = grey;
    }
    if (rules_rows || rules_all) {
        border->width.top = border->width.bottom = 1.0f;
        border->width.top_specificity = border->width.bottom_specificity = -1;
        border->top_style = border->bottom_style = CSS_VALUE_SOLID;
        border->top_color = border->bottom_color = grey;
    }
    log_debug("[HTML] %s border from parent TABLE rules=%s", is_header ? "TH" : "TD", rules_attr);
}

// get parent TR's valign attribute (for TD/TH cells)
static const char* get_parent_tr_valign(DomNode* elmt) {
    // TD/TH -> TR, check TR's valign attribute
    DomNode* node = elmt->parent;
    if (node && node->is_element()) {
        DomElement* elem = node->as_element();
        if (elem->tag_id == HTM_TAG_TR) {
            return elem->get_attribute("valign");
        }
    }
    return nullptr;
}

static void apply_html_table_cell_defaults(LayoutContext* lycon, DomNode* cell_node,
                                           ViewBlock* block, bool is_header) {
    DomElement* cell = cell_node->as_element();
    const char* tag_name = is_header ? "TH" : "TD";
    if (is_header) {
        log_debug("apply default TH styles");
        apply_html_font_weight_bold(ensure_html_block_font(lycon, block));
    }

    BlockProp* block_prop = ensure_html_block_prop(lycon, block);
    block_prop->text_align = is_header ? CSS_VALUE_CENTER : CSS_VALUE_LEFT;
    apply_table_cell_width_attribute(cell, block);
    apply_table_cell_height_attribute(cell, block);

    if (!block->in_line) block->in_line = alloc_inline_prop(lycon);
    block->in_line->vertical_align = CSS_VALUE_MIDDLE;

    float cellpadding = get_parent_table_cellpadding(cell_node);
    float padding = cellpadding >= 0.0f ? cellpadding : 1.0f;
    if (!block->bound) {
        block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
    }
    block->bound->padding.top = block->bound->padding.right =
        block->bound->padding.bottom = block->bound->padding.left = padding;
    block->bound->padding.top_specificity = block->bound->padding.right_specificity =
        block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;

    const char* align_attr = cell->get_attribute("align");
    if (align_attr) {
        size_t align_len = strlen(align_attr);
        if (str_ieq_const(align_attr, align_len, "left")) {
            block_prop->text_align = CSS_VALUE_LEFT;
            block_prop->legacy_block_align = CSS_VALUE_LEFT;
        } else if (str_ieq_const(align_attr, align_len, "right")) {
            block_prop->text_align = CSS_VALUE_RIGHT;
            block_prop->legacy_block_align = CSS_VALUE_RIGHT;
        } else if (str_ieq_const(align_attr, align_len, "center")) {
            block_prop->text_align = CSS_VALUE_CENTER;
            block_prop->legacy_align_center_blocks = true;
            block_prop->legacy_block_align = CSS_VALUE_CENTER;
        }
    }

    const char* valign_attr = cell->get_attribute("valign");
    if (!valign_attr) valign_attr = get_parent_tr_valign(cell_node);
    if (valign_attr) {
        size_t valign_len = strlen(valign_attr);
        if (str_ieq_const(valign_attr, valign_len, "top")) {
            block->in_line->vertical_align = CSS_VALUE_TOP;
        } else if (str_ieq_const(valign_attr, valign_len, "middle")) {
            block->in_line->vertical_align = CSS_VALUE_MIDDLE;
        } else if (str_ieq_const(valign_attr, valign_len, "bottom")) {
            block->in_line->vertical_align = CSS_VALUE_BOTTOM;
        }
    }

    if (cell->get_attribute("nowrap")) {
        block_prop->white_space = CSS_VALUE_NOWRAP;
        log_debug("[HTML] %s nowrap attribute -> white-space: nowrap", tag_name);
    }

    const char* bgcolor_attr = cell->get_attribute("bgcolor");
    if (bgcolor_attr) {
        Color bg_color = parse_html_color(bgcolor_attr);
        apply_html_background_color(lycon, block, bg_color);
        log_debug("[HTML] %s bgcolor attribute: #%02x%02x%02x",
                  tag_name, bg_color.r, bg_color.g, bg_color.b);
    }

    if (get_parent_table_border(cell_node) > 0.0f) {
        Color grey = (Color){ .r=128, .g=128, .b=128, .a=255 };
        apply_html_uniform_border(lycon, block, 1.0f, CSS_VALUE_INSET, grey);
        log_debug("[HTML] %s border from parent TABLE: 1px inset grey", tag_name);
    }
    apply_html_table_rules_cell_border(
        lycon, block, get_parent_table_rules(cell_node), is_header);
}

// HTML5 §14.3.4 / Unicode UAX #9: Detect first strong directional character.
// Returns 1 for RTL (R/AL), -1 for LTR (L), 0 for neutral/not found.
static int bidi_strong_class(uint32_t cp) {
    // L (Left-to-Right): Latin, CJK, LRM
    if (cp == 0x200E) return -1; // LRM
    // R (Right-to-Left): Hebrew, RLM
    if (cp == 0x200F) return 1;  // RLM
    // AL (Arabic Letter): ALM
    if (cp == 0x061C) return 1;  // ALM
    // Hebrew: U+0590-U+05FF (R)
    if (cp >= 0x0590 && cp <= 0x05FF) return 1;
    // Arabic: U+0600-U+07BF (AL) — Arabic, Arabic Supplement, Arabic Extended-A
    if (cp >= 0x0600 && cp <= 0x07BF) return 1;
    // Arabic Extended-B, Arabic Presentation Forms
    if (cp >= 0x0860 && cp <= 0x089F) return 1;
    if (cp >= 0xFB50 && cp <= 0xFDFF) return 1;  // Arabic Presentation Forms-A
    if (cp >= 0xFE70 && cp <= 0xFEFF) return 1;  // Arabic Presentation Forms-B
    // NKo: U+07C0-U+07FF (R)
    if (cp >= 0x07C0 && cp <= 0x07FF) return 1;
    // Syriac: U+0700-U+074F (AL)
    if (cp >= 0x0700 && cp <= 0x074F) return 1;
    // Thaana: U+0780-U+07BF (AL) — already covered above
    // Samaritan, Mandaic
    if (cp >= 0x0800 && cp <= 0x085F) return 1;
    // Common strong LTR ranges (L class)
    // Basic Latin A-Z, a-z
    if ((cp >= 0x0041 && cp <= 0x005A) || (cp >= 0x0061 && cp <= 0x007A)) return -1;
    // Latin Extended
    if (cp >= 0x00C0 && cp <= 0x02AF) return -1;
    // Greek
    if (cp >= 0x0370 && cp <= 0x03FF) return -1;
    // Cyrillic
    if (cp >= 0x0400 && cp <= 0x052F) return -1;
    // CJK Unified Ideographs
    if (cp >= 0x4E00 && cp <= 0x9FFF) return -1;
    // Hangul
    if (cp >= 0xAC00 && cp <= 0xD7AF) return -1;
    // Hiragana, Katakana
    if (cp >= 0x3040 && cp <= 0x30FF) return -1;
    // Thai, Lao, Tibetan, Myanmar, Georgian, Ethiopic, Cherokee, etc.
    if (cp >= 0x0E01 && cp <= 0x0E5B) return -1;  // Thai
    if (cp >= 0x0E81 && cp <= 0x0EDF) return -1;  // Lao
    if (cp >= 0x10A0 && cp <= 0x10FF) return -1;  // Georgian
    if (cp >= 0x1100 && cp <= 0x11FF) return -1;  // Hangul Jamo
    // Devanagari and other Indic scripts
    if (cp >= 0x0900 && cp <= 0x0DFF) return -1;
    // Latin Extended Additional, General Punctuation etc. are neutral
    return 0;
}

// HTML5 §14.3.4: Walk the element's descendants to find the first strong character.
// Skip <script>, <style>, and elements with their own dir attribute.
static int find_first_strong_in_node(DomNode* node) {
    if (!node) return 0;
    if (node->is_text()) {
        DomText* text = node->as_text();
        if (!text->text || text->length == 0) return 0;
        const char* p = text->text;
        const char* end = p + text->length;
        while (p < end) {
            uint32_t cp;
            int bytes = str_utf8_decode(p, (size_t)(end - p), &cp);
            if (bytes <= 0) { p++; continue; }
            int cls = bidi_strong_class(cp);
            if (cls != 0) return cls;
            p += bytes;
        }
        return 0;
    }
    if (node->is_element()) {
        DomElement* elem = node->as_element();
        // Skip <script>, <style> — they don't contribute to dir="auto" detection
        uintptr_t tag = elem->tag_id;
        if (tag == HTM_TAG_SCRIPT || tag == HTM_TAG_STYLE) return 0;
        // Skip elements that have their own dir attribute — per HTML spec,
        // they establish their own directionality and don't contribute to parent's auto
        const char* child_dir = elem->get_attribute("dir");
        if (child_dir) return 0;
        // Recurse into children
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            int result = find_first_strong_in_node(child);
            if (result != 0) return result;
        }
    }
    return 0;
}

// Resolve dir="auto" by finding the first strong directional character.
// Returns CSS_VALUE_RTL or CSS_VALUE_LTR.
static CssEnum resolve_dir_auto(DomElement* elmt) {
    for (DomNode* child = elmt->first_child; child; child = child->next_sibling) {
        int result = find_first_strong_in_node(child);
        if (result > 0) return CSS_VALUE_RTL;
        if (result < 0) return CSS_VALUE_LTR;
    }
    return CSS_VALUE_LTR;  // default to LTR if no strong character found
}

void apply_element_default_style(LayoutContext* lycon, DomNode* elmt) {
    ViewSpan* span = lam::view_require_element(static_cast<View*>(elmt));
    // Default-style resolution runs for both block and inline elements,
    // including generated pseudo-elements like ::after with display:inline.
    // Use the shared element storage view so inline elements can still receive
    // boundary/font defaults without asserting on their current view tag.
    ViewBlock* block = lam::unsafe_view_block_api_span(span);
    float em_size = 0;  uintptr_t elmt_name = elmt->tag();
    switch (elmt_name) {
    case HTM_TAG_BODY: {
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // margin: 8px (CSS logical pixels)
        block->bound->margin.top = block->bound->margin.right =
            block->bound->margin.bottom = block->bound->margin.left = 8;
        block->bound->margin.top_specificity = block->bound->margin.right_specificity =
            block->bound->margin.bottom_specificity = block->bound->margin.left_specificity = -1;
        // Handle HTML bgcolor attribute (e.g., <body bgcolor="#fff">)
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            apply_html_background_color(lycon, block, bg_color);
            log_debug("[HTML] BODY bgcolor attribute: #%02x%02x%02x", bg_color.r, bg_color.g, bg_color.b);
        }
        // Legacy HTML body margin attributes are still honored by browsers.
        // marginwidth controls left/right; marginheight controls top/bottom.
        const char* marginwidth_attr = elmt->get_attribute("marginwidth");
        if (marginwidth_attr && marginwidth_attr[0] >= '0' && marginwidth_attr[0] <= '9') {
            StrView margin_view = strview_init(marginwidth_attr, strlen(marginwidth_attr));
            float margin = (float)strview_to_int(&margin_view);
            if (margin >= 0.0f) {
                block->bound->margin.left = block->bound->margin.right = margin;
                block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
                log_debug("[HTML] BODY marginwidth attribute: %.0fpx", margin);
            }
        }
        const char* marginheight_attr = elmt->get_attribute("marginheight");
        if (marginheight_attr && marginheight_attr[0] >= '0' && marginheight_attr[0] <= '9') {
            StrView margin_view = strview_init(marginheight_attr, strlen(marginheight_attr));
            float margin = (float)strview_to_int(&margin_view);
            if (margin >= 0.0f) {
                block->bound->margin.top = block->bound->margin.bottom = margin;
                block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
                log_debug("[HTML] BODY marginheight attribute: %.0fpx", margin);
            }
        }
        const char* leftmargin_attr = elmt->get_attribute("leftmargin");
        if (leftmargin_attr && leftmargin_attr[0] >= '0' && leftmargin_attr[0] <= '9') {
            StrView margin_view = strview_init(leftmargin_attr, strlen(leftmargin_attr));
            float margin = (float)strview_to_int(&margin_view);
            if (margin >= 0.0f) {
                block->bound->margin.left = margin;
                block->bound->margin.left_specificity = -1;
                log_debug("[HTML] BODY leftmargin attribute: %.0fpx", margin);
            }
        }
        const char* topmargin_attr = elmt->get_attribute("topmargin");
        if (topmargin_attr && topmargin_attr[0] >= '0' && topmargin_attr[0] <= '9') {
            StrView margin_view = strview_init(topmargin_attr, strlen(topmargin_attr));
            float margin = (float)strview_to_int(&margin_view);
            if (margin >= 0.0f) {
                block->bound->margin.top = margin;
                block->bound->margin.top_specificity = -1;
                log_debug("[HTML] BODY topmargin attribute: %.0fpx", margin);
            }
        }
        // overflow: visible (CSS default - no special overflow handling for body)
        break;
    }
    case HTM_TAG_H1:
        em_size = 2;  // 2em font-size
        goto HEADING_PROP;
    case HTM_TAG_H2:
        em_size = 1.5;  // 1.5em font-size
        goto HEADING_PROP;
    case HTM_TAG_H3:
        em_size = 1.17;  // 1.17em font-size
        goto HEADING_PROP;
    case HTM_TAG_H4:
        em_size = 1;  // 1em font-size
        goto HEADING_PROP;
    case HTM_TAG_H5:
        em_size = 0.83;  // 0.83em font-size
        goto HEADING_PROP;
    case HTM_TAG_H6:
        em_size = 0.67;  // 0.67em font-size
        HEADING_PROP: {
        // Font styles
        FontProp* font = ensure_html_block_font(lycon, block);
        float heading_font_size = lycon->font.style->font_size * em_size;
        apply_html_font_size(font, heading_font_size, false);
        apply_html_font_weight_bold(font);
        // Default margins for headings (browser UA stylesheet)
        // margin: 0.67em 0 for h1, varying for other levels
        // The margin is relative to the heading's computed font-size
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        float margin_em;
        switch (elmt_name) {
            case HTM_TAG_H1: margin_em = 0.67; break;
            case HTM_TAG_H2: margin_em = 0.83; break;
            case HTM_TAG_H3: margin_em = 1.00; break;
            case HTM_TAG_H4: margin_em = 1.33; break;
            case HTM_TAG_H5: margin_em = 1.67; break;
            case HTM_TAG_H6: margin_em = 2.33; break;
            default: margin_em = 0.67; break;
        }
        block->bound->margin.top = block->bound->margin.bottom = heading_font_size * margin_em;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        // Handle HTML align attribute (e.g., align="left", align="right", align="center")
        {
            const char* align_attr = elmt->get_attribute("align");
            if (align_attr) {
                if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                if (str_ieq_const(align_attr, strlen(align_attr), "left")) {
                    block->blk->text_align = CSS_VALUE_LEFT;
                } else if (str_ieq_const(align_attr, strlen(align_attr), "right")) {
                    block->blk->text_align = CSS_VALUE_RIGHT;
                } else if (str_ieq_const(align_attr, strlen(align_attr), "center")) {
                    block->blk->text_align = CSS_VALUE_CENTER;
                } else if (str_ieq_const(align_attr, strlen(align_attr), "justify")) {
                    block->blk->text_align = CSS_VALUE_JUSTIFY;
                }
            }
        }
        break;
    }
    case HTM_TAG_P: {
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // margin: 1em 0;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        // Handle HTML align attribute (e.g., align="left", align="right", align="center")
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            if (str_ieq_const(align_attr, strlen(align_attr), "left")) {
                block->blk->text_align = CSS_VALUE_LEFT;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "right")) {
                block->blk->text_align = CSS_VALUE_RIGHT;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "center")) {
                block->blk->text_align = CSS_VALUE_CENTER;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "justify")) {
                block->blk->text_align = CSS_VALUE_JUSTIFY;
            }
        }
        break;
    }
    case HTM_TAG_UL:  case HTM_TAG_OL:  case HTM_TAG_MENU:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        if (!block->bound) {
            block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        // margin: 1em 0; padding: 0 0 0 40px;
        // UA stylesheet: nested lists have margin: 0
        // Chrome: :is(ul, ol, dir, menu, dl) ul, :is(ul, ol, dir, menu, dl) ol { margin-block: 0 }
        // Also: ul ul { list-style-type: circle }, ul ul ul { list-style-type: square }
        {
            bool is_nested = false;
            int ul_ancestor_count = 0;
            DomNode* ancestor = elmt->parent;
            while (ancestor) {
                if (ancestor->is_element()) {
                    uintptr_t atag = ancestor->tag();
                    if (atag == HTM_TAG_UL || atag == HTM_TAG_OL ||
                        atag == HTM_TAG_MENU || atag == HTM_TAG_DIR ||
                        atag == HTM_TAG_DL) {
                        is_nested = true;
                    }
                    if (atag == HTM_TAG_UL) {
                        ul_ancestor_count++;
                    }
                }
                ancestor = ancestor->parent;
            }
            if (elmt_name == HTM_TAG_UL) {
                // Browser UA: ul=disc, ul ul=circle, ul ul ul (and deeper)=square
                block->blk->list_style_type =
                    (ul_ancestor_count == 0) ? CSS_VALUE_DISC :
                    (ul_ancestor_count == 1) ? CSS_VALUE_CIRCLE :
                                               CSS_VALUE_SQUARE;
            } else {
                block->blk->list_style_type = CSS_VALUE_DECIMAL;
            }
            if (!is_nested) {
                block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
                block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
            }
        }
        block->bound->padding.left = 40;  // CSS logical pixels
        block->bound->padding.left_specificity = -1;
        break;
    case HTM_TAG_CENTER:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        break;
    case HTM_TAG_DIV: {
        // HTML spec §14.3.3: <div align> maps to text-align
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            size_t alen = strlen(align_attr);
            if (str_ieq_const(align_attr, alen, "left")) {
                block->blk->text_align = CSS_VALUE_LEFT;
                block->blk->legacy_block_align = CSS_VALUE_LEFT;
            } else if (str_ieq_const(align_attr, alen, "right")) {
                block->blk->text_align = CSS_VALUE_RIGHT;
                block->blk->legacy_block_align = CSS_VALUE_RIGHT;
            } else if (str_ieq_const(align_attr, alen, "center")) {
                block->blk->text_align = CSS_VALUE_CENTER;
                block->blk->legacy_align_center_blocks = true;
                block->blk->legacy_block_align = CSS_VALUE_CENTER;
            } else if (str_ieq_const(align_attr, alen, "justify")) {
                block->blk->text_align = CSS_VALUE_JUSTIFY;
            }
        }
        break;
    }
    case HTM_TAG_IMG:  { // get html width and height (before the css styles)
        const char *value;
        value = elmt->get_attribute("width");
        if (value) {
            HtmlDimensionAttr width_attr;
            if (parse_html_dimension_attr(value, true, true, &width_attr)) {
                if (width_attr.is_percent) {
                    // HTML attribute width="50%" — percentage of containing block
                    float container_width = lycon->block.content_width > 0
                        ? lycon->block.content_width : 0;
                    apply_html_width_percent(lycon, block, width_attr.value, container_width);
                    log_debug("[HTML] IMG width attribute: %.0f%% -> %.1fpx", width_attr.value, lycon->block.given_width);
                } else {
                    // HTML spec: non-negative integer must start with ASCII digit; skip "auto" etc.
                    apply_html_width_px(lycon, block, width_attr.value);
                }
            }
        }
        value = elmt->get_attribute("height");
        if (value) {
            HtmlDimensionAttr height_attr;
            if (parse_html_dimension_attr(value, true, true, &height_attr)) {
                if (height_attr.is_percent) {
                    // HTML attribute height="50%" — percentage of containing block
                    float container_height = lycon->block.content_height > 0
                        ? lycon->block.content_height : 0;
                    apply_html_height_percent(lycon, block, height_attr.value, container_height);
                    log_debug("[HTML] IMG height attribute: %.0f%% -> %.1fpx", height_attr.value, lycon->block.given_height);
                } else {
                    // HTML spec: non-negative integer must start with ASCII digit; skip "auto" etc.
                    apply_html_height_px(lycon, block, height_attr.value);
                }
            }
        }
        // HTML spec §14.3.3: <img align="left|right"> maps to float: left|right
        {
            const char* align_attr = elmt->get_attribute("align");
            if (align_attr) {
                size_t align_len = strlen(align_attr);
                if (str_ieq_const(align_attr, align_len, "left")) {
                    if (!block->position) { block->position = alloc_position_prop(lycon); }
                    block->position->float_prop = CSS_VALUE_LEFT;
                } else if (str_ieq_const(align_attr, align_len, "right")) {
                    if (!block->position) { block->position = alloc_position_prop(lycon); }
                    block->position->float_prop = CSS_VALUE_RIGHT;
                }
            }
        }
        break;
    }
    case HTM_TAG_IFRAME: {
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // HTML spec §15.5.14: iframe { border: 2px inset; }
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left = 2;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        block->bound->border->top_style = block->bound->border->bottom_style = CSS_VALUE_INSET;
        block->bound->border->left_style = block->bound->border->right_style = CSS_VALUE_INSET;
        block->bound->border->top_color.r = block->bound->border->top_color.g =
            block->bound->border->top_color.b = 128; block->bound->border->top_color.a = 255;
        block->bound->border->left_color = block->bound->border->top_color;
        block->bound->border->bottom_color.r = block->bound->border->bottom_color.g =
            block->bound->border->bottom_color.b = 192; block->bound->border->bottom_color.a = 255;
        block->bound->border->right_color = block->bound->border->bottom_color;
        const char* frameborder_attr = elmt->get_attribute("frameborder");
        if (frameborder_attr) {
            size_t frameborder_len = strlen(frameborder_attr);
            bool no_frameborder = str_ieq_const(frameborder_attr, frameborder_len, "0") ||
                str_ieq_const(frameborder_attr, frameborder_len, "no");
            if (no_frameborder) {
                // Legacy frameborder=0 suppresses the iframe UA border before author CSS overrides.
                block->bound->border->width.top = block->bound->border->width.right =
                    block->bound->border->width.bottom = block->bound->border->width.left = 0.0f;
                block->bound->border->top_style = block->bound->border->right_style =
                    block->bound->border->bottom_style = block->bound->border->left_style = CSS_VALUE_NONE;
            }
        }
        if (!block->scroller) { block->scroller = alloc_scroll_prop(lycon); }
        block->scroller->overflow_x = CSS_VALUE_AUTO;
        block->scroller->overflow_y = CSS_VALUE_AUTO;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        // Parse HTML width/height attributes; default 300x150 per HTML spec
        const char *value;
        value = elmt->get_attribute("width");
        if (value) {
            HtmlDimensionAttr width_attr;
            if (parse_html_dimension_attr(value, true, false, &width_attr)) {
                if (width_attr.is_percent) {
                    if (width_attr.value > 0.0f) {
                        apply_html_deferred_width_percent(lycon, block, width_attr.value);
                        log_debug("[HTML] IFRAME width attribute: %.0f%%", width_attr.value);
                    }
                } else {
                    apply_html_width_px(lycon, block, width_attr.value);
                }
            }
        } else {
            apply_html_width_px(lycon, block, 300.0f);  // default intrinsic width
        }
        value = elmt->get_attribute("height");
        if (value) {
            HtmlDimensionAttr height_attr;
            if (parse_html_dimension_attr(value, true, false, &height_attr)) {
                if (height_attr.is_percent) {
                    if (height_attr.value > 0.0f) {
                        apply_html_deferred_height_percent(lycon, block, height_attr.value);
                        log_debug("[HTML] IFRAME height attribute: %.0f%%", height_attr.value);
                    }
                } else {
                    apply_html_height_px(lycon, block, height_attr.value);
                }
            }
        } else {
            apply_html_height_px(lycon, block, 150.0f);  // default intrinsic height
        }
        break;
    }
    case HTM_TAG_EMBED:
        // replaced element with default 300x150 per HTML spec
        block->display.inner = RDT_DISPLAY_REPLACED;
        apply_html_context_default_size(lycon, 300.0f, 150.0f);
        if (const char* w_attr = elmt->get_attribute("width")) {
            HtmlDimensionAttr width_attr;
            if (parse_html_dimension_attr(w_attr, false, true, &width_attr)) {
                // HTML dimension attributes must persist in BlockProp for later replaced layout passes.
                apply_html_width_px(lycon, block, width_attr.value);
            }
        }
        if (const char* h_attr = elmt->get_attribute("height")) {
            HtmlDimensionAttr height_attr;
            if (parse_html_dimension_attr(h_attr, false, true, &height_attr)) {
                apply_html_height_px(lycon, block, height_attr.value);
            }
        }
        break;
    case HTM_TAG_WEBVIEW: {
        // webview: replaced element with default 300x150, supports width/height attributes
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        const char *value;
        value = elmt->get_attribute("width");
        if (value) {
            HtmlDimensionAttr width_attr;
            if (parse_html_dimension_attr(value, false, false, &width_attr)) {
                apply_html_width_px(lycon, block, width_attr.value);
            }
        } else {
            apply_html_width_px(lycon, block, 300.0f);
        }
        value = elmt->get_attribute("height");
        if (value) {
            HtmlDimensionAttr height_attr;
            if (parse_html_dimension_attr(value, false, false, &height_attr)) {
                apply_html_height_px(lycon, block, height_attr.value);
            }
        } else {
            apply_html_height_px(lycon, block, 150.0f);
        }
        break;
    }
    case HTM_TAG_AUDIO: {
        // HTML §4.8.9: <audio> without controls is not rendered
        // <audio controls> is a replaced inline-block element with intrinsic size 300×54
        // (Chrome default audio player dimensions)
        if (elmt->has_attribute("controls")) {
            block->display.inner = RDT_DISPLAY_REPLACED;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            lycon->block.given_width = 300;
            block->blk->given_width = 300;
            lycon->block.given_height = 54;
            block->blk->given_height = 54;
        }
        // audio-only playback: create RdtVideo (AVPlayer handles audio files natively)
        const char* src = elmt->get_attribute("src");
        if (src && *src && lycon->ui_context && lycon->ui_context->document && lycon->ui_context->document->url) {
            if (!block->embed) { block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp)); }
            if (!block->embed->video) {
                Url* abs_url = parse_url(lycon->ui_context->document->url, src);
                char* file_path = abs_url ? url_to_local_path(abs_url) : NULL;
                if (abs_url) url_destroy(abs_url);

                if (file_path) {
                    DomDocument* doc = lycon->ui_context->document;
                    DocState* doc_state = doc ? doc->state : NULL;
                    RdtVideo* video = rdt_video_create(&media_callbacks, doc_state);
                    if (video) {
                        log_debug("audio: opening file: %s", file_path);
                        rdt_video_open_file(video, file_path);
                        if (elmt->has_attribute("loop")) rdt_video_set_loop(video, true);
                        if (elmt->has_attribute("muted")) rdt_video_set_muted(video, true);
                        block->embed->video = video;
                        if (elmt->has_attribute("autoplay")) {
                            rdt_video_play(video);
                            if (doc->state) doc->state->has_active_video = true;
                        }
                    }
                    mem_free(file_path);
                } else {
                    log_error("audio: failed to resolve src path: %s", src);
                }
            }
        }
        break;
    }
    case HTM_TAG_VIDEO: {
        // replaced element with default 300x150 per HTML spec
        block->display.inner = RDT_DISPLAY_REPLACED;
        // Parse HTML width/height attributes; default 300x150
        if (const char* w_attr = elmt->get_attribute("width")) {
            HtmlDimensionAttr width_attr;
            if (parse_html_dimension_attr(w_attr, false, false, &width_attr)) {
                lycon->block.given_width = width_attr.value;
            } else {
                lycon->block.given_width = 300.0f;
            }
        } else { lycon->block.given_width = 300.0f; }
        if (const char* h_attr = elmt->get_attribute("height")) {
            HtmlDimensionAttr height_attr;
            if (parse_html_dimension_attr(h_attr, false, false, &height_attr)) {
                lycon->block.given_height = height_attr.value;
            } else {
                lycon->block.given_height = 150.0f;
            }
        } else { lycon->block.given_height = 150.0f; }

        // initialize video playback if src attribute is present
        const char* src = elmt->get_attribute("src");
        if (src && *src && lycon->ui_context && lycon->ui_context->document && lycon->ui_context->document->url) {
            if (!block->embed) { block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp)); }
            if (!block->embed->video) {
                // resolve relative src path against document URL (same as load_image)
                Url* abs_url = parse_url(lycon->ui_context->document->url, src);
                char* file_path = abs_url ? url_to_local_path(abs_url) : NULL;
                if (abs_url) url_destroy(abs_url);

                if (file_path) {
                    // preload attribute: "none" defers open until play
                    const char* preload = elmt->get_attribute("preload");
                    bool preload_none = preload && strcmp(preload, "none") == 0;

                    DomDocument* doc = lycon->ui_context->document;
                    DocState* doc_state = doc ? doc->state : NULL;
                    RdtVideo* video = rdt_video_create(&media_callbacks, doc_state);
                    if (video) {
                        if (!preload_none) {
                            log_debug("video: opening file: %s", file_path);
                            rdt_video_open_file(video, file_path);
                        } else {
                            log_debug("video: preload=none, deferring open: %s", file_path);
                        }
                        if (elmt->has_attribute("loop")) rdt_video_set_loop(video, true);
                        if (elmt->has_attribute("muted")) rdt_video_set_muted(video, true);
                        block->embed->video = video;
                        // controls attribute
                        if (elmt->has_attribute("controls")) {
                            block->embed->has_controls = true;
                        }
                        // poster attribute: load poster image
                        const char* poster_src = elmt->get_attribute("poster");
                        if (poster_src && *poster_src) {
                            block->embed->poster = load_image(lycon->ui_context, poster_src);
                            if (block->embed->poster) {
                                log_debug("video: loaded poster image: %s", poster_src);
                            }
                        }
                        // autoplay: start playback immediately
                        if (elmt->has_attribute("autoplay")) {
                            if (preload_none) {
                                // need to open first when preload=none + autoplay
                                rdt_video_open_file(video, file_path);
                            }
                            rdt_video_play(video);
                            // enable continuous redraw for video playback
                            if (doc->state) doc->state->has_active_video = true;
                        }
                    }
                    mem_free(file_path);
                } else {
                    log_error("video: failed to resolve src path: %s", src);
                }
            }
        }
        break;
    }
    case HTM_TAG_CANVAS:
        // HTML §4.12.5: canvas is a replaced element with default
        // coordinate-space dimensions 300x150 when attributes are absent.
        block->display.inner = RDT_DISPLAY_REPLACED;
        lycon->block.given_width = 300;
        lycon->block.given_height = 150;
        if (const char* w_attr = elmt->get_attribute("width")) {
            size_t w_len = strlen(w_attr);
            if (w_len > 0 && w_attr[0] >= '0' && w_attr[0] <= '9') {
                StrView w_view = strview_init(w_attr, w_len);
                float w = strview_to_int(&w_view);
                if (w >= 0.0f) lycon->block.given_width = w;
            }
        }
        if (const char* h_attr = elmt->get_attribute("height")) {
            size_t h_len = strlen(h_attr);
            if (h_len > 0 && h_attr[0] >= '0' && h_attr[0] <= '9') {
                StrView h_view = strview_init(h_attr, h_len);
                float h = strview_to_int(&h_view);
                if (h >= 0.0f) lycon->block.given_height = h;
            }
        }
        break;
    case HTM_TAG_OBJECT:
        // HTML §4.8.7: <object> is replaced only when it has a data attribute.
        // Without data, it renders its fallback content (children) as normal flow.
        if (elmt->get_attribute("data")) {
            block->display.inner = RDT_DISPLAY_REPLACED;
            lycon->block.given_width = 300;
            lycon->block.given_height = 150;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            block->blk->given_width = 300;
            block->blk->given_height = 150;
            if (const char* w_attr = elmt->get_attribute("width")) {
                size_t w_len = strlen(w_attr);
                if (w_len > 0 && w_attr[0] >= '0' && w_attr[0] <= '9') {
                    StrView w_view = strview_init(w_attr, w_len);
                    float w = strview_to_int(&w_view);
                    if (w >= 0.0f) {
                        // Object replaced sizing follows its legacy HTML dimension attributes.
                        lycon->block.given_width = w;
                        block->blk->given_width = w;
                    }
                }
            }
            if (const char* h_attr = elmt->get_attribute("height")) {
                size_t h_len = strlen(h_attr);
                if (h_len > 0 && h_attr[0] >= '0' && h_attr[0] <= '9') {
                    StrView h_view = strview_init(h_attr, h_len);
                    float h = strview_to_int(&h_view);
                    if (h >= 0.0f) {
                        lycon->block.given_height = h;
                        block->blk->given_height = h;
                    }
                }
            }
        }
        break;
    case HTM_TAG_HR:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // hr default: 1px border on all sides (creates 2px height from border-top + border-bottom)
        // This matches browser UA stylesheet behavior (CSS logical pixels)
        block->bound->border->width.top = block->bound->border->width.bottom = 1;
        block->bound->border->width.left = block->bound->border->width.right = 1;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        // Default border style: inset (typical browser default for hr)
        block->bound->border->top_style = block->bound->border->bottom_style = CSS_VALUE_INSET;
        block->bound->border->left_style = block->bound->border->right_style = CSS_VALUE_INSET;
        // Default border colors for inset style: darker gray on top/left, lighter on bottom/right
        // Top/left: dark gray for 3D inset effect
        block->bound->border->top_color.r = 128; block->bound->border->top_color.g = 128;
        block->bound->border->top_color.b = 128; block->bound->border->top_color.a = 255;
        block->bound->border->left_color.r = 128; block->bound->border->left_color.g = 128;
        block->bound->border->left_color.b = 128; block->bound->border->left_color.a = 255;
        // Bottom/right: lighter for 3D inset effect
        block->bound->border->bottom_color.r = 192; block->bound->border->bottom_color.g = 192;
        block->bound->border->bottom_color.b = 192; block->bound->border->bottom_color.a = 255;
        block->bound->border->right_color.r = 192; block->bound->border->right_color.g = 192;
        block->bound->border->right_color.b = 192; block->bound->border->right_color.a = 255;
        // 8px margin top/bottom, auto left/right for horizontal centering (browser default)
        block->bound->margin.top = block->bound->margin.bottom = 8;  // CSS logical pixels
        block->bound->margin.left = block->bound->margin.right = 0;
        block->bound->margin.left_type = CSS_VALUE_AUTO;
        block->bound->margin.right_type = CSS_VALUE_AUTO;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        {
            const char* width_attr = elmt->get_attribute("width");
            if (width_attr) {
                size_t value_len = strlen(width_attr);
                if (value_len > 0 && width_attr[value_len - 1] == '%') {
                    StrView width_view = strview_init(width_attr, value_len - 1);
                    float percent = (float)strview_to_int(&width_view);
                    if (percent >= 0.0f) {
                        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                        block->blk->given_width = -1.0f;
                        block->blk->given_width_percent = percent;
                        log_debug("[HTML] HR width attribute: %.0f%%", percent);
                    }
                } else if (width_attr[0] >= '0' && width_attr[0] <= '9') {
                    StrView width_view = strview_init(width_attr, value_len);
                    float width = (float)strview_to_int(&width_view);
                    if (width >= 0.0f) {
                        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                        lycon->block.given_width = width;
                        block->blk->given_width = width;
                        block->blk->given_width_percent = NAN;
                        log_debug("[HTML] HR width attribute: %.0fpx", width);
                    }
                }
            }
        }
        break;
    case HTM_TAG_B:
        apply_html_span_bold(lycon, span);
        break;
    case HTM_TAG_I:
        apply_html_span_font_style(lycon, span, CSS_VALUE_ITALIC);
        break;
    case HTM_TAG_U:
        apply_html_span_text_deco(lycon, span, CSS_VALUE_UNDERLINE);
        break;
    case HTM_TAG_S:
        apply_html_span_text_deco(lycon, span, CSS_VALUE_LINE_THROUGH);
        break;
    case HTM_TAG_FONT: {
        // parse font style
        // Get color attribute using DomNode interface
        const char* color_attr = span->get_attribute("color");
        if (color_attr) {
            if (!span->in_line) { span->in_line = alloc_inline_prop(lycon); }
            span->in_line->color = parse_html_color(color_attr);
            span->in_line->has_color = true;
            log_debug("HTM_TAG_FONT color: %s -> rgb(%d,%d,%d)", color_attr,
                      span->in_line->color.r, span->in_line->color.g, span->in_line->color.b);
        }
        // Handle font size attribute (deprecated HTML but still supported)
        // size="1" = x-small (10px), size="2" = small (13px), size="3" = medium (16px, default)
        // size="4" = large (18px), size="5" = x-large (24px), size="6" = xx-large (32px), size="7" = 48px
        const char* size_attr = span->get_attribute("size");
        if (size_attr) {
            bool relative_size = size_attr[0] == '+' || size_attr[0] == '-';
            int size_value = (int)str_to_int64_default(size_attr, strlen(size_attr), 0);
            float font_size = 16.0f;  // default medium
            if (relative_size) {
                int level = html_font_level_for_size(lycon->font.style->font_size);
                level += size_value;
                font_size = html_font_size_for_level(level);
            } else {
                font_size = html_font_size_for_level(size_value);
            }
            apply_html_span_font_size(lycon, span, font_size, false);  // CSS logical pixels
            log_debug("HTM_TAG_FONT size='%s' -> %.1fpx", size_attr, span->font->font_size);
        }
        // Handle font face attribute
        const char* face_attr = span->get_attribute("face");
        if (face_attr) {
            radiant_retain_font_family(ensure_html_span_font(lycon, span), lam::PoolPtr<char>((char*)face_attr));  // store font family name
            log_debug("HTM_TAG_FONT face: %s", face_attr);
        }
        break;
    }
    case HTM_TAG_A: {
        // anchor style
        if (!span->in_line) { span->in_line = alloc_inline_prop(lycon); }
        span->in_line->cursor = CSS_VALUE_POINTER;
        span->in_line->color = color_name_to_rgb(CSS_VALUE_BLUE);
        span->in_line->has_color = true;
        apply_html_span_text_deco(lycon, span, CSS_VALUE_UNDERLINE);
        break;
    }
    // ========== Additional text formatting elements ==========
    case HTM_TAG_STRONG:
        apply_html_span_bold(lycon, span);
        break;
    case HTM_TAG_EM:  case HTM_TAG_CITE:  case HTM_TAG_DFN:  case HTM_TAG_VAR:
        apply_html_span_font_style(lycon, span, CSS_VALUE_ITALIC);
        break;
    case HTM_TAG_CODE:  case HTM_TAG_KBD:  case HTM_TAG_SAMP:  case HTM_TAG_TT: {
        // monospace font family
        apply_html_span_monospace_font(lycon, span);
        // Browser quirk (Chromium CheckForGenericFamilyChange): when font-family
        // transitions to monospace and no explicit font-size on this element,
        // scale inherited size by 13/16. Only applies when the inherited font-size
        // originates from the CSS 'medium' keyword (initial value), not from an
        // explicit font-size declaration like '12px'.
        bool parent_is_mono = lycon->font.style && lycon->font.style->family &&
            str_ieq_const(lycon->font.style->family, strlen(lycon->font.style->family), "monospace");
        if (!parent_is_mono && span->font->font_size > 0 && span->font->font_size_from_medium) {
            span->font->font_size = span->font->font_size * 13.0f / 16.0f;
        }
        break;
    }
    case HTM_TAG_MARK:
        // yellow background highlight - handled via background property on block
        // Note: InlineProp doesn't have bg_color; would need BackgroundProp
        break;
    case HTM_TAG_SMALL:
        // font-size: smaller (0.83em)
        apply_html_span_font_size(lycon, span, lycon->font.style->font_size * 0.83f, false);
        break;
    case HTM_TAG_BIG:
        // font-size: larger (1.17em) - deprecated but still supported
        apply_html_span_font_size(lycon, span, lycon->font.style->font_size * 1.17f, false);
        break;
    case HTM_TAG_SUB:
        // subscript: smaller font, lowered baseline
        apply_html_span_font_size(lycon, span, lycon->font.style->font_size * 0.83f, false);
        if (!span->in_line) { span->in_line = alloc_inline_prop(lycon); }
        span->in_line->vertical_align = CSS_VALUE_SUB;
        break;
    case HTM_TAG_SUP:
        // superscript: smaller font, raised baseline
        apply_html_span_font_size(lycon, span, lycon->font.style->font_size * 0.83f, false);
        if (!span->in_line) { span->in_line = alloc_inline_prop(lycon); }
        span->in_line->vertical_align = CSS_VALUE_SUPER;
        break;
    case HTM_TAG_DEL:  case HTM_TAG_STRIKE:
        // strikethrough
        apply_html_span_text_deco(lycon, span, CSS_VALUE_LINE_THROUGH);
        break;
    case HTM_TAG_INS:
        // underline for inserted text
        apply_html_span_text_deco(lycon, span, CSS_VALUE_UNDERLINE);
        break;
    case HTM_TAG_Q:
        // inline quotation - browser adds quotes via CSS content, we just style italic
        apply_html_span_font_style(lycon, span, CSS_VALUE_ITALIC);
        break;
    case HTM_TAG_ABBR:  case HTM_TAG_ACRONYM:
        // abbreviation/acronym - dotted underline in some browsers
        // we'll use standard underline for simplicity
        apply_html_span_text_deco(lycon, span, CSS_VALUE_UNDERLINE);
        break;
    // ========== Block elements ==========
    case HTM_TAG_PRE:  case HTM_TAG_LISTING:  case HTM_TAG_XMP: {
        // preformatted: monospace, preserve whitespace, margin 1em 0
        apply_html_block_monospace_font(lycon, block);
        // Browser quirk (Chromium CheckForGenericFamilyChange): when font-family
        // transitions to monospace and no explicit font-size on this element,
        // scale inherited size by 13/16. Only applies when the inherited font-size
        // originates from the CSS 'medium' keyword (initial value).
        float pre_font_size = lycon->font.style->font_size;
        {
            bool parent_is_mono = lycon->font.style && lycon->font.style->family &&
                str_ieq_const(lycon->font.style->family, strlen(lycon->font.style->family), "monospace");
            if (!parent_is_mono && block->font->font_size > 0 && block->font->font_size_from_medium) {
                block->font->font_size = block->font->font_size * 13.0f / 16.0f;
            }
            if (!parent_is_mono && pre_font_size > 0 && block->font->font_size_from_medium) {
                pre_font_size = pre_font_size * 13.0f / 16.0f;
            }
        }
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->white_space = CSS_VALUE_PRE;
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = pre_font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        break;
    }
    case HTM_TAG_BLOCKQUOTE:
        // margin: 1em 40px
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.left = block->bound->margin.right = 40;  // CSS logical pixels
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    case HTM_TAG_ADDRESS:
        // italic, block display
        ensure_html_block_font(lycon, block)->font_style = CSS_VALUE_ITALIC;
        break;
    case HTM_TAG_FIGURE:
        // margin: 1em 40px
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.left = block->bound->margin.right = 40;  // CSS logical pixels
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    case HTM_TAG_FIGCAPTION:
        // Chrome UA: figcaption is a plain block element, no special text-align
        break;
    case HTM_TAG_DL:
        // definition list: margin 1em 0
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        break;
    case HTM_TAG_DD:
        // definition description: margin-left 40px
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.left = 40;  // CSS logical pixels
        block->bound->margin.left_specificity = -1;
        break;
    case HTM_TAG_DT:
        // definition term: browser UA default is normal-weight block text.
        break;
    case HTM_TAG_LI:
        // list item: display list-item handled elsewhere
        break;
    case HTM_TAG_SUMMARY:
        // UA default: list-style: inside disclosure-closed
        // summary elements use inside marker position (disclosure triangle before text)
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->list_style_position = (CssEnum)1;  // 1 = inside
        block->blk->list_style_type = CSS_VALUE_DISCLOSURE_CLOSED;
        break;
    // ========== Table elements ==========
    case HTM_TAG_TABLE: {
        // HTML UA default: border-spacing: 2px (CSS spec default is 0, but HTML tables use 2px)
        // This is applied at the TableProp level in layout_table.cpp, not here in block props

        // Handle HTML width attribute (e.g., width="85%" or width="600")
        const char* width_attr = elmt->get_attribute("width");
        if (width_attr) {
            size_t value_len = strlen(width_attr);
            if (value_len > 0) {
                // Check if it's a percentage value (ends with %)
                if (width_attr[value_len - 1] == '%') {
                    // Parse percentage value
                    StrView width_view = strview_init(width_attr, value_len - 1);
                    float percent = strview_to_int(&width_view);
                    if (percent > 0 && percent <= 100) {
                        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                        block->blk->given_width_percent = percent;
                        // Calculate resolved width from container
                        float container_width = lycon->block.content_width > 0
                            ? lycon->block.content_width
                            : (lycon->line.right - lycon->line.left);
                        if (container_width > 0) {
                            lycon->block.given_width = container_width * percent / 100.0f;
                            block->blk->given_width = lycon->block.given_width;
                            log_debug("[HTML] TABLE width attribute: %.0f%% -> %.1fpx", percent, lycon->block.given_width);
                        }
                    }
                } else {
                    // Parse pixel value
                    StrView width_view = strview_init(width_attr, value_len);
                    float width = strview_to_int(&width_view);
                    if (width > 0) {
                        lycon->block.given_width = width;  // CSS logical pixels
                        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                        block->blk->given_width = lycon->block.given_width;
                        log_debug("[HTML] TABLE width attribute: %.0fpx", width);
                    }
                }
            }
        }
        // Handle HTML height attribute (e.g., height="200")
        const char* height_attr = elmt->get_attribute("height");
        if (height_attr) {
            size_t value_len = strlen(height_attr);
            if (value_len > 0) {
                // Parse pixel value (percentages for table height are less common)
                StrView height_view = strview_init(height_attr, value_len);
                float height = strview_to_int(&height_view);
                if (height > 0) {
                    lycon->block.given_height = height;  // CSS logical pixels
                    if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                    block->blk->given_height = lycon->block.given_height;
                    log_debug("[HTML] TABLE height attribute: %.0fpx", height);
                }
            }
        }
        // Handle HTML bgcolor attribute (e.g., bgcolor="#f6f6ef")
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            apply_html_background_color(lycon, block, bg_color);
            log_debug("[HTML] TABLE bgcolor attribute: #%02x%02x%02x", bg_color.r, bg_color.g, bg_color.b);
        }
        // Handle HTML border attribute (e.g., border="5")
        // Per WHATWG 15.3.10: table[border] { border-style: outset; border-color: grey; }
        // border-width is the attribute value in pixels
        const char* border_attr = elmt->get_attribute("border");
        if (border_attr) {
            StrView bv = strview_init(border_attr, strlen(border_attr));
            float border_width = strview_to_int(&bv);
            if (border_width >= 0) {
                // border-color: grey (128, 128, 128)
                Color grey = (Color){ .r=128, .g=128, .b=128, .a=255 };
                apply_html_uniform_border(lycon, block, border_width, CSS_VALUE_OUTSET, grey);
                log_debug("[HTML] TABLE border attribute: %.0fpx outset grey", border_width);
            }
        }
        // HTML spec §14.3.3: <table align="center"> maps to margin-left: auto; margin-right: auto
        // <table align="left|right"> maps to float: left|right
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            size_t alen = strlen(align_attr);
            if (str_ieq_const(align_attr, alen, "center")) {
                if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
                block->bound->margin.left_type = CSS_VALUE_AUTO;
                block->bound->margin.right_type = CSS_VALUE_AUTO;
                block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
                log_debug("[HTML] TABLE align=center: margin-left/right auto");
            } else if (str_ieq_const(align_attr, alen, "left")) {
                if (!block->position) { block->position = alloc_position_prop(lycon); }
                block->position->float_prop = CSS_VALUE_LEFT;
                log_debug("[HTML] TABLE align=left: float left");
            } else if (str_ieq_const(align_attr, alen, "right")) {
                if (!block->position) { block->position = alloc_position_prop(lycon); }
                block->position->float_prop = CSS_VALUE_RIGHT;
                log_debug("[HTML] TABLE align=right: float right");
            }
        }
        break;
    }
    case HTM_TAG_TR: {
        // Browser UA stylesheet: tbody/thead/tfoot default to vertical-align:
        // middle, and table rows inherit it. Preserve that computed value even
        // when author CSS changes a <tr> to an inline box.
        if (!block->in_line) { block->in_line = alloc_inline_prop(lycon); }
        block->in_line->vertical_align = CSS_VALUE_MIDDLE;

        // Handle HTML bgcolor attribute for table rows
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            apply_html_background_color(lycon, block, bg_color);
            log_debug("[HTML] TR bgcolor attribute: #%02x%02x%02x", bg_color.r, bg_color.g, bg_color.b);
        }
        break;
    }
    case HTM_TAG_TH: {
        apply_html_table_cell_defaults(lycon, elmt, block, true);
        break;
    }
    case HTM_TAG_TD: {
        apply_html_table_cell_defaults(lycon, elmt, block, false);
        break;
    }
    case HTM_TAG_CAPTION:
        // table caption: text-align center
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        break;
    // ========== Form elements ==========
    case HTM_TAG_FIELDSET:
        // fieldset: border and padding (CSS logical pixels)
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // Chrome uses groove style with gray color for fieldset borders
        apply_html_uniform_border(lycon, block, 2.0f, CSS_VALUE_GROOVE,
            (Color){ .r=192, .g=192, .b=192, .a=255 });
        block->bound->padding.top = 0.35 * lycon->font.style->font_size;
        block->bound->padding.bottom = 0.625 * lycon->font.style->font_size;
        block->bound->padding.left = block->bound->padding.right = 0.75 * lycon->font.style->font_size;
        block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
            block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        block->bound->margin.left = block->bound->margin.right = 2;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    case HTM_TAG_LEGEND:
        // legend: padding (CSS logical pixels)
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->padding.left = block->bound->padding.right = 2;
        block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        break;
    case HTM_TAG_BUTTON: {
        // button: centered text, some padding, inline-block display with flow inner
        // All values in CSS logical pixels
        // Guard: only allocate if not already allocated (avoid re-allocating on repeated style resolution)
        bool form_created = false;
        ensure_form_control_prop(lycon, block, FORM_CONTROL_BUTTON, &form_created);
        if (form_created) {
            if (block->has_attribute("disabled")) {
                DocState* state = (DocState*)lycon->doc->state;
                form_control_set_disabled(state, block, true);
            }
        }

        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = CSS_VALUE_FLOW;  // button has flow children
        // Button sizing is determined by content - will be shrink-to-fit

        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        // Chrome UA: font-size 13.3333px, font-family Arial for form controls
        apply_html_form_control_font(lycon, block);
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->padding.top = block->bound->padding.bottom = FormDefaults::BUTTON_PADDING_V;
        block->bound->padding.left = block->bound->padding.right = FormDefaults::BUTTON_PADDING_H;
        block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
            block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        // Default border: 2px outset (Chrome UA stylesheet) — same as <input type=button/submit>
        apply_html_uniform_border_style(lycon, block, FormDefaults::BUTTON_BORDER, CSS_VALUE_OUTSET);
        break;
    }
    case HTM_TAG_INPUT: {
        // Allocate form control prop - all values in CSS logical pixels
        // Guard: only allocate if not already allocated (avoid re-allocating on repeated style resolution)
        bool form_created = false;
        ensure_form_control_prop(lycon, block, FORM_CONTROL_TEXT, &form_created);
        if (form_created) {
            // Parse state attributes and seed canonical StateStore state.
            DocState* state = (DocState*)lycon->doc->state;
            block->form->state_ref = state;
            if (block->has_attribute("disabled")) {
                form_control_set_disabled(state, block, true);
            }
            if (block->has_attribute("readonly")) {
                form_control_set_readonly(state, block, true);
            }
            if (block->has_attribute("checked")) {
                form_control_set_checked(state, block, true);
            }
            if (block->has_attribute("required")) {
                form_control_set_required(state, block, true);
            }
        }

        // JS DOM helpers may create FormControlProp before HTML style resolution
        // runs. Refresh attribute-backed metadata every pass so the form control
        // type and default value reflect the current DOM attributes.
        const char* type = block->get_attribute("type");
        block->form->input_type = type;
        block->form->control_type = get_input_control_type(type);
        block->form->value = block->get_attribute("value");
        block->form->placeholder = block->get_attribute("placeholder");
        block->form->name = block->get_attribute("name");

        const char* size_attr = block->get_attribute("size");
        block->form->size = FormDefaults::TEXT_SIZE_CHARS;
        if (size_attr) {
            block->form->size = (int)str_to_int64_default(size_attr, strlen(size_attr), 0); // INT_CAST_OK: HTML size attribute is a character count
            if (block->form->size <= 0) block->form->size = FormDefaults::TEXT_SIZE_CHARS;
        }

        // Set display and intrinsic size based on control type
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }

        // Chrome UA: font-size 13.3333px, font-family Arial for all form controls
        apply_html_form_control_font(lycon, block);

        switch (block->form->control_type) {
        case FORM_CONTROL_HIDDEN:
            block->display.outer = CSS_VALUE_NONE;
            break;
        case FORM_CONTROL_CHECKBOX:
        case FORM_CONTROL_RADIO:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            block->form->intrinsic_width = FormDefaults::CHECK_SIZE;
            block->form->intrinsic_height = FormDefaults::CHECK_SIZE;
            // Set given_width/height so layout algorithm uses intrinsic size
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
            // Default margin: Chrome UA stylesheet
            // checkbox: 3px 3px 3px 4px, radio: 3px 3px 0px 5px
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->margin.top = 3; block->bound->margin.right = FormDefaults::CHECK_MARGIN;
            block->bound->margin.bottom = (block->form->control_type == FORM_CONTROL_RADIO) ? 0 : FormDefaults::CHECK_MARGIN;
            block->bound->margin.left = (block->form->control_type == FORM_CONTROL_RADIO) ? FormDefaults::RADIO_MARGIN_LEFT : FormDefaults::CHECKBOX_MARGIN_LEFT;
            block->bound->margin.top_specificity = block->bound->margin.right_specificity =
                block->bound->margin.bottom_specificity = block->bound->margin.left_specificity = -1;
            break;
        case FORM_CONTROL_BUTTON:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            // Button intrinsic size depends on value text - computed in layout
            // Default padding: 1px 6px (Chrome UA stylesheet)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.bottom = FormDefaults::BUTTON_PADDING_V;
            block->bound->padding.left = block->bound->padding.right = FormDefaults::BUTTON_PADDING_H;
            block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
                block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
            // Default border: 2px outset (Chrome UA stylesheet)
            apply_html_uniform_border_style(lycon, block, FormDefaults::BUTTON_BORDER, CSS_VALUE_OUTSET);
            break;
        case FORM_CONTROL_IMAGE:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            // Image button is a replaced element; width/height content attributes
            // override the missing-image fallback dimensions.
            {
                float image_width = FormDefaults::IMAGE_INPUT_WIDTH;
                float image_height = FormDefaults::IMAGE_INPUT_HEIGHT;
                const char* width_attr = block->get_attribute("width");
                const char* height_attr = block->get_attribute("height");
                bool has_width_attr = false;
                bool has_height_attr = false;
                if (width_attr) {
                    float parsed_width = (float)str_to_double_default(width_attr, strlen(width_attr), -1.0);
                    if (parsed_width >= 0.0f) {
                        image_width = parsed_width;
                        has_width_attr = true;
                    }
                }
                if (height_attr) {
                    float parsed_height = (float)str_to_double_default(height_attr, strlen(height_attr), -1.0);
                    if (parsed_height >= 0.0f) {
                        image_height = parsed_height;
                        has_height_attr = true;
                    }
                }
                if (has_width_attr && !has_height_attr) {
                    // Chrome gives a width-constrained broken image submit control
                    // a square fallback glyph plus 1px control reserve.
                    image_height = image_width + 1.0f;
                }
                block->form->intrinsic_width = image_width;
                block->form->intrinsic_height = image_height;
            }
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
            break;
        case FORM_CONTROL_RANGE: {
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            block->form->intrinsic_width = FormDefaults::RANGE_WIDTH;
            // On macOS Chrome, a range with a list attribute (tick marks) renders taller (22px) than one without (16px)
            const char* list_attr = block->get_attribute("list");
            block->form->intrinsic_height = list_attr ? FormDefaults::RANGE_HEIGHT_WITH_LIST : FormDefaults::RANGE_HEIGHT;
            // Set given_width/height so layout algorithm uses intrinsic size (border-box)
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
            // Chrome default margin: 2px all sides
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->margin.top = block->bound->margin.right =
                block->bound->margin.bottom = block->bound->margin.left = 2;
            block->bound->margin.top_specificity = block->bound->margin.right_specificity =
                block->bound->margin.bottom_specificity = block->bound->margin.left_specificity = -1;
            // Parse range attributes
            const char* min_attr = block->get_attribute("min");
            const char* max_attr = block->get_attribute("max");
            const char* step_attr = block->get_attribute("step");
            if (min_attr) block->form->range_min = str_to_double_default(min_attr, strlen(min_attr), 0.0);
            if (max_attr) block->form->range_max = str_to_double_default(max_attr, strlen(max_attr), 0.0);
            if (step_attr) block->form->range_step = str_to_double_default(step_attr, strlen(step_attr), 0.0);
            if (block->form->value) {
                float val = (float)str_to_double_default(block->form->value, strlen(block->form->value), 0.0);
                float normalized = (val - block->form->range_min) /
                    (block->form->range_max - block->form->range_min);
                DocState* state = lycon && lycon->doc ? (DocState*)lycon->doc->state : nullptr;
                form_control_set_range_value(state, (View*)block, normalized);
            }
            break;
        }
        default:  // FORM_CONTROL_TEXT
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            // File inputs: Chrome renders as 253×21 border-box with no external border/padding
            // (the internal "Choose File" button + label text are shadow DOM)
            if (block->form->input_type && strcmp(block->form->input_type, "file") == 0) {
                block->form->intrinsic_width = 253.0f;
                block->form->intrinsic_height = FormDefaults::TEXT_HEIGHT;
                if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                block->blk->given_width = 253.0f;
                block->blk->given_height = FormDefaults::TEXT_HEIGHT;
                block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
                break;
            }
            block->form->intrinsic_width = FormDefaults::TEXT_WIDTH;
            // Content-area height: TEXT_HEIGHT (21 border-box) minus default border+padding.
            // Chrome uses fixed 21px border-box for all text inputs regardless of font-size.
            block->form->intrinsic_height = FormDefaults::TEXT_HEIGHT
                - 2 * (FormDefaults::TEXT_BORDER + FormDefaults::TEXT_PADDING_V);
            // Don't set given_width/given_height — layout_form_control computes
            // intrinsic size dynamically from size attribute and font metrics
            // Default border for text inputs (CSS logical pixels)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            apply_html_uniform_border(lycon, block, FormDefaults::TEXT_BORDER, CSS_VALUE_SOLID,
                (Color){ .r=118, .g=118, .b=118, .a=255 });
            // Chrome UA: date/time inputs have padding=0; text-like inputs have padding=1
            {
                const char* itype = block->form->input_type;
                bool is_date_time = itype && (
                    strcmp(itype, "date") == 0 || strcmp(itype, "time") == 0 ||
                    strcmp(itype, "datetime-local") == 0 || strcmp(itype, "month") == 0 ||
                    strcmp(itype, "week") == 0);
                if (is_date_time) {
                    block->bound->padding.top = block->bound->padding.bottom = 0;
                    block->bound->padding.left = block->bound->padding.right = 0;
                } else {
                    block->bound->padding.top = block->bound->padding.bottom = FormDefaults::TEXT_PADDING_V;
                    block->bound->padding.left = block->bound->padding.right = FormDefaults::TEXT_PADDING_H;
                }
            }
            block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
                block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
            break;
        }
        break;
    }
    case HTM_TAG_SELECT: {
        // All values in CSS logical pixels
        // Guard: only allocate if not already allocated (avoid re-allocating on repeated style resolution)
        bool form_created = false;
        ensure_form_control_prop(lycon, block, FORM_CONTROL_SELECT, &form_created);
        if (form_created) {
            block->form->name = block->get_attribute("name");
            if (block->has_attribute("disabled")) {
                DocState* state = (DocState*)lycon->doc->state;
                form_control_set_disabled(state, block, true);
            }
            if (block->has_attribute("multiple")) block->form->multiple = 1;
            // HTML §4.10.7: size attr specifies visible rows in listbox mode
            const char* size_attr = block->get_attribute("size");
            if (size_attr) {
                int size_val = (int)str_to_int64_default(size_attr, strlen(size_attr), 0);
                if (size_val > 0) block->form->select_size = size_val;
            }

            // Count options and find selected index
            int option_count = 0;
            int selected_idx = -1;
            DomNode* child = block->first_child;
            while (child) {
                if (child->is_element()) {
                    DomElement* child_elem = lam::dom_require_element(child);
                    if (child_elem->tag() == HTM_TAG_OPTION) {
                        if (child_elem->has_attribute("selected") && selected_idx < 0) {
                            selected_idx = option_count;
                        }
                        option_count++;
                    } else if (child_elem->tag() == HTM_TAG_OPTGROUP) {
                        // Count options inside optgroup
                        DomNode* opt_child = child_elem->first_child;
                        while (opt_child) {
                            if (opt_child->is_element()) {
                                DomElement* opt_elem = lam::dom_require_element(opt_child);
                                if (opt_elem->tag() == HTM_TAG_OPTION) {
                                    if (opt_elem->has_attribute("selected") && selected_idx < 0) {
                                        selected_idx = option_count;
                                    }
                                    option_count++;
                                }
                            }
                            opt_child = opt_child->next_sibling;
                        }
                    }
                }
                child = child->next_sibling;
            }
            block->form->option_count = option_count;
            int init_index = (selected_idx >= 0) ? selected_idx : (option_count > 0 ? 0 : -1);
            DocState* state = lycon && lycon->doc ? (DocState*)lycon->doc->state : nullptr;
            form_control_set_selected_index(state, (View*)block, init_index);
        }
        // Read CSS `appearance` property — affects intrinsic width and UA-rendered chrome.
        // `appearance: none` removes the native dropdown arrow so author CSS can supply its own.
        {
            CssDeclaration* ap_decl = dom_element_get_specified_value(block, CSS_PROPERTY_APPEARANCE);
            if (ap_decl && ap_decl->value && ap_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                ap_decl->value->data.keyword == CSS_VALUE_NONE) {
                block->form->appearance_none = 1;
            } else {
                block->form->appearance_none = 0;
            }
        }
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = RDT_DISPLAY_REPLACED;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        // Set intrinsic_width to UA default only on first resolve. Later passes
        // (e.g. intrinsic sizing in flex/grid) measure option text and write a
        // larger value here; we must not clobber it.
        if (block->form->intrinsic_width <= 0) {
            block->form->intrinsic_width = FormDefaults::SELECT_WIDTH;
        }
        // Content-area height (border-box minus default border). User CSS
        // padding/border get added by calc_select_size to produce the final
        // border-box height. Don't set given_height here — let
        // layout_form_control compute it after CSS cascade applies padding/border.
        block->form->intrinsic_height = FormDefaults::SELECT_HEIGHT - 2.0f;
        // Default border (UA): 1px solid #767676, 2px border-radius (Chrome-like)
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        apply_html_uniform_border_no_specificity(lycon, block, 1.0f, CSS_VALUE_SOLID,
            (Color){ .r=118, .g=118, .b=118, .a=255 });
        // Border-radius 2px on all four corners (Chrome UA)
        block->bound->border->radius.top_left = block->bound->border->radius.top_right =
            block->bound->border->radius.bottom_left = block->bound->border->radius.bottom_right = 2.0f;
        block->bound->border->radius.top_left_y = block->bound->border->radius.top_right_y =
            block->bound->border->radius.bottom_left_y = block->bound->border->radius.bottom_right_y = 2.0f;
        // UA background: white normally, light grey when disabled (Chrome ~rgb(235,235,228))
        DocState* state = block->doc ? block->doc->state : NULL;
        Color bg_color = form_control_is_disabled(state, (View*)block)
            ? (Color){ .r=235, .g=235, .b=228, .a=255 }
            : (Color){ .r=255, .g=255, .b=255, .a=255 };
        apply_html_background_color(lycon, block, bg_color);
        break;
    }
    case HTM_TAG_TEXTAREA: {
        // All values in CSS logical pixels
        // Guard: only allocate if not already allocated (avoid re-allocating on repeated style resolution)
        bool form_created = false;
        ensure_form_control_prop(lycon, block, FORM_CONTROL_TEXTAREA, &form_created);
        if (form_created) {
            block->form->name = block->get_attribute("name");
            block->form->placeholder = block->get_attribute("placeholder");
            DocState* state = (DocState*)lycon->doc->state;
            if (block->has_attribute("disabled")) {
                form_control_set_disabled(state, block, true);
            }
            if (block->has_attribute("readonly")) {
                form_control_set_readonly(state, block, true);
            }
            // Parse cols/rows
            const char* cols_attr = block->get_attribute("cols");
            const char* rows_attr = block->get_attribute("rows");
            if (cols_attr) block->form->cols = (int)str_to_int64_default(cols_attr, strlen(cols_attr), 0);
            if (rows_attr) block->form->rows = (int)str_to_int64_default(rows_attr, strlen(rows_attr), 0);
        }
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        apply_html_textarea_font(lycon, block);
        // Note: textarea uses content-box (CSS default), same as Chrome UA
        // Intrinsic size: Chrome default 182x36 border-box (20 cols, 2 rows)
        block->form->intrinsic_width = 182.0f;
        block->form->intrinsic_height = 36.0f;
        // Don't set given_width/given_height — layout_form_control computes
        // intrinsic size dynamically from cols/rows and font metrics
        // Default border and padding
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        apply_html_uniform_border_no_specificity(lycon, block, FormDefaults::TEXTAREA_BORDER, CSS_VALUE_SOLID,
            (Color){ .r=118, .g=118, .b=118, .a=255 });
        block->bound->padding.top = block->bound->padding.bottom =
            block->bound->padding.left = block->bound->padding.right = FormDefaults::TEXTAREA_PADDING;
        break;
    }
    case HTM_TAG_METER: {
        // Meter: inline-block replaced element, Chrome default 80x16
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = RDT_DISPLAY_REPLACED;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        block->blk->given_width = FormDefaults::METER_WIDTH;
        block->blk->given_height = FormDefaults::METER_HEIGHT;
        lycon->block.given_width = FormDefaults::METER_WIDTH;
        lycon->block.given_height = FormDefaults::METER_HEIGHT;
        break;
    }
    case HTM_TAG_PROGRESS: {
        // Progress: inline-block replaced element, Chrome default 160x16
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = RDT_DISPLAY_REPLACED;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        block->blk->given_width = FormDefaults::PROGRESS_WIDTH;
        block->blk->given_height = FormDefaults::PROGRESS_HEIGHT;
        lycon->block.given_width = FormDefaults::PROGRESS_WIDTH;
        lycon->block.given_height = FormDefaults::PROGRESS_HEIGHT;
        break;
    }
    case HTM_TAG_LABEL:
        // label is inline by default, no special styling
        break;
    case HTM_TAG_OPTION:
    case HTM_TAG_OPTGROUP: {
        // HTML spec: option/optgroup inside select/datalist are 0×0 (UA rendered).
        // Outside select/datalist, they render as normal block flow content.
        uintptr_t parent_tag = elmt->parent ? elmt->parent->tag() : 0;
        if (parent_tag == HTM_TAG_SELECT || parent_tag == HTM_TAG_DATALIST ||
            parent_tag == HTM_TAG_OPTGROUP) {
            block->display.outer = CSS_VALUE_BLOCK;
            block->display.inner = CSS_VALUE_FLOW;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            block->blk->given_width = 0;
            block->blk->given_height = 0;
        }
        break;
    }
    case HTM_TAG_DATALIST:
        // Datalist should be completely hidden (display:none)
        block->display.outer = CSS_VALUE_NONE;
        block->display.inner = CSS_VALUE_NONE;
        break;
    case HTM_TAG_DIALOG:
        // HTML §4.12.4: <dialog> without the 'open' attribute is not rendered.
        // Chrome UA: dialog:not([open]) { display: none; }
        if (!elmt->has_attribute("open")) {
            block->display.outer = CSS_VALUE_NONE;
            block->display.inner = CSS_VALUE_NONE;
        }
        break;
    // ========== Semantic/sectioning elements with no visual default ==========
    case HTM_TAG_ARTICLE:  case HTM_TAG_SECTION:  case HTM_TAG_NAV:
    case HTM_TAG_ASIDE:  case HTM_TAG_HEADER:  case HTM_TAG_FOOTER:
    case HTM_TAG_MAIN:  case HTM_TAG_HGROUP:  case HTM_TAG_DETAILS:
        // these are block-level but have no special default styling
        break;
    }

    // Handle HTML 'dir' attribute (global, applies to all elements)
    // CSS 2.1 §9.10: The 'dir' attribute maps to the CSS 'direction' property
    const char* dir_attr = elmt->get_attribute("dir");
    if (dir_attr) {
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        if (str_ieq_const(dir_attr, strlen(dir_attr), "rtl")) {
            block->blk->direction = CSS_VALUE_RTL;
            log_debug("[HTML] dir attribute: rtl");
        } else if (str_ieq_const(dir_attr, strlen(dir_attr), "ltr")) {
            block->blk->direction = CSS_VALUE_LTR;
            log_debug("[HTML] dir attribute: ltr");
        } else if (str_ieq_const(dir_attr, strlen(dir_attr), "auto")) {
            // HTML5 §14.3.4: dir="auto" — resolve direction from first strong character
            CssEnum resolved = resolve_dir_auto(lam::dom_require_element(elmt));
            block->blk->direction = resolved;
            log_debug("[HTML] dir attribute: auto -> %s",
                      resolved == CSS_VALUE_RTL ? "rtl" : "ltr");
        }
    }
}
