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
        radiant_stack_is_negative_z_block(view) ||
        radiant_stack_is_out_of_flow_positioned(view);
}

bool radiant_stack_is_negative_z_block(View* view) {
    ViewBlock* block = lam::view_as_block(view);
    return block && block->position && block->positionp()->z_index < 0 &&
        block->positionp()->position != CSS_VALUE_STATIC;
}

bool radiant_stack_is_context(View* view) {
    if (!view || !view->parent) return true;
    ViewElement* element = lam::view_as_element(view);
    if (!element) return false;
    // a selected view root can retain its DOM parent; it still owns the initial stacking context.
    if (element->doc && element->doc->view_tree && element->doc->view_tree->root == view) return true;
    if (dom_element_is_svg(element)) {
        // SVG 2 §3.4.1 isolates foreignObject and each outermost SVG fragment from its parent stack.
        if (element->tag() == MARKUP_NAME_FOREIGNOBJECT) return true;
        DomElement* parent = element->parent ? element->parent->as_element() : nullptr;
        if (element->tag() == MARKUP_NAME_SVG && (!parent || !dom_element_is_svg(parent))) return true;
    }
    const char* clip = render_clip_css_value(element);
    if (clip && strncmp(clip, "none", 4) != 0) return true;
    const PositionProp* position = element->positionp();
    if (position->has_custom_layout_z_index ||
        (position->position != CSS_VALUE_STATIC && (position->has_z_index || position->z_index != 0)) ||
        position->position == CSS_VALUE_FIXED || position->position == CSS_VALUE_STICKY) return true;
    if (transform_has_functions(element->transform) ||
        (element->transform && element->transformp()->perspective > 0.0f) ||
        (element->in_line && (element->inl()->isolation == CSS_VALUE_ISOLATE || element->inl()->opacity < 0.9995f ||
            (element->inl()->mix_blend_mode && element->inl()->mix_blend_mode != CSS_VALUE_NORMAL))) ||
        element->filter_prop() || element->backdrop_filter_prop() ||
        (element->bound && element->boundary()->mask)) return true;
    ViewBlock* block = lam::view_as_block(view);
    return block && ((block->blk && block->block()->contain_positioning) || block->custom_layout_paint_prop());
}

static bool radiant_stack_is_flex_grid_item(View* view) {
    ViewElement* element = lam::view_as_element(view);
    if (!element) return false;
    // committed item membership already accounts for display:contents ancestry.
    DomElement::ParentItemKind kind = element->parent_item_kind();
    return kind == DomElement::PARENT_ITEM_FLEX || kind == DomElement::PARENT_ITEM_GRID;
}

bool radiant_stack_is_float(View* view) {
    ViewElement* element = lam::view_as_element(view);
    return element && !radiant_stack_is_flex_grid_item(view) &&
        !layout_position_is_abs_fixed(element->position) && layout_position_is_floated(element->position);
}

bool radiant_stack_is_paint_atomic(View* view) {
    ViewElement* element = lam::view_as_element(view);
    // inline tables and flex/grid items paint atomically like inline blocks (CSS 2.2 E.2).
    return view && (radiant_stack_is_context(view) || view->view_type == RDT_VIEW_INLINE_BLOCK ||
        (element && element->display.outer == CSS_VALUE_INLINE && element->display.inner == CSS_VALUE_TABLE) ||
        radiant_stack_is_flex_grid_item(view));
}

unsigned radiant_stack_flow_phases(View* first_child) {
    unsigned phases = 0;
    const unsigned flow = (1u << RADIANT_PAINT_BLOCK_BACKGROUNDS) | (1u << RADIANT_PAINT_FOREGROUND);
    for (View* child = first_child; child; child = child->next()) {
        if (radiant_stack_is_deferred_from_normal_flow(child) ||
            radiant_stack_is_in_flow_positioned_step8(child)) continue;
        if (radiant_stack_is_float(child)) {
            phases |= flow | (1u << RADIANT_PAINT_FLOATS);
            continue;
        }
        if (radiant_stack_is_paint_atomic(child)) continue;
        // ordinary block boxes own a background/hit layer even when their fill is transparent.
        if (child->is_block()) phases |= flow;
        if (view_contents_are_skipped(child)) continue;
        ViewElement* element = lam::view_as_element(child);
        if (element) phases |= radiant_stack_flow_phases(element->first_child);
    }
    return phases;
}

bool radiant_stack_in_paint_phase(View* view, RadiantPaintPhase phase) {
    if (phase == RADIANT_PAINT_ALL) return true;
    if (radiant_stack_is_in_flow_positioned_step8(view)) return false;
    if (radiant_stack_is_float(view)) return phase == RADIANT_PAINT_FLOATS;
    if (radiant_stack_is_paint_atomic(view) || view->view_type == RDT_VIEW_TEXT ||
        view->view_type == RDT_VIEW_MARKER) return phase == RADIANT_PAINT_FOREGROUND;
    // ordinary ancestors remain traversable to preserve each descendant's font/clip frame.
    return true;
}

