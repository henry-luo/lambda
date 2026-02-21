#include "layout.hpp"
#include "layout_positioned.hpp"
#include "layout_table.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/strbuf.h"

// Forward declarations from layout_block.cpp for pseudo-element handling
extern PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block);
extern void generate_pseudo_element_content(LayoutContext* lycon, ViewBlock* block, bool is_before);
extern void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before);

// Compute bounding box of a ViewSpan based on union of child views
// The bounding box includes the span's own border and padding
void compute_span_bounding_box(ViewSpan* span) {
    View* child = span->first_child;
    if (!child) {
        // If no child views, keep current position and zero size
        span->width = 0;
        span->height = 0;
        return;
    }

    // Initialize bounds with first child
    int min_x = child->x;
    int min_y = child->y;
    int max_x = child->x + child->width;
    int max_y = child->y + child->height;

    // Iterate through remaining children to find union
    child = child->next();
    while (child) {
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

    // Get border widths if span has border
    float border_top = 0, border_right = 0, border_bottom = 0, border_left = 0;
    if (span->bound && span->bound->border) {
        border_top = span->bound->border->width.top;
        border_right = span->bound->border->width.right;
        border_bottom = span->bound->border->width.bottom;
        border_left = span->bound->border->width.left;
    }

    // Update span's bounding box - expand to include border (vertical always, horizontal for single-line)
    // For inline elements that may span multiple lines, the horizontal border
    // only appears at start/end of the overall inline box, but vertical border
    // affects each line. The bounding box Y position should include top border.
    span->x = min_x;
    span->y = min_y - (int)border_top;
    span->width = max_x - min_x;
    span->height = (max_y - min_y) + (int)border_top + (int)border_bottom;
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

    while (child) {
        DisplayValue child_display = child->is_element() ?
            resolve_display_value(child) : DisplayValue{CSS_VALUE_INLINE, CSS_VALUE_FLOW};

        // CSS 2.1 §9.2.1.1 and §17.2.1: Block children and orphaned table-internal children
        // both break the inline flow (table-internal elements get anonymous table wrappers)
        bool is_block_or_table_internal = child->is_element() &&
            (child_display.outer == CSS_VALUE_BLOCK ||
             is_table_internal_display(child_display.inner) ||
             is_table_internal_display(child_display.outer));

        if (is_block_or_table_internal) {
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
        br_view->width = 0;  br_view->height = lycon->block.line_height;
        line_break(lycon);
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
    lycon->elmt = elmt;

    ViewSpan* span = (ViewSpan*)set_view(lycon, RDT_VIEW_INLINE, elmt);
    span->x = lycon->line.advance_x;  span->y = lycon->block.advance_y;
    span->width = 0;  span->height = 0;
    span->display = display;

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    // CSS Counter handling (CSS 2.1 Section 12.4)
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
    }
    // line.max_ascender and max_descender to be changed only when there's output from the span

    // layout inline content
    DomNode *child = nullptr;
    if (elmt->is_element()) {
        child = static_cast<DomElement*>(elmt)->first_child;
    }

    // CSS 2.1 §8.3: Inline elements' border-left/padding-left push content inward.
    // Advance the inline cursor so child text/elements start inside the border+padding.
    // This applies to both normal inline content and block-in-inline splitting.
    float inline_left_edge = 0;
    float inline_right_edge = 0;
    if (span->bound) {
        if (span->bound->border) {
            inline_left_edge += span->bound->border->width.left;
            inline_right_edge += span->bound->border->width.right;
        }
        inline_left_edge += span->bound->padding.left;
        inline_right_edge += span->bound->padding.right;
    }
    lycon->line.advance_x += inline_left_edge;

    // CSS 2.1 §9.2.1.1 and §17.2.1: Check if inline contains block-level or table-internal children
    // If so, split into anonymous boxes (and wrap table-internal in anonymous tables)
    bool has_block_children = false;
    DomNode* scan = child;
    while (scan) {
        if (scan->is_element()) {
            DisplayValue child_display = resolve_display_value(scan);
            log_debug("block-in-inline scan: child=%s outer=%d inner=%d",
                     scan->node_name(), child_display.outer, child_display.inner);
            // CSS 2.1 §9.2.1.1: Block children break inline flow
            // CSS 2.1 §17.2.1: Table-internal children also break inline flow
            if (child_display.outer == CSS_VALUE_BLOCK ||
                is_table_internal_display(child_display.inner) ||
                is_table_internal_display(child_display.outer)) {
                has_block_children = true;
                log_debug("block-in-inline detected: %s contains block/table-internal child %s",
                         elmt->node_name(), scan->node_name());
                break;
            }
        }
        scan = scan->next_sibling;
    }

    if (has_block_children) {
        // CSS 2.1 §17.2.1: Before handling block-in-inline splitting,
        // wrap any orphaned table-internal children in anonymous table structures
        wrap_orphaned_table_children(lycon, static_cast<DomElement*>(elmt));
        // Re-get first child after wrapping may have inserted anonymous elements
        child = static_cast<DomElement*>(elmt)->first_child;

        // Handle block-in-inline splitting
        layout_inline_with_block_children(lycon, static_cast<DomElement*>(elmt), span, child);

        // Advance past the right border+padding
        lycon->line.advance_x += inline_right_edge;
        compute_span_bounding_box(span);

        // Apply CSS relative positioning after normal layout
        if (span->position && span->position->position == CSS_VALUE_RELATIVE) {
            log_debug("Applying relative positioning to inline span (with block children)");
            layout_relative_positioned(lycon, (ViewBlock*)span);
        }

        lycon->font = pa_font;
        lycon->line.vertical_align = pa_line_align;
        return;
    }

    // Normal inline-only content
    if (child) {
        log_debug("layout inline children: advance_y %f, line_height %f", lycon->block.advance_y, lycon->block.line_height);
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        } while (child);
    }

    // Advance past the right border+padding so the next sibling starts after this inline's border
    lycon->line.advance_x += inline_right_edge;

    compute_span_bounding_box(span);

    // Apply CSS relative positioning after normal layout
    // CSS 2.1 §9.4.3: Relatively positioned inline elements are offset from their normal position
    if (span->position && span->position->position == CSS_VALUE_RELATIVE) {
        log_debug("Applying relative positioning to inline span");
        layout_relative_positioned(lycon, (ViewBlock*)span);
    }

    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    log_debug("inline span view: %d, child %p, x:%d, y:%d, wd:%d, hg:%d", span->view_type,
        span->first_child, span->x, span->y, span->width, span->height);
}
