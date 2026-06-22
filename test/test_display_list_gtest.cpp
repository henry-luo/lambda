#include <gtest/gtest.h>

#include "../radiant/display_list.h"
#include "../radiant/display_list_bounds.hpp"
#include "../radiant/display_list_replay_glyph.hpp"
#include "../radiant/display_list_replay_state.hpp"
#include "../radiant/display_list_storage.hpp"
#include "../radiant/paint_ir.h"
#include "../radiant/render_paint_boundary.hpp"
#include "../radiant/render_paint_block.hpp"
#include "../radiant/render_paint_gateway.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include <string.h>

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
                            stops, 2, RDT_FILL_EVEN_ODD, nullptr, nullptr);
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
                            stops, 2, RDT_FILL_WINDING, &transform, nullptr);
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

TEST_F(DisplayListTest, ReplayedGlyphCoverageKeepsTransparentEdges) {
    uint8_t glyph_pixels[1] = {64};
    GlyphBitmap bitmap = {};
    bitmap.buffer = glyph_pixels;
    bitmap.width = 1;
    bitmap.height = 1;
    bitmap.pitch = 1;
    bitmap.pixel_mode = GLYPH_PIXEL_GRAY;

    uint32_t pixel = 0;
    ImageSurface surface = {};
    surface.width = 1;
    surface.height = 1;
    surface.pitch = 4;
    surface.pixels = &pixel;

    DlDrawGlyph glyph = {};
    glyph.bitmap = bitmap;
    glyph.color = test_color(0xffffffff);
    glyph.clip = {0.0f, 0.0f, 1.0f, 1.0f};

    dl_replay_draw_glyph(&surface, &glyph);

    EXPECT_EQ(pixel & 0xFFu, 64u);
    EXPECT_EQ((pixel >> 8) & 0xFFu, 64u);
    EXPECT_EQ((pixel >> 16) & 0xFFu, 64u);
    EXPECT_EQ((pixel >> 24) & 0xFFu, 64u);
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

TEST_F(DisplayListTest, ValidateAcceptsBalancedReplayState) {
    char path_token = 0;
    RdtPath* path = (RdtPath*)&path_token;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 10.0f, 10.0f);

    dl_push_clip(&dl, path, nullptr);
    dl_pop_clip(&dl);
    dl_save_backdrop(&dl, 0, 0, 12, 12);
    dl_composite_opacity(&dl, 0, 0, 12, 12, 0.5f);
    dl_shadow_clip_save(&dl, 0, 0, 12, 12);
    dl_shadow_clip_restore(&dl, 0, nullptr, 0, 0, 12, 12, 1);
    int begin = dl_begin_element(&dl, 41, 1.0f, 2.0f, 3.0f, 4.0f);
    dl_fill_rect(&dl, 2.0f, 3.0f, 4.0f, 5.0f, test_color(0xff102030));
    dl_end_element(&dl, begin);

    DisplayListValidationResult result = {};
    EXPECT_TRUE(dl_validate(&dl, &result));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.first_error_index, -1);
}

TEST_F(DisplayListTest, ValidateRejectsUnbalancedReplayState) {
    dl_pop_clip(&dl);

    DisplayListValidationResult result = {};
    EXPECT_FALSE(dl_validate(&dl, &result));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.first_error_index, 0);
}

TEST_F(DisplayListTest, ValidateRejectsInvalidDrawablePayload) {
    DisplayItem* image = dl_alloc_item(&dl);
    ASSERT_NE(image, nullptr);
    image->op = DL_DRAW_IMAGE;
    image->draw_image.src_w = 8;
    image->draw_image.src_h = 8;
    image->draw_image.src_stride = 8;
    image->draw_image.dst_w = 8.0f;
    image->draw_image.dst_h = 8.0f;

    DisplayListValidationResult result = {};
    EXPECT_FALSE(dl_validate(&dl, &result));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.first_error_index, 0);
}

TEST_F(DisplayListTest, ValidateRejectsUnclosedElementMarker) {
    dl_begin_element(&dl, 99, 0.0f, 0.0f, 10.0f, 10.0f);

    DisplayListValidationResult result = {};
    EXPECT_FALSE(dl_validate(&dl, &result));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.first_error_index, 0);
}

// ─── PaintIR -> DisplayList parity ──────────────────────────────────────────
//
// Phase C gate (see vibe/radiant/Radiant_Design_Render_Paths.md): raster must
// emit AND re-consume the semantic paint IR at parity before any backend is
// rewired. Each test records a primitive both through the PaintBuilder (then
// paint_ir_lower_raster) and through the matching dl_* call directly, then
// asserts the two DisplayLists are item-by-item identical.

class PaintIrParityTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Arena* arena = nullptr;
    PaintList pl = {};
    DisplayList lowered = {};   // PaintBuilder -> paint_ir_lower_raster
    DisplayList direct = {};    // direct dl_* calls

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        paint_list_init(&pl, arena);
        dl_init(&lowered, arena);
        dl_init(&direct, arena);
    }

    void TearDown() override {
        dl_destroy(&lowered);
        dl_destroy(&direct);
        paint_list_destroy(&pl);
        if (arena) { arena_destroy(arena); arena = nullptr; }
        if (pool) { pool_destroy(pool); pool = nullptr; }
    }

    void lower() { paint_ir_lower_raster(&pl, &lowered); }
};

static void expect_matrix_eq(const RdtMatrix& a, const RdtMatrix& b) {
    EXPECT_FLOAT_EQ(a.e11, b.e11); EXPECT_FLOAT_EQ(a.e12, b.e12); EXPECT_FLOAT_EQ(a.e13, b.e13);
    EXPECT_FLOAT_EQ(a.e21, b.e21); EXPECT_FLOAT_EQ(a.e22, b.e22); EXPECT_FLOAT_EQ(a.e23, b.e23);
}

