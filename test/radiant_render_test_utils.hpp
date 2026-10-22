#pragma once
#include <gtest/gtest.h>
#include "../radiant/view.hpp"
#include "../radiant/render.hpp"

inline DisplayItem test_radiant_command(const DisplayList* list, int ordinal) {
    DisplayListCursor cursor(list);
    while (cursor.next()) if (ordinal-- == 0) return *cursor.item;
    ADD_FAILURE() << "missing expanded display command";
    return {};
}

inline void test_radiant_replay_surface(DisplayList* list, ImageSurface* surface,
        unsigned mode, DirtyRect* dirty_rect = nullptr, int tile_x = 0, int tile_y = 0) {
    RdtVector vector = {}; ScratchArena scratch = {};
    // pixel effects allocate during replay; an unbound scratch arena silently omits their paint.
    Arena* scratch_arena = arena_create_default(); ASSERT_NE(scratch_arena, nullptr);
    scratch_init(&scratch, scratch_arena);
    rdt_vector_init(&vector, (uint32_t*)surface->pixels, surface->width, surface->height, surface->pitch / 4);
    Bound viewport = {0, 0, (float)surface->width, (float)surface->height};
    DirtyTracker dirty = {}; dirty.dirty_list = dirty_rect;
    if (mode == 1) dl_replay_tile(list, &vector, surface, &scratch, tile_x, tile_y, surface->width, surface->height, 1);
    else dl_replay(list, &vector, surface, &viewport, &scratch, 1, mode == 2 ? &dirty : nullptr);
    rdt_vector_destroy(&vector); scratch_release(&scratch); arena_destroy(scratch_arena);
}

inline void test_radiant_expect_surface_pixel(const ImageSurface* surface,
        unsigned x, unsigned y, unsigned r, unsigned g, unsigned b, unsigned tolerance = 2) {
    ASSERT_NE(surface, nullptr);
    ASSERT_LT(x, (unsigned)surface->width); ASSERT_LT(y, (unsigned)surface->height);
    const uint8_t* pixel = (const uint8_t*)surface->pixels + y * surface->pitch + x * 4;
    const unsigned expected[] = {r, g, b};
    for (unsigned channel = 0; channel < 3; channel++) {
        unsigned difference = pixel[channel] > expected[channel]
            ? pixel[channel] - expected[channel] : expected[channel] - pixel[channel];
        EXPECT_LE(difference, tolerance) << "pixel " << x << "," << y << " channel " << channel;
    }
}

inline void test_radiant_expect_pixels_equal(const uint32_t* actual, const uint32_t* expected, size_t count) {
    for (size_t pixel = 0; pixel < count; pixel++) if (actual[pixel] != expected[pixel]) {
        EXPECT_EQ(actual[pixel], expected[pixel]) << "pixel index " << pixel;
        return;
    }
}

inline void test_radiant_fill_native_rect(RdtVector* vector, float x, float y, float w, float h, Color color) {
    RdtPath* path = rdt_path_new(); ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, x, y, w, h, 0, 0);
    rdt_fill_path(vector, path, color, RDT_FILL_WINDING, nullptr);
    rdt_path_free(path);
}
