#include <gtest/gtest.h>

#include "../radiant/display_list.h"
#include "../radiant/display_list_storage.hpp"
#include "../radiant/retained_display_list.hpp"
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
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0));

    surface.generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0));

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
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0));

    surface.generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0));

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
    ASSERT_NE(fragment, nullptr);
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0));

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
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 4));
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 5));

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
    EXPECT_TRUE(retained_dl_fragment_resources_valid(fragment, 0));

    surface.generation++;
    EXPECT_FALSE(retained_dl_fragment_resources_valid(fragment, 0));

    retained_dl_cache_destroy(cache);
    dl_destroy(&source);
}
