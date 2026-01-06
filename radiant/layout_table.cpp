#include "layout_table.hpp"
#include "layout.hpp"
#include "intrinsic_sizing.hpp"
#include "../lib/log.h"
#include "../lib/strview.h"
#include "../lib/arraylist.h"
#include "../lib/utf.h"
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
    // Direct children first (handles both normal rows and acts_as_tbody case)
    for (ViewBlock* child = (ViewBlock*)first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW) {
            return (ViewTableRow*)child;
        }
        // Look inside row groups
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRow* row = ((ViewTableRowGroup*)child)->first_row();
            if (row) return row;
        }
    }
    return nullptr;
}

ViewBlock* ViewTable::first_row_group() {
    // If table acts as tbody, return self; otherwise find first row group child
    if (acts_as_tbody()) return this;

    for (ViewBlock* child = (ViewBlock*)first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) return child;
    }
    return nullptr;
}

ViewTableRow* ViewTable::next_row(ViewTableRow* current) {
    if (!current) return nullptr;

    // Try next sibling first
    for (ViewBlock* sibling = (ViewBlock*)current->next_sibling; sibling; sibling = (ViewBlock*)sibling->next_sibling) {
        if (sibling->view_type == RDT_VIEW_TABLE_ROW) return (ViewTableRow*)sibling;
    }

    // If in row group, try next row group
    ViewBlock* parent = (ViewBlock*)current->parent;
    if (parent && parent->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
        for (ViewBlock* next = (ViewBlock*)parent->next_sibling; next; next = (ViewBlock*)next->next_sibling) {
            if (next->view_type == RDT_VIEW_TABLE_ROW) return (ViewTableRow*)next;
            if (next->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                ViewTableRow* row = ((ViewTableRowGroup*)next)->first_row();
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
}ViewTableRow* ViewTableRowGroup::first_row() {
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

// Get parent table from a cell, traversing up through row and row group
static ViewTable* get_parent_table(ViewTableCell* cell) {
    if (!cell) return nullptr;

    // Cell -> Row -> RowGroup/Table -> Table
    DomNode* parent = cell->parent;
    while (parent) {
        if (parent->view_type == RDT_VIEW_TABLE) {
            return (ViewTable*)parent;
        }
        parent = parent->parent;
    }
    return nullptr;
}

// Forward declaration for layout_table_cell_content (defined later in the file)
static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell);

// Get CSS width from a cell element, handling percentage and length values
// Returns 0 if no explicit width is set
static float get_cell_css_width(LayoutContext* lycon, ViewTableCell* tcell, float table_content_width) {
    if (tcell->node_type != DOM_NODE_ELEMENT) return 0.0f;

    DomElement* dom_elem = tcell->as_element();
    if (!dom_elem || !dom_elem->specified_style) return 0.0f;

    CssDeclaration* width_decl = style_tree_get_declaration(
        dom_elem->specified_style, CSS_PROPERTY_WIDTH);
    if (!width_decl || !width_decl->value) return 0.0f;

    float cell_width = 0.0f;
    float css_content_width = 0.0f;

    if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE && table_content_width > 0) {
        double percentage = width_decl->value->data.percentage.value;
        css_content_width = table_content_width * (float)(percentage / 100.0);
        cell_width = css_content_width;
    } else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        float resolved = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
        css_content_width = resolved;
        if (css_content_width > 0) {
            cell_width = css_content_width;
        }
    }

    if (cell_width <= 0) return 0.0f;

    // Add padding (CSS width is content-box)
    if (tcell->bound && tcell->bound->padding.left >= 0 && tcell->bound->padding.right >= 0) {
        cell_width += tcell->bound->padding.left + tcell->bound->padding.right;
    }

    // Add border width (only if border-style is not none)
    if (tcell->bound && tcell->bound->border) {
        float border_left = (tcell->bound->border->left_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.left : 0.0f;
        float border_right = (tcell->bound->border->right_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.right : 0.0f;
        cell_width += border_left + border_right;
    }

    return cell_width;
}

// Get explicit CSS height from a cell or block element
// Returns 0 if no explicit height is set
static float get_explicit_css_height(LayoutContext* lycon, ViewBlock* element) {
    if (element->node_type != DOM_NODE_ELEMENT) return 0.0f;

    DomElement* dom_elem = element->as_element();
    if (!dom_elem || !dom_elem->specified_style) return 0.0f;

    CssDeclaration* height_decl = style_tree_get_declaration(
        dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
    if (!height_decl || !height_decl->value) return 0.0f;

    float resolved = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
    return (resolved > 0) ? resolved : 0.0f;
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
            // Quick Win #2: Check for Unicode whitespace, not just ASCII
            // Unicode whitespace categories: Zs (space separator), Zl (line separator), Zp (paragraph separator)
            // Common whitespace: space (U+0020), tab (U+0009), LF (U+000A), CR (U+000D), NBSP (U+00A0),
            //                    em space (U+2003), thin space (U+2009), zero-width space (U+200B), etc.
            const char* text = ((DomText*)child)->text;
            if (text) {
                const unsigned char* p = (const unsigned char*)text;
                while (*p) {
                    uint32_t codepoint;
                    int bytes = utf8_to_codepoint(p, &codepoint);
                    if (bytes <= 0) break;  // Invalid UTF-8

                    // Check for Unicode whitespace
                    // Basic ASCII whitespace: space, tab, LF, VT, FF, CR
                    bool is_ws = (codepoint == 0x0020 || codepoint == 0x0009 || codepoint == 0x000A ||
                                  codepoint == 0x000B || codepoint == 0x000C || codepoint == 0x000D);

                    // Unicode whitespace characters
                    if (!is_ws) {
                        // U+00A0: Non-breaking space (NBSP)
                        // U+1680: Ogham space mark
                        // U+2000-U+200A: En quad, Em quad, En space, Em space, Three-per-em space,
                        //                 Four-per-em space, Six-per-em space, Figure space,
                        //                 Punctuation space, Thin space, Hair space
                        // U+202F: Narrow no-break space
                        // U+205F: Medium mathematical space
                        // U+3000: Ideographic space
                        is_ws = (codepoint == 0x00A0 || codepoint == 0x1680 ||
                                 (codepoint >= 0x2000 && codepoint <= 0x200A) ||
                                 codepoint == 0x202F || codepoint == 0x205F || codepoint == 0x3000);
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

// Measure content height from cell's children
static float measure_cell_content_height(LayoutContext* lycon, ViewTableCell* tcell) {
    float content_height = 0.0f;
    float block_content_min_y = -1.0f;  // Track min y of block content (for offset)
    float block_content_max_y = 0.0f;   // Track max bottom of block content

    // Set up line-height for this cell so we can use it for text content measurement
    // This ensures we use the cell's own line-height, not a stale value from lycon
    FontBox saved_font = lycon->font;
    BlockContext saved_block = lycon->block;

    if (tcell->font) {
        setup_font(lycon->ui_context, &lycon->font, tcell->font);
    }
    setup_line_height(lycon, tcell);
    float cell_line_height = lycon->block.line_height;

    // Restore context
    lycon->font = saved_font;
    lycon->block = saved_block;

    for (View* child = ((ViewElement*)tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            // CRITICAL: Use the maximum of CSS line-height and actual text bounding box height.
            // For single-line text: line_height (24px from CSS line-height: 1.5) > text->height (18px from font metrics)
            // For wrapped text: text->height (108px from 6 wrapped lines) > line_height (24px)
            // CSS 2.1 §17.5.3: "The height of a cell box is the minimum height required by the content"
            // This ensures both single-line and wrapped content are measured correctly.
            float text_height = max(cell_line_height > 0 ? cell_line_height : 0.0f, text->height);

            if (text_height > content_height) content_height = text_height;
        }
        else if (child->view_type == RDT_VIEW_BLOCK ||
                 child->view_type == RDT_VIEW_INLINE ||
                 child->view_type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* block = (ViewBlock*)child;
            float child_css_height = get_explicit_css_height(lycon, block);
            float child_height = (child_css_height > 0) ? child_css_height : block->height;
            // Track the min y and max bottom of block content for stacked blocks
            // This handles padding offset by computing (max_y - min_y) instead of absolute position
            float child_top = child->y;
            float child_bottom = child->y + child_height;
            if (block_content_min_y < 0 || child_top < block_content_min_y) {
                block_content_min_y = child_top;
            }
            if (child_bottom > block_content_max_y) {
                block_content_max_y = child_bottom;
            }
        }
        else if (child->view_type == RDT_VIEW_TABLE) {
            // Handle nested tables - use the table's computed height
            ViewTable* nested_table = (ViewTable*)child;
            float table_height = nested_table->height;
            log_debug("measure_cell_content: nested table height=%.1f", table_height);
            if (table_height > content_height) content_height = table_height;
        }
    }

    // Compute block content height as the extent from min to max y
    // This correctly handles cells with padding by subtracting the initial offset
    float block_content_height = (block_content_min_y >= 0) ? (block_content_max_y - block_content_min_y) : 0.0f;

    // Use the larger of inline content height or block content extent
    content_height = max(content_height, block_content_height);

    // Return measured content height (no artificial minimum)
    return content_height;
}

// Calculate final cell height from content, padding, border
static float calculate_cell_height(LayoutContext* lycon, ViewTableCell* tcell, ViewTable* table,
                                  float content_height, float explicit_height) {
    // CSS 2.1 §17.5.3: Cell height includes content, padding, and border
    // The CSS 'height' property sets the content height (content-box) or total height (border-box)

    // Check box-sizing mode
    bool is_border_box = (tcell->blk && tcell->blk->box_sizing == CSS_VALUE_BORDER_BOX);

    if (explicit_height > 0 && is_border_box) {
        // In border-box mode, explicit height already includes padding and border
        return explicit_height;
    }

    // Start with content height or explicit height (whichever applies)
    float cell_height = (explicit_height > 0) ? explicit_height : content_height;

    // Add padding
    if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
        cell_height += tcell->bound->padding.top + tcell->bound->padding.bottom;
    }

    // Add border based on collapse mode
    if (tcell->bound && tcell->bound->border) {
        float border_top = (tcell->bound->border->top_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.top : 0.0f;
        float border_bottom = (tcell->bound->border->bottom_style != CSS_VALUE_NONE)
            ? tcell->bound->border->width.bottom : 0.0f;

        if (table->tb->border_collapse) {
            cell_height += (border_top + border_bottom) / 2.0f;
        } else {
            cell_height += border_top + border_bottom;
        }
    }

    return cell_height;
}

// Apply vertical alignment to cell children
static void apply_cell_vertical_align(ViewTableCell* tcell, float cell_height, float content_height) {
    log_debug("apply_cell_vertical_align: valign=%d, cell_height=%.1f, content_height=%.1f, is_empty=%d",
           tcell->td->vertical_align, cell_height, content_height, tcell->td->is_empty);

    // Quick Win #3: Empty cells with baseline alignment should use bottom alignment
    // CSS 2.1: Empty cells don't have a baseline, so treat like bottom-aligned
    if (tcell->td->is_empty && tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BASELINE) {
        log_debug("  Empty cell with baseline -> treating as bottom alignment");
        tcell->td->vertical_align = TableCellProp::CELL_VALIGN_BOTTOM;
    }

    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_TOP) {
        return; // No adjustment needed
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
        log_debug("  Subtracting borders: top=%.1f, bottom=%.1f", border_top, border_bottom);
    }

    // Subtract padding
    if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
        cell_content_area -= (tcell->bound->padding.top + tcell->bound->padding.bottom);
        log_debug("  Subtracting padding: top=%d, bottom=%d", tcell->bound->padding.top, tcell->bound->padding.bottom);
    }

    log_debug("  cell_content_area=%.1f after border/padding subtraction", cell_content_area);

    float y_adjustment = 0.0f;
    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_MIDDLE) {
        y_adjustment = (cell_content_area - content_height) / 2.0f;
    } else if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BOTTOM) {
        y_adjustment = cell_content_area - content_height;
    }

    if (y_adjustment > 0) {
        for (View* child = ((ViewElement*)tcell)->first_child; child; child = child->next_sibling) {
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

    for (View* child = ((ViewElement*)tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            child->x = content_x;
            child->y = content_y;
        }
    }
}

// Calculate cell width from column widths (for colspan support)
static float calculate_cell_width_from_columns(ViewTableCell* tcell, float* col_widths, int columns) {
    float cell_width = 0.0f;
    int end_col = tcell->td->col_index + tcell->td->col_span;
    for (int c = tcell->td->col_index; c < end_col && c < columns; c++) {
        cell_width += col_widths[c];
    }
    return cell_width;
}

// Process a single cell: position, size, layout content, apply alignment
// Returns the height contribution for the current row (adjusted for rowspan)
static float process_table_cell(LayoutContext* lycon, ViewTableCell* tcell, ViewTable* table,
                               float* col_widths, float* col_x_positions, int columns) {
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

    // Calculate cell width from columns (for colspan support)
    float cell_width = 0.0f;
    int end_col = tcell->td->col_index + tcell->td->col_span;
    for (int c = tcell->td->col_index; c < end_col && c < columns; c++) {
        cell_width += col_widths[c];
    }
    cell->width = cell_width;

    // Layout cell content now that width is set
    log_debug("[PROCESS_TABLE_CELL] About to call layout_table_cell_content for cell: %s", cell->node_name());
    layout_table_cell_content(lycon, cell);
    log_debug("[PROCESS_TABLE_CELL] Returned from layout_table_cell_content");

    // Get explicit CSS height and measure content
    float explicit_cell_height = get_explicit_css_height(lycon, cell);
    float content_height = measure_cell_content_height(lycon, tcell);

    // Calculate final cell height
    float cell_height_val = calculate_cell_height(lycon, tcell, table, content_height, explicit_cell_height);
    cell->height = cell_height_val;

    // Apply vertical alignment
    apply_cell_vertical_align(tcell, cell_height_val, content_height);

    // Handle rowspan for row height calculation
    // For single-row cells, full height contributes to row
    // For rowspan cells, we'll handle distribution in a separate pass
    // For now, use a simple heuristic: contribute average height per row
    // The separate pass will adjust if needed
    float height_for_row = cell_height_val;
    if (tcell->td->row_span > 1) {
        // Use average as initial estimate - will be refined by distribute_rowspan_heights
        height_for_row = cell_height_val / tcell->td->row_span;
        log_debug("Rowspan cell - total_height=%.1f, rowspan=%d, initial_per_row=%.1f (will refine)",
                  cell_height_val, tcell->td->row_span, height_for_row);
    }

    return height_for_row;
}

// Apply fixed row height to row and all its cells
// Forward declaration
static float measure_cell_content_height(LayoutContext* lycon, ViewTableCell* tcell);
static void apply_cell_vertical_align(ViewTableCell* tcell, float cell_height, float content_height);

static void apply_fixed_row_height(LayoutContext* lycon, ViewTableRow* trow, float fixed_height) {
    trow->height = fixed_height;
    log_debug("Applied fixed layout row height: %.1fpx", fixed_height);

    for (ViewTableCell* cell = trow->first_cell(); cell; cell = trow->next_cell(cell)) {
        if (cell->height < fixed_height) {
            cell->height = fixed_height;
            // Re-apply vertical alignment with correct cell height
            float content_height = measure_cell_content_height(lycon, cell);
            apply_cell_vertical_align(cell, fixed_height, content_height);
        }
    }
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

// Table metadata cache - Phase 3 optimization
// Stores pre-analyzed table structure to avoid multiple DOM iterations
struct TableMetadata {
    int column_count;           // Total columns
    int row_count;              // Total rows
    bool* grid_occupied;        // colspan/rowspan tracking (row_count × column_count)
    float* col_widths;          // Final column widths
    float* col_min_widths;      // Minimum column widths (CSS MCW)
    float* col_max_widths;      // Maximum column widths (CSS PCW)
    float* row_heights;         // Row heights for rowspan calculation
    float* row_y_positions;     // Row Y positions for rowspan calculation
    bool* row_collapsed;        // Visibility: collapse tracking per row

    TableMetadata(int cols, int rows)
        : column_count(cols), row_count(rows) {
        grid_occupied = (bool*)calloc(rows * cols, sizeof(bool));
        col_widths = (float*)calloc(cols, sizeof(float));
        col_min_widths = (float*)calloc(cols, sizeof(float));  // Minimum content widths (CSS MCW)
        col_max_widths = (float*)calloc(cols, sizeof(float));  // Preferred content widths (CSS PCW)
        row_heights = (float*)calloc(rows, sizeof(float));
        row_y_positions = (float*)calloc(rows, sizeof(float));
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

struct CollapsedBorder {
    float width;
    CssEnum style;      // CSS_VALUE_NONE, CSS_VALUE_HIDDEN, CSS_VALUE_SOLID, etc.
    Color color;        // Border color (simple RGBA union)
    uint8_t priority;   // Used for conflict resolution (higher wins)

    CollapsedBorder() : width(0), style(CSS_VALUE_NONE), priority(0) {
        color.r = color.g = color.b = color.a = 0;
    }
};

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
    // Rule 1: hidden wins
    if (a.style == CSS_VALUE_HIDDEN) return a;
    if (b.style == CSS_VALUE_HIDDEN) return b;

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

// Extract border info from a cell's BoundaryProp
static CollapsedBorder get_cell_border(ViewTableCell* cell, int side) {
    CollapsedBorder border;
    if (!cell || !cell->bound || !cell->bound->border) return border;

    const BorderProp* bp = cell->bound->border;
    switch (side) {
        case 0: // top
            border.width = bp->width.top;
            border.style = bp->top_style;
            // Copy color fields manually
            border.color.r = bp->top_color.r;
            border.color.g = bp->top_color.g;
            border.color.b = bp->top_color.b;
            border.color.a = bp->top_color.a;
            break;
        case 1: // right
            border.width = bp->width.right;
            border.style = bp->right_style;
            border.color.r = bp->right_color.r;
            border.color.g = bp->right_color.g;
            border.color.b = bp->right_color.b;
            border.color.a = bp->right_color.a;
            break;
        case 2: // bottom
            border.width = bp->width.bottom;
            border.style = bp->bottom_style;
            border.color.r = bp->bottom_color.r;
            border.color.g = bp->bottom_color.g;
            border.color.b = bp->bottom_color.b;
            border.color.a = bp->bottom_color.a;
            break;
        case 3: // left
            border.width = bp->width.left;
            border.style = bp->left_style;
            border.color.r = bp->left_color.r;
            border.color.g = bp->left_color.g;
            border.color.b = bp->left_color.b;
            border.color.a = bp->left_color.a;
            break;
    }
    border.priority = get_border_style_priority(border.style);
    return border;
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

// Forward declaration
struct TableMetadata;

// Get table border (outer edge of table element)
static CollapsedBorder get_table_border(ViewTable* table, int side) {
    CollapsedBorder border;
    if (!table || !table->bound || !table->bound->border) return border;

    const BorderProp* bp = table->bound->border;
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

// Get row border (for row elements with borders)
static CollapsedBorder get_row_border(ViewTableRow* row, int side) {
    CollapsedBorder border;
    if (!row || !row->bound || !row->bound->border) return border;

    const BorderProp* bp = row->bound->border;
    switch (side) {
        case 0: // top
            border.width = bp->width.top;
            border.style = bp->top_style;
            border.color = bp->top_color;
            break;
        case 2: // bottom
            border.width = bp->width.bottom;
            border.style = bp->bottom_style;
            border.color = bp->bottom_color;
            break;
    }
    border.priority = get_border_style_priority(border.style);
    return border;
}

// Find cell at specific grid position (handles rowspan/colspan)
static ViewTableCell* find_cell_at(ViewTable* table, int target_row, int target_col) {
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
            int row_start = cell->td->row_index;
            int row_end = row_start + cell->td->row_span;
            int col_start = cell->td->col_index;
            int col_end = col_start + cell->td->col_span;

            if (target_row >= row_start && target_row < row_end &&
                target_col >= col_start && target_col < col_end) {
                return cell;
            }
        }
    }
    return nullptr;
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
            ArrayList* candidates = arraylist_new(4);

            // Border from cell above (bottom border)
            if (row > 0) {
                ViewTableCell* cell_above = find_cell_at(table, row - 1, col);
                if (cell_above) {
                    CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                    *border = get_cell_border(cell_above, 2); // bottom
                    arraylist_append(candidates, border);
                }
            } else {
                // Top edge of table
                CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                *border = get_table_border(table, 0); // top
                arraylist_append(candidates, border);
            }

            // Border from cell below (top border)
            if (row < meta->row_count) {
                ViewTableCell* cell_below = find_cell_at(table, row, col);
                if (cell_below) {
                    CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                    *border = get_cell_border(cell_below, 0); // top
                    arraylist_append(candidates, border);
                }
            } else {
                // Bottom edge of table
                CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                *border = get_table_border(table, 2); // bottom
                arraylist_append(candidates, border);
            }

            // Row borders (if row elements have borders)
            if (row > 0 && row < meta->row_count) {
                ViewTableRow* trow = nullptr;
                int curr = 0;
                for (ViewTableRow* r = table->first_row(); r; r = table->next_row(r)) {
                    if (curr == row - 1) {
                        CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                        *border = get_row_border(r, 2); // bottom of row above
                        if (border->style != CSS_VALUE_NONE) {
                            arraylist_append(candidates, border);
                        } else {
                            free(border);
                        }
                        break;
                    }
                    curr++;
                }
                curr = 0;
                for (ViewTableRow* r = table->first_row(); r; r = table->next_row(r)) {
                    if (curr == row) {
                        CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                        *border = get_row_border(r, 0); // top of row below
                        if (border->style != CSS_VALUE_NONE) {
                            arraylist_append(candidates, border);
                        } else {
                            free(border);
                        }
                        break;
                    }
                    curr++;
                }
            }

            // Select winner from all candidates
            if (candidates->length > 0) {
                CollapsedBorder* winner = (CollapsedBorder*)candidates->data[0];
                for (int i = 1; i < candidates->length; i++) {
                    CollapsedBorder* candidate = (CollapsedBorder*)candidates->data[i];
                    CollapsedBorder result = select_winning_border(*winner, *candidate);
                    *winner = result;
                }

                // Apply winner to affected cells

                if (row > 0) {
                    ViewTableCell* cell_above = find_cell_at(table, row - 1, col);
                    if (cell_above) {
                        apply_collapsed_border_to_cell(lycon, cell_above, *winner, 2);
                    }
                }
                if (row < meta->row_count) {
                    ViewTableCell* cell_below = find_cell_at(table, row, col);
                    if (cell_below) {
                        apply_collapsed_border_to_cell(lycon, cell_below, *winner, 0);
                    }
                }
            }

            // Cleanup
            for (int i = 0; i < candidates->length; i++) {
                free(candidates->data[i]);
            }
            arraylist_free(candidates);
        }
    }

    // Pass 2: Resolve all vertical borders (between columns)
    // For each vertical border position (between col i and i+1, including left/right edges)
    for (int row = 0; row < meta->row_count; row++) {
        for (int col = 0; col <= meta->column_count; col++) {
            // Collect candidate borders for this vertical edge
            ArrayList* candidates = arraylist_new(4);

            // Border from cell to left (right border)
            if (col > 0) {
                ViewTableCell* cell_left = find_cell_at(table, row, col - 1);
                if (cell_left) {
                    CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                    *border = get_cell_border(cell_left, 1); // right
                    arraylist_append(candidates, border);
                }
            } else {
                // Left edge of table
                CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                *border = get_table_border(table, 3); // left
                arraylist_append(candidates, border);
            }

            // Border from cell to right (left border)
            if (col < meta->column_count) {
                ViewTableCell* cell_right = find_cell_at(table, row, col);
                if (cell_right) {
                    CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                    *border = get_cell_border(cell_right, 3); // left
                    arraylist_append(candidates, border);
                }
            } else {
                // Right edge of table
                CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
                *border = get_table_border(table, 1); // right
                arraylist_append(candidates, border);
            }

            // TODO: Add column and column group border support (CSS 2.1 §17.6.2)
            // For now, we handle cell and table borders which covers most cases

            // Select winner from all candidates
            if (candidates->length > 0) {
                CollapsedBorder* winner = (CollapsedBorder*)candidates->data[0];
                for (int i = 1; i < candidates->length; i++) {
                    CollapsedBorder* candidate = (CollapsedBorder*)candidates->data[i];
                    CollapsedBorder result = select_winning_border(*winner, *candidate);
                    *winner = result;
                }

                // Apply winner to affected cells

                if (col > 0) {
                    ViewTableCell* cell_left = find_cell_at(table, row, col - 1);
                    if (cell_left) {
                        apply_collapsed_border_to_cell(lycon, cell_left, *winner, 1);
                    }
                }
                if (col < meta->column_count) {
                    ViewTableCell* cell_right = find_cell_at(table, row, col);
                    if (cell_right) {
                        apply_collapsed_border_to_cell(lycon, cell_right, *winner, 3);
                    }
                }
            }

            // Cleanup
            for (int i = 0; i < candidates->length; i++) {
                free(candidates->data[i]);
            }
            arraylist_free(candidates);
        }
    }

    log_debug("resolve_collapsed_borders: completed - processed %d horizontal and %d vertical borders",
              (meta->row_count + 1) * meta->column_count,
              meta->row_count * (meta->column_count + 1));
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

    int columns = meta->column_count;
    int rows = meta->row_count;

    // Track rowspan cells that need height distribution
    struct RowspanCell {
        ViewTableCell* cell;
        int start_row;
        int end_row;
        float required_height;
        float current_total;
    };

    ArrayList* rowspan_cells = arraylist_new(8);

    // Collect all rowspan cells
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        for (ViewTableCell* tcell = row->first_cell(); tcell; tcell = row->next_cell(tcell)) {
            if (tcell->td->row_span > 1) {
                int start_row = tcell->td->row_index;
                int end_row = start_row + tcell->td->row_span;
                if (end_row > rows) end_row = rows;

                // Calculate current spanned height
                float current_total = 0;
                for (int r = start_row; r < end_row; r++) {
                    current_total += meta->row_heights[r];
                }

                float required_height = tcell->height;

                if (required_height > current_total) {
                    RowspanCell* rsc = (RowspanCell*)malloc(sizeof(RowspanCell));
                    rsc->cell = tcell;
                    rsc->start_row = start_row;
                    rsc->end_row = end_row;
                    rsc->required_height = required_height;
                    rsc->current_total = current_total;
                    arraylist_append(rowspan_cells, rsc);

                    log_debug("Rowspan cell at row %d spans %d rows: needs %.1fpx, currently %.1fpx",
                             start_row, tcell->td->row_span, required_height, current_total);
                }
            }
        }
    }

    // Distribute excess height for each rowspan cell
    for (int i = 0; i < rowspan_cells->length; i++) {
        RowspanCell* rsc = (RowspanCell*)rowspan_cells->data[i];
        float excess = rsc->required_height - rsc->current_total;

        // Calculate total content height of spanned rows for proportional distribution
        float total_content = 0;
        for (int r = rsc->start_row; r < rsc->end_row; r++) {
            total_content += meta->row_heights[r];
        }

        if (total_content > 0) {
            // Proportional distribution based on current row heights
            float distributed = 0;
            for (int r = rsc->start_row; r < rsc->end_row; r++) {
                float proportion = meta->row_heights[r] / total_content;
                float amount = excess * proportion;
                meta->row_heights[r] += amount;
                distributed += amount;
                log_debug("  Row %d: height %.1fpx + %.1fpx (%.1f%% of excess) = %.1fpx",
                         r, meta->row_heights[r] - amount, amount, proportion * 100, meta->row_heights[r]);
            }
            log_debug("Distributed %.1fpx across rows %d-%d (total excess: %.1fpx)",
                     distributed, rsc->start_row, rsc->end_row - 1, excess);
        } else {
            // Equal fallback if all spanned rows have zero height
            int span = rsc->end_row - rsc->start_row;
            float amount_per_row = excess / span;
            for (int r = rsc->start_row; r < rsc->end_row; r++) {
                meta->row_heights[r] += amount_per_row;
                log_debug("  Row %d: height %.1fpx (equal distribution)", r, meta->row_heights[r]);
            }
            log_debug("Distributed %.1fpx equally across rows %d-%d",
                     excess, rsc->start_row, rsc->end_row - 1);
        }
    }

    // Free the arraylist and allocated structs
    for (int i = 0; i < rowspan_cells->length; i++) {
        free(rowspan_cells->data[i]);
    }
    arraylist_free(rowspan_cells);

    log_debug("=== ROWSPAN HEIGHT DISTRIBUTION COMPLETE ===");
}

// =============================================================================
// CSS PROPERTY PARSING
// =============================================================================

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
                float spacing = (float)atof(cellspacing_attr);
                table->tb->border_spacing_h = spacing;
                table->tb->border_spacing_v = spacing;
                log_debug("[HTML] TABLE cellspacing attribute: %.0fpx", spacing);
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

// Helper: Check if a display value is a table type
static inline bool is_table_display(CssEnum display) {
    return display == CSS_VALUE_TABLE || display == CSS_VALUE_INLINE_TABLE;
}

// Helper: Check if a display value is a column type
static inline bool is_column_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_COLUMN || display == CSS_VALUE_TABLE_COLUMN_GROUP;
}

// Helper: Check if a display value is a caption type
static inline bool is_caption_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_CAPTION;
}

// Helper: Check if a child is a "proper table child" per CSS 2.1 Section 17.2.1
// A proper table child is: table-row-group, table-header-group, table-footer-group,
// table-row, table-column-group, table-column, or table-caption
static inline bool is_proper_table_child(CssEnum display) {
    return is_row_group_display(display) ||
           is_row_display(display) ||
           is_column_display(display) ||
           is_caption_display(display);
}

// Helper: Check if a child is a "proper row group child" per CSS 2.1
// A proper row group child is: table-row
static inline bool is_proper_row_group_child(CssEnum display) {
    return is_row_display(display);
}

// Helper: Check if a child is a "proper row child" per CSS 2.1
// A proper row child is: table-cell
static inline bool is_proper_row_child(CssEnum display) {
    return is_cell_display(display);
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
    DomElement* anon = (DomElement*)pool_calloc(pool, sizeof(DomElement));
    if (!anon) return nullptr;

    // Initialize as element node
    anon->node_type = DOM_NODE_ELEMENT;
    anon->tag_name = tag_name;
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
    // Copy inherited properties from parent (font properties are inheritable)
    if (parent->font) {
        anon->font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
        if (anon->font) {
            memcpy(anon->font, parent->font, sizeof(FontProp));
            // Font family string needs to be copied or shared
            anon->font->family = parent->font->family;  // Share the string
        }
    }

    // Copy inherited inline properties (color is inheritable)
    if (parent->in_line) {
        anon->in_line = (InlineProp*)pool_calloc(pool, sizeof(InlineProp));
        if (anon->in_line) {
            // Only copy inheritable properties
            anon->in_line->color = parent->in_line->color;  // color is inherited
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
 * Insert a child element at the beginning of parent's child list
 */
static void prepend_child_to_element(DomElement* parent, DomElement* child) {
    if (!parent || !child) return;

    child->parent = parent;
    child->prev_sibling = nullptr;

    if (parent->first_child) {
        parent->first_child->prev_sibling = child;
        child->next_sibling = parent->first_child;
        parent->first_child = child;
    } else {
        parent->first_child = child;
        parent->last_child = child;
        child->next_sibling = nullptr;
    }
}

/**
 * Move a node from its current parent to a new parent
 * Removes from old parent and appends to new parent
 */
static void reparent_node(DomNode* node, DomElement* new_parent) {
    if (!node || !new_parent) return;

    DomElement* old_parent = (DomElement*)node->parent;

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

/**
 * Collect consecutive runs of children that need to be wrapped together.
 * Returns the first child after the run ends (or nullptr if at end).
 */
static DomNode* collect_consecutive_run(DomNode* start, DomElement* parent,
                                        bool (*should_include)(DomNode*, uintptr_t),
                                        ArrayList* run) {
    arraylist_clear(run);
    DomNode* child = start;

    while (child) {
        if (!child->is_element()) {
            // Text nodes are included if adjacent to other included elements
            if (run->length > 0) {
                arraylist_append(run, child);
            }
            child = child->next_sibling;
            continue;
        }

        uintptr_t tag = child->tag();
        if (should_include(child, tag)) {
            arraylist_append(run, child);
            child = child->next_sibling;
        } else {
            break;
        }
    }

    // Trim trailing non-element nodes
    while (run->length > 0 && !((DomNode*)run->data[run->length - 1])->is_element()) {
        run->length--;
    }

    return child;
}

/**
 * CSS 2.1 Section 17.2.1: Generate anonymous table boxes.
 *
 * This implements the full CSS 2.1 anonymous table box generation algorithm:
 *
 * 1. If a child of a table-row is not a table-cell, wrap it in anonymous table-cell
 * 2. If a child of a table-row-group is not a table-row, wrap consecutive cells in anonymous table-row
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

    log_debug("[ANON-TABLE] === Starting CSS 2.1 anonymous box generation ===");

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
    DomNode* insertion_point = nullptr;  // Where to insert wrapped elements

    for (int i = 0; i < children_to_process->length; ) {
        DomNode* child = (DomNode*)children_to_process->data[i];

        // Skip non-element nodes in this pass (they'll be handled when adjacent to cells)
        if (!child->is_element()) {
            i++;
            continue;
        }

        DisplayValue display = resolve_display_value(child);
        uintptr_t tag = child->tag();

        // Check if this is a proper table child
        bool is_row_group = is_row_group_display(display.inner) ||
                           tag == HTM_TAG_THEAD || tag == HTM_TAG_TBODY || tag == HTM_TAG_TFOOT;
        bool is_row = is_row_display(display.inner) || tag == HTM_TAG_TR;
        bool is_cell = is_cell_display(display.inner) || tag == HTM_TAG_TD || tag == HTM_TAG_TH;
        bool is_column = is_column_display(display.inner) ||
                        tag == HTM_TAG_COL || tag == HTM_TAG_COLGROUP;
        bool is_caption = is_caption_display(display.inner) || tag == HTM_TAG_CAPTION;

        if (is_row_group || is_column || is_caption) {
            // Proper table child - flush any accumulated runs first
            if (current_cell_run->length > 0) {
                log_debug("[ANON-TABLE] Flushing cell run of %d cells before row group",
                         current_cell_run->length);

                // Create anonymous tbody + tr for consecutive cells
                DomElement* anon_tbody = create_anonymous_table_element(lycon, table,
                    CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");
                DomElement* anon_tr = create_anonymous_table_element(lycon, anon_tbody,
                    CSS_VALUE_TABLE_ROW, "::anon-tr");
                append_child_to_element(anon_tbody, anon_tr);

                // Move cells to anonymous tr
                for (int j = 0; j < current_cell_run->length; j++) {
                    DomNode* cell_node = (DomNode*)current_cell_run->data[j];
                    reparent_node(cell_node, anon_tr);
                }

                // Insert at the position of the first cell
                insert_node_before(table, (DomNode*)anon_tbody, child);
                arraylist_clear(current_cell_run);
            }

            if (current_row_run->length > 0) {
                log_debug("[ANON-TABLE] Flushing row run of %d rows before row group",
                         current_row_run->length);

                // Create anonymous tbody for consecutive rows
                DomElement* anon_tbody = create_anonymous_table_element(lycon, table,
                    CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");

                // Move rows to anonymous tbody
                for (int i = 0; i < current_row_run->length; i++) {
                    DomNode* row_node = (DomNode*)current_row_run->data[i];
                    reparent_node(row_node, anon_tbody);
                }

                // Insert at the position of the first row
                insert_node_before(table, (DomNode*)anon_tbody, child);
                arraylist_clear(current_row_run);
            }

            i++;
            continue;
        }

        if (is_row) {
            // Row as direct child of table - accumulate for wrapping in tbody
            if (current_cell_run->length > 0) {
                // Flush cells first - they get their own tbody+tr
                log_debug("[ANON-TABLE] Flushing cell run of %d cells before row",
                         current_cell_run->length);

                DomElement* anon_tbody = create_anonymous_table_element(lycon, table,
                    CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");
                DomElement* anon_tr = create_anonymous_table_element(lycon, anon_tbody,
                    CSS_VALUE_TABLE_ROW, "::anon-tr");
                append_child_to_element(anon_tbody, anon_tr);

                for (int i = 0; i < current_cell_run->length; i++) {
                    DomNode* cell_node = (DomNode*)current_cell_run->data[i];
                    reparent_node(cell_node, anon_tr);
                }

                insert_node_before(table, (DomNode*)anon_tbody, child);
                arraylist_clear(current_cell_run);
            }

            arraylist_append(current_row_run, child);
            i++;
            continue;
        }

        if (is_cell) {
            // Cell as direct child of table - accumulate for wrapping in tbody+tr
            if (current_row_run->length > 0) {
                // Flush rows first
                log_debug("[ANON-TABLE] Flushing row run of %d rows before cell",
                         current_row_run->length);

                DomElement* anon_tbody = create_anonymous_table_element(lycon, table,
                    CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");

                for (int j = 0; j < current_row_run->length; j++) {
                    DomNode* row_node = (DomNode*)current_row_run->data[j];
                    reparent_node(row_node, anon_tbody);
                }

                insert_node_before(table, (DomNode*)anon_tbody, child);
                arraylist_clear(current_row_run);
            }

            arraylist_append(current_cell_run, child);
            i++;
            continue;
        }

        // Non-table content (text, inline elements, etc.) - wrap in cell
        // CSS 2.1: "Any other child of a table element is treated as if it were
        // wrapped in an anonymous table-cell box"
        log_debug("[ANON-TABLE] Non-table content needs cell wrapping: tag=%s",
                 child->node_name() ? child->node_name() : "unknown");

        // For simplicity, treat non-table content as a cell that will be wrapped later
        arraylist_append(current_cell_run, child);
        i++;
    }

    // Flush any remaining runs
    if (current_cell_run->length > 0) {
        log_debug("[ANON-TABLE] Flushing final cell run of %d cells", current_cell_run->length);

        DomElement* anon_tbody = create_anonymous_table_element(lycon, table,
            CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");
        DomElement* anon_tr = create_anonymous_table_element(lycon, anon_tbody,
            CSS_VALUE_TABLE_ROW, "::anon-tr");
        append_child_to_element(anon_tbody, anon_tr);

        for (int i = 0; i < current_cell_run->length; i++) {
            DomNode* cell_node = (DomNode*)current_cell_run->data[i];
            reparent_node(cell_node, anon_tr);
        }

        append_child_to_element(table, anon_tbody);
    }

    if (current_row_run->length > 0) {
        log_debug("[ANON-TABLE] Flushing final row run of %d rows", current_row_run->length);

        DomElement* anon_tbody = create_anonymous_table_element(lycon, table,
            CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");

        for (int i = 0; i < current_row_run->length; i++) {
            DomNode* row_node = (DomNode*)current_row_run->data[i];
            reparent_node(row_node, anon_tbody);
        }

        append_child_to_element(table, anon_tbody);
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
        uintptr_t tag = child->tag();

        // Only process row groups
        if (!is_row_group_display(display.inner) &&
            tag != HTM_TAG_THEAD && tag != HTM_TAG_TBODY && tag != HTM_TAG_TFOOT) {
            continue;
        }

        // Collect children that need wrapping
        ArrayList* group_children = arraylist_new(8);
        for (DomNode* gchild = row_group->first_child; gchild; gchild = gchild->next_sibling) {
            arraylist_append(group_children, gchild);
        }

        ArrayList* cell_run = arraylist_new(8);
        DomNode* first_cell_position = nullptr;

        for (int j = 0; j < group_children->length; j++) {
            DomNode* gchild = (DomNode*)group_children->data[j];

            if (!gchild->is_element()) {
                // Include text nodes in current run if we have cells
                if (cell_run->length > 0) {
                    arraylist_append(cell_run, gchild);
                }
                continue;
            }

            DisplayValue gdisp = resolve_display_value(gchild);
            uintptr_t gtag = gchild->tag();

            bool is_row = is_row_display(gdisp.inner) || gtag == HTM_TAG_TR;
            bool is_cell = is_cell_display(gdisp.inner) || gtag == HTM_TAG_TD || gtag == HTM_TAG_TH;

            if (is_row) {
                // Flush any accumulated cells before this row
                if (cell_run->length > 0) {
                    log_debug("[ANON-TABLE] Wrapping %d cells in row group in anonymous row",
                             cell_run->length);

                    DomElement* anon_tr = create_anonymous_table_element(lycon, row_group,
                        CSS_VALUE_TABLE_ROW, "::anon-tr");

                    for (int i = 0; i < cell_run->length; i++) {
                        DomNode* cell_node = (DomNode*)cell_run->data[i];
                        reparent_node(cell_node, anon_tr);
                    }

                    insert_node_before(row_group, (DomNode*)anon_tr, gchild);
                    arraylist_clear(cell_run);
                    first_cell_position = nullptr;
                }
                continue;
            }

            if (is_cell) {
                if (cell_run->length == 0) {
                    first_cell_position = gchild;
                }
                arraylist_append(cell_run, gchild);
                continue;
            }

            // Non-row, non-cell content - treat as content to wrap in cell then row
            if (cell_run->length == 0) {
                first_cell_position = gchild;
            }
            arraylist_append(cell_run, gchild);
        }

        // Flush remaining cells
        if (cell_run->length > 0) {
            log_debug("[ANON-TABLE] Wrapping final %d cells in row group in anonymous row",
                     cell_run->length);

            DomElement* anon_tr = create_anonymous_table_element(lycon, row_group,
                CSS_VALUE_TABLE_ROW, "::anon-tr");

            for (int i = 0; i < cell_run->length; i++) {
                DomNode* cell_node = (DomNode*)cell_run->data[i];
                reparent_node(cell_node, anon_tr);
            }

            append_child_to_element(row_group, anon_tr);
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
        uintptr_t group_tag = group_node->tag();

        // Only process row groups
        if (!is_row_group_display(group_display.inner) &&
            group_tag != HTM_TAG_THEAD && group_tag != HTM_TAG_TBODY && group_tag != HTM_TAG_TFOOT) {
            continue;
        }

        // Process rows in this group
        for (DomNode* row_node = row_group->first_child; row_node; row_node = row_node->next_sibling) {
            if (!row_node->is_element()) continue;

            DomElement* row = row_node->as_element();
            DisplayValue row_display = resolve_display_value(row_node);
            uintptr_t row_tag = row_node->tag();

            // Only process rows
            if (!is_row_display(row_display.inner) && row_tag != HTM_TAG_TR) {
                continue;
            }

            // Collect non-cell children that need wrapping
            ArrayList* row_children = arraylist_new(8);
            for (DomNode* rchild = row->first_child; rchild; rchild = rchild->next_sibling) {
                arraylist_append(row_children, rchild);
            }

            ArrayList* non_cell_run = arraylist_new(8);

            for (int k = 0; k < row_children->length; k++) {
                DomNode* rchild = (DomNode*)row_children->data[k];

                if (!rchild->is_element()) {
                    // Include text nodes in current run
                    if (non_cell_run->length > 0) {
                        arraylist_append(non_cell_run, rchild);
                    }
                    continue;
                }

                DisplayValue rdisp = resolve_display_value(rchild);
                uintptr_t rtag = rchild->tag();

                bool is_cell = is_cell_display(rdisp.inner) || rtag == HTM_TAG_TD || rtag == HTM_TAG_TH;

                if (is_cell) {
                    // Flush any accumulated non-cell content
                    if (non_cell_run->length > 0) {
                        log_debug("[ANON-TABLE] Wrapping %d non-cell items in row in anonymous cell",
                                 non_cell_run->length);

                        DomElement* anon_td = create_anonymous_table_element(lycon, row,
                            CSS_VALUE_TABLE_CELL, "::anon-td");

                        for (int m = 0; m < non_cell_run->length; m++) {
                            DomNode* content_node = (DomNode*)non_cell_run->data[m];
                            reparent_node(content_node, anon_td);
                        }

                        insert_node_before(row, (DomNode*)anon_td, rchild);
                        arraylist_clear(non_cell_run);
                    }
                    continue;
                }

                // Non-cell content in row
                arraylist_append(non_cell_run, rchild);
            }

            // Flush remaining non-cell content
            if (non_cell_run->length > 0) {
                log_debug("[ANON-TABLE] Wrapping final %d non-cell items in row in anonymous cell",
                         non_cell_run->length);

                DomElement* anon_td = create_anonymous_table_element(lycon, row,
                    CSS_VALUE_TABLE_CELL, "::anon-td");

                for (int m = 0; m < non_cell_run->length; m++) {
                    DomNode* content_node = (DomNode*)non_cell_run->data[m];
                    reparent_node(content_node, anon_td);
                }

                append_child_to_element(row, anon_td);
            }

            // Free ArrayLists from this row
            arraylist_free(row_children);
            arraylist_free(non_cell_run);
        }
    }

    log_debug("[ANON-TABLE] === Anonymous box generation complete ===");
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
    } else if (elem->specified_style && elem->specified_style->tree) {
        AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
        if (float_node) {
            StyleNode* style_node = (StyleNode*)float_node->declaration;
            if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                CssValue* val = style_node->winning_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    float_value = val->data.keyword;
                }
            }
        }
    }

    // If floated, treat as a regular block element and skip table-specific handling
    if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
        log_debug("[TABLE] Floated element %s inside table - treating as block, not table internal", node->node_name());

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

            BlockContext saved_block = lycon->block;
            Linebox saved_line = lycon->line;

            int caption_width = lycon->line.right - lycon->line.left;
            if (caption_width <= 0) caption_width = 600;

            // Calculate content width by subtracting padding and border (CSS box model)
            float content_width = (float)caption_width;
            if (caption->bound) {
                content_width -= caption->bound->padding.left + caption->bound->padding.right;
                if (caption->bound->border) {
                    content_width -= caption->bound->border->width.left + caption->bound->border->width.right;
                }
            }
            content_width = max(content_width, 0.0f);

            lycon->block.content_width = content_width;
            lycon->block.content_height = 10000;  // Large enough for content
            lycon->block.advance_y = 0;
            lycon->line.left = 0;
            lycon->line.right = (int)content_width;
            lycon->line.advance_x = 0;
            lycon->line.is_line_start = true;

            // Propagate text-align from caption's resolved style (default: center)
            if (caption->blk && caption->blk->text_align) {
                lycon->block.text_align = caption->blk->text_align;
                log_debug("Caption text-align: %d", caption->blk->text_align);
            }

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
            // Add padding to height (advance_y is content height only)
            if (caption->bound) {
                caption->height += caption->bound->padding.top + caption->bound->padding.bottom;
            }
            caption->width = (float)caption_width;  // Also set width explicitly
            log_debug("Caption layout end: caption->height=%.1f (content+padding), advance_y=%.1f", caption->height, lycon->block.advance_y);
            lycon->block = saved_block;
            lycon->line = saved_line;
        }
    }
    else if (tag == HTM_TAG_THEAD || tag == HTM_TAG_TBODY || tag == HTM_TAG_TFOOT ||
             display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
             display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
             display.inner == CSS_VALUE_TABLE_FOOTER_GROUP) {
        // Row group - mark and recurse
        // NOTE: Section type is determined at runtime via get_section_type() method
        ViewTableRowGroup* group = (ViewTableRowGroup*)set_view(lycon, RDT_VIEW_TABLE_ROW_GROUP, node);
        if (group) {
            lycon->view = (View*)group;
            dom_node_resolve_style(node, lycon);  // Resolve styles for proper font inheritance
            DomNode* child = static_cast<DomElement*>(node)->first_child;
            for (; child; child = child->next_sibling) {
                if (child->is_element()) mark_table_node(lycon, child, (ViewElement*)group);
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
                if (child->is_element()) mark_table_node(lycon, child, (ViewElement*)row);
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

    // CSS 2.1 Section 17.2.1: Generate anonymous table boxes BEFORE building view tree
    // This ensures proper table structure for layout regardless of HTML structure
    if (tableNode->is_element()) {
        generate_anonymous_table_boxes(lycon, tableNode->as_element());
    }

    // Recursively mark all table children with correct view types
    if (tableNode->is_element()) {
        DomNode* child = static_cast<DomElement*>(tableNode)->first_child;
        for (; child; child = child->next_sibling) {
            if (child->is_element()) {
                mark_table_node(lycon, child, (ViewElement*)table);
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
    for (View* child = ((ViewElement*)tcell)->first_child; child; child = child->next_sibling) {
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
        for (View* child = ((ViewElement*)tcell)->first_child; child; child = child->next_sibling) {
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

    for (View* child = ((ViewElement*)tcell)->first_child; child; child = child->next_sibling) {
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
        for (View* child = ((ViewElement*)tcell)->first_child; child; child = child->next_sibling) {
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

    // Debug: count children and log their types
    if (tcell->is_element()) {
        int child_count = 0;
        int img_count = 0;
        DomNode* cc = static_cast<DomElement*>(tcell)->first_child;
        for (; cc; cc = cc->next_sibling) {
            child_count++;
            if (cc->tag() == HTM_TAG_IMG) {
                img_count++;
                log_debug("[TABLE CELL CHILDREN] Found IMG child #%d: %s", img_count, cc->node_name());
            } else {
                log_debug("[TABLE CELL CHILDREN] Child #%d: %s (tag=%lu)", child_count, cc->node_name(), cc->tag());
            }
        }
        log_debug("[TABLE CELL CHILDREN] Total children: %d, IMG elements: %d", child_count, img_count);
    }

    // No need to clear text rectangles - this is the first and only layout pass!

    // Save layout context to restore later
    BlockContext saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    DomNode* saved_elmt = lycon->elmt;
    FontBox saved_font = lycon->font;

    // CRITICAL: Set up the cell's font before laying out content
    // This ensures text uses the cell's font-size (e.g., 14px) instead of parent's (e.g., 16px)
    if (tcell->font) {
        setup_font(lycon->ui_context, &lycon->font, tcell->font);
        log_debug("Table cell font setup: family=%s, size=%.1f",
            tcell->font->family ? tcell->font->family : "default", tcell->font->font_size);
    }

    // Update line_height for the new font (must be after setup_font)
    // This ensures text rect height calculation uses correct metrics for the cell's font
    setup_line_height(lycon, tcell);

    // Check if parent table uses border-collapse
    ViewTable* parent_table = get_parent_table(tcell);
    bool border_collapse = parent_table && parent_table->tb && parent_table->tb->border_collapse;

    // Calculate cell border and padding offsets from actual CSS style
    // Content area starts AFTER border and padding
    int border_left = 0;
    int border_top = 0;
    int border_right = 0;
    int border_bottom = 0;

    // Read border widths from cell's bound style (if present)
    if (tcell->bound && tcell->bound->border) {
        if (tcell->bound->border->left_style != CSS_VALUE_NONE) {
            border_left = (int)tcell->bound->border->width.left;
        }
        if (tcell->bound->border->top_style != CSS_VALUE_NONE) {
            border_top = (int)tcell->bound->border->width.top;
        }
        if (tcell->bound->border->right_style != CSS_VALUE_NONE) {
            border_right = (int)tcell->bound->border->width.right;
        }
        if (tcell->bound->border->bottom_style != CSS_VALUE_NONE) {
            border_bottom = (int)tcell->bound->border->width.bottom;
        }
    }

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

    // In border-collapse mode, the cell width is the content width (column width),
    // and borders are shared/collapsed with adjacent cells. Content starts at padding only.
    // In separate mode, borders are part of the cell box and must be subtracted.
    int content_start_x, content_start_y;
    int content_width, content_height;

    if (border_collapse) {
        // Border-collapse: cell width is already the content area width
        // Only add padding offset for content positioning
        content_start_x = padding_left;
        content_start_y = padding_top;
        content_width = (int)cell->width - padding_left - padding_right;
        content_height = (int)cell->height - padding_top - padding_bottom;
        log_debug("Border-collapse cell content: cell=%dx%d, padding=(%d,%d,%d,%d), content_start=(%d,%d), content=%dx%d",
            (int)cell->width, (int)cell->height, padding_left, padding_right, padding_top, padding_bottom,
            content_start_x, content_start_y, content_width, content_height);
    } else {
        // Separate borders: subtract borders from cell dimensions
        content_start_x = border_left + padding_left;
        content_start_y = border_top + padding_top;
        content_width = (int)cell->width - border_left - border_right - padding_left - padding_right;
        content_height = (int)cell->height - border_top - border_bottom - padding_top - padding_bottom;
        log_debug("Separate-borders cell content: cell=%dx%d, border=(%d,%d), padding=(%d,%d,%d,%d), content_start=(%d,%d), content=%dx%d",
            (int)cell->width, (int)cell->height, border_left, border_top,
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
            uintptr_t child_tag = cc->tag();
            if (child_tag == HTM_TAG_IMG) {
                log_debug("[TABLE CELL IMG] Found IMG child in table cell, calling layout_flow_node: %s", cc->node_name());
            }
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
    lycon->font = saved_font;
}

// Forward declaration from layout_text.cpp
extern CssEnum get_white_space_value(DomNode* node);

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
    CssEnum ws_value = get_white_space_value((DomNode*)cell);

    // These values preserve whitespace (don't collapse)
    if (ws_value == CSS_VALUE_PRE ||
        ws_value == CSS_VALUE_PRE_WRAP ||
        ws_value == CSS_VALUE_PRE_LINE ||
        ws_value == CSS_VALUE_BREAK_SPACES) {
        return false;
    }

    return true; // Default: collapse whitespace (normal, nowrap)
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

// Helper: Normalize whitespace in-place to a buffer
// Returns length of normalized text (0 if all whitespace)
// Collapses consecutive whitespace to single space, trims leading/trailing
static size_t normalize_whitespace_to_buffer(const char* text, size_t length, char* buffer, size_t buffer_size) {
    if (!text || length == 0 || !buffer || buffer_size == 0) return 0;

    size_t out_pos = 0;
    bool in_whitespace = true;  // Start as if preceded by whitespace (trims leading)

    for (size_t i = 0; i < length && out_pos < buffer_size - 1; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
            if (!in_whitespace) {
                buffer[out_pos++] = ' ';  // Collapse to single space
                in_whitespace = true;
            }
        } else {
            buffer[out_pos++] = (char)ch;
            in_whitespace = false;
        }
    }

    // Trim trailing whitespace
    while (out_pos > 0 && buffer[out_pos - 1] == ' ') {
        out_pos--;
    }

    buffer[out_pos] = '\0';
    return out_pos;
}

// Result structure for consolidated width measurement
struct CellWidths {
    float min_width;  // Minimum content width (MCW) - narrowest without overflow
    float max_width;  // Maximum content width (PCW) - preferred content width
};

// Measure cell's minimum and maximum content widths in single pass
// This performs accurate measurement using font metrics for CSS 2.1 compliance
// CONSOLIDATED: Combines previous measure_cell_intrinsic_width() and measure_cell_minimum_width()
static CellWidths measure_cell_widths(LayoutContext* lycon, ViewTableCell* cell) {
    CellWidths result = {20.0f, 20.0f}; // CSS minimum usable width
    if (!cell || !cell->is_element()) return result;

    DomElement* cell_elem = cell->as_element();
    if (!cell_elem->first_child) return result; // Empty cell minimum

    // Save current layout context
    BlockContext saved_block = lycon->block;
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
    float min_width = 0.0f;

    // Check if we should collapse whitespace based on CSS white-space property
    bool collapse_ws = should_collapse_whitespace(cell);

    // Measure each child's natural width
    for (DomNode* child = cell_elem->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            // Use unified text measurement from intrinsic_sizing.hpp
            const unsigned char* text = child->text_data();
            if (text && *text) {
                size_t text_len = strlen((const char*)text);

                const char* measure_text = (const char*)text;
                size_t measure_len = text_len;
                static char normalized_buffer[4096];  // Static buffer for normalized text

                if (collapse_ws) {
                    // Check if all whitespace first (fast path)
                    if (is_all_whitespace((const char*)text, text_len)) {
                        continue; // Skip whitespace-only text nodes
                    }
                    // Normalize whitespace to buffer
                    size_t normalized_len = normalize_whitespace_to_buffer(
                        (const char*)text, text_len, normalized_buffer, sizeof(normalized_buffer));
                    log_debug("Cell width measuring text: '%s' -> normalized: '%s'", text, normalized_buffer);
                    if (normalized_len == 0) continue; // Skip if normalized to nothing
                    measure_text = normalized_buffer;
                    measure_len = normalized_len;
                }

                // Use unified intrinsic sizing API - measures both widths in one call
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                    lycon, measure_text, measure_len);

                float text_max = (float)widths.max_content;  // PCW (max-content)
                float text_min = (float)widths.min_content;  // MCW (min-content)
                log_debug("Cell widths: text max=%.2f, min=%.2f (unified API)", text_max, text_min);
                if (text_max > max_width) max_width = text_max;
                if (text_min > min_width) min_width = text_min;
            }
        }
        else if (child->is_element()) {
            // For nested block/inline elements, check for explicit CSS width first
            DomElement* child_elem = child->as_element();

            // CSS 2.1: Floats are taken out of normal flow and don't contribute to intrinsic width
            if (child_elem->specified_style && child_elem->specified_style->tree) {
                AvlNode* float_node = avl_tree_search(child_elem->specified_style->tree, CSS_PROPERTY_FLOAT);
                if (float_node) {
                    StyleNode* style_node = (StyleNode*)float_node->declaration;
                    if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                        CssValue* val = style_node->winning_decl->value;
                        if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                            CssEnum float_val = val->data.keyword;
                            if (float_val == CSS_VALUE_LEFT || float_val == CSS_VALUE_RIGHT) {
                                log_debug("Cell widths: skipping floated element %s (float=%d)",
                                         child->node_name(), float_val);
                                continue; // Skip floats - they don't contribute to content width
                            }
                        }
                    }
                }
            }

            float child_max = 0;
            float child_min = 0;

            if (child_elem->specified_style) {
                CssDeclaration* width_decl = style_tree_get_declaration(
                    child_elem->specified_style, CSS_PROPERTY_WIDTH);
                if (width_decl && width_decl->value && width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    // Resolve length value (handles em, rem, px, etc.)
                    float explicit_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                    child_max = child_min = explicit_width; // Both use explicit width
                }
            }

            // If no explicit CSS width, check for HTML width attribute (for tables)
            if (child_max == 0 && child_min == 0) {
                // Check HTML width attribute directly (don't call dom_node_resolve_style
                // during measurement - it's not safe with the current context)
                DomElement* child_elmt = child->as_element();
                if (child_elmt) {
                    const char* width_attr = child_elmt->get_attribute("width");
                    if (width_attr) {
                        size_t value_len = strlen(width_attr);
                        if (value_len > 0 && width_attr[value_len - 1] != '%') {
                            // Parse pixel value (skip percentages - they need parent context)
                            StrView width_view = strview_init(width_attr, value_len);
                            float width = strview_to_int(&width_view);
                            if (width > 0) {
                                float attr_width = width;  // CSS logical pixels
                                child_max = child_min = attr_width; // Both use attribute width
                                log_debug("Cell widths: using HTML width attribute: %.1fpx for %s",
                                    attr_width, child->node_name());
                            }
                        }
                    }
                }
            }

            // If still no explicit width, measure content
            if (child_max == 0 && child_min == 0) {
                DisplayValue child_display = resolve_display_value(child);

                if (child_display.outer == CSS_VALUE_BLOCK) {
                    // Block elements: measure their content width directly
                    float block_max = 0.0f;
                    float block_min = 0.0f;

                    if (child_elem->first_child) {
                        for (DomNode* grandchild = child_elem->first_child; grandchild; grandchild = grandchild->next_sibling) {
                            if (grandchild->is_text()) {
                                const unsigned char* text = grandchild->text_data();
                                if (text && *text) {
                                    size_t text_len = strlen((const char*)text);
                                    const char* measure_text = (const char*)text;
                                    size_t measure_len = text_len;
                                    static char normalized_buffer2[4096];

                                    if (collapse_ws) {
                                        if (is_all_whitespace((const char*)text, text_len)) continue;
                                        size_t normalized_len = normalize_whitespace_to_buffer(
                                            (const char*)text, text_len, normalized_buffer2, sizeof(normalized_buffer2));
                                        if (normalized_len == 0) continue;
                                        measure_text = normalized_buffer2;
                                        measure_len = normalized_len;
                                    }

                                    TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                                        lycon, measure_text, measure_len);
                                    float gchild_max = (float)widths.max_content;
                                    float gchild_min = (float)widths.min_content;
                                    if (gchild_max > block_max) block_max = gchild_max;
                                    if (gchild_min > block_min) block_min = gchild_min;
                                }
                            }
                        }
                    }

                    child_max = block_max;
                    child_min = block_min;
                    log_debug("Cell widths: block %s max=%.1f, min=%.1f",
                              child->node_name(), child_max, child_min);
                } else {
                    // Inline elements: use intrinsic sizing API for proper measurement
                    // This correctly handles:
                    // - Regular inline elements with text content
                    // - Inline elements with nested block children (block-in-inline)
                    // - Inline elements with table-internal descendants
                    IntrinsicSizes inline_sizes = measure_element_intrinsic_widths(lycon, child_elem);
                    child_max = inline_sizes.max_content;
                    child_min = inline_sizes.min_content;
                    log_debug("Cell widths: inline %s min=%.1f, max=%.1f",
                              child->node_name(), child_min, child_max);
                }
            }

            if (child_max > max_width) max_width = child_max;
            if (child_min > min_width) min_width = child_min;
        }
    }

    // Restore context
    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->elmt = saved_elmt;
    lycon->is_measuring = saved_measuring;
    lycon->font = saved_font; // Restore original font context

    // Add padding and border to both widths
    float padding_horizontal = 0.0f;
    if (cell->bound && cell->bound->padding.left >= 0 && cell->bound->padding.right >= 0) {
        padding_horizontal = (float)(cell->bound->padding.left + cell->bound->padding.right);
    }

    float border_horizontal = 0.0f;
    if (cell->bound && cell->bound->border) {
        border_horizontal = cell->bound->border->width.left + cell->bound->border->width.right;
    }

    max_width += border_horizontal + padding_horizontal;
    min_width += border_horizontal + padding_horizontal;

    // CSS 2.1: Ensure reasonable minimum widths for empty cells
    if (max_width < 16.0f) max_width = 16.0f;
    if (min_width < 16.0f) min_width = 16.0f;

    log_debug("Cell widths: max=%.2f, min=%.2f (content + padding=%.1f + border=%.1f)",
        max_width, min_width, padding_horizontal, border_horizontal);

    // Use ceiling to ensure we always have enough space for content
    result.max_width = ceilf(max_width);
    result.min_width = ceilf(min_width);
    return result;
}

// DEPRECATED: Old separate functions removed - now using measure_cell_widths()
// measure_cell_intrinsic_width() - merged into measure_cell_widths().max_width
// measure_cell_minimum_width() - merged into measure_cell_widths().min_width

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

    // Find and position caption - check both HTML tag and CSS display property
    for (ViewBlock* child = (ViewBlock*)table->first_child;  child;  child = (ViewBlock*)child->next_sibling) {
        // Check for HTML <caption> tag OR CSS display: table-caption
        DisplayValue child_display = resolve_display_value((void*)child);
        bool is_caption = (child->tag() == HTM_TAG_CAPTION) ||
                         (child_display.inner == CSS_VALUE_TABLE_CAPTION);
        if (is_caption) {
            caption = child;
            // Caption height calculation: caption->height includes padding, add border + margin
            if (caption->height > 0) {
                float border_v = 0;
                float margin_v = 0;
                if (caption->bound) {
                    // Include margin - caption-side:top means margin-bottom adds space,
                    // caption-side:bottom means margin-top adds space
                    if (table->tb && table->tb->caption_side == TableProp::CAPTION_SIDE_TOP) {
                        margin_v = caption->bound->margin.bottom; // Space between caption and table
                    } else {
                        margin_v = caption->bound->margin.top; // Space between table and caption
                    }
                    if (caption->bound->border) {
                        border_v = caption->bound->border->width.top + caption->bound->border->width.bottom;
                    }
                }
                caption_height = (int)(caption->height + border_v + margin_v);
                log_debug("Caption height calculation: height(content+padding)=%.1f, border_v=%.1f, margin_v=%.1f, total=%d",
                    caption->height, border_v, margin_v, caption_height);
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

    // Step 1.5: Border-collapse resolution
    // CSS 2.1 §17.6.2: Border resolution determines which borders win in conflicts
    // Resolved borders are stored in TableCellProp->*_resolved fields for rendering
    // Layout continues to use original BorderProp widths for positioning calculations
    if (table->tb->border_collapse) {
        log_debug("Resolving collapsed borders for rendering (layout uses original borders)");
        resolve_collapsed_borders(lycon, table, meta);
    }

    // Check if table has explicit width (for percentage cell width calculation)
    int explicit_table_width = 0;
    int table_content_width = 0; // Width available for cells

    // Check if we're in intrinsic sizing mode (propagated via available_space)
    bool is_intrinsic_sizing = lycon->available_space.is_intrinsic_sizing();
    if (is_intrinsic_sizing) {
        log_debug("Table '%s': in intrinsic sizing mode (width=%s)",
            table->node_name(),
            lycon->available_space.width.is_min_content() ? "min-content" : "max-content");
    }

    // First check resolved style (from HTML width attribute or CSS)
    // The given_width is already resolved to absolute pixels during style resolution
    if (table->blk && table->blk->given_width > 0) {
        explicit_table_width = (int)table->blk->given_width;
        log_debug("Table width from resolved style (HTML attr or CSS): %dpx (percent=%s)",
                explicit_table_width,
                !isnan(table->blk->given_width_percent) ? "yes" : "no");
    }

    // If no resolved width, check CSS specified_style directly
    if (explicit_table_width == 0 && table->node_type == DOM_NODE_ELEMENT) {
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
                    int container_width = (int)container_width_f;
                    if (container_width <= 0) {
                        container_width = lycon->line.right - lycon->line.left;
                    }
                    if (container_width > 0) {
                        explicit_table_width = (int)(container_width * percentage / 100.0);
                        log_debug("Table CSS percentage width: %.1f%% of %dpx = %dpx",
                                percentage, container_width, explicit_table_width);
                    }
                }
                // Handle length value (handles em, rem, px, etc.)
                else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    float resolved_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                    explicit_table_width = (int)resolved_width;
                    log_debug("Table CSS explicit width: %dpx", explicit_table_width);
                }
            }
        }
    }

    // Calculate content width if we have an explicit width
    if (explicit_table_width > 0) {
        table_content_width = explicit_table_width;

        // In border-collapse mode, the table border is shared with cell borders
        // so we don't subtract it from content width. In separate mode, subtract it.
        if (!table->tb->border_collapse) {
            // Subtract table border only in separate mode
            if (table->bound && table->bound->border) {
                table_content_width -= (int)(table->bound->border->width.left + table->bound->border->width.right);
            }
        }

        // Subtract table padding
        if (table->bound && table->bound->padding.left >= 0 && table->bound->padding.right >= 0) {
            table_content_width -= table->bound->padding.left + table->bound->padding.right;
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
            float cell_width = get_cell_css_width(lycon, tcell, table_content_width);

            // Calculate both minimum and preferred widths for CSS 2.1 table layout
            float min_width = 0.0f;   // MCW - Minimum Content Width
            float pref_width = 0.0f;  // PCW - Preferred Content Width

            if (cell_width == 0.0f) {
                // No explicit CSS width - measure intrinsic content widths
                CellWidths widths = measure_cell_widths(lycon, tcell);
                pref_width = widths.max_width;  // PCW (preferred/max-content)
                min_width = widths.min_width;   // MCW (minimum/min-content)
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

                // Distribute min_width across col_min_widths (proportional to existing)
                if (min_width > current_min_total) {
                    int extra_needed = min_width - current_min_total;
                    if (current_min_total > 0) {
                        // Proportional distribution based on current min widths
                        int distributed = 0;
                        for (int c = col; c < col + span && c < columns; c++) {
                            float proportion = (float)meta->col_min_widths[c] / (float)current_min_total;
                            int amount = (int)(extra_needed * proportion);
                            meta->col_min_widths[c] += amount;
                            distributed += amount;
                        }
                        // Distribute remainder to first column to ensure we use all extra_needed
                        if (distributed < extra_needed && col < columns) {
                            meta->col_min_widths[col] += (extra_needed - distributed);
                        }
                    } else {
                        // Equal distribution if all columns are zero
                        int extra_per_col = extra_needed / span;
                        int remainder = extra_needed % span;
                        for (int c = col; c < col + span && c < columns; c++) {
                            meta->col_min_widths[c] += extra_per_col;
                            if (remainder > 0) { meta->col_min_widths[c]++; remainder--; }
                        }
                    }
                }

                // Distribute pref_width across col_max_widths (proportional to existing)
                if (pref_width > current_max_total) {
                    int extra_needed = pref_width - current_max_total;
                    if (current_max_total > 0) {
                        // Proportional distribution based on current max widths
                        int distributed = 0;
                        for (int c = col; c < col + span && c < columns; c++) {
                            float proportion = (float)meta->col_max_widths[c] / (float)current_max_total;
                            int amount = (int)(extra_needed * proportion);
                            meta->col_max_widths[c] += amount;
                            distributed += amount;
                        }
                        // Distribute remainder to first column to ensure we use all extra_needed
                        if (distributed < extra_needed && col < columns) {
                            meta->col_max_widths[col] += (extra_needed - distributed);
                        }
                    } else {
                        // Equal distribution if all columns are zero
                        int extra_per_col = extra_needed / span;
                        int remainder = extra_needed % span;
                        for (int c = col; c < col + span && c < columns; c++) {
                            meta->col_max_widths[c] += extra_per_col;
                            if (remainder > 0) { meta->col_max_widths[c]++; remainder--; }
                        }
                    }
                }

                // Also update col_widths for backward compatibility (proportional)
                if (cell_width > current_col_total) {
                    int extra_needed = cell_width - current_col_total;
                    if (current_col_total > 0) {
                        // Proportional distribution based on current widths
                        int distributed = 0;
                        for (int c = col; c < col + span && c < columns; c++) {
                            float proportion = (float)col_widths[c] / (float)current_col_total;
                            int amount = (int)(extra_needed * proportion);
                            col_widths[c] += amount;
                            distributed += amount;
                        }
                        // Distribute remainder to first column to ensure we use all extra_needed
                        if (distributed < extra_needed && col < columns) {
                            col_widths[col] += (extra_needed - distributed);
                        }
                    } else {
                        // Equal distribution if all columns are zero
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
                        // Use AvailableSpace if definite, otherwise fall back to BlockContext
                        float container_width_f = lycon->available_space.width.is_definite()
                            ? lycon->available_space.width.value
                            : lycon->block.content_width;
                        int container_width = (int)container_width_f;
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
            // CSS 2.1 Section 17.5.2.1: When width is 'auto', the table width is 
            // determined by content (shrink-to-fit). Use PCW sum as content width.
            // Calculate preferred width from column max widths
            int pref_content_width = 0;
            for (int i = 0; i < columns; i++) {
                pref_content_width += meta->col_max_widths[i];
            }
            fixed_explicit_width = pref_content_width;
            log_debug("FIXED LAYOUT - width:auto, using content-based sizing: %dpx (from PCW)", fixed_explicit_width);
        }

        // Store for later use
        fixed_table_width = fixed_explicit_width;
        if (fixed_table_width > 0) {
            log_debug("FIXED LAYOUT - stored fixed_table_width: %dpx", fixed_table_width);
        }

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
        float* explicit_col_widths = (float*)calloc(columns, sizeof(float));
        float total_explicit = 0.0f;  int unspecified_cols = 0;

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
                    float cell_width = 0.0f;
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
                                    cell_width = (float)(content_width * percentage / 100.0);
                                    log_debug("  Column %d: percentage width %.1f%% of %dpx = %.1fpx",
                                            col, percentage, content_width, cell_width);
                                } else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                    // Resolve length value (handles em, rem, px, etc.)
                                    cell_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                                    log_debug("  Column %d: absolute width %.1fpx", col, cell_width);
                                }
                            }
                        }
                    }

                    if (cell_width > 0.0f) {
                        explicit_col_widths[col] = cell_width;
                        total_explicit += cell_width;
                        log_debug("  Column %d: explicit width %.1fpx", col, cell_width);
                    } else {
                        unspecified_cols++;
                        log_debug("  Column %d: no explicit width", col);
                    }
                    col += cell->td->col_span;
            }
        }

        // STEP 4: Distribute widths according to CSS table-layout: fixed algorithm
        if (total_explicit > 0.0f) {
            log_debug("Found %d columns with explicit widths (total: %.1fpx), %d unspecified",
                columns - unspecified_cols, total_explicit, unspecified_cols);

            // Distribute remaining width to unspecified columns
            float remaining_width = content_width - total_explicit;
            if (unspecified_cols > 0 && remaining_width > 0.0f) {
                float width_per_unspecified = remaining_width / unspecified_cols;
                for (int i = 0; i < columns; i++) {
                    if (explicit_col_widths[i] == 0.0f) {
                        explicit_col_widths[i] = width_per_unspecified;
                    }
                }
                log_debug("Distributing %.1fpx to %d unspecified columns (%.1fpx each)",
                       remaining_width, unspecified_cols, width_per_unspecified);
            } else if (unspecified_cols > 0) {
                // Not enough space even for explicit widths, scale everything
                float scale_factor = (float)content_width / total_explicit;
                for (int i = 0; i < columns; i++) {
                    if (explicit_col_widths[i] > 0.0f) {
                        explicit_col_widths[i] = explicit_col_widths[i] * scale_factor;
                    }
                }
                // Distribute any remainder
                float scaled_total = 0.0f;
                for (int i = 0; i < columns; i++) scaled_total += explicit_col_widths[i];
                float remainder = content_width - scaled_total;
                for (int i = 0; i < columns && remainder > 0.0f; i++) {
                    if (explicit_col_widths[i] == 0.0f) {
                        explicit_col_widths[i] = remainder / unspecified_cols;
                    }
                }
                log_debug("Scaled explicit widths by %.2f to fit content width", scale_factor);
            }
        } else {
            // No explicit widths, distribute equally
            float width_per_col = (float)content_width / columns;
            for (int i = 0; i < columns; i++) {
                explicit_col_widths[i] = width_per_col;
            }
            log_debug("No explicit widths - equal distribution: %.1fpx per column", width_per_col);
        }

        // STEP 5: Replace col_widths with fixed layout widths
        memcpy(col_widths, explicit_col_widths, columns * sizeof(float));
        free(explicit_col_widths);

        log_debug("=== FIXED LAYOUT COMPLETE ===");
        for (int i = 0; i < columns; i++) {
            log_debug("  Final column %d width: %.1fpx", i, col_widths[i]);
        }

        // STEP 6: Handle explicit table HEIGHT for fixed layout
        // If table has height: 300px or height="200" attribute, distribute that height across rows
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
            // Fallback to HTML height attribute (stored in blk->given_height)
            if (explicit_table_height <= 0 && table->blk && table->blk->given_height > 0) {
                explicit_table_height = (int)table->blk->given_height;
                log_debug("FIXED LAYOUT - read table HTML height attribute: %dpx", explicit_table_height);
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
    // ONLY run auto algorithm if we're NOT using fixed layout
    // Fixed layout already set column widths above
    if (table->tb->table_layout != TableProp::TABLE_LAYOUT_FIXED) {
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
    // For explicit width, we need to account for border and padding
    // (table_content_width already has border/padding/spacing subtracted,
    // but we need the width after border/padding only, before spacing)
    int explicit_content_area = explicit_table_width;
    if (explicit_table_width > 0) {
        // Subtract table border
        if (table->bound && table->bound->border) {
            explicit_content_area -= (int)(table->bound->border->width.left + table->bound->border->width.right);
        }
        // Subtract table padding
        if (table->bound && table->bound->padding.left >= 0 && table->bound->padding.right >= 0) {
            explicit_content_area -= table->bound->padding.left + table->bound->padding.right;
        }
        log_debug("Explicit content area (after border/padding): %dpx", explicit_content_area);
    }

    int used_table_width;
    if (explicit_table_width > 0) {
        // CSS 2.1: Table has explicit width - use content area (but not less than minimum)
        used_table_width = explicit_content_area > min_table_width ? explicit_content_area : min_table_width;
        log_debug("CSS 2.1: Using explicit table content width: %dpx (requested: %dpx)", used_table_width, explicit_content_area);
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
    } // End of auto layout algorithm guard

    // Calculate final table width
    float table_width = 0.0f;
    for (int i = 0; i < columns; i++) {
        table_width += col_widths[i];
        log_debug("Final column %d width: %.1fpx", i, col_widths[i]);
    }

    log_debug("Final table width: %.1fpx", table_width);

    log_debug("table_width before border adjustments: %.1f, border_collapse=%d",
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
    // For auto layout with explicit CSS width in border-collapse mode,
    // use the explicit width as-is (borders collapse into the table area)
    // In separate border mode, the explicit width is content width, borders are added separately
    else if (explicit_table_width > 0 && table->tb->border_collapse) {
        log_debug("Border-collapse explicit width override - changing table_width from %.1f to %d",
               table_width, explicit_table_width);
        table_width = explicit_table_width;
    }

    log_debug("Final table width for layout: %.0fpx", table_width);
    log_debug("===== CSS 2.1 TABLE LAYOUT COMPLETE =====");

    // Step 4: Position cells and calculate row heights with CSS 2.1 border model

    float* col_x_positions = (float*)calloc(columns + 1, sizeof(float));

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

        // Check if caption needs re-layout due to width change
        // This happens because caption was laid out before table width was calculated
        float old_width = caption->width;
        caption->width = table_width;

        // If the width changed significantly, re-layout caption content to reflow text
        if (fabs(table_width - old_width) > 0.5f) {
            log_debug("Caption width changed: %.1f -> %.1f, re-laying out content", old_width, table_width);

            // Reset child views before re-layout
            // This is necessary because the DOM children still have their old view state
            DomElement* dom_elem = static_cast<DomElement*>(caption);
            if (dom_elem) {
                for (DomNode* child = dom_elem->first_child; child; child = child->next_sibling) {
                    if (child->is_text()) {
                        // Reset text node's view state
                        child->view_type = RDT_VIEW_NONE;
                        ViewText* text_view = (ViewText*)child;
                        text_view->rect = nullptr;
                        text_view->width = 0;
                        text_view->height = 0;
                    }
                }
            }

            // Note: We do NOT clear caption->first_child because that's the DOM tree link!
            // The views share memory with DOM nodes (ViewElement extends DomElement)

            // Save and set up layout context for caption
            BlockContext saved_block = lycon->block;
            Linebox saved_line = lycon->line;
            View* saved_view = lycon->view;

            // Calculate content width by subtracting padding and border (CSS box model)
            float content_width = (float)table_width;
            if (caption->bound) {
                content_width -= caption->bound->padding.left + caption->bound->padding.right;
                if (caption->bound->border) {
                    content_width -= caption->bound->border->width.left + caption->bound->border->width.right;
                }
            }
            content_width = max(content_width, 0.0f);

            lycon->view = (View*)caption;
            // Re-resolve caption styles to refresh font in layout context
            dom_node_resolve_style((DomNode*)caption, lycon);

            lycon->block.content_width = content_width;
            lycon->block.content_height = 10000;
            lycon->block.advance_y = 0;
            lycon->line.left = 0;
            lycon->line.right = (int)content_width;
            lycon->line.advance_x = 0;
            lycon->line.is_line_start = true;
            line_reset(lycon);

            // Propagate text-align from caption's resolved style
            if (caption->blk && caption->blk->text_align) {
                lycon->block.text_align = caption->blk->text_align;
            }

            // Re-layout caption content
            if (dom_elem) {
                DomNode* child = dom_elem->first_child;
                log_debug("Caption re-layout: dom_elem=%p, first_child=%p", dom_elem, child);
                for (; child; child = child->next_sibling) {
                    log_debug("Caption re-layout: laying out child %s (%p)", child->node_name(), child);
                    layout_flow_node(lycon, child);
                }
                log_debug("Caption re-layout: after children, advance_y=%.1f, is_line_start=%d",
                          lycon->block.advance_y, lycon->line.is_line_start);
                if (!lycon->line.is_line_start) { line_break(lycon); }
            }

            caption->height = lycon->block.advance_y;
            // Add padding to height (advance_y is content height only)
            if (caption->bound) {
                caption->height += caption->bound->padding.top + caption->bound->padding.bottom;
            }
            log_debug("Caption re-layout complete: width=%.1f, height=%.1f (content+padding)", table_width, caption->height);

            // Recalculate caption_height since we re-laid out the caption
            // Note: caption->height now includes padding (content + padding)
            float border_v = 0;
            float margin_v = 0;
            if (caption->bound) {
                if (table->tb && table->tb->caption_side == TableProp::CAPTION_SIDE_TOP) {
                    margin_v = caption->bound->margin.bottom;
                } else {
                    margin_v = caption->bound->margin.top;
                }
                if (caption->bound->border) {
                    border_v = caption->bound->border->width.top + caption->bound->border->width.bottom;
                }
            }
            caption_height = (int)(caption->height + border_v + margin_v);
            log_debug("Caption height recalculated after re-layout: %d (height includes padding)", caption_height);

            // Update current_y since caption_height changed
            // current_y was set before re-layout with old caption_height
            current_y = caption_height + table_border_top + table_padding_top;
            if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
                current_y += table->tb->border_spacing_v;
            }
            log_debug("Updated current_y after caption re-layout: %d", current_y);

            lycon->block = saved_block;
            lycon->line = saved_line;
            lycon->view = saved_view;
        } else {
            // Just re-align caption text content for centering
            if (caption->blk && caption->blk->text_align == CSS_VALUE_CENTER) {
                for (View* child = caption->first_child; child; child = child->next_sibling) {
                    if (child->view_type == RDT_VIEW_TEXT) {
                        ViewText* text = (ViewText*)child;
                        // Center the text within the caption width
                        float text_width = text->width;
                        float offset = (table_width - text_width) / 2.0f;
                        if (offset > 0) {
                            text->x = offset;
                            if (text->rect) {
                                text->rect->x = offset;
                            }
                        }
                    }
                }
            }
        }

        log_debug("Positioned caption at top: y=0, width=%.1f", table_width);
    }

    // Global row index for tracking row positions across all row groups
    int global_row_index = 0;

    // =========================================================================
    // CSS 2.1 Section 17.2: Visual ordering of row groups
    // Row groups must be rendered in order: THEAD → TBODY → TFOOT
    // regardless of their order in the DOM source.
    // Collect all row groups and sort them by section type before layout.
    // =========================================================================

    // First pass: collect row groups and direct rows, grouped by section type
    ArrayList* thead_groups = arraylist_new(2);
    ArrayList* tbody_groups = arraylist_new(4);
    ArrayList* tfoot_groups = arraylist_new(2);
    ArrayList* direct_rows = arraylist_new(8);

    for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = (ViewTableRowGroup*)child;
            switch (group->get_section_type()) {
                case TABLE_SECTION_THEAD:
                    arraylist_append(thead_groups, child);
                    break;
                case TABLE_SECTION_TFOOT:
                    arraylist_append(tfoot_groups, child);
                    break;
                case TABLE_SECTION_TBODY:
                default:
                    arraylist_append(tbody_groups, child);
                    break;
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            // Direct rows are treated as part of tbody
            arraylist_append(direct_rows, child);
        }
    }

    // Build ordered list: THEAD → TBODY (with direct rows interleaved) → TFOOT
    // The comment about direct_rows being handled separately is outdated - they need to go before TFOOT
    ArrayList* ordered_elements = arraylist_new(thead_groups->length + tbody_groups->length + direct_rows->length + tfoot_groups->length);
    for (int i = 0; i < thead_groups->length; i++) arraylist_append(ordered_elements, thead_groups->data[i]);
    for (int i = 0; i < tbody_groups->length; i++) arraylist_append(ordered_elements, tbody_groups->data[i]);
    // Direct rows are part of the implicit tbody section - must come before TFOOT
    for (int i = 0; i < direct_rows->length; i++) arraylist_append(ordered_elements, direct_rows->data[i]);
    for (int i = 0; i < tfoot_groups->length; i++) arraylist_append(ordered_elements, tfoot_groups->data[i]);

    log_debug("Row group ordering: %d thead, %d tbody, %d direct rows, %d tfoot (total %d)",
              thead_groups->length, tbody_groups->length, direct_rows->length, tfoot_groups->length,
              ordered_elements->length);

    // Process elements in visual order (THEAD groups → TBODY groups → direct rows → TFOOT groups)
    for (int _i = 0; _i < ordered_elements->length; _i++) {
        ViewBlock* child = (ViewBlock*)ordered_elements->data[_i];
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
                    float row_height = 0.0f;
                    ViewTableRow* trow = (ViewTableRow*)row;
                    for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                        float height_for_row = process_table_cell(lycon, tcell, table, col_widths, col_x_positions, columns);
                        if (height_for_row > row_height) {
                            row_height = height_for_row;
                        }
                    }

                    // CSS 2.1 §17.5.3: Check for explicit CSS height on the row
                    float explicit_row_height = 0.0f;
                    if (row->is_element()) {
                        DomElement* row_elem = row->as_element();
                        if (row_elem->specified_style) {
                            CssDeclaration* height_decl = style_tree_get_declaration(
                                row_elem->specified_style, CSS_PROPERTY_HEIGHT);
                            if (height_decl && height_decl->value) {
                                float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                                if (resolved_height > 0) {
                                    explicit_row_height = resolved_height;
                                    log_debug("Row has explicit CSS height: %.1fpx", explicit_row_height);
                                }
                            }
                        }
                    }

                    // Use the larger of content height and explicit CSS height
                    if (explicit_row_height > row_height) {
                        row_height = explicit_row_height;
                        log_debug("Using explicit row height %.1fpx instead of content height", row_height);
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
                                float content_height = measure_cell_content_height(lycon, tcell);
                                apply_cell_vertical_align(tcell, tcell->height, content_height);
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
            // Handle direct table rows (part of implicit tbody, positioned with other tbody content)
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

            float row_height = 0.0f;
            for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                float height_for_row = process_table_cell(lycon, tcell, table, col_widths, col_x_positions, columns);
                if (height_for_row > row_height) {
                    row_height = height_for_row;
                }
            }            // CSS 2.1 §17.5.3: Check for explicit CSS height on the row
            float explicit_row_height = 0.0f;
            if (trow->is_element()) {
                DomElement* row_elem = trow->as_element();
                if (row_elem->specified_style) {
                    CssDeclaration* height_decl = style_tree_get_declaration(
                        row_elem->specified_style, CSS_PROPERTY_HEIGHT);
                    if (height_decl && height_decl->value) {
                        float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                        if (resolved_height > 0) {
                            explicit_row_height = resolved_height;
                            log_debug("Direct row has explicit CSS height: %.1fpx", explicit_row_height);
                        }
                    }
                }
            }

            // Use the larger of content height and explicit CSS height
            if (explicit_row_height > row_height) {
                row_height = explicit_row_height;
            }

            // Apply row height
            trow->height = row_height;

            // Apply height to all cells in the row
            for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                if (tcell->height < row_height) {
                    tcell->height = row_height;
                    float content_height = measure_cell_content_height(lycon, tcell);
                    apply_cell_vertical_align(tcell, tcell->height, content_height);
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

            // Add vertical border-spacing after row (if not last)
            if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
                current_y += table->tb->border_spacing_v;
                log_debug("Added vertical spacing after direct row: +%dpx", table->tb->border_spacing_v);
            }
        }
    }  // End of ordered elements loop

    // NOTE: direct_rows are now processed in the main loop above as part of ordered_elements

    // =========================================================================
    // ROWSPAN HEIGHT DISTRIBUTION
    // Distribute rowspan cell heights proportionally across spanned rows
    // Must happen after single-row cells establish baseline heights
    // =========================================================================
    log_debug("Applying rowspan height distribution");
    distribute_rowspan_heights(table, meta);

    // After distribution, update actual row heights to match meta->row_heights
    // This ensures rows reflect the distributed heights
    for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = (ViewBlock*)child->first_child; row; row = (ViewBlock*)row->next_sibling) {
                if (row->view_type == RDT_VIEW_TABLE_ROW) {
                    ViewTableRow* trow = (ViewTableRow*)row;
                    // Get row index from first cell
                    ViewTableCell* first_cell = trow->first_cell();
                    if (first_cell) {
                        int row_idx = first_cell->td->row_index;
                        if (row_idx >= 0 && row_idx < meta->row_count) {
                            float old_height = row->height;
                            row->height = meta->row_heights[row_idx];
                            if (row->height != old_height) {
                                log_debug("Updated row %d height: %.1fpx -> %.1fpx (after rowspan distribution)",
                                         row_idx, old_height, row->height);
                                // Update single-row cells in this row
                                for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                                    if (tcell->td->row_span == 1 && tcell->height < row->height) {
                                        tcell->height = row->height;
                                        float content_h = measure_cell_content_height(lycon, tcell);
                                        apply_cell_vertical_align(tcell, tcell->height, content_h);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewTableRow* trow = (ViewTableRow*)child;
            ViewTableCell* first_cell = trow->first_cell();
            if (first_cell) {
                int row_idx = first_cell->td->row_index;
                if (row_idx >= 0 && row_idx < meta->row_count) {
                    float old_height = child->height;
                    child->height = meta->row_heights[row_idx];
                    if (child->height != old_height) {
                        log_debug("Updated direct row %d height: %.1fpx -> %.1fpx (after rowspan distribution)",
                                 row_idx, old_height, child->height);
                        for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                            if (tcell->td->row_span == 1 && tcell->height < child->height) {
                                tcell->height = child->height;
                                float content_h = measure_cell_content_height(lycon, tcell);
                                apply_cell_vertical_align(tcell, tcell->height, content_h);
                            }
                        }
                    }
                }
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

    // CSS 2.1 Section 17.5.3: Handle explicit table height
    // If the table has an explicit height and content is smaller, distribute extra space to rows
    int explicit_css_height = 0;
    if (table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value) {
                float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                if (resolved_height > 0) {
                    explicit_css_height = (int)resolved_height;
                    log_debug("Table has explicit CSS height: %dpx", explicit_css_height);
                }
            }
        }
    }

    // Fallback to HTML height attribute (stored in blk->given_height) for auto layout
    if (explicit_css_height <= 0 && table->blk && table->blk->given_height > 0) {
        explicit_css_height = (int)table->blk->given_height;
        log_debug("Table has explicit HTML height attribute: %dpx", explicit_css_height);
    }

    // Calculate what the minimum content height would be (including padding, borders, spacing)
    int min_content_height = current_y;
    int table_padding_vert = 0;
    int table_border_vert = 0;
    int table_spacing_vert = 0;

    if (table->bound && table->bound->padding.top >= 0) {
        table_padding_vert += table->bound->padding.top;
    }
    if (table->bound && table->bound->padding.bottom >= 0) {
        table_padding_vert += table->bound->padding.bottom;
    }
    if (table->bound && table->bound->border) {
        table_border_vert = (int)(table->bound->border->width.top + table->bound->border->width.bottom);
    }
    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
        table_spacing_vert = (int)(2 * table->tb->border_spacing_v);  // Top and bottom edge spacing
    }

    int total_box_overhead = table_padding_vert + table_border_vert + table_spacing_vert;
    int content_only_height = min_content_height - table_padding_vert;  // current_y includes top padding

    // If explicit height is larger than content, distribute extra height to rows
    // CSS 2.1 §17.5.3: Extra height is distributed to body rows only, not header/footer
    if (explicit_css_height > 0 && meta->row_count > 0) {
        int available_for_content = explicit_css_height - table_border_vert;  // CSS height minus borders
        int extra_height = available_for_content - (content_only_height + table_padding_vert + table_spacing_vert);

        log_debug("Table height distribution: explicit=%d, available=%d, content_only=%d, initial_extra=%d, rows=%d",
                 explicit_css_height, available_for_content, content_only_height, extra_height, meta->row_count);

        if (extra_height > 0) {
            // CSS 2.1 §17.5.3: Extra height goes to body rows only
            // Calculate natural heights for all sections and count sections for spacing
            int caption_and_header_height = 0;
            int body_natural_height = 0;
            int body_row_count = 0;
            int section_count = 0;  // Count sections for inter-section spacing

            log_debug("Calculating natural heights for each section");
            for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
                // Check for caption (HTML tag or CSS display)
                DisplayValue child_display = resolve_display_value((void*)child);
                bool is_caption = (child->tag() == HTM_TAG_CAPTION) ||
                                 (child_display.inner == CSS_VALUE_TABLE_CAPTION);

                if (is_caption) {
                    caption_and_header_height += (int)child->height;
                    section_count++;
                    log_debug("  Caption height: %d", (int)child->height);
                } else if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                    ViewTableRowGroup* group = (ViewTableRowGroup*)child;
                    TableSectionType section_type = group->get_section_type();
                    bool is_body_group = (section_type == TABLE_SECTION_TBODY);

                    log_debug("  Row group section_type=%d (TBODY=%d), is_body=%d",
                             section_type, TABLE_SECTION_THEAD, TABLE_SECTION_TBODY, is_body_group);

                    // Calculate this group's natural height (rows only, no inter-row spacing yet)
                    int group_height = 0;
                    int row_count_in_group = 0;
                    for (ViewTableRow* row = group->first_row(); row; row = group->next_row(row)) {
                        // Get row index to access its natural height
                        for (ViewTableCell* tcell = row->first_cell(); tcell; tcell = row->next_cell(tcell)) {
                            int row_idx = tcell->td->row_index;
                            if (row_idx >= 0 && row_idx < meta->row_count) {
                                int row_height = meta->row_heights[row_idx];
                                group_height += row_height;
                                row_count_in_group++;
                                log_debug("    Row %d natural height: %d", row_idx, row_height);
                                break;  // Found row index
                            }
                        }
                    }

                    // DON'T add inter-row spacing here - it's already in the calculation
                    // We'll account for ALL spacing (inter-row and inter-section) separately

                    if (is_body_group) {
                        body_natural_height += group_height;
                        body_row_count += row_count_in_group;
                        section_count++;
                        log_debug("  Body group natural height: %d (rows only), rows: %d", group_height, row_count_in_group);
                    } else {
                        caption_and_header_height += group_height;
                        section_count++;
                        log_debug("  Header/Footer group natural height: %d (rows only)", group_height);
                    }
                }
            }

            // Calculate total spacing: edge spacing + inter-section spacing
            // With border-spacing and N sections, there are N+1 spacing gaps:
            // [top edge] section1 [spacing] section2 [spacing] ... sectionN [bottom edge]
            int total_spacing = 0;
            if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
                // Inter-section spacing (between caption, header, body groups)
                if (section_count > 0) {
                    total_spacing = (section_count + 1) * table->tb->border_spacing_v;
                }
                // Add inter-row spacing within ALL groups (already counted in content_only_height via current_y)
                // We need to calculate how many row boundaries there are
                int total_row_boundaries = (meta->row_count > 0) ? (meta->row_count - 1) : 0;
                total_spacing += total_row_boundaries * table->tb->border_spacing_v;
            }

            // Now calculate extra height available for body rows
            // Formula: extra_for_body = available - padding - all_spacing - caption/header - body_natural
            int extra_for_body = available_for_content - table_padding_vert - total_spacing -
                                caption_and_header_height - body_natural_height;

            log_debug("Height breakdown: caption+header=%d, body_natural=%d, total_spacing=%d (sections=%d, rows=%d), padding=%d",
                     caption_and_header_height, body_natural_height, total_spacing, section_count, meta->row_count, table_padding_vert);
            log_debug("Distributing %dpx extra height to %d body rows (was %dpx initial)",
                     extra_for_body, body_row_count, extra_height);

            if (extra_for_body > 0 && body_row_count > 0) {
                // Distribute extra height equally among body rows only
                int height_per_row = extra_for_body / body_row_count;
                int remainder = extra_for_body % body_row_count;
                int distributed_count = 0;

                // First pass: update meta->row_heights for body rows
                for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
                    if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                        ViewTableRowGroup* group = (ViewTableRowGroup*)child;
                        TableSectionType section_type = group->get_section_type();
                        bool is_body_group = (section_type == TABLE_SECTION_TBODY);

                        if (is_body_group) {
                            for (ViewTableRow* trow = group->first_row(); trow; trow = group->next_row(trow)) {
                                for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                                    int row_idx = tcell->td->row_index;
                                    if (row_idx >= 0 && row_idx < meta->row_count) {
                                        int row_extra = height_per_row + (distributed_count < remainder ? 1 : 0);
                                        meta->row_heights[row_idx] += row_extra;
                                        log_debug("    Body row %d: natural=%d + extra=%d = %d",
                                                 row_idx, meta->row_heights[row_idx] - row_extra, row_extra, meta->row_heights[row_idx]);
                                        distributed_count++;
                                        break;  // Found row index
                                    }
                                }
                            }
                        }
                    }
                }

                // Second pass: recalculate row y positions after height changes
                int y_accum = table_padding_top;
                if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
                    y_accum += table->tb->border_spacing_v;
                }
                for (int r = 0; r < meta->row_count; r++) {
                    meta->row_y_positions[r] = y_accum;
                    y_accum += meta->row_heights[r];
                    if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
                        y_accum += table->tb->border_spacing_v;
                    }
                }
            } else {
                log_debug("No body rows found, skipping height distribution");
            }

            // Now update all row and cell views with new heights
            // Iterate through row groups and direct rows to update their dimensions
            for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
                if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                    // Track max extent within group for updating group height
                    float group_max_y = 0;

                    // Update rows within group
                    for (ViewBlock* row = (ViewBlock*)child->first_child; row; row = (ViewBlock*)row->next_sibling) {
                        if (row->view_type == RDT_VIEW_TABLE_ROW) {
                            ViewTableRow* trow = (ViewTableRow*)row;
                            // Find row index and update
                            for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                                int row_idx = tcell->td->row_index;
                                if (row_idx >= 0 && row_idx < meta->row_count) {
                                    row->height = meta->row_heights[row_idx];
                                    row->y = meta->row_y_positions[row_idx] - child->y;  // Adjust for group offset

                                    // Track max extent for group height
                                    float row_bottom = row->y + row->height;
                                    if (row_bottom > group_max_y) {
                                        group_max_y = row_bottom;
                                    }

                                    // Update cell heights and vertical alignment
                                    for (ViewTableCell* cell = trow->first_cell(); cell; cell = trow->next_cell(cell)) {
                                        if (cell->td->row_span == 1) {
                                            cell->height = row->height;
                                            // Re-apply vertical alignment with new height
                                            int content_h = measure_cell_content_height(lycon, cell);
                                            apply_cell_vertical_align(cell, (int)cell->height, content_h);
                                        }
                                    }
                                    break;  // Found the row index
                                }
                            }
                        }
                    }

                    // Update group height to encompass all rows after height distribution
                    if (group_max_y > 0) {
                        float old_group_height = child->height;
                        child->height = group_max_y;
                        log_debug("Updated row group height from %.1f to %.1f", old_group_height, child->height);
                    }
                } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
                    ViewTableRow* trow = (ViewTableRow*)child;
                    for (ViewTableCell* tcell = trow->first_cell(); tcell; tcell = trow->next_cell(tcell)) {
                        int row_idx = tcell->td->row_index;
                        if (row_idx >= 0 && row_idx < meta->row_count) {
                            child->height = meta->row_heights[row_idx];
                            child->y = meta->row_y_positions[row_idx];

                            // Update cell heights
                            for (ViewTableCell* cell = trow->first_cell(); cell; cell = trow->next_cell(cell)) {
                                if (cell->td->row_span == 1) {
                                    cell->height = child->height;
                                    int content_h = measure_cell_content_height(lycon, cell);
                                    apply_cell_vertical_align(cell, (int)cell->height, content_h);
                                }
                            }
                            break;
                        }
                    }
                }
            }

            // Update current_y to reflect expanded height
            current_y += extra_height;
            final_table_height = current_y;
            log_debug("Updated final_table_height to %d after height distribution", final_table_height);
        }
    }

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

        // Check if caption needs re-layout due to width change (same as top caption)
        float old_width = caption->width;
        caption->width = table_width;

        if (fabs(table_width - old_width) > 0.5f) {
            log_debug("Bottom caption width changed: %.1f -> %.1f, re-laying out content", old_width, table_width);

            // Reset child views before re-layout
            DomElement* dom_elem = static_cast<DomElement*>(caption);
            if (dom_elem) {
                for (DomNode* child = dom_elem->first_child; child; child = child->next_sibling) {
                    if (child->is_text()) {
                        child->view_type = RDT_VIEW_NONE;
                        ViewText* text_view = (ViewText*)child;
                        text_view->rect = nullptr;
                        text_view->width = 0;
                        text_view->height = 0;
                    }
                }
            }

            // Save and set up layout context for caption
            BlockContext saved_block = lycon->block;
            Linebox saved_line = lycon->line;
            View* saved_view = lycon->view;

            // Calculate content width by subtracting padding and border
            float content_width = (float)table_width;
            if (caption->bound) {
                content_width -= caption->bound->padding.left + caption->bound->padding.right;
                if (caption->bound->border) {
                    content_width -= caption->bound->border->width.left + caption->bound->border->width.right;
                }
            }
            content_width = max(content_width, 0.0f);

            lycon->view = (View*)caption;
            // Re-resolve caption styles to refresh font in layout context
            dom_node_resolve_style((DomNode*)caption, lycon);

            lycon->block.content_width = content_width;
            lycon->block.content_height = 10000;
            lycon->block.advance_y = 0;
            lycon->line.left = 0;
            lycon->line.right = (int)content_width;
            lycon->line.advance_x = 0;
            lycon->line.is_line_start = true;
            line_reset(lycon);

            // Propagate text-align from caption's resolved style
            if (caption->blk && caption->blk->text_align) {
                lycon->block.text_align = caption->blk->text_align;
            }

            // Re-layout caption content
            if (dom_elem) {
                DomNode* child = dom_elem->first_child;
                for (; child; child = child->next_sibling) {
                    layout_flow_node(lycon, child);
                }
                if (!lycon->line.is_line_start) { line_break(lycon); }
            }

            caption->height = lycon->block.advance_y;
            // Add padding to height (advance_y is content height only)
            if (caption->bound) {
                caption->height += caption->bound->padding.top + caption->bound->padding.bottom;
            }
            log_debug("Bottom caption re-layout complete: width=%.1f, height=%.1f (content+padding)", table_width, caption->height);

            // Recalculate caption_height
            float border_v = 0;
            float margin_v = caption->bound ? caption->bound->margin.top : 0;  // Bottom caption uses top margin (space above)
            if (caption->bound && caption->bound->border) {
                border_v = caption->bound->border->width.top + caption->bound->border->width.bottom;
            }
            caption_height = (int)(caption->height + border_v + margin_v);
            log_debug("Bottom caption height recalculated after re-layout: %d", caption_height);

            lycon->block = saved_block;
            lycon->line = saved_line;
            lycon->view = saved_view;
        }

        final_table_height += caption_height;  // Add caption height to table
        log_debug("Positioned caption at bottom: y=%d, caption_height=%d", (int)caption->y, caption_height);
    }

    // Override calculated height with explicit height if set and larger than content height
    // CSS 2.2 Section 17.5.3: If the table has an explicit height, use it
    // Note: The height distribution to rows was already done above (around line 4731)
    // Here we just ensure final_table_height respects the explicit height constraint
    if (explicit_css_height > 0 && explicit_css_height > final_table_height) {
        log_debug("Explicit height override - changing final_table_height from %d to %d",
               final_table_height, explicit_css_height);
        final_table_height = explicit_css_height;
    }

    // CRITICAL FIX: Handle table border dimensions correctly for each border model
    // In border-collapse mode, the table border overlaps with cell borders
    // In separate mode, the table border is added around the table
    int table_border_width = 0;
    int table_border_height = 0;

    if (table->bound && table->bound->border) {
        if (table->tb->border_collapse) {
            // Border-collapse: CSS 2.1 Section 17.6.2
            // The table's border-box includes half of the collapsed outer borders.
            // In collapse mode, the table border wins at outer edges (max of table/cell borders).
            // getBoundingClientRect() returns the border-box which includes these half-borders.
            // The collapsed border width added is half of the winning border on each side.
            float table_border_left = table->bound->border->width.left;
            float table_border_right = table->bound->border->width.right;
            float table_border_top = table->bound->border->width.top;
            float table_border_bottom = table->bound->border->width.bottom;

            // Add half of collapsed borders to table dimensions (for border-box reporting)
            table_border_width = (int)(table_border_left / 2.0f + table_border_right / 2.0f + 0.5f);
            table_border_height = (int)(table_border_top / 2.0f + table_border_bottom / 2.0f + 0.5f);
            log_debug("Border-collapse: adding half of collapsed borders to dimensions: width+%d, height+%d",
                   table_border_width, table_border_height);
        } else {
            // Separate borders: add full table border
            table_border_width = (int)(table->bound->border->width.left + table->bound->border->width.right);
            table_border_height = (int)(table->bound->border->width.top + table->bound->border->width.bottom);
            log_debug("Separate borders: table border width=%dpx (left=%.1f, right=%.1f), height=%dpx (top=%.1f, bottom=%.1f)",
                   table_border_width, table->bound->border->width.left, table->bound->border->width.right,
                   table_border_height, table->bound->border->width.top, table->bound->border->width.bottom);
        }
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

    // Cleanup ArrayLists
    arraylist_free(thead_groups);
    arraylist_free(tbody_groups);
    arraylist_free(tfoot_groups);
    arraylist_free(direct_rows);
    arraylist_free(ordered_elements);

    // Cleanup - TableMetadata destructor handles grid_occupied and col_widths
    delete meta;
    free(col_x_positions);

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

    log_debug("[ORPHAN-TABLE] Found orphaned table-internal children in <%s>, creating anonymous wrappers",
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
                // Include text nodes between table-internal elements
                run_end = next;
            } else {
                break;
            }
        }

        // Determine what wrapper we need based on the child display types
        // CSS 2.1: cells need row, rows need table
        bool needs_row = false;
        bool needs_table = false;

        for (DomNode* n = run_start; n; n = n->next_sibling) {
            if (n->is_element()) {
                DisplayValue n_display = resolve_display_value((void*)n);
                CssEnum disp = n_display.inner;

                if (disp == CSS_VALUE_TABLE_CELL) {
                    needs_row = true;
                    needs_table = true;
                } else if (disp == CSS_VALUE_TABLE_ROW) {
                    needs_table = true;
                } else if (disp == CSS_VALUE_TABLE_ROW_GROUP ||
                           disp == CSS_VALUE_TABLE_HEADER_GROUP ||
                           disp == CSS_VALUE_TABLE_FOOTER_GROUP) {
                    needs_table = true;
                }
            }
            if (n == run_end) break;
        }

        // Create anonymous wrappers
        DomElement* table_wrapper = nullptr;
        DomElement* row_wrapper = nullptr;
        DomElement* insertion_parent = nullptr;

        if (needs_table) {
            // Create anonymous table
            // CSS Display Level 3: 'display: table' is shorthand for 'display: block table'
            // outer = BLOCK (how it participates in flow), inner = TABLE (how children are laid out)
            table_wrapper = (DomElement*)pool_calloc(pool, sizeof(DomElement));
            if (table_wrapper) {
                table_wrapper->node_type = DOM_NODE_ELEMENT;
                table_wrapper->tag_name = "::anon-table";
                table_wrapper->doc = parent->doc;
                table_wrapper->display.outer = CSS_VALUE_BLOCK;  // Tables are block-level
                table_wrapper->display.inner = CSS_VALUE_TABLE;  // Inner is table layout
                table_wrapper->styles_resolved = true;

                // Inherit font from parent
                if (parent->font) {
                    table_wrapper->font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
                    if (table_wrapper->font) {
                        memcpy(table_wrapper->font, parent->font, sizeof(FontProp));
                    }
                }
                if (parent->in_line) {
                    table_wrapper->in_line = (InlineProp*)pool_calloc(pool, sizeof(InlineProp));
                    if (table_wrapper->in_line) {
                        table_wrapper->in_line->color = parent->in_line->color;
                        table_wrapper->in_line->visibility = parent->in_line->visibility;
                        table_wrapper->in_line->opacity = 1.0f;
                    }
                }

                insertion_parent = table_wrapper;
                log_debug("[ORPHAN-TABLE] Created anonymous table wrapper");
            }
        }

        if (needs_row && table_wrapper) {
            // Create anonymous table-row inside the table
            // CSS Display Level 3: table-row has implicit block-level outer
            row_wrapper = (DomElement*)pool_calloc(pool, sizeof(DomElement));
            if (row_wrapper) {
                row_wrapper->node_type = DOM_NODE_ELEMENT;
                row_wrapper->tag_name = "::anon-tr";
                row_wrapper->doc = parent->doc;
                row_wrapper->parent = table_wrapper;
                row_wrapper->display.outer = CSS_VALUE_BLOCK;     // Block-level for layout purposes
                row_wrapper->display.inner = CSS_VALUE_TABLE_ROW; // Inner is table-row
                row_wrapper->styles_resolved = true;

                // Inherit font from table wrapper
                if (table_wrapper->font) {
                    row_wrapper->font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
                    if (row_wrapper->font) {
                        memcpy(row_wrapper->font, table_wrapper->font, sizeof(FontProp));
                    }
                }
                if (table_wrapper->in_line) {
                    row_wrapper->in_line = (InlineProp*)pool_calloc(pool, sizeof(InlineProp));
                    if (row_wrapper->in_line) {
                        row_wrapper->in_line->color = table_wrapper->in_line->color;
                        row_wrapper->in_line->visibility = table_wrapper->in_line->visibility;
                        row_wrapper->in_line->opacity = 1.0f;
                    }
                }

                // Add row to table
                table_wrapper->first_child = row_wrapper;
                table_wrapper->last_child = row_wrapper;

                insertion_parent = row_wrapper;
                log_debug("[ORPHAN-TABLE] Created anonymous table-row wrapper");
            }
        }

        if (insertion_parent) {
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

            // Move the run of children into the appropriate wrapper
            DomNode* move_node = run_start;
            while (move_node) {
                DomNode* next_to_move = move_node->next_sibling;
                bool is_last = (move_node == run_end);

                // Reparent this node
                move_node->parent = insertion_parent;
                move_node->prev_sibling = insertion_parent->last_child;
                move_node->next_sibling = nullptr;

                if (insertion_parent->last_child) {
                    insertion_parent->last_child->next_sibling = move_node;
                } else {
                    insertion_parent->first_child = move_node;
                }
                insertion_parent->last_child = move_node;

                if (is_last) break;
                move_node = next_to_move;
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
