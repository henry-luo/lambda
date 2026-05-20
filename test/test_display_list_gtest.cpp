#include <gtest/gtest.h>

#include "../radiant/display_list.h"
#include "../radiant/display_list_bounds.hpp"
#include "../radiant/display_list_replay_state.hpp"
#include "../radiant/display_list_storage.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"

void test_display_list_stub_set_path_bounds(const RdtPath* path,
                                            bool has_bounds,
                                            float left, float top,
                                            float right, float bottom);
void test_display_list_stub_set_picture_size(RdtPicture* picture, float w, float h);

class DisplayListTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Arena* arena = nullptr;
    DisplayList dl = {};

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        dl_init(&dl, arena);
    }

    void TearDown() override {
        dl_destroy(&dl);
        if (arena) {
            arena_destroy(arena);
            arena = nullptr;
        }
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }
};

static Color test_color(uint32_t value) {
    Color color = {};
    color.c = value;
    return color;
}

static void test_set_zero_bound_state_item(DisplayItem* item, DisplayOp op) {
    ASSERT_NE(item, nullptr);
    item->op = op;
    item->bounds[0] = 0.0f;
    item->bounds[1] = 0.0f;
    item->bounds[2] = 0.0f;
    item->bounds[3] = 0.0f;
}

TEST_F(DisplayListTest, BoundsIntersectorPreservesReplayStateCommands) {
    DisplayItem* clip = dl_alloc_item(&dl);
    ASSERT_NE(clip, nullptr);
    clip->op = DL_PUSH_CLIP;
    clip->bounds[0] = 0.0f;
    clip->bounds[1] = 0.0f;
    clip->bounds[2] = 0.0f;
    clip->bounds[3] = 0.0f;

    DisplayItem* rect = dl_alloc_item(&dl);
    ASSERT_NE(rect, nullptr);
    rect->op = DL_FILL_RECT;
    rect->bounds[0] = 100.0f;
    rect->bounds[1] = 100.0f;
    rect->bounds[2] = 10.0f;
    rect->bounds[3] = 10.0f;

    EXPECT_TRUE(dl_item_intersects_rect(clip, 500.0f, 500.0f, 10.0f, 10.0f));
    EXPECT_FALSE(dl_item_intersects_rect(rect, 0.0f, 0.0f, 10.0f, 10.0f));
    EXPECT_TRUE(dl_item_intersects_rect(rect, 105.0f, 105.0f, 2.0f, 2.0f));
}

TEST_F(DisplayListTest, DirtyCullingKeepsStatefulEffectCommands) {
    DisplayOp state_ops[] = {
        DL_POP_CLIP,
        DL_SAVE_BACKDROP,
        DL_APPLY_BLEND_MODE,
        DL_COMPOSITE_OPACITY,
        DL_SHADOW_CLIP_SAVE,
        DL_SHADOW_CLIP_RESTORE,
        DL_BEGIN_ELEMENT,
        DL_END_ELEMENT
    };

    for (int i = 0; i < 8; i++) {
        DisplayItem item = {};
        test_set_zero_bound_state_item(&item, state_ops[i]);
        EXPECT_TRUE(dl_item_intersects_rect(&item, 400.0f, 400.0f, 10.0f, 10.0f))
            << "state op " << state_ops[i] << " must survive dirty replay";
    }

    DisplayItem drawable = {};
    test_set_zero_bound_state_item(&drawable, DL_APPLY_FILTER);
    EXPECT_FALSE(dl_item_intersects_rect(&drawable, 400.0f, 400.0f, 10.0f, 10.0f));
}

