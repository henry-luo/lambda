#include "view_tree_model.hpp"
#include "view_tree_css.hpp"
#include "layout_paged.hpp"
#include "view.hpp"
#include "render.hpp"
#include "../lib/atomic.h"
#include "../lib/generation.h"
#include "../lib/hashmap.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/mem_grow.hpp"
#include "../lib/memtrack.h"
#include <float.h>
#include <math.h>
#include <string.h>

struct SourceStateEntry {
    uint32_t id;
    ViewNodeState* state;
};

struct ViewModelUndo { void* address; void* saved; size_t size; };
HASHMAP_DEFINE_PTRKEY(view_model_undo, ViewModelUndo, address)
struct ViewModelCheckpoint {
    Pool* pool;
    hashmap* records;
    hashmap* created_states;
    ViewModelCheckpoint* previous;
    ArenaMark mark;
    size_t node_count, node_id_count, page_count;
};

static atomic_int64 next_secondary_tree_id = {0};

static uint64_t source_state_hash(const void* entry, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(&((const SourceStateEntry*)entry)->id, sizeof(uint32_t), seed0, seed1);
}

static int source_state_compare(const void* left, const void* right, void*) {
    uint32_t a = ((const SourceStateEntry*)left)->id;
    uint32_t b = ((const SourceStateEntry*)right)->id;
    return a < b ? -1 : a > b ? 1 : 0;
}

static void model_checkpoint_dispose(ViewTreeModel* model) {
    ViewModelCheckpoint* checkpoint = model->checkpoint;
    model->checkpoint = lam::up(checkpoint->previous);
    hashmap_free(checkpoint->records);
    hashmap_free(checkpoint->created_states);
    mem_pool_destroy(checkpoint->pool);
}

ViewModelCheckpoint* view_tree_model_checkpoint(ViewTree* tree) {
    if (!view_tree_model_source_valid(tree) || tree->model->committed) return nullptr;
    ViewTreeModel* model = tree->model;
    Pool* pool = mem_pool_create((MemContext*)model->document->services.mem_ctx,
        MEM_ROLE_LAYOUT, "view_tree.model.checkpoint");
    if (!pool) return nullptr;
    ViewModelCheckpoint* checkpoint = (ViewModelCheckpoint*)pool_calloc(pool, sizeof(ViewModelCheckpoint));
    if (!checkpoint) { mem_pool_destroy(pool); return nullptr; }
    checkpoint->pool = pool;
    checkpoint->records = view_model_undo_new(0);
    checkpoint->created_states = hashmap_new(sizeof(SourceStateEntry), 0, 0, 0,
        source_state_hash, source_state_compare, nullptr, nullptr);
    if (!checkpoint->records || !checkpoint->created_states) {
        if (checkpoint->records) hashmap_free(checkpoint->records);
        if (checkpoint->created_states) hashmap_free(checkpoint->created_states);
        mem_pool_destroy(pool); return nullptr;
    }
    checkpoint->previous = model->checkpoint;
    checkpoint->mark = arena_mark(model->arena);
    checkpoint->node_count = model->node_count;
    checkpoint->node_id_count = model->node_id_count;
    checkpoint->page_count = model->page_count;
    model->checkpoint = lam::up(checkpoint);
    return checkpoint;
}

static bool model_record(ViewModelCheckpoint* checkpoint, void* address, size_t size) {
    ViewModelUndo key = {address, nullptr, size};
    const ViewModelUndo* existing = (const ViewModelUndo*)hashmap_get(checkpoint->records, &key);
    if (existing) return existing->size == size;
    key.saved = pool_alloc(checkpoint->pool, size);
    if (!key.saved) return false;
    memcpy(key.saved, address, size);
    hashmap_set(checkpoint->records, &key);
    return !hashmap_oom(checkpoint->records);
}

bool view_tree_model_record(ViewTree* tree, void* address, size_t size) {
    if (!address || !size || !view_tree_model_source_valid(tree) || tree->model->committed) return false;
    for (ViewModelCheckpoint* checkpoint = tree->model->checkpoint; checkpoint; checkpoint = checkpoint->previous)
        if (!model_record(checkpoint, address, size)) return false;
    return true;
}

