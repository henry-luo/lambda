#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_flex_multipass.hpp"
#include "layout_positioned.hpp"
#include "grid.hpp"

#include "../lib/log.h"

// Direct declaration of the actual C symbol (compiler will add underscore)
extern "C" int strview_to_int_c(StrView* s) asm("_strview_to_int");

// C++ wrapper function to call the C function directly
static int call_strview_to_int(StrView* s) {
    return strview_to_int_c(s);
}

View* layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
void layout_flex_nodes(LayoutContext* lycon, lxb_dom_node_t *first_child);
void resolve_inline_default(LayoutContext* lycon, ViewSpan* span);
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_table(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_flex_content(LayoutContext* lycon, ViewBlock* block);
void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, Blockbox *pa_block, Linebox *pa_line);

void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, PropValue display) {
    // finalize the block size
    float flow_width, flow_height;
    if (block->bound) {
        // max_width already includes padding.left and border.left
        block->content_width = lycon->block.max_width + block->bound->padding.right;
        // advance_y already includes padding.top and border.top
        block->content_height = lycon->block.advance_y + block->bound->padding.bottom;
        flow_width = block->content_width +
            (block->bound->border ? block->bound->border->width.right : 0);
        flow_height = block->content_height +
            (block->bound->border ? block->bound->border->width.bottom : 0);
    } else {
        flow_width = block->content_width = lycon->block.max_width;
        flow_height = block->content_height = lycon->block.advance_y;
    }

    if (display == LXB_CSS_VALUE_INLINE_BLOCK && lycon->block.given_width < 0) {
        block->width = min(flow_width, block->width);
        log_debug("inline-block final width set to: %f", block->width);
    }

    // handle horizontal overflow
    if (flow_width > block->width) { // hz overflow
        if (!block->scroller) {
            block->scroller = alloc_scroll_prop(lycon);
        }
        block->scroller->has_hz_overflow = true;
        if (block->scroller->overflow_x == LXB_CSS_VALUE_VISIBLE) {
            lycon->block.pa_block->max_width = max(lycon->block.pa_block->max_width, flow_width);
        }
        else if (block->scroller->overflow_x == LXB_CSS_VALUE_SCROLL ||
            block->scroller->overflow_x == LXB_CSS_VALUE_AUTO) {
            block->scroller->has_hz_scroll = true;
        }
        if (block->scroller->has_hz_scroll ||
            block->scroller->overflow_x == LXB_CSS_VALUE_CLIP ||
            block->scroller->overflow_x == LXB_CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
            block->scroller->clip.left = 0;  block->scroller->clip.top = 0;
            block->scroller->clip.right = block->width;  block->scroller->clip.bottom = block->height;
        }
    }

    // handle vertical overflow and determine block->height
    if (lycon->block.given_height >= 0) { // got specified height
        // no change to block->height
        if (flow_height > block->height) { // vt overflow
            if (!block->scroller) {
                block->scroller = alloc_scroll_prop(lycon);
            }
            block->scroller->has_vt_overflow = true;
            if (block->scroller->overflow_y == LXB_CSS_VALUE_VISIBLE) {
                lycon->block.pa_block->max_height = max(lycon->block.pa_block->max_height, block->y + flow_height);
            }
            else if (block->scroller->overflow_y == LXB_CSS_VALUE_SCROLL || block->scroller->overflow_y == LXB_CSS_VALUE_AUTO) {
                block->scroller->has_vt_scroll = true;
            }
            if (block->scroller->has_hz_scroll ||
                block->scroller->overflow_y == LXB_CSS_VALUE_CLIP ||
                block->scroller->overflow_y == LXB_CSS_VALUE_HIDDEN) {
                block->scroller->has_clip = true;
                block->scroller->clip.left = 0;  block->scroller->clip.top = 0;
                block->scroller->clip.right = block->width;  block->scroller->clip.bottom = block->height;
            }
        }
        log_debug("block: given_height: %f, height: %f, flow height: %f", lycon->block.given_height, block->height, flow_height);
    }
    else {
        log_debug("finalize block flow, set block height to flow height: %f", flow_height);
        block->height = flow_height;
    }
    log_debug("block wd:%f, hg:%f after finalize", block->width, block->height);
}