TEST_F(DisplayListTest, ElementMarkersKeepLayoutBoundsAndUseVisualUnionBounds) {
    int begin = dl_begin_element(&dl, 77, 10.0f, 20.0f, 30.0f, 40.0f);
    dl_fill_rect(&dl, 100.0f, 200.0f, 5.0f, 6.0f, test_color(0xff112233));
    dl_end_element(&dl, begin);

    ASSERT_EQ(dl.count, 3);
    DisplayItem* start = &dl.items[0];
    DisplayItem* end = &dl.items[2];

    EXPECT_EQ(start->op, DL_BEGIN_ELEMENT);
    EXPECT_EQ(end->op, DL_END_ELEMENT);
    EXPECT_EQ(start->element_marker.matching_index, 2);
    EXPECT_EQ(end->element_marker.matching_index, 0);

    EXPECT_FLOAT_EQ(start->element_marker.marker_x, 10.0f);
    EXPECT_FLOAT_EQ(start->element_marker.marker_y, 20.0f);
    EXPECT_FLOAT_EQ(start->element_marker.marker_w, 30.0f);
    EXPECT_FLOAT_EQ(start->element_marker.marker_h, 40.0f);

    EXPECT_FLOAT_EQ(start->bounds[0], 10.0f);
    EXPECT_FLOAT_EQ(start->bounds[1], 20.0f);
    EXPECT_FLOAT_EQ(start->bounds[2], 95.0f);
    EXPECT_FLOAT_EQ(start->bounds[3], 186.0f);
    EXPECT_FLOAT_EQ(end->bounds[0], start->bounds[0]);
    EXPECT_FLOAT_EQ(end->bounds[1], start->bounds[1]);
    EXPECT_FLOAT_EQ(end->bounds[2], start->bounds[2]);
    EXPECT_FLOAT_EQ(end->bounds[3], start->bounds[3]);
}

TEST_F(DisplayListTest, FillPathBoundsUseTransformAndPadding) {
    char path_token = 0;
    RdtPath* path = (RdtPath*)&path_token;
    test_display_list_stub_set_path_bounds(path, true, 2.0f, 3.0f, 7.0f, 11.0f);

    RdtMatrix transform = rdt_matrix_translate(10.0f, 20.0f);
    dl_fill_path(&dl, path, test_color(0xff445566), RDT_FILL_WINDING, &transform);

    ASSERT_EQ(dl.count, 1);
    const DisplayItem* item = &dl.items[0];
    EXPECT_EQ(item->op, DL_FILL_PATH);
    EXPECT_EQ(item->fill_path.path, path);
    EXPECT_TRUE(item->fill_path.has_transform);
    EXPECT_FLOAT_EQ(item->bounds[0], 11.0f);
    EXPECT_FLOAT_EQ(item->bounds[1], 22.0f);
    EXPECT_FLOAT_EQ(item->bounds[2], 7.0f);
    EXPECT_FLOAT_EQ(item->bounds[3], 10.0f);
}

TEST_F(DisplayListTest, StrokePathCopiesDashArrayIntoDisplayListArena) {
    char path_token = 0;
    RdtPath* path = (RdtPath*)&path_token;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 10.0f, 4.0f);

    float dashes[2] = {2.0f, 3.0f};
    dl_stroke_path(&dl, path, test_color(0xffabcdef), 2.0f,
                   RDT_CAP_BUTT, RDT_JOIN_MITER, dashes, 2, 1.0f, nullptr);
    dashes[0] = 20.0f;
    dashes[1] = 30.0f;

    ASSERT_EQ(dl.count, 1);
    const DisplayItem* item = &dl.items[0];
    ASSERT_NE(item->stroke_path.dash_array, nullptr);
    EXPECT_NE(item->stroke_path.dash_array, dashes);
    EXPECT_FLOAT_EQ(item->stroke_path.dash_array[0], 2.0f);
    EXPECT_FLOAT_EQ(item->stroke_path.dash_array[1], 3.0f);
    EXPECT_FLOAT_EQ(item->bounds[0], -10.0f);
    EXPECT_FLOAT_EQ(item->bounds[1], -10.0f);
    EXPECT_FLOAT_EQ(item->bounds[2], 30.0f);
    EXPECT_FLOAT_EQ(item->bounds[3], 24.0f);
}

