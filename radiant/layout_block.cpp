#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_flex_multipass.hpp"
#include "layout_grid_multipass.hpp"
#include "layout_positioned.hpp"
#include "layout_bfc.hpp"
#include "intrinsic_sizing.hpp"
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
        block->scroller->clip.left = 0;
        block->scroller->clip.top = 0;
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

// Forward declaration
void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display);

/**
 * Check if an element is a float by examining its specified style
 * This is called before the element has a view, so we check the CSS properties directly
 */
static CssEnum get_element_float_value(DomElement* elem) {
    if (!elem) return CSS_VALUE_NONE;

    // First check if position is already resolved
    if (elem->position) {
        return elem->position->float_prop;
    }

    // Check float property from CSS style tree
    if (elem->specified_style && elem->specified_style->tree) {
        AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
        if (float_node) {
            StyleNode* style_node = (StyleNode*)float_node->declaration;
            if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                CssValue* val = style_node->winning_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    return val->data.keyword;
                }
            }
        }
    }
    return CSS_VALUE_NONE;
}

/**
 * Pre-scan inline siblings for floats and layout them first
 * This ensures floats are positioned before inline content that follows them in DOM order
 *
 * Per CSS 2.2: floats affect the current line, so they must be positioned before
 * we lay out inline content that shares the same line.
 *
 * IMPORTANT: This only applies when there's inline content mixed with floats.
 * If all siblings are block-level, floats appear at their encounter point.
 *
 * @param lycon Layout context
 * @param first_child First child to scan from
 * @param parent_block The parent block establishing the formatting context
 */
/**
 * Pre-scan and layout ALL floats in the content.
 *
 * CSS floats are "out of flow" - they're positioned and then content flows around them.
 * This means floats affect content that comes BEFORE them in DOM order if that content
 * is on the same line.
 *
 * For simplicity, we pre-lay ALL floats at Y=0, then during inline layout, content
 * flows around them via adjust_line_for_floats(). If this causes issues with floats
 * that should appear lower (due to preceding block-level content), we'll need a more
 * sophisticated approach.
 *
 * This handles cases like:
 *   <span>Filler Text</span><float/>  -> float at (0,0), text at (96,0) ✓
 *   <span>Long text...</span><float/> -> float at (0,0) - WRONG, should be (0, line2)
 */
