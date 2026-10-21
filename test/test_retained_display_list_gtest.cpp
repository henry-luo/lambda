#include <gtest/gtest.h>
#include <pthread.h>
#include <float.h>
#include <math.h>

#include "../radiant/render.hpp"
#include "../radiant/render_metrics.hpp"
#include "radiant_render_test_utils.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"

// Tests link only the surface registry, not the full image teardown; their
// surfaces own no pixels, so releasing the slot and the struct is complete.
static void free_test_surface(ImageSurface* surface) {
    image_surface_release_slot(surface);
    mem_free(surface);
}


class RetainedDisplayListTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Arena* arena = nullptr;          // source list's arena
    Arena* replay_arena = nullptr;   // each list's scratch owns its arena

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default();
        replay_arena = arena_create_default();
    }

    void TearDown() override {
        if (arena) {
            arena_destroy(arena);
            arena = nullptr;
        }
        if (replay_arena) {
            arena_destroy(replay_arena);
            replay_arena = nullptr;
        }
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }
};

static DisplayItem* retained_test_add_rect(DisplayList* dl,
                                           float x, float y, float w, float h, uint32_t rgba = 0xff336699u) {
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
    color.c = rgba;
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

static bool retained_test_contains_view_id(void* userdata, const DirtyRect* dirty) {
    uint32_t* contained_id = (uint32_t*)userdata;
    return contained_id && *contained_id == dirty->source_view_id;
}

TEST_F(RetainedDisplayListTest, ReleasesReplacedViewsWhileKeepingCurrentFragments) {
    DisplayList source = {};
    dl_init(&source, arena);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    const RetainedDisplayListFragment* stable = nullptr;

    // reactive redraws preserve the shell while replacing the board's node IDs.
    for (uint32_t frame = 0; frame < 32; frame++) {
        dl_clear(&source);
        int root = dl_begin_element(&source, 42, 0.0f, 0.0f, 100.0f, 10.0f);
        for (uint32_t child = 0; child < 24; child++) {
            float x = child * 4.0f;
            int begin = dl_begin_element(&source, 1000 + frame * 24 + child,
                x, 0.0f, 2.0f, 2.0f);
            ASSERT_NE(retained_test_add_rect(&source, x, 0.0f, 2.0f, 2.0f), nullptr);
            dl_end_element(&source, begin);
        }
        dl_end_element(&source, root);
        retained_dl_cache_begin_frame(cache);
        if (stable) EXPECT_EQ(retained_dl_cache_get(cache, 42), stable);
        retained_dl_cache_capture(cache, &source);
        if (!stable) stable = retained_dl_cache_get(cache, 42);
        ASSERT_NE(stable, nullptr);
        EXPECT_EQ(retained_dl_cache_get(cache, 42), stable);
        for (uint32_t child = 0; child < 24; child++) {
            EXPECT_NE(retained_dl_cache_get(cache, 1000 + frame * 24 + child), nullptr);
            if (frame > 0) {
                EXPECT_EQ(retained_dl_cache_get(cache, 1000 + (frame - 1) * 24 + child), nullptr);
            }
        }
    }

    dl_clear(&source);
    retained_dl_cache_begin_frame(cache);
    retained_dl_cache_capture(cache, &source);
    EXPECT_EQ(retained_dl_cache_get(cache, 42), nullptr);
    for (uint32_t child = 0; child < 24; child++) {
        EXPECT_EQ(retained_dl_cache_get(cache, 1000 + 31 * 24 + child), nullptr);
    }
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
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
    dl_init(&replay, replay_arena);
    ASSERT_TRUE(retained_dl_append_fragment(&replay, fragment));
    ASSERT_EQ(replay.size(), 1u);
    EXPECT_EQ(test_radiant_command(&replay, 0).op, DL_BEGIN_ELEMENT);
    EXPECT_EQ(test_radiant_command(&replay, 0).element_marker.matching_index, 2);
    EXPECT_EQ(test_radiant_command(&replay, 1).op, DL_FILL_RECT);
    EXPECT_FLOAT_EQ(test_radiant_command(&replay, 1).fill_rect.x, 12.0f);
    EXPECT_EQ(test_radiant_command(&replay, 2).op, DL_END_ELEMENT);
    EXPECT_EQ(test_radiant_command(&replay, 2).element_marker.matching_index, 0);

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
    image->draw_image.local_pixels = lam::up((const uint32_t*)pixels);
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
    dl_init(&replay, replay_arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.size(), 1u);
    EXPECT_EQ(test_radiant_command(&replay, 1).op, DL_FILL_RECT);

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
    dl_init(&replay, replay_arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.size(), 1u);

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
    dl_init(&replay, replay_arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.size(), 0u);

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
    dl_init(&replay, replay_arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.size(), 0u);

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
    dl_init(&replay, replay_arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, moved_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.size(), 0u);

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
    dl_init(&replay, replay_arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.size(), 0u);

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
    dl_init(&replay, replay_arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 2.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.size(), 1u);

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
    dl_init(&replay, replay_arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.size(), 1u);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsStaleBorrowedSurfaceGeneration) {
    DisplayList source = {};
    dl_init(&source, arena);

    ImageSurface* surface = image_surface_alloc();
    ASSERT_NE(surface, nullptr);
    surface->width = 10;
    surface->height = 10;
    surface->generation = 5;

    int begin = dl_begin_element(&source, 77, 0.0f, 0.0f, 10.0f, 10.0f);
    DisplayItem* blit = dl_alloc_item(&source);
    ASSERT_NE(blit, nullptr);
    blit->op = DL_BLIT_SURFACE_SCALED;
    blit->bounds[2] = 10.0f;
    blit->bounds[3] = 10.0f;
    blit->blit_surface_scaled.src_resource = surface->self;
    blit->blit_surface_scaled.src_generation = surface->generation;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 77);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    surface->generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
    free_test_surface(surface);
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
    ClipShape polygon = {}; polygon.type = CLIP_SHAPE_POLYGON;
    polygon.polygon = {lam::own_arr(vx), lam::own_arr(vy), 3};
    ClipShape* clips[] = {&polygon};
    ASSERT_TRUE(dl_store_clip_shapes(&source, &fill->fill_surface_rect.clip_shapes, clips, 1));
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 93);
    ASSERT_NE(fragment, nullptr);

    vx[0] = 100.0f;
    vy[0] = 200.0f;

    DisplayList replay = {};
    dl_init(&replay, replay_arena);
    ASSERT_TRUE(retained_dl_append_fragment(&replay, fragment));
    ASSERT_EQ(replay.size(), 1u);
    ASSERT_EQ(test_radiant_command(&replay, 1).op, DL_FILL_SURFACE_RECT);
    const DlClipShapeStack* copied = test_radiant_command(&replay, 1).fill_surface_rect.clip_shapes;
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
    dl_init(&replay, replay_arena);
    EXPECT_TRUE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, &contained_id));
    EXPECT_EQ(replay.size(), 1u);
    EXPECT_FLOAT_EQ(test_radiant_command(&replay, 1).bounds[0], 100.0f);

    dl_destroy(&replay);
    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, RejectsStaleBorrowedImageGeneration) {
    DisplayList source = {};
    dl_init(&source, arena);

    ImageSurface* surface = image_surface_alloc();
    ASSERT_NE(surface, nullptr);
    surface->width = 2;
    surface->height = 2;
    surface->generation = 9;
    uint32_t pixels[4] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff};

    int begin = dl_begin_element(&source, 78, 0.0f, 0.0f, 2.0f, 2.0f);
    DisplayItem* image = dl_alloc_item(&source);
    ASSERT_NE(image, nullptr);
    image->op = DL_DRAW_IMAGE;
    image->bounds[2] = 2.0f;
    image->bounds[3] = 2.0f;
    image->draw_image.resource = surface->self;
    image->draw_image.resource_generation = surface->generation;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 78);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    surface->generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
    free_test_surface(surface);
}

// A retained image whose surface was destroyed must be rejected through the
// slot table, without reading the freed surface.
TEST_F(RetainedDisplayListTest, RejectsRetainedImageAfterSurfaceDestroyed) {
    DisplayList source = {};
    dl_init(&source, arena);

    ImageSurface* surface = image_surface_alloc();
    ASSERT_NE(surface, nullptr);
    ASSERT_FALSE(surface->self.is_null());
    surface->width = 2;
    surface->height = 2;
    surface->generation = 3;
    uint32_t pixels[4] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff};

    int begin = dl_begin_element(&source, 80, 0.0f, 0.0f, 2.0f, 2.0f);
    DisplayItem* image = dl_alloc_item(&source);
    ASSERT_NE(image, nullptr);
    image->op = DL_DRAW_IMAGE;
    image->bounds[2] = 2.0f;
    image->bounds[3] = 2.0f;
    image->draw_image.resource = surface->self;
    image->draw_image.resource_generation = surface->generation;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 80);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    lam::Handle<ImageSurface> stale = surface->self;
    free_test_surface(surface);
    EXPECT_EQ(image_surface_lookup(stale), nullptr);
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, StackSurfaceIsNotRetained) {
    DisplayList source = {};
    dl_init(&source, arena);
    ImageSurface surface = {};
    surface.width = 2;
    surface.height = 2;
    surface.generation = 4;
    uint32_t pixels[4] = {0, 0, 0, 0};
    int begin = dl_begin_element(&source, 81, 0.0f, 0.0f, 2.0f, 2.0f);
    DisplayItem* image = dl_alloc_item(&source);
    ASSERT_NE(image, nullptr);
    image->op = DL_DRAW_IMAGE;
    image->bounds[2] = 2.0f;
    image->bounds[3] = 2.0f;
    image->draw_image.local_pixels = lam::up((const uint32_t*)pixels);
    image->draw_image.resource = surface.self;  // null: built on the stack
    image->draw_image.resource_generation = surface.generation;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    EXPECT_EQ(retained_dl_cache_get(cache, 81), nullptr);  // no handle: liveness uncheckable

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
    image->draw_image.local_pixels = lam::up((const uint32_t*)pixels);
    image->draw_image.resource = lam::Handle<ImageSurface>{};
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
    filter->apply_filter.filter = lam::up((FilterProp*)&fake_filter);
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
    dl_init(&unsafe_source, replay_arena);
    int fake_filter = 1;
    int unsafe_begin = dl_begin_element(&unsafe_source, 86, 0.0f, 0.0f, 10.0f, 10.0f);
    DisplayItem* filter = dl_alloc_item(&unsafe_source);
    ASSERT_NE(filter, nullptr);
    filter->op = DL_APPLY_FILTER;
    filter->bounds[2] = 10.0f;
    filter->bounds[3] = 10.0f;
    filter->apply_filter.filter = lam::up((FilterProp*)&fake_filter);
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
    dl_init(&replay, replay_arena);
    EXPECT_FALSE(retained_dl_append_fragment_for_dirty(
        &replay, fragment, current_marker, &tracker, 1.0f,
        retained_test_contains_view_id, nullptr));
    EXPECT_EQ(replay.size(), 0u);

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
    video->video_placeholder.video = (RdtVideo*)&source;
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

    ImageSurface* surface = image_surface_alloc();
    ASSERT_NE(surface, nullptr);
    surface->width = 12;
    surface->height = 8;
    surface->generation = 3;

    int begin = dl_begin_element(&source, 90, 0.0f, 0.0f, 12.0f, 8.0f);
    DisplayItem* webview = dl_alloc_item(&source);
    ASSERT_NE(webview, nullptr);
    webview->op = DL_WEBVIEW_LAYER_PLACEHOLDER;
    webview->bounds[2] = 12.0f;
    webview->bounds[3] = 8.0f;
    webview->webview_layer_placeholder.surface = surface;
    webview->webview_layer_placeholder.resource = surface->self;
    webview->webview_layer_placeholder.surface_generation = surface->generation;
    dl_end_element(&source, begin);

    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);

    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 90);
    ASSERT_NE(fragment, nullptr);
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    surface->generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0, 1));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
    free_test_surface(surface);
}

