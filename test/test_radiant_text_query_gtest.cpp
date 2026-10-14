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

TEST_F(RadiantTextQuery, NestedColumnsBalanceTheFinalContentSetEvenWithBlankHeightFragments) {
    ASSERT_NE(load("test/html/layout_nested_final_column_set.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 500;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; } cases[] = {
        {"first-final", 100.0f, 0.0f}, {"first-full", 100.0f, 0.0f},
        {"continuation-left", 200.0f, 0.0f}, {"continuation-right", 200.0f, 20.0f},
        {"auto", 0.0f, 60.0f},
        {"three-middle", 200.0f / 3.0f, 0.0f}, {"three-last", 400.0f / 3.0f, 0.0f},
        {"orphans-last", 100.0f, 0.0f}, {"widows-first", 100.0f, 0.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        ASSERT_NE(element->first_child->as_text()->rect, nullptr);
        TextRect* rect = element->first_child->as_text()->rect;
        EXPECT_NEAR(rect->x, entry.x, 0.1f) << entry.id;
        EXPECT_NEAR(rect->y, entry.y, 0.1f) << entry.id;
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

TEST_F(RadiantTextQuery, TypographicUnitsKeepRequiredContextInsideATextNode) {
#ifdef __APPLE__
    ASSERT_NE(load("test/html/layout_contextual_typographic_units.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 500;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; } cases[] = {
        {"plain", 130.1875f},
        {"selected", 136.859375f}, {"selected-tracked", 216.859375f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        ASSERT_NE(element->first_child->as_text()->rect, nullptr);
        // the two selected faces form different required clusters in the same word.
        EXPECT_NEAR(element->width, entry.width, 0.1f) << entry.id;
        EXPECT_NEAR(element->first_child->as_text()->rect->width, entry.width, 0.1f) << entry.id;
    }
    DomElement* arabic = dom_find_element_by_id(document->root->as_element(), "arabic");
    ASSERT_NE(arabic, nullptr);
    DomText* text = arabic->first_child->as_text();
    ASSERT_NE(text, nullptr);
    const char* forms[] = {"ﻋ", "ﻌ", "ﻊ"};
    TextRect* rect = text->rect;
    for (const char* form : forms) {
        ASSERT_NE(rect, nullptr);
        EXPECT_NEAR(rect->width, font_measure_text(arabic->fontp()->font_handle, form, 3).width, 0.1f);
        rect = rect->next;
    }
    EXPECT_EQ(rect, nullptr);
#else
    GTEST_SKIP() << "native Indic contextual range shaping is unavailable";
#endif
}

TEST_F(RadiantTextQuery, ResolvedZeroSpacingSurvivesFontCompletion) {
    ASSERT_NE(load("test/html/layout_zero_spacing_inheritance.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    const char* ids[] = {"zero", "normal", "inherited", "intrinsic"};
    for (const char* id : ids) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr);
        ASSERT_NE(element->font, nullptr);
        EXPECT_FLOAT_EQ(element->font->letter_spacing, 0.0f) << id;
        EXPECT_FLOAT_EQ(element->font->word_spacing, 0.0f) << id;
        ASSERT_NE(element->first_child->as_text()->rect, nullptr);
        EXPECT_FLOAT_EQ(element->first_child->as_text()->rect->width, 60.0f) << id;
    }
    DomElement* intrinsic = dom_find_element_by_id(document->root->as_element(), "intrinsic");
    EXPECT_FLOAT_EQ(intrinsic->width, 60.0f);
}

TEST_F(RadiantTextQuery, NormalLeadingUsesTheSelectedFaceForIdeographs) {
    ASSERT_NE(load("test/html/layout_selected_face_leading.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    const char* ids[] = {"latin", "ideograph", "cell"};
    for (const char* id : ids) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, 40.0f) << id;
    }
}

TEST_F(RadiantTextQuery, SmallFallbackMetricDifferencesStillExpandNormalLeading) {
#ifdef __APPLE__
    ASSERT_NE(load("test/html/layout_small_fallback_leading.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; } cases[] = {
        {"primary", 36.0f}, {"mixed", 38.0f}, {"explicit", 36.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
#else
    GTEST_SKIP() << "the control requires the macOS Apple Symbols fallback face";
#endif
}

TEST_F(RadiantTextQuery, AnywhereScopeCanBreakBeforeAFollowingNormalWord) {
    ASSERT_NE(load("test/html/layout_emergency_inline_boundary.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* tail = dom_find_element_by_id(document->root->as_element(), "tail");
    DomElement* control = dom_find_element_by_id(document->root->as_element(), "control");
    DomElement* unbroken = dom_find_element_by_id(document->root->as_element(), "unbroken");
    ASSERT_NE(tail, nullptr);
    ASSERT_NE(control, nullptr);
    ASSERT_NE(unbroken, nullptr);
    EXPECT_FLOAT_EQ(tail->height, control->height);
    EXPECT_FLOAT_EQ(tail->height, 60.0f);
    EXPECT_FLOAT_EQ(unbroken->height, 20.0f);
    DomText* text = tail->last_child->as_text();
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->rect, nullptr);
    EXPECT_FLOAT_EQ(text->rect->x, 0.0f);
    EXPECT_FLOAT_EQ(text->rect->y, 40.0f);
    EXPECT_FLOAT_EQ(text->rect->width, 40.0f);
    const char* mandatory[] = {"break-all-middle", "any-line-break-middle"};
    for (const char* id : mandatory) {
        DomElement* middle = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(middle, nullptr) << id;
        DomText* run = middle->first_child->as_text();
        ASSERT_NE(run, nullptr);
        ASSERT_NE(run->rect, nullptr);
        ASSERT_NE(run->rect->next, nullptr);
        EXPECT_FLOAT_EQ(run->rect->x, 40.0f) << id;
        EXPECT_FLOAT_EQ(run->rect->width, 40.0f) << id;
        EXPECT_FLOAT_EQ(run->rect->next->x, 0.0f) << id;
        EXPECT_FLOAT_EQ(run->rect->next->width, 40.0f) << id;
        EXPECT_FLOAT_EQ(middle->height, 40.0f) << id;
    }
    DomElement* nowrap = dom_find_element_by_id(document->root->as_element(), "nowrap-break-all");
    ASSERT_NE(nowrap, nullptr);
    EXPECT_FLOAT_EQ(nowrap->height, 20.0f);
}

TEST_F(RadiantTextQuery, FragmentedAbsoluteFlowRetainsSourceAndFixedAnchors) {
    ASSERT_NE(load("test/html/layout_fragmented_absolute_flow.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 300;
    layout_html_doc(&ui, document, false);
    DomElement* absolute = dom_find_element_by_id(document->root->as_element(), "absolute");
    DomElement* child = dom_find_element_by_id(document->root->as_element(), "continuous-child");
    DomElement* fixed = dom_find_element_by_id(document->root->as_element(), "fixed");
    ASSERT_NE(absolute, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(fixed, nullptr);
    EXPECT_FLOAT_EQ(absolute->x, 100.0f);
    EXPECT_FLOAT_EQ(absolute->y, 0.0f);
    EXPECT_FLOAT_EQ(absolute->width, 100.0f);
    EXPECT_FLOAT_EQ(absolute->height, 100.0f);
    EXPECT_FLOAT_EQ(child->width, 100.0f);
    EXPECT_FLOAT_EQ(child->height, 100.0f);
    EXPECT_FLOAT_EQ(fixed->x, 175.0f);
    EXPECT_FLOAT_EQ(fixed->y, 100.0f);
    EXPECT_EQ(absolute->layout_fragments_count(), 4);
    EXPECT_EQ(child->layout_fragments_count(), 4);
    DomElement* nested = dom_find_element_by_id(document->root->as_element(), "nested-child");
    DomElement* empty = dom_find_element_by_id(document->root->as_element(), "empty-absolute");
    DomElement* inner = dom_find_element_by_id(document->root->as_element(), "empty-inner");
    ASSERT_NE(nested, nullptr);
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(inner, nullptr);
    EXPECT_FLOAT_EQ(nested->width, 50.0f);
    EXPECT_FLOAT_EQ(nested->height, 100.0f);
    EXPECT_EQ(nested->layout_fragments_count(), 2);
    EXPECT_FLOAT_EQ(empty->width, 100.0f);
    EXPECT_FLOAT_EQ(empty->height, 100.0f);
    EXPECT_EQ(empty->layout_fragments_count(), 4);
    EXPECT_FLOAT_EQ(inner->height, 0.0f);
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
        {"uppercase-middle", "ﻌ", 0.0f}, {"lowercase-middle", "ﻌ", 0.0f},
        {"capitalize-middle", "ﻌ", 0.0f},
        {"autospace-middle", "ﻌ", 0.0f},
        {"wrap-middle", "ﻌ", 0.0f},
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
    DomElement* mongolian = dom_find_element_by_id(document->root->as_element(), "mongolian-middle");
    ASSERT_NE(mongolian, nullptr);
    EXPECT_NEAR(mongolian->width, 34.109375f, 0.1f);
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

TEST_F(RadiantTextQuery, VerticalAutomaticInlineSizeUsesFinalExtentForAlignment) {
    ASSERT_NE(load("test/html/layout_vertical_auto_alignment.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 500;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* word = dom_find_element_by_id(document->root->as_element(), "mongolian");
    DomElement* middle = dom_find_element_by_id(document->root->as_element(), "middle");
    DomElement* latin = dom_find_element_by_id(document->root->as_element(), "latin");
    ASSERT_NE(word, nullptr);
    ASSERT_NE(middle, nullptr);
    ASSERT_NE(latin, nullptr);
    // Chromium's pinned Noto face joins the three letters into a 144.39px word.
    EXPECT_NEAR(word->height, 166.390625f, 0.1f);
    EXPECT_NEAR(middle->y, 70.0625f, 0.1f);
    EXPECT_NEAR(middle->height, 34.109375f, 0.1f);
    EXPECT_NEAR(latin->height, 68.0f, 0.1f);
    ASSERT_NE(latin->first_child->as_text()->rect, nullptr);
    EXPECT_NEAR(latin->first_child->as_text()->rect->y, 4.0f, 0.1f);
}

TEST_F(RadiantTextQuery, IntrinsicIndentAppliesOnlyToTheFirstUnbreakableSegment) {
    ASSERT_NE(load("test/html/layout_first_segment_indent.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; float height; } cases[] = {
        {"positive", 70.0f, 30.0f}, {"maximum", 170.0f, 10.0f},
        {"later-wide", 50.0f, 10.0f}, {"first-wide", 20.0f, 20.0f},
        {"nowrap", 170.0f, 10.0f}, {"pre", 70.0f, 20.0f},
        {"nested-min", 70.0f, 30.0f}, {"nested-max", 170.0f, 10.0f},
        {"anonymous-multi", 100.0f, 20.0f}, {"anonymous-single", 100.0f, 10.0f},
        {"anonymous-percent", 100.0f, 10.0f}, {"anonymous-plain", 100.0f, 10.0f},
        {"anonymous-following", 100.0f, 20.0f}, {"following-box", 0.0f, 20.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
    struct TextCase { const char* id; float x; } first_lines[] = {
        {"anonymous-multi", 50.0f}, {"anonymous-single", 50.0f},
        {"anonymous-percent", 25.0f}, {"anonymous-plain", 0.0f},
        {"anonymous-following", 50.0f},
    };
    for (const TextCase& entry : first_lines) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        DomText* text = nullptr;
        for (DomNode* node = element->first_child; node; node = node->next_sibling) {
            if (node->is_text() && layout_text_node_has_content(node)) { text = node->as_text(); break; }
        }
        ASSERT_NE(text, nullptr) << entry.id;
        ASSERT_NE(text->rect, nullptr) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->width, 50.0f) << entry.id;
    }
}

TEST_F(RadiantTextQuery, TerminalSoftHyphensRetainTheirSourceStyleAndCompletedLine) {
    ASSERT_NE(load("test/html/layout_terminal_soft_hyphen_style.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; float height; int bytes; bool generated; } cases[] = {
        {"separate-mark", 48.0f, 48.0f, 2, true},
        {"combined-mark", 144.0f, 48.0f, 4, true},
        {"unchosen-mark", 0.0f, 48.0f, 2, false},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
        DomText* text = element->first_child->as_text();
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_EQ(text->rect->length, entry.bytes) << entry.id;
        EXPECT_EQ(text->rect->has_trailing_hyphen, entry.generated) << entry.id;
    }
    const char* completed[] = {"separate", "combined", "forced"};
    for (const char* id : completed) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, 64.0f) << id;
        DomText* tail = element->last_child->as_text();
        ASSERT_NE(tail, nullptr);
        ASSERT_NE(tail->rect, nullptr);
        EXPECT_FLOAT_EQ(tail->rect->y, 48.0f) << id;
    }
}

TEST_F(RadiantTextQuery, IsolatesReorderSplitRunsAndTransparentEmptyBoundaries) {
    ASSERT_NE(load("test/html/layout_bidi_isolate_boundaries.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; } cases[] = {
        {"numeric-boundary", 66.859375f}, {"numeric-run", 44.609375f},
        {"neutral-boundary", 35.703125f}, {"first-boundary", 66.859375f},
        {"ltr-boundary", 17.796875f}, {"nested-boundary", 26.46875f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        float x = 0.0f;
        view_get_visual_bounds(element, &x, nullptr, nullptr, nullptr);
        EXPECT_NEAR(x, entry.x, 0.15f) << entry.id;
        EXPECT_FLOAT_EQ(element->width, 0.0f) << entry.id;
    }
    DomElement* boundary = dom_find_element_by_id(document->root->as_element(), "numeric-boundary");
    DomText* preceding = boundary->prev_sibling->as_text();
    ASSERT_NE(preceding, nullptr);
    ASSERT_NE(preceding->rect, nullptr);
    EXPECT_NEAR(preceding->rect->x, boundary->x, 0.01f);
    DomElement* nested = dom_find_element_by_id(document->root->as_element(), "nested");
    ASSERT_NE(nested, nullptr);
    EXPECT_NEAR(nested->width, 34.703125f, 0.1f);
    EXPECT_NEAR(nested->x, 8.671875f, 0.1f);
}

TEST_F(RadiantTextQuery, UnicodeSeparatorsKeepTheirPrecedingLetterUnits) {
    ASSERT_NE(load("test/html/layout_unicode_separator_boundaries.html"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; int count; float widths[6]; } cases[] = {
        {"ideographic", 60.0f, 6, {30.0f, 10.0f, 20.0f, 10.0f, 20.0f, 20.0f}},
        {"narrow-glue", 30.0f, 3, {35.0f, 25.0f, 35.0f}},
        {"nobreak", 10.0f, 1, {40.0f}},
        {"anywhere", 20.0f, 2, {20.0f, 10.0f}},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
        DomText* text = element->first_child->as_text();
        ASSERT_NE(text, nullptr);
        TextRect* rect = text->rect;
        for (int index = 0; index < entry.count; index++) {
            ASSERT_NE(rect, nullptr) << entry.id;
            EXPECT_FLOAT_EQ(rect->width, entry.widths[index]) << entry.id;
            EXPECT_FLOAT_EQ(rect->x, 0.0f) << entry.id;
            rect = rect->next;
        }
        EXPECT_EQ(rect, nullptr) << entry.id;
    }
    EXPECT_FALSE(layout_text_allows_letter_break(0x3042, 0x202F, CSS_VALUE_AUTO, false, false));
    EXPECT_FALSE(layout_text_allows_letter_break(0x3042, 0x3000, CSS_VALUE_AUTO, false, false));
    EXPECT_TRUE(layout_text_allows_letter_break(0x3042, 0x202F, CSS_VALUE_ANYWHERE, false, false));
}

TEST_F(RadiantTextQuery, SpaceOnlyInlinesRetainTheLineThatAcceptedTheirSeparator) {
    ASSERT_NE(load("test/html/layout_collapsed_space_inline_line.html"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; } cases[] = {
        {"ordinary-space", 20.0f, 0.0f, 0.0f},
        {"fullwidth-space", 20.0f, 40.0f, 0.0f},
        {"wide-space", 20.0f, 80.0f, 20.0f},
        {"terminal-space", 20.0f, 100.0f, 0.0f},
        {"leading-space", 0.0f, 120.0f, 0.0f},
        {"preserved-space", 20.0f, 140.0f, 20.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        float x = 0.0f, y = 0.0f;
        view_get_visual_bounds(element, &x, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, 20.0f) << entry.id;
    }
    DomElement* ordinary = dom_find_element_by_id(document->root->as_element(), "ordinary-space");
    DomText* text = ordinary->first_child->as_text();
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->rect, nullptr);
    EXPECT_EQ(text->rect->length, 1);
    EXPECT_FLOAT_EQ(text->rect->width, 0.0f);
    EXPECT_TRUE(ordinary->has_collapsed_line_fragment_union());
}

TEST_F(RadiantTextQuery, InlineStaticPositionsUseSignedIndentAndResolvedBidiCursor) {
    ASSERT_NE(load("test/html/layout_static_inline_bidi_cursor.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; } cases[] = {
        {"negative", 20.0f, 40.0f}, {"rtl-first", 100.0f, 60.0f},
        {"rtl-after", 50.0f, 80.0f}, {"rtl-negative", 140.0f, 100.0f},
        {"rtl-own", 100.0f, 120.0f}, {"explicit", 40.0f, 140.0f},
        {"block", 40.0f, 160.0f}, {"left-float", 110.0f, 180.0f},
        {"right-float", 90.0f, 200.0f}, {"rtl-latin-run", 90.0f, 220.0f},
        {"ltr-latin-run", 90.0f, 240.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        float x = 0.0f, y = 0.0f;
        view_get_visual_bounds(element, &x, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
    }
    DomElement* own = dom_find_element_by_id(document->root->as_element(), "rtl-own");
    DomText* text = own->first_child->as_text();
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->rect, nullptr);
    EXPECT_FLOAT_EQ(text->rect->x, -10.0f);
    EXPECT_FLOAT_EQ(text->rect->width, 10.0f);
}

TEST_F(RadiantTextQuery, NestedForcedBreaksConsumeWholeInnerColumns) {
    ASSERT_NE(load("test/html/layout_nested_forced_columns.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; int fragments; } cases[] = {
        {"after-inner", 0.0f, 0.0f, 100.0f, 100.0f, 2},
        {"after-wrapper", 0.0f, 0.0f, 100.0f, 100.0f, 4},
        {"after-first", 0.0f, 0.0f, 10.0f, 10.0f, 0},
        {"after-second", 30.0f, 0.0f, 10.0f, 10.0f, 0},
        {"after-tail", 60.0f, 0.0f, 40.0f, 100.0f, 2},
        {"before-inner", 0.0f, 110.0f, 100.0f, 100.0f, 2},
        {"before-wrapper", 0.0f, 110.0f, 100.0f, 100.0f, 4},
        {"before-first", 0.0f, 110.0f, 10.0f, 10.0f, 0},
        {"before-second", 30.0f, 110.0f, 10.0f, 10.0f, 0},
        {"before-tail", 60.0f, 110.0f, 40.0f, 100.0f, 2},
        {"plain-inner", 0.0f, 220.0f, 40.0f, 100.0f, 0},
        {"plain-wrapper", 0.0f, 220.0f, 40.0f, 100.0f, 2},
        {"plain-tail", 0.0f, 220.0f, 40.0f, 100.0f, 2},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        float x = 0.0f, y = 0.0f;
        view_get_visual_bounds(element, &x, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
        EXPECT_EQ(element->layout_fragments_count(), entry.fragments) << entry.id;
        for (LayoutFragmentBox* fragment = element->layout_fragment_list(); fragment; fragment = fragment->next) {
            EXPECT_FLOAT_EQ(fragment->height, 100.0f) << entry.id;
        }
    }
    struct RuleCase { const char* id; int count; } rules[] = {
        {"after-inner", 2}, {"before-inner", 2}, {"plain-inner", 1},
    };
    for (const RuleCase& entry : rules) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        RenderColumnRuleIterator iterator;
        ASSERT_TRUE(render_column_rules_begin(lam::view_require_block(element), &iterator));
        float x = 0.0f, y = 0.0f, height = 0.0f;
        int count = 0;
        while (render_column_rule_next(&iterator, &x, &y, &height)) {
            EXPECT_FLOAT_EQ(x, 20.0f + count * 60.0f) << entry.id;
            EXPECT_FLOAT_EQ(y, 0.0f) << entry.id;
            EXPECT_FLOAT_EQ(height, 100.0f) << entry.id;
            count++;
        }
        EXPECT_EQ(count, entry.count) << entry.id;
    }
    DomElement* wrapper = dom_find_element_by_id(document->root->as_element(), "after-wrapper");
    PaintList paint;
    paint_list_init(&paint, nullptr);
    EXPECT_TRUE(render_paint_boundary_emit_fragmented_simple(&paint,
        lam::view_require_block(wrapper), {5.0f, 7.0f, 200.0f, 200.0f}));
    EXPECT_EQ(paint_list_count(&paint), 4);
    for (size_t index = 0; index < paint.size(); index++) {
        EXPECT_EQ(paint[index].op, PAINT_FILL_RECT);
        EXPECT_FLOAT_EQ(paint[index].fill_rect.x, 5.0f + index * 60.0f);
        EXPECT_FLOAT_EQ(paint[index].fill_rect.y, 7.0f);
        EXPECT_FLOAT_EQ(paint[index].fill_rect.w, 20.0f);
        EXPECT_FLOAT_EQ(paint[index].fill_rect.h, 200.0f);
    }
    paint_list_destroy(&paint);
}

TEST_F(RadiantTextQuery, FailedImagesWithEmptyAlternativesHaveZeroNaturalSize) {
    ASSERT_NE(load("test/html/layout_empty_image_alternative.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; float height; } cases[] = {
        {"empty", 0.0f, 0.0f}, {"absent", 16.0f, 16.0f},
        {"sized", 30.0f, 20.0f},
        {"intrinsic", 0.0f, 18.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, AligningASplitInlinePreservesItsBlockSiblingAndFragmentUnion) {
    ASSERT_NE(load("test/html/layout_split_inline_alignment.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 300;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float width; float height; } cases[] = {
        {"mixed", 0.0f, 200.0f, 60.0f}, {"mixed-block", 0.0f, 100.0f, 40.0f},
        {"tail", 80.0f, 40.0f, 20.0f}, {"only", 0.0f, 200.0f, 40.0f},
        {"bracket", 0.0f, 200.0f, 80.0f}, {"bracket-block", 0.0f, 100.0f, 40.0f},
        {"atomic", 80.0f, 40.0f, 20.0f},
        {"direct-tail", 0.0f, 200.0f, 60.0f}, {"empty-split", 80.0f, 40.0f, 20.0f},
        {"raised-tail", 0.0f, 200.0f, 114.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_NEAR(element->x, entry.x, 0.1f) << entry.id;
        EXPECT_NEAR(element->width, entry.width, 0.1f) << entry.id;
        EXPECT_NEAR(element->height, entry.height, 0.1f) << entry.id;
    }
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

TEST_F(RadiantTextQuery, ImagePercentagesUseAutomaticContainingBlocksUsedWidth) {
    ASSERT_NE(load("test/html/layout_percentage_image_basis.html"), nullptr);
    ui.viewport_width = ui.window_width = 600;
    ui.viewport_height = ui.window_height = 500;
    layout_html_doc(&ui, document, false);
    const char* ids[] = {"ordinary", "picture", "definite", "zero"};
    for (const char* id : ids) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr);
        bool zero = strcmp(id, "zero") == 0;
        EXPECT_NEAR(element->x, zero ? 0.0f : 10.0f, 0.1f) << id;
        EXPECT_NEAR(element->width, zero ? 0.0f : 380.0f, 0.1f) << id;
        EXPECT_NEAR(element->height, zero ? 0.0f : 125.928f, 0.1f) << id;
    }
}

TEST_F(RadiantTextQuery, WinningFontFamilyDeterminesTheMonospaceMediumAdjustment) {
    ASSERT_NE(load("test/html/layout_monospace_cascade.html"), nullptr);
    ui.viewport_width = ui.window_width = 600;
    ui.viewport_height = ui.window_height = 300;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float size; } cases[] = {
        {"ua", 13.0f}, {"named", 16.0f}, {"generic", 13.0f},
        {"generic-span", 13.0f}, {"named-pre", 16.0f},
        {"sized-named", 20.0f}, {"sized-ua", 20.0f}, {"shorthand", 20.0f},
        {"textarea", 13.333333f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        ASSERT_NE(element->font, nullptr);
        EXPECT_NEAR(element->fontp()->font_size, entry.size, 0.01f) << entry.id;
    }
}

TEST_F(RadiantTextQuery, ZeroColumnWidthIsSpecifiedAndUsesTheMinimumPreferredWidth) {
    ASSERT_NE(load("test/html/layout_zero_column_width.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; int count; float width; float computed; } cases[] = {
        {"longhand", 12, 1.0f, 0.0f}, {"shorthand", 12, 1.0f, 0.0f},
        {"unitless", 12, 1.0f, 0.0f}, {"subpixel", 12, 1.0f, 0.5f},
        {"capped", 3, 4.0f, 0.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        ViewBlock* block = lam::view_as_block(element);
        ASSERT_NE(block, nullptr);
        ASSERT_NE(block->multicol_prop(), nullptr);
        EXPECT_TRUE(is_multicol_container(block)) << entry.id;
        EXPECT_EQ(block->multicol_prop()->computed_column_count, entry.count) << entry.id;
        EXPECT_FLOAT_EQ(block->multicol_prop()->computed_column_width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(block->multicol_prop()->column_width, entry.computed) << entry.id;
    }
    DomElement* automatic = dom_find_element_by_id(document->root->as_element(), "automatic");
    ASSERT_NE(automatic, nullptr);
    EXPECT_FALSE(is_multicol_container(lam::view_as_block(automatic)));
}

TEST_F(RadiantTextQuery, IntrinsicWhitespaceCollapsesAtDirectAndNestedForcedBreaks) {
    ASSERT_NE(load("test/html/layout_intrinsic_forced_break.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; float height; } cases[] = {
        {"direct", 120.0f, 40.0f}, {"nested", 120.0f, 40.0f}, {"after", 120.0f, 40.0f},
        {"space", 160.0f, 20.0f}, {"preserved", 140.0f, 40.0f}, {"decorated", 121.0f, 40.0f},
        {"normal-punctuation", 40.0f, 20.0f}, {"word-punctuation", 40.0f, 20.0f},
        {"any-punctuation", 20.0f, 40.0f}, {"all-punctuation", 40.0f, 20.0f},
        {"keep-punctuation", 40.0f, 20.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
    DomElement* direct_break = dom_find_element_by_id(document->root->as_element(), "direct-break");
    ASSERT_NE(direct_break, nullptr);
    EXPECT_FLOAT_EQ(direct_break->x, 120.0f);
    EXPECT_FLOAT_EQ(direct_break->y, 0.0f);
}

TEST_F(RadiantTextQuery, ColumnGroupsUseTheirOwnUnbreakableLineConstraints) {
    ASSERT_NE(load("test/html/layout_balance_group_constraints.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; } cases[] = {
        {"one-line", 20.0f}, {"three-lines", 40.0f}, {"contained", 110.0f},
        {"continuous", 100.0f}, {"continuous-text", 100.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
    const char* contained_ids[] = {"contained-before", "contained-after"};
    for (const char* id : contained_ids) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, 50.0f) << id;
        EXPECT_EQ(element->layout_fragment_list(), nullptr) << id;
    }
}

TEST_F(RadiantTextQuery, UnboundedAutoColumnsSizeFromForcedRunsWithoutBalancing) {
    ASSERT_NE(load("test/html/layout_auto_forced_column_runs.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; } cases[] = {
        {"direct", 80.0f}, {"wrapped", 80.0f}, {"after", 80.0f},
        {"merged", 80.0f}, {"plain", 110.0f}, {"finite", 40.0f},
        {"single", 80.0f}, {"independent", 100.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
    struct Tail { const char* id; float x; float y; } tails[] = {
        {"direct-tail", 44.0f, 0.0f}, {"after-tail", 44.0f, 180.0f},
        {"merged-tail", 22.0f, 270.0f}, {"plain-tail", 0.0f, 390.0f},
    };
    for (const Tail& entry : tails) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float x = 0.0f, y = 0.0f;
        view_get_visual_bounds(element, &x, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
    }
}

TEST_F(RadiantTextQuery, InlinePreflightUsesItsOwnersWrappingPolicy) {
    ASSERT_NE(load("test/html/layout_inline_policy_preflight.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float y; float height; } cases[] = {
        {"own-span", 0.0f, 40.0f}, {"inherited-span", 40.0f, 40.0f},
        {"line-any-span", 80.0f, 40.0f}, {"emergency-span", 140.0f, 20.0f},
        {"variable-span", 160.0f, 40.0f}, {"reset-span", 220.0f, 20.0f},
        {"normal-span", 260.0f, 20.0f}, {"nowrap-span", 300.0f, 20.0f},
        {"mode-span", 320.0f, 40.0f}, {"shorthand-span", 360.0f, 40.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float x = 0.0f, y = 0.0f;
        view_get_visual_bounds(element, &x, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, 0.0f) << entry.id;
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
        EXPECT_FLOAT_EQ(element->width, 120.0f) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, InlineBoundsRefreshRetainsItsOwnTextFontBox) {
    ASSERT_NE(load("test/html/layout_own_inline_font_box.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 300;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float y; float height; } cases[] = {
        {"small-tab", 14.0f, 1.0f}, {"medium-tab", 25.0f, 11.0f},
        {"small-text", 52.0f, 1.0f}, {"medium-text", 63.0f, 11.0f},
        {"text-both", 82.0f, 11.0f}, {"nested", 101.0f, 11.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float y = 0.0f;
        view_get_visual_bounds(element, nullptr, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
    DomElement* breaks = dom_find_element_by_id(document->root->as_element(), "breaks");
    ASSERT_NE(breaks, nullptr);
    DomText* first = breaks->first_child->as_text();
    DomText* second = breaks->first_child->next_sibling->next_sibling->next_sibling->as_text();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(first->rect, nullptr);
    ASSERT_NE(second->rect, nullptr);
    EXPECT_NE(first->rect->line_number, second->rect->line_number);
    EXPECT_FLOAT_EQ(first->rect->height, 11.0f);
    EXPECT_FLOAT_EQ(second->rect->height, 11.0f);
    EXPECT_FLOAT_EQ(second->rect->y - first->rect->y, 19.0f);
    DomElement* vertical = dom_find_element_by_id(document->root->as_element(), "vertical");
    ASSERT_NE(vertical, nullptr);
    EXPECT_FLOAT_EQ(vertical->width, 16.0f);
    EXPECT_FLOAT_EQ(vertical->height, 112.0f);
}

TEST_F(RadiantTextQuery, AutomaticHyphenationKeepsPunctuationAndOverflowingSyllables) {
    ASSERT_NE(load("test/html/layout_hyphen_word_boundaries.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 600;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float y; float width; } cases[] = {
        {"bare", 0.0f, 60.0f}, {"opening", 60.0f, 80.0f},
        {"closing", 120.0f, 60.0f}, {"both", 180.0f, 80.0f},
        {"narrow", 240.0f, 60.0f}, {"manual", 300.0f, 60.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, 60.0f) << entry.id;
        DomText* text = element->first_child->as_text();
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->y, 0.0f) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->width, entry.width) << entry.id;
        EXPECT_TRUE(text->rect->has_trailing_hyphen) << entry.id;
        int fragments = 0;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            EXPECT_GT(rect->length, 0) << entry.id;
            EXPECT_FLOAT_EQ(rect->y, fragments * 20.0f) << entry.id;
            fragments++;
        }
        EXPECT_EQ(fragments, 3) << entry.id;
        float y = 0.0f;
        view_get_visual_bounds(element, nullptr, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
    }
    DomElement* none = dom_find_element_by_id(document->root->as_element(), "none");
    ASSERT_NE(none, nullptr);
    EXPECT_FLOAT_EQ(none->height, 20.0f);
    EXPECT_FLOAT_EQ(none->first_child->as_text()->rect->width, 140.0f);
    DomElement* replay = dom_find_element_by_id(document->root->as_element(), "word-replay");
    ASSERT_NE(replay, nullptr);
    EXPECT_FLOAT_EQ(replay->height, 80.0f);
    DomText* replay_text = replay->first_child->as_text();
    ASSERT_NE(replay_text, nullptr);
    ASSERT_NE(replay_text->rect, nullptr);
    EXPECT_FALSE(replay_text->rect->has_trailing_hyphen);
    ASSERT_NE(replay_text->rect->next, nullptr);
    EXPECT_TRUE(replay_text->rect->next->has_trailing_hyphen);
    int replay_fragments = 0;
    for (TextRect* rect = replay_text->rect; rect; rect = rect->next) {
        EXPECT_GT(rect->length, 0);
        replay_fragments++;
    }
    EXPECT_EQ(replay_fragments, 4);
}

TEST_F(RadiantTextQuery, HyphenationCharacterLimitsInheritAndKeepComputedAuto) {
    ASSERT_NE(load("test/html/layout_hyphenate_limits.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 1000;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; const char* computed; } cases[] = {
        {"auto", 60.0f, "auto"}, {"whole", 20.0f, "8"},
        {"prefix-two", 60.0f, "auto 2"}, {"prefix-three", 40.0f, "auto 3 2"},
        {"prefix-four", 40.0f, "auto 4 2"}, {"prefix-five", 20.0f, "auto 5 2"},
        {"suffix-three", 60.0f, "auto 2 3"}, {"suffix-four", 40.0f, "auto 2 4"},
        {"both", 20.0f, "auto 3 4"}, {"zero", 60.0f, "0 0"},
        {"two-values", 40.0f, "auto 3"}, {"rounded", 60.0f, "5 2 3"},
        {"negative-math", 60.0f, "0"}, {"variable", 40.0f, "auto 3 2"},
        {"relative-math", 20.0f, "20"}, {"inherited", 20.0f, "8"},
        {"explicit-inherit", 20.0f, "8"}, {"unset", 20.0f, "8"},
        {"initial", 60.0f, "auto"}, {"invalid", 20.0f, "8"},
        {"manual", 60.0f, "8"},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
        String* computed = css_prop_serialize_computed_value(
            document->document_pool, element, CSS_PROPERTY_HYPHENATE_LIMIT_CHARS, 0);
        ASSERT_NE(computed, nullptr) << entry.id;
        EXPECT_STREQ(computed->chars, entry.computed) << entry.id;
    }
    const char* marked = "a\xCC\x81-b";
    EXPECT_EQ(layout_hyphenation_character_count(marked, strlen(marked)), 2u);
    EXPECT_EQ(layout_hyphenation_character_count("example", 7), 7u);
}

TEST_F(RadiantTextQuery, HyphenatedSyllablesClearFloatsWithTheirGeneratedMarks) {
    ASSERT_NE(load("test/html/layout_hyphen_float_clearance.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case {
        const char* id;
        float height;
        float first_y;
        float second_y;
        int fragments;
    } cases[] = {
        {"auto-one", 69.2f, 2.0f, 24.4f, 3},
        {"auto-two", 78.8f, 2.0f, 34.0f, 3},
        {"auto-three", 94.8f, 2.0f, 50.0f, 3},
        {"auto-four", 110.8f, 2.0f, 66.0f, 3},
        {"manual-one", 69.2f, 2.0f, 24.4f, 3},
        {"manual-two", 78.8f, 2.0f, 34.0f, 3},
        {"manual-three", 94.8f, 2.0f, 50.0f, 3},
        {"manual-four", 110.8f, 2.0f, 66.0f, 3},
        {"left-float", 94.8f, 2.0f, 50.0f, 3},
        {"indented", 94.8f, 50.0f, 72.4f, 2},
        {"long-mark", 94.8f, 2.0f, 50.0f, 3},
        {"empty-mark", 69.2f, 2.0f, 24.4f, 3},
        {"space-before", 94.8f, 2.0f, 50.0f, 3},
        {"nowrap", 50.0f, 2.0f, 0.0f, 1},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_NEAR(element->height, entry.height, 0.05f) << entry.id;
        DomText* text = element->first_child->next_sibling->as_text();
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_NEAR(text->rect->y, entry.first_y, 0.05f) << entry.id;
        int fragments = 0;
        for (TextRect* rect = text->rect; rect; rect = rect->next) fragments++;
        EXPECT_EQ(fragments, entry.fragments) << entry.id;
        if (entry.fragments > 1) {
            ASSERT_NE(text->rect->next, nullptr);
            EXPECT_NEAR(text->rect->next->y, entry.second_y, 0.05f) << entry.id;
        }
        if (strcmp(entry.id, "left-float") == 0) {
            float character_width = (element->width - 2.0f) / 6.0f;
            EXPECT_NEAR(text->rect->x, 1.0f + 2.0f * character_width, 0.05f);
            EXPECT_FLOAT_EQ(text->rect->next->x, 1.0f);
        } else if (strcmp(entry.id, "indented") == 0) {
            float character_width = (element->width - 2.0f) / 6.0f;
            EXPECT_NEAR(text->rect->x, 1.0f + character_width, 0.05f);
            EXPECT_FLOAT_EQ(text->rect->next->x, 1.0f);
        }
    }
}

TEST_F(RadiantTextQuery, HyphenatedPaintKeepsContinuationGlyphsOnTheirOwnLine) {
    ASSERT_NE(load("test/html/layout_hyphen_float_clearance.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    const char* ids[] = {"auto-three", "manual-three", "long-mark", "empty-mark",
                         "terminal-source", "terminal-unicode",
                         "clipped-terminal", "clipped-unicode"};
    const char* expected[][3] = {
        {"hy‐", "phen‐", "ate!"}, {"hy‐", "phen‐", "ate!"},
        {"hy--", "phen--", "ate!"}, {"hy", "phen", "ate!"},
        {"ab‐", "c", nullptr}, {"ab‐", "é", nullptr},
        {"a…", "c", nullptr}, {"a…", "é", nullptr},
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), ids[i]);
        ASSERT_NE(element, nullptr);
        DomText* text = element->first_child->as_text();
        if (!text) text = element->first_child->next_sibling->as_text();
        ASSERT_NE(text, nullptr);
        RenderContext context = {};
        context.ui_context = lam::up(&ui);
        setup_font(&ui, &context.font, text->font);
        RenderTextPaintScope paint(&context, lam::view_as_block(element),
            &context.font, &context.color);
        RenderTextPaintIterator iterator(&context, lam::view_require_text(text));
        int fragments = 0;
        while (TextRect* rect = iterator.next()) {
            ASSERT_LT(fragments, 3) << ids[i];
            ASSERT_NE(expected[i][fragments], nullptr) << ids[i];
            lam::Temp<char> content(render_text_create_export_segment(text->text_data(),
                rect, CSS_VALUE_NONE, true, CSS_VALUE_NORMAL));
            EXPECT_STREQ(content.get(), expected[i][fragments]) << ids[i];
            if (fragments == 1) {
                EXPECT_NEAR(rect->y, i < 3 ? 50.0f : 24.4f, 0.05f) << ids[i];
            }
            fragments++;
        }
        EXPECT_EQ(fragments, i < 4 ? 3 : 2) << ids[i];
        if (i >= 4) EXPECT_NEAR(element->height, 46.8f, 0.05f) << ids[i];
    }
}

TEST_F(RadiantTextQuery, CjkHyphensKeepTheirPrecedingUnitExceptInLooseMode) {
    ASSERT_NE(load("test/html/layout_cjk_hyphen_boundaries.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; int fragments; } cases[] = {
        {"normal-hyphen", 66.0f, 3}, {"normal-dash", 66.0f, 3},
        {"strict-hyphen", 66.0f, 3}, {"auto-hyphen", 66.0f, 3},
        {"loose-hyphen", 44.0f, 2}, {"loose-japanese", 44.0f, 2},
        {"anywhere-hyphen", 44.0f, 2}, {"normal-inline", 66.0f, 3},
        {"normal-nowrap", 22.0f, 1}, {"intrinsic-normal", 44.0f, 2},
        {"intrinsic-loose", 66.0f, 3},
        {"intrinsic-anywhere", 66.0f, 3}, {"intrinsic-variable", 66.0f, 3},
        {"intrinsic-initial", 44.0f, 2},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
        int fragments = 0;
        auto visit = [&](View* view, int) -> bool {
            if (view->view_type == RDT_VIEW_TEXT) {
                for (TextRect* rect = lam::view_require_text(view)->rect; rect; rect = rect->next)
                    fragments++;
            }
            return true;
        };
        layout_walk_view_tree(element->first_child, visit, [](View*, int) {});
        EXPECT_EQ(fragments, strcmp(entry.id, "normal-inline") == 0 ? 4 : entry.fragments)
            << entry.id;
    }
    DomElement* normal = dom_find_element_by_id(document->root->as_element(), "intrinsic-normal");
    DomElement* loose = dom_find_element_by_id(document->root->as_element(), "intrinsic-loose");
    ASSERT_NE(normal, nullptr);
    ASSERT_NE(loose, nullptr);
    EXPECT_NEAR(normal->width, 21.328125f, 0.05f);
    EXPECT_FLOAT_EQ(loose->width, 16.0f);
}

TEST_F(RadiantTextQuery, HyphenationLookaheadRetainsOutOfFlowAndEmptyInlineAnchors) {
    ASSERT_NE(load("test/html/layout_hyphen_out_of_flow.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float height; bool marked; } cases[] = {
        {"manual-direct", 42.0f, true}, {"manual-abs-prefix", 42.0f, true},
        {"manual-abs-suffix", 42.0f, true}, {"manual-abs-boundary", 42.0f, true},
        {"manual-empty-boundary", 42.0f, true}, {"manual-abs-inline", 42.0f, true},
        {"manual-two-abs", 42.0f, true}, {"manual-relative", 42.0f, true},
        {"manual-br", 42.0f, false}, {"manual-space", 42.0f, false},
        {"manual-nowrap", 24.0f, false}, {"automatic-suffix", 42.0f, true},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
        bool first_text = true;
        auto visit = [&](View* view, int) -> bool {
            if (view->view_type != RDT_VIEW_TEXT) return true;
            ViewText* text = lam::view_require_text(view);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (first_text) {
                    EXPECT_EQ(rect->has_trailing_hyphen, entry.marked) << entry.id;
                    EXPECT_FLOAT_EQ(rect->y, 3.0f) << entry.id;
                    if (entry.marked) {
                        lam::Temp<char> content(render_text_create_export_segment(text->text_data(),
                            rect, CSS_VALUE_NONE, true, CSS_VALUE_NORMAL));
                        EXPECT_STREQ(content.get(), "high-") << entry.id;
                    }
                    first_text = false;
                } else if (entry.height > 24.0f) {
                    EXPECT_FLOAT_EQ(rect->y, 21.0f) << entry.id;
                }
            }
            return false;
        };
        layout_walk_view_tree(element->first_child, visit, [](View*, int) {});
        EXPECT_FALSE(first_text) << entry.id;
    }
    struct Anchor { const char* id; float x; float y; } anchors[] = {
        {"suffix-anchor", 21.65625f, 21.0f},
        {"boundary-anchor", 36.78125f, 3.0f},
        {"empty-boundary", 36.78125f, 3.0f},
        {"inline-anchor", 36.78125f, 3.0f},
        {"first-anchor", 36.78125f, 3.0f},
        {"second-anchor", 36.78125f, 3.0f},
        {"auto-suffix-anchor", 21.65625f, 21.0f},
    };
    for (const Anchor& entry : anchors) {
        DomElement* anchor = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(anchor, nullptr);
        float x = 0.0f, y = 0.0f, parent_x = 0.0f, parent_y = 0.0f;
        view_get_visual_bounds(anchor, &x, &y, nullptr, nullptr);
        view_get_visual_bounds(anchor->parent_view(), &parent_x, &parent_y, nullptr, nullptr);
        EXPECT_NEAR(x - parent_x, entry.x, 0.05f) << entry.id;
        EXPECT_NEAR(y - parent_y, entry.y, 0.05f) << entry.id;
    }
}

TEST_F(RadiantTextQuery, StaticPositionsStopAtTheirInlineContainingBlock) {
    ASSERT_NE(load("test/html/layout_inline_containing_block_static.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; } cases[] = {
        {"custom", 50.0f, 60.0f}, {"known", 50.0f, 60.0f}, {"block", 30.0f, 80.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float x = 0.0f, y = 0.0f;
        view_get_visual_bounds(element, &x, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
    }
}

TEST_F(RadiantTextQuery, AnonymousTableRolesOverrideMissingHTMLTagIdentity) {
    ASSERT_NE(load("test/html/layout_anonymous_table_intrinsic.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; float height; } cases[] = {
        {"cell", 20.0f, 60.0f}, {"row", 20.0f, 60.0f},
        {"inline", 60.0f, 20.0f}, {"wrapper", 20.0f, 60.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
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
