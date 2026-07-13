#include "render.hpp"
#include "state_store.hpp"

static int compare_view_order(View* view_a, View* view_b) {
    if (view_a == view_b) return 0;
    if (!view_a) return -1;
    if (!view_b) return 1;

    View* chain_a[64];
    View* chain_b[64];
    int depth_a = 0, depth_b = 0;

    for (View* v = view_a; v && depth_a < 64; v = v->parent) {
        chain_a[depth_a++] = v;
    }
    for (View* v = view_b; v && depth_b < 64; v = v->parent) {
        chain_b[depth_b++] = v;
    }

    int i = depth_a - 1, j = depth_b - 1;
    while (i >= 0 && j >= 0 && chain_a[i] == chain_b[j]) {
        i--; j--;
    }

    if (i < 0) {
        return -1;
    }
    if (j < 0) {
        return 1;
    }

    View* child_a = chain_a[i];
    View* child_b = chain_b[j];
    for (View* sib = child_a; sib; sib = static_cast<View*>(sib->next_sibling)) {
        if (sib == child_b) {
            return -1;
        }
    }
    return 1;
}

bool render_selection_contains_view(DocState* state, View* view) {
    if (!state || !view) return false;

    View* anchor_view = NULL;
    View* focus_view = NULL;
    if (!selection_get_extent_views(state, &anchor_view, &focus_view)) {
        return false;
    }

    if (!anchor_view || !focus_view || anchor_view == focus_view) {
        return false;
    }

    int anchor_vs_focus = compare_view_order(anchor_view, focus_view);
    View* first_view = (anchor_vs_focus <= 0) ? anchor_view : focus_view;
    View* last_view = (anchor_vs_focus <= 0) ? focus_view : anchor_view;

    int view_vs_first = compare_view_order(view, first_view);
    int view_vs_last = compare_view_order(view, last_view);
    return view_vs_first >= 0 && view_vs_last <= 0;
}
