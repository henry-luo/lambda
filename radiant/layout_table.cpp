#include "layout_table.hpp"
#include "layout.hpp"
#include "intrinsic_sizing.hpp"
#include "../lib/log.h"
#include "../lambda/input/css/dom_element.hpp"
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

ViewTableRow* ViewTable::first_row() {
    // If table acts as its own tbody (is_annoy_tbody), rows are direct children
    if (acts_as_tbody()) {
        for (ViewBlock* child = (ViewBlock*)first_child; child; child = (ViewBlock*)child->next_sibling) {
            if (child->view_type == RDT_VIEW_TABLE_ROW) {
                return (ViewTableRow*)child;
            }
            // Also check for cells if table acts as row too
            if (acts_as_row() && child->view_type == RDT_VIEW_TABLE_CELL) {
                // Table is acting as both tbody and tr - cells are direct children
                // Return nullptr for row iteration; use cell iteration instead
                return nullptr;
            }
        }
    }

    // Otherwise, look in row groups
    for (ViewBlock* child = (ViewBlock*)first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = (ViewTableRowGroup*)child;
            ViewTableRow* row = group->first_row();
            if (row) return row;
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            // Direct row child (table acting as tbody)
            return (ViewTableRow*)child;
        }
    }
    return nullptr;
}

ViewBlock* ViewTable::first_row_group() {
    // If table acts as its own tbody, return the table itself
    if (acts_as_tbody()) {
        return this;
    }

    // Otherwise, find first actual row group
    for (ViewBlock* child = (ViewBlock*)first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            return child;
        }
    }
    return nullptr;
}

ViewTableRow* ViewTable::next_row(ViewTableRow* current) {
    if (!current) return nullptr;

    // First try next sibling in same parent
    for (ViewBlock* sibling = (ViewBlock*)current->next_sibling; sibling; sibling = (ViewBlock*)sibling->next_sibling) {
        if (sibling->view_type == RDT_VIEW_TABLE_ROW) {
            return (ViewTableRow*)sibling;
        }
    }

    // If no more rows in current group, try next row group
    ViewBlock* parent = (ViewBlock*)current->parent;
    if (parent && parent->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
        // Find next row group
        for (ViewBlock* next_group = (ViewBlock*)parent->next_sibling; next_group; next_group = (ViewBlock*)next_group->next_sibling) {
            if (next_group->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                ViewTableRowGroup* group = (ViewTableRowGroup*)next_group;
                ViewTableRow* row = group->first_row();
                if (row) return row;
            }
        }
    }

    return nullptr;
}

ViewTableRow* ViewTableRowGroup::first_row() {
    for (ViewBlock* child = (ViewBlock*)first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW) {
            return (ViewTableRow*)child;
        }
    }
    return nullptr;
}

ViewTableRow* ViewTableRowGroup::next_row(ViewTableRow* current) {
    if (!current) return nullptr;

    for (ViewBlock* sibling = (ViewBlock*)current->next_sibling; sibling; sibling = (ViewBlock*)sibling->next_sibling) {
        if (sibling->view_type == RDT_VIEW_TABLE_ROW) {
            return (ViewTableRow*)sibling;
        }
    }
    return nullptr;
}

ViewTableCell* ViewTableRow::first_cell() {
    for (ViewBlock* child = (ViewBlock*)first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_CELL) {
            return (ViewTableCell*)child;
        }
    }
    return nullptr;
}

ViewTableCell* ViewTableRow::next_cell(ViewTableCell* current) {
    if (!current) return nullptr;

    for (ViewBlock* sibling = (ViewBlock*)current->next_sibling; sibling; sibling = (ViewBlock*)sibling->next_sibling) {
        if (sibling->view_type == RDT_VIEW_TABLE_CELL) {
            return (ViewTableCell*)sibling;
        }
    }
    return nullptr;
}

ViewBlock* ViewTableRow::parent_row_group() {
    ViewBlock* parent = (ViewBlock*)this->parent;
    if (parent && (parent->view_type == RDT_VIEW_TABLE_ROW_GROUP || parent->view_type == RDT_VIEW_TABLE)) {
        return parent;
    }
    return nullptr;
}

// Get first cell when table acts as its own row (cells are direct children)
ViewTableCell* ViewTable::first_direct_cell() {
    if (!acts_as_row()) return nullptr;

    for (ViewBlock* child = (ViewBlock*)first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_CELL) {
            return (ViewTableCell*)child;
        }
    }
    return nullptr;
}

// Get next cell when table acts as its own row
ViewTableCell* ViewTable::next_direct_cell(ViewTableCell* current) {
    if (!current || !acts_as_row()) return nullptr;

    for (ViewBlock* sibling = (ViewBlock*)current->next_sibling; sibling; sibling = (ViewBlock*)sibling->next_sibling) {
        if (sibling->view_type == RDT_VIEW_TABLE_CELL) {
            return (ViewTableCell*)sibling;
        }
    }
    return nullptr;
}

// =============================================================================
// CELL HELPER FUNCTIONS
// =============================================================================
// Common operations for table cell layout to reduce code duplication.

// Forward declaration for layout_table_cell_content (defined later in the file)
static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell);

