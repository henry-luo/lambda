#include "layout.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/tagged.hpp"

#include <string.h>

#define CUSTOM_LAYOUT_MAX_REGISTRY 64
#define CUSTOM_LAYOUT_NAME_CAP 64
#define CUSTOM_LAYOUT_MAX_UNKNOWN_LOGS 128
#define CUSTOM_LAYOUT_CONSTRAINT_SOURCE_CSS "css"
#define CUSTOM_LAYOUT_CONSTRAINT_SOURCE_AVAILABLE "available"
#define CUSTOM_LAYOUT_CONSTRAINT_SOURCE_INTRINSIC "intrinsic"
#define CUSTOM_LAYOUT_CONSTRAINT_SOURCE_FALLBACK "fallback"

typedef struct CustomLayoutRegistryEntry {
    char name[CUSTOM_LAYOUT_NAME_CAP];
    CustomLayoutFn fn;
} CustomLayoutRegistryEntry;

typedef struct CustomLayoutUnknownLogEntry {
    DomDocument* doc;
    char name[CUSTOM_LAYOUT_NAME_CAP];
} CustomLayoutUnknownLogEntry;

static CustomLayoutRegistryEntry g_custom_layout_registry[CUSTOM_LAYOUT_MAX_REGISTRY];
static int g_custom_layout_registry_count = 0;
static CustomLayoutUnknownLogEntry g_custom_layout_unknown_logs[CUSTOM_LAYOUT_MAX_UNKNOWN_LOGS];
static int g_custom_layout_unknown_log_count = 0;

static void velmt_edges_from_box_edges(VelmtEdges* dst, const BoxEdges* src) {
    if (!dst || !src) return;
    dst->left = src->left;
    dst->right = src->right;
    dst->top = src->top;
    dst->bottom = src->bottom;
}

static bool custom_layout_child_is_in_flow(View* child) {
    if (!child || child->view_type == RDT_VIEW_NONE) return false;
    if (!child->is_element()) return true;

    ViewBlock* block = lam::view_as_block(child);
    if (!block || !block->position) return true;
    if (block->position->position == CSS_VALUE_ABSOLUTE ||
        block->position->position == CSS_VALUE_FIXED) {
        return false;
    }
    if (block->position->float_prop == CSS_VALUE_LEFT ||
        block->position->float_prop == CSS_VALUE_RIGHT) {
        return false;
    }
    return true;
}

static int custom_layout_count_children(ViewBlock* block) {
    if (!block) return 0;
    int count = 0;
    for (View* child = (View*)block->first_child; child; child = child->next()) {
        if (custom_layout_child_is_in_flow(child)) count++;
    }
    return count;
}

void custom_layout_fill_velmt_from_view(Velmt* velmt, View* child, int index, bool normalize_origin) {
    if (!velmt) return;
    memset(velmt, 0, sizeof(*velmt));
    if (!child) return;
    velmt->view = child;
    velmt->element = child->is_element() ? child->as_element() : nullptr;
    velmt->index = index;
    // top-level custom layout children are normalized; nested Velmt snapshots
    // preserve local child offsets so callbacks can inspect rich content.
    velmt->border_box.x = normalize_origin ? 0.0f : child->x;
    velmt->border_box.y = normalize_origin ? 0.0f : child->y;
    velmt->border_box.width = child->width;
    velmt->border_box.height = child->height;

    ViewBlock* child_block = lam::view_as_block(child);
    if (child_block) {
        BoxMetrics metrics = layout_box_metrics(child_block);
        velmt_edges_from_box_edges(&velmt->margin, &metrics.margin);
        velmt_edges_from_box_edges(&velmt->border, &metrics.border);
        velmt_edges_from_box_edges(&velmt->padding, &metrics.padding);
    }
}