// M6 gate: image surfaces evicted and replaced while their retained fragments
// stay captured. An evicted surface's fragment must be rejected through its
// handle (never by reading the freed surface); a live one must still append.
TEST_F(RetainedDisplayListTest, EvictionStressRejectsEvictedSurfacesAndKeepsLiveOnes) {
    const int count = 48;
    ImageSurface* surfaces[count] = {};
    uint32_t pixels[count][4] = {};
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    ASSERT_NE(cache, nullptr);
    uint32_t seed = 12345;
    for (int round = 0; round < 40; round++) {
        DisplayList source = {};
        dl_init(&source, arena);
        int image_index[count] = {};
        for (int i = 0; i < count; i++) {
            if (!surfaces[i]) {
                surfaces[i] = image_surface_alloc();
                ASSERT_NE(surfaces[i], nullptr);
                surfaces[i]->width = surfaces[i]->height = 2;
                image_surface_bump_generation(surfaces[i]);
            }
            int begin = dl_begin_element(&source, 1000 + i, (float)i * 4.0f, 0.0f, 2.0f, 2.0f);
            image_index[i] = source.item_count();
            DisplayItem* image = dl_alloc_item(&source);
            ASSERT_NE(image, nullptr);
            image->op = DL_DRAW_IMAGE;
            image->bounds[0] = (float)i * 4.0f;
            image->bounds[2] = image->bounds[3] = 2.0f;
            surfaces[i]->pixels = pixels[i]; surfaces[i]->pitch = 8;
            image->draw_image.src_w = image->draw_image.src_h = image->draw_image.src_stride = 2;
            image->draw_image.surface_w = image->draw_image.surface_h = 2;
            image->draw_image.resource = surfaces[i]->self;
            image->draw_image.resource_generation = surfaces[i]->generation;
            dl_end_element(&source, begin);
        }
        retained_dl_cache_capture(cache, &source);
        // evict about a third of the surfaces, as a cache under pressure would
        bool evicted[count] = {};
        for (int i = 0; i < count; i++) {
            seed = seed * 1103515245u + 12345u;
            if ((seed >> 16) % 3 == 0) {
                free_test_surface(surfaces[i]);
                surfaces[i] = nullptr;
                evicted[i] = true;
            }
        }
        DisplayList replay = {};
        dl_init(&replay, replay_arena);
        for (int i = 0; i < count; i++) {
            const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 1000 + i);
            ASSERT_NE(fragment, nullptr);
            bool valid = retained_dl_fragment_resources_valid(fragment, 0, 1);
            EXPECT_EQ(valid, !evicted[i]) << "round " << round << " element " << i;
            if (valid) EXPECT_TRUE(retained_dl_append_fragment(&replay, fragment));
            // replay resolves a live owner's pixels; an evicted one draws nothing
            const DisplayItem* image_item = &source.data()[image_index[i]];
            ASSERT_EQ(image_item->op, DL_DRAW_IMAGE);
            DlResolvedImage resolved;
            ImageSurfaceReadScope read_scope;
            EXPECT_EQ(dl_draw_image_resolve(&image_item->draw_image, &resolved), !evicted[i]);
            if (!evicted[i]) EXPECT_EQ(resolved.pixels, pixels[i]);
        }
        dl_destroy(&replay);
        dl_destroy(&source);
    }
    for (int i = 0; i < count; i++) if (surfaces[i]) free_test_surface(surfaces[i]);
    retained_dl_cache_destroy(cache);
}

// image_surface_destroy's two steps (it lives in surface.cpp, which this test
// does not link): queue while a read scope is open, otherwise release now
static void retire_test_surface(ImageSurface* surface) {
    if (!image_surface_defer_release(surface)) image_surface_release_now(surface);
}

static ImageSurface* alloc_test_surface_with_pixels(uint32_t value) {
    ImageSurface* surface = image_surface_alloc();
    if (!surface) return nullptr;
    surface->owned_pixels = lam::own_arr((uint8_t*)mem_alloc(4 * sizeof(uint32_t), MEM_CAT_IMAGE));
    if (!surface->owned_pixels) { image_surface_release_now(surface); return nullptr; }
    surface->pixels = surface->owned_pixels;
    for (int i = 0; i < 4; i++) ((uint32_t*)surface->pixels)[i] = value;
    surface->width = surface->height = 2; surface->pitch = 8;
    return surface;
}

// A surface destroyed inside an open read scope stays readable through a
// pointer resolved earlier, while new lookups already see it gone.
TEST_F(RetainedDisplayListTest, DestroyInsideReadScopeDefersRelease) {
    ImageSurface* surface = alloc_test_surface_with_pixels(0xff112233u);
    ASSERT_NE(surface, nullptr);
    lam::Handle<ImageSurface> handle = surface->self;
    {
        ImageSurfaceReadScope read_scope;
        ImageSurface* resolved = image_surface_lookup(handle);
        ASSERT_EQ(resolved, surface);
        retire_test_surface(surface);
        EXPECT_EQ(image_surface_lookup(handle), nullptr);
        // still intact until the scope closes (AddressSanitizer would flag a free)
        EXPECT_EQ(((uint32_t*)resolved->pixels)[3], 0xff112233u);
    }
    EXPECT_EQ(image_surface_lookup(handle), nullptr);
}

// M6 gate: surfaces are destroyed and replaced while reader threads, like tile
// workers, resolve handles and read pixels inside their read scopes. Under
// AddressSanitizer any use of a released surface fails the run.
TEST_F(RetainedDisplayListTest, EvictionWhileReadersReplayIsSafe) {
    const int count = 32;
    static lam::Handle<ImageSurface> handles[count];
    ImageSurface* surfaces[count] = {};
    for (int i = 0; i < count; i++) {
        surfaces[i] = alloc_test_surface_with_pixels(0xff000000u | (uint32_t)i);
        ASSERT_NE(surfaces[i], nullptr);
        __atomic_store_n(&handles[i].index, surfaces[i]->self.index, __ATOMIC_RELAXED);
        __atomic_store_n(&handles[i].gen, surfaces[i]->self.gen, __ATOMIC_RELEASE);
    }
    static int stop = 0;
    static uint64_t reads = 0, observed_checksum = 0;
    struct ReaderStart {
        pthread_mutex_t mutex;
        pthread_cond_t condition;
        int ready;
        bool evicting;
    } start = {};
    ASSERT_EQ(pthread_mutex_init(&start.mutex, nullptr), 0);
    ASSERT_EQ(pthread_cond_init(&start.condition, nullptr), 0);
    __atomic_store_n(&stop, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&reads, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&observed_checksum, (uint64_t)0, __ATOMIC_RELEASE);
    auto reader = [](void* opaque) -> void* {
        ReaderStart* start = (ReaderStart*)opaque;
        image_surface_mark_render_worker_thread();
        uint64_t local = 0, checksum = 0;
        bool first = true;
        do {
            ImageSurfaceReadScope read_scope;
            ImageSurface* pinned[32] = {};
            for (int i = 0; i < 32; i++) {
                lam::Handle<ImageSurface> handle;
                handle.gen = __atomic_load_n(&handles[i].gen, __ATOMIC_ACQUIRE);
                handle.index = __atomic_load_n(&handles[i].index, __ATOMIC_RELAXED);
                pinned[i] = image_surface_lookup(handle);
            }
            if (first) {
                // pin before eviction, then read after it starts; scheduler speed cannot remove the overlap.
                pthread_mutex_lock(&start->mutex);
                start->ready++;
                pthread_cond_broadcast(&start->condition);
                while (!start->evicting) pthread_cond_wait(&start->condition, &start->mutex);
                pthread_mutex_unlock(&start->mutex);
                first = false;
            }
            for (ImageSurface* surface : pinned) {
                if (!surface || !surface->pixels) continue;
                for (int p = 0; p < 4; p++) checksum += ((const uint32_t*)surface->pixels)[p];
                local++;
            }
        } while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE));
        __atomic_add_fetch(&reads, local, __ATOMIC_RELAXED);
        // preserve pixel reads through optimization so sanitizers exercise the retained storage.
        __atomic_add_fetch(&observed_checksum, checksum, __ATOMIC_RELAXED);
        return nullptr;
    };
    pthread_t threads[4];
    for (int t = 0; t < 4; t++) ASSERT_EQ(pthread_create(&threads[t], nullptr, reader, &start), 0);
    pthread_mutex_lock(&start.mutex);
    while (start.ready != 4) pthread_cond_wait(&start.condition, &start.mutex);
    pthread_mutex_unlock(&start.mutex);
    uint32_t seed = 777;
    for (int round = 0; round < 4000; round++) {
        seed = seed * 1103515245u + 12345u;
        int i = (int)((seed >> 16) % count); // INT_CAST_OK: bounded slot index
        ImageSurface* replacement = alloc_test_surface_with_pixels(0xff000000u | (uint32_t)round);
        ASSERT_NE(replacement, nullptr);
        ImageSurface* old = surfaces[i];
        surfaces[i] = replacement;
        __atomic_store_n(&handles[i].index, replacement->self.index, __ATOMIC_RELAXED);
        __atomic_store_n(&handles[i].gen, replacement->self.gen, __ATOMIC_RELEASE);
        retire_test_surface(old);
        if (round == 0) {
            pthread_mutex_lock(&start.mutex);
            start.evicting = true;
            pthread_cond_broadcast(&start.condition);
            pthread_mutex_unlock(&start.mutex);
        }
        // the host releases the queue at its quiet points
        if (round % 64 == 0) {
            ImageSurfaceReadScope host_scope;
        }
    }
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    for (int t = 0; t < 4; t++) pthread_join(threads[t], nullptr);
    image_surface_drain_retired();
    for (int i = 0; i < count; i++) retire_test_surface(surfaces[i]);
    image_surface_drain_retired();
    EXPECT_GT(__atomic_load_n(&reads, __ATOMIC_RELAXED), 0u);
    EXPECT_GT(__atomic_load_n(&observed_checksum, __ATOMIC_RELAXED), 0u);
    pthread_cond_destroy(&start.condition);
    pthread_mutex_destroy(&start.mutex);
}

TEST_F(RetainedDisplayListTest, NestedChunksCaptureEachCommandOnceAndBorrowedFramesOutliveCache) {
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    DisplayList source; dl_init(&source, arena);
    int markers[64];
    for (size_t i = 0; i < 64; i++) {
        markers[i] = dl_begin_element(&source, 1000 + (uint32_t)i, 0, 0, 100, 100);
        DisplayItem* rect = retained_test_add_rect(&source, 1, 2, 3, 4); ASSERT_NE(rect, nullptr);
        rect->fill_rect.color.c = 0xff002244u;
    }
    for (size_t i = 64; i > 0; i--) dl_end_element(&source, markers[i - 1]);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &source);
    RetainedDisplayListStats stats = retained_dl_cache_stats(cache);
    EXPECT_EQ(stats.captured, 64); EXPECT_EQ(stats.captured_commands, source.size());
    // retained command capacity must scale with unique commands, including unused slots.
    EXPECT_GE(counters.retained_command_capacity_bytes, source.size() * sizeof(DisplayItem));
    EXPECT_LE(counters.retained_command_capacity_bytes, source.size() * sizeof(DisplayItem) * 2);
    const RetainedDisplayListFragment* root = retained_dl_cache_get(cache, 1000); ASSERT_NE(root, nullptr);
    DisplayList frame; dl_init(&frame, replay_arena);
    ASSERT_NE(retained_test_add_rect(&frame, 0, 0, 1, 1), nullptr);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, root));
    EXPECT_TRUE(dl_validate(&frame, nullptr));
    EXPECT_EQ(frame.size(), 2u);
    EXPECT_EQ(counters.borrowed_command_bytes, 0u);
    EXPECT_EQ(counters.fragment_references, 1u);
    EXPECT_EQ(counters.fragment_commands, source.size());
    counters.retained_command_capacity_bytes = 0;
    retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &frame);
    stats = retained_dl_cache_stats(cache);
    EXPECT_EQ(stats.captured_commands, 0u); EXPECT_EQ(stats.captured, 0);
    EXPECT_EQ(stats.reused_captures, 64u);
    EXPECT_EQ(counters.retained_command_capacity_bytes, 0u);
    dl_destroy(&source); retained_dl_cache_destroy(cache);
    // active frame leases keep every chunk and its payloads alive after cache retirement.
    EXPECT_TRUE(dl_validate(&frame, nullptr));
    EXPECT_EQ(test_radiant_command(&frame, 2).fill_rect.color.c, 0xff002244u);
    dl_destroy(&frame);
}