bool view_tree_model_touch_node(ViewTree* tree, LayoutViewNode* node) {
    if (!node || view_tree_node_resolve(tree, node->ref) != node || tree->model->committed) return false;
    for (ViewModelCheckpoint* checkpoint = tree->model->checkpoint; checkpoint; checkpoint = checkpoint->previous) {
        // new nodes disappear with the arena tail; only surviving records need undo bytes.
        if (node->ref.node_id > checkpoint->node_id_count) continue;
        if (!model_record(checkpoint, node, node->kind == LAYOUT_VIEW_PAGE ? sizeof(ViewPageBox) : sizeof(LayoutViewNode)) ||
            (node->glyph_run && !model_record(checkpoint, node->glyph_run, sizeof(PaintGlyphRun)))) return false;
    }
    return true;
}

bool view_tree_model_accept(ViewTree* tree, ViewModelCheckpoint* checkpoint) {
    if (!view_tree_model_source_valid(tree) || !checkpoint || tree->model->checkpoint.get() != checkpoint || tree->model->committed) return false;
    model_checkpoint_dispose(tree->model);
    return true;
}

bool view_tree_model_restore(ViewTree* tree, ViewModelCheckpoint* checkpoint) {
    if (!view_tree_model_source_valid(tree) || !checkpoint || tree->model->checkpoint.get() != checkpoint || tree->model->committed) return false;
    ViewTreeModel* model = tree->model;
    size_t cursor = 0; void* item = nullptr;
    while (hashmap_iter(checkpoint->records, &cursor, &item)) {
        const ViewModelUndo* undo = (const ViewModelUndo*)item;
        memcpy(undo->address, undo->saved, undo->size);
    }
    cursor = 0;
    while (hashmap_iter(checkpoint->created_states, &cursor, &item)) hashmap_delete(model->source_states, item);
    // never reuse a discarded node ID within a layout generation (D4.5.1v4).
    for (size_t i = checkpoint->node_id_count; i < model->node_id_count; i++) model->nodes.get()[i] = nullptr;
    for (size_t i = checkpoint->page_count; i < model->page_count; i++) model->pages.get()[i] = nullptr;
    model->node_count = checkpoint->node_count;
    model->page_count = checkpoint->page_count;
    arena_rewind(model->arena, checkpoint->mark);
    model_checkpoint_dispose(model);
    return true;
}

static bool valid_extent(float value, bool allow_zero) {
    return isfinite(value) && (allow_zero ? value >= 0.0f : value > 0.0f);
}

static bool valid_rect(RdtLogicalRect rect) {
    return isfinite(rect.x) && isfinite(rect.y) &&
        valid_extent(rect.width, true) && valid_extent(rect.height, true) &&
        isfinite(rect.x + rect.width) && isfinite(rect.y + rect.height);
}

ViewEnvironment view_environment_default(ViewPresentation presentation) {
    ViewEnvironment environment = {};
    environment.presentation = presentation;
    environment.print_media = presentation == VIEW_PRESENTATION_PAGED;
    environment.device_scale = 1.0f;
    // CSS absolute units use 96 logical pixels per inch (RSC1).
    environment.page_width = 210.0f * 96.0f / 25.4f;
    environment.page_height = 297.0f * 96.0f / 25.4f;
    environment.viewport_width = environment.page_width;
    environment.viewport_height = environment.page_height;
    return environment;
}

ViewPreviewOptions view_preview_options_default() {
    ViewPreviewOptions options = {};
    options.arrangement = VIEW_PAGES_GRID;
    options.rows = options.columns = 1;
    options.scale = 1.0f;
    return options;
}

bool view_tree_model_source_valid(const ViewTree* tree) {
    return tree && tree->model && tree->model->document &&
        tree->model->source_epoch == tree->model->document->mutation_epoch;
}

static LayoutViewNode* model_node_create(ViewTree* tree, LayoutViewKind kind, size_t size) {
    ViewTreeModel* model = tree->model;
    if (model->node_id_count >= UINT32_MAX) return nullptr;
    LayoutViewNode** nodes = model->nodes;
    if (!lam::pool_grow_array(tree->prop_pool, &nodes, &model->node_capacity,
                             model->node_id_count + 1, 16)) return nullptr;
    model->nodes = lam::up(nodes);
    LayoutViewNode* node = (LayoutViewNode*)arena_alloc(model->arena, size);
    if (!node) return nullptr;
    memset(node, 0, size);
    node->kind = kind;
    node->ref = {model->tree_id, tree->layout_generation,
                 static_cast<uint32_t>(model->node_id_count + 1)};
    nodes[model->node_id_count++] = node;
    model->node_count++;
    return node;
}

