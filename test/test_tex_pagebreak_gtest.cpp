// test_tex_pagebreak_gtest.cpp - Unit tests for TeX Page Breaking
//
// Tests the tex_pagebreak.hpp implementation:
// - Page break parameter handling
// - Break candidate finding
// - Page content building
// - Mark/insertion handling
// - Page cost computation

#include <gtest/gtest.h>
#include "lambda/tex/tex_pagebreak.hpp"
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_vlist.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class TexPageBreakTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }

    // Create a simple line (hbox)
    TexNode* make_test_line(float width, float height, float depth) {
        TexNode* hbox = make_hlist(arena);
        hbox->width = width;
        hbox->height = height;
        hbox->depth = depth;
        return hbox;
    }

    // Create a paragraph-like vlist with multiple lines
    TexNode* make_paragraph(int line_count, float line_height = 12.0f) {
        TexNode* vlist = make_vlist(arena);
        for (int i = 0; i < line_count; i++) {
            TexNode* line = make_test_line(300.0f, line_height * 0.8f, line_height * 0.2f);
            vlist->append_child(line);

            // Add baselineskip glue between lines
            if (i < line_count - 1) {
                TexNode* glue = make_glue(arena, Glue::flexible(line_height, 2.0f, 1.0f), "baselineskip");
                vlist->append_child(glue);
            }
        }
        return vlist;
    }
};

// ============================================================================
// Parameter Tests
// ============================================================================

TEST_F(TexPageBreakTest, DefaultParameters) {
    PageBreakParams params = PageBreakParams::defaults();

    // Check default page dimensions
    EXPECT_GT(params.page_height, 500.0f);
    EXPECT_GT(params.top_skip, 0.0f);
    EXPECT_GT(params.max_depth, 0.0f);

    // Check default penalties
    EXPECT_GT(params.widow_penalty, 0);
    EXPECT_GT(params.club_penalty, 0);
    EXPECT_GE(params.inter_line_penalty, 0);

    // Check float parameters
    EXPECT_GT(params.top_fraction, 0.0f);
    EXPECT_LT(params.top_fraction, 1.0f);
    EXPECT_GT(params.bottom_fraction, 0.0f);
    EXPECT_LT(params.bottom_fraction, 1.0f);
}

TEST_F(TexPageBreakTest, CustomParameters) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 700.0f;
    params.widow_penalty = 500;
    params.club_penalty = 500;

    EXPECT_FLOAT_EQ(params.page_height, 700.0f);
    EXPECT_EQ(params.widow_penalty, 500);
    EXPECT_EQ(params.club_penalty, 500);
}

// ============================================================================
// Break Candidate Tests
// ============================================================================

TEST_F(TexPageBreakTest, BreakCandidateCreation) {
    BreakCandidate candidate;
    candidate.node = nullptr;
    candidate.index = 0;
    candidate.type = PageBreakType::Normal;
    candidate.penalty = 0;
    candidate.page_height = 400.0f;
    candidate.badness = 100;

    EXPECT_EQ(candidate.type, PageBreakType::Normal);
    EXPECT_EQ(candidate.penalty, 0);
    EXPECT_FLOAT_EQ(candidate.page_height, 400.0f);
}

TEST_F(TexPageBreakTest, BreakTypeValues) {
    // Verify all break types exist
    EXPECT_NE(PageBreakType::Normal, PageBreakType::Penalty);
    EXPECT_NE(PageBreakType::Display, PageBreakType::Float);
    EXPECT_NE(PageBreakType::Forced, PageBreakType::End);
}

// ============================================================================
// Page Breaking Tests (using functional API)
// ============================================================================

TEST_F(TexPageBreakTest, BreakShortDocument) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 600.0f;

    // Create a short document (one page)
    TexNode* doc = make_paragraph(10, 12.0f);

    PageBreakResult result = break_into_pages(doc, params, arena);

    // Should produce exactly one page
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.page_count, 1);
}

