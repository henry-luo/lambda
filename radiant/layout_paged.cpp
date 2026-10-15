#include "layout_paged.hpp"
#include "page_document.hpp"
#include "layout.hpp"
#include "layout_table.hpp"
#include "render.hpp"
#include "typeset_marks.hpp"
#include "../lib/font/font.h"
#include "../lib/mem_factory.h"
#include "../lib/mem_grow.hpp"
#include "../lib/ref_count.h"
#include "../lib/utf.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/str.h"
#include "../lib/tagged.hpp"
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <new>

struct PagedIntrinsic { float minimum, maximum; bool generated; };





// a running element, which margin boxes lay out with the browsing engine
struct PagedRunningRecord : TypesetRecord { DomElement* element; };
struct PagedFlowString { const char* name; const char* text; };
struct PagedMarkSource {
    DomNode* source;
    size_t assigned_at;
    PagedRunningRecord* running;
    uint32_t target_page;
    uint32_t last_target_page;
    uint32_t generated_pages[3][2];
    // the page flow's record: named strings evaluated where the layout met the
    // source (a display:none root holds its subtree's), the running name of a
    // skipped running element, and a target's counters at the target
    PagedFlowString* strings;
    size_t string_count, string_capacity;
    const char* running_name;
    CounterSnapshot* counters;
    bool target;
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
// the page a sequence with terminal masters ended on in the previous pass, keyed by
// the sequence's source (each pass compiles the page document again)
struct PagedTerminalPage { const DomNode* sequence; uint32_t page; const DomNode* tail; };
// widths discovered by pagination are replayed through layout, including line wrapping.
struct PagedFlowWidth {
    DomNode* source;
    float containing_width;
    float* lines;
    size_t count, capacity;
    uint32_t seen_pass;
};
struct PagedReferenceSession {
    Pool* pool;
    TypesetTargetStore targets;
    TypesetConvergence convergence;
    size_t page_count;
    uint32_t pass;
    const char** page_labels;
    size_t page_label_count;
    PagedTerminalPage* terminals;
    size_t terminal_count, terminal_capacity;
    PagedFlowWidth* widths;
    size_t width_count, width_capacity;
    struct PagedFolioPage { const DomElement* folio; uint32_t page; }* folios; // body folios' pages in the previous pass
    size_t folio_count, folio_capacity;
    ~PagedReferenceSession() {
        typeset_targets_dispose(&targets);
        if (pool) mem_pool_destroy(pool);
    }
};
HASHMAP_DEFINE_PTRKEY(paged_mark_sources, PagedMarkSource, source)

struct PagedPaintRecord {
    PaintList paint;
    PagedPaintRecord* next;
};

struct PagedComposition {
    Pool* pool;
    DomDocument* document;
    ViewCssStyle* root_style; // the document root's page-context style: counters, marks, margin boxes
    // fonts the page flow copied out of the DOM view for its glyph runs; their handles stay pinned
    // until the composition dies, because the DOM view's props do not outlive the next layout
    FontProp** flow_fonts;
    size_t flow_font_count, flow_font_capacity;
    PagedLayoutOptions options;
    PagedLayoutDiagnostic diagnostic;
    lam::Own<HashMap> mark_sources;
    TypesetMarkStore marks;
    CounterContext* counters;
    PagedReferenceSession* references;
    DomNodeRef reference_source;
    bool reference_used;
    Pool* reference_pool;
    TypesetTargetStore targets;
    PagedPaintRecord* paint_records;
};






using PagedBoxEdges = ViewCssBoxEdges;
// the page sequence the flow appends pages to: the current page, its style and
// body column, and the named page, sequence and terminal master that select the next
struct PagedPageCursor {
    ViewTree* tree;
    PagedComposition* composition;
    ViewPageBox* page;
    ViewPageStyle page_style;
    uint32_t column_index;
    float y, bottom, page_start;
    bool page_has_content, sheet_has_content;
    ViewBreak requested_break;
    const char* page_name;
    const RadiantPageSequence* sequence;
    uint32_t terminal_page;
};

static bool paged_has_next_column(const PagedPageCursor* composer) {
    return composer->page_style.column_count > 1 && composer->column_index + 1 < composer->page_style.column_count;
}

static TypesetStatus paged_failure(PagedComposition* composition, TypesetStatus status,
    DomNode* source, uint32_t page, const char* reason);

static TypesetStatus paged_column_select(PagedPageCursor* composer, uint32_t index) {
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
    composer->y = composer->page_start = rect.y; composer->bottom = rect.y + rect.height;
    return TYPESET_OK;
}



PagedLayoutOptions paged_layout_options_default() {
    PagedLayoutOptions options = {};
    options.max_pages = 10000;
    options.max_nodes = 1000000;
    options.max_depth = 256;
    options.max_reference_passes = 8;
    options.max_container_passes = 256;
    options.first_side = VIEW_PAGE_RIGHT;
    return options;
}

void paged_composition_destroy(ViewTree* tree) {
    if (!tree || !tree->model || !tree->model->composition) return;
    PagedComposition* composition = tree->model->composition;
    for (PagedPaintRecord* record = composition->paint_records; record; record = record->next) {
        record->paint.~PaintList();
    }
    for (size_t i = 1; i < composition->flow_font_count; i += 2) font_cache_unpin_handle(composition->flow_fonts[i]->font_handle);
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
    composition->diagnostic.origin = radiant_page_source_origin(composition->document, source);
    return status;
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

// the name of a `position: running(name)` value
static const char* paged_running_value_name(const CssValue* value) {
    const CssFunction* function = value && value->type == CSS_VALUE_TYPE_FUNCTION ? value->data.function : nullptr;
    return css_function_name_is(function, "running") && function->arg_count == 1
        ? css_value_identifier_name(function->args[0]) : nullptr;
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


static float paged_used_length(PagedPageCursor* composer, ViewCssStyle* style, const CssValue* value,
                               CssPropertyCode property, float width, float fallback = 0.0f, float height = NAN) {
    float result = view_css_length(composer->tree, style, value, property, width, height);
    return isfinite(result) ? result : fallback;
}

static TypesetStatus paged_box_edges(PagedPageCursor* composer, const ViewCssStyle* source,
        float width, PagedBoxEdges* box) {
    return view_css_box_edges(composer->tree, source, width, box) == VIEW_MODEL_OK
        ? TYPESET_OK : TYPESET_INVALID;
}


static TypesetStatus paged_boundary_publish(ViewTree* tree, LayoutViewNode* node,
        const PagedBoxEdges& box) {
    if (!view_tree_model_touch_node(tree, node)) return TYPESET_OUT_OF_MEMORY;
    struct Storage { BoundaryProp boundary; BorderProp border; BackgroundProp background; };
    Storage* storage = (Storage*)arena_alloc(tree->model->arena, sizeof(Storage));
    if (!storage) return TYPESET_OUT_OF_MEMORY;
    *storage = {}; storage->border = box.border; storage->background = box.background;
    storage->boundary.border = lam::own(&storage->border);
    storage->boundary.background = lam::own(&storage->background);
    for (size_t i = 0; i < 4; i++) storage->boundary.padding.values[i] = box.padding[i];
    node->computed_boundary = lam::up(&storage->boundary);
    return TYPESET_OK;
}



static ViewPageSide paged_side(const PagedPageCursor* composer, size_t index) {
    ViewPageSide first = composer->composition->options.first_side;
    return index % 2 ? (first == VIEW_PAGE_RIGHT ? VIEW_PAGE_LEFT : VIEW_PAGE_RIGHT) : first;
}

static bool paged_requested_side(const PagedPageCursor* composer, ViewBreak request, ViewPageSide* side) {
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

static TypesetStatus paged_page_spec(PagedPageCursor* composer, size_t index, bool blank, PagedPageSpec* spec) {
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

static TypesetStatus paged_page_style(PagedPageCursor* composer, const PagedPageSpec& spec,
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

static TypesetStatus paged_empty_page_restyle(PagedPageCursor* composer, bool blank, bool preserve_column = false) {
    ViewPageBox* page = composer->page;
    if (!page || composer->page_has_content || composer->sheet_has_content) return TYPESET_INVALID;
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
    return paged_column_select(composer, column);
}

static TypesetStatus paged_page_create(PagedPageCursor* composer, bool blank) {
    ViewTree* tree = composer->tree;
    if (tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
        composer->page_style.width = tree->model->environment.viewport_width;
        composer->page_style.height = tree->model->environment.viewport_height;
        composer->page_style.content_rect = {0.0f, 0.0f, composer->page_style.width, composer->page_style.height};
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
    paged_page_bind(composer->page, spec);
    composer->y = composer->page_start = style->content_rect.y;
    composer->page_has_content = composer->sheet_has_content = false;
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

static TypesetStatus paged_sequence_end(PagedPageCursor* composer, const RadiantPageSequence* following) {
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
    composer->sequence = pending; composer->page_name = name;
    composer->terminal_page = terminal;
    return status;
}

struct PagedContentBinding {
    ViewTree* tree;
    PagedComposition* composition;
    ViewPageBox* page;
    ViewCssStyle* style;
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
        // the browsing layout's generated boxes are not source text; table fixups wrap authored content
        bool generated = node->is_element() && node->as_element()->is_synthetic() && !node->as_element()->is_table_fixup();
        DomNode* child = node->is_element() && !generated ? node->as_element()->first_child.get() : nullptr;
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

// the target a reference names: a local fragment URL from a string, url() or
// attr(); `source` is the element whose content holds the reference
static const TypesetTarget* paged_content_target(ViewTree* tree, PagedComposition* composition, DomElement* source,
        const CssValue* argument) {
    const char* url = argument && argument->type == CSS_VALUE_TYPE_STRING ? argument->data.string :
        argument && argument->type == CSS_VALUE_TYPE_URL ? argument->data.url : nullptr;
    const char* type = nullptr;
    const char* attribute = css_content_attribute_name(argument, &type);
    if (type && !str_ieq_cstr(type, "url") && !str_ieq_cstr(type, "string")) return nullptr;
    if (attribute) url = source->get_attribute(attribute);
    composition->reference_used = true;
    composition->reference_source = dom_node_ref(source);
    const char* fragment = url && url[0] == '#' ? url + 1 : nullptr;
    Url* resolved = nullptr;
    if (!fragment && url && tree->model->document->url) {
        resolved = url_parse_with_base(url, tree->model->document->url);
        if (resolved && url_equals_without_fragment(resolved, tree->model->document->url)) {
            const char* hash = url_get_hash(resolved);
            if (hash && hash[0] == '#') fragment = hash + 1;
        }
    }
    if (!fragment || !*fragment) {
        if (resolved) url_destroy(resolved);
        paged_failure(composition, TYPESET_INVALID, source, 0, "target reference requires a local fragment URL");
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
        paged_failure(composition, TYPESET_OUT_OF_MEMORY, source, 0, "target fragment decoding allocation failed");
        return nullptr;
    }
    if (!target) paged_failure(composition, TYPESET_INVALID, source, 0, "target reference has no matching source ID");
    return target;
}

// target-text(), target-counter() and target-counters() from the previous
// reference pass's bindings; `source` holds the reference
static bool paged_content_target_function(ViewTree* tree, PagedComposition* composition, DomElement* source,
        const CssFunction* function, StrBuf* text) {
    bool target_text = css_function_name_is(function, "target-text");
    bool plural = css_function_name_is(function, "target-counters");
    size_t required = target_text ? 1 : plural ? 3 : 2;
    if ((size_t)function->arg_count < required || (size_t)function->arg_count > required + 1) return false;
    const TypesetTarget* target = paged_content_target(tree, composition, source, function->args[0]);
    if (!target || !target->binding || target->binding->provider != tree->model->tree_id) return false;
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
        if (!target->page_number && composition->references->pass > 1) {
            paged_failure(composition, TYPESET_UNPLACEABLE, source, 0,
                "target page reference has no placed content box");
            return false;
        }
        if (strcmp(name, "page") == 0) {
            // Reference passes retain the target's label snapshot independently of its physical index.
            return value->page_counters ? counter_snapshot_append(value->page_counters, name, separator, style, text) :
                counter_value_append(0, style, text);
        }
        size_t number = composition->references->page_count;
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
// a page context's generated content (CSS GCPM 3, CSS Paged Media 3 §5.3): page
// counters, named strings, running content and reference functions resolve against the page
static bool paged_page_content_function(void* context, const CssFunction* function, StrBuf* text) {
    PagedContentBinding* binding = (PagedContentBinding*)context;
    if (css_function_name_is(function, "target-counter") || css_function_name_is(function, "target-counters") ||
        css_function_name_is(function, "target-text"))
        return paged_content_target_function(binding->tree, binding->composition, binding->style->source, function, text);
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
    if ((!binding->page && (strcmp(name, "page") == 0 || strcmp(name, "pages") == 0)) || strcmp(name, "footnote") == 0)
        return false;
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
    if (strcmp(name, "pages") != 0 || !binding->page)
        return counter_snapshot_append(paged_content_counters(binding, name), name, separator, style, text);
    size_t pages = binding->tree->model->page_count;
    if (pages > INT_MAX) return false;
    return counter_value_append((int)pages, style, text); // INT_CAST_OK: bounded publishing counter, not a dimension.
}

static TypesetStatus paged_content_text(ViewTree* tree, PagedComposition* composition, ViewCssStyle* style,
        ViewPageBox* page, const CssValue* content, char** result, size_t* length) {
    PagedContentBinding binding = {tree, composition, page, style};
    CssContentBindings callbacks = {&binding, paged_content_attribute, paged_content_quote, paged_page_content_function};
    StrBuf* text = strbuf_new();
    if (!text) return TYPESET_OUT_OF_MEMORY;
    int quotes = 0;
    // Source generation shares quote depth; repeated page furniture has its own evaluation.
    bool ok = css_content_append(content, &callbacks, page ? &quotes : &composition->counters->quote_depth, text);
    *length = text->length;
    *result = ok ? pool_dup_n(composition->pool, text->str ? text->str : "", text->length) : nullptr;
    strbuf_free(text);
    return !ok ? (composition->diagnostic.status != TYPESET_OK ? composition->diagnostic.status : TYPESET_INVALID) :
        *result ? TYPESET_OK : TYPESET_OUT_OF_MEMORY;
}

// one `name content-list` group of a string-set value
static bool paged_string_set_group(const CssValue* group, const char** name, CssValue* content) {
    if (!group || group->type != CSS_VALUE_TYPE_LIST || group->data.list.comma_separated || group->data.list.count < 2)
        return false;
    *name = css_value_identifier_name(group->data.list.values[0]);
    if (!*name || !**name) return false;
    *content = *group; content->data.list.values++; content->data.list.count--;
    return true;
}

static bool paged_source_has_own_area(const ViewCssStyle* style) {
    if (style && style->pseudo_element) style = style->parent.get();
    return !style || !style->flow_traits || !style->flow_traits->descendant_areas;
}



struct PagedNoteArea : PagedBoxEdges {
    float margin[4], width, minimum, maximum, preferred;
};
static TypesetStatus paged_note_area(PagedPageCursor* composer, PagedNoteArea* area) {
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

// =============================================================================
// Page flow through the browsing engine (Radiant_Layout_Paged_Media §1.3)
// =============================================================================
// layout_html_doc lays the document out once at the page body width while
// lycon->paged points at a PagedFlowState. Pages are consecutive fragmentainers
// of that single flow: fragmentainer k covers [top_k, bottom_k) in flow
// coordinates and the fragmentainers tile the flow contiguously. A page break
// is a strut inserted before the line or box that would cross a fragmentainer
// end, so the box starts exactly at the next top. Publication then slices the
// DOM-embedded views per fragmentainer into the edition.

static const float kPagedFlowEpsilon = 0.01f;

struct PagedFragmentainer {
    float top, bottom;         // absolute flow extents; bottom is infinite for a continuous edition
    float end;                 // publication: where body content ends, above the material placed here
    ViewPageBox* page;         // null for a continuous edition
    uint32_t column;
    RdtLogicalRect rect;       // page-local body or column rectangle
    LayoutViewNode* root;      // the document root's fragment on this page once published
    // footnotes and page floats placed here (plan P5): their extents stay free at
    // the fragmentainer's end, and top floats move its body down at publication
    float float_top, float_bottom, notes;
    uint32_t note_count, top_floats, bottom_floats;
    PagedNoteArea* area;       // the footnote area's geometry, resolved on first use
    bool occupied;
};
struct PagedFlowLine {
    float top, height;         // in the paragraph's inline frame (BlockContext::advance_y)
    size_t fragmentainer;
    int number;                // the block-local line number its text rects carry
    uint32_t anchor_first, anchor_count; // the footnotes and page floats anchored on the line
};
enum PagedAnchorKind : uint8_t { PAGED_ANCHOR_NOTE, PAGED_ANCHOR_TOP, PAGED_ANCHOR_BOTTOM };
enum PagedNotePolicy : uint8_t { PAGED_NOTE_AUTO, PAGED_NOTE_LINE, PAGED_NOTE_BLOCK };
static const size_t kPagedUnplaced = SIZE_MAX;
struct PagedFlowParagraph;
struct PagedNotePiece {
    float offset, height;
    size_t fragmentainer;
    PagedNotePiece* next;
};
// a footnote or page float out of the flow, anchored where its call or position
// falls (CSS GCPM 3 §2, CSS Page Floats §3); notes are atomic and floats move
// with their anchor (plan §8 rulings 2 and 3)
struct PagedFlowAnchor {
    DomElement* element;       // laid out by the browsing engine outside the flow
    PagedAnchorKind kind;
    PagedNotePolicy policy;    // footnote-policy (CSS GCPM 3 §2.7)
    uint8_t clear;             // the float sides a page float clears: 1 top, 2 bottom
    uint8_t view_type;         // its out-of-flow layout's view type, restored to publish it
    bool deferred;             // an automatic note waiting for a fragmentainer after its call's
    float width, height;       // the width it was laid out at and its margin-box height there
    float full_height;
    PagedNotePiece* continuation;
    size_t fragmentainer;      // where it is placed, kPagedUnplaced before
    PagedFlowParagraph* paragraph; // the inline run of the line holding its call or position
    size_t line;
    View* box;                 // or the box it precedes
};
struct PagedFlowTable;
struct PagedFixedBox {
    ViewBlock* block;
    float origin_x, origin_y;
    PagedFixedBox* next;
};
struct PagedFlowParagraph {    // one block container's uninterrupted inline run
    ViewBlock* block;
    PagedFlowLine* lines;
    size_t count, capacity;
    PagedFlowParagraph* next;
    PagedFlowTable* table;     // set when the unit is a table broken by the row pass instead of a run
};
struct PagedStrut {
    View* view;                // the box, or the paragraph block of a line strut
    PagedFlowParagraph* paragraph; // set for a line strut
    int line;                  // first moved line of a line strut; -1 for a box
    float amount;              // zero for a break the flow reached by position alone
    bool forced;               // an authored break; keep rules never move it
    size_t fragmentainer;      // the fragmentainer the moved content starts
    float before;              // flow position of the moved content before the shift: the content end on the previous fragmentainer
    PagedFlowTable* table;     // a table's breaks as one entry: amount is its growth, the record keeps the breaks
};
// a non-top-aligned cell of a row that continues inside its cells; each slice
// of it aligns on its own (plan §8 ruling 4)
struct PagedSplitCell {
    ViewTableCell* cell;
    size_t first;              // the fragmentainer the row starts on
    float offset;              // a baseline cell's extra reserve above its lines on later fragmentainers
};
// a table broken between rows after layout_table placed it whole (plan P3)
struct PagedFlowTable {
    ViewBlock* table;
    ViewBlock* header;         // the header group that repeats on continuation fragmentainers, or null
    ViewBlock* footer;         // the footer group that repeats before each break, or null
    float spacing;             // vertical border spacing of the separated model
    float header_size;         // space a continuation keeps above the body rows
    float footer_size;         // space every fragmentainer keeps below the body rows
    size_t first;              // the fragmentainer holding the table's top
    float growth;              // the extent the breaks added to the table
    bool modified;             // the internal geometry differs from the snapshot
    float* snapshot;           // the internal geometry as layout_table left it
    size_t snapshot_count;
    PagedStrut* struts;        // the breaks inside the table, in flow order
    size_t strut_count, strut_capacity;
    PagedSplitCell* split;     // the aligned cells of its split rows
    size_t split_count, split_capacity;
    PagedFlowTable* outer;     // the enclosing table while this one is fitted
    PagedFlowTable* next;      // every table of the pass, for publication
};
struct PagedFlowState {
    ViewTree* tree;
    PagedComposition* composition;
    PagedPageCursor composer;    // page selection, sides, blank pages and body columns stay here
    PagedFragmentainer* fragmentainers;
    size_t fragmentainer_count, fragmentainer_capacity, cursor;
    float run_width;           // the flow's content width, constant within a run (plan §4.2)
    PagedStrut* struts;
    size_t strut_count, strut_capacity;
    size_t* open_struts;       // strut count when the block at each layout depth began
    size_t open_capacity;
    PagedFlowParagraph* paragraphs;
    PagedFlowParagraph* last_paragraph;
    PagedFlowParagraph* paragraph; // the open inline run
    ViewBreak pending_break;   // forced break-after of the previous in-flow sibling
    ViewModelCheckpoint* pass; // the pages of the running layout pass
    PagedFlowTable* tables;    // the tables of the pass in flow order
    PagedFlowTable* last_table;
    PagedFlowTable* table;     // the innermost table whose content is being fitted
    ViewFragmentRole publish_role; // the role of the fragments publication appends
    const PagedFlowTable* publish_table; // the broken table whose grid is being published
    float* publish_ends;       // its body end per fragmentainer from its first
    size_t publish_last;       // the fragmentainer holding its bottom
    View* publish_skip;        // a child the walk leaves for later (a broken table's footer group)
    PagedFixedBox* fixed_boxes;
    PagedFixedBox* last_fixed_box;
    bool publishing_fixed;
    PagedMarkSource** pending; // mark sources waiting for the content that binds them to a page
    size_t pending_count, pending_capacity;
    PagedMarkSource** open_targets; // targets whose content publication is inside
    size_t open_target_count, open_target_capacity;
    uint32_t content_page;     // the page of the last published body content
    uint32_t string_depth;     // inside a string-set evaluation, where content() applies
    bool composed;             // paged_flow_compose ran for the settled pass
    PagedFlowAnchor* anchors;  // footnotes and page floats in flow order (plan P5)
    size_t anchor_count, anchor_capacity;
    size_t anchors_bound;      // the anchors before this index have their line or box
    size_t notes_waiting;      // automatic notes deferred and not yet placed
    float flow_end;            // the lowest flow position a line or box reached (the root box keeps the page height)
    bool draining;             // the flow ended: new fragmentainers hold only waiting notes
    DomElement* call;          // the footnote call being laid out at its note
    uint32_t outside;          // material laid out outside the flow: the hooks stand aside
    TypesetStatus status;
    uint32_t relaxed_minima, relaxed_avoidance;
};

static TypesetStatus paged_flow_fail(PagedFlowState* state, TypesetStatus status, DomNode* source, const char* reason) {
    if (state->status == TYPESET_OK) state->status = status;
    return paged_failure(state->composition, status, source,
        state->composer.page ? state->composer.page->page_number : 0, reason);
}

const CssValue* paged_resolve_css_value(LayoutContext* lycon, DomElement* element,
        const CssValue* value, bool* handled) {
    *handled = false;
    if (!lycon->paged || !element || !css_value_contains_var_reference(value)) return nullptr;
    PagedFlowState* state = lycon->paged;
    ViewCssStyle* style = view_css_resolve(state->tree, element);
    if (!style || !style->computed_bindings) return nullptr;
    *handled = true;
    const CssValue* resolved = view_css_resolve_value(state->tree, style, value);
    if (style->binding_status != VIEW_MODEL_OK)
        paged_flow_fail(state, TYPESET_INVALID, element, style->binding_reason);
    return resolved;
}

// flow-coordinate top of a box: spans position their children in the enclosing
// block's frame, so only block ancestors contribute
static float paged_flow_top(View* view) {
    return view_geometry_ancestor_offset(view, nullptr, true).y;
}

static ViewBreak paged_flow_break_keyword(CssEnum keyword) {
    switch (keyword) {
    case CSS_VALUE_AVOID: return VIEW_BREAK_AVOID;
    case CSS_VALUE_AVOID_COLUMN: return VIEW_BREAK_AVOID_COLUMN;
    case CSS_VALUE_AVOID_PAGE: return VIEW_BREAK_AVOID_PAGE;
    case CSS_VALUE_COLUMN: return VIEW_BREAK_COLUMN;
    case CSS_VALUE_PAGE: case CSS_VALUE_ALWAYS: return VIEW_BREAK_PAGE;
    case CSS_VALUE_LEFT: return VIEW_BREAK_LEFT;
    case CSS_VALUE_RIGHT: return VIEW_BREAK_RIGHT;
    case CSS_VALUE_RECTO: return VIEW_BREAK_RECTO;
    case CSS_VALUE_VERSO: return VIEW_BREAK_VERSO;
    default: return VIEW_BREAK_AUTO;
    }
}

static bool paged_break_is_avoid(ViewBreak value) {
    return value == VIEW_BREAK_AVOID || value == VIEW_BREAK_AVOID_COLUMN || value == VIEW_BREAK_AVOID_PAGE;
}

// boxes whose content never breaks: replaced content, flex and grid containers
// and multicol containers; a box taller than a fragmentainer is sliced by publication
static bool paged_flow_atomic(const ViewBlock* block) {
    if (block->display.inner == RDT_DISPLAY_REPLACED || block->display.inner == CSS_VALUE_FLEX ||
        block->display.inner == CSS_VALUE_GRID) return true;
    return block->multicol_prop() && is_multicol_container(const_cast<ViewBlock*>(block));
}

static bool paged_flow_is_table(const ViewBlock* block) {
    return block->view_type == RDT_VIEW_TABLE || block->display.inner == CSS_VALUE_TABLE;
}

// boxes whose content the line and block hooks leave alone: atomic boxes, and
// tables, which the row pass breaks after layout_table placed them whole
static bool paged_flow_closed(const ViewBlock* block) {
    return paged_flow_atomic(block) || paged_flow_is_table(block);
}

// break-inside: avoid keeps its content together when a fragmentainer can hold it;
// otherwise the content breaks as usual and the avoidance is relaxed (CSS Break §4.3)
static bool paged_flow_avoids_inside(const ViewBlock* block) {
    const BlockProp* blk = block->blk ? block->block() : nullptr;
    return blk && (blk->break_inside == CSS_VALUE_AVOID || blk->break_inside == CSS_VALUE_AVOID_PAGE ||
        blk->break_inside == CSS_VALUE_AVOID_COLUMN);
}

// boxes the flow places whole when they fit a fragmentainer
static bool paged_flow_monolithic(const ViewBlock* block) {
    return paged_flow_atomic(block) || paged_flow_avoids_inside(block);
}

// normal-flow block-level boxes take part in fragmentation; inline-level,
// out-of-flow and table-internal boxes are placed by their formatting context
static bool paged_flow_block(const ViewBlock* block) {
    if (!block || !block->view_type || block->view_type == RDT_VIEW_INLINE_BLOCK) return false;
    if (layout_block_is_out_of_flow(block)) return false;
    if (block->display.outer == CSS_VALUE_INLINE || block->display.outer == CSS_VALUE_INLINE_BLOCK) return false;
    return !is_table_internal_display(block->display.inner);
}

// a box inside a monolithic, inline-level or out-of-flow ancestor breaks with that ancestor
static bool paged_flow_fragmentable(LayoutContext* lycon, View* view) {
    if (lycon->run_mode != radiant::RunMode::PerformLayout || lycon->measurement_root) return false;
    for (View* current = view ? view->parent : nullptr; current; current = current->parent) {
        if (!current->is_block()) continue;
        ViewBlock* block = lam::view_require_block(current);
        if (!paged_flow_block(block) || paged_flow_closed(block)) return false;
    }
    return true;
}

// box-decoration-break: clone wraps every fragment with the box's padding and
// border, so a break inside such a box reserves its bottom edges before the
// fragmentainer end and its top edges after it (CSS Fragmentation 3 §5.3)
// the edges `block` repeats on each fragment under box-decoration-break: clone
static void paged_box_clone_edges(const PagedFlowState* state, ViewBlock* block, float* top, float* bottom) {
    *top = *bottom = 0.0f;
    if (!block->bound) return;
    const ViewCssStyle* style = view_css_resolve(state->tree, block);
    const BoundaryProp* bound = block->boundary();
    // native conditionality can be a computed binding, independently for each padding and border edge.
    for (size_t side = 0; side < 2; side++) {
        size_t edge = side * 2;
        float retained = radiant_decoration_retain(style, true, side != 0) ? bound->padding.values[edge] : 0.0f;
        if (bound->border && radiant_decoration_retain(style, false, side != 0)) retained += bound->border->width.values[edge];
        (side ? *bottom : *top) = retained;
    }
}

static void paged_clone_edges(const PagedFlowState* state, View* view, float* top, float* bottom) {
    *top = *bottom = 0.0f;
    for (View* current = view; current; current = current->parent) {
        if (!current->is_block()) continue;
        float edge_top, edge_bottom;
        paged_box_clone_edges(state, lam::view_require_block(current), &edge_top, &edge_bottom);
        *top += edge_top; *bottom += edge_bottom;
    }
}

// is `view` inside the body rows of `table`, below the table's header and above
// its footer (not a caption, a column or one of those groups)?
static bool paged_table_body_view(View* view, const ViewBlock* table) {
    View* child = nullptr;
    for (View* current = view; current; current = current->parent) {
        if (current == (const View*)table) {
            if (!child) return false;
            if (child->view_type == RDT_VIEW_TABLE_ROW) return true;
            return child->view_type == RDT_VIEW_TABLE_ROW_GROUP &&
                static_cast<ViewTableRowGroup*>(child)->get_section_type() == TABLE_SECTION_TBODY;
        }
        child = current;
    }
    return false;
}

static float paged_fragmentainer_reserved(PagedFlowState* state, size_t index);
static TypesetStatus paged_fragmentainer_append(PagedFlowState* state, bool new_page, bool restart = false);
static TypesetStatus paged_note_continue(PagedFlowState* state, PagedFlowAnchor* anchor, size_t index);
static float paged_note_piece_height(PagedFlowAnchor* anchor, float offset, float available);

// the space fragmentainer `index` keeps free above and below `view`'s content:
// cloned box edges, the footnotes and page floats placed on it, and around a
// broken table's rows the repeated header (on continuation fragmentainers) and footer
static void paged_reserve(PagedFlowState* state, View* view, size_t index, float* top, float* bottom) {
    paged_clone_edges(state, view, top, bottom);
    *bottom += paged_fragmentainer_reserved(state, index);
    for (const PagedFlowTable* table = state->table; table; table = table->outer) {
        if (!paged_table_body_view(view, table->table)) continue;
        if (index > table->first) *top += table->header_size;
        *bottom += table->footer_size;
        for (size_t i = 0; i < table->split_count; i++) {
            const PagedSplitCell& split = table->split[i];
            if (index > split.first && split.offset > 0.0f && view_geometry_is_descendant(view, split.cell, true))
                *top += split.offset;
        }
    }
}

static int paged_flow_line_limit(const ViewBlock* block, bool widows) {
    const BlockProp* blk = block && block->blk ? block->block() : nullptr;
    int value = blk ? (widows ? blk->widows : blk->orphans) : 0;
    return value > 0 ? value : 2; // CSS Fragmentation 3 §4.4 initial value
}

// ----- footnotes and page floats: reservations on the fragmentainers (plan P5) -----
// A footnote or page float is laid out once by the browsing engine outside the
// flow, then placed on the fragmentainer of its line or box. Its extent stays free
// at that fragmentainer's end, so later lines and boxes fit above it; publication
// puts the floats at the top and bottom and the footnote area at the bottom.

// the footnote area of fragmentainer `index` from its page's @footnote context,
// with percentages against the fragmentainer's body rectangle (CSS GCPM 3 §2.4);
// without a usable one the area spans the fragmentainer with no edges or limit
static const PagedNoteArea* paged_fragmentainer_area(PagedFlowState* state, size_t index) {
    PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    if (fragmentainer.area) return fragmentainer.area;
    PagedNoteArea* area = (PagedNoteArea*)pool_calloc(state->composition->pool, sizeof(PagedNoteArea));
    if (!area) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, nullptr, nullptr); return nullptr; }
    bool resolved = false;
    if (fragmentainer.page && fragmentainer.page->style) {
        PagedPageCursor local = {};
        local.tree = state->tree; local.composition = state->composition; local.page = fragmentainer.page;
        local.page_style = *fragmentainer.page->style; local.page_style.content_rect = fragmentainer.rect;
        resolved = paged_note_area(&local, area) == TYPESET_OK;
    }
    if (!resolved) {
        *area = {};
        area->width = fragmentainer.rect.width; area->maximum = INFINITY; area->preferred = NAN;
    }
    fragmentainer.area = area;
    return area;
}

// the extent a footnote area holding notes of total height `notes` takes, margins included
static float paged_note_extent(const PagedNoteArea* area, float notes) {
    float content = area->edges[0] + area->edges[2] + notes;
    float box = isfinite(area->preferred) ? fmaxf(area->preferred, content) : fmaxf(area->minimum, content);
    return area->margin[0] + area->margin[2] + box;
}

// the extent fragmentainer `index` keeps free at its end for the material placed on it
static float paged_fragmentainer_reserved(PagedFlowState* state, size_t index) {
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    float reserved = fragmentainer.float_top + fragmentainer.float_bottom;
    if (fragmentainer.note_count) {
        const PagedNoteArea* area = paged_fragmentainer_area(state, index);
        if (area) reserved += paged_note_extent(area, fragmentainer.notes);
    }
    return reserved;
}

static void paged_anchor_place(PagedFlowState* state, PagedFlowAnchor* anchor, size_t index) {
    PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    if (anchor->kind == PAGED_ANCHOR_NOTE) { fragmentainer.notes += anchor->height; fragmentainer.note_count++; }
    else if (anchor->kind == PAGED_ANCHOR_TOP) { fragmentainer.float_top += anchor->height; fragmentainer.top_floats++; }
    else { fragmentainer.float_bottom += anchor->height; fragmentainer.bottom_floats++; }
    anchor->fragmentainer = index;
    if (anchor->kind == PAGED_ANCHOR_NOTE && anchor->full_height > anchor->height + kPagedFlowEpsilon)
        paged_note_continue(state, anchor, index);
}

// take an anchor off its fragmentainer (or out of the waiting notes) before it is placed again
static void paged_anchor_lift(PagedFlowState* state, PagedFlowAnchor* anchor) {
    if (anchor->deferred) { anchor->deferred = false; state->notes_waiting--; }
    if (anchor->fragmentainer == kPagedUnplaced) return;
    PagedFragmentainer& fragmentainer = state->fragmentainers[anchor->fragmentainer];
    if (anchor->kind == PAGED_ANCHOR_NOTE) { fragmentainer.notes -= anchor->height; fragmentainer.note_count--; }
    else if (anchor->kind == PAGED_ANCHOR_TOP) { fragmentainer.float_top -= anchor->height; fragmentainer.top_floats--; }
    else { fragmentainer.float_bottom -= anchor->height; fragmentainer.bottom_floats--; }
    anchor->fragmentainer = kPagedUnplaced;
    for (PagedNotePiece* piece = anchor->continuation; piece; piece = piece->next) {
        state->fragmentainers[piece->fragmentainer].notes -= piece->height;
        state->fragmentainers[piece->fragmentainer].note_count--;
    }
    anchor->continuation = nullptr;
    if (anchor->full_height > 0.0f) anchor->height = anchor->full_height;
}

static void paged_note_defer(PagedFlowState* state, PagedFlowAnchor* anchor) {
    anchor->deferred = true; state->notes_waiting++;
}

// can `anchor` join fragmentainer `index` whose content reaches `content_end`?
// The footnote area keeps its height limit unless the fragmentainer holds no body
// content (`alone`), and a float with clearance avoids a fragmentainer that already
// holds a float of a cleared side
static bool paged_anchor_fits(PagedFlowState* state, PagedFlowAnchor* anchor, size_t index,
        float content_end, bool alone) {
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    if (!isfinite(fragmentainer.bottom)) return true;
    float extra = anchor->height;
    if (anchor->kind == PAGED_ANCHOR_NOTE) {
        const PagedNoteArea* area = paged_fragmentainer_area(state, index);
        if (!area) return false;
        float limit = isfinite(area->preferred) ? area->preferred : area->maximum;
        if (anchor->full_height > fragmentainer.rect.height + kPagedFlowEpsilon) {
            float capacity = fragmentainer.bottom - content_end - fragmentainer.float_top - fragmentainer.float_bottom -
                area->margin[0] - area->margin[2] - area->edges[0] - area->edges[2] - fragmentainer.notes;
            if (!alone) capacity = fminf(capacity, limit - area->edges[0] - area->edges[2] - fragmentainer.notes);
            anchor->height = paged_note_piece_height(anchor, 0.0f, capacity);
            if (anchor->height <= 0.0f) return false;
        }
        float content = area->edges[0] + area->edges[2] + fragmentainer.notes + anchor->height;
        if (!alone && content > limit + kPagedFlowEpsilon) return false;
        extra = paged_note_extent(area, fragmentainer.notes + anchor->height) -
            (fragmentainer.note_count ? paged_note_extent(area, fragmentainer.notes) : 0.0f);
    } else if (((anchor->clear & 1) && fragmentainer.top_floats) || ((anchor->clear & 2) && fragmentainer.bottom_floats)) {
        return false;
    }
    return content_end + paged_fragmentainer_reserved(state, index) + extra <= fragmentainer.bottom + kPagedFlowEpsilon;
}

enum PagedAnchorFit : uint8_t { PAGED_ANCHORS_PLACED, PAGED_ANCHORS_MOVE, PAGED_ANCHORS_FAILED };

// place anchors [first, first + count) on fragmentainer `index` after content
// reaching `content_end`. An automatic note that does not fit waits for a later
// fragmentainer (CSS GCPM 3 §2.7), as does one behind a waiting note, which keeps
// the notes in order; any other anchor that does not fit moves its line or box
// (MOVE), which fails when the content already starts the fragmentainer. The
// anchors of a line move with it together; with `kept`, anchors standing between
// boxes keep the fragmentainer up to the first that does not fit, which reports
// how many stayed, and only that one and the rest move on
static PagedAnchorFit paged_anchors_place(PagedFlowState* state, size_t first, size_t count, size_t index,
        float content_end, bool starts, size_t* kept = nullptr) {
    for (size_t i = first; i < first + count; i++) {
        PagedFlowAnchor* anchor = &state->anchors[i];
        paged_anchor_lift(state, anchor);
        bool automatic = anchor->kind == PAGED_ANCHOR_NOTE && anchor->policy == PAGED_NOTE_AUTO;
        if (automatic && state->notes_waiting) { paged_note_defer(state, anchor); continue; }
        if (paged_anchor_fits(state, anchor, index, content_end, false)) { paged_anchor_place(state, anchor, index); continue; }
        if (automatic) { paged_note_defer(state, anchor); continue; }
        if (kept) {
            *kept = i - first;
            if (!starts || *kept) return PAGED_ANCHORS_MOVE;
        } else {
            for (size_t j = first; j <= i; j++) paged_anchor_lift(state, &state->anchors[j]);
            if (!starts) return PAGED_ANCHORS_MOVE;
        }
        paged_flow_fail(state, TYPESET_UNPLACEABLE, anchor->element, anchor->kind == PAGED_ANCHOR_NOTE ?
            "a footnote does not fit its page's footnote area beside its call" :
            "a page float does not fit its page beside its anchor");
        return PAGED_ANCHORS_FAILED;
    }
    return PAGED_ANCHORS_PLACED;
}

// fragmentainer `index`, whose content reaches `content_end`, takes the waiting
// notes in order while they fit
static void paged_notes_resume(PagedFlowState* state, size_t index, float content_end, bool alone) {
    for (size_t i = 0; state->notes_waiting && i < state->anchor_count; i++) {
        PagedFlowAnchor* anchor = &state->anchors[i];
        if (!anchor->deferred) continue;
        if (!paged_anchor_fits(state, anchor, index, content_end, alone)) return;
        anchor->deferred = false; state->notes_waiting--;
        paged_anchor_place(state, anchor, index);
    }
}

// a forced break restyled an empty fragmentainer: its footnote area is resolved
// again, and a page turned blank gives its waiting notes back
static void paged_fragmentainer_restyle(PagedFlowState* state, size_t index, bool blank) {
    PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    fragmentainer.rect = state->composer.page_style.content_rect;
    fragmentainer.bottom = fragmentainer.end = fragmentainer.top + fragmentainer.rect.height;
    fragmentainer.area = nullptr;
    for (size_t i = 0; blank && i < state->anchor_count; i++) {
        PagedFlowAnchor* anchor = &state->anchors[i];
        if (anchor->fragmentainer != index) continue;
        paged_anchor_lift(state, anchor);
        paged_note_defer(state, anchor);
    }
}

// the anchors of the lines [first, last] of a run, which are consecutive
static void paged_lines_anchors(const PagedFlowParagraph* paragraph, size_t first, size_t last, size_t* from, size_t* count) {
    *from = paragraph->lines[first].anchor_first;
    *count = paragraph->lines[last].anchor_first + paragraph->lines[last].anchor_count - *from;
}

// the page the previous pass ended `sequence` on, which takes its terminal master (0 when unknown)
static uint32_t paged_flow_terminal_page(const PagedFlowState* state, const RadiantPageSequence* sequence) {
    const PagedReferenceSession* references = state->composition->references;
    for (size_t i = 0; sequence && references && i < references->terminal_count; i++)
        if (references->terminals[i].sequence == sequence->source.address) return references->terminals[i].page;
    return 0;
}

static TypesetStatus paged_fragmentainer_append(PagedFlowState* state, bool new_page, bool restart) {
    PagedPageCursor* composer = &state->composer;
    bool first = !state->fragmentainer_count;
    bool continuous = state->tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS;
    if (continuous && !first) return paged_flow_fail(state, TYPESET_INVALID, nullptr, "a continuous edition has one fragmentainer");
    TypesetStatus status;
    if (!first && !new_page && paged_has_next_column(composer)) {
        status = paged_column_select(composer, composer->column_index + 1);
    } else {
        ViewBreak request = composer->requested_break;
        composer->requested_break = VIEW_BREAK_AUTO;
        ViewPageSide wanted = VIEW_PAGE_RIGHT;
        // a sided break inserts a blank page when the next sheet has the wrong side (CSS Break §3.1)
        if (!first && !continuous && paged_requested_side(composer, request, &wanted) &&
            paged_side(composer, state->tree->model->page_count) != wanted) {
            status = paged_page_create(composer, true);
            if (status != TYPESET_OK) return status;
        }
        composer->terminal_page = paged_flow_terminal_page(state, composer->sequence);
        status = paged_page_create(composer, false);
    }
    if (status != TYPESET_OK) return status;
    const RdtLogicalRect& rect = composer->page_style.content_rect;
    if (first) state->run_width = rect.width;
    // width changes are measured on this pass and reflowed before publication settles.
    (void)restart;
    if (!lam::pool_grow_array(state->composition->pool, &state->fragmentainers, &state->fragmentainer_capacity,
        state->fragmentainer_count + 1, 8)) return TYPESET_OUT_OF_MEMORY;
    float top = first ? 0.0f : state->fragmentainers[state->fragmentainer_count - 1].bottom;
    float bottom = continuous ? INFINITY : top + rect.height;
    PagedFragmentainer fragmentainer = {top, bottom, bottom, continuous ? nullptr : composer->page, composer->column_index, rect, nullptr};
    state->fragmentainers[state->fragmentainer_count++] = fragmentainer;
    // automatic notes waiting for a fragmentainer start this one (plan P5)
    if (state->notes_waiting) paged_notes_resume(state, state->fragmentainer_count - 1, top, state->draining);
    return TYPESET_OK;
}

// the fragmentainer holding flow position y, growing the page table on demand
static TypesetStatus paged_fragmentainer_at(PagedFlowState* state, float y, size_t* index) {
    if (state->status != TYPESET_OK) return state->status;
    size_t i = state->cursor < state->fragmentainer_count && y >= state->fragmentainers[state->cursor].top ? state->cursor : 0;
    for (;;) {
        if (i >= state->fragmentainer_count) {
            TypesetStatus status = paged_fragmentainer_append(state, false);
            if (status != TYPESET_OK) return status;
        }
        const PagedFragmentainer& fragmentainer = state->fragmentainers[i];
        // callers already bias closing edges inward; applying epsilon again creates an empty trailing page.
        if (y < fragmentainer.bottom || !isfinite(fragmentainer.bottom)) {
            *index = state->cursor = i;
            return TYPESET_OK;
        }
        i++;
    }
}

// publication lookup: the fragmentainer holding y, clamped to the laid out table
static size_t paged_fragmentainer_index(const PagedFlowState* state, float y) {
    size_t last = state->fragmentainer_count - 1;
    for (size_t i = 0; i < last; i++)
        if (y < state->fragmentainers[i].bottom) return i;
    return last;
}

static TypesetStatus paged_strut_record(PagedFlowState* state, View* view, PagedFlowParagraph* paragraph,
        int line, float amount, bool forced, size_t fragmentainer, float before) {
    if (!lam::pool_grow_array(state->composition->pool, &state->struts, &state->strut_capacity,
        state->strut_count + 1, 16)) return TYPESET_OUT_OF_MEMORY;
    state->struts[state->strut_count++] = {view, paragraph, line, amount, forced, fragmentainer, before, nullptr};
    log_debug("[PAGED_FLOW] strut %zu before %s line=%d amount=%.3f forced=%d top=%.3f", state->strut_count - 1,
        view->source_loc(), line, amount, forced, paged_flow_top(view));
    return TYPESET_OK;
}

// translate one view by delta; a text view moves through its rects
static void paged_shift_view(View* view, float delta) {
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require_text(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) rect->y += delta;
        adjust_text_bounds(text);
    } else view->y += delta;
}

// translate the inline-level content of `parent` that belongs to lines at or
// after `first_line` (text rects and inline views) and every later block child
// by delta; `threshold` is the first moved line's top in the block frame
static void paged_shift_inline_run(ViewElement* parent, int first_line, float threshold, float delta) {
    for (DomNode* node = parent->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        if (!child->view_type) continue;
        switch (child->view_type) {
        case RDT_VIEW_TEXT: {
            ViewText* text = lam::view_require_text(child);
            bool moved = false;
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (first_line >= 0 ? rect->line_number >= first_line : rect->y >= threshold - kPagedFlowEpsilon) {
                    rect->y += delta; moved = true;
                }
            }
            if (moved) adjust_text_bounds(text);
            break;
        }
        case RDT_VIEW_INLINE: {
            ViewSpan* span = lam::view_require_element(child);
            if (first_line >= 0 ? span->inline_line_number >= first_line : span->y >= threshold - kPagedFlowEpsilon)
                span->y += delta;
            // span children sit in the block frame; each decides on its own line
            paged_shift_inline_run(span, first_line, threshold, delta);
            break;
        }
        case RDT_VIEW_BR:
        case RDT_VIEW_INLINE_BLOCK:
            if (first_line >= 0 ? child->inline_line_number >= first_line : child->y >= threshold - kPagedFlowEpsilon)
                child->y += delta;
            break;
        default:
            if (child->is_block() && layout_block_is_out_of_flow(lam::view_require_block(child))) break;
            // markers and later block children follow the geometry of the moved line
            if (child->y >= threshold - kPagedFlowEpsilon) child->y += delta;
            break;
        }
    }
}

static ViewBlock* paged_flow_parent_block(View* view) {
    return layout_nearest_block_ancestor(view ? (View*)view->parent_view() : nullptr);
}

// the content inside `start` grew by delta: `start` grows unless the caller
// already sized it, its following siblings move, each finished ancestor up to
// `root` grows and moves what follows it, and `owner` (the block context whose
// cursor already passed the content) advances
static void paged_flow_grow(BlockContext* owner, View* start, bool grow_start, ViewBlock* root, float delta) {
    for (View* ancestor = root ? start : nullptr; ancestor; ancestor = paged_flow_parent_block(ancestor)) {
        if ((ancestor != start || grow_start) && ancestor->is_block()) {
            ViewBlock* block = lam::view_require_block(ancestor);
            block->height += delta; block->content_height += delta;
        }
        // inside a table the row pass moves boxes by position, and cells sit beside their siblings
        View* parent = ancestor->parent;
        bool table_part = parent && (parent->view_type == RDT_VIEW_TABLE || parent->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
            parent->view_type == RDT_VIEW_TABLE_ROW);
        for (View* sibling = table_part ? nullptr : ancestor->next(); sibling; sibling = sibling->next()) {
            if (!sibling->view_type) continue;
            if (sibling->is_block() && layout_block_is_out_of_flow(lam::view_require_block(sibling))) continue;
            paged_shift_view(sibling, delta);
        }
        if (ancestor == (View*)root) break;
    }
    if (owner) owner->advance_y += delta;
}

// move everything after a point of the open layout by delta: the lines of
// `paragraph` from `line` on (or the box itself), the finished ancestors up to
// `root` grow, their following siblings move, and `owner` (the block context
// whose cursor already passed the moved content) advances. A null root means
// the paragraph is the open block; a null owner leaves every cursor alone.
static void paged_shift_flow(PagedFlowState* state, BlockContext* owner, PagedFlowParagraph* paragraph,
        int line, View* box, ViewBlock* root, float delta) {
    (void)state;
    if (delta == 0.0f) return;
    if (paragraph) {
        // rect line numbers continue across the runs of a block, so the run's own numbers select
        paged_shift_inline_run(paragraph->block, paragraph->lines[line].number, paragraph->lines[line].top, delta);
        for (size_t i = (size_t)line; i < paragraph->count; i++) paragraph->lines[i].top += delta;
        paged_flow_grow(owner, paragraph->block, true, root, delta);
    } else {
        box->y += delta;
        paged_flow_grow(owner, box, false, root, delta);
    }
}

static PagedFlowParagraph* paged_paragraph_open(PagedFlowState* state, ViewBlock* block) {
    if (state->paragraph && state->paragraph->block == block) return state->paragraph;
    PagedFlowParagraph* paragraph = (PagedFlowParagraph*)pool_calloc(state->composition->pool, sizeof(PagedFlowParagraph));
    if (!paragraph) return nullptr;
    paragraph->block = block;
    if (state->last_paragraph) state->last_paragraph->next = paragraph;
    else state->paragraphs = paragraph;
    state->last_paragraph = state->paragraph = paragraph;
    return paragraph;
}

static bool paged_view_inside(View* view, View* ancestor) {
    return view_geometry_is_descendant(view, ancestor, true);
}

// does any strut recorded since `since` lie inside `block`?
static bool paged_struts_inside(const PagedFlowState* state, size_t since, View* block) {
    for (size_t i = since; i < state->strut_count; i++)
        if (paged_view_inside(state->struts[i].view, block)) return true;
    return false;
}

static ViewBlock* paged_previous_flow_sibling(ViewBlock* block) {
    for (View* previous = block->prev_placed_view(); previous; previous = previous->prev_placed_view()) {
        if (previous->view_type == RDT_VIEW_MARKER) continue;
        if (!previous->is_block()) return nullptr; // inline content precedes the box
        ViewBlock* candidate = lam::view_require_block(previous);
        if (layout_block_is_out_of_flow(candidate)) continue;
        return paged_flow_block(candidate) ? candidate : nullptr;
    }
    return nullptr;
}

// an anchor at a box that moved follows it to the box's fragmentainer; the content
// there is laid out already, so it stays even when that leaves the fragmentainer short
static void paged_anchor_follow(PagedFlowState* state, PagedFlowAnchor* anchor) {
    float top = paged_flow_top(anchor->box);
    size_t index;
    if (paged_fragmentainer_at(state, top, &index) != TYPESET_OK || anchor->fragmentainer == index) return;
    paged_anchor_lift(state, anchor);
    bool fits = paged_anchor_fits(state, anchor, index, top, false);
    if (!fits && anchor->kind == PAGED_ANCHOR_NOTE && anchor->policy == PAGED_NOTE_AUTO) { paged_note_defer(state, anchor); return; }
    if (!fits) log_debug("[PAGED_FLOW] anchor %s overfills fragmentainer %zu after its box moved", anchor->element->source_loc(), index);
    paged_anchor_place(state, anchor, index);
}

static TypesetStatus paged_fit_line(PagedFlowState* state, LayoutContext* lycon, BlockContext* owner,
        PagedFlowParagraph* paragraph, size_t line, ViewBlock* root, bool keeps);

static TypesetStatus paged_table_fragment(PagedFlowState* state, LayoutContext* lycon, PagedFlowTable* record,
    BlockContext* owner, ViewBlock* root);
static void paged_table_restore(PagedFlowState* state, PagedFlowTable* record, BlockContext* owner, ViewBlock* root);

// shrink earlier native flexible space before adding an avoidable page break (XSL 1.1 §4.3.1).
static float paged_shrink_preceding_spaces(PagedFlowState* state, BlockContext* owner,
        ViewBlock* block, size_t index, float needed, ViewBlock* root) {
    float reduced = 0.0f;
    for (ViewBlock* previous = paged_previous_flow_sibling(block); previous && needed > kPagedFlowEpsilon;
            previous = paged_previous_flow_sibling(previous)) {
        if (paged_fragmentainer_index(state, paged_flow_top(previous)) != index) break;
        ViewCssStyle* style = view_css_resolve(state->tree, previous);
        const RadiantSpaceSpec* space = style && style->flow_traits ? &style->flow_traits->after : nullptr;
        if (!space || !space->specified || !previous->bound || space->minimum >= space->optimum) continue;
        float available = fmaxf(0.0f, previous->boundary()->margin.bottom - space->minimum);
        float amount = fminf(needed, available);
        if (amount <= 0.0f) continue;
        BoundaryProp* boundary = previous->boundary_mut();
        boundary->margin.bottom -= amount;
        if (boundary->has_flow_margin) boundary->flow_margin.bottom -= amount;
        for (View* sibling = previous->next(); sibling; sibling = sibling->next()) {
            if (sibling->view_type && !(sibling->is_block() && layout_block_is_out_of_flow(lam::view_require_block(sibling))))
                paged_shift_view(sibling, -amount);
            if (sibling == block) break;
        }
        BlockContext* cursor = root ? owner : owner ? owner->parent.get() : nullptr;
        if (cursor) cursor->advance_y -= amount;
        needed -= amount; reduced += amount;
    }
    return reduced;
}

// re-run the fit of every line of the paragraphs inside `root` after they moved,
// and break the tables among them again
static TypesetStatus paged_refit(PagedFlowState* state, LayoutContext* lycon, BlockContext* owner, ViewBlock* root) {
    for (PagedFlowParagraph* paragraph = state->paragraphs; paragraph; paragraph = paragraph->next) {
        if (!paged_view_inside(paragraph->block, root)) continue;
        if (paragraph->table) {
            TypesetStatus status = paged_table_fragment(state, lycon, paragraph->table, owner, root);
            if (status != TYPESET_OK) return status;
            continue;
        }
        for (size_t i = 0; i < paragraph->count; i++) {
            TypesetStatus status = paged_fit_line(state, lycon, owner, paragraph, i, root, false);
            if (status != TYPESET_OK) return status;
        }
    }
    // footnotes and page floats anchored at boxes inside follow those boxes
    for (size_t i = 0; i < state->anchors_bound; i++)
        if (state->anchors[i].box && paged_view_inside(state->anchors[i].box, root)) paged_anchor_follow(state, &state->anchors[i]);
    return state->status;
}

// a break landed right before `box`; honour break-before: avoid on the box and
// break-after: avoid on its previous in-flow siblings by moving that chain to the
// box's fragmentainer as well. `strut` is the journal index of the break. `owner`
// is the context whose cursor passed the broken content: the box's own context
// while the box is open (box_live), else the context of the block containing
// the chain. A chain that cannot move (a sibling with its own breaks, or one
// already at its fragmentainer top) stays and the avoidance is relaxed.
static TypesetStatus paged_keep_chain(PagedFlowState* state, LayoutContext* lycon, BlockContext* owner,
        ViewBlock* box, size_t strut, bool box_live) {
    if (state->struts[strut].forced) return TYPESET_OK;
    ViewBlock* first = box;
    for (;;) {
        ViewBlock* previous = paged_previous_flow_sibling(first);
        if (!previous) break;
        ViewBreak before = paged_flow_break_keyword(first->blk ? first->block()->break_before : CSS_VALUE_AUTO);
        ViewBreak after = paged_flow_break_keyword(previous->blk ? previous->block()->break_after : CSS_VALUE_AUTO);
        if (!paged_break_is_avoid(before) && !paged_break_is_avoid(after)) break;
        bool movable = !paged_struts_inside(state, 0, previous);
        if (movable) {
            size_t index = paged_fragmentainer_index(state, paged_flow_top(previous));
            // moving a box that starts its fragmentainer would leave the page empty (CSS Break §4.3)
            movable = paged_flow_top(previous) > state->fragmentainers[index].top + kPagedFlowEpsilon;
        }
        if (!movable) { state->relaxed_avoidance++; break; }
        first = previous;
    }
    if (first == box) return TYPESET_OK;
    PagedStrut broke = state->struts[strut];
    log_debug("[PAGED_FLOW] keep chain from %s to %s: undo strut %zu (%.3f), box top=%.3f first top=%.3f",
        first->source_loc(), box->source_loc(), strut, broke.amount, paged_flow_top(box), paged_flow_top(first));
    size_t target = paged_fragmentainer_index(state, paged_flow_top(box) + (broke.paragraph ? broke.paragraph->lines[broke.line].top : 0.0f));
    float page_top = state->fragmentainers[target].top;
    // undo the break before the box, then break before the first kept sibling
    if (broke.paragraph) paged_shift_flow(state, owner, broke.paragraph, broke.line, nullptr, box_live ? nullptr : box, -broke.amount);
    else paged_shift_flow(state, owner, nullptr, -1, box, box, -broke.amount);
    state->strut_count = strut;
    float delta = page_top - paged_flow_top(first);
    BlockContext* chain_owner = box_live ? owner->parent.get() : owner;
    for (View* sibling = first; sibling; sibling = sibling->next()) {
        if (sibling->view_type && !(sibling->is_block() && layout_block_is_out_of_flow(lam::view_require_block(sibling))))
            paged_shift_view(sibling, delta);
        if (sibling == (View*)box) break;
    }
    if (chain_owner) chain_owner->advance_y += delta;
    log_debug("[PAGED_FLOW] keep chain moved by %.3f: first top=%.3f box top=%.3f", delta, paged_flow_top(first), paged_flow_top(box));
    TypesetStatus status = paged_strut_record(state, first, nullptr, -1, delta, false, target, paged_flow_top(first) - delta);
    if (status != TYPESET_OK) return status;
    // the kept chain and the box now start the target fragmentainer; their lines may overflow it
    for (View* sibling = first; status == TYPESET_OK && sibling; sibling = sibling->next()) {
        if (!sibling->view_type || !sibling->is_block()) continue;
        if (sibling == (View*)box && box_live) {
            PagedFlowParagraph* paragraph = state->paragraph && state->paragraph->block == box ? state->paragraph : nullptr;
            for (size_t i = 0; status == TYPESET_OK && paragraph && i < paragraph->count; i++)
                status = paged_fit_line(state, lycon, owner, paragraph, i, nullptr, false);
            break;
        }
        status = paged_refit(state, lycon, chain_owner, lam::view_require_block(sibling));
        if (sibling == (View*)box) break;
    }
    return status;
}

// the first content of `block` precedes `line` of its inline run
static bool paged_paragraph_starts_block(const PagedFlowParagraph* paragraph, size_t line) {
    if (line) return false;
    for (DomNode* node = paragraph->block->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        if (!child->view_type || child->view_type == RDT_VIEW_MARKER) continue;
        if (!child->is_block()) return true; // the run begins with inline content
        ViewBlock* candidate = lam::view_require_block(child);
        if (layout_block_is_out_of_flow(candidate)) continue;
        return candidate->display.outer == CSS_VALUE_INLINE_BLOCK;
    }
    return true;
}

// fit `line` of `paragraph`: a line that would cross its fragmentainer end moves
// to the next one (with the orphans before it), and a line that already starts a
// later fragmentainer by position is a break as well, so orphans, keeps and
// widows see it. `root` is the finished block containing the paragraph (null
// while the paragraph is the open block).
static TypesetStatus paged_fit_line(PagedFlowState* state, LayoutContext* lycon, BlockContext* owner,
        PagedFlowParagraph* paragraph, size_t line, ViewBlock* root, bool keeps) {
    float block_top = paged_flow_top(paragraph->block);
    PagedFlowLine* lines = paragraph->lines;
    float top = block_top + lines[line].top, height = lines[line].height;
    size_t index;
    TypesetStatus status = paged_fragmentainer_at(state, top, &index);
    if (status != TYPESET_OK) return status;
    lines[line].fragmentainer = index;
    // the line's footnotes and page floats are placed again with it
    for (uint32_t i = 0; i < lines[line].anchor_count; i++) paged_anchor_lift(state, &state->anchors[lines[line].anchor_first + i]);
    const PagedFragmentainer* fragmentainer = &state->fragmentainers[index];
    float reserve_top, reserve_bottom;
    paged_reserve(state, paragraph->block, index, &reserve_top, &reserve_bottom);
    float excess = top + height + reserve_bottom - fragmentainer->bottom;
    if (excess > kPagedFlowEpsilon && paged_shrink_preceding_spaces(state, owner,
            paragraph->block, index, excess, root) > 0.0f) {
        block_top = paged_flow_top(paragraph->block); top = block_top + lines[line].top;
    }
    bool starts = top <= fragmentainer->top + reserve_top + kPagedFlowEpsilon;
    // a line alone at the top of its fragmentainer overflows rather than moving forever
    bool crossing = isfinite(fragmentainer->bottom) && top + height + reserve_bottom > fragmentainer->bottom + kPagedFlowEpsilon && !starts;
    // a line in the space a fragmentainer reserves at its top moves below it
    bool transition = !crossing && ((line > 0 && lines[line - 1].fragmentainer < index) ||
        (index > 0 && top < fragmentainer->top + reserve_top - kPagedFlowEpsilon));
    // footnotes and page floats that do not fit beside the line move it (footnote-policy:
    // line), and a block policy moves the run's lines on the fragmentainer too (CSS GCPM 3 §2.7)
    bool block_policy = false;
    if (!crossing && lines[line].anchor_count) {
        PagedAnchorFit fit = paged_anchors_place(state, lines[line].anchor_first, lines[line].anchor_count, index,
            top + height + reserve_bottom, starts);
        if (fit == PAGED_ANCHORS_FAILED) return state->status;
        if (fit == PAGED_ANCHORS_MOVE) {
            crossing = true; transition = false;
            for (uint32_t i = 0; i < lines[line].anchor_count; i++) {
                const PagedFlowAnchor& anchor = state->anchors[lines[line].anchor_first + i];
                block_policy |= anchor.kind == PAGED_ANCHOR_NOTE && anchor.policy == PAGED_NOTE_BLOCK;
            }
        }
    }
    if (!crossing && !transition) return TYPESET_OK;
    size_t previous = crossing ? index : line > 0 ? lines[line - 1].fragmentainer : index - 1;
    size_t target = index;
    if (crossing) {
        status = paged_fragmentainer_at(state, fragmentainer->bottom + kPagedFlowEpsilon, &target);
        if (status != TYPESET_OK) return status;
    }
    size_t first = line;
    size_t on_previous = 0;
    while (first > 0 && lines[first - 1].fragmentainer == previous) { first--; on_previous++; }
    if (on_previous && (block_policy || (int)on_previous < paged_flow_line_limit(paragraph->block, false))) {
        // orphans: the lines before the break move with it unless they start the fragmentainer
        float previous_top, previous_bottom;
        paged_reserve(state, paragraph->block, previous, &previous_top, &previous_bottom);
        if (block_top + lines[first].top <= state->fragmentainers[previous].top + previous_top + kPagedFlowEpsilon) {
            first = line; state->relaxed_minima++;
        }
    } else first = line;
    float target_top, target_bottom;
    paged_reserve(state, paragraph->block, target, &target_top, &target_bottom);
    float strut = state->fragmentainers[target].top + target_top - (block_top + lines[first].top);
    if (strut < 0.0f) strut = 0.0f;
    status = paged_strut_record(state, paragraph->block, paragraph, (int)first, strut, false, target, block_top + lines[first].top);
    if (status != TYPESET_OK) return status;
    size_t recorded = state->strut_count - 1;
    paged_shift_flow(state, owner, paragraph, (int)first, nullptr, root, strut);
    for (size_t i = first; i <= line; i++) lines[i].fragmentainer = target;
    // the moved lines take their anchors along: the lines before the line start the
    // target, and when the line's own still do not fit beside it, it moves on
    if (first < line) {
        size_t from, count;
        paged_lines_anchors(paragraph, first, line - 1, &from, &count);
        if (count && paged_anchors_place(state, from, count, target, block_top + lines[line - 1].top +
            lines[line - 1].height + target_bottom, true) == PAGED_ANCHORS_FAILED) return state->status;
    }
    if (lines[line].anchor_count) {
        PagedAnchorFit fit = paged_anchors_place(state, lines[line].anchor_first, lines[line].anchor_count, target,
            block_top + lines[line].top + height + target_bottom, first == line);
        if (fit == PAGED_ANCHORS_FAILED) return state->status;
        if (fit == PAGED_ANCHORS_MOVE) return paged_fit_line(state, lycon, owner, paragraph, line, root, keeps);
    }
    if (keeps && paged_paragraph_starts_block(paragraph, first))
        return paged_keep_chain(state, lycon, owner, paragraph->block, recorded, root == nullptr);
    return TYPESET_OK;
}

// the inline run of `paragraph` ended: widows move the last break earlier when
// too few lines follow it (CSS Fragmentation 3 §4.4)
static TypesetStatus paged_paragraph_close(PagedFlowState* state, LayoutContext* lycon, BlockContext* owner,
        ViewBlock* root, bool keeps = true) {
    PagedFlowParagraph* paragraph = state->paragraph;
    state->paragraph = nullptr;
    if (!paragraph || paragraph->count < 2 || state->status != TYPESET_OK) return state->status;
    PagedFlowLine* lines = paragraph->lines;
    size_t broke = paragraph->count;
    while (broke > 1 && lines[broke - 1].fragmentainer == lines[broke - 2].fragmentainer) broke--;
    if (broke <= 1) return TYPESET_OK;
    broke--; // the first line after the last break
    size_t after = paragraph->count - broke;
    int widows = paged_flow_line_limit(paragraph->block, true);
    if ((int)after >= widows) return TYPESET_OK;
    size_t needed = (size_t)widows - after;
    size_t moved = broke > needed ? broke - needed : 0;
    // orphans stay satisfied or the whole run moves; a run starting its fragmentainer cannot move
    if (moved && (int)moved < paged_flow_line_limit(paragraph->block, false)) moved = 0;
    float block_top = paged_flow_top(paragraph->block);
    size_t previous = lines[broke - 1].fragmentainer, target = lines[broke].fragmentainer;
    float previous_top, previous_bottom, target_top, target_bottom;
    paged_reserve(state, paragraph->block, previous, &previous_top, &previous_bottom);
    paged_reserve(state, paragraph->block, target, &target_top, &target_bottom);
    if (!moved && block_top + lines[0].top <= state->fragmentainers[previous].top + previous_top + kPagedFlowEpsilon) {
        state->relaxed_minima++;
        return TYPESET_OK;
    }
    // widows leave a line with footnotes or page floats where its reservation stands
    size_t anchors_from, anchors_count;
    paged_lines_anchors(paragraph, moved, broke - 1, &anchors_from, &anchors_count);
    if (anchors_count) {
        state->relaxed_minima++;
        return TYPESET_OK;
    }
    // the moved lines join the lines after the break at the target's top; the old
    // strut (if any) closes up behind them
    size_t old = state->strut_count;
    while (old > 0 && !(state->struts[old - 1].paragraph == paragraph && state->struts[old - 1].line == (int)broke)) old--;
    float old_amount = old ? state->struts[old - 1].amount : 0.0f;
    float before = block_top + lines[moved].top;
    float delta = state->fragmentainers[target].top + target_top - before;
    paged_shift_flow(state, owner, paragraph, (int)moved, nullptr, root, delta);
    if (old_amount != 0.0f) paged_shift_flow(state, owner, paragraph, (int)broke, nullptr, root, -old_amount);
    if (old) {
        if (old == state->strut_count) state->strut_count--;
        else state->struts[old - 1].amount = 0.0f;
    }
    TypesetStatus status = paged_strut_record(state, paragraph->block, paragraph, (int)moved, delta, false, target, before);
    if (status != TYPESET_OK) return status;
    size_t recorded = state->strut_count - 1;
    for (size_t i = moved; i < paragraph->count; i++) lines[i].fragmentainer = target;
    // the lines that followed the old break may now cross the target's end
    for (size_t i = broke; status == TYPESET_OK && i < paragraph->count; i++)
        status = paged_fit_line(state, lycon, owner, paragraph, i, root, false);
    if (status == TYPESET_OK && keeps && paged_paragraph_starts_block(paragraph, moved))
        status = paged_keep_chain(state, lycon, owner, paragraph->block, recorded, root == nullptr);
    return status;
}

// the page sequence an element belongs to: its own binding or the nearest ancestor's
static const RadiantPageSequence* paged_flow_sequence(PagedFlowState* state, DomElement* element) {
    const RadiantPageDocument* document = state->tree->model->css->page_document;
    for (DomElement* owner = element; owner; owner = owner->parent_element()) {
        const RadiantPageSequence* sequence = radiant_page_sequence_for(document, owner);
        if (sequence) return sequence;
    }
    return nullptr;
}

// page-only metadata reads the same cascaded declarations as browsing layout.
static const CssValue* paged_flow_declared(PagedFlowState*, DomElement* element, const char* name) {
    CssPropertyCode property = css_property_code_from_name(name);
    const CssDeclaration* declaration = element && property > 0
        ? style_tree_get_declaration(element->specified_style, property) : nullptr;
    return declaration ? declaration->value : nullptr;
}

// the authored `page` name of an element from the paged cascade's sources
static const char* paged_flow_page_name(PagedFlowState* state, DomElement* element) {
    for (DomElement* owner = element; owner; owner = owner->parent_element()) {
        const CssValue* value = paged_flow_declared(state, owner, "page");
        if (!value || css_value_is_inherit(value) || css_value_is_unset(value)) continue;
        const char* name = css_value_identifier_name(value);
        return name && strcmp(name, "auto") != 0 ? name : nullptr;
    }
    return nullptr;
}

static bool paged_flow_same_name(const char* a, const char* b) {
    return a == b || (a && b && strcmp(a, b) == 0);
}

// ----- tables: row breaks, cell continuations and repeated groups (plan P3) -----
// layout_table places a table whole; when the table ends, paged_table_fragment
// breaks it between rows. A row that crosses its fragmentainer's body end moves
// to the next fragmentainer when a fresh one holds it (rows joined by a row span
// move together, a group with break-inside: avoid moves whole); a row that
// cannot move continues inside its cells, whose finished lines and boxes are
// fitted like flow content (orphans and widows included), and the row grows by
// its largest continuation. Header and footer groups no taller than a quarter
// of the fragmentainer repeat on every page the table spans (CSS 2.2 §17.2), so
// the body rows keep their extent free at each fragmentainer's top and bottom.

enum PagedTableItemKind : uint8_t { PAGED_ITEM_CAPTION, PAGED_ITEM_HEADER, PAGED_ITEM_FOOTER, PAGED_ITEM_ROW };
struct PagedTableItem {
    ViewBlock* view;
    ViewBlock* group;          // the row group of a row (the table for a direct row)
    PagedTableItemKind kind;
};

// copy the internal geometry of a table subtree to or from `values` in walk
// order (box y, height and content height, text rect y); a null `values` counts
static size_t paged_table_geometry(ViewElement* parent, float* values, size_t at, bool restore) {
    for (DomNode* node = parent->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        if (!child->view_type) continue;
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(child);
            for (TextRect* rect = text->rect; rect; rect = rect->next, at++)
                if (values) { if (restore) rect->y = values[at]; else values[at] = rect->y; }
            if (values && restore) adjust_text_bounds(text);
            continue;
        }
        if (!child->is_element()) continue;
        ViewBlock* box = child->is_block() ? lam::view_require_block(child) : nullptr;
        float* fields[3] = {&child->y, box ? &box->height : nullptr, box ? &box->content_height : nullptr};
        for (size_t i = 0; i < 3; i++) {
            if (!fields[i]) continue;
            if (values) { if (restore) *fields[i] = values[at]; else values[at] = *fields[i]; }
            at++;
        }
        at = paged_table_geometry(lam::view_require_element(child), values, at, restore);
    }
    return at;
}

// undo a table's breaks: its internal geometry returns to what layout_table
// produced and the space the breaks added leaves the flow
static void paged_table_restore(PagedFlowState* state, PagedFlowTable* record, BlockContext* owner, ViewBlock* root) {
    (void)state;
    if (!record->modified) return;
    ViewBlock* table = record->table;
    table->height = record->snapshot[0]; table->content_height = record->snapshot[1];
    paged_table_geometry(table, record->snapshot, 2, true);
    paged_flow_grow(owner, table, false, root, -record->growth);
    record->growth = 0.0f; record->modified = false; record->strut_count = 0;
}

// open `delta` of space at flow position `at` among the boxes of a table
// subtree: boxes starting at or after it move, boxes spanning it grow (a row
// span crossing a break grows with it), and `ending` grows the boxes ending at it.
// A growing cell keeps its content at its top unless `content` moves the content
// too: text rects and inline views (placed in their block's frame) at or after
// `at`, with atomic inlines moving whole; positioned boxes stay
static void paged_table_open(ViewElement* parent, float parent_top, float at, float delta, bool ending, bool content = false) {
    for (DomNode* node = parent->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        if (!child->view_type) continue;
        if (content && child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(child);
            for (TextRect* rect = text->rect; rect; rect = rect->next)
                if (parent_top + rect->y >= at - kPagedFlowEpsilon) rect->y += delta;
            adjust_text_bounds(text);
            continue;
        }
        if (content && (!child->is_block() || child->view_type == RDT_VIEW_INLINE_BLOCK)) {
            if (parent_top + child->y >= at - kPagedFlowEpsilon) child->y += delta;
            if (child->view_type == RDT_VIEW_INLINE) paged_table_open(lam::view_require_element(child), parent_top, at, delta, ending, content);
            continue;
        }
        if (!child->is_block()) continue;
        ViewBlock* box = lam::view_require_block(child);
        if (content && layout_block_is_out_of_flow_positioned(box)) continue;
        float top = parent_top + box->y, bottom = top + box->height;
        if (top >= at - kPagedFlowEpsilon) { box->y += delta; continue; }
        if (bottom <= at + (ending ? -kPagedFlowEpsilon : kPagedFlowEpsilon)) continue;
        box->height += delta; box->content_height += delta;
        if (box->view_type != RDT_VIEW_TABLE_CELL || content) paged_table_open(box, top, at, delta, ending, content);
    }
}

// record the geometry layout_table produced before the first break; a later
// pass restores it before breaking the table again
static TypesetStatus paged_table_snapshot(PagedFlowState* state, PagedFlowTable* record) {
    if (record->snapshot) return TYPESET_OK;
    ViewBlock* table = record->table;
    size_t count = paged_table_geometry(table, nullptr, 2, false);
    record->snapshot = (float*)pool_alloc(state->composition->pool, count * sizeof(float));
    if (!record->snapshot) return TYPESET_OUT_OF_MEMORY;
    record->snapshot_count = count;
    record->snapshot[0] = table->height; record->snapshot[1] = table->content_height;
    paged_table_geometry(table, record->snapshot, 2, false);
    return TYPESET_OK;
}

static TypesetStatus paged_table_insert(PagedFlowState* state, PagedFlowTable* record, BlockContext* owner,
        ViewBlock* root, float at, float delta, bool ending) {
    (void)state;
    if (delta == 0.0f) return TYPESET_OK;
    ViewBlock* table = record->table;
    paged_table_open(table, paged_flow_top(table), at, delta, ending);
    table->height += delta; table->content_height += delta;
    paged_flow_grow(owner, table, false, root, delta);
    record->growth += delta; record->modified = true;
    return TYPESET_OK;
}

static TypesetStatus paged_fit_finished(PagedFlowState* state, LayoutContext* lycon, ViewBlock* block, ViewBlock* root);

static void paged_geometry_line(PagedFlowParagraph* paragraph, int number, float top, float bottom) {
    size_t i = 0;
    while (i < paragraph->count && paragraph->lines[i].number < number) i++;
    if (i < paragraph->count && paragraph->lines[i].number == number) {
        PagedFlowLine& line = paragraph->lines[i];
        float end = fmaxf(line.top + line.height, bottom);
        line.top = fminf(line.top, top); line.height = end - line.top;
        return;
    }
    memmove(paragraph->lines + i + 1, paragraph->lines + i, (paragraph->count - i) * sizeof(PagedFlowLine));
    paragraph->lines[i] = {top, bottom - top, 0, number};
    paragraph->count++;
}

// the lines of a finished inline run, rebuilt from its rects and atomic inlines
// by line number; a line spans the extents of what it holds
static TypesetStatus paged_geometry_collect(PagedFlowState* state, PagedFlowParagraph* paragraph, DomNode* first, DomNode* end) {
    for (DomNode* node = first; node && node != end; node = node->next_sibling) {
        View* child = (View*)node;
        if (!child->view_type) continue;
        // the worst case is one line per item; grow before inserting
        if (!lam::pool_grow_array(state->composition->pool, &paragraph->lines, &paragraph->capacity, paragraph->count + 1, 16))
            return TYPESET_OUT_OF_MEMORY;
        if (child->view_type == RDT_VIEW_TEXT) {
            for (TextRect* rect = lam::view_require_text(child)->rect; rect; rect = rect->next) {
                if (!lam::pool_grow_array(state->composition->pool, &paragraph->lines, &paragraph->capacity, paragraph->count + 1, 16))
                    return TYPESET_OUT_OF_MEMORY;
                paged_geometry_line(paragraph, rect->line_number, rect->y, rect->y + rect->height);
            }
        } else if (child->view_type == RDT_VIEW_INLINE) {
            TypesetStatus status = paged_geometry_collect(state, paragraph, lam::view_require_element(child)->first_child, nullptr);
            if (status != TYPESET_OK) return status;
        } else if (child->view_type == RDT_VIEW_INLINE_BLOCK) {
            paged_geometry_line(paragraph, child->inline_line_number, child->y, child->y + lam::view_require_block(child)->height);
        }
    }
    return TYPESET_OK;
}

static void paged_note_break_edge(ViewElement* element, float origin, float lower, float upper, float* edge) {
    for (DomNode* node = element->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        if (!child->view_type) continue;
        if (child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
            if (layout_block_is_out_of_flow(block)) continue;
            if (!paged_flow_monolithic(block)) paged_note_break_edge(block, origin + block->y, lower, upper, edge);
            float bottom = origin + block->y + block->height;
            if (bottom > lower + kPagedFlowEpsilon && bottom <= upper + kPagedFlowEpsilon) *edge = fmaxf(*edge, bottom);
        } else if (child->view_type == RDT_VIEW_TEXT) {
            for (TextRect* rect = lam::view_require_text(child)->rect; rect; rect = rect->next) {
                float bottom = origin + rect->y + rect->height;
                if (bottom > lower + kPagedFlowEpsilon && bottom <= upper + kPagedFlowEpsilon) *edge = fmaxf(*edge, bottom);
            }
        } else if (child->view_type == RDT_VIEW_INLINE)
            paged_note_break_edge(lam::view_require_element(child), origin, lower, upper, edge);
    }
}

static float paged_note_piece_height(PagedFlowAnchor* anchor, float offset, float available) {
    if (available <= 0.0f) return 0.0f;
    if (anchor->full_height - offset <= available + kPagedFlowEpsilon) return anchor->full_height - offset;
    float edge = offset;
    paged_note_break_edge((ViewElement*)anchor->element, 0.0f, offset, offset + available, &edge);
    return edge - offset;
}

static TypesetStatus paged_note_continue(PagedFlowState* state, PagedFlowAnchor* anchor, size_t index) {
    float offset = anchor->height;
    PagedNotePiece** tail = &anchor->continuation;
    while (offset < anchor->full_height - kPagedFlowEpsilon) {
        index++;
        if (index >= state->fragmentainer_count) {
            TypesetStatus status = paged_fragmentainer_append(state, false);
            if (status != TYPESET_OK) return status;
        }
        const PagedNoteArea* area = paged_fragmentainer_area(state, index);
        if (!area) return state->status;
        float width = area->width - area->edges[1] - area->edges[3];
        if (fabsf(width - anchor->width) > kPagedFlowEpsilon)
            return paged_flow_fail(state, TYPESET_UNPLACEABLE, anchor->element, "a continued note requires reflow at its changed width");
        PagedFragmentainer& target = state->fragmentainers[index];
        float capacity = target.rect.height - target.float_top - target.float_bottom - area->margin[0] - area->margin[2] -
            area->edges[0] - area->edges[2] - target.notes;
        float height = paged_note_piece_height(anchor, offset, capacity);
        if (height <= 0.0f) return paged_flow_fail(state, TYPESET_UNPLACEABLE, anchor->element,
            "an indivisible note line or box is taller than its continuation area");
        PagedNotePiece* piece = (PagedNotePiece*)pool_alloc(state->composition->pool, sizeof(PagedNotePiece));
        if (!piece) return paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, anchor->element, nullptr);
        *piece = {offset, height, index, nullptr}; *tail = piece; tail = &piece->next;
        target.notes += height; target.note_count++; offset += height;
    }
    return TYPESET_OK;
}

// fit one finished inline run of `block`, from `first` up to `end`
static TypesetStatus paged_fit_run(PagedFlowState* state, LayoutContext* lycon, ViewBlock* block,
        DomNode* first, DomNode* end, ViewBlock* root) {
    if (first == end) return TYPESET_OK;
    PagedFlowParagraph* paragraph = (PagedFlowParagraph*)pool_calloc(state->composition->pool, sizeof(PagedFlowParagraph));
    if (!paragraph) return TYPESET_OUT_OF_MEMORY;
    paragraph->block = block;
    TypesetStatus status = paged_geometry_collect(state, paragraph, first, end);
    for (size_t i = 0; status == TYPESET_OK && i < paragraph->count; i++)
        status = paged_fit_line(state, lycon, nullptr, paragraph, i, root, false);
    if (status != TYPESET_OK || paragraph->count < 2) return status;
    state->paragraph = paragraph;
    return paged_paragraph_close(state, lycon, nullptr, root, false);
}

// a finished block-level box inside finished content: monolithic boxes move to
// a fragmentainer that holds them, other boxes are entered
static TypesetStatus paged_fit_box(PagedFlowState* state, LayoutContext* lycon, ViewBlock* box, ViewBlock* root) {
    float top = paged_flow_top(box);
    size_t index;
    TypesetStatus status = paged_fragmentainer_at(state, top, &index);
    if (status != TYPESET_OK) return status;
    const PagedFragmentainer* fragmentainer = &state->fragmentainers[index];
    float reserve_top, reserve_bottom;
    paged_reserve(state, box, index, &reserve_top, &reserve_bottom);
    if (!isfinite(fragmentainer->bottom) || top + box->height + reserve_bottom <= fragmentainer->bottom + kPagedFlowEpsilon)
        return TYPESET_OK;
    if (paged_flow_monolithic(box) || paged_flow_closed(box)) {
        size_t next;
        status = paged_fragmentainer_at(state, fragmentainer->bottom + kPagedFlowEpsilon, &next);
        if (status != TYPESET_OK) return status;
        float next_top, next_bottom;
        paged_reserve(state, box, next, &next_top, &next_bottom);
        const PagedFragmentainer& target = state->fragmentainers[next];
        bool starts = top <= fragmentainer->top + reserve_top + kPagedFlowEpsilon;
        if (!starts && box->height <= target.bottom - target.top - next_top - next_bottom + kPagedFlowEpsilon) {
            float delta = target.top + next_top - top;
            status = paged_strut_record(state, box, nullptr, -1, delta, false, next, top);
            if (status == TYPESET_OK) paged_shift_flow(state, nullptr, nullptr, -1, box, root, delta);
            return status;
        }
        if (paged_flow_closed(box)) return TYPESET_OK; // sliced by publication
        state->relaxed_avoidance++;
    }
    return paged_fit_finished(state, lycon, box, root);
}

// fit the finished content of `block` (a table cell or caption that layout_table
// laid out outside the page flow) against the fragmentainers, in flow order: its
// inline runs line by line and its block-level boxes; `root` bounds the growth
static TypesetStatus paged_fit_finished(PagedFlowState* state, LayoutContext* lycon, ViewBlock* block, ViewBlock* root) {
    DomNode* run = block->first_child;
    for (DomNode* node = block->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        if (!child->view_type || !child->is_block()) continue;
        ViewBlock* box = lam::view_require_block(child);
        if (!paged_flow_block(box)) continue;
        TypesetStatus status = paged_fit_run(state, lycon, block, run, node, root);
        if (status == TYPESET_OK) status = paged_fit_box(state, lycon, box, root);
        if (status != TYPESET_OK) return status;
        run = node->next_sibling;
    }
    return paged_fit_run(state, lycon, block, run, nullptr, root);
}

static ViewBreak paged_box_break(const ViewBlock* box, bool after) {
    const BlockProp* blk = box && box->blk ? box->block() : nullptr;
    return paged_flow_break_keyword(blk ? (after ? blk->break_after : blk->break_before) : CSS_VALUE_AUTO);
}

// the strongest break value at the boundary before items[i]: the values of the
// rows and groups meeting there combine (CSS Fragmentation 3 §3.1)
static ViewBreak paged_table_boundary(const PagedTableItem* items, size_t i) {
    const PagedTableItem& item = items[i];
    const PagedTableItem& previous = items[i - 1];
    ViewBreak value = paged_box_break(item.view, false);
    ViewBreak after = paged_box_break(previous.view, true);
    if (after > value) value = after;
    bool rows = item.kind == PAGED_ITEM_ROW && previous.kind == PAGED_ITEM_ROW;
    if (!rows || item.group != previous.group) {
        ViewBreak group = item.kind == PAGED_ITEM_ROW && item.group->view_type == RDT_VIEW_TABLE_ROW_GROUP
            ? paged_box_break(item.group, false) : VIEW_BREAK_AUTO;
        if (group > value) value = group;
        group = previous.kind == PAGED_ITEM_ROW && previous.group->view_type == RDT_VIEW_TABLE_ROW_GROUP
            ? paged_box_break(previous.group, true) : VIEW_BREAK_AUTO;
        if (group > value) value = group;
    }
    return value;
}

// the extent of a table item: a row reaches the bottom of its row-spanning cells
static float paged_table_item_bottom(const PagedTableItem& item) {
    float top = paged_flow_top(item.view), bottom = top + item.view->height;
    if (item.kind != PAGED_ITEM_ROW) return bottom;
    ViewTableRow* row = static_cast<ViewTableRow*>(item.view);
    row->each_cell([&](ViewTableCell* cell) { bottom = fmaxf(bottom, top + cell->y + cell->height); });
    return bottom;
}

// the first fragmentainer after `index` that a forced break of `value` reaches
static TypesetStatus paged_forced_target(PagedFlowState* state, size_t index, ViewBreak value, size_t* target) {
    PagedPageCursor* composer = &state->composer;
    if (index + 1 < state->fragmentainer_count) { *target = index + 1; return TYPESET_OK; }
    composer->requested_break = value;
    TypesetStatus status = paged_fragmentainer_append(state, value != VIEW_BREAK_COLUMN || !paged_has_next_column(composer));
    *target = state->fragmentainer_count - 1;
    return status;
}

// a split cell aligns each of its slices on its own (plan §8 ruling 4). Before
// the cells are fitted, middle and bottom cells move their content to the top
// (paged_table_align_slices places each slice once the breaks are final), and
// baseline cells keep the row's alignment on its first fragmentainer while an
// extra reserve aligns their first lines again on each later one: the anchor of
// a continuation is its reserve plus the baseline offset of the cell's first
// line, which stands for the first line of each continuation
static TypesetStatus paged_table_split_cells(PagedFlowState* state, LayoutContext* lycon, PagedFlowTable* record,
        ViewTableRow* row) {
    size_t first = paged_fragmentainer_index(state, paged_flow_top(row));
    size_t begin = record->split_count;
    float anchor = -INFINITY;
    TypesetStatus status = TYPESET_OK;
    row->each_cell([&](ViewTableCell* cell) {
        int align = cell->td ? cell->td->vertical_align : TableCellProp::CELL_VALIGN_TOP;
        float min_y, max_y;
        if (status != TYPESET_OK || align == TableCellProp::CELL_VALIGN_TOP ||
            !layout_table_cell_content_extent(lycon, cell, -INFINITY, INFINITY, &min_y, &max_y)) return;
        if (!lam::pool_grow_array(state->composition->pool, &record->split, &record->split_capacity, record->split_count + 1, 8)) {
            status = TYPESET_OUT_OF_MEMORY; return;
        }
        float offset = 0.0f;
        if (align == TableCellProp::CELL_VALIGN_BASELINE) {
            float reserve_top, reserve_bottom;
            paged_reserve(state, cell, first + 1, &reserve_top, &reserve_bottom);
            offset = reserve_top - min_y;
            anchor = fmaxf(anchor, offset);
        } else {
            TableCellInsets insets = layout_table_cell_alignment_insets(cell);
            paged_table_open(cell, paged_flow_top(cell), -INFINITY, insets.border.top + insets.padding.top - min_y, false, true);
        }
        record->split[record->split_count++] = {cell, first, offset};
    });
    // the offsets apply once every baseline cell of the row is known
    for (size_t i = begin; i < record->split_count; i++)
        if (record->split[i].cell->td->vertical_align == TableCellProp::CELL_VALIGN_BASELINE)
            record->split[i].offset = anchor - record->split[i].offset;
    return status;
}

// a row that cannot move whole continues inside its cells; the row then grows
// by the largest continuation, and every box after it moves
static TypesetStatus paged_table_split_row(PagedFlowState* state, LayoutContext* lycon, PagedFlowTable* record,
        BlockContext* owner, ViewBlock* root, ViewTableRow* row) {
    float growth = 0.0f;
    TypesetStatus status = paged_table_split_cells(state, lycon, record, row);
    row->each_cell([&](ViewTableCell* cell) {
        if (status != TYPESET_OK) return;
        float height = cell->height, content = cell->content_height;
        status = paged_fit_finished(state, lycon, cell, cell);
        growth = fmaxf(growth, cell->height - height);
        cell->height = height; cell->content_height = content;
    });
    if (status != TYPESET_OK || growth <= 0.0f) return status;
    return paged_table_insert(state, record, owner, root, paged_flow_top(row) + row->height, growth, true);
}

static TypesetStatus paged_table_collect(PagedFlowState* state, ViewTable* table, PagedTableItem** result, size_t* count,
        ViewBlock** header, ViewBlock** footer) {
    size_t capacity = 0;
    PagedTableItem* items = nullptr;
    *count = 0; *header = *footer = nullptr;
    TypesetStatus status = TYPESET_OK;
    auto append = [&](ViewBlock* view, ViewBlock* group, PagedTableItemKind kind) {
        if (status != TYPESET_OK) return;
        if (!lam::pool_grow_array(state->composition->pool, &items, &capacity, *count + 1, 16)) { status = TYPESET_OUT_OF_MEMORY; return; }
        items[(*count)++] = {view, group, kind};
    };
    table->each_direct_block([&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_COLUMN || child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP ||
            layout_block_is_out_of_flow(child)) return;
        if (child->view_type == RDT_VIEW_TABLE_ROW) { append(child, table, PAGED_ITEM_ROW); return; }
        if (child->view_type != RDT_VIEW_TABLE_ROW_GROUP) { append(child, nullptr, PAGED_ITEM_CAPTION); return; }
        ViewTableRowGroup* group = static_cast<ViewTableRowGroup*>(child);
        TableSectionType section = group->get_section_type();
        // the first header and footer groups are the table's furniture (CSS 2.2 §17.2)
        if (section == TABLE_SECTION_THEAD && !*header) { *header = child; append(child, nullptr, PAGED_ITEM_HEADER); return; }
        if (section == TABLE_SECTION_TFOOT && !*footer) { *footer = child; append(child, nullptr, PAGED_ITEM_FOOTER); return; }
        group->each_row_with_block([&](ViewTableRow*, ViewBlock* row) { append(row, child, PAGED_ITEM_ROW); });
    });
    // the items in visual order: the footer group follows the body wherever its source is
    for (size_t i = 1; status == TYPESET_OK && i < *count; i++) {
        PagedTableItem item = items[i];
        float top = paged_flow_top(item.view);
        size_t j = i;
        while (j > 0 && paged_flow_top(items[j - 1].view) > top + kPagedFlowEpsilon) { items[j] = items[j - 1]; j--; }
        items[j] = item;
    }
    *result = items;
    return status;
}

static float* paged_table_ends(PagedFlowState* state, const PagedFlowTable* record, size_t last);

// once a table's breaks are final, the content of each slice of a split middle
// or bottom cell is placed in that slice's alignment area: inside the cell's
// edges on its first and last fragmentainers, and between the cloned edges, the
// repeated header and the body end the publication windows use elsewhere
static TypesetStatus paged_table_align_slices(PagedFlowState* state, LayoutContext* lycon, PagedFlowTable* record) {
    if (!record->split_count) return TYPESET_OK;
    float table_top = paged_flow_top(record->table);
    size_t last = paged_fragmentainer_index(state, table_top + record->table->height - kPagedFlowEpsilon);
    float* ends = last > record->first ? paged_table_ends(state, record, last) : nullptr;
    if (last > record->first && !ends) return TYPESET_OUT_OF_MEMORY;
    for (size_t i = 0; i < record->split_count; i++) {
        ViewTableCell* cell = record->split[i].cell;
        int align = cell->td->vertical_align;
        if (align != TableCellProp::CELL_VALIGN_MIDDLE && align != TableCellProp::CELL_VALIGN_BOTTOM) continue;
        float top = paged_flow_top(cell), bottom = top + cell->height;
        size_t first = paged_fragmentainer_index(state, top), end = paged_fragmentainer_index(state, bottom - kPagedFlowEpsilon);
        TableCellInsets insets = layout_table_cell_alignment_insets(cell);
        float clone_top, clone_bottom;
        paged_box_clone_edges(state, cell, &clone_top, &clone_bottom);
        for (size_t k = first; k <= end; k++) {
            const PagedFragmentainer& fragmentainer = state->fragmentainers[k];
            float area_top = top + insets.border.top + insets.padding.top;
            float area_bottom = bottom - insets.border.bottom - insets.padding.bottom;
            if (k > first) {
                float reserve_top, reserve_bottom;
                paged_reserve(state, cell, k, &reserve_top, &reserve_bottom);
                area_top = fragmentainer.top + reserve_top;
            }
            if (k < end) area_bottom = fminf(bottom, ends && k < last ? ends[k - record->first] : fragmentainer.bottom) - clone_bottom;
            float slice_top = fmaxf(top, fragmentainer.top), slice_bottom = fminf(bottom, fragmentainer.bottom);
            float min_y, max_y;
            if (!layout_table_cell_content_extent(lycon, cell, slice_top - top, slice_bottom - top, &min_y, &max_y)) continue;
            float target = layout_table_cell_vertical_align_target(align, area_bottom - area_top, max_y - min_y, area_top, true);
            float delta = target - (top + min_y);
            if (delta <= kPagedFlowEpsilon) continue;
            // the slice's content moves: space opens above it and closes below it
            paged_table_open(cell, top, slice_top, delta, false, true);
            paged_table_open(cell, top, slice_bottom + delta, -delta, false, true);
        }
    }
    return TYPESET_OK;
}

// break a table that layout_table placed whole. `owner` is the context whose
// cursor passed `root`, the finished box bounding the growth (the table itself
// when its layout just ended). A record that broke before is restored first.
static TypesetStatus paged_table_fragment(PagedFlowState* state, LayoutContext* lycon, PagedFlowTable* record,
        BlockContext* owner, ViewBlock* root) {
    if (state->status != TYPESET_OK) return state->status;
    paged_table_restore(state, record, owner, root);
    ViewBlock* block = record->table;
    if (block->view_type != RDT_VIEW_TABLE) return TYPESET_OK;
    ViewTable* table = static_cast<ViewTable*>(block);
    // a table acting as its own row has no row boundaries; publication slices it
    if (table->acts_as_row()) return TYPESET_OK;
    PagedTableItem* items = nullptr;
    size_t count = 0;
    ViewBlock* header = nullptr; ViewBlock* footer = nullptr;
    TypesetStatus status = paged_table_collect(state, table, &items, &count, &header, &footer);
    if (status != TYPESET_OK) return status;
    float table_top = paged_flow_top(block);
    size_t first;
    status = paged_fragmentainer_at(state, table_top, &first);
    if (status != TYPESET_OK) return status;
    const PagedFragmentainer* start = &state->fragmentainers[first];
    if (!isfinite(start->bottom)) return TYPESET_OK;
    const TableProp* prop = table->table();
    float spacing = prop && !prop->border_collapse ? prop->border_spacing_v : 0.0f;
    // furniture that would crowd the body out does not repeat (browsers repeat
    // header and footer groups no taller than a quarter of the fragmentainer)
    float limit = (start->bottom - start->top) * 0.25f;
    record->header = header && header->height <= limit && !radiant_table_omits_repeat(table, false) ? header : nullptr;
    record->footer = footer && footer->height <= limit && !radiant_table_omits_repeat(table, true) ? footer : nullptr;
    record->spacing = spacing;
    record->header_size = spacing + (record->header ? record->header->height + spacing : 0.0f);
    record->footer_size = spacing + (record->footer ? record->footer->height + spacing : 0.0f);
    record->first = first;
    record->strut_count = 0; record->split_count = 0;
    float table_reserve_top, table_reserve_bottom;
    paged_reserve(state, block, first, &table_reserve_top, &table_reserve_bottom);
    if (table_top + block->height + table_reserve_bottom <= start->bottom + kPagedFlowEpsilon) return TYPESET_OK;
    // the leading part (captions, header, first body rows) stays with the table:
    // when it cannot stay on this fragmentainer the whole table moves (or the
    // whole table, under break-inside: avoid)
    size_t lead = 0;
    while (lead < count && items[lead].kind != PAGED_ITEM_ROW) lead++;
    float lead_bottom = paged_flow_avoids_inside(block) || lead == count ? table_top + block->height
        : paged_table_item_bottom(items[lead]) + record->footer_size;
    if (lead_bottom + table_reserve_bottom > start->bottom + kPagedFlowEpsilon && table_top > start->top + table_reserve_top + kPagedFlowEpsilon) {
        size_t next;
        status = paged_fragmentainer_at(state, start->bottom + kPagedFlowEpsilon, &next);
        if (status != TYPESET_OK) return status;
        float next_top, next_bottom;
        paged_reserve(state, block, next, &next_top, &next_bottom);
        const PagedFragmentainer& target = state->fragmentainers[next];
        if (lead_bottom - table_top <= target.bottom - target.top - next_top - next_bottom + kPagedFlowEpsilon) {
            float delta = target.top + next_top - table_top;
            status = paged_strut_record(state, block, nullptr, -1, delta, false, next, table_top);
            if (status != TYPESET_OK) return status;
            paged_shift_flow(state, owner, nullptr, -1, block, root, delta);
            table_top += delta; first = record->first = next;
            if (table_top + block->height + next_bottom <= target.bottom + kPagedFlowEpsilon) return TYPESET_OK;
        } else if (paged_flow_avoids_inside(block)) state->relaxed_avoidance++;
    }
    status = paged_table_snapshot(state, record);
    if (status != TYPESET_OK) return status;
    record->outer = state->table;
    state->table = record;
    size_t journal = state->strut_count;
    bool forced_inside = false;
    for (size_t i = 0; status == TYPESET_OK && i < count; ) {
        const PagedTableItem& item = items[i];
        bool row = item.kind == PAGED_ITEM_ROW;
        float top = paged_flow_top(item.view);
        size_t index;
        status = paged_fragmentainer_at(state, top, &index);
        if (status != TYPESET_OK) break;
        const PagedFragmentainer* fragmentainer = &state->fragmentainers[index];
        float reserve_top, reserve_bottom;
        paged_reserve(state, item.view, index, &reserve_top, &reserve_bottom);
        bool has_content = top > fragmentainer->top + reserve_top + kPagedFlowEpsilon;
        ViewBreak boundary = i > 0 ? paged_table_boundary(items, i) : VIEW_BREAK_AUTO;
        size_t target = index;
        float delta = 0.0f;
        bool forced = false;
        if (boundary >= VIEW_BREAK_COLUMN && has_content && row && items[i - 1].kind == PAGED_ITEM_ROW) {
            // an authored break between body rows; the header and footer groups are
            // furniture of every fragment, so no break separates them from the body
            status = paged_forced_target(state, index, boundary, &target);
            forced = true;
        } else if (index > first && top < fragmentainer->top + reserve_top - kPagedFlowEpsilon) {
            // a box in the space a continuation keeps for the repeated header moves below it
            target = index;
        } else {
            // the extent that moves together: rows joined by a row span
            size_t last = i;
            float bottom = paged_table_item_bottom(item);
            while (row && last + 1 < count && items[last + 1].kind == PAGED_ITEM_ROW && items[last + 1].group == item.group &&
                paged_flow_top(items[last + 1].view) < bottom - kPagedFlowEpsilon)
                bottom = fmaxf(bottom, paged_table_item_bottom(items[++last]));
            if (bottom + reserve_bottom <= fragmentainer->bottom + kPagedFlowEpsilon) { i = last + 1; continue; }
            size_t next;
            status = paged_fragmentainer_at(state, fragmentainer->bottom + kPagedFlowEpsilon, &next);
            if (status != TYPESET_OK) break;
            float next_top, next_bottom;
            paged_reserve(state, item.view, next, &next_top, &next_bottom);
            float capacity = state->fragmentainers[next].bottom - state->fragmentainers[next].top - next_top - next_bottom;
            // break-inside: avoid on a row group moves the whole group, and
            // break-before/after: avoid between rows moves the earlier rows along
            // (CSS Fragmentation 3 §4.3); a move that would empty its fragmentainer
            // or that a fresh fragmentainer cannot hold is relaxed
            size_t from = i + 1;
            if (row && item.group->view_type == RDT_VIEW_TABLE_ROW_GROUP && paged_flow_avoids_inside(item.group)) {
                float group_top = paged_flow_top(item.group);
                if (group_top > fragmentainer->top + reserve_top + kPagedFlowEpsilon && item.group->height <= capacity + kPagedFlowEpsilon) {
                    from = i;
                    while (from > 0 && items[from - 1].group == item.group) from--;
                } else state->relaxed_avoidance++;
            }
            if (from > i) {
                size_t chain = i;
                while (row && chain > 0 && items[chain - 1].kind == PAGED_ITEM_ROW && paged_break_is_avoid(paged_table_boundary(items, chain)))
                    chain--;
                for (; chain <= i; chain++) {
                    float chain_top = paged_flow_top(items[chain].view);
                    if (chain_top > fragmentainer->top + reserve_top + kPagedFlowEpsilon && bottom - chain_top <= capacity + kPagedFlowEpsilon) break;
                    if (chain < i) state->relaxed_avoidance++;
                }
                from = chain;
            }
            if (from <= i) {
                target = next;
                if (from < i) { i = from; top = paged_flow_top(items[i].view); }
            } else {
                // nothing can move: rows continue inside their cells, captions inside their content
                for (size_t r = i; status == TYPESET_OK && r <= last; r++) {
                    ViewBlock* view = items[r].view;
                    if (items[r].kind == PAGED_ITEM_ROW) {
                        // a row of a spanning group that a fresh fragmentainer holds moves alone
                        float row_top = paged_flow_top(view);
                        size_t row_index, row_next;
                        status = paged_fragmentainer_at(state, row_top, &row_index);
                        if (status != TYPESET_OK) break;
                        const PagedFragmentainer& row_fragmentainer = state->fragmentainers[row_index];
                        float row_reserve_top, row_reserve_bottom;
                        paged_reserve(state, view, row_index, &row_reserve_top, &row_reserve_bottom);
                        if (row_top + view->height + row_reserve_bottom <= row_fragmentainer.bottom + kPagedFlowEpsilon) continue;
                        status = paged_fragmentainer_at(state, row_fragmentainer.bottom + kPagedFlowEpsilon, &row_next);
                        if (status != TYPESET_OK) break;
                        const PagedFragmentainer& fresh = state->fragmentainers[row_next];
                        float fresh_top, fresh_bottom;
                        paged_reserve(state, view, row_next, &fresh_top, &fresh_bottom);
                        if (r > i && row_top > row_fragmentainer.top + row_reserve_top + kPagedFlowEpsilon &&
                            view->height <= fresh.bottom - fresh.top - fresh_top - fresh_bottom + kPagedFlowEpsilon) {
                            float row_delta = fresh.top + fresh_top - row_top;
                            status = paged_strut_record(state, view, nullptr, -1, row_delta, false, row_next, row_top);
                            if (status == TYPESET_OK) status = paged_table_insert(state, record, owner, root, row_top, row_delta, false);
                            continue;
                        }
                        status = paged_table_split_row(state, lycon, record, owner, root, static_cast<ViewTableRow*>(view));
                    } else if (items[r].kind == PAGED_ITEM_CAPTION) {
                        float height = view->height, content = view->content_height;
                        status = paged_fit_finished(state, lycon, view, view);
                        float growth = view->height - height;
                        view->height = height; view->content_height = content;
                        if (status == TYPESET_OK && growth > 0.0f)
                            status = paged_table_insert(state, record, owner, root, paged_flow_top(view) + height, growth, true);
                    }
                }
                i = last + 1;
                continue;
            }
        }
        if (status != TYPESET_OK) break;
        float target_top, target_bottom;
        paged_reserve(state, items[i].view, target, &target_top, &target_bottom);
        delta = state->fragmentainers[target].top + target_top - top;
        status = paged_strut_record(state, items[i].view, nullptr, -1, delta, forced, target, top);
        if (status == TYPESET_OK) status = paged_table_insert(state, record, owner, root, top, delta, false);
        forced_inside |= forced;
        // the moved extent is checked again at its new place
    }
    // the breaks stay with the record; the journal keeps one entry for the table
    size_t breaks = state->strut_count - journal;
    if (status == TYPESET_OK && breaks && !lam::pool_grow_array(state->composition->pool, &record->struts,
        &record->strut_capacity, breaks, 16)) status = TYPESET_OUT_OF_MEMORY;
    if (status == TYPESET_OK) {
        if (breaks) memcpy(record->struts, state->struts + journal, breaks * sizeof(PagedStrut));
        record->strut_count = breaks;
        state->strut_count = journal;
        status = paged_table_align_slices(state, lycon, record);
    }
    state->table = record->outer;
    if (status != TYPESET_OK) return paged_flow_fail(state, status, block, "the table rows could not be broken across fragmentainers");
    size_t entry = state->strut_count;
    for (size_t i = 0; i < state->strut_count; i++) if (state->struts[i].table == record) { entry = i; break; }
    if (entry == state->strut_count) {
        if (!record->modified) return TYPESET_OK;
        status = paged_strut_record(state, block, nullptr, -2, 0.0f, false, first, table_top);
        if (status != TYPESET_OK) return status;
    }
    PagedStrut& summary = state->struts[entry];
    summary.table = record; summary.amount = record->growth; summary.forced = forced_inside;
    log_debug("[PAGED_FLOW] table %s broken %zu times, grew %.3f", block->source_loc(), breaks, record->growth);
    return TYPESET_OK;
}

// the row pass when a table's layout ended: the table joins the flow's units so
// later moves break it again, and keep chains apply when it moved whole
static void paged_table_after(PagedFlowState* state, LayoutContext* lycon, ViewBlock* block) {
    PagedFlowTable* record = (PagedFlowTable*)pool_calloc(state->composition->pool, sizeof(PagedFlowTable));
    PagedFlowParagraph* unit = (PagedFlowParagraph*)pool_calloc(state->composition->pool, sizeof(PagedFlowParagraph));
    if (!record || !unit) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
    record->table = block;
    if (state->last_table) state->last_table->next = record;
    else state->tables = record;
    state->last_table = record;
    unit->block = block; unit->table = record;
    if (state->last_paragraph) state->last_paragraph->next = unit;
    else state->paragraphs = unit;
    state->last_paragraph = unit;
    size_t since = state->strut_count;
    if (paged_table_fragment(state, lycon, record, &lycon->block, block) != TYPESET_OK) return;
    if (since < state->strut_count && state->struts[since].view == (View*)block && state->struts[since].line == -1)
        paged_keep_chain(state, lycon, &lycon->block, block, since, false);
}

// ----- marks, named strings and targets through the flow (plan P4) -----
// The layout records a mark source where it meets it, because only there do the
// counters stand as they do at the element: an element's named strings
// (string-set), a running element the flow skips, a target's counter values.
// Publication walks the views in flow order and binds each recorded source to
// the page of the first content after it, as the item engine bound a mark to
// the page its first item reached (CSS GCPM 3 §1.1, §2.1).

static PagedMarkSource* paged_flow_mark(PagedFlowState* state, DomNode* source, bool create) {
    HashMap* sources = state->composition->mark_sources;
    PagedMarkSource key = {}; key.source = source;
    PagedMarkSource* mark = (PagedMarkSource*)hashmap_get(sources, &key);
    if (mark || !create) return mark;
    key.assigned_at = SIZE_MAX;
    hashmap_set(sources, &key);
    if (hashmap_oom(sources)) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, source, nullptr); return nullptr; }
    return (PagedMarkSource*)hashmap_get(sources, &key);
}

// a content function that failed stops the pass with its precise diagnostic
static bool paged_flow_content_failed(PagedFlowState* state, DomElement* element, const char* reason) {
    TypesetStatus status = state->composition->diagnostic.status;
    paged_flow_fail(state, status != TYPESET_OK ? status : TYPESET_INVALID, element, reason);
    return false;
}

// the text a pseudo-element of `element` generates at this point of the flow;
// its own counter operations apply inside a checkpoint that rolls them back
static bool paged_flow_pseudo_content(LayoutContext* lycon, DomElement* element, bool before, StrBuf* text) {
    StyleTree* style = element->pseudo_style(before ? PSEUDO_STYLE_BEFORE : PSEUDO_STYLE_AFTER);
    const CssDeclaration* declaration = style ? style_tree_get_declaration(style, CSS_PROPERTY_CONTENT) : nullptr;
    if (!declaration || !declaration->value) return true;
    ScratchScope scope(&lycon->scratch);
    CounterCheckpoint checkpoint;
    if (!checkpoint.enter(lycon->counter_context, &lycon->scratch, &scope.mark)) return false;
    if (lycon->counter_context) {
        if (!counter_push_scope(lycon->counter_context, true)) return false;
        apply_pseudo_counter_ops(lycon, style);
    }
    int quotes = 0;
    // the pseudo's own content() is not a string-set context
    uint32_t depth = lycon->paged->string_depth;
    lycon->paged->string_depth = 0;
    bool ok = dom_element_append_content(element, declaration->value, lycon->counter_context, &quotes, text,
        paged_content_function, lycon);
    lycon->paged->string_depth = depth;
    return ok;
}

bool paged_content_function(void* context, DomElement* element, const CssFunction* function, StrBuf* text,
        bool* handled) {
    LayoutContext* lycon = (LayoutContext*)context;
    PagedFlowState* state = lycon->paged;
    *handled = true;
    if (css_function_name_is(function, "target-counter") || css_function_name_is(function, "target-counters") ||
        css_function_name_is(function, "target-text")) {
        // references read the bindings of the previous reference pass
        if (paged_content_target_function(state->tree, state->composition, element, function, text)) return true;
        return paged_flow_content_failed(state, element, "unresolved body generated-content binding");
    }
    if (css_function_name_is(function, "content") && state->string_depth) {
        // content() belongs to string-set (CSS GCPM 3 §1.1)
        const char* part = function->arg_count ? css_value_identifier_name(function->args[0]) : "text";
        bool before = part && str_ieq_cstr(part, "before");
        if (function->arg_count <= 1 && part && str_ieq_cstr(part, "text")) { paged_source_text(element, text); return true; }
        if (function->arg_count <= 1 && (before || (part && str_ieq_cstr(part, "after"))))
            return paged_flow_pseudo_content(lycon, element, before, text);
        return paged_flow_content_failed(state, element, "unresolved named-string assignment");
    }
    if (css_function_name_is(function, "counter") || css_function_name_is(function, "counters")) {
        const char* name = function->arg_count ? css_value_identifier_name(function->args[0]) : nullptr;
        if (!name || (strcmp(name, "page") && strcmp(name, "pages"))) { *handled = false; return true; }
        // the flow is laid out before its pages are numbered; page counters belong to page contexts
    } else if (!css_function_name_is(function, "string")) {
        // leader() in body content waits for the inline fill (plan §9 P4 open items)
        *handled = false; return true;
    }
    return paged_flow_content_failed(state, element, state->string_depth ? "unresolved named-string assignment" :
        "unresolved body generated-content binding");
}

// an element's named-string assignments, evaluated where the flow meets it; a
// display:none subtree records its assignments on its root
static void paged_flow_strings(PagedFlowState* state, LayoutContext* lycon, DomElement* element, PagedMarkSource** owner) {
    const CssValue* value = paged_flow_declared(state, element, "string-set");
    if (!value || css_value_is_none(value) || css_value_is_initial(value) || css_value_is_inherit(value) ||
        css_value_is_unset(value)) return;
    bool comma = value->type == CSS_VALUE_TYPE_LIST && value->data.list.comma_separated;
    int count = comma ? value->data.list.count : 1;
    Pool* pool = state->composition->pool;
    for (int i = 0; i < count && state->status == TYPESET_OK; i++) {
        const char* name = nullptr;
        CssValue content = {};
        if (!paged_string_set_group(comma ? value->data.list.values[i] : value, &name, &content)) {
            paged_flow_fail(state, TYPESET_INVALID, element, "unresolved named-string assignment");
            return;
        }
        StrBuf* text = strbuf_new();
        if (!text) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, element, nullptr); return; }
        int quotes = 0;
        state->string_depth++;
        bool ok = dom_element_append_content(element, &content, lycon->counter_context, &quotes, text,
            paged_content_function, lycon);
        state->string_depth--;
        if (!ok) { strbuf_free(text); paged_flow_content_failed(state, element, "unresolved named-string assignment"); return; }
        if (!*owner) *owner = paged_flow_mark(state, element, true);
        PagedMarkSource* mark = *owner;
        if (!mark || !lam::pool_grow_array(pool, &mark->strings, &mark->string_capacity, mark->string_count + 1, 4)) {
            strbuf_free(text); paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, element, nullptr); return;
        }
        PagedFlowString& entry = mark->strings[mark->string_count];
        entry.name = pool_dup_n(pool, name, strlen(name));
        entry.text = pool_dup_n(pool, text->str ? text->str : "", text->length);
        strbuf_free(text);
        if (!entry.name || !entry.text) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, element, nullptr); return; }
        mark->string_count++;
    }
}

