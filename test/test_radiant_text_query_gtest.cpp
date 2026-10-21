// in-process Lambda text queries, retained layout and frame delivery checks.
#include <gtest/gtest.h>
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/hyphenation.hpp"
#include "../radiant/render.hpp"
#include "../radiant/audio.hpp"
#include "../radiant/speech.hpp"
#include "../radiant/event.hpp"
#include "../radiant/network_integration.h"
#include "../radiant/resource_loaders.h"
#include "../radiant/css_resolved_style.hpp"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/dom/dom_cssom.h"
#include "../lambda/dom/dom_engine.h"
#include "../lambda/dom/dom_observers.h"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lib/tagged.hpp"
#include "radiant_render_test_utils.hpp"
#include "../lib/str.h"
#include "../lib/font/font.h"
#include "../lib/hash.h"
#include "../lib/file.h"

extern "C" uint64_t radiant_query_font_build_count(void);
ArrayList* build_view_stack(EventContext* context, View* target);
void fire_events(EventContext* context, ArrayList* stack);
void update_hover_state(EventContext* context, View* target);

TEST(LayoutAnonymousText, BorrowsTheResolvedFontBeforeIntrinsicMeasurement) {
    LayoutContext layout = {};
    ViewBlock container = {};
    container.node_type = DOM_NODE_ELEMENT;
    container.view_type = RDT_VIEW_BLOCK;
    DomText text = {};
    text.node_type = DOM_NODE_TEXT;
    text.view_type = RDT_VIEW_TEXT;
    text.parent = lam::up(static_cast<DomNode*>(&container));
    FontProp inherited = FONT_PROP_DEFAULT, parent_font = FONT_PROP_DEFAULT, text_font = FONT_PROP_DEFAULT;
    inherited.font_size = 17.0f;
    layout.font.style = lam::up(&inherited);
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    scratch_init(&layout.scratch, arena);
    {
        ScratchScope scope(&layout.scratch);
        struct Case { FontProp* parent; FontProp* text; FontProp* expected; };
        const Case cases[] = {{nullptr, nullptr, &inherited}, {&parent_font, nullptr, &parent_font},
                              {&parent_font, &text_font, &text_font}};
        for (const Case& entry : cases) {
            container.font = lam::view_prop(entry.parent);
            text.font = lam::view_ref(entry.text);
            ViewElement* item = layout_create_anonymous_text_item(
                &layout, &scope.mark, &container, &text);
            EXPECT_NE(item, nullptr);
            if (!item) continue;
            EXPECT_TRUE(item->scratch_lived());
            EXPECT_EQ(item->font.get(), entry.expected);
            // the later style query must reuse the borrowed font without requesting pool storage.
            EXPECT_EQ(item->ensure_font(&layout), entry.expected);
        }
    }
    scratch_release(&layout.scratch);
    arena_destroy(arena);
}

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
    void expect_box(const char* id, float wanted_x, float wanted_y,
                    float width, float height) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr) << id;
        float x = 0.0f, y = 0.0f;
        view_get_visual_bounds(element, &x, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, wanted_x) << id;
        EXPECT_FLOAT_EQ(y, wanted_y) << id;
        EXPECT_FLOAT_EQ(element->width, width) << id;
        EXPECT_FLOAT_EQ(element->height, height) << id;
    }
    void expect_fragments(const char* id, const Rect* wanted, size_t count) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr) << id;
        ASSERT_EQ((size_t)element->layout_fragments_count(), count) << id;
        size_t index = 0;
        for (LayoutFragmentBox* piece = element->layout_fragment_list(); piece; piece = piece->next, index++) {
            ASSERT_LT(index, count) << id;
            EXPECT_FLOAT_EQ(piece->x, wanted[index].x) << id;
            EXPECT_FLOAT_EQ(piece->y, wanted[index].y) << id;
            EXPECT_FLOAT_EQ(piece->width, wanted[index].width) << id;
            EXPECT_FLOAT_EQ(piece->height, wanted[index].height) << id;
        }
    }
    DomElement* load(const char* path, bool auto_close = false) {
        DocumentJsHostConfig host = {}; host.ui_context = &ui; host.resource_policy = INPUT_RESOURCE_LOCAL_ONLY;
        host.auto_close_event_loop = auto_close;
        host.virtual_clock_enabled = auto_close;
        Url* base = get_current_dir();
        if (!base) return nullptr;
        document = load_html_doc(base, (char*)path, 320, 200, &host);
        url_destroy(base);
        if (!document || !document->root) return nullptr;
        ui.document = lam::up(document);
        return dom_find_element_by_id(document->root->as_element(), "result");
    }
    DomElement* load_script_source(const char* source) {
        Url* base = get_current_dir();
        if (!base) return nullptr;
        Url* url = url_parse_path_or_url("test/html/navigation.html", base);
        url_destroy(base);
        Pool* pool = pool_create();
        if (!url || !pool) {
            if (url) url_destroy(url);
            if (pool) pool_destroy(pool);
            return nullptr;
        }
        DocumentJsHostConfig host = {}; host.ui_context = &ui;
        host.resource_policy = INPUT_RESOURCE_LOCAL_ONLY;
        host.auto_close_event_loop = true;
        host.virtual_clock_enabled = true;
        // retain the redirecting document itself, before the outer loader follows it.
        document = load_lambda_html_doc(url, nullptr, 320, 200, pool,
            source, false, true, &host);
        if (!document) { url_destroy(url); pool_destroy(pool); return nullptr; }
        if (!dom_document_finalize_loader_pool(document, pool) || !document->root) return nullptr;
        ui.document = lam::up(document);
        return dom_find_element_by_id(document->root->as_element(), "result");
    }
};

TEST_F(RadiantTextQuery, CrossDocumentNavigationFinishesMicrotasksWithoutDrainingOldTimers) {
    DomElement* result = load_script_source(R"HTML(<div id="result"></div><script>
        var result=document.getElementById('result');
        setTimeout(function(){result.setAttribute('data-timer','yes')},0);
        Promise.resolve().then(function(){result.setAttribute('data-microtask','yes')});
        location.replace('next.html');
        result.setAttribute('data-sync','yes');
    </script>)HTML");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->get_attribute("data-sync"), "yes");
    EXPECT_STREQ(result->get_attribute("data-microtask"), "yes");
    EXPECT_EQ(result->get_attribute("data-timer"), nullptr);
    ASSERT_NE(document->pending_navigation_url, nullptr);
    EXPECT_NE(strstr(document->pending_navigation_url, "next.html"), nullptr);
}

TEST_F(RadiantTextQuery, UnicodeEmailPatternSurvivesDirectMatcherResourceLimit) {
    DomElement* result = load_script_source(R"HTML(<div id="result"></div><script>
        var email = /[\p{L}\p{M}\p{N}._%+'-]{1,64}(?:[@\uFF20\uFE6B]|%40)[\p{L}\p{M}\p{N}.\u3002\uFF0E\uFF61-]{1,255}[.\u3002\uFF0E\uFF61][\p{L}\p{M}][\p{L}\p{M}\p{N}-]{1,62}/giu;
        var samples = ['henry@example.com', 'hénry＠例子．公司', 'a﹫b｡com', 'not-an-email'];
        var matches = samples.map(function(sample) { email.lastIndex = 0; return email.test(sample); });
        document.getElementById('result').setAttribute('data-matches', JSON.stringify(matches));
        document.getElementById('result').setAttribute('data-flags', email.flags);
    </script>)HTML");
    ASSERT_NE(result, nullptr);
    // the document realm exercises native matcher routing, unlike the CLI's MVP RegExp.
    EXPECT_STREQ(result->get_attribute("data-matches"), "[true,true,true,false]");
    EXPECT_STREQ(result->get_attribute("data-flags"), "giu");
}

TEST_F(RadiantTextQuery, FragmentNavigationRetainsCurrentDocumentTimers) {
    DomElement* result = load_script_source(R"HTML(<div id="result"></div><script>
        setTimeout(function(){document.getElementById('result').setAttribute('data-timer','yes')},0);
        location.replace('#result');
    </script>)HTML");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->get_attribute("data-timer"), "yes");
}

TEST_F(RadiantTextQuery, NavigationClassificationUsesTheCompletedMicrotaskCheckpoint) {
    DomElement* result = load_script_source(R"HTML(<div id="result"></div><script>
        setTimeout(function(){document.getElementById('result').setAttribute('data-timer','yes')},0);
        location.replace('next.html');
        Promise.resolve().then(function(){location.replace('#result')});
    </script>)HTML");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->get_attribute("data-timer"), "yes");
}

TEST_F(RadiantTextQuery, InlineSizeQueriesSettleGeneratedBlockExtent) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        body { margin:0 }
        #container { container-type:inline-size; width:120px }
        @container (min-width:100px) {
            #result::before { content:'generated'; display:block; height:30px }
        }
    </style><div id="container"><div id="result"></div></div>)HTML"), nullptr);
    // generated content's uncontained height is not a size-query input.
    for (unsigned pass = 0; pass < 2; ++pass) {
        layout_html_doc(&ui, document, pass != 0);
        expect_box("container", 0, 0, 120, 30);
        expect_box("result", 0, 0, 120, 30);
    }
}

TEST_F(RadiantTextQuery, CachedSvgDeliveryUsesItsOwnerBeforePublicationAndAcrossDocuments) {
    ASSERT_NE(load_script_source("<div id='result'><img id='image'></div>"), nullptr);
    DomElement* image = dom_find_element_by_id(document->root->as_element(), "image");
    ASSERT_NE(image, nullptr);
    char path[] = "temp/radiant_network_svg_owner.cache";
    ASSERT_EQ(write_text_file_atomic(path,
        "<svg xmlns='http://www.w3.org/2000/svg' width='23' height='17'>"
        "<rect width='23' height='17' fill='red'/></svg>"), 0);
    ASSERT_EQ(radiant_init_network_support(document, nullptr, nullptr), 0);
    resource_manager_set_ui_context(document->resource_manager, &ui);
    DomDocument other = {};
    char url[] = "https://example.test/extensionless-image";
    NetworkResource resource = {};
    resource.url = url;
    resource.local_path = path;
    resource.type = RESOURCE_IMAGE;
    resource.state = STATE_CACHED;
    resource.manager = document->resource_manager;
    for (DomDocument* published : {static_cast<DomDocument*>(nullptr), &other}) {
        ui.document = lam::up(published);
        process_image_resource(&resource, image);
        EXPECT_EQ(ui.document.get(), published);
        EXPECT_TRUE(resource.image_surface_borrowed);
        EXPECT_NE(resource.image_surface, nullptr);
        if (resource.image_surface) {
            EXPECT_EQ(resource.image_surface->format, IMAGE_FORMAT_SVG);
            EXPECT_EQ(resource.image_surface->width, 23);
            EXPECT_EQ(resource.image_surface->height, 17);
            ASSERT_NE(image->embed, nullptr);
            EXPECT_EQ(image->embed->img.get(), resource.image_surface);
        }
    }
    ui.document = lam::up(document);
    file_delete(path);
}

TEST_F(RadiantTextQuery, DetachedFontDiscoveryDeliversCachedFacesAfterTheHostBinds) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        @font-face { font-family:DetachedFixture; src:url(https://example.test/detached.ttf) }
        #result { font-family:DetachedFixture }
    </style><div id="result">A</div>)HTML"), nullptr);
    EnhancedFileCache* cache = enhanced_cache_create("temp/detached_font_cache", 1024 * 1024, 10);
    ASSERT_NE(cache, nullptr);
    ASSERT_EQ(radiant_init_network_support(document, nullptr, cache), 0);
    document->resource_manager->owns_file_cache = true;
    size_t length = 0;
    lam::Temp<char> bytes(read_binary_file("test/ui/svg_font_assets/rectangle.ttf", &length));
    ASSERT_NE(bytes.get(), nullptr);
    const char* url = "https://example.test/detached.ttf";
    lam::Temp<char> path(enhanced_cache_store(cache, url, bytes.get(), length, nullptr));
    ASSERT_NE(path.get(), nullptr);
    document->resource_policy = INPUT_RESOURCE_ALLOW_NETWORK;
    DocumentJsHostConfig detached = {};
    document_apply_js_host_config(document, &detached);
    resource_manager_set_ui_context(document->resource_manager, nullptr);
    radiant_discover_document_font_resources(document);
    NetworkResource* resource = resource_manager_load(document->resource_manager,
        url, RESOURCE_FONT, PRIORITY_HIGH, nullptr);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->state, STATE_CACHED);
    EXPECT_EQ(resource->on_complete, nullptr);
    EXPECT_FALSE(font_face_family_registered(ui.font_ctx, "DetachedFixture"));

    layout_iframe_embedded_doc(&ui, document, 320, 200);
    EXPECT_EQ(document->resource_manager->ui_context, &ui);
    EXPECT_TRUE(font_face_family_registered(ui.font_ctx, "DetachedFixture"));
    EXPECT_TRUE(resource->processed);
    EXPECT_EQ(ui.document.get(), document);

    DocumentJsHostConfig attached = {};
    attached.ui_context = &ui;
    document_apply_js_host_config(document, &detached);
    EXPECT_EQ(document->resource_manager->ui_context, nullptr);
    document_apply_js_host_config(document, &attached);
    EXPECT_EQ(document->resource_manager->ui_context, &ui);
}

TEST_F(RadiantTextQuery, FetchPreloadRetainsBytesWithoutImageDecodingOrScriptExecution) {
    ASSERT_NE(load_script_source(R"HTML(<link rel="preload" as="fetch"
        href="https://example.test/preload.js"><div id="result"></div>)HTML"), nullptr);
    EnhancedFileCache* cache = enhanced_cache_create("temp/raw_preload_cache", 1024 * 1024, 10);
    ASSERT_NE(cache, nullptr);
    ASSERT_EQ(radiant_init_network_support(document, nullptr, cache), 0);
    document->resource_manager->owns_file_cache = true;
    const char* url = "https://example.test/preload.js";
    const char* source = "document.getElementById('result').setAttribute('data-executed','yes')";
    lam::Temp<char> path(enhanced_cache_store(cache, url, source, strlen(source), nullptr));
    ASSERT_NE(path.get(), nullptr);
    document->resource_policy = INPUT_RESOURCE_ALLOW_NETWORK;
    radiant_discover_document_resources(document);
    NetworkResource* resource = resource_manager_prefetch(document->resource_manager, url, PRIORITY_NORMAL);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->type, RESOURCE_PREFETCH);
    EXPECT_EQ(resource->owner_element, nullptr);
    EXPECT_EQ(resource->image_surface, nullptr);
    EXPECT_TRUE(resource->processed);
    size_t length = 0;
    lam::Temp<char> bytes(resource_manager_copy_resource_content(
        document->resource_manager, url, PRIORITY_NORMAL, &length));
    ASSERT_NE(bytes.get(), nullptr);
    EXPECT_EQ(length, strlen(source));
    EXPECT_EQ(memcmp(bytes.get(), source, length), 0);
    DomElement* result = dom_find_element_by_id(document->root->as_element(), "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->get_attribute("data-executed"), nullptr);
}

TEST_F(RadiantTextQuery, NestedMicrotaskNavigationDefersDiscardedDocumentTimers) {
    DomElement* result = load_script_source(R"HTML(<div id="result"></div><script>
        var result=document.getElementById('result');
        setTimeout(function(){result.setAttribute('data-timer','ran')},0);
        location.replace('#result');
        Promise.resolve().then(function(){
            result.setAttribute('data-first','yes');
            Promise.resolve().then(function(){
                location.replace('next.html');
                result.setAttribute('data-last','yes');
            });
        });
    </script>)HTML");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->get_attribute("data-first"), "yes");
    EXPECT_STREQ(result->get_attribute("data-last"), "yes");
    EXPECT_EQ(result->get_attribute("data-timer"), nullptr);
    ASSERT_NE(document->pending_navigation_url, nullptr);
    EXPECT_NE(strstr(document->pending_navigation_url, "next.html"), nullptr);
}

TEST_F(RadiantTextQuery, AbsoluteSameDocumentNavigationRetainsTimers) {
    DomElement* result = load_script_source(R"HTML(<div id="result"></div><script>
        setTimeout(function(){document.getElementById('result').setAttribute('data-timer','yes')},0);
        location.replace(location.href.split('#')[0]+'#result');
    </script>)HTML");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->get_attribute("data-timer"), "yes");
}

TEST_F(RadiantTextQuery, InlineSizeQueryReevaluatesRealWidthChangesAndRepublishesOnce) {
    DomElement* result = load_script_source(R"HTML(<style>
        body { margin:0 }
        #container { container-type:inline-size; width:120px }
        #result::before { content:'generated'; display:block; height:10px }
        @container (min-width:100px) { #result::before { height:30px } }
    </style><div id="container"><div id="result"></div></div>)HTML");
    ASSERT_NE(result, nullptr);
    DomElement* container = dom_find_element_by_id(document->root->as_element(), "container");
    ASSERT_NE(container, nullptr);
    layout_html_doc(&ui, document, false);
    expect_box("result", 0, 0, 120, 30);
    for (unsigned pass = 0; pass < 4; ++pass) {
        bool narrow = pass % 2 == 0;
        ASSERT_TRUE(container->set_attribute("style", narrow ? "width:80px" : "width:120px"));
        uint64_t layouts = document->view_tree->document_layout_count;
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        EXPECT_EQ(document->view_tree->document_layout_count, layouts + 1);
        expect_box("container", 0, 0, narrow ? 80.0f : 120.0f, narrow ? 10.0f : 30.0f);
        expect_box("result", 0, 0, narrow ? 80.0f : 120.0f, narrow ? 10.0f : 30.0f);
        layouts = document->view_tree->document_layout_count;
        for (unsigned read = 0; read < 16; ++read) {
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            ASSERT_TRUE(layout_update_document(&ui, document));
        }
        EXPECT_EQ(document->view_tree->document_layout_count, layouts);
    }
}

TEST_F(RadiantTextQuery, ContainerSettlementKeepsCommittedGeometryWithFixedPseudoDescendants) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        body { margin:0; font:16px Arial }
        #row { display:flex; width:120px; align-items:flex-start; min-height:100vh }
        #panel footer::after { content:''; position:fixed; top:0; left:0;
            width:100vw; height:100vh; visibility:hidden; opacity:0 }
        #result { container-type:inline-size; position:relative }
        #content { height:10px }
        @container (min-width:100px) { #content { height:30px } }
    </style><div id="row"><div><div></div></div><div id="panel"><footer>
        <div id="result"><div id="content"></div></div>
    </footer></div></div>)HTML"), nullptr);
    // a fixed generated descendant cannot supply the auto-sized flex item's intrinsic width.
    layout_html_doc(&ui, document, false);
    expect_box("result", 0, 0, 0, 10);
    ASSERT_TRUE(layout_update_document(&ui, document));
    uint64_t layouts = document->view_tree->document_layout_count;
    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
    EXPECT_EQ(document->view_tree->document_layout_count, layouts);

    DomElement* panel = dom_find_element_by_id(document->root->as_element(), "panel");
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(panel->set_attribute("style", "width:200px;flex:none"));
    ASSERT_TRUE(layout_update_document(&ui, document));
    expect_box("result", 0, 0, 200, 30);
    ASSERT_TRUE(panel->remove_attribute("style"));
    ASSERT_TRUE(layout_update_document(&ui, document));
    expect_box("result", 0, 0, 0, 10);
}

TEST_F(RadiantTextQuery, IncrementalContainerReflowKeepsUnchangedSiblingStyles) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source, R"HTML(<style>body{margin:0}
        #container{container-type:inline-size;width:300px}
        #result{height:20px}.cold{height:3px}
        @container(min-width:250px){#result{color:rgb(1,2,3)}}
        </style><div id="container"><div id="result"></div>)HTML");
    for (unsigned i = 0; i < 40; i++) strbuf_append_str(source, "<div class='cold'>text</div>");
    strbuf_append_str(source, "</div>");
    DomElement* result = load_script_source(source->str);
    strbuf_free(source);
    ASSERT_NE(result, nullptr);
    layout_html_doc(&ui, document, false);
    expect_box("result", 0, 0, 300, 20);
    uint64_t layouts = document->view_tree->document_layout_count;
    ASSERT_TRUE(dom_cssom_set_inline_property(result, "height", "30px"));
    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
    expect_box("result", 0, 0, 300, 30);
    EXPECT_EQ(document->view_tree->document_layout_count, layouts + 1);
    // a committed query context must not force cold resolution of forty clean siblings.
    EXPECT_LT(g_style_resolve_full, 10);
}

TEST_F(RadiantTextQuery, PseudoFontsOwnInheritedTextShadowsAcrossRetainedReset) {
    DomElement* result = load_script_source(R"HTML(<style>
        #result{text-shadow:1px 2px 3px red,4px 5px 6px blue}
        #result::before{content:'before'}#result::after{content:'after'}
        </style><div id="result"><span id="child">text</span></div>)HTML");
    ASSERT_NE(result, nullptr);
    for (unsigned pass = 0; pass < 3; pass++) {
        layout_html_doc(&ui, document, false);
        ASSERT_NE(result->font, nullptr);
        ASSERT_NE(result->pseudo, nullptr);
        TextShadow* original = result->font->text_shadow;
        ASSERT_NE(original, nullptr);
        DomElement* copies[] = {result->pseudo->before, result->pseudo->after};
        for (DomElement* copy : copies) {
            ASSERT_NE(copy, nullptr);
            ASSERT_NE(copy->font, nullptr);
            TextShadow* shadow = copy->font->text_shadow;
            ASSERT_NE(shadow, nullptr) << copy->node_name();
            EXPECT_NE(shadow, original);
            ASSERT_NE(shadow->next, nullptr);
            EXPECT_NE(shadow->next.get(), original->next.get());
            EXPECT_FLOAT_EQ(shadow->offset_x, 1.0f);
            EXPECT_FLOAT_EQ(shadow->next->offset_x, 4.0f);
            EXPECT_GT(pool_allocation_size(document->view_tree->prop_pool, shadow), 0u);
        }
    }
}

TEST_F(RadiantTextQuery, PreparedStylesKeepInheritedFontsAndRebindControlPercentages) {
    ASSERT_NE(load("test/html/layout_prepared_context.html"), nullptr);
    DomElement* code = dom_find_element_by_id(document->root->as_element(), "code");
    DomElement* control = dom_find_element_by_id(document->root->as_element(), "control");
    DomElement* closed = dom_find_element_by_id(document->root->as_element(), "closed");
    DomElement* generated = dom_find_element_by_id(document->root->as_element(), "generated-item");
    ASSERT_NE(code, nullptr);
    ASSERT_NE(control, nullptr);
    ASSERT_NE(closed, nullptr);
    ASSERT_NE(generated, nullptr);
    for (unsigned pass = 0; pass < 3; pass++) {
        if (pass) ASSERT_TRUE(code->set_attribute("style", pass == 1
            ? "background:blue" : "background:silver"));
        layout_html_doc(&ui, document, pass != 0);
        ASSERT_NE(code->font.get(), nullptr);
        EXPECT_FLOAT_EQ(code->font->font_size, 13.0f);
        EXPECT_FLOAT_EQ(control->width, 100.0f);
        EXPECT_EQ(closed->view_type, RDT_VIEW_NONE);
        ASSERT_NE(generated->pseudo.get(), nullptr);
        DomElement* after = generated->pseudo->after;
        ASSERT_NE(after, nullptr);
        ASSERT_NE(after->pseudo.get(), nullptr);
        ASSERT_NE(after->pseudo->marker.get(), nullptr);
        MarkerProp* marker = after->pseudo->marker->marker_prop();
        ASSERT_NE(marker, nullptr);
        ASSERT_NE(marker->text_content.get(), nullptr);
        EXPECT_STREQ(marker->text_content.get(), "2. ");
        unsigned text_count = 0;
        for (DomNode* child = after->first_child; child; child = child->next_sibling) {
            if (!child->is_text()) continue;
            text_count++;
            EXPECT_STREQ(child->as_text()->text, "tail");
        }
        EXPECT_EQ(text_count, 1u);
    }
}

TEST_F(RadiantTextQuery, SharedPublicationCommitsForeignFrameWritesWithoutParentLayout) {
    ASSERT_NE(load("test/html/layout_shared_iframe_publication.html"), nullptr);
    layout_html_doc(&ui, document, false);
    DomElement* frame = dom_find_element_by_id(document->root->as_element(), "frame");
    ASSERT_NE(frame, nullptr);
    ASSERT_NE(frame->embed.get(), nullptr);
    DomDocument* embedded = frame->embed->doc;
    ASSERT_NE(embedded, nullptr);
    // script-less srcdoc has no evaluator binding; supply the viewer's host for this frame tick.
    DocumentJsHostConfig host = document_js_host_config_inherit(&ui, document);
    document_apply_js_host_config(embedded, &host);
    DomElement* body = radiant_document_body_element(embedded);
    ASSERT_NE(body, nullptr);
    uint64_t parent_passes = document->view_tree->document_layout_count;
    uint64_t child_passes = embedded->view_tree->document_layout_count;
    ASSERT_TRUE(body->set_attribute("style", "margin:0;width:100px;height:20px;transform:scale(2)"));
    dom_notify_mutation(DOM_JS_MUTATION_INLINE_STYLE, body, body->parent);
    radiant_tick_document_animations(document, 0.0, true, false);
    EXPECT_EQ(embedded->js.mutation_count, 0);
    EXPECT_FALSE(body->needs_style_recompute());
    char transform[128];
    ASSERT_TRUE(css_prop_serialize_computed(body, CSS_PROPERTY_TRANSFORM, 0, transform, sizeof(transform)));
    EXPECT_STREQ(transform, "matrix(2, 0, 0, 2, 0, 0)");
    EXPECT_EQ(document->view_tree->document_layout_count, parent_passes);
    EXPECT_LE(embedded->view_tree->document_layout_count, child_passes + 1);
    child_passes = embedded->view_tree->document_layout_count;
    radiant_tick_document_animations(document, 0.01, true, false);
    EXPECT_EQ(embedded->view_tree->document_layout_count, child_passes);
}

TEST_F(RadiantTextQuery, SharedPublicationPreservesScriptGeometryAndInvalidatesInputs) {
    DomElement* result = load("test/html/layout_shared_publication.html", true);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(document->view_tree, nullptr);
    ViewTree* tree = document->view_tree;
    EXPECT_EQ(tree->document_layout_count, 1u);
    DomElement* text_box = dom_find_element_by_id(result, "text");
    ASSERT_NE(text_box, nullptr);
    DomText* text = text_box->first_child->as_text();
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->rect, nullptr);
    TextRect* fragments = text->rect;
    Pool* pool = tree->prop_pool;
    uint32_t generation = tree->layout_generation;
    for (unsigned read = 0; read < 3; read++) {
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        ASSERT_TRUE(layout_update_document(&ui, document));
        EXPECT_EQ(document->view_tree, tree);
        EXPECT_EQ(tree->prop_pool, pool);
        EXPECT_EQ(tree->layout_generation, generation);
        EXPECT_EQ(tree->document_layout_count, 1u);
        EXPECT_EQ(text->rect, fragments);
        EXPECT_FLOAT_EQ(text_box->height, 60.0f);
        EXPECT_FLOAT_EQ(dom_find_element_by_id(result, "probe-title")->height, 48.0f);
    }
    ASSERT_TRUE(dom_text_set_content(text, "XX"));
    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
    EXPECT_EQ(tree->document_layout_count, 2u);
    EXPECT_FLOAT_EQ(text_box->height, 20.0f);
    ASSERT_TRUE(text_box->set_attribute("style", "height:35px"));
    dom_notify_mutation(DOM_JS_MUTATION_INLINE_STYLE, text_box, text_box->parent);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_EQ(tree->document_layout_count, 3u);
    EXPECT_FLOAT_EQ(text_box->height, 35.0f);
    DomElement* title = dom_find_element_by_id(result, "probe-title");
    EXPECT_FLOAT_EQ(title->height, 48.0f) << "given=" << title->block()->given_height
        << " assigned=" << title->fi->main_size_from_flex << " content=" << title->content_height;
    ui.viewport_width = 640.0f;
    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
    EXPECT_EQ(tree->document_layout_count, 4u);
    EXPECT_FLOAT_EQ(tree->root->width, 640.0f);
    doc_state_request_reflow(document->state);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_EQ(tree->document_layout_count, 5u);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_EQ(tree->document_layout_count, 5u);
    reflow_schedule(document->state, text_box, REFLOW_SUBTREE, 0);
    ASSERT_NE(document->state->reflow_scheduler.pending, nullptr);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_EQ(tree->document_layout_count, 6u);
    EXPECT_EQ(document->state->reflow_scheduler.pending, nullptr);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_EQ(tree->document_layout_count, 6u);
}

