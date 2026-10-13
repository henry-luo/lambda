// Lambda text-measurement queries made through the radiant module, loaded
// in-process so the module's query caches are observable.
#include <gtest/gtest.h>
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom.h"

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
