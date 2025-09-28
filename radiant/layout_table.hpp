#ifndef LAYOUT_TABLE_HPP
#define LAYOUT_TABLE_HPP

#include "layout.hpp"

// Phase 1 entry: layout a table formatting context root (table/inline-table)
void layout_table_box(LayoutContext* lycon, DomNode* elmt, DisplayValue display);

// Internal helpers (Phase 1 scaffolding)
// Build the logical table structure under a ViewTable and identify caption/row groups
struct ViewTable* build_table_tree(LayoutContext* lycon, DomNode* elmt);

// Placeholder for Phase 1 column/row sizing
void table_auto_layout(LayoutContext* lycon, struct ViewTable* table);

// Phase 3: Table layout algorithm implementations
void table_auto_layout_algorithm(LayoutContext* lycon, struct ViewTable* table, int columns, int* col_pref, int* col_widths, long long sum_pref, int avail_width);
void table_fixed_layout_algorithm(LayoutContext* lycon, struct ViewTable* table, int columns, int* col_widths, int avail_width);

#endif // LAYOUT_TABLE_HPP
