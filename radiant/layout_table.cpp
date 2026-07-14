#include "layout.hpp"
#include "view.hpp"  // For FormDefaults (radio/checkbox margin constants)
#include "render.hpp"
#include "../lib/log.h"
#include "../lib/strview.h"
#include "../lib/arraylist.h"
#include "../lib/arraylist.hpp"
// str.h included via view.hpp
#include "../lib/memtrack.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_style_node.hpp"


/*
 * RADIANT TABLE LAYOUT ENGINE
 *
 * A clean, browser-compatible implementation of CSS table layout
 * following the CSS 2.1 specification.
 *
 * Architecture:
 * 1. Structure Parser - builds logical table structure from DOM
 * 2. Layout Engine - calculates column widths and row heights
 * 3. Grid System - handles colspan/rowspan positioning
 * 4. Border Model - manages separate/collapsed border modes
 * 5. CSS Integration - parses and applies table-specific properties
 */

// =============================================================================
// TABLE NAVIGATION HELPERS (CSS 2.1 Section 17.2.1 Anonymous Box Support)
// =============================================================================
// These methods provide unified traversal of table structure regardless of
// whether elements have proper HTML structure or use anonymous box wrappers.

static inline ViewBlock* table_array_view_block(ArrayList* list, int index) {
    View* view = static_cast<View*>(list->data[index]);
    return lam::view_require_block(view);
}

static View* table_next_view_of_type(View* view, int view_type) {
    for (; view; view = static_cast<View*>(view->next_sibling)) {
        if (view->view_type == view_type) return view;
    }
    return nullptr;
}

ViewTableRow* ViewTable::first_row() {
    // Direct children first (handles both normal rows and acts_as_tbody case)
    for (View* child = static_cast<View*>(first_child); child; child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_ROW) {
            return lam::view_require<RDT_VIEW_TABLE_ROW>(child);
        }
        // Look inside row groups
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRow* row = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child)->first_row();
            if (row) return row;
        }
    }
    return nullptr;
}

ViewBlock* ViewTable::first_row_group() {
    // If table acts as tbody, return self; otherwise find first row group child
    if (acts_as_tbody()) return this;

    View* group = table_next_view_of_type(static_cast<View*>(first_child),
                                          RDT_VIEW_TABLE_ROW_GROUP);
    return group ? lam::view_require_block(group) : nullptr;
}

ViewTableRow* ViewTable::next_row(ViewTableRow* current) {
    if (!current) return nullptr;

    // Try next sibling first
    for (View* sibling = static_cast<View*>(current->next_sibling); sibling; sibling = static_cast<View*>(sibling->next_sibling)) {
        if (sibling->view_type == RDT_VIEW_TABLE_ROW) return lam::view_require<RDT_VIEW_TABLE_ROW>(sibling);
    }

    // If in row group, try next row group
    ViewBlock* parent = lam::view_as_block(static_cast<View*>(current->parent));
    if (parent && parent->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
        for (View* next = static_cast<View*>(parent->next_sibling); next; next = static_cast<View*>(next->next_sibling)) {
            if (next->view_type == RDT_VIEW_TABLE_ROW) return lam::view_require<RDT_VIEW_TABLE_ROW>(next);
            if (next->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                ViewTableRow* row = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(next)->first_row();
                if (row) return row;
            }
        }
    }
    return nullptr;
}

// Get section type from tag/display for visual ordering (CSS 2.1 Section 17.2)
TableSectionType ViewTableRowGroup::get_section_type() const {
    // Check HTML tag first
    uintptr_t tag = tag_id;
    if (tag == HTM_TAG_THEAD) return TABLE_SECTION_THEAD;
    if (tag == HTM_TAG_TFOOT) return TABLE_SECTION_TFOOT;
    if (tag == HTM_TAG_TBODY) return TABLE_SECTION_TBODY;

    // For CSS table elements (div with display: table-footer-group), resolve display
    // Note: The element's display field may not be set, so we resolve it fresh
    DisplayValue resolved = resolve_display_value((void*)this);

    if (resolved.inner == CSS_VALUE_TABLE_HEADER_GROUP) {
        return TABLE_SECTION_THEAD;
    }
    if (resolved.inner == CSS_VALUE_TABLE_FOOTER_GROUP) {
        return TABLE_SECTION_TFOOT;
    }

    // Default to TBODY for table-row-group and anonymous groups
    return TABLE_SECTION_TBODY;
}

ViewTableRow* ViewTableRowGroup::first_row() {
    View* row = table_next_view_of_type(static_cast<View*>(first_child),
                                        RDT_VIEW_TABLE_ROW);
    return row ? lam::view_require<RDT_VIEW_TABLE_ROW>(row) : nullptr;
}

ViewTableRow* ViewTableRowGroup::next_row(ViewTableRow* current) {
    if (!current) return nullptr;

    View* row = table_next_view_of_type(static_cast<View*>(current->next_sibling),
                                        RDT_VIEW_TABLE_ROW);
    return row ? lam::view_require<RDT_VIEW_TABLE_ROW>(row) : nullptr;
}

ViewTableCell* ViewTableRow::first_cell() {
    View* cell = table_next_view_of_type(static_cast<View*>(first_child),
                                         RDT_VIEW_TABLE_CELL);
    return cell ? lam::view_require<RDT_VIEW_TABLE_CELL>(cell) : nullptr;
}

ViewTableCell* ViewTableRow::next_cell(ViewTableCell* current) {
    if (!current) return nullptr;

    View* cell = table_next_view_of_type(static_cast<View*>(current->next_sibling),
                                         RDT_VIEW_TABLE_CELL);
    return cell ? lam::view_require<RDT_VIEW_TABLE_CELL>(cell) : nullptr;
}

template <typename Fn>
static void for_each_table_row_cell(ViewTableRow* row, Fn fn) {
    if (!row) return;
    for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
        fn(cell);
    }
}

static float table_row_collapsed_vertical_border_contribution(ViewTableRow* row,
                                                              float* max_top_border,
                                                              float* max_bottom_border) {
    if (max_top_border) *max_top_border = 0.0f;
    if (max_bottom_border) *max_bottom_border = 0.0f;
    for_each_table_row_cell(row, [&](ViewTableCell* cell) {
        if (cell->td->top_resolved && max_top_border &&
            cell->td->top_resolved->width > *max_top_border) {
            *max_top_border = cell->td->top_resolved->width;
        }
        if (cell->td->bottom_resolved && max_bottom_border &&
            cell->td->bottom_resolved->width > *max_bottom_border) {
            *max_bottom_border = cell->td->bottom_resolved->width;
        }
    });
    float top = max_top_border ? *max_top_border : 0.0f;
    float bottom = max_bottom_border ? *max_bottom_border : 0.0f;
    return top / 2.0f + bottom / 2.0f;
}

template <typename Fn>
static void for_each_table_row(ViewTable* table, Fn fn) {
    if (!table) return;
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        fn(row);
    }
}

template <typename Fn>
static void for_each_table_row_in_group(ViewTableRowGroup* group, Fn fn) {
    if (!group) return;
    for (ViewTableRow* row = group->first_row(); row; row = group->next_row(row)) {
        fn(row, static_cast<ViewBlock*>(row));
    }
}

static int table_row_group_row_count(ViewTableRowGroup* group) {
    int count = 0;
    for_each_table_row_in_group(group, [&](ViewTableRow* row, ViewBlock* row_block) {
        (void)row;
        (void)row_block;
        count++;
    });
    return count;
}

template <typename Predicate>
static ViewTableCell* find_table_cell(ViewTable* table, Predicate predicate) {
    if (!table) return nullptr;
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
            if (predicate(row, cell)) return cell;
        }
    }
    return nullptr;
}

template <typename Fn>
static void for_each_table_colgroup_column(ViewElement* colgroup, Fn fn) {
    if (!colgroup) return;
    for (View* col_view = static_cast<View*>(colgroup->first_child); col_view;
         col_view = static_cast<View*>(col_view->next_sibling)) {
        ViewElement* col = lam::view_as_element(col_view);
        if (col && col->view_type == RDT_VIEW_TABLE_COLUMN) {
            fn(col);
        }
    }
}

template <typename Fn>
static void for_each_table_column_source(ViewTable* table, Fn fn) {
    if (!table) return;
    for (View* child_view = static_cast<View*>(table->first_child); child_view;
         child_view = static_cast<View*>(child_view->next_sibling)) {
        ViewElement* child = lam::view_as_element(child_view);
        if (!child) continue;
        if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP ||
            child->view_type == RDT_VIEW_TABLE_COLUMN) {
            fn(child);
        }
    }
}

ViewBlock* ViewTableRow::parent_row_group() {
    ViewBlock* parent = lam::view_as_block(static_cast<View*>(this->parent));
    if (parent && (parent->view_type == RDT_VIEW_TABLE_ROW_GROUP || parent->view_type == RDT_VIEW_TABLE)) {
        return parent;
    }
    return nullptr;
}

// Get first cell when table acts as its own row (cells are direct children)
ViewTableCell* ViewTable::first_direct_cell() {
    if (!acts_as_row()) return nullptr;

    View* cell = table_next_view_of_type(static_cast<View*>(first_child),
                                         RDT_VIEW_TABLE_CELL);
    return cell ? lam::view_require<RDT_VIEW_TABLE_CELL>(cell) : nullptr;
}

// Get next cell when table acts as its own row
ViewTableCell* ViewTable::next_direct_cell(ViewTableCell* current) {
    if (!current || !acts_as_row()) return nullptr;

    View* cell = table_next_view_of_type(static_cast<View*>(current->next_sibling),
                                         RDT_VIEW_TABLE_CELL);
    return cell ? lam::view_require<RDT_VIEW_TABLE_CELL>(cell) : nullptr;
}

// =============================================================================
// CELL HELPER FUNCTIONS
// =============================================================================
// Common operations for table cell layout to reduce code duplication.

// Get parent table from a cell, traversing up through row and row group
static ViewTable* get_parent_table(ViewTableCell* cell) {
    if (!cell) return nullptr;

    // Cell -> Row -> RowGroup/Table -> Table
    DomNode* parent = cell->parent;
    while (parent) {
        if (parent->view_type == RDT_VIEW_TABLE) {
            return lam::view_require<RDT_VIEW_TABLE>(static_cast<View*>(parent));
        }
        parent = parent->parent;
    }
    return nullptr;
}

// Forward declaration for layout_table_cell_content (defined later in the file)
static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell, ViewBlock* table = nullptr);

static void table_apply_positioned_layout(LayoutContext* lycon, ViewBlock* block) {
    if (!block || !block->position) return;
    if (block->position->position == CSS_VALUE_RELATIVE) {
        layout_relative_positioned(lycon, block);
    } else if (block->position->position == CSS_VALUE_STICKY) {
        layout_sticky_positioned(lycon, block);
    }
}

static void table_apply_positioned_row(LayoutContext* lycon, ViewTableRow* row) {
    if (!row) return;
    table_apply_positioned_layout(lycon, static_cast<ViewBlock*>(row));
    for_each_table_row_cell(row, [&](ViewTableCell* tcell) {
        table_apply_positioned_layout(lycon, lam::view_require_block(tcell));
    });
}

static float resolve_table_relative_width(LayoutContext* lycon, const CssValue* value, float table_content_width) {
    if (!value) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return table_content_width > 0.0f ?
            (float)(value->data.percentage.value / 100.0) * table_content_width : 0.0f;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        return resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value);
    }
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        BlockContext percentage_base = {};
        percentage_base.content_width = table_content_width;
        BlockContext* saved_parent = lycon->block.parent;
        lycon->block.parent = &percentage_base;
        float resolved = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value);
        lycon->block.parent = saved_parent;
        return isnan(resolved) ? 0.0f : resolved;
    }
    return 0.0f;
}

static bool table_width_value_is_relative(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) return true;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            if (table_width_value_is_relative(value->data.list.values[i])) return true;
        }
    } else if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function) {
        CssFunction* func = value->data.function;
        for (int i = 0; i < func->arg_count; i++) {
            if (table_width_value_is_relative(func->args[i])) return true;
        }
    } else if (value->type == CSS_VALUE_TYPE_CALC) {
        return true;
    }
    return false;
}

static bool table_width_value_has_nonzero_length_term(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        return fabs(value->data.length.value) > 0.0001;
    }
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            if (table_width_value_has_nonzero_length_term(value->data.list.values[i])) return true;
        }
    } else if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function) {
        CssFunction* func = value->data.function;
        for (int i = 0; i < func->arg_count; i++) {
            if (table_width_value_has_nonzero_length_term(func->args[i])) return true;
        }
    }
    return false;
}

static bool table_direct_float_overlaps_y(ViewBlock* floating, ViewTable* table, float y) {
    if (!floating || !table) return false;
    float rel_y = floating->parent == table ? 0.0f : floating->y - table->y;
    float margin_bottom = floating->bound ? floating->bound->margin.bottom : 0.0f;
    return y >= rel_y && y < rel_y + floating->height + margin_bottom;
}

template <typename Fn>
static void for_each_table_direct_float(ViewTable* table, Fn fn) {
    if (!table) return;
    for (View* child = table->first_child; child; child = child->next_sibling) {
        if (!child->is_block()) continue;
        ViewBlock* floating = lam::view_require_block(child);
        if (layout_position_is_floated(floating->position)) fn(floating);
    }
}

static float table_direct_left_float_intrusion(ViewTable* table, float y, float table_width) {
    float intrusion = 0.0f;
    for_each_table_direct_float(table, [&](ViewBlock* floating) {
        if (floating->position->float_prop != CSS_VALUE_LEFT) return;
        if (!table_direct_float_overlaps_y(floating, table, y)) return;

        float rel_x = floating->x - table->x;
        float margin_right = floating->bound ? floating->bound->margin.right : 0.0f;
        float right = rel_x + floating->width + margin_right;
        if (table_width > 0.0f && right > table_width) right = table_width;
        if (right > intrusion) intrusion = right;
    });
    return intrusion;
}

static float table_direct_right_float_intrusion(ViewTable* table, float y, float table_width) {
    float intrusion = 0.0f;
    for_each_table_direct_float(table, [&](ViewBlock* floating) {
        if (floating->position->float_prop != CSS_VALUE_RIGHT) return;
        if (!table_direct_float_overlaps_y(floating, table, y)) return;

        float rel_x = floating->x - table->x;
        if (rel_x < 0.0f) rel_x = 0.0f;
        float left_edge = rel_x;
        if (left_edge > table_width) left_edge = table_width;
        float candidate = table_width - left_edge;
        if (candidate > intrusion) intrusion = candidate;
    });
    return intrusion;
}

static float table_direct_float_next_clear_y(ViewTable* table, float y) {
    float next_y = y;
    for_each_table_direct_float(table, [&](ViewBlock* floating) {
        if (!table_direct_float_overlaps_y(floating, table, y)) return;

        float margin_bottom = floating->bound ? floating->bound->margin.bottom : 0.0f;
        float candidate = floating->height + margin_bottom;
        if (candidate > next_y) next_y = candidate;
    });
    return next_y;
}

static bool table_has_direct_float(ViewTable* table) {
    bool found = false;
    for_each_table_direct_float(table, [&](ViewBlock* floating) {
        (void)floating;
        found = true;
    });
    return found;
}

static float table_clear_direct_float_intrusion(ViewTable* table, float y,
                                                float content_width,
                                                bool has_direct_float) {
    if (!has_direct_float || table->width <= 0.0f || content_width <= 0.0f) return y;
    for (int guard = 0; guard < 32; guard++) {
        float left_intrusion = table_direct_left_float_intrusion(table, y, table->width);
        float right_intrusion = table_direct_right_float_intrusion(table, y, table->width);
        if (left_intrusion + content_width + right_intrusion <= table->width + 0.01f) {
            break;
        }
        float next_y = table_direct_float_next_clear_y(table, y);
        if (next_y <= y + 0.01f) break;
        y = next_y;
    }
    return y;
}

static bool table_cell_calc_width_is_indefinite_constraint(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function) return false;
    CssFunction* func = value->data.function;
    if (!func->name || strcmp(func->name, "calc") != 0) return false;

    // CSS table auto layout treats percentages as column constraints. A calc()
    // that mixes that circular percentage basis with a non-zero length term has
    // no definite preferred column width during intrinsic column measurement.
    return table_width_value_is_relative(value) &&
        table_width_value_has_nonzero_length_term(value);
}

static float get_cell_css_width_percent(ViewTableCell* tcell) {
    if (!tcell || tcell->node_type != DOM_NODE_ELEMENT) return 0.0f;

    DomElement* dom_elem = tcell->as_element();
    if (!dom_elem) return 0.0f;

    CssDeclaration* width_decl = dom_elem->specified_style
        ? style_tree_get_declaration(dom_elem->specified_style, CSS_PROPERTY_WIDTH)
        : nullptr;

    if (width_decl && width_decl->value &&
        width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        float percent = (float)width_decl->value->data.percentage.value;
        return percent > 0.0f ? percent : 0.0f;
    }

    if (tcell->blk && !isnan(tcell->blk->given_width_percent)) {
        return tcell->blk->given_width_percent > 0.0f ?
            tcell->blk->given_width_percent : 0.0f;
    }

    return 0.0f;
}

// Get CSS width from a cell element, handling percentage and length values
// Returns 0 if no explicit width is set
// border_collapse: if true, don't add cell border to width (CSS 2.1 border-collapse model)
static float get_cell_css_width(LayoutContext* lycon, ViewTableCell* tcell, float table_content_width, bool border_collapse = false, bool* is_table_relative = nullptr) {
    if (tcell->node_type != DOM_NODE_ELEMENT) return 0.0f;
    if (is_table_relative) *is_table_relative = false;

    DomElement* dom_elem = tcell->as_element();
    if (!dom_elem) return 0.0f;

    CssDeclaration* width_decl = dom_elem->specified_style
        ? style_tree_get_declaration(dom_elem->specified_style, CSS_PROPERTY_WIDTH)
        : nullptr;

    float cell_width = 0.0f;
    float css_content_width = 0.0f;
    bool html_width_hint = false;

    if (width_decl && width_decl->value &&
        table_cell_calc_width_is_indefinite_constraint(width_decl->value)) {
        cell_width = 0.0f;
        if (is_table_relative) *is_table_relative = true;
    } else if (width_decl && width_decl->value &&
        (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE ||
         width_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
         width_decl->value->type == CSS_VALUE_TYPE_FUNCTION)) {
        css_content_width = resolve_table_relative_width(lycon, width_decl->value, table_content_width);
        cell_width = css_content_width;
        if (is_table_relative) *is_table_relative = table_width_value_is_relative(width_decl->value);
    } else if (width_decl && width_decl->value &&
               width_decl->value->type == CSS_VALUE_TYPE_CALC) {
        cell_width = 0.0f;
        if (is_table_relative) *is_table_relative = true;
    } else if (tcell->blk && !isnan(tcell->blk->given_width_percent) &&
               table_content_width > 0.0f) {
        css_content_width = table_content_width * tcell->blk->given_width_percent / 100.0f;
        cell_width = css_content_width;
        if (is_table_relative) *is_table_relative = true;
        html_width_hint = true;
        log_debug("%s table cell width hint: %.1f%% of %.1fpx = %.1fpx",
                  tcell->source_loc(), tcell->blk->given_width_percent,
                  table_content_width, cell_width);
    } else if (tcell->blk && tcell->blk->given_width >= 0.0f) {
        css_content_width = tcell->blk->given_width;
        cell_width = css_content_width;
        html_width_hint = true;
        log_debug("%s table cell width hint: %.1fpx", tcell->source_loc(), cell_width);
    }

    if (cell_width <= 0) return 0.0f;

    // Check box-sizing model
    bool is_border_box = html_width_hint ||
        layout_uses_border_box(tcell);

    if (is_border_box) {
        // CSS width already includes padding and border — cell_width is the border-box width
        // No need to add anything
    } else {
        // Add padding (CSS width is content-box by default)
        if (tcell->bound && tcell->bound->padding.left >= 0 && tcell->bound->padding.right >= 0) {
            cell_width += layout_box_metrics(tcell).padding_h;
        }

        // CSS 2.1 §17.6.2: In border-collapse mode, cell borders don't contribute to column widths.
        // The column widths are content+padding only. Half-borders are added at positioning stage.
        if (!border_collapse && tcell->bound && tcell->bound->border) {
            float border_left = (tcell->bound->border->left_style != CSS_VALUE_NONE)
                ? tcell->bound->border->width.left : 0.0f;
            float border_right = (tcell->bound->border->right_style != CSS_VALUE_NONE)
                ? tcell->bound->border->width.right : 0.0f;
            cell_width += border_left + border_right;
        }
    }

    // CSS 2.1: Apply min-width/max-width constraints to cell border-box width
    cell_width = layout_clamp_min_max_width(tcell, cell_width);

    return cell_width;
}

// Get explicit CSS height from a cell or block element
// Returns 0 if no explicit height is set
static float get_explicit_css_height(LayoutContext* lycon, ViewBlock* element) {
    if (element->node_type != DOM_NODE_ELEMENT) return 0.0f;

    DomElement* dom_elem = element->as_element();
    if (!dom_elem) return 0.0f;

    // First try CSS specified_style
    if (dom_elem->specified_style) {
        CssDeclaration* height_decl = style_tree_get_declaration(
            dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
        if (height_decl && height_decl->value) {
            float resolved = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
            if (resolved > 0) {
                return resolved;
            }
        }
    }

    // Fallback to blk->given_height (from HTML height attribute or resolved styles)
    if (element->blk && element->blk->given_height > 0) {
        return element->blk->given_height;
    }

    return 0.0f;
}

// Check if a table cell is empty (has no content)
// CSS 2.1 Section 17.6.1: A cell is empty if it contains no in-flow content
// (text nodes with only whitespace are considered empty when whitespace collapses,
// but if white-space preserves whitespace (pre, pre-wrap, etc.), the cell is NOT empty)
static bool is_cell_empty(ViewTableCell* cell) {
    DomNode* child = lam::dom_require<DOM_NODE_ELEMENT>(cell)->first_child;

    // Check if whitespace is preserved for this cell (CSS 2.1 §17.6.1.1)
    bool ws_preserved = false;
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(cell);
    if (elem->blk && elem->blk->white_space != 0) {
        CssEnum ws = elem->blk->white_space;
        if (ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP ||
            ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES) {
            ws_preserved = true;
        }
    }

    while (child) {
        if (child->is_element()) {
            // Element child = has content (not empty)
            return false;
        }
        if (child->is_text()) {
            // Quick Win #2: Check for Unicode whitespace, not just ASCII
            // Unicode whitespace categories: Zs (space separator), Zl (line separator), Zp (paragraph separator)
            // Common whitespace: space (U+0020), tab (U+0009), LF (U+000A), CR (U+000D), NBSP (U+00A0),
            //                    em space (U+2003), thin space (U+2009), zero-width space (U+200B), etc.
            const char* text = lam::dom_require<DOM_NODE_TEXT>(child)->text;
            if (text) {
                // If white-space preserves whitespace, any text content = not empty
                if (ws_preserved && strlen(text) > 0) {
                    return false;
                }
                const unsigned char* p = (const unsigned char*)text;
                const unsigned char* p_end = p + strlen(text);
                while (p < p_end) {
                    uint32_t codepoint;
                    int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
                    if (bytes <= 0) break;  // Invalid UTF-8

                    // Check for Unicode whitespace
                    // Basic ASCII whitespace: space, tab, LF, VT, FF, CR
                    bool is_ws = (codepoint == 0x0020 || codepoint == 0x0009 || codepoint == 0x000A ||
                                  codepoint == 0x000B || codepoint == 0x000C || codepoint == 0x000D);

                    // Unicode whitespace characters (collapsible only)
                    // CSS 2.1 §17.6.1.1: A cell is "empty" if it has no line boxes.
                    // Non-breaking spaces (U+00A0, U+202F) create non-collapsible inline
                    // content that generates a line box, so they are NOT whitespace here.
                    if (!is_ws) {
                        // U+1680: Ogham space mark
                        // U+2000-U+200A: En quad, Em quad, En space, Em space, Three-per-em space,
                        //                 Four-per-em space, Six-per-em space, Figure space,
                        //                 Punctuation space, Thin space, Hair space
                        // U+205F: Medium mathematical space
                        // U+3000: Ideographic space
                        // NOTE: U+00A0 (NBSP) and U+202F (Narrow NBSP) are excluded because they
                        // are non-collapsible per CSS and generate visible inline content.
                        is_ws = (codepoint == 0x1680 ||
                                 (codepoint >= 0x2000 && codepoint <= 0x200A) ||
                                 codepoint == 0x205F || codepoint == 0x3000);
                    }

                    if (!is_ws) {
                        // Non-whitespace content found
                        return false;
                    }

                    p += bytes;
                }
            }
        }
        child = child->next_sibling;
    }
    return true;  // No visible content found
}

// Check if a table row or row group has visibility: collapse
// CSS 2.1 Section 17.5.5: Rows with visibility: collapse are removed from layout
// but still contribute to column width calculations
static bool is_visibility_collapse(ViewBlock* element) {
    if (!element) return false;

    // Check the InlineProp for visibility
    DomElement* dom_elem = element->as_element();
    if (dom_elem && dom_elem->in_line) {
        return dom_elem->in_line->visibility == VIS_COLLAPSE;
    }
    return false;
}

// CSS 2.1 §10.6.3: Find the maximum bottom extent of any float descendants
// relative to a given ancestor. Table cells are BFCs, so their content height
// must include floats for vertical-align calculations.
static float find_descendant_float_max_y(ViewElement* parent, float y_offset) {
    float max_y = 0;
    for (View* child = parent->first_child; child; child = child->next_sibling) {
        if (!child->view_type) continue;
        if (child->view_type == RDT_VIEW_BLOCK ||
            child->view_type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* block = lam::view_require_block(child);
            float abs_y = y_offset + child->y;
            // check if this child is a float
            if (layout_position_is_floated(block->position)) {
                float bottom = abs_y + child->height;
                if (bottom > max_y) max_y = bottom;
            }
            // recurse into block children to find nested floats
            if (block->is_element()) {
                float nested = find_descendant_float_max_y(lam::view_require_element(block), abs_y);
                if (nested > max_y) max_y = nested;
            }
        }
    }
    return max_y;
}

static bool table_cell_vertical_align_skips_child(View* child);

template <typename Fn>
static void for_each_table_cell_vertical_align_child(ViewElement* cell, Fn fn) {
    if (!cell) return;
    for (View* child = cell->first_child; child; child = child->next_sibling) {
        if (!child->view_type) continue;
        if (table_cell_vertical_align_skips_child(child)) continue;
        fn(child);
    }
}

static bool table_inline_span_has_inline_axis_decoration(ViewSpan* span) {
    if (!span || !span->bound) return false;
    if (span->bound->margin.left != 0.0f || span->bound->margin.right != 0.0f ||
        span->bound->padding.left != 0.0f || span->bound->padding.right != 0.0f) {
        return true;
    }
    return span->bound->border &&
        (span->bound->border->width.left != 0.0f ||
         span->bound->border->width.right != 0.0f);
}

static bool table_view_has_cell_line_content(View* view);

static bool table_inline_span_is_phantom_for_cell_height(ViewSpan* span) {
    if (!span) return true;
    if (table_inline_span_has_inline_axis_decoration(span)) return false;
    for (View* child = span->first_child; child; child = child->next_sibling) {
        if (table_view_has_cell_line_content(child)) return false;
    }
    return true;
}

static bool table_view_has_cell_line_content(View* view) {
    if (!view || !view->view_type || table_cell_vertical_align_skips_child(view)) {
        return false;
    }
    if (view->view_type == RDT_VIEW_TEXT) {
        return view->width > 0.0f && view->height > 0.0f;
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        return !table_inline_span_is_phantom_for_cell_height(
            lam::view_require<RDT_VIEW_INLINE>(view));
    }
    return true;
}

// Measure content height from cell's children
static float measure_cell_content_height(LayoutContext* lycon, ViewTableCell* tcell) {
    bool has_block_content = false;
    float block_content_min_y = 0.0f;   // Track min y of block content (for offset)
    float block_content_max_y = 0.0f;   // Track max bottom of block content
    bool has_inline_content = false;
    float inline_content_min_y = 0.0f;  // Track min y of inline/text content
    float inline_content_max_y = 0.0f;  // Track max bottom of inline/text content

    // Set up line-height for this cell so we can use it for text content measurement
    // This ensures we use the cell's own line-height, not a stale value from lycon
    FontBox saved_font = lycon->font;
    BlockContextScope bscope(lycon);

    if (tcell->font) {
        setup_font(lycon->ui_context, &lycon->font, tcell->font);
    }
    setup_line_height(lycon, tcell);
    float cell_line_height = lycon->block.line_height;

    // Restore context
    lycon->font = saved_font;
    // block auto-restored by bscope destructor

    for_each_table_cell_vertical_align_child(lam::view_require_element(tcell), [&](View* child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            // Track min/max Y for text content to handle multi-line cells with <br> elements
            // Each text node may be on a different line (e.g., y=0, y=20, y=40 for 3 lines)
            // CSS 2.1 §17.5.3: "The height of a cell box is the minimum height required by the content"
            float text_top = text->y;
            // Use the maximum of CSS line-height and actual text bounding box height for each line
            float text_height = max(cell_line_height > 0 ? cell_line_height : 0.0f, text->height);
            float text_bottom = text_top + text_height;

            if (!has_inline_content || text_top < inline_content_min_y) {
                inline_content_min_y = text_top;
                has_inline_content = true;
            }
            if (text_bottom > inline_content_max_y) {
                inline_content_max_y = text_bottom;
            }
        }
        else if (child->view_type == RDT_VIEW_BR) {
            // BR elements also contribute to content extent - they mark line breaks
            // Their Y position indicates where the next line starts
            float br_top = child->y;
            float br_bottom = br_top + child->height;
            if (!has_inline_content || br_top < inline_content_min_y) {
                inline_content_min_y = br_top;
                has_inline_content = true;
            }
            if (br_bottom > inline_content_max_y) {
                inline_content_max_y = br_bottom;
            }
        }
        else if (child->view_type == RDT_VIEW_BLOCK ||
                 child->view_type == RDT_VIEW_LIST_ITEM ||
                 child->view_type == RDT_VIEW_INLINE ||
                 child->view_type == RDT_VIEW_INLINE_BLOCK) {
            if (child->view_type == RDT_VIEW_INLINE &&
                table_inline_span_is_phantom_for_cell_height(
                    lam::view_require<RDT_VIEW_INLINE>(child))) {
                return;
            }
            ViewElement* block = lam::view_require_element(child);
            // Use the actual rendered border-box height (block->height), not the CSS content height
            // which excludes child's border/padding. Children are already laid out at this point.
            float child_height = block->height;

            // CSS 2.1 §10.8.1: For inline non-replaced elements (RDT_VIEW_INLINE),
            // margins, borders, and padding do NOT enter into the line box height
            // calculation. The view height includes border+padding for visual rendering,
            // but for line box purposes we use the element's resolved line-height
            // (stored in content_height during layout_inline).
            if (child->view_type == RDT_VIEW_INLINE) {
                if (block->content_height > 0) {
                    child_height = block->content_height;
                }
                if (cell_line_height > child_height) {
                    child_height = cell_line_height;
                }
            }

            // Track the min y and max bottom of block content for stacked blocks
            // CSS 2.1 §9.4.1: Table cells establish a BFC. Child margins don't collapse
            // through the cell boundary, so they must be included in the content extent.
            // Include margin_top of first child and margin_bottom of last child.
            float child_top = child->y;
            float child_bottom = child->y + child_height;
            if (block->bound) {
                // CSS 2.1 §8.3.1: Self-collapsing blocks (height=0, no border/padding)
                // have their margin.bottom set to a "pending chain" value from the margin
                // collapse algorithm. This value was already consumed by sibling collapse
                // and must NOT be added to the content extent — it would double-count.
                bool is_self_collapsing = (child_height == 0);
                if (is_self_collapsing && block->bound->border) {
                    float bt = block->bound->border->width.top;
                    float bb = block->bound->border->width.bottom;
                    if (bt > 0 || bb > 0) is_self_collapsing = false;
                }
                if (is_self_collapsing) {
                    float pt = block->bound->padding.top;
                    float pb = block->bound->padding.bottom;
                    if (pt > 0 || pb > 0) is_self_collapsing = false;
                }
                if (!is_self_collapsing) {
                    child_top -= block->bound->margin.top;
                    child_bottom += block->bound->margin.bottom;
                }
            }
            if (!has_block_content || child_top < block_content_min_y) {
                block_content_min_y = child_top;
                has_block_content = true;
            }
            if (child_bottom > block_content_max_y) {
                block_content_max_y = child_bottom;
            }
        }
        else if (child->view_type == RDT_VIEW_TABLE) {
            // Handle nested tables - use the table's computed height
            ViewTable* nested_table = lam::view_require<RDT_VIEW_TABLE>(child);
            float table_height = nested_table->height;
            log_debug("%s measure_cell_content: nested table height=%.1f", tcell->source_loc(), table_height);
            // Treat nested tables like block content for extent tracking
            float table_top = child->y;
            float table_bottom = table_top + table_height;
            ViewBlock* table_block = lam::view_require_block(child);
            if (table_block->bound) {
                // CSS 2.1 §9.4.1: table cells establish a BFC, so nested table
                // margins do not collapse through the cell boundary.
                table_top -= table_block->bound->margin.top;
                table_bottom += table_block->bound->margin.bottom;
            }
            if (!has_block_content || table_top < block_content_min_y) {
                block_content_min_y = table_top;
                has_block_content = true;
            }
            if (table_bottom > block_content_max_y) {
                block_content_max_y = table_bottom;
            }
        }
    });

    // Compute combined content height as the full extent from earliest to latest content
    // When a cell has both block and inline content (e.g., a div followed by text),
    // the total is the full vertical span, not max of separate extents.
    float overall_min_y = 0, overall_max_y = 0;
    bool has_any = false;
    if (has_block_content) {
        overall_min_y = block_content_min_y;
        overall_max_y = block_content_max_y;
        has_any = true;
    }
    if (has_inline_content) {
        if (!has_any || inline_content_min_y < overall_min_y) overall_min_y = inline_content_min_y;
        if (!has_any || inline_content_max_y > overall_max_y) overall_max_y = inline_content_max_y;
        has_any = true;
    }
    float content_height = has_any ? (overall_max_y - overall_min_y) : 0.0f;
    if (tcell->content_height > content_height) {
        content_height = tcell->content_height;
        has_any = true;
    }

    log_debug("%s measure_cell_content: inline=%.1f (y:%.1f-%.1f), block=%.1f (y:%.1f-%.1f) -> %.1f", tcell->source_loc(),
              has_inline_content ? (inline_content_max_y - inline_content_min_y) : 0.0f,
              inline_content_min_y, inline_content_max_y,
              has_block_content ? (block_content_max_y - block_content_min_y) : 0.0f,
              block_content_min_y, block_content_max_y, content_height);

    // CSS 2.1 §10.6.3: BFC height includes float descendants.
    // Table cells are BFCs, so recursively find any float descendants whose
    // bottom edge extends beyond the measured direct-child content extent.
    float float_max_y = find_descendant_float_max_y(lam::view_require_element(tcell), 0);
    if (float_max_y > 0) {
        if (!has_any || float_max_y > overall_max_y) {
            overall_max_y = float_max_y;
            has_any = true;
        }
        content_height = has_any ? (overall_max_y - overall_min_y) : 0.0f;
        log_debug("%s measure_cell_content: float_max_y=%.1f, adjusted content_height=%.1f", tcell->source_loc(),
                  float_max_y, content_height);
    }

    // Return measured content height (no artificial minimum)
    return content_height;
}

// Calculate final cell height from content, padding, border
static float calculate_cell_height(LayoutContext* lycon, ViewTableCell* tcell, ViewTable* table,
                                  float content_height, float explicit_height) {
    // CSS 2.1 §17.5.3: Cell height includes content, padding, and border
    // The CSS 'height' property sets the content height (content-box) or total height (border-box)

    // Check box-sizing mode
    bool is_border_box = layout_uses_border_box(tcell);

    // Compute padding
    float pad_top = 0, pad_bottom = 0;
    if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
        pad_top = tcell->bound->padding.top;
        pad_bottom = tcell->bound->padding.bottom;
    }

    // Compute border
    float border_top = 0, border_bottom = 0;
    if (table->tb->border_collapse) {
        border_top = tcell->td->top_resolved ? tcell->td->top_resolved->width : 0.0f;
        border_bottom = tcell->td->bottom_resolved ? tcell->td->bottom_resolved->width : 0.0f;
        float half_borders = (border_top + border_bottom) / 2.0f;
        log_debug("%s Border-collapse cell height: content=%.1f, resolved borders top=%.1f bottom=%.1f, +%.1f", tcell->source_loc(),
                content_height, border_top, border_bottom, half_borders);
        border_top = half_borders / 2.0f;
        border_bottom = half_borders - border_top;
    } else if (tcell->bound && tcell->bound->border) {
        if (tcell->bound->border->top_style != CSS_VALUE_NONE)
            border_top = tcell->bound->border->width.top;
        if (tcell->bound->border->bottom_style != CSS_VALUE_NONE)
            border_bottom = tcell->bound->border->width.bottom;
    }

    // Content-based total height (content + padding + border)
    float content_total = content_height + pad_top + pad_bottom + border_top + border_bottom;

    // CSS Tables: explicit height acts as a minimum, cell grows to fit content
    if (explicit_height > 0) {
        float explicit_total;
        if (is_border_box) {
            explicit_total = explicit_height;
        } else {
            explicit_total = explicit_height + pad_top + pad_bottom + border_top + border_bottom;
        }
        return (content_total > explicit_total) ? content_total : explicit_total;
    }

    return content_total;
}

// CSS 2.1 §17.5.4: Find the baseline of a table cell.
// "The baseline of a cell is the baseline of the first in-flow line box in the cell,
// or the first in-flow table-row in the cell, whichever comes first. If a cell has
// no line box and no in-flow table row, the baseline is the bottom of the content edge."
// Returns distance from the view's top to the first text baseline, or -1 if none found.
static float find_table_row_baseline(LayoutContext* lycon, ViewTableRow* trow);

static float table_row_baseline_callback(LayoutContext* lycon, View* row) {
    return find_table_row_baseline(lycon, lam::view_require<RDT_VIEW_TABLE_ROW>(row));
}

float find_first_baseline_recursive(LayoutContext* lycon, View* parent, float cumulative_y, bool use_normal_lh) {
    return radiant::compute_view_first_text_baseline(
        lycon, parent, cumulative_y, use_normal_lh, true,
        table_row_baseline_callback);
}

// Find the baseline of a table cell (distance from cell's border-box top to first text baseline)
// CSS 2.1 §17.5.4: If no line box and no in-flow table row, the baseline is the
// bottom of the content edge of the cell box.
static float find_cell_baseline(LayoutContext* lycon, ViewTableCell* tcell) {
    float baseline = find_first_baseline_recursive(lycon, static_cast<View*>(tcell), 0);
    if (baseline < 0) {
        // No text found. Check if the cell has non-replaced inline children
        // that create a line box with a strut.
        // CSS 2.1 §17.5.4: "The baseline of a cell is the baseline of the first
        // in-flow line box in the cell." Non-replaced inline children create a line
        // box with a strut, so the baseline is the strut's ascent (font ascent).
        // Note: inline-block (RDT_VIEW_INLINE_BLOCK) is replaced-level and has its
        // own baseline rules (bottom margin edge when empty), so we don't use the
        // strut for those — the content-edge-bottom fallback is more appropriate.
        bool has_line_box = false;
        for (View* child = lam::view_require_element(tcell)->first_child; child; child = child->next_sibling) {
            if (child->view_type == RDT_VIEW_INLINE) {
                has_line_box = true;
                break;
            }
        }
        if (has_line_box && tcell->font) {
            float fallback_ascent = tcell->font->font_size * 0.8f;
            baseline = radiant::compute_font_baseline_ascender(
                lycon, tcell->font, false, fallback_ascent);
            log_debug("find_cell_baseline: cell col=%d row=%d -> strut baseline=%.1f (inline children, no text)",
                      tcell->td->col_index, tcell->td->row_index, baseline);
        }

        if (baseline < 0) {
            // CSS 2.1 §17.5.4: No in-flow line box or table row found.
            // Use the bottom of the content edge as the baseline.
            float content_edge_bottom = tcell->height;
            if (tcell->bound) {
                if (tcell->bound->border) {
                    content_edge_bottom -= tcell->bound->border->width.bottom;
                }
                content_edge_bottom -= tcell->bound->padding.bottom;
            }
            baseline = content_edge_bottom;
            log_debug("find_cell_baseline: cell col=%d row=%d -> content-edge baseline=%.1f (no text found)",
                      tcell->td->col_index, tcell->td->row_index, baseline);
        }
    } else {
        log_debug("find_cell_baseline: cell col=%d row=%d -> baseline=%.1f",
                  tcell->td->col_index, tcell->td->row_index, baseline);
    }
    return baseline;
}

// CSS 2.1 §17.5.4: The baseline of a table row is established by the baselines
// of the cells in that row. Empty cells still have a cell baseline: the bottom
// of their content edge. This is also the baseline used by an inline-table's
// first row when the row contains no text.
static float find_table_row_baseline(LayoutContext* lycon, ViewTableRow* trow) {
    if (!trow) return -1.0f;

    float row_baseline = -1.0f;
    for_each_table_row_cell(trow, [&](ViewTableCell* tcell) {
        if (!tcell->td) return;
        float cell_baseline = tcell->y + find_cell_baseline(lycon, tcell);
        if (cell_baseline > row_baseline) {
            row_baseline = cell_baseline;
        }
    });
    log_debug("%s find_table_row_baseline: baseline=%.1f", trow->source_loc(), row_baseline);
    return row_baseline;
}

static bool table_cell_is_baseline_aligned(ViewTableCell* tcell) {
    return tcell->td && tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BASELINE &&
        !tcell->td->is_empty;
}

// CSS 2.1 §17.5.4: Apply baseline alignment across all cells in a row.
// This must be called after all cells in the row are laid out but before
// the final row height is determined.
// Returns the extra height added to the row from baseline alignment.
static void shift_table_cell_vertical_align_child(View* child, float y_adjustment);

static float apply_row_baseline_alignment(LayoutContext* lycon, ViewTableRow* trow, float* row_height) {
    // Step 1: Check if any cells have baseline alignment
    bool has_baseline_cells = false;
    for_each_table_row_cell(trow, [&](ViewTableCell* tcell) {
        if (table_cell_is_baseline_aligned(tcell)) {
            has_baseline_cells = true;
        }
    });
    if (!has_baseline_cells) return 0;

    log_debug("%s apply_row_baseline_alignment: processing row with baseline-aligned cells", trow->source_loc());

    // Step 2: Find each baseline-aligned cell's baseline (only cells with real baselines)
    float max_baseline = 0;
    int baseline_cell_count = 0;
    for_each_table_row_cell(trow, [&](ViewTableCell* tcell) {
        if (table_cell_is_baseline_aligned(tcell)) {
            float baseline = find_cell_baseline(lycon, tcell);
            if (baseline >= 0) {
                if (baseline > max_baseline) max_baseline = baseline;
                baseline_cell_count++;
            }
        }
    });
    log_debug("%s   row baseline = %.1f (%d cells with real baselines)", trow->source_loc(), max_baseline, baseline_cell_count);

    // Need at least 2 cells with real baselines for alignment to make sense
    if (baseline_cell_count < 2) return 0;

    // Step 3: Shift content in each baseline-aligned cell to align baselines
    // Only shift cells that have a real text baseline (skip cells with no line box)
    for_each_table_row_cell(trow, [&](ViewTableCell* tcell) {
        if (table_cell_is_baseline_aligned(tcell)) {
            float cell_baseline = find_cell_baseline(lycon, tcell);
            if (cell_baseline < 0) return;  // Skip cells without real baselines
            float shift = max_baseline - cell_baseline;

            if (shift > 0.5f) {
                log_debug("%s   cell col=%d: baseline=%.1f, shift=%.1f", trow->source_loc(), tcell->td->col_index, cell_baseline, shift);

                // Shift all children down
                for_each_table_cell_vertical_align_child(lam::view_require_element(tcell), [&](View* child) {
                    shift_table_cell_vertical_align_child(child, shift);
                });

                // The cell now needs more height to accommodate the shifted content
                float content_height = measure_cell_content_height(lycon, tcell);
                float needed_height = content_height;
                // Add padding
                if (tcell->bound) {
                    needed_height += layout_box_metrics(tcell).padding_v;
                }
                // Add border
                if (tcell->bound && tcell->bound->border) {
                    float bt = (tcell->bound->border->top_style != CSS_VALUE_NONE) ? tcell->bound->border->width.top : 0;
                    float bb = (tcell->bound->border->bottom_style != CSS_VALUE_NONE) ? tcell->bound->border->width.bottom : 0;
                    needed_height += bt + bb;
                }
                // Account for the shift (extra space above content)
                needed_height += shift;

                if (needed_height > tcell->height) {
                    tcell->height = needed_height;
                }
            }
        }
    });

    // Step 4: Recalculate row height considering baseline-adjusted cells
    float new_row_height = *row_height;
    for_each_table_row_cell(trow, [&](ViewTableCell* tcell) {
        if (tcell->td && tcell->td->row_span <= 1 && tcell->height > new_row_height) {
            new_row_height = tcell->height;
        }
    });

    if (new_row_height > *row_height) {
        log_debug("%s   row height increased: %.1f -> %.1f (baseline alignment)", trow->source_loc(), *row_height, new_row_height);
        *row_height = new_row_height;
    }

    return new_row_height - *row_height;
}

// Apply vertical alignment to cell children
static float compute_cell_strut_baseline(LayoutContext* lycon, ViewTableCell* tcell) {
    if (!lycon || !tcell) return 0.0f;

    FontBox saved_font = lycon->font;
    BlockContextScope bscope(lycon);

    if (tcell->font) {
        setup_font(lycon->ui_context, &lycon->font, tcell->font);
    }
    setup_line_height(lycon, tcell);
    layout_setup_block_font_metrics(lycon);
    float half_leading = (lycon->block.line_height -
        (lycon->block.init_ascender + lycon->block.init_descender)) / 2.0f;
    float baseline = lycon->block.init_ascender + half_leading;
    lycon->font = saved_font;
    return baseline;
}

static float compute_inline_atomic_baseline_for_cell(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return 0.0f;

    float item_height = block->height + (block->bound ?
        block->bound->margin.top + block->bound->margin.bottom : 0.0f);
    bool is_inline_table = block->view_type == RDT_VIEW_TABLE &&
        (block->display.outer == CSS_VALUE_INLINE ||
         block->display.outer == CSS_VALUE_INLINE_BLOCK);
    if (is_inline_table) {
        float table_baseline = find_first_baseline_recursive(lycon, static_cast<View*>(block), 0.0f, true);
        if (table_baseline >= 0.0f) {
            return (block->bound ? block->bound->margin.top : 0.0f) + table_baseline;
        }
    }
    if (block->display.inner == RDT_DISPLAY_REPLACED) {
        return block->height + (block->bound ? block->bound->margin.top : 0.0f);
    }
    if (block->blk && block->blk->last_line_max_ascender > 0.0f) {
        bool overflow_visible = !block->scroller ||
            (block->scroller->overflow_x == CSS_VALUE_VISIBLE &&
             block->scroller->overflow_y == CSS_VALUE_VISIBLE);
        if (overflow_visible) {
            return (block->bound ? block->bound->margin.top : 0.0f) +
                block->blk->last_line_max_ascender;
        }
    }
    return item_height;
}

static float find_cell_content_top_for_vertical_align(LayoutContext* lycon, ViewTableCell* tcell,
                                                      float fallback_top) {
    float content_top = fallback_top;
    bool found_line_box_top = false;
    float strut_baseline = compute_cell_strut_baseline(lycon, tcell);
    if (strut_baseline <= 0.0f) return content_top;

    for (View* child = lam::view_require_element(tcell)->first_child; child; child = child->next_sibling) {
        if (!child->view_type) continue;
        if (child->view_type == RDT_VIEW_INLINE_BLOCK || child->view_type == RDT_VIEW_TABLE) {
            ViewBlock* block = lam::view_require_block(child);
            float item_height = block->height + (block->bound ?
                block->bound->margin.top + block->bound->margin.bottom : 0.0f);
            if (item_height > 0.5f) continue;
            float item_baseline = compute_inline_atomic_baseline_for_cell(lycon, block);
            float line_top = child->y + item_baseline - strut_baseline;
            if (!found_line_box_top || line_top < content_top) {
                content_top = line_top;
                found_line_box_top = true;
            }
        }
    }
    return content_top;
}

static void shift_table_cell_vertical_align_child(View* child, float y_adjustment) {
    if (!child || !child->view_type) return;

    child->y += y_adjustment;
    if (child->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            rect->y += y_adjustment;
        }
        return;
    }

    if (child->view_type == RDT_VIEW_INLINE) {
        ViewElement* element = lam::view_require_element(child);
        for_each_table_cell_vertical_align_child(element, [&](View* grandchild) {
            shift_table_cell_vertical_align_child(grandchild, y_adjustment);
        });
    }
}

static void apply_cell_vertical_align(LayoutContext* lycon, ViewTableCell* tcell, float cell_height, float content_height) {
    log_debug("%s apply_cell_vertical_align: valign=%d, cell_height=%.1f, content_height=%.1f, is_empty=%d", tcell->source_loc(),
           tcell->td->vertical_align, cell_height, content_height, tcell->td->is_empty);

    // Quick Win #3: Empty cells with baseline alignment should use bottom alignment
    // CSS 2.1: Empty cells don't have a baseline, so treat like bottom-aligned
    if (tcell->td->is_empty && tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BASELINE) {
        log_debug("%s   Empty cell with baseline -> treating as bottom alignment", tcell->source_loc());
        tcell->td->vertical_align = TableCellProp::CELL_VALIGN_BOTTOM;
    }

    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_TOP) {
        return; // No adjustment needed
    }

    // CSS 2.1 §17.5.4: Baseline alignment is handled by apply_row_baseline_alignment()
    // at the row level, since it requires cross-cell coordination.
    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BASELINE) {
        return;
    }

    // Calculate content area by subtracting actual border and padding
    float cell_content_area = cell_height;

    // Subtract actual border widths
    if (tcell->bound && tcell->bound->border) {
        float border_top = (tcell->bound->border->top_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.top : 0.0f;
        float border_bottom = (tcell->bound->border->bottom_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.bottom : 0.0f;
        cell_content_area -= border_top + border_bottom;
        log_debug("%s   Subtracting borders: top=%.1f, bottom=%.1f", tcell->source_loc(), border_top, border_bottom);
    }

    // Subtract padding
    if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
        cell_content_area -= layout_box_metrics(tcell).padding_v;
        log_debug("%s   Subtracting padding: top=%d, bottom=%d", tcell->source_loc(), tcell->bound->padding.top, tcell->bound->padding.bottom);
    }

    log_debug("%s   cell_content_area=%.1f after border/padding subtraction", tcell->source_loc(), cell_content_area);

    // Calculate the content start Y position (border + padding from top)
    float content_start_y = 0.0f;
    if (tcell->bound && tcell->bound->border) {
        if (tcell->bound->border->top_style != CSS_VALUE_NONE) {
            content_start_y += tcell->bound->border->width.top;
        }
    }
    if (tcell->bound && tcell->bound->padding.top >= 0) {
        content_start_y += tcell->bound->padding.top;
    }

    // Calculate target Y position for content based on alignment
    float target_y = content_start_y;  // Default: top alignment
    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_MIDDLE) {
        target_y = content_start_y + (cell_content_area - content_height) / 2.0f;
    } else if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BOTTOM) {
        target_y = content_start_y + (cell_content_area - content_height);
    }

    // CSS 2.1 §17.5.4: Vertical alignment positions content within the cell's content area.
    // We find where content actually starts by scanning children, accounting for margins.
    // For block children with margins (e.g., margin-top: 50px), the margin-box top is
    // child.y - margin.top, which gives the true start of the content extent.
    // This preserves margin spacing: if a child has margin pushing it down, the
    // adjustment is relative to the margin-box top, not the child's border-box position.
    // Skip children with view_type == 0 (uninitialized views, e.g. collapsed whitespace nodes).
    float current_content_top = 1e8f;
    for_each_table_cell_vertical_align_child(lam::view_require_element(tcell), [&](View* child) {
        float ct = child->y;
        if (child->view_type == RDT_VIEW_BLOCK ||
            child->view_type == RDT_VIEW_LIST_ITEM ||
            child->view_type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* block = lam::view_require_block(child);
            if (block->bound) {
                ct -= block->bound->margin.top;
            }
        }
        if (ct < current_content_top) current_content_top = ct;
    });
    if (current_content_top >= 1e8f) return; // no content

    current_content_top = find_cell_content_top_for_vertical_align(lycon, tcell, current_content_top);

    float y_adjustment = target_y - current_content_top;
    log_debug("%s   vertical-align: target_y=%.1f, current_content_top=%.1f, y_adjustment=%.1f", tcell->source_loc(),
             target_y, current_content_top, y_adjustment);

    if (y_adjustment != 0) {
        for_each_table_cell_vertical_align_child(lam::view_require_element(tcell), [&](View* child) {
            shift_table_cell_vertical_align_child(child, y_adjustment);
        });
    }
}

// Position text children within a cell (relative coordinates)
static void position_cell_text_children(ViewTableCell* tcell) {
    float content_x = 1; // 1px border
    float content_y = 1;

    if (tcell->bound) {
        content_x += tcell->bound->padding.left;
        content_y += tcell->bound->padding.top;
    }

    for (View* child = lam::view_require_element(tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            child->x = content_x;
            child->y = content_y;
        }
    }
}

// Calculate cell width from column widths (for colspan support)
template <typename Fn>
static int for_each_table_span_column(int start_col, int span, int columns, Fn fn) {
    if (span <= 0 || start_col >= columns) return 0;
    int end_col = start_col + span;
    int count = 0;
    for (int c = start_col; c < end_col && c < columns; c++) {
        fn(c);
        count++;
    }
    return count;
}

static float table_sum_span_columns(float* col_widths, int start_col, int span, int columns) {
    if (!col_widths) return 0.0f;
    float width = 0.0f;
    for_each_table_span_column(start_col, span, columns, [&](int c) {
        width += col_widths[c];
    });
    return width;
}

static void table_assign_span_columns(float* col_widths, int start_col, int span,
                                      int columns, float width, float* assigned_total) {
    if (!col_widths) return;
    for_each_table_span_column(start_col, span, columns, [&](int c) {
        col_widths[c] = width;
        if (assigned_total) *assigned_total += width;
    });
}

static void table_assign_columns(float* col_widths, int columns, float width) {
    table_assign_span_columns(col_widths, 0, columns, columns, width, NULL);
}

static void table_copy_columns(float* dst, float* src, int columns) {
    if (!dst || !src) return;
    for_each_table_span_column(0, columns, columns, [&](int c) {
        dst[c] = src[c];
    });
}

static void table_scale_columns(float* col_widths, int columns, float scale) {
    if (!col_widths) return;
    for_each_table_span_column(0, columns, columns, [&](int c) {
        col_widths[c] *= scale;
    });
}

static void table_position_row_group_box(ViewTable* table, TableMetadata* meta,
                                         ViewBlock* child, float* col_widths,
                                         float* col_x_positions, int columns,
                                         bool has_direct_float, float* current_y) {
    float tbody_content_width = 0.0f;
    if (table->tb->border_collapse) {
        tbody_content_width = col_x_positions[columns] - col_x_positions[0];
        log_debug("%s Border-collapse tbody width: col_positions[%d]=%.1f - col_positions[0]=%.1f = %.1f",
                  table->source_loc(), columns, col_x_positions[columns],
                  col_x_positions[0], tbody_content_width);
    } else {
        tbody_content_width = table_sum_span_columns(col_widths, 0, columns, columns);
        if (table->tb->border_spacing_h > 0.0f && columns > 1) {
            tbody_content_width += (columns - 1) * table->tb->border_spacing_h;
        }
    }

    // table-internal floats can leave a row group with no formal columns.
    if (tbody_content_width <= 0.0f && table->width > 0.0f && columns == 0) {
        tbody_content_width = table->width;
        log_debug("%s No columns: using table->width %.1f for tbody_content_width",
                  table->source_loc(), table->width);
    }

    *current_y = table_clear_direct_float_intrusion(
        table, *current_y, tbody_content_width, has_direct_float);

    float float_shift_x = 0.0f;
    if (has_direct_float && table->width > 0.0f && tbody_content_width > 0.0f) {
        float left_intrusion = table_direct_left_float_intrusion(table, *current_y, table->width);
        float max_shift = table->width - tbody_content_width;
        if (max_shift < 0.0f) max_shift = 0.0f;
        if (left_intrusion > max_shift) left_intrusion = max_shift;
        float_shift_x = left_intrusion;
    }

    child->x = table->tb->border_collapse
        ? meta->collapsed_border_left / 2.0f + float_shift_x
        : col_x_positions[0] + float_shift_x;
    child->y = *current_y;
    child->width = tbody_content_width;
    log_debug("%s Row group positioned at x=%.1f, y=%.1f, width=%.1f (tbody_content_width=%.1f, columns=%d)",
              table->source_loc(), child->x, child->y, child->width,
              tbody_content_width, columns);
}

static bool table_columns_within_tolerance(float* col_widths, int columns, float tolerance) {
    if (!col_widths || columns <= 0) return false;
    float first_width = col_widths[0];
    for (int i = 1; i < columns; i++) {
        if (fabsf(col_widths[i] - first_width) > tolerance) return false;
    }
    return true;
}

static void table_grow_percent_columns(TableMetadata* meta, float* col_widths,
                                       int columns, float extra) {
    if (!meta || !col_widths) return;
    float percent_total = 0.0f;
    for_each_table_span_column(0, columns, columns, [&](int c) {
        if (meta->col_percent_widths[c] > 0.0f) percent_total += meta->col_percent_widths[c];
    });
    if (percent_total <= 0.0f) return;
    for_each_table_span_column(0, columns, columns, [&](int c) {
        if (meta->col_percent_widths[c] > 0.0f) {
            col_widths[c] += extra * meta->col_percent_widths[c] / percent_total;
        }
    });
}

static bool table_apply_percent_column_distribution(TableMetadata* meta, float* col_widths,
                                                    int columns, float total_percent_col_width,
                                                    float available_content_width,
                                                    float min_table_content_width) {
    if (!meta || !col_widths || total_percent_col_width <= 0.0f ||
        available_content_width <= min_table_content_width) {
        return false;
    }

    float assigned_total = 0.0f;
    for (int i = 0; i < columns; i++) {
        float percent = meta->col_percent_widths[i];
        float min_floor = meta->col_single_min_widths[i] > 0.0f
            ? meta->col_single_min_widths[i] : 0.0f;
        float target = 0.0f;
        if (percent > 0.0f) {
            target = available_content_width * percent / 100.0f;
        } else {
            target = meta->col_max_widths[i] > min_floor
                ? meta->col_max_widths[i] : min_floor;
        }
        if (target < min_floor) target = min_floor;
        col_widths[i] = target;
        assigned_total += target;
    }

    if (assigned_total > available_content_width) {
        float excess = assigned_total - available_content_width;
        while (excess > 0.01f) {
            float shrink_capacity = 0.0f;
            for (int i = 0; i < columns; i++) {
                if (col_widths[i] > meta->col_min_widths[i]) {
                    shrink_capacity += col_widths[i] - meta->col_min_widths[i];
                }
            }
            if (shrink_capacity <= 0.01f) break;

            float shrink_step = excess < shrink_capacity ? excess : shrink_capacity;
            for (int i = 0; i < columns; i++) {
                float capacity = col_widths[i] - meta->col_min_widths[i];
                if (capacity <= 0.0f) continue;
                float amount = shrink_step * capacity / shrink_capacity;
                if (amount > capacity) amount = capacity;
                col_widths[i] -= amount;
            }
            excess -= shrink_step;
        }
    } else if (assigned_total < available_content_width) {
        float extra = available_content_width - assigned_total;
        float auto_grow_base_total = 0.0f;
        int auto_grow_count = 0;
        for (int i = 0; i < columns; i++) {
            if (meta->col_percent_widths[i] <= 0.0f) {
                auto_grow_base_total += col_widths[i];
                auto_grow_count++;
            }
        }
        if (auto_grow_count > 0) {
            for (int i = 0; i < columns; i++) {
                if (meta->col_percent_widths[i] > 0.0f) continue;
                if (auto_grow_base_total > 0.0f) {
                    col_widths[i] += extra * col_widths[i] / auto_grow_base_total;
                } else {
                    col_widths[i] += extra / auto_grow_count;
                }
            }
        } else {
            table_grow_percent_columns(meta, col_widths, columns, extra);
        }
    }

    log_debug("CSS table percent distribution: available=%.1fpx, percent_total=%.1f%%",
              available_content_width, total_percent_col_width);
    return true;
}

static void table_apply_auto_column_width_distribution(TableMetadata* meta, float* col_widths,
                                                       int columns, float available_content_width,
                                                       float min_table_content_width,
                                                       float pref_table_content_width) {
    if (!meta || !col_widths) return;
    if (fabsf(available_content_width - pref_table_content_width) < 0.01f) {
        // Case 1: Perfect fit - use preferred widths directly
        log_debug("CSS 2.1 Case 1: Perfect fit - using PCW directly");
        table_copy_columns(col_widths, meta->col_max_widths, columns);
    } else if (available_content_width > pref_table_content_width) {
        // Case 2: Table wider than preferred - distribute extra space
        // Columns with explicit CSS widths keep their preferred width;
        // extra space is distributed only among auto-width columns.
        float extra_space = available_content_width - pref_table_content_width;

        log_debug("CSS 2.1 Case 2: Table wider than preferred - distributing %.1fpx extra", extra_space);

        // Calculate total preferred width of auto-width columns only
        float auto_pref_total = 0;
        int auto_col_count = 0;
        for (int i = 0; i < columns; i++) {
            if (!meta->col_has_explicit_width[i]) {
                auto_pref_total += meta->col_max_widths[i];
                auto_col_count++;
            }
        }

        if (auto_col_count > 0) {
            // Distribute extra space only to auto-width columns
            for (int i = 0; i < columns; i++) {
                if (meta->col_has_explicit_width[i]) {
                    col_widths[i] = meta->col_max_widths[i];
                } else if (auto_pref_total > 0) {
                    float extra_for_col = (extra_space * meta->col_max_widths[i]) / auto_pref_total;
                    col_widths[i] = meta->col_max_widths[i] + extra_for_col;
                } else {
                    float equal_share = extra_space / auto_col_count;
                    col_widths[i] = equal_share;
                }
            }
        } else {
            // All columns have explicit widths - distribute proportionally to all
            for (int i = 0; i < columns; i++) {
                if (pref_table_content_width > 0) {
                    float extra_for_col = (extra_space * meta->col_max_widths[i]) / pref_table_content_width;
                    col_widths[i] = meta->col_max_widths[i] + extra_for_col;
                } else {
                    col_widths[i] = meta->col_max_widths[i];
                }
            }
        }
    } else {
        // Case 3: Table narrower than preferred - CSS 2.1 constrained distribution
        log_debug("CSS 2.1 Case 3: Table narrower than preferred - constrained distribution");

        if (available_content_width >= min_table_content_width) {
            // Can fit minimum widths - scale between min and preferred
            log_debug("Scaling between MCW and PCW (available=%.1f, min=%.1f, pref=%.1f)",
                     available_content_width, min_table_content_width, pref_table_content_width);

            for (int i = 0; i < columns; i++) {
                float min_w = meta->col_min_widths[i];
                float pref_w = meta->col_max_widths[i];
                float range = pref_w - min_w;

                if (pref_table_content_width > min_table_content_width && range > 0) {
                    // Linear interpolation between min and preferred
                    float factor = (available_content_width - min_table_content_width) /
                                   (pref_table_content_width - min_table_content_width);
                    col_widths[i] = min_w + range * factor;
                } else {
                    col_widths[i] = min_w; // Fallback to minimum
                }
            }
        } else {
            // Cannot fit minimum widths - use minimum and overflow
            log_debug("Cannot fit MCW - using minimum widths (will overflow)");
            table_copy_columns(col_widths, meta->col_min_widths, columns);
        }
    }
}

static float table_apply_auto_available_width_constraint(
    LayoutContext* lycon, ViewTable* table, TableMetadata* meta,
    float* pref_table_width, float min_table_width) {
    if (!pref_table_width) return 0.0f;

    float container_width = 0.0f;
    bool margins_already_subtracted = false;
    bool table_box_already_subtracted = false;

    // float-avoidance BFCs already hand over a reduced content box.
    if (lycon->block.content_width > 0.0f) {
        container_width = lycon->block.content_width;
        margins_already_subtracted = true;
        table_box_already_subtracted = true;
        log_debug("Container width from lycon content_width (BFC float avoidance): %.1fpx",
                  container_width);
    }

    if (container_width <= 0.0f) {
        ViewBlock* parent = lam::view_as_block(static_cast<View*>(table->parent));
        if (parent && parent->width > 0.0f) {
            container_width = parent->width;
            if (parent->bound) {
                container_width -= layout_box_metrics(parent).pad_border_h;
            }
            log_debug("Container width from parent element: %.1fpx (parent->width=%.1f)",
                      container_width, parent->width);
        }
    }

    if (container_width <= 0.0f) {
        container_width = lycon->line.right - lycon->line.left;
    }

    if (container_width <= 0.0f && lycon->available_space.width.is_definite()) {
        container_width = lycon->available_space.width.to_px_or_zero();
    }

    if (!margins_already_subtracted) {
        float margin_left = 0.0f, margin_right = 0.0f;
        if (table->bound) {
            margin_left = table->bound->margin.left;
            margin_right = table->bound->margin.right;
        }
        container_width -= margin_left + margin_right;
        log_debug("Auto table width constraint: container=%.1fpx (after subtracting margins %.1f+%.1f)",
                  container_width, margin_left, margin_right);
    }

    float table_horizontal_overhead = 0.0f;
    if (!table_box_already_subtracted) {
        if (!table->tb->border_collapse && table->bound) {
            BoxMetrics table_box = layout_box_metrics(table);
            if (table->bound->padding.left > 0.0f) {
                table_horizontal_overhead += table->bound->padding.left;
            }
            if (table->bound->padding.right > 0.0f) {
                table_horizontal_overhead += table->bound->padding.right;
            }
            table_horizontal_overhead += table_box.border_h;
        } else if (table->tb->border_collapse) {
            table_horizontal_overhead += meta->collapsed_border_left / 2.0f +
                meta->collapsed_border_right / 2.0f;
        }
    }

    float max_available_width = container_width - table_horizontal_overhead;
    if (max_available_width < 0.0f) max_available_width = 0.0f;

    log_debug("Auto table max available grid width: %.1fpx (container=%.1f, table overhead=%.1f)",
              max_available_width, container_width, table_horizontal_overhead);

    if (max_available_width > 0.0f && *pref_table_width > max_available_width) {
        log_debug("Constraining preferred width from %.1fpx to %.1fpx (available space minus margins)",
                  *pref_table_width, max_available_width);
        *pref_table_width = max_available_width;
    } else if (max_available_width == 0.0f && *pref_table_width > min_table_width) {
        log_debug("Margins consume all space, using minimum content width: %.1fpx",
                  min_table_width);
        *pref_table_width = min_table_width;
    }

    return max_available_width;
}

static float table_explicit_content_area_for_auto_layout(ViewTable* table,
                                                         TableMetadata* meta,
                                                         float explicit_table_width) {
    float explicit_content_area = explicit_table_width;
    if (table->tb->border_collapse) {
        float half_left = meta->collapsed_border_left / 2.0f;
        float half_right = meta->collapsed_border_right / 2.0f;
        explicit_content_area -= (half_left + half_right);
        log_debug("Explicit content area (border-collapse, half borders %.1f+%.1f): %.1fpx",
                  half_left, half_right, explicit_content_area);
        return explicit_content_area;
    }

    // Separate-border content-box widths already exclude padding and border.
    bool is_border_box = layout_uses_border_box(table);
    if (is_border_box && table->bound && table->bound->border) {
        explicit_content_area -= layout_box_metrics(table).border_h;
    }
    if (is_border_box && table->bound &&
        table->bound->padding.left >= 0.0f && table->bound->padding.right >= 0.0f) {
        explicit_content_area -= layout_box_metrics(table).padding_h;
    }
    log_debug("Explicit content area (separate borders, border_box=%d): %.1fpx",
              is_border_box, explicit_content_area);
    return explicit_content_area;
}

static float table_fixed_css_padding_box_width(ViewTable* table, float fixed_table_width) {
    float css_padding_box = fixed_table_width;
    bool fixed_is_border_box = table->tb->border_collapse || layout_uses_border_box(table);
    if (fixed_is_border_box && table->bound && table->bound->border) {
        css_padding_box -= layout_box_metrics(table).border_h;
    } else if (!fixed_is_border_box && table->bound) {
        if (table->bound->padding.left >= 0.0f) css_padding_box += table->bound->padding.left;
        if (table->bound->padding.right >= 0.0f) css_padding_box += table->bound->padding.right;
    }
    return css_padding_box;
}

static void table_apply_minmax_width_constraints(ViewTable* table, TableMetadata* meta,
                                                 float* col_widths, int columns,
                                                 float* table_width,
                                                 float table_padding_horizontal) {
    if (!table->blk || !table_width) return;

    float early_border_width = table->tb->border_collapse
        ? meta->collapsed_border_left / 2.0f + meta->collapsed_border_right / 2.0f
        : ((table->bound && table->bound->border) ? layout_box_metrics(table).border_h : 0.0f);
    float old_table_width = *table_width;
    float border_box_width = *table_width + early_border_width;
    bool minmax_is_border_box = table->tb->border_collapse || layout_uses_border_box(table);
    float minmax_extra = minmax_is_border_box ? 0.0f :
        early_border_width + table_padding_horizontal;

    // max-width cannot compress a table below its minimum content border-box.
    if (table->blk->given_max_width >= 0.0f) {
        float max_w_bb = table->blk->given_max_width + minmax_extra;
        float min_col_total = table_sum_span_columns(meta->col_min_widths, 0, columns, columns);
        float min_spacing = 0.0f;
        if (!table->tb->border_collapse && table->tb->border_spacing_h > 0.0f) {
            min_spacing = (columns + 1) * table->tb->border_spacing_h;
        }
        float min_bb = min_col_total + min_spacing + table_padding_horizontal + early_border_width;
        if (max_w_bb < min_bb) max_w_bb = min_bb;
        if (border_box_width > max_w_bb) {
            *table_width = max_w_bb - early_border_width;
            if (*table_width < 0.0f) *table_width = 0.0f;
            log_debug("Table width clamped to max-width: %.0fpx (border-box: %.0f)",
                      *table_width, max_w_bb);
        }
    }
    if (table->blk->given_min_width >= 0.0f) {
        float min_w_bb = table->blk->given_min_width + minmax_extra;
        if (border_box_width < min_w_bb) {
            *table_width = min_w_bb - early_border_width;
            log_debug("Table width clamped to min-width: %.0fpx (border-box: %.0f)",
                      *table_width, min_w_bb);
        }
    }
    if (*table_width == old_table_width || columns <= 0) return;

    float overhead = table_padding_horizontal;
    if (!table->tb->border_collapse && table->tb->border_spacing_h > 0.0f) {
        overhead += 2.0f * table->tb->border_spacing_h;
        if (columns > 1) overhead += (columns - 1) * table->tb->border_spacing_h;
    }
    float new_col_total = *table_width - overhead;
    if (new_col_total < 0.0f) new_col_total = 0.0f;
    float old_col_total = old_table_width - overhead;
    if (old_col_total > 0.0f) {
        table_scale_columns(col_widths, columns, new_col_total / old_col_total);
    } else if (new_col_total > 0.0f) {
        table_assign_columns(col_widths, columns, new_col_total / columns);
    }
    log_debug("Redistributed column widths after min/max: new_col_total=%.0f", new_col_total);
}

static float table_prepare_final_padding_box_width(ViewTable* table, TableMetadata* meta,
                                                   float* col_widths, int columns,
                                                   float* table_padding_horizontal) {
    if (table_padding_horizontal) *table_padding_horizontal = 0.0f;

    for (int i = 0; i < columns; i++) {
        if (!meta->col_collapsed[i]) continue;
        meta->col_original_widths[i] = col_widths[i];
        log_debug("Zeroing collapsed column %d width (was %.1f, saved to original)", i, col_widths[i]);
        col_widths[i] = 0.0f;
        meta->col_min_widths[i] = 0.0f;
        meta->col_max_widths[i] = 0.0f;
    }

    float table_width = table_sum_span_columns(col_widths, 0, columns, columns);
    for (int i = 0; i < columns; i++) {
        log_debug("Final column %d width: %.1fpx", i, col_widths[i]);
    }
    log_debug("Final table width: %.1fpx", table_width);
    log_debug("table_width before border adjustments: %.1f, border_collapse=%d",
              table_width, table->tb->border_collapse);

    if (table->tb->border_collapse) {
        log_debug("Border-collapse: col_widths already include border halves, table_width=%.1fpx",
                  table_width);
    } else if (table->tb->border_spacing_h > 0.0f) {
        log_debug("Applying border-spacing %fpx to table width", table->tb->border_spacing_h);
        if (columns > 1) table_width += (columns - 1) * table->tb->border_spacing_h;
        table_width += 2.0f * table->tb->border_spacing_h;
        log_debug("Border-spacing applied (%.1fpx) - table width: %.1f (includes edge spacing)",
                  table->tb->border_spacing_h, table_width);
    }

    if (!table->tb->border_collapse && table->bound &&
        table->bound->padding.left >= 0.0f && table->bound->padding.right >= 0.0f) {
        float padding_h = layout_box_metrics(table).padding_h;
        if (table_padding_horizontal) *table_padding_horizontal = padding_h;
        table_width += padding_h;
        log_debug("Added table padding horizontal: %.1fpx (left=%.1f, right=%.1f)",
                  padding_h, table->bound->padding.left, table->bound->padding.right);
    }
    return table_width;
}

static int table_positive_span_attr(ViewElement* element) {
    const char* span_str = element ? element->get_attribute("span") : NULL;
    int span = (span_str && *span_str) ? (int)str_to_int64_default(span_str, strlen(span_str), 0) : 1; // INT_CAST_OK: span count from attribute
    return span > 0 ? span : 1;
}

static float table_resolve_fixed_explicit_width(LayoutContext* lycon, ViewTable* table) {
    float fixed_explicit_width = 0.0f;
    if (table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem && dom_elem->specified_style) {
            CssDeclaration* width_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->value) {
                if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    double percentage = width_decl->value->data.percentage.value;
                    float container_width = lycon->available_space.width.is_definite()
                        ? lycon->available_space.width.value
                        : lycon->block.content_width;
                    if (container_width <= 0.0f) {
                        container_width = lycon->line.right - lycon->line.left;
                    }
                    if (container_width > 0.0f) {
                        fixed_explicit_width = container_width * percentage / 100.0;
                        log_debug("FIXED LAYOUT - percentage width: %.1f%% of %.1fpx = %.1fpx",
                                  percentage, container_width, fixed_explicit_width);
                    }
                } else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    fixed_explicit_width = resolve_length_value(
                        lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                    log_debug("FIXED LAYOUT - read table CSS width: %.1fpx",
                              fixed_explicit_width);
                }
            }
        }
    }

    if (fixed_explicit_width == 0.0f && lycon->block.given_width > 0.0f) {
        fixed_explicit_width = lycon->block.given_width;
        log_debug("FIXED LAYOUT - using given_width: %.1fpx", fixed_explicit_width);
    }
    return fixed_explicit_width;
}

static float table_fixed_content_width_for_columns(ViewTable* table,
                                                   float fixed_explicit_width,
                                                   int columns) {
    float content_width = fixed_explicit_width;
    bool fixed_width_is_border_box = table->tb->border_collapse ||
        layout_uses_border_box(table);
    if (fixed_width_is_border_box) {
        float table_border_h = 0.0f;
        if (table->bound && table->bound->border) {
            table_border_h = layout_box_metrics(table).border_h;
        }
        content_width -= table_border_h;
    }

    // fixed-layout column percentages resolve against content tracks, not table border or spacing overhead.
    if (!table->tb->border_collapse && table->tb->border_spacing_h > 0.0f) {
        float spacing_h = (columns + 1) * table->tb->border_spacing_h;
        content_width -= spacing_h;
        log_debug("Subtracting border-spacing: (%d+1)*%.1f = %.1f",
                  columns, table->tb->border_spacing_h, spacing_h);
    }

    log_debug("Content width for columns: %.1fpx", content_width);
    return content_width;
}

static float table_resolve_percent_or_length_width(LayoutContext* lycon,
                                                   const CssValue* value,
                                                   float percentage_basis) {
    if (!value) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return (float)(percentage_basis * value->data.percentage.value / 100.0);
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        return resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value);
    }
    return 0.0f;
}

static float table_resolve_fixed_column_css_width(LayoutContext* lycon, ViewElement* column,
                                                  float content_width) {
    if (!column || column->node_type != DOM_NODE_ELEMENT) return 0.0f;
    DomElement* dom_col = column->as_element();
    if (!dom_col || !dom_col->specified_style) return 0.0f;

    CssDeclaration* w_decl = style_tree_get_declaration(dom_col->specified_style, CSS_PROPERTY_WIDTH);
    if (!w_decl || !w_decl->value) return 0.0f;
    return table_resolve_percent_or_length_width(lycon, w_decl->value, content_width);
}

static int table_apply_fixed_column_css_width(LayoutContext* lycon, ViewElement* column,
                                              float* explicit_col_widths, int col_idx,
                                              int columns, float content_width,
                                              float* total_explicit, const char* source_desc) {
    int span = table_positive_span_attr(column);
    float col_width = table_resolve_fixed_column_css_width(lycon, column, content_width);
    if (col_width > 0.0f) {
        float per_col = col_width / span;
        table_assign_span_columns(explicit_col_widths, col_idx, span,
                                  columns, per_col, total_explicit);
        log_debug("FIXED LAYOUT col %d (span=%d): %.1fpx %s",
                  col_idx, span, col_width, source_desc);
    }
    return span;
}

static float table_resolve_fixed_first_row_cell_width(LayoutContext* lycon, ViewTable* table,
                                                      ViewTableCell* cell, float content_width,
                                                      int col) {
    float cell_width = 0.0f;
    if (cell->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = cell->as_element();
        if (dom_elem && dom_elem->specified_style) {
            CssDeclaration* width_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->value) {
                cell_width = table_resolve_percent_or_length_width(
                    lycon, width_decl->value, content_width);
                if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    double percentage = width_decl->value->data.percentage.value;
                    log_debug("  Column %d: percentage width %.1f%% of %.1fpx = %.1fpx",
                              col, percentage, content_width, cell_width);
                } else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    log_debug("  Column %d: absolute width %.1fpx", col, cell_width);
                }
            }
        }
    }

    if (cell_width <= 0.0f || layout_uses_border_box(cell)) return cell_width;
    if (cell->bound && cell->bound->padding.left >= 0 && cell->bound->padding.right >= 0) {
        cell_width += layout_box_metrics(cell).padding_h;
    }
    if (!table->tb->border_collapse && cell->bound && cell->bound->border) {
        if (cell->bound->border->left_style != CSS_VALUE_NONE) {
            cell_width += cell->bound->border->width.left;
        }
        if (cell->bound->border->right_style != CSS_VALUE_NONE) {
            cell_width += cell->bound->border->width.right;
        }
    }
    return cell_width;
}

static int table_apply_fixed_first_row_cell_width(LayoutContext* lycon, ViewTable* table,
                                                  ViewTableCell* cell,
                                                  float* explicit_col_widths, int col,
                                                  int columns, float content_width,
                                                  float* total_explicit,
                                                  int* unspecified_cols) {
    int span = cell->td->col_span;
    if (explicit_col_widths[col] > 0.0f) {
        log_debug("  Column %d: already set by <col> element (%.1fpx), skipping cell",
                  col, explicit_col_widths[col]);
        return span;
    }

    float cell_width = table_resolve_fixed_first_row_cell_width(
        lycon, table, cell, content_width, col);
    if (cell_width > 0.0f) {
        float per_col_width = cell_width / span;
        table_assign_span_columns(explicit_col_widths, col, span,
                                  columns, per_col_width, total_explicit);
        log_debug("  Column %d (span=%d): %.1fpx each (total=%.1fpx)",
                  col, span, per_col_width, cell_width);
    } else {
        *unspecified_cols += span;
        log_debug("  Column %d (span=%d): no explicit width", col, span);
    }
    return span;
}

static void table_distribute_fixed_column_widths(float* explicit_col_widths, int columns,
                                                 float* content_width,
                                                 float total_explicit,
                                                 int unspecified_cols) {
    if (!explicit_col_widths || columns <= 0 || !content_width) return;
    if (total_explicit > 0.0f) {
        log_debug("Found %d columns with explicit widths (total: %.1fpx), %d unspecified",
                  columns - unspecified_cols, total_explicit, unspecified_cols);

        float remaining_width = *content_width - total_explicit;
        if (unspecified_cols > 0 && remaining_width > 0.0f) {
            float width_per_unspecified = remaining_width / unspecified_cols;
            for (int i = 0; i < columns; i++) {
                if (explicit_col_widths[i] == 0.0f) {
                    explicit_col_widths[i] = width_per_unspecified;
                }
            }
            log_debug("Distributing %.1fpx to %d unspecified columns (%.1fpx each)",
                      remaining_width, unspecified_cols, width_per_unspecified);
        } else if (remaining_width < 0.0f) {
            // CSS 2.1 §17.5.2.1: if explicit columns exceed table width, the table widens.
            log_debug("FIXED LAYOUT: column widths exceed table width (%.1f > %.1f), expanding table",
                      total_explicit, *content_width);
            *content_width = total_explicit;
        }
    } else {
        float width_per_col = *content_width / columns;
        table_assign_columns(explicit_col_widths, columns, width_per_col);
        log_debug("No explicit widths - equal distribution: %.1fpx per column", width_per_col);
    }
}

static float table_resolve_fixed_explicit_height(LayoutContext* lycon, ViewTable* table) {
    float explicit_height = 0.0f;
    if (table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem && dom_elem->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value) {
                explicit_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                if (explicit_height > 0.0f) {
                    log_debug("FIXED LAYOUT - read table CSS height: %.1fpx (resolved)", explicit_height);
                }
            }
        }
        if (explicit_height <= 0.0f && table->blk && table->blk->given_height > 0.0f) {
            explicit_height = table->blk->given_height;
            log_debug("FIXED LAYOUT - read table HTML height attribute: %.1fpx", explicit_height);
        }
    }
    return explicit_height;
}

static void table_apply_fixed_height_distribution(LayoutContext* lycon, ViewTable* table, int rows) {
    float explicit_table_height = table_resolve_fixed_explicit_height(lycon, table);
    if (explicit_table_height <= 0.0f) return;

    log_debug("=== FIXED LAYOUT HEIGHT DISTRIBUTION ===");
    log_debug("Total rows to distribute height: %d", rows);

    float content_height = explicit_table_height;
    bool height_is_border_box = table->tb->border_collapse || layout_uses_border_box(table);
    if (height_is_border_box && table->bound && table->bound->border) {
        content_height -= layout_box_metrics(table).border_v;
    }
    if (!table->tb->border_collapse && table->bound) {
        if (table->bound->padding.top >= 0.0f) content_height -= table->bound->padding.top;
        if (table->bound->padding.bottom >= 0.0f) content_height -= table->bound->padding.bottom;
    }
    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0.0f && rows > 0) {
        content_height -= (rows + 1) * table->tb->border_spacing_v;
        log_debug("Subtracting vertical border-spacing: (%d+1)*%.1f = %.1f",
                  rows, table->tb->border_spacing_v, (rows + 1) * table->tb->border_spacing_v);
    }

    table->tb->fixed_row_height = rows > 0 ? content_height / rows : 0.0f;
    log_debug("Height per row: %.1fpx (content_height=%.1f / rows=%d)",
              table->tb->fixed_row_height, content_height, rows);
    log_debug("=== FIXED LAYOUT HEIGHT DISTRIBUTION COMPLETE ===");
}

static void table_raise_column_width_constraints(TableMetadata* meta, float* col_widths,
                                                 int col, float width) {
    if (!meta || !col_widths || col < 0 || col >= meta->column_count || width <= 0.0f) return;
    if (width > meta->col_min_widths[col]) meta->col_min_widths[col] = width;
    if (width > meta->col_max_widths[col]) meta->col_max_widths[col] = width;
    if (width > col_widths[col]) col_widths[col] = width;
}

static void table_clamp_column_max_width(TableMetadata* meta, float* col_widths,
                                         int col, float max_width) {
    if (!meta || !col_widths || col < 0 || col >= meta->column_count || max_width < 0.0f) return;
    if (meta->col_max_widths[col] > max_width) meta->col_max_widths[col] = max_width;
    if (col_widths[col] > max_width) col_widths[col] = max_width;
}

static float table_resolve_column_length_constraint(LayoutContext* lycon, ViewBlock* col_elem,
                                                    CssPropertyId property, float box_value,
                                                    bool has_box_value, float unset_value) {
    if (has_box_value) return box_value;
    if (!col_elem || !col_elem->specified_style) return unset_value;
    CssDeclaration* decl = style_tree_get_declaration(col_elem->specified_style, property);
    if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        return resolve_length_value(lycon, property, decl->value);
    }
    return unset_value;
}

static void table_apply_column_constraints(LayoutContext* lycon, TableMetadata* meta,
                                           float* col_widths, int col,
                                           ViewBlock* col_elem, float width_divisor) {
    float col_css_width = table_resolve_column_length_constraint(
        lycon, col_elem, CSS_PROPERTY_WIDTH,
        col_elem->blk ? col_elem->blk->given_width : 0.0f,
        col_elem->blk && col_elem->blk->given_width > 0.0f, 0.0f);
    if (col_css_width > 0.0f) {
        table_raise_column_width_constraints(
            meta, col_widths, col,
            width_divisor > 1.0f ? col_css_width / width_divisor : col_css_width);
    }

    float col_min_w = table_resolve_column_length_constraint(
        lycon, col_elem, CSS_PROPERTY_MIN_WIDTH,
        col_elem->blk ? col_elem->blk->given_min_width : -1.0f,
        col_elem->blk && col_elem->blk->given_min_width >= 0.0f, -1.0f);
    if (col_min_w > 0.0f) {
        table_raise_column_width_constraints(
            meta, col_widths, col,
            width_divisor > 1.0f ? col_min_w / width_divisor : col_min_w);
    }

    float col_max_w = table_resolve_column_length_constraint(
        lycon, col_elem, CSS_PROPERTY_MAX_WIDTH,
        col_elem->blk ? col_elem->blk->given_max_width : -1.0f,
        col_elem->blk && col_elem->blk->given_max_width >= 0.0f, -1.0f);
    if (col_max_w >= 0.0f) {
        table_clamp_column_max_width(
            meta, col_widths, col,
            width_divisor > 1.0f ? col_max_w / width_divisor : col_max_w);
    }
}

static float table_sum_cell_span_columns(ViewTableCell* tcell, float* col_widths, int columns) {
    if (!tcell || !tcell->td || !col_widths) return 0.0f;
    return table_sum_span_columns(col_widths, tcell->td->col_index, tcell->td->col_span, columns);
}

static float calculate_cell_width_from_columns(ViewTableCell* tcell, float* col_widths, int columns) {
    return table_sum_cell_span_columns(tcell, col_widths, columns);
}

static float table_cell_internal_border_spacing(ViewTable* table, ViewTableCell* tcell) {
    if (!table || !table->tb || !tcell || !tcell->td) return 0.0f;
    if (table->tb->border_collapse || table->tb->border_spacing_h <= 0.0f) return 0.0f;
    if (tcell->td->col_span <= 1) return 0.0f;
    return table->tb->border_spacing_h * (tcell->td->col_span - 1);
}

static float table_column_visual_x(ViewTable* table, float* col_widths, float* col_x_positions,
                                   int start_col, int span, int columns);

typedef struct ColspanWidthContribution {
    ViewTableCell* cell;
    int col;
    int span;
    int order;
    float min_width;
    float pref_width;
    float cell_width;
} ColspanWidthContribution;

static int compare_colspan_width_contributions(ArrayListValue left, ArrayListValue right) {
    ColspanWidthContribution* a = (ColspanWidthContribution*)left;
    ColspanWidthContribution* b = (ColspanWidthContribution*)right;
    if (!a || !b) return 0;
    if (a->span != b->span) return a->span - b->span;
    return a->order - b->order;
}

static void apply_colspan_width_contribution(ViewTable* table, TableMetadata* meta,
                                             ColspanWidthContribution* contribution) {
    if (!table || !meta || !contribution || !contribution->cell) return;

    int columns = meta->column_count;
    int col = contribution->col;
    int span = contribution->span;
    if (span <= 1) return;

    float current_col_total = 0.0f;
    float current_min_total = 0.0f;
    float current_max_total = 0.0f;
    int actual_span = 0;
    for_each_table_span_column(col, span, columns, [&](int c) {
        current_col_total += meta->col_widths[c];
        current_min_total += meta->col_min_widths[c];
        current_max_total += meta->col_max_widths[c];
        actual_span++;
    });
    if (actual_span <= 0) return;

    float internal_spacing = table_cell_internal_border_spacing(table, contribution->cell);
    if (actual_span < span && internal_spacing > 0.0f) {
        internal_spacing = table->tb->border_spacing_h * (actual_span - 1);
    }

    // CSS 2.1 §17.5.2.2: After single-column cells establish the column
    // floors, spanning cells widen the covered columns proportionally.
    float current_min_span_width = current_min_total + internal_spacing;
    if (contribution->min_width > current_min_span_width) {
        float extra_needed = contribution->min_width - current_min_span_width;
        if (current_min_total > 0.0f) {
            for_each_table_span_column(col, span, columns, [&](int c) {
                float proportion = meta->col_min_widths[c] / current_min_total;
                meta->col_min_widths[c] += extra_needed * proportion;
            });
        } else {
            float extra_per_col = extra_needed / actual_span;
            for_each_table_span_column(col, span, columns, [&](int c) {
                meta->col_min_widths[c] += extra_per_col;
            });
        }
    }

    float current_max_span_width = current_max_total + internal_spacing;
    if (contribution->pref_width > current_max_span_width) {
        float extra_needed = contribution->pref_width - current_max_span_width;
        if (current_max_total > 0.0f) {
            for_each_table_span_column(col, span, columns, [&](int c) {
                float proportion = meta->col_max_widths[c] / current_max_total;
                meta->col_max_widths[c] += extra_needed * proportion;
            });
        } else {
            float extra_per_col = extra_needed / actual_span;
            for_each_table_span_column(col, span, columns, [&](int c) {
                meta->col_max_widths[c] += extra_per_col;
            });
        }
    }

    float current_col_span_width = current_col_total + internal_spacing;
    if (contribution->cell_width > current_col_span_width) {
        float extra_needed = contribution->cell_width - current_col_span_width;
        if (current_col_total > 0.0f) {
            for_each_table_span_column(col, span, columns, [&](int c) {
                float proportion = meta->col_widths[c] / current_col_total;
                meta->col_widths[c] += extra_needed * proportion;
            });
        } else {
            float extra_per_col = extra_needed / actual_span;
            for_each_table_span_column(col, span, columns, [&](int c) {
                meta->col_widths[c] += extra_per_col;
            });
        }
    }
}

// Process a single cell: position, size, layout content, apply alignment
// Returns the height contribution for the current row (adjusted for rowspan)
// col_edge_max_border: max resolved border at each column edge (size columns+1), or nullptr
// col_collapsed: per-column visibility:collapse flags, or nullptr
// col_original_widths: pre-collapse column widths for correct height computation, or nullptr
static float process_table_cell(LayoutContext* lycon, ViewTableCell* tcell, ViewTable* table,
                               float* col_widths, float* col_x_positions, int columns,
                               float* col_edge_max_border = nullptr,
                               bool* col_collapsed = nullptr,
                               float* col_original_widths = nullptr) {
    ViewBlock* cell = lam::view_require_block(tcell);

    // CSS 2.1 §17.5.5: Detect if ALL columns this cell spans are collapsed.
    // If so, we lay out at the original (pre-collapse) width for correct height,
    // then zero the rendered width. Row heights are NOT recalculated.
    bool cell_is_collapsed = false;
    if (col_collapsed) {
        bool all_collapsed = true;
        int end_col = tcell->td->col_index + tcell->td->col_span;
        for (int c = tcell->td->col_index; c < end_col && c < columns; c++) {
            if (!col_collapsed[c]) { all_collapsed = false; break; }
        }
        cell_is_collapsed = all_collapsed;
    }

    // Check if this empty cell should have its border/background hidden
    // CSS 2.1 Section 17.6.1.1: In separated borders model, empty cells can have
    // their borders and backgrounds hidden based on empty-cells property.
    // empty-cells is inherited, so check cell's own cascade first, then table.
    {
        bool empty_cells_hide = (table->tb->empty_cells == TableProp::EMPTY_CELLS_HIDE);
        DomElement* cell_dom = lam::dom_require<DOM_NODE_ELEMENT>(tcell);
        if (cell_dom->specified_style) {
            CssDeclaration* ec_decl = style_tree_get_declaration(
                cell_dom->specified_style, CSS_PROPERTY_EMPTY_CELLS);
            if (ec_decl && ec_decl->value && ec_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                empty_cells_hide = (ec_decl->value->data.keyword == CSS_VALUE_HIDE);
            }
        }
        if (tcell->td->is_empty && !table->tb->border_collapse && empty_cells_hide) {
            tcell->td->hide_empty = 1;
            log_debug("%s Cell at col=%d row=%d: hide_empty=1 (empty + empty-cells:hide)", tcell->source_loc(),
                tcell->td->col_index, tcell->td->row_index);
        } else {
            tcell->td->hide_empty = 0;
        }
    }

    // Position cell relative to row
    float cell_abs_x = table_column_visual_x(table, col_widths, col_x_positions,
                                             tcell->td->col_index, tcell->td->col_span, columns);
    cell->x = cell_abs_x - col_x_positions[0];
    cell->y = 0;

    // Position text children within cell
    position_cell_text_children(tcell);

    // Calculate cell width from columns (for colspan support)
    float cell_width = 0.0f;
    if (cell_is_collapsed && col_original_widths) {
        // CSS 2.1 §17.5.5: Use original (pre-collapse) width for content layout
        // so that row heights are computed correctly ("not recalculated")
        cell_width = table_sum_cell_span_columns(tcell, col_original_widths, columns);
        log_debug("%s Collapsed cell col=%d: using original width %.1f for layout", tcell->source_loc(),
                 tcell->td->col_index, cell_width);
    } else {
        cell_width = table_sum_cell_span_columns(tcell, col_widths, columns);
    }

    // CSS 2.1 §17.6.2: In border-collapse mode, col_widths already include
    // per-cell border halves (added during column width measurement).
    // No additional border adjustment needed here.
    if (table->tb->border_collapse) {
        log_debug("%s Border-collapse cell width: col=%d, cell_width=%.1f (includes border halves from col_widths)", tcell->source_loc(),
                tcell->td->col_index, cell_width);
    } else {
        cell_width += table_cell_internal_border_spacing(table, tcell);
    }

    cell->width = cell_width;

    // Layout cell content now that width is set
    layout_table_cell_content(lycon, cell, table);

    float explicit_cell_height = get_explicit_css_height(lycon, cell);

    float content_height = measure_cell_content_height(lycon, tcell);

    // Calculate final cell height
    float cell_height_val = calculate_cell_height(lycon, tcell, table, content_height, explicit_cell_height);

    cell->height = cell_height_val;

    // CSS 2.1 §17.5.5: After computing height at original width,
    // zero the rendered dimensions for collapsed cells.
    // The height still contributes to row height calculation via height_for_row.
    if (cell_is_collapsed) {
        cell->width = 0;
        cell->height = 0;
        log_debug("%s Collapsed cell col=%d row=%d: zeroed to 0x0 (height_for_row=%.1f preserved)", tcell->source_loc(),
                 tcell->td->col_index, tcell->td->row_index, cell_height_val);
    }

    // Apply vertical alignment
    apply_cell_vertical_align(lycon, tcell, cell_height_val, content_height);

    // Handle rowspan for row height calculation.
    // Single-row cells establish each row's natural height. Rowspanning cells
    // are reconciled later by distribute_rowspan_heights(), which compares the
    // cell's required height with the complete set of rows it spans.
    float height_for_row = cell_height_val;
    if (tcell->td->row_span > 1) {
        height_for_row = 0.0f;
        log_debug("%s Rowspan cell - total_height=%.1f, rowspan=%d (deferred to distribution pass)", tcell->source_loc(),
                  cell_height_val, tcell->td->row_span);
    }

    return height_for_row;
}

// Apply fixed row height to row and all its cells
// Forward declaration
static float measure_cell_content_height(LayoutContext* lycon, ViewTableCell* tcell);
static void apply_cell_vertical_align(LayoutContext* lycon, ViewTableCell* tcell, float cell_height, float content_height);

static void update_row_cells_after_height_change(LayoutContext* lycon, ViewTableRow* trow,
                                                 float row_height, bool only_single_rowspan,
                                                 bool grow_only) {
    if (!trow) return;
    for_each_table_row_cell(trow, [&](ViewTableCell* cell) {
        if (only_single_rowspan && (!cell->td || cell->td->row_span != 1)) return;
        if (grow_only && cell->height >= row_height) return;
        cell->height = row_height;
        float content_height = measure_cell_content_height(lycon, cell);
        apply_cell_vertical_align(lycon, cell, cell->height, content_height);
    });
}

static void apply_fixed_row_height(LayoutContext* lycon, ViewTableRow* trow, float fixed_height) {
    trow->height = fixed_height;
    log_debug("%s Applied fixed layout row height: %.1fpx", trow->source_loc(), fixed_height);
    update_row_cells_after_height_change(lycon, trow, fixed_height, false, true);
}

static bool table_uses_rtl_column_order(ViewTable* table) {
    return table && table->blk && table->blk->direction == CSS_VALUE_RTL;
}

static float table_inter_column_spacing(ViewTable* table) {
    if (!table || !table->tb || table->tb->border_collapse) return 0.0f;
    return table->tb->border_spacing_h > 0.0f ? table->tb->border_spacing_h : 0.0f;
}

static float table_column_span_width(ViewTable* table, float* col_widths,
                                     int start_col, int span, int columns) {
    if (!col_widths || columns <= 0 || span <= 0) return 0.0f;
    if (start_col < 0) {
        span += start_col;
        start_col = 0;
    }
    if (start_col >= columns || span <= 0) return 0.0f;

    int end_col = start_col + span;
    if (end_col > columns) end_col = columns;
    int actual_span = end_col - start_col;
    if (actual_span <= 0) return 0.0f;

    float width = 0.0f;
    for (int c = start_col; c < end_col; c++) {
        width += col_widths[c];
    }

    float spacing = table_inter_column_spacing(table);
    if (spacing > 0.0f && actual_span > 1) {
        width += spacing * (actual_span - 1);
    }
    return width;
}

static float table_column_visual_x(ViewTable* table, float* col_widths, float* col_x_positions,
                                   int start_col, int span, int columns) {
    if (!col_x_positions || columns <= 0) return 0.0f;
    if (start_col < 0) start_col = 0;
    if (start_col >= columns) start_col = columns - 1;
    if (span <= 0) span = 1;

    if (!table_uses_rtl_column_order(table)) {
        return col_x_positions[start_col];
    }

    int end_col = start_col + span;
    if (end_col > columns) end_col = columns;

    float grid_left = col_x_positions[0];
    float total_width = table_column_span_width(table, col_widths, 0, columns, columns);
    float leading_source_width = table_column_span_width(table, col_widths, 0, end_col, columns);
    return grid_left + total_width - leading_source_width;
}

// =============================================================================
// COLUMN/COLGROUP LAYOUT (CSS 2.1 Section 17.5.1)
// =============================================================================
// CSS 2.1 §17.5.1: Column groups and columns do not generate cells.
// They exist as part of the table layer structure for:
// - Applying backgrounds (lowest layer, behind cells)
// - Applying borders (in collapsed borders mode)
// - Applying width constraints
//
// Per CSS 2.1 §17.5.1, table layers from bottom to top are:
// table → column groups → columns → row groups → rows → cells
//
// This function sets column/colgroup dimensions after table layout is complete.

/**
 * Layout column and column group elements.
 * Called after table layout to set their dimensions based on final column widths.
 *
 * @param table The table view
 * @param col_widths Array of final column widths
 * @param col_x_positions Array of column X positions
 * @param columns Number of columns
 * @param table_height Height of the table content area (excluding caption)
 */
static void layout_column_elements(ViewTable* table, float* col_widths, float* col_x_positions,
                                   int columns, float table_height, float content_y_offset) {
    if (!table || columns <= 0) return;

    log_debug("%s [COLUMN-LAYOUT] Setting column/colgroup dimensions for %d columns, table_height=%.1f", table->source_loc(),
              columns, table_height);

    // Track current column index for iterating through columns within colgroups
    int current_col = 0;

    // Iterate through table children to find column groups and columns
    for_each_table_column_source(table, [&](ViewElement* child) {
        if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
            // Column group: spans multiple columns
            // Find the first and last column indices this group covers
            int first_col = current_col;
            int last_col = current_col;

            // Count columns in this group by iterating its children.
            // A <col> box covers its declared span in static table geometry.
            int col_count = 0;
            for_each_table_colgroup_column(child, [&](ViewElement* col) {
                col_count += table_positive_span_attr(col);
            });

            // If no col children, colgroup with span attribute would handle columns
            // For now, assume each colgroup without children represents 1 column
            if (col_count == 0) {
                col_count = table_positive_span_attr(child);
            }

            last_col = first_col + col_count - 1;
            if (last_col >= columns) last_col = columns - 1;

            // Calculate colgroup dimensions
            // CSS 2.1 §17.2.1: Column groups span the table content area.
            // col_x_positions[] are absolute from the table border-box origin
            // (they include border + padding + border-spacing offsets).
            if (first_col < columns) {
                float x = table_column_visual_x(table, col_widths, col_x_positions,
                                                first_col, col_count, columns);
                float width = table_column_span_width(table, col_widths,
                                                      first_col, col_count, columns);

                child->x = x;
                child->y = content_y_offset;
                child->width = width;
                // CSS 2.1 §17.5.1: Column groups with zero width have zero height
                // (no visible column = no visible box in getBoundingClientRect)
                child->height = (width > 0) ? table_height : 0;

                log_debug("%s [COLUMN-LAYOUT] Colgroup: cols %d-%d, x=%.1f, y=0, width=%.1f, height=%.1f", table->source_loc(),
                          first_col, last_col, x, width, child->height);
            }

            // Now set dimensions for child column elements
            // Column x is relative to parent colgroup, not to table
            float colgroup_x = child->x;  // Colgroup's x relative to table
            int col_idx = first_col;
            for_each_table_colgroup_column(child, [&](ViewElement* col) {
                if (col_idx < columns) {
                    int col_span = table_positive_span_attr(col);
                    int col_end = col_idx + col_span - 1;
                    if (col_end >= columns) col_end = columns - 1;

                    float col_x_in_table = table_column_visual_x(table, col_widths, col_x_positions,
                                                                 col_idx, col_span, columns);
                    // Column x relative to parent colgroup
                    float col_x = col_x_in_table - colgroup_x;
                    float col_width = table_column_span_width(table, col_widths,
                                                              col_idx, col_span, columns);

                    col->x = col_x;
                    col->y = 0;
                    col->width = col_width;
                    // CSS 2.1 §17.5.1: Columns with zero width have zero height
                    col->height = (col_width > 0) ? table_height : 0;

                    log_debug("%s [COLUMN-LAYOUT] Column %d-%d: x=%.1f (in table: %.1f), y=0, width=%.1f, height=%.1f", table->source_loc(),
                              col_idx, col_end, col_x, col_x_in_table, col_width, col->height);
                    col_idx = col_end + 1;
                }
            });

            current_col = last_col + 1;
        }
        else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
            // Standalone column (not in a colgroup)
            if (current_col < columns) {
                int span = table_positive_span_attr(child);
                int col_end = current_col + span - 1;
                if (col_end >= columns) col_end = columns - 1;

                float col_x = table_column_visual_x(table, col_widths, col_x_positions,
                                                    current_col, span, columns);
                float col_width = table_column_span_width(table, col_widths,
                                                          current_col, span, columns);

                child->x = col_x;
                child->y = content_y_offset;
                child->width = col_width;
                // CSS 2.1 §17.5.1: Columns with zero width have zero height
                child->height = (col_width > 0) ? table_height : 0;

                log_debug("%s [COLUMN-LAYOUT] Standalone column %d-%d: x=%.1f, y=0, width=%.1f, height=%.1f", table->source_loc(),
                          current_col, col_end, col_x, col_width, child->height);
                current_col = col_end + 1;
            }
        }
    });
}

// =============================================================================
// BORDER-COLLAPSE ALGORITHM (CSS 2.1 Section 17.6.2)
// =============================================================================
// Implements CSS 2.1 border conflict resolution for collapsed borders model
// Priority rules (§17.6.2.1):
//   1. border-style: hidden wins over all
//   2. border-style: none has lowest priority
//   3. Wider border wins
//   4. Style priority: double > solid > dashed > dotted > ridge > outset > groove > inset
//   5. Top/left wins over bottom/right (arbitrary tie-breaker)

// CollapsedBorder struct is now defined in view.hpp

// Get border style priority for conflict resolution
static uint8_t get_border_style_priority(CssEnum style) {
    switch (style) {
        case CSS_VALUE_HIDDEN:  return 255; // Always wins
        case CSS_VALUE_NONE:    return 0;   // Always loses
        case CSS_VALUE_DOUBLE:  return 8;
        case CSS_VALUE_SOLID:   return 7;
        case CSS_VALUE_DASHED:  return 6;
        case CSS_VALUE_DOTTED:  return 5;
        case CSS_VALUE_RIDGE:   return 4;
        case CSS_VALUE_OUTSET:  return 3;
        case CSS_VALUE_GROOVE:  return 2;
        case CSS_VALUE_INSET:   return 1;
        default:                return 0;
    }
}

// Select winner between two borders according to CSS 2.1 rules
static CollapsedBorder select_winning_border(const CollapsedBorder& a, const CollapsedBorder& b) {
    // Rule 1: hidden wins — used width is 0 per CSS 2.1 §17.6.2.1
    if (a.style == CSS_VALUE_HIDDEN) {
        CollapsedBorder result = a;
        result.width = 0;
        return result;
    }
    if (b.style == CSS_VALUE_HIDDEN) {
        CollapsedBorder result = b;
        result.width = 0;
        return result;
    }

    // Rule 2: none loses (skip if both none)
    if (a.style == CSS_VALUE_NONE && b.style == CSS_VALUE_NONE) return a;
    if (a.style == CSS_VALUE_NONE) return b;
    if (b.style == CSS_VALUE_NONE) return a;

    // Rule 3: wider wins
    if (a.width > b.width) return a;
    if (b.width > a.width) return b;

    // Rule 4: style priority
    uint8_t a_pri = get_border_style_priority(a.style);
    uint8_t b_pri = get_border_style_priority(b.style);
    if (a_pri > b_pri) return a;
    if (b_pri > a_pri) return b;

    // Rule 5: source priority (a is top/left, wins on tie)
    return a;
}

static CollapsedBorder get_boundary_border(const BoundaryProp* bound, int side) {
    CollapsedBorder border;
    if (!bound || !bound->border) return border;

    const BorderProp* bp = bound->border;
    switch (side) {
        case 0: // top
            border.width = bp->width.top;
            border.style = bp->top_style;
            border.color = bp->top_color;
            break;
        case 1: // right
            border.width = bp->width.right;
            border.style = bp->right_style;
            border.color = bp->right_color;
            break;
        case 2: // bottom
            border.width = bp->width.bottom;
            border.style = bp->bottom_style;
            border.color = bp->bottom_color;
            break;
        case 3: // left
            border.width = bp->width.left;
            border.style = bp->left_style;
            border.color = bp->left_color;
            break;
    }
    border.priority = get_border_style_priority(border.style);
    return border;
}

// Extract border info from a cell's BoundaryProp
static CollapsedBorder get_cell_border(ViewTableCell* cell, int side) {
    return get_boundary_border(cell ? cell->bound : NULL, side);
}

// Apply collapsed border to cell (stores in TableCellProp for rendering)
// CSS 2.1 §17.6.2: Border resolution is for RENDERING, not layout
// This stores resolved borders in TableCellProp->*_resolved fields
// Layout calculations continue to use original BorderProp widths
static void apply_collapsed_border_to_cell(LayoutContext* lycon, ViewTableCell* cell,
                                           const CollapsedBorder& border, int side) {
    if (!cell || !cell->td) return;

    // Allocate resolved border storage if needed
    CollapsedBorder** target = nullptr;
    switch (side) {
        case 0: target = &cell->td->top_resolved; break;
        case 1: target = &cell->td->right_resolved; break;
        case 2: target = &cell->td->bottom_resolved; break;
        case 3: target = &cell->td->left_resolved; break;
    }

    if (!target) return;

    // Allocate CollapsedBorder if not already allocated
    if (!*target) {
        *target = (CollapsedBorder*)alloc_prop(lycon, sizeof(CollapsedBorder));
        if (!*target) {
            log_error("Failed to allocate CollapsedBorder");
            return;
        }
    }

    // Store resolved border for rendering phase
    (*target)->width = border.width;
    (*target)->style = border.style;
    (*target)->color = border.color;
    (*target)->priority = border.priority;
}

// Get table border (outer edge of table element)
static CollapsedBorder get_table_border(ViewTable* table, int side) {
    return get_boundary_border(table ? table->bound : NULL, side);
}

// Get row border (for row elements with borders)
static CollapsedBorder get_row_border(ViewTableRow* row, int side) {
    return get_boundary_border(row ? row->bound : NULL, side);
}

// Extract border info from a row-group element (tbody/thead/tfoot)
// CSS 2.1 §17.6.2: row groups participate in border conflict resolution
static CollapsedBorder get_rowgroup_border(ViewBlock* rg, int side) {
    return get_boundary_border(rg ? rg->bound : NULL, side);
}

// Find the row-group block that contains a given row index.
// Also outputs the first and last row indices within that group.
// Returns nullptr if no row-group found (e.g., table acts as tbody).
static ViewBlock* find_rowgroup_for_row(ViewTable* table, int target_row,
                                        int* out_first_row_in_group, int* out_last_row_in_group) {
    int row_idx = 0;
    for (View* child = static_cast<View*>(table->first_child); child; child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            int first_in_group = row_idx;
            ViewTableRowGroup* rg = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            row_idx += table_row_group_row_count(rg);
            int last_in_group = row_idx - 1;
            if (target_row >= first_in_group && target_row <= last_in_group) {
                if (out_first_row_in_group) *out_first_row_in_group = first_in_group;
                if (out_last_row_in_group) *out_last_row_in_group = last_in_group;
                return lam::view_require_block(child);
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            if (row_idx == target_row) {
                // row is direct child of table (no row-group wrapper)
                if (out_first_row_in_group) *out_first_row_in_group = -1;
                if (out_last_row_in_group) *out_last_row_in_group = -1;
                return nullptr;
            }
            row_idx++;
        }
    }
    return nullptr;
}

// Extract border info from a column or colgroup element (ViewBlock)
// CSS 2.1 §17.6.2: columns and column groups participate in border conflict resolution
static CollapsedBorder get_column_border(ViewBlock* col, int side) {
    return get_boundary_border(col ? col->bound : NULL, side);
}

typedef lam::ArrayOwnedList<CollapsedBorder, lam::LayoutSessionDomain> CollapsedBorderList;

static void append_collapsed_border_candidate(CollapsedBorderList& candidates,
                                              const CollapsedBorder& value) {
    lam::SessionPtr<CollapsedBorder> border = lam::session_make<CollapsedBorder>(MEM_CAT_LAYOUT);
    if (!border) {
        log_error("append_collapsed_border_candidate_alloc_failed");
        return;
    }
    *border = value;
    if (!candidates.append(static_cast<lam::SessionPtr<CollapsedBorder>&&>(border))) {
        log_error("append_collapsed_border_candidate_append_failed: count=%zu", candidates.size());
    }
}

static void append_visible_collapsed_border_candidate(CollapsedBorderList& candidates,
                                                      const CollapsedBorder& value) {
    if (value.style != CSS_VALUE_NONE) {
        append_collapsed_border_candidate(candidates, value);
    }
}

// Find column element at a given column index
// Returns the ViewBlock for the <col> element, or NULL if not found
static ViewBlock* find_column_element(ViewTable* table, int target_col) {
    int current_col = 0;
    ViewBlock* found_col = nullptr;
    for_each_table_column_source(table, [&](ViewElement* child) {
        if (found_col) return;
        if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
            for_each_table_colgroup_column(child, [&](ViewElement* col) {
                if (found_col) return;
                if (current_col == target_col) found_col = lam::view_require_block(col);
                current_col++;
            });
            // colgroup without col children
            if (!found_col && current_col <= target_col) {
                current_col += table_positive_span_attr(child);
            }
        } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
            if (current_col == target_col) found_col = lam::view_require_block(child);
            current_col++;
        }
    });
    return found_col;
}

// Find colgroup element that contains a given column index
// Returns the ViewBlock for the <colgroup> element, or NULL if not found
static ViewBlock* find_colgroup_element(ViewTable* table, int target_col) {
    int current_col = 0;
    ViewBlock* found_colgroup = nullptr;
    for_each_table_column_source(table, [&](ViewElement* child) {
        if (found_colgroup) return;
        if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
            int first_col = current_col;
            int col_count = 0;
            for_each_table_colgroup_column(child, [&](ViewElement* col) {
                (void)col;
                col_count++;
            });
            if (col_count == 0) {
                col_count = table_positive_span_attr(child);
            }
            int last_col = first_col + col_count - 1;
            if (target_col >= first_col && target_col <= last_col) {
                found_colgroup = lam::view_require_block(child);
            }
            current_col = last_col + 1;
        } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
            current_col++;
        }
    });
    return found_colgroup;
}

static ViewTableRow* table_row_at_index(ViewTable* table, int target_row) {
    if (!table || target_row < 0) return nullptr;
    int row_index = 0;
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        if (row_index == target_row) return row;
        row_index++;
    }
    return nullptr;
}

// Find cell at specific grid position (handles rowspan/colspan)
static ViewTableCell* find_cell_at(ViewTable* table, int target_row, int target_col) {
    return find_table_cell(table, [&](ViewTableRow* row, ViewTableCell* cell) {
        (void)row;
        int row_start = cell->td->row_index;
        int row_end = row_start + cell->td->row_span;
        int col_start = cell->td->col_index;
        int col_end = col_start + cell->td->col_span;

        return target_row >= row_start && target_row < row_end &&
            target_col >= col_start && target_col < col_end;
    });
}

static void apply_collapsed_border_pair(LayoutContext* lycon, ViewTable* table,
                                        TableMetadata* meta, CollapsedBorderList& candidates,
                                        int row, int col, bool horizontal) {
    if (candidates.size() == 0) return;

    CollapsedBorder winner = *candidates[0];
    for (size_t i = 1; i < candidates.size(); i++) {
        winner = select_winning_border(winner, *candidates[i]);
    }

    if (horizontal ? row > 0 : col > 0) {
        ViewTableCell* previous = find_cell_at(
            table, horizontal ? row - 1 : row, horizontal ? col : col - 1);
        if (previous) {
            apply_collapsed_border_to_cell(lycon, previous, winner, horizontal ? 2 : 1);
        }
    }
    if (horizontal ? row < meta->row_count : col < meta->column_count) {
        ViewTableCell* next = find_cell_at(table, row, col);
        if (next) {
            apply_collapsed_border_to_cell(lycon, next, winner, horizontal ? 0 : 3);
        }
    }
}

static bool is_out_of_flow_table_cell_slot(View* view) {
    ViewElement* elem = lam::view_as_element(view);
    if (!elem || elem->view_type == RDT_VIEW_TABLE_CELL) return false;

    uintptr_t tag = elem->tag();
    if (tag != HTM_TAG_TD && tag != HTM_TAG_TH) return false;

    return layout_view_is_out_of_flow_positioned(view);
}

template <typename Fn>
static void for_each_table_row_cell_slot(ViewTableRow* row, Fn fn) {
    if (!row) return;
    for (View* child = static_cast<View*>(row->first_child); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_CELL || is_out_of_flow_table_cell_slot(child)) {
            fn(child);
        }
    }
}

// Resolve collapsed borders for all cells in table
// This implements CSS 2.1 Section 17.6.2 border conflict resolution
// CSS 2.1 §17.6.2: Each border around a cell can be specified by various elements
// (cell, row, row group, column, column group, table), and these must be resolved
static void resolve_collapsed_borders(LayoutContext* lycon, ViewTable* table, TableMetadata* meta) {
    if (!table || !meta || !table->tb->border_collapse) return;

    log_debug("resolve_collapsed_borders: starting comprehensive border resolution for %dx%d table",
              meta->column_count, meta->row_count);

    // Pass 1: Resolve all horizontal borders (between rows)
    // For each horizontal border position (between row i and i+1, including top/bottom edges)
    for (int row = 0; row <= meta->row_count; row++) {
        for (int col = 0; col < meta->column_count; col++) {
            // Collect candidate borders for this horizontal edge
            CollapsedBorderList candidates(MEM_CAT_LAYOUT, 4);

            // Border from cell above (bottom border)
            if (row > 0) {
                ViewTableCell* cell_above = find_cell_at(table, row - 1, col);
                if (cell_above) {
                    append_collapsed_border_candidate(candidates, get_cell_border(cell_above, 2)); // bottom
                }
            } else {
                // Top edge of table
                append_collapsed_border_candidate(candidates, get_table_border(table, 0)); // top
            }

            // Border from cell below (top border)
            if (row < meta->row_count) {
                ViewTableCell* cell_below = find_cell_at(table, row, col);
                if (cell_below) {
                    append_collapsed_border_candidate(candidates, get_cell_border(cell_below, 0)); // top
                }
            } else {
                // Bottom edge of table
                append_collapsed_border_candidate(candidates, get_table_border(table, 2)); // bottom
            }

            // Row borders (if row elements have borders)
            // Check row above (bottom border) - for interior and top edge
            if (row > 0) {
                ViewTableRow* row_above = table_row_at_index(table, row - 1);
                if (row_above) {
                    append_visible_collapsed_border_candidate(candidates, get_row_border(row_above, 2)); // bottom of row above
                }
            }

            // Check row below (top border) - for interior and bottom edge
            if (row < meta->row_count) {
                ViewTableRow* row_below = table_row_at_index(table, row);
                if (row_below) {
                    append_visible_collapsed_border_candidate(candidates, get_row_border(row_below, 0)); // top of row below
                }
            }

            // Row-group borders (CSS 2.1 §17.6.2: row groups participate in conflict resolution)
            // Row-group top border at the top edge of its first row
            if (row < meta->row_count) {
                int first_in_group = -1, last_in_group = -1;
                ViewBlock* rg = find_rowgroup_for_row(table, row, &first_in_group, &last_in_group);
                if (rg && row == first_in_group) {
                    CollapsedBorder cb = get_rowgroup_border(rg, 0); // top
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
            }
            // Row-group bottom border at the bottom edge of its last row
            if (row > 0) {
                int first_in_group = -1, last_in_group = -1;
                ViewBlock* rg = find_rowgroup_for_row(table, row - 1, &first_in_group, &last_in_group);
                if (rg && (row - 1) == last_in_group) {
                    CollapsedBorder cb = get_rowgroup_border(rg, 2); // bottom
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
            }

            // Column/colgroup top/bottom borders at table edges (CSS 2.1 §17.6.2)
            if (row == 0 || row == meta->row_count) {
                ViewBlock* col_elem = find_column_element(table, col);
                if (col_elem) {
                    int side = (row == 0) ? 0 : 2; // top or bottom
                    CollapsedBorder cb = get_column_border(col_elem, side);
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
                ViewBlock* cg = find_colgroup_element(table, col);
                if (cg) {
                    int side = (row == 0) ? 0 : 2;
                    CollapsedBorder cb = get_column_border(cg, side);
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
            }

            apply_collapsed_border_pair(lycon, table, meta, candidates, row, col, true);

        }
    }

    // Pass 2: Resolve all vertical borders (between columns)
    // For each vertical border position (between col i and i+1, including left/right edges)
    for (int row = 0; row < meta->row_count; row++) {
        for (int col = 0; col <= meta->column_count; col++) {
            // Collect candidate borders for this vertical edge
            CollapsedBorderList candidates(MEM_CAT_LAYOUT, 4);

            // Border from cell to left (right border)
            if (col > 0) {
                ViewTableCell* cell_left = find_cell_at(table, row, col - 1);
                if (cell_left) {
                    append_collapsed_border_candidate(candidates, get_cell_border(cell_left, 1)); // right
                }
            } else {
                // Left edge of table
                append_collapsed_border_candidate(candidates, get_table_border(table, 3)); // left
            }

            // Border from cell to right (left border)
            if (col < meta->column_count) {
                ViewTableCell* cell_right = find_cell_at(table, row, col);
                if (cell_right) {
                    append_collapsed_border_candidate(candidates, get_cell_border(cell_right, 3)); // left
                }
            } else {
                // Right edge of table
                append_collapsed_border_candidate(candidates, get_table_border(table, 1)); // right
            }

            // Row borders at left and right edges (CSS 2.1 §17.6.2: rows can have borders)
            if (col == 0 || col == meta->column_count) {
                ViewTableRow* edge_row = table_row_at_index(table, row);
                if (edge_row) {
                    int side = (col == 0) ? 3 : 1; // left or right
                    append_visible_collapsed_border_candidate(candidates, get_row_border(edge_row, side));
                }
            }

            // Row-group borders at left and right edges (CSS 2.1 §17.6.2)
            if (col == 0 || col == meta->column_count) {
                int first_in_group = -1, last_in_group = -1;
                ViewBlock* rg = find_rowgroup_for_row(table, row, &first_in_group, &last_in_group);
                if (rg) {
                    int side = (col == 0) ? 3 : 1; // left or right
                    CollapsedBorder cb = get_rowgroup_border(rg, side);
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
            }

            // Column borders (CSS 2.1 §17.6.2: columns participate in border conflict resolution)
            // Column to the left of this vertical edge
            if (col > 0) {
                ViewBlock* col_elem = find_column_element(table, col - 1);
                if (col_elem) {
                    CollapsedBorder cb = get_column_border(col_elem, 1); // right border
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
            }
            // Column to the right of this vertical edge
            if (col < meta->column_count) {
                ViewBlock* col_elem = find_column_element(table, col);
                if (col_elem) {
                    CollapsedBorder cb = get_column_border(col_elem, 3); // left border
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
            }

            // Colgroup borders at column group edges (CSS 2.1 §17.6.2)
            {
                ViewBlock* cg_left = col > 0 ? find_colgroup_element(table, col - 1) : nullptr;
                ViewBlock* cg_right = col < meta->column_count ? find_colgroup_element(table, col) : nullptr;
                // Add right border of left colgroup if column is at the right edge of that group
                if (cg_left && cg_left != cg_right) {
                    CollapsedBorder cb = get_column_border(cg_left, 1); // right
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
                // Add left border of right colgroup if column is at the left edge of that group
                if (cg_right && cg_right != cg_left) {
                    CollapsedBorder cb = get_column_border(cg_right, 3); // left
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
                // At table edges, add the colgroup border at that edge
                if (col == 0 && cg_right) {
                    CollapsedBorder cb = get_column_border(cg_right, 3); // left edge
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
                if (col == meta->column_count && cg_left) {
                    CollapsedBorder cb = get_column_border(cg_left, 1); // right edge
                    if (cb.style != CSS_VALUE_NONE) {
                        append_collapsed_border_candidate(candidates, cb);
                    }
                }
            }

            apply_collapsed_border_pair(lycon, table, meta, candidates, row, col, false);

        }
    }

    log_debug("resolve_collapsed_borders: completed - processed %d horizontal and %d vertical borders",
              (meta->row_count + 1) * meta->column_count,
              meta->row_count * (meta->column_count + 1));

    // After resolving all borders, calculate max winning borders at table edges
    // CSS 2.1 §17.6.2: The table's border-box includes half of the outer collapsed borders.
    // We need to find the maximum resolved border width at each edge.

    // Top edge: scan all cells in first row
    for (int col = 0; col < meta->column_count; col++) {
        ViewTableCell* cell = find_cell_at(table, 0, col);
        if (cell && cell->td->top_resolved) {
            if (cell->td->top_resolved->width > meta->collapsed_border_top) {
                meta->collapsed_border_top = cell->td->top_resolved->width;
            }
        }
    }

    // Bottom edge: scan all cells in last row
    int last_row = meta->row_count - 1;
    for (int col = 0; col < meta->column_count; col++) {
        ViewTableCell* cell = find_cell_at(table, last_row, col);
        if (cell && cell->td->bottom_resolved) {
            if (cell->td->bottom_resolved->width > meta->collapsed_border_bottom) {
                meta->collapsed_border_bottom = cell->td->bottom_resolved->width;
            }
        }
    }

    // Left edge: scan all cells in first column
    for (int row = 0; row < meta->row_count; row++) {
        ViewTableCell* cell = find_cell_at(table, row, 0);
        if (cell && cell->td->left_resolved) {
            if (cell->td->left_resolved->width > meta->collapsed_border_left) {
                meta->collapsed_border_left = cell->td->left_resolved->width;
            }
        }
    }

    // Right edge: scan all cells in last column
    int last_col = meta->column_count - 1;
    for (int row = 0; row < meta->row_count; row++) {
        ViewTableCell* cell = find_cell_at(table, row, last_col);
        if (cell && cell->td->right_resolved) {
            if (cell->td->right_resolved->width > meta->collapsed_border_right) {
                meta->collapsed_border_right = cell->td->right_resolved->width;
            }
        }
    }

    log_debug("Max collapsed borders at edges: top=%.1f, right=%.1f, bottom=%.1f, left=%.1f",
              meta->collapsed_border_top, meta->collapsed_border_right,
              meta->collapsed_border_bottom, meta->collapsed_border_left);
}

// =============================================================================
// INTERNAL DATA STRUCTURES
// =============================================================================
// INTERNAL DATA STRUCTURES
// =============================================================================

// =============================================================================
// ROWSPAN HEIGHT DISTRIBUTION (CSS 2.1 Section 17.5.3)
// =============================================================================
// Implements proportional distribution of rowspan cell heights across spanned rows
// Algorithm:
//   Pass 1: Single-row cells establish baseline row heights (already done)
//   Pass 2: For each rowspan cell, check if it needs more height than spanned rows
//   Pass 3: Distribute excess height proportionally to row content heights (not equally)

static void distribute_rowspan_heights(ViewTable* table, TableMetadata* meta) {
    log_debug("=== ROWSPAN HEIGHT DISTRIBUTION ===");

    int rows = meta->row_count;

    // Track rowspan cells that need height distribution
    struct RowspanCell {
        ViewTableCell* cell;
        int start_row;
        int end_row;
        float required_height;
    };

    ArrayList* rowspan_cells = arraylist_new(8);

    // Collect all rowspan cells
    for_each_table_row(table, [&](ViewTableRow* row) {
        for_each_table_row_cell(row, [&](ViewTableCell* tcell) {
            if (tcell->td->row_span > 1) {
                int start_row = tcell->td->row_index;
                int end_row = start_row + tcell->td->row_span;
                if (start_row < 0) start_row = 0;     // never index before row_heights[0]
                if (end_row > rows) end_row = rows;    // never index past row_heights[rows-1]

                // Calculate current spanned height. In separate-border mode,
                // border-spacing between the spanned rows is part of the area
                // covered by the rowspanning cell.
                float current_total = 0.0f;
                for (int r = start_row; r < end_row; r++) {
                    current_total += meta->row_heights[r];
                    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0.0f && r < end_row - 1) {
                        current_total += table->tb->border_spacing_v;
                    }
                }

                float required_height = tcell->height;

                if (required_height > current_total) {
                    RowspanCell* rsc = (RowspanCell*)mem_alloc(sizeof(RowspanCell), MEM_CAT_LAYOUT);
                    rsc->cell = tcell;
                    rsc->start_row = start_row;
                    rsc->end_row = end_row;
                    rsc->required_height = required_height;
                    arraylist_append(rowspan_cells, rsc);

                    log_debug("Rowspan cell at row %d spans %d rows: needs %.1fpx, currently %.1fpx",
                             start_row, tcell->td->row_span, required_height, current_total);
                }
            }
        });
    });

    // Distribute excess height for each rowspan cell
    for (int i = 0; i < rowspan_cells->length; i++) {
        RowspanCell* rsc = (RowspanCell*)rowspan_cells->data[i];
        // recompute the spanned height after earlier rowspan cells may have
        // expanded overlapping rows. otherwise sibling rowspans over the same
        // row range each add the full original deficit.
        float current_total = 0.0f;
        for (int r = rsc->start_row; r < rsc->end_row; r++) {
            current_total += meta->row_heights[r];
            if (!table->tb->border_collapse && table->tb->border_spacing_v > 0.0f && r < rsc->end_row - 1) {
                current_total += table->tb->border_spacing_v;
            }
        }

        float excess = rsc->required_height - current_total;
        if (excess <= 0.0f) {
            log_debug("Rowspan cell at row %d spans %d rows: already satisfied %.1fpx >= %.1fpx",
                      rsc->start_row, rsc->end_row - rsc->start_row,
                      current_total, rsc->required_height);
            continue;
        }

        // Calculate total content height of spanned rows for proportional distribution
        float total_content = 0;
        for (int r = rsc->start_row; r < rsc->end_row; r++) {
            total_content += meta->row_heights[r];
        }

        if (total_content > 0) {
            // Proportional distribution based on current row heights
#ifndef NDEBUG
            float distributed = 0;
#endif
            for (int r = rsc->start_row; r < rsc->end_row; r++) {
                float proportion = meta->row_heights[r] / total_content;
                float amount = excess * proportion;
                meta->row_heights[r] += amount;
#ifndef NDEBUG
                distributed += amount;
#endif
                log_debug("  Row %d: height %.1fpx + %.1fpx (%.1f%% of excess) = %.1fpx",
                         r, meta->row_heights[r] - amount, amount, proportion * 100, meta->row_heights[r]);
            }
            log_debug("Distributed %.1fpx across rows %d-%d (total excess: %.1fpx)",
                     distributed, rsc->start_row, rsc->end_row - 1, excess);
        } else {
            // if all spanned rows have zero own height, browsers keep the row
            // where the rowspan starts at zero and place the spanned cell's
            // height into the later grid row. this preserves empty placeholder
            // rows used only to terminate rowspans.
            int target_row = rsc->end_row - 1;
            if (meta->row_collapsed) {
                while (target_row > rsc->start_row && meta->row_collapsed[target_row]) {
                    target_row--;
                }
            }
            meta->row_heights[target_row] += excess;
            log_debug("Distributed %.1fpx to zero-height rowspan target row %d across rows %d-%d",
                     excess, target_row, rsc->start_row, rsc->end_row - 1);
        }
    }

    // Free the arraylist and allocated structs
    for (int i = 0; i < rowspan_cells->length; i++) {
        mem_free(rowspan_cells->data[i]);
    }
    arraylist_free(rowspan_cells);

    log_debug("=== ROWSPAN HEIGHT DISTRIBUTION COMPLETE ===");
}

// =============================================================================
// CSS PROPERTY PARSING
// =============================================================================

static bool table_resolve_border_collapse_value(CssValue* value, bool* border_collapse, bool* keep_inheriting) {
    if (keep_inheriting) *keep_inheriting = false;
    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD) return false;

    CssEnum kw = value->data.keyword;
    if (kw == CSS_VALUE_INHERIT || kw == CSS_VALUE_UNSET) {
        if (keep_inheriting) *keep_inheriting = true;
        return false;
    }

    if (kw == CSS_VALUE_COLLAPSE || kw == CSS_VALUE_COLLAPSE_TABLE) {
        *border_collapse = true;
        return true;
    }
    if (kw == CSS_VALUE_SEPARATE || kw == CSS_VALUE_INITIAL) {
        *border_collapse = false;
        return true;
    }
    return false;
}

static bool table_resolve_caption_side_value(CssValue* value, bool* is_bottom) {
    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD || !is_bottom) return false;
    *is_bottom = value->data.keyword == CSS_VALUE_BOTTOM;
    return true;
}

static bool table_resolve_border_spacing_value(LayoutContext* lycon, CssValue* value,
        float* spacing_h, float* spacing_v, bool* keep_inheriting) {
    if (keep_inheriting) *keep_inheriting = false;
    if (!value) return false;

    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        float resolved = resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING, value);
        *spacing_h = resolved;
        *spacing_v = resolved;
        return true;
    }

    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 1) {
        CssValue* h_value = value->data.list.values[0];
        CssValue* v_value = value->data.list.count >= 2 ? value->data.list.values[1] : h_value;
        if (!h_value) return false;
        *spacing_h = resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING, h_value);
        *spacing_v = v_value ? resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING, v_value) : *spacing_h;
        return true;
    }

    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        float spacing = (float)value->data.number.value;
        *spacing_h = spacing;
        *spacing_v = spacing;
        return true;
    }

    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum kw = value->data.keyword;
        if (kw == CSS_VALUE_INHERIT || kw == CSS_VALUE_UNSET) {
            if (keep_inheriting) *keep_inheriting = true;
            return false;
        }
        if (kw == CSS_VALUE_INITIAL) {
            *spacing_h = 0.0f;
            *spacing_v = 0.0f;
            return true;
        }
    }

    return false;
}

static bool table_inherit_border_collapse(LayoutContext* lycon, DomNode* element, bool* border_collapse) {
    (void)lycon;
    DomNode* ancestor = element ? element->parent : nullptr;
    while (ancestor) {
        if (ancestor->is_element()) {
            DomElement* anc_elem = ancestor->as_element();
            if (anc_elem->specified_style) {
                CssDeclaration* decl = style_tree_get_declaration(
                    anc_elem->specified_style,
                    CSS_PROPERTY_BORDER_COLLAPSE);
                if (decl && decl->value) {
                    bool keep_inheriting = false;
                    if (table_resolve_border_collapse_value((CssValue*)decl->value,
                            border_collapse, &keep_inheriting)) {
                        return true;
                    }
                    if (!keep_inheriting) return false;
                }
            }

            if (anc_elem->item_prop_type == DomElement::ITEM_PROP_TABLE && anc_elem->tb) {
                *border_collapse = anc_elem->tb->border_collapse;
                return true;
            }
        }
        ancestor = ancestor->parent;
    }
    return false;
}

static bool table_inherit_border_spacing(LayoutContext* lycon, DomNode* element,
        float* spacing_h, float* spacing_v) {
    DomNode* ancestor = element ? element->parent : nullptr;
    while (ancestor) {
        if (ancestor->is_element()) {
            DomElement* anc_elem = ancestor->as_element();
            if (anc_elem->specified_style) {
                CssDeclaration* decl = style_tree_get_declaration(
                    anc_elem->specified_style,
                    CSS_PROPERTY_BORDER_SPACING);
                if (decl && decl->value) {
                    bool keep_inheriting = false;
                    if (table_resolve_border_spacing_value(lycon, (CssValue*)decl->value,
                            spacing_h, spacing_v, &keep_inheriting)) {
                        return true;
                    }
                    if (!keep_inheriting) return false;
                }
            }

            if (anc_elem->item_prop_type == DomElement::ITEM_PROP_TABLE && anc_elem->tb) {
                *spacing_h = anc_elem->tb->border_spacing_h;
                *spacing_v = anc_elem->tb->border_spacing_v;
                return true;
            }
        }
        ancestor = ancestor->parent;
    }
    return false;
}

// Parse table-specific CSS properties from DOM element
static void resolve_table_properties(LayoutContext* lycon, DomNode* element, ViewTable* table) {
    // HTML User-Agent default: border-spacing: 2px for HTML table elements
    // CSS 2.1 spec default is 0, but HTML tables have 2px as the UA stylesheet default
    // This is only applied if the element is an actual HTML <table> tag
    if (element->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = element->as_element();
        if (dom_elem->tag() == HTM_TAG_TABLE) {
            // Set HTML UA default (can be overridden by CSS or cellspacing attribute below)
            table->tb->border_spacing_h = 2.0f;
            table->tb->border_spacing_v = 2.0f;

            // Handle HTML cellspacing attribute (e.g., cellspacing="0")
            // This overrides the UA default but can be overridden by CSS border-spacing
            const char* cellspacing_attr = dom_elem->get_attribute("cellspacing");
            if (cellspacing_attr) {
                float spacing = (float)str_to_double_default(cellspacing_attr, strlen(cellspacing_attr), 0.0);
                table->tb->border_spacing_h = spacing;
                table->tb->border_spacing_v = spacing;
                log_debug("[HTML] TABLE cellspacing attribute: %.0fpx", spacing);
            }

            // HTML rules presentational hint: browsers put these tables into the
            // collapsed border model. The CSS border-spacing computed value still
            // reports the UA 2px value, but it is ignored by collapsed layout.
            const char* rules_attr = dom_elem->get_attribute("rules");
            if (rules_attr) {
                size_t rules_len = strlen(rules_attr);
                if (!str_ieq_const(rules_attr, rules_len, "none")) {
                    table->tb->border_collapse = true;
                    log_debug("[HTML] TABLE rules=%s -> border-collapse: collapse", rules_attr);
                }
            }
        }
    }

    // Read CSS border-collapse and border-spacing properties
    // These apply regardless of table-layout mode

    // Handle both Lexbor and Lambda CSS elements for border properties
    if (element->node_type == DOM_NODE_ELEMENT) {
        // Lambda CSS path - read border-collapse and border-spacing
        DomElement* dom_elem = element->as_element();

        if (dom_elem->specified_style) {
            // Read border-collapse property (203)
            CssDeclaration* collapse_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_BORDER_COLLAPSE);

            if (collapse_decl && collapse_decl->value) {
                CssValue* val = (CssValue*)collapse_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    bool keep_inheriting = false;
                    if (table_resolve_border_collapse_value(val, &table->tb->border_collapse, &keep_inheriting)) {
                        log_debug("Table border-collapse: %s", table->tb->border_collapse ? "collapse" : "separate");
                    } else if (keep_inheriting) {
                        if (table_inherit_border_collapse(lycon, element, &table->tb->border_collapse)) {
                            log_debug("Table border-collapse: inherit -> %s (from ancestor)",
                                      table->tb->border_collapse ? "collapse" : "separate");
                        } else {
                            // CSS 2.1 initial value for border-collapse is 'separate'
                            table->tb->border_collapse = false;
                            log_debug("Table border-collapse: inherit -> separate (initial, no ancestor)");
                        }
                    }
                }
            }

            // Read border-spacing property (204)
            CssDeclaration* spacing_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_BORDER_SPACING);

            if (spacing_decl && spacing_decl->value) {
                CssValue* val = (CssValue*)spacing_decl->value;
                bool keep_inheriting = false;
                if (table_resolve_border_spacing_value(lycon, val, &table->tb->border_spacing_h,
                        &table->tb->border_spacing_v, &keep_inheriting)) {
                    log_debug("Table border-spacing: h=%.2fpx v=%.2fpx",
                              table->tb->border_spacing_h, table->tb->border_spacing_v);
                } else if (keep_inheriting) {
                    if (table_inherit_border_spacing(lycon, element, &table->tb->border_spacing_h,
                            &table->tb->border_spacing_v)) {
                        log_debug("Table border-spacing: inherit -> h=%.2fpx v=%.2fpx (from ancestor)",
                                  table->tb->border_spacing_h, table->tb->border_spacing_v);
                    } else {
                        // CSS 2.1 initial value for border-spacing is 0
                        table->tb->border_spacing_h = 0.0f;
                        table->tb->border_spacing_v = 0.0f;
                        log_debug("Table border-spacing: inherit -> 0 (initial, no ancestor value)");
                    }
                }
            } else {
                // CSS 2.1 §17.6.1: border-spacing is an inherited property.
                // When no border-spacing is declared on this element, inherit from
                // ancestors. For real <table> elements, the UA default (2px) is already
                // set above and should not be overridden by implicit inheritance.
                bool is_html_table = (dom_elem->tag() == HTM_TAG_TABLE);
                if (!is_html_table) {
                    if (table_inherit_border_spacing(lycon, element, &table->tb->border_spacing_h,
                            &table->tb->border_spacing_v)) {
                        log_debug("Table border-spacing: implicit inherit -> h=%.2fpx v=%.2fpx (from ancestor)",
                                  table->tb->border_spacing_h, table->tb->border_spacing_v);
                    }
                }
            }

            // Read caption-side property (CSS 2.1 Section 17.4.1)
            CssDeclaration* caption_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_CAPTION_SIDE);

            if (caption_decl && caption_decl->value) {
                bool caption_bottom = false;
                if (table_resolve_caption_side_value(caption_decl->value, &caption_bottom)) {
                    table->tb->caption_side = caption_bottom
                        ? TableProp::CAPTION_SIDE_BOTTOM : TableProp::CAPTION_SIDE_TOP;
                    log_debug("Table caption-side: %s",
                              caption_bottom ? "bottom" : "top");
                }
            }

            // Read empty-cells property (CSS 2.1 Section 17.6.1.1)
            CssDeclaration* empty_cells_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_EMPTY_CELLS);

            if (empty_cells_decl && empty_cells_decl->value) {
                CssValue* val = (CssValue*)empty_cells_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    if (val->data.keyword == CSS_VALUE_HIDE) {
                        table->tb->empty_cells = TableProp::EMPTY_CELLS_HIDE;
                        log_debug("Table empty-cells: hide");
                    } else {
                        table->tb->empty_cells = TableProp::EMPTY_CELLS_SHOW;
                        log_debug("Table empty-cells: show");
                    }
                }
            }

            // Read table-layout property (CSS 2.1 Section 17.5.2)
            CssDeclaration* layout_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_TABLE_LAYOUT);

            if (layout_decl && layout_decl->value) {
                CssValue* val = (CssValue*)layout_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    if (val->data.keyword == CSS_VALUE_FIXED) {
                        table->tb->table_layout = TableProp::TABLE_LAYOUT_FIXED;
                        log_debug("Table table-layout: fixed (from CSS)");
                    } else if (val->data.keyword == CSS_VALUE_AUTO) {
                        table->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;
                        log_debug("Table table-layout: auto (from CSS)");
                    }
                }
            }
        } else {
            // CSS 2.1 §17.6: border-collapse is an inherited property.
            // Anonymous table elements (e.g., ::anon-table created when <table> has display:block)
            // have no specified_style, so we must inherit from ancestors.
            // Also inherit border-spacing (CSS 2.1 §17.6.1: inherited property).
            bool inherited_collapse = false;
            if (table_inherit_border_collapse(lycon, element, &inherited_collapse)) {
                table->tb->border_collapse = inherited_collapse;
                log_debug("Table border-collapse: anonymous inherit -> %s (from ancestor)",
                          table->tb->border_collapse ? "collapse" : "separate");
            }

            bool is_html_table = (dom_elem->tag() == HTM_TAG_TABLE);
            if (!is_html_table && table_inherit_border_spacing(lycon, element,
                    &table->tb->border_spacing_h, &table->tb->border_spacing_v)) {
                log_debug("Table border-spacing: anonymous inherit -> h=%.2fpx v=%.2fpx (from ancestor)",
                          table->tb->border_spacing_h, table->tb->border_spacing_v);
            }
        }
    }

    // Check if table-layout was already set to FIXED by CSS
    // If so, respect the CSS value and don't override it with heuristic
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED) {
        log_debug("Table layout: FIXED (from CSS), skipping heuristic");
        return;
    }

    // Default to auto layout per CSS 2.1 specification
    // The table-layout property initial value is 'auto'
    table->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;
    log_debug("Table layout: auto (CSS 2.1 default)");
}

// Parse cell attributes (colspan, rowspan)
static bool table_cell_apply_vertical_align_keyword(ViewTableCell* cell,
                                                    CssEnum keyword,
                                                    bool from_inline_style) {
    if (!cell || !cell->td) return false;

#ifndef NDEBUG
    // release strips log_debug(), so the readable keyword name must be debug-only too.
    const char* align_name = nullptr;
#endif
    switch (keyword) {
    case CSS_VALUE_TOP:
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_TOP;
#ifndef NDEBUG
        align_name = "top";
#endif
        break;
    case CSS_VALUE_MIDDLE:
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_MIDDLE;
#ifndef NDEBUG
        align_name = "middle";
#endif
        break;
    case CSS_VALUE_BOTTOM:
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_BOTTOM;
#ifndef NDEBUG
        align_name = "bottom";
#endif
        break;
    case CSS_VALUE_BASELINE:
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_BASELINE;
#ifndef NDEBUG
        align_name = "baseline";
#endif
        break;
    default:
        return false;
    }

#ifndef NDEBUG
    if (from_inline_style) {
        log_debug("Cell vertical-align from in_line: %s", align_name);
    } else {
        log_debug("Cell vertical-align: %s", align_name);
    }
#endif
    return true;
}

static void parse_cell_attributes(LayoutContext* lycon, DomNode* cellNode, ViewTableCell* cell) {
    assert(cell->td);
    // Initialize defaults
    cell->td->col_span = 1;
    cell->td->row_span = 1;
    cell->td->col_index = -1;
    cell->td->row_index = -1;
    cell->td->is_empty = is_cell_empty(cell) ? 1 : 0;  // Check if cell has no content
    // CSS 2.1: Default vertical-align is 'baseline' (initial value)
    // HTML TD/TH elements get 'middle' via UA stylesheet (set in resolve_htm_style.cpp)
    // For CSS display:table-cell, baseline alignment positions single-line text at top
    cell->td->vertical_align = TableCellProp::CELL_VALIGN_BASELINE;
    if (!cellNode->is_element()) return;

    if (cellNode->node_type == DOM_NODE_ELEMENT) {
        // Lambda CSS path
        DomElement* dom_elem = cellNode->as_element();

        // Parse colspan attribute
        const char* colspan_str = dom_element_get_attribute(dom_elem, "colspan");
        if (colspan_str && colspan_str[0] != '\0') {
            int span = (int)str_to_int64_default(colspan_str, strlen(colspan_str), 0); // INT_CAST_OK: string length
            if (span > 0 && span <= 1000) {
                cell->td->col_span = span;
            }
        }

        // Parse rowspan attribute
        const char* rowspan_str = dom_element_get_attribute(dom_elem, "rowspan");
        if (rowspan_str && rowspan_str[0] != '\0') {
            int span = (int)str_to_int64_default(rowspan_str, strlen(rowspan_str), 0); // INT_CAST_OK: string length
            if (span == 0) {
                // HTML spec: rowspan=0 means "span all remaining rows in the row group"
                // Store as 0 sentinel - resolved in analyze_table_structure
                cell->td->row_span = 0;
            } else if (span > 0 && span <= 65534) {
                cell->td->row_span = span;
            }
        }

        // Parse vertical-align: check resolved in_line property first (set by apply_element_default_style),
        // then check CSS declarations for overrides
        // First, check the resolved in_line->vertical_align (set by HTML default styles in resolve_htm_style.cpp)
        // This handles the CSS 2.1 default: vertical-align: middle for td/th
        if (cell->in_line && cell->in_line->vertical_align) {
            table_cell_apply_vertical_align_keyword(cell, cell->in_line->vertical_align, true);
        }

        // Then check CSS declarations (may override the default)
        if (dom_elem->specified_style) {
            CssDeclaration* valign_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_VERTICAL_ALIGN);

            log_debug("parse_cell_attributes: element=%s, specified_style=%p, valign_decl=%p",
                      cellNode->node_name(), (void*)dom_elem->specified_style, (void*)valign_decl);
            if (valign_decl && valign_decl->value) {
                log_debug("valign_decl->value: type=%d, keyword=%d (TOP=%d, MIDDLE=%d, BOTTOM=%d)",
                          valign_decl->value->type,
                          valign_decl->value->type == CSS_VALUE_TYPE_KEYWORD ? valign_decl->value->data.keyword : -1,
                          CSS_VALUE_TOP, CSS_VALUE_MIDDLE, CSS_VALUE_BOTTOM);
            }

            if (valign_decl && valign_decl->value && valign_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                table_cell_apply_vertical_align_keyword(
                    cell, valign_decl->value->data.keyword, false);
            }
        }
    }
}

// =============================================================================
// TABLE STRUCTURE BUILDER - ANONYMOUS BOX SUPPORT
// =============================================================================

// CSS 2.1 Section 17.2.1: Anonymous table objects
// When the document language does not contain elements to represent
// missing table components, user agents must generate anonymous objects.
//
// Per CSS 2.1 spec, anonymous boxes:
// - INHERIT all inheritable properties from their table parent (color, font, etc.)
// - Use INITIAL VALUES for non-inherited properties (margin, padding, border, background)
//
// This implementation creates actual anonymous DomElement nodes with proper styling,
// rather than using flags, to ensure correct layout behavior.

// Helper: Check if a display value is a row group type
static inline bool is_row_group_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_ROW_GROUP ||
           display == CSS_VALUE_TABLE_HEADER_GROUP ||
           display == CSS_VALUE_TABLE_FOOTER_GROUP;
}

// Helper: Check if a display value is a table cell type
static inline bool is_cell_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_CELL;
}

// Helper: Check if a display value is a table row type
static inline bool is_row_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_ROW;
}

// Helper: Check if a display value is a column type
static inline bool is_column_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_COLUMN || display == CSS_VALUE_TABLE_COLUMN_GROUP;
}

// Helper: Check if a display value is a caption type
static inline bool is_caption_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_CAPTION;
}

static bool table_view_is_caption(ViewBlock* child) {
    if (!child) return false;
    DisplayValue child_display = resolve_display_value((void*)child);
    return child->tag() == HTM_TAG_CAPTION ||
        is_caption_display(child_display.inner);
}

static void inherit_anonymous_table_block_props(LayoutContext* lycon, DomElement* anon, DomElement* parent) {
    if (!lycon || !anon || !parent) return;

    anon->blk = alloc_block_prop(lycon);
    if (!anon->blk) return;

    if (parent->blk) {
        anon->blk->text_align = parent->blk->text_align;
        anon->blk->text_align_last = parent->blk->text_align_last;
        anon->blk->direction = parent->blk->direction;
        anon->blk->text_transform = parent->blk->text_transform;
        anon->blk->line_height = parent->blk->line_height;
        anon->blk->text_indent = parent->blk->text_indent;
        anon->blk->text_indent_percent = parent->blk->text_indent_percent;
        anon->blk->text_indent_calc = parent->blk->text_indent_calc;
        anon->blk->white_space = parent->blk->white_space;
        anon->blk->word_break = parent->blk->word_break;
        anon->blk->overflow_wrap = parent->blk->overflow_wrap;
        anon->blk->line_break = parent->blk->line_break;
        anon->blk->text_spacing_trim = parent->blk->text_spacing_trim;
        anon->blk->tab_size = parent->blk->tab_size;
        anon->blk->text_box_trim = parent->blk->text_box_trim;
        anon->blk->text_box_over_edge = parent->blk->text_box_over_edge;
        anon->blk->text_box_under_edge = parent->blk->text_box_under_edge;
        anon->blk->text_overflow = parent->blk->text_overflow;
        anon->blk->line_clamp = parent->blk->line_clamp;
    }

    CssEnum specified_white_space = layout_specified_keyword(parent, CSS_PROPERTY_WHITE_SPACE);
    if (specified_white_space != 0) {
        anon->blk->white_space = specified_white_space;
    }
    CssEnum specified_direction = layout_specified_keyword(parent, CSS_PROPERTY_DIRECTION);
    if (specified_direction != 0) {
        anon->blk->direction = specified_direction;
    }
    CssEnum specified_text_transform = layout_specified_keyword(parent, CSS_PROPERTY_TEXT_TRANSFORM);
    if (specified_text_transform != 0) {
        anon->blk->text_transform = specified_text_transform;
    }
    CssEnum specified_word_break = layout_specified_keyword(parent, CSS_PROPERTY_WORD_BREAK);
    if (specified_word_break != 0) {
        anon->blk->word_break = specified_word_break;
    }
    CssEnum specified_overflow_wrap = layout_specified_keyword(parent, CSS_PROPERTY_OVERFLOW_WRAP);
    if (specified_overflow_wrap != 0) {
        anon->blk->overflow_wrap = specified_overflow_wrap;
    }
    CssEnum specified_line_break = layout_specified_keyword(parent, CSS_PROPERTY_LINE_BREAK);
    if (specified_line_break != 0) {
        anon->blk->line_break = specified_line_break;
    }
}

// =============================================================================
// ANONYMOUS TABLE ELEMENT CREATION (CSS 2.1 Section 17.2.1)
// =============================================================================

/**
 * Create an anonymous table element with proper CSS spec styling.
 *
 * Per CSS 2.1 Section 17.2.1:
 * - Anonymous boxes inherit inheritable properties from their table parent
 * - Non-inherited properties get their initial values (no margin, padding, border, background)
 *
 * @param lycon Layout context
 * @param parent Parent element (provides inherited styles)
 * @param display_type Display type for the anonymous element (table-row-group, table-row, table-cell)
 * @param tag_name Tag name for debugging (e.g., "::anon-tbody", "::anon-tr", "::anon-td")
 * @return New anonymous DomElement, or NULL on failure
 */
static DomElement* create_anonymous_table_element(LayoutContext* lycon, DomElement* parent,
                                                   CssEnum display_type, const char* tag_name) {
    if (!lycon || !parent) return nullptr;

    Pool* pool = lycon->doc->view_tree->pool;
    if (!pool) return nullptr;

    // Allocate the anonymous element
    DomElement* anon = lam::pool_alloc_dom_element(pool);
    if (!anon) return nullptr;

    // Initialize as element node
    anon->node_type = DOM_NODE_ELEMENT;
    dom_element_retain_tag_name(anon, lam::borrow_const(lam::promote_to_pool(pool, tag_name)));
    anon->doc = parent->doc;
    anon->parent = parent;
    anon->first_child = nullptr;
    anon->last_child = nullptr;
    anon->next_sibling = nullptr;
    anon->prev_sibling = nullptr;

    // Set display type based on requested type
    switch (display_type) {
        case CSS_VALUE_TABLE_ROW_GROUP:
        case CSS_VALUE_TABLE_HEADER_GROUP:
        case CSS_VALUE_TABLE_FOOTER_GROUP:
            anon->display.outer = CSS_VALUE_TABLE_ROW_GROUP;
            anon->display.inner = CSS_VALUE_TABLE_ROW_GROUP;
            break;
        case CSS_VALUE_TABLE_ROW:
            anon->display.outer = CSS_VALUE_TABLE_ROW;
            anon->display.inner = CSS_VALUE_TABLE_ROW;
            break;
        case CSS_VALUE_TABLE_CELL:
            anon->display.outer = CSS_VALUE_TABLE_CELL;
            anon->display.inner = CSS_VALUE_TABLE_CELL;
            break;
        default:
            anon->display.outer = display_type;
            anon->display.inner = display_type;
            break;
    }

    // CSS 2.1 Section 17.2.1: Anonymous boxes inherit inheritable properties
    // CSS 2.1 §17.2.1: Anonymous boxes inherit inheritable properties from parent.
    // Only inherit from parent->font (not lycon->font.style) because this function is
    // called during anonymous box generation before child style resolution.
    // Font context propagation through lycon->font happens later in mark_table_node.
    if (parent->font) {
            anon->font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
        if (anon->font) {
            // Copy only specified font properties, not derived/cached fields
            radiant_retain_font_family(anon->font, lam::PoolPtr<char>(parent->font->family));  // share the string
            anon->font->font_size = parent->font->font_size;
            anon->font->font_style = parent->font->font_style;
            anon->font->font_weight = parent->font->font_weight;
            anon->font->font_variant = parent->font->font_variant;
            anon->font->text_deco = parent->font->text_deco;
            anon->font->text_deco_color = parent->font->text_deco_color;
            anon->font->text_deco_style = parent->font->text_deco_style;
            anon->font->text_deco_thickness = parent->font->text_deco_thickness;
            anon->font->text_underline_offset = parent->font->text_underline_offset;
            anon->font->letter_spacing = parent->font->letter_spacing;
            anon->font->word_spacing = parent->font->word_spacing;
            // Derived fields (space_width, ascender, descender, font_height,
            // has_kerning, font_handle) are left as zero/NULL from pool_calloc.
            // They will be resolved by setup_font() during layout.
        }
    }

    inherit_anonymous_table_block_props(lycon, anon, parent);

    // Copy inherited inline properties (color is inheritable)
    if (parent->in_line) {
        anon->in_line = (InlineProp*)pool_calloc(pool, sizeof(InlineProp));
        if (anon->in_line) {
            // Only copy inheritable properties
            anon->in_line->color = parent->in_line->color;  // color is inherited
            anon->in_line->has_color = parent->in_line->has_color;
            anon->in_line->cursor = CSS_VALUE_AUTO;  // cursor inherits, use auto as default
            anon->in_line->visibility = 0;  // visibility inherits, 0 = visible
            anon->in_line->opacity = 1.0f;  // opacity is not inherited, use initial
            anon->in_line->vertical_align = CSS_VALUE_BASELINE;  // not inherited, use initial
        }
    }

    // CSS 2.1: Non-inherited properties get initial values
    // - margin: 0 (initial)
    // - padding: 0 (initial)
    // - border: none (initial)
    // - background: transparent (initial)
    // By using pool_calloc, all these are already 0/NULL which represents initial values
    anon->bound = nullptr;  // No margin, padding, border, or background

    // Mark that this element doesn't need style resolution (styles are set here)
    anon->styles_resolved = true;

    log_debug("[ANON-TABLE] Created %s element (display=%d) with inherited styles from <%s>",
              tag_name, display_type, parent->tag_name ? parent->tag_name : "unknown");

    return anon;
}

/**
 * Insert a child element at the end of parent's child list
 */
static void append_child_to_element(DomElement* parent, DomElement* child) {
    if (!parent || !child) return;

    child->parent = parent;
    child->next_sibling = nullptr;

    if (parent->last_child) {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
        parent->last_child = child;
    } else {
        parent->first_child = child;
        parent->last_child = child;
        child->prev_sibling = nullptr;
    }
}

/**
 * Move a node from its current parent to a new parent
 * Removes from old parent and appends to new parent
 */
static void reparent_node(DomNode* node, DomElement* new_parent) {
    if (!node || !new_parent) return;

    DomElement* old_parent = lam::dom_as<DOM_NODE_ELEMENT>(node->parent);

    // Remove from old parent's child list
    if (old_parent) {
        if (node->prev_sibling) {
            node->prev_sibling->next_sibling = node->next_sibling;
        } else {
            old_parent->first_child = node->next_sibling;
        }

        if (node->next_sibling) {
            node->next_sibling->prev_sibling = node->prev_sibling;
        } else {
            old_parent->last_child = node->prev_sibling;
        }
    }

    // Add to new parent
    node->parent = new_parent;
    node->prev_sibling = new_parent->last_child;
    node->next_sibling = nullptr;

    if (new_parent->last_child) {
        new_parent->last_child->next_sibling = node;
    } else {
        new_parent->first_child = node;
    }
    new_parent->last_child = node;
}

/**
 * Insert a node before another node in the DOM tree.
 * The reference node must already be a child of the parent.
 */
static void insert_node_before(DomElement* parent, DomNode* new_node, DomNode* ref_node) {
    if (!parent || !new_node) return;

    if (!ref_node) {
        // Append at end
        new_node->parent = parent;
        new_node->next_sibling = nullptr;
        new_node->prev_sibling = parent->last_child;
        if (parent->last_child) {
            parent->last_child->next_sibling = new_node;
        } else {
            parent->first_child = new_node;
        }
        parent->last_child = new_node;
        return;
    }

    new_node->parent = parent;
    new_node->next_sibling = ref_node;
    new_node->prev_sibling = ref_node->prev_sibling;

    if (ref_node->prev_sibling) {
        ref_node->prev_sibling->next_sibling = new_node;
    } else {
        parent->first_child = new_node;
    }
    ref_node->prev_sibling = new_node;
}

// CSS 2.1 Section 17.2.1: Generate anonymous table boxes.
// This implements the full CSS 2.1 anonymous table box generation algorithm:
// 1. If a child of a table-row is not a table-cell, wrap it in anonymous table-cell
// 2. If a child of a table-row-group is not a table-row, wrap consecutive cells in anonymous table-row
/**
 * Helper to check if an element is absolutely positioned or fixed.
 * CSS 2.1 §9.7: Such elements are completely out of flow and should NOT
 * participate in anonymous table box generation.
 * Note: floated elements are NOT excluded — they still float within anonymous cells.
 */
static bool is_abspos_or_fixed(DomElement* elem) {
    if (!elem) return false;

    // Check resolved position prop first (if styles already resolved)
    if (elem->position) {
        if (layout_position_is_abs_fixed(elem->position)) {
            return true;
        }
    }

    CssEnum position = layout_specified_keyword(
        elem, CSS_PROPERTY_POSITION, CSS_VALUE_STATIC);
    return position == CSS_VALUE_ABSOLUTE || position == CSS_VALUE_FIXED;
}

static bool table_white_space_preserves_space_advance(CssEnum white_space) {
    return white_space == CSS_VALUE_PRE ||
           white_space == CSS_VALUE_PRE_WRAP ||
           white_space == CSS_VALUE_BREAK_SPACES;
}

static bool table_text_node_has_non_whitespace_content(DomNode* node) {
    if (!node || !node->is_text()) return false;

    const char* text = lam::dom_require<DOM_NODE_TEXT>(node)->text;
    if (!text || !*text) return false;

    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\f' && *p != '\v') {
            return true;
        }
    }

    return false;
}

static bool table_text_node_has_preserved_whitespace_content(DomNode* node) {
    if (!node || !node->is_text()) return false;

    const char* text = lam::dom_require<DOM_NODE_TEXT>(node)->text;
    if (!text || !*text) return false;
    if (table_text_node_has_non_whitespace_content(node)) return false;

    return table_white_space_preserves_space_advance(get_white_space_value(node));
}

static bool table_anonymous_run_allows_preserved_whitespace(ArrayList* run) {
    if (!run) return false;

    for (int i = run->length - 1; i >= 0; i--) {
        DomNode* node = static_cast<DomNode*>(run->data[i]);
        if (!node) continue;
        if (node->is_text()) {
            return table_text_node_has_non_whitespace_content(node) ||
                   table_text_node_has_preserved_whitespace_content(node);
        }
        if (node->is_element()) {
            DisplayValue display = resolve_display_value(node);
            if (layout_display_is_none(display)) {
                continue;
            }
            return false;
        }
    }

    return false;
}

static bool table_text_node_generates_anonymous_content(DomNode* node,
        bool run_allows_preserved_whitespace) {
    if (table_text_node_has_non_whitespace_content(node)) return true;
    if (!table_text_node_has_preserved_whitespace_content(node)) return false;

    // CSS 2.1 §17.2.1 anonymous table content follows normal table boundary
    // filtering: preserved whitespace is content inside a non-cell run, but a
    // whitespace-only node at the boundary after a table-cell does not start one.
    return run_allows_preserved_whitespace;
}

/**
 * Helper to wrap a run of nodes in table cells.
 * - Elements that are already cells get reparented directly
 * - Consecutive non-cell elements get wrapped together in a single anonymous cell
 * This matches CSS 2.1 behavior where consecutive non-cell content forms a single cell.
 */
static void wrap_run_in_cells(LayoutContext* lycon, ArrayList* run, DomElement* parent_row) {
    DomElement* current_anon_td = nullptr;

    for (int i = 0; i < run->length; i++) {
        DomNode* node = static_cast<DomNode*>(run->data[i]);

        bool is_cell = false;
        if (node->is_element()) {
            DisplayValue disp = resolve_display_value(node);
            is_cell = is_cell_display(disp.inner);
        }

        if (is_cell) {
            // Cell element - reparent directly, reset accumulator
            current_anon_td = nullptr;
            reparent_node(node, parent_row);
        } else {
            // Non-cell content - add to current anonymous cell or create new one
            if (!current_anon_td) {
                current_anon_td = create_anonymous_table_element(lycon, parent_row,
                    CSS_VALUE_TABLE_CELL, "::anon-td");
                append_child_to_element(parent_row, current_anon_td);
                log_debug("%s [ANON-TABLE] Created anonymous cell for non-cell content", parent_row->source_loc());
            }
            reparent_node(node, current_anon_td);
        }
    }
}

static void place_anonymous_table_child(DomElement* parent, DomElement* child,
                                        DomNode* before) {
    if (before) {
        insert_node_before(parent, static_cast<DomNode*>(child), before);
    } else {
        append_child_to_element(parent, child);
    }
}

static void flush_anonymous_cell_run(LayoutContext* lycon, DomElement* parent,
                                     ArrayList* run, DomNode* before,
                                     bool create_row_group) {
    if (!run || run->length == 0) return;

    DomElement* row_parent = parent;
    DomElement* row_group = nullptr;
    if (create_row_group) {
        row_group = create_anonymous_table_element(
            lycon, parent, CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");
        row_parent = row_group;
    }
    DomElement* row = create_anonymous_table_element(
        lycon, row_parent, CSS_VALUE_TABLE_ROW, "::anon-tr");
    if (row_group) append_child_to_element(row_group, row);
    wrap_run_in_cells(lycon, run, row);
    place_anonymous_table_child(parent, row_group ? row_group : row, before);
    arraylist_clear(run);
}

static void flush_anonymous_row_run(LayoutContext* lycon, DomElement* table,
                                    ArrayList* run, DomNode* before) {
    if (!run || run->length == 0) return;

    DomElement* row_group = create_anonymous_table_element(
        lycon, table, CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");
    for (int i = 0; i < run->length; i++) {
        reparent_node(static_cast<DomNode*>(run->data[i]), row_group);
    }
    place_anonymous_table_child(table, row_group, before);
    arraylist_clear(run);
}

/*
 * CSS 2.1 Section 17.2.1: Anonymous table objects
 * 3. If a child of a table/inline-table is not a proper table child (row-group, row, column, caption),
 *    wrap it appropriately:
 *    - Consecutive table-cells get wrapped in anonymous table-row, then in anonymous table-row-group
 *    - Consecutive table-rows get wrapped in anonymous table-row-group
 *    - Non-table content (like text/inline) gets wrapped in cell -> row -> row-group
 *
 * Note: This modifies the DOM tree by inserting anonymous wrapper elements.
 * Anonymous elements are created with proper CSS 2.1 style inheritance.
 */
static void generate_anonymous_table_boxes(LayoutContext* lycon, DomElement* table) {
    if (!lycon || !table) return;

    Pool* pool = lycon->doc->view_tree->pool;
    if (!pool) return;

    log_debug("%s [ANON-TABLE] === Starting CSS 2.1 anonymous box generation ===", table->source_loc());

    // ========================================================================
    // PHASE 1: Process children of table/inline-table
    // CSS 2.1 Rule: Children that are not proper table children need wrapping
    // ========================================================================

    // First pass: identify what needs to be wrapped and collect runs of consecutive elements
    ArrayList* children_to_process = arraylist_new(8);
    for (DomNode* child = table->first_child; child; child = child->next_sibling) {
        arraylist_append(children_to_process, child);
    }

    // Track runs of consecutive cells that need wrapping
    ArrayList* current_cell_run = arraylist_new(8);
    ArrayList* current_row_run = arraylist_new(8);
    for (int i = 0; i < children_to_process->length; ) {
        DomNode* child = static_cast<DomNode*>(children_to_process->data[i]);

        // Handle text nodes - they need to be wrapped in anonymous cells
        // CSS 2.1 Section 17.2.1: "Any content that is not a table-* element
        // will be wrapped in an anonymous table-cell box"
        if (child->is_text()) {
            if (table_text_node_generates_anonymous_content(child,
                    table_anonymous_run_allows_preserved_whitespace(current_cell_run))) {
                log_debug("%s [ANON-TABLE] Text node with content needs cell wrapping", table->source_loc());
                arraylist_append(current_cell_run, child);
            }
            i++;
            continue;
        }

        // Skip other non-element nodes
        if (!child->is_element()) {
            i++;
            continue;
        }

        DisplayValue display = resolve_display_value(child);
        uintptr_t tag = child->tag();

        // Check if this is a proper table child
        bool is_row_group = is_row_group_display(display.inner);
        bool is_row = is_row_display(display.inner);
        bool is_cell = is_cell_display(display.inner);
        bool is_column = is_column_display(display.inner) ||
                        tag == HTM_TAG_COL || tag == HTM_TAG_COLGROUP;
        bool is_caption = is_caption_display(display.inner) || tag == HTM_TAG_CAPTION;

        if (is_row_group || is_column || is_caption) {
            // Proper table child - flush any accumulated runs first
            if (current_cell_run->length > 0) {
                log_debug("%s [ANON-TABLE] Flushing cell run of %d items before row group", table->source_loc(),
                         current_cell_run->length);
                flush_anonymous_cell_run(
                    lycon, table, current_cell_run, child, true);
            }

            if (current_row_run->length > 0) {
                log_debug("%s [ANON-TABLE] Flushing row run of %d rows before row group", table->source_loc(),
                         current_row_run->length);
                flush_anonymous_row_run(lycon, table, current_row_run, child);
            }

            i++;
            continue;
        }

        if (is_row) {
            // Row as direct child of table - accumulate for wrapping in tbody
            if (current_cell_run->length > 0) {
                // Flush cells first - they get their own tbody+tr
                log_debug("%s [ANON-TABLE] Flushing cell run of %d items before row", table->source_loc(),
                         current_cell_run->length);
                flush_anonymous_cell_run(
                    lycon, table, current_cell_run, child, true);
            }

            arraylist_append(current_row_run, child);
            i++;
            continue;
        }

        if (is_cell) {
            // Cell as direct child of table - accumulate for wrapping in tbody+tr
            if (current_row_run->length > 0) {
                // Flush rows first
                log_debug("%s [ANON-TABLE] Flushing row run of %d rows before cell", table->source_loc(),
                         current_row_run->length);
                flush_anonymous_row_run(lycon, table, current_row_run, child);
            }

            arraylist_append(current_cell_run, child);
            i++;
            continue;
        }

        // Non-table content (text, inline elements, etc.) - wrap in cell
        // CSS 2.1: "Any other child of a table element is treated as if it were
        // wrapped in an anonymous table-cell box"
        // Note: Floated/positioned elements are already skipped above (out of flow).
        log_debug("%s [ANON-TABLE] Non-table content needs cell wrapping: tag=%s", table->source_loc(),
                 child->node_name() ? child->node_name() : "unknown");

        // For simplicity, treat non-table content as a cell that will be wrapped later
        arraylist_append(current_cell_run, child);
        i++;
    }

    // Flush any remaining runs
    if (current_cell_run->length > 0) {
        log_debug("%s [ANON-TABLE] Flushing final cell run of %d items", table->source_loc(), current_cell_run->length);
        flush_anonymous_cell_run(
            lycon, table, current_cell_run, nullptr, true);
    }

    if (current_row_run->length > 0) {
        log_debug("%s [ANON-TABLE] Flushing final row run of %d rows", table->source_loc(), current_row_run->length);
        flush_anonymous_row_run(lycon, table, current_row_run, nullptr);
    }

    // Free ArrayLists from Phase 1
    arraylist_free(children_to_process);
    arraylist_free(current_cell_run);
    arraylist_free(current_row_run);

    // ========================================================================
    // PHASE 2: Process children of row groups (thead, tbody, tfoot)
    // CSS 2.1: If a child of a row-group is not a table-row, wrap cells in anonymous row
    // ========================================================================

    for (DomNode* child = table->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;

        DomElement* row_group = child->as_element();
        DisplayValue display = resolve_display_value(child);

        // Only process row groups
        if (!is_row_group_display(display.inner)) {
            continue;
        }

        // Collect children that need wrapping
        ArrayList* group_children = arraylist_new(8);
        for (DomNode* gchild = row_group->first_child; gchild; gchild = gchild->next_sibling) {
            arraylist_append(group_children, gchild);
        }

        ArrayList* cell_run = arraylist_new(8);
        for (int j = 0; j < group_children->length; j++) {
            DomNode* gchild = static_cast<DomNode*>(group_children->data[j]);

            if (!gchild->is_element()) {
                // Text nodes need to be wrapped in cell -> row
                // CSS 2.1: "Any other child of a table-row-group is treated as if it were
                // wrapped in an anonymous table-cell box"
                if (gchild->is_text()) {
                    if (table_text_node_generates_anonymous_content(gchild,
                            table_anonymous_run_allows_preserved_whitespace(cell_run))) {
                        arraylist_append(cell_run, gchild);
                        log_debug("%s [ANON-TABLE] Phase 2: Text node with content added to cell run", table->source_loc());
                    }
                }
                continue;
            }

            DisplayValue gdisp = resolve_display_value(gchild);

            bool is_row = is_row_display(gdisp.inner);
            bool is_cell = is_cell_display(gdisp.inner);

            if (is_row) {
                // Flush any accumulated cells before this row
                if (cell_run->length > 0) {
                    log_debug("%s [ANON-TABLE] Wrapping %d items in row group in anonymous row", table->source_loc(),
                             cell_run->length);
                    flush_anonymous_cell_run(
                        lycon, row_group, cell_run, gchild, false);
                }
                continue;
            }

            if (is_cell) {
                arraylist_append(cell_run, gchild);
                continue;
            }

            // Non-row, non-cell content - treat as content to wrap in cell then row
            arraylist_append(cell_run, gchild);
        }

        // Flush remaining cells
        if (cell_run->length > 0) {
            log_debug("%s [ANON-TABLE] Wrapping final %d items in row group in anonymous row", table->source_loc(),
                     cell_run->length);
            flush_anonymous_cell_run(
                lycon, row_group, cell_run, nullptr, false);
        }

        // Free ArrayLists from this group
        arraylist_free(group_children);
        arraylist_free(cell_run);
    }

    // ========================================================================
    // PHASE 3: Process children of rows
    // CSS 2.1: If a child of a table-row is not a table-cell, wrap it in anonymous cell
    // ========================================================================

    // Process rows in all row groups
    for (DomNode* group_node = table->first_child; group_node; group_node = group_node->next_sibling) {
        if (!group_node->is_element()) continue;

        DomElement* row_group = group_node->as_element();
        DisplayValue group_display = resolve_display_value(group_node);

        // Only process row groups
        if (!is_row_group_display(group_display.inner)) {
            continue;
        }

        // Process rows in this group
        for (DomNode* row_node = row_group->first_child; row_node; row_node = row_node->next_sibling) {
            if (!row_node->is_element()) continue;

            DomElement* row = row_node->as_element();
            DisplayValue row_display = resolve_display_value(row_node);

            // Only process rows
            if (!is_row_display(row_display.inner)) {
                continue;
            }

            // Collect non-cell children that need wrapping
            ArrayList* row_children = arraylist_new(8);
            for (DomNode* rchild = row->first_child; rchild; rchild = rchild->next_sibling) {
                arraylist_append(row_children, rchild);
            }

            ArrayList* non_cell_run = arraylist_new(8);

            for (int k = 0; k < row_children->length; k++) {
                DomNode* rchild = static_cast<DomNode*>(row_children->data[k]);

                if (!rchild->is_element()) {
                    // Text nodes need to be wrapped in cells
                    // CSS 2.1: "Any other child of a table-row is treated as if it were
                    // wrapped in an anonymous table-cell box"
                    if (rchild->is_text()) {
                        if (table_text_node_generates_anonymous_content(rchild,
                                table_anonymous_run_allows_preserved_whitespace(non_cell_run))) {
                            arraylist_append(non_cell_run, rchild);
                            log_debug("%s [ANON-TABLE] Phase 3: Text node with content added to non-cell run", table->source_loc());
                        }
                    }
                    continue;
                }

                DisplayValue rdisp = resolve_display_value(rchild);

                bool is_cell = is_cell_display(rdisp.inner);

                if (is_cell) {
                    // Flush any accumulated non-cell content
                    if (non_cell_run->length > 0) {
                        log_debug("%s [ANON-TABLE] Wrapping %d non-cell items in row in anonymous cell", table->source_loc(),
                                 non_cell_run->length);

                        DomElement* anon_td = create_anonymous_table_element(lycon, row,
                            CSS_VALUE_TABLE_CELL, "::anon-td");

                        for (int m = 0; m < non_cell_run->length; m++) {
                            DomNode* content_node = static_cast<DomNode*>(non_cell_run->data[m]);
                            reparent_node(content_node, anon_td);
                        }

                        insert_node_before(row, static_cast<DomNode*>(anon_td), rchild);
                        arraylist_clear(non_cell_run);
                    }
                    continue;
                }

                // Non-cell content in row
                arraylist_append(non_cell_run, rchild);
            }

            // Flush remaining non-cell content
            if (non_cell_run->length > 0) {
                log_debug("%s [ANON-TABLE] Wrapping final %d non-cell items in row in anonymous cell", table->source_loc(),
                         non_cell_run->length);

                DomElement* anon_td = create_anonymous_table_element(lycon, row,
                    CSS_VALUE_TABLE_CELL, "::anon-td");

                for (int m = 0; m < non_cell_run->length; m++) {
                    DomNode* content_node = static_cast<DomNode*>(non_cell_run->data[m]);
                    reparent_node(content_node, anon_td);
                }

                append_child_to_element(row, anon_td);
            }

            // Free ArrayLists from this row
            arraylist_free(row_children);
            arraylist_free(non_cell_run);
        }
    }

    log_debug("%s [ANON-TABLE] === Anonymous box generation complete ===", table->source_loc());
}

// Detect and set anonymous box flags for a table element
// Call this after build_table_tree() but before layout
static void detect_anonymous_boxes(ViewTable* table) {
    if (!table || !table->tb) return;

    // Initialize all anonymous flags to false
    table->tb->is_annoy_tbody = 0;
    table->tb->is_annoy_tr = 0;
    table->tb->is_annoy_td = 0;
    table->tb->is_annoy_colgroup = 0;

    bool has_row_group = false;
    bool has_direct_row = false;
    bool has_direct_cell = false;

    // Scan immediate children to detect structure
    for (View* child = static_cast<View*>(table->first_child); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            has_row_group = true;
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            has_direct_row = true;
        } else if (child->view_type == RDT_VIEW_TABLE_CELL) {
            has_direct_cell = true;
        }
    }

    // Case 1: Table has direct rows without row groups
    // => Table acts as anonymous tbody
    if (has_direct_row && !has_row_group) {
        table->tb->is_annoy_tbody = 1;
        log_debug("%s Anonymous box: table doubled as tbody", table->source_loc());
    }

    // Case 2: Table has direct cells without rows
    // => Table acts as anonymous tbody AND anonymous tr
    if (has_direct_cell) {
        table->tb->is_annoy_tbody = 1;
        table->tb->is_annoy_tr = 1;
        log_debug("%s Anonymous box: table doubled as tbody+tr", table->source_loc());
    }

    // Now check each row group for anonymous tr cases
    for (View* child = static_cast<View*>(table->first_child); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            // Check if row group has direct cells (no rows)
            bool group_has_direct_cell = false;
            for (View* gchild = static_cast<View*>(lam::dom_require<DOM_NODE_ELEMENT>(child)->first_child); gchild;
                 gchild = static_cast<View*>(gchild->next_sibling)) {
                if (gchild->view_type == RDT_VIEW_TABLE_CELL) {
                    group_has_direct_cell = true;
                    break;
                }
            }
            if (group_has_direct_cell) {
                // Mark the first direct cell as having is_annoy_tr
                // (The group acts as anonymous row)
                for (View* gchild = static_cast<View*>(lam::dom_require<DOM_NODE_ELEMENT>(child)->first_child); gchild;
                     gchild = static_cast<View*>(gchild->next_sibling)) {
                    if (gchild->view_type == RDT_VIEW_TABLE_CELL) {
                        ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(gchild);
                        if (cell->td) {
                            cell->td->is_annoy_tr = 1;
                            log_debug("%s Anonymous box: cell marked as wrapped in anonymous tr", table->source_loc());
                        }
                    }
                }
            }
        }
    }
}

// =============================================================================
// TABLE STRUCTURE BUILDER
// =============================================================================

// Recursive helper to mark table structure nodes with correct view types
static void mark_table_node(LayoutContext* lycon, DomNode* node, ViewElement* parent) {
    if (!node || !node->is_element()) return;

    DisplayValue display = resolve_display_value(node);
    uintptr_t tag = node->tag();

    // CSS 2.1 §9.7: Elements with float become block-level elements
    // Check if this element has float set - if so, it's not a table internal element
    DomElement* elem = node->as_element();
    CssEnum float_value = CSS_VALUE_NONE;
    if (elem->position) {
        float_value = elem->position->float_prop;
    } else {
        float_value = layout_specified_keyword(
            elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
    }

    // If floated, treat as a regular block element and skip table-specific handling
    if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
        log_debug("[TABLE] Floated element %s inside table - treating as block, not table internal", node->source_loc());

        // CSS 2.1 §9.7: Floated elements become block-level
        // Layout this element as a float, not as a table internal element
        DisplayValue float_display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};

        // Mark as pre-laid to prevent double processing
        elem->float_prelaid = true;

        // Save and restore view context for the float
        View* saved_view = lycon->view;

        // Layout the float as a block
        layout_block(lycon, node, float_display);

        // Restore view context
        lycon->view = saved_view;

        return;
    }

    // CSS 2.1 §9.7: Absolutely positioned/fixed elements become block-level
    // and are taken out of flow. Handle them via normal flow code path.
    if (is_abspos_or_fixed(elem)) {
        log_debug("[TABLE] Abspos/fixed element %s inside table - treating as block, not table internal", node->source_loc());

        // Save and restore view context
        View* saved_view = lycon->view;

        // Use layout_flow_node which handles abspos block creation and deferral
        layout_flow_node(lycon, node);

        // Restore view context
        lycon->view = saved_view;

        return;
    }

    // Save context. Table-internal boxes are walked out of normal block flow, so
    // the shared layout font must be restored after each subtree; otherwise a
    // caption/th style can leak into following row groups or sibling rows.
    DomNode* saved_elmt = lycon->elmt;
    FontBox saved_mark_font = lycon->font;
    lycon->elmt = node;

    // Mark node based on display type or HTML tag
    if (tag == HTM_TAG_CAPTION || display.inner == CSS_VALUE_TABLE_CAPTION) {
        // Caption - mark as block and layout content immediately
        ViewBlock* caption = lam::view_require_block(set_view(lycon, RDT_VIEW_BLOCK, node));
        if (caption) {
            caption->display.inner = CSS_VALUE_TABLE_CAPTION;
            lycon->view = static_cast<View*>(caption);
            dom_node_resolve_style(node, lycon);  // Resolve caption styles

            // Read caption-side from caption element's style and store in table
            DomElement* dom_elem = lam::dom_require_element(node);
            if (dom_elem->specified_style && parent && parent->view_type == RDT_VIEW_TABLE) {
                ViewTable* table = lam::view_require<RDT_VIEW_TABLE>(parent);
                if (table->tb) {
                    CssDeclaration* caption_decl = style_tree_get_declaration(
                        dom_elem->specified_style, CSS_PROPERTY_CAPTION_SIDE);
                    bool caption_bottom = false;
                    if (table_resolve_caption_side_value(
                            caption_decl ? caption_decl->value : nullptr, &caption_bottom) &&
                        caption_bottom) {
                        table->tb->caption_side = TableProp::CAPTION_SIDE_BOTTOM;
                        log_debug("%s Caption side: bottom (from caption element)", node->source_loc());
                    }
                }
            }

            LayoutContextScope lscope(lycon);

            float caption_width = lycon->line.right - lycon->line.left;
            if (caption_width <= 0) caption_width = 600;

            // Calculate content width by subtracting padding and border (CSS box model)
            float content_width = caption_width;
            if (caption->bound) {
                content_width -= layout_box_metrics(caption).pad_border_h;
            }
            content_width = max(content_width, 0.0f);

            lycon->block.content_width = content_width;
            lycon->block.content_height = 10000;  // Large enough for content
            lycon->block.advance_y = 0;

            // Calculate inner content bounds from border and padding (same as layout_block)
            // line.left/right include border+padding so child positions are correct
            // advance_y includes border-top+padding-top so child y positions are correct
            float inner_left = 0;
            if (caption->bound) {
                if (caption->bound->border) {
                    inner_left += caption->bound->border->width.left;
                    lycon->block.advance_y += caption->bound->border->width.top;
                }
                inner_left += caption->bound->padding.left;
                lycon->block.advance_y += caption->bound->padding.top;
            }
            lycon->line.left = inner_left;
            lycon->line.right = inner_left + content_width;

            // CSS 2.1 §10.8.1: Set up font and line-height for the caption's own styles
            if (caption->font) {
                setup_font(lycon->ui_context, &lycon->font, caption->font);
            }
            setup_line_height(lycon, caption);
            layout_setup_block_font_metrics(lycon);

            // CSS 2.1 §16.1: Propagate text-indent for caption's first line
            if (caption->blk) {
                if (!isnan(caption->blk->text_indent_percent)) {
                    lycon->block.text_indent = content_width * caption->blk->text_indent_percent / 100.0f;
                } else {
                    lycon->block.text_indent = caption->blk->text_indent;
                }
                if (lycon->block.text_indent != 0.0f) {
                    lycon->block.is_first_line = true;
                    log_debug("%s Caption text-indent: %.1f", node->source_loc(), lycon->block.text_indent);
                }
            }

            line_reset(lycon);  // reset start_view, advance_x, etc. for fresh line

            // Propagate text-align from caption's resolved style (default: center)
            if (caption->blk && caption->blk->text_align) {
                lycon->block.text_align = caption->blk->text_align;
                log_debug("%s Caption text-align: %d", node->source_loc(), caption->blk->text_align);
            }
            // CSS 2.1 §9.2.1: Propagate direction from caption
            if (caption->blk && caption->blk->direction) {
                lycon->block.direction = caption->blk->direction;
            }

            log_debug("%s Caption layout start: width=%d, advance_y=%.1f", node->source_loc(), caption_width, lycon->block.advance_y);

            DomNode* child = lam::dom_require_element(node)->first_child;
            for (; child; child = child->next_sibling) {
                layout_flow_node(lycon, child);
            }
            // Handle last line
            log_debug("%s Caption before line_break: is_line_start=%d, advance_y=%.1f", node->source_loc(), lycon->line.is_line_start, lycon->block.advance_y);
            if (!lycon->line.is_line_start) { line_break(lycon); }
            log_debug("%s Caption after line_break: advance_y=%.1f", node->source_loc(), lycon->block.advance_y);

            // Determine caption height: use given_height if specified, otherwise content flow height
            // advance_y already includes border-top + padding-top, so only add bottom
            float caption_content_height = lycon->block.advance_y;
            float caption_given_height = (caption->blk && caption->blk->given_height >= 0) ? caption->blk->given_height : -1;
            if (caption_given_height >= 0) {
                caption->height = caption_given_height;
                // given_height is content height only, add all padding and border
                if (caption->bound) {
                    caption->height += layout_box_metrics(caption).pad_border_v;
                }
            } else {
                caption->height = caption_content_height;
                // advance_y includes border-top+padding-top, only add bottom
                if (caption->bound) {
                    caption->height += caption->bound->padding.bottom;
                    if (caption->bound->border) {
                        caption->height += caption->bound->border->width.bottom;
                    }
                }
            }
            // Apply min-height/max-height constraints (CSS 2.1 §10.7)
            // caption->height includes content+padding+border; coordinate system must match box-sizing
            if (caption->blk) {
                bool is_border_box = layout_uses_border_box(caption);
                if (is_border_box) {
                    // border-box: caption->height is total, given_min/max are also border-box
                    caption->height = adjust_min_max_height(caption, caption->height);
                } else {
                    // content-box: extract content height, clamp, re-add padding+border
                    BoxMetrics box = layout_box_metrics(caption);
                    float content_h = caption->height - box.pad_border_v;
                    float clamped_h = adjust_min_max_height(caption, content_h);
                    caption->height = clamped_h + box.pad_border_v;
                }
            }
            caption->width = (float)caption_width;  // Preliminary width; final width set during positioning
            log_debug("%s Caption layout end: caption->height=%.1f (given=%.1f, content=%.1f), advance_y=%.1f", node->source_loc(),
                caption->height, caption_given_height, caption_content_height, lycon->block.advance_y);
            // Context auto-restored by lscope destructor
        }
    }
    else if (display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
             display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
             display.inner == CSS_VALUE_TABLE_FOOTER_GROUP) {
        // Row group - mark and recurse
        // NOTE: Section type is determined at runtime via get_section_type() method
        ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(set_view(lycon, RDT_VIEW_TABLE_ROW_GROUP, node));
        if (group) {
            group->display = display;  // preserve thead/tbody/tfoot display distinction
            lycon->view = static_cast<View*>(group);
            dom_node_resolve_style(node, lycon);  // Resolve styles for proper font inheritance
            // Propagate element font to layout context so children inherit correctly.
            // dom_node_resolve_style may skip pre-resolved elements, leaving lycon->font stale.
            if (group->font) {
                setup_font(lycon->ui_context, &lycon->font, group->font);
            }
            DomNode* child = lam::dom_require_element(node)->first_child;
            for (; child; child = child->next_sibling) {
                if (child->is_element()) mark_table_node(lycon, child, lam::view_require_element(group));
            }
        }
    }
    else if (display.inner == CSS_VALUE_TABLE_ROW) {
        // Row - mark and recurse
        ViewTableRow* row = lam::view_require<RDT_VIEW_TABLE_ROW>(set_view(lycon, RDT_VIEW_TABLE_ROW, node));
        if (row) {
            row->display = display;
            lycon->view = static_cast<View*>(row);
            dom_node_resolve_style(node, lycon);  // Resolve styles for proper font inheritance
            // Propagate element font to layout context so children inherit correctly.
            if (row->font) {
                setup_font(lycon->ui_context, &lycon->font, row->font);
            }

            // CSS 2.1 §12.1: Generate ::before/::after pseudo-elements for table rows.
            // When pseudo-elements have display:table-cell, they become cell children of the row.
            // Otherwise, they need wrapping in an anonymous table-cell (CSS 2.1 §17.2.1).
            if (node->is_element()) {
                row->pseudo = alloc_pseudo_content_prop(lycon, lam::view_require_block(row));
                if (row->pseudo) {
                    DomElement* row_elem = node->as_element();
                    // Helper lambda to insert pseudo and wrap in anon cell if needed
                    auto insert_pseudo_for_row = [&](DomElement* pseudo, bool is_before) {
                        if (!pseudo) return;
                        DisplayValue pseudo_display = resolve_display_value(pseudo);
                        bool is_cell = is_cell_display(pseudo_display.inner);
                        if (is_cell) {
                            // Pseudo-element is already a table-cell, insert directly
                            insert_pseudo_into_dom(row_elem, pseudo, is_before);
                        } else {
                            // Wrap non-cell pseudo-element in an anonymous table-cell
                            DomElement* anon_td = create_anonymous_table_element(lycon, row_elem,
                                CSS_VALUE_TABLE_CELL, "::anon-td");
                            if (anon_td) {
                                // Reparent pseudo into the anonymous cell
                                pseudo->parent = anon_td;
                                anon_td->first_child = pseudo;
                                anon_td->last_child = pseudo;
                                // Insert the anonymous cell into the row
                                insert_pseudo_into_dom(row_elem, anon_td, is_before);
                                log_debug("%s [TABLE] Wrapped ::%s pseudo in anonymous cell",
                                         node->source_loc(), is_before ? "before" : "after");
                            }
                        }
                    };
                    insert_pseudo_for_row(row->pseudo->before, true);
                    insert_pseudo_for_row(row->pseudo->after, false);
                }
            }

            DomNode* child = lam::dom_require_element(node)->first_child;
            for (; child; child = child->next_sibling) {
                if (child->is_element()) mark_table_node(lycon, child, lam::view_require_element(row));
            }
        }
    }
    else if (display.inner == CSS_VALUE_TABLE_CELL) {
        // Cell - mark with styles and attributes
        ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(set_view(lycon, RDT_VIEW_TABLE_CELL, node));
        if (cell) {
            cell->display = display;
            lycon->view = static_cast<View*>(cell);
            // save parent font context before cell resolution, so that em-based
            // font sizes in sibling cells resolve against the row/table font,
            // not the previously resolved cell's font (CSS 2.1 §4.3.2)
            FontBox saved_font = lycon->font;
            dom_node_resolve_style(node, lycon);
            parse_cell_attributes(lycon, node, cell);
            lycon->font = saved_font;
        }
    }
    else if (tag == HTM_TAG_COLGROUP || display.inner == CSS_VALUE_TABLE_COLUMN_GROUP) {
        // Column group - mark with view type and recurse to handle child columns
        // CSS 2.1 §17.2.1: Column groups don't generate cells, only provide metadata
        ViewBlock* colgroup = lam::view_require_block(set_view(lycon, RDT_VIEW_TABLE_COLUMN_GROUP, node));
        if (colgroup) {
            colgroup->display = display;
            lycon->view = static_cast<View*>(colgroup);
            // CSS 2.1 §17.3: Save font context before resolving column-group styles.
            // Column groups only influence border, background, width, visibility on cells.
            // Inherited text properties (word-spacing, letter-spacing, etc.) must not
            // leak to sibling rows/cells through the shared layout context.
            FontBox saved_font = lycon->font;
            dom_node_resolve_style(node, lycon);  // Resolve styles (background, border, width)
            // Recurse to mark child column elements
            DomNode* child = lam::dom_require_element(node)->first_child;
            for (; child; child = child->next_sibling) {
                if (child->is_element()) mark_table_node(lycon, child, lam::view_require_element(colgroup));
            }
            lycon->font = saved_font;
        }
    }
    else if (tag == HTM_TAG_COL || display.inner == CSS_VALUE_TABLE_COLUMN) {
        // Column - mark with view type
        // CSS 2.1 §17.2.1: Columns don't generate cells, only provide metadata
        ViewBlock* col = lam::view_require_block(set_view(lycon, RDT_VIEW_TABLE_COLUMN, node));
        if (col) {
            col->display = display;
            lycon->view = static_cast<View*>(col);
            // CSS 2.1 §17.3: Save font context — same rationale as column-group above
            FontBox saved_font = lycon->font;
            dom_node_resolve_style(node, lycon);  // Resolve styles (background, border, width)
            lycon->font = saved_font;
        }
    }

    // Restore context
    lycon->elmt = saved_elmt;
    lycon->font = saved_mark_font;
}

// Build table structure from DOM - simplified using unified tree architecture
ViewTable* build_table_tree(LayoutContext* lycon, DomNode* tableNode) {
    log_debug("%s Building table structure", tableNode->source_loc());

    // Use tableNode directly — lycon->view may not point to the table
    // (e.g., when table is an abs-positioned child of a grid container)
    ViewTable* table = lam::view_require<RDT_VIEW_TABLE>(tableNode);
    dom_node_resolve_style(tableNode, lycon);
    resolve_table_properties(lycon, tableNode, table);

    // CSS 2.1 Section 17.2.1: Generate anonymous table boxes BEFORE building view tree
    // This ensures proper table structure for layout regardless of HTML structure
    if (tableNode->is_element()) {
        generate_anonymous_table_boxes(lycon, tableNode->as_element());
    }

    // Recursively mark all table children with correct view types
    if (tableNode->is_element()) {
        DomNode* child = lam::dom_require_element(tableNode)->first_child;
        for (; child; child = child->next_sibling) {
            if (child->is_element()) {
                mark_table_node(lycon, child, lam::view_require_element(table));
            }
        }
    }

    log_debug("%s Table structure built successfully", tableNode->source_loc());
    return table;
}

// Apply CSS vertical-align positioning to cell content
static bool table_cell_vertical_align_skips_child(View* child) {
    ViewElement* element = lam::view_as_element(child);
    return element && layout_position_is_abs_fixed(element->position);
}

static void apply_cell_vertical_alignment(LayoutContext* lycon, ViewTableCell* tcell, float content_height) {
    if (!tcell || !tcell->td) return;

    int valign = tcell->td->vertical_align;

    // Calculate content's actual height to determine offset
    // We need to find both min and max Y to get the true content height
    float min_y = 1e9f;
    float max_y = 0;
    bool has_content = false;

    // Find the bounding box of all child content
    for_each_table_cell_vertical_align_child(lam::view_require_element(tcell), [&](View* child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            if (text->y < min_y) min_y = text->y;
            float child_bottom = text->y + text->height;
            if (child_bottom > max_y) max_y = child_bottom;
            has_content = true;
        } else if (child->view_type == RDT_VIEW_BLOCK ||
                   child->view_type == RDT_VIEW_LIST_ITEM ||
                   child->view_type == RDT_VIEW_INLINE ||
                   child->view_type == RDT_VIEW_BR) {
            ViewElement* element = lam::view_require_element(child);
            if (element->y < min_y) min_y = element->y;
            float child_bottom = element->y + element->height;
            if (child_bottom > max_y) max_y = child_bottom;
            has_content = true;
        }
    });

    if (!has_content) return;

    // Content actual height is the span from first content to last content bottom
    float content_actual_height = max_y - min_y;
    if (tcell->content_height > content_actual_height) {
        content_actual_height = tcell->content_height;
    }
    log_debug("%s Cell vertical-align: content_height=%.1f, content_actual_height=%.1f (min_y=%.1f, max_y=%.1f)", tcell->source_loc(),
             content_height, content_actual_height, min_y, max_y);

    // Calculate vertical offset based on alignment
    float vertical_offset = 0;
    switch (valign) {
        case 0: // CELL_VALIGN_TOP
            // Default - no offset needed
            vertical_offset = 0;
            break;

        case 1: // CELL_VALIGN_MIDDLE
            // Center content vertically
            if (content_height > content_actual_height) {
                vertical_offset = (content_height - content_actual_height) / 2;
            }
            break;

        case 2: // CELL_VALIGN_BOTTOM
            // Align content to bottom
            if (content_height > content_actual_height) {
                vertical_offset = content_height - content_actual_height;
            }
            break;

        case 3: // CELL_VALIGN_BASELINE
            // CSS 2.1 §17.5.4: Baseline alignment is handled at row level by
            // apply_row_baseline_alignment(), not here in per-cell alignment.
            vertical_offset = 0;
            break;
    }

    // Apply offset to all child content
    if (vertical_offset != 0.0f) {
        for_each_table_cell_vertical_align_child(lam::view_require_element(tcell), [&](View* child) {
            shift_table_cell_vertical_align_child(child, vertical_offset);
            if (child->view_type == RDT_VIEW_TEXT) {
                log_debug("%s CSS vertical-align: adjusted text Y by +%.1fpx (align=%d)", tcell->source_loc(),
                         vertical_offset, (int)valign);
            }
        });
    }
}

// Re-apply vertical alignment for rowspan cells after their final height is computed
// This is needed because rowspan cells are initially laid out with estimated height,
// but their final height is only known after all row heights are calculated
static void reapply_rowspan_vertical_alignment(ViewTableCell* tcell) {
    if (!tcell || !tcell->td) return;
    if (tcell->td->row_span <= 1) return;  // Only for rowspan cells

    int valign = tcell->td->vertical_align;
    if (valign == TableCellProp::CELL_VALIGN_TOP) return;  // No adjustment needed for top

    // Calculate the content area (cell height minus border and padding)
    float border_top = 1, border_bottom = 1;
    float padding_top = 0, padding_bottom = 0;

    if (tcell->bound) {
        if (tcell->bound->padding.top >= 0) padding_top = tcell->bound->padding.top;
        if (tcell->bound->padding.bottom >= 0) padding_bottom = tcell->bound->padding.bottom;
        if (tcell->bound->border) {
            if (tcell->bound->border->width.top >= 0) border_top = tcell->bound->border->width.top;
            if (tcell->bound->border->width.bottom >= 0) border_bottom = tcell->bound->border->width.bottom;
        }
    }

    float content_area_height = tcell->height - border_top - border_bottom - padding_top - padding_bottom;
    float content_start_y = border_top + padding_top;

    // Find content bounds (min and max Y of children)
    float content_min_y = 1e9f;
    float content_max_y = 0;
    bool has_content = false;

    for (View* child = lam::view_require_element(tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            if (text->y < content_min_y) content_min_y = text->y;
            float child_bottom = text->y + text->height;
            if (child_bottom > content_max_y) content_max_y = child_bottom;
            has_content = true;
        }
        else if (child->view_type == RDT_VIEW_BR) {
            ViewElement* element = lam::view_require_element(child);
            if (element->y < content_min_y) content_min_y = element->y;
            float child_bottom = element->y + element->height;
            if (child_bottom > content_max_y) content_max_y = child_bottom;
            has_content = true;
        }
        // Handle other child types (ViewBlock, etc.)
        else if (child->view_type >= RDT_VIEW_BLOCK) {
            ViewBlock* block = lam::view_require_block(child);
            if (block->y < content_min_y) content_min_y = block->y;
            float child_bottom = block->y + block->height;
            if (child_bottom > content_max_y) content_max_y = child_bottom;
            has_content = true;
        }
    }

    if (!has_content) return;

    float content_actual_height = content_max_y - content_min_y;

    // Calculate new vertical offset based on alignment
    float new_offset = 0;
    switch (valign) {
        case TableCellProp::CELL_VALIGN_MIDDLE:
            new_offset = content_start_y + (content_area_height - content_actual_height) / 2;
            break;
        case TableCellProp::CELL_VALIGN_BOTTOM:
            new_offset = content_start_y + content_area_height - content_actual_height;
            break;
        default:
            new_offset = content_start_y;
            break;
    }

    // Calculate the adjustment needed (new position - current position)
    float adjustment = new_offset - content_min_y;

    log_debug("Rowspan vertical-align: cell_height=%.1f, content_area=%.1f, content_height=%.1f, "
              "valign=%d, content_min_y=%.1f, new_offset=%.1f, adjustment=%.1f",
              tcell->height, content_area_height, content_actual_height,
              valign, content_min_y, new_offset, adjustment);

    if (adjustment != 0) {
        for (View* child = lam::view_require_element(tcell)->first_child; child; child = child->next_sibling) {
            if (child->view_type == RDT_VIEW_TEXT) {
                ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
                text->y += adjustment;
                for (TextRect* rect = text->rect; rect; rect = rect->next) {
                    rect->y += adjustment;
                }
            }
            else if (child->view_type == RDT_VIEW_BR) {
                ViewElement* element = lam::view_require_element(child);
                element->y += adjustment;
            }
            else if (child->view_type >= RDT_VIEW_BLOCK) {
                ViewBlock* block = lam::view_require_block(child);
                block->y += adjustment;
            }
        }
    }
}

static float table_rowspan_spanned_height(ViewTable* table, TableMetadata* meta,
                                          int start_row, int row_span) {
    if (!table || !table->tb || !meta || row_span <= 1) return 0.0f;

    int end_row = start_row + row_span;
    if (start_row < 0) start_row = 0;
    if (start_row >= meta->row_count) return 0.0f;
    if (end_row > meta->row_count) end_row = meta->row_count;
    if (end_row <= start_row) return 0.0f;

    float spanned_height = 0.0f;
    for (int r = start_row; r < end_row; r++) {
        spanned_height += meta->row_heights[r];
        if (!table->tb->border_collapse && table->tb->border_spacing_v > 0.0f && r < end_row - 1) {
            spanned_height += table->tb->border_spacing_v;
        }
    }
    return spanned_height;
}

template <typename Fn>
static void for_each_table_cell(ViewTable* table, Fn fn) {
    for_each_table_row(table, [&](ViewTableRow* row) {
        for_each_table_row_cell(row, [&](ViewTableCell* tcell) {
            fn(row, tcell);
        });
    });
}

template <typename Fn>
static void for_each_direct_table_block(ViewTable* table, Fn fn) {
    if (!table) return;
    for (View* child_view = table->first_child; child_view; child_view = child_view->next_sibling) {
        if (!child_view->is_block()) continue;
        fn(lam::view_require_block(child_view));
    }
}

template <typename Fn>
static void for_each_direct_table_row_group(ViewTable* table, Fn fn) {
    for_each_direct_table_block(table, [&](ViewBlock* child) {
        if (child->view_type != RDT_VIEW_TABLE_ROW_GROUP) return;
        fn(lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child), child);
    });
}

struct TableOrderedRowElements {
    ViewBlock* header_group;
    ViewBlock* footer_group;
    ArrayList* body_groups;
    ArrayList* ordered_elements;
};

static TableOrderedRowElements table_collect_ordered_row_elements(ViewTable* table) {
    TableOrderedRowElements result = {nullptr, nullptr, arraylist_new(8), nullptr};

    for_each_direct_table_block(table, [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            int section = group->get_section_type();
            if (section == TABLE_SECTION_THEAD && !result.header_group) {
                result.header_group = child;
            } else if (section == TABLE_SECTION_TFOOT && !result.footer_group) {
                result.footer_group = child;
            } else {
                arraylist_append(result.body_groups, child);
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            arraylist_append(result.body_groups, child);
        }
    });

    result.ordered_elements = arraylist_new(result.body_groups->length + 2);
    if (result.header_group) arraylist_append(result.ordered_elements, result.header_group);
    for (int i = 0; i < result.body_groups->length; i++) {
        arraylist_append(result.ordered_elements, result.body_groups->data[i]);
    }
    if (result.footer_group) arraylist_append(result.ordered_elements, result.footer_group);

    return result;
}

static void update_rowspan_cell_heights(ViewTable* table, TableMetadata* meta) {
    if (!table || !meta) return;

    for_each_table_cell(table, [&](ViewTableRow* row, ViewTableCell* tcell) {
            (void)row;
            if (!tcell->td || tcell->td->row_span <= 1) return;

            int start_row = tcell->td->row_index;
            int end_row = start_row + tcell->td->row_span;
            if (start_row < 0) start_row = 0;
            if (end_row > meta->row_count) end_row = meta->row_count;

            float spanned_height = table_rowspan_spanned_height(
                table, meta, tcell->td->row_index, tcell->td->row_span);
            if (spanned_height <= 0.0f) return;

            log_debug("%s Rowspan cell height update: rows %d-%d, old height=%.1f, new height=%.1f",
                      table->source_loc(), start_row, end_row - 1, tcell->height, spanned_height);

            tcell->height = spanned_height;
            reapply_rowspan_vertical_alignment(tcell);
    });
}

static float table_row_float_content_bottom(ViewBlock* row) {
    float max_float_bottom = 0.0f;
    if (!row) return max_float_bottom;
    for (View* child = row->first_child; child; child = child->next_sibling) {
        if (!child->view_type || !child->is_block()) continue;
        ViewBlock* block = lam::view_require_block(child);
        if (layout_position_is_floated(block->position)) {
            float bottom = block->y + block->height;
            if (bottom > max_float_bottom) max_float_bottom = bottom;
        }
    }
    return max_float_bottom;
}

static void table_size_float_containing_row(ViewTable* table, ViewBlock* row,
                                            float* group_height) {
    float max_float_bottom = table_row_float_content_bottom(row);
    if (max_float_bottom > 0.0f) {
        row->width = table->width;
        row->height = max_float_bottom;
        log_debug("Float-containing row sized: width=%.0f, height=%.0f",
                  row->width, row->height);
    }
    if (group_height && row->height > *group_height) {
        *group_height = row->height;
    }
}

static int table_row_metadata_index_from_row(ViewTableRow* trow, int fallback_index) {
    if (!trow) return fallback_index;
    ViewTableCell* first_cell = trow->first_cell();
    if (first_cell && first_cell->td && first_cell->td->row_index >= 0) {
        return first_cell->td->row_index;
    }
    return fallback_index;
}

static int table_row_metadata_index(ViewBlock* row, int fallback_index) {
    if (!row || row->view_type != RDT_VIEW_TABLE_ROW) return fallback_index;
    return table_row_metadata_index_from_row(
        lam::view_require<RDT_VIEW_TABLE_ROW>(row), fallback_index);
}

template <typename Fn>
static void for_each_table_body_group_row(ViewTable* table, Fn fn) {
    for_each_direct_table_row_group(table, [&](ViewTableRowGroup* group, ViewBlock* child) {
        (void)child;
        if (group->get_section_type() != TABLE_SECTION_TBODY) return;
        for_each_table_row_in_group(group, [&](ViewTableRow* row, ViewBlock* row_block) {
            (void)row_block;
            fn(group, row);
        });
    });
}

struct TableHeightSectionSummary {
    float non_body_grid_height;
    float body_natural_height;
    int body_row_count;
    int section_count;
};

static TableHeightSectionSummary table_collect_height_section_summary(ViewTable* table,
                                                                      TableMetadata* meta) {
    TableHeightSectionSummary summary = {};
    if (!table || !meta) return summary;

    log_debug("%s Calculating natural heights for each section", table->source_loc());
    for_each_direct_table_block(table, [&](ViewBlock* child) {
        if (table_view_is_caption(child)) {
            log_debug("%s   Caption height outside grid: %.1f",
                      table->source_loc(), child->height);
        } else if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            TableSectionType section_type = group->get_section_type();
            bool is_body_group = (section_type == TABLE_SECTION_TBODY);

            log_debug("%s   Row group section_type=%d (TBODY=%d), is_body=%d",
                      table->source_loc(), section_type, TABLE_SECTION_THEAD,
                      TABLE_SECTION_TBODY, is_body_group);

            float group_height = 0.0f;
            int row_count_in_group = 0;
            for_each_table_row_in_group(group, [&](ViewTableRow* row, ViewBlock* row_block) {
                (void)row_block;
                int row_idx = table_row_metadata_index_from_row(row, -1);
                if (row_idx < 0 || row_idx >= meta->row_count) return;

                float row_height = meta->row_heights[row_idx];
                group_height += row_height;
                row_count_in_group++;
                log_debug("%s     Row %d natural height: %.1f",
                          table->source_loc(), row_idx, row_height);
            });

            if (is_body_group) {
                summary.body_natural_height += group_height;
                summary.body_row_count += row_count_in_group;
                summary.section_count++;
                log_debug("%s   Body group natural height: %.1f (rows only), rows: %d",
                          table->source_loc(), group_height, row_count_in_group);
            } else {
                summary.non_body_grid_height += group_height;
                summary.section_count++;
                log_debug("%s   Header/Footer group natural height: %.1f (rows only)",
                          table->source_loc(), group_height);
            }
        }
    });
    return summary;
}

static float table_explicit_height_row_start_y(ViewTable* table, float table_border_top,
                                               float table_padding_top,
                                               ViewBlock* caption,
                                               float caption_height) {
    float y_accum = table_border_top + table_padding_top;
    if (caption && table->tb->caption_side == TableProp::CAPTION_SIDE_TOP) {
        y_accum += caption_height;
    }
    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
        y_accum += table->tb->border_spacing_v;
    }
    return y_accum;
}

static float table_explicit_height_grid_spacing(ViewTable* table, TableMetadata* meta,
                                                int section_count) {
    if (!table || !meta || table->tb->border_collapse ||
        table->tb->border_spacing_v <= 0.0f) return 0.0f;

    float total_spacing = section_count > 0
        ? (section_count + 1) * table->tb->border_spacing_v : 0.0f;
    int within_group_boundaries = meta->row_count - section_count;
    if (within_group_boundaries < 0) within_group_boundaries = 0;
    return total_spacing + within_group_boundaries * table->tb->border_spacing_v;
}

static float table_caption_positive_margin_top(ViewBlock* caption) {
    return (caption && caption->bound && caption->bound->margin.top > 0.0f)
        ? caption->bound->margin.top : 0.0f;
}

static float table_caption_positive_margin_bottom(ViewBlock* caption) {
    return (caption && caption->bound && caption->bound->margin.bottom > 0.0f)
        ? caption->bound->margin.bottom : 0.0f;
}

static float table_caption_positive_margin_left(ViewBlock* caption) {
    return (caption && caption->bound &&
            caption->bound->margin.left_type != CSS_VALUE_AUTO &&
            caption->bound->margin.left > 0.0f)
        ? caption->bound->margin.left : 0.0f;
}

static float table_caption_positive_margin_right(ViewBlock* caption) {
    return (caption && caption->bound &&
            caption->bound->margin.right_type != CSS_VALUE_AUTO &&
            caption->bound->margin.right > 0.0f)
        ? caption->bound->margin.right : 0.0f;
}

static float table_caption_positive_horizontal_margin(ViewBlock* caption) {
    return table_caption_positive_margin_left(caption) +
        table_caption_positive_margin_right(caption);
}

static float table_caption_positive_vertical_margin(ViewBlock* caption) {
    return table_caption_positive_margin_top(caption) +
        table_caption_positive_margin_bottom(caption);
}

static float table_caption_height_with_margins(ViewBlock* caption) {
    if (!caption) return 0.0f;
    float margin_v = table_caption_positive_vertical_margin(caption);
    return caption->height + margin_v;
}

template <typename Fn>
static void for_each_table_caption(ArrayList* captions, Fn fn) {
    if (!captions) return;
    for (int ci = 0; ci < captions->length; ci++) {
        fn(table_array_view_block(captions, ci), ci);
    }
}

struct TableCaptionCollection {
    ArrayList* captions;
    ViewBlock* first_caption;
    float total_height;
};

static TableCaptionCollection table_collect_captions(ViewTable* table) {
    TableCaptionCollection result = {arraylist_new(4), nullptr, 0.0f};
    for_each_direct_table_block(table, [&](ViewBlock* child) {
        if (!table_view_is_caption(child)) return;
        arraylist_append(result.captions, child);
        if (!result.first_caption) result.first_caption = child;
        if (child->height > 0.0f) {
            float margin_v = table_caption_positive_vertical_margin(child);
            result.total_height += table_caption_height_with_margins(child);
            log_debug("Caption height calculation: height(content+padding+border)=%.1f, margin_v=%.1f, total_so_far=%.1f",
                      child->height, margin_v, result.total_height);
        }
    });
    return result;
}

static void table_position_caption_with_margins(ViewBlock* caption, float base_y) {
    if (!caption) return;
    caption->x = table_caption_positive_margin_left(caption);
    caption->y = base_y + table_caption_positive_margin_top(caption);
}

enum TableCaptionWidthChangeReference {
    TABLE_CAPTION_WIDTH_REFERENCE_ADJUSTED_CAP,
    TABLE_CAPTION_WIDTH_REFERENCE_WRAPPER
};

enum TableCaptionStackSide {
    TABLE_CAPTION_STACK_TOP,
    TABLE_CAPTION_STACK_BOTTOM
};

static float table_adjust_caption_width_and_height(LayoutContext* lycon,
                                                   ViewTable* table,
                                                   ViewBlock* caption,
                                                   float table_width,
                                                   float wrapper_content_width,
                                                   int caption_index,
                                                   TableCaptionWidthChangeReference reference) {
    float old_width = caption->width;
    adjust_table_caption_width(caption, wrapper_content_width);

    float comparison_width = reference == TABLE_CAPTION_WIDTH_REFERENCE_ADJUSTED_CAP
        ? caption->width : wrapper_content_width;
    if (fabs(comparison_width - old_width) <= 0.5f) {
        return table_caption_height_with_margins(caption);
    }

    if (reference == TABLE_CAPTION_WIDTH_REFERENCE_ADJUSTED_CAP) {
        log_debug("Caption %d width changed: %.1f -> %.1f, re-laying out",
                  caption_index, old_width, caption->width);
    } else {
        log_debug("%s Bottom caption %d width changed: %.1f -> %.1f, re-laying out",
                  table->source_loc(), caption_index, old_width, table_width);
    }
    return relayout_table_caption(lycon, caption, table_width);
}

static float table_position_caption_stack(LayoutContext* lycon,
                                          ViewTable* table,
                                          ArrayList* captions,
                                          float base_y,
                                          float table_width,
                                          float wrapper_content_width,
                                          TableCaptionWidthChangeReference reference,
                                          TableCaptionStackSide side) {
    float cap_y = base_y;
    float total_height = 0.0f;
    for_each_table_caption(captions, [&](ViewBlock* cap, int ci) {
        table_position_caption_with_margins(cap, cap_y);

        float this_cap_height = table_adjust_caption_width_and_height(
            lycon, table, cap, table_width, wrapper_content_width, ci, reference);

        total_height += this_cap_height;
        cap_y += this_cap_height;
        if (side == TABLE_CAPTION_STACK_TOP) {
            log_debug("Positioned caption %d at top: y=%.1f, height=%.1f, total=%.1f",
                      ci, cap->y, cap->height, total_height);
        } else {
            log_debug("%s Positioned caption %d at bottom: y=%.1f, height=%.1f",
                      table->source_loc(), ci, cap->y, cap->height);
        }
    });
    return total_height;
}

static float table_measure_caption_width_contribution(LayoutContext* lycon,
                                                      ViewTable* table,
                                                      ViewBlock* caption) {
    if (!caption) return 0.0f;

    float contribution = 0.0f;
    if (caption->blk && caption->blk->given_width > 0.0f) {
        contribution = adjust_min_max_width(caption, caption->blk->given_width);
        log_debug("Caption has explicit CSS width: %.1fpx (after min/max clamp)", contribution);
    } else if (DomElement* caption_elem = caption->as_element()) {
        IntrinsicSizes caption_sizes = layout_measure_intrinsic_widths(
            lycon, caption_elem, "table caption");
        contribution = adjust_min_max_width(caption, ceilf(caption_sizes.min_content));
        log_debug("Caption auto width - intrinsic min-content: %.1fpx (max-content: %.1f, after min/max clamp)",
                  contribution, caption_sizes.max_content);
    }

    // Caption width is compared against the table grid content width, so convert
    // the caption margin box into that coordinate space before applying it.
    if (caption->blk && caption->blk->given_width > 0.0f &&
        !layout_uses_border_box(caption) && caption->bound) {
        BoxMetrics caption_box = layout_box_metrics(caption);
        contribution += caption_box.border_h;
        if (caption->bound->padding.left > 0.0f) contribution += caption->bound->padding.left;
        if (caption->bound->padding.right > 0.0f) contribution += caption->bound->padding.right;
    }
    if (table->bound) {
        contribution -= layout_box_metrics(table).border_h;
        if (table->bound->padding.left > 0.0f) contribution -= table->bound->padding.left;
        if (table->bound->padding.right > 0.0f) contribution -= table->bound->padding.right;
    }

    if (caption->bound) {
        float ml = table_caption_positive_margin_left(caption);
        float mr = table_caption_positive_margin_right(caption);
        if (ml + mr > 0.0f) {
            contribution += ml + mr;
            log_debug("Caption margin-box: added margins (left=%.0f, right=%.0f) -> contribution=%.1fpx",
                      ml, mr, contribution);
        }
    }
    return contribution;
}

static void table_recalculate_row_y_positions(ViewTable* table, TableMetadata* meta,
                                              float y_accum) {
    if (!table || !meta) return;
    for (int r = 0; r < meta->row_count; r++) {
        meta->row_y_positions[r] = y_accum;
        y_accum += meta->row_heights[r];
        if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
            y_accum += table->tb->border_spacing_v;
        }
    }
}

static void table_recalculate_explicit_height_row_y_positions(
    ViewTable* table, TableMetadata* meta, float table_border_top,
    float table_padding_top, ViewBlock* caption, float caption_height) {
    float y_accum = table_explicit_height_row_start_y(
        table, table_border_top, table_padding_top, caption, caption_height);
    table_recalculate_row_y_positions(table, meta, y_accum);
}

static float table_apply_explicit_height_row_extra(ViewTable* table, TableMetadata* meta,
                                                   int row_idx, float extra_height,
                                                   int eligible_row_count,
                                                   float eligible_height_total,
                                                   const char* row_label,
                                                   const char* log_indent) {
    if (!table || !meta || row_idx < 0 || row_idx >= meta->row_count ||
        eligible_row_count <= 0) return 0.0f;
    float natural_height = meta->row_heights[row_idx];
    float row_extra = eligible_height_total > 0.0f
        ? extra_height * natural_height / eligible_height_total
        : extra_height / eligible_row_count;
    meta->row_heights[row_idx] += row_extra;
    log_debug("%s%s%s row %d: natural=%.1f + extra=%.1f = %.1f",
              table->source_loc(), log_indent ? log_indent : "     ",
              row_label, row_idx, natural_height,
              row_extra, meta->row_heights[row_idx]);
    return row_extra;
}

static float table_resolve_row_explicit_height(LayoutContext* lycon, ViewTable* table,
                                               TableMetadata* meta, ViewBlock* row,
                                               int row_idx, const char* row_label) {
    if (!row || !row->is_element()) return 0.0f;
    DomElement* row_elem = row->as_element();
    if (!row_elem->specified_style) return 0.0f;

    CssDeclaration* height_decl = style_tree_get_declaration(
        row_elem->specified_style, CSS_PROPERTY_HEIGHT);
    if (!height_decl || !height_decl->value) return 0.0f;

    // CSS 2.1 §17.5.3: percentage row heights compute to auto but still
    // need to be remembered so explicit table-height distribution skips them.
    if (height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        if (row_idx >= 0 && row_idx < meta->row_count) {
            meta->row_has_percent_height[row_idx] = true;
            log_debug("%s %s %d has percentage height - treating as auto",
                      table->source_loc(), row_label, row_idx);
        }
        return 0.0f;
    }

    float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
    if (resolved_height > 0.0f) {
        log_debug("%s %s has explicit CSS height: %.1fpx",
                  table->source_loc(), row_label, resolved_height);
        return resolved_height;
    }
    return 0.0f;
}

static float table_resolve_row_group_explicit_height(LayoutContext* lycon, ViewTable* table,
                                                     ViewBlock* group_block,
                                                     bool* has_percent_height) {
    if (has_percent_height) *has_percent_height = false;
    if (!group_block || !group_block->is_element()) return 0.0f;
    DomElement* group_elem = group_block->as_element();
    if (!group_elem->specified_style) return 0.0f;

    CssDeclaration* height_decl = style_tree_get_declaration(
        group_elem->specified_style, CSS_PROPERTY_HEIGHT);
    if (!height_decl || !height_decl->value) return 0.0f;

    // CSS 2.1 row-group percentage heights compute to auto, but the rows
    // must be marked so later table-height distribution preserves that policy.
    if (height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        if (has_percent_height) *has_percent_height = true;
        log_debug("%s Row group has percentage height - all rows compute to auto",
                  table->source_loc());
        return 0.0f;
    }

    float resolved = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
    if (resolved > 0.0f) {
        log_debug("%s Row group has explicit CSS height: %.1fpx",
                  table->source_loc(), resolved);
        return resolved;
    }
    return 0.0f;
}

static void table_apply_row_group_min_height(LayoutContext* lycon, ViewTable* table,
                                             TableMetadata* meta, ViewTableRowGroup* group,
                                             ViewBlock* group_block,
                                             float explicit_group_height,
                                             float* current_y) {
    float content_group_height = group_block->height;
    if (explicit_group_height > group_block->height) {
        group_block->height = explicit_group_height;
        log_debug("%s Row group expanded to explicit height: %.1fpx (content was %.1fpx)",
                  table->source_loc(), group_block->height, content_group_height);
    }

    if (group_block->height <= content_group_height) return;

    float extra = group_block->height - content_group_height;
    *current_y += extra;
    log_debug("%s Advanced current_y by %.1f for expanded row group",
              table->source_loc(), extra);

    int eligible_rows = 0;
    for_each_table_row_in_group(group, [&](ViewTableRow* trow, ViewBlock* row) {
        (void)trow;
        if (row->height > 0.0f) eligible_rows++;
    });
    if (eligible_rows <= 0) return;

    float extra_per_row = extra / eligible_rows;
    float y_accum = 0.0f;
    for_each_table_row_in_group(group, [&](ViewTableRow* trow, ViewBlock* row) {
        if (row->height <= 0.0f) return;

        row->y = y_accum;
        row->height += extra_per_row;
        log_debug("%s Row expanded to %.1fpx (added %.1fpx)",
                  table->source_loc(), row->height, extra_per_row);

        int row_idx = table_row_metadata_index_from_row(trow, -1);
        if (row_idx >= 0 && row_idx < meta->row_count) {
            meta->row_heights[row_idx] = row->height;
        }

        update_row_cells_after_height_change(lycon, trow, row->height, false, true);

        y_accum += row->height;
        if (!table->tb->border_collapse && table->tb->border_spacing_v > 0.0f) {
            y_accum += table->tb->border_spacing_v;
        }
    });
}

static void table_track_row_metrics(ViewTable* table, TableMetadata* meta,
                                    int row_idx, float row_y,
                                    float row_height, const char* row_label) {
    if (row_idx < 0 || row_idx >= meta->row_count) return;
    meta->row_y_positions[row_idx] = row_y;
    meta->row_heights[row_idx] = row_height;
    log_debug("%s Tracking %s %d: y=%.1f, height=%.1f",
              table->source_loc(), row_label, row_idx, row_y, row_height);
}

static void table_place_collapsed_row(ViewTable* table, TableMetadata* meta,
                                      ViewTableRow* trow, float row_y,
                                      float row_width, float metadata_y,
                                      float* col_widths, float* col_x_positions,
                                      int columns, int row_idx,
                                      const char* row_label) {
    trow->x = 0.0f;
    trow->y = row_y;
    trow->width = row_width;
    trow->height = 0.0f;

    for_each_table_row_cell(trow, [&](ViewTableCell* tcell) {
        ViewBlock* cell = lam::view_require_block(tcell);
        float cell_abs_x = table_column_visual_x(table, col_widths, col_x_positions,
                                                 tcell->td->col_index,
                                                 tcell->td->col_span, columns);
        cell->x = cell_abs_x - col_x_positions[0];
        cell->y = 0.0f;
        cell->width = calculate_cell_width_from_columns(tcell, col_widths, columns);
        cell->height = 0.0f;
    });

    if (row_idx >= 0 && row_idx < meta->row_count) {
        meta->row_y_positions[row_idx] = metadata_y;
        meta->row_heights[row_idx] = 0.0f;
        log_debug("%s Collapsed %s %d: y=%.1f, height=0",
                  table->source_loc(), row_label, row_idx, metadata_y);
    }
}

static float table_measure_row_cells(LayoutContext* lycon, ViewTable* table,
                                     TableMetadata* meta, ViewTableRow* trow,
                                     float* col_widths, float* col_x_positions,
                                     int columns) {
    float row_height = 0.0f;
    for_each_table_row_cell(trow, [&](ViewTableCell* tcell) {
        float height_for_row = process_table_cell(
            lycon, tcell, table, col_widths, col_x_positions, columns,
            meta->col_edge_max_border, meta->col_collapsed, meta->col_original_widths);
        if (height_for_row > row_height) {
            row_height = height_for_row;
        }
    });
    return row_height;
}

static void table_update_row_views_from_metadata(LayoutContext* lycon, ViewTable* table,
                                                 TableMetadata* meta) {
    if (!table || !meta) return;

    for_each_direct_table_block(table, [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            float group_max_y = 0.0f;
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);

            for_each_table_row_in_group(group, [&](ViewTableRow* trow, ViewBlock* row) {
                int row_idx = table_row_metadata_index_from_row(trow, -1);
                if (row_idx < 0 || row_idx >= meta->row_count) return;

                row->height = meta->row_heights[row_idx];
                row->y = meta->row_y_positions[row_idx] - child->y;

                float row_bottom = row->y + row->height;
                if (row_bottom > group_max_y) group_max_y = row_bottom;

                update_row_cells_after_height_change(lycon, trow, row->height, true, false);
            });

            if (group_max_y > 0.0f) {
                float old_group_height = child->height;
                child->height = group_max_y;
                log_debug("%s Updated row group height from %.1f to %.1f",
                          table->source_loc(), old_group_height, child->height);
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewTableRow* trow = lam::view_require<RDT_VIEW_TABLE_ROW>(child);
            int row_idx = table_row_metadata_index_from_row(trow, -1);
            if (row_idx < 0 || row_idx >= meta->row_count) return;

            child->height = meta->row_heights[row_idx];
            child->y = meta->row_y_positions[row_idx];
            update_row_cells_after_height_change(lycon, trow, child->height, true, false);
        }
    });
}

static void table_reposition_row_groups_from_metadata(ViewTable* table, TableMetadata* meta) {
    if (!table || !meta || !table->tb) return;

    float group_y_accum = -1.0f;
    for_each_direct_table_block(table, [&](ViewBlock* child) {
        if (child->view_type != RDT_VIEW_TABLE_ROW_GROUP) return;

        if (group_y_accum >= 0.0f) {
            float old_y = child->y;
            child->y = group_y_accum;
            if (old_y != child->y) {
                log_debug("%s Repositioned row group from y=%.1f to y=%.1f",
                          table->source_loc(), old_y, child->y);
            }
        } else {
            group_y_accum = child->y;
        }

        float group_max_y = 0.0f;
        ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
        for_each_table_row_in_group(group, [&](ViewTableRow* trow, ViewBlock* row) {
            int row_idx = table_row_metadata_index_from_row(trow, -1);
            if (row_idx < 0 || row_idx >= meta->row_count) return;

            row->y = meta->row_y_positions[row_idx] - child->y;
            float row_bottom = row->y + row->height;
            if (row_bottom > group_max_y) group_max_y = row_bottom;
        });
        if (group_max_y > 0.0f) child->height = group_max_y;

        group_y_accum = child->y + child->height;
        if (!table->tb->border_collapse && table->tb->border_spacing_v > 0.0f) {
            group_y_accum += table->tb->border_spacing_v;
        }
    });
}

static void update_single_row_cells_after_height_change(LayoutContext* lycon, ViewTableRow* trow,
                                                        float row_height) {
    update_row_cells_after_height_change(lycon, trow, row_height, true, false);
}

static float reflow_table_rows_from_metadata(LayoutContext* lycon, ViewTable* table,
                                             TableMetadata* meta, ArrayList* ordered_elements,
                                             float content_area_top_y) {
    if (!table || !meta || !ordered_elements) return content_area_top_y;

    float cursor_y = content_area_top_y;
    int visual_row_index = 0;

    for (int i = 0; i < ordered_elements->length; i++) {
        ViewBlock* child = table_array_view_block(ordered_elements, i);
        if (!child) continue;

        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            float group_start_y = cursor_y;
            float group_max_y = 0.0f;
            child->y = group_start_y;
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);

            for_each_table_row_in_group(group, [&](ViewTableRow* trow, ViewBlock* row) {
                int row_idx = table_row_metadata_index(row, visual_row_index);
                if (row_idx < 0 || row_idx >= meta->row_count) return;

                float row_height = meta->row_heights[row_idx];
                bool is_collapsed = meta->row_collapsed && meta->row_collapsed[row_idx];
                meta->row_y_positions[row_idx] = cursor_y;
                row->y = cursor_y - group_start_y;
                row->height = row_height;

                update_single_row_cells_after_height_change(
                    lycon, trow, row_height);

                float row_bottom = row->y + row->height;
                if (row_bottom > group_max_y) group_max_y = row_bottom;

                cursor_y += row_height;
                visual_row_index++;
                if (!is_collapsed && !table->tb->border_collapse &&
                    table->tb->border_spacing_v > 0.0f &&
                    visual_row_index < meta->row_count) {
                    cursor_y += table->tb->border_spacing_v;
                }
            });

            child->height = group_max_y;
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            int row_idx = table_row_metadata_index(child, visual_row_index);
            if (row_idx < 0 || row_idx >= meta->row_count) continue;

            float row_height = meta->row_heights[row_idx];
            bool is_collapsed = meta->row_collapsed && meta->row_collapsed[row_idx];
            meta->row_y_positions[row_idx] = cursor_y;
            child->y = cursor_y;
            child->height = row_height;

            update_single_row_cells_after_height_change(
                lycon, lam::view_require<RDT_VIEW_TABLE_ROW>(child), row_height);

            cursor_y += row_height;
            visual_row_index++;
            if (!is_collapsed && !table->tb->border_collapse &&
                table->tb->border_spacing_v > 0.0f &&
                visual_row_index < meta->row_count) {
                cursor_y += table->tb->border_spacing_v;
            }
        }
    }

    log_debug("%s Row reflow from metadata: start=%.1f, end=%.1f, rows=%d",
              table->source_loc(), content_area_top_y, cursor_y, visual_row_index);
    return cursor_y;
}

static void table_apply_rowspan_distributed_height(LayoutContext* lycon,
                                                   ViewTable* table,
                                                   TableMetadata* meta,
                                                   ViewTableRow* trow,
                                                   ViewBlock* row,
                                                   const char* row_label) {
    int row_idx = table_row_metadata_index_from_row(trow, -1);
    if (row_idx < 0 || row_idx >= meta->row_count) return;

    float old_height = row->height;
    row->height = meta->row_heights[row_idx];
    if (row->height == old_height) return;

    log_debug("%s Updated %s %d height: %.1fpx -> %.1fpx (after rowspan distribution)",
              table->source_loc(), row_label, row_idx, old_height, row->height);
    update_row_cells_after_height_change(lycon, trow, row->height, true, true);
}

// Layout cell content with correct parent width (after cell dimensions are set)
// This is the ONLY place where cell content gets laid out (single pass)
static void table_shift_view_x(View* view, float delta_x) {
    if (!view || delta_x == 0.0f) return;
    view->x += delta_x;
}

static void align_table_cell_block_child(ViewTableCell* cell, ViewBlock* child,
                                         float content_start_x, float content_width) {
    if (!cell || !child || !cell->blk || content_width <= 0.0f) return;
    CssEnum align = cell->blk->legacy_block_align;
    if (align != CSS_VALUE_CENTER && align != CSS_VALUE_RIGHT) return;
    if (layout_block_is_out_of_flow_positioned(child)) {
        return;
    }
    if (element_has_float(child)) return;
    if (child->width >= content_width) return;

    float target_x = content_start_x;
    if (align == CSS_VALUE_RIGHT) {
        target_x = content_start_x + content_width - child->width;
    } else {
        target_x = content_start_x + (content_width - child->width) / 2.0f;
    }

    float delta_x = target_x - child->x;
    if (fabsf(delta_x) <= 0.01f) return;
    table_shift_view_x(static_cast<View*>(child), delta_x);
    log_debug("%s table-cell legacy block align shifted child by %.1f", child->source_loc(), delta_x);
}

static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell, ViewBlock* table) {
    ViewTableCell* tcell = lam::view_require<RDT_VIEW_TABLE_CELL>(cell);
    if (!tcell) return;

    // No need to clear text rectangles - this is the first and only layout pass!

    // Save layout context to restore later
    BlockContext saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    DomNode* saved_elmt = lycon->elmt;
    FontBox saved_font = lycon->font;
    View* saved_view = lycon->view;

    // CRITICAL: Set up the cell's font before laying out content
    // This ensures text uses the cell's font-size (e.g., 14px) instead of parent's (e.g., 16px)
    if (tcell->font) {
        setup_font(lycon->ui_context, &lycon->font, tcell->font);
        log_debug("%s Table cell font setup: family=%s, size=%.1f", cell->source_loc(),
            tcell->font->family ? tcell->font->family : "default", tcell->font->font_size);
    }

    // Update line_height for the new font (must be after setup_font)
    // This ensures text rect height calculation uses correct metrics for the cell's font
    setup_line_height(lycon, tcell);

    // CSS 2.1 §10.8.1: Recalculate init_ascender/init_descender/lead_y for the cell's
    // font and line-height. Without this, stale parent values cause incorrect half-leading
    // placement for text in cells with explicit line-height (e.g., line-height: 2in).
    layout_setup_block_font_metrics(lycon);

    // Check if parent table uses border-collapse
    ViewTable* parent_table = get_parent_table(tcell);
    bool border_collapse = parent_table && parent_table->tb && parent_table->tb->border_collapse;

    // Calculate cell border and padding offsets from actual CSS style
    // Content area starts AFTER border and padding
    float border_left = 0;
    float border_top = 0;
    float border_right = 0;
    float border_bottom = 0;

    // Read border widths from cell's bound style (if present)
    if (tcell->bound && tcell->bound->border) {
        if (tcell->bound->border->left_style != CSS_VALUE_NONE) {
            border_left = tcell->bound->border->width.left;
        }
        if (tcell->bound->border->top_style != CSS_VALUE_NONE) {
            border_top = tcell->bound->border->width.top;
        }
        if (tcell->bound->border->right_style != CSS_VALUE_NONE) {
            border_right = tcell->bound->border->width.right;
        }
        if (tcell->bound->border->bottom_style != CSS_VALUE_NONE) {
            border_bottom = tcell->bound->border->width.bottom;
        }
    }

    float padding_left = 0;
    float padding_right = 0;
    float padding_top = 0;
    float padding_bottom = 0;

    if (tcell->bound) {
        padding_left = tcell->bound->padding.left >= 0 ? tcell->bound->padding.left : 0;
        padding_right = tcell->bound->padding.right >= 0 ? tcell->bound->padding.right : 0;
        padding_top = tcell->bound->padding.top >= 0 ? tcell->bound->padding.top : 0;
        padding_bottom = tcell->bound->padding.bottom >= 0 ? tcell->bound->padding.bottom : 0;
    }

    // In border-collapse mode, the cell width is the content width (column width),
    // and borders are shared/collapsed with adjacent cells. Content starts at padding only.
    // In separate mode, borders are part of the cell box and must be subtracted.
    float content_start_x, content_start_y;
    float content_width, content_height;

    if (border_collapse) {
        // Border-collapse: cell->width = content + padding + half_left_border + half_right_border
        // (col_widths includes both half-borders added as floats in column width measurement).
        // cell->x is at the center of the left collapsed border (i.e., halfway through).
        // Content area starts after the inner half-border and padding.
        //
        // IMPORTANT: Use the same float half-border values used during column width measurement
        // to avoid rounding-up errors that would reduce content_width below the measured minimum.
        // Column widths are computed as: min_text_width + half_left_float + half_right_float.
        // If we round UP the halves here (e.g., 1.5 -> 2 each), we'd subtract 4 but the column
        // only reserved 3 for borders, causing text to wrap unexpectedly.
        float half_left_f  = tcell->td->left_resolved   ? tcell->td->left_resolved->width   / 2.0f : 0.0f;
        float half_top_f   = tcell->td->top_resolved    ? tcell->td->top_resolved->width    / 2.0f : 0.0f;
        float half_right_f = tcell->td->right_resolved  ? tcell->td->right_resolved->width  / 2.0f : 0.0f;
        float half_bot_f   = tcell->td->bottom_resolved ? tcell->td->bottom_resolved->width / 2.0f : 0.0f;
        // Floor the left/top start (don't overshoot into border on the start side)
        float half_left   = half_left_f;
        float half_top    = half_top_f;
        float half_right  = half_right_f;
        float half_bottom = half_bot_f;
        content_start_x = half_left + padding_left;
        content_start_y = half_top + padding_top;
        // Compute line.right from cell->width minus right-side deductions (avoid double rounding):
        // cell->width was built as text_width + half_left_f + half_right_f, so subtracting
        // the same floats recovers the original text_width.
        float line_right_x = cell->width - half_right_f - padding_right;
        float line_right_y = cell->height - half_bot_f - padding_bottom;
        content_width  = line_right_x - content_start_x;
        content_height = line_right_y - content_start_y;
        log_debug("%s Border-collapse cell content: cell=%.1fx%.1f, half_borders=(%.1f,%.1f,%.1f,%.1f), padding=(%.1f,%.1f,%.1f,%.1f), content_start=(%.1f,%.1f), content=%.1fx%.1f", cell->source_loc(),
            cell->width, cell->height, half_left, half_top, half_right, half_bottom,
            padding_left, padding_right, padding_top, padding_bottom,
            content_start_x, content_start_y, content_width, content_height);
    } else {
        // Separate borders: subtract borders from cell dimensions
        content_start_x = border_left + padding_left;
        content_start_y = border_top + padding_top;
        content_width = cell->width - border_left - border_right - padding_left - padding_right;
        content_height = cell->height - border_top - border_bottom - padding_top - padding_bottom;
        log_debug("%s Separate-borders cell content: cell=%.1fx%.1f, border=(%.1f,%.1f), padding=(%.1f,%.1f,%.1f,%.1f), content_start=(%.1f,%.1f), content=%.1fx%.1f", cell->source_loc(),
            cell->width, cell->height, border_left, border_top,
            padding_left, padding_right, padding_top, padding_bottom,
            content_start_x, content_start_y, content_width, content_height);
    }

    // Ensure non-negative dimensions
    if (content_width < 0) content_width = 0;
    if (content_height < 0) content_height = 0;

    // Set up layout context for cell content with CORRECT positioning
    // CRITICAL FIX: Set line.left and advance_x to content_start_x to apply padding offset
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;

    // CSS 2.2 §10.5: If the cell has an explicit CSS height, set given_height in the
    // block context so children with percentage heights can resolve against it.
    // This only sets given_height (for % resolution via resolve_length_value's
    // given_height fallback), NOT content_height (which affects available_space).
    // Reset first: prevent leaked values from prior cell/style resolution
    lycon->block.given_height = -1;
    if (tcell->is_element()) {
        DomElement* cell_elem = tcell->as_element();
        if (cell_elem->specified_style) {
            CssDeclaration* h_decl = style_tree_get_declaration(
                cell_elem->specified_style, CSS_PROPERTY_HEIGHT);
            if (h_decl && h_decl->value && h_decl->value->type != CSS_VALUE_TYPE_PERCENTAGE) {
                float explicit_h = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, h_decl->value);
                if (explicit_h > 0) {
                    lycon->block.given_height = explicit_h;
                    log_debug("%s [TABLE CELL] Set given_height=%.1f for %% resolution", cell->source_loc(), explicit_h);
                }
            }
        }
    }
    // CSS Tables: If the cell has no explicit non-% height, but the table has
    // an explicit non-% height, the cell's content height is definite (from
    // table height distribution). Compute expected cell content height from
    // the table's height, since cell->height is 0 at this point (not yet sized).
    if (lycon->block.given_height < 0 && table && table->blk && table->blk->given_height > 0) {
        float table_h = table->blk->given_height;
        // Adjust for border-box: subtract table borders
        if (layout_uses_border_box(table) && table->bound && table->bound->border) {
            table_h -= layout_box_metrics(table).border_v;
        }
        // Subtract vertical border-spacing (top + bottom)
        if (table->tb && !table->tb->border_collapse && table->tb->border_spacing_v > 0) {
            table_h -= table->tb->border_spacing_v * 2;
        }
        // For single-row: cell height = table content height
        // Subtract cell border and padding to get cell content height
        float cell_content_h = table_h - border_top - border_bottom - padding_top - padding_bottom;
        if (cell_content_h > 0) {
            lycon->block.given_height = cell_content_h;
            log_debug("%s [TABLE CELL] Set given_height=%.1f from table explicit height %.1f", cell->source_loc(), cell_content_h, table->blk->given_height);
        }
    }

    lycon->block.advance_y = content_start_y;  // Start Y position after border+padding
    lycon->line.left = content_start_x;        // Text starts after padding!
    lycon->line.right = content_start_x + content_width;  // Text ends before right padding
    lycon->line.advance_x = content_start_x;   // Start advancing from padding offset
    lycon->line.is_line_start = true;
    lycon->line.start_view = NULL;  // Reset start_view so new text nodes become start of line
    lycon->elmt = tcell;

    // Propagate text-align from cell (e.g., TH has text-align: center by default)
    if (tcell->blk && tcell->blk->text_align) {
        lycon->block.text_align = tcell->blk->text_align;
        log_debug("%s Table cell text-align: %d", cell->source_loc(), tcell->blk->text_align);
    }
    // CSS 2.1 §9.2.1: Propagate direction from cell (inherited from row-group/row/table)
    if (tcell->blk && tcell->blk->direction) {
        lycon->block.direction = tcell->blk->direction;
        log_debug("%s Table cell direction: %d", cell->source_loc(), tcell->blk->direction);
    }
    // CSS 2.1 §16.1: Propagate text-indent from cell for first-line indentation
    if (tcell->blk) {
        if (!isnan(tcell->blk->text_indent_percent)) {
            lycon->block.text_indent = content_width * tcell->blk->text_indent_percent / 100.0f;
        } else {
            lycon->block.text_indent = tcell->blk->text_indent;
        }
        if (lycon->block.text_indent != 0.0f) {
            // Apply text-indent to the first line directly since cell setup
            // does not go through line_reset() / line_init()
            lycon->line.advance_x += lycon->block.text_indent;
            lycon->line.effective_left = lycon->line.left + lycon->block.text_indent;
            lycon->block.is_first_line = false;  // consumed for this line
            log_debug("%s Table cell text-indent: %.1f, advance_x=%.1f", cell->source_loc(),
                      lycon->block.text_indent, lycon->line.advance_x);
        }
    }

    log_debug("%s Layout cell content - cell=%.1fx%.1f, border=(%.1f,%.1f), padding=(%.1f,%.1f,%.1f,%.1f), content_start=(%.1f,%.1f), content=%.1fx%.1f", cell->source_loc(),
        cell->width, cell->height, border_left, border_top,
        padding_left, padding_right, padding_top, padding_bottom,
        content_start_x, content_start_y, content_width, content_height);

    // Layout children with correct parent width
    // NOTE: Do NOT call dom_node_resolve_style here before layout_flow_node.
    // The styles will be resolved properly inside layout_block, which creates
    // the ViewBlock first and then resolves CSS styles. Calling it here would
    // mark styles_resolved=true prematurely, causing layout_block to skip
    // resolution and lose the given_width/given_height values.

    // Generate ::before and ::after pseudo-elements for table cells
    if (tcell->is_element()) {
        tcell->pseudo = alloc_pseudo_content_prop(lycon, tcell);
        generate_pseudo_element_content(lycon, tcell, true);   // ::before
        generate_pseudo_element_content(lycon, tcell, false);  // ::after
        if (tcell->pseudo) {
            if (tcell->pseudo->before) {
                insert_pseudo_into_dom(lam::dom_require<DOM_NODE_ELEMENT>(tcell), tcell->pseudo->before, true);
            }
            if (tcell->pseudo->after) {
                insert_pseudo_into_dom(lam::dom_require<DOM_NODE_ELEMENT>(tcell), tcell->pseudo->after, false);
            }
        }
    }

    if (tcell->is_element()) {
        DomElement* cell_elem = lam::dom_require_element(tcell);
        if (cell_elem && wrap_orphaned_table_children(lycon, cell_elem)) {
            log_debug("%s [TABLE CELL] Wrapped orphaned table-internal content", cell->source_loc());
        }

        DomNode* cc = lam::dom_require_element(tcell)->first_child;
        for (; cc; cc = cc->next_sibling) {
            uintptr_t child_tag = cc->tag();
            if (child_tag == HTM_TAG_IMG) {
                log_debug("%s [TABLE CELL IMG] Found IMG child in table cell, calling layout_flow_node: %s", cell->source_loc(), cc->node_name());
            }
            layout_flow_node(lycon, cc);
            if (cc->is_element()) {
                ViewBlock* child_block = lam::view_as_block(static_cast<View*>(cc->as_element()));
                if (child_block && (child_block->display.outer == CSS_VALUE_BLOCK ||
                                    child_block->display.inner == CSS_VALUE_TABLE)) {
                    align_table_cell_block_child(tcell, child_block, content_start_x, content_width);
                }
            }
        }
    }

    // CSS 2.1 §10.8.1: Final line break after all cell content.
    // This applies vertical alignment (half-leading) and horizontal alignment
    // for the last line of text, matching the behavior in layout_block_content().
    if (!lycon->line.is_line_start) {
        lycon->line.is_last_line = true;
        line_break(lycon);
    } else {
        line_align(lycon);
    }
    cell->content_height = lycon->block.advance_y - content_start_y;
    if (cell->content_height < 0.0f) cell->content_height = 0.0f;

    // Apply CSS vertical-align positioning after content layout
    apply_cell_vertical_alignment(lycon, tcell, content_height);

    // Restore layout context
    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->elmt = saved_elmt;
    lycon->font = saved_font;
    lycon->view = saved_view;
}

// Helper: Check if whitespace should be collapsed for this element
// CSS white-space: normal, nowrap -> collapse whitespace
// CSS white-space: pre, pre-wrap, pre-line, break-spaces -> preserve whitespace
// Checks the cell's own white-space property first, then falls back to inherited value
static bool should_collapse_whitespace(ViewTableCell* cell) {
    if (!cell) return true; // Default to collapse

    // First check the cell's own resolved white-space property
    DomElement* elem = cell->as_element();
    if (elem && elem->blk && elem->blk->white_space != 0) {
        CssEnum ws = elem->blk->white_space;
        // Check for preserve-whitespace values
        if (ws == CSS_VALUE_PRE ||
            ws == CSS_VALUE_PRE_WRAP ||
            ws == CSS_VALUE_PRE_LINE ||
            ws == CSS_VALUE_BREAK_SPACES) {
            return false;
        }
        // Explicit normal/nowrap means collapse
        if (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP) {
            return true;
        }
    }

    // Fall back to get_white_space_value which walks up from parent
    // We pass the cell itself - get_white_space_value starts from node->parent
    CssEnum ws_value = get_white_space_value(static_cast<DomNode*>(cell));

    // These values preserve whitespace (don't collapse)
    if (ws_value == CSS_VALUE_PRE ||
        ws_value == CSS_VALUE_PRE_WRAP ||
        ws_value == CSS_VALUE_PRE_LINE ||
        ws_value == CSS_VALUE_BREAK_SPACES) {
        return false;
    }

    return true; // Default: collapse whitespace (normal, nowrap)
}

// Helper: Check if wrapping is suppressed for this cell
// CSS white-space: nowrap, pre -> no line break opportunities (min-content = max-content)
// CSS white-space: normal, pre-wrap, pre-line, break-spaces -> wrapping allowed
static bool should_prevent_wrapping(ViewTableCell* cell) {
    if (!cell) return false;

    // Check the cell's own resolved white-space property
    DomElement* elem = cell->as_element();
    if (elem && elem->blk && elem->blk->white_space != 0) {
        CssEnum ws = elem->blk->white_space;
        return (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE);
    }

    // Fall back to inherited value
    CssEnum ws_value = get_white_space_value(static_cast<DomNode*>(cell));
    return (ws_value == CSS_VALUE_NOWRAP || ws_value == CSS_VALUE_PRE);
}

// Helper: Check if text is all whitespace
static bool is_all_whitespace(const char* text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f') {
            return false;
        }
    }
    return true;
}

// Result structure for consolidated width measurement
struct CellWidths {
    float min_width;  // Minimum content width (MCW) - narrowest without overflow
    float max_width;  // Maximum content width (PCW) - preferred content width
};

static float table_cell_width_constraint_border_box(ViewTableCell* cell, float css_width,
                                                    bool border_collapse) {
    if (!cell || !cell->blk || css_width < 0.0f) return css_width;

    if (layout_uses_border_box(cell)) return css_width;

    BoxMetrics box = layout_box_metrics(cell);
    float border_horizontal = border_collapse ? 0.0f : box.border_h;
    return css_width + box.padding_h + border_horizontal;
}

static void apply_table_cell_width_constraints(ViewTableCell* cell, bool border_collapse,
                                               CellWidths* widths) {
    if (!cell || !cell->blk || !widths) return;

    // measure_cell_widths() returns border-box contributions. Convert CSS
    // content-box constraints before comparing, and let min-width override
    // max-width as CSS 2.1 used-width rules require.
    if (cell->blk->given_max_width >= 0.0f) {
        float max_border_box = table_cell_width_constraint_border_box(
            cell, cell->blk->given_max_width, border_collapse);
        if (widths->min_width > max_border_box) widths->min_width = max_border_box;
        if (widths->max_width > max_border_box) widths->max_width = max_border_box;
    }

    if (cell->blk->given_min_width >= 0.0f) {
        float min_border_box = table_cell_width_constraint_border_box(
            cell, cell->blk->given_min_width, border_collapse);
        if (widths->min_width < min_border_box) widths->min_width = min_border_box;
        if (widths->max_width < min_border_box) widths->max_width = min_border_box;
    }
}

// Check if an inline element's last text descendant ends with whitespace.
// CSS 2.1: When consecutive inline elements flow on the same line, trailing whitespace
// in one element serves as inter-word spacing before the next element. This is needed
// for accurate max-content width calculation in anonymous table cells where inter-element
// whitespace text nodes may not be present.
static bool element_text_ends_with_whitespace(DomNode* element) {
    if (!element || !element->is_element()) return false;

    // Walk last children depth-first to find the last text node
    DomNode* node = element->as_element()->last_child;
    while (node) {
        if (node->is_text()) {
            const char* text = (const char*)node->text_data();
            if (text) {
                size_t len = strlen(text);
                if (len > 0) {
                    unsigned char last_char = (unsigned char)text[len - 1];
                    return (last_char == ' ' || last_char == '\t' || last_char == '\n' ||
                            last_char == '\r' || last_char == '\f');
                }
            }
            return false;
        }
        if (node->is_element() && node->as_element()->last_child) {
            node = node->as_element()->last_child;
        } else {
            return false;
        }
    }
    return false;
}

// Check if an inline element's first text descendant starts with whitespace.
static bool element_text_starts_with_whitespace(DomNode* element) {
    if (!element || !element->is_element()) return false;

    DomNode* node = element->as_element()->first_child;
    while (node) {
        if (node->is_text()) {
            const char* text = (const char*)node->text_data();
            if (text && *text) {
                unsigned char first_char = (unsigned char)text[0];
                return (first_char == ' ' || first_char == '\t' || first_char == '\n' ||
                        first_char == '\r' || first_char == '\f');
            }
            return false;
        }
        if (node->is_element() && node->as_element()->first_child) {
            node = node->as_element()->first_child;
        } else {
            return false;
        }
    }
    return false;
}

static float table_intrinsic_child_horizontal_margin(LayoutContext* lycon,
                                                    DomElement* child_elem,
                                                    bool include_shorthand) {
    if (!child_elem) return 0.0f;
    float margin_h = 0.0f;
    if (child_elem->bound) {
        if (child_elem->bound->margin.left_type != CSS_VALUE_AUTO) {
            margin_h += child_elem->bound->margin.left;
        }
        if (child_elem->bound->margin.right_type != CSS_VALUE_AUTO) {
            margin_h += child_elem->bound->margin.right;
        }
        return margin_h;
    }
    if (!child_elem->specified_style) return 0.0f;

    CssDeclaration* ml = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN_LEFT);
    if (ml && ml->value && ml->value->type == CSS_VALUE_TYPE_LENGTH) {
        margin_h += resolve_length_value(lycon, CSS_PROPERTY_MARGIN_LEFT, ml->value);
    }
    CssDeclaration* mr = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN_RIGHT);
    if (mr && mr->value && mr->value->type == CSS_VALUE_TYPE_LENGTH) {
        margin_h += resolve_length_value(lycon, CSS_PROPERTY_MARGIN_RIGHT, mr->value);
    }
    if (!include_shorthand || margin_h != 0.0f) return margin_h;

    CssDeclaration* m = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN);
    if (!m || !m->value) return margin_h;
    if (m->value->type == CSS_VALUE_TYPE_LENGTH) {
        return 2.0f * resolve_length_value(lycon, CSS_PROPERTY_MARGIN, m->value);
    }
    if (m->value->type == CSS_VALUE_TYPE_LIST) {
        const CssValue* ml_value = css_box_shorthand_side_value(m->value, 3);
        const CssValue* mr_value = css_box_shorthand_side_value(m->value, 1);
        float ml_resolved = ml_value ? resolve_length_value(lycon, CSS_PROPERTY_MARGIN_LEFT, ml_value) : 0.0f;
        float mr_resolved = mr_value ? resolve_length_value(lycon, CSS_PROPERTY_MARGIN_RIGHT, mr_value) : 0.0f;
        return ml_resolved + mr_resolved;
    }
    return margin_h;
}

static bool table_element_is_floated(DomElement* element) {
    if (!element) return false;

    if (element->position) {
        if (layout_position_is_abs_fixed(element->position)) {
            return false;
        }
        return layout_position_is_floated(element->position);
    }

    if (!element->specified_style) return false;

    CssDeclaration* position_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_POSITION);
    if (position_decl && position_decl->value &&
        position_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum position = position_decl->value->data.keyword;
        if (position == CSS_VALUE_ABSOLUTE || position == CSS_VALUE_FIXED) {
            return false;
        }
    }

    CssDeclaration* float_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_FLOAT);
    if (!float_decl || !float_decl->value ||
        float_decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return false;
    }
    CssEnum float_value = float_decl->value->data.keyword;
    return float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT;
}

// Measure cell's minimum and maximum content widths in single pass
// This performs accurate measurement using font metrics for CSS 2.1 compliance
// CONSOLIDATED: Combines previous measure_cell_intrinsic_width() and measure_cell_minimum_width()
// border_collapse: if true, don't add cell border to width (CSS 2.1 border-collapse model)
static CellWidths measure_cell_widths(LayoutContext* lycon, ViewTableCell* cell, bool border_collapse = false) {
    CellWidths result = {0.0f, 0.0f};
    if (!cell || !cell->is_element()) return result;

    DomElement* cell_elem = cell->as_element();

    // CSS 2.1 §16.5: Resolve inherited text-transform for cell text measurement
    CssEnum cell_text_transform = get_element_text_transform(cell_elem);

    // CSS 2.1 §15.8: Resolve inherited font-variant for cell text measurement
    CssEnum cell_font_variant = get_element_font_variant(cell_elem);

    // Check if the cell will have pseudo-element generated content (::before/::after)
    // Note: at measurement time, pseudo elements haven't been generated yet,
    // so we check the CSS styles directly via dom_element_has_before/after_content()
    bool has_pseudo_before = dom_element_has_before_content(cell_elem);
    bool has_pseudo_after = dom_element_has_after_content(cell_elem);
    bool has_pseudo_content = has_pseudo_before || has_pseudo_after;

    // CSS 2.1 §17.5.2.2: For truly empty cells (no DOM children and no pseudo content),
    // intrinsic widths are determined by padding and border only (content width = 0).
    if (!cell_elem->first_child && !has_pseudo_content) {
        BoxMetrics box = layout_box_metrics(cell);
        float padding_horizontal = box.padding_h;
        float border_horizontal = border_collapse ? 0.0f : box.border_h;
        result.min_width = padding_horizontal + border_horizontal;
        result.max_width = padding_horizontal + border_horizontal;

        apply_table_cell_width_constraints(cell, border_collapse, &result);
        return result;
    }

    // Set up CSS 2.1 measurement context with infinite width
    radiant::LayoutMeasureScope measure_scope(lycon, cell);

    // Apply the cell's CSS font properties for accurate measurement
    if (cell->font) {
        log_debug("%s PCW measurement: using cell font family=%s, size=%.1f", cell->source_loc(),
            cell->font->family ? cell->font->family : "default", cell->font->font_size);
        setup_font(lycon->ui_context, &lycon->font, cell->font);
    } else {
        log_debug("%s PCW measurement: using context font (no cell-specific font)", cell->source_loc());
    }

    // CSS 2.1: Infinite width for preferred content width (no line wrapping)
    lycon->block.content_width = 10000.0f;
    lycon->block.content_height = 10000.0f;
    lycon->block.advance_y = 0;
    lycon->line.left = 0;
    lycon->line.right = 10000.0f;
    lycon->line.advance_x = 0;
    lycon->line.is_line_start = true;

    float max_width = 0.0f;
    float min_width = 0.0f;

    // Check if we should collapse whitespace based on CSS white-space property
    bool collapse_ws = should_collapse_whitespace(cell);

    // Get overflow-wrap from cell or ancestors (inherited property)
    // CSS Text 3 §5.2: word-break: break-word behaves as overflow-wrap: anywhere
    CssEnum cell_overflow_wrap = CSS_VALUE_NORMAL;
    {
        DomNode* n = static_cast<DomNode*>(cell);
        while (n) {
            if (n->is_element()) {
                DomElement* el = lam::dom_require<DOM_NODE_ELEMENT>(n);
                if (el->blk) {
                    if (el->blk->overflow_wrap != 0) {
                        cell_overflow_wrap = el->blk->overflow_wrap;
                        break;
                    }
                    if (el->blk->word_break == CSS_VALUE_BREAK_WORD) {
                        cell_overflow_wrap = CSS_VALUE_ANYWHERE;
                        break;
                    }
                }
            }
            n = n->parent;
        }
    }

    // CSS 2.1: For inline content, consecutive text nodes flow on the same line.
    // We track "inline run width" - the accumulated max-content width of consecutive
    // inline/text children that would flow together on one line.
    // For PCW (max-content): sum widths of consecutive inline children
    // For MCW (min-content): take max of individual word widths
    float inline_run_max = 0.0f;  // Running sum for current inline sequence
    float float_run_max = 0.0f;   // Sum of side-by-side floats in the current run
    float float_run_min = 0.0f;   // Widest float in the current run
    bool has_inline_content = false;  // Track if we have any inline content
    bool prev_ended_with_space = false;  // Track whitespace between text nodes

    // CSS 2.1 §16.1: text-indent applies to the first formatted line of a block
    // container. Add the cell's text-indent to the first inline run width.
    // Percentage text-indent cannot be resolved during intrinsic measurement
    // (circular dependency with table width), so only fixed lengths are used.
    float cell_text_indent = 0.0f;
    if (cell->blk && cell->blk->text_indent != 0.0f && isnan(cell->blk->text_indent_percent)) {
        cell_text_indent = cell->blk->text_indent;
    }

    // Measure each child's natural width
    for (DomNode* child = cell_elem->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            // Use unified text measurement from intrinsic_sizing.hpp
            const unsigned char* text = child->text_data();
            if (text && *text) {
                size_t text_len = strlen((const char*)text);

                const char* measure_text = (const char*)text;
                size_t measure_len = text_len;
                static char normalized_buffer[4096];  // LARGE_ARRAY_OK: static buffer — not on call stack.

                // Track if original text has leading/trailing whitespace (before normalization)
                bool original_has_leading_ws = (text_len > 0 && is_all_whitespace((const char*)text, 1));
                bool original_has_trailing_ws = false;
                if (text_len > 0) {
                    const char* end = (const char*)text + text_len - 1;
                    while (end >= (const char*)text && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
                        original_has_trailing_ws = true;
                        end--;
                    }
                }

                if (collapse_ws) {
                    // Check if all whitespace first (fast path)
                    if (is_all_whitespace((const char*)text, text_len)) {
                        // Whitespace-only text contributes a space between adjacent text nodes
                        prev_ended_with_space = true;
                        continue; // Skip whitespace-only text nodes
                    }
                    // Normalize whitespace to buffer
                    size_t normalized_len = layout_normalize_collapsible_whitespace(
                        (const char*)text, text_len, normalized_buffer, sizeof(normalized_buffer));
                    log_debug("%s Cell width measuring text: '%s' -> normalized: '%s'", cell->source_loc(), text, normalized_buffer);
                    if (normalized_len == 0) continue; // Skip if normalized to nothing
                    measure_text = normalized_buffer;
                    measure_len = normalized_len;
                }

                // Use unified intrinsic sizing API - measures both widths in one call
                TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
                    lycon, measure_text, measure_len, cell_text_transform, cell_font_variant,
                    CSS_VALUE_NORMAL, cell_overflow_wrap, CSS_VALUE_NORMAL, "table cell text");

                float text_max = (float)widths.max_content;  // PCW (max-content)
                float text_min = (float)widths.min_content;  // MCW (min-content)
                log_debug("%s Cell widths: text max=%.2f, min=%.2f (unified API)", cell->source_loc(), text_max, text_min);

                // Add space width if there was whitespace between this and previous text
                // This handles: "text1 " + "text2" OR "text1" + " text2" OR "text1 " + " text2"
                if (collapse_ws && has_inline_content && (prev_ended_with_space || original_has_leading_ws) && lycon->font.style) {
                    inline_run_max += lycon->font.style->space_width;
                    log_debug("%s Cell widths: adding inter-text space width=%.2f", cell->source_loc(), lycon->font.style->space_width);
                }

                // Accumulate max-content for inline run (consecutive text flows together)
                inline_run_max += text_max;
                has_inline_content = true;

                // For min-content, take the max of all word widths
                if (text_min > min_width) min_width = text_min;

                // Check if ORIGINAL text ended with whitespace (for next text node)
                prev_ended_with_space = original_has_trailing_ws;
            }
        }
        else if (child->is_element()) {
            // For nested block/inline elements, check for explicit CSS width first
            DomElement* child_elem = child->as_element();

            // Use unified intrinsic sizing API for ALL element types
            // This properly handles explicit CSS widths (with border/padding),
            // block/inline elements, floats, replaced elements, etc. Floats are
            // out of normal flow vertically, but they still contribute to the
            // intrinsic width of the block formatting context that contains them.
            IntrinsicSizes child_sizes = layout_measure_intrinsic_widths(lycon, child_elem, "table cell child");
            float child_max = child_sizes.max_content;
            float child_min = child_sizes.min_content;
            float child_unresolved_box_extra = layout_unresolved_html_cell_horizontal_box_extra(child_elem);
            if (child_unresolved_box_extra > 0.0f) {
                child_max += child_unresolved_box_extra;
                child_min += child_unresolved_box_extra;
                log_debug("%s Cell widths: element %s unresolved html cell box extra=%.1f",
                          cell->source_loc(), child->node_name(), child_unresolved_box_extra);
            }
            log_debug("%s Cell widths: element %s min=%.1f, max=%.1f", cell->source_loc(),
                      child->node_name(), child_min, child_max);

            // Check if this is an inline element (flows with text) or block element (starts new line)
            DisplayValue child_display = resolve_display_value(child);
            bool is_inline = (child_display.outer == CSS_VALUE_INLINE ||
                              child_display.outer == CSS_VALUE_INLINE_BLOCK);
            bool child_is_float = table_element_is_floated(child_elem);

            // Special handling for <br> - it breaks the inline run even though it's inline
            uintptr_t child_tag = child->tag();
            bool is_line_break = (child_tag == HTM_TAG_BR);

            if (is_line_break) {
                // <br> forces a line break - finalize current inline run
                float line_max = inline_run_max + float_run_max;
                if (line_max > max_width) max_width = line_max;
                if (float_run_min > min_width) min_width = float_run_min;
                inline_run_max = 0.0f;
                float_run_max = 0.0f;
                float_run_min = 0.0f;
                has_inline_content = false;
                prev_ended_with_space = false;
                log_debug("%s Cell widths: <br> breaks inline run, max_width now=%.2f", cell->source_loc(), max_width);
            } else if (is_inline) {
                // Inline elements flow with text - add to inline run
                // CSS 2.1: Account for whitespace between inline elements.
                // When the previous inline content ended with whitespace OR this element
                // starts with whitespace, add inter-word spacing.
                bool starts_with_ws = element_text_starts_with_whitespace(child);
                if (collapse_ws && has_inline_content && (prev_ended_with_space || starts_with_ws) && lycon->font.style) {
                    inline_run_max += lycon->font.style->space_width;
                    log_debug("%s Cell widths: adding inter-element space width=%.2f", cell->source_loc(), lycon->font.style->space_width);
                }
                // CSS 2.1: inline element horizontal margins contribute to line box width
                float inline_margin_h = table_intrinsic_child_horizontal_margin(
                    lycon, child_elem, false);
                if (!child_elem->bound && !child_elem->specified_style) {
                    // No resolved styles yet (measurement pass before layout).
                    // Apply UA default margins for known form controls to get accurate sizing.
                    // These mirror the values set in resolve_htm_style.cpp for checkbox/radio.
                    uintptr_t ctag = child_elem->tag();
                    if (ctag == HTM_TAG_INPUT) {
                        const char* inp_type = child_elem->get_attribute("type");
                        if (inp_type && strcmp(inp_type, "radio") == 0) {
                            inline_margin_h = FormDefaults::RADIO_MARGIN_LEFT + FormDefaults::RADIO_MARGIN_RIGHT;
                        } else if (inp_type && strcmp(inp_type, "checkbox") == 0) {
                            inline_margin_h = FormDefaults::CHECKBOX_MARGIN_LEFT + FormDefaults::CHECKBOX_MARGIN_RIGHT;
                        }
                    }
                }
                inline_run_max += child_max + inline_margin_h;
                child_min += inline_margin_h;
                if (child_min < 0) child_min = 0;
                has_inline_content = true;
                // Track if this element's text ends with whitespace for next sibling
                prev_ended_with_space = element_text_ends_with_whitespace(child);
            } else {
                // Block elements break the inline run - finalize current run first
                if (!child_is_float) {
                    float line_max = inline_run_max + float_run_max;
                    if (line_max > max_width) max_width = line_max;
                    if (float_run_min > min_width) min_width = float_run_min;
                    inline_run_max = 0.0f;
                    float_run_max = 0.0f;
                    float_run_min = 0.0f;
                    has_inline_content = false;
                    prev_ended_with_space = false;
                }

                // Block element: account for horizontal margins (including negative)
                float margin_h = table_intrinsic_child_horizontal_margin(
                    lycon, child_elem, true);
                float block_outer_max = child_max + margin_h;
                if (block_outer_max < 0) block_outer_max = 0;
                float block_outer_min = child_min + margin_h;
                if (block_outer_min < 0) block_outer_min = 0;

                if (child_is_float) {
                    // CSS Sizing 3 §5: at max-content, floats are placed as high
                    // as possible and can sit side by side; at min-content they
                    // stack, so the widest float controls.
                    float rounded_child = ceilf(block_outer_max * 2.0f) / 2.0f;
                    float_run_max += rounded_child;
                    if (block_outer_min > float_run_min) float_run_min = block_outer_min;
                    child_min = block_outer_min;
                    log_debug("%s Cell widths: float element %s run_max=%.1f, run_min=%.1f",
                              cell->source_loc(), child->node_name(), float_run_max, float_run_min);
                } else {
                    // Block element width is compared independently
                    if (block_outer_max > max_width) max_width = block_outer_max;
                    child_min = block_outer_min;
                }
            }

            if (child_min > min_width) min_width = child_min;
        }
    }

    // CSS 2.1 §12.2: Account for ::before/::after pseudo-element generated content
    // At measurement time, pseudo elements haven't been generated yet, so we
    // get content directly from CSS styles via dom_element_get_pseudo_element_content()
    if (has_pseudo_content) {
        for (int p = 0; p < 2; p++) {
            bool is_before = (p == 0);
            if ((is_before && !has_pseudo_before) || (!is_before && !has_pseudo_after)) continue;

            const char* content = nullptr;
            if (lycon->counter_context) {
                content = dom_element_get_pseudo_element_content_with_counters(
                    cell_elem, is_before ? PSEUDO_ELEMENT_BEFORE : PSEUDO_ELEMENT_AFTER,
                    lycon->counter_context, lycon->doc ? lycon->doc->arena : nullptr);
            }
            if (!content) {
                content = dom_element_get_pseudo_element_content(
                    cell_elem, is_before ? PSEUDO_ELEMENT_BEFORE : PSEUDO_ELEMENT_AFTER);
            }
            if (!content || !*content) continue;

            size_t content_len = strlen(content);
            TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
                lycon, content, content_len, cell_text_transform, cell_font_variant,
                CSS_VALUE_NORMAL, cell_overflow_wrap, CSS_VALUE_NORMAL, "table pseudo text");

            float text_max = (float)widths.max_content;
            float text_min = (float)widths.min_content;
            log_debug("%s Cell widths: pseudo %s content='%s' max=%.2f, min=%.2f", cell->source_loc(),
                      is_before ? "::before" : "::after", content, text_max, text_min);

            // Pseudo content flows inline with other content
            inline_run_max += text_max;
            has_inline_content = true;
            if (text_min > min_width) min_width = text_min;
        }
    }

    // CSS 2.1 §16.1: text-indent adds to the first line of inline content.
    // For max-content: adds to the single (unwrapped) line width.
    // For min-content: adds to the first word's line width.
    if (cell_text_indent > 0 && has_inline_content) {
        inline_run_max += cell_text_indent;
        min_width += cell_text_indent;
        log_debug("%s Cell widths: added text-indent=%.2f to inline_run_max and min_width", cell->source_loc(), cell_text_indent);
    }

    // Finalize any remaining inline run
    {
        float line_max = inline_run_max + float_run_max;
        if (line_max > max_width) max_width = line_max;
        if (float_run_min > min_width) min_width = float_run_min;
    }

    // CSS Text 3 §5.2: white-space: nowrap/pre prevents soft wrap opportunities,
    // so min-content width equals max-content width (content cannot break into lines)
    if (should_prevent_wrapping(cell) && max_width > min_width) {
        log_debug("%s Cell widths: nowrap/pre forces min=max (%.2f -> %.2f)", cell->source_loc(), min_width, max_width);
        min_width = max_width;
    }

    log_debug("%s Cell widths: inline_run_max=%.2f, final max_width=%.2f", cell->source_loc(), inline_run_max, max_width);

    // Add padding and border to both widths
    BoxMetrics box = layout_box_metrics(cell);
    float padding_horizontal = box.padding_h;

    // CSS 2.1 §17.6.2: In border-collapse mode, cell borders don't contribute to column widths.
    // The column widths are content+padding only. The half-borders are added only at
    // the final cell positioning stage for getBoundingClientRect reporting.
    float border_horizontal = border_collapse ? 0.0f : box.border_h;

    max_width += border_horizontal + padding_horizontal;
    min_width += border_horizontal + padding_horizontal;

    // CSS 2.1: Ensure max_width is at least 1px for cells that have actual content
    // (prevents zero-width cells that would make text invisible)
    // Note: min_width is NOT clamped - cells with no visible content can be 0-width
    if (max_width < 1.0f && max_width > 0.0f) max_width = 1.0f;

    log_debug("%s Cell widths: max=%.2f, min=%.2f (content + padding=%.1f + border=%.1f, collapse=%d)", cell->source_loc(),
        max_width, min_width, padding_horizontal, border_horizontal, border_collapse);

    result.max_width = max_width;
    result.min_width = min_width;

    apply_table_cell_width_constraints(cell, border_collapse, &result);
    return result;
}

// DEPRECATED: Old separate functions removed - now using measure_cell_widths()
// measure_cell_intrinsic_width() - merged into measure_cell_widths().max_width
// measure_cell_minimum_width() - merged into measure_cell_widths().min_width

// Single-pass table structure analysis - Phase 3 optimization
// Counts columns/rows and assigns column indices in one pass
// Uses navigation helpers for proper anonymous box support
static int table_row_remaining_in_group(ViewTableRow* row) {
    if (!row) return 0;

    int remaining = 0;
    for (View* sibling = static_cast<View*>(row); sibling; sibling = static_cast<View*>(sibling->next_sibling)) {
        if (sibling->view_type == RDT_VIEW_TABLE_ROW) remaining++;
    }
    return remaining > 0 ? remaining : 1;
}

static bool normalize_rowspans_to_row_groups(ViewTable* table) {
    bool changed = false;
    if (!table) return changed;

    for_each_table_row(table, [&](ViewTableRow* row) {
        int remaining_in_group = table_row_remaining_in_group(row);
        for_each_table_row_cell(row, [&](ViewTableCell* cell) {
            int original_span = cell->td->row_span;
            int used_span = original_span;
            if (used_span == 0) {
                // HTML rowspan=0 spans all remaining rows in the row group.
                used_span = remaining_in_group;
            } else if (used_span > remaining_in_group) {
                // CSS 2.1 §17.5: a cell box cannot extend beyond the last row
                // box of its row group.
                used_span = remaining_in_group;
            }
            if (used_span < 1) used_span = 1;
            if (used_span != original_span) {
                log_debug("%s Rowspan normalized to row group: %d -> %d (remaining=%d)",
                          table->source_loc(), original_span, used_span, remaining_in_group);
                cell->td->row_span = used_span;
                changed = true;
            }
        });
    });

    return changed;
}

static int table_occupancy_place_span(bool* occupied, int rows, int columns, int row, int col,
                                      int row_span, int col_span, int* max_col_used) {
    while (col < columns && occupied[row * columns + col]) col++;
    for (int r = row; r < row + row_span && r < rows; r++) {
        for (int c = col; c < col + col_span && c < columns; c++) {
            occupied[r * columns + c] = true;
        }
    }
    int right = col + col_span;
    if (max_col_used && right > *max_col_used) *max_col_used = right;
    return right;
}

static int table_metadata_place_span(TableMetadata* meta, int rows, int columns, int row, int col,
                                     int row_span, int col_span, int* start_col) {
    while (col < columns && meta->grid(row, col)) col++;
    if (start_col) *start_col = col;
    for (int r = row; r < row + row_span && r < rows; r++) {
        for (int c = col; c < col + col_span && c < columns; c++) {
            meta->grid(r, c) = true;
        }
    }
    return col + col_span;
}

static TableMetadata* analyze_table_structure(LayoutContext* lycon, ViewTable* table) {
    // First pass: count columns and rows using navigation helpers
    int columns = 0;
    int rows = 0;

    // Iterate all rows using navigation helpers
    // CSS 2.1 §17.5.5: Collapsed rows still contribute to column width calculation
    for_each_table_row(table, [&](ViewTableRow* row) {
        rows++;
        int row_cells = 0;
        for_each_table_row_cell_slot(row, [&](View* child) {
            if (child->view_type == RDT_VIEW_TABLE_CELL) {
                ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(child);
                row_cells += cell->td->col_span;
            } else {
                row_cells++;
            }
        });
        if (row_cells > columns) columns = row_cells;
    });

    // CSS 2.1 §17.2.1: Column count is max(cells_per_row, col_element_count)
    {
        int col_count = 0;
        for_each_table_column_source(table, [&](ViewElement* child) {
            if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
                bool has_col = false;
                for_each_table_colgroup_column(child, [&](ViewElement* col) {
                    col_count += table_positive_span_attr(col);
                    has_col = true;
                });
                if (!has_col) {
                    col_count += table_positive_span_attr(child);
                }
            } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
                col_count += table_positive_span_attr(child);
            }
        });
        if (col_count > columns) columns = col_count;
    }

    if (columns <= 0) return nullptr;

    // Resolve rowspans against row-group boundaries before assigning grid slots.
    // CSS 2.1 §17.5: a cell cannot extend beyond the last row box of its row group.
    // HTML rowspan=0 also spans only the remaining rows in the row group.
    {
        bool rowspans_changed = normalize_rowspans_to_row_groups(table);

        // Recount columns if normalization changed a span, since rowspans can
        // displace cells in later rows within the same group.
        if (rowspans_changed) {
            // Use a simple grid simulation to find actual column count
            // Allocate a temporary occupancy array (rows × current_columns_estimate)
            int est_cols = columns * 2 + 4;  // generous estimate
            bool* occupied = (bool*)mem_calloc(rows * est_cols, sizeof(bool), MEM_CAT_LAYOUT);
            int max_col_used = 0;

            int cur_row = 0;
            for_each_table_row(table, [&](ViewTableRow* row) {
                int col = 0;
                for_each_table_row_cell_slot(row, [&](View* child) {
                    if (is_out_of_flow_table_cell_slot(child)) {
                        col = table_occupancy_place_span(occupied, rows, est_cols,
                                                         cur_row, col, 1, 1, &max_col_used);
                        return;
                    }

                    ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(child);
                    col = table_occupancy_place_span(occupied, rows, est_cols,
                                                     cur_row, col, cell->td->row_span,
                                                     cell->td->col_span, &max_col_used);
                });
                cur_row++;
            });
            mem_free(occupied);
            if (max_col_used > columns) {
                log_debug("%s Recount columns after rowspan normalization: %d -> %d", table->source_loc(), columns, max_col_used);
                columns = max_col_used;
            }
        }
    }

    // Create metadata structure
    TableMetadata* meta = table_metadata_create(&lycon->scratch, columns, rows);

    // Second pass: assign column indices, measure widths, and track collapsed rows
    int current_row = 0;
    for_each_table_row(table, [&](ViewTableRow* row) {
        // Track visibility: collapse for this row
        // CSS 2.1 §17.5.5: Rows with visibility: collapse don't contribute to height
        if (is_visibility_collapse(lam::view_require_block(row))) {
            meta->row_collapsed[current_row] = true;
            log_debug("%s Row %d has visibility: collapse", table->source_loc(), current_row);
        }

        int col = 0;
        for_each_table_row_cell_slot(row, [&](View* child) {
            if (is_out_of_flow_table_cell_slot(child)) {
                col = table_metadata_place_span(meta, rows, columns, current_row, col, 1, 1, nullptr);
                return;
            }

            ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(child);
            int start_col = 0;
            col = table_metadata_place_span(meta, rows, columns, current_row, col,
                                            cell->td->row_span, cell->td->col_span, &start_col);

            // Assign indices. An over-full row can leave start_col == columns; clamp the stored
            // column index so it never indexes one past the columns-sized width arrays
            // (col_widths/col_min_widths/col_max_widths). The grid placement helper marks
            // from the raw start_col and is already bounded by `c < columns`.
            cell->td->col_index = (start_col < columns) ? start_col : (columns > 0 ? columns - 1 : 0);
            cell->td->row_index = current_row;
        });
        current_row++;
    });

    // CSS 2.1 §17.5.5: Track visibility: collapse for columns
    // Walk column/colgroup elements and check visibility on each column
    {
        int col_idx = 0;
        for_each_table_column_source(table, [&](ViewElement* child) {
            if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
                // Check if the colgroup itself is collapsed
                bool colgroup_collapsed = is_visibility_collapse(lam::view_require_block(child));
                bool has_col_children = false;
                for_each_table_colgroup_column(child, [&](ViewElement* col) {
                    has_col_children = true;
                    if (col_idx < columns) {
                        if (colgroup_collapsed || is_visibility_collapse(lam::view_require_block(col))) {
                            meta->col_collapsed[col_idx] = true;
                            log_debug("%s Column %d has visibility: collapse", table->source_loc(), col_idx);
                        }
                        col_idx++;
                    }
                });
                // Colgroup without col children implicitly defines span columns
                if (!has_col_children) {
                    int span = table_positive_span_attr(child);
                    for (int s = 0; s < span && col_idx < columns; s++) {
                        if (colgroup_collapsed) {
                            meta->col_collapsed[col_idx] = true;
                            log_debug("%s Column %d has visibility: collapse (from colgroup without children)", table->source_loc(), col_idx);
                        }
                        col_idx++;
                    }
                }
            } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
                if (col_idx < columns && is_visibility_collapse(lam::view_require_block(child))) {
                    meta->col_collapsed[col_idx] = true;
                    log_debug("%s Column %d has visibility: collapse", table->source_loc(), col_idx);
                }
                col_idx++;
            }
        });
    }

    return meta;
}

// Enhanced table layout algorithm with colspan/rowspan support
void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    if (!table) return;

    // Use the table's computed font-size saved at layout_table_content entry.
    // This is necessary because cell layout modifies lycon->font to the cell's font-size,
    // but the table's CSS properties (like height: 4em) should use the table's font-size.
    float table_font_size = (table->tb && table->tb->computed_font_size > 0)
                           ? table->tb->computed_font_size : 16.0f;

    // Initialize fixed layout fields
    table->tb->fixed_row_height = 0;  // 0 = auto height (calculate from content)
    log_debug("Starting enhanced table auto layout");
    log_debug("Table font-size: %.1fpx (from tb->computed_font_size)", table_font_size);
    log_debug("Table layout mode: %s", table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED ? "fixed" : "auto");
    log_debug("Table border-spacing: %fpx %fpx, border-collapse: %s",
        table->tb->border_spacing_h, table->tb->border_spacing_v, table->tb->border_collapse ? "true" : "false");
    bool has_direct_float = table_has_direct_float(table);

    // CRITICAL FIX: Handle caption positioning first
    // CSS 2.1 §17.4: A table may have multiple captions; all are rendered.
    TableCaptionCollection caption_collection = table_collect_captions(table);
    ArrayList* captions = caption_collection.captions;
    ViewBlock* caption = caption_collection.first_caption;  // first caption (for backward-compat checks)
    float caption_height = caption_collection.total_height;
    log_debug("Found %d caption(s), total caption_height=%.1f", captions->length, caption_height);

    // Step 1: Analyze table structure (Phase 3 optimization)
    // Single-pass analysis counts columns/rows AND assigns cell indices
    TableMetadata* meta = analyze_table_structure(lycon, table);
    if (!meta) {
        // No rows/columns — but the table may still have a caption.
        // CSS 2.1 §17.4: A table with only a caption is valid; the caption
        // is rendered as a block box and the table wrapper box accommodates it.
        if (caption) {
            // Use caption's explicit CSS width if set, otherwise shrink auto
            // captions to their intrinsic width before sizing the table wrapper.
            float table_width = caption->width;
            if (caption->blk && caption->blk->given_width > 0) {
                table_width = caption->blk->given_width;
                // Add padding + border for border-box width
                if (caption->bound) {
                    table_width += layout_box_metrics(caption).pad_border_h;
                }
            } else if (DomElement* caption_elem = caption->as_element()) {
                IntrinsicSizes caption_sizes = layout_measure_intrinsic_widths(
                    lycon, caption_elem, "caption-only table caption");
                float available_width = lycon->block.content_width;
                if (available_width <= 0.0f) {
                    available_width = lycon->line.right - lycon->line.left;
                }
                AvailableSize available = (available_width > 0.0f)
                    ? AvailableSize::make_definite(available_width)
                    : AvailableSize::make_indefinite();
                table_width = ceilf(compute_shrink_to_fit_width(
                    caption_sizes.min_content, caption_sizes.max_content, available));
                table_width = adjust_min_max_width(caption, table_width);
            }
            float caption_box_width = table_width;  // Caption's own box width (without margins)
            // CSS 2.1 §17.4: The table wrapper must accommodate the caption's margin-box.
            // Add fixed horizontal margins to the table wrapper width without affecting caption width.
            table_width += table_caption_positive_horizontal_margin(caption);

            // Position caption: margin-left determines x offset within table wrapper
            float cap_y = 0;
            for_each_table_caption(captions, [&](ViewBlock* cap, int ci) {
                table_position_caption_with_margins(cap, cap_y);
                cap->width = caption_box_width;
                cap_y += table_caption_height_with_margins(cap);
            });

            // CSS 2.1 §17.5.3: The 'height' property on the table element sets the
            // minimum height of the table grid. Even when the grid has no rows,
            // the explicit height contributes to the table wrapper height.
            float grid_height = 0;
            if (table->blk && table->blk->given_height >= 0) {
                grid_height = table->blk->given_height;
                // Add table border+padding to grid height (border-box)
                if (table->bound) {
                    BoxMetrics table_box = layout_box_metrics(table);
                    grid_height += table_box.border_v;
                    if (table_box.padding.top > 0) grid_height += table_box.padding.top;
                    if (table_box.padding.bottom > 0) grid_height += table_box.padding.bottom;
                }
            }
            float total_height = caption_height + grid_height;

            // Table wrapper width accommodates caption's margin-box
            table->width = table_width;
            table->height = total_height;
            table->content_width = table_width;
            table->content_height = total_height;
            lam::view_require_block(table)->height = total_height;

            log_debug("Caption-only table: width=%.1f (caption=%.1f), height=%.1f (caption=%.1f + grid=%.1f)",
                      table_width, caption_box_width, total_height, caption_height, grid_height);
        } else {
            // Empty table (no rows, no caption): dimensions come from explicit width/height
            // if specified, otherwise from the element's own padding and border.
            float bp_top = 0, bp_bottom = 0, bp_left = 0, bp_right = 0;
            if (table->bound) {
                if (table->bound->padding.top > 0) bp_top += table->bound->padding.top;
                if (table->bound->padding.bottom > 0) bp_bottom += table->bound->padding.bottom;
                if (table->bound->padding.left > 0) bp_left += table->bound->padding.left;
                if (table->bound->padding.right > 0) bp_right += table->bound->padding.right;
                if (table->bound->border) {
                    bp_top += table->bound->border->width.top;
                    bp_bottom += table->bound->border->width.bottom;
                    bp_left += table->bound->border->width.left;
                    bp_right += table->bound->border->width.right;
                }
            }

            // Use explicit width if given
            if (table->blk && table->blk->given_width > 0) {
                if (layout_uses_border_box(table)) {
                    table->width = table->blk->given_width;
                } else {
                    // content-box: given_width is content width, add padding+border
                    table->width = table->blk->given_width + bp_left + bp_right;
                }
            } else {
                // CSS 2.1 §17.5.2: A block-level table with 'width: auto' uses the
                // containing block width when it contains floated children that it must
                // enclose as a BFC. Tables with no in-flow or floated content use only
                // padding+border plus horizontal separated-border edge spacing
                // (shrink-to-fit to zero content).
                float empty_table_auto_width = bp_left + bp_right;
                if (table->tb && !table->tb->border_collapse && table->tb->border_spacing_h > 0.0f) {
                    empty_table_auto_width += 2.0f * table->tb->border_spacing_h;
                }
                bool has_float_children = false;
                for (View* ch = table->first_child; ch; ch = ch->next_sibling) {
                    if (ch->node_type != DOM_NODE_ELEMENT) continue;
                    ViewBlock* vb = lam::view_require_block(ch);
                    if (layout_position_is_floated(vb->position)) {
                        has_float_children = true;
                        break;
                    }
                }
                bool is_floated = layout_position_is_floated(table->position);
                bool is_inline_table = (table->display.outer == CSS_VALUE_INLINE);
                bool is_abspos = layout_position_is_abs_fixed(table->position);
                if (has_float_children && !is_floated && !is_inline_table && !is_abspos) {
                    // Block-level table in normal flow with float children: use containing block width
                    float container_width = lycon->block.content_width;
                    if (container_width <= 0) {
                        ViewBlock* parent = lam::view_as_block(static_cast<View*>(table->parent));
                        if (parent && parent->width > 0) {
                            container_width = parent->width;
                            if (parent->bound) {
                                container_width -= layout_box_metrics(parent).pad_border_h;
                            }
                        }
                    }
                    if (container_width > 0) {
                        table->width = container_width;
                        log_debug("Empty block-level table with floats: using containing block width %.1fpx", container_width);
                    } else {
                        table->width = empty_table_auto_width;
                    }
                } else {
                    table->width = empty_table_auto_width;
                }
            }
            table->height = bp_top + bp_bottom;
            if (table->blk && table->blk->given_height > 0) {
                if (layout_uses_border_box(table)) {
                    table->height = table->blk->given_height;
                } else {
                    // content-box: given_height is content height, add padding+border
                    table->height = table->blk->given_height + bp_top + bp_bottom;
                }
            }
            lam::view_require_block(table)->height = table->height;
            log_debug("Empty table: width=%.0f, height=%.0f (bp: t=%.0f b=%.0f l=%.0f r=%.0f)",
                      table->width, table->height, bp_top, bp_bottom, bp_left, bp_right);

            // CSS 2.1 §9.7: When table-internal elements have float applied, they are
            // blockified and removed from the table layout. But row groups and rows that
            // contained them still need sizing. Expand rows/row-groups to contain floated
            // children, inheriting the table's width.
            for (View* tch = table->first_child; tch; tch = tch->next_sibling) {
                if (!tch->view_type) continue;  // skip nil-views (text nodes)
                ViewBlock* tblk = lam::view_require_block(tch);
                bool is_row_group = (tblk->view_type == RDT_VIEW_TABLE_ROW_GROUP);
                bool is_row = (tblk->view_type == RDT_VIEW_TABLE_ROW);
                if (!is_row_group && !is_row) continue;

                float group_height = 0;

                if (is_row) {
                    table_size_float_containing_row(table, tblk, nullptr);
                } else {
                    // Row group: process each child row
                    for (View* rch = tblk->first_child; rch; rch = rch->next_sibling) {
                        if (!rch->view_type) continue;
                        ViewBlock* row = lam::view_require_block(rch);
                        if (row->view_type != RDT_VIEW_TABLE_ROW) continue;
                        table_size_float_containing_row(table, row, &group_height);
                    }
                    if (group_height > 0) {
                        tblk->width = table->width;
                        tblk->height = group_height;
                        log_debug("Float-containing row group sized: width=%.0f, height=%.0f",
                                  tblk->width, tblk->height);
                    }
                }
            }
        }
        return;
    }

    log_debug("Table layout: metadata created successfully, proceeding with width calculation");

    int columns = meta->column_count;
    int rows = meta->row_count;
    log_debug("Table has %d columns, %d rows (analyzed in single pass)", columns, rows);

    // Step 1.5: Border-collapse resolution
    // CSS 2.1 §17.6.2: Border resolution determines which borders win in conflicts
    // Resolved borders are stored in TableCellProp->*_resolved fields for rendering
    // Layout continues to use original BorderProp widths for positioning calculations
    if (table->tb->border_collapse) {
        log_debug("Resolving collapsed borders for rendering (layout uses original borders)");
        resolve_collapsed_borders(lycon, table, meta);

        // CSS 2.1 §17.6.2: Compute the maximum resolved border width at each column edge
        // across ALL rows. In border-collapse mode, the column grid lines are fixed vertically,
        // so the max border at each edge determines the space allocated for borders.
        // This ensures all cells in a column have the same width regardless of per-cell borders.
        for_each_table_cell(table, [&](ViewTableRow* row, ViewTableCell* tcell) {
                (void)row;
                int col = tcell->td->col_index;
                int right_edge = col + tcell->td->col_span;
                if (tcell->td->left_resolved && col >= 0 && col <= columns) {
                    float w = tcell->td->left_resolved->width;
                    if (w > meta->col_edge_max_border[col]) {
                        meta->col_edge_max_border[col] = w;
                    }
                }
                if (tcell->td->right_resolved && right_edge >= 0 && right_edge <= columns) {
                    float w = tcell->td->right_resolved->width;
                    if (w > meta->col_edge_max_border[right_edge]) {
                        meta->col_edge_max_border[right_edge] = w;
                    }
                }
        });
        for (int i = 0; i <= columns; i++) {
            log_debug("Col edge %d max border: %.1fpx", i, meta->col_edge_max_border[i]);
        }
    }

    // Check if table has explicit width (for percentage cell width calculation)
    float explicit_table_width = 0;
    bool has_explicit_table_width = false;
    float table_content_width = 0; // Width available for cells

    // Check if we're in intrinsic sizing mode (propagated via available_space)
    bool is_intrinsic_sizing = lycon->available_space.is_intrinsic_sizing();
    if (is_intrinsic_sizing) {
        log_debug("Table '%s': in intrinsic sizing mode (width=%s)",
            table->node_name(),
            lycon->available_space.width.is_min_content() ? "min-content" : "max-content");
    }

    // First check resolved style (from HTML width attribute or CSS)
    // The given_width is already resolved to absolute pixels during style resolution
    if (table->blk && table->blk->given_width >= 0) {
        explicit_table_width = table->blk->given_width;
        has_explicit_table_width = true;
        log_debug("Table width from resolved style (HTML attr or CSS): %.1fpx (percent=%s)",
                explicit_table_width,
                !isnan(table->blk->given_width_percent) ? "yes" : "no");
    }

    // If no resolved width, check CSS specified_style directly
    if (!has_explicit_table_width && table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem->specified_style) {
            CssDeclaration* width_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->value) {
                // Handle percentage width (e.g., width: 100%)
                if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    double percentage = width_decl->value->data.percentage.value;
                    // Calculate percentage relative to container width
                    // Use AvailableSpace if definite, otherwise fall back to BlockContext
                    float container_width_f = lycon->available_space.width.is_definite()
                        ? lycon->available_space.width.value
                        : lycon->block.content_width;
                    float container_width = container_width_f;
                    if (container_width <= 0) {
                        container_width = lycon->line.right - lycon->line.left;
                    }
                    if (container_width > 0) {
                        explicit_table_width = container_width * percentage / 100.0;
                        has_explicit_table_width = true;
                        log_debug("Table CSS percentage width: %.1f%% of %.1fpx = %.1fpx",
                                percentage, container_width, explicit_table_width);
                    }
                }
                // Handle length value (handles em, rem, px, etc.)
                else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
                         (width_decl->value->type == CSS_VALUE_TYPE_NUMBER &&
                          width_decl->value->data.number.value == 0)) {
                    float resolved_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                    explicit_table_width = resolved_width;
                    has_explicit_table_width = true;
                    log_debug("Table CSS explicit width: %.1fpx", explicit_table_width);
                }
            }
        }
    }

    // Calculate content width if we have an explicit width
    if (explicit_table_width > 0) {
        table_content_width = explicit_table_width;

        // CSS 2.1 §17.6.2: In border-collapse mode, CSS width is border-box
        // (includes half of outer collapsed borders), so subtract border.
        // CSS 2.1 §10.2: In separate borders mode, CSS width is content-box,
        // so border is additional and must NOT be subtracted.
        // Exception: box-sizing:border-box makes width border-box.
        // Note: HTML <table> elements get box-sizing:border-box from UA stylesheet
        // (set in resolve_css_style.cpp), so no need to check tag() here.
        bool table_width_is_border_box = table->tb->border_collapse ||
            layout_uses_border_box(table);
        if (table_width_is_border_box && table->bound && table->bound->border) {
            BoxMetrics table_box = layout_box_metrics(table);
            table_content_width -= table_box.border_h;
            log_debug("Subtracted table border from content width: -%.1fpx (left=%.1f, right=%.1f)",
                     table_box.border_h,
                     table->bound->border->width.left, table->bound->border->width.right);
        }

        // Subtract table padding from content width only when CSS width is border-box.
        // CSS 2.1 §10.2: In content-box mode (default for CSS tables), 'width' already
        // specifies the content area, which includes border-spacing and columns.
        // Padding is outside the content area and must NOT be subtracted.
        // CSS 2.1 §17.6.2: Padding on table elements is ignored in border-collapse mode.
        // Only box-sizing:border-box (e.g., HTML <table> gets this from UA stylesheet)
        // includes padding in the width, requiring subtraction here.
        if (table_width_is_border_box && !table->tb->border_collapse &&
            table->bound && table->bound->padding.left >= 0 && table->bound->padding.right >= 0) {
            table_content_width -= layout_box_metrics(table).padding_h;
        }

        // Subtract border-spacing (only in separate mode)
        if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
            table_content_width -= (columns + 1) * table->tb->border_spacing_h;
        }

        log_debug("Table content width for cells: %dpx (border_collapse=%d)",
                 table_content_width, table->tb->border_collapse);
    }

    // Step 2: Enhanced column width calculation with colspan/rowspan support
    // Use metadata's col_widths array (already allocated)
    float* col_widths = meta->col_widths;
    ArrayList* colspan_widths = arraylist_new(8);

    // Assign column indices and measure content with grid support
    // Use navigation helpers to iterate over all cells uniformly
    for_each_table_cell(table, [&](ViewTableRow* row, ViewTableCell* tcell) {
            (void)row;
            // Use pre-assigned column index from analyze_table_structure()
            int col = tcell->td->col_index;

            // Get explicit CSS width using helper function
            // CSS 2.1 §17.6.2: In border-collapse mode, cell borders don't add to column width
            // Set up the cell's font context for em-based width resolution
            FontBox saved_font_cell = lycon->font;
            if (tcell->font) {
                setup_font(lycon->ui_context, &lycon->font, tcell->font);
            }
            bool cell_width_is_table_relative = false;
            float cell_width = get_cell_css_width(lycon, tcell, table_content_width,
                table->tb->border_collapse, &cell_width_is_table_relative);
            float cell_width_percent = get_cell_css_width_percent(tcell);
            if (cell_width_percent > 0.0f && col >= 0 && col < meta->column_count) {
                int span = tcell->td->col_span > 0 ? tcell->td->col_span : 1;
                float percent_per_col = cell_width_percent / span;
                for_each_table_span_column(col, span, meta->column_count, [&](int c) {
                    if (percent_per_col > meta->col_percent_widths[c]) {
                        meta->col_percent_widths[c] = percent_per_col;
                    }
                });
            }

            // Track columns with explicit CSS width for distribution
            if (cell_width > 0 && tcell->td->col_span == 1 && col >= 0 && col < meta->column_count) {
                meta->col_has_explicit_width[col] = true;
            }

            // Calculate both minimum and preferred widths for CSS 2.1 table layout
            float min_width = 0.0f;   // MCW - Minimum Content Width
            float pref_width = 0.0f;  // PCW - Preferred Content Width

            if (cell_width == 0.0f) {
                // No explicit CSS width - measure intrinsic content widths
                // CSS 2.1 §17.6.2: In border-collapse mode, cell borders don't add to column width
                CellWidths widths = measure_cell_widths(lycon, tcell, table->tb->border_collapse);
                pref_width = widths.max_width;  // PCW (preferred/max-content)
                min_width = widths.min_width;   // MCW (minimum/min-content)
                cell_width = pref_width; // Use preferred for backward compatibility
            } else if (table->tb->border_collapse) {
                // Border-collapse with explicit CSS width: CSS width sets the
                // preferred column width, but the actual minimum is based on
                // content MCW. This allows the table constraint (from containing
                // block) to shrink columns below their CSS width when needed,
                // matching browser behavior.
                pref_width = cell_width;
                CellWidths widths = measure_cell_widths(lycon, tcell, table->tb->border_collapse);
                min_width = widths.min_width;  // MCW from actual content
            } else {
                // Separate borders with explicit CSS width
                pref_width = cell_width;
                // CSS Tables §4.1: percentage/calc widths on cells are
                // distribution constraints relative to the table width, not
                // unbreakable minimum content. Absolute widths still floor MCW.
                CellWidths widths = measure_cell_widths(lycon, tcell, table->tb->border_collapse);
                min_width = cell_width_is_table_relative ?
                    widths.min_width : (widths.min_width > cell_width ? widths.min_width : cell_width);
            }

            // CSS 2.1 §17.5.2.2: When white-space: nowrap/pre prevents soft wrap
            // opportunities, the cell's min-content equals max-content. The preferred
            // width must expand beyond any CSS width to accommodate content that cannot
            // break. Only applies when wrapping is actually suppressed.
            if (should_prevent_wrapping(tcell) && pref_width < min_width) {
                pref_width = min_width;
                cell_width = min_width;
            }

            // Store intrinsic width for post-border-resolution adjustment
            tcell->td->intrinsic_width = pref_width;

            // CSS 2.1 §17.6.2: In border-collapse mode, include half of the cell's
            // resolved collapsed borders in the column width calculation.
            // Each cell needs (content + padding + half_left_border + half_right_border).
            // The column width must accommodate the cell with the largest total.
            // This correctly handles cases where a cell with less content but wider
            // borders doesn't inflate the column beyond what's needed.
            if (table->tb->border_collapse && tcell->td->col_span == 1) {
                float half_left = tcell->td->left_resolved ? tcell->td->left_resolved->width / 2.0f : 0.0f;
                float half_right = tcell->td->right_resolved ? tcell->td->right_resolved->width / 2.0f : 0.0f;
                pref_width += half_left + half_right;
                min_width += half_left + half_right;
                cell_width += half_left + half_right;
                log_debug("Border-collapse cell measurement: col=%d, content+pad=%.1f, +half_left=%.1f, +half_right=%.1f, total=%.1f",
                         col, tcell->td->intrinsic_width, half_left, half_right, pref_width);
            }

            if (tcell->td->col_span == 1) {
                // Single column cell - update min and preferred widths (bounds check)
                if (col >= 0 && col < meta->column_count) {
                    if (min_width > meta->col_min_widths[col]) {
                        meta->col_min_widths[col] = min_width;
                    }
                    if (min_width > meta->col_single_min_widths[col]) {
                        meta->col_single_min_widths[col] = min_width;
                    }
                    if (pref_width > meta->col_max_widths[col]) {
                        meta->col_max_widths[col] = pref_width;
                    }
                    // Maintain backward compatibility for now
                    if (cell_width > col_widths[col]) {
                        col_widths[col] = cell_width;
                    }
                }
            } else {
                ColspanWidthContribution* contribution =
                    (ColspanWidthContribution*)mem_calloc(1, sizeof(ColspanWidthContribution), MEM_CAT_LAYOUT);
                contribution->cell = tcell;
                contribution->col = col;
                contribution->span = tcell->td->col_span;
                contribution->order = colspan_widths->length;
                contribution->min_width = min_width;
                contribution->pref_width = pref_width;
                contribution->cell_width = cell_width;
                arraylist_append(colspan_widths, contribution);
            }
            // Restore parent font context after cell width measurement
            lycon->font = saved_font_cell;
    });

    arraylist_sort(colspan_widths, compare_colspan_width_contributions);
    for (int i = 0; i < colspan_widths->length; i++) {
        ColspanWidthContribution* contribution =
            (ColspanWidthContribution*)colspan_widths->data[i];
        apply_colspan_width_contribution(table, meta, contribution);
        mem_free(contribution);
    }
    arraylist_free(colspan_widths);

    // CSS 2.1 §17.5.2.2: Apply <col> element width/min-width/max-width
    // Column elements can set column widths even for columns without cells.
    for (int c = 0; c < columns; c++) {
        ViewBlock* col_elem = find_column_element(table, c);
        float width_divisor = 1.0f;
        if (!col_elem) {
            // CSS 2.1 §17.3: A table-column-group without child columns defines
            // implicit columns. Its width constrains the group, distributed over
            // the columns it spans.
            col_elem = find_colgroup_element(table, c);
            if (!col_elem) continue;
            int span = table_positive_span_attr(col_elem);
            width_divisor = (float)span;
        }
        table_apply_column_constraints(lycon, meta, col_widths, c, col_elem, width_divisor);
    }

    // Apply CSS 2.1 table-layout algorithm with improved precision
    float fixed_table_width = 0; // Store explicit width for fixed layout
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED) {
        log_debug("=== CSS 2.1 FIXED LAYOUT ALGORITHM ===");

        // STEP 1: Get explicit table width from CSS (CSS 2.1 Section 17.5.2)
        float fixed_explicit_width = table_resolve_fixed_explicit_width(lycon, table);

        // CSS 2.1 §17.5.2.1: "A value of 'auto' (for both 'display: table' and
        // 'display: inline-table') means use the automatic table layout algorithm."
        // If no explicit width is available, fall back to the auto layout algorithm.
        if (fixed_explicit_width == 0) {
            log_debug("FIXED LAYOUT skipped: width:auto, falling back to auto layout");
        }

        // Store for later use
        fixed_table_width = fixed_explicit_width;
        if (fixed_table_width > 0) {
            log_debug("FIXED LAYOUT - stored fixed_table_width: %.1fpx", fixed_table_width);
        }

      if (fixed_explicit_width > 0) {
        // STEP 2: Calculate available content width for CSS fixed-layout columns.
        float content_width = table_fixed_content_width_for_columns(
            table, fixed_explicit_width, columns);
        // STEP 3: Determine column widths per CSS 2.1 §17.5.2.1
        // Priority: 1) col element width, 2) first-row cell width, 3) equal distribution
        float* explicit_col_widths = (float*)scratch_calloc(&lycon->scratch, columns * sizeof(float));
        float total_explicit = 0.0f;  int unspecified_cols = 0;

        // STEP 3a: Read explicit widths from <col>/<colgroup> elements first
        {
            int col_idx = 0;
            for_each_table_column_source(table, [&](ViewElement* child) {
                if (col_idx >= columns) return;
                if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
                    for_each_table_colgroup_column(child, [&](ViewElement* col) {
                        if (col_idx >= columns) return;
                        col_idx += table_apply_fixed_column_css_width(
                            lycon, col, explicit_col_widths, col_idx, columns,
                            content_width, &total_explicit, "from <col> element");
                    });
                } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
                    col_idx += table_apply_fixed_column_css_width(
                        lycon, child, explicit_col_widths, col_idx, columns,
                        content_width, &total_explicit, "from standalone <col>");
                }
            });
        }

        // STEP 3b: Read cell widths from first row (only for columns not yet specified by col elements)
        // Find first row using navigation helper
        ViewTableRow* first_row = table->first_row();

        // Read cell widths from first row (only for columns not yet set by col elements)
        if (first_row) {
            int col = 0;
            log_debug("Reading first row cell widths...");
            for_each_table_row_cell(first_row, [&](ViewTableCell* cell) {
                if (col >= columns) return;
                col += table_apply_fixed_first_row_cell_width(
                    lycon, table, cell, explicit_col_widths, col, columns,
                    content_width, &total_explicit, &unspecified_cols);
            });
        }

        // STEP 4: Distribute widths according to CSS table-layout: fixed algorithm
        table_distribute_fixed_column_widths(
            explicit_col_widths, columns, &content_width, total_explicit, unspecified_cols);

        // STEP 5: Replace col_widths with fixed layout widths
        memcpy(col_widths, explicit_col_widths, columns * sizeof(float));
        scratch_free(&lycon->scratch, explicit_col_widths);

        log_debug("=== FIXED LAYOUT COMPLETE ===");
        for (int i = 0; i < columns; i++) {
            log_debug("  Final column %d width: %.1fpx", i, col_widths[i]);
        }

        table_apply_fixed_height_distribution(lycon, table, rows);
      } // end if (fixed_explicit_width > 0)
    }

    // Step 3: CSS 2.1 Table Layout Algorithm - Width Distribution (Section 17.5.2)
    // Run auto algorithm if NOT using fixed layout, or if fixed layout was skipped (width:auto)
    if (table->tb->table_layout != TableProp::TABLE_LAYOUT_FIXED || fixed_table_width == 0) {
        log_debug("===== CSS 2.1 AUTO TABLE LAYOUT ALGORITHM =====");

    // Calculate minimum and preferred table widths (including borders and spacing)
    float min_table_content_width = 0;  // MCW sum for table content
    float pref_table_content_width = 0; // PCW sum for table content

    for (int i = 0; i < columns; i++) {
        min_table_content_width += meta->col_min_widths[i];
        pref_table_content_width += meta->col_max_widths[i];
        log_debug("Column %d: MCW=%.1fpx, PCW=%.1fpx",
                 i, meta->col_min_widths[i], meta->col_max_widths[i]);
    }
    float total_percent_col_width =
        table_sum_span_columns(meta->col_percent_widths, 0, columns, columns);

    // Add border-spacing to table width calculation (CSS 2.1 requirement)
    float border_spacing_total = 0;
    if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
        border_spacing_total = (columns + 1) * table->tb->border_spacing_h;
    }

    float min_table_width = min_table_content_width + border_spacing_total;
    float pref_table_width = pref_table_content_width + border_spacing_total;
    if (caption) {
        float caption_width_contribution =
            table_measure_caption_width_contribution(lycon, table, caption);
        if (caption_width_contribution > pref_table_width) {
            log_debug("Caption wider than table content: caption=%dpx > table=%dpx",
                     caption_width_contribution, pref_table_width);
            pref_table_width = caption_width_contribution;
        }
        if (caption_width_contribution > min_table_width) {
            min_table_width = caption_width_contribution;
        }
    }

    log_debug("Table content: min=%.1fpx, preferred=%.1fpx", min_table_content_width, pref_table_content_width);
    log_debug("Table total (with spacing): min=%.1fpx, preferred=%.1fpx", min_table_width, pref_table_width);

    // CSS 2.1: For auto-width tables, constrain by available space minus margins.
    float max_available_width = 0.0f;
    if (!has_explicit_table_width) {
        max_available_width = table_apply_auto_available_width_constraint(
            lycon, table, meta, &pref_table_width, min_table_width);
    }

    float used_table_width;
    bool direct_float_expanded_auto_width = false;
    if (has_explicit_table_width) {
        float explicit_content_area = table_explicit_content_area_for_auto_layout(
            table, meta, explicit_table_width);
        // CSS 2.1: Table has explicit width - use content area (but not less than minimum)
        used_table_width = explicit_content_area > min_table_width ? explicit_content_area : min_table_width;
        log_debug("CSS 2.1: Using explicit table content width: %.1fpx (requested: %.1fpx)", used_table_width, explicit_content_area);
    } else {
        // CSS 2.1: Table width is auto - use preferred width
        used_table_width = pref_table_width;
        if (has_direct_float && table->display.outer == CSS_VALUE_BLOCK &&
            max_available_width > used_table_width) {
            // Direct floated children establish float intrusions inside the table
            // formatting context. A block-level auto-width table must leave room
            // for both the float and following row content instead of shrink-
            // wrapping only the row grid.
            used_table_width = max_available_width;
            direct_float_expanded_auto_width = true;
        }
        if (total_percent_col_width >= 99.0f &&
            max_available_width > used_table_width) {
            // CSS Tables auto layout treats percentage cell widths as table
            // width constraints. A near/full-width percentage column cannot be
            // satisfied by the intrinsic shrink-wrap width when the containing
            // block has a definite available width.
            used_table_width = max_available_width;
        }
        log_debug("CSS 2.1: Using preferred table width: %.1fpx (table width: auto)", used_table_width);
    }

    // Calculate available content width for column distribution
    float available_content_width = used_table_width - border_spacing_total;
    if (direct_float_expanded_auto_width) {
        available_content_width = pref_table_content_width;
    }

    // In border-collapse mode, col_max_widths and col_min_widths already include
    // per-cell border halves (added during measurement). No additional subtraction needed.
    if (table->tb->border_collapse && explicit_table_width > 0) {
        log_debug("Border-collapse: col widths already include border halves, available=%.1fpx",
                  available_content_width);
    }

    bool used_percent_distribution = table_apply_percent_column_distribution(
        meta, col_widths, columns, total_percent_col_width,
        available_content_width, min_table_content_width);

    // Check for equal distribution case (CSS behavior for similar columns)
    bool use_equal_distribution = table_columns_within_tolerance(meta->col_max_widths, columns, 3.0f);

    if (!used_percent_distribution &&
        use_equal_distribution && columns > 1 && !has_explicit_table_width) {
        // Special case: columns have similar preferred widths and table width is auto
        // Use equal distribution (common browser optimization for balanced tables)
        float avg_width = used_table_width / columns;

        log_debug("Using equal distribution - columns have similar content (avg=~%.1fpx)", avg_width);
        table_assign_columns(col_widths, columns, avg_width);
    }

    // CSS 2.1 Column Width Distribution Algorithm (Section 17.5.2.2)
    if (!used_percent_distribution) {
        table_apply_auto_column_width_distribution(
            meta, col_widths, columns, available_content_width,
            min_table_content_width, pref_table_content_width);
    }
    } // End of auto layout algorithm guard

    float table_padding_horizontal = 0.0f;
    float table_width = table_prepare_final_padding_box_width(
        table, meta, col_widths, columns, &table_padding_horizontal);

    // CSS 2.1 §17.5.2.1: For fixed layout, "the width of the table is then the
    // greater of the value of the 'width' property for the table element and the
    // sum of the column widths (plus cell spacing or borders)."
    // table_width is padding-box (columns + spacing + padding).
    // Convert CSS width to padding-box for comparison:
    //   border-collapse (§17.6.2): CSS width is border-box → subtract border
    //   separate borders (§10.2): CSS width is content-box → add padding
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED && fixed_table_width > 0) {
        float css_padding_box = table_fixed_css_padding_box_width(table, fixed_table_width);
        log_debug("Fixed layout: css_padding_box=%.0f, min_padding_box=%.0f",
               css_padding_box, table_width);
        if (css_padding_box > table_width) {
            table_width = css_padding_box;
        }
        log_debug("Fixed layout: final table_width=%.0f", table_width);
    }
    // For auto layout with explicit CSS width in border-collapse mode,
    // explicit_table_width is border-box (includes outer half-borders of collapsed cells).
    // table_width (sum of col_widths) is already the content area; table_border_width will be
    // added at the final step. Ensure table_width is at least the explicit content area
    // (explicit_table_width minus outer half-borders), so the final border-box = explicit_table_width.
    else if (explicit_table_width > 0 && table->tb->border_collapse) {
        float bc_outer_half = meta->collapsed_border_left / 2.0f + meta->collapsed_border_right / 2.0f;
        float bc_content_area = explicit_table_width - bc_outer_half;
        if (bc_content_area > table_width) {
            log_debug("Border-collapse explicit width: raising table_width from %.1f to %.1f (explicit=%.1f minus outer_half=%.1f)",
                   table_width, bc_content_area, explicit_table_width, bc_outer_half);
            table_width = bc_content_area;
        } else {
            log_debug("Border-collapse explicit width: table_width=%.1f >= bc_content_area=%.1f (explicit=%.1f, outer_half=%.1f)",
                   table_width, bc_content_area, explicit_table_width, bc_outer_half);
        }
    }

    log_debug("Final table width for layout: %.0fpx", table_width);

    // CSS 2.1 §10.4: Apply min-width/max-width constraints to table width.
    // table_width is padding-box (excludes border).
    // given_min/max_width is border-box when box-sizing:border-box, content-box otherwise.
    // Convert min/max to border-box for comparison with border_box_width.
    table_apply_minmax_width_constraints(
        table, meta, col_widths, columns, &table_width, table_padding_horizontal);

    log_debug("===== CSS 2.1 TABLE LAYOUT COMPLETE =====");

    // Step 4: Position cells and calculate row heights with CSS 2.1 border model

    float* col_x_positions = (float*)scratch_calloc(&lycon->scratch, (columns + 1) * sizeof(float));

    // Start with table padding and left border-spacing for separate border model
    // CSS 2.1 §17.6.2: Padding on table elements is ignored in border-collapse mode
    float table_padding_left = 0;
    if (!table->tb->border_collapse && table->bound && table->bound->padding.left >= 0) {
        table_padding_left = table->bound->padding.left;
        log_debug("Added table padding left: +%.1fpx", table_padding_left);
    }

    // Add table border width (content starts inside the border)
    float table_border_left = 0;
    if (table->bound && table->bound->border && table->bound->border->width.left > 0) {
        if (table->tb->border_collapse) {
            // Border-collapse: cells start at half of the collapsed border
            // The other half is outside the cells (part of table's border area)
            table_border_left = table->bound->border->width.left / 2.0f;
            log_debug("Border-collapse: table border left half: +%.1fpx (full=%.1fpx)",
                     table_border_left, table->bound->border->width.left);
        } else {
            table_border_left = table->bound->border->width.left;
            log_debug("Added table border left: +%.1fpx", table_border_left);
        }
    }

    col_x_positions[0] = table_border_left + table_padding_left;
    if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
        col_x_positions[0] += table->tb->border_spacing_h;
        log_debug("Added left border-spacing: +%.1fpx", table->tb->border_spacing_h);
    }

    // CSS 2.1 Column Position Calculation (Section 17.5)
    // In border-collapse mode, col_widths already include per-cell border halves
    // (added during measurement). Position columns so cells touch.
    if (table->tb->border_collapse) {
        // col_widths already include border halves, use them directly
        for (int c = 0; c < columns; c++) {
            log_debug("Border-collapse col_width[%d]: %.1f (includes border halves)", c, col_widths[c]);
        }

        // Set column positions: cells touch each other
        for (int i = 1; i <= columns; i++) {
            col_x_positions[i] = col_x_positions[i-1] + col_widths[i-1];
            log_debug("Border-collapse: Column %d starts at x=%.1fpx (prev + col_width %.1f)",
                     i, col_x_positions[i], col_widths[i-1]);
        }
    } else {
        // Non-collapsed: use original column position logic
        for (int i = 1; i <= columns; i++) {
            col_x_positions[i] = col_x_positions[i-1] + col_widths[i-1];

            if (table->tb->border_spacing_h > 0) {
                // CSS 2.1: Separate borders - add border-spacing between columns
                float precise_spacing = table->tb->border_spacing_h;
                col_x_positions[i] += precise_spacing;
                log_debug("Border-spacing: Added %.1fpx between columns %d and %d",
                         precise_spacing, i-1, i);
            }
            log_debug("CSS 2.1: Column %d starts at x=%.1fpx", i, col_x_positions[i]);
        }
    }

    // Start Y position - only include caption height if caption is at top
    float current_y = 0;
    if (caption && table->tb->caption_side == TableProp::CAPTION_SIDE_TOP) {
        current_y = caption_height;
    }

    // Add table border (content starts inside the border)
    float table_border_top = 0;
    if (table->tb->border_collapse) {
        // Border-collapse: CSS 2.1 §17.6.2
        // Content starts at half the collapsed top border.
        // The collapsed border may come from cells, rows, columns, or the table itself,
        // so we use meta->collapsed_border_top (resolved in resolve_collapsed_borders)
        // rather than checking only the table element's own border.
        float collapsed_top = meta->collapsed_border_top;
        if (collapsed_top <= 0 && table->bound && table->bound->border) {
            // Fallback if no cells resolved: use table's own border
            collapsed_top = table->bound->border->width.top;
        }
        if (collapsed_top > 0) {
            table_border_top = collapsed_top / 2.0f;
            current_y += table_border_top;
            log_debug("Added collapsed table border top: +%.1fpx (resolved=%.1f)", table_border_top, collapsed_top);
        }
    } else if (table->bound && table->bound->border && table->bound->border->width.top > 0) {
        table_border_top = table->bound->border->width.top;
        current_y += table_border_top;
        log_debug("Added table border top: +%.1fpx", table_border_top);
    }

    // Add table padding (space inside table border)
    // CSS 2.1 §17.6.2: Padding on table elements is ignored in border-collapse mode
    float table_padding_top = 0;
    if (!table->tb->border_collapse && table->bound && table->bound->padding.top >= 0) {
        table_padding_top = table->bound->padding.top;
        current_y += table_padding_top;
        log_debug("Added table padding top: +%.1fpx", table_padding_top);
    }

    // Add top border-spacing for separate border model
    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
        current_y += table->tb->border_spacing_v;
        log_debug("Added top border-spacing: +%.1fpx", table->tb->border_spacing_v);
    }

    // Save the y-offset where the content area starts (for column positioning)
    // CSS 2.1 §17.2.1: column elements span from the content area top
    float content_area_top_y = current_y;

    // Compute table border-box width (= table wrapper content width) for caption sizing.
    // CSS 2.1 §17.4: captions use the wrapper's content width as containing block.
    float table_border_h = 0;
    if (table->tb->border_collapse) {
        table_border_h = meta->collapsed_border_left / 2.0f + meta->collapsed_border_right / 2.0f;
    } else if (table->bound && table->bound->border) {
        table_border_h = layout_box_metrics(table).border_h;
    }
    float wrapper_content_width = table_width + table_border_h;

    // Position caption at top if caption-side is top (default)
    if (captions->length > 0 && table->tb->caption_side == TableProp::CAPTION_SIDE_TOP) {
        caption_height = table_position_caption_stack(
            lycon, table, captions, 0.0f, table_width, wrapper_content_width,
            TABLE_CAPTION_WIDTH_REFERENCE_ADJUSTED_CAP, TABLE_CAPTION_STACK_TOP);

        // Update current_y with total caption height
        current_y = caption_height + table_border_top + table_padding_top;
        if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
            current_y += table->tb->border_spacing_v;
        }
        content_area_top_y = current_y;
        log_debug("Updated current_y after all captions: %.1f", current_y);
    }

    // Global row index for tracking row positions across all row groups
    int global_row_index = 0;

    // =========================================================================
    // CSS 2.1 Section 17.2: Visual ordering of row groups
    // Only the FIRST table-header-group acts as header (rendered at top).
    // Only the FIRST table-footer-group acts as footer (rendered at bottom).
    // Additional thead/tfoot elements are treated as table-row-group and
    // maintain their source order relative to other body groups.
    // =========================================================================

    TableOrderedRowElements ordered_rows = table_collect_ordered_row_elements(table);
    ArrayList* body_groups = ordered_rows.body_groups;
    ArrayList* ordered_elements = ordered_rows.ordered_elements;

    log_info("%s Row group ordering: header=%s, %d body groups, footer=%s (total %d)", table->source_loc(),
              ordered_rows.header_group ? "yes" : "no", body_groups->length,
              ordered_rows.footer_group ? "yes" : "no", ordered_elements->length);

    // Process elements in visual order (THEAD groups → TBODY groups → direct rows → TFOOT groups)
    for (int _i = 0; _i < ordered_elements->length; _i++) {
        ViewBlock* child = table_array_view_block(ordered_elements, _i);
        log_info("%s Processing ordered element %d: view_type=%d", table->source_loc(), _i, child->view_type);
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            table_position_row_group_box(
                table, meta, child, col_widths, col_x_positions, columns,
                has_direct_float, &current_y);
            float group_start_y = current_y;
            float group_content_end_y = group_start_y;

            // CSS 2.1 §17.5.3: Check row group height properties
            // Percentage heights compute to auto; non-percentage heights act as minimum height
            // Note: CSS 2.1 says min-height/max-height on row groups is undefined.
            // Browsers apply 'height' as minimum but ignore min-height/max-height.
            bool group_has_percent_height = false;
            float explicit_group_height = table_resolve_row_group_explicit_height(
                lycon, table, child, &group_has_percent_height);

            for_each_table_row_in_group(group, [&](ViewTableRow* trow, ViewBlock* row) {
                // CSS 2.1 §17.5.3: If row group has percentage height, mark all its rows
                if (group_has_percent_height && global_row_index < meta->row_count) {
                    meta->row_has_percent_height[global_row_index] = true;
                }

                // CSS 2.1 §17.5.5: Check for visibility: collapse
                // Collapsed rows don't render and don't contribute to height
                bool is_collapsed = (global_row_index < meta->row_count &&
                                     meta->row_collapsed[global_row_index]);

                if (is_collapsed) {
                    table_place_collapsed_row(
                        table, meta, trow, current_y - group_start_y, child->width,
                        current_y, col_widths, col_x_positions, columns,
                        global_row_index, "row");
                    global_row_index++;

                    // No height contribution, no spacing after collapsed row
                    return;
                }

                // Position row relative to row group
                row->x = 0;
                row->y = current_y - group_start_y; // Relative to row group
                row->width = child->width; // Match tbody width
                log_debug("%s Row positioned at x=%.1f, y=%.1f (relative to group), width=%.1f", table->source_loc(),
                    row->x, row->y, row->width);

                // Calculate row height and position cells
                float row_height = table_measure_row_cells(
                    lycon, table, meta, trow, col_widths, col_x_positions, columns);

                // CSS 2.1 §9.4.1: Tables establish a BFC. When a row contains
                // floated children (blockified table-internal elements per §9.7),
                // the row expands to contain them.
                for (DomNode* rchild = row->first_child; rchild; rchild = rchild->next_sibling) {
                    if (!rchild->is_element()) continue;
                    ViewBlock* rvb = lam::view_as_block(static_cast<View*>(rchild));
                    if (!rvb) continue;
                    if (layout_position_is_floated(rvb->position)) {
                        float float_bottom = rvb->height;
                        if (float_bottom > row_height) {
                            log_debug("%s Row float containment: expanding height %.1f -> %.1f for floated child", table->source_loc(),
                                      row_height, float_bottom);
                            row_height = float_bottom;
                        }
                    }
                }

                // CSS 2.1 §17.5.4: Apply baseline alignment across cells in this row.
                // This must happen after all cells are laid out but before row height is finalized,
                // because baseline alignment may shift content and increase cell/row heights.
                apply_row_baseline_alignment(lycon, trow, &row_height);

                float explicit_row_height = table_resolve_row_explicit_height(
                    lycon, table, meta, row, global_row_index, "Row");

                // Use the larger of content height and explicit CSS height
                if (explicit_row_height > row_height) {
                    row_height = explicit_row_height;
                    log_debug("%s Using explicit row height %.1fpx instead of content height", table->source_loc(), row_height);
                }

                // Apply fixed layout height if specified
                if (table->tb->fixed_row_height > 0) {
                    apply_fixed_row_height(lycon, trow, table->tb->fixed_row_height);
                } else {
                    row->height = row_height;

                    // CSS 2.1 §17.6.2: In border-collapse mode, cell heights already include
                    // half of the collapsed borders (via calculate_cell_height).
                    // Row height = max(cell heights), no additional border adjustment needed.
                    // The row's reported height from getBoundingClientRect should match
                    // the tallest cell's height.
                    log_debug("%s Row height from cells: %.1f (border-collapse=%d)", table->source_loc(),
                            row_height, table->tb->border_collapse);

                    update_row_cells_after_height_change(lycon, trow, row->height, false, true);
                }

                // Track row height and Y position for rowspan calculation
                table_track_row_metrics(table, meta, global_row_index,
                                        current_y, row->height, "row");
                global_row_index++;

                current_y += row->height;
                group_content_end_y = current_y;

                // Add vertical border-spacing after each row except the last row in the table.
                // CSS 2.1 §17.6.1: border-spacing applies between adjacent cell borders,
                // regardless of row group boundaries. The bottom edge spacing (after the
                // last row) is handled separately when finalizing table height.
                // Note: global_row_index was already incremented above.
                if (!table->tb->border_collapse && table->tb->border_spacing_v > 0 && global_row_index < meta->row_count) {
                    current_y += table->tb->border_spacing_v;
                    log_debug("%s Added vertical spacing after row: +%.1fpx", table->source_loc(), table->tb->border_spacing_v);
                }
            });

            // Set row group dimensions (relative to table) - preserve our calculated positioning
            // Don't override x and y - they were set earlier with proper calculations
            // Width already set above based on border-collapse mode
            // Keep inter-row-group border-spacing in the table cursor, but outside
            // the preceding row group's own border box.
            child->height = group_content_end_y - group_start_y;

            // CSS 2.1 §17.5.3: height on row groups specifies minimum height
            table_apply_row_group_min_height(
                lycon, table, meta, group, child, explicit_group_height, &current_y);
        }
        else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            // Handle direct table rows (part of implicit tbody, positioned with other tbody content)
            ViewTableRow* trow = lam::view_require<RDT_VIEW_TABLE_ROW>(child);

            // CSS 2.1 §17.5.5: Check for visibility: collapse
            bool is_collapsed = (global_row_index < meta->row_count &&
                                 meta->row_collapsed[global_row_index]);

            if (is_collapsed) {
                table_place_collapsed_row(
                    table, meta, trow, current_y, table_width, current_y,
                    col_widths, col_x_positions, columns, global_row_index,
                    "direct row");
                global_row_index++;

                // No height contribution, no spacing after collapsed row
                continue;
            }

            current_y = table_clear_direct_float_intrusion(
                table, current_y, table_width, has_direct_float);

            trow->x = 0;  trow->y = current_y; // Relative to table
            trow->width = table_width;
            log_debug("%s Direct row positioned at x=%.1f, y=%.1f (relative to table), width=%.1f", table->source_loc(),
                   trow->x, trow->y, trow->width);

            float row_height = table_measure_row_cells(
                lycon, table, meta, trow, col_widths, col_x_positions, columns);

            // CSS 2.1 §17.5.4: Apply baseline alignment across cells in this row
            apply_row_baseline_alignment(lycon, trow, &row_height);

            float explicit_row_height = table_resolve_row_explicit_height(
                lycon, table, meta, trow, global_row_index, "Direct row");

            // Use the larger of content height and explicit CSS height
            if (explicit_row_height > row_height) {
                row_height = explicit_row_height;
            }

            // Apply row height
            trow->height = row_height;

            // CSS 2.1 §17.6.2: In border-collapse mode, row height includes
            // half of the collapsed top border + half of the collapsed bottom border.
            if (table->tb->border_collapse) {
                float max_top_border = 0.0f, max_bottom_border = 0.0f;
                float border_contribution = table_row_collapsed_vertical_border_contribution(
                    trow, &max_top_border, &max_bottom_border);
                trow->height += border_contribution;
                log_debug("%s Border-collapse direct row height adjustment: +%.1f (top %.1f + bottom %.1f) / 2", table->source_loc(),
                        border_contribution, max_top_border, max_bottom_border);
            }

            update_row_cells_after_height_change(lycon, trow, trow->height, false, true);

            // Track row height and Y position for rowspan calculation
            table_track_row_metrics(table, meta, global_row_index,
                                    current_y, trow->height, "direct row");
            global_row_index++;

            current_y += trow->height;

            // Add vertical border-spacing after row (if not last)
            if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
                current_y += table->tb->border_spacing_v;
                log_debug("%s Added vertical spacing after direct row: +%.1fpx", table->source_loc(), table->tb->border_spacing_v);
            }
        }
    }  // End of ordered elements loop

    // NOTE: direct_rows are now processed in the main loop above as part of ordered_elements

    // =========================================================================
    // ROWSPAN HEIGHT DISTRIBUTION
    // Distribute rowspan cell heights proportionally across spanned rows
    // Must happen after single-row cells establish baseline heights
    // =========================================================================
    log_debug("%s Applying rowspan height distribution", table->source_loc());
    distribute_rowspan_heights(table, meta);

    // After distribution, update actual row heights to match meta->row_heights
    // This ensures rows reflect the distributed heights
    for_each_direct_table_block(table, [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            for_each_table_row_in_group(group, [&](ViewTableRow* trow, ViewBlock* row) {
                table_apply_rowspan_distributed_height(
                    lycon, table, meta, trow, row, "row");
            });
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewTableRow* trow = lam::view_require<RDT_VIEW_TABLE_ROW>(child);
            table_apply_rowspan_distributed_height(
                lycon, table, meta, trow, child, "direct row");
        }
    });

    // Rowspanning distribution changes row heights after the first layout pass.
    // Rebuild row y positions and the table cursor from the final row metadata
    // before computing the table height.
    current_y = reflow_table_rows_from_metadata(
        lycon, table, meta, ordered_elements, content_area_top_y);

    // Rowspanning cells span the final heights of all rows they cover.
    update_rowspan_cell_heights(table, meta);

    // Calculate final table height with border-spacing and padding
    float final_table_height = current_y;

    // CSS 2.1 Section 17.5.3: Handle explicit table height
    // If the table has an explicit height and content is smaller, distribute extra space to rows
    float explicit_css_height = 0;
    if (table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value) {
                // CRITICAL: Use the TABLE's font-size for resolving em units in height,
                // not the cell's font-size which may have polluted lycon->font.current_font_size.
                // CSS 2.1: "height: 4em" on the table uses the table's computed font-size.
                float saved_font_size = lycon->font.current_font_size;
                lycon->font.current_font_size = table_font_size;
                float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                lycon->font.current_font_size = saved_font_size;  // restore
                if (resolved_height > 0) {
                    explicit_css_height = resolved_height;
                    log_debug("%s Table has explicit CSS height: %.1fpx (resolved with table_font_size=%.1f)", table->source_loc(),
                             explicit_css_height, table_font_size);
                }
            }
        }
    }

    // Fallback to HTML height attribute (stored in blk->given_height) for auto layout
    if (explicit_css_height <= 0 && table->blk && table->blk->given_height > 0) {
        explicit_css_height = table->blk->given_height;
        log_debug("%s Table has explicit HTML height attribute: %.1fpx", table->source_loc(), explicit_css_height);
    }

    float constrained_css_height = layout_clamp_min_max_height(table, explicit_css_height);
    if (constrained_css_height != explicit_css_height) {
        log_debug("%s Table min/max-height applied: explicit_css_height %.1fpx -> %.1fpx",
                  table->source_loc(), explicit_css_height, constrained_css_height);
        explicit_css_height = constrained_css_height;
    }

    // Calculate what the minimum content height would be (including padding, borders, spacing)
    float min_content_height = current_y;
    float table_padding_vert = 0;
    float table_border_vert = 0;
    float table_spacing_vert = 0;

    if (table->bound && table->bound->padding.top >= 0) {
        table_padding_vert += table->bound->padding.top;
    }
    if (table->bound && table->bound->padding.bottom >= 0) {
        table_padding_vert += table->bound->padding.bottom;
    }
    if (table->bound && table->bound->border) {
        if (table->tb->border_collapse) {
            // CSS 2.1 §17.6.2: In border-collapse mode, the table's border box includes
            // only HALF of the collapsed border on each edge (shared with the outer cells).
            float collapsed_top = meta->collapsed_border_top;
            float collapsed_bottom = meta->collapsed_border_bottom;
            if (collapsed_top <= 0) collapsed_top = table->bound->border->width.top;
            if (collapsed_bottom <= 0) collapsed_bottom = table->bound->border->width.bottom;
            table_border_vert = collapsed_top / 2.0f + collapsed_bottom / 2.0f;
        } else {
            table_border_vert = layout_box_metrics(table).border_v;
        }
    }
    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
        table_spacing_vert = 2 * table->tb->border_spacing_v;  // Top and bottom edge spacing
    }

    float content_only_height = min_content_height - table_padding_vert;  // current_y includes top padding

    // If explicit height is larger than content, distribute extra height to rows
    // CSS 2.1 §17.5.3: Extra height is distributed to body rows only, not header/footer
    if (explicit_css_height > 0 && meta->row_count > 0) {
        // CSS 2.1 §17.6.2: In border-collapse, CSS height is border-box — subtract border.
        // CSS 2.1 §10.2: In separate borders, CSS height is content-box — border is additional.
        // Exception: box-sizing:border-box makes height border-box.
        bool auto_height_is_border_box = table->tb->border_collapse ||
            layout_uses_border_box(table);
        float available_for_content = auto_height_is_border_box
            ? explicit_css_height - table_border_vert
            : explicit_css_height;
        float extra_height = available_for_content - (content_only_height + table_padding_vert + table_spacing_vert);

        log_debug("%s Table height distribution: explicit=%.1f, available=%.1f, content_only=%.1f, initial_extra=%.1f, rows=%d", table->source_loc(),
                 explicit_css_height, available_for_content, content_only_height, extra_height, meta->row_count);

        if (extra_height > 0) {
            // CSS 2.1 table captions are outside the table grid box. The table
            // height applies to the grid; captions are added to the wrapper
            // before or after it, so they must not consume row height.
            TableHeightSectionSummary section_summary =
                table_collect_height_section_summary(table, meta);
            float non_body_grid_height = section_summary.non_body_grid_height;
            float body_natural_height = section_summary.body_natural_height;
            int body_row_count = section_summary.body_row_count;
            int section_count = section_summary.section_count;  // Count row-group sections for grid spacing
            float distributed_height_delta = 0.0f;

            float total_spacing = table_explicit_height_grid_spacing(table, meta, section_count);

            // Now calculate extra height available for body rows
            // Formula: extra_for_body = available - padding - all_grid_spacing - header/footer - body_natural
            float extra_for_body = available_for_content - table_padding_vert - total_spacing -
                                non_body_grid_height - body_natural_height;

            log_debug("%s Height breakdown: non_body_grid=%.1f, body_natural=%.1f, total_spacing=%.1f (sections=%d, rows=%d), padding=%.1f", table->source_loc(),
                     non_body_grid_height, body_natural_height, total_spacing, section_count, meta->row_count, table_padding_vert);
            log_debug("%s Distributing %.1fpx extra height to %d body rows (was %.1fpx initial)", table->source_loc(),
                     extra_for_body, body_row_count, extra_height);

            if (extra_for_body > 0 && body_row_count > 0) {
                // CSS 2.1 §17.5.3: Rows with percentage heights compute to auto and should
                // not receive extra height. Only distribute to rows without percentage heights.
                int eligible_row_count = 0;
                float eligible_height_total = 0.0f;
                for_each_table_body_group_row(table, [&](ViewTableRowGroup* group, ViewTableRow* trow) {
                    (void)group;
                    int row_idx = table_row_metadata_index_from_row(trow, -1);
                    if (row_idx >= 0 && row_idx < meta->row_count) {
                        // Only count rows that don't have percentage height
                        if (!meta->row_has_percent_height[row_idx]) {
                            eligible_row_count++;
                            if (meta->row_heights[row_idx] > 0.0f) {
                                eligible_height_total += meta->row_heights[row_idx];
                            }
                        }
                    }
                });

                log_debug("%s Eligible rows for height distribution: %d (of %d body rows)", table->source_loc(),
                         eligible_row_count, body_row_count);

                if (eligible_row_count > 0) {
                    distributed_height_delta = extra_for_body;
                    // First pass: update meta->row_heights for eligible body rows
                    for_each_table_body_group_row(table, [&](ViewTableRowGroup* group, ViewTableRow* trow) {
                        (void)group;
                        int row_idx = table_row_metadata_index_from_row(trow, -1);
                        if (row_idx < 0 || row_idx >= meta->row_count) return;
                        // Skip rows with percentage height
                        if (meta->row_has_percent_height[row_idx]) {
                            log_debug("%s     Skipping row %d (percentage height)", table->source_loc(), row_idx);
                            return;
                        }
                        table_apply_explicit_height_row_extra(
                            table, meta, row_idx, extra_for_body, eligible_row_count,
                            eligible_height_total, "Body", "     ");
                    });
                } else {
                    log_debug("%s No eligible rows for height distribution (all have percentage heights)", table->source_loc());
                }

                table_recalculate_explicit_height_row_y_positions(
                    table, meta, table_border_top, table_padding_top, caption, caption_height);
            } else if (extra_for_body > 0 && body_row_count == 0 && meta->row_count > 0) {
                // CSS Tables 3: no tbody rows exist — distribute extra height to all
                // rows in header/footer groups (thead/tfoot receive the space).
                // Use direct index-based iteration over meta->row_heights to avoid
                // missing rows that don't have RDT_VIEW_TABLE_ROW view children.
                // Also: do NOT exclude percent-height rows here — when there are no
                // body rows, ALL rows should participate in filling the table height.
                int eligible_row_count = meta->row_count;  // all rows are eligible
                log_debug("%s No-body-row header/footer distribution: %.1fpx extra to %d eligible rows", table->source_loc(),
                         extra_for_body, eligible_row_count);
                if (eligible_row_count > 0) {
                    distributed_height_delta = extra_for_body;
                    float eligible_height_total = 0.0f;
                    for (int r = 0; r < meta->row_count; r++) {
                        if (meta->row_heights[r] > 0.0f) {
                            eligible_height_total += meta->row_heights[r];
                        }
                    }
                    for (int r = 0; r < meta->row_count; r++) {
                        table_apply_explicit_height_row_extra(
                            table, meta, r, extra_for_body, eligible_row_count,
                            eligible_height_total, "Non-body", "   ");
                    }
                    table_recalculate_explicit_height_row_y_positions(
                        table, meta, table_border_top, table_padding_top, caption, caption_height);
                } else {
                    log_debug("%s No eligible non-body rows for height distribution", table->source_loc());
                }
            } else {
                log_debug("%s No body rows found, skipping height distribution", table->source_loc());
            }

            table_update_row_views_from_metadata(lycon, table, meta);

            // Update current_y to reflect expanded height
            current_y += distributed_height_delta;
            final_table_height = current_y;
            log_debug("%s Updated final_table_height to %.1f after height distribution (delta=%.1f)", table->source_loc(),
                      final_table_height, distributed_height_delta);

            // Third pass: recalculate row group y-positions and heights after height distribution.
            // The view update loop above computed group heights using stale group y positions,
            // so we must reposition groups sequentially and recalculate both row-relative
            // positions and group heights from the authoritative meta->row_y_positions.
            table_reposition_row_groups_from_metadata(table, meta);

            // Explicit table height changes row heights after the initial rowspan
            // pass, so rowspanning cell boxes must be refreshed from the final rows.
            update_rowspan_cell_heights(table, meta);
        }
    }

    // Save the row area height before padding/spacing/caption additions.
    // CSS 2.1 §17.2.1: Column/column-group elements span only the table row area,
    // not including captions, padding, or border-spacing at the table edges.
    float row_area_height = final_table_height - content_area_top_y;

    // Add table padding bottom
    // CSS 2.1 §17.6.2: Padding on table elements is ignored in border-collapse mode
    float table_padding_bottom = 0;
    if (!table->tb->border_collapse && table->bound && table->bound->padding.bottom >= 0) {
        table_padding_bottom = table->bound->padding.bottom;
        final_table_height += table_padding_bottom;
        log_debug("%s Added table padding bottom: +%.1fpx", table->source_loc(), table_padding_bottom);
    }

    // Add vertical border-spacing around table edges for separate border model
    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
        // Border-spacing adds space around the entire table perimeter
        // Bottom spacing around the table (top was already added)
        final_table_height += table->tb->border_spacing_v;
        log_debug("%s Added table edge bottom vertical spacing: +%.1fpx", table->source_loc(), table->tb->border_spacing_v);
    }

    // Position captions at bottom if caption-side is bottom (CSS 2.1 Section 17.4.1)
    if (captions->length > 0 && table->tb->caption_side == TableProp::CAPTION_SIDE_BOTTOM) {
        float total_bottom_caption_height = table_position_caption_stack(
            lycon, table, captions, final_table_height, table_width, wrapper_content_width,
            TABLE_CAPTION_WIDTH_REFERENCE_WRAPPER, TABLE_CAPTION_STACK_BOTTOM);
        final_table_height += total_bottom_caption_height;
        caption_height = total_bottom_caption_height;
        log_debug("%s Total bottom caption height: %.1f", table->source_loc(), total_bottom_caption_height);
    }

    // Override calculated height with explicit height if set and larger than content height
    // CSS 2.1 Section 17.5.3: If the table has an explicit height, use it
    // Note: The height distribution to rows was already done above (around line 4731)
    // Here we just ensure final_table_height respects the explicit height constraint.
    // final_table_height includes border_top (via current_y) but NOT border_bottom
    // (added separately below).
    // For border-box: explicit_css_height is border-box — subtract border_bottom for comparison.
    // For content-box: explicit_css_height is content — add border_top + padding for comparison.
    if (explicit_css_height > 0) {
        float css_height_comparable = explicit_css_height;
        bool height_css_is_border_box = table->tb->border_collapse ||
            layout_uses_border_box(table);
        if (height_css_is_border_box) {
            if (!table->tb->border_collapse && table->bound && table->bound->border) {
                css_height_comparable -= table->bound->border->width.bottom;
            }
        } else {
            // Content-box: add border_top + padding to make comparable with final_table_height
            if (table->bound) {
                if (table->bound->border)
                    css_height_comparable += table->bound->border->width.top;
                if (table->bound->padding.top >= 0)
                    css_height_comparable += table->bound->padding.top;
                if (table->bound->padding.bottom >= 0)
                    css_height_comparable += table->bound->padding.bottom;
            }
        }
        if (css_height_comparable > final_table_height) {
            log_debug("%s Explicit height override - changing final_table_height from %.1f to %.1f", table->source_loc(),
                   final_table_height, css_height_comparable);
            final_table_height = css_height_comparable;
        }
    }

    // When a table has an explicit height but contains no table-rows, expand any
    // row-group views directly to fill the available content height.
    // This handles CSS tables where display:table-header-group contains non-row content.
    if (meta->row_count == 0 && explicit_css_height > 0) {
        float bottom_overhead = table_padding_bottom;
        if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
            bottom_overhead += table->tb->border_spacing_v;
        }
        float available_for_groups = final_table_height - content_area_top_y - bottom_overhead;
        if (available_for_groups > 0) {
            int row_group_count = 0;
            for_each_direct_table_row_group(table, [&](ViewTableRowGroup* group, ViewBlock* child) {
                (void)group; (void)child;
                row_group_count++;
            });
            if (row_group_count > 0) {
                float height_per_group = available_for_groups / row_group_count;
                log_debug("%s No-row table: expanding %d row groups to %.1fpx each (available=%.1f)", table->source_loc(),
                         row_group_count, height_per_group, available_for_groups);
                for_each_direct_table_row_group(table, [&](ViewTableRowGroup* group, ViewBlock* child) {
                    (void)group;
                    if (height_per_group > child->height) {
                        child->height = height_per_group;
                    }
                });
            }
        }
    }

    // CRITICAL FIX: Handle table border dimensions correctly for each border model
    // In border-collapse mode, the table border overlaps with cell borders
    // In separate mode, the table border is added around the table
    float table_border_width = 0;
    float table_border_height = 0;

    if (table->tb->border_collapse) {
        // Border-collapse: CSS 2.1 Section 17.6.2
        // The table's border-box includes half of the collapsed outer borders.
        // Use the max resolved borders at each edge (from cells, rows, rowgroups, colgroups, table).
        // These were calculated in resolve_collapsed_borders() and stored in TableMetadata.
        float collapsed_left = meta->collapsed_border_left;
        float collapsed_right = meta->collapsed_border_right;
        float collapsed_bottom = meta->collapsed_border_bottom;

        // Width: table_border_left (half) is NOT in table_width, so add both halves
        table_border_width = collapsed_left / 2.0f + collapsed_right / 2.0f;
        // Height: half_top is already included in final_table_height
        // (via current_y which starts at half_top + padding_top for row positioning),
        // so only add half_bottom to avoid double-counting half_top.
        // This mirrors the separate border pattern where border_top is in current_y
        // and only border_bottom is added at the end.
        table_border_height = collapsed_bottom / 2.0f;
        log_debug("%s Border-collapse: using max resolved borders - left=%.1f, right=%.1f, bottom=%.1f", table->source_loc(),
               collapsed_left, collapsed_right, collapsed_bottom);
        log_debug("%s Border-collapse: adding half borders to dimensions: width+%.1f, height+%.1f (half_top already in current_y)", table->source_loc(),
               table_border_width, table_border_height);
    } else if (table->bound && table->bound->border) {
        // Separate borders: border_top is already included in final_table_height
        // (via current_y which starts at border_top + padding_top for row positioning),
        // so only add border_bottom to avoid double-counting border_top.
        table_border_width = layout_box_metrics(table).border_h;
        table_border_height = table->bound->border->width.bottom;
        log_debug("%s Separate borders: table border width=%.1fpx (left=%.1f, right=%.1f), height=%.1fpx (bottom=%.1f, top=%.1f already in current_y)", table->source_loc(),
               table_border_width, table->bound->border->width.left, table->bound->border->width.right,
               table_border_height, table->bound->border->width.bottom, table->bound->border->width.top);
    }

    // Set final table dimensions including border
    table->width = table_width + table_border_width;
    table->height = final_table_height + table_border_height;
    table->content_width = table_width;  // Content area excludes border
    // Content area (padding-box) excludes border: border_top was in final_table_height
    // via current_y, so subtract it for the padding-box content_height
    table->content_height = final_table_height - table_border_top;

    // CSS Tables 3: Table dimensions = max(styled_size, content_size).
    // max-width/max-height only affect the styled height used for row distribution
    // (already applied above at the explicit_css_height level). They must NOT shrink
    // below the natural content. Only min-width/min-height act as a floor here.
    // For content-box, given_min values are content-box — convert to content-box
    // for comparison, then convert back to border-box.
    bool table_is_content_box = !layout_uses_border_box(table);
    if (table_is_content_box && table->bound && !table->tb->border_collapse) {
        BoxMetrics table_box = layout_box_metrics(table);
        float pb_w = table_box.pad_border_h;
        float pb_h = table_box.pad_border_v;
        float content_w = max(table->width - pb_w, 0.0f);
        float content_h = max(table->height - pb_h, 0.0f);
        // Only apply min as a floor (not max)
        content_w = layout_floor_min_width(table, content_w);
        content_h = layout_floor_min_height(table, content_h);
        table->width = content_w + pb_w;
        table->height = content_h + pb_h;
    } else {
        // Border-box: only apply min as a floor
        if (table->blk) {
            table->width = layout_floor_min_width(table, table->width);
            table->height = layout_floor_min_height(table, table->height);
            // CSS Box Model: In border-box, the box width cannot be smaller than padding+border
            if (layout_uses_border_box(table) && table->bound) {
                BoxMetrics table_box = layout_box_metrics(table);
                float pad_border_w = table_box.pad_border_h;
                float pad_border_h = table_box.pad_border_v;
                if (table->width < pad_border_w) table->width = pad_border_w;
                if (table->height < pad_border_h) table->height = pad_border_h;
            }
        }
    }

    log_debug("%s Added table border: +%dpx width, +%dpx height", table->source_loc(),
           table_border_width, table_border_height);

    // CRITICAL: Also set ViewBlock height for block layout system integration
    // ViewTable inherits from ViewBlock, so block layout reads this field
    lam::view_require_block(table)->height = table->height;
    log_debug("%s Set ViewBlock height to %.1fpx for block layout integration (table ptr=%p)", table->source_loc(), table->height, table);

    log_debug("%s Table dimensions calculated: width=%dpx, height=%dpx (ptr=%p, table->width=%.1f, table->height=%.1f)", table->source_loc(),
              table_width, final_table_height, table, table->width, table->height);
    log_debug("%s Table layout complete: %.1fx%.1f", table->source_loc(), table_width, current_y);

    // CSS 2.1 §17.5.1: Set dimensions for column and column group elements
    // Column elements span the table row area only (not including captions)
    // Their width is determined by the computed column widths
    layout_column_elements(table, col_widths, col_x_positions, columns,
                           (float)row_area_height, (float)content_area_top_y);

    // Cleanup ArrayLists
    arraylist_free(body_groups);
    arraylist_free(ordered_elements);

    // Cleanup - TableMetadata destructor handles grid_occupied and col_widths
    table_metadata_destroy(meta);
    scratch_free(&lycon->scratch, col_x_positions);

    #undef GRID
}

// =============================================================================
// ORPHANED TABLE-INTERNAL ELEMENT HANDLING (CSS 2.1 Section 17.2.1)
// =============================================================================

/**
 * Check if a display value is a table-internal type (cell, row, row-group, etc.)
 * This does NOT include table/inline-table.
 */
bool is_table_internal_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_CELL ||
           display == CSS_VALUE_TABLE_ROW ||
           display == CSS_VALUE_TABLE_ROW_GROUP ||
           display == CSS_VALUE_TABLE_HEADER_GROUP ||
           display == CSS_VALUE_TABLE_FOOTER_GROUP ||
           display == CSS_VALUE_TABLE_COLUMN ||
           display == CSS_VALUE_TABLE_COLUMN_GROUP ||
           display == CSS_VALUE_TABLE_CAPTION;
}

/**
 * CSS 2.1 Section 17.2.1: Wrap orphaned table-internal children in anonymous table structures.
 *
 * This handles cases like:
 *   <div><span style="display:table-cell">...</span></div>
 *
 * Per CSS 2.1:
 * - If table-cell is not in table-row → wrap in anonymous table-row
 * - If table-row is not in table → wrap in anonymous table
 * - If table-row-group is not in table → wrap in anonymous table
 *
 * @param lycon Layout context
 * @param parent Parent element containing orphaned table-internal children
 * @return true if any wrapping was performed
 */
bool wrap_orphaned_table_children(LayoutContext* lycon, DomElement* parent) {
    if (!lycon || !parent || !parent->first_child) return false;

    Pool* pool = lycon->doc->view_tree->pool;
    if (!pool) return false;

    // First pass: check if any children have table-internal display
    // Note: We use resolve_display_value() directly because DomElement->display
    // is only set later during layout (on ViewBlock), so we need to read from
    // specified_style directly.
    bool has_table_internal = false;
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;

        // Use resolve_display_value to get display from specified_style
        DisplayValue child_display = resolve_display_value((void*)child);

        if (is_table_internal_display(child_display.inner)) {
            has_table_internal = true;
            break;
        }
    }

    if (!has_table_internal) {
        return false;
    }

    log_debug("%s [ORPHAN-TABLE] Found orphaned table-internal children in <%s>, creating anonymous wrappers", parent->source_loc(),
              parent->tag_name ? parent->tag_name : "unknown");

    // Collect runs of consecutive table-internal elements and wrap them
    DomNode* child = parent->first_child;
    bool wrapped_any = false;

    while (child) {
        // Skip non-elements and non-table-internal elements
        if (!child->is_element()) {
            child = child->next_sibling;
            continue;
        }

        // Use resolve_display_value to get display from specified_style
        DisplayValue child_display = resolve_display_value((void*)child);

        if (!is_table_internal_display(child_display.inner)) {
            child = child->next_sibling;
            continue;
        }

        // Found a table-internal element - collect consecutive run
        DomNode* run_start = child;
        DomNode* run_end = child;

        // Collect consecutive table-internal siblings (and any text/whitespace between them)
        // CSS 2.1 §17.2.1: Text nodes between table-internal elements are included in the
        // anonymous wrapper. Trailing whitespace-only text nodes are also included (they get
        // absorbed per §17.2.1 rule about whitespace adjacent to table elements).
        // Non-whitespace text nodes NOT followed by table-internal elements remain outside.
        while (run_end->next_sibling) {
            DomNode* next = run_end->next_sibling;
            if (next->is_element()) {
                DisplayValue next_display = resolve_display_value((void*)next);
                if (is_table_internal_display(next_display.inner)) {
                    run_end = next;
                } else {
                    break;
                }
            } else if (next->is_text()) {
                // CSS 2.1 §17.2.1: whitespace adjacent to table-internal boxes is
                // ignored for anonymous table object construction. Non-whitespace
                // text is ordinary flow content, so it terminates the current
                // orphaned table-internal run even when another table-cell follows.
                const unsigned char* text = next->text_data();
                bool is_whitespace_only = true;
                if (text) {
                    for (const unsigned char* p = text; *p; p++) {
                        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\f') {
                            is_whitespace_only = false;
                            break;
                        }
                    }
                }
                if (is_whitespace_only) {
                    run_end = next;  // absorb trailing whitespace
                    DomNode* after_text = next->next_sibling;
                    while (after_text && after_text->is_text()) {
                        const unsigned char* after = after_text->text_data();
                        bool after_is_whitespace_only = true;
                        if (after) {
                            for (const unsigned char* p = after; *p; p++) {
                                if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\f') {
                                    after_is_whitespace_only = false;
                                    break;
                                }
                            }
                        }
                        if (!after_is_whitespace_only) break;
                        after_text = after_text->next_sibling;
                    }
                    if (after_text && after_text->is_element()) {
                        DisplayValue after_display = resolve_display_value((void*)after_text);
                        if (is_table_internal_display(after_display.inner)) {
                            continue;
                        }
                    }
                }
                break;  // stop regardless (whitespace included or not)
            } else {
                break;
            }
        }

        // Determine what wrapper we need based on the child display types
        // CSS 2.1 §17.2.1: All orphaned table-internal elements need an anonymous table.
        // When the run contains ONLY cells (no rows/row-groups), also create an
        // anonymous row to wrap them directly (avoiding an unnecessary extra anon-tbody
        // level from generate_anonymous_table_boxes()). When there's a MIX of cells
        // and row-groups/rows, put cells into an anon-row and row-groups/rows directly
        // into the table — generate_anonymous_table_boxes() handles the rest.
        bool has_cells = false;
        bool has_rows_or_groups = false;
        bool needs_table = false;

        for (DomNode* n = run_start; n; n = n->next_sibling) {
            if (n->is_element()) {
                DisplayValue n_display = resolve_display_value((void*)n);
                CssEnum disp = n_display.inner;

                if (disp == CSS_VALUE_TABLE_CELL) {
                    has_cells = true;
                    needs_table = true;
                } else if (disp == CSS_VALUE_TABLE_CAPTION) {
                    // Orphan captions are proper table children, so they need the
                    // anonymous table wrapper but must not be wrapped in a row.
                    needs_table = true;
                } else if (disp == CSS_VALUE_TABLE_ROW ||
                           disp == CSS_VALUE_TABLE_ROW_GROUP ||
                           disp == CSS_VALUE_TABLE_HEADER_GROUP ||
                           disp == CSS_VALUE_TABLE_FOOTER_GROUP) {
                    has_rows_or_groups = true;
                    needs_table = true;
                }
            }
            if (n == run_end) break;
        }

        // Create anonymous wrappers
        DomElement* table_wrapper = nullptr;
        DomElement* row_wrapper = nullptr;

        if (needs_table) {
            // Create anonymous table
            // CSS 2.1 §17.2.1: If parent is inline, create inline-table (participates in inline flow)
            // Otherwise create block-level table
            DisplayValue parent_display = resolve_display_value((void*)parent);
            bool parent_is_inline = (parent_display.outer == CSS_VALUE_INLINE);

            table_wrapper = lam::pool_alloc_dom_element(pool);
            if (table_wrapper) {
                table_wrapper->node_type = DOM_NODE_ELEMENT;
                dom_element_retain_tag_name(table_wrapper, lam::borrow_const(lam::promote_to_pool(pool, "::anon-table")));
                table_wrapper->doc = parent->doc;
                table_wrapper->display.outer = parent_is_inline ? CSS_VALUE_INLINE : CSS_VALUE_BLOCK;
                table_wrapper->display.inner = CSS_VALUE_TABLE;  // Inner is table layout
                table_wrapper->styles_resolved = true;

                // CSS 2.1 §17.2.1: Anonymous boxes inherit inheritable properties.
                // Use parent->font if available; otherwise fall back to the current
                // layout font context (lycon->font.style), which holds the computed
                // inherited font for elements that don't declare font properties.
                FontProp* inherit_font = parent->font ? parent->font : lycon->font.style;
                if (inherit_font) {
                    table_wrapper->font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
                    if (table_wrapper->font) {
                        // Copy only specified font properties, not derived/cached fields
                        radiant_retain_font_family(table_wrapper->font, lam::PoolPtr<char>(inherit_font->family));
                        table_wrapper->font->font_size = inherit_font->font_size;
                        table_wrapper->font->font_style = inherit_font->font_style;
                        table_wrapper->font->font_weight = inherit_font->font_weight;
                        table_wrapper->font->font_variant = inherit_font->font_variant;
                        table_wrapper->font->text_deco = inherit_font->text_deco;
                        table_wrapper->font->text_deco_color = inherit_font->text_deco_color;
                        table_wrapper->font->text_deco_style = inherit_font->text_deco_style;
                        table_wrapper->font->text_deco_thickness = inherit_font->text_deco_thickness;
                        table_wrapper->font->text_underline_offset = inherit_font->text_underline_offset;
                        table_wrapper->font->letter_spacing = inherit_font->letter_spacing;
                        table_wrapper->font->word_spacing = inherit_font->word_spacing;
                        // Derived fields left zero/NULL from pool_calloc
                    }
                }
                InlineProp* inherit_inline = parent->in_line;
                if (!inherit_inline) {
                    // Fall back to lycon color context if parent has no explicit inline props
                    // The color is inherited and tracked in the layout context
                }
                if (inherit_inline) {
                    table_wrapper->in_line = (InlineProp*)pool_calloc(pool, sizeof(InlineProp));
                    if (table_wrapper->in_line) {
                        table_wrapper->in_line->color = inherit_inline->color;
                        table_wrapper->in_line->has_color = inherit_inline->has_color;
                        table_wrapper->in_line->visibility = inherit_inline->visibility;
                        table_wrapper->in_line->opacity = 1.0f;
                    }
                }

                log_debug("%s [ORPHAN-TABLE] Created anonymous table wrapper (font from %s)", parent->source_loc(),
                          parent->font ? "parent" : "lycon context");
            }
        }

        // Create anonymous row for cells when needed:
        // - cells-only: create anon-tr as sole child of anon-table
        // - mixed cells + rows/groups: create anon-tr for cells, rows/groups go directly in table
        if (has_cells && table_wrapper) {
            row_wrapper = lam::pool_alloc_dom_element(pool);
            if (row_wrapper) {
                row_wrapper->node_type = DOM_NODE_ELEMENT;
                dom_element_retain_tag_name(row_wrapper, lam::borrow_const(lam::promote_to_pool(pool, "::anon-tr")));
                row_wrapper->doc = parent->doc;
                row_wrapper->parent = table_wrapper;
                row_wrapper->display.outer = CSS_VALUE_BLOCK;
                row_wrapper->display.inner = CSS_VALUE_TABLE_ROW;
                row_wrapper->styles_resolved = true;

                if (table_wrapper->font) {
                    row_wrapper->font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
                    if (row_wrapper->font) {
                        radiant_retain_font_family(row_wrapper->font, lam::PoolPtr<char>(table_wrapper->font->family));
                        row_wrapper->font->font_size = table_wrapper->font->font_size;
                        row_wrapper->font->font_style = table_wrapper->font->font_style;
                        row_wrapper->font->font_weight = table_wrapper->font->font_weight;
                        row_wrapper->font->font_variant = table_wrapper->font->font_variant;
                        row_wrapper->font->text_deco = table_wrapper->font->text_deco;
                        row_wrapper->font->text_deco_color = table_wrapper->font->text_deco_color;
                        row_wrapper->font->text_deco_style = table_wrapper->font->text_deco_style;
                        row_wrapper->font->text_deco_thickness = table_wrapper->font->text_deco_thickness;
                        row_wrapper->font->text_underline_offset = table_wrapper->font->text_underline_offset;
                        row_wrapper->font->letter_spacing = table_wrapper->font->letter_spacing;
                        row_wrapper->font->word_spacing = table_wrapper->font->word_spacing;
                    }
                }
                if (table_wrapper->in_line) {
                    row_wrapper->in_line = (InlineProp*)pool_calloc(pool, sizeof(InlineProp));
                    if (row_wrapper->in_line) {
                        row_wrapper->in_line->color = table_wrapper->in_line->color;
                        row_wrapper->in_line->has_color = table_wrapper->in_line->has_color;
                        row_wrapper->in_line->visibility = table_wrapper->in_line->visibility;
                        row_wrapper->in_line->opacity = 1.0f;
                    }
                }

                log_debug("%s [ORPHAN-TABLE] Created anonymous table-row wrapper", parent->source_loc());
            }
        }

        if (table_wrapper) {
            // Insert the anonymous table at run_start's position
            DomNode* prev = run_start->prev_sibling;
            DomNode* next_after_run = run_end->next_sibling;

            // Link table_wrapper into parent's child list
            table_wrapper->parent = parent;
            table_wrapper->prev_sibling = prev;
            table_wrapper->next_sibling = next_after_run;

            if (prev) {
                prev->next_sibling = table_wrapper;
            } else {
                parent->first_child = table_wrapper;
            }

            if (next_after_run) {
                next_after_run->prev_sibling = table_wrapper;
            } else {
                parent->last_child = table_wrapper;
            }

            // Move children into the appropriate wrapper:
            // - Cells go into row_wrapper (if present)
            // - Rows/row-groups go directly into table_wrapper
            // - When no mix (cells-only), all go into row_wrapper
            // - When no cells, all go directly into table_wrapper
            bool has_mix = has_cells && has_rows_or_groups;
            bool row_added_to_table = false;

            DomNode* move_node = run_start;
            while (move_node) {
                DomNode* next_to_move = move_node->next_sibling;
                bool is_last = (move_node == run_end);

                // Determine where this node goes
                DomElement* target = table_wrapper;  // default: direct child of table

                if (row_wrapper) {
                    if (has_mix && move_node->is_element()) {
                        DisplayValue n_display = resolve_display_value((void*)move_node);
                        CssEnum disp = n_display.inner;
                        if (disp == CSS_VALUE_TABLE_ROW ||
                            disp == CSS_VALUE_TABLE_ROW_GROUP ||
                            disp == CSS_VALUE_TABLE_HEADER_GROUP ||
                            disp == CSS_VALUE_TABLE_FOOTER_GROUP) {
                            // Row/row-group: goes directly into table
                            // But first, flush the row_wrapper if it has children
                            if (!row_added_to_table && row_wrapper->first_child) {
                                append_child_to_element(table_wrapper, row_wrapper);
                                row_added_to_table = true;
                            }
                            target = table_wrapper;
                        } else {
                            // Cells and other content: go into row_wrapper
                            target = row_wrapper;
                        }
                    } else {
                        // Cells-only: all go into row_wrapper
                        target = row_wrapper;
                    }
                }

                // Reparent this node
                move_node->parent = target;
                move_node->prev_sibling = target->last_child;
                move_node->next_sibling = nullptr;

                if (target->last_child) {
                    target->last_child->next_sibling = move_node;
                } else {
                    target->first_child = move_node;
                }
                target->last_child = move_node;

                if (is_last) break;
                move_node = next_to_move;
            }

            // If row_wrapper has children and hasn't been added to table yet, add it
            if (row_wrapper && row_wrapper->first_child && !row_added_to_table) {
                // For cells-only case or when row comes at the end
                append_child_to_element(table_wrapper, row_wrapper);
                // Move all row_wrapper's children to be under the table's child list
                // by making the row_wrapper a child of the table
                // (row_wrapper is already set up as child above via append_child_to_element)
            } else if (row_wrapper && !row_wrapper->first_child) {
                // row_wrapper was created but nothing was added to it (shouldn't happen)
                // Just ignore it
            }

            wrapped_any = true;

            // Continue from after the wrapper
            child = table_wrapper->next_sibling;
        } else {
            child = run_end->next_sibling;
        }
    }

    return wrapped_any;
}

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

// Main table layout entry point
void layout_table_content(LayoutContext* lycon, DomNode* tableNode, DisplayValue display) {
    if (!tableNode) {
        log_debug("layout_table_content: null tableNode, returning");
        return;
    }
    log_debug("%s Initial layout context - line.left=%d, advance_y=%d", tableNode->source_loc(), lycon->line.left, lycon->block.advance_y);

    // CRITICAL: Save the table's font-size BEFORE building table tree.
    // Cell layout in build_table_tree will modify lycon->font to the cell's font-size,
    // but the table's CSS properties (like height: 4em) should use the table's font-size.
    float table_font_size = 16.0f;  // default
    if (lycon->font.current_font_size > 0) {
        table_font_size = lycon->font.current_font_size;
    } else if (lycon->font.style && lycon->font.style->font_size > 0) {
        table_font_size = lycon->font.style->font_size;
    }
    log_debug("%s Saved table font-size: %.1fpx for later height resolution", tableNode->source_loc(), table_font_size);

    // CRITICAL: Update font context before building table tree
    // This ensures children inherit the correct computed font-size from the table element.
    // Without this, lycon->font.style would still point to the grandparent's font.
    // Use tableNode->font directly (safe) instead of casting lycon->view to ViewTable*
    // (which may not be a ViewTable yet — it's the parent's view at this point).
    DomElement* table_element = tableNode->is_element() ? lam::dom_require<DOM_NODE_ELEMENT>(tableNode) : nullptr;
    if (table_element && table_element->font) {
        log_debug("%s Table font context check: table_element=%p, font=%p, font_size=%.1f", tableNode->source_loc(),
            (void*)table_element, (void*)table_element->font, table_element->font->font_size);
        setup_font(lycon->ui_context, &lycon->font, table_element->font);
        log_debug("%s Updated font context for table: font-size=%.1f", tableNode->source_loc(), table_element->font->font_size);
    } else {
        log_debug("%s WARNING: table font is NULL, cannot update font context", tableNode->source_loc());
    }

    // Ensure the table has proper ViewTable setup.
    // When a table is an absolutely positioned child of a grid/flex container,
    // init_grid_item_view/init_flex_item_view sets view_type=RDT_VIEW_BLOCK and
    // item_prop_type=ITEM_PROP_GRID/FLEX without allocating TableProp.
    // We must allocate tb before build_table_tree accesses it.
    if (tableNode->is_element()) {
        ViewTable* vtable = lam::unsafe_view_table_storage(tableNode);
        if (vtable->item_prop_type != DomElement::ITEM_PROP_TABLE) {
            vtable->tb = (TableProp*)alloc_prop(lycon, sizeof(TableProp));
            vtable->item_prop_type = DomElement::ITEM_PROP_TABLE;
            vtable->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;
            vtable->tb->border_spacing_h = 0.0f;
            vtable->tb->border_spacing_v = 0.0f;
            vtable->tb->border_collapse = false;
            vtable->tb->is_annoy_tbody = 0;
            vtable->tb->is_annoy_tr = 0;
            vtable->tb->is_annoy_td = 0;
            vtable->tb->is_annoy_colgroup = 0;
            vtable->view_type = RDT_VIEW_TABLE;
        }
        lycon->view = static_cast<View*>(vtable);
    }

    // Step 1: Build table structure from DOM
    ViewTable* table = build_table_tree(lycon, tableNode);
    if (!table) {
        log_error("%s Failed to build table structure", tableNode->source_loc());
        return;
    }

    // Store the table's font-size in TableProp for use during height resolution
    if (table->tb) {
        table->tb->computed_font_size = table_font_size;
    }

    // Step 1.5: Detect and mark anonymous box wrappers
    detect_anonymous_boxes(table);

    // Step 2: Calculate layout
    table_auto_layout(lycon, table);
    log_debug("%s Table layout calculated: %dx%d", tableNode->source_loc(), table->width, table->height);

    // Step 3: Update layout context for proper block integration
    // CRITICAL: Set advance_y to table height so finalize_block_flow works correctly
    // The block layout system uses advance_y to calculate the final block height
    lycon->block.advance_y = table->height;

    // CRITICAL FIX: Update max_width for inline-table elements
    // finalize_block_flow uses lycon->block.max_width to calculate flow_width:
    //   content_width = max_width + padding.right
    //   flow_width = content_width + border.right
    // So max_width must be table->width (border-box) MINUS padding.right and border.right,
    // because finalize_block_flow will add them back.
    {
        float sub_right = 0;
        if (table->bound) {
            sub_right += table->bound->padding.right;
            if (table->bound->border)
                sub_right += table->bound->border->width.right;
        }
        lycon->block.max_width = table->width - sub_right;
    }

    // CRITICAL FIX: Ensure proper line state management for tables
    // Tables are block-level elements and should not participate in line layout
    // Set is_line_start = true to prevent parent from calling line_break()
    lycon->line.is_line_start = true;

    // CSS Position 3 §3.4: Apply relative/sticky positioning to table sub-elements
    // (table cells, rows, row groups, captions) after all table layout is finalized.
    // Table-internal elements can be relatively positioned per CSS 2.1 §17.5.1.
    for_each_direct_table_block(table, [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            table_apply_positioned_layout(lycon, child);
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            for_each_table_row_in_group(group, [&](ViewTableRow* row, ViewBlock* row_block) {
                (void)row_block;
                table_apply_positioned_row(lycon, row);
            });
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            table_apply_positioned_row(lycon, lam::view_require<RDT_VIEW_TABLE_ROW>(child));
        } else {
            table_apply_positioned_layout(lycon, child);
        }
    });

}
