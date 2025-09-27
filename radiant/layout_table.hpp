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

#endif // LAYOUT_TABLE_HPP
