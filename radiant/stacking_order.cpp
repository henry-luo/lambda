#include "render.hpp"
#include "layout.hpp"
#include "../lib/mem.h"
#include "../lib/tagged.hpp"
extern "C" {
#include "../lib/log.h"
}

static int radiant_stack_view_z_index(View* view) {
    ViewElement* element = lam::view_as_element(view);
    if (!element || !element->position) return 0;
    return element->positionp()->has_custom_layout_z_index ?
        element->positionp()->custom_layout_z_index : element->positionp()->z_index;
}

bool radiant_stack_is_positive_z_positioned(View* view) {
    ViewElement* element = lam::view_as_element(view);
    if (element && element->position && element->positionp()->has_custom_layout_z_index) {
        // custom layout places normal-flow children but may still request
        // paint/hit-test stacking without rewriting authored CSS position.
        return element->positionp()->custom_layout_z_index > 0;
    }
    return element && element->position &&
        element->positionp()->z_index > 0 &&
        element->positionp()->position != CSS_VALUE_STATIC;
}

bool radiant_stack_is_out_of_flow_positioned(View* view) {
    return layout_view_is_out_of_flow_positioned(view);
}

bool radiant_stack_is_deferred_from_normal_flow(View* view) {
    return radiant_stack_is_positive_z_positioned(view) ||
        radiant_stack_is_out_of_flow_positioned(view);
}

static void radiant_stack_collect_positive_z_descendants_into(View* view, ArrayList* out_views,
                                                              const char* log_prefix) {
    while (view) {
        if (radiant_stack_is_positive_z_positioned(view)) {
            if (!arraylist_append(out_views, view)) {
                log_warn("%s failed to grow positive z-order list", log_prefix);
                return;
            }
        } else if (view->view_type == RDT_VIEW_INLINE) {
            ViewElement* element = lam::view_require_element(view);
            radiant_stack_collect_positive_z_descendants_into(element->first_child, out_views,
                                                              log_prefix);
        }
        view = view->next();
    }
}

ArrayList* radiant_stack_collect_positive_z_descendants(View* first_child, const char* log_prefix) {
    if (!first_child) return NULL;
    if (!log_prefix) log_prefix = "[RAD_STACK]";

    ArrayList* views = arraylist_new(16);
    if (!views) {
        log_warn("%s failed to allocate positive z-order list", log_prefix);
        return NULL;
    }
    radiant_stack_collect_positive_z_descendants_into(first_child, views, log_prefix);
    return views;
}

ArrayList* radiant_stack_collect_positioned_children(ViewBlock* block, const char* log_prefix) {
    if (!block || !block->position || !block->positionp()->first_abs_child) return NULL;
    if (!log_prefix) log_prefix = "[RAD_STACK]";

    ArrayList* views = arraylist_new(16);
    if (!views) {
        log_warn("%s failed to allocate positioned list", log_prefix);
        return NULL;
    }

    ViewBlock* child = block->positionp()->first_abs_child;
    while (child) {
        if (!arraylist_append(views, child)) {
            log_warn("%s failed to grow positioned list", log_prefix);
            break;
        }
        child = child->position ? child->positionp()->next_abs_sibling : NULL;
    }
    return views;
}

void radiant_stack_sort_in_paint_order(ArrayList* views) {
    if (!views) return;

    // Keep equal z-index entries in collection order so reverse hit-testing is
    // exactly the top-to-bottom inverse of the render walk's paint order.
    for (int i = 1; i < views->length; i++) {
        View* key = (View*)views->data[i];
        int key_z = radiant_stack_view_z_index(key);
        int j = i - 1;
        while (j >= 0) {
            View* current = (View*)views->data[j];
            int current_z = radiant_stack_view_z_index(current);
            if (current_z > key_z) {
                views->data[j + 1] = views->data[j];
                j--;
            } else {
                break;
            }
        }
        views->data[j + 1] = key;
    }
}

static bool radiant_stack_entry_after(const RadiantStackPaintEntry* left,
                                      const RadiantStackPaintEntry* right) {
    if (left->z != right->z) return left->z > right->z;
    if (left->is_generated_layer != right->is_generated_layer) {
        return !left->is_generated_layer;
    }
    return left->order > right->order;
}

RadiantStackPaintList radiant_stack_collect_custom_layout_paint(ViewBlock* block) {
    RadiantStackPaintList list = {};
    if (!block || !block->custom_layout_paint_prop()) return list;

    CustomLayoutPaintState* paint =
        (CustomLayoutPaintState*)block->custom_layout_paint_prop();
    int child_count = 0;
    for (View* child = block->first_child; child; child = child->next()) {
        if (child->view_type != RDT_VIEW_NONE &&
            !radiant_stack_is_out_of_flow_positioned(child)) {
            child_count++;
        }
    }
    int total = child_count + paint->layer_count;
    if (total <= 0) return list;
    list.entries = (RadiantStackPaintEntry*)mem_calloc(
        (size_t)total, sizeof(RadiantStackPaintEntry), MEM_CAT_RENDER);
    if (!list.entries) return list;

    for (int i = 0; i < paint->layer_count; i++) {
        RadiantStackPaintEntry* entry = &list.entries[list.count++];
        entry->layer = &paint->layers[i];
        entry->z = paint->layers[i].z;
        entry->order = paint->layers[i].order;
        entry->is_generated_layer = true;
    }
    int child_order = 0;
    for (View* child = block->first_child; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE ||
            radiant_stack_is_out_of_flow_positioned(child)) {
            continue;
        }
        RadiantStackPaintEntry* entry = &list.entries[list.count++];
        entry->view = child;
        entry->z = radiant_stack_view_z_index(child);
        entry->order = child_order++;
        entry->is_generated_layer = false;
    }

    for (int i = 1; i < list.count; i++) {
        RadiantStackPaintEntry key = list.entries[i];
        int j = i - 1;
        while (j >= 0 && radiant_stack_entry_after(&list.entries[j], &key)) {
            list.entries[j + 1] = list.entries[j];
            j--;
        }
        list.entries[j + 1] = key;
    }
    return list;
}

void radiant_stack_free_custom_layout_paint(RadiantStackPaintList* list) {
    if (!list) return;
    if (list->entries) mem_free(list->entries);
    list->entries = nullptr;
    list->count = 0;
}