// Get CSS width from a cell element, handling percentage and length values
// Returns 0 if no explicit width is set
static int get_cell_css_width(LayoutContext* lycon, ViewTableCell* tcell, int table_content_width) {
    if (tcell->node_type != DOM_NODE_ELEMENT) return 0;

    DomElement* dom_elem = tcell->as_element();
    if (!dom_elem || !dom_elem->specified_style) return 0;

    CssDeclaration* width_decl = style_tree_get_declaration(
        dom_elem->specified_style, CSS_PROPERTY_WIDTH);
    if (!width_decl || !width_decl->value) return 0;

    int cell_width = 0;
    int css_content_width = 0;

    if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE && table_content_width > 0) {
        double percentage = width_decl->value->data.percentage.value;
        css_content_width = (int)(table_content_width * percentage / 100.0);
        cell_width = css_content_width;
    } else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        float resolved = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
        css_content_width = (int)resolved;
        if (css_content_width > 0) {
            cell_width = css_content_width;
        }
    }

    if (cell_width <= 0) return 0;

    // Add padding (CSS width is content-box)
    if (tcell->bound && tcell->bound->padding.left >= 0 && tcell->bound->padding.right >= 0) {
        cell_width += tcell->bound->padding.left + tcell->bound->padding.right;
    }

    // Add border width (only if border-style is not none)
    if (tcell->bound && tcell->bound->border) {
        float border_left = (tcell->bound->border->left_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.left : 0;
        float border_right = (tcell->bound->border->right_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.right : 0;
        cell_width += (int)(border_left + border_right);
    }

    return cell_width;
}

// Get explicit CSS height from a cell or block element
// Returns 0 if no explicit height is set
static int get_explicit_css_height(LayoutContext* lycon, ViewBlock* element) {
    if (element->node_type != DOM_NODE_ELEMENT) return 0;

    DomElement* dom_elem = element->as_element();
    if (!dom_elem || !dom_elem->specified_style) return 0;

    CssDeclaration* height_decl = style_tree_get_declaration(
        dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
    if (!height_decl || !height_decl->value) return 0;

    float resolved = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
    return (resolved > 0) ? (int)resolved : 0;
}

// Check if a table cell is empty (has no content)
// CSS 2.1 Section 17.6.1: A cell is empty if it contains no in-flow content
// (text nodes with only whitespace are considered empty, but replaced elements are content)
static bool is_cell_empty(ViewTableCell* cell) {
    DomNode* child = ((DomElement*)cell)->first_child;
    while (child) {
        if (child->is_element()) {
            // Element child = has content (not empty)
            return false;
        }
        if (child->is_text()) {
            // Check if text is only whitespace
            const char* text = ((DomText*)child)->text;
            if (text) {
                for (const char* p = text; *p; p++) {
                    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                        // Non-whitespace content found
                        return false;
                    }
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

// Measure content height from cell's children
static int measure_cell_content_height(LayoutContext* lycon, ViewTableCell* tcell) {
    int content_height = 0;

    for (View* child = ((ViewGroup*)tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            int text_height = text->height > 0 ? text->height : 17;

            // Use line-height from context if available (CSS line-height)
            // This ensures proper vertical spacing for table cells
            if (lycon->block.line_height > 0 && lycon->block.line_height > text_height) {
                text_height = (int)lycon->block.line_height;
            }

            if (text_height > content_height) content_height = text_height;
        }
        else if (child->view_type == RDT_VIEW_BLOCK ||
                 child->view_type == RDT_VIEW_INLINE ||
                 child->view_type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* block = (ViewBlock*)child;
            int child_css_height = get_explicit_css_height(lycon, block);
            int child_height = (child_css_height > 0) ? child_css_height : block->height;
            if (child_height > content_height) content_height = child_height;
        }
    }

    // Ensure minimum content height
    return (content_height < 17) ? 17 : content_height;
}

// Calculate final cell height from content, padding, border
static int calculate_cell_height(LayoutContext* lycon, ViewTableCell* tcell, ViewTable* table,
                                  int content_height, int explicit_height) {
    if (explicit_height > 0) {
        return explicit_height;
    }

    int cell_height = content_height;

    // Add padding
    if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
        cell_height += tcell->bound->padding.top + tcell->bound->padding.bottom;
    }

    // Add border based on collapse mode
    if (tcell->bound && tcell->bound->border) {
        float border_top = (tcell->bound->border->top_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.top : 0;
        float border_bottom = (tcell->bound->border->bottom_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.bottom : 0;

        if (table->tb->border_collapse) {
            cell_height += (int)((border_top + border_bottom) / 2);
        } else {
            cell_height += (int)(border_top + border_bottom);
        }
    }

    return cell_height;
}

// Apply vertical alignment to cell children
static void apply_cell_vertical_align(ViewTableCell* tcell, int cell_height, int content_height) {
    log_debug("apply_cell_vertical_align: valign=%d, cell_height=%d, content_height=%d",
           tcell->td->vertical_align, cell_height, content_height);
    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_TOP) {
        return; // No adjustment needed
    }

    // Calculate content area (subtract border and padding)
    int cell_content_area = cell_height - 2; // Approximate border
    if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
        cell_content_area -= (tcell->bound->padding.top + tcell->bound->padding.bottom);
    }

    int y_adjustment = 0;
    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_MIDDLE) {
        y_adjustment = (cell_content_area - content_height) / 2;
    } else if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BOTTOM) {
        y_adjustment = cell_content_area - content_height;
    }

    if (y_adjustment > 0) {
        for (View* child = ((ViewGroup*)tcell)->first_child; child; child = child->next_sibling) {
            child->y += y_adjustment;
            // Also update TextRect for ViewText nodes
            if (child->view_type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)child;
                if (text->rect) {
                    text->rect->y += y_adjustment;
                }
            }
        }
    }
}

// Position text children within a cell (relative coordinates)
static void position_cell_text_children(ViewTableCell* tcell) {
    int content_x = 1; // 1px border
    int content_y = 1;

    if (tcell->bound) {
        content_x += tcell->bound->padding.left;
        content_y += tcell->bound->padding.top;
    }

    for (View* child = ((ViewGroup*)tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            child->x = content_x;
            child->y = content_y;
        }
    }
}

// Calculate cell width from column widths (for colspan support)
static int calculate_cell_width_from_columns(ViewTableCell* tcell, int* col_widths, int columns) {
    int cell_width = 0;
    int end_col = tcell->td->col_index + tcell->td->col_span;
    for (int c = tcell->td->col_index; c < end_col && c < columns; c++) {
        cell_width += col_widths[c];
    }
    return cell_width;
}

// Process a single cell: position, size, layout content, apply alignment
// Returns the height contribution for the current row (adjusted for rowspan)
static int process_table_cell(LayoutContext* lycon, ViewTableCell* tcell, ViewTable* table,
                               int* col_widths, int* col_x_positions, int columns) {
    ViewBlock* cell = (ViewBlock*)tcell;

    // Check if this empty cell should have its border/background hidden
    // CSS 2.1 Section 17.6.1: In separated borders model, empty cells can have
    // their borders and backgrounds hidden based on empty-cells property
    if (tcell->td->is_empty && !table->tb->border_collapse &&
        table->tb->empty_cells == TableProp::EMPTY_CELLS_HIDE) {
        tcell->td->hide_empty = 1;
        log_debug("Cell at col=%d row=%d: hide_empty=1 (empty + empty-cells:hide)",
            tcell->td->col_index, tcell->td->row_index);
    } else {
        tcell->td->hide_empty = 0;
    }

    // Position cell relative to row
    cell->x = col_x_positions[tcell->td->col_index] - col_x_positions[0];
    cell->y = 0;

    // Position text children within cell
    position_cell_text_children(tcell);

    // Calculate cell width from columns
    cell->width = calculate_cell_width_from_columns(tcell, col_widths, columns);

    // Layout cell content now that width is set
    layout_table_cell_content(lycon, cell);

    // Get explicit CSS height and measure content
    int explicit_cell_height = get_explicit_css_height(lycon, cell);
    int content_height = measure_cell_content_height(lycon, tcell);

    // Calculate final cell height
    int cell_height = calculate_cell_height(lycon, tcell, table, content_height, explicit_cell_height);
    cell->height = cell_height;

    // Apply vertical alignment
    apply_cell_vertical_align(tcell, cell_height, content_height);

    // Handle rowspan for row height calculation
    int height_for_row = cell_height;
    if (tcell->td->row_span > 1) {
        height_for_row = cell_height / tcell->td->row_span;
        log_debug("Rowspan cell - total_height=%d, rowspan=%d, height_for_row=%d",
                  cell_height, tcell->td->row_span, height_for_row);
    }

    return height_for_row;
}

// Apply fixed row height to row and all its cells
// Forward declaration
static int measure_cell_content_height(LayoutContext* lycon, ViewTableCell* tcell);
static void apply_cell_vertical_align(ViewTableCell* tcell, int cell_height, int content_height);

static void apply_fixed_row_height(LayoutContext* lycon, ViewTableRow* trow, int fixed_height) {
    trow->height = fixed_height;
    log_debug("Applied fixed layout row height: %dpx", fixed_height);

    for (ViewTableCell* cell = trow->first_cell(); cell; cell = trow->next_cell(cell)) {
        if (cell->height < fixed_height) {
            cell->height = fixed_height;
            // Re-apply vertical alignment with correct cell height
            int content_height = measure_cell_content_height(lycon, cell);
            apply_cell_vertical_align(cell, fixed_height, content_height);
        }
    }
}

// =============================================================================
// INTERNAL DATA STRUCTURES
// =============================================================================

// Table metadata cache - Phase 3 optimization
// Stores pre-analyzed table structure to avoid multiple DOM iterations
struct TableMetadata {
    int column_count;           // Total columns
    int row_count;              // Total rows
    bool* grid_occupied;        // colspan/rowspan tracking (row_count × column_count)
    int* col_widths;            // Final column widths
    int* col_min_widths;        // Minimum column widths (future)
    int* col_max_widths;        // Maximum column widths (future)
    int* row_heights;           // Row heights for rowspan calculation
    int* row_y_positions;       // Row Y positions for rowspan calculation
    bool* row_collapsed;        // Visibility: collapse tracking per row

    TableMetadata(int cols, int rows)
        : column_count(cols), row_count(rows) {
        grid_occupied = (bool*)calloc(rows * cols, sizeof(bool));
        col_widths = (int*)calloc(cols, sizeof(int));
        col_min_widths = (int*)calloc(cols, sizeof(int));  // Minimum content widths (CSS MCW)
        col_max_widths = (int*)calloc(cols, sizeof(int));  // Preferred content widths (CSS PCW)
        row_heights = (int*)calloc(rows, sizeof(int));
        row_y_positions = (int*)calloc(rows, sizeof(int));
        row_collapsed = (bool*)calloc(rows, sizeof(bool));
    }

    ~TableMetadata() {
        free(grid_occupied);
        free(col_widths);
        free(col_min_widths);
        free(col_max_widths);
        free(row_heights);
        free(row_y_positions);
        free(row_collapsed);
    }

    // Grid accessor
    inline bool& grid(int row, int col) {
        return grid_occupied[row * column_count + col];
    }
};

// =============================================================================
// CSS PROPERTY PARSING
// =============================================================================

// Parse table-specific CSS properties from DOM element
static void resolve_table_properties(LayoutContext* lycon, DomNode* element, ViewTable* table) {
    // Read CSS border-collapse and border-spacing properties first
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
                    if (val->data.keyword == CSS_VALUE_COLLAPSE || val->data.keyword == CSS_VALUE_COLLAPSE_TABLE) {
                        table->tb->border_collapse = true;
                        log_debug("Table border-collapse: collapse (true)");
                    } else if (val->data.keyword == CSS_VALUE_SEPARATE) {
                        table->tb->border_collapse = false;
                        log_debug("Table border-collapse: separate (false)");
                    }
                }
            }

            // Read border-spacing property (204)
            CssDeclaration* spacing_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_BORDER_SPACING);

            if (spacing_decl && spacing_decl->value) {
                CssValue* val = (CssValue*)spacing_decl->value;

                // border-spacing can be a single length or a list of two lengths
                if (val->type == CSS_VALUE_TYPE_LENGTH) {
                    // Single value applies to both horizontal and vertical
                    // Use resolve_length_value for em/rem support
                    float resolved = resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING, val);
                    table->tb->border_spacing_h = resolved;
                    table->tb->border_spacing_v = resolved;
                    log_debug("Table border-spacing: %.2fpx (both h and v, resolved)", resolved);
                } else if (val->type == CSS_VALUE_TYPE_LIST && val->data.list.count >= 2) {
                    // Two values: horizontal and vertical
                    CssValue* h_val = val->data.list.values[0];
                    CssValue* v_val = val->data.list.values[1];

                    if (h_val) {
                        float h_resolved = resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING, h_val);
                        table->tb->border_spacing_h = h_resolved;
                        log_debug("Table border-spacing horizontal: %.2fpx (resolved)", h_resolved);
                    }
                    if (v_val) {
                        float v_resolved = resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING, v_val);
                        table->tb->border_spacing_v = v_resolved;
                        log_debug("Table border-spacing vertical: %.2fpx (resolved)", v_resolved);
                    }
                } else if (val->type == CSS_VALUE_TYPE_NUMBER) {
                    // Handle numeric values (convert to length)
                    float spacing = (float)val->data.number.value;
                    table->tb->border_spacing_h = spacing;
                    table->tb->border_spacing_v = spacing;
                    log_debug("Table border-spacing: %.2fpx (numeric, both h and v)", spacing);
                }
            }

            // Read caption-side property (CSS 2.1 Section 17.4.1)
            CssDeclaration* caption_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_CAPTION_SIDE);

            if (caption_decl && caption_decl->value) {
                CssValue* val = (CssValue*)caption_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    if (val->data.keyword == CSS_VALUE_BOTTOM) {
                        table->tb->caption_side = TableProp::CAPTION_SIDE_BOTTOM;
                        log_debug("Table caption-side: bottom");
                    } else {
                        table->tb->caption_side = TableProp::CAPTION_SIDE_TOP;
                        log_debug("Table caption-side: top");
                    }
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
        }
    }

    // Check if table-layout was already set to FIXED by CSS (via custom property)
    // If so, respect the CSS value and don't override it
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED) {
        log_debug("Table layout: already set to FIXED by CSS, skipping heuristic");
        return;
    }

    // Default to auto layout
    table->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;

    // Use heuristic: if table has BOTH explicit width AND height, assume fixed layout
    // This matches common CSS patterns where fixed layout is used with constrained dimensions

    bool has_explicit_width = false;
    bool has_explicit_height = false;

    if (element->node_type == DOM_NODE_ELEMENT) {
        // Lambda CSS path
        DomElement* dom_elem = element->as_element();

        if (dom_elem->specified_style) {
            // Check for explicit width property
            CssDeclaration* width_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_WIDTH);

            if (width_decl && width_decl->value) {
                has_explicit_width = true;
            }

            // Check for explicit height property
            CssDeclaration* height_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_HEIGHT);

            if (height_decl && height_decl->value) {
                has_explicit_height = true;
            }
        }
    }

    // If both width and height are explicitly set, use fixed layout
    // This heuristic works for most real-world cases where fixed layout is desired
    if (has_explicit_width && has_explicit_height) {
        table->tb->table_layout = TableProp::TABLE_LAYOUT_FIXED;
        log_debug("Table layout: fixed (heuristic: table has explicit width AND height)");
    } else {
        log_debug("Table layout: auto (no explicit width+height combo)");
    }
}

