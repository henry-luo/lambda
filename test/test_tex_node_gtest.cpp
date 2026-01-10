// test_tex_node_gtest.cpp - Unit tests for unified TeX node system
//
// Tests the new tex_node.hpp, tex_tfm.hpp, and tex_hlist.hpp implementations.

#include <gtest/gtest.h>
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_hlist.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"

using namespace tex;

class TexNodeTest : public ::testing::Test {
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
};

// ============================================================================
// Node Creation Tests
// ============================================================================

TEST_F(TexNodeTest, CreateCharNode) {
    FontSpec font;
    font.name = "cmr10";
    font.size_pt = 10.0f;

    TexNode* node = make_char(arena, 'A', font);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->node_class, NodeClass::Char);
    EXPECT_EQ(node->content.ch.codepoint, 'A');
    EXPECT_STREQ(node->content.ch.font.name, "cmr10");
}

TEST_F(TexNodeTest, CreateGlueNode) {
    Glue g = Glue::flexible(10.0f, 5.0f, 3.0f);
    TexNode* node = make_glue(arena, g, "test");

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->node_class, NodeClass::Glue);
    EXPECT_FLOAT_EQ(node->content.glue.spec.space, 10.0f);
    EXPECT_FLOAT_EQ(node->content.glue.spec.stretch, 5.0f);
    EXPECT_FLOAT_EQ(node->content.glue.spec.shrink, 3.0f);
    EXPECT_STREQ(node->content.glue.name, "test");
}

TEST_F(TexNodeTest, CreateKernNode) {
    TexNode* node = make_kern(arena, 5.5f);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->node_class, NodeClass::Kern);
    EXPECT_FLOAT_EQ(node->content.kern.amount, 5.5f);
    EXPECT_FLOAT_EQ(node->width, 5.5f);
}

TEST_F(TexNodeTest, CreatePenaltyNode) {
    TexNode* node = make_penalty(arena, 150);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->node_class, NodeClass::Penalty);
    EXPECT_EQ(node->content.penalty.value, 150);
}

TEST_F(TexNodeTest, CreateRuleNode) {
    TexNode* node = make_rule(arena, 100.0f, 0.4f, 0.0f);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->node_class, NodeClass::Rule);
    EXPECT_FLOAT_EQ(node->width, 100.0f);
    EXPECT_FLOAT_EQ(node->height, 0.4f);
    EXPECT_FLOAT_EQ(node->depth, 0.0f);
}

TEST_F(TexNodeTest, CreateHList) {
    TexNode* hlist = make_hlist(arena);

    ASSERT_NE(hlist, nullptr);
    EXPECT_EQ(hlist->node_class, NodeClass::HList);
    EXPECT_EQ(hlist->first_child, nullptr);
    EXPECT_EQ(hlist->child_count(), 0);
}

TEST_F(TexNodeTest, CreateVList) {
    TexNode* vlist = make_vlist(arena);

    ASSERT_NE(vlist, nullptr);
    EXPECT_EQ(vlist->node_class, NodeClass::VList);
}

// ============================================================================
// Tree Structure Tests
// ============================================================================

TEST_F(TexNodeTest, AppendChild) {
    TexNode* hlist = make_hlist(arena);

    FontSpec font = {"cmr10", 10.0f, nullptr, 0};
    TexNode* c1 = make_char(arena, 'A', font);
    TexNode* c2 = make_char(arena, 'B', font);
    TexNode* c3 = make_char(arena, 'C', font);

    hlist->append_child(c1);
    hlist->append_child(c2);
    hlist->append_child(c3);

    EXPECT_EQ(hlist->child_count(), 3);
    EXPECT_EQ(hlist->first_child, c1);
    EXPECT_EQ(hlist->last_child, c3);

    EXPECT_EQ(c1->next_sibling, c2);
    EXPECT_EQ(c2->next_sibling, c3);
    EXPECT_EQ(c3->next_sibling, nullptr);

    EXPECT_EQ(c1->prev_sibling, nullptr);
    EXPECT_EQ(c2->prev_sibling, c1);
    EXPECT_EQ(c3->prev_sibling, c2);

    EXPECT_EQ(c1->parent, hlist);
    EXPECT_EQ(c2->parent, hlist);
    EXPECT_EQ(c3->parent, hlist);
}

