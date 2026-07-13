#include "layout_custom.hpp"
#include "layout_box.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/tagged.hpp"

#include <string.h>

#define CUSTOM_LAYOUT_MAX_REGISTRY 64

typedef struct CustomLayoutRegistryEntry {
    const char* name;
    CustomLayoutFn fn;
} CustomLayoutRegistryEntry;

static CustomLayoutRegistryEntry g_custom_layout_registry[CUSTOM_LAYOUT_MAX_REGISTRY];
static int g_custom_layout_registry_count = 0;

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

static void custom_layout_fill_velmt(Velmt* velmt, View* child, int index) {
    if (!velmt || !child) return;
    velmt->view = child;
    velmt->element = child->is_element() ? child->as_element() : nullptr;
    velmt->index = index;
    // Custom layout receives each child as a measured local border box; the
    // callback owns final placement and should not depend on normal-flow x/y.
    velmt->border_box.x = 0.0f;
    velmt->border_box.y = 0.0f;
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
        custom_layout_fill_velmt(&children[count], child, count);
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

static void custom_layout_apply_parent_size(ViewBlock* block,
                                            const CustomLayoutResult* result,
                                            float min_x,
                                            float min_y,
                                            float max_x,
                                            float max_y) {
    if (!block || !result) return;

    BoxMetrics metrics = layout_box_metrics(block);
    float content_width = (max_x > min_x) ? max_x - min_x : 0.0f;
    float content_height = (max_y > min_y) ? max_y - min_y : 0.0f;
    if (result->has_width) content_width = result->width;
    if (result->has_height) content_height = result->height;

    if (!block->blk || block->blk->given_width < 0.0f) {
        block->content_width = max(content_width, 0.0f);
        block->width = layout_floor_border_box_width(
            block, block->content_width + metrics.pad_border_h);
    }
    if (!block->blk || block->blk->given_height < 0.0f) {
        block->content_height = max(content_height, 0.0f);
        block->height = layout_floor_border_box_height(
            block, block->content_height + metrics.pad_border_v);
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
    g_custom_layout_registry[g_custom_layout_registry_count].name = name;
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
        log_debug("CUSTOM_LAYOUT_UNREGISTERED %s layout='%s'", block->source_loc(), layout_name);
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
        // A successful custom layout pass owns every in-flow child position;
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
