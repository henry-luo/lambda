#include <gtest/gtest.h>

#include "../radiant/render.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"

class RetainedDisplayListTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Arena* arena = nullptr;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
    }

    void TearDown() override {
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

static DisplayItem* retained_test_add_rect(DisplayList* dl,
                                           float x, float y, float w, float h) {
    DisplayItem* rect = dl_alloc_item(dl);
    if (!rect) return nullptr;
    rect->op = DL_FILL_RECT;
    rect->bounds[0] = x;
    rect->bounds[1] = y;
    rect->bounds[2] = w;
    rect->bounds[3] = h;
    rect->fill_rect.x = x;
    rect->fill_rect.y = y;
    rect->fill_rect.w = w;
    rect->fill_rect.h = h;
    Color color = {};
    color.c = 0xff336699;
    rect->fill_rect.color = color;
    return rect;
}

static DirtyRect retained_test_dirty(float x, float y, float w, float h,
                                     uint32_t source_view_id) {
    DirtyRect dirty = {};
    dirty.x = x;
    dirty.y = y;
    dirty.width = w;
    dirty.height = h;
    dirty.source_view_id = source_view_id;
    return dirty;
}

static bool retained_test_contains_view_id(void* userdata, uint32_t source_view_id) {
    uint32_t* contained_id = (uint32_t*)userdata;
    return contained_id && *contained_id == source_view_id;
}

TEST_F(RetainedDisplayListTest, CapturesAndAppendsMatchedElementFragment) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 42, 10.0f, 20.0f, 30.0f, 40.0f);
    Color color = { .c = 0xff336699 };
    DisplayItem* rect = dl_alloc_item(&source);
    ASSERT_NE(rect, nullptr);
    rect->op = DL_FILL_RECT;
    rect->bounds[0] = 12.0f;
    rect->bounds[1] = 24.0f;
    rect->bounds[2] = 8.0f;
    rect->bounds[3] = 6.0f;
    rect->fill_rect.x = 12.0f;
    rect->fill_rect.y = 24.0f;
    rect->fill_rect.w = 8.0f;
    rect->fill_rect.h = 6.0f;
    rect->fill_rect.color = color;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_begin_frame(cache);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 42);
    ASSERT_NE(fragment, nullptr);
    EXPECT_EQ(retained_dl_fragment_item_count(fragment), 3);
    RetainedDisplayListStats stats = retained_dl_cache_stats(cache);
    EXPECT_EQ(stats.capture_candidates, 1);
    EXPECT_EQ(stats.captured, 1);
    EXPECT_EQ(stats.skipped_non_retainable, 0);
    EXPECT_EQ(stats.copy_failed, 0);

    DisplayList replay = {};
    dl_init(&replay, arena);
    ASSERT_TRUE(retained_dl_append_fragment(&replay, fragment));
    ASSERT_EQ(replay.count, 3);
    EXPECT_EQ(replay.items[0].op, DL_BEGIN_ELEMENT);
    EXPECT_EQ(replay.items[0].element_marker.matching_index, 2);
    EXPECT_EQ(replay.items[1].op, DL_FILL_RECT);
    EXPECT_FLOAT_EQ(replay.items[1].fill_rect.x, 12.0f);
    EXPECT_EQ(replay.items[2].op, DL_END_ELEMENT);
    EXPECT_EQ(replay.items[2].element_marker.matching_index, 0);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, TracksSkippedBorrowedResourceWithoutGeneration) {
    DisplayList source = {};
    dl_init(&source, arena);

    uint32_t pixels[4] = {};
    int begin = dl_begin_element(&source, 51, 0.0f, 0.0f, 2.0f, 2.0f);
    DisplayItem* image = dl_alloc_item(&source);
    ASSERT_NE(image, nullptr);
    image->op = DL_DRAW_IMAGE;
    image->bounds[2] = 2.0f;
    image->bounds[3] = 2.0f;
    image->draw_image.pixels = pixels;
    image->draw_image.src_w = 2;
    image->draw_image.src_h = 2;
    image->draw_image.src_stride = 2;
    image->draw_image.dst_w = 2.0f;
    image->draw_image.dst_h = 2.0f;
    EXPECT_FALSE(dl_item_is_retainable_for_fragment(image));
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_begin_frame(cache);
    retained_dl_cache_capture(cache, &source);

    EXPECT_EQ(retained_dl_cache_get(cache, 51), nullptr);
    RetainedDisplayListStats stats = retained_dl_cache_stats(cache);
    EXPECT_EQ(stats.capture_candidates, 1);
    EXPECT_EQ(stats.captured, 0);
    EXPECT_EQ(stats.skipped_non_retainable, 1);

    retained_dl_cache_begin_frame(cache);
    stats = retained_dl_cache_stats(cache);
    EXPECT_EQ(stats.capture_candidates, 0);
    EXPECT_EQ(stats.skipped_non_retainable, 0);

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, TracksReuseOutcomesForRenderPathTrace) {
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_begin_frame(cache);

    retained_dl_cache_note_reuse_miss(cache);
    retained_dl_cache_note_reuse_rejected_resources(cache);
    retained_dl_cache_note_reuse_rejected_dirty(cache);
    retained_dl_cache_note_reuse_hit(cache);

    RetainedDisplayListStats stats = retained_dl_cache_stats(cache);
    EXPECT_EQ(stats.reuse_misses, 1);
    EXPECT_EQ(stats.reuse_rejected_resources, 1);
    EXPECT_EQ(stats.reuse_rejected_dirty, 1);
    EXPECT_EQ(stats.reuse_hits, 1);

    retained_dl_cache_begin_frame(cache);
    stats = retained_dl_cache_stats(cache);
    EXPECT_EQ(stats.reuse_misses, 0);
    EXPECT_EQ(stats.reuse_hits, 0);

    retained_dl_cache_destroy(cache);
}