// Op-aware comparison: variable-length payloads (gradient stops, dash arrays)
// are copied into each DisplayList's own arena, so compare values not pointers.
static void expect_item_eq(const DisplayItem& a, const DisplayItem& b) {
    ASSERT_EQ(a.op, b.op);
    for (int i = 0; i < 4; i++) EXPECT_FLOAT_EQ(a.bounds[i], b.bounds[i]);
    switch (a.op) {
    case DL_FILL_RECT:
        EXPECT_EQ(memcmp(&a.fill_rect, &b.fill_rect, sizeof(DlFillRect)), 0);
        break;
    case DL_FILL_ROUNDED_RECT:
        EXPECT_EQ(memcmp(&a.fill_rounded_rect, &b.fill_rounded_rect, sizeof(DlFillRoundedRect)), 0);
        break;
    case DL_FILL_PATH:
        EXPECT_EQ(a.fill_path.path, b.fill_path.path);
        EXPECT_EQ(a.fill_path.color.c, b.fill_path.color.c);
        EXPECT_EQ(a.fill_path.rule, b.fill_path.rule);
        EXPECT_EQ(a.fill_path.has_transform, b.fill_path.has_transform);
        if (a.fill_path.has_transform) expect_matrix_eq(a.fill_path.transform, b.fill_path.transform);
        break;
    case DL_STROKE_PATH:
        EXPECT_EQ(a.stroke_path.path, b.stroke_path.path);
        EXPECT_EQ(a.stroke_path.color.c, b.stroke_path.color.c);
        EXPECT_FLOAT_EQ(a.stroke_path.width, b.stroke_path.width);
        EXPECT_EQ(a.stroke_path.cap, b.stroke_path.cap);
        EXPECT_EQ(a.stroke_path.join, b.stroke_path.join);
        EXPECT_EQ(a.stroke_path.dash_count, b.stroke_path.dash_count);
        EXPECT_FLOAT_EQ(a.stroke_path.dash_phase, b.stroke_path.dash_phase);
        for (int i = 0; i < a.stroke_path.dash_count; i++)
            EXPECT_FLOAT_EQ(a.stroke_path.dash_array[i], b.stroke_path.dash_array[i]);
        EXPECT_EQ(a.stroke_path.has_transform, b.stroke_path.has_transform);
        if (a.stroke_path.has_transform) expect_matrix_eq(a.stroke_path.transform, b.stroke_path.transform);
        break;
    case DL_FILL_LINEAR_GRADIENT: {
        const DlFillLinearGradient& x = a.fill_linear_gradient;
        const DlFillLinearGradient& y = b.fill_linear_gradient;
        EXPECT_EQ(x.path, y.path);
        EXPECT_FLOAT_EQ(x.x1, y.x1); EXPECT_FLOAT_EQ(x.y1, y.y1);
        EXPECT_FLOAT_EQ(x.x2, y.x2); EXPECT_FLOAT_EQ(x.y2, y.y2);
        EXPECT_EQ(x.rule, y.rule);
        ASSERT_EQ(x.stop_count, y.stop_count);
        for (int i = 0; i < x.stop_count; i++)
            EXPECT_EQ(memcmp(&x.stops[i], &y.stops[i], sizeof(RdtGradientStop)), 0);
        EXPECT_EQ(x.has_transform, y.has_transform);
        if (x.has_transform) expect_matrix_eq(x.transform, y.transform);
        break;
    }
    case DL_FILL_RADIAL_GRADIENT: {
        const DlFillRadialGradient& x = a.fill_radial_gradient;
        const DlFillRadialGradient& y = b.fill_radial_gradient;
        EXPECT_EQ(x.path, y.path);
        EXPECT_FLOAT_EQ(x.cx, y.cx); EXPECT_FLOAT_EQ(x.cy, y.cy); EXPECT_FLOAT_EQ(x.r, y.r);
        EXPECT_EQ(x.rule, y.rule);
        ASSERT_EQ(x.stop_count, y.stop_count);
        for (int i = 0; i < x.stop_count; i++)
            EXPECT_EQ(memcmp(&x.stops[i], &y.stops[i], sizeof(RdtGradientStop)), 0);
        EXPECT_EQ(x.has_transform, y.has_transform);
        if (x.has_transform) expect_matrix_eq(x.transform, y.transform);
        break;
    }
    case DL_DRAW_IMAGE: {
        const DlDrawImage& x = a.draw_image;
        const DlDrawImage& y = b.draw_image;
        EXPECT_EQ(x.pixels, y.pixels);
        EXPECT_EQ(x.src_w, y.src_w); EXPECT_EQ(x.src_h, y.src_h);
        EXPECT_EQ(x.src_stride, y.src_stride);
        EXPECT_FLOAT_EQ(x.dst_x, y.dst_x); EXPECT_FLOAT_EQ(x.dst_y, y.dst_y);
        EXPECT_FLOAT_EQ(x.dst_w, y.dst_w); EXPECT_FLOAT_EQ(x.dst_h, y.dst_h);
        EXPECT_EQ(x.opacity, y.opacity);
        EXPECT_EQ(x.resource_owner, y.resource_owner);
        EXPECT_EQ(x.resource_generation, y.resource_generation);
        EXPECT_EQ(x.has_transform, y.has_transform);
        if (x.has_transform) expect_matrix_eq(x.transform, y.transform);
        break;
    }
    case DL_DRAW_GLYPH: {
        const DlDrawGlyph& x = a.draw_glyph;
        const DlDrawGlyph& y = b.draw_glyph;
        EXPECT_EQ(x.bitmap.buffer, y.bitmap.buffer);
        EXPECT_EQ(x.bitmap.width, y.bitmap.width);
        EXPECT_EQ(x.bitmap.height, y.bitmap.height);
        EXPECT_EQ(x.bitmap.pitch, y.bitmap.pitch);
        EXPECT_EQ(x.bitmap.pixel_mode, y.bitmap.pixel_mode);
        EXPECT_FLOAT_EQ(x.bitmap.bitmap_scale, y.bitmap.bitmap_scale);
        EXPECT_EQ(x.resource_generation, y.resource_generation);
        EXPECT_EQ(x.x, y.x);
        EXPECT_EQ(x.y, y.y);
        EXPECT_EQ(x.color.c, y.color.c);
        EXPECT_EQ(x.is_color_emoji, y.is_color_emoji);
        EXPECT_EQ(memcmp(&x.clip, &y.clip, sizeof(Bound)), 0);
        EXPECT_EQ(x.has_transform, y.has_transform);
        if (x.has_transform) expect_matrix_eq(x.transform, y.transform);
        break;
    }
    case DL_DRAW_PICTURE:
        EXPECT_EQ(a.draw_picture.picture, b.draw_picture.picture);
        EXPECT_EQ(a.draw_picture.opacity, b.draw_picture.opacity);
        EXPECT_EQ(a.draw_picture.has_transform, b.draw_picture.has_transform);
        if (a.draw_picture.has_transform) expect_matrix_eq(a.draw_picture.transform, b.draw_picture.transform);
        break;
    case DL_VIDEO_PLACEHOLDER:
        EXPECT_EQ(memcmp(&a.video_placeholder, &b.video_placeholder, sizeof(DlVideoPlaceholder)), 0);
        break;
    case DL_WEBVIEW_LAYER_PLACEHOLDER:
        EXPECT_EQ(memcmp(&a.webview_layer_placeholder, &b.webview_layer_placeholder,
                         sizeof(DlWebviewLayerPlaceholder)), 0);
        break;
    case DL_PUSH_CLIP:
        EXPECT_EQ(a.push_clip.path, b.push_clip.path);
        EXPECT_EQ(a.push_clip.has_transform, b.push_clip.has_transform);
        if (a.push_clip.has_transform) expect_matrix_eq(a.push_clip.transform, b.push_clip.transform);
        break;
    case DL_POP_CLIP:
        break;
    case DL_SAVE_BACKDROP:
        EXPECT_EQ(memcmp(&a.save_backdrop, &b.save_backdrop, sizeof(DlSaveBackdrop)), 0);
        break;
    case DL_COMPOSITE_OPACITY:
        EXPECT_EQ(memcmp(&a.composite_opacity, &b.composite_opacity, sizeof(DlCompositeOpacity)), 0);
        break;
    case DL_APPLY_BLEND_MODE:
        EXPECT_EQ(memcmp(&a.apply_blend_mode, &b.apply_blend_mode, sizeof(DlApplyBlendMode)), 0);
        break;
    case DL_APPLY_FILTER:
        EXPECT_FLOAT_EQ(a.apply_filter.x, b.apply_filter.x);
        EXPECT_FLOAT_EQ(a.apply_filter.y, b.apply_filter.y);
        EXPECT_FLOAT_EQ(a.apply_filter.w, b.apply_filter.w);
        EXPECT_FLOAT_EQ(a.apply_filter.h, b.apply_filter.h);
        EXPECT_EQ(a.apply_filter.filter, b.apply_filter.filter);
        EXPECT_EQ(memcmp(&a.apply_filter.clip, &b.apply_filter.clip, sizeof(Bound)), 0);
        break;
    case DL_BOX_BLUR_REGION:
        EXPECT_EQ(memcmp(&a.box_blur_region, &b.box_blur_region, sizeof(DlBoxBlurRegion)), 0);
        break;
    case DL_BOX_BLUR_INSET:
        EXPECT_EQ(memcmp(&a.box_blur_inset, &b.box_blur_inset, sizeof(DlBoxBlurInset)), 0);
        break;
    case DL_SHADOW_CLIP_SAVE:
        EXPECT_EQ(memcmp(&a.shadow_clip_save, &b.shadow_clip_save, sizeof(DlShadowClipSave)), 0);
        break;
    case DL_SHADOW_CLIP_RESTORE:
        EXPECT_EQ(memcmp(&a.shadow_clip_restore, &b.shadow_clip_restore, sizeof(DlShadowClipRestore)), 0);
        break;
    case DL_OUTER_SHADOW:
        EXPECT_EQ(memcmp(&a.outer_shadow, &b.outer_shadow, sizeof(DlOuterShadow)), 0);
        break;
    case DL_FILL_SURFACE_RECT:
        EXPECT_EQ(memcmp(&a.fill_surface_rect, &b.fill_surface_rect,
                         sizeof(DlFillSurfaceRect)), 0);
        break;
    case DL_BLIT_SURFACE_SCALED:
        EXPECT_EQ(memcmp(&a.blit_surface_scaled, &b.blit_surface_scaled,
                         sizeof(DlBlitSurfaceScaled)), 0);
        break;
    default:
        ADD_FAILURE() << "unexpected op in parity comparison: " << a.op;
        break;
    }
}

static void expect_lists_equal(const DisplayList& a, const DisplayList& b) {
    ASSERT_EQ(a.count, b.count);
    for (int i = 0; i < a.count; i++) expect_item_eq(a.items[i], b.items[i]);
}

