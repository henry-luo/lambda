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
struct PagedTerminalPage { const DomNode* sequence; uint32_t page; const DomNode* tail; float offset; };
// widths discovered by pagination are replayed through layout, including line wrapping.
struct PagedFlowWidth {
    DomNode* source;
    float containing_width;
    float layout_width;        // the current pass's input, before pagination updates the replay
    const char* master_name;
    bool detached;
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
    TypesetTargetStore native_targets;
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



static TypesetStatus paged_native_sources(Pool* pool, const PagedLayoutOptions* options, TypesetSourceScope* scope) {
    if (!options->native_flow_count) return TYPESET_OK;
    TypesetSourceNamespace* sources = (TypesetSourceNamespace*)pool_alloc(pool,
        options->native_flow_count * sizeof(TypesetSourceNamespace));
    if (!sources) return TYPESET_OUT_OF_MEMORY;
    for (size_t i = 0; i < options->native_flow_count; i++) {
        const TypesetFlowProvider* provider = options->native_flows[i].provider;
        sources[i] = {provider->identity, provider->generation};
    }
    *scope = {sources, options->native_flow_count};
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
    TypesetStatus status = TYPESET_OK;
    auto admit = [&](const auto& value) {
        if (status == TYPESET_OK && value && value->status != VIEW_MODEL_OK)
            status = paged_failure(composition, TYPESET_INVALID, style->source, 0, value->reason);
    };
    admit(style->flow_traits); admit(style->whitespace); admit(style->image_spec);
    admit(style->label_body); admit(style->page_query); admit(style->note_binding);
    return status;
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
    // a wider only alternative can fit the sequence before ordinary pagination grows it.
    if (!composer->terminal_page && spec->ordinal == 1) {
        bool selected_only = false;
        const RadiantPageRule* candidate = radiant_page_master_select(spec->sequence,
            spec->ordinal, spec->folio, blank, true, &selected_only);
        if (selected_only) master = candidate;
    }
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

static bool paged_source_has_own_area(const LayoutViewNode* node) {
    // common fragments keep selected source styles on their generation-owned state.
    const ViewCssStyle* style = node->computed_style ? node->computed_style.get() :
        node->state ? node->state->computed_style.get() : nullptr;
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
    float alignment;           // publication-only body translation; auxiliary areas retain their slots
};
struct PagedNativeLine {
    const PagedNativeFlowBinding* binding;
    TypesetContribution contribution;
    TypesetLineCandidate selected;
    TypesetResume resume;
    TypesetBreak before;
};
struct PagedFlowLine {
    float top, height;         // in the paragraph's inline frame (BlockContext::advance_y)
    size_t fragmentainer;
    int number;                // the block-local line number its text rects carry
    uint32_t anchor_first, anchor_count; // the footnotes and page floats anchored on the line
    PagedNativeLine* native;
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
struct PagedNativeEvent {
    const PagedNativeFlowBinding* binding;
    TypesetContribution contribution;
    size_t line;
    PagedNativeEvent* next;
};
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
    PagedNativeEvent* events;
    PagedNativeEvent* last_event;
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
struct PagedMissingTableSlot { size_t column; float x, width; };
struct PagedMissingTableRow { ViewTableRow* row; size_t first, count; };
// a table broken between rows after layout_table placed it whole (plan P3)
struct PagedFlowTable {
    ViewBlock* table;
    ViewBlock* header;         // the header group that repeats on continuation fragmentainers, or null
    ViewBlock* footer;         // the footer group that repeats before each break, or null
    ViewTableRow* terminal_row; // the body tail before a non-repeated final footer
    float terminal_footer;    // natural final group and trailing grid edge
    float spacing;             // vertical border spacing of the separated model
    float header_offset;       // the common grid's leading inset before its header
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
    PagedMissingTableSlot* missing;
    size_t missing_count, missing_capacity;
    PagedMissingTableRow* missing_rows;
    size_t missing_row_count, missing_row_capacity;
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
    struct SpaceBox { ViewBlock* box; BlockContext* owner; size_t parent; float top; }* space_boxes;
    size_t space_box_count, space_box_capacity, space_open;
    RadiantSpaceSpec* pending_spaces;
    size_t pending_space_count, pending_space_capacity;
    struct SpaceGap {
        ViewBlock* box;
        PagedFlowParagraph* paragraph;
        size_t line;
        RadiantSpaceSpec range;
        float used;
        bool after;
        bool native;
        TypesetGlue glue;
    }* space_gaps;
    size_t space_gap_count, space_gap_capacity;
    ViewModelCheckpoint* pass; // the pages of the running layout pass
    PagedFlowTable* tables;    // the tables of the pass in flow order
    PagedFlowTable* last_table;
    PagedFlowTable* table;     // the innermost table whose content is being fitted
    ViewFragmentRole publish_role; // the role of the fragments publication appends
    const PagedFlowTable* publish_table; // the broken table whose grid is being published
    const PagedFlowTable* publish_grid; // resolved missing slots of the current table, including unbroken grids
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
    DomElement* detached_root; // auxiliary layout's independent inline reference frame
    float detached_width;
    bool detached_note;        // note continuations replay inline measures through ordinary layout
    struct PagedIndentBinding { const ViewCssStyle* owner; float width; }* indents;
    size_t indent_count, indent_capacity;
    TypesetStatus status;
    uint32_t relaxed_minima, relaxed_avoidance;
    size_t native_steps;
};

static TypesetStatus paged_flow_fail(PagedFlowState* state, TypesetStatus status, DomNode* source, const char* reason) {
    if (state->status == TYPESET_OK) state->status = status;
    return paged_failure(state->composition, status, source,
        state->composer.page ? state->composer.page->page_number : 0, reason);
}

ViewTree* paged_flow_view_tree(const LayoutContext* lycon) {
    return lycon->paged->tree;
}

static ViewCssStyle* paged_source_style(PagedFlowState* state, DomElement* source) {
    if (state->status != TYPESET_OK || source->is_synthetic()) return nullptr;
    ViewCssStyle* style = view_css_resolve(state->tree, source);
    if (!style) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, source, nullptr); return nullptr; }
    TypesetStatus status = paged_style_traits_admit(state->composition, style);
    if (status != TYPESET_OK) { state->status = status; return nullptr; }
    return style;
}

static TypesetStatus paged_source_traits_admit(PagedFlowState* state, DomElement* source) {
    paged_source_style(state, source);
    return state->status;
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

// later sides win; generic page/column demands still accumulate (CSS Break 3 §4.3).
static ViewBreak paged_break_combine(ViewBreak earlier, ViewBreak later) {
    return later >= VIEW_BREAK_LEFT ? later : (earlier > later ? earlier : later);
}

static ViewBreak paged_flow_box_break(PagedFlowState* state, ViewBlock* box, bool after);

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

enum PagedKeepRelation { PAGED_KEEP_TOGETHER, PAGED_KEEP_NEXT, PAGED_KEEP_PREVIOUS };

static size_t paged_fragmentainer_index(const PagedFlowState* state, float y);

static void paged_keep_combine(RadiantKeepSpec* result, const RadiantKeepSpec& other) {
    for (size_t scope = 0; scope < 3; scope++)
        result->scope[scope] = radiant_keep_max(result->scope[scope], other.scope[scope]);
}

// preserve the separate domains until the actual break context selects their constraints.
static RadiantKeepSpec paged_keep_spec(PagedFlowState* state, const ViewBlock* box,
        PagedKeepRelation relation, bool columns) {
    RadiantKeepSpec result = {};
    if (!box) return result;
    const BlockProp* blk = box->blk ? box->block() : nullptr;
    ViewBreak css = relation == PAGED_KEEP_TOGETHER
        ? paged_flow_break_keyword(blk ? blk->break_inside : CSS_VALUE_AUTO)
        : paged_flow_box_break(state, const_cast<ViewBlock*>(box), relation == PAGED_KEEP_NEXT);
    if (css == VIEW_BREAK_AVOID || (columns && css == VIEW_BREAK_AVOID_COLUMN)) result.scope[1].kind = RADIANT_KEEP_ALWAYS;
    if (css == VIEW_BREAK_AVOID || css == VIEW_BREAK_AVOID_PAGE) result.scope[2].kind = RADIANT_KEEP_ALWAYS;
    const ViewCssStyle* style = !box->is_synthetic() ? paged_source_style(state, const_cast<ViewBlock*>(box)) : nullptr;
    const RadiantFlowTraits* traits = style ? style->flow_traits.get() : nullptr;
    if (traits) paged_keep_combine(&result, relation == PAGED_KEEP_TOGETHER ? traits->together :
        relation == PAGED_KEEP_NEXT ? traits->next : traits->previous);
    return result;
}

static bool paged_context_has_columns(const PagedFlowState* state, size_t index) {
    // continuous editions own an unbounded fragmentainer without a physical page.
    const ViewPageBox* page = index < state->fragmentainer_count ? state->fragmentainers[index].page : nullptr;
    return page && page->style && page->style->column_count > 1;
}

static RadiantKeepSpec paged_inside_keep(PagedFlowState* state, const ViewBlock* block) {
    size_t index = state->fragmentainer_count ? paged_fragmentainer_index(state, paged_flow_top(const_cast<ViewBlock*>(block))) : 0;
    bool columns = paged_context_has_columns(state, index);
    return paged_keep_spec(state, block, PAGED_KEEP_TOGETHER, columns);
}

// soft avoidance relaxes when a fresh context cannot contain the material (integration §4.1).
static bool paged_flow_avoids_inside(PagedFlowState* state, const ViewBlock* block) {
    RadiantKeepSpec keep = paged_inside_keep(state, block);
    return keep.scope[1].kind != RADIANT_KEEP_AUTO || keep.scope[2].kind != RADIANT_KEEP_AUTO;
}

static bool paged_flow_monolithic(PagedFlowState* state, const ViewBlock* block) {
    return paged_flow_atomic(block) || paged_flow_avoids_inside(state, block);
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

struct PagedTableFragmentInsets {
    float before, after;
    float furniture_before, furniture_after;
};

static PagedTableFragmentInsets paged_table_fragment_insets(const PagedFlowState* state, const PagedFlowTable* table) {
    float cloned_top, cloned_bottom;
    paged_box_clone_edges(state, table->table, &cloned_top, &cloned_bottom);
    PagedTableFragmentInsets insets = {};
    // repeated groups already own natural grid edges; bare continuations retain their cloned edges separately.
    insets.before = table->header_size + (table->header ? 0.0f : cloned_top);
    insets.after = table->footer_size + (table->footer ? 0.0f : cloned_bottom);
    insets.furniture_before = table->header_size - (table->header ? cloned_top : 0.0f);
    insets.furniture_after = table->footer_size - (table->footer ? cloned_bottom : 0.0f);
    return insets;
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
static float paged_note_piece_height(PagedFlowState* state, PagedFlowAnchor* anchor, float offset, float available);
static bool paged_auxiliary_extend_terminal(PagedFlowState* state, size_t index);

static const PagedTerminalPage* paged_terminal_tail(const PagedFlowState* state, const DomNode* source) {
    const PagedReferenceSession* references = state->composition->references;
    for (size_t i = 0; references && i < references->terminal_count; i++)
        if (references->terminals[i].tail == source) return &references->terminals[i];
    return nullptr;
}

// cell fitting can grow the final row before its extent is committed to the table.
static float paged_table_row_bottom(ViewTableRow* row) {
    float top = paged_flow_top(row), bottom = top + row->height;
    row->each_cell([&](ViewTableCell* cell) { bottom = fmaxf(bottom, top + cell->y + cell->height); });
    return bottom;
}

// the space fragmentainer `index` keeps free above and below `view`'s content:
// cloned box edges, the footnotes and page floats placed on it, and around a
// broken table's rows the repeated header (on continuation fragmentainers) and footer
static void paged_reserve(PagedFlowState* state, View* view, size_t index, float* top, float* bottom) {
    paged_clone_edges(state, view, top, bottom);
    *bottom += paged_fragmentainer_reserved(state, index);
    for (const PagedFlowTable* table = state->table; table; table = table->outer) {
        if (!paged_table_body_view(view, table->table)) continue;
        PagedTableFragmentInsets insets = paged_table_fragment_insets(state, table);
        if (index > table->first) *top += insets.furniture_before;
        *bottom += insets.furniture_after;
        // an omitted repeat still closes the last body fragment with its natural footer.
        if (table->terminal_row) {
            float end = paged_table_row_bottom(table->terminal_row);
            const PagedFragmentainer& frame = state->fragmentainers[index];
            if (end > frame.top + kPagedFlowEpsilon && end <= frame.bottom + kPagedFlowEpsilon)
                *bottom += table->terminal_footer;
        }
        for (size_t i = 0; i < table->split_count; i++) {
            const PagedSplitCell& split = table->split[i];
            if (index > split.first && split.offset > 0.0f && view_geometry_is_descendant(view, split.cell, true))
                *top += split.offset;
        }
    }
}

static int paged_flow_line_limit(PagedFlowState* state, const ViewBlock* block, bool widows) {
    const BlockProp* blk = block && block->blk ? block->block() : nullptr;
    const ViewCssStyle* style = block && !block->is_synthetic()
        ? paged_source_style(state, const_cast<ViewBlock*>(block)) : nullptr;
    // selected computed limits include inheritance that table workspace props omit.
    int value = style ? (widows ? style->widows : style->orphans)
        : blk ? (widows ? blk->widows : blk->orphans) : 0;
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
        const PagedTerminalPage* terminal = paged_terminal_tail(state, anchor->element);
        bool held = terminal && fragmentainer.page && fragmentainer.page->page_number < terminal->page;
        if (held || anchor->full_height > fragmentainer.rect.height + kPagedFlowEpsilon) {
            float capacity = fragmentainer.bottom - content_end - fragmentainer.float_top - fragmentainer.float_bottom -
                area->margin[0] - area->margin[2] - area->edges[0] - area->edges[2] - fragmentainer.notes;
            if (!alone) capacity = fminf(capacity, limit - area->edges[0] - area->edges[2] - fragmentainer.notes);
            if (held) capacity = fminf(capacity, terminal->offset);
            anchor->height = paged_note_piece_height(state, anchor, 0.0f, capacity);
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
        const PagedTerminalPage* terminal = paged_terminal_tail(state, anchor->element);
        if (anchor->kind != PAGED_ANCHOR_NOTE && terminal && state->fragmentainers[index].page &&
            state->fragmentainers[index].page->page_number < terminal->page) {
            if (kept) *kept = i - first;
            return PAGED_ANCHORS_MOVE;
        }
        bool automatic = anchor->kind == PAGED_ANCHOR_NOTE && anchor->policy == PAGED_NOTE_AUTO;
        if (automatic && state->notes_waiting) { paged_note_defer(state, anchor); continue; }
        bool fits = paged_anchor_fits(state, anchor, index, content_end, false);
        if (!fits && starts && paged_auxiliary_extend_terminal(state, index))
            fits = paged_anchor_fits(state, anchor, index, content_end, false);
        if (fits) { paged_anchor_place(state, anchor, index); continue; }
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

static PagedTerminalPage* paged_terminal_record(PagedFlowState* state, const RadiantPageSequence* sequence) {
    PagedReferenceSession* references = state->composition->references;
    if (!references) return nullptr;
    for (size_t i = 0; i < references->terminal_count; i++)
        if (references->terminals[i].sequence == sequence->source.address) return &references->terminals[i];
    if (!lam::pool_grow_array(references->pool, &references->terminals,
        &references->terminal_capacity, references->terminal_count + 1, 4)) {
        paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, sequence->source.address, nullptr); return nullptr;
    }
    PagedTerminalPage* record = &references->terminals[references->terminal_count++];
    *record = {sequence->source.address};
    return record;
}

// an indivisible auxiliary tail can require an ordinary sheet before the last master.
static bool paged_auxiliary_extend_terminal(PagedFlowState* state, size_t index) {
    PagedFragmentainer& frame = state->fragmentainers[index];
    ViewPageBox* page = frame.page;
    const RadiantPageSequence* sequence = page ? page->sequence.get() : nullptr;
    if (!sequence || !sequence->master_program || !sequence->master_program->terminal ||
        page->page_number != state->composer.terminal_page || index + 1 != state->fragmentainer_count ||
        frame.column || frame.notes || frame.float_top || frame.float_bottom) return false;
    const RadiantPageRule* ordinary = radiant_page_master_select(sequence, page->sequence_page, page->folio, page->blank, false);
    if (!ordinary || !strcmp(ordinary->name, page->name)) return false;
    PagedTerminalPage* terminal = paged_terminal_record(state, sequence);
    if (!terminal) return false;
    terminal->page = page->page_number + 1;
    state->composer.terminal_page = terminal->page;
    TypesetStatus status = paged_empty_page_restyle(&state->composer, false);
    if (status != TYPESET_OK) { paged_flow_fail(state, status, sequence->source.address, nullptr); return false; }
    paged_fragmentainer_restyle(state, index, false);
    state->composition->reference_used = true;
    return true;
}

static TypesetStatus paged_fragmentainer_append(PagedFlowState* state, bool new_page, bool restart) {
    if (state->status != TYPESET_OK) return state->status;
    PagedPageCursor* composer = &state->composer;
    bool first = !state->fragmentainer_count;
    bool continuous = state->tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS;
    if (continuous && !first) return paged_flow_fail(state, TYPESET_INVALID, nullptr, "a continuous edition has one fragmentainer");
    TypesetStatus status = TYPESET_OK;
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
        }
        if (status == TYPESET_OK) {
            composer->terminal_page = paged_flow_terminal_page(state, composer->sequence);
            // a forced run boundary can try a wider/multicolumn last master before ordinary pages expand the tail.
            if (restart && !first && !composer->terminal_page && composer->sequence && composer->page &&
                composer->page->sequence.get() == composer->sequence && composer->sequence->master_program &&
                composer->sequence->master_program->terminal)
                composer->terminal_page = static_cast<uint32_t>(state->tree->model->page_count + 1);
            status = paged_page_create(composer, false);
        }
    }
    // void layout hooks may discard a return value; publication must still see the failed append.
    if (status != TYPESET_OK) return paged_flow_fail(state, status, nullptr,
        status == TYPESET_BUDGET_EXHAUSTED ? "page budget exhausted" : nullptr);
    const RdtLogicalRect& rect = composer->page_style.content_rect;
    if (first) state->run_width = rect.width;
    // width changes are measured on this pass and reflowed before publication settles.
    (void)restart;
    if (!lam::pool_grow_array(state->composition->pool, &state->fragmentainers, &state->fragmentainer_capacity,
        state->fragmentainer_count + 1, 8)) return paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, nullptr, nullptr);
    float top = first ? 0.0f : state->fragmentainers[state->fragmentainer_count - 1].bottom;
    float bottom = continuous ? INFINITY : top + rect.height;
    PagedFragmentainer fragmentainer = {top, bottom, bottom, continuous ? nullptr : composer->page, composer->column_index, rect, nullptr};
    state->fragmentainers[state->fragmentainer_count++] = fragmentainer;
    // automatic notes waiting for a fragmentainer start this one (plan P5)
    if (state->notes_waiting) paged_notes_resume(state, state->fragmentainer_count - 1, top, state->draining);
    return state->status;
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

static TypesetStatus paged_space_append(PagedFlowState* state, ViewBlock* block, bool after) {
    const ViewCssStyle* style = paged_source_style(state, block);
    const RadiantSpaceSpec* space = style && style->flow_traits
        ? (after ? &style->flow_traits->after : &style->flow_traits->before) : nullptr;
    if (!space || !space->specified) return state->status;
    if (!lam::pool_grow_array(state->composition->pool, &state->pending_spaces, &state->pending_space_capacity,
            state->pending_space_count + 1, 8)) return paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr);
    state->pending_spaces[state->pending_space_count++] = *space;
    return TYPESET_OK;
}

static void paged_space_anchors_shift(PagedFlowState* state, size_t skip, float delta) {
    for (size_t i = 0; i < state->space_box_count; i++)
        if (i != skip && state->space_boxes[i].parent == kPagedUnplaced)
            paged_shift_view(state->space_boxes[i].box, delta);
}

static void paged_space_shift(PagedFlowState* state, BlockContext* owner,
        const PagedFlowState::SpaceGap& gap, ViewBlock* root, float delta) {
    if (gap.after) {
        gap.box->height += delta; gap.box->content_height += delta;
        paged_flow_grow(owner, gap.box, false, root, delta);
    } else paged_shift_flow(state, owner, gap.paragraph, (int)gap.line, gap.box, root, delta); // INT_CAST_OK: paragraph line index
}

// open ancestors and empty siblings share one space boundary until real material separates it.
static TypesetStatus paged_space_commit(PagedFlowState* state, BlockContext* owner,
        PagedFlowParagraph* paragraph = nullptr, size_t line = 0, ViewBlock* closing = nullptr) {
    size_t first = state->space_open;
    while (first != kPagedUnplaced && state->space_boxes[first].parent != kPagedUnplaced)
        first = state->space_boxes[first].parent;
    ViewBlock* box = first != kPagedUnplaced ? state->space_boxes[first].box : closing;
    bool after = box && box == closing;
    if (state->pending_space_count && (box || paragraph)) {
        float top = box ? paged_flow_top(box) : paged_flow_top(paragraph->block) + paragraph->lines[line].top;
        if (after) top += box->height - layout_axis_decoration_end(box->bound, LAYOUT_AXIS_Y);
        // CSS margins position the border box later; they do not make a fresh reference area noninitial.
        float boundary = first != kPagedUnplaced ? state->space_boxes[first].top : top;
        size_t index;
        TypesetStatus status = paged_fragmentainer_at(state, boundary, &index);
        if (status != TYPESET_OK) return status;
        RadiantSpaceSpec range = radiant_spaces_resolve(state->pending_spaces, state->pending_space_count,
            boundary <= state->fragmentainers[index].top + kPagedFlowEpsilon, false);
        PagedFlowState::SpaceGap gap = {box, box ? nullptr : paragraph, line, range, range.optimum, after};
        if (range.minimum < range.optimum) {
            if (!lam::pool_grow_array(state->composition->pool, &state->space_gaps, &state->space_gap_capacity,
                    state->space_gap_count + 1, 16)) return paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, box, nullptr);
            state->space_gaps[state->space_gap_count++] = gap;
        }
        // only the outermost box moves; descendants already use its coordinate frame.
        paged_space_shift(state, first != kPagedUnplaced ? state->space_boxes[first].owner : owner, gap, nullptr, range.optimum);
        paged_space_anchors_shift(state, first, range.optimum);
        log_debug("[PAGED_SPACE] boundary at %.3f uses %.3f in [%.3f, %.3f]", top, range.optimum, range.minimum, range.maximum);
    }
    state->pending_space_count = state->space_box_count = 0;
    state->space_open = kPagedUnplaced;
    return TYPESET_OK;
}

static TypesetStatus paged_space_before(PagedFlowState* state, ViewBlock* block, BlockContext* owner) {
    if (!lam::pool_grow_array(state->composition->pool, &state->space_boxes, &state->space_box_capacity,
            state->space_box_count + 1, 16)) return paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr);
    state->space_boxes[state->space_box_count] = {block, owner, state->space_open, paged_flow_top(block)};
    state->space_open = state->space_box_count++;
    TypesetStatus status = paged_space_append(state, block, false);
    if (status != TYPESET_OK) return status;
    // a border/padding edge or an atomic/definite box separates the spaces outside and inside it.
    if (layout_box_metrics(block).pad_border_v != 0.0f || paged_flow_atomic(block) ||
        (block->blk && block->block()->given_height > 0.0f)) return paged_space_commit(state, owner);
    return TYPESET_OK;
}