// does `element` name counter `name` in its own counter-reset or counter-increment?
static bool paged_counter_named(DomElement* element, CssPropertyCode property, const char* name) {
    StyleTree* style = element->specified_style.get();
    const CssDeclaration* declaration = style ? style_tree_get_declaration(style, property) : nullptr;
    const CssValue* value = declaration ? declaration->value : nullptr;
    size_t count = !value ? 0 : value->type == CSS_VALUE_TYPE_LIST ? (size_t)value->data.list.count : 1;
    for (size_t i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.values[i] : value;
        const char* ident = item ? css_value_identifier_name(item) : nullptr;
        if (ident && strcmp(ident, name) == 0) return true;
    }
    return false;
}

void paged_element_enter(LayoutContext* lycon, DomElement* element) {
    PagedFlowState* state = lycon->paged;
    if (state->status != TYPESET_OK) return;
    // the footnote counter starts at the root unless the root sets it (CSS GCPM 3 §2.6)
    if (element == state->tree->model->document->root && lycon->counter_context &&
        !paged_counter_named(element, CSS_PROPERTY_COUNTER_RESET, "footnote"))
        counter_reset(lycon->counter_context, "footnote 0");
    // a measurement layout met the element before; its last layout holds the values
    PagedMarkSource* mark = paged_flow_mark(state, element, false);
    if (mark) mark->string_count = 0;
    paged_flow_strings(state, lycon, element, &mark);
    const char* id = element->id;
    PagedReferenceSession* references = state->composition->references;
    const TypesetTarget* target = id && *id && references ? typeset_target_find(&references->targets, id) : nullptr;
    const PagedTargetValue* value = target ? (const PagedTargetValue*)target->binding.get() : nullptr;
    // duplicate ids bind the first element in document order
    if (state->status != TYPESET_OK || !value || value->source.address != element) return;
    if (!mark) mark = paged_flow_mark(state, element, true);
    if (!mark) return;
    mark->target = true;
    mark->counters = counter_snapshot_create(lycon->counter_context, state->composition->pool);
    if (!mark->counters) paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, element, nullptr);
}