// Parse cell attributes (colspan, rowspan)
static void parse_cell_attributes(LayoutContext* lycon, DomNode* cellNode, ViewTableCell* cell) {
    assert(cell->td);
    // Initialize defaults
    cell->td->col_span = 1;
    cell->td->row_span = 1;
    cell->td->col_index = -1;
    cell->td->row_index = -1;
    cell->td->is_empty = is_cell_empty(cell) ? 1 : 0;  // Check if cell has no content
    // CSS 2.1: Default vertical-align for table cells is 'middle'
    cell->td->vertical_align = TableCellProp::CELL_VALIGN_MIDDLE;
    if (!cellNode->is_element()) return;

    if (cellNode->node_type == DOM_NODE_ELEMENT) {
        // Lambda CSS path
        DomElement* dom_elem = cellNode->as_element();
        log_debug("Lambda CSS: parse_cell_attributes for element type=%d", cellNode->node_type);

        // Parse colspan attribute
        const char* colspan_str = dom_element_get_attribute(dom_elem, "colspan");
        log_debug("Lambda CSS: colspan_str = %s", colspan_str ? colspan_str : "NULL");
        if (colspan_str && colspan_str[0] != '\0') {
            int span = atoi(colspan_str);
            if (span > 0 && span <= 1000) {
                cell->td->col_span = span;
                log_debug("Lambda CSS: Parsed colspan=%d", span);
            }
        }

        // Parse rowspan attribute
        const char* rowspan_str = dom_element_get_attribute(dom_elem, "rowspan");
        log_debug("Lambda CSS: rowspan_str = %s", rowspan_str ? rowspan_str : "NULL");
        if (rowspan_str && rowspan_str[0] != '\0') {
            int span = atoi(rowspan_str);
            if (span > 0 && span <= 65534) {
                cell->td->row_span = span;
                log_debug("Lambda CSS: Parsed rowspan=%d from attribute value '%s'", span, rowspan_str);
            }
        }

        // Parse vertical-align: check resolved in_line property first (set by apply_element_default_style),
        // then check CSS declarations for overrides
        bool valign_resolved = false;

        // First, check the resolved in_line->vertical_align (set by HTML default styles in resolve_htm_style.cpp)
        // This handles the CSS 2.1 default: vertical-align: middle for td/th
        if (cell->in_line && cell->in_line->vertical_align) {
            CssEnum valign_value = cell->in_line->vertical_align;
            if (valign_value == CSS_VALUE_TOP) {
                cell->td->vertical_align = TableCellProp::CELL_VALIGN_TOP;
                valign_resolved = true;
                log_debug("Cell vertical-align from in_line: top");
            } else if (valign_value == CSS_VALUE_MIDDLE) {
                cell->td->vertical_align = TableCellProp::CELL_VALIGN_MIDDLE;
                valign_resolved = true;
                log_debug("Cell vertical-align from in_line: middle");
            } else if (valign_value == CSS_VALUE_BOTTOM) {
                cell->td->vertical_align = TableCellProp::CELL_VALIGN_BOTTOM;
                valign_resolved = true;
                log_debug("Cell vertical-align from in_line: bottom");
            } else if (valign_value == CSS_VALUE_BASELINE) {
                cell->td->vertical_align = TableCellProp::CELL_VALIGN_BASELINE;
                valign_resolved = true;
                log_debug("Cell vertical-align from in_line: baseline");
            }
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
                CssEnum valign_keyword = valign_decl->value->data.keyword;

                // Map CSS vertical-align keywords to cell enum
                if (valign_keyword == CSS_VALUE_TOP) {
                    cell->td->vertical_align = TableCellProp::CELL_VALIGN_TOP;
                    log_debug("Cell vertical-align: top");
                } else if (valign_keyword == CSS_VALUE_MIDDLE) {
                    cell->td->vertical_align = TableCellProp::CELL_VALIGN_MIDDLE;
                    log_debug("Cell vertical-align: middle");
                } else if (valign_keyword == CSS_VALUE_BOTTOM) {
                    cell->td->vertical_align = TableCellProp::CELL_VALIGN_BOTTOM;
                    log_debug("Cell vertical-align: bottom");
                } else if (valign_keyword == CSS_VALUE_BASELINE) {
                    cell->td->vertical_align = TableCellProp::CELL_VALIGN_BASELINE;
                    log_debug("Cell vertical-align: baseline");
                }
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
// Instead of creating new DOM nodes (which would violate View* = DomNode*),
// we use flags to mark existing elements as "doubled" as anonymous boxes:
// - is_annoy_tbody: table acts as its own anonymous tbody
// - is_annoy_tr: row group/table acts as anonymous tr (cells as direct children)
// - is_annoy_td: tr has text content wrapped in anonymous cell
// - is_annoy_colgroup: col elements treated as wrapped in anonymous colgroup

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
    for (ViewBlock* child = (ViewBlock*)table->first_child; child;
         child = (ViewBlock*)child->next_sibling) {
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
        log_debug("Anonymous box: table doubled as tbody");
    }

    // Case 2: Table has direct cells without rows
    // => Table acts as anonymous tbody AND anonymous tr
    if (has_direct_cell) {
        table->tb->is_annoy_tbody = 1;
        table->tb->is_annoy_tr = 1;
        log_debug("Anonymous box: table doubled as tbody+tr");
    }

    // Now check each row group for anonymous tr cases
    for (ViewBlock* child = (ViewBlock*)table->first_child; child;
         child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            // Check if row group has direct cells (no rows)
            bool group_has_direct_cell = false;
            for (ViewBlock* gchild = (ViewBlock*)child->first_child; gchild;
                 gchild = (ViewBlock*)gchild->next_sibling) {
                if (gchild->view_type == RDT_VIEW_TABLE_CELL) {
                    group_has_direct_cell = true;
                    break;
                }
            }
            if (group_has_direct_cell) {
                // Mark the first direct cell as having is_annoy_tr
                // (The group acts as anonymous row)
                for (ViewBlock* gchild = (ViewBlock*)child->first_child; gchild;
                     gchild = (ViewBlock*)gchild->next_sibling) {
                    if (gchild->view_type == RDT_VIEW_TABLE_CELL) {
                        ViewTableCell* cell = (ViewTableCell*)gchild;
                        if (cell->td) {
                            cell->td->is_annoy_tr = 1;
                            log_debug("Anonymous box: cell marked as wrapped in anonymous tr");
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
static void mark_table_node(LayoutContext* lycon, DomNode* node, ViewGroup* parent) {
    if (!node || !node->is_element()) return;

    DisplayValue display = resolve_display_value(node);
    uintptr_t tag = node->tag();

    // Save context
    DomNode* saved_elmt = lycon->elmt;
    lycon->elmt = node;

    // Mark node based on display type or HTML tag
    if (tag == HTM_TAG_CAPTION || display.inner == CSS_VALUE_TABLE_CAPTION) {
        // Caption - mark as block and layout content immediately
        ViewBlock* caption = (ViewBlock*)set_view(lycon, RDT_VIEW_BLOCK, node);
        if (caption) {
            lycon->view = (View*)caption;
            dom_node_resolve_style(node, lycon);  // Resolve caption styles

            // Read caption-side from caption element's style and store in table
            DomElement* dom_elem = static_cast<DomElement*>(node);
            if (dom_elem->specified_style && parent && parent->view_type == RDT_VIEW_TABLE) {
                ViewTable* table = (ViewTable*)parent;
                if (table->tb) {
                    CssDeclaration* caption_decl = style_tree_get_declaration(
                        dom_elem->specified_style, CSS_PROPERTY_CAPTION_SIDE);
                    if (caption_decl && caption_decl->value) {
                        CssValue* val = caption_decl->value;
                        if (val->type == CSS_VALUE_TYPE_KEYWORD && val->data.keyword == CSS_VALUE_BOTTOM) {
                            table->tb->caption_side = TableProp::CAPTION_SIDE_BOTTOM;
                            log_debug("Caption side: bottom (from caption element)");
                        }
                    }
                }
            }

            Blockbox saved_block = lycon->block;
            Linebox saved_line = lycon->line;

            int caption_width = lycon->line.right - lycon->line.left;
            if (caption_width <= 0) caption_width = 600;

            lycon->block.content_width = (float)caption_width;
            lycon->block.content_height = 10000;  // Large enough for content
            lycon->block.advance_y = 0;
            lycon->line.left = 0;
            lycon->line.right = caption_width;
            lycon->line.advance_x = 0;
            lycon->line.is_line_start = true;
            log_debug("Caption layout start: width=%d, advance_y=%.1f", caption_width, lycon->block.advance_y);

            DomNode* child = static_cast<DomElement*>(node)->first_child;
            for (; child; child = child->next_sibling) {
                layout_flow_node(lycon, child);
            }
            // Handle last line
            log_debug("Caption before line_break: is_line_start=%d, advance_y=%.1f", lycon->line.is_line_start, lycon->block.advance_y);
            if (!lycon->line.is_line_start) { line_break(lycon); }
            log_debug("Caption after line_break: advance_y=%.1f", lycon->block.advance_y);

            caption->height = lycon->block.advance_y;
            caption->width = (float)caption_width;  // Also set width explicitly
            log_debug("Caption layout end: caption->height=%.1f, advance_y=%.1f", caption->height, lycon->block.advance_y);
            lycon->block = saved_block;
            lycon->line = saved_line;
        }
    }
    else if (tag == HTM_TAG_THEAD || tag == HTM_TAG_TBODY || tag == HTM_TAG_TFOOT ||
             display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
             display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
             display.inner == CSS_VALUE_TABLE_FOOTER_GROUP) {
        // Row group - mark and recurse
        ViewTableRowGroup* group = (ViewTableRowGroup*)set_view(lycon, RDT_VIEW_TABLE_ROW_GROUP, node);
        if (group) {
            lycon->view = (View*)group;
            dom_node_resolve_style(node, lycon);  // Resolve styles for proper font inheritance
            DomNode* child = static_cast<DomElement*>(node)->first_child;
            for (; child; child = child->next_sibling) {
                if (child->is_element()) mark_table_node(lycon, child, (ViewGroup*)group);
            }
        }
    }
    else if (tag == HTM_TAG_TR || display.inner == CSS_VALUE_TABLE_ROW) {
        // Row - mark and recurse
        ViewTableRow* row = (ViewTableRow*)set_view(lycon, RDT_VIEW_TABLE_ROW, node);
        if (row) {
            lycon->view = (View*)row;
            dom_node_resolve_style(node, lycon);  // Resolve styles for proper font inheritance
            DomNode* child = static_cast<DomElement*>(node)->first_child;
            for (; child; child = child->next_sibling) {
                if (child->is_element()) mark_table_node(lycon, child, (ViewGroup*)row);
            }
        }
    }
    else if (tag == HTM_TAG_TD || tag == HTM_TAG_TH || display.inner == CSS_VALUE_TABLE_CELL) {
        // Cell - mark with styles and attributes
        ViewTableCell* cell = (ViewTableCell*)set_view(lycon, RDT_VIEW_TABLE_CELL, node);
        if (cell) {
            lycon->view = (View*)cell;
            dom_node_resolve_style(node, lycon);
            parse_cell_attributes(lycon, node, cell);
        }
    }

    // Restore context
    lycon->elmt = saved_elmt;
}

// Build table structure from DOM - simplified using unified tree architecture
ViewTable* build_table_tree(LayoutContext* lycon, DomNode* tableNode) {
    log_debug("Building table structure");

    // Create table view and resolve styles
    ViewTable* table = (ViewTable*)lycon->view;
    dom_node_resolve_style(tableNode, lycon);
    resolve_table_properties(lycon, tableNode, table);

    // Recursively mark all table children with correct view types
    if (tableNode->is_element()) {
        DomNode* child = static_cast<DomElement*>(tableNode)->first_child;
        for (; child; child = child->next_sibling) {
            if (child->is_element()) {
                mark_table_node(lycon, child, (ViewGroup*)table);
            }
        }
    }

    log_debug("Table structure built successfully");
    return table;
}

// Calculate proper height distribution for rowspan cells
// Uses navigation helpers for proper anonymous box support
static void calculate_rowspan_heights(ViewTable* table, TableMetadata* meta, int* row_heights) {
    if (!table || !meta || !row_heights) return;

    // Iterate all rows and cells using navigation helpers
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
            if (cell->td->row_span > 1) {
                // Calculate total height needed for spanned rows
                int start_row = cell->td->row_index;
                int end_row = start_row + cell->td->row_span;

                // Get current total height of spanned rows
                int current_total = 0;
                for (int r = start_row; r < end_row && r < meta->row_count; r++) {
                    current_total += row_heights[r];
                }

                // If cell needs more height, distribute the extra
                if (cell->height > current_total) {
                    int extra_needed = cell->height - current_total;
                    int extra_per_row = extra_needed / cell->td->row_span;
                    int remainder = extra_needed % cell->td->row_span;

                    for (int r = start_row; r < end_row && r < meta->row_count; r++) {
                        row_heights[r] += extra_per_row;
                        if (r - start_row < remainder) {
                            row_heights[r] += 1; // Distribute remainder
                        }
                    }

                    log_debug("Enhanced rowspan: cell height=%d distributed across %d rows (extra=%d)",
                             cell->height, cell->td->row_span, extra_needed);
                }
            }
        }
    }
}

// Apply CSS vertical-align positioning to cell content
static void apply_cell_vertical_alignment(LayoutContext* lycon, ViewTableCell* tcell, int content_height) {
    if (!tcell || !tcell->td) return;

    int valign = tcell->td->vertical_align;

    // Calculate content's actual height to determine offset
    int content_actual_height = 0;
    float max_y = 0;

    // Find the maximum Y position of all child content to determine actual height
    for (View* child = ((ViewGroup*)tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            float child_bottom = text->y + text->height;
            if (child_bottom > max_y) max_y = child_bottom;
        }
        // Add other child types as needed (blocks, inlines, etc.)
    }
    content_actual_height = (int)max_y;

    // Calculate vertical offset based on alignment
    int vertical_offset = 0;
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
            // Align to text baseline - simplified to top for now
            // TODO: Implement proper baseline alignment with font metrics
            vertical_offset = 0;
            break;
    }

    // Apply offset to all child content
    if (vertical_offset > 0) {
        for (View* child = ((ViewGroup*)tcell)->first_child; child; child = child->next_sibling) {
            if (child->view_type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)child;
                text->y += vertical_offset;
                log_debug("CSS vertical-align: adjusted text Y by +%dpx (align=%d)",
                         vertical_offset, (int)valign);
            }
            // Apply to other child types as needed
        }
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
    int border_top = 1, border_bottom = 1;
    int padding_top = 0, padding_bottom = 0;

    if (tcell->bound) {
        if (tcell->bound->padding.top >= 0) padding_top = tcell->bound->padding.top;
        if (tcell->bound->padding.bottom >= 0) padding_bottom = tcell->bound->padding.bottom;
        if (tcell->bound->border) {
            if (tcell->bound->border->width.top >= 0) border_top = tcell->bound->border->width.top;
            if (tcell->bound->border->width.bottom >= 0) border_bottom = tcell->bound->border->width.bottom;
        }
    }

    int content_area_height = tcell->height - border_top - border_bottom - padding_top - padding_bottom;
    int content_start_y = border_top + padding_top;

    // Find content bounds (min and max Y of children)
    float content_min_y = 1e9f;
    float content_max_y = 0;
    bool has_content = false;

    for (View* child = ((ViewGroup*)tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            if (text->y < content_min_y) content_min_y = text->y;
            float child_bottom = text->y + text->height;
            if (child_bottom > content_max_y) content_max_y = child_bottom;
            has_content = true;
        }
        // Handle other child types (ViewBlock, etc.)
        else if (child->view_type >= RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)child;
            if (block->y < content_min_y) content_min_y = block->y;
            float child_bottom = block->y + block->height;
            if (child_bottom > content_max_y) content_max_y = child_bottom;
            has_content = true;
        }
    }

    if (!has_content) return;

    int content_actual_height = (int)(content_max_y - content_min_y);

    // Calculate new vertical offset based on alignment
    int new_offset = 0;
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
    int adjustment = new_offset - (int)content_min_y;

    log_debug("Rowspan vertical-align: cell_height=%d, content_area=%d, content_height=%d, "
              "valign=%d, content_min_y=%.1f, new_offset=%d, adjustment=%d",
              (int)tcell->height, content_area_height, content_actual_height,
              valign, content_min_y, new_offset, adjustment);

    if (adjustment != 0) {
        for (View* child = ((ViewGroup*)tcell)->first_child; child; child = child->next_sibling) {
            if (child->view_type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)child;
                text->y += adjustment;
                if (text->rect) {
                    text->rect->y += adjustment;
                }
            }
            else if (child->view_type >= RDT_VIEW_BLOCK) {
                ViewBlock* block = (ViewBlock*)child;
                block->y += adjustment;
            }
        }
    }
}

// Layout cell content with correct parent width (after cell dimensions are set)
// This is the ONLY place where cell content gets laid out (single pass)
static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell) {
    ViewTableCell* tcell = static_cast<ViewTableCell*>(cell);
    if (!tcell) return;

    // No need to clear text rectangles - this is the first and only layout pass!

    // Save layout context to restore later
    Blockbox saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    DomNode* saved_elmt = lycon->elmt;

    // Calculate cell border and padding offsets
    // Content area starts AFTER border and padding
    int border_left = 1;  // 1px left border
    int border_top = 1;   // 1px top border
    int border_right = 1; // 1px right border
    int border_bottom = 1; // 1px bottom border

    int padding_left = 0;
    int padding_right = 0;
    int padding_top = 0;
    int padding_bottom = 0;

    if (tcell->bound) {
        padding_left = tcell->bound->padding.left >= 0 ? tcell->bound->padding.left : 0;
        padding_right = tcell->bound->padding.right >= 0 ? tcell->bound->padding.right : 0;
        padding_top = tcell->bound->padding.top >= 0 ? tcell->bound->padding.top : 0;
        padding_bottom = tcell->bound->padding.bottom >= 0 ? tcell->bound->padding.bottom : 0;
    }

    // Calculate content area START position (offset from cell origin)
    int content_start_x = border_left + padding_left;
    int content_start_y = border_top + padding_top;

    // Calculate content area dimensions (space available for content)
    int content_width = cell->width - border_left - border_right - padding_left - padding_right;
    int content_height = cell->height - border_top - border_bottom - padding_top - padding_bottom;

    // Ensure non-negative dimensions
    if (content_width < 0) content_width = 0;
    if (content_height < 0) content_height = 0;

    // Set up layout context for cell content with CORRECT positioning
    // CRITICAL FIX: Set line.left and advance_x to content_start_x to apply padding offset
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
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
        log_debug("Table cell text-align: %d", tcell->blk->text_align);
    }

    log_debug("Layout cell content - cell=%dx%d, border=(%d,%d), padding=(%d,%d,%d,%d), content_start=(%d,%d), content=%dx%d",
        cell->width, cell->height, border_left, border_top,
        padding_left, padding_right, padding_top, padding_bottom,
        content_start_x, content_start_y, content_width, content_height);

    // Layout children with correct parent width
    // NOTE: Do NOT call dom_node_resolve_style here before layout_flow_node.
    // The styles will be resolved properly inside layout_block, which creates
    // the ViewBlock first and then resolves CSS styles. Calling it here would
    // mark styles_resolved=true prematurely, causing layout_block to skip
    // resolution and lose the given_width/given_height values.
    if (tcell->is_element()) {
        DomNode* cc = static_cast<DomElement*>(tcell)->first_child;
        for (; cc; cc = cc->next_sibling) {
            layout_flow_node(lycon, cc);
        }
    }

    // Apply horizontal text alignment (e.g., center for TH elements)
    // This must be called after content layout to align the line of text
    line_align(lycon);

    // Apply CSS vertical-align positioning after content layout
    apply_cell_vertical_alignment(lycon, tcell, content_height);

    // Restore layout context
    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->elmt = saved_elmt;
}

// Measure cell's intrinsic content width (Preferred Content Width - PCW)
// This performs accurate measurement using font metrics for CSS 2.1 compliance
// REFACTORED: Now uses unified intrinsic_sizing.hpp for text measurement
static int measure_cell_intrinsic_width(LayoutContext* lycon, ViewTableCell* cell) {
    if (!cell || !cell->is_element()) return 20; // CSS minimum usable width

    DomElement* cell_elem = cell->as_element();
    if (!cell_elem->first_child) return 20; // Empty cell minimum

    // Save current layout context
    Blockbox saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    DomNode* saved_elmt = lycon->elmt;
    bool saved_measuring = lycon->is_measuring;
    FontBox saved_font = lycon->font; // Save font context

    // Set up CSS 2.1 measurement context with infinite width
    lycon->is_measuring = true; // Flag to indicate measurement mode
    lycon->elmt = cell;

    // Apply the cell's CSS font properties for accurate measurement
    if (cell->font) {
        log_debug("PCW measurement: using cell font family=%s, size=%.1f",
            cell->font->family ? cell->font->family : "default", cell->font->font_size);
        setup_font(lycon->ui_context, &lycon->font, cell->font);
    } else {
        log_debug("PCW measurement: using context font (no cell-specific font)");
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

    // Measure each child's natural width
    for (DomNode* child = cell_elem->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            // Use unified text measurement from intrinsic_sizing.hpp
            const unsigned char* text = child->text_data();
            if (text && *text) {
                size_t text_len = strlen((const char*)text);
                log_debug("PCW measuring text: '%s' (len=%zu)", text, text_len);

                // Use unified intrinsic sizing API
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                    lycon, (const char*)text, text_len);

                float text_width = (float)widths.max_content;  // PCW uses max-content
                log_debug("PCW text measured width: %.2f (unified API)", text_width);
                if (text_width > max_width) max_width = text_width;
            }
        }
        else if (child->is_element()) {
            // For nested block/inline elements, check for explicit CSS width first
            DomElement* child_elem = child->as_element();
            float child_width = 0;

            if (child_elem->specified_style) {
                CssDeclaration* width_decl = style_tree_get_declaration(
                    child_elem->specified_style, CSS_PROPERTY_WIDTH);
                if (width_decl && width_decl->value && width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    // Resolve length value (handles em, rem, px, etc.)
                    child_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                }
            }

            // If no explicit width, perform temporary layout to measure
            if (child_width == 0) {
                float child_start_x = lycon->line.advance_x;

                // Temporarily layout the child element in measurement mode
                layout_flow_node(lycon, child);

                // Measure the width consumed by this child
                child_width = lycon->line.advance_x - child_start_x;
            }

            if (child_width > max_width) max_width = child_width;
        }
    }

    // Restore context
    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->elmt = saved_elmt;
    lycon->is_measuring = saved_measuring;
    lycon->font = saved_font; // Restore original font context

    // Add padding
    float padding_horizontal = 0.0f;
    if (cell->bound && cell->bound->padding.left >= 0 && cell->bound->padding.right >= 0) {
        padding_horizontal = (float)(cell->bound->padding.left + cell->bound->padding.right);
    }

    // Add border - read actual border widths from style
    float border_horizontal = 0.0f;
    if (cell->bound && cell->bound->border) {
        border_horizontal = cell->bound->border->width.left + cell->bound->border->width.right;
    }

    max_width += border_horizontal;
    max_width += padding_horizontal;

    // CSS 2.1: Ensure reasonable minimum width for empty cells
    if (max_width < 16.0f) max_width = 16.0f;

    log_debug("PCW: %.2fpx (content + padding=%.1f + border=%.1f)",
        max_width, padding_horizontal, border_horizontal);

    // Use precise rounding for consistency with browser behavior
    return (int)roundf(max_width);
}

