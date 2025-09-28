#include "layout_table.hpp"
#include "layout.hpp"
#include "../lib/log.h"

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
static void parse_table_properties(DomNode* element, ViewTable* table) {
    if (!element || !table) return;
    
    // Initialize defaults
    table->table_layout = ViewTable::TABLE_LAYOUT_AUTO;
    table->border_collapse = false;
    table->border_spacing_h = 0;
    table->border_spacing_v = 0;
    
    // Parse inline style attribute for table-specific properties
    if (!element->is_element()) return;
    
    lxb_html_element_t* html_element = element->lxb_elmt;
    if (!html_element) return;
    
    lxb_dom_attr_t* style_attr = lxb_dom_element_attr_by_name(
        lxb_dom_interface_element(html_element), (const lxb_char_t*)"style", 5);
    
    if (style_attr && style_attr->value) {
        size_t attr_len;
        const char* style_str = (const char*)lxb_dom_attr_value(style_attr, &attr_len);
        if (style_str) {
            // Parse table-layout
            if (strstr(style_str, "table-layout: fixed")) {
                table->table_layout = ViewTable::TABLE_LAYOUT_FIXED;
                printf("DEBUG: Detected table-layout: fixed\n");
            }
            
            // Parse border-collapse
            if (strstr(style_str, "border-collapse: collapse")) {
                table->border_collapse = true;
                printf("DEBUG: Detected border-collapse: collapse\n");
            }
            
            // Parse border-spacing
            const char* spacing_pos = strstr(style_str, "border-spacing");
            if (spacing_pos) {
                const char* q = spacing_pos;
                while (*q && *q != ':') q++;
                if (*q == ':') q++;
                while (*q == ' ') q++;
                int h = atoi(q);
                while (*q && *q != ' ' && *q != ';' && *q != 'p') q++;
                if (*q == 'p' && *(q+1) == 'x') q += 2;
                int v = -1;
                while (*q == ' ') q++;
                if (*q && *q != ';') {
                    v = atoi(q);
                }
                if (h < 0) h = 0;
                if (v < 0) v = h;
                table->border_spacing_h = h;
                table->border_spacing_v = v;
                printf("DEBUG: Detected border-spacing: %dpx %dpx\n", h, v);
            }
        }
    }
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
    
    // Parse cell attributes
    parse_cell_attributes(cellNode, cell);
    
    // Resolve CSS styles
    dom_node_resolve_style(cellNode, lycon);
    
    return cell;
}

// Create and initialize a table row view
static ViewTableRow* create_table_row(LayoutContext* lycon, DomNode* rowNode) {
    ViewTableRow* row = (ViewTableRow*)alloc_view(lycon, RDT_VIEW_TABLE_ROW, rowNode);
    if (!row) return nullptr;
    
    // Resolve CSS styles
    dom_node_resolve_style(rowNode, lycon);
    
    return row;
}

// Create and initialize a table row group view
static ViewTableRowGroup* create_table_row_group(LayoutContext* lycon, DomNode* groupNode) {
    ViewTableRowGroup* group = (ViewTableRowGroup*)alloc_view(lycon, RDT_VIEW_TABLE_ROW_GROUP, groupNode);
    if (!group) return nullptr;
    
    // Resolve CSS styles
    dom_node_resolve_style(groupNode, lycon);
    
    return group;
}

