// in-process Lambda text-measurement queries and native text layout checks.
#include <gtest/gtest.h>
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom.h"
#include "../lib/tagged.hpp"

extern "C" uint64_t radiant_query_font_build_count(void);

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

class RadiantTextLayout : public RadiantTextQuery {};

TEST_F(RadiantTextLayout, ShadowMathKeepsLengthsDistinctFromColors) {
    ASSERT_NE(load("test/html/shadow_math.html"), nullptr);
    layout_html_doc(&ui, document, false);
    DomElement* result = dom_find_element_by_id(document->root->as_element(), "result");
    ViewElement* box = lam::view_require_element(result);
    ASSERT_NE(box->bound, nullptr);
    BoxShadow* outer = box->boundary()->box_shadow;
    ASSERT_NE(outer, nullptr);
    EXPECT_FLOAT_EQ(outer->offset_x, 10.0f);
    EXPECT_FLOAT_EQ(outer->offset_y, -5.0f);
    EXPECT_FLOAT_EQ(outer->blur_radius, 20.0f);
    EXPECT_FLOAT_EQ(outer->spread_radius, 2.0f);
    EXPECT_EQ(outer->color.r, 20);
    EXPECT_EQ(outer->color.g, 40);
    EXPECT_EQ(outer->color.b, 60);
    BoxShadow* inner = outer->next;
    ASSERT_NE(inner, nullptr);
    EXPECT_TRUE(inner->inset);
    EXPECT_FLOAT_EQ(inner->offset_x, 2.0f);
    EXPECT_FLOAT_EQ(inner->offset_y, 3.0f);
    EXPECT_FLOAT_EQ(inner->blur_radius, 4.0f);
    EXPECT_FLOAT_EQ(inner->spread_radius, 1.0f);
    EXPECT_EQ(inner->color.r, 0xab);

    DomElement* text = dom_find_element_by_id(document->root->as_element(), "text");
    ViewElement* span = lam::view_require_element(text);
    ASSERT_NE(span->font, nullptr);
    TextShadow* shadow = span->font->text_shadow;
    ASSERT_NE(shadow, nullptr);
    EXPECT_FLOAT_EQ(shadow->offset_x, 2.0f);
    EXPECT_FLOAT_EQ(shadow->offset_y, 3.0f);
    EXPECT_FLOAT_EQ(shadow->blur_radius, 4.0f);
    EXPECT_EQ(shadow->color.r, 70);
}

TEST_F(RadiantTextLayout, AnonymousFlexTextRetainsInheritedLineHeightAndLeading) {
    ui.viewport_width = 640;
    ui.viewport_height = 600;
    ASSERT_NE(load("test/html/flex_anonymous_line_height.html"), nullptr);
    layout_html_doc(&ui, document, false);

    struct TextPair { const char* direct; const char* wrapped; float height; };
    const TextPair pairs[] = {
        {"unitless-direct", "unitless-wrapped", 26.0f},
        {"large-direct", "large-wrapped", 38.0f},
        {"length-direct", "length-wrapped", 38.0f},
        {"normal-direct", "normal-wrapped", -1.0f},
        {"fixed-direct", "fixed-wrapped", 48.0f},
        {"column-direct", "column-wrapped", 26.0f},
        {"icon-direct", "icon-wrapped", 29.0f},
    };
    // direct text and an unstyled child span generate equivalent line boxes.
    // check painted text origins as well as outer sizes, including retained reflow.
    for (int pass = 0; pass < 2; pass++) {
        for (const TextPair& pair : pairs) {
            SCOPED_TRACE(pair.direct);
            DomElement* direct = dom_find_element_by_id(document->root->as_element(), pair.direct);
            DomElement* wrapped = dom_find_element_by_id(document->root->as_element(), pair.wrapped);
            ASSERT_NE(direct, nullptr);
            ASSERT_NE(wrapped, nullptr);
            ViewElement* direct_view = lam::view_require_element(direct);
            ViewElement* wrapped_view = lam::view_require_element(wrapped);
            ASSERT_NE(direct_view, nullptr);
            ASSERT_NE(wrapped_view, nullptr);
            EXPECT_NEAR(direct_view->height, wrapped_view->height, 0.1f);
            if (pair.height >= 0.0f) EXPECT_NEAR(direct_view->height, pair.height, 0.1f);

            DomText* direct_text = direct->last_child->as_text();
            DomText* wrapped_text = wrapped->last_child->as_element()->first_child->as_text();
            ASSERT_NE(direct_text->rect, nullptr);
            ASSERT_NE(wrapped_text->rect, nullptr);
            RdtLogicalPoint direct_origin = view_geometry_text_rect_document_origin(
                direct_text, direct_text->rect);
            RdtLogicalPoint wrapped_origin = view_geometry_text_rect_document_origin(
                wrapped_text, wrapped_text->rect);
            EXPECT_NEAR(direct_origin.y, wrapped_origin.y, 0.1f);
            EXPECT_NEAR(direct_text->rect->height, wrapped_text->rect->height, 0.1f);
        }
        if (pass == 0) layout_html_doc(&ui, document, true);
    }
}