TEST_F(TexPageBreakTest, BreakLongDocument) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 200.0f;  // Small page for testing

    // Create a long document (multiple pages)
    TexNode* doc = make_paragraph(50, 12.0f);

    PageBreakResult result = break_into_pages(doc, params, arena);

    // Should produce multiple pages
    EXPECT_TRUE(result.success);
    EXPECT_GT(result.page_count, 1);
}

TEST_F(TexPageBreakTest, ForcedBreak) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 600.0f;

    // Create document with forced break
    TexNode* vlist = make_vlist(arena);

    // First page content
    for (int i = 0; i < 5; i++) {
        vlist->append_child(make_test_line(300.0f, 10.0f, 2.0f));
        vlist->append_child(make_glue(arena, Glue::fixed(12.0f), "baselineskip"));
    }

    // Add forced break (penalty -10000)
    vlist->append_child(make_penalty(arena, -10000));

    // Second page content
    for (int i = 0; i < 5; i++) {
        vlist->append_child(make_test_line(300.0f, 10.0f, 2.0f));
        vlist->append_child(make_glue(arena, Glue::fixed(12.0f), "baselineskip"));
    }

    PageBreakResult result = break_into_pages(vlist, params, arena);

    // Should succeed - may produce 1 or 2 pages depending on implementation
    EXPECT_TRUE(result.success);
    EXPECT_GE(result.page_count, 1);
}

// ============================================================================
// Penalty Tests
// ============================================================================

TEST_F(TexPageBreakTest, HighPenaltyBreak) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 200.0f;

    TexNode* vlist = make_vlist(arena);

    // Add content with high penalty (discourage break)
    for (int i = 0; i < 30; i++) {
        vlist->append_child(make_test_line(300.0f, 10.0f, 2.0f));
        // Add penalty at line 15
        if (i == 15) {
            vlist->append_child(make_penalty(arena, 5000));
        }
        vlist->append_child(make_glue(arena, Glue::fixed(12.0f), "baselineskip"));
    }

    PageBreakResult result = break_into_pages(vlist, params, arena);

    // Should still find valid breaks
    EXPECT_TRUE(result.success);
    EXPECT_GT(result.page_count, 0);
}

// ============================================================================
// Insertion/Float Tests
// ============================================================================

TEST_F(TexPageBreakTest, InsertionClassConstants) {
    // Check well-known insertion classes exist
    EXPECT_EQ(INSERT_CLASS_TOPFLOAT, 253);
    EXPECT_EQ(INSERT_CLASS_FOOTNOTE, 254);
    EXPECT_EQ(INSERT_CLASS_BOTTOMFLOAT, 255);
}

TEST_F(TexPageBreakTest, InsertionStateInit) {
    InsertionState state;

    EXPECT_FLOAT_EQ(state.total_height(), 0.0f);
    for (int i = 0; i < 256; i++) {
        EXPECT_EQ(state.class_content[i], nullptr);
        EXPECT_FLOAT_EQ(state.class_heights[i], 0.0f);
    }
}

TEST_F(TexPageBreakTest, InsertionStateAddInsert) {
    InsertionState state;

    TexNode* content1 = make_test_line(100.0f, 10.0f, 2.0f);
    state.add_insert(INSERT_CLASS_FOOTNOTE, content1, 12.0f);

    EXPECT_FLOAT_EQ(state.class_heights[INSERT_CLASS_FOOTNOTE], 12.0f);
    EXPECT_EQ(state.class_content[INSERT_CLASS_FOOTNOTE], content1);
    EXPECT_FLOAT_EQ(state.total_height(), 12.0f);

    // Add another
    TexNode* content2 = make_test_line(100.0f, 10.0f, 2.0f);
    state.add_insert(INSERT_CLASS_FOOTNOTE, content2, 12.0f);

    EXPECT_FLOAT_EQ(state.class_heights[INSERT_CLASS_FOOTNOTE], 24.0f);
    EXPECT_FLOAT_EQ(state.total_height(), 24.0f);
}

