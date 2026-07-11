#include "stacking_order.hpp"
#include "layout.hpp"
#include "../lib/tagged.hpp"
extern "C" {
#include "../lib/log.h"
}

static int radiant_stack_view_z_index(View* view) {
    ViewElement* element = lam::view_as_element(view);
    return element && element->position ? element->position->z_index : 0;
}

bool radiant_stack_is_positive_z_positioned(View* view) {
    ViewElement* element = lam::view_as_element(view);
    return element && element->position &&
        element->position->z_index > 0 &&
        element->position->position != CSS_VALUE_STATIC;
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
    if (!block || !block->position || !block->position->first_abs_child) return NULL;
    if (!log_prefix) log_prefix = "[RAD_STACK]";

    ArrayList* views = arraylist_new(16);
    if (!views) {
        log_warn("%s failed to allocate positioned list", log_prefix);
        return NULL;
    }

    ViewBlock* child = block->position->first_abs_child;
    while (child) {
        if (!arraylist_append(views, child)) {
            log_warn("%s failed to grow positioned list", log_prefix);
            break;
        }
        child = child->position ? child->position->next_abs_sibling : NULL;
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