TEST_F(DisplayListTest, LinearGradientCopiesStopsAndTracksPathBounds) {
    char path_token = 0;
    RdtPath* path = (RdtPath*)&path_token;
    test_display_list_stub_set_path_bounds(path, true, 4.0f, 6.0f, 14.0f, 16.0f);

    RdtGradientStop stops[2] = {
        {0.0f, 10, 20, 30, 255},
        {1.0f, 40, 50, 60, 128}
    };
    dl_fill_linear_gradient(&dl, path, 0.0f, 0.0f, 10.0f, 10.0f,
                            stops, 2, RDT_FILL_EVEN_ODD, nullptr);
    stops[0].r = 99;

    ASSERT_EQ(dl.count, 1);
    const DisplayItem* item = &dl.items[0];
    ASSERT_NE(item->fill_linear_gradient.stops, nullptr);
    EXPECT_NE(item->fill_linear_gradient.stops, stops);
    EXPECT_EQ(item->fill_linear_gradient.stops[0].r, 10);
    EXPECT_EQ(item->fill_linear_gradient.stops[1].a, 128);
    EXPECT_FLOAT_EQ(item->bounds[0], 3.0f);
    EXPECT_FLOAT_EQ(item->bounds[1], 5.0f);
    EXPECT_FLOAT_EQ(item->bounds[2], 12.0f);
    EXPECT_FLOAT_EQ(item->bounds[3], 12.0f);
}

TEST_F(DisplayListTest, RadialGradientCopiesStopsAndTracksTransformedBounds) {
    char path_token = 0;
    RdtPath* path = (RdtPath*)&path_token;
    test_display_list_stub_set_path_bounds(path, true, 1.0f, 2.0f, 9.0f, 12.0f);

    RdtGradientStop stops[2] = {
        {0.0f, 1, 2, 3, 255},
        {1.0f, 4, 5, 6, 128}
    };
    RdtMatrix transform = rdt_matrix_translate(20.0f, 30.0f);
    dl_fill_radial_gradient(&dl, path, 5.0f, 6.0f, 7.0f,
                            stops, 2, RDT_FILL_WINDING, &transform);
    stops[1].a = 99;

    ASSERT_EQ(dl.count, 1);
    const DisplayItem* item = &dl.items[0];
    EXPECT_EQ(item->op, DL_FILL_RADIAL_GRADIENT);
    ASSERT_NE(item->fill_radial_gradient.stops, nullptr);
    EXPECT_NE(item->fill_radial_gradient.stops, stops);
    EXPECT_EQ(item->fill_radial_gradient.stops[1].a, 128);
    EXPECT_TRUE(item->fill_radial_gradient.has_transform);
    EXPECT_FLOAT_EQ(item->bounds[0], 20.0f);
    EXPECT_FLOAT_EQ(item->bounds[1], 31.0f);
    EXPECT_FLOAT_EQ(item->bounds[2], 10.0f);
    EXPECT_FLOAT_EQ(item->bounds[3], 12.0f);
}

TEST_F(DisplayListTest, DrawImageStoresGenerationOpacityAndTransformedBounds) {
    uint32_t pixels[4] = {0, 1, 2, 3};
    ImageSurface owner = {};
    owner.generation = 42;
    RdtMatrix transform = rdt_matrix_translate(5.0f, -2.0f);

    dl_draw_image(&dl, pixels, 2, 2, 2, 10.0f, 20.0f, 30.0f, 40.0f,
                  123, &transform, &owner, owner.generation);

    ASSERT_EQ(dl.count, 1);
    const DisplayItem* item = &dl.items[0];
    EXPECT_EQ(item->op, DL_DRAW_IMAGE);
    EXPECT_EQ(item->draw_image.pixels, pixels);
    EXPECT_EQ(item->draw_image.resource_owner, &owner);
    EXPECT_EQ(item->draw_image.resource_generation, 42u);
    EXPECT_EQ(item->draw_image.opacity, 123);
    EXPECT_TRUE(item->draw_image.has_transform);
    EXPECT_FLOAT_EQ(item->bounds[0], 14.0f);
    EXPECT_FLOAT_EQ(item->bounds[1], 17.0f);
    EXPECT_FLOAT_EQ(item->bounds[2], 32.0f);
    EXPECT_FLOAT_EQ(item->bounds[3], 42.0f);
}