static int custom_layout_collect_children(ViewBlock* block, Velmt* children, int capacity) {
    if (!block || !children || capacity <= 0) return 0;
    int count = 0;
    for (View* child = (View*)block->first_child; child; child = child->next()) {
        if (!custom_layout_child_is_in_flow(child)) continue;
        custom_layout_fill_velmt_from_view(&children[count], child, count, true);
        count++;
        if (count >= capacity) break;
    }
    return count;
}

static bool custom_layout_placement_valid(const CustomLayoutContext* context,
                                          const CustomLayoutPlacement* placement) {
    return context && placement &&
        placement->child_index >= 0 &&
        placement->child_index < context->child_count;
}

static void custom_layout_clear_child_z(const Velmt* child) {
    if (!child || !child->view) return;
    ViewElement* element = lam::view_as_element(child->view);
    if (!element || !element->position) return;

    element->position->has_custom_layout_z_index = false;
    element->position->custom_layout_z_index = 0;
}

static PositionProp* custom_layout_alloc_position_prop(LayoutContext* lycon) {
    if (!lycon) return nullptr;
    Pool* pool = nullptr;
    if (lycon->doc && lycon->doc->view_tree) {
        pool = lycon->doc->view_tree->pool;
    }
    if (!pool) {
        pool = lycon->pool;
    }
    if (!pool) return nullptr;

    PositionProp* prop = (PositionProp*)pool_calloc(pool, sizeof(PositionProp));
    if (!prop) return nullptr;
    // Custom z can be attached to otherwise unpositioned children, so use the
    // shared static-position defaults before applying the pass-scoped overlay.
    position_prop_init_defaults(prop);
    return prop;
}

static bool custom_layout_apply_child_z(LayoutContext* lycon, const Velmt* child, int z) {
    if (!lycon || !child || !child->view) return false;
    ViewElement* element = lam::view_as_element(child->view);
    if (!element) return false;
    if (!element->position) {
        element->position = custom_layout_alloc_position_prop(lycon);
    }
    if (!element->position) {
        log_error("CUSTOM_LAYOUT_Z_ALLOC_FAILED child_index=%d", child->index);
        return false;
    }
    // z from layout(fn) is a pass-scoped paint/hit-test overlay, not authored
    // CSS z-index; clear_child_z resets stale values before every callback.
    element->position->custom_layout_z_index = z;
    element->position->has_custom_layout_z_index = true;
    return true;
}

static bool custom_layout_child_has_percent_size(const Velmt* child, bool horizontal) {
    if (!child || !child->view) return false;
    ViewBlock* block = lam::view_as_block(child->view);
    if (!block || !block->blk) return false;
    if (horizontal) {
        return !isnan(block->blk->given_width_percent) ||
            !isnan(block->blk->given_min_width_percent) ||
            !isnan(block->blk->given_max_width_percent);
    }
    return !isnan(block->blk->given_height_percent) ||
        !isnan(block->blk->given_min_height_percent) ||
        !isnan(block->blk->given_max_height_percent);
}

static void custom_layout_warn_auto_axis_percent_children(const CustomLayoutContext* context,
                                                          bool horizontal) {
    if (!context || !context->parent) return;
    float css_size = horizontal ? context->css_width : context->css_height;
    if (css_size >= 0.0f) return;

    int percent_child_count = 0;
    int first_percent_child_index = -1;
    const char* first_percent_child_loc = nullptr;
    for (int i = 0; i < context->child_count; i++) {
        const Velmt* child = &context->children[i];
        if (!custom_layout_child_has_percent_size(child, horizontal)) continue;
        if (first_percent_child_index < 0) {
            first_percent_child_index = i;
            first_percent_child_loc = child->view ? child->view->source_loc() : "(unknown)";
        }
        percent_child_count++;
    }
    if (percent_child_count <= 0) return;

    float child_available = horizontal ?
        context->child_available_width : context->child_available_height;
    bool child_available_definite = horizontal ?
        context->child_available_width_definite : context->child_available_height_definite;
    const char* source = horizontal ?
        context->child_available_width_source : context->child_available_height_source;
    const char* code = horizontal ?
        "CUSTOM_LAYOUT_PERCENT_CHILD_AUTO_WIDTH" :
        "CUSTOM_LAYOUT_PERCENT_CHILD_AUTO_HEIGHT";
    const char* axis = horizontal ? "width" : "height";

    // Percent child sizes need a containing size before custom placement, but
    // auto custom parents may derive that axis only after placements return.
    log_warn("%s %s layout='%s' children=%d first_child=%d first_loc=%s child_%s=%.1f child_%s_definite=%d child_%s_source=%s",
             code, context->parent->source_loc(),
             context->layout_name ? context->layout_name : "(null)",
             percent_child_count, first_percent_child_index,
             first_percent_child_loc ? first_percent_child_loc : "(unknown)",
             axis, child_available, axis, child_available_definite,
             axis, source ? source : CUSTOM_LAYOUT_CONSTRAINT_SOURCE_FALLBACK);
}