static bool model_initialize(ViewTree* tree) {
    ViewTreeModel* model = tree->model;
    model->source_epoch = model->document->mutation_epoch;
    model->root = lam::up(model_node_create(tree, LAYOUT_VIEW_ROOT, sizeof(LayoutViewNode)));
    return model->root != nullptr;
}

static void secondary_views_cleanup(void* data) {
    view_tree_secondary_release_all((DomDocument*)data);
}

ViewTree* view_tree_secondary_create(DomDocument* document,
                                    const ViewEnvironment* environment) {
    if (!document || !document->document_pool || !environment ||
        environment->presentation > VIEW_PRESENTATION_PAGED ||
        !valid_extent(environment->viewport_width, false) ||
        !valid_extent(environment->viewport_height, false) ||
        !valid_extent(environment->device_scale, false) ||
        !valid_extent(environment->page_width, false) ||
        !valid_extent(environment->page_height, false) ||
        !isfinite(environment->sampled_time)) {
        return nullptr;
    }
    lam::Own<ViewTree> shell = view_tree_shell_create(document);
    if (!shell) return nullptr;
    shell->init((MemContext*)document->services.mem_ctx);
    if (!shell->prop_pool || !shell->scratch_arena || !shell->render_scratch_arena ||
        !shell->display_list_arena || !shell->layout_pass_arena) {
        view_tree_shell_destroy(document, shell);
        return nullptr;
    }
    ViewTreeModel* model = (ViewTreeModel*)pool_calloc(shell->prop_pool, sizeof(ViewTreeModel));
    if (!model) {
        view_tree_shell_destroy(document, shell);
        return nullptr;
    }
    shell->model = lam::own(model);
    model->document = lam::up(document);
    model->environment = *environment;
    int64_t tree_id = atomic_inc64(&next_secondary_tree_id);
    if (tree_id <= 0) {
        view_tree_shell_destroy(document, shell);
        return nullptr;
    }
    model->tree_id = static_cast<uint64_t>(tree_id);
    model->arena = lam::own(mem_arena_create((MemContext*)document->services.mem_ctx,
                                           MEM_ROLE_VIEW, "view_tree.secondary.nodes"));
    model->source_states = lam::own(hashmap_new(sizeof(SourceStateEntry), 16, 0, 0,
        source_state_hash, source_state_compare, nullptr, nullptr));
    if (!model->arena || !model->source_states || !model_initialize(shell)) {
        view_tree_shell_destroy(document, shell);
        return nullptr;
    }
    if (!document->secondary_views_cleanup_registered) {
        if (!pool_add_cleanup(document->document_pool, secondary_views_cleanup, document)) {
            view_tree_shell_destroy(document, shell);
            return nullptr;
        }
        document->secondary_views_cleanup_registered = true;
    }
    shell->next_secondary = document->secondary_view_trees;
    document->secondary_view_trees = shell;
    return shell;
}

bool view_tree_secondary_release(DomDocument* document, ViewTree* tree) {
    if (!document || !tree) return false;
    lam::Own<ViewTree>* edge = &document->secondary_view_trees;
    while (*edge && (ViewTree*)*edge != tree) edge = &(*edge)->next_secondary;
    if (!*edge) return false;
    lam::Own<ViewTree> removed = *edge;
    *edge = removed->next_secondary;
    removed->next_secondary = nullptr;
    view_tree_shell_destroy(document, removed);
    return true;
}

void view_tree_secondary_release_all(DomDocument* document) {
    if (!document) return;
    while (document->secondary_view_trees) {
        view_tree_secondary_release(document, document->secondary_view_trees);
    }
}

void view_tree_model_destroy(ViewTree* tree) {
    if (!tree || !tree->model) return;
    ViewTreeModel* model = tree->model;
    while (model->checkpoint) model_checkpoint_dispose(model);
    paged_composition_destroy(tree);
    view_css_context_destroy(tree);
    if (model->source_states) hashmap_free(model->source_states);
    if (model->arena) mem_arena_destroy(model->arena);
    tree->model = nullptr;
}

