#pragma once

#include "scale.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/css_paged_media.hpp"
#include "../lib/ownership.hpp"
#include <stddef.h>
#include <stdint.h>

struct DomDocument;
struct DomNode;
struct ViewTree;
struct FontProp;
struct InlineProp;
struct BoundaryProp;
struct BlockProp;
struct StyleTree;
struct ViewCssStyle;
struct ViewCssContext;
struct PagedComposition;
struct ViewPageStyle;
struct PaintGlyphRun;
struct PaintImageBox;
struct ViewModelCheckpoint;
struct ViewPageGeneration;
struct RadiantPageSequence;
struct RadiantFixedPage;
struct Arena;
struct hashmap;

enum ViewPresentation : uint8_t {
    VIEW_PRESENTATION_CONTINUOUS,
    VIEW_PRESENTATION_PAGED,
};

struct ViewEnvironment {
    ViewPresentation presentation;
    bool print_media;
    float viewport_width, viewport_height;
    float device_scale;
    float page_width, page_height;
    uint64_t resource_generation;
    double sampled_time;
};

enum LayoutViewKind : uint8_t {
    LAYOUT_VIEW_ROOT,
    LAYOUT_VIEW_PAGE,
    LAYOUT_VIEW_FRAGMENT,
    LAYOUT_VIEW_PAGE_INSTANCE,
};

struct LayoutViewRef {
    uint64_t tree_id;
    uint32_t generation;
    uint32_t node_id;
};

struct ViewNodeState;
enum ViewFragmentRole : uint8_t {
    VIEW_FRAGMENT_BODY, VIEW_FRAGMENT_MARGIN, VIEW_FRAGMENT_NOTE, VIEW_FRAGMENT_FLOAT,
    VIEW_FRAGMENT_RUNNING, VIEW_FRAGMENT_REPEATED_TABLE, VIEW_FRAGMENT_STATIC,
};
struct ViewTableRange { size_t column, span; bool missing; };
struct LayoutViewNode {
    LayoutViewRef ref;
    LayoutViewKind kind;
    DomNodeRef source;
    RdtLogicalRect rect;
    lam::Up<ViewNodeState> state;
    lam::Up<LayoutViewNode> parent;
    lam::Up<LayoutViewNode> first_child;
    lam::Up<LayoutViewNode> last_child;
    lam::Up<LayoutViewNode> next_sibling;
    lam::Up<LayoutViewNode> next_occurrence;
    size_t text_start, text_length;
    lam::Up<PaintGlyphRun> glyph_run;
    lam::Up<PaintImageBox> image_box;
    lam::Up<ViewCssStyle> computed_style;
    lam::Up<BoundaryProp> computed_boundary;
    lam::Up<const ViewTableRange> table_range;
    ViewFragmentRole role;
    bool generated;
    bool clip_content;
    float clip_inset[4];
    bool first_fragment, last_fragment;
    bool paint_box;
};

struct ViewNodeState {
    DomNodeRef source;
    lam::Up<ViewCssStyle> computed_style;
    lam::Up<StyleTree> specified_style;
    lam::Up<FontProp> font;
    lam::Up<InlineProp> inline_prop;
    lam::Up<BoundaryProp> boundary;
    lam::Up<BlockProp> block;
    lam::Up<LayoutViewNode> first_occurrence;
    lam::Up<LayoutViewNode> last_occurrence;
    size_t occurrence_count;
};

enum ViewPageSide : uint8_t {
    VIEW_PAGE_LEFT,
    VIEW_PAGE_RIGHT,
};

struct ViewPageBox {
    LayoutViewNode node;
    LayoutViewRef referenced_page; // instances borrow immutable content through their generation lease
    RdtLogicalRect content_rect;
    uint32_t page_number;
    uint32_t sequence_page, folio;
    lam::Up<const char> label;
    lam::Up<const RadiantPageSequence> sequence;
    lam::Up<const RadiantFixedPage> fixed;
    ViewPageSide side;
    bool blank;
    lam::Up<ViewPageStyle> style;
    lam::Up<const char> name;
    lam::Up<LayoutViewNode> margin_boxes[CSS_PAGE_MARGIN_BOX_COUNT];
    lam::Up<LayoutViewNode> static_boxes[4];
};

struct ViewPageRange {
    uint32_t first, last;
};

struct ViewPageSelection {
    bool all;
    const ViewPageRange* ranges;
    size_t range_count;
};

enum ViewPageArrangement : uint8_t {
    VIEW_PAGES_GRID,
    VIEW_PAGES_BOOK,
};

struct ViewPreviewOptions {
    ViewPageArrangement arrangement;
    uint32_t rows, columns;
    bool groups_horizontal;
    bool column_major;
    bool right_binding;
    uint32_t anchor_page;
    float scale;
    float padding;
    float column_gap, row_gap, group_gap;
};