TEST_F(RetainedDisplayListTest, AppendsRetainedFragmentForExternalDirtySource) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 42, 10.0f, 20.0f, 30.0f, 40.0f);
    ASSERT_NE(retained_test_add_rect(&source, 12.0f, 24.0f, 8.0f, 6.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 42);
    ASSERT_NE(fragment, nullptr);

    DirtyRect dirty = retained_test_dirty(12.0f, 24.0f, 2.0f, 2.0f, 9001);
    DirtyTracker tracker = {};
    tracker.dirty_list = &dirty;
    Bound current_marker = {10.0f, 20.0f, 40.0f, 60.0f};
    uint32_t contained_id = 7777;

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.count, 3);
    EXPECT_EQ(replay.items[1].op, DL_FILL_RECT);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, AppendsRetainedFragmentWhenUnknownDirtyMissesVisualBounds) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 42, 10.0f, 20.0f, 30.0f, 40.0f);
    ASSERT_NE(retained_test_add_rect(&source, 12.0f, 24.0f, 8.0f, 6.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 42);
    ASSERT_NE(fragment, nullptr);

    DirtyRect dirty = retained_test_dirty(100.0f, 100.0f, 5.0f, 5.0f, 0);
    DirtyTracker tracker = {};
    tracker.dirty_list = &dirty;
    Bound current_marker = {10.0f, 20.0f, 40.0f, 60.0f};

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.count, 3);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsRetainedFragmentForUnknownIntersectingDirtySource) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 42, 10.0f, 20.0f, 30.0f, 40.0f);
    ASSERT_NE(retained_test_add_rect(&source, 12.0f, 24.0f, 8.0f, 6.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 42);
    ASSERT_NE(fragment, nullptr);

    DirtyRect dirty = retained_test_dirty(12.0f, 24.0f, 2.0f, 2.0f, 0);
    DirtyTracker tracker = {};
    tracker.dirty_list = &dirty;
    Bound current_marker = {10.0f, 20.0f, 40.0f, 60.0f};

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.count, 0);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsRetainedFragmentForDirtySourceInsideSubtree) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 42, 10.0f, 20.0f, 30.0f, 40.0f);
    ASSERT_NE(retained_test_add_rect(&source, 12.0f, 24.0f, 8.0f, 6.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 42);
    ASSERT_NE(fragment, nullptr);

    uint32_t contained_id = 1234;
    DirtyRect dirty = retained_test_dirty(12.0f, 24.0f, 2.0f, 2.0f, contained_id);
    DirtyTracker tracker = {};
    tracker.dirty_list = &dirty;
    Bound current_marker = {10.0f, 20.0f, 40.0f, 60.0f};

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.count, 0);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsRetainedFragmentWhenMarkerBoundsChanged) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 42, 10.0f, 20.0f, 30.0f, 40.0f);
    ASSERT_NE(retained_test_add_rect(&source, 12.0f, 24.0f, 8.0f, 6.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 42);
    ASSERT_NE(fragment, nullptr);

    DirtyRect dirty = retained_test_dirty(12.0f, 24.0f, 2.0f, 2.0f, 9001);
    DirtyTracker tracker = {};
    tracker.dirty_list = &dirty;
    Bound moved_marker = {11.0f, 20.0f, 41.0f, 60.0f};

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, moved_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.count, 0);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsRetainedFragmentDuringFullRepaint) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 43, 10.0f, 20.0f, 30.0f, 40.0f);
    ASSERT_NE(retained_test_add_rect(&source, 12.0f, 24.0f, 8.0f, 6.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 43);
    ASSERT_NE(fragment, nullptr);

    DirtyRect dirty = retained_test_dirty(12.0f, 24.0f, 2.0f, 2.0f, 9001);
    DirtyTracker tracker = {};
    tracker.full_repaint = true;
    tracker.dirty_list = &dirty;
    Bound current_marker = {10.0f, 20.0f, 40.0f, 60.0f};

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.count, 0);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, AppliesDirtyScaleWhenTestingFragmentVisualBounds) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 44, 10.0f, 10.0f, 20.0f, 20.0f);
    ASSERT_NE(retained_test_add_rect(&source, 20.0f, 20.0f, 8.0f, 8.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 44);
    ASSERT_NE(fragment, nullptr);

    DirtyRect dirty = retained_test_dirty(10.0f, 10.0f, 1.0f, 1.0f, 9001);
    DirtyTracker tracker = {};
    tracker.dirty_list = &dirty;
    Bound current_marker = {10.0f, 10.0f, 30.0f, 30.0f};
    uint32_t contained_id = 7777;

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 2.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.count, 3);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, ReusesWhenUnknownDirtyMissesAndExternalDirtyIntersects) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 45, 10.0f, 20.0f, 30.0f, 40.0f);
    ASSERT_NE(retained_test_add_rect(&source, 12.0f, 24.0f, 8.0f, 6.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 45);
    ASSERT_NE(fragment, nullptr);

    DirtyRect unknown_miss = retained_test_dirty(100.0f, 100.0f, 5.0f, 5.0f, 0);
    DirtyRect external_hit = retained_test_dirty(12.0f, 24.0f, 2.0f, 2.0f, 9001);
    unknown_miss.next = &external_hit;
    DirtyTracker tracker = {};
    tracker.dirty_list = &unknown_miss;
    Bound current_marker = {10.0f, 20.0f, 40.0f, 60.0f};
    uint32_t contained_id = 7777;

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.count, 3);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsStaleBorrowedSurfaceGeneration) {
    DisplayList source = {};
    dl_init(&source, arena);

    ImageSurface surface = {};
    surface.width = 10;
    surface.height = 10;
    surface.generation = 5;

    int begin = dl_begin_element(&source, 77, 0.0f, 0.0f, 10.0f, 10.0f);
    DisplayItem* blit = dl_alloc_item(&source);
    ASSERT_NE(blit, nullptr);
    blit->op = DL_BLIT_SURFACE_SCALED;
    blit->bounds[2] = 10.0f;
    blit->bounds[3] = 10.0f;
    blit->blit_surface_scaled.src_surface = &surface;
    blit->blit_surface_scaled.src_generation = surface.generation;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 77);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    surface.generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, DeepCopiesRasterClipShapeStacksForRetainedReplay) {
    DisplayList source = {};
    dl_init(&source, arena);

    float vx[3] = {1.0f, 9.0f, 5.0f};
    float vy[3] = {2.0f, 2.0f, 8.0f};

    int begin = dl_begin_element(&source, 93, 0.0f, 0.0f, 20.0f, 20.0f);
    DisplayItem* fill = dl_alloc_item(&source);
    ASSERT_NE(fill, nullptr);
    fill->op = DL_FILL_SURFACE_RECT;
    fill->bounds[0] = 0.0f;
    fill->bounds[1] = 0.0f;
    fill->bounds[2] = 20.0f;
    fill->bounds[3] = 20.0f;
    fill->fill_surface_rect.clip_shapes.depth = 1;
    fill->fill_surface_rect.clip_shapes.type[0] = CLIP_SHAPE_POLYGON;
    fill->fill_surface_rect.clip_shapes.polygon_count[0] = 3;
    fill->fill_surface_rect.clip_shapes.polygon_vx[0] = vx;
    fill->fill_surface_rect.clip_shapes.polygon_vy[0] = vy;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 93);
    ASSERT_NE(fragment, nullptr);

    vx[0] = 100.0f;
    vy[0] = 200.0f;

    DisplayList replay = {};
    dl_init(&replay, arena);
    ASSERT_TRUE(retained_dl_append_fragment(&replay, fragment));
    ASSERT_EQ(replay.count, 3);
    ASSERT_EQ(replay.items[1].op, DL_FILL_SURFACE_RECT);
    const DlClipShapeStack* copied = &replay.items[1].fill_surface_rect.clip_shapes;
    EXPECT_EQ(copied->depth, 1);
    EXPECT_EQ(copied->type[0], CLIP_SHAPE_POLYGON);
    EXPECT_EQ(copied->polygon_count[0], 3);
    ASSERT_NE(copied->polygon_vx[0], nullptr);
    ASSERT_NE(copied->polygon_vy[0], nullptr);
    EXPECT_NE(copied->polygon_vx[0], vx);
    EXPECT_NE(copied->polygon_vy[0], vy);
    EXPECT_FLOAT_EQ(copied->polygon_vx[0][0], 1.0f);
    EXPECT_FLOAT_EQ(copied->polygon_vy[0][0], 2.0f);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, AppendsTransformedVisualFragmentWithStableMarkerBounds) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 92, 10.0f, 20.0f, 30.0f, 40.0f);
    ASSERT_NE(retained_test_add_rect(&source, 100.0f, 200.0f, 30.0f, 40.0f), nullptr);
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 92);
    ASSERT_NE(fragment, nullptr);

    DirtyRect dirty = retained_test_dirty(110.0f, 210.0f, 5.0f, 5.0f, 9001);
    DirtyTracker tracker = {};
    tracker.dirty_list = &dirty;
    Bound current_marker = {10.0f, 20.0f, 40.0f, 60.0f};
    uint32_t contained_id = 7777;

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.count, 3);
    EXPECT_FLOAT_EQ(replay.items[1].bounds[0], 100.0f);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsStaleBorrowedImageGeneration) {
    DisplayList source = {};
    dl_init(&source, arena);

    ImageSurface surface = {};
    surface.width = 2;
    surface.height = 2;
    surface.generation = 9;
    uint32_t pixels[4] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff};

    int begin = dl_begin_element(&source, 78, 0.0f, 0.0f, 2.0f, 2.0f);
    DisplayItem* image = dl_alloc_item(&source);
    ASSERT_NE(image, nullptr);
    image->op = DL_DRAW_IMAGE;
    image->bounds[2] = 2.0f;
    image->bounds[3] = 2.0f;
    image->draw_image.pixels = pixels;
    image->draw_image.resource_owner = &surface;
    image->draw_image.resource_generation = surface.generation;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 78);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    surface.generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsBorrowedImageWithoutGenerationAtCapture) {
    DisplayList source = {};
    dl_init(&source, arena);

    uint32_t pixels[4] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff};
    int begin = dl_begin_element(&source, 79, 0.0f, 0.0f, 2.0f, 2.0f);
    DisplayItem* image = dl_alloc_item(&source);
    ASSERT_NE(image, nullptr);
    image->op = DL_DRAW_IMAGE;
    image->bounds[2] = 2.0f;
    image->bounds[3] = 2.0f;
    image->draw_image.pixels = pixels;
    image->draw_image.resource_owner = nullptr;
    image->draw_image.resource_generation = 0;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    EXPECT_EQ(retained_dl_cache_get(cache, 79), nullptr);

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsBorrowedGlyphWithoutGeneration) {
    DisplayList source = {};
    dl_init(&source, arena);

    uint8_t pixel = 255;
    int begin = dl_begin_element(&source, 88, 0.0f, 0.0f, 4.0f, 4.0f);
    DisplayItem* glyph = dl_alloc_item(&source);
    ASSERT_NE(glyph, nullptr);
    glyph->op = DL_DRAW_GLYPH;
    glyph->bounds[2] = 4.0f;
    glyph->bounds[3] = 4.0f;
    glyph->draw_glyph.bitmap.buffer = &pixel;
    glyph->draw_glyph.bitmap.width = 1;
    glyph->draw_glyph.bitmap.height = 1;
    glyph->draw_glyph.bitmap.pitch = 1;
    glyph->draw_glyph.resource_generation = 0;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 88);
    EXPECT_EQ(fragment, nullptr);

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsBorrowedFilterAtCapture) {
    DisplayList source = {};
    dl_init(&source, arena);

    int fake_filter = 1;
    int begin = dl_begin_element(&source, 87, 0.0f, 0.0f, 10.0f, 10.0f);
    DisplayItem* filter = dl_alloc_item(&source);
    ASSERT_NE(filter, nullptr);
    filter->op = DL_APPLY_FILTER;
    filter->bounds[2] = 10.0f;
    filter->bounds[3] = 10.0f;
    filter->apply_filter.filter = &fake_filter;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    EXPECT_EQ(retained_dl_cache_get(cache, 87), nullptr);

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, UnsafeRecaptureClearsPreviousFragment) {
    DisplayList safe_source = {};
    dl_init(&safe_source, arena);
    int safe_begin = dl_begin_element(&safe_source, 86, 0.0f, 0.0f, 10.0f, 10.0f);
    ASSERT_NE(retained_test_add_rect(&safe_source, 1.0f, 1.0f, 4.0f, 4.0f), nullptr);
    dl_end_element(&safe_source, safe_begin);

    DisplayList unsafe_source = {};
    dl_init(&unsafe_source, arena);
    int fake_filter = 1;
    int unsafe_begin = dl_begin_element(&unsafe_source, 86, 0.0f, 0.0f, 10.0f, 10.0f);
    DisplayItem* filter = dl_alloc_item(&unsafe_source);
    ASSERT_NE(filter, nullptr);
    filter->op = DL_APPLY_FILTER;
    filter->bounds[2] = 10.0f;
    filter->bounds[3] = 10.0f;
    filter->apply_filter.filter = &fake_filter;
    dl_end_element(&unsafe_source, unsafe_begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &safe_source);
    ASSERT_NE(retained_dl_cache_get(cache, 86), nullptr);

    retained_dl_cache_capture(cache, &unsafe_source);
    EXPECT_EQ(retained_dl_cache_get(cache, 86), nullptr);

    retained_dl_cache_destroy(cache);
    dl_destroy(&unsafe_source);
    dl_destroy(&safe_source);
}