void view_tree_model_reset(ViewTree* tree) {
    if (!tree || !tree->model) return;
    ViewTreeModel* model = tree->model;
    while (model->checkpoint) model_checkpoint_dispose(model);
    paged_composition_destroy(tree);
    view_css_context_destroy(tree);
    hashmap_clear(model->source_states, false);
    arena_reset(model->arena);
    model->node_count = model->node_id_count = model->page_count = model->placement_count = 0;
    model->root = nullptr;
    model->committed = false;
    model->preview_bounds = {};
    model->presentation_generation = generation_next32(model->presentation_generation);
    if (!model_initialize(tree)) log_error("VIEW_MODEL reset: root allocation failed");
}

ViewNodeState* view_tree_node_state(ViewTree* tree, DomNode* source, bool create) {
    if (!source || !view_tree_model_source_valid(tree)) return nullptr;
    ViewTreeModel* model = tree->model;
    DomNodeRef ref = dom_node_ref(source);
    if (dom_node_ref_validate(model->document, ref) != source) return nullptr;
    SourceStateEntry key = {ref.expected_id, nullptr};
    const SourceStateEntry* existing = (const SourceStateEntry*)hashmap_get(model->source_states, &key);
    if (existing) return existing->state;
    if (!create || model->committed) return nullptr;
    for (ViewModelCheckpoint* checkpoint = model->checkpoint; checkpoint; checkpoint = checkpoint->previous) {
        hashmap_set(checkpoint->created_states, &key);
        if (hashmap_oom(checkpoint->created_states)) return nullptr;
    }
    ViewNodeState* state = (ViewNodeState*)arena_alloc(model->arena, sizeof(ViewNodeState));
    if (!state) return nullptr;
    memset(state, 0, sizeof(*state));
    state->source = ref;
    key.state = state;
    hashmap_set(model->source_states, &key);
    return hashmap_oom(model->source_states) ? nullptr : state;
}

LayoutViewNode* view_tree_node_resolve(ViewTree* tree, LayoutViewRef ref) {
    if (!view_tree_model_source_valid(tree) || ref.tree_id != tree->model->tree_id ||
        ref.generation != tree->layout_generation || ref.node_id == 0 ||
        ref.node_id > tree->model->node_id_count) return nullptr;
    return tree->model->nodes.get()[ref.node_id - 1];
}

static void model_append_child(LayoutViewNode* parent, LayoutViewNode* child) {
    child->parent = lam::up(parent);
    if (parent->last_child) parent->last_child->next_sibling = lam::up(child);
    else parent->first_child = lam::up(child);
    parent->last_child = lam::up(child);
}

LayoutViewNode* view_tree_fragment_append(ViewTree* tree, LayoutViewNode* parent,
        DomNode* source, RdtLogicalRect rect, size_t text_start, size_t text_length) {
    if (!parent || !valid_rect(rect) || !view_tree_model_source_valid(tree) ||
        tree->model->committed || view_tree_node_resolve(tree, parent->ref) != parent ||
        text_length > SIZE_MAX - text_start) return nullptr;
    ViewNodeState* state = source ? view_tree_node_state(tree, source, true) : nullptr;
    if (source && !state) return nullptr;
    if (!view_tree_model_touch_node(tree, parent) ||
        (parent->last_child && !view_tree_model_touch_node(tree, parent->last_child))) return nullptr;
    if (state) {
        SourceStateEntry key = {state->source.expected_id, nullptr};
        for (ViewModelCheckpoint* checkpoint = tree->model->checkpoint; checkpoint; checkpoint = checkpoint->previous)
            if (!hashmap_get(checkpoint->created_states, &key) && !model_record(checkpoint, state, sizeof(*state))) return nullptr;
        if (state->last_occurrence && !view_tree_model_touch_node(tree, state->last_occurrence)) return nullptr;
    }
    LayoutViewNode* node = model_node_create(tree, LAYOUT_VIEW_FRAGMENT, sizeof(LayoutViewNode));
    if (!node) return nullptr;
    node->rect = rect;
    node->text_start = text_start;
    node->text_length = text_length;
    node->state = lam::up(state);
    if (state) {
        node->source = state->source;
        if (state->last_occurrence) state->last_occurrence->next_occurrence = lam::up(node);
        else state->first_occurrence = lam::up(node);
        state->last_occurrence = lam::up(node);
        state->occurrence_count++;
    }
    model_append_child(parent, node);
    return node;
}

