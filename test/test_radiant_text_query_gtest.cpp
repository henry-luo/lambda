// Lambda text-measurement queries made through the radiant module, loaded
// in-process so the module's query caches are observable.
#include <gtest/gtest.h>
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/hyphenation.hpp"
#include "../radiant/render.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom.h"
#include "../lib/tagged.hpp"

extern "C" uint64_t radiant_query_font_build_count(void);

TEST(TextWhitespace, UnicodeCodepointsDoNotAliasAsciiWhitespace) {
    EXPECT_TRUE(is_space(0x000A));
    EXPECT_TRUE(is_space(0x000D));
    EXPECT_TRUE(is_space(0x0020));
    EXPECT_FALSE(is_space(0x200A));
    EXPECT_FALSE(is_space(0x200D));
    EXPECT_FALSE(is_space(0x2020));
    EXPECT_FALSE(is_space(0x300A));
}

TEST(TextWhitespace, NativeLineBoundariesUseSourceBytesAndKeepTerminalEdgesClosed) {
#ifdef __APPLE__
    const char* thai = "ภาษาไทยภาษาไทย";
    LayoutTextBreaks boundaries(thai, strlen(thai), true);
    EXPECT_TRUE(boundaries.allows_after(strlen("ภาษา")));
    EXPECT_TRUE(boundaries.allows_after(strlen("ภาษาไทย")));
    EXPECT_FALSE(boundaries.allows_after(strlen("ภา")));
    EXPECT_FALSE(boundaries.allows_after(strlen(thai)));
    const char* supplementary = "😀ภาษาไทย";
    LayoutTextBreaks shifted(supplementary, strlen(supplementary), true);
    EXPECT_TRUE(shifted.allows_after(strlen("😀ภาษา")));
    LayoutTextBreaks disabled(thai, strlen(thai), false);
    EXPECT_FALSE(disabled.allows_after(strlen("ภาษา")));
    const char* khmer = "ភាសាខ្មែរភាសាខ្មែរ";
    LayoutTextBreaks complex(khmer, strlen(khmer), true);
    EXPECT_TRUE(complex.allows_after(strlen("ភាសាខ្មែរ")));
    const char* mixed = "中文english中文";
    LayoutTextBreaks cjk(mixed, strlen(mixed), true);
    const unsigned char* mixed_start = (const unsigned char*)mixed;
    const unsigned char* mixed_end = mixed_start + strlen(mixed);
    size_t latin_edge = strlen("中文english");
    EXPECT_TRUE(layout_text_native_break_allowed(cjk, latin_edge, 'h',
        mixed_start + latin_edge, mixed_end, false));
    EXPECT_FALSE(layout_text_native_break_allowed(cjk, latin_edge, 'h',
        mixed_start + latin_edge, mixed_end, true));
    const char* url = "前 URL/path";
    LayoutTextBreaks punctuation(url, strlen(url), true);
    const unsigned char* url_start = (const unsigned char*)url;
    size_t slash = strlen("前 URL/");
    EXPECT_FALSE(layout_text_native_break_allowed(punctuation, slash, '/',
        url_start + slash, url_start + strlen(url), false));
    const char* jamo = "애기판다";
    LayoutTextBreaks korean(jamo, strlen(jamo), true);
    const unsigned char* jamo_start = (const unsigned char*)jamo;
    size_t syllable = strlen("애");
    EXPECT_FALSE(layout_text_native_break_allowed(korean, syllable, 0x1162,
        jamo_start + syllable, jamo_start + strlen(jamo), true));
#else
    GTEST_SKIP() << "native Unicode line-break resource is unavailable";
#endif
}

TEST(TextWhitespace, PreservedBreaksAndControlsAreNotCollapsibleSpaces) {
    DomElement parent = {};
    parent.node_type = DOM_NODE_ELEMENT;
    BlockProp properties = BLOCK_PROP_DEFAULT;
    parent.blk = lam::view_prop(&properties);
    DomText text = {};
    text.node_type = DOM_NODE_TEXT;
    text.parent = lam::up(&parent);
    struct Case {
        const char* content;
        CssEnum white_space;
        bool collapsible;
    } cases[] = {
        {" \n", CSS_VALUE_NORMAL, true},
        {" \n", CSS_VALUE_PRE_LINE, false},
        {" \t", CSS_VALUE_PRE_LINE, true},
        {"\r", CSS_VALUE_PRE_LINE, true},
        {"\f", CSS_VALUE_NORMAL, false},
        {" ", CSS_VALUE_PRE_WRAP, false},
    };
    for (const Case& entry : cases) {
        properties.white_space = entry.white_space;
        text.text = lam::up(entry.content);
        text.length = strlen(entry.content);
        EXPECT_EQ(layout_text_is_collapsible_whitespace(&text), entry.collapsible)
            << entry.content;
    }
    properties.white_space = CSS_VALUE_PRE_LINE;
    text.text = lam::up(" \n");
    text.length = 2;
    EXPECT_TRUE(layout_text_is_collapsible_whitespace(&text, 0, 1));
    EXPECT_FALSE(layout_text_is_collapsible_whitespace(&text, 1, 1));
    EXPECT_FALSE(layout_text_is_collapsible_whitespace(&text, 0, 3));
}