TEST_F(RetainedDisplayListTest, FailedChunkAssemblyRollsBackCommandsAndOwnerLeases) {
    DisplayList source; dl_init(&source, arena);
    int begin = dl_begin_element(&source, 2000, 0, 0, 20, 20);
    ASSERT_NE(retained_test_add_rect(&source, 1, 2, 3, 4), nullptr);
    dl_end_element(&source, begin);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 2000); ASSERT_NE(fragment, nullptr);
    bool failed = false, succeeded = false;
    for (size_t fail_at = 0; fail_at < 4; fail_at++) {
        DisplayList frame; dl_init(&frame, replay_arena);
        memtrack_fault_inject(fail_at);
        bool appended = retained_dl_append_fragment(&frame, fragment);
        memtrack_fault_clear();
        failed |= !appended; succeeded |= appended;
        if (!appended) {
            EXPECT_TRUE(frame.empty());
            ASSERT_TRUE(retained_dl_append_fragment(&frame, fragment));
        }
        EXPECT_EQ(frame.size(), 1u); EXPECT_TRUE(dl_validate(&frame, nullptr));
        dl_destroy(&frame);
    }
    EXPECT_TRUE(failed); EXPECT_TRUE(succeeded);
    retained_dl_cache_destroy(cache); dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, StorageBudgetEvictsOldRootsInUseOrderAndPinsActiveReaders) {
    DisplayList source; dl_init(&source, arena);
    for (uint32_t id : {3001u, 3002u}) {
        int begin = dl_begin_element(&source, id, 0, 0, 20, 20);
        ASSERT_NE(retained_test_add_rect(&source, 1, 2, 3, 4), nullptr);
        dl_end_element(&source, begin);
    }
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    size_t initial = retained_dl_cache_stats(cache).charged_storage_bytes; ASSERT_GT(initial, 0u);
    const RetainedDisplayListFragment* first = retained_dl_cache_get(cache, 3001); ASSERT_NE(first, nullptr);
    DisplayList frame; dl_init(&frame, replay_arena);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, first));
    retained_dl_cache_set_storage_budget(cache, initial / 2);
    retained_dl_cache_begin_frame(cache);
    EXPECT_EQ(retained_dl_cache_get(cache, 3002), nullptr);
    EXPECT_NE(retained_dl_cache_get(cache, 3001), nullptr);
    EXPECT_LE(retained_dl_cache_stats(cache).charged_storage_bytes, initial / 2);
    retained_dl_cache_set_storage_budget(cache, 0);
    retained_dl_cache_begin_frame(cache);
    EXPECT_EQ(retained_dl_cache_get(cache, 3001), nullptr);
    EXPECT_GT(retained_dl_cache_stats(cache).charged_storage_bytes, 0u);
    EXPECT_TRUE(dl_validate(&frame, nullptr));
    dl_clear(&frame);
    EXPECT_EQ(retained_dl_cache_stats(cache).charged_storage_bytes, 0u);
    retained_dl_cache_capture(cache, &source);
    EXPECT_GT(retained_dl_cache_stats(cache).budget_rejections, 0);
    EXPECT_EQ(retained_dl_cache_get(cache, 3001), nullptr);
    retained_dl_cache_destroy(cache); dl_destroy(&frame); dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, StorageBudgetRetiresWholeNestedGraphWithoutAncestorPins) {
    DisplayList source; dl_init(&source, arena);
    int parent = dl_begin_element(&source, 3100, 0, 0, 20, 20);
    int child = dl_begin_element(&source, 3101, 0, 0, 20, 20);
    ASSERT_NE(retained_test_add_rect(&source, 1, 2, 3, 4), nullptr);
    dl_end_element(&source, child); dl_end_element(&source, parent);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    ASSERT_NE(retained_dl_cache_get(cache, 3100), nullptr);
    retained_dl_cache_set_storage_budget(cache, 0); retained_dl_cache_begin_frame(cache);
    EXPECT_EQ(retained_dl_cache_get(cache, 3100), nullptr);
    EXPECT_EQ(retained_dl_cache_get(cache, 3101), nullptr);
    EXPECT_EQ(retained_dl_cache_stats(cache).charged_storage_bytes, 0u);
    EXPECT_EQ(retained_dl_cache_stats(cache).evictions, 2);
    retained_dl_cache_destroy(cache); dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, MixedFrameRebuildsEditedCommandsAndKeepsSiblingPublication) {
    DisplayList source; dl_init(&source, arena);
    int root = dl_begin_element(&source, 8100, 0, 0, 40, 40);
    for (uint32_t id : {8101u, 8102u}) {
        int child = dl_begin_element(&source, id, 0, 0, 20, 20);
        ASSERT_NE(retained_test_add_rect(&source, 0, 0, 20, 20, id == 8101 ? 0xff0000ffu : 0xffff0000u), nullptr);
        dl_end_element(&source, child);
    }
    dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    DisplayList frame; dl_init(&frame, replay_arena);
    root = dl_begin_element(&frame, 8100, 0, 0, 40, 40);
    ASSERT_NE(retained_test_add_rect(&frame, 0, 0, 40, 40, 0xff00ff00u), nullptr);
    int edited = dl_begin_element(&frame, 8101, 0, 0, 20, 20);
    ASSERT_NE(retained_test_add_rect(&frame, 0, 0, 20, 20, 0xff00ffffu), nullptr); dl_end_element(&frame, edited);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 8102)));
    DisplayPayloadOwner* stable = frame.back().draw_fragment.owner;
    dl_end_element(&frame, root);
    ASSERT_TRUE(dl_validate(&frame, nullptr));
    EXPECT_EQ(frame.size(), 7u); EXPECT_EQ(counters.borrowed_command_bytes, 0u);
    counters = {};
    retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &frame);
    EXPECT_EQ(retained_dl_cache_stats(cache).captured_commands, 6u);
    EXPECT_EQ(retained_dl_cache_stats(cache).reused_captures, 1u);
    EXPECT_EQ(counters.retained_index_publications, 2u);
    EXPECT_EQ(counters.retained_index_prune_checks, 1u);
    DisplayList snapshot;
    ASSERT_TRUE(retained_dl_append_fragment(&snapshot, retained_dl_cache_get(cache, 8100)));
    DisplayList sibling;
    ASSERT_TRUE(retained_dl_append_fragment(&sibling, retained_dl_cache_get(cache, 8102)));
    EXPECT_EQ(sibling.front().draw_fragment.owner.get(), stable);
    dl_destroy(&sibling); dl_destroy(&frame); dl_destroy(&source); retained_dl_cache_destroy(cache);
    ASSERT_TRUE(dl_validate(&snapshot, nullptr));
    const uint32_t expected[] = {0xff00ff00u, 0xff00ffffu, 0xffff0000u};
    size_t painted = 0;
    DisplayListCursor cursor(&snapshot);
    while (const DisplayItem* item = cursor.next()) if (item->op == DL_FILL_RECT) {
        ASSERT_LT(painted, 3u); EXPECT_EQ(item->fill_rect.color.c, expected[painted++]);
    }
    EXPECT_FALSE(cursor.failed); EXPECT_EQ(painted, 3u);
    dl_destroy(&snapshot);
}

TEST_F(RetainedDisplayListTest, CrossCacheCaptureRehomesTheBudgetAndRepeatedReferencesStayOrdered) {
    DisplayList source; dl_init(&source, arena);
    int marker = dl_begin_element(&source, 8200, 0, 0, 20, 20);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 20, 20, 0xff123456u), nullptr); dl_end_element(&source, marker);
    RetainedDisplayListCache* original = retained_dl_cache_create(pool); ASSERT_NE(original, nullptr);
    retained_dl_cache_capture(original, &source);
    DisplayList frame; dl_init(&frame, replay_arena);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(original, 8200)));
    DisplayPayloadOwner* first = frame.front().draw_fragment.owner;
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(original, 8200)));
    ASSERT_TRUE(dl_validate(&frame, nullptr)); EXPECT_EQ(frame.size(), 2u);
    RetainedDisplayListCache* adopted = retained_dl_cache_create(pool); ASSERT_NE(adopted, nullptr);
    retained_dl_cache_capture(adopted, &frame);
    EXPECT_GT(retained_dl_cache_stats(adopted).charged_storage_bytes, 0u);
    DisplayList snapshot;
    ASSERT_TRUE(retained_dl_append_fragment(&snapshot, retained_dl_cache_get(adopted, 8200)));
    EXPECT_NE(snapshot.front().draw_fragment.owner.get(), first);
    dl_destroy(&frame); dl_destroy(&source); retained_dl_cache_destroy(original);
    retained_dl_cache_destroy(adopted);
    ASSERT_TRUE(dl_validate(&snapshot, nullptr));
    EXPECT_EQ(test_radiant_command(&snapshot, 1).fill_rect.color.c, 0xff123456u);
    dl_destroy(&snapshot);
}

TEST_F(RetainedDisplayListTest, ForeignLeafSubscenesKeepDescriptorsAndProducerIdentityDuringParentCapture) {
    DisplayList source, frame, assembled;
    int begin = dl_begin_element(&source, 0, 0, 0, 128, 10);
    for (uint32_t i = 0; i < 128; i++)
        ASSERT_NE(retained_test_add_rect(&source, (float)i, 0, 1, 10, 0xff123456u), nullptr);
    dl_end_element(&source, begin);
    RetainedDisplayListCache* producer = retained_dl_cache_create(pool); ASSERT_NE(producer, nullptr);
    RetainedDisplayListCache* consumer = retained_dl_cache_create(pool); ASSERT_NE(consumer, nullptr);
    const auto* published = retained_dl_cache_capture_fragment(producer, &source, 0, source.item_count(), 49000);
    ASSERT_NE(published, nullptr);
    begin = dl_begin_element(&frame, 49000, 0, 0, 128, 10);
    ASSERT_NE(retained_test_add_rect(&frame, 0, 0, 1, 1, 0xff000001u), nullptr);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, published));
    const DisplayPayloadOwner* original = frame.back().draw_fragment.owner;
    const DisplayItem* descriptors[128];
    for (size_t i = 0; i < 128; i++) descriptors[i] = &original->commands->data()[i + 1];
    ASSERT_TRUE(retained_dl_append_fragment(&frame, published));
    ASSERT_NE(retained_test_add_rect(&frame, 0, 0, 1, 1, 0xff000002u), nullptr);
    dl_end_element(&frame, begin);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    DisplayResourceSummary* resources = nullptr;
    ASSERT_TRUE(retained_dl_cache_capture_resources(consumer, &frame, frame.item_count(), &resources, &assembled));
    EXPECT_EQ(retained_dl_cache_stats(consumer).captured_commands, 4u);
    EXPECT_EQ(counters.command_copy_bytes, 4 * sizeof(DisplayItem));
    EXPECT_EQ(counters.retained_foreign_chunks, 2u);
    EXPECT_EQ(retained_dl_cache_get(producer, 49000), published);
    ASSERT_NE(retained_dl_cache_get(consumer, 49000), nullptr);
    EXPECT_EQ(retained_dl_fragment_item_count(retained_dl_cache_get(consumer, 49000)), 264);
    EXPECT_GE(retained_dl_cache_stats(consumer).charged_storage_bytes, 2 * original->retained_bytes);
    counters = {};
    retained_dl_cache_begin_frame(consumer); retained_dl_cache_capture(consumer, &assembled);
    EXPECT_EQ(retained_dl_cache_stats(consumer).captured_commands, 0u);
    EXPECT_EQ(counters.retained_membership_nodes, 1u);
    EXPECT_EQ(counters.retained_foreign_chunks, 0u);
    retained_dl_cache_set_storage_budget(consumer, 0); retained_dl_cache_begin_frame(consumer);
    EXPECT_EQ(retained_dl_cache_get(consumer, 49000), nullptr);
    EXPECT_EQ(retained_dl_cache_get(producer, 49000), published);
    EXPECT_EQ(retained_dl_fragment_item_count(published), 130);
    source.data()[1].fill_rect.color.c = 0xffabcdefu;
    ASSERT_NE(retained_dl_cache_capture_fragment(producer, &source, 0, source.item_count(), 49000), nullptr);
    dl_resource_summary_destroy(resources);
    dl_destroy(&frame); dl_destroy(&source);
    retained_dl_cache_destroy(producer); retained_dl_cache_destroy(consumer);
    ASSERT_TRUE(dl_validate(&assembled, nullptr));
    DisplayListCursor cursor(&assembled);
    size_t seen = 0;
    while (const DisplayItem* item = cursor.next()) if (item->op == DL_FILL_RECT) {
        if (seen == 0 || seen == 257) EXPECT_EQ(item->fill_rect.color.c, seen == 0 ? 0xff000001u : 0xff000002u);
        else {
            EXPECT_EQ(cursor.owner, original);
            EXPECT_EQ(item, descriptors[(seen - 1) % 128]);
            EXPECT_EQ(item->fill_rect.color.c, 0xff123456u);
        }
        seen++;
    }
    EXPECT_FALSE(cursor.failed); EXPECT_EQ(seen, 258u);
    dl_destroy(&assembled);
}

