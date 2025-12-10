#include "layout.hpp"

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

    // Update span's bounding box - expand to include vertical border only
    // For inline elements that may span multiple lines, the horizontal border
    // only appears at start/end of the overall inline box, but vertical border
    // affects each line. The bounding box Y position should include top border.
    span->x = min_x;
    span->y = min_y - (int)border_top;
    span->width = max_x - min_x;
    span->height = (max_y - min_y) + (int)border_top + (int)border_bottom;
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
    if (child) {
        log_debug("layout inline children: advance_y %f, line_height %f", lycon->block.advance_y, lycon->block.line_height);
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        } while (child);
    }

    compute_span_bounding_box(span);
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    log_debug("inline span view: %d, child %p, x:%d, y:%d, wd:%d, hg:%d", span->view_type,
        span->first_child, span->x, span->y, span->width, span->height);
}