ViewPageBox* view_tree_page_append(ViewTree* tree, float width, float height,
        RdtLogicalRect content_rect, ViewPageSide side, bool blank) {
    if (!view_tree_model_source_valid(tree) || tree->model->committed ||
        tree->model->environment.presentation != VIEW_PRESENTATION_PAGED ||
        !valid_extent(width, false) || !valid_extent(height, false) ||
        !valid_rect(content_rect) || content_rect.x < 0.0f || content_rect.y < 0.0f ||
        content_rect.x + content_rect.width > width ||
        content_rect.y + content_rect.height > height || side > VIEW_PAGE_RIGHT ||
        tree->model->page_count >= UINT32_MAX) return nullptr;
    ViewTreeModel* model = tree->model;
    if (!view_tree_model_touch_node(tree, model->root) ||
        (model->root->last_child && !view_tree_model_touch_node(tree, model->root->last_child))) return nullptr;
    ViewPageBox** pages = model->pages;
    if (!lam::pool_grow_array(tree->prop_pool, &pages, &model->page_capacity,
                             model->page_count + 1, 16)) return nullptr;
    model->pages = lam::up(pages);
    ViewPageBox* page = (ViewPageBox*)model_node_create(tree, LAYOUT_VIEW_PAGE, sizeof(ViewPageBox));
    if (!page) return nullptr;
    page->node.rect = {0.0f, 0.0f, width, height};
    page->content_rect = content_rect;
    page->side = side;
    page->blank = blank;
    page->page_number = static_cast<uint32_t>(model->page_count + 1);
    pages[model->page_count++] = page;
    model_append_child(model->root, &page->node);
    return page;
}

bool view_tree_model_commit(ViewTree* tree) {
    if (!view_tree_model_source_valid(tree) || !tree->model->root || tree->model->checkpoint) return false;
    tree->model->committed = true;
    return true;
}

static ViewModelStatus page_selection_resolve(const ViewTreeModel* model,
        const ViewPageSelection* selection, uint8_t* selected, size_t* count) {
    bool all = !selection || selection->all;
    *count = all ? model->page_count : 0;
    if (model->page_count) memset(selected, all ? 1 : 0, model->page_count);
    if (all) return VIEW_MODEL_OK;
    if (selection->range_count && !selection->ranges) return VIEW_MODEL_INVALID_PAGE_RANGE;
    for (size_t i = 0; i < selection->range_count; i++) {
        const ViewPageRange& range = selection->ranges[i];
        if (!range.first || range.last < range.first || range.last > model->page_count) {
            return VIEW_MODEL_INVALID_PAGE_RANGE;
        }
        for (size_t page = range.first - 1; page < range.last; page++) {
            if (!selected[page]) { selected[page] = 1; (*count)++; }
        }
    }
    return VIEW_MODEL_OK;
}

static bool preview_options_valid(const ViewPreviewOptions* options) {
    return options && options->arrangement <= VIEW_PAGES_BOOK &&
        options->rows && options->columns &&
        valid_extent(options->scale, false) && valid_extent(options->padding, true) &&
        valid_extent(options->column_gap, true) && valid_extent(options->row_gap, true) &&
        valid_extent(options->group_gap, true);
}

static void placement_set(ViewPagePlacement* placement, const ViewPageBox* page,
                          float x, float y, float scale) {
    *placement = {page->node.ref, page->page_number,
        {x, y, page->node.rect.width * scale, page->node.rect.height * scale}, scale};
}