TEST_F(DisplayListTest, DrawGlyphIntersectsRecordedBoundsWithClip) {
    uint8_t glyph_pixels[16] = {};
    GlyphBitmap bitmap = {};
    bitmap.buffer = glyph_pixels;
    bitmap.width = 10;
    bitmap.height = 8;
    bitmap.pitch = 10;
    Bound clip = {7.0f, 9.0f, 20.0f, 20.0f};

    dl_draw_glyph(&dl, &bitmap, 5, 6, test_color(0xff123456),
                  false, &clip, nullptr, 88);

    ASSERT_EQ(dl.count, 1);
    const DisplayItem* item = &dl.items[0];
    EXPECT_EQ(item->op, DL_DRAW_GLYPH);
    EXPECT_EQ(item->draw_glyph.bitmap.buffer, glyph_pixels);
    EXPECT_EQ(item->draw_glyph.resource_generation, 88u);
    EXPECT_FLOAT_EQ(item->bounds[0], 7.0f);
    EXPECT_FLOAT_EQ(item->bounds[1], 9.0f);
    EXPECT_FLOAT_EQ(item->bounds[2], 9.0f);
    EXPECT_FLOAT_EQ(item->bounds[3], 6.0f);
}

TEST_F(DisplayListTest, DrawPictureUsesBackendSizeForBounds) {
    char picture_token = 0;
    RdtPicture* picture = (RdtPicture*)&picture_token;
    test_display_list_stub_set_picture_size(picture, 32.0f, 18.0f);

    dl_draw_picture(&dl, picture, 200, nullptr);

    ASSERT_EQ(dl.count, 1);
    const DisplayItem* item = &dl.items[0];
    EXPECT_EQ(item->op, DL_DRAW_PICTURE);
    EXPECT_EQ(item->draw_picture.picture, picture);
    EXPECT_EQ(item->draw_picture.opacity, 200);
    EXPECT_FLOAT_EQ(item->bounds[0], -1.0f);
    EXPECT_FLOAT_EQ(item->bounds[1], -1.0f);
    EXPECT_FLOAT_EQ(item->bounds[2], 34.0f);
    EXPECT_FLOAT_EQ(item->bounds[3], 20.0f);
}

TEST_F(DisplayListTest, ClipCommandsRecordPreciseBoundsButPopStaysStateful) {
    char path_token = 0;
    RdtPath* path = (RdtPath*)&path_token;
    test_display_list_stub_set_path_bounds(path, true, 4.0f, 5.0f, 14.0f, 17.0f);

    RdtMatrix transform = rdt_matrix_translate(-2.0f, 3.0f);
    dl_push_clip(&dl, path, &transform);
    dl_pop_clip(&dl);

    ASSERT_EQ(dl.count, 2);
    EXPECT_EQ(dl.items[0].op, DL_PUSH_CLIP);
    EXPECT_TRUE(dl.items[0].push_clip.has_transform);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[0], 1.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[1], 7.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[2], 12.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[3], 14.0f);
    EXPECT_TRUE(dl_item_intersects_rect(&dl.items[0], 2.0f, 8.0f, 1.0f, 1.0f));
    EXPECT_FALSE(dl_item_intersects_rect(&dl.items[0], 40.0f, 40.0f, 1.0f, 1.0f));

    EXPECT_EQ(dl.items[1].op, DL_POP_CLIP);
    EXPECT_TRUE(dl_item_intersects_rect(&dl.items[1], 40.0f, 40.0f, 1.0f, 1.0f));
}

TEST_F(DisplayListTest, RasterCommandsStoreClipAndCopiedClipShapes) {
    float vx[3] = {1.0f, 2.0f, 3.0f};
    float vy[3] = {4.0f, 5.0f, 6.0f};
    ClipShape polygon = {};
    polygon.type = CLIP_SHAPE_POLYGON;
    polygon.polygon.vx = vx;
    polygon.polygon.vy = vy;
    polygon.polygon.count = 3;
    ClipShape* shapes[1] = {&polygon};
    Bound clip = {2.0f, 3.0f, 9.0f, 10.0f};

    dl_fill_surface_rect(&dl, 0.0f, 0.0f, 20.0f, 20.0f,
                         0xff998877, &clip, shapes, 1);
    vx[0] = 100.0f;
    vy[0] = 200.0f;

    ASSERT_EQ(dl.count, 1);
    const DisplayItem* item = &dl.items[0];
    EXPECT_EQ(item->op, DL_FILL_SURFACE_RECT);
    EXPECT_FLOAT_EQ(item->bounds[0], 2.0f);
    EXPECT_FLOAT_EQ(item->bounds[1], 3.0f);
    EXPECT_FLOAT_EQ(item->bounds[2], 7.0f);
    EXPECT_FLOAT_EQ(item->bounds[3], 7.0f);
    EXPECT_EQ(item->fill_surface_rect.clip_shapes.depth, 1);
    EXPECT_EQ(item->fill_surface_rect.clip_shapes.type[0], CLIP_SHAPE_POLYGON);
    ASSERT_NE(item->fill_surface_rect.clip_shapes.polygon_vx[0], nullptr);
    ASSERT_NE(item->fill_surface_rect.clip_shapes.polygon_vy[0], nullptr);
    EXPECT_NE(item->fill_surface_rect.clip_shapes.polygon_vx[0], vx);
    EXPECT_NE(item->fill_surface_rect.clip_shapes.polygon_vy[0], vy);
    EXPECT_FLOAT_EQ(item->fill_surface_rect.clip_shapes.polygon_vx[0][0], 1.0f);
    EXPECT_FLOAT_EQ(item->fill_surface_rect.clip_shapes.polygon_vy[0][0], 4.0f);
}

