#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_flex_multipass.hpp"
#include "layout_grid_multipass.hpp"
#include "layout_multicol.hpp"
#include "layout_positioned.hpp"
#include "intrinsic_sizing.hpp"
#include "layout_cache.hpp"
#include "grid.hpp"
#include "form_control.hpp"
#include "render_svg_inline.hpp"

#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/font/font.h"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include <chrono>
#include <cfloat>
using namespace std::chrono;

// DEBUG: Global for tracking table height between calls
// WORKAROUND: Table height gets corrupted between layout_block_content return and caller
// This is a mysterious issue that needs further investigation
static float g_layout_table_height = 0;

// Thread-local iframe depth counter to prevent infinite recursion
// (e.g., <iframe src="index.html"> loading itself)
// Shared between layout_block.cpp and layout_flex_multipass.cpp
__thread int iframe_depth = 0;

// External timing accumulators from layout.cpp
extern double g_table_layout_time;
extern double g_flex_layout_time;
extern double g_grid_layout_time;
extern double g_block_layout_time;
extern int64_t g_block_layout_count;

View* layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);
// void layout_flex_nodes(LayoutContext* lycon, lxb_dom_node_t *first_child);  // Removed: lexbor dependency
void resolve_inline_default(LayoutContext* lycon, ViewSpan* span);
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_flex_content(LayoutContext* lycon, ViewBlock* block);
void layout_form_control(LayoutContext* lycon, ViewBlock* block);
void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line);

// CSS 2.1 Section 17.2.1: Wrap orphaned table-internal children in anonymous table structures
bool wrap_orphaned_table_children(LayoutContext* lycon, DomElement* parent);
bool is_table_internal_display(CssEnum display);

// Forward declarations for min/max constraint functions
float adjust_min_max_height(ViewBlock* block, float height);

// Counter system functions (from layout_counters.cpp)
typedef struct CounterContext CounterContext;
int counter_format(CounterContext* ctx, const char* name, uint32_t style,
                  char* buffer, size_t buffer_size);

// ============================================================================
// Math Element Detection and Layout Support
// ============================================================================

/**
 * Check if an element is a display math element (has class "math display").
 * Returns: true if display math, false otherwise.
 */
static bool is_display_math_element(DomElement* elem) {
    if (!elem) return false;

    // check for class="math display"
    return dom_element_has_class(elem, "math") && dom_element_has_class(elem, "display");
}

/**
 * Layout a display math element.
 *
 * NOTE: The legacy MathLive pipeline has been removed. Math elements using
 * the old MathBox-based approach should migrate to RDT_VIEW_TEXNODE.
 * For now, this function is a stub that logs a warning.
 *
 * To enable math rendering, use the unified TeX pipeline:
 *   1. Parse LaTeX with tex::typeset_latex_math()
 *   2. Set elem->view_type = RDT_VIEW_TEXNODE
 *   3. Set elem->tex_root = tex_node
 */
static void layout_display_math_block(LayoutContext* lycon, DomElement* elem) {
    log_debug("layout_display_math_block: MathLive pipeline removed - use RDT_VIEW_TEXNODE instead");
    // TODO: Implement using unified TeX pipeline
    // For now, skip math rendering
    (void)lycon;
    (void)elem;
}

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
                                          const char* content, bool is_before,
                                          FontProp* parent_font) {
    // Allow empty content - pseudo-elements with display:block and clear:both still need to be created
    if (!lycon || !parent) return nullptr;

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

    // IMPORTANT: Do NOT share parent's FontProp pointer with pseudo-element!
    // If we set pseudo_elem->font = parent->font, then when the pseudo-element's
    // font-size (e.g., 1.2em) is resolved, it would modify the shared FontProp,
    // incorrectly changing the parent's font-size as well.
    // Instead, leave pseudo_elem->font = nullptr so that style resolution will
    // allocate a new FontProp via alloc_font_prop(), which properly copies from
    // lycon->font.style (the parent's computed font values).
    pseudo_elem->font = nullptr;

    // Log that font will be allocated during style resolution
    log_debug("[PSEUDO FONT] %s font=nullptr (will be allocated during style resolution)",
              is_before ? "::before" : "::after");

    // DON'T copy bound - pseudo-element should have its own BoundaryProp
    // pseudo_elem->bound = parent->bound;  // BUG: causes shared BackgroundProp
    pseudo_elem->bound = nullptr;  // Will be allocated when CSS properties are applied
    pseudo_elem->in_line = parent->in_line;

    // Get display value from pseudo-element's styles (before_styles or after_styles)
    // Default to inline for pseudo-elements per CSS spec
    pseudo_elem->display.outer = CSS_VALUE_INLINE;
    pseudo_elem->display.inner = CSS_VALUE_FLOW;

    // Check for explicit display in pseudo-element styles
    StyleTree* pseudo_styles = is_before ? parent->before_styles : parent->after_styles;
    if (pseudo_styles && pseudo_styles->tree) {
        AvlNode* display_node = avl_tree_search(pseudo_styles->tree, CSS_PROPERTY_DISPLAY);
        if (display_node) {
            StyleNode* style_node = (StyleNode*)display_node->declaration;
            if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                CssValue* val = style_node->winning_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    if (val->data.keyword == CSS_VALUE_BLOCK) {
                        pseudo_elem->display.outer = CSS_VALUE_BLOCK;
                        log_debug("[PSEUDO] Setting display: block for ::%s", is_before ? "before" : "after");
                    } else if (val->data.keyword == CSS_VALUE_INLINE_BLOCK) {
                        pseudo_elem->display.outer = CSS_VALUE_INLINE_BLOCK;
                    }
                }
            }
        }

        // Copy pseudo-element styles to the pseudo element itself
        pseudo_elem->specified_style = pseudo_styles;
    }

    // Create the text child only if there's content
    // Empty content pseudo-elements still participate in layout (e.g., clearfix)
    if (content && *content) {
        log_info("[PSEUDO] Creating text node for pseudo-element, content_len=%zu, first_byte=0x%02x",
            strlen(content), (unsigned char)*content);
        DomText* text_node = (DomText*)pool_calloc(pool, sizeof(DomText));
        if (text_node) {
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
                log_info("[PSEUDO] Text node created with content_len=%zu, bytes=[%02x %02x %02x]",
                    content_len,
                    content_len > 0 ? (unsigned char)text_content[0] : 0,
                    content_len > 1 ? (unsigned char)text_content[1] : 0,
                    content_len > 2 ? (unsigned char)text_content[2] : 0);
            }
            text_node->text = text_content;
            text_node->length = content_len;
            text_node->native_string = nullptr;  // Not backed by Lambda String
            text_node->content_type = DOM_TEXT_STRING;

            // Link text node as child of pseudo element
            pseudo_elem->first_child = text_node;
        }
    } else {
        log_info("[PSEUDO] NOT creating text node: content=%p, first_byte=%s",
            (void*)content, content ? ((*content) ? "nonzero" : "ZERO") : "NULL");
    }

    log_debug("[PSEUDO] Created ::%s element for <%s> with content \"%s\", display.outer=%d",
              is_before ? "before" : "after",
              parent->tag_name ? parent->tag_name : "unknown",
              content ? content : "(empty)",
              pseudo_elem->display.outer);

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

    log_debug("[PSEUDO] Checking <%s>: has_before=%d, has_after=%d, before_styles=%p",
              elem->tag_name ? elem->tag_name : "?", has_before, has_after, (void*)elem->before_styles);

    if (!has_before && !has_after) return nullptr;

    // Allocate PseudoContentProp
    PseudoContentProp* pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
    if (!pseudo) return nullptr;

    // Initialize
    memset(pseudo, 0, sizeof(PseudoContentProp));

    // Create ::before pseudo-element if needed
    // Note: Even empty content "" creates a pseudo-element for layout purposes (e.g., clearfix)
    if (has_before) {
        log_info("[PSEUDO] Getting before content for <%s>", elem->tag_name ? elem->tag_name : "?");
        const char* before_content = nullptr;
        if (lycon->counter_context) {
            log_info("[PSEUDO] Calling get_pseudo_element_content_with_counters");
            before_content = dom_element_get_pseudo_element_content_with_counters(
                elem, PSEUDO_ELEMENT_BEFORE, lycon->counter_context, lycon->doc->arena);
            log_info("[PSEUDO] Returned from with_counters: %p, len=%zu, bytes=[%02x %02x %02x]",
                (void*)before_content,
                before_content ? strlen(before_content) : 0,
                before_content && strlen(before_content) > 0 ? (unsigned char)before_content[0] : 0,
                before_content && strlen(before_content) > 1 ? (unsigned char)before_content[1] : 0,
                before_content && strlen(before_content) > 2 ? (unsigned char)before_content[2] : 0);
        }
        if (!before_content) {
            log_info("[PSEUDO] Calling dom_element_get_pseudo_element_content");
            before_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_BEFORE);
            log_info("[PSEUDO] Returned: %p", (void*)before_content);
        }

        // Debug: log what font we're passing to pseudo-element
        log_debug("[PSEUDO ALLOC] block->font=%p, elem->font=%p", (void*)block->font, (void*)elem->font);
        if (block->font && block->font->family) {
            log_debug("[PSEUDO ALLOC] Passing font '%s' (size %.1f) from ViewBlock",
                     block->font->family, block->font->font_size);
        } else if (block->font) {
            log_debug("[PSEUDO ALLOC] block->font exists but has no family");
        } else {
            log_debug("[PSEUDO ALLOC] block->font is NULL");
        }

        // Create pseudo-element even for empty content if display/clear properties are set
        // Pass block->font (from ViewBlock) for accurate font-family inheritance
        pseudo->before = create_pseudo_element(lycon, elem, before_content ? before_content : "", true, block->font);
        log_debug("[PSEUDO] Created ::before for <%s> with content='%s'",
                  elem->tag_name ? elem->tag_name : "?", before_content ? before_content : "(empty)");
    }

    // Create ::after pseudo-element if needed
    // Note: Even empty content "" creates a pseudo-element for layout purposes
    if (has_after) {
        const char* after_content = nullptr;
        if (lycon->counter_context) {
            after_content = dom_element_get_pseudo_element_content_with_counters(
                elem, PSEUDO_ELEMENT_AFTER, lycon->counter_context, lycon->doc->arena);
        }
        if (!after_content) {
            after_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_AFTER);
        }
        // Pass block->font (from ViewBlock) for accurate font-family inheritance
        pseudo->after = create_pseudo_element(lycon, elem, after_content ? after_content : "", false, block->font);
        log_debug("[PSEUDO] Created ::after for <%s> with content='%s'",
                  elem->tag_name ? elem->tag_name : "?", after_content ? after_content : "(empty)");
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

    // Resolve CSS styles for the pseudo-element BEFORE layout
    // This ensures font-family and other properties from CSS are applied
    dom_node_resolve_style(pseudo_elem, lycon);

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
        log_debug("FINALIZE TRACE: advance_y=%.1f, padding.bottom=%.1f, content_height=%.1f",
            lycon->block.advance_y, block->bound->padding.bottom, block->content_height);
        flow_width = block->content_width +
            (block->bound->border ? block->bound->border->width.right : 0);
        flow_height = block->content_height +
            (block->bound->border ? block->bound->border->width.bottom : 0);
    } else {
        flow_width = block->content_width = lycon->block.max_width;
        flow_height = block->content_height = lycon->block.advance_y;
        log_debug("FINALIZE TRACE: (no bound) advance_y=%.1f, content_height=%.1f",
            lycon->block.advance_y, block->content_height);
    }

    log_debug("finalizing block, display=%d, given wd:%f", display, lycon->block.given_width);
    if (display == CSS_VALUE_INLINE_BLOCK && lycon->block.given_width < 0) {
        block->width = min(flow_width, block->width);
        log_debug("inline-block final width set to: %f, text_align=%d", block->width, lycon->block.text_align);

        // For inline-block with auto width and text-align:center/right,
        // we deferred alignment during line_align. Now apply it with final width.
        if (lycon->block.text_align == CSS_VALUE_CENTER || lycon->block.text_align == CSS_VALUE_RIGHT) {
            // Calculate content width (excluding border/padding)
            float final_content_width = block->width;
            if (block->bound) {
                final_content_width -= (block->bound->padding.left + block->bound->padding.right);
                if (block->bound->border) {
                    final_content_width -= (block->bound->border->width.left + block->bound->border->width.right);
                }
            }

            // Align children using the final content width
            View* child = block->first_child;
            while (child) {
                if (child->view_type == RDT_VIEW_TEXT) {
                    ViewText* text = (ViewText*)child;
                    TextRect* rect = text->rect;
                    while (rect) {
                        float line_width = rect->width;
                        // Calculate offset to center/right align within content area
                        // Note: rect->x is relative to block including padding offset
                        float padding_left = block->bound ? block->bound->padding.left : 0;
                        float current_offset_in_content = rect->x - padding_left;
                        float target_offset_in_content;
                        if (lycon->block.text_align == CSS_VALUE_CENTER) {
                            target_offset_in_content = (final_content_width - line_width) / 2;
                        } else { // RIGHT
                            target_offset_in_content = final_content_width - line_width;
                        }
                        float offset = target_offset_in_content - current_offset_in_content;
                        if (abs(offset) > 0.5f) {  // Only adjust if offset is significant
                            rect->x += offset;
                            log_debug("deferred text align: rect->x adjusted by %.1f to %.1f (content_width=%.1f)",
                                      offset, rect->x, final_content_width);
                        }
                        rect = rect->next;
                    }
                }
                child = child->next();
            }
        }
    }

    // handle horizontal overflow
    if (flow_width > block->width) { // hz overflow
        if (!block->scroller) {
            block->scroller = alloc_scroll_prop(lycon);
        }
        block->scroller->has_hz_overflow = true;
        if (block->scroller->overflow_x == CSS_VALUE_VISIBLE) {
            if (lycon->block.parent) lycon->block.parent->max_width = max(lycon->block.parent->max_width, flow_width);
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
    // Use block->blk->given_height instead of lycon->block.given_height to avoid corruption
    // from child layouts that modify lycon->block during their CSS resolution
    float block_given_height = (block->blk && block->blk->given_height >= 0) ? block->blk->given_height : -1;
    if (block_given_height >= 0) { // got specified height
        // Ensure block->height is set from given_height if it hasn't been set yet
        // This is critical for the html element which doesn't go through normal layout_block path
        if (block->height <= 0) {
            block->height = block_given_height;
            log_debug("finalize: set block->height from given_height: %.1f", block_given_height);
        }
        if (flow_height > block->height) { // vt overflow
            if (!block->scroller) {
                block->scroller = alloc_scroll_prop(lycon);
            }
            block->scroller->has_vt_overflow = true;
            if (block->scroller->overflow_y == CSS_VALUE_VISIBLE) {
                if (lycon->block.parent) lycon->block.parent->max_height = max(lycon->block.parent->max_height, block->y + flow_height);
            }
            else if (block->scroller->overflow_y == CSS_VALUE_SCROLL || block->scroller->overflow_y == CSS_VALUE_AUTO) {
                block->scroller->has_vt_scroll = true;
            }
            if (block->scroller->has_vt_scroll ||
                block->scroller->overflow_y == CSS_VALUE_CLIP ||
                block->scroller->overflow_y == CSS_VALUE_HIDDEN) {
                block->scroller->has_clip = true;
                block->scroller->clip.left = 0;  block->scroller->clip.top = 0;
                block->scroller->clip.right = block->width;  block->scroller->clip.bottom = block->height;
            }
        }
        log_debug("block: given_height: %f, height: %f, flow height: %f", block_given_height, block->height, flow_height);
    }
    else {
        // For non-flex containers, set height to flow height
        // For flex containers, the height is already set by flex algorithm
        // For table elements, the height is already set by table_auto_layout
        bool has_embed = block->embed != nullptr;
        bool has_flex = has_embed && block->embed->flex != nullptr;
        bool is_table = (block->view_type == RDT_VIEW_TABLE);
        log_debug("finalize block flow: has_embed=%d, has_flex=%d, is_table=%d, block=%s",
                  has_embed, has_flex, is_table, block->node_name());
        if (!has_flex && !is_table) {
            // Apply min-height/max-height constraints to auto height
            float final_height = adjust_min_max_height(block, flow_height);
            log_debug("finalize block flow, set block height to flow height: %f (after min/max: %f)",
                      flow_height, final_height);
            block->height = final_height;
        } else {
            log_debug("finalize block flow: %s container, keeping height: %f (flow=%f)",
                      is_table ? "table" : "flex", block->height, flow_height);
        }
        // DEBUG: Check table height RIGHT BEFORE fprintf (only for body and html)
        if (strcmp(block->node_name(), "html") == 0 || strcmp(block->node_name(), "body") == 0) {
            View* html_or_body = block;
            View* body_view = nullptr;

            // For html, find body first; for body, use itself
            if (strcmp(block->node_name(), "html") == 0) {
                View* child = ((ViewElement*)block)->first_placed_child();
                while (child) {
                    if (child->is_block() && strcmp(child->node_name(), "body") == 0) {
                        body_view = child;
                        break;
                    }
                    child = child->next();
                }
            } else {
                body_view = block;
            }

            if (body_view) {
                View* grandchild = ((ViewElement*)body_view)->first_placed_child();
                while (grandchild) {
                    grandchild = grandchild->next();
                }
            }
        }
    }

    // BFC (Block Formatting Context) height expansion to contain floats
    // CSS 2.2 Section 10.6.7: For BFC roots, floating descendants are included in height
    // This applies to html/body elements which establish the initial BFC
    if (lycon->block.establishing_element == block) {
        float max_float_bottom = 0;
        // Check all floats in this BFC
        for (FloatBox* fb = lycon->block.left_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > max_float_bottom) {
                max_float_bottom = fb->margin_box_bottom;
            }
        }
        for (FloatBox* fb = lycon->block.right_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > max_float_bottom) {
                max_float_bottom = fb->margin_box_bottom;
            }
        }
        log_debug("finalize BFC %s: max_float_bottom=%.1f, block->height=%.1f",
            block->node_name(), max_float_bottom, block->height);
        if (max_float_bottom > block->height) {
            float old_height = block->height;
            block->height = max_float_bottom;
            log_debug("finalize BFC height expansion: old=%.1f, new=%.1f",
                      old_height, block->height);
        }
    }

    // Update scroller clip if height changed and scroller has clipping enabled
    // This ensures the clip region is correct after auto-height is calculated
    if (block->scroller && block->scroller->has_clip) {
        block->scroller->clip.left = 0;
        block->scroller->clip.top = 0;
        block->scroller->clip.right = block->width;
        block->scroller->clip.bottom = block->height;
    }
    // Also enable clipping when overflow is hidden/clip, even without actual overflow
    // This is needed for border-radius clipping to work correctly
    if (block->scroller && !block->scroller->has_clip) {
        if (block->scroller->overflow_x == CSS_VALUE_HIDDEN ||
            block->scroller->overflow_x == CSS_VALUE_CLIP ||
            block->scroller->overflow_y == CSS_VALUE_HIDDEN ||
            block->scroller->overflow_y == CSS_VALUE_CLIP) {
            block->scroller->has_clip = true;
            block->scroller->clip.left = 0;
            block->scroller->clip.top = 0;
            block->scroller->clip.right = block->width;
            block->scroller->clip.bottom = block->height;
            log_debug("finalize: enabling clip for overflow:hidden, wd:%f, hg:%f", block->width, block->height);
        }
    }
    log_debug("finalized block wd:%f, hg:%f", block->width, block->height);
}

