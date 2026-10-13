// Lambda text-measurement queries made through the radiant module, loaded
// in-process so the module's query caches are observable.
#include <gtest/gtest.h>
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/render.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom.h"
#include "../lib/tagged.hpp"

extern "C" uint64_t radiant_query_font_build_count(void);

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
    };
    for (const Expected& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
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