static TypesetStatus paged_space_after(PagedFlowState* state, ViewBlock* block, BlockContext* owner) {
    if (state->space_open != kPagedUnplaced && state->space_boxes[state->space_open].box == block) {
        if (block->height > 0.0f) {
            TypesetStatus status = paged_space_commit(state, owner);
            if (status != TYPESET_OK) return status;
        } else {
            // the saved parent context belongs to this layout call and cannot outlive its return.
            PagedFlowState::SpaceBox& empty = state->space_boxes[state->space_open];
            empty.owner = nullptr; state->space_open = empty.parent;
        }
    }
    if (layout_axis_decoration_end(block->bound, LAYOUT_AXIS_Y) != 0.0f) {
        TypesetStatus status = paged_space_commit(state, owner, nullptr, 0, block);
        if (status != TYPESET_OK) return status;
    }
    return paged_space_append(state, block, true);
}

static void paged_space_reduce(PagedFlowState* state, BlockContext* owner, ViewBlock* block,
        PagedFlowState::SpaceGap& gap, ViewBlock* root, float amount) {
    ViewBlock* branch = gap.box ? gap.box : gap.paragraph->block;
    ViewBlock* enclosing = gap.box ? paged_flow_parent_block(branch) : branch;
    ViewBlock* closed = gap.box ? branch : nullptr;
    BlockContext* cursor = nullptr;
    for (; enclosing; enclosing = paged_flow_parent_block(enclosing)) {
        if (paged_view_inside(block, enclosing))
            for (BlockContext* active = owner; active; active = active->parent)
                // ordinary ancestors own cursors even when they share an outer BFC.
                if (active->flow_box == enclosing) { cursor = active; break; }
        if (cursor) break;
        if (enclosing == root) { closed = root; break; }
        closed = enclosing;
    }
    paged_space_shift(state, cursor, gap, closed, -amount);
    gap.used -= amount;
}

static bool paged_space_precedes(PagedFlowState* state, const PagedFlowState::SpaceGap& gap,
        size_t index, float before) {
    if (!gap.box && gap.line >= gap.paragraph->count) return false;
    float end = gap.box ? paged_flow_top(gap.box) : paged_flow_top(gap.paragraph->block) + gap.paragraph->lines[gap.line].top;
    if (gap.after) end += gap.box->height - layout_axis_decoration_end(gap.box->bound, LAYOUT_AXIS_Y);
    return end <= before + kPagedFlowEpsilon && paged_fragmentainer_index(state, end) == index;
}

// native vertical glue uses the same ordered packing contract as paragraph glue.
static float paged_native_shrink_spaces(PagedFlowState* state, BlockContext* owner,
        ViewBlock* block, size_t index, float before, float needed, ViewBlock* root) {
    if (!state->composition->options.native_flow_count || !state->space_gap_count) return 0.0f;
    TypesetItem* items = (TypesetItem*)pool_calloc(state->composition->pool, state->space_gap_count * sizeof(TypesetItem));
    if (!items) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return 0.0f; }
    float natural = 0.0f;
    for (size_t i = 0; i < state->space_gap_count; i++) {
        const PagedFlowState::SpaceGap& gap = state->space_gaps[i];
        if (!gap.native || !paged_space_precedes(state, gap, index, before)) continue;
        items[i].kind = TYPESET_GLUE; items[i].glue = gap.glue; items[i].glue.natural = gap.used;
        if (!gap.glue.shrink_order) items[i].glue.shrink = fmaxf(0.0f, gap.glue.shrink - gap.glue.natural + gap.used);
        natural += gap.used;
    }
    TypesetPacking packing = typeset_pack_glue(items, 0, state->space_gap_count, natural, natural - needed);
    float reduced = 0.0f;
    for (size_t i = 0; i < state->space_gap_count; i++) {
        if (items[i].kind != TYPESET_GLUE) continue;
        float amount = state->space_gaps[i].used - typeset_glue_advance(&items[i].glue, &packing);
        if (amount > 0.0f) { paged_space_reduce(state, owner, block, state->space_gaps[i], root, amount); reduced += amount; }
    }
    pool_free(state->composition->pool, items);
    return reduced;
}

