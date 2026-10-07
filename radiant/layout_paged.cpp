#include "layout_paged.hpp"
#include "layout.hpp"
#include "render.hpp"
#include "typeset_marks.hpp"
#include "typeset_regions.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lib/font/font.h"
#include "../lib/mem_factory.h"
#include "../lib/mem_grow.hpp"
#include "../lib/utf.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/str.h"
#include <limits.h>
#include <math.h>
#include <string.h>

enum PagedFlowKind : uint8_t { PAGED_FLOW_BLOCK, PAGED_FLOW_PARAGRAPH };
enum PagedRegionKind : uint8_t { PAGED_REGION_NOTE, PAGED_REGION_TOP, PAGED_REGION_BOTTOM, PAGED_REGION_COUNT };
struct PagedRegionRecord;
struct PagedImagePaint;
struct PagedFlowNode {
    PagedFlowKind kind;
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
    bool has_images;
};
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
};
struct PagedRunningRecord : TypesetRecord { PagedFlowNode* flow; };
struct PagedMarkSource {
    DomNode* source;
    ViewCssStyle* style;
    size_t assigned_at;
    PagedRunningRecord* running;
    uint32_t target_page;
};
struct PagedTargetValue : TypesetRecord {
    DomNodeRef source;
    CounterSnapshot* counters;
    const char* text;
    const char* before;
    const char* after;
};
struct PagedReferenceSession {
    Pool* pool;
    TypesetTargetStore targets;
    TypesetConvergence convergence;
    size_t page_count;
    uint32_t pass;
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
    float x, width, y;
};
struct PagedRegionPaint : TypesetRecord {
    PagedRegionLine* lines;
    size_t count, capacity;
};
HASHMAP_DEFINE_PTRKEY(paged_mark_sources, PagedMarkSource, source)
struct PagedComposition {
    Pool* pool;
    PagedFlowNode* root;
    PagedLayoutOptions options;
    PagedLayoutDiagnostic diagnostic;
    size_t nodes, items, generated_glyphs, block_trials;
    lam::Own<HashMap> mark_sources;
    TypesetMarkStore marks;
    TypesetRegionQueue queues[PAGED_REGION_COUNT];
    CounterContext* counters;
    int quote_depth;
    uint64_t note_counter;
    PagedReferenceSession* references;
    DomNodeRef reference_source;
    bool reference_used;
    Pool* reference_pool;
    TypesetTargetStore targets;
};
struct PagedBoxEdges {
    float edges[4], padding[4];
    BorderProp border;
    BackgroundProp background;
};
struct PagedFrame {
    PagedFlowNode* flow;
    LayoutViewNode* fragment;
    float x, content_x, width, content_width, content_height, replaced_height;
    float page_start, consumed_content;
    PagedBoxEdges box;
    size_t occurrence;
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
struct PagedComposer {
    ViewTree* tree;
    PagedComposition* composition;
    PagedFrame* frames;
    size_t depth, frame_capacity;
    ViewPageBox* page;
    ViewPageStyle page_style;
    RdtLogicalRect initial_containing_block;
    float y, bottom, page_start, pending_margin;
    bool pending_break, page_has_content, closure_failure;
    ViewFragmentRole role;
    ViewBreak requested_break;
    const char* page_name;
    size_t committed_lines;
    PagedRegionState regions[PAGED_REGION_COUNT];
};

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
    const TypesetRegionMaterial** anchors[PAGED_REGION_COUNT] = {};
    TypesetRegionPlan plans[PAGED_REGION_COUNT] = {};
    TypesetRegionCheckpoint queues[PAGED_REGION_COUNT] = {};
    TypesetMarkCheckpoint marks = {};
    PagedLayoutDiagnostic diagnostic = {};
    size_t generated_glyphs = 0;

    void dispose() {
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
            typeset_region_plan_dispose(&plans[i]);
            // replay starts a new transaction; queue snapshots belonged to the released pool.
            queues[i] = {};
        }
        if (pool) mem_pool_destroy(pool);
        pool = nullptr;
    }

    TypesetStatus begin(PagedComposer* target) {
        composer = target; saved = *target;
        pool = mem_pool_create((MemContext*)target->tree->model->document->services.mem_ctx,
            MEM_ROLE_LAYOUT, "typeset.html.checkpoint");
        if (!pool) return TYPESET_OUT_OF_MEMORY;
        frames = (PagedFrame*)paged_checkpoint_copy(pool, target->frames, target->depth * sizeof(PagedFrame));
        if (target->depth && !frames) return TYPESET_OUT_OF_MEMORY;
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
            const PagedRegionState& region = target->regions[i];
            anchors[i] = (const TypesetRegionMaterial**)paged_checkpoint_copy(pool, region.anchors,
                region.anchor_count * sizeof(TypesetRegionMaterial*));
            if (region.anchor_count && !anchors[i]) return TYPESET_OUT_OF_MEMORY;
            TypesetStatus status = typeset_region_plan_retain(&region.plan, &plans[i]);
            if (status == TYPESET_OK) status = typeset_region_checkpoint(&target->composition->queues[i], pool, &queues[i]);
            if (status != TYPESET_OK) return status;
        }
        marks = typeset_marks_checkpoint(&target->composition->marks);
        generated_glyphs = target->composition->generated_glyphs;
        diagnostic = target->composition->diagnostic;
        view = view_tree_model_checkpoint(target->tree);
        return view ? TYPESET_OK : TYPESET_OUT_OF_MEMORY;
    }

    TypesetStatus restore() {
        if (!view) return TYPESET_INVALID;
        TypesetStatus status = TYPESET_OK;
        PagedFrame* current_frames = composer->frames;
        size_t frame_capacity = composer->frame_capacity;
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
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
            PagedRegionState& region = composer->regions[i];
            region.anchors = current[i].anchors; region.anchor_capacity = current[i].anchor_capacity;
            if (region.anchor_count) memcpy(region.anchors, anchors[i], region.anchor_count * sizeof(TypesetRegionMaterial*));
            region.plan = plans[i]; plans[i] = {};
        }
        TypesetStatus restored = typeset_marks_restore(&composer->composition->marks, marks);
        if (status == TYPESET_OK) status = restored;
        composer->composition->generated_glyphs = generated_glyphs;
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
static TypesetStatus paged_region_measure(void* context, const TypesetResume* start,
    const TypesetRegionConstraints* constraints, bool split, Pool* scratch, TypesetRegionSlice* slice);
static TypesetStatus paged_flow_height(PagedComposer* composer, PagedFlowNode* flow,
        float parent_width, float parent_height, bool leading, float* result, bool* forced, bool allow_overflow = false);
static TypesetStatus paged_fixed_flow(PagedComposer* composer, PagedFlowNode* flow);
static TypesetStatus paged_content_text(ViewTree* tree, PagedComposition* composition, ViewCssStyle* style,
    ViewPageBox* page, const CssValue* content, uint64_t footnote, char** result, size_t* length,
    PagedFlowNode* paragraph = nullptr);

PagedLayoutOptions paged_layout_options_default() {
    PagedLayoutOptions options = {};
    options.max_pages = 10000;
    options.max_nodes = 1000000;
    options.max_block_trials = 2 * options.max_nodes;
    options.max_items = 4000000;
    options.max_depth = 256;
    options.max_reference_passes = 8;
    options.first_side = VIEW_PAGE_RIGHT;
    return options;
}