TEST_F(RadiantTextQuery, SharedPublicationRegeneratesTableListAndPseudoStructure) {
    DomElement* result = load("test/html/layout_shared_publication.html", true);
    ASSERT_NE(result, nullptr);
    auto find = [&](const char* id) { return dom_find_element_by_id(result, id); };
    ui.window_width = 320.0f;
    ui.window_height = 240.0f;
    ui.create_surface(ui.window_width, ui.window_height);
    for (unsigned update = 0; update < 3; update++) {
        SCOPED_TRACE(update);
        EXPECT_FLOAT_EQ(find("cell-a")->width, 40.0f);
        EXPECT_FLOAT_EQ(find("cell-b")->width, 40.0f);
        EXPECT_FLOAT_EQ(find("table")->height, 20.0f);
        EXPECT_FLOAT_EQ(find("item-b")->y - find("item-a")->y, 20.0f);
        for (const char* id : {"item-a", "item-b"}) {
            ASSERT_NE(find(id)->pseudo, nullptr);
            ASSERT_NE(find(id)->pseudo->marker, nullptr);
            EXPECT_EQ(find(id)->pseudo->marker->view_type, RDT_VIEW_MARKER);
        }
        ASSERT_NE(find("generated")->pseudo, nullptr);
        ASSERT_NE(find("generated")->pseudo->before, nullptr);
        EXPECT_FLOAT_EQ(find("generated")->height, 20.0f);
        float svg_x = 0.0f, svg_y = 0.0f;
        view_get_visual_bounds(find("picture"), &svg_x, &svg_y, nullptr, nullptr);
        render_html_doc(&ui, document->view_tree, nullptr);
        ASSERT_FALSE(ui.render_failed);
        test_radiant_expect_surface_pixel(ui.surface,
            (unsigned)roundf(svg_x + 10.0f), (unsigned)roundf(svg_y + 10.0f), 255, 0, 0);
        // explicitly replace geometry; source normalization must be repeatable.
        layout_html_doc(&ui, document, false);
        ASSERT_TRUE(layout_update_document(&ui, document));
    }
    auto change_style = [&](const char* id, const char* value) {
        DomElement* element = find(id);
        ASSERT_NE(element, nullptr);
        ASSERT_TRUE(value ? element->set_attribute("style", value) : element->remove_attribute("style"));
        dom_notify_mutation(DOM_JS_MUTATION_INLINE_STYLE, element, element->parent);
        ASSERT_TRUE(layout_update_document(&ui, document));
    };
    change_style("cell-a", "display:none");
    EXPECT_FLOAT_EQ(find("cell-b")->x, 0.0f);
    change_style("cell-a", nullptr);
    EXPECT_FLOAT_EQ(find("cell-b")->x - find("cell-a")->x, 40.0f);
    change_style("item-a", "display:block");
    if (find("item-a")->pseudo && find("item-a")->pseudo->marker)
        EXPECT_NE(find("item-a")->pseudo->marker->parent, find("item-a"));
    change_style("item-a", nullptr);
    ASSERT_NE(find("item-a")->pseudo, nullptr);
    ASSERT_NE(find("item-a")->pseudo->marker, nullptr);
    EXPECT_EQ(find("item-a")->pseudo->marker->parent, find("item-a"));
}

TEST_F(RadiantTextQuery, SharedPublicationInvalidatesNewFontResources) {
    DomElement* result = load("test/html/layout_shared_publication.html", true);
    ASSERT_NE(result, nullptr);
    DomElement* box = dom_find_element_by_id(result, "text");
    ASSERT_NE(box, nullptr);
    ASSERT_TRUE(box->set_attribute("style", "font-family:LatePublicationMetrics"));
    ASSERT_TRUE(layout_update_document(&ui, document));
    uint64_t count = document->view_tree->document_layout_count;
    uint64_t generation = font_context_resource_generation(ui.font_ctx);
    FontFaceSource source = {};
    source.path = "test/layout/data/support/fonts/Ahem.ttf";
    FontFaceDesc face = {};
    face.family = "LatePublicationMetrics";
    face.weight = FONT_WEIGHT_NORMAL;
    face.slant = FONT_SLANT_NORMAL;
    face.sources = &source;
    face.source_count = 1;
    ASSERT_TRUE(font_face_register(ui.font_ctx, &face));
    ASSERT_NE(font_context_resource_generation(ui.font_ctx), generation);
    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
    EXPECT_EQ(document->view_tree->document_layout_count, count + 1);
    EXPECT_FLOAT_EQ(box->height, 60.0f);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_EQ(document->view_tree->document_layout_count, count + 1);
}

TEST_F(RadiantTextQuery, SharedPublicationNormalizesMergedRunsAndScriptScroll) {
    DomElement* result = load("test/html/layout_shared_publication_mutations.html", true);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(layout_update_document(&ui, document));
    auto find = [&](const char* id) { return dom_find_element_by_id(result, id); };
    EXPECT_FLOAT_EQ(find("append-inline")->height, 40.0f);
    ASSERT_TRUE(find("append-inline")->last_child->is_text());
    EXPECT_NE(find("append-inline")->last_child->as_text()->rect, nullptr);
    EXPECT_FLOAT_EQ(find("cell-b")->x - find("cell-a")->x, 50.0f);
    EXPECT_FLOAT_EQ(find("cell-b")->y, find("cell-a")->y);
    float row_a_x = 0.0f, row_a_y = 0.0f, row_b_x = 0.0f, row_b_y = 0.0f;
    view_get_visual_bounds(find("row-a"), &row_a_x, &row_a_y, nullptr, nullptr);
    view_get_visual_bounds(find("row-b"), &row_b_x, &row_b_y, nullptr, nullptr);
    EXPECT_FLOAT_EQ(row_b_y - row_a_y, 100.0f);
    EXPECT_FLOAT_EQ(row_b_x, row_a_x);
    EXPECT_FLOAT_EQ(find("figure")->width, 60.0f);
    DomElement* scroller = find("scroller");
    ASSERT_NE(scroller->scroll()->pane, nullptr);
    EXPECT_FLOAT_EQ(scroller->scroll()->pane->v_scroll_position, 150.0f);
    float scroller_y = 0.0f, sticky_y = 0.0f;
    view_get_visual_bounds(scroller, nullptr, &scroller_y, nullptr, nullptr);
    view_get_visual_bounds(find("sticky"), nullptr, &sticky_y, nullptr, nullptr);
    EXPECT_FLOAT_EQ(sticky_y - scroller_y, 5.0f);
    EXPECT_FLOAT_EQ(find("control-slot")->width, 144.0f);
    EXPECT_FLOAT_EQ(find("control")->width, 144.0f);
    radiant_run_behavior_init(document);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_FLOAT_EQ(find("control-slot")->width, 144.0f);
    EXPECT_FLOAT_EQ(find("control")->width, 144.0f)
        << "box=" << find("control")->block()->box_sizing;
    uint64_t count = document->view_tree->document_layout_count;
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_EQ(document->view_tree->document_layout_count, count);
    doc_state_request_reflow(document->state);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_FLOAT_EQ(find("control-slot")->width, 144.0f);
    EXPECT_FLOAT_EQ(find("control")->width, 144.0f);
}

TEST_F(RadiantTextQuery, ComputedSizesRemainSeparateFromInlineAndAtomicFlow) {
    ASSERT_NE(load("test/html/layout_computed_box_roles.html"), nullptr);
    process_document_font_faces(&ui, document);
    ui.viewport_width = 320;
    ui.viewport_height = 240;
    for (unsigned pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        struct Sample { const char* id; float width, height; } samples[] = {
            {"inline", 40.0f, 20.0f}, {"atomic", 100.0f, 70.0f}, {"item", 50.0f, 20.0f},
            {"cyclic", 200.0f, 36.0f}, {"cyclic-math", 200.0f, 28.0f}, {"definite", 320.0f, 50.0f},
            {"ratio-intrinsic", 100.0f, 25.0f}, {"cyclic-width", 100.0f, 100.0f},
            {"ratio-percentage", 100.0f, 100.0f}, {"intrinsic-fit-limit", 200.0f, 50.0f},
        };
        for (const Sample& sample : samples) {
            DomElement* box = dom_find_element_by_id(document->root->as_element(), sample.id);
            ASSERT_NE(box, nullptr);
            EXPECT_FLOAT_EQ(box->width, sample.width) << sample.id;
            EXPECT_FLOAT_EQ(box->height, sample.height) << sample.id;
        }
        DomElement* inline_box = dom_find_element_by_id(document->root->as_element(), "inline");
        EXPECT_FLOAT_EQ(inline_box->x, 0.0f);
        EXPECT_FLOAT_EQ(inline_box->block()->given_width, 100.0f);
        EXPECT_FLOAT_EQ(inline_box->block()->given_height, 70.0f);
    }
}

struct FlexFlowTestContext {
    LayoutContext layout = {};
    FlexContainerLayout parent = {};
    BlockContext containing = {};
    Arena* arena;

    FlexFlowTestContext(UiContext* ui, DomDocument* document, DomElement* item)
        : arena(arena_create_default()) {
        layout.doc = lam::up(document);
        layout.ui_context = lam::up(ui);
        layout.pool = lam::up(document->view_tree->prop_pool.get());
        layout.selected_view_tree = lam::up(document->view_tree.get());
        layout.view = lam::up(static_cast<View*>(item));
        layout.elmt = lam::up(static_cast<DomNode*>(item));
        layout.width = 320.0f;
        layout.height = 240.0f;
        layout.root_font_size = 16.0f;
        layout.run_mode = radiant::RunMode::PerformLayout;
        layout.node_limit = 1000;
        parent.direction = CSS_VALUE_ROW;
        layout.flex_container = lam::up(&parent);
        containing.content_width = containing.given_width = 160.0f;
        containing.given_height = -1.0f;
        layout.block.parent = lam::up(&containing);
        if (arena) scratch_init(&layout.scratch, arena);
    }

    ~FlexFlowTestContext() {
        scratch_release(&layout.scratch);
        if (arena) arena_destroy(arena);
    }
};

TEST_F(RadiantTextQuery, CompletedFlexTextFlowSurvivesStretchAndInvalidatesConstraints) {
    ASSERT_NE(load("test/html/layout_flex_completed_text.html"), nullptr);
    process_document_font_faces(&ui, document);
    ui.viewport_width = 320;
    ui.viewport_height = 240;
    auto find = [&](const char* id) { return dom_find_element_by_id(document->root->as_element(), id); };
    for (unsigned pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        for (const char* id : {"wrapped", "stretched", "padded", "baseline"}) {
            DomElement* item = find(id);
            ASSERT_NE(item, nullptr);
            ASSERT_NE(item->layout_cache, nullptr);
            EXPECT_NE(item->layout_cache->measured_flex_flow, nullptr) << id;
        }
        EXPECT_FLOAT_EQ(find("wrapped")->height, 60.0f);
        EXPECT_FLOAT_EQ(find("stretched")->height, 100.0f);
        EXPECT_FLOAT_EQ(find("padded")->height, 74.0f);
        EXPECT_FLOAT_EQ(find("baseline")->height, 20.0f);
        EXPECT_FLOAT_EQ(find("baseline")->y, 16.0f);
        EXPECT_FLOAT_EQ(find("complex")->height, 60.0f);
        EXPECT_FLOAT_EQ(find("line-a")->height, 26.0f);
        EXPECT_FLOAT_EQ(find("line-b")->height, 26.0f);
        EXPECT_FLOAT_EQ(find("line-b")->y - find("line-a")->y, 26.0f);
        DomElement* quoted = find("quote-inner");
        ASSERT_NE(quoted->pseudo, nullptr);
        ASSERT_NE(quoted->pseudo->before, nullptr);
        DomNode* quote = quoted->pseudo->before->first_child;
        ASSERT_NE(quote, nullptr);
        ASSERT_TRUE(quote->is_text());
        EXPECT_STREQ(quote->as_text()->text, "1");
        DomElement* list = find("implicit-counter");
        ASSERT_NE(list->layout_cache, nullptr);
        EXPECT_NE(list->layout_cache->measured_flex_flow, nullptr);
    }
    DomElement* item = find("wrapped");
    ASSERT_TRUE(item->set_attribute("style", "flex-basis:100px"));
    layout_html_doc(&ui, document, true);
    EXPECT_FLOAT_EQ(item->height, 40.0f);
    ASSERT_TRUE(item->set_attribute("style", "flex-basis:100px;font-size:10px"));
    layout_html_doc(&ui, document, true);
    EXPECT_FLOAT_EQ(item->height, 10.0f);

    FlexFlowTestContext flow(&ui, document, item);
    ASSERT_NE(flow.arena, nullptr);
    LayoutContext& context = flow.layout;
    FlexContainerLayout& parent = flow.parent;
    auto* block = lam::view_require_block(item);
    radiant::flex_measured_flow_invalidate(item);
    float height = 0.0f;
    EXPECT_TRUE(flex_measure_completed_flow(&context, static_cast<ViewElement*>(block), &parent, 100.0f, &height));
    EXPECT_FLOAT_EQ(height, 10.0f);
    EXPECT_EQ(context.profiler.flex_flow_measures, 1u);
    TextRect* rectangles = item->first_child->as_text()->rect;
    float baseline = item->block()->first_line_baseline;
    item->height = 100.0f;
    layout_flex_item_content(&context, block);
    EXPECT_EQ(context.profiler.flex_flow_reuses, 1u);
    EXPECT_EQ(item->first_child->as_text()->rect, rectangles);
    EXPECT_FLOAT_EQ(item->height, 100.0f);
    EXPECT_FLOAT_EQ(item->block()->first_line_baseline, baseline);
    item->width = 40.0f;
    layout_flex_item_content(&context, block);
    EXPECT_FLOAT_EQ(item->content_height, 30.0f);
    item->width = 100.0f;
    layout_flex_item_content(&context, block);
    EXPECT_EQ(context.profiler.flex_flow_reuses, 2u);
    EXPECT_FLOAT_EQ(item->content_height, 10.0f);
    EXPECT_EQ(item->first_child->as_text()->rect->next, nullptr);
    {
        radiant::LayoutMeasureScope trial(&context, item);
        EXPECT_FALSE(flex_measure_completed_flow(&context, static_cast<ViewElement*>(block), &parent, 100.0f, &height));
    }
    EXPECT_TRUE(flex_measure_completed_flow(&context, static_cast<ViewElement*>(block), &parent, 100.0f, &height));
    EXPECT_EQ(context.profiler.flex_flow_measures, 1u);
    item->width = 40.0f;
    EXPECT_TRUE(flex_measure_completed_flow(&context, static_cast<ViewElement*>(block), &parent, 40.0f, &height));
    EXPECT_FLOAT_EQ(height, 30.0f);
    EXPECT_EQ(context.profiler.flex_flow_measures, 1u);
    context.root_font_size += 1.0f;
    EXPECT_TRUE(flex_measure_completed_flow(&context, static_cast<ViewElement*>(block), &parent, 40.0f, &height));
    EXPECT_EQ(context.profiler.flex_flow_measures, 2u);
    context.available_space = AvailableSpace::make_definite(100.0f, 200.0f);
    EXPECT_TRUE(flex_measure_completed_flow(&context, static_cast<ViewElement*>(block), &parent, 40.0f, &height));
    EXPECT_EQ(context.profiler.flex_flow_measures, 3u);
    context.node_limit = context.node_count;
    EXPECT_FALSE(flex_measure_completed_flow(&context, static_cast<ViewElement*>(block), &parent, 40.0f, &height));
    context.node_limit = 1000;
    EXPECT_TRUE(flex_measure_completed_flow(&context, lam::view_as_element(static_cast<DomNode*>(find("complex"))), &parent, 60.0f, &height));

}

TEST_F(RadiantTextQuery, OwnedFlowRestoresEmptyAndUnplacedTextFragments) {
    ASSERT_NE(load("test/html/layout_flex_completed_text.html"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* item = dom_find_element_by_id(document->root->as_element(), "wrapped");
    ASSERT_NE(item, nullptr);
    DomText* empty = DomText::create_detached_copy(document, "", 0);
    ASSERT_NE(empty, nullptr);
    ASSERT_TRUE(item->DomNode::append_child(empty));
    ASSERT_EQ(empty->rect, nullptr);
    FlexFlowTestContext flow(&ui, document, item);
    auto* result = radiant::layout_flow_result_capture(&flow.layout, item);
    ASSERT_NE(result, nullptr);
    for (unsigned pass = 0; pass < 3; pass++) {
        empty->rect = lam::view_prop(document->view_tree->alloc_text_rect());
        ASSERT_NE(empty->rect, nullptr);
        empty->rect->width = 123.0f;
        ASSERT_TRUE(radiant::layout_flow_result_apply(&flow.layout, result));
        EXPECT_EQ(empty->rect, nullptr);
        EXPECT_EQ(empty->width, 0.0f);
    }
    ASSERT_TRUE(dom_text_set_content(item->first_child->as_text(), "X"));
    EXPECT_FALSE(radiant::layout_flow_result_apply(&flow.layout, result));
    radiant::layout_flow_result_release(document->view_tree->prop_pool, result);
    PoolStats before = {}, after = {};
    pool_get_detailed_stats(document->view_tree->prop_pool, &before);
    result = radiant::layout_flow_result_capture(&flow.layout, item);
    ASSERT_NE(result, nullptr);
    radiant::layout_flow_result_release(document->view_tree->prop_pool, result);
    pool_get_detailed_stats(document->view_tree->prop_pool, &after);
    EXPECT_EQ(after.live_bytes, before.live_bytes);
}

static uint64_t retained_subtree_geometry(DomElement* root) {
    struct Digest { DomNode* root; uint64_t hash; } digest = {root, HASH_FNV1A_64_OFFSET_BASIS};
    view_geometry_walk_dom_tree(root, [](DomNode* node, void* data) {
        auto* digest = (Digest*)data;
        if (node != digest->root) {
            float box[] = {node->x, node->y, node->width, node->height};
            digest->hash = hash_fnv1a_64_extend(digest->hash, box, sizeof(box));
        }
        if (node->is_text()) {
            DomText* text = node->as_text();
            digest->hash = hash_fnv1a_64_extend(digest->hash, text->text, text->length);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                float box[] = {rect->x, rect->y, rect->width, rect->height};
                int source[] = {rect->start_index, rect->length, rect->line_number};
                digest->hash = hash_fnv1a_64_extend(digest->hash, box, sizeof(box));
                digest->hash = hash_fnv1a_64_extend(digest->hash, source, sizeof(source));
            }
        } else if (node->is_element()) {
            DomElement* element = node->as_element();
            float content[] = {element->content_width, element->content_height,
                element->block()->first_line_baseline, element->block()->last_line_baseline};
            digest->hash = hash_fnv1a_64_extend(digest->hash, content, sizeof(content));
            if (element->ext) {
                digest->hash = hash_fnv1a_64_extend(digest->hash, element->ext->frags, sizeof(element->ext->frags));
                digest->hash = hash_fnv1a_64_extend_u64le(digest->hash, element->ext->fragment_presence_mask);
            }
            if (element->embed && element->embedp()->grid && element->embedp()->grid->used_track_text)
                digest->hash = hash_fnv1a_64_extend_cstr(digest->hash, element->embedp()->grid->used_track_text);
            if (const TableCellProp* cell = element->cell_prop()) {
                const CollapsedBorder* borders[] = {cell->top_resolved, cell->right_resolved,
                    cell->bottom_resolved, cell->left_resolved};
                for (const CollapsedBorder* border : borders) if (border) {
                    digest->hash = hash_fnv1a_64_extend(digest->hash, &border->width, sizeof(border->width));
                    digest->hash = hash_fnv1a_64_extend_u64le(digest->hash, border->color.c);
                }
            }
        }
        return true;
    }, &digest);
    return digest.hash;
}

TEST_F(RadiantTextQuery, AncestorReplayExpiresDescendantLiveAssociation) {
    ASSERT_NE(load("test/html/layout_flex_completed_text.html"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* item = dom_find_element_by_id(document->root->as_element(), "wrapped");
    ASSERT_NE(item, nullptr);
    FlexFlowTestContext flow(&ui, document, item);
    float height = 0.0f;
    ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, lam::view_require_block(item),
        &flow.parent, 60.0f, &height));
    auto* ancestor = radiant::layout_flow_result_capture(&flow.layout, item->parent);
    ASSERT_NE(ancestor, nullptr);
    item->width = 100.0f;
    ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, lam::view_require_block(item),
        &flow.parent, 100.0f, &height));
    uint64_t wide = retained_subtree_geometry(item);
    uint64_t computations = flow.layout.profiler.flex_flow_measures;
    ASSERT_TRUE(radiant::layout_flow_result_apply(&flow.layout, ancestor));
    EXPECT_NE(retained_subtree_geometry(item), wide);
    item->width = 100.0f;
    ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, lam::view_require_block(item),
        &flow.parent, 100.0f, &height));
    EXPECT_EQ(flow.layout.profiler.flex_flow_measures, computations);
    EXPECT_EQ(retained_subtree_geometry(item), wide);
    radiant::layout_flow_result_release(document->view_tree->prop_pool, ancestor);
}

TEST_F(RadiantTextQuery, CommonFlexSubtreesRestoreFragmentsWithoutRepeatingOrderedEffects) {
    ASSERT_NE(load("test/html/layout_flex_retained_effects.html"), nullptr);
    process_document_font_faces(&ui, document);
    ui.viewport_width = 320.0f; ui.viewport_height = 240.0f;
    auto find = [&](const char* id) { return dom_find_element_by_id(document->root->as_element(), id); };
    for (unsigned pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        for (const char* id : {"list", "generated", "resource", "positioned", "table", "grid", "percent-inline"}) {
            SCOPED_TRACE(id);
            DomElement* item = find(id);
            ASSERT_NE(item, nullptr);
            FlexFlowTestContext flow(&ui, document, item);
            flow.layout.generated_effects_prepared = true;
            float height = 0.0f;
            ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, lam::view_require_block(item),
                &flow.parent, 60.0f, &height));
            EXPECT_GT(height, 0.0f);
            uint64_t original = retained_subtree_geometry(item);
            uint64_t computations = flow.layout.profiler.flex_flow_measures;
            item->width = 30.0f;
            ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, lam::view_require_block(item),
                &flow.parent, 30.0f, &height));
            uint64_t narrow = retained_subtree_geometry(item);
            EXPECT_NE(narrow, original);
            EXPECT_EQ(flow.layout.profiler.flex_flow_measures, computations + 1);
            item->width = 60.0f;
            item->height = 111.0f;
            ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, lam::view_require_block(item),
                &flow.parent, 60.0f, &height));
            EXPECT_EQ(flow.layout.profiler.flex_flow_measures, computations + 1);
            EXPECT_EQ(retained_subtree_geometry(item), original);
            EXPECT_FLOAT_EQ(item->height, 111.0f);
            // probe restoration may destroy live rectangles; the independently owned result remains usable.
            { radiant::LayoutMeasureScope trial(&flow.layout, item); }
            layout_flex_item_content(&flow.layout, lam::view_require_block(item));
            EXPECT_EQ(retained_subtree_geometry(item), original);
            // repeated ancestor probes must replay both owned entries without rerunning flow or effects.
            for (unsigned replay = 0; replay < 16; ++replay) {
                item->width = replay % 2 ? 60.0f : 30.0f;
                ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, lam::view_require_block(item),
                    &flow.parent, item->width, &height));
                EXPECT_EQ(retained_subtree_geometry(item), replay % 2 ? original : narrow);
                EXPECT_EQ(flow.layout.profiler.flex_flow_measures, computations + 1);
            }
        }
        auto generated = [&](const char* id, bool before) -> const char* {
            DomElement* element = find(id);
            DomElement* pseudo = before ? element->pseudo->before.get() : element->pseudo->after.get();
            return pseudo->first_child->as_text()->text;
        };
        EXPECT_STREQ(generated("chapter-a", true), "1 ");
        EXPECT_STREQ(generated("chapter-b", true), "2 ");
        EXPECT_STREQ(generated("quote-a", true), "A");
        EXPECT_STREQ(generated("quote-b", true), "B");
        EXPECT_STREQ(generated("quote-b", false), "Y");
        EXPECT_STREQ(generated("quote-a", false), "Z");
    }
}

TEST_F(RadiantTextQuery, NestedFlexNaturalFlowPrecedesStretchAndValidatesBlockConstraint) {
    ASSERT_NE(load("test/html/layout_flex_natural_nested.html"), nullptr);
    process_document_font_faces(&ui, document);
    ui.viewport_width = 320.0f;
    ui.viewport_height = 240.0f;
    auto find = [&](const char* id) { return dom_find_element_by_id(document->root->as_element(), id); };
    for (unsigned pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        for (const char* id : {"row", "column", "start-row", "start-column"})
            EXPECT_FLOAT_EQ(find(id)->height, 40.0f) << id;
        for (const char* id : {"page", "main", "content"})
            EXPECT_FLOAT_EQ(find(id)->height, 60.0f) << id;
        EXPECT_FLOAT_EQ(find("article")->height, 40.0f);
        EXPECT_FLOAT_EQ(find("footer")->y - find("article")->y, 40.0f);
        for (const char* id : {"fixed-row", "fixed-column", "min-row", "min-column"})
            EXPECT_FLOAT_EQ(find(id)->height, 100.0f) << id;
        EXPECT_FLOAT_EQ(find("fill-first")->height, 50.0f);
        EXPECT_FLOAT_EQ(find("fill-last")->y - find("fill-first")->y, 50.0f);
        ASSERT_NE(find("column")->layout_cache, nullptr);
        EXPECT_NE(find("column")->layout_cache->measured_flex_flow, nullptr);
        for (const char* id : {"percent-gap", "percent-basis", "stretch-basis"}) {
            ASSERT_NE(find(id)->layout_cache, nullptr);
            EXPECT_EQ(find(id)->layout_cache->measured_flex_flow, nullptr) << id;
        }
    }
    DomElement* item = find("fixed-column");
    FlexFlowTestContext flow(&ui, document, item);
    ASSERT_NE(flow.arena, nullptr);
    ViewBlock* block = lam::view_require_block(item);
    float height = 0.0f;
    radiant::flex_measured_flow_invalidate(item);
    ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, block, &flow.parent, 200.0f, &height));
    EXPECT_FLOAT_EQ(height, 40.0f);
    EXPECT_EQ(flow.layout.profiler.flex_flow_measures, 1u);
    DomText* text = find("fill-first")->first_child->as_element()->first_child->as_text();
    TextRect* fragments = text->rect;
    item->height = height;
    layout_flex_item_content(&flow.layout, block);
    EXPECT_EQ(flow.layout.profiler.flex_flow_reuses, 1u);
    EXPECT_EQ(text->rect, fragments);
    EXPECT_FLOAT_EQ(find("fill-first")->height, 20.0f);
    item->height = 100.0f;
    layout_flex_item_content(&flow.layout, block);
    EXPECT_EQ(flow.layout.profiler.flex_flow_reuses, 1u);
    EXPECT_FLOAT_EQ(find("fill-first")->height, 50.0f);
    item->height = 40.0f;
    layout_flex_item_content(&flow.layout, block);
    EXPECT_EQ(flow.layout.profiler.flex_flow_reuses, 2u);
    EXPECT_FLOAT_EQ(find("fill-first")->height, 20.0f);
    ASSERT_TRUE(find("row")->set_attribute("style", "width:20px"));
    ASSERT_TRUE(dom_text_set_content(find("text")->first_child->as_text(), "XX XX XX"));
    layout_html_doc(&ui, document, true);
    EXPECT_FLOAT_EQ(find("row")->height, 80.0f);
    EXPECT_FLOAT_EQ(find("column")->height, 80.0f);
}

TEST_F(RadiantTextQuery, ColumnContributionPreservesPublishedChildGeometry) {
    DomElement* result = load("test/html/layout_shared_publication.html", true);
    ASSERT_NE(result, nullptr);
    DomElement* column = dom_find_element_by_id(result, "probe-toc");
    DomElement* title = dom_find_element_by_id(result, "probe-title");
    ASSERT_NE(column, nullptr);
    ASSERT_NE(title, nullptr);
    float width = title->width, height = title->height;
    DomText* text = title->first_child->as_element()->first_child->as_text();
    ASSERT_NE(text->rect, nullptr);
    TextRect* fragments = text->rect;
    FlexFlowTestContext flow(&ui, document, column);
    ASSERT_NE(flow.arena, nullptr);
    flex_column_content_contribution(&flow.layout, lam::view_as_element(column));
    EXPECT_FLOAT_EQ(title->width, width);
    EXPECT_FLOAT_EQ(title->height, height);
    EXPECT_EQ(text->rect, fragments);
}