// Measure cell's minimum content width (MCW) - narrowest width without overflow
// This calculates the width needed for the longest word or unbreakable content
// REFACTORED: Now uses unified intrinsic_sizing.hpp for text measurement
static int measure_cell_minimum_width(LayoutContext* lycon, ViewTableCell* cell) {
    if (!cell || !cell->is_element()) return 16; // Minimum default

    DomElement* cell_elem = cell->as_element();
    if (!cell_elem->first_child) return 16; // Empty cell

    // Save current layout context
    Blockbox saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    DomNode* saved_elmt = lycon->elmt;
    bool saved_measuring = lycon->is_measuring;
    FontBox saved_font = lycon->font; // Save font context

    // Set up temporary measurement context
    lycon->is_measuring = true;
    lycon->elmt = cell;

    // Apply the cell's CSS font properties for accurate measurement
    if (cell->font) {
        setup_font(lycon->ui_context, &lycon->font, cell->font);
    }

    // For minimum width, we want the width of the longest word
    float min_width = 0.0f;

    // Measure each child's minimum width
    for (DomNode* child = cell_elem->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            // Use unified text measurement from intrinsic_sizing.hpp
            const unsigned char* text = child->text_data();
            if (text && *text) {
                size_t text_len = strlen((const char*)text);

                // Use unified intrinsic sizing API - min_content gives longest word
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                    lycon, (const char*)text, text_len);

                float longest_word = (float)widths.min_content;
                if (longest_word > min_width) min_width = longest_word;
            }
        }
        else if (child->is_element()) {
            // For nested elements, use conservative minimum
            // TODO: Implement proper minimum width calculation for nested elements
            float child_min = 20.0f; // Conservative minimum
            if (child_min > min_width) min_width = child_min;
        }
    }

    // Restore context
    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->elmt = saved_elmt;
    lycon->is_measuring = saved_measuring;
    lycon->font = saved_font;

    // Add padding and border with precise calculation
    float padding_horizontal = 0.0f;
    if (cell->bound && cell->bound->padding.left >= 0 && cell->bound->padding.right >= 0) {
        padding_horizontal = (float)(cell->bound->padding.left + cell->bound->padding.right);
    }

    float border_horizontal = 0.0f;
    if (cell->bound && cell->bound->border) {
        border_horizontal = cell->bound->border->width.left + cell->bound->border->width.right;
    }

    min_width += border_horizontal + padding_horizontal;

    // CSS 2.1: Apply minimum cell width constraint for usability (reduced for accuracy)
    float min_cell_constraint = 16.0f + padding_horizontal + border_horizontal;
    if (min_width < min_cell_constraint) {
        min_width = min_cell_constraint;
    }

    // Use precise rounding for pixel-perfect layout
    return (int)ceilf(min_width);  // Always round up for minimum width to prevent overflow
}

// Single-pass table structure analysis - Phase 3 optimization
// Counts columns/rows and assigns column indices in one pass
// Uses navigation helpers for proper anonymous box support
static TableMetadata* analyze_table_structure(LayoutContext* lycon, ViewTable* table) {
    // First pass: count columns and rows using navigation helpers
    int columns = 0;
    int rows = 0;

    // Iterate all rows using navigation helpers
    // CSS 2.1 §17.5.5: Collapsed rows still contribute to column width calculation
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        rows++;
        int row_cells = 0;
        for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
            row_cells += cell->td->col_span;
        }
        if (row_cells > columns) columns = row_cells;
    }

    if (columns <= 0 || rows <= 0) return nullptr;

    // Create metadata structure
    TableMetadata* meta = new TableMetadata(columns, rows);

    // Second pass: assign column indices, measure widths, and track collapsed rows
    int current_row = 0;
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        // Track visibility: collapse for this row
        // CSS 2.1 §17.5.5: Rows with visibility: collapse don't contribute to height
        if (is_visibility_collapse((ViewBlock*)row)) {
            meta->row_collapsed[current_row] = true;
            log_debug("Row %d has visibility: collapse", current_row);
        }

        int col = 0;
        for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
            // Find next available column
            while (col < columns && meta->grid(current_row, col)) {
                col++;
            }

            // Assign indices
            cell->td->col_index = col;
            cell->td->row_index = current_row;

            // Mark grid as occupied
            for (int r = current_row; r < current_row + cell->td->row_span && r < rows; r++) {
                for (int c = col; c < col + cell->td->col_span && c < columns; c++) {
                    meta->grid(r, c) = true;
                }
            }

            col += cell->td->col_span;
        }
        current_row++;
    }

    return meta;
}