TEST_F(RetainedDisplayListTest, ForeignLeafLeasesKeepBothBudgetsAndResourceDependenciesAfterEviction) {
    ImageSurface* surface = image_surface_alloc(); ASSERT_NE(surface, nullptr);
    surface->width = 2; surface->height = 2; surface->generation = 9;
    DisplayList source, frame, assembled;
    int begin = dl_begin_element(&source, 0, 0, 0, 2, 2);
    DisplayItem* image = dl_alloc_item(&source); ASSERT_NE(image, nullptr);
    image->op = DL_DRAW_IMAGE;
    image->draw_image.resource = surface->self; image->draw_image.resource_generation = surface->generation;
    image->draw_image.src_w = image->draw_image.src_h = image->draw_image.src_stride = 2;
    image->draw_image.dst_w = image->draw_image.dst_h = 2;
    dl_end_element(&source, begin);
    auto* producer = retained_dl_cache_create(pool); ASSERT_NE(producer, nullptr);
    auto* consumer = retained_dl_cache_create(pool); ASSERT_NE(consumer, nullptr);
    const auto* leaf = retained_dl_cache_capture_fragment(producer, &source, 0, source.item_count(), 49001);
    ASSERT_NE(leaf, nullptr);
    size_t producer_bytes = retained_dl_cache_stats(producer).charged_storage_bytes;
    begin = dl_begin_element(&frame, 49002, 0, 0, 2, 2);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, leaf)); dl_end_element(&frame, begin);
    DisplayResourceSummary* resources = nullptr;
    ASSERT_TRUE(retained_dl_cache_capture_resources(consumer, &frame, frame.item_count(), &resources, &assembled));
    ASSERT_NE(resources, nullptr);
    EXPECT_GT(retained_dl_cache_stats(consumer).charged_storage_bytes, producer_bytes);
    EXPECT_EQ(retained_dl_cache_get(consumer, 49001), nullptr);
    EXPECT_TRUE(dl_resource_summary_current(resources, 0, 1));
    dl_destroy(&frame); dl_destroy(&source);
    retained_dl_cache_set_storage_budget(producer, 0); retained_dl_cache_begin_frame(producer);
    EXPECT_EQ(retained_dl_cache_get(producer, 49001), nullptr);
    EXPECT_EQ(retained_dl_cache_stats(producer).evictions, 1);
    EXPECT_EQ(retained_dl_cache_stats(producer).charged_storage_bytes, producer_bytes);
    retained_dl_cache_set_storage_budget(consumer, 0); retained_dl_cache_begin_frame(consumer);
    EXPECT_EQ(retained_dl_cache_get(consumer, 49002), nullptr);
    EXPECT_GT(retained_dl_cache_stats(consumer).charged_storage_bytes, producer_bytes);
    EXPECT_TRUE(dl_resources_current(&assembled, 0, 1));
    surface->generation++;
    EXPECT_FALSE(dl_resource_summary_current(resources, 0, 1));
    EXPECT_FALSE(dl_resources_current(&assembled, 0, 1));
    free_test_surface(surface);
    EXPECT_FALSE(dl_resources_current(&assembled, 0, 1));
    dl_destroy(&assembled);
    EXPECT_EQ(retained_dl_cache_stats(producer).charged_storage_bytes, 0u);
    EXPECT_EQ(retained_dl_cache_stats(consumer).charged_storage_bytes, 0u);
    retained_dl_cache_destroy(consumer); retained_dl_cache_destroy(producer);
    EXPECT_FALSE(dl_resource_summary_current(resources, 0, 1));
    dl_resource_summary_destroy(resources);
}

TEST_F(RetainedDisplayListTest, ForeignLeafAdmissionFailurePreservesSourceAndBalancesLeases) {
    DisplayList source, frame;
    int begin = dl_begin_element(&source, 0, 0, 0, 2, 2);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 2, 2), nullptr); dl_end_element(&source, begin);
    auto* producer = retained_dl_cache_create(pool); ASSERT_NE(producer, nullptr);
    const auto* leaf = retained_dl_cache_capture_fragment(producer, &source, 0, source.item_count(), 49003);
    ASSERT_NE(leaf, nullptr);
    begin = dl_begin_element(&frame, 49004, 0, 0, 2, 2);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, leaf));
    const auto* owner = frame.back().draw_fragment.owner.get(); dl_end_element(&frame, begin);
    int32_t original_refs = ref_count_get(&owner->refs);
    bool failed = false, succeeded = false;
    for (size_t fail_at = 0; fail_at < 24; fail_at++) {
        auto* consumer = retained_dl_cache_create(pool); ASSERT_NE(consumer, nullptr);
        DisplayList assembled; DisplayResourceSummary* resources = nullptr;
        RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
        memtrack_fault_inject(fail_at);
        bool captured = retained_dl_cache_capture_resources(consumer, &frame, frame.item_count(), &resources, &assembled);
        memtrack_fault_clear();
        failed |= !captured; succeeded |= captured;
        if (captured) { EXPECT_EQ(counters.command_copy_bytes, 2 * sizeof(DisplayItem)); EXPECT_TRUE(dl_validate(&assembled, nullptr)); }
        else { EXPECT_TRUE(assembled.empty()); EXPECT_EQ(resources, nullptr); }
        dl_resource_summary_destroy(resources); dl_destroy(&assembled); retained_dl_cache_destroy(consumer);
        EXPECT_EQ(ref_count_get(&owner->refs), original_refs);
        EXPECT_EQ(retained_dl_cache_get(producer, 49003), leaf);
        EXPECT_TRUE(dl_validate(&frame, nullptr));
    }
    EXPECT_TRUE(failed); EXPECT_TRUE(succeeded);
    auto* consumer = retained_dl_cache_create(pool); ASSERT_NE(consumer, nullptr);
    retained_dl_cache_set_storage_budget(consumer, owner->retained_bytes);
    DisplayList assembled; DisplayResourceSummary* resources = nullptr;
    EXPECT_FALSE(retained_dl_cache_capture_resources(consumer, &frame, frame.item_count(), &resources, &assembled));
    EXPECT_TRUE(assembled.empty()); EXPECT_EQ(resources, nullptr);
    EXPECT_GT(retained_dl_cache_stats(consumer).budget_rejections, 0);
    EXPECT_EQ(ref_count_get(&owner->refs), original_refs);
    retained_dl_cache_set_storage_budget(consumer, (size_t)32 << 20);
    ASSERT_TRUE(retained_dl_cache_capture_resources(consumer, &frame, frame.item_count(), &resources, &assembled));
    retained_dl_cache_destroy(consumer); retained_dl_cache_destroy(producer);
    dl_destroy(&source); dl_destroy(&frame);
    EXPECT_TRUE(dl_validate(&assembled, nullptr)); dl_destroy(&assembled); dl_resource_summary_destroy(resources);
}

TEST_F(RetainedDisplayListTest, ForeignNestedSubscenesKeepLocalDescendantBindings) {
    DisplayList source, frame;
    int outer = dl_begin_element(&source, 0, 0, 0, 8, 8);
    int inner = dl_begin_element(&source, 49005, 0, 0, 8, 8);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 8, 8), nullptr);
    dl_end_element(&source, inner); dl_end_element(&source, outer);
    auto* producer = retained_dl_cache_create(pool); ASSERT_NE(producer, nullptr);
    auto* consumer = retained_dl_cache_create(pool); ASSERT_NE(consumer, nullptr);
    const auto* leaf = retained_dl_cache_capture_fragment(producer, &source, 0, source.item_count(), 49006);
    ASSERT_NE(leaf, nullptr);
    outer = dl_begin_element(&frame, 49007, 0, 0, 8, 8);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, leaf)); dl_end_element(&frame, outer);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    retained_dl_cache_capture(consumer, &frame);
    EXPECT_EQ(counters.retained_foreign_chunks, 0u);
    EXPECT_EQ(retained_dl_cache_stats(consumer).captured_commands, 7u);
    ASSERT_NE(retained_dl_cache_get(consumer, 49005), nullptr);
    DisplayList pinned;
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, retained_dl_cache_get(consumer, 49005)));
    retained_dl_cache_destroy(consumer); retained_dl_cache_destroy(producer);
    dl_destroy(&source); dl_destroy(&frame);
    EXPECT_TRUE(dl_validate(&pinned, nullptr)); dl_destroy(&pinned);
}