TEST_F(PaintIrParityTest, FillRectMatchesDirect) {
    paint_fill_rect(&pl, 1.0f, 2.0f, 30.0f, 40.0f, test_color(0xff112233));
    lower();
    dl_fill_rect(&direct, 1.0f, 2.0f, 30.0f, 40.0f, test_color(0xff112233));
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, FillRoundedRectMatchesDirect) {
    paint_fill_rounded_rect(&pl, 5.0f, 6.0f, 20.0f, 10.0f, 3.0f, 4.0f, test_color(0xffaabbcc));
    lower();
    dl_fill_rounded_rect(&direct, 5.0f, 6.0f, 20.0f, 10.0f, 3.0f, 4.0f, test_color(0xffaabbcc));
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, FillPathWithTransformMatchesDirect) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 10.0f, 10.0f);
    RdtMatrix m = rdt_matrix_identity();
    m.e13 = 7.0f; m.e23 = 9.0f;
    paint_fill_path(&pl, path, test_color(0xff445566), RDT_FILL_EVEN_ODD, &m);
    lower();
    dl_fill_path(&direct, path, test_color(0xff445566), RDT_FILL_EVEN_ODD, &m);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, StrokePathWithDashesMatchesDirect) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 10.0f, 10.0f);
    float dashes[3] = {2.0f, 4.0f, 1.5f};
    paint_stroke_path(&pl, path, test_color(0xffddeeff), 2.5f,
                      RDT_CAP_ROUND, RDT_JOIN_BEVEL, dashes, 3, 0.75f, nullptr);
    lower();
    dl_stroke_path(&direct, path, test_color(0xffddeeff), 2.5f,
                   RDT_CAP_ROUND, RDT_JOIN_BEVEL, dashes, 3, 0.75f, nullptr);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, LinearGradientMatchesDirect) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 10.0f, 10.0f);
    RdtGradientStop stops[2] = { {0.0f, 255, 0, 0, 255}, {1.0f, 0, 0, 255, 255} };
    paint_fill_linear_gradient(&pl, path, 0.0f, 0.0f, 10.0f, 10.0f, stops, 2,
                               RDT_FILL_WINDING, nullptr, nullptr);
    lower();
    dl_fill_linear_gradient(&direct, path, 0.0f, 0.0f, 10.0f, 10.0f, stops, 2,
                            RDT_FILL_WINDING, nullptr, nullptr);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, RadialGradientWithTransformMatchesDirect) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 10.0f, 10.0f);
    RdtGradientStop stops[3] = {
        {0.0f, 10, 20, 30, 255}, {0.5f, 40, 50, 60, 128}, {1.0f, 70, 80, 90, 0}
    };
    RdtMatrix m = rdt_matrix_identity();
    m.e11 = 2.0f; m.e22 = 2.0f; m.e13 = 5.0f;
    paint_fill_radial_gradient(&pl, path, 5.0f, 5.0f, 5.0f, stops, 3,
                               RDT_FILL_WINDING, &m, nullptr);
    lower();
    dl_fill_radial_gradient(&direct, path, 5.0f, 5.0f, 5.0f, stops, 3,
                            RDT_FILL_WINDING, &m, nullptr);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, DrawImageWithGenerationMatchesDirect) {
    static const uint32_t pixels[4] = {0, 0, 0, 0};
    ImageSurface owner = {};
    owner.generation = 42;
    RdtMatrix m = rdt_matrix_identity();
    m.e13 = 3.0f;
    paint_draw_image(&pl, pixels, 2, 2, 2, 1.0f, 1.0f, 8.0f, 8.0f, 200, &m, &owner);
    lower();
    dl_draw_image(&direct, pixels, 2, 2, 2, 1.0f, 1.0f, 8.0f, 8.0f, 200, &m,
                  &owner, owner.generation);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, DrawImageResourceMatchesDirect) {
    static const uint32_t pixels[4] = {0, 0, 0, 0};
    ImageSurface image = {};
    image.width = 2;
    image.height = 2;
    image.pitch = 8;
    image.pixels = (void*)pixels;
    image.generation = 91;
    RdtMatrix m = rdt_matrix_identity();
    m.e13 = 3.0f;
    m.e23 = 4.0f;

    paint_draw_image_resource(&pl, &image, 1.0f, 1.0f, 8.0f, 8.0f, 200, &m);
    lower();

    dl_draw_image(&direct, pixels, 2, 2, 2, 1.0f, 1.0f, 8.0f, 8.0f, 200, &m,
                  &image, image.generation);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, DrawGlyphWithGenerationMatchesDirect) {
    uint8_t glyph_pixels[16] = {};
    GlyphBitmap bitmap = {};
    bitmap.buffer = glyph_pixels;
    bitmap.width = 4;
    bitmap.height = 4;
    bitmap.pitch = 4;
    bitmap.pixel_mode = GLYPH_PIXEL_GRAY;
    bitmap.bitmap_scale = 1.25f;
    Bound clip = {1.0f, 2.0f, 12.0f, 14.0f};
    RdtMatrix m = rdt_matrix_identity();
    m.e13 = 2.0f;
    m.e23 = 3.0f;

    paint_draw_glyph(&pl, &bitmap, 5, 6, test_color(0xffabcdef),
                     false, &clip, &m, 99);
    lower();
    dl_draw_glyph(&direct, &bitmap, 5, 6, test_color(0xffabcdef),
                  false, &clip, &m, 99);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, ExternalLayerPlaceholdersMatchDirect) {
    ImageSurface surface = {};
    surface.generation = 123;
    Bound clip = {2.0f, 4.0f, 90.0f, 100.0f};
    char video_token = 0;

    paint_video_placeholder(&pl, &video_token, 10.0f, 11.0f, 50.0f, 60.0f,
                            0x105, &clip, 77);
    paint_webview_layer_placeholder(&pl, &surface,
                                    12.0f, 13.0f, 70.0f, 80.0f,
                                    &clip, surface.generation);
    paint_video_placeholder(&pl, &video_token, 1.0f, 2.0f, 3.0f, 4.0f,
                            6, nullptr, 0);
    lower();

    dl_video_placeholder(&direct, &video_token, 10.0f, 11.0f, 50.0f, 60.0f,
                         0x105, &clip, 77);
    dl_webview_layer_placeholder(&direct, &surface,
                                 12.0f, 13.0f, 70.0f, 80.0f,
                                 &clip, surface.generation);
    dl_video_placeholder(&direct, &video_token, 1.0f, 2.0f, 3.0f, 4.0f,
                         6, nullptr, 0);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, ClipPushPopMatchesDirect) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 10.0f, 10.0f);
    RdtMatrix m = rdt_matrix_identity();
    paint_push_clip(&pl, path, &m);
    paint_pop_clip(&pl);
    lower();
    dl_push_clip(&direct, path, &m);
    dl_pop_clip(&direct);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, RasterEffectOpsMatchDirect) {
    char filter_token = 0;
    Bound clip = {1, 2, 30, 40};

    paint_save_backdrop(&pl, 2, 3, 20, 30);
    paint_composite_opacity(&pl, 2, 3, 20, 30, 0.5f, true);
    paint_save_backdrop(&pl, 2, 3, 20, 30);
    paint_apply_blend_mode(&pl, 2, 3, 20, 30, 7);
    paint_apply_filter(&pl, 4.0f, 5.0f, 40.0f, 50.0f, &filter_token, &clip);
    paint_apply_filter(&pl, 6.0f, 7.0f, 60.0f, 70.0f, &filter_token, nullptr);
    lower();

    dl_save_backdrop(&direct, 2, 3, 20, 30);
    dl_composite_opacity(&direct, 2, 3, 20, 30, 0.5f, true);
    dl_save_backdrop(&direct, 2, 3, 20, 30);
    dl_apply_blend_mode(&direct, 2, 3, 20, 30, 7);
    dl_apply_filter(&direct, 4.0f, 5.0f, 40.0f, 50.0f, &filter_token, &clip);
    dl_apply_filter(&direct, 6.0f, 7.0f, 60.0f, 70.0f, &filter_token, nullptr);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, RasterEffectFragmentsMatchDirect) {
    paint_save_backdrop(&pl, 2, 3, 20, 30);
    paint_ir_lower_raster_fragment(&pl, &lowered);
    paint_list_clear(&pl);
    paint_composite_opacity(&pl, 2, 3, 20, 30, 0.5f, true);
    paint_ir_lower_raster_fragment(&pl, &lowered);

    dl_save_backdrop(&direct, 2, 3, 20, 30);
    dl_composite_opacity(&direct, 2, 3, 20, 30, 0.5f, true);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, RasterShadowOpsMatchDirect) {
    float clip_params[8] = {1.0f, 2.0f, 30.0f, 40.0f, 4.0f, 5.0f, 6.0f, 7.0f};
    float exclude_params[8] = {8.0f, 9.0f, 50.0f, 60.0f, 10.0f, 11.0f, 12.0f, 13.0f};
    Color tint = test_color(0x80203040);

    paint_box_blur_region(&pl, 1, 2, 30, 40, 3.5f,
                          2, clip_params, 3, exclude_params,
                          true, true, tint);
    paint_box_blur_inset(&pl, 5, 6, 70, 80, 9, 4.5f, 0xffaabbcc);
    paint_shadow_clip_save(&pl, 10, 11, 90, 91);
    paint_shadow_clip_restore(&pl, 3, exclude_params, 10, 11, 90, 91, 1);
    paint_outer_shadow(&pl, 12.0f, 13.0f, 100.0f, 110.0f,
                       2.0f, 3.0f, 4.0f, 5.0f,
                       test_color(0x70445566), 6.5f,
                       3, exclude_params, 2, clip_params);
    lower();

    dl_box_blur_region(&direct, 1, 2, 30, 40, 3.5f,
                       2, clip_params, 3, exclude_params,
                       true, true, tint);
    dl_box_blur_inset(&direct, 5, 6, 70, 80, 9, 4.5f, 0xffaabbcc);
    dl_shadow_clip_save(&direct, 10, 11, 90, 91);
    dl_shadow_clip_restore(&direct, 3, exclude_params, 10, 11, 90, 91, 1);
    dl_outer_shadow(&direct, 12.0f, 13.0f, 100.0f, 110.0f,
                    2.0f, 3.0f, 4.0f, 5.0f,
                    test_color(0x70445566), 6.5f,
                    3, exclude_params, 2, clip_params);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, RasterSurfaceOpsMatchDirect) {
    ImageSurface src = {};
    src.generation = 321;
    Bound clip = {1.0f, 2.0f, 40.0f, 50.0f};

    paint_fill_surface_rect(&pl, 3.0f, 4.0f, 30.0f, 31.0f,
                            0xff102030, &clip, nullptr, 0);
    paint_blit_surface_scaled(&pl, &src, 5.0f, 6.0f, 70.0f, 80.0f,
                              SCALE_MODE_LINEAR, &clip, nullptr, 0,
                              210, src.generation);
    lower();

    dl_fill_surface_rect(&direct, 3.0f, 4.0f, 30.0f, 31.0f,
                         0xff102030, &clip, nullptr, 0);
    dl_blit_surface_scaled(&direct, &src, 5.0f, 6.0f, 70.0f, 80.0f,
                           SCALE_MODE_LINEAR, &clip, nullptr, 0,
                           210, src.generation);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, SimpleBoundaryHelperEmitsBackgroundAndSolidBorder) {
    ViewBlock view = {};
    BoundaryProp bound = {};
    BackgroundProp bg = {};
    BorderProp border = {};

    view.bound = &bound;
    view.width = 100.0f;
    view.height = 50.0f;
    bound.background = &bg;
    bound.border = &border;
    bg.color = test_color(0xff102030);
    border.width.top = 1.0f;
    border.width.right = 2.0f;
    border.width.bottom = 3.0f;
    border.width.left = 4.0f;
    border.top_style = CSS_VALUE_SOLID;
    border.right_style = CSS_VALUE_SOLID;
    border.bottom_style = CSS_VALUE_SOLID;
    border.left_style = CSS_VALUE_SOLID;
    border.top_color = test_color(0xff405060);
    border.right_color = border.top_color;
    border.bottom_color = border.top_color;
    border.left_color = border.top_color;

    ASSERT_TRUE(render_paint_boundary_emit_simple(&pl, &view, 10.0f, 20.0f));
    lower();

    dl_fill_rect(&direct, 10.0f, 20.0f, 100.0f, 50.0f, bg.color);
    dl_fill_rect(&direct, 10.0f, 20.0f, 100.0f, 1.0f, border.top_color);
    dl_fill_rect(&direct, 108.0f, 20.0f, 2.0f, 50.0f, border.right_color);
    dl_fill_rect(&direct, 10.0f, 67.0f, 100.0f, 3.0f, border.bottom_color);
    dl_fill_rect(&direct, 10.0f, 20.0f, 4.0f, 50.0f, border.left_color);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, SimpleBoundaryHelperEmitsUniformRoundedBackground) {
    ViewBlock view = {};
    BoundaryProp bound = {};
    BackgroundProp bg = {};
    BorderProp border = {};

    view.bound = &bound;
    view.width = 100.0f;
    view.height = 50.0f;
    bound.background = &bg;
    bound.border = &border;
    bg.color = test_color(0xff203040);
    border.radius.top_left = 6.0f;
    border.radius.top_right = 6.0f;
    border.radius.bottom_right = 6.0f;
    border.radius.bottom_left = 6.0f;
    border.radius.top_left_y = 6.0f;
    border.radius.top_right_y = 6.0f;
    border.radius.bottom_right_y = 6.0f;
    border.radius.bottom_left_y = 6.0f;

    ASSERT_TRUE(render_paint_boundary_emit_simple(&pl, &view, 10.0f, 20.0f));
    lower();

    dl_fill_rounded_rect(&direct, 10.0f, 20.0f, 100.0f, 50.0f,
                         6.0f, 6.0f, bg.color);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, SimpleBoundaryHelperEmitsOpaqueUniformRoundedBorder) {
    ViewBlock view = {};
    BoundaryProp bound = {};
    BackgroundProp bg = {};
    BorderProp border = {};

    view.bound = &bound;
    view.width = 100.0f;
    view.height = 50.0f;
    bound.background = &bg;
    bound.border = &border;
    bg.color = test_color(0xff203040);
    border.width.top = 4.0f;
    border.width.right = 4.0f;
    border.width.bottom = 4.0f;
    border.width.left = 4.0f;
    border.top_style = CSS_VALUE_SOLID;
    border.right_style = CSS_VALUE_SOLID;
    border.bottom_style = CSS_VALUE_SOLID;
    border.left_style = CSS_VALUE_SOLID;
    border.top_color = test_color(0xff506070);
    border.right_color = border.top_color;
    border.bottom_color = border.top_color;
    border.left_color = border.top_color;
    border.radius.top_left = 8.0f;
    border.radius.top_right = 8.0f;
    border.radius.bottom_right = 8.0f;
    border.radius.bottom_left = 8.0f;
    border.radius.top_left_y = 8.0f;
    border.radius.top_right_y = 8.0f;
    border.radius.bottom_right_y = 8.0f;
    border.radius.bottom_left_y = 8.0f;

    ASSERT_TRUE(render_paint_boundary_emit_simple(&pl, &view, 10.0f, 20.0f));
    lower();

    dl_fill_rounded_rect(&direct, 10.0f, 20.0f, 100.0f, 50.0f,
                         8.0f, 8.0f, border.top_color);
    dl_fill_rounded_rect(&direct, 14.0f, 24.0f, 92.0f, 42.0f,
                         4.0f, 4.0f, bg.color);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, SimpleBoundaryHelperRejectsFallbackCases) {
    ViewBlock view = {};
    BoundaryProp bound = {};
    BackgroundProp bg = {};
    BorderProp border = {};

    view.bound = &bound;
    view.width = 100.0f;
    view.height = 50.0f;
    bound.background = &bg;
    bound.border = &border;

    bg.gradient_type = GRADIENT_LINEAR;
    EXPECT_FALSE(render_paint_boundary_emit_simple(&pl, &view, 0.0f, 0.0f));

    bg.gradient_type = GRADIENT_NONE;
    border.width.top = 1.0f;
    border.top_style = CSS_VALUE_DASHED;
    border.top_color = test_color(0xff000000);
    EXPECT_FALSE(render_paint_boundary_emit_simple(&pl, &view, 0.0f, 0.0f));

    border.top_style = CSS_VALUE_SOLID;
    border.radius.top_left = 2.0f;
    EXPECT_FALSE(render_paint_boundary_emit_simple(&pl, &view, 0.0f, 0.0f));

    border.width.top = 0.0f;
    border.top_color = {};
    border.radius.top_right = 2.0f;
    EXPECT_FALSE(render_paint_boundary_emit_simple(&pl, &view, 0.0f, 0.0f));

    border.radius = {};
    border.width.top = 2.0f;
    border.width.right = 2.0f;
    border.width.bottom = 2.0f;
    border.width.left = 2.0f;
    border.top_style = CSS_VALUE_SOLID;
    border.right_style = CSS_VALUE_SOLID;
    border.bottom_style = CSS_VALUE_SOLID;
    border.left_style = CSS_VALUE_SOLID;
    border.top_color = test_color(0x80000000);
    border.right_color = border.top_color;
    border.bottom_color = border.top_color;
    border.left_color = border.top_color;
    border.radius.top_left = 4.0f;
    border.radius.top_right = 4.0f;
    border.radius.bottom_right = 4.0f;
    border.radius.bottom_left = 4.0f;
    border.radius.top_left_y = 4.0f;
    border.radius.top_right_y = 4.0f;
    border.radius.bottom_right_y = 4.0f;
    border.radius.bottom_left_y = 4.0f;
    EXPECT_FALSE(render_paint_boundary_emit_simple(&pl, &view, 0.0f, 0.0f));
}

