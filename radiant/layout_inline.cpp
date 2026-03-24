#include "layout.hpp"
#include "layout_positioned.hpp"
#include "layout_table.hpp"
#include "layout_counters.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/font/font.h"
#include "../lib/strbuf.h"

// Forward declarations from layout_block.cpp for pseudo-element handling
extern PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block);
extern void generate_pseudo_element_content(LayoutContext* lycon, ViewBlock* block, bool is_before);
extern void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before);

// Check if a view child is out of normal flow (absolute, fixed, or float)
static inline bool is_out_of_flow_child(View* child) {
    DomNode* node = (DomNode*)child;
    if (node->is_element()) {
        DomElement* elem = static_cast<DomElement*>(node);
        if (elem->position) {
            if (elem->position->position == CSS_VALUE_ABSOLUTE ||
                elem->position->position == CSS_VALUE_FIXED) {
                return true;
            }
            if (elem->position->float_prop == CSS_VALUE_LEFT ||
                elem->position->float_prop == CSS_VALUE_RIGHT) {
                return true;
            }
        }
    }
    return false;
}

// Compute bounding box of a ViewSpan based on union of child views
// The bounding box includes the span's own border and padding
void compute_span_bounding_box(ViewSpan* span, bool is_multi_line, struct FontHandle* fallback_fh) {
    View* child = span->first_child;
    if (!child) {
        // Truly empty inline element (no children at all) — CSS 2.1 §9.4.2:
        // A line box is zero-height when it contains no text, no preserved whitespace,
        // and no inline elements with non-zero inline-direction decorations.
        // Per browser behavior, only horizontal (inline-direction) borders/padding/margins
        // keep a line box non-zero-height. Vertical-only borders (top/bottom) do not.
        float border_left = 0, border_right = 0, border_top = 0, border_bottom = 0;
        float pad_left = 0, pad_right = 0, pad_top = 0, pad_bottom = 0;
        if (span->bound) {
            if (span->bound->border) {
                border_left = span->bound->border->width.left;
                border_right = span->bound->border->width.right;
                border_top = span->bound->border->width.top;
                border_bottom = span->bound->border->width.bottom;
            }
            pad_left = span->bound->padding.left > 0 ? span->bound->padding.left : 0;
            pad_right = span->bound->padding.right > 0 ? span->bound->padding.right : 0;
            pad_top = span->bound->padding.top > 0 ? span->bound->padding.top : 0;
            pad_bottom = span->bound->padding.bottom > 0 ? span->bound->padding.bottom : 0;
        }
        float inline_sum = border_left + pad_left + pad_right + border_right;
        if (inline_sum > 0) {
            // CSS 2.1 §10.6.1: Horizontal decorations keep the inline box "present".
            // The inline box height includes the font content area (ascender + descender)
            // plus vertical borders and padding. Borders/padding extend around the
            // content area, not just around zero.
            float font_content_h = 0;
            struct FontHandle* fh = span->font ? span->font->font_handle : fallback_fh;
            if (fh) {
                font_content_h = font_get_cell_height(fh);
            }
            span->width = (int)inline_sum;
            span->height = (int)(font_content_h + border_top + pad_top + pad_bottom + border_bottom);
        } else {
            // No horizontal decorations — the inline box is invisible, collapse to zero
            span->width = 0;
            span->height = 0;
        }
        return;
    }

    // Skip nil-views (RDT_VIEW_NONE) and out-of-flow children (absolute/fixed) —
    // they don't participate in normal flow layout and have positions determined
    // by their containing block, not the inline span (CSS 2.1 §9.3.1, §10.6.3)
    while (child && (child->view_type == RDT_VIEW_NONE || is_out_of_flow_child(child))) {
        child = child->next();
    }
    if (!child) {
        // All children are nil-views or out-of-flow — treat as effectively empty.
        // CSS 2.1 §9.4.2: Only inline-direction (horizontal) decorations keep the
        // line box non-zero-height. If no horizontal border/padding exists, collapse.
        float border_left = 0, border_right = 0, border_top = 0, border_bottom = 0;
        float pad_left = 0, pad_right = 0, pad_top = 0, pad_bottom = 0;
        if (span->bound) {
            if (span->bound->border) {
                border_left = span->bound->border->width.left;
                border_right = span->bound->border->width.right;
                border_top = span->bound->border->width.top;
                border_bottom = span->bound->border->width.bottom;
            }
            pad_left = span->bound->padding.left > 0 ? span->bound->padding.left : 0;
            pad_right = span->bound->padding.right > 0 ? span->bound->padding.right : 0;
            pad_top = span->bound->padding.top > 0 ? span->bound->padding.top : 0;
            pad_bottom = span->bound->padding.bottom > 0 ? span->bound->padding.bottom : 0;
        }
        float inline_sum = border_left + pad_left + pad_right + border_right;
        if (inline_sum > 0) {
            float font_content_h = 0;
            struct FontHandle* fh = span->font ? span->font->font_handle : fallback_fh;
            if (fh) {
                font_content_h = font_get_cell_height(fh);
            }
            span->width = (int)inline_sum;
            span->height = (int)(font_content_h + border_top + pad_top + pad_bottom + border_bottom);
        } else {
            span->width = 0;
            span->height = 0;
        }
        return;
    }

    // Initialize bounds with first non-nil child
    int min_x = child->x;
    int min_y = child->y;
    int max_x = child->x + child->width;
    int max_y = child->y + child->height;

    // Iterate through remaining children to find union
    child = child->next();
    while (child) {
        // Skip nil-views and out-of-flow children (absolute/fixed positioned)
        if (child->view_type == RDT_VIEW_NONE || is_out_of_flow_child(child)) {
            child = child->next();
            continue;
        }
        int child_min_x = child->x;
        int child_min_y = child->y;
        int child_max_x = child->x + child->width;
        int child_max_y = child->y + child->height;

        // Expand bounding box to include this child
        if (child_min_x < min_x) min_x = child_min_x;
        if (child_min_y < min_y) min_y = child_min_y;
        if (child_max_x > max_x) max_x = child_max_x;
        if (child_max_y > max_y) max_y = child_max_y;

        child = child->next();
    }

    // Get border and padding widths
    float border_top = 0, border_right = 0, border_bottom = 0, border_left = 0;
    float pad_left = 0, pad_right = 0, pad_top = 0, pad_bottom = 0;
    if (span->bound && span->bound->border) {
        border_top = span->bound->border->width.top;
        border_right = span->bound->border->width.right;
        border_bottom = span->bound->border->width.bottom;
        border_left = span->bound->border->width.left;
    }
    if (span->bound) {
        pad_left = span->bound->padding.left > 0 ? span->bound->padding.left : 0;
        pad_right = span->bound->padding.right > 0 ? span->bound->padding.right : 0;
        pad_top = span->bound->padding.top > 0 ? span->bound->padding.top : 0;
        pad_bottom = span->bound->padding.bottom > 0 ? span->bound->padding.bottom : 0;
    }

    // CSS 2.1 §9.4.2: If children have zero content extent AND the span has no
    // horizontal (inline-direction) decorations, the span generates no visible
    // inline box — collapse to zero. This handles nested empty spans.
    float left_edge = border_left + pad_left;
    float right_edge = border_right + pad_right;
    float inline_sum = left_edge + right_edge;
    int content_width = max_x - min_x;
    int content_height = max_y - min_y;
    if (content_width == 0 && content_height == 0 && inline_sum == 0) {
        span->width = 0;
        span->height = 0;
        return;
    }

    // CSS 2.1 §8.5.1: Inline elements' border/padding appear at the start and end
    // of the inline box. For single-line spans, border+padding expand the bounding box
    // symmetrically. For multi-line spans, left border+padding only appears on the
    // first line fragment and right on the last — the union bounding box cannot simply
    // add both edges, so we skip horizontal expansion for multi-line.
    if (is_multi_line) {
        // Multi-line: don't add horizontal border+padding to union bounding box
        span->x = min_x;
        span->y = min_y - (int)border_top - (int)pad_top;
        span->width = content_width;
        span->height = content_height + (int)border_top + (int)pad_top + (int)pad_bottom + (int)border_bottom;
    } else {
        // Single-line: include horizontal border+padding in bounding box
        span->x = min_x - (int)left_edge;
        span->y = min_y - (int)border_top - (int)pad_top;
        span->width = content_width + (int)left_edge + (int)right_edge;
        span->height = content_height + (int)border_top + (int)pad_top + (int)pad_bottom + (int)border_bottom;
    }
}