TEST_F(DisplayListTest, BlitAndExternalLayerCommandsStoreGenerations) {
    ImageSurface src = {};
    src.generation = 17;
    Bound clip = {10.0f, 10.0f, 40.0f, 40.0f};

    dl_blit_surface_scaled(&dl, &src, 0.0f, 0.0f, 50.0f, 50.0f,
                           2, &clip, nullptr, 0, 77, src.generation);
    dl_video_placeholder(&dl, &src, 1.0f, 2.0f, 3.0f, 4.0f, 5, &clip, 18);
    dl_webview_layer_placeholder(&dl, &src, 2.0f, 3.0f, 4.0f, 5.0f, &clip, 19);

    ASSERT_EQ(dl.count, 3);
    EXPECT_EQ(dl.items[0].op, DL_BLIT_SURFACE_SCALED);
    EXPECT_EQ(dl.items[0].blit_surface_scaled.src_surface, &src);
    EXPECT_EQ(dl.items[0].blit_surface_scaled.src_generation, 17u);
    EXPECT_EQ(dl.items[0].blit_surface_scaled.opacity, 77);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[0], 10.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[1], 10.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[2], 30.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[3], 30.0f);

    EXPECT_EQ(dl.items[1].op, DL_VIDEO_PLACEHOLDER);
    EXPECT_EQ(dl.items[1].video_placeholder.video_generation, 18u);
    EXPECT_EQ(dl.items[1].video_placeholder.object_fit, 5);

    EXPECT_EQ(dl.items[2].op, DL_WEBVIEW_LAYER_PLACEHOLDER);
    EXPECT_EQ(dl.items[2].webview_layer_placeholder.surface_generation, 19u);
}

TEST_F(DisplayListTest, EffectCommandsUseExpandedAndClippedBounds) {
    Bound clip = {3.0f, 4.0f, 15.0f, 16.0f};
    dl_apply_filter(&dl, 0.0f, 0.0f, 20.0f, 20.0f, &dl, &clip);
    dl_box_blur_inset(&dl, 10, 20, 30, 40, 5, 6.0f, 0xff000000);
    dl_outer_shadow(&dl, 100.0f, 200.0f, 20.0f, 10.0f,
                    1.0f, 2.0f, 3.0f, 4.0f, test_color(0xaa000000), 7.0f,
                    0, nullptr, 0, nullptr);

    ASSERT_EQ(dl.count, 3);
    EXPECT_EQ(dl.items[0].op, DL_APPLY_FILTER);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[0], 3.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[1], 4.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[2], 12.0f);
    EXPECT_FLOAT_EQ(dl.items[0].bounds[3], 12.0f);

    EXPECT_EQ(dl.items[1].op, DL_BOX_BLUR_INSET);
    EXPECT_FLOAT_EQ(dl.items[1].bounds[0], 5.0f);
    EXPECT_FLOAT_EQ(dl.items[1].bounds[1], 15.0f);
    EXPECT_FLOAT_EQ(dl.items[1].bounds[2], 40.0f);
    EXPECT_FLOAT_EQ(dl.items[1].bounds[3], 50.0f);

    EXPECT_EQ(dl.items[2].op, DL_OUTER_SHADOW);
    EXPECT_FLOAT_EQ(dl.items[2].bounds[0], 93.0f);
    EXPECT_FLOAT_EQ(dl.items[2].bounds[1], 193.0f);
    EXPECT_FLOAT_EQ(dl.items[2].bounds[2], 34.0f);
    EXPECT_FLOAT_EQ(dl.items[2].bounds[3], 24.0f);
}