TEST_F(PaintIrParityTest, BoundaryHelperBuildsLinearGradientPaint) {
    ViewBlock view = {};
    BoundaryProp bound = {};
    BackgroundProp bg = {};
    LinearGradient gradient = {};
    GradientStop css_stops[2] = {};
    RdtGradientStop stops[2] = {};
    BoundaryLinearGradientPaint paint = {};

    view.bound = &bound;
    view.width = 100.0f;
    view.height = 50.0f;
    bound.background = &bg;
    bg.gradient_type = GRADIENT_LINEAR;
    bg.linear_gradient = &gradient;
    gradient.angle = 90.0f;
    gradient.stop_count = 2;
    gradient.stops = css_stops;
    css_stops[0].position = 0.0f;
    css_stops[0].color.r = 0x10;
    css_stops[0].color.g = 0x20;
    css_stops[0].color.b = 0x30;
    css_stops[0].color.a = 0xff;
    css_stops[1].position = 1.0f;
    css_stops[1].color.r = 0x40;
    css_stops[1].color.g = 0x50;
    css_stops[1].color.b = 0x60;
    css_stops[1].color.a = 0xff;

    ASSERT_TRUE(render_paint_boundary_build_linear_gradient(&view, 10.0f, 20.0f,
                                                            stops, 2, &paint));
    ASSERT_NE(paint.path, nullptr);
    EXPECT_FLOAT_EQ(paint.x1, 10.0f);
    EXPECT_FLOAT_EQ(paint.y1, 45.0f);
    EXPECT_FLOAT_EQ(paint.x2, 110.0f);
    EXPECT_FLOAT_EQ(paint.y2, 45.0f);
    ASSERT_EQ(paint.stop_count, 2);
    EXPECT_FLOAT_EQ(paint.stops[0].offset, 0.0f);
    EXPECT_FLOAT_EQ(paint.stops[1].offset, 1.0f);
    EXPECT_EQ(paint.stops[0].r, 0x10);
    EXPECT_EQ(paint.stops[1].r, 0x40);
    rdt_path_free(paint.path);
}

