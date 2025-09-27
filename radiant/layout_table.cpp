#include "layout_table.hpp"
#include "layout.hpp"
#include "../lib/log.h"

// Helpers to iterate DOM children safely
static inline DomNode* first_element_child(DomNode* n) {
    DomNode* c = n ? n->first_child() : nullptr;
    while (c && !c->is_element() && c->next_sibling()) c = c->next_sibling();
    return (c && c->is_element()) ? c : nullptr;
}
static inline DomNode* next_element_sibling(DomNode* n) {
    DomNode* c = n ? n->next_sibling() : nullptr;
    while (c && !c->is_element() && c->next_sibling()) c = c->next_sibling();
    return (c && c->is_element()) ? c : nullptr;
}

// Build a ViewTable subtree from DOM (Phase 1)
// - Creates ViewTable under current lycon->parent
// - Creates row groups (thead/tbody/tfoot), rows, and cells
// - For cells, lays out inner content using existing flow
ViewTable* build_table_tree(LayoutContext* lycon, DomNode* elmt) {
    if (!elmt || !elmt->is_element()) return nullptr;

    // Save context
    ViewGroup* saved_parent = lycon->parent;
    View* saved_prev = lycon->prev_view;
    DomNode* saved_elmt = lycon->elmt;

    // Create the table root view
    lycon->elmt = elmt;
    ViewTable* table = (ViewTable*)alloc_view(lycon, RDT_VIEW_TABLE, elmt);
    if (!table) { lycon->parent = saved_parent; lycon->prev_view = saved_prev; lycon->elmt = saved_elmt; return nullptr; }
    // Resolve table styles onto the view
    dom_node_resolve_style(elmt, lycon);

    // Enter table as parent
    lycon->parent = (ViewGroup*)table;
    lycon->prev_view = nullptr;

    // Iterate table children
    for (DomNode* child = first_element_child(elmt); child; child = next_element_sibling(child)) {
        uintptr_t tag = child->tag();
        if (tag == LXB_TAG_CAPTION) {
            // Treat caption as a normal block for Phase 1; layout its children normally
            ViewBlock* caption = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, child);
            dom_node_resolve_style(child, lycon);
            // Descend into caption for content
            ViewGroup* cap_saved_parent = lycon->parent;
            View* cap_saved_prev = lycon->prev_view;
            lycon->parent = (ViewGroup*)caption; lycon->prev_view = nullptr; lycon->elmt = child;
            for (DomNode* gc = child->first_child(); gc; gc = gc->next_sibling()) {
                layout_flow_node(lycon, gc);
            }
            lycon->parent = cap_saved_parent; lycon->prev_view = (View*)caption; lycon->elmt = elmt;
            continue;
        }

        // Row group level (thead/tbody/tfoot)
        if (tag == LXB_TAG_THEAD || tag == LXB_TAG_TBODY || tag == LXB_TAG_TFOOT) {
            ViewTableRowGroup* group = (ViewTableRowGroup*)alloc_view(lycon, RDT_VIEW_TABLE_ROW_GROUP, child);
            dom_node_resolve_style(child, lycon);
            // Descend into group to collect rows
            ViewGroup* grp_saved_parent = lycon->parent;
            View* grp_saved_prev = lycon->prev_view;
            lycon->parent = (ViewGroup*)group; lycon->prev_view = nullptr; lycon->elmt = child;

            for (DomNode* rowNode = first_element_child(child); rowNode; rowNode = next_element_sibling(rowNode)) {
                if (rowNode->tag() != LXB_TAG_TR) continue;
                ViewTableRow* row = (ViewTableRow*)alloc_view(lycon, RDT_VIEW_TABLE_ROW, rowNode);
                dom_node_resolve_style(rowNode, lycon);

                // Descend into row to collect cells
                ViewGroup* row_saved_parent = lycon->parent; View* row_saved_prev = lycon->prev_view; lycon->parent = (ViewGroup*)row; lycon->prev_view = nullptr; lycon->elmt = rowNode;
                for (DomNode* cellNode = first_element_child(rowNode); cellNode; cellNode = next_element_sibling(cellNode)) {
                    uintptr_t ctag = cellNode->tag();
                    if (ctag != LXB_TAG_TD && ctag != LXB_TAG_TH) continue;
                    ViewTableCell* cell = (ViewTableCell*)alloc_view(lycon, RDT_VIEW_TABLE_CELL, cellNode);
                    dom_node_resolve_style(cellNode, lycon);
                    // Default separate border for Phase 1 if none specified by CSS
                    if (!cell->bound) {
                        cell->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                    }
                    if (!cell->bound->border) {
                        cell->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
                        int px = max(2, (int)lycon->ui_context->pixel_ratio * 2); // thicker for visibility
                        cell->bound->border->width.top = cell->bound->border->width.right =
                            cell->bound->border->width.bottom = cell->bound->border->width.left = px;
                        // darker gray borders for visibility - set channels explicitly (opaque)
                        Color c; c.r = 102; c.g = 102; c.b = 102; c.a = 255;
                        cell->bound->border->top_color = c;
                        cell->bound->border->right_color = c;
                        cell->bound->border->bottom_color = c;
                        cell->bound->border->left_color = c;
                        // Subtle debug background to visualize cells in Phase 1
                        if (!cell->bound->background) { 
                            cell->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp)); 
                            cell->bound->background->color.r = 255; 
                            cell->bound->background->color.g = 250; 
                            cell->bound->background->color.b = 230; 
                            cell->bound->background->color.a = 255; 
                        }
                    }
                    // Layout cell contents using existing flow
                    ViewGroup* cell_saved_parent = lycon->parent; View* cell_saved_prev = lycon->prev_view; lycon->parent = (ViewGroup*)cell; lycon->prev_view = nullptr; lycon->elmt = cellNode;
                    for (DomNode* cc = cellNode->first_child(); cc; cc = cc->next_sibling()) {
                        layout_flow_node(lycon, cc);
                    }
                    lycon->parent = cell_saved_parent; lycon->prev_view = (View*)cell; lycon->elmt = rowNode;
                }
                lycon->parent = row_saved_parent; lycon->prev_view = (View*)row; lycon->elmt = child;
            }
            lycon->parent = grp_saved_parent; lycon->prev_view = (View*)group; lycon->elmt = elmt;
            continue;
        }

        // Allow TR directly under TABLE as a convenience (Phase 1)
        if (tag == LXB_TAG_TR) {
            ViewTableRow* row = (ViewTableRow*)alloc_view(lycon, RDT_VIEW_TABLE_ROW, child);
            dom_node_resolve_style(child, lycon);
            ViewGroup* row_saved_parent = lycon->parent; View* row_saved_prev = lycon->prev_view; lycon->parent = (ViewGroup*)row; lycon->prev_view = nullptr; lycon->elmt = child;
            for (DomNode* cellNode = first_element_child(child); cellNode; cellNode = next_element_sibling(cellNode)) {
                uintptr_t ctag = cellNode->tag();
                if (ctag != LXB_TAG_TD && ctag != LXB_TAG_TH) continue;
                ViewTableCell* cell = (ViewTableCell*)alloc_view(lycon, RDT_VIEW_TABLE_CELL, cellNode);
                dom_node_resolve_style(cellNode, lycon);
                if (!cell->bound) {
                    cell->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                }
                if (!cell->bound->border) {
                    cell->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
                    int px = max(2, (int)lycon->ui_context->pixel_ratio * 2); // thicker for visibility
                    cell->bound->border->width.top = cell->bound->border->width.right =
                        cell->bound->border->width.bottom = cell->bound->border->width.left = px;
                    Color c; c.c = 0xFF666666;
                    cell->bound->border->top_color = c;
                    cell->bound->border->right_color = c;
                    cell->bound->border->bottom_color = c;
                    cell->bound->border->left_color = c;
                    // Optional debug background
                    // if (!cell->bound->background) { cell->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp)); cell->bound->background->color.c = 0xFFFFF2CC; }
                }
                ViewGroup* cell_saved_parent = lycon->parent; View* cell_saved_prev = lycon->prev_view; lycon->parent = (ViewGroup*)cell; lycon->prev_view = nullptr; lycon->elmt = cellNode;
                for (DomNode* cc = cellNode->first_child(); cc; cc = cc->next_sibling()) {
                    layout_flow_node(lycon, cc);
                }
                lycon->parent = cell_saved_parent; lycon->prev_view = (View*)cell; lycon->elmt = child;
            }
            lycon->parent = row_saved_parent; lycon->prev_view = (View*)row; lycon->elmt = elmt;
            continue;
        }

        // Other children (e.g., colgroup/col) can be skipped or handled later phases
    }

    // Restore context and return
    lycon->parent = saved_parent;
    lycon->prev_view = (View*)table; // table is now the last created view under saved_parent
    lycon->elmt = saved_elmt;
    return table;
}

