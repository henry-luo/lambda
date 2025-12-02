#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_flex_multipass.hpp"
#include "layout_grid_multipass.hpp"
#include "layout_positioned.hpp"
#include "grid.hpp"

#include "../lib/log.h"
#include "../lambda/input/css/selector_matcher.hpp"

View* layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
// void layout_flex_nodes(LayoutContext* lycon, lxb_dom_node_t *first_child);  // Removed: lexbor dependency
void resolve_inline_default(LayoutContext* lycon, ViewSpan* span);
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_table(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_flex_content(LayoutContext* lycon, ViewBlock* block);
void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, Blockbox *pa_block, Linebox *pa_line);

// ============================================================================
// Pseudo-element (::before/::after) Layout Support
// ============================================================================

/**
 * Create a pseudo-element DomElement with a DomText child for the content
 *
 * @param lycon Layout context
 * @param parent The parent element
 * @param content The content string for the pseudo-element
 * @param is_before true for ::before, false for ::after
 * @return The created DomElement or NULL on failure
 */
static DomElement* create_pseudo_element(LayoutContext* lycon, DomElement* parent,
                                          const char* content, bool is_before) {
    if (!lycon || !parent || !content || !*content) return nullptr;

    Pool* pool = lycon->doc->view_tree->pool;
    if (!pool) return nullptr;

    // Create the pseudo DomElement
    // Per CSS spec: pseudo-element is child of defining element,
    // text node is child of pseudo-element
    DomElement* pseudo_elem = (DomElement*)pool_calloc(pool, sizeof(DomElement));
    if (!pseudo_elem) return nullptr;

    // Initialize as element node
    pseudo_elem->node_type = DOM_NODE_ELEMENT;
    pseudo_elem->tag_name = is_before ? "::before" : "::after";
    pseudo_elem->doc = parent->doc;
    // Pseudo-element is child of defining element
    pseudo_elem->parent = parent;
    pseudo_elem->first_child = nullptr;
    pseudo_elem->next_sibling = nullptr;
    pseudo_elem->prev_sibling = nullptr;

    // Copy inherited styles from parent
    pseudo_elem->font = parent->font;
    pseudo_elem->bound = parent->bound;
    pseudo_elem->in_line = parent->in_line;

    // Set display to inline by default for pseudo-elements
    pseudo_elem->display.outer = CSS_VALUE_INLINE;
    pseudo_elem->display.inner = CSS_VALUE_FLOW;

    // Create the text child
    DomText* text_node = (DomText*)pool_calloc(pool, sizeof(DomText));
    if (!text_node) return nullptr;

    // Initialize as text node
    text_node->node_type = DOM_NODE_TEXT;
    // Text node is child of pseudo-element
    text_node->parent = pseudo_elem;
    text_node->next_sibling = nullptr;
    text_node->prev_sibling = nullptr;

    // Copy the content string
    size_t content_len = strlen(content);
    char* text_content = (char*)pool_calloc(pool, content_len + 1);
    if (text_content) {
        memcpy(text_content, content, content_len);
        text_content[content_len] = '\0';
    }
    text_node->text = text_content;
    text_node->length = content_len;
    text_node->native_string = nullptr;  // Not backed by Lambda String
    text_node->content_type = DOM_TEXT_STRING;

    // Link text node as child of pseudo element
    pseudo_elem->first_child = text_node;

    log_debug("[PSEUDO] Created ::%s element for <%s> with content \"%s\"",
              is_before ? "before" : "after",
              parent->tag_name ? parent->tag_name : "unknown",
              content);

    return pseudo_elem;
}

/**
 * Allocate PseudoContentProp and create pseudo-elements if needed
 *
 * On first layout: creates pseudo-elements and inserts them into DOM tree
 * On reflow: reuses existing pseudo-elements (already in DOM tree)
 *
 * @param lycon Layout context
 * @param block The block element to check
 * @return PseudoContentProp pointer or NULL if no pseudo content
 */
PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block || !block->is_element()) return nullptr;

    DomElement* elem = (DomElement*)block;

    // Check if pseudo-elements already exist (reflow case)
    if (block->pseudo) {
        log_debug("[PSEUDO] Reusing existing pseudo-elements for <%s>",
                  elem->tag_name ? elem->tag_name : "unknown");
        return block->pseudo;
    }

    // Check if element has ::before or ::after content
    bool has_before = dom_element_has_before_content(elem);
    bool has_after = dom_element_has_after_content(elem);

    if (!has_before && !has_after) return nullptr;

    // Allocate PseudoContentProp
    PseudoContentProp* pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
    if (!pseudo) return nullptr;

    // Initialize
    memset(pseudo, 0, sizeof(PseudoContentProp));

    // Create ::before pseudo-element if needed
    if (has_before) {
        const char* before_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_BEFORE);
        if (before_content && *before_content) {
            pseudo->before = create_pseudo_element(lycon, elem, before_content, true);
        }
    }

    // Create ::after pseudo-element if needed
    if (has_after) {
        const char* after_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_AFTER);
        if (after_content && *after_content) {
            pseudo->after = create_pseudo_element(lycon, elem, after_content, false);
        }
    }

    return pseudo;
}

/**
 * Layout a pseudo-element using the existing inline layout infrastructure
 *
 * Per CSS spec: pseudo-element is child of defining element, with display: inline.
 * We use layout_inline to handle the pseudo-element which will recursively
 * lay out its text child.
 *
 * @param lycon Layout context
 * @param pseudo_elem The pseudo-element DomElement (created by create_pseudo_element)
 */
static void layout_pseudo_element(LayoutContext* lycon, DomElement* pseudo_elem) {
    if (!pseudo_elem) return;

    log_debug("[PSEUDO] Laying out %s content", pseudo_elem->tag_name);

    // Layout the pseudo-element as inline (it will lay out its text child)
    layout_inline(lycon, pseudo_elem, pseudo_elem->display);
}

// ============================================================================
// End of Pseudo-element Layout Support
// ============================================================================