TEST_F(RetainedDisplayListTest, WideFrameAssemblyStoresOneReferenceWithoutDescriptorCopies) {
    DisplayList source; dl_init(&source, arena);
    int root = dl_begin_element(&source, 9000, 0, 0, 100, 100);
    for (uint32_t child = 0; child < 4096; child++) {
        int marker = dl_begin_element(&source, 9001 + child, 0, 0, 100, 100);
        ASSERT_NE(retained_test_add_rect(&source, 1, 2, 3, 4), nullptr);
        dl_end_element(&source, marker);
    }
    dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    DisplayResourceSummary* captured_snapshot = nullptr;
    ASSERT_TRUE(retained_dl_cache_capture_resources(cache, &source, source.item_count(), &captured_snapshot));
    EXPECT_EQ(captured_snapshot, nullptr);
    EXPECT_EQ(counters.resource_capture_commands, 0u);
    EXPECT_EQ(counters.resource_capture_chunks, 1u);
    ASSERT_EQ(retained_dl_cache_stats(cache).captured, 4097);
    EXPECT_EQ(counters.validation_command_checks, source.size());
    EXPECT_EQ(counters.validation_chunk_checks, 4096u);
    DisplayList frame; dl_init(&frame, replay_arena);
    counters = {};
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 9000)));
    EXPECT_EQ(frame.size(), 1u); EXPECT_LE(frame.capacity(), (size_t)DL_INITIAL_CAPACITY);
    EXPECT_EQ(counters.fragment_commands, 12290u);
    EXPECT_EQ(counters.command_copy_bytes, 0u); EXPECT_EQ(counters.borrowed_command_bytes, 0u);
    EXPECT_EQ(counters.path_copy_bytes, 0u); EXPECT_EQ(counters.payload_copy_bytes, 0u);
    counters = {};
    ASSERT_TRUE(dl_validate(&frame, nullptr));
    EXPECT_EQ(counters.validation_command_checks, 0u);
    EXPECT_EQ(counters.validation_chunk_checks, 1u);
    counters = {};
    DisplayResourceSummary* snapshot = nullptr;
    memtrack_fault_inject(0);
    bool captured = dl_resource_summary_capture(&frame, &snapshot, 0);
    memtrack_fault_clear();
    EXPECT_TRUE(captured); EXPECT_EQ(snapshot, nullptr);
    EXPECT_EQ(counters.resource_capture_commands, 0u);
    EXPECT_EQ(counters.resource_capture_chunks, 1u);
    dl_resource_summary_destroy(snapshot);
    DisplayListCursor cursor(&frame); size_t commands = 0;
    while (cursor.next()) commands++;
    EXPECT_EQ(commands, source.size()); EXPECT_FALSE(cursor.failed);
    counters = {};
    retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &frame);
    EXPECT_EQ(retained_dl_cache_stats(cache).captured_commands, 0u);
    EXPECT_EQ(retained_dl_cache_stats(cache).reused_captures, 4097u);
    EXPECT_EQ(counters.retained_membership_nodes, 1u);
    EXPECT_EQ(counters.retained_membership_groups, 1u);
    EXPECT_EQ(counters.retained_index_publications, 0u);
    EXPECT_EQ(counters.retained_index_prune_checks, 1u);
    counters = {};
    EXPECT_TRUE(retained_dl_fragment_resources_valid(retained_dl_cache_get(cache, 9000), 18, 29));
    EXPECT_EQ(counters.resource_command_checks, 0u);
    EXPECT_EQ(counters.resource_summary_checks, 1u);
    EXPECT_EQ(counters.resource_child_checks, 0u);
    EXPECT_EQ(counters.resource_surface_checks, 0u);
    counters = {};
    EXPECT_TRUE(dl_resources_current(&frame, 19, 30));
    EXPECT_EQ(counters.resource_command_checks, 0u);
    EXPECT_EQ(counters.resource_summary_checks, 1u);
    dl_destroy(&source); retained_dl_cache_destroy(cache); dl_destroy(&frame);
}

TEST_F(RetainedDisplayListTest, CertifiedChunksRespectCallerElementAndFragmentDepthLimits) {
    DisplayList source;
    int root = dl_begin_element(&source, 35000, 0, 0, 3, 3);
    int child = dl_begin_element(&source, 35001, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3), nullptr);
    dl_end_element(&source, child); dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    DisplayList pinned;
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, retained_dl_cache_get(cache, 35000)));
    const DisplayPayloadOwner* owner = pinned.front().draw_fragment.owner;
    const DisplayChunkValidation* validation = retained_dl_chunk_validation(owner);
    ASSERT_NE(validation, nullptr);
    EXPECT_EQ(validation->max_element_depth, 2); EXPECT_EQ(validation->max_fragment_depth, 2);
    dl_destroy(&source); retained_dl_cache_destroy(cache);

    DisplayList frame;
    for (int depth = DL_VALIDATE_ELEMENT_STACK_LIMIT - 2; depth < DL_VALIDATE_ELEMENT_STACK_LIMIT; depth++) {
        int markers[DL_VALIDATE_ELEMENT_STACK_LIMIT];
        for (int i = 0; i < depth; i++) markers[i] = dl_begin_element(&frame, 0, 0, 0, 3, 3);
        ASSERT_TRUE(dl_copy_range(&frame, &pinned, 0, 0));
        for (int i = depth - 1; i >= 0; i--) dl_end_element(&frame, markers[i]);
        DisplayListValidationResult result;
        EXPECT_EQ(dl_validate(&frame, &result), depth == DL_VALIDATE_ELEMENT_STACK_LIMIT - 2);
        if (!result.valid) EXPECT_STREQ(result.message.get(), "element marker nesting is too deep");
        dl_clear(&frame);
    }

    // foreign wrappers add graph depth without adding element scopes.
    DisplayPayloadOwner wrappers[DL_VALIDATE_ELEMENT_STACK_LIMIT];
    DisplayCommandSpan spans[DL_VALIDATE_ELEMENT_STACK_LIMIT];
    for (int i = 0; i < DL_VALIDATE_ELEMENT_STACK_LIMIT; i++) {
        ref_count_init(&wrappers[i].refs);
        wrappers[i].destroy = [](DisplayPayloadOwner*) {};
        wrappers[i].commands = owner->commands;
        wrappers[i].command_spans = &spans[i]; wrappers[i].span_count = 1;
        wrappers[i].command_count = owner->command_count;
    }
    for (int depth = DL_VALIDATE_ELEMENT_STACK_LIMIT - 2; depth < DL_VALIDATE_ELEMENT_STACK_LIMIT; depth++) {
        for (int i = 0; i < depth; i++)
            spans[i] = {i + 1 < depth ? &wrappers[i + 1] : const_cast<DisplayPayloadOwner*>(owner), 0, 0};
        ASSERT_TRUE(dl_reference_fragment(&frame, &wrappers[0], {0, 0, 3, 3}));
        DisplayListValidationResult result;
        EXPECT_EQ(dl_validate(&frame, &result), depth == DL_VALIDATE_ELEMENT_STACK_LIMIT - 2);
        if (!result.valid) EXPECT_STREQ(result.message.get(), "fragment graph is invalid or cyclic");
        DisplayResourceSummary* snapshot = nullptr;
        EXPECT_EQ(dl_resource_summary_capture(&frame, &snapshot), depth == DL_VALIDATE_ELEMENT_STACK_LIMIT - 2);
        dl_resource_summary_destroy(snapshot);
        dl_clear(&frame);
    }
    // the cursor must still check a foreign wrapper after skipping its certified child.
    spans[0] = {const_cast<DisplayPayloadOwner*>(owner), 0, 0};
    wrappers[0].command_count++;
    ASSERT_TRUE(dl_reference_fragment(&frame, &wrappers[0], {0, 0, 3, 3}));
    EXPECT_FALSE(dl_validate(&frame, nullptr));
    DisplayResourceSummary* snapshot = nullptr;
    EXPECT_FALSE(dl_resource_summary_capture(&frame, &snapshot));
    dl_resource_summary_destroy(snapshot);
    dl_destroy(&frame);
    for (int i = 0; i < DL_VALIDATE_ELEMENT_STACK_LIMIT; i++) dl_payload_owner_release(&wrappers[i]);
    dl_destroy(&pinned);
}

TEST_F(RetainedDisplayListTest, CertifiedChunksCannotHideInvalidFreshCommandsOrPublishFailedProperties) {
    DisplayList source;
    int marker = dl_begin_element(&source, 35200, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3), nullptr); dl_end_element(&source, marker);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    DisplayList frame;
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 35200)));
    ASSERT_NE(retained_test_add_rect(&frame, 0, 0, -1, 3), nullptr);
    DisplayListProperties properties{true, true}; DisplayListValidationResult result;
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    EXPECT_FALSE(dl_validate_range(&frame, 0, frame.item_count(), &result, &properties));
    EXPECT_EQ(result.first_error_index, 1); EXPECT_STREQ(result.message.get(), "fill rect has negative size");
    EXPECT_FALSE(properties.has_glyphs); EXPECT_FALSE(properties.has_video);
    EXPECT_EQ(counters.validation_command_checks, 1u); EXPECT_EQ(counters.validation_chunk_checks, 1u);
    dl_destroy(&frame); retained_dl_cache_destroy(cache); dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, ReorderedIndexReusePrunesExpiredEntriesWithoutStagingAllocation) {
    DisplayList source;
    for (uint32_t id = 38001; id <= 38004; id++) {
        int marker = dl_begin_element(&source, id, 0, 0, 3, 3);
        ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3), nullptr);
        dl_end_element(&source, marker);
    }
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    DisplayList pinned, frame;
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, retained_dl_cache_get(cache, 38002)));
    for (uint32_t id : {38003u, 38001u})
        ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, id)));
    retained_dl_cache_begin_frame(cache);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    memtrack_fault_inject(0);
    retained_dl_cache_capture(cache, &frame);
    memtrack_fault_clear();
    EXPECT_NE(retained_dl_cache_get(cache, 38001), nullptr);
    EXPECT_NE(retained_dl_cache_get(cache, 38003), nullptr);
    EXPECT_EQ(retained_dl_cache_get(cache, 38002), nullptr);
    EXPECT_EQ(retained_dl_cache_get(cache, 38004), nullptr);
    EXPECT_EQ(counters.retained_index_publications, 0u);
    EXPECT_EQ(counters.retained_index_prune_checks, 3u);
    // an empty following frame retires both ends of the list, including its final singleton.
    DisplayList empty;
    retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &empty);
    EXPECT_EQ(retained_dl_cache_get(cache, 38001), nullptr);
    EXPECT_EQ(retained_dl_cache_get(cache, 38003), nullptr);
    dl_destroy(&source); retained_dl_cache_destroy(cache);
    EXPECT_TRUE(dl_validate(&frame, nullptr)); EXPECT_TRUE(dl_validate(&pinned, nullptr));
    dl_destroy(&frame); dl_destroy(&pinned); dl_destroy(&empty);
}

TEST_F(RetainedDisplayListTest, ReplacedAndEvictedIndexEntriesRebindPinnedPublicationsInSourceOrder) {
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    DisplayList sources[2], leases[2];
    for (unsigned generation = 0; generation < 2; generation++) {
        int marker = dl_begin_element(&sources[generation], 39000, 0, 0, 3, 3);
        ASSERT_NE(retained_test_add_rect(&sources[generation], 0, 0, 3, 3,
            generation ? 0xffff0000u : 0xff0000ffu), nullptr);
        dl_end_element(&sources[generation], marker);
        retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &sources[generation]);
        ASSERT_TRUE(retained_dl_append_fragment(&leases[generation], retained_dl_cache_get(cache, 39000)));
    }
    DisplayList frame;
    for (unsigned generation : {0u, 1u, 0u}) ASSERT_TRUE(dl_copy_range(&frame, &leases[generation], 0, 0));
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &frame);
    EXPECT_EQ(counters.retained_index_publications, 3u);
    DisplayList snapshot;
    ASSERT_TRUE(retained_dl_append_fragment(&snapshot, retained_dl_cache_get(cache, 39000)));
    EXPECT_EQ(snapshot.front().draw_fragment.owner.get(), leases[0].front().draw_fragment.owner.get());
    size_t pinned_bytes = retained_dl_cache_stats(cache).charged_storage_bytes;
    retained_dl_cache_set_storage_budget(cache, 0); retained_dl_cache_begin_frame(cache);
    EXPECT_EQ(retained_dl_cache_get(cache, 39000), nullptr);
    EXPECT_EQ(retained_dl_cache_stats(cache).charged_storage_bytes, pinned_bytes);
    retained_dl_cache_set_storage_budget(cache, pinned_bytes);
    counters = {};
    retained_dl_cache_capture(cache, &frame);
    EXPECT_EQ(counters.retained_index_publications, 3u);
    DisplayList rebound;
    ASSERT_TRUE(retained_dl_append_fragment(&rebound, retained_dl_cache_get(cache, 39000)));
    EXPECT_EQ(rebound.front().draw_fragment.owner.get(), leases[0].front().draw_fragment.owner.get());
    retained_dl_cache_destroy(cache);
    for (unsigned generation = 0; generation < 2; generation++) {
        dl_destroy(&sources[generation]); dl_destroy(&leases[generation]);
    }
    EXPECT_TRUE(dl_validate(&frame, nullptr)); EXPECT_TRUE(dl_validate(&rebound, nullptr));
    EXPECT_EQ(test_radiant_command(&rebound, 1).fill_rect.color.c, 0xff0000ffu);
    dl_destroy(&frame); dl_destroy(&snapshot); dl_destroy(&rebound);
}