// Perform auto table layout. Phase 1: stub.
// Measure maximum content width of a view subtree (best-effort)
static int measure_view_max_width(View* v) {
    if (!v) return 0;
    int w = 0;
    switch (v->type) {
        case RDT_VIEW_BLOCK:
        case RDT_VIEW_INLINE_BLOCK:
        case RDT_VIEW_LIST_ITEM:
        case RDT_VIEW_TABLE:
        case RDT_VIEW_TABLE_ROW_GROUP:
        case RDT_VIEW_TABLE_ROW:
        case RDT_VIEW_TABLE_CELL:
            w = ((ViewBlock*)v)->width;
            break;
        case RDT_VIEW_INLINE:
            // Inline width is not directly tracked here; use children below
            w = 0;
            break;
        case RDT_VIEW_TEXT:
            w = ((ViewText*)v)->width;
            break;
        default:
            break;
    }
    // Check children for larger width
    if (v->type != RDT_VIEW_TEXT) {
        View* ch = ((ViewGroup*)v)->child;
        if (ch) {
            for (View* c = ch; c; c = c->next) {
                int cw = measure_view_max_width(c);
                if (cw > w) w = cw;
            }
        }
    }
    return w;
}

static int measure_cell_content_width(ViewBlock* cell) {
    if (!cell) return 0;
    int maxw = 0;
    for (View* c = ((ViewGroup*)cell)->child; c; c = c->next) {
        int cw = measure_view_max_width(c);
        if (cw > maxw) maxw = cw;
    }
    return maxw;
}

