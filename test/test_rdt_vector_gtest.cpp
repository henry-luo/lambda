#include <gtest/gtest.h>

#include "../radiant/render.hpp"

TEST(RdtVectorTest, ComposesOneClipMaskPerStableBatch) {
    rdt_engine_init(0);

    uint32_t pixels[24 * 12] = {};
    RdtVector vector = {};
    rdt_vector_init(&vector, pixels, 24, 12, 24);

    RdtPath* outer = rdt_path_new();
    RdtPath* inner = rdt_path_new();
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr);
    rdt_path_add_rect(outer, 2.0f, 2.0f, 16.0f, 8.0f, 0.0f, 0.0f);
    rdt_path_add_rect(inner, 8.0f, 2.0f, 8.0f, 8.0f, 0.0f, 0.0f);

    Color red = {};
    red.r = 255;
    red.a = 255;
    Color blue = {};
    blue.b = 255;
    blue.a = 255;

    rdt_vector_begin_batch(&vector);
    rdt_push_clip(&vector, outer, nullptr);
    // Many paints under one unchanged clip must share one composed mask.
    for (int x = 0; x < 24; x += 2) {
        rdt_fill_rect(&vector, (float)x, 0.0f, 2.0f, 12.0f, red);
    }
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 0u);
    rdt_vector_flush_batch(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 1u);
    EXPECT_NE(pixels[4 * 24 + 4], 0u);
    EXPECT_EQ(pixels[4 * 24 + 0], 0u);

    rdt_push_clip(&vector, inner, nullptr);
    rdt_fill_rect(&vector, 0.0f, 0.0f, 24.0f, 12.0f, blue);
    rdt_pop_clip(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 2u);
    EXPECT_EQ(pixels[4 * 24 + 4], pixels[4 * 24 + 6]);
    EXPECT_NE(pixels[4 * 24 + 10], pixels[4 * 24 + 4]);
    EXPECT_EQ(pixels[4 * 24 + 20], 0u);

    rdt_pop_clip(&vector);
    rdt_vector_end_batch(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 2u);

    rdt_path_free(inner);
    rdt_path_free(outer);
    rdt_vector_destroy(&vector);
    rdt_engine_term();
}