TEST_F(RadiantTextQuery, CompletedFlexContentEnlargesProvisionalAncestorHeight) {
    ASSERT_NE(load("test/html/layout_flex_parent_constraints.html"), nullptr);
    process_document_font_faces(&ui, document);
    ui.viewport_width = 320.0f;
    ui.viewport_height = 240.0f;
    auto find = [&](const char* id) { return dom_find_element_by_id(document->root->as_element(), id); };
    for (unsigned pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        for (const char* id : {"auto-page", "auto-main", "auto-content", "start-page", "start-main", "start-content"})
            EXPECT_FLOAT_EQ(find(id)->height, 60.0f) << id;
        for (const char* id : {"tall-page", "tall-main", "tall-content", "fixed-page", "fixed-main", "fixed-content"})
            EXPECT_FLOAT_EQ(find(id)->height, 100.0f) << id;
        EXPECT_FLOAT_EQ(find("auto-article")->height, 40.0f);
        EXPECT_FLOAT_EQ(find("auto-sidebar")->height, 60.0f);
        EXPECT_FLOAT_EQ(find("auto-dependent")->height, 60.0f);
        EXPECT_FLOAT_EQ(find("auto-footer")->y - find("auto-article")->y, 40.0f);
        EXPECT_FLOAT_EQ(find("tall-footer")->y - find("tall-article")->y, 80.0f);
        EXPECT_FLOAT_EQ(find("fixed-footer")->y - find("fixed-article")->y, 80.0f);
        EXPECT_FLOAT_EQ(find("resource-page")->height, 30.0f);
        EXPECT_FLOAT_EQ(find("resource-content")->height, 30.0f);
        EXPECT_FLOAT_EQ(find("resource-svg")->height, 10.0f);
    }
    ASSERT_TRUE(find("auto-page")->set_attribute("style", "width:20px"));
    ASSERT_TRUE(dom_text_set_content(find("auto-text")->first_child->as_text(), "XX XX XX"));
    layout_html_doc(&ui, document, true);
    for (const char* id : {"auto-page", "auto-main", "auto-content"})
        EXPECT_FLOAT_EQ(find(id)->height, 100.0f) << id;
    EXPECT_FLOAT_EQ(find("auto-article")->height, 80.0f);
    EXPECT_FLOAT_EQ(find("auto-sidebar")->height, 100.0f);
    EXPECT_FLOAT_EQ(find("auto-dependent")->height, 100.0f);
    EXPECT_FLOAT_EQ(find("auto-footer")->y - find("auto-article")->y, 80.0f);
}

TEST_F(RadiantTextQuery, ScalarSizeCacheExpiresWithoutDiscardingComputedStyle) {
    DomElement* result = load("test/html/layout_shared_publication.html", true);
    ASSERT_NE(result, nullptr);
    FlexFlowTestContext flow(&ui, document, result);
    ASSERT_NE(flow.arena, nullptr);
    radiant::LayoutRunModeScope measuring(&flow.layout, radiant::RunMode::ComputeSize);
    auto dimensions = radiant::known_dimensions_none();
    radiant::SizeF size = {};
    radiant::layout_pass_cache_store(&flow.layout, result, dimensions, {90.0f, 70.0f}, "publication");
    ASSERT_TRUE(radiant::layout_pass_cache_get(&flow.layout, result, dimensions, &size, "publication"));
    EXPECT_FLOAT_EQ(size.height, 70.0f);
    auto* computed = result->layout_cache->resolved_style.get();
    uint32_t storage_generation = document->view_tree->layout_generation;
    advance_measurement_cache_generation(document->view_tree);
    EXPECT_FALSE(radiant::layout_pass_cache_get(&flow.layout, result, dimensions, &size, "publication"));
    EXPECT_EQ(document->view_tree->layout_generation, storage_generation);
    EXPECT_EQ(result->layout_cache->resolved_style, computed);
}

TEST_F(RadiantTextQuery, CompletedFlexSubtreeFlowSharesBlocksAndInvalidatesDescendants) {
    ASSERT_NE(load("test/html/layout_flex_completed_subtrees.html"), nullptr);
    process_document_font_faces(&ui, document);
    ui.viewport_width = 320;
    ui.viewport_height = 240;
    auto find = [&](const char* id) { return dom_find_element_by_id(document->root->as_element(), id); };
    for (unsigned pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        for (const char* id : {"inline", "blocks", "atomic"}) {
            ASSERT_NE(find(id)->layout_cache, nullptr);
            EXPECT_NE(find(id)->layout_cache->measured_flex_flow, nullptr) << id;
        }
        EXPECT_FLOAT_EQ(find("inline")->height, 60.0f);
        EXPECT_FLOAT_EQ(find("blocks")->height, 160.0f);
        EXPECT_FLOAT_EQ(find("first")->height, 60.0f);
        EXPECT_FLOAT_EQ(find("last")->height, 20.0f);
        EXPECT_FLOAT_EQ(find("last")->y - find("first")->y, 71.0f);
        EXPECT_FLOAT_EQ(find("percent")->height, 100.0f);
        EXPECT_FLOAT_EQ(find("percent")->first_child->height, 50.0f);
        for (const char* id : {"generated", "resource", "positioned", "counter", "list-item"}) {
            ASSERT_NE(find(id)->layout_cache, nullptr);
            EXPECT_NE(find(id)->layout_cache->measured_flex_flow, nullptr) << id;
        }
        for (const char* id : {"percent", "intrinsic"}) {
            ASSERT_NE(find(id)->layout_cache, nullptr);
            EXPECT_EQ(find(id)->layout_cache->measured_flex_flow, nullptr) << id;
        }
    }
    ASSERT_TRUE(find("first")->set_attribute("style", "margin-bottom:7px;font-size:10px"));
    layout_html_doc(&ui, document, true);
    EXPECT_FLOAT_EQ(find("first")->height, 20.0f);
    EXPECT_FLOAT_EQ(find("last")->y - find("first")->y, 31.0f);
    DomText* text = find("nested")->first_child->as_text();
    ASSERT_TRUE(dom_text_set_content(text, "XX"));
    EXPECT_EQ(find("nested")->first_child, text);
    EXPECT_EQ(find("nested")->last_child, text);
    ASSERT_EQ(text->next_sibling, nullptr);
    ASSERT_EQ(text->prev_sibling, nullptr);
    layout_html_doc(&ui, document, true);
    EXPECT_FLOAT_EQ(find("first")->height, 10.0f);
    EXPECT_FLOAT_EQ(find("last")->y - find("first")->y, 21.0f);

    DomElement* item = find("blocks");
    FlexFlowTestContext flow(&ui, document, item);
    ASSERT_NE(flow.arena, nullptr);
    LayoutContext& context = flow.layout;
    ViewBlock* block = lam::view_require_block(item);
    float width = layout_content_size_from_border_box(block, block->width, true);
    float height = 0.0f;
    radiant::flex_measured_flow_invalidate(item);
    ASSERT_TRUE(flex_measure_completed_flow(&context, block, &flow.parent, width, &height));
    EXPECT_FLOAT_EQ(height, 51.0f);
    EXPECT_EQ(context.profiler.flex_flow_measures, 1u);
    TextRect* rectangles = text->rect;
    float last_y = find("last")->y;
    float baseline = block->block()->first_line_baseline;
    block->height = 160.0f;
    layout_flex_item_content(&context, block);
    EXPECT_EQ(context.profiler.flex_flow_reuses, 1u);
    EXPECT_EQ(text->rect, rectangles);
    EXPECT_FLOAT_EQ(find("last")->y, last_y);
    EXPECT_FLOAT_EQ(block->height, 160.0f);
    EXPECT_FLOAT_EQ(block->block()->first_line_baseline, baseline);

    // a descendant text edit changes the subtree input even without another document layout.
    uint32_t previous_revision = item->intrinsic_input_revision;
    ASSERT_TRUE(dom_text_set_content(text, "XX XX XX"));
    EXPECT_NE(item->intrinsic_input_revision, previous_revision);
    ASSERT_TRUE(flex_measure_completed_flow(&context, block, &flow.parent, width, &height));
    EXPECT_FLOAT_EQ(height, 61.0f);
    EXPECT_EQ(context.profiler.flex_flow_measures, 2u);
    layout_flex_item_content(&context, block);
    EXPECT_EQ(context.profiler.flex_flow_reuses, 2u);
    EXPECT_FLOAT_EQ(find("first")->height, 20.0f);
    {
        radiant::LayoutMeasureScope trial(&context, find("nested"));
    }
    layout_flex_item_content(&context, block);
    EXPECT_EQ(context.profiler.flex_flow_reuses, 3u);
    ASSERT_TRUE(flex_measure_completed_flow(&context, block, &flow.parent, width, &height));
    {
        radiant::LayoutMeasureScope trial(&context, document->root);
    }
    layout_flex_item_content(&context, block);
    EXPECT_EQ(context.profiler.flex_flow_reuses, 5u);
    ASSERT_TRUE(flex_measure_completed_flow(&context, block, &flow.parent, width, &height));
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, find("last"), width), 20.0f);
    layout_flex_item_content(&context, block);
    EXPECT_EQ(context.profiler.flex_flow_reuses, 7u);
    // atomic-inline break fidelity has a separate known defect; retain its actual fragments unchanged.
    DomElement* atomic = find("atomic");
    ViewBlock* atomic_block = lam::view_require_block(atomic);
    float atomic_width = layout_content_size_from_border_box(atomic_block, atomic_block->width, true);
    radiant::flex_measured_flow_invalidate(atomic);
    ASSERT_TRUE(flex_measure_completed_flow(&context, atomic_block, &flow.parent, atomic_width, &height));
    DomElement* atomic_child = atomic->first_child->as_element();
    TextRect* atomic_rectangles = atomic_child->first_child->as_text()->rect;
    float atomic_height = atomic->height, child_x = atomic_child->x, child_y = atomic_child->y;
    uint64_t reuses = context.profiler.flex_flow_reuses;
    layout_flex_item_content(&context, atomic_block);
    EXPECT_EQ(context.profiler.flex_flow_reuses, reuses + 1);
    EXPECT_EQ(atomic_child->first_child->as_text()->rect, atomic_rectangles);
    EXPECT_FLOAT_EQ(atomic->height, atomic_height);
    EXPECT_FLOAT_EQ(atomic_child->x, child_x);
    EXPECT_FLOAT_EQ(atomic_child->y, child_y);
}

TEST_F(RadiantTextQuery, LegacyReplacedAlignmentUsesTheParentBaselineAndAuthorCascade) {
    ASSERT_NE(load("test/html/layout_legacy_replaced_alignment.html"), nullptr);
    process_document_font_faces(&ui, document);
    struct Sample {
        const char* id; float height, text_y, image_y; const char* value;
        const char* floating = "none";
    } samples[] = {
        {"none", 47, 32, 0, "baseline"},
        {"center", 40, 12, 0, "-webkit-baseline-middle"},
        {"middle", 40, 12, 0, "-webkit-baseline-middle"},
        {"mixed-case", 40, 12, 0, "-webkit-baseline-middle"},
        {"top", 40, 5, 0, "top"}, {"texttop", 45, 5, 5, "text-top"},
        {"absmiddle", 40, 16, 0, "middle"}, {"abscenter", 40, 16, 0, "middle"},
        {"css-middle", 40, 16, 0, "middle"},
        {"css-legacy", 40, 12, 0, "-webkit-baseline-middle"},
        {"author-rule", 47, 32, 0, "baseline"}, {"author-inline", 47, 32, 0, "baseline"},
        {"author-initial", 47, 32, 0, "baseline"}, {"author-unset", 47, 32, 0, "baseline"},
        {"author-inherit", 40, 5, 0, "top"}, {"unknown", 47, 32, 0, "baseline"},
        {"author-revert", 47, 32, 0, "baseline"},
        {"author-revert-layer", 40, 12, 0, "-webkit-baseline-middle"},
        {"rule-revert-layer", 40, 12, 0, "-webkit-baseline-middle"},
        {"author-layer", 47, 32, 0, "baseline"},
        {"layer-revert", 40, 12, 0, "-webkit-baseline-middle"},
        {"untrimmed", 47, 32, 0, "baseline"},
        {"small-center", 20, 5, 7, "-webkit-baseline-middle"},
        {"small-middle", 20, 5, 3, "middle"}, {"parent-font", 40, 16, 0, "middle"},
        {"legacy-font", 40, 12, 0, "-webkit-baseline-middle"},
        {"input-image", 40, 12, 0, "-webkit-baseline-middle"},
        {"iframe", 40, 12, 0, "-webkit-baseline-middle"},
        {"object", 40, 12, 0, "-webkit-baseline-middle"},
        {"embed", 40, 12, 0, "-webkit-baseline-middle"},
        {"ordinary-span", 47, 32, 0, "baseline"},
        {"left", 20, 5, 0, "top", "left"}, {"right", 20, 5, 0, "top", "right"},
    };
    auto expect_keyword = [&](DomElement* element, CssPropertyCode property, const char* value) {
        String* computed = css_prop_serialize_computed_value(document->document_pool, element, property, 0);
        ASSERT_NE(computed, nullptr);
        EXPECT_STREQ(computed->chars, value);
    };
    auto expect_sample = [&](const Sample& sample) {
        SCOPED_TRACE(sample.id);
        DomElement* box = dom_find_element_by_id(document->root->as_element(), sample.id);
        ASSERT_NE(box, nullptr);
        DomText* text = box->first_child ? box->first_child->as_text() : nullptr;
        DomElement* image = box->last_child ? box->last_child->as_element() : nullptr;
        ASSERT_NE(text, nullptr); ASSERT_NE(image, nullptr);
        float parent_y = 0, text_y = 0, image_y = 0;
        view_get_visual_bounds(box, nullptr, &parent_y, nullptr, nullptr);
        view_get_visual_bounds(text, nullptr, &text_y, nullptr, nullptr);
        view_get_visual_bounds(image, nullptr, &image_y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(box->height, sample.height);
        // text and image offsets distinguish legacy baseline-middle from CSS middle.
        EXPECT_FLOAT_EQ(text_y - parent_y, sample.text_y);
        EXPECT_FLOAT_EQ(image_y - parent_y, sample.image_y);
        expect_keyword(image, CSS_PROPERTY_VERTICAL_ALIGN, sample.value);
        expect_keyword(image, CSS_PROPERTY_FLOAT, sample.floating);
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 600; ui.viewport_height = 2000;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) expect_sample(sample);
            DomElement* ordinary = dom_find_element_by_id(document->root->as_element(), "text-input");
            ASSERT_NE(ordinary, nullptr);
            expect_keyword(ordinary->last_child->as_element(), CSS_PROPERTY_VERTICAL_ALIGN, "baseline");
            DomElement* logo = dom_find_element_by_id(document->root->as_element(), "logo");
            ASSERT_NE(logo, nullptr);
            DomElement* image = logo->first_child ? logo->first_child->as_element() : nullptr;
            ASSERT_NE(image, nullptr);
            // the original large SVG must not add an unrelated text-strut descent to its line.
            EXPECT_FLOAT_EQ(logo->height, image->height);
            for (const char* id : {"wrapped-right", "wrapped-left"}) {
                SCOPED_TRACE(id);
                DomElement* wrapper = dom_find_element_by_id(document->root->as_element(), id);
                ASSERT_NE(wrapper, nullptr);
                DomElement* anchor = wrapper->first_child ? wrapper->first_child->as_element() : nullptr;
                ASSERT_NE(anchor, nullptr);
                // an out-of-flow image cannot create an anonymous block fragment in its inline anchor.
                EXPECT_FLOAT_EQ(anchor->width, 0.0f);
                EXPECT_FLOAT_EQ(anchor->height, 0.0f);
            }
            DomElement* center = dom_find_element_by_id(document->root->as_element(), "center");
            ASSERT_NE(center, nullptr);
            DomElement* aligned = center->last_child->as_element();
            ASSERT_NE(aligned, nullptr);
            ASSERT_TRUE(aligned->set_attribute("align", "absmiddle"));
            layout_html_doc(&ui, document, true);
            expect_sample({"center", 40, 16, 0, "middle"});
            ASSERT_TRUE(aligned->remove_attribute("align"));
            layout_html_doc(&ui, document, true);
            expect_sample({"center", 47, 32, 0, "baseline"});
            ASSERT_TRUE(aligned->set_attribute("align", "center"));
            layout_html_doc(&ui, document, true);
            expect_sample({"center", 40, 12, 0, "-webkit-baseline-middle"});
            DomElement* image_input = dom_find_element_by_id(document->root->as_element(), "input-image");
            ASSERT_NE(image_input, nullptr);
            DomElement* input = image_input->last_child->as_element();
            ASSERT_NE(input, nullptr);
            ASSERT_TRUE(input->set_attribute("type", "text"));
            layout_html_doc(&ui, document, true);
            expect_keyword(input, CSS_PROPERTY_VERTICAL_ALIGN, "baseline");
            ASSERT_TRUE(input->set_attribute("type", "image"));
            layout_html_doc(&ui, document, true);
            expect_sample({"input-image", 40, 12, 0, "-webkit-baseline-middle"});
            DomElement* left = dom_find_element_by_id(document->root->as_element(), "left");
            ASSERT_NE(left, nullptr);
            DomElement* floating = left->last_child->as_element();
            ASSERT_NE(floating, nullptr);
            ASSERT_TRUE(floating->set_attribute("align", "center"));
            layout_html_doc(&ui, document, true);
            expect_keyword(floating, CSS_PROPERTY_FLOAT, "none");
            expect_sample({"left", 40, 12, 0, "-webkit-baseline-middle"});
            ASSERT_TRUE(floating->set_attribute("align", "left"));
            layout_html_doc(&ui, document, true);
            expect_sample({"left", 20, 5, 0, "top", "left"});
        }
    }
}

TEST_F(RadiantTextQuery, NormalPunctuationBreaksShareInlineAndIntrinsicPolicy) {
    ASSERT_NE(load("test/html/layout_ascii_opening_breaks.html"), nullptr);
    process_document_font_faces(&ui, document);
    struct Sample { const char* id; float y, width, height; } samples[] = {
        {"less", 5, 224, 30}, {"round", 55, 224, 30}, {"square", 105, 224, 30},
        {"brace", 155, 224, 30}, {"nested", 205, 224, 30}, {"split", 255, 224, 30},
        {"bare", 305, 220, 30}, {"break-word", 355, 224, 30}, {"anywhere", 405, 224, 30},
        {"break-all", 455, 234, 30}, {"keep-all", 505, 224, 30}, {"nowrap", 575, 108, 10},
        {"letters", 625, 88, 10}, {"digit", 675, 108, 10}, {"opening", 725, 108, 10},
        {"currency", 775, 108, 10}, {"late", 825, 128, 10},
        {"minimum-bare", 850, 50, 40}, {"minimum-slice", 900, 54, 40},
        {"minimum-clone", 950, 58, 40}, {"minimum-nested", 1000, 74, 40},
        {"minimum-cell", 1050, 54, 40},
        // cloned fragments fit at 78px; Chromium's nested min-content query overestimates this by 4px.
        {"minimum-clone-nested", 1100, 78, 40},
        {"terminal-normal", 1150, 58, 20}, {"terminal-preserved", 1180, 68, 20},
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 600; ui.viewport_height = 1200;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) {
                // inline unions distinguish a legal fitting prefix from moving the whole word.
                expect_box(sample.id, 0, sample.y, sample.width, sample.height);
            }
            // the 78px intrinsic guard must also contain the actual cloned fragments.
            expect_box("clone-box", 0, 1105, 78, 30);
            expect_box("clone-first", 24, 1105, 50, 10);
            expect_box("clone-last", 4, 1125, 50, 10);
        }
    }
}

TEST_F(RadiantTextQuery, IntrinsicMinimaRetainInlineWordEdges) {
    ASSERT_NE(load("test/html/layout_inline_minimum_context.html"), nullptr);
    process_document_font_faces(&ui, document);
    struct Sample { const char* id; float width, height; } samples[] = {
        {"plain", 80, 20}, {"mixed", 100, 23}, {"decorated", 108, 23},
        {"nested", 108, 23}, {"split", 108, 23}, {"prefix", 70, 40},
        {"suffix", 50, 40}, {"both", 50, 40}, {"spaces", 60, 60},
        {"forced", 40, 40}, {"atomic", 70, 40}, {"nowrap", 80, 20},
        {"anywhere", 40, 40}, {"break-all", 40, 40},
        {"out-of-flow", 80, 20}, {"inline-minimum", 80, 20},
        {"empty-hyphen", 40, 40}, {"optional-break", 40, 40},
        {"margin-cell", 87, 20}, {"margin-minimum", 87, 20},
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 600; ui.viewport_height = 1000;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) {
                DomElement* box = dom_find_element_by_id(document->root->as_element(), sample.id);
                ASSERT_NE(box, nullptr);
                // cell and min-content box geometry reveal word-edge losses through nested inlines.
                EXPECT_FLOAT_EQ(box->width, sample.width) << sample.id << " scale " << scale;
                EXPECT_FLOAT_EQ(box->height, sample.height) << sample.id;
            }
        }
    }
}

TEST_F(RadiantTextQuery, ClosingPunctuationRetainsAdjacentInlineContext) {
    ASSERT_NE(load("test/html/layout_closing_punctuation_context.html"), nullptr);
    process_document_font_faces(&ui, document);
    struct Sample { const char* id; float x, y, width, height; size_t pieces; } samples[] = {
        {"split-code", 0, 5, 30, 30, 2}, {"nested-span", 0, 65, 30, 30, 2},
        {"decorated-code", 0, 125, 32, 30, 2}, {"same-code", 0, 185, 40, 30, 2},
        {"atomic-code", 10, 245, 60, 10, 1}, {"ex-code", 0, 305, 30, 30, 2},
        {"is-code", 0, 365, 30, 30, 2}, {"sy-code", 0, 425, 30, 30, 2},
        {"same-ex-code", 0, 485, 40, 30, 2}, {"nowrap-code", 10, 545, 60, 10, 1},
        {"preserved-code", 0, 585, 40, 30, 2}, {"anywhere-code", 0, 645, 70, 30, 2},
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 600; ui.viewport_height = 800;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) {
                expect_box(sample.id, sample.x, sample.y, sample.width, sample.height);
                DomElement* box = dom_find_element_by_id(document->root->as_element(), sample.id);
                ASSERT_NE(box, nullptr);
                DomNode* child = box->first_child;
                while (child && child->is_element()) child = child->as_element()->first_child;
                DomText* text = child ? child->as_text() : nullptr;
                ASSERT_NE(text, nullptr);
                // text rectangles expose a stranded suffix even when its parent keeps the same height.
                size_t pieces = 0;
                for (TextRect* rect = text->rect; rect; rect = rect->next) {
                    pieces++;
                    if (rect->next) EXPECT_FLOAT_EQ(rect->next->y - rect->y, 20.0f);
                }
                EXPECT_EQ(pieces, sample.pieces) << sample.id;
            }
        }
    }
}

TEST_F(RadiantTextQuery, BaseReplacementPreservesTheLoadersUrlCarrier) {
    Url* base = get_current_dir();
    ASSERT_NE(base, nullptr);
    Url* source = url_parse_path_or_url("test/html/layout_base_url_lifetime.html", base);
    url_destroy(base);
    ASSERT_NE(source, nullptr);
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    DocumentJsHostConfig host = {}; host.ui_context = &ui;
    host.resource_policy = INPUT_RESOURCE_LOCAL_ONLY;
    document = load_lambda_html_doc(source, nullptr, 320, 200, pool,
        nullptr, false, false, &host);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(dom_document_finalize_loader_pool(document, pool));
    Url* loaded_url = document->url;
    // the CLI retains this carrier until its separate cleanup after DOM retirement.
    ASSERT_EQ(loaded_url, source);
    ASSERT_EQ(document->input->url, source);
    EXPECT_STREQ(url_get_href(source), "file:///");
    ui.document = lam::up(document);
    layout_html_doc(&ui, document, false);
    expect_box("result", 0, 0, 40, 20);
}

TEST_F(RadiantTextQuery, ResponsiveImageMinimaRetainBoxAndAuthoredFloors) {
    ASSERT_NE(load("test/html/layout_responsive_image_minimum.html"), nullptr);
    struct Sample { const char* id; const char* image; float width, image_width, height; } samples[] = {
        {"direct", "direct-image", 40, 40, 12.5f},
        {"anchor", "anchor-image", 40, 40, 12.5f},
        {"span", "span-image", 40, 40, 12.5f},
        {"unconstrained", "unconstrained-image", 64, 64, 20},
        {"half", "half-image", 40, 20, 6.25f},
        {"math", "math-image", 40, 40, 12.5f},
        {"minimum", "minimum-image", 64, 64, 20},
        {"decorated", "decorated-image", 40, 52, 24.5f},
        {"css-width", "css-width-image", 40, 40, 12.5f},
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 600; ui.viewport_height = 800;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) {
                DomElement* box = dom_find_element_by_id(document->root->as_element(), sample.id);
                ASSERT_NE(box, nullptr);
                EXPECT_FLOAT_EQ(box->width, sample.width) << sample.id << " scale " << scale;
                EXPECT_FLOAT_EQ(box->height, sample.height) << sample.id;
                DomElement* image = dom_find_element_by_id(document->root->as_element(), sample.image);
                ASSERT_NE(image, nullptr);
                EXPECT_FLOAT_EQ(image->width, sample.image_width) << sample.image;
                EXPECT_FLOAT_EQ(image->height, sample.height) << sample.image;
            }
        }
    }
}

TEST_F(RadiantTextQuery, InheritedDeclarationFilteringRetainsAncestorsResetsAndNewProperties) {
    DomElement* body = load("test/html/layout_inherited_declaration_filter.html");
    ASSERT_NE(body, nullptr);
    bool reflow = false;
    auto check = [&](CssEnum white_space, float letter_spacing) {
        layout_html_doc(&ui, document, reflow);
        reflow = true;
        DomElement* inherited = dom_find_element_by_id(document->root->as_element(), "inherited");
        DomElement* initial = dom_find_element_by_id(document->root->as_element(), "initial");
        ASSERT_NE(inherited, nullptr); ASSERT_NE(initial, nullptr);
        ASSERT_NE(inherited->font, nullptr); ASSERT_NE(initial->font, nullptr);
        EXPECT_FLOAT_EQ(inherited->font->font_size, 17.0f);
        EXPECT_EQ(inherited->block()->white_space, white_space);
        EXPECT_FLOAT_EQ(inherited->font->letter_spacing, letter_spacing);
        EXPECT_EQ(inherited->block()->text_transform, CSS_VALUE_UPPERCASE);
        EXPECT_FLOAT_EQ(initial->font->font_size, 9.0f);
        EXPECT_FLOAT_EQ(initial->font->letter_spacing, 0.0f);
        // an absent stored keyword represents the initial normal white-space value.
        EXPECT_TRUE(initial->block()->white_space == CSS_VALUE_NORMAL ||
                    initial->block()->white_space == CSS_VALUE__UNDEF);
    };
    check(CSS_VALUE_PRE, 0.0f);
    bool changed = false;
    ASSERT_TRUE(dom_element_set_presentation_style(body, "white-space", "normal", &changed));
    ASSERT_TRUE(dom_element_set_presentation_style(body, "letter-spacing", "2px", &changed));
    check(CSS_VALUE_NORMAL, 2.0f);
    ASSERT_TRUE(dom_element_clear_presentation_style(body));
    check(CSS_VALUE_PRE, 0.0f);
}

TEST_F(RadiantTextQuery, IntrinsicBoxVariablesUseTheMeasuredOwnerAndResolvedSides) {
    ASSERT_NE(load("test/html/layout_intrinsic_box_variables.html"), nullptr);
    struct Sample { const char* id; float width, left; } samples[] = {
        {"row-zero", 80, 0}, {"row-length", 104, 12}, {"cell-override", 94, 7},
        {"ancestor-length", 98, 9}, {"shorthand-pair", 102, 11}, {"two-variables", 96, 8},
        {"fallback", 92, 6}, {"arithmetic", 108, 14}, {"invalid", 80, 0},
        {"cyclic", 80, 8}, {"cyclic-math", 100, 20}, {"longhand-winner", 94, 2},
        {"logical", 104, 12}, {"font-math", 100, 20}, {"block-guard", 104, 12},
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 600; ui.viewport_height = 800;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) {
                DomElement* box = dom_find_element_by_id(document->root->as_element(), sample.id);
                ASSERT_NE(box, nullptr);
                EXPECT_FLOAT_EQ(box->width, sample.width) << sample.id << " scale " << scale;
                ASSERT_NE(box->bound, nullptr);
                EXPECT_FLOAT_EQ(box->boundary()->padding.left, sample.left) << sample.id;
                ASSERT_NE(box->first_child, nullptr);
                EXPECT_FLOAT_EQ(box->first_child->x, sample.left) << sample.id;
            }
        }
    }
}