static void custom_layout_set_axis_constraint(float css_size,
                                              const AvailableSize* available,
                                              float css_used_size,
                                              float fallback_size,
                                              float* out_size,
                                              bool* out_definite,
                                              const char** out_source) {
    if (!out_size || !out_definite || !out_source) return;

    if (css_size >= 0.0f) {
        *out_size = css_used_size;
        *out_definite = true;
        *out_source = CUSTOM_LAYOUT_CONSTRAINT_SOURCE_CSS;
        return;
    }
    if (available && available->is_definite()) {
        *out_size = available->value;
        *out_definite = true;
        *out_source = CUSTOM_LAYOUT_CONSTRAINT_SOURCE_AVAILABLE;
        return;
    }
    if (available && available->is_intrinsic()) {
        *out_size = fallback_size;
        *out_definite = false;
        *out_source = CUSTOM_LAYOUT_CONSTRAINT_SOURCE_INTRINSIC;
        return;
    }

    // Auto custom parents may derive final size after placement, so callbacks
    // get the visible fallback without treating it as a definite CSS constraint.
    *out_size = fallback_size;
    *out_definite = false;
    *out_source = CUSTOM_LAYOUT_CONSTRAINT_SOURCE_FALLBACK;
}

static void custom_layout_set_child_constraints(CustomLayoutContext* context) {
    if (!context || !context->parent) return;

    AvailableSize* available_width = context->lycon ? &context->lycon->available_space.width : nullptr;
    AvailableSize* available_height = context->lycon ? &context->lycon->available_space.height : nullptr;
    custom_layout_set_axis_constraint(context->css_width, available_width,
        context->parent->content_width, context->available_width,
        &context->child_available_width, &context->child_available_width_definite,
        &context->child_available_width_source);
    custom_layout_set_axis_constraint(context->css_height, available_height,
        context->parent->content_height, context->available_height,
        &context->child_available_height, &context->child_available_height_definite,
        &context->child_available_height_source);
}

static bool custom_layout_copy_name(char* dst, const char* name, const char* log_prefix) {
    if (!dst || !name || name[0] == '\0') return false;
    size_t name_len = strlen(name);
    if (name_len >= CUSTOM_LAYOUT_NAME_CAP) {
        log_error("%s layout name too long '%s'", log_prefix ? log_prefix : "CUSTOM_LAYOUT", name);
        return false;
    }
    memcpy(dst, name, name_len + 1);
    return true;
}

static DomDocument* custom_layout_document_for_block(ViewBlock* block) {
    if (!block || !block->is_element()) return nullptr;
    DomElement* element = block->as_element();
    return element ? element->doc : nullptr;
}