// a hidden assignment sees the inherited counters and applies none of its subtree's counter operations
static void paged_flow_hidden_strings(PagedFlowState* state, LayoutContext* lycon, DomElement* element,
        PagedMarkSource** owner, size_t depth) {
    if (depth > state->composition->options.max_depth) {
        paged_flow_fail(state, TYPESET_BUDGET_EXHAUSTED, element, "hidden named strings exceed the depth budget");
        return;
    }
    paged_flow_strings(state, lycon, element, owner);
    for (DomNode* child = element->first_child; child && state->status == TYPESET_OK; child = child->next_sibling)
        if (child->is_element()) paged_flow_hidden_strings(state, lycon, child->as_element(), owner, depth + 1);
}

void paged_element_hidden(LayoutContext* lycon, DomElement* element) {
    PagedFlowState* state = lycon->paged;
    if (state->status != TYPESET_OK) return;
    PagedMarkSource* mark = paged_flow_mark(state, element, false);
    if (mark) mark->string_count = 0;
    paged_flow_hidden_strings(state, lycon, element, &mark, 0);
}

// a running element leaves the flow; margin boxes take it through element() (CSS GCPM 3 §2.1)
static const char* paged_flow_running_name(DomElement* element) {
    StyleTree* style = element->specified_style.get();
    const CssDeclaration* declaration = style ? style_tree_get_declaration(style, CSS_PROPERTY_POSITION) : nullptr;
    return declaration ? paged_running_value_name(declaration->value) : nullptr;
}