TEST_F(PaintIrParityTest, BoundaryHelperBuildsRadialGradientPaint) {
    ViewBlock view = {};
    BoundaryProp bound = {};
    BackgroundProp bg = {};
    RadialGradient gradient = {};
    GradientStop css_stops[2] = {};
    RdtGradientStop stops[2] = {};
    BoundaryRadialGradientPaint paint = {};

    view.bound = &bound;
    view.width = 120.0f;
    view.height = 80.0f;
    bound.background = &bg;
    bg.gradient_type = GRADIENT_RADIAL;
    bg.radial_gradient = &gradient;
    gradient.cx_set = true;
    gradient.cy_set = true;
    gradient.cx = 0.25f;
    gradient.cy = 0.75f;
    gradient.stop_count = 2;
    gradient.stops = css_stops;
    css_stops[0].position = -1.0f;
    css_stops[0].color.r = 0x01;
    css_stops[0].color.g = 0x02;
    css_stops[0].color.b = 0x03;
    css_stops[0].color.a = 0xff;
    css_stops[1].position = -1.0f;
    css_stops[1].color.r = 0x04;
    css_stops[1].color.g = 0x05;
    css_stops[1].color.b = 0x06;
    css_stops[1].color.a = 0xff;

    ASSERT_TRUE(render_paint_boundary_build_radial_gradient(&view, 5.0f, 7.0f,
                                                            stops, 2, &paint));
    ASSERT_NE(paint.path, nullptr);
    EXPECT_FLOAT_EQ(paint.cx, 35.0f);
    EXPECT_FLOAT_EQ(paint.cy, 67.0f);
    EXPECT_FLOAT_EQ(paint.r, 40.0f);
    ASSERT_EQ(paint.stop_count, 2);
    EXPECT_FLOAT_EQ(paint.stops[0].offset, 0.0f);
    EXPECT_FLOAT_EQ(paint.stops[1].offset, 1.0f);
    EXPECT_EQ(paint.stops[0].b, 0x03);
    EXPECT_EQ(paint.stops[1].b, 0x06);
    rdt_path_free(paint.path);
}

TEST_F(PaintIrParityTest, MixedSequenceMatchesDirect) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;
    test_display_list_stub_set_path_bounds(path, true, 0.0f, 0.0f, 10.0f, 10.0f);
    RdtMatrix m = rdt_matrix_identity();

    paint_push_clip(&pl, path, &m);
    paint_fill_rect(&pl, 0.0f, 0.0f, 10.0f, 10.0f, test_color(0xff010203));
    paint_fill_rounded_rect(&pl, 1.0f, 1.0f, 8.0f, 8.0f, 2.0f, 2.0f, test_color(0xff040506));
    paint_fill_path(&pl, path, test_color(0xff070809), RDT_FILL_WINDING, nullptr);
    paint_pop_clip(&pl);
    lower();

    dl_push_clip(&direct, path, &m);
    dl_fill_rect(&direct, 0.0f, 0.0f, 10.0f, 10.0f, test_color(0xff010203));
    dl_fill_rounded_rect(&direct, 1.0f, 1.0f, 8.0f, 8.0f, 2.0f, 2.0f, test_color(0xff040506));
    dl_fill_path(&direct, path, test_color(0xff070809), RDT_FILL_WINDING, nullptr);
    dl_pop_clip(&direct);

    EXPECT_EQ(paint_list_count(&pl), 5);
    expect_lists_equal(lowered, direct);
}

TEST_F(PaintIrParityTest, GatewayRequiresPaintIrAndDisplayListTargets) {
    PaintRecordTarget missing_paint = {nullptr, &lowered, "TEST_GATEWAY"};
    paint_record_fill_rect(&missing_paint, "gateway_missing_paint",
                           1.0f, 2.0f, 3.0f, 4.0f,
                           test_color(0xff010203));
    EXPECT_EQ(lowered.count, 0);

    PaintRecordTarget missing_dl = {&pl, nullptr, "TEST_GATEWAY"};
    paint_record_fill_rect(&missing_dl, "gateway_missing_dl",
                           1.0f, 2.0f, 3.0f, 4.0f,
                           test_color(0xff010203));
    EXPECT_EQ(paint_list_count(&pl), 0);

    PaintRecordTarget ready = {&pl, &lowered, "TEST_GATEWAY"};
    paint_record_fill_rect(&ready, "gateway_ready",
                           1.0f, 2.0f, 3.0f, 4.0f,
                           test_color(0xff010203));
    dl_fill_rect(&direct, 1.0f, 2.0f, 3.0f, 4.0f,
                 test_color(0xff010203));

    EXPECT_EQ(paint_list_count(&pl), 0);
    expect_lists_equal(lowered, direct);
}

typedef struct PaintBlockDriverProbe {
    int begin_count;
    int self_count;
    int children_count;
    int finish_count;
    bool continue_children;
} PaintBlockDriverProbe;

static bool probe_block_begin(void* ctx, ViewBlock* block, void** phase) {
    (void)block;
    PaintBlockDriverProbe* probe = (PaintBlockDriverProbe*)ctx;
    probe->begin_count++;
    *phase = probe;
    return true;
}

static bool probe_block_self(void* ctx, ViewBlock* block, void* phase) {
    (void)block;
    (void)phase;
    PaintBlockDriverProbe* probe = (PaintBlockDriverProbe*)ctx;
    probe->self_count++;
    return probe->continue_children;
}

static double probe_block_children(void* ctx, ViewBlock* block, void* phase) {
    (void)block;
    (void)phase;
    PaintBlockDriverProbe* probe = (PaintBlockDriverProbe*)ctx;
    probe->children_count++;
    return 7.0;
}

static void probe_block_finish(void* ctx, ViewBlock* block, void* phase) {
    (void)block;
    (void)phase;
    PaintBlockDriverProbe* probe = (PaintBlockDriverProbe*)ctx;
    probe->finish_count++;
}

TEST_F(PaintIrParityTest, SharedBlockPaintDriverSkipsChildrenButFinishes) {
    PaintBlockDriverProbe probe = {};
    probe.continue_children = false;
    RenderPaintBlockOps ops = {};
    ops.ctx = &probe;
    ops.begin = probe_block_begin;
    ops.paint_self = probe_block_self;
    ops.paint_children = probe_block_children;
    ops.finish = probe_block_finish;
    ViewBlock block = {};

    RenderPaintBlockResult result = render_paint_block_run(&ops, &block);

    EXPECT_TRUE(result.painted);
    EXPECT_EQ(result.children_time, 0.0);
    EXPECT_EQ(probe.begin_count, 1);
    EXPECT_EQ(probe.self_count, 1);
    EXPECT_EQ(probe.children_count, 0);
    EXPECT_EQ(probe.finish_count, 1);
}

TEST_F(PaintIrParityTest, SharedBlockPaintDriverRecordsChildrenTime) {
    PaintBlockDriverProbe probe = {};
    probe.continue_children = true;
    RenderPaintBlockOps ops = {};
    ops.ctx = &probe;
    ops.begin = probe_block_begin;
    ops.paint_self = probe_block_self;
    ops.paint_children = probe_block_children;
    ops.finish = probe_block_finish;
    ViewBlock block = {};

    RenderPaintBlockResult result = render_paint_block_run(&ops, &block);

    EXPECT_TRUE(result.painted);
    EXPECT_EQ(result.children_time, 7.0);
    EXPECT_EQ(probe.begin_count, 1);
    EXPECT_EQ(probe.self_count, 1);
    EXPECT_EQ(probe.children_count, 1);
    EXPECT_EQ(probe.finish_count, 1);
}

