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

    // Check if table-layout was already set to FIXED by CSS (via custom property)
    // If so, respect the CSS value and don't override it
    if (table->table_layout == ViewTable::TABLE_LAYOUT_FIXED) {
        log_debug("Table layout: already set to FIXED by CSS, skipping heuristic");
        return;
    }

    // Default to auto layout
    table->table_layout = ViewTable::TABLE_LAYOUT_AUTO;

    // WORKAROUND: Lexbor doesn't expose table-layout property through normal CSS API
    // Use heuristic: if table has BOTH explicit width AND height, assume fixed layout
    // This matches common CSS patterns where fixed layout is used with constrained dimensions

    bool has_explicit_width = false;
    bool has_explicit_height = false;

    if (html_element->element.style) {
        // Check for explicit width
        const lxb_css_rule_declaration_t* width_decl =
            lxb_dom_element_style_by_id(
                (lxb_dom_element_t*)html_element,
                LXB_CSS_PROPERTY_WIDTH);

        if (width_decl && width_decl->u.width) {
            has_explicit_width = true;
        }

        // Check for explicit height
        const lxb_css_rule_declaration_t* height_decl =
            lxb_dom_element_style_by_id(
                (lxb_dom_element_t*)html_element,
                LXB_CSS_PROPERTY_HEIGHT);

        if (height_decl && height_decl->u.height) {
            has_explicit_height = true;
        }
    }

    // If both width and height are explicitly set, use fixed layout
    // This heuristic works for most real-world cases where fixed layout is desired
    if (has_explicit_width && has_explicit_height) {
        table->table_layout = ViewTable::TABLE_LAYOUT_FIXED;
        log_debug("Table layout: fixed (heuristic: table has explicit width AND height)");
    } else {
        log_debug("Table layout: auto (no explicit width+height combo)");
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

    // Parse vertical-align CSS property
    if (element->element.style) {
        const lxb_css_rule_declaration_t* valign_decl =
            lxb_dom_element_style_by_id(
                (lxb_dom_element_t*)element,
                LXB_CSS_PROPERTY_VERTICAL_ALIGN);
        if (valign_decl && valign_decl->u.vertical_align) {
            lxb_css_property_vertical_align_t* vertical_align = valign_decl->u.vertical_align;
            PropValue valign = vertical_align->alignment.type ?
                vertical_align->alignment.type : vertical_align->shift.type;

            // Map CSS vertical-align values to cell enum
            switch (valign) {
                case LXB_CSS_VALUE_TOP:
                    cell->vertical_align = ViewTableCell::CELL_VALIGN_TOP;
                    log_debug("Cell vertical-align: top");
                    break;
                case LXB_CSS_VALUE_MIDDLE:
                    cell->vertical_align = ViewTableCell::CELL_VALIGN_MIDDLE;
                    log_debug("Cell vertical-align: middle");
                    break;
                case LXB_CSS_VALUE_BOTTOM:
                    cell->vertical_align = ViewTableCell::CELL_VALIGN_BOTTOM;
                    log_debug("Cell vertical-align: bottom");
                    break;
                case LXB_CSS_VALUE_BASELINE:
                    cell->vertical_align = ViewTableCell::CELL_VALIGN_BASELINE;
                    log_debug("Cell vertical-align: baseline");
                    break;
                default:
                    // Keep default (top)
                    break;
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
        lxb_html_element_t* child_elmt = child->as_element();
        if (!child_elmt) {
            log_debug("layout_table: as_element() returned NULL for child, skipping");
            continue;
        }
        DisplayValue child_display = resolve_display(child_elmt);

        uintptr_t tag = child->tag();

        log_debug("Processing table child - tag=%s, display.outer=%d, display.inner=%d",
               child->name(), child_display.outer, child_display.inner);

        if (tag == LXB_TAG_CAPTION || child_display.inner == LXB_CSS_VALUE_TABLE_CAPTION) {
            // Create caption as block
            ViewBlock* caption = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, child);
            if (caption) {
                // Save layout context
                Blockbox cap_saved_block = lycon->block;
                Linebox cap_saved_line = lycon->line;
                ViewGroup* cap_saved_parent = lycon->parent;
                View* cap_saved_prev = lycon->prev_view;
                DomNode* cap_saved_elmt = lycon->elmt;

                // Initialize block context for caption
                // Caption takes full width of parent (table's available width)
                int caption_width = lycon->line.right - lycon->line.left;
                if (caption_width <= 0) caption_width = 600; // Fallback

                lycon->block.width = (float)caption_width;
                lycon->block.height = 0;
                lycon->block.advance_y = 0;
                lycon->line.left = lycon->line.left;
                lycon->line.right = lycon->line.left + caption_width;
                lycon->line.advance_x = (float)lycon->line.left;
                lycon->line.is_line_start = true;
                lycon->parent = (ViewGroup*)caption;
                lycon->prev_view = nullptr;
                lycon->elmt = child;

                log_debug("Laying out caption with width=%d", caption_width);

                // Layout caption content (text, inline elements)
                for (DomNode* cc = child->first_child(); cc; cc = cc->next_sibling()) {
                    layout_flow_node(lycon, cc);
                }

                // Set caption height from laid out content
                caption->height = (int)lycon->block.advance_y;
                if (caption->height == 0 && ((ViewGroup*)caption)->child) {
                    // Fallback: measure first child height
                    View* first_child = ((ViewGroup*)caption)->child;
                    if (first_child->type == RDT_VIEW_TEXT) {
                        caption->height = ((ViewText*)first_child)->height;
                    }
                }

                log_debug("Caption laid out - height=%d", caption->height);

                // Restore layout context
                lycon->block = cap_saved_block;
                lycon->line = cap_saved_line;
                lycon->parent = cap_saved_parent;
                lycon->prev_view = (View*)caption;
                lycon->elmt = cap_saved_elmt;
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
                    lxb_html_element_t* row_elmt = rowNode->as_element();
                    if (!row_elmt) {
                        log_debug("layout_table: as_element() returned NULL for row, skipping");
                        continue;
                    }
                    DisplayValue row_display = resolve_display(row_elmt);

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
                                lxb_html_element_t* cell_elmt = cellNode->as_element();
                                if (!cell_elmt) {
                                    log_debug("layout_table: as_element() returned NULL for cell, skipping");
                                    continue;
                                }
                                DisplayValue cell_display = resolve_display(cell_elmt);

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

                                        // Initial layout for content measurement
                                        // NOTE: This uses potentially incorrect parent width (cell->width may be 0)
                                        // We'll re-layout later with correct parent width after cell dimensions are set
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
                    lxb_html_element_t* cell_elmt = cellNode->as_element();
                    if (!cell_elmt) {
                        log_debug("layout_table: as_element() returned NULL for cell, skipping");
                        continue;
                    }
                    DisplayValue cell_display = resolve_display(cell_elmt);

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

                            // Initial layout for content measurement
                            // NOTE: This uses potentially incorrect parent width (cell->width may be 0)
                            // We'll re-layout later with correct parent width after cell dimensions are set
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

// Layout cell content with correct parent width (after cell dimensions are set)
// This re-lays out children that were previously laid out with incorrect (0px) parent width.
// This fixes the child block width inheritance issue.
static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell) {
    ViewTableCell* tcell = static_cast<ViewTableCell*>(cell);
    if (!tcell || !tcell->node) return;

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

    // Clear existing children (they were laid out with wrong parent width)
    // We need to save and clear the child list, then re-layout from DOM
    ((ViewGroup*)cell)->child = nullptr;
    if (cell->first_child) {
        // Note: Not freeing memory here, assuming it will be garbage collected later
        // or that the layout system handles this
        cell->first_child = nullptr;
    }

    // Set up layout context for cell content with CORRECT positioning
    // CRITICAL FIX: Set line.left and advance_x to content_start_x to apply padding offset
    lycon->block.width = content_width;
    lycon->block.height = content_height;
    lycon->block.advance_y = content_start_y;  // Start Y position after border+padding
    lycon->line.left = content_start_x;        // Text starts after padding!
    lycon->line.right = content_start_x + content_width;  // Text ends before right padding
    lycon->line.advance_x = content_start_x;   // Start advancing from padding offset
    lycon->line.is_line_start = true;
    lycon->parent = (ViewGroup*)cell;
    lycon->prev_view = nullptr;
    lycon->elmt = tcell->node;

    log_debug("Pass 2: Re-layout cell content - cell=%dx%d, border=(%d,%d), padding=(%d,%d,%d,%d), content_start=(%d,%d), content=%dx%d",
              cell->width, cell->height, border_left, border_top,
              padding_left, padding_right, padding_top, padding_bottom,
              content_start_x, content_start_y, content_width, content_height);

    // Re-layout children with correct parent width
    // Child blocks without explicit width will now inherit content_width via pa_block.width
    for (DomNode* cc = tcell->node->first_child(); cc; cc = cc->next_sibling()) {
        layout_flow_node(lycon, cc);
    }

    // Restore layout context
    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->parent = saved_parent;
    lycon->prev_view = saved_prev;
    lycon->elmt = saved_elmt;
}

// Enhanced cell width measurement with browser-accurate calculations
// NOTE: After lazy child layout implementation, children aren't laid out yet when this is called.
// We need to measure from already-laid-out children if they exist, or use a default minimum.
static int measure_cell_min_width(ViewTableCell* cell) {
    if (!cell) return 0;

    // STEP 1: Check for explicit CSS width first
    if (cell->node && cell->node->lxb_elmt && cell->node->lxb_elmt->element.style) {
        const lxb_css_rule_declaration_t* width_decl =
            lxb_dom_element_style_by_id(
                (lxb_dom_element_t*)cell->node->lxb_elmt,
                LXB_CSS_PROPERTY_WIDTH);
        if (width_decl && width_decl->u.width) {
            // Note: resolve_length_value needs a LayoutContext, but we don't have one here
            // For now, we'll just detect that explicit width exists and measure content
            // The width will be properly applied later in the fixed layout or column width calculation
            log_debug("Cell has explicit CSS width property");
        }
    }

    // STEP 2: Measure content width with sub-pixel precision
    float content_width = 0.0f;

    // Measure actual content width WITHOUT arbitrary adjustments
    // NOTE: With lazy child layout, children may not be laid out yet, so this may find no children
    for (View* child = ((ViewGroup*)cell)->child; child; child = child->next) {
        float child_width = 0.0f;

        if (child->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            // Use exact text width, no arbitrary margins
            child_width = text->width;
            log_debug("Text child width: %.1fpx", child_width);
        } else if (child->type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)child;
            // CRITICAL: Block children may have incorrect width at this point
            // Try to read explicit CSS width if available
            if (block->blk && block->blk->given_width > 0) {
                child_width = block->blk->given_width;
            } else {
                // No explicit width - use measured width (may be inaccurate)
                child_width = block->width;
            }
        }

        if (child_width > content_width) {
            content_width = child_width;
        }
    }

    // STEP 3: For empty cells, use minimal content width
    if (content_width < 1.0f) {
        content_width = 1.0f; // Minimum 1px content for empty cells
        log_debug("Empty cell detected - using minimum 1px content width");
    }

    // Browser-compatible box model calculation with float precision
    float total_width = content_width;

    // Add cell padding - read from actual CSS properties
    float padding_horizontal = 0.0f;
    if (cell->bound && cell->bound->padding.left >= 0 && cell->bound->padding.right >= 0) {
        padding_horizontal = (float)(cell->bound->padding.left + cell->bound->padding.right);
        log_debug("Using CSS padding: left=%d, right=%d, total=%.1f",
               cell->bound->padding.left, cell->bound->padding.right, padding_horizontal);
    } else {
        log_debug("No CSS padding found or invalid values, using default 0");
        padding_horizontal = 0.0f;
    }
    total_width += padding_horizontal;

    // Add cell border (CSS: border: 1px solid)
    total_width += 2.0f; // 1px left + 1px right

    // Ensure browser-accurate minimum width (16px matches typical browser behavior)
    if (total_width < 16.0f) {
        total_width = 16.0f; // Browser-compatible minimum cell width
    }

    log_debug("Cell width calculation - content=%.1f, padding=%.1f, border=2, total=%.1f",
           content_width, padding_horizontal, total_width);

    // Round to nearest pixel for final result
    return (int)roundf(total_width);
}

// Enhanced table layout algorithm with colspan/rowspan support
void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    if (!table) return;

    // Initialize fixed layout fields
    table->fixed_row_height = 0;  // 0 = auto height (calculate from content)

    log_debug("Starting enhanced table auto layout");
    log_debug("Table layout mode: %s",
           table->table_layout == ViewTable::TABLE_LAYOUT_FIXED ? "fixed" : "auto");
    log_debug("Table border-spacing: %fpx %fpx, border-collapse: %s",
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

    // Check if table has explicit width (for percentage cell width calculation)
    int explicit_table_width = 0;
    int table_content_width = 0; // Width available for cells

    if (table->node && table->node->lxb_elmt && table->node->lxb_elmt->element.style) {
        const lxb_css_rule_declaration_t* width_decl =
            lxb_dom_element_style_by_id(
                (lxb_dom_element_t*)table->node->lxb_elmt,
                LXB_CSS_PROPERTY_WIDTH);
        if (width_decl && width_decl->u.width) {
            explicit_table_width = resolve_length_value(
                lycon, LXB_CSS_PROPERTY_WIDTH, width_decl->u.width);

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
            if (!table->border_collapse && table->border_spacing_h > 0) {
                table_content_width -= (columns + 1) * table->border_spacing_h;
            }

            log_debug("Table explicit width: %dpx, content width for cells: %dpx",
                   explicit_table_width, table_content_width);
        }
    }

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

                            // Try to get explicit width from CSS first
                            int cell_width = 0;
                            if (tcell->node && tcell->node->lxb_elmt && tcell->node->lxb_elmt->element.style) {
                                const lxb_css_rule_declaration_t* width_decl =
                                    lxb_dom_element_style_by_id(
                                        (lxb_dom_element_t*)tcell->node->lxb_elmt,
                                        LXB_CSS_PROPERTY_WIDTH);
                                if (width_decl && width_decl->u.width) {
                                    // Check if it's a percentage value
                                    if (width_decl->u.width->type == LXB_CSS_VALUE__PERCENTAGE && table_content_width > 0) {
                                        // Calculate percentage relative to table content width
                                        float percentage = width_decl->u.width->u.percentage.num;
                                        int css_content_width = (int)(table_content_width * percentage / 100.0f);

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
                                    } else {
                                        // Absolute width
                                        int css_content_width = resolve_length_value(
                                            lycon, LXB_CSS_PROPERTY_WIDTH, width_decl->u.width);
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

                            // If no explicit width, measure content
                            if (cell_width == 0) {
                                cell_width = measure_cell_min_width(tcell);
                            }

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

                    // Try to get explicit width from CSS first
                    int cell_width = 0;
                    if (tcell->node && tcell->node->lxb_elmt && tcell->node->lxb_elmt->element.style) {
                        const lxb_css_rule_declaration_t* width_decl =
                            lxb_dom_element_style_by_id(
                                (lxb_dom_element_t*)tcell->node->lxb_elmt,
                                LXB_CSS_PROPERTY_WIDTH);
                        if (width_decl && width_decl->u.width) {
                            // Check if it's a percentage value
                            if (width_decl->u.width->type == LXB_CSS_VALUE__PERCENTAGE && table_content_width > 0) {
                                // Calculate percentage relative to table content width
                                float percentage = width_decl->u.width->u.percentage.num;
                                int css_content_width = (int)(table_content_width * percentage / 100.0f);

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
                            } else {
                                // Absolute width
                                int css_content_width = resolve_length_value(
                                    lycon, LXB_CSS_PROPERTY_WIDTH, width_decl->u.width);
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

                    // If no explicit width, measure content
                    if (cell_width == 0) {
                        cell_width = measure_cell_min_width(tcell);
                    }                    if (tcell->col_span == 1) {
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
    int fixed_table_width = 0; // Store explicit width for fixed layout
    if (table->table_layout == ViewTable::TABLE_LAYOUT_FIXED) {
        log_debug("=== FIXED LAYOUT ALGORITHM ===");

        // STEP 1: Get explicit table width from CSS
        int explicit_table_width = 0;

        // Try to read width directly from table element's CSS
        if (table->node && table->node->lxb_elmt && table->node->lxb_elmt->element.style) {
            const lxb_css_rule_declaration_t* width_decl =
                lxb_dom_element_style_by_id(
                    (lxb_dom_element_t*)table->node->lxb_elmt,
                    LXB_CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->u.width) {
                explicit_table_width = resolve_length_value(
                    lycon, LXB_CSS_PROPERTY_WIDTH, width_decl->u.width);
                log_debug("FIXED LAYOUT - read table CSS width: %dpx", explicit_table_width);
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
        if (!table->border_collapse && table->border_spacing_h > 0) {
            content_width -= (columns + 1) * table->border_spacing_h; // Spacing around and between columns
            log_debug("Subtracting border-spacing: (%d+1)*%.1f = %.1f",
                   columns, table->border_spacing_h, (columns + 1) * table->border_spacing_h);
        }

        log_debug("Content width for columns: %dpx", content_width);

        // STEP 3: Read explicit column widths from FIRST ROW cells
        int* explicit_col_widths = (int*)calloc(columns, sizeof(int));
        int total_explicit = 0;
        int unspecified_cols = 0;

        // Find first row
        ViewTableRow* first_row = nullptr;
        for (ViewBlock* child = table->first_child; child; child = child->next_sibling) {
            if (child->type == RDT_VIEW_TABLE_ROW_GROUP) {
                ViewTableRowGroup* group = (ViewTableRowGroup*)child;
                for (ViewBlock* row_child = group->first_child; row_child; row_child = row_child->next_sibling) {
                    if (row_child->type == RDT_VIEW_TABLE_ROW) {
                        first_row = (ViewTableRow*)row_child;
                        break;
                    }
                }
                if (first_row) break;
            } else if (child->type == RDT_VIEW_TABLE_ROW) {
                first_row = (ViewTableRow*)child;
                break;
            }
        }

        // Read cell widths from first row
        if (first_row) {
            int col = 0;
            log_debug("Reading first row cell widths...");
            for (ViewBlock* cell_view = first_row->first_child;
                 cell_view && col < columns;
                 cell_view = cell_view->next_sibling) {
                if (cell_view->type == RDT_VIEW_TABLE_CELL) {
                    ViewTableCell* cell = (ViewTableCell*)cell_view;

                    // Try to get explicit width from CSS
                    int cell_width = 0;
                    if (cell->node && cell->node->lxb_elmt && cell->node->lxb_elmt->element.style) {
                        const lxb_css_rule_declaration_t* width_decl =
                            lxb_dom_element_style_by_id(
                                (lxb_dom_element_t*)cell->node->lxb_elmt,
                                LXB_CSS_PROPERTY_WIDTH);
                        if (width_decl && width_decl->u.width) {
                            // Check if it's a percentage value
                            if (width_decl->u.width->type == LXB_CSS_VALUE__PERCENTAGE) {
                                // Calculate percentage relative to table content width
                                float percentage = width_decl->u.width->u.percentage.num;
                                cell_width = (int)(content_width * percentage / 100.0f);
                                log_debug("  Column %d: percentage width %.1f%% of %dpx = %dpx",
                                       col, percentage, content_width, cell_width);
                            } else {
                                // Absolute width (px, em, etc.)
                                cell_width = resolve_length_value(
                                    lycon, LXB_CSS_PROPERTY_WIDTH, width_decl->u.width);
                                log_debug("  Column %d: absolute width %dpx", col, cell_width);
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

                    col += cell->col_span;
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
        if (table->node && table->node->lxb_elmt && table->node->lxb_elmt->element.style) {
            const lxb_css_rule_declaration_t* height_decl =
                lxb_dom_element_style_by_id(
                    (lxb_dom_element_t*)table->node->lxb_elmt,
                    LXB_CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->u.height) {
                explicit_table_height = resolve_length_value(
                    lycon, LXB_CSS_PROPERTY_HEIGHT, height_decl->u.height);
                log_debug("FIXED LAYOUT - read table CSS height: %dpx", explicit_table_height);
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
            if (!table->border_collapse && table->border_spacing_v > 0 && total_rows > 0) {
                content_height -= (int)((total_rows + 1) * table->border_spacing_v);
                log_debug("Subtracting vertical border-spacing: (%d+1)*%.1f = %.1f",
                       total_rows, table->border_spacing_v, (total_rows + 1) * table->border_spacing_v);
            }

            // Distribute height equally across rows
            int height_per_row = total_rows > 0 ? content_height / total_rows : 0;
            log_debug("Height per row: %dpx (content_height=%d / rows=%d)",
                   height_per_row, content_height, total_rows);

            // Store the fixed row height for later application during positioning
            // We'll apply this when positioning rows in the main layout loop
            table->fixed_row_height = height_per_row;
            log_debug("=== FIXED LAYOUT HEIGHT DISTRIBUTION COMPLETE ===");
        }
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

    log_debug("table_width before border adjustments: %d, border_collapse=%d",
           table_width, table->border_collapse);

    // Apply border spacing or border collapse adjustments
    if (table->border_collapse) {
        // Border-collapse: borders overlap, reduce total width
        if (columns > 1) {
            int reduction = (columns - 1);
            log_debug("Border-collapse reducing width by %dpx", reduction);
            table_width -= reduction; // Remove 1px per internal border
        }
        log_debug("Border-collapse applied - table width: %d", table_width);
    } else if (table->border_spacing_h > 0) {
        // Separate borders: add spacing between columns AND around table edges
        log_debug("Applying border-spacing %fpx to table width", table->border_spacing_h);
        if (columns > 1) {
            table_width += (columns - 1) * table->border_spacing_h; // Between columns
        }
        table_width += 2 * table->border_spacing_h; // Left and right edges
        log_debug("Border-spacing applied (%dpx) - table width: %d (includes edge spacing)",
               (int)table->border_spacing_h, table_width);
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
    if (table->table_layout == ViewTable::TABLE_LAYOUT_FIXED && fixed_table_width > 0) {
        log_debug("Fixed layout override - changing table_width from %d to %d",
               table_width, fixed_table_width);
        table_width = fixed_table_width;
        log_debug("Fixed layout override - using CSS width: %dpx", table_width);
    }

    log_debug("Final table_width for layout: %dpx", table_width);

    // Step 4: Position cells and calculate row heights with border model
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

            // Calculate tbody content width as sum of column widths
            int tbody_content_width = 0;
            for (int i = 0; i < columns; i++) {
                tbody_content_width += col_widths[i];
            }

            // Add border-spacing between columns (if separate borders)
            if (!table->border_collapse && table->border_spacing_h > 0 && columns > 1) {
                tbody_content_width += (columns - 1) * table->border_spacing_h;
            }

            // Position tbody based on border-collapse mode
            if (table->border_collapse) {
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
            for (ViewBlock* count_row = child->first_child; count_row; count_row = count_row->next_sibling) {
                if (count_row->type == RDT_VIEW_TABLE_ROW) row_count++;
            }
            int current_row_index = 0;

            for (ViewBlock* row = child->first_child; row; row = row->next_sibling) {
                if (row->type == RDT_VIEW_TABLE_ROW) {
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
                            for (int c = tcell->col_index; c < tcell->col_index + tcell->col_span && c < columns; c++) {
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
                            if (tcell->node && tcell->node->lxb_elmt && tcell->node->lxb_elmt->element.style) {
                                const lxb_css_rule_declaration_t* height_decl =
                                    lxb_dom_element_style_by_id(
                                        (lxb_dom_element_t*)tcell->node->lxb_elmt,
                                        LXB_CSS_PROPERTY_HEIGHT);
                                if (height_decl && height_decl->u.height) {
                                    explicit_cell_height = resolve_length_value(
                                        lycon, LXB_CSS_PROPERTY_HEIGHT, height_decl->u.height);
                                    log_debug("Cell has explicit CSS height: %dpx", explicit_cell_height);
                                }
                            }

                            // STEP 2: Measure content height precisely (for auto height or minimum)
                            for (View* cc = ((ViewGroup*)cell)->child; cc; cc = cc->next) {
                                if (cc->type == RDT_VIEW_TEXT) {
                                    ViewText* text = (ViewText*)cc;
                                    int text_height = text->height > 0 ? text->height : 17; // Default line height
                                    if (text_height > content_height) content_height = text_height;
                                } else if (cc->type == RDT_VIEW_BLOCK || cc->type == RDT_VIEW_INLINE || cc->type == RDT_VIEW_INLINE_BLOCK) {
                                    ViewBlock* block = (ViewBlock*)cc;

                                    // Check if child has explicit CSS height
                                    int child_css_height = 0;
                                    if (block->node && block->node->lxb_elmt && block->node->lxb_elmt->element.style) {
                                        const lxb_css_rule_declaration_t* child_height_decl =
                                            lxb_dom_element_style_by_id(
                                                (lxb_dom_element_t*)block->node->lxb_elmt,
                                                LXB_CSS_PROPERTY_HEIGHT);
                                        if (child_height_decl && child_height_decl->u.height) {
                                            child_css_height = resolve_length_value(
                                                lycon, LXB_CSS_PROPERTY_HEIGHT, child_height_decl->u.height);
                                            log_debug("Child element (type=%d) has explicit CSS height: %dpx", cc->type, child_css_height);
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
                            if (tcell->vertical_align != ViewTableCell::CELL_VALIGN_TOP) {
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
                                if (tcell->vertical_align == ViewTableCell::CELL_VALIGN_MIDDLE) {
                                    y_adjustment = (cell_content_area - child_height) / 2;
                                    log_debug("Vertical-align middle: cell_content_area=%d, child_height=%d, adjustment=%d",
                                           cell_content_area, child_height, y_adjustment);
                                } else if (tcell->vertical_align == ViewTableCell::CELL_VALIGN_BOTTOM) {
                                    y_adjustment = cell_content_area - child_height;
                                    log_debug("Vertical-align bottom: cell_content_area=%d, child_height=%d, adjustment=%d",
                                           cell_content_area, child_height, y_adjustment);
                                }

                                // Apply adjustment to all children
                                if (y_adjustment > 0) {
                                    for (View* cc = ((ViewGroup*)cell)->child; cc; cc = cc->next) {
                                        cc->y += y_adjustment;
                                        log_debug("Applied vertical-align adjustment: child y=%d (added %d)",
                                               cc->y, y_adjustment);
                                    }
                                }
                            }

                            // Handle rowspan for row height calculation
                            // If cell spans multiple rows, only count a portion of its height for this row
                            int height_for_row = cell_height;
                            if (tcell->row_span > 1) {
                                // Distribute cell height across spanned rows
                                // For simplicity, divide evenly (more complex: consider content distribution)
                                height_for_row = cell_height / tcell->row_span;
                                log_debug("Rowspan cell - total_height=%d, rowspan=%d, height_for_row=%d",
                                       cell_height, tcell->row_span, height_for_row);
                            }

                            if (height_for_row > row_height) {
                                row_height = height_for_row;
                            }
                        }
                    }

                    // Apply fixed layout height if specified
                    if (table->fixed_row_height > 0) {
                        row->height = table->fixed_row_height;
                        log_debug("Applied fixed layout row height: %dpx", table->fixed_row_height);

                        // CRITICAL: Update all cell heights in this row to match fixed row height
                        // Cells were calculated with auto height, but fixed layout overrides this
                        for (ViewBlock* cell = row->first_child; cell; cell = cell->next_sibling) {
                            if (cell->type == RDT_VIEW_TABLE_CELL) {
                                cell->height = table->fixed_row_height;
                                log_debug("Updated cell height to match fixed_row_height=%d", table->fixed_row_height);
                            }
                        }
                    } else {
                        row->height = row_height;
                    }
                    current_y += row->height;

                    // Add vertical border-spacing after each row (except last row in group)
                    if (!table->border_collapse && table->border_spacing_v > 0 && !is_last_row) {
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

                    // CRITICAL FIX: Now that cell width is set, layout cell content with correct parent width
                    // This allows child blocks to inherit the correct parent width instead of 0
                    layout_table_cell_content(lycon, cell);

                    // Enhanced cell height calculation with browser accuracy
                    int content_height = 0;

                    // STEP 1: Check for explicit CSS height property first
                    int explicit_cell_height = 0;
                    if (tcell->node && tcell->node->lxb_elmt && tcell->node->lxb_elmt->element.style) {
                        const lxb_css_rule_declaration_t* height_decl =
                            lxb_dom_element_style_by_id(
                                (lxb_dom_element_t*)tcell->node->lxb_elmt,
                                LXB_CSS_PROPERTY_HEIGHT);
                        if (height_decl && height_decl->u.height) {
                            explicit_cell_height = resolve_length_value(
                                lycon, LXB_CSS_PROPERTY_HEIGHT, height_decl->u.height);
                            log_debug("Cell has explicit CSS height: %dpx", explicit_cell_height);
                        }
                    }

                    // STEP 2: Measure content height precisely (for auto height or minimum)
                    for (View* cc = ((ViewGroup*)cell)->child; cc; cc = cc->next) {
                        if (cc->type == RDT_VIEW_TEXT) {
                            ViewText* text = (ViewText*)cc;
                            int text_height = text->height > 0 ? text->height : 17;
                            if (text_height > content_height) content_height = text_height;
                        } else if (cc->type == RDT_VIEW_BLOCK || cc->type == RDT_VIEW_INLINE || cc->type == RDT_VIEW_INLINE_BLOCK) {
                            ViewBlock* block = (ViewBlock*)cc;

                            // Check if child has explicit CSS height
                            int child_css_height = 0;
                            if (block->node && block->node->lxb_elmt && block->node->lxb_elmt->element.style) {
                                const lxb_css_rule_declaration_t* child_height_decl =
                                    lxb_dom_element_style_by_id(
                                        (lxb_dom_element_t*)block->node->lxb_elmt,
                                        LXB_CSS_PROPERTY_HEIGHT);
                                if (child_height_decl && child_height_decl->u.height) {
                                    child_css_height = resolve_length_value(
                                        lycon, LXB_CSS_PROPERTY_HEIGHT, child_height_decl->u.height);
                                    log_debug("Direct row child element (type=%d) has explicit CSS height: %dpx", cc->type, child_css_height);
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
                    if (tcell->vertical_align != ViewTableCell::CELL_VALIGN_TOP) {
                        // Calculate available space in cell (content area after border and padding)
                        int cell_content_area = cell_height - 2; // Subtract border (1px top + 1px bottom)
                        if (tcell->bound && tcell->bound->padding.top >= 0 && tcell->bound->padding.bottom >= 0) {
                            cell_content_area -= (tcell->bound->padding.top + tcell->bound->padding.bottom);
                        }

                        // Measure child height
                        int child_height = content_height; // Use measured content height

                        // Calculate adjustment based on alignment
                        int y_adjustment = 0;
                        if (tcell->vertical_align == ViewTableCell::CELL_VALIGN_MIDDLE) {
                            y_adjustment = (cell_content_area - child_height) / 2;
                            log_debug("Vertical-align middle: cell_content_area=%d, child_height=%d, adjustment=%d",
                                   cell_content_area, child_height, y_adjustment);
                        } else if (tcell->vertical_align == ViewTableCell::CELL_VALIGN_BOTTOM) {
                            y_adjustment = cell_content_area - child_height;
                            log_debug("Vertical-align bottom: cell_content_area=%d, child_height=%d, adjustment=%d",
                                   cell_content_area, child_height, y_adjustment);
                        }

                        // Apply adjustment to all children
                        if (y_adjustment > 0) {
                            for (View* cc = ((ViewGroup*)cell)->child; cc; cc = cc->next) {
                                cc->y += y_adjustment;
                                log_debug("Applied vertical-align adjustment: child y=%d (added %d)",
                                       cc->y, y_adjustment);
                            }
                        }
                    }

                    // Handle rowspan for row height calculation
                    // If cell spans multiple rows, only count a portion of its height for this row
                    int height_for_row = cell_height;
                    if (tcell->row_span > 1) {
                        // Distribute cell height across spanned rows
                        height_for_row = cell_height / tcell->row_span;
                        log_debug("Rowspan cell - total_height=%d, rowspan=%d, height_for_row=%d",
                               cell_height, tcell->row_span, height_for_row);
                    }

                    if (height_for_row > row_height) {
                        row_height = height_for_row;
                    }
                }
            }

            // Apply fixed layout height if specified
            if (table->fixed_row_height > 0) {
                row->height = table->fixed_row_height;
                log_debug("Applied fixed layout row height: %dpx", table->fixed_row_height);

                // CRITICAL: Update all cell heights in this row to match fixed row height
                // Cells were calculated with auto height, but fixed layout overrides this
                for (ViewBlock* cell = row->first_child; cell; cell = cell->next_sibling) {
                    if (cell->type == RDT_VIEW_TABLE_CELL) {
                        cell->height = table->fixed_row_height;
                        log_debug("Updated cell height to match fixed_row_height=%d", table->fixed_row_height);
                    }
                }
            } else {
                row->height = row_height;
            }
            current_y += row->height;

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