TEST(TextWhitespace, ExtendedEmojiClustersRetainTheirSourceBoundaries) {
    const char* cases[] = {"🤹‍♀️", "⛹🏿‍♀️", "🏳️‍🌈", "😀‍😀"};
    for (const char* source : cases) {
        size_t length = 0;
        const unsigned char* start = (const unsigned char*)source;
        ASSERT_TRUE(layout_get_grapheme_cluster_bytes(start, start + strlen(source), &length));
        EXPECT_EQ(length, strlen(source));
    }
    EXPECT_TRUE(text_codepoint_is_extended_pictographic(0x1F939));
    EXPECT_TRUE(text_codepoint_is_extended_pictographic(0x26F9));
    EXPECT_FALSE(text_codepoint_is_extended_pictographic('A'));
}

TEST(TextJustification, WhollyHangingFragmentsDoNotConsumeJustificationOpportunities) {
    DomElement parent = {};
    parent.node_type = DOM_NODE_ELEMENT;
    BlockProp properties = BLOCK_PROP_DEFAULT;
    parent.blk = lam::view_prop(&properties);
    DomText text = {};
    text.node_type = DOM_NODE_TEXT;
    text.view_type = RDT_VIEW_TEXT;
    text.parent = lam::up(&parent);
    struct Case {
        const char* text;
        CssEnum white_space;
        float hanging_width;
        int expected;
    } cases[] = {
        {"   ", CSS_VALUE_PRE_WRAP, 60.0f, 0},
        {" \t", CSS_VALUE_PRE_WRAP, 80.0f, 0},
        {"   ", CSS_VALUE_PRE_WRAP, 0.0f, 3},
        {"X X ", CSS_VALUE_PRE_WRAP, 0.0f, 2},
        {"X X  ", CSS_VALUE_BREAK_SPACES, 0.0f, 3},
    };
    for (const Case& entry : cases) {
        properties.white_space = entry.white_space;
        text.text = lam::up(entry.text);
        text.length = strlen(entry.text);
        TextRect rect = {};
        rect.width = entry.hanging_width;
        rect.length = (int)text.length;
        rect.hanging_trim = entry.hanging_width;
        EXPECT_EQ(count_rendered_justify_opportunities(
            lam::view_require_text(&text), &rect, true), entry.expected) << entry.text;
        // painting uses the same opportunities without requesting collapsible trimming.
        EXPECT_EQ(count_rendered_justify_opportunities(
            lam::view_require_text(&text), &rect, false), entry.expected) << entry.text;
    }
}

TEST(TextJustification, ClusteredScriptGapsRespectGraphemeBoundaries) {
    const char* text = u8"សាហា";
    EXPECT_EQ(count_justify_opportunities(text, (int)strlen(text)), 1);
    TextJustifyContext context = {};
    EXPECT_FALSE(text_justify_character_gap(&context, 0x179F));
    EXPECT_FALSE(text_justify_character_gap(&context, 0x17B6));
    EXPECT_TRUE(text_justify_character_gap(&context, 0x17A0));
    EXPECT_FALSE(text_justify_character_gap(&context, 0x17B6));
    EXPECT_FALSE(text_justify_character_gap(&context, ' '));
    EXPECT_FALSE(text_justify_character_gap(&context, 0x179F));
}