TEST_F(RetainedDisplayListTest, FrameCaptureSharesExactPublicationsAndAdoptsAfterOverlayRetirement) {
    DisplayList source; dl_init(&source, arena);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3, 0xff111111u), nullptr);
    static uint8_t pixel = 255;
    for (unsigned version = 0; version < 2; version++) {
        int root = dl_begin_element(&source, 40001, 0, 0, 3, 3);
        RenderSemanticGroup semantics = {}; semantics.title = lam::up("shared frame title");
        ASSERT_TRUE(dl_copy_semantic_group(&source, &source[root].element_marker.semantics, &semantics));
        int child = dl_begin_element(&source, 40002, 0, 0, 3, 3);
        ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3,
            version ? 0xffff0000u : 0xff0000ffu), nullptr);
        DisplayItem* glyph = dl_alloc_item(&source); ASSERT_NE(glyph, nullptr);
        glyph->op = DL_DRAW_GLYPH; glyph->bounds[2] = glyph->bounds[3] = 1;
        glyph->draw_glyph.bitmap = {&pixel, 1, 1, 1};
        glyph->draw_glyph.resource_generation = 7;
        dl_end_element(&source, child); dl_end_element(&source, root);
    }
    int document_commands = source.item_count();
    int overlay = dl_begin_element(&source, 40003, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3, 0xffeeeeeeu), nullptr);
    dl_end_element(&source, overlay);
    ASSERT_TRUE(dl_validate(&source, nullptr));
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    DisplayList staged;
    DisplayResourceSummary* resources = nullptr;
    ASSERT_TRUE(retained_dl_cache_capture_resources(cache, &source, document_commands, &resources, &staged));
    ASSERT_EQ(staged.size(), 3u);
    EXPECT_EQ(staged[1].op, DL_DRAW_FRAGMENT); EXPECT_EQ(staged[2].op, DL_DRAW_FRAGMENT);
    EXPECT_NE(staged[1].draw_fragment.owner.get(), staged[2].draw_fragment.owner.get());
    EXPECT_TRUE(dl_resource_summary_current(resources, 0, 7));
    EXPECT_FALSE(dl_resource_summary_current(resources, 0, 8));
    Arena* backing = source.arena.arena;
    ValidatedDisplayList recording;
    dl_truncate(&source, document_commands);
    // adoption owns new leases before resetting the producer's registered scratch arena.
    memtrack_fault_inject(0);
    bool adopted = dl_adopt_recording(&source, &staged, &recording, "test shared full frame");
    memtrack_fault_clear();
    ASSERT_TRUE(adopted); EXPECT_EQ(source.arena.arena, backing);
    EXPECT_EQ(scratch_live_count(&source.arena), 0u);
    EXPECT_STREQ(test_radiant_command(&source, 1).element_marker.semantics.title.get(), "shared frame title");
    EXPECT_EQ(recording.get(), &source); EXPECT_TRUE(recording.content().has_glyphs);
    EXPECT_FALSE(recording.content().has_video); EXPECT_TRUE(staged.empty());
    retained_dl_cache_destroy(cache);
    const uint32_t expected[] = {0xff111111u, 0xff0000ffu, 0xffff0000u};
    size_t painted = 0;
    DisplayListCursor cursor(&source);
    while (const DisplayItem* item = cursor.next()) if (item->op == DL_FILL_RECT) {
        ASSERT_LT(painted, 3u); EXPECT_EQ(item->fill_rect.color.c, expected[painted++]);
    }
    EXPECT_FALSE(cursor.failed); EXPECT_EQ(painted, 3u);
    EXPECT_TRUE(dl_validate(&source, nullptr));
    dl_destroy(&source); dl_destroy(&staged);
    EXPECT_TRUE(dl_resource_summary_current(resources, 0, 7));
    dl_resource_summary_destroy(resources);
}

TEST_F(RetainedDisplayListTest, FrameCaptureFailurePreservesOriginalRecordingAndClearsStagingLeases) {
    DisplayList source;
    int root = dl_begin_element(&source, 41001, 0, 0, 3, 3);
    static uint8_t pixel = 255;
    DisplayItem* glyph = dl_alloc_item(&source); ASSERT_NE(glyph, nullptr);
    glyph->op = DL_DRAW_GLYPH; glyph->bounds[2] = glyph->bounds[3] = 1;
    glyph->draw_glyph.bitmap = {&pixel, 1, 1, 1}; glyph->draw_glyph.resource_generation = 7;
    dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    DisplayList pinned;
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, retained_dl_cache_get(cache, 41001)));
    DisplayPayloadOwner* owner = pinned.front().draw_fragment.owner;
    bool failed = false, succeeded = false;
    for (size_t fail_at = 0; fail_at < 6; fail_at++) {
        DisplayList staged;
        DisplayResourceSummary* resources = nullptr;
        retained_dl_cache_begin_frame(cache);
        memtrack_fault_inject(fail_at);
        bool captured = retained_dl_cache_capture_resources(cache, &pinned, pinned.item_count(), &resources, &staged);
        memtrack_fault_clear();
        failed |= !captured; succeeded |= captured;
        if (captured) {
            ASSERT_EQ(staged.size(), 1u); EXPECT_EQ(staged.front().draw_fragment.owner.get(), owner);
            EXPECT_TRUE(dl_resource_summary_current(resources, 0, 7));
        } else {
            EXPECT_TRUE(staged.empty()); EXPECT_EQ(resources, nullptr);
        }
        EXPECT_EQ(pinned.front().draw_fragment.owner.get(), owner);
        EXPECT_TRUE(dl_validate(&pinned, nullptr));
        dl_destroy(&staged); dl_resource_summary_destroy(resources);
    }
    EXPECT_TRUE(failed); EXPECT_TRUE(succeeded);
    retained_dl_cache_set_storage_budget(cache, 0);
    retained_dl_cache_begin_frame(cache);
    DisplayList staged; DisplayResourceSummary* resources = nullptr;
    EXPECT_FALSE(retained_dl_cache_capture_resources(cache, &source, source.item_count(), &resources, &staged));
    EXPECT_TRUE(staged.empty()); EXPECT_EQ(resources, nullptr); EXPECT_TRUE(dl_validate(&source, nullptr));
    dl_destroy(&source); retained_dl_cache_destroy(cache); dl_destroy(&pinned); dl_destroy(&staged);
}

TEST_F(RetainedDisplayListTest, AdoptionRejectsBorrowedScratchAndInvalidCommandsWithoutChangingDestination) {
    DisplayList source, staged; dl_init(&source, arena); dl_init(&staged, replay_arena);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3), nullptr);
    ASSERT_NE(retained_test_add_rect(&staged, 0, 0, 3, 3), nullptr);
    ValidatedDisplayList recording;
    ASSERT_TRUE(dl_validate_recording(&source, source.item_count(), &recording, "test old recording"));
    DisplayItem* original = source.data();
    EXPECT_FALSE(dl_adopt_recording(&source, &staged, &recording, "test scratch rejection"));
    EXPECT_EQ(source.data(), original); EXPECT_EQ(recording.get(), &source); EXPECT_EQ(staged.size(), 1u);
    dl_destroy(&staged);
    // the arena-free staging list can still fail validation before the old recording is retired.
    staged.arena = {};
    ASSERT_NE(retained_test_add_rect(&staged, 0, 0, -1, 3), nullptr);
    EXPECT_FALSE(dl_adopt_recording(&source, &staged, &recording, "test invalid staging"));
    EXPECT_EQ(source.data(), original); EXPECT_EQ(recording.get(), &source);
    dl_destroy(&source); dl_destroy(&staged);
}

TEST_F(RetainedDisplayListTest, UnmarkedScratchPayloadKeepsFlatCaptureFallback) {
    DisplayList source; dl_init(&source, arena);
    DisplayItem* fill = dl_alloc_item(&source); ASSERT_NE(fill, nullptr);
    fill->op = DL_FILL_SURFACE_RECT; fill->bounds[2] = fill->bounds[3] = 3;
    ClipShape clip = {}; clip.type = CLIP_SHAPE_INSET;
    ClipShape* clips[] = {&clip};
    ASSERT_TRUE(dl_store_clip_shapes(&source, &fill->fill_surface_rect.clip_shapes, clips, 1));
    int root = dl_begin_element(&source, 42001, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3), nullptr); dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    DisplayItem* original = source.data();
    DisplayList staged; DisplayResourceSummary* resources = nullptr;
    EXPECT_FALSE(retained_dl_cache_capture_resources(cache, &source, source.item_count(), &resources, &staged));
    EXPECT_TRUE(staged.empty()); EXPECT_EQ(resources, nullptr); EXPECT_EQ(source.data(), original);
    EXPECT_EQ(source.front().fill_surface_rect.clip_shapes->depth, 1);
    EXPECT_TRUE(dl_validate(&source, nullptr));
    EXPECT_NE(retained_dl_cache_get(cache, 42001), nullptr);
    EXPECT_TRUE(dl_resource_summary_capture(&source, &resources));
    dl_resource_summary_destroy(resources);
    retained_dl_cache_destroy(cache); dl_destroy(&source); dl_destroy(&staged);
}

TEST_F(RetainedDisplayListTest, ReusingNestedGroupPrunesAncestorsWithoutVisitingRetainedDescendants) {
    DisplayList source;
    int markers[64];
    for (unsigned i = 0; i < 64; i++) markers[i] = dl_begin_element(&source, 43000 + i, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3), nullptr);
    for (unsigned i = 64; i > 0; i--) dl_end_element(&source, markers[i - 1]);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    DisplayList full, child;
    ASSERT_TRUE(retained_dl_append_fragment(&full, retained_dl_cache_get(cache, 43000)));
    ASSERT_TRUE(retained_dl_append_fragment(&child, retained_dl_cache_get(cache, 43032)));
    retained_dl_cache_begin_frame(cache);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    memtrack_fault_inject(0);
    retained_dl_cache_capture(cache, &child);
    memtrack_fault_clear();
    EXPECT_EQ(counters.retained_membership_nodes, 1u);
    EXPECT_EQ(counters.retained_membership_groups, 1u);
    EXPECT_EQ(counters.retained_index_prune_checks, 33u);
    EXPECT_EQ(retained_dl_cache_stats(cache).reused_captures, 32u);
    for (unsigned i = 0; i < 64; i++)
        EXPECT_EQ(retained_dl_cache_get(cache, 43000 + i) != nullptr, i >= 32);
    retained_dl_cache_destroy(cache); dl_destroy(&source);
    EXPECT_TRUE(dl_validate(&full, nullptr)); EXPECT_TRUE(dl_validate(&child, nullptr));
    dl_destroy(&full); dl_destroy(&child);
}

TEST_F(RetainedDisplayListTest, MixedCaptureKeepsWideGroupCurrentWhileEvictingItsOldParent) {
    DisplayList source;
    int root = dl_begin_element(&source, 44000, 0, 0, 3, 3);
    int edited = dl_begin_element(&source, 44001, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3, 0xff00ff00u), nullptr);
    dl_end_element(&source, edited);
    int stable = dl_begin_element(&source, 44002, 0, 0, 3, 3);
    for (unsigned i = 0; i < 256; i++) {
        int child = dl_begin_element(&source, 44003 + i, 0, 0, 3, 3);
        ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3, 0xff0000ffu), nullptr);
        dl_end_element(&source, child);
    }
    dl_end_element(&source, stable); dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    size_t storage = retained_dl_cache_stats(cache).charged_storage_bytes;
    DisplayList frame;
    root = dl_begin_element(&frame, 44000, 0, 0, 3, 3);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 44002)));
    edited = dl_begin_element(&frame, 44001, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&frame, 0, 0, 3, 3, 0xffff0000u), nullptr);
    dl_end_element(&frame, edited); dl_end_element(&frame, root);
    // one replacement forces eviction while descendants inherit the unchanged group's current epoch.
    retained_dl_cache_set_storage_budget(cache, storage);
    retained_dl_cache_begin_frame(cache);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    retained_dl_cache_capture(cache, &frame);
    EXPECT_EQ(counters.retained_membership_nodes, 1u);
    EXPECT_EQ(counters.retained_membership_groups, 1u);
    EXPECT_EQ(retained_dl_cache_stats(cache).reused_captures, 257u);
    EXPECT_EQ(retained_dl_cache_stats(cache).captured, 2);
    EXPECT_GT(retained_dl_cache_stats(cache).evictions, 0);
    for (unsigned i = 0; i < 259; i++) EXPECT_NE(retained_dl_cache_get(cache, 44000 + i), nullptr);
    DisplayList pinned;
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, retained_dl_cache_get(cache, 44000)));
    dl_destroy(&source); dl_destroy(&frame); retained_dl_cache_destroy(cache);
    EXPECT_TRUE(dl_validate(&pinned, nullptr));
    size_t painted = 0;
    DisplayListCursor cursor(&pinned);
    while (const DisplayItem* item = cursor.next()) if (item->op == DL_FILL_RECT) {
        EXPECT_EQ(item->fill_rect.color.c, painted < 256 ? 0xff0000ffu : 0xffff0000u);
        painted++;
    }
    EXPECT_FALSE(cursor.failed); EXPECT_EQ(painted, 257u);
    dl_destroy(&pinned);
}