void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    Document* doc = NULL;
    if (!(block->embed && block->embed->doc)) {
        // load iframe document
        size_t value_len;
        const lxb_char_t *value = block->node->get_attribute("src", &value_len);
        if (value && value_len) {
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, (const char*)value, value_len);
            printf("iframe doc src: %s\n", src->str);
            doc = load_html_doc(lycon->ui_context->document->url, src->str);
            strbuf_free(src);
            if (!doc) {
                printf("Failed to load iframe document\n");
                // todo: use a placeholder
            } else {
                if (!(block->embed)) block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                block->embed->doc = doc; // assign loaded document to embed property
                if (doc->dom_tree) {
                    layout_html_doc(lycon->ui_context, doc, false);
                }
            }
        }
    }
    else {
        doc = block->embed->doc;
    }
    if (doc && doc->view_tree && doc->view_tree->root) {
        ViewBlock* root = (ViewBlock*)doc->view_tree->root;
        lycon->block.max_width = root->content_width;
        lycon->block.advance_y = root->content_height;
    }
    finalize_block_flow(lycon, block, display.outer);
}

void layout_block_inner_content(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    log_debug("layout block inner content");
    if (block->display.inner == RDT_DISPLAY_REPLACED) {  // image, iframe
        uintptr_t elmt_name = block->node->tag();
        if (elmt_name == LXB_TAG_IFRAME) {
            layout_iframe(lycon, block, display);
        }
        // else LXB_TAG_IMG
    } else {  // layout block child content
        DomNode *child = block->node->first_child();
        if (child) {
            lycon->parent = (ViewGroup*)block;  lycon->prev_view = NULL;
            if (display.inner == LXB_CSS_VALUE_FLOW) {
                // inline content flow
                do {
                    layout_flow_node(lycon, child);
                    DomNode* next_child = child->next_sibling();
                    child = next_child;
                } while (child);
                // handle last line
                if (!lycon->line.is_line_start) { line_break(lycon); }
            }
            else if (display.inner == LXB_CSS_VALUE_FLEX) {
                log_debug("Setting up flex container for %s", block->node->name());
                layout_flex_content(lycon, block);
                log_debug("Finished flex container layout for %s", block->node->name());
            }
            else if (display.inner == LXB_CSS_VALUE_GRID) {
                log_debug("Setting up grid container for %s", block->node->name());
                GridContainerLayout* pa_grid = lycon->grid_container;
                init_grid_container(lycon, block);
                // Process DOM children into View objects first
                // Grid containers need their DOM children converted to View objects
                // before the grid algorithm can work
                int child_count = 0;
                const int MAX_CHILDREN = 100; // Safety limit
                do {
                    log_debug("Processing grid child %p (count: %d)", child, child_count);
                    if (child_count >= MAX_CHILDREN) {
                        log_error("ERROR: Too many children, breaking to prevent infinite loop");
                        break;
                    }
                    layout_flow_node(lycon, child);
                    child = child->next_sibling();
                    child_count++;
                } while (child);

                // Now run the grid layout algorithm with the processed children
                log_debug("About to call layout_grid_container");
                layout_grid_container(lycon, block);

                cleanup_grid_container(lycon);
                lycon->grid_container = pa_grid;
                log_debug("Finished layout_grid_container");
            }
            else if (display.inner == LXB_CSS_VALUE_TABLE) {
                log_debug("TABLE LAYOUT TRIGGERED! outer=%d, inner=%d, element=%s",
                        display.outer, display.inner, block->node->name());
                layout_table(lycon, block->node, display);
                return;
            }
            else {
                log_debug("unknown display type");
            }
            lycon->parent = block->parent;
        }
        finalize_block_flow(lycon, block, display.outer);
    }
}