TEST_F(TexNodeTest, PrependChild) {
    TexNode* hlist = make_hlist(arena);

    FontSpec font = {"cmr10", 10.0f, nullptr, 0};
    TexNode* c1 = make_char(arena, 'A', font);
    TexNode* c2 = make_char(arena, 'B', font);

    hlist->append_child(c1);
    hlist->prepend_child(c2);

    EXPECT_EQ(hlist->first_child, c2);
    EXPECT_EQ(hlist->last_child, c1);
    EXPECT_EQ(c2->next_sibling, c1);
}

TEST_F(TexNodeTest, InsertAfter) {
    TexNode* hlist = make_hlist(arena);

    FontSpec font = {"cmr10", 10.0f, nullptr, 0};
    TexNode* c1 = make_char(arena, 'A', font);
    TexNode* c2 = make_char(arena, 'B', font);
    TexNode* c3 = make_char(arena, 'C', font);

    hlist->append_child(c1);
    hlist->append_child(c3);
    hlist->insert_after(c1, c2);

    EXPECT_EQ(c1->next_sibling, c2);
    EXPECT_EQ(c2->next_sibling, c3);
    EXPECT_EQ(c2->prev_sibling, c1);
    EXPECT_EQ(c3->prev_sibling, c2);
}

TEST_F(TexNodeTest, RemoveChild) {
    TexNode* hlist = make_hlist(arena);

    FontSpec font = {"cmr10", 10.0f, nullptr, 0};
    TexNode* c1 = make_char(arena, 'A', font);
    TexNode* c2 = make_char(arena, 'B', font);
    TexNode* c3 = make_char(arena, 'C', font);

    hlist->append_child(c1);
    hlist->append_child(c2);
    hlist->append_child(c3);

    hlist->remove_child(c2);

    EXPECT_EQ(hlist->child_count(), 2);
    EXPECT_EQ(c1->next_sibling, c3);
    EXPECT_EQ(c3->prev_sibling, c1);
    EXPECT_EQ(c2->parent, nullptr);
}

// ============================================================================
// Math Node Tests
// ============================================================================

TEST_F(TexNodeTest, CreateFractionNode) {
    FontSpec font = {"cmr10", 10.0f, nullptr, 0};
    TexNode* num = make_char(arena, '1', font);
    TexNode* den = make_char(arena, '2', font);

    TexNode* frac = make_fraction(arena, num, den, 0.4f);

    ASSERT_NE(frac, nullptr);
    EXPECT_EQ(frac->node_class, NodeClass::Fraction);
    EXPECT_EQ(frac->content.frac.numerator, num);
    EXPECT_EQ(frac->content.frac.denominator, den);
    EXPECT_FLOAT_EQ(frac->content.frac.rule_thickness, 0.4f);
}

TEST_F(TexNodeTest, CreateRadicalNode) {
    FontSpec font = {"cmr10", 10.0f, nullptr, 0};
    TexNode* radicand = make_char(arena, 'x', font);

    TexNode* radical = make_radical(arena, radicand, nullptr);

    ASSERT_NE(radical, nullptr);
    EXPECT_EQ(radical->node_class, NodeClass::Radical);
    EXPECT_EQ(radical->content.radical.radicand, radicand);
    EXPECT_EQ(radical->content.radical.degree, nullptr);
}

TEST_F(TexNodeTest, CreateScriptsNode) {
    FontSpec font = {"cmr10", 10.0f, nullptr, 0};
    TexNode* nucleus = make_char(arena, 'x', font);
    TexNode* subscript = make_char(arena, '2', font);
    TexNode* superscript = make_char(arena, 'n', font);

    TexNode* scripts = make_scripts(arena, nucleus, subscript, superscript);

    ASSERT_NE(scripts, nullptr);
    EXPECT_EQ(scripts->node_class, NodeClass::Scripts);
    EXPECT_EQ(scripts->content.scripts.nucleus, nucleus);
    EXPECT_EQ(scripts->content.scripts.subscript, subscript);
    EXPECT_EQ(scripts->content.scripts.superscript, superscript);
}

// ============================================================================
// Named Glue Tests
// ============================================================================