// Enhanced table layout algorithm with colspan/rowspan support
void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    if (!table) return;

    // Initialize fixed layout fields
    table->tb->fixed_row_height = 0;  // 0 = auto height (calculate from content)
    log_debug("Starting enhanced table auto layout");
    log_debug("Table layout mode: %s", table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED ? "fixed" : "auto");
    log_debug("Table border-spacing: %fpx %fpx, border-collapse: %s",
        table->tb->border_spacing_h, table->tb->border_spacing_v, table->tb->border_collapse ? "true" : "false");

    // CRITICAL FIX: Handle caption positioning first
    ViewBlock* caption = nullptr;
    int caption_height = 0;

    // Find and position caption
    for (ViewBlock* child = (ViewBlock*)table->first_child;  child;  child = (ViewBlock*)child->next_sibling) {
        if (child->tag() == HTM_TAG_CAPTION) {
            caption = child;
            // Caption height = content height + padding + border
            if (caption->height > 0) {
                float padding_v = 0;
                float border_v = 0;
                if (caption->bound) {
                    padding_v = caption->bound->padding.top + caption->bound->padding.bottom;
                    if (caption->bound->border) {
                        border_v = caption->bound->border->width.top + caption->bound->border->width.bottom;
                    }
                }
                caption_height = (int)(caption->height + padding_v + border_v);
                log_debug("Caption height calculation: content=%.1f, padding_v=%.1f, border_v=%.1f, total=%d",
                    caption->height, padding_v, border_v, caption_height);
            }
            break;
        }
    }

    // Step 1: Analyze table structure (Phase 3 optimization)
    // Single-pass analysis counts columns/rows AND assigns cell indices
    TableMetadata* meta = analyze_table_structure(lycon, table);
    if (!meta) {
        log_debug("Empty table, setting zero dimensions");
        table->width = 0;  table->height = 0;
        return;
    }

    log_debug("Table layout: metadata created successfully, proceeding with width calculation");

    int columns = meta->column_count;
    int rows = meta->row_count;
    log_debug("Table has %d columns, %d rows (analyzed in single pass)", columns, rows);

    // Check if table has explicit width (for percentage cell width calculation)
    int explicit_table_width = 0;
    int table_content_width = 0; // Width available for cells
    if (table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem->specified_style) {
            CssDeclaration* width_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->value) {
                // Handle percentage width (e.g., width: 100%)
                if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    double percentage = width_decl->value->data.percentage.value;
                    // Calculate percentage relative to container width
                    int container_width = lycon->block.content_width;
                    if (container_width <= 0) {
                        container_width = lycon->line.right - lycon->line.left;
                    }
                    if (container_width > 0) {
                        explicit_table_width = (int)(container_width * percentage / 100.0);
                        log_debug("Table percentage width: %.1f%% of %dpx = %dpx",
                                percentage, container_width, explicit_table_width);
                    }
                }
                // Handle length value (handles em, rem, px, etc.)
                else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    float resolved_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                    explicit_table_width = (int)resolved_width;
                    log_debug("Table explicit width: %dpx", explicit_table_width);
                }

                // Calculate content width if we have an explicit width
                if (explicit_table_width > 0) {
                    table_content_width = explicit_table_width;

                    // Subtract table border
                    if (table->bound && table->bound->border) {
                        table_content_width -= (int)(table->bound->border->width.left + table->bound->border->width.right);
                    }

                    // Subtract table padding
                    if (table->bound && table->bound->padding.left >= 0 && table->bound->padding.right >= 0) {
                        table_content_width -= table->bound->padding.left + table->bound->padding.right;
                    }

                    // Subtract border-spacing
                    if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
                        table_content_width -= (columns + 1) * table->tb->border_spacing_h;
                    }

                    log_debug("Table content width for cells: %dpx", table_content_width);
                }
            }
        }
    }

    // Step 2: Enhanced column width calculation with colspan/rowspan support
    // Use metadata's col_widths array (already allocated)
    int* col_widths = meta->col_widths;

    // Use metadata's grid for colspan/rowspan tracking (already populated)
    bool* grid_occupied = meta->grid_occupied;
    #define GRID(r, c) grid_occupied[(r) * columns + (c)]

    // Assign column indices and measure content with grid support
    // Use navigation helpers to iterate over all cells uniformly
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        for (ViewTableCell* tcell = row->first_cell(); tcell; tcell = row->next_cell(tcell)) {
            // Use pre-assigned column index from analyze_table_structure()
            int col = tcell->td->col_index;

            // Get explicit CSS width using helper function
            int cell_width = get_cell_css_width(lycon, tcell, table_content_width);

            // Calculate both minimum and preferred widths for CSS 2.1 table layout
            int min_width = 0;   // MCW - Minimum Content Width
            int pref_width = 0;  // PCW - Preferred Content Width

            if (cell_width == 0) {
                // No explicit CSS width - measure intrinsic content widths
                pref_width = measure_cell_intrinsic_width(lycon, tcell);
                min_width = measure_cell_minimum_width(lycon, tcell);
                cell_width = pref_width; // Use preferred for backward compatibility
            } else {
                // Has explicit CSS width - use it for both min and preferred
                min_width = pref_width = cell_width;
            }

            if (tcell->td->col_span == 1) {
                // Single column cell - update min and preferred widths (bounds check)
                if (col >= 0 && col < meta->column_count) {
                    if (min_width > meta->col_min_widths[col]) {
                        meta->col_min_widths[col] = min_width;
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
                // Multi-column cell - distribute width across spanned columns
                // CSS 2.1 Section 17.5.2.2: Distribute colspan cell's width proportionally
                int span = tcell->td->col_span;

                // Calculate current totals for all three width arrays
                int current_col_total = 0;
                int current_min_total = 0;
                int current_max_total = 0;
                for (int c = col; c < col + span && c < columns; c++) {
                    current_col_total += col_widths[c];
                    current_min_total += meta->col_min_widths[c];
                    current_max_total += meta->col_max_widths[c];
                }

                // Distribute min_width across col_min_widths
                if (min_width > current_min_total) {
                    int extra_needed = min_width - current_min_total;
                    int extra_per_col = extra_needed / span;
                    int remainder = extra_needed % span;
                    for (int c = col; c < col + span && c < columns; c++) {
                        meta->col_min_widths[c] += extra_per_col;
                        if (remainder > 0) { meta->col_min_widths[c]++; remainder--; }
                    }
                }

                // Distribute pref_width across col_max_widths
                if (pref_width > current_max_total) {
                    int extra_needed = pref_width - current_max_total;
                    int extra_per_col = extra_needed / span;
                    int remainder = extra_needed % span;
                    for (int c = col; c < col + span && c < columns; c++) {
                        meta->col_max_widths[c] += extra_per_col;
                        if (remainder > 0) { meta->col_max_widths[c]++; remainder--; }
                    }
                }

                // Also update col_widths for backward compatibility
                if (cell_width > current_col_total) {
                    int extra_needed = cell_width - current_col_total;
                    int extra_per_col = extra_needed / span;
                    int remainder = extra_needed % span;
                    for (int c = col; c < col + span && c < columns; c++) {
                        col_widths[c] += extra_per_col;
                        if (remainder > 0) { col_widths[c]++; remainder--; }
                    }
                }
            }
        }
    }

    // Apply CSS 2.1 table-layout algorithm with improved precision
    int fixed_table_width = 0; // Store explicit width for fixed layout
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED) {
        log_debug("=== CSS 2.1 FIXED LAYOUT ALGORITHM ===");

        // STEP 1: Get explicit table width from CSS (CSS 2.1 Section 17.5.2)
        int fixed_explicit_width = 0;

        // Try to read width directly from table element's CSS
        if (table->node_type == DOM_NODE_ELEMENT) {
            DomElement* dom_elem = table->as_element();
            if (dom_elem->specified_style) {
                CssDeclaration* width_decl = style_tree_get_declaration(
                    dom_elem->specified_style, CSS_PROPERTY_WIDTH);
                if (width_decl && width_decl->value) {
                    // Handle percentage width
                    if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        double percentage = width_decl->value->data.percentage.value;
                        int container_width = lycon->block.content_width;
                        if (container_width <= 0) {
                            container_width = lycon->line.right - lycon->line.left;
                        }
                        if (container_width > 0) {
                            fixed_explicit_width = (int)(container_width * percentage / 100.0);
                            log_debug("FIXED LAYOUT - percentage width: %.1f%% of %dpx = %dpx",
                                    percentage, container_width, fixed_explicit_width);
                        }
                    }
                    // Handle length value
                    else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                        float resolved_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                        fixed_explicit_width = (int)resolved_width;
                        log_debug("FIXED LAYOUT - read table CSS width: %dpx", fixed_explicit_width);
                    }
                }
            }
        }

        // Fallback to lycon->block.given_width or container
        if (fixed_explicit_width == 0 && lycon->block.given_width > 0) {
            fixed_explicit_width = lycon->block.given_width;
            log_debug("FIXED LAYOUT - using given_width: %dpx", fixed_explicit_width);
        } else if (fixed_explicit_width == 0) {
            // No explicit width, use container width or default
            int container_width = lycon->line.right - lycon->line.left;
            fixed_explicit_width = container_width > 0 ? container_width : 600;
            log_debug("FIXED LAYOUT - given_width=0, using container/default: %dpx (container=%d-%d=%d)",
                   fixed_explicit_width, lycon->line.right, lycon->line.left, container_width);
        }

        // Store for later use
        fixed_table_width = fixed_explicit_width;
        log_debug("FIXED LAYOUT - stored fixed_table_width: %dpx", fixed_table_width);

        // STEP 2: Calculate available content width (subtract borders and spacing)
        int content_width = fixed_explicit_width;

        // Subtract actual table border (we'll add it back later for final width)
        int table_border_h = 0;
        if (table->bound && table->bound->border) {
            table_border_h = (int)(table->bound->border->width.left + table->bound->border->width.right);
        }
        content_width -= table_border_h;

        // For border-collapse, no additional adjustments needed
        // For separate borders, subtract border-spacing
        if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
            content_width -= (columns + 1) * table->tb->border_spacing_h; // Spacing around and between columns
            log_debug("Subtracting border-spacing: (%d+1)*%.1f = %.1f",
                   columns, table->tb->border_spacing_h, (columns + 1) * table->tb->border_spacing_h);
        }

        log_debug("Content width for columns: %dpx", content_width);
        // STEP 3: Read explicit column widths from FIRST ROW cells
        int* explicit_col_widths = (int*)calloc(columns, sizeof(int));
        int total_explicit = 0;  int unspecified_cols = 0;

        // Find first row using navigation helper
        ViewTableRow* first_row = table->first_row();

        // Read cell widths from first row
        if (first_row) {
            int col = 0;
            log_debug("Reading first row cell widths...");
            for (ViewTableCell* cell = first_row->first_cell();
                 cell && col < columns;
                 cell = first_row->next_cell(cell)) {

                    // Try to get explicit width from CSS
                    int cell_width = 0;
                    if (cell->node_type == DOM_NODE_ELEMENT) {
                        DomElement* dom_elem = cell->as_element();
                        if (dom_elem->specified_style) {
                            CssDeclaration* width_decl = style_tree_get_declaration(
                                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
                            if (width_decl && width_decl->value) {
                                // Check if it's a percentage value
                                if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                    // Calculate percentage relative to table content width
                                    double percentage = width_decl->value->data.percentage.value;
                                    cell_width = (int)(content_width * percentage / 100.0);
                                    log_debug("  Column %d: percentage width %.1f%% of %dpx = %dpx",
                                            col, percentage, content_width, cell_width);
                                } else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                    // Resolve length value (handles em, rem, px, etc.)
                                    float resolved_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                                    cell_width = (int)resolved_width;
                                    log_debug("  Column %d: absolute width %dpx", col, cell_width);
                                }
                            }
                        }
                    }

                    if (cell_width > 0) {
                        explicit_col_widths[col] = cell_width;
                        total_explicit += cell_width;
                        log_debug("  Column %d: explicit width %dpx", col, cell_width);
                    } else {
                        unspecified_cols++;
                        log_debug("  Column %d: no explicit width", col);
                    }
                    col += cell->td->col_span;
            }
        }

        // STEP 4: Distribute widths according to CSS table-layout: fixed algorithm
        if (total_explicit > 0) {
            log_debug("Found %d columns with explicit widths (total: %dpx), %d unspecified",
                columns - unspecified_cols, total_explicit, unspecified_cols);

            // Distribute remaining width to unspecified columns
            int remaining_width = content_width - total_explicit;
            if (unspecified_cols > 0 && remaining_width > 0) {
                int width_per_unspecified = remaining_width / unspecified_cols;
                for (int i = 0; i < columns; i++) {
                    if (explicit_col_widths[i] == 0) {
                        explicit_col_widths[i] = width_per_unspecified;
                    }
                }
                log_debug("Distributing %dpx to %d unspecified columns (%dpx each)",
                       remaining_width, unspecified_cols, width_per_unspecified);
            } else if (unspecified_cols > 0) {
                // Not enough space even for explicit widths, scale everything
                double scale_factor = (double)content_width / total_explicit;
                for (int i = 0; i < columns; i++) {
                    if (explicit_col_widths[i] > 0) {
                        explicit_col_widths[i] = (int)(explicit_col_widths[i] * scale_factor);
                    }
                }
                // Distribute any remainder
                int scaled_total = 0;
                for (int i = 0; i < columns; i++) scaled_total += explicit_col_widths[i];
                int remainder = content_width - scaled_total;
                for (int i = 0; i < columns && remainder > 0; i++) {
                    if (explicit_col_widths[i] == 0) {
                        explicit_col_widths[i] = remainder / unspecified_cols;
                    }
                }
                log_debug("Scaled explicit widths by %.2f to fit content width", scale_factor);
            }
        } else {
            // No explicit widths, distribute equally
            int width_per_col = content_width / columns;
            for (int i = 0; i < columns; i++) {
                explicit_col_widths[i] = width_per_col;
            }
            log_debug("No explicit widths - equal distribution: %dpx per column", width_per_col);
        }

        // STEP 5: Replace col_widths with fixed layout widths
        memcpy(col_widths, explicit_col_widths, columns * sizeof(int));
        free(explicit_col_widths);

        log_debug("=== FIXED LAYOUT COMPLETE ===");
        for (int i = 0; i < columns; i++) {
            log_debug("  Final column %d width: %dpx", i, col_widths[i]);
        }

        // STEP 6: Handle explicit table HEIGHT for fixed layout
        // If table has height: 300px, distribute that height across rows
        int explicit_table_height = 0;
        if (table->node_type == DOM_NODE_ELEMENT) {
            DomElement* dom_elem = table->as_element();
            if (dom_elem->specified_style) {
                CssDeclaration* height_decl = style_tree_get_declaration(
                    dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
                if (height_decl && height_decl->value) {
                    // Use resolve_length_value to properly handle em/rem/px units
                    float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                    if (resolved_height > 0) {
                        explicit_table_height = (int)resolved_height;
                        log_debug("FIXED LAYOUT - read table CSS height: %dpx (resolved)", explicit_table_height);
                    }
                }
            }
        }

        if (explicit_table_height > 0) {
            log_debug("=== FIXED LAYOUT HEIGHT DISTRIBUTION ===");

            // Count total rows
            int total_rows = rows;  // 'rows' variable from earlier count
            log_debug("Total rows to distribute height: %d", total_rows);

            // Calculate available content height (subtract borders, padding, spacing)
            int content_height = explicit_table_height;

            // Subtract table border
            if (table->bound && table->bound->border) {
                content_height -= (int)(table->bound->border->width.top + table->bound->border->width.bottom);
            }

            // Subtract table padding
            if (table->bound) {
                if (table->bound->padding.top >= 0) content_height -= table->bound->padding.top;
                if (table->bound->padding.bottom >= 0) content_height -= table->bound->padding.bottom;
            }

            // Subtract border-spacing (if separate borders)
            if (!table->tb->border_collapse && table->tb->border_spacing_v > 0 && total_rows > 0) {
                content_height -= (int)((total_rows + 1) * table->tb->border_spacing_v);
                log_debug("Subtracting vertical border-spacing: (%d+1)*%.1f = %.1f",
                       total_rows, table->tb->border_spacing_v, (total_rows + 1) * table->tb->border_spacing_v);
            }

            // Distribute height equally across rows
            int height_per_row = total_rows > 0 ? content_height / total_rows : 0;
            log_debug("Height per row: %dpx (content_height=%d / rows=%d)",
                   height_per_row, content_height, total_rows);

            // Store the fixed row height for later application during positioning
            // We'll apply this when positioning rows in the main layout loop
            table->tb->fixed_row_height = height_per_row;
            log_debug("=== FIXED LAYOUT HEIGHT DISTRIBUTION COMPLETE ===");
        }
    }

    // Step 3: CSS 2.1 Table Layout Algorithm - Width Distribution (Section 17.5.2)
    log_debug("===== CSS 2.1 AUTO TABLE LAYOUT ALGORITHM =====");

    // Calculate minimum and preferred table widths (including borders and spacing)
    int min_table_content_width = 0;  // MCW sum for table content
    int pref_table_content_width = 0; // PCW sum for table content

    for (int i = 0; i < columns; i++) {
        min_table_content_width += meta->col_min_widths[i];
        pref_table_content_width += meta->col_max_widths[i];
        log_debug("Column %d: MCW=%dpx, PCW=%dpx",
                 i, meta->col_min_widths[i], meta->col_max_widths[i]);
    }

    // Add border-spacing to table width calculation (CSS 2.1 requirement)
    int border_spacing_total = 0;
    if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
        border_spacing_total = (int)((columns + 1) * table->tb->border_spacing_h);
    }

    int min_table_width = min_table_content_width + border_spacing_total;
    int pref_table_width = pref_table_content_width + border_spacing_total;

    log_debug("Table content: min=%dpx, preferred=%dpx", min_table_content_width, pref_table_content_width);
    log_debug("Table total (with spacing): min=%dpx, preferred=%dpx", min_table_width, pref_table_width);

    // Determine used table width according to CSS 2.1 specification
    int used_table_width;
    if (explicit_table_width > 0) {
        // CSS 2.1: Table has explicit width - use it (but not less than minimum)
        used_table_width = explicit_table_width > min_table_width ? explicit_table_width : min_table_width;
        log_debug("CSS 2.1: Using explicit table width: %dpx (requested: %dpx)", used_table_width, explicit_table_width);
    } else {
        // CSS 2.1: Table width is auto - use preferred width
        used_table_width = pref_table_width;
        log_debug("CSS 2.1: Using preferred table width: %dpx (table width: auto)", used_table_width);
    }

    // Calculate available content width for column distribution
    int available_content_width = used_table_width - border_spacing_total;

    // Check for equal distribution case (CSS behavior for similar columns)
    bool use_equal_distribution = true;
    if (columns > 0) {
        int first_pref = meta->col_max_widths[0];
        for (int i = 1; i < columns; i++) {
            if (abs(meta->col_max_widths[i] - first_pref) > 3) { // Allow small differences
                use_equal_distribution = false;
                break;
            }
        }
    } else {
        use_equal_distribution = false; // No columns means no equal distribution
    }

    if (use_equal_distribution && columns > 1 && explicit_table_width == 0) {
        // Special case: columns have similar preferred widths and table width is auto
        // Use equal distribution (common browser optimization for balanced tables)
        int avg_width = used_table_width / columns;
        int remainder = used_table_width % columns;

        log_debug("Using equal distribution - columns have similar content (avg=~%dpx)", avg_width);
        for (int i = 0; i < columns; i++) {
            col_widths[i] = avg_width;
            if (i < remainder) col_widths[i]++; // Distribute remainder
        }
    }

    // CSS 2.1 Column Width Distribution Algorithm (Section 17.5.2.2)
    if (available_content_width == pref_table_content_width) {
        // Case 1: Perfect fit - use preferred widths directly
        log_debug("CSS 2.1 Case 1: Perfect fit - using PCW directly");
        for (int i = 0; i < columns; i++) {
            col_widths[i] = meta->col_max_widths[i];
        }
    } else if (available_content_width > pref_table_content_width) {
        // Case 2: Table wider than preferred - distribute extra space proportionally
        int extra_space = available_content_width - pref_table_content_width;

        log_debug("CSS 2.1 Case 2: Table wider than preferred - distributing %dpx extra", extra_space);

        // Distribute proportionally based on preferred widths (CSS 2.1 behavior)
        int total_distributed = 0;
        for (int i = 0; i < columns; i++) {
            if (pref_table_content_width > 0) {
                int extra_for_col = (extra_space * meta->col_max_widths[i]) / pref_table_content_width;
                col_widths[i] = meta->col_max_widths[i] + extra_for_col;
                total_distributed += extra_for_col;
            } else {
                col_widths[i] = meta->col_max_widths[i];
            }
        }

        // Distribute any remainder to maintain exact width
        int remainder = extra_space - total_distributed;
        for (int i = 0; i < columns && remainder > 0; i++) {
            col_widths[i]++;
            remainder--;
        }
    } else {
        // Case 3: Table narrower than preferred - CSS 2.1 constrained distribution
        log_debug("CSS 2.1 Case 3: Table narrower than preferred - constrained distribution");

        if (available_content_width >= min_table_content_width) {
            // Can fit minimum widths - scale between min and preferred
            log_debug("Scaling between MCW and PCW (available=%d, min=%d, pref=%d)",
                     available_content_width, min_table_content_width, pref_table_content_width);

            for (int i = 0; i < columns; i++) {
                int min_w = meta->col_min_widths[i];
                int pref_w = meta->col_max_widths[i];
                int range = pref_w - min_w;

                if (pref_table_content_width > min_table_content_width && range > 0) {
                    // Linear interpolation between min and preferred
                    double factor = (double)(available_content_width - min_table_content_width) /
                                   (pref_table_content_width - min_table_content_width);
                    col_widths[i] = min_w + (int)(range * factor);
                } else {
                    col_widths[i] = min_w; // Fallback to minimum
                }
            }
        } else {
            // Cannot fit minimum widths - use minimum and overflow
            log_debug("Cannot fit MCW - using minimum widths (will overflow)");
            for (int i = 0; i < columns; i++) {
                col_widths[i] = meta->col_min_widths[i];
            }
        }
    }

    // Calculate final table width
    int table_width = 0;
    for (int i = 0; i < columns; i++) {
        table_width += col_widths[i];
        log_debug("Final column %d width: %dpx", i, col_widths[i]);
    }

    log_debug("Final table width: %dpx", table_width);

    log_debug("table_width before border adjustments: %d, border_collapse=%d",
           table_width, table->tb->border_collapse);

    // Apply border spacing or border collapse adjustments
    if (table->tb->border_collapse) {
        // Border-collapse: borders overlap between adjacent cells
        // Calculate the overlap amount based on actual cell border widths
        // In collapse mode, adjacent borders share space, so we subtract the overlapping border width

        if (columns > 1) {
            // Get actual table border width for proper CSS 2.1 collapse calculation
            float table_border_width = 1.0f; // Default to 1px

            // Get table border width from computed style
            if (table->bound && table->bound->border) {
                table_border_width = table->bound->border->width.left; // Use left border as representative
                log_debug("Border-collapse: detected table border width: %.1fpx", table_border_width);
            }

            // For border-collapse, use the table border width as the collapsed border width
            // This is a simplification but matches CSS behavior where table border takes precedence
            // at table edges, and for interior boundaries we assume similar border widths
            float collapsed_border_width = table_border_width;

            // Reduce table width by the overlapping border widths
            // Each interior column boundary has one border that would otherwise be doubled
            int reduction = (int)((columns - 1) * collapsed_border_width);
            log_debug("Border-collapse: using table border width %.1fpx for collapsed borders",
                collapsed_border_width);
            log_debug("Border-collapse reducing width by %dpx (%d boundaries × %.1fpx collapsed border)",
                reduction, columns - 1, collapsed_border_width);
            table_width -= reduction;
        }
        log_debug("Border-collapse applied - table width: %d", table_width);
    } else if (table->tb->border_spacing_h > 0) {
        // Separate borders: add spacing between columns AND around table edges
        log_debug("Applying border-spacing %fpx to table width", table->tb->border_spacing_h);
        if (columns > 1) {
            table_width += (columns - 1) * table->tb->border_spacing_h; // Between columns
        }
        table_width += 2 * table->tb->border_spacing_h; // Left and right edges
        log_debug("Border-spacing applied (%dpx) - table width: %d (includes edge spacing)",
               (int)table->tb->border_spacing_h, table_width);
    }

    // Add table padding to width
    int table_padding_horizontal = 0;
    if (table->bound && table->bound->padding.left >= 0 && table->bound->padding.right >= 0) {
        table_padding_horizontal = table->bound->padding.left + table->bound->padding.right;
        table_width += table_padding_horizontal;
        log_debug("Added table padding horizontal: %dpx (left=%d, right=%d)",
               table_padding_horizontal, table->bound->padding.left, table->bound->padding.right);
    }

    // CRITICAL FIX: For fixed layout, override calculated width with CSS width
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED && fixed_table_width > 0) {
        log_debug("Fixed layout override - changing table_width from %d to %d",
               table_width, fixed_table_width);
        table_width = fixed_table_width;
        log_debug("Fixed layout override - using CSS width: %dpx", table_width);
    }

    log_debug("Final table width for layout: %dpx", table_width);
    log_debug("===== CSS 2.1 TABLE LAYOUT COMPLETE =====");

    // Step 4: Position cells and calculate row heights with CSS 2.1 border model

    int* col_x_positions = (int*)calloc(columns + 1, sizeof(int));

    // Start with table padding and left border-spacing for separate border model
    int table_padding_left = 0;
    if (table->bound && table->bound->padding.left >= 0) {
        table_padding_left = table->bound->padding.left;
        log_debug("Added table padding left: +%dpx", table_padding_left);
    }

    // Add table border width (content starts inside the border)
    int table_border_left = 0;
    if (table->bound && table->bound->border && table->bound->border->width.left > 0) {
        table_border_left = (int)table->bound->border->width.left;
        log_debug("Added table border left: +%dpx", table_border_left);
    }

    col_x_positions[0] = table_border_left + table_padding_left;
    if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
        col_x_positions[0] += table->tb->border_spacing_h;
        log_debug("Added left border-spacing: +%dpx", table->tb->border_spacing_h);
    }

    // Enhanced border width calculation for pixel-perfect precision
    float cell_border_width = 1.0f; // Default to 1px

    if (table->tb->border_collapse && table->first_child != nullptr) {
        // Simple safety check before traversing table structure
        ViewBlock* first_child = (ViewBlock*)table->first_child;
        if ((uintptr_t)first_child >= 0x1000 && (uintptr_t)first_child <= 0x7FFFFFFFFFFF) {
            log_debug("Enhanced border precision: Attempting to calculate optimal border width");

            // CSS 2.1 border-collapse: Use the maximum border width among adjacent borders
            float max_border_width = 0.0f;

            // Check table border (all sides to get the most representative value)
            if (table->bound && table->bound->border) {
                float table_border_avg = (table->bound->border->width.left +
                                        table->bound->border->width.right +
                                        table->bound->border->width.top +
                                        table->bound->border->width.bottom) / 4.0f;
                if (table_border_avg > max_border_width) {
                    max_border_width = table_border_avg;
                }
                log_debug("Enhanced border precision: table average border=%.2fpx", table_border_avg);
            }

            // Sample cell borders to get more accurate representation
            int sampled_cells = 0;
            float total_cell_border = 0.0f;
            ViewBlock* sample_row = first_child;

            // Find the first actual table row
            while (sample_row && sample_row->view_type != RDT_VIEW_TABLE_ROW) {
                if (sample_row->view_type == RDT_VIEW_TABLE_ROW_GROUP && sample_row->first_child) {
                    sample_row = (ViewBlock*)sample_row->first_child;
                } else {
                    sample_row = (ViewBlock*)sample_row->next_sibling;
                }
            }

            // Sample a few cells from the first row to get representative border width
            if (sample_row && sample_row->view_type == RDT_VIEW_TABLE_ROW) {
                for (ViewBlock* cell = (ViewBlock*)sample_row->first_child;
                     cell && sampled_cells < 3;
                     cell = (ViewBlock*)cell->next_sibling) {
                    if (cell->view_type == RDT_VIEW_TABLE_CELL && cell->bound && cell->bound->border) {
                        // Average horizontal borders for width calculation
                        float cell_h_border = (cell->bound->border->width.left +
                                             cell->bound->border->width.right) / 2.0f;
                        if (cell_h_border > 0) {
                            total_cell_border += cell_h_border;
                            sampled_cells++;
                            log_debug("Enhanced border precision: cell %d border=%.2fpx",
                                     sampled_cells, cell_h_border);
                        }
                    }
                }
            }

            // Calculate final border width using maximum principle
            if (sampled_cells > 0) {
                float avg_cell_border = total_cell_border / sampled_cells;
                if (avg_cell_border > max_border_width) {
                    max_border_width = avg_cell_border;
                }
                log_debug("Enhanced border precision: average cell border=%.2fpx (sampled %d cells)",
                         avg_cell_border, sampled_cells);
            }

            // Apply the calculated border width with reasonable bounds
            if (max_border_width > 0.5f) {
                cell_border_width = max_border_width;
                // Cap at reasonable maximum to prevent layout explosion
                if (cell_border_width > 8.0f) {
                    cell_border_width = 8.0f;
                }
            }

            log_debug("Enhanced border precision: final border width=%.2fpx for collapse calculation",
                     cell_border_width);
        } else {
            log_debug("Enhanced border precision: Skipping border detection due to invalid table structure");
        }
    }

    // CSS 2.1 Column Position Calculation (Section 17.5)
    for (int i = 1; i <= columns; i++) {
        col_x_positions[i] = col_x_positions[i-1] + col_widths[i-1];

        if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
            // CSS 2.1: Separate borders - add border-spacing between columns with precision
            float precise_spacing = table->tb->border_spacing_h;
            col_x_positions[i] += (int)(precise_spacing + 0.5f); // Round to nearest pixel
            log_debug("Enhanced border precision: Added border-spacing %.1fpx (rounded to %dpx) between columns %d and %d",
                     precise_spacing, (int)(precise_spacing + 0.5f), i-1, i);
        } else if (table->tb->border_collapse && i > 1) {
            // CSS 2.1: Border-collapse - adjacent borders overlap with proper rounding
            // For border-collapse, we need at least 1px overlap to merge borders properly
            float precise_overlap = cell_border_width / 2.0f; // Half border width for overlap

            // Ensure minimum 1px overlap for border-collapse to work correctly
            int overlap_pixels = (int)(precise_overlap + 0.5f); // Round to nearest pixel
            if (overlap_pixels < 1 && cell_border_width > 0.5f) {
                overlap_pixels = 1; // Minimum 1px overlap for border-collapse
            }

            // Apply the overlap
            col_x_positions[i] -= overlap_pixels;

            log_debug("Enhanced border precision: Border-collapse overlap -%.2fpx (applied as %dpx) between columns %d and %d",
                     precise_overlap, overlap_pixels, i-1, i);
        }
        log_debug("CSS 2.1: Column %d starts at x=%dpx", i-1, col_x_positions[i-1]);
    }

    // Start Y position - only include caption height if caption is at top
    int current_y = 0;
    if (caption && table->tb->caption_side == TableProp::CAPTION_SIDE_TOP) {
        current_y = caption_height;
    }

    // Add table border (content starts inside the border)
    int table_border_top = 0;
    if (table->bound && table->bound->border && table->bound->border->width.top > 0) {
        table_border_top = (int)table->bound->border->width.top;
        current_y += table_border_top;
        log_debug("Added table border top: +%dpx", table_border_top);
    }

    // Add table padding (space inside table border)
    int table_padding_top = 0;
    if (table->bound && table->bound->padding.top >= 0) {
        table_padding_top = table->bound->padding.top;
        current_y += table_padding_top;
        log_debug("Added table padding top: +%dpx", table_padding_top);
    }

    // Add top border-spacing for separate border model
    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
        current_y += table->tb->border_spacing_v;
        log_debug("Added top border-spacing: +%dpx", table->tb->border_spacing_v);
    }

    // Position caption at top if caption-side is top (default)
    if (caption && table->tb->caption_side == TableProp::CAPTION_SIDE_TOP) {
        caption->x = 0;
        caption->y = 0;
        caption->width = table_width;
        log_debug("Positioned caption at top: y=0");
    }

    // Global row index for tracking row positions across all row groups
    int global_row_index = 0;

    for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            int group_start_y = current_y;

            // Position row group at table content area (after padding and border-spacing)

            // Calculate tbody content width
            int tbody_content_width;
            if (table->tb->border_collapse) {
                // For border-collapse, use the final table width (includes border adjustments)
                tbody_content_width = table_width;
            } else {
                // For border-spacing, calculate as sum of column widths + spacing
                tbody_content_width = 0;
                for (int i = 0; i < columns; i++) {
                    tbody_content_width += col_widths[i];
                }
                // Add border-spacing between columns
                if (table->tb->border_spacing_h > 0 && columns > 1) {
                    tbody_content_width += (columns - 1) * table->tb->border_spacing_h;
                }
            }

            // Position tbody based on border-collapse mode
            if (table->tb->border_collapse) {
                // Border-collapse: tbody starts at half the table border width horizontally,
                // but vertically uses current_y to stack row groups properly
                child->x = 1.5f; // Half of table border width (3px / 2)
                child->y = (float)current_y; // Use current_y to stack row groups
                child->width = (float)tbody_content_width;
            } else {
                // Border-separate: tbody starts after table padding and left border-spacing
                // col_x_positions[0] already includes table padding + border-spacing
                child->x = (float)col_x_positions[0];
                child->y = (float)current_y;
                child->width = (float)tbody_content_width;
            }

            log_debug("Row group positioned at x=%.1f, y=%.1f, width=%.1f (tbody_content_width=%d, columns=%d)",
                   child->x, child->y, child->width, tbody_content_width, columns);

            // Count rows in this group to identify the last row
            int row_count = 0;
            for (ViewBlock* count_row = (ViewBlock*)child->first_child; count_row; count_row = (ViewBlock*)count_row->next_sibling) {
                if (count_row->view_type == RDT_VIEW_TABLE_ROW) row_count++;
            }
            int current_row_index = 0;

            for (ViewBlock* row = (ViewBlock*)child->first_child; row; row = (ViewBlock*)row->next_sibling) {
                if (row->view_type == RDT_VIEW_TABLE_ROW) {
                    current_row_index++;
                    bool is_last_row = (current_row_index == row_count);

                    // CSS 2.1 §17.5.5: Check for visibility: collapse
                    // Collapsed rows don't render and don't contribute to height
                    bool is_collapsed = (global_row_index < meta->row_count &&
                                         meta->row_collapsed[global_row_index]);

                    if (is_collapsed) {
                        // Position row but with zero height
                        row->x = 0;
                        row->y = current_y - group_start_y;
                        row->width = child->width;
                        row->height = 0;

                        // Still process cells for column width contribution
                        // but don't let them affect row height
                        ViewTableRow* trow = (ViewTableRow*)row;
                        for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                            // Position cell but with zero height
                            ViewBlock* cell = (ViewBlock*)tcell;
                            cell->x = col_x_positions[tcell->td->col_index] - col_x_positions[0];
                            cell->y = 0;
                            cell->width = calculate_cell_width_from_columns(tcell, col_widths, columns);
                            cell->height = 0;
                        }

                        // Track collapsed row
                        if (global_row_index < meta->row_count) {
                            meta->row_y_positions[global_row_index] = current_y;
                            meta->row_heights[global_row_index] = 0;
                            log_debug("Collapsed row %d: y=%d, height=0", global_row_index, current_y);
                        }
                        global_row_index++;

                        // No height contribution, no spacing after collapsed row
                        continue;
                    }

                    // Position row relative to row group
                    row->x = 0;
                    row->y = current_y - group_start_y; // Relative to row group
                    row->width = child->width; // Match tbody width
                    log_debug("Row positioned at x=%d, y=%d (relative to group), width=%d",
                        row->x, row->y, row->width);

                    // Calculate row height and position cells
                    int row_height = 0;
                    ViewTableRow* trow = (ViewTableRow*)row;
                    for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                        int height_for_row = process_table_cell(lycon, tcell, table, col_widths, col_x_positions, columns);
                        if (height_for_row > row_height) {
                            row_height = height_for_row;
                        }
                    }

                    // CSS 2.1 §17.5.3: Check for explicit CSS height on the row
                    int explicit_row_height = 0;
                    if (row->is_element()) {
                        DomElement* row_elem = row->as_element();
                        if (row_elem->specified_style) {
                            CssDeclaration* height_decl = style_tree_get_declaration(
                                row_elem->specified_style, CSS_PROPERTY_HEIGHT);
                            if (height_decl && height_decl->value) {
                                float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                                if (resolved_height > 0) {
                                    explicit_row_height = (int)resolved_height;
                                    log_debug("Row has explicit CSS height: %dpx", explicit_row_height);
                                }
                            }
                        }
                    }

                    // Use the larger of content height and explicit CSS height
                    if (explicit_row_height > row_height) {
                        row_height = explicit_row_height;
                        log_debug("Using explicit row height %dpx instead of content height", row_height);
                    }

                    // Apply fixed layout height if specified
                    if (table->tb->fixed_row_height > 0) {
                        apply_fixed_row_height(lycon, trow, table->tb->fixed_row_height);
                    } else {
                        row->height = row_height;
                        // Also apply height to all cells in the row
                        for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                            if (tcell->height < row_height) {
                                tcell->height = row_height;
                                // Re-apply vertical alignment with correct cell height
                                int content_height = measure_cell_content_height(lycon, tcell);
                                apply_cell_vertical_align(tcell, (int)tcell->height, content_height);
                            }
                        }
                    }

                    // Track row height and Y position for rowspan calculation
                    if (global_row_index < meta->row_count) {
                        meta->row_y_positions[global_row_index] = current_y;
                        meta->row_heights[global_row_index] = row->height;
                        log_debug("Tracking row %d: y=%d, height=%d", global_row_index, current_y, (int)row->height);
                    }
                    global_row_index++;

                    current_y += row->height;

                    // Add vertical border-spacing after each row (except last row in group)
                    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0 && !is_last_row) {
                        current_y += table->tb->border_spacing_v;
                        log_debug("Added vertical spacing after row: +%dpx", table->tb->border_spacing_v);
                    }
                }
            }

            // Set row group dimensions (relative to table) - preserve our calculated positioning
            // Don't override x and y - they were set earlier with proper calculations
            // Width already set above based on border-collapse mode
            child->height = (float)(current_y - group_start_y);
            // printf("DEBUG: Final row group dimensions - x=%.1f, y=%.1f, width=%.1f, height=%.1f\n",
            //        child->x, child->y, child->width, child->height);
        }
        else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            // Handle direct table rows (relative to table)
            ViewTableRow* trow = (ViewTableRow*)child;

            // CSS 2.1 §17.5.5: Check for visibility: collapse
            bool is_collapsed = (global_row_index < meta->row_count &&
                                 meta->row_collapsed[global_row_index]);

            if (is_collapsed) {
                // Position row but with zero height
                trow->x = 0;
                trow->y = current_y;
                trow->width = table_width;
                trow->height = 0;

                // Still process cells for column width but with zero height
                for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                    ViewBlock* cell = (ViewBlock*)tcell;
                    cell->x = col_x_positions[tcell->td->col_index] - col_x_positions[0];
                    cell->y = 0;
                    cell->width = calculate_cell_width_from_columns(tcell, col_widths, columns);
                    cell->height = 0;
                }

                // Track collapsed row
                if (global_row_index < meta->row_count) {
                    meta->row_y_positions[global_row_index] = current_y;
                    meta->row_heights[global_row_index] = 0;
                    log_debug("Collapsed direct row %d: y=%d, height=0", global_row_index, current_y);
                }
                global_row_index++;

                // No height contribution, no spacing after collapsed row
                continue;
            }

            trow->x = 0;  trow->y = current_y; // Relative to table
            trow->width = table_width;
            log_debug("Direct row positioned at x=%d, y=%d (relative to table), width=%d",
                   trow->x, trow->y, trow->width);

            int row_height = 0;
            for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                int height_for_row = process_table_cell(lycon, tcell, table, col_widths, col_x_positions, columns);
                if (height_for_row > row_height) {
                    row_height = height_for_row;
                }
            }

            // CSS 2.1 §17.5.3: Check for explicit CSS height on the row
            int explicit_row_height = 0;
            if (trow->is_element()) {
                DomElement* row_elem = trow->as_element();
                if (row_elem->specified_style) {
                    CssDeclaration* height_decl = style_tree_get_declaration(
                        row_elem->specified_style, CSS_PROPERTY_HEIGHT);
                    if (height_decl && height_decl->value) {
                        float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                        if (resolved_height > 0) {
                            explicit_row_height = (int)resolved_height;
                            log_debug("Direct row has explicit CSS height: %dpx", explicit_row_height);
                        }
                    }
                }
            }

            // Use the larger of content height and explicit CSS height
            if (explicit_row_height > row_height) {
                row_height = explicit_row_height;
                log_debug("Using explicit row height %dpx instead of content height", row_height);
            }

            // Apply fixed layout height if specified
            if (table->tb->fixed_row_height > 0) {
                apply_fixed_row_height(lycon, trow, table->tb->fixed_row_height);
            } else {
                trow->height = row_height;
                // Also apply height to all cells in the row
                for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                    if (tcell->height < row_height) {
                        tcell->height = row_height;
                        // Re-apply vertical alignment with correct cell height
                        int content_height = measure_cell_content_height(lycon, tcell);
                        apply_cell_vertical_align(tcell, (int)tcell->height, content_height);
                    }
                }
            }

            // Track row height and Y position for rowspan calculation
            if (global_row_index < meta->row_count) {
                meta->row_y_positions[global_row_index] = current_y;
                meta->row_heights[global_row_index] = trow->height;
                log_debug("Tracking direct row %d: y=%d, height=%d", global_row_index, current_y, (int)trow->height);
            }
            global_row_index++;

            current_y += trow->height;

            // Add vertical border-spacing after each row (except last)
            if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
                current_y += table->tb->border_spacing_v;
                log_debug("Added vertical spacing after direct row: +%dpx", table->tb->border_spacing_v);
            }
        }
    }

    // =========================================================================
    // SECOND PASS: Fix rowspan cell heights and re-apply vertical alignment
    // After all rows are positioned, update cells with rowspan > 1 to span
    // the correct total height of their spanned rows, then re-apply vertical
    // alignment since the content was laid out with estimated height
    // =========================================================================
    for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = (ViewBlock*)child->first_child; row; row = (ViewBlock*)row->next_sibling) {
                if (row->view_type == RDT_VIEW_TABLE_ROW) {
                    ViewTableRow* trow = (ViewTableRow*)row;
                    for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                        if (tcell->td->row_span > 1) {
                            int start_row = tcell->td->row_index;
                            int end_row = start_row + tcell->td->row_span;
                            if (end_row > meta->row_count) end_row = meta->row_count;

                            // Sum heights of spanned rows
                            int spanned_height = 0;
                            for (int r = start_row; r < end_row; r++) {
                                spanned_height += meta->row_heights[r];
                                // Add border-spacing between rows (but not after last)
                                if (!table->tb->border_collapse && table->tb->border_spacing_v > 0 && r < end_row - 1) {
                                    spanned_height += table->tb->border_spacing_v;
                                }
                            }

                            log_debug("Rowspan cell fix: rows %d-%d, old height=%d, new height=%d",
                                      start_row, end_row - 1, (int)tcell->height, spanned_height);
                            tcell->height = spanned_height;

                            // Re-apply vertical alignment now that final height is known
                            reapply_rowspan_vertical_alignment(tcell);
                        }
                    }
                }
            }
        }
        else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewTableRow* trow = (ViewTableRow*)child;
            for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                if (tcell->td->row_span > 1) {
                    int start_row = tcell->td->row_index;
                    int end_row = start_row + tcell->td->row_span;
                    if (end_row > meta->row_count) end_row = meta->row_count;

                    // Sum heights of spanned rows
                    int spanned_height = 0;
                    for (int r = start_row; r < end_row; r++) {
                        spanned_height += meta->row_heights[r];
                        // Add border-spacing between rows (but not after last)
                        if (!table->tb->border_collapse && table->tb->border_spacing_v > 0 && r < end_row - 1) {
                            spanned_height += table->tb->border_spacing_v;
                        }
                    }

                    log_debug("Rowspan cell fix (direct): rows %d-%d, old height=%d, new height=%d",
                              start_row, end_row - 1, (int)tcell->height, spanned_height);
                    tcell->height = spanned_height;

                    // Re-apply vertical alignment now that final height is known
                    reapply_rowspan_vertical_alignment(tcell);
                }
            }
        }
    }

    // Calculate final table height with border-spacing and padding
    int final_table_height = current_y;

    // Add table padding bottom
    int table_padding_bottom = 0;
    if (table->bound && table->bound->padding.bottom >= 0) {
        table_padding_bottom = table->bound->padding.bottom;
        final_table_height += table_padding_bottom;
        log_debug("Added table padding bottom: +%dpx", table_padding_bottom);
    }

    // Add vertical border-spacing around table edges for separate border model
    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
        // Border-spacing adds space around the entire table perimeter
        // Bottom spacing around the table (top was already added)
        final_table_height += table->tb->border_spacing_v;
        log_debug("Added table edge bottom vertical spacing: +%dpx", table->tb->border_spacing_v);
    }

    // Position caption at bottom if caption-side is bottom (CSS 2.1 Section 17.4.1)
    if (caption && table->tb->caption_side == TableProp::CAPTION_SIDE_BOTTOM) {
        caption->x = 0;
        caption->y = final_table_height;  // Position after all rows
        caption->width = table_width;
        final_table_height += caption_height;  // Add caption height to table
        log_debug("Positioned caption at bottom: y=%d, caption_height=%d", (int)caption->y, caption_height);
    }

    // CRITICAL FIX: Add table border to final dimensions
    // Read actual table border widths
    int table_border_width = 0;
    int table_border_height = 0;

    if (table->bound && table->bound->border) {
        table_border_width = (int)(table->bound->border->width.left + table->bound->border->width.right);
        table_border_height = (int)(table->bound->border->width.top + table->bound->border->width.bottom);
        log_debug("Using actual table border: width=%dpx (left=%.1f, right=%.1f), height=%dpx (top=%.1f, bottom=%.1f)",
               table_border_width, table->bound->border->width.left, table->bound->border->width.right,
               table_border_height, table->bound->border->width.top, table->bound->border->width.bottom);
    }

    // Set final table dimensions including border
    table->width = table_width + table_border_width;
    table->height = final_table_height + table_border_height;
    table->content_width = table_width;  // Content area excludes border
    table->content_height = final_table_height;  // Content area excludes border

    log_debug("Added table border: +%dpx width, +%dpx height",
           table_border_width, table_border_height);

    // CRITICAL: Also set ViewBlock height for block layout system integration
    // ViewTable inherits from ViewBlock, so block layout reads this field
    ((ViewBlock*)table)->height = final_table_height + table_border_height;
    log_debug("Set ViewBlock height to %.1fpx for block layout integration (table ptr=%p)", (float)(final_table_height + table_border_height), table);

    log_debug("Table dimensions calculated: width=%dpx, height=%dpx (ptr=%p, table->width=%.1f, table->height=%.1f)",
              table_width, final_table_height, table, table->width, table->height);
    log_debug("Table layout complete: %dx%d", table_width, current_y);

    // Cleanup - TableMetadata destructor handles grid_occupied and col_widths
    delete meta;
    free(col_x_positions);

    #undef GRID
}

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