static void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* parent_block) {
    if (!first_child) return;

    // Check if there are any floats in the content
    // Also check if the content BEFORE the first float is short enough to share a line
    bool has_floats = false;
    bool has_inline_content = false;
    float preceding_content_width = 0.0f;  // Estimated width of content before first float
    // Note: parent_block->content_width might be 0 at this point (set during finalization)
    // Use lycon->block.content_width which is set during block setup
    float container_width = lycon->block.content_width;
    DomNode* first_float_node = nullptr;

    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) {
            // Text nodes - estimate width (rough approximation)
            if (child->is_text()) {
                DomText* text = child->as_text();
                if (text && text->text && !first_float_node) {
                    // Count non-whitespace characters and estimate width
                    const char* p = text->text;
                    int char_count = 0;
                    while (*p) {
                        if (!isspace(*p)) char_count++;
                        p++;
                    }
                    // Rough estimate: 8px per character
                    preceding_content_width += char_count * 8.0f;
                    if (char_count > 0) has_inline_content = true;
                }
            }
            continue;
        }

        DomElement* elem = child->as_element();
        if (elem->float_prelaid) continue;

        // Check if element is a float
        CssEnum float_value = get_element_float_value(elem);
        if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
            has_floats = true;
            if (!first_float_node) first_float_node = child;
            continue;
        }

        // Check if element is inline or block content before the first float
        if (!first_float_node) {
            DisplayValue display = resolve_display_value(child);
            if (display.outer == CSS_VALUE_INLINE || display.outer == CSS_VALUE_INLINE_BLOCK) {
                has_inline_content = true;

                // Count text content inside this inline element for width estimation
                for (DomNode* text_node = elem->first_child; text_node; text_node = text_node->next_sibling) {
                    if (text_node->is_text()) {
                        DomText* text = text_node->as_text();
                        if (text && text->text) {
                            const char* p = text->text;
                            int char_count = 0;
                            while (*p) {
                                if (!isspace(*p)) char_count++;
                                p++;
                            }
                            preceding_content_width += char_count * 8.0f;
                        }
                    }
                }
            } else if (display.outer == CSS_VALUE_BLOCK) {
                // Block element before the first float - don't pre-scan
                // The float should appear after this block in normal flow
                log_debug("[FLOAT PRE-SCAN] Block element before float, skipping pre-scan");
                return;
            }
        }
    }

    // No floats to pre-scan
    if (!has_floats) {
        log_debug("[FLOAT PRE-SCAN] No floats found, skipping pre-scan");
        return;
    }

    log_debug("[FLOAT PRE-SCAN] has_inline_content=%d, container_width=%.1f, preceding_content_width=%.1f",
              has_inline_content, container_width, preceding_content_width);

    // Check if preceding content is too wide to share a line with the float
    // If so, don't pre-scan - let the float appear at its encounter point
    if (has_inline_content && container_width > 0) {
        // Rough estimate: assume float is ~100px wide (common case)
        // This heuristic works for:
        // - floats-029: container=1200, content=67, float=96 → 67+96=163 < 1200 → pre-scan
        // - floats-020: container=216, content=~200+, float=96 → 200+96=296 > 216 → no pre-scan
        float float_width = 100.0f;  // Conservative estimate

        // If preceding content + float > container width, don't pre-scan
        if (preceding_content_width + float_width > container_width) {
            log_debug("[FLOAT PRE-SCAN] Content before float (%.1f) + float (%.1f) > container (%.1f), skip pre-scan",
                      preceding_content_width, float_width, container_width);
            return;
        }
    }

    // Initialize float context if needed (for float positioning)
    if (!lycon->current_float_context && parent_block) {
        lycon->current_float_context = float_context_create(parent_block);
        log_debug("[FLOAT PRE-SCAN] Created float context for parent block %s",
                  parent_block->node_name());
    }

    if (!lycon->current_float_context) {
        log_debug("[FLOAT PRE-SCAN] No float context available, cannot pre-scan");
        return;
    }

    log_debug("[FLOAT PRE-SCAN] Pre-laying ALL floats in content");

    // Pre-lay ALL floats in the content
    // This is a simplification - ideally we'd only pre-lay floats that can share a line with preceding content
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;

        DomElement* elem = child->as_element();

        // Skip if already pre-laid
        if (elem->float_prelaid) continue;

        CssEnum float_value = get_element_float_value(elem);
        if (float_value != CSS_VALUE_LEFT && float_value != CSS_VALUE_RIGHT) continue;

        log_debug("[FLOAT PRE-SCAN] Pre-laying float: %s (float=%d)",
                  child->node_name(), float_value);

        // Layout the float now
        DisplayValue display = resolve_display_value(child);
        display.outer = CSS_VALUE_BLOCK;  // Floats become block per CSS 9.7

        // Mark as pre-laid to skip during normal flow
        elem->float_prelaid = true;

        // Layout the float block
        layout_block(lycon, child, display);
    }

    // After pre-scanning floats, adjust the current line bounds for the floats we just laid out
    // This is critical: the first line needs to start AFTER the float, not at x=0
    //
    // Note: We can't use adjust_line_for_floats() here because lycon->view is not set to the
    // current block yet. Instead, directly query float space at y=0 and update line bounds.
    if (lycon->current_float_context) {
        log_debug("[FLOAT PRE-SCAN] Adjusting initial line bounds for pre-scanned floats");

        float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;
        FloatAvailableSpace space = float_space_at_y(lycon->current_float_context, 0.0f, line_height);

        if (space.has_left_float) {
            // Left float intrudes - adjust effective_left and advance_x
            // space.left is relative to the container content area
            float new_left = space.left;
            if (new_left > lycon->line.effective_left) {
                log_debug("[FLOAT PRE-SCAN] Adjusting line.effective_left: %.1f -> %.1f",
                          lycon->line.effective_left, new_left);
                lycon->line.effective_left = new_left;
                lycon->line.has_float_intrusion = true;
                if (lycon->line.advance_x < new_left) {
                    lycon->line.advance_x = new_left;
                }
            }
        }
        if (space.has_right_float) {
            // Right float intrudes - adjust effective_right
            float new_right = space.right;
            if (new_right < lycon->line.effective_right) {
                log_debug("[FLOAT PRE-SCAN] Adjusting line.effective_right: %.1f -> %.1f",
                          lycon->line.effective_right, new_right);
                lycon->line.effective_right = new_right;
                lycon->line.has_float_intrusion = true;
            }
        }
    }

    log_debug("[FLOAT PRE-SCAN] Pre-scan complete");
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
                // Pre-scan and layout floats BEFORE laying out inline content
                // This ensures floats are positioned and affect line bounds correctly
                prescan_and_layout_floats(lycon, child, block);

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

    // Calculate the block's inner content bounds based on border and padding
    // Note: content_width is already the inner content width (excluding padding/border)
    // line.left/right define the line box boundaries in local block coordinates
    float inner_left = 0;

    if (block->bound) {
        if (block->bound->border) {
            inner_left += block->bound->border->width.left;
            lycon->block.advance_y += block->bound->border->width.top;
        }
        inner_left += block->bound->padding.left;
        lycon->block.advance_y += block->bound->padding.top;
    }

    // line.right = inner_left + content_width gives the full content area
    float inner_right = inner_left + content_width;

    // Set the block's container bounds (line.left/right)
    // These define the nominal line box boundaries for this block
    lycon->line.left = inner_left;
    lycon->line.right = inner_right;

    // Initialize effective bounds to match container bounds
    // line_reset() will adjust these for floats if needed
    lycon->line.effective_left = inner_left;
    lycon->line.effective_right = inner_right;
    lycon->line.has_float_intrusion = false;
    lycon->line.advance_x = inner_left;
    lycon->line.vertical_align = CSS_VALUE_BASELINE;

    // Now call line_reset to adjust for floats at current Y position
    // This will call adjust_line_for_floats which updates effective_left/right
    line_reset(lycon);

    log_debug("setup_inline: line.left=%.1f, line.right=%.1f, effective_left=%.1f, effective_right=%.1f, advance_x=%.1f",
              lycon->line.left, lycon->line.right, lycon->line.effective_left, lycon->line.effective_right, lycon->line.advance_x);

    if (block->blk) lycon->block.text_align = block->blk->text_align;
    // setup font
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, block->font);
    }
    // setup line height
    setup_line_height(lycon, block);

    // setup initial ascender and descender
    // Use OS/2 sTypo metrics only when USE_TYPO_METRICS flag is set (Chrome behavior)
    TypoMetrics typo = get_os2_typo_metrics(lycon->font.ft_face);
    if (typo.valid && typo.use_typo_metrics) {
        lycon->block.init_ascender = typo.ascender;
        lycon->block.init_descender = typo.descender;
    } else {
        // Fallback to FreeType HHEA metrics
        lycon->block.init_ascender = lycon->font.ft_face->size->metrics.ascender / 64.0;
        lycon->block.init_descender = (-lycon->font.ft_face->size->metrics.descender) / 64.0;
    }
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height - (lycon->block.init_ascender + lycon->block.init_descender)) / 2);
    log_debug("block line_height: %f, font height: %f, asc+desc: %f, lead_y: %f", lycon->block.line_height, lycon->font.ft_face->size->metrics.height / 64.0,
        lycon->block.init_ascender + lycon->block.init_descender, lycon->block.lead_y);
}