TEST_F(TexNodeTest, NamedGlues) {
    Glue hfil = hfil_glue();
    EXPECT_EQ(hfil.stretch_order, GlueOrder::Fil);
    EXPECT_FLOAT_EQ(hfil.stretch, 1.0f);

    Glue hfill = hfill_glue();
    EXPECT_EQ(hfill.stretch_order, GlueOrder::Fill);

    Glue hss = hss_glue();
    EXPECT_EQ(hss.stretch_order, GlueOrder::Fil);
    EXPECT_EQ(hss.shrink_order, GlueOrder::Fil);
}

// ============================================================================
// Traversal Tests
// ============================================================================

TEST_F(TexNodeTest, TraversePreorder) {
    TexNode* root = make_hlist(arena);
    FontSpec font = {"cmr10", 10.0f, nullptr, 0};

    TexNode* c1 = make_char(arena, 'A', font);
    TexNode* c2 = make_char(arena, 'B', font);

    root->append_child(c1);
    root->append_child(c2);

    int count = 0;
    traverse_preorder(root, [&count](TexNode* n) {
        count++;
    });

    EXPECT_EQ(count, 3);  // root + 2 children
}

// ============================================================================
// TFM Font Tests
// ============================================================================

TEST_F(TexNodeTest, BuiltinCMR10) {
    TFMFont* cmr10 = get_builtin_cmr10(arena);

    ASSERT_NE(cmr10, nullptr);
    EXPECT_STREQ(cmr10->name, "cmr10");
    EXPECT_FLOAT_EQ(cmr10->design_size, 10.0f);

    // Check character existence
    EXPECT_TRUE(cmr10->has_char('A'));
    EXPECT_TRUE(cmr10->has_char('a'));
    EXPECT_TRUE(cmr10->has_char('0'));

    // Check metrics are reasonable
    float w_A = cmr10->char_width('A');
    EXPECT_GT(w_A, 0);
    EXPECT_LT(w_A, 20.0f);  // Should be less than 2em

    float h_A = cmr10->char_height('A');
    EXPECT_GT(h_A, 0);
}

TEST_F(TexNodeTest, TFMFontManager) {
    TFMFontManager* mgr = create_font_manager(arena);

    ASSERT_NE(mgr, nullptr);

    // Get builtin font
    TFMFont* cmr10 = mgr->get_font("cmr10");
    ASSERT_NE(cmr10, nullptr);

    // Should return same instance
    TFMFont* cmr10_again = mgr->get_font("cmr10");
    EXPECT_EQ(cmr10, cmr10_again);
}

TEST_F(TexNodeTest, TFMScaledMetrics) {
    TFMFont* cmr10 = get_builtin_cmr10(arena);

    // At design size
    float w10 = cmr10->scaled_width('A', 10.0f);

    // At 12pt
    float w12 = cmr10->scaled_width('A', 12.0f);

    // Should scale proportionally
    EXPECT_NEAR(w12 / w10, 1.2f, 0.01f);
}

// ============================================================================
// HList Builder Tests
// ============================================================================

TEST_F(TexNodeTest, TextToHList_SingleWord) {
    TFMFontManager* fonts = create_font_manager(arena);
    HListContext ctx(arena, fonts);
    set_font(ctx, "cmr10", 10.0f);

    TexNode* hlist = text_to_hlist("Hello", 5, ctx);

    ASSERT_NE(hlist, nullptr);
    EXPECT_EQ(hlist->node_class, NodeClass::HList);

    // Count character nodes
    int char_count = 0;
    for (TexNode* n = hlist->first_child; n; n = n->next_sibling) {
        if (n->node_class == NodeClass::Char || n->node_class == NodeClass::Ligature) {
            char_count++;
        }
    }
    EXPECT_EQ(char_count, 5);  // H-e-l-l-o

    // Check total width is positive
    EXPECT_GT(hlist->width, 0);
}

TEST_F(TexNodeTest, TextToHList_WithSpaces) {
    TFMFontManager* fonts = create_font_manager(arena);
    HListContext ctx(arena, fonts);
    set_font(ctx, "cmr10", 10.0f);

    TexNode* hlist = text_to_hlist("Hello World", 11, ctx);

    ASSERT_NE(hlist, nullptr);

    // Should have glue between words
    int glue_count = 0;
    for (TexNode* n = hlist->first_child; n; n = n->next_sibling) {
        if (n->node_class == NodeClass::Glue) {
            glue_count++;
        }
    }
    EXPECT_EQ(glue_count, 1);
}