TEST_F(RadiantTextQuery, TableBackgroundsUseSourcePositioningAndCellCoverage) {
    ASSERT_NE(load("test/html/layout_table_background_layers.html"), nullptr);
    ui.viewport_width = 1200; ui.viewport_height = 800;
    layout_html_doc(&ui, document, false);
    DomElement* source = dom_find_element_by_id(document->root->as_element(), "ignored-source");
    ASSERT_NE(source, nullptr); ASSERT_NE(source->bound, nullptr);
    float padding = source->boundary()->padding.top;
    struct Sample {
        const char* id;
        float x, y;
        unsigned r, g, b, tolerance = 0;
    } samples[] = {
        {"empty-later-row", 60, 30, 0, 255, 0},
        {"occupied-later-row", 10, 50, 0, 255, 0},
        {"occupied-later-row", 60, 60, 0, 0, 255},
        {"originating-row", 10, 50, 255, 0, 0},
        {"originating-row", 60, 20, 255, 0, 0},
        {"cross-cell-ink", 10, 10, 0, 0, 0},
        {"cross-cell-ink", 45, 10, 0, 0, 255},
        {"row-gradient", 10, 10, 255, 0, 0},
        {"row-gradient", 10, 30, 0, 255, 0},
        {"row-gradient", 10, 50, 255, 0, 0},
        {"row-gradient", 10, 70, 0, 255, 0},
        {"column-span", 50, 10, 255, 0, 0},
        {"column-span", 70, 30, 0, 255, 0},
        {"column-span", 50, 50, 0, 0, 255},
        {"separated-row", 55, 30, 255, 255, 255},
        {"separated-row", 70, 30, 255, 0, 0},
        {"empty-cell-layers", 30, 30, 255, 255, 0},
        {"empty-cell-layers", 55, 30, 255, 255, 0},
        {"empty-cell-layers", 90, 40, 255, 0, 0},
        {"row-opacity", 10, 60, 128, 255, 128, 1},
        {"row-opacity", 60, 20, 255, 128, 128, 1},
        {"row-opacity", 60, 60, 0, 0, 255},
        {"row-group", 10, 50, 255, 0, 0},
        {"row-missing-cell", 60, 50, 255, 255, 255},
        {"row-missing-cell", 10, 50, 255, 0, 0},
        {"column-under-row", 10, 10, 0, 255, 0},
        {"column-under-row", 60, 10, 0, 0, 255},
        {"column-under-row", 50, 60, 255, 0, 0},
        {"column-repeat", 30, 10, 255, 0, 0},
        {"column-repeat", 50, 10, 0, 255, 0},
        {"column-group-span", 30, 10, 255, 0, 0},
        {"column-group-span", 50, 10, 0, 255, 0},
        {"alpha-layers", 10, 10, 80, 215, 167, 2},
        {"alpha-layers", 60, 10, 191, 159, 96, 2},
        {"ignored-track-insets", 10, 30, 0, 255, 0},
        {"ignored-track-insets", 10, 50, 255, 0, 0},
        {"css-table-cell", 10, 10, 255, 0, 0},
        {"css-table-cell", 60, 30, 255, 0, 0},
        {"nested-alpha", 10, 10, 255, 128, 128, 1},
        {"nested-alpha", 60, 30, 255, 128, 128, 1},
        {"row-image", 10, 50, 255, 0, 0},
        {"row-image", 30, 70, 0, 255, 0},
        {"zero-row-image", 10, 10, 255, 0, 0},
        {"zero-row-image", 30, 30, 0, 255, 0},
        {"zero-row-gradient", 10, 10, 255, 0, 0},
        {"zero-row-gradient", 10, 30, 0, 255, 0},
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 1200; ui.viewport_height = 800;
        ui.window_width = 1200 * scale; ui.window_height = 800 * scale;
        ui.create_surface(ui.window_width, ui.window_height);
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            render_html_doc(&ui, document->view_tree, nullptr);
            ASSERT_FALSE(ui.render_failed);
            for (const Sample& sample : samples) {
                SCOPED_TRACE(sample.id);
                DomElement* box = dom_find_element_by_id(document->root->as_element(), sample.id);
                ASSERT_NE(box, nullptr);
                float x = 0.0f, y = 0.0f;
                view_get_visual_bounds(box, &x, &y, nullptr, nullptr);
                test_radiant_expect_surface_pixel(ui.surface,
                    (unsigned)roundf((x + sample.x) * scale),
                    (unsigned)roundf((y + sample.y) * scale),
                    sample.r, sample.g, sample.b, sample.tolerance);
            }
            EXPECT_FLOAT_EQ(source->boundary()->padding.top, padding);
            for (const char* id : {"zero-image-source", "zero-gradient-source"}) {
                DomElement* row = dom_find_element_by_id(document->root->as_element(), id);
                ASSERT_NE(row, nullptr);
                EXPECT_FLOAT_EQ(row->height, 0.0f);
            }
        }
    }
}

TEST_F(RadiantTextQuery, AutoGridHeightUsesItsFinalRowsAfterWidthChanges) {
    ASSERT_NE(load("test/html/layout_grid_auto_content_extent.html"), nullptr);
    DomElement* grid = dom_find_element_by_id(document->root->as_element(), "grid");
    ASSERT_NE(grid, nullptr);
    const float widths[] = {500.0f, 320.0f, 640.0f};
    for (float width : widths) {
        ui.viewport_width = ui.window_width = width;
        layout_html_doc(&ui, document, true);
        EXPECT_FLOAT_EQ(grid->width, width);
        float bottom = 0.0f;
        for (DomNode* child = grid->first_child; child; child = child->next_sibling) {
            if (child->is_element()) bottom = max(bottom, child->y + child->height);
        }
        EXPECT_NEAR(grid->height, bottom + grid->boundary()->padding.bottom, 0.5f) << width;
    }
}

TEST_F(RadiantTextQuery, FilenameGlyphsKeepTheirInheritedPointerCursor) {
    ASSERT_NE(load("test/html/layout_grid_auto_content_extent.html"), nullptr);
    layout_html_doc(&ui, document, false);
    struct CursorCase { const char* id; CssEnum cursor; };
    const CursorCase cases[] = {{"label", CSS_VALUE_POINTER}, {"ordinary", CSS_VALUE_TEXT},
        {"crosshair", CSS_VALUE_CROSSHAIR}, {"explicit", CSS_VALUE_TEXT}};
    for (const CursorCase& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        DomNode* text = element->first_child;
        ASSERT_NE(text, nullptr);
        if (entry.cursor == CSS_VALUE_POINTER) {
            EXPECT_EQ(element->inl()->cursor, CSS_VALUE_POINTER) << "label before hover";
            EXPECT_EQ(element->parent->as_element()->inl()->cursor, CSS_VALUE_POINTER) << "button before hover";
        }
        EventContext context = {};
        context.ui_context = &ui;
        context.target_document = document;
        context.event.type = RDT_EVENT_MOUSE_MOVE;
        context.new_cursor = CSS_VALUE_AUTO;
        update_hover_state(&context, static_cast<View*>(text));
        ArrayList* stack = build_view_stack(&context, static_cast<View*>(text));
        fire_events(&context, stack);
        arraylist_free(stack);
        EXPECT_EQ(context.new_cursor, entry.cursor) << entry.id;
    }
}

TEST_F(RadiantTextQuery, ComputedGapLonghandsProjectTheirWinningShorthand) {
    ASSERT_NE(load("test/html/layout_grid_auto_content_extent.html"), nullptr);
    layout_html_doc(&ui, document, false);
    struct GapCase { const char* id; const char* row; const char* column; };
    const GapCase cases[] = {{"grid", "22px", "10px"}, {"gap-one", "8px", "8px"},
        {"gap-axis", "4px", "9px"}, {"gap-later", "6px", "7px"}};
    for (const GapCase& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        char value[64];
        ASSERT_TRUE(css_prop_serialize_computed(element, CSS_PROPERTY_ROW_GAP, 0, value, sizeof(value)));
        EXPECT_STREQ(value, entry.row) << entry.id;
        ASSERT_TRUE(css_prop_serialize_computed(element, CSS_PROPERTY_COLUMN_GAP, 0, value, sizeof(value)));
        EXPECT_STREQ(value, entry.column) << entry.id;
    }
}

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

TEST_F(RadiantTextQuery, AxisRollbackExcludesSameLayerShorthands) {
    ASSERT_NE(load("test/html/layout_overflow_component_rollback.html"), nullptr);
    layout_html_doc(&ui, document, false);
    DomElement* overflow = dom_find_element_by_id(document->root->as_element(), "overflow");
    DomElement* overscroll = dom_find_element_by_id(document->root->as_element(), "overscroll");
    ASSERT_NE(overflow, nullptr); ASSERT_NE(overscroll, nullptr);
    EXPECT_EQ(overflow->scroll()->overflow_x, CSS_VALUE_VISIBLE);
    EXPECT_EQ(overflow->scroll()->overflow_y, CSS_VALUE_CLIP);
    EXPECT_EQ(overscroll->scroll()->overscroll_x, CSS_VALUE_AUTO);
    EXPECT_EQ(overscroll->scroll()->overscroll_y, CSS_VALUE_NONE);
}

TEST_F(RadiantTextQuery, BrokenImageAlternativesKeepTextBaselinesInsideExplicitBoxes) {
    ASSERT_NE(load("test/html/layout_broken_image_baseline.html"), nullptr);
    layout_html_doc(&ui, document, false);
    const struct { const char* id; float link_y; float image_height; } cases[] = {
        {"fallback", 3.0f, 24.0f}, {"leading", 11.0f, 40.0f},
        {"loaded", 9.0f, 24.0f}, {"sized", 3.0f, 24.0f},
        {"tall", 3.0f, 60.0f}, {"empty", 9.0f, 24.0f}
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.id);
        char link_id[64], image_id[64];
        snprintf(link_id, sizeof(link_id), "%s-link", item.id);
        snprintf(image_id, sizeof(image_id), "%s-image", item.id);
        DomElement* parent = dom_find_element_by_id(document->root->as_element(), item.id);
        DomElement* link = dom_find_element_by_id(document->root->as_element(), link_id);
        DomElement* image = dom_find_element_by_id(document->root->as_element(), image_id);
        ASSERT_NE(parent, nullptr); ASSERT_NE(link, nullptr); ASSERT_NE(image, nullptr);
        float parent_y = 0.0f, link_y = 0.0f, image_y = 0.0f;
        view_get_visual_bounds(parent, nullptr, &parent_y, nullptr, nullptr);
        view_get_visual_bounds(link, nullptr, &link_y, nullptr, nullptr);
        view_get_visual_bounds(image, nullptr, &image_y, nullptr, nullptr);
        EXPECT_NEAR(link_y - parent_y, item.link_y, 0.1f);
        EXPECT_NEAR(image_y - parent_y, 0.0f, 0.1f);
        EXPECT_NEAR(link->height, 18.0f, 0.1f);
        EXPECT_NEAR(image->height, item.image_height, 0.1f);
    }
}

TEST_F(RadiantTextQuery, LtrIsolatesKeepLeadingNeutralsBeforeLatinRunsInRtlBlocks) {
    ASSERT_NE(load("test/html/layout_bidi_ltr_neutral_isolate.html"), nullptr);
    layout_html_doc(&ui, document, false);
    for (const char* id : {"html", "css"}) {
        DomElement* span = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(span, nullptr);
        EXPECT_EQ(span->block()->direction, CSS_VALUE_LTR) << id;
        EXPECT_EQ(span->block()->unicode_bidi, CSS_VALUE_ISOLATE) << id;
    }
    const struct {const char* id; CssEnum direction;} wide[] = {
        {"inherit", CSS_VALUE_RTL}, {"initial", CSS_VALUE_LTR}, {"unset", CSS_VALUE_RTL}
    };
    for (const auto& item : wide) {
        DomElement* span = dom_find_element_by_id(document->root->as_element(), item.id);
        ASSERT_NE(span, nullptr);
        EXPECT_EQ(span->block()->direction, item.direction) << item.id;
    }
    const char* markers[] = {"html-marker", "css-marker"};
    const char* tails[] = {"html-tail", "css-tail"};
    for (size_t i = 0; i < 2; i++) {
        DomElement* marker = dom_find_element_by_id(document->root->as_element(), markers[i]);
        DomElement* tail = dom_find_element_by_id(document->root->as_element(), tails[i]);
        ASSERT_NE(marker, nullptr); ASSERT_NE(tail, nullptr);
        float marker_x = 0.0f, tail_x = 0.0f;
        view_get_visual_bounds(marker, &marker_x, nullptr, nullptr, nullptr);
        view_get_visual_bounds(tail, &tail_x, nullptr, nullptr, nullptr);
        EXPECT_LT(marker_x, tail_x) << markers[i];
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

TEST_F(RadiantTextQuery, DomCarriageReturnsKeepSpaceGeometryAndPaint) {
    ASSERT_NE(load("test/html/layout_carriage_return_spacing.html"), nullptr);
    process_document_font_faces(&ui, document);
    auto find = [&](const char* id) {
        return dom_find_element_by_id(document->root->as_element(), id);
    };
    auto compare = [&](auto&& self, DomNode* actual, DomNode* control) -> void {
        ASSERT_NE(actual, nullptr); ASSERT_NE(control, nullptr);
        if (actual->is_text()) {
            ASSERT_TRUE(control->is_text());
            DomText* text = actual->as_text();
            DomText* expected = control->as_text();
            TextRect* rect = text->rect;
            TextRect* reference = expected->rect;
            for (; rect && reference; rect = rect->next, reference = reference->next) {
                EXPECT_NEAR(rect->x, reference->x, 0.05f);
                EXPECT_NEAR(rect->y, reference->y, 0.05f);
                EXPECT_NEAR(rect->width, reference->width, 0.05f);
                EXPECT_NEAR(rect->height, reference->height, 0.05f);
                CssEnum mode = get_white_space_value(actual);
                lam::Temp<char> painted(render_text_create_export_segment(text->text_data(),
                    rect, CSS_VALUE_NONE, false, mode));
                lam::Temp<char> expected_paint(render_text_create_export_segment(expected->text_data(),
                    reference, CSS_VALUE_NONE, false, mode));
                EXPECT_STREQ(painted.get(), expected_paint.get());
            }
            EXPECT_EQ(rect, nullptr); EXPECT_EQ(reference, nullptr);
            return;
        }
        ASSERT_TRUE(actual->is_element()); ASSERT_TRUE(control->is_element());
        DomNode* child = actual->as_element()->first_child;
        DomNode* expected_child = control->as_element()->first_child;
        for (; child && expected_child;
                child = child->next_sibling, expected_child = expected_child->next_sibling) {
            self(self, child, expected_child);
        }
        EXPECT_EQ(child, nullptr); EXPECT_EQ(expected_child, nullptr);
    };
    auto expect_pair = [&](const char* prefix) {
        char actual_id[96], control_id[96];
        str_fmt(actual_id, sizeof(actual_id), "%s-cr", prefix);
        str_fmt(control_id, sizeof(control_id), "%s-space", prefix);
        DomElement* actual = find(actual_id);
        DomElement* control = find(control_id);
        ASSERT_NE(actual, nullptr); ASSERT_NE(control, nullptr);
        SCOPED_TRACE(prefix);
        // a DOM CR has space geometry and paint (CSS Text §4), while source bytes stay intact.
        EXPECT_NEAR(actual->width, control->width, 0.05f);
        EXPECT_NEAR(actual->height, control->height, 0.05f);
        compare(compare, actual, control);
    };
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 4000;
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const char* mode : {"normal", "nowrap", "pre", "pre-wrap", "pre-line", "break-spaces"}) {
                for (const char* kind : {"word", "break", "only", "wrap", "zwsp"}) {
                    char prefix[80]; str_fmt(prefix, sizeof(prefix), "%s-%s", mode, kind);
                    expect_pair(prefix);
                }
            }
            for (const char* prefix : {"styled", "bidi", "spacing", "balance"}) expect_pair(prefix);
            for (const char* id : {"pre-line-break-cr", "pre-line-break-space"}) {
                DomElement* owner = find(id);
                ASSERT_NE(owner, nullptr);
                DomText* text = owner->first_child->as_text();
                ASSERT_NE(text, nullptr); ASSERT_NE(text->rect.get(), nullptr);
                // max-content retains the widest forced line after collapsible edge spaces are removed.
                EXPECT_NEAR(owner->width, text->rect->width, 0.05f) << id;
                EXPECT_NEAR(owner->height, 48.0f, 0.05f) << id;
            }
            DomText* source = find("pre-word-cr")->first_child->as_text();
            ASSERT_NE(source, nullptr);
            EXPECT_EQ(source->text_data()[2], '\r');
            DomElement* changed = find("pre-word-cr");
            DomElement* control = find("pre-word-space");
            for (const char* style : {"white-space:normal;width:60px", "white-space:pre;width:max-content",
                    "white-space:pre-line;width:60px", "white-space:pre;width:max-content"}) {
                ASSERT_TRUE(changed->set_attribute("style", style));
                ASSERT_TRUE(control->set_attribute("style", style));
                layout_html_doc(&ui, document, true);
                expect_pair("pre-word");
            }
        }
    }
}

TEST_F(RadiantTextQuery, NoWrapSeparatorsKeepTheirAdjacentWordsTogether) {
    ASSERT_NE(load("test/html/layout_nowrap_separator_lookahead.html"), nullptr);
    process_document_font_faces(&ui, document);
    auto find = [&](const char* id) {
        return dom_find_element_by_id(document->root->as_element(), id);
    };
    auto expect_group = [&](const char* id, float head_y, float tail_delta = 0.0f) {
        char query[96]; str_fmt(query, sizeof(query), "%s-head", id);
        DomElement* head = find(query);
        str_fmt(query, sizeof(query), "%s-tail", id);
        DomElement* tail = find(query);
        ASSERT_NE(head, nullptr); ASSERT_NE(tail, nullptr);
        DomText* head_text = head->first_child->as_text();
        DomText* tail_text = tail->first_child->as_text();
        ASSERT_NE(head_text, nullptr); ASSERT_NE(tail_text, nullptr);
        ASSERT_NE(head_text->rect.get(), nullptr); ASSERT_NE(tail_text->rect.get(), nullptr);
        DomElement* row = find(id);
        ASSERT_NE(row, nullptr);
        DomText* prefix = row->first_child->as_text();
        ASSERT_NE(prefix, nullptr); ASSERT_NE(prefix->rect.get(), nullptr);
        EXPECT_NEAR(head_text->rect->y - prefix->rect->y, head_y, 0.05f) << id;
        EXPECT_NEAR(tail_text->rect->y - head_text->rect->y, tail_delta, 0.05f) << id;
    };
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 2400;
    for (float scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(scale);
        ui_context_set_device_scale(&ui, scale, scale);
        for (int pass = 0; pass < 2; pass++) {
            SCOPED_TRACE(pass);
            layout_html_doc(&ui, document, pass != 0);
            for (int width : {80, 100}) {
                for (const char* kind : {"nbsp", "nowrap-one", "nowrap-five", "nowrap-tab",
                        "nowrap-lf", "nowrap-cr", "wrap-mode", "pre-one", "prewrap-nowrap",
                        "whole-nowrap", "nested-normal"}) {
                    char id[96]; str_fmt(id, sizeof(id), "w%d-%s", width, kind);
                    expect_group(id, width == 80 ? 48.0f : 24.0f);
                }
            }
            expect_group("w80-direct-nowrap", 48.0f);
            expect_group("w80-direct-nbsp", 48.0f);
            expect_group("w80-direct-collapsed", 48.0f);
            EXPECT_NEAR(find("w80-direct-collapsed-tail")->x,
                find("w80-direct-nbsp-tail")->x, 0.05f);
            expect_group("w80-normal", 24.0f, 24.0f);
            expect_group("w100-normal", 24.0f);
            expect_group("w90-small-separator", 24.0f);
            expect_group("w90-spaced-separator", 24.0f);
            DomElement* spaced = find("w90-spaced-separator-space");
            ASSERT_NE(spaced, nullptr); ASSERT_NE(spaced->font.get(), nullptr);
            EXPECT_STREQ(spaced->fontp()->family.get(), find("w90-spaced-separator")->fontp()->family.get());
            expect_group("w80-forced-lf", 24.0f, 24.0f);
            DomElement* changed = find("w80-nowrap-five");
            DomElement* separator = find("w80-nowrap-five-separator");
            ASSERT_NE(changed, nullptr); ASSERT_NE(separator, nullptr);
            // retained width and source-policy edits must change the same legal opportunity.
            ASSERT_TRUE(changed->set_attribute("style", "width:100px"));
            layout_html_doc(&ui, document, true);
            expect_group("w80-nowrap-five", 24.0f);
            ASSERT_TRUE(changed->set_attribute("style", "width:80px"));
            ASSERT_TRUE(separator->set_attribute("style", "white-space:normal"));
            layout_html_doc(&ui, document, true);
            expect_group("w80-nowrap-five", 24.0f, 24.0f);
            ASSERT_TRUE(separator->set_attribute("style", "white-space:nowrap"));
            layout_html_doc(&ui, document, true);
            expect_group("w80-nowrap-five", 48.0f);
        }
    }
}

TEST_F(RadiantTextQuery, LookaheadUsesTheFollowingTextOwnersFont) {
    ASSERT_NE(load("test/html/layout_lookahead_text_owner_font.html"), nullptr);
    process_document_font_faces(&ui, document);
    auto find = [&](const char* id) {
        return dom_find_element_by_id(document->root->as_element(), id);
    };
    auto expect_head = [&](const char* id, bool fits, float expected_x) {
        DomElement* row = find(id);
        char query[96]; str_fmt(query, sizeof(query), "%s-head", id);
        DomElement* head = find(query);
        ASSERT_NE(row, nullptr); ASSERT_NE(head, nullptr);
        DomText* prefix = row->first_child->as_text();
        DomText* text = head->first_child->as_text();
        ASSERT_NE(prefix, nullptr); ASSERT_NE(text, nullptr);
        ASSERT_NE(prefix->rect.get(), nullptr); ASSERT_NE(text->rect.get(), nullptr);
        // a fitting word and its following period stay after the earlier space.
        EXPECT_NEAR(head->x, expected_x, 0.05f) << id;
        EXPECT_EQ(text->rect->y < prefix->rect->y + 20.0f, fits) << id;
    };
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 1400;
    for (float scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(scale);
        ui_context_set_device_scale(&ui, scale, scale);
        for (int pass = 0; pass < 2; pass++) {
            SCOPED_TRACE(pass);
            layout_html_doc(&ui, document, pass != 0);
            for (const char* kind : {"direct", "nested", "decorated", "spaced", "reverse"}) {
                char id[96]; str_fmt(id, sizeof(id), "%s-fit", kind);
                float prefix_width = strcmp(kind, "reverse") == 0 ? 60.0f :
                    strcmp(kind, "spaced") == 0 ? 35.0f : 30.0f;
                expect_head(id, true, prefix_width);
                str_fmt(id, sizeof(id), "%s-overflow", kind);
                expect_head(id, false, 0.0f);
            }
            DomElement* changed = find("direct-fit");
            ASSERT_NE(changed, nullptr);
            // a width edit changes the legal break without retaining a speculative font.
            ASSERT_TRUE(changed->set_attribute("style", "width:59px"));
            layout_html_doc(&ui, document, true);
            expect_head("direct-fit", false, 0.0f);
            ASSERT_TRUE(changed->set_attribute("style", "width:60px"));
            layout_html_doc(&ui, document, true);
            expect_head("direct-fit", true, 30.0f);
            ASSERT_TRUE(changed->set_attribute("style", "width:60px;font-size:11px"));
            layout_html_doc(&ui, document, true);
            expect_head("direct-fit", false, 0.0f);
            ASSERT_TRUE(changed->set_attribute("style", "width:60px;font-size:10px"));
            layout_html_doc(&ui, document, true);
            expect_head("direct-fit", true, 30.0f);
        }
    }
}

#ifdef __APPLE__
#ifdef __APPLE__
TEST_F(RadiantTextQuery, CombiningClustersKeepInkAcrossTransformsAndRetainedPaint) {
    DomElement* root = load("test/html/layout_combining_cluster_paint.html");
    ASSERT_NE(root, nullptr);
    DomElement* changed = dom_find_element_by_id(document->root->as_element(), "normal-acute");
    ASSERT_NE(changed, nullptr);
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 1200; ui.viewport_height = 2040;
        ui.window_width = 1200 * scale; ui.window_height = 2040 * scale;
        ui.create_surface(ui.window_width, ui.window_height);
        for (unsigned pass = 0; pass < 3; pass++) {
            SCOPED_TRACE(scale);
            SCOPED_TRACE(pass);
            if (pass == 2) ASSERT_TRUE(changed->set_attribute("style", "font-size:24px;font-family:Arial"));
            layout_html_doc(&ui, document, pass != 0);
            render_html_doc(&ui, document->view_tree, nullptr);
            ASSERT_FALSE(ui.render_failed);
            size_t pairs = 0;
            for (DomNode* node = root->first_child; node; node = node->next_sibling) {
                if (!node->is_element() || node->as_element()->tag() != MARKUP_NAME_DIV) continue;
                DomElement* row = node->as_element();
                const char* id = row->get_attribute("id"); ASSERT_NE(id, nullptr);
                SCOPED_TRACE(id);
                DomElement* first = row->first_child->as_element();
                DomElement* second = first->next_sibling->as_element();
                ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
                float ax, ay, bx, by;
                view_get_visual_bounds(first, &ax, &ay, nullptr, nullptr);
                view_get_visual_bounds(second, &bx, &by, nullptr, nullptr);
                EXPECT_FLOAT_EQ(ay, by); EXPECT_FLOAT_EQ(first->width, second->width);
                DomElement* left_ink = first->first_child->as_element();
                DomElement* right_ink = second->first_child->as_element();
                ASSERT_NE(left_ink, nullptr); ASSERT_NE(right_ink, nullptr);
                float padding = row->fontp()->font_size * 0.25f;
                // compare padded ink, excluding the neighboring column's negative glyph bearings.
                unsigned x1 = (unsigned)lroundf((ax - padding) * scale);
                unsigned x2 = (unsigned)lroundf((bx - padding) * scale);
                unsigned top = (unsigned)lroundf(ay * scale);
                unsigned width = (unsigned)ceilf((max(left_ink->width, right_ink->width) + 2 * padding) * scale);
                ASSERT_LT(width, (unsigned)lroundf((bx - ax - padding) * scale));
                unsigned height = (unsigned)lroundf(row->height * scale);
                ASSERT_LE(x1 + width, (unsigned)ui.surface->width);
                ASSERT_LE(x2 + width, (unsigned)ui.surface->width);
                ASSERT_LE(top + height, (unsigned)ui.surface->height);
                unsigned maximum_error = 0; size_t dark_pixels = 0;
                unsigned first_error_x = 0, first_error_y = 0, first_left = 0, first_right = 0;
                for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < width; x++) {
                    const uint8_t* left = (const uint8_t*)ui.surface->pixels +
                        (top + y) * ui.surface->pitch + (x1 + x) * 4;
                    const uint8_t* right = (const uint8_t*)ui.surface->pixels +
                        (top + y) * ui.surface->pitch + (x2 + x) * 4;
                    for (unsigned channel = 0; channel < 4; channel++) {
                        unsigned error = left[channel] > right[channel]
                            ? left[channel] - right[channel] : right[channel] - left[channel];
                        if (error && !maximum_error) {
                            first_error_x = x; first_error_y = y;
                            memcpy(&first_left, left, 4); memcpy(&first_right, right, 4);
                        }
                        if (error > maximum_error) maximum_error = error;
                    }
                    if (left[0] < 192 && left[1] < 192 && left[2] < 192) dark_pixels++;
                }
                // equivalent source forms must retain ink, including marks that have no advance.
                EXPECT_GT(dark_pixels, 0u); EXPECT_EQ(maximum_error, 0u)
                    << "origins " << ax << ',' << ay << " / " << bx << ',' << by
                    << " first difference " << first_error_x << ',' << first_error_y
                    << " rgba " << first_left << " / " << first_right;
                pairs++;
            }
            EXPECT_EQ(pairs, 42u);
            if (pass == 2) EXPECT_FLOAT_EQ(changed->fontp()->font_size, 24.0f);
            DomElement* source = dom_find_element_by_id(root, "normal-acute-nfd");
            ASSERT_NE(source, nullptr); ASSERT_NE(source->first_child, nullptr);
            EXPECT_STREQ((const char*)source->first_child->text_data(), "e\u0301M");
        }
        ASSERT_TRUE(changed->set_attribute("style", "font-size:16px;font-family:Arial"));
    }
}
#endif