// ============================================================================
// Math Element Handling
// ============================================================================

/**
 * Check if an element is a math element (has class "math" with "inline" or "display" subclass).
 * Returns: 0 = not math, 1 = inline math, 2 = display math
 */
static int detect_math_element(DomElement* elem) {
    if (!elem) return 0;

    // check for class="math inline" or class="math display"
    if (dom_element_has_class(elem, "math")) {
        if (dom_element_has_class(elem, "inline")) {
            return 1;  // inline math
        }
        if (dom_element_has_class(elem, "display")) {
            return 2;  // display math
        }
        // just "math" class defaults to inline
        return 1;
    }

    // check for tag <math>
    if (elem->tag() == HTM_TAG_MATH) {
        return 1;  // MathML element
    }

    return 0;
}

/**
 * Layout a math element.
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
static void layout_math_span(LayoutContext* lycon, DomElement* elem, bool is_display) {
    log_debug("layout_math_span: MathLive pipeline removed - use RDT_VIEW_TEXNODE instead");
    // TODO: Implement using unified TeX pipeline
    // For now, skip math rendering
    (void)lycon;
    (void)elem;
    (void)is_display;
}

/**
 * Handle inline elements containing block-level children per CSS 2.1 §9.2.1.1
 *
 * When a block box appears inside an inline box, the inline box is split into
 * anonymous inline boxes before and after the block:
 *
 * <span>Text 1 <div>Block</div> Text 2</span>
 *
 * Creates:
 * - Anonymous inline box: "Text 1"
 * - Block box: <div>Block</div>
 * - Anonymous inline box: "Text 2"
 *
 * The inline box's properties (font, color, etc.) apply to the anonymous boxes.
 */