float adjust_min_max_width(ViewBlock* block, float width) {
    fprintf(stderr, "[ADJUST] adjust_min_max_width: input=%.2f, blk=%p\n", width, (void*)block->blk);
    if (block->blk) {
        fprintf(stderr, "[ADJUST] min=%.2f, max=%.2f\n", block->blk->given_min_width, block->blk->given_max_width);
        if (block->blk->given_max_width >= 0 && width > block->blk->given_max_width) {
            width = block->blk->given_max_width;
            fprintf(stderr, "[ADJUST] Clamped to max: %.2f\n", width);
        }
        // Note: given_min_width overrides given_max_width if both are specified
        if (block->blk->given_min_width >= 0 && width < block->blk->given_min_width) {
            width = block->blk->given_min_width;
            fprintf(stderr, "[ADJUST] Clamped to min: %.2f\n", width);
        }
    }
    fprintf(stderr, "[ADJUST] adjust_min_max_width: output=%.2f\n", width);
    return width;
}

float adjust_min_max_height(ViewBlock* block, float height) {
    if (block->blk) {
        if (block->blk->given_max_height >= 0 && height > block->blk->given_max_height) {
            height = block->blk->given_max_height;
        }
        // Note: given_min_height overrides given_max_height if both are specified
        if (block->blk->given_min_height >= 0 && height < block->blk->given_min_height) {
            height = block->blk->given_min_height;
        }
    }
    return height;
}

