#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_content.hpp"
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

void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, PropValue display) {
    // finalize the block size
    int flow_width, flow_height;
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
    }
    // handle horizontal overflow
    if (flow_width > block->width) { // hz overflow
        if (!block->scroller) {
            block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
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
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
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
    }
    else {
        block->height = flow_height;
    }
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

void layout_block_content(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    log_debug("layout block content");

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
                    // printf("Processing child %p\n", child);
                    layout_flow_node(lycon, child);
                    DomNode* next_child = child->next_sibling();
                    child = next_child;
                } while (child);
                // handle last line
                if (!lycon->line.is_line_start) { line_break(lycon); }
            }
            else if (display.inner == LXB_CSS_VALUE_FLEX) {
                // Enhanced multi-pass flex layout
                FlexContainerLayout* pa_flex = lycon->flex_container;
                init_flex_container(lycon, block);

                // PASS 1: Content measurement
                log_debug("FLEX MULTIPASS: Starting content measurement");
                int child_count = 0;
                const int MAX_CHILDREN = 100; // Safety limit
                DomNode* measure_child = child;
                do {
                    log_debug("Measuring flex child %p (count: %d)", measure_child, child_count);
                    if (child_count >= MAX_CHILDREN) {
                        log_error("ERROR: Too many flex children, breaking to prevent infinite loop");
                        break;
                    }
                    measure_flex_child_content(lycon, measure_child);
                    measure_child = measure_child->next_sibling();
                    child_count++;
                } while (measure_child);

                // PASS 2: Create View objects with measured sizes
                log_debug("FLEX MULTIPASS: Creating View objects with measured sizes");
                child_count = 0;
                do {
                    log_debug("Processing flex child %p (count: %d)", child, child_count);
                    if (child_count >= MAX_CHILDREN) {
                        log_error("ERROR: Too many flex children, breaking to prevent infinite loop");
                        break;
                    }
                    layout_flow_node_for_flex(lycon, child);
                    DomNode* next_child = child->next_sibling();
                    log_debug("Got next flex sibling %p", next_child);
                    child = next_child;
                    child_count++;
                } while (child);

                // PASS 3: Run enhanced flex algorithm with nested content support
                log_debug("FLEX MULTIPASS: Running enhanced flex algorithm");
                layout_flex_container_with_nested_content(lycon, block);

                // restore parent flex context
                cleanup_flex_container(lycon);
                lycon->flex_container = pa_flex;
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
                log_debug("Table detected inner=%d", display.outer, display.inner);
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
    lycon->block.width = lycon->block.height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;
    // lycon->block.line_height // inherit from parent context
    log_debug("layout block line_height: %d", lycon->block.line_height);

    uintptr_t elmt_name = elmt->tag();
    ViewBlock* block = (ViewBlock*)alloc_view(lycon,
        display.outer == LXB_CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == LXB_CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        display.inner == LXB_CSS_VALUE_TABLE ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK,
        elmt);
    block->display = display;

    // handle element default styles
    float em_size = 0;  size_t value_len;  const lxb_char_t *value;
    resolve_inline_default(lycon, (ViewSpan*)block);
    switch (elmt_name) {
    case LXB_TAG_BODY: {
        // Default body margin will be applied after CSS resolution
        // to respect CSS resets and specificity cascading
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
        block->font->font_size = lycon->font.face.style.font_size * em_size;
        block->font->font_weight = LXB_CSS_VALUE_BOLD;
        break;
    case LXB_TAG_P:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // CRITICAL FIX: Don't set default margins yet - wait until after CSS resolution
        // This will be handled in the post-CSS resolution logic
        break;
    case LXB_TAG_UL:  case LXB_TAG_OL:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->list_style_type = elmt_name == LXB_TAG_UL ?
            LXB_CSS_VALUE_DISC : LXB_CSS_VALUE_DECIMAL;
        if (!block->bound) {
            block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        // margin: 1em 0; padding: 0 0 0 40px;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.face.style.font_size;
        block->bound->padding.left = 40 * lycon->ui_context->pixel_ratio;
        break;
    case LXB_TAG_CENTER:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = LXB_CSS_VALUE_CENTER;
        break;
    case LXB_TAG_IMG:  // get html width and height (before the css styles)
        value = elmt->get_attribute("width", &value_len);
        if (value) {
            StrView width_view = strview_init(value, value_len);
            int width = call_strview_to_int(&width_view);
            if (width >= 0) lycon->block.given_width = width * lycon->ui_context->pixel_ratio;
            // else width attr ignored
        }
        value = elmt->get_attribute("height", &value_len);
        if (value) {
            StrView height_view = strview_init(value, value_len);
            int height = call_strview_to_int(&height_view);
            if (height >= 0) lycon->block.given_height = height * lycon->ui_context->pixel_ratio;
            // else height attr ignored
        }
        break;
    case LXB_TAG_IFRAME:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // todo: inset border style
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left =
            1 * lycon->ui_context->pixel_ratio;
        if (!block->scroller) { block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp)); }
        block->scroller->overflow_x = LXB_CSS_VALUE_AUTO;
        block->scroller->overflow_y = LXB_CSS_VALUE_AUTO;
        // default iframe size to 300 x 200
        lycon->block.given_width = 300 * lycon->ui_context->pixel_ratio;
        lycon->block.given_height = 200 * lycon->ui_context->pixel_ratio;
        break;
    }

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    // CRITICAL FIX: After CSS resolution, update font face if font size changed
    // This ensures the font face matches the resolved font size
    if (lycon->font.current_font_size > 0 && lycon->font.current_font_size != lycon->font.face.style.font_size) {
        // Font size was changed by CSS, need to reload font face
        lycon->font.face.style.font_size = lycon->font.current_font_size;
        lycon->font.face.ft_face = load_styled_font(lycon->ui_context, lycon->font.face.ft_face->family_name, &lycon->font.face.style);
        log_debug("Updated font face for new font size: %d", lycon->font.face.style.font_size);
    }

    // CRITICAL FIX: After CSS resolution, handle body margin properly
    // This handles CSS resets like "* { margin: 0; }" properly
    if (elmt_name == LXB_TAG_BODY) {
        int body_margin_physical = (int)(8.0 * lycon->ui_context->pixel_ratio);

        if (!block->bound) {
            // No CSS margin properties - apply default body margin
            block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            block->bound->margin.top = block->bound->margin.bottom =
                block->bound->margin.left = block->bound->margin.right = body_margin_physical;
            log_debug("DEBUG: Applied default body margin (no CSS): %d", body_margin_physical);
        } else {
            // CSS margin properties exist - check if it's a reset (all margins are 0)
            if (block->bound->margin.top == 0 && block->bound->margin.bottom == 0 &&
                block->bound->margin.left == 0 && block->bound->margin.right == 0) {
                // CSS reset detected - keep margins at 0
                log_debug("DEBUG: CSS margin reset detected - keeping margins at 0");
            } else {
                // CSS has some non-zero margins - check for unset margins and apply defaults
                if (block->bound->margin.top_specificity == 0) {
                    block->bound->margin.top = body_margin_physical;
                }
                if (block->bound->margin.bottom_specificity == 0) {
                    block->bound->margin.bottom = body_margin_physical;
                }
                if (block->bound->margin.left_specificity == 0) {
                    block->bound->margin.left = body_margin_physical;
                }
                if (block->bound->margin.right_specificity == 0) {
                    block->bound->margin.right = body_margin_physical;
                }
                log_debug("DEBUG: Applied partial default body margins");
            }
        }
    }

    // CRITICAL FIX: After CSS resolution, handle paragraph margins properly
    // Apply default paragraph margins only if not explicitly set by CSS
    if (elmt_name == LXB_TAG_P) {
        if (!block->bound) {
            // No CSS margin properties - apply default paragraph margins
            block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            block->bound->margin.top = block->bound->margin.bottom = lycon->font.face.style.font_size;
            log_debug("Applied default paragraph margins (no CSS): %d", lycon->font.face.style.font_size);
        } else {
            // CSS margin properties exist - check if it's a reset (all margins are 0)
            if (block->bound->margin.top == 0 && block->bound->margin.bottom == 0 &&
                block->bound->margin.left == 0 && block->bound->margin.right == 0) {
                // CSS reset detected - keep margins at 0
                log_debug("CSS paragraph margin reset detected - keeping margins at 0");
            } else {
                // CSS has some non-zero margins - check for unset margins and apply defaults
                if (block->bound->margin.top_specificity == 0) {
                    block->bound->margin.top = lycon->font.face.style.font_size;
                }
                if (block->bound->margin.bottom_specificity == 0) {
                    block->bound->margin.bottom = lycon->font.face.style.font_size;
                }
                log_debug("Applied partial default paragraph margins");
            }
        }
    }

    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    if (block->blk) lycon->block.text_align = block->blk->text_align;
    lycon->line.left = 0;  lycon->line.right = pa_block.width;
    lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(lycon);
    block->x = pa_line.left;  block->y = pa_block.advance_y;

    if (elmt_name == LXB_TAG_IMG) { // load image intrinsic width and height
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
                int w = img->width * lycon->ui_context->pixel_ratio;
                int h = img->height * lycon->ui_context->pixel_ratio;
                log_debug("image dims: intrinsic - %d x %d, spec - %d x %d", w, h,
                    lycon->block.given_width, lycon->block.given_height);
                if (lycon->block.given_width >= 0) { // scale unspecified height
                    lycon->block.given_height = lycon->block.given_width * h / w;
                }
                if (lycon->block.given_height >= 0) { // scale unspecified width
                    lycon->block.given_width = lycon->block.given_height * w / h;
                }
                else { // both width and height unspecified
                    if (img->format == IMAGE_FORMAT_SVG) {
                        // scale to parent block width
                        lycon->block.given_width = lycon->block.pa_block->width;
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
            log_debug("image dimensions: %d x %d", lycon->block.given_width, lycon->block.given_height);
        }
        else { // failed to load image
            lycon->block.given_width = 40;  lycon->block.given_height = 30;
            // todo: use a placeholder
        }
    }

    log_debug("setting up block blk");
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, pa_font.face.ft_face->family_name, block->font);
    }
    lycon->block.init_ascender = lycon->font.face.ft_face->size->metrics.ascender >> 6;
    lycon->block.init_descender = (-lycon->font.face.ft_face->size->metrics.descender) >> 6;

    // determine block width and height
    int content_width = 0;
    if (lycon->block.given_width >= 0) {
        content_width = lycon->block.given_width;

        // Apply box-sizing calculation
        if (block->blk && block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            // For border-box, the given width includes padding and borders
            // So we need to subtract them to get the content width
            int padding_and_border = 0;
            if (block->bound) {
                padding_and_border += block->bound->padding.left + block->bound->padding.right;
                if (block->bound->border) {
                    padding_and_border += block->bound->border->width.left + block->bound->border->width.right;
                }
            }
            content_width = max(content_width - padding_and_border, 0);
            log_debug("box-sizing: border-box - given_width=%d, padding+border=%d, content_width=%d",
                   lycon->block.given_width, padding_and_border, content_width);
        } else {
            log_debug("box-sizing: content-box - given_width=%d, content_width=%d",
                   lycon->block.given_width, content_width);
        }
    }
    else {
        if (!block->bound) {
            content_width = pa_block.width;
        } else {
            content_width = pa_block.width
                - (block->bound->margin.left == LENGTH_AUTO ? 0 : block->bound->margin.left)
                - (block->bound->margin.right == LENGTH_AUTO ? 0 : block->bound->margin.right)
                - (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0)
                - (block->bound->padding.left + block->bound->padding.right);
        }
    }
    int content_height = 0;
    if (lycon->block.given_height >= 0) {
        content_height = lycon->block.given_height;

        // Apply box-sizing calculation for height
        if (block->blk && block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            // For border-box, the given height includes padding and borders
            // So we need to subtract them to get the content height
            int padding_and_border = 0;
            if (block->bound) {
                padding_and_border += block->bound->padding.top + block->bound->padding.bottom;
                if (block->bound->border) {
                    padding_and_border += block->bound->border->width.top + block->bound->border->width.bottom;
                }
            }
            content_height = max(content_height - padding_and_border, 0);
            log_debug("box-sizing: border-box - given_height=%d, padding+border=%d, content_height=%d",
                   lycon->block.given_height, padding_and_border, content_height);
        } else {
            log_debug("box-sizing: content-box - given_height=%d, content_height=%d",
                   lycon->block.given_height, content_height);
        }
    }
    else {
        if (!block->bound) {
            content_height = pa_block.height;
        } else {
            content_height = pa_block.height
                - (block->bound->margin.top + block->bound->margin.bottom)
                - (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0)
                - (block->bound->padding.top + block->bound->padding.bottom);
        }
    }
    if (block->blk) {
        if (block->blk->max_width >= 0) {
            content_width = min(content_width, block->blk->max_width);
        }
        if (block->blk->min_width >= 0) {
            content_width = max(content_width, block->blk->min_width);
        }
        if (block->blk->max_height >= 0) {
            content_height = min(content_height, block->blk->max_height);
        }
        if (block->blk->min_height >= 0) {
            content_height = max(content_height, block->blk->min_height);
        }
    }
    content_width = max(content_width, 0);  content_height = max(content_height, 0);
    lycon->block.width = content_width;  lycon->block.height = content_height;

    if (block->bound) {
        block->width = content_width + block->bound->padding.left + block->bound->padding.right +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
        block->height = content_height + block->bound->padding.top + block->bound->padding.bottom +
            (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
        // todo: we should keep LENGTH_AUTO (may be in flags) for reflow
        if (block->bound->margin.left == LENGTH_AUTO && block->bound->margin.right == LENGTH_AUTO)  {
            block->bound->margin.left = block->bound->margin.right = max((pa_block.width - block->width) / 2, 0);
        } else {
            if (block->bound->margin.left == LENGTH_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right == LENGTH_AUTO) block->bound->margin.right = 0;
        }
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;
        if (block->bound->border) {
            lycon->line.advance_x += block->bound->border->width.left;
            lycon->block.advance_y += block->bound->border->width.top;
        }
        lycon->line.advance_x += block->bound->padding.left;
        lycon->block.advance_y += block->bound->padding.top;
        lycon->line.left = lycon->line.advance_x;
        // CRITICAL FIX: Set line.right to content area (block width - right padding)
        lycon->line.right = lycon->line.advance_x + content_width;
    }
    else {
        block->width = content_width;  block->height = content_height;
        // no change to block->x, block->y, lycon->line.advance_x, lycon->block.advance_y
        lycon->line.right = lycon->block.width;
    }

    log_debug("layout-block-sizes: x:%d, y:%d, wd:%d, hg:%d, line-hg:%d, given-w:%d, given-h:%d",
        block->x, block->y, block->width, block->height, lycon->block.line_height, lycon->block.given_width, lycon->block.given_height);

    // layout block content
    if (elmt_name != LXB_TAG_IMG) {
        layout_block_content(lycon, block, display);
    }

    // check for margin collapsing with children
    if (block->bound) {
        // collapse top margin with first child block
        log_debug("check margin collapsing");
        if ((!block->bound->border || block->bound->border->width.top == 0) &&
            block->bound->padding.top == 0 && block->child) {
            View* first_child = block->child;
            if (first_child->is_block() && ((ViewBlock*)first_child)->bound) {
                ViewBlock* first_child_block = (ViewBlock*)first_child;
                if (first_child_block->bound->margin.top > 0) {
                    int margin_top = max(block->bound->margin.top, first_child_block->bound->margin.top);
                    block->y += margin_top - block->bound->margin.top;
                    block->bound->margin.top = margin_top;
                    block->height -= first_child_block->bound->margin.top;
                    first_child_block->bound->margin.top = 0;
                    first_child_block->y = 0;
                    log_debug("collapsed top margin %d between block and first child", margin_top);
                }
            }
        }
        // collapse bottom margin with last child block
        if ((!block->bound->border || block->bound->border->width.bottom == 0) &&
            block->bound->padding.bottom == 0 && block->child) {
            View* last_child = block->child;
            while (last_child && last_child->next) { last_child = last_child->next; }
            if (last_child->is_block() && ((ViewBlock*)last_child)->bound) {
                ViewBlock* last_child_block = (ViewBlock*)last_child;
                if (last_child_block->bound->margin.bottom > 0) {
                    int margin_bottom = max(block->bound->margin.bottom, last_child_block->bound->margin.bottom);
                    block->height -= last_child_block->bound->margin.bottom;
                    block->bound->margin.bottom = margin_bottom;
                    last_child_block->bound->margin.bottom = 0;
                    log_debug("collapsed bottom margin %d between block and last child", margin_bottom);
                }
            }
        }
    }

    // Apply CSS positioning after normal layout
    if (block->position) {
        log_debug("DEBUG: Found position property: type=%d (RELATIVE=334, ABSOLUTE=335, FIXED=337)", block->position->position);
        log_debug("DEBUG: Position offsets: top=%d(%s), right=%d(%s), bottom=%d(%s), left=%d(%s)",
            block->position->top, block->position->has_top ? "set" : "unset",
            block->position->right, block->position->has_right ? "set" : "unset",
            block->position->bottom, block->position->has_bottom ? "set" : "unset",
            block->position->left, block->position->has_left ? "set" : "unset");

        if (block->position->position == 334) {  // LXB_CSS_VALUE_RELATIVE
            log_debug("DEBUG: Applying relative positioning");
            layout_relative_positioned(lycon, block);
        } else if (block->position->position == 335 || block->position->position == 337) {  // ABSOLUTE or FIXED
            log_debug("DEBUG: Applying absolute positioning");
            layout_absolute_positioned(lycon, block);
        } else {
            log_debug("DEBUG: Position type %d not handled yet", block->position->position);
        }
    } else {
        log_debug("DEBUG: No position property found for element %s", elmt->name());
    }

    // Apply CSS float layout after positioning
    if (block->position && element_has_float(block)) {
        log_debug("DEBUG: Element has float property, applying float layout");
        layout_float_element(lycon, block);
    }

    // Apply CSS clear property after float layout
    if (block->position && block->position->clear != LXB_CSS_VALUE_NONE) {
        log_debug("DEBUG: Element has clear property, applying clear layout");
        layout_clear_element(lycon, block);
    }

    // flow the block in parent context
    log_debug("flow block in parent context");
    lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;

    // Skip normal flow positioning for absolutely positioned elements
    if (block->position && (block->position->position == LXB_CSS_VALUE_ABSOLUTE ||
        block->position->position == LXB_CSS_VALUE_FIXED)) {
        log_debug("DEBUG: Skipping normal flow positioning for absolutely positioned element");
        // Absolutely positioned elements don't participate in normal flow
        // Their position was already set by the positioning code above
    }
    else if (display.outer == LXB_CSS_VALUE_INLINE_BLOCK) {
        if (!lycon->line.start_view) lycon->line.start_view = (View*)block;
        if (lycon->line.advance_x + block->width > lycon->line.right) {
            line_break(lycon);
            block->x = lycon->line.left;
        } else {
            block->x = lycon->line.advance_x;
        }
        if (block->in_line && block->in_line->vertical_align) {
            int offset = calculate_vertical_align_offset(
                block->in_line->vertical_align, block->height, lycon->block.line_height,
                lycon->line.max_ascender, block->height);
            block->y = lycon->block.advance_y + offset;  // block->bound->margin.top will be added below
            log_debug("vertical-aligned-inline-block: offset %d, line %d, block %d, adv: %d, y: %d, va:%d, %d",
                offset, lycon->block.line_height, block->height, lycon->block.advance_y, block->y,
                block->in_line->vertical_align, LXB_CSS_VALUE_BOTTOM);
        } else {
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
            int block_flow_height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
            lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->line.max_ascender);
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
            // check sibling margin collapsing
            int collapse = 0;
            View* prev_sibling = block->previous_view();
            if (prev_sibling && prev_sibling->is_block() && ((ViewBlock*)prev_sibling)->bound) {
                ViewBlock* prev_block = (ViewBlock*)prev_sibling;
                if (prev_block->bound->margin.bottom > 0 && block->bound->margin.top > 0) {
                    collapse = min(prev_block->bound->margin.bottom, block->bound->margin.top);
                    block->y -= collapse;
                    prev_block->bound->margin.bottom = 0;
                    log_debug("collapsed margin %d between sibling blocks", collapse);
                }
            }
            lycon->block.advance_y += block->height + block->bound->margin.top + block->bound->margin.bottom - collapse;
            lycon->block.max_width = max(lycon->block.max_width, block->width
                + block->bound->margin.left + block->bound->margin.right);
        } else {
            lycon->block.advance_y += block->height;
            lycon->block.max_width = max(lycon->block.max_width, block->width);
        }
        assert(lycon->line.is_line_start);
    }
    lycon->prev_view = (View*)block;
    log_debug("block view: %d, end block", block->type);
    log_leave();
}