static void paged_flow_running(PagedFlowState* state, DomElement* element, const char* name) {
    if (state->status != TYPESET_OK) return;
    PagedMarkSource* mark = paged_flow_mark(state, element, true);
    if (!mark || mark->running) return;
    PagedRunningRecord* record = (PagedRunningRecord*)pool_calloc(state->composition->pool, sizeof(PagedRunningRecord));
    if (!record) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, element, nullptr); return; }
    record->provider = state->tree->model->tree_id; record->element = element;
    mark->running = record; mark->running_name = name;
}

// ----- folios and static content through the flow (plan P6) -----

// the text a folio shows: on a page laid out with its static content, that page's
// label; in the body, the label of the page it reached in the previous pass, which
// convergence revisits (an unresolved one measures as "0"); a folio reference shows
// its target's first or last page label (page_document.hpp, RadiantPageQuery)
static const char* paged_folio_text(PagedFlowState* state, DomElement* folio, const ViewPageBox* page) {
    PagedComposition* composition = state->composition;
    PagedReferenceSession* references = composition->references;
    if (radiant_page_element(folio, "folio")) {
        if (page) return page->label ? page->label.get() : "";
        composition->reference_used = true; composition->reference_source = dom_node_ref(folio);
        for (size_t i = 0; references && i < references->folio_count; i++) {
            uint32_t number = references->folios[i].page;
            if (references->folios[i].folio == folio && number && number <= references->page_label_count)
                return references->page_labels[number - 1];
        }
        return "0";
    }
    composition->reference_used = true; composition->reference_source = dom_node_ref(folio);
    const char* id = folio->get_attribute("ref-id");
    const TypesetTarget* target = id && references ? typeset_target_find(&references->targets, id) : nullptr;
    const PagedTargetValue* value = target ? (const PagedTargetValue*)target->binding.get() : nullptr;
    const char* edge = folio->get_attribute("edge");
    const char* area = folio->get_attribute("area");
    size_t areas = area && !strcmp(area, "normal") ? RADIANT_QUERY_NORMAL : area && !strcmp(area, "non-blank") ? RADIANT_QUERY_NON_BLANK : RADIANT_QUERY_ALL;
    const char* text = value ? value->labels[areas][edge && !strcmp(edge, "last")] : nullptr;
    return text ? text : "0";
}

// fill the folios of `element`'s subtree for `page` (null in the body flow)
static bool paged_folios_fill(PagedFlowState* state, DomElement* element, const ViewPageBox* page, size_t depth = 0) {
    if (depth > state->composition->options.max_depth) return false;
    if (radiant_page_element(element, "folio") || radiant_page_element(element, "folio-ref")) {
        layout_generated_text(element, paged_folio_text(state, element, page));
        return true;
    }
    bool found = false;
    for (DomElement* child = element->first_child_element(); child; child = child->next_sibling_element())
        found |= paged_folios_fill(state, child, page, depth + 1);
    return found;
}

// a body folio's text reached fragmentainer `index`: its page settles the next pass
static TypesetStatus paged_folio_capture(PagedFlowState* state, ViewText* text, size_t index) {
    DomNode* box = text->parent;
    if (!box || !box->is_element() || !box->as_element()->is_synthetic() || !box->parent ||
        !box->parent->is_element() || !radiant_page_element(box->parent->as_element(), "folio")) return TYPESET_OK;
    PagedReferenceSession* references = state->composition->references;
    const ViewPageBox* page = state->fragmentainers[index].page;
    if (!references || !page || state->publish_role != VIEW_FRAGMENT_BODY) return TYPESET_OK;
    const DomElement* folio = box->parent->as_element();
    size_t at = 0;
    while (at < references->folio_count && references->folios[at].folio != folio) at++;
    if (at == references->folio_count && !lam::pool_grow_array(references->pool, &references->folios,
        &references->folio_capacity, references->folio_count + 1, 8)) return TYPESET_OUT_OF_MEMORY;
    if (at == references->folio_count) references->folio_count++;
    references->folios[at] = {folio, page->page_number};
    return TYPESET_OK;
}

// ----- footnotes and page floats through the flow (plan P5) -----

// lay `element` out with the browsing engine as a block formatting context root of
// `width` (and of a definite `height`, the basis of percentage heights, unless NAN),
// outside the flow's hooks, and return its margin-box height. The element left
// the flow, so its views hold this layout until the next one (§3.4); a note met
// in the middle of a line lays out with the line's context set aside
static float paged_detached_layout(PagedFlowState* state, LayoutContext* lycon, DomElement* element, float width,
        float height = NAN) {
    LayoutContextScope context_scope(lycon); LayoutViewScope view_scope(lycon);
    AvailableSpace saved_space = lycon->available_space;
    radiant::RunMode saved_mode = lycon->run_mode;
    auto saved_flex = lycon->flex_container; auto saved_grid = lycon->grid_container;
    lycon->flex_container = nullptr; lycon->grid_container = nullptr;
    lycon->run_mode = radiant::RunMode::PerformLayout;
    lycon->available_space = AvailableSpace::make_width_definite(width);
    block_context_init(&lycon->block, paged_flow_parent_block(element), lycon->pool);
    lycon->block.origin_x = lycon->block.origin_y = 0.0f;
    lycon->block.content_width = lycon->block.given_width = lycon->block.max_width = width;
    lycon->block.float_left_edge = 0.0f; lycon->block.float_right_edge = width;
    if (!isnan(height)) lycon->block.given_height = height;
    line_init(lycon, 0.0f, width);
    DisplayValue display = resolve_display_value(element);
    if (display.outer != CSS_VALUE_BLOCK && display.outer != CSS_VALUE_LIST_ITEM) display.outer = CSS_VALUE_BLOCK;
    state->outside++;
    layout_block(lycon, element, display);
    state->outside--;
    lycon->available_space = saved_space; lycon->run_mode = saved_mode;
    lycon->flex_container = saved_flex; lycon->grid_container = saved_grid;
    ViewBlock* view = lam::view_as_block((View*)element);
    if (!view) return 0.0f;
    float margin_bottom = view->bound ? view->boundary()->margin.bottom : 0.0f;
    return view->y + view->height + margin_bottom;
}

// the footnote call's and marker's user-agent defaults, which author rules
// override (CSS GCPM 3 Appendix B). The call keeps the item engine's superscript,
// which never changed its line's height: a zero line-height keeps the raised box
// inside the line, as `sup { line-height: 0 }` does in browsers
static void paged_note_defaults(DomElement* note) {
    static const struct { PseudoStyleKind kind; uint8_t pseudo; const char* text; CssPropertyCode codes[4]; } defaults[] = {
        {PSEUDO_STYLE_FOOTNOTE_CALL, PSEUDO_ELEMENT_FOOTNOTE_CALL,
            "content: counter(footnote); font-size: 65%; vertical-align: super; line-height: 0",
            {CSS_PROPERTY_CONTENT, CSS_PROPERTY_FONT_SIZE, CSS_PROPERTY_VERTICAL_ALIGN, CSS_PROPERTY_LINE_HEIGHT}},
        {PSEUDO_STYLE_FOOTNOTE_MARKER, PSEUDO_ELEMENT_FOOTNOTE_MARKER, "content: counter(footnote) ' '",
            {CSS_PROPERTY_CONTENT, CSS_PROPERTY_CONTENT, CSS_PROPERTY_CONTENT, CSS_PROPERTY_CONTENT}},
    };
    Pool* pool = note->storage_pool();
    for (const auto& entry : defaults) {
        bool missing = false;
        for (CssPropertyCode code : entry.codes) missing |= !dom_element_get_pseudo_element_value(note, code, entry.pseudo);
        if (!missing) continue;
        size_t count = 0;
        CssDeclaration** declarations = css_parse_declaration_list_text(entry.text, strlen(entry.text), pool, &count);
        lam::Own<StyleTree>* slot = note->pseudo_style_slot(entry.kind);
        if (!declarations || !slot) continue;
        if (!*slot) *slot = lam::own(style_tree_create(pool));
        for (size_t i = 0; *slot && i < count; i++) {
            CssDeclaration* declaration = declarations[i];
            if (!declaration || dom_element_get_pseudo_element_value(note, declaration->property_code, entry.pseudo)) continue;
            declaration->origin = CSS_ORIGIN_USER_AGENT; declaration->specificity = {};
            style_tree_apply_declaration(*slot, declaration);
        }
    }
}

// a footnote or page float of a paged edition: `float: footnote`, or `float: top`
// or `bottom` against its page or column (CSS GCPM 3 §2.2, CSS Page Floats 3 §3);
// an inline float reference keeps a top or bottom float in the flow
static bool paged_flow_anchor_kind(PagedFlowState* state, DomElement* element, PagedAnchorKind* kind) {
    if (state->outside || state->tree->model->environment.presentation != VIEW_PRESENTATION_PAGED) return false;
    // a native note's body is its footnote; its authored call stays in the line (page_document.hpp)
    if (radiant_page_element(element, "note-body") && radiant_page_element(element->parent_element(), "note")) {
        *kind = PAGED_ANCHOR_NOTE;
        return true;
    }
    StyleTree* style = element->specified_style.get();
    const CssDeclaration* declaration = style ? style_tree_get_declaration(style, CSS_PROPERTY_FLOAT) : nullptr;
    const CssValue* value = declaration ? declaration->value : nullptr;
    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD) return false;
    CssEnum keyword = value->data.keyword;
    if (keyword != CSS_VALUE_FOOTNOTE && keyword != CSS_VALUE_TOP && keyword != CSS_VALUE_BOTTOM) return false;
    if (layout_display_is_none(resolve_display_value(element))) return false;
    if (keyword == CSS_VALUE_FOOTNOTE) { *kind = PAGED_ANCHOR_NOTE; return true; }
    const CssDeclaration* reference = style_tree_get_declaration(style, CSS_PROPERTY_FLOAT_REFERENCE);
    const char* name = reference && reference->value ? css_value_identifier_name(reference->value) : nullptr;
    if (!name || (!str_ieq_cstr(name, "page") && !str_ieq_cstr(name, "column"))) return false;
    *kind = keyword == CSS_VALUE_TOP ? PAGED_ANCHOR_TOP : PAGED_ANCHOR_BOTTOM;
    return true;
}

// a footnote call or marker generates a box unless its content is none; without
// content it shows the footnote counter (layout_update_pseudo_content_with_counters)
static bool paged_note_generates(DomElement* note, uint8_t pseudo) {
    const CssDeclaration* declaration = dom_element_get_pseudo_element_value(note, CSS_PROPERTY_CONTENT, pseudo);
    return !declaration || !declaration->value || !css_value_is_none(declaration->value);
}

static PagedFlowAnchor* paged_flow_anchor_find(PagedFlowState* state, DomElement* element) {
    for (size_t i = state->anchor_count; i > 0; i--)
        if (state->anchors[i - 1].element == element) return &state->anchors[i - 1];
    return nullptr;
}