TEST_F(TexNodeTest, TextToHList_Ligatures) {
    TFMFontManager* fonts = create_font_manager(arena);
    HListContext ctx(arena, fonts);
    set_font(ctx, "cmr10", 10.0f);
    ctx.apply_ligatures = true;

    TexNode* hlist = text_to_hlist("difficult", 9, ctx);

    ASSERT_NE(hlist, nullptr);

    // "difficult" has "ffi" which could be fi + f or f + fi
    // Count ligatures
    int lig_count = 0;
    for (TexNode* n = hlist->first_child; n; n = n->next_sibling) {
        if (n->node_class == NodeClass::Ligature) {
            lig_count++;
        }
    }
    // Should have at least one ligature (fi)
    EXPECT_GE(lig_count, 1);
}

TEST_F(TexNodeTest, HListMeasurement) {
    TFMFontManager* fonts = create_font_manager(arena);
    HListContext ctx(arena, fonts);
    set_font(ctx, "cmr10", 10.0f);

    TexNode* hlist = text_to_hlist("Test", 4, ctx);

    HListDimensions dim = measure_hlist(hlist);

    EXPECT_GT(dim.width, 0);
    EXPECT_GT(dim.height, 0);
    EXPECT_GE(dim.depth, 0);
}

TEST_F(TexNodeTest, HListGlueSetting) {
    TFMFontManager* fonts = create_font_manager(arena);
    HListContext ctx(arena, fonts);
    set_font(ctx, "cmr10", 10.0f);

    TexNode* hlist = text_to_hlist("Hello World", 11, ctx);

    float natural_width = hlist->width;

    // Set to wider than natural
    float target = natural_width + 50.0f;
    float ratio = set_hlist_glue(hlist, target);

    EXPECT_GT(ratio, 0);  // Stretched
    EXPECT_FLOAT_EQ(hlist->width, target);
}

// ============================================================================
// Node Class Name Tests
// ============================================================================

TEST_F(TexNodeTest, NodeClassNames) {
    EXPECT_STREQ(node_class_name(NodeClass::Char), "Char");
    EXPECT_STREQ(node_class_name(NodeClass::HList), "HList");
    EXPECT_STREQ(node_class_name(NodeClass::Glue), "Glue");
    EXPECT_STREQ(node_class_name(NodeClass::Fraction), "Fraction");
}

// ============================================================================
// Line Breaking Tests
// ============================================================================

#include "lambda/tex/tex_linebreak.hpp"

TEST_F(TexNodeTest, LineBreakParams_Defaults) {
    LineBreakParams params = LineBreakParams::defaults();

    EXPECT_FLOAT_EQ(params.hsize, 468.0f);
    EXPECT_FLOAT_EQ(params.tolerance, 200.0f);
    EXPECT_EQ(params.line_penalty, 10);
    EXPECT_EQ(params.hyphen_penalty, 50);
}

TEST_F(TexNodeTest, ComputeBadness_ZeroExcess) {
    // No stretch needed
    int badness = compute_badness(0, 10.0f, 5.0f);
    EXPECT_EQ(badness, 0);
}

TEST_F(TexNodeTest, ComputeBadness_NormalStretch) {
    // Need to stretch by half the available
    int badness = compute_badness(5.0f, 10.0f, 5.0f);
    // 100 * 0.5^3 = 12.5
    EXPECT_GE(badness, 10);
    EXPECT_LE(badness, 15);
}

TEST_F(TexNodeTest, ComputeBadness_Overfull) {
    // Need more shrink than available
    int badness = compute_badness(-10.0f, 5.0f, 5.0f);
    EXPECT_GT(badness, INF_BAD);  // Overfull
}

TEST_F(TexNodeTest, ComputeFitness) {
    EXPECT_EQ(compute_fitness(-0.6f), Fitness::Tight);
    EXPECT_EQ(compute_fitness(0.0f), Fitness::Normal);
    EXPECT_EQ(compute_fitness(0.7f), Fitness::Loose);
    EXPECT_EQ(compute_fitness(1.5f), Fitness::VeryLoose);
}

