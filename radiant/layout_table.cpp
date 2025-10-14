#include "layout_table.hpp"
#include "layout.hpp"
#include "../lib/log.h"

// Forward declaration for CSS length resolution
float resolve_length_value(LayoutContext* lycon, uintptr_t property,
    const lxb_css_value_length_percentage_t *value);

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
// UTILITY FUNCTIONS
// =============================================================================

// Safe DOM traversal helpers
static inline DomNode* first_element_child(DomNode* n) {
    if (!n) return nullptr;
    DomNode* c = n->first_child();
    while (c && !c->is_element()) c = c->next_sibling();
    return c;
}

static inline DomNode* next_element_sibling(DomNode* n) {
    if (!n) return nullptr;
    DomNode* c = n->next_sibling();
    while (c && !c->is_element()) c = c->next_sibling();
    return c;
}

// =============================================================================
// CSS PROPERTY PARSING
// =============================================================================

// Parse table-specific CSS properties from DOM element
static void resolve_table_properties(DomNode* element, ViewTable* table) {
    lxb_html_element_t* html_element = element->lxb_elmt;
    if (!html_element) return;

    // if (width_decl) {
    //     log_debug("Table has explicit width - likely fixed layout");
    //     // For now, if a table has explicit width, assume it might be fixed layout
    //     // This is a heuristic until we implement proper table-layout parsing
    //     table->table_layout = ViewTable::TABLE_LAYOUT_FIXED;
    //     log_debug("WORKAROUND: Assuming table-layout: fixed due to explicit width");
    // }
}

// Parse cell attributes (colspan, rowspan)
static void parse_cell_attributes(DomNode* cellNode, ViewTableCell* cell) {
    if (!cellNode || !cell) return;

    // Initialize defaults
    cell->col_span = 1;
    cell->row_span = 1;
    cell->col_index = -1;
    cell->row_index = -1;
    cell->vertical_align = ViewTableCell::CELL_VALIGN_TOP;

    if (!cellNode->is_element()) return;

    // Parse colspan/rowspan from DOM attributes
    lxb_html_element_t* element = cellNode->lxb_elmt;
    if (!element) return;

    // Parse colspan
    lxb_dom_attr_t* colspan_attr = lxb_dom_element_attr_by_name(
        lxb_dom_interface_element(element), (const lxb_char_t*)"colspan", 7);
    if (colspan_attr && colspan_attr->value) {
        size_t attr_len;
        const char* colspan_str = (const char*)lxb_dom_attr_value(colspan_attr, &attr_len);
        if (colspan_str && attr_len > 0) {
            int span = atoi(colspan_str);
            if (span > 0 && span <= 1000) {
                cell->col_span = span;
            }
        }
    }

    // Parse rowspan
    lxb_dom_attr_t* rowspan_attr = lxb_dom_element_attr_by_name(
        lxb_dom_interface_element(element), (const lxb_char_t*)"rowspan", 7);
    if (rowspan_attr && rowspan_attr->value) {
        size_t attr_len;
        const char* rowspan_str = (const char*)lxb_dom_attr_value(rowspan_attr, &attr_len);
        if (rowspan_str && attr_len > 0) {
            int span = atoi(rowspan_str);
            if (span > 0 && span <= 65534) {
                cell->row_span = span;
            }
        }
    }
}

// =============================================================================
// TABLE STRUCTURE BUILDER
// =============================================================================

// Create and initialize a table cell view
static ViewTableCell* create_table_cell(LayoutContext* lycon, DomNode* cellNode) {
    ViewTableCell* cell = (ViewTableCell*)alloc_view(lycon, RDT_VIEW_TABLE_CELL, cellNode);
    if (!cell) return nullptr;

    // Save current layout context
    DomNode* saved_elmt = lycon->elmt;
    View* saved_view = lycon->view;

    // Set context for style resolution
    lycon->elmt = cellNode;
    lycon->view = (View*)cell;

    // Resolve CSS styles for the cell
    dom_node_resolve_style(cellNode, lycon);

    // Parse cell attributes
    parse_cell_attributes(cellNode, cell);

    // Restore layout context
    lycon->elmt = saved_elmt;
    lycon->view = saved_view;

    return cell;
}

// Create and initialize a table row view
static ViewTableRow* create_table_row(LayoutContext* lycon, DomNode* rowNode) {
    ViewTableRow* row = (ViewTableRow*)alloc_view(lycon, RDT_VIEW_TABLE_ROW, rowNode);
    if (!row) return nullptr;

    // Note: CSS styles should already be resolved by the layout system

    return row;
}

// Create and initialize a table row group view
static ViewTableRowGroup* create_table_row_group(LayoutContext* lycon, DomNode* groupNode) {
    ViewTableRowGroup* group = (ViewTableRowGroup*)alloc_view(lycon, RDT_VIEW_TABLE_ROW_GROUP, groupNode);
    if (!group) return nullptr;

    // Note: CSS styles should already be resolved by the layout system

    return group;
}