void layout_inline_with_block_children(LayoutContext* lycon, DomElement* inline_elem,
                                        ViewSpan* span, DomNode* first_child) {
    log_debug("block-in-inline: splitting inline box for %s", inline_elem->node_name());

    // Save inline formatting context state
    Linebox saved_line = lycon->line;
    FontBox saved_font = lycon->font;
    CssEnum saved_vertical_align = lycon->line.vertical_align;

    DomNode* child = first_child;
    bool in_inline_sequence = false;
    bool had_block_child = false;

    while (child) {
        DisplayValue child_display = child->is_element() ?
            resolve_display_value(child) : DisplayValue{CSS_VALUE_INLINE, CSS_VALUE_FLOW};

        // CSS 2.1 §9.2.1.1 and §17.2.1: Block children and orphaned table-internal children
        // both break the inline flow. Table-internal elements have been wrapped in anonymous
        // block-level tables by wrap_orphaned_table_children() when block children exist.
        bool is_block_or_table_internal = child->is_element() &&
            (child_display.outer == CSS_VALUE_BLOCK ||
             child_display.outer == CSS_VALUE_LIST_ITEM ||
             child_display.outer == CSS_VALUE_TABLE ||
             is_table_internal_display(child_display.inner) ||
             is_table_internal_display(child_display.outer));

        if (is_block_or_table_internal) {
            // CSS 2.1 §9.2.1.1: Leading anonymous block strut.
            // When a block appears as the first content inside an inline with
            // inline-start border/padding, the leading anonymous block box
            // contains the span's inline-start edge, creating a line box
            // of one line-height even though there's no other inline content.
            if (!had_block_child && !in_inline_sequence && span->bound) {
                bool is_rtl = lycon->block.direction == CSS_VALUE_RTL;
                float start_border = 0, start_padding = 0;
                if (span->bound->border) {
                    start_border = is_rtl ? span->bound->border->width.right
                                          : span->bound->border->width.left;
                }
                start_padding = is_rtl ? (span->bound->padding.right > 0 ? span->bound->padding.right : 0)
                                       : (span->bound->padding.left > 0 ? span->bound->padding.left : 0);
                if (start_border > 0 || start_padding > 0) {
                    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 18.0f;
                    lycon->block.advance_y += line_height;
                    log_debug("block-in-inline: leading anonymous block strut: advance_y += %.1f (inline-start decoration)",
                              line_height);
                }
            }
            had_block_child = true;

            // Found block/table-internal child - end current inline sequence if active
            if (in_inline_sequence) {
                if (!lycon->line.is_line_start) {
                    log_debug("block-in-inline: calling line_break before block, advance_x=%.1f, max_width=%.1f",
                             lycon->line.advance_x, lycon->block.max_width);
                    line_break(lycon);
                    log_debug("block-in-inline: after line_break, advance_x=%.1f, max_width=%.1f",
                             lycon->line.advance_x, lycon->block.max_width);
                }
                in_inline_sequence = false;
            }

            // Layout block child (it breaks out of inline context)
            // The block will cause a line break and establish its own formatting context
            // IMPORTANT: Save/restore max_width because block layout will set it to container width,
            // overwriting the inline content width we just measured
            float saved_max_width = lycon->block.max_width;
            log_debug("block-in-inline: laying out block child %s", child->node_name());
            layout_block(lycon, child, child_display);
            lycon->block.max_width = saved_max_width; // Restore inline content width
            log_debug("block-in-inline: after block layout, restored max_width=%.1f", lycon->block.max_width);

        } else {
            // Inline or text content - accumulate in anonymous inline box
            if (!in_inline_sequence) {
                // Start new anonymous inline box sequence
                in_inline_sequence = true;

                // Restore inline formatting context for this anonymous box
                // This ensures the inline's font, colors, etc. apply
                // IMPORTANT: Don't restore advance_x - let it continue from current position
                // Only restore after a block break (where advance_x was already reset by line_break)
                float current_advance_x = lycon->line.advance_x;
                lycon->line = saved_line;
                lycon->line.advance_x = current_advance_x; // Preserve current X position
                lycon->line.is_line_start = (current_advance_x == lycon->line.left);
                lycon->font = saved_font;
                lycon->line.vertical_align = saved_vertical_align;

                log_debug("block-in-inline: starting anonymous inline sequence at advance_y=%f, advance_x=%f",
                         lycon->block.advance_y, lycon->line.advance_x);
            }

            log_debug("block-in-inline: laying out inline/text child %s at advance_y=%f",
                     child->node_name(), lycon->block.advance_y);
            layout_flow_node(lycon, child);
        }

        child = child->next_sibling;
    }

    // CSS 2.1 §9.2.1.1: When an inline element with border/padding is split by
    // a block child, the trailing anonymous block contains the span's continuation
    // (with the inline-end edge: border-right/padding-right in LTR, or
    // border-left/padding-left in RTL). Even if the trailing content is only
    // whitespace (which collapses), the span's inline-end edge generates a strut
    // that creates a non-zero-height line box in the trailing anonymous block.
    // Per CSS 2.1 §9.4.2, a line box is non-zero-height when it contains an inline
    // element with non-zero inline-direction padding or border. Account for this by
    // advancing advance_y by one line-height when:
    // 1. A block child was encountered (the inline was split)
    // 2. The trailing anonymous block is empty (is_line_start still true)
    // 3. The span has non-zero inline-END decoration (border/padding on the end side)
    if (!in_inline_sequence || lycon->line.is_line_start) {
        // The last content was a block child, or trailing inline content was whitespace-only.
        // Check if the span has inline-end decoration that would keep the line box open.
        bool has_inline_end_decoration = false;
        if (span->bound) {
            bool is_rtl = lycon->block.direction == CSS_VALUE_RTL;
            float end_border = 0, end_padding = 0;
            if (span->bound->border) {
                end_border = is_rtl ? span->bound->border->width.left
                                    : span->bound->border->width.right;
            }
            end_padding = is_rtl ? (span->bound->padding.left > 0 ? span->bound->padding.left : 0)
                                 : (span->bound->padding.right > 0 ? span->bound->padding.right : 0);
            has_inline_end_decoration = (end_border > 0 || end_padding > 0);
        }
        if (has_inline_end_decoration) {
            float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 18.0f;
            lycon->block.advance_y += line_height;
            log_debug("block-in-inline: trailing anonymous block strut: advance_y += %.1f (inline-end decoration)",
                      line_height);
        }
    }

    // Note: Don't call line_break() here - the caller is responsible for line breaking.
    // This function may be called for nested inlines, and the outer inline may have
    // more siblings to process on the same line.
}