TEST_F(PaintIrParityTest, ClearRewindsCountForReuse) {
    paint_fill_rect(&pl, 0.0f, 0.0f, 1.0f, 1.0f, test_color(0xff000000));
    EXPECT_EQ(paint_list_count(&pl), 1);
    int cap = pl.capacity;
    paint_list_clear(&pl);
    EXPECT_EQ(paint_list_count(&pl), 0);
    EXPECT_EQ(pl.capacity, cap);
    paint_fill_rect(&pl, 0.0f, 0.0f, 2.0f, 2.0f, test_color(0xffffffff));
    EXPECT_EQ(paint_list_count(&pl), 1);
}

TEST_F(PaintIrParityTest, ValidateAcceptsBalancedReplayState) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;

    paint_push_clip(&pl, path, nullptr);
    paint_fill_rect(&pl, 0.0f, 0.0f, 10.0f, 10.0f, test_color(0xff000000));
    paint_pop_clip(&pl);
    paint_save_backdrop(&pl, 0, 0, 10, 10);
    paint_composite_opacity(&pl, 0, 0, 10, 10, 0.5f, false);
    paint_shadow_clip_save(&pl, 1, 2, 30, 40);
    paint_shadow_clip_restore(&pl, 0, nullptr, 1, 2, 30, 40, 1);

    PaintIrValidationResult result = {};
    EXPECT_TRUE(paint_ir_validate(&pl, &result));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.first_error_index, -1);
}

TEST_F(PaintIrParityTest, ValidateRejectsUnbalancedClipStack) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;

    paint_push_clip(&pl, path, nullptr);

    PaintIrValidationResult result = {};
    EXPECT_FALSE(paint_ir_validate(&pl, &result));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.first_error_index, paint_list_count(&pl));
    EXPECT_EQ(result.clip_depth, 1);
}

TEST_F(PaintIrParityTest, ValidateRejectsInvalidPayload) {
    paint_fill_rect(&pl, 0.0f, 0.0f, -1.0f, 10.0f, test_color(0xff000000));

    PaintIrValidationResult result = {};
    EXPECT_FALSE(paint_ir_validate(&pl, &result));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.first_error_index, 0);
}

TEST_F(PaintIrParityTest, ValidateRejectsCompositeWithoutBackdrop) {
    paint_composite_opacity(&pl, 0, 0, 10, 10, 0.5f, false);

    PaintIrValidationResult result = {};
    EXPECT_FALSE(paint_ir_validate(&pl, &result));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.first_error_index, 0);
}

TEST_F(PaintIrParityTest, SemanticBuildersValidateAndLowerEffectGroupRasterOps) {
    PaintEffectGroup group = {};
    group.bounds = {1.0f, 2.0f, 30.0f, 40.0f};
    group.opacity = 0.75f;
    group.blend_mode = 1;

    uint32_t glyph_ids[2] = {11, 12};
    float xs[2] = {3.0f, 8.0f};
    float ys[2] = {5.0f, 5.0f};
    PaintGlyphRun glyph_run = {};
    glyph_run.font = &group;
    glyph_run.color = test_color(0xff102030);
    glyph_run.glyph_ids = glyph_ids;
    glyph_run.xs = xs;
    glyph_run.ys = ys;
    glyph_run.count = 2;

    paint_begin_effect_group(&pl, &group);
    paint_glyph_run(&pl, &glyph_run);
    paint_end_effect_group(&pl);

    PaintIrValidationResult result = {};
    EXPECT_TRUE(paint_ir_validate(&pl, &result));
    EXPECT_EQ(paint_list_count(&pl), 3);

    lower();
    ASSERT_EQ(lowered.count, 4);
    EXPECT_EQ(lowered.items[0].op, DL_SAVE_BACKDROP);
    EXPECT_EQ(lowered.items[1].op, DL_SAVE_BACKDROP);
    EXPECT_EQ(lowered.items[2].op, DL_COMPOSITE_OPACITY);
    EXPECT_EQ(lowered.items[3].op, DL_APPLY_BLEND_MODE);
    EXPECT_EQ(lowered.items[0].save_backdrop.x0, 1);
    EXPECT_EQ(lowered.items[0].save_backdrop.y0, 2);
    EXPECT_EQ(lowered.items[0].save_backdrop.w, 29);
    EXPECT_EQ(lowered.items[0].save_backdrop.h, 38);
    EXPECT_FLOAT_EQ(lowered.items[2].composite_opacity.opacity, 0.75f);
    EXPECT_EQ(lowered.items[3].apply_blend_mode.blend_mode, 1);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);
    PaintSvgLoweringOptions options = {};
    options.emit_unsupported_comments = true;
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, &options, &stats);

    EXPECT_EQ(stats.command_count, 3);
    EXPECT_EQ(stats.emitted_count, 2);
    EXPECT_EQ(stats.fallback_count, 1);
    EXPECT_EQ(stats.unsupported_count, 1);
    EXPECT_NE(strstr(out->str, "data-radiant-fallback=\"effect\""), nullptr);
    EXPECT_NE(strstr(out->str, "<!-- unsupported PAINT_GLYPH_RUN -->"), nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringFallbackEffectGroupPreservesSupportedChildren) {
    PaintEffectGroup group = {};
    group.bounds = {0.0f, 0.0f, 20.0f, 20.0f};
    group.opacity = 0.5f;
    group.blend_mode = 1;

    Color fill_color = {};
    fill_color.r = 12;
    fill_color.g = 34;
    fill_color.b = 56;
    fill_color.a = 255;

    paint_begin_effect_group(&pl, &group);
    paint_fill_rect(&pl, 2.0f, 3.0f, 4.0f, 5.0f, fill_color);
    paint_end_effect_group(&pl);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, nullptr, &stats);

    EXPECT_EQ(stats.command_count, 3);
    EXPECT_EQ(stats.emitted_count, 3);
    EXPECT_EQ(stats.fallback_count, 1);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str, "<g data-radiant-fallback=\"effect\" opacity=\"0.5000\">\n"), nullptr);
    EXPECT_NE(strstr(out->str,
        "<rect x=\"2.00\" y=\"3.00\" width=\"4.00\" height=\"5.00\" fill=\"rgb(12,34,56)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "</g>"), nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, ValidateRejectsInvalidSemanticPayload) {
    PaintEffectGroup group = {};
    group.bounds = {0.0f, 0.0f, 10.0f, 10.0f};
    group.opacity = 1.5f;
    paint_begin_effect_group(&pl, &group);
    paint_end_effect_group(&pl);

    PaintIrValidationResult result = {};
    EXPECT_FALSE(paint_ir_validate(&pl, &result));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.first_error_index, 0);
}

TEST_F(PaintIrParityTest, SemanticEffectGroupLowersFilterAndBackdrop) {
    char filter_token = 0;
    PaintEffectGroup group = {};
    group.bounds = {4.0f, 5.0f, 24.0f, 35.0f};
    group.opacity = 1.0f;
    group.filter = &filter_token;
    group.backdrop = true;
    group.has_clip = true;

    paint_begin_effect_group(&pl, &group);
    paint_fill_rect(&pl, 6.0f, 7.0f, 8.0f, 9.0f, test_color(0xff102030));
    paint_end_effect_group(&pl);

    lower();
    ASSERT_EQ(lowered.count, 4);
    EXPECT_EQ(lowered.items[0].op, DL_SAVE_BACKDROP);
    EXPECT_EQ(lowered.items[1].op, DL_FILL_RECT);
    EXPECT_EQ(lowered.items[2].op, DL_APPLY_FILTER);
    EXPECT_EQ(lowered.items[3].op, DL_COMPOSITE_OPACITY);
    EXPECT_EQ(lowered.items[2].apply_filter.filter, &filter_token);
    EXPECT_FLOAT_EQ(lowered.items[2].apply_filter.x, 4.0f);
    EXPECT_FLOAT_EQ(lowered.items[2].apply_filter.y, 5.0f);
    EXPECT_FLOAT_EQ(lowered.items[2].apply_filter.w, 20.0f);
    EXPECT_FLOAT_EQ(lowered.items[2].apply_filter.h, 30.0f);
    EXPECT_FLOAT_EQ(lowered.items[3].composite_opacity.opacity, 1.0f);
}

TEST_F(PaintIrParityTest, ValidateRejectsUnbalancedTransformStack) {
    RdtMatrix transform = rdt_matrix_translate(3.0f, 4.0f);
    paint_push_transform(&pl, &transform);

    PaintIrValidationResult result = {};
    EXPECT_FALSE(paint_ir_validate(&pl, &result));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.first_error_index, paint_list_count(&pl));
}

TEST_F(PaintIrParityTest, RasterLoweringRejectsInvalidPaintIr) {
    paint_pop_clip(&pl);

    lower();

    EXPECT_EQ(lowered.count, 0);
}