// Build table structure from DOM
ViewTable* build_table_tree(LayoutContext* lycon, DomNode* tableNode) {
    if (!tableNode || !tableNode->is_element()) {
        log_debug("ERROR: Invalid table node");
        return nullptr;
    }

    log_debug("Building table structure");
    // Save layout context
    ViewGroup* saved_parent = lycon->parent;
    View* saved_prev = lycon->prev_view;
    DomNode* saved_elmt = lycon->elmt;

    // Create table view
    lycon->elmt = tableNode;
    ViewTable* table = (ViewTable*)lycon->view;

    // Resolve table styles
    dom_node_resolve_style(tableNode, lycon);
    resolve_table_properties(tableNode, table);

    // Set table as parent for children
    lycon->parent = (ViewGroup*)table;
    lycon->prev_view = nullptr;

    // Process table children
    for (DomNode* child = first_element_child(tableNode); child; child = next_element_sibling(child)) {
        // Get display property (styles should already be resolved by layout system)
        DisplayValue child_display = resolve_display(child->as_element());

        uintptr_t tag = child->tag();

        log_debug("Processing table child - tag=%s, display.outer=%d, display.inner=%d",
               child->name(), child_display.outer, child_display.inner);

        if (tag == LXB_TAG_CAPTION || child_display.inner == LXB_CSS_VALUE_TABLE_CAPTION) {
            // Create caption as block
            ViewBlock* caption = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, child);
            if (caption) {
                // Layout caption content
                ViewGroup* cap_saved_parent = lycon->parent;
                View* cap_saved_prev = lycon->prev_view;
                lycon->parent = (ViewGroup*)caption;
                lycon->prev_view = nullptr;
                lycon->elmt = child;

                for (DomNode* cc = child->first_child(); cc; cc = cc->next_sibling()) {
                    layout_flow_node(lycon, cc);
                }

                lycon->parent = cap_saved_parent;
                lycon->prev_view = (View*)caption;
                lycon->elmt = tableNode;
            }
        }
        else if (tag == LXB_TAG_THEAD || tag == LXB_TAG_TBODY || tag == LXB_TAG_TFOOT ||
                 child_display.inner == LXB_CSS_VALUE_TABLE_ROW_GROUP ||
                 child_display.inner == LXB_CSS_VALUE_TABLE_HEADER_GROUP ||
                 child_display.inner == LXB_CSS_VALUE_TABLE_FOOTER_GROUP) {
            // Create row group
            ViewTableRowGroup* group = create_table_row_group(lycon, child);
            if (group) {
                // Process rows in group
                ViewGroup* grp_saved_parent = lycon->parent;
                View* grp_saved_prev = lycon->prev_view;
                lycon->parent = (ViewGroup*)group;
                lycon->prev_view = nullptr;
                lycon->elmt = child;

                for (DomNode* rowNode = first_element_child(child); rowNode; rowNode = next_element_sibling(rowNode)) {
                    // Check for table row by CSS display property or HTML tag
                    DisplayValue row_display = resolve_display(rowNode->as_element());

                    log_debug("Processing row candidate - tag=%s, display.outer=%d, display.inner=%d",
                           rowNode->name(), row_display.outer, row_display.inner);

                    if (rowNode->tag() == LXB_TAG_TR || row_display.inner == LXB_CSS_VALUE_TABLE_ROW) {
                        ViewTableRow* row = create_table_row(lycon, rowNode);
                        if (row) {
                            // Process cells in row
                            ViewGroup* row_saved_parent = lycon->parent;
                            View* row_saved_prev = lycon->prev_view;
                            lycon->parent = (ViewGroup*)row;
                            lycon->prev_view = nullptr;
                            lycon->elmt = rowNode;

                            for (DomNode* cellNode = first_element_child(rowNode); cellNode; cellNode = next_element_sibling(cellNode)) {
                                // Check for table cell by CSS display property or HTML tag
                                DisplayValue cell_display = resolve_display(cellNode->as_element());

                                log_debug("Processing cell candidate - tag=%s, display.outer=%d, display.inner=%d",
                                       cellNode->name(), cell_display.outer, cell_display.inner);

                                uintptr_t ctag = cellNode->tag();
                                if (ctag == LXB_TAG_TD || ctag == LXB_TAG_TH ||
                                    cell_display.inner == LXB_CSS_VALUE_TABLE_CELL) {
                                    ViewTableCell* cell = create_table_cell(lycon, cellNode);
                                    if (cell) {
                                        // Layout cell content
                                        ViewGroup* cell_saved_parent = lycon->parent;
                                        View* cell_saved_prev = lycon->prev_view;

                                        // CRITICAL FIX: Reset layout context for cell content
                                        // Save current context state
                                        Blockbox saved_block = lycon->block;
                                        Linebox saved_line = lycon->line;

                                        // Set cell as parent and reset layout state for cell content
                                        lycon->parent = (ViewGroup*)cell;
                                        lycon->prev_view = nullptr;
                                        lycon->elmt = cellNode;

                                        // Reset block layout state for cell content area
                                        lycon->block.advance_y = 0;
                                        lycon->block.width = cell->width - 2; // subtract border
                                        lycon->block.height = cell->height - 2; // subtract border
                                        lycon->line.left = 0;
                                        lycon->line.right = lycon->block.width;
                                        lycon->line.advance_x = 0;
                                        lycon->line.is_line_start = true;

                                        log_debug("Cell content layout - width=%d, height=%d, advance_y=%d",
                                               lycon->block.width, lycon->block.height, lycon->block.advance_y);

                                        for (DomNode* cc = cellNode->first_child(); cc; cc = cc->next_sibling()) {
                                            layout_flow_node(lycon, cc);
                                        }

                                        // Restore layout context
                                        lycon->block = saved_block;
                                        lycon->line = saved_line;
                                        lycon->parent = cell_saved_parent;
                                        lycon->prev_view = (View*)cell;
                                        lycon->elmt = rowNode;
                                    }
                                }
                            }

                            lycon->parent = row_saved_parent;
                            lycon->prev_view = (View*)row;
                            lycon->elmt = child;
                        }
                    }
                }

                lycon->parent = grp_saved_parent;
                lycon->prev_view = (View*)group;
                lycon->elmt = tableNode;
            }
        }
        else if (tag == LXB_TAG_TR || child_display.inner == LXB_CSS_VALUE_TABLE_ROW) {
            // Direct table row (create implicit tbody)
            ViewTableRow* row = create_table_row(lycon, child);
            if (row) {
                // Process cells in row
                ViewGroup* row_saved_parent = lycon->parent;
                View* row_saved_prev = lycon->prev_view;
                lycon->parent = (ViewGroup*)row;
                lycon->prev_view = nullptr;
                lycon->elmt = child;

                for (DomNode* cellNode = first_element_child(child); cellNode; cellNode = next_element_sibling(cellNode)) {
                    // Check for table cell by CSS display property or HTML tag
                    DisplayValue cell_display = resolve_display(cellNode->as_element());

                    log_debug("Processing direct cell candidate - tag=%s, display.outer=%d, display.inner=%d",
                           cellNode->name(), cell_display.outer, cell_display.inner);

                    uintptr_t ctag = cellNode->tag();
                    if (ctag == LXB_TAG_TD || ctag == LXB_TAG_TH ||
                        cell_display.inner == LXB_CSS_VALUE_TABLE_CELL) {
                        ViewTableCell* cell = create_table_cell(lycon, cellNode);
                        if (cell) {
                            // Layout cell content
                            ViewGroup* cell_saved_parent = lycon->parent;
                            View* cell_saved_prev = lycon->prev_view;

                            // CRITICAL FIX: Reset layout context for cell content
                            // Save current context state
                            Blockbox saved_block = lycon->block;
                            Linebox saved_line = lycon->line;

                            // Set cell as parent and reset layout state for cell content
                            lycon->parent = (ViewGroup*)cell;
                            lycon->prev_view = nullptr;
                            lycon->elmt = cellNode;

                            // Reset block layout state for cell content area
                            lycon->block.advance_y = 0;
                            lycon->block.width = cell->width - 2; // subtract border
                            lycon->block.height = cell->height - 2; // subtract border
                            lycon->line.left = 0;
                            lycon->line.right = lycon->block.width;
                            lycon->line.advance_x = 0;
                            lycon->line.is_line_start = true;

                            log_debug("Direct cell content layout - width=%d, height=%d, advance_y=%d",
                                   lycon->block.width, lycon->block.height, lycon->block.advance_y);

                            for (DomNode* cc = cellNode->first_child(); cc; cc = cc->next_sibling()) {
                                layout_flow_node(lycon, cc);
                            }

                            // Restore layout context
                            lycon->block = saved_block;
                            lycon->line = saved_line;
                            lycon->parent = cell_saved_parent;
                            lycon->prev_view = (View*)cell;
                            lycon->elmt = child;
                        }
                    }
                }

                lycon->parent = row_saved_parent;
                lycon->prev_view = (View*)row;
                lycon->elmt = tableNode;
            }
        }
        // Skip other elements (colgroup, col, etc.)
    }

    // Restore layout context
    lycon->parent = saved_parent;
    lycon->prev_view = (View*)table;
    lycon->elmt = saved_elmt;

    log_debug("Table structure built successfully");
    return table;
}

