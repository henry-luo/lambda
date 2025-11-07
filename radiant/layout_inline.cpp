#include "layout.hpp"

// Compute bounding box of a ViewSpan based on union of child views
void compute_span_bounding_box(ViewSpan* span) {
    View* child = span->child;
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
    child = child->next;
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

        child = child->next;
    }

    // Update span's bounding box
    span->x = min_x;
    span->y = min_y;
    span->width = max_x - min_x;
    span->height = max_y - min_y;
}

void resolve_inline_default(LayoutContext* lycon, ViewSpan* span) {
    uintptr_t elmt_name = span->node->tag();
    switch (elmt_name) {
    case LXB_TAG_B:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_weight = LXB_CSS_VALUE_BOLD;
        break;
    case LXB_TAG_I:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_style = LXB_CSS_VALUE_ITALIC;
        break;
    case LXB_TAG_U:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->text_deco = LXB_CSS_VALUE_UNDERLINE;
        break;
    case LXB_TAG_S:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->text_deco = LXB_CSS_VALUE_LINE_THROUGH;
        break;
    case LXB_TAG_FONT: {
        // parse font style
        // Get color attribute using DomNode interface
        const char* color_attr = span->node->get_attribute("color");
        if (color_attr) {
            log_debug("font color: %s", color_attr);
        }
        break;
    }
    case LXB_TAG_A: {
        // anchor style
        if (!span->in_line) { span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
        span->in_line->cursor = LXB_CSS_VALUE_POINTER;
        span->in_line->color = color_name_to_rgb(LXB_CSS_VALUE_BLUE);
        span->font = alloc_font_prop(lycon);
        span->font->text_deco = LXB_CSS_VALUE_UNDERLINE;
        break;
    }
    }
}

void layout_inline(LayoutContext* lycon, DomNodeBase *elmt, DisplayValue display) {
    log_debug("layout inline %s", elmt->name());
    if (elmt->tag() == LXB_TAG_BR) {
        // allocate a line break view
        View* br_view = alloc_view(lycon, RDT_VIEW_BR, elmt);
        br_view->x = lycon->line.advance_x;  br_view->y = lycon->block.advance_y;
        br_view->width = 0;  br_view->height = lycon->block.line_height;
        lycon->prev_view = br_view;
        line_break(lycon);
        return;
    }

    // save parent context
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // unresolved yet
    PropValue pa_line_align = lycon->line.vertical_align;
    lycon->elmt = elmt;

    ViewSpan* span = (ViewSpan*)alloc_view(lycon, RDT_VIEW_INLINE, elmt);
    span->x = lycon->line.advance_x;  span->y = lycon->block.advance_y;
    span->width = 0;  span->height = 0;
    span->display = display;
    // resolve element default styles
    resolve_inline_default(lycon, span);
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
    DomNodeBase *child = elmt->first_child;
    if (child) {
        lycon->parent = (ViewGroup*)span;  lycon->prev_view = NULL;
        log_debug("layout inline children: advance_y %f, line_height %f", lycon->block.advance_y, lycon->block.line_height);
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        } while (child);
        lycon->parent = span->parent;
    }

    compute_span_bounding_box(span);
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    lycon->prev_view = (View*)span;
    log_debug("inline span view: %d, child %p, x:%d, y:%d, wd:%d, hg:%d", span->type, span->child, span->x, span->y, span->width, span->height);
}
