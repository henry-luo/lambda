// test_tex_vlist_gtest.cpp - Unit tests for VList builder and page breaking
//
// Tests the tex_vlist.hpp and tex_pagebreak.hpp implementations (Phase 3).

#include <gtest/gtest.h>
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_hlist.hpp"
#include "lambda/tex/tex_linebreak.hpp"
#include "lambda/tex/tex_vlist.hpp"
#include "lambda/tex/tex_pagebreak.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>

using namespace tex;

class TexVListTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    TFMFontManager* fonts;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        fonts = create_font_manager(arena);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }
};

// ============================================================================
// VList Params Tests
// ============================================================================

TEST_F(TexVListTest, DefaultParams) {
    VListParams params = VListParams::defaults();

    EXPECT_FLOAT_EQ(params.baseline_skip, 12.0f);
    EXPECT_FLOAT_EQ(params.line_skip_limit, 0.0f);
    EXPECT_FLOAT_EQ(params.line_skip, 1.0f);
    EXPECT_FLOAT_EQ(params.max_depth, 4.0f);
}

// ============================================================================
// VList Context Tests
// ============================================================================

TEST_F(TexVListTest, InitContext) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 300.0f);

    EXPECT_FLOAT_EQ(ctx.line_params.hsize, 300.0f);
    EXPECT_STREQ(ctx.body_font.name, "cmr10");
    EXPECT_FLOAT_EQ(ctx.body_font.size_pt, 10.0f);
}

TEST_F(TexVListTest, BeginEndVList) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 300.0f);

    TexNode* vlist = begin_vlist(ctx);
    ASSERT_NE(vlist, nullptr);
    EXPECT_EQ(vlist->node_class, NodeClass::VList);

    TexNode* result = end_vlist(ctx);
    EXPECT_EQ(result, vlist);
}

// ============================================================================
// VList Building Tests
// ============================================================================

TEST_F(TexVListTest, AddSingleParagraph) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);

    const char* text = "Hello world";
    add_paragraph(ctx, text, strlen(text));

    TexNode* vlist = end_vlist(ctx);

    ASSERT_NE(vlist, nullptr);
    EXPECT_GT(vlist->height, 0);

    // Should have at least one line
    int line_count = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (c->node_class == NodeClass::HBox) {
            line_count++;
        }
    }
    EXPECT_GE(line_count, 1);
}

TEST_F(TexVListTest, AddMultipleParagraphs) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);

    const char* para1 = "First paragraph with some text.";
    const char* para2 = "Second paragraph with more text.";

    add_paragraph(ctx, para1, strlen(para1));
    add_paragraph(ctx, para2, strlen(para2));

    TexNode* vlist = end_vlist(ctx);

    ASSERT_NE(vlist, nullptr);

    // Should have parskip glue between paragraphs
    bool found_parskip = false;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (c->node_class == NodeClass::Glue) {
            if (c->content.glue.name && strcmp(c->content.glue.name, "parskip") == 0) {
                found_parskip = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_parskip);
}

TEST_F(TexVListTest, AddHeading) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);

    const char* heading = "Section Title";
    add_heading(ctx, heading, strlen(heading), 1);

    TexNode* vlist = end_vlist(ctx);

    ASSERT_NE(vlist, nullptr);

    // Should have below-section-skip after heading
    bool found_below_skip = false;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (c->node_class == NodeClass::Glue) {
            if (c->content.glue.name && strcmp(c->content.glue.name, "belowsectionskip") == 0) {
                found_below_skip = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_below_skip);
}

TEST_F(TexVListTest, AddVSpace) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);
    add_vspace(ctx, Glue::fixed(20.0f));
    TexNode* vlist = end_vlist(ctx);

    ASSERT_NE(vlist, nullptr);
    ASSERT_NE(vlist->first_child, nullptr);
    EXPECT_EQ(vlist->first_child->node_class, NodeClass::Glue);
    EXPECT_FLOAT_EQ(vlist->first_child->content.glue.spec.space, 20.0f);
}