float adjust_border_padding_width(ViewBlock* block, float width) {
    // for border-box, the given width includes padding and borders
    // so we need to subtract them to get the content width
    float padding_and_border = 0;
    if (block->bound) {
        padding_and_border += block->bound->padding.left + block->bound->padding.right;
        if (block->bound->border) {
            padding_and_border += block->bound->border->width.left + block->bound->border->width.right;
        }
    }
    width = max(width - padding_and_border, 0);
    log_debug("box-sizing: border-box - padding+border=%f, content_width=%f, border_width=%f", padding_and_border, width,
        block->bound && block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
    return width;
}

float adjust_border_padding_height(ViewBlock* block, float height) {
    // for border-box, the given height includes padding and borders
    // so we need to subtract them to get the content height
    float padding_and_border = 0;
    if (block->bound) {
        padding_and_border += block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            padding_and_border += block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }
    height = max(height - padding_and_border, 0);
    log_debug("box-sizing: border-box - padding+border=%f, content_height=%f", padding_and_border, height);
    return height;
}

void apply_element_default_style(LayoutContext* lycon, DomNode* elmt, ViewBlock* block) {
    float em_size = 0;  size_t value_len;  const lxb_char_t *value;
    resolve_inline_default(lycon, (ViewSpan*)block);
    uintptr_t elmt_name = elmt->tag();
    switch (elmt_name) {
    case LXB_TAG_BODY: {
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // margin: 8px
        block->bound->margin.top = block->bound->margin.right =
            block->bound->margin.bottom = block->bound->margin.left = 8 * lycon->ui_context->pixel_ratio;
        block->bound->margin.top_specificity = block->bound->margin.right_specificity =
            block->bound->margin.bottom_specificity = block->bound->margin.left_specificity = -1;
        break;
    }
    case LXB_TAG_H1:
        em_size = 2;  // 2em
        goto HEADING_PROP;
    case LXB_TAG_H2:
        em_size = 1.5;  // 1.5em
        goto HEADING_PROP;
    case LXB_TAG_H3:
        em_size = 1.17;  // 1.17em
        goto HEADING_PROP;
    case LXB_TAG_H4:
        em_size = 1;  // 1em
        goto HEADING_PROP;
    case LXB_TAG_H5:
        em_size = 0.83;  // 0.83em
        goto HEADING_PROP;
    case LXB_TAG_H6:
        em_size = 0.67;  // 0.67em
        HEADING_PROP:
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->font_size = lycon->font.style->font_size * em_size;
        block->font->font_weight = LXB_CSS_VALUE_BOLD;
        break;
    case LXB_TAG_P:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // margin: 1em 0;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        break;
    case LXB_TAG_UL:  case LXB_TAG_OL:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->list_style_type = elmt_name == LXB_TAG_UL ? LXB_CSS_VALUE_DISC : LXB_CSS_VALUE_DECIMAL;
        if (!block->bound) {
            block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        // margin: 1em 0; padding: 0 0 0 40px;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        block->bound->padding.left = 40 * lycon->ui_context->pixel_ratio;
        block->bound->padding.left_specificity = -1;
        break;
    case LXB_TAG_CENTER:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = LXB_CSS_VALUE_CENTER;
        break;
    case LXB_TAG_IMG:  // get html width and height (before the css styles)
        value = elmt->get_attribute("width", &value_len);
        if (value) {
            StrView width_view = strview_init(value, value_len);
            float width = call_strview_to_int(&width_view);
            if (width >= 0) lycon->block.given_width = width * lycon->ui_context->pixel_ratio;
            // else width attr ignored
        }
        value = elmt->get_attribute("height", &value_len);
        if (value) {
            StrView height_view = strview_init(value, value_len);
            float height = call_strview_to_int(&height_view);
            if (height >= 0) lycon->block.given_height = height * lycon->ui_context->pixel_ratio;
            // else height attr ignored
        }
        break;
    case LXB_TAG_IFRAME:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // todo: inset border style
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left = 1 * lycon->ui_context->pixel_ratio;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        if (!block->scroller) { block->scroller = alloc_scroll_prop(lycon); }
        block->scroller->overflow_x = LXB_CSS_VALUE_AUTO;
        block->scroller->overflow_y = LXB_CSS_VALUE_AUTO;
        // default iframe size to 300 x 200
        lycon->block.given_width = 300 * lycon->ui_context->pixel_ratio;
        lycon->block.given_height = 200 * lycon->ui_context->pixel_ratio;
        break;
    case LXB_TAG_HR:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // 1px top border
        block->bound->border->width.top = 1 * lycon->ui_context->pixel_ratio;
        block->bound->border->width.left = block->bound->border->width.right = block->bound->border->width.bottom = 0;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        // 0.5em margin
        block->bound->margin.top = block->bound->margin.bottom =
            block->bound->margin.left = block->bound->margin.right = 0.5 * lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    }
}

void setup_inline(LayoutContext* lycon, ViewBlock* block) {
    // setup inline context
    float content_width = lycon->block.content_width;
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    line_init(lycon, 0, content_width);
    if (block->bound) {
        if (block->bound->border) {
            lycon->line.advance_x += block->bound->border->width.left;
            lycon->block.advance_y += block->bound->border->width.top;
            lycon->line.right -= block->bound->border->width.right;
        }
        lycon->line.advance_x += block->bound->padding.left;
        lycon->block.advance_y += block->bound->padding.top;
        lycon->line.left = lycon->line.advance_x;
        lycon->line.right = lycon->line.left + content_width;
    }

    if (block->blk) lycon->block.text_align = block->blk->text_align;
    // setup font
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, block->font);
    }
    // setup line height
    if (!block->blk || !block->blk->line_height || block->blk->line_height->type== LXB_CSS_VALUE_INHERIT) {  // inherit from parent
        lycon->block.line_height = inherit_line_height(lycon, block);
    } else {
        lycon->block.line_height = calc_line_height(&lycon->font, block->blk->line_height);
    }
    // setup initial ascender and descender
    lycon->block.init_ascender = lycon->font.ft_face->size->metrics.ascender / 64.0;
    lycon->block.init_descender = (-lycon->font.ft_face->size->metrics.descender) / 64.0;
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height - (lycon->block.init_ascender + lycon->block.init_descender)) / 2);
    log_debug("block line_height: %f, font height: %f, asc+desc: %f, lead_y: %f", lycon->block.line_height, lycon->font.ft_face->size->metrics.height / 64.0,
        lycon->block.init_ascender + lycon->block.init_descender, lycon->block.lead_y);
}