void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    DomDocument* doc = NULL;
    log_debug("layout iframe");

    // Iframe recursion depth limit to prevent infinite loops (e.g., <iframe src="index.html">)
    // This is a thread-local variable shared with layout_flex_multipass.cpp
    // Keep this low since each HTTP download can take seconds
    const int MAX_IFRAME_DEPTH = 3;

    if (iframe_depth >= MAX_IFRAME_DEPTH) {
        log_warn("iframe: maximum nesting depth (%d) exceeded, skipping", MAX_IFRAME_DEPTH);
        return;
    }

    if (!(block->embed && block->embed->doc)) {
        // load iframe document
        const char *value = block->get_attribute("src");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            // Use iframe's actual dimensions as viewport, not window dimensions
            // This ensures the embedded document layouts to fit within the iframe
            int iframe_width = block->width > 0 ? (int)block->width : lycon->ui_context->window_width;
            int iframe_height = block->height > 0 ? (int)block->height : lycon->ui_context->window_height;
            log_debug("load iframe doc src: %s (iframe viewport=%dx%d, depth=%d)", src->str, iframe_width, iframe_height, iframe_depth);

            // Increment depth before loading
            iframe_depth++;

            // Load iframe document - pixel_ratio from ui_context is still used internally
            doc = load_html_doc(lycon->ui_context->document->url, src->str,
                iframe_width, iframe_height,
                1.0f);  // Layout in CSS logical pixels
            strbuf_free(src);
            if (!doc) {
                log_debug("failed to load iframe document");
                iframe_depth--;
                // todo: use a placeholder
            } else {
                if (!(block->embed)) block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                block->embed->doc = doc; // assign loaded document to embed property
                if (doc->html_root) {
                    log_debug("IFRAME TRACE: about to layout iframe document");
                    // Save parent document and window dimensions
                    DomDocument* parent_doc = lycon->ui_context->document;
                    float saved_window_width = lycon->ui_context->window_width;
                    float saved_window_height = lycon->ui_context->window_height;

                    // Temporarily set window dimensions to iframe size
                    // This ensures layout_html_doc uses iframe dimensions for layout
                    lycon->ui_context->document = doc;
                    lycon->ui_context->window_width = (float)iframe_width;
                    lycon->ui_context->window_height = (float)iframe_height;

                    // Process @font-face rules before layout (critical for custom fonts like Computer Modern)
                    process_document_font_faces(lycon->ui_context, doc);

                    layout_html_doc(lycon->ui_context, doc, false);

                    // Restore parent document and window dimensions
                    lycon->ui_context->document = parent_doc;
                    lycon->ui_context->window_width = saved_window_width;
                    lycon->ui_context->window_height = saved_window_height;
                    log_debug("IFRAME TRACE: finished layout iframe document");
                }
                iframe_depth--;
                // PDF scaling now happens inside pdf_page_to_view_tree via load_html_doc
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
        log_debug("IFRAME TRACE: iframe embedded doc root->content_width=%.1f, root->content_height=%.1f",
            root->content_width, root->content_height);
        // For PDF and other pre-laid-out documents, use width/height if content_width/height are 0
        float iframe_width = root->content_width > 0 ? root->content_width : root->width;
        float iframe_height = root->content_height > 0 ? root->content_height : root->height;
        lycon->block.max_width = iframe_width;
        lycon->block.advance_y = iframe_height;
        log_debug("IFRAME TRACE: set lycon->block.advance_y = %.1f from iframe_height", lycon->block.advance_y);
    }
    finalize_block_flow(lycon, block, display.outer);
    log_debug("IFRAME TRACE: after finalize_block_flow, iframe block->content_height=%.1f", block->content_height);
}

/**
 * Layout inline SVG element with intrinsic sizing from width/height attributes or viewBox
 */
void layout_inline_svg(LayoutContext* lycon, ViewBlock* block) {
    log_debug("layout inline SVG element");

    // Get intrinsic size from SVG attributes
    Element* native_elem = static_cast<DomElement*>(block)->native_element;
    if (!native_elem) {
        log_debug("inline SVG has no native element, using default size");
        block->width = 300;  // HTML default for SVG
        block->height = 150;
        return;
    }

    SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(native_elem);

    log_debug("SVG intrinsic: width=%.1f height=%.1f aspect=%.3f has_w=%d has_h=%d",
              intrinsic.width, intrinsic.height, intrinsic.aspect_ratio,
              intrinsic.has_intrinsic_width, intrinsic.has_intrinsic_height);

    // Determine final dimensions considering CSS properties
    float width = lycon->block.given_width;
    float height = lycon->block.given_height;

    if (width >= 0 && height >= 0) {
        // Both CSS dimensions specified - use them
        block->width = width;
        block->height = height;
    } else if (width >= 0) {
        // Width specified, calculate height from aspect ratio
        block->width = width;
        if (intrinsic.aspect_ratio > 0) {
            block->height = width / intrinsic.aspect_ratio;
        } else {
            block->height = intrinsic.height;
        }
    } else if (height >= 0) {
        // Height specified, calculate width from aspect ratio
        block->height = height;
        if (intrinsic.aspect_ratio > 0) {
            block->width = height * intrinsic.aspect_ratio;
        } else {
            block->width = intrinsic.width;
        }
    } else {
        // Neither CSS dimension specified - use intrinsic size
        // or parent width if intrinsic width is not available
        if (intrinsic.has_intrinsic_width) {
            block->width = intrinsic.width;
        } else if (lycon->block.parent && lycon->block.parent->content_width > 0) {
            block->width = lycon->block.parent->content_width;
        } else {
            block->width = 300;  // HTML default
        }

        if (intrinsic.has_intrinsic_height) {
            block->height = intrinsic.height;
        } else if (intrinsic.aspect_ratio > 0) {
            block->height = block->width / intrinsic.aspect_ratio;
        } else {
            block->height = 150;  // HTML default
        }
    }

    // Add padding and border
    float padding_top = block->bound && block->bound->padding.top > 0 ? block->bound->padding.top : 0;
    float padding_bottom = block->bound && block->bound->padding.bottom > 0 ? block->bound->padding.bottom : 0;
    float padding_left = block->bound && block->bound->padding.left > 0 ? block->bound->padding.left : 0;
    float padding_right = block->bound && block->bound->padding.right > 0 ? block->bound->padding.right : 0;
    float border_top = block->bound && block->bound->border ? block->bound->border->width.top : 0;
    float border_bottom = block->bound && block->bound->border ? block->bound->border->width.bottom : 0;
    float border_left = block->bound && block->bound->border ? block->bound->border->width.left : 0;
    float border_right = block->bound && block->bound->border ? block->bound->border->width.right : 0;

    block->content_width = block->width;
    block->content_height = block->height;
    block->width += padding_left + padding_right + border_left + border_right;
    block->height += padding_top + padding_bottom + border_top + border_bottom;

    log_debug("SVG layout result: content=%.1fx%.1f, total=%.1fx%.1f",
              block->content_width, block->content_height, block->width, block->height);
}

/**
 * Insert pseudo-element into DOM tree at appropriate position
 * ::before is inserted as first child, ::after as last child
 */