static void custom_layout_log_unregistered_once(ViewBlock* block, const char* layout_name) {
    if (!layout_name || layout_name[0] == '\0') return;
    DomDocument* doc = custom_layout_document_for_block(block);
    for (int i = 0; i < g_custom_layout_unknown_log_count; i++) {
        CustomLayoutUnknownLogEntry* entry = &g_custom_layout_unknown_logs[i];
        if (entry->doc == doc && strcmp(entry->name, layout_name) == 0) {
            return;
        }
    }
    if (g_custom_layout_unknown_log_count < CUSTOM_LAYOUT_MAX_UNKNOWN_LOGS) {
        CustomLayoutUnknownLogEntry* entry =
            &g_custom_layout_unknown_logs[g_custom_layout_unknown_log_count];
        entry->doc = doc;
        if (custom_layout_copy_name(entry->name, layout_name, "CUSTOM_LAYOUT_UNKNOWN_LOG")) {
            g_custom_layout_unknown_log_count++;
        }
    }
    log_debug("CUSTOM_LAYOUT_UNREGISTERED %s layout='%s'",
              block ? block->source_loc() : "(null)", layout_name);
}

static void custom_layout_apply_parent_axis(ViewBlock* block, float content_size, bool horizontal) {
    if (!block) return;

    content_size = max(content_size, 0.0f);
    float border_size = 0.0f;
    if (layout_uses_border_box(block)) {
        border_size = layout_border_size_from_content_box(block, content_size, horizontal);
        border_size = layout_apply_min_max_axis(block, border_size, horizontal, true);
        border_size = layout_floor_border_box_axis(block, border_size, horizontal);
        content_size = layout_content_size_from_border_box(block, border_size, horizontal);
    } else {
        content_size = layout_apply_min_max_axis(block, content_size, horizontal, false);
        border_size = layout_border_size_from_content_box(block, content_size, horizontal);
        border_size = layout_floor_border_box_axis(block, border_size, horizontal);
    }

    if (horizontal) {
        block->content_width = content_size;
        block->width = border_size;
    } else {
        block->content_height = content_size;
        block->height = border_size;
    }
}

static void custom_layout_apply_parent_size(ViewBlock* block,
                                            const CustomLayoutResult* result,
                                            float min_x,
                                            float min_y,
                                            float max_x,
                                            float max_y) {
    if (!block || !result) return;

    if (result->has_baseline && block->blk) {
        block->blk->first_line_baseline = result->baseline;
    }
    float content_width = (max_x > min_x) ? max_x - min_x : 0.0f;
    float content_height = (max_y > min_y) ? max_y - min_y : 0.0f;
    if (result->has_width) content_width = result->width;
    if (result->has_height) content_height = result->height;

    if (!block->blk || block->blk->given_width < 0.0f) {
        custom_layout_apply_parent_axis(block, content_width, true);
    }
    if (!block->blk || block->blk->given_height < 0.0f) {
        custom_layout_apply_parent_axis(block, content_height, false);
    }
}

bool custom_layout_register(const char* name, CustomLayoutFn fn) {
    if (!name || name[0] == '\0' || !fn) return false;
    for (int i = 0; i < g_custom_layout_registry_count; i++) {
        if (strcmp(g_custom_layout_registry[i].name, name) == 0) {
            g_custom_layout_registry[i].fn = fn;
            return true;
        }
    }
    if (g_custom_layout_registry_count >= CUSTOM_LAYOUT_MAX_REGISTRY) {
        log_error("CUSTOM_LAYOUT_REGISTRY_FULL cannot register layout '%s'", name);
        return false;
    }
    // layout names can originate in Lambda/CSS-managed memory; copy the key
    // into registry-owned storage before later layout passes call strcmp().
    if (!custom_layout_copy_name(g_custom_layout_registry[g_custom_layout_registry_count].name,
        name, "CUSTOM_LAYOUT_REGISTRY")) {
        return false;
    }
    g_custom_layout_registry[g_custom_layout_registry_count].fn = fn;
    g_custom_layout_registry_count++;
    return true;
}

CustomLayoutFn custom_layout_lookup(const char* name) {
    if (!name || name[0] == '\0') return nullptr;
    for (int i = 0; i < g_custom_layout_registry_count; i++) {
        if (strcmp(g_custom_layout_registry[i].name, name) == 0) {
            return g_custom_layout_registry[i].fn;
        }
    }
    return nullptr;
}

