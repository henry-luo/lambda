#include <gtest/gtest.h>

#include "../radiant/layout_custom.hpp"
#include "../lib/arena.h"
#include "../lib/mempool.h"
#include <string.h>

class CustomLayoutTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Arena* arena = nullptr;
    LayoutContext lycon = {};
    BlockProp parent_blk = {};

    void SetUp() override {
        custom_layout_registry_clear();
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        arena = arena_create_default(pool);
        ASSERT_NE(arena, nullptr);
        scratch_init(&lycon.scratch, arena);
        lycon.block.direction = CSS_VALUE_LTR;
        parent_blk.given_width = -1.0f;
        parent_blk.given_height = -1.0f;
        parent_blk.given_min_width = -1.0f;
        parent_blk.given_max_width = -1.0f;
        parent_blk.given_min_height = -1.0f;
        parent_blk.given_max_height = -1.0f;
        parent_blk.box_sizing = CSS_VALUE_CONTENT_BOX;
    }

    void TearDown() override {
        custom_layout_registry_clear();
        scratch_release(&lycon.scratch);
        if (arena) {
            arena_destroy(arena);
            arena = nullptr;
        }
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }

    static void init_block(ViewBlock* block, const char* tag, float width, float height) {
        ASSERT_NE(block, nullptr);
        block->tag_name = tag;
        block->tag_id = DomNode::tag_name_to_id(tag);
        block->view_type = RDT_VIEW_BLOCK;
        block->width = width;
        block->height = height;
        block->content_width = width;
        block->content_height = height;
    }

    void init_parent(ViewBlock* parent, float width, float height) {
        init_block(parent, "div", width, height);
        parent->blk = &parent_blk;
    }
};

static bool custom_layout_test_negative_bounds(const CustomLayoutContext* context,
                                               CustomLayoutResult* result) {
    if (!context || !result || context->child_count != 2) return false;
    return custom_layout_result_place(result, 0, -10.0f, 5.0f) &&
        custom_layout_result_place(result, 1, 40.0f, 20.0f);
}

static bool custom_layout_test_explicit_size(const CustomLayoutContext* context,
                                             CustomLayoutResult* result) {
    if (!context || !result || context->child_count != 1) return false;
    result->baseline = 17.0f;
    result->has_baseline = true;
    return custom_layout_result_place(result, 0, 200.0f, 120.0f);
}

static bool custom_layout_test_duplicate_and_missing(const CustomLayoutContext* context,
                                                     CustomLayoutResult* result) {
    if (!context || !result || context->child_count != 2) return false;
    return custom_layout_result_place(result, 0, 12.0f, 8.0f) &&
        custom_layout_result_place(result, 0, 44.0f, 30.0f);
}

TEST_F(CustomLayoutTest, AutoParentSizeUsesPlacedChildContainingBox) {
    ViewBlock parent;
    ViewBlock first;
    ViewBlock second;
    init_parent(&parent, 0.0f, 0.0f);
    init_block(&first, "section", 30.0f, 10.0f);
    init_block(&second, "section", 20.0f, 15.0f);
    ASSERT_TRUE(parent.append_child(&first));
    ASSERT_TRUE(parent.append_child(&second));

    ASSERT_TRUE(custom_layout_register("unit-negative-bounds", custom_layout_test_negative_bounds));
    ASSERT_TRUE(layout_custom_apply(&lycon, &parent, "unit-negative-bounds"));

    EXPECT_FLOAT_EQ(first.x, -10.0f);
    EXPECT_FLOAT_EQ(first.y, 5.0f);
    EXPECT_FLOAT_EQ(second.x, 40.0f);
    EXPECT_FLOAT_EQ(second.y, 20.0f);
    EXPECT_FLOAT_EQ(parent.content_width, 70.0f);
    EXPECT_FLOAT_EQ(parent.content_height, 30.0f);
    EXPECT_FLOAT_EQ(parent.width, 70.0f);
    EXPECT_FLOAT_EQ(parent.height, 30.0f);
}