static ViewModelStatus book_arrange(const ViewTreeModel* model, const uint8_t* selected,
        const ViewPreviewOptions* options, ViewPagePlacement* placements,
        size_t* count, RdtLogicalRect* bounds) {
    *count = 0;
    for (size_t i = 1; i < model->page_count; i++) {
        if (model->pages.get()[i]->side == model->pages.get()[i - 1]->side) {
            return VIEW_MODEL_INVALID_PAGE_SIDE;
        }
    }
    if (options->anchor_page > model->page_count) return VIEW_MODEL_INVALID_PAGE_RANGE;
    ViewPageSide leading = options->right_binding ? VIEW_PAGE_RIGHT : VIEW_PAGE_LEFT;
    size_t start = 0;
    const ViewPageBox* first = nullptr;
    const ViewPageBox* second = nullptr;
    while (start < model->page_count) {
        const ViewPageBox* a = model->pages.get()[start];
        const ViewPageBox* b = nullptr;
        size_t next = start + 1;
        if (a->side == leading && next < model->page_count) b = model->pages.get()[next++];
        bool eligible = selected[start] || (b && selected[next - 1]);
        bool anchored = !options->anchor_page || options->anchor_page == a->page_number ||
            (b && options->anchor_page == b->page_number);
        if (eligible && anchored) { first = a; second = b; break; }
        start = next;
    }
    if (!first) {
        if (options->anchor_page) return VIEW_MODEL_INVALID_PAGE_RANGE;
        *bounds = {0.0f, 0.0f, 2.0f * options->padding, 2.0f * options->padding};
        return VIEW_MODEL_OK;
    }
    const ViewPageBox* left = first->side == VIEW_PAGE_LEFT ? first : second;
    const ViewPageBox* right = first->side == VIEW_PAGE_RIGHT ? first : second;
    float left_width = (left ? left : right)->node.rect.width * options->scale;
    float right_width = (right ? right : left)->node.rect.width * options->scale;
    float height = fmaxf(first->node.rect.height, second ? second->node.rect.height : 0.0f) * options->scale;
    // Empty partners retain their slot; filtering never invents a new physical pair.
    if (left && selected[left->page_number - 1]) {
        placement_set(&placements[(*count)++], left, options->padding, options->padding, options->scale);
    }
    if (right && selected[right->page_number - 1]) {
        placement_set(&placements[(*count)++], right,
                      options->padding + left_width + options->column_gap,
                      options->padding, options->scale);
    }
    *bounds = {0.0f, 0.0f, left_width + right_width + options->column_gap + 2.0f * options->padding,
               height + 2.0f * options->padding};
    return valid_rect(*bounds) ? VIEW_MODEL_OK : VIEW_MODEL_INVALID_ARGUMENT;
}

static ViewModelStatus grid_arrange(const ViewTreeModel* model, const uint8_t* selected,
        size_t selected_count, const ViewPreviewOptions* options,
        ViewPagePlacement* placements, RdtLogicalRect* bounds) {
    uint64_t per_group = (uint64_t)options->rows * options->columns;
    float cell_width = 0.0f, cell_height = 0.0f;
    for (size_t i = 0; i < model->page_count; i++) {
        if (!selected[i]) continue;
        cell_width = fmaxf(cell_width, model->pages.get()[i]->node.rect.width * options->scale);
        cell_height = fmaxf(cell_height, model->pages.get()[i]->node.rect.height * options->scale);
    }
    float group_width = (float)options->columns * cell_width + (float)(options->columns - 1) * options->column_gap;
    float group_height = (float)options->rows * cell_height + (float)(options->rows - 1) * options->row_gap;
    if (!isfinite(group_width) || !isfinite(group_height)) return VIEW_MODEL_INVALID_ARGUMENT;
    float right = options->padding, bottom = options->padding;
    size_t ordinal = 0;
    for (size_t i = 0; i < model->page_count; i++) {
        if (!selected[i]) continue;
        size_t group = ordinal / per_group;
        size_t cell = ordinal % per_group;
        size_t row = options->column_major ? cell % options->rows : cell / options->columns;
        size_t column = options->column_major ? cell / options->rows : cell % options->columns;
        float x = options->padding + (float)column * (cell_width + options->column_gap);
        float y = options->padding + (float)row * (cell_height + options->row_gap);
        if (options->groups_horizontal) x += (float)group * (group_width + options->group_gap);
        else y += (float)group * (group_height + options->group_gap);
        const ViewPageBox* page = model->pages.get()[i];
        x += (cell_width - page->node.rect.width * options->scale) * 0.5f;
        placement_set(&placements[ordinal++], page, x, y, options->scale);
        right = fmaxf(right, x + page->node.rect.width * options->scale);
        bottom = fmaxf(bottom, y + page->node.rect.height * options->scale);
    }
    (void)selected_count;
    *bounds = {0.0f, 0.0f, right + options->padding, bottom + options->padding};
    return valid_rect(*bounds) ? VIEW_MODEL_OK : VIEW_MODEL_INVALID_ARGUMENT;
}