void custom_layout_registry_clear(void) {
    for (int i = 0; i < g_custom_layout_registry_count; i++) {
        g_custom_layout_registry[i].name[0] = '\0';
        g_custom_layout_registry[i].fn = nullptr;
    }
    g_custom_layout_registry_count = 0;
    for (int i = 0; i < g_custom_layout_unknown_log_count; i++) {
        g_custom_layout_unknown_logs[i].doc = nullptr;
        g_custom_layout_unknown_logs[i].name[0] = '\0';
    }
    g_custom_layout_unknown_log_count = 0;
}

bool custom_layout_result_place(CustomLayoutResult* result, int child_index, float x, float y) {
    if (!result || !result->placements) return false;
    if (child_index < 0 || result->placement_count >= result->placement_capacity) return false;
    CustomLayoutPlacement* placement = &result->placements[result->placement_count];
    placement->child_index = child_index;
    placement->x = x;
    placement->y = y;
    result->placement_count++;
    return true;
}

const char* custom_layout_name_from_css_value(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION) return nullptr;
    CssFunction* fn = value->data.function;
    if (!fn || !fn->name || !str_ieq_const(fn->name, strlen(fn->name), "layout") ||
        !fn->args || fn->arg_count < 1 || !fn->args[0]) {
        return nullptr;
    }

    CssValue* arg = fn->args[0];
    if (arg->type == CSS_VALUE_TYPE_STRING) {
        return arg->data.string;
    }
    if (arg->type == CSS_VALUE_TYPE_CUSTOM) {
        return arg->data.custom_property.name;
    }
    if (arg->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(arg->data.keyword);
        return info ? info->name : nullptr;
    }
    return nullptr;
}

const char* custom_layout_name_for_element(DomElement* element) {
    if (!element) return nullptr;
    if (element->specified_style) {
        CssDeclaration* display_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_DISPLAY);
        if (display_decl) {
            const char* css_name = custom_layout_name_from_css_value(display_decl->value);
            if (css_name && css_name[0] != '\0') return css_name;
        }
    }
    const char* name = element->get_attribute("layout");
    if (name && name[0] != '\0') return name;
    name = element->get_attribute("data-layout");
    if (name && name[0] != '\0') return name;
    name = element->get_attribute("data-radiant-layout");
    if (name && name[0] != '\0') return name;
    return nullptr;
}