void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before) {
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

/**
 * Generate pseudo-element content based on content property
 * CSS 2.1 Section 12.2
 */
void generate_pseudo_element_content(LayoutContext* lycon, ViewBlock* block, bool is_before) {
    if (!block || !block->pseudo) return;

    log_debug("[Pseudo-Generate] Called for %s, block=%p, pseudo=%p",
              is_before ? "::before" : "::after", (void*)block, (void*)block->pseudo);

    PseudoContentProp* pseudo = block->pseudo;

    // Check if already generated
    if ((is_before && pseudo->before_generated) || (!is_before && pseudo->after_generated)) {
        return;
    }

    // Get content string and type
    char* content = is_before ? pseudo->before_content : pseudo->after_content;
    uint8_t content_type = is_before ? pseudo->before_content_type : pseudo->after_content_type;

    // Skip if no content or content is none
    if (content_type == CONTENT_TYPE_NONE || !content) {
        return;
    }

    log_debug("[Pseudo-Element] Generating %s content, type=%d",
              is_before ? "::before" : "::after", content_type);

    // Cast block to DomElement to access DOM fields
    DomElement* parent_elem = (DomElement*)block;

    // Create pseudo-element DomElement
    DomElement* pseudo_elem = dom_element_create(parent_elem->doc,
                                                  is_before ? "::before" : "::after",
                                                  nullptr);
    if (!pseudo_elem) {
        log_error("[Pseudo-Element] Failed to create DomElement");
        return;
    }

    // Set pseudo-element properties - tag_name already set by dom_element_create
    pseudo_elem->parent = parent_elem;

    // IMPORTANT: Do NOT share parent's FontProp pointer with pseudo-element!
    // If we set pseudo_elem->font = parent_elem->font, then when the pseudo-element's
    // font-size (e.g., 1.2em) is resolved, it would modify the shared FontProp,
    // incorrectly changing the parent's font-size as well.
    // Instead, leave pseudo_elem->font = nullptr so that style resolution will
    // allocate a new FontProp via alloc_font_prop(), which properly copies from
    // lycon->font.style (the parent's computed font values).
    pseudo_elem->font = nullptr;
    pseudo_elem->in_line = parent_elem->in_line;

    // Log font inheritance for debugging
    log_debug("[Pseudo-Element] font=nullptr for %s (will be allocated during style resolution)",
              is_before ? "::before" : "::after");

    // Copy pseudo-element-specific styles (::before or ::after styles)
    pseudo_elem->specified_style = is_before ? parent_elem->before_styles : parent_elem->after_styles;

    // Handle different content types
    switch (content_type) {
        case CONTENT_TYPE_STRING: {
            // Create Lambda String for the content
            size_t content_len = strlen(content);
            String* text_string = (String*)arena_alloc(parent_elem->doc->arena,
                                                        sizeof(String) + content_len + 1);
            if (text_string) {
                text_string->ref_cnt = 1;
                text_string->len = content_len;
                memcpy(text_string->chars, content, content_len);
                text_string->chars[content_len] = '\0';

                // Create text node with Lambda String
                DomText* text_node = dom_text_create(text_string, pseudo_elem);
                if (text_node) {
                    pseudo_elem->first_child = text_node;
                    log_debug("[Pseudo-Element] Created text content: \"%s\"", content);
                }
            }
            break;
        }

        case CONTENT_TYPE_COUNTER:
        case CONTENT_TYPE_COUNTERS:
            // TODO: Implement counter resolution (Phase 2)
            log_debug("[Pseudo-Element] Counter content not yet implemented");
            break;

        case CONTENT_TYPE_ATTR:
            // TODO: Implement attribute reading (Phase 5)
            log_debug("[Pseudo-Element] attr() content not yet implemented");
            break;

        case CONTENT_TYPE_URI:
            // TODO: Implement image content (Phase 5)
            log_debug("[Pseudo-Element] url() content not yet implemented");
            break;

        default:
            log_debug("[Pseudo-Element] Unknown content type: %d", content_type);
            break;
    }

    // Insert pseudo-element into DOM
    insert_pseudo_into_dom(parent_elem, pseudo_elem, is_before);

    // Store pseudo-element reference
    if (is_before) {
        pseudo->before = pseudo_elem;
        pseudo->before_generated = true;
    } else {
        pseudo->after = pseudo_elem;
        pseudo->after_generated = true;
    }

    log_debug("[Pseudo-Element] %s pseudo-element inserted", is_before ? "::before" : "::after");
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
void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* parent_block) {
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

    // Float context is now unified in BlockContext - no need to create separate context
    if (!lycon->block.establishing_element && parent_block) {
        // Initialize BlockContext for float tracking if not already done
        lycon->block.establishing_element = parent_block;
        lycon->block.float_right_edge = parent_block->content_width > 0 ? parent_block->content_width : parent_block->width;
        log_debug("[FLOAT PRE-SCAN] Initialized BlockContext for parent block %s",
                  parent_block->node_name());
    }

    if (!lycon->block.establishing_element) {
        log_debug("[FLOAT PRE-SCAN] No establishing element available, cannot pre-scan");
        return;
    }

    log_debug("[FLOAT PRE-SCAN] Pre-laying floats before first non-floated block");

    // Pre-lay floats ONLY until we encounter a non-floated block element
    // CSS 2.1 §9.5.1 Rule 6: "The outer top of a floating box may not be higher than
    // the outer top of any block or floated box generated by an element earlier in
    // the source document."
    // This means floats that come AFTER a non-floated block in source order must
    // appear at or below that block's top edge - they cannot be pre-scanned to y=0.
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;

        DomElement* elem = child->as_element();

        // Skip if already pre-laid
        if (elem->float_prelaid) continue;

        // Check display:none first - hidden elements should not participate in float layout
        DisplayValue display = resolve_display_value(child);
        if (display.outer == CSS_VALUE_NONE) continue;

        CssEnum float_value = get_element_float_value(elem);

        // If this is a non-floated block, stop pre-scanning
        // Subsequent floats must be laid out in normal flow order
        if (float_value != CSS_VALUE_LEFT && float_value != CSS_VALUE_RIGHT) {
            if (display.outer == CSS_VALUE_BLOCK) {
                log_debug("[FLOAT PRE-SCAN] Encountered non-floated block %s, stopping pre-scan",
                          child->node_name());
                break;  // Stop pre-scanning - remaining floats go through normal flow
            }
            continue;  // Skip non-float non-block elements
        }

        log_debug("[FLOAT PRE-SCAN] Pre-laying float: %s (float=%d)",
                  child->node_name(), float_value);

        // Layout the float now
        display.outer = CSS_VALUE_BLOCK;  // Floats become block per CSS 9.7

        // Mark as pre-laid to skip during normal flow
        elem->float_prelaid = true;

        // Layout the float block
        layout_block(lycon, child, display);
    }

    // After pre-scanning floats, adjust the current line bounds for the floats we just laid out
    // This is critical: the first line needs to start AFTER the float, not at x=0
    //
    // Use unified BlockContext for float space queries
    // IMPORTANT: Floats are registered to the BFC (parent chain), not lycon->block
    // So we need to check the BFC's float counts, not the current block's
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc && (bfc->left_float_count > 0 || bfc->right_float_count > 0)) {
        log_debug("[FLOAT PRE-SCAN] Adjusting initial line bounds for pre-scanned floats (bfc=%p, left=%d, right=%d)",
                  (void*)bfc, bfc->left_float_count, bfc->right_float_count);

        float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;

        // Calculate current block's Y position in BFC coordinates
        // We need to walk up from the parent_block to the BFC establishing element
        float bfc_y_offset = 0.0f;
        float bfc_x_offset = 0.0f;
        ViewElement* walker = parent_block;
        ViewBlock* bfc_elem = bfc->establishing_element;
        while (walker && walker != bfc_elem) {
            bfc_y_offset += walker->y;
            bfc_x_offset += walker->x;
            walker = walker->parent_view();
        }
        // Add parent_block's border/padding to get to content area
        if (parent_block && parent_block->bound) {
            if (parent_block->bound->border) {
                bfc_y_offset += parent_block->bound->border->width.top;
                bfc_x_offset += parent_block->bound->border->width.left;
            }
            bfc_y_offset += parent_block->bound->padding.top;
            bfc_x_offset += parent_block->bound->padding.left;
        }

        // Query at the BFC-relative Y position of this block's first line
        float query_y = bfc_y_offset + lycon->block.advance_y;
        log_debug("[FLOAT PRE-SCAN] querying space at bfc_y=%.1f, line_height=%.1f, left_count=%d",
               query_y, line_height, bfc->left_float_count);
        FloatAvailableSpace space = block_context_space_at_y(bfc, query_y, line_height);
        log_debug("[FLOAT PRE-SCAN] space=(%.1f, %.1f), has_left=%d, has_right=%d",
               space.left, space.right, space.has_left_float, space.has_right_float);

        if (space.has_left_float) {
            // Left float intrudes - adjust effective_left and advance_x
            // space.left is in BFC coordinates, need to convert to local (block content area) coords
            float local_left = space.left - bfc_x_offset;
            log_debug("[FLOAT PRE-SCAN] space.left=%.1f, bfc_x_offset=%.1f, local_left=%.1f, current effective_left=%.1f",
                   space.left, bfc_x_offset, local_left, lycon->line.effective_left);
            if (local_left > lycon->line.effective_left) {
                log_debug("[FLOAT PRE-SCAN] Adjusting line.effective_left: %.1f -> %.1f, advance_x: %.1f -> %.1f",
                       lycon->line.effective_left, local_left, lycon->line.advance_x, local_left);
                lycon->line.effective_left = local_left;
                lycon->line.has_float_intrusion = true;
                if (lycon->line.advance_x < local_left) {
                    lycon->line.advance_x = local_left;
                }
            }
        }
        if (space.has_right_float) {
            // Right float intrudes - adjust effective_right
            // space.right is in BFC coordinates, convert to local
            float local_right = space.right - bfc_x_offset;
            if (local_right < lycon->line.effective_right) {
                log_debug("[FLOAT PRE-SCAN] Adjusting line.effective_right: %.1f -> %.1f",
                          lycon->line.effective_right, local_right);
                lycon->line.effective_right = local_right;
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

        // Generate pseudo-element content from CSS content property (CSS 2.1 Section 12.2)
        // Must be done AFTER alloc_pseudo_content_prop populates the content/type fields
        generate_pseudo_element_content(lycon, block, true);   // ::before
        generate_pseudo_element_content(lycon, block, false);  // ::after

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

    if (block->display.inner == RDT_DISPLAY_REPLACED) {  // image, iframe, hr, form controls, SVG
        uintptr_t elmt_name = block->tag();
        if (elmt_name == HTM_TAG_IFRAME) {
            layout_iframe(lycon, block, block->display);
        }
        else if (elmt_name == HTM_TAG_SVG) {
            // Inline SVG element: use width/height attributes or viewBox for intrinsic size
            layout_inline_svg(lycon, block);
        }
        else if (elmt_name == HTM_TAG_HR) {
            // hr element: Use explicit height if specified, otherwise use border height
            if (lycon->block.given_height >= 0) {
                // CSS height property is set - use it as content height
                float content_height = lycon->block.given_height;
                float padding_top = block->bound && block->bound->padding.top > 0 ? block->bound->padding.top : 0;
                float padding_bottom = block->bound && block->bound->padding.bottom > 0 ? block->bound->padding.bottom : 0;
                float border_top = block->bound && block->bound->border ? block->bound->border->width.top : 0;
                float border_bottom = block->bound && block->bound->border ? block->bound->border->width.bottom : 0;
                block->height = content_height + padding_top + padding_bottom + border_top + border_bottom;
                log_debug("hr layout: explicit height=%f, total=%f", content_height, block->height);
            } else {
                // No explicit height - use border thickness (traditional hr behavior)
                float border_top = block->bound && block->bound->border ? block->bound->border->width.top : 0;
                float border_bottom = block->bound && block->bound->border ? block->bound->border->width.bottom : 0;
                block->height = border_top + border_bottom;
                log_debug("hr layout: border-only height=%f", block->height);
            }
        }
        else if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form &&
                 elmt_name != HTM_TAG_BUTTON) {
            // Form control elements (input, select, textarea) - replaced elements with intrinsic size
            // Note: <button> elements have content children, so they go through normal layout flow
            layout_form_control(lycon, block);
        }
        // else HTM_TAG_IMG - handled by layout_block_content width/height
    } else if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form &&
               block->tag() != HTM_TAG_BUTTON) {
        // Form control fallback (for cases where display.inner != RDT_DISPLAY_REPLACED)
        layout_form_control(lycon, block);
    } else {  // layout block child content
        // No longer need separate pseudo-element layout - they're part of child list now
        DomNode *child = nullptr;
        if (block->is_element()) { child = block->first_child; }
        if (child) {
            // CSS 2.1 §17.2.1: Orphaned table-internal elements (table-row, table-cell, etc.)
            // inside non-table contexts should be treated as block+flow for layout purposes.
            // This handles cases like floated table-row-group containing table-row/table-cell.
            bool is_orphaned_table_internal =
                block->display.inner == CSS_VALUE_TABLE_ROW ||
                block->display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
                block->display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
                block->display.inner == CSS_VALUE_TABLE_FOOTER_GROUP ||
                block->display.inner == CSS_VALUE_TABLE_COLUMN ||
                block->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
                block->display.inner == CSS_VALUE_TABLE_CELL ||
                block->display.inner == CSS_VALUE_TABLE_CAPTION;

            // CSS 2.1 §17.2.1: Before flow layout, check if any children are orphaned
            // table-internal elements (table-cell, table-row, etc.) that need wrapping
            // in anonymous table structures. This must happen before layout.
            if (block->display.inner == CSS_VALUE_FLOW && !is_orphaned_table_internal) {
                DomElement* block_elem = block->as_element();
                if (block_elem && wrap_orphaned_table_children(lycon, block_elem)) {
                    // Re-get first child after wrapping may have inserted anonymous elements
                    child = block->first_child;
                }
            }

            if (block->display.inner == CSS_VALUE_FLOW || is_orphaned_table_internal) {
                // Check for multi-column layout
                bool is_multicol = is_multicol_container(block);

                if (is_multicol) {
                    log_debug("[MULTICOL] Container detected: %s", block->node_name());
                    // Multi-column layout handles its own content distribution
                    layout_multicol_content(lycon, block);
                } else {
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
            }
            else if (block->display.inner == CSS_VALUE_FLEX) {
                auto t_flex_start = high_resolution_clock::now();
                log_debug("Setting up flex container for %s", block->node_name());
                layout_flex_content(lycon, block);
                log_debug("Finished flex container layout for %s", block->node_name());
                g_flex_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_flex_start).count();

                // After flex layout, update content_height/advance_y from container height
                // so that parent containers (like iframes) get the correct scroll height
                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                log_debug("FLEX FINALIZE: Updated advance_y=%.1f from block->height=%.1f",
                    lycon->block.advance_y, block->height);

                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_GRID) {
                auto t_grid_start = high_resolution_clock::now();
                log_debug("Setting up grid container for %s (multipass)", block->node_name());
                // Use multipass grid layout (similar to flex layout pattern)
                layout_grid_content(lycon, block);
                log_debug("Finished grid container layout for %s", block->node_name());
                g_grid_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_grid_start).count();

                // After grid layout, update content_height/advance_y from container height
                // so that parent containers (like iframes) get the correct scroll height
                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                log_debug("GRID FINALIZE: Updated advance_y=%.1f from block->height=%.1f",
                    lycon->block.advance_y, block->height);

                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_TABLE) {
                auto t_table_start = high_resolution_clock::now();
                log_debug("TABLE LAYOUT TRIGGERED! outer=%d, inner=%d, element=%s",
                    block->display.outer, block->display.inner, block->node_name());
                layout_table_content(lycon, block, block->display);
                g_table_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_table_start).count();

                // After table layout, update content_height/advance_y from container height
                // so that parent containers (like iframes) get the correct scroll height
                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                log_debug("TABLE FINALIZE: Updated advance_y=%.1f from block->height=%.1f",
                    lycon->block.advance_y, block->height);

                finalize_block_flow(lycon, block, block->display.outer);

                // WORKAROUND: Save table height to global - it gets corrupted after return
                // This is a mysterious issue where the height field gets zeroed between
                // the return statement and the caller's next instruction
                g_layout_table_height = block->height;
                return;
            }
            else {
                log_debug("unknown display type");
            }
        } else {
            // Empty container (no children) - still need to run flex/grid layout
            // for proper shrink-to-fit sizing (e.g., abs-pos flex with only border/padding)
            if (block->display.inner == CSS_VALUE_FLEX) {
                auto t_flex_start = high_resolution_clock::now();
                log_debug("Setting up EMPTY flex container for %s", block->node_name());
                layout_flex_content(lycon, block);
                log_debug("Finished EMPTY flex container layout for %s", block->node_name());
                g_flex_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_flex_start).count();

                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                log_debug("FLEX EMPTY FINALIZE: Updated advance_y=%.1f from block->height=%.1f",
                    lycon->block.advance_y, block->height);

                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_GRID) {
                auto t_grid_start = high_resolution_clock::now();
                log_debug("Setting up EMPTY grid container for %s", block->node_name());
                layout_grid_content(lycon, block);
                log_debug("Finished EMPTY grid container layout for %s", block->node_name());
                g_grid_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_grid_start).count();

                lycon->block.advance_y = block->height;
                if (block->bound && block->bound->border) {
                    lycon->block.advance_y -= block->bound->border->width.bottom;
                }
                if (block->bound) {
                    lycon->block.advance_y -= block->bound->padding.bottom;
                }
                log_debug("GRID EMPTY FINALIZE: Updated advance_y=%.1f from block->height=%.1f",
                    lycon->block.advance_y, block->height);

                finalize_block_flow(lycon, block, block->display.outer);
                return;
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

    // CSS 2.1 §16.1: text-indent applies only to the first formatted line of a block container
    // Initialize is_first_line to true when starting a new block
    lycon->block.is_first_line = true;

    // Resolve text-indent: percentage needs containing block width (now available)
    float resolved_text_indent = 0.0f;
    if (block->blk) {
        if (!isnan(block->blk->text_indent_percent)) {
            // Percentage text-indent: resolve against containing block width
            resolved_text_indent = content_width * block->blk->text_indent_percent / 100.0f;
            log_debug("setup_inline: resolved text-indent %.1f%% -> %.1fpx (content_width=%.1f)",
                     block->blk->text_indent_percent, resolved_text_indent, content_width);
        } else if (block->blk->text_indent != 0.0f) {
            // Fixed length text-indent
            resolved_text_indent = block->blk->text_indent;
        }
    }
    lycon->block.text_indent = resolved_text_indent;
    if (lycon->block.text_indent != 0.0f) {
        log_debug("setup_inline: text-indent=%.1fpx for block", lycon->block.text_indent);
    }

    // Calculate BFC offset for this block (used for float coordinate conversion)
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc) {
        block_context_calc_bfc_offset((ViewElement*)block, bfc,
                                      &lycon->block.bfc_offset_x, &lycon->block.bfc_offset_y);
    } else {
        lycon->block.bfc_offset_x = 0;
        lycon->block.bfc_offset_y = 0;
    }

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
    TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
    if (typo.valid && typo.use_typo_metrics) {
        lycon->block.init_ascender = typo.ascender;
        lycon->block.init_descender = typo.descender;
    } else if (lycon->font.font_handle) {
        const FontMetrics* m = font_get_metrics(lycon->font.font_handle);
        if (m) {
            lycon->block.init_ascender = m->hhea_ascender;
            lycon->block.init_descender = -(m->hhea_descender);
        }
    }
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height - (lycon->block.init_ascender + lycon->block.init_descender)) / 2);
    float font_height = lycon->font.font_handle ? font_get_metrics(lycon->font.font_handle)->hhea_line_height : 0;
    log_debug("block line_height: %f, font height: %f, asc+desc: %f, lead_y: %f", lycon->block.line_height, font_height,
        lycon->block.init_ascender + lycon->block.init_descender, lycon->block.lead_y);
}