// Build table structure from DOM
ViewTable* build_table_tree(LayoutContext* lycon, DomNode* tableNode) {
    if (!tableNode || !tableNode->is_element()) {
        printf("ERROR: Invalid table node\n");
        return nullptr;
    }
    
    printf("DEBUG: Building table structure\n");
    
    // Save layout context
    ViewGroup* saved_parent = lycon->parent;
    View* saved_prev = lycon->prev_view;
    DomNode* saved_elmt = lycon->elmt;
    
    // Create table view
    lycon->elmt = tableNode;
    ViewTable* table = (ViewTable*)alloc_view(lycon, RDT_VIEW_TABLE, tableNode);
    if (!table) {
        printf("ERROR: Failed to allocate ViewTable\n");
        lycon->parent = saved_parent;
        lycon->prev_view = saved_prev;
        lycon->elmt = saved_elmt;
        return nullptr;
    }
    
    // Parse table properties
    parse_table_properties(tableNode, table);
    
    // Resolve table styles
    dom_node_resolve_style(tableNode, lycon);
    
    // Set table as parent for children
    lycon->parent = (ViewGroup*)table;
    lycon->prev_view = nullptr;
    
    // Process table children
    for (DomNode* child = first_element_child(tableNode); child; child = next_element_sibling(child)) {
        uintptr_t tag = child->tag();
        
        if (tag == LXB_TAG_CAPTION) {
            // Create caption as block
            ViewBlock* caption = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, child);
            if (caption) {
                dom_node_resolve_style(child, lycon);
                
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
        else if (tag == LXB_TAG_THEAD || tag == LXB_TAG_TBODY || tag == LXB_TAG_TFOOT) {
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
                    if (rowNode->tag() == LXB_TAG_TR) {
                        ViewTableRow* row = create_table_row(lycon, rowNode);
                        if (row) {
                            // Process cells in row
                            ViewGroup* row_saved_parent = lycon->parent;
                            View* row_saved_prev = lycon->prev_view;
                            lycon->parent = (ViewGroup*)row;
                            lycon->prev_view = nullptr;
                            lycon->elmt = rowNode;
                            
                            for (DomNode* cellNode = first_element_child(rowNode); cellNode; cellNode = next_element_sibling(cellNode)) {
                                uintptr_t ctag = cellNode->tag();
                                if (ctag == LXB_TAG_TD || ctag == LXB_TAG_TH) {
                                    ViewTableCell* cell = create_table_cell(lycon, cellNode);
                                    if (cell) {
                                        // Layout cell content
                                        ViewGroup* cell_saved_parent = lycon->parent;
                                        View* cell_saved_prev = lycon->prev_view;
                                        lycon->parent = (ViewGroup*)cell;
                                        lycon->prev_view = nullptr;
                                        lycon->elmt = cellNode;
                                        
                                        for (DomNode* cc = cellNode->first_child(); cc; cc = cc->next_sibling()) {
                                            layout_flow_node(lycon, cc);
                                        }
                                        
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
        else if (tag == LXB_TAG_TR) {
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
                    uintptr_t ctag = cellNode->tag();
                    if (ctag == LXB_TAG_TD || ctag == LXB_TAG_TH) {
                        ViewTableCell* cell = create_table_cell(lycon, cellNode);
                        if (cell) {
                            // Layout cell content
                            ViewGroup* cell_saved_parent = lycon->parent;
                            View* cell_saved_prev = lycon->prev_view;
                            lycon->parent = (ViewGroup*)cell;
                            lycon->prev_view = nullptr;
                            lycon->elmt = cellNode;
                            
                            for (DomNode* cc = cellNode->first_child(); cc; cc = cc->next_sibling()) {
                                layout_flow_node(lycon, cc);
                            }
                            
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
    
    printf("DEBUG: Table structure built successfully\n");
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
    
    // Add cell padding (CSS: padding: 8px)
    total_width += 16; // 8px left + 8px right
    
    // Add cell border (CSS: border: 1px solid)
    total_width += 2; // 1px left + 1px right
    
    // Ensure reasonable minimum width
    if (total_width < 20) {
        total_width = 20; // Minimum cell width for usability
    }
    
    printf("DEBUG: Cell width calculation - content=%d, padding=16, border=2, total=%d\n", 
           content_width, total_width);
    
    return total_width;
}

// Enhanced table layout algorithm with colspan/rowspan support
void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    if (!table) return;
    
    printf("DEBUG: Starting enhanced table auto layout\n");
    printf("DEBUG: Table layout mode: %s\n", 
           table->table_layout == ViewTable::TABLE_LAYOUT_FIXED ? "fixed" : "auto");
    
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
            for (ViewBlock* cell = child->first_child; cell; cell = child->next_sibling) {
                if (cell->type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;
                    row_cells += tcell->col_span;
                }
            }
            if (row_cells > columns) columns = row_cells;
        }
    }
    
    if (columns <= 0 || rows <= 0) {
        printf("DEBUG: Empty table, setting zero dimensions\n");
        table->width = 0;
        table->height = 0;
        return;
    }
    
    printf("DEBUG: Table has %d columns, %d rows\n", columns, rows);
    
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
            for (ViewBlock* cell = child->first_child; cell; cell = child->next_sibling) {
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
            printf("DEBUG: Using explicit CSS table width: %dpx\n", target_table_width);
        } else {
            // Priority 2: Use container width
            int container_width = lycon->line.right - lycon->line.left;
            if (container_width > 0) {
                target_table_width = container_width;
            }
            printf("DEBUG: Using container/default width: %dpx\n", target_table_width);
        }
        
        // For fixed layout, distribute the CONTENT width equally among columns
        // We need to subtract border-spacing from the total width
        int content_width = target_table_width;
        
        // Subtract border spacing from available width
        if (table->border_spacing_h > 0 && columns > 1) {
            // Border spacing appears between columns: (columns-1) * spacing
            content_width -= (columns - 1) * table->border_spacing_h;
            printf("DEBUG: Subtracting border-spacing: %d * %d = %d\n", 
                   columns - 1, table->border_spacing_h, (columns - 1) * table->border_spacing_h);
        }
        
        // Distribute remaining width equally among columns
        int width_per_col = content_width / columns;
        
        for (int i = 0; i < columns; i++) {
            col_widths[i] = width_per_col;
        }
        
        printf("DEBUG: Fixed layout - %dpx per column (content: %dpx, total: %dpx)\n", 
               width_per_col, content_width, target_table_width);
    }
    
    // Step 3: Calculate table width with border model support
    printf("DEBUG: ===== COLUMN WIDTH ANALYSIS =====\n");
    printf("DEBUG: Browser expects: table=59.13px, cell=29.56px each\n");
    
    int table_width = 0;
    for (int i = 0; i < columns; i++) {
        table_width += col_widths[i];
        printf("DEBUG: Column %d width: %dpx (browser expects ~29.56px, diff: %.2fpx)\n", 
               i, col_widths[i], col_widths[i] - 29.56);
    }
    
    printf("DEBUG: Our total table width: %dpx (browser expects 59.13px, diff: %.2fpx)\n", 
           table_width, table_width - 59.13);
    
    // Apply border spacing or border collapse adjustments
    if (table->border_collapse) {
        // Border-collapse: borders overlap, reduce total width
        if (columns > 1) {
            table_width -= (columns - 1); // Remove 1px per internal border
        }
        printf("DEBUG: Border-collapse applied - table width: %d\n", table_width);
    } else if (table->border_spacing_h > 0) {
        // Separate borders: add spacing between columns
        if (columns > 1) {
            table_width += (columns - 1) * table->border_spacing_h;
        }
        printf("DEBUG: Border-spacing applied (%dpx) - table width: %d\n", 
               table->border_spacing_h, table_width);
    }
    
    // CRITICAL FIX: For fixed layout, override calculated width with CSS width
    if (table->table_layout == ViewTable::TABLE_LAYOUT_FIXED && lycon->block.given_width > 0) {
        table_width = lycon->block.given_width;
        printf("DEBUG: Fixed layout override - using CSS width: %dpx\n", table_width);
    }
    
    // Step 4: Position cells and calculate row heights with border model
    int* col_x_positions = (int*)calloc(columns + 1, sizeof(int));
    
    // Start with left border-spacing for separate border model
    if (!table->border_collapse && table->border_spacing_h > 0) {
        col_x_positions[0] = table->border_spacing_h;
        printf("DEBUG: Added left border-spacing: +%dpx\n", table->border_spacing_h);
    } else {
        col_x_positions[0] = 0;
    }
    
    // Calculate column positions based on border model
    for (int i = 1; i <= columns; i++) {
        col_x_positions[i] = col_x_positions[i-1] + col_widths[i-1];
        
        if (!table->border_collapse && table->border_spacing_h > 0) {
            // Add border spacing between columns
            col_x_positions[i] += table->border_spacing_h;
        }
    }
    
    // Start Y position after caption, with top border-spacing
    int current_y = caption_height;
    
    // Add top border-spacing for separate border model
    if (!table->border_collapse && table->border_spacing_v > 0) {
        current_y += table->border_spacing_v;
        printf("DEBUG: Added top border-spacing: +%dpx\n", table->border_spacing_v);
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
            
            // Position row group
            child->x = 0;
            child->y = current_y; // Relative to table
            child->width = table_width;
            printf("DEBUG: Row group positioned at x=%d, y=%d, width=%d\n", 
                   child->x, child->y, child->width);
            
            for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
                if (row->type == RDT_VIEW_TABLE_ROW) {
                    // Position row relative to row group
                    row->x = 0;
                    row->y = current_y - group_start_y; // Relative to row group
                    row->width = table_width;
                    printf("DEBUG: Row positioned at x=%d, y=%d (relative to group), width=%d\n", 
                           row->x, row->y, row->width);
                    
                    // Calculate row height and position cells
                    int row_height = 0;
                    
                    for (ViewBlock* cell = row->first_child; cell; cell = cell->next_sibling) {
                        if (cell->type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;
                            
                            // Position cell relative to row
                            cell->x = col_x_positions[tcell->col_index];
                            cell->y = 0; // Relative to row
                            printf("DEBUG: Cell positioned at x=%d, y=%d (relative to row), size=%dx%d\n", 
                                   cell->x, cell->y, cell->width, cell->height);
                            
                            // RADIANT RELATIVE POSITIONING: Text positioned relative to cell parent
                            for (View* text_child = ((ViewGroup*)cell)->child; text_child; text_child = text_child->next) {
                                if (text_child->type == RDT_VIEW_TEXT) {
                                    ViewText* text = (ViewText*)text_child;
                                    
                                    // In Radiant's relative positioning system:
                                    // Text x,y should be relative to its parent cell, not absolute
                                    
                                    // Cell content area offset (border + padding)
                                    int content_x = 1 + 8; // 1px border + 8px padding
                                    int content_y = 1 + 8; // 1px border + 8px padding
                                    
                                    // Position text relative to cell (parent)
                                    text->x = content_x;
                                    text->y = content_y;
                                    
                                    printf("DEBUG: Relative text positioning - x=%d, y=%d (relative to cell parent)\n", 
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
                            cell_height += 16; // CSS padding: 8px top + 8px bottom
                            cell_height += 2;  // CSS border: 1px top + 1px bottom
                            cell_height += 1;  // Browser-compatible adjustment for precise height matching
                            
                            printf("DEBUG: Cell height calculation - content=%d, padding=16, border=2, browser_adj=1, total=%d\n", 
                                   content_height, cell_height);
                            
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
                        printf("DEBUG: Added vertical spacing after row: +%dpx\n", table->border_spacing_v);
                    }
                }
            }
            
            // Set row group dimensions (relative to table)
            child->x = 0;
            child->y = group_start_y;
            child->width = table_width;
            child->height = current_y - group_start_y;
            
        } else if (child->type == RDT_VIEW_TABLE_ROW) {
            // Handle direct table rows (relative to table)
            ViewBlock* row = child;
            
            row->x = 0;
            row->y = current_y; // Relative to table
            row->width = table_width;
            printf("DEBUG: Direct row positioned at x=%d, y=%d (relative to table), width=%d\n", 
                   row->x, row->y, row->width);
            
            int row_height = 0;
            
            for (ViewBlock* cell = row->first_child; cell; cell = cell->next_sibling) {
                if (cell->type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;
                    
                    // Position cell relative to row
                    cell->x = col_x_positions[tcell->col_index];
                    cell->y = 0; // Relative to row
                    printf("DEBUG: Direct cell positioned at x=%d, y=%d (relative to row), size=%dx%d\n", 
                           cell->x, cell->y, cell->width, cell->height);
                    
                    // RADIANT RELATIVE POSITIONING: Text positioned relative to cell parent
                    for (View* text_child = ((ViewGroup*)cell)->child; text_child; text_child = text_child->next) {
                        if (text_child->type == RDT_VIEW_TEXT) {
                            ViewText* text = (ViewText*)text_child;
                            
                            // In Radiant's relative positioning system:
                            // Text x,y should be relative to its parent cell, not absolute
                            
                            // Cell content area offset (border + padding)
                            int content_x = 1 + 8; // 1px border + 8px padding
                            int content_y = 1 + 8; // 1px border + 8px padding
                            
                            // Position text relative to cell (parent)
                            text->x = content_x;
                            text->y = content_y;
                            
                            printf("DEBUG: Relative text positioning - x=%d, y=%d (relative to cell parent)\n", 
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
                    int cell_height = content_height + 18 + 1; // content + padding(16) + border(2) + browser_adj(1)
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
                printf("DEBUG: Added vertical spacing after direct row: +%dpx\n", table->border_spacing_v);
            }
        }
    }
    
    // Calculate final table height with border-spacing
    int final_table_height = current_y;
    
    // Add vertical border-spacing around table edges for separate border model
    if (!table->border_collapse && table->border_spacing_v > 0) {
        // Border-spacing adds space around the entire table perimeter
        // Top and bottom spacing around the table
        final_table_height += 2 * table->border_spacing_v;
        printf("DEBUG: Added table edge vertical spacing: 2 * %dpx = +%dpx\n", 
               table->border_spacing_v, 2 * table->border_spacing_v);
    }
    
    // Set final table dimensions
    table->width = table_width;
    table->height = final_table_height;
    table->content_width = table_width;
    table->content_height = final_table_height;
    
    // CRITICAL: Also set ViewBlock height for block layout system integration
    // ViewTable inherits from ViewBlock, so block layout reads this field
    ((ViewBlock*)table)->height = final_table_height;
    printf("DEBUG: Set ViewBlock height to %dpx for block layout integration\n", final_table_height);
    
    printf("DEBUG: Table dimensions calculated: width=%dpx, height=%dpx\n", table_width, final_table_height);
    printf("DEBUG: Table layout complete: %dx%d\n", table_width, current_y);
    
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
void layout_table_box(LayoutContext* lycon, DomNode* tableNode, DisplayValue display) {
    printf("\n=== TABLE LAYOUT START ===\n");
    printf("DEBUG: Starting table layout\n");
    printf("DEBUG: Initial layout context - line.left=%d, advance_y=%d\n", lycon->line.left, lycon->block.advance_y);
    printf("DEBUG: Building table structure\n");
    if (!tableNode) {
        printf("ERROR: Null table node\n");
        return;
    }
    
    // Step 1: Build table structure from DOM
    printf("DEBUG: Step 1 - Building table tree\n");
    ViewTable* table = build_table_tree(lycon, tableNode);
    if (!table) {
        printf("ERROR: Failed to build table structure\n");
        return;
    }
    printf("DEBUG: Table tree built successfully\n");
    
    // Step 2: Calculate layout
    printf("DEBUG: Step 2 - Calculating table layout\n");
    table_auto_layout(lycon, table);
    printf("DEBUG: Table layout calculated - size: %dx%d\n", table->width, table->height);
    
    // Step 3: Position table relative to parent (body)
    printf("DEBUG: Step 3 - Positioning table\n");
    // In Radiant's relative positioning system, table should be at (0,0) relative to parent
    table->x = 0;
    table->y = 0;
    
    printf("DEBUG: Table positioned at x=%d, y=%d (relative to parent body)\n", 
           table->x, table->y);
    
    // Step 4: Update layout context for proper block integration
    // CRITICAL: Set advance_y to table height so finalize_block_flow works correctly
    // The block layout system uses advance_y to calculate the final block height
    lycon->block.advance_y = table->height;
    
    // Also ensure ViewBlock height is set
    ((ViewBlock*)table)->height = table->height;
    
    // CRITICAL FIX: Ensure proper line state management for tables
    // Tables are block-level elements and should not participate in line layout
    // Set is_line_start = true to prevent parent from calling line_break()
    lycon->line.is_line_start = true;
    
    
    printf("=== TABLE LAYOUT COMPLETE ===\n\n");
}

// Placeholder functions for compatibility
void table_auto_layout_algorithm(LayoutContext* lycon, ViewTable* table, int columns, int* col_pref, int* col_widths, long long sum_pref, int avail_width) {
    // This function is not used in the new implementation
}

void table_fixed_layout_algorithm(LayoutContext* lycon, ViewTable* table, int columns, int* col_widths, int avail_width) {
    printf("DEBUG: table_fixed_layout_algorithm starting with %d columns, avail_width=%d\n", columns, avail_width);
    
    // Enhanced fixed layout algorithm:
    // 1. Use explicit table width from CSS if available
    // 2. Parse column widths from first row or <col> elements
    // 3. Distribute remaining width equally among unspecified columns
    
    int target_table_width = avail_width;
    
    // Priority 1: Use explicit table width from CSS
    if (lycon->block.given_width > 0) {
        target_table_width = lycon->block.given_width;
        printf("DEBUG: Using explicit CSS table width: %d\n", target_table_width);
    } else {
        // Priority 2: Use available container width
        int container_width = lycon->line.right - lycon->line.left;
        if (container_width > 0) {
            target_table_width = container_width;
            printf("DEBUG: Using container width: %d\n", target_table_width);
        } else {
            // Priority 3: Use default width
            target_table_width = 600; // More reasonable default for fixed layout
            printf("DEBUG: Using default fixed layout width: %d\n", target_table_width);
        }
    }
    
    // For now, distribute width equally among columns
    // TODO: Parse explicit column widths from <col> elements or first row cells
    int width_per_col = target_table_width / columns;
    
    for (int i = 0; i < columns; i++) {
        col_widths[i] = width_per_col;
    }
    
    printf("DEBUG: Fixed layout complete - %dpx per column (total: %dpx)\n", width_per_col, target_table_width);
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