// a footnote or page float met in the flow is laid out outside it, at the width
// it takes on the current fragmentainer, and waits for the line or box that
// anchors it; a footnote counts itself on the footnote counter and leaves its
// call in its place (CSS GCPM 3 §2.3, §2.6). A measuring layout lays out only
// the call, and a note met again in the same pass keeps its record.
static void paged_flow_anchor(PagedFlowState* state, LayoutContext* lycon, DomElement* element, PagedAnchorKind kind) {
    if (state->status != TYPESET_OK) return;
    // a native note body brings its own call and marker and counts on no counter
    bool authored = radiant_page_element(element, "note-body");
    bool note = kind == PAGED_ANCHOR_NOTE && !authored;
    if (note) {
        paged_note_defaults(element);
        if (lycon->counter_context && !paged_counter_named(element, CSS_PROPERTY_COUNTER_INCREMENT, "footnote"))
            counter_increment(lycon->counter_context, "footnote 1");
    }
    bool committed = lycon->run_mode == radiant::RunMode::PerformLayout && !lycon->measurement_root;
    if (committed && !paged_flow_anchor_find(state, element)) {
        if (!state->fragmentainer_count && paged_fragmentainer_append(state, true) != TYPESET_OK) return;
        size_t index = state->cursor < state->fragmentainer_count ? state->cursor : state->fragmentainer_count - 1;
        PagedFlowAnchor anchor = {};
        anchor.element = element; anchor.kind = kind; anchor.fragmentainer = kPagedUnplaced;
        anchor.width = state->fragmentainers[index].rect.width;
        if (kind == PAGED_ANCHOR_NOTE) {
            const PagedNoteArea* area = paged_fragmentainer_area(state, index);
            if (!area) return;
            anchor.width = area->width - area->edges[1] - area->edges[3];
            const CssValue* policy = paged_flow_declared(state, element, "footnote-policy");
            const char* name = policy ? css_value_identifier_name(policy) : nullptr;
            anchor.policy = name && str_ieq_cstr(name, "line") ? PAGED_NOTE_LINE :
                name && str_ieq_cstr(name, "block") ? PAGED_NOTE_BLOCK : PAGED_NOTE_AUTO;
            // the marker opens the note's content (CSS GCPM 3 §2.3)
            if (note && paged_note_generates(element, PSEUDO_ELEMENT_FOOTNOTE_MARKER)) layout_footnote_pseudo(lycon, element, false);
        } else {
            StyleTree* style = element->specified_style.get();
            const CssDeclaration* clear = style ? style_tree_get_declaration(style, CSS_PROPERTY_CLEAR) : nullptr;
            CssEnum side = clear && clear->value && clear->value->type == CSS_VALUE_TYPE_KEYWORD ? clear->value->data.keyword : CSS_VALUE_NONE;
            anchor.clear = side == CSS_VALUE_TOP ? 1 : side == CSS_VALUE_BOTTOM ? 2 : side == CSS_VALUE_BOTH ? 3 : 0;
            // deferral to later pages is a later extension (plan §7): the float stays with its anchor
            const CssDeclaration* defer = style ? style_tree_get_declaration(style, CSS_PROPERTY_FLOAT_DEFER) : nullptr;
            if (defer && defer->value && !css_value_is_none(defer->value))
                log_debug("[PAGED_FLOW] float-defer of %s is not applied; the float stays with its anchor", element->source_loc());
        }
        // a page float's containing block is its page area or column (CSS Page Floats 3 §4)
        anchor.height = paged_detached_layout(state, lycon, element, anchor.width,
            kind == PAGED_ANCHOR_NOTE ? NAN : state->fragmentainers[index].rect.height);
        anchor.full_height = anchor.height;
        anchor.view_type = element->view_type;
        if (!lam::pool_grow_array(state->composition->pool, &state->anchors, &state->anchor_capacity, state->anchor_count + 1, 16)) {
            paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, element, nullptr); return;
        }
        state->anchors[state->anchor_count++] = anchor;
    }
    if (!note || !paged_note_generates(element, PSEUDO_ELEMENT_FOOTNOTE_CALL)) return;
    DomElement* call = layout_footnote_pseudo(lycon, element, true);
    if (!call) return;
    state->call = call;
    layout_flow_node(lycon, call);
    state->call = nullptr;
}

// publication reached a recorded source: its assignments wait for the next
// content, and a target collects the pages its content reaches; repeated
// table occurrences record nothing
static bool paged_publish_mark(PagedFlowState* state, DomNode* node, bool hidden) {
    if (state->publishing_fixed || state->publish_role != VIEW_FRAGMENT_BODY || !hashmap_count(state->composition->mark_sources)) return false;
    PagedMarkSource* mark = paged_flow_mark(state, node, false);
    if (!mark) return false;
    Pool* pool = state->composition->pool;
    if ((mark->string_count || mark->running) &&
        lam::pool_grow_array(pool, &state->pending, &state->pending_capacity, state->pending_count + 1, 16))
        state->pending[state->pending_count++] = mark;
    if (!mark->target || hidden ||
        !lam::pool_grow_array(pool, &state->open_targets, &state->open_target_capacity, state->open_target_count + 1, 16))
        return false;
    mark->target_page = mark->last_target_page = 0;
    state->open_targets[state->open_target_count++] = mark;
    return true;
}

static TypesetStatus paged_flow_mark_append(PagedFlowState* state, PagedMarkSource* mark, TypesetMark entry, uint32_t page) {
    ViewTree* tree = state->tree;
    entry.source = {tree->model->tree_id, tree->layout_generation, dom_node_ref(mark->source).expected_id,
        TYPESET_PROVIDER_OFFSETS, nullptr};
    entry.page_number = page;
    entry.at_page_start = state->content_page != page;
    TypesetMarkStore* marks = &state->composition->marks;
    if (!marks->count || marks->entries[marks->count - 1].page_number <= page) return typeset_mark_append(marks, &entry);
    // content placed out of page order (a float) keeps the store ordered by page
    if (!lam::pool_grow_array(marks->pool, &marks->entries, &marks->capacity, marks->count + 1, 16)) return TYPESET_OUT_OF_MEMORY;
    size_t at = marks->count;
    while (at && marks->entries[at - 1].page_number > page) at--;
    memmove(&marks->entries[at + 1], &marks->entries[at], (marks->count - at) * sizeof(TypesetMark));
    marks->entries[at] = entry; marks->count++;
    return TYPESET_OK;
}

// the waiting sources bind to `page`
static TypesetStatus paged_publish_pending(PagedFlowState* state, uint32_t page) {
    TypesetStatus status = TYPESET_OK;
    for (size_t i = 0; status == TYPESET_OK && i < state->pending_count; i++) {
        PagedMarkSource* mark = state->pending[i];
        for (size_t j = 0; status == TYPESET_OK && j < mark->string_count; j++) {
            TypesetMark entry = {};
            entry.kind = TYPESET_MARK_STRING; entry.name = mark->strings[j].name; entry.text = mark->strings[j].text;
            status = paged_flow_mark_append(state, mark, entry, page);
        }
        if (status == TYPESET_OK && mark->running) {
            TypesetMark entry = {};
            entry.kind = TYPESET_MARK_RUNNING; entry.name = mark->running_name;
            entry.value = lam::up((const TypesetRecord*)mark->running);
            status = paged_flow_mark_append(state, mark, entry, page);
        }
    }
    state->pending_count = 0;
    return status;
}

// body content on fragmentainer `index`: the waiting sources bind to its page
// and the open targets reach it
static TypesetStatus paged_publish_content(PagedFlowState* state, size_t index) {
    const ViewPageBox* page = state->fragmentainers[index].page;
    if (state->publishing_fixed || state->publish_role != VIEW_FRAGMENT_BODY || !page) return TYPESET_OK;
    state->fragmentainers[index].occupied = true;
    uint32_t number = page->page_number;
    TypesetStatus status = state->pending_count ? paged_publish_pending(state, number) : TYPESET_OK;
    for (size_t i = 0; i < state->open_target_count; i++) {
        PagedMarkSource* target = state->open_targets[i];
        if (!target->target_page) target->target_page = number;
        target->last_target_page = number;
    }
    state->content_page = number;
    return status;
}

// a body child (or a root child where the root holds the flow) at a forced
// break may start a run of another body width (plan §4.2)
static bool paged_flow_run_holder(const PagedFlowState* state, const DomNode* node) {
    const DomNode* root = state->tree->model->document->root;
    if (node && (node == root || (node->parent == root && node->is_element() &&
        node->as_element()->tag() == MARKUP_NAME_BODY))) return true;
    // native sequence/flow wrappers hold the same page flow as HTML's body.
    for (const RadiantPageSequence* sequence = state->tree->model->css->page_document->sequences;
            node && sequence; sequence = sequence->next) {
        if (sequence->source.address == node) return true;
        for (const RadiantPageFlowBinding* flow = sequence->flows; flow; flow = flow->next)
            if (flow->source.address == node) return true;
    }
    return false;
}

static bool paged_flow_top_level(const PagedFlowState* state, const ViewBlock* block) {
    return paged_flow_run_holder(state, block->parent);
}

// the new run's width reaches the box and its following siblings through the
// parent's context, which layout_block writes back after the box; publication
// gives the root and body each run's width
static void paged_flow_run_restart(PagedFlowState* state, BlockContext* pa_block, float width) {
    float previous = pa_block->paged_reference_width > 0.0f ? pa_block->paged_reference_width
        : state->fragmentainers[0].rect.width;
    float delta = width - previous;
    pa_block->paged_reference_width = width;
    state->run_width = width;
    if (fabsf(delta) <= kPagedFlowEpsilon) return;
    log_debug("[PAGED_FLOW] run restart from width %.3f to %.3f", previous, width);
    pa_block->content_width += delta; pa_block->max_width += delta; pa_block->float_right_edge += delta;
    if (pa_block->given_width >= 0.0f) pa_block->given_width += delta;
}

static PagedFlowWidth* paged_flow_width(PagedFlowState* state, ViewBlock* block, bool create) {
    PagedReferenceSession* session = state->composition->references;
    if (!session || !paged_flow_top_level(state, block)) return nullptr;
    for (size_t i = 0; i < session->width_count; i++)
        if (session->widths[i].source == block) return &session->widths[i];
    if (!create) return nullptr;
    if (session->width_count >= state->composition->options.max_nodes ||
        !lam::pool_grow_array(session->pool, &session->widths, &session->width_capacity,
            session->width_count + 1, 16)) {
        paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return nullptr;
    }
    PagedFlowWidth* width = &session->widths[session->width_count++];
    *width = {block, state->run_width};
    return width;
}

void paged_line_start(LayoutContext* lycon, ViewBlock* block) {
    PagedFlowState* state = lycon->paged;
    if (state->outside || state->status != TYPESET_OK || !block ||
        !paged_flow_fragmentable(lycon, block)) return;
    PagedFlowWidth* width = paged_flow_width(state, block, false);
    size_t line = lycon->block.line_number;
    if (!width || line >= width->count || !width->lines[line] ||
        (block->blk && block->block()->given_width >= 0.0f)) return;
    // the DOM box publishes content_width only when it closes; the live context owns it during wrapping.
    float content = fmaxf(0.0f, lycon->block.content_width + width->lines[line] - width->containing_width);
    lycon->line.right = lycon->line.align_right = lycon->line.left + content;
    lycon->line.effective_right = lycon->line.right;
}

static TypesetStatus paged_flow_widths_capture(PagedFlowState* state) {
    PagedReferenceSession* session = state->composition->references;
    if (!session) return TYPESET_OK;
    bool variable = false;
    for (size_t i = 1; i < state->fragmentainer_count; i++)
        variable |= fabsf(state->fragmentainers[i].rect.width - state->fragmentainers[0].rect.width) > kPagedFlowEpsilon;
    if (!variable && !session->width_count) return TYPESET_OK;
    for (size_t i = 0; i < session->width_count; i++) {
        PagedFlowWidth& width = session->widths[i];
        if (width.seen_pass != session->pass) continue;
        width.containing_width = state->fragmentainers[paged_fragmentainer_index(state,
            paged_flow_top((View*)width.source))].rect.width;
        width.count = 0;
    }
    for (PagedFlowParagraph* paragraph = state->paragraphs; paragraph; paragraph = paragraph->next) {
        PagedFlowWidth* width = paged_flow_width(state, paragraph->block, false);
        if (!width) continue;
        for (size_t i = 0; i < paragraph->count; i++) {
            const PagedFlowLine& line = paragraph->lines[i];
            size_t count = (size_t)line.number + 1;
            if (!lam::pool_grow_array(session->pool, &width->lines, &width->capacity, count, 8))
                return TYPESET_OUT_OF_MEMORY;
            while (width->count < count) width->lines[width->count++] = 0.0f;
            width->lines[line.number] = state->fragmentainers[line.fragmentainer].rect.width;
        }
    }
    if (variable) state->composition->reference_used = true;
    return TYPESET_OK;
}

// an unforced break before `block` truncates the margin that adjoins it (CSS Fragmentation 3 §5.2)
static void paged_break_truncate_margin(ViewBlock* block) {
    if (!block->bound) return;
    BoundaryProp* boundary = block->boundary_mut();
    if (!boundary->has_flow_margin) { boundary->flow_margin = boundary->margin; boundary->has_flow_margin = true; }
    boundary->margin.top = 0.0f;
}

// the break before `block`: a forced break, a named page or sequence, or previous
// content that filled its fragmentainer exactly
static void paged_block_break(PagedFlowState* state, LayoutContext* lycon, ViewBlock* block, BlockContext* pa_block) {
    PagedPageCursor* composer = &state->composer;
    ViewBreak request = paged_flow_break_keyword(block->blk ? block->block()->break_before : CSS_VALUE_AUTO);
    if (state->pending_break > request) request = state->pending_break;
    state->pending_break = VIEW_BREAK_AUTO;
    // named pages and page sequences start a page (CSS Paged Media 3 §7.4)
    const RadiantPageSequence* sequence = paged_flow_sequence(state, block);
    bool changed_sequence = sequence && sequence != composer->sequence;
    if (changed_sequence) composer->sequence = sequence;
    const char* name = sequence ? sequence->master_reference : paged_flow_page_name(state, block);
    bool changed_name = !paged_flow_same_name(name, composer->page_name);
    if (changed_name) composer->page_name = name;
    float top = paged_flow_top(block);
    size_t index;
    if (paged_fragmentainer_at(state, top, &index) != TYPESET_OK) return;
    const PagedFragmentainer* fragmentainer = &state->fragmentainers[index];
    bool has_content = top > fragmentainer->top + kPagedFlowEpsilon;
    bool forced = request >= VIEW_BREAK_COLUMN || changed_name || changed_sequence;
    // keep a terminal tail on the reserved last sheet when a wider preceding master would absorb it.
    for (size_t i = 0; state->composition->references && i < state->composition->references->terminal_count; i++) {
        const PagedTerminalPage& terminal = state->composition->references->terminals[i];
        if (terminal.tail == block && fragmentainer->page && fragmentainer->page->page_number < terminal.page) {
            request = VIEW_BREAK_PAGE; forced = true;
        }
    }
    // the previous content filled its fragmentainer exactly: an unforced break lands before the box
    bool at_top = !forced && index > 0 && !has_content;
    bool last_page = fragmentainer->page && index + 1 == state->fragmentainer_count;
    ViewPageSide wanted = VIEW_PAGE_RIGHT;
    if (forced && !has_content && last_page && paged_requested_side(composer, request, &wanted) &&
        fragmentainer->page->side != wanted) {
        // an empty page of the wrong side turns blank and the box starts the next page (CSS Break 3 §3.1)
        if (paged_empty_page_restyle(composer, true) != TYPESET_OK) { paged_flow_fail(state, TYPESET_INVALID, block, nullptr); return; }
        paged_fragmentainer_restyle(state, index, true);
        has_content = true;
    }
    if (forced && !has_content) {
        // the page is still empty: a new name or sequence restyles it in place
        if ((changed_name || changed_sequence) && last_page) {
            if (paged_empty_page_restyle(composer, false, true) != TYPESET_OK) { state->status = TYPESET_INVALID; return; }
            paged_fragmentainer_restyle(state, index, false);
            PagedFragmentainer& current = state->fragmentainers[index];
            if (index == 0) state->run_width = current.rect.width;
            else if (paged_flow_top_level(state, block)) paged_flow_run_restart(state, pa_block, current.rect.width);
            else if (fabsf(current.rect.width - state->run_width) > kPagedFlowEpsilon)
                paged_flow_fail(state, TYPESET_UNPLACEABLE, block,
                    "page body width changes inside a flow run are not supported in this phase (Radiant_Impl_Paged_Integration §4.2)");
        }
        return;
    }
    if (!forced && !at_top) return;
    float strut = 0.0f;
    size_t target = index;
    if (forced) {
        if (request < VIEW_BREAK_PAGE && (changed_name || changed_sequence)) request = VIEW_BREAK_PAGE;
        bool new_page = request != VIEW_BREAK_COLUMN || !paged_has_next_column(composer);
        composer->requested_break = request;
        bool top_level = paged_flow_top_level(state, block);
        // the ending sequence pads to its forced page count before the next one starts
        if (changed_sequence && paged_sequence_end(composer, sequence) != TYPESET_OK) {
            paged_flow_fail(state, TYPESET_INVALID, block, nullptr); return;
        }
        if (paged_fragmentainer_append(state, new_page, top_level) != TYPESET_OK) return;
        target = state->fragmentainer_count - 1;
        if (!top_level && fabsf(state->fragmentainers[target].rect.width - state->run_width) > kPagedFlowEpsilon) {
            paged_flow_fail(state, TYPESET_UNPLACEABLE, block,
                "a nested named-page transition requires reflow of its containing block"); return;
        }
        strut = state->fragmentainers[target].top - top;
        if (top_level) paged_flow_run_restart(state, pa_block, state->fragmentainers[target].rect.width);
    } else paged_break_truncate_margin(block);
    pa_block->advance_y += strut;
    block->y = pa_block->advance_y;
    paged_strut_record(state, block, nullptr, -1, strut, forced, target, top);
}

// footnotes and page floats waiting before `block` take its fragmentainer while
// they fit beside its top; returns the first that does not (first + count when
// all do), from which on they move with the box
static size_t paged_box_anchors(PagedFlowState* state, ViewBlock* block, size_t first, size_t count) {
    for (size_t i = first; i < first + count; i++) { state->anchors[i].box = block; state->anchors[i].paragraph = nullptr; }
    state->anchors_bound = first + count;
    float top = paged_flow_top(block);
    size_t index, kept = 0;
    if (paged_fragmentainer_at(state, top, &index) != TYPESET_OK) return first + count;
    float reserve_top, reserve_bottom;
    paged_reserve(state, block, index, &reserve_top, &reserve_bottom);
    bool starts = top <= state->fragmentainers[index].top + reserve_top + kPagedFlowEpsilon;
    return paged_anchors_place(state, first, count, index, top + reserve_bottom, starts, &kept) == PAGED_ANCHORS_MOVE
        ? first + kept : first + count;
}

// the waiting anchors from `from` on did not fit before `block`: the box starts
// the next fragmentainer with them (its own break may have moved it to a top
// already), and again for the ones that still do not fit there
static void paged_box_anchors_break(PagedFlowState* state, ViewBlock* block, BlockContext* pa_block, size_t from, size_t end) {
    bool advance = false;
    while (from < end && state->status == TYPESET_OK) {
        float top = paged_flow_top(block);
        size_t index;
        if (paged_fragmentainer_at(state, top, &index) != TYPESET_OK) return;
        float reserve_top, reserve_bottom;
        paged_reserve(state, block, index, &reserve_top, &reserve_bottom);
        if (advance || top > state->fragmentainers[index].top + reserve_top + kPagedFlowEpsilon) {
            size_t target;
            if (paged_fragmentainer_at(state, state->fragmentainers[index].bottom + kPagedFlowEpsilon, &target) != TYPESET_OK) return;
            float strut = state->fragmentainers[target].top - top;
            paged_break_truncate_margin(block);
            pa_block->advance_y += strut;
            block->y = pa_block->advance_y;
            if (paged_strut_record(state, block, nullptr, -1, strut, false, target, top) != TYPESET_OK) return;
            index = target; top += strut;
            paged_reserve(state, block, index, &reserve_top, &reserve_bottom);
        }
        size_t kept = 0;
        if (paged_anchors_place(state, from, end - from, index, top + reserve_bottom, true, &kept) != PAGED_ANCHORS_MOVE) return;
        from += kept; advance = true;
    }
}

void paged_block_before(LayoutContext* lycon, ViewBlock* block, BlockContext* pa_block) {
    PagedFlowState* state = lycon->paged;
    if (state->outside || state->status != TYPESET_OK || !paged_flow_block(block) || !paged_flow_fragmentable(lycon, block)) return;
    // reject invalid native traits before the CSS workspace approximates them as ordinary layout properties.
    // anonymous table fixups have no DOM identity or authored native traits to admit.
    if (!block->is_synthetic()) {
        const ViewCssStyle* style = view_css_resolve(state->tree, block);
        if (!style) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
        TypesetStatus admitted = paged_style_traits_admit(state->composition, style);
        if (admitted != TYPESET_OK) { state->status = admitted; return; }
    }
    if (!lam::pool_grow_array(state->composition->pool, &state->open_struts, &state->open_capacity,
        (size_t)lycon->depth + 1, 32)) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
    state->open_struts[lycon->depth] = state->strut_count;
    // a block child ends the enclosing block's inline run
    if (state->paragraph && state->paragraph->block != block) {
        TypesetStatus closed = paged_paragraph_close(state, lycon, pa_block, nullptr);
        if (closed != TYPESET_OK) return;
        block->y = pa_block->advance_y;
    }
    // footnotes and page floats anchored before the box belong before any break it brings
    size_t end = state->anchor_count;
    size_t moving = state->anchors_bound < end ? paged_box_anchors(state, block, state->anchors_bound, end - state->anchors_bound) : end;
    if (state->status != TYPESET_OK) return;
    paged_block_break(state, lycon, block, pa_block);
    if (state->status == TYPESET_OK && paged_flow_top_level(state, block)) {
        PagedFlowWidth* width = paged_flow_width(state, block, true);
        if (width) {
            if (!width->seen_pass) width->containing_width = state->fragmentainers[
                paged_fragmentainer_index(state, paged_flow_top(block))].rect.width;
            paged_flow_run_restart(state, pa_block, width->containing_width);
            width->seen_pass = state->composition->references->pass;
        }
    }
    // layout_block saved the parent before this hook; children inherit the updated reference, not that stale copy.
    lycon->block.paged_reference_width = pa_block->paged_reference_width;
    if (moving < end && state->status == TYPESET_OK) paged_box_anchors_break(state, block, pa_block, moving, end);
}

void paged_line_commit(LayoutContext* lycon, float used_line_height) {
    PagedFlowState* state = lycon->paged;
    if (state->outside || state->status != TYPESET_OK || !lycon->line.start_view) return;
    ViewBlock* block = paged_flow_parent_block(lycon->line.start_view);
    if (!block || !paged_flow_block(block) || paged_flow_closed(block) || !paged_flow_fragmentable(lycon, block)) return;
    PagedFlowParagraph* paragraph = paged_paragraph_open(state, block);
    if (!paragraph || !lam::pool_grow_array(state->composition->pool, &paragraph->lines, &paragraph->capacity,
        paragraph->count + 1, 16)) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
    // the footnote calls and page floats met since the last line are anchored on this one
    uint32_t first = (uint32_t)state->anchors_bound, count = (uint32_t)(state->anchor_count - state->anchors_bound); // INT_CAST_OK: anchor counts are bounded by the element budget
    for (size_t i = state->anchors_bound; i < state->anchor_count; i++) {
        state->anchors[i].paragraph = paragraph; state->anchors[i].line = paragraph->count; state->anchors[i].box = nullptr;
    }
    state->anchors_bound = state->anchor_count;
    paragraph->lines[paragraph->count++] = {lycon->block.advance_y, used_line_height, 0, lycon->block.line_number, first, count};
    paged_fit_line(state, lycon, &lycon->block, paragraph, paragraph->count - 1, nullptr, true);
    const PagedFlowLine& fitted = paragraph->lines[paragraph->count - 1];
    state->flow_end = fmaxf(state->flow_end, paged_flow_top(block) + fitted.top + fitted.height);
}

void paged_block_after(LayoutContext* lycon, ViewBlock* block) {
    PagedFlowState* state = lycon->paged;
    if (state->outside || state->status != TYPESET_OK || !state->fragmentainer_count || !paged_flow_block(block) ||
        !paged_flow_fragmentable(lycon, block)) return;
    BlockContext* owner = &lycon->block;
    size_t since = (size_t)lycon->depth < state->open_capacity ? state->open_struts[lycon->depth] : state->strut_count;
    if (state->paragraph && state->paragraph->block == block && paged_paragraph_close(state, lycon, owner, block) != TYPESET_OK) return;
    state->paragraph = nullptr;
    ViewBreak after = paged_flow_break_keyword(block->blk ? block->block()->break_after : CSS_VALUE_AUTO);
    if (paged_flow_is_table(block)) {
        // tables break between rows after layout_table placed them whole
        paged_table_after(state, lycon, block);
        state->pending_break = after >= VIEW_BREAK_COLUMN ? after : VIEW_BREAK_AUTO;
        return;
    }
    float top = paged_flow_top(block), bottom = top + block->height;
    size_t index = paged_fragmentainer_index(state, top);
    float fragmentainer_top = state->fragmentainers[index].top, fragmentainer_bottom = state->fragmentainers[index].bottom;
    // the box ends above the footnotes and page floats placed on its fragmentainer
    float end = fragmentainer_bottom - paged_fragmentainer_reserved(state, index);
    ViewCssStyle* selected = paged_flow_avoids_inside(block) ? view_css_resolve(state->tree, block) : nullptr;
    const RadiantFlowTraits* traits = selected ? selected->flow_traits.get() : nullptr;
    bool page_keep = traits && traits->together.scope[2].kind != RADIANT_KEEP_AUTO &&
        traits->together.scope[1].kind == RADIANT_KEEP_AUTO;
    bool kept_on_page = page_keep && state->fragmentainers[paged_fragmentainer_index(state,
        bottom - kPagedFlowEpsilon)].page == state->fragmentainers[index].page;
    if (paged_flow_monolithic(block) && !kept_on_page && isfinite(fragmentainer_bottom) && bottom > end + kPagedFlowEpsilon) {
        // a box that must not break inside moves whole to a fragmentainer it fits;
        // one that starts its fragmentainer or fits none keeps the breaks its
        // content made (browsers slice it)
        float inner = 0.0f;
        bool forced_inside = false;
        for (size_t i = since; i < state->strut_count; i++) { inner += state->struts[i].amount; forced_inside |= state->struts[i].forced; }
        size_t next;
        if (paged_fragmentainer_at(state, fragmentainer_bottom + kPagedFlowEpsilon, &next) != TYPESET_OK) return;
        if (page_keep) {
            // a page keep permits column breaks, but starts on the next sheet if the remainder of this one is too small.
            while (next < state->fragmentainer_count && state->fragmentainers[next].page == state->fragmentainers[index].page) next++;
            if (next == state->fragmentainer_count && paged_fragmentainer_append(state, true) != TYPESET_OK) return;
        }
        // the next fragmentainer keeps room for its own material and for the anchors
        // inside the box, which may stand there already
        float within = 0.0f, there = 0.0f;
        for (size_t i = 0; i < state->anchors_bound; i++) {
            const PagedFlowAnchor& anchor = state->anchors[i];
            View* at = anchor.box ? anchor.box : anchor.paragraph ? (View*)anchor.paragraph->block : nullptr;
            if (!at || !paged_view_inside(at, block)) continue;
            within += anchor.height;
            if (anchor.fragmentainer == next) there += anchor.height;
        }
        float capacity = state->fragmentainers[next].bottom - state->fragmentainers[next].top -
            (paged_fragmentainer_reserved(state, next) - there) - within;
        bool starts = top <= fragmentainer_top + kPagedFlowEpsilon;
        if (page_keep && state->fragmentainers[next].page && state->fragmentainers[next].page->style) {
            const ViewPageStyle* style = state->fragmentainers[next].page->style;
            capacity = -within;
            for (uint32_t column = 0; column < style->column_count; column++) {
                RdtLogicalRect rect = {};
                if (!view_css_page_column(style, column, &rect)) { state->status = TYPESET_INVALID; return; }
                capacity += rect.height;
                size_t slot = next + column;
                if (slot < state->fragmentainer_count && state->fragmentainers[slot].page == state->fragmentainers[next].page)
                    capacity -= paged_fragmentainer_reserved(state, slot);
            }
            starts &= state->fragmentainers[index].column == 0;
        }
        if (forced_inside) {
            // an authored break inside the box overrides its avoidance (CSS Break §4.3)
        } else if (starts || block->height - inner > capacity + kPagedFlowEpsilon) {
            if (paged_flow_avoids_inside(block)) state->relaxed_avoidance++;
        } else {
            float resolved_height = block->height, resolved_content_height = block->content_height;
            for (size_t i = state->strut_count; i > since; i--) {
                const PagedStrut& strut = state->struts[i - 1];
                if (strut.table) paged_table_restore(state, strut.table, owner, block);
                else paged_shift_flow(state, owner, strut.paragraph, strut.line, strut.paragraph ? nullptr : strut.view, block, -strut.amount);
            }
            // closing a definite-height box already discarded its open line's strut from the used height.
            if (block->blk && block->block()->given_height >= 0.0f) {
                block->height = resolved_height; block->content_height = resolved_content_height;
                owner->advance_y += inner;
            }
            state->strut_count = since;
            float strut = state->fragmentainers[next].top - top;
            if (paged_strut_record(state, block, nullptr, -1, strut, false, next, top) != TYPESET_OK) return;
            paged_shift_flow(state, owner, nullptr, -1, block, block, strut);
            if (paged_keep_chain(state, lycon, owner, block, state->strut_count - 1, false) != TYPESET_OK) return;
            if (paged_refit(state, lycon, owner, block) != TYPESET_OK) return;
        }
    } else if (since + 1 == state->strut_count && state->struts[since].view == (View*)block &&
        state->struts[since].line < 0 && !state->struts[since].forced) {
        // the box alone moved to a fresh fragmentainer: keep chains apply to that boundary
        if (paged_keep_chain(state, lycon, owner, block, since, false) != TYPESET_OK) return;
    }
    // a box whose closing edges reach into the space its fragmentainer keeps for
    // footnotes and page floats continues after that space, as across a page end
    float box_bottom = paged_flow_top(block) + block->height;
    size_t last = paged_fragmentainer_index(state, box_bottom - kPagedFlowEpsilon);
    float reserved = paged_fragmentainer_reserved(state, last), last_bottom = state->fragmentainers[last].bottom;
    if (!paged_flow_monolithic(block) && reserved > 0.0f && isfinite(last_bottom) &&
        box_bottom > last_bottom - reserved + kPagedFlowEpsilon && box_bottom <= last_bottom + kPagedFlowEpsilon) {
        size_t next;
        if (paged_fragmentainer_at(state, last_bottom + kPagedFlowEpsilon, &next) != TYPESET_OK) return;
        paged_flow_grow(owner, block, true, block, reserved);
    }
    float flow_bottom = paged_flow_top(block) + block->height;
    // definite-height boxes retain their tail even when no child line reaches that fragmentainer.
    size_t tail;
    if (block->height > 0.0f && paged_fragmentainer_at(state, flow_bottom - kPagedFlowEpsilon, &tail) != TYPESET_OK) return;
    state->flow_end = fmaxf(state->flow_end, flow_bottom);
    state->pending_break = after >= VIEW_BREAK_COLUMN ? after : VIEW_BREAK_AUTO;
}

void paged_block_moved(LayoutContext* lycon, ViewBlock* block, float delta) {
    PagedFlowState* state = lycon->paged;
    if (state->outside || state->status != TYPESET_OK || !state->fragmentainer_count) return;
    size_t since = (size_t)lycon->depth < state->open_capacity ? state->open_struts[lycon->depth] : state->strut_count;
    for (size_t i = since; i < state->strut_count; i++) {
        PagedStrut& strut = state->struts[i];
        if (!paged_view_inside(strut.view, block)) continue;
        if (strut.table) {
            // a broken table moved with the block: it breaks again at its new place
            paged_table_fragment(state, lycon, strut.table, nullptr, block);
            break;
        }
        // the content after the first strut inside the moved block returns to its page top;
        // the parent's cursor has not passed the block yet, so no cursor moves
        paged_shift_flow(state, nullptr, strut.paragraph, strut.line, strut.paragraph ? nullptr : strut.view, block, -delta);
        strut.amount -= delta;
        break;
    }
}

