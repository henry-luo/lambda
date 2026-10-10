#pragma once
#include "typeset.hpp"
#include "typeset_marks.hpp"
#include "view_tree_css.hpp"

struct PaintList;
struct RadiantSourceOrigin;
struct TypesetRegionPlacement;
// bindings and policy are borrowed for layout; immutable material owners move with the retained generation.
struct PagedNativeFlowBinding {
    DomNodeRef control; // absent for a registered nested-only provider
    TypesetFlowProvider provider;
    TypesetResume start;
    ViewNativeOwner owner;
    void* context;
    // fill paint/owner and box source intervals; the compositor preserves provider source/metrics and item intervals.
    TypesetStatus (*material)(void* context, const TypesetContribution* contribution,
        const TypesetItem* item, const TypesetLineCandidate* alternative, ViewNativeMaterial* material);
    const TypesetTarget* targets; // declared names seed forward references; target contributions place their anchors
    size_t target_count;
    // enumerate selected region paint in region-local coordinates; TYPESET_DONE ends the owned slice.
    TypesetStatus (*region_item)(void* context, const TypesetRegionPlacement* placement, size_t index,
        ViewNativeMaterial* material, RdtLogicalRect* rect);
};
struct PagedLayoutOptions {
    uint32_t max_pages, max_depth;
    size_t max_nodes, max_items;
    size_t max_block_trials;
    uint32_t max_reference_passes;
    bool right_binding;
    ViewPageSide first_side;
    const PagedNativeFlowBinding* native_flows;
    size_t native_flow_count;
    const TypesetPagePolicy* page_policy;
    size_t max_page_transitions;
};
struct PagedLayoutDiagnostic {
    TypesetStatus status;
    DomNodeRef source;
    uint32_t page_number;
    const char* reason;
    uint32_t relaxed_line_minima, relaxed_avoidance;
    uint32_t reference_passes;
    const RadiantSourceOrigin* origin;
};
PagedLayoutOptions paged_layout_options_default();
TypesetStatus layout_secondary_view(ViewTree* tree, const PagedLayoutOptions* options,
                                     PagedLayoutDiagnostic* diagnostic);
void paged_composition_destroy(ViewTree* tree);
const TypesetTarget* layout_secondary_target(ViewTree* tree, const char* id);
const TypesetMark* layout_secondary_mark(ViewTree* tree, TypesetMarkKind kind, const char* name,
    uint32_t page_number, TypesetMarkSelection selection);
bool layout_secondary_paint_page(ViewTree* tree, const ViewPageBox* page, PaintList* paint);
bool layout_secondary_paint_root(ViewTree* tree, PaintList* paint, const RdtLogicalRect* clip = nullptr);