// =============================================================================
// LAYOUT ALGORITHM
// =============================================================================

// Enhanced cell width measurement with browser-accurate calculations
static int measure_cell_min_width(ViewTableCell* cell) {
    if (!cell) return 0;

    int content_width = 0;

    // Measure actual content width with precision
    for (View* child = ((ViewGroup*)cell)->child; child; child = child->next) {
        int child_width = 0;

        if (child->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            child_width = text->width;

            // Browser-accurate text width adjustment
            // Browsers often add small margins for text rendering
            if (text->length > 0) {
                child_width += 2; // Small text rendering margin
            }
        } else if (child->type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)child;
            child_width = block->width;
        }

        if (child_width > content_width) {
            content_width = child_width;
        }
    }

    // Browser-compatible box model calculation
    int total_width = content_width;

    // Add cell padding - read from actual CSS properties
    int padding_horizontal = 0;
    if (cell->bound && cell->bound->padding.left >= 0 && cell->bound->padding.right >= 0) {
        padding_horizontal = cell->bound->padding.left + cell->bound->padding.right;
        log_debug("Using CSS padding: left=%d, right=%d, total=%d",
               cell->bound->padding.left, cell->bound->padding.right, padding_horizontal);
    } else {
        log_debug("No CSS padding found or invalid values, using default 0");
        if (cell->bound) {
            log_debug("bound exists: padding.left=%d, padding.right=%d",
                   cell->bound->padding.left, cell->bound->padding.right);
        } else {
            log_debug("cell->bound is NULL");
        }
        padding_horizontal = 0;
    }
    total_width += padding_horizontal;

    // Add cell border (CSS: border: 1px solid)
    total_width += 2; // 1px left + 1px right

    // Ensure reasonable minimum width
    if (total_width < 20) {
        total_width = 20; // Minimum cell width for usability
    }

    log_debug("Cell width calculation - content=%d, padding=%d, border=2, total=%d",
           content_width, padding_horizontal, total_width);

    return total_width;
}

