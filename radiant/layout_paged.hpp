#pragma once
#include "typeset.hpp"
#include "view_tree_css.hpp"

struct PaintList;
struct PagedLayoutOptions {
    uint32_t max_pages, max_depth;
    size_t max_nodes, max_items;
    uint32_t max_reference_passes;
    bool right_binding;
    ViewPageSide first_side;
};
struct PagedLayoutDiagnostic {
    TypesetStatus status;
    DomNodeRef source;
    uint32_t page_number;
    const char* reason;
    uint32_t relaxed_line_minima, relaxed_avoidance;
    uint32_t reference_passes;
};
PagedLayoutOptions paged_layout_options_default();
TypesetStatus layout_secondary_view(ViewTree* tree, const PagedLayoutOptions* options,
                                     PagedLayoutDiagnostic* diagnostic);
void paged_composition_destroy(ViewTree* tree);
const TypesetTarget* layout_secondary_target(ViewTree* tree, const char* id);
bool layout_secondary_paint_page(ViewTree* tree, const ViewPageBox* page, PaintList* paint);
bool layout_secondary_paint_root(ViewTree* tree, PaintList* paint);