void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    log_debug("layout inline %s", elmt->node_name());
    if (elmt->tag() == HTM_TAG_BR) {
        // allocate a line break view
        View* br_view = set_view(lycon, RDT_VIEW_BR, elmt);
        br_view->x = lycon->line.advance_x;  br_view->y = lycon->block.advance_y;
        br_view->width = 0;
        // The <br> element's bounding box height is the font content area (cell height),
        // not the CSS line-height. The line-height is used by line_break() to advance
        // the block cursor, but the element's own reported height matches the font metrics.
        struct FontHandle* br_fh = lycon->font.font_handle;
        br_view->height = br_fh ? font_get_cell_height(br_fh) : lycon->block.line_height;
        line_break(lycon);

        // CSS 2.1 §9.5.2: check if the <br> has a 'clear' property and apply float clearance.
        // Browsers treat <br style="clear:both"> as clearing floats at the line break point.
        if (elmt->is_element()) {
            DomElement* br_elem = static_cast<DomElement*>(elmt);
            CssEnum clear_value = CSS_VALUE_NONE;
            if (br_elem->specified_style && br_elem->specified_style->tree) {
                AvlNode* clear_node = avl_tree_search(br_elem->specified_style->tree, CSS_PROPERTY_CLEAR);
                if (clear_node) {
                    StyleNode* sn = (StyleNode*)clear_node->declaration;
                    if (sn && sn->winning_decl && sn->winning_decl->value &&
                        sn->winning_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        clear_value = sn->winning_decl->value->data.keyword;
                    }
                }
            }
            if (clear_value == CSS_VALUE_LEFT || clear_value == CSS_VALUE_RIGHT ||
                clear_value == CSS_VALUE_BOTH) {
                BlockContext* bfc = block_context_find_bfc(&lycon->block);
                if (bfc) {
                    // Progressive clear: only clear past floats whose top edge is at or
                    // above the current advance_y. This handles the case where all floats
                    // were pre-laid in the prescan and we need to clear them incrementally
                    // as the inline flow encounters each <br> clear point.
                    float current_bfc_y = lycon->block.advance_y + lycon->block.bfc_offset_y;
                    float clear_y = 0;
                    if (clear_value == CSS_VALUE_LEFT || clear_value == CSS_VALUE_BOTH) {
                        for (FloatBox* fb = bfc->left_floats; fb; fb = fb->next) {
                            if (fb->margin_box_top <= current_bfc_y && fb->margin_box_bottom > clear_y)
                                clear_y = fb->margin_box_bottom;
                        }
                    }
                    if (clear_value == CSS_VALUE_RIGHT || clear_value == CSS_VALUE_BOTH) {
                        for (FloatBox* fb = bfc->right_floats; fb; fb = fb->next) {
                            if (fb->margin_box_top <= current_bfc_y && fb->margin_box_bottom > clear_y)
                                clear_y = fb->margin_box_bottom;
                        }
                    }
                    // clear_y is in BFC coordinates; convert to local
                    float local_clear_y = clear_y - lycon->block.bfc_offset_y;
                    if (local_clear_y > lycon->block.advance_y) {
                        log_debug("<br> clear: advance_y %.1f -> %.1f (clear_y=%.1f, bfc_offset=%.1f)",
                                  lycon->block.advance_y, local_clear_y, clear_y, lycon->block.bfc_offset_y);
                        lycon->block.advance_y = local_clear_y;

                        // CSS 2.1 §9.5.2: After clearing, re-adjust the line's effective
                        // bounds and advance_x for float intrusion at the new y position.
                        // line_break() already called line_reset() which set advance_x based
                        // on float space at the pre-clear y; we must update for post-clear y.
                        lycon->line.effective_left = lycon->line.left;
                        lycon->line.effective_right = lycon->line.right;
                        lycon->line.has_float_intrusion = false;
                        lycon->line.advance_x = lycon->line.left + lycon->line.inline_start_edge_pending;
                        adjust_line_for_floats(lycon);
                    }
                }
            }
        }
        return;
    }

    // check for math elements (class="math inline" or class="math display")
    if (elmt->is_element()) {
        DomElement* elem = static_cast<DomElement*>(elmt);
        // debug: check what classes this element has
        bool has_math = dom_element_has_class(elem, "math");
        bool has_inline = dom_element_has_class(elem, "inline");
        log_debug("layout_inline: checking %s has_math=%d has_inline=%d",
                  elem->node_name(), has_math, has_inline);
        int math_type = detect_math_element(elem);
        if (math_type > 0) {
            bool is_display = (math_type == 2);
            log_debug("layout_inline: detected math element, type=%d", math_type);
            layout_math_span(lycon, elem, is_display);
            return;
        }
    }

    // save parent context
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // unresolved yet
    CssEnum pa_line_align = lycon->line.vertical_align;
    float pa_valign_offset = lycon->line.vertical_align_offset;
    lycon->elmt = elmt;

    ViewSpan* span = (ViewSpan*)set_view(lycon, RDT_VIEW_INLINE, elmt);
    span->x = lycon->line.advance_x;  span->y = lycon->block.advance_y;
    span->width = 0;  span->height = 0;
    span->display = display;

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    // CSS Counter handling (CSS 2.1 Section 12.4, CSS Lists 3)
    // Push a new counter scope for this inline element so that counter-reset
    // creates a properly nested counter instance (not modifying the parent scope)
    bool pushed_counter_scope = false;
    if (lycon->counter_context) {
        counter_push_scope(lycon->counter_context);
        pushed_counter_scope = true;
    }

    // Apply counter operations for this inline element
    if (lycon->counter_context && span->blk) {
        // Apply counter-reset if specified
        if (span->blk->counter_reset) {
            log_debug("    [Inline] Applying counter-reset: %s", span->blk->counter_reset);
            counter_reset(lycon->counter_context, span->blk->counter_reset);
        }

        // Apply counter-increment if specified
        if (span->blk->counter_increment) {
            log_debug("    [Inline] Applying counter-increment: %s", span->blk->counter_increment);
            counter_increment(lycon->counter_context, span->blk->counter_increment);
        }

        // Apply counter-set if specified (CSS Lists 3)
        // Processed after counter-reset and counter-increment per spec
        if (span->blk->counter_set) {
            log_debug("    [Inline] Applying counter-set: %s", span->blk->counter_set);
            counter_set(lycon->counter_context, span->blk->counter_set);
        }
    }

    // Allocate pseudo-element content if ::before or ::after is present
    // Inline elements can have pseudo-elements too (e.g., <span>::before)
    if (elmt->is_element()) {
        DomElement* elem = static_cast<DomElement*>(elmt);
        elem->pseudo = alloc_pseudo_content_prop(lycon, (ViewBlock*)span);

        // Generate pseudo-element content from CSS content property
        generate_pseudo_element_content(lycon, (ViewBlock*)span, true);   // ::before
        generate_pseudo_element_content(lycon, (ViewBlock*)span, false);  // ::after

        // Insert pseudo-elements into DOM tree for proper view tree linking
        if (elem->pseudo) {
            if (elem->pseudo->before) {
                insert_pseudo_into_dom(elem, elem->pseudo->before, true);
            }
            if (elem->pseudo->after) {
                insert_pseudo_into_dom(elem, elem->pseudo->after, false);
            }
        }
    }

    if (span->font) {
        setup_font(lycon->ui_context, &lycon->font,  span->font);
    }
    if (span->in_line && span->in_line->vertical_align) {
        lycon->line.vertical_align = span->in_line->vertical_align;
        lycon->line.vertical_align_offset = span->in_line->vertical_align_offset;
    }

    // CSS 2.1 §10.8.1: Each inline box uses its own 'line-height' property for
    // the half-leading model, not the parent block's line-height. Save the block's
    // line-height and resolve the inline element's own line-height if specified.
    float pa_line_height = lycon->block.line_height;
    bool pa_line_height_is_normal = lycon->block.line_height_is_normal;
    // Check the element's specified_style for an explicit line-height or font shorthand
    // declaration. If neither is present, line-height is inherited and the parent's
    // resolved value is already correct in lycon->block.line_height — UNLESS the
    // inherited line-height is a <number> value and the font-size changed.
    // CSS 2.1: <number> line-heights inherit the number, not the computed length,
    // so each element must recompute: number × own font-size.
    bool has_own_line_height = false;
    if (elmt->is_element()) {
        DomElement* dom_elmt = static_cast<DomElement*>(elmt);
        if (dom_elmt->specified_style) {
            has_own_line_height =
                style_tree_get_declaration(dom_elmt->specified_style, CSS_PROPERTY_LINE_HEIGHT) != nullptr ||
                style_tree_get_declaration(dom_elmt->specified_style, CSS_PROPERTY_FONT) != nullptr;
        }
    }
    // Also re-resolve when font-size changed AND inherited line-height is a
    // <number> or 'normal'. CSS 2.1: number line-heights inherit the number (not
    // the computed length), so each element recomputes: number × own font-size.
    // Length/percentage line-heights inherit the computed absolute value and must
    // NOT be re-resolved against the child's font-size.
    bool font_size_changed = lycon->font.style && pa_font.style &&
        lycon->font.style->font_size != pa_font.style->font_size;
    if (has_own_line_height || font_size_changed) {
        if (has_own_line_height) {
            setup_line_height(lycon, (ViewBlock*)span);
        } else {
            // font-size changed: only re-resolve for number/normal line-height
            CssValue inherited_lh = inherit_line_height(lycon, (ViewBlock*)span);
            if (inherited_lh.type == CSS_VALUE_TYPE_NUMBER ||
                (inherited_lh.type == CSS_VALUE_TYPE_KEYWORD &&
                 inherited_lh.data.keyword == CSS_VALUE_NORMAL)) {
                setup_line_height(lycon, (ViewBlock*)span);
            }
        }
        // Track if this inline's line-height exceeds the parent block's,
        // so line_break() knows to respect the expanded line box height.
        if (lycon->block.line_height > pa_line_height) {
            lycon->line.has_expanded_inline_lh = true;
        }
    }
    // line.max_ascender and max_descender to be changed only when there's output from the span

    // layout inline content
    DomNode *child = nullptr;
    if (elmt->is_element()) {
        child = static_cast<DomElement*>(elmt)->first_child;
    }

    // CSS 2.1 §8.3: Inline elements' margin/border/padding push content inward.
    // Advance the inline cursor so child text/elements start inside the margin+border+padding.
    // This applies to both normal inline content and block-in-inline splitting.
    float inline_left_edge = 0;
    float inline_right_edge = 0;
    if (span->bound) {
        // CSS 2.1 §8.3: horizontal margins apply to inline elements
        inline_left_edge += span->bound->margin.left;
        inline_right_edge += span->bound->margin.right;
        if (span->bound->border) {
            inline_left_edge += span->bound->border->width.left;
            inline_right_edge += span->bound->border->width.right;
        }
        inline_left_edge += span->bound->padding.left;
        inline_right_edge += span->bound->padding.right;
    }
    // CSS 2.1 §8.3: Track pending inline left edges for line break re-application.
    // If the span's first content wraps to a new line, line_reset() will re-apply
    // the pending edges so the content starts correctly indented on the new line.
    float saved_inline_pending = lycon->line.inline_start_edge_pending;
    lycon->line.inline_start_edge_pending += inline_left_edge;
    lycon->line.advance_x += inline_left_edge;

    // CSS 2.1 §9.2.1.1 and §17.2.1: Check for block-level and table-internal children
    // We need to handle two different scenarios:
    // 1. If ONLY table-internal children (no blocks) → wrap in anonymous inline-table
    // 2. If block children exist → table-internal treated as block-breaking (block-in-inline)
    bool has_block_children = false;
    bool has_table_internal = false;
    DomNode* scan = child;
    while (scan) {
        if (scan->is_element()) {
            DisplayValue child_display = resolve_display_value(scan);
            log_debug("block-in-inline scan: child=%s outer=%d inner=%d",
                     scan->node_name(), child_display.outer, child_display.inner);
            if (child_display.outer == CSS_VALUE_BLOCK ||
                child_display.outer == CSS_VALUE_LIST_ITEM ||
                child_display.outer == CSS_VALUE_TABLE) {
                // CSS 2.1 §9.6.1: Absolutely/fixed positioned elements are out of flow
                // and should not trigger block-in-inline splitting, even though their
                // display is blockified per §9.7.
                DomElement* child_elem = scan->as_element();
                bool child_is_abspos = false;
                if (child_elem->position &&
                    (child_elem->position->position == CSS_VALUE_ABSOLUTE ||
                     child_elem->position->position == CSS_VALUE_FIXED)) {
                    child_is_abspos = true;
                } else if (child_elem->specified_style && child_elem->specified_style->tree) {
                    AvlNode* pos_node = avl_tree_search(child_elem->specified_style->tree, CSS_PROPERTY_POSITION);
                    if (pos_node) {
                        StyleNode* sn = (StyleNode*)pos_node->declaration;
                        if (sn && sn->winning_decl && sn->winning_decl->value &&
                            sn->winning_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                            (sn->winning_decl->value->data.keyword == CSS_VALUE_ABSOLUTE ||
                             sn->winning_decl->value->data.keyword == CSS_VALUE_FIXED)) {
                            child_is_abspos = true;
                        }
                    }
                }
                if (!child_is_abspos) {
                    has_block_children = true;
                }
            }
            if (is_table_internal_display(child_display.inner) ||
                is_table_internal_display(child_display.outer)) {
                has_table_internal = true;
            }
        }
        scan = scan->next_sibling;
    }

    // CSS 2.1 §17.2.1: When only table-internal children exist (no block children),
    // wrap them in anonymous inline-table to participate in inline flow
    if (has_table_internal && !has_block_children) {
        wrap_orphaned_table_children(lycon, static_cast<DomElement*>(elmt));
        child = static_cast<DomElement*>(elmt)->first_child;
    }

    // If block children exist, table-internal children act as block-breaking
    if (has_block_children && has_table_internal) {
        log_debug("block-in-inline detected: %s has both block and table-internal children",
                 elmt->node_name());
    }

    if (has_block_children || (has_table_internal && has_block_children)) {
        // When block children exist alongside table-internal, handle via block-in-inline
        // splitting. The anonymous block wrappers created by splitting will later call
        // wrap_orphaned_table_children() during their own layout in layout_block.cpp.

        // Handle block-in-inline splitting
        float pre_split_advance_y = lycon->block.advance_y;
        layout_inline_with_block_children(lycon, static_cast<DomElement*>(elmt), span, child);

        // Advance past the right border+padding
        lycon->line.advance_x += inline_right_edge;

        // CSS 2.1 §9.2.1.1: When an inline element contains block-level children,
        // it is split into anonymous block boxes spanning the full content width of
        // the containing block. The inline element's bounding box should encompass
        // this full extent, so getBoundingClientRect() matches the browser.
        // Compute vertical union from children, horizontal from the parent block.
        compute_span_bounding_box(span, true, lycon->font.font_handle);  // get vertical bounds from children

        // CSS 2.1 §9.2.1.1: For relatively-positioned block-in-inline spans,
        // override y with the flow position. compute_span_bounding_box uses
        // children's visual positions (after their own relative positioning),
        // which would contaminate the span's base position for its own offset.
        if (span->position && span->position->position == CSS_VALUE_RELATIVE) {
            span->y = (int)pre_split_advance_y;
        }

        // CSS 2.1 §9.2.1.1: Extend span bounding box upward to cover the leading
        // anonymous block's strut. When the span has inline-start border/padding,
        // the leading strut creates a line box before the first block child.
        // The span's bbox should extend up to include this strut area.
        {
            bool has_inline_start = false;
            if (span->bound) {
                bool is_rtl = lycon->block.direction == CSS_VALUE_RTL;
                float start_border = 0, start_padding = 0;
                if (span->bound->border) {
                    start_border = is_rtl ? span->bound->border->width.right
                                          : span->bound->border->width.left;
                }
                start_padding = is_rtl ? (span->bound->padding.right > 0 ? span->bound->padding.right : 0)
                                       : (span->bound->padding.left > 0 ? span->bound->padding.left : 0);
                has_inline_start = (start_border > 0 || start_padding > 0);
            }
            if (has_inline_start) {
                float border_top = 0, pad_top = 0;
                if (span->bound) {
                    if (span->bound->border)
                        border_top = span->bound->border->width.top;
                    if (span->bound->padding.top > 0)
                        pad_top = span->bound->padding.top;
                }
                float strut_top = pre_split_advance_y - border_top - pad_top;
                if (strut_top < span->y) {
                    int old_y = span->y;
                    span->height += (old_y - (int)strut_top);
                    span->y = (int)strut_top;
                    log_debug("block-in-inline: extended span y upward from %d to %d (leading strut)",
                              old_y, span->y);
                }
            }
        }

        // CSS 2.1 §9.2.1.1: Extend span bounding box to cover the trailing anonymous
        // block's strut. Only do this when the span has inline-end decorations that
        // caused a trailing strut to be generated. Without inline-end decorations,
        // advance_y may include the block child's margins which should NOT be part
        // of the span's bounding box (margins are outside the border box).
        {
            bool has_inline_end = false;
            if (span->bound) {
                bool is_rtl = lycon->block.direction == CSS_VALUE_RTL;
                float end_border = 0, end_padding = 0;
                if (span->bound->border) {
                    end_border = is_rtl ? span->bound->border->width.left
                                        : span->bound->border->width.right;
                }
                end_padding = is_rtl ? (span->bound->padding.left > 0 ? span->bound->padding.left : 0)
                                     : (span->bound->padding.right > 0 ? span->bound->padding.right : 0);
                has_inline_end = (end_border > 0 || end_padding > 0);
            }
            if (has_inline_end) {
                float span_bottom = span->y + span->height;
                float flow_bottom = lycon->block.advance_y;
                if (flow_bottom > span_bottom) {
                    float border_bottom = 0, pad_bottom = 0;
                    if (span->bound) {
                        if (span->bound->border)
                            border_bottom = span->bound->border->width.bottom;
                        if (span->bound->padding.bottom > 0)
                            pad_bottom = span->bound->padding.bottom;
                    }
                    span->height = (int)(flow_bottom - span->y + border_bottom + pad_bottom);
                    log_debug("block-in-inline: extended span height to %d (flow_bottom=%.1f, span_y=%d)",
                              span->height, flow_bottom, span->y);
                }
            }
        }

        // Override horizontal bounds to match the parent block's content area
        ViewElement* parent_block = span->parent_view();
        if (parent_block && parent_block->is_block()) {
            ViewBlock* pb = (ViewBlock*)parent_block;
            float pb_border_left = 0, pb_border_right = 0;
            float pb_pad_left = 0, pb_pad_right = 0;
            if (pb->bound) {
                if (pb->bound->border) {
                    pb_border_left = pb->bound->border->width.left;
                    pb_border_right = pb->bound->border->width.right;
                }
                pb_pad_left = pb->bound->padding.left > 0 ? pb->bound->padding.left : 0;
                pb_pad_right = pb->bound->padding.right > 0 ? pb->bound->padding.right : 0;
            }
            float content_left = pb_border_left + pb_pad_left;
            float content_width = pb->width - pb_border_left - pb_border_right - pb_pad_left - pb_pad_right;
            if (content_width < 0) content_width = 0;
            span->x = (int)content_left;
            span->width = (int)content_width;
            log_debug("block-in-inline: span bounds set to parent content area: x=%d, w=%d (pb_w=%d, bl=%f, br=%f)",
                      span->x, span->width, pb->width, pb_border_left, pb_border_right);
        }

        // Apply CSS relative/sticky positioning after normal layout
        if (span->position && span->position->position == CSS_VALUE_RELATIVE) {
            log_debug("Applying relative positioning to inline span (with block children)");
            layout_relative_positioned(lycon, (ViewBlock*)span);
        } else if (span->position && span->position->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, (ViewBlock*)span);
        }

        lycon->font = pa_font;
        lycon->line.vertical_align = pa_line_align;
        lycon->block.line_height = pa_line_height;
        lycon->block.line_height_is_normal = pa_line_height_is_normal;
        if (pushed_counter_scope) {
            // Propagate counter-reset counters for real elements (sibling visibility)
            // but not for pseudo-elements (their counter-reset stays scoped)
            bool is_pseudo = elmt->is_element() &&
                static_cast<DomElement*>(elmt)->tag_name &&
                static_cast<DomElement*>(elmt)->tag_name[0] == ':';
            counter_pop_scope_propagate(lycon->counter_context, !is_pseudo);
        }
        return;
    }

    // Normal inline-only content
    bool had_children = (child != nullptr);
    float start_advance_y = lycon->block.advance_y;
    if (child) {
        log_debug("layout inline children: advance_y %f, line_height %f", lycon->block.advance_y, lycon->block.line_height);
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        } while (child);
    }

    // Advance past the right border+padding so the next sibling starts after this inline's border
    lycon->line.advance_x += inline_right_edge;

    // CSS 2.1 §8.3: Now that this span is closing, remove its contribution from
    // the pending inline edges if it wasn't consumed by content output.
    // If output_text() was called inside this span, pending was cleared to 0.
    // If no content was output (empty span), restore saved value so the
    // parent's pending state is unaffected.
    if (lycon->line.inline_start_edge_pending > saved_inline_pending) {
        lycon->line.inline_start_edge_pending = saved_inline_pending;
    }

    // CSS 2.1 §10.8.1: For non-replaced inline elements, the inline box height
    // equals its 'line-height', computed via the half-leading model. Even empty
    // inline elements contribute their strut to the line box height calculation.
    // When children produce text output, output_text() applies the half-leading;
    // for empty inlines (no children at all), we must apply it here explicitly.
    if (!had_children && lycon->font.font_handle) {
        TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
        float ascender = 0, descender = 0;
        if (typo.valid && typo.use_typo_metrics) {
            ascender = typo.ascender;
            descender = typo.descender;
        } else {
            const FontMetrics* m = font_get_metrics(lycon->font.font_handle);
            if (m) {
                ascender = m->hhea_ascender;
                descender = -(m->hhea_descender);
            }
        }
        if (ascender > 0 || descender > 0) {
            if (!lycon->block.line_height_is_normal) {
                float content_height = ascender + descender;
                float half_leading = (lycon->block.line_height - content_height) / 2.0f;
                ascender += half_leading;
                descender += half_leading;
            }
            float va_offset = lycon->line.vertical_align_offset;
            if (va_offset != 0) {
                ascender += va_offset;
                descender -= va_offset;
            }
            lycon->line.max_ascender = max(lycon->line.max_ascender, ascender);
            lycon->line.max_descender = max(lycon->line.max_descender, descender);
            if (lycon->block.line_height_is_normal) {
                float normal_lh = font_calc_normal_line_height(lycon->font.font_handle);
                lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height, normal_lh);
            }
            log_debug("empty inline strut: ascender=%.1f, descender=%.1f, line_height=%.1f",
                     ascender, descender, lycon->block.line_height);
        }
        // Mark empty inline span for height fixup in view_vertical_align.
        // compute_span_bounding_box sets height=0, but the inline box should
        // report line-height when on a line with visible content.
        span->content_height = lycon->block.line_height;
        // CSS 2.1 §9.4.2: An inline element with non-zero margins, borders, or
        // padding makes the line box non-empty (not subject to zero-height rule).
        // Mark the line as having content so line_break() is called at finalization.
        if (inline_left_edge > 0 || inline_right_edge > 0) {
            lycon->line.is_line_start = false;
        }
    }

    // CSS 2.1 §8.5.1: Detect multi-line by checking if children are on different lines.
    // Multi-line means the span's content itself spans multiple lines, requiring
    // left border+padding only on the first line fragment and right on the last.
    bool span_is_multi_line = false;
    {
        View* first_content = span->first_child;
        // skip nil-views and out-of-flow children
        while (first_content && (first_content->view_type == RDT_VIEW_NONE || is_out_of_flow_child(first_content)))
            first_content = first_content->next();
        if (first_content) {
            float first_y = first_content->y;
            // Check 1: different DOM children on different y-positions
            View* c = first_content->next();
            while (c) {
                if (c->view_type != RDT_VIEW_NONE && !is_out_of_flow_child(c)) {
                    if (c->y != first_y) {
                        span_is_multi_line = true;
                        break;
                    }
                }
                c = c->next();
            }
            // Check 2: advance_y moved during children layout, meaning a line
            // break occurred while laying out this span's content (text wrapped).
            // start_advance_y was captured before children layout, so if advance_y
            // increased, a line_break() was called inside this span.
            if (!span_is_multi_line) {
                if (lycon->block.advance_y > start_advance_y) {
                    span_is_multi_line = true;
                }
            }
        }
    }

    // CSS 2.1 §16.6.1: Trailing whitespace at end of a line should not expand
    // the inline element's bounding box. We trim trailing whitespace from the
    // span bounding box only when the span is the last inline content on the line
    // (i.e., followed by a block element or end of parent). When more inline
    // content follows, the trailing space is inter-element spacing and should
    // be included in the span's bounding box.
    float saved_trailing = 0;
    View* last_child_for_trim = nullptr;
    if (lycon->line.trailing_space_width > 0) {
        // Check if there's more inline content after this span on the same line
        bool has_following_inline = false;
        DomNode* sib = ((DomNode*)span)->next_sibling;
        while (sib) {
            if (sib->is_element()) {
                DomElement* elem = sib->as_element();
                DisplayValue dv = resolve_display_value(elem);
                if (dv.outer == CSS_VALUE_INLINE) {
                    has_following_inline = true;
                }
                break;  // any element determines the answer
            } else if (sib->is_text()) {
                DomText* tn = sib->as_text();
                if (tn && tn->text && tn->length > 0) {
                    // Check if it's all whitespace
                    bool all_ws = true;
                    for (size_t i = 0; i < tn->length; i++) {
                        char c = tn->text[i];
                        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                            all_ws = false;
                            break;
                        }
                    }
                    if (!all_ws) {
                        has_following_inline = true;
                        break;
                    }
                }
            }
            sib = sib->next_sibling;
        }

        // CSS 2.1 §16.6.1: If no following inline content at this level, walk up
        // the inline parent chain. The trailing whitespace should only be trimmed
        // if the entire inline ancestor chain ends the line — not just the current
        // inline element. A nested inline at end-of-parent still has trailing space
        // visible if the parent inline has more content following.
        if (!has_following_inline) {
            DomNode* ancestor = ((DomNode*)span)->parent;
            while (ancestor && !has_following_inline) {
                if (!ancestor->is_element()) break;
                DomElement* anc_elem = ancestor->as_element();
                if (!anc_elem) break;
                // Stop at block-level or non-inline-flow ancestors
                if (anc_elem->view_type >= RDT_VIEW_INLINE_BLOCK) break;
                if (anc_elem->view_type != RDT_VIEW_INLINE) break;
                // Check ancestor's following siblings for inline content
                DomNode* anc_sib = ancestor->next_sibling;
                while (anc_sib) {
                    if (anc_sib->is_element()) {
                        DomElement* se = anc_sib->as_element();
                        DisplayValue sdv = resolve_display_value(se);
                        if (sdv.outer == CSS_VALUE_INLINE) {
                            has_following_inline = true;
                        }
                        break;
                    } else if (anc_sib->is_text()) {
                        DomText* tn = anc_sib->as_text();
                        if (tn && tn->text && tn->length > 0) {
                            bool all_ws = true;
                            for (size_t i = 0; i < tn->length; i++) {
                                char c = tn->text[i];
                                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                                    all_ws = false;
                                    break;
                                }
                            }
                            if (!all_ws) {
                                has_following_inline = true;
                                break;
                            }
                        }
                    }
                    anc_sib = anc_sib->next_sibling;
                }
                ancestor = ancestor->parent;
            }
        }

        if (!has_following_inline) {
            // Span is last inline content on this line — trim trailing whitespace
            View* c = span->first_child;
            while (c) {
                if (c->view_type) last_child_for_trim = c;
                c = c->next();
            }
            if (last_child_for_trim && last_child_for_trim->view_type != RDT_VIEW_INLINE) {
                // Only trim non-span children (text rects, inline-blocks, etc.).
                // Inline spans already handled their own trailing space trim
                // during their own layout pass — trimming again would double-count.
                saved_trailing = lycon->line.trailing_space_width;
                last_child_for_trim->width -= saved_trailing;
            }
        }
    }

    // CSS 2.1 §10.6.1: Store span's resolved line-height for use by
    // view_vertical_align() to compute the inline box Y/height.
    // For inline non-replaced elements, the inline box height equals line-height,
    // not the union of children's visual extent.
    span->content_height = lycon->block.line_height;

    compute_span_bounding_box(span, span_is_multi_line, lycon->font.font_handle);

    // CSS 2.1 §10.6.1: For inline non-replaced elements, the bounding box height
    // should reflect the font's content area + border + padding, not the union of
    // children's visual extent. When children contain tall replaced elements (images,
    // inline-blocks) that extend beyond the font content area, cap the span's height.
    // Use font_get_cell_height() which matches the text rect height and browser behavior.
    if (span->height > 0) {
        struct FontHandle* fh = span->font ? span->font->font_handle : lycon->font.font_handle;
        if (fh) {
            float content_area = font_get_cell_height(fh);
            float bt = 0, bb = 0, pt_val = 0, pb_val = 0;
            if (span->bound) {
                if (span->bound->border) {
                    bt = span->bound->border->width.top;
                    bb = span->bound->border->width.bottom;
                }
                pt_val = span->bound->padding.top > 0 ? span->bound->padding.top : 0;
                pb_val = span->bound->padding.bottom > 0 ? span->bound->padding.bottom : 0;
            }
            int expected_height = (int)(content_area + bt + pt_val + pb_val + bb);
            if (!span_is_multi_line && span->height > expected_height) {
                span->height = expected_height;
                log_debug("inline span height capped to content area: %d (area=%.1f)", expected_height, content_area);
            }
        }
    }

    // CSS 2.1 §10.8.1: Mark collapsed-content inline spans for line-break fixup.
    // When an inline element has children but all content collapsed (whitespace-only),
    // the span gets 0×0 from compute_span_bounding_box. However, per CSS 2.1, the
    // inline box still contributes its line-height to the line box. We store the
    // span's resolved line-height in content_height as a marker; line_break() will
    // set the height if the line turns out to have visible content.
    if (span->height == 0 && span->width == 0 && span->first_child) {
        // Check if all children are collapsed (RDT_VIEW_NONE)
        bool all_collapsed = true;
        DomNode* chk = static_cast<DomElement*>(elmt)->first_child;
        while (chk) {
            if (chk->view_type != RDT_VIEW_NONE) {
                all_collapsed = false;
                break;
            }
            chk = chk->next_sibling;
        }
        if (all_collapsed) {
            span->content_height = lycon->block.line_height;
            log_debug("marking collapsed inline span %s for line-height fixup (lh=%.1f)",
                     elmt->node_name(), lycon->block.line_height);
        }
    }

    // Restore the trailing space on the child so advance_x remains correct
    // for any subsequent layout processing
    if (last_child_for_trim && saved_trailing > 0) {
        last_child_for_trim->width += saved_trailing;
    }

    // Apply CSS relative/sticky positioning after normal layout
    // CSS 2.1 §9.4.3: Relatively positioned inline elements are offset from their normal position
    if (span->position && span->position->position == CSS_VALUE_RELATIVE) {
        log_debug("Applying relative positioning to inline span");
        layout_relative_positioned(lycon, (ViewBlock*)span);
    } else if (span->position && span->position->position == CSS_VALUE_STICKY) {
        layout_sticky_positioned(lycon, (ViewBlock*)span);
    }

    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    lycon->line.vertical_align_offset = pa_valign_offset;
    lycon->block.line_height = pa_line_height;
    lycon->block.line_height_is_normal = pa_line_height_is_normal;
    if (pushed_counter_scope) {
        bool is_pseudo = elmt->is_element() &&
            static_cast<DomElement*>(elmt)->tag_name &&
            static_cast<DomElement*>(elmt)->tag_name[0] == ':';
        counter_pop_scope_propagate(lycon->counter_context, !is_pseudo);
    }
    log_debug("inline span view: %d, child %p, x:%.0f, y:%.0f, wd:%.0f, hg:%.0f", span->view_type,
        span->first_child, span->x, span->y, span->width, span->height);
}