TEST_F(PaintIrParityTest, SvgLoweringRejectsInvalidPaintIr) {
    paint_pop_clip(&pl);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringOptions options = {};
    options.emit_unsupported_comments = true;
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, &options, &stats);

    EXPECT_EQ(stats.command_count, 0);
    EXPECT_EQ(stats.emitted_count, 0);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_STREQ(out->str, "");

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsRectPrimitives) {
    Color solid = {};
    solid.r = 1;
    solid.g = 2;
    solid.b = 3;
    solid.a = 255;

    Color translucent = {};
    translucent.r = 4;
    translucent.g = 5;
    translucent.b = 6;
    translucent.a = 128;

    paint_fill_rect(&pl, 1.25f, 2.5f, 30.0f, 40.75f, solid);
    paint_fill_rounded_rect(&pl, 5.0f, 6.0f, 70.0f, 80.0f, 3.5f, 4.5f, translucent);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringOptions options = {};
    options.indent_level = 1;
    options.emit_unsupported_comments = true;
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, &options, &stats);

    EXPECT_EQ(stats.command_count, 2);
    EXPECT_EQ(stats.emitted_count, 2);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "  <rect x=\"1.25\" y=\"2.50\" width=\"30.00\" height=\"40.75\" fill=\"rgb(1,2,3)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "  <rect x=\"5.00\" y=\"6.00\" width=\"70.00\" height=\"80.00\" rx=\"3.50\" ry=\"4.50\" fill=\"rgba(4,5,6,0.502)\" />"),
        nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsImageResource) {
    ImageSurface image = {};
    Url url = {};
    url.href = create_string(pool, "assets/a&b.png");
    image.url = &url;

    RdtMatrix transform = rdt_matrix_identity();
    transform.e13 = 3.0f;
    transform.e23 = 4.0f;
    paint_draw_image_resource(&pl, &image, 10.0f, 11.0f, 50.0f, 60.0f,
                              128, &transform);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, nullptr, &stats);

    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<image x=\"10.00\" y=\"11.00\" width=\"50.00\" height=\"60.00\" href=\"assets/a&amp;b.png\" preserveAspectRatio=\"none\" opacity=\"0.5020\" transform=\"matrix(1 0 0 1 3 4)\" />"),
        nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringCountsUnsupportedOps) {
    char tok = 0;
    RdtPath* path = (RdtPath*)&tok;
    paint_fill_path(&pl, path, test_color(0xff010203), RDT_FILL_WINDING, nullptr);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringOptions options = {};
    options.emit_unsupported_comments = true;
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, &options, &stats);

    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 0);
    EXPECT_EQ(stats.unsupported_count, 1);
    EXPECT_NE(strstr(out->str, "<!-- unsupported PAINT_FILL_PATH -->"), nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsInspectableFillPath) {
    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_move_to(path, 1.0f, 2.0f);
    rdt_path_line_to(path, 11.0f, 2.0f);
    rdt_path_line_to(path, 11.0f, 12.0f);
    rdt_path_close(path);

    Color color = {};
    color.r = 8;
    color.g = 9;
    color.b = 10;
    color.a = 255;
    RdtMatrix transform = rdt_matrix_translate(3.0f, 4.0f);
    paint_fill_path(&pl, path, color, RDT_FILL_EVEN_ODD, &transform);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, nullptr, &stats);

    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<path d=\"M1.00,2.00 L11.00,2.00 L11.00,12.00 Z \" fill=\"rgb(8,9,10)\" fill-rule=\"evenodd\" transform=\"matrix(1 0 0 1 3 4)\" />"),
        nullptr);

    strbuf_free(out);
    rdt_path_free(path);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsInspectableStrokePath) {
    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_move_to(path, 0.0f, 0.0f);
    rdt_path_cubic_to(path, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f);

    Color color = {};
    color.r = 20;
    color.g = 30;
    color.b = 40;
    color.a = 128;
    float dashes[2] = {2.0f, 4.0f};
    paint_stroke_path(&pl, path, color, 1.5f,
                      RDT_CAP_ROUND, RDT_JOIN_BEVEL,
                      dashes, 2, 0.5f, nullptr);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, nullptr, &stats);

    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<path d=\"M0.00,0.00 C1.00,2.00 3.00,4.00 5.00,6.00 \" fill=\"none\" stroke=\"rgba(20,30,40,0.502)\" stroke-width=\"1.50\" stroke-linecap=\"round\" stroke-linejoin=\"bevel\" stroke-dasharray=\"2.00 4.00\" stroke-dashoffset=\"0.50\" />"),
        nullptr);

    strbuf_free(out);
    rdt_path_free(path);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsClipGroup) {
    RdtPath* clip_path = rdt_path_new();
    ASSERT_NE(clip_path, nullptr);
    rdt_path_add_rect(clip_path, 1.0f, 2.0f, 30.0f, 40.0f, 3.0f, 4.0f);

    RdtMatrix transform = rdt_matrix_translate(5.0f, 6.0f);
    Color fill_color = {};
    fill_color.r = 1;
    fill_color.g = 2;
    fill_color.b = 3;
    fill_color.a = 255;
    paint_push_clip(&pl, clip_path, &transform);
    paint_fill_rect(&pl, 10.0f, 11.0f, 12.0f, 13.0f, fill_color);
    paint_pop_clip(&pl);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringOptions options = {};
    options.resource_id_base = 30;
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, &options, &stats);

    EXPECT_EQ(stats.command_count, 3);
    EXPECT_EQ(stats.emitted_count, 3);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<defs><clipPath id=\"paint-ir-clip-30\"><path d=\"M4.00,2.00 L28.00,2.00 A3.00,4.00 0 0 1 31.00,6.00 L31.00,38.00 A3.00,4.00 0 0 1 28.00,42.00 L4.00,42.00 A3.00,4.00 0 0 1 1.00,38.00 L1.00,6.00 A3.00,4.00 0 0 1 4.00,2.00 Z \" transform=\"matrix(1 0 0 1 5 6)\" /></clipPath></defs>"),
        nullptr);
    EXPECT_NE(strstr(out->str, "<g clip-path=\"url(#paint-ir-clip-30)\">"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "  <rect x=\"10.00\" y=\"11.00\" width=\"12.00\" height=\"13.00\" fill=\"rgb(1,2,3)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "</g>"), nullptr);

    strbuf_free(out);
    rdt_path_free(clip_path);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsTransformGroup) {
    RdtMatrix transform = rdt_matrix_translate(5.0f, 6.0f);
    Color fill_color = {};
    fill_color.r = 7;
    fill_color.g = 8;
    fill_color.b = 9;
    fill_color.a = 255;

    paint_push_transform(&pl, &transform);
    paint_fill_rect(&pl, 1.0f, 2.0f, 3.0f, 4.0f, fill_color);
    paint_pop_transform(&pl);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, nullptr, &stats);

    EXPECT_EQ(stats.command_count, 3);
    EXPECT_EQ(stats.emitted_count, 3);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str, "<g transform=\"matrix(1 0 0 1 5 6)\">\n"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "  <rect x=\"1.00\" y=\"2.00\" width=\"3.00\" height=\"4.00\" fill=\"rgb(7,8,9)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "</g>"), nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgStreamingLoweringKeepsTransformOpenAcrossFragments) {
    RdtMatrix transform = rdt_matrix_translate(5.0f, 6.0f);
    Color fill_color = {};
    fill_color.r = 7;
    fill_color.g = 8;
    fill_color.b = 9;
    fill_color.a = 255;

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringState state = {};
    paint_svg_lowering_state_init(&state, 0);
    PaintSvgLoweringStats stats = {};

    paint_push_transform(&pl, &transform);
    paint_ir_lower_svg_stream(&pl, out, nullptr, &state, &stats);
    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(state.open_transform_depth, 1);
    EXPECT_EQ(state.indent_level, 1);
    paint_list_clear(&pl);

    paint_fill_rect(&pl, 1.0f, 2.0f, 3.0f, 4.0f, fill_color);
    paint_ir_lower_svg_stream(&pl, out, nullptr, &state, &stats);
    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(state.open_transform_depth, 1);
    EXPECT_EQ(state.indent_level, 1);
    paint_list_clear(&pl);

    paint_pop_transform(&pl);
    paint_ir_lower_svg_stream(&pl, out, nullptr, &state, &stats);
    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(state.open_transform_depth, 0);
    EXPECT_EQ(state.indent_level, 0);

    EXPECT_NE(strstr(out->str, "<g transform=\"matrix(1 0 0 1 5 6)\">\n"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "  <rect x=\"1.00\" y=\"2.00\" width=\"3.00\" height=\"4.00\" fill=\"rgb(7,8,9)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "</g>"), nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsOpacityEffectGroup) {
    PaintEffectGroup group = {};
    group.opacity = 0.625f;
    Color fill_color = {};
    fill_color.r = 7;
    fill_color.g = 8;
    fill_color.b = 9;
    fill_color.a = 255;

    paint_begin_effect_group(&pl, &group);
    paint_fill_rect(&pl, 1.0f, 2.0f, 3.0f, 4.0f, fill_color);
    paint_end_effect_group(&pl);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, nullptr, &stats);

    EXPECT_EQ(stats.command_count, 3);
    EXPECT_EQ(stats.emitted_count, 3);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str, "<g opacity=\"0.6250\">\n"), nullptr);
    EXPECT_NE(strstr(out->str,
        "  <rect x=\"1.00\" y=\"2.00\" width=\"3.00\" height=\"4.00\" fill=\"rgb(7,8,9)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "</g>"), nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsTransformedOpacityEffectGroup) {
    PaintEffectGroup group = {};
    group.opacity = 0.5f;
    group.has_transform = true;
    group.transform = rdt_matrix_translate(10.0f, 12.0f);
    Color fill_color = {};
    fill_color.r = 7;
    fill_color.g = 8;
    fill_color.b = 9;
    fill_color.a = 255;

    paint_begin_effect_group(&pl, &group);
    paint_fill_rect(&pl, 1.0f, 2.0f, 3.0f, 4.0f, fill_color);
    paint_end_effect_group(&pl);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, nullptr, &stats);

    EXPECT_EQ(stats.command_count, 3);
    EXPECT_EQ(stats.emitted_count, 3);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<g opacity=\"0.5000\" transform=\"matrix(1 0 0 1 10 12)\">\n"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "  <rect x=\"1.00\" y=\"2.00\" width=\"3.00\" height=\"4.00\" fill=\"rgb(7,8,9)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "</g>"), nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgStreamingLoweringKeepsOpacityOpenAcrossFragments) {
    PaintEffectGroup group = {};
    group.opacity = 0.5f;
    Color fill_color = {};
    fill_color.r = 7;
    fill_color.g = 8;
    fill_color.b = 9;
    fill_color.a = 255;

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringState state = {};
    paint_svg_lowering_state_init(&state, 0);
    PaintSvgLoweringStats stats = {};

    paint_begin_effect_group(&pl, &group);
    paint_ir_lower_svg_stream(&pl, out, nullptr, &state, &stats);
    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(state.open_effect_depth, 1);
    EXPECT_EQ(state.indent_level, 1);
    paint_list_clear(&pl);

    paint_fill_rect(&pl, 1.0f, 2.0f, 3.0f, 4.0f, fill_color);
    paint_ir_lower_svg_stream(&pl, out, nullptr, &state, &stats);
    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(state.open_effect_depth, 1);
    EXPECT_EQ(state.indent_level, 1);
    paint_list_clear(&pl);

    paint_end_effect_group(&pl);
    paint_ir_lower_svg_stream(&pl, out, nullptr, &state, &stats);
    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(state.open_effect_depth, 0);
    EXPECT_EQ(state.indent_level, 0);

    EXPECT_NE(strstr(out->str, "<g opacity=\"0.5000\">\n"), nullptr);
    EXPECT_NE(strstr(out->str,
        "  <rect x=\"1.00\" y=\"2.00\" width=\"3.00\" height=\"4.00\" fill=\"rgb(7,8,9)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "</g>"), nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsNativeTextRun) {
    PaintGlyphRun run = {};
    run.text = "A < B & C";
    run.text_len = -1;
    run.font_family = "A&B Sans";
    run.font_size = 13.5f;
    run.x = 2.0f;
    run.baseline_y = 9.0f;
    run.word_spacing = 1.25f;
    run.font_weight = 700;
    run.italic = true;
    run.color.r = 17;
    run.color.g = 34;
    run.color.b = 51;
    run.color.a = 255;
    RdtMatrix transform = rdt_matrix_translate(3.0f, 4.0f);
    run.has_transform = true;
    run.transform = transform;

    paint_glyph_run(&pl, &run);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, nullptr, &stats);

    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<text x=\"2.00\" y=\"9.00\" font-family=\"A&amp;B Sans\" font-size=\"13.50\" fill=\"rgb(17,34,51)\" font-weight=\"700\" font-style=\"italic\" word-spacing=\"1.25\" transform=\"matrix(1 0 0 1 3 4)\">A &lt; B &amp; C</text>"),
        nullptr);

    strbuf_free(out);
}