RadiantPaintPhase radiant_stack_paint_phase(unsigned index) {
    static const RadiantPaintPhase phases[] = {RADIANT_PAINT_BLOCK_BACKGROUNDS,
        RADIANT_PAINT_FLOATS, RADIANT_PAINT_FOREGROUND};
    return index < 3 ? phases[index] : RADIANT_PAINT_ALL;
}

View* radiant_stack_path_child(View* ancestor, View* descendant) {
    for (View* child = descendant; child; child = child->parent) {
        if (child->parent == ancestor) return child;
    }
    return nullptr;
}



static void radiant_stack_collect_z_into(View* view, ArrayList* out, bool negative) {
    for (View* child = view; child; child = child->next()) {
        // hidden DOM records retain styles; neither they nor their descendants own paint layers.
        if (child->view_type == RDT_VIEW_NONE) continue;
        if (negative ? radiant_stack_is_negative_z_block(child) : radiant_stack_is_positive_z_positioned(child)) {
            if (!arraylist_append(out, child)) {
                log_warn("[RAD_STACK_LAYERS] failed to grow context paint list");
                return;
            }
        } else {
            ViewElement* element = lam::view_as_element(child);
            if (element && element->first_child && !view_contents_are_skipped(child) && !radiant_stack_is_context(child))
                radiant_stack_collect_z_into(element->first_child, out, negative);
        }
    }
}

static ArrayList* radiant_stack_collect_z_list(View* first_child, bool negative) {
    ArrayList* views = arraylist_new(8);
    if (!views) return nullptr;
    radiant_stack_collect_z_into(first_child, views, negative);
    radiant_stack_sort_in_paint_order(views);
    return views;
}

ArrayList* radiant_stack_collect_positive_z_descendants(View* first_child, const char*) {
    return first_child ? radiant_stack_collect_z_list(first_child, false) : nullptr;
}

static ArrayList* radiant_stack_collect_z_descendants(View* owner, bool negative) {
    ViewElement* element = lam::view_as_element(owner);
    return element && radiant_stack_is_context(owner) ?
        radiant_stack_collect_z_list(element->first_child, negative) : nullptr;
}

static void radiant_stack_visit_z_descendants(View* owner, bool negative, bool reverse, void* context,
        bool (*visit)(void* context, View* owner, View* layer)) {
    ArrayList* views = radiant_stack_collect_z_descendants(owner, negative);
    if (!views) return;
    for (int i = 0; i < views->length; i++) {
        int index = reverse ? views->length - 1 - i : i;
        if (!visit(context, owner, (View*)views->data[index])) break;
    }
    arraylist_free(views);
}

void radiant_stack_visit_negative_z_descendants(View* owner, bool reverse, void* context,
        bool (*visit)(void* context, View* owner, View* layer)) {
    radiant_stack_visit_z_descendants(owner, true, reverse, context, visit);
}

void radiant_stack_visit_positive_z_descendants(View* owner, bool reverse, void* context,
        bool (*visit)(void* context, View* owner, View* layer)) {
    // positive layers belong to the stacking context, across ordinary paragraph ancestors.
    radiant_stack_visit_z_descendants(owner, false, reverse, context, visit);
}

// CSS 2.1 Appendix E step 8: in-flow positioned boxes (relative, sticky) with
// z-index auto or 0 paint after their non-positioned siblings, in tree order,
// so one pulled over a later sibling stays on top. Stacking is resolved among
// siblings, as for positive z; negative blocks belong to their enclosing context.
bool radiant_stack_is_in_flow_positioned_step8(View* view) {
    ViewElement* element = lam::view_as_element(view);
    if (!element) return false;
    ViewBlock* block = lam::view_as_block(view);
    // layout/paint containment creates a stacking context even for static boxes.
    if (block && block->blk && block->block()->contain_positioning &&
        (!element->position || element->positionp()->position == CSS_VALUE_STATIC ||
         element->positionp()->z_index == 0)) return true;
    if (!element->position) return false;
    const PositionProp* position = element->positionp();
    // a custom layout sequences its own children (radiant_stack_collect_custom_layout_paint)
    if (position->has_custom_layout_z_index) return false;
    return (position->position == CSS_VALUE_RELATIVE ||
            position->position == CSS_VALUE_STICKY) &&
        position->z_index == 0;
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
        if (child->view_type != RDT_VIEW_NONE && !radiant_stack_is_negative_z_block(child) &&
            (!radiant_stack_is_positive_z_positioned(child) || block->custom_layout_paint_prop()) &&
            !arraylist_append(views, child)) {
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
    list.entries = lam::own_arr((RadiantStackPaintEntry*)mem_calloc(
        (size_t)total, sizeof(RadiantStackPaintEntry), MEM_CAT_RENDER));
    if (!list.entries) return list;

    for (int i = 0; i < paint->layer_count; i++) {
        RadiantStackPaintEntry* entry = &list.entries[list.count++];
        entry->layer = lam::up(&paint->layers[i]);
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
        entry->view = lam::up(child);
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
    lam::free_owned(list->entries);
    list->count = 0;
}