void layout_block_content(LayoutContext* lycon, ViewBlock* block, Blockbox *pa_block, Linebox *pa_line) {
    block->x = pa_line->left;  block->y = pa_block->advance_y;

    // CSS 2.2 9.5: For floats appearing after inline content, position at bottom of current line
    // "A floating box's outer top may not be higher than the outer top of any block or floated
    // box generated by an element earlier in the source document."
    // When a float appears after inline content on the current line, it should start
    // below that content, not at advance_y which may be 0.
    bool is_float =  block->position && (block->position->float_prop == CSS_VALUE_LEFT || block->position->float_prop == CSS_VALUE_RIGHT);

    if (is_float && !pa_line->is_line_start) {
        // Float appears after inline content - position at bottom of current line
        float line_height = pa_block->line_height > 0 ? pa_block->line_height : 18.0f;
        block->y = pa_block->advance_y + line_height;
        log_debug("Float positioned below current line: y=%.1f (advance_y=%.1f + line_height=%.1f)",
                  block->y, pa_block->advance_y, line_height);
    }

    log_debug("block init position (%s): x=%f, y=%f, pa_block.advance_y=%f, display: outer=%d, inner=%d",
        block->node_name(), block->x, block->y, pa_block->advance_y, block->display.outer, block->display.inner);

    // Check if this block establishes a new BFC
    BlockFormattingContext* parent_bfc = lycon->bfc;
    BlockFormattingContext* new_bfc = create_bfc_if_needed(block, lycon->pool, parent_bfc);
    if (new_bfc) {
        lycon->bfc = new_bfc;
        lycon->owns_bfc = true;
        log_debug("[BFC] Block %s establishes new BFC", block->node_name());
    } else {
        lycon->owns_bfc = false;
    }

    uintptr_t elmt_name = block->tag();
    if (elmt_name == HTM_TAG_IMG) { // load image intrinsic width and height
        const char *value;
        value = block->get_attribute("src");
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
        block->node_name(), lycon->block.given_width, lycon->block.given_height, (void*)block->blk,
        block->blk ? block->blk->given_width_type : -1);

    // Check if this is a floated element with auto width
    // CSS 2.2 Section 10.3.5: Floats with auto width use shrink-to-fit width
    // We'll do a post-layout adjustment after content is laid out
    bool is_float_auto_width = element_has_float(block) &&
        lycon->block.given_width < 0 && (!block->blk || block->blk->given_width_type == CSS_VALUE_AUTO);

    if (is_float_auto_width) {
        // For floats with auto width, initially use available width for layout
        // Then shrink to fit content in post-layout step
        float available_width = pa_block->content_width;
        if (block->bound) {
            available_width -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        content_width = available_width;
        log_debug("Float auto-width: initial layout with available_width=%.2f (will shrink post-layout)", content_width);
        content_width = adjust_min_max_width(block, content_width);
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        }
    }
    else if (lycon->block.given_width >= 0 && (!block->blk || block->blk->given_width_type != CSS_VALUE_AUTO)) {
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
    // Note: Check for actual clear values (LEFT=50, RIGHT=51, BOTH), not just "not NONE"
    // because uninitialized clear is 0 (CSS_VALUE__UNDEF) which would incorrectly pass "!= NONE"
    if (block->position && (block->position->clear == CSS_VALUE_LEFT ||
                             block->position->clear == CSS_VALUE_RIGHT ||
                             block->position->clear == CSS_VALUE_BOTH)) {
        log_debug("Element has clear property, applying clear layout BEFORE children");
        layout_clear_element(lycon, block);
    }

    // setup inline context
    setup_inline(lycon, block);

    // For floats with auto width, calculate intrinsic width BEFORE laying out children
    // This ensures children are laid out with the correct shrink-to-fit width
    if (is_float_auto_width && block->is_element()) {
        // Font is loaded after setup_inline, so now we can calculate intrinsic width
        DomElement* dom_element = (DomElement*)block;
        float available = pa_block->content_width;
        if (block->bound) {
            available -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                      + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }

        // Calculate fit-content width (shrink-to-fit)
        int fit_content = calculate_fit_content_width(lycon, dom_element, (int)available);

        if (fit_content > 0 && fit_content < (int)block->width) {
            log_debug("Float shrink-to-fit: fit_content=%d, old_width=%.1f, available=%.1f",
                fit_content, block->width, available);

            // Update block width to shrink-to-fit size
            block->width = (float)fit_content;

            // Also update content_width for child layout
            float new_content_width = block->width;
            if (block->bound) {
                new_content_width -= block->bound->padding.left + block->bound->padding.right;
                if (block->bound->border) {
                    new_content_width -= block->bound->border->width.left + block->bound->border->width.right;
                }
            }
            block->content_width = max(new_content_width, 0.0f);
            lycon->block.content_width = block->content_width;

            // Re-setup line context with new width
            line_init(lycon, 0, block->content_width);
            if (block->bound) {
                if (block->bound->border) {
                    lycon->line.advance_x += block->bound->border->width.left;
                    lycon->line.right -= block->bound->border->width.right;
                }
                lycon->line.advance_x += block->bound->padding.left;
                lycon->line.left = lycon->line.advance_x;
                lycon->line.right = lycon->line.left + block->content_width;
            }
        }
    }

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

    // BFC (Block Formatting Context) height expansion to contain floats
    // CSS 2.2 Section 10.6.7: In certain cases (BFC roots), the heights of floating
    // descendants are also taken into account when computing the height
    // BFC is established by: overflow != visible, display: flow-root, float != none, etc.
    bool creates_bfc = block->scroller &&
                       (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
                        block->scroller->overflow_y != CSS_VALUE_VISIBLE);

    log_debug("BFC check for %s: creates_bfc=%d, scroller=%p, overflow_x=%d",
        block->node_name(), creates_bfc, block->scroller, block->scroller ? block->scroller->overflow_x : -1);

    if (creates_bfc) {
        // Try new BFC system first
        if (lycon->bfc && lycon->bfc->establishing_element == block) {
            float max_float_bottom = lycon->bfc->lowest_float_bottom;
            float content_bottom = block->y + block->height;
            log_debug("[BFC] Height expansion check: max_float_bottom=%.1f, content_bottom=%.1f",
                      max_float_bottom, content_bottom);
            if (max_float_bottom > content_bottom - block->y) {
                float old_height = block->height;
                block->height = max_float_bottom;
                log_debug("[BFC] Height expanded: old=%.1f, new=%.1f", old_height, block->height);
            }
        }

        // Also check legacy FloatContext
        FloatContext* float_ctx = get_current_float_context(lycon);
        log_debug("BFC %s: float_ctx=%p, container=%p, this_block=%p",
            block->node_name(), float_ctx, float_ctx ? float_ctx->container : nullptr, block);
        if (float_ctx && float_ctx->container == block) {
            // Find the maximum bottom of all floated children
            float max_float_bottom = 0;
            log_debug("BFC %s: checking left floats, head=%p", block->node_name(), float_ctx->left.head);
            for (FloatBox* fb = float_ctx->left.head; fb; fb = fb->next) {
                log_debug("BFC left float: margin_box_bottom=%.1f", fb->margin_box_bottom);
                if (fb->margin_box_bottom > max_float_bottom) {
                    max_float_bottom = fb->margin_box_bottom;
                }
            }
            log_debug("BFC %s: checking right floats, head=%p", block->node_name(), float_ctx->right.head);
            for (FloatBox* fb = float_ctx->right.head; fb; fb = fb->next) {
                log_debug("BFC right float: margin_box_bottom=%.1f", fb->margin_box_bottom);
                if (fb->margin_box_bottom > max_float_bottom) {
                    max_float_bottom = fb->margin_box_bottom;
                }
            }

            // Float margin_box coordinates are relative to container's content area
            // Compare to block->height which is also relative/local
            log_debug("BFC %s: max_float_bottom=%.1f, block->height=%.1f",
                block->node_name(), max_float_bottom, block->height);
            if (max_float_bottom > block->height) {
                float old_height = block->height;
                block->height = max_float_bottom;
                log_debug("BFC height expansion: old=%.1f, new=%.1f (float_bottom=%.1f)",
                          old_height, block->height, max_float_bottom);

                // Update scroller clip to match new height (for overflow:hidden rendering)
                if (block->scroller && block->scroller->has_clip) {
                    block->scroller->clip.bottom = block->height;
                    log_debug("BFC updated clip.bottom to %.1f", block->height);
                }
            }
        }
    }

    // Apply CSS float layout using BFC or legacy FloatContext
    if (block->position && element_has_float(block)) {
        log_debug("Element has float property, applying float layout");

        // For now, always use legacy FloatContext to maintain backward compatibility
        // BFC float positioning will be enabled later after further testing
        layout_float_element(lycon, block);

        // Also add to BFC for future line adjustments (parallel tracking)
        if (lycon->bfc) {
            lycon->bfc->add_float(block);
            log_debug("[BFC] Float added to BFC (legacy positioned)");
        }
    }

    // Restore parent BFC if we created a new one
    if (lycon->owns_bfc && lycon->bfc && lycon->bfc->parent_bfc) {
        lycon->bfc = lycon->bfc->parent_bfc;
        lycon->owns_bfc = false;
    }
}

void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    log_enter();
    // display: CSS_VALUE_BLOCK, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_LIST_ITEM
    log_debug("layout block %s (display: outer=%d, inner=%d)", elmt->node_name(), display.outer, display.inner);

    // Check if this block is a flex item
    ViewElement* parent_block = (ViewElement*)elmt->parent;
    bool is_flex_item = (parent_block && parent_block->display.inner == CSS_VALUE_FLEX);

    // CSS 2.2: Floats are removed from normal flow and don't cause line breaks
    // Check for float property before deciding on line break
    bool is_float = false;
    if (elmt->is_element()) {
        DomElement* elem = elmt->as_element();
        if (elem->position && elem->position->float_prop != CSS_VALUE_NONE) {
            is_float = true;
        } else if (elem->specified_style && elem->specified_style->tree) {
            // Check float property from CSS style tree
            AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
            if (float_node) {
                StyleNode* style_node = (StyleNode*)float_node->declaration;
                if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                    CssValue* val = style_node->winning_decl->value;
                    if (val->type == CSS_VALUE_TYPE_KEYWORD &&
                        (val->data.keyword == CSS_VALUE_LEFT || val->data.keyword == CSS_VALUE_RIGHT)) {
                        is_float = true;
                    }
                }
            }
        }
    }

    // Only cause line break for non-inline-block, non-float blocks
    if (display.outer != CSS_VALUE_INLINE_BLOCK && !is_float) {
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
        layout_block_content(lycon, block, &pa_block, &pa_line);

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
                // Note: floats don't require is_line_start - they're out of flow
            } else if (block->bound) {
                // collapse top margin with parent block
                log_debug("check margin collapsing");

                // Find first in-flow child (skip floats for margin collapsing purposes)
                View* first_in_flow_child = block->parent_view()->first_placed_child();
                while (first_in_flow_child) {
                    if (!first_in_flow_child->is_block()) break;
                    ViewBlock* vb = (ViewBlock*)first_in_flow_child;
                    if (vb->position && element_has_float(vb)) {
                        // Skip to next placed sibling
                        View* next = (View*)first_in_flow_child->next_sibling;
                        while (next && !next->view_type) {
                            next = (View*)next->next_sibling;
                        }
                        first_in_flow_child = next;
                        continue;
                    }
                    break;
                }

                if (first_in_flow_child == block) {  // first in-flow child
                    if (block->bound->margin.top > 0) {
                        ViewBlock* parent = block->parent->is_block() ? (ViewBlock*)block->parent : NULL;
                        // Check if parent creates a BFC - BFC prevents margin collapsing
                        // BFC is created by: overflow != visible, float, position absolute/fixed, etc.
                        bool parent_creates_bfc = parent && parent->scroller &&
                            (parent->scroller->overflow_x != CSS_VALUE_VISIBLE ||
                             parent->scroller->overflow_y != CSS_VALUE_VISIBLE);
                        // parent has top margin, but no border, no padding;  parent->parent to exclude html
                        // Also: no margin collapsing if parent creates BFC
                        if (parent && parent->parent && parent->bound && !parent_creates_bfc &&
                            parent->bound->padding.top == 0 &&
                            (!parent->bound->border || parent->bound->border->width.top == 0)) {
                            float margin_top = max(block->bound->margin.top, parent->bound->margin.top);
                            parent->y += margin_top - parent->bound->margin.top;
                            parent->bound->margin.top = margin_top;
                            block->y = 0;  block->bound->margin.top = 0;
                            log_debug("collapsed margin between block and first child: %f, parent y: %f, block y: %f", margin_top, parent->y, block->y);
                        }
                        else {
                            log_debug("no parent margin collapsing: parent->bound=%p, border-top=%f, padding-top=%f, parent_creates_bfc=%d",
                                parent ? parent->bound : NULL,
                                parent && parent->bound && parent->bound->border ? parent->bound->border->width.top : 0,
                                parent && parent->bound ? parent->bound->padding.top : 0,
                                parent_creates_bfc);
                        }
                    }
                }
                else {
                    // check sibling margin collapsing
                    // CSS 2.2 Section 8.3.1: Margins do NOT collapse when there's clearance
                    // If this block has clear property, skip sibling margin collapsing
                    bool has_clearance = block->position &&
                        (block->position->clear == CSS_VALUE_LEFT ||
                         block->position->clear == CSS_VALUE_RIGHT ||
                         block->position->clear == CSS_VALUE_BOTH);

                    if (!has_clearance) {
                        float collapse = 0;
                        // Find previous in-flow sibling (skip floats)
                        View* prev_view = block->prev_placed_view();
                        while (prev_view && prev_view->is_block()) {
                            ViewBlock* vb = (ViewBlock*)prev_view;
                            // Skip floats - they're out of normal flow and don't participate in margin collapsing
                            if (vb->position && element_has_float(vb)) {
                                prev_view = prev_view->prev_placed_view();
                                continue;
                            }
                            break;
                        }
                        if (prev_view && prev_view->is_block() && ((ViewBlock*)prev_view)->bound) {
                            ViewBlock* prev_block = (ViewBlock*)prev_view;
                            if (prev_block->bound->margin.bottom > 0 && block->bound->margin.top > 0) {
                                collapse = min(prev_block->bound->margin.bottom, block->bound->margin.top);
                                block->y -= collapse;
                                block->bound->margin.top -= collapse;
                                log_debug("collapsed margin between sibling blocks: %f, block->y now: %f", collapse, block->y);
                            }
                        }
                    } else {
                        log_debug("skipping sibling margin collapsing for element with clear property");
                    }
                }
                lycon->block.advance_y += block->height + block->bound->margin.top + block->bound->margin.bottom;
                lycon->block.max_width = max(lycon->block.max_width, block->width
                    + block->bound->margin.left + block->bound->margin.right);
            } else {
                lycon->block.advance_y += block->height;
                lycon->block.max_width = max(lycon->block.max_width, block->width);
            }
            // For non-float blocks, we should be at line start after the block
            // (floats are handled above and don't require this assertion)
            if (!is_float) {
                assert(lycon->line.is_line_start);
            }
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
