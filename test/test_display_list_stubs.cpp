#include "../radiant/rdt_vector.hpp"
#include "../radiant/display_list_bounds.hpp"
#include "../radiant/state_store.hpp"
#include <string.h>

static const RdtPath* g_test_path = nullptr;
static bool g_test_path_has_bounds = false;
static float g_test_path_left = 0.0f;
static float g_test_path_top = 0.0f;
static float g_test_path_right = 0.0f;
static float g_test_path_bottom = 0.0f;

static RdtPicture* g_test_picture = nullptr;
static float g_test_picture_w = 0.0f;
static float g_test_picture_h = 0.0f;

struct RdtPath {
    struct Entry {
        RdtPathCommand command;
        float args[6];
        int arg_count;
    };
    Entry entries[32];
    int count;
};

static RdtPath g_test_new_path;

void test_display_list_stub_set_path_bounds(const RdtPath* path,
                                            bool has_bounds,
                                            float left, float top,
                                            float right, float bottom) {
    g_test_path = path;
    g_test_path_has_bounds = has_bounds;
    g_test_path_left = left;
    g_test_path_top = top;
    g_test_path_right = right;
    g_test_path_bottom = bottom;
}

void test_display_list_stub_set_picture_size(RdtPicture* picture, float w, float h) {
    g_test_picture = picture;
    g_test_picture_w = w;
    g_test_picture_h = h;
}

RdtPath* rdt_path_clone(const RdtPath* src) {
    return (RdtPath*)src;
}

RdtPath* rdt_path_new(void) {
    memset(&g_test_new_path, 0, sizeof(g_test_new_path));
    return &g_test_new_path;
}

void rdt_path_move_to(RdtPath* path, float x, float y) {
    if (!path || path->count >= 32) return;
    RdtPath::Entry* e = &path->entries[path->count++];
    e->command = RDT_PATH_MOVE;
    e->arg_count = 2;
    e->args[0] = x;
    e->args[1] = y;
}

void rdt_path_line_to(RdtPath* path, float x, float y) {
    if (!path || path->count >= 32) return;
    RdtPath::Entry* e = &path->entries[path->count++];
    e->command = RDT_PATH_LINE;
    e->arg_count = 2;
    e->args[0] = x;
    e->args[1] = y;
}

void rdt_path_cubic_to(RdtPath* path, float cx1, float cy1,
                       float cx2, float cy2, float x, float y) {
    if (!path || path->count >= 32) return;
    RdtPath::Entry* e = &path->entries[path->count++];
    e->command = RDT_PATH_CUBIC;
    e->arg_count = 6;
    e->args[0] = cx1;
    e->args[1] = cy1;
    e->args[2] = cx2;
    e->args[3] = cy2;
    e->args[4] = x;
    e->args[5] = y;
}

void rdt_path_close(RdtPath* path) {
    if (!path || path->count >= 32) return;
    RdtPath::Entry* e = &path->entries[path->count++];
    e->command = RDT_PATH_CLOSE;
    e->arg_count = 0;
}

void rdt_path_add_rect(RdtPath* path, float x, float y, float w, float h,
                       float rx, float ry) {
    if (!path || path->count >= 32) return;
    RdtPath::Entry* e = &path->entries[path->count++];
    e->command = RDT_PATH_RECT;
    e->arg_count = 6;
    e->args[0] = x;
    e->args[1] = y;
    e->args[2] = w;
    e->args[3] = h;
    e->args[4] = rx;
    e->args[5] = ry;
}

void rdt_path_add_circle(RdtPath* path, float cx, float cy, float rx, float ry) {
    if (!path || path->count >= 32) return;
    RdtPath::Entry* e = &path->entries[path->count++];
    e->command = RDT_PATH_CIRCLE;
    e->arg_count = 4;
    e->args[0] = cx;
    e->args[1] = cy;
    e->args[2] = rx;
    e->args[3] = ry;
}

void rdt_path_free(RdtPath* path) {
    (void)path;
}

bool rdt_path_get_bounds(const RdtPath* path, float* left, float* top,
                         float* right, float* bottom) {
    if (!path || path != g_test_path || !g_test_path_has_bounds) return false;
    if (left) *left = g_test_path_left;
    if (top) *top = g_test_path_top;
    if (right) *right = g_test_path_right;
    if (bottom) *bottom = g_test_path_bottom;
    return true;
}

bool rdt_path_visit(const RdtPath* path, RdtPathVisitFn fn, void* context) {
    if (!path || path != &g_test_new_path || !fn) return false;
    for (int i = 0; i < path->count; i++) {
        const RdtPath::Entry* e = &path->entries[i];
        if (!fn(context, e->command, e->args, e->arg_count)) return false;
    }
    return true;
}

RdtPicture* rdt_picture_dup(RdtPicture* pic) {
    return pic;
}

void rdt_picture_get_size(RdtPicture* pic, float* w, float* h) {
    if (pic == g_test_picture) {
        if (w) *w = g_test_picture_w;
        if (h) *h = g_test_picture_h;
        return;
    }
    if (w) *w = 0.0f;
    if (h) *h = 0.0f;
}

void rdt_picture_free(RdtPicture* pic) {
    (void)pic;
}

void rdt_push_clip(RdtVector* vec, RdtPath* clip, const RdtMatrix* transform) {
    (void)vec; (void)clip; (void)transform;
}

void rdt_pop_clip(RdtVector* vec) {
    (void)vec;
}

bool dirty_tracker_bounds(DirtyTracker* tracker, Bound* out_bounds, float scale) {
    if (!tracker || !out_bounds || !tracker->dirty_list || tracker->full_repaint) {
        return false;
    }

    // display-list tests link replay helpers without the full state store.
    DirtyRect* dirty = tracker->dirty_list;
    float left = dirty->x * scale;
    float top = dirty->y * scale;
    float right = (dirty->x + dirty->width) * scale;
    float bottom = (dirty->y + dirty->height) * scale;
    for (dirty = dirty->next; dirty; dirty = dirty->next) {
        float rect_left = dirty->x * scale;
        float rect_top = dirty->y * scale;
        float rect_right = (dirty->x + dirty->width) * scale;
        float rect_bottom = (dirty->y + dirty->height) * scale;
        if (rect_left < left) left = rect_left;
        if (rect_top < top) top = rect_top;
        if (rect_right > right) right = rect_right;
        if (rect_bottom > bottom) bottom = rect_bottom;
    }

    *out_bounds = {left, top, right, bottom};
    return true;
}