bool paged_flow_skips_element(LayoutContext* lycon, DomElement* element) {
    if (radiant_page_control_hidden(element) || radiant_page_element(element, "static-content") ||
        radiant_page_element(element, "fixed-pages") || radiant_page_element(element, "fixed-page")) return true;
    PagedFlowState* state = lycon->paged;
    // a folio shows its page label as generated text (page_document.hpp, RadiantPageQuery)
    if (!state->outside && (radiant_page_element(element, "folio") || radiant_page_element(element, "folio-ref"))) {
        paged_folios_fill(state, element, nullptr);
        return false;
    }
    // a footnote call lays out at its note (paged_flow_anchor), not where a retained one stands
    PseudoStyleKind generated;
    if (layout_generated_pseudo(element, nullptr, &generated) && generated == PSEUDO_STYLE_FOOTNOTE_CALL)
        return element != state->call;
    const char* running = paged_flow_running_name(element);
    if (running) { paged_flow_running(state, element, running); return true; }
    PagedAnchorKind kind;
    if (!paged_flow_anchor_kind(state, element, &kind)) return false;
    paged_flow_anchor(state, lycon, element, kind);
    return true;
}

bool paged_flow_pass_begin(LayoutContext* lycon) {
    PagedFlowState* state = lycon->paged;
    if (state->pass && state->composition->diagnostic.container_passes >= state->composition->options.max_container_passes) {
        paged_flow_fail(state, TYPESET_BUDGET_EXHAUSTED, state->tree->model->document->root,
            "container styles exhausted their condition-pass budget");
        return false;
    }
    ++state->composition->diagnostic.container_passes;
    if (state->pass && !view_tree_model_restore(state->tree, state->pass)) { state->status = TYPESET_STALE; return false; }
    state->pass = nullptr;
    state->fragmentainer_count = state->strut_count = state->cursor = 0;
    state->paragraphs = state->last_paragraph = state->paragraph = nullptr;
    state->tables = state->last_table = state->table = nullptr;
    state->pending_break = VIEW_BREAK_AUTO;
    state->anchor_count = state->anchors_bound = state->notes_waiting = 0;
    state->draining = false; state->flow_end = 0.0f;
    // fixed sheets are composed at their own viewports; the document shell owns no flow page.
    state->outside = state->tree->model->css->page_document->fixed_count ? 1 : 0;
    // the hooks record the pass's mark sources again
    hashmap_clear(state->composition->mark_sources, false);
    PagedPageCursor* composer = &state->composer;
    composer->page = nullptr; composer->page_name = nullptr; composer->sequence = nullptr;
    composer->requested_break = VIEW_BREAK_AUTO; composer->column_index = 0;
    state->pass = view_tree_model_checkpoint(state->tree);
    if (!state->pass) { state->status = TYPESET_OUT_OF_MEMORY; return false; }
    return true;
}

bool paged_flow_pass_settle(LayoutContext* lycon) {
    return lycon->paged->status == TYPESET_OK;
}

// ----- publication: slicing the flow into the edition -----

struct PagedParentMap {       // the enclosing box's fragments per fragmentainer
    size_t first, count;
    LayoutViewNode** nodes;
};

static PaintList* paged_publish_paint(PagedFlowState* state) {
    void* storage = pool_alloc(state->composition->pool, sizeof(PagedPaintRecord));
    if (!storage) return nullptr;
    PagedPaintRecord* record = new (storage) PagedPaintRecord{};
    record->next = state->composition->paint_records;
    state->composition->paint_records = record;
    return &record->paint;
}

// D4.5.1v4: shared painters decode directly into the edition's resource owner.
struct PagedPaintResources {
    ViewTree* tree;
    UiContext* ui;
    DomDocument* document;
    lam::Own<hashmap> cache;
    explicit PagedPaintResources(PagedFlowState* state)
        : tree(state->tree), ui(tree->model->environment.ui_context), document(ui->document), cache(ui->image_cache) {
        ui->document = tree->model->document;
        ui->image_cache = tree->model->image_resources;
    }
    ~PagedPaintResources() {
        tree->model->image_resources = ui->image_cache;
        ui->image_cache = cache; ui->document = lam::up(document);
    }
};

static TypesetStatus paged_publish_box_paint(PagedFlowState* state, LayoutViewNode* node,
        ViewBlock* source, BoundaryProp* boundary, Rect rect, const Rect* background_area = nullptr) {
    ViewBlock box = *source; box.bound = lam::view_prop(boundary);
    PagedPaintResources resources(state);
    auto capture = [&](RenderExportTargetKind target, lam::Up<const PaintList>* destination) -> TypesetStatus {
        PaintList* paint = paged_publish_paint(state);
        if (!paint) return TYPESET_OUT_OF_MEMORY;
        if (!render_paint_boundary_box(paint, resources.ui, &box, rect, target, background_area))
            return paged_flow_fail(state, TYPESET_UNPLACEABLE, source, "the fragment boundary could not be published");
        *destination = lam::up((const PaintList*)paint);
        return TYPESET_OK;
    };
    TypesetStatus status = capture(RENDER_EXPORT_TARGET_SVG, &node->paint_before);
    if (status == TYPESET_OK &&
        render_paint_background_requires_composite(boundary->background, RENDER_EXPORT_TARGET_PDF) !=
        render_paint_background_requires_composite(boundary->background, RENDER_EXPORT_TARGET_SVG))
        // PDF's shared background producer composites image layers before edge coverage is reconstructed.
        status = capture(RENDER_EXPORT_TARGET_PDF, &node->paint_before_pdf);
    return status;
}

static LayoutViewNode* paged_publish_parent(PagedFlowState* state, const PagedParentMap* parents, size_t index) {
    if (parents && index >= parents->first && index < parents->first + parents->count && parents->nodes[index - parents->first])
        return parents->nodes[index - parents->first];
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    if (fragmentainer.root) return fragmentainer.root;
    return fragmentainer.page ? &fragmentainer.page->node : state->tree->model->root.get();
}

// append a fragment for a view; a synthetic box (marker, generated content,
// table fixup) is attributed to its nearest authored ancestor as generated content
static LayoutViewNode* paged_publish_append(PagedFlowState* state, LayoutViewNode* parent, View* view,
        RdtLogicalRect rect, size_t text_start = 0, size_t text_length = 0) {
    if (state->tree->model->node_count >= state->composition->options.max_nodes) {
        paged_flow_fail(state, TYPESET_BUDGET_EXHAUSTED, view, "published fragments exceed the node budget");
        return nullptr;
    }
    DomNode* source = view;
    // generated text belongs to its pseudo-element, which belongs to its originating element
    if (source->is_text() && source->parent && source->parent->is_element() && source->parent->as_element()->is_synthetic())
        source = source->parent;
    while (source && source->is_element() && source->as_element()->is_synthetic()) source = source->parent;
    LayoutViewNode* node = view_tree_fragment_append(state->tree, parent, source, rect, text_start, text_length);
    if (node) {
        if (view->is_element()) node->display_inner = lam::view_require_element(view)->display.inner;
        node->hidden = view_visibility_is_hidden(view);
        node->role = state->publish_role;
        node->generated = state->publishing_fixed || source != (DomNode*)view || state->publish_role != VIEW_FRAGMENT_BODY;
        node->first_fragment = node->last_fragment = true;
    }
    return node;
}

static RdtLogicalRect paged_publish_rect(const PagedFragmentainer& fragmentainer, float left, float top, float width, float bottom) {
    float start = fmaxf(top, fragmentainer.top), end = fminf(bottom, fragmentainer.end);
    return {fragmentainer.rect.x + left, fragmentainer.rect.y + (start - fragmentainer.top), width, fmaxf(0.0f, end - start)};
}

static void paged_flow_background_copy(BackgroundProp* copy, const BackgroundProp* source);

// fragment edges follow box-decoration-break: a sliced box paints its top edge
// on the first fragment and its bottom edge on the last (CSS Fragmentation 3 §5.3)
static TypesetStatus paged_publish_boundary(PagedFlowState* state, LayoutViewNode* node, ViewBlock* block, bool first, bool last,
        bool background = true, const RdtLogicalRect* unsliced = nullptr) {
    const BoundaryProp* source = block->boundary();
    if (!source) return TYPESET_OK;
    ViewTree* tree = state->tree;
    // ordinary flow fragments need not attach a style snapshot, but their retained edge policy still comes from the source.
    const ViewCssStyle* style = view_css_resolve(tree, block);
    if (!view_tree_model_touch_node(tree, node)) return TYPESET_OUT_OF_MEMORY;
    struct Storage { BoundaryProp boundary; BorderProp border; BackgroundProp background; };
    Storage* storage = (Storage*)arena_alloc(tree->model->arena, sizeof(Storage));
    if (!storage) return TYPESET_OUT_OF_MEMORY;
    *storage = {};
    storage->boundary.padding = source->padding;
    if (source->border) {
        storage->border = *source->border;
        // scalar metadata stays independent; immutable commands retain border-image paint
        storage->border.border_image_type = GRADIENT_NONE;
        storage->border.border_image_linear_gradient = nullptr; storage->border.border_image_url = nullptr;
        storage->boundary.border = lam::own(&storage->border);
    }
    if (source->background && background) {
        paged_flow_background_copy(&storage->background, source->background);
        storage->boundary.background = lam::own(&storage->background);
    }
    // a collapsed cell paints the borders the grid resolved, straddling its grid lines (CSS 2.2 §17.6.2)
    const TableCellProp* cell = block->view_type == RDT_VIEW_TABLE_CELL ? static_cast<ViewTableCell*>(block)->cell() : nullptr;
    bool collapsed = cell && cell->top_resolved;
    if (collapsed) {
        const CollapsedBorder* sides[4] = {cell->top_resolved, cell->right_resolved, cell->bottom_resolved, cell->left_resolved};
        storage->border = {};
        for (size_t i = 0; i < 4; i++) {
            const CollapsedBorder* side = sides[i];
            bool painted = side && side->style != CSS_VALUE_NONE && side->style != CSS_VALUE_HIDDEN && side->color.a;
            storage->border.width.values[i] = painted ? side->width : 0.0f;
            storage->border.styles[i] = painted ? side->style : CSS_VALUE_NONE;
            if (painted) storage->border.colors[i] = side->color;
            node->border_outset[i] = painted ? side->width * 0.5f : 0.0f;
        }
        storage->boundary.border = lam::own(&storage->border);
    }
    bool clone = block->blk && block->block()->box_decoration_break == CSS_VALUE_CLONE;
    bool has_border = source->border || collapsed;
    for (size_t side = 0; side < 2; side++) if (!(side ? last : first)) {
        size_t edge = side * 2;
        if (!radiant_decoration_retain(style, true, side != 0)) storage->boundary.padding.values[edge] = 0.0f;
        if (has_border && !radiant_decoration_retain(style, false, side != 0)) {
            storage->border.width.values[edge] = 0.0f; node->border_outset[edge] = 0.0f;
        }
    }
    node->computed_boundary = lam::up(&storage->boundary);
    node->paint_box = true;
    BoundaryProp boundary = *source;
    BorderProp border = source->border ? *source->border : BorderProp{};
    if (source->border || collapsed) {
        if (collapsed) border = storage->border;
        // retained paint and scalar metadata share the independently selected edge widths.
        border.width = storage->border.width;
        boundary.border = lam::own(&border);
    }
    boundary.padding = storage->boundary.padding;
    if (!background) boundary.background = nullptr;
    const float* outset = node->border_outset;
    Rect rect = {node->rect.x - outset[3], node->rect.y - outset[0],
        node->rect.width + outset[1] + outset[3], node->rect.height + outset[0] + outset[2]};
    if (!clone && unsliced && (!first || !last)) {
        // sliced backgrounds keep the original positioning area and phase across sheets.
        Rect complete = {unsliced->x - outset[3], unsliced->y - outset[0],
            unsliced->width + outset[1] + outset[3], unsliced->height + outset[0] + outset[2]};
        return paged_publish_box_paint(state, node, block, &boundary, rect, &complete);
    }
    return paged_publish_box_paint(state, node, block, &boundary, rect);
}

static TypesetStatus paged_publish_image(PagedFlowState* state, LayoutViewNode* node, ViewBlock* block,
        float parent_x, float parent_y, const PagedFragmentainer& fragmentainer) {
    ImageSurface* image = render_media_image_surface(block);
    if (!image) return TYPESET_OK;
    ViewTree* tree = state->tree;
    if (block->embed && block->embedp()->selected_image_source) {
        // D4.5.1v4: an edition owns its images after the host cache and DOM workspace retire
        image = load_document_image_resource(tree->model->document, &tree->model->image_resources,
            block->embedp()->selected_image_source);
        if (!image) return paged_flow_fail(state, TYPESET_UNPLACEABLE, block,
            "the edition could not retain its image resource");
    }
    PaintImageBox* box = (PaintImageBox*)arena_alloc(tree->model->arena, sizeof(PaintImageBox));
    if (!box) return TYPESET_OUT_OF_MEMORY;
    *box = {};
    // the content box in page coordinates: the parent's flow origin mapped onto this page
    BlockBlot blot = {};
    blot.x = fragmentainer.rect.x + parent_x;
    blot.y = fragmentainer.rect.y + (parent_y - fragmentainer.top);
    box->content_rect = render_geometry_block_content_rect(&blot, block, 1.0f);
    box->image = lam::up(image);
    box->fonts = lam::up((FontContext*)tree->model->css->fonts);
    box->raster_scale = tree->model->environment.device_scale;
    box->opacity = 255;
    box->image_rect = render_media_image_rect(block, image, box->content_rect, 1.0f);
    node->image_box = lam::up(box);
    // a sliced replaced box clips its image to the fragment
    if (!node->first_fragment || !node->last_fragment) node->clip_content = true;
    return TYPESET_OK;
}

// the edition's copy of a DOM view font: the DOM view's props are reset by the
// next layout, so a glyph run keeps its own pinned copy (strings copied too)
static FontProp* paged_flow_font(PagedFlowState* state, FontProp* source) {
    if (!source) return nullptr;
    PagedComposition* composition = state->composition;
    // the cache is tiny: a document uses a handful of distinct fonts
    for (size_t i = 0; i < composition->flow_font_count; i += 2)
        if (composition->flow_fonts[i] == source) return composition->flow_fonts[i + 1];
    Arena* arena = state->tree->model->arena;
    FontProp* copy = (FontProp*)arena_alloc(arena, sizeof(FontProp));
    if (!copy) return nullptr;
    memset(copy, 0, sizeof(FontProp));
    font_prop_copy(copy, source);
    copy->text_shadow = nullptr;
    copy->family = lam::up(source->family ? (char*)pool_dup_n(composition->pool, source->family, strlen(source->family)) : nullptr);
    copy->platform_fallback_family = lam::up(source->platform_fallback_family
        ? (const char*)pool_dup_n(composition->pool, source->platform_fallback_family, strlen(source->platform_fallback_family)) : nullptr);
    if (!lam::pool_grow_array(composition->pool, &composition->flow_fonts, &composition->flow_font_capacity,
        composition->flow_font_count + 2, 16)) { font_cache_unpin_handle(copy->font_handle); return nullptr; }
    composition->flow_fonts[composition->flow_font_count++] = source;
    composition->flow_fonts[composition->flow_font_count++] = copy;
    return copy;
}

static bool paged_flow_forget_font_source(DomNode* node, void* context) {
    PagedComposition* composition = (PagedComposition*)context;
    FontProp* font = nullptr;
    if (node->is_text()) font = node->as_text()->font;
    else if (node->is_element()) font = node->as_element()->font;
    // published copies stay pinned; a reused DOM prop must acquire a fresh copy after restyling
    for (size_t i = 0; font && i < composition->flow_font_count; i += 2)
        if (composition->flow_fonts[i] == font) composition->flow_fonts[i] = nullptr;
    return true;
}

// boundary metadata keeps its scalar colour; complete image/gradient/layer paint
// lives in the edition's immutable PaintList rather than borrowing DOM props
static void paged_flow_background_copy(BackgroundProp* copy, const BackgroundProp* source) {
    *copy = {}; copy->color = source->color;
}

static Color paged_flow_text_color(DomNode* node) {
    for (DomNode* ancestor = node ? node->parent : nullptr; ancestor; ancestor = ancestor->parent) {
        DomElement* element = ancestor->as_element();
        if (element && element->in_line && element->inl()->has_color) return element->inl()->color;
    }
    Color black = {}; black.a = 255;
    return black;
}

static TypesetStatus paged_publish_glyph_run(PagedFlowState* state, LayoutViewNode* node, ViewText* text,
        const TextRect* rect, FontProp* font, Color color, float block_x, float block_y) {
    ViewTree* tree = state->tree;
    font = paged_flow_font(state, font);
    if (!font) return TYPESET_OUT_OF_MEMORY;
    struct Storage { PaintGlyphRun run; FontBox font; };
    Storage* storage = (Storage*)arena_alloc(tree->model->arena, sizeof(Storage));
    if (!storage) return TYPESET_OUT_OF_MEMORY;
    *storage = {};
    storage->font = {lam::up(font), font_prop_used_size(font)};
    lam::Temp<char> content(render_text_rect_glyph_run(text, rect, &storage->font, color, block_x, block_y, &storage->run));
    // a rect of collapsed spaces has no glyphs; the lowering rejects an empty run
    if (!content.get() || !content.get()[0]) return TYPESET_OK;
    // the edition owns its text; the export segment is transient
    size_t length = strlen(content.get());
    char* copy = (char*)arena_alloc(tree->model->arena, length + 1);
    if (!copy) return TYPESET_OUT_OF_MEMORY;
    memcpy(copy, content.get(), length + 1);
    storage->run.text = lam::up((const char*)copy);
    node->glyph_run = lam::up(&storage->run);
    PaintList* before = paged_publish_paint(state);
    PaintList* after = paged_publish_paint(state);
    if (!before || !after) return TYPESET_OUT_OF_MEMORY;
    PagedPaintResources resources(state);
    DomElement* parent = text->parent ? text->parent->as_element() : nullptr;
    if (parent) render_paint_text_background(before, resources.ui, parent, &storage->run,
        {block_x + parent->x, block_y + parent->y, parent->width, parent->height});
    RenderTextDecorationSource decoration = {text->text_data(), const_cast<TextRect*>(rect), block_x, block_y};
    if (text->font->text_shadow && !render_paint_text_shadows(before, resources.ui, &storage->run,
            text->font->text_shadow, &decoration)) return TYPESET_UNPLACEABLE;
    if (!render_paint_text_decorations(before, &storage->font, text->text_data(), const_cast<TextRect*>(rect),
            block_x, block_y, color, true) ||
        !render_paint_text_decorations(after, &storage->font, text->text_data(), const_cast<TextRect*>(rect),
            block_x, block_y, color, false) ||
        !render_paint_text_emphasis(after, resources.ui->font_ctx, &storage->run)) return TYPESET_UNPLACEABLE;
    node->paint_before = lam::up((const PaintList*)before); node->paint_after = lam::up((const PaintList*)after);
    return TYPESET_OK;
}

