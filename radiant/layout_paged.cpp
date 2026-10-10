#include "layout_paged.hpp"
#include "page_document.hpp"
#include "layout.hpp"
#include "layout_table.hpp"
#include "render.hpp"
#include "typeset_marks.hpp"
#include "typeset_regions.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lib/font/font.h"
#include "../lib/mem_factory.h"
#include "../lib/mem_grow.hpp"
#include "../lib/ref_count.h"
#include "../lib/utf.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/str.h"
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum PagedFlowKind : uint8_t {
    PAGED_FLOW_BLOCK, PAGED_FLOW_PARAGRAPH, PAGED_FLOW_TABLE_ROW, PAGED_FLOW_TABLE_COLUMN, PAGED_FLOW_TABLE_WRAPPER, PAGED_FLOW_NATIVE
};
enum PagedFlowStepKind : uint8_t { PAGED_STEP_BEFORE, PAGED_STEP_OPEN, PAGED_STEP_LINE, PAGED_STEP_TABLE_ROW, PAGED_STEP_NATIVE, PAGED_STEP_CLOSE };
enum PagedTableGroup : uint8_t { PAGED_TABLE_BODY, PAGED_TABLE_HEADER, PAGED_TABLE_FOOTER };
enum PagedRelaxation : uint32_t { PAGED_RELAX_MINIMA = 1, PAGED_RELAX_AVOIDANCE = 2 };
enum PagedRegionKind : uint8_t { PAGED_REGION_NOTE, PAGED_REGION_TOP, PAGED_REGION_BOTTOM, PAGED_REGION_COUNT };
enum PagedCellAnchor : uint8_t { PAGED_CELL_BASELINE, PAGED_CELL_BEFORE, PAGED_CELL_UNALIGNED };
struct PagedTableAlignment { float anchors[PAGED_CELL_UNALIGNED]; };
struct PagedRegionRecord;
struct PagedImagePaint;
struct PagedCellLeaf;
struct PagedIntrinsic { float minimum, maximum; bool generated; };
struct PagedTableRows {
    PagedTableRows* next;
    float containing_height;
    uint64_t reference_revision;
    float* heights;
    PagedTableAlignment* alignments;
    bool forced;
};
struct PagedTableTracks {
    PagedTableTracks* next;
    float width;
    float label_gap;
    float* columns;
    PagedTableRows* rows;
};
struct PagedTableContinuation {
    PagedTableContinuation* next;
    uint64_t identity;
    size_t* first;
};
struct PagedNativeFlow;
struct PagedFlowNode {
    PagedFlowKind kind;
    PagedNativeFlow* native;
    DomElement* source;
    ViewCssStyle* style;
    PagedFlowNode* first_child;
    PagedFlowNode* last_child;
    PagedFlowNode* next;
    PagedFlowNode* parent;
    TypesetParagraph paragraph;
    TypesetItem* items;
    size_t item_capacity, block_note_item;
    PagedImagePaint* image;
    bool has_dynamic_items;
    bool has_line_keeps;
    bool table_cell;
    bool group_first, group_last;
    PagedTableGroup table_group;
    ViewCssStyle* row_group;
    size_t columns, cell_count, column, column_span, row_span, row_index;
    size_t row_count;
    PagedFlowNode** table_rows;
    PagedFlowNode* span_first;
    PagedFlowNode* span_last;
    bool has_rowspans;
    bool table_fixed, table_proportional;
    float table_spacing_h, table_spacing_v;
    LayoutTableColumnWidths table_measures;
    float table_minimum, table_maximum, table_percent;
    PagedTableTracks* table_tracks;
    PagedTableContinuation* table_continuations;
    PagedFlowNode* column_sources;
    PagedFlowNode* last_column_source;
    size_t declared_columns, next_declared_column;
    PagedFlowNode* captions;
    PagedFlowNode* last_caption;
    PagedFlowNode* table_grid;
    PagedCellLeaf* cell_leaves;
    size_t cell_leaf_count, cell_leaf_capacity, cell_begin, cell_end;
};

struct PagedCellLeaf { PagedFlowNode* flow; size_t begin, end; };

static bool paged_is_table_grid(const PagedFlowNode* flow) {
    return flow->kind != PAGED_FLOW_TABLE_WRAPPER && flow->style->display.inner == CSS_VALUE_TABLE;
}

static bool paged_is_label_body_grid(const PagedFlowNode* flow) {
    return flow->style->label_body && flow->style->label_body->grid;
}

template<typename Fn>
static TypesetStatus paged_flow_walk(PagedFlowNode* flow, size_t depth, size_t max_depth, Fn& visit) {
    if (depth > max_depth) return TYPESET_BUDGET_EXHAUSTED;
    if (flow->kind == PAGED_FLOW_PARAGRAPH) return visit(PAGED_STEP_LINE, flow);
    TypesetStatus status = visit(PAGED_STEP_BEFORE, flow);
    if (flow->kind == PAGED_FLOW_TABLE_ROW)
        return status == TYPESET_OK ? visit(PAGED_STEP_TABLE_ROW, flow) : status;
    if (status == TYPESET_OK) status = visit(PAGED_STEP_OPEN, flow);
    if (status == TYPESET_OK && flow->native) status = visit(PAGED_STEP_NATIVE, flow);
    for (PagedFlowNode* child = flow->first_child; status == TYPESET_OK && child; child = child->next)
        status = paged_flow_walk(child, depth + 1, max_depth, visit);
    return status == TYPESET_OK ? visit(PAGED_STEP_CLOSE, flow) : status;
}

static bool paged_is_wrapped_grid(const PagedFlowNode* flow) {
    return paged_is_table_grid(flow) && flow->parent && flow->parent->kind == PAGED_FLOW_TABLE_WRAPPER;
}
struct PagedSourceRecord : TypesetRecord { DomNode* source; PagedRegionRecord* insertion; ViewCssStyle* style; };
enum PagedPaintKind : uint8_t { PAGED_PAINT_TEXT, PAGED_PAINT_IMAGE };
struct PagedPaint : TypesetRecord {
    PagedPaintKind kind;
    DomNode* source;
    ViewCssStyle* style;
};
struct PagedImagePaint : PagedPaint {
    ImageSurface* image;
    DomElement* dimension_source;
    const char* selected_source;
    ReplacedIntrinsicFacts facts;
    EmbedProp object;
    bool framed;
    uint8_t opacity;
};
struct PagedTextPaint : PagedPaint {
    PaintGlyphRun run;
    bool leader;
    PagedFlowNode* marker_owner;
    float marker_width;
    bool current_folio;
    const RadiantPageSequence* query_sequence;
    uint32_t query_folio;
    TypesetMetrics query_metrics;
    PagedTextPaint* query_variants;
    PagedTextPaint* query_next;
};
struct PagedRunningRecord : TypesetRecord { PagedFlowNode* flow; };
struct PagedMarkSource {
    DomNode* source;
    ViewCssStyle* style;
    size_t assigned_at;
    PagedRunningRecord* running;
    uint32_t target_page;
    uint32_t last_target_page;
    uint32_t generated_pages[3][2];
    PagedFlowNode* static_flow;
};
struct PagedTargetValue : TypesetRecord {
    DomNodeRef source;
    CounterSnapshot* counters;
    CounterSnapshot* page_counters;
    const char* text;
    const char* before;
    const char* after;
    uint32_t pages[3][2];
    const char* labels[3][2];
};
struct PagedReferenceSession {
    Pool* pool;
    TypesetTargetStore targets;
    TypesetConvergence convergence;
    size_t page_count;
    uint32_t pass;
    const char** page_labels;
    size_t page_label_count;
    ~PagedReferenceSession() {
        typeset_targets_dispose(&targets);
        if (pool) mem_pool_destroy(pool);
    }
};
enum PagedRegionPartKind : uint8_t { PAGED_REGION_OPEN, PAGED_REGION_CLOSE, PAGED_REGION_PARAGRAPH };
struct PagedRegionPart { PagedRegionPartKind kind; PagedFlowNode* flow; };
struct PagedRegionRecord : TypesetRecord {
    PagedRegionKind kind;
    ViewTree* tree;
    PagedFlowNode* flow;
    PagedRegionPart* parts;
    size_t count, capacity;
    TypesetRegionMaterial material;
    bool block_policy;
};
struct PagedRegionLine {
    PagedFlowNode* flow;
    TypesetLineCandidate line;
    float atomic_height, containing_height;
    float replaced_content_width, replaced_content_height;
    float x, width, y, reference_width;
};
struct PagedRegionPaint : TypesetRecord {
    PagedRegionLine* lines;
    size_t count, capacity;
};
HASHMAP_DEFINE_PTRKEY(paged_mark_sources, PagedMarkSource, source)
struct PagedReferenceWidth {
    const ViewCssStyle* owner;
    float width;
    uint32_t page;
    bool valid;
};
HASHMAP_DEFINE_PTRKEY(paged_reference_widths, PagedReferenceWidth, owner)
struct PagedReferenceUndo { PagedReferenceWidth previous; bool present; uint64_t revision; };

struct PagedNativeCursor {
    TypesetResume resume;
    PagedNativeFlow* producer;
    size_t parent, index;
    TypesetBreak return_boundary;
    TypesetSource return_source;
};
static int paged_native_cursor_compare(const void* first, const void* second, void*) {
    const auto& a = *(const PagedNativeCursor*)first; const auto& b = *(const PagedNativeCursor*)second;
    int order = memcmp(&a.resume, &b.resume, sizeof(TypesetResume));
    if (order) return order;
    if (a.producer != b.producer) return (uintptr_t)a.producer < (uintptr_t)b.producer ? -1 : 1;
    return a.parent == b.parent ? 0 : a.parent < b.parent ? -1 : 1;
}
static uint64_t paged_native_cursor_hash(const void* value, uint64_t seed0, uint64_t seed1) {
    const auto& cursor = *(const PagedNativeCursor*)value;
    uint64_t hash = hashmap_hash_bytes(&cursor.resume, sizeof(TypesetResume), seed0, seed1);
    hash = hashmap_hash_bytes(&cursor.producer, sizeof(cursor.producer), hash, seed1);
    return hashmap_hash_bytes(&cursor.parent, sizeof(cursor.parent), hash, seed1);
}
struct PagedNativeFlow {
    PagedNativeFlowBinding binding;
    PagedNativeFlow* next;
    TypesetResume active;
    PagedNativeCursor* cursors;
    size_t count, capacity;
    HashMap* index;
    bool used;
};
struct PagedNativeRegion {
    const TypesetRegionMaterial* source;
    PagedNativeFlow* flow;
    TypesetRegionMaterial material;
    PagedRegionKind kind;
};
struct PagedNativeRegionEntry {
    const TypesetRegionMaterial* source;
    PagedNativeFlow* flow;
    PagedNativeRegion* region;
};
HASHMAP_DEFINE_FIELD2_KEY(paged_native_regions, PagedNativeRegionEntry, source, flow)
struct PagedNativeCheckpoint {
    PagedNativeFlow* flow;
    TypesetResume saved;
    PagedNativeCheckpoint* next;
};

struct PagedComposition {
    Pool* pool;
    DomDocument* document;
    PagedFlowNode* root;
    PagedNativeFlow* native_flows;
    lam::Own<HashMap> native_regions;
    PagedLayoutOptions options;
    PagedLayoutDiagnostic diagnostic;
    size_t nodes, items, generated_glyphs, block_trials, table_continuations, page_transitions;
    // query caches survive trials in the composition pool; their glyph budget must not rewind.
    size_t query_glyphs;
    lam::Own<HashMap> mark_sources;
    TypesetMarkStore marks;
    TypesetRegionQueue queues[PAGED_REGION_COUNT];
    CounterContext* counters;
    uint64_t note_counter;
    PagedReferenceSession* references;
    DomNodeRef reference_source;
    bool reference_used;
    Pool* reference_pool;
    TypesetTargetStore targets;
    TypesetTargetStore native_targets;
    lam::Own<HashMap> reference_widths;
    PagedReferenceUndo* reference_undo;
    size_t reference_count, reference_capacity;
    uint64_t reference_revision, reference_serial;
};

static TypesetStatus paged_native_checkpoint(PagedComposition* composition, Pool* pool,
        PagedNativeCheckpoint** records) {
    for (PagedNativeFlow* flow = composition->native_flows; flow; flow = flow->next) {
        auto* record = (PagedNativeCheckpoint*)pool_calloc(pool, sizeof(PagedNativeCheckpoint));
        if (!record) return TYPESET_OUT_OF_MEMORY;
        TypesetStatus status = typeset_flow_checkpoint(&flow->binding.provider, &flow->active, &record->saved);
        if (status != TYPESET_OK) return status;
        record->flow = flow; record->next = *records; *records = record;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_native_restore(PagedNativeCheckpoint* records) {
    TypesetStatus status = TYPESET_OK;
    for (auto* record = records; record; record = record->next) {
        TypesetResume restored = {};
        TypesetStatus result = typeset_flow_restore(&record->flow->binding.provider, &record->saved, &restored);
        if (result == TYPESET_OK && memcmp(&restored, &record->saved, sizeof(restored))) result = TYPESET_STALE;
        if (status == TYPESET_OK) status = result;
        if (result == TYPESET_OK) record->flow->active = restored;
    }
    return status;
}

static void paged_reference_restore(PagedComposition* composition, size_t count) {
    while (composition->reference_count > count) {
        const PagedReferenceUndo& undo = composition->reference_undo[--composition->reference_count];
        PagedReferenceWidth previous = undo.previous;
        if (!undo.present) previous.valid = false;
        // retain slots so restoring an invalidated declaration never allocates.
        hashmap_set(composition->reference_widths, &previous);
        composition->reference_revision = undo.revision;
    }
}

struct PagedReferenceMeasurement {
    PagedComposition* composition;
    size_t count;
    explicit PagedReferenceMeasurement(PagedComposition* value) : composition(value), count(value->reference_count) {}
    ~PagedReferenceMeasurement() { paged_reference_restore(composition, count); }
};

static TypesetStatus paged_reference_change(PagedComposition* composition, PagedReferenceWidth value, bool remove = false) {
    if (!composition->reference_widths) {
        composition->reference_widths = lam::own(paged_reference_widths_new(16));
        if (!composition->reference_widths) return TYPESET_OUT_OF_MEMORY;
    }
    const PagedReferenceWidth* prior = (const PagedReferenceWidth*)hashmap_get(composition->reference_widths, &value);
    if (!prior && hashmap_count(composition->reference_widths) >= composition->options.max_nodes) return TYPESET_BUDGET_EXHAUSTED;
    PagedReferenceUndo undo = {prior ? *prior : value, prior != nullptr, composition->reference_revision};
    if (composition->reference_count >= composition->options.max_items) return TYPESET_BUDGET_EXHAUSTED;
    if (!lam::pool_grow_array(composition->pool, &composition->reference_undo, &composition->reference_capacity,
        composition->reference_count + 1, 16)) return TYPESET_OUT_OF_MEMORY;
    value.valid = !remove;
    hashmap_set(composition->reference_widths, &value);
    if (hashmap_oom(composition->reference_widths)) return TYPESET_OUT_OF_MEMORY;
    composition->reference_undo[composition->reference_count++] = undo;
    // rollback restores the old identity; never reuse identities for different selected contexts.
    composition->reference_revision = ++composition->reference_serial;
    return TYPESET_OK;
}

static TypesetStatus paged_reference_capture(PagedComposition* composition, const ViewCssStyle* style, float width, uint32_t page) {
    if (!style->flow_traits) return TYPESET_OK;
    for (const auto& owner : style->flow_traits->indent_owners) if (owner) {
        PagedReferenceWidth key = {owner.get(), width, page, true};
        const PagedReferenceWidth* selected = composition->reference_widths
            ? (const PagedReferenceWidth*)hashmap_get(composition->reference_widths, &key) : nullptr;
        if (selected && selected->valid) continue;
        TypesetStatus status = paged_reference_change(composition, key);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_reference_restyle(PagedComposition* composition, uint32_t page) {
    // an empty restyled sheet replaces its first areas; the undo journal preserves surrounding page trials.
    size_t count = composition->reference_count;
    for (size_t i = 0; i < count; i++) {
        PagedReferenceWidth key = composition->reference_undo[i].previous;
        const PagedReferenceWidth* value = (const PagedReferenceWidth*)hashmap_get(composition->reference_widths, &key);
        if (!value || !value->valid || value->page != page) continue;
        TypesetStatus status = paged_reference_change(composition, *value, true);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}
using PagedBoxEdges = ViewCssBoxEdges;
struct PagedFrame {
    PagedFlowNode* flow;
    LayoutViewNode* fragment;
    float x, content_x, width, content_width, content_height, replaced_height;
    float reference_x, reference_width;
    float replaced_content_width, replaced_content_height;
    float page_start, consumed_content;
    PagedBoxEdges box;
    size_t occurrence;
    bool table_started, table_terminal;
    float table_footer_height;
};
struct PagedRegionState {
    TypesetRegionPlan plan;
    const TypesetRegionMaterial** anchors;
    size_t anchor_count, anchor_capacity;
};
struct PagedRegionTrial {
    TypesetRegionPlan plans[PAGED_REGION_COUNT];
    size_t anchor_counts[PAGED_REGION_COUNT];
};
struct PagedPageProvider;
struct PagedVerticalMeasure { float natural, minimum; size_t end; };
struct PagedSheetDecision { TypesetPageCandidate* columns; size_t count; };
struct PagedSheetRegionLease { TypesetRegionPlan plan; PagedSheetRegionLease* next; };
struct PagedSheetPolicyTrial {
    Pool* pool;
    TypesetPageCandidate* candidates;
    PagedSheetDecision* decisions;
    size_t count, capacity, decision_capacity, region_count;
    TypesetPageCandidate* prefix;
    TypesetFragmentainerCandidate* regions;
    size_t prefix_count, prefix_capacity, region_capacity;
    const PagedSheetDecision* replay;
    TypesetContribution* contributions;
    size_t contribution_count, contribution_capacity;
    TypesetStatus status;
    PagedSheetRegionLease* leases;
    size_t leased_items;
};
struct PagedComposer {
    PagedPageProvider* provider;
    ViewTree* tree;
    PagedComposition* composition;
    PagedFrame* frames;
    size_t depth, frame_capacity;
    ViewPageBox* page;
    LayoutViewNode* column_root;
    ViewPageStyle page_style;
    uint32_t column_index;
    RdtLogicalRect initial_containing_block;
    float y, bottom, page_start, pending_margin;
    bool pending_break, page_has_content, sheet_has_content, closure_failure, body_aligned, policy_done, auxiliary_page;
    ViewFragmentRole role;
    ViewBreak requested_break;
    const char* page_name;
    const RadiantPageSequence* sequence;
    uint32_t terminal_page;
    size_t committed_lines;
    bool atomic_fragment;
    bool retain_aux_tail, policy_trial;
    PagedSheetPolicyTrial* sheet_trial;
    PagedRegionState regions[PAGED_REGION_COUNT];
    RadiantSpaceSpec* spaces;
    size_t space_count, space_capacity, space_depth;
    LayoutViewNode** space_anchors;
    size_t space_anchor_count, space_anchor_capacity;
    bool space_start;
    float space_ratio, space_optimum_extra;
};

static float paged_body_height(const PagedComposer* composer) {
    return composer->y - composer->page_style.content_rect.y - composer->regions[PAGED_REGION_TOP].plan.reserved_height;
}

static bool paged_has_next_column(const PagedComposer* composer) {
    return composer->page_style.column_count > 1 && composer->column_index + 1 < composer->page_style.column_count;
}

static bool paged_break_avoids(const PagedComposer* composer, ViewBreak value) {
    return value == VIEW_BREAK_AVOID || value == VIEW_BREAK_AVOID_COLUMN ||
        (value == VIEW_BREAK_AVOID_PAGE && !paged_has_next_column(composer));
}

static TypesetStatus paged_failure(PagedComposition* composition, TypesetStatus status,
    DomNode* source, uint32_t page, const char* reason);

static TypesetStatus paged_column_select(PagedComposer* composer, uint32_t index) {
    const ViewPageStyle* style = composer->page->style;
    if (style->column_count > 1 && composer->tree->model->environment.presentation != VIEW_PRESENTATION_PAGED)
        return paged_failure(composer->composition, TYPESET_INVALID, style->body_region->source.address,
            composer->page->page_number, "body-region columns require paged presentation");
    if (style->column_count > composer->composition->options.max_nodes)
        return paged_failure(composer->composition, TYPESET_BUDGET_EXHAUSTED, style->body_region->source.address,
            composer->page->page_number, "body columns exceed the common node budget");
    RdtLogicalRect rect = {};
    if (!view_css_page_column(style, index, &rect)) return TYPESET_INVALID;
    composer->column_index = index; composer->page_style.content_rect = rect;
    composer->column_root = nullptr;
    composer->y = composer->page_start = rect.y; composer->bottom = rect.y + rect.height;
    return TYPESET_OK;
}

static PagedVerticalMeasure paged_vertical_measure(PagedComposer* composer, float body, bool end);

static bool paged_table_omits(const PagedFlowNode* table, bool footer) {
    return table->style->flow_traits && table->style->flow_traits->table_omit[footer];
}

static bool paged_table_header_visible(const PagedFrame* frame) {
    return !paged_table_omits(frame->flow, false) || !frame->fragment || frame->fragment->first_fragment;
}

static void* paged_checkpoint_copy(Pool* pool, const void* source, size_t size) {
    if (!size) return nullptr;
    void* result = pool_alloc(pool, size);
    if (result) memcpy(result, source, size);
    return result;
}

struct PagedCheckpoint {
    PagedComposer* composer = nullptr;
    PagedComposer saved = {};
    Pool* pool = nullptr;
    ViewModelCheckpoint* view = nullptr;
    PagedFrame* frames = nullptr;
    RadiantSpaceSpec* spaces = nullptr;
    LayoutViewNode** space_anchors = nullptr;
    const TypesetRegionMaterial** anchors[PAGED_REGION_COUNT] = {};
    TypesetRegionPlan plans[PAGED_REGION_COUNT] = {};
    TypesetRegionCheckpoint queues[PAGED_REGION_COUNT] = {};
    TypesetMarkCheckpoint marks = {};
    PagedLayoutDiagnostic diagnostic = {};
    size_t generated_glyphs = 0, reference_count = 0;
    PagedNativeCheckpoint* native = nullptr;
    TypesetPolicyCheckpoint policy_checkpoint = {};
    bool policy_saved = false;

    void dispose() {
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
            typeset_region_plan_dispose(&plans[i]);
            // replay starts a new transaction; queue snapshots belonged to the released pool.
            queues[i] = {};
        }
        if (pool) mem_pool_destroy(pool);
        pool = nullptr; native = nullptr; policy_saved = false;
    }

    TypesetStatus begin(PagedComposer* target) {
        composer = target; saved = *target;
        pool = mem_pool_create((MemContext*)target->tree->model->document->services.mem_ctx,
            MEM_ROLE_LAYOUT, "typeset.html.checkpoint");
        if (!pool) return TYPESET_OUT_OF_MEMORY;
        frames = (PagedFrame*)paged_checkpoint_copy(pool, target->frames, target->depth * sizeof(PagedFrame));
        if (target->depth && !frames) return TYPESET_OUT_OF_MEMORY;
        spaces = (RadiantSpaceSpec*)paged_checkpoint_copy(pool, target->spaces, target->space_count * sizeof(RadiantSpaceSpec));
        space_anchors = (LayoutViewNode**)paged_checkpoint_copy(pool, target->space_anchors,
            target->space_anchor_count * sizeof(LayoutViewNode*));
        if ((target->space_count && !spaces) || (target->space_anchor_count && !space_anchors)) return TYPESET_OUT_OF_MEMORY;
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
            const PagedRegionState& region = target->regions[i];
            anchors[i] = (const TypesetRegionMaterial**)paged_checkpoint_copy(pool, region.anchors,
                region.anchor_count * sizeof(TypesetRegionMaterial*));
            if (region.anchor_count && !anchors[i]) return TYPESET_OUT_OF_MEMORY;
            TypesetStatus status = typeset_region_plan_retain(&region.plan, &plans[i]);
            if (status == TYPESET_OK) status = typeset_region_checkpoint(&target->composition->queues[i], pool, &queues[i]);
            if (status != TYPESET_OK) return status;
        }
        TypesetStatus native_status = paged_native_checkpoint(target->composition, pool, &native);
        if (native_status != TYPESET_OK) return native_status;
        const TypesetPagePolicy* policy = target->composition->options.page_policy;
        if (policy && policy->checkpoint) {
            TypesetStatus status = typeset_policy_checkpoint(policy, &policy_checkpoint);
            if (status != TYPESET_OK) return status;
            policy_saved = true;
        }
        marks = typeset_marks_checkpoint(&target->composition->marks);
        generated_glyphs = target->composition->generated_glyphs;
        reference_count = target->composition->reference_count;
        diagnostic = target->composition->diagnostic;
        view = view_tree_model_checkpoint(target->tree);
        return view ? TYPESET_OK : TYPESET_OUT_OF_MEMORY;
    }

    TypesetStatus restore() {
        if (!view) return TYPESET_INVALID;
        TypesetStatus status = paged_native_restore(native);
        const TypesetPagePolicy* policy = composer->composition->options.page_policy;
        if (policy_saved) {
            TypesetStatus result = typeset_policy_restore(policy, &policy_checkpoint);
            if (status == TYPESET_OK) status = result;
        }
        PagedFrame* current_frames = composer->frames;
        size_t frame_capacity = composer->frame_capacity;
        RadiantSpaceSpec* current_spaces = composer->spaces; size_t space_capacity = composer->space_capacity;
        LayoutViewNode** current_space_anchors = composer->space_anchors; size_t space_anchor_capacity = composer->space_anchor_capacity;
        PagedRegionState current[PAGED_REGION_COUNT];
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
            current[i] = composer->regions[i];
            typeset_region_plan_dispose(&current[i].plan);
            TypesetStatus restored = typeset_region_restore(&queues[i], &plans[i]);
            if (status == TYPESET_OK) status = restored;
        }
        *composer = saved;
        composer->frames = current_frames; composer->frame_capacity = frame_capacity;
        if (saved.depth) memcpy(current_frames, frames, saved.depth * sizeof(PagedFrame));
        composer->spaces = current_spaces; composer->space_capacity = space_capacity;
        composer->space_anchors = current_space_anchors; composer->space_anchor_capacity = space_anchor_capacity;
        if (saved.space_count) memcpy(current_spaces, spaces, saved.space_count * sizeof(RadiantSpaceSpec));
        if (saved.space_anchor_count) memcpy(current_space_anchors, space_anchors, saved.space_anchor_count * sizeof(LayoutViewNode*));
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
            PagedRegionState& region = composer->regions[i];
            region.anchors = current[i].anchors; region.anchor_capacity = current[i].anchor_capacity;
            if (region.anchor_count) memcpy(region.anchors, anchors[i], region.anchor_count * sizeof(TypesetRegionMaterial*));
            region.plan = plans[i]; plans[i] = {};
        }
        TypesetStatus restored = typeset_marks_restore(&composer->composition->marks, marks);
        if (status == TYPESET_OK) status = restored;
        composer->composition->generated_glyphs = generated_glyphs;
        paged_reference_restore(composer->composition, reference_count);
        composer->composition->diagnostic = diagnostic;
        if (!view_tree_model_restore(composer->tree, view)) status = TYPESET_STALE;
        view = nullptr; dispose();
        return status;
    }

    TypesetStatus accept() {
        if (!view || !view_tree_model_accept(composer->tree, view)) return TYPESET_STALE;
        view = nullptr; dispose();
        return TYPESET_OK;
    }

    TypesetStatus fail(TypesetStatus status) {
        PagedLayoutDiagnostic failure = composer->composition->diagnostic;
        bool closure = composer->closure_failure;
        TypesetStatus restored = view ? restore() : TYPESET_OK;
        composer->composition->diagnostic = failure;
        composer->closure_failure = closure;
        return restored == TYPESET_OK ? status : restored;
    }

    ~PagedCheckpoint() {
        if (view && restore() != TYPESET_OK) log_error("PAGED_CHECKPOINT restore: composition rollback failed");
        dispose();
    }
};

static TypesetStatus paged_regions_prepare(PagedComposer* composer);
static TypesetStatus paged_regions_close(PagedComposer* composer);
static TypesetStatus paged_regions_drain(PagedComposer* composer);
static TypesetStatus paged_page_policy_finish(PagedComposer* composer);
static TypesetStatus paged_sheet_aux_column(PagedComposer* composer, TypesetResume* cursor);
static TypesetStatus paged_policy_aux_sheet(PagedComposer* composer, TypesetPageAssembly* assembly, bool* finished, bool* reinsert);
static TypesetStatus paged_assembly_resolve(PagedComposer* composer, TypesetPagePlan* plan,
    TypesetPageAssembly* assembly, bool* reinsert);
static TypesetStatus paged_region_measure(void* context, const TypesetResume* start,
    const TypesetRegionConstraints* constraints, bool split, Pool* scratch, TypesetRegionSlice* slice);
static TypesetStatus paged_flow_height(PagedComposer* composer, PagedFlowNode* flow,
        float parent_width, float parent_height, bool leading, float* result, bool* forced,
        bool allow_overflow = false, float* baseline = nullptr, float* before = nullptr);
static TypesetStatus paged_fixed_flow(PagedComposer* composer, PagedFlowNode* flow, float* natural_height = nullptr);
static TypesetStatus paged_native_fixed_flow(PagedComposer* composer, PagedFlowNode* flow, bool measure);
static TypesetStatus paged_table_build(ViewTree* tree, PagedComposition* composition,
    PagedFlowNode* table, size_t depth);
static TypesetStatus paged_table_furniture(PagedComposer* composer, PagedFrame* frame, bool footer);
static TypesetStatus paged_table_dimensions(PagedComposer* composer, PagedFrame* frame);
static TypesetStatus paged_table_height(PagedComposer* composer, PagedFlowNode* flow,
    float width, float height, bool leading, float* result, bool* forced);
static TypesetStatus paged_table_measure(PagedComposer* composer, PagedFlowNode* table);
static TypesetStatus paged_flow_intrinsic(PagedComposer* composer, PagedFlowNode* flow,
    float basis, PagedIntrinsic* result);
static TypesetStatus paged_table_columns_open(PagedComposer* composer, PagedFrame* frame);
static TypesetStatus paged_table_columns_finish(ViewTree* tree, LayoutViewNode* table);
static TypesetStatus paged_content_text(ViewTree* tree, PagedComposition* composition, ViewCssStyle* style,
    ViewPageBox* page, const CssValue* content, uint64_t footnote, char** result, size_t* length,
    PagedFlowNode* paragraph = nullptr);

PagedLayoutOptions paged_layout_options_default() {
    PagedLayoutOptions options = {};
    options.max_pages = 10000;
    options.max_nodes = 1000000;
    options.max_block_trials = 2 * options.max_nodes;
    options.max_page_transitions = options.max_block_trials;
    options.max_items = 4000000;
    options.max_depth = 256;
    options.max_reference_passes = 8;
    options.max_container_passes = 256;
    options.first_side = VIEW_PAGE_RIGHT;
    return options;
}

void paged_composition_destroy(ViewTree* tree) {
    if (!tree || !tree->model || !tree->model->composition) return;
    PagedComposition* composition = tree->model->composition;
    for (PagedNativeFlow* flow = composition->native_flows; flow; flow = flow->next) {
        if (flow->index) hashmap_free(flow->index);
        flow->binding.owner.release(flow->binding.owner.context);
    }
    if (composition->mark_sources) hashmap_free(composition->mark_sources);
    if (composition->reference_widths) hashmap_free(composition->reference_widths);
    if (composition->native_regions) hashmap_free(composition->native_regions);
    typeset_targets_dispose(&composition->targets);
    typeset_targets_dispose(&composition->native_targets);
    if (composition->reference_pool) mem_pool_destroy(composition->reference_pool);
    counter_context_destroy(composition->counters);
    mem_pool_destroy(composition->pool);
    pool_free(tree->prop_pool, composition);
    tree->model->composition = nullptr;
}

static TypesetStatus paged_failure(PagedComposition* composition, TypesetStatus status,
        DomNode* source, uint32_t page, const char* reason) {
    // Preserve the producer's precise failure when an outer composition layer adds context.
    if (composition->diagnostic.status != TYPESET_OK) return status;
    composition->diagnostic.status = status;
    composition->diagnostic.source = source ? dom_node_ref(source) : DomNodeRef{};
    composition->diagnostic.page_number = page;
    composition->diagnostic.reason = reason;
    composition->diagnostic.origin = radiant_page_source_origin(composition->document, source);
    return status;
}

static PagedNativeFlow* paged_native_find(PagedComposition* composition, DomNode* control) {
    for (PagedNativeFlow* flow = composition->native_flows; flow; flow = flow->next)
        if (control && flow->binding.control.address == control) return flow;
    return nullptr;
}

static TypesetStatus paged_native_region_measure(void* context, const TypesetResume* start,
        const TypesetRegionConstraints* constraints, bool split, Pool* scratch, TypesetRegionSlice* slice) {
    const TypesetRegionMaterial* source = ((const PagedNativeRegion*)context)->source;
    return source->measure(source->context, start, constraints, split, scratch, slice);
}

static TypesetStatus paged_native_namespaces(Pool* pool, const PagedLayoutOptions& options, TypesetSourceScope* scope) {
    if (!options.native_flow_count) return TYPESET_OK;
    if (options.native_flow_count > options.max_nodes || options.native_flow_count > SIZE_MAX / sizeof(TypesetSourceNamespace))
        return TYPESET_BUDGET_EXHAUSTED;
    TypesetSourceNamespace* namespaces = (TypesetSourceNamespace*)pool_alloc(pool,
        options.native_flow_count * sizeof(TypesetSourceNamespace));
    if (!namespaces) return TYPESET_OUT_OF_MEMORY;
    for (size_t i = 0; i < options.native_flow_count; i++) {
        const PagedNativeFlowBinding& binding = options.native_flows[i];
        if (!binding.provider.identity || !binding.provider.generation || (binding.target_count && !binding.targets) ||
            binding.target_count > options.max_nodes) return TYPESET_INVALID;
        for (size_t j = 0; j < i; j++)
            if (namespaces[j].provider == binding.provider.identity && namespaces[j].generation == binding.provider.generation)
                return TYPESET_INVALID;
        namespaces[i] = {binding.provider.identity, binding.provider.generation};
    }
    *scope = {namespaces, options.native_flow_count};
    return TYPESET_OK;
}

static TypesetStatus paged_native_cursor(PagedComposition* composition, PagedNativeFlow* flow,
        PagedNativeCursor key, size_t* index) {
    key.index = flow->count;
    const auto* existing = (const PagedNativeCursor*)hashmap_get(flow->index, &key);
    if (existing) {
        if (existing->return_boundary.legality != key.return_boundary.legality ||
            existing->return_boundary.scope != key.return_boundary.scope ||
            existing->return_boundary.penalty != key.return_boundary.penalty ||
            existing->return_boundary.reason != key.return_boundary.reason ||
            !typeset_source_same_identity(existing->return_source, key.return_source)) return TYPESET_STALE;
        *index = existing->index; return TYPESET_OK;
    }
    if (flow->count >= composition->options.max_items) return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED,
        flow->binding.control.address, 0, "native continuation cursor count exceeds the common item budget");
    if (!lam::pool_grow_array(composition->pool, &flow->cursors, &flow->capacity, flow->count + 1, 16))
        return TYPESET_OUT_OF_MEMORY;
    hashmap_set(flow->index, &key);
    if (hashmap_oom(flow->index)) return TYPESET_OUT_OF_MEMORY;
    *index = flow->count; flow->cursors[flow->count++] = key;
    return TYPESET_OK;
}

static TypesetStatus paged_native_bindings(ViewTree* tree, PagedComposition* composition) {
    const PagedLayoutOptions& options = composition->options;
    if (!options.native_flow_count) return TYPESET_OK;
    if (!options.native_flows || options.native_flow_count > options.max_nodes ||
        tree->model->environment.presentation != VIEW_PRESENTATION_PAGED || tree->model->css->page_document->fixed_count)
        return paged_failure(composition, TYPESET_INVALID, nullptr, 0, "native flows require flowing paged controls and bounded bindings");
    TypesetStatus namespace_status = paged_native_namespaces(composition->pool, options, &composition->marks.sources);
    if (namespace_status != TYPESET_OK) return namespace_status;
    composition->native_targets = {tree->model->tree_id, tree->layout_generation, composition->pool,
        nullptr, 0, 0, options.max_nodes, nullptr, composition->marks.sources};
    for (size_t i = 0; i < options.native_flow_count; i++) {
        const PagedNativeFlowBinding& binding = options.native_flows[i];
        DomNode* control = dom_node_ref_validate(composition->document, binding.control);
        bool bound = false;
        for (const RadiantPageSequence* sequence = tree->model->css->page_document->sequences; sequence; sequence = sequence->next) {
            const RadiantPageFlowBinding* lists[] = {sequence->flows, sequence->static_content};
            for (const auto* first : lists)
                for (const RadiantPageFlowBinding* flow = first; flow; flow = flow->next)
                    bound |= flow->source.address == control;
        }
        bool body = !binding.control.address || (control && control->is_element() &&
            (radiant_page_element(control->as_element(), "flow") || radiant_page_element(control->as_element(), "static-content")) &&
            bound && !paged_native_find(composition, control));
        if (!body || !binding.provider.identity || !binding.provider.generation ||
            !binding.provider.next || !binding.provider.checkpoint || !binding.provider.restore || !binding.material ||
            !binding.owner.context || !binding.owner.retain || !binding.owner.release)
            return paged_failure(composition, TYPESET_INVALID, control, 0, "native flow binding requires a live body/static or nested-only control, replayable provider and material owner");
        for (DomNode* child = control ? control->as_element()->first_child : nullptr; child; child = child->next_sibling)
            if (!child->is_text() || !radiant_page_whitespace(child->as_text()))
                return paged_failure(composition, TYPESET_INVALID, control, 0, "native flow bindings cannot replace authored content");
        PagedNativeFlow* flow = (PagedNativeFlow*)pool_calloc(composition->pool, sizeof(PagedNativeFlow));
        if (!flow) return TYPESET_OUT_OF_MEMORY;
        if (!binding.owner.retain(binding.owner.context)) return TYPESET_STALE;
        flow->binding = binding; flow->next = composition->native_flows; composition->native_flows = flow;
        flow->index = hashmap_new(sizeof(PagedNativeCursor), 16, 0, 0,
            paged_native_cursor_hash, paged_native_cursor_compare, nullptr, nullptr);
        if (!flow->index) return TYPESET_OUT_OF_MEMORY;
        TypesetStatus status = typeset_flow_restore(&binding.provider, &binding.start, &flow->active);
        if (status == TYPESET_OK && memcmp(&flow->active, &binding.start, sizeof(binding.start))) status = TYPESET_STALE;
        TypesetResume saved = {};
        if (status == TYPESET_OK) status = typeset_flow_checkpoint(&binding.provider, &flow->active, &saved);
        size_t index = 0;
        if (status == TYPESET_OK) status = paged_native_cursor(composition, flow, {saved, flow, SIZE_MAX, 0, {}, {}}, &index);
        if (status != TYPESET_OK) return paged_failure(composition, status, control, 0, "native flow start could not be checkpointed");
        for (size_t j = 0; j < binding.target_count; j++) {
            TypesetTarget target = binding.targets[j]; target.page_number = target.last_page_number = 0; target.binding = nullptr;
            status = typeset_target_append(&composition->native_targets, &target);
            if (status != TYPESET_OK) return status;
        }
    }
    return TYPESET_OK;
}

static RadiantSpaceSpec paged_space_range(const PagedComposer* composer, bool end) {
    return radiant_spaces_resolve(composer->spaces, composer->space_count, composer->space_start, end);
}

static TypesetStatus paged_space_append(PagedComposer* composer, const RadiantSpaceSpec& space) {
    if (!space.specified) return TYPESET_OK;
    if (composer->space_count >= composer->composition->options.max_items) return TYPESET_BUDGET_EXHAUSTED;
    if (!lam::pool_grow_array(composer->composition->pool, &composer->spaces, &composer->space_capacity,
        composer->space_count + 1, 8)) return TYPESET_OUT_OF_MEMORY;
    if (!composer->space_count) {
        composer->space_depth = composer->depth;
        float start = composer->page_style.content_rect.y + composer->regions[PAGED_REGION_TOP].plan.reserved_height;
        composer->space_start = !composer->page_has_content && composer->y == start;
    }
    composer->spaces[composer->space_count++] = space;
    return TYPESET_OK;
}

static TypesetStatus paged_space_flush(PagedComposer* composer, bool end = false) {
    if (!composer->space_count) return TYPESET_OK;
    RadiantSpaceSpec space = paged_space_range(composer, end);
    bool fixed = composer->atomic_fragment || composer->role != VIEW_FRAGMENT_BODY ||
        composer->tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS;
    float used = fixed ? space.optimum : space.minimum + (space.optimum - space.minimum) * composer->space_ratio;
    if (!isfinite(used) || !isfinite(composer->space_optimum_extra + space.optimum - space.minimum)) return TYPESET_INVALID;
    // opened empty ancestors move with the resolved stack; their own before-space stays outside their boxes.
    for (size_t i = 0; i < composer->space_anchor_count; i++) {
        LayoutViewNode* node = composer->space_anchors[i];
        if (!view_tree_model_touch_node(composer->tree, node)) return TYPESET_OUT_OF_MEMORY;
        node->rect.y += used;
    }
    for (size_t i = composer->space_depth; i < composer->depth; i++) composer->frames[i].page_start += used;
    composer->y += used; composer->space_optimum_extra += space.optimum - space.minimum;
    composer->space_count = composer->space_anchor_count = 0;
    return TYPESET_OK;
}

static bool paged_list_attribute(DomElement* source, const char* name, int64_t* result) {
    const char* value = source->get_attribute(name);
    return value && str_to_int64(value, strlen(value), result, nullptr) && *result >= INT_MIN && *result <= INT_MAX;
}

static const CssValue* paged_list_counter_property(void* context, DomElement* source, CssPropertyCode property) {
    ViewTree* tree = (ViewTree*)context;
    ViewCssStyle* style = view_css_resolve(tree, source);
    if (!style) return nullptr;
    if (property == CSS_PROPERTY_COUNTER_RESET) return style->counter_reset;
    if (property == CSS_PROPERTY_COUNTER_INCREMENT) return style->counter_increment;
    if (style->counter_set) return style->counter_set;
    int64_t value = 0;
    if (source->tag_id != MARKUP_NAME_LI || !paged_list_attribute(source, "value", &value)) return nullptr;
    char hint[64]; snprintf(hint, sizeof(hint), "counter-set: list-item %lld", (long long)value);
    CssDeclaration* declaration = css_parse_declaration_text(hint, strlen(hint), tree->model->css->pool);
    return declaration ? declaration->value : nullptr;
}

static bool paged_list_counter_item(void* context, DomElement* source) {
    ViewCssStyle* style = view_css_resolve((ViewTree*)context, source);
    return style && style->display.list_item;
}

static bool paged_list_counter_visible(void* context, DomElement* source) {
    ViewCssStyle* style = view_css_resolve((ViewTree*)context, source);
    return style && style->display.outer != CSS_VALUE_NONE;
}

static TypesetStatus paged_counter_property(ViewTree* tree, PagedComposition* owner,
        ViewCssStyle* style, CounterContext* counters, const CssValue* value, size_t operation,
        bool page_context = false) {
    if (!value || css_value_is_none(value)) return TYPESET_OK;
    for (int j = 0; j < css_value_count(value, 0); j++) {
        const CssValue* item = css_value_at(value, j);
        const char* name = item ? layout_css_counter_name(item, true) : nullptr;
        if (!page_context && name && (strcmp(name, "page") == 0 || strcmp(name, "pages") == 0 || strcmp(name, "footnote") == 0))
            return paged_failure(owner, TYPESET_INVALID, style->source, 0, "paged counter declarations require composition checkpoints");
        if (item && item->type == CSS_VALUE_TYPE_FUNCTION && css_function_name_is(item->data.function, "reversed"))
            return paged_failure(owner, TYPESET_INVALID, style->source, 0, "reversed counters require settled list scopes");
    }
    const char* names[] = {"counter-reset", "counter-increment", "counter-set"};
    void (*apply[])(CounterContext*, const char*) = {counter_reset, counter_increment, counter_set};
    LayoutContext context = {}; context.doc = tree->model->document;
    context.pool = lam::up(tree->model->css->pool.get()); context.selected_view_tree = lam::up(tree);
    char* specification = nullptr;
    resolve_counter_property(&context, value, &specification, names[operation], false);
    if (!specification) return TYPESET_OUT_OF_MEMORY;
    apply[operation](counters, specification);
    return TYPESET_OK;
}

struct PagedCounterScope {
    PagedComposition* composition;
    CounterStyleScope style_scope;
    bool pushed, preserve_reset_scope;
    TypesetStatus status;
    PagedCounterScope(ViewTree* tree, PagedComposition* owner, ViewCssStyle* style, bool pseudo = false)
        : composition(owner), pushed(false), preserve_reset_scope(!pseudo), status(TYPESET_OK) {
        CounterScope* previous = owner->counters->current_scope;
        counter_push_scope(owner->counters, pseudo);
        pushed = owner->counters->current_scope != previous;
        if (!pushed) { status = TYPESET_OUT_OF_MEMORY; return; }
        int named_value = 0;
        DomElement* source = style->source;
        if (!pseudo && layout_is_html_list_container_tag(source->tag_id) && !style->counter_reset) {
            // HTML list defaults and attribute hints are below authored counter declarations.
            int64_t start = 0; bool reversed = style->list_reversed;
            bool has_start = source->tag_id == MARKUP_NAME_OL && paged_list_attribute(source, "start", &start);
            if (has_start && (reversed ? start == INT_MAX : start == INT_MIN)) {
                status = paged_failure(owner, TYPESET_BUDGET_EXHAUSTED, source, 0, "list start exceeds the source counter range"); return;
            }
            if (has_start) start += reversed ? 1 : -1;
            else if (reversed) {
                LayoutListCounterQuery query = {tree, paged_list_counter_property, paged_list_counter_item, paged_list_counter_visible};
                int total = 0, last = 0, set = 0;
                layout_sum_reversed_counter_incs(source, "list-item", &total, &last, &set, true, &query);
                start = (int64_t)total + last + set;
                if (start < INT_MIN || start > INT_MAX) { status = TYPESET_BUDGET_EXHAUSTED; return; }
            }
            char specification[64]; snprintf(specification, sizeof(specification), "list-item %lld", (long long)start);
            counter_reset(owner->counters, specification);
        }
        // CSS Lists 3 section 4 applies increment before an explicit set on the same element.
        const CssValue* values[] = {style->counter_reset, style->counter_increment, style->counter_set};
        for (size_t i = 0; i < 3; i++) {
            if (i == 1 && !pseudo && style->display.list_item &&
                !layout_counter_named_value(style->counter_increment, "list-item", 1, &named_value))
                counter_increment(owner->counters, style->list_reversed ? "list-item -1" : "list-item 1");
            status = paged_counter_property(tree, owner, style, owner->counters, values[i], i);
            if (status != TYPESET_OK) return;
        }
        if (!pseudo && style->display.list_item && source->tag_id == MARKUP_NAME_LI && !style->counter_set) {
            int64_t value = 0;
            if (paged_list_attribute(source, "value", &value)) {
                char specification[64]; snprintf(specification, sizeof(specification), "list-item %lld", (long long)value);
                counter_set(owner->counters, specification);
            }
        }
        style->counters = lam::up(counter_snapshot_create(owner->counters, tree->model->css->pool));
        if (!style->counters) status = TYPESET_OUT_OF_MEMORY;
        if (!style_scope.enter(owner->counters,
                (style->computed_containment & CSS_CONTAIN_STYLE) || style->container_axes))
            status = TYPESET_OUT_OF_MEMORY;
    }
    ~PagedCounterScope() {
        style_scope.close();
        // Element resets reach following siblings; the parent frame bounds their lifetime (CSS Lists 3 section 4.4).
        if (pushed) counter_pop_scope_propagate(composition->counters, true, preserve_reset_scope);
    }
};

static PagedFlowNode* paged_flow_new(PagedComposition* composition, PagedFlowKind kind,
                                    DomElement* source, ViewCssStyle* style) {
    if (++composition->nodes > composition->options.max_nodes) {
        paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, source, 0, "source node budget exhausted");
        return nullptr;
    }
    PagedFlowNode* flow = (PagedFlowNode*)pool_calloc(composition->pool, sizeof(PagedFlowNode));
    if (!flow) return nullptr;
    flow->kind = kind; flow->source = source; flow->style = style;
    flow->paragraph.minimum_line_height = style->line_height;
    return flow;
}

static void paged_flow_link(PagedFlowNode* parent, PagedFlowNode* child) {
    if (parent->last_child) parent->last_child->next = child;
    else parent->first_child = child;
    parent->last_child = child;
    child->parent = parent;
}

static const ViewCssStyle* paged_item_style(const TypesetItem& item) {
    return item.paint ? ((const PagedPaint*)item.paint.get())->style :
        item.source.native ? ((const PagedSourceRecord*)item.source.native.get())->style : nullptr;
}

static bool paged_atomic_boundary_allows(const TypesetItem& left, const TypesetItem& right) {
    if (left.kind == TYPESET_PENALTY || right.kind == TYPESET_PENALTY) return false;
    const PagedPaint* a = (const PagedPaint*)left.paint.get();
    const PagedPaint* b = (const PagedPaint*)right.paint.get();
    bool a_image = a && a->kind == PAGED_PAINT_IMAGE, b_image = b && b->kind == PAGED_PAINT_IMAGE;
    if (!a_image && !b_image) return false;
    if ((a_image && ((const PagedImagePaint*)a)->framed) || (b_image && ((const PagedImagePaint*)b)->framed)) return false;
    const ViewCssStyle* common = view_css_common_ancestor(paged_item_style(left), paged_item_style(right));
    if (!common || !radiant_whitespace_spec(common).wrap) return false;
    const PagedPaint* paints[] = {a, b};
    for (size_t side = 0; side < 2; side++) {
        if (!paints[side] || paints[side]->kind == PAGED_PAINT_IMAGE) continue;
        const PagedTextPaint* text = (const PagedTextPaint*)paints[side];
        if (text->marker_owner || text->leader || !text->run.text_len) return false;
        size_t offset = 0, length = text->run.text_len;
        if (!side) {
            offset = length - 1;
            while (offset && (((uint8_t)text->run.text.get()[offset] & 0xC0) == 0x80)) offset--;
        }
        uint32_t cp = 0;
        if (utf8_decode(text->run.text.get() + offset, length - offset, &cp) <= 0 || !layout_atomic_wrap_neighbor_allows(cp)) return false;
    }
    return true;
}

static size_t paged_previous_styled_item(PagedFlowNode* paragraph, size_t count) {
    while (count && paragraph->items[count - 1].kind == TYPESET_GLUE && !paragraph->items[count - 1].paint &&
           !paragraph->items[count - 1].length && paragraph->items[count - 1].glue.natural == 0.0f &&
           paragraph->items[count - 1].boundary.legality == TYPESET_BREAK_FORBIDDEN) count--;
    return count;
}

static TypesetStatus paged_item_append(PagedComposition* composition, PagedFlowNode* paragraph,
                                      const TypesetItem& item) {
    if (++composition->items > composition->options.max_items) return TYPESET_BUDGET_EXHAUSTED;
    if (!lam::pool_grow_array(composition->pool, &paragraph->items, &paragraph->item_capacity,
                              paragraph->paragraph.count + 1, 16)) return TYPESET_OUT_OF_MEMORY;
    TypesetItem next = item;
    size_t previous = paragraph->paragraph.count;
    bool styled_item = item.paint || (item.source.native && ((const PagedSourceRecord*)item.source.native.get())->style);
    const ViewCssStyle* item_style = paged_item_style(item);
    for (const ViewCssStyle* owner = item_style; owner && !paragraph->has_line_keeps; owner = owner->parent) {
        if (owner->display.outer == CSS_VALUE_BLOCK) break;
        const RadiantFlowTraits* traits = owner->flow_traits;
        if (traits && (traits->together.scope[0].kind != RADIANT_KEEP_AUTO || traits->next.scope[0].kind != RADIANT_KEEP_AUTO ||
            traits->previous.scope[0].kind != RADIANT_KEEP_AUTO)) paragraph->has_line_keeps = true;
    }
    // source-only anchors retain order but do not interrupt an adjacent atomic wrap boundary.
    if (styled_item) previous = paged_previous_styled_item(paragraph, previous);
    if (styled_item && previous && paged_atomic_boundary_allows(paragraph->items[previous - 1], item)) {
        next.has_before = true;
        next.before = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_LINE, 0, 0};
    }
    paragraph->items[paragraph->paragraph.count++] = next;
    paragraph->paragraph.items = paragraph->items;
    return TYPESET_OK;
}

static bool paged_white(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f'; }

static bool paged_control_whitespace(DomElement* owner, DomText* text) {
    if (!radiant_page_element(owner, "page-document") && !radiant_page_element(owner, "page-sequence") &&
        !radiant_page_element(owner, "flow") && !radiant_page_element(owner, "static-content")) return false;
    return radiant_page_whitespace(text);
}

static TypesetSource paged_source_identity(ViewTree* tree, PagedComposition* composition,
        DomNode* source, TypesetOffsetUnit unit, ViewCssStyle* style = nullptr) {
    PagedSourceRecord* record = (PagedSourceRecord*)pool_calloc(composition->pool, sizeof(PagedSourceRecord));
    if (!record) return {};
    record->provider = tree->model->tree_id; record->source = source; record->style = style;
    return {tree->model->tree_id, tree->layout_generation, dom_node_ref(source).expected_id, unit,
        lam::up((const TypesetRecord*)record)};
}

static TypesetStatus paged_text_payload(ViewTree* tree, PagedComposition* composition,
        DomNode* source, const char* text, size_t length, ViewCssStyle* style, TypesetItem* item) {
    FontHandle* handle = style->font.font_handle;
    const FontMetrics* metrics = handle ? font_get_metrics(handle) : nullptr;
    if (!metrics) return TYPESET_UNPLACEABLE;
    int bytes = 0; uint32_t cp = 0;
    PagedTextPaint* paint = (PagedTextPaint*)pool_calloc(composition->pool, sizeof(PagedTextPaint));
    if (!paint) return TYPESET_OUT_OF_MEMORY;
    paint->provider = tree->model->tree_id;
    paint->source = source; paint->style = style;
    paint->run.font = lam::up(&style->font_box);
    paint->run.font_family = lam::up(style->font.family.get());
    paint->run.font_size = style->font.font_size;
    paint->run.font_weight = style->font.font_weight_numeric;
    paint->run.italic = style->font.font_style != CSS_VALUE_NORMAL;
    paint->run.color = style->color;
    paint->run.text = lam::up(text);
    if (length > INT_MAX) return TYPESET_BUDGET_EXHAUSTED;
    paint->run.text_len = (int)(length); // INT_CAST_OK: bounded UTF-8 byte count for PaintGlyphRun.
    size_t count = utf8_count(text, length);
    if (count > INT_MAX) return TYPESET_BUDGET_EXHAUSTED;
    uint32_t* glyphs = (uint32_t*)pool_alloc(composition->pool, count * sizeof(uint32_t));
    float* xs = (float*)pool_alloc(composition->pool, count * sizeof(float));
    float* ys = (float*)pool_calloc(composition->pool, count * sizeof(float));
    if (!glyphs || !xs || !ys) return TYPESET_OUT_OF_MEMORY;
    float advance = 0.0f;
    uint32_t previous = 0;
    size_t index = 0;
    for (size_t offset = 0; offset < length;) {
        bytes = utf8_decode(text + offset, length - offset, &cp);
        if (bytes <= 0) return TYPESET_INVALID;
        if (previous) advance += font_get_kerning(handle, previous, cp);
        xs[index] = advance;
        GlyphInfo glyph = font_get_glyph(handle, cp);
        glyphs[index++] = glyph.id;
        advance += glyph.advance_x + style->font.letter_spacing + (cp == ' ' ? style->font.word_spacing : 0.0f);
        previous = cp;
        offset += (size_t)bytes;
    }
    paint->run.glyph_ids = lam::up(glyphs);
    paint->run.xs = lam::up(xs); paint->run.ys = lam::up(ys);
    paint->run.count = (int)count; // INT_CAST_OK: bounded glyph count for PaintGlyphRun.
    float altitude, depth;
    if (!radiant_text_metrics(style, &altitude, &depth)) return TYPESET_UNPLACEABLE;
    item->metrics = {advance, altitude, depth, altitude, {}, nullptr};
    item->paint = lam::up((const TypesetRecord*)paint);
    return TYPESET_OK;
}

static TypesetStatus paged_text_items(ViewTree* tree, PagedComposition* composition,
        PagedFlowNode* paragraph, DomNode* source, const char* text, size_t length, ViewCssStyle* style, bool generated = false) {
    if (!text || !length) return TYPESET_OK;
    FontHandle* handle = style->font.font_handle;
    if (!handle) return TYPESET_UNPLACEABLE;
    const FontMetrics* metrics = font_get_metrics(handle);
    if (!metrics) return TYPESET_UNPLACEABLE;
    RadiantWhitespaceSpec policy = radiant_whitespace_spec(style);
    TypesetSource identity = paged_source_identity(tree, composition, source,
        generated ? TYPESET_PROVIDER_OFFSETS : TYPESET_UTF8_BYTES, style);
    if (!identity.native) return TYPESET_OUT_OF_MEMORY;
    for (size_t start = 0; start < length;) {
        size_t end = start;
        uint32_t cp = 0;
        int bytes = utf8_decode(text + end, length - end, &cp);
        if (bytes < 0) return TYPESET_INVALID;
        bool whitespace = paged_white(cp);
        bool linefeed = cp == '\n' || (cp == '\r' && !style->whitespace);
        bool newline = linefeed && policy.linefeed == RADIANT_LINEFEED_PRESERVE;
        bool zero_width = cp == 0x200B || (linefeed && policy.linefeed == RADIANT_LINEFEED_ZERO_WIDTH);
        end += (size_t)bytes;
        while (end < length && policy.collapse && !newline && !zero_width &&
               (!linefeed || policy.linefeed == RADIANT_LINEFEED_SPACE)) {
            uint32_t next = 0;
            bytes = utf8_decode(text + end, length - end, &next);
            if (bytes < 0) return TYPESET_INVALID;
            if (paged_white(next) != whitespace || next == 0x200B ||
                ((next == '\n' || (next == '\r' && !style->whitespace)) && policy.linefeed != RADIANT_LINEFEED_SPACE)) break;
            end += (size_t)bytes;
        }
        if (!whitespace && !policy.collapse && !zero_width) {
            while (end < length) {
                uint32_t next = 0;
                bytes = utf8_decode(text + end, length - end, &next);
                if (bytes < 0) return TYPESET_INVALID;
                if (paged_white(next) || next == 0x200B) break;
                end += (size_t)bytes;
            }
        }
        TypesetItem item = {};
        item.source = identity;
        item.start = start; item.length = end - start;
        item.boundary = {policy.wrap ? TYPESET_BREAK_ALLOWED : TYPESET_BREAK_FORBIDDEN, TYPESET_BREAK_LINE, 0, 0};
        if ((linefeed && policy.linefeed == RADIANT_LINEFEED_IGNORE) || (whitespace && !newline && !zero_width && policy.ignore)) {
            start = end; continue;
        }
        if (newline) {
            item.kind = TYPESET_PENALTY;
            item.boundary.legality = TYPESET_BREAK_FORCED;
            // collapsed XML whitespace adjacent to a retained linefeed generates no area, across inline sources too.
            for (size_t cursor = paragraph->paragraph.count; cursor;) {
                cursor = paged_previous_styled_item(paragraph, cursor); if (!cursor) break;
                TypesetItem& previous = paragraph->items[--cursor];
                if (previous.kind != TYPESET_GLUE || previous.paint || !previous.length) break;
                const ViewCssStyle* previous_style = paged_item_style(previous);
                if (previous_style && radiant_whitespace_spec(previous_style).collapse)
                    previous.glue.natural = previous.glue.stretch = previous.glue.shrink = 0.0f;
            }
        } else if (zero_width) {
            item.kind = TYPESET_PENALTY;
        } else if (whitespace) {
            item.kind = TYPESET_GLUE;
            float space = font_measure_char(handle, ' ') + style->font.word_spacing;
            size_t previous_count = paged_previous_styled_item(paragraph, paragraph->paragraph.count);
            const TypesetItem* previous = previous_count ? &paragraph->items[previous_count - 1] : nullptr;
            bool repeated = previous && ((previous->kind == TYPESET_GLUE && !previous->paint && previous->length) ||
                (previous->kind == TYPESET_PENALTY && previous->boundary.legality == TYPESET_BREAK_FORCED));
            if (repeated && policy.collapse) space = 0.0f;
            item.glue = {space, space * 0.5f, space * 0.333333f, 0, 0, policy.discard_start, policy.discard_end};
        } else {
            item.kind = TYPESET_BOX;
            TypesetStatus status = paged_text_payload(tree, composition, source, text + start, end - start, style, &item);
            if (status != TYPESET_OK) return status;
        }
        TypesetStatus status = paged_item_append(composition, paragraph, item);
        if (status != TYPESET_OK) return status;
        start = end;
    }
    ViewNodeState* state = view_tree_node_state(tree, source, true);
    if (!state) return TYPESET_OUT_OF_MEMORY;
    if (source->is_text()) state->computed_style = lam::up(style);
    return TYPESET_OK;
}

static TypesetStatus paged_paragraph_ensure(PagedComposition* composition, PagedFlowNode* parent,
        PagedFlowNode** paragraph) {
    if (*paragraph) return TYPESET_OK;
    *paragraph = paged_flow_new(composition, PAGED_FLOW_PARAGRAPH, parent->source, parent->style);
    if (!*paragraph) return TYPESET_OUT_OF_MEMORY;
    paged_flow_link(parent, *paragraph);
    return TYPESET_OK;
}

static const char* paged_running_name(ViewCssStyle* style) {
    const CssValue* value = style->running_position;
    const CssFunction* function = value && value->type == CSS_VALUE_TYPE_FUNCTION ? value->data.function : nullptr;
    return css_function_name_is(function, "running") && function->arg_count == 1
        ? css_value_identifier_name(function->args[0]) : nullptr;
}

static TypesetStatus paged_mark_register(PagedComposition* composition, ViewCssStyle* style) {
    if ((!style->string_set || css_value_is_none(style->string_set)) &&
        (style->display.outer == CSS_VALUE_NONE || !paged_running_name(style)) &&
        (!style->source->id || !*style->source->id) && !style->page_query) return TYPESET_OK;
    PagedMarkSource mark = {style->source, style, SIZE_MAX, nullptr};
    hashmap_set(composition->mark_sources, &mark);
    return hashmap_oom(composition->mark_sources) ? TYPESET_OUT_OF_MEMORY : TYPESET_OK;
}

static TypesetStatus paged_anchor_append(ViewTree* tree, PagedComposition* composition,
        DomElement* source, PagedFlowNode* parent, PagedFlowNode** paragraph) {
    TypesetStatus status = paged_paragraph_ensure(composition, parent, paragraph);
    if (status != TYPESET_OK) return status;
    // An assignment retains source order without consuming body space.
    TypesetItem item = {}; item.kind = TYPESET_GLUE;
    item.glue.discard_start = item.glue.discard_end = true;
    item.boundary.legality = TYPESET_BREAK_FORBIDDEN;
    item.source = paged_source_identity(tree, composition, source, TYPESET_PROVIDER_OFFSETS);
    if (!item.source.native) return TYPESET_OUT_OF_MEMORY;
    return paged_item_append(composition, *paragraph, item);
}

static TypesetStatus paged_hidden_marks(ViewTree* tree, PagedComposition* composition,
        DomElement* source, ViewCssStyle* style, PagedFlowNode* parent, PagedFlowNode** paragraph, size_t depth) {
    if (depth > composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    // A hidden assignment sees inherited counters but does not apply hidden counter declarations.
    style->counters = lam::up(counter_snapshot_create(composition->counters, tree->model->css->pool));
    if (!style->counters) return TYPESET_OUT_OF_MEMORY;
    TypesetStatus status = paged_mark_register(composition, style);
    if (status != TYPESET_OK) return status;
    if (style->string_set && !css_value_is_none(style->string_set)) {
        status = paged_anchor_append(tree, composition, source, parent, paragraph);
        if (status != TYPESET_OK) return status;
    }
    for (DomNode* child = source->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        ViewCssStyle* child_style = view_css_resolve(tree, child->as_element());
        if (!child_style) return TYPESET_OUT_OF_MEMORY;
        status = paged_hidden_marks(tree, composition, child->as_element(), child_style, parent, paragraph, depth + 1);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_region_parts(PagedComposition* composition, PagedRegionRecord* record,
        PagedFlowNode* flow, size_t depth) {
    auto visit = [&](PagedFlowStepKind kind, PagedFlowNode* source) {
        if (kind == PAGED_STEP_BEFORE) return TYPESET_OK;
        if (kind == PAGED_STEP_TABLE_ROW) return TYPESET_INVALID;
        if (!lam::pool_grow_array(composition->pool, &record->parts, &record->capacity, record->count + 1, 16))
            return TYPESET_OUT_OF_MEMORY;
        PagedRegionPartKind part = kind == PAGED_STEP_LINE ? PAGED_REGION_PARAGRAPH :
            kind == PAGED_STEP_OPEN ? PAGED_REGION_OPEN : PAGED_REGION_CLOSE;
        record->parts[record->count++] = {part, source}; return TYPESET_OK;
    };
    return paged_flow_walk(flow, depth, composition->options.max_depth, visit);
}

static TypesetStatus paged_build_children(ViewTree* tree, PagedComposition* composition,
    DomElement* element, PagedFlowNode* parent, PagedFlowNode** paragraph, size_t depth, bool furniture);

static TypesetStatus paged_extracted_flow(ViewTree* tree, PagedComposition* composition,
        DomElement* source, ViewCssStyle* style, size_t depth, const char* prefix, size_t prefix_length,
        PagedFlowNode** result, ViewCssStyle* prefix_style = nullptr) {
    *result = paged_flow_new(composition, PAGED_FLOW_BLOCK, source, style);
    if (!*result) return TYPESET_OUT_OF_MEMORY;
    PagedFlowNode* paragraph = nullptr;
    TypesetStatus status = TYPESET_OK;
    if (prefix_length) {
        status = paged_paragraph_ensure(composition, *result, &paragraph);
        if (status == TYPESET_OK) status = paged_text_items(tree, composition, paragraph,
            source, prefix, prefix_length, prefix_style ? prefix_style : style, true);
    }
    return status == TYPESET_OK ? paged_build_children(tree, composition, source, *result, &paragraph, depth + 1, true) : status;
}

static void paged_region_bind(ViewTree* tree, PagedRegionRecord* record, PagedRegionKind kind) {
    record->provider = tree->model->tree_id; record->tree = tree; record->kind = kind;
    record->material.identity = dom_node_ref(record->flow->source).expected_id;
    record->material.source = {tree->model->tree_id, tree->layout_generation, record->material.identity,
        TYPESET_PROVIDER_OFFSETS, lam::up((const TypesetRecord*)record)};
    record->material.start = {tree->model->tree_id, tree->layout_generation, 0, {0, 0, 0, 0}};
    record->material.context = record; record->material.measure = paged_region_measure;
}

static TypesetStatus paged_note_text(ViewTree* tree, PagedComposition* composition, DomElement* source,
        uint8_t pseudo, uint64_t number, const char* fallback, size_t fallback_length,
        ViewCssStyle** style, char** text, size_t* length) {
    *style = view_css_resolve_pseudo(tree, source, pseudo);
    if (!*style) return TYPESET_OUT_OF_MEMORY;
    PagedCounterScope scope(tree, composition, *style, true);
    if (scope.status != TYPESET_OK) return scope.status;
    const CssValue* content = (*style)->content;
    if (!content || css_value_keyword_equals(content, CSS_VALUE_NORMAL)) {
        *text = pool_dup_n(composition->pool, fallback, fallback_length); *length = fallback_length;
        return *text ? TYPESET_OK : TYPESET_OUT_OF_MEMORY;
    }
    return paged_content_text(tree, composition, *style, nullptr, content, number, text, length);
}

static TypesetStatus paged_note_build(ViewTree* tree, PagedComposition* composition, DomElement* child_element,
        ViewCssStyle* style, PagedFlowNode* parent, PagedFlowNode** paragraph, size_t depth) {
    RadiantNoteBinding* binding = style->note_binding;
    if (binding && binding->status != VIEW_MODEL_OK)
        return paged_failure(composition, TYPESET_INVALID, child_element, 0, binding->reason);
    PagedRegionRecord* record = (PagedRegionRecord*)pool_calloc(composition->pool, sizeof(PagedRegionRecord));
    if (!record) return TYPESET_OUT_OF_MEMORY;
    TypesetStatus status = paged_paragraph_ensure(composition, parent, paragraph);
    if (status != TYPESET_OK) return status;
    size_t call_first = (*paragraph)->paragraph.count;
    if (binding) {
        DomElement* call = binding->call.address->as_element();
        ViewCssStyle* call_style = view_css_resolve(tree, call);
        if (!call_style) return TYPESET_OUT_OF_MEMORY;
        PagedFlowNode* call_flow = nullptr;
        {
            PagedCounterScope call_scope(tree, composition, call_style);
            status = call_scope.status;
            if (status == TYPESET_OK) status = paged_mark_register(composition, call_style);
            if (status == TYPESET_OK) status = paged_extracted_flow(tree, composition, call, call_style, depth, nullptr, 0, &call_flow);
        }
        if (status != TYPESET_OK) return status;
        PagedFlowNode* content = call_flow->first_child;
        if (content && (content->kind != PAGED_FLOW_PARAGRAPH || content->next))
            return paged_failure(composition, TYPESET_INVALID, call, 0, "note call requires inline material");
        // authored call items retain their own sources and styles; no generated number or marker is inserted.
        for (size_t i = 0; content && status == TYPESET_OK && i < content->paragraph.count; i++)
            status = paged_item_append(composition, *paragraph, content->items[i]);
        if (content) (*paragraph)->has_dynamic_items |= content->has_dynamic_items;
        if (status != TYPESET_OK) return status;
        DomElement* body = binding->body.address->as_element();
        ViewCssStyle* body_style = view_css_resolve(tree, body);
        if (!body_style) return TYPESET_OUT_OF_MEMORY;
        PagedCounterScope body_scope(tree, composition, body_style);
        status = body_scope.status;
        if (status == TYPESET_OK) status = paged_mark_register(composition, body_style);
        if (status == TYPESET_OK) status = paged_extracted_flow(tree, composition, body, body_style, depth, nullptr, 0, &record->flow);
    } else {
        uint64_t number = ++composition->note_counter;
        if (number > INT_MAX) return TYPESET_BUDGET_EXHAUSTED;
        char marker[32] = {};
        int marker_length = counter_format_value((int)number, CSS_VALUE_DECIMAL, marker, sizeof(marker)); // INT_CAST_OK: bounded source-order footnote counter.
        if (marker_length <= 0 || (size_t)marker_length >= sizeof(marker) - 1) return TYPESET_INVALID;
        marker[marker_length++] = ' '; marker[marker_length] = '\0';
        ViewCssStyle* marker_style = nullptr; char* note_marker = nullptr; size_t note_length = 0;
        status = paged_note_text(tree, composition, child_element, PSEUDO_ELEMENT_FOOTNOTE_MARKER,
            number, marker, (size_t)marker_length, &marker_style, &note_marker, &note_length);
        if (status == TYPESET_OK) status = paged_extracted_flow(tree, composition, child_element, style,
            depth, note_marker, note_length, &record->flow, marker_style);
        if (status != TYPESET_OK) return status;
        ViewCssStyle* call_style = nullptr; char* call = nullptr; size_t call_length = 0;
        status = paged_note_text(tree, composition, child_element, PSEUDO_ELEMENT_FOOTNOTE_CALL,
            number, marker, (size_t)marker_length - 1, &call_style, &call, &call_length);
        if (status == TYPESET_OK) status = paged_text_items(tree, composition, *paragraph, child_element, call, call_length, call_style, true);
        if (status != TYPESET_OK) return status;
    }
    if (status == TYPESET_OK) status = paged_region_parts(composition, record, record->flow, 0);
    if (status != TYPESET_OK) return status;
    paged_region_bind(tree, record, PAGED_REGION_NOTE);
    const char* reference = css_value_identifier_name(style->float_reference);
    if (reference && str_ieq_cstr(reference, "column")) record->material.reference = TYPESET_REGION_REFERENCE_COLUMN;
    const char* policy = css_value_identifier_name(style->footnote_policy);
    record->block_policy = policy && str_ieq_cstr(policy, "block");
    record->material.split = !record->block_policy && (!policy || !str_ieq_cstr(policy, "line"));
    record->material.defer_anchor = !policy || str_ieq_cstr(policy, "auto");
    if (!binding && call_first == (*paragraph)->paragraph.count) {
        status = paged_anchor_append(tree, composition, child_element, parent, paragraph);
        if (status != TYPESET_OK) return status;
    }
    size_t anchor = call_first;
    if (binding) {
        // an authored call anchors the note at its last generated area, including a wrapped call.
        size_t end = (*paragraph)->paragraph.count;
        while (end > call_first) {
            const TypesetItem& item = (*paragraph)->items[end - 1];
            if (item.paint || (item.kind == TYPESET_GLUE && !item.glue.discard_end && item.glue.natural > 0.0f)) break;
            end--;
        }
        if (end > call_first) anchor = end - 1;
        else {
            status = paged_anchor_append(tree, composition, child_element, parent, paragraph);
            if (status != TYPESET_OK) return status;
            anchor = (*paragraph)->paragraph.count - 1;
        }
    }
    TypesetItem& anchor_item = (*paragraph)->items[anchor];
    PagedSourceRecord* call_source = (PagedSourceRecord*)pool_alloc(composition->pool, sizeof(PagedSourceRecord));
    if (!call_source) return TYPESET_OUT_OF_MEMORY;
    // text runs share source records; only the chosen item may admit this insertion.
    *call_source = *(const PagedSourceRecord*)anchor_item.source.native.get();
    call_source->insertion = record;
    anchor_item.source.native = lam::up((const TypesetRecord*)call_source);
    if (record->block_policy) (*paragraph)->block_note_item = anchor + 1;
    return TYPESET_OK;
}

static TypesetStatus paged_style_traits_admit(PagedComposition* composition, const ViewCssStyle* style) {
    if (style->binding_status != VIEW_MODEL_OK)
        return paged_failure(composition, style->binding_status == VIEW_MODEL_OUT_OF_MEMORY ? TYPESET_OUT_OF_MEMORY : TYPESET_INVALID,
            style->source, 0, style->binding_reason);
    if (style->flow_traits && style->flow_traits->status != VIEW_MODEL_OK)
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, style->flow_traits->reason);
    if (style->whitespace && style->whitespace->status != VIEW_MODEL_OK)
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, style->whitespace->reason);
    if (style->image_spec && style->image_spec->status != VIEW_MODEL_OK)
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, style->image_spec->reason);
    if (style->label_body && style->label_body->status != VIEW_MODEL_OK)
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, style->label_body->reason);
    return TYPESET_OK;
}

static TypesetStatus paged_context_admit(ViewTree* tree, PagedComposition* composition, ViewCssStyle* style,
        bool table_cell = false) {
    // a flow producer must not silently serialize a different formatting context into block text.
    TypesetStatus admitted = paged_style_traits_admit(composition, style);
    if (admitted != TYPESET_OK) return admitted;
    const char* reason = nullptr;
    if (style->note_binding && style->float_value != CSS_VALUE_FOOTNOTE)
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, "note binding requires footnote placement");
    if ((radiant_page_element(style->source, "note-call") || radiant_page_element(style->source, "note-body")) &&
        !radiant_page_element(style->source->parent_element(), "note"))
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, "note call and body require a note parent");
    bool image = style->source->tag() == MARKUP_NAME_IMG && !style->pseudo_element;
    if (image && style->image_spec) {
        const CssValue* overflow = view_css_property(tree, style, "overflow");
        if (overflow && !css_value_keyword_equals(overflow, CSS_VALUE_VISIBLE) && !css_value_keyword_equals(overflow, CSS_VALUE_HIDDEN) &&
            !css_value_keyword_equals(overflow, CSS_VALUE_CLIP))
            return paged_failure(composition, TYPESET_INVALID, style->source, 0, "image viewport overflow requires visible, hidden or clip");
    }
    if ((!image && style->display.inner == RDT_DISPLAY_REPLACED) || css_is_mathml_element(style->source) ||
        css_content_value_has_image_url(view_css_property(tree, style, "content")))
        reason = "replaced content requires a paged fragment producer";
    else if ((!image && style->display.inner != CSS_VALUE_FLOW &&
              style->display.inner != CSS_VALUE_TABLE && !table_cell) ||
             (style->display.outer != CSS_VALUE_BLOCK && style->display.outer != CSS_VALUE_INLINE &&
              !(image && style->display.outer == CSS_VALUE_INLINE_BLOCK) && !table_cell))
        reason = "formatting context requires a paged fragment producer";
    else if (style->position != CSS_VALUE_STATIC)
        reason = "positioned content requires a paged containing-block policy";
    else if (style->float_value != CSS_VALUE_NONE && style->float_value != CSS_VALUE_FOOTNOTE &&
             style->float_value != CSS_VALUE_TOP && style->float_value != CSS_VALUE_BOTTOM)
        reason = "inline floats require a paged exclusion policy";
    const char* columns[] = {"column-count", "column-width"};
    for (size_t i = 0; !reason && i < 2; i++) {
        const CssValue* value = view_css_property(tree, style, columns[i]);
        if (value && !css_value_is_auto(value) && !css_value_is_initial(value) && !css_value_is_unset(value))
            reason = "columns require nested paged fragmentainers";
    }
    if (reason) {
        const char* class_name = style->source->get_attribute("class");
        log_debug("[PAGED_ADMISSION] class=%s outer=%d inner=%d source=%u reason=%s",
            class_name ? class_name : "", style->display.outer, style->display.inner,
            dom_node_ref(style->source).expected_id, reason);
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, reason);
    }
    return TYPESET_OK;
}

static TypesetStatus paged_image_append(ViewTree* tree, PagedComposition* composition,
        PagedFlowNode* parent, PagedFlowNode** paragraph, ViewCssStyle* style) {
    float density = 1.0f; DomElement* dimensions = style->source;
    lam::Temp<char> source(layout_resolve_replaced_image_source(style->source,
        tree->model->css->engine, &density, &dimensions));
    if (!isfinite(density) || density <= 0.0f)
        return paged_failure(composition, TYPESET_UNPLACEABLE, style->source, 0, "image source density requires finite positive natural dimensions");
    const char* selected = source.get();
    if (selected && style->image_spec) selected = radiant_page_resource_url(style->source, selected,
        composition->pool, composition->options.max_depth);
    ImageSurface* image = selected ? load_document_image_resource(tree->model->document,
        &tree->model->image_resources, selected) : nullptr;
    if (!image || (image->format == IMAGE_FORMAT_UNKNOWN && !image->pixels && !image->source_data && !image->source_path))
        return paged_failure(composition, TYPESET_UNPLACEABLE, style->source, 0, "image source is unavailable or requires a sampled media producer");
    PagedImagePaint* payload = (PagedImagePaint*)pool_calloc(composition->pool, sizeof(PagedImagePaint));
    if (!payload) return TYPESET_OUT_OF_MEMORY;
    payload->provider = tree->model->tree_id; payload->kind = PAGED_PAINT_IMAGE;
    payload->source = style->source; payload->style = style; payload->image = image;
    payload->dimension_source = dimensions;
    payload->selected_source = pool_dup_n(composition->pool, selected, strlen(selected));
    if (!payload->selected_source) return TYPESET_OUT_OF_MEMORY;
    payload->object.content_image_resolution = density;
    const CssValue* orientation = nullptr;
    for (ViewCssStyle* current = style; current; current = current->parent) {
        orientation = view_css_property(tree, current, "image-orientation");
        if (orientation && !css_value_is_inherit(orientation) && !css_value_is_unset(orientation)) break;
    }
    if (image->orientation != 1 && css_value_is_none(orientation))
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, "unoriented image pixels require a selected image-orientation producer");
    layout_replaced_image_facts(&payload->facts, image, true, density);
    const CssValue* opacity = nullptr;
    for (ViewCssStyle* current = style; current; current = current->parent) {
        opacity = view_css_property(tree, current, "opacity");
        if (!opacity || !css_value_is_inherit(opacity)) break;
    }
    float alpha = opacity && opacity->type == CSS_VALUE_TYPE_NUMBER ? (float)opacity->data.number.value
        : opacity && opacity->type == CSS_VALUE_TYPE_PERCENTAGE ? (float)opacity->data.percentage.value / 100.0f : 1.0f;
    if (clamp_unit(alpha) != 1.0f)
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, "image opacity requires a paged box effect group");
    payload->opacity = 255;
    const CssValue* fit = view_css_property(tree, style, "object-fit");
    if (fit && css_value_is_inherit(fit) && style->parent) fit = view_css_property(tree, style->parent, "object-fit");
    payload->object.object_fit = fit && (css_value_is_initial(fit) || css_value_is_unset(fit)) ? CSS_VALUE_FILL
        : fit && fit->type == CSS_VALUE_TYPE_KEYWORD ? fit->data.keyword : (CssEnum)0;
    LayoutContext context = {}; context.doc = tree->model->document;
    context.selected_view_tree = lam::up(tree); context.selected_style = lam::up(style);
    context.font = style->font_box; context.root_font_size = tree->model->css->root_font_size;
    resolve_object_position_value(&context, view_css_property(tree, style, "object-position"), &payload->object);
    payload->framed = parent->source == style->source && parent->kind == PAGED_FLOW_BLOCK;
    if (payload->framed) parent->image = payload;
    TypesetStatus status = paged_paragraph_ensure(composition, parent, paragraph);
    if (status != TYPESET_OK) return status;
    (*paragraph)->has_dynamic_items = true;
    if (payload->framed) (*paragraph)->paragraph.minimum_line_height = 0.0f;
    TypesetItem item = {}; item.kind = TYPESET_BOX;
    item.boundary.legality = TYPESET_BREAK_FORBIDDEN;
    item.source = paged_source_identity(tree, composition, style->source, TYPESET_PROVIDER_OFFSETS);
    item.paint = lam::up((const TypesetRecord*)payload);
    return item.source.native ? paged_item_append(composition, *paragraph, item) : TYPESET_OUT_OF_MEMORY;
}

static TypesetStatus paged_pseudo_append(ViewTree* tree, PagedComposition* composition,
        DomElement* source, uint8_t pseudo, PagedFlowNode* parent, PagedFlowNode** paragraph) {
    ViewCssStyle* style = view_css_resolve_pseudo(tree, source, pseudo);
    if (!style) return TYPESET_OUT_OF_MEMORY;
    const CssValue* content = style->content;
    if (!content || css_value_is_none(content) || css_value_keyword_equals(content, CSS_VALUE_NORMAL) ||
        style->display.outer == CSS_VALUE_NONE) return TYPESET_OK;
    if ((style->display.outer != CSS_VALUE_INLINE && style->display.outer != CSS_VALUE_BLOCK) ||
        style->display.inner != CSS_VALUE_FLOW)
        return paged_failure(composition, TYPESET_INVALID, source, 0, "block generated content requires nested fragment producers");
    TypesetStatus admitted = paged_context_admit(tree, composition, style);
    if (admitted != TYPESET_OK) return admitted;

    PagedCounterScope scope(tree, composition, style, true);
    if (scope.status != TYPESET_OK) return scope.status;
    char* text = nullptr; size_t length = 0;
    PagedFlowNode* generated_parent = parent;
    PagedFlowNode* generated_paragraph = nullptr;
    bool block = style->display.outer == CSS_VALUE_BLOCK;
    if (block) {
        // a block pseudo owns a formatting box while retaining its semantic source; no DOM child is added.
        *paragraph = nullptr;
        generated_parent = paged_flow_new(composition, PAGED_FLOW_BLOCK, source, style);
        if (!generated_parent) return TYPESET_OUT_OF_MEMORY;
        paged_flow_link(parent, generated_parent);
    }
    PagedFlowNode** destination = block ? &generated_paragraph : paragraph;
    TypesetStatus status = paged_paragraph_ensure(composition, generated_parent, destination);
    if (status == TYPESET_OK) status = paged_content_text(tree, composition, style, nullptr, content, 0, &text, &length, *destination);
    if (status != TYPESET_OK) return composition->diagnostic.status != TYPESET_OK ? status :
        paged_failure(composition, status, source, 0, "unresolved body generated-content binding");
    style->generated_text = lam::up(text);
    return TYPESET_OK;
}

static TypesetStatus paged_list_marker(ViewTree* tree, PagedComposition* composition,
        PagedFlowNode* parent, ViewCssStyle* source, TypesetItem* item, bool* outside) {
    *item = {}; *outside = source->display.outer == CSS_VALUE_BLOCK && !source->list_marker_inside;
    ViewCssStyle* style = view_css_resolve_pseudo(tree, source->source, PSEUDO_ELEMENT_MARKER);
    if (!style) return TYPESET_OUT_OF_MEMORY;
    PagedCounterScope scope(tree, composition, style, true);
    if (scope.status != TYPESET_OK) return scope.status;
    char* text = nullptr; size_t length = 0;
    const CssValue* content = style->content;
    if (css_value_is_none(content)) return TYPESET_OK;
    if (content && !css_value_keyword_equals(content, CSS_VALUE_NORMAL)) {
        TypesetStatus status = paged_content_text(tree, composition, style, nullptr, content, 0, &text, &length);
        if (status != TYPESET_OK) return status;
    } else {
        if (source->list_style_image && !css_value_is_none(source->list_style_image) && !css_value_is_initial(source->list_style_image))
            return paged_failure(composition, TYPESET_INVALID, source->source, 0, "image list markers require a replaced fragment producer");
        StrBuf* buffer = strbuf_new(); if (!buffer) return TYPESET_OUT_OF_MEMORY;
        bool valid = true;
        if (source->list_style_string) strbuf_append_str(buffer, source->list_style_string);
        else if (source->list_style_type == 0) valid = false;
        else if (source->list_style_type != CSS_VALUE_NONE) {
            valid = counter_snapshot_append(source->counters, "list-item", nullptr, source->list_style_type, buffer);
            bool bullet = source->list_style_type == CSS_VALUE_DISC || source->list_style_type == CSS_VALUE_CIRCLE ||
                source->list_style_type == CSS_VALUE_SQUARE;
            if (valid) strbuf_append_str(buffer, bullet ? " " : ". ");
        }
        length = buffer->length; text = pool_dup_n(composition->pool, buffer->str, length); strbuf_free(buffer);
        if (!valid) return paged_failure(composition, TYPESET_INVALID, source->source, 0, "custom list marker styles require a counter-style producer");
        if (!text) return TYPESET_OUT_OF_MEMORY;
    }
    style->generated_text = lam::up(text);
    if (!length) return TYPESET_OK;
    item->kind = TYPESET_BOX; item->boundary.legality = TYPESET_BREAK_FORBIDDEN;
    item->source = paged_source_identity(tree, composition, source->source, TYPESET_PROVIDER_OFFSETS);
    item->length = length;
    if (!item->source.native) return TYPESET_OUT_OF_MEMORY;
    TypesetStatus status = paged_text_payload(tree, composition, source->source, text, length, style, item);
    if (status != TYPESET_OK) return status;
    if (*outside) {
        PagedTextPaint* paint = (PagedTextPaint*)item->paint.get();
        paint->marker_owner = parent; paint->marker_width = item->metrics.advance;
        item->metrics.advance = 0.0f;
    }
    return TYPESET_OK;
}

static PagedFlowNode* paged_first_paragraph(PagedFlowNode* flow) {
    if (flow->kind == PAGED_FLOW_PARAGRAPH) return flow;
    for (PagedFlowNode* child = flow->first_child; child; child = child->next)
        if (PagedFlowNode* paragraph = paged_first_paragraph(child)) return paragraph;
    return nullptr;
}

static TypesetStatus paged_build_children(ViewTree* tree, PagedComposition* composition,
    DomElement* element, PagedFlowNode* parent, PagedFlowNode** paragraph, size_t depth, bool furniture = false) {
    if (depth > composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    ViewCssStyle* source_style = view_css_resolve(tree, element);
    if (!source_style) return TYPESET_OUT_OF_MEMORY;
    if (source_style->display.inner == CSS_VALUE_TABLE)
        return furniture ? paged_failure(composition, TYPESET_INVALID, element, 0,
            "nested and auxiliary tables require a nested table producer") : paged_table_build(tree, composition, parent, depth);
    TypesetStatus admitted = paged_context_admit(tree, composition, source_style,
        parent->source == element && (parent->table_cell || source_style->display.inner == CSS_VALUE_TABLE_CAPTION));
    if (admitted != TYPESET_OK) return admitted;
    if (source_style->page_query) {
        const RadiantPageQuery* query = source_style->page_query;
        if (tree->model->environment.presentation != VIEW_PRESENTATION_PAGED)
            return paged_failure(composition, TYPESET_INVALID, element, 0, "folio queries require paged presentation");
        if (query->status != VIEW_MODEL_OK) return paged_failure(composition, TYPESET_INVALID, element, 0, query->reason);
        composition->reference_used = true; composition->reference_source = dom_node_ref(element);
        const char* text = nullptr;
        if (query->edge == RADIANT_QUERY_CURRENT) {
            TypesetStatus status = paged_paragraph_ensure(composition, parent, paragraph);
            if (status != TYPESET_OK) return status;
            TypesetItem item = {}; item.kind = TYPESET_BOX;
            status = paged_text_payload(tree, composition, element, "0", 1, source_style, &item);
            if (status != TYPESET_OK) return status;
            ((PagedTextPaint*)item.paint.get())->current_folio = true;
            item.source = paged_source_identity(tree, composition, element, TYPESET_PROVIDER_OFFSETS, source_style);
            if (!item.source.native) return TYPESET_OUT_OF_MEMORY;
            (*paragraph)->has_dynamic_items = true;
            return paged_item_append(composition, *paragraph, item);
        } else {
            const TypesetTarget* target = typeset_target_find(&composition->references->targets, query->target);
            if (!target) return paged_failure(composition, TYPESET_INVALID, element, 0, "folio reference has no matching source ID");
            const PagedTargetValue* value = (const PagedTargetValue*)target->binding.get();
            text = value->labels[query->area][query->edge == RADIANT_QUERY_LAST];
        }
        if (!text && composition->references->pass > 1)
            return paged_failure(composition, TYPESET_UNPLACEABLE, element, 0, "folio query has no qualifying placed area");
        if (!text) text = "0"; // unresolved values are measured in their actual font and revisited by convergence.
        text = pool_strdup(composition->pool, text);
        if (!text) return TYPESET_OUT_OF_MEMORY;
        TypesetStatus status = paged_paragraph_ensure(composition, parent, paragraph);
        return status == TYPESET_OK ? paged_text_items(tree, composition, *paragraph, element, text, strlen(text), source_style, true) : status;
    }
    if (element->tag() == MARKUP_NAME_IMG) return paged_image_append(tree, composition, parent, paragraph, source_style);
    TypesetItem marker = {}; bool outside = false;
    if (source_style->display.list_item) {
        TypesetStatus status = paged_list_marker(tree, composition, parent, source_style, &marker, &outside);
        if (status != TYPESET_OK) return status;
        if (marker.paint && !outside) {
            status = paged_paragraph_ensure(composition, parent, paragraph);
            if (status == TYPESET_OK) status = paged_item_append(composition, *paragraph, marker);
            if (status != TYPESET_OK) return status;
        }
    }
    TypesetStatus generated = paged_pseudo_append(tree, composition, element, PSEUDO_ELEMENT_BEFORE, parent, paragraph);
    if (generated != TYPESET_OK) return generated;
    // Freeze logical source values before descendants and page retries can change the counter scopes.
    source_style->counters = lam::up(counter_snapshot_create(composition->counters, tree->model->css->pool));
    if (!source_style->counters) return TYPESET_OUT_OF_MEMORY;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        ViewCssStyle* style = parent->style;
        if (child->is_element()) {
            DomElement* child_element = child->as_element();
            if (radiant_page_element(child_element, "fixed-pages") || radiant_page_element(child_element, "fixed-page"))
                return paged_failure(composition, TYPESET_INVALID, child, 0, "fixed page controls cannot occur inside flowing content");
            if (radiant_page_control_hidden(child_element)) continue;
            if (radiant_page_element(child_element, "static-content")) {
                if (furniture) return paged_failure(composition, TYPESET_INVALID, child, 0, "static content cannot contain another static binding");
                ViewCssStyle* static_style = view_css_resolve(tree, child_element);
                if (!static_style) return TYPESET_OUT_OF_MEMORY;
                PagedCounterScope scope(tree, composition, static_style);
                if (scope.status != TYPESET_OK) return scope.status;
                PagedFlowNode* flow = paged_flow_new(composition, PAGED_FLOW_BLOCK, child_element, static_style);
                if (!flow) return TYPESET_OUT_OF_MEMORY;
                flow->native = paged_native_find(composition, child_element);
                if (flow->native) { flow->kind = PAGED_FLOW_NATIVE; flow->native->used = true; }
                PagedFlowNode* content = nullptr;
                TypesetStatus status = paged_build_children(tree, composition, child_element, flow, &content, depth + 1, true);
                if (status != TYPESET_OK) return status;
                PagedMarkSource mark = {}; mark.source = child; mark.style = static_style; mark.assigned_at = SIZE_MAX; mark.static_flow = flow;
                hashmap_set(composition->mark_sources, &mark);
                if (hashmap_oom(composition->mark_sources)) return TYPESET_OUT_OF_MEMORY;
                continue;
            }
            style = view_css_resolve(tree, child_element);
            if (!style) return TYPESET_OUT_OF_MEMORY;
            TypesetStatus mark_status = paged_mark_register(composition, style);
            if (mark_status != TYPESET_OK) return mark_status;
            if (style->display.outer == CSS_VALUE_NONE) {
                mark_status = paged_hidden_marks(tree, composition, child_element, style, parent, paragraph, depth + 1);
                if (mark_status != TYPESET_OK) return mark_status;
                continue;
            }
            TypesetStatus admitted = paged_context_admit(tree, composition, style);
            if (admitted != TYPESET_OK) return admitted;
            PagedCounterScope counter_scope(tree, composition, style);
            if (counter_scope.status != TYPESET_OK) return counter_scope.status;
            if (style->float_value == CSS_VALUE_FOOTNOTE) {
                if (furniture || tree->model->environment.presentation != VIEW_PRESENTATION_PAGED)
                    return paged_failure(composition, TYPESET_INVALID, child, 0, "footnotes require the paged body flow");
                TypesetStatus status = paged_note_build(tree, composition, child_element, style, parent, paragraph, depth);
                if (status != TYPESET_OK) return status;
                continue;
            }
            if (style->float_value == CSS_VALUE_TOP || style->float_value == CSS_VALUE_BOTTOM) {
                const char* reference = css_value_identifier_name(style->float_reference);
                bool column = reference && str_ieq_cstr(reference, "column");
                if (furniture || tree->model->environment.presentation != VIEW_PRESENTATION_PAGED ||
                    !reference || (!column && !str_ieq_cstr(reference, "page")))
                    return paged_failure(composition, TYPESET_INVALID, child, 0, "top and bottom floats require a page or column reference in paged body flow");
                const CssValue* defer = style->float_defer;
                double delay = !defer || css_value_is_none(defer) ? 0.0 :
                    defer->type == CSS_VALUE_TYPE_NUMBER ? defer->data.number.value : NAN;
                if (!isfinite(delay) || delay < 0.0 || delay > UINT32_MAX || floor(delay) != delay)
                    return paged_failure(composition, TYPESET_INVALID, child, 0, "end-relative float deferral requires settled-page composition");
                if (style->clear_value != CSS_VALUE_NONE && style->clear_value != style->float_value)
                    return paged_failure(composition, TYPESET_INVALID, child, 0, "page-float clearance requires ordered flush checkpoints");
                PagedRegionRecord* record = (PagedRegionRecord*)pool_calloc(composition->pool, sizeof(PagedRegionRecord));
                if (!record) return TYPESET_OUT_OF_MEMORY;
                TypesetStatus status = paged_extracted_flow(tree, composition, child_element, style, depth, nullptr, 0, &record->flow);
                if (status != TYPESET_OK) return status;
                paged_region_bind(tree, record, style->float_value == CSS_VALUE_TOP ? PAGED_REGION_TOP : PAGED_REGION_BOTTOM);
                record->material.reference = column ? TYPESET_REGION_REFERENCE_COLUMN : TYPESET_REGION_REFERENCE_PAGE;
                record->material.delay_pages = static_cast<uint32_t>(delay);
                record->material.clear_before = style->clear_value != CSS_VALUE_NONE;
                status = paged_anchor_append(tree, composition, child_element, parent, paragraph);
                if (status != TYPESET_OK) return status;
                PagedSourceRecord* anchor = (PagedSourceRecord*)(*paragraph)->items[(*paragraph)->paragraph.count - 1].source.native.get();
                anchor->insertion = record;
                continue;
            }
            if (paged_running_name(style)) {
                if (furniture) return paged_failure(composition, TYPESET_INVALID, child, 0, "nested running elements are not admitted");
                PagedFlowNode* flow = nullptr;
                TypesetStatus status = paged_extracted_flow(tree, composition, child_element, style, depth, nullptr, 0, &flow);
                if (status != TYPESET_OK) return status;
                PagedRunningRecord* record = (PagedRunningRecord*)pool_alloc(composition->pool, sizeof(PagedRunningRecord));
                if (!record) return TYPESET_OUT_OF_MEMORY;
                record->provider = tree->model->tree_id; record->flow = flow;
                PagedMarkSource key = {}; key.source = child;
                PagedMarkSource* mark = (PagedMarkSource*)hashmap_get(composition->mark_sources, &key);
                if (!mark) return TYPESET_INVALID;
                mark->running = record;
                status = paged_anchor_append(tree, composition, child_element, parent, paragraph);
                if (status != TYPESET_OK) return status;
                continue;
            }
            bool block = style->display.outer == CSS_VALUE_BLOCK;
            if (block) {
                *paragraph = nullptr;
                PagedNativeFlow* native = paged_native_find(composition, child_element);
                if (native && furniture) return paged_failure(composition, TYPESET_INVALID, child, 0,
                    "native body providers cannot occur in page furniture");
                PagedFlowNode* flow = paged_flow_new(composition, native ? PAGED_FLOW_NATIVE : PAGED_FLOW_BLOCK, child_element, style);
                if (!flow) return TYPESET_OUT_OF_MEMORY;
                paged_flow_link(parent, flow);
                if (native) { flow->native = native; native->used = true; continue; }
                PagedFlowNode* nested_paragraph = nullptr;
                TypesetStatus status = paged_build_children(tree, composition, child_element, flow, &nested_paragraph, depth + 1, furniture);
                if (status != TYPESET_OK) return status;
                continue;
            }
            if (child_element->tag() == MARKUP_NAME_BR) {
                TypesetStatus ensured = paged_paragraph_ensure(composition, parent, paragraph);
                if (ensured != TYPESET_OK) return ensured;
                TypesetItem item = {};
                item.kind = TYPESET_PENALTY;
                item.boundary = {TYPESET_BREAK_FORCED, TYPESET_BREAK_LINE, 0, 0};
                PagedSourceRecord* source_record = (PagedSourceRecord*)pool_calloc(composition->pool, sizeof(PagedSourceRecord));
                if (!source_record) return TYPESET_OUT_OF_MEMORY;
                source_record->provider = tree->model->tree_id; source_record->source = child;
                item.source = {tree->model->tree_id, tree->layout_generation, dom_node_ref(child).expected_id,
                               TYPESET_UTF8_BYTES, lam::up((const TypesetRecord*)source_record)};
                TypesetStatus status = paged_item_append(composition, *paragraph, item);
                if (status != TYPESET_OK) return status;
            } else {
                TypesetStatus status = paged_build_children(tree, composition, child_element, parent, paragraph, depth + 1, furniture);
                if (status != TYPESET_OK) return status;
            }
        } else if (child->is_text()) {
            // XML indentation between page-control bindings is structural, outside the HTML content subtree.
            if (paged_control_whitespace(element, child->as_text())) continue;
            TypesetStatus ensured = paged_paragraph_ensure(composition, parent, paragraph);
            if (ensured != TYPESET_OK) return ensured;
            ViewCssStyle* text_style = child->parent && child->parent->is_element()
                ? view_css_resolve(tree, child->parent->as_element()) : style;
            TypesetStatus status = paged_text_items(tree, composition, *paragraph, child, child->as_text()->text, child->as_text()->length, text_style);
            if (status != TYPESET_OK) return status;
        }
    }
    TypesetStatus status = paged_pseudo_append(tree, composition, element, PSEUDO_ELEMENT_AFTER, parent, paragraph);
    if (status == TYPESET_OK && marker.paint && outside) {
        // an outside marker joins the first content line, including a nested block's line; retries roll both back.
        PagedFlowNode* destination = paged_first_paragraph(parent);
        if (!destination) {
            status = paged_paragraph_ensure(composition, parent, paragraph); destination = *paragraph;
        }
        if (status == TYPESET_OK) status = paged_item_append(composition, destination, marker);
        if (status == TYPESET_OK) {
            size_t insertion = 0;
            // the marker is out of flow; leading collapsible body spaces must still precede its first paint box.
            while (insertion + 1 < destination->paragraph.count && destination->items[insertion].kind == TYPESET_GLUE &&
                destination->items[insertion].glue.discard_start) insertion++;
            memmove(destination->items + insertion + 1, destination->items + insertion,
                (destination->paragraph.count - insertion - 1) * sizeof(TypesetItem));
            destination->items[insertion] = marker;
            if (destination->block_note_item > insertion) destination->block_note_item++;
        }
    }
    return status;
}

static bool paged_table_whitespace(DomNode* node) {
    if (!node->is_text()) return false;
    DomText* text = node->as_text();
    for (size_t i = 0; i < text->length; i++) if (!paged_white((unsigned char)text->text[i])) return false;
    return true;
}

static TypesetStatus paged_size_keywords_admit(PagedComposition* composition, ViewCssStyle* style) {
    const CssValue* sizes[] = {style->width.get(), style->min_width.get(), style->max_width.get()};
    for (const CssValue* value : sizes)
        // the shared length resolver's numeric sentinel is not an intrinsic measurement.
        if (value && value->type == CSS_VALUE_TYPE_KEYWORD && !css_value_is_auto(value) && !css_value_is_none(value))
            return paged_failure(composition, TYPESET_INVALID, style->source, 0,
                "intrinsic sizing keywords require a table sizing policy");
    return TYPESET_OK;
}

static TypesetStatus paged_table_structure_admit(ViewTree* tree, PagedComposition* composition, ViewCssStyle* style) {
    TypesetStatus admitted = paged_style_traits_admit(composition, style);
    if (admitted != TYPESET_OK) return admitted;
    if (style->position != CSS_VALUE_STATIC || style->float_value != CSS_VALUE_NONE)
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, "out-of-flow table boxes require a table containing-block policy");
    const uint8_t pseudos[] = {PSEUDO_ELEMENT_BEFORE, PSEUDO_ELEMENT_AFTER};
    for (uint8_t pseudo : pseudos) {
        ViewCssStyle* generated = view_css_resolve_pseudo(tree, style->source, pseudo);
        if (!generated) return TYPESET_OUT_OF_MEMORY;
        if (generated->display.outer != CSS_VALUE_NONE && generated->content &&
            !css_value_is_none(generated->content) && !css_value_keyword_equals(generated->content, CSS_VALUE_NORMAL))
            return paged_failure(composition, TYPESET_INVALID, style->source, 0, "table structural pseudos require anonymous table boxes");
    }
    return TYPESET_OK;
}

static TypesetStatus paged_table_source_register(ViewTree* tree, PagedComposition* composition, ViewCssStyle* style) {
    style->counters = lam::up(counter_snapshot_create(composition->counters, tree->model->css->pool));
    return style->counters ? paged_mark_register(composition, style) : TYPESET_OUT_OF_MEMORY;
}

static TypesetStatus paged_table_column_collect(ViewTree* tree, PagedComposition* composition,
        PagedFlowNode* table, ViewCssStyle* style, size_t depth, PagedFlowNode* group = nullptr) {
    if (depth > composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    TypesetStatus status = paged_size_keywords_admit(composition, style);
    if (status != TYPESET_OK) return status;
    if (css_value_keyword_equals(view_css_property(tree, style, "visibility"), CSS_VALUE_COLLAPSE))
        return paged_failure(composition, TYPESET_INVALID, style->source, 0,
            "collapsed columns require a table grid visibility policy");
    PagedFlowNode* column = paged_flow_new(composition, PAGED_FLOW_TABLE_COLUMN, style->source, style);
    if (!column) return composition->diagnostic.status == TYPESET_OK ? TYPESET_OUT_OF_MEMORY : composition->diagnostic.status;
    const RadiantFlowTraits* traits = style->flow_traits.get();
    column->column = traits && traits->column_number ? traits->column_number - 1 : table->next_declared_column;
    if (column->column >= composition->options.max_items)
        return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, style->source, 0, "table grid exceeds its track budget");
    if (traits && traits->column_number && style->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP)
        return paged_failure(composition, TYPESET_INVALID, style->source, 0, "column numbers apply to leaf columns and cells");
    if (group) paged_flow_link(group, column);
    else {
        if (table->last_column_source) table->last_column_source->next = column;
        else table->column_sources = column;
        table->last_column_source = column; column->parent = table;
    }
    if (style->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP) {
        for (DomNode* child = style->source->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;
            ViewCssStyle* nested = view_css_resolve(tree, child->as_element());
            if (!nested) return TYPESET_OUT_OF_MEMORY;
            // CSS table fixup discards every non-column child of a column group.
            if (nested->display.outer == CSS_VALUE_NONE || nested->display.inner != CSS_VALUE_TABLE_COLUMN) continue;
            status = paged_table_structure_admit(tree, composition, nested);
            PagedCounterScope scope(tree, composition, nested);
            if (status == TYPESET_OK) status = scope.status;
            if (status == TYPESET_OK) status = paged_table_source_register(tree, composition, nested);
            if (status == TYPESET_OK) status = paged_table_column_collect(tree, composition, table, nested, depth + 1, column);
            if (status != TYPESET_OK) return status;
        }
    }
    if (style->flow_traits && style->flow_traits->column_proportion > 0.0f) {
        if (!table->table_fixed || paged_is_label_body_grid(table))
            return paged_failure(composition, TYPESET_INVALID, style->source, 0,
                "proportional columns require a fixed table with a definite inline extent");
        if (!column->first_child) table->table_proportional = true;
    }
    if (column->first_child) {
        size_t end = 0;
        column->column = column->first_child->column;
        for (PagedFlowNode* child = column->first_child; child; child = child->next) {
            if (child->column < column->column) column->column = child->column;
            if (child->column + child->column_span > end) end = child->column + child->column_span;
        }
        column->column_span = end - column->column;
    } else {
        column->column_span = layout_table_column_span(style->source);
        if (column->column_span > composition->options.max_items - column->column)
            return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, style->source, 0, "table grid exceeds its track budget");
        // source order sets the next implicit position; the grid bound retains the greatest explicit endpoint.
        table->next_declared_column = column->column + column->column_span;
        if (table->next_declared_column > table->declared_columns) table->declared_columns = table->next_declared_column;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_table_collect(ViewTree* tree, PagedComposition* composition,
        PagedFlowNode* table, DomElement* source, ViewCssStyle* group, PagedTableGroup role, size_t depth) {
    if (depth > composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    for (DomNode* node = source->first_child; node; node = node->next_sibling) {
        if (node->is_text()) {
            if (!paged_table_whitespace(node))
                return paged_failure(composition, TYPESET_INVALID, node, 0, "anonymous table boxes require a table fixup producer");
            continue;
        }
        if (!node->is_element()) continue;
        DomElement* element = node->as_element();
        ViewCssStyle* style = view_css_resolve(tree, element);
        if (!style) return TYPESET_OUT_OF_MEMORY;
        if (style->display.outer == CSS_VALUE_NONE) continue;
        CssEnum kind = style->display.inner;
        bool caption = !group && kind == CSS_VALUE_TABLE_CAPTION;
        TypesetStatus admitted = caption ? paged_context_admit(tree, composition, style, true) :
            paged_table_structure_admit(tree, composition, style);
        if (admitted != TYPESET_OK) return admitted;
        if (caption && style->float_value != CSS_VALUE_NONE)
            return paged_failure(composition, TYPESET_INVALID, element, 0, "out-of-flow captions require a table containing-block policy");
        PagedCounterScope scope(tree, composition, style);
        if (scope.status != TYPESET_OK) return scope.status;
        TypesetStatus status = paged_table_source_register(tree, composition, style);
        if (status != TYPESET_OK) return status;
        if (caption) {
            PagedFlowNode* flow = paged_flow_new(composition, PAGED_FLOW_BLOCK, element, style);
            if (!flow) return TYPESET_OUT_OF_MEMORY;
            if (table->last_caption) table->last_caption->next = flow;
            else table->captions = flow;
            table->last_caption = flow;
            PagedFlowNode* paragraph = nullptr;
            status = paged_build_children(tree, composition, element, flow, &paragraph, depth + 1, true);
        } else if (!group && (kind == CSS_VALUE_TABLE_COLUMN || kind == CSS_VALUE_TABLE_COLUMN_GROUP)) {
            status = paged_table_column_collect(tree, composition, table, style, depth);
        } else if (kind == CSS_VALUE_TABLE_ROW_GROUP || kind == CSS_VALUE_TABLE_HEADER_GROUP || kind == CSS_VALUE_TABLE_FOOTER_GROUP) {
            if (group) return paged_failure(composition, TYPESET_INVALID, element, 0, "nested table groups require anonymous table fixup");
            if (style->height)
                return paged_failure(composition, TYPESET_INVALID, element, 0, "row-group extents require group height distribution");
            PagedTableGroup selected = kind == CSS_VALUE_TABLE_HEADER_GROUP ? PAGED_TABLE_HEADER :
                kind == CSS_VALUE_TABLE_FOOTER_GROUP ? PAGED_TABLE_FOOTER : PAGED_TABLE_BODY;
            // the shared HTML default display groups thead/tfoot; preserve their semantic group kind.
            if (!view_css_property(tree, style, "display")) {
                if (element->tag_id == MARKUP_NAME_THEAD) selected = PAGED_TABLE_HEADER;
                if (element->tag_id == MARKUP_NAME_TFOOT) selected = PAGED_TABLE_FOOTER;
            }
            for (PagedFlowNode* row = table->first_child; row; row = row->next)
                if (selected != PAGED_TABLE_BODY && row->table_group == selected) { selected = PAGED_TABLE_BODY; break; }
            if (selected != PAGED_TABLE_BODY &&
                (style->break_before >= VIEW_BREAK_COLUMN || style->break_after >= VIEW_BREAK_COLUMN))
                return paged_failure(composition, TYPESET_INVALID, element, 0,
                    "forced breaks on repeated groups require a table furniture boundary policy");
            PagedFlowNode* previous = table->last_child;
            status = paged_table_collect(tree, composition, table, element, style, selected, depth + 1);
            PagedFlowNode* first = previous ? previous->next : table->first_child;
            if (first) { first->group_first = true; table->last_child->group_last = true; }
        } else if (kind == CSS_VALUE_TABLE_ROW) {
            PagedFlowNode* row = paged_flow_new(composition, PAGED_FLOW_TABLE_ROW, element, style);
            if (!row) return TYPESET_OUT_OF_MEMORY;
            row->row_group = group; row->table_group = role;
            row->row_index = table->row_count++;
            paged_flow_link(table, row);
            for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                if (child->is_text()) {
                    if (!paged_table_whitespace(child))
                        return paged_failure(composition, TYPESET_INVALID, child, 0, "anonymous table cells require a table fixup producer");
                    continue;
                }
                if (!child->is_element()) continue;
                DomElement* cell = child->as_element(); ViewCssStyle* cell_style = view_css_resolve(tree, cell);
                if (!cell_style) return TYPESET_OUT_OF_MEMORY;
                if (cell_style->display.outer == CSS_VALUE_NONE) continue;
                if (cell_style->float_value != CSS_VALUE_NONE)
                    return paged_failure(composition, TYPESET_INVALID, cell, 0, "floating table cells require an insertion-aware grid producer");
                size_t span = layout_table_cell_colspan(cell);
                if (cell_style->display.inner != CSS_VALUE_TABLE_CELL)
                    return paged_failure(composition, TYPESET_INVALID, cell, 0, "anonymous cells require a table grid producer");
                const RadiantFlowTraits* traits = cell_style->flow_traits.get();
                size_t specified = traits && traits->column_number ? traits->column_number - 1 : 0;
                if (specified > composition->options.max_items)
                    return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, cell, 0, "table grid exceeds its track budget");
                if (specified > row->columns) row->columns = specified;
                if (span > composition->options.max_items - row->columns)
                    return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, cell, 0, "table grid exceeds its track budget");
                for (const CssValue* padding : cell_style->padding)
                    if (layout_css_value_has_percentage(padding))
                        return paged_failure(composition, TYPESET_INVALID, cell, 0, "percentage cell padding requires a table percentage basis");
                PagedCounterScope cell_scope(tree, composition, cell_style);
                if (cell_scope.status != TYPESET_OK) return cell_scope.status;
                status = paged_mark_register(composition, cell_style);
                PagedFlowNode* flow = paged_flow_new(composition, PAGED_FLOW_BLOCK, cell, cell_style);
                if (!flow) return TYPESET_OUT_OF_MEMORY;
                flow->table_cell = true; flow->column = row->columns; flow->column_span = span;
                flow->row_span = layout_table_cell_rowspan(cell);
                paged_flow_link(row, flow); row->columns += flow->column_span; row->cell_count++;
                PagedFlowNode* paragraph = nullptr;
                if (status == TYPESET_OK) status = paged_build_children(tree, composition, cell, flow, &paragraph, depth + 2, true);
                if (status != TYPESET_OK) return status;
            }
        } else return paged_failure(composition, TYPESET_INVALID, element, 0,
            "anonymous table boxes require a table fixup producer");
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

template <typename Fn>
static TypesetStatus paged_table_columns_visit(PagedFlowNode* table, Fn fn) {
    for (PagedFlowNode* source = table->column_sources; source; source = source->next) {
        TypesetStatus status = fn(source);
        if (status != TYPESET_OK) return status;
        for (PagedFlowNode* child = source->first_child; child; child = child->next) {
            status = fn(child);
            if (status != TYPESET_OK) return status;
        }
    }
    return TYPESET_OK;
}


static TypesetStatus paged_table_grid(ViewTree* tree, PagedComposition* composition, PagedFlowNode* table) {
    size_t capacity = 0, work = 0;
    for (PagedFlowNode* row = table->first_child; row; row = row->next) {
        if (row->columns > composition->options.max_items - capacity)
            return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, row->source, 0, "table grid exceeds its track budget");
        capacity += row->columns;
    }
    if (table->declared_columns > capacity) capacity = table->declared_columns;
    if (table->row_count > composition->options.max_items)
        return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, table->source, 0, "table grid exceeds its row budget");
    if (!capacity || !table->row_count)
        return paged_failure(composition, TYPESET_INVALID, table->source, 0, "empty table grids require explicit track sizing");
    Pool* pool = composition->pool;
    table->table_rows = (PagedFlowNode**)pool_calloc(pool, table->row_count * sizeof(PagedFlowNode*));
    size_t* ends = (size_t*)pool_calloc(pool, capacity * sizeof(size_t));
    size_t* covered = (size_t*)pool_calloc(pool, table->row_count * sizeof(size_t));
    if (!table->table_rows || !ends || !covered) return TYPESET_OUT_OF_MEMORY;
    // leaf declarations may arrive out of order, but their physical track ranges must be disjoint.
    TypesetStatus status = paged_table_columns_visit(table, [&](PagedFlowNode* column) {
        if (column->first_child) return TYPESET_OK;
        for (size_t c = column->column; c < column->column + column->column_span; c++) {
            if (ends[c]) return paged_failure(composition, TYPESET_INVALID, column->source, 0,
                "overlapping table column declarations require a column property conflict policy");
            ends[c] = 1;
        }
        return TYPESET_OK;
    });
    memset(ends, 0, capacity * sizeof(size_t));
    PagedFlowNode* group_end = nullptr;
    for (PagedFlowNode* row = table->first_child; status == TYPESET_OK && row; row = row->next) {
        table->table_rows[row->row_index] = row;
        if (!group_end || row->row_index > group_end->row_index) {
            group_end = row;
            while (group_end->next && group_end->next->row_group == row->row_group &&
                    group_end->next->table_group == row->table_group && !group_end->group_last)
                group_end = group_end->next;
        }
        size_t column = 0;
        for (PagedFlowNode* cell = row->first_child; cell; cell = cell->next) {
            cell->row_span = layout_table_used_rowspan(cell->row_span, group_end->row_index - row->row_index + 1);
            if (cell->row_span > (composition->options.max_items - work) / cell->column_span) {
                status = paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, cell->source, 0, "table span coverage exceeds its grid budget");
                break;
            }
            work += cell->row_span * cell->column_span;
            bool overlap = false;
            const RadiantFlowTraits* traits = cell->style->flow_traits.get();
            bool explicit_column = traits && traits->column_number;
            if (explicit_column) column = traits->column_number - 1;
            column = layout_table_place_span(table->row_count, capacity, row->row_index, column,
                cell->row_span, cell->column_span,
                [&](size_t r, size_t c) { return !explicit_column && ends[c] > r; },
                [&](size_t r, size_t c) {
                    if (r == row->row_index && ends[c] > r) overlap = true;
                    ends[c] = r + 1; covered[r]++;
                }, &cell->column, &table->columns);
            table->has_rowspans |= cell->row_span > 1;
            if (overlap) {
                status = paged_failure(composition, TYPESET_INVALID, cell->source, 0, "overlapping table spans require a valid rectangular grid");
                break;
            }
        }
    }
    if (table->declared_columns > table->columns) table->columns = table->declared_columns;
    if (status == TYPESET_OK && table->columns > composition->options.max_items / table->row_count)
        status = paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, table->source, 0,
            "completed table coverage exceeds its grid budget");
    // replay occupied columns after the final track count is known; gaps may precede later rowspan origins.
    memset(ends, 0, capacity * sizeof(size_t));
    for (size_t r = 0; status == TYPESET_OK && r < table->row_count; r++) {
        PagedFlowNode* row = table->table_rows[r];
        for (PagedFlowNode* cell = row->first_child; cell; cell = cell->next)
            for (size_t c = cell->column; c < cell->column + cell->column_span; c++) ends[c] = r + cell->row_span;
        if (covered[r] == table->columns) continue;
        if (table->columns - covered[r] > composition->options.max_nodes - composition->nodes) {
            status = paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, row->source, 0,
                "anonymous table cells exceed node budget");
            break;
        }
        ViewCssStyle* style = view_css_anonymous_style(tree, row->style, {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_CELL});
        if (!style) { status = TYPESET_OUT_OF_MEMORY; break; }
        for (size_t c = 0; c < table->columns; c++) {
            if (ends[c] > r) continue;
            PagedFlowNode* cell = paged_flow_new(composition, PAGED_FLOW_BLOCK, nullptr, style);
            if (!cell) {
                status = composition->diagnostic.status == TYPESET_OK ? TYPESET_OUT_OF_MEMORY : composition->diagnostic.status;
                break;
            }
            cell->table_cell = true; cell->column = c; cell->column_span = cell->row_span = 1;
            paged_flow_link(row, cell); row->cell_count++;
        }
    }
    pool_free(pool, ends); pool_free(pool, covered);
    // a connected span cluster has no independent row boundary until cell continuations exist.
    for (size_t first = 0; status == TYPESET_OK && first < table->row_count;) {
        size_t end = first + 1;
        for (size_t row = first; row < end; row++)
            for (PagedFlowNode* cell = table->table_rows[row]->first_child; cell; cell = cell->next)
                if (row + cell->row_span > end) end = row + cell->row_span;
        if (end > first + 1) for (size_t row = first; row < end; row++) {
            PagedFlowNode* flow = table->table_rows[row];
            flow->span_first = table->table_rows[first]; flow->span_last = table->table_rows[end - 1];
            if ((row > first && flow->style->break_before >= VIEW_BREAK_COLUMN) ||
                    (row + 1 < end && flow->style->break_after >= VIEW_BREAK_COLUMN))
                status = paged_failure(composition, TYPESET_INVALID, flow->source, 0,
                    "forced breaks within row spans require spanning-cell continuations");
        }
        first = end;
    }
    return status;
}

static TypesetStatus paged_table_wrap(PagedComposition* composition, PagedFlowNode* wrapper) {
    if (!wrapper->captions) return TYPESET_OK;
    PagedFlowNode* grid = paged_flow_new(composition, PAGED_FLOW_BLOCK, wrapper->source, wrapper->style);
    if (!grid) return TYPESET_OUT_OF_MEMORY;
    // the source's margins belong to the transparent wrapper; decorations belong to its grid (CSS 2.2 §17.4).
    *grid = *wrapper;
    grid->next = nullptr; grid->captions = grid->last_caption = nullptr;
    for (PagedFlowNode* row = grid->first_child; row; row = row->next) row->parent = grid;
    for (PagedFlowNode* column = grid->column_sources; column; column = column->next) column->parent = grid;
    wrapper->kind = PAGED_FLOW_TABLE_WRAPPER; wrapper->table_grid = grid;
    wrapper->first_child = wrapper->last_child = nullptr;
    wrapper->column_sources = wrapper->last_column_source = nullptr;
    PagedFlowNode* bottom = nullptr; PagedFlowNode* last_bottom = nullptr;
    for (PagedFlowNode* caption = wrapper->captions; caption;) {
        PagedFlowNode* next = caption->next; caption->next = nullptr;
        if (caption->style->caption_side == CSS_VALUE_BOTTOM) {
            if (last_bottom) last_bottom->next = caption;
            else bottom = caption;
            last_bottom = caption;
        } else paged_flow_link(wrapper, caption);
        caption = next;
    }
    paged_flow_link(wrapper, grid);
    while (bottom) {
        PagedFlowNode* next = bottom->next; bottom->next = nullptr;
        paged_flow_link(wrapper, bottom); bottom = next;
    }
    wrapper->captions = wrapper->last_caption = nullptr;
    return TYPESET_OK;
}

static TypesetStatus paged_table_build(ViewTree* tree, PagedComposition* composition,
        PagedFlowNode* table, size_t depth) {
    ViewCssStyle* style = table->style;
    TypesetStatus admitted = paged_table_structure_admit(tree, composition, style);
    if (admitted == TYPESET_OK) admitted = paged_size_keywords_admit(composition, style);
    if (admitted != TYPESET_OK) return admitted;
    if (tree->model->environment.presentation != VIEW_PRESENTATION_PAGED)
        return paged_failure(composition, TYPESET_INVALID, table->source, 0,
            "table fragmentation requires a paged presentation");
    // fixed layout with an automatic width uses the automatic algorithm (CSS 2.2 §17.5.2).
    table->table_fixed = css_value_keyword_equals(view_css_property(tree, style, "table-layout"), CSS_VALUE_FIXED) &&
        style->width && !css_value_is_auto(style->width);
    if (paged_is_label_body_grid(table)) table->table_fixed = true;
    const CssValue* collapse = view_css_property(tree, style, "border-collapse");
    if (collapse && !css_value_keyword_equals(collapse, CSS_VALUE_SEPARATE))
        return paged_failure(composition, TYPESET_INVALID, table->source, 0, "collapsed table borders require fragment conflict resolution");
    if (!view_css_border_spacing(tree, style, &table->table_spacing_h, &table->table_spacing_v))
        return paged_failure(composition, TYPESET_INVALID, table->source, 0, "table spacing must resolve to nonnegative lengths");
    if ((style->content && !css_value_keyword_equals(style->content, CSS_VALUE_NORMAL)) || style->height || style->min_height || style->max_height)
        return paged_failure(composition, TYPESET_INVALID, table->source, 0, "table height distribution requires a table sizing policy");
    style->counters = lam::up(counter_snapshot_create(composition->counters, tree->model->css->pool));
    if (!style->counters) return TYPESET_OUT_OF_MEMORY;
    TypesetStatus status = paged_table_collect(tree, composition, table, table->source, nullptr, PAGED_TABLE_BODY, depth + 1);
    if (status != TYPESET_OK) return status;
    status = paged_table_grid(tree, composition, table);
    if (status != TYPESET_OK) return status;
    if (paged_is_label_body_grid(table)) {
        if (table->row_count != 1 || table->columns != 2 || table->declared_columns || table->captions ||
            table->table_spacing_h != 0.0f || table->table_spacing_v != 0.0f)
            return paged_failure(composition, TYPESET_INVALID, table->source, 0,
                "label/body grid requires one two-cell row without columns, captions or grid spacing");
        PagedFlowNode* row = table->table_rows[0];
        if (row->table_group != PAGED_TABLE_BODY || row->cell_count != 2)
            return paged_failure(composition, TYPESET_INVALID, row->source, 0, "label/body grid requires two explicit body cells");
        for (PagedFlowNode* cell = row->first_child; cell; cell = cell->next)
            if (!cell->source)
                return paged_failure(composition, TYPESET_INVALID, row->source, 0, "label/body grid requires two explicit body cells");
            else if (cell->column_span != 1 || cell->row_span != 1)
                return paged_failure(composition, TYPESET_INVALID, cell->source, 0, "label/body cells cannot span tracks or rows");
    }
    for (PagedFlowNode* row = table->first_child; row; row = row->next)
        if (row->table_group == PAGED_TABLE_BODY) return paged_table_wrap(composition, table);
    return paged_failure(composition, TYPESET_INVALID, table->source, 0, "header-only tables require an atomic table producer");
}

static float paged_used_length(PagedComposer* composer, ViewCssStyle* style, const CssValue* value,
                               CssPropertyCode property, float width, float fallback = 0.0f, float height = NAN) {
    float result = view_css_length(composer->tree, style, value, property, width, height);
    return isfinite(result) ? result : fallback;
}

static float paged_flow_margin(PagedComposer* composer, PagedFlowNode* flow, size_t edge, float width) {
    // internal table cells have no margins; measurement and block replay must agree on all four edges.
    if ((flow->kind != PAGED_FLOW_BLOCK && flow->kind != PAGED_FLOW_TABLE_WRAPPER) ||
        flow->table_cell || paged_is_wrapped_grid(flow)) return 0.0f;
    static const CssPropertyCode properties[] = {CSS_PROPERTY_MARGIN_TOP, CSS_PROPERTY_MARGIN_RIGHT,
        CSS_PROPERTY_MARGIN_BOTTOM, CSS_PROPERTY_MARGIN_LEFT};
    return paged_used_length(composer, flow->style, flow->style->margin[edge], properties[edge], width);
}

static float paged_table_spacing_width(const PagedFlowNode* table) {
    return (table->columns + 1) * table->table_spacing_h;
}

static float paged_root_height(const PagedComposer* composer) {
    // body percentages retain the first page's ICB; auxiliary regions supply their own stable container.
    return composer->role == VIEW_FRAGMENT_BODY && composer->initial_containing_block.width > 0.0f
        ? composer->initial_containing_block.height : composer->page_style.content_rect.height;
}

static TypesetStatus paged_box_edges(PagedComposer* composer, const ViewCssStyle* source,
        float width, PagedBoxEdges* box) {
    return view_css_box_edges(composer->tree, source, width, box) == VIEW_MODEL_OK
        ? TYPESET_OK : TYPESET_INVALID;
}


static float paged_fragment_edge(const ViewCssStyle* style, const PagedBoxEdges& box, size_t edge, bool terminal) {
    // border and padding conditionality are independent; layout and published paint use the same policies.
    return (terminal || radiant_decoration_retain(style, false, edge == 2) ? box.border.width.values[edge] : 0.0f) +
        (terminal || radiant_decoration_retain(style, true, edge == 2) ? box.padding[edge] : 0.0f);
}

static TypesetStatus paged_boundary_publish(ViewTree* tree, LayoutViewNode* node,
        const PagedBoxEdges& box, bool top, bool bottom) {
    if (!view_tree_model_touch_node(tree, node)) return TYPESET_OUT_OF_MEMORY;
    struct Storage { BoundaryProp boundary; BorderProp border; BackgroundProp background; };
    Storage* storage = (Storage*)arena_alloc(tree->model->arena, sizeof(Storage));
    if (!storage) return TYPESET_OUT_OF_MEMORY;
    *storage = {}; storage->border = box.border; storage->background = box.background;
    storage->boundary.border = lam::own(&storage->border);
    storage->boundary.background = lam::own(&storage->background);
    for (size_t i = 0; i < 4; i++) storage->boundary.padding.values[i] = box.padding[i];
    // Fragment edges belong to the occurrence, not its source's computed style (D4.5.1v4).
    for (size_t side = 0; side < 2; side++) if (!(side ? bottom : top)) {
        if (!radiant_decoration_retain(node->computed_style, false, side != 0)) storage->border.width.values[side * 2] = 0.0f;
        if (!radiant_decoration_retain(node->computed_style, true, side != 0)) storage->boundary.padding.values[side * 2] = 0.0f;
    }
    node->computed_boundary = lam::up(&storage->boundary);
    return TYPESET_OK;
}

static TypesetStatus paged_frame_finish(PagedComposer* composer, PagedFrame* frame, bool last) {
    if (!view_tree_model_touch_node(composer->tree, frame->fragment)) return TYPESET_OUT_OF_MEMORY;
    frame->fragment->rect.height = fmaxf(0.0f, composer->y - frame->page_start);
    frame->fragment->last_fragment = last;
    ViewNodeState* state = frame->fragment->state;
    ViewCssStyle* style = frame->flow->style;
    if (state && style && !style->pseudo_element && style == state->computed_style.get() &&
        composer->role == VIEW_FRAGMENT_BODY) {
        if (!view_tree_model_record(composer->tree, state, sizeof(*state))) return TYPESET_OUT_OF_MEMORY;
        if (frame->fragment->first_fragment) state->container_width = frame->content_width;
        if (last) {
            float top = paged_fragment_edge(style, frame->box, 0, frame->fragment->first_fragment);
            float bottom = paged_fragment_edge(style, frame->box, 2, true);
            state->container_height = isfinite(frame->content_height) ? frame->content_height
                : frame->consumed_content + fmaxf(0.0f, frame->fragment->rect.height - top - bottom);
            state->container_measured = isfinite(state->container_width) && isfinite(state->container_height);
        }
    }
    if (frame->flow->column_sources) {
        TypesetStatus status = paged_table_columns_finish(composer->tree, frame->fragment);
        if (status != TYPESET_OK) return status;
    }
    return paged_boundary_publish(composer->tree, frame->fragment, frame->box, frame->fragment->first_fragment, last);
}

struct PagedImageMeasure { PagedBoxEdges box; float margin[4], width, height, content_width, content_height; };
static TypesetStatus paged_image_measure(PagedComposer* composer, const PagedImagePaint* image,
        float available_width, float parent_height, PagedImageMeasure* result) {
    *result = {};
    ViewCssStyle* style = image->style;
    TypesetStatus status = paged_box_edges(composer, style, available_width, &result->box);
    if (status != TYPESET_OK) return status;
    float edges_x = result->box.edges[3] + result->box.edges[1];
    float edges_y = result->box.edges[0] + result->box.edges[2];
    auto content_length = [&](const CssValue* value, CssPropertyCode property, bool horizontal, float fallback) {
        float used = paged_used_length(composer, style, value, property, available_width, fallback, parent_height);
        return style->box_sizing == CSS_VALUE_BORDER_BOX && isfinite(used)
            ? fmaxf(0.0f, used - (horizontal ? edges_x : edges_y)) : used;
    };
    ReplacedSizeConstraints constraints = {};
    constraints.width = content_length(style->width, CSS_PROPERTY_WIDTH, true, NAN);
    constraints.height = content_length(style->height, CSS_PROPERTY_HEIGHT, false, NAN);
    const char* hints[] = {"width", "height"};
    const CssValue* specified[] = {style->width, style->height};
    float* axes[] = {&constraints.width, &constraints.height};
    for (size_t i = 0; i < 2; i++) if (!specified[i]) {
        const char* hint = image->dimension_source->get_attribute(hints[i]);
        if (hint) {
            float length = (float)str_to_double_default(hint, strlen(hint), NAN);
            if (isfinite(length) && length >= 0.0f) *axes[i] = length;
        }
    }
    constraints.min_width = content_length(style->min_width, CSS_PROPERTY_MIN_WIDTH, true, 0.0f);
    constraints.max_width = content_length(style->max_width, CSS_PROPERTY_MAX_WIDTH, true, INFINITY);
    constraints.min_height = content_length(style->min_height, CSS_PROPERTY_MIN_HEIGHT, false, 0.0f);
    constraints.max_height = content_length(style->max_height, CSS_PROPERTY_MAX_HEIGHT, false, INFINITY);
    const CssValue* ratio_value = view_css_property(composer->tree, style, "aspect-ratio");
    float ratio = layout_aspect_ratio_value(ratio_value);
    bool natural = image->facts.has_natural_aspect_ratio &&
        (ratio <= 0.0f || layout_aspect_ratio_value_has_auto(ratio_value));
    constraints.preferred_ratio = natural ? image->facts.natural_aspect_ratio : ratio;
    constraints.ratio_content_box = natural || style->box_sizing != CSS_VALUE_BORDER_BOX;
    constraints.horizontal_edges = edges_x; constraints.vertical_edges = edges_y;
    for (size_t i = 0; i < 4; i++) result->margin[i] = paged_used_length(composer, style,
        style->margin[i], radiant_box_side_property(CSS_PROPERTY_MARGIN, static_cast<CssBoxSide>(i)), available_width);
    if (style->image_spec) {
        float natural_width, natural_height;
        layout_replaced_default_object_size(&image->facts, available_width, isfinite(parent_height) ? parent_height : 150.0f,
            &natural_width, &natural_height);
        auto constrain = [](float value, float minimum, float maximum) { return isnan(value) ? value : fmaxf(minimum, fminf(value, maximum)); };
        result->width = constrain(constraints.width, constraints.min_width, constraints.max_width);
        result->height = constrain(constraints.height, constraints.min_height, constraints.max_height);
        if (!radiant_image_size(style->image_spec, natural_width, natural_height,
            &result->width, &result->height, &result->content_width, &result->content_height)) return TYPESET_UNPLACEABLE;
        float width = constrain(result->width, constraints.min_width, constraints.max_width);
        float height = constrain(result->height, constraints.min_height, constraints.max_height);
        if (width != result->width || height != result->height) {
            result->width = width; result->height = height;
            if (!radiant_image_size(style->image_spec, natural_width, natural_height,
                &result->width, &result->height, &result->content_width, &result->content_height)) return TYPESET_UNPLACEABLE;
        }
        return TYPESET_OK;
    }
    if (!layout_replaced_content_size(&image->facts, &constraints,
        fmaxf(0.0f, available_width - edges_x - result->margin[3] - result->margin[1]), &result->width, &result->height))
        return TYPESET_UNPLACEABLE;
    return TYPESET_OK;
}

static void paged_reference_geometry(const PagedComposer* composer, size_t depth, float* x, float* width) {
    const PagedFrame* parent = depth ? &composer->frames[depth - 1] : nullptr;
    *x = parent ? parent->reference_width > 0.0f ? parent->reference_x : parent->content_x : composer->page_style.content_rect.x;
    *width = parent ? parent->reference_width > 0.0f ? parent->reference_width : parent->content_width : composer->page_style.content_rect.width;
}

static TypesetStatus paged_frame_measure(PagedComposer* composer, PagedFrame* frame,
        float parent_x, float parent_width, float parent_height, bool table_outer = false,
        float reference_x = NAN, float reference_width = NAN) {
    if (!isfinite(reference_width)) {
        size_t depth = composer->depth;
        if (depth && frame == &composer->frames[depth - 1]) depth--;
        paged_reference_geometry(composer, depth, &reference_x, &reference_width);
    }
    frame->reference_x = reference_x; frame->reference_width = reference_width;
    if (frame->flow->kind == PAGED_FLOW_TABLE_WRAPPER) {
        PagedFrame grid = {}; grid.flow = frame->flow->table_grid;
        TypesetStatus status = paged_frame_measure(composer, &grid, parent_x, parent_width, parent_height, true, reference_x, reference_width);
        if (status != TYPESET_OK) return status;
        frame->x = frame->content_x = grid.x;
        frame->width = frame->content_width = grid.width;
        frame->content_height = NAN; frame->box = {};
        return TYPESET_OK;
    }
    ViewCssStyle* style = frame->flow->style;
    TypesetStatus captured = paged_reference_capture(composer->composition, style, reference_width,
        composer->page ? composer->page->page_number : 0);
    if (captured != TYPESET_OK) return captured;
    bool wrapped = paged_is_wrapped_grid(frame->flow);
    auto margin = [&](size_t edge) {
        return table_outer ? paged_flow_margin(composer, frame->flow->parent, edge, parent_width) :
            paged_flow_margin(composer, frame->flow, edge, parent_width);
    };
    float left = margin(3), right = margin(1);
    frame->x = parent_x + left;
    TypesetStatus status = paged_box_edges(composer, style, parent_width, &frame->box);
    if (status != TYPESET_OK) return paged_failure(composer->composition, status, frame->flow->source, 0,
        "block decoration requires supported padding and solid border styles");
    float edges = frame->box.edges[3] + frame->box.edges[1];
    bool reference = style->flow_traits && style->flow_traits->reference_inline;
    if (style->label_body && (style->label_body->grid || (style->label_body->context && !reference)) && edges != 0.0f)
        return paged_failure(composer->composition, TYPESET_INVALID, frame->flow->source, 0,
            "label/body grid horizontal decoration requires reference-relative indent geometry");
    frame->content_height = paged_used_length(composer, style, style->height,
        CSS_PROPERTY_HEIGHT, parent_width, NAN, parent_height);
    if (isfinite(frame->content_height)) frame->content_height = fmaxf(0.0f, frame->content_height -
        (style->box_sizing == CSS_VALUE_BORDER_BOX ? frame->box.edges[0] + frame->box.edges[2] : 0.0f));
    if (reference) {
        if ((style->width && !css_value_is_auto(style->width)) || style->min_width || style->max_width || left || right)
            return paged_failure(composer->composition, TYPESET_INVALID, frame->flow->source, 0,
                "reference block geometry requires automatic inline sizing and zero horizontal margins");
        // indents are measured from the reference content edges; decoration extends outside them.
        float indents[2];
        for (size_t i = 0; i < 2; i++) {
            indents[i] = style->flow_traits->indents[i];
            if (const CssValue* expression = style->flow_traits->indent_expressions[i]) {
                PagedReferenceWidth key = {style->flow_traits->indent_owners[i].get(), 0.0f, 0};
                const PagedReferenceWidth* selected = (const PagedReferenceWidth*)hashmap_get(composer->composition->reference_widths, &key);
                indents[i] = selected ? view_css_length(composer->tree, key.owner, expression,
                    CSS_PROPERTY_MARGIN_LEFT, selected->width, NAN) : NAN;
            }
            if (!isfinite(indents[i])) return paged_failure(composer->composition, TYPESET_INVALID, frame->flow->source,
                composer->page ? composer->page->page_number : 0, "reference indents require finite declaration-area results");
        }
        frame->content_x = reference_x + indents[0];
        frame->content_width = reference_width - indents[0] - indents[1];
        frame->x = frame->content_x - frame->box.edges[3]; frame->width = frame->content_width + edges;
        return isfinite(frame->content_width) && frame->content_width > 0.0f ? TYPESET_OK : TYPESET_UNPLACEABLE;
    }
    if (wrapped && !table_outer) {
        // percentages and auto margins were resolved against the wrapper's containing block, exactly once.
        frame->width = parent_width; frame->content_x = frame->x + frame->box.edges[3];
        frame->content_width = frame->width - edges;
        return frame->content_width > 0.0f ? paged_table_dimensions(composer, frame) : TYPESET_UNPLACEABLE;
    }
    if (frame->flow->image) {
        PagedImageMeasure measured = {};
        status = paged_image_measure(composer, frame->flow->image, parent_width, parent_height, &measured);
        if (status != TYPESET_OK) return status;
        frame->width = measured.width + edges; frame->content_width = measured.width;
        frame->replaced_height = measured.height;
        frame->replaced_content_width = measured.content_width;
        frame->replaced_content_height = measured.content_height;
        frame->content_height = measured.height;
        float free = parent_width - frame->width - left - right;
        bool auto_left = css_value_is_auto(style->margin[3]), auto_right = css_value_is_auto(style->margin[1]);
        if (free > 0.0f && auto_left) frame->x += auto_right ? free * 0.5f : free;
        frame->content_x = frame->x + frame->box.edges[3];
        return TYPESET_OK;
    }
    float specified = paged_used_length(composer, style, style->width, CSS_PROPERTY_WIDTH, parent_width, NAN);
    bool table = paged_is_table_grid(frame->flow);
    if (table && style->width && !css_value_is_auto(style->width) && !isfinite(specified))
        return paged_failure(composer->composition, TYPESET_INVALID, frame->flow->source,
            composer->page ? composer->page->page_number : 0, "table width must resolve in its page containing block");
    if (table && !frame->flow->table_fixed) {
        status = paged_table_measure(composer, frame->flow);
        if (status != TYPESET_OK) return status;
        if (!isfinite(specified)) specified = fminf(frame->flow->table_percent >= 100.0f ? INFINITY :
            frame->flow->table_maximum + paged_table_spacing_width(frame->flow),
            fmaxf(0.0f, parent_width - left - right - edges));
        if (style->box_sizing == CSS_VALUE_BORDER_BOX && (!style->width || css_value_is_auto(style->width))) specified += edges;
    }
    frame->width = isfinite(specified) ? specified + (style->box_sizing == CSS_VALUE_BORDER_BOX ? 0.0f : edges) : parent_width - left - right;
    float minimum = paged_used_length(composer, style, style->min_width, CSS_PROPERTY_MIN_WIDTH, parent_width, 0.0f);
    float maximum = paged_used_length(composer, style, style->max_width, CSS_PROPERTY_MAX_WIDTH, parent_width, INFINITY);
    if (style->box_sizing != CSS_VALUE_BORDER_BOX) { minimum += edges; maximum += edges; }
    if (table && !frame->flow->table_fixed)
        minimum = fmaxf(minimum, frame->flow->table_minimum + paged_table_spacing_width(frame->flow) + edges);
    if (table && wrapped) {
        for (PagedFlowNode* caption = frame->flow->parent->first_child; caption; caption = caption->next) {
            if (caption == frame->flow) continue;
            PagedIntrinsic sizes = {};
            status = paged_flow_intrinsic(composer, caption, NAN, &sizes);
            if (status != TYPESET_OK) return status;
            minimum = fmaxf(minimum, sizes.minimum);
        }
    }
    frame->width = fmaxf(minimum, fminf(frame->width, maximum));
    // track allocation owns cell width; declarations contribute during measurement only.
    if (frame->flow->table_cell) frame->width = parent_width;
    if (table || style->display.inner == CSS_VALUE_TABLE_CAPTION) {
        float free = parent_width - frame->width - left - right;
        if (free > 0.0f && css_value_is_auto(style->margin[3]))
            frame->x += css_value_is_auto(style->margin[1]) ? free * 0.5f : free;
    }
    frame->content_x = frame->x + frame->box.edges[3];
    frame->content_width = frame->width - frame->box.edges[3] - frame->box.edges[1];
    if (frame->flow->table_cell) {
        frame->reference_x = frame->content_x; frame->reference_width = frame->content_width;
    }
    if (!isfinite(frame->content_width) || frame->content_width <= 0.0f) return TYPESET_UNPLACEABLE;
    if (table && frame->width > parent_width)
        return paged_failure(composer->composition, TYPESET_UNPLACEABLE, frame->flow->source,
            composer->page ? composer->page->page_number : 0, "table width exceeds its page region");
    return table ? paged_table_dimensions(composer, frame) : TYPESET_OK;
}

static TypesetStatus paged_frame_descend(PagedComposer* composer, PagedFrame* frame,
        float* x, float* width, float* height, float* reference_x = nullptr, float* reference_width = nullptr) {
    TypesetStatus status = paged_frame_measure(composer, frame, *x, *width, *height, false,
        reference_x ? *reference_x : NAN, reference_width ? *reference_width : NAN);
    if (status == TYPESET_OK) {
        *x = frame->content_x; *width = frame->content_width; *height = frame->content_height;
        if (reference_x) *reference_x = frame->reference_x;
        if (reference_width) *reference_width = frame->reference_width;
    }
    return status;
}

static TypesetStatus paged_frame_open(PagedComposer* composer, size_t index, bool first) {
    PagedFrame* frame = &composer->frames[index];
    ViewCssStyle* style = frame->flow->style;
    float parent_x = index ? composer->frames[index - 1].content_x : composer->page_style.content_rect.x;
    float parent_width = index ? composer->frames[index - 1].content_width : composer->page_style.content_rect.width;
    float parent_height = index ? composer->frames[index - 1].content_height : paged_root_height(composer);
    // ancestors reopen from the new page outward; deeper saved frames still belong to the previous page.
    float reference_x, reference_width;
    paged_reference_geometry(composer, index, &reference_x, &reference_width);
    TypesetStatus status = paged_frame_measure(composer, frame, parent_x, parent_width, parent_height, false, reference_x, reference_width);
    if (status != TYPESET_OK) return status;
    float top = paged_fragment_edge(style, frame->box, 0, first);
    if (top != 0.0f || ((first || style->decoration_clone) && paged_is_table_grid(frame->flow))) {
        status = paged_space_flush(composer);
        if (status != TYPESET_OK) return status;
    }
    LayoutViewNode* parent = index ? composer->frames[index - 1].fragment :
        composer->page ? &composer->page->node : composer->tree->model->root.get();
    frame->page_start = composer->y;
    frame->table_started = frame->table_terminal = false;
    frame->fragment = view_tree_fragment_append(composer->tree, parent, frame->flow->source,
                                               {frame->x, composer->y, frame->width, 0.0f});
    if (!frame->fragment) return TYPESET_OUT_OF_MEMORY;
    if (!index && composer->role == VIEW_FRAGMENT_BODY) composer->column_root = frame->fragment;
    if (composer->space_count) {
        if (!lam::pool_grow_array(composer->composition->pool, &composer->space_anchors, &composer->space_anchor_capacity,
            composer->space_anchor_count + 1, 8)) return TYPESET_OUT_OF_MEMORY;
        composer->space_anchors[composer->space_anchor_count++] = frame->fragment;
    }
    frame->fragment->first_fragment = first;
    frame->fragment->paint_box = frame->flow->kind != PAGED_FLOW_TABLE_WRAPPER;
    frame->fragment->role = composer->role;
    frame->fragment->generated = !frame->flow->source || composer->role != VIEW_FRAGMENT_BODY;
    frame->fragment->computed_style = lam::up(style);
    frame->occurrence++;
    composer->y += top;
    return frame->flow->column_sources ? paged_table_columns_open(composer, frame) : TYPESET_OK;
}

static TypesetStatus paged_frames_close(PagedComposer* composer) {
    for (size_t i = composer->depth; i > 0; i--) {
        PagedFrame& frame = composer->frames[i - 1];
        bool blank = composer->page && composer->page->blank;
        float top = paged_fragment_edge(frame.flow->style, frame.box, 0, frame.fragment->first_fragment);
        float bottom = blank ? 0.0f : paged_fragment_edge(frame.flow->style, frame.box, 2, false);
        TypesetStatus furniture = paged_table_furniture(composer, &frame, true);
        if (furniture != TYPESET_OK) return furniture;
        composer->y += bottom;
        TypesetStatus status = paged_frame_finish(composer, &frame, false);
        if (status != TYPESET_OK) return status;
        if (blank) frame.fragment->rect.height = 0.0f;
        frame.consumed_content += fmaxf(0.0f, frame.fragment->rect.height - top - bottom);
    }
    return TYPESET_OK;
}

static ViewPageSide paged_side(const PagedComposer* composer, size_t index) {
    ViewPageSide first = composer->composition->options.first_side;
    return index % 2 ? (first == VIEW_PAGE_RIGHT ? VIEW_PAGE_LEFT : VIEW_PAGE_RIGHT) : first;
}

static bool paged_requested_side(const PagedComposer* composer, ViewBreak request, ViewPageSide* side) {
    if (request != VIEW_BREAK_LEFT && request != VIEW_BREAK_RIGHT && request != VIEW_BREAK_RECTO && request != VIEW_BREAK_VERSO) return false;
    *side = request == VIEW_BREAK_LEFT ? VIEW_PAGE_LEFT : VIEW_PAGE_RIGHT;
    if (request == VIEW_BREAK_RECTO || request == VIEW_BREAK_VERSO) {
        *side = composer->composition->options.right_binding ? VIEW_PAGE_LEFT : VIEW_PAGE_RIGHT;
        if (request == VIEW_BREAK_VERSO) *side = *side == VIEW_PAGE_LEFT ? VIEW_PAGE_RIGHT : VIEW_PAGE_LEFT;
    }
    return true;
}

struct PagedPageSpec {
    const char* name;
    const RadiantPageSequence* sequence;
    uint32_t ordinal, folio;
};

static TypesetStatus paged_page_spec(PagedComposer* composer, size_t index, bool blank, PagedPageSpec* spec) {
    *spec = {composer->page_name, composer->sequence, static_cast<uint32_t>(index + 1), static_cast<uint32_t>(index + 1)};
    if (!spec->sequence) return TYPESET_OK;
    const ViewPageBox* previous = index ? composer->tree->model->pages.get()[index - 1] : nullptr;
    bool continuing = previous && previous->sequence.get() == spec->sequence;
    spec->ordinal = continuing ? previous->sequence_page + 1 : 1;
    spec->folio = continuing || spec->sequence->initial != RADIANT_FOLIO_NUMBER ?
        (previous ? previous->folio + 1 : 1) : spec->sequence->initial_folio;
    if (!continuing && ((spec->sequence->initial == RADIANT_FOLIO_ODD && !(spec->folio % 2)) ||
        (spec->sequence->initial == RADIANT_FOLIO_EVEN && spec->folio % 2))) spec->folio++;
    if (spec->folio > INT32_MAX) return paged_failure(composer->composition, TYPESET_BUDGET_EXHAUSTED,
        spec->sequence->source.address, static_cast<uint32_t>(index + 1), "sequence folio exceeds the counter range");
    spec->name = spec->sequence->master_reference;
    if (!spec->sequence->master_program) return TYPESET_OK;
    if (spec->sequence->master_program->terminal) {
        composer->composition->reference_used = true;
        composer->composition->reference_source = spec->sequence->source;
    }
    const RadiantPageRule* master = radiant_page_master_select(spec->sequence, spec->ordinal, spec->folio, blank,
        composer->terminal_page == index + 1);
    // the first pass has no terminal extent; a terminal-only alternative can provisionally seed it.
    if (!master && composer->terminal_page != index + 1) master = radiant_page_master_select(spec->sequence, spec->ordinal, spec->folio, blank, true);
    if (!master) return paged_failure(composer->composition, TYPESET_INVALID, spec->sequence->source.address,
        static_cast<uint32_t>(index + 1), "sequence master has no eligible page alternative or exhausted its repeat limit");
    bool bound = false;
    for (const RadiantPageFlowBinding* flow = spec->sequence->flows; flow; flow = flow->next)
        bound |= !strcmp(flow->region_name, master->body ? master->body->name : "body");
    if (!bound) return paged_failure(composer->composition, TYPESET_INVALID, spec->sequence->source.address,
        static_cast<uint32_t>(index + 1), "selected page master has no bound body flow");
    spec->name = master->name;
    return TYPESET_OK;
}

static void paged_page_bind(ViewPageBox* page, const PagedPageSpec& spec) {
    page->sequence = lam::up(spec.sequence); page->sequence_page = spec.ordinal; page->folio = spec.folio;
    page->name = lam::up(spec.name);
}

static TypesetStatus paged_page_style(PagedComposer* composer, const PagedPageSpec& spec,
        uint32_t number, ViewPageSide side, bool blank, ViewPageStyle* style) {
    RadiantPageDiagnostic diagnostic = {};
    ViewModelStatus resolved = view_css_page_style(composer->tree, spec.name, number, side, blank, style, &diagnostic);
    if (resolved != VIEW_MODEL_OK) {
        if (diagnostic.source.address) return paged_failure(composer->composition, TYPESET_INVALID,
            diagnostic.source.address, number, diagnostic.reason);
        // selected control bindings keep their original source for transactional FO/native diagnostics.
        for (ViewCssStyle* selected = composer->tree->model->css->styles; selected; selected = selected->next)
            if (selected->binding_status != VIEW_MODEL_OK) return paged_style_traits_admit(composer->composition, selected);
        return resolved == VIEW_MODEL_OUT_OF_MEMORY ? TYPESET_OUT_OF_MEMORY : TYPESET_INVALID;
    }
    for (size_t i = 0; i <= RADIANT_REGION_EDGE_COUNT; i++) {
        bool body = i == RADIANT_REGION_EDGE_COUNT;
        const RadiantPageRegion* region = body ? style->body_region.get() : style->edge_regions[i].get();
        if (!region) continue;
        ViewCssStyle* computed = body ? style->body_style.get() : style->edge_style[i].get();
        ViewCssBoxEdges* box = body ? &style->body_box : &style->edge_boxes[i];
        if (!body && view_css_box_edges(composer->tree, computed, style->edge_rects[i].width, box) != VIEW_MODEL_OK)
            return paged_failure(composer->composition, TYPESET_INVALID, region->source.address, number,
                "region decoration requires supported padding and solid border styles");
        if (region->zero_box) for (float edge : box->edges) if (edge != 0.0f)
            return paged_failure(composer->composition, TYPESET_INVALID, region->source.address, number,
                "region box policy requires zero border and padding");
        const CssValue* overflow = view_css_property(composer->tree, computed, "overflow");
        if (overflow && !css_value_keyword_equals(overflow, CSS_VALUE_VISIBLE) &&
            !css_value_keyword_equals(overflow, CSS_VALUE_HIDDEN) && !css_value_keyword_equals(overflow, CSS_VALUE_CLIP))
            return paged_failure(composer->composition, TYPESET_INVALID, region->source.address, number,
                body ? "body region overflow requires visible, hidden or clip" : "static region overflow requires visible, hidden or clip");
        (body ? style->body_clip : style->edge_clip[i]) = overflow && !css_value_keyword_equals(overflow, CSS_VALUE_VISIBLE);
    }
    return TYPESET_OK;
}

static TypesetStatus paged_empty_page_restyle(PagedComposer* composer, bool blank, bool preserve_column = false) {
    ViewPageBox* page = composer->page;
    if (!page || composer->page_has_content || composer->sheet_has_content) return TYPESET_INVALID;
    TypesetStatus rebound = paged_reference_restyle(composer->composition, page->page_number);
    if (rebound != TYPESET_OK) return rebound;
    if (!view_tree_model_touch_node(composer->tree, &page->node)) return TYPESET_OUT_OF_MEMORY;
    ViewPageStyle* style = (ViewPageStyle*)pool_alloc(composer->composition->pool, sizeof(ViewPageStyle));
    if (!style) return TYPESET_OUT_OF_MEMORY;
    PagedPageSpec spec = {};
    TypesetStatus selected = paged_page_spec(composer, page->page_number - 1, blank, &spec);
    if (selected != TYPESET_OK) return selected;
    selected = paged_page_style(composer, spec, page->page_number, page->side, blank, style);
    if (selected != TYPESET_OK) return selected;
    bool same_name = spec.name == page->name.get() || (spec.name && page->name && !strcmp(spec.name, page->name));
    // replaying leading ancestors must not restart a bypassed column of the same selected master.
    uint32_t column = preserve_column && same_name && spec.sequence == page->sequence.get() && blank == page->blank
        ? composer->column_index : 0;
    page->style = lam::up(style); paged_page_bind(page, spec); page->blank = blank;
    page->node.rect.width = style->width; page->node.rect.height = style->height; page->content_rect = style->content_rect;
    composer->page_style = *style;
    composer->body_aligned = false;
    if (page->page_number == 1) composer->initial_containing_block = style->content_rect;
    selected = paged_column_select(composer, column);
    if (selected != TYPESET_OK) return selected;
    if (composer->depth) composer->column_root = composer->frames[0].fragment;
    float x = composer->page_style.content_rect.x, width = composer->page_style.content_rect.width, height = paged_root_height(composer);
    float reference_x = x, reference_width = width;
    for (size_t i = 0; i < composer->depth; i++) {
        PagedFrame* frame = &composer->frames[i];
        // deeper saved frames still carry the ordinary master's reference area until their turn.
        TypesetStatus status = paged_frame_descend(composer, frame, &x, &width, &height, &reference_x, &reference_width);
        if (status != TYPESET_OK) return status;
        frame->page_start = composer->y;
        if (!view_tree_model_touch_node(composer->tree, frame->fragment)) return TYPESET_OUT_OF_MEMORY;
        frame->fragment->rect = {frame->x, composer->y, frame->width, 0.0f};
        composer->y += paged_fragment_edge(frame->flow->style, frame->box, 0, frame->fragment->first_fragment);
    }
    // queued insertions must be measured again when the selected master's region geometry changes.
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
        typeset_region_plan_dispose(&composer->regions[i].plan);
        composer->regions[i].anchor_count = 0;
    }
    return paged_regions_prepare(composer);
}

static TypesetStatus paged_page_create(PagedComposer* composer, bool blank) {
    ViewTree* tree = composer->tree;
    if (tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
        composer->page_style.width = tree->model->environment.viewport_width;
        composer->page_style.height = tree->model->environment.viewport_height;
        composer->page_style.content_rect = {0.0f, 0.0f, composer->page_style.width, composer->page_style.height};
        composer->initial_containing_block = composer->page_style.content_rect;
        composer->bottom = INFINITY;
        return TYPESET_OK;
    }
    size_t count = tree->model->page_count;
    if (count >= composer->composition->options.max_pages) return TYPESET_BUDGET_EXHAUSTED;
    ViewPageSide side = paged_side(composer, count);
    PagedPageSpec spec = {};
    TypesetStatus selected = paged_page_spec(composer, count, blank, &spec);
    if (selected != TYPESET_OK) return selected;
    selected = paged_page_style(composer, spec, static_cast<uint32_t>(count + 1), side, blank, &composer->page_style);
    if (selected != TYPESET_OK) return selected;
    ViewPageStyle* style = (ViewPageStyle*)pool_alloc(composer->composition->pool, sizeof(ViewPageStyle));
    if (!style) return TYPESET_OUT_OF_MEMORY;
    *style = composer->page_style;
    composer->page = view_tree_page_append(tree, style->width, style->height, style->content_rect, side, blank);
    if (!composer->page) return TYPESET_OUT_OF_MEMORY;
    composer->page->style = lam::up(style);
    if (!count) composer->initial_containing_block = style->content_rect;
    paged_page_bind(composer->page, spec);
    composer->y = composer->page_start = style->content_rect.y;
    composer->page_has_content = composer->sheet_has_content = false;
    composer->policy_done = composer->auxiliary_page = false;
    composer->body_aligned = false;
    composer->retain_aux_tail = false;
    composer->space_count = composer->space_anchor_count = 0;
    composer->space_optimum_extra = 0.0f;
    return paged_column_select(composer, 0);
}

static bool paged_sequence_padding(const ViewPageBox* page, const RadiantPageSequence* following) {
    const RadiantPageSequence* sequence = page ? page->sequence.get() : nullptr;
    if (!sequence) return false;
    RadiantSequenceEnd end = sequence->end;
    if (end == RADIANT_END_AUTO && following && following->initial != RADIANT_FOLIO_AUTO) {
        bool odd = following->initial == RADIANT_FOLIO_ODD ||
            (following->initial == RADIANT_FOLIO_NUMBER && following->initial_folio % 2);
        end = odd ? RADIANT_END_ON_EVEN : RADIANT_END_ON_ODD;
    }
    return (end == RADIANT_END_EVEN && page->sequence_page % 2) ||
        (end == RADIANT_END_ODD && !(page->sequence_page % 2)) ||
        (end == RADIANT_END_ON_EVEN && page->folio % 2) || (end == RADIANT_END_ON_ODD && !(page->folio % 2));
}

static TypesetStatus paged_sequence_end(PagedComposer* composer, const RadiantPageSequence* following) {
    ViewPageBox* page = composer->page;
    if (!paged_sequence_padding(page, following)) return TYPESET_OK;
    const RadiantPageSequence* sequence = page->sequence;
    // end padding belongs to the completed sequence and participates in its terminal master selection.
    const RadiantPageSequence* pending = composer->sequence;
    const char* name = composer->page_name;
    uint32_t terminal = composer->terminal_page;
    composer->sequence = sequence; composer->page_name = sequence->master_reference;
    composer->terminal_page = static_cast<uint32_t>(composer->tree->model->page_count + 1);
    TypesetStatus status = paged_page_create(composer, true);
    if (status == TYPESET_OK) status = paged_page_policy_finish(composer);
    composer->sequence = pending; composer->page_name = name;
    composer->terminal_page = terminal;
    return status;
}

static TypesetStatus paged_fragmentainer_close(PagedComposer* composer) {
    // suspended body frames belong to the preceding column, not to an auxiliary-only continuation.
    TypesetStatus status = composer->auxiliary_page ? TYPESET_OK : paged_space_flush(composer, true);
    if (status == TYPESET_OK && !composer->auxiliary_page) status = paged_frames_close(composer);
    if (status == TYPESET_OK) status = paged_regions_close(composer);
    return status;
}

static TypesetStatus paged_fragmentainer_open(PagedComposer* composer, bool previous_blank) {
    for (size_t i = 0; i < composer->depth; i++) {
        bool first = previous_blank && composer->frames[i].fragment->first_fragment;
        TypesetStatus status = paged_frame_open(composer, i, first);
        if (status != TYPESET_OK) return status;
    }
    composer->pending_break = false; composer->requested_break = VIEW_BREAK_AUTO; composer->pending_margin = 0.0f;
    return paged_regions_prepare(composer);
}

static TypesetStatus paged_column_open(PagedComposer* composer) {
    composer->sheet_has_content |= composer->page_has_content;
    TypesetStatus status = paged_column_select(composer, composer->column_index + 1);
    if (status != TYPESET_OK) return status;
    composer->page_has_content = composer->body_aligned = false;
    composer->space_count = composer->space_anchor_count = 0; composer->space_optimum_extra = 0.0f;
    return paged_fragmentainer_open(composer, false);
}

static TypesetStatus paged_next_column(PagedComposer* composer) {
    TypesetStatus status = paged_fragmentainer_close(composer);
    return status == TYPESET_OK ? paged_column_open(composer) : status;
}

static TypesetStatus paged_next_page(PagedComposer* composer) {
    if (composer->tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
        composer->pending_break = false; return TYPESET_OK;
    }
    bool previous_blank = composer->page && composer->page->blank;
    if (composer->page) {
        bool changed_sequence = composer->page->sequence.get() != composer->sequence;
        TypesetStatus status = paged_fragmentainer_close(composer);
        if (status == TYPESET_OK && changed_sequence) status = paged_regions_drain(composer);
        if (status == TYPESET_OK && changed_sequence) status = paged_sequence_end(composer, composer->sequence);
        if (status != TYPESET_OK) return status;
    }
    ViewBreak request = composer->requested_break;
    composer->terminal_page = 0;
    ViewPageSide wanted = VIEW_PAGE_RIGHT;
    bool side_request = paged_requested_side(composer, request, &wanted);
    if (side_request && paged_side(composer, composer->tree->model->page_count) != wanted) {
        TypesetStatus status = paged_page_create(composer, true);
        if (status == TYPESET_OK) status = paged_page_policy_finish(composer);
        if (status != TYPESET_OK) return status;
    }
    TypesetStatus status = paged_page_create(composer, false);
    if (status != TYPESET_OK) return status;
    return paged_fragmentainer_open(composer, previous_blank);
}

struct PagedContentBinding {
    ViewTree* tree;
    PagedComposition* composition;
    ViewPageBox* page;
    ViewCssStyle* style;
    uint64_t footnote;
    PagedFlowNode* paragraph;
    size_t flushed;
};
static bool paged_counter_contains(const CounterSnapshot* snapshot, const char* name) {
    if (snapshot) for (size_t i = 0; i < snapshot->count; i++)
        if (strcmp(snapshot->entries[i].name, name) == 0) return true;
    return false;
}
static const CounterSnapshot* paged_content_counters(const PagedContentBinding* binding, const char* name) {
    ViewCssStyle* style = binding->style;
    // Page/margin bindings shadow the complete document counter stack of the same name.
    while (style && style->page_context) {
        if (paged_counter_contains(style->counters, name)) return style->counters;
        style = style->parent;
    }
    if (strcmp(name, "page") == 0 && binding->page && binding->page->style)
        return binding->page->style->computed_style->counters;
    return style ? style->counters.get() : nullptr;
}
static void paged_source_text(DomElement* root, StrBuf* text) {
    bool whitespace = false;
    DomNode* node = root->first_child;
    while (node) {
        if (node->is_text()) whitespace = strbuf_append_collapsed_ascii_whitespace(text,
            node->as_text()->text, node->as_text()->length, true, whitespace);
        DomNode* child = node->is_element() ? node->as_element()->first_child.get() : nullptr;
        if (child) { node = child; continue; }
        while (node != root && !node->next_sibling) node = node->parent;
        if (node == root) break;
        node = node->next_sibling;
    }
}

static TypesetStatus paged_targets_seed(ViewTree* tree, PagedReferenceSession* session,
        DomElement* source, size_t depth, size_t* visited, const PagedLayoutOptions* options) {
    if (depth > options->max_depth || ++*visited > options->max_nodes) return TYPESET_BUDGET_EXHAUSTED;
    const char* id = source->id;
    // HTML duplicate IDs bind the first element in document order.
    if (id && *id && !typeset_target_find(&session->targets, id)) {
        PagedTargetValue* value = (PagedTargetValue*)pool_calloc(session->pool, sizeof(PagedTargetValue));
        if (!value) return TYPESET_OUT_OF_MEMORY;
        value->provider = tree->model->tree_id; value->source = dom_node_ref(source);
        value->counters = counter_snapshot_copy(nullptr, session->pool);
        StrBuf* text = strbuf_new();
        if (!text || !value->counters) { if (text) strbuf_free(text); return TYPESET_OUT_OF_MEMORY; }
        paged_source_text(source, text);
        value->text = pool_dup_n(session->pool, text->str ? text->str : "", text->length);
        strbuf_free(text);
        if (!value->text) return TYPESET_OUT_OF_MEMORY;
        value->before = value->after = "";
        TypesetTarget target = {id, {tree->model->tree_id, tree->layout_generation,
            value->source.expected_id, TYPESET_PROVIDER_OFFSETS, nullptr}, 0, lam::up((const TypesetRecord*)value)};
        target.binding = target.value;
        TypesetStatus status = typeset_target_append(&session->targets, &target);
        if (status != TYPESET_OK) return status;
    }
    for (DomNode* child = source->first_child; child; child = child->next_sibling) if (child->is_element()) {
        TypesetStatus status = paged_targets_seed(tree, session, child->as_element(), depth + 1, visited, options);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_native_targets_seed(ViewTree* tree, PagedReferenceSession* session,
        const PagedLayoutOptions& options) {
    TypesetStatus status = paged_native_namespaces(session->pool, options, &session->targets.sources);
    for (size_t i = 0; status == TYPESET_OK && i < options.native_flow_count; i++) {
        const PagedNativeFlowBinding& flow = options.native_flows[i];
        for (size_t j = 0; status == TYPESET_OK && j < flow.target_count; j++) {
            TypesetTarget target = flow.targets[j];
            if (target.source.provider != flow.provider.identity || target.source.generation != flow.provider.generation ||
                target.source.offset_unit > TYPESET_PROVIDER_OFFSETS ||
                (target.source.native && target.source.native->provider != target.source.provider) ||
                (target.value && target.value->provider != target.source.provider)) return TYPESET_INVALID;
            PagedTargetValue* value = (PagedTargetValue*)pool_calloc(session->pool, sizeof(PagedTargetValue));
            if (!value) return TYPESET_OUT_OF_MEMORY;
            value->provider = tree->model->tree_id; value->text = value->before = value->after = "";
            value->counters = counter_snapshot_copy(nullptr, session->pool);
            if (!value->counters) return TYPESET_OUT_OF_MEMORY;
            target.page_number = target.last_page_number = 0; target.binding = lam::up((const TypesetRecord*)value);
            status = typeset_target_append(&session->targets, &target);
        }
    }
    return status;
}

static const TypesetTarget* paged_content_target(PagedContentBinding* binding, const CssValue* argument) {
    const char* url = argument && argument->type == CSS_VALUE_TYPE_STRING ? argument->data.string :
        argument && argument->type == CSS_VALUE_TYPE_URL ? argument->data.url : nullptr;
    const char* type = nullptr;
    const char* attribute = css_content_attribute_name(argument, &type);
    if (type && !str_ieq_cstr(type, "url") && !str_ieq_cstr(type, "string")) return nullptr;
    if (attribute) url = binding->style->source->get_attribute(attribute);
    PagedComposition* composition = binding->composition;
    composition->reference_used = true;
    composition->reference_source = dom_node_ref(binding->style->source);
    const char* fragment = url && url[0] == '#' ? url + 1 : nullptr;
    Url* resolved = nullptr;
    if (!fragment && url && binding->tree->model->document->url) {
        resolved = url_parse_with_base(url, binding->tree->model->document->url);
        if (resolved && url_equals_without_fragment(resolved, binding->tree->model->document->url)) {
            const char* hash = url_get_hash(resolved);
            if (hash && hash[0] == '#') fragment = hash + 1;
        }
    }
    if (!fragment || !*fragment) {
        if (resolved) url_destroy(resolved);
        paged_failure(composition, TYPESET_INVALID, binding->style->source, 0, "target reference requires a local fragment URL");
        return nullptr;
    }
    size_t length = strlen(fragment);
    lam::Temp<char> decoded((char*)mem_alloc(length + 1, MEM_CAT_LAYOUT));
    const TypesetTarget* target = nullptr;
    if (decoded) {
        length = url_decode_lenient_write(fragment, length, false, decoded.get());
        decoded.get()[length] = '\0';
        // an encoded NUL must never alias a shorter source ID.
        if (!memchr(decoded.get(), '\0', length) && utf8_valid(decoded.get(), length))
            target = typeset_target_find(&composition->references->targets, decoded.get());
    }
    if (resolved) url_destroy(resolved);
    if (!decoded) {
        paged_failure(composition, TYPESET_OUT_OF_MEMORY, binding->style->source, 0, "target fragment decoding allocation failed");
        return nullptr;
    }
    if (!target) paged_failure(composition, TYPESET_INVALID, binding->style->source, 0, "target reference has no matching source ID");
    return target;
}

static bool paged_content_target_function(PagedContentBinding* binding, const CssFunction* function, StrBuf* text) {
    bool target_text = css_function_name_is(function, "target-text");
    bool plural = css_function_name_is(function, "target-counters");
    size_t required = target_text ? 1 : plural ? 3 : 2;
    if ((size_t)function->arg_count < required || (size_t)function->arg_count > required + 1) return false;
    const TypesetTarget* target = paged_content_target(binding, function->args[0]);
    if (!target || !target->binding || target->binding->provider != binding->tree->model->tree_id) return false;
    const PagedTargetValue* value = (const PagedTargetValue*)target->binding.get();
    if (target_text) {
        const char* part = function->arg_count == 2 ? css_value_identifier_name(function->args[1]) : "content";
        const char* result = part && str_ieq_cstr(part, "content") ? value->text :
            part && str_ieq_cstr(part, "before") ? value->before : part && str_ieq_cstr(part, "after") ? value->after : nullptr;
        if (!result) return false;
        strbuf_append_str(text, result); return true;
    }
    const char* name = css_value_identifier_name(function->args[1]);
    if (!name) return false;
    const char* separator = nullptr;
    if (plural) {
        if (function->args[2]->type != CSS_VALUE_TYPE_STRING) return false;
        separator = function->args[2]->data.string;
    }
    CssEnum style = CSS_VALUE_DECIMAL;
    if ((size_t)function->arg_count > required) {
        if (function->args[required]->type != CSS_VALUE_TYPE_KEYWORD) return false;
        style = function->args[required]->data.keyword;
    }
    if (strcmp(name, "page") == 0 || strcmp(name, "pages") == 0) {
        if (!target->page_number && binding->composition->references->pass > 1) {
            paged_failure(binding->composition, TYPESET_UNPLACEABLE, binding->style->source, 0,
                "target page reference has no placed content box");
            return false;
        }
        if (strcmp(name, "page") == 0) {
            // Reference passes retain the target's label snapshot independently of its physical index.
            return value->page_counters ? counter_snapshot_append(value->page_counters, name, separator, style, text) :
                counter_value_append(0, style, text);
        }
        size_t number = binding->composition->references->page_count;
        if (number > INT_MAX) return false;
        return counter_value_append((int)number, style, text); // INT_CAST_OK: bounded target counter value.
    }
    if (strcmp(name, "footnote") == 0) return false;
    return counter_snapshot_append(value->counters, name, separator, style, text);
}
static const char* paged_content_attribute(void* context, const char* name) {
    return ((PagedContentBinding*)context)->style->source->get_attribute(name);
}
static const char* paged_content_quote(void* context, bool open, int depth) {
    return css_content_quote_char(((PagedContentBinding*)context)->style->quotes, open, depth);
}
static bool paged_content_mark(PagedContentBinding* binding, const CssFunction* function,
        TypesetMarkKind kind, const TypesetMark** result) {
    if (!binding->page || function->arg_count < 1 || function->arg_count > 2) return false;
    const char* name = css_value_identifier_name(function->args[0]);
    const char* choice = function->arg_count == 2 ? css_value_identifier_name(function->args[1]) : "first";
    if (!name || !choice) return false;
    static const char* names[] = {"first", "start", "last", "first-except"};
    size_t selected = 0;
    while (selected < 4 && !str_ieq_cstr(choice, names[selected])) selected++;
    if (selected == 4) return false;
    *result = typeset_mark_select(&binding->composition->marks, kind, name,
        binding->page->page_number, static_cast<TypesetMarkSelection>(selected));
    return true;
}
static bool paged_content_flush(PagedContentBinding* binding, StrBuf* text) {
    if (text->length == binding->flushed) return true;
    // The evaluator may grow its StrBuf again; item payloads require a stable owned slice.
    char* copy = pool_dup_n(binding->composition->pool, text->str + binding->flushed, text->length - binding->flushed);
    TypesetStatus status = copy ? paged_text_items(binding->tree, binding->composition, binding->paragraph,
        binding->style->source, copy, text->length - binding->flushed, binding->style, true) : TYPESET_OUT_OF_MEMORY;
    binding->flushed = text->length;
    if (status != TYPESET_OK) paged_failure(binding->composition, status, binding->style->source, 0,
        "generated inline content could not be shaped");
    return status == TYPESET_OK;
}

static bool paged_content_leader(PagedContentBinding* binding, const CssFunction* function, StrBuf* text) {
    if (!binding->paragraph || function->arg_count != 1) return false;
    const CssValue* argument = function->args[0];
    const char* name = css_value_identifier_name(argument);
    const char* pattern = argument && argument->type == CSS_VALUE_TYPE_STRING ? argument->data.string :
        name && str_ieq_cstr(name, "dotted") ? "." : name && str_ieq_cstr(name, "solid") ? "_" :
        name && str_ieq_cstr(name, "space") ? " " : nullptr;
    if (!pattern || !*pattern || !paged_content_flush(binding, text)) return false;
    StrBuf* normalized = strbuf_new();
    if (!normalized) {
        paged_failure(binding->composition, TYPESET_OUT_OF_MEMORY, binding->style->source, 0, "leader pattern allocation failed");
        return false;
    }
    // A pattern's line breaks are ignored; the remaining whitespace follows CSS collapsing.
    bool whitespace = false;
    for (size_t i = 0; pattern[i]; i++) if (pattern[i] != '\n' && pattern[i] != '\r')
        whitespace = strbuf_append_collapsed_ascii_whitespace(normalized, pattern + i, 1, true, whitespace);
    char* owned = pool_dup_n(binding->composition->pool, normalized->str ? normalized->str : "", normalized->length);
    size_t length = normalized->length; strbuf_free(normalized);
    if (!owned) {
        paged_failure(binding->composition, TYPESET_OUT_OF_MEMORY, binding->style->source, 0, "leader pattern allocation failed");
        return false;
    }
    if (!length) return false;
    TypesetItem item = {}; item.kind = TYPESET_GLUE;
    TypesetStatus status = paged_text_payload(binding->tree, binding->composition, binding->style->source,
        owned, length, binding->style, &item);
    if (status != TYPESET_OK) {
        paged_failure(binding->composition, status, binding->style->source, 0, "leader pattern could not be shaped");
        return false;
    }
    if (item.metrics.advance <= 0.0f) return false;
    ((PagedTextPaint*)item.paint.get())->leader = true;
    item.glue = {item.metrics.advance, 1.0f, 0.0f, 1, 0, false, false};
    item.boundary = {TYPESET_BREAK_FORBIDDEN, TYPESET_BREAK_LINE, 0, 0};
    item.source = paged_source_identity(binding->tree, binding->composition, binding->style->source, TYPESET_PROVIDER_OFFSETS);
    if (!item.source.native) return false;
    status = paged_item_append(binding->composition, binding->paragraph, item);
    if (status != TYPESET_OK) paged_failure(binding->composition, status, binding->style->source, 0,
        "leader exhausted its contribution budget");
    return status == TYPESET_OK;
}
static bool paged_content_function(void* context, const CssFunction* function, StrBuf* text) {
    PagedContentBinding* binding = (PagedContentBinding*)context;
    if (css_function_name_is(function, "leader")) return paged_content_leader(binding, function, text);
    if (css_function_name_is(function, "target-counter") || css_function_name_is(function, "target-counters") ||
        css_function_name_is(function, "target-text")) return paged_content_target_function(binding, function, text);
    if (css_function_name_is(function, "content")) {
        const char* part = function->arg_count ? css_value_identifier_name(function->args[0]) : "text";
        if (function->arg_count > 1 || !part) return false;
        if (str_ieq_cstr(part, "before") || str_ieq_cstr(part, "after")) {
            ViewCssStyle* pseudo = view_css_resolve_pseudo(binding->tree, binding->style->source,
                str_ieq_cstr(part, "before") ? PSEUDO_ELEMENT_BEFORE : PSEUDO_ELEMENT_AFTER);
            if (!pseudo) return false;
            if (pseudo->generated_text) strbuf_append_str(text, pseudo->generated_text);
            return true;
        }
        if (!str_ieq_cstr(part, "text")) return false;
        // The source text is copied once; repeating a header never re-enters its source.
        paged_source_text(binding->style->source, text);
        return true;
    }
    if (css_function_name_is(function, "string")) {
        const TypesetMark* mark = nullptr;
        if (!paged_content_mark(binding, function, TYPESET_MARK_STRING, &mark)) return false;
        if (mark && mark->text) strbuf_append_str(text, mark->text);
        return true;
    }
    bool plural = css_function_name_is(function, "counters");
    if ((!plural && !css_function_name_is(function, "counter")) || function->arg_count < (plural ? 2 : 1) ||
        function->arg_count > (plural ? 3 : 2)) return false;
    const char* name = css_value_identifier_name(function->args[0]);
    if (!name) return false;
    if ((!binding->page && (strcmp(name, "page") == 0 || strcmp(name, "pages") == 0)) ||
        (!binding->footnote && strcmp(name, "footnote") == 0)) return false;
    const char* separator = nullptr;
    if (plural) {
        if (function->args[1]->type != CSS_VALUE_TYPE_STRING) return false;
        separator = function->args[1]->data.string;
    }
    size_t format = plural ? 2 : 1;
    CssEnum style = CSS_VALUE_DECIMAL;
    if ((size_t)function->arg_count > format) {
        if (function->args[format]->type != CSS_VALUE_TYPE_KEYWORD) return false;
        style = function->args[format]->data.keyword;
    }
    uint64_t value = strcmp(name, "footnote") == 0 && binding->footnote ? binding->footnote :
        strcmp(name, "pages") == 0 && binding->page ? binding->tree->model->page_count : UINT64_MAX;
    if (value == UINT64_MAX) return counter_snapshot_append(paged_content_counters(binding, name), name, separator, style, text);
    if (value > INT_MAX) return false;
    return counter_value_append((int)value, style, text); // INT_CAST_OK: bounded publishing counter, not a dimension.
}

static TypesetStatus paged_content_text(ViewTree* tree, PagedComposition* composition, ViewCssStyle* style,
        ViewPageBox* page, const CssValue* content, uint64_t footnote, char** result, size_t* length, PagedFlowNode* paragraph) {
    PagedContentBinding binding = {tree, composition, page, style, footnote, paragraph, 0};
    CssContentBindings callbacks = {&binding, paged_content_attribute, paged_content_quote, paged_content_function};
    StrBuf* text = strbuf_new();
    if (!text) return TYPESET_OUT_OF_MEMORY;
    int quotes = 0;
    // Source generation shares quote depth; repeated page furniture has its own evaluation.
    bool ok = css_content_append(content, &callbacks, page ? &quotes : &composition->counters->quote_depth, text);
    *length = text->length;
    *result = ok ? pool_dup_n(composition->pool, text->str ? text->str : "", text->length) : nullptr;
    TypesetStatus status = TYPESET_OK;
    if (ok && *result && paragraph) status = paged_text_items(tree, composition, paragraph, style->source,
        *result + binding.flushed, text->length - binding.flushed, style, true);
    strbuf_free(text);
    if (status != TYPESET_OK) return status;
    return !ok ? (composition->diagnostic.status != TYPESET_OK ? composition->diagnostic.status : TYPESET_INVALID) :
        *result ? TYPESET_OK : TYPESET_OUT_OF_MEMORY;
}

static TypesetStatus paged_mark_group(PagedComposer* composer, PagedMarkSource* owner,
        const CssValue* group) {
    if (!group || group->type != CSS_VALUE_TYPE_LIST || group->data.list.comma_separated || group->data.list.count < 2)
        return TYPESET_INVALID;
    const char* name = css_value_identifier_name(group->data.list.values[0]);
    if (!name || !*name) return TYPESET_INVALID;
    CssValue content = *group; content.data.list.values++; content.data.list.count--;
    char* value = nullptr; size_t length = 0;
    TypesetStatus status = paged_content_text(composer->tree, composer->composition, owner->style,
        composer->page, &content, 0, &value, &length);
    if (status != TYPESET_OK) return status;
    TypesetMark mark = {};
    mark.kind = TYPESET_MARK_STRING; mark.name = name; mark.text = value;
    mark.source = {composer->tree->model->tree_id, composer->tree->layout_generation,
        dom_node_ref(owner->source).expected_id, TYPESET_PROVIDER_OFFSETS, nullptr};
    mark.page_number = composer->page->page_number;
    mark.at_page_start = !composer->page_has_content && !composer->sheet_has_content;
    return typeset_mark_append(&composer->composition->marks, &mark);
}

static bool paged_source_has_own_area(const ViewCssStyle* style) {
    if (style && style->pseudo_element) style = style->parent.get();
    return !style || !style->flow_traits || !style->flow_traits->descendant_areas;
}

static TypesetStatus paged_mark_enter(PagedComposer* composer, DomNode* source, size_t depth = 0, bool area = true) {
    if (!composer->page || !source) return TYPESET_OK;
    if (depth > composer->composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    if (!depth && source->is_element()) {
        ViewNodeState* state = view_tree_node_state(composer->tree, source, false);
        area = paged_source_has_own_area(state ? state->computed_style.get() : nullptr);
    }
    if (source->parent) {
        TypesetStatus status = paged_mark_enter(composer, source->parent, depth + 1, area);
        if (status != TYPESET_OK) return status;
    }
    PagedMarkSource key = {}; key.source = source;
    PagedMarkSource* owner = (PagedMarkSource*)hashmap_get(composer->composition->mark_sources, &key);
    if (!owner) return TYPESET_OK;
    if (!view_tree_model_record(composer->tree, owner, sizeof(*owner))) return TYPESET_OUT_OF_MEMORY;
    if (area && owner->style->display.outer != CSS_VALUE_NONE &&
        owner->style->float_value != CSS_VALUE_FOOTNOTE && owner->style->float_value != CSS_VALUE_TOP &&
        owner->style->float_value != CSS_VALUE_BOTTOM && !paged_running_name(owner->style))
    {
        if (!owner->target_page) owner->target_page = composer->page->page_number;
        owner->last_target_page = composer->page->page_number;
    }
    TypesetMarkStore* marks = &composer->composition->marks;
    uint32_t source_id = dom_node_ref(source).expected_id;
    TypesetSource identity = {composer->tree->model->tree_id, composer->tree->layout_generation,
        source_id, TYPESET_PROVIDER_OFFSETS, nullptr};
    // Count restoration also invalidates the adapter's assignment cursor.
    if (owner->assigned_at < marks->count && typeset_source_same_identity(marks->entries[owner->assigned_at].source, identity))
        return TYPESET_OK;
    size_t assignment = marks->count;
    const CssValue* value = owner->style->string_set;
    TypesetStatus status = TYPESET_OK;
    if (value && !css_value_is_none(value)) {
        if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.comma_separated) {
            for (int i = 0; status == TYPESET_OK && i < value->data.list.count; i++)
                status = paged_mark_group(composer, owner, value->data.list.values[i]);
        } else status = paged_mark_group(composer, owner, value);
    }
    if (status != TYPESET_OK) return paged_failure(composer->composition, status, source,
        composer->page->page_number, "unresolved named-string assignment");
    if (owner->running) {
        TypesetMark mark = {};
        mark.kind = TYPESET_MARK_RUNNING; mark.name = paged_running_name(owner->style);
        mark.source = {composer->tree->model->tree_id, composer->tree->layout_generation,
            dom_node_ref(source).expected_id, TYPESET_PROVIDER_OFFSETS, nullptr};
        mark.value = lam::up((const TypesetRecord*)owner->running);
        mark.page_number = composer->page->page_number; mark.at_page_start = !composer->page_has_content && !composer->sheet_has_content;
        status = typeset_mark_append(&composer->composition->marks, &mark);
        if (status != TYPESET_OK) return status;
    }
    owner->assigned_at = assignment;
    return TYPESET_OK;
}

static TypesetStatus paged_leader_run(PagedComposer* composer, const PagedTextPaint* payload,
        float x, float width, float edge, float step, PaintGlyphRun* run) {
    if (!isfinite(step) || step <= 0.0f || !isfinite(width) || width <= 0.0f) return TYPESET_UNPLACEABLE;
    // Tile from the line's end edge; hide complete patterns intersecting either adjacent text run.
    float hidden = ceilf(fmaxf(0.0f, (edge - x - width) / step));
    float end = edge - hidden * step;
    float copies = floorf(fmaxf(0.0f, (end - x) / step));
    size_t glyphs_per_copy = (size_t)payload->run.count, bytes_per_copy = (size_t)payload->run.text_len;
    if (copies < 1.0f) return paged_failure(composer->composition, TYPESET_UNPLACEABLE, payload->source,
        composer->page ? composer->page->page_number : 0, "leader has no complete pattern in its line");
    if (!glyphs_per_copy || !bytes_per_copy || copies > (float)(composer->composition->options.max_items / glyphs_per_copy) ||
        copies > (float)(INT_MAX / bytes_per_copy)) return TYPESET_BUDGET_EXHAUSTED;
    size_t repetitions = static_cast<size_t>(copies);
    size_t count = repetitions * glyphs_per_copy, length = repetitions * bytes_per_copy;
    if (count > INT_MAX || length > INT_MAX || count > SIZE_MAX / sizeof(float) ||
        count > composer->composition->options.max_items - composer->composition->generated_glyphs) return TYPESET_BUDGET_EXHAUSTED;
    composer->composition->generated_glyphs += count;
    Arena* arena = composer->tree->model->arena;
    uint32_t* glyphs = (uint32_t*)arena_alloc(arena, count * sizeof(uint32_t));
    float* xs = (float*)arena_alloc(arena, count * sizeof(float));
    float* ys = (float*)arena_alloc(arena, count * sizeof(float));
    char* text = (char*)arena_alloc(arena, length + 1);
    if (!glyphs || !xs || !ys || !text) return TYPESET_OUT_OF_MEMORY;
    for (size_t i = 0; i < repetitions; i++) {
        memcpy(text + i * bytes_per_copy, payload->run.text, bytes_per_copy);
        for (size_t j = 0; j < glyphs_per_copy; j++) {
            size_t index = i * glyphs_per_copy + j;
            glyphs[index] = payload->run.glyph_ids.get()[j];
            xs[index] = (float)i * step + payload->run.xs.get()[j];
            ys[index] = payload->run.ys.get()[j];
        }
    }
    text[length] = '\0'; run->x = end - (float)repetitions * step;
    run->glyph_ids = lam::up(glyphs); run->xs = lam::up(xs); run->ys = lam::up(ys);
    run->text = lam::up(text);
    run->count = (int)count; // INT_CAST_OK: bounded repeated glyph count.
    run->text_len = (int)length; // INT_CAST_OK: bounded generated UTF-8 byte count.
    return TYPESET_OK;
}

static const CssValue* paged_inline_vertical_align(ViewCssStyle* parent, ViewCssStyle* style) {
    // a block's own alignment positions its box, not the anonymous inline text inside it.
    return parent == style ? nullptr : style->vertical_align.get();
}

static float paged_vertical_top(PagedComposer* composer, ViewCssStyle* parent, ViewCssStyle* style,
        float extent, float item_baseline, float line_height, float baseline, float width) {
    LayoutContext context = {}; context.doc = composer->tree->model->document;
    context.line.parent_font_style = lam::up(&parent->font);
    context.line.parent_font_size = parent->font.font_size;
    if (!radiant_text_metrics(parent, &context.line.parent_font_ascender, &context.line.parent_font_descender)) return NAN;
    const CssValue* align = paged_inline_vertical_align(parent, style);
    CssEnum keyword = align && align->type == CSS_VALUE_TYPE_KEYWORD ? align->data.keyword : CSS_VALUE_BASELINE;
    float offset = !align ? 0.0f : align->type == CSS_VALUE_TYPE_PERCENTAGE
        ? (float)(align->data.percentage.value * 0.01) * style->line_height
        : paged_used_length(composer, style, align, CSS_PROPERTY_VERTICAL_ALIGN, width);
    return calculate_vertical_align_offset(&context, keyword, extent, line_height, baseline, item_baseline, offset);
}

struct PagedLineMeasurement { PagedComposer* composer; PagedFlowNode* flow; float containing_height, atomic_height; };

static RadiantLineStacking paged_line_stacking(const ViewCssStyle* style) {
    return style->flow_traits ? style->flow_traits->line_stacking : RADIANT_LINE_STACK_CSS;
}

static size_t paged_stacked_line_alternatives(const TypesetParagraph* paragraph, size_t first,
        float width, TypesetLineCandidate* candidates, size_t capacity, void* context) {
    size_t count = typeset_line_alternatives(paragraph, first, width, candidates, capacity, context);
    ViewCssStyle* style = ((PagedLineMeasurement*)context)->flow->style;
    float altitude, depth;
    if (!radiant_text_metrics(style, &altitude, &depth)) return 0;
    float half_leading = (style->line_height - altitude - depth) * 0.5f;
    for (size_t i = 0; i < count && i < capacity; i++) {
        TypesetLineCandidate& line = candidates[i];
        if (line.paint_first == line.paint_end) continue;
        if (paged_line_stacking(style) == RADIANT_LINE_STACK_FONT) {
            line.height = style->line_height; line.baseline = altitude + half_leading;
        } else {
            // max-height adds block leading after enclosing the raw inline allocation rectangles.
            line.height += 2.0f * half_leading; line.baseline += half_leading;
        }
        line.depth = fmaxf(0.0f, line.height - line.baseline);
    }
    return count;
}

static TypesetStatus paged_current_folio_payload(PagedComposer* composer, const PagedTextPaint* payload,
        const PagedTextPaint** result) {
    *result = payload;
    if (!payload->current_folio || !composer->page) return TYPESET_OK;
    const RadiantPageSequence* sequence = composer->page->sequence;
    uint32_t folio = composer->page->folio;
    PagedTextPaint* owner = const_cast<PagedTextPaint*>(payload);
    for (PagedTextPaint* variant = owner->query_variants; variant; variant = variant->query_next)
        if (variant->query_sequence == sequence && variant->query_folio == folio) { *result = variant; return TYPESET_OK; }
    StrBuf* label = strbuf_new();
    if (!label) return TYPESET_OUT_OF_MEMORY;
    PagedReferenceSession* references = composer->composition->references;
    bool formatted = true;
    if (!sequence && references && composer->page->page_number <= references->page_label_count)
        strbuf_append_str(label, references->page_labels[composer->page->page_number - 1]);
    else formatted = radiant_folio_append(sequence ? &sequence->format : nullptr, folio, label);
    char* owned = formatted ? pool_dup_n(composer->composition->pool, label->str, label->length) : nullptr;
    size_t length = label->length; strbuf_free(label);
    if (!owned) return TYPESET_OUT_OF_MEMORY;
    size_t glyphs = utf8_count(owned, length);
    if (glyphs > composer->composition->options.max_items - composer->composition->query_glyphs) return TYPESET_BUDGET_EXHAUSTED;
    TypesetItem item = {};
    TypesetStatus status = paged_text_payload(composer->tree, composer->composition, payload->source, owned, length, payload->style, &item);
    if (status != TYPESET_OK) return status;
    composer->composition->query_glyphs += glyphs;
    PagedTextPaint* variant = (PagedTextPaint*)item.paint.get();
    variant->query_sequence = sequence; variant->query_folio = folio; variant->query_metrics = item.metrics;
    variant->query_next = owner->query_variants; owner->query_variants = variant;
    *result = variant;
    return TYPESET_OK;
}

static RadiantKeepStrength paged_inline_keep(PagedFlowNode* flow, size_t first, size_t end, size_t next, size_t scope) {
    if (next == flow->paragraph.count) return {};
    const ViewCssStyle* left = nullptr; const ViewCssStyle* right = nullptr;
    for (size_t i = end; i > first && !left; i--) left = paged_item_style(flow->items[i - 1]);
    for (size_t i = next; i < flow->paragraph.count && !right; i++) right = paged_item_style(flow->items[i]);
    const ViewCssStyle* common = view_css_common_ancestor(left, right);
    RadiantKeepStrength keep = {};
    // block keeps constrain their block areas; inline keeps constrain material inside a line/page.
    for (const ViewCssStyle* owner = common; owner && owner->display.outer != CSS_VALUE_BLOCK; owner = owner->parent)
        if (owner->flow_traits) keep = radiant_keep_max(keep, owner->flow_traits->together.scope[scope]);
    for (const ViewCssStyle* owner = left; owner && owner != common; owner = owner->parent)
        if (owner->flow_traits) keep = radiant_keep_max(keep, owner->flow_traits->next.scope[scope]);
    for (const ViewCssStyle* owner = right; owner && owner != common; owner = owner->parent)
        if (owner->flow_traits) keep = radiant_keep_max(keep, owner->flow_traits->previous.scope[scope]);
    return keep;
}

static size_t paged_choose_kept_line(const TypesetLineCandidate* candidates, size_t count, void* context) {
    PagedFlowNode* flow = ((PagedLineMeasurement*)context)->flow;
    size_t selected = SIZE_MAX; RadiantKeepStrength weakest = {};
    for (size_t i = 0; i < count; i++) {
        if (candidates[i].overflow) continue;
        if (candidates[i].forced) return i;
        const TypesetLineCandidate& line = candidates[i];
        RadiantKeepStrength keep = paged_inline_keep(flow, line.paint_first, line.paint_end, line.next, 0);
        if (keep.kind == RADIANT_KEEP_ALWAYS) continue;
        if (selected == SIZE_MAX || radiant_keep_compare(keep, weakest) <= 0) { selected = i; weakest = keep; }
    }
    return selected;
}
static bool paged_line_item_metrics(const TypesetParagraph* paragraph, size_t index, float width,
        TypesetMetrics* metrics, void* context) {
    PagedLineMeasurement* measurement = (PagedLineMeasurement*)context;
    const TypesetItem& item = paragraph->items[index];
    const PagedPaint* payload = (const PagedPaint*)item.paint.get();
    if (!payload) return true;
    if (payload->kind == PAGED_PAINT_IMAGE) {
        const PagedImagePaint* image = (const PagedImagePaint*)payload;
        if (image->framed) {
            if (!isfinite(measurement->atomic_height)) return false;
            *metrics = {width, measurement->atomic_height, 0.0f, measurement->atomic_height, {}, nullptr};
        } else {
            PagedImageMeasure measured = {};
            if (paged_image_measure(measurement->composer, image, width, measurement->containing_height, &measured) != TYPESET_OK) return false;
            float advance = measured.width + measured.box.edges[3] + measured.box.edges[1] + measured.margin[3] + measured.margin[1];
            float height = measured.height + measured.box.edges[0] + measured.box.edges[2] + measured.margin[0] + measured.margin[2];
            *metrics = {advance, height, 0.0f, height, {}, nullptr};
        }
        if (image->framed) return true;
    } else if (((const PagedTextPaint*)payload)->current_folio) {
        const PagedTextPaint* resolved = nullptr;
        TypesetStatus status = paged_current_folio_payload(measurement->composer, (const PagedTextPaint*)payload, &resolved);
        if (status != TYPESET_OK) {
            paged_failure(measurement->composer->composition, status, payload->source,
                measurement->composer->page ? measurement->composer->page->page_number : 0, "current folio could not be shaped");
            return false;
        }
        if (resolved != payload) *metrics = resolved->query_metrics;
    }
    ViewCssStyle* parent = measurement->flow->style;
    float altitude, depth;
    if (!radiant_text_metrics(parent, &altitude, &depth)) return false;
    float parent_height = paged_line_stacking(parent) == RADIANT_LINE_STACK_CSS
        ? parent->line_height : altitude + depth;
    float baseline = altitude + (parent_height - altitude - depth) * 0.5f;
    float extent = metrics->height + metrics->depth;
    const CssValue* align = paged_inline_vertical_align(parent, payload->style);
    CssEnum keyword = align && align->type == CSS_VALUE_TYPE_KEYWORD ? align->data.keyword : CSS_VALUE_BASELINE;
    float top = paged_vertical_top(measurement->composer, parent, payload->style,
        extent, metrics->baseline, parent_height, baseline, width);
    if (keyword == CSS_VALUE_TOP || keyword == CSS_VALUE_BOTTOM) {
        // top/bottom align to the completed line; they require height without moving its strut.
        metrics->depth = fmaxf(0.0f, parent_height - baseline);
        metrics->height = fmaxf(0.0f, extent - metrics->depth);
    } else {
        metrics->height = fmaxf(0.0f, baseline - top);
        metrics->depth = fmaxf(0.0f, extent - metrics->height);
    }
    return true;
}
static TypesetStatus paged_next_line(PagedComposer* composer, PagedFlowNode* flow, size_t first,
        float width, float containing_height, TypesetLineCandidate* scratch, size_t capacity, TypesetLineCandidate* result,
        float atomic_height = NAN) {
    TypesetParagraph paragraph = flow->paragraph;
    PagedLineMeasurement measurement = {composer, flow, containing_height, atomic_height};
    if (flow->has_line_keeps) { paragraph.context = &measurement; paragraph.choose = paged_choose_kept_line; }
    bool atomic = flow->parent && flow->parent->image && flow->parent->image->framed;
    bool stacked = !atomic && paged_line_stacking(flow->style) != RADIANT_LINE_STACK_CSS;
    const RadiantFlowTraits* traits = flow->style->flow_traits.get();
    bool nominal = traits && (traits->text_metrics_set[0] || traits->text_metrics_set[1]);
    if (flow->has_dynamic_items || stacked || nominal) {
        paragraph.context = &measurement; paragraph.measure = paged_line_item_metrics;
        if (!atomic && (paragraph.minimum_line_height > 0.0f || stacked || nominal)) {
            float altitude, depth;
            if (!radiant_text_metrics(flow->style, &altitude, &depth)) return TYPESET_UNPLACEABLE;
            paragraph.minimum_baseline = altitude + (flow->style->line_height - altitude - depth) * 0.5f;
            paragraph.baseline_aware = true;
            if (stacked) {
                paragraph.minimum_line_height = altitude + depth;
                paragraph.minimum_baseline = altitude;
                paragraph.alternatives = paged_stacked_line_alternatives;
            }
        }
    }
    return typeset_next_line(&paragraph, first, width, scratch, capacity, result);
}

static float paged_line_baseline(PagedFlowNode* flow, const TypesetLineCandidate& line) {
    if (line.baseline > 0.0f) return line.baseline;
    float altitude, depth;
    return radiant_text_metrics(flow->style, &altitude, &depth) ? (line.height - altitude - depth) * 0.5f + altitude : NAN;
}

static TypesetStatus paged_commit_line(PagedComposer* composer, PagedFlowNode* flow,
        const TypesetLineCandidate& line) {
    PagedFrame* frame = &composer->frames[composer->depth - 1];
    LayoutViewNode* paragraph = view_tree_fragment_append(composer->tree, frame->fragment, flow->source,
        {frame->content_x, composer->y, frame->content_width, line.height});
    if (!paragraph) return TYPESET_OUT_OF_MEMORY;
    paragraph->role = composer->role;
    float x = frame->content_x;
    ViewCssStyle* style = flow->style;
    bool expand = line.packing.order && !line.packing.shrinking;
    float used_width = expand ? frame->content_width : line.width;
    if (style->text_align == CSS_VALUE_CENTER) x += (frame->content_width - used_width) * 0.5f;
    else if (style->text_align == CSS_VALUE_RIGHT || style->text_align == CSS_VALUE_END) x += frame->content_width - used_width;
    bool justify = style->text_align == CSS_VALUE_JUSTIFY && line.next < flow->paragraph.count && !line.forced;
    for (size_t i = line.first; i < line.next; i++) {
        const TypesetItem& item = flow->items[i];
        if (!item.source.native || item.source.native->provider != composer->tree->model->tree_id) return TYPESET_INVALID;
        DomNode* source = ((const PagedSourceRecord*)item.source.native.get())->source;
        if (composer->role == VIEW_FRAGMENT_BODY) {
            TypesetStatus status = paged_mark_enter(composer, source);
            if (status != TYPESET_OK) return status;
        }
        float width = 0.0f;
        bool painted = i >= line.paint_first && i < line.paint_end;
        TypesetMetrics item_metrics = item.metrics;
        PagedLineMeasurement measurement = {composer, flow, frame->content_height, frame->replaced_height};
        if (flow->has_dynamic_items && !paged_line_item_metrics(&flow->paragraph, i, frame->content_width, &item_metrics, &measurement)) return TYPESET_UNPLACEABLE;
        if (painted) width = item.kind == TYPESET_BOX ? item_metrics.advance :
            item.kind == TYPESET_GLUE ? (justify || expand ? typeset_glue_advance(&item.glue, &line.packing) : item.glue.natural) : 0.0f;
        LayoutViewNode* text = view_tree_fragment_append(composer->tree, paragraph, source,
            {x, composer->y, width, line.height}, item.start, item.length);
        if (!text) return TYPESET_OUT_OF_MEMORY;
        text->role = composer->role; text->generated = item.source.offset_unit == TYPESET_PROVIDER_OFFSETS;
        if (painted && item.paint) {
            const PagedPaint* paint_payload = (const PagedPaint*)item.paint.get();
            if (paint_payload->kind == PAGED_PAINT_IMAGE) {
                const PagedImagePaint* image = (const PagedImagePaint*)paint_payload;
                PagedImageMeasure measured = {};
                if (image->framed) {
                    measured.width = frame->content_width; measured.height = frame->replaced_height;
                    // framed images retain the content measured against the original containing block.
                    measured.content_width = frame->replaced_content_width;
                    measured.content_height = frame->replaced_content_height;
                }
                else {
                    TypesetStatus status = paged_image_measure(composer, image, frame->content_width, frame->content_height, &measured);
                    if (status != TYPESET_OK) return status;
                }
                float box_width = measured.width + measured.box.edges[3] + measured.box.edges[1];
                float box_height = measured.height + measured.box.edges[0] + measured.box.edges[2];
                float extent = box_height + measured.margin[0] + measured.margin[2];
                float baseline = image->framed ? line.height : paged_line_baseline(flow, line);
                float top = image->framed ? 0.0f : paged_vertical_top(composer, flow->style, image->style,
                    extent, extent, line.height, baseline, frame->content_width);
                text->rect = {x + measured.margin[3], composer->y + top + measured.margin[0], box_width, box_height};
                text->computed_style = lam::up(image->style); text->paint_box = !image->framed;
                text->generated = composer->role != VIEW_FRAGMENT_BODY;
                if (!image->framed) {
                    TypesetStatus status = paged_boundary_publish(composer->tree, text, measured.box, true, true);
                    if (status != TYPESET_OK) return status;
                }
                PaintImageBox* box = (PaintImageBox*)arena_alloc(composer->tree->model->arena, sizeof(PaintImageBox));
                if (!box) return TYPESET_OUT_OF_MEMORY;
                *box = {}; box->image = lam::up(image->image); box->fonts = lam::up((FontContext*)composer->tree->model->css->fonts);
                box->raster_scale = composer->tree->model->environment.device_scale; box->opacity = image->opacity;
                box->content_rect = {text->rect.x + measured.box.edges[3], text->rect.y + measured.box.edges[0], measured.width, measured.height};
                box->image_rect = render_media_object_rect(&image->object, image->image, box->content_rect, 1.0f);
                if (image->style->image_spec) {
                    box->image_rect = render_media_positioned_rect(&image->object, box->content_rect,
                        measured.content_width, measured.content_height, 1.0f);
                    const CssValue* overflow = view_css_property(composer->tree, image->style, "overflow");
                    box->overflow_visible = !overflow || css_value_keyword_equals(overflow, CSS_VALUE_VISIBLE);
                }
                text->image_box = lam::up(box);
                x += width;
                continue;
            }
            const PagedTextPaint* payload = (const PagedTextPaint*)item.paint.get();
            if (payload->current_folio) {
                TypesetStatus status = paged_current_folio_payload(composer, payload, &payload);
                if (status != TYPESET_OK) return status;
                text->text_length = static_cast<size_t>(payload->run.text_len);
            }
            if (payload->leader) {
                bool adjacent = false;
                for (size_t j = line.paint_first; j < line.paint_end; j++) if (flow->items[j].kind == TYPESET_BOX) { adjacent = true; break; }
                if (!adjacent) return paged_failure(composer->composition, TYPESET_UNPLACEABLE, payload->source,
                    composer->page ? composer->page->page_number : 0, "leader cannot occupy a line alone");
            }
            text->computed_style = lam::up(payload->style);
            PaintGlyphRun* run = (PaintGlyphRun*)arena_alloc(composer->tree->model->arena, sizeof(PaintGlyphRun));
            if (!run) return TYPESET_OUT_OF_MEMORY;
            *run = payload->run;
            float altitude, depth;
            if (!radiant_text_metrics(payload->style, &altitude, &depth)) return TYPESET_UNPLACEABLE;
            float font_height = altitude + depth;
            run->x = x;
            if (payload->marker_owner) {
                float anchor = frame->content_x;
                for (size_t j = composer->depth; j > 0; j--)
                    if (composer->frames[j - 1].flow == payload->marker_owner) { anchor = composer->frames[j - 1].content_x; break; }
                run->x = anchor - payload->marker_width;
                text->rect.x = run->x; text->rect.width = payload->marker_width;
            }
            float baseline = paged_line_baseline(flow, line);
            float top = paged_vertical_top(composer, style, payload->style, font_height,
                altitude, line.height, baseline, frame->content_width);
            run->baseline_y = composer->y + top + altitude;
            if (payload->leader) {
                TypesetStatus status = paged_leader_run(composer, payload, x, width,
                    frame->content_x + frame->content_width, item.metrics.advance, run);
                if (status != TYPESET_OK) return status;
            }
            text->glyph_run = lam::up(run);
        }
        x += width;
        if (composer->role == VIEW_FRAGMENT_BODY && painted && width > 0.0f) composer->page_has_content = true;
    }
    composer->y += line.height;
    composer->committed_lines++;
    if (line.height > 0.0f) composer->page_has_content = true;
    return TYPESET_OK;
}

static TypesetStatus paged_measure_lines(PagedComposer* composer, PagedFlowNode* flow, size_t first, float width, float containing_height,
        TypesetLineCandidate* scratch, TypesetLineCandidate* lines, size_t capacity,
        size_t* count, float* height, bool allow_overflow = false,
        size_t stop_item = SIZE_MAX, size_t stop_lines = SIZE_MAX) {
    *count = 0; *height = 0.0f;
    while (first < flow->paragraph.count && first < stop_item && *count < stop_lines) {
        TypesetLineCandidate line = {};
        float atomic_height = flow->parent && flow->parent->image && composer->depth
            ? composer->frames[composer->depth - 1].replaced_height : NAN;
        TypesetStatus status = paged_next_line(composer, flow, first, width, containing_height, scratch, capacity, &line, atomic_height);
        if (status != TYPESET_OK) return status;
        if (line.overflow && !allow_overflow) return TYPESET_UNPLACEABLE;
        if (lines) lines[*count] = line;
        (*count)++; *height += line.height;
        first = line.next;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_following_style(PagedComposer* composer, ViewPageStyle* style) {
    size_t index = composer->tree->model->page_count;
    PagedPageSpec spec = {};
    TypesetStatus status = paged_page_spec(composer, index, false, &spec);
    if (status != TYPESET_OK) return status;
    return paged_page_style(composer, spec, static_cast<uint32_t>(index + 1), paged_side(composer, index), false, style);
}

static TypesetStatus paged_next_constraints(PagedComposer* composer, float* width, float* height, float* containing_height) {
    PagedReferenceMeasurement references(composer->composition);
    PagedComposer trial = *composer;
    RdtLogicalRect column = {};
    if (paged_has_next_column(composer)) {
        if (!view_css_page_column(composer->page->style, composer->column_index + 1, &column)) return TYPESET_INVALID;
    } else {
        if (paged_following_style(composer, &trial.page_style) != TYPESET_OK ||
            !view_css_page_column(&trial.page_style, 0, &column)) return TYPESET_INVALID;
    }
    trial.page_style.content_rect = column;
    float x = trial.page_style.content_rect.x;
    *width = trial.page_style.content_rect.width;
    *height = trial.page_style.content_rect.height;
    *containing_height = paged_root_height(&trial);
    float reference_x = x, reference_width = *width;
    for (size_t i = 0; i < composer->depth; i++) {
        PagedFrame frame = {}; frame.flow = composer->frames[i].flow;
        TypesetStatus status = paged_frame_descend(&trial, &frame, &x, width, containing_height, &reference_x, &reference_width);
        if (status != TYPESET_OK) return status;
        *height -= paged_fragment_edge(frame.flow->style, frame.box, 0, false) +
            paged_fragment_edge(frame.flow->style, frame.box, 2, false);
    }
    return TYPESET_OK;
}

static TypesetStatus paged_region_frame(PagedComposer* composer, PagedFlowNode* flow,
        PagedFlowNode** ancestors, PagedFrame* result, float* parent_width) {
    size_t count = 0;
    for (PagedFlowNode* owner = flow->kind == PAGED_FLOW_PARAGRAPH ? flow->parent : flow; owner; owner = owner->parent) {
        if (count >= composer->composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
        ancestors[count++] = owner;
    }
    float x = 0.0f, width = composer->page_style.content_rect.width, height = paged_root_height(composer);
    float reference_x = x, reference_width = width;
    for (size_t i = count; i > 0; i--) {
        *result = {}; result->flow = ancestors[i - 1]; *parent_width = width;
        TypesetStatus status = paged_frame_descend(composer, result, &x, &width, &height, &reference_x, &reference_width);
        if (status != TYPESET_OK) return status;
    }
    return count ? TYPESET_OK : TYPESET_INVALID;
}

static TypesetStatus paged_region_measure(void* context, const TypesetResume* start,
        const TypesetRegionConstraints* constraints, bool split, Pool* scratch, TypesetRegionSlice* slice) {
    PagedRegionRecord* record = (PagedRegionRecord*)context;
    PagedComposition* composition = record->tree->model->composition;
    PagedReferenceMeasurement references(composition);
    if (record->kind != PAGED_REGION_NOTE) {
        if (constraints->retain_tail) return TYPESET_UNPLACEABLE;
        PagedComposer measure = {}; measure.tree = record->tree; measure.composition = composition;
        measure.page = constraints->page_number && constraints->page_number <= record->tree->model->page_count
            ? record->tree->model->pages.get()[constraints->page_number - 1] : nullptr;
        measure.role = VIEW_FRAGMENT_FLOAT;
        measure.page_style.content_rect = {0.0f, 0.0f, constraints->inline_size,
            constraints->reference_height > 0.0f ? constraints->reference_height : constraints->available_height};
        PagedFrame frame = {}; frame.flow = record->flow;
        TypesetStatus status = paged_frame_measure(&measure, &frame, 0.0f, constraints->inline_size, paged_root_height(&measure));
        if (status != TYPESET_OK) return status;
        float left = paged_used_length(&measure, record->flow->style, record->flow->style->margin[3], CSS_PROPERTY_MARGIN_LEFT, constraints->inline_size);
        float right = paged_used_length(&measure, record->flow->style, record->flow->style->margin[1], CSS_PROPERTY_MARGIN_RIGHT, constraints->inline_size);
        if (frame.width + left + right != constraints->inline_size) return TYPESET_INVALID;
        float height = 0.0f; bool forced = false;
        status = paged_flow_height(&measure, record->flow, constraints->inline_size, paged_root_height(&measure), false, &height, &forced);
        if (status != TYPESET_OK) return status;
        if (forced) return TYPESET_INVALID;
        height += fmaxf(0.0f, paged_used_length(&measure, record->flow->style,
            record->flow->style->margin[0], CSS_PROPERTY_MARGIN_TOP, constraints->inline_size));
        height += fmaxf(0.0f, paged_used_length(&measure, record->flow->style,
            record->flow->style->margin[2], CSS_PROPERTY_MARGIN_BOTTOM, constraints->inline_size));
        if (height <= 0.0f || height > constraints->available_height) return TYPESET_UNPLACEABLE;
        *slice = {}; slice->end = *start; slice->end.serial++; slice->complete = true;
        slice->metrics.height = height;
        return TYPESET_OK;
    }
    if (start->state[0] >= record->count) return TYPESET_INVALID;
    PagedRegionPaint* paint = (PagedRegionPaint*)pool_calloc(scratch, sizeof(PagedRegionPaint));
    PagedFlowNode** ancestors = (PagedFlowNode**)pool_alloc(scratch, composition->options.max_depth * sizeof(PagedFlowNode*));
    if (!paint || !ancestors) return TYPESET_OUT_OF_MEMORY;
    paint->provider = record->provider;
    PagedComposer measure = {}; measure.tree = record->tree; measure.composition = composition;
    measure.page = constraints->page_number && constraints->page_number <= record->tree->model->page_count
        ? record->tree->model->pages.get()[constraints->page_number - 1] : nullptr;
    measure.role = VIEW_FRAGMENT_NOTE;
    measure.page_style.content_rect = {0.0f, 0.0f, constraints->inline_size, constraints->available_height};
    TypesetResume end = *start;
    TypesetResume before_last = *start;
    float before_last_height = 0.0f;
    size_t before_last_count = 0;
    float height = 0.0f;
    while (end.state[0] < record->count) {
        const PagedRegionPart& part = record->parts[end.state[0]];
        PagedFrame frame = {}; float parent_width = 0.0f;
        TypesetStatus status = paged_region_frame(&measure, part.flow, ancestors, &frame, &parent_width);
        if (status != TYPESET_OK) return status;
        if (part.kind != PAGED_REGION_PARAGRAPH) {
            size_t edge = part.kind == PAGED_REGION_OPEN ? 0 : 2;
            CssPropertyCode property = edge ? CSS_PROPERTY_MARGIN_BOTTOM : CSS_PROPERTY_MARGIN_TOP;
            float extent = fmaxf(0.0f, frame.box.edges[edge] + paged_used_length(&measure,
                part.flow->style, part.flow->style->margin[edge], property, parent_width));
            if (height + extent > constraints->available_height) break;
            height += extent; end.state[0]++; end.serial++; continue;
        }
        if (end.state[1] >= part.flow->paragraph.count) { end.state[0]++; end.state[1] = 0; end.serial++; continue; }
        if (split && constraints->minimum && paint->count) break;
        size_t capacity = part.flow->paragraph.count;
        TypesetLineCandidate* candidates = (TypesetLineCandidate*)pool_alloc(scratch, capacity * sizeof(TypesetLineCandidate));
        if (!candidates) return TYPESET_OUT_OF_MEMORY;
        TypesetLineCandidate line = {};
        status = paged_next_line(&measure, part.flow, end.state[1], frame.content_width, frame.content_height, candidates, capacity, &line, frame.replaced_height);
        pool_free(scratch, candidates);
        if (status != TYPESET_OK) return status;
        if (line.overflow) return TYPESET_UNPLACEABLE;
        if (height + line.height > constraints->available_height) break;
        if (!lam::pool_grow_array(scratch, &paint->lines, &paint->capacity, paint->count + 1, 16)) return TYPESET_OUT_OF_MEMORY;
        before_last = end; before_last_height = height; before_last_count = paint->count;
        paint->lines[paint->count++] = {part.flow, line, frame.replaced_height, frame.content_height,
            frame.replaced_content_width, frame.replaced_content_height, frame.content_x, frame.content_width, height, frame.reference_width};
        height += line.height; end.state[1] = line.next; end.serial++;
    }
    bool complete = end.state[0] == record->count;
    if (complete && constraints->retain_tail) {
        if (!split || !before_last_count) return TYPESET_UNPLACEABLE;
        // restore the last actual line and its closing edges to the owned continuation.
        end = before_last; height = before_last_height; paint->count = before_last_count; complete = false;
    }
    if ((!split && !complete) || height <= 0.0f || (!paint->count && !start->serial)) return TYPESET_UNPLACEABLE;
    *slice = {}; slice->end = end; slice->complete = complete;
    slice->metrics.height = height; slice->paint = lam::up((const TypesetRecord*)paint);
    return TYPESET_OK;
}

static void paged_region_trial_dispose(PagedRegionTrial* trial) {
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) typeset_region_plan_dispose(&trial->plans[i]);
}


struct PagedNoteArea : PagedBoxEdges {
    float margin[4], width, minimum, maximum, preferred;
};
static TypesetStatus paged_note_area(PagedComposer* composer, PagedNoteArea* area) {
    *area = {};
    const ViewPageAreaStyle& declarations = composer->page_style.footnote;
    ViewCssStyle* style = declarations.computed_style;
    if (!style) return TYPESET_INVALID;
    float width = composer->page_style.content_rect.width;
    TypesetStatus status = paged_box_edges(composer, style, width, area);
    if (status != TYPESET_OK) return status;
    const CssValue* value = view_css_declaration_value(composer->tree, style, declarations.float_value, "float");
    if (value && !css_value_keyword_equals(value, CSS_VALUE_BOTTOM)) return TYPESET_INVALID;
    value = view_css_declaration_value(composer->tree, style, declarations.box_sizing, "box-sizing");
    CssEnum sizing = css_value_keyword_equals(value, CSS_VALUE_BORDER_BOX) ? CSS_VALUE_BORDER_BOX : CSS_VALUE_CONTENT_BOX;
    BoundaryProp boundary = {}; boundary.border = lam::own(&area->border);
    for (size_t i = 0; i < 4; i++) {
        boundary.padding.values[i] = area->padding[i];
        CssPropertyCode property = radiant_box_side_property(CSS_PROPERTY_MARGIN, static_cast<CssBoxSide>(i));
        value = view_css_declaration_value(composer->tree, style, declarations.margin[i], css_property_get_by_code(property)->name);
        // The area is floated; auto margins have zero used size and signed margins do not collapse.
        area->margin[i] = paged_used_length(composer, style, value, property, width);
    }
    auto dimension = [&](const CssDeclaration* declaration, CssPropertyCode property, float fallback, bool horizontal) {
        const CssValue* specified = view_css_declaration_value(composer->tree, style, declaration, css_property_get_by_code(property)->name);
        float used = paged_used_length(composer, style, specified, property, width, fallback, composer->page_style.content_rect.height);
        return isnan(used) ? used : layout_css_size_to_border_box(&boundary, sizing, used, horizontal);
    };
    float minimum = dimension(declarations.min_width, CSS_PROPERTY_MIN_WIDTH, 0.0f, true);
    float maximum = dimension(declarations.max_width, CSS_PROPERTY_MAX_WIDTH, INFINITY, true);
    float specified = dimension(declarations.width, CSS_PROPERTY_WIDTH, NAN, true);
    area->width = fmaxf(minimum, fminf(isfinite(specified) ? specified : width - area->margin[1] - area->margin[3], maximum));
    area->minimum = dimension(declarations.min_height, CSS_PROPERTY_MIN_HEIGHT, 0.0f, false);
    area->maximum = fmaxf(area->minimum, dimension(declarations.max_height, CSS_PROPERTY_MAX_HEIGHT, INFINITY, false));
    specified = dimension(declarations.height, CSS_PROPERTY_HEIGHT, NAN, false);
    area->preferred = isfinite(specified) ? fmaxf(area->minimum, fminf(specified, area->maximum)) : NAN;
    return isfinite(area->width) && area->width > area->edges[1] + area->edges[3] ? TYPESET_OK : TYPESET_UNPLACEABLE;
}

static float paged_region_reserved(const PagedComposer* composer) {
    float height = 0.0f;
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++)
        if (i != PAGED_REGION_TOP) height += composer->regions[i].plan.reserved_height;
    return height;
}

static bool paged_regions_pending(const PagedComposer* composer) {
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) if (composer->composition->queues[i].count) return true;
    return false;
}

static bool paged_regions_placed(const PagedComposer* composer) {
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) if (composer->regions[i].plan.count) return true;
    return false;
}

static bool paged_regions_blocked(const PagedComposer* composer) {
    for (const PagedRegionState& region : composer->regions) if (region.plan.blocked) return true;
    return false;
}

static bool paged_region_plans_pending(const PagedComposer* composer) {
    for (const PagedRegionState& region : composer->regions) if (region.plan.pending_count) return true;
    return false;
}

static TypesetStatus paged_regions_trial(PagedComposer* composer, float body_height, PagedRegionTrial* trial, bool limit_notes = false) {
    float reserved = 0.0f;
    size_t tail_region = PAGED_REGION_COUNT;
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++)
        if (composer->composition->queues[i].count || trial->anchor_counts[i]) tail_region = i;
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
        TypesetRegionQueue* queue = &composer->composition->queues[i];
        if (!queue->count && !trial->anchor_counts[i]) continue;
        if (composer->page_style.column_count > 1) {
            // page-attached reservations must be solved jointly with every body column.
            for (size_t j = 0; j < queue->count + trial->anchor_counts[i]; j++) {
                const TypesetRegionMaterial* material = j < queue->count ? queue->entries[j].material :
                    composer->regions[i].anchors[j - queue->count];
                if (!material || material->reference != TYPESET_REGION_REFERENCE_COLUMN) {
                    paged_region_trial_dispose(trial);
                    return paged_failure(composer->composition, TYPESET_INVALID, composer->page_style.body_region->source.address,
                        composer->page->page_number, "page-attached auxiliary regions require joint body-column reservation");
                }
            }
        }
        TypesetRegionConstraints constraints = {composer->page_style.content_rect.width,
            composer->page_style.content_rect.height, composer->page->page_number, nullptr, false,
            i != PAGED_REGION_NOTE, i == PAGED_REGION_TOP, composer->page_style.content_rect.height};
        constraints.column_number = composer->column_index + 1;
        constraints.column_count = composer->page_style.column_count;
        // empty explicit columns may route pending input forward; an empty whole sheet still fails at its last column.
        constraints.defer_unplaceable = composer->page->style->column_rects &&
            (paged_has_next_column(composer) || composer->sheet_has_content || body_height > 0.0f || reserved > 0.0f);
        constraints.retain_tail = composer->retain_aux_tail && i == tail_region;
        float occupied = body_height + reserved, separator = 0.0f;
        PagedNoteArea area = {};
        if (i == PAGED_REGION_NOTE) {
            TypesetStatus status = paged_note_area(composer, &area);
            bool feasible = status == TYPESET_OK;
            if (!feasible && (status != TYPESET_UNPLACEABLE || !constraints.defer_unplaceable)) {
                paged_region_trial_dispose(trial); return status;
            }
            if (body_height > constraints.available_height) { paged_region_trial_dispose(trial); return TYPESET_UNPLACEABLE; }
            constraints.occupied = body_height > 0.0f || limit_notes;
            constraints.available_height -= body_height;
            float margins = area.margin[0] + area.margin[2];
            if (constraints.occupied) constraints.available_height = fminf(constraints.available_height, fmaxf(0.0f, area.maximum + margins));
            if (isfinite(area.preferred)) constraints.available_height = fminf(constraints.available_height, fmaxf(0.0f, area.preferred + margins));
            constraints.minimum_height = fmaxf(0.0f, (isfinite(area.preferred) ? area.preferred : area.minimum) + margins);
            // an infeasible note area offers zero capacity; queue input survives without a measured or painted slice.
            if (feasible) constraints.inline_size = area.width - area.edges[1] - area.edges[3];
            else constraints.available_height = 0.0f;
            separator = area.edges[0] + area.edges[2] + margins; occupied = 0.0f;
        }
        TypesetStatus status = typeset_region_plan(queue, composer->regions[i].anchors, trial->anchor_counts[i],
            &constraints, occupied, separator, &trial->plans[i]);
        if (status != TYPESET_OK) { paged_region_trial_dispose(trial); return status; }
        if (i == PAGED_REGION_BOTTOM) {
            for (size_t j = 0; j < trial->plans[i].count; j++)
                trial->plans[i].placements[j].rect.y -= trial->plans[PAGED_REGION_NOTE].reserved_height;
        }
        if (i == PAGED_REGION_NOTE) for (size_t j = 0; j < trial->plans[i].count; j++) {
            trial->plans[i].placements[j].rect.x += area.margin[3] + area.edges[3];
            trial->plans[i].placements[j].rect.y += composer->page_style.content_rect.height - constraints.available_height - area.margin[2] - area.edges[2];
        }
        reserved += trial->plans[i].reserved_height;
    }
    if (!limit_notes && body_height == 0.0f && trial->plans[PAGED_REGION_NOTE].count &&
        (trial->plans[PAGED_REGION_TOP].count || trial->plans[PAGED_REGION_BOTTOM].count)) {
        // Only a genuinely note-only page ignores the area limit; replay joint admissions once.
        paged_region_trial_dispose(trial);
        return paged_regions_trial(composer, body_height, trial, true);
    }
    return TYPESET_OK;
}

static TypesetStatus paged_fragments_translate(ViewTree* tree, LayoutViewNode* node, float dy, bool body_only, bool siblings = true) {
    for (; node; node = siblings ? node->next_sibling.get() : nullptr) {
        if (body_only && node->role != VIEW_FRAGMENT_BODY) continue;
        if (!view_tree_model_touch_node(tree, node)) return TYPESET_OUT_OF_MEMORY;
        node->rect.y += dy;
        if (node->glyph_run) node->glyph_run->baseline_y += dy;
        if (node->image_box) {
            node->image_box->content_rect.y += dy;
            node->image_box->image_rect.y += dy;
        }
        // select body roots once; repeated table furniture remains part of the moved subtree.
        TypesetStatus status = paged_fragments_translate(tree, node->first_child, dy, false);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_body_translate(PagedComposer* composer, float dy) {
    // earlier columns share this page; only the current root belongs to this fragmentainer trial.
    bool columns = composer->page_style.column_count > 1;
    LayoutViewNode* root = columns ? composer->column_root : composer->page->node.first_child.get();
    return paged_fragments_translate(composer->tree, root, dy, true, !columns);
}

static TypesetStatus paged_regions_accept(PagedComposer* composer, PagedRegionTrial* trial) {
    // Full-width top floats keep every inline constraint unchanged; translate only this page's body.
    float dy = trial->plans[PAGED_REGION_TOP].reserved_height - composer->regions[PAGED_REGION_TOP].plan.reserved_height;
    if (dy != 0.0f) {
        TypesetStatus status = paged_body_translate(composer, dy);
        if (status != TYPESET_OK) { paged_region_trial_dispose(trial); return status; }
        composer->y += dy;
        for (size_t i = 0; i < composer->depth; i++) composer->frames[i].page_start += dy;
    }
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
        typeset_region_plan_dispose(&composer->regions[i].plan);
        composer->regions[i].plan = trial->plans[i]; trial->plans[i] = {};
        composer->regions[i].anchor_count = trial->anchor_counts[i];
    }
    return TYPESET_OK;
}

static TypesetStatus paged_regions_prepare(PagedComposer* composer) {
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) composer->regions[i].anchor_count = 0;
    if (!paged_regions_pending(composer)) return TYPESET_OK;
    PagedRegionTrial trial = {};
    TypesetStatus status = paged_regions_trial(composer, 0.0f, &trial);
    if (status == TYPESET_OK) status = paged_regions_accept(composer, &trial);
    return status;
}

static TypesetStatus paged_native_append(PagedComposer* composer, LayoutViewNode* parent,
        const ViewNativeMaterial& material, RdtLogicalRect rect, float baseline, LayoutViewNode** result = nullptr) {
    ViewModelStatus model_status = VIEW_MODEL_OK;
    LayoutViewNode* node = view_tree_native_fragment_append(composer->tree, parent, &material, rect, &model_status);
    if (!node) return model_status == VIEW_MODEL_OUT_OF_MEMORY ? TYPESET_OUT_OF_MEMORY :
        model_status == VIEW_MODEL_STALE_SOURCE ? TYPESET_STALE : TYPESET_INVALID;
    if (node->glyph_run) { node->glyph_run->x += rect.x; node->glyph_run->baseline_y += baseline; }
    if (node->image_box) {
        node->image_box->content_rect.x += rect.x; node->image_box->content_rect.y += rect.y;
        node->image_box->image_rect.x += rect.x; node->image_box->image_rect.y += rect.y;
    }
    node->paint_box = true; node->role = composer->role;
    if (result) *result = node;
    return TYPESET_OK;
}

struct PagedNativeRegionLease {
    RefCount references;
    ViewNativeOwner producer;
    TypesetRegionPlan plan;
};
static bool paged_native_region_retain(void* context) {
    return ref_count_retain(&((PagedNativeRegionLease*)context)->references);
}
static void paged_native_region_release(void* context) {
    PagedNativeRegionLease* lease = (PagedNativeRegionLease*)context;
    if (ref_count_release(&lease->references) != REF_COUNT_LAST) return;
    typeset_region_plan_dispose(&lease->plan);
    lease->producer.release(lease->producer.context); mem_free(lease);
}

static TypesetStatus paged_native_region_emit(PagedComposer* composer, LayoutViewNode* parent,
        const TypesetRegionPlan* plan, const TypesetRegionPlacement& placement) {
    const PagedNativeRegion* region = (const PagedNativeRegion*)placement.material->context;
    PagedNativeRegionLease* lease = (PagedNativeRegionLease*)mem_calloc(1, sizeof(PagedNativeRegionLease), MEM_CAT_LAYOUT); // OBJ_HEAP_OK: retained fragments own this lease beyond the composition pool; the last owner releases it.
    if (!lease) return TYPESET_OUT_OF_MEMORY;
    ref_count_init(&lease->references); lease->producer = region->flow->binding.owner;
    if (!lease->producer.retain(lease->producer.context)) { mem_free(lease); return TYPESET_STALE; }
    // measured paint/metric records may live in the plan scratch, independently of the producer owner.
    TypesetStatus status = typeset_region_plan_retain(plan, &lease->plan);
    ViewNativeOwner owner = {lease, paged_native_region_retain, paged_native_region_release};
    RdtLogicalRect rect = placement.rect;
    rect.x += composer->page_style.content_rect.x; rect.y += composer->page_style.content_rect.y;
    PagedComposer local = *composer; local.role = region->kind == PAGED_REGION_NOTE ? VIEW_FRAGMENT_NOTE : VIEW_FRAGMENT_FLOAT;
    ViewNativeMaterial material = {};
    material.source = placement.material->source; material.metrics = placement.slice.metrics;
    material.solution = placement.slice.paint; material.owner = owner;
    LayoutViewNode* box = nullptr;
    if (status == TYPESET_OK) status = paged_native_append(&local, parent, material, rect, rect.y + material.metrics.baseline, &box);
    if (status == TYPESET_OK) {
        box->generated = true; box->first_fragment = placement.start.serial == placement.material->start.serial;
        box->last_fragment = placement.slice.complete;
    }
    for (size_t index = 0; status == TYPESET_OK; index++) {
        material = {}; RdtLogicalRect item_rect = {};
        TypesetRegionPlacement selected = placement; selected.material = region->source;
        status = region->flow->binding.region_item(region->flow->binding.context, &selected, index, &material, &item_rect);
        if (status == TYPESET_DONE) { status = TYPESET_OK; break; }
        if (status != TYPESET_OK) break;
        if (index >= composer->composition->options.max_items) { status = TYPESET_BUDGET_EXHAUSTED; break; }
        if (!isfinite(item_rect.x) || !isfinite(item_rect.y) || !isfinite(item_rect.width) || !isfinite(item_rect.height) ||
            item_rect.x < 0.0f || item_rect.y < 0.0f || item_rect.width < 0.0f || item_rect.height < 0.0f ||
            item_rect.x + item_rect.width > rect.width || item_rect.y + item_rect.height > rect.height) {
            status = TYPESET_UNPLACEABLE; break;
        }
        if (!material.owner.context) material.owner = owner;
        item_rect.x += rect.x; item_rect.y += rect.y;
        status = paged_native_append(&local, box, material, item_rect, item_rect.y + material.metrics.baseline);
    }
    paged_native_region_release(lease);
    return status;
}

static TypesetStatus paged_region_emit(PagedComposer* composer, LayoutViewNode* parent,
        const TypesetRegionPlan* plan, const TypesetRegionPlacement& placement) {
    if (placement.material->measure == paged_native_region_measure)
        return paged_native_region_emit(composer, parent, plan, placement);
    const PagedRegionRecord* record = (const PagedRegionRecord*)placement.material->source.native.get();
    const PagedRegionPaint* paint = (const PagedRegionPaint*)placement.slice.paint.get();
    RdtLogicalRect rect = placement.rect;
    rect.x += composer->page_style.content_rect.x; rect.y += composer->page_style.content_rect.y;
    bool note = record->kind == PAGED_REGION_NOTE;
    LayoutViewNode* box = view_tree_fragment_append(composer->tree, parent, note ? record->flow->source : nullptr, rect);
    if (!box) return TYPESET_OUT_OF_MEMORY;
    box->role = note ? VIEW_FRAGMENT_NOTE : VIEW_FRAGMENT_FLOAT;
    box->generated = true;
    box->first_fragment = placement.start.serial == placement.material->start.serial;
    box->last_fragment = placement.slice.complete;
    PagedComposer region = {}; region.tree = composer->tree; region.composition = composer->composition;
    region.page = composer->page; region.role = box->role; region.y = rect.y;
    region.page_style = composer->page_style;
    PagedFrame frame = {}; frame.fragment = box; frame.content_x = rect.x; frame.content_width = rect.width;
    frame.content_height = paged_root_height(&region);
    if (!note) {
        if (!lam::pool_grow_array(composer->composition->pool, &region.frames, &region.frame_capacity, 1, 16)) return TYPESET_OUT_OF_MEMORY;
        region.frames[0] = frame; region.depth = 1;
        TypesetStatus status = paged_fixed_flow(&region, record->flow);
        pool_free(composer->composition->pool, region.frames);
        return status;
    }
    box->computed_style = lam::up(record->flow->style); box->paint_box = true;
    region.frames = &frame; region.depth = 1;
    for (size_t j = 0; j < paint->count; j++) {
        const PagedRegionLine& line = paint->lines[j];
        // note slices replay measured lines, so commit their first-area contexts with the selected paint.
        for (const ViewCssStyle* style = line.flow->style; style; style = style->parent) {
            TypesetStatus status = paged_reference_capture(composer->composition, style, line.reference_width, composer->page->page_number);
            if (status != TYPESET_OK) return status;
            if (style == record->flow->style) break;
        }
        frame.flow = line.flow; frame.content_x = rect.x + line.x; frame.content_width = line.width;
        frame.replaced_height = line.atomic_height;
        frame.replaced_content_width = line.replaced_content_width;
        frame.replaced_content_height = line.replaced_content_height;
        frame.content_height = line.containing_height;
        region.y = rect.y + line.y;
        TypesetStatus status = paged_commit_line(&region, line.flow, line.line);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_note_area_emit(PagedComposer* composer, const TypesetRegionPlan* plan, LayoutViewNode** result) {
    PagedNoteArea area = {};
    TypesetStatus status = paged_note_area(composer, &area);
    if (status != TYPESET_OK) return status;
    const RdtLogicalRect& content = composer->page_style.content_rect;
    LayoutViewNode* box = view_tree_fragment_append(composer->tree, &composer->page->node, nullptr,
        {content.x + area.margin[3], content.y + content.height - plan->reserved_height + area.margin[0],
         area.width, plan->reserved_height - area.margin[0] - area.margin[2]});
    if (!box) return TYPESET_OUT_OF_MEMORY;
    status = paged_boundary_publish(composer->tree, box, area, true, true);
    if (status != TYPESET_OK) return status;
    for (size_t i = 0; i < 4; i++) box->computed_boundary->margin.values[i] = area.margin[i];
    // Anonymous page furniture belongs to this view arena, never to the shared DOM.
    box->computed_style = composer->page_style.footnote.computed_style;
    box->role = VIEW_FRAGMENT_NOTE;
    box->paint_box = box->generated = box->first_fragment = box->last_fragment = true;
    *result = box;
    return TYPESET_OK;
}

static TypesetStatus paged_regions_close_trial(PagedComposer* composer) {
    bool active = false;
    PagedRegionTrial trial = {};
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
        trial.anchor_counts[i] = composer->regions[i].anchor_count;
        active |= composer->regions[i].plan.scratch != nullptr;
    }
    // Declared block extents and closing decorations participate in the final reservation.
    float body_height = paged_body_height(composer);
    TypesetStatus status = active ? paged_regions_trial(composer, body_height, &trial) : TYPESET_OK;
    if (status != TYPESET_OK) {
        composer->closure_failure = status == TYPESET_UNPLACEABLE;
        return paged_failure(composer->composition, status, nullptr,
            composer->page->page_number, "closing body extents exceed the reserved page regions");
    }
    if (active) status = paged_regions_accept(composer, &trial);
    if (status != TYPESET_OK) return status;
    const RadiantPageRegion* body = composer->page_style.body_region;
    if (!composer->body_aligned && composer->page && body && body->align != RADIANT_REGION_ALIGN_BEFORE) {
        float free_height = composer->page_style.content_rect.height - body_height - paged_region_reserved(composer) -
            composer->regions[PAGED_REGION_TOP].plan.reserved_height;
        float offset = free_height * (body->align == RADIANT_REGION_ALIGN_CENTER ? 0.5f : 1.0f);
        // alignment is final placement; pagination extents and insertion reservations stay authoritative.
        if (offset != 0.0f) status = paged_body_translate(composer, offset);
        if (status != TYPESET_OK) return status;
    }
    composer->body_aligned = true;
    bool placed = paged_regions_placed(composer);
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
        PagedRegionState* region = &composer->regions[i];
        TypesetRegionPlan* plan = &region->plan;
        if (!plan->scratch) continue;
        LayoutViewNode* parent = &composer->page->node;
        if (i == PAGED_REGION_NOTE && plan->count) status = paged_note_area_emit(composer, plan, &parent);
        for (size_t j = 0; status == TYPESET_OK && j < plan->count; j++) status = paged_region_emit(composer, parent, plan, plan->placements[j]);
        if (status == TYPESET_OK) status = typeset_region_commit(&composer->composition->queues[i], plan);
        typeset_region_plan_dispose(plan); region->anchor_count = 0;
    }
    if (status == TYPESET_OK) {
        composer->sheet_has_content |= placed;
        ViewPageBox* page = composer->page;
        // continuous editions have no physical page or column occupancy.
        if (page && page->style->column_count > 1 && (composer->page_has_content || placed) &&
            (!page->occupied_columns || page->occupied_columns->index != composer->column_index)) {
            // immutable occupancy follows the selected input, including auxiliary-only columns and trial rollback.
            if (!view_tree_model_touch_node(composer->tree, &page->node)) return TYPESET_OUT_OF_MEMORY;
            auto* column = (ViewPageColumn*)arena_alloc(composer->tree->model->arena, sizeof(ViewPageColumn));
            if (!column) return TYPESET_OUT_OF_MEMORY;
            *column = {page->occupied_columns, composer->column_index};
            page->occupied_columns = lam::up((const ViewPageColumn*)column);
        }
    }
    return status;
}

static TypesetRegionSelection paged_region_selection(size_t region, const TypesetRegionPlan& plan) {
    return {region == PAGED_REGION_NOTE ? TYPESET_REGION_NOTE : TYPESET_REGION_FLOAT,
        region == PAGED_REGION_BOTTOM ? TYPESET_REGION_END : TYPESET_REGION_START, plan.placements, plan.count};
}

static TypesetContribution paged_region_contribution(const TypesetRegionSelection& region, const TypesetRegionPlacement& placement) {
    TypesetContribution value = {};
    value.kind = region.kind == TYPESET_REGION_NOTE ? TYPESET_CONTRIBUTION_INSERTION : TYPESET_CONTRIBUTION_FLOAT;
    value.region = region.kind; value.region_edge = region.edge;
    value.source = placement.material->source; value.metrics = placement.slice.metrics;
    value.region_material = placement.material;
    return value;
}

static TypesetStatus paged_page_policy_finish(PagedComposer* composer) {
    const TypesetPagePolicy* policy = composer->composition->options.page_policy;
    if (!policy || !composer->page || composer->policy_done || composer->policy_trial) return TYPESET_OK;
    PagedCheckpoint checkpoint;
    TypesetStatus status = checkpoint.begin(composer);
    if (status != TYPESET_OK) return status;
    TypesetPageAssembly assembly = {};
    while (status == TYPESET_OK) {
        size_t count = composer->page->fixed ? 1 : 0;
        for (const PagedRegionState& region : composer->regions) {
            if (region.plan.count > composer->composition->options.max_items - count) { status = TYPESET_BUDGET_EXHAUSTED; break; }
            count += region.plan.count;
        }
        if (status != TYPESET_OK) break;
        if (count > SIZE_MAX / sizeof(TypesetContribution)) { status = TYPESET_BUDGET_EXHAUSTED; break; }
        Pool* scratch = mem_pool_create((MemContext*)composer->composition->document->services.mem_ctx,
            MEM_ROLE_LAYOUT, "typeset.physical-page.assembly");
        if (!scratch) { status = TYPESET_OUT_OF_MEMORY; break; }
        TypesetContribution* contributions = count ? (TypesetContribution*)pool_calloc(scratch,
            count * sizeof(TypesetContribution)) : nullptr;
        if (count && !contributions) { mem_pool_destroy(scratch); status = TYPESET_OUT_OF_MEMORY; break; }
        TypesetPageCandidate candidate = {};
        candidate.kind = composer->page->fixed ? TYPESET_PAGE_FIXED : composer->page->blank ? TYPESET_PAGE_BLANK :
            composer->auxiliary_page || count ? TYPESET_PAGE_REGION : TYPESET_PAGE_EMPTY;
        candidate.page_number = composer->page->page_number;
        candidate.start = {composer->tree->model->tree_id, composer->tree->layout_generation,
            candidate.page_number - 1, {candidate.page_number, candidate.kind}};
        candidate.end = candidate.start; candidate.end.serial++;
        candidate.body_height = paged_body_height(composer);
        candidate.available_height = composer->page_style.content_rect.height;
        candidate.note_height = composer->regions[PAGED_REGION_NOTE].plan.reserved_height;
        candidate.float_height = composer->regions[PAGED_REGION_TOP].plan.reserved_height +
            composer->regions[PAGED_REGION_BOTTOM].plan.reserved_height;
        candidate.boundary = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_PAGE, 0, 0};
        size_t index = 0;
        if (composer->page->fixed) {
            contributions[index].kind = TYPESET_CONTRIBUTION_BOX;
            contributions[index].metrics = {composer->page_style.content_rect.width, candidate.body_height, 0,
                candidate.body_height, {0, 0, composer->page_style.content_rect.width, candidate.body_height}, {}};
            index++;
        }
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) for (size_t j = 0; j < composer->regions[i].plan.count; j++) {
            const TypesetRegionPlacement& placement = composer->regions[i].plan.placements[j];
            contributions[index++] = paged_region_contribution(paged_region_selection(i, composer->regions[i].plan), placement);
        }
        TypesetPagePlan plan = {}; plan.scratch = scratch; plan.policy = policy; plan.candidate = candidate;
        plan.contributions = contributions; plan.count = count; plan.complete = true;
        size_t selected = SIZE_MAX; bool reinsert = false;
        status = typeset_page_select(policy, &candidate, 1, &selected, &plan.action);
        if (status == TYPESET_OK) status = paged_assembly_resolve(composer, &plan, &assembly, &reinsert);
        TypesetPolicyCheckpoint progressed = {};
        if (status == TYPESET_OK && reinsert) status = typeset_policy_checkpoint(policy, &progressed);
        if (status == TYPESET_OK && !reinsert && policy->committed) status = policy->committed(policy->context, &candidate);
        mem_pool_destroy(scratch);
        if (status != TYPESET_OK || !reinsert) break;
        // reinsert the selected physical input while retaining only the journaled policy transition.
        status = checkpoint.restore();
        if (status == TYPESET_OK) status = typeset_policy_restore(policy, &progressed);
        if (status == TYPESET_OK) status = checkpoint.begin(composer);
        if (status == TYPESET_OK && !composer->page->blank && !composer->page->fixed) status = paged_regions_prepare(composer);
    }
    if (status == TYPESET_OK) { composer->policy_done = true; return checkpoint.accept(); }
    return checkpoint.fail(status);
}

static TypesetStatus paged_regions_close(PagedComposer* composer) {
    PagedCheckpoint checkpoint;
    TypesetStatus status = checkpoint.begin(composer);
    if (status != TYPESET_OK) return status;
    status = paged_page_policy_finish(composer);
    if (status == TYPESET_OK) status = paged_regions_close_trial(composer);
    return status == TYPESET_OK ? checkpoint.accept() : checkpoint.fail(status);
}

static TypesetStatus paged_sheet_tail_columns(PagedComposer* composer, bool* finished, TypesetResume* cursor = nullptr) {
    size_t depth = composer->depth;
    bool pending_break = composer->pending_break;
    ViewBreak requested_break = composer->requested_break;
    TypesetStatus status = composer->auxiliary_page && composer->sheet_trial && cursor ?
        paged_sheet_aux_column(composer, cursor) : TYPESET_OK;
    if (status == TYPESET_OK) status = paged_fragmentainer_close(composer);
    composer->depth = 0;
    while (status == TYPESET_OK && paged_regions_pending(composer) && paged_has_next_column(composer)) {
        if (composer->sheet_trial && composer->sheet_trial->replay &&
            composer->column_index + 1 == composer->sheet_trial->replay->count) break;
        status = paged_column_open(composer);
        composer->auxiliary_page = true;
        if (status == TYPESET_OK && composer->sheet_trial && cursor) status = paged_sheet_aux_column(composer, cursor);
        if (status == TYPESET_OK) status = paged_regions_close(composer);
    }
    *finished = status == TYPESET_OK && !paged_regions_pending(composer);
    composer->depth = depth;
    composer->pending_break = pending_break; composer->requested_break = requested_break;
    return status;
}

static TypesetStatus paged_sheet_restyle(PagedCheckpoint* sheet, const RadiantPageSequence* sequence,
        uint32_t terminal_page, bool retain_tail, bool policy_trial) {
    PagedComposer* composer = sheet->composer;
    TypesetStatus status = sheet->restore();
    if (status == TYPESET_OK) status = sheet->begin(composer);
    if (status != TYPESET_OK) return status;
    composer->sequence = sequence; composer->page_name = sequence ? sequence->master_reference : nullptr;
    composer->terminal_page = terminal_page; composer->retain_aux_tail = retain_tail; composer->policy_trial = policy_trial;
    return paged_empty_page_restyle(composer, false);
}


static TypesetStatus paged_regions_drain(PagedComposer* composer) {
    const RadiantPageSequence* pending_sequence = composer->sequence;
    const char* pending_name = composer->page_name;
    size_t depth = composer->depth;
    // auxiliary-only sheets stay with their producing sequence until every queue has drained.
    const RadiantPageSequence* sequence = composer->page ? composer->page->sequence.get() : nullptr;
    composer->sequence = sequence;
    if (sequence) composer->page_name = sequence->master_reference;
    composer->depth = 0;
    TypesetStatus status = TYPESET_OK;
    TypesetPageAssembly assembly = {};
    while (status == TYPESET_OK && paged_regions_pending(composer)) {
        composer->terminal_page = 0;
        // a policy-selected physical eject leaves its remaining columns empty.
        bool custom = composer->composition->options.page_policy != nullptr;
        bool column = !custom && paged_has_next_column(composer);
        status = column ? paged_next_column(composer) : paged_page_create(composer, false);
        if (status != TYPESET_OK) break;
        composer->auxiliary_page = true;
        PagedCheckpoint sheet;
        status = sheet.begin(composer);
        if (status != TYPESET_OK) break;
        bool provisional = !column && custom && sequence &&
            sequence->master_program && sequence->master_program->terminal;
        bool reinsert = false;
        auto layout = [&](bool* finished) {
            return custom ? paged_policy_aux_sheet(composer, &assembly, finished, &reinsert) :
                paged_sheet_tail_columns(composer, finished);
        };
        do {
            reinsert = false; composer->policy_trial = provisional;
            if (!column) status = paged_regions_prepare(composer);
            bool ordinary_finished = false, terminal_finished = false;
            if (status == TYPESET_OK) status = layout(&ordinary_finished);
            if (!column && sequence && sequence->master_program && sequence->master_program->terminal &&
                (status == TYPESET_OK || status == TYPESET_UNPLACEABLE) && !paged_sequence_padding(composer->page, sequence->next)) {
                status = paged_sheet_restyle(&sheet, sequence, composer->page->page_number, false, provisional);
                if (status == TYPESET_OK) status = layout(&terminal_finished);
                if (status == TYPESET_UNPLACEABLE || (status == TYPESET_OK && !terminal_finished)) {
                    status = paged_sheet_restyle(&sheet, sequence, 0, ordinary_finished, provisional);
                    if (status == TYPESET_OK) status = layout(&ordinary_finished);
                }
            }
            if (status == TYPESET_OK && provisional) {
                uint32_t terminal_page = composer->terminal_page;
                bool retain_tail = composer->retain_aux_tail;
                status = paged_sheet_restyle(&sheet, sequence, terminal_page, retain_tail, false);
                if (status == TYPESET_OK) status = layout(&ordinary_finished);
            }
            if (status == TYPESET_OK && reinsert) {
                TypesetPolicyCheckpoint progressed = {};
                status = typeset_policy_checkpoint(composer->composition->options.page_policy, &progressed);
                if (status == TYPESET_OK) status = sheet.restore();
                if (status == TYPESET_OK) status = typeset_policy_restore(composer->composition->options.page_policy, &progressed);
                if (status == TYPESET_OK) status = sheet.begin(composer);
            }
        } while (status == TYPESET_OK && reinsert);
        status = status == TYPESET_OK ? sheet.accept() : sheet.fail(status);
    }
    composer->depth = depth;
    composer->sequence = pending_sequence; composer->page_name = pending_name;
    composer->terminal_page = 0; composer->retain_aux_tail = false;
    return status;
}

static float paged_tail_edges(const PagedComposer* composer, bool closes) {
    float extent = 0.0f;
    for (size_t i = composer->depth; i > 0; i--) {
        const PagedFrame& frame = composer->frames[i - 1];
        extent += frame.table_footer_height > 0.0f && paged_table_omits(frame.flow, true) && !closes && !frame.table_terminal
            ? frame.flow->table_spacing_v : frame.table_footer_height;
        extent += paged_fragment_edge(frame.flow->style, frame.box, 2, closes);
        closes = closes && !frame.flow->next;
    }
    return extent;
}

static TypesetStatus paged_region_anchor(PagedComposer* composer, PagedRegionKind kind,
        const TypesetRegionMaterial* material, size_t* count) {
    if (*count >= composer->composition->options.max_nodes) return TYPESET_BUDGET_EXHAUSTED;
    PagedRegionState* region = &composer->regions[kind];
    if (!lam::pool_grow_array(composer->composition->pool, &region->anchors, &region->anchor_capacity, *count + 1, 16))
        return TYPESET_OUT_OF_MEMORY;
    region->anchors[(*count)++] = material;
    return TYPESET_OK;
}

static TypesetStatus paged_regions_for_lines(PagedComposer* composer, PagedFlowNode* flow,
        const TypesetLineCandidate* lines, size_t fit, PagedRegionTrial* trial) {
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) trial->anchor_counts[i] = composer->regions[i].anchor_count;
    float body_height = paged_body_height(composer);
    for (size_t i = 0; i < fit; i++) {
        body_height += lines[i].height;
        for (size_t j = lines[i].first; j < lines[i].next; j++) {
            const PagedSourceRecord* source = (const PagedSourceRecord*)flow->items[j].source.native.get();
            if (!source || !source->insertion) continue;
            PagedRegionKind kind = source->insertion->kind;
            size_t* count = &trial->anchor_counts[kind];
            TypesetStatus status = paged_region_anchor(composer, kind, &source->insertion->material, count);
            if (status != TYPESET_OK) return status;
        }
    }
    if (fit) body_height += paged_tail_edges(composer,
        lines[fit - 1].next == flow->paragraph.count && !flow->next);
    return paged_regions_trial(composer, body_height, trial);
}

static TypesetStatus paged_flow_cursor_copy(void*, const TypesetResume* cursor, TypesetResume* copied) {
    *copied = *cursor; return TYPESET_OK;
}
static size_t paged_block_note_prefix(PagedFlowNode* flow, const TypesetLineCandidate* lines, size_t count) {
    size_t prefix = 0;
    for (size_t i = 0; i < count; i++) for (size_t j = lines[i].first; j < lines[i].next; j++) {
        const PagedSourceRecord* source = (const PagedSourceRecord*)flow->items[j].source.native.get();
        if (source && source->insertion && source->insertion->block_policy) prefix = i + 1;
    }
    return prefix;
}

struct PagedHeightMeasurement {
    PagedComposer composer = {};
    float baseline = -1.0f;
    float before = -1.0f;
    ~PagedHeightMeasurement() {
        Pool* pool = composer.composition->pool;
        if (composer.frames) pool_free(pool, composer.frames);
        if (composer.spaces) pool_free(pool, composer.spaces);
    }
};

static TypesetStatus paged_flow_height_inner(PagedHeightMeasurement* measurement, PagedFlowNode* flow,
        float parent_width, float parent_height, bool leading, bool* forced, bool allow_overflow) {
    PagedComposer* composer = &measurement->composer;
    if (flow->kind == PAGED_FLOW_PARAGRAPH) {
        size_t capacity = flow->paragraph.count;
        if (!capacity) return TYPESET_OK;
        Pool* pool = composer->composition->pool;
        TypesetLineCandidate* scratch = (TypesetLineCandidate*)pool_alloc(pool, capacity * sizeof(TypesetLineCandidate));
        if (!scratch) return TYPESET_OUT_OF_MEMORY;
        size_t first = 0, count = 0;
        TypesetStatus status = TYPESET_OK;
        while (first < capacity && (!leading || count < flow->style->orphans)) {
            TypesetLineCandidate line = {};
            status = paged_next_line(composer, flow, first, parent_width, parent_height, scratch, capacity, &line);
            if (status != TYPESET_OK || (line.overflow && !allow_overflow)) { if (status == TYPESET_OK) status = TYPESET_UNPLACEABLE; break; }
            if (line.height > 0.0f) status = paged_space_flush(composer);
            if (status != TYPESET_OK) break;
            if (measurement->baseline < 0.0f && line.height > 0.0f)
                measurement->baseline = composer->y + paged_line_baseline(flow, line);
            if (measurement->before < 0.0f && composer->depth == 1 && line.height > 0.0f)
                measurement->before = composer->y;
            composer->y += line.height; first = line.next; count++;
            if (line.height > 0.0f) composer->page_has_content = true;
        }
        pool_free(pool, scratch);
        return status;
    }
    PagedFrame frame = {}; frame.flow = flow;
    TypesetStatus status = flow->style->flow_traits ? paged_space_append(composer, flow->style->flow_traits->before) : TYPESET_OK;
    if (status == TYPESET_OK) status = paged_frame_measure(composer, &frame, 0.0f, parent_width, parent_height);
    if (status != TYPESET_OK) return status;
    if (frame.box.edges[0] != 0.0f || paged_is_table_grid(flow) || flow->image) status = paged_space_flush(composer);
    if (status != TYPESET_OK) return status;
    if (paged_is_table_grid(flow) || flow->image) {
        if (measurement->before < 0.0f && composer->depth == 1)
            measurement->before = composer->y + frame.box.edges[0];
        float height = frame.box.edges[0] + frame.replaced_height + frame.box.edges[2];
        if (paged_is_table_grid(flow)) status = paged_table_height(composer, flow, parent_width, parent_height, leading, &height, forced);
        if (status != TYPESET_OK) return status;
        composer->y += height;
        if (height > 0.0f) composer->page_has_content = true;
    } else {
        if (!lam::pool_grow_array(composer->composition->pool, &composer->frames, &composer->frame_capacity,
            composer->depth + 1, 16)) return TYPESET_OUT_OF_MEMORY;
        size_t index = composer->depth++; frame.page_start = composer->y; composer->frames[index] = frame;
        composer->y += frame.box.edges[0];
        if (flow->native && composer->role != VIEW_FRAGMENT_BODY) {
            status = paged_native_fixed_flow(composer, flow, true);
            if (status != TYPESET_OK) return status;
        }
        float pending = 0.0f;
        for (PagedFlowNode* child = flow->first_child; child; child = child->next) {
            if (child->style->break_before >= VIEW_BREAK_COLUMN || child->style->break_after >= VIEW_BREAK_COLUMN) *forced = true;
            float top = paged_flow_margin(composer, child, 0, frame.content_width);
            if (top || pending) status = paged_space_flush(composer);
            if (status != TYPESET_OK) return status;
            composer->y += layout_collapse_margins(pending, top);
            float start = composer->y;
            status = paged_flow_height_inner(measurement, child, frame.content_width, frame.content_height, leading, forced, allow_overflow);
            if (status != TYPESET_OK) return status;
            pending = paged_flow_margin(composer, child, 2, frame.content_width);
            if (leading && composer->y > start) break;
        }
        if (flow->kind == PAGED_FLOW_TABLE_WRAPPER) composer->y += pending;
        if (frame.box.edges[2] != 0.0f || frame.content_height > 0.0f) status = paged_space_flush(composer);
        if (status != TYPESET_OK) return status;
        float start = composer->frames[index].page_start;
        // relative-before aligns the first child's content edge, including that child's decoration.
        if (measurement->before < 0.0f && index == 1) measurement->before = start + frame.box.edges[0];
        composer->y = start + frame.box.edges[0] + fmaxf(composer->y - start - frame.box.edges[0], frame.content_height) + frame.box.edges[2];
        if (measurement->baseline < 0.0f && flow->table_cell) measurement->baseline = composer->y - frame.box.edges[2];
        composer->depth--;
        if (composer->space_count && composer->space_depth > composer->depth) composer->space_depth = composer->depth;
    }
    return flow->style->flow_traits ? paged_space_append(composer, flow->style->flow_traits->after) : TYPESET_OK;
}

static TypesetStatus paged_flow_height(PagedComposer* composer, PagedFlowNode* flow,
        float parent_width, float parent_height, bool leading, float* result, bool* forced,
        bool allow_overflow, float* baseline, float* before) {
    PagedReferenceMeasurement references(composer->composition);
    // pure measurement shares space resolution and font/box metrics with replay, without publishing occurrences.
    PagedHeightMeasurement measurement;
    measurement.composer.tree = composer->tree; measurement.composer.composition = composer->composition;
    measurement.composer.page = composer->page; measurement.composer.page_style = composer->page_style;
    measurement.composer.role = composer->role;
    measurement.composer.page_style.content_rect.x = measurement.composer.page_style.content_rect.y = 0.0f;
    if (composer->depth) {
        float reference_x;
        paged_reference_geometry(composer, composer->depth, &reference_x, &measurement.composer.page_style.content_rect.width);
    }
    measurement.composer.initial_containing_block = composer->initial_containing_block;
    measurement.composer.atomic_fragment = true;
    TypesetStatus status = paged_flow_height_inner(&measurement, flow, parent_width, parent_height, leading, forced, allow_overflow);
    if (status == TYPESET_OK) status = paged_space_flush(&measurement.composer, true);
    *result = measurement.composer.y;
    if (baseline) *baseline = measurement.baseline;
    if (before) *before = measurement.before;
    return status;
}

static TypesetStatus paged_intrinsic_admit(PagedComposer* composer, ViewCssStyle* style, bool cell) {
    TypesetStatus status = paged_size_keywords_admit(composer->composition, style);
    if (status != TYPESET_OK) return status;
    // a caption's cyclic percentage width contributes as auto; its used width resolves after the grid width settles.
    bool caption = style->display.inner == CSS_VALUE_TABLE_CAPTION;
    const CssValue* lengths[] = {cell || caption ? nullptr : style->width.get(), style->min_width.get(), style->max_width.get(),
        style->padding[3].get(), style->padding[1].get(), cell ? nullptr : style->margin[3].get(),
        cell ? nullptr : style->margin[1].get()};
    for (const CssValue* value : lengths)
        if (layout_css_value_has_percentage(value)) return paged_failure(composer->composition, TYPESET_INVALID,
            style->source, composer->page ? composer->page->page_number : 0,
            "cyclic descendant percentages require a table intrinsic sizing policy");
    return TYPESET_OK;
}

struct PagedIntrinsicMeasurement { PagedLineMeasurement line; float width; };
static bool paged_intrinsic_item_metrics(const TypesetParagraph* paragraph, size_t index, float,
        TypesetMetrics* metrics, void* context) {
    PagedIntrinsicMeasurement* measurement = (PagedIntrinsicMeasurement*)context;
    const PagedPaint* paint = (const PagedPaint*)paragraph->items[index].paint.get();
    if (!isfinite(measurement->width) && paint && paint->kind == PAGED_PAINT_IMAGE &&
        paged_intrinsic_admit(measurement->line.composer, paint->style, false) != TYPESET_OK) return false;
    const PagedImagePaint* image = paint && paint->kind == PAGED_PAINT_IMAGE ? (const PagedImagePaint*)paint : nullptr;
    // image facts already contain the shared natural/default object rectangle.
    float basis = isfinite(measurement->width) ? measurement->width : image ? image->facts.width : 0.0f;
    return paged_line_item_metrics(paragraph, index, basis,
        metrics, &measurement->line);
}

static TypesetStatus paged_flow_intrinsic(PagedComposer* composer, PagedFlowNode* flow,
        float basis, PagedIntrinsic* result) {
    *result = {};
    if (!flow) return TYPESET_OK;
    result->generated = true;
    if (flow->kind == PAGED_FLOW_PARAGRAPH && !flow->image) {
        if (!flow->paragraph.count) return TYPESET_OK;
        Pool* pool = composer->composition->pool;
        TypesetLineCandidate* scratch = (TypesetLineCandidate*)pool_alloc(pool,
            flow->paragraph.count * sizeof(TypesetLineCandidate));
        if (!scratch) return TYPESET_OUT_OF_MEMORY;
        TypesetParagraph paragraph = flow->paragraph;
        PagedIntrinsicMeasurement measurement = {{composer, flow, NAN, NAN}, basis};
        if (flow->has_dynamic_items) { paragraph.context = &measurement; paragraph.measure = paged_intrinsic_item_metrics; }
        // use the producer's legal boundaries and glue trimming for both intrinsic extremes.
        const float widths[] = {FLT_MIN, FLT_MAX};
        float* results[] = {&result->minimum, &result->maximum};
        TypesetStatus status = TYPESET_OK;
        for (size_t pass = 0; pass < 2 && status == TYPESET_OK; pass++) {
            for (size_t first = 0; first < paragraph.count;) {
                TypesetLineCandidate line = {};
                status = typeset_next_line(&paragraph, first, widths[pass], scratch, paragraph.count, &line);
                if (status != TYPESET_OK) break;
                *results[pass] = fmaxf(*results[pass], line.width); first = line.next;
            }
        }
        pool_free(pool, scratch);
        if (status != TYPESET_OK && composer->composition->diagnostic.status != TYPESET_OK)
            return composer->composition->diagnostic.status;
        return status;
    }
    ViewCssStyle* style = flow->style;
    if (!isfinite(basis)) {
        TypesetStatus status = paged_intrinsic_admit(composer, style, flow->table_cell);
        if (status != TYPESET_OK) return status;
    }
    if (flow->image) {
        PagedImageMeasure image = {};
        TypesetStatus status = paged_image_measure(composer, flow->image,
            isfinite(basis) ? basis : flow->image->facts.width, NAN, &image);
        if (status != TYPESET_OK) return status;
        result->minimum = result->maximum = image.width + image.box.edges[3] + image.box.edges[1] +
            image.margin[3] + image.margin[1];
        return TYPESET_OK;
    }
    for (PagedFlowNode* child = flow->first_child; child; child = child->next) {
        PagedIntrinsic widths = {};
        TypesetStatus status = paged_flow_intrinsic(composer, child, basis, &widths);
        if (status != TYPESET_OK) return status;
        result->minimum = fmaxf(result->minimum, widths.minimum);
        result->maximum = fmaxf(result->maximum, widths.maximum);
    }
    PagedBoxEdges box = {};
    TypesetStatus status = paged_box_edges(composer, style, isfinite(basis) ? basis : 0.0f, &box);
    if (status != TYPESET_OK) return status;
    float edges = box.edges[3] + box.edges[1];
    auto outer = [&](const CssValue* value, CssPropertyCode property, float fallback) {
        float used = paged_used_length(composer, style, value, property, basis, NAN);
        return isfinite(used) ? fmaxf(edges, used + (style->box_sizing == CSS_VALUE_BORDER_BOX ? 0.0f : edges)) : fallback;
    };
    float specified = outer(style->width, CSS_PROPERTY_WIDTH, NAN);
    float minimum = outer(style->min_width, CSS_PROPERTY_MIN_WIDTH, edges);
    float maximum = outer(style->max_width, CSS_PROPERTY_MAX_WIDTH, INFINITY);
    result->minimum += edges; result->maximum += edges;
    if (flow->table_cell || style->display.inner == CSS_VALUE_TABLE_CAPTION) {
        // a cell width is a column minimum; it cannot erase an unbreakable content contribution.
        minimum = fmaxf(minimum, isfinite(specified) ? specified : 0.0f);
        result->minimum = fmaxf(result->minimum, minimum);
        result->maximum = fmaxf(result->minimum, fminf(result->maximum, maximum));
    } else {
        if (isfinite(specified)) result->minimum = result->maximum = specified;
        result->minimum = fmaxf(minimum, fminf(result->minimum, maximum));
        result->maximum = fmaxf(result->minimum, fminf(result->maximum, maximum));
    }
    if (!flow->table_cell) {
        float margins = paged_flow_margin(composer, flow, 3, basis) + paged_flow_margin(composer, flow, 1, basis);
        result->minimum += margins; result->maximum += margins;
    }
    return TYPESET_OK;
}

struct PagedTableSpanMeasure { size_t column, span, order; PagedIntrinsic widths; };


static ViewCssStyle* paged_table_column_width_style(PagedFlowNode* column, bool fixed) {
    ViewCssStyle* style = column->style;
    // automatic tracks use a group's width for otherwise automatic child columns.
    if (!fixed && (!style->width || css_value_is_auto(style->width)) &&
        column->parent->kind == PAGED_FLOW_TABLE_COLUMN) return column->parent->style;
    return style;
}

static int paged_table_span_order(const void* left, const void* right) {
    const PagedTableSpanMeasure* a = (const PagedTableSpanMeasure*)left;
    const PagedTableSpanMeasure* b = (const PagedTableSpanMeasure*)right;
    if (a->span != b->span) return a->span < b->span ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order ? 1 : 0;
}

static TypesetStatus paged_table_measure(PagedComposer* composer, PagedFlowNode* table) {
    if (table->table_measures.count) return TYPESET_OK;
    Pool* pool = composer->composition->pool;
    float* minimum = (float*)pool_calloc(pool, table->columns * sizeof(float));
    float* maximum = (float*)pool_calloc(pool, table->columns * sizeof(float));
    float* single = (float*)pool_calloc(pool, table->columns * sizeof(float));
    float* percentage = (float*)pool_calloc(pool, table->columns * sizeof(float));
    bool* constrained = (bool*)pool_calloc(pool, table->columns * sizeof(bool));
    size_t capacity = 0, count = 0;
    for (PagedFlowNode* row = table->first_child; row; row = row->next) capacity += row->cell_count;
    PagedTableSpanMeasure* spans = (PagedTableSpanMeasure*)pool_alloc(pool, capacity * sizeof(PagedTableSpanMeasure));
    if (!minimum || !maximum || !single || !percentage || !constrained || !spans) return TYPESET_OUT_OF_MEMORY;
    TypesetStatus status = paged_table_columns_visit(table, [&](PagedFlowNode* column) {
        if (column->first_child) return TYPESET_OK;
        ViewCssStyle* style = paged_table_column_width_style(column, false);
        const CssValue* value = style->width;
        if (value && value->type != CSS_VALUE_TYPE_PERCENTAGE && layout_css_value_has_percentage(value))
            return paged_failure(composer->composition, TYPESET_INVALID, column->source, 0,
                "calculated column percentages require a table intrinsic sizing policy");
        float width = paged_used_length(composer, style, value, CSS_PROPERTY_WIDTH, NAN);
        for (size_t i = column->column; i < column->column + column->column_span; i++) {
            minimum[i] = maximum[i] = fmaxf(0.0f, width);
            if (value && value->type == CSS_VALUE_TYPE_PERCENTAGE) percentage[i] = (float)value->data.percentage.value;
            constrained[i] = value && !css_value_is_auto(value);
        }
        return TYPESET_OK;
    });
    if (status != TYPESET_OK) return status;
    for (PagedFlowNode* row = table->first_child; row; row = row->next) {
        for (PagedFlowNode* cell = row->first_child; cell; cell = cell->next) {
            size_t column = cell->column, span = cell->column_span;
            PagedIntrinsic sizes = {};
            TypesetStatus status = paged_flow_intrinsic(composer, cell, NAN, &sizes);
            if (status != TYPESET_OK) return status;
            if (span == 1) {
                minimum[column] = fmaxf(minimum[column], sizes.minimum);
                maximum[column] = fmaxf(maximum[column], sizes.maximum);
            } else {
                spans[count] = {column, span, count, sizes}; count++;
            }
            const CssValue* width = cell->style->width;
            if (width && width->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float percent = (float)width->data.percentage.value / span;
                for (size_t i = column; i < column + span; i++) percentage[i] = fmaxf(percentage[i], percent);
            }
            else if (layout_css_value_has_percentage(width)) return paged_failure(composer->composition, TYPESET_INVALID,
                cell->source, 0, "calculated cell percentages require a table intrinsic sizing policy");
            else if (span == 1 && width && !css_value_is_auto(width)) constrained[column] = true;
        }
    }
    memcpy(single, minimum, table->columns * sizeof(float));
    LayoutTableColumnWidths measures = {minimum, maximum, single, percentage, constrained, table->columns};
    // apply shorter spans first, retaining source order for ties and single-column constraints.
    qsort(spans, count, sizeof(PagedTableSpanMeasure), paged_table_span_order);
    for (size_t i = 0; i < count; i++) {
        const PagedTableSpanMeasure& span = spans[i];
        float* tracks[] = {minimum, maximum};
        const float required[] = {span.widths.minimum, span.widths.maximum};
        for (size_t pass = 0; pass < 2; pass++) {
            float current = layout_table_span_width(tracks[pass], table->columns, span.column,
                span.span, table->table_spacing_h);
            layout_table_distribute_span_extra(tracks[pass], measures, span.column, span.span, required[pass] - current);
        }
    }
    pool_free(pool, spans);
    float min_total = 0.0f, max_total = 0.0f, percent_total = 0.0f;
    for (size_t i = 0; i < table->columns; i++) {
        maximum[i] = fmaxf(maximum[i], minimum[i]);
        percentage[i] = fminf(percentage[i], 100.0f - percent_total);
        percent_total += percentage[i]; min_total += minimum[i]; max_total += maximum[i];
    }
    table->table_minimum = min_total;
    table->table_maximum = fmaxf(max_total, layout_table_percent_preferred_width(measures, percent_total));
    table->table_percent = percent_total; table->table_measures = measures;
    return TYPESET_OK;
}

static TypesetStatus paged_table_tracks(PagedComposer* composer, PagedFlowNode* table,
        float width, PagedTableTracks** result) {
    for (PagedTableTracks* tracks = table->table_tracks; tracks; tracks = tracks->next)
        if (tracks->width == width) { *result = tracks; return TYPESET_OK; }
    Pool* pool = composer->composition->pool;
    PagedTableTracks* tracks = (PagedTableTracks*)pool_calloc(pool, sizeof(PagedTableTracks));
    float* columns = (float*)pool_calloc(pool, table->columns * sizeof(float));
    float* proportions = table->table_proportional ? (float*)pool_calloc(pool, table->columns * sizeof(float)) : nullptr;
    if (!tracks || !columns || (table->table_proportional && !proportions)) return TYPESET_OUT_OF_MEMORY;
    // spacing belongs to the grid, so percentages and column distribution share the remaining track width.
    float available = width - paged_table_spacing_width(table);
    if (!isfinite(available) || available <= 0.0f) return paged_failure(composer->composition,
        TYPESET_UNPLACEABLE, table->source, composer->page ? composer->page->page_number : 0,
        "table spacing leaves no room for its tracks");
    if (paged_is_label_body_grid(table)) {
        // the gap is outside both decorated cells, rather than padding or outer table spacing.
        if (!radiant_label_body_size(radiant_label_body_geometry(table->style), available, &columns[0], &tracks->label_gap, &columns[1]))
            return paged_failure(composer->composition, TYPESET_INVALID, table->source, 0,
                "label/body geometry overlaps or leaves a nonpositive part width");
    } else if (table->table_fixed) {
        PagedFlowNode* first = nullptr;
        for (PagedFlowNode* row = table->first_child; row; row = row->next) {
            if (row->table_group == PAGED_TABLE_HEADER) { first = row; break; }
            if (!first && row->table_group == PAGED_TABLE_BODY) first = row;
        }
        TypesetStatus status = paged_table_columns_visit(table, [&](PagedFlowNode* column) {
            if (column->first_child) return TYPESET_OK;
            ViewCssStyle* style = paged_table_column_width_style(column, true);
            float proportion = style->flow_traits ? style->flow_traits->column_proportion : 0.0f;
            if (proportions) for (size_t i = column->column; i < column->column + column->column_span; i++) proportions[i] = proportion;
            float value = paged_used_length(composer, style, style->width, CSS_PROPERTY_WIDTH, available, NAN);
            if (!isfinite(value)) return !style->width || css_value_is_auto(style->width) ? TYPESET_OK :
                paged_failure(composer->composition, TYPESET_INVALID, column->source, 0, "column width must resolve in its table track budget");
            if (value < 0.0f || (value == 0.0f && proportion == 0.0f)) return paged_failure(composer->composition, TYPESET_INVALID, column->source, 0,
                "zero-width fixed columns require a zero-track continuation policy");
            for (size_t i = column->column; i < column->column + column->column_span; i++) columns[i] = value;
            return TYPESET_OK;
        });
        if (status != TYPESET_OK) return status;
        float specified = 0.0f; size_t unspecified = 0;
        for (PagedFlowNode* cell = first->first_child; cell; cell = cell->next) {
            ViewCssStyle* style = cell->style;
            float value = paged_used_length(composer, style, style->width, CSS_PROPERTY_WIDTH, available, NAN);
            if (isfinite(value)) {
                PagedBoxEdges box = {};
                TypesetStatus status = paged_box_edges(composer, style, width, &box);
                if (status != TYPESET_OK) return status;
                float outer = value + (style->box_sizing == CSS_VALUE_BORDER_BOX ? 0.0f : box.edges[3] + box.edges[1]);
                float each = fmaxf(0.0f, outer - (cell->column_span - 1) * table->table_spacing_h) / cell->column_span;
                // first-row widths only fill tracks without an explicit column width.
                for (size_t i = cell->column; i < cell->column + cell->column_span; i++)
                    if (columns[i] == 0.0f && (!proportions || proportions[i] == 0.0f)) columns[i] = each;
            }
        }
        for (size_t i = 0; i < table->columns; i++) {
            specified += columns[i];
            if (columns[i] == 0.0f) {
                unspecified++;
                if (proportions && proportions[i] == 0.0f) proportions[i] = 1.0f;
            }
        }
        float used = available;
        layout_table_distribute_fixed_columns(columns, table->columns, &used, specified, unspecified, proportions);
        float sum = 0.0f;
        for (size_t i = 0; i < table->columns; i++) sum += columns[i];
        if (used > available) return paged_failure(composer->composition, TYPESET_UNPLACEABLE, table->source,
            composer->page ? composer->page->page_number : 0, "fixed table tracks exceed their page region");
        // all-definite tracks share excess width; retain the author's table extent.
        if (!proportions && !unspecified && sum < available)
            for (size_t i = 0; i < table->columns; i++) columns[i] += (available - sum) / table->columns;
    } else {
        TypesetStatus status = paged_table_measure(composer, table);
        if (status != TYPESET_OK) return status;
        if (!layout_table_distribute_percent_columns(table->table_measures, columns,
                table->table_percent, available, table->table_minimum))
            layout_table_distribute_auto_columns(table->table_measures, columns,
                available, table->table_minimum, table->table_maximum);
    }
    float total = 0.0f;
    for (size_t i = 0; i < table->columns; i++) {
        if (!isfinite(columns[i]) || columns[i] <= 0.0f) return TYPESET_UNPLACEABLE;
        total += columns[i];
    }
    if (total + tracks->label_gap > available + 0.01f) return paged_failure(composer->composition, TYPESET_UNPLACEABLE, table->source,
        composer->page ? composer->page->page_number : 0, "table tracks exceed their page region");
    tracks->width = width; tracks->columns = columns; tracks->next = table->table_tracks;
    table->table_tracks = tracks; *result = tracks;
    return TYPESET_OK;
}

static bool paged_is_column_box(LayoutViewNode* node) {
    return node->computed_style && (node->computed_style->display.inner == CSS_VALUE_TABLE_COLUMN ||
        node->computed_style->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP);
}

static TypesetStatus paged_table_range_publish(ViewTree* tree, LayoutViewNode* node, PagedFlowNode* source) {
    ViewTableRange* range = (ViewTableRange*)arena_alloc(tree->model->arena, sizeof(ViewTableRange));
    if (!range) return TYPESET_OUT_OF_MEMORY;
    *range = {source->column, source->column_span, source->table_cell && !source->source}; node->table_range = lam::up(range);
    return TYPESET_OK;
}

static float paged_table_column_x(const PagedFrame* frame, size_t column, PagedTableTracks* tracks) {
    float preceding = layout_table_span_width(tracks->columns, frame->flow->columns, 0,
        column, frame->flow->table_spacing_h);
    return frame->content_x + frame->flow->table_spacing_h + preceding +
        (column ? frame->flow->table_spacing_h + tracks->label_gap : 0.0f);
}

static TypesetStatus paged_table_column_open(PagedComposer* composer, PagedFrame* frame,
        PagedTableTracks* tracks, PagedFlowNode* source, LayoutViewNode* parent) {
    float x = paged_table_column_x(frame, source->column, tracks);
    float width = layout_table_span_width(tracks->columns, frame->flow->columns,
        source->column, source->column_span, frame->flow->table_spacing_h);
    LayoutViewNode* box = view_tree_fragment_append(composer->tree, parent, source->source, {x, composer->y, width, 0.0f});
    if (!box) return TYPESET_OUT_OF_MEMORY;
    box->computed_style = lam::up(source->style); box->role = composer->role;
    box->generated = composer->role != VIEW_FRAGMENT_BODY;
    TypesetStatus status = paged_table_range_publish(composer->tree, box, source);
    if (status != TYPESET_OK) return status;
    for (PagedFlowNode* child = source->first_child; child; child = child->next) {
        TypesetStatus status = paged_table_column_open(composer, frame, tracks, child, box);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_table_columns_open(PagedComposer* composer, PagedFrame* frame) {
    PagedTableTracks* tracks = nullptr;
    TypesetStatus status = paged_table_tracks(composer, frame->flow, frame->content_width, &tracks);
    for (PagedFlowNode* source = frame->flow->column_sources; status == TYPESET_OK && source; source = source->next)
        status = paged_table_column_open(composer, frame, tracks, source, frame->fragment);
    return status;
}

static TypesetStatus paged_table_column_finish(ViewTree* tree, LayoutViewNode* node, const LayoutViewNode* table,
        float top, float bottom) {
    if (!view_tree_model_touch_node(tree, node)) return TYPESET_OUT_OF_MEMORY;
    node->rect.y = top; node->rect.height = fmaxf(0.0f, bottom - top);
    node->first_fragment = table->first_fragment; node->last_fragment = table->last_fragment;
    for (LayoutViewNode* child = node->first_child; child; child = child->next_sibling) {
        TypesetStatus status = paged_table_column_finish(tree, child, table, top, bottom);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_table_columns_finish(ViewTree* tree, LayoutViewNode* table) {
    float top = INFINITY, bottom = table->rect.y;
    for (LayoutViewNode* row = table->first_child; row; row = row->next_sibling) {
        if (paged_is_column_box(row)) continue;
        top = fminf(top, row->rect.y); bottom = fmaxf(bottom, row->rect.y + row->rect.height);
    }
    if (!isfinite(top)) top = bottom;
    for (LayoutViewNode* column = table->first_child; column && paged_is_column_box(column); column = column->next_sibling) {
        TypesetStatus status = paged_table_column_finish(tree, column, table, top, bottom);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static float paged_table_cell_width(PagedFlowNode* cell, PagedTableTracks* tracks) {
    PagedFlowNode* table = cell->parent->parent;
    return layout_table_span_width(tracks->columns, table->columns, cell->column, cell->column_span, table->table_spacing_h);
}

static PagedFlowNode* paged_table_next_body_row(PagedFlowNode* row) {
    for (PagedFlowNode* next = row->span_last ? row->span_last->next : row->next; next; next = next->next)
        if (next->table_group == PAGED_TABLE_BODY) return next;
    return nullptr;
}

static ViewBreak paged_flow_break(PagedFlowNode* flow, bool before) {
    if (!before && flow->span_last && flow->span_last != flow) flow = flow->span_last;
    ViewBreak own = paged_is_wrapped_grid(flow) ? VIEW_BREAK_AUTO :
        before ? flow->style->break_before : flow->style->break_after;
    if (own == VIEW_BREAK_AUTO && paged_is_table_grid(flow)) {
        PagedFlowNode* edge = nullptr;
        for (PagedFlowNode* row = flow->first_child; row; row = row->next)
            if (row->table_group == PAGED_TABLE_BODY) { edge = row; if (before) break; }
        // edge constraints also reach the adjoining outer block boundary.
        if (edge) return paged_flow_break(edge, before);
    }
    if (flow->kind != PAGED_FLOW_TABLE_ROW || !flow->row_group ||
        !(before ? flow->group_first : flow->group_last)) return own;
    ViewBreak group = before ? flow->row_group->break_before : flow->row_group->break_after;
    // the row boundary is later than its group boundary; forced values outrank soft keeps.
    if (own >= VIEW_BREAK_COLUMN) return own;
    if (group >= VIEW_BREAK_COLUMN) return group;
    if (own == VIEW_BREAK_AVOID || group == VIEW_BREAK_AVOID ||
        (own != VIEW_BREAK_AUTO && group != VIEW_BREAK_AUTO && own != group)) return VIEW_BREAK_AVOID;
    return own == VIEW_BREAK_AUTO ? group : own;
}

static PagedCellAnchor paged_table_cell_anchor(PagedFlowNode* cell) {
    if (cell->style->flow_traits && cell->style->flow_traits->relative_cell_before) return PAGED_CELL_BEFORE;
    // CSS 2.2 maps every cell alignment other than top/middle/bottom to baseline.
    const CssValue* align = cell->style->vertical_align;
    return !css_value_keyword_equals(align, CSS_VALUE_TOP) &&
        !css_value_keyword_equals(align, CSS_VALUE_MIDDLE) && !css_value_keyword_equals(align, CSS_VALUE_BOTTOM) ?
        PAGED_CELL_BASELINE : PAGED_CELL_UNALIGNED;
}

static TypesetStatus paged_table_cell_measure(PagedComposer* composer, PagedFlowNode* cell,
        float width, float containing_height, float* height, bool* forced, float* anchor) {
    PagedCellAnchor kind = paged_table_cell_anchor(cell);
    *anchor = -1.0f;
    TypesetStatus status = paged_flow_height(composer, cell, width, containing_height,
        false, height, forced, false, kind == PAGED_CELL_BASELINE ? anchor : nullptr,
        kind == PAGED_CELL_BEFORE ? anchor : nullptr);
    if (status == TYPESET_OK && kind == PAGED_CELL_BEFORE && *anchor < 0.0f) {
        PagedBoxEdges box = {};
        status = paged_box_edges(composer, cell->style, width, &box);
        *anchor = box.edges[0];
    }
    return status;
}

static TypesetStatus paged_table_row_minimum(PagedComposer* composer, PagedFlowNode* row,
        PagedTableTracks* tracks, float containing_height, float* height, bool* forced, PagedTableAlignment* alignment = nullptr) {
    *height = fmaxf(0.0f, paged_used_length(composer, row->style, row->style->height,
        CSS_PROPERTY_HEIGHT, tracks->width, 0.0f, containing_height));
    PagedTableAlignment ascent = {}, descent = {};
    for (PagedFlowNode* cell = row->first_child; cell; cell = cell->next) {
        float extent = 0.0f, cell_anchor = -1.0f;
        TypesetStatus status = paged_table_cell_measure(composer, cell, paged_table_cell_width(cell, tracks),
            containing_height, &extent, forced, &cell_anchor);
        if (status != TYPESET_OK) return status;
        // spanning cells supply their first-row baseline, but defer their height to the span.
        if (cell->row_span == 1) *height = fmaxf(*height, extent);
        if (cell_anchor >= 0.0f) {
            PagedCellAnchor kind = paged_table_cell_anchor(cell);
            ascent.anchors[kind] = fmaxf(ascent.anchors[kind], cell_anchor);
            if (cell->row_span == 1) descent.anchors[kind] = fmaxf(descent.anchors[kind], extent - cell_anchor);
        }
    }
    for (size_t i = 0; i < PAGED_CELL_UNALIGNED; i++) *height = fmaxf(*height, ascent.anchors[i] + descent.anchors[i]);
    if (alignment) *alignment = ascent;
    return TYPESET_OK;
}

static TypesetStatus paged_table_rows_measure(PagedComposer* composer, PagedFlowNode* table,
        PagedTableTracks* tracks, float containing_height, PagedTableRows** result) {
    PagedTableRows* rows = tracks->rows;
    while (rows && (rows->reference_revision != composer->composition->reference_revision ||
        !(rows->containing_height == containing_height ||
            (isnan(rows->containing_height) && isnan(containing_height))))) rows = rows->next;
    if (!rows) {
        Pool* pool = composer->composition->pool;
        rows = (PagedTableRows*)pool_calloc(pool, sizeof(PagedTableRows));
        if (!rows) return TYPESET_OUT_OF_MEMORY;
        rows->containing_height = containing_height;
        rows->heights = (float*)pool_calloc(pool, table->row_count * sizeof(float));
        rows->alignments = (PagedTableAlignment*)pool_calloc(pool, table->row_count * sizeof(PagedTableAlignment));
        if (!rows->heights || !rows->alignments) return TYPESET_OUT_OF_MEMORY;
        for (size_t i = 0; i < table->row_count; i++) {
            TypesetStatus status = paged_table_row_minimum(composer, table->table_rows[i], tracks,
                containing_height, &rows->heights[i], &rows->forced, &rows->alignments[i]);
            if (status != TYPESET_OK) return status;
        }
        for (size_t i = 0; i < table->row_count; i++)
            for (PagedFlowNode* cell = table->table_rows[i]->first_child; cell; cell = cell->next) {
                if (cell->row_span <= 1) continue;
                float extent = 0.0f, cell_anchor = -1.0f;
                TypesetStatus status = paged_table_cell_measure(composer, cell, paged_table_cell_width(cell, tracks),
                    containing_height, &extent, &rows->forced, &cell_anchor);
                if (status != TYPESET_OK) return status;
                if (cell_anchor >= 0.0f)
                    extent += fmaxf(0.0f, rows->alignments[i].anchors[paged_table_cell_anchor(cell)] - cell_anchor);
                layout_table_distribute_rowspan_height(rows->heights, table->row_count, i, cell->row_span,
                    extent, table->table_spacing_v, i + cell->row_span - 1);
            }
        // first-area refinement is another measurement input, including after rejected trials.
        rows->reference_revision = composer->composition->reference_revision;
        // immutable measurements survive trial rollback, keyed by both available dimensions.
        rows->next = tracks->rows; tracks->rows = rows;
    }
    *result = rows;
    return TYPESET_OK;
}

static TypesetStatus paged_table_row_height(PagedComposer* composer, PagedFlowNode* row,
        PagedTableTracks* tracks, float containing_height, float* height, bool* forced, PagedTableAlignment* alignment = nullptr) {
    if (!row->parent->has_rowspans)
        return paged_table_row_minimum(composer, row, tracks, containing_height, height, forced, alignment);
    PagedTableRows* rows = nullptr;
    TypesetStatus status = paged_table_rows_measure(composer, row->parent, tracks, containing_height, &rows);
    if (status != TYPESET_OK) return status;
    *height = rows->heights[row->row_index]; *forced |= rows->forced;
    if (alignment) *alignment = rows->alignments[row->row_index];
    return TYPESET_OK;
}

static TypesetStatus paged_table_dimensions(PagedComposer* composer, PagedFrame* frame) {
    PagedTableTracks* tracks = nullptr;
    TypesetStatus status = paged_table_tracks(composer, frame->flow, frame->content_width, &tracks);
    // every committed table fragment owns a trailing grid gap, also when there is no footer.
    frame->table_footer_height = frame->flow->table_spacing_v;
    for (PagedFlowNode* row = frame->flow->first_child; status == TYPESET_OK && row; row = row->next) {
        if (row->table_group != PAGED_TABLE_FOOTER) continue;
        float height = 0.0f; bool forced = false;
        status = paged_table_row_height(composer, row, tracks, frame->content_height, &height, &forced);
        frame->table_footer_height += frame->flow->table_spacing_v + height;
    }
    return status;
}

static TypesetStatus paged_table_height(PagedComposer* composer, PagedFlowNode* flow,
        float width, float height, bool leading, float* result, bool* forced) {
    PagedFrame frame = {}; frame.flow = flow;
    TypesetStatus status = paged_frame_measure(composer, &frame, 0.0f, width, height);
    PagedTableTracks* tracks = nullptr;
    if (status == TYPESET_OK) status = paged_table_tracks(composer, flow, frame.content_width, &tracks);
    *result = frame.box.edges[0] + frame.box.edges[2] + flow->table_spacing_v;
    PagedFlowNode* leading_last = nullptr;
    for (PagedFlowNode* row = flow->first_child; status == TYPESET_OK && row; row = row->next) {
        if (leading && leading_last && row->table_group == PAGED_TABLE_BODY && row->row_index > leading_last->row_index) continue;
        float extent = 0.0f;
        status = paged_table_row_height(composer, row, tracks, frame.content_height, &extent, forced);
        *result += flow->table_spacing_v + extent;
        if (row->table_group == PAGED_TABLE_BODY && !leading_last) leading_last = row->span_last ? row->span_last : row;
    }
    return status;
}

static LayoutViewNode* paged_table_row_box(PagedComposer* composer, PagedFrame* frame,
        PagedFlowNode* row, float height, ViewFragmentRole role) {
    LayoutViewNode* parent = frame->fragment;
    RdtLogicalRect rect = {frame->content_x + frame->flow->table_spacing_h, composer->y,
        frame->content_width - 2.0f * frame->flow->table_spacing_h, height};
    if (row->row_group) {
        parent = view_tree_fragment_append(composer->tree, parent, row->row_group->source, rect);
        if (!parent) return nullptr;
        parent->computed_style = lam::up(row->row_group);
        parent->paint_box = true; parent->role = role; parent->generated = role != VIEW_FRAGMENT_BODY;
    }
    LayoutViewNode* box = view_tree_fragment_append(composer->tree, parent, row->source, rect);
    if (!box) return nullptr;
    box->computed_style = lam::up(row->style); box->paint_box = true;
    box->role = role; box->generated = role != VIEW_FRAGMENT_BODY;
    return box;
}

static float paged_table_cell_offset(PagedFlowNode* cell, float height, float natural_height,
        const PagedTableAlignment& alignment, float cell_anchor = -1.0f) {
    PagedCellAnchor kind = paged_table_cell_anchor(cell);
    if (kind != PAGED_CELL_UNALIGNED)
        return cell_anchor >= 0.0f ? fmaxf(0.0f, alignment.anchors[kind] - cell_anchor) : 0.0f;
    const CssValue* align = cell->style->vertical_align;
    auto valign = css_value_keyword_equals(align, CSS_VALUE_MIDDLE) ? TableCellProp::CELL_VALIGN_MIDDLE :
        css_value_keyword_equals(align, CSS_VALUE_BOTTOM) ? TableCellProp::CELL_VALIGN_BOTTOM : TableCellProp::CELL_VALIGN_TOP;
    return layout_table_cell_vertical_align_target(valign, height, natural_height, 0.0f, true);
}

static TypesetStatus paged_table_row_emit(PagedComposer* composer, PagedFrame* frame,
        PagedFlowNode* row, PagedTableTracks* tracks, float height, const PagedTableAlignment& alignment, ViewFragmentRole role) {
    LayoutViewNode* box = paged_table_row_box(composer, frame, row, height, role);
    if (!box) return TYPESET_OUT_OF_MEMORY;
    TypesetStatus status = paged_mark_enter(composer, row->source);
    for (PagedFlowNode* cell = row->first_child; status == TYPESET_OK && cell; cell = cell->next) {
        // each cell has independent block state; only its committed row advances the outer flow.
        PagedComposer nested = {}; nested.tree = composer->tree; nested.composition = composer->composition;
        nested.page = composer->page; nested.page_style = composer->page_style;
        nested.initial_containing_block = composer->initial_containing_block;
        nested.y = composer->y; nested.bottom = INFINITY; nested.role = role; nested.atomic_fragment = true;
        PagedFrame containing = {}; containing.flow = row; containing.fragment = box;
        containing.content_x = paged_table_column_x(frame, cell->column, tracks); containing.content_width = paged_table_cell_width(cell, tracks);
        containing.content_height = frame->content_height;
        if (!lam::pool_grow_array(composer->composition->pool, &nested.frames, &nested.frame_capacity, 1, 16))
            return TYPESET_OUT_OF_MEMORY;
        nested.depth = 1; nested.frames[0] = containing;
        float natural_height = 0.0f, cell_anchor = -1.0f, measured_height = 0.0f; bool forced = false;
        if (paged_table_cell_anchor(cell) != PAGED_CELL_UNALIGNED)
            status = paged_table_cell_measure(composer, cell, containing.content_width,
                containing.content_height, &measured_height, &forced, &cell_anchor);
        if (status == TYPESET_OK) status = paged_fixed_flow(&nested, cell, &natural_height);
        if (status == TYPESET_OK) {
            LayoutViewNode* occurrence = box->last_child;
            if (!view_tree_model_touch_node(composer->tree, occurrence)) status = TYPESET_OUT_OF_MEMORY;
            else {
                float cell_height = height;
                if (cell->row_span > 1) {
                    PagedTableRows* rows = nullptr;
                    status = paged_table_rows_measure(composer, frame->flow, tracks, frame->content_height, &rows);
                    if (status == TYPESET_OK) {
                        cell_height = layout_table_span_width(rows->heights, frame->flow->row_count,
                            row->row_index, cell->row_span, frame->flow->table_spacing_v);
                    }
                }
                occurrence->rect.height = cell_height;
                if (!cell->source || frame->flow->column_sources) status = paged_table_range_publish(composer->tree, occurrence, cell);
                float offset = paged_table_cell_offset(cell, cell_height, natural_height, alignment, cell_anchor);
                if (status == TYPESET_OK && offset != 0.0f) status = paged_fragments_translate(composer->tree, occurrence->first_child, offset, false);
            }
        }
        pool_free(composer->composition->pool, nested.frames);
    }
    if (status == TYPESET_OK) composer->y += height;
    return status;
}

struct PagedCellChunk {
    PagedCellChunk* next_chunk;
    const PagedCellLeaf* leaf;
    PagedFrame* frames;
    TypesetLineCandidate* lines;
    size_t depth, count, first, next;
};
struct PagedTableCellSlice {
    PagedFlowNode* cell;
    PagedCellChunk* chunks;
    PagedCellChunk* last_chunk;
    PagedFrame outer;
    size_t first, next;
    float height, anchor, offset;
    bool active;
};
static void paged_cell_chunk_free(Pool* pool, PagedCellChunk* chunk) {
    if (chunk->lines) pool_free(pool, chunk->lines);
    if (chunk->frames) pool_free(pool, chunk->frames);
    pool_free(pool, chunk);
}
struct PagedTableSlice {
    Pool* pool;
    PagedTableCellSlice* cells;
    size_t count;
    PagedTableAlignment alignment;
    ~PagedTableSlice() { clear(); }
    void clear() {
        if (!cells) return;
        for (size_t i = 0; i < count; i++) for (PagedCellChunk* chunk = cells[i].chunks; chunk;) {
            PagedCellChunk* next = chunk->next_chunk; paged_cell_chunk_free(pool, chunk); chunk = next;
        }
        pool_free(pool, cells); cells = nullptr; count = 0; alignment = {};
    }
};

static TypesetStatus paged_cell_sources(PagedComposer* composer, PagedFlowNode* cell) {
    if (cell->cell_leaf_count) return TYPESET_OK;
    size_t cursor = 0;
    auto leaf = [&](PagedFlowNode* flow, size_t items) {
        if (items >= SIZE_MAX - cursor || cell->cell_leaf_count >= composer->composition->options.max_nodes)
            return TYPESET_BUDGET_EXHAUSTED;
        if (!lam::pool_grow_array(composer->composition->pool, &cell->cell_leaves, &cell->cell_leaf_capacity,
            cell->cell_leaf_count + 1, 8)) return TYPESET_OUT_OF_MEMORY;
        // the closing position distinguishes empty blocks and adjoining paragraph ends.
        size_t begin = cursor; cursor += items + 1;
        cell->cell_leaves[cell->cell_leaf_count++] = {flow, begin, cursor}; return TYPESET_OK;
    };
    auto visit = [&](PagedFlowStepKind kind, PagedFlowNode* flow) {
        if (kind == PAGED_STEP_BEFORE && (flow->kind != PAGED_FLOW_BLOCK || paged_is_table_grid(flow)))
            return paged_failure(composer->composition, TYPESET_INVALID, flow->source, composer->page->page_number,
                "split cells with nested formatting contexts require a nested subflow producer");
        if (kind == PAGED_STEP_OPEN) {
            flow->cell_begin = cursor;
            if (!flow->first_child) return leaf(flow, 0);
        } else if (kind == PAGED_STEP_LINE) {
            flow->cell_begin = cursor;
            TypesetStatus status = leaf(flow, flow->paragraph.count); flow->cell_end = cursor; return status;
        } else if (kind == PAGED_STEP_CLOSE) flow->cell_end = cursor;
        return TYPESET_OK;
    };
    TypesetStatus status = paged_flow_walk(cell, 0, composer->composition->options.max_depth, visit);
    if (status != TYPESET_OK) cell->cell_leaf_count = 0;
    return status;
}

static const PagedCellLeaf* paged_cell_leaf(PagedFlowNode* cell, size_t cursor) {
    size_t first = 0, last = cell->cell_leaf_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (cell->cell_leaves[middle].end <= cursor) first = middle + 1; else last = middle;
    }
    return first < cell->cell_leaf_count ? &cell->cell_leaves[first] : nullptr;
}

static TypesetStatus paged_cell_frames(PagedComposer* composer, PagedFlowNode* cell, PagedCellChunk* chunk,
        float width, float containing_height) {
    PagedFlowNode* owner = chunk->leaf->flow;
    if (owner->kind == PAGED_FLOW_PARAGRAPH) owner = owner->parent;
    for (PagedFlowNode* flow = owner; flow; flow = flow->parent) {
        if (chunk->depth++ >= composer->composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
        if (flow == cell) break;
    }
    chunk->frames = (PagedFrame*)pool_calloc(composer->composition->pool, chunk->depth * sizeof(PagedFrame));
    if (!chunk->frames) return TYPESET_OUT_OF_MEMORY;
    for (size_t i = chunk->depth; i > 0; i--, owner = owner->parent) chunk->frames[i - 1].flow = owner;
    float x = 0.0f, reference_x = x, reference_width = width;
    for (size_t i = 0; i < chunk->depth; i++) {
        PagedFrame* frame = &chunk->frames[i]; PagedFlowNode* flow = frame->flow; ViewCssStyle* style = flow->style;
        if (style->height || style->min_height || style->max_height)
            return paged_failure(composer->composition, TYPESET_INVALID, flow->source, composer->page->page_number,
                "split cells currently require inline flow with automatic height");
        if (i) {
            for (size_t edge = 0; edge < 4; edge++) if (style->margin[edge] &&
                paged_used_length(composer, style, style->margin[edge], radiant_box_side_property(CSS_PROPERTY_MARGIN,
                    static_cast<CssBoxSide>(edge)), width) != 0.0f)
                return paged_failure(composer->composition, TYPESET_INVALID, flow->source, composer->page->page_number,
                    "split cell block margins require a block-flow spacing policy");
            const RadiantFlowTraits* traits = style->flow_traits;
            if (traits && (traits->before.specified || traits->after.specified))
                return paged_failure(composer->composition, TYPESET_INVALID, flow->source, composer->page->page_number,
                    "split cell block spaces require a block-flow spacing policy");
            if (traits) for (size_t scope = 1; scope < 3; scope++)
                if (traits->together.scope[scope].kind != RADIANT_KEEP_AUTO || traits->next.scope[scope].kind != RADIANT_KEEP_AUTO ||
                    traits->previous.scope[scope].kind != RADIANT_KEEP_AUTO)
                    return paged_failure(composer->composition, TYPESET_INVALID, flow->source, composer->page->page_number,
                        "split cell block keeps require joint subflow boundary selection");
        }
        TypesetStatus status = paged_frame_descend(composer, frame, &x, &width, &containing_height, &reference_x, &reference_width);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_cell_chunk_new(PagedComposer* composer, PagedTableCellSlice* part, const PagedCellLeaf* leaf,
        size_t first, float width, float containing_height, PagedCellChunk** result) {
    PagedCellChunk* chunk = (PagedCellChunk*)pool_calloc(composer->composition->pool, sizeof(PagedCellChunk));
    if (!chunk) return TYPESET_OUT_OF_MEMORY;
    chunk->leaf = leaf; chunk->first = chunk->next = first;
    TypesetStatus status = paged_cell_frames(composer, part->cell, chunk, width, containing_height);
    size_t items = leaf->flow->kind == PAGED_FLOW_PARAGRAPH ? leaf->flow->paragraph.count : 0;
    if (status == TYPESET_OK && items) {
        chunk->lines = (TypesetLineCandidate*)pool_alloc(composer->composition->pool, items * sizeof(TypesetLineCandidate));
        if (!chunk->lines) status = TYPESET_OUT_OF_MEMORY;
    }
    if (status != TYPESET_OK) { paged_cell_chunk_free(composer->composition->pool, chunk); return status; }
    if (part->last_chunk) part->last_chunk->next_chunk = chunk; else part->chunks = chunk;
    part->last_chunk = chunk; *result = chunk; return TYPESET_OK;
}

static size_t paged_cell_common_path(const PagedCellChunk* left, const PagedCellChunk* right) {
    size_t common = 1;
    if (left) while (common < left->depth && common < right->depth &&
        left->frames[common].flow == right->frames[common].flow) common++;
    return common;
}

static float paged_cell_opening(const PagedCellChunk* chunk, const PagedCellChunk* previous) {
    float height = 0.0f;
    for (size_t i = paged_cell_common_path(previous, chunk); i < chunk->depth; i++) {
        const PagedFrame& frame = chunk->frames[i];
        height += paged_fragment_edge(frame.flow->style, frame.box, 0, chunk->first == frame.flow->cell_begin);
    }
    return height;
}

static float paged_cell_closing(const PagedCellChunk* chunk) {
    float height = 0.0f;
    for (size_t i = 1; i < chunk->depth; i++)
        if (chunk->frames[i].flow->cell_end == chunk->leaf->end) height += chunk->frames[i].box.edges[2];
    return height;
}

static float paged_cell_candidate_tail(const PagedCellChunk* chunk, size_t cursor) {
    float height = 0.0f;
    for (size_t i = 0; i < chunk->depth; i++) {
        const PagedFrame& frame = chunk->frames[i];
        height += paged_fragment_edge(frame.flow->style, frame.box, 2, cursor == frame.flow->cell_end);
    }
    return height;
}

static float paged_table_cell_tail(const PagedTableCellSlice& part, bool complete) {
    float height = paged_fragment_edge(part.cell->style, part.outer.box, 2, complete);
    const PagedCellChunk* chunk = part.last_chunk;
    if (chunk) for (size_t i = 1; i < chunk->depth; i++)
        if (chunk->frames[i].flow->cell_end > part.next)
            height += paged_fragment_edge(chunk->frames[i].flow->style, chunk->frames[i].box, 2, false);
    return height;
}

static TypesetStatus paged_table_split(PagedComposer* composer, PagedFrame* frame, PagedFlowNode* row,
        PagedTableTracks* tracks, uint64_t identity, float available, PagedTableSlice* slice,
        bool* complete, uint32_t* relaxation, float* height) {
    PagedReferenceMeasurement references(composer->composition);
    slice->pool = composer->composition->pool; slice->count = row->cell_count;
    slice->cells = (PagedTableCellSlice*)pool_calloc(slice->pool, slice->count * sizeof(PagedTableCellSlice));
    if (!slice->cells) return TYPESET_OUT_OF_MEMORY;
    PagedTableContinuation* start = row->table_continuations;
    while (start && start->identity != identity) start = start->next;
    if (identity && !start) return TYPESET_STALE;
    if (row->style->height || (row->row_group && (row->row_group->height || row->row_group->min_height || row->row_group->max_height)))
        return paged_failure(composer->composition, TYPESET_INVALID, row->source,
            composer->page->page_number, "split row heights require distributed minimum-height continuations");
    PagedTableTracks* following_tracks = nullptr; float following_height = NAN;
    size_t column = 0;
    for (PagedFlowNode* cell = row->first_child; cell; cell = cell->next, column++) {
        PagedTableCellSlice* part = &slice->cells[column]; part->cell = cell;
        TypesetStatus status = paged_cell_sources(composer, cell);
        if (status != TYPESET_OK) return status;
        part->first = part->next = start ? start->first[column] : 0;
        if (part->next > cell->cell_end) return TYPESET_STALE;
        part->active = part->next < cell->cell_end; part->anchor = -1.0f;
        float width = paged_table_cell_width(cell, tracks);
        part->outer.flow = cell;
        status = paged_frame_measure(composer, &part->outer, 0.0f, width, frame->content_height);
        if (status != TYPESET_OK) return status;
        part->height = paged_fragment_edge(cell->style, part->outer.box, 0, !identity);
        if (!part->active) continue;
        PagedCellChunk* chunk = nullptr;
        status = paged_cell_chunk_new(composer, part, paged_cell_leaf(cell, part->next), part->next, width, frame->content_height, &chunk);
        if (status != TYPESET_OK) return status;
        PagedCellAnchor anchor = paged_table_cell_anchor(cell);
        if (anchor == PAGED_CELL_UNALIGNED) continue;
        if (!identity) {
            float extent = 0.0f; bool forced = false;
            status = paged_table_cell_measure(composer, cell, width, frame->content_height, &extent, &forced, &part->anchor);
        } else if (anchor == PAGED_CELL_BEFORE) {
            part->anchor = part->height;
            if (chunk->depth > 1) part->anchor += paged_fragment_edge(chunk->frames[1].flow->style,
                chunk->frames[1].box, 0, part->next == chunk->frames[1].flow->cell_begin);
        } else if (chunk->leaf->flow->kind == PAGED_FLOW_PARAGRAPH) {
            PagedFlowNode* paragraph = chunk->leaf->flow; const PagedFrame& inner = chunk->frames[chunk->depth - 1];
            for (size_t probe = part->next - chunk->leaf->begin; probe < paragraph->paragraph.count;) {
                TypesetLineCandidate line = {};
                status = paged_next_line(composer, paragraph, probe, inner.content_width, inner.content_height,
                    chunk->lines, paragraph->paragraph.count, &line, inner.replaced_height);
                if (status != TYPESET_OK) break;
                if (line.height > 0.0f) { part->anchor = part->height + paged_cell_opening(chunk, nullptr) + paged_line_baseline(paragraph, line); break; }
                probe = line.next;
            }
        }
        if (status != TYPESET_OK) return status;
        if (!identity && part->anchor < 0.0f) part->anchor = part->height;
        slice->alignment.anchors[anchor] = fmaxf(slice->alignment.anchors[anchor], part->anchor);
    }
    *height = 0.0f; *complete = true;
    for (size_t index = 0; index < slice->count; index++) {
        PagedTableCellSlice* part = &slice->cells[index]; PagedFlowNode* cell = part->cell;
        part->offset = paged_table_cell_offset(cell, 0.0f, 0.0f, slice->alignment, part->anchor); part->height += part->offset;
        PagedCellChunk* previous = nullptr;
        while (part->next < cell->cell_end) {
            PagedCellChunk* chunk = previous ? previous->next_chunk : part->chunks;
            if (!chunk) {
                TypesetStatus status = paged_cell_chunk_new(composer, part, paged_cell_leaf(cell, part->next), part->next,
                    paged_table_cell_width(cell, tracks), frame->content_height, &chunk);
                if (status != TYPESET_OK) return status;
            }
            float before = part->height; part->height += paged_cell_opening(chunk, previous);
            PagedFlowNode* paragraph = chunk->leaf->flow->kind == PAGED_FLOW_PARAGRAPH ? chunk->leaf->flow : nullptr;
            size_t items = paragraph ? paragraph->paragraph.count : 0;
            const PagedFrame& inner = chunk->frames[chunk->depth - 1];
            TypesetLineCandidate* scratch = items ? (TypesetLineCandidate*)pool_alloc(slice->pool, items * sizeof(TypesetLineCandidate)) : nullptr;
            if (items && !scratch) return TYPESET_OUT_OF_MEMORY;
            TypesetStatus status = TYPESET_OK;
            size_t local = chunk->next - chunk->leaf->begin;
            while (local < items) {
                TypesetLineCandidate line = {};
                status = paged_next_line(composer, paragraph, local, inner.content_width, inner.content_height, scratch, items, &line, inner.replaced_height);
                if (status != TYPESET_OK || line.overflow) { if (status == TYPESET_OK) status = TYPESET_UNPLACEABLE; break; }
                size_t next = line.next == items ? chunk->leaf->end : chunk->leaf->begin + line.next;
                if (part->height + line.height + paged_cell_candidate_tail(chunk, next) > available) break;
                chunk->lines[chunk->count++] = line; part->height += line.height; local = line.next; chunk->next = next;
            }
            if (status == TYPESET_OK && local == items) {
                if (part->height + paged_cell_candidate_tail(chunk, chunk->leaf->end) <= available) chunk->next = chunk->leaf->end;
            }
            if (status == TYPESET_OK && chunk->next > chunk->first && chunk->next < chunk->leaf->end && paragraph) {
                if (!following_tracks) {
                    float width = 0.0f, height = 0.0f;
                    status = paged_next_constraints(composer, &width, &height, &following_height);
                    if (status == TYPESET_OK) status = paged_table_tracks(composer, frame->flow, width, &following_tracks);
                }
                PagedCellChunk following = {}; following.leaf = chunk->leaf;
                if (status == TYPESET_OK) status = paged_cell_frames(composer, cell, &following,
                    paged_table_cell_width(cell, following_tracks), following_height);
                size_t remaining = 0; float tail = 0.0f;
                while (status == TYPESET_OK) {
                    const PagedFrame& next_inner = following.frames[following.depth - 1];
                    status = paged_measure_lines(composer, paragraph, chunk->next - chunk->leaf->begin, next_inner.content_width,
                        next_inner.content_height, scratch, nullptr, items, &remaining, &tail, false, SIZE_MAX, paragraph->style->widows);
                    if (status != TYPESET_OK || remaining >= paragraph->style->widows || chunk->count <= paragraph->style->orphans) break;
                    const TypesetLineCandidate& removed = chunk->lines[--chunk->count];
                    chunk->next = chunk->leaf->begin + removed.first; part->height -= removed.height;
                }
                if (following.frames) pool_free(slice->pool, following.frames);
                if (chunk->count < paragraph->style->orphans || remaining < paragraph->style->widows) {
                    if (part->next > part->first) {
                        // an earlier sibling boundary preserves minima without publishing this block's opening edge.
                        chunk->next = chunk->first; chunk->count = 0;
                    } else *relaxation |= PAGED_RELAX_MINIMA;
                }
            }
            if (scratch) pool_free(slice->pool, scratch);
            if (status != TYPESET_OK) return status;
            if (chunk->next == chunk->first) {
                part->height = before;
                if (previous) previous->next_chunk = nullptr; else part->chunks = nullptr;
                part->last_chunk = previous; paged_cell_chunk_free(slice->pool, chunk);
                if (part->next == part->first) return TYPESET_UNPLACEABLE;
                break;
            }
            part->next = chunk->next;
            if (part->next == chunk->leaf->end) part->height += paged_cell_closing(chunk);
            else break;
            previous = chunk;
        }
        if (part->next < cell->cell_end) *complete = false;
        *height = fmaxf(*height, part->height + paged_table_cell_tail(*part, false));
    }
    if (*complete) for (size_t i = 0; i < slice->count; i++)
        *height = fmaxf(*height, slice->cells[i].height + paged_table_cell_tail(slice->cells[i], true));
    if (*height <= 0.0f || *height > available) return TYPESET_UNPLACEABLE;
    if (!*complete && paged_break_avoids(composer, row->style->break_inside)) *relaxation |= PAGED_RELAX_AVOIDANCE;
    if (!*complete) for (size_t i = 0; i < slice->count; i++)
        for (PagedCellChunk* chunk = slice->cells[i].chunks; chunk; chunk = chunk->next_chunk)
            for (size_t j = 0; j < chunk->depth; j++)
                if (paged_break_avoids(composer, chunk->frames[j].flow->style->break_inside)) *relaxation |= PAGED_RELAX_AVOIDANCE;
    return TYPESET_OK;
}

static TypesetStatus paged_table_continuation(PagedComposer* composer, PagedFlowNode* row,
        const PagedTableSlice& slice, uint64_t* identity) {
    for (PagedTableContinuation* saved = row->table_continuations; saved; saved = saved->next) {
        size_t i = 0;
        while (i < slice.count && saved->first[i] == slice.cells[i].next) i++;
        if (i == slice.count) { *identity = saved->identity; return TYPESET_OK; }
    }
    if (composer->composition->table_continuations >= composer->composition->options.max_items)
        return TYPESET_BUDGET_EXHAUSTED;
    Pool* pool = composer->composition->pool;
    PagedTableContinuation* saved = (PagedTableContinuation*)pool_calloc(pool, sizeof(PagedTableContinuation));
    size_t* first = (size_t*)pool_alloc(pool, slice.count * sizeof(size_t));
    if (!saved || !first) return TYPESET_OUT_OF_MEMORY;
    for (size_t i = 0; i < slice.count; i++) first[i] = slice.cells[i].next;
    // intern source cursors so rejected page trials replay the same stable continuation ID.
    saved->identity = ++composer->composition->table_continuations; saved->first = first;
    saved->next = row->table_continuations; row->table_continuations = saved; *identity = saved->identity;
    return TYPESET_OK;
}

static TypesetStatus paged_table_slice_emit(PagedComposer* composer, PagedFrame* frame,
        PagedFlowNode* row, PagedTableTracks* tracks, const PagedTableSlice& slice,
        float height, bool first, bool complete) {
    LayoutViewNode* box = paged_table_row_box(composer, frame, row, height, VIEW_FRAGMENT_BODY);
    if (!box) return TYPESET_OUT_OF_MEMORY;
    box->first_fragment = first; box->last_fragment = complete;
    TypesetStatus status = paged_mark_enter(composer, row->source);
    for (size_t i = 0; status == TYPESET_OK && i < slice.count; i++) {
        const PagedTableCellSlice& part = slice.cells[i]; const PagedFrame& outer = part.outer;
        status = paged_reference_capture(composer->composition, part.cell->style, outer.reference_width, composer->page->page_number);
        if (status != TYPESET_OK) return status;
        float x = paged_table_column_x(frame, part.cell->column, tracks);
        LayoutViewNode* cell = view_tree_fragment_append(composer->tree, box, part.cell->source, {x, composer->y, outer.width, height});
        if (!cell) return TYPESET_OUT_OF_MEMORY;
        cell->computed_style = lam::up(part.cell->style); cell->paint_box = true; cell->generated = !part.cell->source;
        cell->first_fragment = first; cell->last_fragment = complete;
        status = paged_boundary_publish(composer->tree, cell, outer.box, first, complete);
        if (status == TYPESET_OK && (!part.cell->source || frame->flow->column_sources))
            status = paged_table_range_publish(composer->tree, cell, part.cell);
        PagedComposer nested = {}; nested.tree = composer->tree; nested.composition = composer->composition;
        nested.page = composer->page; nested.page_style = composer->page_style;
        nested.initial_containing_block = composer->initial_containing_block;
        nested.role = VIEW_FRAGMENT_BODY; nested.atomic_fragment = true;
        float natural_height = part.height + paged_table_cell_tail(part, complete);
        nested.y = composer->y + paged_table_cell_offset(part.cell, height, natural_height, slice.alignment, part.anchor);
        nested.y += paged_fragment_edge(part.cell->style, outer.box, 0, first);
        status = paged_mark_enter(&nested, part.cell->source);
        LayoutViewNode** nodes = (LayoutViewNode**)pool_calloc(slice.pool,
            composer->composition->options.max_depth * sizeof(LayoutViewNode*));
        if (!nodes) return TYPESET_OUT_OF_MEMORY;
        nodes[0] = cell; size_t open = 1;
        const PagedCellChunk* previous = nullptr;
        for (const PagedCellChunk* chunk = part.chunks; status == TYPESET_OK && chunk; chunk = chunk->next_chunk) {
            for (size_t j = open; status == TYPESET_OK && j < chunk->depth; j++) {
                const PagedFrame& measured = chunk->frames[j];
                status = paged_reference_capture(composer->composition, measured.flow->style,
                    measured.reference_width, composer->page->page_number);
                if (status != TYPESET_OK) break;
                bool beginning = chunk->first == measured.flow->cell_begin;
                bool ending = part.next >= measured.flow->cell_end;
                LayoutViewNode* block = view_tree_fragment_append(composer->tree, nodes[j - 1], measured.flow->source,
                    {x + measured.x, nested.y, measured.width, 0.0f});
                if (!block) { status = TYPESET_OUT_OF_MEMORY; break; }
                nodes[j] = block; block->computed_style = lam::up(measured.flow->style); block->paint_box = true;
                block->first_fragment = beginning; block->last_fragment = ending;
                status = paged_boundary_publish(composer->tree, block, measured.box, beginning, ending);
                if (status == TYPESET_OK) status = paged_mark_enter(&nested, measured.flow->source);
                nested.y += paged_fragment_edge(measured.flow->style, measured.box, 0, beginning);
            }
            open = chunk->depth;
            PagedFrame containing = chunk->frames[chunk->depth - 1]; containing.fragment = nodes[chunk->depth - 1];
            containing.content_x += x; nested.frames = &containing; nested.depth = 1;
            PagedFlowNode* paragraph = chunk->leaf->flow;
            for (size_t j = 0; status == TYPESET_OK && j < chunk->count; j++)
                status = paged_commit_line(&nested, paragraph, chunk->lines[j]);
            if (chunk->next == chunk->leaf->end) {
                while (open > 1 && chunk->frames[open - 1].flow->cell_end <= chunk->next) {
                    const PagedFrame& closing = chunk->frames[--open]; nested.y += closing.box.edges[2];
                    nodes[open]->rect.height = nested.y - nodes[open]->rect.y;
                }
            }
            previous = chunk;
        }
        while (status == TYPESET_OK && previous && open > 1) {
            const PagedFrame& closing = previous->frames[--open];
            nested.y += paged_fragment_edge(closing.flow->style, closing.box, 2, false);
            nodes[open]->rect.height = nested.y - nodes[open]->rect.y;
        }
        pool_free(slice.pool, nodes);
    }
    if (status == TYPESET_OK) composer->y += height;
    return status;
}

static TypesetStatus paged_table_furniture(PagedComposer* composer, PagedFrame* frame, bool footer) {
    if (!paged_is_table_grid(frame->flow) || (footer && !frame->table_started)) return TYPESET_OK;
    PagedTableTracks* tracks = nullptr;
    TypesetStatus status = paged_table_tracks(composer, frame->flow, frame->content_width, &tracks);
    if (status == TYPESET_OK && !footer) status = paged_table_columns_visit(frame->flow, [&](PagedFlowNode* column) {
        return paged_mark_enter(composer, column->source);
    });
    if (status != TYPESET_OK) return status;
    for (PagedFlowNode* row = frame->flow->first_child; status == TYPESET_OK && row; row = row->next) {
        if (row->table_group != (footer ? PAGED_TABLE_FOOTER : PAGED_TABLE_HEADER)) continue;
        if (footer ? paged_table_omits(frame->flow, true) && !frame->table_terminal : !paged_table_header_visible(frame)) continue;
        float height = 0.0f; bool forced = false; PagedTableAlignment alignment = {};
        status = paged_table_row_height(composer, row, tracks, frame->content_height, &height, &forced, &alignment);
        ViewNodeState* state = view_tree_node_state(composer->tree, row->source, false);
        ViewFragmentRole role = state && state->occurrence_count ? VIEW_FRAGMENT_REPEATED_TABLE : VIEW_FRAGMENT_BODY;
        if (status == TYPESET_OK) {
            composer->y += frame->flow->table_spacing_v;
            status = paged_table_row_emit(composer, frame, row, tracks, height, alignment, role);
        }
    }
    if (footer && status == TYPESET_OK) {
        composer->y += frame->flow->table_spacing_v;
        frame->table_started = false; frame->table_footer_height = 0.0f;
    }
    return status;
}

static TypesetStatus paged_table_row(PagedComposer* composer, PagedFlowNode* row, uint64_t continuation,
        uint64_t* following, bool* complete, uint32_t* relaxation, float* extent) {
    PagedFrame* frame = &composer->frames[composer->depth - 1];
    PagedTableTracks* tracks = nullptr;
    TypesetStatus status = paged_table_tracks(composer, frame->flow, frame->content_width, &tracks);
    float height = 0.0f, headers = 0.0f, gap = frame->flow->table_spacing_v; bool forced = false;
    if (status == TYPESET_OK) status = paged_table_row_height(composer, row, tracks, frame->content_height, &height, &forced);
    PagedFlowNode* last = row->span_last ? row->span_last : row;
    for (PagedFlowNode* following_row = row->next; status == TYPESET_OK && row != last && following_row; following_row = following_row->next) {
        float size = 0.0f;
        status = paged_table_row_height(composer, following_row, tracks, frame->content_height, &size, &forced);
        height += gap + size;
        if (following_row == last) break;
    }
    for (PagedFlowNode* header = frame->flow->first_child; status == TYPESET_OK && !frame->table_started && header; header = header->next) {
        if (header->table_group != PAGED_TABLE_HEADER || !paged_table_header_visible(frame)) continue;
        float size = 0.0f;
        status = paged_table_row_height(composer, header, tracks, frame->content_height, &size, &forced);
        headers += gap + size;
    }
    if (status != TYPESET_OK) return status;
    if (forced) return paged_failure(composer->composition, TYPESET_INVALID, row->source, composer->page->page_number,
        "forced breaks inside table cells require cell continuations");
    float available = composer->bottom - composer->y - headers - gap - paged_tail_edges(composer, false) - paged_region_reserved(composer);
    bool terminal_row = !paged_table_next_body_row(row);
    float terminal_footer = terminal_row && paged_table_omits(frame->flow, true)
        ? frame->table_footer_height - gap : 0.0f;
    PagedTableSlice slice = {}; *complete = true;
    bool split = continuation || height + terminal_footer > available;
    if (split && row->span_last) return TYPESET_UNPLACEABLE;
    if (split) {
        float fresh = composer->page_style.content_rect.height - headers - gap - paged_tail_edges(composer, false);
        if (!continuation && height + terminal_footer <= fresh && composer->page_has_content) return TYPESET_UNPLACEABLE;
        status = paged_table_split(composer, frame, row, tracks, continuation, available, &slice, complete, relaxation, &height);
        if (status != TYPESET_OK) return status;
        if (*complete && height + terminal_footer > available) {
            // keep a legal real tail when completing the row would require a footer that cannot fit.
            float prefix = nextafterf(height, -INFINITY);
            slice.clear(); *relaxation = 0;
            status = paged_table_split(composer, frame, row, tracks, continuation, prefix, &slice, complete, relaxation, &height);
            if (status != TYPESET_OK) return status;
            if (*complete) return TYPESET_NO_PROGRESS;
        }
        if (!*complete) {
            status = paged_table_continuation(composer, row, slice, following);
            if (status != TYPESET_OK) return status;
        }
    }
    if (!frame->table_started) status = paged_table_furniture(composer, frame, false);
    if (status == TYPESET_OK) composer->y += gap;
    if (status == TYPESET_OK && split) status = paged_table_slice_emit(composer, frame, row, tracks, slice,
        height, !continuation, *complete);
    else for (PagedFlowNode* emitted = row; status == TYPESET_OK && emitted; emitted = emitted->next) {
        float size = 0.0f; PagedTableAlignment alignment = {};
        status = paged_table_row_height(composer, emitted, tracks, frame->content_height, &size, &forced, &alignment);
        if (status == TYPESET_OK) status = paged_table_row_emit(composer, frame, emitted, tracks, size, alignment, VIEW_FRAGMENT_BODY);
        if (emitted == last) break;
        composer->y += gap;
    }
    if (status == TYPESET_OK) {
        frame->table_started = true; frame->table_terminal = terminal_row && *complete;
        composer->page_has_content = true; *extent = headers + gap + height;
        ViewBreak after = paged_flow_break(row, false);
        if (*complete && after >= VIEW_BREAK_COLUMN) {
            composer->pending_break = true; composer->requested_break = after;
        }
    }
    return status;
}

static TypesetStatus paged_block_trial_begin(PagedComposer* composer, PagedFlowNode* flow) {
    // trial work is monotonic across rollback, including replay under enclosing checkpoints.
    if (composer->composition->block_trials >= composer->composition->options.max_block_trials)
        return paged_failure(composer->composition, TYPESET_BUDGET_EXHAUSTED, flow->source,
            composer->page ? composer->page->page_number : 0, "block composition exhausted its trial budget");
    composer->composition->block_trials++;
    if (composer->depth >= composer->composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    return TYPESET_OK;
}

static TypesetStatus paged_block_enter(PagedComposer* composer, PagedFlowNode* flow) {
    float parent_width = composer->depth ? composer->frames[composer->depth - 1].content_width : composer->page_style.content_rect.width;
    float top = paged_flow_margin(composer, flow, 0, parent_width);
    if (top || composer->pending_margin) {
        TypesetStatus status = paged_space_flush(composer);
        if (status != TYPESET_OK) return status;
    }
    composer->y += layout_collapse_margins(composer->pending_margin, top);
    composer->pending_margin = 0.0f;
    if (flow->style->flow_traits) {
        TypesetStatus status = paged_space_append(composer, flow->style->flow_traits->before);
        if (status != TYPESET_OK) return status;
    }
    if (!lam::pool_grow_array(composer->composition->pool, &composer->frames, &composer->frame_capacity,
                              composer->depth + 1, 16)) return TYPESET_OUT_OF_MEMORY;
    size_t index = composer->depth++;
    composer->frames[index] = {}; composer->frames[index].flow = flow;
    return paged_frame_open(composer, index, true);
}

static float paged_block_remaining(PagedComposer* composer) {
    PagedFrame* frame = &composer->frames[composer->depth - 1];
    float top_edge = paged_fragment_edge(frame->flow->style, frame->box, 0, frame->fragment->first_fragment);
    float consumed = frame->consumed_content + composer->y - frame->page_start - top_edge;
    return fmaxf(0.0f, frame->content_height - consumed);
}

static TypesetStatus paged_block_finish(PagedComposer* composer, float parent_width) {
    PagedFrame* frame = &composer->frames[composer->depth - 1];
    PagedFlowNode* flow = frame->flow;
    ViewCssStyle* style = flow->style;
    if (frame->box.edges[2] != 0.0f) {
        TypesetStatus status = paged_space_flush(composer);
        if (status != TYPESET_OK) return status;
    }
    TypesetStatus status = paged_table_furniture(composer, frame, true);
    if (status != TYPESET_OK) return status;
    if (flow->kind == PAGED_FLOW_TABLE_WRAPPER) {
        // a wrapper establishes a BFC, so the last caption's margin cannot escape through its bottom.
        composer->y += composer->pending_margin; composer->pending_margin = 0.0f;
    }
    // cell replay belongs to an admitted row; only the outer row participates in joint region scheduling.
    if (composer->role == VIEW_FRAGMENT_BODY && !composer->atomic_fragment && composer->tree->model->environment.presentation == VIEW_PRESENTATION_PAGED) {
        // closing a sibling can expose ancestor edges that no paragraph candidate could reserve.
        float body_height = paged_body_height(composer) + paged_tail_edges(composer, true);
        PagedRegionTrial trial = {};
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) trial.anchor_counts[i] = composer->regions[i].anchor_count;
        // closing trials use the same native shrink/conditional-space constraints as page candidates.
        body_height = paged_vertical_measure(composer, body_height, true).minimum;
        status = body_height > composer->page_style.content_rect.height ? TYPESET_UNPLACEABLE :
            paged_regions_trial(composer, body_height, &trial);
        if (status == TYPESET_OK) status = paged_regions_accept(composer, &trial);
        else paged_region_trial_dispose(&trial);
        if (status != TYPESET_OK) {
            composer->closure_failure = status == TYPESET_UNPLACEABLE;
            return paged_failure(composer->composition, status, flow->source,
                composer->page->page_number, "block closing decorations cannot fit with the page regions");
        }
    }
    if (composer->role == VIEW_FRAGMENT_BODY) status = paged_mark_enter(composer, flow->source);
    if (status != TYPESET_OK) return status;
    composer->y += frame->box.edges[2];
    status = paged_frame_finish(composer, frame, true);
    if (status != TYPESET_OK) return status;
    composer->depth--;
    if (composer->space_count) composer->space_depth = composer->space_depth < composer->depth ? composer->space_depth : composer->depth;
    composer->pending_margin = paged_flow_margin(composer, flow, 2, parent_width);
    if (style->flow_traits) {
        status = paged_space_append(composer, style->flow_traits->after);
        if (status != TYPESET_OK) return status;
    }
    if (composer->role == VIEW_FRAGMENT_BODY && !paged_is_wrapped_grid(flow) && style->break_after >= VIEW_BREAK_COLUMN) {
        composer->pending_break = true; composer->requested_break = style->break_after;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_block_before(PagedComposer* composer, PagedFlowNode* flow, bool* forced) {
    if (forced) *forced = false;
    ViewCssStyle* style = flow->style;
    bool has_content = composer->page && (composer->page_has_content || composer->sheet_has_content);
    const RadiantPageSequence* sequence = radiant_page_sequence_for(composer->tree->model->css->page_document, flow->source);
    bool changed_sequence = sequence && sequence != composer->sequence;
    if (changed_sequence) composer->sequence = sequence;
    const char* name = nullptr;
    for (ViewCssStyle* owner = style; owner; owner = owner->parent) {
        if (owner->page_name) { name = owner->page_name; break; }
    }
    bool changed_name = (name == nullptr) != (composer->page_name == nullptr) ||
        (name && composer->page_name && strcmp(name, composer->page_name) != 0);
    if (changed_name) composer->page_name = name;
    ViewBreak before = paged_flow_break(flow, true);
    if (before >= VIEW_BREAK_COLUMN) {
        composer->pending_break = true; composer->requested_break = before;
    }
    if (!composer->page && composer->tree->model->environment.presentation == VIEW_PRESENTATION_PAGED) {
        TypesetStatus status = paged_next_page(composer);
        if (status != TYPESET_OK) return status;
    } else if ((composer->pending_break || changed_name || changed_sequence) && has_content) {
        if ((changed_name || changed_sequence) && composer->requested_break < VIEW_BREAK_PAGE)
            composer->requested_break = VIEW_BREAK_PAGE;
        if (forced) { *forced = true; return TYPESET_OK; }
        TypesetStatus status = paged_next_page(composer);
        if (status != TYPESET_OK) return status;
    } else if (composer->page && !has_content && (composer->pending_break || changed_name || changed_sequence)) {
        TypesetStatus status = changed_name || changed_sequence ? paged_empty_page_restyle(composer, false, true) : TYPESET_OK;
        if (status != TYPESET_OK) return status;
        ViewPageSide wanted = VIEW_PAGE_RIGHT;
        if (paged_requested_side(composer, composer->requested_break, &wanted) && composer->page->side != wanted) {
            status = paged_empty_page_restyle(composer, true);
            if (status == TYPESET_OK) status = paged_next_page(composer);
            if (status != TYPESET_OK) return status;
        }
        composer->pending_break = false; composer->requested_break = VIEW_BREAK_AUTO;
    }
    return TYPESET_OK;
}

struct PagedFlowStep { PagedFlowStepKind kind; PagedFlowNode* flow; };
struct PagedCandidateInfo {
    RadiantKeepStrength keep, page_keep;
    float space_capacity;
    const RadiantPageSequence* sequence;
    bool pending_aux;
    TypesetPacking native_packing;
    size_t native_end;
    const TypesetRegionSelection* auxiliary;
    size_t auxiliary_count;
};

struct PagedPageProvider {
    PagedComposer* composer;
    PagedFlowStep* steps = nullptr;
    size_t count = 0, capacity = 0;
    TypesetLineCandidate* scratch = nullptr;
    TypesetLineCandidate* lines = nullptr;
    size_t line_capacity = 0, lines_capacity = 0;
    TypesetResume start = {}, active = {};
    PagedCheckpoint checkpoint;
    TypesetStatus blocked = TYPESET_OK;
    DomElement* failed_source = nullptr;
    const char* failed_reason = nullptr;
    PagedLayoutDiagnostic hard_failure = {};
    bool relax_minima = false, relax_avoidance = false;
    RadiantKeepStrength boundary_keep = {}, page_keep = {};
    float space_ratio = 0.0f;
    Pool* candidate_pool = nullptr;
    bool reject_terminal = false;
    bool measure_only = false;
    TypesetItem* vertical_glues = nullptr;
    size_t vertical_count = 0, vertical_capacity = 0, vertical_tail = 0, vertical_end = SIZE_MAX;
    TypesetPacking vertical_packing = {};

    ~PagedPageProvider() {
        if (composer->provider == this) composer->provider = nullptr;
        Pool* pool = composer->composition->pool;
        if (steps) pool_free(pool, steps);
        if (scratch) pool_free(pool, scratch);
        if (lines) pool_free(pool, lines);
        if (vertical_glues) pool_free(pool, vertical_glues);
    }

    TypesetStatus append(PagedFlowStepKind kind, PagedFlowNode* flow) {
        if (!lam::pool_grow_array(composer->composition->pool, &steps, &capacity, count + 1, 32))
            return TYPESET_OUT_OF_MEMORY;
        steps[count++] = {kind, flow};
        return TYPESET_OK;
    }

    TypesetStatus flatten(PagedFlowNode* flow) {
        auto visit = [&](PagedFlowStepKind kind, PagedFlowNode* source) {
            if (source->kind == PAGED_FLOW_TABLE_ROW && (source->table_group != PAGED_TABLE_BODY ||
                (source->span_first && source->span_first != source))) return TYPESET_OK;
            return append(kind, source);
        };
        return paged_flow_walk(flow, 0, composer->composition->options.max_depth, visit);
    }

    TypesetStatus line_storage(PagedFlowNode* flow) {
        size_t needed = flow->paragraph.count;
        Pool* pool = composer->composition->pool;
        return lam::pool_grow_array(pool, &scratch, &line_capacity, needed, 16) &&
            lam::pool_grow_array(pool, &lines, &lines_capacity, needed, 16) ? TYPESET_OK : TYPESET_OUT_OF_MEMORY;
    }

    size_t following_material(size_t index) const {
        while (index < count) {
            if (steps[index].kind == PAGED_STEP_CLOSE) { index++; continue; }
            if (steps[index].kind == PAGED_STEP_LINE) {
                const TypesetParagraph* paragraph = &steps[index].flow->paragraph;
                TypesetLineCandidate line = {};
                // reuse the line producer's trimming; collapsed whitespace still replays its source records.
                bool collapsed = !paragraph->count || (typeset_line_alternatives(paragraph, 0, 0.0f, &line, 1, nullptr) == 1 &&
                    line.next == paragraph->count && line.paint_first == line.paint_end && line.height == 0.0f);
                for (size_t i = 0; collapsed && i < paragraph->count; i++) {
                    const PagedSourceRecord* source = (const PagedSourceRecord*)paragraph->items[i].source.native.get();
                    // zero-height insertion/mark/target anchors still belong after a forced boundary.
                    if (!source || source->insertion || !source->source->is_text()) collapsed = false;
                }
                if (collapsed) { index++; continue; }
            }
            break;
        }
        return index;
    }

    const RadiantPageSequence* source_sequence(DomElement* source) const {
        for (; source; source = source->parent_element()) {
            const RadiantPageSequence* sequence = radiant_page_sequence_for(composer->tree->model->css->page_document, source);
            if (sequence) return sequence;
        }
        return nullptr;
    }

    bool finishes_sequence(const TypesetResume& end, const RadiantPageSequence* sequence) const {
        if (!sequence) return false;
        size_t index = following_material(end.state[0]);
        if (index == count) return true;
        const RadiantPageSequence* following = source_sequence(steps[index].flow->source);
        return following && following != sequence;
    }

    bool finishes_page(const TypesetPageCandidate& candidate, const RadiantPageSequence* sequence) const {
        const PagedCandidateInfo* info = (const PagedCandidateInfo*)candidate.trial;
        return finishes_sequence(candidate.end, sequence) && (!info || !info->pending_aux);
    }
};

static PagedVerticalMeasure paged_vertical_measure(PagedComposer* composer, float body, bool end) {
    PagedPageProvider* provider = composer->provider;
    if (!provider || !provider->vertical_count) return {body, body, 0};
    // selected replay has already applied packing and conditional trimming to its geometry.
    if (provider->vertical_end != SIZE_MAX) return {body, body, provider->vertical_count};
    size_t glue_end = provider->vertical_count;
    if (end) while (glue_end > provider->vertical_tail && provider->vertical_glues[glue_end - 1].glue.discard_end)
        body -= provider->vertical_glues[--glue_end].glue.natural;
    TypesetPacking minimum = typeset_pack_glue(provider->vertical_glues, 0, glue_end, body, 0.0f);
    return {body, glue_end ? fmaxf(0.0f, -minimum.residual) : body, glue_end};
}

static RadiantKeepStrength paged_context_keep(PagedComposer* composer, const RadiantKeepSpec& spec, bool physical) {
    // an interior column boundary does not cross the physical page's keep domain.
    return !physical && paged_has_next_column(composer) ? spec.scope[1] : radiant_keep_max(spec.scope[1], spec.scope[2]);
}

static RadiantKeepStrength paged_boundary_keep(PagedComposer* composer, PagedFlowNode* flow, bool complete, bool physical) {
    RadiantKeepStrength result = {};
    for (size_t i = 0; i < composer->depth; i++) {
        PagedFlowNode* owner = composer->frames[i].flow;
        bool finishes = false;
        if (complete) for (PagedFlowNode* end = flow; end; end = end->parent) {
            if (end == owner) { finishes = true; break; }
            if (end->next) break;
        }
        if (!finishes && owner->style->flow_traits)
            result = radiant_keep_max(result, paged_context_keep(composer, owner->style->flow_traits->together, physical));
    }
    if (!complete) return result;
    for (PagedFlowNode* end = flow; end; end = end->parent) {
        PagedFlowNode* next = end->kind == PAGED_FLOW_TABLE_ROW ? paged_table_next_body_row(end) : end->next;
        if (!next) continue;
        if (end->style->flow_traits) result = radiant_keep_max(result, paged_context_keep(composer, end->style->flow_traits->next, physical));
        if (next->style->flow_traits) result = radiant_keep_max(result, paged_context_keep(composer, next->style->flow_traits->previous, physical));
        break;
    }
    return result;
}

static TypesetPageCandidate paged_sheet_candidate(const TypesetPageCandidate* columns,
        const TypesetFragmentainerCandidate* regions, size_t count, uint32_t physical_columns) {
    TypesetPageCandidate result = columns[count - 1];
    result.start = columns[0].start;
    result.body_height = result.note_height = result.float_height = result.available_height = 0.0f; result.cost = 0.0;
    for (size_t i = 0; i < count; i++) {
        result.body_height += columns[i].body_height; result.note_height += columns[i].note_height;
        result.float_height += columns[i].float_height;
        result.cost += columns[i].cost;
        if (columns[i].kind == TYPESET_PAGE_FLOW) result.kind = TYPESET_PAGE_FLOW;
    }
    for (size_t i = 0; i < physical_columns; i++) result.available_height += regions[i].rect.height;
    // an interior forced column is a constraint on column filling, not a mandatory sheet eject.
    if (result.boundary.legality == TYPESET_BREAK_FORCED && result.boundary.scope == TYPESET_BREAK_COLUMN && count < physical_columns)
        result.boundary.legality = TYPESET_BREAK_ALLOWED;
    result.boundary.scope = TYPESET_BREAK_PAGE;
    result.fragmentainers = regions; result.fragmentainer_count = physical_columns;
    return result;
}

static TypesetFragmentainerCandidate paged_sheet_region(PagedComposer* composer, const TypesetPageCandidate& candidate) {
    const auto* info = (const PagedCandidateInfo*)candidate.trial;
    return {composer->page_style.content_rect, candidate.start, candidate.end,
        candidate.body_height, candidate.note_height, candidate.float_height, candidate.boundary,
        info ? info->auxiliary : nullptr, info ? info->auxiliary_count : 0};
}

static TypesetStatus paged_sheet_auxiliary_select(PagedComposer* composer, const PagedRegionTrial* region_trial,
        const TypesetRegionSelection** selections, size_t* count) {
    PagedSheetPolicyTrial* trial = composer->sheet_trial;
    *selections = nullptr; *count = 0;
    TypesetRegionSelection* values = nullptr;
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
        const TypesetRegionPlan& plan = region_trial ? region_trial->plans[i] : composer->regions[i].plan;
        if (!plan.count) continue;
        size_t limit = composer->composition->options.max_items;
        if (trial->leased_items >= limit || plan.count > limit - trial->leased_items - 1) return TYPESET_BUDGET_EXHAUSTED;
        if (!values) values = (TypesetRegionSelection*)pool_calloc(trial->pool, PAGED_REGION_COUNT * sizeof(*values));
        auto* lease = (PagedSheetRegionLease*)pool_calloc(trial->pool, sizeof(PagedSheetRegionLease));
        if (!values || !lease) return TYPESET_OUT_OF_MEMORY;
        TypesetStatus status = typeset_region_plan_retain(&plan, &lease->plan);
        if (status != TYPESET_OK) return status;
        // held physical inputs retain measured slices after column queues commit and release their plans.
        lease->next = trial->leases; trial->leases = lease; trial->leased_items += plan.count + 1;
        values[(*count)++] = paged_region_selection(i, lease->plan);
    }
    *selections = values;
    return TYPESET_OK;
}

static TypesetStatus paged_sheet_empty_regions(PagedComposer* composer, TypesetFragmentainerCandidate* regions,
        size_t used, const TypesetResume& end) {
    for (size_t i = used; i < composer->page_style.column_count; i++) {
        regions[i] = {}; regions[i].start = regions[i].end = end;
        if (!view_css_page_column(composer->page->style, static_cast<uint32_t>(i), &regions[i].rect)) return TYPESET_INVALID;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_sheet_policy_collect(PagedComposer* composer, const TypesetPageCandidate* candidates, size_t count) {
    PagedSheetPolicyTrial* trial = composer->sheet_trial;
    size_t columns = trial->prefix_count + 1, physical_columns = composer->page_style.column_count;
    size_t limit = composer->composition->options.max_items;
    if (count > limit - trial->count || columns > limit || columns > SIZE_MAX / sizeof(TypesetPageCandidate) ||
        physical_columns > SIZE_MAX / sizeof(TypesetFragmentainerCandidate) || count > (limit - trial->region_count) / physical_columns)
        return paged_failure(composer->composition, TYPESET_BUDGET_EXHAUSTED, nullptr,
            composer->page->page_number, "physical sheet candidates exceed the common item budget");
    if (!lam::pool_grow_array(trial->pool, &trial->candidates, &trial->capacity, trial->count + count, 16) ||
        !lam::pool_grow_array(trial->pool, &trial->decisions, &trial->decision_capacity, trial->count + count, 16))
        return TYPESET_OUT_OF_MEMORY;
    for (size_t i = 0; i < count; i++) {
        const auto* info = (const PagedCandidateInfo*)candidates[i].trial;
        // a legal column break can still be forbidden as a physical eject by a page-scoped keep.
        if (info && info->page_keep.kind == RADIANT_KEEP_ALWAYS &&
            !(candidates[i].boundary.legality == TYPESET_BREAK_FORCED && candidates[i].boundary.scope == TYPESET_BREAK_PAGE)) continue;
        auto* values = (TypesetPageCandidate*)pool_alloc(trial->pool, columns * sizeof(TypesetPageCandidate));
        auto* regions = (TypesetFragmentainerCandidate*)pool_alloc(trial->pool, physical_columns * sizeof(TypesetFragmentainerCandidate));
        if (!values || !regions) return TYPESET_OUT_OF_MEMORY;
        if (trial->prefix_count) {
            memcpy(values, trial->prefix, trial->prefix_count * sizeof(*values));
            memcpy(regions, trial->regions, trial->prefix_count * sizeof(*regions));
        }
        values[columns - 1] = candidates[i]; regions[columns - 1] = paged_sheet_region(composer, candidates[i]);
        TypesetStatus status = paged_sheet_empty_regions(composer, regions, columns, candidates[i].end);
        if (status != TYPESET_OK) return status;
        trial->decisions[trial->count] = {values, columns};
        trial->candidates[trial->count++] = paged_sheet_candidate(values, regions, columns, composer->page_style.column_count);
        trial->region_count += physical_columns;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_sheet_policy_prefix(PagedComposer* composer, const TypesetPagePlan& plan) {
    PagedSheetPolicyTrial* trial = composer->sheet_trial;
    size_t count = trial->prefix_count + 1;
    if (!lam::pool_grow_array(trial->pool, &trial->prefix, &trial->prefix_capacity, count, 4) ||
        !lam::pool_grow_array(trial->pool, &trial->regions, &trial->region_capacity, composer->page_style.column_count, 4)) return TYPESET_OUT_OF_MEMORY;
    trial->prefix[trial->prefix_count] = plan.candidate;
    trial->regions[trial->prefix_count++] = paged_sheet_region(composer, plan.candidate);
    TypesetStatus status = paged_sheet_empty_regions(composer, trial->regions, count, plan.candidate.end);
    if (status != TYPESET_OK) return status;
    if (trial->replay) {
        if (plan.count > composer->composition->options.max_items - trial->contribution_count) return TYPESET_BUDGET_EXHAUSTED;
        if (!lam::pool_grow_array(trial->pool, &trial->contributions, &trial->contribution_capacity,
            trial->contribution_count + plan.count, 16)) return TYPESET_OUT_OF_MEMORY;
        memcpy(trial->contributions + trial->contribution_count, plan.contributions, plan.count * sizeof(TypesetContribution));
        trial->contribution_count += plan.count;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_sheet_empty_column(PagedComposer* composer, const TypesetResume& cursor) {
    if (!composer->sheet_trial) return TYPESET_OK;
    TypesetPagePlan empty = {};
    empty.candidate.start = empty.candidate.end = cursor;
    empty.candidate.kind = TYPESET_PAGE_EMPTY; empty.candidate.page_number = composer->page->page_number;
    empty.candidate.available_height = composer->page_style.content_rect.height;
    empty.candidate.boundary = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_COLUMN, 0, 0};
    return paged_sheet_policy_prefix(composer, empty);
}

static size_t paged_choose_default(const TypesetPageCandidate* candidates, size_t count) {
    size_t selected = 0; RadiantKeepStrength weakest = {};
    if (candidates[0].trial) weakest = ((const PagedCandidateInfo*)candidates[0].trial)->keep;
    for (size_t i = 0; i < count; i++) {
        if (candidates[i].boundary.legality == TYPESET_BREAK_FORCED) return i;
        RadiantKeepStrength keep = candidates[i].trial ? ((const PagedCandidateInfo*)candidates[i].trial)->keep : RadiantKeepStrength{};
        if (radiant_keep_compare(keep, weakest) <= 0) { selected = i; weakest = keep; }
    }
    return selected;
}

static size_t paged_choose_page(void* context, const TypesetPageCandidate* candidates, size_t count) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    PagedComposer* composer = provider->composer;
    if (PagedSheetPolicyTrial* trial = composer->sheet_trial) {
        if (trial->replay) {
            if (composer->column_index >= trial->replay->count) return SIZE_MAX;
            const TypesetResume& end = trial->replay->columns[composer->column_index].end;
            for (size_t i = 0; i < count; i++) if (!memcmp(&candidates[i].end, &end, sizeof(end))) return i;
            return SIZE_MAX;
        }
        trial->status = paged_sheet_policy_collect(composer, candidates, count);
        return trial->status == TYPESET_OK ? paged_choose_default(candidates, count) : SIZE_MAX;
    }
    const TypesetPagePolicy* policy = composer->composition->options.page_policy;
    return policy ? policy->choose(policy->context, candidates, count) : paged_choose_default(candidates, count);
}

static TypesetStatus paged_sheet_aux_column(PagedComposer* composer, TypesetResume* cursor) {
    PagedSheetPolicyTrial* trial = composer->sheet_trial;
    auto* info = (PagedCandidateInfo*)pool_calloc(trial->pool, sizeof(PagedCandidateInfo));
    if (!info) return TYPESET_OUT_OF_MEMORY;
    TypesetStatus status = paged_sheet_auxiliary_select(composer, nullptr, &info->auxiliary, &info->auxiliary_count);
    if (status != TYPESET_OK) return status;
    // routing across an unplaceable empty column changes geometry only, not the host input cursor.
    if (!info->auxiliary_count && paged_regions_blocked(composer)) return paged_sheet_empty_column(composer, *cursor);
    info->sequence = composer->page->sequence;
    for (const PagedRegionState& region : composer->regions) info->pending_aux |= region.plan.pending_count != 0;
    TypesetPageCandidate candidate = {};
    candidate.start = candidate.end = *cursor;
    if (cursor->serial == UINT64_MAX) return TYPESET_BUDGET_EXHAUSTED;
    candidate.end.serial++;
    candidate.kind = info->auxiliary_count ? TYPESET_PAGE_REGION : TYPESET_PAGE_EMPTY;
    candidate.page_number = composer->page->page_number; candidate.trial = info;
    candidate.available_height = composer->page_style.content_rect.height;
    candidate.note_height = composer->regions[PAGED_REGION_NOTE].plan.reserved_height;
    candidate.float_height = composer->regions[PAGED_REGION_TOP].plan.reserved_height + composer->regions[PAGED_REGION_BOTTOM].plan.reserved_height;
    candidate.boundary = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_COLUMN, 0, 0};
    size_t selected = paged_choose_page(composer->provider, &candidate, 1);
    if (selected != 0) return trial->status != TYPESET_OK ? trial->status : TYPESET_UNPLACEABLE;
    TypesetPagePlan plan = {}; plan.scratch = trial->pool; plan.candidate = candidate;
    for (size_t i = 0; i < info->auxiliary_count; i++) {
        size_t count = info->auxiliary[i].count;
        if (count > composer->composition->options.max_items - plan.count) return TYPESET_BUDGET_EXHAUSTED;
        plan.count += count;
    }
    if (plan.count > SIZE_MAX / sizeof(TypesetContribution)) return TYPESET_BUDGET_EXHAUSTED;
    plan.contributions = plan.count ? (TypesetContribution*)pool_alloc(trial->pool, plan.count * sizeof(TypesetContribution)) : nullptr;
    if (plan.count && !plan.contributions) return TYPESET_OUT_OF_MEMORY;
    size_t index = 0;
    for (size_t i = 0; i < info->auxiliary_count; i++) {
        const TypesetRegionSelection& region = info->auxiliary[i];
        for (size_t j = 0; j < region.count; j++) plan.contributions[index++] = paged_region_contribution(region, region.placements[j]);
    }
    status = paged_sheet_policy_prefix(composer, plan);
    if (status == TYPESET_OK) *cursor = candidate.end;
    return status;
}

static TypesetAssemblyAction paged_pack_page(void* context, const TypesetPageCandidate* candidate) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    const TypesetPagePolicy* policy = provider->composer->composition->options.page_policy;
    TypesetAssemblyAction action = policy && !provider->composer->policy_trial ?
        policy->assemble(policy->context, candidate) : TYPESET_ASSEMBLY_FINALIZE;
    if (action != TYPESET_ASSEMBLY_FINALIZE) return action;
    const PagedCandidateInfo* info = (const PagedCandidateInfo*)candidate->trial;
    float free = candidate->available_height - candidate->body_height - candidate->note_height - candidate->float_height;
    provider->space_ratio = info && info->space_capacity > 0.0f ? fminf(1.0f, fmaxf(0.0f, free / info->space_capacity)) : 1.0f;
    provider->vertical_packing = info ? info->native_packing : TypesetPacking{};
    provider->vertical_end = info ? info->native_end : SIZE_MAX;
    return TYPESET_ASSEMBLY_FINALIZE;
}

static TypesetStatus paged_policy_checkpoint(void* context, TypesetPolicyCheckpoint* saved) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    return typeset_policy_checkpoint(provider->composer->composition->options.page_policy, saved);
}
static TypesetStatus paged_policy_restore(void* context, const TypesetPolicyCheckpoint* saved) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    return typeset_policy_restore(provider->composer->composition->options.page_policy, saved);
}
static TypesetStatus paged_page_transition(void* context, TypesetAssemblyAction action, const TypesetPagePlan* plan) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    const TypesetPagePolicy* policy = provider->composer->composition->options.page_policy;
    if (!policy || !policy->transition || !policy->checkpoint || !policy->restore)
        return paged_failure(provider->composer->composition, TYPESET_INVALID, nullptr,
            provider->composer->page->page_number, "held or reinserted pages require a journaled policy transition");
    return policy->transition(policy->context, action, plan);
}

static TypesetStatus paged_assembly_resolve(PagedComposer* composer, TypesetPagePlan* plan,
        TypesetPageAssembly* assembly, bool* reinsert) {
    assembly->pool = composer->composition->pool;
    assembly->limit = composer->composition->options.max_page_transitions;
    assembly->transitions = composer->composition->page_transitions;
    *reinsert = false;
    TypesetStatus status = TYPESET_OK;
    while (status == TYPESET_OK && plan->action != TYPESET_ASSEMBLY_FINALIZE && !*reinsert)
        status = typeset_page_transition(plan, assembly, reinsert);
    composer->composition->page_transitions = assembly->transitions;
    if (status != TYPESET_OK) paged_failure(composer->composition, status, nullptr,
        composer->page->page_number, status == TYPESET_NO_PROGRESS ? "page assembly repeated a policy state" :
        status == TYPESET_BUDGET_EXHAUSTED ? "page assembly exhausted its transition budget" : "page assembly transition failed");
    return status;
}

static bool paged_sibling_avoidance(PagedComposer* composer, PagedFlowNode* flow) {
    PagedFlowNode* next = flow->kind == PAGED_FLOW_TABLE_ROW ? paged_table_next_body_row(flow) : flow->next;
    return next && (paged_break_avoids(composer, paged_flow_break(flow, false)) ||
        paged_break_avoids(composer, paged_flow_break(next, true)));
}

static TypesetStatus paged_page_line(PagedPageProvider* provider, PagedFlowNode* flow,
        const TypesetResume* cursor, TypesetResume* next, TypesetContribution* contribution) {
    PagedComposer* composer = provider->composer;
    size_t first = cursor->state[1];
    if (first == flow->paragraph.count) { next->state[0]++; next->state[1] = next->state[2] = 0; return TYPESET_OK; }
    TypesetStatus status = paged_space_flush(composer);
    if (status == TYPESET_OK) status = provider->line_storage(flow);
    if (status != TYPESET_OK) return status;
    float width = composer->frames[composer->depth - 1].content_width;
    float containing_height = composer->frames[composer->depth - 1].content_height;
    if (!first && flow->block_note_item) {
        size_t count = 0; float height = 0.0f;
        status = paged_measure_lines(composer, flow, first, width, containing_height, provider->scratch, provider->lines,
            provider->line_capacity, &count, &height, false, flow->block_note_item);
        size_t prefix = status == TYPESET_OK ? paged_block_note_prefix(flow, provider->lines, count) : 0;
        if (prefix) {
            // block-note admission includes the paragraph prefix before its first call.
            PagedRegionTrial trial = {};
            status = paged_regions_for_lines(composer, flow, provider->lines, prefix, &trial);
            paged_region_trial_dispose(&trial);
        }
        if (status != TYPESET_OK) return status;
    }
    TypesetLineCandidate line = {};
    status = paged_next_line(composer, flow, first, width, containing_height, provider->scratch, provider->line_capacity, &line, composer->frames[composer->depth - 1].replaced_height);
    if (status != TYPESET_OK) return status;
    if (line.overflow) return TYPESET_UNPLACEABLE;
    float tail = paged_tail_edges(composer, line.next == flow->paragraph.count && !flow->next);
    if (line.height + tail > composer->bottom - composer->y) return TYPESET_UNPLACEABLE;
    PagedRegionTrial trial = {};
    status = paged_regions_for_lines(composer, flow, &line, 1, &trial);
    if (status == TYPESET_OK) status = paged_regions_accept(composer, &trial);
    else paged_region_trial_dispose(&trial);
    if (status == TYPESET_OK) status = paged_commit_line(composer, flow, line);
    if (status != TYPESET_OK) return status;
    contribution->kind = TYPESET_CONTRIBUTION_BOX; contribution->metrics.height = line.height;
    contribution->boundary.legality = TYPESET_BREAK_ALLOWED;
    next->state[1] = line.next; next->state[2]++;
    if (line.next == flow->paragraph.count) {
        next->state[0]++; next->state[1] = next->state[2] = 0;
        for (PagedFlowNode* ending = flow; ending; ending = ending->parent) {
            if (paged_sibling_avoidance(composer, ending)) contribution->boundary.reason |= PAGED_RELAX_AVOIDANCE;
            if (ending->next) break;
        }
    } else {
        bool minima = next->state[2] < flow->style->orphans;
        float next_width = 0.0f, next_height = 0.0f, next_containing_height = NAN;
        status = paged_next_constraints(composer, &next_width, &next_height, &next_containing_height);
        size_t tail_count = 0; float tail_height = 0.0f;
        if (status == TYPESET_OK) status = paged_measure_lines(composer, flow, line.next, next_width, next_containing_height,
            provider->scratch, nullptr, provider->line_capacity, &tail_count, &tail_height,
            false, SIZE_MAX, flow->style->widows);
        if (status != TYPESET_OK) return status;
        if (minima || tail_count < flow->style->widows) contribution->boundary.reason |= PAGED_RELAX_MINIMA;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_native_material(PagedPageProvider* provider, PagedFlowNode* flow, PagedNativeFlow* native,
        const TypesetContribution& contribution, const TypesetItem* item, const TypesetLineCandidate* alternative,
        const TypesetMetrics& metrics, float x, float y, float baseline) {
    if (provider->measure_only) return TYPESET_OK;
    TypesetStatus status = paged_mark_enter(provider->composer, flow->source);
    if (status != TYPESET_OK) return status;
    ViewNativeMaterial material = {};
    status = native->binding.material(native->binding.context, &contribution, item, alternative, &material);
    if (status != TYPESET_OK) return status;
    material.source = item ? item->source : contribution.source; material.metrics = metrics;
    material.solution = alternative ? alternative->solution : nullptr;
    if (item) { material.start = item->start; material.length = item->length; }
    if (!material.owner.context) material.owner = native->binding.owner;
    PagedComposer* composer = provider->composer;
    return paged_native_append(composer, composer->frames[composer->depth - 1].fragment, material,
        {x, y, metrics.advance, metrics.height + metrics.depth}, baseline);
}

static TypesetStatus paged_native_event(PagedComposer* composer, PagedFlowNode* flow,
        const TypesetContribution& value) {
    TypesetStatus status = paged_mark_enter(composer, flow->source);
    if (status != TYPESET_OK) return status;
    if (value.kind == TYPESET_CONTRIBUTION_MARK) {
        if (!value.mark || !typeset_source_same_identity(value.mark->source, value.source) ||
            value.mark->kind == TYPESET_MARK_RUNNING ||
            (value.mark->value && value.mark->value->provider != value.source.provider)) return TYPESET_INVALID;
        if (composer->composition->marks.count >= composer->composition->options.max_items) return TYPESET_BUDGET_EXHAUSTED;
        TypesetMark mark = *value.mark;
        if (!mark.name || !*mark.name) return TYPESET_INVALID;
        mark.name = pool_strdup(composer->composition->pool, mark.name);
        if (mark.text) mark.text = pool_strdup(composer->composition->pool, mark.text);
        if (!mark.name || (value.mark->text && !mark.text)) return TYPESET_OUT_OF_MEMORY;
        mark.page_number = composer->page->page_number; mark.at_page_start = !composer->page_has_content && !composer->sheet_has_content;
        return typeset_mark_append(&composer->composition->marks, &mark);
    }
    if (!value.target || !typeset_source_same_identity(value.target->source, value.source)) return TYPESET_INVALID;
    TypesetTarget* target = const_cast<TypesetTarget*>(typeset_target_find(&composer->composition->native_targets, value.target->name));
    if (!target || !typeset_source_same_identity(target->source, value.source) || target->value.get() != value.target->value.get())
        return TYPESET_INVALID;
    // all target slots are seeded before trials, so their addresses remain stable through nested journals.
    if (!view_tree_model_record(composer->tree, target, sizeof(*target))) return TYPESET_OUT_OF_MEMORY;
    if (!target->page_number) target->page_number = composer->page->page_number;
    target->last_page_number = composer->page->page_number;
    return TYPESET_OK;
}

static TypesetStatus paged_native_region_anchor(PagedComposer* composer, PagedFlowNode* flow, PagedNativeFlow* native,
        const TypesetContribution& value) {
    const TypesetRegionMaterial* material = value.region_material;
    if (!material || !material->identity || !material->measure || !native->binding.region_item ||
        !typeset_source_same_identity(material->source, value.source) ||
        material->start.provider != value.source.provider || material->start.generation != value.source.generation ||
        material->reference < TYPESET_REGION_REFERENCE_PAGE || material->reference > TYPESET_REGION_REFERENCE_COLUMN ||
        (value.identity && value.identity != material->identity) || value.region_edge > TYPESET_REGION_END ||
        (value.kind == TYPESET_CONTRIBUTION_INSERTION ? value.region != TYPESET_REGION_NOTE : value.region != TYPESET_REGION_FLOAT))
        return TYPESET_INVALID;
    PagedRegionKind kind = value.kind == TYPESET_CONTRIBUTION_INSERTION ? PAGED_REGION_NOTE :
        value.region_edge == TYPESET_REGION_START ? PAGED_REGION_TOP : PAGED_REGION_BOTTOM;
    PagedComposition* composition = composer->composition;
    if (!composition->native_regions) composition->native_regions = lam::own(paged_native_regions_new(16));
    if (!composition->native_regions) return TYPESET_OUT_OF_MEMORY;
    PagedNativeRegionEntry key = {material, native, nullptr};
    const PagedNativeRegionEntry* prior = (const PagedNativeRegionEntry*)hashmap_get(composition->native_regions, &key);
    if (prior) key.region = prior->region;
    else {
        if (hashmap_count(composition->native_regions) >= composition->options.max_nodes) return TYPESET_BUDGET_EXHAUSTED;
        key.region = (PagedNativeRegion*)pool_calloc(composition->pool, sizeof(PagedNativeRegion));
        if (!key.region) return TYPESET_OUT_OF_MEMORY;
        *key.region = {material, native, *material, kind};
        key.region->material.context = key.region; key.region->material.measure = paged_native_region_measure;
        hashmap_set(composition->native_regions, &key);
        if (hashmap_oom(composition->native_regions)) return TYPESET_OUT_OF_MEMORY;
    }
    if (key.region->kind != kind) return TYPESET_INVALID;
    PagedRegionTrial trial = {};
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) trial.anchor_counts[i] = composer->regions[i].anchor_count;
    TypesetStatus status = paged_mark_enter(composer, flow->source);
    if (status == TYPESET_OK) status = paged_region_anchor(composer, kind, &key.region->material, &trial.anchor_counts[kind]);
    float body = paged_body_height(composer) + paged_tail_edges(composer, false);
    body = paged_vertical_measure(composer, body, true).minimum;
    if (status == TYPESET_OK) status = paged_regions_trial(composer, body, &trial);
    if (status == TYPESET_OK) status = paged_regions_accept(composer, &trial);
    paged_region_trial_dispose(&trial);
    return status;
}

static TypesetStatus paged_native_expand(PagedComposition* composition, PagedNativeFlow* root,
        size_t index, TypesetContribution* value, size_t* following, PagedNativeFlow** producer, bool* structural) {
    if (index >= root->count) return TYPESET_STALE;
    PagedNativeCursor cursor = root->cursors[index];
    PagedNativeFlow* native = cursor.producer; *producer = native; *structural = false;
    if (memcmp(&cursor.resume, &native->active, sizeof(TypesetResume))) {
        TypesetStatus status = typeset_flow_restore(&native->binding.provider, &cursor.resume, &native->active);
        if (status != TYPESET_OK) return status;
        if (memcmp(&cursor.resume, &native->active, sizeof(TypesetResume))) return TYPESET_STALE;
    }
    TypesetResume end = {};
    TypesetStatus status = typeset_flow_next(&native->binding.provider, &cursor.resume, value, &end);
    if (status == TYPESET_DONE && cursor.parent != SIZE_MAX) {
        *value = {}; value->kind = TYPESET_CONTRIBUTION_BOUNDARY;
        value->source = cursor.return_source; value->boundary = cursor.return_boundary;
        *following = cursor.parent; *structural = true; return TYPESET_OK;
    }
    if (status != TYPESET_OK) return status;
    native->active = end;
    if (value->source.provider != native->binding.provider.identity || value->source.generation != native->binding.provider.generation ||
        value->source.offset_unit > TYPESET_PROVIDER_OFFSETS ||
        (value->source.native && value->source.native->provider != value->source.provider) ||
        value->boundary.legality > TYPESET_BREAK_FORCED || value->boundary.scope > TYPESET_BREAK_PAGE)
        return TYPESET_INVALID;
    status = typeset_flow_checkpoint(&native->binding.provider, &end, &cursor.resume);
    if (status == TYPESET_OK) status = paged_native_cursor(composition, root, cursor, following);
    if (status != TYPESET_OK || value->kind != TYPESET_CONTRIBUTION_NESTED) return status;
    const TypesetFlowProvider* requested = value->nested;
    PagedNativeFlow* child = nullptr;
    for (auto* entry = composition->native_flows; requested && entry; entry = entry->next)
        if (entry->binding.provider.identity == requested->identity && entry->binding.provider.generation == requested->generation)
            child = entry;
    if (!child || child->binding.control.address || child->binding.provider.context != requested->context ||
        child->binding.provider.next != requested->next || child->binding.provider.checkpoint != requested->checkpoint ||
        child->binding.provider.restore != requested->restore) return TYPESET_INVALID;
    size_t depth = 1;
    for (size_t parent = index; parent != SIZE_MAX; parent = root->cursors[parent].parent) {
        if (root->cursors[parent].producer == child) return TYPESET_INVALID;
        if (++depth > composition->options.max_depth) return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED,
            root->binding.control.address, 0, "native nested provider depth exceeds the common context budget");
    }
    // parent indices and canonical cursors survive trial rewind; no stack or scratch pointer is serialized.
    PagedNativeCursor entered = {child->cursors[0].resume, child, *following, 0, value->boundary, value->source};
    status = paged_native_cursor(composition, root, entered, following);
    if (status != TYPESET_OK) return status;
    *value = {}; value->kind = TYPESET_CONTRIBUTION_BOUNDARY;
    value->source = entered.return_source; value->boundary.legality = TYPESET_BREAK_FORBIDDEN;
    *structural = true; return TYPESET_OK;
}

static TypesetStatus paged_native_lookahead(PagedComposition* composition, PagedNativeFlow* root,
        size_t index, bool* terminal) {
    Pool* pool = mem_pool_create((MemContext*)composition->document->services.mem_ctx,
        MEM_ROLE_LAYOUT, "typeset.native.lookahead");
    if (!pool) return TYPESET_OUT_OF_MEMORY;
    PagedNativeCheckpoint* records = nullptr;
    TypesetStatus status = paged_native_checkpoint(composition, pool, &records);
    *terminal = false;
    for (size_t steps = 0; status == TYPESET_OK; steps++) {
        if (steps >= composition->options.max_items) { status = TYPESET_BUDGET_EXHAUSTED; break; }
        TypesetContribution value = {}; PagedNativeFlow* producer = nullptr; bool structural = false;
        status = paged_native_expand(composition, root, index, &value, &index, &producer, &structural);
        if (status == TYPESET_DONE) { *terminal = true; status = TYPESET_OK; break; }
        if (status != TYPESET_OK || !structural) break;
    }
    TypesetStatus restored = paged_native_restore(records);
    mem_pool_destroy(pool);
    return restored == TYPESET_OK ? status : restored;
}

static TypesetStatus paged_native_next(PagedPageProvider* provider, PagedFlowNode* flow,
        const TypesetResume* cursor, TypesetResume* next, TypesetContribution* contribution) {
    PagedComposer* composer = provider->composer;
    PagedNativeFlow* root = flow->native;
    size_t index = cursor->state[1];
    if (!composer->depth || index >= root->count) return TYPESET_STALE;
    const TypesetResume saved = root->cursors[index].resume;
    TypesetContribution value = {}; PagedNativeFlow* native = nullptr;
    size_t following = 0; bool structural = false;
    TypesetStatus status = paged_native_expand(composer->composition, root, index, &value, &following, &native, &structural);
    if (status == TYPESET_DONE) {
        next->state[0]++; next->state[1] = next->state[3] = 0;
        return TYPESET_OK;
    }
    if (status != TYPESET_OK) return paged_failure(composer->composition, status, flow->source,
        composer->page ? composer->page->page_number : 0, "native flow expansion failed");
    *contribution = value; next->state[1] = following; next->state[3] = 0;
    float x = composer->frames[composer->depth - 1].content_x;
    float width = composer->frames[composer->depth - 1].content_width;
    float extent = 0.0f;
    status = paged_space_flush(composer);
    if (status != TYPESET_OK) return status;
    // repeated furniture cannot create body insertions or consume a page-assembly transition.
    if (composer->role != VIEW_FRAGMENT_BODY && (value.kind == TYPESET_CONTRIBUTION_INSERTION ||
        value.kind == TYPESET_CONTRIBUTION_FLOAT || value.kind == TYPESET_CONTRIBUTION_FLUSH_DEFERRED ||
        value.kind == TYPESET_CONTRIBUTION_MARK))
        return paged_failure(composer->composition, TYPESET_INVALID, flow->source,
            composer->page ? composer->page->page_number : 0, "native static content cannot schedule marks, insertions or deferred floats");
    switch (value.kind) {
        case TYPESET_CONTRIBUTION_BOX:
            extent = value.metrics.height + value.metrics.depth;
            if (!isfinite(extent) || extent < 0.0f || value.metrics.advance > width) return TYPESET_UNPLACEABLE;
            status = paged_native_material(provider, flow, native, value, nullptr, nullptr, value.metrics, x, composer->y,
                composer->y + value.metrics.baseline);
            break;
        case TYPESET_CONTRIBUTION_PARAGRAPH: {
            const TypesetParagraph* paragraph = value.paragraph;
            if (!paragraph || !paragraph->items || !paragraph->count) return TYPESET_INVALID;
            size_t capacity = paragraph->max_alternatives ? paragraph->max_alternatives : paragraph->count;
            if (paragraph->count > composer->composition->options.max_items || capacity > composer->composition->options.max_items)
                return TYPESET_BUDGET_EXHAUSTED;
            if (!lam::pool_grow_array(composer->composition->pool, &provider->scratch, &provider->line_capacity,
                capacity, 16)) return TYPESET_OUT_OF_MEMORY;
            TypesetLineCandidate line = {};
            status = typeset_next_line(paragraph, cursor->state[3], width, provider->scratch, capacity, &line);
            if (status != TYPESET_OK) return status;
            extent = line.height;
            if (line.overflow) return TYPESET_UNPLACEABLE;
            float baseline = paragraph->baseline_aware || paragraph->minimum_baseline > 0.0f || line.baseline > 0.0f
                ? line.baseline : line.height - line.depth;
            if (!isfinite(baseline)) return TYPESET_INVALID;
            for (size_t i = line.paint_first; status == TYPESET_OK && i < line.paint_end; i++) {
                const TypesetItem& item = paragraph->items[i];
                TypesetMetrics metrics = item.metrics;
                if (paragraph->measure && !paragraph->measure(paragraph, i, width, &metrics, paragraph->context))
                    return TYPESET_INVALID;
                if (item.kind == TYPESET_GLUE) x += typeset_glue_advance(&item.glue, &line.packing);
                else if (item.kind == TYPESET_BOX || item.paint) {
                    status = paged_native_material(provider, flow, native, value, &item, &line, metrics, x,
                        composer->y + baseline - metrics.height, composer->y + baseline);
                    x += metrics.advance;
                }
            }
            if (line.next < paragraph->count) {
                next->state[1] = index; next->state[3] = line.next;
                contribution->boundary = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_PAGE, line.penalty, 0};
                // partially consumed paragraphs retain the producer's pre-expansion checkpoint.
                TypesetStatus restored = typeset_flow_restore(&native->binding.provider, &saved, &native->active);
                if (restored != TYPESET_OK) return restored;
            }
            contribution->kind = TYPESET_CONTRIBUTION_BOX; contribution->metrics.height = extent; contribution->metrics.depth = 0.0f;
            break;
        }
        case TYPESET_CONTRIBUTION_GLUE:
            if (!isfinite(value.glue.natural) || !isfinite(value.glue.stretch) || !isfinite(value.glue.shrink) ||
                value.glue.stretch < 0.0f || value.glue.shrink < 0.0f) return TYPESET_INVALID;
            if (value.glue.discard_start && !composer->page_has_content) break;
            if (provider->vertical_count >= composer->composition->options.max_items) return TYPESET_BUDGET_EXHAUSTED;
            if (!lam::pool_grow_array(composer->composition->pool, &provider->vertical_glues, &provider->vertical_capacity,
                provider->vertical_count + 1, 16)) return TYPESET_OUT_OF_MEMORY;
            provider->vertical_glues[provider->vertical_count] = {};
            provider->vertical_glues[provider->vertical_count].kind = TYPESET_GLUE;
            provider->vertical_glues[provider->vertical_count].glue = value.glue;
            extent = provider->vertical_count < provider->vertical_end ? typeset_glue_advance(&value.glue, &provider->vertical_packing) : 0.0f;
            provider->vertical_count++;
            break;
        case TYPESET_CONTRIBUTION_BOUNDARY:
            break;
        case TYPESET_CONTRIBUTION_MARK:
        case TYPESET_CONTRIBUTION_TARGET:
            if (!provider->measure_only) status = paged_native_event(composer, flow, value);
            break;
        case TYPESET_CONTRIBUTION_INSERTION:
        case TYPESET_CONTRIBUTION_FLOAT:
            status = paged_native_region_anchor(composer, flow, native, value);
            break;
        case TYPESET_CONTRIBUTION_FLUSH_DEFERRED: {
            bool pending = paged_region_plans_pending(composer);
            bool occupied = composer->page_has_content || paged_regions_placed(composer) || pending;
            contribution->boundary.legality = occupied ? TYPESET_BREAK_FORCED : TYPESET_BREAK_ALLOWED;
            if (pending) {
                // keep the flush cursor until every earlier region is placed, independently of ordinary page ejects.
                next->state[1] = index;
                status = typeset_flow_restore(&native->binding.provider, &saved, &native->active);
            }
            break;
        }
        default:
            return paged_failure(composer->composition, TYPESET_INVALID, flow->source,
                composer->page->page_number, "native contribution requires an admitted common region or event producer");
    }
    if (status == TYPESET_OK && contribution->boundary.legality == TYPESET_BREAK_FORCED) {
        bool terminal = false;
        status = paged_native_lookahead(composer->composition, root, next->state[1], &terminal);
        if (status != TYPESET_OK) return status;
        // nested providers may finish before their parent; only exhaustion of the full path suppresses an eject.
        if (terminal) contribution->boundary.legality = TYPESET_BREAK_ALLOWED;
    }
    if (status == TYPESET_OK) {
        composer->y += extent;
        if (value.kind == TYPESET_CONTRIBUTION_BOX || value.kind == TYPESET_CONTRIBUTION_PARAGRAPH) {
            composer->page_has_content = true; provider->vertical_tail = provider->vertical_count;
        }
    }
    return status;
}

static TypesetStatus paged_native_fixed_flow(PagedComposer* composer, PagedFlowNode* flow, bool measure) {
    Pool* pool = mem_pool_create((MemContext*)composer->composition->document->services.mem_ctx,
        MEM_ROLE_LAYOUT, "typeset.native.fixed-region");
    if (!pool) return TYPESET_OUT_OF_MEMORY;
    PagedNativeCheckpoint* checkpoint = nullptr;
    TypesetStatus status = paged_native_checkpoint(composer->composition, pool, &checkpoint);
    PagedPageProvider provider = {}; provider.composer = composer; provider.measure_only = measure;
    TypesetResume cursor = {}, next = {};
    for (size_t count = 0; status == TYPESET_OK && !cursor.state[0]; count++) {
        if (count >= composer->composition->options.max_items) { status = TYPESET_BUDGET_EXHAUSTED; break; }
        next = cursor;
        TypesetContribution value = {};
        status = paged_native_next(&provider, flow, &cursor, &next, &value);
        if (status == TYPESET_OK && value.boundary.legality == TYPESET_BREAK_FORCED)
            status = paged_failure(composer->composition, TYPESET_INVALID, flow->source,
                composer->page ? composer->page->page_number : 0, "native static content cannot force a page break");
        cursor = next;
    }
    // each repeated region starts from immutable input without consuming another flow's producer state.
    TypesetStatus restored = paged_native_restore(checkpoint);
    mem_pool_destroy(pool);
    return restored == TYPESET_OK ? status : restored;
}

static TypesetStatus paged_page_next(void* context, const TypesetResume* cursor,
        TypesetContribution* contribution, TypesetResume* next) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    PagedComposer* composer = provider->composer;
    if (cursor->state[0] == provider->count) return TYPESET_DONE;
    if (cursor->state[0] > provider->count || memcmp(cursor, &provider->active, sizeof(*cursor))) return TYPESET_STALE;
    if (!provider->checkpoint.view) {
        TypesetStatus status = provider->checkpoint.begin(composer);
        if (status != TYPESET_OK) return status;
    }
    const PagedFlowStep& step = provider->steps[cursor->state[0]];
    PagedFlowNode* flow = step.flow;
    composer->space_ratio = provider->space_ratio;
    provider->boundary_keep = provider->page_keep = {};
    *next = *cursor; next->serial++; next->state[0]++;
    *contribution = {}; contribution->kind = TYPESET_CONTRIBUTION_BOUNDARY;
    contribution->payload = &step;
    contribution->boundary = {TYPESET_BREAK_FORBIDDEN, TYPESET_BREAK_PAGE, 0, 0};
    contribution->source = {composer->tree->model->tree_id, composer->tree->layout_generation,
        dom_node_ref(flow->source).expected_id, TYPESET_PROVIDER_OFFSETS, nullptr};
    TypesetStatus status = TYPESET_OK;
    float previous_y = composer->y;
    switch (step.kind) {
        case PAGED_STEP_BEFORE: {
            bool forced = false;
            status = paged_block_before(composer, flow, &forced);
            if (forced) contribution->boundary.legality = TYPESET_BREAK_FORCED;
            break;
        }
        case PAGED_STEP_OPEN:
            status = paged_block_trial_begin(composer, flow);
            if (status == TYPESET_OK) status = paged_block_enter(composer, flow);
            break;
        case PAGED_STEP_LINE:
            next->state[0] = cursor->state[0];
            status = paged_page_line(provider, flow, cursor, next, contribution);
            break;
        case PAGED_STEP_NATIVE:
            next->state[0] = cursor->state[0];
            status = paged_native_next(provider, flow, cursor, next, contribution);
            contribution->payload = &step;
            break;
        case PAGED_STEP_TABLE_ROW: {
            bool complete = false; uint64_t continuation = 0;
            // one row slice per fragmentainer; another column on the same sheet is a distinct region.
            uint64_t region = (static_cast<uint64_t>(composer->page->page_number) << 32) | composer->column_index;
            status = paged_space_flush(composer);
            if (status == TYPESET_OK) status = cursor->state[3] == region ? TYPESET_UNPLACEABLE :
                paged_table_row(composer, flow, cursor->state[1], &continuation, &complete,
                    &contribution->boundary.reason, &contribution->metrics.height);
            contribution->kind = TYPESET_CONTRIBUTION_BOX;
            // a terminal row closes its table and footer before the group-after boundary advances.
            contribution->boundary.legality = composer->pending_break && paged_table_next_body_row(flow) ?
                TYPESET_BREAK_FORCED : TYPESET_BREAK_ALLOWED;
            PagedFlowNode* boundary_row = flow->span_last ? flow->span_last : flow;
            if (flow->row_group && paged_break_avoids(composer, flow->row_group->break_inside) && (!complete || !boundary_row->group_last))
                contribution->boundary.reason |= PAGED_RELAX_AVOIDANCE;
            if (complete && (paged_sibling_avoidance(composer, flow) || paged_break_avoids(composer, paged_flow_break(flow, false))))
                contribution->boundary.reason |= PAGED_RELAX_AVOIDANCE;
            next->state[1] = continuation; next->state[2] = 0;
            next->state[3] = complete ? 0 : region;
            if (!complete) next->state[0] = cursor->state[0];
            break;
        }
        case PAGED_STEP_CLOSE: {
            float parent_width = composer->depth > 1 ? composer->frames[composer->depth - 2].content_width : composer->page_style.content_rect.width;
            if (paged_block_remaining(composer) > 0.0f) {
                status = paged_space_flush(composer);
                if (status != TYPESET_OK) break;
            }
            float remaining = paged_block_remaining(composer);
            float available = composer->bottom - composer->y - paged_region_reserved(composer) - paged_tail_edges(composer, true);
            if (remaining > 0.0f) {
                if (available <= 0.0f) { status = TYPESET_UNPLACEABLE; break; }
                float used = fminf(remaining, available);
                if (remaining - used >= remaining) { status = TYPESET_NO_PROGRESS; break; }
                composer->y += used; composer->page_has_content = true;
                status = paged_mark_enter(composer, flow->source);
                if (status != TYPESET_OK) break;
                if (used < remaining) { next->state[0] = cursor->state[0]; contribution->boundary.legality = TYPESET_BREAK_ALLOWED; break; }
            }
            status = paged_block_finish(composer, parent_width);
            size_t following = provider->following_material(next->state[0]);
            // trailing forced breaks do not defer the enclosing closures to an empty sheet.
            contribution->boundary.legality = composer->pending_break && following < provider->count ?
                TYPESET_BREAK_FORCED : TYPESET_BREAK_ALLOWED;
            if (paged_sibling_avoidance(composer, flow)) contribution->boundary.reason |= PAGED_RELAX_AVOIDANCE;
            break;
        }
    }
    if (status == TYPESET_OK && step.kind != PAGED_STEP_NATIVE && composer->y > previous_y)
        provider->vertical_tail = provider->vertical_count;
    if (status == TYPESET_UNPLACEABLE) {
        // keep earlier candidates; the overflowing trial is discarded by the start checkpoint.
        provider->blocked = status; provider->failed_source = flow->source;
        if (composer->composition->diagnostic.status != TYPESET_OK)
            provider->hard_failure = composer->composition->diagnostic;
        provider->failed_reason = step.kind == PAGED_STEP_TABLE_ROW ?
            (flow->span_last ? "row spans exceeding a page require spanning-cell continuations" :
            "table row and repeated groups cannot fit in a fresh page region") : step.kind == PAGED_STEP_CLOSE ?
            "block extent cannot make progress after reopening page decorations" :
            "paragraph cannot make progress in a fresh page region";
        status = TYPESET_OK;
    }
    if (status != TYPESET_OK) {
        provider->hard_failure = composer->composition->diagnostic;
        return status;
    }
    if (contribution->boundary.legality == TYPESET_BREAK_FORCED) {
        // forced boundaries override soft keeps; they do not constitute avoidance relaxation.
        contribution->boundary.reason = 0;
        if (step.kind != PAGED_STEP_NATIVE && composer->requested_break == VIEW_BREAK_COLUMN)
            contribution->boundary.scope = TYPESET_BREAK_COLUMN;
    } else {
        bool complete = step.kind == PAGED_STEP_CLOSE || (step.kind == PAGED_STEP_LINE && next->state[0] != cursor->state[0]) ||
            (step.kind == PAGED_STEP_TABLE_ROW && next->state[0] != cursor->state[0]);
        provider->boundary_keep = paged_boundary_keep(composer, flow, complete, false);
        provider->page_keep = paged_boundary_keep(composer, flow, complete, true);
        if (step.kind == PAGED_STEP_LINE && !complete && status == TYPESET_OK) {
            for (size_t scope = 1; scope < 3; scope++) {
                RadiantKeepStrength keep = paged_inline_keep(flow, cursor->state[1], next->state[1], next->state[1], scope);
                if (scope == 1 || !paged_has_next_column(composer)) provider->boundary_keep = radiant_keep_max(provider->boundary_keep, keep);
                provider->page_keep = radiant_keep_max(provider->page_keep, keep);
            }
        }
        if (provider->boundary_keep.kind == RADIANT_KEEP_ALWAYS) contribution->boundary.legality = TYPESET_BREAK_FORBIDDEN;
        for (size_t i = 0; i < composer->depth; i++)
            if (paged_break_avoids(composer, composer->frames[i].flow->style->break_inside))
                contribution->boundary.reason |= PAGED_RELAX_AVOIDANCE;
        if (((contribution->boundary.reason & PAGED_RELAX_MINIMA) && !provider->relax_minima) ||
            ((contribution->boundary.reason & PAGED_RELAX_AVOIDANCE) && !provider->relax_avoidance))
            contribution->boundary.legality = TYPESET_BREAK_FORBIDDEN;
    }
    provider->active = *next;
    return TYPESET_OK;
}

static TypesetStatus paged_page_restore(void* context, const TypesetResume* saved, TypesetResume* restored) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    if (saved->serial < provider->start.serial) return TYPESET_STALE;
    TypesetStatus status = provider->checkpoint.view ? provider->checkpoint.restore() : TYPESET_OK;
    provider->blocked = TYPESET_OK; provider->active = provider->start;
    provider->vertical_count = provider->vertical_tail = 0;
    // continuations contain stable flow indices; replay under one journal owns every trial fragment.
    while (status == TYPESET_OK && provider->active.serial < saved->serial) {
        TypesetContribution contribution = {}; TypesetResume next = {};
        status = paged_page_next(provider, &provider->active, &contribution, &next);
        if (status == TYPESET_OK && provider->blocked != TYPESET_OK) status = provider->blocked;
    }
    if (status != TYPESET_OK) return status;
    if (memcmp(&provider->active, saved, sizeof(*saved))) return TYPESET_STALE;
    *restored = provider->active;
    return TYPESET_OK;
}

static TypesetStatus paged_page_probe(void* context, const TypesetContribution* contributions, size_t count,
        TypesetPageCandidate* candidate, bool* stop) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    PagedComposer* composer = provider->composer;
    if (provider->blocked != TYPESET_OK) { *stop = true; return provider->blocked; }
    float body = paged_body_height(composer);
    body += paged_tail_edges(composer, false);
    RadiantSpaceSpec tail = paged_space_range(composer, true);
    body += tail.minimum + (tail.optimum - tail.minimum) * provider->space_ratio;
    PagedVerticalMeasure vertical = paged_vertical_measure(composer, body, true);
    size_t glue_end = vertical.end; body = vertical.natural;
    PagedRegionTrial trial = {};
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) trial.anchor_counts[i] = composer->regions[i].anchor_count;
    TypesetStatus status = paged_regions_trial(composer, vertical.minimum, &trial);
    if (status == TYPESET_OK) {
        candidate->kind = TYPESET_PAGE_FLOW; candidate->page_number = composer->page->page_number;
        candidate->body_height = fmaxf(0.0f, body);
        candidate->note_height = trial.plans[PAGED_REGION_NOTE].reserved_height;
        candidate->float_height = trial.plans[PAGED_REGION_TOP].reserved_height + trial.plans[PAGED_REGION_BOTTOM].reserved_height;
        candidate->available_height = composer->page_style.content_rect.height;
        float target = fmaxf(0.0f, candidate->available_height - candidate->note_height - candidate->float_height);
        TypesetPacking packing = typeset_pack_glue(provider->vertical_glues, 0, glue_end, body, target);
        if (glue_end) candidate->body_height = fmaxf(0.0f, target - packing.residual);
        if (glue_end && !packing.order) candidate->cost += fmin(10000.0, 100.0 * fabs((double)packing.ratio * packing.ratio * packing.ratio));
        float capacity = composer->space_optimum_extra + tail.optimum - tail.minimum;
        const RadiantPageSequence* sequence = composer->page->sequence;
        bool pending = false;
        for (const TypesetRegionPlan& plan : trial.plans) pending |= plan.pending_count != 0;
        if (provider->reject_terminal && provider->finishes_sequence(candidate->end, sequence) && !pending)
            candidate->boundary.legality = TYPESET_BREAK_FORBIDDEN;
        const TypesetRegionSelection* auxiliary = nullptr; size_t auxiliary_count = 0;
        if (candidate->boundary.legality != TYPESET_BREAK_FORBIDDEN && composer->sheet_trial)
            status = paged_sheet_auxiliary_select(composer, &trial, &auxiliary, &auxiliary_count);
        if (status == TYPESET_OK && candidate->boundary.legality != TYPESET_BREAK_FORBIDDEN &&
            (provider->boundary_keep.kind != RADIANT_KEEP_AUTO || provider->page_keep.kind != RADIANT_KEEP_AUTO ||
                auxiliary_count || capacity > 0.0f || provider->vertical_count || (sequence && sequence->master_program))) {
            PagedCandidateInfo* info = (PagedCandidateInfo*)pool_alloc(provider->candidate_pool, sizeof(PagedCandidateInfo));
            if (!info) status = TYPESET_OUT_OF_MEMORY;
            else { *info = {provider->boundary_keep, provider->page_keep, capacity, sequence, pending, packing, glue_end,
                auxiliary, auxiliary_count}; candidate->trial = info; }
        }
    }
    paged_region_trial_dispose(&trial);
    if (status == TYPESET_UNPLACEABLE || candidate->body_height + candidate->note_height + candidate->float_height > composer->page_style.content_rect.height) {
        const PagedFlowStep* step = (const PagedFlowStep*)contributions[count - 1].payload;
        // opening edges cannot choose a break; diagnose at the first material they obstruct.
        *stop = step->kind != PAGED_STEP_OPEN && step->kind != PAGED_STEP_BEFORE;
        if (*stop) {
            provider->failed_source = step->flow->source;
            provider->failed_reason = "paragraph cannot make progress in a fresh page region";
        }
        return TYPESET_UNPLACEABLE;
    }
    return status;
}

static TypesetStatus paged_page_committed(void* context, const TypesetPageCandidate* candidate) {
    PagedPageProvider* provider = (PagedPageProvider*)context;
    if (candidate->boundary.reason & PAGED_RELAX_MINIMA) provider->composer->composition->diagnostic.relaxed_line_minima++;
    if (candidate->boundary.reason & PAGED_RELAX_AVOIDANCE) provider->composer->composition->diagnostic.relaxed_avoidance++;
    TypesetStatus status = paged_space_flush(provider->composer, true);
    const TypesetPagePolicy* policy = provider->composer->composition->options.page_policy;
    if (status == TYPESET_OK && policy && policy->committed && !provider->composer->policy_trial)
        status = policy->committed(policy->context, candidate);
    if (status != TYPESET_OK) return status;
    provider->composer->policy_done = true;
    return provider->checkpoint.accept();
}

static TypesetStatus paged_page_plan_try(PagedPageProvider* context, const TypesetFlowProvider* provider,
        const TypesetResume* cursor, const TypesetPageConstraints* constraints, const TypesetPageProbe* probe,
        const TypesetPagePolicy* policy, Pool* scratch, TypesetPagePlan* plan) {
    context->relax_minima = context->relax_avoidance = false;
    context->hard_failure = {}; context->failed_source = nullptr; context->failed_reason = nullptr;
    context->space_ratio = 0.0f; context->vertical_packing = {}; context->vertical_end = SIZE_MAX;
    context->vertical_count = context->vertical_tail = 0;
    TypesetPolicyCheckpoint policy_checkpoint = {};
    TypesetStatus status = typeset_policy_checkpoint(policy, &policy_checkpoint);
    if (status == TYPESET_OK) status = typeset_page_plan(provider, cursor, constraints, probe, policy, scratch, plan);
    if (status == TYPESET_UNPLACEABLE && !paged_regions_placed(context->composer)) {
        context->relax_minima = true; context->space_ratio = 0.0f; context->vertical_packing = {}; context->vertical_end = SIZE_MAX;
        status = typeset_policy_restore(policy, &policy_checkpoint);
        if (status == TYPESET_OK) status = typeset_page_plan(provider, cursor, constraints, probe, policy, scratch, plan);
        if (status == TYPESET_UNPLACEABLE) {
            context->relax_avoidance = true; context->space_ratio = 0.0f; context->vertical_packing = {}; context->vertical_end = SIZE_MAX;
            status = typeset_policy_restore(policy, &policy_checkpoint);
            if (status == TYPESET_OK) status = typeset_page_plan(provider, cursor, constraints, probe, policy, scratch, plan);
        }
    }
    return status;
}

static bool paged_selected_page_break(PagedPageProvider* context, const TypesetResume& next,
        const TypesetPageCandidate& candidate) {
    PagedComposer* composer = context->composer;
    size_t following = context->following_material(next.state[0]);
    if (following < context->count && context->steps[following].kind == PAGED_STEP_BEFORE) {
        ViewBreak before = paged_flow_break(context->steps[following].flow, true);
        // resolve adjoining forced values before sidedness can insert a blank sheet.
        if (before >= VIEW_BREAK_COLUMN) { composer->pending_break = true; composer->requested_break = before; }
    }
    return (candidate.boundary.legality == TYPESET_BREAK_FORCED && candidate.boundary.scope == TYPESET_BREAK_PAGE) ||
        (composer->pending_break && composer->requested_break >= VIEW_BREAK_PAGE);
}

struct PagedSheetResult {
    const RadiantPageSequence* sequence;
    bool finished, reinsert;
};


static TypesetStatus paged_layout_columns(PagedPageProvider* context, const TypesetFlowProvider* provider,
        TypesetResume* cursor, const TypesetPageConstraints* constraints, const TypesetPageProbe* probe,
        const TypesetPagePolicy* policy, TypesetPageAssembly* assembly, bool reject_terminal, PagedSheetResult* result) {
    PagedComposer* composer = context->composer;
    *result = {};
    TypesetStatus status = TYPESET_OK;
    if (composer->auxiliary_page) {
        result->sequence = composer->page->sequence;
        return paged_sheet_tail_columns(composer, &result->finished, cursor);
    }
    while (status == TYPESET_OK && cursor->state[0] < context->count) {
        context->start = context->active = *cursor; context->blocked = TYPESET_OK;
        context->reject_terminal = reject_terminal;
        Pool* scratch = composer->sheet_trial ? composer->sheet_trial->pool :
            mem_pool_create((MemContext*)composer->tree->model->document->services.mem_ctx, MEM_ROLE_LAYOUT, "typeset.html.whole-page");
        if (!scratch) return TYPESET_OUT_OF_MEMORY;
        context->candidate_pool = scratch;
        TypesetPagePlan plan = {}; TypesetResume next = {};
        status = paged_page_plan_try(context, provider, cursor, constraints, probe, policy, scratch, &plan);
        if (composer->sheet_trial && composer->sheet_trial->status != TYPESET_OK) status = composer->sheet_trial->status;
        const PagedCandidateInfo* info = status == TYPESET_OK ? (const PagedCandidateInfo*)plan.candidate.trial : nullptr;
        if (info) result->sequence = info->sequence;
        else if (status == TYPESET_UNPLACEABLE) result->sequence = context->source_sequence(context->failed_source);
        bool reinsert = false, force_page = false;
        if (status == TYPESET_OK) status = paged_assembly_resolve(composer, &plan, assembly, &reinsert);
        if (status == TYPESET_OK && !reinsert) status = typeset_page_commit(&plan, &next);
        if (status == TYPESET_OK && !reinsert) {
            result->sequence = composer->page->sequence;
            result->finished = context->finishes_page(plan.candidate, result->sequence);
            force_page = paged_selected_page_break(context, next, plan.candidate);
            if (composer->sheet_trial) status = paged_sheet_policy_prefix(composer, plan);
        }
        if (!composer->sheet_trial) mem_pool_destroy(scratch);
        if (status == TYPESET_OK && reinsert) {
            if (composer->composition->options.page_policy && composer->sequence && composer->sequence->master_program &&
                composer->sequence->master_program->terminal) {
                result->reinsert = true; return TYPESET_OK;
            }
            // the current input returns without shipout; retain the policy's progress for its next plan.
            composer->terminal_page = 0; composer->retain_aux_tail = false;
            status = paged_empty_page_restyle(composer, false);
            continue;
        }
        if (status == TYPESET_UNPLACEABLE && paged_regions_placed(composer)) {
            // insertion-only columns advance queues without consuming body input.
            if (!paged_has_next_column(composer)) return TYPESET_OK;
            status = paged_next_column(composer); cursor->state[2] = 0; continue;
        }
        if (status == TYPESET_UNPLACEABLE && composer->page->style->column_rects && !composer->page_has_content) {
            // an empty narrow/short column may be bypassed; only a sheet with source progress can eject.
            if (!paged_has_next_column(composer)) return composer->sheet_has_content ? TYPESET_OK : status;
            status = paged_sheet_empty_column(composer, *cursor);
            if (status != TYPESET_OK) return status;
            status = paged_next_column(composer); cursor->state[2] = 0; continue;
        }
        if (status == TYPESET_UNPLACEABLE && reject_terminal && composer->sheet_has_content)
            return TYPESET_OK; // retain the final body material for a following terminal sheet.
        if (status != TYPESET_OK) return status;
        if (next.serial <= cursor->serial) return TYPESET_NO_PROGRESS;
        *cursor = next;
        if (!composer->policy_trial) assembly->count = 0;
        if (composer->sheet_trial && composer->sheet_trial->replay &&
            composer->column_index + 1 == composer->sheet_trial->replay->count) break;
        if (!result->finished && (next.state[0] == context->count || context->finishes_sequence(next, result->sequence)) &&
            paged_region_plans_pending(composer) && paged_has_next_column(composer)) {
            // unused columns can finish queued material under the same provisional sheet master.
            status = paged_sheet_tail_columns(composer, &result->finished, cursor);
            cursor->state[2] = 0;
            break;
        }
        if (result->finished || cursor->state[0] == context->count || force_page || !paged_has_next_column(composer)) break;
        status = paged_next_column(composer); cursor->state[2] = 0;
    }
    return status;
}

static TypesetStatus paged_layout_sheet(PagedPageProvider* context, const TypesetFlowProvider* provider,
        TypesetResume* cursor, const TypesetPageConstraints* constraints, const TypesetPageProbe* probe,
        const TypesetPagePolicy* policy, TypesetPageAssembly* assembly, bool reject_terminal, PagedSheetResult* result) {
    PagedComposer* composer = context->composer;
    const TypesetPagePolicy* external = composer->composition->options.page_policy;
    if (!external) return paged_layout_columns(context, provider, cursor, constraints, probe, policy, assembly, reject_terminal, result);
    Pool* pool = mem_pool_create((MemContext*)composer->tree->model->document->services.mem_ctx,
        MEM_ROLE_LAYOUT, "typeset.html.sheet-policy");
    if (!pool) return TYPESET_OUT_OF_MEMORY;
    PagedSheetPolicyTrial trial = {}; trial.pool = pool;
    PagedCheckpoint checkpoint;
    TypesetStatus status = checkpoint.begin(composer);
    TypesetResume start = *cursor;
    bool provisional = composer->policy_trial;
    PagedSheetPolicyTrial* previous = composer->sheet_trial;
    if (status == TYPESET_OK) {
        // enumerate physical ejects along the ordinary sequential column prefix without assembly side effects.
        composer->policy_trial = true; composer->sheet_trial = &trial;
        status = paged_layout_columns(context, provider, cursor, constraints, probe, policy, assembly, reject_terminal, result);
    }
    if (status == TYPESET_UNPLACEABLE && trial.count) status = TYPESET_OK;
    if (context->checkpoint.view) {
        TypesetStatus restored = context->checkpoint.restore();
        if (restored != TYPESET_OK) status = restored;
    }
    size_t selected = SIZE_MAX;
    TypesetPolicyCheckpoint chosen = {};
    if (status == TYPESET_OK) {
        selected = trial.count ? external->choose(external->context, trial.candidates, trial.count) : SIZE_MAX;
        status = selected < trial.count ? typeset_policy_checkpoint(external, &chosen) : TYPESET_UNPLACEABLE;
    }
    if (status == TYPESET_OK) status = checkpoint.restore();
    if (status == TYPESET_OK) status = typeset_policy_restore(external, &chosen);
    if (status == TYPESET_OK) status = checkpoint.begin(composer);
    *cursor = start;
    if (status == TYPESET_OK) {
        // replay stable column endpoints; the shared scratch retains every selected metric and contribution.
        trial.replay = &trial.decisions[selected]; trial.prefix_count = 0;
        composer->policy_trial = true; composer->sheet_trial = &trial;
        status = paged_layout_columns(context, provider, cursor, constraints, probe, policy, assembly, reject_terminal, result);
    }
    if (context->checkpoint.view) {
        TypesetStatus restored = context->checkpoint.restore();
        if (restored != TYPESET_OK) status = restored;
    }
    bool reinsert = false;
    if (status == TYPESET_OK && !provisional) {
        TypesetPagePlan plan = {}; plan.scratch = pool; plan.provider = provider; plan.policy = external;
        plan.candidate = paged_sheet_candidate(trial.prefix, trial.regions, trial.prefix_count, composer->page_style.column_count);
        plan.contributions = trial.contributions; plan.count = trial.contribution_count; plan.complete = result->finished;
        composer->policy_trial = false; composer->sheet_trial = previous;
        status = typeset_page_assemble(external, &plan.candidate, &plan.action);
        if (status == TYPESET_OK) status = paged_assembly_resolve(composer, &plan, assembly, &reinsert);
        if (status == TYPESET_OK && reinsert) {
            TypesetPolicyCheckpoint progressed = {};
            status = typeset_policy_checkpoint(external, &progressed);
            if (status == TYPESET_OK) status = checkpoint.restore();
            if (status == TYPESET_OK) status = typeset_policy_restore(external, &progressed);
            *cursor = start; result->reinsert = true;
        } else if (status == TYPESET_OK) {
            if (external->committed) status = external->committed(external->context, &plan.candidate);
            if (status == TYPESET_OK) { composer->policy_done = true; assembly->count = 0; }
        }
    }
    composer->policy_trial = provisional; composer->sheet_trial = previous;
    if (status != TYPESET_OK) status = checkpoint.fail(status);
    else if (!reinsert) status = checkpoint.accept();
    for (PagedSheetRegionLease* lease = trial.leases; lease; lease = lease->next) typeset_region_plan_dispose(&lease->plan);
    mem_pool_destroy(pool);
    return status;
}

static TypesetStatus paged_policy_aux_sheet(PagedComposer* composer, TypesetPageAssembly* assembly, bool* finished, bool* reinsert) {
    PagedPageProvider* previous = composer->provider;
    PagedPageProvider context = {}; context.composer = composer; composer->provider = &context;
    TypesetFlowProvider provider = {composer->tree->model->tree_id, composer->tree->layout_generation,
        &context, paged_page_next, paged_flow_cursor_copy, paged_page_restore};
    TypesetResume cursor = {provider.identity, provider.generation, 0, {}};
    TypesetPagePolicy policy = {&context, paged_choose_page, paged_pack_page, paged_page_committed,
        paged_policy_checkpoint, paged_policy_restore, paged_page_transition};
    TypesetPageConstraints constraints = {INFINITY, composer->composition->options.max_items, composer->composition->options.max_items};
    TypesetPageProbe probe = {&context, paged_page_probe};
    PagedSheetResult result = {};
    TypesetStatus status = paged_layout_sheet(&context, &provider, &cursor, &constraints, &probe, &policy, assembly, false, &result);
    *finished = result.finished; *reinsert = result.reinsert;
    composer->provider = previous;
    return status;
}

static TypesetStatus paged_layout_pages(PagedComposer* composer, PagedFlowNode* root) {
    PagedCheckpoint document;
    TypesetStatus status = document.begin(composer);
    if (status != TYPESET_OK) return status;
    PagedPageProvider context = {}; context.composer = composer; composer->provider = &context;
    status = context.flatten(root);
    if (status == TYPESET_OK) status = paged_next_page(composer);
    TypesetFlowProvider provider = {composer->tree->model->tree_id, composer->tree->layout_generation,
        &context, paged_page_next, paged_flow_cursor_copy, paged_page_restore};
    TypesetResume cursor = {provider.identity, provider.generation, 0, {}};
    TypesetPagePolicy policy = {&context, paged_choose_page, paged_pack_page, paged_page_committed,
        paged_policy_checkpoint, paged_policy_restore, paged_page_transition};
    TypesetPageAssembly assembly = {};
    TypesetPageProbe probe = {&context, paged_page_probe};
    TypesetPageConstraints constraints = {INFINITY, composer->composition->options.max_items,
        composer->composition->options.max_items};
    bool provisional = composer->composition->options.page_policy != nullptr;
    while (status == TYPESET_OK && cursor.state[0] < context.count) {
        // leading sidedness sheets are physical inputs before the body trial and its reinsertion checkpoint.
        composer->policy_trial = false;
        for (size_t first = cursor.state[0]; status == TYPESET_OK && first < context.count; first++) {
            const PagedFlowStep& step = context.steps[first];
            if (step.kind == PAGED_STEP_BEFORE) status = paged_block_before(composer, step.flow, nullptr);
            else if (step.kind != PAGED_STEP_OPEN) break;
        }
        if (status != TYPESET_OK) break;
        PagedCheckpoint sheet;
        status = sheet.begin(composer);
        if (status != TYPESET_OK) break;
        TypesetResume start = cursor;
        composer->policy_trial = provisional;
        PagedSheetResult ordinary = {}, terminal = {};
        status = paged_layout_sheet(&context, &provider, &cursor, &constraints, &probe, &policy, &assembly, false, &ordinary);
        const RadiantPageSequence* sequence = ordinary.sequence;
        bool terminal_program = sequence && sequence->master_program && sequence->master_program->terminal;
        bool pad = terminal_program && paged_sequence_padding(composer->page, sequence->next);
        if ((status == TYPESET_OK || status == TYPESET_UNPLACEABLE) && terminal_program && !pad) {
            // a terminal master remeasures every column from the same immutable sheet input.
            if (context.checkpoint.view) status = context.checkpoint.restore();
            else status = TYPESET_OK;
            cursor = start;
            if (status == TYPESET_OK) status = paged_sheet_restyle(&sheet, sequence, composer->page->page_number, false, provisional);
            if (status == TYPESET_OK) status = paged_layout_sheet(&context, &provider, &cursor, &constraints, &probe,
                &policy, &assembly, false, &terminal);
            if (status == TYPESET_UNPLACEABLE || (status == TYPESET_OK && !terminal.finished)) {
                if (context.checkpoint.view) status = context.checkpoint.restore();
                else status = TYPESET_OK;
                cursor = start;
                if (status == TYPESET_OK) status = paged_sheet_restyle(&sheet, sequence, 0, ordinary.finished, provisional);
                if (status == TYPESET_OK) status = paged_layout_sheet(&context, &provider, &cursor, &constraints, &probe,
                    &policy, &assembly, ordinary.finished, &ordinary);
            }
        }
        if (status == TYPESET_OK && provisional) {
            uint32_t terminal_page = composer->terminal_page;
            bool retain_tail = composer->retain_aux_tail, reject_terminal = context.reject_terminal;
            if (context.checkpoint.view) status = context.checkpoint.restore();
            cursor = start;
            // assembly/commit callbacks see only the selected geometry, with the original input and policy state.
            if (status == TYPESET_OK) status = paged_sheet_restyle(&sheet, sequence, terminal_page, retain_tail, false);
            if (status == TYPESET_OK) status = paged_layout_sheet(&context, &provider, &cursor, &constraints, &probe,
                &policy, &assembly, reject_terminal, &ordinary);
            if (status == TYPESET_OK && ordinary.reinsert) {
                TypesetPolicyCheckpoint progressed = {};
                status = typeset_policy_checkpoint(&policy, &progressed);
                if (status == TYPESET_OK && context.checkpoint.view) status = context.checkpoint.restore();
                if (status == TYPESET_OK) status = sheet.restore();
                if (status == TYPESET_OK) status = typeset_policy_restore(&policy, &progressed);
                cursor = start;
                if (status == TYPESET_OK) continue;
            }
        }
        if (context.checkpoint.view) {
            TypesetStatus restored = context.checkpoint.restore();
            if (restored != TYPESET_OK) status = restored;
        }
        status = status == TYPESET_OK ? sheet.accept() : sheet.fail(status);
        if (status == TYPESET_OK && cursor.state[0] < context.count) {
            status = paged_next_page(composer); cursor.state[2] = 0;
        }
    }
    PagedLayoutDiagnostic failure = context.hard_failure.status != TYPESET_OK ? context.hard_failure : composer->composition->diagnostic;
    if (context.checkpoint.view) {
        TypesetStatus restored = context.checkpoint.restore();
        if (restored != TYPESET_OK) status = restored;
    }
    if (status == TYPESET_OK) return document.accept();
    composer->composition->diagnostic = failure;
    // trial rewind preserves a precise producer diagnostic; add the whole-page fallback only when absent.
    if (failure.status != status || !failure.reason)
        paged_failure(composer->composition, status, context.failed_source ? context.failed_source : root->source,
            composer->page ? composer->page->page_number : 0,
            context.failed_reason ? context.failed_reason : "whole-page flow could not be composed");
    return document.fail(status);
}

static TypesetStatus paged_fixed_flow(PagedComposer* composer, PagedFlowNode* flow, float* natural_height) {
    if (flow->kind == PAGED_FLOW_PARAGRAPH) {
        size_t capacity = flow->paragraph.count;
        if (!capacity) return TYPESET_OK;
        TypesetLineCandidate* scratch = (TypesetLineCandidate*)pool_alloc(composer->composition->pool,
            capacity * sizeof(TypesetLineCandidate));
        if (!scratch) return TYPESET_OUT_OF_MEMORY;
        TypesetStatus status = paged_space_flush(composer);
        for (size_t first = 0; status == TYPESET_OK && first < capacity;) {
            TypesetLineCandidate line = {};
            status = paged_next_line(composer, flow, first,
                composer->frames[composer->depth - 1].content_width, composer->frames[composer->depth - 1].content_height, scratch, capacity, &line,
                composer->frames[composer->depth - 1].replaced_height);
            if (status == TYPESET_OK && line.overflow && composer->role == VIEW_FRAGMENT_BODY)
                status = paged_failure(composer->composition, TYPESET_UNPLACEABLE, flow->source, 0,
                    "paragraph cannot make progress in a fresh page region");
            if (status == TYPESET_OK) status = paged_commit_line(composer, flow, line);
            first = line.next;
        }
        pool_free(composer->composition->pool, scratch);
        return status;
    }
    TypesetStatus status = composer->role == VIEW_FRAGMENT_BODY ? paged_block_trial_begin(composer, flow) :
        composer->depth >= composer->composition->options.max_depth ? TYPESET_BUDGET_EXHAUSTED : TYPESET_OK;
    float parent_width = composer->depth ? composer->frames[composer->depth - 1].content_width : composer->page_style.content_rect.width;
    if (status == TYPESET_OK) status = paged_block_enter(composer, flow);
    if (status == TYPESET_OK && flow->native) status = paged_native_fixed_flow(composer, flow, false);
    for (PagedFlowNode* child = flow->first_child; status == TYPESET_OK && child; child = child->next)
        status = paged_fixed_flow(composer, child);
    if (status != TYPESET_OK) return status;
    if (natural_height) {
        const PagedFrame& frame = composer->frames[composer->depth - 1];
        // specified cell heights expand the row, while alignment uses the actual content extent.
        *natural_height = composer->y - frame.page_start + frame.box.edges[2];
    }
    if (paged_block_remaining(composer) > 0.0f) {
        status = paged_space_flush(composer);
        if (status != TYPESET_OK) return status;
    }
    composer->y += paged_block_remaining(composer);
    return paged_block_finish(composer, parent_width);
}

static TypesetStatus paged_layout_continuous(PagedComposer* composer, PagedFlowNode* root) {
    PagedCheckpoint checkpoint;
    TypesetStatus status = checkpoint.begin(composer);
    if (status == TYPESET_OK) status = paged_fixed_flow(composer, root);
    if (status == TYPESET_OK) status = paged_space_flush(composer, true);
    return status == TYPESET_OK ? checkpoint.accept() : checkpoint.fail(status);
}

static float paged_margin_pair(PagedIntrinsic a, PagedIntrinsic b, float available) {
    if (!a.generated) return 0.0f;
    if (!b.generated) return available;
    float base_a = a.minimum, base_b = b.minimum;
    float factor_a = a.minimum, factor_b = b.minimum;
    if (a.maximum + b.maximum < available) {
        base_a = factor_a = a.maximum; base_b = factor_b = b.maximum;
    } else if (a.minimum + b.minimum < available) {
        factor_a = a.maximum - a.minimum; factor_b = b.maximum - b.minimum;
    }
    if (factor_a + factor_b == 0.0f) factor_a = factor_b = 1.0f;
    return base_a + (available - base_a - base_b) * factor_a / (factor_a + factor_b);
}

static TypesetStatus paged_margin_intrinsic(PagedComposer* composer, PagedFlowNode* flow,
        bool vertical, float fixed, PagedIntrinsic* result) {
    *result = {};
    if (!flow) return TYPESET_OK;
    result->generated = true;
    if (vertical) {
        if (flow->kind == PAGED_FLOW_BLOCK) {
            bool forced = false;
            TypesetStatus status = fixed > 0.0f ? paged_flow_height(composer, flow, fixed, paged_root_height(composer), false,
                &result->maximum, &forced, true) : TYPESET_OK;
            result->minimum = result->maximum;
            return status;
        }
        size_t capacity = flow->paragraph.count;
        if (!capacity) return TYPESET_OK;
        TypesetLineCandidate* scratch = (TypesetLineCandidate*)pool_alloc(composer->composition->pool, capacity * sizeof(TypesetLineCandidate));
        if (!scratch) return TYPESET_OUT_OF_MEMORY;
        size_t count = 0; float height = 0.0f;
        TypesetStatus status = fixed > 0.0f ? paged_measure_lines(composer, flow, 0, fixed, paged_root_height(composer), scratch, nullptr, capacity, &count, &height, true) : TYPESET_OK;
        pool_free(composer->composition->pool, scratch);
        if (status != TYPESET_OK) return status;
        result->minimum = result->maximum = height;
    } else return paged_flow_intrinsic(composer, flow, composer->page_style.content_rect.width, result);
    return TYPESET_OK;
}

static TypesetStatus paged_margin_group(PagedComposer* composer, PagedFlowNode** flows,
        const CssPageMarginBox boxes[3], RdtLogicalRect band, bool vertical, RdtLogicalRect* rects) {
    float available = vertical ? band.height : band.width, fixed = vertical ? band.width : band.height;
    PagedIntrinsic intrinsic[3] = {};
    for (size_t i = 0; i < 3; i++) {
        TypesetStatus status = paged_margin_intrinsic(composer, flows[boxes[i]], vertical, fixed, &intrinsic[i]);
        if (status != TYPESET_OK) return status;
    }
    float sizes[3] = {};
    if (intrinsic[1].generated) {
        PagedIntrinsic sides = {2.0f * fmaxf(intrinsic[0].minimum, intrinsic[2].minimum),
            2.0f * fmaxf(intrinsic[0].maximum, intrinsic[2].maximum), intrinsic[0].generated || intrinsic[2].generated};
        sizes[1] = paged_margin_pair(intrinsic[1], sides, available);
        sizes[0] = sizes[2] = (available - sizes[1]) * 0.5f;
    } else {
        sizes[0] = paged_margin_pair(intrinsic[0], intrinsic[2], available);
        sizes[2] = available - sizes[0];
    }
    // CSS Paged Media 3 section 5.3.2 centers B using the imaginary doubled AC box.
    for (size_t i = 0; i < 3; i++) {
        RdtLogicalRect rect = band;
        float offset = i == 0 ? 0.0f : i == 1 ? (available - sizes[i]) * 0.5f : available - sizes[i];
        if (vertical) { rect.y += offset; rect.height = sizes[i]; }
        else { rect.x += offset; rect.width = sizes[i]; }
        rects[boxes[i]] = rect;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_margin_layout(PagedComposer* composer, ViewPageBox* page) {
    ViewPageStyle* page_style = page->style;
    if (!page_style) return TYPESET_OK;
    composer->page_style = *page_style;
    PagedFlowNode* flows[CSS_PAGE_MARGIN_BOX_COUNT] = {};
    RdtLogicalRect rects[CSS_PAGE_MARGIN_BOX_COUNT] = {};
    ViewCssStyle* base = composer->composition->root->style;
    for (size_t i = 0; i < CSS_PAGE_MARGIN_BOX_COUNT; i++) {
        const CssDeclaration* declaration = page_style->margin_content[i];
        ViewCssStyle* context = page_style->margin_style[i];
        const CssValue* content = declaration ? view_css_resolve_value(composer->tree, context, declaration->value) : nullptr;
        if (!content || css_value_is_none(content) || (content->type == CSS_VALUE_TYPE_KEYWORD && content->data.keyword == CSS_VALUE_NORMAL)) continue;
        PagedContentBinding binding = {composer->tree, composer->composition, page, context};
        if (content->type == CSS_VALUE_TYPE_FUNCTION && css_function_name_is(content->data.function, "element")) {
            const TypesetMark* mark = nullptr;
            if (!paged_content_mark(&binding, content->data.function, TYPESET_MARK_RUNNING, &mark))
                return paged_failure(composer->composition, TYPESET_INVALID, base->source, page->page_number, "invalid running-element selection");
            if (mark) {
                if (!mark->value || mark->value->provider != composer->tree->model->tree_id) return TYPESET_STALE;
                flows[i] = ((const PagedRunningRecord*)mark->value.get())->flow;
            }
            continue;
        }
        char* owned = nullptr; size_t length = 0;
        TypesetStatus content_status = paged_content_text(composer->tree, composer->composition, context,
            page, content, 0, &owned, &length);
        if (content_status != TYPESET_OK) return paged_failure(composer->composition, content_status,
            base->source, page->page_number, "unresolved page-margin generated content");
        static const CssEnum alignments[CSS_PAGE_MARGIN_BOX_COUNT] = {
            CSS_VALUE_RIGHT, CSS_VALUE_LEFT, CSS_VALUE_CENTER, CSS_VALUE_RIGHT, CSS_VALUE_LEFT,
            CSS_VALUE_CENTER, CSS_VALUE_CENTER, CSS_VALUE_CENTER, CSS_VALUE_LEFT, CSS_VALUE_RIGHT,
            CSS_VALUE_CENTER, CSS_VALUE_LEFT, CSS_VALUE_RIGHT, CSS_VALUE_CENTER, CSS_VALUE_CENTER, CSS_VALUE_CENTER};
        CssEnum align = alignments[i];
        if (page_style->margin_align[i]) align = context->text_align;
        ViewCssStyle* style = view_css_generated_style(composer->tree, page_style->margin_style[i], nullptr, nullptr, align);
        if (!style) return TYPESET_OUT_OF_MEMORY;
        flows[i] = paged_flow_new(composer->composition, PAGED_FLOW_PARAGRAPH, base->source, style);
        if (!flows[i]) return TYPESET_OUT_OF_MEMORY;
        TypesetStatus status = paged_text_items(composer->tree, composer->composition, flows[i], base->source, owned, length, style, true);
        if (status != TYPESET_OK) return status;
    }
    float width = page_style->width, height = page_style->height;
    float top = page_style->margin[0], right = page_style->margin[1], bottom = page_style->margin[2], left = page_style->margin[3];
    rects[CSS_PAGE_TOP_LEFT_CORNER] = {0.0f, 0.0f, left, top};
    rects[CSS_PAGE_TOP_RIGHT_CORNER] = {width - right, 0.0f, right, top};
    rects[CSS_PAGE_BOTTOM_LEFT_CORNER] = {0.0f, height - bottom, left, bottom};
    rects[CSS_PAGE_BOTTOM_RIGHT_CORNER] = {width - right, height - bottom, right, bottom};
    static const CssPageMarginBox groups[4][3] = {
        {CSS_PAGE_TOP_LEFT, CSS_PAGE_TOP_CENTER, CSS_PAGE_TOP_RIGHT},
        {CSS_PAGE_BOTTOM_LEFT, CSS_PAGE_BOTTOM_CENTER, CSS_PAGE_BOTTOM_RIGHT},
        {CSS_PAGE_LEFT_TOP, CSS_PAGE_LEFT_MIDDLE, CSS_PAGE_LEFT_BOTTOM},
        {CSS_PAGE_RIGHT_TOP, CSS_PAGE_RIGHT_MIDDLE, CSS_PAGE_RIGHT_BOTTOM}};
    RdtLogicalRect bands[] = {{left, 0.0f, width - left - right, top}, {left, height - bottom, width - left - right, bottom},
        {0.0f, top, left, height - top - bottom}, {width - right, top, right, height - top - bottom}};
    for (size_t i = 0; i < 4; i++) {
        TypesetStatus status = paged_margin_group(composer, flows, groups[i], bands[i], i >= 2, rects);
        if (status != TYPESET_OK) return status;
    }
    for (size_t i = 0; i < CSS_PAGE_MARGIN_BOX_COUNT; i++) {
        PagedFlowNode* flow = flows[i];
        if (!flow) continue;
        RdtLogicalRect rect = rects[i];
        LayoutViewNode* box = view_tree_fragment_append(composer->tree, &page->node, base->source, rect);
        if (!box) return TYPESET_OUT_OF_MEMORY;
        box->role = VIEW_FRAGMENT_MARGIN; box->generated = true;
        box->computed_style = lam::up(flow->style); page->margin_boxes[i] = lam::up(box);
        const CssValue* overflow = page_style->margin_overflow[i] ? page_style->margin_overflow[i]->value : nullptr;
        box->clip_content = overflow && overflow->type == CSS_VALUE_TYPE_KEYWORD &&
            (overflow->data.keyword == CSS_VALUE_HIDDEN || overflow->data.keyword == CSS_VALUE_CLIP);
        if (rect.width <= 0.0f) continue;
        Pool* pool = composer->composition->pool;
        float content_height = 0.0f; bool forced = false;
        PagedComposer margin = {}; margin.tree = composer->tree; margin.composition = composer->composition;
        margin.page_style = *page_style; margin.page = page;
        margin.page_style.content_rect = rect;
        margin.role = flow->kind == PAGED_FLOW_BLOCK ? VIEW_FRAGMENT_RUNNING : VIEW_FRAGMENT_MARGIN;
        margin.depth = 1;
        if (!lam::pool_grow_array(pool, &margin.frames, &margin.frame_capacity, 1, 16)) return TYPESET_OUT_OF_MEMORY;
        margin.frames[0] = {}; margin.frames[0].flow = flow; margin.frames[0].fragment = box;
        margin.frames[0].content_x = rect.x; margin.frames[0].content_width = rect.width;
        margin.frames[0].content_height = rect.height;
        TypesetStatus status = paged_flow_height(&margin, flow, rect.width, rect.height, false, &content_height, &forced, true);
        margin.y = rect.y + (rect.height - content_height) * 0.5f;
        if (status == TYPESET_OK) status = paged_fixed_flow(&margin, flow);
        pool_free(pool, margin.frames);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_static_layout(PagedComposer* composer, ViewPageBox* page) {
    const RadiantPageSequence* sequence = page->sequence;
    for (size_t i = 0; i < RADIANT_REGION_EDGE_COUNT; i++) {
        const RadiantPageRegion* region = page->style->edge_regions[i];
        if (!region) continue;
        RdtLogicalRect rect = page->style->edge_rects[i];
        if (rect.width <= 0.0f || rect.height <= 0.0f) continue;
        ViewCssStyle* style = page->style->edge_style[i];
        LayoutViewNode* box = view_tree_fragment_append(composer->tree, &page->node, region->source.address, rect);
        if (!box) return TYPESET_OUT_OF_MEMORY;
        box->role = VIEW_FRAGMENT_STATIC; box->generated = box->paint_box = true; box->computed_style = lam::up(style);
        box->clip_content = page->style->edge_clip[i];
        page->static_boxes[i] = lam::up(box);
        PagedComposer furniture = {}; furniture.tree = composer->tree; furniture.composition = composer->composition;
        furniture.page = page; furniture.page_style = *page->style; furniture.role = VIEW_FRAGMENT_STATIC;
        const PagedBoxEdges& edges = page->style->edge_boxes[i];
        for (size_t j = 0; j < 4; j++) box->clip_inset[j] = edges.border.width.values[j];
        TypesetStatus status = paged_boundary_publish(composer->tree, box, edges, true, true);
        if (status != TYPESET_OK) return status;
        const RadiantPageFlowBinding* binding = sequence ? sequence->static_content : nullptr;
        while (binding && strcmp(binding->region_name, region->name)) binding = binding->next;
        if (!binding) continue;
        PagedMarkSource key = {}; key.source = binding->source.address;
        const PagedMarkSource* record = (const PagedMarkSource*)hashmap_get(composer->composition->mark_sources, &key);
        if (!record || !record->static_flow) return paged_failure(composer->composition, TYPESET_INVALID,
            binding->source.address, page->page_number, "static region has no prepared content flow");
        PagedFlowNode* flow = record->static_flow;
        float width = rect.width - edges.edges[1] - edges.edges[3];
        float height = rect.height - edges.edges[0] - edges.edges[2];
        if (width <= 0.0f || height < 0.0f) return TYPESET_UNPLACEABLE;
        Pool* pool = composer->composition->pool;
        if (!lam::pool_grow_array(pool, &furniture.frames, &furniture.frame_capacity, 1, 16)) return TYPESET_OUT_OF_MEMORY;
        furniture.depth = 1; furniture.frames[0] = {};
        furniture.frames[0].flow = flow; furniture.frames[0].fragment = box;
        furniture.frames[0].content_x = rect.x + edges.edges[3]; furniture.frames[0].content_width = width;
        furniture.frames[0].content_height = height;
        float used = 0.0f; bool forced = false;
        status = paged_flow_height(&furniture, flow, width, height, false, &used, &forced, true);
        furniture.y = rect.y + edges.edges[0];
        if (region->align != RADIANT_REGION_ALIGN_BEFORE)
            furniture.y += (height - used) * (region->align == RADIANT_REGION_ALIGN_CENTER ? 0.5f : 1.0f);
        if (status == TYPESET_OK) status = paged_fixed_flow(&furniture, flow);
        if (status == TYPESET_OK) status = paged_space_flush(&furniture, true);
        pool_free(pool, furniture.frames); pool_free(pool, furniture.spaces); pool_free(pool, furniture.space_anchors);
        if (status != TYPESET_OK) return paged_failure(composer->composition, status, binding->source.address,
            page->page_number, "static region content could not be composed");
    }
    return TYPESET_OK;
}

static TypesetStatus paged_page_counters(ViewTree* tree, PagedComposition* composition) {
    struct Contexts {
        CounterContext* entries[1 + CSS_PAGE_MARGIN_BOX_COUNT] = {};
        ~Contexts() { for (CounterContext* context : entries) counter_context_destroy(context); }
    } contexts;
    if (tree->model->page_count > INT_MAX) return TYPESET_BUDGET_EXHAUSTED;
    char total[64]; snprintf(total, sizeof(total), "pages %zu", tree->model->page_count);
    // Evaluate only finalized sheets: speculative page creation/restyling cannot advance these scopes.
    for (size_t page_index = 0; page_index < tree->model->page_count; page_index++) {
        ViewPageBox* sheet = tree->model->pages.get()[page_index];
        ViewPageStyle* page = sheet->style;
        for (size_t i = 0; i < 1 + CSS_PAGE_MARGIN_BOX_COUNT; i++) {
            ViewCssStyle* style = i ? page->margin_style[i - 1].get() : page->computed_style.get();
            if (!style) continue;
            CounterContext*& context = contexts.entries[i];
            if (!context) context = counter_context_create(tree->layout_pass_arena);
            if (!context) return TYPESET_OUT_OF_MEMORY;
            const CssValue* values[] = {style->counter_reset, style->counter_increment, style->counter_set};
            for (size_t operation = 0; operation < 3; operation++) {
                int increment = 0;
                if (!i && operation == 1 && !layout_counter_named_value(values[operation], "page", 1, &increment))
                    counter_increment(context, "page");
                TypesetStatus status = paged_counter_property(tree, composition, style, context, values[operation], operation, true);
                if (status != TYPESET_OK) return status;
            }
            if (!i && sheet->sequence) {
                char folio[64]; snprintf(folio, sizeof(folio), "page %u", sheet->folio);
                counter_set(context, folio);
            }
            // CSS Paged Media 3 section 6.1 makes pages read-only, including in margin scopes.
            counter_set(context, total);
            style->counters = lam::up(counter_snapshot_create(context, tree->model->css->pool));
            if (!style->counters) return TYPESET_OUT_OF_MEMORY;
        }
        StrBuf* label = strbuf_new();
        if (!label) return TYPESET_OUT_OF_MEMORY;
        bool formatted = sheet->sequence ? radiant_folio_append(&sheet->sequence->format, sheet->folio, label) :
            counter_snapshot_append(page->computed_style->counters, "page", nullptr, CSS_VALUE_DECIMAL, label);
        sheet->label = lam::up(formatted ? pool_dup_n(composition->pool, label->str, label->length) : nullptr);
        strbuf_free(label);
        if (!sheet->label) return TYPESET_OUT_OF_MEMORY;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_fixed_document_admit(ViewTree* tree, PagedComposition* composition,
        DomElement* source, DomNode* container, size_t depth) {
    if (source == container) return TYPESET_OK;
    if (depth > composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    if (radiant_page_control_hidden(source)) return TYPESET_OK;
    ViewCssStyle* style = view_css_resolve(tree, source);
    if (!style) return TYPESET_OUT_OF_MEMORY;
    if (style->display.outer == CSS_VALUE_NONE) return TYPESET_OK;
    bool ancestor = false;
    for (DomNode* parent = container->parent; parent; parent = parent->parent) if (parent == source) { ancestor = true; break; }
    if (!ancestor) return paged_failure(composition, TYPESET_INVALID, source, 0, "fixed pages cannot mix with visible flowing content");
    for (DomNode* child = source->first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            TypesetStatus status = paged_fixed_document_admit(tree, composition, child->as_element(), container, depth + 1);
            if (status != TYPESET_OK) return status;
        } else if (child->is_text() && !radiant_page_whitespace(child->as_text()))
            return paged_failure(composition, TYPESET_INVALID, child, 0, "fixed pages cannot mix with visible flowing text");
    }
    return TYPESET_OK;
}

static TypesetStatus paged_layout_fixed_pages(ViewTree* tree, PagedComposition* composition, ViewCssStyle* root_style) {
    const RadiantPageDocument* program = tree->model->css->page_document;
    if (tree->model->environment.presentation != VIEW_PRESENTATION_PAGED)
        return paged_failure(composition, TYPESET_INVALID, program->fixed_source.address, 0, "fixed page controls require paged presentation");
    if (program->fixed_count > composition->options.max_pages)
        return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, program->fixed_source.address, 0, "fixed page sequence exceeds its page budget");
    TypesetStatus status = paged_fixed_document_admit(tree, composition, tree->model->document->root, program->fixed_source.address, 0);
    if (status != TYPESET_OK) return status;
    composition->root = paged_flow_new(composition, PAGED_FLOW_BLOCK, tree->model->document->root, root_style);
    if (!composition->root) return TYPESET_OUT_OF_MEMORY;
    PagedComposer edition_composer = {}; edition_composer.tree = tree; edition_composer.composition = composition;
    PagedCheckpoint edition;
    status = edition.begin(&edition_composer);
    for (const RadiantFixedPage* fixed = program->fixed_pages; status == TYPESET_OK && fixed; fixed = fixed->next) {
        DomElement* source = fixed->source.address->as_element();
        ViewCssStyle* style = view_css_resolve(tree, source);
        if (!style) { status = TYPESET_OUT_OF_MEMORY; break; }
        PagedCounterScope scope(tree, composition, style);
        status = scope.status;
        if (status == TYPESET_OK) status = paged_mark_register(composition, style);
        PagedFlowNode* flow = status == TYPESET_OK ? paged_flow_new(composition, PAGED_FLOW_BLOCK, source, style) : nullptr;
        if (status == TYPESET_OK && !flow) status = TYPESET_OUT_OF_MEMORY;
        if (status != TYPESET_OK) break;
        paged_flow_link(composition->root, flow);
        PagedFlowNode* paragraph = nullptr;
        // fixed pages use common atomic producers; insertions and nested page programs are inadmissible.
        status = paged_build_children(tree, composition, source, flow, &paragraph, 0, true);
        if (status != TYPESET_OK) break;
        PagedComposer composer = {}; composer.tree = tree; composer.composition = composition; composer.atomic_fragment = true;
        composer.page_style.width = fixed->viewport.width; composer.page_style.height = fixed->viewport.height;
        composer.page_style.content_rect = fixed->viewport;
        composer.initial_containing_block = fixed->viewport; composer.bottom = INFINITY;
        composer.page = view_tree_page_append(tree, fixed->viewport.width, fixed->viewport.height, fixed->viewport,
            paged_side(&composer, tree->model->page_count));
        if (!composer.page) { status = TYPESET_OUT_OF_MEMORY; break; }
        composer.page->fixed = lam::up(fixed);
        composer.page->folio = fixed->geometry.source_page;
        composer.page->sequence_page = composer.page->page_number;
        if (fixed->label) composer.page->label = lam::up(fixed->label);
        else {
            char label[32]; snprintf(label, sizeof(label), "%u", fixed->geometry.source_page);
            composer.page->label = lam::up(pool_strdup(composition->pool, label));
        }
        if (!composer.page->label) { status = TYPESET_OUT_OF_MEMORY; break; }
        status = paged_fixed_flow(&composer, flow);
        if (status == TYPESET_OK) status = paged_space_flush(&composer, true);
        if (status == TYPESET_OK) status = paged_page_policy_finish(&composer);
    }
    if (status != TYPESET_OK && composition->diagnostic.status == TYPESET_OK)
        paged_failure(composition, status, program->fixed_source.address, 0, "fixed page content could not be composed");
    return status == TYPESET_OK ? edition.accept() : edition.view ? edition.fail(status) : status;
}

static TypesetStatus paged_layout_pass(ViewTree* tree, const PagedLayoutOptions* options,
                                     PagedReferenceSession* references) {
    PagedComposition* composition = (PagedComposition*)pool_calloc(tree->prop_pool, sizeof(PagedComposition));
    if (!composition) return TYPESET_OUT_OF_MEMORY;
    tree->model->composition = lam::own(composition);
    composition->document = tree->model->document;
    composition->options = *options;
    composition->references = references;
    composition->pool = mem_pool_create((MemContext*)tree->model->document->services.mem_ctx,
                                        MEM_ROLE_LAYOUT, "view_tree.secondary.composition");
    if (!composition->pool) return TYPESET_OUT_OF_MEMORY;
    composition->mark_sources = lam::own(paged_mark_sources_new(16));
    if (!composition->mark_sources) return TYPESET_OUT_OF_MEMORY;
    composition->marks = {tree->model->tree_id, tree->layout_generation, composition->pool, nullptr, 0, 0};
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) composition->queues[i] = {composition->pool,
        (MemContext*)tree->model->document->services.mem_ctx, 0, nullptr, 0, 0, options->max_nodes};
    DomElement* root = tree->model->document->root;
    ViewCssStyle* style = view_css_resolve(tree, root);
    if (!style) return TYPESET_OUT_OF_MEMORY;
    const RadiantPageDiagnostic& page_diagnostic = tree->model->css->page_document->diagnostic;
    if (page_diagnostic.status != VIEW_MODEL_OK) return paged_failure(composition,
        page_diagnostic.status == VIEW_MODEL_OUT_OF_MEMORY ? TYPESET_OUT_OF_MEMORY : TYPESET_INVALID,
        page_diagnostic.source.address, 0, page_diagnostic.reason);
    composition->counters = counter_context_create(tree->layout_pass_arena);
    if (!composition->counters) return TYPESET_OUT_OF_MEMORY;
    PagedCounterScope root_scope(tree, composition, style);
    if (root_scope.status != TYPESET_OK) return root_scope.status;
    TypesetStatus mark_status = paged_mark_register(composition, style);
    if (mark_status != TYPESET_OK) return mark_status;
    TypesetStatus native_status = paged_native_bindings(tree, composition);
    if (native_status != TYPESET_OK) return native_status;
    if (tree->model->css->page_document->fixed_count) return paged_layout_fixed_pages(tree, composition, style);
    composition->root = paged_flow_new(composition, PAGED_FLOW_BLOCK, root, style);
    if (!composition->root) return TYPESET_OUT_OF_MEMORY;
    PagedFlowNode* paragraph = nullptr;
    TypesetStatus status = paged_build_children(tree, composition, root, composition->root, &paragraph, 0);
    for (PagedNativeFlow* flow = composition->native_flows; status == TYPESET_OK && flow; flow = flow->next)
        if (flow->binding.control.address && !flow->used) status = paged_failure(composition, TYPESET_INVALID, flow->binding.control.address, 0,
            "native flow control was not admitted to body or static composition");
    PagedComposer composer = {}; composer.tree = tree; composer.composition = composition;
    PagedCheckpoint edition;
    if (status == TYPESET_OK) status = edition.begin(&composer);
    if (status == TYPESET_OK && tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) status = paged_page_create(&composer, false);
    if (status == TYPESET_OK) status = tree->model->environment.presentation == VIEW_PRESENTATION_PAGED ?
        paged_layout_pages(&composer, composition->root) : paged_layout_continuous(&composer, composition->root);
    if (status == TYPESET_OK && !composer.page && tree->model->environment.presentation == VIEW_PRESENTATION_PAGED) status = paged_next_page(&composer);
    if (status == TYPESET_OK) status = paged_regions_close(&composer);
    if (status == TYPESET_OK) status = paged_regions_drain(&composer);
    if (status == TYPESET_OK) status = paged_sequence_end(&composer, nullptr);
    for (size_t i = 0; status == TYPESET_OK && i < tree->model->page_count; i++) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        const RadiantPageSequence* sequence = page->sequence;
        if (!sequence || !sequence->master_program) continue;
        bool terminal = i + 1 == tree->model->page_count || tree->model->pages.get()[i + 1]->sequence.get() != sequence;
        const RadiantPageRule* selected = radiant_page_master_select(sequence, page->sequence_page, page->folio, page->blank, terminal);
        // provisional last/only fallback may seed measurement, but never publish an ineligible page.
        if (!selected || strcmp(selected->name, page->name)) status = paged_failure(composition, TYPESET_UNPLACEABLE,
            sequence->source.address, page->page_number, "sequence material cannot satisfy its terminal master alternatives");
    }
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) typeset_region_plan_dispose(&composer.regions[i].plan);
    if (status == TYPESET_OK) status = paged_page_counters(tree, composition);
    for (size_t i = 0; status == TYPESET_OK && i < tree->model->page_count; i++) {
        status = paged_margin_layout(&composer, tree->model->pages.get()[i]);
        if (status == TYPESET_OK) status = paged_static_layout(&composer, tree->model->pages.get()[i]);
    }
    if (status == TYPESET_OK) {
        if (tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
            tree->model->root->rect = {0.0f, 0.0f, tree->model->environment.viewport_width, composer.y};
        }
    }
    if (status != TYPESET_OK && composition->diagnostic.status == TYPESET_OK) paged_failure(composition, status, root,
        composer.page ? composer.page->page_number : 0, "secondary flow could not be composed");
    // source admission can fail before the edition checkpoint exists.
    return status == TYPESET_OK ? edition.accept() : edition.view ? edition.fail(status) : status;
}

static uint32_t paged_target_page(ViewTree* tree, DomElement* source, bool last = false) {
    PagedMarkSource key = {}; key.source = source;
    PagedMarkSource* mark = (PagedMarkSource*)hashmap_get(tree->model->composition->mark_sources, &key);
    if (mark && mark->target_page) return last ? mark->last_target_page : mark->target_page;
    ViewNodeState* state = view_tree_node_state(tree, source, false);
    // Extracted notes/floats bind to their placed region, not their body anchor.
    ViewCssStyle* style = state ? state->computed_style.get() : nullptr;
    ViewFragmentRole role = style && style->float_value == CSS_VALUE_FOOTNOTE ? VIEW_FRAGMENT_NOTE : VIEW_FRAGMENT_FLOAT;
    uint32_t result = 0;
    for (LayoutViewNode* node = state ? state->first_occurrence.get() : nullptr; node; node = node->next_occurrence) {
        if (node->role != role || !node->paint_box) continue;
        LayoutViewNode* page = node->parent;
        while (page && page->kind != LAYOUT_VIEW_PAGE) page = page->parent;
        if (page) { result = ((ViewPageBox*)page)->page_number; if (!last) break; }
    }
    return result;
}

static void paged_target_area_range(uint32_t (*pages)[2], uint32_t page, bool normal, bool blank) {
    if (!page) return;
    for (size_t area = 0; area < 3; area++) {
        if ((area == RADIANT_QUERY_NORMAL && !normal) || (area == RADIANT_QUERY_NON_BLANK && blank)) continue;
        if (!pages[area][0] || page < pages[area][0]) pages[area][0] = page;
        if (page > pages[area][1]) pages[area][1] = page;
    }
}

static void paged_target_area_enter(PagedComposition* composition, DomNode* source,
        uint32_t page, bool normal, bool blank) {
    for (DomNode* node = source; node; node = node->parent) {
        PagedMarkSource key = {}; key.source = node;
        PagedMarkSource* mark = (PagedMarkSource*)hashmap_get(composition->mark_sources, &key);
        if (!mark) continue;
        paged_target_area_range(mark->generated_pages, page, normal, blank);
    }
}

static void paged_native_target_areas(ViewTree* tree, TypesetTarget* target, PagedTargetValue* value) {
    const ViewNodeState* state = view_tree_native_state(tree, &target->source);
    bool placed = false;
    for (LayoutViewNode* node = state ? state->first_occurrence.get() : nullptr; node; node = node->next_occurrence) {
        LayoutViewNode* parent = node->parent;
        while (parent && parent->kind != LAYOUT_VIEW_PAGE) parent = parent->parent;
        if (!parent) continue;
        const ViewPageBox* page = (const ViewPageBox*)parent;
        bool normal = node->role == VIEW_FRAGMENT_BODY || node->role == VIEW_FRAGMENT_REPEATED_TABLE || node->role == VIEW_FRAGMENT_STATIC;
        paged_target_area_range(value->pages, page->page_number, normal, page->blank); placed = true;
    }
    if (!placed) {
        // explicit zero-size events bind anchors without manufacturing paint fragments.
        paged_target_area_range(value->pages, target->page_number, true, false);
        paged_target_area_range(value->pages, target->last_page_number, true, false);
    }
    target->page_number = value->pages[RADIANT_QUERY_ALL][0];
    target->last_page_number = value->pages[RADIANT_QUERY_ALL][1];
}

static void paged_target_areas_capture(ViewTree* tree) {
    PagedComposition* composition = tree->model->composition;
    // aggregate committed occurrences once; descendants and repeated/out-of-line areas retain their source ancestry.
    for (size_t i = 0; i < tree->model->node_id_count; i++) {
        LayoutViewNode* node = tree->model->nodes.get()[i];
        if (!node || node->kind != LAYOUT_VIEW_FRAGMENT || (!node->paint_box && !node->glyph_run && !node->image_box)) continue;
        if (!paged_source_has_own_area(node->computed_style.get())) continue;
        LayoutViewNode* parent = node->parent;
        while (parent && parent->kind != LAYOUT_VIEW_PAGE) parent = parent->parent;
        if (!parent) continue;
        bool viewport = node->source.address && node->source.address->is_element() &&
            radiant_page_element(node->source.address->as_element(), "region");
        bool normal = node->role == VIEW_FRAGMENT_BODY || node->role == VIEW_FRAGMENT_REPEATED_TABLE ||
            (node->role == VIEW_FRAGMENT_STATIC && !viewport);
        DomNode* area_source = node->source.address;
        if (!area_source && node->native_material) {
            // real page-control ancestors own the native body's area; native source identity remains independent.
            for (LayoutViewNode* owner = node->parent; owner && owner->kind != LAYOUT_VIEW_PAGE; owner = owner->parent)
                if (owner->source.address) { area_source = owner->source.address; break; }
        }
        paged_target_area_enter(composition, area_source, ((ViewPageBox*)parent)->page_number, normal, false);
    }
    for (size_t i = 0; i < tree->model->page_count; i++) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        if (page->blank && page->sequence)
            paged_target_area_enter(composition, page->sequence->source.address, page->page_number, false, true);
    }
}

static TypesetStatus paged_targets_capture(ViewTree* tree, PagedReferenceSession* session) {
    paged_target_areas_capture(tree);
    session->page_label_count = tree->model->page_count;
    session->page_labels = (const char**)pool_calloc(session->pool, session->page_label_count * sizeof(const char*));
    if (session->page_label_count && !session->page_labels) return TYPESET_OUT_OF_MEMORY;
    for (size_t i = 0; i < session->page_label_count; i++) {
        session->page_labels[i] = pool_strdup(session->pool, tree->model->pages.get()[i]->label);
        if (!session->page_labels[i]) return TYPESET_OUT_OF_MEMORY;
    }
    TypesetTargetStore next = {tree->model->tree_id, tree->layout_generation, session->pool,
        nullptr, 0, 0, session->targets.limit, nullptr, session->targets.sources};
    TypesetStatus status = TYPESET_OK;
    for (size_t i = 0; status == TYPESET_OK && i < session->targets.count; i++) {
        const TypesetTarget& previous = session->targets.entries[i];
        const PagedTargetValue* old = (const PagedTargetValue*)previous.binding.get();
        DomNode* node = old->source.address ? dom_node_ref_validate(tree->model->document, old->source) : nullptr;
        if (old->source.address && (!node || !node->is_element())) { status = TYPESET_STALE; break; }
        DomElement* source = node ? node->as_element() : nullptr;
        ViewCssStyle* style = source ? view_css_resolve(tree, source) : nullptr;
        PagedTargetValue* value = (PagedTargetValue*)pool_calloc(session->pool, sizeof(PagedTargetValue));
        if ((source && !style) || !value) { status = TYPESET_OUT_OF_MEMORY; break; }
        value->provider = tree->model->tree_id; value->source = old->source; value->text = old->text;
        value->counters = counter_snapshot_copy(style ? style->counters.get() : old->counters, session->pool);
        if (!value->counters) { status = TYPESET_OUT_OF_MEMORY; break; }
        const uint8_t pseudos[] = {PSEUDO_ELEMENT_BEFORE, PSEUDO_ELEMENT_AFTER};
        const char** parts[] = {&value->before, &value->after};
        for (size_t j = 0; j < 2; j++) {
            ViewCssStyle* pseudo = source ? view_css_resolve_pseudo(tree, source, pseudos[j]) : nullptr;
            if (source && !pseudo) { status = TYPESET_OUT_OF_MEMORY; break; }
            *parts[j] = pool_strdup(session->pool, pseudo && pseudo->generated_text ? pseudo->generated_text.get() : "");
            if (!*parts[j]) { status = TYPESET_OUT_OF_MEMORY; break; }
        }
        if (status != TYPESET_OK) break;
        const TypesetTarget* placed = source ? nullptr : typeset_target_find(&tree->model->composition->native_targets, previous.name);
        if (!source && !placed) { status = TYPESET_STALE; break; }
        TypesetTarget target = previous;
        target.page_number = source ? paged_target_page(tree, source) : placed->page_number;
        target.last_page_number = source ? paged_target_page(tree, source, true) : placed->last_page_number;
        target.binding = lam::up((const TypesetRecord*)value);
        if (source) {
            // DOM sources follow the rebuilt edition; native provider generations remain unchanged.
            target.source.generation = tree->layout_generation; target.value = target.binding;
        } else paged_native_target_areas(tree, &target, value);
        PagedMarkSource key = {}; key.source = source;
        const PagedMarkSource* mark = source ? (const PagedMarkSource*)hashmap_get(tree->model->composition->mark_sources, &key) : nullptr;
        for (size_t area = 0; area < 3; area++) for (size_t edge = 0; edge < 2; edge++) {
            uint32_t number = source ? mark ? mark->generated_pages[area][edge] : 0 : value->pages[area][edge];
            value->pages[area][edge] = number;
            if (number) {
                value->labels[area][edge] = pool_strdup(session->pool, tree->model->pages.get()[number - 1]->label);
                if (!value->labels[area][edge]) { status = TYPESET_OUT_OF_MEMORY; break; }
            }
        }
        if (status != TYPESET_OK) break;
        const ViewPageBox* page = target.page_number ? tree->model->pages.get()[target.page_number - 1] : nullptr;
        value->page_counters = counter_snapshot_copy(page ? page->style->computed_style->counters.get() : nullptr, session->pool);
        if (!value->page_counters) { status = TYPESET_OUT_OF_MEMORY; break; }
        status = typeset_target_append(&next, &target);
    }
    if (status == TYPESET_OK) {
        typeset_targets_dispose(&session->targets);
        session->targets = next;
        session->page_count = tree->model->page_count;
    } else typeset_targets_dispose(&next);
    return status;
}

template<typename T> static void paged_signature_value(StrBuf* signature, const T& value) {
    strbuf_append_str_n(signature, (const char*)&value, sizeof(T));
}
static void paged_signature_text(StrBuf* signature, const char* text, size_t length) {
    paged_signature_value(signature, length);
    if (length) strbuf_append_str_n(signature, text, length);
}
static void paged_signature_text(StrBuf* signature, const char* text) {
    paged_signature_text(signature, text, text ? strlen(text) : 0);
}
static void paged_signature_rect(StrBuf* signature, RdtLogicalRect rect) {
    paged_signature_value(signature, rect.x); paged_signature_value(signature, rect.y);
    paged_signature_value(signature, rect.width); paged_signature_value(signature, rect.height);
}
static void paged_signature_counters(StrBuf* signature, const CounterSnapshot* snapshot) {
    paged_signature_value(signature, snapshot ? snapshot->count : 0);
    if (snapshot) for (size_t i = 0; i < snapshot->count; i++) {
        paged_signature_text(signature, snapshot->entries[i].name);
        paged_signature_value(signature, snapshot->entries[i].value);
    }
}
static TypesetStatus paged_reference_observe(ViewTree* tree, PagedReferenceSession* session, bool* settled) {
    StrBuf* signature = strbuf_new();
    if (!signature) return TYPESET_OUT_OF_MEMORY;
    paged_signature_value(signature, tree->model->page_count);
    for (size_t i = 0; i < tree->model->page_count; i++) {
        ViewPageBox* page = tree->model->pages.get()[i];
        paged_signature_rect(signature, page->content_rect);
        paged_signature_value(signature, page->side); paged_signature_value(signature, page->blank);
        paged_signature_value(signature, page->sequence ? page->sequence->source.expected_id : 0);
        paged_signature_value(signature, page->sequence_page); paged_signature_value(signature, page->folio);
        paged_signature_text(signature, page->name);
        paged_signature_counters(signature, page->style->computed_style->counters);
    }
    paged_signature_value(signature, tree->model->node_count);
    for (size_t i = 0; i < tree->model->node_id_count; i++) {
        LayoutViewNode* node = tree->model->nodes.get()[i];
        if (!node) continue;
        paged_signature_value(signature, node->source.expected_id); paged_signature_value(signature, node->role);
        paged_signature_rect(signature, node->rect);
        paged_signature_value(signature, node->text_start); paged_signature_value(signature, node->text_length);
        paged_signature_value(signature, node->native_material != nullptr);
        if (node->native_material) {
            const ViewNativeMaterial& material = *node->native_material;
            paged_signature_value(signature, material.source.provider); paged_signature_value(signature, material.source.generation);
            paged_signature_value(signature, material.source.node); paged_signature_value(signature, material.source.offset_unit);
            paged_signature_value(signature, material.metrics.advance); paged_signature_value(signature, material.metrics.height);
            paged_signature_value(signature, material.metrics.depth); paged_signature_value(signature, material.metrics.baseline);
            paged_signature_rect(signature, material.metrics.ink);
        }
        paged_signature_value(signature, node->first_fragment); paged_signature_value(signature, node->last_fragment);
        if (node->image_box) {
            const PaintImageBox* box = node->image_box;
            paged_signature_rect(signature, {box->content_rect.x, box->content_rect.y, box->content_rect.width, box->content_rect.height});
            paged_signature_rect(signature, {box->image_rect.x, box->image_rect.y, box->image_rect.width, box->image_rect.height});
        }
        const PaintGlyphRun* run = node->glyph_run;
        paged_signature_text(signature, run ? run->text.get() : nullptr, run ? (size_t)run->text_len : 0);
    }
    paged_signature_value(signature, session->targets.count);
    for (size_t i = 0; i < session->targets.count; i++) {
        const TypesetTarget& target = session->targets.entries[i];
        const PagedTargetValue* value = (const PagedTargetValue*)target.binding.get();
        paged_signature_text(signature, target.name); paged_signature_value(signature, target.page_number);
        paged_signature_value(signature, target.last_page_number);
        if (!value->source.address) {
            paged_signature_value(signature, target.source.provider); paged_signature_value(signature, target.source.generation);
            paged_signature_value(signature, target.source.node); paged_signature_value(signature, target.source.offset_unit);
        }
        paged_signature_text(signature, value->text); paged_signature_text(signature, value->before);
        paged_signature_text(signature, value->after);
        paged_signature_counters(signature, value->counters);
        paged_signature_counters(signature, value->page_counters);
        for (size_t area = 0; area < 3; area++) for (size_t edge = 0; edge < 2; edge++) {
            paged_signature_value(signature, value->pages[area][edge]);
            paged_signature_text(signature, value->labels[area][edge]);
        }
    }
    const TypesetMarkStore& marks = tree->model->composition->marks;
    paged_signature_value(signature, marks.count);
    for (size_t i = 0; i < marks.count; i++) {
        const TypesetMark& mark = marks.entries[i];
        paged_signature_value(signature, mark.kind); paged_signature_text(signature, mark.name);
        // a rebuilt DOM edition changes its layout epoch; producer generations remain semantic input.
        uint64_t generation = mark.source.provider == marks.provider && mark.source.generation == marks.generation ?
            0 : mark.source.generation;
        paged_signature_value(signature, mark.source.provider); paged_signature_value(signature, generation);
        paged_signature_value(signature, mark.source.node); paged_signature_value(signature, mark.source.offset_unit);
        paged_signature_value(signature, mark.page_number); paged_signature_value(signature, mark.at_page_start);
        paged_signature_text(signature, mark.text);
    }
    TypesetStatus status = typeset_convergence_observe(&session->convergence, signature->str, signature->length, settled);
    strbuf_free(signature);
    return status;
}

TypesetStatus layout_secondary_view(ViewTree* tree, const PagedLayoutOptions* options,
                                    PagedLayoutDiagnostic* diagnostic) {
    if (diagnostic) *diagnostic = {};
    if (!tree || !tree->model || !options || (options->native_flow_count && !options->native_flows) ||
        (options->page_policy && (!options->page_policy->choose || !options->page_policy->assemble ||
            (!!options->page_policy->checkpoint != !!options->page_policy->restore) ||
            ((options->page_policy->committed || options->page_policy->transition) && !options->page_policy->checkpoint))) ||
        !options->max_pages || !options->max_nodes ||
        !options->max_items || !options->max_block_trials || !options->max_depth || !options->max_reference_passes ||
        !options->max_container_passes ||
        options->first_side > VIEW_PAGE_RIGHT || tree->model->committed) return TYPESET_INVALID;
    if (!view_tree_model_source_valid(tree)) return TYPESET_STALE;
    if (tree->model->composition || tree->model->node_count != 1) return TYPESET_INVALID;
    PagedReferenceSession session = {};
    session.pool = mem_pool_create((MemContext*)tree->model->document->services.mem_ctx,
        MEM_ROLE_LAYOUT, "view_tree.secondary.references");
    if (!session.pool) return TYPESET_OUT_OF_MEMORY;
    session.targets = {tree->model->tree_id, tree->layout_generation, session.pool,
        nullptr, 0, 0, options->max_nodes, nullptr};
    session.convergence.pool = session.pool; session.convergence.limit = options->max_reference_passes;
    TypesetPolicyCheckpoint policy_checkpoint = {};
    const TypesetPagePolicy* policy = options->page_policy;
    bool policy_saved = policy && policy->checkpoint;
    TypesetStatus status = typeset_policy_checkpoint(policy, &policy_checkpoint);
    if (status != TYPESET_OK) return status;
    size_t visited = 0;
    status = paged_targets_seed(tree, &session, tree->model->document->root, 0, &visited, options);
    if (status == TYPESET_OK) status = paged_native_targets_seed(tree, &session, *options);
    bool settled = false;
    uint32_t container_passes = 0;
    while (status == TYPESET_OK && !settled) {
        session.pass++;
        if (!view_css_container_pass_begin(tree)) { status = TYPESET_OUT_OF_MEMORY; break; }
        status = paged_layout_pass(tree, options, &session);
        PagedComposition* composition = tree->model->composition;
        if (status == TYPESET_OK) {
            status = paged_targets_capture(tree, &session);
            bool containers_settled = false;
            if (status == TYPESET_OK && !view_css_container_pass_end(tree, &containers_settled)) status = TYPESET_OUT_OF_MEMORY;
            if (status == TYPESET_OK && !containers_settled) {
                if (++container_passes >= options->max_container_passes)
                    status = paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, nullptr, 0,
                        "container styles exhausted their condition-pass budget");
            } else if (status == TYPESET_OK && composition->reference_used)
                status = paged_reference_observe(tree, &session, &settled);
            else if (status == TYPESET_OK) settled = true;
            if (status != TYPESET_OK) paged_failure(composition, status, composition->reference_source.address, 0,
                status == TYPESET_NO_PROGRESS ? "reference pagination repeated a nonconverged state" :
                status == TYPESET_BUDGET_EXHAUSTED ? "reference pagination exhausted its pass budget" : "target bindings could not be retained");
        }
        if (composition) {
            composition->diagnostic.reference_passes = session.pass;
            composition->diagnostic.container_passes = container_passes;
            if (diagnostic) *diagnostic = composition->diagnostic;
            composition->references = nullptr;
        }
        if (status == TYPESET_OK && !settled) {
            // provisional page callbacks rewind with a rejected reference pass (D4.5.1v4).
            if (policy_saved) status = typeset_policy_restore(policy, &policy_checkpoint);
            if (status == TYPESET_OK && !view_css_container_pass_reset(tree)) status = TYPESET_OUT_OF_MEMORY;
        }
    }
    if (status == TYPESET_OK && session.targets.count) {
        // Settled anchor bindings belong to this view generation, alongside its fragments.
        tree->model->composition->targets = session.targets;
        tree->model->composition->reference_pool = session.pool;
        session.targets = {}; session.pool = nullptr;
    }
    if (status == TYPESET_OK) status = view_tree_model_commit(tree) ? TYPESET_OK : TYPESET_INVALID;
    if (status != TYPESET_OK && policy_saved) {
        TypesetStatus restored = typeset_policy_restore(policy, &policy_checkpoint);
        if (restored != TYPESET_OK) status = restored;
    }
    if (diagnostic && diagnostic->status == TYPESET_OK) diagnostic->status = status;
    return status;
}

const TypesetTarget* layout_secondary_target(ViewTree* tree, const char* id) {
    if (!view_tree_model_source_valid(tree) || !tree->model->committed) return nullptr;
    if (tree->model->page_instances) tree = view_tree_page_content_owner(tree);
    if (!tree || !tree->model->composition) return nullptr;
    return typeset_target_find(&tree->model->composition->targets, id);
}

const TypesetMark* layout_secondary_mark(ViewTree* tree, TypesetMarkKind kind, const char* name,
        uint32_t page_number, TypesetMarkSelection selection) {
    if (!view_tree_model_source_valid(tree) || !tree->model->committed) return nullptr;
    if (tree->model->page_instances) tree = view_tree_page_content_owner(tree);
    if (!tree || !tree->model->composition) return nullptr;
    return typeset_mark_select(&tree->model->composition->marks, kind, name, page_number, selection);
}

static void paged_paint_table_background(LayoutViewNode* node, PaintList* paint, Color color,
        const ViewTableRange* columns = nullptr) {
    for (LayoutViewNode* child = node->first_child; child; child = child->next_sibling) {
        if (!child->computed_style) continue;
        if (child->computed_style->display.inner == CSS_VALUE_TABLE_CELL) {
            // grid identity survives subpixel tracks, float rounding and retained-source mutations.
            const ViewTableRange* cell = child->table_range;
            // missing-cell fixup suppresses every cell background layer (CSS Tables 3 §5.3.2).
            if (cell && cell->missing) continue;
            if (!columns || (cell && cell->column >= columns->column && cell->column - columns->column < columns->span))
                paint_fill_rect(paint, child->rect.x, child->rect.y, child->rect.width, child->rect.height, color);
        } else if (child->computed_style->display.inner == CSS_VALUE_TABLE_ROW ||
            layout_display_is_table_row_group(child->computed_style->display.inner))
            paged_paint_table_background(child, paint, color, columns);
    }
}

static void paged_paint_column_backgrounds(LayoutViewNode* table, LayoutViewNode* parent, PaintList* paint, bool groups) {
    for (LayoutViewNode* column = parent->first_child; column && paged_is_column_box(column); column = column->next_sibling) {
        ViewCssStyle* style = column->computed_style;
        if ((style->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP) == groups && style->background.a)
            paged_paint_table_background(table, paint, style->background, column->table_range);
        paged_paint_column_backgrounds(table, column, paint, groups);
    }
}

static bool paged_paint_clip(PaintList* paint, const RdtLogicalRect& rect, const float* inset = nullptr) {
    RdtPath* clip = rdt_path_new();
    if (!clip) return false;
    float top = inset ? inset[0] : 0.0f, right = inset ? inset[1] : 0.0f;
    float bottom = inset ? inset[2] : 0.0f, left = inset ? inset[3] : 0.0f;
    rdt_path_add_rect(clip, rect.x + left, rect.y + top, fmaxf(0.0f, rect.width - left - right),
        fmaxf(0.0f, rect.height - top - bottom), 0.0f, 0.0f);
    paint_push_clip(paint, clip, nullptr); rdt_path_free(clip);
    return true;
}

static bool paged_paint_node(LayoutViewNode* node, PaintList* paint) {
    if (node->table_range && node->table_range->missing) return true;
    if (node->paint_box && node->computed_boundary) {
        if (!render_paint_boundary_emit_box(paint, node->computed_boundary,
            {node->rect.x, node->rect.y, node->rect.width, node->rect.height})) return false;
    }
    else if (node->paint_box && (node->computed_style || (node->state && node->state->computed_style))) {
        ViewCssStyle* style = node->computed_style ? node->computed_style.get() : node->state->computed_style.get();
        if (style->background.a) {
            // separated-border row/group backgrounds stop at cell borders, leaving grid gaps transparent.
            if (style->display.inner == CSS_VALUE_TABLE_ROW || layout_display_is_table_row_group(style->display.inner))
                paged_paint_table_background(node, paint, style->background);
            else paint_fill_rect(paint, node->rect.x, node->rect.y, node->rect.width, node->rect.height, style->background);
        }
    }
    if (node->paint_box && node->computed_style && node->computed_style->display.inner == CSS_VALUE_TABLE) {
        // table layers paint all column groups before all columns, then the ordinary row/cell walk.
        paged_paint_column_backgrounds(node, node, paint, true);
        paged_paint_column_backgrounds(node, node, paint, false);
    }
    // overflow clips the padding box's content; it never cuts the region's own border/background.
    if (node->clip_content && !paged_paint_clip(paint, node->rect, node->clip_inset)) return false;
    if (node->glyph_run) paint_glyph_run(paint, node->glyph_run);
    if (node->image_box && !render_paint_image_box(paint, node->image_box)) return false;
    for (LayoutViewNode* child = node->first_child; child; child = child->next_sibling)
        if (!paged_paint_node(child, paint)) return false;
    if (node->clip_content) paint_pop_clip(paint);
    return true;
}

static bool paged_paint_column_rules(const ViewPageBox* page, PaintList* paint) {
    const ViewPageStyle* style = page->style;
    const ViewCssStyle* body = style ? style->body_style.get() : nullptr;
    if (!body || body->column_rule_style == CSS_VALUE_NONE || body->column_rule_style == CSS_VALUE_HIDDEN ||
        !body->column_rule_color.a || !body->column_rule_width) return true;
    float width = (float)body->column_rule_width->data.length.value;
    if (width <= 0.0f) return true;
    BorderProp border = {}; border.width.left = width;
    // CSS multicol interprets rule styles using collapsed borders: inset is ridge, outset is groove.
    border.left_style = body->column_rule_style == CSS_VALUE_INSET ? CSS_VALUE_RIDGE :
        body->column_rule_style == CSS_VALUE_OUTSET ? CSS_VALUE_GROOVE : body->column_rule_style;
    border.left_color = body->column_rule_color;
    BoundaryProp boundary = {}; boundary.border = lam::own(&border);
    if (style->body_clip && !paged_paint_clip(paint, style->body_rect, style->body_box.border.width.values)) return false;
    for (const ViewPageColumn* right = page->occupied_columns; right && right->previous; right = right->previous) {
        const ViewPageColumn* left = right->previous;
        if (left->index + 1 != right->index) continue;
        RdtLogicalRect first = {}, second = {};
        if (!view_css_page_column(style, left->index, &first) || !view_css_page_column(style, right->index, &second)) return false;
        if (second.x < first.x) { RdtLogicalRect swap = first; first = second; second = swap; }
        float gap_start = first.x + first.width, gap_end = second.x;
        float top = fmaxf(first.y, second.y), bottom = fminf(first.y + first.height, second.y + second.height);
        // explicit rectangles share a vertical rule only along their horizontally separated, overlapping edges.
        if (gap_end < gap_start || bottom <= top) continue;
        float x = gap_start + (gap_end - gap_start - width) * 0.5f;
        if (!render_paint_boundary_emit_box(paint, &boundary, {x, top, width, bottom - top})) return false;
    }
    if (style->body_clip) paint_pop_clip(paint);
    return true;
}

bool layout_secondary_paint_page(ViewTree* tree, const ViewPageBox* page, PaintList* paint) {
    if (!paint) return false;
    page = view_tree_page_material(tree, page);
    if (!page) return false;
    Color white = {.r = 255, .g = 255, .b = 255, .a = 255};
    paint_fill_rect(paint, 0.0f, 0.0f, page->node.rect.width, page->node.rect.height, page->style ? page->style->background : white);
    if (!paged_paint_clip(paint, page->node.rect)) return false;
    if (!page->blank && page->style && page->style->body_style) {
        // the boundary painter may normalize radii; keep immutable edition geometry untouched.
        ViewCssBoxEdges box = page->style->body_box;
        BoundaryProp boundary = {}; boundary.border = lam::own(&box.border); boundary.background = lam::own(&box.background);
        const RdtLogicalRect& rect = page->style->body_rect;
        if (!render_paint_boundary_emit_box(paint, &boundary, {rect.x, rect.y, rect.width, rect.height})) return false;
    }
    if (!page->blank && !paged_paint_column_rules(page, paint)) return false;
    for (LayoutViewNode* child = page->node.first_child; child; child = child->next_sibling) {
        if (page->blank && child->role != VIEW_FRAGMENT_MARGIN && child->role != VIEW_FRAGMENT_STATIC) continue;
        bool body_clip = page->style && page->style->body_clip &&
            (child->role == VIEW_FRAGMENT_BODY || child->role == VIEW_FRAGMENT_NOTE || child->role == VIEW_FRAGMENT_FLOAT);
        if (body_clip && !paged_paint_clip(paint, page->style->body_rect, page->style->body_box.border.width.values)) return false;
        if (!paged_paint_node(child, paint)) return false;
        if (body_clip) paint_pop_clip(paint);
    }
    paint_pop_clip(paint);
    return true;
}

static bool paged_paint_placement(ViewTree* tree, const ViewPageBox* page,
        const ViewPagePlacement* placement, void* context) {
    PaintList* paint = (PaintList*)context;
    RdtMatrix matrix = rdt_matrix_identity();
    matrix.e11 = matrix.e22 = placement->scale;
    matrix.e13 = placement->rect.x; matrix.e23 = placement->rect.y;
    paint_push_transform(paint, &matrix);
    bool painted = layout_secondary_paint_page(tree, page, paint);
    paint_pop_transform(paint);
    return painted;
}

bool layout_secondary_paint_root(ViewTree* tree, PaintList* paint, const RdtLogicalRect* clip) {
    if (!paint || !view_tree_model_source_valid(tree) || !tree->model->committed) return false;
    if (tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
        return paged_paint_node(tree->model->root, paint);
    }
    // shared placement traversal culls offscreen sheets before recording glyphs and images.
    return view_tree_preview_paint(tree, clip, paged_paint_placement, paint) == VIEW_MODEL_OK;
}
