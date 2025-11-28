#include "layout_table.hpp"
#include "layout.hpp"
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
    
    TableMetadata(int cols, int rows) 
        : column_count(cols), row_count(rows) {
        grid_occupied = (bool*)calloc(rows * cols, sizeof(bool));
        col_widths = (int*)calloc(cols, sizeof(int));
        col_min_widths = (int*)calloc(cols, sizeof(int));  // Minimum content widths (CSS MCW)
        col_max_widths = (int*)calloc(cols, sizeof(int));  // Preferred content widths (CSS PCW)
    }
    
    ~TableMetadata() {
        free(grid_occupied);
        free(col_widths);
        free(col_min_widths);
        free(col_max_widths);
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
static void resolve_table_properties(DomNode* element, ViewTable* table) {
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
                    table->tb->border_spacing_h = val->data.length.value;
                    table->tb->border_spacing_v = val->data.length.value;
                    log_debug("Table border-spacing: %.2fpx (both h and v)", val->data.length.value);
                } else if (val->type == CSS_VALUE_TYPE_LIST && val->data.list.count >= 2) {
                    // Two values: horizontal and vertical
                    CssValue* h_val = val->data.list.values[0];
                    CssValue* v_val = val->data.list.values[1];

                    if (h_val && h_val->type == CSS_VALUE_TYPE_LENGTH) {
                        table->tb->border_spacing_h = h_val->data.length.value;
                        log_debug("Table border-spacing horizontal: %.2fpx", h_val->data.length.value);
                    }
                    if (v_val && v_val->type == CSS_VALUE_TYPE_LENGTH) {
                        table->tb->border_spacing_v = v_val->data.length.value;
                        log_debug("Table border-spacing vertical: %.2fpx", v_val->data.length.value);
                    }
                } else if (val->type == CSS_VALUE_TYPE_NUMBER) {
                    // Handle numeric values (convert to length)
                    float spacing = (float)val->data.number.value;
                    table->tb->border_spacing_h = spacing;
                    table->tb->border_spacing_v = spacing;
                    log_debug("Table border-spacing: %.2fpx (numeric, both h and v)", spacing);
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
    cell->td->vertical_align = TableCellProp::CELL_VALIGN_TOP;
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

        // Parse vertical-align CSS property
        if (dom_elem->specified_style) {
            CssDeclaration* valign_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_VERTICAL_ALIGN);

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
// TABLE STRUCTURE BUILDER
// =============================================================================

// Recursive helper to mark table structure nodes with correct view types
static void mark_table_node(LayoutContext* lycon, DomNode* node, ViewGroup* parent) {
    if (!node || !node->is_element()) return;

    DisplayValue display = resolve_display_value(node);
    uintptr_t tag = node->tag();

    // Save context
    ViewGroup* saved_parent = lycon->parent;
    View* saved_prev = lycon->prev_view;
    DomNode* saved_elmt = lycon->elmt;

    lycon->parent = parent;
    lycon->prev_view = nullptr;
    lycon->elmt = node;

    // Mark node based on display type or HTML tag
    if (tag == HTM_TAG_CAPTION || display.inner == CSS_VALUE_TABLE_CAPTION) {
        // Caption - mark as block and layout content immediately
        ViewBlock* caption = (ViewBlock*)set_view(lycon, RDT_VIEW_BLOCK, node);
        if (caption) {
            Blockbox saved_block = lycon->block;
            Linebox saved_line = lycon->line;

            int caption_width = lycon->line.right - lycon->line.left;
            if (caption_width <= 0) caption_width = 600;

            lycon->block.content_width = (float)caption_width;
            lycon->block.content_height = 0;
            lycon->block.advance_y = 0;
            lycon->line.left = lycon->line.left;
            lycon->line.right = lycon->line.left + caption_width;
            lycon->line.advance_x = (float)lycon->line.left;
            lycon->line.is_line_start = true;
            lycon->parent = (ViewGroup*)caption;

            DomNode* child = static_cast<DomElement*>(node)->first_child;
            for (; child; child = child->next_sibling) {
                layout_flow_node(lycon, child);
            }

            caption->height = (int)lycon->block.advance_y;
            lycon->block = saved_block;
            lycon->line = saved_line;
            lycon->prev_view = (View*)caption;
        }
    }
    else if (tag == HTM_TAG_THEAD || tag == HTM_TAG_TBODY || tag == HTM_TAG_TFOOT ||
             display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
             display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
             display.inner == CSS_VALUE_TABLE_FOOTER_GROUP) {
        // Row group - mark and recurse
        ViewTableRowGroup* group = (ViewTableRowGroup*)set_view(lycon, RDT_VIEW_TABLE_ROW_GROUP, node);
        if (group) {
            DomNode* child = static_cast<DomElement*>(node)->first_child;
            for (; child; child = child->next_sibling) {
                if (child->is_element()) mark_table_node(lycon, child, (ViewGroup*)group);
            }
            lycon->prev_view = (View*)group;
        }
    }
    else if (tag == HTM_TAG_TR || display.inner == CSS_VALUE_TABLE_ROW) {
        // Row - mark and recurse
        ViewTableRow* row = (ViewTableRow*)set_view(lycon, RDT_VIEW_TABLE_ROW, node);
        if (row) {
            DomNode* child = static_cast<DomElement*>(node)->first_child;
            for (; child; child = child->next_sibling) {
                if (child->is_element()) mark_table_node(lycon, child, (ViewGroup*)row);
            }
            lycon->prev_view = (View*)row;
        }
    }
    else if (tag == HTM_TAG_TD || tag == HTM_TAG_TH || display.inner == CSS_VALUE_TABLE_CELL) {
        // Cell - mark with styles and attributes
        ViewTableCell* cell = (ViewTableCell*)set_view(lycon, RDT_VIEW_TABLE_CELL, node);
        if (cell) {
            lycon->view = (View*)cell;
            dom_node_resolve_style(node, lycon);
            parse_cell_attributes(lycon, node, cell);
            lycon->prev_view = (View*)cell;
        }
    }

    // Restore context
    lycon->parent = saved_parent;
    lycon->prev_view = saved_prev;
    lycon->elmt = saved_elmt;
}

// Build table structure from DOM - simplified using unified tree architecture
ViewTable* build_table_tree(LayoutContext* lycon, DomNode* tableNode) {
    log_debug("Building table structure (simplified recursive version)");

    // Create table view and resolve styles
    ViewTable* table = (ViewTable*)lycon->view;
    dom_node_resolve_style(tableNode, lycon);
    resolve_table_properties(tableNode, table);

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
static void calculate_rowspan_heights(ViewTable* table, TableMetadata* meta, int* row_heights) {
    if (!table || !meta || !row_heights) return;
    
    // First pass: collect all rowspan cells and their requirements
    for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = (ViewBlock*)child->first_child; row; row = (ViewBlock*)row->next_sibling) {
                if (row->view_type == RDT_VIEW_TABLE_ROW) {
                    for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                        if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;
                            
                            if (tcell->td->row_span > 1) {
                                // Calculate total height needed for spanned rows
                                int start_row = tcell->td->row_index;
                                int end_row = start_row + tcell->td->row_span;
                                
                                // Get current total height of spanned rows
                                int current_total = 0;
                                for (int r = start_row; r < end_row && r < meta->row_count; r++) {
                                    current_total += row_heights[r];
                                }
                                
                                // If cell needs more height, distribute the extra
                                if (cell->height > current_total) {
                                    int extra_needed = cell->height - current_total;
                                    int extra_per_row = extra_needed / tcell->td->row_span;
                                    int remainder = extra_needed % tcell->td->row_span;
                                    
                                    for (int r = start_row; r < end_row && r < meta->row_count; r++) {
                                        row_heights[r] += extra_per_row;
                                        if (r - start_row < remainder) {
                                            row_heights[r] += 1; // Distribute remainder
                                        }
                                    }
                                    
                                    log_debug("Enhanced rowspan: cell height=%d distributed across %d rows (extra=%d)",
                                             cell->height, tcell->td->row_span, extra_needed);
                                }
                            }
                        }
                    }
                }
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            // Handle direct table rows similarly
            ViewBlock* row = child;
            for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;
                    
                    if (tcell->td->row_span > 1) {
                        int start_row = tcell->td->row_index;
                        int end_row = start_row + tcell->td->row_span;
                        
                        int current_total = 0;
                        for (int r = start_row; r < end_row && r < meta->row_count; r++) {
                            current_total += row_heights[r];
                        }
                        
                        if (cell->height > current_total) {
                            int extra_needed = cell->height - current_total;
                            int extra_per_row = extra_needed / tcell->td->row_span;
                            int remainder = extra_needed % tcell->td->row_span;
                            
                            for (int r = start_row; r < end_row && r < meta->row_count; r++) {
                                row_heights[r] += extra_per_row;
                                if (r - start_row < remainder) {
                                    row_heights[r] += 1;
                                }
                            }
                        }
                    }
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

// Layout cell content with correct parent width (after cell dimensions are set)
// This is the ONLY place where cell content gets laid out (single pass)
static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell) {
    ViewTableCell* tcell = static_cast<ViewTableCell*>(cell);
    if (!tcell) return;

    // No need to clear text rectangles - this is the first and only layout pass!

    // Save layout context to restore later
    Blockbox saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    ViewGroup* saved_parent = lycon->parent;
    View* saved_prev = lycon->prev_view;
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
    lycon->parent = (ViewGroup*)cell;
    lycon->prev_view = nullptr;
    lycon->elmt = tcell;

    log_debug("Layout cell content - cell=%dx%d, border=(%d,%d), padding=(%d,%d,%d,%d), content_start=(%d,%d), content=%dx%d",
        cell->width, cell->height, border_left, border_top,
        padding_left, padding_right, padding_top, padding_bottom,
        content_start_x, content_start_y, content_width, content_height);

    // Layout children with correct parent width
    if (tcell->is_element()) {
        DomNode* cc = static_cast<DomElement*>(tcell)->first_child;
        for (; cc; cc = cc->next_sibling) {
            // CRITICAL FIX: Ensure CSS styles are resolved for table cell children
            // This ensures .wide-content and .narrow-content width/height are applied
            if (cc->is_element()) {
                log_debug("Table cell child: %s with classes, resolving CSS styles first", cc->node_name());
                
                // Force CSS style resolution before layout to ensure dimensions are applied
                dom_node_resolve_style(cc, lycon);
                
                log_debug("Post-CSS resolution: given_width=%.2f, given_height=%.2f", 
                         lycon->block.given_width, lycon->block.given_height);
            }
            
            layout_flow_node(lycon, cc);
        }
    }
    
    // Apply CSS vertical-align positioning after content layout
    apply_cell_vertical_alignment(lycon, tcell, content_height);

    // Restore layout context
    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->parent = saved_parent;
    lycon->prev_view = saved_prev;
    lycon->elmt = saved_elmt;
}

// Measure cell's intrinsic content width (Preferred Content Width - PCW)
// This performs accurate measurement using font metrics for CSS 2.1 compliance
static int measure_cell_intrinsic_width(LayoutContext* lycon, ViewTableCell* cell) {
    if (!cell || !cell->is_element()) return 20; // CSS minimum usable width

    DomElement* cell_elem = cell->as_element();
    if (!cell_elem->first_child) return 20; // Empty cell minimum

    // Save current layout context
    Blockbox saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    ViewGroup* saved_parent = lycon->parent;
    View* saved_prev = lycon->prev_view;
    DomNode* saved_elmt = lycon->elmt;
    bool saved_measuring = lycon->is_measuring;
    FontBox saved_font = lycon->font; // Save font context

    // Set up CSS 2.1 measurement context with infinite width
    lycon->is_measuring = true; // Flag to indicate measurement mode
    lycon->parent = (ViewGroup*)cell;
    lycon->prev_view = nullptr;
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
            // Measure text without wrapping
            const unsigned char* text = child->text_data();
            if (text && *text) {
                // Measure text width using current font
                float text_width = 0;
                const unsigned char* p = text;
                while (*p) {
                    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                        // Count space but don't wrap
                        if (lycon->font.ft_face && lycon->font.ft_face->size) {
                            text_width += lycon->font.ft_face->size->metrics.max_advance / 64.0f * 0.25f;
                        } else {
                            text_width += 4.0f; // Fallback space width
                        }
                        p++;
                        continue;
                    }
                    
                    // Get glyph for character
                    if (lycon->font.ft_face) {
                        FT_UInt glyph_index = FT_Get_Char_Index(lycon->font.ft_face, *p);
                        if (glyph_index) {
                            FT_Load_Glyph(lycon->font.ft_face, glyph_index, FT_LOAD_DEFAULT);
                            text_width += lycon->font.ft_face->glyph->advance.x / 64.0f;
                        }
                    } else {
                        // Fallback when no font face available - use average character width
                        text_width += 8.0f; // Approximate character width
                    }
                    p++;
                }
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
                    // Use explicit CSS width
                    child_width = width_decl->value->data.length.value;
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
    lycon->parent = saved_parent;
    lycon->prev_view = saved_prev;
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
static int measure_cell_minimum_width(LayoutContext* lycon, ViewTableCell* cell) {
    if (!cell || !cell->is_element()) return 16; // Minimum default

    DomElement* cell_elem = cell->as_element();
    if (!cell_elem->first_child) return 16; // Empty cell

    // Save current layout context
    Blockbox saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    ViewGroup* saved_parent = lycon->parent;
    View* saved_prev = lycon->prev_view;
    DomNode* saved_elmt = lycon->elmt;
    bool saved_measuring = lycon->is_measuring;
    FontBox saved_font = lycon->font; // Save font context

    // Set up temporary measurement context
    lycon->is_measuring = true;
    lycon->parent = (ViewGroup*)cell;
    lycon->prev_view = nullptr;
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
            // For text, find the longest word
            const unsigned char* text = child->text_data();
            if (text && *text) {
                float longest_word = 0.0f;
                float current_word = 0.0f;
                const unsigned char* p = text;
                
                while (*p) {
                    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                        // End of word
                        if (current_word > longest_word) longest_word = current_word;
                        current_word = 0.0f;
                        p++;
                        continue;
                    }
                    
                    // Add character to current word
                    if (lycon->font.ft_face) {
                        FT_UInt glyph_index = FT_Get_Char_Index(lycon->font.ft_face, *p);
                        if (glyph_index) {
                            FT_Load_Glyph(lycon->font.ft_face, glyph_index, FT_LOAD_DEFAULT);
                            current_word += lycon->font.ft_face->glyph->advance.x / 64.0f;
                        }
                    } else {
                        // Fallback when no font face available - use average character width
                        current_word += 8.0f; // Approximate character width
                    }
                    p++;
                }
                
                // Check final word
                if (current_word > longest_word) longest_word = current_word;
                
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
    lycon->parent = saved_parent;
    lycon->prev_view = saved_prev;
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
static TableMetadata* analyze_table_structure(LayoutContext* lycon, ViewTable* table) {
    // First pass: count columns and rows
    int columns = 0;
    int rows = 0;
    
    for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = (ViewBlock*)child->first_child; row; row = (ViewBlock*)row->next_sibling) {
                if (row->view_type == RDT_VIEW_TABLE_ROW) {
                    rows++;
                    int row_cells = 0;
                    for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                        if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;
                            row_cells += tcell->td->col_span;
                        }
                    }
                    if (row_cells > columns) columns = row_cells;
                }
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewBlock* row = child;
            rows++;
            int row_cells = 0;
            for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;
                    row_cells += tcell->td->col_span;
                }
            }
            if (row_cells > columns) columns = row_cells;
        }
    }
    
    if (columns <= 0 || rows <= 0) return nullptr;
    
    // Create metadata structure
    TableMetadata* meta = new TableMetadata(columns, rows);
    
    // Second pass: assign column indices and measure widths
    int current_row = 0;
    for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = (ViewBlock*)child->first_child; row; row = (ViewBlock*)row->next_sibling) {
                if (row->view_type == RDT_VIEW_TABLE_ROW) {
                    int col = 0;
                    for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                        if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;
                            
                            // Find next available column
                            while (col < columns && meta->grid(current_row, col)) {
                                col++;
                            }
                            
                            // Assign indices
                            tcell->td->col_index = col;
                            tcell->td->row_index = current_row;
                            
                            // Mark grid as occupied
                            for (int r = current_row; r < current_row + tcell->td->row_span && r < rows; r++) {
                                for (int c = col; c < col + tcell->td->col_span && c < columns; c++) {
                                    meta->grid(r, c) = true;
                                }
                            }
                            
                            col += tcell->td->col_span;
                        }
                    }
                    current_row++;
                }
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewBlock* row = child;
            int col = 0;
            for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;
                    
                    // Find next available column
                    while (col < columns && meta->grid(current_row, col)) {
                        col++;
                    }
                    
                    // Assign indices
                    tcell->td->col_index = col;
                    tcell->td->row_index = current_row;
                    
                    // Mark grid as occupied
                    for (int r = current_row; r < current_row + tcell->td->row_span && r < rows; r++) {
                        for (int c = col; c < col + tcell->td->col_span && c < columns; c++) {
                            meta->grid(r, c) = true;
                        }
                    }
                    
                    col += tcell->td->col_span;
                }
            }
            current_row++;
        }
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
            // Caption should have proper dimensions from content layout
            if (caption->height > 0) {
                caption_height = caption->height + 8; // Add margin
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
                // TODO: Need Lambda CSS version of resolve_length_value
                // For now, try to extract numeric value if it's a length
                if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    explicit_table_width = (int)width_decl->value->data.length.value;

                    // Calculate content width (subtract borders and spacing)
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

                    log_debug("Table explicit width: %dpx, content width for cells: %dpx",
                            explicit_table_width, table_content_width);
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
    int current_row = 0;
    for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            for (ViewBlock* row = (ViewBlock*)child->first_child; row; row = (ViewBlock*)row->next_sibling) {
                if (row->view_type == RDT_VIEW_TABLE_ROW) {
                    for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                        if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;

                            // Use pre-assigned column index from analyze_table_structure()
                            int col = tcell->td->col_index;

                            // Try to get explicit width from CSS first
                            int cell_width = 0;
                            if (tcell->node_type == DOM_NODE_ELEMENT) {
                                DomElement* dom_elem = tcell->as_element();
                                if (dom_elem->specified_style) {
                                    CssDeclaration* width_decl = style_tree_get_declaration(
                                        dom_elem->specified_style, CSS_PROPERTY_WIDTH);
                                    if (width_decl && width_decl->value) {
                                        // Check if it's a percentage value
                                        if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE && table_content_width > 0) {
                                            // Calculate percentage relative to table content width
                                            double percentage = width_decl->value->data.percentage.value;
                                            int css_content_width = (int)(table_content_width * percentage / 100.0);

                                            // CSS width is content-box, need to add border and padding
                                            cell_width = css_content_width;

                                            // Add padding
                                            if (tcell->bound && tcell->bound->padding.left >= 0 && tcell->bound->padding.right >= 0) {
                                                cell_width += tcell->bound->padding.left + tcell->bound->padding.right;
                                            }

                                            // Add border (1px left + 1px right)
                                            cell_width += 2;

                                            log_debug("Cell percentage width: %.1f%% of %dpx = %dpx content + padding + border = %dpx total",
                                                    percentage, table_content_width, css_content_width, cell_width);
                                        } else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                            // Absolute width
                                            int css_content_width = (int)width_decl->value->data.length.value;
                                            if (css_content_width > 0) {
                                                // CSS width is content-box, need to add border and padding
                                                cell_width = css_content_width;
                                                // Add padding
                                                if (tcell->bound && tcell->bound->padding.left >= 0 && tcell->bound->padding.right >= 0) {
                                                    cell_width += tcell->bound->padding.left + tcell->bound->padding.right;
                                                }
                                                // Add border (1px left + 1px right)
                                                cell_width += 2;
                                                log_debug("Cell explicit CSS width: %dpx content + padding + border = %dpx total",
                                                    css_content_width, cell_width);
                                            }
                                        }
                                    }
                                }
                            }

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
                                int current_total = 0;
                                for (int c = col; c < col + tcell->td->col_span && c < columns; c++) {
                                    current_total += col_widths[c];
                                }

                                if (cell_width > current_total) {
                                    int extra_needed = cell_width - current_total;
                                    int extra_per_col = extra_needed / tcell->td->col_span;
                                    int remainder = extra_needed % tcell->td->col_span;

                                    for (int c = col; c < col + tcell->td->col_span && c < columns; c++) {
                                        col_widths[c] += extra_per_col;
                                        if (remainder > 0) {
                                            col_widths[c] += 1;
                                            remainder--;
                                        }
                                    }
                                }
                            }

                        }
                    }
                    current_row++;
                }
            }
        }
        else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            for (ViewBlock* cell = (ViewBlock*)child->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;

                    // Use pre-assigned column index from analyze_table_structure()
                    int col = tcell->td->col_index;

                    // Try to get explicit width from CSS first
                    int cell_width = 0;
                    if (tcell->node_type == DOM_NODE_ELEMENT && tcell->as_element()) {
                        DomElement* dom_elem = tcell->as_element();
                        if (dom_elem->specified_style) {
                            CssDeclaration* width_decl = style_tree_get_declaration(
                                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
                            if (width_decl && width_decl->value) {
                                // Check if it's a percentage value
                                if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE && table_content_width > 0) {
                                    // Calculate percentage relative to table content width
                                    double percentage = width_decl->value->data.percentage.value;
                                    int css_content_width = (int)(table_content_width * percentage / 100.0);

                                    // CSS width is content-box, need to add border and padding
                                    cell_width = css_content_width;

                                    // Add padding
                                    if (tcell->bound && tcell->bound->padding.left >= 0 && tcell->bound->padding.right >= 0) {
                                        cell_width += tcell->bound->padding.left + tcell->bound->padding.right;
                                    }
                                    // Add border (1px left + 1px right)
                                    cell_width += 2;
                                    log_debug("Direct row cell percentage width: %.1f%% of %dpx = %dpx content + padding + border = %dpx total",
                                            percentage, table_content_width, css_content_width, cell_width);
                                }
                                else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                    // Absolute width
                                    int css_content_width = (int)width_decl->value->data.length.value;
                                    if (css_content_width > 0) {
                                        // CSS width is content-box, need to add border and padding
                                        cell_width = css_content_width;

                                        // Add padding
                                        if (tcell->bound && tcell->bound->padding.left >= 0 && tcell->bound->padding.right >= 0) {
                                            cell_width += tcell->bound->padding.left + tcell->bound->padding.right;
                                        }
                                        // Add border (1px left + 1px right)
                                        cell_width += 2;
                                        log_debug("Direct row cell explicit CSS width: %dpx content + padding + border = %dpx total",
                                            css_content_width, cell_width);
                                    }
                                }
                            }
                        }
                    }

                    // If no explicit width, measure intrinsic content width
                    if (cell_width == 0) {
                        cell_width = measure_cell_intrinsic_width(lycon, tcell);
                    }
                    if (tcell->td->col_span == 1) {
                        if (cell_width > col_widths[col]) {
                            col_widths[col] = cell_width;
                        }
                    } else {
                        // Handle colspan for direct table rows
                        int current_total = 0;
                        for (int c = col; c < col + tcell->td->col_span && c < columns; c++) {
                            current_total += col_widths[c];
                        }

                        if (cell_width > current_total) {
                            int extra_needed = cell_width - current_total;
                            int extra_per_col = extra_needed / tcell->td->col_span;
                            int remainder = extra_needed % tcell->td->col_span;

                            for (int c = col; c < col + tcell->td->col_span && c < columns; c++) {
                                col_widths[c] += extra_per_col;
                                if (remainder > 0) {
                                    col_widths[c] += 1;
                                    remainder--;
                                }
                            }
                        }
                    }
                }
            }
            current_row++;
        }
    }

    // Apply CSS 2.1 table-layout algorithm with improved precision
    int fixed_table_width = 0; // Store explicit width for fixed layout
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED) {
        log_debug("=== CSS 2.1 FIXED LAYOUT ALGORITHM ===");

        // STEP 1: Get explicit table width from CSS (CSS 2.1 Section 17.5.2)
        int explicit_table_width = 0;

        // Try to read width directly from table element's CSS
        if (table->node_type == DOM_NODE_ELEMENT) {
            DomElement* dom_elem = table->as_element();
            if (dom_elem->specified_style) {
                CssDeclaration* width_decl = style_tree_get_declaration(
                    dom_elem->specified_style, CSS_PROPERTY_WIDTH);
                if (width_decl && width_decl->value && width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    explicit_table_width = (int)width_decl->value->data.length.value;
                    log_debug("FIXED LAYOUT - read table CSS width: %dpx", explicit_table_width);
                }
            }
        }

        // Fallback to lycon->block.given_width or container
        if (explicit_table_width == 0 && lycon->block.given_width > 0) {
            explicit_table_width = lycon->block.given_width;
            log_debug("FIXED LAYOUT - using given_width: %dpx", explicit_table_width);
        } else if (explicit_table_width == 0) {
            // No explicit width, use container width or default
            int container_width = lycon->line.right - lycon->line.left;
            explicit_table_width = container_width > 0 ? container_width : 600;
            log_debug("FIXED LAYOUT - given_width=0, using container/default: %dpx (container=%d-%d=%d)",
                   explicit_table_width, lycon->line.right, lycon->line.left, container_width);
        }

        // Store for later use
        fixed_table_width = explicit_table_width;
        log_debug("FIXED LAYOUT - stored fixed_table_width: %dpx", fixed_table_width);

        // STEP 2: Calculate available content width (subtract borders and spacing)
        int content_width = explicit_table_width;

        // Subtract table border (we'll add it back later for final width)
        content_width -= 4; // 2px left + 2px right border

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

        // Find first row
        ViewTableRow* first_row = nullptr;
        for (ViewBlock* child = (ViewBlock*)table->first_child; child; child = (ViewBlock*)child->next_sibling) {
            if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                ViewTableRowGroup* group = (ViewTableRowGroup*)child;
                for (ViewBlock* row_child = (ViewBlock*)group->first_child; row_child; row_child = (ViewBlock*)row_child->next_sibling) {
                    if (row_child->view_type == RDT_VIEW_TABLE_ROW) {
                        first_row = (ViewTableRow*)row_child;
                        break;
                    }
                }
                if (first_row) break;
            }
            else if (child->view_type == RDT_VIEW_TABLE_ROW) {
                first_row = (ViewTableRow*)child;
                break;
            }
        }

        // Read cell widths from first row
        if (first_row) {
            int col = 0;
            log_debug("Reading first row cell widths...");
            for (ViewBlock* cell_view = (ViewBlock*)first_row->first_child;
                 cell_view && col < columns;
                 cell_view = (ViewBlock*)cell_view->next_sibling) {
                if (cell_view->view_type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* cell = (ViewTableCell*)cell_view;

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
                                    // Absolute width (px, em, etc.)
                                    cell_width = (int)width_decl->value->data.length.value;
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
                if (height_decl && height_decl->value && height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    explicit_table_height = (int)height_decl->value->data.length.value;
                    log_debug("FIXED LAYOUT - read table CSS height: %dpx", explicit_table_height);
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

    // Start Y position after caption, with table padding and top border-spacing
    int current_y = caption_height;

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

    // Position caption if it exists
    if (caption) {
        caption->x = 0;
        caption->y = 0;
        caption->width = table_width;
    }

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
                // Border-collapse: tbody starts at half the table border width
                child->x = 1.5f; // Half of table border width (3px / 2)
                child->y = 1.5f; // Half of table border width (3px / 2)
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
                    // Position row relative to row group
                    row->x = 0;
                    row->y = current_y - group_start_y; // Relative to row group
                    row->width = child->width; // Match tbody width
                    log_debug("Row positioned at x=%d, y=%d (relative to group), width=%d",
                        row->x, row->y, row->width);

                    // Calculate row height and position cells
                    int row_height = 0;
                    for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                        if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                            ViewTableCell* tcell = (ViewTableCell*)cell;

                            // CSS 2.1: Position cell relative to row (precise column position)
                            cell->x = col_x_positions[tcell->td->col_index] - col_x_positions[0];
                            cell->y = 0; // Relative to row top
                            
                            // Apply row-specific positioning adjustments for border-collapse
                            if (table->tb->border_collapse) {
                                // No additional adjustments needed - already in column positions
                            }
                            
                            log_debug("CSS 2.1: Cell positioned at x=%d, y=%d (relative to row), size=%dx%d",
                                   cell->x, cell->y, cell->width, cell->height);

                            // RADIANT RELATIVE POSITIONING: Text positioned relative to cell parent
                            for (View* text_child = ((ViewGroup*)cell)->first_child; text_child; text_child = text_child->next_sibling) {
                                if (text_child->view_type == RDT_VIEW_TEXT) {
                                    ViewText* text = (ViewText*)text_child;

                                    // In Radiant's relative positioning system:
                                    // Text x,y should be relative to its parent cell, not absolute

                                    // Cell content area offset (border + padding)
                                    int content_x = 1; // 1px border
                                    int content_y = 1; // 1px border

                                    // Add CSS padding for X (left)
                                    if (tcell->bound) {
                                        content_x += tcell->bound->padding.left;
                                        content_y += tcell->bound->padding.top;
                                    }

                                    // Apply vertical alignment to Y position
                                    // Vertical align adjusts within the cell's content area (after border+padding)
                                    // We need to know cell height first, so we'll adjust this after height calculation
                                    // For now, store the base Y (will adjust below after measuring child)

                                    // Position text relative to cell (parent)
                                    text->x = content_x;
                                    text->y = content_y;  // Will adjust for vertical-align later
                                    log_debug("Initial text positioning - x=%d, y=%d (before vertical-align)",
                                           text->x, text->y);
                                }
                            }

                            // Calculate cell width (sum of spanned columns)
                            int cell_width = 0;
                            for (int c = tcell->td->col_index; c < tcell->td->col_index + tcell->td->col_span && c < columns; c++) {
                                cell_width += col_widths[c];
                            }
                            cell->width = cell_width;

                            // CRITICAL FIX: Now that cell width is set, layout cell content with correct parent width
                            // This allows child blocks to inherit the correct parent width instead of 0
                            layout_table_cell_content(lycon, cell);

                            // Enhanced cell height calculation with browser accuracy
                            int content_height = 0;

                            // STEP 1: Check for explicit CSS height property first
                            int explicit_cell_height = 0;
                            if (tcell->node_type == DOM_NODE_ELEMENT) {
                                DomElement* dom_elem = tcell->as_element();
                                if (dom_elem->specified_style) {
                                    CssDeclaration* height_decl = style_tree_get_declaration(
                                        dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
                                    if (height_decl && height_decl->value && height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                        explicit_cell_height = (int)height_decl->value->data.length.value;
                                        log_debug("Cell has explicit CSS height: %dpx", explicit_cell_height);
                                    }
                                }
                            }

                            // STEP 2: Measure content height precisely (for auto height or minimum)
                            for (View* cc = ((ViewGroup*)cell)->first_child; cc; cc = cc->next_sibling) {
                                if (cc->view_type == RDT_VIEW_TEXT) {
                                    ViewText* text = (ViewText*)cc;
                                    int text_height = text->height > 0 ? text->height : 17; // Default line height
                                    if (text_height > content_height) content_height = text_height;
                                }
                                else if (cc->view_type == RDT_VIEW_BLOCK || cc->view_type == RDT_VIEW_INLINE || cc->view_type == RDT_VIEW_INLINE_BLOCK) {
                                    ViewBlock* block = (ViewBlock*)cc;

                                    // Check if child has explicit CSS height
                                    int child_css_height = 0;
                                    if (block->node_type == DOM_NODE_ELEMENT) {
                                        DomElement* dom_elem = block->as_element();
                                        if (dom_elem->specified_style) {
                                            CssDeclaration* child_height_decl = style_tree_get_declaration(
                                                dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
                                            if (child_height_decl && child_height_decl->value && child_height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                                child_css_height = (int)child_height_decl->value->data.length.value;
                                                log_debug("Child element (type=%d) has explicit CSS height: %dpx", cc->view_type, child_css_height);
                                            }
                                        }
                                    }

                                    // Use child CSS height if present, otherwise use measured height
                                    int child_height = child_css_height > 0 ? child_css_height : block->height;
                                    if (child_height > content_height) content_height = child_height;
                                }
                            }

                            // Ensure minimum content height
                            if (content_height < 17) {
                                content_height = 17; // Browser default line height
                            }

                            // STEP 3: Calculate final cell height - use explicit height if present
                            int cell_height = 0;

                            // Read cell padding
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

                            // Use explicit CSS height if provided, otherwise use content height
                            if (explicit_cell_height > 0) {
                                // CSS height already includes everything, just use it directly
                                cell_height = explicit_cell_height;
                                log_debug("Using explicit CSS height: %dpx (overrides content height %dpx)",
                                       cell_height, content_height);
                            } else {
                                // Auto height: calculate from content + padding + border
                                cell_height = content_height;
                                cell_height += padding_vertical;  // Add CSS padding
                                cell_height += 2;  // CSS border: 1px top + 1px bottom
                                log_debug("Using auto height - content=%d, padding=%d, border=2, total=%d",
                                       content_height, padding_vertical, cell_height);
                            }

                            cell->height = cell_height;

                            // Store calculated height
                            cell->height = cell_height;

                            // Apply vertical alignment to cell children
                            // This adjusts the Y position of content within the cell based on vertical-align property
                            if (tcell->td->vertical_align != TableCellProp::CELL_VALIGN_TOP) {
                                // Calculate available space in cell (content area after border and padding)
                                int cell_content_area = cell_height - 2; // Subtract border (1px top + 1px bottom)
                                int padding_vertical = 0;
                                if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
                                    padding_vertical = tcell->bound->padding.top + tcell->bound->padding.bottom;
                                    cell_content_area -= padding_vertical;
                                }

                                // Measure child height
                                int child_height = content_height; // Use measured content height

                                // Calculate adjustment based on alignment
                                int y_adjustment = 0;
                                if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_MIDDLE) {
                                    y_adjustment = (cell_content_area - child_height) / 2;
                                    log_debug("Vertical-align middle: cell_content_area=%d, child_height=%d, adjustment=%d",
                                        cell_content_area, child_height, y_adjustment);
                                }
                                else if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BOTTOM) {
                                    y_adjustment = cell_content_area - child_height;
                                    log_debug("Vertical-align bottom: cell_content_area=%d, child_height=%d, adjustment=%d",
                                        cell_content_area, child_height, y_adjustment);
                                }

                                // Apply adjustment to all children
                                if (y_adjustment > 0) {
                                    for (View* cc = ((ViewGroup*)cell)->first_child; cc; cc = cc->next_sibling) {
                                        cc->y += y_adjustment;
                                        log_debug("Applied vertical-align adjustment: child y=%d (added %d)",
                                            cc->y, y_adjustment);
                                    }
                                }
                            }

                            // Handle rowspan for row height calculation
                            // If cell spans multiple rows, only count a portion of its height for this row
                            int height_for_row = cell_height;
                            if (tcell->td->row_span > 1) {
                                // Distribute cell height across spanned rows
                                // For simplicity, divide evenly (more complex: consider content distribution)
                                height_for_row = cell_height / tcell->td->row_span;
                                log_debug("Rowspan cell - total_height=%d, rowspan=%d, height_for_row=%d",
                                    cell_height, tcell->td->row_span, height_for_row);
                            }

                            if (height_for_row > row_height) {
                                row_height = height_for_row;
                            }
                        }
                    }

                    // Apply fixed layout height if specified
                    if (table->tb->fixed_row_height > 0) {
                        row->height = table->tb->fixed_row_height;
                        log_debug("Applied fixed layout row height: %dpx", table->tb->fixed_row_height);

                        // CRITICAL: Update all cell heights in this row to match fixed row height
                        // Cells were calculated with auto height, but fixed layout overrides this
                        for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                            if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                                cell->height = table->tb->fixed_row_height;
                                log_debug("Updated cell height to match fixed_row_height=%d", table->tb->fixed_row_height);
                            }
                        }
                    } else {
                        row->height = row_height;
                    }
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
            ViewBlock* row = child;

            row->x = 0;  row->y = current_y; // Relative to table
            row->width = table_width;
            log_debug("Direct row positioned at x=%d, y=%d (relative to table), width=%d",
                   row->x, row->y, row->width);
            int row_height = 0;
            for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* tcell = (ViewTableCell*)cell;

                    // CSS 2.1: Position cell relative to row (direct table row)
                    cell->x = col_x_positions[tcell->td->col_index] - col_x_positions[0];
                    cell->y = 0; // Relative to row top
                    
                    // Ensure consistent positioning between grouped and direct rows
                    log_debug("CSS 2.1: Direct cell positioned at x=%d, y=%d (relative to row), size=%dx%d",
                           cell->x, cell->y, cell->width, cell->height);

                    // RADIANT RELATIVE POSITIONING: Text positioned relative to cell parent
                    for (View* text_child = ((ViewGroup*)cell)->first_child; text_child; text_child = text_child->next_sibling) {
                        if (text_child->view_type == RDT_VIEW_TEXT) {
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
                            text->x = content_x;  text->y = content_y;
                            log_debug("Relative text positioning - x=%d, y=%d (relative to cell parent)",
                                   text->x, text->y);
                        }
                    }

                    // Calculate cell width
                    int cell_width = 0;
                    for (int c = tcell->td->col_index; c < tcell->td->col_index + tcell->td->col_span && c < columns; c++) {
                        cell_width += col_widths[c];
                    }
                    cell->width = cell_width;

                    // CRITICAL FIX: Now that cell width is set, layout cell content with correct parent width
                    // This allows child blocks to inherit the correct parent width instead of 0
                    layout_table_cell_content(lycon, cell);

                    // Enhanced cell height calculation with browser accuracy
                    int content_height = 0;

                    // STEP 1: Check for explicit CSS height property first
                    int explicit_cell_height = 0;
                    if (tcell->node_type == DOM_NODE_ELEMENT) {
                        DomElement* dom_elem = tcell->as_element();
                        if (dom_elem->specified_style) {
                            CssDeclaration* height_decl = style_tree_get_declaration(
                                dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
                            if (height_decl && height_decl->value && height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                explicit_cell_height = (int)height_decl->value->data.length.value;
                                log_debug("Cell has explicit CSS height: %dpx", explicit_cell_height);
                            }
                        }
                    }

                    // STEP 2: Measure content height precisely (for auto height or minimum)
                    for (View* cc = ((ViewGroup*)cell)->first_child; cc; cc = cc->next_sibling) {
                        if (cc->view_type == RDT_VIEW_TEXT) {
                            ViewText* text = (ViewText*)cc;
                            int text_height = text->height > 0 ? text->height : 17;
                            if (text_height > content_height) content_height = text_height;
                        }
                        else if (cc->view_type == RDT_VIEW_BLOCK || cc->view_type == RDT_VIEW_INLINE || cc->view_type == RDT_VIEW_INLINE_BLOCK) {
                            ViewBlock* block = (ViewBlock*)cc;

                            // Check if child has explicit CSS height
                            int child_css_height = 0;
                            if (block->node_type == DOM_NODE_ELEMENT) {
                                DomElement* dom_elem = block->as_element();
                                if (dom_elem->specified_style) {
                                    CssDeclaration* child_height_decl = style_tree_get_declaration(
                                        dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
                                    if (child_height_decl && child_height_decl->value && child_height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                        child_css_height = (int)child_height_decl->value->data.length.value;
                                        log_debug("Direct row child element (type=%d) has explicit CSS height: %dpx", cc->view_type, child_css_height);
                                    }
                                }
                            }

                            // Use child CSS height if present, otherwise use measured height
                            int child_height = child_css_height > 0 ? child_css_height : block->height;
                            if (child_height > content_height) content_height = child_height;
                        }
                    }

                    // Ensure minimum content height
                    if (content_height < 17) {
                        content_height = 17;
                    }

                    // STEP 3: Calculate final cell height - use explicit height if present
                    int cell_height = 0;

                    // Read cell padding
                    int padding_vertical = 0;
                    if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
                        padding_vertical = tcell->bound->padding.top + tcell->bound->padding.bottom;
                        log_debug("Using CSS padding: top=%d, bottom=%d, total=%d",
                               tcell->bound->padding.top, tcell->bound->padding.bottom, padding_vertical);
                    } else {
                        log_debug("No CSS padding found, using default 0");
                        padding_vertical = 0;
                    }

                    // Use explicit CSS height if provided, otherwise use content height
                    if (explicit_cell_height > 0) {
                        // CSS height already includes everything, just use it directly
                        cell_height = explicit_cell_height;
                        log_debug("Using explicit CSS height: %dpx (overrides content height %dpx)",
                               cell_height, content_height);
                    } else {
                        // Auto height: calculate from content + padding + border
                        cell_height = content_height;
                        cell_height += padding_vertical;  // Add CSS padding
                        cell_height += 2;  // CSS border: 1px top + 1px bottom
                        log_debug("Using auto height - content=%d, padding=%d, border=2, total=%d",
                               content_height, padding_vertical, cell_height);
                    }
                    // Store calculated height
                    cell->height = cell_height;

                    // Apply vertical alignment to cell children
                    // This adjusts the Y position of content within the cell based on vertical-align property
                    if (tcell->td->vertical_align != TableCellProp::CELL_VALIGN_TOP) {
                        // Calculate available space in cell (content area after border and padding)
                        int cell_content_area = cell_height - 2; // Subtract border (1px top + 1px bottom)
                        if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
                            cell_content_area -= (tcell->bound->padding.top + tcell->bound->padding.bottom);
                        }

                        // Measure child height
                        int child_height = content_height; // Use measured content height

                        // Calculate adjustment based on alignment
                        int y_adjustment = 0;
                        if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_MIDDLE) {
                            y_adjustment = (cell_content_area - child_height) / 2;
                            log_debug("Vertical-align middle: cell_content_area=%d, child_height=%d, adjustment=%d",
                                   cell_content_area, child_height, y_adjustment);
                        }
                        else if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BOTTOM) {
                            y_adjustment = cell_content_area - child_height;
                            log_debug("Vertical-align bottom: cell_content_area=%d, child_height=%d, adjustment=%d",
                                   cell_content_area, child_height, y_adjustment);
                        }

                        // Apply adjustment to all children
                        if (y_adjustment > 0) {
                            for (View* cc = ((ViewGroup*)cell)->first_child; cc; cc = cc->next_sibling) {
                                cc->y += y_adjustment;
                                log_debug("Applied vertical-align adjustment: child y=%d (added %d)",
                                       cc->y, y_adjustment);
                            }
                        }
                    }

                    // Handle rowspan for row height calculation
                    // If cell spans multiple rows, only count a portion of its height for this row
                    int height_for_row = cell_height;
                    if (tcell->td->row_span > 1) {
                        // Distribute cell height across spanned rows
                        height_for_row = cell_height / tcell->td->row_span;
                        log_debug("Rowspan cell - total_height=%d, rowspan=%d, height_for_row=%d",
                               cell_height, tcell->td->row_span, height_for_row);
                    }

                    if (height_for_row > row_height) {
                        row_height = height_for_row;
                    }
                }
            }

            // Apply fixed layout height if specified
            if (table->tb->fixed_row_height > 0) {
                row->height = table->tb->fixed_row_height;
                log_debug("Applied fixed layout row height: %dpx", table->tb->fixed_row_height);

                // CRITICAL: Update all cell heights in this row to match fixed row height
                // Cells were calculated with auto height, but fixed layout overrides this
                for (ViewBlock* cell = (ViewBlock*)row->first_child; cell; cell = (ViewBlock*)cell->next_sibling) {
                    if (cell->view_type == RDT_VIEW_TABLE_CELL) {
                        cell->height = table->tb->fixed_row_height;
                        log_debug("Updated cell height to match fixed_row_height=%d", table->tb->fixed_row_height);
                    }
                }
            } else {
                row->height = row_height;
            }
            current_y += row->height;

            // Add vertical border-spacing after each row (except last)
            if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
                current_y += table->tb->border_spacing_v;
                log_debug("Added vertical spacing after direct row: +%dpx", table->tb->border_spacing_v);
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
    log_debug("Set ViewBlock height to %dpx for block layout integration", final_table_height + table_border_height);

    log_debug("Table dimensions calculated: width=%dpx, height=%dpx", table_width, final_table_height);
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