TEST_F(RadiantTextQuery, FloatPaintPhasesPreserveBackgroundsInlineInkAndHitOrder) {
    ASSERT_NE(load("test/html/layout_float_paint_order.html"), nullptr);
    auto find = [&](const char* id) {
        return dom_find_element_by_id(document->root->as_element(), id);
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 1200; ui.viewport_height = 1200;
        ui.window_width = 1200 * scale; ui.window_height = 1200 * scale;
        ui.create_surface(ui.window_width, ui.window_height);
        for (unsigned pass = 0; pass < 3; pass++) {
            SCOPED_TRACE(scale);
            SCOPED_TRACE(pass);
            DomElement* alpha_flow = find("alpha-native")->last_child->as_element();
            ASSERT_NE(alpha_flow, nullptr);
            if (pass == 2) ASSERT_TRUE(alpha_flow->set_attribute("style", "background:lime"));
            layout_html_doc(&ui, document, pass != 0);
            render_html_doc(&ui, document->view_tree, nullptr);
            ASSERT_FALSE(ui.render_failed);
            auto pixel = [&](const char* id, float dx, float dy, unsigned r, unsigned g, unsigned b) {
                DomElement* box = find(id); ASSERT_NE(box, nullptr);
                float x = 0, y = 0; view_get_visual_bounds(box, &x, &y, nullptr, nullptr);
                test_radiant_expect_surface_pixel(ui.surface,
                    (unsigned)lroundf((x + dx) * scale), (unsigned)lroundf((y + dy) * scale), r, g, b);
            };
            struct VisitProbe : RenderContext { ViewElement* watched; unsigned visits; } probe = {};
            probe.watched = lam::view_require_element(static_cast<View*>(alpha_flow)); probe.ui_context = lam::up(&ui);
            RenderBackend backend = {}; backend.ctx = lam::up(&probe);
            backend.visit_element = [](RenderContext* context, ViewElement* view, float, float) {
                VisitProbe* probe = (VisitProbe*)context;
                if (view == probe->watched) probe->visits++;
            };
            RenderWalkState walk = {}; walk.ui_context = lam::up(&ui);
            setup_font(&ui, &walk.font, &ui.default_font);
            render_walk_block(&backend, &walk, lam::view_require_block(document->view_tree->root));
            // export metadata belongs to one logical element visit, regardless of paint passes.
            EXPECT_EQ(probe.visits, 1u);
            pixel("opaque-native", 15, 15, 153, 153, 153);
            pixel("alpha-native", 15, 15, pass == 2 ? 0 : 127, pass == 2 ? 127 : 0, 128);
            pixel("nested-native", 15, 15, 0, 128, 0);
            pixel("nested-native", 60, 15, 153, 153, 153);
            pixel("inline", 15, 15, 0, 128, 0);
            pixel("inline", 60, 15, 153, 153, 153);
            pixel("clipped", 15, 15, 0, 0, 255);
            pixel("clipped", 15, 50, 255, 255, 255);
            pixel("opacity", 15, 15, 127, 127, 255);
            pixel("no-pointer", 15, 15, 153, 153, 153);
            pixel("positioned", 15, 15, 255, 0, 0);
            pixel("no-float", 15, 15, 0, 128, 0);
            pixel("no-float", 100, 15, 0, 0, 255);
            pixel("replaced-svg", 15, 15, 0, 0, 255);
            pixel("replaced-frame", 15, 15, 0, 0, 255);
            DomElement* frame = find("replaced-frame");
            ASSERT_NE(frame->embed.get(), nullptr); ASSERT_NE(frame->embedp()->doc.get(), nullptr);
            DomElement* frame_ink = dom_find_element_by_id(frame->embedp()->doc->root->as_element(), "frame-ink");
            ASSERT_NE(frame_ink, nullptr);
            float frame_x = 0, frame_y = 0;
            view_get_visual_bounds(frame, &frame_x, &frame_y, nullptr, nullptr);
            EXPECT_EQ(radiant_document_element_from_point(document, frame_x + 15, frame_y + 15), frame_ink);
            struct Hit { const char* box; const char* target; } hits[] = {
                {"inline", "inline-ink"}, {"clipped", "clipped-float"},
                {"opacity", "opacity-float"}, {"no-pointer", "no-pointer-flow"},
                {"positioned", "positioned-flow"}, {"no-float", "no-float-ink"},
            };
            for (const Hit& hit : hits) {
                float x = 0, y = 0; view_get_visual_bounds(find(hit.box), &x, &y, nullptr, nullptr);
                EXPECT_EQ(radiant_document_element_from_point(document, x + 15, y + 15), find(hit.target)) << hit.box;
            }
        }
        ASSERT_TRUE(find("alpha-native")->last_child->as_element()->set_attribute("style", "background:red"));
    }
}

TEST_F(RadiantTextQuery, SystemColorFontTrackingDoesNotChangeCssAdvances) {
    ASSERT_NE(load("test/html/layout_system_color_font_tracking.html"), nullptr);
    auto find = [&](const char* id) {
        return dom_find_element_by_id(document->root->as_element(), id);
    };
    auto expect_width = [&](const char* id, float width) {
        DomElement* glyph = find(id);
        ASSERT_NE(glyph, nullptr);
        DomText* text = glyph->first_child->as_text();
        ASSERT_NE(text, nullptr); ASSERT_NE(text->rect.get(), nullptr);
        EXPECT_NEAR(text->rect->width, width, 0.05f) << id;
    };
    struct Case { int size; float advance; } cases[] = {
        {12, 15.0f}, {13, 16.0f}, {15, 19.0f}, {16, 20.0f},
        {17, 21.0f}, {20, 22.0f}, {32, 32.0f}, {64, 64.0f},
    };
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 1400;
    for (float scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(scale);
        ui_context_set_device_scale(&ui, scale, scale);
        for (int pass = 0; pass < 2; pass++) {
            SCOPED_TRACE(pass);
            layout_html_doc(&ui, document, pass != 0);
            for (const Case& entry : cases) {
                char id[64]; str_fmt(id, sizeof(id), "size-%d-glyph", entry.size);
                expect_width(id, entry.advance);
            }
            for (const char* id : {"family-glyph", "fallback-glyph", "cluster-glyph"})
                expect_width(id, 20.0f);
            DomElement* legacy = find("size-16-legacy");
            ASSERT_NE(legacy, nullptr); ASSERT_NE(legacy->font.get(), nullptr);
            EXPECT_FLOAT_EQ(legacy->fontp()->font_size, 18.0f);
            DomElement* annotation = find("size-16-ruby");
            ASSERT_NE(annotation, nullptr); ASSERT_NE(annotation->font.get(), nullptr);
            EXPECT_FLOAT_EQ(annotation->fontp()->font_size, 8.0f);
            DomElement* first = find("spaced-first");
            DomElement* second = find("spaced-second");
            ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
            EXPECT_NEAR(second->x - first->x, 23.0f, 0.05f);
            EXPECT_NEAR(find("wrapped")->height, 48.0f, 0.05f);
            expect_width("ascii-glyph", 31.8125f);
            // retained size edits must not reuse the preceding color-font metric.
            DomElement* changed = find("size-16");
            ASSERT_NE(changed, nullptr);
            ASSERT_TRUE(changed->set_attribute("style", "font-size:20px"));
            layout_html_doc(&ui, document, true);
            ASSERT_NE(changed->font.get(), nullptr);
            EXPECT_FLOAT_EQ(changed->fontp()->font_size, 20.0f);
            DomElement* inherited = find("size-16-glyph");
            ASSERT_NE(inherited, nullptr); ASSERT_NE(inherited->font.get(), nullptr);
            EXPECT_FLOAT_EQ(inherited->fontp()->font_size, 20.0f);
            expect_width("size-16-glyph", 22.0f);
            DomElement* ascii = find("size-16-ascii");
            DomElement* ascii_control = find("size-20-ascii");
            DomElement* fixed = find("size-16-fixed");
            ASSERT_NE(ascii, nullptr); ASSERT_NE(ascii_control, nullptr); ASSERT_NE(fixed, nullptr);
            ASSERT_NE(ascii->font.get(), nullptr); ASSERT_NE(fixed->font.get(), nullptr);
            EXPECT_FLOAT_EQ(ascii->fontp()->font_size, 20.0f);
            EXPECT_FLOAT_EQ(fixed->fontp()->font_size, 12.0f);
            EXPECT_NEAR(ascii->width, ascii_control->width, 0.05f);
            EXPECT_FLOAT_EQ(legacy->fontp()->font_size, 18.0f);
            DomElement* invalid = find("size-16-invalid");
            ASSERT_NE(invalid, nullptr); ASSERT_NE(invalid->font.get(), nullptr);
            EXPECT_FLOAT_EQ(invalid->fontp()->font_size, 20.0f);
            EXPECT_FLOAT_EQ(annotation->fontp()->font_size, 10.0f);
            ASSERT_TRUE(changed->set_attribute("style", "font-size:16px"));
            layout_html_doc(&ui, document, true);
            expect_width("size-16-glyph", 20.0f);
        }
    }
}
#endif

TEST_F(RadiantTextQuery, SpaceCreatedWrapsUseTheirSourceInlinePolicy) {
    ASSERT_NE(load("test/html/layout_carriage_return_spacing.html"), nullptr);
    process_document_font_faces(&ui, document);
    auto find = [&](const char* id) {
        return dom_find_element_by_id(document->root->as_element(), id);
    };
    ui.viewport_width = ui.window_width = 1200;
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const char* kind : {"pre-edge", "nowrap-edge", "nested-edge"}) {
                for (const char* variant : {"space", "cr"}) {
                    char id[96]; str_fmt(id, sizeof(id), "%s-%s-tail", kind, variant);
                    DomElement* tail = find(id);
                    ASSERT_NE(tail, nullptr);
                    SCOPED_TRACE(id);
                    bool nested = strcmp(kind, "nested-edge") == 0;
                    EXPECT_NEAR(tail->x, nested ? 0.0f : strcmp(kind, "pre-edge") == 0 ? 80.0f : 40.0f, 0.05f);
                    DomText* head = tail->parent->as_element()->first_child->as_text();
                    ASSERT_NE(head, nullptr); ASSERT_NE(head->rect.get(), nullptr);
                    EXPECT_NEAR(tail->y - head->rect->y, nested ? 24.0f : 0.0f, 0.05f);
                }
            }
            DomElement* atomic_tail = find("atomic-edge-tail");
            ASSERT_NE(atomic_tail, nullptr);
            EXPECT_NEAR(atomic_tail->x, 0.0f, 0.05f);
        }
    }
}

TEST_F(RadiantTextQuery, BidiRangesRetainShapedAdvancesAcrossPreservedBreaks) {
    ASSERT_NE(load("test/html/layout_bidi_advance_ranges.html"), nullptr);
    process_document_font_faces(&ui, document);
    auto box = [&](const char* id) {
        return dom_find_element_by_id(document->root->as_element(), id);
    };
    auto expect_ranges = [&](const char* id, const char* first, const char* second = nullptr) {
        DomElement* owner = box(id);
        ASSERT_NE(owner, nullptr) << id;
        DomText* text = owner->first_child ? owner->first_child->as_text() : nullptr;
        ASSERT_NE(text, nullptr) << id;
        const char* controls[] = {first, second};
        size_t index = 0;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->length == 0) continue;
            ASSERT_LT(index, second ? 2u : 1u) << id;
            DomElement* control = box(controls[index++]);
            ASSERT_NE(control, nullptr);
            // a range's advance is independent of fallback glyph ink and preserved source breaks.
            EXPECT_NEAR(rect->width, control->width, 0.05f) << id;
        }
        EXPECT_EQ(index, second ? 2u : 1u) << id;
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = ui.window_width = 1200;
        ui.viewport_height = ui.window_height = 2000;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const char* id : {"plain", "rtl", "hebrew", "hebrew-no-lf", "latin",
                    "document", "spaced", "tabbed", "bold-control", "ascii-control", "space-control", "trailing-space-lf"})
                expect_ranges(id, id);
            expect_ranges("trailing-lf", "plain");
            expect_ranges("trailing-crlf", "trailing-space-lf");
            expect_ranges("line-before", "ascii-control", "plain");
            expect_ranges("line-after", "plain", "ascii-control");
            expect_ranges("repeat", "repeat", "repeat");
            DomElement* styled = box("styled-word");
            DomElement* bold = box("bold-control");
            ASSERT_NE(styled, nullptr); ASSERT_NE(bold, nullptr);
            EXPECT_NEAR(styled->width, bold->width, 0.05f);
            DomElement* changed = box("trailing-lf");
            ASSERT_NE(changed, nullptr);
            for (const char* family : {"font-family:'Courier New',monospace",
                    "font-family:Menlo,'Courier New',monospace"}) {
                ASSERT_TRUE(changed->set_attribute("style", family));
                layout_html_doc(&ui, document, true);
                expect_ranges("trailing-lf", "trailing-lf");
            }
        }
    }
}

TEST_F(RadiantTextQuery, PreservedTabsKeepAdvanceBoundsWithFallbackBidiGlyphs) {
    ASSERT_NE(load("test/html/layout_bidi_preserved_tabs.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    layout_html_doc(&ui, document, false);
    DomElement* control = dom_find_element_by_id(document->root->as_element(), "latin-ltr-control");
    ASSERT_NE(control, nullptr);
    ASSERT_GT(control->width, 300.0f);
    const char* ids[] = {"latin-ltr-nested", "latin-ltr-override", "hebrew-ltr-nested",
        "hebrew-ltr-override", "hebrew-ltr-control", "latin-rtl-nested", "latin-rtl-override",
        "latin-rtl-control", "hebrew-rtl-nested", "hebrew-rtl-override", "hebrew-rtl-control"};
    for (const char* id : ids) {
        DomElement* span = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(span, nullptr) << id;
        EXPECT_NEAR(span->width, control->width, 0.1f) << id;
        EXPECT_NEAR(span->x, control->x, 0.1f) << id;
    }
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

TEST_F(RadiantTextQuery, IntrinsicTablesShareCollapsedBorderGeometry) {
    ASSERT_NE(load("test/html/layout_table_intrinsic_collapsed_borders.html"), nullptr);
    process_document_font_faces(&ui, document);
    struct Sample { const char* id; float width; } samples[] = {
        {"table-auto", 536}, {"table-max", 536}, {"block-five", 536},
        {"atomic-five", 536}, {"block-one", 108}, {"block-two", 215},
        {"thick", 542}, {"mixed", 152}, {"mixed-table", 152}, {"hidden", 141},
        {"spanning", 195}, {"rowspan", 162}, {"row-border", 150}, {"columns", 150},
        {"separate", 558}, {"separate-table", 558}, {"minimum", 436},
        {"table-border", 150}, {"table-border-parent", 150}, {"block-border", 152},
        {"row-group", 150}, {"column-group", 150}, {"shifted-span", 155}, {"fractional", 538.5f},
    };
    auto expect_width = [&](const char* id, float width) {
        DomElement* table = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(table, nullptr);
        EXPECT_FLOAT_EQ(table->width, width) << id;
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = ui.window_width = 1200;
        ui.viewport_height = ui.window_height = 2000;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) expect_width(sample.id, sample.width);
            DomElement* table = dom_find_element_by_id(document->root->as_element(), "block-five");
            ASSERT_NE(table, nullptr);
            // changing collapse changes grid geometry; retained measurement must discard old edges.
            ASSERT_TRUE(table->set_attribute("style", "border-collapse:separate"));
            layout_html_doc(&ui, document, true);
            expect_width("block-five", 540);
            ASSERT_TRUE(table->set_attribute("style", "border-collapse:collapse"));
            layout_html_doc(&ui, document, true);
            expect_width("block-five", 536);
            DomElement* row = dom_find_element_by_id(document->root->as_element(), "row-border-row");
            ASSERT_NE(row, nullptr);
            ASSERT_TRUE(row->set_attribute("style", "border-left:8px solid black;border-right:10px solid black"));
            layout_html_doc(&ui, document, true);
            expect_width("row-border", 158);
            ASSERT_TRUE(row->set_attribute("style", "border-left:4px solid black;border-right:6px solid black"));
            layout_html_doc(&ui, document, true);
            expect_width("row-border", 150);
        }
    }
}

TEST_F(RadiantTextQuery, TableRowsConsumeCollapsedBlockMarginsOnce) {
    ASSERT_NE(load("test/html/layout_table_collapsed_margin_extents.html"), nullptr);
    process_document_font_faces(&ui, document);
    struct Sample { const char* id; float height; } samples[] = {
        {"ordinary", 82}, {"flex", 82}, {"leading", 66}, {"trailing", 66},
        {"no-empty", 66}, {"tail-larger", 74}, {"tail-smaller", 66},
        {"negative", 58}, {"mixed-chain", 54}, {"leading-negative", 58},
        {"all-empty", 26}, {"all-empty-mixed", 14}, {"nested", 66},
        {"barrier", 75}, {"min-height", 75}, {"relative", 66},
        {"separate", 66}, {"zero", 50},
        {"mixed-float", 90}, {"aligned-float", 130},
    };
    auto expect_height = [&](const char* id, float height) {
        SCOPED_TRACE(id);
        DomElement* table = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(table, nullptr);
        char cell_id[48]; str_fmt(cell_id, sizeof(cell_id), "%s-cell", id);
        DomElement* cell = dom_find_element_by_id(document->root->as_element(), cell_id);
        ASSERT_NE(cell, nullptr);
        EXPECT_FLOAT_EQ(table->height, height) << "cell flow height: " << cell->content_height;
        EXPECT_FLOAT_EQ(cell->height, height);
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 600; ui.viewport_height = 2000;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) expect_height(sample.id, sample.height);
            for (const char* id : {"mixed-float-cell", "aligned-float-cell"}) {
                DomElement* cell = dom_find_element_by_id(document->root->as_element(), id);
                ASSERT_NE(cell, nullptr);
                DomElement* floating = cell->first_child ? cell->first_child->as_element() : nullptr;
                ASSERT_NE(floating, nullptr);
                float cell_y = 0, float_y = 0;
                view_get_visual_bounds(cell, nullptr, &cell_y, nullptr, nullptr);
                view_get_visual_bounds(floating, nullptr, &float_y, nullptr, nullptr);
                // centering a taller row must not feed the visual float shift back into its natural height.
                EXPECT_FLOAT_EQ(float_y - cell_y, strcmp(id, "mixed-float-cell") == 0 ? 5.0f : 25.0f);
                EXPECT_FLOAT_EQ(cell->content_height, 80.0f);
            }
            DomElement* tail = dom_find_element_by_id(document->root->as_element(), "ordinary-tail");
            ASSERT_NE(tail, nullptr); ASSERT_NE(tail->bound, nullptr);
            const Margin* resolved = view_boundary_flow_margin(tail->boundary());
            ASSERT_NE(resolved, nullptr);
            // the zero authored margin stays zero even when flow promotes the preceding chain.
            EXPECT_FLOAT_EQ(resolved->top, 0.0f);
            EXPECT_FLOAT_EQ(resolved->bottom, 0.0f);
            ASSERT_TRUE(tail->set_attribute("style", "margin-bottom:24px"));
            layout_html_doc(&ui, document, true);
            expect_height("ordinary", 90);
            ASSERT_TRUE(tail->set_attribute("style", "margin:0"));
            layout_html_doc(&ui, document, true);
            expect_height("ordinary", 82);
        }
    }
}