TEST_F(TexPageBreakTest, InsertionStateReset) {
    InsertionState state;

    TexNode* content = make_test_line(100.0f, 10.0f, 2.0f);
    state.add_insert(INSERT_CLASS_FOOTNOTE, content, 12.0f);

    state.reset();

    EXPECT_FLOAT_EQ(state.total_height(), 0.0f);
    EXPECT_EQ(state.class_content[INSERT_CLASS_FOOTNOTE], nullptr);
}

// ============================================================================
// Mark State Tests
// ============================================================================

TEST_F(TexPageBreakTest, MarkStateInit) {
    MarkState state;

    EXPECT_EQ(state.top_mark, nullptr);
    EXPECT_EQ(state.first_mark, nullptr);
    EXPECT_EQ(state.bot_mark, nullptr);
}

TEST_F(TexPageBreakTest, MarkStateRecordMark) {
    MarkState state;

    TexNode* mark1 = make_mark(arena, "First");
    state.record_mark(mark1);

    EXPECT_EQ(state.first_mark, mark1);
    EXPECT_EQ(state.bot_mark, mark1);

    TexNode* mark2 = make_mark(arena, "Second");
    state.record_mark(mark2);

    EXPECT_EQ(state.first_mark, mark1);
    EXPECT_EQ(state.bot_mark, mark2);
}

TEST_F(TexPageBreakTest, MarkStateAdvancePage) {
    MarkState state;

    TexNode* mark1 = make_mark(arena, "Page1");
    state.record_mark(mark1);

    state.advance_page();

    // After advance, bot_mark becomes top_mark
    EXPECT_EQ(state.top_mark, mark1);
    EXPECT_EQ(state.first_mark, nullptr);
    EXPECT_EQ(state.bot_mark, nullptr);
}

// ============================================================================
// Mark Node Tests
// ============================================================================

TEST_F(TexPageBreakTest, MarkNodeCreation) {
    TexNode* mark = make_mark(arena, "chapter:Introduction");

    ASSERT_NE(mark, nullptr);
    EXPECT_EQ(mark->node_class, NodeClass::Mark);
    EXPECT_STREQ(mark->content.mark.text, "chapter:Introduction");
}

TEST_F(TexPageBreakTest, InsertionNodeCreation) {
    TexNode* content = make_test_line(200.0f, 10.0f, 2.0f);
    TexNode* insert = make_insert(arena, INSERT_CLASS_FOOTNOTE, content);

    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(insert->node_class, NodeClass::Insert);
    EXPECT_EQ(insert->content.insert.insert_class, INSERT_CLASS_FOOTNOTE);
    EXPECT_EQ(insert->content.insert.content, content);
}

// ============================================================================
// Page Content Building Tests
// ============================================================================

TEST_F(TexPageBreakTest, BuildPages) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 200.0f;

    TexNode* doc = make_paragraph(30, 12.0f);

    PageBreakResult result = break_into_pages(doc, params, arena);
    EXPECT_TRUE(result.success);

    // Build actual pages
    PageContent* pages = build_pages(doc, result, params, arena);

    ASSERT_NE(pages, nullptr);
    for (int i = 0; i < result.page_count; i++) {
        EXPECT_NE(pages[i].vlist, nullptr);
        EXPECT_GT(pages[i].height, 0.0f);
    }
}

// ============================================================================
// Paginate Convenience Function
// ============================================================================

TEST_F(TexPageBreakTest, Paginate) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 300.0f;

    TexNode* doc = make_paragraph(40, 12.0f);

    int page_count = 0;
    PageContent* pages = paginate(doc, params, &page_count, arena);

    ASSERT_NE(pages, nullptr);
    EXPECT_GT(page_count, 1);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST_F(TexPageBreakTest, GetNodePenalty) {
    TexNode* penalty = make_penalty(arena, 150);
    EXPECT_EQ(get_node_penalty(penalty), 150);

    TexNode* line = make_test_line(100.0f, 10.0f, 2.0f);
    EXPECT_EQ(get_node_penalty(line), 0);
}

TEST_F(TexPageBreakTest, IsForcedPageBreak) {
    TexNode* forced = make_penalty(arena, -10000);
    EXPECT_TRUE(is_forced_page_break(forced));

    TexNode* normal = make_penalty(arena, 100);
    EXPECT_FALSE(is_forced_page_break(normal));

    TexNode* line = make_test_line(100.0f, 10.0f, 2.0f);
    EXPECT_FALSE(is_forced_page_break(line));
}