// Main table layout entry point
void layout_table(LayoutContext* lycon, DomNode* tableNode, DisplayValue display) {
    log_debug("=== TABLE LAYOUT START ===");
    log_debug("Starting table layout");
    log_debug("Initial layout context - line.left=%d, advance_y=%d", lycon->line.left, lycon->block.advance_y);
    if (!tableNode) {
        log_debug("ERROR: Null table node");
        return;
    }

    // CRITICAL: Update font context before building table tree
    // This ensures children inherit the correct computed font-size from the table element.
    // Without this, lycon->font.style would still point to the grandparent's font.
    ViewTable* table = (ViewTable*)lycon->view;
    log_debug("Table font context check: table=%p, table->font=%p, lycon->font.style=%p, lycon->font.style->font_size=%.1f",
        (void*)table, table ? (void*)table->font : nullptr,
        (void*)lycon->font.style, lycon->font.style ? lycon->font.style->font_size : -1.0f);
    if (table && table->font) {
        setup_font(lycon->ui_context, &lycon->font, table->font);
        log_debug("Updated font context for table: font-size=%.1f", table->font->font_size);
    } else {
        log_debug("WARNING: table->font is NULL, cannot update font context");
    }

    // Step 1: Build table structure from DOM
    log_debug("Step 1 - Building table tree");
    table = build_table_tree(lycon, tableNode);
    if (!table) {
        log_debug("ERROR: Failed to build table structure");
        return;
    }
    log_debug("Table tree built successfully");

    // Step 1.5: Detect and mark anonymous box wrappers
    detect_anonymous_boxes(table);

    // Step 2: Calculate layout
    log_debug("Step 2 - Calculating table layout (table ptr=%p)", table);
    table_auto_layout(lycon, table);
    log_debug("Table layout calculated - size: %dx%d (table ptr=%p)", table->width, table->height, table);
    log_debug("Table final position: x=%d, y=%d (trusting block layout positioning)", table->x, table->y);

    // Step 3: Update layout context for proper block integration
    // CRITICAL: Set advance_y to table height so finalize_block_flow works correctly
    // The block layout system uses advance_y to calculate the final block height
    lycon->block.advance_y = table->height;

    // CRITICAL FIX: Ensure proper line state management for tables
    // Tables are block-level elements and should not participate in line layout
    // Set is_line_start = true to prevent parent from calling line_break()
    lycon->line.is_line_start = true;
    log_debug("=== TABLE LAYOUT COMPLETE ===");
}