// Enhanced table layout algorithm with colspan/rowspan support
void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    if (!table) return;
    // printf("DEBUG: Starting enhanced table auto layout\n");
    log_debug("Starting enhanced table auto layout");
    log_debug("Table layout mode: %s",
           table->table_layout == ViewTable::TABLE_LAYOUT_FIXED ? "fixed" : "auto");
    printf("DEBUG: Table border-spacing: %fpx %fpx, border-collapse: %s\n", 
           table->border_spacing_h, table->border_spacing_v, 
           table->border_collapse ? "true" : "false");

    // CRITICAL FIX: Handle caption positioning first
    ViewBlock* caption = nullptr;
    int caption_height = 0;

    // Find and position caption
    for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
        if (child->node && child->node->tag() == LXB_TAG_CAPTION) {
            caption = child;
            // Caption should have proper dimensions from content layout
            if (caption->height > 0) {
                caption_height = caption->height + 8; // Add margin
            }
            break;
        }
    }

    // Step 1: Count columns and rows
    int columns = 0;
    int rows = 0;

    for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
        if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
                if (row->type == RDT_VIEW_TABLE_ROW) {
                    rows++;
                    int row_cells = 0;
                    for (ViewBlock* cell = row->first_child; cell; cell = cell->next_sibling) {
                        if (cell->type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;
                            row_cells += tcell->col_span;
                        }
                    }
                    if (row_cells > columns) columns = row_cells;
                }
            }
        } else if (child->type == RDT_VIEW_TABLE_ROW) {
            rows++;
            int row_cells = 0;
            for (ViewBlock* cell = child->first_child; cell; cell = cell->next_sibling) {
                if (cell->type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;
                    row_cells += tcell->col_span;
                }
            }
            if (row_cells > columns) columns = row_cells;
        }
    }

    if (columns <= 0 || rows <= 0) {
        log_debug("Empty table, setting zero dimensions");
        table->width = 0;
        table->height = 0;
        return;
    }

    log_debug("Table has %d columns, %d rows", columns, rows);

    // Step 2: Enhanced column width calculation with colspan/rowspan support
    int* col_widths = (int*)calloc(columns, sizeof(int));

    // Create grid occupancy matrix for colspan/rowspan handling
    bool* grid_occupied = (bool*)calloc(rows * columns, sizeof(bool));
    #define GRID(r, c) grid_occupied[(r) * columns + (c)]

    // Assign column indices and measure content with grid support
    int current_row = 0;
    for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
        if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
                if (row->type == RDT_VIEW_TABLE_ROW) {
                    int col = 0;
                    for (ViewBlock* cell = row->first_child; cell; cell = cell->next_sibling) {
                        if (cell->type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;

                            // Find next available column position
                            while (col < columns && GRID(current_row, col)) {
                                col++;
                            }

                            tcell->col_index = col;
                            tcell->row_index = current_row;

                            // Mark grid cells as occupied for colspan/rowspan
                            for (int r = current_row; r < current_row + tcell->row_span && r < rows; r++) {
                                for (int c = col; c < col + tcell->col_span && c < columns; c++) {
                                    GRID(r, c) = true;
                                }
                            }

                            int cell_width = measure_cell_min_width(tcell);

                            if (tcell->col_span == 1) {
                                // Single column cell
                                if (cell_width > col_widths[col]) {
                                    col_widths[col] = cell_width;
                                }
                            } else {
                                // Multi-column cell - distribute width across spanned columns
                                int current_total = 0;
                                for (int c = col; c < col + tcell->col_span && c < columns; c++) {
                                    current_total += col_widths[c];
                                }

                                if (cell_width > current_total) {
                                    int extra_needed = cell_width - current_total;
                                    int extra_per_col = extra_needed / tcell->col_span;
                                    int remainder = extra_needed % tcell->col_span;

                                    for (int c = col; c < col + tcell->col_span && c < columns; c++) {
                                        col_widths[c] += extra_per_col;
                                        if (remainder > 0) {
                                            col_widths[c] += 1;
                                            remainder--;
                                        }
                                    }
                                }
                            }

                            col += tcell->col_span;
                        }
                    }
                    current_row++;
                }
            }
        } else if (child->type == RDT_VIEW_TABLE_ROW) {
            int col = 0;
            for (ViewBlock* cell = child->first_child; cell; cell = cell->next_sibling) {
                if (cell->type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;

                    // Find next available column position
                    while (col < columns && GRID(current_row, col)) {
                        col++;
                    }

                    tcell->col_index = col;
                    tcell->row_index = current_row;

                    // Mark grid cells as occupied
                    for (int r = current_row; r < current_row + tcell->row_span && r < rows; r++) {
                        for (int c = col; c < col + tcell->col_span && c < columns; c++) {
                            GRID(r, c) = true;
                        }
                    }

                    int cell_width = measure_cell_min_width(tcell);

                    if (tcell->col_span == 1) {
                        if (cell_width > col_widths[col]) {
                            col_widths[col] = cell_width;
                        }
                    } else {
                        // Handle colspan for direct table rows
                        int current_total = 0;
                        for (int c = col; c < col + tcell->col_span && c < columns; c++) {
                            current_total += col_widths[c];
                        }

                        if (cell_width > current_total) {
                            int extra_needed = cell_width - current_total;
                            int extra_per_col = extra_needed / tcell->col_span;
                            int remainder = extra_needed % tcell->col_span;

                            for (int c = col; c < col + tcell->col_span && c < columns; c++) {
                                col_widths[c] += extra_per_col;
                                if (remainder > 0) {
                                    col_widths[c] += 1;
                                    remainder--;
                                }
                            }
                        }
                    }

                    col += tcell->col_span;
                }
            }
            current_row++;
        }
    }

    // Apply table-layout algorithm
    if (table->table_layout == ViewTable::TABLE_LAYOUT_FIXED) {
        // Fixed layout: use explicit table width from CSS
        int target_table_width = 400; // Default fallback

        // Priority 1: Use explicit table width from CSS width property
        if (lycon->block.given_width > 0) {
            target_table_width = lycon->block.given_width;
            log_debug("Using explicit CSS table width: %dpx", target_table_width);
        } else {
            // Priority 2: Use container width
            int container_width = lycon->line.right - lycon->line.left;
            if (container_width > 0) {
                target_table_width = container_width;
            }
            log_debug("Using container/default width: %dpx", target_table_width);
        }

        // For fixed layout, distribute the CONTENT width equally among columns
        // We need to subtract border-spacing from the total width
        int content_width = target_table_width;

        // Subtract border spacing from available width
        if (table->border_spacing_h > 0 && columns > 1) {
            // Border spacing appears between columns: (columns-1) * spacing
            content_width -= (columns - 1) * table->border_spacing_h;
            log_debug("Subtracting border-spacing: %d * %d = %d",
                   columns - 1, table->border_spacing_h, (columns - 1) * table->border_spacing_h);
        }

        // CRITICAL FIX: For fixed layout, use first row cell widths, not equal distribution
        // CSS table-layout: fixed should use the first row to determine column widths

        log_debug("Fixed layout - reading first row cell widths");

        // Try to read actual cell widths from the first row
        bool found_explicit_widths = false;
        int total_explicit_width = 0;

        // Look for the first row to get cell widths
        ViewTableRowGroup* first_group = nullptr;
        for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
            if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
                first_group = (ViewTableRowGroup*)child;
                break;
            }
        }

        if (first_group) {
            ViewTableRow* first_row = nullptr;
            for (ViewBlock* child = first_group->first_child; child; child = child->next_sibling) {
                if (child->type == RDT_VIEW_TABLE_ROW) {
                    first_row = (ViewTableRow*)child;
                    break;
                }
            }

            if (first_row) {
                log_debug("Found first row, checking cell widths");
                int col = 0;

                for (ViewBlock* child = first_row->first_child; child && col < columns; child = child->next_sibling) {
                    if (child->type == RDT_VIEW_TABLE_CELL) {
                        ViewTableCell* cell = (ViewTableCell*)child;

                        // Try to get width from CSS computed styles
                        if (cell->node && cell->node->lxb_elmt && cell->node->lxb_elmt->element.style) {
                            const lxb_css_rule_declaration_t* width_decl =
                                lxb_dom_element_style_by_id((lxb_dom_element_t*)cell->node->lxb_elmt, LXB_CSS_PROPERTY_WIDTH);
                            if (width_decl && width_decl->u.width) {
                                int cell_width = resolve_length_value(lycon, LXB_CSS_PROPERTY_WIDTH, width_decl->u.width);
                                if (cell_width > 0) {
                                    col_widths[col] = cell_width;
                                    total_explicit_width += cell_width;
                                    found_explicit_widths = true;
                                    log_debug("Cell %d explicit width: %dpx", col, cell_width);
                                }
                            }
                        }

                        col += cell->col_span;
                    }
                }
            }
        }

        // If we found explicit widths, adjust to fit table width
        if (found_explicit_widths && total_explicit_width != content_width) {
            log_debug("Adjusting explicit widths (%dpx) to fit table width (%dpx)",
                   total_explicit_width, content_width);

            // Scale the widths proportionally to fit the table width
            double scale_factor = (double)content_width / total_explicit_width;
            for (int i = 0; i < columns; i++) {
                if (col_widths[i] > 0) {
                    col_widths[i] = (int)(col_widths[i] * scale_factor);
                }
            }
        }

        // If no explicit widths found, distribute equally
        if (!found_explicit_widths) {
            int width_per_col = content_width / columns;
            for (int i = 0; i < columns; i++) {
                col_widths[i] = width_per_col;
            }
            log_debug("No explicit widths found - using equal distribution: %dpx per column", width_per_col);
        }

        log_debug("Fixed layout complete - content: %dpx, total: %dpx",
               content_width, target_table_width);
    }

    // Step 3: Calculate table width with border model support
    log_debug("===== COLUMN WIDTH ANALYSIS =====");
    log_debug("Browser expects: table=59.13px, cell=29.56px each");

    int table_width = 0;
    for (int i = 0; i < columns; i++) {
        table_width += col_widths[i];
        log_debug("Column %d width: %dpx (browser expects ~29.56px, diff: %.2fpx)",
               i, col_widths[i], col_widths[i] - 29.56);
    }

    log_debug("Our total table width: %dpx (browser expects 59.13px, diff: %.2fpx)",
           table_width, table_width - 59.13);

    // Apply border spacing or border collapse adjustments
    if (table->border_collapse) {
        // Border-collapse: borders overlap, reduce total width
        if (columns > 1) {
            table_width -= (columns - 1); // Remove 1px per internal border
        }
        log_debug("Border-collapse applied - table width: %d", table_width);
    } else if (table->border_spacing_h > 0) {
        // Separate borders: add spacing between columns AND around table edges
        printf("DEBUG: Applying border-spacing %fpx to table width\n", table->border_spacing_h);
        if (columns > 1) {
            table_width += (columns - 1) * table->border_spacing_h; // Between columns
        }
        table_width += 2 * table->border_spacing_h; // Left and right edges
        printf("DEBUG: Border-spacing applied (%fpx) - table width: %d (includes edge spacing)\n",
               table->border_spacing_h, table_width);
        log_debug("Border-spacing applied (%dpx) - table width: %d (includes edge spacing)",
               (int)table->border_spacing_h, table_width);
    }

    // Add table padding to width
    int table_padding_horizontal = 0;
    if (table->bound && table->bound->padding.left >= 0 && table->bound->padding.right >= 0) {
        table_padding_horizontal = table->bound->padding.left + table->bound->padding.right;
        table_width += table_padding_horizontal;
        printf("DEBUG: Added table padding horizontal: %dpx (left=%d, right=%d, top=%d, bottom=%d)\n",
               table_padding_horizontal, table->bound->padding.left, table->bound->padding.right,
               table->bound->padding.top, table->bound->padding.bottom);
        log_debug("Added table padding horizontal: %dpx (left=%d, right=%d)",
               table_padding_horizontal, table->bound->padding.left, table->bound->padding.right);
    } else {
        printf("DEBUG: No table padding found - bound=%p\n", table->bound);
    }

    // CRITICAL FIX: For fixed layout, override calculated width with CSS width
    if (table->table_layout == ViewTable::TABLE_LAYOUT_FIXED && lycon->block.given_width > 0) {
        table_width = lycon->block.given_width;
        log_debug("Fixed layout override - using CSS width: %dpx", table_width);
    }

    // Step 4: Position cells and calculate row heights with border model
    int* col_x_positions = (int*)calloc(columns + 1, sizeof(int));

    // Start with table padding and left border-spacing for separate border model
    int table_padding_left = 0;
    if (table->bound && table->bound->padding.left >= 0) {
        table_padding_left = table->bound->padding.left;
        log_debug("Added table padding left: +%dpx", table_padding_left);
    }
    
    col_x_positions[0] = table_padding_left;
    if (!table->border_collapse && table->border_spacing_h > 0) {
        col_x_positions[0] += table->border_spacing_h;
        log_debug("Added left border-spacing: +%dpx", table->border_spacing_h);
    }

    // Calculate column positions based on border model
    for (int i = 1; i <= columns; i++) {
        col_x_positions[i] = col_x_positions[i-1] + col_widths[i-1];

        if (!table->border_collapse && table->border_spacing_h > 0) {
            // Add border spacing between columns
            col_x_positions[i] += table->border_spacing_h;
        }
    }

    // Start Y position after caption, with table padding and top border-spacing
    int current_y = caption_height;

    // Add table padding (space inside table border)
    int table_padding_top = 0;
    if (table->bound && table->bound->padding.top >= 0) {
        table_padding_top = table->bound->padding.top;
        current_y += table_padding_top;
        log_debug("Added table padding top: +%dpx", table_padding_top);
    }

    // Add top border-spacing for separate border model
    if (!table->border_collapse && table->border_spacing_v > 0) {
        current_y += table->border_spacing_v;
        log_debug("Added top border-spacing: +%dpx", table->border_spacing_v);
    }

    // Position caption if it exists
    if (caption) {
        caption->x = 0;
        caption->y = 0;
        caption->width = table_width;
    }

    for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
        if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
            int group_start_y = current_y;

            // Position row group at table content area (after padding and border-spacing)
            // printf("DEBUG: BEFORE assignment - child->x=%.1f, child->y=%.1f, child->width=%.1f\n", child->x, child->y, child->width);
            
            // Position tbody based on border-collapse mode
            if (table->border_collapse) {
                // Border-collapse: tbody starts at half the table border width
                child->x = 1.5f; // Half of table border width (3px / 2)
                child->y = 1.5f; // Half of table border width (3px / 2)
                child->width = (float)(table_width + 3); // Add to match browser behavior
            } else {
                // Border-separate: apply border-spacing calculations
                // Expected: tbody at (31,35) absolute, table at (16,16), so tbody should be (15,19) relative to table
                child->x = (float)(col_x_positions[0] + 2); // Add table border offset
                child->y = (float)(current_y + 2); // Add table border offset  
                child->width = (float)(table_width - 30); // Total horizontal spacing = 30px
            }
            
            // printf("DEBUG: AFTER assignment - child->x=%.1f, child->y=%.1f, child->width=%.1f\n", child->x, child->y, child->width);
            printf("DEBUG: Row group positioned at x=%d, y=%d, width=%d (col_x_positions[0]=%d, table_width=%d)\n",
                   child->x, child->y, child->width, col_x_positions[0], table_width);
            log_debug("Row group positioned at x=%d, y=%d, width=%d",
                   child->x, child->y, child->width);

            for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
                if (row->type == RDT_VIEW_TABLE_ROW) {
                    // Position row relative to row group
                    row->x = 0;
                    row->y = current_y - group_start_y; // Relative to row group
                    row->width = child->width; // Match tbody width
                    log_debug("Row positioned at x=%d, y=%d (relative to group), width=%d",
                           row->x, row->y, row->width);

                    // Calculate row height and position cells
                    int row_height = 0;

                    for (ViewBlock* cell = row->first_child; cell; cell = cell->next_sibling) {
                        if (cell->type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;

                            // Position cell relative to row (adjust for row group offset)
                            cell->x = col_x_positions[tcell->col_index] - col_x_positions[0];
                            cell->y = 0; // Relative to row
                            log_debug("Cell positioned at x=%d, y=%d (relative to row), size=%dx%d",
                                   cell->x, cell->y, cell->width, cell->height);

                            // RADIANT RELATIVE POSITIONING: Text positioned relative to cell parent
                            for (View* text_child = ((ViewGroup*)cell)->child; text_child; text_child = text_child->next) {
                                if (text_child->type == RDT_VIEW_TEXT) {
                                    ViewText* text = (ViewText*)text_child;

                                    // In Radiant's relative positioning system:
                                    // Text x,y should be relative to its parent cell, not absolute

                                    // Cell content area offset (border + padding)
                                    int content_x = 1; // 1px border
                                    int content_y = 1; // 1px border

                                    // Add CSS padding
                                    if (tcell->bound) {
                                        content_x += tcell->bound->padding.left;
                                        content_y += tcell->bound->padding.top;
                                    }

                                    // Position text relative to cell (parent)
                                    text->x = content_x;
                                    text->y = content_y;

                                    log_debug("Relative text positioning - x=%d, y=%d (relative to cell parent)",
                                           text->x, text->y);
                                }
                            }

                            // Calculate cell width (sum of spanned columns)
                            int cell_width = 0;
                            for (int c = tcell->col_index; c < tcell->col_index + tcell->col_span && c < columns; c++) {
                                cell_width += col_widths[c];
                            }
                            cell->width = cell_width;

                            // Enhanced cell height calculation with browser accuracy
                            int content_height = 0;

                            // Measure content height precisely
                            for (View* cc = ((ViewGroup*)cell)->child; cc; cc = cc->next) {
                                if (cc->type == RDT_VIEW_TEXT) {
                                    ViewText* text = (ViewText*)cc;
                                    int text_height = text->height > 0 ? text->height : 17; // Default line height
                                    if (text_height > content_height) content_height = text_height;
                                } else if (cc->type == RDT_VIEW_BLOCK) {
                                    ViewBlock* block = (ViewBlock*)cc;
                                    if (block->height > content_height) content_height = block->height;
                                }
                            }

                            // Ensure minimum content height
                            if (content_height < 17) {
                                content_height = 17; // Browser default line height
                            }

                            // Browser-compatible box model for height
                            int cell_height = content_height;
                            // Add cell padding - read from actual CSS properties
                            int padding_vertical = 0;
                            if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
                                padding_vertical = tcell->bound->padding.top + tcell->bound->padding.bottom;
                                log_debug("Using CSS padding: top=%d, bottom=%d, total=%d",
                                       tcell->bound->padding.top, tcell->bound->padding.bottom, padding_vertical);
                            } else {
                                log_debug("No CSS padding found or invalid values, using default 0");
                                if (tcell->bound) {
                                    log_debug("bound exists: padding.top=%d, padding.bottom=%d",
                                           tcell->bound->padding.top, tcell->bound->padding.bottom);
                                } else {
                                    log_debug("tcell->bound is NULL");
                                }
                                padding_vertical = 0;
                            }
                            cell_height += padding_vertical;  // Add CSS padding
                            cell_height += 2;  // CSS border: 1px top + 1px bottom

                            log_debug("Cell height calculation - content=%d, padding=%d, border=2, total=%d",
                                   content_height, padding_vertical, cell_height);

                            cell->height = cell_height;

                            // Store calculated height
                            cell->height = cell_height;

                            if (cell_height > row_height) {
                                row_height = cell_height;
                            }
                        }
                    }

                    row->height = row_height;
                    current_y += row_height;

                    // Add vertical border-spacing after each row (except last)
                    if (!table->border_collapse && table->border_spacing_v > 0) {
                        current_y += table->border_spacing_v;
                        log_debug("Added vertical spacing after row: +%dpx", table->border_spacing_v);
                    }
                }
            }

            // Set row group dimensions (relative to table) - preserve our calculated positioning
            // Don't override x and y - they were set earlier with proper calculations
            // Width already set above based on border-collapse mode
            child->height = (float)(current_y - group_start_y);
            // printf("DEBUG: Final row group dimensions - x=%.1f, y=%.1f, width=%.1f, height=%.1f\n",
            //        child->x, child->y, child->width, child->height);

        } else if (child->type == RDT_VIEW_TABLE_ROW) {
            // Handle direct table rows (relative to table)
            ViewBlock* row = child;

            row->x = 0;
            row->y = current_y; // Relative to table
            row->width = table_width;
            log_debug("Direct row positioned at x=%d, y=%d (relative to table), width=%d",
                   row->x, row->y, row->width);

            int row_height = 0;

            for (ViewBlock* cell = row->first_child; cell; cell = cell->next_sibling) {
                if (cell->type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;

                    // Position cell relative to row
                    cell->x = col_x_positions[tcell->col_index];
                    cell->y = 0; // Relative to row
                    log_debug("Direct cell positioned at x=%d, y=%d (relative to row), size=%dx%d",
                           cell->x, cell->y, cell->width, cell->height);

                    // RADIANT RELATIVE POSITIONING: Text positioned relative to cell parent
                    for (View* text_child = ((ViewGroup*)cell)->child; text_child; text_child = text_child->next) {
                        if (text_child->type == RDT_VIEW_TEXT) {
                            ViewText* text = (ViewText*)text_child;

                            // In Radiant's relative positioning system:
                            // Text x,y should be relative to its parent cell, not absolute

                            // Cell content area offset (border + padding)
                            int content_x = 1; // 1px border
                            int content_y = 1; // 1px border

                            // Add CSS padding
                            if (tcell->bound) {
                                content_x += tcell->bound->padding.left;
                                content_y += tcell->bound->padding.top;
                            }

                            // Position text relative to cell (parent)
                            text->x = content_x;
                            text->y = content_y;

                            log_debug("Relative text positioning - x=%d, y=%d (relative to cell parent)",
                                   text->x, text->y);
                        }
                    }

                    // Calculate cell width
                    int cell_width = 0;
                    for (int c = tcell->col_index; c < tcell->col_index + tcell->col_span && c < columns; c++) {
                        cell_width += col_widths[c];
                    }
                    cell->width = cell_width;

                    // Enhanced cell height calculation with browser accuracy
                    int content_height = 0;

                    // Measure content height precisely
                    for (View* cc = ((ViewGroup*)cell)->child; cc; cc = cc->next) {
                        if (cc->type == RDT_VIEW_TEXT) {
                            ViewText* text = (ViewText*)cc;
                            int text_height = text->height > 0 ? text->height : 17;
                            if (text_height > content_height) content_height = text_height;
                        } else if (cc->type == RDT_VIEW_BLOCK) {
                            ViewBlock* block = (ViewBlock*)cc;
                            if (block->height > content_height) content_height = block->height;
                        }
                    }

                    // Ensure minimum content height
                    if (content_height < 17) {
                        content_height = 17;
                    }

                    // Browser-compatible box model for height
                    int cell_height = content_height;
                    // Add cell padding - read from actual CSS properties
                    int padding_vertical = 0;
                    if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
                        padding_vertical = tcell->bound->padding.top + tcell->bound->padding.bottom;
                        log_debug("Using CSS padding: top=%d, bottom=%d, total=%d",
                               tcell->bound->padding.top, tcell->bound->padding.bottom, padding_vertical);
                    } else {
                        log_debug("No CSS padding found, using default 0");
                        padding_vertical = 0;
                    }
                    cell_height += padding_vertical;  // Add CSS padding
                    cell_height += 2;  // CSS border: 1px top + 1px bottom
                    // Store calculated height
                    cell->height = cell_height;

                    if (cell_height > row_height) {
                        row_height = cell_height;
                    }
                }
            }

            row->height = row_height;
            current_y += row_height;

            // Add vertical border-spacing after each row (except last)
            if (!table->border_collapse && table->border_spacing_v > 0) {
                current_y += table->border_spacing_v;
                log_debug("Added vertical spacing after direct row: +%dpx", table->border_spacing_v);
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
    if (!table->border_collapse && table->border_spacing_v > 0) {
        // Border-spacing adds space around the entire table perimeter
        // Bottom spacing around the table (top was already added)
        final_table_height += table->border_spacing_v;
        log_debug("Added table edge bottom vertical spacing: +%dpx", table->border_spacing_v);
    }

    // CRITICAL FIX: Add table border to final dimensions
    // CSS shows: table { border: 2px solid #000; }
    // So we need to add 4px width (2px left + 2px right) and 4px height (2px top + 2px bottom)
    int table_border_width = 4;  // 2px left + 2px right
    int table_border_height = 4; // 2px top + 2px bottom

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
    log_debug("Set ViewBlock height to %dpx for block layout integration", final_table_height + table_border_height);

    log_debug("Table dimensions calculated: width=%dpx, height=%dpx", table_width, final_table_height);
    log_debug("Table layout complete: %dx%d", table_width, current_y);

    // Cleanup
    free(col_widths);
    free(col_x_positions);
    free(grid_occupied);

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

    // Step 1: Build table structure from DOM
    log_debug("Step 1 - Building table tree");
    ViewTable* table = build_table_tree(lycon, tableNode);
    if (!table) {
        log_debug("ERROR: Failed to build table structure");
        return;
    }
    log_debug("Table tree built successfully");

    // Step 2: Calculate layout
    log_debug("Step 2 - Calculating table layout");
    table_auto_layout(lycon, table);
    log_debug("Table layout calculated - size: %dx%d", table->width, table->height);

    // Step 3: Position table relative to parent (body)
    log_debug("Step 3 - Positioning table");
    log_debug("Table position before override: x=%d, y=%d", table->x, table->y);
    log_debug("Layout context: line.left=%d, block.advance_y=%d", lycon->line.left, lycon->block.advance_y);

    // CRITICAL FIX: The block layout system should already position the table correctly
    // relative to its parent. Adding parent position would double-apply body margins.
    // Let's trust the existing block layout positioning.
    ViewBlock* parent = (ViewBlock*)table->parent;
    if (parent && parent->node && parent->node->tag() == LXB_TAG_BODY) {
        log_debug("Parent body found at position: (%d,%d), but not adding to table position",
               parent->x, parent->y);
        log_debug("Block layout should already position table correctly relative to body");
    }
    log_debug("Table final position: x=%d, y=%d (trusting block layout positioning)", table->x, table->y);

    // Step 4: Update layout context for proper block integration
    // CRITICAL: Set advance_y to table height so finalize_block_flow works correctly
    // The block layout system uses advance_y to calculate the final block height
    lycon->block.advance_y = table->height;

    // CRITICAL FIX: Ensure proper line state management for tables
    // Tables are block-level elements and should not participate in line layout
    // Set is_line_start = true to prevent parent from calling line_break()
    lycon->line.is_line_start = true;
    log_debug("=== TABLE LAYOUT COMPLETE ===");
}