TEST_F(RetainedDisplayListTest, SharedDescendantInvalidatesEnclosingGroupsAndKeepsSourceOrder) {
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    DisplayList first;
    int parent = dl_begin_element(&first, 45001, 0, 0, 3, 3);
    int leaf = dl_begin_element(&first, 45003, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&first, 0, 0, 3, 3), nullptr);
    dl_end_element(&first, leaf); dl_end_element(&first, parent);
    retained_dl_cache_capture(cache, &first);
    DisplayList second;
    parent = dl_begin_element(&second, 45002, 0, 0, 3, 3);
    ASSERT_TRUE(retained_dl_append_fragment(&second, retained_dl_cache_get(cache, 45003)));
    dl_end_element(&second, parent);
    // the second publication shares the exact leaf owner rather than cloning its commands.
    retained_dl_cache_capture(cache, &second);
    DisplayList frame;
    parent = dl_begin_element(&frame, 45000, 0, 0, 3, 3);
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 45001)));
    ASSERT_TRUE(retained_dl_append_fragment(&frame, retained_dl_cache_get(cache, 45002)));
    dl_end_element(&frame, parent);
    retained_dl_cache_capture(cache, &frame);
    DisplayList pinned;
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, retained_dl_cache_get(cache, 45000)));
    retained_dl_cache_begin_frame(cache);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    retained_dl_cache_capture(cache, &pinned);
    EXPECT_EQ(counters.retained_membership_nodes, 5u);
    EXPECT_EQ(counters.retained_membership_groups, 2u);
    EXPECT_EQ(counters.retained_index_publications, 0u);
    EXPECT_EQ(retained_dl_cache_stats(cache).reused_captures, 5u);
    for (unsigned i = 0; i < 4; i++) EXPECT_NE(retained_dl_cache_get(cache, 45000 + i), nullptr);
    DisplayList survivor;
    ASSERT_TRUE(retained_dl_append_fragment(&survivor, retained_dl_cache_get(cache, 45002)));
    retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &survivor);
    EXPECT_EQ(retained_dl_cache_get(cache, 45000), nullptr);
    EXPECT_EQ(retained_dl_cache_get(cache, 45001), nullptr);
    EXPECT_NE(retained_dl_cache_get(cache, 45002), nullptr);
    EXPECT_NE(retained_dl_cache_get(cache, 45003), nullptr);
    retained_dl_cache_destroy(cache);
    EXPECT_TRUE(dl_validate(&pinned, nullptr)); EXPECT_TRUE(dl_validate(&survivor, nullptr));
    dl_destroy(&first); dl_destroy(&second); dl_destroy(&frame); dl_destroy(&pinned); dl_destroy(&survivor);
}

TEST_F(RetainedDisplayListTest, ReplacingDescendantInvalidatesAncestorGroupBeforeOlderPublicationReuses) {
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    DisplayList source;
    int root = dl_begin_element(&source, 46000, 0, 0, 3, 3);
    int child = dl_begin_element(&source, 46001, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 3, 3, 0xff0000ffu), nullptr);
    dl_end_element(&source, child); dl_end_element(&source, root);
    retained_dl_cache_capture(cache, &source);
    DisplayList old_parent;
    ASSERT_TRUE(retained_dl_append_fragment(&old_parent, retained_dl_cache_get(cache, 46000)));
    DisplayList replacement;
    child = dl_begin_element(&replacement, 46001, 0, 0, 3, 3);
    ASSERT_NE(retained_test_add_rect(&replacement, 0, 0, 3, 3, 0xffff0000u), nullptr);
    dl_end_element(&replacement, child);
    // both occurrences belong to this paint epoch; replacing a child must invalidate its old ancestor.
    retained_dl_cache_capture(cache, &replacement);
    ASSERT_NE(retained_dl_cache_get(cache, 46000), nullptr);
    DisplayList new_child;
    ASSERT_TRUE(retained_dl_append_fragment(&new_child, retained_dl_cache_get(cache, 46001)));
    retained_dl_cache_begin_frame(cache);
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    retained_dl_cache_capture(cache, &old_parent);
    EXPECT_EQ(counters.retained_membership_nodes, 2u);
    EXPECT_EQ(counters.retained_membership_groups, 0u);
    EXPECT_EQ(counters.retained_index_publications, 1u);
    DisplayList current_child;
    ASSERT_TRUE(retained_dl_append_fragment(&current_child, retained_dl_cache_get(cache, 46001)));
    retained_dl_cache_destroy(cache); dl_destroy(&source); dl_destroy(&replacement);
    EXPECT_TRUE(dl_validate(&old_parent, nullptr)); EXPECT_TRUE(dl_validate(&new_child, nullptr));
    EXPECT_EQ(test_radiant_command(&old_parent, 2).fill_rect.color.c, 0xff0000ffu);
    EXPECT_EQ(test_radiant_command(&current_child, 1).fill_rect.color.c, 0xff0000ffu);
    EXPECT_EQ(test_radiant_command(&new_child, 1).fill_rect.color.c, 0xffff0000u);
    dl_destroy(&old_parent); dl_destroy(&new_child); dl_destroy(&current_child);
}

TEST_F(RetainedDisplayListTest, TranslatedNestedFragmentsKeepOriginalDescriptorsAndSurviveRetirement) {
    DisplayList source, original, placed;
    int root = dl_begin_element(&source, 47000, 1, 2, 10, 12);
    int child = dl_begin_element(&source, 47001, 3, 4, 5, 6);
    ASSERT_NE(retained_test_add_rect(&source, 3, 4, 5, 6), nullptr);
    dl_end_element(&source, child); dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const auto* fragment = retained_dl_cache_get(cache, 47000); ASSERT_NE(fragment, nullptr);
    ASSERT_TRUE(retained_dl_append_fragment_translated(&original, fragment, 0, 0));
    RenderPipelineMetrics counters; RenderPipelineMetricsScope probe(&counters);
    ASSERT_TRUE(retained_dl_append_fragment_translated(&placed, fragment, 10, -2));
    ASSERT_TRUE(retained_dl_append_fragment_translated(&placed, fragment, -4, 20));
    EXPECT_EQ(placed.size(), 2u);
    EXPECT_GT(placed.front().draw_fragment.owner->retained_bytes,
        original.front().draw_fragment.owner->retained_bytes);
    EXPECT_EQ(counters.command_copy_bytes, 0u); EXPECT_EQ(counters.payload_copy_bytes, 0u);
    EXPECT_EQ(counters.path_clones, 0u); EXPECT_GT(counters.placement_command_copy_bytes, 0u);
    retained_dl_cache_destroy(cache); dl_destroy(&source);
    EXPECT_TRUE(dl_validate(&placed, nullptr));
    DisplayListCursor canonical(&original), translated(&placed);
    for (unsigned placement = 0; placement < 2; placement++) {
        DisplayListCursor unshifted(&original);
        while (const DisplayItem* before = unshifted.next()) {
            const DisplayItem* after = translated.next(); ASSERT_NE(after, nullptr);
            EXPECT_EQ(before, &unshifted.owner->commands->data()[unshifted.source_index]);
            EXPECT_NE(after, &translated.owner->commands->data()[translated.source_index]);
            EXPECT_EQ(after->op, before->op); EXPECT_EQ(translated.index, unshifted.index);
            EXPECT_FLOAT_EQ(after->bounds[0], before->bounds[0] + (placement ? -4 : 10));
            EXPECT_FLOAT_EQ(after->bounds[1], before->bounds[1] + (placement ? 20 : -2));
            if (before->op == DL_FILL_RECT) {
                EXPECT_FLOAT_EQ(after->fill_rect.x, placement ? -1 : 13);
                EXPECT_FLOAT_EQ(after->fill_rect.y, placement ? 24 : 2);
            }
        }
        EXPECT_FALSE(unshifted.failed);
    }
    EXPECT_EQ(translated.next(), nullptr); EXPECT_FALSE(translated.failed);
    EXPECT_FLOAT_EQ(canonical.next()->element_marker.marker_x, 1);
    dl_destroy(&original); dl_destroy(&placed);
}

TEST_F(RetainedDisplayListTest, TranslatedAppendRejectsUnsupportedAndNonFinitePlacementAtomically) {
    for (bool pixel_operation : {false, true}) {
        DisplayList source, destination;
        int root = dl_begin_element(&source, 47100, 0, 0, 8, 8);
        DisplayItem* item = retained_test_add_rect(&source, 1, 2, 3, 4); ASSERT_NE(item, nullptr);
        if (pixel_operation) {
            item->op = DL_FILL_SURFACE_RECT;
            item->fill_surface_rect = {}; item->fill_surface_rect.w = item->fill_surface_rect.h = 3;
        }
        dl_end_element(&source, root);
        RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
        retained_dl_cache_capture(cache, &source);
        const auto* fragment = retained_dl_cache_get(cache, 47100); ASSERT_NE(fragment, nullptr);
        ASSERT_TRUE(retained_dl_append_fragment_translated(&destination, fragment, 0, 0));
        EXPECT_FALSE(retained_dl_append_fragment_translated(&destination, fragment, INFINITY, 0));
        EXPECT_FALSE(retained_dl_append_fragment_translated(&destination, fragment, 0, NAN));
        EXPECT_EQ(retained_dl_append_fragment_translated(&destination, fragment, 4, 5), !pixel_operation);
        EXPECT_EQ(destination.size(), pixel_operation ? 1u : 2u);
        EXPECT_TRUE(dl_validate(&destination, nullptr));
        retained_dl_cache_destroy(cache); dl_destroy(&source); dl_destroy(&destination);
    }
    DisplayList huge, destination;
    int root = dl_begin_element(&huge, 47101, FLT_MAX / 2, 0, 1, 1);
    ASSERT_NE(retained_test_add_rect(&huge, FLT_MAX / 2, 0, 1, 1), nullptr);
    dl_end_element(&huge, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &huge);
    const auto* fragment = retained_dl_cache_get(cache, 47101); ASSERT_NE(fragment, nullptr);
    EXPECT_FALSE(retained_dl_append_fragment_translated(&destination, fragment, FLT_MAX, 0));
    EXPECT_TRUE(destination.empty());
    retained_dl_cache_destroy(cache); dl_destroy(&huge); dl_destroy(&destination);
}