static TypesetStatus paged_publish_text(PagedFlowState* state, ViewText* text, float origin_x, float origin_y,
        const PagedParentMap* parents) {
    FontProp* font = text->font;
    if (!font) return TYPESET_OK;
    Color color = paged_flow_text_color(text);
    for (TextRect* rect = text->rect; rect; rect = rect->next) {
        if (rect->length <= 0 && rect->width <= 0.0f) continue;
        float top = origin_y + rect->y;
        // a rect follows its line box; negative half-leading can start it above the line
        size_t index = paged_fragmentainer_index(state, top + rect->height * 0.5f);
        const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
        if (state->publish_role == VIEW_FRAGMENT_NOTE &&
            (top + rect->height <= fragmentainer.top + kPagedFlowEpsilon || top >= fragmentainer.end - kPagedFlowEpsilon)) continue;
        RdtLogicalRect local = {fragmentainer.rect.x + origin_x + rect->x, fragmentainer.rect.y + (top - fragmentainer.top),
            rect->width, rect->height};
        int start, length;
        text_rect_painted_source_range(rect, &start, &length);
        LayoutViewNode* node = paged_publish_append(state, paged_publish_parent(state, parents, index), text,
            local, (size_t)start, (size_t)(length > 0 ? length : 0));
        if (!node) return TYPESET_OUT_OF_MEMORY;
        TypesetStatus status = paged_publish_glyph_run(state, node, text, rect, font, color,
            fragmentainer.rect.x + origin_x, fragmentainer.rect.y + (origin_y - fragmentainer.top));
        if (status == TYPESET_OK && node->glyph_run) status = paged_publish_content(state, index);
        if (status == TYPESET_OK) status = paged_folio_capture(state, text, index);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

// a list marker: numbered markers are a right-aligned glyph run in the marker
// font; disc, circle and square are a box with the bullet colour, as the raster
// painter draws them (render_list.cpp); image markers share its image producer
static TypesetStatus paged_publish_marker(PagedFlowState* state, ViewSpan* marker, float origin_x, float origin_y,
        const PagedParentMap* parents) {
    DomElement* element = marker;
    MarkerProp* prop = element->marker_prop();
    if (!prop) return TYPESET_OK;
    FontProp* font = element->font;
    if (!font) {
        ViewBlock* container = paged_flow_parent_block(marker);
        font = container ? container->font : nullptr;
    }
    if (!font) return TYPESET_OK;
    Color color = prop->has_color ? prop->color : paged_flow_text_color(marker);
    float top = origin_y + marker->y;
    size_t index = paged_fragmentainer_index(state, top + marker->height * 0.5f);
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    float page_x = fragmentainer.rect.x + origin_x + marker->x, page_y = fragmentainer.rect.y + (top - fragmentainer.top);
    ViewTree* tree = state->tree;
    TypesetStatus content = paged_publish_content(state, index);
    if (content != TYPESET_OK) return content;
    if (prop->is_image_marker || (prop->image.url && strcmp(prop->image.url, "none") != 0)) {
        LayoutViewNode* node = paged_publish_append(state, paged_publish_parent(state, parents, index), marker,
            {page_x, page_y, marker->width, marker->height});
        PaintList* paint = node ? paged_publish_paint(state) : nullptr;
        if (!paint) return TYPESET_OUT_OF_MEMORY;
        PagedPaintResources resources(state);
        FontBox box = {lam::up(font), font_prop_used_size(font)};
        if (!render_paint_marker_image(paint, resources.ui, marker,
                fragmentainer.rect.x + origin_x, fragmentainer.rect.y + origin_y - fragmentainer.top, &box))
            return paged_flow_fail(state, TYPESET_UNPLACEABLE, marker, "the marker image could not be published");
        node->paint_before = lam::up((const PaintList*)paint);
        return TYPESET_OK;
    }
    bool bullet = prop->marker_type == CSS_VALUE_DISC || prop->marker_type == CSS_VALUE_CIRCLE || prop->marker_type == CSS_VALUE_SQUARE;
    if (bullet) {
        float size = prop->bullet_size, font_size = font_prop_used_size(font);
        RdtLogicalRect rect = {page_x + prop->width - font_size - size * 0.5f, page_y + marker->height * 0.5f - size * 0.5f, size, size};
        LayoutViewNode* node = paged_publish_append(state, paged_publish_parent(state, parents, index), marker, rect);
        if (!node) return TYPESET_OUT_OF_MEMORY;
        node->generated = true;
        struct Storage { BoundaryProp boundary; BorderProp border; BackgroundProp background; };
        Storage* storage = (Storage*)arena_alloc(tree->model->arena, sizeof(Storage));
        if (!storage) return TYPESET_OUT_OF_MEMORY;
        *storage = {};
        if (prop->marker_type == CSS_VALUE_CIRCLE) {
            for (size_t i = 0; i < 4; i++) { storage->border.width.values[i] = 1.0f; storage->border.styles[i] = CSS_VALUE_SOLID; storage->border.colors[i] = color; }
        } else storage->background.color = color;
        if (prop->marker_type != CSS_VALUE_SQUARE)
            for (size_t i = 0; i < 4; i++) { storage->border.radius.horizontal[i] = size * 0.5f; storage->border.radius.vertical[i] = size * 0.5f; }
        storage->boundary.border = lam::own(&storage->border);
        storage->boundary.background = lam::own(&storage->background);
        node->computed_boundary = lam::up(&storage->boundary);
        node->paint_box = true;
        return TYPESET_OK;
    }
    const char* text = prop->text_content;
    if (!text || !*text || !font->font_handle) return TYPESET_OK;
    font = paged_flow_font(state, font);
    if (!font) return TYPESET_OUT_OF_MEMORY;
    RdtLogicalRect rect = {page_x, page_y, prop->width, marker->height};
    LayoutViewNode* node = paged_publish_append(state, paged_publish_parent(state, parents, index), marker, rect);
    if (!node) return TYPESET_OUT_OF_MEMORY;
    node->generated = true;
    struct Storage { PaintGlyphRun run; FontBox font; };
    Storage* storage = (Storage*)arena_alloc(tree->model->arena, sizeof(Storage));
    if (!storage) return TYPESET_OUT_OF_MEMORY;
    *storage = {};
    storage->font = {lam::up(font), font_prop_used_size(font)};
    size_t length = strlen(text);
    char* copy = (char*)arena_alloc(tree->model->arena, length + 1);
    if (!copy) return TYPESET_OUT_OF_MEMORY;
    memcpy(copy, text, length + 1);
    PaintGlyphRun* run = &storage->run;
    run->font = lam::up(&storage->font);
    run->color = color;
    run->text = lam::up((const char*)copy);
    run->text_len = (int)length; // INT_CAST_OK: marker text is a short counter string
    run->font_family = lam::up(font_handle_get_family_name(font->font_handle));
    run->font_size = font_prop_used_size(font);
    // the marker text ends at the marker box's end, as the raster painter places it
    TextExtents extents = font_measure_text(font->font_handle, text, (int)length); // INT_CAST_OK: marker text is a short counter string
    run->x = page_x + prop->width - extents.width;
    const FontMetrics* metrics = font_get_metrics(font->font_handle);
    run->baseline_y = page_y + (metrics ? metrics->hhea_ascender : prop->ascender);
    node->glyph_run = lam::up(run);
    return TYPESET_OK;
}

static TypesetStatus paged_publish_block(PagedFlowState* state, ViewBlock* block, float origin_x, float origin_y,
        const PagedParentMap* parents, bool root);

struct PagedInlinePaint {
    PagedFlowState* state;
    ViewSpan* span;
    float x, y;
    const PagedParentMap* parents;
    TypesetStatus status;
};

static bool paged_publish_inline_fragment(void* context, RdtLogicalRect rect, bool first, bool last, float offset, float extent) {
    auto* input = (PagedInlinePaint*)context;
    PagedFlowState* state = input->state;
    size_t index = paged_fragmentainer_index(state, input->y + rect.y + rect.height * 0.5f);
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    rect.x += fragmentainer.rect.x + input->x;
    rect.y += fragmentainer.rect.y + input->y - fragmentainer.top;
    LayoutViewNode* node = paged_publish_append(state, paged_publish_parent(state, input->parents, index), input->span, rect);
    if (!node) { input->status = TYPESET_OUT_OF_MEMORY; return false; }
    node->first_fragment = first; node->last_fragment = last; node->paint_box = true;
    BoundaryProp boundary = *input->span->boundary();
    BorderProp border = boundary.border ? *boundary.border : BorderProp{};
    bool clone = input->span->blk && input->span->block()->box_decoration_break == CSS_VALUE_CLONE;
    if (!first && !clone) { boundary.padding.left = 0.0f; border.width.left = 0.0f; }
    if (!last && !clone) { boundary.padding.right = 0.0f; border.width.right = 0.0f; }
    if (boundary.border) boundary.border = lam::own(&border);
    Rect clip = {rect.x, rect.y, rect.width, rect.height};
    Rect complete = clone ? clip : Rect{rect.x - offset, rect.y, extent, rect.height};
    input->status = paged_publish_box_paint(state, node, lam::unsafe_view_block_api_span(input->span),
        &boundary, complete, clone ? nullptr : &clip);
    return input->status == TYPESET_OK;
}

static TypesetStatus paged_publish_children(PagedFlowState* state, ViewElement* parent, float origin_x, float origin_y,
        const PagedParentMap* parents) {
    for (DomNode* node = parent->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        // a display:none subtree or a running element binds its assignments where the walk passes it
        if (!child->view_type && node->is_element()) paged_publish_mark(state, node, true);
        if (!child->view_type || child == state->publish_skip) continue;
        if (!state->publishing_fixed && child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
            if (block->position && block->positionp()->position == CSS_VALUE_FIXED &&
                find_containing_block(block, CSS_VALUE_FIXED) == find_initial_containing_view_block(block) &&
                state->tree->model->environment.presentation == VIEW_PRESENTATION_PAGED) {
                // page-fixed content is independent of its source parent's fragment and repeats once per sheet
                bool collected = false;
                for (PagedFixedBox* fixed = state->fixed_boxes; fixed; fixed = fixed->next)
                    if (fixed->block == block) { collected = true; break; }
                if (collected) continue;
                PagedFixedBox* fixed = (PagedFixedBox*)pool_calloc(state->composition->pool, sizeof(PagedFixedBox));
                if (!fixed) return TYPESET_OUT_OF_MEMORY;
                // remove the source parent's page offset while retaining its local containing-block offsets
                float page_top = state->fragmentainers[paged_fragmentainer_index(state, origin_y)].top;
                *fixed = {block, origin_x, origin_y - page_top, nullptr};
                if (state->last_fixed_box) state->last_fixed_box->next = fixed;
                else state->fixed_boxes = fixed;
                state->last_fixed_box = fixed;
                continue;
            }
        }
        TypesetStatus status = TYPESET_OK;
        if (child->view_type == RDT_VIEW_TEXT) status = paged_publish_text(state, lam::view_require_text(child), origin_x, origin_y, parents);
        else if (child->is_block()) status = paged_publish_block(state, lam::view_require_block(child), origin_x, origin_y, parents, false);
        else if (child->view_type == RDT_VIEW_MARKER)
            status = paged_publish_marker(state, lam::view_require_element(child), origin_x, origin_y, parents);
        else if (child->view_type == RDT_VIEW_INLINE) {
            bool target = paged_publish_mark(state, node, false);
            ViewSpan* span = lam::view_require_element(child);
            if (span->bound) {
                PagedInlinePaint input = {state, span, origin_x, origin_y, parents, TYPESET_OK};
                if (!layout_inline_visit_fragments(span, paged_publish_inline_fragment, &input))
                    status = input.status == TYPESET_OK ? TYPESET_OUT_OF_MEMORY : input.status;
            }
            if (status == TYPESET_OK) status = paged_publish_children(state, lam::view_require_element(child), origin_x, origin_y, parents);
            if (target) state->open_target_count--;
        }
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

// where the content a strut moved ended on the previous fragmentainer, plus the
// cloned bottom edges of the boxes from the strut up to `block` (a box strut
// moved the box itself, so its own edges start on the next fragmentainer)
static float paged_strut_slice_end(const PagedFlowState* state, const PagedStrut& strut, View* block, bool include_block) {
    float end = strut.before;
    for (View* current = strut.paragraph ? strut.view : strut.view->parent; current; current = current->parent) {
        if (current == block && !include_block) break;
        if (current->is_block()) {
            float edge_top, edge_bottom;
            paged_box_clone_edges(state, lam::view_require_block(current), &edge_top, &edge_bottom);
            end += edge_bottom;
        }
        if (current == block) break;
    }
    return end;
}

// a box with cloned edges ends its slice on fragmentainer `index` after the content
// the break left there, plus the cloned bottom edges of the boxes between
static float paged_clone_slice_end(const PagedFlowState* state, ViewBlock* block, size_t index) {
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    float retained_top, retained_bottom;
    paged_box_clone_edges(state, block, &retained_top, &retained_bottom);
    if (retained_top == 0.0f && retained_bottom == 0.0f) return fragmentainer.end;
    for (size_t i = 0; i < state->strut_count; i++) {
        const PagedStrut& entry = state->struts[i];
        // a broken table's entry stands for the breaks its record keeps
        size_t count = entry.table ? entry.table->strut_count : 1;
        for (size_t j = 0; j < count; j++) {
            const PagedStrut& strut = entry.table ? entry.table->struts[j] : entry;
            if (strut.fragmentainer != index + 1 || !paged_view_inside(strut.view, block)) continue;
            return fminf(paged_strut_slice_end(state, strut, block, true), fragmentainer.end);
        }
    }
    return fragmentainer.end;
}

// a broken table's body content ends on each fragmentainer it continues from:
// where its breaks left the rows and cells there, or at the space kept for the
// repeated footer; the footer copy follows one border spacing below
static float* paged_table_ends(PagedFlowState* state, const PagedFlowTable* record, size_t last) {
    size_t count = last - record->first;
    float* ends = (float*)pool_calloc(state->composition->pool, (count ? count : 1) * sizeof(float));
    if (!ends) return nullptr;
    for (size_t k = 0; k < count; k++) ends[k] = -INFINITY;
    for (size_t i = 0; i < record->strut_count; i++) {
        const PagedStrut& strut = record->struts[i];
        if (strut.fragmentainer <= record->first || strut.fragmentainer > last) continue;
        float end = paged_strut_slice_end(state, strut, record->table, false);
        // a moved row left the spacing above it behind
        if (strut.line < 0 && (strut.view->view_type == RDT_VIEW_TABLE_ROW || strut.view->view_type == RDT_VIEW_TABLE_ROW_GROUP))
            end -= record->spacing;
        float* slot = &ends[strut.fragmentainer - 1 - record->first];
        *slot = fmaxf(*slot, end);
    }
    for (size_t k = 0; k < count; k++) {
        const PagedFragmentainer& fragmentainer = state->fragmentainers[record->first + k];
        // the body content ends above the footnotes and page floats there, in layout and publication alike
        float limit = fragmentainer.bottom - paged_fragmentainer_reserved(state, record->first + k) - record->footer_size;
        ends[k] = isfinite(ends[k]) ? fminf(ends[k], limit) : limit;
    }
    return ends;
}

static const PagedFlowTable* paged_publish_table_record(const PagedFlowState* state, const ViewBlock* block) {
    for (const PagedFlowTable* record = state->tables; record; record = record->next)
        if (record->table == block) return record->modified ? record : nullptr;
    return nullptr;
}

// row, row group and column backgrounds paint only under the cells they hold, so
// the table's background shows in the spacing and where cells are missing (CSS
// 2.2 §17.5.1); their boxes publish without a background
static bool paged_table_layer(const ViewBlock* block) {
    return block->view_type == RDT_VIEW_TABLE_ROW || block->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
        block->view_type == RDT_VIEW_TABLE_COLUMN || block->view_type == RDT_VIEW_TABLE_COLUMN_GROUP;
}

// the background of one layer box under a cell fragment
static TypesetStatus paged_publish_cell_layer(PagedFlowState* state, LayoutViewNode* parent, const RdtLogicalRect& cell,
        ViewBlock* layer) {
    const BackgroundProp* background = layer->bound ? layer->boundary()->background.get() : nullptr;
    if (!background || cell.width <= 0.0f || cell.height <= 0.0f) return TYPESET_OK;
    LayoutViewNode* node = view_tree_fragment_append(state->tree, parent, nullptr, cell);
    if (!node) return TYPESET_OUT_OF_MEMORY;
    node->role = state->publish_role; node->generated = true;
    node->hidden = view_visibility_is_hidden(layer);
    struct Storage { BoundaryProp boundary; BackgroundProp background; };
    Storage* storage = (Storage*)arena_alloc(state->tree->model->arena, sizeof(Storage));
    if (!storage) return TYPESET_OUT_OF_MEMORY;
    *storage = {};
    paged_flow_background_copy(&storage->background, background);
    storage->boundary.background = lam::own(&storage->background);
    node->computed_boundary = lam::up(&storage->boundary);
    node->paint_box = true;
    BoundaryProp boundary = {}; boundary.background = lam::own(const_cast<BackgroundProp*>(background));
    return paged_publish_box_paint(state, node, layer, &boundary, {cell.x, cell.y, cell.width, cell.height});
}

// the layers under a cell fragment, in order: the column groups and columns its
// first column belongs to, its row group and the row it starts in; each covers
// the whole cell, as the cells a layer holds are the ones originating in it
static TypesetStatus paged_publish_cell_layers(PagedFlowState* state, LayoutViewNode* parent, const RdtLogicalRect& rect,
        ViewBlock* cell) {
    View* row = cell->parent;
    const TableCellProp* prop = static_cast<ViewTableCell*>(cell)->cell();
    if (!row || row->view_type != RDT_VIEW_TABLE_ROW || !prop) return TYPESET_OK;
    ViewBlock* group = static_cast<ViewTableRow*>(row)->parent_row_group();
    ViewTable* table = nullptr;
    for (View* current = row; current && !table; current = current->parent)
        if (current->view_type == RDT_VIEW_TABLE) table = static_cast<ViewTable*>(current);
    if (!table) return TYPESET_OK;
    size_t column = prop->col_index > 0 ? (size_t)prop->col_index : 0;
    TypesetStatus status = TYPESET_OK;
    for (int pass = 0; pass < 2; pass++) {
        // every column group first, then every column (CSS 2.2 §17.5.1 layers)
        size_t next = 0;
        table->each_column_source([&](ViewElement* source) {
            if (status != TYPESET_OK) return;
            size_t start = next;
            if (source->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
                bool columns = false;
                radiant_each_table_colgroup_column(source, [&](ViewElement* child) {
                    size_t span = layout_table_column_span(child);
                    if (pass == 1 && status == TYPESET_OK && column >= next && column < next + span)
                        status = paged_publish_cell_layer(state, parent, rect, lam::view_require_block(child));
                    next += span; columns = true;
                });
                if (!columns) next += layout_table_column_span(source);
                if (pass == 0 && status == TYPESET_OK && column >= start && column < next)
                    status = paged_publish_cell_layer(state, parent, rect, lam::view_require_block(source));
            } else {
                next += layout_table_column_span(source);
                if (pass == 1 && column >= start && column < next)
                    status = paged_publish_cell_layer(state, parent, rect, lam::view_require_block(source));
            }
        });
    }
    if (status == TYPESET_OK && group && group->view_type == RDT_VIEW_TABLE_ROW_GROUP)
        status = paged_publish_cell_layer(state, parent, rect, group);
    if (status == TYPESET_OK) status = paged_publish_cell_layer(state, parent, rect, lam::view_require_block(row));
    return status;
}

static TypesetStatus paged_publish_cell_range(PagedFlowState* state, LayoutViewNode* node, ViewBlock* block) {
    const TableCellProp* cell = static_cast<ViewTableCell*>(block)->cell();
    if (!cell) return TYPESET_OK;
    ViewTableRange* range = (ViewTableRange*)arena_alloc(state->tree->model->arena, sizeof(ViewTableRange));
    if (!range) return TYPESET_OUT_OF_MEMORY;
    *range = {(size_t)(cell->col_index > 0 ? cell->col_index : 0), (size_t)(cell->col_span > 1 ? cell->col_span : 1), false};
    node->table_range = lam::up((const ViewTableRange*)range);
    return TYPESET_OK;
}

// the slice of a box on fragmentainer `index`: a box with cloned edges ends after
// the content its break left there, and inside a broken table a body box keeps
// to the table's body window while the table box and its columns end after the
// repeated footer
static void paged_publish_slice(const PagedFlowState* state, ViewBlock* block, size_t index, size_t last,
        float top, float bottom, const PagedFlowTable* record, const float* ends, float* slice_top, float* slice_bottom) {
    *slice_top = top;
    *slice_bottom = index < last ? fminf(bottom, paged_clone_slice_end(state, block, index)) : bottom;
    const PagedFlowTable* table = state->publish_table;
    if (record) {
        if (index >= record->first && index < last) *slice_bottom = fminf(bottom, ends[index - record->first] + record->footer_size);
        return;
    }
    if (!table || state->publish_role != VIEW_FRAGMENT_BODY || index < table->first) return;
    bool column = block->view_type == RDT_VIEW_TABLE_COLUMN || block->view_type == RDT_VIEW_TABLE_COLUMN_GROUP;
    bool body = !column && paged_table_body_view(block, table->table);
    if (!column && !body) return;
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    if (index < state->publish_last) {
        float end = state->publish_ends[index - table->first];
        *slice_bottom = fminf(bottom, column ? end + table->footer_size : end);
    }
    if (body && index > table->first) *slice_top = fmaxf(top, fragmentainer.top + table->header_size);
}

// a box without child views is content of its own, as are atomic inlines and replaced boxes
static bool paged_publish_leaf(const ViewBlock* block) {
    if (block->display.inner == RDT_DISPLAY_REPLACED || block->display.outer == CSS_VALUE_INLINE_BLOCK) return true;
    for (const DomNode* child = block->first_child; child; child = child->next_sibling)
        if (((const View*)child)->view_type) return false;
    return block->height > 0.0f;
}

static TypesetStatus paged_publish_block(PagedFlowState* state, ViewBlock* block, float origin_x, float origin_y,
        const PagedParentMap* parents, bool root);

static TypesetStatus paged_publish_box(PagedFlowState* state, ViewBlock* block, float origin_x, float origin_y,
        const PagedParentMap* parents, bool root);

static TypesetStatus paged_publish_block(PagedFlowState* state, ViewBlock* block, float origin_x, float origin_y,
        const PagedParentMap* parents, bool root) {
    if (block->display.outer == CSS_VALUE_NONE) return TYPESET_OK;
    bool target = paged_publish_mark(state, block, false);
    TypesetStatus status = paged_publish_box(state, block, origin_x, origin_y, parents, root);
    if (target) state->open_target_count--;
    return status;
}

static TypesetStatus paged_publish_box(PagedFlowState* state, ViewBlock* block, float origin_x, float origin_y,
        const PagedParentMap* parents, bool root) {
    float left = origin_x + block->x, top = origin_y + block->y, bottom = top + block->height;
    size_t first, last;
    if (root) {
        // the root box is the flow itself: every page carries a slice of it
        first = 0; last = state->fragmentainer_count - 1;
    } else if (block->display.outer == CSS_VALUE_INLINE_BLOCK || block->view_type == RDT_VIEW_INLINE_BLOCK) {
        // an atomic inline follows its line box, which may start above its leading
        first = last = paged_fragmentainer_index(state, top + block->height * 0.5f);
    } else {
        first = paged_fragmentainer_index(state, top);
        last = block->height > 0.0f ? paged_fragmentainer_index(state, bottom - kPagedFlowEpsilon) : first;
        if (last < first) last = first;
    }
    Pool* pool = state->composition->pool;
    const PagedFlowTable* record = paged_flow_is_table(block) ? paged_publish_table_record(state, block) : nullptr;
    float* ends = record && last > record->first ? paged_table_ends(state, record, last) : nullptr;
    if (record && last > record->first && !ends) return TYPESET_OUT_OF_MEMORY;
    if (!ends) record = nullptr;
    float slice_top, slice_bottom;
    if (!root) {
        // a window can leave a box nothing on its first or last fragmentainer
        while (first < last) {
            paged_publish_slice(state, block, first, last, top, bottom, record, ends, &slice_top, &slice_bottom);
            if (fminf(slice_bottom, state->fragmentainers[first].end) > fmaxf(slice_top, state->fragmentainers[first].top) + kPagedFlowEpsilon) break;
            first++;
        }
        while (last > first) {
            paged_publish_slice(state, block, last, last, top, bottom, record, ends, &slice_top, &slice_bottom);
            if (fminf(slice_bottom, state->fragmentainers[last].end) > fmaxf(slice_top, state->fragmentainers[last].top) + kPagedFlowEpsilon) break;
            last--;
        }
    }
    PagedParentMap mine = {first, last - first + 1, nullptr};
    mine.nodes = (LayoutViewNode**)pool_calloc(pool, mine.count * sizeof(LayoutViewNode*));
    if (!mine.nodes) return TYPESET_OUT_OF_MEMORY;
    TypesetStatus status = TYPESET_OK;
    for (size_t index = first; status == TYPESET_OK && index <= last; index++) {
        const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
        LayoutViewNode* parent = root ? (fragmentainer.page ? &fragmentainer.page->node : state->tree->model->root.get())
            : paged_publish_parent(state, parents, index);
        paged_publish_slice(state, block, index, last, top, bottom, record, ends, &slice_top, &slice_bottom);
        // the root and body hold the runs (plan §4.2): each fragment takes its run's width
        float width = block->width;
        if (state->publish_role == VIEW_FRAGMENT_BODY && state->fragmentainer_count > 1 &&
            paged_flow_run_holder(state, block)) {
            // a later native sequence starts at its own width, not the document's opening master.
            PagedFlowWidth* measured = paged_flow_width(state, block, false);
            width += fragmentainer.rect.width - (measured ? measured->containing_width : state->fragmentainers[0].rect.width);
        }
        else if (state->publish_role == VIEW_FRAGMENT_BODY && (!block->blk || block->block()->given_width < 0.0f)) {
            PagedFlowWidth* measured = paged_flow_width(state, block, false);
            if (measured) width += fragmentainer.rect.width - measured->containing_width;
        }
        RdtLogicalRect rect = paged_publish_rect(fragmentainer, left, slice_top, width, slice_bottom);
        if (block->view_type == RDT_VIEW_TABLE_CELL) {
            status = paged_publish_cell_layers(state, parent, rect, block);
            if (status != TYPESET_OK) break;
        }
        LayoutViewNode* node = paged_publish_append(state, parent, block, rect);
        if (!node) { status = TYPESET_OUT_OF_MEMORY; break; }
        node->first_fragment = index == first; node->last_fragment = index == last;
        if (root) state->fragmentainers[index].root = node;
        mine.nodes[index - first] = node;
        if (block->view_type == RDT_VIEW_TABLE) {
            // captions belong to the wrapper; only the grid owns table borders and background.
            RdtLogicalRect grid = layout_table_grid_rect(static_cast<ViewTable*>(block));
            float grid_top = top + grid.y, grid_bottom = grid_top + grid.height;
            float begin = fmaxf(grid_top, fmaxf(slice_top, fragmentainer.top));
            float end = fminf(grid_bottom, fminf(slice_bottom, fragmentainer.end));
            if (end > begin) {
                LayoutViewNode* box = paged_publish_append(state, node, block,
                    {rect.x + grid.x, fragmentainer.rect.y + begin - fragmentainer.top, grid.width, end - begin});
                if (!box) { status = TYPESET_OUT_OF_MEMORY; break; }
                box->generated = true;
                box->first_fragment = begin <= grid_top + kPagedFlowEpsilon;
                box->last_fragment = end >= grid_bottom - kPagedFlowEpsilon;
                RdtLogicalRect complete = {rect.x + grid.x, fragmentainer.rect.y + grid_top - fragmentainer.top, grid.width, grid.height};
                status = paged_publish_boundary(state, box, block, box->first_fragment, box->last_fragment, true, &complete);
            }
        } else {
            RdtLogicalRect complete = {fragmentainer.rect.x + left, fragmentainer.rect.y + top - fragmentainer.top, width, block->height};
            status = paged_publish_boundary(state, node, block, node->first_fragment, node->last_fragment, !paged_table_layer(block), &complete);
        }
        if (status == TYPESET_OK && block->view_type == RDT_VIEW_TABLE_CELL) status = paged_publish_cell_range(state, node, block);
        if (status == TYPESET_OK && block->display.inner == RDT_DISPLAY_REPLACED)
            status = paged_publish_image(state, node, block, origin_x, origin_y, fragmentainer);
        if (status == TYPESET_OK && index == first && !root && paged_publish_leaf(block)) status = paged_publish_content(state, index);
    }
    if (record) {
        // the table's grid keeps to its body windows; the header group then
        // repeats at the top of each continuation and the footer group closes
        // each fragment before the last (CSS 2.2 §17.2)
        const PagedFlowTable* saved_table = state->publish_table;
        float* saved_ends = state->publish_ends;
        size_t saved_last = state->publish_last;
        // the footer group publishes after its copies, so its occurrences follow the pages
        View* saved_skip = state->publish_skip;
        state->publish_table = record; state->publish_ends = ends; state->publish_last = last;
        state->publish_skip = record->footer;
        if (status == TYPESET_OK) status = paged_publish_children(state, block, left, top, &mine);
        state->publish_table = saved_table; state->publish_ends = saved_ends; state->publish_last = saved_last;
        state->publish_skip = saved_skip;
        ViewFragmentRole saved_role = state->publish_role;
        state->publish_role = VIEW_FRAGMENT_REPEATED_TABLE;
        for (size_t index = record->first + 1; status == TYPESET_OK && record->header && index <= last; index++)
            status = paged_publish_block(state, record->header, left,
                state->fragmentainers[index].top + record->spacing - record->header->y, &mine, false);
        for (size_t index = record->first; status == TYPESET_OK && record->footer && index < last; index++)
            status = paged_publish_block(state, record->footer, left,
                ends[index - record->first] + record->spacing - record->footer->y, &mine, false);
        state->publish_role = saved_role;
        if (status == TYPESET_OK && record->footer) status = paged_publish_block(state, record->footer, left, top, &mine, false);
    } else if (status == TYPESET_OK) status = paged_publish_children(state, block, left, top, &mine);
    pool_free(pool, mine.nodes);
    return status;
}

static ViewBlock* paged_flow_root(PagedFlowState* state) {
    ViewTree* views = state->tree->model->document->view_tree;
    return views ? lam::view_as_block(views->root.get()) : nullptr;
}

// the flow ended: anchors after the last content take the last fragmentainer, or
// a new page when they do not fit there, and the notes still waiting take the
// last fragmentainer's space, then pages of their own, where the footnote area
// keeps no height limit
static TypesetStatus paged_anchors_finish(PagedFlowState* state) {
    if (!state->anchor_count) return state->status;
    float flow_end = state->flow_end;
    size_t last = paged_fragmentainer_index(state, flow_end - kPagedFlowEpsilon);
    size_t from = state->anchors_bound, end = state->anchor_count;
    state->anchors_bound = end;
    float content_end = flow_end;
    while (from < end) {
        // each anchor that does not fit after the content starts a new page with the ones after it
        size_t kept = 0;
        PagedAnchorFit fit = paged_anchors_place(state, from, end - from, last, content_end,
            content_end <= state->fragmentainers[last].top + kPagedFlowEpsilon, &kept);
        if (fit == PAGED_ANCHORS_FAILED) return state->status;
        if (fit == PAGED_ANCHORS_PLACED) break;
        from += kept;
        TypesetStatus status = paged_fragmentainer_append(state, true);
        if (status != TYPESET_OK) return status;
        last = state->fragmentainer_count - 1;
        content_end = flow_end = state->fragmentainers[last].top;
    }
    if (state->notes_waiting) paged_notes_resume(state, last, fmaxf(flow_end, state->fragmentainers[last].top), false);
    state->draining = true;
    while (state->notes_waiting) {
        size_t waiting = state->notes_waiting;
        TypesetStatus status = paged_fragmentainer_append(state, true);
        if (status != TYPESET_OK) return status;
        if (state->notes_waiting == waiting) {
            for (size_t i = 0; i < state->anchor_count; i++) if (state->anchors[i].deferred)
                return paged_flow_fail(state, TYPESET_UNPLACEABLE, state->anchors[i].element, "a footnote is taller than its page's body area");
        }
    }
    return state->status;
}

static TypesetStatus paged_publish_anchors(PagedFlowState* state, LayoutContext* lycon);
static TypesetStatus paged_publish_fixed(PagedFlowState* state, LayoutContext* lycon);

static TypesetStatus paged_flow_publish(PagedFlowState* state, LayoutContext* lycon) {
    ViewBlock* root = paged_flow_root(state);
    if (!root) return paged_flow_fail(state, TYPESET_INVALID, nullptr, "the page flow produced no root box");
    // the body ends above the footnotes and page floats placed on each
    // fragmentainer, and its top floats move it down (plan P5)
    for (size_t i = 0; i < state->fragmentainer_count; i++) {
        PagedFragmentainer& fragmentainer = state->fragmentainers[i];
        fragmentainer.end = fragmentainer.bottom - paged_fragmentainer_reserved(state, i);
        fragmentainer.rect.y += fragmentainer.float_top;
    }
    state->pending_count = state->open_target_count = 0; state->content_page = 0;
    state->fixed_boxes = state->last_fixed_box = nullptr;
    TypesetStatus status = paged_publish_block(state, root, 0.0f, 0.0f, nullptr, true);
    // sources after the last content bind to the last page
    const ViewPageBox* last = state->fragmentainer_count ? state->fragmentainers[state->fragmentainer_count - 1].page : nullptr;
    if (status == TYPESET_OK && state->pending_count && last) status = paged_publish_pending(state, last->page_number);
    if (status == TYPESET_OK && state->anchor_count) status = paged_publish_anchors(state, lycon);
    // rule painting consumes the columns with published body material, not speculative empty columns.
    for (size_t i = 0; status == TYPESET_OK && i < state->fragmentainer_count; i++) {
        const PagedFragmentainer& fragmentainer = state->fragmentainers[i];
        ViewPageBox* page = fragmentainer.page;
        if (!page || !fragmentainer.occupied ||
            (page->occupied_columns && page->occupied_columns->index == fragmentainer.column)) continue;
        ViewPageColumn* column = (ViewPageColumn*)pool_alloc(state->composition->pool, sizeof(ViewPageColumn));
        if (!column || !view_tree_model_touch_node(state->tree, &page->node)) return TYPESET_OUT_OF_MEMORY;
        *column = {page->occupied_columns, fragmentainer.column};
        page->occupied_columns = lam::up((const ViewPageColumn*)column);
    }
    return status;
}

// publish an element laid out outside the flow into `rect` under `parent`: the
// publisher's walk places its views in one fragmentainer at the rectangle; the
// element's own fragment, returned, is the occurrence of the material's box
static LayoutViewNode* paged_detached_publish(PagedFlowState* state, DomElement* element, RdtLogicalRect rect,
        LayoutViewNode* parent, ViewFragmentRole role, ViewPageBox* page, TypesetStatus* status,
        float origin_x = 0.0f, float origin_y = 0.0f, float window_top = 0.0f, float window_height = INFINITY) {
    ViewBlock* view = lam::view_as_block((View*)element);
    *status = TYPESET_OK;
    if (!view) return nullptr;
    PagedFragmentainer* saved = state->fragmentainers;
    size_t saved_count = state->fragmentainer_count;
    ViewFragmentRole saved_role = state->publish_role;
    PagedFragmentainer local = {window_top, window_top + window_height, window_top + window_height, page, 0, rect, parent};
    state->fragmentainers = &local; state->fragmentainer_count = 1; state->publish_role = role;
    LayoutViewNode* before = parent->last_child;
    *status = paged_publish_block(state, view, origin_x, origin_y, nullptr, false);
    state->fragmentainers = saved; state->fragmentainer_count = saved_count; state->publish_role = saved_role;
    LayoutViewNode* node = parent->last_child;
    return node != before ? node : nullptr;
}

static TypesetStatus paged_publish_fixed(PagedFlowState* state, LayoutContext* lycon) {
    if (!state->fixed_boxes) return TYPESET_OK;
    TypesetStatus status = TYPESET_OK;
    ViewBlock* viewport = paged_flow_root(state);
    UiContext* ui = lycon->ui_context;
    CssEngine* engine = (CssEngine*)lycon->doc->services.cached_css_engine;
    float saved_width = viewport->width, saved_height = viewport->height;
    float saved_ui_width = ui->viewport_width, saved_ui_height = ui->viewport_height;
    float saved_layout_width = lycon->width, saved_layout_height = lycon->height;
    double saved_css_width = engine ? engine->context.viewport_width : 0.0;
    double saved_css_height = engine ? engine->context.viewport_height : 0.0;
    float width = saved_ui_width, height = saved_ui_height;
    state->publishing_fixed = true;
    for (size_t i = 0; status == TYPESET_OK && i < state->tree->model->page_count; i++) {
        ViewPageBox* page = state->tree->model->pages.get()[i];
        RdtLogicalRect area = page->style->content_rect;
        bool changed = fabsf(area.width - width) > kPagedFlowEpsilon || fabsf(area.height - height) > kPagedFlowEpsilon;
        if (changed) {
            // fixed percentages and viewport queries use this sheet's containing block.
            viewport->width = ui->viewport_width = lycon->width = area.width;
            viewport->height = ui->viewport_height = lycon->height = area.height;
            if (engine) css_engine_set_viewport(engine, area.width, area.height);
        }
        for (PagedFixedBox* fixed = state->fixed_boxes; status == TYPESET_OK && fixed; fixed = fixed->next) {
            if (changed) {
                view_geometry_walk_dom_tree(fixed->block, paged_flow_forget_font_source, state->composition);
                view_pool_reset_retained_subtree(lycon->doc->view_tree, fixed->block);
                radiant_recascade_document_styles(lycon->doc, fixed->block);
                paged_detached_layout(state, lycon, fixed->block, area.width, area.height);
            }
            paged_detached_publish(state, fixed->block, page->style->content_rect, &page->node,
                VIEW_FRAGMENT_STATIC, page, &status, fixed->origin_x, fixed->origin_y);
        }
        width = area.width; height = area.height;
    }
    viewport->width = saved_width; viewport->height = saved_height;
    ui->viewport_width = saved_ui_width; ui->viewport_height = saved_ui_height;
    lycon->width = saved_layout_width; lycon->height = saved_layout_height;
    if (engine) css_engine_set_viewport(engine, saved_css_width, saved_css_height);
    state->publishing_fixed = false;
    return status;
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

// a margin box's material, laid out by the browsing engine in a containing block
// of `width`: a running element (CSS GCPM 3 §2.1) or generated content. Running
// elements left the flow, so their views are free to hold the last margin box's
// layout (the DOM view is a workspace, §3.4)
static TypesetStatus paged_margin_intrinsic(PagedFlowState* state, LayoutContext* lycon, DomElement* element,
        bool vertical, float fixed, PagedIntrinsic* result) {
    *result = {};
    if (!element) return TYPESET_OK;
    result->generated = true;
    if (vertical) {
        if (fixed > 0.0f) result->minimum = result->maximum = paged_detached_layout(state, lycon, element, fixed);
        return TYPESET_OK;
    }
    IntrinsicSizes sizes = layout_measure_intrinsic_widths(lycon, element);
    result->minimum = sizes.min_content; result->maximum = sizes.max_content;
    return TYPESET_OK;
}

static TypesetStatus paged_margin_group(PagedFlowState* state, LayoutContext* lycon, DomElement** elements,
        const CssPageMarginBox boxes[3], RdtLogicalRect band, bool vertical, RdtLogicalRect* rects) {
    float available = vertical ? band.height : band.width, fixed = vertical ? band.width : band.height;
    PagedIntrinsic intrinsic[3] = {};
    for (size_t i = 0; i < 3; i++) {
        TypesetStatus status = paged_margin_intrinsic(state, lycon, elements[boxes[i]], vertical, fixed, &intrinsic[i]);
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

// one declaration of a margin box's generated content
static bool paged_margin_declare(StyleTree* declarations, const char* name, const CssValue* value, Pool* pool) {
    if (!value) return true;
    CssDeclaration* declaration = css_declaration_create(css_property_code_from_name(name), (void*)value, {},
        CSS_ORIGIN_AUTHOR, pool);
    return declaration && style_tree_apply_declaration(declarations, declaration);
}

// page-margin generated content as a synthetic block the browsing engine lays out
// in its margin box (CSS Paged Media 3 §5.3). Its declarations restate the margin
// context's computed text style; the root element is its parent for the rest,
// while it stays out of the root's children and so out of the flow. It borrows
// the declarations, from the composition pool, until its margin box is published
static DomElement* paged_margin_element(PagedFlowState* state, size_t box, ViewCssStyle* context, CssEnum align,
        const char* text) {
    ViewTreeModel* model = state->tree->model;
    DomElement* element = model->margin_elements[box];
    if (!element) {
        element = DomElement::create(model->document, "::margin", nullptr);
        if (!element) return nullptr;
        model->margin_elements[box] = lam::up(element);
    }
    element->parent = lam::up((DomNode*)model->document->root.get());
    element->display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
    Pool* pool = state->composition->pool;
    StyleTree* declarations = style_tree_create(pool);
    if (!declarations) return nullptr;
    static const char* const computed[] = {"font-family", "font-size", "font-weight", "font-style",
        "letter-spacing", "word-spacing", "line-height", "color"};
    for (const char* name : computed)
        if (!paged_margin_declare(declarations, name, view_css_computed_property(state->tree, context, name), pool)) return nullptr;
    const CssEnumInfo* align_name = css_enum_info(align);
    const CssEnumInfo* white_space = css_enum_info(context->white_space);
    if (align_name && !paged_margin_declare(declarations, "text-align", css_value_create_keyword(pool, align_name->name), pool)) return nullptr;
    if (white_space && !paged_margin_declare(declarations, "white-space", css_value_create_keyword(pool, white_space->name), pool)) return nullptr;
    dom_element_borrow_specified_style(element, declarations);
    return layout_generated_text(element, text) ? element : nullptr;
}

// the generated boxes of one page's margin boxes give their declarations back on every path
struct PagedMarginBorrows {
    PagedComposition* composition = nullptr;
    DomElement* elements[CSS_PAGE_MARGIN_BOX_COUNT] = {};
    ~PagedMarginBorrows() {
        for (DomElement* element : elements) if (element) {
            // detached margin views borrow their declarations' strings; retire those props before returning the borrow
            view_geometry_walk_dom_tree(element, paged_flow_forget_font_source, composition);
            view_pool_reset_retained_subtree(element->doc->view_tree, element);
            dom_element_borrow_specified_style(element, nullptr);
        }
    }
};

static TypesetStatus paged_margin_layout(PagedFlowState* state, LayoutContext* lycon, ViewPageBox* page) {
    ViewPageStyle* page_style = page->style;
    if (!page_style) return TYPESET_OK;
    ViewTree* tree = state->tree;
    PagedComposition* composition = state->composition;
    DomElement* elements[CSS_PAGE_MARGIN_BOX_COUNT] = {};
    PagedMarginBorrows borrows;
    borrows.composition = composition;
    RdtLogicalRect rects[CSS_PAGE_MARGIN_BOX_COUNT] = {};
    ViewCssStyle* base = composition->root_style;
    for (size_t i = 0; i < CSS_PAGE_MARGIN_BOX_COUNT; i++) {
        const CssDeclaration* declaration = page_style->margin_content[i];
        ViewCssStyle* context = page_style->margin_style[i];
        const CssValue* content = declaration ? view_css_resolve_value(tree, context, declaration->value) : nullptr;
        if (!content || css_value_is_none(content) || (content->type == CSS_VALUE_TYPE_KEYWORD && content->data.keyword == CSS_VALUE_NORMAL)) continue;
        PagedContentBinding binding = {tree, composition, page, context};
        if (content->type == CSS_VALUE_TYPE_FUNCTION && css_function_name_is(content->data.function, "element")) {
            const TypesetMark* mark = nullptr;
            if (!paged_content_mark(&binding, content->data.function, TYPESET_MARK_RUNNING, &mark))
                return paged_failure(composition, TYPESET_INVALID, base->source, page->page_number, "invalid running-element selection");
            if (mark) {
                if (!mark->value || mark->value->provider != tree->model->tree_id) return TYPESET_STALE;
                elements[i] = ((const PagedRunningRecord*)mark->value.get())->element;
            }
            continue;
        }
        char* owned = nullptr; size_t length = 0;
        TypesetStatus content_status = paged_content_text(tree, composition, context, page, content, &owned, &length);
        if (content_status != TYPESET_OK) return paged_failure(composition, content_status,
            base->source, page->page_number, "unresolved page-margin generated content");
        static const CssEnum alignments[CSS_PAGE_MARGIN_BOX_COUNT] = {
            CSS_VALUE_RIGHT, CSS_VALUE_LEFT, CSS_VALUE_CENTER, CSS_VALUE_RIGHT, CSS_VALUE_LEFT,
            CSS_VALUE_CENTER, CSS_VALUE_CENTER, CSS_VALUE_CENTER, CSS_VALUE_LEFT, CSS_VALUE_RIGHT,
            CSS_VALUE_CENTER, CSS_VALUE_LEFT, CSS_VALUE_RIGHT, CSS_VALUE_CENTER, CSS_VALUE_CENTER, CSS_VALUE_CENTER};
        CssEnum align = page_style->margin_align[i] ? context->text_align : alignments[i];
        elements[i] = borrows.elements[i] = paged_margin_element(state, i, context, align, owned);
        if (!elements[i]) return TYPESET_OUT_OF_MEMORY;
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
        TypesetStatus status = paged_margin_group(state, lycon, elements, groups[i], bands[i], i >= 2, rects);
        if (status != TYPESET_OK) return status;
    }
    for (size_t i = 0; i < CSS_PAGE_MARGIN_BOX_COUNT; i++) {
        DomElement* element = elements[i];
        if (!element) continue;
        RdtLogicalRect rect = rects[i];
        LayoutViewNode* box = view_tree_fragment_append(tree, &page->node, base->source, rect);
        if (!box) return TYPESET_OUT_OF_MEMORY;
        box->role = VIEW_FRAGMENT_MARGIN; box->generated = true;
        box->computed_style = page_style->margin_style[i]; page->margin_boxes[i] = lam::up(box);
        const CssValue* overflow = page_style->margin_overflow[i] ? page_style->margin_overflow[i]->value : nullptr;
        box->clip_content = overflow && overflow->type == CSS_VALUE_TYPE_KEYWORD &&
            (overflow->data.keyword == CSS_VALUE_HIDDEN || overflow->data.keyword == CSS_VALUE_CLIP);
        if (rect.width <= 0.0f) continue;
        // the material is centred vertically in its margin box
        float used = paged_detached_layout(state, lycon, element, rect.width);
        TypesetStatus status;
        paged_detached_publish(state, element, {rect.x, rect.y + (rect.height - used) * 0.5f, rect.width, used}, box,
            borrows.elements[i] ? VIEW_FRAGMENT_MARGIN : VIEW_FRAGMENT_RUNNING, page, &status);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
}

// a footnote or page float takes its slot: material laid out at another width
// lays out again at the slot's, and its box is the occurrence of the element
static TypesetStatus paged_publish_anchor(PagedFlowState* state, LayoutContext* lycon, PagedFlowAnchor* anchor,
        RdtLogicalRect slot, LayoutViewNode* parent, ViewFragmentRole role, ViewPageBox* page, float offset = 0.0f) {
    DomElement* element = anchor->element;
    bool folio = paged_folios_fill(state, element, page);
    if (lycon && (folio || fabsf(slot.width - anchor->width) > kPagedFlowEpsilon)) {
        log_debug("[PAGED_FLOW] %s lays out again at width %.3f (measured at %.3f)", element->source_loc(), slot.width, anchor->width);
        auto counters = lycon->counter_context;
        lycon->counter_context = nullptr;
        paged_detached_layout(state, lycon, element, slot.width,
            anchor->kind == PAGED_ANCHOR_NOTE ? NAN : state->fragmentainers[anchor->fragmentainer].rect.height);
        lycon->counter_context = counters;
        anchor->width = slot.width; anchor->view_type = element->view_type;
    }
    // the element left the flow: its view takes its type only while it publishes
    element->view_type = (ViewType)anchor->view_type;
    TypesetStatus status;
    bool continued = anchor->continuation != nullptr;
    LayoutViewNode* box = paged_detached_publish(state, element, slot, parent, role, page, &status,
        0.0f, 0.0f, offset, continued ? slot.height : INFINITY);
    element->view_type = RDT_VIEW_NONE;
    if (box) box->paint_box = true;
    if (box && continued) {
        box->first_fragment = offset <= kPagedFlowEpsilon;
        box->last_fragment = offset + slot.height >= anchor->full_height - kPagedFlowEpsilon;
    }
    // a footnote or float that is a reference target is on the page it was placed on
    PagedMarkSource* mark = page ? paged_flow_mark(state, element, false) : nullptr;
    if (mark && mark->target) {
        if (!mark->target_page) mark->target_page = page->page_number;
        mark->last_target_page = page->page_number;
    }
    return status;
}

// each fragmentainer's page floats, at its top and above its footnote area, and its
// footnote area at its end holding the notes in order (CSS GCPM 3 §2.4; the area's
// box comes from the page's @footnote context, as the item engine painted it)
static TypesetStatus paged_publish_anchors(PagedFlowState* state, LayoutContext* lycon) {
    TypesetStatus status = TYPESET_OK;
    for (size_t index = 0; status == TYPESET_OK && index < state->fragmentainer_count; index++) {
        PagedFragmentainer& fragmentainer = state->fragmentainers[index];
        if (!fragmentainer.note_count && !fragmentainer.top_floats && !fragmentainer.bottom_floats) continue;
        ViewPageBox* page = fragmentainer.page;
        LayoutViewNode* page_node = page ? &page->node : state->tree->model->root.get();
        RdtLogicalRect body = fragmentainer.rect;
        body.y -= fragmentainer.float_top;
        const PagedNoteArea* area = fragmentainer.note_count ? paged_fragmentainer_area(state, index) : nullptr;
        float notes = area ? paged_note_extent(area, fragmentainer.notes) : 0.0f;
        float top_y = body.y, bottom_y = body.y + body.height - notes - fragmentainer.float_bottom;
        LayoutViewNode* area_box = nullptr;
        RdtLogicalRect note = {};
        if (area) {
            RdtLogicalRect rect = {body.x + area->margin[3], body.y + body.height - notes + area->margin[0],
                area->width, notes - area->margin[0] - area->margin[2]};
            area_box = view_tree_fragment_append(state->tree, page_node, nullptr, rect);
            if (!area_box) return TYPESET_OUT_OF_MEMORY;
            // anonymous page furniture belongs to the edition, never to the DOM
            if (page && page->style) area_box->computed_style = page->style->footnote.computed_style;
            status = paged_boundary_publish(state->tree, area_box, *area);
            if (status != TYPESET_OK) return status;
            for (size_t i = 0; i < 4; i++) area_box->computed_boundary->margin.values[i] = area->margin[i];
            area_box->role = VIEW_FRAGMENT_NOTE;
            area_box->paint_box = area_box->generated = area_box->first_fragment = area_box->last_fragment = true;
            note = {rect.x + area->edges[3], rect.y + area->edges[0], area->width - area->edges[1] - area->edges[3], 0.0f};
        }
        for (size_t i = 0; status == TYPESET_OK && i < state->anchor_count; i++) {
            PagedFlowAnchor* anchor = &state->anchors[i];
            float offset = 0.0f, height = anchor->height;
            if (anchor->fragmentainer != index) {
                PagedNotePiece* piece = anchor->continuation;
                while (piece && piece->fragmentainer != index) piece = piece->next;
                if (!piece) continue;
                offset = piece->offset; height = piece->height;
            }
            if (anchor->kind == PAGED_ANCHOR_NOTE) {
                status = paged_publish_anchor(state, lycon, anchor, {note.x, note.y, note.width, height},
                    area_box, VIEW_FRAGMENT_NOTE, page, offset);
                note.y += height;
            } else {
                float* y = anchor->kind == PAGED_ANCHOR_TOP ? &top_y : &bottom_y;
                status = paged_publish_anchor(state, lycon, anchor, {body.x, *y, body.width, anchor->height},
                    page_node, VIEW_FRAGMENT_FLOAT, page);
                *y += anchor->height;
            }
        }
    }
    return status;
}

// a page's edge regions: each region box with its decoration and clip, holding
// the static content its sequence binds to the region, laid out by the browsing
// engine at the region's content width with the page's folios and aligned by the
// region's display alignment. The content left the flow, so its views hold the
// last page's layout (§3.4).
static TypesetStatus paged_flow_static_layout(PagedFlowState* state, LayoutContext* lycon, ViewPageBox* page) {
    ViewTree* tree = state->tree;
    const RadiantPageSequence* sequence = page->sequence;
    for (size_t i = 0; i < RADIANT_REGION_EDGE_COUNT; i++) {
        const RadiantPageRegion* region = page->style->edge_regions[i];
        RdtLogicalRect rect = page->style->edge_rects[i];
        if (!region || rect.width <= 0.0f || rect.height <= 0.0f) continue;
        LayoutViewNode* box = view_tree_fragment_append(tree, &page->node, region->source.address, rect);
        if (!box) return TYPESET_OUT_OF_MEMORY;
        box->role = VIEW_FRAGMENT_STATIC; box->generated = box->paint_box = true;
        box->computed_style = lam::up(page->style->edge_style[i]);
        box->clip_content = page->style->edge_clip[i];
        page->static_boxes[i] = lam::up(box);
        const PagedBoxEdges& edges = page->style->edge_boxes[i];
        for (size_t j = 0; j < 4; j++) box->clip_inset[j] = edges.border.width.values[j];
        TypesetStatus status = paged_boundary_publish(tree, box, edges);
        if (status != TYPESET_OK) return status;
        const RadiantPageFlowBinding* binding = sequence ? sequence->static_content : nullptr;
        while (binding && strcmp(binding->region_name, region->name)) binding = binding->next;
        DomElement* content = binding && binding->source.address && binding->source.address->is_element()
            ? binding->source.address->as_element() : nullptr;
        if (!content) continue;
        float width = rect.width - edges.edges[1] - edges.edges[3];
        float height = rect.height - edges.edges[0] - edges.edges[2];
        if (width <= 0.0f || height < 0.0f) return TYPESET_UNPLACEABLE;
        paged_folios_fill(state, content, page);
        float used = paged_detached_layout(state, lycon, content, width);
        float y = rect.y + edges.edges[0];
        if (region->align != RADIANT_REGION_ALIGN_BEFORE)
            y += (height - used) * (region->align == RADIANT_REGION_ALIGN_CENTER ? 0.5f : 1.0f);
        paged_detached_publish(state, content, {rect.x + edges.edges[3], y, width, used}, box, VIEW_FRAGMENT_STATIC, page, &status);
        // static content left the flow: its view takes its type only while it publishes
        content->view_type = RDT_VIEW_NONE;
        if (status != TYPESET_OK) return paged_failure(state->composition, status, content, page->page_number,
            "static region content could not be composed");
    }
    return TYPESET_OK;
}

// a fixed-page document (imported PDF sheets): each fixed page is one page of its
// viewport, whose content the browsing engine lays out at the viewport's size; no
// flow, sequence or margin box takes part (page_document.hpp, RadiantFixedPage)
static TypesetStatus paged_flow_fixed_pages(PagedFlowState* state, LayoutContext* lycon) {
    ViewTree* tree = state->tree;
    PagedComposition* composition = state->composition;
    const RadiantPageDocument* program = tree->model->css->page_document;
    if (tree->model->environment.presentation != VIEW_PRESENTATION_PAGED)
        return paged_failure(composition, TYPESET_INVALID, program->fixed_source.address, 0, "fixed page controls require paged presentation");
    if (program->fixed_count > composition->options.max_pages)
        return paged_failure(composition, TYPESET_BUDGET_EXHAUSTED, program->fixed_source.address, 0, "fixed page sequence exceeds its page budget");
    for (const RadiantFixedPage* fixed = program->fixed_pages; fixed; fixed = fixed->next) {
        DomElement* source = fixed->source.address ? fixed->source.address->as_element() : nullptr;
        ViewPageBox* page = view_tree_page_append(tree, fixed->viewport.width, fixed->viewport.height, fixed->viewport,
            paged_side(&state->composer, tree->model->page_count));
        if (!source || !page) return TYPESET_OUT_OF_MEMORY;
        page->fixed = lam::up(fixed);
        page->folio = fixed->geometry.source_page;
        page->sequence_page = page->page_number;
        if (fixed->label) page->label = lam::up(fixed->label);
        else {
            char label[32]; snprintf(label, sizeof(label), "%u", fixed->geometry.source_page);
            page->label = lam::up(pool_strdup(composition->pool, label));
        }
        if (!page->label) return TYPESET_OUT_OF_MEMORY;
        float used = paged_detached_layout(state, lycon, source, fixed->viewport.width, fixed->viewport.height);
        TypesetStatus status;
        paged_detached_publish(state, source, {fixed->viewport.x, fixed->viewport.y, fixed->viewport.width,
            fmaxf(used, fixed->viewport.height)}, &page->node, VIEW_FRAGMENT_BODY, page, &status);
        source->view_type = RDT_VIEW_NONE;
        if (status != TYPESET_OK) return paged_failure(composition, status, source, page->page_number, "fixed page content could not be composed");
    }
    return TYPESET_OK;
}

// each page of a sequence with a master program carries the master it selects as
// a terminal or ordinary page. Which page ends a sequence is known once the flow
// is composed, so a page that took the other kind is recorded and the whole-document
// pass repeats with it, as references do (the convergence signature holds the page names)
static TypesetStatus paged_flow_terminals(PagedFlowState* state) {
    ViewTree* tree = state->tree;
    PagedComposition* composition = state->composition;
    PagedReferenceSession* references = composition->references;
    for (size_t i = 0; i < tree->model->page_count; ) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        const RadiantPageSequence* sequence = page->sequence;
        uint32_t last = page->page_number;
        while (last < tree->model->page_count && tree->model->pages.get()[last]->sequence.get() == sequence) last++;
        bool mismatch = false;
        for (size_t at = i; sequence && sequence->master_program && at < last; at++) {
            const ViewPageBox* candidate = tree->model->pages.get()[at];
            const RadiantPageRule* selected = radiant_page_master_select(sequence, candidate->sequence_page,
                candidate->folio, candidate->blank, at + 1 == last);
            mismatch |= !selected || strcmp(selected->name, candidate->name);
        }
        i = last;
        if (!mismatch) continue;
        // update once per sequence: a later page must not see this pass's newly recorded extent as old evidence.
        if (!references || paged_flow_terminal_page(state, sequence) == last)
            return paged_failure(composition, TYPESET_UNPLACEABLE, sequence->source.address, page->page_number,
                "sequence material cannot satisfy its terminal master alternatives");
        size_t at = 0;
        while (at < references->terminal_count && references->terminals[at].sequence != sequence->source.address) at++;
        if (at == references->terminal_count && !lam::pool_grow_array(references->pool, &references->terminals,
            &references->terminal_capacity, references->terminal_count + 1, 4)) return TYPESET_OUT_OF_MEMORY;
        if (at == references->terminal_count) references->terminal_count++;
        log_debug("[PAGED_FLOW] terminal extent changes from %u to %u", paged_flow_terminal_page(state, sequence), last);
        const DomNode* tail = nullptr;
        for (PagedFlowParagraph* paragraph = state->paragraphs; paragraph; paragraph = paragraph->next)
            if (paragraph->count && paged_flow_sequence(state, paragraph->block) == sequence &&
                paragraph->block->height <= state->fragmentainers[paragraph->lines[paragraph->count - 1].fragmentainer].rect.height + kPagedFlowEpsilon)
                tail = paragraph->block;
        references->terminals[at] = {sequence->source.address, last, tail};
        composition->reference_used = true;
        composition->reference_source = sequence->source;
    }
    return TYPESET_OK;
}

// the page tail once the flow settled: publication, regions, sequence ends,
// page counters, then each page's margin boxes and static regions
static TypesetStatus paged_flow_compose_pages(PagedFlowState* state, LayoutContext* lycon) {
    ViewTree* tree = state->tree;
    PagedComposition* composition = state->composition;
    PagedPageCursor* composer = &state->composer;
    TypesetStatus status = TYPESET_OK;
    if (tree->model->css->page_document->fixed_count) return paged_flow_fixed_pages(state, lycon);
    // a document without flow content still has its first page
    if (!state->fragmentainer_count) status = paged_fragmentainer_append(state, true);
    if (status == TYPESET_OK) status = paged_anchors_finish(state);
    if (status == TYPESET_OK) status = paged_flow_widths_capture(state);
    if (status == TYPESET_OK) status = paged_flow_publish(state, lycon);
    if (status == TYPESET_OK) status = paged_sequence_end(composer, nullptr);
    if (status == TYPESET_OK) status = paged_flow_terminals(state);
    if (status == TYPESET_OK) status = paged_page_counters(tree, composition);
    for (size_t i = 0; status == TYPESET_OK && i < tree->model->page_count; i++) {
        status = paged_margin_layout(state, lycon, tree->model->pages.get()[i]);
        if (status == TYPESET_OK) status = paged_flow_static_layout(state, lycon, tree->model->pages.get()[i]);
    }
    if (status == TYPESET_OK) status = paged_publish_fixed(state, lycon);
    return status;
}

void paged_flow_compose(LayoutContext* lycon) {
    PagedFlowState* state = lycon->paged;
    if (state->status != TYPESET_OK) return;
    ViewTree* tree = state->tree;
    // the browsing layout materializes generated boxes and recascades, which
    // advances the document's mutation epoch; the edition is built from that
    // settled DOM, so it adopts the epoch before publication (D4.5.1v4)
    tree->model->source_epoch = tree->model->document->mutation_epoch;
    if (state->pass) { view_tree_model_accept(tree, state->pass); state->pass = nullptr; }
    // running elements and margin content lay out outside the flow's hooks
    lycon->paged = nullptr;
    TypesetStatus status = paged_flow_compose_pages(state, lycon);
    lycon->paged = lam::up(state);
    if (state->status == TYPESET_OK) state->status = status;
    state->composed = true;
}

// ----- the pass: lay the flow out with the browsing engine, then compose pages -----

static TypesetStatus paged_flow_pass(ViewTree* tree, PagedComposition* composition) {
    DomDocument* document = tree->model->document;
    UiContext* ui = tree->model->environment.ui_context;
    if (!ui) return paged_failure(composition, TYPESET_INVALID, document->root, 0,
        "page flow layout needs the host UI context in the edition environment");
    PagedFlowState* state = (PagedFlowState*)pool_calloc(composition->pool, sizeof(PagedFlowState));
    if (!state) return TYPESET_OUT_OF_MEMORY;
    state->tree = tree; state->composition = composition;
    state->composer.tree = tree; state->composer.composition = composition;
    ViewModelCheckpoint* edition = view_tree_model_checkpoint(tree);
    if (!edition) return TYPESET_OUT_OF_MEMORY;
    PagedPageCursor* composer = &state->composer;
    // a native document opens with its first page sequence and that sequence's master
    const RadiantPageSequence* opening = tree->model->css->page_document->sequences;
    if (opening && !opening->implicit) { composer->sequence = opening; composer->page_name = opening->master_reference; }
    // the first page's body geometry is the flow's viewport; its page name comes
    // from the first flow content so an opening named page needs no restyle
    for (DomElement* element = document->root; element && !composer->page_name; ) {
        composer->page_name = paged_flow_page_name(state, element);
        DomNode* child = element->first_child;
        while (child && !child->is_element()) child = child->next_sibling;
        element = child ? child->as_element() : nullptr;
    }
    RdtLogicalRect first = {0.0f, 0.0f, tree->model->environment.viewport_width, tree->model->environment.viewport_height};
    TypesetStatus status = TYPESET_OK;
    if (tree->model->environment.presentation == VIEW_PRESENTATION_PAGED) {
        PagedPageSpec spec = {};
        ViewPageStyle style = {};
        status = paged_page_spec(composer, 0, false, &spec);
        if (status == TYPESET_OK) status = paged_page_style(composer, spec, 1, paged_side(composer, 0), false, &style);
        if (status == TYPESET_OK && !view_css_page_column(&style, 0, &first))
            status = paged_failure(composition, TYPESET_INVALID, document->root, 1, "the first page has no body area");
    }
    if (status == TYPESET_OK) {
        // the flow is laid out at the page body size; the host viewport and any
        // viewport meta are restored afterwards
        float saved_viewport_width = ui->viewport_width, saved_viewport_height = ui->viewport_height;
        int saved_meta_width = document->viewport.width, saved_meta_height = document->viewport.height;
        ui->viewport_width = first.width; ui->viewport_height = first.height;
        document->viewport.width = document->viewport.height = 0;
        CssEngine* engine = (CssEngine*)document->services.cached_css_engine;
        bool saved_print = engine && engine->context.print_media;
        bool print = tree->model->environment.print_media;
        if (engine && saved_print != print) {
            engine->context.print_media = print;
            radiant_recascade_document_styles(document, document->root);
        }
        // paged_flow_pass_begin opens the checkpoint of each pass and the hooks
        // append the pages; paged_flow_compose accepts the settled pass's
        // checkpoint and composes the pages before the layout context closes
        layout_html_doc(ui, document, false, state);
        ui->viewport_width = saved_viewport_width; ui->viewport_height = saved_viewport_height;
        document->viewport.width = saved_meta_width; document->viewport.height = saved_meta_height;
        if (engine && saved_print != print) engine->context.print_media = saved_print;
        status = state->status;
        if (status == TYPESET_OK && !state->composed)
            status = paged_flow_fail(state, TYPESET_INVALID, document->root, "the page flow layout ended before its pages were composed");
    }
    composition->diagnostic.relaxed_line_minima = state->relaxed_minima;
    composition->diagnostic.relaxed_avoidance = state->relaxed_avoidance;
    if (status == TYPESET_OK && tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS) {
        ViewBlock* root = document->view_tree ? lam::view_as_block(document->view_tree->root.get()) : nullptr;
        tree->model->root->rect = {0.0f, 0.0f, tree->model->environment.viewport_width, root ? root->height : 0.0f};
    }
    if (status != TYPESET_OK) log_debug("[PAGED_FLOW] composition failed with status %u after %zu pages", (unsigned)status, tree->model->page_count);
    if (status != TYPESET_OK && composition->diagnostic.status == TYPESET_OK)
        paged_failure(composition, status, document->root, composer->page ? composer->page->page_number : 0,
            "page flow could not be composed");
    if (state->pass) { view_tree_model_restore(tree, state->pass); state->pass = nullptr; }
    if (status == TYPESET_OK) return view_tree_model_accept(tree, edition) ? TYPESET_OK : TYPESET_STALE;
    PagedLayoutDiagnostic failure = composition->diagnostic;
    view_tree_model_restore(tree, edition);
    composition->diagnostic = failure;
    return status;
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
    DomElement* root = tree->model->document->root;
    ViewCssStyle* style = view_css_resolve(tree, root);
    if (!style) return TYPESET_OUT_OF_MEMORY;
    composition->root_style = style;
    const RadiantPageDiagnostic& page_diagnostic = tree->model->css->page_document->diagnostic;
    if (page_diagnostic.status != VIEW_MODEL_OK) return paged_failure(composition,
        page_diagnostic.status == VIEW_MODEL_OUT_OF_MEMORY ? TYPESET_OUT_OF_MEMORY : TYPESET_INVALID,
        page_diagnostic.source.address, 0, page_diagnostic.reason);
    composition->counters = counter_context_create(tree->layout_pass_arena);
    if (!composition->counters) return TYPESET_OUT_OF_MEMORY;
    // page contexts inherit the root counter snapshot; source counters use LayoutCounterScope.
    const CssValue* values[] = {style->counter_reset, style->counter_increment, style->counter_set};
    for (size_t i = 0; i < 3; i++) {
        TypesetStatus status = paged_counter_property(tree, composition, style, composition->counters, values[i], i, true);
        if (status != TYPESET_OK) return status;
    }
    style->counters = lam::up(counter_snapshot_create(composition->counters, tree->model->css->pool));
    if (!style->counters) return TYPESET_OUT_OF_MEMORY;
    // the browsing engine lays out the page flow, and the fixed pages' content (design §1.3)
    return paged_flow_pass(tree, composition);
}

static uint32_t paged_target_page(ViewTree* tree, DomElement* source, bool last = false) {
    PagedMarkSource key = {}; key.source = source;
    PagedMarkSource* mark = (PagedMarkSource*)hashmap_get(tree->model->composition->mark_sources, &key);
    if (mark && mark->target_page) return last ? mark->last_target_page : mark->target_page;
    ViewNodeState* state = view_tree_node_state(tree, source, false);
    uint32_t result = 0;
    for (LayoutViewNode* node = state ? state->first_occurrence.get() : nullptr; node; node = node->next_occurrence) {
        if (node->role != VIEW_FRAGMENT_BODY && node->role != VIEW_FRAGMENT_NOTE && node->role != VIEW_FRAGMENT_FLOAT) continue;
        if (!node->paint_box && !node->glyph_run && !node->image_box) continue;
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

// the text a pseudo-element of the page flow generated in its layout
static const char* paged_flow_pseudo_text(Pool* pool, DomElement* element, bool before) {
    PseudoContentProp* pseudo = element ? element->pseudo.get() : nullptr;
    DomElement* generated = pseudo ? (before ? pseudo->before.get() : pseudo->after.get()) : nullptr;
    DomNode* child = generated ? generated->first_child.get() : nullptr;
    DomText* text = child && child->is_text() ? child->as_text() : nullptr;
    return text && text->text ? pool_dup_n(pool, text->text, text->length) : pool_strdup(pool, "");
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
        // every target is a DOM element the page flow met
        DomNode* node = old->source.address ? dom_node_ref_validate(tree->model->document, old->source) : nullptr;
        if (!node || !node->is_element()) { status = TYPESET_STALE; break; }
        DomElement* source = node->as_element();
        // the page flow recorded a target's counters where its layout met the target
        PagedMarkSource key = {}; key.source = source;
        const PagedMarkSource* mark = (const PagedMarkSource*)hashmap_get(tree->model->composition->mark_sources, &key);
        PagedTargetValue* value = (PagedTargetValue*)pool_calloc(session->pool, sizeof(PagedTargetValue));
        if (!value) { status = TYPESET_OUT_OF_MEMORY; break; }
        value->provider = tree->model->tree_id; value->source = old->source; value->text = old->text;
        value->counters = counter_snapshot_copy(mark && mark->counters ? mark->counters : old->counters, session->pool);
        if (!value->counters) { status = TYPESET_OUT_OF_MEMORY; break; }
        const char** parts[] = {&value->before, &value->after};
        for (size_t j = 0; j < 2 && status == TYPESET_OK; j++) {
            *parts[j] = paged_flow_pseudo_text(session->pool, source, j == 0);
            if (!*parts[j]) status = TYPESET_OUT_OF_MEMORY;
        }
        if (status != TYPESET_OK) break;
        TypesetTarget target = previous;
        target.page_number = paged_target_page(tree, source);
        target.last_page_number = paged_target_page(tree, source, true);
        target.binding = lam::up((const TypesetRecord*)value);
        target.source.generation = tree->layout_generation; target.value = target.binding;
        for (size_t area = 0; area < 3; area++) for (size_t edge = 0; edge < 2; edge++) {
            uint32_t number = mark ? mark->generated_pages[area][edge] : 0;
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
    for (size_t i = 0; i < session->width_count; i++) {
        const PagedFlowWidth& width = session->widths[i];
        paged_signature_value(signature, width.containing_width);
        paged_signature_value(signature, width.count);
        for (size_t line = 0; line < width.count; line++) paged_signature_value(signature, width.lines[line]);
    }
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
    if (!tree || !tree->model || !options || !options->max_pages || !options->max_nodes ||
        !options->max_depth || !options->max_reference_passes || !options->max_container_passes ||
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
            if (status == TYPESET_OK && composition->reference_used)
                status = paged_reference_observe(tree, &session, &settled);
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
        if (status == TYPESET_OK && !settled) {
            uint32_t generation = tree->layout_generation;
            tree->reset_retained();
            if (tree->layout_generation == generation || tree->model->node_count != 1) status = TYPESET_OUT_OF_MEMORY;
        }
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

const TypesetMark* layout_secondary_mark(ViewTree* tree, TypesetMarkKind kind, const char* name,
        uint32_t page_number, TypesetMarkSelection selection) {
    if (!view_tree_model_source_valid(tree) || !tree->model->committed) return nullptr;
    if (tree->model->page_instances) tree = view_tree_page_content_owner(tree);
    if (!tree || !tree->model->composition) return nullptr;
    return typeset_mark_select(&tree->model->composition->marks, kind, name, page_number, selection);
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

static bool paged_native_boundary(void* context, const BoundaryProp* boundary, RdtLogicalRect rect) {
    ViewBlock box = {};
    box.width = rect.width; box.height = rect.height;
    box.bound = lam::view_prop(const_cast<BoundaryProp*>(boundary));
    return render_paint_boundary_box((PaintList*)context, nullptr, &box,
        {rect.x, rect.y, rect.width, rect.height}, RENDER_EXPORT_TARGET_SVG);
}
static bool paged_native_fill(void* context, Color color, RdtLogicalRect rect) {
    paint_fill_rect((PaintList*)context, rect.x, rect.y, rect.width, rect.height, color);
    return true;
}
static bool paged_native_clip(void* context, const RdtLogicalRect* rect, const float* inset) {
    if (rect) return paged_paint_clip((PaintList*)context, *rect, inset);
    paint_pop_clip((PaintList*)context);
    return true;
}
static bool paged_native_glyph(void* context, const LayoutViewNode*, const PaintGlyphRun* run) {
    paint_glyph_run((PaintList*)context, run);
    return true;
}
static bool paged_native_image(void* context, const LayoutViewNode*, const PaintImageBox* image) {
    return render_paint_image_box((PaintList*)context, image);
}
static bool paged_native_paint(void* context, const PaintList* source) {
    return paint_list_borrow_commands((PaintList*)context, source);
}
static SecondaryViewVisitor paged_native_visitor(PaintList* paint, bool pdf = false) {
    return {paint, nullptr, paged_native_boundary, paged_native_fill, paged_native_clip,
        paged_native_glyph, paged_native_image, paged_native_paint, pdf};
}





static bool paged_visit_node(LayoutViewNode* node, const SecondaryViewVisitor* visitor) {
    if (node->table_range && node->table_range->missing) return true;
    if (visitor->fragment && !visitor->fragment(visitor->context, node, true)) return false;
    const PaintList* before = visitor->pdf && node->paint_before_pdf ? node->paint_before_pdf.get() : node->paint_before.get();
    if (!node->hidden && before) {
        if (!visitor->paint || !visitor->paint(visitor->context, before)) return false;
    } else if (!node->hidden && node->paint_box && node->computed_boundary) {
        const float* outset = node->border_outset;
        RdtLogicalRect box = {node->rect.x - outset[3], node->rect.y - outset[0],
            node->rect.width + outset[1] + outset[3], node->rect.height + outset[0] + outset[2]};
        if (!visitor->boundary(visitor->context, node->computed_boundary, box)) return false;
    }
    if (node->clip_content && !visitor->clip(visitor->context, &node->rect, node->clip_inset)) return false;
    if (!node->hidden) {
        if (node->glyph_run && !visitor->glyph(visitor->context, node, node->glyph_run)) return false;
        if (node->paint_after && (!visitor->paint || !visitor->paint(visitor->context, node->paint_after))) return false;
        if (node->image_box && !visitor->image(visitor->context, node, node->image_box)) return false;
    }
    for (LayoutViewNode* child = node->first_child; child; child = child->next_sibling)
        if (!paged_visit_node(child, visitor)) return false;
    if (node->clip_content && !visitor->clip(visitor->context, nullptr, nullptr)) return false;
    return !visitor->fragment || visitor->fragment(visitor->context, node, false);
}

static bool paged_paint_column_rules(const ViewPageBox* page, const SecondaryViewVisitor* visitor) {
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
    if (style->body_clip && !visitor->clip(visitor->context, &style->body_rect, style->body_box.border.width.values)) return false;
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
        if (!visitor->boundary(visitor->context, &boundary, {x, top, width, bottom - top})) return false;
    }
    if (style->body_clip && !visitor->clip(visitor->context, nullptr, nullptr)) return false;
    return true;
}

bool layout_secondary_visit_page(ViewTree* tree, const ViewPageBox* page, const SecondaryViewVisitor* visitor) {
    if (!visitor || !visitor->boundary || !visitor->fill || !visitor->clip || !visitor->glyph || !visitor->image) return false;
    page = view_tree_page_material(tree, page);
    if (!page) return false;
    Color white = {.r = 255, .g = 255, .b = 255, .a = 255};
    if (!visitor->fill(visitor->context, page->style ? page->style->background : white, page->node.rect)) return false;
    if (!visitor->clip(visitor->context, &page->node.rect, nullptr)) return false;
    if (!page->blank && page->style && page->style->body_style) {
        // the boundary painter may normalize radii; keep immutable edition geometry untouched.
        ViewCssBoxEdges box = page->style->body_box;
        BoundaryProp boundary = {}; boundary.border = lam::own(&box.border); boundary.background = lam::own(&box.background);
        const RdtLogicalRect& rect = page->style->body_rect;
        if (!visitor->boundary(visitor->context, &boundary, rect)) return false;
    }
    if (!page->blank && !paged_paint_column_rules(page, visitor)) return false;
    for (LayoutViewNode* child = page->node.first_child; child; child = child->next_sibling) {
        if (page->blank && child->role != VIEW_FRAGMENT_MARGIN && child->role != VIEW_FRAGMENT_STATIC) continue;
        bool body_clip = page->style && page->style->body_clip &&
            (child->role == VIEW_FRAGMENT_BODY || child->role == VIEW_FRAGMENT_NOTE || child->role == VIEW_FRAGMENT_FLOAT);
        if (body_clip && !visitor->clip(visitor->context, &page->style->body_rect, page->style->body_box.border.width.values)) return false;
        if (!paged_visit_node(child, visitor)) return false;
        if (body_clip && !visitor->clip(visitor->context, nullptr, nullptr)) return false;
    }
    return visitor->clip(visitor->context, nullptr, nullptr);
}

bool layout_secondary_paint_page(ViewTree* tree, const ViewPageBox* page, PaintList* paint, bool pdf) {
    if (!paint) return false;
    SecondaryViewVisitor visitor = paged_native_visitor(paint, pdf);
    return layout_secondary_visit_page(tree, page, &visitor);
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
        SecondaryViewVisitor visitor = paged_native_visitor(paint);
        return paged_visit_node(tree->model->root, &visitor);
    }
    // shared placement traversal culls offscreen sheets before recording glyphs and images.
    return view_tree_preview_paint(tree, clip, paged_paint_placement, paint) == VIEW_MODEL_OK;
}