TEST_F(CustomLayoutTest, ExplicitParentSizeWinsOverPlacedChildBounds) {
    ViewBlock parent;
    ViewBlock child;
    init_parent(&parent, 123.0f, 45.0f);
    parent_blk.given_width = 123.0f;
    parent_blk.given_height = 45.0f;
    init_block(&child, "section", 20.0f, 10.0f);
    ASSERT_TRUE(parent.append_child(&child));

    ASSERT_TRUE(custom_layout_register("unit-explicit-size", custom_layout_test_explicit_size));
    ASSERT_TRUE(layout_custom_apply(&lycon, &parent, "unit-explicit-size"));

    EXPECT_FLOAT_EQ(child.x, 200.0f);
    EXPECT_FLOAT_EQ(child.y, 120.0f);
    EXPECT_FLOAT_EQ(parent.content_width, 123.0f);
    EXPECT_FLOAT_EQ(parent.content_height, 45.0f);
    EXPECT_FLOAT_EQ(parent.width, 123.0f);
    EXPECT_FLOAT_EQ(parent.height, 45.0f);
    EXPECT_FLOAT_EQ(parent_blk.first_line_baseline, 17.0f);
}

TEST_F(CustomLayoutTest, AutoParentSizeHonorsMinMaxConstraints) {
    ViewBlock parent;
    ViewBlock first;
    ViewBlock second;
    init_parent(&parent, 0.0f, 0.0f);
    parent_blk.given_min_width = 100.0f;
    parent_blk.given_max_height = 25.0f;
    init_block(&first, "section", 30.0f, 10.0f);
    init_block(&second, "section", 20.0f, 15.0f);
    ASSERT_TRUE(parent.append_child(&first));
    ASSERT_TRUE(parent.append_child(&second));

    ASSERT_TRUE(custom_layout_register("unit-min-max", custom_layout_test_negative_bounds));
    ASSERT_TRUE(layout_custom_apply(&lycon, &parent, "unit-min-max"));

    EXPECT_FLOAT_EQ(parent.content_width, 100.0f);
    EXPECT_FLOAT_EQ(parent.width, 100.0f);
    EXPECT_FLOAT_EQ(parent.content_height, 25.0f);
    EXPECT_FLOAT_EQ(parent.height, 25.0f);
    EXPECT_FLOAT_EQ(first.x, -10.0f);
    EXPECT_FLOAT_EQ(second.y, 20.0f);
}

TEST_F(CustomLayoutTest, UnregisteredLayoutFallsBackWithoutChangingBlock) {
    ViewBlock parent;
    init_parent(&parent, 64.0f, 32.0f);

    EXPECT_FALSE(layout_custom_apply(&lycon, &parent, "unit-missing-layout"));
    EXPECT_FALSE(layout_custom_apply(&lycon, &parent, "unit-missing-layout"));
    EXPECT_FLOAT_EQ(parent.width, 64.0f);
    EXPECT_FLOAT_EQ(parent.height, 32.0f);
}

TEST_F(CustomLayoutTest, DuplicatePlacementsAreIgnoredAndMissingChildrenResetToOrigin) {
    ViewBlock parent;
    ViewBlock first;
    ViewBlock second;
    init_parent(&parent, 0.0f, 0.0f);
    init_block(&first, "section", 20.0f, 10.0f);
    init_block(&second, "section", 30.0f, 15.0f);
    ASSERT_TRUE(parent.append_child(&first));
    ASSERT_TRUE(parent.append_child(&second));

    ASSERT_TRUE(custom_layout_register("unit-duplicate-missing", custom_layout_test_duplicate_and_missing));
    ASSERT_TRUE(layout_custom_apply(&lycon, &parent, "unit-duplicate-missing"));

    EXPECT_FLOAT_EQ(first.x, 12.0f);
    EXPECT_FLOAT_EQ(first.y, 8.0f);
    EXPECT_FLOAT_EQ(second.x, 0.0f);
    EXPECT_FLOAT_EQ(second.y, 0.0f);
    EXPECT_FLOAT_EQ(parent.content_width, 32.0f);
    EXPECT_FLOAT_EQ(parent.content_height, 18.0f);
}
