#pragma once
#include "typeset.hpp"
#include "typeset_marks.hpp"
#include "view_tree_css.hpp"

struct PaintList;
struct RadiantSourceOrigin;
struct LayoutContext;
struct TableMetadata;
// native list geometry refines the shared table tracks before cell layout.
float paged_label_body_columns(LayoutContext* lycon, ViewTable* table, TableMetadata* metadata, float width);
size_t paged_flow_node_limit(LayoutContext* lycon);
bool paged_table_source_admit(LayoutContext* lycon, ViewElement* source);
void paged_flow_layout_error(LayoutContext* lycon, DomElement* source, TypesetStatus status, const char* reason);
struct PagedNativeBreakCandidate {
    TypesetSource source;
    TypesetResume resume;
    size_t first, next;
    uint32_t page_number, column;
    float used, available, height;
    TypesetBreak boundary;
};
// callbacks are replayable: material returns OK per part, then DONE; rectangles and paint use the selected line's origin.
typedef TypesetStatus (*PagedNativeMaterialFn)(void* context, const TypesetContribution* contribution,
    const TypesetLineCandidate* line, size_t index, ViewNativeMaterial* material, RdtLogicalRect* rect);
typedef TypesetStatus (*PagedNativeBreakFn)(void* context, const PagedNativeBreakCandidate* candidate,
    TypesetBreak* boundary);
struct PagedNativeFlowBinding {
    DomElement* source; // an empty r:flow/static-content; null registers a nested-only provider
    const TypesetFlowProvider* provider;
    TypesetResume start;
    ViewNativeOwner owner; // retains immutable glyph/image/exact/solution payloads with published editions (D4.5.1v4)
    void* context;
    PagedNativeMaterialFn material;
    PagedNativeBreakFn break_candidate; // pure preference/legality; shared flow still owns page commitment
    const TypesetTarget* targets; // declare names before enumeration so body references can resolve forward
    size_t target_count;
};
struct PagedLayoutOptions {
    uint32_t max_pages, max_depth;
    size_t max_nodes;
    uint32_t max_reference_passes;
    uint32_t max_container_passes;
    bool right_binding;
    ViewPageSide first_side;
    const PagedNativeFlowBinding* native_flows;
    size_t native_flow_count;
};
struct PagedLayoutDiagnostic {
    TypesetStatus status;
    DomNodeRef source;
    uint32_t page_number;
    const char* reason;
    uint32_t relaxed_line_minima, relaxed_avoidance;
    uint32_t reference_passes;
    uint32_t container_passes;
    const RadiantSourceOrigin* origin;
    TypesetSource native_source;
};
PagedLayoutOptions paged_layout_options_default();
TypesetStatus layout_secondary_view(ViewTree* tree, const PagedLayoutOptions* options,
                                     PagedLayoutDiagnostic* diagnostic);
void paged_composition_destroy(ViewTree* tree);
const TypesetTarget* layout_secondary_target(ViewTree* tree, const char* id);
const TypesetMark* layout_secondary_mark(ViewTree* tree, TypesetMarkKind kind, const char* name,
    uint32_t page_number, TypesetMarkSelection selection);
// one ordered fragment traversal serves native painting and schema-backed export.
struct SecondaryViewVisitor {
    void* context;
    bool (*fragment)(void*, const LayoutViewNode*, bool entering);
    bool (*boundary)(void*, const BoundaryProp*, RdtLogicalRect);
    bool (*fill)(void*, Color, RdtLogicalRect);
    bool (*clip)(void*, const RdtLogicalRect*, const float* inset); // null rect closes the scope
    bool (*glyph)(void*, const LayoutViewNode*, const PaintGlyphRun*);
    bool (*image)(void*, const LayoutViewNode*, const PaintImageBox*);
    bool (*paint)(void*, const PaintList*);
    bool pdf;
};
bool layout_secondary_visit_page(ViewTree* tree, const ViewPageBox* page,
    const SecondaryViewVisitor* visitor);
bool layout_secondary_paint_page(ViewTree* tree, const ViewPageBox* page, PaintList* paint, bool pdf = false);
bool layout_secondary_paint_root(ViewTree* tree, PaintList* paint, const RdtLogicalRect* clip = nullptr);