TEST_F(TexVListTest, AddHRule) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);
    add_hrule(ctx, 0.4f, -1);  // Full width
    TexNode* vlist = end_vlist(ctx);

    ASSERT_NE(vlist, nullptr);
    ASSERT_NE(vlist->first_child, nullptr);
    EXPECT_EQ(vlist->first_child->node_class, NodeClass::Rule);
    EXPECT_FLOAT_EQ(vlist->first_child->height, 0.4f);
    EXPECT_FLOAT_EQ(vlist->first_child->width, 200.0f);  // Full line width
}

// ============================================================================
// VList Measurement Tests
// ============================================================================

TEST_F(TexVListTest, MeasureVList) {
    // Create a simple VList manually
    TexNode* vlist = make_vlist(arena);

    // Add two lines with known dimensions
    TexNode* line1 = make_hbox(arena);
    line1->height = 10.0f;
    line1->depth = 2.0f;
    vlist->append_child(line1);

    TexNode* glue = make_glue(arena, Glue::fixed(10.0f), "baselineskip");
    vlist->append_child(glue);

    TexNode* line2 = make_hbox(arena);
    line2->height = 10.0f;
    line2->depth = 3.0f;
    vlist->append_child(line2);

    VListDimensions dim = measure_vlist(vlist);

    // Total = line1.h + line1.d + glue + line2.h + line2.d = 10+2+10+10+3 = 35
    // height = total - last_depth = 35 - 3 = 32
    // depth = last_depth = 3
    EXPECT_FLOAT_EQ(dim.depth, 3.0f);
    EXPECT_FLOAT_EQ(dim.height, 32.0f);
}

// ============================================================================
// Inter-line Spacing Tests
// ============================================================================

TEST_F(TexVListTest, ComputeInterlineGlue_Normal) {
    VListParams params = VListParams::defaults();
    params.baseline_skip = 12.0f;

    TexNode* prev = make_hbox(arena);
    prev->depth = 2.0f;

    TexNode* curr = make_hbox(arena);
    curr->height = 8.0f;

    TexNode* interline = compute_interline_glue(prev, curr, params, arena);

    ASSERT_NE(interline, nullptr);
    // desired = 12 - 2 - 8 = 2 >= 0 (lineskiplimit), so use baselineskip-based glue
    EXPECT_EQ(interline->node_class, NodeClass::Glue);
    EXPECT_NEAR(interline->content.glue.spec.space, 2.0f, 0.01f);
}