TEST_F(DisplayListTest, ElementMarkerBoundsIncludeEffectOverflowForDirtyReplay) {
    int begin = dl_begin_element(&dl, 130, 50.0f, 50.0f, 20.0f, 20.0f);
    dl_outer_shadow(&dl, 50.0f, 50.0f, 20.0f, 20.0f,
                    4.0f, 5.0f, 6.0f, 3.0f,
                    test_color(0x99000000), 4.0f,
                    0, nullptr, 0, nullptr);
    dl_end_element(&dl, begin);

    ASSERT_EQ(dl.count, 3);
    const DisplayItem* marker = &dl.items[0];
    const DisplayItem* shadow = &dl.items[1];
    EXPECT_EQ(marker->op, DL_BEGIN_ELEMENT);
    EXPECT_EQ(shadow->op, DL_OUTER_SHADOW);

    Bound marker_bounds = dl_item_bounds(marker);
    Bound shadow_bounds = dl_item_bounds(shadow);
    EXPECT_LE(marker_bounds.left, shadow_bounds.left);
    EXPECT_LE(marker_bounds.top, shadow_bounds.top);
    EXPECT_GE(marker_bounds.right, shadow_bounds.right);
    EXPECT_GE(marker_bounds.bottom, shadow_bounds.bottom);
    EXPECT_LT(marker_bounds.left, 50.0f);
    EXPECT_LT(marker_bounds.top, 50.0f);
    EXPECT_GT(marker_bounds.right, 70.0f);
    EXPECT_GT(marker_bounds.bottom, 70.0f);
}

TEST_F(DisplayListTest, DirtyReplayClipIntersectsExpandedEffectBounds) {
    DisplayReplayDirtyClip dirty_clip = {};
    dirty_clip.active = true;
    dirty_clip.bounds = {20.0f, 25.0f, 60.0f, 65.0f};

    Bound effect_bounds = {10.0f, 12.0f, 80.0f, 90.0f};
    dl_replay_intersect_dirty_clip(&dirty_clip, &effect_bounds);

    EXPECT_FLOAT_EQ(effect_bounds.left, 20.0f);
    EXPECT_FLOAT_EQ(effect_bounds.top, 25.0f);
    EXPECT_FLOAT_EQ(effect_bounds.right, 60.0f);
    EXPECT_FLOAT_EQ(effect_bounds.bottom, 65.0f);

    dirty_clip.active = false;
    Bound unchanged = {1.0f, 2.0f, 3.0f, 4.0f};
    dl_replay_intersect_dirty_clip(&dirty_clip, &unchanged);
    EXPECT_FLOAT_EQ(unchanged.left, 1.0f);
    EXPECT_FLOAT_EQ(unchanged.top, 2.0f);
    EXPECT_FLOAT_EQ(unchanged.right, 3.0f);
    EXPECT_FLOAT_EQ(unchanged.bottom, 4.0f);
}

TEST_F(DisplayListTest, ClearRewindsScratchCopiesButKeepsCapacityForReuse) {
    char path_token = 0;
    RdtPath* path = (RdtPath*)&path_token;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 1.0f, 1.0f);
    float dashes[1] = {3.0f};
    dl_stroke_path(&dl, path, test_color(0xffffffff), 1.0f,
                   RDT_CAP_BUTT, RDT_JOIN_MITER, dashes, 1, 0.0f, nullptr);

    ASSERT_EQ(dl.count, 1);
    int capacity = dl.capacity;
    ASSERT_GT(capacity, 0);

    dl_clear(&dl);
    EXPECT_EQ(dl.count, 0);
    EXPECT_EQ(dl.capacity, capacity);

    dl_fill_rect(&dl, 1.0f, 2.0f, 3.0f, 4.0f, test_color(0xff000000));
    ASSERT_EQ(dl.count, 1);
    EXPECT_EQ(dl.capacity, capacity);
    EXPECT_EQ(dl.items[0].op, DL_FILL_RECT);
}