ViewModelStatus view_tree_preview_arrange(ViewTree* tree,
        const ViewPageSelection* selection, const ViewPreviewOptions* options) {
    if (!preview_options_valid(options) || !tree || !tree->model ||
        !tree->model->committed || tree->model->environment.presentation != VIEW_PRESENTATION_PAGED) {
        return VIEW_MODEL_INVALID_ARGUMENT;
    }
    if (!view_tree_model_source_valid(tree)) return VIEW_MODEL_STALE_SOURCE;
    ViewTreeModel* model = tree->model;
    lam::Temp<uint8_t> selected = lam::temp_array_zero<uint8_t>(model->page_count, MEM_CAT_LAYOUT);
    if (model->page_count && !selected) return VIEW_MODEL_OUT_OF_MEMORY;
    size_t count = 0;
    ViewModelStatus status = page_selection_resolve(model, selection, selected.get(), &count);
    if (status != VIEW_MODEL_OK) return status;
    size_t capacity = options->arrangement == VIEW_PAGES_BOOK ? 2 : count;
    size_t bytes = 0;
    if (!lam::checked_mul(capacity, sizeof(ViewPagePlacement), &bytes)) return VIEW_MODEL_OUT_OF_MEMORY;
    ViewPagePlacement* placements = capacity ? (ViewPagePlacement*)pool_alloc(tree->prop_pool, bytes) : nullptr;
    if (capacity && !placements) return VIEW_MODEL_OUT_OF_MEMORY;
    RdtLogicalRect bounds = {};
    if (options->arrangement == VIEW_PAGES_BOOK) {
        status = book_arrange(model, selected.get(), options, placements, &count, &bounds);
    } else {
        status = grid_arrange(model, selected.get(), count, options, placements, &bounds);
    }
    if (status != VIEW_MODEL_OK) {
        if (placements) pool_free(tree->prop_pool, placements);
        return status;
    }
    // Publish all placements together; rejected input preserves the prior preview.
    if (model->placements) pool_free(tree->prop_pool, model->placements);
    model->placements = lam::up(placements);
    model->placement_count = count;
    model->preview_bounds = bounds;
    model->root->rect = bounds;
    model->presentation_generation = generation_next32(model->presentation_generation);
    return VIEW_MODEL_OK;
}

static bool rects_overlap(RdtLogicalRect a, RdtLogicalRect b) {
    return a.width > 0.0f && a.height > 0.0f && b.width > 0.0f && b.height > 0.0f &&
        a.x < b.x + b.width && b.x < a.x + a.width &&
        a.y < b.y + b.height && b.y < a.y + a.height;
}

ViewModelStatus view_tree_preview_paint(ViewTree* tree, const RdtLogicalRect* clip,
                                       ViewPagePaintFn paint, void* context) {
    if (!tree || !tree->model || !paint || !tree->model->committed ||
        (clip && !valid_rect(*clip))) return VIEW_MODEL_INVALID_ARGUMENT;
    if (!view_tree_model_source_valid(tree)) return VIEW_MODEL_STALE_SOURCE;
    ViewTreeModel* model = tree->model;
    for (size_t i = 0; i < model->placement_count; i++) {
        const ViewPagePlacement* placement = &model->placements.get()[i];
        if (clip && !rects_overlap(placement->rect, *clip)) continue;
        LayoutViewNode* node = view_tree_node_resolve(tree, placement->page);
        if (!node) return VIEW_MODEL_STALE_SOURCE;
        if (!paint(tree, (const ViewPageBox*)node, placement, context)) return VIEW_MODEL_INVALID_ARGUMENT;
    }
    return VIEW_MODEL_OK;
}

ViewModelStatus view_tree_pages_visit(ViewTree* tree,
        const ViewPageSelection* selection, ViewPagePaintFn paint, void* context) {
    if (!tree || !tree->model || !paint || !tree->model->committed) return VIEW_MODEL_INVALID_ARGUMENT;
    if (!view_tree_model_source_valid(tree)) return VIEW_MODEL_STALE_SOURCE;
    ViewTreeModel* model = tree->model;
    lam::Temp<uint8_t> selected = lam::temp_array_zero<uint8_t>(model->page_count, MEM_CAT_LAYOUT);
    if (model->page_count && !selected) return VIEW_MODEL_OUT_OF_MEMORY;
    size_t count = 0;
    ViewModelStatus status = page_selection_resolve(model, selection, selected.get(), &count);
    if (status != VIEW_MODEL_OK) return status;
    for (size_t i = 0; i < model->page_count; i++) {
        if (!selected.get()[i]) continue;
        const ViewPageBox* page = model->pages.get()[i];
        // Physical-page consumers deliberately receive no preview transform.
        if (!paint(tree, page, nullptr, context)) return VIEW_MODEL_INVALID_ARGUMENT;
    }
    return VIEW_MODEL_OK;
}