class RadiantTextQuery : public ::testing::Test {
protected:
    UiContext ui{};
    DomDocument* document = nullptr;
    void SetUp() override { ASSERT_EQ(ui_context_init(&ui, true, 1), 0); }
    void TearDown() override {
        ui.document = nullptr;
        if (document) free_document(document);
        ui_context_cleanup(&ui);
    }
    DomElement* load(const char* path) {
        DocumentJsHostConfig host = {}; host.ui_context = &ui; host.resource_policy = INPUT_RESOURCE_LOCAL_ONLY;
        Url* base = get_current_dir();
        if (!base) return nullptr;
        document = load_html_doc(base, (char*)path, 320, 200, &host);
        url_destroy(base);
        if (!document || !document->root) return nullptr;
        ui.document = lam::up(document);
        return dom_find_element_by_id(document->root->as_element(), "result");
    }
};

TEST_F(RadiantTextQuery, PreservedWhitespaceUsesHangingAndCrossNodeBreakState) {
    ASSERT_NE(load("test/html/layout_preserved_whitespace.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Expected {
        const char* id;
        float width;
        float height;
    } cases[] = {
        {"green", 80.0f, 20.0f},
        {"tabs", 40.0f, 140.0f},
        {"spaces", 40.0f, 80.0f},
        {"unbroken", 40.0f, 120.0f},
        {"newline", 0.0f, 20.0f},
        {"separate-newline", 0.0f, 20.0f},
    };
    for (const Expected& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, NativeWordBoundariesAlsoSizeIntrinsicRuns) {
#ifdef __APPLE__
    ASSERT_NE(load("test/html/layout_native_line_boundaries.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; } cases[] = {
        {"wrapped", 72.0f}, {"nowrap", 36.0f}, {"keep", 72.0f},
        {"minimum", 144.0f}, {"manual", 144.0f},
        {"khmer", 72.0f}, {"khmer-manual", 72.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
    DomElement* automatic = dom_find_element_by_id(document->root->as_element(), "minimum");
    DomElement* manual = dom_find_element_by_id(document->root->as_element(), "manual");
    ASSERT_NE(automatic, nullptr);
    ASSERT_NE(manual, nullptr);
    EXPECT_FLOAT_EQ(automatic->width, manual->width);
#else
    GTEST_SKIP() << "native Unicode line-break resource is unavailable";
#endif
}

TEST_F(RadiantTextQuery, FixedColumnRowsCountGuttersInTheirPhysicalPitch) {
    ASSERT_NE(load("test/html/layout_multicol_rows.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    struct Expected { const char* id; float height; } cases[] = {
        {"default", 100.0f}, {"explicit", 100.0f}, {"styled", 100.0f},
        {"two", 40.0f}, {"auto", 100.0f}, {"short", 100.0f}, {"zero", 80.0f},
    };
    for (const Expected& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, BalancedColumnSetsKeepRowTracksAndPartialContinuations) {
    ASSERT_NE(load("test/html/layout_multicol_balanced_rows.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 400;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; } cases[] = {
        {"finite", 50.0f}, {"finite-child", 100.0f},
        {"fixed", 100.0f}, {"fixed-child", 80.0f},
        {"spanners", 110.0f}, {"before", 35.0f}, {"after", 55.0f},
        {"fractional", 82.5f}, {"fractional-child", 63.75f},
        {"nested-fixed", 100.0f}, {"nested-small", 80.0f}, {"nested-fractional", 17.5f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
    DomElement* child = dom_find_element_by_id(document->root->as_element(), "fractional-child");
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->layout_fragments_count(), 9);
    LayoutFragmentBox* last = child->layout_fragment_list();
    ASSERT_NE(last, nullptr);
    while (last->next) last = last->next;
    EXPECT_FLOAT_EQ(last->y, 60.0f);
    EXPECT_FLOAT_EQ(last->height, 3.75f);
    DomElement* nested = dom_find_element_by_id(document->root->as_element(), "nested-fractional");
    ASSERT_NE(nested, nullptr);
    EXPECT_FLOAT_EQ(nested->width, 90.0f);
    EXPECT_EQ(nested->layout_fragments_count(), 8);
    for (LayoutFragmentBox* fragment = nested->layout_fragment_list(); fragment; fragment = fragment->next) {
        EXPECT_FLOAT_EQ(fragment->height, 7.5f);
    }
}

TEST_F(RadiantTextQuery, VerticalBreakEdgesFollowTheParagraphDirection) {
    ASSERT_NE(load("test/html/layout_multicol_rtl_break_edge.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* rtl = dom_find_element_by_id(document->root->as_element(), "rtl-break");
    DomElement* ltr = dom_find_element_by_id(document->root->as_element(), "ltr-break");
    ASSERT_NE(rtl, nullptr);
    ASSERT_NE(ltr, nullptr);
    EXPECT_FLOAT_EQ(view_geometry_node_viewport_origin_resolved(rtl, nullptr, nullptr).y, 0.0f);
    EXPECT_FLOAT_EQ(view_geometry_node_viewport_origin_resolved(ltr, nullptr, nullptr).y, 80.0f);
    DomElement* nested_end = dom_find_element_by_id(document->root->as_element(), "nested-end");
    ASSERT_NE(nested_end, nullptr);
    EXPECT_FLOAT_EQ(view_geometry_node_viewport_origin_resolved(nested_end, nullptr, nullptr).y, 40.0f);
}

TEST_F(RadiantTextQuery, SpannersResolveAuthoredWidthsAgainstTheFullContainer) {
    ASSERT_NE(load("test/html/layout_multicol_spanner_width.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; } cases[] = {
        {"content", 95.0f}, {"border", 75.0f}, {"percentage", 70.0f},
        {"maximum", 80.0f}, {"minimum", 90.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
    }
}

TEST_F(RadiantTextQuery, ForcedEmptyLinesAndOverflowColumnsRetainTheirFlow) {
    ASSERT_NE(load("test/html/layout_multicol_forced_lines.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 400;
    layout_html_doc(&ui, document, false);
    DomElement* empty = dom_find_element_by_id(document->root->as_element(), "empty");
    DomElement* nested = dom_find_element_by_id(document->root->as_element(), "nested");
    DomElement* third = dom_find_element_by_id(document->root->as_element(), "third");
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(nested, nullptr);
    ASSERT_NE(third, nullptr);
    EXPECT_FLOAT_EQ(empty->height, 65.0f);
    EXPECT_FLOAT_EQ(nested->height, 60.0f);
    EXPECT_FLOAT_EQ(third->x, 100.0f);
    EXPECT_FLOAT_EQ(third->y, 0.0f);
}

TEST_F(RadiantTextQuery, EmphasisLineExtentDoesNotDependOnClipping) {
    ASSERT_NE(load("test/html/layout_emphasis_line_extent.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; } cases[] = {
        {"plain", 20.0f}, {"over", 30.0f}, {"under", 30.0f},
        {"clipped", 30.0f}, {"spaces", 30.0f}, {"punctuation", 30.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, AbsoluteParallelFlowsUseColumnsWithoutFragmentingTheirContainingBlock) {
    ASSERT_NE(load("test/html/layout_multicol_parallel_flows.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 500;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"long", 0.0f, 0.0f, 100.0f, 100.0f},
        {"long-child", 0.0f, 0.0f, 100.0f, 100.0f},
        {"static", 50.0f, 0.0f, 50.0f, 100.0f},
        {"offset", 0.0f, 0.0f, 100.0f, 100.0f},
        {"scroll", 0.0f, 0.0f, 20.0f, 200.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(element->y, entry.y) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, ClusterShapingRetainsTrackingInLayoutAndIntrinsicSizing) {
#ifdef __APPLE__
    ASSERT_NE(load("test/html/layout_cluster_tracking.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 400;
    layout_html_doc(&ui, document, false);
    DomElement* plain = dom_find_element_by_id(document->root->as_element(), "plain");
    DomElement* tracked = dom_find_element_by_id(document->root->as_element(), "tracked");
    DomElement* intrinsic_plain = dom_find_element_by_id(document->root->as_element(), "intrinsic-plain");
    DomElement* intrinsic_tracked = dom_find_element_by_id(document->root->as_element(), "intrinsic-tracked");
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(tracked, nullptr);
    ASSERT_NE(intrinsic_plain, nullptr);
    ASSERT_NE(intrinsic_tracked, nullptr);
    ASSERT_NE(plain->first_child->as_text()->rect, nullptr);
    ASSERT_NE(tracked->first_child->as_text()->rect, nullptr);
    EXPECT_NEAR(tracked->first_child->as_text()->rect->width -
        plain->first_child->as_text()->rect->width, 40.0f, 0.01f);
    EXPECT_NEAR(intrinsic_tracked->width - intrinsic_plain->width, 40.0f, 0.01f);
#else
    GTEST_SKIP() << "native Khmer shaping is unavailable";
#endif
}

TEST_F(RadiantTextQuery, EmergencyMinContentRetainsWholeShapedClusters) {
#ifdef __APPLE__
    ASSERT_NE(load("test/html/layout_cluster_min_content.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 400;
    layout_html_doc(&ui, document, false);
    DomElement* reference = dom_find_element_by_id(document->root->as_element(), "reference");
    DomElement* anywhere = dom_find_element_by_id(document->root->as_element(), "anywhere");
    DomElement* legacy = dom_find_element_by_id(document->root->as_element(), "legacy");
    DomElement* ordinary = dom_find_element_by_id(document->root->as_element(), "ordinary");
    ASSERT_NE(reference, nullptr);
    ASSERT_NE(anywhere, nullptr);
    ASSERT_NE(legacy, nullptr);
    ASSERT_NE(ordinary, nullptr);
    EXPECT_FLOAT_EQ(anywhere->width, reference->width);
    EXPECT_FLOAT_EQ(legacy->width, reference->width);
    EXPECT_FLOAT_EQ(ordinary->width, 2.0f * reference->width - 4.0f);
#else
    GTEST_SKIP() << "native Tamil shaping is unavailable";
#endif
}

TEST_F(RadiantTextQuery, NarrowSpaceSynthesisUsesTheSelectedFacesSpaceMetric) {
    ASSERT_NE(load("test/html/layout_narrow_space_metrics.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* regular = dom_find_element_by_id(document->root->as_element(), "regular");
    DomElement* narrow = dom_find_element_by_id(document->root->as_element(), "narrow");
    DomElement* pair = dom_find_element_by_id(document->root->as_element(), "pair");
    ASSERT_NE(regular, nullptr);
    ASSERT_NE(narrow, nullptr);
    ASSERT_NE(pair, nullptr);
    EXPECT_FLOAT_EQ(regular->width, 20.0f);
    EXPECT_FLOAT_EQ(narrow->width, 10.0f);
    EXPECT_FLOAT_EQ(pair->width, 50.0f);
}

TEST_F(RadiantTextQuery, ColumnSpanningRequiresAnInFlowBlockLevelBox) {
    ASSERT_NE(load("test/html/layout_multicol_spanner_eligibility.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    DomElement* inline_box = dom_find_element_by_id(document->root->as_element(), "inline");
    DomElement* block = dom_find_element_by_id(document->root->as_element(), "block");
    ASSERT_NE(inline_box, nullptr);
    ASSERT_NE(block, nullptr);
    EXPECT_FALSE(multicol_is_spanner_block(lam::view_require_block(inline_box)));
    EXPECT_TRUE(multicol_is_spanner_block(lam::view_require_block(block)));
    EXPECT_FLOAT_EQ(inline_box->width, 50.0f);
    EXPECT_FLOAT_EQ(inline_box->height, 50.0f);
    EXPECT_FLOAT_EQ(block->width, 100.0f);
    EXPECT_FLOAT_EQ(block->height, 25.0f);
}

TEST_F(RadiantTextQuery, InlineBoundariesDoNotCreateMinContentBreaksBeforePunctuation) {
    ASSERT_NE(load("test/html/layout_inline_min_content_boundaries.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; } cases[] = {
        {"punctuation", 40.0f}, {"letters", 20.0f},
        {"separator", 20.0f}, {"nested", 40.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
    }
}

TEST_F(RadiantTextQuery, EmojiCompositionUsesFontAdvancesWithoutSplittingClusters) {
#ifdef __APPLE__
    ASSERT_NE(load("test/html/layout_emoji_cluster_advances.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    const char* ids[] = {"juggle", "basket"};
    for (const char* id : ids) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, 30.0f) << id;
        DomText* text = element->first_child->as_text();
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->width, 30.0f);
        EXPECT_EQ(text->rect->next, nullptr);
    }
    DomElement* rainbow = dom_find_element_by_id(document->root->as_element(), "rainbow");
    DomElement* unsupported = dom_find_element_by_id(document->root->as_element(), "unsupported");
    DomElement* separate = dom_find_element_by_id(document->root->as_element(), "separate");
    ASSERT_NE(rainbow, nullptr);
    ASSERT_NE(unsupported, nullptr);
    ASSERT_NE(separate, nullptr);
    EXPECT_FLOAT_EQ(rainbow->width, 30.0f);
    EXPECT_FLOAT_EQ(unsupported->width, separate->width);
    EXPECT_FLOAT_EQ(unsupported->width, 60.0f);
#else
    GTEST_SKIP() << "native emoji shaping is unavailable";
#endif
}

TEST_F(RadiantTextQuery, FloatClearanceRetainsTheFirstFormattedLine) {
    ASSERT_NE(load("test/html/layout_float_first_line.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    const char* ids[] = {"direct", "inline", "pseudo", "partial", "later"};
    for (const char* id : ids) {
        DomElement* container = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(container, nullptr);
        DomElement* owner = container;
        if (strcmp(id, "inline") == 0) owner = container->first_child->as_element();
        DomText* text = nullptr;
        for (DomNode* child = owner->first_child; child; child = child->next_sibling) {
            if (child->is_text() && child->as_text()->rect) { text = child->as_text(); break; }
        }
        ASSERT_NE(text, nullptr) << id;
        EXPECT_FLOAT_EQ(text->rect->x, 50.0f) << id;
        EXPECT_FLOAT_EQ(text->rect->y, 50.0f) << id;
        EXPECT_FLOAT_EQ(text->rect->width, 50.0f) << id;
        EXPECT_FLOAT_EQ(text->rect->height, 50.0f) << id;
        if (strcmp(id, "later") == 0) {
            DomText* later = container->last_child->as_text();
            ASSERT_NE(later, nullptr);
            ASSERT_NE(later->rect, nullptr);
            EXPECT_FLOAT_EQ(later->rect->x, 0.0f);
            EXPECT_FLOAT_EQ(later->rect->y, 100.0f);
        }
    }
}

TEST_F(RadiantTextQuery, SegmentBreakContextCrossesOnlyNonAtomicInlineBoundaries) {
    ASSERT_NE(load("test/html/layout_segment_break_context.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; } cases[] = {
        {"previous", 40.0f, 0.0f}, {"decorated", 60.0f, 0.0f},
        {"following", 40.0f, 0.0f}, {"positioned", 40.0f, 0.0f},
        {"floated", 40.0f, 0.0f}, {"hidden", 40.0f, 0.0f},
        {"atomic", 80.0f, 4.0f}, {"ordinary", 100.0f, 0.0f},
        {"preserved", 0.0f, 20.0f},
        {"cjk-unknown", 60.0f, 0.0f}, {"cjk-known", 60.0f, 0.0f},
        {"cjk-zwsp", 40.0f, 0.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(element->y, entry.y) << entry.id;
    }
    struct StaticCase { const char* id; float x; float y; } static_cases[] = {
        {"static-block", 0.0f, 20.0f}, {"static-grown", 0.0f, 40.0f},
        {"static-first", 0.0f, 0.0f}, {"explicit-top", 0.0f, 7.0f},
        {"static-inline", 40.0f, 0.0f},
    };
    for (const StaticCase& entry : static_cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(element->y, entry.y) << entry.id;
    }
}

TEST_F(RadiantTextQuery, PreservedSpacesHangConditionallyBeforeForcedBreaks) {
    ASSERT_NE(load("test/html/layout_conditional_hanging.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float width; } cases[] = {
        {"fitting", 60.0f, 40.0f}, {"overflowing", 0.0f, 140.0f},
        {"lf", 60.0f, 40.0f}, {"br", 60.0f, 40.0f},
        {"soft", 0.0f, 80.0f}, {"normal", 80.0f, 20.0f},
        {"break-spaces", 60.0f, 40.0f}, {"block", 60.0f, 40.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        DomText* text = element->first_child->as_text();
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->width, entry.width) << entry.id;
        if (strcmp(entry.id, "soft") == 0) {
            TextRect* last = text->rect;
            while (last->next) last = last->next;
            EXPECT_FLOAT_EQ(last->x, 20.0f);
            EXPECT_FLOAT_EQ(last->width, 40.0f);
        }
    }
}

TEST_F(RadiantTextQuery, EqualHeightShiftedDescendantsRetainTheirAncestorsBaseline) {
    ASSERT_NE(load("test/html/layout_nested_baseline_boxes.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* row; const char* outer; const char* middle; } cases[] = {
        {"super-row", "super-outer", "super-middle"},
        {"sub-row", "sub-outer", "sub-middle"},
        {"baseline-row", "baseline-outer", nullptr},
    };
    for (const Case& entry : cases) {
        DomElement* row = dom_find_element_by_id(document->root->as_element(), entry.row);
        DomElement* outer = dom_find_element_by_id(document->root->as_element(), entry.outer);
        ASSERT_NE(row, nullptr);
        ASSERT_NE(outer, nullptr);
        DomText* unshifted = row->first_child->as_text();
        ASSERT_NE(unshifted, nullptr);
        ASSERT_NE(unshifted->rect, nullptr);
        EXPECT_FLOAT_EQ(outer->y, unshifted->rect->y) << entry.outer;
        EXPECT_FLOAT_EQ(outer->height, unshifted->rect->height) << entry.outer;
        if (entry.middle) {
            DomElement* middle = dom_find_element_by_id(document->root->as_element(), entry.middle);
            ASSERT_NE(middle, nullptr);
            EXPECT_FLOAT_EQ(middle->y, outer->y) << entry.middle;
        }
    }
}

TEST_F(RadiantTextQuery, HyphensNoneKeepsConditionalCharactersUnbreakable) {
    ASSERT_NE(load("test/html/layout_hyphens_none.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; float first_width; } cases[] = {
        {"none", 20.0f, 120.0f}, {"inherited", 20.0f, 120.0f},
        {"later-none", 40.0f, 20.0f}, {"literal", 40.0f, 80.0f}, {"manual", 40.0f, 80.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
        DomElement* owner = strcmp(entry.id, "inherited") == 0
            ? element->first_child->as_element() : element;
        ASSERT_NE(owner, nullptr);
        DomText* text = owner->first_child->as_text();
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->width, entry.first_width) << entry.id;
    }
}

TEST_F(RadiantTextQuery, ConditionalBreaksRetainFittingMarksAndContinuationPoints) {
    ASSERT_NE(load("test/html/layout_hyphens_none.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; } cases[] = {
        {"manual-double", 100.0f}, {"automatic", 60.0f}, {"empty-mark", 40.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
    EXPECT_FALSE(layout_hyphenation_language_supported(""));
    EXPECT_EQ(layout_hyphenation_next_break("example", 7, 0, "und"), SIZE_MAX);
    if (layout_hyphenation_language_supported("de-DE")) {
        EXPECT_EQ(layout_hyphenation_next_break("Donaudampfschifffahrt", 20, 0, "de-DE"),
                  layout_hyphenation_next_break("Donaudampfschifffahrt", 20, 0, "De-dE"));
        DomElement* german = dom_find_element_by_id(document->root->as_element(), "german");
        ASSERT_NE(german, nullptr);
        EXPECT_FLOAT_EQ(german->height, 80.0f);
    }
}

TEST_F(RadiantTextQuery, OversizedFloatsCannotUseSpaceOutsideTheirContainingBlock) {
    ASSERT_NE(load("test/html/layout_oversized_floats.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; } cases[] = {
        {"zero-left-first", 0.0f, 0.0f}, {"zero-left-second", 0.0f, 20.0f},
        {"narrow-left-first", 0.0f, 0.0f}, {"narrow-left-second", 0.0f, 20.0f},
        {"narrow-right-first", -10.0f, 0.0f}, {"narrow-right-second", -10.0f, 20.0f},
        {"wide-left-first", 0.0f, 0.0f}, {"wide-left-second", 60.0f, 0.0f},
        {"wide-right-first", 80.0f, 0.0f}, {"wide-right-second", 20.0f, 0.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(element->y, entry.y) << entry.id;
    }
}

TEST_F(RadiantTextQuery, ZeroFontSizePreservesBreaksAndLengthTabStops) {
    ASSERT_NE(load("test/html/layout_zero_font_controls.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; } cases[] = {
        {"after-lf", 0.0f, 20.0f}, {"after-length-tab", 100.0f, 0.0f},
        {"after-number-tab", 40.0f, 0.0f}, {"after-zero-glyphs", 40.0f, 0.0f},
        {"empty-first", 0.0f, 0.0f}, {"empty-second", 50.0f, 0.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(element->y, entry.y) << entry.id;
    }
}

TEST_F(RadiantTextQuery, TablesMeasurePreservedLinesAndInlineContinuations) {
    ASSERT_NE(load("test/html/layout_table_preserved_lines.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Expected {
        const char* id;
        float width;
        float height;
    } cases[] = {
        {"direct", 80.0f, 40.0f},
        {"split", 60.0f, 40.0f},
        {"siblings", 60.0f, 40.0f},
        {"tabs", 100.0f, 40.0f},
        {"soft-wrap", 60.0f, 60.0f},
        {"spaces", 80.0f, 40.0f},
        {"atomic", 100.0f, 40.0f},
        {"indent", 80.0f, 40.0f},
        {"later-line", 80.0f, 40.0f},
    };
    for (const Expected& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, InlineShapingPreservesJoiningAndRequiredBoundaries) {
    ASSERT_NE(load("test/html/layout_inline_shaping.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* plain = dom_find_element_by_id(document->root->as_element(), "plain");
    ASSERT_NE(plain, nullptr);
    FontHandle* handle = plain->fontp()->font_handle;
    ASSERT_NE(handle, nullptr);
    TextExtents probe = {};
    if (!font_measure_text_range(handle, "ععع", 6, 2, 2, &probe)) {
        GTEST_SKIP() << "the font backend does not support contextual ranges";
    }
    struct Case { const char* id; const char* form; float padding; } cases[] = {
        {"plain-middle", "ﻌ", 0.0f}, {"color-middle", "ﻌ", 0.0f},
        {"joined-middle", "ﻌ", 0.0f}, {"zero-middle", "ﻌ", 0.0f},
        {"padding-middle", "ﻋ", 20.0f}, {"isolate-middle", "ع", 0.0f},
        {"auto-middle", "ع", 0.0f}, {"raised-middle", "ع", 0.0f},
        {"size-middle", "ﻌ", 0.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        ASSERT_NE(element->fontp()->font_handle, nullptr);
        // explicit presentation forms provide an independent glyph-advance reference.
        float expected = font_measure_text(element->fontp()->font_handle,
            entry.form, (int)strlen(entry.form)).width;
        EXPECT_NEAR(element->width, expected + entry.padding, 0.1f) << entry.id;
    }
    float total = 0.0f;
    const char* forms[] = {"ﻋ", "ﻌ", "ﻊ"};
    for (size_t index = 0; index < 3; index++) {
        TextExtents range = {};
        ASSERT_TRUE(font_measure_text_range(handle, "ععع", 6, (int)(index * 2), 2, &range));
        EXPECT_NEAR(range.width, font_measure_text(handle, forms[index], 3).width, 0.02f);
        total += range.width;
    }
    EXPECT_NEAR(total, font_measure_text(handle, "ععع", 6).width, 0.02f);
    EXPECT_FALSE(font_measure_text_range(handle, "ععع", 6, 1, 2, &probe));
    EXPECT_FALSE(font_measure_text_range(handle, "ععع", 6, 4, 3, &probe));
    struct LigatureCase { const char* id; const char* text; float decoration; } ligatures[] = {
        {"ligature-middle", "", 0.0f}, {"ligature-padding", "fi", 10.0f},
        {"ligature-border", "", 10.0f},
    };
    for (const LigatureCase& entry : ligatures) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float expected = font_measure_text(element->fontp()->font_handle,
            entry.text, (int)strlen(entry.text)).width + entry.decoration;
        EXPECT_NEAR(element->width, expected, 0.1f) << entry.id;
    }
    DomElement* empty_boundary = dom_find_element_by_id(document->root->as_element(), "empty-boundary");
    ASSERT_NE(empty_boundary, nullptr);
    DomText* first = empty_boundary->first_child->as_text();
    DomText* last = empty_boundary->last_child->as_text();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(last, nullptr);
    ASSERT_NE(first->rect, nullptr);
    ASSERT_NE(last->rect, nullptr);
    EXPECT_NEAR(first->rect->width,
        font_measure_text(first->font->font_handle, "f", 1).width, 0.02f);
    EXPECT_NEAR(last->rect->width,
        font_measure_text(last->font->font_handle, "i", 1).width, 0.02f);
}

TEST_F(RadiantTextQuery, WordSpacingExpressionsRetainLengthAdvances) {
    ASSERT_NE(load("test/html/layout_font_spacing.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; } cases[] = {
        {"sum", 100.0f}, {"product", 80.0f}, {"length", 100.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
    }
}

// A query without faces is the common chart case. Its cache key used to be
// rejected, so every call built a new font context and rescanned every font
// directory; all face-less queries in one evaluation now share one context.
TEST_F(RadiantTextQuery, FacelessQueriesReuseOneCachedFontContext) {
    uint64_t builds_before = radiant_query_font_build_count();
    DomElement* result = load("test/lambda/ui/_measure_text_cache.ls");
    ASSERT_NE(result, nullptr);
    const char* measured = result->get_attribute("data-measured");
    const char* agree = result->get_attribute("data-agree");
    ASSERT_NE(measured, nullptr);
    ASSERT_NE(agree, nullptr);
    EXPECT_STREQ(measured, "24");  // 12 null-face and 12 empty-face queries all measured
    EXPECT_STREQ(agree, "true");   // null and [] name the same system-font context
    EXPECT_LE(radiant_query_font_build_count() - builds_before, 1u);
}