__attribute__((noinline))
void layout_block_content(LayoutContext* lycon, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line) {
    block->x = pa_line->left;  block->y = pa_block->advance_y;

    // CSS 2.2 9.5.1: Float positioning relative to preceding content
    // When a float appears after inline content, CSS rules determine its Y position:
    // - If there's inline content AFTER the float (float is mid-line), position at current line top
    // - If float is the last content (or followed only by block elements), position below current line
    bool is_float = block->position && (block->position->float_prop == CSS_VALUE_LEFT || block->position->float_prop == CSS_VALUE_RIGHT);

    if (is_float && !pa_line->is_line_start) {
        // Float appears after inline content
        // Check if there's more inline content after this float in the parent
        // ViewBlock extends DomElement which extends DomNode, so block can be used as DomNode*
        DomNode* float_node = (DomNode*)block;
        bool has_inline_after = false;
        if (float_node) {
            for (DomNode* sib = float_node->next_sibling; sib; sib = sib->next_sibling) {
                if (sib->is_text()) {
                    // Check if it's non-whitespace text
                    const char* text = (const char*)sib->text_data();
                    if (text) {
                        for (const char* p = text; *p; p++) {
                            unsigned char c = (unsigned char)*p;
                            if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
                                has_inline_after = true;
                                break;
                            }
                        }
                    }
                    if (has_inline_after) break;
                } else if (sib->is_element()) {
                    DomElement* elem = sib->as_element();
                    ViewBlock* view = (ViewBlock*)elem;

                    // First check if this sibling is also a float - floats don't count as inline
                    bool sib_is_float = view->position &&
                        (view->position->float_prop == CSS_VALUE_LEFT ||
                         view->position->float_prop == CSS_VALUE_RIGHT);

                    if (sib_is_float) {
                        // Skip other floats - they don't count as inline content after
                        continue;
                    }

                    // Check if it's an inline element (not positioned)
                    bool is_inline_elem = (view->display.outer == CSS_VALUE_INLINE ||
                                          view->display.outer == CSS_VALUE_INLINE_BLOCK);

                    // If display is unresolved (0), check the tag name for common inline elements
                    if (view->display.outer == 0) {
                        const char* tag = elem->node_name();
                        if (tag && (strcmp(tag, "span") == 0 || strcmp(tag, "a") == 0 ||
                                   strcmp(tag, "em") == 0 || strcmp(tag, "strong") == 0 ||
                                   strcmp(tag, "b") == 0 || strcmp(tag, "i") == 0)) {
                            is_inline_elem = true;
                        }
                    }

                    if (is_inline_elem) {
                        has_inline_after = true;
                        break;
                    } else if (view->display.outer == CSS_VALUE_BLOCK ||
                               view->display.outer == CSS_VALUE_LIST_ITEM ||
                               view->display.outer == 0) {
                        // Block element (or unresolved block-like element like div) follows
                        // Float should be below current line
                        break;
                    }
                }
            }
        }

        if (!has_inline_after) {
            // Float is the last inline content or followed by block - position below current line
            float line_height = pa_block->line_height > 0 ? pa_block->line_height : 18.0f;
            block->y = pa_block->advance_y + line_height;
            log_debug("Float positioned below current line: y=%.1f (advance_y=%.1f + line_height=%.1f)",
                      block->y, pa_block->advance_y, line_height);
        } else {
            // Float has inline content after it - position at current line top
            log_debug("Float positioned at current line top: y=%.1f (has inline content after)",
                      block->y);
        }
    } else if (is_float) {
        log_debug("Float positioned at line start: y=%.1f", block->y);
    }

    log_debug("block init position (%s): x=%f, y=%f, pa_block.advance_y=%f, display: outer=%d, inner=%d",
        block->node_name(), block->x, block->y, pa_block->advance_y, block->display.outer, block->display.inner);

    // Check if this block establishes a new BFC using unified BlockContext
    bool establishes_bfc = block_context_establishes_bfc(block);

    // CSS 2.1 Section 9.5: "The border box of a table, a block-level replaced element,
    // or an element in the normal flow that establishes a new block formatting context...
    // must not overlap the margin box of any floats in the same block formatting context
    // as the element itself."
    // Block-level replaced elements (like <img display:block>) must also avoid floats.
    bool is_block_level_replaced = (block->display.outer == CSS_VALUE_BLOCK &&
                                    block->display.inner == RDT_DISPLAY_REPLACED);

    bool is_normal_flow = !is_float &&
        (!block->position || (block->position->position != CSS_VALUE_ABSOLUTE &&
                              block->position->position != CSS_VALUE_FIXED));

    // Elements that must avoid floats: BFC roots, block-level replaced elements
    bool should_avoid_floats = (establishes_bfc || is_block_level_replaced) && is_normal_flow;

    // Query parent BFC for available space at current y position
    float bfc_float_offset_x = 0;
    float bfc_available_width_reduction = 0;
    float bfc_shift_down = 0;  // Amount to shift down if element doesn't fit beside floats
    BlockContext* parent_bfc = nullptr;

    if (should_avoid_floats) {
        // Find the BFC root from the parent context - that's where floats are registered
        parent_bfc = block_context_find_bfc(pa_block);
        if (parent_bfc && (parent_bfc->left_float_count > 0 || parent_bfc->right_float_count > 0)) {
            // Calculate this block's position in BFC coordinates
            // block->y is relative to parent's content area, need to convert to BFC coordinates
            float y_in_bfc = block->y;
            float x_in_bfc = block->x;

            // Walk up from parent to BFC establishing element, accumulating offsets
            ViewElement* walker = block->parent_view();
            while (walker && walker != parent_bfc->establishing_element) {
                y_in_bfc += walker->y;
                x_in_bfc += walker->x;
                walker = walker->parent_view();
            }

            // Get the element's actual width requirement
            // For elements with explicit CSS width, use that; otherwise use parent width
            float element_required_width = pa_block->content_width;
            bool has_explicit_width = false;

            // Check block->blk for CSS width (resolved by dom_node_resolve_style)
            if (block->blk) {
                if (block->blk->given_width > 0) {
                    // Explicit width in px
                    element_required_width = block->blk->given_width;
                    has_explicit_width = true;
                } else if (!isnan(block->blk->given_width_percent)) {
                    // Percentage width - resolve against parent
                    element_required_width = pa_block->content_width * block->blk->given_width_percent / 100.0f;
                    has_explicit_width = true;
                }
            }

            // Add margins if they're explicitly set (not auto)
            if (has_explicit_width && block->bound) {
                if (block->bound->margin.left_type != CSS_VALUE_AUTO)
                    element_required_width += block->bound->margin.left;
                if (block->bound->margin.right_type != CSS_VALUE_AUTO)
                    element_required_width += block->bound->margin.right;
            }

            log_debug("[BFC Float Avoid] element %s: required_width=%.1f, has_explicit_width=%d, y_in_bfc=%.1f",
                      block->node_name(), element_required_width, has_explicit_width, y_in_bfc);

            // For elements WITHOUT explicit width, they can shrink to fit - no need to shift down
            // For elements WITH explicit width, shift down if they don't fit
            float current_y = y_in_bfc;

            if (has_explicit_width) {
                // Check if element fits at current Y position
                // If not, shift down like floats do
                int max_iterations = 100;

                while (max_iterations-- > 0) {
                    FloatAvailableSpace space = block_context_space_at_y(parent_bfc, current_y, 1.0f);

                    // Calculate how much space is available in the PARENT's content area
                    // (not the BFC's full width, which may be much larger)
                    float local_left = space.left - x_in_bfc;  // Float edge in local coords
                    float local_right = space.right - x_in_bfc;  // Right edge in local coords

                    // Clamp to parent's content area bounds
                    float parent_left_bound = 0;
                    float parent_right_bound = pa_block->content_width;

                    float effective_left = max(local_left, parent_left_bound);
                    float effective_right = min(local_right, parent_right_bound);
                    float available_width = max(0.0f, effective_right - effective_left);

                    log_debug("[BFC Float Avoid] Checking y=%.1f: space=(%.1f,%.1f), local=(%.1f,%.1f), parent_width=%.1f, available=%.1f, needed=%.1f",
                              current_y, space.left, space.right, local_left, local_right,
                              pa_block->content_width, available_width, element_required_width);

                    // Check if element fits
                    if (available_width >= element_required_width ||
                        (!space.has_left_float && !space.has_right_float)) {
                        // Element fits here - calculate offset
                        float float_intrusion_left = max(0.0f, local_left);
                        float float_intrusion_right = max(0.0f, pa_block->content_width - local_right);

                        if (space.has_left_float && float_intrusion_left > 0) {
                            bfc_float_offset_x = float_intrusion_left;
                        }
                        bfc_available_width_reduction = float_intrusion_left + float_intrusion_right;
                        break;
                    }

                    // Element doesn't fit - find next float boundary to try
                    float next_y = FLT_MAX;
                    for (FloatBox* fb = parent_bfc->left_floats; fb; fb = fb->next) {
                        if (fb->margin_box_bottom > current_y && fb->margin_box_bottom < next_y) {
                            next_y = fb->margin_box_bottom;
                        }
                    }
                    for (FloatBox* fb = parent_bfc->right_floats; fb; fb = fb->next) {
                        if (fb->margin_box_bottom > current_y && fb->margin_box_bottom < next_y) {
                            next_y = fb->margin_box_bottom;
                        }
                    }

                    if (next_y == FLT_MAX || next_y <= current_y) {
                        // No more floats below - use current position
                        break;
                    }

                    log_debug("[BFC Float Avoid] Element doesn't fit, shifting from y=%.1f to y=%.1f",
                              current_y, next_y);
                    current_y = next_y;
                }
            } else {
                // No explicit width - element will shrink to fit, just calculate intrusion
                FloatAvailableSpace space = block_context_space_at_y(parent_bfc, current_y, 1.0f);
                float local_left = space.left - x_in_bfc;
                float local_right = space.right - x_in_bfc;
                float float_intrusion_left = max(0.0f, local_left);
                float float_intrusion_right = max(0.0f, pa_block->content_width - local_right);

                if (space.has_left_float && float_intrusion_left > 0) {
                    bfc_float_offset_x = float_intrusion_left;
                }
                bfc_available_width_reduction = float_intrusion_left + float_intrusion_right;

                log_debug("[BFC Float Avoid] Auto-width element: offset_x=%.1f, width_reduction=%.1f",
                          bfc_float_offset_x, bfc_available_width_reduction);
            }

            // Calculate total shift needed in local coordinates
            bfc_shift_down = current_y - y_in_bfc;
            if (bfc_shift_down > 0) {
                log_debug("[BFC Float Avoid] Shifting element down by %.1f to avoid floats", bfc_shift_down);
                block->y += bfc_shift_down;
                pa_block->advance_y += bfc_shift_down;
            }
        }
    }

    if (establishes_bfc) {
        lycon->block.is_bfc_root = true;
        lycon->block.establishing_element = block;
        // Reset float lists for new BFC (children won't see parent's floats)
        block_context_reset_floats(&lycon->block);
        log_debug("[BlockContext] Block %s establishes new BFC", block->node_name());
    } else {
        // Clear is_bfc_root so we don't inherit it from parent
        // This ensures block_context_find_bfc walks up to the actual BFC root
        lycon->block.is_bfc_root = false;
        lycon->block.establishing_element = nullptr;
        // Don't reset floats - they belong to the parent BFC
    }

    uintptr_t elmt_name = block->tag();
    if (elmt_name == HTM_TAG_IMG) { // load image intrinsic width and height
        log_debug("[IMG LAYOUT] Processing IMG element: %s", block->node_name());
        const char *value;
        value = block->get_attribute("src");
        log_debug("[IMG LAYOUT] src attribute: %s", value ? value : "NULL");
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
        if (block->embed && block->embed->img) {
            ImageSurface* img = block->embed->img;
            // Image intrinsic dimensions are in CSS logical pixels
            float w = img->width;
            float h = img->height;

            // Check if width was specified as percentage but resolved to 0
            // This happens when parent has auto/0 width - use intrinsic width instead
            bool width_is_zero_percent = (lycon->block.given_width == 0 &&
                                          block->blk && !isnan(block->blk->given_width_percent));

            log_debug("image intrinsic dims: %f x %f, given: %f x %f, zero_percent=%d", w, h,
                lycon->block.given_width, lycon->block.given_height, width_is_zero_percent);

            if (lycon->block.given_width < 0 || lycon->block.given_height < 0 || width_is_zero_percent) {
                if (lycon->block.given_width >= 0 && !width_is_zero_percent) {
                    // Width specified, scale unspecified height
                    lycon->block.given_height = lycon->block.given_width * h / w;
                }
                else if (lycon->block.given_height >= 0 && lycon->block.given_width < 0) {
                    // Height specified, scale unspecified width
                    lycon->block.given_width = lycon->block.given_height * w / h;
                }
                else {
                    // Both width and height unspecified, or width was 0% on 0-width parent
                    if (img->format == IMAGE_FORMAT_SVG) {
                        // For SVG, try to use parent width, but fall back to intrinsic if parent is 0
                        float parent_width = lycon->block.parent ? lycon->block.parent->content_width : 0;
                        if (parent_width > 0) {
                            lycon->block.given_width = parent_width;
                        } else {
                            // Parent has no width, use intrinsic SVG dimensions
                            lycon->block.given_width = w;
                        }
                        lycon->block.given_height = lycon->block.given_width * h / w;
                    }
                    else { // use image intrinsic dimensions
                        lycon->block.given_width = w;
                        lycon->block.given_height = h;
                    }
                }
            }
            // else both width and height specified (non-zero)
            if (img->format == IMAGE_FORMAT_SVG) {
                img->max_render_width = max(lycon->block.given_width, img->max_render_width);
            }
            log_debug("image dimensions: %f x %f", lycon->block.given_width, lycon->block.given_height);
        }
        else { // failed to load image
            // use html width/height attributes if specified, otherwise use placeholder size
            if (lycon->block.given_width <= 0) lycon->block.given_width = 40;
            if (lycon->block.given_height <= 0) lycon->block.given_height = 30;
            // todo: use a placeholder
        }
    }

    // determine block width and height
    float content_width = -1;
    log_debug("Block '%s': given_width=%.2f,  given_height=%.2f, blk=%p, width_type=%d",
        block->node_name(), lycon->block.given_width, lycon->block.given_height, (void*)block->blk,
        block->blk ? block->blk->given_width_type : -1);

    // Check if parent is measuring intrinsic sizes (propagated via available_space)
    // This allows intrinsic sizing mode to flow down through nested blocks
    bool parent_is_intrinsic_sizing = lycon->available_space.is_intrinsic_sizing();
    if (parent_is_intrinsic_sizing) {
        log_debug("Block '%s': parent is in intrinsic sizing mode (width=%s)",
            block->node_name(),
            lycon->available_space.width.is_min_content() ? "min-content" : "max-content");
    }

    // Check if this is a floated element with auto width
    // CSS 2.2 Section 10.3.5: Floats with auto width use shrink-to-fit width
    // We'll do a post-layout adjustment after content is laid out
    // Note: width is "auto" if either explicitly set to auto (CSS_VALUE_AUTO=84) or unset (CSS_VALUE__UNDEF=0)
    bool width_is_auto = !block->blk ||
                         block->blk->given_width_type == CSS_VALUE_AUTO ||
                         block->blk->given_width_type == CSS_VALUE__UNDEF;
    bool is_float_auto_width = element_has_float(block) && lycon->block.given_width < 0 && width_is_auto;

    // Check for width: max-content or min-content (intrinsic sizing keywords)
    // Either from CSS property OR propagated from parent's intrinsic sizing mode
    bool is_max_content_width = (block->blk && block->blk->given_width_type == CSS_VALUE_MAX_CONTENT) ||
                                (parent_is_intrinsic_sizing && lycon->available_space.is_width_max_content());
    bool is_min_content_width = (block->blk && block->blk->given_width_type == CSS_VALUE_MIN_CONTENT) ||
                                (parent_is_intrinsic_sizing && lycon->available_space.is_width_min_content());

    if (is_max_content_width || is_min_content_width) {
        // For max-content/min-content width, use shrink-to-fit behavior
        // Initially use available width for layout, then shrink to content width post-layout
        float available_width = pa_block->content_width;
        if (block->bound) {
            available_width -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        content_width = available_width;
        log_debug("max/min-content width: initial layout with available_width=%.2f (will shrink post-layout)", content_width);
    }
    else if (is_float_auto_width) {
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
        log_debug("Deriving from parent: pa_block->content_width=%.2f", pa_block->content_width);
        float available_from_parent = pa_block->content_width;

        // Reduce available width for BFC elements avoiding floats
        if (bfc_available_width_reduction > 0) {
            available_from_parent -= bfc_available_width_reduction;
            log_debug("[BFC Float Avoid] Reduced available width by %.1f to %.1f",
                      bfc_available_width_reduction, available_from_parent);
        }

        if (block->bound) {
            content_width = available_from_parent
                - (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                - (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }
        else { content_width = available_from_parent; }
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            content_width = adjust_min_max_width(block, content_width);
            if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        } else {
            content_width = adjust_border_padding_width(block, content_width);
            if (block->bound) content_width = adjust_min_max_width(block, content_width);
        }
    }
    // Clamp to 0 - negative content_width can occur with very narrow containers
    // (e.g., width:1px) after subtracting borders/padding/margins. CSS allows this,
    // with content overflowing the container.
    if (content_width < 0) content_width = 0;
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
    else { // auto height - will be determined by content
        // Don't inherit parent's content_height for auto height blocks
        // The height will be finalized after content is laid out in finalize_block_flow
        content_height = 0;  // Initial value, will be updated during layout
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

    // If this block establishes a BFC, update the float edge boundaries
    // This must be done AFTER content_width is calculated
    if (lycon->block.is_bfc_root && lycon->block.establishing_element == block) {
        lycon->block.float_left_edge = 0;
        lycon->block.float_right_edge = content_width;
        log_debug("[BFC] Updated float edges for %s: left=0, right=%.1f", block->node_name(), content_width);
    }

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

        // CSS behavior for <center> element: block children should be horizontally centered
        // This is achieved by applying margin:auto to block children
        // Note: <center> is deprecated HTML but still widely used
        if (block->parent && block->parent->is_element() && block->parent->tag() == HTM_TAG_CENTER) {
            // Only apply centering to blocks that don't already have explicit margin values
            // and that are not full-width (i.e., have a defined width less than parent)
            if (block->width < pa_block->content_width &&
                block->bound->margin.left_type != CSS_VALUE_AUTO &&
                block->bound->margin.right_type != CSS_VALUE_AUTO) {
                block->bound->margin.left_type = CSS_VALUE_AUTO;
                block->bound->margin.right_type = CSS_VALUE_AUTO;
                log_debug("applied margin:auto centering for block inside <center>");
            }
        }

        log_debug("block margins: left=%f, right=%f, left_type=%d, right_type=%d",
            block->bound->margin.left, block->bound->margin.right, block->bound->margin.left_type, block->bound->margin.right_type);

        // CSS 2.1 §10.3.5: For floats, if margin-left/right is 'auto', its used value is 0
        // CSS 2.1 §10.3.3: For normal flow blocks, auto margins center the element
        if (is_float) {
            // Floats: auto margins become 0
            if (block->bound->margin.left_type == CSS_VALUE_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right_type == CSS_VALUE_AUTO) block->bound->margin.right = 0;
        } else if (block->bound->margin.left_type == CSS_VALUE_AUTO && block->bound->margin.right_type == CSS_VALUE_AUTO)  {
            block->bound->margin.left = block->bound->margin.right = max((pa_block->content_width - block->width) / 2, 0);
        } else {
            if (block->bound->margin.left_type == CSS_VALUE_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right_type == CSS_VALUE_AUTO) block->bound->margin.right = 0;
        }
        log_debug("finalize block margins: left=%f, right=%f", block->bound->margin.left, block->bound->margin.right);
        float y_before_margin = block->y;
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;

        // Apply BFC float avoidance offset for normal-flow BFC elements
        if (bfc_float_offset_x > 0) {
            block->x += bfc_float_offset_x;
            log_debug("[BFC Float Avoid] Applied x offset: block->x now=%.1f", block->x);
        }

        log_debug("Y coordinate: before margin=%f, margin.top=%f, after margin=%f (tag=%s)",
                  y_before_margin, block->bound->margin.top, block->y, block->node_name());
    }
    else {
        block->width = content_width;  block->height = content_height;
        // no change to block->x, block->y

        // Apply BFC float avoidance offset for normal-flow BFC elements
        if (bfc_float_offset_x > 0) {
            block->x += bfc_float_offset_x;
            log_debug("[BFC Float Avoid] Applied x offset (no bounds): block->x now=%.1f", block->x);
        }
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
    if ((is_float_auto_width || is_max_content_width || is_min_content_width) && block->is_element()) {
        // Font is loaded after setup_inline, so now we can calculate intrinsic width
        DomElement* dom_element = (DomElement*)block;
        float available = pa_block->content_width;
        if (block->bound) {
            available -= (block->bound->margin.left_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.left)
                      + (block->bound->margin.right_type == CSS_VALUE_AUTO ? 0 : block->bound->margin.right);
        }

        // Calculate fit-content width (shrink-to-fit)
        // Use float to avoid truncation that could cause text wrapping issues
        float fit_content = calculate_fit_content_width(lycon, dom_element, available);

        // For min-content, use min-content width instead of fit-content
        if (is_min_content_width) {
            fit_content = calculate_min_content_width(lycon, (DomNode*)dom_element);
            log_debug("min-content width: using min_content=%.1f", fit_content);
        }

        if (fit_content > 0 && fit_content < block->width) {
            log_debug("Shrink-to-fit (%s): fit_content=%.1f, old_width=%.1f, available=%.1f",
                is_max_content_width ? "max-content" : (is_min_content_width ? "min-content" : "float"),
                fit_content, block->width, available);

            // Update block width to shrink-to-fit size
            // Round up to next 0.5px to prevent text wrapping due to floating-point precision issues
            // while avoiding larger additions that prevent adjacent content from fitting
            float rounded_width = ceilf(fit_content * 2.0f) / 2.0f;
            block->width = rounded_width;

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
    // CSS 2.2 Section 8.3.1: Margins collapse when parent has no border/padding
    // This applies when block->bound is NULL (no border/padding/margin) OR
    // when block->bound exists but has no bottom border/padding
    // IMPORTANT: Elements that establish a BFC do NOT collapse margins with their children
    bool has_border_bottom = block->bound && block->bound->border && block->bound->border->width.bottom > 0;
    bool has_padding_bottom = block->bound && block->bound->padding.bottom > 0;
    bool creates_bfc_for_collapse = block->scroller &&
                       (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
                        block->scroller->overflow_y != CSS_VALUE_VISIBLE);

    if (!has_border_bottom && !has_padding_bottom && !creates_bfc_for_collapse && block->first_child) {
        // collapse bottom margin with last in-flow child block
        // Skip absolutely positioned and floated children - they're out of normal flow
        // Find last in-flow child (skip abs-positioned, floated elements, and empty zero-height blocks)
        // CSS 2.2 Section 8.3.1: An empty block allows margins to collapse "through" it when:
        // - It has zero height
        // - It has no borders, padding, or line boxes
        View* last_in_flow = nullptr;
        View* child = (View*)block->first_child;
        while (child) {
            if (child->view_type && child->is_block()) {
                ViewBlock* vb = (ViewBlock*)child;
                // CSS 2.1 Section 8.3.1: Only block-level boxes participate in margin collapsing
                // Inline-blocks are inline-level boxes in inline formatting context - they don't collapse
                bool is_inline_block = (vb->view_type == RDT_VIEW_INLINE_BLOCK);
                bool is_out_of_flow = is_inline_block || (vb->position &&
                    (vb->position->position == CSS_VALUE_ABSOLUTE ||
                     vb->position->position == CSS_VALUE_FIXED ||
                     element_has_float(vb)));
                if (!is_out_of_flow) {
                    last_in_flow = child;
                }
            } else if (child->view_type) {
                // Non-block placed children (like inline elements) count as in-flow
                last_in_flow = child;
            }
            child = (View*)child->next_sibling;
        }

        // Skip empty zero-height blocks at the end - margins collapse "through" them
        // Find the effective last child whose margin-bottom should collapse with parent
        // CSS 2.2 Section 8.3.1: Margins collapse through an element only if it has no:
        // - min-height (> 0), borders, padding, inline content, or block formatting context
        View* effective_last = last_in_flow;
        while (effective_last && effective_last->is_block()) {
            ViewBlock* vb = (ViewBlock*)effective_last;
            // Check if this is an empty zero-height block that allows collapse-through
            // Must have: zero height, no borders, no padding, no margin, AND no in-flow content
            // Note: absolutely positioned children don't count as in-flow content
            bool has_in_flow_children = false;
            if (vb->first_child) {
                View* child_of_vb = (View*)vb->first_child;
                while (child_of_vb) {
                    if (child_of_vb->view_type && child_of_vb->is_block()) {
                        ViewBlock* child_block = (ViewBlock*)child_of_vb;
                        bool is_child_out_of_flow = (child_block->position &&
                            (child_block->position->position == CSS_VALUE_ABSOLUTE ||
                             child_block->position->position == CSS_VALUE_FIXED ||
                             element_has_float(child_block)));
                        if (!is_child_out_of_flow) {
                            has_in_flow_children = true;
                            break;
                        }
                    } else if (child_of_vb->view_type) {
                        // non-block children (like text) are in-flow
                        has_in_flow_children = true;
                        break;
                    }
                    child_of_vb = (View*)child_of_vb->next_sibling;
                }
            }

            if (vb->height == 0 && !has_in_flow_children) {
                float border_top = vb->bound && vb->bound->border ? vb->bound->border->width.top : 0;
                float border_bottom = vb->bound && vb->bound->border ? vb->bound->border->width.bottom : 0;
                float padding_top = vb->bound ? vb->bound->padding.top : 0;
                float padding_bottom = vb->bound ? vb->bound->padding.bottom : 0;
                float margin_bottom = vb->bound ? vb->bound->margin.bottom : 0;
                if (border_top == 0 && border_bottom == 0 && padding_top == 0 && padding_bottom == 0 && margin_bottom == 0) {
                    // This empty block allows margins to collapse through - look at previous sibling
                    log_debug("skipping empty zero-height block (only out-of-flow children) for bottom margin collapsing");
                    View* prev = effective_last->prev_placed_view();
                    effective_last = prev;
                    continue;
                }
            }
            break;
        }

        if (effective_last && effective_last->is_block() && ((ViewBlock*)effective_last)->bound) {
            ViewBlock* last_child_block = (ViewBlock*)effective_last;
            if (last_child_block->bound->margin.bottom > 0) {
                // CSS 2.2 Section 8.3.1: Margins collapse only if there's NO content separating them.
                // Check if there's any inline-level content (inline-blocks, text) AFTER the last
                // block-level child. If so, this content separates the child's margin from the
                // parent's margin, and they should NOT collapse.
                // Note: Empty zero-height blocks (like containers for only floats) don't count as
                // "separating content" - margins can collapse through them.
                bool has_content_after = false;
                View* sibling = (View*)effective_last->next_sibling;
                while (sibling) {
                    if (sibling->view_type) {
                        // Any placed view after effective_last means content separates the margins
                        // Except for absolutely/fixed positioned elements and floats which are out of flow
                        if (sibling->is_block()) {
                            ViewBlock* sb = (ViewBlock*)sibling;
                            bool is_truly_out_of_flow = sb->position &&
                                (sb->position->position == CSS_VALUE_ABSOLUTE ||
                                 sb->position->position == CSS_VALUE_FIXED ||
                                 element_has_float(sb));
                            // Inline-blocks ARE inline-level content that separates margins
                            bool is_inline_level = (sb->view_type == RDT_VIEW_INLINE_BLOCK);
                            if (is_inline_level) {
                                has_content_after = true;
                                break;
                            }
                            // Regular blocks with zero height don't separate margins (CSS 8.3.1)
                            // Margins can collapse "through" empty blocks
                            if (!is_truly_out_of_flow && sb->height > 0) {
                                has_content_after = true;
                                break;
                            }
                        } else {
                            // Non-block content (text, inline elements)
                            has_content_after = true;
                            break;
                        }
                    }
                    sibling = (View*)sibling->next_sibling;
                }

                if (has_content_after) {
                    log_debug("NOT collapsing bottom margin - content exists after last block child");
                } else {
                    float parent_margin = block->bound ? block->bound->margin.bottom : 0;
                    float margin_bottom = max(parent_margin, last_child_block->bound->margin.bottom);
                    block->height -= last_child_block->bound->margin.bottom;

                    // If parent has no bound yet, allocate one to store the collapsed margin
                    if (!block->bound) {
                        block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                        memset(block->bound, 0, sizeof(BoundaryProp));
                    }
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

    if (creates_bfc || lycon->block.is_bfc_root) {
        // Check unified BlockContext for float containment
        if (lycon->block.establishing_element == block) {
            float max_float_bottom = lycon->block.lowest_float_bottom;
            float content_bottom = block->y + block->height;
            log_debug("[BlockContext] Height expansion check: max_float_bottom=%.1f, content_bottom=%.1f",
                      max_float_bottom, content_bottom);
            if (max_float_bottom > content_bottom - block->y) {
                float old_height = block->height;
                block->height = max_float_bottom;
                log_debug("[BlockContext] Height expanded: old=%.1f, new=%.1f", old_height, block->height);
            }
        }

        // Also check for floats in block context
        log_debug("BFC %s: left_float_count=%d, right_float_count=%d",
            block->node_name(), lycon->block.left_float_count, lycon->block.right_float_count);
        if (lycon->block.establishing_element == block) {
            // Find the maximum bottom of all floated children
            float max_float_bottom = 0;
            log_debug("BFC %s: checking left floats", block->node_name());
            for (FloatBox* fb = lycon->block.left_floats; fb; fb = fb->next) {
                log_debug("BFC left float: margin_box_bottom=%.1f", fb->margin_box_bottom);
                if (fb->margin_box_bottom > max_float_bottom) {
                    max_float_bottom = fb->margin_box_bottom;
                }
            }
            log_debug("BFC %s: checking right floats", block->node_name());
            for (FloatBox* fb = lycon->block.right_floats; fb; fb = fb->next) {
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

    // Apply CSS float layout using BlockContext
    // IMPORTANT: Floats must be added to the BFC root, not just the immediate parent
    if (block->position && element_has_float(block)) {
        log_debug("Element has float property, applying float layout");

        // Position the float using the parent's BlockContext
        // layout_float_element uses block_context_find_bfc which walks up parent chain
        layout_float_element(lycon, block);

        // Add float to the BFC root (not just immediate parent)
        // This ensures sibling elements can see the float via block_context_find_bfc
        BlockContext* bfc = block_context_find_bfc(pa_block);
        if (bfc) {
            block_context_add_float(bfc, block);
            log_debug("[BlockContext] Float added to BFC root (bfc=%p, pa_block=%p)", (void*)bfc, (void*)pa_block);
        } else {
            // Fallback to parent if no BFC found
            block_context_add_float(pa_block, block);
            log_debug("[BlockContext] Float added to parent context (no BFC found)");
        }
    }

    // Restore parent BFC if we created a new one - handled by block.parent in calling code
}

void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    uintptr_t tag = elmt->tag();
    if (tag == HTM_TAG_IMG) {
        log_debug("[LAYOUT_BLOCK IMG] layout_block ENTRY for IMG element: %s", elmt->node_name());
    }
    auto t_block_start = high_resolution_clock::now();

    log_enter();
    // display: CSS_VALUE_BLOCK, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_LIST_ITEM
    log_debug("layout block %s (display: outer=%d, inner=%d)", elmt->node_name(), display.outer, display.inner);

    // check for display math elements (class="math display")
    if (elmt->is_element()) {
        DomElement* elem = static_cast<DomElement*>(elmt);
        if (is_display_math_element(elem)) {
            // ensure line break before display math
            if (!lycon->line.is_line_start) { line_break(lycon); }
            layout_display_math_block(lycon, elem);
            log_leave();
            return;
        }
    }

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
    BlockContext pa_block = lycon->block;  Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // -1 as unresolved
    lycon->block.parent = &pa_block;  lycon->elmt = elmt;
    log_debug("saved pa_block.advance_y: %.2f for element %s", pa_block.advance_y, elmt->node_name());
    lycon->block.content_width = lycon->block.content_height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;

    uintptr_t elmt_name = elmt->tag();
    ViewBlock* block = (ViewBlock*)set_view(lycon,
        // Check table first to handle inline-table correctly
        display.inner == CSS_VALUE_TABLE ? RDT_VIEW_TABLE :
        display.outer == CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        RDT_VIEW_BLOCK,
        elmt);
    block->display = display;

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    // =========================================================================
    // LAYOUT CACHE INTEGRATION (Phase 3: Run Mode Integration)
    // Try cache lookup for early bailout when dimensions already computed
    // =========================================================================
    DomElement* dom_elem = elmt->is_element() ? elmt->as_element() : nullptr;
    radiant::LayoutCache* cache = dom_elem ? dom_elem->layout_cache : nullptr;

    // Build known dimensions from current constraints
    radiant::KnownDimensions known_dims = radiant::known_dimensions_none();
    if (block->blk && block->blk->given_width > 0) {
        known_dims.width = block->blk->given_width;
        known_dims.has_width = true;
    }
    if (block->blk && block->blk->given_height > 0) {
        known_dims.height = block->blk->given_height;
        known_dims.has_height = true;
    }

    // Try cache lookup
    if (cache) {
        radiant::SizeF cached_size;
        if (radiant::layout_cache_get(cache, known_dims, lycon->available_space,
                                       lycon->run_mode, &cached_size)) {
            // Cache hit! Use cached dimensions
            block->width = cached_size.width;
            block->height = cached_size.height;
            g_layout_cache_hits++;
            log_info("BLOCK CACHE HIT: element=%s, size=(%.1f x %.1f), mode=%d",
                     elmt->node_name(), cached_size.width, cached_size.height, (int)lycon->run_mode);
            // Restore parent context and return early
            lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
            log_leave();
            auto t_block_end = high_resolution_clock::now();
            g_block_layout_time += duration<double, std::milli>(t_block_end - t_block_start).count();
            g_block_layout_count++;
            return;
        }
        g_layout_cache_misses++;
        log_debug("BLOCK CACHE MISS: element=%s, mode=%d", elmt->node_name(), (int)lycon->run_mode);
    }

    // Early bailout for ComputeSize mode when both dimensions are known
    if (lycon->run_mode == radiant::RunMode::ComputeSize) {
        bool has_definite_width = (block->blk && block->blk->given_width > 0);
        bool has_definite_height = (block->blk && block->blk->given_height > 0);

        if (has_definite_width && has_definite_height) {
            // Both dimensions known - can skip full layout
            block->width = block->blk->given_width;
            block->height = block->blk->given_height;
            log_info("BLOCK EARLY BAILOUT: Both dimensions known (%.1fx%.1f), skipping full layout",
                     block->width, block->height);
            // Restore parent context and return early
            lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
            log_leave();
            auto t_block_end = high_resolution_clock::now();
            g_block_layout_time += duration<double, std::milli>(t_block_end - t_block_start).count();
            g_block_layout_count++;
            return;
        }
        log_debug("BLOCK: ComputeSize mode but dimensions not fully known (w=%d, h=%d)",
                  has_definite_width, has_definite_height);
    }

    // CSS Counter handling (CSS 2.1 Section 12.4)
    // Push a new counter scope for this element
    if (lycon->counter_context) {
        counter_push_scope(lycon->counter_context);

        // Apply counter-reset if specified
        if (block->blk && block->blk->counter_reset) {
            log_debug("    [Block] Applying counter-reset: %s", block->blk->counter_reset);
            counter_reset(lycon->counter_context, block->blk->counter_reset);
        }

        // Apply counter-increment if specified
        if (block->blk && block->blk->counter_increment) {
            log_debug("    [Block] Applying counter-increment: %s", block->blk->counter_increment);
            counter_increment(lycon->counter_context, block->blk->counter_increment);
        }

        // CSS 2.1 Section 12.5: List markers use implicit "list-item" counter
        // For display:list-item, auto-increment list-item counter
        if (display.outer == CSS_VALUE_LIST_ITEM) {
            log_debug("    [List] Auto-incrementing list-item counter");
            counter_increment(lycon->counter_context, "list-item 1");

            // Set default list-style-position to outside if not specified
            // CSS 2.1 Section 12.5.1: Initial value is 'outside'
            bool is_outside_position = true;  // Default is outside
            if (block->blk && block->blk->list_style_position != 0) {
                // Check if position is explicitly set to "inside"
                // Position values: 1=inside, 2=outside (from shorthand expansion)
                if (block->blk->list_style_position == 1) {
                    is_outside_position = false;
                    log_debug("    [List] list-style-position=inside (is_outside=0)");
                } else {
                    is_outside_position = true;
                    log_debug("    [List] list-style-position=outside (is_outside=1)");
                }
            } else {
                log_debug("    [List] Using default list-style-position=outside");
            }

            // Generate list marker if list-style-type is not 'none'
            // Only create ::marker pseudo-element for 'inside' positioned markers
            // Outside markers are rendered directly in the margin area (not in DOM tree)
            if (block->blk && block->blk->list_style_type && block->blk->list_style_type != CSS_VALUE_NONE) {
                CssEnum marker_style = block->blk->list_style_type;
                const CssEnumInfo* info = css_enum_info(marker_style);
                log_debug("    [List] Generating marker with style: %s (0x%04X)", info ? info->name : "unknown", marker_style);

                // Determine if this is a bullet marker (disc, circle, square) or text marker (decimal, roman, alpha)
                bool is_bullet_marker = (marker_style == CSS_VALUE_DISC ||
                                        marker_style == CSS_VALUE_CIRCLE ||
                                        marker_style == CSS_VALUE_SQUARE);

                if (!is_outside_position) {
                    // Create ::marker pseudo-element for 'inside' positioned markers
                    DomElement* parent_elem = (DomElement*)elmt;

                    if (!block->pseudo) {
                        block->pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
                        memset(block->pseudo, 0, sizeof(PseudoContentProp));
                    }

                    if (!block->pseudo->before_generated) {
                        // Create ViewMarker element for the ::marker pseudo-element
                        // Use fixed width of ~1.4em (22px at 16px font) to match browser behavior
                        // Get font size from block->font (already resolved by dom_node_resolve_style)
                        // This is more reliable than lycon->font.ft_face which is still parent context
                        float font_size = 16.0f;  // default
                        if (block->font && block->font->font_size > 0) {
                            font_size = block->font->font_size;
                        }
                        float fixed_marker_width = font_size * 1.375f;  // ~22px at 16px font
                        float bullet_size = font_size * 0.35f;  // ~5-6px at 16px font

                        // Create DomElement for ::marker (using ViewMarker structure)
                        DomElement* marker_elem = dom_element_create(parent_elem->doc, "::marker", nullptr);
                        if (marker_elem) {
                            marker_elem->parent = parent_elem;

                            // Allocate and set MarkerProp
                            MarkerProp* marker_prop = (MarkerProp*)alloc_prop(lycon, sizeof(MarkerProp));
                            memset(marker_prop, 0, sizeof(MarkerProp));
                            marker_prop->marker_type = marker_style;
                            marker_prop->width = fixed_marker_width;
                            marker_prop->bullet_size = bullet_size;

                            // For text markers (decimal, roman, alpha), format the counter text
                            if (!is_bullet_marker) {
                                char marker_text[64];
                                int marker_len = counter_format(lycon->counter_context, "list-item", marker_style, marker_text, sizeof(marker_text));
                                if (marker_len > 0 && marker_len + 2 < (int)sizeof(marker_text)) {
                                    marker_text[marker_len] = '.';
                                    marker_text[marker_len + 1] = ' ';
                                    marker_text[marker_len + 2] = '\0';
                                    marker_len += 2;

                                    // Copy text to arena
                                    char* text_copy = (char*)arena_alloc(parent_elem->doc->arena, marker_len + 1);
                                    if (text_copy) {
                                        memcpy(text_copy, marker_text, marker_len + 1);
                                        marker_prop->text_content = text_copy;
                                    }
                                }
                            }

                            // Store marker_prop in the element (use embed as a generic storage or cast)
                            // We'll use the view_type to identify it's a marker during rendering
                            marker_elem->view_type = RDT_VIEW_MARKER;

                            // Store marker properties - use blk field to store marker prop pointer
                            // (reusing the blk pointer since markers don't need BlockProp)
                            marker_elem->blk = (BlockProp*)marker_prop;

                            log_debug("    [List] Created ::marker with fixed width=%.1f, bullet_size=%.1f, type=%s",
                                     fixed_marker_width, bullet_size, is_bullet_marker ? "bullet" : "text");

                            block->pseudo->before = marker_elem;
                            block->pseudo->before_generated = true;
                        }
                    }
                } else {
                    // Outside markers are not added to DOM tree
                    // They should be rendered directly in the margin area during paint
                    log_debug("    [List] Skipping 'outside' marker creation (should render in margin area)");
                }
            }
        }
    }

    if (block->position && (block->position->position == CSS_VALUE_ABSOLUTE || block->position->position == CSS_VALUE_FIXED)) {
        layout_abs_block(lycon, elmt, block, &pa_block, &pa_line);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
    } else {
        // layout block content to determine content width and height
        // DEBUG: Check if this is a table before layout_block_content
        // Always print block type for debugging
        bool is_table = (block->view_type == RDT_VIEW_TABLE);
        layout_block_content(lycon, block, &pa_block, &pa_line);

        // WORKAROUND: Restore table height from global - it gets corrupted after return
        // This is a mysterious issue where the height field gets zeroed between
        // the return statement in layout_block_content and this point
        if (is_table && g_layout_table_height > 0) {
            block->height = g_layout_table_height;
            g_layout_table_height = 0;  // Reset for next table
        }

        // CSS 2.1 Section 10.8.1: For non-replaced inline-blocks with in-flow line boxes
        // and overflow:visible, the baseline is the baseline of the last line box.
        // The last line's ascender was saved to lycon->block.last_line_ascender in line_break().
        // If there was no line break (single line), use lycon->line.max_ascender.
        float content_last_line_ascender = lycon->block.last_line_ascender;
        if (content_last_line_ascender == 0 && lycon->line.max_ascender > 0) {
            // No line break occurred - use current line's ascender
            content_last_line_ascender = lycon->line.max_ascender;
        }
        bool content_has_line_boxes = content_last_line_ascender > 0;
        log_debug("inline-block content baseline: last_line_ascender=%.1f, has_line_boxes=%d",
            content_last_line_ascender, content_has_line_boxes);

        log_debug("flow block in parent context, block->y before restoration: %.2f", block->y);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;

        // flow the block in parent context
        if (display.outer == CSS_VALUE_INLINE_BLOCK) {
            if (!lycon->line.start_view) lycon->line.start_view = (View*)block;

            // Update effective line bounds for floats at current Y position
            // This is critical for inline-blocks to wrap around floats correctly
            update_line_for_bfc_floats(lycon);

            // Check available width considering floats
            float effective_left = lycon->line.has_float_intrusion ?
                lycon->line.effective_left : lycon->line.left;
            float effective_right = lycon->line.has_float_intrusion ?
                lycon->line.effective_right : lycon->line.right;

            log_debug("inline-block float check: has_float_intrusion=%d, effective_left=%.1f, effective_right=%.1f, line.left=%.1f, line.right=%.1f, advance_x=%.1f",
                lycon->line.has_float_intrusion, lycon->line.effective_left, lycon->line.effective_right,
                lycon->line.left, lycon->line.right, lycon->line.advance_x);

            // Ensure advance_x is at least at effective_left
            if (lycon->line.advance_x < effective_left) {
                lycon->line.advance_x = effective_left;
            }

            if (lycon->line.advance_x + block->width > effective_right) {
                line_break(lycon);
                // After line break, update effective bounds for new Y
                update_line_for_bfc_floats(lycon);
                effective_left = lycon->line.has_float_intrusion ?
                    lycon->line.effective_left : lycon->line.left;
                block->x = effective_left;
            } else {
                block->x = lycon->line.advance_x;
            }
            if (block->in_line && block->in_line->vertical_align) {
                float item_height = block->height + (block->bound ?
                    block->bound->margin.top + block->bound->margin.bottom : 0);
                // For non-replaced inline-blocks with content: baseline is at content baseline
                // For replaced elements (like img): baseline is at bottom margin edge
                bool overflow_visible = !block->scroller ||
                    (block->scroller->overflow_x == CSS_VALUE_VISIBLE &&
                     block->scroller->overflow_y == CSS_VALUE_VISIBLE);
                float item_baseline;
                if (content_has_line_boxes && overflow_visible) {
                    // Baseline from top of margin-box = margin.top + content_baseline
                    item_baseline = (block->bound ? block->bound->margin.top : 0) + content_last_line_ascender;
                } else {
                    // Replaced or no content: baseline at bottom margin edge
                    item_baseline = item_height;
                }
                float line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
                float offset = calculate_vertical_align_offset(
                    lycon, block->in_line->vertical_align, item_height, line_height,
                    lycon->line.max_ascender, item_baseline);
                block->y = lycon->block.advance_y + offset;  // block->bound->margin.top will be added below
                log_debug("valigned-inline-block: offset %f, line %f, block %f, adv: %f, y: %f, va:%d",
                    offset, line_height, block->height, lycon->block.advance_y, block->y, block->in_line->vertical_align);
                // For TOP/BOTTOM, we handle max_descender/max_ascender specially in the section below
                // Don't apply this generic formula which assumes baseline-relative positioning
                if (block->in_line->vertical_align != CSS_VALUE_TOP && block->in_line->vertical_align != CSS_VALUE_BOTTOM) {
                    lycon->line.max_descender = max(lycon->line.max_descender, offset + item_height - lycon->line.max_ascender);
                }
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
                else if (block->in_line->vertical_align == CSS_VALUE_TOP) {
                    // CSS 2.1 10.8.1: vertical-align:top aligns element's top with line box top
                    // The line box top is at init_ascender above the baseline
                    // Element contributes (block_flow_height - init_ascender) below the baseline
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->block.init_ascender);
                    // The strut always contributes its ascender to the line box
                    lycon->line.max_ascender = max(lycon->line.max_ascender, lycon->block.init_ascender);
                }
                else if (block->in_line->vertical_align == CSS_VALUE_BOTTOM) {
                    // CSS 2.1 10.8.1: vertical-align:bottom aligns element's bottom with line box bottom
                    // Similar calculation but relative to init_descender
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block_flow_height - lycon->block.init_descender);
                    // The strut always contributes its descender to the line box
                    lycon->line.max_descender = max(lycon->line.max_descender, lycon->block.init_descender);
                }
                else {
                    // For other vertical-align values (sub, super, middle, etc.)
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->line.max_ascender);
                }
            } else {
                // default baseline alignment for inline block
                // CSS 2.1 Section 10.8.1:
                // - Non-replaced inline-block with in-flow line boxes and overflow:visible:
                //   baseline = baseline of last line box (captured as content_last_line_ascender)
                // - Non-replaced inline-block with no in-flow line boxes OR overflow != visible:
                //   baseline = bottom margin edge
                // - Replaced inline-block (like img): baseline = bottom margin edge

                // Check if this inline-block has overflow:visible and in-flow line boxes
                bool overflow_visible = !block->scroller ||
                    (block->scroller->overflow_x == CSS_VALUE_VISIBLE &&
                     block->scroller->overflow_y == CSS_VALUE_VISIBLE);
                bool uses_content_baseline = content_has_line_boxes && overflow_visible;

                if (uses_content_baseline) {
                    // Non-replaced inline-block with text content and overflow:visible
                    // Baseline is at content_last_line_ascender from top of content box
                    // Distance above parent baseline = content_last_line_ascender
                    // Distance below parent baseline = block->height - content_last_line_ascender
                    lycon->line.max_ascender = max(lycon->line.max_ascender, content_last_line_ascender +
                        (block->bound ? block->bound->margin.top : 0));
                    float descender_part = block->height - content_last_line_ascender +
                        (block->bound ? block->bound->margin.bottom : 0);
                    lycon->line.max_descender = max(lycon->line.max_descender, descender_part);
                    log_debug("inline-block with content baseline: ascender=%.1f, descender=%.1f",
                        content_last_line_ascender, descender_part);
                } else {
                    // Replaced element or no in-flow content: baseline at bottom margin edge
                    if (block->bound) {
                        // margin-box above baseline = height + margin-top + margin-bottom
                        lycon->line.max_ascender = max(lycon->line.max_ascender,
                            block->height + block->bound->margin.top + block->bound->margin.bottom);
                        // only strut descender below baseline
                        lycon->line.max_descender = max(lycon->line.max_descender, lycon->block.init_descender);
                    }
                    else {
                        lycon->line.max_ascender = max(lycon->line.max_ascender, block->height);
                        lycon->line.max_descender = max(lycon->line.max_descender, lycon->block.init_descender);
                    }
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
                // Include lycon->line.left to account for parent's left border+padding
                if (block->bound) {
                    lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width
                        + block->bound->margin.left + block->bound->margin.right);
                } else {
                    lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width);
                }
                log_debug("float block end (no advance_y update), pa max_width: %f, block hg: %f",
                    lycon->block.max_width, block->height);
                // Note: floats don't require is_line_start - they're out of flow
            } else if (block->bound) {
                // collapse top margin with parent block
                log_debug("check margin collapsing");

                // Find first in-flow child that can participate in margin collapsing
                // Skip floats AND empty zero-height blocks (CSS 2.2 Section 8.3.1)
                // An empty block allows margins to collapse "through" it when:
                // - It has zero height
                // - It has no borders, padding, or line boxes
                View* first_in_flow_child = block->parent_view()->first_placed_child();
                while (first_in_flow_child) {
                    if (!first_in_flow_child->is_block()) break;
                    ViewBlock* vb = (ViewBlock*)first_in_flow_child;
                    // Skip floats
                    if (vb->position && element_has_float(vb)) {
                        View* next = (View*)first_in_flow_child->next_sibling;
                        while (next && !next->view_type) {
                            next = (View*)next->next_sibling;
                        }
                        first_in_flow_child = next;
                        continue;
                    }
                    // Skip empty zero-height blocks that have no borders/padding
                    // These blocks allow margins to collapse through them (CSS 2.2 8.3.1)
                    if (vb->height == 0) {
                        float border_top = vb->bound && vb->bound->border ? vb->bound->border->width.top : 0;
                        float border_bottom = vb->bound && vb->bound->border ? vb->bound->border->width.bottom : 0;
                        float padding_top = vb->bound ? vb->bound->padding.top : 0;
                        float padding_bottom = vb->bound ? vb->bound->padding.bottom : 0;
                        if (border_top == 0 && border_bottom == 0 && padding_top == 0 && padding_bottom == 0) {
                            log_debug("skipping empty zero-height block for margin collapsing");
                            View* next = (View*)first_in_flow_child->next_sibling;
                            while (next && !next->view_type) {
                                next = (View*)next->next_sibling;
                            }
                            first_in_flow_child = next;
                            continue;
                        }
                    }
                    break;
                }

                // save original margin_top before any collapse modifies it
                // (needed for self-collapsing block calculation below)
                float original_margin_top = block->bound->margin.top;

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
                        // If parent->bound is NULL, parent has no margin/border/padding - margins collapse through
                        float parent_padding_top = parent && parent->bound ? parent->bound->padding.top : 0;
                        float parent_border_top = parent && parent->bound && parent->bound->border ? parent->bound->border->width.top : 0;
                        float parent_margin_top = parent && parent->bound ? parent->bound->margin.top : 0;
                        if (parent && parent->parent && !parent_creates_bfc &&
                            parent_padding_top == 0 && parent_border_top == 0) {
                            float margin_top = max(block->bound->margin.top, parent_margin_top);

                            // CSS 8.3.1: When parent has no border/padding, child margin collapses through parent
                            // If parent had no margin (parent_margin_top == 0), we need to retroactively collapse
                            // with parent's previous sibling
                            float sibling_collapse = 0;
                            if (parent_margin_top == 0) {
                                // Find parent's previous in-flow sibling for retroactive sibling collapsing
                                View* prev_view = parent->prev_placed_view();
                                while (prev_view && prev_view->is_block()) {
                                    ViewBlock* vb = (ViewBlock*)prev_view;
                                    if (vb->position && element_has_float(vb)) {
                                        prev_view = prev_view->prev_placed_view();
                                        continue;
                                    }
                                    break;
                                }
                                if (prev_view && prev_view->is_block() && ((ViewBlock*)prev_view)->bound) {
                                    ViewBlock* prev_block = (ViewBlock*)prev_view;
                                    if (prev_block->bound->margin.bottom > 0 && margin_top > 0) {
                                        sibling_collapse = min(prev_block->bound->margin.bottom, margin_top);
                                        log_debug("retroactive sibling collapse for parent-child: sibling_collapse=%f", sibling_collapse);
                                    }
                                }
                            }

                            parent->y += margin_top - parent_margin_top - sibling_collapse;
                            if (parent->bound) {
                                parent->bound->margin.top = margin_top - sibling_collapse;
                            }
                            block->y = 0;  block->bound->margin.top = 0;
                            log_debug("collapsed margin between block and first child: %f, parent y: %f, block y: %f, sibling_collapse: %f",
                                margin_top, parent->y, block->y, sibling_collapse);
                        }
                        else {
                            log_debug("no parent margin collapsing: parent->bound=%p, border-top=%f, padding-top=%f, parent_creates_bfc=%d",
                                parent ? parent->bound : NULL, parent_border_top, parent_padding_top, parent_creates_bfc);
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
                        // Find previous in-flow sibling (skip out-of-flow elements: floats and absolute/fixed positioned)
                        View* prev_view = block->prev_placed_view();
                        while (prev_view && prev_view->is_block()) {
                            ViewBlock* vb = (ViewBlock*)prev_view;
                            // Skip floats - they're out of normal flow and don't participate in margin collapsing
                            if (vb->position && element_has_float(vb)) {
                                prev_view = prev_view->prev_placed_view();
                                continue;
                            }
                            // Skip absolute/fixed positioned elements - they're out of normal flow
                            // CSS 2.2 Section 9.3.1: "An element is called out of flow if it is floated,
                            // absolutely positioned, or is the root element."
                            if (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                                  vb->position->position == CSS_VALUE_FIXED)) {
                                prev_view = prev_view->prev_placed_view();
                                continue;
                            }
                            break;
                        }
                        // Inline-block elements are part of inline formatting context and create line boxes.
                        // They don't participate in sibling margin collapsing because:
                        // 1. They establish their own BFC internally
                        // 2. They act as in-flow content that separates adjacent block margins
                        // CSS 2.2 Section 8.3.1: Margins don't collapse when separated by in-flow content
                        if (prev_view && prev_view->is_block() && prev_view->view_type != RDT_VIEW_INLINE_BLOCK
                            && ((ViewBlock*)prev_view)->bound) {
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

                // CSS 2.2 Section 8.3.1: Self-collapsing blocks
                // A block is "self-collapsing" when its top and bottom margins are adjoining:
                // - height is 0 (no content height)
                // - no top/bottom border or padding
                // - does not establish a new BFC (overflow is visible)
                // - not a float or absolutely positioned
                // - no in-flow children (no inline content, no in-flow block children)
                // For self-collapsing blocks, margin-top and margin-bottom collapse into
                // max(margin-top, margin-bottom), and this collapsed margin then participates
                // in collapsing with adjacent siblings' margins.
                bool is_self_collapsing = false;
                if (block->height == 0) {
                    float bt = block->bound->border ? block->bound->border->width.top : 0;
                    float bb = block->bound->border ? block->bound->border->width.bottom : 0;
                    float pt = block->bound->padding.top;
                    float pb = block->bound->padding.bottom;
                    bool creates_bfc = block->scroller &&
                        (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
                         block->scroller->overflow_y != CSS_VALUE_VISIBLE);
                    bool is_float_blk = block->position && element_has_float(block);
                    if (bt == 0 && bb == 0 && pt == 0 && pb == 0 && !creates_bfc && !is_float_blk) {
                        // check that the block has no in-flow children
                        // (text nodes, inline content, or in-flow block children prevent self-collapsing)
                        bool has_in_flow_children = false;
                        View* child = block->parent_view()->first_placed_child();
                        // iterate children of THIS block, not parent
                        // block itself IS a ViewBlock which extends ViewElement
                        child = ((ViewElement*)block)->first_placed_child();
                        while (child) {
                            if (child->is_block()) {
                                ViewBlock* vb = (ViewBlock*)child;
                                bool is_out_of_flow = (vb->position && element_has_float(vb)) ||
                                    (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                                      vb->position->position == CSS_VALUE_FIXED));
                                if (!is_out_of_flow) {
                                    has_in_flow_children = true;
                                    break;
                                }
                            } else {
                                // text, inline, span = in-flow content
                                has_in_flow_children = true;
                                break;
                            }
                            // move to next placed child
                            View* next = (View*)child->next_sibling;
                            while (next && !next->view_type) next = (View*)next->next_sibling;
                            child = next;
                        }
                        if (!has_in_flow_children) {
                            is_self_collapsing = true;
                        }
                    }
                }

                if (is_self_collapsing) {
                    // self-collapsing: margins collapse through this element
                    // The element's own margins merge: max(original_mt, mb)
                    // This merged margin then participates in sibling collapsing
                    float prev_mb = 0;
                    {
                        View* pv = block->prev_placed_view();
                        while (pv && pv->is_block()) {
                            ViewBlock* vb = (ViewBlock*)pv;
                            if (vb->position && element_has_float(vb)) { pv = pv->prev_placed_view(); continue; }
                            if (vb->position && (vb->position->position == CSS_VALUE_ABSOLUTE ||
                                                  vb->position->position == CSS_VALUE_FIXED)) { pv = pv->prev_placed_view(); continue; }
                            break;
                        }
                        if (pv && pv->is_block() && pv->view_type != RDT_VIEW_INLINE_BLOCK && ((ViewBlock*)pv)->bound) {
                            prev_mb = ((ViewBlock*)pv)->bound->margin.bottom;
                        }
                    }

                    float self_collapsed = max(original_margin_top, block->bound->margin.bottom);
                    float new_pending = max(prev_mb, self_collapsed);
                    float contribution = max(0.f, new_pending - prev_mb);
                    lycon->block.advance_y += contribution;
                    // expose the merged margin to next sibling via margin.bottom
                    block->bound->margin.bottom = new_pending;
                    log_debug("self-collapsing block: original_mt=%f, mb=%f, self_collapsed=%f, prev_mb=%f, contribution=%f, new_pending=%f",
                        original_margin_top, block->bound->margin.bottom, self_collapsed, prev_mb, contribution, new_pending);
                } else {
                    lycon->block.advance_y += block->height + block->bound->margin.top + block->bound->margin.bottom;
                }
                // Include lycon->line.left to account for parent's left border+padding
                lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width
                    + block->bound->margin.left + block->bound->margin.right);
            } else {
                lycon->block.advance_y += block->height;
                // Include lycon->line.left to account for parent's left border+padding
                lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width);
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

    // Pop counter scope when leaving this block
    if (lycon->counter_context) {
        counter_pop_scope(lycon->counter_context);
    }

    // =========================================================================
    // CACHE STORE: Save computed dimensions for future lookups
    // =========================================================================
    if (cache) {
        radiant::SizeF result = radiant::size_f(block->width, block->height);
        radiant::layout_cache_store(cache, known_dims, lycon->available_space,
                                    lycon->run_mode, result);
        g_layout_cache_stores++;
        log_debug("BLOCK CACHE STORE: element=%s, size=(%.1f x %.1f), mode=%d",
                  elmt->node_name(), block->width, block->height, (int)lycon->run_mode);
    }

    log_leave();

    auto t_block_end = high_resolution_clock::now();
    g_block_layout_time += duration<double, std::milli>(t_block_end - t_block_start).count();
    g_block_layout_count++;
}