struct ViewPagePlacement {
    LayoutViewRef page;
    uint32_t page_number;
    RdtLogicalRect rect;
    float scale;
};

enum ViewModelStatus : uint8_t {
    VIEW_MODEL_OK,
    VIEW_MODEL_INVALID_ARGUMENT,
    VIEW_MODEL_OUT_OF_MEMORY,
    VIEW_MODEL_STALE_SOURCE,
    VIEW_MODEL_INVALID_PAGE_RANGE,
    VIEW_MODEL_INVALID_PAGE_SIDE,
};
// parsed ranges borrow the caller's pool; parsing preserves physical numbering and range order.
ViewModelStatus view_page_selection_parse(Pool* pool, const char* text, ViewPageSelection* result);
bool view_page_selection_text_valid(const char* text);

struct ViewTreeModel {
    lam::Up<DomDocument> document;
    ViewEnvironment environment;
    uint64_t source_epoch;
    uint64_t tree_id;
    lam::Own<Arena> arena;
    lam::Own<hashmap> source_states;
    lam::Own<ViewCssContext> css;
    lam::Own<PagedComposition> composition;
    lam::Own<hashmap> image_resources; // generation owns assets independently of browsing and other editions
    lam::Up<LayoutViewNode> root;
    lam::Up<LayoutViewNode*> nodes;
    size_t node_count, node_id_count, node_capacity; // live count; issued IDs include rollback tombstones
    lam::Up<ViewPageBox*> pages;
    size_t page_count, page_capacity;
    lam::Up<ViewPagePlacement> placements;
    size_t placement_count;
    RdtLogicalRect preview_bounds;
    uint32_t presentation_generation;
    bool committed;
    bool page_instances;
    lam::Counted<ViewPageGeneration> page_generation;
    lam::Up<ViewPageGeneration> retained_generation; // retired owner is read-only until the last lease ends
    lam::Up<ViewModelCheckpoint> checkpoint;
};

ViewEnvironment view_environment_default(ViewPresentation presentation);
ViewPreviewOptions view_preview_options_default();
ViewTree* view_tree_secondary_create(DomDocument* document,
                                    const ViewEnvironment* environment);
// independent presentation over every physical page; selection affects placement only.
ViewTree* view_tree_page_instances_create(ViewTree* source,
    const ViewPageSelection* selection, const ViewPreviewOptions* options,
    ViewModelStatus* status = nullptr);
ViewTree* view_tree_page_content_owner(ViewTree* tree);
const ViewPageBox* view_tree_page_material(ViewTree* tree, const ViewPageBox* page);
bool view_tree_secondary_release(DomDocument* document, ViewTree* tree);
void view_tree_secondary_release_all(DomDocument* document);
bool view_tree_model_destroy(ViewTree* tree);
bool view_tree_model_reset(ViewTree* tree);
bool view_tree_model_source_valid(const ViewTree* tree);
ViewNodeState* view_tree_node_state(ViewTree* tree, DomNode* source, bool create);
LayoutViewNode* view_tree_fragment_append(ViewTree* tree, LayoutViewNode* parent,
    DomNode* source, RdtLogicalRect rect, size_t text_start = 0, size_t text_length = 0);
ViewPageBox* view_tree_page_append(ViewTree* tree, float width, float height,
    RdtLogicalRect content_rect, ViewPageSide side, bool blank = false);
LayoutViewNode* view_tree_node_resolve(ViewTree* tree, LayoutViewRef ref);
bool view_tree_model_commit(ViewTree* tree);
// checkpoints are LIFO and end on accept/restore; records must retain stable addresses until then.
ViewModelCheckpoint* view_tree_model_checkpoint(ViewTree* tree);
bool view_tree_model_record(ViewTree* tree, void* address, size_t size);
bool view_tree_model_touch_node(ViewTree* tree, LayoutViewNode* node);
bool view_tree_model_restore(ViewTree* tree, ViewModelCheckpoint* checkpoint);
bool view_tree_model_accept(ViewTree* tree, ViewModelCheckpoint* checkpoint);
ViewModelStatus view_tree_preview_arrange(ViewTree* tree,
    const ViewPageSelection* selection, const ViewPreviewOptions* options);
typedef bool (*ViewPagePaintFn)(ViewTree* tree, const ViewPageBox* page,
                              const ViewPagePlacement* placement, void* context);
ViewModelStatus view_tree_preview_paint(ViewTree* tree, const RdtLogicalRect* clip,
                                       ViewPagePaintFn paint, void* context);
ViewModelStatus view_tree_pages_visit(ViewTree* tree,
    const ViewPageSelection* selection, ViewPagePaintFn paint, void* context);