void paged_composition_destroy(ViewTree* tree) {
    if (!tree || !tree->model || !tree->model->composition) return;
    PagedComposition* composition = tree->model->composition;
    if (composition->mark_sources) hashmap_free(composition->mark_sources);
    typeset_targets_dispose(&composition->targets);
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
    return status;
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

struct PagedCounterScope {
    PagedComposition* composition;
    bool pushed, preserve_reset_scope;
    TypesetStatus status;
    PagedCounterScope(ViewTree* tree, PagedComposition* owner, ViewCssStyle* style, bool pseudo = false)
        : composition(owner), pushed(false), preserve_reset_scope(!pseudo), status(TYPESET_OK) {
        CounterScope* previous = owner->counters->current_scope;
        counter_push_scope(owner->counters, pseudo);
        pushed = owner->counters->current_scope != previous;
        if (!pushed) { status = TYPESET_OUT_OF_MEMORY; return; }
        LayoutContext context = {}; context.doc = tree->model->document;
        context.pool = lam::up(tree->model->css->pool.get()); context.selected_view_tree = lam::up(tree);
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
        const char* names[] = {"counter-reset", "counter-increment", "counter-set"};
        void (*apply[])(CounterContext*, const char*) = {counter_reset, counter_increment, counter_set};
        for (size_t i = 0; i < 3; i++) {
            if (i == 1 && !pseudo && style->display.list_item &&
                !layout_counter_named_value(style->counter_increment, "list-item", 1, &named_value))
                counter_increment(owner->counters, style->list_reversed ? "list-item -1" : "list-item 1");
            if (!values[i] || css_value_is_none(values[i])) continue;
            for (int j = 0; j < css_value_count(values[i], 0); j++) {
                const CssValue* item = css_value_at(values[i], j);
                const char* name = item ? layout_css_counter_name(item, true) : nullptr;
                if (name && (strcmp(name, "page") == 0 || strcmp(name, "pages") == 0 || strcmp(name, "footnote") == 0)) {
                    status = paged_failure(owner, TYPESET_INVALID, style->source, 0, "paged counter declarations require composition checkpoints");
                    return;
                }
                if (item && item->type == CSS_VALUE_TYPE_FUNCTION && css_function_name_is(item->data.function, "reversed")) {
                    status = paged_failure(owner, TYPESET_INVALID, style->source, 0, "reversed source counters require settled list scopes");
                    return;
                }
            }
            char* specification = nullptr;
            resolve_counter_property(&context, values[i], &specification, names[i], false);
            if (specification) apply[i](owner->counters, specification);
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
    }
    ~PagedCounterScope() {
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

static bool paged_atomic_boundary_allows(const TypesetItem& left, const TypesetItem& right) {
    if (left.kind == TYPESET_PENALTY || right.kind == TYPESET_PENALTY) return false;
    const PagedPaint* a = (const PagedPaint*)left.paint.get();
    const PagedPaint* b = (const PagedPaint*)right.paint.get();
    bool a_image = a && a->kind == PAGED_PAINT_IMAGE, b_image = b && b->kind == PAGED_PAINT_IMAGE;
    if (!a_image && !b_image) return false;
    if ((a_image && ((const PagedImagePaint*)a)->framed) || (b_image && ((const PagedImagePaint*)b)->framed)) return false;
    auto item_style = [](const TypesetItem& item, const PagedPaint* paint) -> const ViewCssStyle* {
        return paint ? paint->style : item.source.native ? ((const PagedSourceRecord*)item.source.native.get())->style : nullptr;
    };
    const ViewCssStyle* common = view_css_common_ancestor(item_style(left, a), item_style(right, b));
    if (!common || common->white_space == CSS_VALUE_NOWRAP || common->white_space == CSS_VALUE_PRE) return false;
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

static TypesetStatus paged_item_append(PagedComposition* composition, PagedFlowNode* paragraph,
                                      const TypesetItem& item) {
    if (++composition->items > composition->options.max_items) return TYPESET_BUDGET_EXHAUSTED;
    if (!lam::pool_grow_array(composition->pool, &paragraph->items, &paragraph->item_capacity,
                              paragraph->paragraph.count + 1, 16)) return TYPESET_OUT_OF_MEMORY;
    TypesetItem next = item;
    size_t previous = paragraph->paragraph.count;
    bool styled_item = item.paint || (item.source.native && ((const PagedSourceRecord*)item.source.native.get())->style);
    // source-only anchors retain order but do not interrupt an adjacent atomic wrap boundary.
    while (styled_item && previous && paragraph->items[previous - 1].kind == TYPESET_GLUE && !paragraph->items[previous - 1].paint &&
           !paragraph->items[previous - 1].length && paragraph->items[previous - 1].glue.natural == 0.0f &&
           paragraph->items[previous - 1].boundary.legality == TYPESET_BREAK_FORBIDDEN) previous--;
    if (styled_item && previous && paged_atomic_boundary_allows(paragraph->items[previous - 1], item)) {
        next.has_before = true;
        next.before = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_LINE, 0, 0};
    }
    paragraph->items[paragraph->paragraph.count++] = next;
    paragraph->paragraph.items = paragraph->items;
    return TYPESET_OK;
}

static bool paged_white(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f'; }

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
    item->metrics = {advance, metrics->ascender, -metrics->descender, metrics->ascender, {}, nullptr};
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
    bool preserve = style->white_space == CSS_VALUE_PRE || style->white_space == CSS_VALUE_PRE_WRAP ||
        style->white_space == CSS_VALUE_BREAK_SPACES;
    bool nowrap = style->white_space == CSS_VALUE_NOWRAP || style->white_space == CSS_VALUE_PRE;
    TypesetSource identity = paged_source_identity(tree, composition, source,
        generated ? TYPESET_PROVIDER_OFFSETS : TYPESET_UTF8_BYTES, style);
    if (!identity.native) return TYPESET_OUT_OF_MEMORY;
    for (size_t start = 0; start < length;) {
        size_t end = start;
        uint32_t cp = 0;
        int bytes = utf8_decode(text + end, length - end, &cp);
        if (bytes < 0) return TYPESET_INVALID;
        bool whitespace = paged_white(cp);
        bool newline = preserve && (cp == '\n' || cp == '\r');
        end += (size_t)bytes;
        while (end < length && !preserve) {
            uint32_t next = 0;
            bytes = utf8_decode(text + end, length - end, &next);
            if (bytes < 0) return TYPESET_INVALID;
            if (paged_white(next) != whitespace) break;
            end += (size_t)bytes;
        }
        if (!whitespace && preserve) {
            while (end < length) {
                uint32_t next = 0;
                bytes = utf8_decode(text + end, length - end, &next);
                if (bytes < 0) return TYPESET_INVALID;
                if (paged_white(next)) break;
                end += (size_t)bytes;
            }
        }
        TypesetItem item = {};
        item.source = identity;
        item.start = start; item.length = end - start;
        item.boundary = {nowrap ? TYPESET_BREAK_FORBIDDEN : TYPESET_BREAK_ALLOWED, TYPESET_BREAK_LINE, 0, 0};
        if (newline) {
            item.kind = TYPESET_PENALTY;
            item.boundary.legality = TYPESET_BREAK_FORCED;
        } else if (whitespace) {
            item.kind = TYPESET_GLUE;
            float space = font_measure_char(handle, ' ') + style->font.word_spacing;
            bool repeated = paragraph->paragraph.count && paragraph->items[paragraph->paragraph.count - 1].kind == TYPESET_GLUE;
            item.glue = {repeated && !preserve ? 0.0f : space, space * 0.5f, space * 0.333333f, 0, 0, !preserve, !preserve};
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
        (!style->source->id || !*style->source->id)) return TYPESET_OK;
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
    if (depth > composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    if (!lam::pool_grow_array(composition->pool, &record->parts, &record->capacity, record->count + 1, 16))
        return TYPESET_OUT_OF_MEMORY;
    record->parts[record->count++] = {flow->kind == PAGED_FLOW_PARAGRAPH ? PAGED_REGION_PARAGRAPH : PAGED_REGION_OPEN, flow};
    for (PagedFlowNode* child = flow->first_child; child; child = child->next) {
        TypesetStatus status = paged_region_parts(composition, record, child, depth + 1);
        if (status != TYPESET_OK) return status;
    }
    if (flow->kind == PAGED_FLOW_BLOCK) {
        if (!lam::pool_grow_array(composition->pool, &record->parts, &record->capacity, record->count + 1, 16))
            return TYPESET_OUT_OF_MEMORY;
        record->parts[record->count++] = {PAGED_REGION_CLOSE, flow};
    }
    return TYPESET_OK;
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

static TypesetStatus paged_context_admit(ViewTree* tree, PagedComposition* composition, ViewCssStyle* style) {
    // A flow producer must not silently serialize a different formatting context into block text.
    const char* reason = nullptr;
    bool image = style->source->tag() == MARKUP_NAME_IMG && !style->pseudo_element;
    if ((!image && style->display.inner == RDT_DISPLAY_REPLACED) || css_is_mathml_element(style->source) ||
        css_content_value_has_image_url(view_css_property(tree, style, "content")))
        reason = "replaced content requires a paged fragment producer";
    else if ((!image && style->display.inner != CSS_VALUE_FLOW) ||
             (style->display.outer != CSS_VALUE_BLOCK && style->display.outer != CSS_VALUE_INLINE &&
              !(image && style->display.outer == CSS_VALUE_INLINE_BLOCK)))
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
    ImageSurface* image = source ? load_document_image_resource(tree->model->document,
        &tree->model->image_resources, source.get()) : nullptr;
    if (!image || (image->format == IMAGE_FORMAT_UNKNOWN && !image->pixels && !image->source_data && !image->source_path))
        return paged_failure(composition, TYPESET_UNPLACEABLE, style->source, 0, "image source is unavailable or requires a sampled media producer");
    PagedImagePaint* payload = (PagedImagePaint*)pool_calloc(composition->pool, sizeof(PagedImagePaint));
    if (!payload) return TYPESET_OUT_OF_MEMORY;
    payload->provider = tree->model->tree_id; payload->kind = PAGED_PAINT_IMAGE;
    payload->source = style->source; payload->style = style; payload->image = image;
    payload->dimension_source = dimensions;
    payload->selected_source = pool_dup_n(composition->pool, source.get(), strlen(source.get()));
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
    (*paragraph)->has_images = true;
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
    TypesetStatus admitted = paged_context_admit(tree, composition, source_style);
    if (admitted != TYPESET_OK) return admitted;
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
                const char* policy = css_value_identifier_name(style->footnote_policy);
                PagedRegionRecord* record = (PagedRegionRecord*)pool_calloc(composition->pool, sizeof(PagedRegionRecord));
                if (!record) return TYPESET_OUT_OF_MEMORY;
                uint64_t number = ++composition->note_counter;
                if (number > INT_MAX) return TYPESET_BUDGET_EXHAUSTED;
                char marker[32] = {};
                int marker_length = counter_format_value((int)number, CSS_VALUE_DECIMAL, marker, sizeof(marker)); // INT_CAST_OK: bounded source-order footnote counter.
                if (marker_length <= 0 || (size_t)marker_length >= sizeof(marker) - 1) return TYPESET_INVALID;
                marker[marker_length++] = ' '; marker[marker_length] = '\0';
                ViewCssStyle* marker_style = nullptr; char* note_marker = nullptr; size_t note_length = 0;
                TypesetStatus status = paged_note_text(tree, composition, child_element, PSEUDO_ELEMENT_FOOTNOTE_MARKER,
                    number, marker, (size_t)marker_length, &marker_style, &note_marker, &note_length);
                if (status == TYPESET_OK) status = paged_extracted_flow(tree, composition, child_element, style,
                    depth, note_marker, note_length, &record->flow, marker_style);
                if (status == TYPESET_OK) status = paged_region_parts(composition, record, record->flow, 0);
                if (status != TYPESET_OK) return status;
                paged_region_bind(tree, record, PAGED_REGION_NOTE);
                record->block_policy = policy && str_ieq_cstr(policy, "block");
                record->material.split = !record->block_policy && (!policy || !str_ieq_cstr(policy, "line"));
                record->material.defer_anchor = !policy || str_ieq_cstr(policy, "auto");
                status = paged_paragraph_ensure(composition, parent, paragraph);
                if (status != TYPESET_OK) return status;
                size_t call_first = (*paragraph)->paragraph.count;
                ViewCssStyle* call_style = nullptr; char* call = nullptr; size_t call_length = 0;
                status = paged_note_text(tree, composition, child_element, PSEUDO_ELEMENT_FOOTNOTE_CALL,
                    number, marker, (size_t)marker_length - 1, &call_style, &call, &call_length);
                if (status == TYPESET_OK) status = paged_text_items(tree, composition, *paragraph, child, call, call_length, call_style, true);
                if (status != TYPESET_OK) return status;
                if (call_first == (*paragraph)->paragraph.count) {
                    status = paged_anchor_append(tree, composition, child_element, parent, paragraph);
                    if (status != TYPESET_OK) return status;
                }
                PagedSourceRecord* call_source = (PagedSourceRecord*)(*paragraph)->items[call_first].source.native.get();
                call_source->insertion = record;
                if (record->block_policy) (*paragraph)->block_note_item = call_first + 1;
                continue;
            }
            if (style->float_value == CSS_VALUE_TOP || style->float_value == CSS_VALUE_BOTTOM) {
                const char* reference = css_value_identifier_name(style->float_reference);
                if (furniture || tree->model->environment.presentation != VIEW_PRESENTATION_PAGED ||
                    !reference || !str_ieq_cstr(reference, "page"))
                    return paged_failure(composition, TYPESET_INVALID, child, 0, "top and bottom floats require the page reference in paged body flow");
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
                PagedFlowNode* flow = paged_flow_new(composition, PAGED_FLOW_BLOCK, child_element, style);
                if (!flow) return TYPESET_OUT_OF_MEMORY;
                paged_flow_link(parent, flow);
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

static float paged_used_length(PagedComposer* composer, ViewCssStyle* style, const CssValue* value,
                               CssPropertyCode property, float width, float fallback = 0.0f, float height = NAN) {
    float result = view_css_length(composer->tree, style, value, property, width, height);
    return isfinite(result) ? result : fallback;
}

static float paged_root_height(const PagedComposer* composer) {
    // body percentages retain the first page's ICB; auxiliary regions supply their own stable container.
    return composer->role == VIEW_FRAGMENT_BODY && composer->initial_containing_block.width > 0.0f
        ? composer->initial_containing_block.height : composer->page_style.content_rect.height;
}

static TypesetStatus paged_box_edges(PagedComposer* composer, const ViewCssStyle* source,
        const ViewPageAreaStyle* declarations, float width, PagedBoxEdges* box) {
    *box = {};
    ViewCssStyle style = *source;
    LayoutContext context = {}; context.doc = composer->tree->model->document;
    context.pool = lam::up(composer->tree->model->css->pool.get());
    context.selected_view_tree = lam::up(composer->tree); context.selected_style = lam::up(&style);
    auto query = [&](const CssDeclaration* declaration, const char* name) {
        return declarations ? view_css_declaration_value(composer->tree, &style, declaration, name)
            : view_css_property(composer->tree, &style, name);
    };
    const CssValue* value = query(declarations ? declarations->color : nullptr, "color");
    if (value) style.color = resolve_color_value(&context, value);
    value = query(declarations ? declarations->background : nullptr, "background-color");
    if (value) box->background.color = resolve_color_value(&context, value);
    for (size_t i = 0; i < 4; i++) {
        CssBoxSide side = static_cast<CssBoxSide>(i);
        CssPropertyCode width_property = radiant_border_width_property(side);
        const char* width_name = css_property_get_by_code(width_property)->name;
        const char* style_name = css_property_get_by_code(radiant_box_side_property(CSS_PROPERTY_BORDER_STYLE, side))->name;
        const char* color_name = css_property_get_by_code(radiant_box_side_property(CSS_PROPERTY_BORDER_COLOR, side))->name;
        CssPropertyCode padding_property = radiant_box_side_property(CSS_PROPERTY_PADDING, side);
        value = declarations ? query(declarations->padding[i], css_property_get_by_code(padding_property)->name) : source->padding[i].get();
        box->padding[i] = paged_used_length(composer, &style, value, padding_property, width);
        value = query(declarations ? declarations->border_style[i] : nullptr, style_name);
        CssEnum border_style = value && value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE_NONE;
        if (border_style != CSS_VALUE_NONE && border_style != CSS_VALUE_HIDDEN && border_style != CSS_VALUE_SOLID) return TYPESET_INVALID;
        box->border.styles[i] = border_style;
        value = query(declarations ? declarations->border_width[i] : nullptr, width_name);
        if (border_style != CSS_VALUE_NONE && border_style != CSS_VALUE_HIDDEN) {
            box->border.width.values[i] = !value || value->type == CSS_VALUE_TYPE_KEYWORD
                ? layout_css_border_width_keyword(value ? value->data.keyword : CSS_VALUE_MEDIUM)
                : paged_used_length(composer, &style, value, width_property, width);
        }
        box->edges[i] = box->padding[i] + box->border.width.values[i];
        if (!isfinite(box->edges[i]) || box->edges[i] < 0.0f) return TYPESET_INVALID;
        value = query(declarations ? declarations->border_color[i] : nullptr, color_name);
        box->border.colors[i] = value ? resolve_color_value(&context, value) : style.color;
    }
    // Simple boundary lowering shares one color across visible border sides.
    Color border_color = {}; bool visible = false;
    for (size_t i = 0; i < 4; i++) if (box->border.width.values[i] > 0.0f && box->border.colors[i].a) {
        if (visible && border_color.c != box->border.colors[i].c) return TYPESET_INVALID;
        visible = true; border_color = box->border.colors[i];
    }
    return TYPESET_OK;
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
    if (!top) { storage->border.width.top = 0.0f; storage->boundary.padding.top = 0.0f; }
    if (!bottom) { storage->border.width.bottom = 0.0f; storage->boundary.padding.bottom = 0.0f; }
    node->computed_boundary = lam::up(&storage->boundary);
    return TYPESET_OK;
}

static TypesetStatus paged_frame_finish(PagedComposer* composer, PagedFrame* frame, bool last) {
    if (!view_tree_model_touch_node(composer->tree, frame->fragment)) return TYPESET_OUT_OF_MEMORY;
    frame->fragment->rect.height = fmaxf(0.0f, composer->y - frame->page_start);
    frame->fragment->last_fragment = last;
    bool clone = frame->flow->style->decoration_clone;
    return paged_boundary_publish(composer->tree, frame->fragment, frame->box,
        frame->fragment->first_fragment || clone, last || clone);
}

struct PagedImageMeasure { PagedBoxEdges box; float margin[4], width, height; };
static TypesetStatus paged_image_measure(PagedComposer* composer, const PagedImagePaint* image,
        float available_width, float parent_height, PagedImageMeasure* result) {
    *result = {};
    ViewCssStyle* style = image->style;
    TypesetStatus status = paged_box_edges(composer, style, nullptr, available_width, &result->box);
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
    if (!layout_replaced_content_size(&image->facts, &constraints,
        fmaxf(0.0f, available_width - edges_x - result->margin[3] - result->margin[1]), &result->width, &result->height))
        return TYPESET_UNPLACEABLE;
    return TYPESET_OK;
}

static TypesetStatus paged_frame_measure(PagedComposer* composer, PagedFrame* frame,
        float parent_x, float parent_width, float parent_height) {
    ViewCssStyle* style = frame->flow->style;
    float left = paged_used_length(composer, style, style->margin[3], CSS_PROPERTY_MARGIN_LEFT, parent_width);
    float right = paged_used_length(composer, style, style->margin[1], CSS_PROPERTY_MARGIN_RIGHT, parent_width);
    frame->x = parent_x + left;
    TypesetStatus status = paged_box_edges(composer, style, nullptr, parent_width, &frame->box);
    if (status != TYPESET_OK) return paged_failure(composer->composition, status, frame->flow->source, 0,
        "block decoration requires supported padding and solid border styles");
    float edges = frame->box.edges[3] + frame->box.edges[1];
    frame->content_height = paged_used_length(composer, style, style->height,
        CSS_PROPERTY_HEIGHT, parent_width, NAN, parent_height);
    if (isfinite(frame->content_height)) frame->content_height = fmaxf(0.0f, frame->content_height -
        (style->box_sizing == CSS_VALUE_BORDER_BOX ? frame->box.edges[0] + frame->box.edges[2] : 0.0f));
    if (frame->flow->image) {
        PagedImageMeasure measured = {};
        status = paged_image_measure(composer, frame->flow->image, parent_width, parent_height, &measured);
        if (status != TYPESET_OK) return status;
        frame->width = measured.width + edges; frame->content_width = measured.width;
        frame->replaced_height = measured.height;
        frame->content_height = measured.height;
        float free = parent_width - frame->width - left - right;
        bool auto_left = css_value_is_auto(style->margin[3]), auto_right = css_value_is_auto(style->margin[1]);
        if (free > 0.0f && auto_left) frame->x += auto_right ? free * 0.5f : free;
        frame->content_x = frame->x + frame->box.edges[3];
        return TYPESET_OK;
    }
    float specified = paged_used_length(composer, style, style->width, CSS_PROPERTY_WIDTH, parent_width, NAN);
    frame->width = isfinite(specified) ? specified + (style->box_sizing == CSS_VALUE_BORDER_BOX ? 0.0f : edges) : parent_width - left - right;
    float minimum = paged_used_length(composer, style, style->min_width, CSS_PROPERTY_MIN_WIDTH, parent_width, 0.0f);
    float maximum = paged_used_length(composer, style, style->max_width, CSS_PROPERTY_MAX_WIDTH, parent_width, INFINITY);
    if (style->box_sizing != CSS_VALUE_BORDER_BOX) { minimum += edges; maximum += edges; }
    frame->width = fmaxf(minimum, fminf(frame->width, maximum));
    frame->content_x = frame->x + frame->box.edges[3];
    frame->content_width = frame->width - frame->box.edges[3] - frame->box.edges[1];
    if (!isfinite(frame->content_width) || frame->content_width <= 0.0f) return TYPESET_UNPLACEABLE;
    return TYPESET_OK;
}

static TypesetStatus paged_frame_open(PagedComposer* composer, size_t index, bool first) {
    PagedFrame* frame = &composer->frames[index];
    ViewCssStyle* style = frame->flow->style;
    float parent_x = index ? composer->frames[index - 1].content_x : composer->page_style.content_rect.x;
    float parent_width = index ? composer->frames[index - 1].content_width : composer->page_style.content_rect.width;
    float parent_height = index ? composer->frames[index - 1].content_height : paged_root_height(composer);
    TypesetStatus status = paged_frame_measure(composer, frame, parent_x, parent_width, parent_height);
    if (status != TYPESET_OK) return status;
    LayoutViewNode* parent = index ? composer->frames[index - 1].fragment :
        composer->page ? &composer->page->node : composer->tree->model->root.get();
    frame->page_start = composer->y;
    frame->fragment = view_tree_fragment_append(composer->tree, parent, frame->flow->source,
                                               {frame->x, composer->y, frame->width, 0.0f});
    if (!frame->fragment) return TYPESET_OUT_OF_MEMORY;
    frame->fragment->first_fragment = first;
    frame->fragment->paint_box = true;
    frame->fragment->role = composer->role;
    frame->fragment->generated = composer->role != VIEW_FRAGMENT_BODY;
    frame->fragment->computed_style = lam::up(style);
    frame->occurrence++;
    if (first || style->decoration_clone) composer->y += frame->box.edges[0];
    return TYPESET_OK;
}

static TypesetStatus paged_frames_close(PagedComposer* composer) {
    for (size_t i = composer->depth; i > 0; i--) {
        PagedFrame& frame = composer->frames[i - 1];
        bool blank = composer->page && composer->page->blank;
        bool clone = frame.flow->style->decoration_clone;
        float top = frame.fragment->first_fragment || clone ? frame.box.edges[0] : 0.0f;
        float bottom = clone && !blank ? frame.box.edges[2] : 0.0f;
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

static TypesetStatus paged_empty_page_restyle(PagedComposer* composer, bool blank) {
    ViewPageBox* page = composer->page;
    if (!page || composer->page_has_content) return TYPESET_INVALID;
    if (!view_tree_model_touch_node(composer->tree, &page->node)) return TYPESET_OUT_OF_MEMORY;
    ViewPageStyle* style = (ViewPageStyle*)pool_alloc(composer->composition->pool, sizeof(ViewPageStyle));
    if (!style) return TYPESET_OUT_OF_MEMORY;
    if (view_css_page_style(composer->tree, composer->page_name, page->page_number, page->side, blank, style) != VIEW_MODEL_OK) return TYPESET_INVALID;
    page->style = lam::up(style); page->name = lam::up(composer->page_name); page->blank = blank;
    page->node.rect.width = style->width; page->node.rect.height = style->height; page->content_rect = style->content_rect;
    composer->page_style = *style;
    if (page->page_number == 1) composer->initial_containing_block = style->content_rect;
    composer->y = composer->page_start = style->content_rect.y;
    composer->bottom = style->content_rect.y + style->content_rect.height;
    float x = style->content_rect.x, width = style->content_rect.width, height = paged_root_height(composer);
    for (size_t i = 0; i < composer->depth; i++) {
        PagedFrame* frame = &composer->frames[i];
        TypesetStatus status = paged_frame_measure(composer, frame, x, width, height);
        if (status != TYPESET_OK) return status;
        frame->page_start = composer->y;
        if (!view_tree_model_touch_node(composer->tree, frame->fragment)) return TYPESET_OUT_OF_MEMORY;
        frame->fragment->rect = {frame->x, composer->y, frame->width, 0.0f};
        if (frame->fragment->first_fragment || frame->flow->style->decoration_clone) composer->y += frame->box.edges[0];
        x = frame->content_x; width = frame->content_width; height = frame->content_height;
    }
    return TYPESET_OK;
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
    if (view_css_page_style(tree, composer->page_name, static_cast<uint32_t>(count + 1), side,
                            blank, &composer->page_style) != VIEW_MODEL_OK) return TYPESET_INVALID;
    ViewPageStyle* style = (ViewPageStyle*)pool_alloc(composer->composition->pool, sizeof(ViewPageStyle));
    if (!style) return TYPESET_OUT_OF_MEMORY;
    *style = composer->page_style;
    composer->page = view_tree_page_append(tree, style->width, style->height, style->content_rect, side, blank);
    if (!composer->page) return TYPESET_OUT_OF_MEMORY;
    composer->page->style = lam::up(style);
    if (!count) composer->initial_containing_block = style->content_rect;
    composer->page->name = lam::up(composer->page_name);
    composer->y = composer->page_start = style->content_rect.y;
    composer->page_has_content = false;
    composer->bottom = style->content_rect.y + style->content_rect.height;
    return TYPESET_OK;
}

static TypesetStatus paged_next_page(PagedComposer* composer) {
    if (composer->tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
        composer->pending_break = false; return TYPESET_OK;
    }
    bool previous_blank = composer->page && composer->page->blank;
    if (composer->page) {
        TypesetStatus status = paged_frames_close(composer);
        if (status == TYPESET_OK) status = paged_regions_close(composer);
        if (status != TYPESET_OK) return status;
    }
    ViewBreak request = composer->requested_break;
    ViewPageSide wanted = VIEW_PAGE_RIGHT;
    bool side_request = paged_requested_side(composer, request, &wanted);
    if (side_request && paged_side(composer, composer->tree->model->page_count) != wanted) {
        TypesetStatus status = paged_page_create(composer, true);
        if (status != TYPESET_OK) return status;
    }
    TypesetStatus status = paged_page_create(composer, false);
    if (status != TYPESET_OK) return status;
    for (size_t i = 0; i < composer->depth; i++) {
        bool first = previous_blank && composer->frames[i].fragment->first_fragment;
        status = paged_frame_open(composer, i, first);
        if (status != TYPESET_OK) return status;
    }
    composer->pending_break = false;
    composer->requested_break = VIEW_BREAK_AUTO;
    composer->pending_margin = 0.0f;
    return paged_regions_prepare(composer);
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
        TypesetStatus status = typeset_target_append(&session->targets, &target);
        if (status != TYPESET_OK) return status;
    }
    for (DomNode* child = source->first_child; child; child = child->next_sibling) if (child->is_element()) {
        TypesetStatus status = paged_targets_seed(tree, session, child->as_element(), depth + 1, visited, options);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
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
    if (!target || !target->value || target->value->provider != binding->tree->model->tree_id) return false;
    const PagedTargetValue* value = (const PagedTargetValue*)target->value.get();
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
        size_t number = strcmp(name, "page") == 0 ? target->page_number : binding->composition->references->page_count;
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
        strcmp(name, "page") == 0 && binding->page ? binding->page->page_number :
        strcmp(name, "pages") == 0 && binding->page ? binding->tree->model->page_count : UINT64_MAX;
    if (value == UINT64_MAX) return counter_snapshot_append(binding->style->counters, name, separator, style, text);
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
    bool ok = css_content_append(content, &callbacks, page ? &quotes : &composition->quote_depth, text);
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
    mark.at_page_start = !composer->page_has_content;
    return typeset_mark_append(&composer->composition->marks, &mark);
}

static TypesetStatus paged_mark_enter(PagedComposer* composer, DomNode* source, size_t depth = 0) {
    if (!composer->page) return TYPESET_OK;
    if (depth > composer->composition->options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    if (source->parent) {
        TypesetStatus status = paged_mark_enter(composer, source->parent, depth + 1);
        if (status != TYPESET_OK) return status;
    }
    PagedMarkSource key = {}; key.source = source;
    PagedMarkSource* owner = (PagedMarkSource*)hashmap_get(composer->composition->mark_sources, &key);
    if (!owner) return TYPESET_OK;
    if (!view_tree_model_record(composer->tree, owner, sizeof(*owner))) return TYPESET_OUT_OF_MEMORY;
    if (!owner->target_page && owner->style->display.outer != CSS_VALUE_NONE &&
        owner->style->float_value != CSS_VALUE_FOOTNOTE && owner->style->float_value != CSS_VALUE_TOP &&
        owner->style->float_value != CSS_VALUE_BOTTOM && !paged_running_name(owner->style))
        owner->target_page = composer->page->page_number;
    TypesetMarkStore* marks = &composer->composition->marks;
    uint32_t source_id = dom_node_ref(source).expected_id;
    // Count restoration also invalidates the adapter's assignment cursor.
    if (owner->assigned_at < marks->count && marks->entries[owner->assigned_at].source.node == source_id) return TYPESET_OK;
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
        mark.page_number = composer->page->page_number; mark.at_page_start = !composer->page_has_content;
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

static float paged_vertical_top(PagedComposer* composer, ViewCssStyle* parent, ViewCssStyle* style,
        float extent, float item_baseline, float line_height, float baseline, float width) {
    LayoutContext context = {}; context.doc = composer->tree->model->document;
    context.line.parent_font_style = lam::up(&parent->font);
    context.line.parent_font_size = parent->font.font_size;
    const FontMetrics* metrics = font_get_metrics(parent->font.font_handle);
    context.line.parent_font_ascender = metrics->ascender;
    context.line.parent_font_descender = -metrics->descender;
    const CssValue* align = style->vertical_align;
    CssEnum keyword = align && align->type == CSS_VALUE_TYPE_KEYWORD ? align->data.keyword : CSS_VALUE_BASELINE;
    float offset = !align ? 0.0f : align->type == CSS_VALUE_TYPE_PERCENTAGE
        ? (float)(align->data.percentage.value * 0.01) * style->line_height
        : paged_used_length(composer, style, align, CSS_PROPERTY_VERTICAL_ALIGN, width);
    return calculate_vertical_align_offset(&context, keyword, extent, line_height, baseline, item_baseline, offset);
}

struct PagedLineMeasurement { PagedComposer* composer; PagedFlowNode* flow; float containing_height, atomic_height; };
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
    }
    ViewCssStyle* parent = measurement->flow->style;
    const FontMetrics* font = font_get_metrics(parent->font.font_handle);
    if (!font) return false;
    float baseline = font->ascender + (parent->line_height - font->ascender + font->descender) * 0.5f;
    float extent = metrics->height + metrics->depth;
    const CssValue* align = payload->style->vertical_align;
    CssEnum keyword = align && align->type == CSS_VALUE_TYPE_KEYWORD ? align->data.keyword : CSS_VALUE_BASELINE;
    float top = paged_vertical_top(measurement->composer, parent, payload->style,
        extent, metrics->baseline, parent->line_height, baseline, width);
    if (keyword == CSS_VALUE_TOP || keyword == CSS_VALUE_BOTTOM) {
        // top/bottom align to the completed line; they require height without moving its strut.
        metrics->depth = fmaxf(0.0f, parent->line_height - baseline);
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
    if (flow->has_images) {
        paragraph.context = &measurement; paragraph.measure = paged_line_item_metrics;
        if (paragraph.minimum_line_height > 0.0f) {
            const FontMetrics* font = font_get_metrics(flow->style->font.font_handle);
            if (!font) return TYPESET_UNPLACEABLE;
            paragraph.minimum_baseline = font->ascender + (flow->style->line_height - font->ascender + font->descender) * 0.5f;
        }
    }
    return typeset_next_line(&paragraph, first, width, scratch, capacity, result);
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
        if (flow->has_images && !paged_line_item_metrics(&flow->paragraph, i, frame->content_width, &item_metrics, &measurement)) return TYPESET_UNPLACEABLE;
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
                if (image->framed) { measured.width = frame->content_width; measured.height = frame->replaced_height; }
                else {
                    TypesetStatus status = paged_image_measure(composer, image, frame->content_width, frame->content_height, &measured);
                    if (status != TYPESET_OK) return status;
                }
                float box_width = measured.width + measured.box.edges[3] + measured.box.edges[1];
                float box_height = measured.height + measured.box.edges[0] + measured.box.edges[2];
                float extent = box_height + measured.margin[0] + measured.margin[2];
                float baseline = line.baseline > 0.0f ? line.baseline : line.height;
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
                text->image_box = lam::up(box);
                x += width;
                continue;
            }
            const PagedTextPaint* payload = (const PagedTextPaint*)item.paint.get();
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
            const FontMetrics* metrics = font_get_metrics(payload->style->font.font_handle);
            float font_height = metrics->ascender - metrics->descender;
            run->x = x;
            if (payload->marker_owner) {
                float anchor = frame->content_x;
                for (size_t j = composer->depth; j > 0; j--)
                    if (composer->frames[j - 1].flow == payload->marker_owner) { anchor = composer->frames[j - 1].content_x; break; }
                run->x = anchor - payload->marker_width;
                text->rect.x = run->x; text->rect.width = payload->marker_width;
            }
            const FontMetrics* parent_metrics = font_get_metrics(style->font.font_handle);
            float parent_height = parent_metrics->ascender - parent_metrics->descender;
            float baseline = line.baseline > 0.0f ? line.baseline
                : (line.height - parent_height) * 0.5f + parent_metrics->ascender;
            float top = paged_vertical_top(composer, style, payload->style, font_height,
                metrics->ascender, line.height, baseline, frame->content_width);
            run->baseline_y = composer->y + top + metrics->ascender;
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
    return view_css_page_style(composer->tree, composer->page_name, static_cast<uint32_t>(index + 1),
        paged_side(composer, index), false, style) == VIEW_MODEL_OK ? TYPESET_OK : TYPESET_INVALID;
}

static TypesetStatus paged_next_constraints(PagedComposer* composer, float* width, float* height, float* containing_height) {
    PagedComposer trial = *composer;
    if (paged_following_style(composer, &trial.page_style) != TYPESET_OK) return TYPESET_INVALID;
    float x = trial.page_style.content_rect.x;
    *width = trial.page_style.content_rect.width;
    *height = trial.page_style.content_rect.height;
    *containing_height = paged_root_height(&trial);
    for (size_t i = 0; i < composer->depth; i++) {
        PagedFrame frame = {}; frame.flow = composer->frames[i].flow;
        TypesetStatus status = paged_frame_measure(&trial, &frame, x, *width, *containing_height);
        if (status != TYPESET_OK) return status;
        x = frame.content_x; *width = frame.content_width;
        *containing_height = frame.content_height;
        if (frame.flow->style->decoration_clone) *height -= frame.box.edges[0] + frame.box.edges[2];
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
    for (size_t i = count; i > 0; i--) {
        *result = {}; result->flow = ancestors[i - 1]; *parent_width = width;
        TypesetStatus status = paged_frame_measure(composer, result, x, width, height);
        if (status != TYPESET_OK) return status;
        x = result->content_x; width = result->content_width; height = result->content_height;
    }
    return count ? TYPESET_OK : TYPESET_INVALID;
}

static TypesetStatus paged_region_measure(void* context, const TypesetResume* start,
        const TypesetRegionConstraints* constraints, bool split, Pool* scratch, TypesetRegionSlice* slice) {
    PagedRegionRecord* record = (PagedRegionRecord*)context;
    PagedComposition* composition = record->tree->model->composition;
    if (record->kind != PAGED_REGION_NOTE) {
        PagedComposer measure = {}; measure.tree = record->tree; measure.composition = composition;
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
    measure.page_style.content_rect = {0.0f, 0.0f, constraints->inline_size, constraints->available_height};
    TypesetResume end = *start;
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
        paint->lines[paint->count++] = {part.flow, line, frame.replaced_height, frame.content_height, frame.content_x, frame.content_width, height};
        height += line.height; end.state[1] = line.next; end.serial++;
    }
    bool complete = end.state[0] == record->count;
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
    TypesetStatus status = paged_box_edges(composer, style, &declarations, width, area);
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

static TypesetStatus paged_regions_trial(PagedComposer* composer, float body_height, PagedRegionTrial* trial, bool limit_notes = false) {
    float reserved = 0.0f;
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
        TypesetRegionQueue* queue = &composer->composition->queues[i];
        if (!queue->count && !trial->anchor_counts[i]) continue;
        TypesetRegionConstraints constraints = {composer->page_style.content_rect.width,
            composer->page_style.content_rect.height, composer->page->page_number, nullptr, false,
            i != PAGED_REGION_NOTE, i == PAGED_REGION_TOP, composer->page_style.content_rect.height};
        float occupied = body_height + reserved, separator = 0.0f;
        PagedNoteArea area = {};
        if (i == PAGED_REGION_NOTE) {
            TypesetStatus status = paged_note_area(composer, &area);
            if (status != TYPESET_OK) { paged_region_trial_dispose(trial); return status; }
            if (body_height > constraints.available_height) { paged_region_trial_dispose(trial); return TYPESET_UNPLACEABLE; }
            constraints.occupied = body_height > 0.0f || limit_notes;
            constraints.available_height -= body_height;
            float margins = area.margin[0] + area.margin[2];
            if (constraints.occupied) constraints.available_height = fminf(constraints.available_height, fmaxf(0.0f, area.maximum + margins));
            if (isfinite(area.preferred)) constraints.available_height = fminf(constraints.available_height, fmaxf(0.0f, area.preferred + margins));
            constraints.minimum_height = fmaxf(0.0f, (isfinite(area.preferred) ? area.preferred : area.minimum) + margins);
            constraints.inline_size = area.width - area.edges[1] - area.edges[3];
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

static TypesetStatus paged_body_translate(ViewTree* tree, LayoutViewNode* node, float dy) {
    for (; node; node = node->next_sibling) {
        if (node->role != VIEW_FRAGMENT_BODY) continue;
        if (!view_tree_model_touch_node(tree, node)) return TYPESET_OUT_OF_MEMORY;
        node->rect.y += dy;
        if (node->glyph_run) node->glyph_run->baseline_y += dy;
        if (node->image_box) {
            node->image_box->content_rect.y += dy;
            node->image_box->image_rect.y += dy;
        }
        TypesetStatus status = paged_body_translate(tree, node->first_child, dy);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_regions_accept(PagedComposer* composer, PagedRegionTrial* trial) {
    // Full-width top floats keep every inline constraint unchanged; translate only this page's body.
    float dy = trial->plans[PAGED_REGION_TOP].reserved_height - composer->regions[PAGED_REGION_TOP].plan.reserved_height;
    if (dy != 0.0f) {
        TypesetStatus status = paged_body_translate(composer->tree, composer->page->node.first_child, dy);
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

static TypesetStatus paged_region_emit(PagedComposer* composer, LayoutViewNode* parent, const TypesetRegionPlacement& placement) {
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
        frame.flow = line.flow; frame.content_x = rect.x + line.x; frame.content_width = line.width;
        frame.replaced_height = line.atomic_height;
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
    if (!active) return TYPESET_OK;
    // Declared block extents and closing decorations participate in the final reservation.
    float body_height = composer->y - composer->page_style.content_rect.y - composer->regions[PAGED_REGION_TOP].plan.reserved_height;
    TypesetStatus status = paged_regions_trial(composer, body_height, &trial);
    if (status != TYPESET_OK) {
        composer->closure_failure = status == TYPESET_UNPLACEABLE;
        return paged_failure(composer->composition, status, nullptr,
            composer->page->page_number, "closing body extents exceed the reserved page regions");
    }
    status = paged_regions_accept(composer, &trial);
    if (status != TYPESET_OK) return status;
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) {
        PagedRegionState* region = &composer->regions[i];
        TypesetRegionPlan* plan = &region->plan;
        if (!plan->scratch) continue;
        LayoutViewNode* parent = &composer->page->node;
        if (i == PAGED_REGION_NOTE && plan->count) status = paged_note_area_emit(composer, plan, &parent);
        for (size_t j = 0; status == TYPESET_OK && j < plan->count; j++) status = paged_region_emit(composer, parent, plan->placements[j]);
        if (status == TYPESET_OK) status = typeset_region_commit(&composer->composition->queues[i], plan);
        typeset_region_plan_dispose(plan); region->anchor_count = 0;
    }
    return status;
}

static TypesetStatus paged_regions_close(PagedComposer* composer) {
    PagedCheckpoint checkpoint;
    TypesetStatus status = checkpoint.begin(composer);
    if (status != TYPESET_OK) return status;
    status = paged_regions_close_trial(composer);
    return status == TYPESET_OK ? checkpoint.accept() : checkpoint.fail(status);
}

static float paged_tail_edges(const PagedComposer* composer, bool closes) {
    float extent = 0.0f;
    for (size_t i = composer->depth; i > 0; i--) {
        const PagedFrame& frame = composer->frames[i - 1];
        if (closes || frame.flow->style->decoration_clone) extent += frame.box.edges[2];
        closes = closes && !frame.flow->next;
    }
    return extent;
}

static TypesetStatus paged_regions_for_lines(PagedComposer* composer, PagedFlowNode* flow,
        const TypesetLineCandidate* lines, size_t fit, PagedRegionTrial* trial) {
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) trial->anchor_counts[i] = composer->regions[i].anchor_count;
    float body_height = composer->y - composer->page_style.content_rect.y - composer->regions[PAGED_REGION_TOP].plan.reserved_height;
    for (size_t i = 0; i < fit; i++) {
        body_height += lines[i].height;
        for (size_t j = lines[i].first; j < lines[i].next; j++) {
            const PagedSourceRecord* source = (const PagedSourceRecord*)flow->items[j].source.native.get();
            if (!source || !source->insertion) continue;
            size_t kind = source->insertion->kind;
            PagedRegionState* region = &composer->regions[kind];
            size_t* count = &trial->anchor_counts[kind];
            if (!lam::pool_grow_array(composer->composition->pool, &region->anchors,
                &region->anchor_capacity, *count + 1, 16)) return TYPESET_OUT_OF_MEMORY;
            region->anchors[(*count)++] = &source->insertion->material;
        }
    }
    if (fit) body_height += paged_tail_edges(composer,
        lines[fit - 1].next == flow->paragraph.count && !flow->next);
    return paged_regions_trial(composer, body_height, trial);
}

static TypesetStatus paged_flow_cursor_copy(void*, const TypesetResume* cursor, TypesetResume* copied) {
    *copied = *cursor; return TYPESET_OK;
}
static size_t paged_choose_last(void*, const TypesetPageCandidate*, size_t count) { return count - 1; }
static TypesetAssemblyAction paged_finalize(void*, const TypesetPageCandidate*) { return TYPESET_ASSEMBLY_FINALIZE; }

static size_t paged_block_note_prefix(PagedFlowNode* flow, const TypesetLineCandidate* lines, size_t count) {
    size_t prefix = 0;
    for (size_t i = 0; i < count; i++) for (size_t j = lines[i].first; j < lines[i].next; j++) {
        const PagedSourceRecord* source = (const PagedSourceRecord*)flow->items[j].source.native.get();
        if (source && source->insertion && source->insertion->block_policy) prefix = i + 1;
    }
    return prefix;
}

static TypesetStatus paged_flow_height(PagedComposer* composer, PagedFlowNode* flow,
        float parent_width, float parent_height, bool leading, float* result, bool* forced, bool allow_overflow) {
    *result = 0.0f;
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
            *result += line.height; first = line.next; count++;
        }
        pool_free(pool, scratch);
        return status;
    }
    PagedFrame frame = {}; frame.flow = flow;
    TypesetStatus status = paged_frame_measure(composer, &frame, 0.0f, parent_width, parent_height);
    if (status != TYPESET_OK) return status;
    if (flow->image) {
        *result = frame.box.edges[0] + frame.replaced_height + frame.box.edges[2];
        return TYPESET_OK;
    }
    float height = frame.box.edges[0], pending = 0.0f;
    for (PagedFlowNode* child = flow->first_child; child; child = child->next) {
        if (child->style->break_before >= VIEW_BREAK_PAGE || child->style->break_after >= VIEW_BREAK_PAGE) *forced = true;
        float top = child->kind == PAGED_FLOW_BLOCK ? paged_used_length(composer, child->style,
            child->style->margin[0], CSS_PROPERTY_MARGIN_TOP, frame.content_width) : 0.0f;
        height += layout_collapse_margins(pending, top);
        float child_height = 0.0f;
        status = paged_flow_height(composer, child, frame.content_width, frame.content_height, leading, &child_height, forced, allow_overflow);
        if (status != TYPESET_OK) return status;
        height += child_height;
        pending = child->kind == PAGED_FLOW_BLOCK ? paged_used_length(composer, child->style,
            child->style->margin[2], CSS_PROPERTY_MARGIN_BOTTOM, frame.content_width) : 0.0f;
        if (leading && child_height > 0.0f) break;
    }
    *result = frame.box.edges[0] + fmaxf(height - frame.box.edges[0], frame.content_height) + frame.box.edges[2];
    return TYPESET_OK;
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
    float top = paged_used_length(composer, flow->style, flow->style->margin[0], CSS_PROPERTY_MARGIN_TOP, parent_width);
    composer->y += layout_collapse_margins(composer->pending_margin, top);
    composer->pending_margin = 0.0f;
    if (!lam::pool_grow_array(composer->composition->pool, &composer->frames, &composer->frame_capacity,
                              composer->depth + 1, 16)) return TYPESET_OUT_OF_MEMORY;
    size_t index = composer->depth++;
    composer->frames[index] = {}; composer->frames[index].flow = flow;
    return paged_frame_open(composer, index, true);
}

static float paged_block_remaining(PagedComposer* composer) {
    PagedFrame* frame = &composer->frames[composer->depth - 1];
    ViewCssStyle* style = frame->flow->style;
    float top_edge = frame->fragment->first_fragment || style->decoration_clone ? frame->box.edges[0] : 0.0f;
    float consumed = frame->consumed_content + composer->y - frame->page_start - top_edge;
    return fmaxf(0.0f, frame->content_height - consumed);
}

static TypesetStatus paged_block_finish(PagedComposer* composer, float parent_width) {
    PagedFrame* frame = &composer->frames[composer->depth - 1];
    PagedFlowNode* flow = frame->flow;
    ViewCssStyle* style = flow->style;
    TypesetStatus status = TYPESET_OK;
    if (composer->role == VIEW_FRAGMENT_BODY && composer->tree->model->environment.presentation == VIEW_PRESENTATION_PAGED) {
        // closing a sibling can expose ancestor edges that no paragraph candidate could reserve.
        float body_height = composer->y - composer->page_style.content_rect.y -
            composer->regions[PAGED_REGION_TOP].plan.reserved_height + paged_tail_edges(composer, true);
        PagedRegionTrial trial = {};
        for (size_t i = 0; i < PAGED_REGION_COUNT; i++) trial.anchor_counts[i] = composer->regions[i].anchor_count;
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
    composer->pending_margin = paged_used_length(composer, style, style->margin[2], CSS_PROPERTY_MARGIN_BOTTOM, parent_width);
    if (composer->role == VIEW_FRAGMENT_BODY && style->break_after >= VIEW_BREAK_PAGE) {
        composer->pending_break = true; composer->requested_break = style->break_after;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_block_before(PagedComposer* composer, PagedFlowNode* flow, bool* forced) {
    if (forced) *forced = false;
    ViewCssStyle* style = flow->style;
    bool has_content = composer->page && composer->page_has_content;
    const char* name = nullptr;
    for (ViewCssStyle* owner = style; owner; owner = owner->parent) {
        if (owner->page_name) { name = owner->page_name; break; }
    }
    bool changed_name = (name == nullptr) != (composer->page_name == nullptr) ||
        (name && composer->page_name && strcmp(name, composer->page_name) != 0);
    if (changed_name) composer->page_name = name;
    if (style->break_before >= VIEW_BREAK_PAGE) {
        composer->pending_break = true; composer->requested_break = style->break_before;
    }
    if (!composer->page && composer->tree->model->environment.presentation == VIEW_PRESENTATION_PAGED) {
        TypesetStatus status = paged_next_page(composer);
        if (status != TYPESET_OK) return status;
    } else if ((composer->pending_break || changed_name) && has_content) {
        if (forced) { *forced = true; return TYPESET_OK; }
        TypesetStatus status = paged_next_page(composer);
        if (status != TYPESET_OK) return status;
    } else if (composer->page && !has_content && (composer->pending_break || changed_name)) {
        TypesetStatus status = changed_name ? paged_empty_page_restyle(composer, false) : TYPESET_OK;
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

enum PagedFlowStepKind : uint8_t { PAGED_STEP_BEFORE, PAGED_STEP_OPEN, PAGED_STEP_LINE, PAGED_STEP_CLOSE };
struct PagedFlowStep { PagedFlowStepKind kind; PagedFlowNode* flow; };
enum PagedRelaxation : uint32_t { PAGED_RELAX_MINIMA = 1, PAGED_RELAX_AVOIDANCE = 2 };

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

    ~PagedPageProvider() {
        Pool* pool = composer->composition->pool;
        if (steps) pool_free(pool, steps);
        if (scratch) pool_free(pool, scratch);
        if (lines) pool_free(pool, lines);
    }

    TypesetStatus append(PagedFlowStepKind kind, PagedFlowNode* flow) {
        if (!lam::pool_grow_array(composer->composition->pool, &steps, &capacity, count + 1, 32))
            return TYPESET_OUT_OF_MEMORY;
        steps[count++] = {kind, flow};
        return TYPESET_OK;
    }

    TypesetStatus flatten(PagedFlowNode* flow) {
        if (flow->kind == PAGED_FLOW_PARAGRAPH) return append(PAGED_STEP_LINE, flow);
        TypesetStatus status = append(PAGED_STEP_BEFORE, flow);
        if (status == TYPESET_OK) status = append(PAGED_STEP_OPEN, flow);
        for (PagedFlowNode* child = flow->first_child; status == TYPESET_OK && child; child = child->next)
            status = flatten(child);
        return status == TYPESET_OK ? append(PAGED_STEP_CLOSE, flow) : status;
    }

    TypesetStatus line_storage(PagedFlowNode* flow) {
        size_t needed = flow->paragraph.count;
        Pool* pool = composer->composition->pool;
        return lam::pool_grow_array(pool, &scratch, &line_capacity, needed, 16) &&
            lam::pool_grow_array(pool, &lines, &lines_capacity, needed, 16) ? TYPESET_OK : TYPESET_OUT_OF_MEMORY;
    }
};

static bool paged_sibling_avoidance(PagedFlowNode* flow) {
    return flow->next && (flow->style->break_after == VIEW_BREAK_AVOID ||
        flow->next->style->break_before == VIEW_BREAK_AVOID);
}

static TypesetStatus paged_page_line(PagedPageProvider* provider, PagedFlowNode* flow,
        const TypesetResume* cursor, TypesetResume* next, TypesetContribution* contribution) {
    PagedComposer* composer = provider->composer;
    size_t first = cursor->state[1];
    if (first == flow->paragraph.count) { next->state[0]++; next->state[1] = next->state[2] = 0; return TYPESET_OK; }
    TypesetStatus status = provider->line_storage(flow);
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
            if (paged_sibling_avoidance(ending)) contribution->boundary.reason |= PAGED_RELAX_AVOIDANCE;
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
    *next = *cursor; next->serial++; next->state[0]++;
    *contribution = {}; contribution->kind = TYPESET_CONTRIBUTION_BOUNDARY;
    contribution->payload = &step;
    contribution->boundary = {TYPESET_BREAK_FORBIDDEN, TYPESET_BREAK_PAGE, 0, 0};
    contribution->source = {composer->tree->model->tree_id, composer->tree->layout_generation,
        dom_node_ref(flow->source).expected_id, TYPESET_PROVIDER_OFFSETS, nullptr};
    TypesetStatus status = TYPESET_OK;
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
        case PAGED_STEP_CLOSE: {
            float parent_width = composer->depth > 1 ? composer->frames[composer->depth - 2].content_width : composer->page_style.content_rect.width;
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
            size_t following = next->state[0];
            while (following < provider->count && provider->steps[following].kind == PAGED_STEP_CLOSE) following++;
            // trailing forced breaks do not defer the enclosing closures to an empty sheet.
            contribution->boundary.legality = composer->pending_break && following < provider->count ?
                TYPESET_BREAK_FORCED : TYPESET_BREAK_ALLOWED;
            if (paged_sibling_avoidance(flow)) contribution->boundary.reason |= PAGED_RELAX_AVOIDANCE;
            break;
        }
    }
    if (status == TYPESET_UNPLACEABLE) {
        // keep earlier candidates; the overflowing trial is discarded by the start checkpoint.
        provider->blocked = status; provider->failed_source = flow->source;
        if (composer->composition->diagnostic.status != TYPESET_OK)
            provider->hard_failure = composer->composition->diagnostic;
        provider->failed_reason = step.kind == PAGED_STEP_CLOSE ?
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
    } else {
        for (size_t i = 0; i < composer->depth; i++)
            if (composer->frames[i].flow->style->break_inside == VIEW_BREAK_AVOID)
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
    float body = composer->y - composer->page_style.content_rect.y - composer->regions[PAGED_REGION_TOP].plan.reserved_height;
    body += paged_tail_edges(composer, false);
    PagedRegionTrial trial = {};
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) trial.anchor_counts[i] = composer->regions[i].anchor_count;
    TypesetStatus status = paged_regions_trial(composer, body, &trial);
    if (status == TYPESET_OK) {
        candidate->body_height = fmaxf(0.0f, body);
        candidate->note_height = trial.plans[PAGED_REGION_NOTE].reserved_height;
        candidate->float_height = trial.plans[PAGED_REGION_TOP].reserved_height + trial.plans[PAGED_REGION_BOTTOM].reserved_height;
        candidate->available_height = composer->page_style.content_rect.height;
    }
    paged_region_trial_dispose(&trial);
    if (status == TYPESET_UNPLACEABLE || body > composer->page_style.content_rect.height) {
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
    return provider->checkpoint.accept();
}

static TypesetStatus paged_layout_pages(PagedComposer* composer, PagedFlowNode* root) {
    PagedCheckpoint document;
    TypesetStatus status = document.begin(composer);
    if (status != TYPESET_OK) return status;
    PagedPageProvider context = {}; context.composer = composer;
    status = context.flatten(root);
    if (status == TYPESET_OK) status = paged_next_page(composer);
    TypesetFlowProvider provider = {composer->tree->model->tree_id, composer->tree->layout_generation,
        &context, paged_page_next, paged_flow_cursor_copy, paged_page_restore};
    TypesetResume cursor = {provider.identity, provider.generation, 0, {}}, next = {};
    TypesetPagePolicy policy = {&context, paged_choose_last, paged_finalize, paged_page_committed};
    TypesetPageProbe probe = {&context, paged_page_probe};
    TypesetPageConstraints constraints = {INFINITY, composer->composition->options.max_items,
        composer->composition->options.max_items};
    while (status == TYPESET_OK && cursor.state[0] < context.count) {
        context.start = context.active = cursor; context.blocked = TYPESET_OK;
        context.hard_failure = {};
        context.failed_source = nullptr; context.failed_reason = nullptr;
        context.relax_minima = context.relax_avoidance = false;
        Pool* scratch = mem_pool_create((MemContext*)composer->tree->model->document->services.mem_ctx,
            MEM_ROLE_LAYOUT, "typeset.html.whole-page");
        if (!scratch) { status = TYPESET_OUT_OF_MEMORY; break; }
        TypesetPagePlan plan = {};
        status = typeset_page_plan(&provider, &cursor, &constraints, &probe, &policy, scratch, &plan);
        if (status == TYPESET_UNPLACEABLE && !paged_regions_placed(composer)) {
            context.relax_minima = true;
            status = typeset_page_plan(&provider, &cursor, &constraints, &probe, &policy, scratch, &plan);
            if (status == TYPESET_UNPLACEABLE) {
                context.relax_avoidance = true;
                status = typeset_page_plan(&provider, &cursor, &constraints, &probe, &policy, scratch, &plan);
            }
        }
        if (status == TYPESET_OK) status = typeset_page_commit(&plan, &next);
        mem_pool_destroy(scratch);
        if (status == TYPESET_UNPLACEABLE && paged_regions_placed(composer)) {
            // an insertion-only page advances its queue without advancing the body continuation.
            status = paged_next_page(composer); cursor.state[2] = 0; continue;
        }
        if (status != TYPESET_OK) break;
        if (next.serial <= cursor.serial) { status = TYPESET_NO_PROGRESS; break; }
        cursor = next;
        if (cursor.state[0] < context.count) {
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
    paged_failure(composer->composition, status, context.failed_source ? context.failed_source : root->source,
        composer->page ? composer->page->page_number : 0,
        context.failed_reason ? context.failed_reason : "whole-page flow could not be composed");
    return document.fail(status);
}

struct PagedMarginIntrinsic { float minimum, maximum; bool generated; };

static TypesetStatus paged_fixed_flow(PagedComposer* composer, PagedFlowNode* flow) {
    if (flow->kind == PAGED_FLOW_PARAGRAPH) {
        size_t capacity = flow->paragraph.count;
        if (!capacity) return TYPESET_OK;
        TypesetLineCandidate* scratch = (TypesetLineCandidate*)pool_alloc(composer->composition->pool,
            capacity * sizeof(TypesetLineCandidate));
        if (!scratch) return TYPESET_OUT_OF_MEMORY;
        TypesetStatus status = TYPESET_OK;
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
    for (PagedFlowNode* child = flow->first_child; status == TYPESET_OK && child; child = child->next)
        status = paged_fixed_flow(composer, child);
    if (status != TYPESET_OK) return status;
    composer->y += paged_block_remaining(composer);
    return paged_block_finish(composer, parent_width);
}

static TypesetStatus paged_layout_continuous(PagedComposer* composer, PagedFlowNode* root) {
    PagedCheckpoint checkpoint;
    TypesetStatus status = checkpoint.begin(composer);
    if (status == TYPESET_OK) status = paged_fixed_flow(composer, root);
    return status == TYPESET_OK ? checkpoint.accept() : checkpoint.fail(status);
}

static float paged_margin_pair(PagedMarginIntrinsic a, PagedMarginIntrinsic b, float available) {
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
        bool vertical, float fixed, PagedMarginIntrinsic* result) {
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
    } else {
        if (flow->image) {
            PagedFrame frame = {}; frame.flow = flow;
            TypesetStatus status = paged_frame_measure(composer, &frame, 0.0f, composer->page_style.content_rect.width, paged_root_height(composer));
            if (status != TYPESET_OK) return status;
            result->minimum = result->maximum = frame.width;
            return TYPESET_OK;
        }
        if (flow->kind == PAGED_FLOW_BLOCK) {
            for (PagedFlowNode* child = flow->first_child; child; child = child->next) {
                PagedMarginIntrinsic intrinsic = {};
                TypesetStatus status = paged_margin_intrinsic(composer, child, false, fixed, &intrinsic);
                if (status != TYPESET_OK) return status;
                result->minimum = fmaxf(result->minimum, intrinsic.minimum);
                result->maximum = fmaxf(result->maximum, intrinsic.maximum);
            }
            float parent_width = composer->page_style.content_rect.width;
            float specified = paged_used_length(composer, flow->style, flow->style->width, CSS_PROPERTY_WIDTH, parent_width, NAN);
            if (isfinite(specified)) result->minimum = result->maximum = specified;
            PagedBoxEdges box = {};
            TypesetStatus status = paged_box_edges(composer, flow->style, nullptr, parent_width, &box);
            if (status != TYPESET_OK) return status;
            float edges = box.edges[3] + box.edges[1];
            if (!isfinite(specified) || flow->style->box_sizing != CSS_VALUE_BORDER_BOX) {
                result->minimum += edges; result->maximum += edges;
            }
            return TYPESET_OK;
        }
        float line = 0.0f;
        for (size_t i = 0; i < flow->paragraph.count; i++) {
            const TypesetItem& item = flow->items[i];
            TypesetMetrics metrics = item.metrics;
            PagedLineMeasurement measurement = {composer, flow, paged_root_height(composer), NAN};
            if (flow->has_images && !paged_line_item_metrics(&flow->paragraph, i,
                composer->page_style.content_rect.width, &metrics, &measurement)) return TYPESET_UNPLACEABLE;
            if (item.kind == TYPESET_BOX) { line += metrics.advance; result->minimum = fmaxf(result->minimum, metrics.advance); }
            else if (item.kind == TYPESET_GLUE) line += item.glue.natural;
            else if (item.boundary.legality == TYPESET_BREAK_FORCED) { result->maximum = fmaxf(result->maximum, line); line = 0.0f; }
        }
        result->maximum = fmaxf(result->maximum, line);
    }
    return TYPESET_OK;
}

static TypesetStatus paged_margin_group(PagedComposer* composer, PagedFlowNode** flows,
        const CssPageMarginBox boxes[3], RdtLogicalRect band, bool vertical, RdtLogicalRect* rects) {
    float available = vertical ? band.height : band.width, fixed = vertical ? band.width : band.height;
    PagedMarginIntrinsic intrinsic[3] = {};
    for (size_t i = 0; i < 3; i++) {
        TypesetStatus status = paged_margin_intrinsic(composer, flows[boxes[i]], vertical, fixed, &intrinsic[i]);
        if (status != TYPESET_OK) return status;
    }
    float sizes[3] = {};
    if (intrinsic[1].generated) {
        PagedMarginIntrinsic sides = {2.0f * fmaxf(intrinsic[0].minimum, intrinsic[2].minimum),
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
        TypesetStatus status = paged_flow_height(composer, flow, rect.width, rect.height, false, &content_height, &forced, true);
        PagedComposer margin = {}; margin.tree = composer->tree; margin.composition = composer->composition;
        margin.page_style = *page_style; margin.page = page;
        margin.role = flow->kind == PAGED_FLOW_BLOCK ? VIEW_FRAGMENT_RUNNING : VIEW_FRAGMENT_MARGIN;
        margin.depth = 1;
        if (!lam::pool_grow_array(pool, &margin.frames, &margin.frame_capacity, 1, 16)) return TYPESET_OUT_OF_MEMORY;
        margin.frames[0] = {}; margin.frames[0].flow = flow; margin.frames[0].fragment = box;
        margin.frames[0].content_x = rect.x; margin.frames[0].content_width = rect.width;
        margin.frames[0].content_height = rect.height;
        margin.y = rect.y + (rect.height - content_height) * 0.5f;
        if (status == TYPESET_OK) status = paged_fixed_flow(&margin, flow);
        pool_free(pool, margin.frames);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

static TypesetStatus paged_layout_pass(ViewTree* tree, const PagedLayoutOptions* options,
                                     PagedReferenceSession* references) {
    PagedComposition* composition = (PagedComposition*)pool_calloc(tree->prop_pool, sizeof(PagedComposition));
    if (!composition) return TYPESET_OUT_OF_MEMORY;
    tree->model->composition = lam::own(composition);
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
    composition->counters = counter_context_create(tree->layout_pass_arena);
    if (!composition->counters) return TYPESET_OUT_OF_MEMORY;
    PagedCounterScope root_scope(tree, composition, style);
    if (root_scope.status != TYPESET_OK) return root_scope.status;
    TypesetStatus mark_status = paged_mark_register(composition, style);
    if (mark_status != TYPESET_OK) return mark_status;
    composition->root = paged_flow_new(composition, PAGED_FLOW_BLOCK, root, style);
    if (!composition->root) return TYPESET_OUT_OF_MEMORY;
    PagedFlowNode* paragraph = nullptr;
    TypesetStatus status = paged_build_children(tree, composition, root, composition->root, &paragraph, 0);
    PagedComposer composer = {}; composer.tree = tree; composer.composition = composition;
    if (status == TYPESET_OK && tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) status = paged_page_create(&composer, false);
    if (status == TYPESET_OK) status = tree->model->environment.presentation == VIEW_PRESENTATION_PAGED ?
        paged_layout_pages(&composer, composition->root) : paged_layout_continuous(&composer, composition->root);
    if (status == TYPESET_OK && !composer.page && tree->model->environment.presentation == VIEW_PRESENTATION_PAGED) status = paged_next_page(&composer);
    if (status == TYPESET_OK) status = paged_regions_close(&composer);
    while (status == TYPESET_OK && paged_regions_pending(&composer)) {
        status = paged_next_page(&composer);
        if (status == TYPESET_OK) status = paged_regions_close(&composer);
    }
    for (size_t i = 0; i < PAGED_REGION_COUNT; i++) typeset_region_plan_dispose(&composer.regions[i].plan);
    for (size_t i = 0; status == TYPESET_OK && i < tree->model->page_count; i++)
        status = paged_margin_layout(&composer, tree->model->pages.get()[i]);
    if (status == TYPESET_OK) {
        if (tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
            tree->model->root->rect = {0.0f, 0.0f, tree->model->environment.viewport_width, composer.y};
        }
    }
    if (status != TYPESET_OK && composition->diagnostic.status == TYPESET_OK) paged_failure(composition, status, root,
        composer.page ? composer.page->page_number : 0, "secondary flow could not be composed");
    return status;
}

static uint32_t paged_target_page(ViewTree* tree, DomElement* source) {
    PagedMarkSource key = {}; key.source = source;
    PagedMarkSource* mark = (PagedMarkSource*)hashmap_get(tree->model->composition->mark_sources, &key);
    if (mark && mark->target_page) return mark->target_page;
    ViewNodeState* state = view_tree_node_state(tree, source, false);
    // Extracted notes/floats bind to their placed region, not their body anchor.
    ViewCssStyle* style = state ? state->computed_style.get() : nullptr;
    ViewFragmentRole role = style && style->float_value == CSS_VALUE_FOOTNOTE ? VIEW_FRAGMENT_NOTE : VIEW_FRAGMENT_FLOAT;
    for (LayoutViewNode* node = state ? state->first_occurrence.get() : nullptr; node; node = node->next_occurrence) {
        if (node->role != role || !node->paint_box) continue;
        LayoutViewNode* page = node->parent;
        while (page && page->kind != LAYOUT_VIEW_PAGE) page = page->parent;
        if (page) return ((ViewPageBox*)page)->page_number;
    }
    return 0;
}

static TypesetStatus paged_targets_capture(ViewTree* tree, PagedReferenceSession* session) {
    TypesetTargetStore next = {tree->model->tree_id, tree->layout_generation, session->pool,
        nullptr, 0, 0, session->targets.limit, nullptr};
    TypesetStatus status = TYPESET_OK;
    for (size_t i = 0; status == TYPESET_OK && i < session->targets.count; i++) {
        const TypesetTarget& previous = session->targets.entries[i];
        const PagedTargetValue* old = (const PagedTargetValue*)previous.value.get();
        DomNode* node = dom_node_ref_validate(tree->model->document, old->source);
        if (!node || !node->is_element()) { status = TYPESET_STALE; break; }
        DomElement* source = node->as_element();
        ViewCssStyle* style = view_css_resolve(tree, source);
        PagedTargetValue* value = (PagedTargetValue*)pool_calloc(session->pool, sizeof(PagedTargetValue));
        if (!style || !value) { status = TYPESET_OUT_OF_MEMORY; break; }
        value->provider = tree->model->tree_id; value->source = old->source; value->text = old->text;
        value->counters = counter_snapshot_copy(style->counters, session->pool);
        if (!value->counters) { status = TYPESET_OUT_OF_MEMORY; break; }
        const uint8_t pseudos[] = {PSEUDO_ELEMENT_BEFORE, PSEUDO_ELEMENT_AFTER};
        const char** parts[] = {&value->before, &value->after};
        for (size_t j = 0; j < 2; j++) {
            ViewCssStyle* pseudo = view_css_resolve_pseudo(tree, source, pseudos[j]);
            if (!pseudo) { status = TYPESET_OUT_OF_MEMORY; break; }
            *parts[j] = pool_strdup(session->pool, pseudo->generated_text ? pseudo->generated_text.get() : "");
            if (!*parts[j]) { status = TYPESET_OUT_OF_MEMORY; break; }
        }
        if (status != TYPESET_OK) break;
        TypesetTarget target = {previous.name, {tree->model->tree_id, tree->layout_generation,
            value->source.expected_id, TYPESET_PROVIDER_OFFSETS, nullptr}, paged_target_page(tree, source),
            lam::up((const TypesetRecord*)value)};
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
static TypesetStatus paged_reference_observe(ViewTree* tree, PagedReferenceSession* session, bool* settled) {
    StrBuf* signature = strbuf_new();
    if (!signature) return TYPESET_OUT_OF_MEMORY;
    paged_signature_value(signature, tree->model->page_count);
    for (size_t i = 0; i < tree->model->page_count; i++) {
        ViewPageBox* page = tree->model->pages.get()[i];
        paged_signature_rect(signature, page->content_rect);
        paged_signature_value(signature, page->side); paged_signature_value(signature, page->blank);
        paged_signature_text(signature, page->name);
    }
    paged_signature_value(signature, tree->model->node_count);
    for (size_t i = 0; i < tree->model->node_id_count; i++) {
        LayoutViewNode* node = tree->model->nodes.get()[i];
        if (!node) continue;
        paged_signature_value(signature, node->source.expected_id); paged_signature_value(signature, node->role);
        paged_signature_rect(signature, node->rect);
        paged_signature_value(signature, node->text_start); paged_signature_value(signature, node->text_length);
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
        const PagedTargetValue* value = (const PagedTargetValue*)target.value.get();
        paged_signature_text(signature, target.name); paged_signature_value(signature, target.page_number);
        paged_signature_text(signature, value->text); paged_signature_text(signature, value->before);
        paged_signature_text(signature, value->after); paged_signature_value(signature, value->counters->count);
        for (size_t j = 0; j < value->counters->count; j++) {
            paged_signature_text(signature, value->counters->entries[j].name);
            paged_signature_value(signature, value->counters->entries[j].value);
        }
    }
    TypesetStatus status = typeset_convergence_observe(&session->convergence, signature->str, signature->length, settled);
    strbuf_free(signature);
    return status;
}

TypesetStatus layout_secondary_view(ViewTree* tree, const PagedLayoutOptions* options,
                                    PagedLayoutDiagnostic* diagnostic) {
    if (diagnostic) *diagnostic = {};
    if (!tree || !tree->model || !options || !options->max_pages || !options->max_nodes ||
        !options->max_items || !options->max_block_trials || !options->max_depth || !options->max_reference_passes ||
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
    size_t visited = 0;
    TypesetStatus status = paged_targets_seed(tree, &session, tree->model->document->root, 0, &visited, options);
    bool settled = false;
    while (status == TYPESET_OK && !settled) {
        session.pass++;
        status = paged_layout_pass(tree, options, &session);
        PagedComposition* composition = tree->model->composition;
        if (status == TYPESET_OK) {
            status = paged_targets_capture(tree, &session);
            if (status == TYPESET_OK && composition->reference_used) status = paged_reference_observe(tree, &session, &settled);
            else if (status == TYPESET_OK) settled = true;
            if (status != TYPESET_OK) paged_failure(composition, status, composition->reference_source.address, 0,
                status == TYPESET_NO_PROGRESS ? "reference pagination repeated a nonconverged state" :
                status == TYPESET_BUDGET_EXHAUSTED ? "reference pagination exhausted its pass budget" : "target bindings could not be retained");
        }
        if (composition) {
            composition->diagnostic.reference_passes = session.pass;
            if (diagnostic) *diagnostic = composition->diagnostic;
            composition->references = nullptr;
        }
        if (status == TYPESET_OK && !settled) tree->reset_retained();
    }
    if (status == TYPESET_OK && session.targets.count) {
        // Settled anchor bindings belong to this view generation, alongside its fragments.
        tree->model->composition->targets = session.targets;
        tree->model->composition->reference_pool = session.pool;
        session.targets = {}; session.pool = nullptr;
    }
    if (status == TYPESET_OK) status = view_tree_model_commit(tree) ? TYPESET_OK : TYPESET_INVALID;
    if (diagnostic && diagnostic->status == TYPESET_OK) diagnostic->status = status;
    return status;
}

const TypesetTarget* layout_secondary_target(ViewTree* tree, const char* id) {
    if (!view_tree_model_source_valid(tree) || !tree->model->committed) return nullptr;
    if (tree->model->page_instances) tree = view_tree_page_content_owner(tree);
    if (!tree || !tree->model->composition) return nullptr;
    return typeset_target_find(&tree->model->composition->targets, id);
}

static bool paged_paint_node(LayoutViewNode* node, PaintList* paint) {
    if (node->clip_content) {
        RdtPath* clip = rdt_path_new();
        if (!clip) return false;
        rdt_path_add_rect(clip, node->rect.x, node->rect.y, node->rect.width, node->rect.height, 0.0f, 0.0f);
        paint_push_clip(paint, clip, nullptr); rdt_path_free(clip);
    }
    if (node->glyph_run) paint_glyph_run(paint, node->glyph_run);
    else if (node->paint_box && node->computed_boundary) {
        if (!render_paint_boundary_emit_box(paint, node->computed_boundary,
            {node->rect.x, node->rect.y, node->rect.width, node->rect.height})) return false;
    }
    else if (node->paint_box && (node->computed_style || (node->state && node->state->computed_style))) {
        ViewCssStyle* style = node->computed_style ? node->computed_style.get() : node->state->computed_style.get();
        if (style->background.a) paint_fill_rect(paint, node->rect.x, node->rect.y, node->rect.width, node->rect.height, style->background);
    }
    if (node->image_box && !render_paint_image_box(paint, node->image_box)) return false;
    for (LayoutViewNode* child = node->first_child; child; child = child->next_sibling)
        if (!paged_paint_node(child, paint)) return false;
    if (node->clip_content) paint_pop_clip(paint);
    return true;
}

bool layout_secondary_paint_page(ViewTree* tree, const ViewPageBox* page, PaintList* paint) {
    if (!paint) return false;
    page = view_tree_page_material(tree, page);
    if (!page) return false;
    Color white = {.r = 255, .g = 255, .b = 255, .a = 255};
    paint_fill_rect(paint, 0.0f, 0.0f, page->node.rect.width, page->node.rect.height, page->style ? page->style->background : white);
    RdtPath* clip = rdt_path_new();
    if (!clip) return false;
    rdt_path_add_rect(clip, 0.0f, 0.0f, page->node.rect.width, page->node.rect.height, 0.0f, 0.0f);
    paint_push_clip(paint, clip, nullptr);
    rdt_path_free(clip);
    for (LayoutViewNode* child = page->node.first_child; child; child = child->next_sibling)
        if ((!page->blank || child->role == VIEW_FRAGMENT_MARGIN) && !paged_paint_node(child, paint)) return false;
    paint_pop_clip(paint);
    return true;
}

bool layout_secondary_paint_root(ViewTree* tree, PaintList* paint) {
    if (!paint || !view_tree_model_source_valid(tree) || !tree->model->committed) return false;
    if (tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
        return paged_paint_node(tree->model->root, paint);
    }
    for (size_t i = 0; i < tree->model->placement_count; i++) {
        const ViewPagePlacement& placement = tree->model->placements.get()[i];
        LayoutViewNode* node = view_tree_node_resolve(tree, placement.page);
        if (!node) return false;
        RdtMatrix matrix = rdt_matrix_identity();
        matrix.e11 = matrix.e22 = placement.scale; matrix.e13 = placement.rect.x; matrix.e23 = placement.rect.y;
        paint_push_transform(paint, &matrix);
        if (!layout_secondary_paint_page(tree, (ViewPageBox*)node, paint)) return false;
        paint_pop_transform(paint);
    }
    return true;
}