void layout_block_content(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, Blockbox *pa_block, Linebox *pa_line) {
    block->x = pa_line->left;  block->y = pa_block->advance_y;
    const char* tag = elmt->name();
    log_debug("block init position (%s): x=%f, y=%f, pa_block.advance_y=%f", tag, block->x, block->y, pa_block->advance_y);

    uintptr_t elmt_name = elmt->tag();
    if (elmt_name == LXB_TAG_IMG) { // load image intrinsic width and height
        size_t value_len;  const lxb_char_t *value;
        value = elmt->get_attribute("src", &value_len);
        if (value && value_len) {
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, (const char*)value, value_len);
            log_debug("image src: %s", src->str);
            if (!block->embed) {
                block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
            }
            block->embed->img = load_image(lycon->ui_context, src->str);
            strbuf_free(src);
            if (!block->embed->img) {
                log_debug("Failed to load image");
                // todo: use a placeholder
            }
        }
        if (block->embed->img) {
            ImageSurface* img = block->embed->img;
            if (lycon->block.given_width < 0 || lycon->block.given_height < 0) {
                // scale image by pixel ratio
                float w = img->width * lycon->ui_context->pixel_ratio;
                float h = img->height * lycon->ui_context->pixel_ratio;
                log_debug("image intrinsic dims: %f x %f, given: %f x %f", w, h,
                    lycon->block.given_width, lycon->block.given_height);
                if (lycon->block.given_width >= 0) { // scale unspecified height
                    lycon->block.given_height = lycon->block.given_width * h / w;
                }
                else if (lycon->block.given_height >= 0) { // scale unspecified width
                    lycon->block.given_width = lycon->block.given_height * w / h;
                }
                else { // both width and height unspecified
                    if (img->format == IMAGE_FORMAT_SVG) {
                        // scale to parent block width
                        lycon->block.given_width = lycon->block.pa_block->content_width;
                        lycon->block.given_height = lycon->block.given_width * h / w;
                    }
                    else { // use image intrinsic dimensions
                        lycon->block.given_width = w;  lycon->block.given_height = h;
                    }
                }
            }
            // else both width and height specified
            if (img->format == IMAGE_FORMAT_SVG) {
                img->max_render_width = max(lycon->block.given_width, img->max_render_width);
            }
            log_debug("image dimensions: %f x %f", lycon->block.given_width, lycon->block.given_height);
        }
        else { // failed to load image
            lycon->block.given_width = 40;  lycon->block.given_height = 30;
            // todo: use a placeholder
        }
    }

    // determine block width and height
    float content_width = -1;
    log_debug("Block '%s': given_width=%.2f,  given_height=%.2f, blk=%p, width_type=%d",
        elmt->name(), lycon->block.given_width, lycon->block.given_height, (void*)block->blk,
        block->blk ? block->blk->given_width_type : -1);
    bool cond1 = (lycon->block.given_width >= 0);
    bool cond2 = (!block->blk || block->blk->given_width_type != LXB_CSS_VALUE_AUTO);
    if (lycon->block.given_width >= 0 && (!block->blk || block->blk->given_width_type != LXB_CSS_VALUE_AUTO)) {
        content_width = max(lycon->block.given_width, 0);
        log_debug("Using given_width: content_width=%.2f", content_width);
        content_width = adjust_min_max_width(block, content_width);
        log_debug("After adjust_min_max_width: content_width=%.2f", content_width);
        if (block->blk && block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
            log_debug("After adjust_border_padding (border-box): content_width=%.2f", content_width);
        }
    }
    else { // derive from parent block width
        log_debug("Deriving from parent: pa_block.content_width=%.2f", pa_block->content_width);
        if (block->bound) {
            content_width = pa_block->content_width
                - (block->bound->margin.left_type == LXB_CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                - (block->bound->margin.right_type == LXB_CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        else { content_width = pa_block->content_width; }
        if (block->blk && block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            content_width = adjust_min_max_width(block, content_width);
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        } else {
            content_width = adjust_border_padding_width(block, content_width);
            if (block->bound) content_width = adjust_min_max_width(block, content_width);
        }
    }
    assert(content_width >= 0);
    log_debug("content_width=%f, given_width=%f, max_width=%f", content_width, lycon->block.given_width,
        block->blk && block->blk->given_max_width >= 0 ? block->blk->given_max_width : -1);

    float content_height = -1;
    if (lycon->block.given_height >= 0) {
        content_height = max(lycon->block.given_height, 0);
        content_height = adjust_min_max_height(block, content_height);
        if (block->blk && block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_height = adjust_border_padding_height(block, content_height);
        }
    }
    else { // derive from parent block height
        if (block->bound) {
            content_height = pa_block->content_height - block->bound->margin.top - block->bound->margin.bottom;
        }
        else { content_height = pa_block->content_height; }
        if (block->blk && block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            content_height = adjust_min_max_height(block, content_height);
            if (block->bound) content_height = adjust_border_padding_height(block, content_height);
        } else {
            content_height = adjust_border_padding_height(block, content_height);
            if (block->bound) content_height = adjust_min_max_height(block, content_height);
        }
    }
    assert(content_height >= 0);
    log_debug("content_height=%f, given_height=%f, max_height=%f", content_height, lycon->block.given_height,
        block->blk && block->blk->given_max_height >= 0 ? block->blk->given_max_height : -1);
    lycon->block.content_width = content_width;  lycon->block.content_height = content_height;

    if (block->bound) {
        block->width = content_width + block->bound->padding.left + block->bound->padding.right +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
        block->height = content_height + block->bound->padding.top + block->bound->padding.bottom +
            (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
        // todo: we should keep LENGTH_AUTO (may be in flags) for reflow
        log_debug("block margins: left=%f, right=%f, left_type=%d, right_type=%d",
            block->bound->margin.left, block->bound->margin.right, block->bound->margin.left_type, block->bound->margin.right_type);
        if (block->bound->margin.left_type == LXB_CSS_VALUE_AUTO && block->bound->margin.right_type == LXB_CSS_VALUE_AUTO)  {
            block->bound->margin.left = block->bound->margin.right = max((pa_block->content_width - block->width) / 2, 0);
        } else {
            if (block->bound->margin.left_type == LXB_CSS_VALUE_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right_type == LXB_CSS_VALUE_AUTO) block->bound->margin.right = 0;
        }
        log_debug("finalize block margins: left=%f, right=%f", block->bound->margin.left, block->bound->margin.right);
        float y_before_margin = block->y;
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;
        log_debug("Y coordinate: before margin=%f, margin.top=%f, after margin=%f (tag=%s)",
                  y_before_margin, block->bound->margin.top, block->y, tag);
    }
    else {
        block->width = content_width;  block->height = content_height;
        // no change to block->x, block->y
    }
    log_debug("layout-block-sizes: x:%f, y:%f, wd:%f, hg:%f, line-hg:%f, given-w:%f, given-h:%f",
        block->x, block->y, block->width, block->height, lycon->block.line_height, lycon->block.given_width, lycon->block.given_height);

    // setup inline context
    setup_inline(lycon, block);

    // layout block content, and determine flow width and height
    layout_block_inner_content(lycon, block, block->display);

    // check for margin collapsing with children
    if (block->bound) {
        // collapse bottom margin with last child block
        if ((!block->bound->border || block->bound->border->width.bottom == 0) &&
            block->bound->padding.bottom == 0 && block->child) {
            View* last_child = block->child;
            while (last_child && last_child->next) { last_child = last_child->next; }
            if (last_child->is_block() && ((ViewBlock*)last_child)->bound) {
                ViewBlock* last_child_block = (ViewBlock*)last_child;
                if (last_child_block->bound->margin.bottom > 0) {
                    float margin_bottom = max(block->bound->margin.bottom, last_child_block->bound->margin.bottom);
                    block->height -= last_child_block->bound->margin.bottom;
                    block->bound->margin.bottom = margin_bottom;
                    last_child_block->bound->margin.bottom = 0;
                    log_debug("collapsed bottom margin %f between block and last child", margin_bottom);
                }
            }
        }
    }

    // apply CSS positioning after normal layout
    if (block->position) {
        log_debug("Found position property: type=%d (RELATIVE=334, ABSOLUTE=335, FIXED=337)", block->position->position);
        log_debug("Position offsets: top=%.2f(%s), right=%.2f(%s), bottom=%.2f(%s), left=%.2f(%s)",
            block->position->top, block->position->has_top ? "set" : "unset",
            block->position->right, block->position->has_right ? "set" : "unset",
            block->position->bottom, block->position->has_bottom ? "set" : "unset",
            block->position->left, block->position->has_left ? "set" : "unset");

        if (block->position->position == LXB_CSS_VALUE_RELATIVE) {
            log_debug("Applying relative positioning");
            layout_relative_positioned(lycon, block);
        }
    } else {
        log_debug("No position property found for element %s", elmt->name());
    }

    // apply CSS float layout after positioning
    if (block->position && element_has_float(block)) {
        log_debug("Element has float property, applying float layout");
        layout_float_element(lycon, block);
    }

    // apply CSS clear property after float layout
    if (block->position && block->position->clear != LXB_CSS_VALUE_NONE) {
        log_debug("Element has clear property, applying clear layout");
        layout_clear_element(lycon, block);
    }
}

void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    log_enter();
    // display: LXB_CSS_VALUE_BLOCK, LXB_CSS_VALUE_INLINE_BLOCK, LXB_CSS_VALUE_LIST_ITEM
    log_debug("layout block %s (display.inner=%d)", elmt->name(), display.inner);

    // Check if this block is a flex item
    ViewBlock* parent_block = (ViewBlock*)lycon->parent;
    bool is_flex_item = (parent_block && parent_block->display.inner == LXB_CSS_VALUE_FLEX);

    if (display.outer != LXB_CSS_VALUE_INLINE_BLOCK) {
        if (!lycon->line.is_line_start) { line_break(lycon); }
    }
    // save parent context
    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // -1 as unresolved
    lycon->block.pa_block = &pa_block;  lycon->elmt = elmt;
    log_debug("saved pa_block.advance_y: %.2f for element %s", pa_block.advance_y, elmt->name());
    lycon->block.content_width = lycon->block.content_height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;

    uintptr_t elmt_name = elmt->tag();
    ViewBlock* block = (ViewBlock*)alloc_view(lycon,
        display.outer == LXB_CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == LXB_CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        display.inner == LXB_CSS_VALUE_TABLE ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK,
        elmt);
    block->display = display;

    // handle element default styles
    apply_element_default_style(lycon, elmt, block);

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    if (block->position && (block->position->position == LXB_CSS_VALUE_ABSOLUTE || block->position->position == LXB_CSS_VALUE_FIXED)) {
        layout_abs_block(lycon, elmt, block, &pa_block, &pa_line);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
        lycon->prev_view = (View*)block;
    } else {
        // layout block content to determine content width and height
        layout_block_content(lycon, elmt, block, &pa_block, &pa_line);

        log_debug("flow block in parent context, block->y before restoration: %.2f", block->y);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;

        // flow the block in parent context
        if (display.outer == LXB_CSS_VALUE_INLINE_BLOCK) {
            if (!lycon->line.start_view) lycon->line.start_view = (View*)block;
            if (lycon->line.advance_x + block->width > lycon->line.right) {
                line_break(lycon);
                block->x = lycon->line.left;
            } else {
                block->x = lycon->line.advance_x;
            }
            if (block->in_line && block->in_line->vertical_align) {
                float item_height = block->height + (block->bound ?
                    block->bound->margin.top + block->bound->margin.bottom : 0);
                float item_baseline = block->height + (block->bound ? block->bound->margin.top: 0);
                float line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
                float offset = calculate_vertical_align_offset(
                    lycon, block->in_line->vertical_align, item_height, line_height,
                    lycon->line.max_ascender, item_baseline);
                block->y = lycon->block.advance_y + offset;  // block->bound->margin.top will be added below
                log_debug("valigned-inline-block: offset %f, line %f, block %f, adv: %f, y: %f, va:%d",
                    offset, line_height, block->height, lycon->block.advance_y, block->y, block->in_line->vertical_align);
                lycon->line.max_descender = max(lycon->line.max_descender, offset + item_height - lycon->line.max_ascender);
                log_debug("new max_descender=%f", lycon->line.max_descender);
            } else {
                log_debug("valigned-inline-block: default baseline align");
                block->y = lycon->block.advance_y;
            }
            lycon->line.advance_x += block->width;
            if (block->bound) {
                block->x += block->bound->margin.left;
                block->y += block->bound->margin.top;
                lycon->line.advance_x += block->bound->margin.left + block->bound->margin.right;
            }
            log_debug("inline-block in line: x: %d, y: %d, adv-x: %d, mg-left: %d, mg-top: %d",
                block->x, block->y, lycon->line.advance_x, block->bound ? block->bound->margin.left : 0, block->bound ? block->bound->margin.top : 0);
            // update baseline
            if (block->in_line && block->in_line->vertical_align != LXB_CSS_VALUE_BASELINE) {
                float block_flow_height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
                if (block->in_line->vertical_align == LXB_CSS_VALUE_TEXT_TOP) {
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->block.init_ascender);
                }
                else if (block->in_line->vertical_align == LXB_CSS_VALUE_TEXT_BOTTOM) {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block_flow_height - lycon->block.init_descender);
                }
                else {
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->line.max_ascender);
                }
            } else {
                // default baseline alignment for inline block
                if (block->bound) {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block->height + block->bound->margin.top);
                    // bottom margin is placed below the baseline as descender
                    lycon->line.max_descender = max(lycon->line.max_descender, block->bound->margin.bottom);
                }
                else {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block->height);
                }
                log_debug("inline-block set max_ascender to: %d", lycon->line.max_ascender);
            }
            // line got content
            lycon->line.reset_space();
        }
        else { // normal block
            if (block->bound) {
                // collapse top margin with parent block
                log_debug("check margin collapsing");
                if (block->parent->child == block) {  // first child
                    if (block->bound->margin.top > 0) {
                        ViewBlock* parent = block->parent->is_block() ? (ViewBlock*)block->parent : NULL;
                        // parent has top margin, but no border, no padding;  parent->parent to exclude html
                        if (parent && parent->parent && parent->bound && parent->bound->padding.top == 0 &&
                            (!parent->bound->border || parent->bound->border->width.top == 0)) {
                            float margin_top = max(block->bound->margin.top, parent->bound->margin.top);
                            parent->y += margin_top - parent->bound->margin.top;
                            parent->bound->margin.top = margin_top;
                            block->y = 0;  block->bound->margin.top = 0;
                            log_debug("collapsed margin between block and first child: %f, parent y: %f, block y: %f", margin_top, parent->y, block->y);
                        }
                        else {
                            log_debug("no parent margin collapsing: parent->bound=%p, border-top=%f, padding-top=%f",
                                parent ? parent->bound : NULL,
                                parent && parent->bound && parent->bound->border ? parent->bound->border->width.top : 0,
                                parent && parent->bound ? parent->bound->padding.top : 0);
                        }
                    }
                }
                else {
                    // check sibling margin collapsing
                    float collapse = 0;
                    View* prev_sibling = block->previous_view();
                    if (prev_sibling && prev_sibling->is_block() && ((ViewBlock*)prev_sibling)->bound) {
                        ViewBlock* prev_block = (ViewBlock*)prev_sibling;
                        if (prev_block->bound->margin.bottom > 0 && block->bound->margin.top > 0) {
                            collapse = min(prev_block->bound->margin.bottom, block->bound->margin.top);
                            block->y -= collapse;
                            block->bound->margin.top -= collapse;
                            log_debug("collapsed margin between sibling blocks: %f, block->y now: %f", collapse, block->y);
                        }
                    }
                }
                lycon->block.advance_y += block->height + block->bound->margin.top + block->bound->margin.bottom;
                lycon->block.max_width = max(lycon->block.max_width, block->width
                    + block->bound->margin.left + block->bound->margin.right);
            } else {
                lycon->block.advance_y += block->height;
                lycon->block.max_width = max(lycon->block.max_width, block->width);
            }
            assert(lycon->line.is_line_start);
            log_debug("block end, pa max_width: %f, pa advance_y: %f, block hg: %f",
                lycon->block.max_width, lycon->block.advance_y, block->height);
        }
        lycon->prev_view = (View*)block;
    }
    log_leave();
}