bool layout_custom_apply(LayoutContext* lycon, ViewBlock* block, const char* layout_name) {
    if (!lycon || !block || !layout_name || layout_name[0] == '\0') return false;
    CustomLayoutFn fn = custom_layout_lookup(layout_name);
    if (!fn) {
        custom_layout_log_unregistered_once(block, layout_name);
        return false;
    }

    int child_count = custom_layout_count_children(block);
    ScratchMark mark = scratch_mark(&lycon->scratch);
    int scratch_child_capacity = child_count > 0 ? child_count : 1;
    Velmt* children = (Velmt*)scratch_calloc(&lycon->scratch, sizeof(Velmt) * scratch_child_capacity);
    CustomLayoutPlacement* placements = (CustomLayoutPlacement*)scratch_calloc(
        &lycon->scratch, sizeof(CustomLayoutPlacement) * scratch_child_capacity);
    bool* placed_children = (bool*)scratch_calloc(
        &lycon->scratch, sizeof(bool) * scratch_child_capacity);
    if (!children || !placements || !placed_children) {
        scratch_restore(&lycon->scratch, mark);
        log_error("CUSTOM_LAYOUT_ALLOC_FAILED %s layout='%s' children=%d",
                  block->source_loc(), layout_name, child_count);
        return false;
    }

    child_count = custom_layout_collect_children(block, children, scratch_child_capacity);
    for (int i = 0; i < child_count; i++) {
        custom_layout_clear_child_z(&children[i]);
    }
    CustomLayoutContext context;
    memset(&context, 0, sizeof(context));
    context.lycon = lycon;
    context.parent = block;
    context.layout_name = layout_name;
    context.children = children;
    context.child_count = child_count;
    context.available_width = block->content_width;
    context.available_height = block->content_height;
    context.css_width = block->blk ? block->blk->given_width : -1.0f;
    context.css_height = block->blk ? block->blk->given_height : -1.0f;
    context.direction = (block->blk && block->blk->direction) ?
        block->blk->direction : lycon->block.direction;
    context.writing_mode = "horizontal-tb";
    custom_layout_set_child_constraints(&context);
    custom_layout_warn_auto_axis_percent_children(&context, true);
    custom_layout_warn_auto_axis_percent_children(&context, false);

    CustomLayoutResult result;
    memset(&result, 0, sizeof(result));
    result.placements = placements;
    result.placement_capacity = child_count;

    bool ok = fn(&context, &result);
    if (!ok) {
        scratch_restore(&lycon->scratch, mark);
        log_error("CUSTOM_LAYOUT_FAILED %s layout='%s'", block->source_loc(), layout_name);
        return false;
    }

    bool has_bounds = false;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    for (int i = 0; i < result.placement_count; i++) {
        CustomLayoutPlacement* placement = &result.placements[i];
        if (!custom_layout_placement_valid(&context, placement)) {
            log_error("CUSTOM_LAYOUT_INVALID_PLACEMENT %s layout='%s' child_index=%d",
                      block->source_loc(), layout_name, placement->child_index);
            continue;
        }
        if (placed_children[placement->child_index]) {
            log_error("CUSTOM_LAYOUT_DUPLICATE_PLACEMENT %s layout='%s' child_index=%d",
                      block->source_loc(), layout_name, placement->child_index);
            continue;
        }
        Velmt* child = &context.children[placement->child_index];
        child->view->x = placement->x;
        child->view->y = placement->y;
        if (placement->has_z) {
            custom_layout_apply_child_z(lycon, child, placement->z);
        }
        placed_children[placement->child_index] = true;

        float child_left = placement->x;
        float child_top = placement->y;
        float child_right = placement->x + child->border_box.width;
        float child_bottom = placement->y + child->border_box.height;
        if (!has_bounds) {
            min_x = child_left;
            min_y = child_top;
            max_x = child_right;
            max_y = child_bottom;
            has_bounds = true;
        } else {
            min_x = min(min_x, child_left);
            min_y = min(min_y, child_top);
            max_x = max(max_x, child_right);
            max_y = max(max_y, child_bottom);
        }
    }

    for (int i = 0; i < child_count; i++) {
        if (placed_children[i]) continue;
        Velmt* child = &context.children[i];
        // a successful custom layout pass owns every in-flow child position;
        // omitted placements must not leak normal-flow coordinates.
        child->view->x = 0.0f;
        child->view->y = 0.0f;
        float child_right = child->border_box.width;
        float child_bottom = child->border_box.height;
        if (!has_bounds) {
            min_x = 0.0f;
            min_y = 0.0f;
            max_x = child_right;
            max_y = child_bottom;
            has_bounds = true;
        } else {
            min_x = min(min_x, 0.0f);
            min_y = min(min_y, 0.0f);
            max_x = max(max_x, child_right);
            max_y = max(max_y, child_bottom);
        }
        log_error("CUSTOM_LAYOUT_MISSING_PLACEMENT %s layout='%s' child_index=%d",
                  block->source_loc(), layout_name, i);
    }

    custom_layout_apply_parent_size(block, &result, min_x, min_y, max_x, max_y);
    log_debug("CUSTOM_LAYOUT_APPLIED %s layout='%s' children=%d placements=%d size=%.1fx%.1f",
              block->source_loc(), layout_name, child_count, result.placement_count,
              block->width, block->height);
    scratch_restore(&lycon->scratch, mark);
    return true;
}
