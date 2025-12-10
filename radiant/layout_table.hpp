#ifndef LAYOUT_TABLE_HPP
#define LAYOUT_TABLE_HPP

#include "layout.hpp"

// Phase 1 entry: layout a table formatting context root (table/inline-table)
void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);

// Internal helpers (Phase 1 scaffolding)
// Build the logical table structure under a ViewTable and identify caption/row groups
struct ViewTable* build_table_tree(LayoutContext* lycon, DomNode* elmt);

// Placeholder for Phase 1 column/row sizing
void table_auto_layout(LayoutContext* lycon, struct ViewTable* table);

// Phase 3: Table layout algorithm implementations
void table_auto_layout_algorithm(LayoutContext* lycon, struct ViewTable* table, int columns, int* col_pref, int* col_widths, long long sum_pref, int avail_width);
void table_fixed_layout_algorithm(LayoutContext* lycon, struct ViewTable* table, int columns, int* col_widths, int avail_width);

// Text positioning adjustment helpers (called after cell positions are calculated)
void adjust_table_text_positions_final(struct ViewTable* table);
void adjust_row_text_positions_final(struct ViewTable* table, struct ViewBlock* row, int table_abs_x, int cell_border, int cell_padding);
void adjust_cell_text_positions_final(struct ViewBlock* cell, int text_abs_x);

#endif // LAYOUT_TABLE_HPP