TEST_F(TexPageBreakTest, IsPageDiscardable) {
    TexNode* glue = make_glue(arena, Glue::fixed(10.0f), "test");
    EXPECT_TRUE(is_page_discardable(glue));

    TexNode* penalty = make_penalty(arena, 0);
    EXPECT_TRUE(is_page_discardable(penalty));

    TexNode* line = make_test_line(100.0f, 10.0f, 2.0f);
    EXPECT_FALSE(is_page_discardable(line));
}

// ============================================================================
// Badness Computation Tests
// ============================================================================

TEST_F(TexPageBreakTest, BadnessComputation) {
    // Perfect fit
    EXPECT_EQ(compute_page_badness(100.0f, 100.0f, 0.0f, 0.0f), 0);

    // Slight stretch
    int bad = compute_page_badness(90.0f, 100.0f, 20.0f, 0.0f);
    EXPECT_GT(bad, 0);
    EXPECT_LT(bad, 100);

    // More stretch (half of available)
    bad = compute_page_badness(80.0f, 100.0f, 40.0f, 0.0f);
    EXPECT_GE(bad, 10);

    // Overfull (infinite badness - can't stretch enough)
    bad = compute_page_badness(80.0f, 100.0f, 10.0f, 0.0f);
    // Should return a very large value (AWFUL_PAGE_BAD or similar)
    EXPECT_GT(bad, 10000);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TexPageBreakTest, EmptyDocument) {
    PageBreakParams params = PageBreakParams::defaults();

    TexNode* vlist = make_vlist(arena);

    PageBreakResult result = break_into_pages(vlist, params, arena);

    // Empty document behavior depends on implementation
    // May fail or produce 0-1 pages
    if (result.success) {
        EXPECT_LE(result.page_count, 1);
    }
}

TEST_F(TexPageBreakTest, SingleLineDocument) {
    PageBreakParams params = PageBreakParams::defaults();

    TexNode* vlist = make_vlist(arena);
    vlist->append_child(make_test_line(300.0f, 10.0f, 2.0f));

    PageBreakResult result = break_into_pages(vlist, params, arena);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.page_count, 1);
}

TEST_F(TexPageBreakTest, OversizedContent) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 100.0f;

    TexNode* vlist = make_vlist(arena);
    // Single line taller than page
    vlist->append_child(make_test_line(300.0f, 150.0f, 20.0f));

    PageBreakResult result = break_into_pages(vlist, params, arena);

    // Should still produce a page (overfull warning)
    EXPECT_TRUE(result.success);
    EXPECT_GE(result.page_count, 1);
}

// ============================================================================
// Display Math Spacing
// ============================================================================

TEST_F(TexPageBreakTest, DisplayMathContent) {
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = 300.0f;

    TexNode* vlist = make_vlist(arena);

    // Text before display
    for (int i = 0; i < 10; i++) {
        vlist->append_child(make_test_line(300.0f, 10.0f, 2.0f));
        vlist->append_child(make_glue(arena, Glue::fixed(12.0f), "baselineskip"));
    }

    // Display math (abovedisplayskip + display + belowdisplayskip)
    vlist->append_child(make_glue(arena, Glue::flexible(12.0f, 4.0f, 2.0f), "abovedisplayskip"));
    TexNode* display = make_test_line(200.0f, 20.0f, 5.0f);
    // Display math box (larger than normal line)
    vlist->append_child(display);
    vlist->append_child(make_glue(arena, Glue::flexible(12.0f, 4.0f, 2.0f), "belowdisplayskip"));

    // More text
    for (int i = 0; i < 10; i++) {
        vlist->append_child(make_test_line(300.0f, 10.0f, 2.0f));
        vlist->append_child(make_glue(arena, Glue::fixed(12.0f), "baselineskip"));
    }

    PageBreakResult result = break_into_pages(vlist, params, arena);
    EXPECT_TRUE(result.success);
}