TEST_F(TexVListTest, ComputeInterlineGlue_TooClose) {
    VListParams params = VListParams::defaults();
    params.baseline_skip = 12.0f;
    params.line_skip_limit = 0.0f;
    params.line_skip = 1.0f;

    TexNode* prev = make_hbox(arena);
    prev->depth = 6.0f;

    TexNode* curr = make_hbox(arena);
    curr->height = 8.0f;

    TexNode* interline = compute_interline_glue(prev, curr, params, arena);

    ASSERT_NE(interline, nullptr);
    // desired = 12 - 6 - 8 = -2 < 0 (lineskiplimit), so use lineskip
    EXPECT_EQ(interline->node_class, NodeClass::Kern);
    EXPECT_FLOAT_EQ(interline->content.kern.amount, 1.0f);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST_F(TexVListTest, CenterLine) {
    TexNode* content = make_hbox(arena);
    content->width = 50.0f;
    content->height = 10.0f;
    content->depth = 2.0f;

    TexNode* centered = center_line(content, 200.0f, arena);

    ASSERT_NE(centered, nullptr);
    EXPECT_EQ(centered->node_class, NodeClass::HBox);
    EXPECT_FLOAT_EQ(centered->width, 200.0f);

    // Should have: hfil, content, hfil
    int child_count = centered->child_count();
    EXPECT_EQ(child_count, 3);
}

TEST_F(TexVListTest, RightAlignLine) {
    TexNode* content = make_hbox(arena);
    content->width = 50.0f;
    content->height = 10.0f;

    TexNode* aligned = right_align_line(content, 200.0f, arena);

    ASSERT_NE(aligned, nullptr);
    EXPECT_EQ(aligned->node_class, NodeClass::HBox);

    // Should have: hfill, content
    int child_count = aligned->child_count();
    EXPECT_EQ(child_count, 2);
}

TEST_F(TexVListTest, SplitLine) {
    TexNode* left = make_hbox(arena);
    left->width = 30.0f;
    left->height = 10.0f;

    TexNode* right = make_hbox(arena);
    right->width = 40.0f;
    right->height = 12.0f;

    TexNode* split = split_line(left, right, 200.0f, arena);

    ASSERT_NE(split, nullptr);
    EXPECT_EQ(split->node_class, NodeClass::HBox);
    EXPECT_FLOAT_EQ(split->width, 200.0f);
    EXPECT_FLOAT_EQ(split->height, 12.0f);  // max of both

    // Should have: left, hfill, right
    int child_count = split->child_count();
    EXPECT_EQ(child_count, 3);
}

// ============================================================================
// Page Break Params Tests
// ============================================================================

TEST_F(TexVListTest, PageBreakDefaultParams) {
    PageBreakParams params = PageBreakParams::defaults();

    EXPECT_FLOAT_EQ(params.page_height, 592.0f);
    EXPECT_FLOAT_EQ(params.top_skip, 10.0f);
    EXPECT_FLOAT_EQ(params.max_depth, 4.0f);
    EXPECT_EQ(params.widow_penalty, 150);
    EXPECT_EQ(params.club_penalty, 150);
}

// ============================================================================
// Page Breaking Tests
// ============================================================================

TEST_F(TexVListTest, PageBreak_SinglePage) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);
    const char* text = "Short paragraph.";
    add_paragraph(ctx, text, strlen(text));
    TexNode* vlist = end_vlist(ctx);

    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 500.0f;  // Large enough for one paragraph

    PageBreakResult result = break_into_pages(vlist, params, arena);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.page_count, 1);
}

TEST_F(TexVListTest, PageBreak_MultiplePages) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);

    // Add many paragraphs to force page breaks
    const char* text = "This is a paragraph with enough text to take some space on the page. We need multiple paragraphs to test page breaking.";
    for (int i = 0; i < 20; ++i) {
        add_paragraph(ctx, text, strlen(text));
    }

    TexNode* vlist = end_vlist(ctx);

    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 200.0f;  // Small page to force breaks

    PageBreakResult result = break_into_pages(vlist, params, arena);

    EXPECT_TRUE(result.success);
    EXPECT_GT(result.page_count, 1);
}

TEST_F(TexVListTest, PageBreak_ForcedBreak) {
    TexNode* vlist = make_vlist(arena);

    // Add some content
    TexNode* line1 = make_hbox(arena);
    line1->height = 10.0f;
    line1->depth = 2.0f;
    vlist->append_child(line1);

    // Add forced page break
    TexNode* penalty = make_penalty(arena, -10000);  // EJECT_PENALTY
    vlist->append_child(penalty);

    // Add more content
    TexNode* line2 = make_hbox(arena);
    line2->height = 10.0f;
    line2->depth = 2.0f;
    vlist->append_child(line2);

    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 500.0f;  // Large page

    PageBreakResult result = break_into_pages(vlist, params, arena);

    EXPECT_TRUE(result.success);
    EXPECT_GE(result.page_count, 2);  // Should break at forced penalty
}

// ============================================================================
// Page Building Tests
// ============================================================================

TEST_F(TexVListTest, BuildPages) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);
    const char* text = "Test paragraph for page building.";
    add_paragraph(ctx, text, strlen(text));
    TexNode* vlist = end_vlist(ctx);

    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 500.0f;

    PageBreakResult result = break_into_pages(vlist, params, arena);
    ASSERT_TRUE(result.success);

    PageContent* pages = build_pages(vlist, result, params, arena);
    ASSERT_NE(pages, nullptr);

    // Check first page
    EXPECT_NE(pages[0].vlist, nullptr);
    EXPECT_GT(pages[0].height, 0);
}