void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display) {
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

    log_debug("finalizing block, display=%d, given wd:%f", display, lycon->block.given_width);
    if (display == CSS_VALUE_INLINE_BLOCK && lycon->block.given_width < 0) {
        block->width = min(flow_width, block->width);
        log_debug("inline-block final width set to: %f", block->width);
    }

    // handle horizontal overflow
    if (flow_width > block->width) { // hz overflow
        if (!block->scroller) {
            block->scroller = alloc_scroll_prop(lycon);
        }
        block->scroller->has_hz_overflow = true;
        if (block->scroller->overflow_x == CSS_VALUE_VISIBLE) {
            lycon->block.pa_block->max_width = max(lycon->block.pa_block->max_width, flow_width);
        }
        else if (block->scroller->overflow_x == CSS_VALUE_SCROLL ||
            block->scroller->overflow_x == CSS_VALUE_AUTO) {
            block->scroller->has_hz_scroll = true;
        }
        if (block->scroller->has_hz_scroll ||
            block->scroller->overflow_x == CSS_VALUE_CLIP ||
            block->scroller->overflow_x == CSS_VALUE_HIDDEN) {
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
            if (block->scroller->overflow_y == CSS_VALUE_VISIBLE) {
                lycon->block.pa_block->max_height = max(lycon->block.pa_block->max_height, block->y + flow_height);
            }
            else if (block->scroller->overflow_y == CSS_VALUE_SCROLL || block->scroller->overflow_y == CSS_VALUE_AUTO) {
                block->scroller->has_vt_scroll = true;
            }
            if (block->scroller->has_hz_scroll ||
                block->scroller->overflow_y == CSS_VALUE_CLIP ||
                block->scroller->overflow_y == CSS_VALUE_HIDDEN) {
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
    // Update scroller clip if height changed and scroller has clipping enabled
    // This ensures the clip region is correct after auto-height is calculated
    if (block->scroller && block->scroller->has_clip) {
        block->scroller->clip.right = block->width;
        block->scroller->clip.bottom = block->height;
    }
    log_debug("finalized block wd:%f, hg:%f", block->width, block->height);
}

void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    DomDocument* doc = NULL;
    log_debug("layout iframe");
    if (!(block->embed && block->embed->doc)) {
        // load iframe document
        const char *value = block->get_attribute("src");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            log_debug("load iframe doc src: %s", src->str);
            doc = load_html_doc(lycon->ui_context->document->url, src->str);
            strbuf_free(src);
            if (!doc) {
                log_debug("failed to load iframe document");
                // todo: use a placeholder
            } else {
                if (!(block->embed)) block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                block->embed->doc = doc; // assign loaded document to embed property
                if (doc->html_root) {
                    layout_html_doc(lycon->ui_context, doc, false);
                }
            }
        } else {
            log_debug("iframe has no src attribute");
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

/**
 * Insert pseudo-element into DOM tree at appropriate position
 * ::before is inserted as first child, ::after as last child
 */
static void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before) {
    if (!parent || !pseudo) return;

    if (is_before) {
        // Insert as first child
        DomNode* old_first = parent->first_child;
        pseudo->next_sibling = old_first;
        pseudo->prev_sibling = nullptr;
        if (old_first) {
            old_first->prev_sibling = pseudo;
        }
        parent->first_child = pseudo;
    } else {
        // Insert as last child
        if (!parent->first_child) {
            parent->first_child = pseudo;
            pseudo->prev_sibling = nullptr;
            pseudo->next_sibling = nullptr;
        } else {
            // Find last child
            DomNode* last = parent->first_child;
            while (last->next_sibling) {
                last = last->next_sibling;
            }
            last->next_sibling = pseudo;
            pseudo->prev_sibling = last;
            pseudo->next_sibling = nullptr;
        }
    }
}

void layout_block_inner_content(LayoutContext* lycon, ViewBlock* block) {
    log_debug("layout block inner content");

    // Allocate pseudo-element content if ::before or ::after is present
    if (block->is_element()) {
        block->pseudo = alloc_pseudo_content_prop(lycon, block);

        // Insert pseudo-elements into DOM tree for proper view tree linking
        if (block->pseudo) {
            if (block->pseudo->before) {
                insert_pseudo_into_dom((DomElement*)block, block->pseudo->before, true);
            }
            if (block->pseudo->after) {
                insert_pseudo_into_dom((DomElement*)block, block->pseudo->after, false);
            }
        }
    }

    if (block->display.inner == RDT_DISPLAY_REPLACED) {  // image, iframe
        uintptr_t elmt_name = block->tag();
        if (elmt_name == HTM_TAG_IFRAME) {
            layout_iframe(lycon, block, block->display);
        }
        // else HTM_TAG_IMG
    } else {  // layout block child content
        // No longer need separate pseudo-element layout - they're part of child list now
        DomNode *child = nullptr;
        if (block->is_element()) { child = block->first_child; }
        if (child) {
            if (block->display.inner == CSS_VALUE_FLOW) {
                // inline content flow
                do {
                    layout_flow_node(lycon, child);
                    child = child->next_sibling;
                } while (child);
                // handle last line
                if (!lycon->line.is_line_start) {
                    line_break(lycon);
                }
            }
            else if (block->display.inner == CSS_VALUE_FLEX) {
                log_debug("Setting up flex container for %s", block->node_name());
                layout_flex_content(lycon, block);
                log_debug("Finished flex container layout for %s", block->node_name());
                return;
            }
            else if (block->display.inner == CSS_VALUE_GRID) {
                log_debug("Setting up grid container for %s (multipass)", block->node_name());
                // Use multipass grid layout (similar to flex layout pattern)
                layout_grid_content(lycon, block);
                log_debug("Finished grid container layout for %s", block->node_name());
                return;
            }
            else if (block->display.inner == CSS_VALUE_TABLE) {
                log_debug("TABLE LAYOUT TRIGGERED! outer=%d, inner=%d, element=%s",
                        block->display.outer, block->display.inner, block->node_name());
                layout_table(lycon, block, block->display);
                return;
            }
            else {
                log_debug("unknown display type");
            }
        }

        // Final line break after all content
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }

        finalize_block_flow(lycon, block, block->display.outer);
    }
}

float adjust_min_max_width(ViewBlock* block, float width) {
    if (block->blk) {
        if (block->blk->given_max_width >= 0 && width > block->blk->given_max_width) {
            width = block->blk->given_max_width;
            log_debug("[ADJUST] Clamped to max: %.2f", width);
        }
        // Note: given_min_width overrides given_max_width if both are specified
        if (block->blk->given_min_width >= 0 && width < block->blk->given_min_width) {
            width = block->blk->given_min_width;
            log_debug("[ADJUST] Clamped to min: %.2f", width);
        }
    }
    log_debug("[ADJUST] adjust_min_max_width: output=%.2f", width);
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
    setup_line_height(lycon, block);

    // setup initial ascender and descender
    lycon->block.init_ascender = lycon->font.ft_face->size->metrics.ascender / 64.0;
    lycon->block.init_descender = (-lycon->font.ft_face->size->metrics.descender) / 64.0;
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height - (lycon->block.init_ascender + lycon->block.init_descender)) / 2);
    log_debug("block line_height: %f, font height: %f, asc+desc: %f, lead_y: %f", lycon->block.line_height, lycon->font.ft_face->size->metrics.height / 64.0,
        lycon->block.init_ascender + lycon->block.init_descender, lycon->block.lead_y);
}