void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    if (!table) return;
    // Determine available width from current block context
    int avail_width = lycon->block.width > 0 ? lycon->block.width : 0;
    if (avail_width <= 0) avail_width = 800; // fallback default

    // 1) Discover max column count from first row encountered
    int columns = 0;
    for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
        if (child->type == RDT_VIEW_TABLE_ROW_GROUP || child->type == RDT_VIEW_TABLE_ROW) {
            ViewBlock* first_row = child;
            if (child->type == RDT_VIEW_TABLE_ROW_GROUP) first_row = child->first_child;
            if (first_row && first_row->type == RDT_VIEW_TABLE_ROW) {
                int count = 0;
                for (ViewBlock* cell = first_row->first_child; cell; cell = cell->next_sibling) {
                    if (cell->type == RDT_VIEW_TABLE_CELL) count++;
                }
                columns = count;
                break;
            }
        }
    }
    if (columns <= 0) {
        // No rows/cells, set minimal size and return
        table->x = 0; table->y = 0; table->width = 0; table->height = 0;
        table->content_width = 0; table->content_height = 0;
        return;
    }

    // 2) Compute preferred width per column from cell content
    int* col_pref = (int*)calloc(columns, sizeof(int));
    for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
        if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
                if (row->type != RDT_VIEW_TABLE_ROW) continue;
                int col = 0;
                for (ViewBlock* cell = row->first_child; cell && col < columns; cell = cell->next_sibling) {
                    if (cell->type != RDT_VIEW_TABLE_CELL) continue;
                    int cw = measure_cell_content_width(cell);
                    if (cw > col_pref[col]) col_pref[col] = cw;
                    col++;
                }
            }
        } else if (child->type == RDT_VIEW_TABLE_ROW) {
            int col = 0;
            for (ViewBlock* cell = child->first_child; cell && col < columns; cell = cell->next_sibling) {
                if (cell->type != RDT_VIEW_TABLE_CELL) continue;
                int cw = measure_cell_content_width(cell);
                if (cw > col_pref[col]) col_pref[col] = cw;
                col++;
            }
        }
    }

    // Sanity: ensure at least minimal width
    for (int i = 0; i < columns; i++) if (col_pref[i] <= 0) col_pref[i] = 10;

    long long sum_pref = 0; for (int i = 0; i < columns; i++) sum_pref += col_pref[i];
    int* col_widths = (int*)calloc(columns, sizeof(int));
    if (sum_pref <= avail_width && sum_pref > 0) {
        // Fit content without scaling; distribute remaining space evenly
        int remaining = avail_width - (int)sum_pref;
        int even = (columns > 0) ? remaining / columns : 0;
        for (int i = 0; i < columns; i++) col_widths[i] = col_pref[i] + even;
        // Adjust any rounding remainder to the first column
        int used = 0; for (int i=0;i<columns;i++) used += col_widths[i];
        if (used < avail_width && columns > 0) col_widths[0] += (avail_width - used);
    } else if (sum_pref > 0) {
        // Scale down proportionally
        for (int i = 0; i < columns; i++) {
            col_widths[i] = (int)((double)col_pref[i] * (double)avail_width / (double)sum_pref);
            if (col_widths[i] < 10) col_widths[i] = 10;
        }
        int used = 0; for (int i=0;i<columns;i++) used += col_widths[i];
        if (used != avail_width && columns > 0) col_widths[0] += (avail_width - used);
    } else {
        // Fallback equal widths
        int eq = columns > 0 ? (avail_width / columns) : avail_width;
        for (int i = 0; i < columns; i++) col_widths[i] = eq;
    }

    int col_width = avail_width / columns; // legacy single value for row computations below (will be replaced)
    int current_y = 0;
    int table_content_width = 0; for (int i=0;i<columns;i++) table_content_width += col_widths[i];
    int table_content_height = 0;

    // 2) Layout rows and cells using equal-width columns, compute row heights
    auto layout_row = [&](ViewBlock* row){
        int x = 0;
        int row_height = 0;
        int col = 0;
        for (ViewBlock* cell = row->first_child; cell && col < columns; cell = cell->next_sibling) {
            if (cell->type != RDT_VIEW_TABLE_CELL) continue;
            int cw = col_widths[col];
            cell->x = x; cell->y = current_y; cell->width = cw;
            // Approximate cell height from children if available
            int cell_h = 0;
            for (View* cc = ((ViewGroup*)cell)->child; cc; cc = cc->next) {
                if (cc->type == RDT_VIEW_BLOCK || cc->type == RDT_VIEW_INLINE_BLOCK || cc->type == RDT_VIEW_LIST_ITEM) {
                    ViewBlock* cb = (ViewBlock*)cc;
                    if (cb->height > cell_h) cell_h = cb->height;
                } else if (cc->type == RDT_VIEW_TEXT) {
                    ViewText* tx = (ViewText*)cc; if (tx->height > cell_h) cell_h = tx->height;
                }
            }
            if (cell_h <= 0) cell_h = lycon->block.line_height > 0 ? (int)lycon->block.line_height : 16;
            cell->height = cell_h;
            if (cell_h > row_height) row_height = cell_h;

            x += cw; col++;
        }
        // Set final row height and position cells vertically (top align for now)
        row->x = 0; row->y = current_y; row->width = table_content_width; row->height = row_height;
        return row_height;
    };

    for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
        if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
            // Row group wraps multiple rows
            int group_start_y = current_y;
            int group_height = 0;
            for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
                if (row->type != RDT_VIEW_TABLE_ROW) continue;
                int rh = layout_row(row);
                current_y += rh;
                group_height += rh;
            }
            child->x = 0; child->y = group_start_y; child->width = table_content_width; child->height = group_height;
        } else if (child->type == RDT_VIEW_TABLE_ROW) {
            int rh = layout_row(child);
            current_y += rh;
        } else {
            // For caption or other blocks, place above rows if encountered first, otherwise leave as-is
            // If it's a caption, position at current_y and advance
            if (child->node && child->node->tag() == LXB_TAG_CAPTION) {
                child->x = 0; child->y = current_y; child->width = table_content_width;
                if (child->height <= 0) child->height = (int)(lycon->block.line_height > 0 ? lycon->block.line_height : 20);
                current_y += child->height;
            }
        }
    }

    table_content_height = current_y;
    table->x = 0; table->y = 0; table->width = table_content_width; table->height = table_content_height;
    table->content_width = table_content_width; table->content_height = table_content_height;
    free(col_pref);
    free(col_widths);
}

// Phase 1 scaffolding: call stubs and fallback to block layout while algorithm evolves.
void layout_table_box(LayoutContext* lycon, DomNode* elmt, DisplayValue display) {
    log_debug("layout_table_box: entering Phase 1");
    // Attempt to build the table tree
    ViewTable* table = build_table_tree(lycon, elmt);
    if (!table) {
        log_debug("layout_table_box: fallback to layout_block (Phase 1)");
        layout_block(lycon, elmt, (DisplayValue){ .outer = LXB_CSS_VALUE_BLOCK, .inner = LXB_CSS_VALUE_FLOW });
        return;
    }
    // Run sizing when implemented
    table_auto_layout(lycon, table);
}