TEST_F(TexVListTest, Paginate) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 200.0f);

    begin_vlist(ctx);
    const char* text = "Another test paragraph for pagination.";
    add_paragraph(ctx, text, strlen(text));
    TexNode* vlist = end_vlist(ctx);

    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 500.0f;

    int page_count = 0;
    PageContent* pages = paginate(vlist, params, &page_count, arena);

    EXPECT_GT(page_count, 0);
    ASSERT_NE(pages, nullptr);
    EXPECT_NE(pages[0].vlist, nullptr);
}

// ============================================================================
// Page Badness Tests
// ============================================================================

TEST_F(TexVListTest, ComputePageBadness_Perfect) {
    // Page exactly fills target
    int badness = compute_page_badness(500.0f, 500.0f, 10.0f, 5.0f);
    EXPECT_EQ(badness, 0);
}

TEST_F(TexVListTest, ComputePageBadness_Underfull) {
    // Page is short
    int badness = compute_page_badness(400.0f, 500.0f, 200.0f, 50.0f);
    EXPECT_GT(badness, 0);
    EXPECT_LT(badness, 100);  // With good stretch, badness should be low
}

TEST_F(TexVListTest, ComputePageBadness_Overfull) {
    // Page is too tall with no shrink
    int badness = compute_page_badness(550.0f, 500.0f, 10.0f, 0.0f);
    EXPECT_GT(badness, 10000);  // Awful
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(TexVListTest, Integration_DocumentWithSections) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 300.0f);

    begin_vlist(ctx);

    // Add a section
    const char* heading = "Introduction";
    add_heading(ctx, heading, strlen(heading), 1);

    // Add paragraph
    const char* para1 = "This is the introduction paragraph with some text content.";
    add_paragraph(ctx, para1, strlen(para1));

    // Add another section
    const char* heading2 = "Methods";
    add_heading(ctx, heading2, strlen(heading2), 1);

    // Add more text
    const char* para2 = "This describes the methods used in the study.";
    add_paragraph(ctx, para2, strlen(para2));

    TexNode* vlist = end_vlist(ctx);

    ASSERT_NE(vlist, nullptr);
    EXPECT_GT(vlist->height, 0);

    // Now paginate
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 500.0f;

    int page_count = 0;
    PageContent* pages = paginate(vlist, params, &page_count, arena);

    EXPECT_GE(page_count, 1);
    ASSERT_NE(pages, nullptr);
}

TEST_F(TexVListTest, Integration_LongDocument) {
    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 300.0f);
    ctx.line_params.pretolerance = 10000.0f;  // High tolerance for testing
    ctx.line_params.tolerance = 10000.0f;

    begin_vlist(ctx);

    // Add many paragraphs
    const char* para = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";

    for (int i = 0; i < 30; ++i) {
        if (i % 5 == 0) {
            char heading[32];
            snprintf(heading, sizeof(heading), "Section %d", i / 5 + 1);
            add_heading(ctx, heading, strlen(heading), 1);
        }
        add_paragraph(ctx, para, strlen(para));
    }

    TexNode* vlist = end_vlist(ctx);

    ASSERT_NE(vlist, nullptr);
    EXPECT_GT(vlist->height, 500.0f);  // Should be tall

    // Paginate with small pages
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 200.0f;

    int page_count = 0;
    PageContent* pages = paginate(vlist, params, &page_count, arena);

    EXPECT_GT(page_count, 5);  // Should have many pages
    ASSERT_NE(pages, nullptr);

    // Verify all pages have content
    for (int i = 0; i < page_count; ++i) {
        EXPECT_NE(pages[i].vlist, nullptr);
        EXPECT_GT(pages[i].height, 0);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    log_init("log.conf");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
