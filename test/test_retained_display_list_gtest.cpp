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