TEST_F(RetainedDisplayListTest, TranslatedAllocationFailureBalancesSourceLeaseAndPreservesPrefix) {
    DisplayList source, pinned;
    int root = dl_begin_element(&source, 47200, 0, 0, 8, 8);
    ASSERT_NE(retained_test_add_rect(&source, 1, 2, 3, 4), nullptr); dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    const auto* fragment = retained_dl_cache_get(cache, 47200); ASSERT_NE(fragment, nullptr);
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, fragment));
    auto* owner = pinned.front().draw_fragment.owner.get();
    int32_t references = ref_count_get(&owner->refs);
    bool saw_failure = false, saw_success = false;
    for (bool with_prefix : {false, true}) for (size_t fail_at = 0; fail_at < 5; fail_at++) {
        SCOPED_TRACE(fail_at);
        SCOPED_TRACE(with_prefix);
        DisplayList destination;
        if (with_prefix) ASSERT_NE(retained_test_add_rect(&destination, 0, 0, 1, 1), nullptr);
        memtrack_fault_inject(fail_at);
        bool ok = retained_dl_append_fragment_translated(&destination, fragment, 2, 3);
        memtrack_fault_clear();
        saw_failure |= !ok; saw_success |= ok;
        EXPECT_EQ(destination.size(), (with_prefix ? 1u : 0u) + (ok ? 1u : 0u));
        if (with_prefix) EXPECT_EQ(destination.front().op, DL_FILL_RECT);
        EXPECT_TRUE(dl_validate(&destination, nullptr));
        dl_destroy(&destination); EXPECT_EQ(ref_count_get(&owner->refs), references);
    }
    EXPECT_TRUE(saw_failure); EXPECT_TRUE(saw_success);
    retained_dl_cache_destroy(cache); dl_destroy(&source); dl_destroy(&pinned);
}

TEST_F(RetainedDisplayListTest, TranslatedRecaptureDoesNotRepublishUnshiftedSourceIdentity) {
    DisplayList source, placed;
    int root = dl_begin_element(&source, 47300, 0, 0, 8, 8);
    ASSERT_NE(retained_test_add_rect(&source, 1, 2, 3, 4), nullptr); dl_end_element(&source, root);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    retained_dl_cache_capture(cache, &source);
    ASSERT_TRUE(retained_dl_append_fragment_translated(&placed, retained_dl_cache_get(cache, 47300), 12, 16));
    retained_dl_cache_begin_frame(cache); retained_dl_cache_capture(cache, &placed);
    EXPECT_EQ(retained_dl_cache_get(cache, 47300), nullptr);
    retained_dl_cache_destroy(cache); dl_destroy(&source);
    EXPECT_TRUE(dl_validate(&placed, nullptr));
    DisplayListCursor cursor(&placed); ASSERT_NE(cursor.next(), nullptr);
    EXPECT_FLOAT_EQ(cursor.item->element_marker.marker_x, 12);
    ASSERT_NE(cursor.next(), nullptr); EXPECT_FLOAT_EQ(cursor.item->fill_rect.x, 13);
    dl_destroy(&placed);
}

TEST_F(RetainedDisplayListTest, TranslatedCursorComposesShellsAndRejectsInvalidGraphs) {
    DisplayList source;
    int root = dl_begin_element(&source, 47400, 0, 0, 8, 8);
    ASSERT_NE(retained_test_add_rect(&source, 1, 2, 3, 4), nullptr); dl_end_element(&source, root);
    DisplayPayloadOwner leaf = {}, inner = {}, outer = {};
    DisplayCommandSpan spans[] = {{nullptr, 0, 3}, {&leaf, 0, 0}, {&inner, 0, 0}};
    DisplayPayloadOwner* owners[] = {&leaf, &inner, &outer};
    for (unsigned i = 0; i < 3; i++) {
        owners[i]->commands = lam::up(&source); owners[i]->command_count = 3;
        owners[i]->command_spans = &spans[i]; owners[i]->span_count = 1;
    }
    inner.translate_x = 1; inner.translate_y = 2;
    outer.translate_x = 3; outer.translate_y = 4;
    EXPECT_TRUE(dl_validate_chunk(&outer, nullptr, nullptr));
    DisplayListCursor cursor(&outer); ASSERT_NE(cursor.next(), nullptr);
    ASSERT_NE(cursor.next(), nullptr);
    EXPECT_FLOAT_EQ(cursor.item->fill_rect.x, 5); EXPECT_FLOAT_EQ(cursor.item->fill_rect.y, 8);
    outer.translate_x = -1; outer.translate_y = -2;
    DisplayListCursor cancelled(&outer); ASSERT_NE(cancelled.next(), nullptr);
    EXPECT_EQ(cancelled.item, &source.front());
    outer.translate_x = inner.translate_x = FLT_MAX;
    EXPECT_FALSE(dl_validate_chunk(&outer, nullptr, nullptr));
    outer.translate_x = 3; inner.translate_x = 1;
    spans[1].child = &outer;
    EXPECT_FALSE(dl_validate_chunk(&outer, nullptr, nullptr));
    spans[1].child = &leaf; spans[0].count = 4;
    EXPECT_FALSE(dl_validate_chunk(&outer, nullptr, nullptr));
    DisplayPayloadOwner chain[DL_VALIDATE_ELEMENT_STACK_LIMIT] = {};
    DisplayCommandSpan links[DL_VALIDATE_ELEMENT_STACK_LIMIT];
    for (unsigned i = 0; i < DL_VALIDATE_ELEMENT_STACK_LIMIT; i++) {
        chain[i].commands = lam::up(&source); chain[i].command_count = 3;
        chain[i].command_spans = &links[i]; chain[i].span_count = 1;
        links[i] = {i + 1 < DL_VALIDATE_ELEMENT_STACK_LIMIT ? &chain[i + 1] : nullptr, 0, 3};
    }
    chain[0].translate_x = 1;
    EXPECT_TRUE(dl_validate_chunk(&chain[0], nullptr, nullptr));
    spans[2].child = &chain[0];
    EXPECT_FALSE(dl_validate_chunk(&outer, nullptr, nullptr));
    dl_destroy(&source);
}

TEST_F(RetainedDisplayListTest, KeyedSubscenesPreserveZeroMarkersAndUnrelatedPublications) {
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    DisplayList source, pinned;
    DisplayItem* prefix = retained_test_add_rect(&source, 0, 0, 1, 1); ASSERT_NE(prefix, nullptr);
    prefix->op = DL_FILL_SURFACE_RECT; prefix->fill_surface_rect = {};
    int begin = dl_begin_element(&source, 0, 3, 4, 8, 8);
    ASSERT_NE(retained_test_add_rect(&source, 3, 4, 8, 8), nullptr); dl_end_element(&source, begin);
    int end = source.item_count();
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 1, 1), nullptr);
    EXPECT_FALSE(dl_supports_vector_translation(&source));
    EXPECT_TRUE(dl_supports_vector_translation(&source, begin, end));
    EXPECT_FALSE(dl_supports_vector_translation(&source, source.item_count() + 1));
    EXPECT_FALSE(dl_supports_vector_translation(&source, begin, end + 2));
    ASSERT_NE(retained_dl_cache_capture_fragment(cache, &source, begin, end, 48000), nullptr);
    ASSERT_TRUE(retained_dl_append_fragment_translated(&pinned, retained_dl_cache_get(cache, 48000), 10, 20));
    ASSERT_NE(retained_dl_cache_capture_fragment(cache, &source, begin, end, 48001), nullptr);
    EXPECT_NE(retained_dl_cache_get(cache, 48000), nullptr);
    EXPECT_NE(retained_dl_cache_get(cache, 48001), nullptr);
    EXPECT_EQ(test_radiant_command(&pinned, 0).element_marker.view_id, 0u);
    EXPECT_EQ(test_radiant_command(&pinned, 0).element_marker.matching_index, 2);
    retained_dl_cache_destroy(cache); dl_destroy(&source);
    EXPECT_TRUE(dl_validate(&pinned, nullptr));
    EXPECT_FLOAT_EQ(test_radiant_command(&pinned, 1).fill_rect.x, 13);
    dl_destroy(&pinned);
}

TEST_F(RetainedDisplayListTest, KeyedSubscenesRejectInvalidRangesAndRecoverFromPublicationFailure) {
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool); ASSERT_NE(cache, nullptr);
    DisplayList source, pinned;
    int begin = dl_begin_element(&source, 0, 0, 0, 8, 8);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 8, 8), nullptr); dl_end_element(&source, begin);
    EXPECT_EQ(retained_dl_cache_capture_fragment(cache, &source, 0, 3, 0), nullptr);
    EXPECT_EQ(retained_dl_cache_capture_fragment(cache, &source, 1, 3, 48002), nullptr);
    EXPECT_EQ(retained_dl_cache_capture_fragment(cache, &source, 0, 2, 48002), nullptr);
    source.front().element_marker.view_id = 48002;
    EXPECT_EQ(retained_dl_cache_capture_fragment(cache, &source, 0, 3, 48002), nullptr);
    source.front().element_marker.view_id = 0;
    ASSERT_NE(retained_dl_cache_capture_fragment(cache, &source, 0, 3, 48002), nullptr);
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, retained_dl_cache_get(cache, 48002)));
    bool saw_failure = false, saw_success = false;
    for (size_t fail_at = 0; fail_at < 20; fail_at++) {
        memtrack_fault_inject(fail_at);
        const auto* fragment = retained_dl_cache_capture_fragment(cache, &source, 0, 3, 48002);
        memtrack_fault_clear();
        saw_failure |= !fragment; saw_success |= fragment != nullptr;
        EXPECT_TRUE(dl_validate(&pinned, nullptr));
    }
    EXPECT_TRUE(saw_failure); EXPECT_TRUE(saw_success);
    ASSERT_NE(retained_dl_cache_capture_fragment(cache, &source, 0, 3, 48002), nullptr);
    retained_dl_cache_set_storage_budget(cache, 1);
    EXPECT_EQ(retained_dl_cache_capture_fragment(cache, &source, 0, 3, 48002), nullptr);
    retained_dl_cache_destroy(cache); dl_destroy(&source);
    EXPECT_TRUE(dl_validate(&pinned, nullptr)); dl_destroy(&pinned);
}

TEST_F(RetainedDisplayListTest, PublishedStoragePinsItsContextBeyondDocumentAndCacheOwners) {
    MemContext* document = mem_context_create(nullptr, MEM_ROLE_RENDER, "test.retained.document");
    ASSERT_NE(document, nullptr);
    uint32_t doc_id = mem_doc_register("test:retained-reader", 0);
    mem_context_set_doc_id(document, doc_id);
    Pool* owner_pool = mem_pool_create(document, MEM_ROLE_RENDER, "test.retained.pool");
    ASSERT_NE(owner_pool, nullptr);
    RetainedDisplayListCache* cache = retained_dl_cache_create(owner_pool); ASSERT_NE(cache, nullptr);
    DisplayList source, pinned;
    int root = dl_begin_element(&source, 0, 0, 0, 8, 8);
    ASSERT_NE(retained_test_add_rect(&source, 0, 0, 8, 8), nullptr); dl_end_element(&source, root);
    ASSERT_NE(retained_dl_cache_capture_fragment(cache, &source, 0, 3, 48003), nullptr);
    ASSERT_TRUE(retained_dl_append_fragment(&pinned, retained_dl_cache_get(cache, 48003)));
    retained_dl_cache_destroy(cache); mem_pool_destroy(owner_pool); mem_context_destroy(document);
    dl_destroy(&source);
    MemSnapshot* snapshot = mem_snapshot_capture(nullptr); ASSERT_NE(snapshot, nullptr);
    bool found = false;
    for (uint32_t i = 0; i < snapshot->count; i++) {
        const MemStatSample& sample = snapshot->samples[i];
        if (sample.doc_id == doc_id && strcmp(sample.label, "retained_dl.chunk") == 0) found = true;
    }
    mem_snapshot_free(snapshot); EXPECT_TRUE(found);
    EXPECT_TRUE(dl_validate(&pinned, nullptr)); dl_destroy(&pinned);
    snapshot = mem_snapshot_capture(nullptr); ASSERT_NE(snapshot, nullptr);
    for (uint32_t i = 0; i < snapshot->count; i++) EXPECT_NE(snapshot->samples[i].doc_id, doc_id);
    mem_snapshot_free(snapshot); mem_doc_retire(doc_id);
}