void layout_block_content(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, Blockbox *pa_block, Linebox *pa_line) {
    block->x = pa_line->left;  block->y = pa_block->advance_y;
    log_debug("block init position (%s): x=%f, y=%f, pa_block.advance_y=%f, display: outer=%d, inner=%d",
        elmt->node_name(), block->x, block->y, pa_block->advance_y, block->display.outer, block->display.inner);

    uintptr_t elmt_name = elmt->tag();
    if (elmt_name == HTM_TAG_IMG) { // load image intrinsic width and height
        const char *value;
        value = elmt->get_attribute("src");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
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
        elmt->node_name(), lycon->block.given_width, lycon->block.given_height, (void*)block->blk,
        block->blk ? block->blk->given_width_type : -1);
    bool cond1 = (lycon->block.given_width >= 0);
    bool cond2 = (!block->blk || block->blk->given_width_type != CSS_VALUE_AUTO);
    if (lycon->block.given_width >= 0 && (!block->blk || block->blk->given_width_type != CSS_VALUE_AUTO)) {
        content_width = max(lycon->block.given_width, 0);
        log_debug("Using given_width: content_width=%.2f", content_width);
        content_width = adjust_min_max_width(block, content_width);
        log_debug("After adjust_min_max_width: content_width=%.2f", content_width);
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
            log_debug("After adjust_border_padding (border-box): content_width=%.2f", content_width);
        }
    }
    else { // derive from parent block width
        log_debug("Deriving from parent: pa_block.content_width=%.2f", pa_block->content_width);
        if (block->bound) {
            content_width = pa_block->content_width
                - (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                - (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        else { content_width = pa_block->content_width; }
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
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
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_height = adjust_border_padding_height(block, content_height);
        }
    }
    else { // derive from parent block height
        if (block->bound) {
            content_height = pa_block->content_height - block->bound->margin.top - block->bound->margin.bottom;
        }
        else { content_height = pa_block->content_height; }
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
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

    // Update available space to match content dimensions
    // Preserve intrinsic sizing mode if already set (for nested measurement)
    if (!lycon->available_space.is_intrinsic_sizing()) {
        lycon->available_space.width = AvailableSize::make_definite(content_width);
        if (content_height > 0) {
            lycon->available_space.height = AvailableSize::make_definite(content_height);
        }
    }

    if (block->bound) {
        block->width = content_width + block->bound->padding.left + block->bound->padding.right +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
        block->height = content_height + block->bound->padding.top + block->bound->padding.bottom +
            (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
        // todo: we should keep LENGTH_AUTO (may be in flags) for reflow
        log_debug("block margins: left=%f, right=%f, left_type=%d, right_type=%d",
            block->bound->margin.left, block->bound->margin.right, block->bound->margin.left_type, block->bound->margin.right_type);
        if (block->bound->margin.left_type == CSS_VALUE_AUTO && block->bound->margin.right_type == CSS_VALUE_AUTO)  {
            block->bound->margin.left = block->bound->margin.right = max((pa_block->content_width - block->width) / 2, 0);
        } else {
            if (block->bound->margin.left_type == CSS_VALUE_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right_type == CSS_VALUE_AUTO) block->bound->margin.right = 0;
        }
        log_debug("finalize block margins: left=%f, right=%f", block->bound->margin.left, block->bound->margin.right);
        float y_before_margin = block->y;
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;
        log_debug("Y coordinate: before margin=%f, margin.top=%f, after margin=%f (tag=%s)",
                  y_before_margin, block->bound->margin.top, block->y, block->node_name());
    }
    else {
        block->width = content_width;  block->height = content_height;
        // no change to block->x, block->y
    }
    log_debug("layout-block-sizes: x:%f, y:%f, wd:%f, hg:%f, line-hg:%f, given-w:%f, given-h:%f",
        block->x, block->y, block->width, block->height, lycon->block.line_height, lycon->block.given_width, lycon->block.given_height);

    // IMPORTANT: Apply clear BEFORE setting up inline context and laying out children
    // Clear positions this element below earlier floats
    // This must happen after Y position and margins are set, but before children are laid out
    if (block->position && block->position->clear != CSS_VALUE_NONE) {
        log_debug("Element has clear property, applying clear layout BEFORE children");
        layout_clear_element(lycon, block);
    }

    // setup inline context
    setup_inline(lycon, block);

    // layout block content, and determine flow width and height
    layout_block_inner_content(lycon, block);

    // check for margin collapsing with children
    if (block->bound) {
        // collapse bottom margin with last child block
        if ((!block->bound->border || block->bound->border->width.bottom == 0) &&
            block->bound->padding.bottom == 0 && block->first_child) {
            View* last_placed = block->last_placed_child();
            if (last_placed && last_placed->is_block() && ((ViewBlock*)last_placed)->bound) {
                ViewBlock* last_child_block = (ViewBlock*)last_placed;
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

    // Apply CSS float layout
    // This adds the element to the float context for affecting later content
    if (block->position && element_has_float(block)) {
        log_debug("Element has float property, applying float layout");
        layout_float_element(lycon, block);
    }
}

void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    log_enter();
    // display: CSS_VALUE_BLOCK, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_LIST_ITEM
    log_debug("layout block %s (display: outer=%d, inner=%d)", elmt->node_name(), display.outer, display.inner);

    // Check if this block is a flex item
    ViewGroup* parent_block = (ViewGroup*)elmt->parent;
    bool is_flex_item = (parent_block && parent_block->display.inner == CSS_VALUE_FLEX);

    if (display.outer != CSS_VALUE_INLINE_BLOCK) {
        if (!lycon->line.is_line_start) { line_break(lycon); }
    }
    // save parent context
    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // -1 as unresolved
    lycon->block.pa_block = &pa_block;  lycon->elmt = elmt;
    log_debug("saved pa_block.advance_y: %.2f for element %s", pa_block.advance_y, elmt->node_name());
    lycon->block.content_width = lycon->block.content_height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;

    uintptr_t elmt_name = elmt->tag();
    ViewBlock* block = (ViewBlock*)set_view(lycon,
        display.outer == CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        display.inner == CSS_VALUE_TABLE ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK,
        elmt);
    block->display = display;

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    if (block->position && (block->position->position == CSS_VALUE_ABSOLUTE || block->position->position == CSS_VALUE_FIXED)) {
        layout_abs_block(lycon, elmt, block, &pa_block, &pa_line);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
    } else {
        // layout block content to determine content width and height
        layout_block_content(lycon, elmt, block, &pa_block, &pa_line);

        log_debug("flow block in parent context, block->y before restoration: %.2f", block->y);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;

        // flow the block in parent context
        if (display.outer == CSS_VALUE_INLINE_BLOCK) {
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
            if (block->in_line && block->in_line->vertical_align != CSS_VALUE_BASELINE) {
                float block_flow_height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
                if (block->in_line->vertical_align == CSS_VALUE_TEXT_TOP) {
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->block.init_ascender);
                }
                else if (block->in_line->vertical_align == CSS_VALUE_TEXT_BOTTOM) {
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
            // Check if this is a floated element - floats are out of normal flow
            // and should NOT advance the parent's advance_y
            bool is_float = block->position && element_has_float(block);

            if (is_float) {
                // Floated elements don't participate in normal flow
                // They don't advance the parent's advance_y
                // Only update max_width for containing block sizing
                if (block->bound) {
                    lycon->block.max_width = max(lycon->block.max_width, block->width
                        + block->bound->margin.left + block->bound->margin.right);
                } else {
                    lycon->block.max_width = max(lycon->block.max_width, block->width);
                }
                log_debug("float block end (no advance_y update), pa max_width: %f, block hg: %f",
                    lycon->block.max_width, block->height);
            } else if (block->bound) {
                // collapse top margin with parent block
                log_debug("check margin collapsing");
                if (block->parent_view()->first_placed_child() == block) {  // first child
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
                    View* prev_view = block->prev_placed_view();
                    if (prev_view && prev_view->is_block() && ((ViewBlock*)prev_view)->bound) {
                        ViewBlock* prev_block = (ViewBlock*)prev_view;
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

        // apply CSS relative positioning after normal layout
        if (block->position && block->position->position == CSS_VALUE_RELATIVE) {
            log_debug("Applying relative positioning");
            layout_relative_positioned(lycon, block);
        }
    }
    log_leave();
}