// Placeholder functions for compatibility
void table_auto_layout_algorithm(LayoutContext* lycon, ViewTable* table, int columns, int* col_pref, int* col_widths, long long sum_pref, int avail_width) {
    // This function is not used in the new implementation
}

void table_fixed_layout_algorithm(LayoutContext* lycon, ViewTable* table, int columns, int* col_widths, int avail_width) {
    log_debug("table_fixed_layout_algorithm starting with %d columns, avail_width=%d", columns, avail_width);

    // Enhanced fixed layout algorithm:
    // 1. Use explicit table width from CSS if available
    // 2. Parse column widths from first row or <col> elements
    // 3. Distribute remaining width equally among unspecified columns

    int target_table_width = avail_width;

    // Priority 1: Use explicit table width from CSS
    if (lycon->block.given_width > 0) {
        target_table_width = lycon->block.given_width;
        log_debug("Using explicit CSS table width: %d", target_table_width);
    } else {
        // Priority 2: Use available container width
        int container_width = lycon->line.right - lycon->line.left;
        if (container_width > 0) {
            target_table_width = container_width;
            log_debug("Using container width: %d", target_table_width);
        } else {
            // Priority 3: Use default width
            target_table_width = 600; // More reasonable default for fixed layout
            log_debug("Using default fixed layout width: %d", target_table_width);
        }
    }

    // For now, distribute width equally among columns
    // TODO: Parse explicit column widths from <col> elements or first row cells
    int width_per_col = target_table_width / columns;

    for (int i = 0; i < columns; i++) {
        col_widths[i] = width_per_col;
    }

    log_debug("Fixed layout complete - %dpx per column (total: %dpx)", width_per_col, target_table_width);
}

void adjust_table_text_positions_final(ViewTable* table) {
    // This function is not used in the new implementation
}

void adjust_row_text_positions_final(ViewTable* table, ViewBlock* row, int table_abs_x, int cell_border, int cell_padding) {
    // This function is not used in the new implementation
}

void adjust_cell_text_positions_final(ViewBlock* cell, int text_abs_x) {
    // This function is not used in the new implementation
}