TEST_F(RadiantTextQuery, TableCellsMeasurePositionedInlineBoxesAndTheirRootStruts) {
    ASSERT_NE(load("test/html/layout_table_inline_extents.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    struct Case {
        const char* table;
        const char* cell;
        const char* atom;
        float height, atom_top;
    } cases[] = {
        {"bare", "bare-cell", "bare-atom", 42, 0},
        {"wrapped", "wrapped-cell", "wrapped-atom", 42, 0},
        {"nested", "nested-cell", "nested-atom", 42, 0},
        {"rowspan", "rowspan-cell", "rowspan-atom", 42, 0},
        {"break", "break-cell", "break-atom", 52, 10},
        {"aligned", "aligned-cell", "aligned-atom", 80, 19},
        {"font-strut", "font-strut-cell", nullptr, 40, 0},
        {"glyph-overflow", "glyph-overflow-cell", nullptr, 23, 0},
        {"relative", "relative-cell", "relative-atom", 52, 30},
        {"image", "image-cell", "image-atom", 52, 10},
        {"margins", "margins-cell", nullptr, 50, 0},
    };
    for (int pass = 0; pass < 3; pass++) {
        layout_html_doc(&ui, document, pass > 0);
        float top = 0;
        for (const Case& entry : cases) {
            SCOPED_TRACE(entry.table);
            expect_box(entry.table, 0, top, 200, entry.height);
            DomElement* cell = dom_find_element_by_id(document->root->as_element(), entry.cell);
            ASSERT_NE(cell, nullptr);
            EXPECT_FLOAT_EQ(cell->height, entry.height);
            if (entry.atom) {
                // visual offsets move the atom, while the table keeps its normal-flow line height.
                expect_box(entry.atom, 0, top + entry.atom_top, 30, 40);
            }
            top += entry.height + 10;
        }
        DomElement* margins = dom_find_element_by_id(document->root->as_element(), "margins-cell");
        ASSERT_NE(margins, nullptr);
        float margin_x = 0, margin_y = 0;
        view_get_visual_bounds(margins, &margin_x, &margin_y, nullptr, nullptr);
        expect_box("margins-first", margin_x, margin_y + 10, 200, 10);
        expect_box("margins-last", margin_x, margin_y + 30, 200, 10);
    }
}

TEST_F(RadiantTextQuery, IntrinsicGridRowsRespectForcedBreaksWithoutSoftWrapping) {
    ASSERT_NE(load("test/html/layout_intrinsic_text_wrap.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    struct Case { const char* grid; const char* content; float y, height; } cases[] = {
        {"grid-pre", "pre", 0, 20}, {"grid-nowrap", "nowrap", 30, 20},
        {"grid-pre-line", "pre-line", 60, 30}, {"grid-pre-wrap", "pre-wrap", 100, 30},
        {"grid-mode", "mode", 140, 20}, {"grid-inherited", "inherited", 170, 20},
        {"grid-code", "code", 200, 20}, {"grid-blank", "blank", 230, 30},
        {"grid-terminal-pre", "terminal-pre", 270, 20},
        {"grid-terminal-wrap", "terminal-wrap", 300, 30},
        {"grid-terminal-br", "terminal-br", 340, 20},
    };
    // estimated grid contributions must agree with the rendered forced-line groups after reflow.
    for (int pass = 0; pass < 3; pass++) {
        layout_html_doc(&ui, document, pass > 0);
        for (const Case& entry : cases) {
            expect_box(entry.grid, 0, entry.y, 120, entry.height);
            expect_box(entry.content, 0, entry.y, 120, entry.height);
        }
    }
}

TEST_F(RadiantTextQuery, BalancedForcedBreakGroupsMatchIndependentParagraphs) {
    ASSERT_NE(load("test/html/layout_balance_paragraphs.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    struct Line { float y, left, right; };
    struct Lines { Line values[16]; size_t count; };
    auto block_for_id = [&](const char* id) {
        return lam::view_as_block(dom_find_element_by_id(document->root->as_element(), id));
    };
    auto collect_lines = [&](auto&& self, DomNode* node, ViewBlock* root, Lines* lines) -> void {
        if (node->is_text() && node->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(node);
            ViewBlock* owner = layout_nearest_block_ancestor(text->parent_view());
            float owner_x = 0, owner_y = 0, root_x = 0, root_y = 0;
            view_get_visual_bounds(owner, &owner_x, &owner_y, nullptr, nullptr);
            view_get_visual_bounds(root, &root_x, &root_y, nullptr, nullptr);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (rect->width <= 0.0f) continue;
                float x = owner_x - root_x + rect->x;
                float y = owner_y - root_y + rect->y;
                size_t index = 0;
                while (index < lines->count && fabsf(lines->values[index].y - y) > 0.1f) index++;
                if (index == lines->count) {
                    ASSERT_LT(index, sizeof(lines->values) / sizeof(lines->values[0]));
                    lines->values[lines->count++] = {y, x, x + rect->width};
                } else {
                    lines->values[index].left = min(lines->values[index].left, x);
                    lines->values[index].right = max(lines->values[index].right, x + rect->width);
                }
            }
        } else if (node->is_element()) {
            for (DomNode* child = node->as_element()->first_child; child; child = child->next_sibling)
                self(self, child, root, lines);
        }
    };
    struct Case { const char* id; const char* reference; float height; size_t count; float widths[6]; } cases[] = {
        {"groups", "groups-ref", 50, 5, {80, 80, 90, 40, 60}},
        {"reverse", "reverse-ref", 30, 3, {60, 80, 80}},
        {"nested", "nested-ref", 40, 4, {80, 80, 80, 110}},
        {"newlines", "newlines-ref", 50, 4, {80, 80, 80, 80}},
        {"six-lines", "six-lines-ref", 60, 6, {100, 100, 100, 90, 110, 110}},
        {"line-count", "line-count-ref", 30, 3, {70, 70, 30}},
    };
    // the source and independent blocks must keep the same breaks after retained reflow.
    for (int pass = 0; pass < 3; pass++) {
        layout_html_doc(&ui, document, pass > 0);
        for (const Case& entry : cases) {
            SCOPED_TRACE(entry.id);
            ViewBlock* source = block_for_id(entry.id);
            ViewBlock* reference = block_for_id(entry.reference);
            ASSERT_NE(source, nullptr);
            ASSERT_NE(reference, nullptr);
            EXPECT_FLOAT_EQ(source->height, entry.height);
            EXPECT_FLOAT_EQ(reference->height, entry.height);
            Lines actual = {}, expected = {};
            collect_lines(collect_lines, source, source, &actual);
            collect_lines(collect_lines, reference, reference, &expected);
            ASSERT_EQ(actual.count, entry.count);
            ASSERT_EQ(expected.count, entry.count);
            for (size_t index = 0; index < entry.count; index++) {
                EXPECT_NEAR(actual.values[index].y, expected.values[index].y, 0.1f);
                EXPECT_NEAR(actual.values[index].left, expected.values[index].left, 0.1f);
                EXPECT_NEAR(actual.values[index].right, expected.values[index].right, 0.1f);
                EXPECT_NEAR(actual.values[index].right - actual.values[index].left, entry.widths[index], 0.1f);
            }
        }
        Lines balanced = {}, automatic = {}, cleared = {};
        ViewBlock* source = block_for_id("external-float");
        ViewBlock* reference = block_for_id("external-float-auto");
        ViewBlock* clearance = block_for_id("float-clearance");
        ASSERT_NE(source, nullptr);
        ASSERT_NE(reference, nullptr);
        ASSERT_NE(clearance, nullptr);
        collect_lines(collect_lines, source, source, &balanced);
        collect_lines(collect_lines, reference, reference, &automatic);
        collect_lines(collect_lines, clearance, clearance, &cleared);
        ASSERT_EQ(balanced.count, 5u);
        ASSERT_EQ(automatic.count, balanced.count);
        EXPECT_FLOAT_EQ(source->height, 50);
        EXPECT_FLOAT_EQ(reference->height, source->height);
        EXPECT_FLOAT_EQ(balanced.values[0].y, 0);
        auto remaining_space_variance = [](const Lines& lines) {
            float total = 0, squares = 0;
            for (size_t index = 0; index < lines.count; index++) {
                const Line& line = lines.values[index];
                float available = line.y < 30 ? 130 : 280;
                float remaining = available - (line.right - line.left);
                total += remaining;
                squares += remaining * remaining;
            }
            float mean = total / (float)lines.count;
            return squares / (float)lines.count - mean * mean;
        };
        // CSS Text balances remaining space after float exclusion, preserving auto line count.
        EXPECT_LT(remaining_space_variance(balanced), remaining_space_variance(automatic));
        ASSERT_EQ(cleared.count, 1u);
        EXPECT_FLOAT_EQ(cleared.values[0].y, 30);
        EXPECT_FLOAT_EQ(cleared.values[0].right - cleared.values[0].left, 260);
        EXPECT_FLOAT_EQ(clearance->height, 40);
    }
}

TEST_F(RadiantTextQuery, AnonymousGridTextParticipatesInTracksAndRetainedLayout) {
    ASSERT_NE(load("test/html/layout_grid_anonymous_text.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    struct Case {
        const char* id;
        float y, width, height, text_x, text_y, text_width, next_x;
        bool wraps;
    } cases[] = {
        {"indent", 8, 106, 26, 53, 3, 50, 3, true},
        {"fixed", 46, 166, 26, 63, 3, 50, 63, true},
        {"before", 84, 166, 26, 3, 3, 50, 3, true},
        {"only", 122, 186, 26, 3, 3, 50, 3, true},
        {"aligned", 160, 166, 56, 113, 23, 50, 0, false},
        {"percent", 228, 206, 26, 83, 3, 50, 43, true},
        {"contents", 266, 166, 26, 63, 3, 50, 0, false},
        {"fits", 332, 166, 16, 63, 3, 50, 0, false},
        {"baseline", 360, 166, 26, 3, 11, 10, 0, false},
    };
    // repeated retained passes also exercise trial formatting over existing rectangles.
    for (int pass = 0; pass < 3; pass++) {
        layout_html_doc(&ui, document, pass > 0);
        for (const Case& entry : cases) {
            expect_box(entry.id, 8, entry.y, entry.width, entry.height);
            DomElement* grid = dom_find_element_by_id(document->root->as_element(), entry.id);
            ASSERT_NE(grid, nullptr);
            DomNode* child = grid->first_child;
            while (child && !child->is_text()) {
                if (child->is_element() && child->as_element()->display.outer == CSS_VALUE_CONTENTS) {
                    child = child->as_element()->first_child;
                    break;
                }
                child = child->next_sibling;
            }
            ASSERT_NE(child, nullptr) << entry.id;
            ASSERT_TRUE(child->is_text()) << entry.id;
            TextRect* rect = child->as_text()->rect;
            ASSERT_NE(rect, nullptr) << entry.id;
            EXPECT_FLOAT_EQ(rect->x, entry.text_x) << entry.id;
            EXPECT_FLOAT_EQ(rect->y, entry.text_y) << entry.id;
            EXPECT_FLOAT_EQ(rect->width, entry.text_width) << entry.id;
            EXPECT_FLOAT_EQ(rect->height, 10) << entry.id;
            if (entry.wraps) {
                ASSERT_NE(rect->next, nullptr) << entry.id;
                EXPECT_FLOAT_EQ(rect->next->x, entry.next_x) << entry.id;
                EXPECT_FLOAT_EQ(rect->next->y, 13) << entry.id;
                EXPECT_FLOAT_EQ(rect->next->width, 100) << entry.id;
                EXPECT_EQ(rect->next->next, nullptr) << entry.id;
            } else {
                EXPECT_EQ(rect->next, nullptr) << entry.id;
            }
        }
        expect_box("whitespace", 8, 304, 166, 16);
        DomElement* whitespace = dom_find_element_by_id(document->root->as_element(), "whitespace");
        for (DomNode* child = whitespace->first_child; child; child = child->next_sibling) {
            if (child->is_text()) EXPECT_EQ(child->as_text()->rect, nullptr);
        }
        DomElement* contents = dom_find_element_by_id(document->root->as_element(), "contents");
        ASSERT_TRUE(contents->last_child->is_text());
        TextRect* tail = contents->last_child->as_text()->rect;
        ASSERT_NE(tail, nullptr);
        EXPECT_FLOAT_EQ(tail->x, 63);
        EXPECT_FLOAT_EQ(tail->y, 13);
        EXPECT_FLOAT_EQ(tail->width, 100);
        // equal 20px font/line boxes expose the inline-grid's exported baseline.
        expect_box("baseline-large", 71, 363, 100, 20);
        expect_box("baseline-reference", 174, 363, 20, 20);
    }
}

TEST_F(RadiantTextQuery, FontSpacingRetainsComputedLengthsAndInheritedPercentages) {
    ASSERT_NE(load("test/html/layout_font_spacing.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; } cases[] = {
        {"sum", 100.0f}, {"product", 80.0f}, {"length", 100.0f},
        {"percent", 80.0f}, {"affine", 80.0f},
        {"inherited-percent", 80.0f}, {"inherited-affine", 150.0f},
        {"inherited-length", 140.0f}, {"explicit-inherit", 160.0f},
        {"normal", 60.0f}, {"zero", 60.0f}, {"negative", 55.0f},
        {"inherited-letters", 110.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        ASSERT_NE(element->first_child, nullptr) << entry.id;
        ASSERT_TRUE(element->first_child->is_text()) << entry.id;
        TextRect* rect = element->first_child->as_text()->rect;
        ASSERT_NE(rect, nullptr) << entry.id;
        EXPECT_FLOAT_EQ(rect->width, entry.width) << entry.id;
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

TEST_F(RadiantTextQuery, HyphenLookaheadUsesOnlyFittingBreakOpportunities) {
    ASSERT_NE(load("test/html/layout_hyphen_lookahead_fit.html"), nullptr);
    process_document_font_faces(&ui, document);
    struct Sample { const char* id; float height, head_top, tail_top; } samples[] = {
        {"split-110", 40, 25, 25}, {"split-119", 40, 25, 25},
        {"split-120", 40, 25, 25}, {"split-129", 40, 25, 25},
        {"split-130", 40, 5, 5}, {"split-131", 40, 5, 5}, {"split-140", 40, 5, 5},
        {"bare", 40, -1, -1}, {"opening", 40, 25, 25}, {"nested", 40, 25, 25},
        {"space", 40, 5, 25}, {"break-all", 40, 5, 5}, {"nowrap", 20, 5, 5},
        {"small-prefix", 42, 29, 25}, {"first-hyphen-fits", 40, 5, 5},
        {"second-hyphen", 40, 5, 5}, {"shy-overflow", 40, 25, 25},
        {"shy-fit", 40, 5, 5}, {"shy-empty", 40, 5, 5},
        {"shy-double", 40, 25, 25}, {"shy-double-fit", 40, 5, 5},
        {"shy-none", 40, 25, 25}, {"literal-none", 40, 25, 25},
        {"shy-inline-double", 40, 25, 25}, {"shy-inline-empty", 40, 5, 5},
        {"shy-inline-none", 40, 25, 25},
    };
    auto expect_top = [&](DomElement* owner, const char* id, const char* suffix, float expected) {
        char span_id[64]; str_fmt(span_id, sizeof(span_id), "%s-%s", id, suffix);
        DomElement* span = dom_find_element_by_id(document->root->as_element(), span_id);
        ASSERT_NE(span, nullptr);
        float owner_y = 0, span_y = 0;
        view_get_visual_bounds(owner, nullptr, &owner_y, nullptr, nullptr);
        view_get_visual_bounds(span, nullptr, &span_y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(span_y - owner_y, expected) << span_id;
    };
    for (float scale : {1.0f, 2.0f}) {
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = ui.window_width = 1200;
        ui.viewport_height = ui.window_height = 2000;
        for (int pass = 0; pass < 2; pass++) {
            layout_html_doc(&ui, document, pass != 0);
            for (const Sample& sample : samples) {
                DomElement* owner = dom_find_element_by_id(document->root->as_element(), sample.id);
                ASSERT_NE(owner, nullptr);
                EXPECT_FLOAT_EQ(owner->height, sample.height) << sample.id;
                if (sample.head_top >= 0) {
                    expect_top(owner, sample.id, "head", sample.head_top);
                    expect_top(owner, sample.id, "tail", sample.tail_top);
                }
            }
            DomElement* width = dom_find_element_by_id(document->root->as_element(), "split-120");
            ASSERT_NE(width, nullptr);
            // crossing the first fitting hyphen changes the whole joined prefix's line.
            for (float available : {130.0f, 129.0f, 120.0f}) {
                char style[40]; str_fmt(style, sizeof(style), "width:%.0fpx", available);
                ASSERT_TRUE(width->set_attribute("style", style));
                layout_html_doc(&ui, document, true);
                expect_top(width, "split-120", "head", available >= 130.0f ? 5.0f : 25.0f);
            }
            DomElement* marked = dom_find_element_by_id(document->root->as_element(), "shy-inline-double");
            DomElement* marker = dom_find_element_by_id(document->root->as_element(), "shy-inline-double-tail");
            ASSERT_NE(marked, nullptr); ASSERT_NE(marker, nullptr);
            // retained lookahead must see the following owner's replacement marker.
            ASSERT_TRUE(marker->set_attribute("style", "hyphenate-character:\"\""));
            layout_html_doc(&ui, document, true);
            expect_top(marked, "shy-inline-double", "head", 5);
            ASSERT_TRUE(marker->set_attribute("style", "hyphenate-character:\"xx\""));
            layout_html_doc(&ui, document, true);
            expect_top(marked, "shy-inline-double", "head", 25);
        }
    }
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

TEST_F(RadiantTextQuery, PunctuationSpaceUsesTheSelectedPeriodAcrossLayoutAndPaint) {
    ASSERT_NE(load("test/html/layout_punctuation_space_metric.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; float height; float first_width; } cases[] = {
        {"wrapped", 20.0f, 20.0f, 20.0f},
        {"unbreakable-prefix", 20.0f, 20.0f, 30.0f},
        {"intrinsic", 20.0f, 20.0f, 20.0f},
        {"inline", 20.0f, 20.0f, 10.0f},
        {"rtl", 20.0f, 20.0f, 20.0f},
        {"wide", 50.0f, 10.0f, 30.0f},
        {"decorated", 50.0f, 10.0f, 30.0f},
        {"clipped", 25.0f, 10.0f, 30.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
        DomText* text = element->first_child->as_text();
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->width, entry.first_width) << entry.id;
    }
    DomElement* wide = dom_find_element_by_id(document->root->as_element(), "wide");
    ASSERT_NE(wide, nullptr);
    DomText* text = wide->first_child->as_text();
    ASSERT_NE(text, nullptr);
    FontBox font = {lam::up(text->font.get()), 10.0f};
    EXPECT_TRUE(layout_unicode_space_is_synthesized(font_box_handle(&font), 0x2008));
    float advance = 0.0f;
    ASSERT_TRUE(render_text_synthesized_space_advance(&font, 0x2008, &advance));
    EXPECT_FLOAT_EQ(advance, 10.0f);
    struct GlyphCapture { int count; float positions[2]; } capture = {};
    PaintGlyphRun run = {};
    run.font = lam::up(&font);
    run.text = lam::up("x\xE2\x80\x88y");
    run.text_len = 5;
    auto visit = [](void* pointer, uint32_t, FontHandle*, float x, float, float) -> bool {
        GlyphCapture* glyphs = (GlyphCapture*)pointer;
        if (glyphs->count >= 2) return false;
        glyphs->positions[glyphs->count++] = x;
        return true;
    };
    EXPECT_TRUE(render_visit_text_run_glyphs(&run, ui.font_ctx, visit, &capture));
    EXPECT_EQ(capture.count, 2);
    EXPECT_FLOAT_EQ(capture.positions[0], 0.0f);
    EXPECT_FLOAT_EQ(capture.positions[1], 20.0f);

    DomElement* clipped = dom_find_element_by_id(document->root->as_element(), "clipped");
    ASSERT_NE(clipped, nullptr);
    RenderContext context = {};
    context.ui_context = lam::up(&ui);
    setup_font(&ui, &context.font, clipped->font);
    RenderTextPaintScope paint(&context, lam::view_as_block(clipped),
        &context.font, &context.color);
    RenderTextPaintIterator iterator(&context, lam::view_require_text(clipped->first_child));
    TextRect* visible = iterator.next();
    ASSERT_NE(visible, nullptr);
    lam::Temp<char> content(render_text_create_export_segment(
        clipped->first_child->text_data(), visible, CSS_VALUE_NONE, true, CSS_VALUE_NORMAL));
    EXPECT_STREQ(content.get(), "x…");
}

TEST_F(RadiantTextQuery, TerminalSpacesCollapseBeforeEmptyInlineDecorations) {
    ASSERT_NE(load("test/html/layout_terminal_space_decoration.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 300;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float width; } cases[] = {
        {"terminal-edge", 10.0f, 30.0f}, {"terminal-outer", 0.0f, 40.0f},
        {"followed-edge", 20.0f, 30.0f}, {"preserved-edge", 20.0f, 40.0f},
        {"inner-space-edge", 10.0f, 30.0f}, {"nested-edge", 10.0f, 30.0f},
        {"nested-outer", 10.0f, 30.0f}, {"forced-edge", 10.0f, 30.0f},
        {"before-block-edge", 10.0f, 30.0f}, {"out-of-flow-edge", 10.0f, 30.0f},
        {"anchor", 40.0f, 10.0f}, {"contents-edge", 20.0f, 30.0f},
        {"contents-terminal-edge", 10.0f, 30.0f}, {"pre-line-edge", 10.0f, 30.0f},
        {"generated-edge", 20.0f, 30.0f}, {"generated-after-edge", 20.0f, 30.0f},
        {"contents-ancestor-edge", 20.0f, 30.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float x = 0.0f;
        view_get_visual_bounds(element, &x, nullptr, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
    }
    DomElement* annotation = dom_find_element_by_id(document->root->as_element(), "ruby-annotation");
    ASSERT_NE(annotation, nullptr);
    EXPECT_FLOAT_EQ(annotation->width, 10.0f);
    EXPECT_FLOAT_EQ(annotation->height, 5.0f);
}

TEST_F(RadiantTextQuery, TextPreflightUsesItsActiveFloatContext) {
    ASSERT_NE(load("test/html/layout_float_line_context.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 600;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float continuation_x; } cases[] = {
        {"flat25", 0.0f}, {"flat35", 0.0f}, {"flat45", 20.0f},
        {"inline", 0.0f}, {"same-line", 0.0f}, {"isolated", 20.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        EXPECT_FLOAT_EQ(element->height, 60.0f) << entry.id;
        DomNode* suffix = element->last_child;
        if (suffix && suffix->is_element()) suffix = suffix->as_element()->first_child;
        ASSERT_NE(suffix, nullptr);
        ViewText* text = lam::view_require_text(suffix);
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->x, 20.0f) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->y, 25.0f) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->width, 50.0f) << entry.id;
        ASSERT_NE(text->rect->next, nullptr);
        EXPECT_FLOAT_EQ(text->rect->next->x, entry.continuation_x) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->next->y, 45.0f) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->next->width, 50.0f) << entry.id;
    }
    DomElement* isolated = dom_find_element_by_id(document->root->as_element(), "isolated");
    ASSERT_NE(isolated, nullptr);
    DomElement* floating = isolated->first_child->as_element();
    ASSERT_NE(floating, nullptr);
    ViewText* inside = lam::view_require_text(floating->first_child);
    ASSERT_NE(inside, nullptr);
    ASSERT_NE(inside->rect, nullptr);
    EXPECT_FLOAT_EQ(inside->rect->x, 0.0f);
    ASSERT_NE(inside->rect->next, nullptr);
    EXPECT_FLOAT_EQ(inside->rect->next->x, 0.0f);
}

TEST_F(RadiantTextQuery, MixedSpaceStylesRetainTheirHangingPolicy) {
    ASSERT_NE(load("test/html/layout_mixed_hanging_spaces.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 1000;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float width; } cases[] = {
        {"one-leading", 25.0f, 75.0f}, {"one-fixed", 100.0f, 25.0f},
        {"two-leading", 25.0f, 75.0f}, {"double-leading", 25.0f, 50.0f},
        {"double-fixed", 75.0f, 50.0f}, {"ideographic-leading", 50.0f, 50.0f},
        {"ideographic-fixed", 100.0f, 25.0f}, {"following-hanging", 125.0f, 25.0f},
        {"forced-leading", 25.0f, 75.0f}, {"conditional4-leading", 0.0f, 150.0f},
        {"conditional3-leading", 25.0f, 125.0f}, {"conditional2-leading", 50.0f, 100.0f},
        {"conditional1-leading", 0.0f, 75.0f}, {"conditional3-suffix", 150.0f, 50.0f},
        {"conditional1-suffix", 75.0f, 50.0f}, {"rtl-leading", 50.0f, 75.0f},
        {"rtl-fixed", 25.0f, 25.0f}, {"alternating-visible", 100.0f, 25.0f},
        {"alternating-conditional", 125.0f, 50.0f}, {"alternating-hanging", 175.0f, 25.0f},
        {"alternating-prefix-visible", 100.0f, 50.0f}, {"alternating-prefix-conditional", 150.0f, 25.0f},
        {"pre-fixed-leading", 50.0f, 50.0f}, {"pre-fixed-suffix", 100.0f, 25.0f},
        {"pre-ideographic-leading", 50.0f, 50.0f}, {"pre-ideographic-suffix", 100.0f, 25.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float x = 0.0f;
        view_get_visual_bounds(element, &x, nullptr, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
    }
    struct TrimCase { const char* id; float trim; } trim_cases[] = {
        {"conditional3-suffix", 50.0f}, {"conditional1-suffix", 25.0f},
        {"alternating-conditional", 50.0f},
    };
    for (const TrimCase& entry : trim_cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        ViewText* text = lam::view_require_text(element->first_child);
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->hanging_trim, entry.trim) << entry.id;
    }
}

TEST_F(RadiantTextQuery, RtlAtomicReorderingPreservesThePhysicalLineOrigin) {
    ASSERT_NE(load("test/html/layout_bidi_atomic_origin.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float width; } cases[] = {
        {"text-first", 100.0f, 150.0f}, {"text-first-atomic", 100.0f, 50.0f},
        {"atomic-first", 100.0f, 150.0f}, {"atomic-first-atomic", 200.0f, 50.0f},
        {"decorated", 76.0f, 174.0f}, {"decorated-atomic", 88.0f, 50.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float x = 0.0f;
        view_get_visual_bounds(element, &x, nullptr, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
    }
}

TEST_F(RadiantTextQuery, OneColumnAutoHeightIncludesItsVisibleInFlowOverflow) {
    ASSERT_NE(load("test/html/layout_single_column_overflow.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 600;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float y; float height; } cases[] = {
        {"balanced", 0.0f, 100.0f}, {"sequential", 110.0f, 100.0f},
        {"fixed", 220.0f, 50.0f}, {"clipped", 280.0f, 50.0f},
        {"positioned", 340.0f, 50.0f}, {"following", 400.0f, 100.0f},
        {"collapsed", 510.0f, 40.0f}, {"nested", 560.0f, 130.0f},
        {"nested-clipped", 700.0f, 50.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float y = 0.0f;
        view_get_visual_bounds(element, nullptr, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, BalancedColumnsKeepSingleLineItemsWholeBeforeSpanners) {
    ASSERT_NE(load("test/html/layout_multicol_line_floor.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 240;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"plain-one", 0.0f, 0.0f, 60.0f, 20.0f},
        {"plain-two", 70.0f, 0.0f, 60.0f, 20.0f},
        {"plain-after", 0.0f, 30.0f, 60.0f, 20.0f},
        {"button-one", 0.0f, 70.0f, 60.0f, 20.0f},
        {"button-two", 70.0f, 70.0f, 60.0f, 20.0f},
        {"button-after", 0.0f, 100.0f, 60.0f, 20.0f},
        {"margin-one", 0.0f, 145.0f, 60.0f, 20.0f},
        {"margin-two", 70.0f, 140.0f, 60.0f, 20.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        float x = 0.0f, y = 0.0f;
        view_get_visual_bounds(element, &x, &y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(x, entry.x) << entry.id;
        EXPECT_FLOAT_EQ(y, entry.y) << entry.id;
        EXPECT_FLOAT_EQ(element->width, entry.width) << entry.id;
        EXPECT_FLOAT_EQ(element->height, entry.height) << entry.id;
    }
}

TEST_F(RadiantTextQuery, InterCharacterJustificationUsesTypographicUnits) {
    ASSERT_NE(load("test/html/layout_inter_character_justify.html"), nullptr);
    ui.viewport_width = ui.window_width = 320;
    ui.viewport_height = ui.window_height = 400;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float width; int gaps; float spacing; } cases[] = {
        {"latin", 120.0f, 2, 30.0f}, {"spaces", 120.0f, 2, 30.0f},
        {"punctuation", 120.0f, 2, 30.0f}, {"nbsp", 120.0f, 2, 30.0f},
        {"zero", 120.0f, 1, 80.0f}, {"combine", 120.0f, 1, 80.0f},
        {"word", 60.0f, 0, 0.0f}, {"auto", 60.0f, 0, 0.0f},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr);
        ViewText* text = lam::view_require_text(element->first_child);
        ASSERT_NE(text, nullptr); ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->width, entry.width) << entry.id;
        EXPECT_EQ(count_rendered_justify_opportunities(text, text->rect, true), entry.gaps) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->justification_spacing, entry.spacing) << entry.id;
        CssEnum mode = text_justify_computed_value(element);
        RenderTextJustification paint = render_text_justification(text->rect, mode);
        EXPECT_FLOAT_EQ(paint.character, entry.spacing) << entry.id;
        EXPECT_FLOAT_EQ(paint.word, 0.0f) << entry.id;
    }
    TextJustifyContext cursive = {};
    EXPECT_FALSE(text_justify_character_gap(&cursive, 0x0645, CSS_VALUE_INTER_CHARACTER));
    EXPECT_FALSE(text_justify_character_gap(&cursive, 0x0645, CSS_VALUE_INTER_CHARACTER));
    EXPECT_TRUE(text_justify_character_gap(&cursive, ' ', CSS_VALUE_INTER_CHARACTER));
    EXPECT_TRUE(text_justify_character_gap(&cursive, 0x0645, CSS_VALUE_INTER_CHARACTER));
}

TEST_F(RadiantTextQuery, NestedBlockColumnsConsumeOuterCapacityOncePerSet) {
    ASSERT_NE(load("test/html/layout_nested_block_column_slots.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 600;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"half-inner", 0.0f, 0.0f, 400.0f, 100.0f},
        {"half-flow", 0.0f, 0.0f, 350.0f, 100.0f},
        {"half-prefix", 0.0f, 0.0f, 200.0f, 100.0f},
        {"half-absolute", 150.0f, 0.0f, 180.0f, 100.0f},
        {"full-inner", 0.0f, 120.0f, 250.0f, 100.0f},
        {"full-flow", 0.0f, 120.0f, 250.0f, 100.0f},
        {"short-inner", 0.0f, 265.0f, 100.0f, 75.0f},
        {"short-flow", 0.0f, 265.0f, 100.0f, 75.0f},
    };
    for (const Case& entry : cases) {
        expect_box(entry.id, entry.x, entry.y, entry.width, entry.height);
    }
}

TEST_F(RadiantTextQuery, AutoHeightWrappersKeepMonolithicOverflowSeparateFromFlow) {
    ASSERT_NE(load("test/html/layout_monolithic_wrapper_fragments.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 1800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"mixed", 0.0f, 0.0f, 320.0f, 250.0f},
        {"one", 0.0f, 280.0f, 100.0f, 250.0f},
        {"fitting", 0.0f, 560.0f, 100.0f, 100.0f},
        {"authored", 0.0f, 840.0f, 320.0f, 100.0f},
        {"padding", 0.0f, 1120.0f, 100.0f, 100.0f},
        {"before-tail", 0.0f, 1400.0f, 210.0f, 250.0f},
        {"tail", 110.0f, 1450.0f, 100.0f, 10.0f},
    };
    for (const Case& entry : cases) {
        expect_box(entry.id, entry.x, entry.y, entry.width, entry.height);
    }
    DomElement* mixed = dom_find_element_by_id(document->root->as_element(), "mixed");
    ASSERT_EQ(mixed->layout_fragments_count(), 3);
    const float heights[] = {250.0f, 100.0f, 250.0f};
    size_t index = 0;
    for (LayoutFragmentBox* piece = mixed->layout_fragment_list(); piece; piece = piece->next, index++) {
        ASSERT_LT(index, sizeof(heights) / sizeof(heights[0]));
        EXPECT_FLOAT_EQ(piece->height, heights[index]);
        EXPECT_TRUE(piece->has_flow_extent);
        EXPECT_FLOAT_EQ(piece->flow_extent, 100.0f);
    }
}

TEST_F(RadiantTextQuery, NestedVerticalColumnsShareLogicalSlotsAndPublishPhysicalPieces) {
    ASSERT_NE(load("test/html/layout_vertical_nested_column_slots.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float width; float height; } cases[] = {
        {"vertical-rl-50-inner", 0.0f, 100.0f, 400.0f},
        {"vertical-rl-50-flow", 0.0f, 100.0f, 350.0f},
        {"vertical-rl-50-prefix", 0.0f, 100.0f, 200.0f},
        {"vertical-lr-50-inner", 120.0f, 100.0f, 400.0f},
        {"vertical-lr-50-flow", 120.0f, 100.0f, 350.0f},
        {"vertical-lr-50-prefix", 120.0f, 100.0f, 200.0f},
        {"vertical-rl-0-inner", 240.0f, 100.0f, 250.0f},
        {"vertical-rl-0-flow", 240.0f, 100.0f, 250.0f},
        {"vertical-lr-0-inner", 360.0f, 100.0f, 250.0f},
        {"vertical-lr-0-flow", 360.0f, 100.0f, 250.0f},
        {"vertical-rl-25-inner", 480.0f, 75.0f, 100.0f},
        {"vertical-rl-25-flow", 480.0f, 75.0f, 100.0f},
        {"vertical-rl-25-prefix", 495.0f, 60.0f, 50.0f},
        {"vertical-lr-25-inner", 625.0f, 75.0f, 100.0f},
        {"vertical-lr-25-flow", 625.0f, 75.0f, 100.0f},
        {"vertical-lr-25-prefix", 625.0f, 60.0f, 50.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, entry.x, 0.0f, entry.width, entry.height);
    expect_box("vertical-rl-50-absolute", 0.0f, 150.0f, 100.0f, 180.0f);
    const Rect pieces[] = {
        {0.0f, 0.0f, 50.0f, 50.0f}, {0.0f, 50.0f, 50.0f, 50.0f},
        {0.0f, 150.0f, 100.0f, 50.0f}, {0.0f, 200.0f, 100.0f, 50.0f},
        {50.0f, 300.0f, 50.0f, 50.0f},
    };
    expect_fragments("vertical-rl-50-flow", pieces, sizeof(pieces) / sizeof(pieces[0]));
    // positioned fragments currently retain containing-block coordinates.
    const Rect absolute[] = {
        {0.0f, 150.0f, 40.0f, 30.0f}, {0.0f, 200.0f, 100.0f, 30.0f},
        {70.0f, 300.0f, 30.0f, 30.0f},
    };
    expect_fragments("vertical-rl-50-absolute", absolute, sizeof(absolute) / sizeof(absolute[0]));
    DomElement* flow = dom_find_element_by_id(document->root->as_element(), "vertical-rl-50-flow");
    ASSERT_NE(flow, nullptr);
    for (LayoutFragmentBox* piece = flow->layout_fragment_list(); piece; piece = piece->next) {
        EXPECT_TRUE(piece->has_flow_extent);
        EXPECT_FLOAT_EQ(piece->flow_extent, piece->width);
    }
}

TEST_F(RadiantTextQuery, RelativeColumnInsetsMovePiecesWithoutMovingFollowingFlow) {
    ASSERT_NE(load("test/html/layout_multicol_relative_offsets.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; } cases[] = {
        {"plain", 0.0f, 0.0f}, {"shift", 15.0f, 100.0f},
        {"end", -10.0f, 145.0f}, {"percentage", 10.0f, 260.0f},
    };
    const Rect pieces[] = {
        {0.0f, 0.0f, 100.0f, 40.0f}, {110.0f, 0.0f, 100.0f, 40.0f},
        {220.0f, 0.0f, 100.0f, 40.0f},
    };
    for (const Case& entry : cases) {
        expect_box(entry.id, entry.x, entry.y, 320.0f, 40.0f);
        expect_fragments(entry.id, pieces, sizeof(pieces) / sizeof(pieces[0]));
        DomElement* wrapper = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(wrapper, nullptr);
        ViewText* text = lam::view_as<RDT_VIEW_TEXT>(lam::view_require_block(wrapper)->first_placed_child());
        ASSERT_NE(text, nullptr);
        ASSERT_NE(text->rect, nullptr);
        EXPECT_FLOAT_EQ(text->rect->x, 0.0f) << entry.id;
        EXPECT_FLOAT_EQ(text->rect->y, 0.0f) << entry.id;
    }
    expect_box("plain-tail", 330.0f, 0.0f, 100.0f, 10.0f);
    expect_box("shift-tail", 330.0f, 80.0f, 100.0f, 10.0f);
    expect_box("end-tail", 330.0f, 160.0f, 100.0f, 10.0f);
    expect_box("percentage-tail", 330.0f, 240.0f, 100.0f, 10.0f);
}

TEST_F(RadiantTextQuery, ClearedFloatRowsDoNotAddTheirIntrinsicWidthsTogether) {
    ASSERT_NE(load("test/html/layout_float_clear_intrinsic.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float width; float height; } cases[] = {
        {"left", 0.0f, 106.0f, 66.0f}, {"right", 126.0f, 106.0f, 66.0f},
        {"both", 252.0f, 106.0f, 66.0f}, {"none", 378.0f, 246.0f, 26.0f},
        {"irrelevant", 644.0f, 246.0f, 26.0f}, {"mixed", 910.0f, 246.0f, 26.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, entry.x, 0.0f, entry.width, entry.height);
    DomElement* wrapper = dom_find_element_by_id(document->root->as_element(), "result");
    ASSERT_NE(wrapper, nullptr);
    EXPECT_FLOAT_EQ(wrapper->height, 0.0f);
    float min_y = 0.0f, max_y = 0.0f;
    layout_in_flow_content_bounds(lam::view_require_element(wrapper), LAYOUT_AXIS_Y,
        false, &min_y, &max_y);
    EXPECT_FLOAT_EQ(max_y, 0.0f);
    layout_in_flow_content_bounds(lam::view_require_element(wrapper), LAYOUT_AXIS_Y,
        true, &min_y, &max_y);
    EXPECT_FLOAT_EQ(max_y, 66.0f);
}

TEST_F(RadiantTextQuery, OutOfFlowBlockificationRetainsInlineSeparators) {
    ASSERT_NE(load("test/html/layout_oof_inline_separators.html"), nullptr);
    layout_html_doc(&ui, document, false);
    struct Case { const char* lead; const char* tail; float gap; } cases[] = {
        {"after-lead", "after-tail", 12.0f},
        {"before-lead", "before-tail", 12.0f},
        {"both-lead", "both-tail", 12.0f},
        {"fixed-lead", "fixed-tail", 12.0f},
        {"blockified-lead", "blockified-tail", 12.0f},
        {"float-lead", "float-tail", 12.0f},
        {"hidden-lead", "hidden-tail", 12.0f},
        {"none-lead", "none-tail", 0.0f},
    };
    for (const Case& entry : cases) {
        DomElement* lead = dom_find_element_by_id(document->root->as_element(), entry.lead);
        DomElement* tail = dom_find_element_by_id(document->root->as_element(), entry.tail);
        ASSERT_NE(lead, nullptr) << entry.lead;
        ASSERT_NE(tail, nullptr) << entry.tail;
        float lead_x = 0.0f, lead_y = 0.0f, tail_x = 0.0f, tail_y = 0.0f;
        view_get_visual_bounds(lead, &lead_x, &lead_y, nullptr, nullptr);
        view_get_visual_bounds(tail, &tail_x, &tail_y, nullptr, nullptr);
        EXPECT_NEAR(tail_x - lead_x - lead->width, entry.gap, 1.0f / 64.0f) << entry.tail;
        EXPECT_FLOAT_EQ(tail_y, lead_y) << entry.tail;
        if (entry.gap > 0.0f) EXPECT_FLOAT_EQ(tail->width, lead->width) << entry.tail;
    }
    DomElement* lead = dom_find_element_by_id(document->root->as_element(), "real-lead");
    DomElement* tail = dom_find_element_by_id(document->root->as_element(), "real-tail");
    ASSERT_NE(lead, nullptr);
    ASSERT_NE(tail, nullptr);
    EXPECT_FLOAT_EQ(tail->y - lead->y, 34.0f);
}

TEST_F(RadiantTextQuery, NestedInlineColumnsFitTheRemainingOuterSpace) {
    ASSERT_NE(load("test/html/layout_nested_inline_remaining_columns.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"quarter-inner", 0.0f, 75.0f, 50.0f, 25.0f},
        {"quarter-first", 0.0f, 75.0f, 25.0f, 25.0f},
        {"quarter-second", 25.0f, 75.0f, 25.0f, 25.0f},
        {"quarter-tail", 50.0f, 0.0f, 50.0f, 100.0f},
        {"remainder-inner", 0.0f, 130.0f, 50.0f, 90.0f},
        {"remainder-first", 0.0f, 130.0f, 25.0f, 50.0f},
        {"remainder-second", 25.0f, 130.0f, 25.0f, 50.0f},
        {"remainder-tail", 50.0f, 120.0f, 50.0f, 100.0f},
        {"fitting-inner", 0.0f, 290.0f, 50.0f, 50.0f},
        {"fitting-first", 0.0f, 290.0f, 25.0f, 25.0f},
        {"fitting-second", 0.0f, 315.0f, 25.0f, 25.0f},
        {"fitting-tail", 50.0f, 240.0f, 50.0f, 100.0f},
        {"fixed-inner", 0.0f, 385.0f, 50.0f, 50.0f},
        {"fixed-first", 0.0f, 385.0f, 25.0f, 25.0f},
        {"fixed-second", 0.0f, 410.0f, 25.0f, 25.0f},
        {"fixed-tail", 50.0f, 360.0f, 50.0f, 100.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, entry.x, entry.y, entry.width, entry.height);
}

TEST_F(RadiantTextQuery, SpannersCloseThePrecedingColumnFloatContext) {
    ASSERT_NE(load("test/html/layout_single_column_spanner_floats.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float y; float height; } cases[] = {
        {"left", 0.0f, 90.0f}, {"right", 110.0f, 110.0f},
        {"parallel", 240.0f, 110.0f}, {"margins", 370.0f, 109.0f},
        {"later", 499.0f, 110.0f}, {"none", 629.0f, 50.0f},
        {"left-span", 60.0f, 10.0f}, {"left-tail", 70.0f, 20.0f},
        {"right-span", 190.0f, 10.0f}, {"right-tail", 200.0f, 20.0f},
        {"parallel-span", 320.0f, 10.0f}, {"parallel-tail", 330.0f, 20.0f},
        {"margins-span", 445.0f, 10.0f}, {"margins-tail", 459.0f, 20.0f},
        {"later-span", 559.0f, 10.0f}, {"later-tail", 569.0f, 20.0f},
        {"none-span", 649.0f, 10.0f}, {"none-tail", 659.0f, 20.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, 0.0f, entry.y, 100.0f, entry.height);
    expect_box("later-float", 0.0f, 569.0f, 30.0f, 40.0f);
}

TEST_F(RadiantTextQuery, ZeroUsedFlexWidthsStayDefinite) {
    ASSERT_NE(load("test/html/layout_zero_width_flex_lines.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float y; float width; float height; } cases[] = {
        {"auto", 0.0f, 0.0f, 20.0f}, {"explicit", 40.0f, 0.0f, 20.0f},
        {"padding", 80.0f, 14.0f, 34.0f}, {"column", 134.0f, 0.0f, 20.0f},
        {"reverse", 174.0f, 0.0f, 20.0f}, {"empty", 214.0f, 0.0f, 0.0f},
        {"intrinsic", 234.0f, 20.0f, 20.0f}, {"ordinary", 274.0f, 80.0f, 20.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, 0.0f, entry.y, entry.width, entry.height);
}

TEST_F(RadiantTextQuery, QuotationFontStyleUsesInheritanceAndAuthorRules) {
    ASSERT_NE(load("test/html/layout_q_font_inheritance.html"), nullptr);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; CssEnum style; } cases[] = {
        {"plain", CSS_VALUE_NORMAL}, {"inherited", CSS_VALUE_ITALIC},
        {"author", CSS_VALUE_ITALIC}, {"normal", CSS_VALUE_NORMAL},
        {"cite", CSS_VALUE_ITALIC}, {"dfn", CSS_VALUE_ITALIC}, {"var", CSS_VALUE_ITALIC},
    };
    for (const Case& entry : cases) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(element, nullptr) << entry.id;
        ASSERT_NE(element->font, nullptr) << entry.id;
        EXPECT_EQ(element->font->font_style, entry.style) << entry.id;
    }
}

TEST_F(RadiantTextQuery, AvoidFloorsRespectDefiniteCapacityAndPrincipalMinimums) {
    ASSERT_NE(load("test/html/layout_avoid_fragmentainer_limits.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 1200;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"fixed-tall", 25.0f, 0.0f, 50.0f, 100.0f},
        {"minimum-prefix", 0.0f, 120.0f, 50.0f, 100.0f},
        {"minimum-leaf", 0.0f, 120.0f, 37.5f, 100.0f},
        {"minimum-short", 25.0f, 145.0f, 25.0f, 75.0f},
        {"minimum-tall", 50.0f, 120.0f, 50.0f, 100.0f},
        {"balanced-tall", 25.0f, 240.0f, 50.0f, 100.0f},
        {"maximum-tall", 25.0f, 360.0f, 50.0f, 100.0f},
        {"unconstrained", 0.0f, 480.0f, 100.0f, 200.0f},
        {"unconstrained-tall", 25.0f, 480.0f, 25.0f, 200.0f},
        {"column-tall", 25.0f, 700.0f, 50.0f, 100.0f},
        {"large-minimum-prefix", 0.0f, 820.0f, 100.0f, 100.0f},
        {"large-minimum-leaf", 0.0f, 820.0f, 37.5f, 100.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, entry.x, entry.y, entry.width, entry.height);
    const Rect tall[] = {{0.0f, 0.0f, 25.0f, 100.0f}, {25.0f, 0.0f, 25.0f, 100.0f}};
    const char* split[] = {"fixed-tall", "minimum-tall", "balanced-tall", "maximum-tall", "column-tall"};
    for (const char* id : split) expect_fragments(id, tall, 2);
    const Rect minimum[] = {{0.0f, 0.0f, 25.0f, 100.0f}, {25.0f, 0.0f, 25.0f, 25.0f}};
    expect_fragments("minimum-prefix", minimum, 2);
    const Rect large_minimum[] = {{0.0f, 0.0f, 25.0f, 100.0f}, {25.0f, 0.0f, 25.0f, 100.0f},
        {50.0f, 0.0f, 25.0f, 100.0f}, {75.0f, 0.0f, 25.0f, 25.0f}};
    expect_fragments("large-minimum-prefix", large_minimum, 4);
    const Rect nested_content[] = {{0.0f, 0.0f, 12.5f, 100.0f}, {12.5f, 0.0f, 12.5f, 100.0f},
        {25.0f, 0.0f, 12.5f, 5.0f}};
    expect_fragments("minimum-leaf", nested_content, 3);
    expect_fragments("large-minimum-leaf", nested_content, 3);
}

TEST_F(RadiantTextQuery, NestedContinuousFlowUsesBalancedRowCapacities) {
    ASSERT_NE(load("test/html/layout_nested_balanced_rows.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    layout_html_doc(&ui, document, false);
    struct Case {
        const char* inner;
        const char* leaf;
        float y;
        float height;
        float last_row_height;
        int inner_columns;
        int last_row_columns;
    } cases[] = {
        {"balanced-inner", "balanced-leaf", 0.0f, 100.0f, 30.0f, 2, 2},
        {"short-inner", "short-leaf", 120.0f, 80.0f, 10.0f, 2, 2},
        {"three-inner", "three-leaf", 240.0f, 90.0f, 20.0f, 3, 2},
        {"automatic-inner", "automatic-leaf", 360.0f, 130.0f, 60.0f, 2, 1},
    };
    for (const Case& entry : cases) {
        expect_box(entry.inner, 0.0f, entry.y, 100.0f, entry.height);
        expect_box(entry.leaf, 0.0f, entry.y, 100.0f, entry.height);
        Rect pieces[12] = {};
        float piece_width = 50.0f / entry.inner_columns;
        int count = 0;
        for (int row = 0; row < 2; row++) {
            int columns = (row == 0 ? 2 : entry.last_row_columns) * entry.inner_columns;
            for (int column = 0; column < columns; column++) {
                pieces[count++] = {column * piece_width, row * 70.0f,
                    piece_width, row == 0 ? 60.0f : entry.last_row_height};
            }
        }
        expect_fragments(entry.leaf, pieces, (size_t)count);
    }
    expect_box("ordinary-leaf", 0.0f, 480.0f, 100.0f, 100.0f);
    expect_box("spanners", 0.0f, 600.0f, 100.0f, 110.0f);
    const Rect outer[] = {{0.0f, 0.0f, 50.0f, 60.0f}, {50.0f, 0.0f, 50.0f, 60.0f},
        {0.0f, 70.0f, 50.0f, 30.0f}, {50.0f, 70.0f, 50.0f, 30.0f}};
    expect_fragments("balanced-inner", outer, 4);
    expect_fragments("ordinary-leaf", outer, 4);
}

TEST_F(RadiantTextQuery, NestedAvoidBoxesUseFittingFragmentSlots) {
    ASSERT_NE(load("test/html/layout_nested_avoid_slots.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 1200;
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"avoid-inner", 0.0f, 0.0f, 100.0f, 100.0f},
        {"avoid-first", 0.0f, 50.0f, 50.0f, 50.0f},
        {"avoid-last", 50.0f, 0.0f, 50.0f, 100.0f},
        {"column-last", 50.0f, 120.0f, 50.0f, 100.0f},
        {"auto-last", 25.0f, 240.0f, 75.0f, 100.0f},
        {"oversize-last", 50.0f, 360.0f, 75.0f, 100.0f},
        {"page-last", 25.0f, 480.0f, 75.0f, 100.0f},
        {"lr-first", 50.0f, 600.0f, 50.0f, 50.0f},
        {"lr-last", 0.0f, 650.0f, 100.0f, 50.0f},
        {"rl-first", 0.0f, 720.0f, 50.0f, 50.0f},
        {"rl-last", 0.0f, 770.0f, 100.0f, 50.0f},
        {"content-last", 50.0f, 840.0f, 50.0f, 100.0f},
        {"content-child", 50.0f, 840.0f, 20.0f, 20.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, entry.x, entry.y, entry.width, entry.height);
    const Rect wrapper[] = {{0.0f, 50.0f, 50.0f, 50.0f}, {50.0f, 0.0f, 50.0f, 100.0f}};
    expect_fragments("avoid-inner", wrapper, 2);
    const Rect split[] = {{0.0f, 50.0f, 50.0f, 50.0f}, {25.0f, 0.0f, 50.0f, 50.0f}};
    expect_fragments("auto-last", split, 2);
    expect_fragments("page-last", split, 2);
    const Rect oversized[] = {{0.0f, 0.0f, 50.0f, 100.0f}, {25.0f, 0.0f, 50.0f, 50.0f}};
    expect_fragments("oversize-last", oversized, 2);
    const char* whole[] = {"avoid-last", "column-last", "lr-last", "rl-last", "content-last"};
    for (const char* id : whole) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr) << id;
        EXPECT_LE(element->layout_fragments_count(), 1) << id;
    }
}

TEST_F(RadiantTextQuery, MulticolBaselinesUseCompletedLineFragments) {
    ASSERT_NE(load("test/html/layout_multicol_fragment_baselines.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"short", 0.0f, 20.0f, 90.0f, 20.0f},
        {"tall", 90.0f, 0.0f, 90.0f, 40.0f},
        {"marker", 180.0f, 16.0f, 20.0f, 20.0f},
        {"plain-short", 0.0f, 120.0f, 20.0f, 20.0f},
        {"plain-tall", 20.0f, 100.0f, 20.0f, 40.0f},
        {"plain-marker", 40.0f, 116.0f, 20.0f, 20.0f},
        {"hidden-short", 0.0f, 220.0f, 90.0f, 20.0f},
        {"hidden-tall", 90.0f, 200.0f, 90.0f, 40.0f},
        {"hidden-marker", 180.0f, 220.0f, 20.0f, 20.0f},
        {"first-short", 0.0f, 304.0f, 90.0f, 20.0f},
        {"first-tall", 90.0f, 304.0f, 90.0f, 40.0f},
        {"first-marker", 180.0f, 300.0f, 20.0f, 20.0f},
        {"frag-short", 0.0f, 420.0f, 90.0f, 20.0f},
        {"frag-tall", 90.0f, 400.0f, 90.0f, 40.0f},
        {"frag-marker", 180.0f, 416.0f, 20.0f, 20.0f},
        {"span-short", 0.0f, 540.0f, 90.0f, 20.0f},
        {"span-tall", 90.0f, 500.0f, 90.0f, 60.0f},
        {"span-marker", 180.0f, 536.0f, 20.0f, 20.0f},
        {"unequal-short", 0.0f, 640.0f, 90.0f, 20.0f},
        {"unequal-tall", 90.0f, 600.0f, 90.0f, 60.0f},
        {"unequal-marker", 180.0f, 636.0f, 20.0f, 20.0f},
        {"positioned-short", 0.0f, 720.0f, 90.0f, 20.0f},
        {"positioned-tall", 90.0f, 700.0f, 90.0f, 40.0f},
        {"positioned-marker", 180.0f, 716.0f, 20.0f, 20.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, entry.x, entry.y, entry.width, entry.height);
}

TEST_F(RadiantTextQuery, PhantomInlineCursorsFollowTheirColumnFragments) {
    ASSERT_NE(load("test/html/layout_multicol_phantom_static.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 1200;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; } cases[] = {
        {"center-a", 160.0f, 20.0f}, {"right-a", 210.0f, 80.0f},
        {"left-a", 110.0f, 140.0f}, {"unequal-a", 160.0f, 200.0f},
        {"before-a", 50.0f, 280.0f}, {"spanners-first-a", 160.0f, 360.0f},
        {"spanners-last-a", 160.0f, 420.0f}, {"fixed-a", 160.0f, 480.0f},
        {"relative-a", 50.0f, 540.0f}, {"margins-a", 163.0f, 634.0f},
        {"explicit-x-a", 7.0f, 690.0f}, {"explicit-y-a", 160.0f, 737.0f},
        {"boundary-a", 50.0f, 810.0f}, {"float-a", 20.0f, 850.0f},
        {"ordinary-first-a", 160.0f, 960.0f}, {"ordinary-last-a", 160.0f, 980.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, entry.x, entry.y, 10.0f, 10.0f);
    expect_box("center", 0.0f, 0.0f, 320.0f, 20.0f);
    expect_box("spanners", 0.0f, 340.0f, 320.0f, 80.0f);
    expect_box("ordinary", 0.0f, 940.0f, 320.0f, 40.0f);
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

TEST_F(RadiantTextQuery, MixedColumnFlowRetainsAtomicInlineLines) {
    ASSERT_NE(load("test/html/layout_mixed_atomic_columns.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float y; float height; } cases[] = {
        {"input", 0.0f, 45.0f}, {"inline", 90.0f, 45.0f},
        {"block", 180.0f, 45.0f}, {"only", 270.0f, 25.0f},
        {"tail", 360.0f, 45.0f}, {"baseline", 450.0f, 34.0f},
        {"nested", 540.0f, 45.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, 0.0f, entry.y, 200.0f, entry.height);
    expect_box("input-first", 0.0f, 20.0f, 30.0f, 25.0f);
    expect_box("input-last", 100.0f, 20.0f, 30.0f, 25.0f);
    expect_box("inline-first", 0.0f, 110.0f, 30.0f, 25.0f);
    expect_box("inline-last", 100.0f, 110.0f, 30.0f, 25.0f);
    expect_box("block-first", 0.0f, 200.0f, 30.0f, 25.0f);
    expect_box("block-last", 100.0f, 200.0f, 30.0f, 25.0f);
    expect_box("only-first", 0.0f, 270.0f, 30.0f, 25.0f);
    expect_box("only-avoid", 100.0f, 270.0f, 100.0f, 25.0f);
    expect_box("tail-last", 100.0f, 380.0f, 30.0f, 25.0f);
    expect_box("baseline-first", 20.0f, 450.0f, 30.0f, 30.0f);
    expect_box("baseline-last", 120.0f, 450.0f, 30.0f, 30.0f);
    expect_box("nested-first", 0.0f, 560.0f, 30.0f, 25.0f);
    expect_box("nested-last", 100.0f, 560.0f, 30.0f, 25.0f);
}

TEST_F(RadiantTextQuery, GridSpanningItemsRetainTheirMonolithicFirstLine) {
    ASSERT_NE(load("test/html/layout_grid_single_line_fragments.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case {
        const char* grid; const char* item; float x; float y;
        float width; float height; float pieces[6]; size_t count;
    } cases[] = {
        {"eighteen-grid", "eighteen-item", 0.0f, 0.0f, 216.0f, 18.0f, {18, 10, 10, 2}, 4},
        {"twenty-grid", "twenty-item", 0.0f, 70.0f, 158.0f, 20.0f, {20, 10, 10}, 3},
        {"twenty-four-grid", "twenty-four-item", 0.0f, 140.0f, 158.0f, 24.0f, {24, 10, 6}, 3},
        {"fits-grid", "fits-item", 0.0f, 210.0f, 216.0f, 10.0f, {10, 10, 10, 10}, 4},
        {"sixty-grid", "sixty-item", 0.0f, 280.0f, 332.0f, 18.0f, {18, 10, 10, 10, 10, 2}, 6},
        {"prefix-grid", "prefix-item", 58.0f, 350.0f, 158.0f, 20.0f, {20, 10, 10}, 3},
    };
    for (const Case& entry : cases) {
        expect_box(entry.grid, entry.x, entry.y, entry.width, entry.height);
        expect_box(entry.item, entry.x + 40.0f, entry.y, entry.width - 2.0f, entry.height);
        Rect pieces[6];
        for (size_t index = 0; index < entry.count; index++)
            pieces[index] = {58.0f * (float)index, 0.0f, 42.0f, entry.pieces[index]};
        expect_fragments(entry.grid, pieces, entry.count);
        for (size_t index = 0; index < entry.count; index++) pieces[index].width = 40.0f;
        expect_fragments(entry.item, pieces, entry.count);
    }
}

TEST_F(RadiantTextQuery, SlicedInlineDecorationsFollowSourceEdgeFragments) {
    ASSERT_NE(load("test/html/layout_inline_slice_edges.html"), nullptr);
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    struct Case { const char* id; float x; float y; float width; float height; } cases[] = {
        {"right", 0.0f, 0.0f, 64.0f, 32.0f},
        {"both", 0.0f, 72.0f, 64.0f, 32.0f},
        {"first-wide", 0.0f, 144.0f, 64.0f, 32.0f},
        {"last-wide", 0.0f, 200.0f, 80.0f, 32.0f},
        {"nested", 0.0f, 256.0f, 72.0f, 32.0f},
        {"forced", 0.0f, 312.0f, 64.0f, 32.0f},
        {"rtl", 32.0f, 368.0f, 48.0f, 32.0f},
        {"lookahead", 0.0f, 424.0f, 32.0f, 32.0f},
        {"clone", 0.0f, 496.0f, 96.0f, 48.0f},
        {"relative", 0.0f, 568.0f, 72.0f, 32.0f},
    };
    for (const Case& entry : cases)
        expect_box(entry.id, entry.x, entry.y, entry.width, entry.height);
    DomElement* rtl = dom_find_element_by_id(document->root->as_element(), "rtl");
    ASSERT_NE(rtl, nullptr);
    DomText* text = rtl->first_child ? rtl->first_child->as_text() : nullptr;
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->rect, nullptr);
    ASSERT_NE(text->rect->next, nullptr);
    EXPECT_FLOAT_EQ(text->rect->x, 32.0f);
    EXPECT_FLOAT_EQ(text->rect->next->x, 48.0f);
    DomElement* lookahead = dom_find_element_by_id(document->root->as_element(), "lookahead");
    ASSERT_NE(lookahead, nullptr);
    text = lookahead->first_child ? lookahead->first_child->as_text() : nullptr;
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->rect, nullptr);
    EXPECT_FLOAT_EQ(text->rect->width, 32.0f);
    EXPECT_EQ(text->rect->length, 3);
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

class RadiantFormControls : public RadiantTextQuery {};

TEST_F(RadiantFormControls, ScriptCreatedFormSurvivesRetainedStyleReset) {
    DomElement* owner = load("test/html/select_retained_refresh.html");
    ASSERT_NE(owner, nullptr);
    DomElement* select = dom_find_element_by_id(owner, "select");
    ASSERT_NE(select, nullptr);
    layout_html_doc(&ui, document, false);
    form_control_release_prop(select);
    FormControlProp* form = tc_get_or_create_form(select);
    ASSERT_NE(form, nullptr);
    ASSERT_TRUE(form->heap_allocated);
    view_pool_reset_retained_subtree(document->view_tree, owner);
    EXPECT_EQ(select->form, form);
    layout_html_doc(&ui, document, true);
    EXPECT_EQ(select->form, form);
    form_control_release_prop(select);
    EXPECT_EQ(select->form, nullptr);
}

TEST_F(RadiantFormControls, RetainedStyleResetPreservesOpenSelectUntilControlRelease) {
    DomElement* owner = load("test/html/select_retained_refresh.html");
    ASSERT_NE(owner, nullptr);
    layout_html_doc(&ui, document, false);
    DocState* state = dom_engine_document_ensure_state(document, "select_refresh_test");
    ASSERT_NE(state, nullptr);
    DomElement* select = dom_find_element_by_id(owner, "select");
    ASSERT_NE(select, nullptr);
    FormControlProp* form = select->form;
    ASSERT_NE(form, nullptr);
    doc_state_open_dropdown(state, select);
    form_control_set_hover_index(state, select, 2);
    ASSERT_EQ(state->open_dropdown, select);
    // restyling retains the control; releasing it later must still dismiss its popup.
    view_pool_reset_retained_subtree(document->view_tree, owner);
    layout_html_doc(&ui, document, true);
    EXPECT_EQ(select->form, form);
    EXPECT_EQ(state->open_dropdown, select);
    EXPECT_TRUE(form_control_is_dropdown_open(state, select));
    EXPECT_EQ(form_control_get_selected_index(state, select), 1);
    EXPECT_EQ(form_control_get_hover_index(state, select), 2);
    EXPECT_TRUE(radiant_state_validate_interaction(state, nullptr));
    form_control_release_prop(select);
    EXPECT_EQ(state->open_dropdown, nullptr);
    EXPECT_EQ(select->form, nullptr);
    EXPECT_TRUE(radiant_state_validate_interaction(state, nullptr));
}

class RadiantTextLayout : public RadiantTextQuery {
protected:
    void expect_snapshot_capture(const char* path, float final_width) {
        DomElement* box = load(path, true);
        ASSERT_NE(box, nullptr);
        layout_html_doc(&ui, document, false);
        EXPECT_FALSE(document->root->has_class("reftest-wait"));
        EXPECT_STREQ(box->get_attribute("data-cancelled"), "safe");
        if (final_width > 0.0f) EXPECT_FLOAT_EQ(box->width, final_width);
        else {
            // an ordinary ticker keeps a bounded sample before a full finite chain.
            EXPECT_GT(box->width, 10.0f);
            EXPECT_LT(box->width, 22.0f);
        }
    }
    void expect_inline_strut_pair(const char* actual_id, const char* reference_id,
                                  float height, bool image, float image_y = NAN) {
        SCOPED_TRACE(actual_id);
        DomElement* actual = dom_find_element_by_id(document->root->as_element(), actual_id);
        DomElement* reference = dom_find_element_by_id(document->root->as_element(), reference_id);
        ASSERT_NE(actual, nullptr); ASSERT_NE(reference, nullptr);
        EXPECT_FLOAT_EQ(actual->height, height);
        EXPECT_FLOAT_EQ(reference->height, height);
        if (!image) return;
        DomElement* actual_image = actual->first_child->as_element()->first_child->as_element();
        DomElement* reference_image = reference->first_child->as_element()->first_child->as_element();
        ASSERT_NE(actual_image, nullptr); ASSERT_NE(reference_image, nullptr);
        float actual_y = 0.0f, reference_y = 0.0f;
        view_get_visual_bounds(actual_image, nullptr, &actual_y, nullptr, nullptr);
        view_get_visual_bounds(reference_image, nullptr, &reference_y, nullptr, nullptr);
        EXPECT_FLOAT_EQ(actual_y, reference_y);
        EXPECT_FLOAT_EQ(actual_image->height, 2.0f);
        if (!isnan(image_y)) {
            float block_y = 0.0f;
            view_get_visual_bounds(actual, nullptr, &block_y, nullptr, nullptr);
            EXPECT_FLOAT_EQ(actual_y - block_y, image_y);
        }
    }
    void expect_centered_button_text(DomElement* button) {
        ASSERT_NE(button, nullptr);
        DomElement* label = button->first_child->as_element();
        DomText* text = label->first_child->as_text();
        ASSERT_NE(text->rect, nullptr);
        float x, y, width, height;
        view_get_visual_bounds(button, &x, &y, &width, &height);
        RdtLogicalPoint origin = view_geometry_text_rect_document_origin(text, text->rect);
        EXPECT_NEAR(origin.y + text->rect->height / 2, y + height / 2, 0.6f);
    }
};

TEST_F(RadiantTextLayout, InvalidVariableLeadingInheritsWithoutMovingButtonText) {
    DomElement* owner = load("test/html/line_height_invalid_variables.html");
    ASSERT_NE(owner, nullptr);
    const char* centered[] = {"normal", "unset", "initial", "missing", "cycle", "bad-type",
        "fallback", "inherited-missing", "inherited-unset", "inherited-initial"};
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        for (const char* id : centered) {
            SCOPED_TRACE(id);
            DomElement* button = dom_find_element_by_id(owner, id);
            ASSERT_NE(button, nullptr);
            expect_centered_button_text(button);
            if (strcmp(id, "inherited-missing") == 0 || strcmp(id, "inherited-unset") == 0) {
                ASSERT_EQ(button->block()->line_height->type, CSS_VALUE_TYPE_NUMBER);
                EXPECT_DOUBLE_EQ(button->block()->line_height->data.number.value, 1.5);
            }
        }
        DomElement* zero = dom_find_element_by_id(owner, "zero");
        ASSERT_NE(zero, nullptr);
        ASSERT_EQ(zero->block()->line_height->type, CSS_VALUE_TYPE_NUMBER);
        EXPECT_DOUBLE_EQ(zero->block()->line_height->data.number.value, 0);
    }
}

TEST_F(RadiantTextLayout, PhotoDtnaButtonHasThemeLeadingAndCenteredText) {
    DomElement* owner = load("test/lambda/ui/_photo_button_layout.ls");
    ASSERT_NE(owner, nullptr);
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        DomElement* button = dom_find_element_by_id(owner, "add-text");
        ASSERT_NE(button, nullptr);
        ASSERT_EQ(button->block()->line_height->type, CSS_VALUE_TYPE_LENGTH);
        EXPECT_DOUBLE_EQ(button->block()->line_height->data.length.value, 20);
        expect_centered_button_text(button);
    }
}

class RadiantFrameDelivery : public RadiantTextQuery {};

TEST_F(RadiantTextLayout, GraphLayoutConnectsAutomaticallyAcrossDocumentsAndReflow) {
    ASSERT_NE(load("test/html/automatic_graph_layout.html"), nullptr);
    layout_html_doc(&ui, document, false);
    Runtime* first_runtime = dom_document_loader_runtime(document);
    ASSERT_NE(first_runtime, nullptr);
    DomElement* graph = dom_find_element_by_id(document->root->as_element(), "graph");
    ASSERT_NE(graph, nullptr);
    ASSERT_NE(graph->custom_layout_paint_prop(), nullptr);
    ASSERT_GT(graph->custom_layout_paint_prop()->layer_count, 0);
    float first_width = graph->width;
    EXPECT_GT(first_width, 160.0f);

    // two static documents own independent package state; closing one must not disconnect the other.
    DomDocument* first = document;
    struct HeldDocument {
        DomDocument* doc;
        ~HeldDocument() { free_document(doc); }
    } held = {first};
    document = nullptr;
    ASSERT_NE(load("test/html/automatic_graph_layout.html"), nullptr);
    layout_html_doc(&ui, document, false);
    EXPECT_NE(dom_document_loader_runtime(document), first_runtime);
    DomElement* second_graph = dom_find_element_by_id(document->root->as_element(), "graph");
    ASSERT_NE(second_graph, nullptr);
    EXPECT_FLOAT_EQ(second_graph->width, first_width);
    ui.document = nullptr;
    free_document(document);
    document = nullptr;

    ui.document = lam::up(first);
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, first, true);
        EXPECT_EQ(dom_document_loader_runtime(first), first_runtime);
        EXPECT_FLOAT_EQ(graph->width, first_width);
        DomElement* a = dom_find_element_by_id(first->root->as_element(), "a");
        DomElement* b = dom_find_element_by_id(first->root->as_element(), "b");
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        EXPECT_GT(b->x, a->x + a->width);
        EXPECT_FLOAT_EQ(a->y, b->y);
        ASSERT_NE(graph->custom_layout_paint_prop(), nullptr);
        EXPECT_GT(graph->custom_layout_paint_prop()->layer_count, 0);
    }
    ui.document = nullptr;
}

static bool test_package_layout_callback(const CustomLayoutContext* context,
                                         CustomLayoutResult* result) {
    return radiant_package_layout_callback(context, result, ".test.lambda._custom_layout_callbacks");
}

TEST_F(RadiantTextLayout, TikzLayoutConnectsAutomaticallyAndRetainsMeasuredPaint) {
    DomElement* owner = load("test/lambda/ui/_automatic_tikz_layout.ls");
    ASSERT_NE(owner, nullptr);
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        DomElement* wrapper = dom_find_element_by_id(owner, "picture");
        ASSERT_NE(wrapper, nullptr);
        ASSERT_NE(wrapper->first_child, nullptr);
        DomElement* picture = wrapper->first_child->as_element();
        ASSERT_NE(picture, nullptr);
        EXPECT_STREQ(picture->get_attribute("data-radiant-layout"), "lambda-tikz");
        EXPECT_GT(picture->width, 100.0f);
        EXPECT_GT(picture->height, 0.0f);
        CustomLayoutPaintState* paint = picture->custom_layout_paint_prop();
        ASSERT_NE(paint, nullptr);
        ASSERT_EQ(paint->layer_count, 1);
        ASSERT_NE(paint->layers[0].content, nullptr);
        EXPECT_TRUE(strview_equal(&((TypeElmt*)paint->layers[0].content->type)->name, "svg"));
        DomElement* a = picture->first_child->as_element();
        ASSERT_NE(a, nullptr);
        ASSERT_NE(a->next_sibling, nullptr);
        DomElement* b = a->next_sibling->as_element();
        ASSERT_NE(b, nullptr);
        EXPECT_GT(a->width, 0.0f);
        EXPECT_GT(b->x, a->x + a->width);
    }
}

class RadiantPackageLayout : public RadiantTextLayout {
    void TearDown() override {
        RadiantTextLayout::TearDown();
        custom_layout_registry_clear();
    }
};

TEST_F(RadiantPackageLayout, ChildMarginsStayInsideCustomFormattingContext) {
    ASSERT_TRUE(custom_layout_register("bfc_smoke", test_package_layout_callback));
    ASSERT_NE(load("test/lambda/radiant_custom_layout_bfc.html"), nullptr);
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        expect_box("graph", 0.0f, 0.0f, 20.0f, 10.0f);
        expect_box("child", 0.0f, 0.0f, 20.0f, 10.0f);
        expect_box("after", 0.0f, 10.0f, 5.0f, 5.0f);
    }
}

TEST_F(RadiantPackageLayout, MeasuredChildConstraintsAndNegativePlacementsSurviveReflow) {
    ASSERT_TRUE(custom_layout_register("flow_smoke", test_package_layout_callback));
    ASSERT_NE(load("test/lambda/radiant_custom_layout_flow.html"), nullptr);
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        expect_box("graph", 0.0f, 0.0f, 140.0f, 80.0f);
        expect_box("a", -10.0f, 0.0f, 40.0f, 20.0f);
        expect_box("b", 40.0f, 30.0f, 40.0f, 20.0f);
        expect_box("c", 90.0f, 60.0f, 40.0f, 20.0f);
    }
}

TEST_F(RadiantPackageLayout, VelmtAttributesRemainAvailableToGeneratedPaint) {
    ASSERT_TRUE(custom_layout_register("projection-smoke", test_package_layout_callback));
    ASSERT_NE(load("test/html/custom_layout_projection.html"), nullptr);
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        DomElement* graph = dom_find_element_by_id(document->root->as_element(), "projection");
        ASSERT_NE(graph, nullptr);
        CustomLayoutPaintState* paint = graph->custom_layout_paint_prop();
        ASSERT_NE(paint, nullptr);
        ASSERT_EQ(paint->layer_count, 1);
        ElementReader content(paint->layers[0].content);
        String* tag = content.get_attr("data-projection-tag").asString();
        String* role = content.get_attr("data-projection-role").asString();
        String* id = content.get_attr("data-projection-id").asString();
        ASSERT_NE(tag, nullptr);
        ASSERT_NE(role, nullptr);
        ASSERT_NE(id, nullptr);
        EXPECT_STREQ(tag->chars, "div");
        EXPECT_STREQ(role->chars, "node");
        EXPECT_STREQ(id->chars, "n0");
    }
}

TEST_F(RadiantTextLayout, PendingResourceLoadsReleasePinsWithEitherOwner) {
    ASSERT_NE(load("test/html/layout_load_runtime.html", true), nullptr);
    EvalContext* owner = runtime_get_eval_context(dom_document_script_runtime(document));
    ASSERT_TRUE(radiant_eval_context_switch(owner));
    struct PendingDocuments {
        DomDocument* documents[2] = {};
        ~PendingDocuments() {
            dom_dispatch_pending_resource_loads();
            for (DomDocument* doc : documents) if (doc) dom_document_destroy(doc);
        }
    } held;
    DomElement* frames[2] = {};
    DomElement* images[2] = {};
    for (size_t i = 0; i < 2; i++) {
        held.documents[i] = dom_document_create(document->input);
        ASSERT_NE(held.documents[i], nullptr);
        dom_document_borrow_input_resources(held.documents[i]);
        frames[i] = dom_element_create(held.documents[i], "iframe", nullptr);
        images[i] = dom_element_create(held.documents[i], "img", nullptr);
        ASSERT_NE(frames[i], nullptr); ASSERT_NE(images[i], nullptr);
        dom_iframe_navigation_complete(frames[i]);
        dom_queue_image_completion(images[i], true);
    }
    auto expect_pins = [&](size_t index, uint32_t count) {
        for (DomElement* node : {frames[index], images[index]})
            EXPECT_EQ(dom_node_pin_count(held.documents[index], dom_node_ref(node),
                DOM_NODE_PIN_EVENT_QUEUE), count);
    };
    expect_pins(0, 1); expect_pins(1, 1);
    dom_document_release_runtime_resources(held.documents[0], owner);
    expect_pins(0, 0); expect_pins(1, 1);
    if (HasFailure()) return;
    dom_iframe_navigation_complete(frames[0]);
    dom_queue_image_completion(images[0], false);
    expect_pins(0, 1);
    // document-first retirement must also detach the capsule's queued borrows.
    dom_document_destroy(held.documents[0]);
    held.documents[0] = nullptr;
    dom_document_release_runtime_resources(held.documents[1], owner);
    expect_pins(1, 0);
}

TEST_F(RadiantTextLayout, LoadTimeMathLayoutRetainsItsRuntimeUntilDocumentTeardown) {
    struct TransientScripts {
        TransientScripts() { script_runner_set_retain_js_state(false); }
        ~TransientScripts() { script_runner_set_retain_js_state(true); }
    } transient;
    ASSERT_NE(load("test/html/layout_load_runtime.html", true), nullptr);
    // embedded load blockers measure MathML before the transient JS realm is released.
    ASSERT_EQ(document->js.runtime, nullptr);
    Runtime* runtime = document->lambda_runtime;
    ASSERT_NE(runtime, nullptr);
    ASSERT_TRUE(document->owns_script_runtime);
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        radiant_run_behavior_init(document);
        EXPECT_EQ(dom_document_script_runtime(document), runtime);
        DomElement* math = dom_find_element_by_id(document->root->as_element(), "formula");
        ASSERT_NE(math, nullptr);
        EXPECT_GT(math->width, 0.0f);
        ASSERT_NE(math->custom_layout_paint_prop(), nullptr);
        EXPECT_GT(math->custom_layout_paint_prop()->layer_count, 0);
    }
}

TEST_F(RadiantTextLayout, MathMLUsesMeasuredPaintAndRetainsItsSourceDom) {
    DomElement* owner = load("test/html/mathml_native.html");
    ASSERT_NE(owner, nullptr);
    for (int pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        for (const char* id : {"inline", "display", "flex", "matrix"}) {
            SCOPED_TRACE(id);
            DomElement* math = dom_find_element_by_id(owner, id);
            ASSERT_NE(math, nullptr);
            EXPECT_STREQ(dom_element_namespace_uri(math), "http://www.w3.org/1998/Math/MathML");
            ASSERT_NE(math->first_child, nullptr);
            EXPECT_GT(math->content_width, 0.0f);
            EXPECT_GT(math->content_height, 0.0f);
            ASSERT_NE(math->custom_layout_paint_prop(), nullptr);
            CustomLayoutPaintState* paint = math->custom_layout_paint_prop();
            ASSERT_EQ(paint->layer_count, 1);
            ASSERT_NE(paint->layers[0].content, nullptr);
            EXPECT_TRUE(strview_equal(&((TypeElmt*)paint->layers[0].content->type)->name, "svg"));
        }
        DomElement* inline_math = dom_find_element_by_id(owner, "inline");
        EXPECT_STREQ(inline_math->first_child->node_name(), "mfrac");
        DomElement* flex_math = dom_find_element_by_id(owner, "flex");
        DomElement* semantics = flex_math->first_child->as_element();
        EXPECT_STREQ(semantics->last_child->node_name(), "annotation");
        EXPECT_EQ(dom_find_element_by_id(owner, "cssflow")->custom_layout_paint_prop(), nullptr);
    }
}

TEST_F(RadiantFrameDelivery, FullscreenCatalogRefusesWindowlessTransitions) {
    DomElement* owner = load("test/lambda/ui/_window_fullscreen.ls");
    ASSERT_NE(owner, nullptr);
    layout_html_doc(&ui, document, false);
    ASSERT_NE(dom_engine_document_ensure_state(document, "fullscreen_test"), nullptr);
    ASSERT_NE(dom_engine_frame_request(owner, "probe"), 0u);
    ASSERT_TRUE(dom_engine_frame_tick(document, 1.0));
    EXPECT_STREQ(owner->get_attribute("data-available"), "false");
    EXPECT_STREQ(owner->get_attribute("data-actual"), "false");
    EXPECT_STREQ(owner->get_attribute("data-enter"), "false");
    EXPECT_STREQ(owner->get_attribute("data-exit"), "false");
}

TEST_F(RadiantFrameDelivery, AudibleOutputGateFollowsDocumentOrigin) {
    // D7.5.4: a local document may start audio; a network one needs trusted
    // activation or the host grant, and activation is sticky.
    ASSERT_NE(load("test/lambda/ui/_window_fullscreen.ls"), nullptr);
    EXPECT_TRUE(radiant_document_may_start_audio(document));
    Url* local = document->url.get();
    Url* network = url_parse("https://example.com/narration.html");
    ASSERT_NE(network, nullptr);
    document->url = lam::own(network);
    EXPECT_FALSE(radiant_document_may_start_audio(document));
    radiant_audio_grant_activation(true);
    EXPECT_TRUE(radiant_document_may_start_audio(document));
    radiant_audio_grant_activation(false);
    EXPECT_FALSE(radiant_document_may_start_audio(document));
    radiant_audio_note_activation(document);
    EXPECT_TRUE(radiant_document_may_start_audio(document));
    document->url = lam::own(local);
    url_destroy(network);
}

TEST_F(RadiantFrameDelivery, ScheduledUtteranceStartsAtItsSubmixSampleOffline) {
    // Radiant_Design_Speech §3.2/§3.4: `at` is seconds on the document sub-mix
    // clock, and an offline sub-mix renders without a device.
    DomElement* owner = load("test/lambda/ui/_window_fullscreen.ls");
    ASSERT_NE(owner, nullptr);
#ifdef _WIN32
    _putenv_s("LAMBDA_SPEECH_ENGINE", "fake");
#else
    setenv("LAMBDA_SPEECH_ENGINE", "fake", 1);
#endif
    audio_bus_set_offline(true);
    SpeechText text = {};
    ASSERT_TRUE(speech_text_from_string("go go go go", 11, &text));   // 1.2 s of fake speech
    SpeechOptions options = {nullptr, nullptr, 1, 1, 1, 0.5};
    const char* error = nullptr;
    uint64_t token = radiant_speech_speak(owner, &text, &options, &error);
    ASSERT_NE(token, 0u) << (error ? error : "");
    EXPECT_TRUE(radiant_speech_tick(document));   // arms the utterance on the speech voice
    AudioSubmixId submix = radiant_document_submix(document);
    const uint32_t frames = AUDIO_BUS_RATE;
    auto* mix = (float*)mem_calloc((size_t)frames * 2, sizeof(float), MEM_CAT_RENDER);
    ASSERT_NE(mix, nullptr);
    ASSERT_TRUE(audio_submix_render(submix, mix, frames));
    uint32_t first = frames;
    for (uint32_t i = 0; i < frames && first == frames; i++) if (mix[i * 2] != 0) first = i;
    // the fake tone begins at sin(0), so its first audible sample follows the start frame.
    EXPECT_GE(first, AUDIO_BUS_RATE / 2);
    EXPECT_LE(first, AUDIO_BUS_RATE / 2 + 4);
    EXPECT_DOUBLE_EQ(radiant_audio_clock(document), 1.0);
    radiant_speech_tick(document);
    SpeechInfo info = {};
    ASSERT_TRUE(radiant_speech_info(document, token, &info));
    EXPECT_EQ(info.state, SPEECH_SPEAKING);
    EXPECT_NEAR(info.position, 0.5, 1e-9);
    // leaving the bus idle lets the output mode change back.
    EXPECT_TRUE(radiant_speech_command(document, token, "cancel"));
    EXPECT_FALSE(radiant_speech_tick(document));
    EXPECT_FALSE(radiant_audio_frames_pending(document));
    mem_free(mix);
    audio_bus_set_offline(false);
#ifdef _WIN32
    _putenv_s("LAMBDA_SPEECH_ENGINE", "");
#else
    unsetenv("LAMBDA_SPEECH_ENGINE");
#endif
}

TEST_F(RadiantFrameDelivery, HidingFocusedPanelSettlesBeforeValidationWithoutHostBatch) {
    DomElement* owner = load("test/lambda/ui/_frame_focus.ls");
    ASSERT_NE(owner, nullptr);
    layout_html_doc(&ui, document, false);
    ASSERT_NE(dom_engine_document_ensure_state(document, "frame_focus_test"), nullptr);
    ASSERT_NE(dom_engine_frame_request(owner, "prepare_frame"), 0u);
    ASSERT_TRUE(dom_engine_frame_tick(document, 1.0));
    ASSERT_STREQ(owner->get_attribute("data-prepared"), "true");
    DomElement* button = dom_find_element_by_id(owner, "focused");
    ASSERT_NE(button, nullptr);
    ASSERT_EQ(focus_get(document->state), static_cast<View*>(button));
    ASSERT_TRUE(radiant_document_has_pending_frames(document));
    ASSERT_EQ(document->state->state_batch_depth, 0u);
    // the interactive clock calls directly, without the replay loop's outer batch.
    ASSERT_TRUE(dom_engine_frame_tick(document, 2.0));
    EXPECT_STREQ(owner->get_attribute("data-finished"), "true");
    EXPECT_EQ(focus_get(document->state), nullptr);
    EXPECT_FALSE(state_get_bool(document->state, button, STATE_FOCUS));
    EXPECT_EQ(document->state->state_batch_depth, 0u);
    EXPECT_TRUE(radiant_state_validate_interaction(document->state, nullptr));
}

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

TEST_F(RadiantTextLayout, NestedInlineFontsKeepTheirOwnLeadingAcrossReflow) {
    ASSERT_NE(load("test/html/nested_font_strut.html"), nullptr);
    ui.viewport_width = 240;
    ui.viewport_height = 2816;
    layout_html_doc(&ui, document, false);
    struct FontMetrics { float size; float normal_height; };
    const FontMetrics fonts[] = {{24.0f, 28.0f}, {32.0f, 37.0f}, {48.0f, 55.0f}};
    for (unsigned pass = 0; pass < 2; pass++) {
        unsigned index = 0;
        for (const FontMetrics& font : fonts) for (bool normal : {true, false})
            for (bool image : {false, true}) for (bool outside_text : {false, true}) {
                SCOPED_TRACE(outside_text ? "outside text" : "inside text");
                char actual_id[32], reference_id[32];
                str_fmt(actual_id, sizeof(actual_id), "actual%u", index);
                str_fmt(reference_id, sizeof(reference_id), "reference%u", index++);
                float expected_height = normal ? font.normal_height : font.size;
                expect_inline_strut_pair(actual_id, reference_id, expected_height, image);
            }
        expect_inline_strut_pair("wrapped-normal", "wrapped-normal-reference", 220.0f, true, 42.0f);
        expect_inline_strut_pair("wrapped-unitless", "wrapped-unitless-reference", 192.0f, true, 38.0f);
        // recompute metrics when retained descendants re-enter their parent context.
        if (pass == 0) layout_html_doc(&ui, document, true);
    }
}

TEST_F(RadiantTextLayout, ExplicitSnapshotReadinessSettlesLongFrameChains) {
    expect_snapshot_capture("test/html/layout_snapshot_ready.html", 22.0f);
}

TEST_F(RadiantTextLayout, ExplicitSnapshotReadinessPreservesDelayedTimerChains) {
    expect_snapshot_capture("test/html/layout_snapshot_timers.html", 41.0f);
}

TEST_F(RadiantTextLayout, PositionedTableDescendantsSkipStaticTableOffsetParents) {
    DomElement* result = load("test/html/layout_offset_parent_table.html", true);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->get_attribute("data-sticky"), "host");
    EXPECT_STREQ(result->get_attribute("data-relative"), "host");
    EXPECT_STREQ(result->get_attribute("data-absolute"), "host");
    EXPECT_STREQ(result->get_attribute("data-static"), "table");
    EXPECT_STREQ(result->get_attribute("data-static-child"), "sticky");
    EXPECT_STREQ(result->get_attribute("data-left"), "100");
}

TEST_F(RadiantTextLayout, RelativeInlineOffsetsSurviveFinalLineAlignmentAndReflow) {
    ASSERT_NE(load("test/html/layout_relative_inline_break.html"), nullptr);
    layout_html_doc(&ui, document, false);
    const struct { const char* id; float offset; } cases[] = {
        {"normal", 0.0f}, {"relative", -6.0f}, {"nested", -9.0f}
    };
    for (unsigned pass = 0; pass < 2; pass++) {
        DomElement* normal = dom_find_element_by_id(document->root->as_element(), "normal");
        ASSERT_NE(normal, nullptr);
        ViewText* first = lam::view_require_text(normal->first_child);
        ASSERT_NE(first->rect, nullptr);
        for (const auto& item : cases) {
            DomElement* span = dom_find_element_by_id(document->root->as_element(), item.id);
            ASSERT_NE(span, nullptr);
            ViewText* begin = lam::view_require_text(span->first_child);
            ViewText* end = lam::view_require_text(span->last_child);
            ASSERT_NE(begin->rect, nullptr); ASSERT_NE(end->rect, nullptr);
            EXPECT_NEAR(begin->rect->y, first->rect->y + item.offset, 0.01f) << item.id;
            EXPECT_NEAR(end->rect->y - begin->rect->y, 24.0f, 0.01f) << item.id;
            EXPECT_NEAR(span->height, normal->height, 0.01f) << item.id;
        }
        if (pass == 0) layout_html_doc(&ui, document, true);
    }
}

TEST_F(RadiantTextLayout, PunctuationRetainsKerningAcrossInlineBoundariesAndReflow) {
    ASSERT_NE(load("test/html/layout_inline_punctuation_kerning.html"), nullptr);
    layout_html_doc(&ui, document, false);
    for (unsigned pass = 0; pass < 2; pass++) {
        DomElement* whole = dom_find_element_by_id(document->root->as_element(), "whole");
        DomElement* split = dom_find_element_by_id(document->root->as_element(), "split");
        DomElement* wrapped = dom_find_element_by_id(document->root->as_element(), "split-wrap");
        DomElement* separated = dom_find_element_by_id(document->root->as_element(), "separated");
        ASSERT_NE(whole, nullptr); ASSERT_NE(split, nullptr); ASSERT_NE(wrapped, nullptr);
        ASSERT_NE(separated, nullptr);
        EXPECT_NEAR(split->width, whole->width, 0.02f);
        EXPECT_NEAR(wrapped->width, whole->width, 0.02f);
        // padding ends the shaping run; its width must survive along with the unkerned pair.
        EXPECT_GE(separated->width, whole->width + 7.99f);
        if (pass == 0) layout_html_doc(&ui, document, true);
    }
}

TEST_F(RadiantTextLayout, SnapshotReadinessNotifiesTheCommittedFirstFrame) {
    expect_snapshot_capture("test/html/layout_snapshot_test_rendered.html", 37.0f);
    DomElement* box = dom_find_element_by_id(document->root->as_element(), "result");
    ASSERT_NE(box, nullptr);
    EXPECT_STREQ(box->get_attribute("data-rendered"), "yes");
}

TEST_F(RadiantTextLayout, ScrollReadsUpdateStickyGeometryAndFixedSubtreesUseTheViewport) {
    DomElement* body = load("test/html/layout_snapshot_sticky_scroll.html", true);
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(document->root->has_class("reftest-wait"));
    for (const char* attribute : {"data-scroll", "data-offset", "data-flow", "data-fixed", "data-reset"})
        EXPECT_STREQ(body->get_attribute(attribute), "yes") << attribute;
}

TEST_F(RadiantTextLayout, SnapshotReadinessPreservesFrameCancellation) {
    expect_snapshot_capture("test/html/layout_snapshot_cancelled.html", 34.0f);
}

TEST_F(RadiantTextLayout, OrdinarySnapshotKeepsBoundedRecurringAnimation) {
    expect_snapshot_capture("test/html/layout_snapshot_ordinary.html", 0.0f);
}

TEST_F(RadiantTextLayout, NormalLeadingBaselinesRetainFontCellRoundingAcrossReflow) {
    ASSERT_NE(load("test/html/layout_normal_leading_baselines.html"), nullptr);
    ui.viewport_width = 320;
    ui.viewport_height = 200;
    layout_html_doc(&ui, document, false);
    struct Metrics { float height; float baseline; };
    const Metrics expected[] = {{32, 25}, {28, 22}, {26, 20}, {22, 17}, {21, 16}, {19, 15}};
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned index = 0; index < 6; index++) {
            char id[16];
            str_fmt(id, sizeof(id), "word%u", index);
            DomElement* word = dom_find_element_by_id(document->root->as_element(), id);
            ASSERT_NE(word, nullptr); ASSERT_NE(word->blk, nullptr);
            EXPECT_FLOAT_EQ(word->height, expected[index].height) << id;
            EXPECT_FLOAT_EQ(word->block()->first_line_baseline, expected[index].baseline) << id;
        }
        // measure_html exposes these same per-box baselines to SVG chart placement.
        if (pass == 0) layout_html_doc(&ui, document, true);
    }
}

TEST_F(RadiantTextLayout, NestedMulticolBreaksRetainTheirLineEndpointsAcrossReflow) {
    ASSERT_NE(load("test/html/layout_nested_multicol_break_endpoints.html"), nullptr);
    ui.viewport_width = 240;
    ui.viewport_height = 960;
    layout_html_doc(&ui, document, false);
    const float font_heights[] = {14.0f, 17.0f, 26.0f};
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned index = 0; index < 6; index++) {
            float gap = index % 2 ? 2.0f : 0.0f;
            float width = (50.0f - 4.0f * gap) / 5.0f;
            float pitch = width + gap;
            float y = index * 150.0f + 70.0f;
            for (unsigned line = 0; line < 3; line++) {
                char id[32];
                str_fmt(id, sizeof(id), "case%u-br%u", index, line);
                expect_box(id, 16.0f + 2.0f * pitch + width, y,
                    0.0f, font_heights[index / 2]);
                str_fmt(id, sizeof(id), "case%u-ib%u", index, line);
                expect_box(id, 16.0f + (line + 2.0f) * pitch, y, width, 30.0f);
            }
        }
        // retained reflow must reconstruct source cursors before projecting again.
        if (pass == 0) layout_html_doc(&ui, document, true);
    }
}

TEST_F(RadiantTextLayout, AdjoiningEmptyChildrenPreserveMarginChainsAndComputedEdges) {
    ASSERT_NE(load("test/html/layout_adjoining_empty_margins.html"), nullptr);
    ui.viewport_width = 240;
    ui.viewport_height = 896;
    layout_html_doc(&ui, document, false);
    struct MarginPair { const char* name; float y; float height; const char* child_margin; };
    const MarginPair pairs[] = {
        {"larger-heading", 18.72f, 22.0f, "18.72px"}, {"two-positive", 152.0f, 22.0f, "24px"},
        {"negative", 270.0f, 22.0f, "24px"}, {"nested", 424.0f, 22.0f, "10px"},
        {"bfc", 520.0f, 70.0f, "24px"}, {"border", 648.0f, 47.0f, "24px"},
        {"padding", 776.0f, 47.0f, "24px"},
    };
    for (unsigned pass = 0; pass < 2; pass++) {
        for (const MarginPair& pair : pairs) {
            char actual_id[48], reference_id[48];
            str_fmt(actual_id, sizeof(actual_id), "%s-0", pair.name);
            str_fmt(reference_id, sizeof(reference_id), "%s-1", pair.name);
            expect_box(actual_id, 16.0f, pair.y, 100.0f, pair.height);
            expect_box(reference_id, 128.0f, pair.y, 100.0f, pair.height);
            DomElement* actual = dom_find_element_by_id(document->root->as_element(), actual_id);
            ASSERT_NE(actual, nullptr);
            DomElement* child = actual->last_child->as_element();
            ASSERT_NE(child, nullptr);
            size_t before_bytes = 0, before_count = 0;
            pool_get_stats(document->document_pool, &before_bytes, &before_count);
            char value[64];
            for (unsigned repeat = 0; repeat < 128; repeat++) {
                for (CssPropertyCode side : {CSS_PROPERTY_MARGIN_TOP, CSS_PROPERTY_MARGIN_BOTTOM}) {
                    ASSERT_TRUE(css_prop_serialize_computed(actual, side, 0, value, sizeof(value)));
                    EXPECT_STREQ(value, "8px");
                    ASSERT_TRUE(css_prop_serialize_computed(child, side, 0, value, sizeof(value)));
                    EXPECT_STREQ(value, pair.child_margin);
                }
            }
            size_t after_bytes = 0, after_count = 0;
            pool_get_stats(document->document_pool, &after_bytes, &after_count);
            EXPECT_EQ(after_bytes, before_bytes); EXPECT_EQ(after_count, before_count);
        }
        // consumed margins must be reconstructed from resolved values during reflow.
        if (pass == 0) layout_html_doc(&ui, document, true);
    }
}
