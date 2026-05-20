#include "../radiant/rdt_vector.hpp"

static const RdtPath* g_test_path = nullptr;
static bool g_test_path_has_bounds = false;
static float g_test_path_left = 0.0f;
static float g_test_path_top = 0.0f;
static float g_test_path_right = 0.0f;
static float g_test_path_bottom = 0.0f;

static RdtPicture* g_test_picture = nullptr;
static float g_test_picture_w = 0.0f;
static float g_test_picture_h = 0.0f;
static char g_test_new_path;

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
    return (RdtPath*)&g_test_new_path;
}

void rdt_path_move_to(RdtPath* path, float x, float y) {
    (void)path; (void)x; (void)y;
}

void rdt_path_line_to(RdtPath* path, float x, float y) {
    (void)path; (void)x; (void)y;
}

void rdt_path_close(RdtPath* path) {
    (void)path;
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