TEST_F(RetainedDisplayListTest, KeepsBorrowedGlyphWhenGenerationMatches) {
    DisplayList source = {};
    dl_init(&source, arena);

    uint8_t pixel = 255;
    int begin = dl_begin_element(&source, 91, 0.0f, 0.0f, 4.0f, 4.0f);
    DisplayItem* glyph = dl_alloc_item(&source);
    ASSERT_NE(glyph, nullptr);
    glyph->op = DL_DRAW_GLYPH;
    glyph->bounds[2] = 4.0f;
    glyph->bounds[3] = 4.0f;
    glyph->draw_glyph.bitmap.buffer = &pixel;
    glyph->draw_glyph.bitmap.width = 1;
    glyph->draw_glyph.bitmap.height = 1;
    glyph->draw_glyph.bitmap.pitch = 1;
    glyph->draw_glyph.resource_generation = 7;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 91);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 7));
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 8));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, PreservesOriginalMarkerBoundsForTransformedVisualBounds) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 92, 10.0f, 20.0f, 30.0f, 40.0f);
    DisplayItem* transformed_rect = dl_alloc_item(&source);
    ASSERT_NE(transformed_rect, nullptr);
    transformed_rect->op = DL_FILL_RECT;
    transformed_rect->bounds[0] = 100.0f;
    transformed_rect->bounds[1] = 200.0f;
    transformed_rect->bounds[2] = 30.0f;
    transformed_rect->bounds[3] = 40.0f;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 92);
    ASSERT_NE(fragment, nullptr);
    Bound marker_bounds = retained_dl_fragment_marker_bounds(fragment);
    EXPECT_FLOAT_EQ(marker_bounds.left, 10.0f);
    EXPECT_FLOAT_EQ(marker_bounds.top, 20.0f);
    EXPECT_FLOAT_EQ(marker_bounds.right, 40.0f);
    EXPECT_FLOAT_EQ(marker_bounds.bottom, 60.0f);

    Bound visual_bounds = retained_dl_fragment_bounds(fragment);
    EXPECT_FLOAT_EQ(visual_bounds.left, 10.0f);
    EXPECT_FLOAT_EQ(visual_bounds.top, 20.0f);
    EXPECT_FLOAT_EQ(visual_bounds.right, 130.0f);
    EXPECT_FLOAT_EQ(visual_bounds.bottom, 240.0f);

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsUnknownDirtyIntersectingOnlyEffectOverflow) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 94, 50.0f, 50.0f, 20.0f, 20.0f);
    DisplayItem* shadow = dl_alloc_item(&source);
    ASSERT_NE(shadow, nullptr);
    shadow->op = DL_OUTER_SHADOW;
    shadow->bounds[0] = 40.0f;
    shadow->bounds[1] = 40.0f;
    shadow->bounds[2] = 48.0f;
    shadow->bounds[3] = 42.0f;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 94);
    ASSERT_NE(fragment, nullptr);
    Bound visual_bounds = retained_dl_fragment_bounds(fragment);
    EXPECT_FLOAT_EQ(visual_bounds.left, 40.0f);
    EXPECT_FLOAT_EQ(visual_bounds.top, 40.0f);
    EXPECT_FLOAT_EQ(visual_bounds.right, 88.0f);
    EXPECT_FLOAT_EQ(visual_bounds.bottom, 82.0f);

    DirtyRect dirty = retained_test_dirty(42.0f, 42.0f, 3.0f, 3.0f, 0);
    DirtyTracker tracker = {};
    tracker.dirty_list = &dirty;
    Bound current_marker = {50.0f, 50.0f, 70.0f, 70.0f};

    DisplayList replay = {};
    dl_init(&replay, arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.count, 0);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsStaleVideoGeneration) {
    DisplayList source = {};
    dl_init(&source, arena);

    int begin = dl_begin_element(&source, 89, 0.0f, 0.0f, 12.0f, 8.0f);
    DisplayItem* video = dl_alloc_item(&source);
    ASSERT_NE(video, nullptr);
    video->op = DL_VIDEO_PLACEHOLDER;
    video->bounds[2] = 12.0f;
    video->bounds[3] = 8.0f;
    video->video_placeholder.video = &source;
    video->video_placeholder.video_generation = 4;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 89);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 4, 1));
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 5, 1));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsStaleWebviewSurfaceGeneration) {
    DisplayList source = {};
    dl_init(&source, arena);

    ImageSurface surface = {};
    surface.width = 12;
    surface.height = 8;
    surface.generation = 3;

    int begin = dl_begin_element(&source, 90, 0.0f, 0.0f, 12.0f, 8.0f);
    DisplayItem* webview = dl_alloc_item(&source);
    ASSERT_NE(webview, nullptr);
    webview->op = DL_WEBVIEW_LAYER_PLACEHOLDER;
    webview->bounds[2] = 12.0f;
    webview->bounds[3] = 8.0f;
    webview->webview_layer_placeholder.surface = &surface;
    webview->webview_layer_placeholder.surface_generation = surface.generation;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 90);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    surface.generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}