TEST_F(TexNodeTest, ComputeDemerits_Basic) {
    // Basic case: badness=0, no penalty
    int dem = compute_demerits(0, 0, 10, Fitness::Normal, Fitness::Normal, 10000);
    // (10 + 0)^2 + 0 = 100
    EXPECT_EQ(dem, 100);
}

TEST_F(TexNodeTest, ComputeDemerits_FitnessMismatch) {
    // Fitness changes from Tight to VeryLoose (diff > 1)
    int dem1 = compute_demerits(0, 0, 10, Fitness::Normal, Fitness::Normal, 10000);
    int dem2 = compute_demerits(0, 0, 10, Fitness::Tight, Fitness::VeryLoose, 10000);
    EXPECT_GT(dem2, dem1 + 9000);  // Should add adj_demerits
}

TEST_F(TexNodeTest, GetLineWidth_Default) {
    LineBreakParams params = LineBreakParams::defaults();
    EXPECT_FLOAT_EQ(get_line_width(1, params), params.hsize);
    EXPECT_FLOAT_EQ(get_line_width(2, params), params.hsize);
}

TEST_F(TexNodeTest, GetLineIndent_FirstLine) {
    LineBreakParams params = LineBreakParams::defaults();
    EXPECT_FLOAT_EQ(get_line_indent(1, params), params.par_indent);
    EXPECT_FLOAT_EQ(get_line_indent(2, params), 0);
}

TEST_F(TexNodeTest, BreakParagraph_SimpleText) {
    // Build a simple paragraph
    TFMFontManager* fonts = create_font_manager(arena);
    HListContext ctx(arena, fonts);
    set_font(ctx, "cmr10", 10.0f);

    // Create text that should break into multiple lines
    const char* text = "This is a test paragraph that should be long enough to require line breaking when set in a narrow column width.";
    TexNode* hlist = text_to_hlist(text, strlen(text), ctx);

    // Use narrow width to force breaks
    LineBreakParams params = LineBreakParams::defaults();
    params.hsize = 150.0f;  // Narrow column
    params.par_indent = 0;  // No indent for testing
    params.pretolerance = 10000.0f;  // Very high tolerance for testing
    params.tolerance = 10000.0f;      // Accept anything

    LineBreakResult result = break_paragraph(hlist, params, arena);

    EXPECT_TRUE(result.success);
    EXPECT_GT(result.line_count, 1);  // Should need multiple lines
    EXPECT_LT(result.total_demerits, AWFUL_BAD);
}

TEST_F(TexNodeTest, BreakParagraph_SingleWord) {
    TFMFontManager* fonts = create_font_manager(arena);
    HListContext ctx(arena, fonts);
    set_font(ctx, "cmr10", 10.0f);

    TexNode* hlist = text_to_hlist("Hello", 5, ctx);

    LineBreakParams params = LineBreakParams::defaults();
    params.hsize = 200.0f;

    LineBreakResult result = break_paragraph(hlist, params, arena);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.line_count, 1);  // Single word fits on one line
}

TEST_F(TexNodeTest, TypesetParagraph_Integration) {
    TFMFontManager* fonts = create_font_manager(arena);
    HListContext ctx(arena, fonts);
    set_font(ctx, "cmr10", 10.0f);

    const char* text = "The quick brown fox jumps over the lazy dog. This classic pangram contains every letter of the alphabet.";
    TexNode* hlist = text_to_hlist(text, strlen(text), ctx);

    LineBreakParams params = LineBreakParams::defaults();
    params.hsize = 200.0f;
    params.par_indent = 15.0f;
    params.pretolerance = 10000.0f;  // High tolerance for testing
    params.tolerance = 10000.0f;

    TexNode* vlist = typeset_paragraph(hlist, params, 12.0f, arena);

    ASSERT_NE(vlist, nullptr);
    EXPECT_EQ(vlist->node_class, NodeClass::VList);
    EXPECT_GT(vlist->height, 0);

    // Count lines (first-level children that are HBox)
    int line_count = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (c->node_class == NodeClass::HBox) {
            line_count++;
        }
    }
    EXPECT_GT(line_count, 1);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    log_init("log.conf");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