TEST_F(PaintIrParityTest, SvgLoweringHonorsExportTargetCaps) {
    Color color = {};
    color.r = 1;
    color.g = 2;
    color.b = 3;
    color.a = 255;

    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_move_to(path, 0.0f, 0.0f);
    rdt_path_line_to(path, 10.0f, 0.0f);
    rdt_path_close(path);

    paint_fill_rect(&pl, 1.0f, 2.0f, 3.0f, 4.0f, color);
    paint_fill_rounded_rect(&pl, 5.0f, 6.0f, 7.0f, 8.0f, 2.0f, 2.0f, color);
    paint_fill_path(&pl, path, color, RDT_FILL_WINDING, nullptr);
    paint_stroke_path(&pl, path, color, 1.25f, RDT_CAP_BUTT, RDT_JOIN_MITER,
                      nullptr, 0, 0.0f, nullptr);
    RdtGradientStop stops[2] = {
        {0.0f, 1, 2, 3, 255},
        {1.0f, 4, 5, 6, 255}
    };
    paint_fill_linear_gradient(&pl, path, 0.0f, 0.0f, 10.0f, 10.0f,
                               stops, 2, RDT_FILL_WINDING, nullptr, nullptr);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringOptions options = {};
    options.emit_unsupported_comments = true;
    options.caps = render_export_target_get_caps(RENDER_EXPORT_TARGET_PDF);
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, &options, &stats);

    EXPECT_EQ(stats.command_count, 5);
    EXPECT_EQ(stats.emitted_count, 5);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<rect x=\"1.00\" y=\"2.00\" width=\"3.00\" height=\"4.00\" fill=\"rgb(1,2,3)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "<rect x=\"5.00\" y=\"6.00\" width=\"7.00\" height=\"8.00\" rx=\"2.00\" ry=\"2.00\" fill=\"rgb(1,2,3)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "<path d=\"M0.00,0.00 L10.00,0.00 Z \" fill=\"rgb(1,2,3)\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "<path d=\"M0.00,0.00 L10.00,0.00 Z \" fill=\"none\" stroke=\"rgb(1,2,3)\" stroke-width=\"1.25\" stroke-linecap=\"butt\" stroke-linejoin=\"miter\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str, "<linearGradient"), nullptr);

    strbuf_free(out);
    rdt_path_free(path);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsLinearGradientPath) {
    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, 1.0f, 2.0f, 30.0f, 40.0f, 0.0f, 0.0f);

    RdtGradientStop stops[2] = {
        {0.0f, 10, 20, 30, 255},
        {1.0f, 40, 50, 60, 128}
    };
    paint_fill_linear_gradient(&pl, path, 1.0f, 2.0f, 31.0f, 42.0f,
                               stops, 2, RDT_FILL_WINDING, nullptr, nullptr);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringOptions options = {};
    options.resource_id_base = 70;
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, &options, &stats);

    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<defs><linearGradient id=\"paint-ir-linear-70\" gradientUnits=\"userSpaceOnUse\" x1=\"1.000\" y1=\"2.000\" x2=\"31.000\" y2=\"42.000\">"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "<stop offset=\"1.0000\" stop-color=\"rgb(40,50,60)\" stop-opacity=\"0.5020\" />"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "<path d=\"M1.00,2.00 L31.00,2.00 L31.00,42.00 L1.00,42.00 Z \" fill=\"url(#paint-ir-linear-70)\" />"),
        nullptr);

    strbuf_free(out);
    rdt_path_free(path);
}

TEST_F(PaintIrParityTest, SvgLoweringEmitsRadialGradientPath) {
    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_add_rect(path, 0.0f, 0.0f, 20.0f, 10.0f, 0.0f, 0.0f);

    RdtGradientStop stops[2] = {
        {0.25f, 1, 2, 3, 255},
        {0.75f, 4, 5, 6, 255}
    };
    paint_fill_radial_gradient(&pl, path, 10.0f, 5.0f, 8.0f,
                               stops, 2, RDT_FILL_EVEN_ODD, nullptr, nullptr);

    StrBuf* out = strbuf_new();
    ASSERT_NE(out, nullptr);

    PaintSvgLoweringOptions options = {};
    options.resource_id_base = 90;
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&pl, out, &options, &stats);

    EXPECT_EQ(stats.command_count, 1);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(out->str,
        "<defs><radialGradient id=\"paint-ir-radial-90\" gradientUnits=\"userSpaceOnUse\" cx=\"10.000\" cy=\"5.000\" r=\"8.000\">"),
        nullptr);
    EXPECT_NE(strstr(out->str,
        "<path d=\"M0.00,0.00 L20.00,0.00 L20.00,10.00 L0.00,10.00 Z \" fill=\"url(#paint-ir-radial-90)\" fill-rule=\"evenodd\" />"),
        nullptr);

    strbuf_free(out);
    rdt_path_free(path);
}