// shrink earlier flexible space before adding an avoidable page break (XSL 1.1 §4.3.1).
static float paged_shrink_preceding_spaces(PagedFlowState* state, BlockContext* owner,
        ViewBlock* block, size_t index, float before, float needed, ViewBlock* root) {
    float reduced = paged_native_shrink_spaces(state, owner, block, index, before, needed, root);
    needed -= reduced;
    for (size_t i = state->space_gap_count; i > 0 && needed > kPagedFlowEpsilon; i--) {
        PagedFlowState::SpaceGap& gap = state->space_gaps[i - 1];
        // keeps and refits can move a gap after it was recorded; only the current line's earlier spaces can help.
        if (gap.native || !paged_space_precedes(state, gap, index, before)) continue;
        float amount = fminf(needed, fmaxf(0.0f, gap.used - gap.range.minimum));
        if (amount <= 0.0f) continue;
        paged_space_reduce(state, owner, block, gap, root, amount);
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

// anchors already on the destination are counted in its reservation; only incoming material adds capacity demand.
static void paged_box_anchor_reservations(PagedFlowState* state, View* box,
        size_t target, float* within, float* there, bool physical = false) {
    *within = *there = 0.0f;
    for (size_t i = 0; i < state->anchors_bound; i++) {
        const PagedFlowAnchor& anchor = state->anchors[i];
        View* at = anchor.box ? anchor.box : anchor.paragraph ? (View*)anchor.paragraph->block : nullptr;
        if (!at || !paged_view_inside(at, box)) continue;
        *within += anchor.height;
        if (anchor.fragmentainer == target || (physical && anchor.fragmentainer != kPagedUnplaced &&
            state->fragmentainers[anchor.fragmentainer].page == state->fragmentainers[target].page)) *there += anchor.height;
    }
}

static bool paged_keep_crosses_page(PagedFlowState* state, size_t from, size_t target) {
    return state->fragmentainers[from].page != state->fragmentainers[target].page;
}

static RadiantKeepStrength paged_keep_active(const RadiantKeepSpec& keep, bool page_break) {
    return page_break ? radiant_keep_max(keep.scope[1], keep.scope[2]) : keep.scope[1];
}

static RadiantKeepStrength paged_boundary_keep_side(PagedFlowState* state, ViewBlock* box,
        bool after, size_t from, size_t target) {
    bool columns = paged_context_has_columns(state, from) || paged_context_has_columns(state, target);
    RadiantKeepSpec keep = paged_keep_spec(state, box, after ? PAGED_KEEP_NEXT : PAGED_KEEP_PREVIOUS, columns);
    return paged_keep_active(keep, paged_keep_crosses_page(state, from, target));
}

static RadiantKeepStrength paged_boundary_keep(PagedFlowState* state, ViewBlock* previous,
        ViewBlock* next, size_t from, size_t target, RadiantKeepSpec* resolved = nullptr) {
    if (paged_flow_box_break(state, previous, true) >= VIEW_BREAK_COLUMN ||
        paged_flow_box_break(state, next, false) >= VIEW_BREAK_COLUMN) {
        if (resolved) *resolved = {};
        return {};
    }
    bool columns = paged_context_has_columns(state, from) || paged_context_has_columns(state, target);
    RadiantKeepSpec keep = paged_keep_spec(state, previous, PAGED_KEEP_NEXT, columns);
    paged_keep_combine(&keep, paged_keep_spec(state, next, PAGED_KEEP_PREVIOUS, columns));
    if (resolved) *resolved = keep;
    return paged_keep_active(keep, paged_keep_crosses_page(state, from, target));
}

static size_t paged_keep_context_first(PagedFlowState* state, size_t index, bool physical) {
    if (physical) while (index && state->fragmentainers[index - 1].page == state->fragmentainers[index].page) index--;
    return index;
}

// sheet capacity is the sum of its selected columns, including reservations already placed on that sheet.
static float paged_keep_capacity(PagedFlowState* state, size_t target, bool physical, float incoming) {
    const PagedFragmentainer& fragmentainer = state->fragmentainers[target];
    if (!physical) return fragmentainer.bottom - fragmentainer.top - paged_fragmentainer_reserved(state, target) - incoming;
    const ViewPageStyle* style = fragmentainer.page->style;
    float capacity = -incoming;
    for (uint32_t column = fragmentainer.column; column < style->column_count; column++) {
        RdtLogicalRect rect = {};
        if (!view_css_page_column(style, column, &rect)) { state->status = TYPESET_INVALID; return 0.0f; }
        capacity += rect.height;
        size_t slot = target + column - fragmentainer.column;
        if (slot < state->fragmentainer_count && state->fragmentainers[slot].page == fragmentainer.page)
            capacity -= paged_fragmentainer_reserved(state, slot);
    }
    return capacity;
}

static float paged_undo_struts(PagedFlowState* state, BlockContext* owner, ViewBlock* root, size_t since) {
    float inner = 0.0f;
    for (size_t i = state->strut_count; i > since; i--) {
        PagedStrut strut = state->struts[i - 1];
        inner += strut.amount;
        if (strut.table) paged_table_restore(state, strut.table, owner, root);
        else paged_shift_flow(state, owner, strut.paragraph, strut.line,
            strut.paragraph ? nullptr : strut.view, root, -strut.amount);
    }
    state->strut_count = since;
    return inner;
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
    PagedStrut broke = state->struts[strut];
    size_t target = broke.fragmentainer;
    if (!target) return TYPESET_OK;
    size_t origin = target - 1;
    RadiantKeepSpec initial = {};
    ViewBlock* previous_box = paged_previous_flow_sibling(box);
    if (previous_box) paged_boundary_keep(state, previous_box, box, origin, target, &initial);
    bool physical = paged_keep_crosses_page(state, origin, target) &&
        initial.scope[2].kind != RADIANT_KEEP_AUTO && initial.scope[1].kind == RADIANT_KEEP_AUTO;
    target = paged_keep_context_first(state, target, physical);
    float end = paged_flow_top(box) + box->height - broke.amount;
    if (box_live && broke.paragraph && broke.paragraph->count) {
        const PagedFlowLine& last = broke.paragraph->lines[broke.paragraph->count - 1];
        end = paged_flow_top(box) + last.top + last.height - broke.amount;
    }
    ViewBlock* first = box;
    size_t replay_since = strut, selected_since = strut;
    float compacted = 0.0f, leading = 0.0f;
    RadiantKeepStrength weakest = {0, RADIANT_KEEP_ALWAYS};
    float within = 0.0f, there = 0.0f;
    for (ViewBlock* candidate = box; candidate;) {
        ViewBlock* previous = paged_previous_flow_sibling(candidate);
        float reserve_top, reserve_bottom, incoming, present;
        paged_reserve(state, candidate, target, &reserve_top, &reserve_bottom);
        paged_box_anchor_reservations(state, candidate, target, &incoming, &present, physical);
        within += incoming; there += present;
        float capacity = paged_keep_capacity(state, target, physical, within - there);
        if (!physical) capacity -= reserve_top + reserve_bottom - paged_fragmentainer_reserved(state, target);
        // internal column struts are removed before a page-only prefix is fitted to its new sheet.
        float material = end - compacted - (paged_flow_top(candidate) - leading);
        if (candidate != box && material > capacity + kPagedFlowEpsilon) break;
        RadiantKeepStrength strength = previous
            ? paged_boundary_keep(state, previous, candidate, origin, target) : RadiantKeepStrength{};
        if (state->status != TYPESET_OK) return state->status;
        if (radiant_keep_compare(strength, weakest) <= 0) {
            first = candidate; weakest = strength; selected_since = replay_since;
        }
        if (!previous || strength.kind == RADIANT_KEEP_AUTO) break;
        bool movable = true;
        float inner = 0.0f;
        leading = 0.0f;
        for (size_t i = 0; i < strut; i++) {
            const PagedStrut& old = state->struts[i];
            if (!paged_view_inside(old.view, previous)) continue;
            if (!physical || old.forced) { movable = false; break; }
            replay_since = min(replay_since, i); inner += old.amount;
            if (old.view == previous && !old.paragraph && !old.table) leading += old.amount;
        }
        if (movable) {
            size_t index = paged_fragmentainer_index(state, paged_flow_top(previous));
            size_t start = paged_keep_context_first(state, index, physical);
            // moving a prefix that already starts its context would leave that context empty.
            movable = paged_flow_top(previous) - leading > state->fragmentainers[start].top + kPagedFlowEpsilon;
        }
        if (!movable) break;
        compacted += inner;
        candidate = previous;
    }
    if (weakest.kind != RADIANT_KEEP_AUTO) state->relaxed_avoidance++;
    if (first == box) return TYPESET_OK;
    log_debug("[PAGED_FLOW_KEEP] weakest sibling cut before %s: kind=%u value=%d target=%zu",
        first->source_loc(), weakest.kind, weakest.value, target);
    float page_top = state->fragmentainers[target].top;
    // undo the break before the box, then break before the first kept sibling
    if (broke.paragraph) paged_shift_flow(state, owner, broke.paragraph, broke.line, nullptr, box_live ? nullptr : box, -broke.amount);
    else paged_shift_flow(state, owner, nullptr, -1, box, box, -broke.amount);
    state->strut_count = strut;
    BlockContext* chain_owner = box_live ? owner->parent.get() : owner;
    if (selected_since < strut) {
        ViewBlock* parent = paged_flow_parent_block(first);
        paged_undo_struts(state, chain_owner, parent ? parent : first, selected_since);
    }
    float delta = page_top - paged_flow_top(first);
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

// forced and automatic line breaks share the same geometry and strut journal.
static TypesetStatus paged_lines_move(PagedFlowState* state, BlockContext* owner,
        PagedFlowParagraph* paragraph, size_t first, size_t last, size_t target,
        float reserve_top, ViewBlock* root, bool forced) {
    float before = paged_flow_top(paragraph->block) + paragraph->lines[first].top;
    float strut = fmaxf(0.0f, state->fragmentainers[target].top + reserve_top - before);
    int line = (int)first; // INT_CAST_OK: paragraph line indices use the layout line counter
    TypesetStatus status = paged_strut_record(state, paragraph->block, paragraph, line, strut, forced, target, before);
    if (status != TYPESET_OK) return status;
    paged_shift_flow(state, owner, paragraph, line, nullptr, root, strut);
    for (size_t i = first; i <= last; i++) paragraph->lines[i].fragmentainer = target;
    return TYPESET_OK;
}

static TypesetStatus paged_native_boundary(PagedFlowState* state, PagedFlowParagraph* paragraph,
        size_t line, size_t frame, TypesetBreak* boundary) {
    const PagedFlowLine& placed = paragraph->lines[line];
    const PagedNativeLine* native = placed.native;
    *boundary = native ? native->before : TypesetBreak{};
    if (boundary->scope == TYPESET_BREAK_LINE) *boundary = {};
    if (!native || !native->binding->break_candidate) return TYPESET_OK;
    const PagedFragmentainer& area = state->fragmentainers[frame];
    PagedNativeBreakCandidate candidate = {native->contribution.source, native->resume,
        native->selected.first, native->selected.next, area.page ? area.page->page_number : 0, area.column,
        paged_flow_top(paragraph->block) + placed.top - area.top, area.bottom - area.top,
        placed.height, *boundary};
    TypesetStatus status = native->binding->break_candidate(native->binding->context, &candidate, boundary);
    if (status == TYPESET_OK && (boundary->legality > TYPESET_BREAK_FORCED || boundary->scope > TYPESET_BREAK_PAGE))
        status = TYPESET_INVALID;
    return status;
}

// producer preferences rank legal boundaries; the shared fit still supplies the strut and page.
static TypesetStatus paged_native_break_first(PagedFlowState* state, PagedFlowParagraph* paragraph,
        size_t line, size_t previous, size_t* first) {
    if (!paragraph->lines[line].native) return TYPESET_OK;
    size_t chosen = SIZE_MAX;
    int32_t cost = INT32_MAX;
    size_t start = *first;
    while (start > 0 && paragraph->lines[start - 1].fragmentainer == previous) start--;
    for (size_t at = *first + 1; at > start; ) {
        at--;
        if (at > start && at - start < (size_t)paged_flow_line_limit(state, paragraph->block, false)) continue;
        TypesetBreak boundary;
        TypesetStatus status = paged_native_boundary(state, paragraph, at, previous, &boundary);
        if (status != TYPESET_OK) return status;
        if (boundary.legality == TYPESET_BREAK_FORBIDDEN) continue;
        float top = paged_flow_top(paragraph->block) + paragraph->lines[at].top;
        if (at < line && top <= state->fragmentainers[previous].top + kPagedFlowEpsilon) continue;
        if (chosen == SIZE_MAX || boundary.penalty < cost) { chosen = at; cost = boundary.penalty; }
        if (boundary.legality == TYPESET_BREAK_FORCED) { chosen = at; break; }
    }
    if (chosen == SIZE_MAX) return TYPESET_UNPLACEABLE;
    *first = chosen;
    return TYPESET_OK;
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
            paragraph->block, index, top, excess, root) > 0.0f) {
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
    bool forced = lines[line].native && lines[line].native->before.legality == TYPESET_BREAK_FORCED &&
        lines[line].native->before.scope >= TYPESET_BREAK_COLUMN;
    size_t on_previous = 0;
    while (first > 0 && lines[first - 1].fragmentainer == previous) { first--; on_previous++; }
    if (!forced && on_previous && (block_policy || (int)on_previous < paged_flow_line_limit(state, paragraph->block, false))) {
        // orphans: the lines before the break move with it unless they start the fragmentainer
        float previous_top, previous_bottom;
        paged_reserve(state, paragraph->block, previous, &previous_top, &previous_bottom);
        if (block_top + lines[first].top <= state->fragmentainers[previous].top + previous_top + kPagedFlowEpsilon) {
            first = line; state->relaxed_minima++;
        }
    } else first = line;
    status = forced ? TYPESET_OK : paged_native_break_first(state, paragraph, line, previous, &first);
    if (status != TYPESET_OK) return status;
    float target_top, target_bottom;
    paged_reserve(state, paragraph->block, target, &target_top, &target_bottom);
    // an explicit producer break stays forced even when it lands exactly on the natural page edge.
    status = paged_lines_move(state, owner, paragraph, first, line, target, target_top, root, forced);
    if (status != TYPESET_OK) return status;
    size_t recorded = state->strut_count - 1;
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

// pagination gaps are not part of the widow group's size in a fresh fragmentainer.
static float paged_paragraph_span_height(const PagedFlowState* state, const PagedFlowParagraph* paragraph, size_t first) {
    const PagedFlowLine& last = paragraph->lines[paragraph->count - 1];
    float height = last.top + last.height - paragraph->lines[first].top;
    for (size_t i = 0; i < state->strut_count; i++) {
        const PagedStrut& strut = state->struts[i];
        if (strut.paragraph == paragraph && strut.line >= 0 && (size_t)strut.line > first)
            height -= strut.amount;
    }
    return height;
}

// the inline run of `paragraph` ended: widows move the last break earlier when
// too few lines follow it (CSS Fragmentation 3 §4.4)
static TypesetStatus paged_paragraph_widows(PagedFlowState* state, LayoutContext* lycon, BlockContext* owner,
        PagedFlowParagraph* paragraph, ViewBlock* root, bool keeps, bool* retry) {
    PagedFlowLine* lines = paragraph->lines;
    size_t broke = paragraph->count;
    while (broke > 1 && lines[broke - 1].fragmentainer == lines[broke - 2].fragmentainer) broke--;
    if (broke <= 1) return TYPESET_OK;
    broke--; // the first line after the last break
    for (size_t i = state->strut_count; i > 0; i--) {
        const PagedStrut& strut = state->struts[i - 1];
        if (strut.paragraph == paragraph && strut.line == (int)broke && strut.forced) return TYPESET_OK; // INT_CAST_OK: paragraph line index
    }
    size_t after = paragraph->count - broke;
    int widows = paged_flow_line_limit(state, paragraph->block, true);
    if ((int)after >= widows) return TYPESET_OK;
    size_t needed = (size_t)widows - after;
    size_t moved = broke > needed ? broke - needed : 0;
    // orphans stay satisfied or the whole run moves.
    if (moved && (int)moved < paged_flow_line_limit(state, paragraph->block, false)) moved = 0;
    float block_top = paged_flow_top(paragraph->block);
    size_t previous = lines[broke - 1].fragmentainer, target = lines[broke].fragmentainer;
    while (lines[moved].native) {
        TypesetBreak boundary;
        TypesetStatus status = paged_native_boundary(state, paragraph, moved, previous, &boundary);
        if (status != TYPESET_OK) return status;
        if (boundary.legality != TYPESET_BREAK_FORBIDDEN) break;
        if (!moved) return TYPESET_UNPLACEABLE;
        moved--;
        if (moved && (int)moved < paged_flow_line_limit(state, paragraph->block, false)) moved = 0; // INT_CAST_OK: paragraph line count
    }
    float previous_top, previous_bottom, target_top, target_bottom;
    paged_reserve(state, paragraph->block, previous, &previous_top, &previous_bottom);
    paged_reserve(state, paragraph->block, target, &target_top, &target_bottom);
    if (block_top + lines[moved].top <= state->fragmentainers[previous].top + previous_top + kPagedFlowEpsilon) {
        float height = paged_paragraph_span_height(state, paragraph, moved);
        float previous_capacity = state->fragmentainers[previous].bottom - state->fragmentainers[previous].top - previous_top - previous_bottom;
        float target_capacity = state->fragmentainers[target].bottom - state->fragmentainers[target].top - target_top - target_bottom;
        // a fresh short column may be emptied when the following area can actually hold the widow group.
        if (height <= previous_capacity + kPagedFlowEpsilon || height > target_capacity + kPagedFlowEpsilon) {
            state->relaxed_minima++;
            return TYPESET_OK;
        }
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
    // refitting can split the repaired group again at a shorter intermediate column.
    *retry = status == TYPESET_OK && lines[paragraph->count - 1].fragmentainer > target;
    return status;
}

static TypesetStatus paged_paragraph_close(PagedFlowState* state, LayoutContext* lycon, BlockContext* owner,
        ViewBlock* root, bool keeps = true) {
    PagedFlowParagraph* paragraph = state->paragraph;
    state->paragraph = nullptr;
    if (!paragraph || paragraph->count < 2 || state->status != TYPESET_OK) return state->status;
    TypesetStatus status;
    bool retry;
    do {
        retry = false;
        status = paged_paragraph_widows(state, lycon, owner, paragraph, root, keeps, &retry);
    } while (status == TYPESET_OK && retry);
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
        if (bottom <= at + (ending ? -kPagedFlowEpsilon : kPagedFlowEpsilon)) {
            // a completed origin row can still own a cell spanning the later break.
            if (!content && box->view_type == RDT_VIEW_TABLE_ROW &&
                    paged_table_row_bottom(static_cast<ViewTableRow*>(box)) > at + kPagedFlowEpsilon)
                paged_table_open(box, top, at, delta, ending, content);
            continue;
        }
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

static void paged_note_break_edge(PagedFlowState* state, ViewElement* element, float origin, float lower, float upper,
        float* edge, float* last_start = nullptr) {
    for (DomNode* node = element->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        if (!child->view_type) continue;
        if (child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
            if (layout_block_is_out_of_flow(block)) continue;
            // inline atomic boxes contribute their end, never their descendant break edges.
            bool atomic = layout_display_is_inline_level(block->display.outer) || paged_flow_monolithic(state, block);
            if (!atomic) paged_note_break_edge(state, block, origin + block->y, lower, upper, edge, last_start);
            if (last_start && (atomic || !block->first_child) && block->height > 0.0f)
                *last_start = fmaxf(*last_start, origin + block->y);
            float bottom = origin + block->y + block->height;
            if (bottom > lower + kPagedFlowEpsilon && bottom <= upper + kPagedFlowEpsilon) *edge = fmaxf(*edge, bottom);
        } else if (child->view_type == RDT_VIEW_TEXT) {
            for (TextRect* rect = lam::view_require_text(child)->rect; rect; rect = rect->next) {
                if (last_start && (rect->length > 0 || rect->width > 0.0f)) *last_start = fmaxf(*last_start, origin + rect->y);
                float bottom = origin + rect->y + rect->height;
                if (bottom > lower + kPagedFlowEpsilon && bottom <= upper + kPagedFlowEpsilon) *edge = fmaxf(*edge, bottom);
            }
        } else if (child->view_type == RDT_VIEW_INLINE)
            paged_note_break_edge(state, lam::view_require_element(child), origin, lower, upper, edge, last_start);
    }
}

static float paged_note_piece_height(PagedFlowState* state, PagedFlowAnchor* anchor, float offset, float available) {
    if (available <= 0.0f) return 0.0f;
    if (anchor->full_height - offset <= available + kPagedFlowEpsilon) return anchor->full_height - offset;
    float edge = offset;
    paged_note_break_edge(state, (ViewElement*)anchor->element, 0.0f, offset, offset + available, &edge);
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
        PagedFragmentainer& target = state->fragmentainers[index];
        float capacity = target.rect.height - target.float_top - target.float_bottom - area->margin[0] - area->margin[2] -
            area->edges[0] - area->edges[2] - target.notes;
        const PagedTerminalPage* terminal = paged_terminal_tail(state, anchor->element);
        if (terminal && target.page && target.page->page_number < terminal->page) {
            if (offset >= terminal->offset - kPagedFlowEpsilon) continue;
            capacity = fminf(capacity, terminal->offset - offset);
        }
        float height = paged_note_piece_height(state, anchor, offset, capacity);
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
    if (paged_flow_monolithic(state, box) || paged_flow_closed(box)) {
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

static ViewBreak paged_source_break(PagedFlowState* state, DomElement* element, bool after) {
    const CssPropertyCode properties[] = {
        after ? CSS_PROPERTY_BREAK_AFTER : CSS_PROPERTY_BREAK_BEFORE,
        after ? CSS_PROPERTY_PAGE_BREAK_AFTER : CSS_PROPERTY_PAGE_BREAK_BEFORE, CSS_PROPERTY_ALL};
    for (; element; element = element->parent_element()) {
        ViewCssStyle* style = paged_source_style(state, element);
        if (!style) return VIEW_BREAK_AUTO;
        const CssDeclaration* declaration = style_tree_get_component_declaration(
            element->specified_style, properties, sizeof(properties) / sizeof(properties[0]));
        if (!declaration) return VIEW_BREAK_AUTO;
        const CssValue* value = view_css_declaration_value(state->tree, style, declaration,
            after ? "break-after" : "break-before");
        TypesetStatus status = paged_style_traits_admit(state->composition, style);
        if (status != TYPESET_OK) { state->status = status; return VIEW_BREAK_AUTO; }
        if (!value || value->type != CSS_VALUE_TYPE_KEYWORD) return VIEW_BREAK_AUTO;
        if (value->data.keyword == CSS_VALUE_INHERIT) continue;
        return paged_flow_break_keyword(value->data.keyword);
    }
    return VIEW_BREAK_AUTO;
}

static ViewBreak paged_flow_box_break(PagedFlowState* state, ViewBlock* box, bool after) {
    ViewBreak value = paged_box_break(box, after);
    if (!box || !paged_flow_is_table(box)) return value;
    LayoutTableBodyEdge edge = layout_table_source_body_edge(box, after);
    // propagation changes the used boundary, never the table's computed break properties.
    if (edge.group) value = paged_break_combine(value, paged_source_break(state, edge.group, after));
    if (edge.row) value = paged_break_combine(value, paged_source_break(state, edge.row, after));
    return value;
}

// groups precede their descendant rows in flow order on each side of the boundary.
static ViewBreak paged_table_boundary(const PagedTableItem* items, size_t i) {
    const PagedTableItem& item = items[i];
    const PagedTableItem& previous = items[i - 1];
    ViewBreak before = paged_box_break(item.view, false);
    ViewBreak after = paged_box_break(previous.view, true);
    bool rows = item.kind == PAGED_ITEM_ROW && previous.kind == PAGED_ITEM_ROW;
    if (!rows || item.group != previous.group) {
        ViewBreak group = item.kind == PAGED_ITEM_ROW && item.group->view_type == RDT_VIEW_TABLE_ROW_GROUP
            ? paged_box_break(item.group, false) : VIEW_BREAK_AUTO;
        before = paged_break_combine(group, before);
        group = previous.kind == PAGED_ITEM_ROW && previous.group->view_type == RDT_VIEW_TABLE_ROW_GROUP
            ? paged_box_break(previous.group, true) : VIEW_BREAK_AUTO;
        after = paged_break_combine(group, after);
    }
    return paged_break_combine(after, before);
}

static RadiantKeepStrength paged_table_boundary_keep(PagedFlowState* state,
        const PagedTableItem* items, size_t i, size_t from, size_t target) {
    if (paged_table_boundary(items, i) >= VIEW_BREAK_COLUMN) return {};
    const PagedTableItem& item = items[i];
    const PagedTableItem& previous = items[i - 1];
    RadiantKeepStrength result = paged_boundary_keep(state, previous.view, item.view, from, target);
    if (item.group != previous.group) {
        if (item.group->view_type == RDT_VIEW_TABLE_ROW_GROUP)
            result = radiant_keep_max(result, paged_boundary_keep_side(state, item.group, false, from, target));
        if (previous.group->view_type == RDT_VIEW_TABLE_ROW_GROUP)
            result = radiant_keep_max(result, paged_boundary_keep_side(state, previous.group, true, from, target));
    }
    return result;
}

// the extent of a table item: a row reaches the bottom of its row-spanning cells
static float paged_table_item_bottom(const PagedTableItem& item) {
    float top = paged_flow_top(item.view), bottom = top + item.view->height;
    if (item.kind != PAGED_ITEM_ROW) return bottom;
    return paged_table_row_bottom(static_cast<ViewTableRow*>(item.view));
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
            align == TableCellProp::CELL_VALIGN_RELATIVE_BEFORE ||
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
    LayoutTableSectionOrder sections;
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
        // the first header and footer groups are the table's furniture (CSS 2.2 §17.2)
        if (!sections.accept_body_group(group)) {
            if (sections.header == child) { *header = child; append(child, nullptr, PAGED_ITEM_HEADER); }
            else { *footer = child; append(child, nullptr, PAGED_ITEM_FOOTER); }
            return;
        }
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
    // common layout has resolved collapsed half-borders and ignored table padding already.
    RdtLogicalRect grid = layout_table_grid_rect(table);
    record->header_offset = record->header ? fmaxf(0.0f, record->header->y - grid.y) : spacing;
    record->header_size = record->header ? record->header_offset + record->header->height + spacing : spacing;
    record->footer_size = spacing + (record->footer ? fmaxf(0.0f, grid.y + grid.height - record->footer->y) : 0.0f);
    record->first = first;
    record->terminal_row = nullptr; record->terminal_footer = 0.0f;
    if (footer && !record->footer) {
        for (size_t i = count; i > 0; i--) if (items[i - 1].kind == PAGED_ITEM_ROW) {
            record->terminal_row = static_cast<ViewTableRow*>(items[i - 1].view);
            break;
        }
        record->terminal_footer = fmaxf(0.0f, grid.y + grid.height - footer->y);
    }
    record->strut_count = 0; record->split_count = 0;
    float table_reserve_top, table_reserve_bottom;
    paged_reserve(state, block, first, &table_reserve_top, &table_reserve_bottom);
    if (table_top + block->height + table_reserve_bottom <= start->bottom + kPagedFlowEpsilon) {
        bool forced_boundary = false;
        for (size_t i = 1; i < count; i++) {
            if (items[i - 1].kind == PAGED_ITEM_ROW && items[i].kind == PAGED_ITEM_ROW &&
                paged_table_boundary(items, i) >= VIEW_BREAK_COLUMN) {
                forced_boundary = true;
                break;
            }
        }
        // a fitting table still fragments at authored internal row boundaries.
        if (!forced_boundary) return TYPESET_OK;
    }
    // the leading part (captions, header, first body rows) stays with the table:
    // when it cannot stay on this fragmentainer the whole table moves (or the
    // whole table, under break-inside: avoid)
    size_t lead = 0;
    while (lead < count && items[lead].kind != PAGED_ITEM_ROW) lead++;
    float lead_bottom = paged_flow_avoids_inside(state, block) || lead == count ? table_top + block->height
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
        } else if (paged_flow_avoids_inside(state, block)) state->relaxed_avoidance++;
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
        // a row can reach the next fragmentainer exactly without overflowing; its keep still applies there.
        bool natural_break = row && i > 0 && items[i - 1].kind == PAGED_ITEM_ROW && index > first &&
            paged_flow_top(items[i - 1].view) < fragmentainer->top &&
            top <= fragmentainer->top + reserve_top + kPagedFlowEpsilon;
        ViewBreak boundary = i > 0 ? paged_table_boundary(items, i) : VIEW_BREAK_AUTO;
        size_t target = index;
        float delta = 0.0f;
        bool forced = false;
        if (boundary >= VIEW_BREAK_COLUMN && has_content && row && items[i - 1].kind == PAGED_ITEM_ROW) {
            // an authored break between body rows; the header and footer groups are
            // furniture of every fragment, so no break separates them from the body
            status = paged_forced_target(state, index, boundary, &target);
            forced = true;
        } else if (!natural_break && index > first && top < fragmentainer->top + reserve_top - kPagedFlowEpsilon) {
            // a box in the space a continuation keeps for the repeated header moves below it
            target = index;
        } else {
            // the extent that moves together: rows joined by a row span
            size_t last = i;
            float bottom = paged_table_item_bottom(item);
            // a forced boundary still breaks a row group crossed by a spanning cell.
            while (row && last + 1 < count && items[last + 1].kind == PAGED_ITEM_ROW && items[last + 1].group == item.group &&
                paged_table_boundary(items, last + 1) < VIEW_BREAK_COLUMN &&
                paged_flow_top(items[last + 1].view) < bottom - kPagedFlowEpsilon)
                bottom = fmaxf(bottom, paged_table_item_bottom(items[++last]));
            if (!natural_break && bottom + reserve_bottom <= fragmentainer->bottom + kPagedFlowEpsilon) { i = last + 1; continue; }
            bool recorded_break = state->strut_count > journal && state->struts[state->strut_count - 1].view == item.view &&
                state->struts[state->strut_count - 1].fragmentainer == index;
            // a rechecked cut has already resolved avoidance; an authored boundary overrides it.
            if (natural_break && (recorded_break || boundary >= VIEW_BREAK_COLUMN)) { i = last + 1; continue; }
            size_t origin = natural_break ? index - 1 : index;
            float origin_top = state->fragmentainers[origin].top;
            float origin_reserve_top, origin_reserve_bottom;
            paged_reserve(state, item.view, origin, &origin_reserve_top, &origin_reserve_bottom);
            size_t next = index;
            if (!natural_break) status = paged_fragmentainer_at(state, fragmentainer->bottom + kPagedFlowEpsilon, &next);
            if (status != TYPESET_OK) break;
            float next_top, next_bottom;
            paged_reserve(state, item.view, next, &next_top, &next_bottom);
            float capacity = state->fragmentainers[next].bottom - state->fragmentainers[next].top - next_top - next_bottom;
            // break-inside: avoid on a row group moves the whole group, and
            // break-before/after: avoid between rows moves the earlier rows along
            // (CSS Fragmentation 3 §4.3); a move that would empty its fragmentainer
            // or that a fresh fragmentainer cannot hold is relaxed
            size_t from = i + 1;
            if (row && item.group->view_type == RDT_VIEW_TABLE_ROW_GROUP && paged_flow_avoids_inside(state, item.group)) {
                float group_top = paged_flow_top(item.group);
                if (group_top > origin_top + origin_reserve_top + kPagedFlowEpsilon && item.group->height <= capacity + kPagedFlowEpsilon) {
                    from = i;
                    while (from > 0 && items[from - 1].group == item.group) from--;
                } else state->relaxed_avoidance++;
            }
            if (from > i) {
                RadiantKeepStrength weakest = {0, RADIANT_KEEP_ALWAYS};
                for (size_t chain = i;; chain--) {
                    float chain_top = paged_flow_top(items[chain].view);
                    if (chain_top <= origin_top + origin_reserve_top + kPagedFlowEpsilon ||
                        bottom - chain_top > capacity + kPagedFlowEpsilon) break;
                    RadiantKeepStrength strength = row && chain > 0 && items[chain - 1].kind == PAGED_ITEM_ROW
                        ? paged_table_boundary_keep(state, items, chain, origin, next) : RadiantKeepStrength{};
                    if (state->status != TYPESET_OK) return state->status;
                    if (radiant_keep_compare(strength, weakest) <= 0) { from = chain; weakest = strength; }
                    if (!chain || strength.kind == RADIANT_KEEP_AUTO) break;
                }
                if (from <= i && weakest.kind != RADIANT_KEEP_AUTO) state->relaxed_avoidance++;
            }
            if (natural_break && from == i && top >= state->fragmentainers[next].top + next_top - kPagedFlowEpsilon) {
                i = last + 1;
                continue;
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

static PagedFlowTable* paged_table_find(const PagedFlowState* state, const ViewBlock* block) {
    for (PagedFlowTable* record = state->tables; record; record = record->next)
        if (record->table == block) return record;
    return nullptr;
}

static PagedFlowTable* paged_table_get(PagedFlowState* state, ViewBlock* block) {
    if (PagedFlowTable* record = paged_table_find(state, block)) return record;
    PagedFlowTable* record = (PagedFlowTable*)pool_calloc(state->composition->pool, sizeof(PagedFlowTable));
    if (!record) return nullptr;
    record->table = block;
    if (state->last_table) state->last_table->next = record;
    else state->tables = record;
    state->last_table = record;
    return record;
}

void paged_table_grid(LayoutContext* lycon, ViewTable* table, TableMetadata* meta, float* positions) {
    PagedFlowState* state = lycon->paged;
    if (state->status != TYPESET_OK) return;
    PagedFlowTable* record = paged_table_find(state, table);
    if (record) record->missing_count = record->missing_row_count = 0;
    size_t count = 0;
    for (int row = 0; row < meta->row_count; row++)
        for (int column = 0; column < meta->column_count; column++)
            if (!meta->grid(row, column)) count++;
    if (!count) return;
    if (!record) record = paged_table_get(state, table);
    if (!record || !lam::pool_grow_array(state->composition->pool, &record->missing,
            &record->missing_capacity, count, 16) ||
        !lam::pool_grow_array(state->composition->pool, &record->missing_rows,
            &record->missing_row_capacity, (size_t)meta->row_count, 16)) {
        paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, table, nullptr); return;
    }
    int index = 0;
    table->each_row([&](ViewTableRow* row) {
        size_t first = record->missing_count;
        for (int column = 0; column < meta->column_count; column++) {
            if (meta->grid(index, column)) continue;
            // reuse common visual placement, including RTL; offsets are relative to the row's grid edge.
            float x = layout_table_column_visual_x(table, meta->col_widths, positions,
                column, 1, meta->column_count) - positions[0];
            record->missing[record->missing_count++] = {(size_t)column, x, meta->col_widths[column]};
        }
        if (record->missing_count > first)
            record->missing_rows[record->missing_row_count++] = {row, first, record->missing_count - first};
        index++;
    });
    // row identity gives logarithmic publication lookup without rescanning the occupancy grid.
    qsort(record->missing_rows, record->missing_row_count, sizeof(PagedMissingTableRow),
        [](const void* a, const void* b) {
            uintptr_t left = (uintptr_t)((const PagedMissingTableRow*)a)->row;
            uintptr_t right = (uintptr_t)((const PagedMissingTableRow*)b)->row;
            return left < right ? -1 : left > right ? 1 : 0;
        });
}

// the row pass when a table's layout ended: the table joins the flow's units so
// later moves break it again, and keep chains apply when it moved whole
static void paged_table_after(PagedFlowState* state, LayoutContext* lycon, ViewBlock* block) {
    PagedFlowTable* record = paged_table_get(state, block);
    PagedFlowParagraph* unit = (PagedFlowParagraph*)pool_calloc(state->composition->pool, sizeof(PagedFlowParagraph));
    if (!record || !unit) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
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

static PagedMarkSource* paged_source_mark(PagedComposition* composition, DomNode* source, bool create) {
    HashMap* sources = composition->mark_sources;
    PagedMarkSource key = {}; key.source = source;
    PagedMarkSource* mark = (PagedMarkSource*)hashmap_get(sources, &key);
    if (mark || !create) return mark;
    key.assigned_at = SIZE_MAX;
    hashmap_set(sources, &key);
    if (hashmap_oom(sources)) return nullptr;
    return (PagedMarkSource*)hashmap_get(sources, &key);
}

static PagedMarkSource* paged_flow_mark(PagedFlowState* state, DomNode* source, bool create) {
    PagedMarkSource* mark = paged_source_mark(state->composition, source, create);
    if (!mark && create) paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, source, nullptr);
    return mark;
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
        // structured generated boxes handle leaders; this hook only appends scalar text bindings.
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

static bool paged_flow_anchor_kind(PagedFlowState* state, DomElement* element, PagedAnchorKind* kind);

static bool paged_note_context_admit(PagedFlowState* state, DomElement* element) {
    bool authored = radiant_page_element(element, "note");
    bool nested = state->outside;
    for (DomElement* owner = authored ? element->parent_element() : nullptr; owner && !nested; owner = owner->parent_element())
        nested = radiant_page_element(owner, "note");
    PagedAnchorKind kind;
    if ((authored && nested) || (state->outside && element != state->detached_root &&
        paged_flow_anchor_kind(state, element, &kind) && kind == PAGED_ANCHOR_NOTE)) {
        paged_flow_fail(state, TYPESET_INVALID, element, "footnotes require the paged body flow");
        return false;
    }
    // an inline binding may never pass through the block admission hook.
    if (authored && !paged_source_style(state, element)) return false;
    DisplayValue display = resolve_display_value(element);
    if (display.outer == CSS_VALUE_NONE || display.outer == CSS_VALUE_CONTENTS ||
        layout_display_is_inline_level(display.outer) || layout_view_is_out_of_flow(element)) return true;
    for (DomElement* owner = element; owner; owner = owner->parent_element()) {
        if (radiant_page_element(owner, "note-call")) {
            paged_flow_fail(state, TYPESET_INVALID, owner, "note call requires inline material");
            return false;
        }
        if (owner != element) {
            DisplayValue parent = resolve_display_value(owner);
            // an atomic inline owns its block descendants; they are not block material in the call's line.
            if (layout_intrinsic_element_is_atomic_inline(owner) ||
                (parent.outer != CSS_VALUE_CONTENTS && !layout_display_is_inline_level(parent.outer))) break;
        }
    }
    return true;
}

void paged_element_enter(LayoutContext* lycon, DomElement* element) {
    PagedFlowState* state = lycon->paged;
    if (state->status != TYPESET_OK || !paged_note_context_admit(state, element)) return;
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
    TypesetStatus admitted = paged_source_traits_admit(state, folio);
    if (admitted != TYPESET_OK) { state->status = admitted; return ""; }
    if (state->tree->model->environment.presentation != VIEW_PRESENTATION_PAGED) {
        paged_flow_fail(state, TYPESET_INVALID, folio, "folio queries require paged presentation"); return "";
    }
    if (radiant_page_element(folio, "folio")) {
        if (page && page->label) return page->label;
        composition->reference_used = true; composition->reference_source = dom_node_ref(folio);
        // notes measure before the current pass's page counters; converge using
        // the preceding label rather than measuring an empty query as zero height.
        if (page && references && page->page_number <= references->page_label_count)
            return references->page_labels[page->page_number - 1];
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
    if (!target) {
        paged_flow_fail(state, TYPESET_INVALID, folio, "folio reference target has no page area"); return "";
    }
    const PagedTargetValue* value = target ? (const PagedTargetValue*)target->binding.get() : nullptr;
    const char* edge = folio->get_attribute("edge");
    const char* area = folio->get_attribute("area");
    size_t areas = area && !strcmp(area, "normal") ? RADIANT_QUERY_NORMAL : area && !strcmp(area, "non-blank") ? RADIANT_QUERY_NON_BLANK : RADIANT_QUERY_ALL;
    const char* text = value ? value->labels[areas][edge && !strcmp(edge, "last")] : nullptr;
    // repeated static regions have selected areas without a normal-flow page_number.
    if (!text && references->pass > 1) {
        paged_flow_fail(state, TYPESET_INVALID, folio, "folio reference target has no page area"); return "";
    }
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
    auto saved_paged = lycon->paged;
    lycon->paged = lam::up(state);
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
    DomElement* saved_root = state->detached_root; float saved_width = state->detached_width;
    state->detached_root = element; state->detached_width = width;
    layout_block(lycon, element, display);
    state->detached_root = saved_root; state->detached_width = saved_width;
    state->outside--;
    lycon->available_space = saved_space; lycon->run_mode = saved_mode;
    lycon->flex_container = saved_flex; lycon->grid_container = saved_grid;
    lycon->paged = saved_paged;
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
    if (state->tree->model->environment.presentation != VIEW_PRESENTATION_PAGED) return false;
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
        paged_folios_fill(state, element, state->fragmentainers[index].page);
        bool saved_note = state->detached_note;
        state->detached_note = kind == PAGED_ANCHOR_NOTE;
        anchor.height = paged_detached_layout(state, lycon, element, anchor.width,
            kind == PAGED_ANCHOR_NOTE ? NAN : state->fragmentainers[index].rect.height);
        state->detached_note = saved_note;
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
    if (!session) return nullptr;
    for (size_t i = 0; i < session->width_count; i++)
        if (session->widths[i].source == block) return &session->widths[i];
    bool detached = state->outside && state->detached_note;
    if (detached) {
        for (ViewBlock* owner = block; owner != state->detached_root; owner = paged_flow_parent_block(owner))
            if (!owner || owner->display.inner != CSS_VALUE_FLOW || (owner->blk && owner->block()->given_width >= 0.0f)) return nullptr;
    } else if (!paged_flow_top_level(state, block)) {
        if (paged_flow_run_holder(state, block)) return nullptr;
        // auto-width descendants share the page's width change through their containing blocks.
        ViewBlock* owner = block;
        while (owner && !paged_flow_run_holder(state, owner)) {
            if (owner->display.inner != CSS_VALUE_FLOW || (owner->blk && owner->block()->given_width >= 0.0f)) return nullptr;
            owner = paged_flow_parent_block(owner);
        }
        if (!owner) return nullptr;
    }
    if (!create) return nullptr;
    if (session->width_count >= state->composition->options.max_nodes ||
        !lam::pool_grow_array(session->pool, &session->widths, &session->width_capacity,
            session->width_count + 1, 16)) {
        paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return nullptr;
    }
    PagedFlowWidth* width = &session->widths[session->width_count++];
    *width = {block, detached ? state->detached_width : state->run_width};
    width->detached = detached;
    return width;
}

static void paged_reference_inline(LayoutContext* lycon, ViewBlock* block, BlockContext* parent) {
    PagedFlowState* state = lycon->paged;
    const ViewCssStyle* style = paged_source_style(state, block);
    if (!style) return;
    const RadiantFlowTraits* traits = style->flow_traits;
    if (!traits) return;
    // cells and detached regions establish reference areas; ordinary ancestors retain that frame.
    ViewBlock* area = nullptr;
    for (ViewBlock* owner = paged_flow_parent_block(block); owner; owner = paged_flow_parent_block(owner)) {
        if (owner->view_type == RDT_VIEW_TABLE_CELL) { area = owner; break; }
        if (owner == state->detached_root || paged_flow_run_holder(state, owner)) break;
    }
    float width = area ? area->content_width : state->outside ? state->detached_width
        : parent->paged_reference_width > 0.0f ? parent->paged_reference_width : state->run_width;
    BoxMetrics area_edges = area ? layout_box_metrics(area) : BoxMetrics{};
    float left = area_edges.border.left + area_edges.padding.left;
    View* stop = area ? (View*)area : state->outside && state->detached_root
        ? state->detached_root->parent_view() : nullptr;
    float position = view_geometry_ancestor_offset(block, stop, true).x;
    float indents[2] = {traits->indents[0], traits->indents[1]};
    for (size_t side = 0; side < 2; side++) if (const CssValue* expression = traits->indent_expressions[side]) {
        const ViewCssStyle* owner = traits->indent_owners[side];
        size_t at = 0;
        while (at < state->indent_count && state->indents[at].owner != owner) at++;
        bool measuring = layout_context_is_measuring(lycon);
        if (at == state->indent_count && !measuring) {
            if (state->indent_count >= state->composition->options.max_nodes ||
                !lam::pool_grow_array(state->composition->pool, &state->indents, &state->indent_capacity,
                    state->indent_count + 1, 16)) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
            state->indents[state->indent_count++] = {owner, width};
        }
        float basis = at < state->indent_count ? state->indents[at].width : width;
        indents[side] = view_css_length(state->tree, owner, expression, CSS_PROPERTY_MARGIN_LEFT, basis, NAN);
        if (!isfinite(indents[side])) { paged_flow_fail(state, TYPESET_INVALID, block, "reference indents require finite used lengths"); return; }
    }
    if (!traits->reference_inline) return;
    block->ensure_boundary(lycon);
    BoundaryProp* boundary = block->boundary_mut();
    if (!boundary) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
    BoxMetrics edges = layout_box_metrics(block);
    boundary->margin.left = left + indents[0] - position - edges.border.left - edges.padding.left;
    boundary->margin.right = parent->content_width - width - boundary->margin.left -
        edges.pad_border_h + indents[0] + indents[1];
}

bool paged_line_keep(LayoutContext* lycon, DomNode* node) {
    for (DomNode* current = node && node->is_element() ? node : node ? node->parent : nullptr;
            current; current = current->parent) {
        DomElement* element = current->as_element();
        if (!element) continue;
        if (element->is_synthetic()) {
            if (((View*)element)->is_block()) break;
            continue;
        }
        const ViewCssStyle* style = paged_source_style(lycon->paged, element);
        if (!style) return false;
        if (style->flow_traits && style->flow_traits->together.scope[0].kind == RADIANT_KEEP_ALWAYS) return true;
        // an atomic or block ancestor starts a different inline formatting context.
        if (style->display.outer != CSS_VALUE_INLINE && style->display.outer != CSS_VALUE_CONTENTS) break;
    }
    return false;
}

void paged_line_start(LayoutContext* lycon, ViewBlock* block) {
    PagedFlowState* state = lycon->paged;
    if (state->status != TYPESET_OK || !block ||
        (state->outside ? !state->detached_note : !paged_flow_fragmentable(lycon, block))) return;
    PagedFlowWidth* width = paged_flow_width(state, block, false);
    size_t line = lycon->block.line_number;
    if (!width || line >= width->count || !width->lines[line] ||
        (block->blk && block->block()->given_width >= 0.0f)) return;
    // the DOM box publishes content_width only when it closes; the live context owns it during wrapping.
    float content = fmaxf(0.0f, lycon->block.content_width + width->lines[line] - width->containing_width);
    lycon->line.right = lycon->line.align_right = lycon->line.left + content;
    lycon->line.effective_right = lycon->line.right;
}

void paged_line_metrics(LayoutContext* lycon) {
    PagedFlowState* state = lycon->paged;
    ViewBlock* block = lycon->line.start_view ? paged_flow_parent_block(lycon->line.start_view) : nullptr;
    if (!block || state->status != TYPESET_OK) return;
    const ViewCssStyle* style = view_css_resolve(state->tree, block);
    const RadiantFlowTraits* traits = style ? style->flow_traits.get() : nullptr;
    if (!traits || (!traits->line_stacking_set &&
        !traits->text_metrics_set[0] && !traits->text_metrics_set[1])) return;
    float altitude, depth;
    if (!radiant_text_metrics(style, &altitude, &depth)) return;
    // XSL stacking separates raw allocation from signed leading; font-height
    // deliberately permits replaced content to overflow a zero-height line.
    float leading = (lycon->block.strut_line_height - altitude - depth) * 0.5f;
    bool expanded = traits->line_stacking == RADIANT_LINE_STACK_CSS;
    float ascent = altitude + (expanded ? leading : 0.0f);
    float descent = depth + (expanded ? leading : 0.0f);
    if (traits->line_stacking != RADIANT_LINE_STACK_FONT) {
        ascent = fmaxf(ascent, lycon->line.max_atomic_inline_height);
        auto contribute = [&](View* view) {
            if (view->view_type != RDT_VIEW_TEXT) return false;
            ViewText* text = lam::view_require_text(view);
            bool on_line = false;
            for (TextRect* rect = text->rect; rect; rect = rect->next)
                on_line |= rect->line_number == lycon->block.line_number && rect->width > 0.0f;
            if (!on_line) return false;
            const ViewCssStyle* owner = text->parent && text->parent->is_element()
                ? view_css_resolve(state->tree, text->parent->as_element()) : nullptr;
            float above, below;
            if (radiant_text_metrics(owner, &above, &below)) {
                float own_leading = expanded ? (owner->line_height - above - below) * 0.5f : 0.0f;
                above += own_leading; below += own_leading;
                ascent = fmaxf(ascent, above); descent = fmaxf(descent, below);
            }
            return false;
        };
        layout_walk_inline_views(block->first_placed_child(), contribute, [](View*) {});
    }
    lycon->line.has_native_metrics = true;
    lycon->line.max_ascender = ascent + (expanded ? 0.0f : leading);
    lycon->line.max_descender = descent + (expanded ? 0.0f : leading);
}

static TypesetStatus paged_flow_width_line(PagedReferenceSession* session, PagedFlowWidth* width,
        size_t line, float measure, bool* changed = nullptr) {
    if (!lam::pool_grow_array(session->pool, &width->lines, &width->capacity, line + 1, 8))
        return TYPESET_OUT_OF_MEMORY;
    while (width->count <= line) width->lines[width->count++] = 0.0f;
    if (changed && fabsf(width->lines[line] - measure) > kPagedFlowEpsilon) *changed = true;
    width->lines[line] = measure;
    return TYPESET_OK;
}

// replay each note line at its selected area width without restarting its source or computed indents.
static TypesetStatus paged_note_widths_capture(PagedFlowState* state, PagedFlowAnchor* anchor,
        ViewBlock* block, float origin) {
    PagedReferenceSession* session = state->composition->references;
    PagedFlowWidth* width = paged_flow_width(state, block, false);
    if (width) {
        PagedFlowParagraph paragraph = {}; paragraph.block = block;
        TypesetStatus status = paged_geometry_collect(state, &paragraph, block->first_child, nullptr);
        bool changed = fabsf(width->containing_width - anchor->width) > kPagedFlowEpsilon;
        width->containing_width = anchor->width;
        size_t count = 0;
        for (size_t i = 0; status == TYPESET_OK && i < paragraph.count; i++) {
            const PagedFlowLine& line = paragraph.lines[i];
            size_t index = anchor->fragmentainer;
            for (PagedNotePiece* piece = anchor->continuation; piece; piece = piece->next)
                if (origin + line.top >= piece->offset - kPagedFlowEpsilon) index = piece->fragmentainer;
            const PagedNoteArea* area = paged_fragmentainer_area(state, index);
            if (!area) { status = state->status; break; }
            status = paged_flow_width_line(session, width, (size_t)line.number,
                area->width - area->edges[1] - area->edges[3], &changed);
            count = max(count, (size_t)line.number + 1);
        }
        if (count != width->count) changed = true;
        width->count = count;
        if (paragraph.lines) pool_free(state->composition->pool, paragraph.lines);
        if (status != TYPESET_OK) return status;
        if (changed) state->composition->reference_used = true;
    }
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        ViewBlock* descendant = lam::view_as_block((View*)child);
        if (!descendant || paged_flow_closed(descendant)) continue;
        TypesetStatus status = paged_note_widths_capture(state, anchor, descendant, origin + descendant->y);
        if (status != TYPESET_OK) return status;
    }
    return TYPESET_OK;
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
        if (width.detached || width.seen_pass != session->pass) continue;
        width.containing_width = state->fragmentainers[paged_fragmentainer_index(state,
            paged_flow_top((View*)width.source))].rect.width;
        const ViewPageBox* page = state->fragmentainers[paged_fragmentainer_index(state,
            paged_flow_top((View*)width.source))].page;
        const char* name = page ? page->name : nullptr;
        if (!paged_flow_same_name(width.master_name, name)) {
            width.master_name = name ? (const char*)pool_dup_n(session->pool, name, strlen(name)) : nullptr;
            if (name && !width.master_name) return TYPESET_OUT_OF_MEMORY;
        }
        width.count = 0;
    }
    for (PagedFlowParagraph* paragraph = state->paragraphs; paragraph; paragraph = paragraph->next) {
        PagedFlowWidth* width = paged_flow_width(state, paragraph->block, false);
        if (!width) continue;
        for (size_t i = 0; i < paragraph->count; i++) {
            const PagedFlowLine& line = paragraph->lines[i];
            TypesetStatus status = paged_flow_width_line(session, width, (size_t)line.number,
                state->fragmentainers[line.fragmentainer].rect.width);
            if (status != TYPESET_OK) return status;
        }
    }
    for (size_t i = 0; i < state->anchor_count; i++) {
        PagedFlowAnchor* anchor = &state->anchors[i];
        if (anchor->kind != PAGED_ANCHOR_NOTE || anchor->fragmentainer == kPagedUnplaced) continue;
        DomElement* element = anchor->element;
        element->view_type = (ViewType)anchor->view_type;
        TypesetStatus status = paged_note_widths_capture(state, anchor, lam::view_require_block((View*)element), element->y);
        element->view_type = RDT_VIEW_NONE;
        if (status != TYPESET_OK) return status;
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

// an empty page of the wrong side becomes blank before either a box or an anonymous inline run.
static TypesetStatus paged_forced_empty_side(PagedFlowState* state, size_t index,
        ViewBreak request, bool* has_content, DomNode* source) {
    const PagedFragmentainer& frame = state->fragmentainers[index];
    ViewPageSide wanted = VIEW_PAGE_RIGHT;
    if (*has_content || !frame.page || index + 1 != state->fragmentainer_count ||
        !paged_requested_side(&state->composer, request, &wanted) || frame.page->side == wanted) return TYPESET_OK;
    TypesetStatus status = paged_empty_page_restyle(&state->composer, true);
    if (status != TYPESET_OK) return paged_flow_fail(state, status, source, nullptr);
    paged_fragmentainer_restyle(state, index, true);
    *has_content = true;
    return TYPESET_OK;
}

// the break before `block`: a forced break, a named page or sequence, or previous
// content that filled its fragmentainer exactly
static void paged_block_break(PagedFlowState* state, LayoutContext* lycon, ViewBlock* block, BlockContext* pa_block) {
    PagedPageCursor* composer = &state->composer;
    ViewBreak request = paged_flow_box_break(state, block, false);
    if (state->status != TYPESET_OK) return;
    request = paged_break_combine(state->pending_break, request);
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
    if (forced && paged_forced_empty_side(state, index, request, &has_content, block) != TYPESET_OK) return;
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
// lines and terminal anchors share forced insertion handling, including an empty page of the wrong side.
static TypesetStatus paged_forced_insertion(PagedFlowState* state, DomNode* source, ViewBreak request,
        size_t* index, float* top) {
    if (request < VIEW_BREAK_COLUMN) return TYPESET_OK;
    bool has_content = *top > state->fragmentainers[*index].top + kPagedFlowEpsilon;
    TypesetStatus status = paged_forced_empty_side(state, *index, request, &has_content, source);
    if (status != TYPESET_OK || !has_content) return status;
    size_t target;
    status = paged_forced_target(state, *index, request, &target);
    if (status != TYPESET_OK) return status;
    *index = target; *top = state->fragmentainers[target].top;
    return TYPESET_OK;
}

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

void paged_table_cell_alignment(LayoutContext* lycon, ViewTableCell* cell) {
    PagedFlowState* state = lycon->paged;
    const ViewCssStyle* style = paged_source_style(state, cell);
    if (!style) return;
    if (style->flow_traits && style->flow_traits->relative_cell_before)
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_RELATIVE_BEFORE;
}

bool paged_table_source_admit(LayoutContext* lycon, ViewElement* source) {
    return !lycon->paged || source->is_synthetic() || paged_source_traits_admit(lycon->paged, source) == TYPESET_OK;
}

size_t paged_flow_node_limit(LayoutContext* lycon) {
    return lycon->paged ? lycon->paged->composition->options.max_nodes : SIZE_MAX;
}

void paged_flow_layout_error(LayoutContext* lycon, DomElement* source, TypesetStatus status, const char* reason) {
    if (lycon->paged) paged_flow_fail(lycon->paged, status, source, reason);
    else log_error("layout-flow-invalid: %s", reason);
}

float paged_table_column_share(LayoutContext* lycon, ViewElement* column, bool fixed_definite) {
    PagedFlowState* state = lycon->paged;
    const ViewCssStyle* style = paged_source_style(state, column);
    if (!style) return 0.0f;
    // explicit inheritance and bindings compute at the selected style, not in a raw attribute parser.
    float weight = style->flow_traits ? style->flow_traits->column_proportion : 0.0f;
    if (weight > 0.0f && !fixed_definite)
        paged_flow_fail(state, TYPESET_INVALID, column,
            "proportional columns require a fixed table with a definite inline extent");
    return weight;
}

float paged_label_body_columns(LayoutContext* lycon, ViewTable* table, TableMetadata* metadata, float width) {
    PagedFlowState* state = lycon->paged;
    if (state->status != TYPESET_OK || table->is_synthetic()) return 0.0f;
    const ViewCssStyle* style = view_css_resolve(state->tree, table);
    if (!style) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, table, nullptr); return 0.0f; }
    if (!style->label_body || !style->label_body->grid) return 0.0f;
    bool valid = metadata->column_count == 2 && metadata->row_count == 1 &&
        !table->tb->border_spacing_h && !table->tb->border_spacing_v;
    size_t cells = 0;
    table->each_cell([&](ViewTableRow*, ViewTableCell* cell) {
        cells++;
        valid &= cell->td->col_span == 1 && cell->td->row_span == 1;
    });
    table->each_direct_block([&](ViewBlock* child) {
        valid &= child->view_type != RDT_VIEW_TABLE_COLUMN && child->view_type != RDT_VIEW_TABLE_COLUMN_GROUP &&
            !table_view_is_caption(child);
    });
    if (!valid || cells != 2) {
        paged_flow_fail(state, TYPESET_INVALID, table,
            "label/body grid requires one two-cell row without columns, captions or grid spacing");
        return 0.0f;
    }
    if (layout_box_metrics(table).pad_border_h != 0.0f) {
        paged_flow_fail(state, TYPESET_INVALID, table,
            "label/body grid horizontal decoration requires reference-relative indent geometry");
        return 0.0f;
    }
    float label, separation, body;
    if (!radiant_label_body_size(radiant_label_body_geometry(style), width, &label, &separation, &body)) {
        paged_flow_fail(state, TYPESET_INVALID, table,
            "label/body geometry overlaps or leaves a nonpositive part width");
        return 0.0f;
    }
    // the separation is a gap, so neither cell's background paints it.
    metadata->col_widths[0] = label;
    metadata->col_widths[1] = body;
    return separation;
}

void paged_image_size(LayoutContext* lycon, ViewBlock* block, float natural_width, float natural_height) {
    PagedFlowState* state = lycon->paged;
    block->embed->native_image_sized = false;
    if (state->status != TYPESET_OK) return;
    const ViewCssStyle* style = view_css_resolve(state->tree, block);
    if (!style) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
    const RadiantImageSpec* spec = style->image_spec;
    if (!spec) return;
    TypesetStatus admitted = paged_style_traits_admit(state->composition, style);
    if (admitted != TYPESET_OK) { state->status = admitted; return; }
    float width = lycon->block.given_width < 0.0f ? NAN
        : layout_content_size_if_border_box(block, lycon->block.given_width, true);
    float height = lycon->block.given_height < 0.0f ? NAN
        : layout_content_size_if_border_box(block, lycon->block.given_height, false);
    float content_width, content_height;
    if (!radiant_image_size(spec, natural_width, natural_height, &width, &height, &content_width, &content_height)) {
        paged_flow_fail(state, TYPESET_UNPLACEABLE, block, "native image allowed scales cannot satisfy the content size");
        return;
    }
    lycon->block.given_width = layout_border_size_if_content_box(block, width, true);
    lycon->block.given_height = layout_border_size_if_content_box(block, height, false);
    // copy values into the workspace; its embed must not borrow an edition's style lifetime.
    block->embed->native_image_width = content_width;
    block->embed->native_image_height = content_height;
    block->embed->native_image_sized = true;
}

void paged_block_before(LayoutContext* lycon, ViewBlock* block, BlockContext* pa_block) {
    PagedFlowState* state = lycon->paged;
    if (state->outside) {
        if (state->detached_note) {
            PagedFlowWidth* width = paged_flow_width(state, block, true);
            if (width) { width->layout_width = state->detached_width; width->seen_pass = state->composition->references->pass; }
        }
        paged_reference_inline(lycon, block, pa_block); return;
    }
    if (state->status != TYPESET_OK) return;
    if (!paged_flow_block(block) || !paged_flow_fragmentable(lycon, block)) {
        paged_reference_inline(lycon, block, pa_block); return;
    }
    // reject invalid native traits before the CSS workspace approximates them as ordinary layout properties.
    // anonymous table fixups have no DOM identity or authored native traits to admit.
    TypesetStatus admitted = paged_source_traits_admit(state, block);
    if (admitted != TYPESET_OK) { state->status = admitted; return; }
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
    // a preceding body's break belongs before waiting insertion anchors, not after they bind to this box.
    bool preceding = state->anchors_bound < end && state->pending_break >= VIEW_BREAK_COLUMN;
    if (preceding) paged_block_break(state, lycon, block, pa_block);
    if (state->status != TYPESET_OK) return;
    size_t moving = state->anchors_bound < end ? paged_box_anchors(state, block, state->anchors_bound, end - state->anchors_bound) : end;
    if (state->status != TYPESET_OK) return;
    if (!preceding) paged_block_break(state, lycon, block, pa_block);
    if (state->status == TYPESET_OK && paged_space_before(state, block, pa_block) != TYPESET_OK) return;
    if (state->status == TYPESET_OK && paged_flow_top_level(state, block)) {
        PagedFlowWidth* width = paged_flow_width(state, block, true);
        if (width) {
            // retain a deferred box's first area, but discard widths belonging to a replaced master.
            const PagedFragmentainer& frame = state->fragmentainers[paged_fragmentainer_index(state, paged_flow_top(block))];
            bool replaced = !paged_flow_same_name(width->master_name, frame.page ? frame.page->name : nullptr);
            width->layout_width = !width->seen_pass || replaced ? frame.rect.width : width->containing_width;
            if (!width->seen_pass) width->containing_width = width->layout_width;
            paged_flow_run_restart(state, pa_block, width->layout_width);
            width->seen_pass = state->composition->references->pass;
        }
    }
    // layout_block saved the parent before this hook; children inherit the updated reference, not that stale copy.
    lycon->block.paged_reference_width = pa_block->paged_reference_width;
    if (!paged_flow_top_level(state, block)) {
        PagedFlowWidth* width = paged_flow_width(state, block, true);
        if (width) { width->layout_width = state->run_width; width->seen_pass = state->composition->references->pass; }
    }
    paged_reference_inline(lycon, block, pa_block);
    // style resolution preceded the run restart; percentage widths need the new containing block.
    if (block->blk && !isnan(block->block()->given_width_percent))
        layout_store_given_axis(lycon, block, pa_block->content_width * block->block()->given_width_percent * 0.01f, true);
    if (moving < end && state->status == TYPESET_OK) paged_box_anchors_break(state, block, pa_block, moving, end);
}

// anonymous blocks have no block-before hook: consume the preceding box's break at their first real line.
static TypesetStatus paged_line_break_before(PagedFlowState* state, BlockContext* owner,
        PagedFlowParagraph* paragraph, size_t line) {
    ViewBreak request = state->pending_break;
    state->pending_break = VIEW_BREAK_AUTO;
    if (request < VIEW_BREAK_COLUMN || state->tree->model->environment.presentation == VIEW_PRESENTATION_CONTINUOUS)
        return TYPESET_OK;
    float top = paged_flow_top(paragraph->block) + paragraph->lines[line].top;
    size_t index;
    TypesetStatus status = paged_fragmentainer_at(state, top, &index);
    if (status != TYPESET_OK) return status;
    size_t target = index;
    status = paged_forced_insertion(state, paragraph->block, request, &target, &top);
    if (status != TYPESET_OK || target == index) return status;
    float reserve_top, reserve_bottom;
    paged_reserve(state, paragraph->block, target, &reserve_top, &reserve_bottom);
    return paged_lines_move(state, owner, paragraph, line, line, target, reserve_top, nullptr, true);
}

static void paged_commit_line(LayoutContext* lycon, ViewBlock* block, float used_line_height,
        PagedNativeLine* native = nullptr) {
    PagedFlowState* state = lycon->paged;
    PagedFlowParagraph* paragraph = paged_paragraph_open(state, block);
    if (!paragraph || !lam::pool_grow_array(state->composition->pool, &paragraph->lines, &paragraph->capacity,
        paragraph->count + 1, 16)) { paged_flow_fail(state, TYPESET_OUT_OF_MEMORY, block, nullptr); return; }
    if (native && state->outside) {
        if (native->before.legality == TYPESET_BREAK_FORCED && native->before.scope >= TYPESET_BREAK_COLUMN) {
            paged_flow_fail(state, TYPESET_INVALID, block, "native static content cannot force an internal page break"); return;
        }
        paragraph->lines[paragraph->count++] = {lycon->block.advance_y, used_line_height, 0, lycon->block.line_number, 0, 0, native};
        return;
    }
    // the footnote calls and page floats met since the last line are anchored on this one
    uint32_t first = (uint32_t)state->anchors_bound, count = (uint32_t)(state->anchor_count - state->anchors_bound); // INT_CAST_OK: anchor counts are bounded by the element budget
    for (size_t i = state->anchors_bound; i < state->anchor_count; i++) {
        state->anchors[i].paragraph = paragraph; state->anchors[i].line = paragraph->count; state->anchors[i].box = nullptr;
    }
    state->anchors_bound = state->anchor_count;
    paragraph->lines[paragraph->count++] = {lycon->block.advance_y, used_line_height, 0, lycon->block.line_number, first, count, native};
    if (native) {
        size_t frame;
        TypesetStatus status = paged_fragmentainer_at(state, paged_flow_top(block) + lycon->block.advance_y, &frame);
        TypesetBreak boundary = {};
        if (status == TYPESET_OK) status = paged_native_boundary(state, paragraph, paragraph->count - 1, frame, &boundary);
        if (status != TYPESET_OK) { paged_flow_fail(state, status, block, "native break candidate failed"); return; }
        native->before = boundary;
        if (boundary.legality == TYPESET_BREAK_FORCED && boundary.scope >= TYPESET_BREAK_COLUMN)
            state->pending_break = boundary.scope == TYPESET_BREAK_PAGE ? VIEW_BREAK_PAGE : VIEW_BREAK_COLUMN;
    }
    TypesetStatus status = paged_line_break_before(state, &lycon->block, paragraph, paragraph->count - 1);
    if (status != TYPESET_OK) { paged_flow_fail(state, status, block, nullptr); return; }
    if (paged_space_commit(state, &lycon->block, paragraph, paragraph->count - 1) != TYPESET_OK) return;
    status = paged_fit_line(state, lycon, &lycon->block, paragraph, paragraph->count - 1, nullptr, true);
    if (status != TYPESET_OK) { paged_flow_fail(state, status, block, "page flow line could not be fitted"); return; }
    const PagedFlowLine& fitted = paragraph->lines[paragraph->count - 1];
    state->flow_end = fmaxf(state->flow_end, paged_flow_top(block) + fitted.top + fitted.height);
}

void paged_line_commit(LayoutContext* lycon, float used_line_height) {
    PagedFlowState* state = lycon->paged;
    if (state->outside || state->status != TYPESET_OK || !lycon->line.start_view) return;
    ViewBlock* block = paged_flow_parent_block(lycon->line.start_view);
    if (!block || !paged_flow_block(block) || paged_flow_closed(block) || !paged_flow_fragmentable(lycon, block)) return;
    paged_commit_line(lycon, block, used_line_height);
}

static const PagedNativeFlowBinding* paged_native_binding(const PagedLayoutOptions& options,
        const TypesetFlowProvider* provider, DomElement* source = nullptr) {
    for (size_t i = 0; i < options.native_flow_count; i++) {
        const PagedNativeFlowBinding* binding = &options.native_flows[i];
        if (source ? binding->source == source : binding->provider == provider) return binding;
    }
    return nullptr;
}

struct PagedNativePath { const PagedNativeFlowBinding* binding; const PagedNativePath* parent; };

static TypesetStatus paged_native_stream(LayoutContext* lycon, ViewBlock* block,
        const PagedNativeFlowBinding* binding, const PagedNativePath* parent, TypesetBreak* before) {
    PagedFlowState* state = lycon->paged;
    const PagedLayoutOptions& options = state->composition->options;
    size_t depth = 0;
    for (const PagedNativePath* at = parent; at; at = at->parent) {
        if (at->binding == binding) return TYPESET_INVALID;
        if (++depth >= options.max_depth) return TYPESET_BUDGET_EXHAUSTED;
    }
    PagedNativePath path = {binding, parent};
    TypesetResume saved = {}, cursor = binding->start;
    TypesetStatus status = typeset_flow_checkpoint(binding->provider, &cursor, &saved);
    if (status != TYPESET_OK) return status;
    status = typeset_flow_restore(binding->provider, &saved, &cursor);
    while (status == TYPESET_OK) {
        if (++state->native_steps > options.max_nodes) { status = TYPESET_BUDGET_EXHAUSTED; break; }
        TypesetContribution contribution = {}; TypesetResume next = {};
        status = typeset_flow_next(binding->provider, &cursor, &contribution, &next);
        if (status == TYPESET_DONE) { status = TYPESET_OK; break; }
        if (status != TYPESET_OK) break;
        if (contribution.source.provider != binding->provider->identity ||
            contribution.source.generation != binding->provider->generation ||
            contribution.source.offset_unit > TYPESET_PROVIDER_OFFSETS) { status = TYPESET_STALE; break; }
        state->composition->diagnostic.native_source = contribution.source;
        if (contribution.kind == TYPESET_CONTRIBUTION_NESTED) {
            const PagedNativeFlowBinding* child = paged_native_binding(options, contribution.nested);
            status = child && !child->source ? paged_native_stream(lycon, block, child, &path, before) : TYPESET_INVALID;
        } else if (contribution.kind == TYPESET_CONTRIBUTION_BOUNDARY) {
            *before = contribution.boundary;
        } else if (contribution.kind == TYPESET_CONTRIBUTION_MARK || contribution.kind == TYPESET_CONTRIBUTION_TARGET) {
            if (state->outside) { status = TYPESET_INVALID; break; }
            const TypesetSource* source = contribution.kind == TYPESET_CONTRIBUTION_MARK
                ? (contribution.mark ? &contribution.mark->source : nullptr)
                : (contribution.target ? &contribution.target->source : nullptr);
            if (!source || !typeset_source_same_identity(*source, contribution.source)) { status = TYPESET_INVALID; break; }
            PagedFlowParagraph* paragraph = paged_paragraph_open(state, block);
            PagedNativeEvent* event = (PagedNativeEvent*)pool_calloc(state->composition->pool, sizeof(PagedNativeEvent));
            if (!paragraph || !event) { status = TYPESET_OUT_OF_MEMORY; break; }
            *event = {binding, contribution, paragraph->count, nullptr};
            if (paragraph->last_event) paragraph->last_event->next = event; else paragraph->events = event;
            paragraph->last_event = event;
        } else if (contribution.kind == TYPESET_CONTRIBUTION_GLUE) {
            const TypesetGlue& glue = contribution.glue;
            if (!isfinite(glue.natural) || glue.natural < 0.0f || !isfinite(glue.shrink) || glue.shrink < 0.0f ||
                !isfinite(glue.stretch) || glue.stretch < 0.0f || glue.shrink_order > 3 || glue.stretch_order > 3) { status = TYPESET_INVALID; break; }
            float top = paged_flow_top(block) + lycon->block.advance_y;
            size_t frame = paged_fragmentainer_index(state, top);
            bool starting = !state->outside && (top <= state->fragmentainers[frame].top + kPagedFlowEpsilon ||
                state->pending_break >= VIEW_BREAK_COLUMN ||
                (before->legality == TYPESET_BREAK_FORCED && before->scope >= TYPESET_BREAK_COLUMN));
            PagedFlowParagraph* paragraph = paged_paragraph_open(state, block);
            if (!paragraph || !lam::pool_grow_array(state->composition->pool, &state->space_gaps, &state->space_gap_capacity,
                    state->space_gap_count + 1, 16)) { status = TYPESET_OUT_OF_MEMORY; break; }
            float used = starting && glue.discard_start ? 0.0f : glue.natural;
            TypesetGlue active = glue;
            // discarded leading glue contributes neither size nor ordered shrink capacity.
            if (starting && glue.discard_start) active.natural = active.stretch = active.shrink = 0.0f;
            state->space_gaps[state->space_gap_count++] = {nullptr, paragraph, paragraph->count, {}, used, false, true, active};
            lycon->block.advance_y += used;
        } else if (contribution.kind == TYPESET_CONTRIBUTION_BOX || contribution.kind == TYPESET_CONTRIBUTION_PARAGRAPH) {
            const TypesetParagraph* paragraph = contribution.kind == TYPESET_CONTRIBUTION_PARAGRAPH ? contribution.paragraph : nullptr;
            if (contribution.kind == TYPESET_CONTRIBUTION_PARAGRAPH && !paragraph) { status = TYPESET_INVALID; break; }
            size_t capacity = paragraph ? (paragraph->max_alternatives ? paragraph->max_alternatives : paragraph->count) : 1;
            if (!binding->material || !capacity || capacity > options.max_nodes ||
                capacity > SIZE_MAX / sizeof(TypesetLineCandidate)) { status = TYPESET_INVALID; break; }
            TypesetLineCandidate* scratch = (TypesetLineCandidate*)pool_calloc(state->composition->pool,
                capacity * sizeof(TypesetLineCandidate));
            if (!scratch) { status = TYPESET_OUT_OF_MEMORY; break; }
            size_t first = 0;
            do {
                if (++state->native_steps > options.max_nodes || lycon->block.line_number == INT_MAX) {
                    status = TYPESET_BUDGET_EXHAUSTED; break;
                }
                PagedNativeLine* native = (PagedNativeLine*)pool_calloc(state->composition->pool, sizeof(PagedNativeLine));
                if (!native) { status = TYPESET_OUT_OF_MEMORY; break; }
                native->binding = binding; native->contribution = contribution; native->resume = cursor; native->before = *before;
                if (paragraph) {
                    status = typeset_next_line(paragraph, first, lycon->block.content_width, scratch, capacity, &native->selected);
                    if (status != TYPESET_OK) break;
                    if (first && paragraph->items) native->before = paragraph->items[first].has_before
                        ? paragraph->items[first].before : paragraph->items[first - 1].boundary;
                } else {
                    const TypesetMetrics& metrics = contribution.metrics;
                    native->selected = {0, 1, 0, 1, metrics.advance, metrics.height + metrics.depth, metrics.depth, metrics.baseline};
                }
                if (!isfinite(native->selected.height) || native->selected.height <= 0.0f ||
                    !isfinite(native->selected.width) || native->selected.width < 0.0f) { status = TYPESET_INVALID; break; }
                // native lines enter the same paragraph/strut journal as ordinary inline layout.
                paged_commit_line(lycon, block, native->selected.height, native);
                status = state->status;
                if (status != TYPESET_OK) break;
                lycon->block.advance_y += native->selected.height;
                lycon->block.max_width = fmaxf(lycon->block.max_width, native->selected.width);
                lycon->block.line_number++;
                first = native->selected.next;
                *before = {};
            } while (paragraph && first < paragraph->count);
            pool_free(state->composition->pool, scratch);
        } else status = paged_flow_fail(state, TYPESET_INVALID, block,
            "native auxiliary contributions require an insertion/float adapter");
        if (status == TYPESET_OK && contribution.kind != TYPESET_CONTRIBUTION_BOUNDARY &&
            contribution.boundary.scope >= TYPESET_BREAK_COLUMN) *before = contribution.boundary;
        cursor = next;
    }
    // every exit rewinds the producer, including nested and rejected layout passes.
    TypesetResume restored = {};
    TypesetStatus rewind = typeset_flow_restore(binding->provider, &saved, &restored);
    return status == TYPESET_OK ? rewind : status;
}

bool paged_native_layout(LayoutContext* lycon, ViewBlock* block) {
    PagedFlowState* state = lycon->paged;
    const PagedNativeFlowBinding* binding = paged_native_binding(state->composition->options, nullptr, block);
    if (!binding) return false;
    if (state->status != TYPESET_OK) return true;
    if (!state->outside && !paged_flow_fragmentable(lycon, block)) {
        paged_flow_fail(state, TYPESET_INVALID, block, "native provider needs a fragmentable body flow");
        return true;
    }
    PagedFlowParagraph* saved = state->paragraph;
    if (state->outside) {
        // each static measurement replaces its workspace records; already published editions own their material.
        for (PagedFlowParagraph* paragraph = state->paragraphs; paragraph; paragraph = paragraph->next)
            if (paragraph->block == block) paragraph->block = nullptr;
        state->paragraph = nullptr;
    }
    TypesetBreak before = {};
    TypesetStatus status = paged_native_stream(lycon, block, binding, nullptr, &before);
    if (status == TYPESET_OK) for (size_t i = 0; i < state->space_gap_count; i++) {
        PagedFlowState::SpaceGap& gap = state->space_gaps[i];
        if (gap.native && gap.paragraph->block == block && gap.line == gap.paragraph->count && gap.glue.discard_end) {
            lycon->block.advance_y -= gap.used; gap.used = 0.0f;
        }
    }
    if (status == TYPESET_OK && !state->outside && before.legality == TYPESET_BREAK_FORCED && before.scope >= TYPESET_BREAK_COLUMN)
        state->pending_break = before.scope == TYPESET_BREAK_PAGE ? VIEW_BREAK_PAGE : VIEW_BREAK_COLUMN;
    if (status != TYPESET_OK) paged_flow_fail(state, status, block, "native provider could not be laid out");
    if (state->outside) state->paragraph = saved;
    return true;
}

// tables and ordinary boxes both contribute their completed extent before pending anchors drain.
static void paged_flow_box_end(PagedFlowState* state, ViewBlock* block, ViewBreak after) {
    if (state->status != TYPESET_OK) return;
    float flow_bottom = paged_flow_top(block) + block->height;
    // definite-height boxes retain their tail even when no child line reaches that fragmentainer.
    size_t tail;
    if (block->height > 0.0f && paged_fragmentainer_at(state, flow_bottom - kPagedFlowEpsilon, &tail) != TYPESET_OK) return;
    state->flow_end = fmaxf(state->flow_end, flow_bottom);
    state->pending_break = after >= VIEW_BREAK_COLUMN ? after : VIEW_BREAK_AUTO;
}

void paged_block_after(LayoutContext* lycon, ViewBlock* block) {
    PagedFlowState* state = lycon->paged;
    if (state->outside || state->status != TYPESET_OK || !state->fragmentainer_count || !paged_flow_block(block) ||
        !paged_flow_fragmentable(lycon, block)) return;
    BlockContext* owner = &lycon->block;
    size_t since = (size_t)lycon->depth < state->open_capacity ? state->open_struts[lycon->depth] : state->strut_count;
    if (state->paragraph && state->paragraph->block == block && paged_paragraph_close(state, lycon, owner, block) != TYPESET_OK) return;
    state->paragraph = nullptr;
    if (paged_space_after(state, block, owner) != TYPESET_OK) return;
    ViewBreak after = paged_flow_box_break(state, block, true);
    if (state->status != TYPESET_OK) return;
    if (paged_flow_is_table(block)) {
        // tables break between rows after layout_table placed them whole
        paged_table_after(state, lycon, block);
        paged_flow_box_end(state, block, after);
        return;
    }
    float top = paged_flow_top(block), bottom = top + block->height;
    size_t index = paged_fragmentainer_index(state, top);
    float fragmentainer_top = state->fragmentainers[index].top, fragmentainer_bottom = state->fragmentainers[index].bottom;
    // the box ends above the footnotes and page floats placed on its fragmentainer
    float end = fragmentainer_bottom - paged_fragmentainer_reserved(state, index);
    RadiantKeepSpec keep = paged_inside_keep(state, block);
    bool page_keep = keep.scope[2].kind != RADIANT_KEEP_AUTO && keep.scope[1].kind == RADIANT_KEEP_AUTO;
    bool kept_on_page = page_keep && state->fragmentainers[paged_fragmentainer_index(state,
        bottom - kPagedFlowEpsilon)].page == state->fragmentainers[index].page;
    if (paged_flow_monolithic(state, block) && !kept_on_page && isfinite(fragmentainer_bottom) && bottom > end + kPagedFlowEpsilon) {
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
        paged_box_anchor_reservations(state, block, next, &within, &there, page_keep);
        float capacity = paged_keep_capacity(state, next, page_keep, within - there);
        if (state->status != TYPESET_OK) return;
        bool starts = top <= fragmentainer_top + kPagedFlowEpsilon &&
            (!page_keep || state->fragmentainers[index].column == 0);
        if (forced_inside) {
            // an authored break inside the box overrides its avoidance (CSS Break §4.3)
        } else if (starts || block->height - inner > capacity + kPagedFlowEpsilon) {
            if (paged_flow_avoids_inside(state, block)) state->relaxed_avoidance++;
        } else {
            float resolved_height = block->height, resolved_content_height = block->content_height;
            paged_undo_struts(state, owner, block, since);
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
    if (!paged_flow_monolithic(state, block) && reserved > 0.0f && isfinite(last_bottom) &&
        box_bottom > last_bottom - reserved + kPagedFlowEpsilon && box_bottom <= last_bottom + kPagedFlowEpsilon) {
        size_t next;
        if (paged_fragmentainer_at(state, last_bottom + kPagedFlowEpsilon, &next) != TYPESET_OK) return;
        paged_flow_grow(owner, block, true, block, reserved);
    }
    paged_flow_box_end(state, block, after);
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
    if (state->outside) return false;
    PagedAnchorKind kind;
    if (!paged_flow_anchor_kind(state, element, &kind)) return false;
    paged_flow_anchor(state, lycon, element, kind);
    return true;
}

static void paged_flow_opening(PagedFlowState* state) {
    PagedPageCursor* composer = &state->composer;
    composer->page_name = nullptr; composer->sequence = nullptr;
    const RadiantPageSequence* opening = state->tree->model->css->page_document->sequences;
    if (opening && !opening->implicit) { composer->sequence = opening; composer->page_name = opening->master_reference; }
    for (DomElement* element = state->tree->model->document->root; element && !composer->page_name; ) {
        composer->page_name = paged_flow_page_name(state, element);
        DomNode* child = element->first_child;
        while (child && !child->is_element()) child = child->next_sibling;
        element = child ? child->as_element() : nullptr;
    }
    // viewport selection and condition restarts must use the same terminal extent as page creation.
    composer->terminal_page = paged_flow_terminal_page(state, composer->sequence);
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
    state->pending_space_count = state->space_box_count = state->space_gap_count = 0;
    state->space_open = kPagedUnplaced;
    state->anchor_count = state->anchors_bound = state->notes_waiting = 0;
    // condition passes must bind against their newly selected areas, not a speculative predecessor.
    state->indent_count = 0;
    state->draining = false; state->flow_end = 0.0f;
    state->native_steps = 0;
    // fixed sheets are composed at their own viewports; the document shell owns no flow page.
    state->outside = state->tree->model->css->page_document->fixed_count ? 1 : 0;
    // the hooks record the pass's mark sources again
    hashmap_clear(state->composition->mark_sources, false);
    PagedPageCursor* composer = &state->composer;
    composer->page = nullptr;
    // restarting a condition pass must retain the opening master chosen for the viewport.
    paged_flow_opening(state);
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
    ViewBlock box = view_with_boundary(source, boundary);
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
static bool paged_publish_admit(PagedFlowState* state, DomNode* source) {
    if (state->tree->model->node_count >= state->composition->options.max_nodes) {
        paged_flow_fail(state, TYPESET_BUDGET_EXHAUSTED, source, "published fragments exceed the node budget");
        return false;
    }
    return true;
}

static LayoutViewNode* paged_publish_append(PagedFlowState* state, LayoutViewNode* parent, View* view,
        RdtLogicalRect rect, size_t text_start = 0, size_t text_length = 0) {
    if (!paged_publish_admit(state, view)) return nullptr;
    DomNode* source = view;
    // pseudo-text belongs to its origin; anonymous table fixups still contain authored text.
    if (source->is_text() && source->parent && source->parent->is_element() &&
        source->parent->as_element()->is_synthetic() && !source->parent->as_element()->is_table_fixup())
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

// internal table backgrounds paint under cells; their borders enter the collapsed
// grid conflict solver rather than painting independent boxes (CSS 2.2 §17.5–17.6).
static bool paged_table_layer(const ViewBlock* block) {
    return block->view_type == RDT_VIEW_TABLE_ROW || block->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
        block->view_type == RDT_VIEW_TABLE_COLUMN || block->view_type == RDT_VIEW_TABLE_COLUMN_GROUP;
}

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
    bool table_layer = paged_table_layer(block);
    bool collapsed_table = block->view_type == RDT_VIEW_TABLE && block->tb && block->tb->border_collapse;
    // the collapsed grid's cells own resolved edge paint; its authored border is only a solver input.
    const BorderProp* source_border = table_layer || collapsed_table ? nullptr : source->border.get();
    if (!table_layer && !collapsed_table) storage->boundary.padding = source->padding;
    if (source_border) {
        storage->border = *source_border;
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
            storage->border.styles[i] = side ? side->style : CSS_VALUE_NONE;
            if (painted) storage->border.colors[i] = side->color;
            node->border_outset[i] = painted ? side->width * 0.5f : 0.0f;
        }
        storage->boundary.border = lam::own(&storage->border);
    }
    if (collapsed_table) storage->boundary.border = lam::own(&storage->border);
    bool clone = block->blk && block->block()->box_decoration_break == CSS_VALUE_CLONE;
    bool has_border = source_border || collapsed || collapsed_table;
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
    boundary.border = nullptr;
    BorderProp border = source_border ? *source_border : BorderProp{};
    if (has_border) {
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
    box->overflow_visible = block->embed && block->embedp()->native_image_sized &&
        (!block->scroller || (block->scroll()->overflow_x == CSS_VALUE_VISIBLE &&
            block->scroll()->overflow_y == CSS_VALUE_VISIBLE));
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
    lam::Temp<char> content(render_text_rect_glyph_run(text, rect, &storage->font, color, block_x, block_y, &storage->run,
        false, nullptr, radiant_text_whitespace(tree, text)));
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
        float middle = top + rect->height * 0.5f;
        size_t index = paged_fragmentainer_index(state, middle);
        const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
        // continuation windows own a glyph once even when its ink crosses the line edge.
        if (state->publish_role == VIEW_FRAGMENT_NOTE &&
            (middle < fragmentainer.top - kPagedFlowEpsilon || middle >= fragmentainer.end - kPagedFlowEpsilon)) continue;
        RdtLogicalRect local = {fragmentainer.rect.x + origin_x + rect->x, fragmentainer.rect.y + (top - fragmentainer.top),
            rect->width, rect->height};
        int start, length;
        // line-end suppression changes paint, while the fragment still owns those source bytes.
        text_rect_source_range(rect, &start, &length);
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
    run->baseline_y = render_marker_baseline_y(prop, page_y);
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
    bool paint;
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
    node->first_fragment = first; node->last_fragment = last; node->paint_box = input->paint;
    if (!input->paint) return true;
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

enum PagedChildSelection : uint8_t { PAGED_CHILD_ALL, PAGED_CHILD_GRID, PAGED_CHILD_BEFORE, PAGED_CHILD_AFTER };
static TypesetStatus paged_publish_children(PagedFlowState* state, ViewElement* parent, float origin_x, float origin_y,
        const PagedParentMap* parents, PagedChildSelection selection = PAGED_CHILD_ALL) {
    for (DomNode* node = parent->first_child; node; node = node->next_sibling) {
        View* child = (View*)node;
        if (selection != PAGED_CHILD_ALL) {
            ViewBlock* box = lam::view_as_block(child);
            bool caption = box && table_view_is_caption(box);
            if (selection == PAGED_CHILD_GRID ? caption : !caption ||
                table_caption_is_bottom(box) != (selection == PAGED_CHILD_AFTER)) continue;
        }
        // a display:none subtree or a running element binds its assignments where the walk passes it
        if (!child->view_type && node->is_element()) paged_publish_mark(state, node, true);
        if (!child->view_type || child == state->publish_skip) continue;
        // inline/replaced and table-internal sources bypass the block admission hook.
        // validate them after layout has settled its DOM epoch, before publishing an edition.
        if (node->is_element() && !node->as_element()->is_synthetic()) {
            TypesetStatus admitted = paged_source_traits_admit(state, node->as_element());
            if (admitted != TYPESET_OK) return admitted;
            // native documents require their graphic resource; a failed load must not publish an empty area.
            if (node->as_element()->tag() == MARKUP_NAME_IMG &&
                radiant_page_element(state->tree->model->document->root, "page-document") &&
                !render_media_image_surface(lam::view_as_block(child)))
                return paged_flow_fail(state, TYPESET_UNPLACEABLE, node,
                    "image source is unavailable or requires a sampled media producer");
        }
        if (child->view_type == RDT_VIEW_TABLE_COLUMN || child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
            size_t first = 0, span = 0;
            // span-qualified property definitions have no geometric column box to publish.
            if (!layout_table_column_range(lam::view_require_element(child), &first, &span)) continue;
        }
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
            PagedInlinePaint input = {state, span, origin_x, origin_y, parents, TYPESET_OK, true};
            auto publish = [&](bool paint) {
                input.paint = paint;
                if (!layout_inline_visit_fragments(span, paged_publish_inline_fragment, &input))
                    status = input.status == TYPESET_OK ? TYPESET_OUT_OF_MEMORY : input.status;
            };
            if (span->bound) publish(true);
            // empty inlines retain insertion positions independently of any zero-width decoration paint.
            if (status == TYPESET_OK && span->width == 0.0f && layout_span_children_have_no_line_content(span)) publish(false);
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

// the settled break journal includes each table's private row/line breaks.
static const PagedStrut* paged_slice_break(const PagedFlowState* state, ViewBlock* block,
        size_t index, const PagedFlowTable** table = nullptr) {
    if (table) *table = nullptr;
    for (size_t i = 0; i < state->strut_count; i++) {
        const PagedStrut& entry = state->struts[i];
        size_t count = entry.table ? entry.table->strut_count : 1;
        for (size_t j = 0; j < count; j++) {
            const PagedStrut& strut = entry.table ? entry.table->struts[j] : entry;
            if (strut.fragmentainer != index + 1 || !paged_view_inside(strut.view, block)) continue;
            if (table) *table = entry.table;
            return &strut;
        }
    }
    return nullptr;
}

// a box with cloned edges ends its slice on fragmentainer `index` after the content
// the break left there, plus the cloned bottom edges of the boxes between
static float paged_clone_slice_end(const PagedFlowState* state, ViewBlock* block, size_t index) {
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    float retained_top, retained_bottom;
    paged_box_clone_edges(state, block, &retained_top, &retained_bottom);
    if (retained_top == 0.0f && retained_bottom == 0.0f) return fragmentainer.end;
    const PagedStrut* strut = paged_slice_break(state, block, index);
    return strut ? fminf(paged_strut_slice_end(state, *strut, block, true), fragmentainer.end) : fragmentainer.end;
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
        layout_table_each_column(table, [&](ViewElement* source, size_t first, size_t span, bool) {
            bool column_group = source->view_type == RDT_VIEW_TABLE_COLUMN_GROUP;
            if (status == TYPESET_OK && column_group == (pass == 0) && column >= first && column - first < span)
                status = paged_publish_cell_layer(state, parent, rect, lam::view_require_block(source));
        });
    }
    if (status == TYPESET_OK && group && group->view_type == RDT_VIEW_TABLE_ROW_GROUP)
        status = paged_publish_cell_layer(state, parent, rect, group);
    if (status == TYPESET_OK) status = paged_publish_cell_layer(state, parent, rect, lam::view_require_block(row));
    return status;
}

static TypesetStatus paged_publish_table_range(PagedFlowState* state, LayoutViewNode* node,
        size_t column, size_t span, bool missing = false) {
    ViewTableRange* range = (ViewTableRange*)arena_alloc(state->tree->model->arena, sizeof(ViewTableRange));
    if (!range) return TYPESET_OUT_OF_MEMORY;
    *range = {column, span, missing};
    node->table_range = lam::up((const ViewTableRange*)range);
    return TYPESET_OK;
}

static TypesetStatus paged_publish_cell_range(PagedFlowState* state, LayoutViewNode* node, ViewBlock* block) {
    const TableCellProp* cell = static_cast<ViewTableCell*>(block)->cell();
    return cell ? paged_publish_table_range(state, node,
        (size_t)(cell->col_index > 0 ? cell->col_index : 0), (size_t)(cell->col_span > 1 ? cell->col_span : 1)) : TYPESET_OK;
}

static TypesetStatus paged_publish_column_range(PagedFlowState* state, LayoutViewNode* node, ViewBlock* block) {
    size_t first = 0, span = 0;
    if (!layout_table_column_range(block, &first, &span)) return TYPESET_OK;
    return paged_publish_table_range(state, node, first, span);
}

static TypesetStatus paged_publish_missing_cells(PagedFlowState* state, ViewBlock* row, const PagedParentMap& parents) {
    const PagedFlowTable* table = state->publish_grid;
    if (!table || !table->missing_row_count) return TYPESET_OK;
    size_t first = 0, end = table->missing_row_count;
    uintptr_t source = (uintptr_t)row;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        if ((uintptr_t)table->missing_rows[middle].row < source) first = middle + 1;
        else end = middle;
    }
    if (first == table->missing_row_count || table->missing_rows[first].row != row) return TYPESET_OK;
    const PagedMissingTableRow& slots = table->missing_rows[first];
    for (size_t page = 0; page < parents.count; page++) {
        LayoutViewNode* parent = parents.nodes[page];
        if (!parent) continue;
        for (size_t i = 0; i < slots.count; i++) {
            const PagedMissingTableSlot& slot = table->missing[slots.first + i];
            if (!paged_publish_admit(state, row)) return state->status;
            RdtLogicalRect rect = {parent->rect.x + slot.x, parent->rect.y, slot.width, parent->rect.height};
            LayoutViewNode* node = view_tree_fragment_append(state->tree, parent, nullptr, rect);
            if (!node) return TYPESET_OUT_OF_MEMORY;
            node->generated = true; node->role = state->publish_role;
            node->display_inner = CSS_VALUE_TABLE_CELL;
            node->first_fragment = parent->first_fragment; node->last_fragment = parent->last_fragment;
            // missing slots carry geometry and budgets; table background shows through them.
            TypesetStatus status = paged_publish_table_range(state, node, slot.column, 1, true);
            if (status != TYPESET_OK) return status;
        }
    }
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
        if (index >= record->first && index < last) *slice_bottom = fminf(bottom, ends[index - record->first] + paged_table_fragment_insets(state, record).after);
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
    if (body && index > table->first) *slice_top = fmaxf(top, fragmentainer.top + paged_table_fragment_insets(state, table).before);
}

// a box without child views is content of its own, as are atomic inlines and replaced boxes
static void paged_publish_parent_window(const PagedFlowState* state, ViewBlock* block,
        const LayoutViewNode* parent, size_t index, size_t first, size_t last, RdtLogicalRect* rect) {
    ViewBlock* owner = paged_flow_parent_block(block);
    if (!owner || !parent || parent->kind != LAYOUT_VIEW_FRAGMENT) return;
    bool start = index > first && !parent->first_fragment && paged_slice_break(state, owner, index - 1);
    bool end = index < last && !parent->last_fragment && paged_slice_break(state, owner, index);
    if (!start && !end) return;
    Rect window = {parent->rect.x - parent->border_outset[3], parent->rect.y - parent->border_outset[0],
        parent->rect.width + parent->border_outset[1] + parent->border_outset[3],
        parent->rect.height + parent->border_outset[0] + parent->border_outset[2]};
    const BoundaryProp* boundary = parent->computed_boundary;
    if (boundary) window = render_geometry_adjust_box_rect(window, CSS_VALUE_CONTENT_BOX, 1.0f,
        boundary->border, &boundary->padding);
    // only retained block-axis decorations add a continuation window to visible-overflow boxes.
    if (window.y == parent->rect.y && window.height == parent->rect.height) return;
    float bottom = rect->y + rect->height;
    // committed flow breaks reserve the parent's decoration window for continued child boxes.
    if (start) rect->y = fmaxf(rect->y, window.y);
    if (end) bottom = fminf(bottom, window.y + window.height);
    rect->height = fmaxf(0.0f, bottom - rect->y);
}

static bool paged_publish_leaf(const ViewBlock* block) {
    if (block->display.inner == RDT_DISPLAY_REPLACED || block->display.outer == CSS_VALUE_INLINE_BLOCK) return true;
    for (const DomNode* child = block->first_child; child; child = child->next_sibling)
        if (((const View*)child)->view_type) return false;
    return block->height > 0.0f;
}

static TypesetStatus paged_publish_block(PagedFlowState* state, ViewBlock* block, float origin_x, float origin_y,
        const PagedParentMap* parents, bool root);

static TypesetStatus paged_native_event_publish(PagedFlowState* state, const PagedNativeEvent* event,
        size_t index, LayoutViewNode* parent) {
    const TypesetContribution& contribution = event->contribution;
    const PagedFragmentainer& area = state->fragmentainers[index];
    uint32_t page = area.page ? area.page->page_number : 0;
    ViewNativeMaterial lease = {}; lease.source = contribution.source; lease.owner = event->binding->owner;
    if (!paged_publish_admit(state, nullptr) || !view_tree_native_fragment_append(state->tree, parent, &lease,
            {area.rect.x, area.rect.y, 0, 0})) return TYPESET_INVALID;
    if (contribution.kind == TYPESET_CONTRIBUTION_MARK) {
        TypesetMark mark = *contribution.mark;
        if (mark.kind == TYPESET_MARK_RUNNING) return TYPESET_INVALID;
        mark.page_number = page; mark.at_page_start = state->content_page != page;
        return typeset_mark_append(&state->composition->marks, &mark);
    }
    TypesetTarget target = *contribution.target;
    const TypesetTarget* declared = typeset_target_find(&state->composition->references->targets, target.name);
    if (!declared || !typeset_source_same_identity(declared->source, target.source)) return TYPESET_INVALID;
    target.page_number = target.last_page_number = page;
    return typeset_target_append(&state->composition->native_targets, &target);
}

static TypesetStatus paged_native_publish(PagedFlowState* state, ViewBlock* block,
        float left, float top, const PagedParentMap* parents) {
    if (!state->composition->options.native_flow_count) return TYPESET_OK;
    for (PagedFlowParagraph* paragraph = state->paragraphs; paragraph; paragraph = paragraph->next) {
        if (paragraph->block != block) continue;
        PagedNativeEvent* event = paragraph->events;
        for (size_t line = 0; line < paragraph->count; line++) {
            const PagedFlowLine& placed = paragraph->lines[line];
            const PagedNativeLine* native = placed.native;
            if (!native) continue;
            size_t first = paged_fragmentainer_index(state, top + placed.top);
            size_t last = paged_fragmentainer_index(state, top + placed.top + placed.height - kPagedFlowEpsilon);
            for (; event && event->line == line; event = event->next) {
                TypesetStatus status = paged_native_event_publish(state, event, first, paged_publish_parent(state, parents, first));
                if (status != TYPESET_OK) return status;
            }
            for (size_t part = 0; part < state->composition->options.max_nodes; part++) {
                ViewNativeMaterial material = {}; RdtLogicalRect relative = {};
                TypesetStatus status = native->binding->material(native->binding->context, &native->contribution,
                    &native->selected, part, &material, &relative);
                if (status == TYPESET_DONE) break;
                if (status != TYPESET_OK) return status;
                if (material.source.provider != native->binding->provider->identity ||
                    material.source.generation != native->binding->provider->generation) return TYPESET_STALE;
                material.owner = native->binding->owner;
                if (!material.solution) material.solution = native->selected.solution;
                for (size_t index = first; index <= last; index++) {
                    const PagedFragmentainer& area = state->fragmentainers[index];
                    float x = area.rect.x + left + layout_axis_decoration_start(block->bound, LAYOUT_AXIS_X);
                    float y = area.rect.y + top + placed.top - area.top;
                    if (!paged_publish_admit(state, block)) return state->status;
                    ViewModelStatus model_status;
                    LayoutViewNode* node = view_tree_native_fragment_append(state->tree,
                        paged_publish_parent(state, parents, index), &material,
                        {x + relative.x, y + relative.y, relative.width, relative.height}, &model_status);
                    if (!node) return model_status == VIEW_MODEL_OUT_OF_MEMORY ? TYPESET_OUT_OF_MEMORY : TYPESET_INVALID;
                    if (node->glyph_run) { node->glyph_run->x += x; node->glyph_run->baseline_y += y; }
                    if (node->image_box) {
                        node->image_box->content_rect.x += x; node->image_box->content_rect.y += y;
                        node->image_box->image_rect.x += x; node->image_box->image_rect.y += y;
                    }
                    node->role = state->publish_role; node->first_fragment = index == first; node->last_fragment = index == last;
                    status = paged_publish_content(state, index);
                    if (status != TYPESET_OK) return status;
                }
                if (part + 1 == state->composition->options.max_nodes) return TYPESET_BUDGET_EXHAUSTED;
            }
        }
        size_t tail = paged_fragmentainer_index(state, top + (paragraph->count
            ? paragraph->lines[paragraph->count - 1].top : 0.0f));
        for (; event; event = event->next) {
            TypesetStatus status = paged_native_event_publish(state, event, tail, paged_publish_parent(state, parents, tail));
            if (status != TYPESET_OK) return status;
        }
    }
    return TYPESET_OK;
}

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
    const PagedFlowTable* grid_record = paged_flow_is_table(block) ? paged_table_find(state, block) : nullptr;
    const PagedFlowTable* record = grid_record && grid_record->modified ? grid_record : nullptr;
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
    bool table = block->view_type == RDT_VIEW_TABLE;
    PagedParentMap grid_nodes = {first, mine.count, nullptr};
    if (table) {
        grid_nodes.nodes = (LayoutViewNode**)pool_calloc(pool, mine.count * sizeof(LayoutViewNode*));
        if (!grid_nodes.nodes) { pool_free(pool, mine.nodes); return TYPESET_OUT_OF_MEMORY; }
    }
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
            width += fragmentainer.rect.width - (measured ? measured->layout_width : state->fragmentainers[0].rect.width);
        }
        else if ((state->publish_role == VIEW_FRAGMENT_BODY || state->publish_role == VIEW_FRAGMENT_NOTE) &&
                (!block->blk || block->block()->given_width < 0.0f)) {
            PagedFlowWidth* measured = paged_flow_width(state, block, false);
            if (measured) width += fragmentainer.rect.width - measured->layout_width;
        }
        RdtLogicalRect rect = paged_publish_rect(fragmentainer, left, slice_top, width, slice_bottom);
        if (!root) paged_publish_parent_window(state, block, parent, index, first, last, &rect);
        if (block->view_type == RDT_VIEW_TABLE_CELL) {
            status = paged_publish_cell_layers(state, parent, rect, block);
            if (status != TYPESET_OK) break;
        }
        LayoutViewNode* node = paged_publish_append(state, parent, block, rect);
        if (!node) { status = TYPESET_OUT_OF_MEMORY; break; }
        node->first_fragment = index == first; node->last_fragment = index == last;
        if (root) state->fragmentainers[index].root = node;
        mine.nodes[index - first] = node;
        if (!table) {
            RdtLogicalRect complete = {fragmentainer.rect.x + left, fragmentainer.rect.y + top - fragmentainer.top, width, block->height};
            status = paged_publish_boundary(state, node, block, node->first_fragment, node->last_fragment, !paged_table_layer(block), &complete);
        }
        if (status == TYPESET_OK && block->view_type == RDT_VIEW_TABLE_CELL) status = paged_publish_cell_range(state, node, block);
        if (status == TYPESET_OK && (block->view_type == RDT_VIEW_TABLE_COLUMN || block->view_type == RDT_VIEW_TABLE_COLUMN_GROUP))
            status = paged_publish_column_range(state, node, block);
        if (status == TYPESET_OK && block->display.inner == RDT_DISPLAY_REPLACED)
            status = paged_publish_image(state, node, block, origin_x, origin_y, fragmentainer);
        if (status == TYPESET_OK && index == first && !root && paged_publish_leaf(block) &&
            !paged_native_binding(state->composition->options, nullptr, block)) status = paged_publish_content(state, index);
    }
    if (table) {
        // captions are wrapper children in visual order; rows and columns belong to the decorated grid.
        if (status == TYPESET_OK) status = paged_publish_children(state, block, left, top, &mine, PAGED_CHILD_BEFORE);
        for (size_t index = first; status == TYPESET_OK && index <= last; index++) {
            const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
            LayoutViewNode* node = mine.nodes[index - first];
            RdtLogicalRect rect = node->rect;
            paged_publish_slice(state, block, index, last, top, bottom, record, ends, &slice_top, &slice_bottom);
            // captions belong to the wrapper; only the grid owns table borders and background.
            RdtLogicalRect grid = layout_table_grid_rect(static_cast<ViewTable*>(block));
            float grid_top = top + grid.y, grid_bottom = grid_top + grid.height;
            float begin = fmaxf(grid_top, fmaxf(slice_top, fragmentainer.top));
            float end = fminf(grid_bottom, fminf(slice_bottom, fragmentainer.end));
            if (end > begin) {
                LayoutViewNode* box = paged_publish_append(state, node, block,
                    {rect.x + grid.x, fragmentainer.rect.y + begin - fragmentainer.top, grid.width, end - begin});
                if (!box) { status = TYPESET_OUT_OF_MEMORY; break; }
                grid_nodes.nodes[index - first] = box;
                box->generated = true;
                box->first_fragment = begin <= grid_top + kPagedFlowEpsilon;
                box->last_fragment = end >= grid_bottom - kPagedFlowEpsilon;
                RdtLogicalRect complete = {rect.x + grid.x, fragmentainer.rect.y + grid_top - fragmentainer.top, grid.width, grid.height};
                status = paged_publish_boundary(state, box, block, box->first_fragment, box->last_fragment, true, &complete);
            }
        }
    }
    const PagedParentMap* content_parents = table ? &grid_nodes : &mine;
    PagedChildSelection content_selection = table ? PAGED_CHILD_GRID : PAGED_CHILD_ALL;
    const PagedFlowTable* saved_grid = state->publish_grid;
    if (table) state->publish_grid = grid_record;
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
        if (status == TYPESET_OK) status = paged_publish_children(state, block, left, top, content_parents, content_selection);
        state->publish_table = saved_table; state->publish_ends = saved_ends; state->publish_last = saved_last;
        state->publish_skip = saved_skip;
        ViewFragmentRole saved_role = state->publish_role;
        state->publish_role = VIEW_FRAGMENT_REPEATED_TABLE;
        for (size_t index = record->first + 1; status == TYPESET_OK && record->header && index <= last; index++)
            status = paged_publish_block(state, record->header, left,
                state->fragmentainers[index].top + record->header_offset - record->header->y, content_parents, false);
        for (size_t index = record->first; status == TYPESET_OK && record->footer && index < last; index++)
            status = paged_publish_block(state, record->footer, left,
                ends[index - record->first] + record->spacing - record->footer->y, content_parents, false);
        state->publish_role = saved_role;
        if (status == TYPESET_OK && record->footer) status = paged_publish_block(state, record->footer, left, top, content_parents, false);
    } else if (status == TYPESET_OK) status = paged_publish_children(state, block, left, top, content_parents, content_selection);
    if (status == TYPESET_OK) status = paged_native_publish(state, block, left, top, &mine);
    if (status == TYPESET_OK && block->view_type == RDT_VIEW_TABLE_ROW)
        status = paged_publish_missing_cells(state, block, mine);
    if (table && status == TYPESET_OK) status = paged_publish_children(state, block, left, top, &mine, PAGED_CHILD_AFTER);
    state->publish_grid = saved_grid;
    if (grid_nodes.nodes) pool_free(pool, grid_nodes.nodes);
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
    if (from < end) {
        ViewBreak request = state->pending_break;
        state->pending_break = VIEW_BREAK_AUTO;
        // no later line or box consumes a terminal anchor's insertion boundary.
        TypesetStatus status = paged_forced_insertion(state, state->anchors[from].element, request, &last, &content_end);
        if (status != TYPESET_OK) return status;
        flow_end = content_end;
    }
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

static float paged_body_alignment(const PagedFlowState* state, ViewBlock* root, size_t index) {
    const PagedFragmentainer& fragmentainer = state->fragmentainers[index];
    const ViewPageStyle* style = fragmentainer.page ? fragmentainer.page->style.get() : nullptr;
    const RadiantPageRegion* region = style ? style->body_region.get() : nullptr;
    if (!region || region->align == RADIANT_REGION_ALIGN_BEFORE) return 0.0f;
    float end = state->flow_end;
    const PagedFlowTable* table = nullptr;
    if (const PagedStrut* strut = paged_slice_break(state, root, index, &table)) {
        // a forced break's empty tail is not content; closing edges and repeated footers are.
        end = paged_strut_slice_end(state, *strut, root, true);
        if (table) {
            end += table->footer_size;
            if (strut->line < 0 && (strut->view->view_type == RDT_VIEW_TABLE_ROW ||
                strut->view->view_type == RDT_VIEW_TABLE_ROW_GROUP)) end -= table->spacing;
        }
    }
    float free = fmaxf(0.0f, fragmentainer.end - fmaxf(fragmentainer.top, end));
    return free * (region->align == RADIANT_REGION_ALIGN_CENTER ? 0.5f : 1.0f);
}

static TypesetStatus paged_flow_publish(PagedFlowState* state, LayoutContext* lycon) {
    ViewBlock* root = paged_flow_root(state);
    if (!root) return paged_flow_fail(state, TYPESET_INVALID, nullptr, "the page flow produced no root box");
    // the root does not pass through the child block or inline admission hooks.
    TypesetStatus admitted = paged_source_traits_admit(state, root);
    if (admitted != TYPESET_OK) return admitted;
    // the body ends above the footnotes and page floats placed on each
    // fragmentainer, and its top floats move it down (plan P5)
    for (size_t i = 0; i < state->fragmentainer_count; i++) {
        PagedFragmentainer& fragmentainer = state->fragmentainers[i];
        fragmentainer.end = fragmentainer.bottom - paged_fragmentainer_reserved(state, i);
        fragmentainer.alignment = paged_body_alignment(state, root, i);
        fragmentainer.rect.y += fragmentainer.float_top + fragmentainer.alignment;
    }
    state->pending_count = state->open_target_count = 0; state->content_page = 0;
    state->fixed_boxes = state->last_fixed_box = nullptr;
    TypesetStatus status = paged_publish_block(state, root, 0.0f, 0.0f, nullptr, true);
    // body alignment must not translate the independently positioned floats and footnote area.
    for (size_t i = 0; i < state->fragmentainer_count; i++)
        state->fragmentainers[i].rect.y -= state->fragmentainers[i].alignment;
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
                // each occurrence measures its own page label before intrinsic sizing.
                paged_folios_fill(state, elements[i], page);
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
    bool continued = anchor->continuation != nullptr;
    if (lycon && (folio || (!continued && fabsf(slot.width - anchor->width) > kPagedFlowEpsilon))) {
        log_debug("[PAGED_FLOW] %s lays out again at width %.3f (measured at %.3f)", element->source_loc(), slot.width, anchor->width);
        auto counters = lycon->counter_context;
        lycon->counter_context = nullptr;
        bool saved_note = state->detached_note;
        state->detached_note = anchor->kind == PAGED_ANCHOR_NOTE;
        float width = continued ? anchor->width : slot.width;
        paged_detached_layout(state, lycon, element, width,
            anchor->kind == PAGED_ANCHOR_NOTE ? NAN : state->fragmentainers[anchor->fragmentainer].rect.height);
        state->detached_note = saved_note;
        lycon->counter_context = counters;
        anchor->width = width; anchor->view_type = element->view_type;
    }
    // the element left the flow: its view takes its type only while it publishes
    element->view_type = (ViewType)anchor->view_type;
    TypesetStatus status;
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
        PagedTerminalPage* terminal = paged_terminal_record(state, sequence);
        if (!terminal) return TYPESET_OUT_OF_MEMORY;
        log_debug("[PAGED_FLOW] terminal extent changes from %u to %u", paged_flow_terminal_page(state, sequence), last);
        const DomNode* tail = nullptr;
        for (PagedFlowParagraph* paragraph = state->paragraphs; paragraph; paragraph = paragraph->next)
            if (paragraph->count && paged_flow_sequence(state, paragraph->block) == sequence &&
                paragraph->block->height <= state->fragmentainers[paragraph->lines[paragraph->count - 1].fragmentainer].rect.height + kPagedFlowEpsilon)
                tail = paragraph->block;
        float offset = 0.0f;
        // auxiliary-only sheets own their real tail; pinning the call would move body content instead.
        for (size_t anchor_index = 0; anchor_index < state->anchor_count; anchor_index++) {
            const PagedFlowAnchor& anchor = state->anchors[anchor_index];
            if (anchor.fragmentainer == kPagedUnplaced) continue;
            size_t index = anchor.fragmentainer; float start = 0.0f;
            for (PagedNotePiece* piece = anchor.continuation; piece; piece = piece->next) {
                index = piece->fragmentainer; start = piece->offset;
            }
            const PagedFragmentainer& frame = state->fragmentainers[index];
            if (frame.page && frame.page->page_number == last && !frame.occupied) {
                tail = anchor.element; offset = start;
                if (anchor.kind == PAGED_ANCHOR_NOTE) {
                    // retain the last content, not the trailing half-leading below its glyph rectangle.
                    float last_start = 0.0f; offset = 0.0f;
                    paged_note_break_edge(state, (ViewElement*)anchor.element, 0.0f, 0.0f,
                        anchor.full_height, &offset, &last_start);
                    offset = 0.0f;
                    paged_note_break_edge(state, (ViewElement*)anchor.element, 0.0f, 0.0f,
                        last_start, &offset);
                }
            }
        }
        *terminal = {sequence->source.address, last, tail, offset};
        composition->reference_used = true;
        composition->reference_source = sequence->source;
    }
    return TYPESET_OK;
}

// final conditional spaces vanish; retained spaces still position empty trailing anchors.
static TypesetStatus paged_space_finish(PagedFlowState* state) {
    if (!state->pending_space_count) return TYPESET_OK;
    float top = state->flow_end;
    size_t index = paged_fragmentainer_index(state, top > 0.0f ? top - kPagedFlowEpsilon : top);
    RadiantSpaceSpec range = radiant_spaces_resolve(state->pending_spaces, state->pending_space_count,
        top <= state->fragmentainers[index].top + kPagedFlowEpsilon, true);
    paged_space_anchors_shift(state, kPagedUnplaced, range.optimum);
    state->pending_space_count = state->space_box_count = 0; state->space_open = kPagedUnplaced;
    if (range.optimum == 0.0f) return TYPESET_OK;
    state->flow_end += range.optimum;
    ViewBlock* root = paged_flow_root(state);
    if (root) {
        root->height = fmaxf(root->height, state->flow_end);
        root->content_height = fmaxf(root->content_height, state->flow_end);
    }
    return paged_fragmentainer_at(state, fmaxf(0.0f, state->flow_end - kPagedFlowEpsilon), &index);
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
    if (status == TYPESET_OK) status = paged_space_finish(state);
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
    // the first page's body geometry is the flow's viewport; its page name comes
    // from the first flow content so an opening named page needs no restyle
    paged_flow_opening(state);
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
        float saved_device_scale = ui->device_scale;
        int saved_meta_width = document->viewport.width, saved_meta_height = document->viewport.height;
        ui->viewport_width = first.width; ui->viewport_height = first.height;
        ui->device_scale = tree->model->environment.device_scale;
        document->viewport.width = document->viewport.height = 0;
        CssEngine* engine = (CssEngine*)document->services.cached_css_engine;
        bool saved_print = engine && engine->context.print_media;
        double saved_density = engine ? engine->context.device_pixel_ratio : 1.0;
        bool print = tree->model->environment.print_media;
        if (engine && (saved_print != print || saved_density != tree->model->environment.device_scale)) {
            engine->context.print_media = print;
            engine->context.device_pixel_ratio = tree->model->environment.device_scale;
            radiant_recascade_document_styles(document, document->root);
        }
        // paged_flow_pass_begin opens the checkpoint of each pass and the hooks
        // append the pages; paged_flow_compose accepts the settled pass's
        // checkpoint and composes the pages before the layout context closes
        layout_html_doc(ui, document, false, state);
        ui->viewport_width = saved_viewport_width; ui->viewport_height = saved_viewport_height;
        ui->device_scale = saved_device_scale;
        document->viewport.width = saved_meta_width; document->viewport.height = saved_meta_height;
        if (engine) {
            engine->context.print_media = saved_print;
            engine->context.device_pixel_ratio = saved_density;
        }
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
    if (paged_native_sources(composition->pool, options, &composition->marks.sources) != TYPESET_OK) return TYPESET_OUT_OF_MEMORY;
    composition->native_targets = {tree->model->tree_id, tree->layout_generation, composition->pool,
        nullptr, 0, 0, options->max_nodes, nullptr, composition->marks.sources};
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

static bool paged_target_area_enter(PagedComposition* composition, DomNode* source,
        uint32_t page, bool normal, bool blank) {
    for (DomNode* node = source; node; node = node->parent) {
        // static/auxiliary layout need not execute a counter scope to own target areas.
        bool target = node->is_element() && node->as_element()->id && *node->as_element()->id;
        PagedMarkSource* mark = paged_source_mark(composition, node, target);
        if (target && !mark) return false;
        if (!mark) continue;
        paged_target_area_range(mark->generated_pages, page, normal, blank);
    }
    return true;
}

static TypesetStatus paged_target_areas_capture(ViewTree* tree) {
    PagedComposition* composition = tree->model->composition;
    // aggregate committed occurrences once; descendants and repeated/out-of-line areas retain their source ancestry.
    for (size_t i = 0; i < tree->model->node_id_count; i++) {
        LayoutViewNode* node = tree->model->nodes.get()[i];
        if (!node || node->kind != LAYOUT_VIEW_FRAGMENT || (!node->paint_box && !node->glyph_run && !node->image_box)) continue;
        if (!paged_source_has_own_area(node)) continue;
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
        if (!paged_target_area_enter(composition, area_source, ((ViewPageBox*)parent)->page_number, normal, false))
            return TYPESET_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < tree->model->page_count; i++) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        if (page->blank && page->sequence &&
            !paged_target_area_enter(composition, page->sequence->source.address, page->page_number, false, true))
            return TYPESET_OUT_OF_MEMORY;
    }
    return TYPESET_OK;
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
    TypesetStatus captured = paged_target_areas_capture(tree);
    if (captured != TYPESET_OK) return captured;
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
        const TypesetTarget* native = !old->source.address
            ? typeset_target_find(&tree->model->composition->native_targets, previous.name) : nullptr;
        DomNode* node = old->source.address ? dom_node_ref_validate(tree->model->document, old->source) : nullptr;
        if ((!native && !node) || (node && !node->is_element())) { status = TYPESET_STALE; break; }
        DomElement* source = node ? node->as_element() : nullptr;
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
            *parts[j] = source ? paged_flow_pseudo_text(session->pool, source, j == 0) : "";
            if (!*parts[j]) status = TYPESET_OUT_OF_MEMORY;
        }
        if (status != TYPESET_OK) break;
        TypesetTarget target = previous;
        target.page_number = native ? native->page_number : paged_target_page(tree, source);
        target.last_page_number = native ? native->last_page_number : paged_target_page(tree, source, true);
        target.binding = lam::up((const TypesetRecord*)value);
        if (source) { target.source.generation = tree->layout_generation; target.value = target.binding; }
        else target.value = native->value;
        for (size_t area = 0; area < 3; area++) for (size_t edge = 0; edge < 2; edge++) {
            uint32_t number = native ? (edge ? native->last_page_number : native->page_number)
                : mark ? mark->generated_pages[area][edge] : 0;
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
    if (!tree || !tree->model || !options || options->first_side > VIEW_PAGE_RIGHT || tree->model->committed)
        return TYPESET_INVALID;
    if (!options->max_pages || !options->max_nodes || !options->max_depth ||
        !options->max_reference_passes || !options->max_container_passes) {
        if (diagnostic) diagnostic->status = TYPESET_BUDGET_EXHAUSTED;
        return TYPESET_BUDGET_EXHAUSTED;
    }
    if (!view_tree_model_source_valid(tree)) return TYPESET_STALE;
    if (tree->model->composition || tree->model->node_count != 1) return TYPESET_INVALID;
    struct NativeOwners {
        const PagedLayoutOptions* options;
        size_t count;
        ~NativeOwners() {
            while (count) {
                const ViewNativeOwner& owner = options->native_flows[--count].owner;
                owner.release(owner.context);
            }
        }
    } owners = {options, 0};
    if ((options->native_flow_count && !options->native_flows) || options->native_flow_count > options->max_nodes ||
        options->native_flow_count > SIZE_MAX / sizeof(TypesetSourceNamespace))
        return TYPESET_INVALID;
    for (size_t i = 0; i < options->native_flow_count; i++) {
        const PagedNativeFlowBinding& binding = options->native_flows[i];
        const TypesetFlowProvider* provider = binding.provider;
        TypesetStatus invalid = binding.target_count > options->max_nodes ? TYPESET_BUDGET_EXHAUSTED : TYPESET_OK;
        if (!provider || !provider->identity || !provider->generation || !provider->next || !provider->checkpoint ||
            !provider->restore || !binding.owner.context || !binding.owner.retain || !binding.owner.release) invalid = TYPESET_INVALID;
        else if (binding.start.provider != provider->identity || binding.start.generation != provider->generation) invalid = TYPESET_STALE;
        if (binding.source) {
            DomElement* root = binding.source;
            size_t depth = 0;
            while (root && root != tree->model->document->root && depth++ <= options->max_depth) root = root->parent_element();
            if (root != tree->model->document->root ||
                (!radiant_page_element(binding.source, "flow") && !radiant_page_element(binding.source, "static-content"))) invalid = TYPESET_INVALID;
            for (DomNode* child = binding.source->first_child; child; child = child->next_sibling)
                if (!child->is_text() || !radiant_page_whitespace(child->as_text())) invalid = TYPESET_INVALID;
        }
        for (size_t earlier = 0; earlier < i; earlier++) {
            const PagedNativeFlowBinding& other = options->native_flows[earlier];
            if ((provider && other.provider->identity == provider->identity && other.provider->generation == provider->generation) ||
                (binding.source && binding.source == other.source)) invalid = TYPESET_INVALID;
        }
        if (invalid == TYPESET_OK && !binding.owner.retain(binding.owner.context)) invalid = TYPESET_STALE;
        if (invalid != TYPESET_OK) {
            if (diagnostic) { diagnostic->status = invalid; diagnostic->source = dom_node_ref(binding.source); diagnostic->reason = "invalid native flow binding"; }
            return invalid;
        }
        owners.count++;
    }
    PagedReferenceSession session = {};
    session.pool = mem_pool_create((MemContext*)tree->model->document->services.mem_ctx,
        MEM_ROLE_LAYOUT, "view_tree.secondary.references");
    if (!session.pool) return TYPESET_OUT_OF_MEMORY;
    session.targets = {tree->model->tree_id, tree->layout_generation, session.pool,
        nullptr, 0, 0, options->max_nodes, nullptr};
    if (paged_native_sources(session.pool, options, &session.targets.sources) != TYPESET_OK) return TYPESET_OUT_OF_MEMORY;
    session.convergence.pool = session.pool; session.convergence.limit = options->max_reference_passes;
    size_t visited = 0;
    TypesetStatus status = paged_targets_seed(tree, &session, tree->model->document->root, 0, &visited, options);
    for (size_t i = 0; status == TYPESET_OK && i < options->native_flow_count; i++) {
        const PagedNativeFlowBinding& binding = options->native_flows[i];
        if (binding.target_count && !binding.targets) { status = TYPESET_INVALID; break; }
        for (size_t j = 0; status == TYPESET_OK && j < binding.target_count; j++) {
            TypesetTarget target = binding.targets[j];
            if (target.source.provider != binding.provider->identity || target.source.generation != binding.provider->generation) {
                status = TYPESET_STALE; break;
            }
            PagedTargetValue* value = (PagedTargetValue*)pool_calloc(session.pool, sizeof(PagedTargetValue));
            if (!value) { status = TYPESET_OUT_OF_MEMORY; break; }
            // reference metadata belongs to the compositor; the opaque value keeps its producer namespace.
            value->provider = tree->model->tree_id; value->text = value->before = value->after = "";
            value->counters = counter_snapshot_copy(nullptr, session.pool);
            if (!value->counters) { status = TYPESET_OUT_OF_MEMORY; break; }
            target.page_number = target.last_page_number = 0; target.binding = lam::up((const TypesetRecord*)value);
            status = typeset_target_append(&session.targets, &target);
        }
    }
    bool settled = false;
    LayoutContentVisibilityTransaction history = {};
    while (status == TYPESET_OK && !settled) {
        if (!history.begin(tree)) { status = TYPESET_OUT_OF_MEMORY; break; }
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
            history.close();
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
    if (status == TYPESET_OK) history.close(true);
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
