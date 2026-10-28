// in-process Lambda text queries, retained layout and frame delivery checks.
#include <gtest/gtest.h>
#include "../radiant/radiant.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/layout_text_context.hpp"
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
#include "../lib/font/font_internal.h"
#include "../lib/hash.h"
#include "../lib/file.h"

#if defined(__APPLE__) && !defined(LAMBDA_NO_GUI)
#include <OpenGL/gl.h>
#endif

extern "C" uint64_t radiant_query_font_build_count(void);
ArrayList* build_view_stack(EventContext* context, View* target);
void fire_events(EventContext* context, ArrayList* stack);
void update_hover_state(EventContext* context, View* target);


#ifdef __APPLE__
// CoreText's legacy Rect type conflicts with Radiant's geometry type.
#define Rect MacOSRect
#include <CoreText/CoreText.h>
#undef Rect

TEST(FontCollections, LookupUsesPhysicalFaces) {
    FontContextConfig config = {};
    FontContext* context = font_context_create(&config);
    ASSERT_NE(context, nullptr);
    const char* families[] = {"PingFang HK", "PingFang SC", "PingFang TC", "PingFang MO", "Avenir Next", "Songti SC"};
    int tested = 0;
    for (const char* family : families) {
        CFStringRef family_name = CFStringCreateWithCString(nullptr, family, kCFStringEncodingUTF8);
        CTFontRef selected = CTFontCreateWithNameAndOptions(family_name, 16.0, nullptr, kCTFontOptionsPreventAutoDownload);
        CFStringRef installed = CTFontCopyFamilyName(selected);
        bool available = installed && CFEqual(installed, family_name);
        if (installed) CFRelease(installed);
        CFRelease(selected);
        CFRelease(family_name);
        if (!available) continue;
        int index = -1;
        char* path = font_platform_find_fallback(family, &index);
        ASSERT_NE(path, nullptr) << family;
        tested++;
        FontHandle* handle = font_load_face_internal(context, path, index, 16.0f, 16.0f,
            FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL);
        ASSERT_NE(handle, nullptr) << family;
        int count = font_tables_get_face_count(handle->memory_buffer, handle->memory_buffer_size);
        EXPECT_GE(index, 0) << family;
        EXPECT_LT(index, count) << family << " at " << path;
        EXPECT_NE(handle->tables, nullptr) << family;
        if (handle->tables) {
            NameTable* name = font_tables_get_name(handle->tables);
            ASSERT_NE(name, nullptr);
            EXPECT_STREQ(name->family_name, family);
            CFStringRef expected = CFStringCreateWithCString(nullptr, name->postscript_name, kCFStringEncodingUTF8);
            ASSERT_NE(handle->ct_raster_ref, nullptr);
            CFStringRef actual = CTFontCopyPostScriptName((CTFontRef)handle->ct_raster_ref);
            EXPECT_TRUE(CFEqual(expected, actual)) << family;
            CFRelease(actual);
            CFRelease(expected);
        }
        log_info("font_collection_test: family=%s physical_index=%d count=%d path=%s", family, index, count, path);
        font_handle_release(handle);
        mem_free(path);
    }
    font_context_destroy(context);
    if (!tested) GTEST_SKIP() << "regression font collections are not installed";
}

TEST(FontCollections, EmojiLookupRetainsTheSelectedNativeFace) {
    FontContextConfig config = {};
    FontContext* context = font_context_create(&config);
    ASSERT_NE(context, nullptr);
    int index = -1;
    char* path = font_platform_find_emoji_font(0x1F600, &index);
    ASSERT_NE(path, nullptr);
    FontHandle* handle = font_load_face_internal(context, path, index, 16.0f, 16.0f,
        FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL);
    ASSERT_NE(handle, nullptr);
    EXPECT_NE(handle->tables, nullptr);
    EXPECT_GT(font_get_glyph_index(handle, 0x1F600), 0u);
    const GlyphBitmap* bitmap = font_render_glyph(handle, 0x1F600, GLYPH_RENDER_NORMAL);
    ASSERT_NE(bitmap, nullptr);
    EXPECT_NE(bitmap->buffer, nullptr);
    EXPECT_GT(bitmap->width, 0);
    font_handle_release(handle);
    mem_free(path);
    font_context_destroy(context);
}

#endif

TEST(FontLocalMetadata, LocalFontMetadataIsOwnedSortedAndUnique) {
    ArrayList* faces = font_query_local_fonts(nullptr);
    ASSERT_NE(faces, nullptr);
    ASSERT_GT(faces->length, 0);
    for (int i = 0; i < faces->length; i++) {
        auto* face = (FontLocalFace*)faces->data[i];
        ASSERT_NE(face, nullptr);
        ASSERT_NE(face->postscript_name, nullptr);
        ASSERT_NE(face->family, nullptr);
        ASSERT_NE(face->full_name, nullptr);
        ASSERT_NE(face->style, nullptr);
        EXPECT_GT(strlen(face->postscript_name), 0u);
        EXPECT_GT(strlen(face->family), 0u);
        EXPECT_GT(strlen(face->full_name), 0u);
        if (i) EXPECT_LT(strcmp(((FontLocalFace*)faces->data[i - 1])->postscript_name, face->postscript_name), 0);
    }
    ArrayList* again = font_query_local_fonts(nullptr);
    ASSERT_NE(again, nullptr);
    ASSERT_EQ(again->length, faces->length);
    EXPECT_NE(again->data[0], faces->data[0]);
    font_local_fonts_free(faces);
    EXPECT_GT(strlen(((FontLocalFace*)again->data[0])->family), 0u);
    font_local_fonts_free(again);
}

TEST(FontTextMeasurement, TextMeasurementsOwnKeysAndInvalidateWhenFontResourcesChange) {
    FontContextConfig config = {};
    config.pixel_ratio = 1.0f;
    FontContext* ctx = font_context_create(&config);
    ASSERT_NE(ctx, nullptr);
    FontStyleDesc style = {};
    style.size_px = 16.0f;
    style.weight = FONT_WEIGHT_NORMAL;
    FontHandle* handle = font_load_from_file(ctx,
        "test/lambda/math/fonts/NotoSansMath-Regular.ttf", &style);
    ASSERT_NE(handle, nullptr);
    char text[] = "office";
    TextExtents original = font_measure_text(handle, text, 6);
    ASSERT_GT(original.width, 0.0f);
    uint64_t hits = font_get_cache_stats(ctx).text_measure_hits;
    text[0] = 'a';
    font_measure_text(handle, text, 6);
    EXPECT_FLOAT_EQ(font_measure_text(handle, "office", 6).width, original.width);
    EXPECT_EQ(font_get_cache_stats(ctx).text_measure_hits, hits + 1);

    // even an occupied slot with a colliding hash must compare the complete bytes.
    FontTextMeasureEntry* entry = &handle->text_measure_cache[
        hash_fnv1a_32("office", 6) % FONT_TEXT_MEASURE_CACHE_SIZE];
    entry->text[0] = 'x';
    entry->extents.width = -1.0f;
    EXPECT_FLOAT_EQ(font_measure_text(handle, "office", 6).width, original.width);
    EXPECT_EQ(font_get_cache_stats(ctx).text_measure_hits, hits + 1);
    entry->extents.width = -1.0f;
    ctx->resource_generation++;
    EXPECT_FLOAT_EQ(font_measure_text(handle, "office", 6).width, original.width);
    EXPECT_EQ(font_get_cache_stats(ctx).text_measure_hits, hits + 1);
#ifdef __APPLE__
    entry->extents.width = -1.0f;
    font_backend_use_ct_font(handle, handle->ct_raster_ref);
    EXPECT_FLOAT_EQ(font_measure_text(handle, "office", 6).width, original.width);
    EXPECT_EQ(font_get_cache_stats(ctx).text_measure_hits, hits + 1);
#endif
    font_handle_release(handle);
    font_context_destroy(ctx);
}

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

TEST(TextShapingContext, EdgeSlicesPreserveUtf8AndStopAtTheNearestBoundary) {
    struct Case { const char* text; bool cursive; const char* prefix; const char* suffix; };
    const Case cases[] = {
        {"", false, "", ""},
        {"office", false, "office", "office"},
        {"one two", false, "one", "two"},
        {"one\n\ttwo", false, "one", "two"},
        {" one ", false, "", ""},
        {"abécd", false, "ab", "cd"},
        {"عع عع", true, "عع", "عع"},
        {"عع\xE2\x80\x8Dعع", true, "عع\xE2\x80\x8Dعع", "عع\xE2\x80\x8Dعع"},
        {"عع\xE2\x80\x8Cعع", true, "عع\xE2\x80\x8Cعع", "عع\xE2\x80\x8Cعع"},
        {"ab\x80" "cd", false, "ab", "cd"},
        {"ab\xC3", false, "ab", ""},
        {"عع\x80عع", true, "عع", "عع"},
    };
    const bool directions[] = {false, true};
    for (const Case& entry : cases) {
        for (bool forward : directions) {
            size_t first, last;
            size_t length = strlen(entry.text);
            layout_text_shaping_context_slice(entry.text, length, forward, entry.cursive, &first, &last);
            const char* expected = forward ? entry.prefix : entry.suffix;
            ASSERT_LE(first, last);
            ASSERT_LE(last, length);
            ASSERT_EQ(last - first, strlen(expected));
            EXPECT_EQ(memcmp(entry.text + first, expected, strlen(expected)), 0);
            EXPECT_EQ(forward ? first : last, forward ? 0u : length);
        }
    }
    char long_source[65536];
    memset(long_source, 'x', sizeof(long_source));
    const char* suffix = " tail";
    memcpy(long_source + sizeof(long_source) - strlen(suffix), suffix, strlen(suffix));
    size_t first, last;
    layout_text_shaping_context_slice(long_source, sizeof(long_source), false, false, &first, &last);
    EXPECT_EQ(first, sizeof(long_source) - strlen("tail"));
    EXPECT_EQ(last, sizeof(long_source));
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
    IntrinsicSizes measure_physical_widths(DomElement* element, DomElement* flow_box = nullptr) {
        LayoutContext context = {};
        LayoutPassScope pass(&context, document, &ui);
        context.selected_view_tree = lam::up(document->view_tree.get());
        context.root_font_size = 16.0f;
        ViewBlock* caller = flow_box ? lam::view_as_block(flow_box) : nullptr;
        context.block.flow_box = lam::up(caller);
        IntrinsicSizes sizes = layout_measure_intrinsic_widths(&context, element, true);
        EXPECT_EQ(context.block.flow_box.get(), caller);
        return sizes;
    }
    void expect_inline_pair(const char* actual_id, const char* control_id) {
        DomElement* actual = dom_find_element_by_id(document->root->as_element(), actual_id);
        DomElement* control = dom_find_element_by_id(document->root->as_element(), control_id);
        ASSERT_NE(actual, nullptr);
        ASSERT_NE(control, nullptr);
        float actual_x = 0.0f, control_x = 0.0f;
        view_get_visual_bounds(actual, &actual_x, nullptr, nullptr, nullptr);
        view_get_visual_bounds(control, &control_x, nullptr, nullptr, nullptr);
        EXPECT_NEAR(actual_x, control_x, 0.01f) << actual_id;
        EXPECT_NEAR(actual->width, control->width, 0.01f) << actual_id;
        EXPECT_NEAR(actual->height, control->height, 0.01f) << actual_id;
    }
    void setup_text_paint_context(RenderContext* context, DomText* text) {
        *context = {};
        context->ui_context = lam::up(&ui);
        setup_font(&ui, &context->font, text->font);
    }
    bool visit_text_paint_glyphs(DomElement* block, DomText* text,
                                  RenderTextRunGlyphFn visitor, void* data) {
        RenderContext context = {};
        setup_text_paint_context(&context, text);
        RenderTextPaintScope paint(&context, lam::view_as_block(block), &context.font, &context.color);
        RenderTextPaintIterator vector(&context, lam::view_require_text(text));
        while (TextRect* rect = vector.next(true)) {
            PaintGlyphRun run = {};
            lam::Temp<char> content(render_text_rect_glyph_run(lam::view_require_text(text), rect,
                &context.font, context.color, 0, 0, &run, vector.reverses_clusters()));
            if (!content || !render_visit_text_run_glyphs(&run, ui.font_ctx, visitor, data)) return false;
        }
        return true;
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
    // consume a formatted fixture and use its declared browser-probe viewport.
    DomElement* layout_script_source(StrBuf* source, float width = 320, float height = 200) {
        DomElement* result = load_script_source(source->str);
        strbuf_free(source);
        if (!result) return nullptr;
        ui.viewport_width = ui.window_width = width;
        ui.viewport_height = ui.window_height = height;
        process_document_font_faces(&ui, document);
        layout_html_doc(&ui, document, false);
        return result;
    }
};

TEST(CounterFormatting, JapaneseInformalAndCjkDecimal) {
    const struct { int value; const char* expected; } cases[] = {
        {0,"〇"}, {1,"一"}, {10,"十"}, {11,"十一"}, {20,"二十"}, {99,"九十九"},
        {100,"百"}, {101,"百一"}, {1000,"千"}, {6001,"六千一"}, {9999,"九千九百九十九"},
        {-12,"マイナス十二"}, {10000,"一〇〇〇〇"}, {-10000,"-一〇〇〇〇"}
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.value);
        char buffer[64];
        EXPECT_EQ(counter_format_value(entry.value, CSS_VALUE_JAPANESE_INFORMAL, buffer, sizeof(buffer)), strlen(entry.expected));
        EXPECT_STREQ(buffer, entry.expected);
    }
    char buffer[64];
    counter_format_value(101, CSS_VALUE_CJK_DECIMAL, buffer, sizeof(buffer));
    EXPECT_STREQ(buffer, "一〇一");
    counter_format_value(INT32_MIN, CSS_VALUE_CJK_DECIMAL, buffer, sizeof(buffer));
    EXPECT_STREQ(buffer, "-二一四七四八三六四八");
    char short_buffer[4];
    EXPECT_EQ(counter_format_value(123, CSS_VALUE_CJK_DECIMAL, short_buffer, sizeof(short_buffer)), 9);
    EXPECT_STREQ(short_buffer, "一");
    EXPECT_STREQ(counter_style_suffix(CSS_VALUE_JAPANESE_INFORMAL), "、");
    EXPECT_STREQ(counter_style_suffix(CSS_VALUE_CJK_DECIMAL), "、");
    EXPECT_STREQ(counter_style_suffix(CSS_VALUE_DECIMAL), ". ");
}

TEST_F(RadiantTextQuery, JapaneseListMarkersRetainStyleAndRenumberAfterStartChanges) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>ol{list-style-type:japanese-informal}"
        "#digits{list-style-type:cjk-decimal}</style>"
        "<ol id=result start=9><li id=first>First</li><li id=second>Second</li></ol>"
        "<ol id=digits start=101><li id=third>Third</li></ol>");
    DomElement* list = layout_script_source(source);
    ASSERT_NE(list, nullptr);
    const struct { const char* id; const char* expected; } cases[] = {
        {"first","九、"}, {"second","十、"}, {"third","一〇一、"}
    };
    for (const auto& entry : cases) {
        DomElement* item = dom_find_element_by_id(document->root->as_element(), entry.id);
        ASSERT_NE(item, nullptr);
        ASSERT_NE(item->pseudo.get(), nullptr);
        ASSERT_NE(item->pseudo->marker.get(), nullptr);
        MarkerProp* marker = item->pseudo->marker->marker_prop();
        ASSERT_NE(marker, nullptr);
        EXPECT_STREQ(marker->text_content.get(), entry.expected);
        EXPECT_GT(marker->width, 0.0f);
    }
    ASSERT_TRUE(list->set_attribute("start", "99"));
    dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, list, list->parent);
    layout_html_doc(&ui, document, true);
    DomElement* second = dom_find_element_by_id(document->root->as_element(), "second");
    EXPECT_STREQ(second->pseudo->marker->marker_prop()->text_content.get(), "百、");
}

TEST_F(RadiantTextQuery, NestedSearchControlsUseTextareaRowsAndLineHeight) {
    ASSERT_NE(load("test/html/layout_search_control_intrinsics.html"), nullptr);
    ui.viewport_width = 480;
    ui.viewport_height = 360;
    layout_html_doc(&ui, document, false);
    const struct { const char* shell; const char* input; float height; } cases[] = {
        {"single", "single-input", 52.0f},
        {"multi", "multi-input", 90.0f},
        {"default", "default-input", 68.0f},
    };
    for (const auto& entry : cases) {
        DomElement* shell = dom_find_element_by_id(document->root->as_element(), entry.shell);
        DomElement* input = dom_find_element_by_id(document->root->as_element(), entry.input);
        ASSERT_NE(shell, nullptr);
        ASSERT_NE(input, nullptr);
        EXPECT_FLOAT_EQ(shell->height, entry.height) << entry.shell;
        EXPECT_FLOAT_EQ(input->height, entry.height - 2.0f) << entry.input;
    }
}

TEST_F(RadiantTextQuery, BorderRadiusShorthandInheritsThroughSearchWrappers) {
    ASSERT_NE(load("test/html/layout_search_control_intrinsics.html"), nullptr);
    layout_html_doc(&ui, document, false);
    for (const char* id : {"single", "row", "actions"}) {
        DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(element, nullptr);
        ASSERT_NE(element->bound, nullptr);
        ASSERT_NE(element->boundary()->border, nullptr);
        const Corner& radius = element->boundary()->border->radius;
        const Corner& computed = radius.expressions ? radius.expressions->computed : radius;
        for (int corner = 0; corner < 4; corner++) {
            EXPECT_FLOAT_EQ(computed.horizontal[corner], 26.0f) << id << ":" << corner;
            EXPECT_FLOAT_EQ(computed.vertical[corner], 26.0f) << id << ":" << corner;
        }
    }
}

TEST_F(RadiantTextQuery, SearchActionCentersItselfAndItsFlowLabel) {
    ASSERT_NE(load("test/html/layout_search_control_intrinsics.html"), nullptr);
    layout_html_doc(&ui, document, false);
    DomElement* row = dom_find_element_by_id(document->root->as_element(), "button-row");
    DomElement* button = dom_find_element_by_id(document->root->as_element(), "action");
    DomElement* label = dom_find_element_by_id(document->root->as_element(), "label");
    ASSERT_NE(row, nullptr);
    ASSERT_NE(button, nullptr);
    ASSERT_NE(label, nullptr);
    LayoutContext context = {};
    context.ui_context = lam::up(&ui);
    FontProp font = *button->fontp();
    context.font.style = lam::up(&font);
    EXPECT_FLOAT_EQ(layout_resolve_intrinsic_margin_side(
        &context, lam::view_require_element(static_cast<View*>(button)),
        CSS_PROPERTY_MARGIN_LEFT, -1.0f, false), 8.0f);
    float row_y = 0.0f, button_y = 0.0f, label_y = 0.0f;
    view_get_visual_bounds(row, nullptr, &row_y, nullptr, nullptr);
    view_get_visual_bounds(button, nullptr, &button_y, nullptr, nullptr);
    view_get_visual_bounds(label, nullptr, &label_y, nullptr, nullptr);
    EXPECT_FLOAT_EQ(row->height, 52.0f);
    EXPECT_FLOAT_EQ(button->height, 36.0f);
    EXPECT_FLOAT_EQ(button_y - row_y, 8.0f);
    EXPECT_FLOAT_EQ(label->height, 20.0f);
    EXPECT_FLOAT_EQ(label_y - button_y, 8.0f);
}

TEST_F(RadiantTextQuery, SearchTextareaCaretUsesItsOwnVerticalPadding) {
    ASSERT_NE(load("test/html/layout_search_control_intrinsics.html"), nullptr);
    layout_html_doc(&ui, document, false);
    DomElement* input = dom_find_element_by_id(document->root->as_element(), "single-input");
    ASSERT_NE(input, nullptr);
    EditingCaretRect caret = {};
    ASSERT_TRUE(editing_geometry_text_control_caret_rect(&ui, input, 0, &caret));
    EXPECT_FLOAT_EQ(caret.x - input->x, 0.0f);
    EXPECT_FLOAT_EQ(caret.y - input->y, 14.0f);
    Rect content = editing_geometry_text_control_content_box(lam::view_require_block(input));
    EXPECT_FLOAT_EQ(content.height, input->height - 22.0f);
}

TEST_F(RadiantTextQuery, OrthogonalAutoInlineSizesUseFitContentAndDefiniteOrViewportSpace) {
    const struct { const char* mode; const char* parent_height; const char* text; float width, height; } cases[] = {
        {"vertical-lr", "auto", "short", 30, 50},
        {"vertical-lr", "auto", "unbreakable", 30, 250},
        {"vertical-lr", "auto", "wrapping", 90, 200},
        {"vertical-lr", "120px", "short", 30, 50},
        {"vertical-lr", "120px", "unbreakable", 30, 250},
        {"vertical-lr", "120px", "wrapping", 130, 120},
        {"vertical-lr", "0px", "short", 30, 50},
        {"vertical-lr", "0px", "unbreakable", 30, 250},
        {"vertical-lr", "0px", "wrapping", 250, 50},
        {"vertical-lr", "400px", "short", 30, 50},
        {"vertical-lr", "400px", "unbreakable", 30, 250},
        {"vertical-lr", "400px", "wrapping", 50, 400},
        {"vertical-rl", "auto", "short", 30, 50},
        {"vertical-rl", "auto", "unbreakable", 30, 250},
        {"vertical-rl", "auto", "wrapping", 90, 200},
        {"vertical-rl", "120px", "short", 30, 50},
        {"vertical-rl", "120px", "unbreakable", 30, 250},
        {"vertical-rl", "120px", "wrapping", 130, 120},
        {"vertical-rl", "0px", "short", 30, 50},
        {"vertical-rl", "0px", "unbreakable", 30, 250},
        {"vertical-rl", "0px", "wrapping", 250, 50},
        {"vertical-rl", "400px", "short", 30, 50},
        {"vertical-rl", "400px", "unbreakable", 30, 250},
        {"vertical-rl", "400px", "wrapping", 50, 400},
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.mode);
        SCOPED_TRACE(entry.parent_height);
        SCOPED_TRACE(entry.text);
        const char* text = strcmp(entry.text, "short") == 0 ? "AA"
            : strcmp(entry.text, "unbreakable") == 0 ? "AAAAAAAAAAAA"
            : "AA AA AA AA AA AA AA AA AA AA AA AA";
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#parent{width:300px;height:%s}#result{font:20px/1 Ahem;"
            "writing-mode:%s;border:2px solid;padding:3px}</style>"
            "<div id=parent><div id=result>%s</div></div>", entry.parent_height, entry.mode, text);
        ASSERT_NE(layout_script_source(source), nullptr);
        expect_box("result", 0, 0, entry.width, entry.height);
        if (strcmp(entry.parent_height, "auto") == 0 && strcmp(entry.text, "wrapping") == 0) {
            DomElement* parent = dom_find_element_by_id(document->root->as_element(), "parent");
            ASSERT_NE(parent, nullptr);
            ASSERT_TRUE(dom_cssom_set_inline_property(parent, "height", "120px"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            expect_box("result", 0, 0, 130, 120);
            ASSERT_TRUE(dom_cssom_set_inline_property(parent, "height", "auto"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            expect_box("result", 0, 0, 90, 200);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, OrthogonalFallbackUsesContainingLimitsAndNearestScrollport) {
    // scrollport rows follow CSS Writing Modes 3 §7.3.1; Chromium's explicit
    // height controls verify their columns independently of its fallback gap.
    const struct {
        const char* mode; const char* name; const char* outer_css; const char* parent_css;
        const char* box_css; const char* text; float width, height;
    } cases[] = {
        {"vertical-lr", "parent-max", "", "max-height:120px", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 130, 120},
        {"vertical-lr", "parent-minmax", "", "min-height:140px;max-height:80px", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 130, 140},
        {"vertical-lr", "fixed-nonscroller", "height:80px", "", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 90, 200},
        {"vertical-lr", "fixed-scrollport", "height:120px;overflow:auto", "", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 130, 120},
        {"vertical-lr", "max-scrollport", "max-height:100px;overflow:auto", "", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 250, 100},
        {"vertical-lr", "nowrap", "", "", "white-space:nowrap", "AA AA AA AA AA AA AA AA AA AA AA AA", 30, 710},
        {"vertical-lr", "breaks", "", "", "", "AA<br>AA", 50, 50},
        {"vertical-lr", "border-box", "", "", "box-sizing:border-box", "AA AA AA AA AA AA AA AA AA AA AA AA", 90, 200},
        {"vertical-rl", "parent-max", "", "max-height:120px", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 130, 120},
        {"vertical-rl", "parent-minmax", "", "min-height:140px;max-height:80px", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 130, 140},
        {"vertical-rl", "fixed-nonscroller", "height:80px", "", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 90, 200},
        {"vertical-rl", "fixed-scrollport", "height:120px;overflow:auto", "", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 130, 120},
        {"vertical-rl", "max-scrollport", "max-height:100px;overflow:auto", "", "", "AA AA AA AA AA AA AA AA AA AA AA AA", 250, 100},
        {"vertical-rl", "nowrap", "", "", "white-space:nowrap", "AA AA AA AA AA AA AA AA AA AA AA AA", 30, 710},
        {"vertical-rl", "breaks", "", "", "", "AA<br>AA", 50, 50},
        {"vertical-rl", "border-box", "", "", "box-sizing:border-box", "AA AA AA AA AA AA AA AA AA AA AA AA", 90, 200},
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.mode);
        SCOPED_TRACE(entry.name);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#outer{width:300px;%s}#parent{%s}#result{font:20px/1 Ahem;"
            "writing-mode:%s;border:2px solid;padding:3px;%s}</style>"
            "<div id=outer><div id=parent><div id=result>%s</div></div></div>",
            entry.outer_css, entry.parent_css, entry.mode, entry.box_css, entry.text);
        ASSERT_NE(layout_script_source(source), nullptr);
        expect_box("result", 0, 0, entry.width, entry.height);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, HorizontalOrthogonalFitContentUsesTheParentsBlockAxis) {
    const struct { const char* mode; const char* parent_width; const char* text; float x, width, height; } cases[] = {
        {"vertical-lr", "auto", "short", 0, 50, 30},
        {"vertical-lr", "auto", "unbreakable", 0, 490, 30},
        {"vertical-lr", "auto", "wrapping", 0, 320, 70},
        {"vertical-lr", "120px", "short", 0, 50, 30},
        {"vertical-lr", "120px", "unbreakable", 0, 490, 30},
        {"vertical-lr", "120px", "wrapping", 0, 120, 130},
        {"vertical-lr", "0px", "short", 0, 50, 30},
        {"vertical-lr", "0px", "unbreakable", 0, 490, 30},
        {"vertical-lr", "0px", "wrapping", 0, 50, 250},
        {"vertical-lr", "400px", "short", 0, 50, 30},
        {"vertical-lr", "400px", "unbreakable", 0, 490, 30},
        {"vertical-lr", "400px", "wrapping", 0, 400, 50},
        {"vertical-rl", "auto", "short", 0, 50, 30},
        {"vertical-rl", "auto", "unbreakable", 0, 490, 30},
        {"vertical-rl", "auto", "wrapping", 0, 320, 70},
        {"vertical-rl", "120px", "short", 70, 50, 30},
        {"vertical-rl", "120px", "unbreakable", -370, 490, 30},
        {"vertical-rl", "120px", "wrapping", 0, 120, 130},
        {"vertical-rl", "0px", "short", -50, 50, 30},
        {"vertical-rl", "0px", "unbreakable", -490, 490, 30},
        {"vertical-rl", "0px", "wrapping", -50, 50, 250},
        {"vertical-rl", "400px", "short", 350, 50, 30},
        {"vertical-rl", "400px", "unbreakable", -90, 490, 30},
        {"vertical-rl", "400px", "wrapping", 0, 400, 50},
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.mode);
        SCOPED_TRACE(entry.parent_width);
        SCOPED_TRACE(entry.text);
        const char* text = strcmp(entry.text, "short") == 0 ? "AA"
            : strcmp(entry.text, "unbreakable") == 0 ? "AAAAAAAAAAAAAAAAAAAAAAAA"
            : "AA AA AA AA AA AA AA AA AA AA AA AA";
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#parent{writing-mode:%s;width:%s;height:300px}"
            "#result{writing-mode:horizontal-tb;font:20px/1 Ahem;border:2px solid;padding:3px}</style>"
            "<div id=parent><div id=result>%s</div></div>", entry.mode, entry.parent_width, text);
        ASSERT_NE(layout_script_source(source), nullptr);
        expect_box("result", entry.x, 0, entry.width, entry.height);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalSiblingCursorConsumesOrthogonalBlocksOnce) {
    for (const char* mode : {"vertical-lr", "vertical-rl"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#parent{writing-mode:%s;height:300px}"
            "#result{writing-mode:horizontal-tb;font:20px/1 Ahem;border:2px solid;padding:3px}</style>"
            "<div id=parent><div id=before style='width:20px;height:30px'></div>"
            "<div id=result>AA AA AA AA AA AA AA AA AA AA AA AA</div>"
            "<div id=after style='width:30px;height:30px'></div></div>", mode);
        ASSERT_NE(layout_script_source(source), nullptr);
        bool reverse = strcmp(mode, "vertical-rl") == 0;
        expect_box("parent", 0, 0, 370, 300);
        expect_box("before", reverse ? 350 : 0, 0, 20, 30);
        expect_box("result", reverse ? 30 : 20, 0, 320, 70);
        expect_box("after", reverse ? 0 : 340, 0, 30, 30);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalAutoInlineSizeKeepsPhysicalDecorationsAndBodyConstraints) {
    const struct { const char* css; float width, narrow_width, height, top, left, right; } cases[] = {
        {"", 60, 80, 180, 0, 0, 0},
        {"border-top:20px solid;border-bottom:20px solid", 80, 140, 180, 0, 0, 0},
        {"border-left:10px solid;border-right:30px solid", 100, 120, 180, 0, 0, 0},
        {"padding:15px 5px 25px 10px", 95, 155, 180, 0, 0, 0},
        {"border:2px solid;padding:15px 5px 25px 10px;margin:3px 4px 7px 6px", 99, 159, 170, 3, 6, 4},
        {"box-sizing:border-box;border:2px solid;padding:15px 5px 25px 10px;"
         "margin:3px 4px 7px 6px", 99, 159, 170, 3, 6, 4}
    };
    for (const char* mode : {"vertical-lr", "vertical-rl"}) {
        for (bool root_flow : {false, true}) {
            for (const auto& entry : cases) {
                SCOPED_TRACE(mode);
                SCOPED_TRACE(root_flow);
                SCOPED_TRACE(entry.css);
                StrBuf* source = strbuf_new();
                strbuf_append_format(source,
                    "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
                    "body{margin:0;font:20px/1 Ahem}%s{writing-mode:%s}#parent{height:180px}"
                    "#result{%s}</style>%s<div id=result>AA AA AA AA AA AA AA</div>%s",
                    root_flow ? "html" : "#parent", mode, entry.css,
                    root_flow ? "<body id=parent>" : "<div id=parent>", root_flow ? "" : "</div>");
                ASSERT_NE(layout_script_source(source), nullptr);
                bool reverse = strcmp(mode, "vertical-rl") == 0;
                float x = root_flow && reverse ? 320 - entry.width - entry.right : entry.left;
                expect_box("result", x, entry.top, entry.width, entry.height);
                DomElement* parent = dom_find_element_by_id(document->root->as_element(), "parent");
                ASSERT_NE(parent, nullptr);
                EXPECT_FLOAT_EQ(parent->height, 180);
                for (bool narrow : {true, false}) {
                    ASSERT_TRUE(dom_cssom_set_inline_property(parent, "height", narrow ? "120px" : "180px"));
                    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                    float width = narrow ? entry.narrow_width : entry.width;
                    float updated_x = root_flow && reverse ? 320 - width - entry.right : entry.left;
                    expect_box("result", updated_x, entry.top, width, entry.height - (narrow ? 60 : 0));
                    EXPECT_FLOAT_EQ(parent->height, narrow ? 120 : 180);
                }
                ui.document = nullptr;
                free_document(document);
                document = nullptr;
            }
        }
    }
}

TEST_F(RadiantTextQuery, VerticalAutoBlockSizeRetainsMinMaxAndBoxEdges) {
    struct State {
        bool visible;
        const char* child_width;
        float natural_width;
        const char* minimum;
        float min_width;
        const char* maximum;
        float max_width;
        const char* height;
        float css_height;
    };
    const State states[] = {
        {false, "23px", 0, "50px", 50, "none", -1, "40px", 40},
        {true, "80px", 80, "50px", 50, "none", -1, "70px", 70},
        {true, "80px", 80, "50px", 50, "60px", 60, "40px", 40},
        {true, "80px", 80, "90px", 90, "60px", 60, "70px", 70},
        {true, "80px", 80, "0px", 0, "60px", 60, "40px", 40},
        {false, "23px", 0, "0px", 0, "4px", 4, "70px", 70},
        {true, "23px", 23, "50px", 50, "none", -1, "40px", 40}
    };
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl"}) for (bool border_box : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(border_box);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0;font:10px/10px Ahem}#outer{position:absolute}"
            "#result{writing-mode:%s;box-sizing:%s;height:40px;padding:2px 7px 4px 3px;"
            "border:2px solid;min-width:50px}#child{width:23px;height:20px;display:none}"
            "</style><div id=outer><div id=result><div id=child></div></div></div>",
            mode, border_box ? "border-box" : "content-box");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* outer = dom_find_element_by_id(document->root->as_element(), "outer");
        DomElement* child = dom_find_element_by_id(result, "child");
        ASSERT_NE(outer, nullptr);
        ASSERT_NE(child, nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.minimum);
            SCOPED_TRACE(state.maximum);
            SCOPED_TRACE(state.child_width);
            SCOPED_TRACE(state.visible);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "min-width", state.minimum));
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "max-width", state.maximum));
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "height", state.height));
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "display", state.visible ? "block" : "none"));
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "width", state.child_width));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // min/max constrain the used box, while an oversized child retains its physical extent.
            float decoration = 14.0f;
            float minimum = state.min_width + (border_box ? 0.0f : decoration);
            float width = state.natural_width + decoration;
            if (state.max_width >= 0.0f)
                width = min(width, state.max_width + (border_box ? 0.0f : decoration));
            width = max(max(width, minimum), decoration);
            float height = state.css_height + (border_box ? 0.0f : 10.0f);
            EXPECT_FLOAT_EQ(result->width, width);
            EXPECT_FLOAT_EQ(result->height, height);
            EXPECT_FLOAT_EQ(outer->width, width);
            EXPECT_FLOAT_EQ(outer->height, height);
            if (state.visible) {
                EXPECT_FLOAT_EQ(child->width, state.natural_width);
                EXPECT_FLOAT_EQ(child->height, 20.0f);
                EXPECT_FLOAT_EQ(child->x, strcmp(mode, "vertical-rl") == 0
                    ? width - 9.0f - state.natural_width : 5.0f);
                EXPECT_FLOAT_EQ(child->y, 4.0f);
            }
            IntrinsicSizes contribution = measure_physical_widths(result, outer);
            EXPECT_FLOAT_EQ(contribution.min_content, width);
            EXPECT_FLOAT_EQ(contribution.max_content, width);
            EXPECT_FLOAT_EQ(result->width, width);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, NormalVerticalBlocksUseContainingInlineMarginsAndDirection) {
    struct State { const char* top; const char* bottom; float height; const char* alignment; };
    const State states[] = {
        {"7px", "11px", 30, "normal"}, {"-9px", "13px", 30, "normal"},
        {"10%", "20%", 30, "normal"}, {"auto", "11px", 30, "normal"},
        {"7px", "auto", 30, "normal"}, {"auto", "auto", 30, "normal"},
        {"7px", "11px", 150, "normal"}, {"auto", "auto", 150, "normal"},
        {"7px", "11px", 30, "end"}, {"7px", "11px", 30, "center"},
        {"7px", "11px", 30, "normal"}
    };
    for (const char* mode : {"vertical-lr", "vertical-rl", "sideways-lr", "sideways-rl"})
    for (const char* direction : {"ltr", "rtl"})
    for (const char* child_mode : {"horizontal-tb", "vertical-lr", "vertical-rl"})
    for (const char* child_direction : {"ltr", "rtl"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        SCOPED_TRACE(child_direction);
        SCOPED_TRACE(child_mode);
        bool reverse = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "sideways-rl") == 0;
        bool reverse_inline = (strcmp(direction, "rtl") == 0) != (strcmp(mode, "sideways-lr") == 0);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{writing-mode:%s;direction:%s;"
            "width:180px;height:120px;padding:3px 5px 7px 11px;border:2px solid}"
            "#child{writing-mode:%s;direction:%s;width:40px;height:30px;"
            "padding:2px;border:1px solid}#marker{writing-mode:horizontal-tb;width:9px;height:6px}"
            "#after{writing-mode:horizontal-tb;width:23px;height:20px}</style>"
            "<div id=result><div id=child><div id=marker></div></div><div id=after></div></div>",
            mode, direction, child_mode, child_direction);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* child = dom_find_element_by_id(result, "child");
        DomElement* after = dom_find_element_by_id(result, "after");
        DomElement* marker = dom_find_element_by_id(result, "marker");
        ASSERT_NE(child, nullptr);
        ASSERT_NE(after, nullptr);
        ASSERT_NE(marker, nullptr);
        for (float inline_size : {120.0f, 160.0f}) for (const State& state : states) {
            SCOPED_TRACE(inline_size);
            SCOPED_TRACE(state.top);
            SCOPED_TRACE(state.bottom);
            SCOPED_TRACE(state.height);
            SCOPED_TRACE(state.alignment);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "height", inline_size == 120 ? "120px" : "160px"));
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "margin-top", state.top));
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "margin-bottom", state.bottom));
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "height", state.height == 30 ? "30px" : "150px"));
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "justify-self", state.alignment));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool auto_top = strcmp(state.top, "auto") == 0;
            bool auto_bottom = strcmp(state.bottom, "auto") == 0;
            float top = auto_top ? 0 : strcmp(state.top, "10%") == 0 ? inline_size * .1f
                : strcmp(state.top, "-9px") == 0 ? -9 : 7;
            float bottom = auto_bottom ? 0 : strcmp(state.bottom, "20%") == 0 ? inline_size * .2f
                : strcmp(state.bottom, "13px") == 0 ? 13 : 11;
            float height = state.height + 6;
            float free_space = inline_size - height - top - bottom;
            // CSS2 inline auto margins consume positive free space; overconstraint keeps inline-start.
            if (auto_top || auto_bottom) {
                if (free_space >= 0 && auto_top) top += free_space / (auto_bottom ? 2 : 1);
                else if (free_space < 0 && reverse_inline) top += free_space;
            } else if (strcmp(state.alignment, "normal") == 0 && reverse_inline) top += free_space;
            else if (strcmp(state.alignment, "end") == 0 && !reverse_inline) top += free_space;
            else if (strcmp(state.alignment, "center") == 0) top += free_space / 2;
            EXPECT_FLOAT_EQ(result->width, 200);
            EXPECT_FLOAT_EQ(result->height, inline_size + 14);
            EXPECT_FLOAT_EQ(child->x, reverse ? 147 : 13);
            EXPECT_NEAR(child->y, 5 + top, .02f);
            EXPECT_FLOAT_EQ(child->width, 46);
            EXPECT_FLOAT_EQ(child->height, height);
            EXPECT_FLOAT_EQ(after->x, reverse ? 124 : 59);
            EXPECT_FLOAT_EQ(after->y, 5 + (reverse_inline ? inline_size - 20 : 0));
            EXPECT_FLOAT_EQ(after->width, 23);
            EXPECT_FLOAT_EQ(after->height, 20);
            bool horizontal_child = strcmp(child_mode, "horizontal-tb") == 0;
            bool child_rtl = strcmp(child_direction, "rtl") == 0;
            EXPECT_FLOAT_EQ(marker->x, (horizontal_child ? child_rtl : strcmp(child_mode, "vertical-rl") == 0) ? 34 : 3);
            EXPECT_FLOAT_EQ(marker->y, !horizontal_child && child_rtl ? state.height - 3 : 3);
            EXPECT_FLOAT_EQ(marker->width, 9);
            EXPECT_FLOAT_EQ(marker->height, 6);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalAutomaticInlineExtentPrecedesBlockPlacement) {
    struct State { const char* minimum; const char* maximum; float extent; };
    const State states[] = {{"0px", "none", 58}, {"120px", "none", 120},
        {"0px", "25px", 25}, {"0px", "none", 58}};
    for (const char* mode : {"vertical-lr", "vertical-rl", "sideways-lr", "sideways-rl"})
    for (const char* direction : {"ltr", "rtl"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{float:right;writing-mode:%s;direction:%s;"
            "width:180px;padding:3px 5px 7px 11px;border:2px solid}"
            "#child{width:46px;height:40px;margin-top:7px;margin-bottom:11px}</style>"
            "<div id=result><div id=child></div></div>", mode, direction);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* child = dom_find_element_by_id(result, "child");
        ASSERT_NE(child, nullptr);
        bool reverse = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "sideways-rl") == 0;
        bool reverse_inline = (strcmp(direction, "rtl") == 0) != (strcmp(mode, "sideways-lr") == 0);
        for (const State& state : states) {
            SCOPED_TRACE(state.minimum);
            SCOPED_TRACE(state.maximum);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "min-height", state.minimum));
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "max-height", state.maximum));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FLOAT_EQ(result->width, 200);
            EXPECT_FLOAT_EQ(result->height, state.extent + 14);
            EXPECT_FLOAT_EQ(child->x, reverse ? 147 : 13);
            EXPECT_FLOAT_EQ(child->y, 5 + (reverse_inline ? state.extent - 40 - 11 : 7));
            EXPECT_FLOAT_EQ(child->width, 46);
            EXPECT_FLOAT_EQ(child->height, 40);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, ColumnProgressionPreservesResolvedInlineMargins) {
    for (const char* direction : {"ltr", "rtl"}) for (bool decorated : {false, true}) {
        SCOPED_TRACE(direction);
        SCOPED_TRACE(decorated);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{width:220px;height:20px;columns:2;"
            "column-gap:20px;column-fill:auto;direction:%s}.child{box-sizing:border-box;"
            "width:40px;height:20px;border:%s}</style>"
            "<div id=result><div id=a class=child></div><div id=b class=child></div></div>",
            direction, decorated ? "1px solid" : "0");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* children[] = {dom_find_element_by_id(result, "a"), dom_find_element_by_id(result, "b")};
        ASSERT_NE(children[0], nullptr);
        ASSERT_NE(children[1], nullptr);
        bool rtl = strcmp(direction, "rtl") == 0;
        for (float width : {220.0f, 260.0f}) for (const char* margin : {"0", "7px 11px", "auto"}) {
            SCOPED_TRACE(width);
            SCOPED_TRACE(margin);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "width", width == 220 ? "220px" : "260px"));
            for (DomElement* child : children) {
                ASSERT_TRUE(dom_cssom_set_inline_property(child, "margin-left", strcmp(margin, "7px 11px") == 0 ? "7px" : margin));
                ASSERT_TRUE(dom_cssom_set_inline_property(child, "margin-right", strcmp(margin, "7px 11px") == 0 ? "11px" : margin));
            }
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float column = (width - 20) / 2;
            float offset = strcmp(margin, "auto") == 0 ? (column - 40) / 2
                : strcmp(margin, "0") == 0 ? 0 : rtl ? 11 : 7;
            for (size_t index = 0; index < 2; index++) {
                float column_offset = (float)index * (column + 20);
                EXPECT_FLOAT_EQ(children[index]->x, rtl ? width - 40 - offset - column_offset : offset + column_offset);
                EXPECT_FLOAT_EQ(children[index]->y, 0);
                EXPECT_FLOAT_EQ(children[index]->width, 40);
                EXPECT_FLOAT_EQ(children[index]->height, 20);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, BlockFlowUsesChildBoxRatherThanDescendantOverflow) {
    struct State { const char* used; float block_size; const char* content; float content_size; };
    const State states[] = {{"40px", 40, "90px", 90}, {"70px", 70, "30px", 30},
        {"40px", 40, "110px", 110}, {"auto", -1, "90px", 90}, {"40px", 40, "90px", 90}};
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl"}) for (bool decorated : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(decorated);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        bool reverse = strcmp(mode, "vertical-rl") == 0;
        const char* block_axis = vertical ? "width" : "height";
        const char* inline_axis = vertical ? "height" : "width";
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0;font:10px/10px Ahem}"
            "#result{writing-mode:%s;%s:120px}#before{%s:11px;%s:20px}"
            "#after{%s:17px;%s:20px}#child{%s:40px;%s:60px;padding:%s;border:%s}"
            "#content{%s:90px;%s:20px}</style><div id=result><div id=before></div>"
            "<div id=child><div id=content></div></div><div id=after></div></div>",
            mode, inline_axis, block_axis, inline_axis, block_axis, inline_axis,
            block_axis, inline_axis, decorated ? "3px 5px 7px 9px" : "0",
            decorated ? "2px solid" : "0", block_axis, inline_axis);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* before = dom_find_element_by_id(result, "before");
        DomElement* child = dom_find_element_by_id(result, "child");
        DomElement* content = dom_find_element_by_id(result, "content");
        DomElement* after = dom_find_element_by_id(result, "after");
        ASSERT_NE(before, nullptr);
        ASSERT_NE(child, nullptr);
        ASSERT_NE(content, nullptr);
        ASSERT_NE(after, nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.used);
            SCOPED_TRACE(state.content);
            ASSERT_TRUE(dom_cssom_set_inline_property(child, block_axis, state.used));
            ASSERT_TRUE(dom_cssom_set_inline_property(content, block_axis, state.content));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float decoration_x = decorated ? 18.0f : 0.0f;
            float decoration_y = decorated ? 14.0f : 0.0f;
            float natural_block = state.block_size < 0.0f ? state.content_size : state.block_size;
            float child_width = vertical ? natural_block + decoration_x : 60.0f + decoration_x;
            float child_height = vertical ? 60.0f + decoration_y : natural_block + decoration_y;
            float parent_block = 28.0f + (vertical ? child_width : child_height);
            // normal-flow advancement uses the child's margin box even when its content overflows.
            EXPECT_FLOAT_EQ(result->width, vertical ? parent_block : 120.0f);
            EXPECT_FLOAT_EQ(result->height, vertical ? 120.0f : parent_block);
            EXPECT_FLOAT_EQ(before->x, reverse ? parent_block - 11.0f : 0.0f);
            EXPECT_FLOAT_EQ(before->y, 0.0f);
            EXPECT_FLOAT_EQ(child->width, child_width);
            EXPECT_FLOAT_EQ(child->height, child_height);
            EXPECT_FLOAT_EQ(child->x, reverse ? 17.0f : vertical ? 11.0f : 0.0f);
            EXPECT_FLOAT_EQ(child->y, vertical ? 0.0f : 11.0f);
            EXPECT_FLOAT_EQ(after->x, reverse || !vertical ? 0.0f : 11.0f + child_width);
            EXPECT_FLOAT_EQ(after->y, vertical ? 0.0f : 11.0f + child_height);
            EXPECT_FLOAT_EQ(content->width, vertical ? state.content_size : 20.0f);
            EXPECT_FLOAT_EQ(content->height, vertical ? 20.0f : state.content_size);
            float content_x = reverse
                ? child_width - (decorated ? 7.0f : 0.0f) - state.content_size
                : decorated ? 11.0f : 0.0f;
            float content_y = decorated ? 5.0f : 0.0f;
            EXPECT_FLOAT_EQ(content->x, content_x);
            EXPECT_FLOAT_EQ(content->y, content_y);
            float overflow_min = 0.0f, overflow_max = 0.0f;
            layout_in_flow_content_bounds(lam::view_as_element(child),
                vertical ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y, true, &overflow_min, &overflow_max);
            float content_start = vertical ? content_x : content_y;
            EXPECT_FLOAT_EQ(overflow_min, min(content_start, 0.0f));
            EXPECT_FLOAT_EQ(overflow_max, max(content_start + state.content_size, 0.0f));
            if (vertical) {
                IntrinsicSizes intrinsic = measure_physical_widths(result);
                EXPECT_FLOAT_EQ(intrinsic.min_content, parent_block);
                EXPECT_FLOAT_EQ(intrinsic.max_content, parent_block);
                EXPECT_FLOAT_EQ(result->width, parent_block);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalAtomicOverflowParticipatesThroughLineBaselines) {
    struct State { const char* width_css; float width; const char* line_height_css; float line_height; };
    const State states[] = {{"10px", 10, "13px", 13}, {"10px", 10, "14px", 14},
        {"100px", 100, "14px", 14}, {"100px", 100, "13px", 13}, {"10px", 10, "13px", 13}};
    for (const char* mode : {"vertical-lr", "vertical-rl"})
        for (const char* align : {"bottom", "baseline"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(align);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}.outer{writing-mode:%s;display:inline-block}"
            "#result{display:inline-block;vertical-align:%s;height:80px;border:2px solid;font:8px/13px Ahem}"
            ".atom{display:inline-block;width:10px;border:2px solid}</style><div class=outer><div id=result>"
            "<div class=atom>10px<br>container: min-content.</div> "
            "<div class=atom>10px<br>container: min-content.</div></div></div>", mode, align);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.width_css);
            SCOPED_TRACE(state.line_height_css);
            float width = state.width;
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "line-height", state.line_height_css));
            for (DomNode* node = result->first_child; node; node = node->next_sibling) {
                if (node->is_element()) ASSERT_TRUE(dom_cssom_set_inline_property(node->as_element(), "width", state.width_css));
            }
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // the final overflowing Ahem line exports a baseline beyond the atom's small used width.
            float advance = max(width + 4.0f, 4.0f * state.line_height + 2.0f);
            float expected_width = 4.0f + 2.0f * advance;
            EXPECT_FLOAT_EQ(result->width, expected_width);
            EXPECT_FLOAT_EQ(result->height, 84.0f);
            float index = 0.0f;
            for (DomNode* node = result->first_child; node; node = node->next_sibling) {
                if (!node->is_element()) continue;
                DomElement* atom = node->as_element();
                EXPECT_FLOAT_EQ(atom->width, width + 4.0f);
                EXPECT_FLOAT_EQ(atom->height, 84.0f);
                EXPECT_FLOAT_EQ(atom->x, strcmp(mode, "vertical-rl") == 0
                    ? expected_width - 2.0f - width - 4.0f - index * advance : 2.0f + index * advance);
                EXPECT_FLOAT_EQ(atom->y, 2.0f);
                index += 1.0f;
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalAtomicAutoInlineSizePreservesItsMinContentOverflow) {
    for (const char* mode : {"vertical-lr", "vertical-rl"}) {
        for (const char* parent_display : {"block", "inline-block"}) {
            for (const char* atomic_css : {"display:inline-block", "float:left", "display:block"}) {
                SCOPED_TRACE(mode);
                SCOPED_TRACE(parent_display);
                SCOPED_TRACE(atomic_css);
                StrBuf* source = strbuf_new();
                strbuf_append_format(source,
                    "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
                    "body{margin:0}#parent{writing-mode:%s;display:%s;height:40px;font:20px/1 Ahem}"
                    "#result{%s;border:2px solid}</style><div id=parent><div id=result>AAAA</div></div>",
                    mode, parent_display, atomic_css);
                ASSERT_NE(layout_script_source(source), nullptr);
                DomElement* result = dom_find_element_by_id(document->root->as_element(), "result");
                ASSERT_NE(result, nullptr);
                EXPECT_FLOAT_EQ(result->width, 24);
                EXPECT_FLOAT_EQ(result->height, strcmp(atomic_css, "display:block") == 0 ? 40 : 84);
                ui.document = nullptr;
                free_document(document);
                document = nullptr;
            }
        }
    }
}

TEST_F(RadiantTextQuery, OrthogonalPercentageInlineSizesUseDefiniteOrFallbackSpace) {
    const struct { const char* name; const char* parent; const char* outer;
        float initial_basis, fallback; bool scrollport; } cases[] = {
        {"auto", "", "", -1, -1, false},
        {"definite", "%s:160px", "", 160, -1, false},
        {"zero", "%s:0", "", 0, -1, false},
        {"maximum", "max-%s:120px", "", 120, 120, false},
        {"minmax", "min-%s:140px;max-%s:80px", "", 140, 140, false},
        {"scrollport", "", "%s:120px;overflow:auto", 120, 120, true},
        {"non-scroll", "", "%s:120px", -1, -1, false}
    };
    for (bool vertical : {false, true}) for (const char* mode : {"vertical-lr", "vertical-rl"}) {
        for (const auto& entry : cases) for (bool math : {false, true})
        for (bool border_box : {false, true}) for (const char* limit : {"", "min-%s:80%%;", "max-%s:40%%;"}) {
            SCOPED_TRACE(vertical);
            SCOPED_TRACE(mode);
            SCOPED_TRACE(entry.name);
            SCOPED_TRACE(math);
            SCOPED_TRACE(border_box);
            SCOPED_TRACE(limit);
            const char* axis = vertical ? "height" : "width";
            StrBuf* source = strbuf_new();
            strbuf_append_str(source, "<!doctype html><style>body{margin:0}#outer{");
            strbuf_append_format(source, entry.outer, axis, axis);
            strbuf_append_format(source, "}#parent{writing-mode:%s;", vertical ? "horizontal-tb" : mode);
            strbuf_append_format(source, entry.parent, axis, axis);
            strbuf_append_format(source,
                "}#result{writing-mode:%s;%s:20px;%s:%s;box-sizing:%s;"
                "padding:3px;border:2px solid;",
                vertical ? mode : "horizontal-tb", vertical ? "width" : "height",
                axis, math ? "calc(50% + 10px)" : "50%",
                border_box ? "border-box" : "content-box");
            strbuf_append_format(source, limit, axis);
            strbuf_append_str(source, "}</style><div id=outer><div id=parent><div id=result></div></div></div>");
            ASSERT_NE(layout_script_source(source), nullptr);
            DomElement* parent = dom_find_element_by_id(document->root->as_element(), "parent");
            DomElement* outer = dom_find_element_by_id(document->root->as_element(), "outer");
            DomElement* result = dom_find_element_by_id(document->root->as_element(), "result");
            ASSERT_NE(parent, nullptr);
            ASSERT_NE(outer, nullptr);
            ASSERT_NE(result, nullptr);
            for (int phase = 0; phase < (entry.scrollport ? 5 : 4); phase++) {
                SCOPED_TRACE(phase);
                if (phase == 1 || phase == 2) {
                    ASSERT_TRUE(dom_cssom_set_inline_property(parent, axis, phase == 1 ? "80px" : "auto"));
                } else if (phase == 3) {
                    ui.viewport_width = ui.window_width = 400;
                    ui.viewport_height = ui.window_height = 240;
                    layout_html_doc(&ui, document, false);
                } else if (phase == 4) {
                    ASSERT_TRUE(dom_cssom_set_inline_property(outer, axis, "100px"));
                }
                ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                float viewport = vertical ? (phase >= 3 ? 240.0f : 200.0f)
                    : (phase >= 3 ? 400.0f : 320.0f);
                float basis = phase == 0 ? entry.initial_basis
                    : phase == 1 ? (strcmp(entry.name, "minmax") == 0 ? 140.0f : 80.0f)
                    : phase == 4 ? 100.0f : entry.fallback;
                if (basis < 0.0f) basis = viewport;
                float css_size = basis * 0.5f + (math ? 10.0f : 0.0f);
                if (strncmp(limit, "min-", 4) == 0) css_size = max(css_size, basis * 0.8f);
                else if (*limit) css_size = min(css_size, basis * 0.4f);
                float used = max(css_size + (border_box ? 0.0f : 10.0f), 10.0f);
                EXPECT_FLOAT_EQ(vertical ? result->height : result->width, used);
                EXPECT_FLOAT_EQ(vertical ? result->width : result->height, border_box ? 20 : 30);
            }
            ui.document = nullptr;
            free_document(document);
            document = nullptr;
        }
    }
}

TEST_F(RadiantTextQuery, VerticalRootPublicationPreservesZeroBoxesAndPhysicalDecorations) {
    for (const char* mode : {"vertical-lr", "vertical-rl"})
    for (bool definite : {false, true}) for (bool decorated : {false, true})
    for (const char* content : {"", "<p id=child>A</p>", "\n <p id=child>A</p>\n"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(definite);
        SCOPED_TRACE(decorated);
        SCOPED_TRACE(content);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "html{writing-mode:%s;height:%s;margin:0;padding:%s;%s}"
            "body{font:20px/20px Ahem}p{position:absolute;left:8px;top:40px;"
            "margin:0;writing-mode:horizontal-tb}</style><body id=result>%s</body>",
            mode, definite ? "100%" : "auto", decorated ? "40px 7px 60px 5px" : "40px 0 60px",
            decorated ? "border:2px solid" : "", content);
        DomElement* body = layout_script_source(source);
        ASSERT_NE(body, nullptr);
        DomElement* root = document->root->as_element();
        DomElement* child = dom_find_element_by_id(root, "child");
        for (bool in_flow : {false, true, false}) {
            if (!child && in_flow) break;
            SCOPED_TRACE(in_flow);
            if (child) {
                ASSERT_TRUE(dom_cssom_set_inline_property(child, "position", in_flow ? "static" : "absolute"));
                ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            }
            float left_decoration = decorated ? 7.0f : 0.0f;
            float right_decoration = decorated ? 9.0f : 0.0f;
            float top_decoration = decorated ? 42.0f : 40.0f;
            float inline_decoration = decorated ? 104.0f : 100.0f;
            float body_width = in_flow ? 20.0f : 0.0f;
            float root_height = definite ? 200.0f + inline_decoration : 200.0f;
            float root_width = left_decoration + right_decoration + body_width + (in_flow ? 16.0f : 8.0f);
            float root_x = strcmp(mode, "vertical-rl") == 0 ? 320.0f - root_width : 0.0f;
            float root_actual_x = 0.0f;
            view_get_visual_bounds(root, &root_actual_x, nullptr, nullptr, nullptr);
            EXPECT_FLOAT_EQ(root_actual_x, root_x);
            EXPECT_FLOAT_EQ(root->width, root_width);
            EXPECT_FLOAT_EQ(root->height, root_height);
            float body_x = root_x + left_decoration +
                (strcmp(mode, "vertical-rl") == 0 && !in_flow ? 0.0f : 8.0f);
            expect_box("result", body_x, top_decoration + 8.0f, body_width,
                root_height - inline_decoration - 16.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalAutoInlineConstraintsPrecedeDirectTextWrapping) {
    const struct { const char* css; float minimum, maximum; bool percent; } limits[] = {
        {"", -1, -1, false}, {"min-height:160px", 160, -1, false},
        {"max-height:60px", -1, 60, false}, {"min-height:300px", 300, -1, false},
        {"max-height:50%", -1, 0.5f, true}, {"min-height:80%", 0.8f, -1, true},
        {"min-height:100px;max-height:60px", 100, 60, false}
    };
    const struct { const char* css; float inline_decoration, block_decoration; bool border_box; } boxes[] = {
        {"", 0, 0, false},
        {"padding:4px 5px 6px 3px;border:2px solid", 14, 12, false},
        {"padding:4px 5px 6px 3px;border:2px solid;box-sizing:border-box", 14, 12, true}
    };
    const char* words = "AA AA AA AA AA AA AA";
    for (const char* mode : {"vertical-lr", "vertical-rl"}) for (bool root_flow : {false, true})
    for (const auto& limit : limits) for (const auto& box : boxes) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(root_flow);
        SCOPED_TRACE(limit.css);
        SCOPED_TRACE(box.css);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "html{writing-mode:%s;padding:%s}body{margin:0;font:20px/20px Ahem}"
            "#host{writing-mode:%s;height:100px;padding:10px 0 20px;border:2px solid;box-sizing:border-box}"
            "#result{margin:8px;%s;%s}</style>%s%s%s",
            root_flow ? mode : "horizontal-tb", root_flow ? "40px 0 60px" : "0", mode,
            box.css, limit.css,
            root_flow ? "<body id=result>" : "<div id=host><div id=result>", words,
            root_flow ? "</body>" : "</div></div>");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        ASSERT_NE(result->first_child, nullptr);
        DomText* text = result->first_child->as_text();
        ASSERT_NE(text, nullptr);
        for (int phase = 0; phase < 5; phase++) {
            SCOPED_TRACE(phase);
            if (phase == 1 || phase == 4) {
                ui.viewport_height = ui.window_height = phase == 1 ? 240 : 200;
                layout_html_doc(&ui, document, false);
            } else if (phase == 2 || phase == 3) {
                ASSERT_TRUE(dom_text_set_content(text, phase == 2 ? "" : words));
                ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            }
            float containing_inline = root_flow ? (phase >= 1 && phase < 4 ? 140.0f : 100.0f) : 66.0f;
            float css_size = containing_inline - 16.0f - (box.border_box ? 0.0f : box.inline_decoration);
            if (limit.maximum >= 0.0f) css_size = min(css_size,
                limit.maximum * (limit.percent ? containing_inline : 1.0f));
            if (limit.minimum >= 0.0f) css_size = max(css_size,
                limit.minimum * (limit.percent ? containing_inline : 1.0f));
            float height = max(css_size + (box.border_box ? 0.0f : box.inline_decoration), box.inline_decoration);
            // Ahem words advance 40px, separated by a 20px collapsible space.
            float words_per_column = max(floorf((height - box.inline_decoration + 20.0f) / 60.0f), 1.0f);
            float columns = phase == 2 ? 0.0f : ceilf(7.0f / words_per_column);
            EXPECT_FLOAT_EQ(result->height, height);
            EXPECT_FLOAT_EQ(result->width, columns * 20.0f + box.block_decoration);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, IntrinsicQueriesRestoreCallerSizingContext) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>body{direction:rtl}#result{display:inline-block}"
        "#child{display:inline-block;width:40px;height:30px;direction:ltr}</style>"
        "<div id=result><span id=child></span></div>");
    DomElement* result = layout_script_source(source);
    ASSERT_NE(result, nullptr);
    DomElement* child = dom_find_element_by_id(document->root->as_element(), "child");
    ASSERT_NE(child, nullptr);
    for (const char* height : {"30px", "50px"}) {
        SCOPED_TRACE(height);
        ASSERT_TRUE(dom_cssom_set_inline_property(child, "height", height));
        LayoutContext context = {};
        LayoutPassScope pass(&context, document, &ui);
        context.selected_view_tree = lam::up(document->view_tree.get());
        context.view = lam::up(static_cast<View*>(result));
        context.elmt = lam::up(static_cast<DomNode*>(result));
        context.root_font_size = 16.0f;
        context.block.given_width = context.block.given_height = -1.0f;
        context.block.content_width = 160.0f;
        context.block.content_height = 0.0f;
        context.block.direction = CSS_VALUE_RTL;
        // a scalar intrinsic query may materialize descendant styles, but its
        // first and repeated calls must preserve the caller's active sizing inputs.
        for (bool repeated : {false, true}) {
            SCOPED_TRACE(repeated);
            IntrinsicSizes sizes = measure_element_intrinsic_widths(&context, result);
            EXPECT_GE(calculate_max_content_height(&context, static_cast<DomNode*>(result), 160.0f), 30.0f);
            EXPECT_FLOAT_EQ(sizes.min_content, 40.0f);
            EXPECT_FLOAT_EQ(sizes.max_content, 40.0f);
            EXPECT_FLOAT_EQ(context.block.given_width, -1.0f);
            EXPECT_FLOAT_EQ(context.block.given_height, -1.0f);
            EXPECT_FLOAT_EQ(context.block.content_width, 160.0f);
            EXPECT_FLOAT_EQ(context.block.content_height, 0.0f);
            EXPECT_EQ(context.block.direction, CSS_VALUE_RTL);
            EXPECT_EQ(context.view.get(), static_cast<View*>(result));
            EXPECT_EQ(context.elmt.get(), static_cast<DomNode*>(result));
        }
    }
}

TEST_F(RadiantTextQuery, QuirksPercentageHeightsSkipAutomaticBlockWrappers) {
    for (unsigned depth : {0u, 1u, 2u}) {
        SCOPED_TRACE(depth);
        StrBuf* source = strbuf_new();
        strbuf_append_str(source,
            "<style>body{margin:0}#result{float:left;height:100px}canvas{display:block;height:100%}</style>"
            "<div id=result>");
        for (unsigned i = 0; i < depth; i++) strbuf_append_str(source, "<div>");
        strbuf_append_str(source, "<canvas id=canvas width=10 height=10></canvas>");
        for (unsigned i = 0; i < depth; i++) strbuf_append_str(source, "</div>");
        strbuf_append_str(source, "</div>");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        for (const char* height : {"100px", "160px", "100px"}) {
            SCOPED_TRACE(height);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "height", height));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float size = strcmp(height, "160px") == 0 ? 160.0f : 100.0f;
            EXPECT_FLOAT_EQ(result->width, size);
            EXPECT_FLOAT_EQ(result->height, size);
            expect_box("canvas", 0, 0, size, size);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalColumnSpannersUseFullInlineSpaceBeforeWrapping) {
    for (const char* mode : {"vertical-lr", "vertical-rl"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{columns:2;column-gap:14px;writing-mode:%s;inline-size:200px;"
            "block-size:100px;padding:10px;font:20px/20px Ahem}#span{column-span:all}</style>"
            "<div id=result><div style='block-size:20px'></div>"
            "<div id=span>AA AA AA</div><div style='block-size:70px;break-inside:avoid'></div></div>", mode);
        ASSERT_NE(layout_script_source(source), nullptr);
        DomElement* span = dom_find_element_by_id(document->root->as_element(), "span");
        ASSERT_NE(span, nullptr);
        ASSERT_NE(span->first_child, nullptr);
        DomText* text = span->first_child->as_text();
        ASSERT_NE(text, nullptr);
        for (bool extended : {false, true, false}) {
            SCOPED_TRACE(extended);
            ASSERT_TRUE(dom_text_set_content(text, extended ? "AA AA AA AA AA" : "AA AA AA"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FLOAT_EQ(span->height, 200.0f);
            EXPECT_FLOAT_EQ(span->width, extended ? 40.0f : 20.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, EmptyVerticalColumnsRetainTheirInlineGapStructure) {
    for (const char* mode : {"vertical-lr", "vertical-rl"}) for (unsigned count : {2u, 3u}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(count);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{float:left;writing-mode:%s;"
            "column-count:%u;font:20px/1 serif;padding:3px;border:2px solid}</style><div id=result></div>",
            mode, count);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        for (const char* gap : {"7px", "normal", "0px"}) {
            SCOPED_TRACE(gap);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "column-gap", gap));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float gap_size = strcmp(gap, "normal") == 0 ? 20.0f : strcmp(gap, "7px") == 0 ? 7.0f : 0.0f;
            EXPECT_FLOAT_EQ(result->width, 10.0f);
            EXPECT_FLOAT_EQ(result->height, (count - 1) * gap_size + 10.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalColumnInlineBoundsIncludeAllFragments) {
    for (const char* mode : {"vertical-lr", "vertical-rl"}) for (const char* direction : {"ltr", "rtl"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{position:absolute;columns:3;column-gap:10px;inline-size:290px;"
            "font:20px/1 Ahem;orphans:1;widows:1;writing-mode:%s;direction:%s}</style>"
            "<div id=result><div style='block-size:20px'></div><span id=span>XXXX XXXX XXXX XXXX</span></div>",
            mode, direction);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        EXPECT_FLOAT_EQ(result->width, 40.0f);
        EXPECT_FLOAT_EQ(result->height, 290.0f);
        expect_box("span", 0, strcmp(direction, "rtl") == 0 ? 10 : 0, 40, 280);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, InlineTextAlignmentKeepsItsOwnLeadingAndDominantBaseline) {
    // same embedded Ahem control is independently measured by Chromium; CSS 2.2
    // aligns inline line-height boxes, and Writing Modes 3 maps their line-over edge.
    const struct { const char* align; float horizontal, vertical; } cases[] = {
        {"none", 0, 0}, {"baseline", 84, 75}, {"top", 30, 120}, {"bottom", 120, 30},
        {"text-top", 90, 60}, {"text-bottom", 60, 90}, {"middle", 69, 99},
        {"super", 63, 96}, {"sub", 97, 62}, {"20px", 64, 95}, {"-20px", 104, 55}
    };
    for (const char* mode : {"horizontal-tb", "vertical-rl", "vertical-lr"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:60px/3 Ahem;writing-mode:%s;inline-size:120px;block-size:180px}"
            "#child{font-size:30px}</style><div id=result>A<span id=child>O</span></div>", mode);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* child = dom_find_element_by_id(block, "child");
        ASSERT_NE(child, nullptr);
        DomText* direct = block->first_child->as_text();
        ASSERT_NE(direct, nullptr);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        for (const auto& entry : cases) {
            SCOPED_TRACE(entry.align);
            bool hidden = strcmp(entry.align, "none") == 0;
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "display", hidden ? "none" : "inline"));
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "vertical-align", hidden ? "baseline" : entry.align));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            ASSERT_NE(direct->rect, nullptr);
            EXPECT_FLOAT_EQ(vertical ? direct->rect->x : direct->rect->y, 60.0f);
            EXPECT_FLOAT_EQ(vertical ? direct->rect->y : direct->rect->x, 0.0f);
            if (hidden) continue;
            float cross = vertical ? entry.vertical : entry.horizontal;
            expect_box("child", vertical ? cross : 60.0f, vertical ? 60.0f : cross, 30.0f, 30.0f);
            DomText* text = child->first_child->as_text();
            ASSERT_NE(text, nullptr);
            ASSERT_NE(text->rect, nullptr);
            EXPECT_FLOAT_EQ(vertical ? text->rect->x : text->rect->y, cross);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalLineBoxesKeepInlineContextAcrossForcedBreaks) {
    // independent browser ranges include numeric/fixed/normal leading and unequal columns.
    const struct { const char* leading; const char* late; float width, rl_first, rl_last, lr_first, lr_last; } states[] = {
        {"3", "inherit", 360, 300, 120, 120, 300},
        {"180px", "inherit", 360, 255, 75, 75, 255},
        {"240px", "inherit", 480, 345, 105, 105, 345},
        {"normal", "inherit", 120, 90, 30, 30, 90},
        {"3", "240px", 420, 360, 105, 120, 285},
        {"3", "inherit", 360, 300, 120, 120, 300}
    };
    for (const char* mode : {"vertical-rl", "vertical-lr"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:60px/3 Ahem;writing-mode:%s;inline-size:120px}"
            "#child{font-size:30px;vertical-align:top}</style>"
            "<div id=result>A<span id=child>O<br><span id=late>O</span></span></div>", mode);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* child = dom_find_element_by_id(block, "child");
        DomElement* late = dom_find_element_by_id(block, "late");
        ASSERT_NE(child, nullptr);
        ASSERT_NE(late, nullptr);
        DomText* first = child->first_child->as_text();
        DomText* last = late->first_child->as_text();
        ASSERT_NE(first, nullptr);
        ASSERT_NE(last, nullptr);
        for (const auto& state : states) {
            SCOPED_TRACE(state.leading);
            SCOPED_TRACE(state.late);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "line-height", state.leading));
            ASSERT_TRUE(dom_cssom_set_inline_property(late, "line-height", state.late));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FLOAT_EQ(block->width, state.width);
            EXPECT_FLOAT_EQ(block->height, 120.0f);
            bool rtl_blocks = strcmp(mode, "vertical-rl") == 0;
            float first_x = rtl_blocks ? state.rl_first : state.lr_first;
            float last_x = rtl_blocks ? state.rl_last : state.lr_last;
            expect_box("child", min(first_x, last_x), 0.0f, fabsf(first_x - last_x) + 30.0f, 90.0f);
            expect_box("late", last_x, 0.0f, 30.0f, 30.0f);
            ASSERT_NE(first->rect, nullptr);
            ASSERT_NE(last->rect, nullptr);
            EXPECT_FLOAT_EQ(first->rect->x, first_x);
            EXPECT_FLOAT_EQ(first->rect->y, 60.0f);
            EXPECT_FLOAT_EQ(last->rect->x, last_x);
            EXPECT_FLOAT_EQ(last->rect->y, 0.0f);
            EXPECT_EQ(last->rect->line_number, first->rect->line_number + 1);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, EmptyVerticalAtomsExportTheirMarginBoxCentralBaseline) {
    // nonsquare atoms distinguish the inline measure from the baseline's transverse extent.
    for (const char* mode : {"vertical-rl", "vertical-lr"}) for (bool margins : {false, true})
        for (const char* tag : {"span", "canvas"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(margins);
        SCOPED_TRACE(tag);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:20px/0 Ahem;writing-mode:%s;inline-size:100px}"
            "#atom{display:inline-block;block-size:80px;inline-size:20px;margin-left:%gpx;margin-right:%gpx}"
            "</style><div id=result><%s id=atom></%s></div>", mode, margins ? 6.0f : 0.0f, margins ? 14.0f : 0.0f, tag, tag);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        for (const char* leading : {"0", "160px", "0px"}) {
            SCOPED_TRACE(leading);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "line-height", leading));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float margin_extent = margins ? 100.0f : 80.0f;
            float line_extent = strcmp(leading, "160px") == 0 ? 160.0f : margin_extent;
            EXPECT_FLOAT_EQ(block->width, line_extent);
            EXPECT_FLOAT_EQ(block->height, 100.0f);
            expect_box("atom", (line_extent - margin_extent) / 2.0f + (margins ? 6.0f : 0.0f),
                0.0f, 80.0f, 20.0f);
            ASSERT_NE(block->blk, nullptr);
            EXPECT_FLOAT_EQ(block->block()->first_line_baseline, line_extent / 2.0f);
            EXPECT_FLOAT_EQ(block->block()->last_line_baseline, line_extent / 2.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalSourceBaselineOriginsIncludePaddingOnce) {
    const char* blocks = "<div id=source><div id=first class=cell><i></i></div>"
        "<div id=last class=cell><i></i></div></div>";
    const char* rows = "<table id=source><tr id=first class=cell><td><i></i></td></tr>"
        "<tr id=last class=cell><td><i></i></td></tr></table>";
    const char* fragments = "<table id=source class=fragment><caption></caption>"
        "<tr><td class=small><i></i><br><i></i></td><td><i id=first></i><br><i></i></td></tr>"
        "<tr><td><i id=last></i><br><i></i></td><td class=small><i></i><br><i></i></td></tr>"
        "<caption style='caption-side:bottom'></caption></table>";
    const char* fieldset = "<fieldset id=source><i id=first></i><br><i id=last></i>"
        "<legend><i></i></legend></fieldset>";
    const struct { const char* display; const char* markup; } owners[] = {
        {"inline-flex", blocks}, {"inline-grid", blocks}, {"inline-table", rows},
        {"inline-table", fragments}, {"inline-block", fieldset}, {"inline-flex", fieldset}
    };
    for (const char* mode : {"vertical-rl", "vertical-lr"}) for (const auto& owner : owners)
        for (bool margins : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(owner.display);
        SCOPED_TRACE(owner.markup);
        SCOPED_TRACE(margins);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:20px/0 Ahem;writing-mode:%s;inline-size:200px}"
            "#probe,i{display:inline-block;inline-size:20px;block-size:20px}"
            "#source{display:%s;flex-direction:column;font-size:30px;inline-size:40px;border:5px solid;padding:10px;%s}"
            ".cell{block-size:30px}i{inline-size:30px;block-size:30px}"
            "table{border-spacing:0}td{padding:0}tr{vertical-align:baseline}"
            ".fragment i{inline-size:1em;block-size:1em}.small{font-size:10px}"
            "caption{margin:10px;block-size:20px}</style>"
            "<div id=result><span id=probe></span>%s</div>", mode, owner.display,
            margins ? "margin-left:6px;margin-right:14px" : "", owner.markup);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* source_box = dom_find_element_by_id(block, "source");
        DomElement* probe = dom_find_element_by_id(block, "probe");
        ASSERT_NE(source_box, nullptr);
        ASSERT_NE(probe, nullptr);
        for (const char* selected : {"first", "last", "first"}) {
            SCOPED_TRACE(selected);
            ASSERT_TRUE(dom_cssom_set_inline_property(source_box, "baseline-source", selected));
            ASSERT_TRUE(dom_cssom_set_inline_property(source_box, "padding",
                strcmp(selected, "last") == 0 ? "20px" : "10px"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            if (owner.markup == fragments) {
                // four 30px glyph columns, two 20px captions with 10px margins, and the authored decoration.
                float padding = strcmp(selected, "last") == 0 ? 20.0f : 10.0f;
                EXPECT_FLOAT_EQ(source_box->width, 4.0f * 30.0f + 2.0f * 40.0f + 2.0f * (5.0f + padding));
            }
            DomElement* item = dom_find_element_by_id(block, selected);
            ASSERT_NE(item, nullptr);
            float probe_x = 0.0f, item_x = 0.0f;
            view_get_visual_bounds(probe, &probe_x, nullptr, nullptr, nullptr);
            view_get_visual_bounds(item, &item_x, nullptr, nullptr, nullptr);
            EXPECT_FLOAT_EQ(probe_x + probe->width / 2.0f, item_x + item->width / 2.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalContentBaselineExtentsUseTheTransverseAxis) {
    for (const char* mode : {"vertical-rl", "vertical-lr"}) for (bool control : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(control);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:20px/20px Ahem;writing-mode:%s;inline-size:300px}"
            "#source{display:inline-block;font:20px/20px Ahem;border:3px solid;inline-size:120px}"
            "input#source{width:40px;padding:2px;margin:0}</style>"
            "<div id=result><span>X</span>%s</div>", mode,
            control ? "<input id=source type=text value=X>" : "<div id=source>X</div>");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* atom = dom_find_element_by_id(block, "source");
        ASSERT_NE(atom, nullptr);
        for (const char* extent : {"120px", "200px", "120px"}) {
            SCOPED_TRACE(extent);
            ASSERT_TRUE(dom_cssom_set_inline_property(atom, "inline-size", extent));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // the internal line fits across the box; changing its inline measure cannot widen the parent line.
            EXPECT_FLOAT_EQ(block->width, atom->width);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, QuirksInlineLinesExcludeTheBlockRootStrut) {
    for (bool standards : {false, true}) {
        SCOPED_TRACE(standards);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "%s<style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:100px/1 Ahem}span{font-size:40px}</style>"
            "<div id=result><span id=text>X</span></div>",
            standards ? "<!doctype html>" : "");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* text = dom_find_element_by_id(block, "text");
        ASSERT_NE(text, nullptr);
        for (const char* size : {"40px", "60px", "40px"}) {
            SCOPED_TRACE(size);
            ASSERT_TRUE(dom_cssom_set_inline_property(text, "font-size", size));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // the child supplies the entire line when quirks mode suppresses the root strut.
            EXPECT_FLOAT_EQ(block->height, standards ? 100.0f : text->height);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalAutoInlineExtentIncludesTextBeforeAnOrthogonalAtom) {
    for (const char* mode : {"vertical-lr", "sideways-rl"}) for (const char* direction : {"ltr", "rtl"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:40px/40px Ahem;writing-mode:%s;direction:%s;dominant-baseline:text-top}"
            "i{display:inline-block;writing-mode:horizontal-tb;width:40px;height:20px}</style>"
            "<div id=result>X<i id=atom></i></div>", mode, direction);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* atom = dom_find_element_by_id(block, "atom");
        ASSERT_NE(atom, nullptr);
        for (const char* height : {"20px", "30px", "20px"}) {
            SCOPED_TRACE(height);
            ASSERT_TRUE(dom_cssom_set_inline_property(atom, "height", height));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FLOAT_EQ(block->height, 40.0f + atom->height);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, OrthogonalIntrinsicBaselinesIncludeFractionalFontLeading) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
        "body{margin:0}#result{display:inline-block;font:30px/1 Ahem;dominant-baseline:text-top}"
        "#side{writing-mode:sideways-rl;font-size:17.25px}"
        "i{display:inline-block;writing-mode:horizontal-tb;width:20px;height:25px}</style>"
        "<div id=result>X<span id=side>X<i id=atom></i></span></div>");
    DomElement* block = layout_script_source(source);
    ASSERT_NE(block, nullptr);
    DomElement* side = dom_find_element_by_id(block, "side");
    ASSERT_NE(side, nullptr);
    DomElement* atom = dom_find_element_by_id(block, "atom");
    ASSERT_NE(atom, nullptr);
    for (const char* baseline : {"text-top", "alphabetic", "text-top"}) {
        SCOPED_TRACE(baseline);
        ASSERT_TRUE(dom_cssom_set_inline_property(block, "dominant-baseline", baseline));
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        LayoutContext context = {};
        LayoutPassScope pass(&context, document, &ui);
        context.selected_view_tree = lam::up(document->view_tree.get());
        context.root_font_size = 16.0f;
        context.block.flow_box = lam::up(lam::view_as_block(block));
        // shrink-to-fit measurement must predict the used baseline union, including fractional leading.
        IntrinsicSizes sizes = measure_element_intrinsic_widths(&context, side);
        EXPECT_NEAR(sizes.max_content, side->width, 1.0f / 64.0f);
        EXPECT_EQ(context.block.flow_box.get(), lam::view_as_block(block));
        if (strcmp(baseline, "text-top") == 0) {
            EXPECT_FLOAT_EQ(block->height, max(30.0f, side->height));
        }
    }
}

TEST_F(RadiantTextQuery, PositionedVerticalLinesUseTheirOwnBaselinePolicy) {
    for (const char* mode : {"vertical-rl", "vertical-lr"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#host{position:relative;width:200px;height:100px}"
            "#result{position:absolute;writing-mode:%s;width:50%%}canvas{width:100%%}</style>"
            "<div id=host><div id=result><canvas id=canvas width=1 height=1></canvas></div></div>", mode);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* host = dom_find_element_by_id(document->root->as_element(), "host");
        ASSERT_NE(host, nullptr);
        for (const char* width : {"200px", "300px", "200px"}) {
            SCOPED_TRACE(width);
            ASSERT_TRUE(dom_cssom_set_inline_property(host, "width", width));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float extent = host->width / 2.0f;
            EXPECT_FLOAT_EQ(block->width, extent);
            EXPECT_FLOAT_EQ(block->height, extent);
            expect_box("canvas", 0.0f, 0.0f, extent, extent);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, WrappedVerticalAtomsRetainTheirSourceLineGeometry) {
    for (const char* mode : {"vertical-rl", "vertical-lr"})
        for (bool definite : {false, true}) for (float cross : {20.0f, 60.0f}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(definite);
        SCOPED_TRACE(cross);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#host{font:20px/20px Ahem;width:300px;height:80px;overflow:hidden}"
            "#result{writing-mode:%s;%s}i{display:inline-block;width:20px;height:20px}"
            "#large{width:%gpx}</style><div id=host><div id=result>XXX "
            "<i id=large></i><i id=small></i></div></div>", mode,
            definite ? "inline-size:80px" : "", cross);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* host = dom_find_element_by_id(document->root->as_element(), "host");
        DomElement* large = dom_find_element_by_id(block, "large");
        DomElement* small = dom_find_element_by_id(block, "small");
        ASSERT_NE(host, nullptr);
        ASSERT_NE(large, nullptr);
        ASSERT_NE(small, nullptr);
        for (const char* size : {"80px", "160px", "80px"}) {
            SCOPED_TRACE(size);
            ASSERT_TRUE(dom_cssom_set_inline_property(definite ? block : host,
                definite ? "inline-size" : "height", size));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool wrapped = strcmp(size, "80px") == 0;
            EXPECT_FLOAT_EQ(block->width, cross + (wrapped ? 20.0f : 0.0f));
            EXPECT_FLOAT_EQ(block->height, wrapped ? 80.0f : definite ? 160.0f : 120.0f);
            float root_x = 0.0f, root_y = 0.0f, large_x = 0.0f, large_y = 0.0f;
            float small_x = 0.0f, small_y = 0.0f;
            view_get_visual_bounds(block, &root_x, &root_y, nullptr, nullptr);
            view_get_visual_bounds(large, &large_x, &large_y, nullptr, nullptr);
            view_get_visual_bounds(small, &small_x, &small_y, nullptr, nullptr);
            float line_start = wrapped && strcmp(mode, "vertical-lr") == 0 ? 20.0f : 0.0f;
            EXPECT_FLOAT_EQ(large_x - root_x, line_start);
            EXPECT_FLOAT_EQ(small_x - root_x, line_start + (cross - 20.0f) / 2.0f);
            EXPECT_FLOAT_EQ(large_y - root_y, wrapped ? 0.0f : 80.0f);
            EXPECT_FLOAT_EQ(small_y - root_y, wrapped ? 20.0f : 100.0f);
            EXPECT_EQ(large->inline_line_number, wrapped ? 1 : 0);
            EXPECT_EQ(small->inline_line_number, large->inline_line_number);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, OrthogonalFitContentUsesAtomicInlineContributions) {
    for (const char* mode : {"vertical-rl", "vertical-lr"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#host{font:20px/20px Ahem;width:300px;height:80px;overflow:hidden}"
            "#result{writing-mode:%s}i{display:inline-block;font-style:normal;"
            "padding-inline:2px 4px;border-inline:1px solid;margin-inline:3px 5px}</style>"
            "<div id=host><div id=result>XXX <i id=atom>X X</i></div></div>", mode);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* host = dom_find_element_by_id(document->root->as_element(), "host");
        DomElement* atom = dom_find_element_by_id(block, "atom");
        ASSERT_NE(host, nullptr);
        ASSERT_NE(atom, nullptr);
        for (const char* font_size : {"20px", "30px", "20px"}) {
            SCOPED_TRACE(font_size);
            ASSERT_TRUE(dom_cssom_set_inline_property(atom, "font-size", font_size));
            float maximum = strcmp(font_size, "30px") == 0 ? 186.0f : 156.0f;
            for (const char* available : {"40px", "80px", "200px", "80px"}) {
                SCOPED_TRACE(available);
                ASSERT_TRUE(dom_cssom_set_inline_property(host, "height", available));
                ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                EXPECT_FLOAT_EQ(block->height, min(maximum, max(60.0f, host->height)));
                float atom_x = atom->x, atom_y = atom->y;
                float atom_width = atom->width, atom_height = atom->height;
                LayoutContext context = {};
                LayoutPassScope pass(&context, document, &ui);
                context.selected_view_tree = lam::up(document->view_tree.get());
                context.root_font_size = 16.0f;
                context.block.flow_box = lam::up(lam::view_as_block(block));
                IntrinsicSizes sizes = {};
                ASSERT_TRUE(layout_measure_direct_inline_intrinsics(
                    &context, lam::view_as_block(block), &sizes));
                EXPECT_FLOAT_EQ(sizes.min_content, 60.0f);
                EXPECT_FLOAT_EQ(sizes.max_content, maximum);
                EXPECT_FLOAT_EQ(atom->x, atom_x);
                EXPECT_FLOAT_EQ(atom->y, atom_y);
                EXPECT_FLOAT_EQ(atom->width, atom_width);
                EXPECT_FLOAT_EQ(atom->height, atom_height);
                EXPECT_EQ(context.block.flow_box.get(), lam::view_as_block(block));
                EXPECT_EQ(context.view.get(), nullptr);
                EXPECT_EQ(context.elmt.get(), nullptr);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, AspectRatioDeterminesOrthogonalAutoInlineSize) {
    for (const char* mode : {"vertical-rl", "vertical-lr"})
        for (bool border_box : {false, true}) for (const char* text : {"", "X X X", "XXXXX"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(border_box);
        SCOPED_TRACE(text);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#host{font:20px/20px Ahem;width:300px;height:200px}"
            "#result{writing-mode:%s;width:100px;aspect-ratio:2/1;box-sizing:%s;"
            "padding:10px;border:2px solid}</style><div id=host><div id=result>%s</div></div>",
            mode, border_box ? "border-box" : "content-box", text);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        for (const char* width : {"100px", "160px", "100px"}) {
            SCOPED_TRACE(width);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "width", width));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float css_width = strcmp(width, "160px") == 0 ? 160.0f : 100.0f;
            float decoration = border_box ? 0.0f : 24.0f;
            EXPECT_FLOAT_EQ(block->width, css_width + decoration);
            // the ratio supplies the preferred size; the automatic minimum
            // still retains a 100px unbreakable run plus its box decoration.
            float minimum = strcmp(text, "XXXXX") == 0 ? 124.0f : 0.0f;
            EXPECT_FLOAT_EQ(block->height, max(css_width / 2.0f + decoration, minimum));
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, GridTracksUseContainerAxesForReplacedContributions) {
    struct State { const char* available; const char* natural_width; float inline_size; float width; };
    const State states[] = {{"150px", "200", 150.0f, 200.0f}, {"250px", "200", 250.0f, 200.0f},
        {"250px", "120", 250.0f, 120.0f}, {"150px", "120", 150.0f, 120.0f},
        {"150px", "200", 150.0f, 200.0f}};
    for (const char* mode : {"horizontal-tb", "vertical-rl", "vertical-lr"})
        for (bool orthogonal : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(orthogonal);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        const char* child_mode = orthogonal ? (vertical ? "horizontal-tb" : "vertical-rl") : mode;
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#host{writing-mode:%s;font:20px/20px Ahem;width:300px;height:400px}"
            "#result{display:grid;inline-size:150px;border:1px solid}#cell{border:1px solid}"
            "canvas{writing-mode:%s;vertical-align:top}</style><div id=host><div id=result>"
            "<div id=cell><canvas id=canvas width=200 height=100></canvas></div></div></div>",
            mode, child_mode);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* cell = dom_find_element_by_id(block, "cell");
        DomElement* canvas = dom_find_element_by_id(block, "canvas");
        ASSERT_NE(cell, nullptr);
        ASSERT_NE(canvas, nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.available);
            SCOPED_TRACE(state.natural_width);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "inline-size", state.available));
            ASSERT_TRUE(canvas->set_attribute("width", state.natural_width));
            layout_html_doc(&ui, document, true);
            float natural_inline = vertical ? 100.0f : state.width;
            float natural_block = vertical ? state.width : 100.0f;
            float item_inline = max(state.inline_size, natural_inline + 2.0f);
            EXPECT_FLOAT_EQ(block->width, vertical ? natural_block + 4.0f : state.inline_size + 2.0f);
            EXPECT_FLOAT_EQ(block->height, vertical ? state.inline_size + 2.0f : natural_block + 4.0f);
            EXPECT_FLOAT_EQ(cell->width, vertical ? natural_block + 2.0f : item_inline);
            EXPECT_FLOAT_EQ(cell->height, vertical ? item_inline : natural_block + 2.0f);
            EXPECT_FLOAT_EQ(canvas->width, state.width);
            EXPECT_FLOAT_EQ(canvas->height, 100.0f);
            float root_x = 0.0f, root_y = 0.0f, cell_x = 0.0f, cell_y = 0.0f;
            float canvas_x = 0.0f, canvas_y = 0.0f;
            view_get_visual_bounds(block, &root_x, &root_y, nullptr, nullptr);
            view_get_visual_bounds(cell, &cell_x, &cell_y, nullptr, nullptr);
            view_get_visual_bounds(canvas, &canvas_x, &canvas_y, nullptr, nullptr);
            EXPECT_FLOAT_EQ(cell_x - root_x, 1.0f);
            EXPECT_FLOAT_EQ(cell_y - root_y, 1.0f);
            EXPECT_FLOAT_EQ(canvas_x - cell_x, 1.0f);
            EXPECT_FLOAT_EQ(canvas_y - cell_y, 1.0f);
            if (vertical) {
                LayoutContext context = {};
                LayoutPassScope pass(&context, document, &ui);
                context.selected_view_tree = lam::up(document->view_tree.get());
                context.root_font_size = 16.0f;
                context.block.flow_box = lam::up(lam::view_as_block(block));
                // physical width is the row extent of a vertical grid, not its column extent.
                IntrinsicSizes sizes = measure_element_intrinsic_widths(&context, block);
                EXPECT_FLOAT_EQ(sizes.min_content, state.width + 4.0f);
                EXPECT_FLOAT_EQ(sizes.max_content, state.width + 4.0f);
                EXPECT_FLOAT_EQ(block->width, state.width + 4.0f);
                EXPECT_FLOAT_EQ(cell->width, state.width + 2.0f);
                EXPECT_EQ(context.block.flow_box.get(), lam::view_as_block(block));
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, GridAreasAndSelfAlignmentFollowLogicalAxes) {
    for (const char* mode : {"horizontal-tb", "vertical-rl", "vertical-lr"})
        for (bool rtl : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(rtl);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{writing-mode:%s;direction:%s;position:relative;"
            "display:grid;inline-size:140px;block-size:100px;padding:2px 4px 6px 8px;border:1px solid;"
            "grid-template-columns:40px 60px;grid-template-rows:30px 50px;gap:8px 10px;"
            "justify-content:center;align-content:end}#a,#b{inline-size:20px;block-size:10px;"
            "justify-self:end;align-self:center}#a{grid-area:1/1}#b{grid-area:2/2}"
            "#auto{grid-area:2/1;justify-self:end;align-self:center;margin:5%%;font:10px/10px Ahem}"
            "canvas{vertical-align:top}"
            "#abs{position:absolute;inset:0;grid-column:2/3;grid-row:1/3}</style>"
            "<div id=result><div id=a></div><div id=b></div>"
            "<div id=auto><canvas width=20 height=30></canvas></div><div id=abs></div></div>",
            mode, rtl ? "rtl" : "ltr");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        for (bool distributed : {true, false, true}) {
            SCOPED_TRACE(distributed);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "justify-content", distributed ? "center" : "start"));
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "align-content", distributed ? "end" : "start"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float column_offset = distributed ? 15.0f : 0.0f;
            float row_offset = distributed ? 12.0f : 0.0f;
            struct Area { const char* id; float column; float row; float inline_size; float block_size; };
            const Area areas[] = {{"a", column_offset + 20.0f, row_offset + 10.0f, 20.0f, 10.0f},
                {"b", column_offset + 90.0f, row_offset + 58.0f, 20.0f, 10.0f},
                {"auto", column_offset + 38.0f - (vertical ? 30.0f : 20.0f),
                    row_offset + 38.0f + (50.0f - (vertical ? 20.0f : 30.0f)) / 2.0f,
                    vertical ? 30.0f : 20.0f, vertical ? 20.0f : 30.0f},
                {"abs", column_offset + 50.0f, row_offset, 60.0f, 88.0f}};
            float root_x = 0.0f, root_y = 0.0f;
            view_get_visual_bounds(block, &root_x, &root_y, nullptr, nullptr);
            for (const Area& area : areas) {
                SCOPED_TRACE(area.id);
                DomElement* child = dom_find_element_by_id(block, area.id);
                ASSERT_NE(child, nullptr);
                float inline_start = rtl ? 140.0f - area.column - area.inline_size : area.column;
                float block_start = strcmp(mode, "vertical-rl") == 0
                    ? 100.0f - area.row - area.block_size : area.row;
                float x = 0.0f, y = 0.0f;
                view_get_visual_bounds(child, &x, &y, nullptr, nullptr);
                EXPECT_FLOAT_EQ(x - root_x, 9.0f + (vertical ? block_start : inline_start));
                EXPECT_FLOAT_EQ(y - root_y, 3.0f + (vertical ? inline_start : block_start));
                EXPECT_FLOAT_EQ(child->width, vertical ? area.block_size : area.inline_size);
                EXPECT_FLOAT_EQ(child->height, vertical ? area.inline_size : area.block_size);
                if (strcmp(area.id, "auto") == 0) {
                    // all sides use the 40px column area, independent of physical orientation.
                    EXPECT_FLOAT_EQ(child->boundary()->margin.left, 2.0f);
                    EXPECT_FLOAT_EQ(child->boundary()->margin.right, 2.0f);
                    EXPECT_FLOAT_EQ(child->boundary()->margin.top, 2.0f);
                    EXPECT_FLOAT_EQ(child->boundary()->margin.bottom, 2.0f);
                }
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, GridAutomaticRatioBlockSizeRetainsItsContentMinimum) {
    for (bool capped : {false, true}) for (bool auto_minimum : {true, false})
        for (bool scrolls : {false, true}) {
        SCOPED_TRACE(capped);
        SCOPED_TRACE(auto_minimum);
        SCOPED_TRACE(scrolls);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:grid;width:100px;"
            "aspect-ratio:2/1;align-items:center;max-height:%s;min-height:%s;overflow:%s}"
            "#child{width:100%%;height:20px}</style><div id=result><div id=child></div></div>",
            capped ? "100px" : "none", auto_minimum ? "auto" : "0px", scrolls ? "hidden" : "visible");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* child = dom_find_element_by_id(block, "child");
        ASSERT_NE(child, nullptr);
        for (float height : {20.0f, 200.0f, 20.0f}) {
            SCOPED_TRACE(height);
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "height", height == 20.0f ? "20px" : "200px"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // the preferred ratio is a floor, and the automatic minimum is capped by max-height.
            float expected = auto_minimum && !scrolls ? max(50.0f, height) : 50.0f;
            if (capped) expected = min(expected, 100.0f);
            EXPECT_FLOAT_EQ(block->width, 100.0f);
            EXPECT_FLOAT_EQ(block->height, expected);
            EXPECT_FLOAT_EQ(child->height, height);
            EXPECT_FLOAT_EQ(child->y, (max(50.0f, height) - height) / 2.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, GridReplacedBlockContributionsApplyIntrinsicMinMax) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>body{margin:0}#result{display:grid;width:max-content}"
        "canvas{display:block;width:max-content;height:0;min-height:max-content}</style>"
        "<div id=result><canvas id=canvas width=50 height=50></canvas></div>");
    DomElement* block = layout_script_source(source);
    ASSERT_NE(block, nullptr);
    DomElement* canvas = dom_find_element_by_id(block, "canvas");
    ASSERT_NE(canvas, nullptr);
    for (bool minimum : {true, false, true}) {
        SCOPED_TRACE(minimum);
        ASSERT_TRUE(dom_cssom_set_inline_property(canvas, "height", minimum ? "0px" : "100px"));
        ASSERT_TRUE(dom_cssom_set_inline_property(canvas, "min-height", minimum ? "max-content" : "0px"));
        ASSERT_TRUE(dom_cssom_set_inline_property(canvas, "max-height", minimum ? "none" : "max-content"));
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        EXPECT_FLOAT_EQ(block->width, 50.0f);
        EXPECT_FLOAT_EQ(block->height, 50.0f);
        EXPECT_FLOAT_EQ(canvas->width, 50.0f);
        EXPECT_FLOAT_EQ(canvas->height, 50.0f);
    }
}

TEST_F(RadiantTextQuery, VerticalFlexAutomaticBlockExtentRetainsConstraintsAndRestoration) {
    const struct State {
        const char* inline_size; const char* block_size; const char* minimum; const char* maximum;
        float width; float height; bool empty = false; bool switch_mode = false;
    } states[] = {
        {"auto","auto","0px","none",20,30},
        {"120px","auto","0px","none",20,30},
        {"120px","80px","0px","none",20,30},
        {"auto","auto","50px","none",20,30},
        {"auto","auto","0px","12px",20,30},
        {"120px","0px","0px","none",20,30},
        {"auto","auto","0px","none",40,25},
        {"auto","auto","0px","none",40,25,false,true},
        {"auto","auto","0px","none",20,30,true},
        {"auto","auto","0px","none",20,30}};
    for (const char* initial_mode : {"horizontal-tb","vertical-lr","vertical-rl","sideways-lr","sideways-rl"})
    for (const char* direction : {"row","column","row-reverse","column-reverse"})
    for (bool decorated : {false,true}) {
        SCOPED_TRACE(initial_mode);
        SCOPED_TRACE(direction);
        SCOPED_TRACE(decorated);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flow-root}"
            "#child{display:flex;writing-mode:%s;flex-direction:%s;%s}.item{width:20px;height:30px}"
            "</style><div id=result><div id=child><div class=item></div><div class=item></div>"
            "<div class=item></div></div></div>",initial_mode,direction,
            decorated ? "padding:3px 7px 5px 11px;border:2px solid" : "");
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.inline_size);
            SCOPED_TRACE(state.block_size);
            SCOPED_TRACE(state.minimum);
            SCOPED_TRACE(state.maximum);
            SCOPED_TRACE(state.empty);
            SCOPED_TRACE(state.switch_mode);
            const char* mode = state.switch_mode
                ? (strcmp(initial_mode,"horizontal-tb") == 0 ? "vertical-lr" : "horizontal-tb") : initial_mode;
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            bool column = strncmp(direction,"column",6) == 0;
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"writing-mode",mode));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"inline-size",state.inline_size));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"block-size",state.block_size));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"min-block-size",state.minimum));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"max-block-size",state.maximum));
            char width[32],height[32];
            str_fmt(width,sizeof(width),"%gpx",state.width);
            str_fmt(height,sizeof(height),"%gpx",state.height);
            for (DomNode* node = child->first_child; node; node = node->next_sibling) {
                ASSERT_TRUE(dom_cssom_set_inline_property(node->as_element(),"width",width));
                ASSERT_TRUE(dom_cssom_set_inline_property(node->as_element(),"height",height));
                ASSERT_TRUE(dom_cssom_set_inline_property(node->as_element(),"display",state.empty ? "none" : "block"));
            }
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // nowrap flex block extent follows its lines; automatic inline extent follows the containing flow.
            float block_extent = state.empty ? 0.0f : vertical
                ? state.width*(column ? 3.0f : 1.0f) : state.height*(column ? 3.0f : 1.0f);
            if (strcmp(state.block_size,"80px") == 0) block_extent = 80.0f;
            else if (strcmp(state.block_size,"0px") == 0) block_extent = 0.0f;
            if (strcmp(state.minimum,"50px") == 0) block_extent = max(block_extent,50.0f);
            if (strcmp(state.maximum,"12px") == 0) block_extent = min(block_extent,12.0f);
            float inline_decoration = decorated ? (vertical ? 12.0f : 22.0f) : 0.0f;
            float block_decoration = decorated ? (vertical ? 22.0f : 12.0f) : 0.0f;
            float inline_extent = vertical ? (state.empty ? 0.0f : state.height*(column ? 1.0f : 3.0f))
                : 1200.0f-inline_decoration;
            if (strcmp(state.inline_size,"120px") == 0) inline_extent = 120.0f;
            EXPECT_FLOAT_EQ(child->width,(vertical ? block_extent+block_decoration : inline_extent+inline_decoration));
            EXPECT_FLOAT_EQ(child->height,(vertical ? inline_extent+inline_decoration : block_extent+block_decoration));
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantTextQuery, NestedVerticalFlexKeepsTheParentsAssignedPhysicalWidth) {
    for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl","sideways-lr","sideways-rl"})
    for (const char* direction : {"row","column","row-reverse","column-reverse"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flex;width:200px;height:120px}"
            "#child{display:flex;flex:1;writing-mode:%s;flex-direction:%s}"
            ".item{width:20px;height:30px}</style><div id=result><div id=child>"
            "<div class=item></div><div class=item></div><div class=item></div></div></div>",mode,direction);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        for (const char* width : {"200px","260px","200px"}) {
            ASSERT_TRUE(dom_cssom_set_inline_property(result,"width",width));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // the outer flex algorithm owns this border box; inner flow only measures its contents.
            EXPECT_FLOAT_EQ(child->width,strcmp(width,"260px") == 0 ? 260.0f : 200.0f);
            EXPECT_FLOAT_EQ(child->height,120.0f);
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantTextQuery, OrthogonalFlexBlockContentsRetainPhysicalContributions) {
    struct State { const char* available; const char* width; const char* height; float child_width; float child_height; };
    const State states[] = {{"280px", "40px", "30px", 40.0f, 30.0f},
        {"360px", "40px", "30px", 40.0f, 30.0f}, {"360px", "25px", "45px", 25.0f, 45.0f},
        {"280px", "25px", "45px", 25.0f, 45.0f}, {"280px", "40px", "30px", 40.0f, 30.0f}};
    for (bool decorated : {false, true}) for (bool parallel_control : {true, false})
        for (bool spaced : {false, true}) {
        SCOPED_TRACE(decorated);
        SCOPED_TRACE(parallel_control);
        SCOPED_TRACE(spaced);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flex;width:280px;font:10px/10px Ahem}"
            ".item{padding:%s;border:%s}#htb{display:%s}#vrl{writing-mode:vertical-rl}#vlr{writing-mode:vertical-lr}"
            ".child{width:40px;height:30px}#vrl .child,#vlr .child{margin-block-start:%s;margin-block-end:%s}"
            "</style><div id=result>"
            "<div class=item id=htb><div class=child></div><div class=child></div></div>"
            "<div class=item id=vrl><div class=child></div><div class=child></div></div>"
            "<div class=item id=vlr><div class=child></div><div class=child></div></div></div>",
            decorated ? "2px 3px 4px 5px" : "0", decorated ? "1px solid" : "0",
            parallel_control ? "block" : "none", spaced ? "6px" : "0", spaced ? "4px" : "0");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.available);
            SCOPED_TRACE(state.width);
            SCOPED_TRACE(state.height);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "width", state.available));
            for (const char* id : {"htb", "vrl", "vlr"}) {
                DomElement* item = dom_find_element_by_id(block, id);
                ASSERT_NE(item, nullptr);
                for (DomNode* child = item->first_child; child; child = child->next_sibling) {
                    ASSERT_TRUE(child->is_element());
                    ASSERT_TRUE(dom_cssom_set_inline_property(child->as_element(), "width", state.width));
                    ASSERT_TRUE(dom_cssom_set_inline_property(child->as_element(), "height", state.height));
                }
            }
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float decoration_x = decorated ? 10.0f : 0.0f;
            float decoration_y = decorated ? 8.0f : 0.0f;
            float item_x = 0.0f;
            EXPECT_FLOAT_EQ(block->height, (parallel_control ? 2.0f : 1.0f) * state.child_height + decoration_y);
            for (const char* id : {"htb", "vrl", "vlr"}) {
                SCOPED_TRACE(id);
                DomElement* item = dom_find_element_by_id(block, id);
                bool vertical = strcmp(id, "htb") != 0;
                if (!vertical && !parallel_control) continue;
                float start_margin = spaced ? 6.0f : 0.0f;
                float end_margin = spaced ? 4.0f : 0.0f;
                float collapsed_gap = max(start_margin, end_margin);
                float flow_width = 2.0f * state.child_width + start_margin + collapsed_gap + end_margin;
                float expected_width = (vertical ? flow_width : state.child_width) + decoration_x;
                EXPECT_FLOAT_EQ(item->x, item_x);
                EXPECT_FLOAT_EQ(item->y, 0.0f);
                EXPECT_FLOAT_EQ(item->width, expected_width);
                EXPECT_FLOAT_EQ(item->height, block->height);
                float child_index = 0.0f;
                for (DomNode* child = item->first_child; child; child = child->next_sibling) {
                    DomElement* element = child->as_element();
                    float x = decorated ? 6.0f : 0.0f;
                    float y = decorated ? 3.0f : 0.0f;
                    if (vertical) {
                        float offset = start_margin + child_index * (state.child_width + collapsed_gap);
                        x += strcmp(id, "vrl") == 0 ? flow_width - offset - state.child_width : offset;
                    }
                    else y += child_index * state.child_height;
                    EXPECT_FLOAT_EQ(element->x, x);
                    EXPECT_FLOAT_EQ(element->y, y);
                    EXPECT_FLOAT_EQ(element->width, state.child_width);
                    EXPECT_FLOAT_EQ(element->height, state.child_height);
                    child_index += 1.0f;
                }
                // flex bases consume physical width, including the item's block progression.
                IntrinsicSizes sizes = measure_physical_widths(item, block);
                EXPECT_FLOAT_EQ(sizes.min_content, expected_width);
                EXPECT_FLOAT_EQ(sizes.max_content, expected_width);
                EXPECT_FLOAT_EQ(item->width, expected_width);
                item_x += expected_width;
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, OrthogonalFlowPreservesFlexAndGridAssignedWidths) {
    for (bool grid : {false, true}) {
        SCOPED_TRACE(grid);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{%s;width:450px;font:10px/10px Ahem}"
            ".item{%s;min-width:0}#vrl{writing-mode:vertical-rl}#vlr{writing-mode:vertical-lr}"
            ".child{width:40px;height:30px}</style><div id=result>"
            "<div class=item id=htb><div class=child></div><div class=child></div></div>"
            "<div class=item id=vrl><div class=child></div><div class=child></div></div>"
            "<div class=item id=vlr><div class=child></div><div class=child></div></div></div>",
            grid ? "display:grid;grid-template-columns:repeat(3,minmax(0,1fr))" : "display:flex",
            grid ? "" : "flex:1 1 0");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        for (bool narrow : {false, true, false}) {
            SCOPED_TRACE(narrow);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "width", narrow ? "90px" : "450px"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FLOAT_EQ(block->height, 60.0f);
            float used_width = block->width / 3.0f;
            float item_index = 0.0f;
            for (const char* id : {"htb", "vrl", "vlr"}) {
                SCOPED_TRACE(id);
                DomElement* item = dom_find_element_by_id(block, id);
                ASSERT_NE(item, nullptr);
                EXPECT_FLOAT_EQ(item->width, used_width);
                EXPECT_FLOAT_EQ(item->height, 60.0f);
                EXPECT_FLOAT_EQ(item->x, item_index * used_width);
                float child_index = 0.0f;
                for (DomNode* child = item->first_child; child; child = child->next_sibling) {
                    DomElement* element = child->as_element();
                    ASSERT_NE(element, nullptr);
                    float x = strcmp(id, "vrl") == 0 ? used_width - (child_index + 1.0f) * 40.0f
                        : strcmp(id, "vlr") == 0 ? child_index * 40.0f : 0.0f;
                    EXPECT_FLOAT_EQ(element->x, x);
                    EXPECT_FLOAT_EQ(element->y, strcmp(id, "htb") == 0 ? child_index * 30.0f : 0.0f);
                    EXPECT_FLOAT_EQ(element->width, 40.0f);
                    EXPECT_FLOAT_EQ(element->height, 30.0f);
                    child_index += 1.0f;
                }
                item_index += 1.0f;
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalFlexButtonLabelsUseCanonicalCentering) {
    for (const char* mode : {"vertical-rl", "vertical-lr"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flex;width:300px;font:20px/20px Ahem}"
            "button{font:inherit;line-height:inherit;text-align:center;margin:0;padding:0;border:0;"
            "width:200px;height:100px;writing-mode:%s}</style>"
            "<div id=result><button id=label>XXX</button></div>", mode);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* button = dom_find_element_by_id(block, "label");
        ASSERT_NE(button, nullptr);
        for (bool small : {false, true, false}) {
            SCOPED_TRACE(small);
            ASSERT_TRUE(dom_cssom_set_inline_property(button, "width", small ? "100px" : "200px"));
            ASSERT_TRUE(dom_cssom_set_inline_property(button, "height", small ? "120px" : "100px"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            ASSERT_NE(button->first_child, nullptr);
            ASSERT_TRUE(button->first_child->is_text());
            float button_x = 0.0f, button_y = 0.0f, text_x = 0.0f, text_y = 0.0f;
            float text_width = 0.0f, text_height = 0.0f;
            view_get_visual_bounds(button, &button_x, &button_y, nullptr, nullptr);
            view_get_visual_bounds(static_cast<View*>(button->first_child), &text_x, &text_y,
                &text_width, &text_height);
            EXPECT_FLOAT_EQ(text_x - button_x + text_width / 2.0f, button->width / 2.0f);
            EXPECT_FLOAT_EQ(text_y - button_y + text_height / 2.0f, button->height / 2.0f);
            LayoutTextRectBounds fragments = layout_text_rect_bounds(button->first_child->as_text()->rect);
            ASSERT_TRUE(fragments.valid);
            EXPECT_FLOAT_EQ((fragments.min_x + fragments.max_x) / 2.0f, button->width / 2.0f);
            EXPECT_FLOAT_EQ((fragments.min_y + fragments.max_y) / 2.0f, button->height / 2.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, DominantTextEdgesAlignWithOrthogonalAtomicBoxes) {
    for (const char* mode : {"horizontal-tb", "vertical-rl", "vertical-lr"}) {
        SCOPED_TRACE(mode);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:40px/40px Ahem;writing-mode:%s;inline-size:200px}"
            "i{display:inline-block;width:80px;height:80px;writing-mode:%s}</style>"
            "<div id=result><span id=text>X</span><i id=atom></i></div>", mode,
            vertical ? "horizontal-tb" : "vertical-lr");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* text = dom_find_element_by_id(block, "text");
        DomElement* atom = dom_find_element_by_id(block, "atom");
        ASSERT_NE(text, nullptr);
        ASSERT_NE(atom, nullptr);
        for (const char* leading : {"40px", "80px"}) for (const char* baseline : {"text-top", "text-bottom", "text-top"}) {
            SCOPED_TRACE(leading);
            SCOPED_TRACE(baseline);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "line-height", leading));
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "dominant-baseline", baseline));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float text_x = 0.0f, text_y = 0.0f, atom_x = 0.0f, atom_y = 0.0f;
            view_get_visual_bounds(text, &text_x, &text_y, nullptr, nullptr);
            view_get_visual_bounds(atom, &atom_x, &atom_y, nullptr, nullptr);
            bool over = strcmp(baseline, "text-top") == 0;
            if (vertical) EXPECT_FLOAT_EQ(text_x + (over ? text->width : 0.0f),
                atom_x + (over ? atom->width : 0.0f));
            else EXPECT_FLOAT_EQ(text_y + (over ? 0.0f : text->height),
                atom_y + (over ? 0.0f : atom->height));
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, CheckableControlsPreserveTheirBaselineType) {
    for (const char* mode : {"horizontal-tb", "vertical-rl", "vertical-lr"})
        for (const char* type : {"checkbox", "radio"}) for (bool margins : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(type);
        SCOPED_TRACE(margins);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:20px/20px Ahem;writing-mode:%s;inline-size:200px}"
            "input{width:40px;height:12px;margin:0;margin-%s:%gpx;margin-%s:%gpx}"
            "</style><div id=result><input id=control type=%s><span id=label>A</span></div>",
            mode, vertical ? "left" : "top", margins ? 6.0f : 0.0f,
            vertical ? "right" : "bottom", margins ? 14.0f : 0.0f, type);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* control = dom_find_element_by_id(block, "control");
        DomElement* label = dom_find_element_by_id(block, "label");
        ASSERT_NE(control, nullptr);
        ASSERT_NE(label, nullptr);
        for (const char* orientation : {"mixed", "upright", "mixed"}) {
            SCOPED_TRACE(orientation);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "text-orientation", orientation));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float control_x = 0.0f, control_y = 0.0f, label_x = 0.0f, label_y = 0.0f;
            view_get_visual_bounds(control, &control_x, &control_y, nullptr, nullptr);
            view_get_visual_bounds(label, &label_x, &label_y, nullptr, nullptr);
            if (vertical) {
                float margin_center = control_x - (margins ? 6.0f : 0.0f) +
                    (control->width + (margins ? 20.0f : 0.0f)) / 2.0f;
                EXPECT_FLOAT_EQ(label_x + label->width / 2.0f, margin_center);
            } else {
                // the widget supplies an alphabetic border-edge baseline; Ahem's ascent is 0.8em.
                EXPECT_FLOAT_EQ(control_y + control->height, label_y + 16.0f);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, AnInlineMovedAsAWholeHasOneRenderedLine) {
    for (bool atomic : {false, true}) {
        SCOPED_TRACE(atomic);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:20px/20px Ahem;width:100px}img{width:40px;height:100px}</style>"
            "<div id=result>XXXX <span id=scope>%s</span></div>", atomic
                ? "<img src='../layout/data/wpt-css-writing-modes/css-writing-modes/support/swatch-teal.png'>" : "XX");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* scope = dom_find_element_by_id(block, "scope");
        ASSERT_NE(scope, nullptr);
        ViewSpan* span = lam::view_as<RDT_VIEW_INLINE>(scope);
        ASSERT_NE(span, nullptr);
        for (const char* width : {"100px", "160px", "100px"}) {
            SCOPED_TRACE(width);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "width", width));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FALSE(inline_span_has_multiple_line_fragments(span));
            bool moved = strcmp(width, "100px") == 0;
            float y = (moved ? 20.0f : 0.0f) + (atomic ? 84.0f : 0.0f);
            expect_box("scope", moved ? 0.0f : 100.0f, y, 40.0f, 20.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, InsideVerticalMarkersContributeTheirDominantBaseline) {
    for (const char* mode : {"vertical-rl", "vertical-lr"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}ol{font:20px/1 Ahem;writing-mode:%s;list-style-position:inside;margin:0;padding:0}"
            "li{inline-size:200px}li::marker{font-size:40px;content:'1. '}</style>"
            "<ol><li id=result><span>X</span></li></ol>", mode);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        for (const char* size : {"20px", "60px", "20px"}) {
            SCOPED_TRACE(size);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "font-size", size));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FLOAT_EQ(block->width, strcmp(size, "60px") == 0 ? 60.0f : 40.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, InsideMarkerFragmentsKeepTheirRenderedLineIdentity) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
        "body{margin:0}#result{font:20px/20px Ahem;width:100px}"
        "li{display:inline list-item;list-style-position:inside}li::marker{content:'1. '}"
        "</style><div id=result><li id=scope>XXX</li></div>");
    DomElement* block = layout_script_source(source);
    ASSERT_NE(block, nullptr);
    DomElement* scope = dom_find_element_by_id(block, "scope");
    ASSERT_NE(scope, nullptr);
    ViewSpan* span = lam::view_as<RDT_VIEW_INLINE>(scope);
    ASSERT_NE(span, nullptr);
    for (const char* width : {"100px", "160px", "100px"}) {
        SCOPED_TRACE(width);
        ASSERT_TRUE(dom_cssom_set_inline_property(block, "width", width));
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        bool wrapped = strcmp(width, "100px") == 0;
        EXPECT_EQ(inline_span_has_multiple_line_fragments(span), wrapped);
        EXPECT_FLOAT_EQ(scope->height, wrapped ? 40.0f : 20.0f);
    }
}

TEST_F(RadiantTextQuery, DecoratedInlineLineIdentityFollowsItsSeparator) {
    for (const char* policy : {"slice", "clone"}) for (bool inside : {false, true}) {
        SCOPED_TRACE(policy);
        SCOPED_TRACE(inside);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:20px/20px Ahem;width:100px}"
            "#scope{border-left:2px solid;padding-left:8px;box-decoration-break:%s}"
            "</style><div id=result>XXX%s<span id=scope>%sXX</span></div>",
            policy, inside ? "" : " ", inside ? " " : "");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* scope = dom_find_element_by_id(block, "scope");
        ASSERT_NE(scope, nullptr);
        ViewSpan* span = lam::view_as<RDT_VIEW_INLINE>(scope);
        ASSERT_NE(span, nullptr);
        for (const char* width : {"100px", "160px", "100px"}) {
            SCOPED_TRACE(width);
            ASSERT_TRUE(dom_cssom_set_inline_property(block, "width", width));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool split = inside && strcmp(width, "100px") == 0;
            EXPECT_EQ(inline_span_has_multiple_line_fragments(span), split);
            EXPECT_FLOAT_EQ(scope->height, split ? 40.0f : 20.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, InlineFragmentLineIdentityIgnoresAtomicOverflow) {
    for (bool forced : {false, true}) {
        SCOPED_TRACE(forced);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:20px/20px Ahem}"
            "img{width:20px;height:100px}</style>"
            "<div id=result>X<br>X<br><span id=scope><source><img id=atom src='../layout/data/wpt-css-writing-modes/css-writing-modes/support/swatch-teal.png'>%s</span></div>", forced ? "<br>X" : "");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* scope = dom_find_element_by_id(block, "scope");
        DomElement* atom = dom_find_element_by_id(block, "atom");
        ASSERT_NE(scope, nullptr);
        ASSERT_NE(atom, nullptr);
        ViewSpan* span = lam::view_as<RDT_VIEW_INLINE>(scope);
        ASSERT_NE(span, nullptr);
        for (const char* height : {"100px", "40px", "100px"}) {
            SCOPED_TRACE(height);
            ASSERT_TRUE(dom_cssom_set_inline_property(atom, "height", height));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_EQ(inline_span_has_multiple_line_fragments(span), forced);
            if (!forced) EXPECT_FLOAT_EQ(scope->height, 20.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalAtomicAlignmentKeepsItsInlineAdvance) {
    for (const char* mode : {"vertical-rl", "vertical-lr"}) for (bool definite : {false, true}) for (bool gaps : {false, true})
        for (const char* orientation : {"mixed", "sideways"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(definite);
        SCOPED_TRACE(gaps);
        SCOPED_TRACE(orientation);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:inline-block;font:20px/20px Ahem;writing-mode:%s;inline-size:%s;text-orientation:%s}"
            "i{display:inline-block;inline-size:20px;vertical-align:bottom}"
            "#first{block-size:20px}#middle{block-size:80px}#last{block-size:40px}</style>"
            "<div id=result><i id=first></i>%s<i id=middle></i>%s<i id=last></i></div>",
            mode, definite ? "200px" : "auto", orientation, gaps ? " " : "", gaps ? " " : "");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* middle = dom_find_element_by_id(block, "middle");
        ASSERT_NE(middle, nullptr);
        for (const char* extent : {"80px", "120px", "80px"}) {
            SCOPED_TRACE(extent);
            ASSERT_TRUE(dom_cssom_set_inline_property(middle, "block-size", extent));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float cross_extent = strcmp(extent, "120px") == 0 ? 120.0f : 80.0f;
            EXPECT_FLOAT_EQ(block->width, cross_extent);
            float gap = gaps ? 20.0f : 0.0f;
            EXPECT_FLOAT_EQ(block->height, definite ? 200.0f : 60.0f + 2.0f * gap);
            expect_box("first", 0.0f, 0.0f, 20.0f, 20.0f);
            expect_box("middle", 0.0f, 20.0f + gap, cross_extent, 20.0f);
            expect_box("last", 0.0f, 40.0f + 2.0f * gap, 40.0f, 20.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalTextareaSourcesAlignWithTheirEditableLines) {
    for (const char* mode : {"vertical-rl", "vertical-lr"}) for (unsigned lines : {1u, 2u, 4u}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(lines);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{font:20px/0 Ahem;writing-mode:%s;inline-size:200px;padding:10px;border:3px solid;position:relative}"
            "#probe{display:inline-block;inline-size:20px;block-size:20px}"
            "textarea{font:30px/40px Ahem;writing-mode:inherit;inline-size:60px;block-size:90px;"
            "border:5px solid;padding:7px;margin:0;baseline-source:last}</style>"
            "<div id=result> <span id=probe></span> <textarea id=source>", mode);
        for (unsigned line = 0; line < lines; line++) {
            if (line) strbuf_append_str(source, "&#10;");
            strbuf_append_str(source, "X");
        }
        strbuf_append_str(source, "</textarea> </div>");
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomElement* control = dom_find_element_by_id(block, "source");
        DomElement* probe = dom_find_element_by_id(block, "probe");
        ASSERT_NE(control, nullptr);
        ASSERT_NE(probe, nullptr);
        for (const char* selected : {static_cast<const char*>(nullptr), "first", "last"}) {
            SCOPED_TRACE(selected ? selected : "initial-last");
            if (selected) {
                ASSERT_TRUE(dom_cssom_set_inline_property(control, "baseline-source", selected));
                ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            }
            // the central baseline bisects the editable line; overflow clamps to the border edge.
            float baseline = 5.0f + 7.0f + 40.0f / 2.0f;
            if (!selected || strcmp(selected, "last") == 0) baseline += (float)(lines - 1) * 40.0f;
            baseline = min(baseline, control->width);
            float probe_x = 0.0f, control_x = 0.0f;
            view_get_visual_bounds(probe, &probe_x, nullptr, nullptr, nullptr);
            view_get_visual_bounds(control, &control_x, nullptr, nullptr, nullptr);
            ASSERT_NE(control->form, nullptr);
            if (lines == 4) EXPECT_GT(control->form->last_text_baseline_overflow, 0.0f);
            float baseline_x = control_x + (strcmp(mode, "vertical-rl") == 0
                ? control->width - baseline : baseline);
            EXPECT_FLOAT_EQ(probe_x + probe->width / 2.0f, baseline_x);
            // the probe can extend beyond the control's clamped baseline, but its internal scroll overflow cannot size the line.
            float content_extent = max(control->width,
                max(baseline + probe->width / 2.0f, control->width - baseline + probe->width / 2.0f));
            EXPECT_FLOAT_EQ(block->width, content_extent + 2.0f * (10.0f + 3.0f));
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalIntrinsicWidthsPreserveZeroLeading) {
    for (const char* mode : {"vertical-lr", "vertical-rl"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:inline-flex;flex-direction:column;"
            "writing-mode:%s;font:30px/0 serif;padding:10px;border:5px solid}"
            "span{display:inline-block;width:30px;height:30px}</style>"
            "<div id=result><div><span></span></div><div><span></span></div></div>", mode);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        for (const char* leading : {"0", "50px", "0px"}) {
            SCOPED_TRACE(leading);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "line-height", leading));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float item_width = strcmp(leading, "50px") == 0 ? 50.0f : 30.0f;
            EXPECT_FLOAT_EQ(result->width, 2.0f * item_width + 30.0f);
            for (View* child = result->first_child; child; child = child->next()) {
                EXPECT_FLOAT_EQ(child->width, item_width);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalMulticolUsesLogicalIntrinsicAxesAndBalancedExtents) {
    const struct { const char* id; float width, height; } items[] = {
        {"c0", 20, 30}, {"c1", 40, 50}, {"c2", 30, 20}, {"c3", 10, 40}
    };
    for (const char* mode : {"vertical-lr", "vertical-rl"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:inline-block;writing-mode:%s;"
            "column-count:2;column-gap:7px;padding:3px;border:2px solid;font:10px/0 serif}"
            "#result>div{break-inside:avoid}span{display:inline-block;vertical-align:top}</style>"
            "<div id=result>", mode);
        for (const auto& item : items) {
            strbuf_append_format(source, "<div id=%s style='width:%gpx'>"
                "<span style='width:%gpx;height:%gpx'></span></div>", item.id, item.width, item.width, item.height);
        }
        strbuf_append_str(source, "</div>");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        for (unsigned columns : {2u, 3u, 2u}) {
            SCOPED_TRACE(columns);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "column-count", columns == 2 ? "2" : "3"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float block_extent = columns == 2 ? 60.0f : 40.0f;
            EXPECT_FLOAT_EQ(result->width, block_extent + 10.0f);
            EXPECT_FLOAT_EQ(result->height, columns * 50.0f + (columns - 1) * 7.0f + 10.0f);
            unsigned index = 0;
            for (const auto& item : items) {
                unsigned group = columns == 2 ? index / 2 : min(index, 2u);
                float cursor = index == 1 && columns == 2 ? 20.0f : index == 3 ? 30.0f : 0.0f;
                float x = 5.0f + (strcmp(mode, "vertical-rl") == 0 ? block_extent - cursor - item.width : cursor);
                expect_box(item.id, x, 5.0f + group * 57.0f, item.width, 50.0f);
                index++;
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalLineEdgesAlignPhysicalMarginBoxes) {
    for (const char* mode : {"vertical-lr", "vertical-rl"})
    for (bool over : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(over);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0;font:20px/40px Ahem}#result{writing-mode:%s;width:140px;height:180px}"
            "span{display:inline-block;vertical-align:%s}#first{width:20px;height:30px;"
            "margin-left:3px;margin-right:7px;left:10px;top:7px}"
            "#second{width:30px;height:20px;margin-left:-2px;margin-right:5px}</style>"
            "<div id=result><span id=first></span><span id=second></span></div>", mode, over ? "top" : "bottom");
        ASSERT_NE(layout_script_source(source), nullptr);
        DomElement* first = dom_find_element_by_id(document->root->as_element(), "first");
        ASSERT_NE(first, nullptr);
        float line_left = strcmp(mode, "vertical-rl") == 0 ? 100.0f : 0.0f;
        for (bool relative : {false, true, false}) {
            SCOPED_TRACE(relative);
            ASSERT_TRUE(dom_cssom_set_inline_property(first, "position", relative ? "relative" : "static"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            expect_box("first", line_left + (over ? 13.0f : 3.0f) + (relative ? 10.0f : 0.0f),
                relative ? 7.0f : 0.0f, 20, 30);
            expect_box("second", line_left + (over ? 5.0f : -2.0f), 30, 30, 20);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, PositionedEndInsetsUseTheFinalPhysicalBoxAfterVerticalFlow) {
    const struct { const char* css; bool automatic, border_box; float decorations, margin_start, margin_end; } cases[] = {
        {"", true, false, 0, 0, 0},
        {"border:2px solid;padding:3px 5px 4px;margin:7px 9px 11px 13px", true, false, 11, 7, 11},
        {"height:40px;border:2px solid;padding:3px 5px 4px;margin:7px 9px 11px 13px", false, false, 11, 7, 11},
        {"height:40px;box-sizing:border-box;border:2px solid;padding:3px 5px 4px;"
         "margin:7px 9px 11px 13px", false, true, 11, 7, 11}
    };
    for (const char* mode : {"vertical-lr", "vertical-rl"}) {
        for (const char* direction : {"ltr", "rtl"}) {
            for (bool percent : {false, true}) {
                for (bool from_end : {false, true}) {
                    for (const auto& entry : cases) {
                        SCOPED_TRACE(mode);
                        SCOPED_TRACE(direction);
                        SCOPED_TRACE(percent);
                        SCOPED_TRACE(from_end);
                        SCOPED_TRACE(entry.css);
                        StrBuf* source = strbuf_new();
                        strbuf_append_format(source,
                            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
                            "body{margin:0}#parent{position:relative;writing-mode:%s;direction:%s;"
                            "font:20px/1 Ahem;width:160px;height:160px;border:3px solid;padding:5px}"
                            "#result{position:absolute;%s:%s;%s}</style>"
                            "<div id=parent>AA AA<span id=result>X</span></div>",
                            mode, direction, from_end ? "bottom" : "top", percent ? "25%" : "40px", entry.css);
                        ASSERT_NE(layout_script_source(source), nullptr);
                        DomElement* parent = dom_find_element_by_id(document->root->as_element(), "parent");
                        DomElement* result = dom_find_element_by_id(document->root->as_element(), "result");
                        ASSERT_NE(parent, nullptr);
                        ASSERT_NE(result, nullptr);
                        const struct { const char* text; const char* height; float content, natural; } states[] = {
                            {nullptr, nullptr, 160, 20}, {"XX", nullptr, 160, 40}, {nullptr, "200px", 200, 40}
                        };
                        for (const auto& state : states) {
                            if (state.text) ASSERT_TRUE(dom_text_set_content(result->first_child->as_text(), state.text));
                            if (state.height) ASSERT_TRUE(dom_cssom_set_inline_property(parent, "height", state.height));
                            if (state.text || state.height) ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                            float content_height = state.content;
                            float natural_height = state.natural;
                            float expected_height = entry.automatic ? natural_height + entry.decorations
                                : 40.0f + (entry.border_box ? 0.0f : entry.decorations);
                            EXPECT_FLOAT_EQ(parent->height, content_height + 16);
                            EXPECT_FLOAT_EQ(result->height, expected_height);
                            float parent_y = 0.0f, result_y = 0.0f;
                            view_get_visual_bounds(parent, nullptr, &parent_y, nullptr, nullptr);
                            view_get_visual_bounds(result, nullptr, &result_y, nullptr, nullptr);
                            float padding_height = content_height + 10;
                            float end_inset = percent ? padding_height * 0.25f : 40.0f;
                            // css 2.2 §10.6.4 constrains the final border box, including physical margins.
                            float expected_y = from_end
                                ? 3 + padding_height - end_inset - entry.margin_end - expected_height
                                : 3 + end_inset + entry.margin_start;
                            EXPECT_FLOAT_EQ(result_y - parent_y, expected_y);
                        }
                        ui.document = nullptr;
                        free_document(document);
                        document = nullptr;
                    }
                }
            }
        }
    }
}

TEST_F(RadiantTextQuery, FontUnitsUseInheritedOrientationAndHorizontalSidewaysMetrics) {
    const struct { const char* css; const char* orientation; bool inherits; } cases[] = {
        {"", "upright", true}, {"text-orientation:inherit", "upright", true},
        {"text-orientation:unset", "upright", true}, {"text-orientation:initial", "mixed", false},
        {"text-orientation:mixed", "mixed", false}, {"text-orientation:sideways", "sideways", false},
        {"text-orientation:var(--orientation)", "upright", false},
        {"text-orientation:var(--missing)", "upright", true}
    };
    for (const char* mode : {"horizontal-tb", "vertical-rl", "vertical-lr", "sideways-rl", "sideways-lr"}) {
        for (const char* unit : {"ch", "ic"}) {
            for (const auto& entry : cases) {
                SCOPED_TRACE(mode);
                SCOPED_TRACE(unit);
                SCOPED_TRACE(entry.css);
                StrBuf* source = strbuf_new();
                strbuf_append_format(source,
                    "<!doctype html><style>body{margin:0;font:20px/1 Times}"
                    "#parent{--orientation:upright;writing-mode:%s;text-orientation:upright;"
                    "width:200px;height:200px}#result,#control{width:4%s;height:4%s;line-height:4%s}"
                    "</style><div id=parent><div id=result style='%s'></div>"
                    "<div id=control></div></div>", mode, unit, unit, unit, entry.css);
                ASSERT_NE(layout_script_source(source), nullptr);
                DomElement* parent = dom_find_element_by_id(document->root->as_element(), "parent");
                DomElement* result = dom_find_element_by_id(document->root->as_element(), "result");
                DomElement* control = dom_find_element_by_id(document->root->as_element(), "control");
                ASSERT_NE(parent, nullptr);
                ASSERT_NE(result, nullptr);
                ASSERT_NE(control, nullptr);
                for (const char* orientation : {"upright", "mixed", "upright"}) {
                    ASSERT_TRUE(dom_cssom_set_inline_property(parent, "text-orientation", orientation));
                    bool vertical_typography = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "vertical-lr") == 0;
                    const char* expected = vertical_typography
                        ? entry.inherits ? orientation : entry.orientation : "mixed";
                    ASSERT_TRUE(dom_cssom_set_inline_property(control, "text-orientation", expected));
                    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                    EXPECT_NEAR(result->width, control->width, 0.01f);
                    EXPECT_NEAR(result->height, control->height, 0.01f);
                }
                ui.document = nullptr;
                free_document(document);
                document = nullptr;
            }
        }
    }
}

static const char* const atomic_inline_pair_markup[] = {
    "<img id=first src='../layout/data/wpt-css-writing-modes/css-writing-modes/support/swatch-green.png'>"
    "<img id=second src='../layout/data/wpt-css-writing-modes/css-writing-modes/support/swatch-green.png'>",
    "<span id=first></span><span id=second></span>"
};

TEST_F(RadiantTextQuery, AtomicInlineBidiOrderAndAlignmentPreserveRelativeOffsets) {
    for (const char* content : atomic_inline_pair_markup) for (bool rtl : {false, true}) {
        for (const char* align : {"start", "left", "center"}) for (bool prefix : {false, true}) {
            for (bool percent : {false, true}) for (bool first_relative : {false, true})
            for (bool margins : {false, true}) {
                SCOPED_TRACE(content);
                SCOPED_TRACE(rtl);
                SCOPED_TRACE(align);
                SCOPED_TRACE(prefix);
                SCOPED_TRACE(percent);
                SCOPED_TRACE(first_relative);
                SCOPED_TRACE(margins);
                const char* target_id = first_relative ? "first" : "second";
                StrBuf* source = strbuf_new();
                strbuf_append_format(source,
                    "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
                    "body{margin:0;font:20px/1 Ahem}#result{width:200px;height:60px;direction:%s;text-align:%s}"
                    "#result span{display:inline-block}#first,#second{height:20px;vertical-align:top}"
                    "#first{width:20px;%s}#second{width:30px;%s}#%s{position:relative;%s}</style>"
                    "<div id=result>%s%s</div>", rtl ? "rtl" : "ltr", align,
                    margins ? "margin-left:5px;margin-right:7px" : "",
                    margins ? "margin-left:-2px;margin-right:3px" : "", target_id,
                    percent ? "right:10%;top:10%" : "left:10px;top:7px", prefix ? "A" : "", content);
                ASSERT_NE(layout_script_source(source), nullptr);
                DomElement* parent = dom_find_element_by_id(document->root->as_element(), "result");
                DomElement* first = dom_find_element_by_id(document->root->as_element(), "first");
                DomElement* second = dom_find_element_by_id(document->root->as_element(), "second");
                ASSERT_NE(parent, nullptr);
                ASSERT_NE(first, nullptr);
                ASSERT_NE(second, nullptr);
                DomElement* target = first_relative ? first : second;
                const struct { const char* position; float width, height; } states[] = {
                    {"relative", 200, 60}, {"static", 200, 60}, {"relative", 160, 80}
                };
                for (const auto& state : states) {
                    ASSERT_TRUE(dom_cssom_set_inline_property(target, "position", state.position));
                    if (state.width < 200) {
                        ASSERT_TRUE(dom_cssom_set_inline_property(parent, "width", "160px"));
                        ASSERT_TRUE(dom_cssom_set_inline_property(parent, "height", "80px"));
                    }
                    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                    float prefix_width = prefix ? 20.0f : 0.0f;
                    float first_margin = margins ? 5.0f : 0.0f;
                    float second_margin = margins ? -2.0f : 0.0f;
                    float first_advance = 20 + first_margin + (margins ? 7 : 0);
                    float second_advance = 30 + second_margin + (margins ? 3 : 0);
                    float line_width = first_advance + second_advance + prefix_width;
                    float start = strcmp(align, "center") == 0 ? (state.width - line_width) / 2
                        : rtl && strcmp(align, "start") == 0 ? state.width - line_width : 0.0f;
                    float first_x = start + (rtl ? second_advance : prefix_width) + first_margin;
                    float second_x = start + (rtl ? 0.0f : prefix_width + first_advance) + second_margin;
                    bool relative = strcmp(state.position, "relative") == 0;
                    float dx = relative ? percent ? -0.1f * state.width : 10.0f : 0.0f;
                    float dy = relative ? percent ? 0.1f * state.height : 7.0f : 0.0f;
                    expect_box("first", first_x + (first_relative ? dx : 0),
                        first_relative ? dy : 0, 20, 20);
                    expect_box("second", second_x + (first_relative ? 0 : dx),
                        first_relative ? 0 : dy, 30, 20);
                }
                ui.document = nullptr;
                free_document(document);
                document = nullptr;
            }
        }
    }
}

TEST_F(RadiantTextQuery, VerticalAtomicBidiUsesPhysicalInlineAdvancesAndRelativeOffsets) {
    for (const char* mode : {"vertical-lr", "vertical-rl"}) for (bool rtl : {false, true}) {
        for (const char* align : {"start", "left", "center"}) {
            for (const char* content : atomic_inline_pair_markup)
            for (bool margins : {false, true}) for (bool percentages : {false, true}) {
                SCOPED_TRACE(mode);
                SCOPED_TRACE(rtl);
                SCOPED_TRACE(align);
                SCOPED_TRACE(content);
                SCOPED_TRACE(margins);
                SCOPED_TRACE(percentages);
                StrBuf* source = strbuf_new();
                strbuf_append_format(source,
                    "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
                    "body{margin:0;font:20px/40px Ahem}#result{width:140px;height:180px;"
                    "writing-mode:%s;direction:%s;text-align:%s}#result span{display:inline-block}"
                    "#first,#second{vertical-align:top}#first{width:20px;height:30px;%s%s}"
                    "#second{width:30px;height:20px;%s}</style><div id=result>%s</div>",
                    mode, rtl ? "rtl" : "ltr", align,
                    percentages ? "right:10%;bottom:10%;" : "left:10px;top:7px;",
                    margins ? "margin-top:5px;margin-bottom:7px;" : "",
                    margins ? "margin-top:-2px;margin-bottom:3px;" : "", content);
                ASSERT_NE(layout_script_source(source), nullptr);
                DomElement* first = dom_find_element_by_id(document->root->as_element(), "first");
                DomElement* second = dom_find_element_by_id(document->root->as_element(), "second");
                ASSERT_NE(first, nullptr);
                ASSERT_NE(second, nullptr);
                float normal_x = 0.0f, second_x = 0.0f;
                view_get_visual_bounds(first, &normal_x, nullptr, nullptr, nullptr);
                view_get_visual_bounds(second, &second_x, nullptr, nullptr, nullptr);
                for (bool relative : {false, true, false}) {
                    SCOPED_TRACE(relative);
                    ASSERT_TRUE(dom_cssom_set_inline_property(first, "position", relative ? "relative" : "static"));
                    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                    float first_before = margins ? 5.0f : 0.0f;
                    float first_after = margins ? 7.0f : 0.0f;
                    float second_before = margins ? -2.0f : 0.0f;
                    float second_after = margins ? 3.0f : 0.0f;
                    float first_advance = first_before + 30.0f + first_after;
                    float second_advance = second_before + 20.0f + second_after;
                    float remaining = 180.0f - first_advance - second_advance;
                    float start = strcmp(align, "center") == 0 ? remaining * 0.5f
                        : rtl && strcmp(align, "start") == 0 ? remaining : 0.0f;
                    float relative_x = relative ? (percentages ? -14.0f : 10.0f) : 0.0f;
                    float relative_y = relative ? (percentages ? -18.0f : 7.0f) : 0.0f;
                    float first_y = start + (rtl ? second_advance : 0.0f) + first_before + relative_y;
                    float second_y = start + (rtl ? 0.0f : first_advance) + second_before;
                    expect_box("first", normal_x + relative_x, first_y, 20, 30);
                    expect_box("second", second_x, second_y, 30, 20);
                }
                ui.document = nullptr;
                free_document(document);
                document = nullptr;
            }
        }
    }
}

TEST_F(RadiantTextQuery, RootRelativeWebFontsMatchTheirDirectFileSources) {
    const struct { const char* source; const char* file; const char* format; const char* text; } cases[] = {
        {"/fonts/Ahem.ttf", "ref/wpt/fonts/Ahem.ttf", "truetype", "XXXX"},
        {"file:///fonts/Ahem.ttf", "ref/wpt/fonts/Ahem.ttf", "truetype", "XXXX"},
        {"/fonts/sileot-webfont.woff", "ref/wpt/fonts/sileot-webfont.woff", "woff", u8"a > א"}
    };
    size_t index = 0;
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.source);
        Url* base = get_current_dir();
        ASSERT_NE(base, nullptr);
        Url* direct = url_parse_path_or_url(entry.file, base);
        url_destroy(base);
        ASSERT_NE(direct, nullptr);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:root_probe_%zu;src:url('%s') format('%s')}"
            "@font-face{font-family:direct_probe_%zu;src:url('%s') format('%s')}"
            "body{margin:0;font-size:20px}span{display:inline-block;vertical-align:top}"
            "#result{font-family:root_probe_%zu}#control{font-family:direct_probe_%zu}</style>"
            "<span id=result>%s</span><span id=control>%s</span>",
            index, entry.source, entry.format, index, url_get_href(direct), entry.format,
            index, index, entry.text, entry.text);
        url_destroy(direct);
        ASSERT_NE(layout_script_source(source), nullptr);
        DomElement* result = dom_find_element_by_id(document->root->as_element(), "result");
        DomElement* control = dom_find_element_by_id(document->root->as_element(), "control");
        ASSERT_NE(result, nullptr);
        ASSERT_NE(control, nullptr);
        EXPECT_FLOAT_EQ(result->width, control->width);
        EXPECT_FLOAT_EQ(result->height, control->height);
        if (index < 2) EXPECT_FLOAT_EQ(control->width, 80);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
        index++;
    }
}

TEST_F(RadiantTextQuery, ParagraphStartAlignmentUsesEachContainersDirection) {
    const char* cases[] = {
        "<div id=result>X</div>",
        "<div id=result style=display:inline-block>X</div>",
        "<div class=table><div class=row><div id=result style=display:table-cell>X</div></div></div>",
        "<div class=table><div id=result style=display:table-caption>X</div><div class=row><div class=cell></div></div></div>"
    };
    const struct { const char* direction; const char* align; float x; } states[] = {
        {"rtl", "", 50}, {"ltr", "", 0}, {"rtl", "", 50},
        {"rtl", "left", 0}, {"rtl", "center", 25}, {"rtl", "right", 50},
        {"ltr", "right", 50}, {"rtl", "start", 50}, {"ltr", "start", 0},
        {"rtl", "end", 0}, {"ltr", "end", 50}, {"rtl", "initial", 50},
        {"rtl", "unset", 50}
    };
    for (const char* content : cases) {
        SCOPED_TRACE(content);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0;font:50px/1 Ahem}.table{display:table;width:100px}.row{display:table-row}"
            ".cell{display:table-cell}#result{width:100px;height:100px;direction:rtl}</style>%s", content);
        ASSERT_NE(layout_script_source(source), nullptr);
        DomElement* result = dom_find_element_by_id(document->root->as_element(), "result");
        ASSERT_NE(result, nullptr);
        for (const auto& state : states) {
            SCOPED_TRACE(state.direction);
            SCOPED_TRACE(state.align);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "direction", state.direction));
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "text-align", state.align));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            ASSERT_NE(result->first_child, nullptr);
            DomText* text = result->first_child->as_text();
            ASSERT_NE(text, nullptr);
            ASSERT_NE(text->rect, nullptr);
            EXPECT_FLOAT_EQ(result->width, 100.0f);
            EXPECT_FLOAT_EQ(text->rect->x, state.x);
            EXPECT_FLOAT_EQ(text->rect->width, 50.0f);
            EXPECT_EQ(text->rect->next, nullptr);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, FlexItemParagraphDirectionSurvivesWrappingAndSiblingLayout) {
    ASSERT_NE(load_script_source(R"HTML(<!doctype html><style>
        @font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}
        body{margin:0;display:flex}
        .box{position:relative;width:80px;height:120px;font:20px/1 Ahem}
        .anchor{position:absolute}
    </style><div class=box>1 2 34<span id=first class=anchor>X</span></div>
    <div class=box style=direction:rtl>1 2 34<span id=middle class=anchor>X</span></div>
    <div class=box>1 2 34<span id=result class=anchor>X</span></div>)HTML"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    expect_box("first", 40, 20, 20, 20);
    expect_box("middle", 100, 20, 20, 20);
    expect_box("result", 200, 20, 20, 20);
}

TEST_F(RadiantTextQuery, InlineStaticPositionsUseLogicalLineAxesAndDirectionalMarginEdges) {
    // independently measured with unequal physical dimensions in Chromium.
    const struct { const char* mode; const char* direction; float width, height, x, y, margin_x, margin_y; } flows[] = {
        {"horizontal-tb", "ltr", 80, 120, 40, 20, 43, 27},
        {"horizontal-tb", "rtl", 80, 120, 20, 20, 17, 27},
        {"vertical-lr", "ltr", 120, 80, 20, 40, 27, 43},
        {"vertical-lr", "rtl", 120, 80, 20, 20, 27, 17},
        {"vertical-rl", "ltr", 120, 80, 80, 40, 73, 43},
        {"vertical-rl", "rtl", 120, 80, 80, 20, 73, 17}
    };
    const struct { const char* name; bool nested, margins, insets; const char* css; } variants[] = {
        {"direct", false, false, false, ""},
        {"nested", true, false, false, ""},
        {"margins", false, true, false, "margin-inline:3px 5px;margin-block:7px 11px"},
        {"insets", false, false, true, "left:17px;top:19px"}
    };
    for (const auto& flow : flows) for (const auto& variant : variants) {
        SCOPED_TRACE(flow.mode);
        SCOPED_TRACE(flow.direction);
        SCOPED_TRACE(variant.name);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{position:relative;font:20px/1 Ahem;writing-mode:%s;direction:%s;"
            "width:%gpx;height:%gpx}#anchor{position:absolute;%s}</style>"
            "<div id=result>%s1 2 34<span id=anchor>X</span>%s</div>",
            flow.mode, flow.direction, flow.width, flow.height, variant.css,
            variant.nested ? "<i style='font-style:normal'>" : "", variant.nested ? "</i>" : "");
        ASSERT_NE(load_script_source(source->str), nullptr);
        strbuf_free(source);
        process_document_font_faces(&ui, document);
        layout_html_doc(&ui, document, false);
        expect_box("anchor", variant.insets ? 17.0f : variant.margins ? flow.margin_x : flow.x,
            variant.insets ? 19.0f : variant.margins ? flow.margin_y : flow.y, 20, 20);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalStaticAnchorsPreserveOriginsAlignmentAndContainingBlocks) {
    const struct { const char* mode; const char* direction; const char* variant; float x, y; } cases[] = {
        {"vertical-lr", "ltr", "padding", 33, 45},
        {"vertical-lr", "ltr", "inline-cb", 20, 40},
        {"vertical-lr", "ltr", "orthogonal", 20, 40},
        {"vertical-lr", "ltr", "empty-positive", 0, 10},
        {"vertical-lr", "ltr", "empty-negative", 0, -10},
        {"vertical-lr", "ltr", "center", 20, 60},
        {"vertical-lr", "ltr", "end", 20, 80},
        {"vertical-lr", "rtl", "padding", 33, 25},
        {"vertical-lr", "rtl", "inline-cb", 20, 20},
        {"vertical-lr", "rtl", "orthogonal", 20, 20},
        {"vertical-lr", "rtl", "empty-positive", 0, 50},
        {"vertical-lr", "rtl", "empty-negative", 0, 70},
        {"vertical-lr", "rtl", "center", 20, 0},
        {"vertical-lr", "rtl", "end", 20, -20},
        {"vertical-rl", "ltr", "padding", 93, 45},
        {"vertical-rl", "ltr", "inline-cb", 80, 40},
        {"vertical-rl", "ltr", "orthogonal", 80, 40},
        {"vertical-rl", "ltr", "empty-positive", 100, 10},
        {"vertical-rl", "ltr", "empty-negative", 100, -10},
        {"vertical-rl", "ltr", "center", 80, 60},
        {"vertical-rl", "ltr", "end", 80, 80},
        {"vertical-rl", "rtl", "padding", 93, 25},
        {"vertical-rl", "rtl", "inline-cb", 80, 20},
        {"vertical-rl", "rtl", "orthogonal", 80, 20},
        {"vertical-rl", "rtl", "empty-positive", 100, 50},
        {"vertical-rl", "rtl", "empty-negative", 100, 70},
        {"vertical-rl", "rtl", "center", 80, 0},
        {"vertical-rl", "rtl", "end", 80, -20},
        {"vertical-lr", "ltr", "block-empty", 0, 0},
        {"vertical-lr", "ltr", "block-after", 40, 0},
        {"vertical-lr", "rtl", "block-empty", 0, 60},
        {"vertical-lr", "rtl", "block-after", 40, 60},
        {"vertical-rl", "ltr", "block-empty", 100, 0},
        {"vertical-rl", "ltr", "block-after", 60, 0},
        {"vertical-rl", "rtl", "block-empty", 100, 60},
        {"vertical-rl", "rtl", "block-after", 60, 60},
    };
    for (bool sideways : {false, true}) for (const auto& entry : cases) {
        const char* mode = sideways ? (strcmp(entry.mode, "vertical-lr") == 0 ? "sideways-lr" : "sideways-rl") : entry.mode;
        SCOPED_TRACE(mode);
        SCOPED_TRACE(entry.mode);
        SCOPED_TRACE(entry.direction);
        SCOPED_TRACE(entry.variant);
        bool inline_cb = strcmp(entry.variant, "inline-cb") == 0;
        bool empty_positive = strcmp(entry.variant, "empty-positive") == 0;
        bool empty_negative = strcmp(entry.variant, "empty-negative") == 0;
        const char* css = strcmp(entry.variant, "padding") == 0
            ? "padding:3px 5px 7px 11px;border:2px solid" : empty_positive
            ? "text-indent:10px" : empty_negative ? "text-indent:-10px"
            : strcmp(entry.variant, "center") == 0 ? "text-align:center"
            : strcmp(entry.variant, "end") == 0 ? "text-align:end" : "";
        bool block_origin = strncmp(entry.variant, "block-", 6) == 0;
        bool empty_line = empty_positive || empty_negative ||
            strcmp(entry.variant, "block-empty") == 0;
        const char* anchor_css = block_origin
            ? "display:block;width:20px;height:20px" : strcmp(entry.variant, "orthogonal") == 0
            ? "writing-mode:horizontal-tb;width:20px;height:20px"
            : empty_positive || empty_negative ? "width:20px;height:20px" : "";
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{position:relative;font:20px/1 Ahem;writing-mode:%s;direction:%s;"
            "width:120px;height:80px;%s}#anchor{position:absolute;%s}</style>"
            "<div id=result>%s%s<span id=anchor>X</span>%s</div>",
            mode, entry.direction, css, anchor_css,
            inline_cb ? "<span style='position:relative'>" : "",
            empty_line ? "" : "1 2 34", inline_cb ? "</span>" : "");
        ASSERT_NE(load_script_source(source->str), nullptr);
        strbuf_free(source);
        process_document_font_faces(&ui, document);
        layout_html_doc(&ui, document, false);
        float expected_y = sideways && strcmp(entry.mode, "vertical-lr") == 0
            ? (strcmp(entry.variant, "padding") == 0 ? 90 : 80) - entry.y - 20 : entry.y;
        expect_box("anchor", entry.x, expected_y, 20, 20);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, StaticBlockAnchorsFollowTheirOriginalFormattingContext) {
    struct Size { const char* mode; const char* width; const char* height; float w; float h; };
    const Size sizes[] = {{"inherit", "20px", "15px", 20, 15},
        {"horizontal-tb", "37px", "23px", 37, 23}, {"inherit", "20px", "15px", 20, 15}};
    for (const char* cb_mode : {"horizontal-tb", "vertical-lr", "vertical-rl"})
    for (const char* cb_direction : {"ltr", "rtl"})
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl", "sideways-lr", "sideways-rl"})
    for (const char* direction : {"ltr", "rtl"}) {
        SCOPED_TRACE(cb_mode);
        SCOPED_TRACE(cb_direction);
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#cb{position:relative;writing-mode:%s;direction:%s;"
            "width:220px;height:180px;border:solid;border-width:2px 4px 6px 8px;padding:3px 5px 7px 9px}"
            "#result{writing-mode:%s;direction:%s;width:90px;height:70px;border:1px solid;padding:2px}"
            "#prefix{width:13px;height:11px}#anchor{position:absolute;width:20px;height:15px}</style>"
            "<div id=cb><div id=result><div id=prefix></div><div id=anchor></div></div></div>",
            cb_mode, cb_direction, mode, direction);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* prefix = dom_find_element_by_id(result, "prefix");
        DomElement* anchor = dom_find_element_by_id(result, "anchor");
        ASSERT_NE(prefix, nullptr);
        ASSERT_NE(anchor, nullptr);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        bool reverse_block = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "sideways-rl") == 0;
        bool reverse_inline = (strcmp(direction, "rtl") == 0) != (strcmp(mode, "sideways-lr") == 0);
        for (bool visible : {false, true, false}) for (const Size& size : sizes) {
            SCOPED_TRACE(visible);
            SCOPED_TRACE(size.mode);
            SCOPED_TRACE(size.width);
            ASSERT_TRUE(dom_cssom_set_inline_property(prefix, "display", visible ? "block" : "none"));
            ASSERT_TRUE(dom_cssom_set_inline_property(anchor, "writing-mode", size.mode));
            ASSERT_TRUE(dom_cssom_set_inline_property(anchor, "width", size.width));
            ASSERT_TRUE(dom_cssom_set_inline_property(anchor, "height", size.height));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float parent_x = 0, parent_y = 0, anchor_x = 0, anchor_y = 0;
            view_get_visual_bounds(result, &parent_x, &parent_y, nullptr, nullptr);
            view_get_visual_bounds(anchor, &anchor_x, &anchor_y, nullptr, nullptr);
            // the static rectangle follows the parent flow; the outer containing block only supplies its origin.
            float expected_x = vertical ? (reverse_block ? 93 - (visible ? 13 : 0) - size.w
                : 3 + (visible ? 13 : 0)) : strcmp(direction, "rtl") == 0 ? 93 - size.w : 3;
            float expected_y = vertical ? (reverse_inline ? 73 - size.h : 3) : 3 + (visible ? 11 : 0);
            EXPECT_FLOAT_EQ(anchor_x - parent_x, expected_x);
            EXPECT_FLOAT_EQ(anchor_y - parent_y, expected_y);
            EXPECT_FLOAT_EQ(anchor->width, size.w);
            EXPECT_FLOAT_EQ(anchor->height, size.h);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, HtmlPrincipalWritingModeChangesUsedRootFlowAndPreservesComputedMapping) {
    ASSERT_NE(load_script_source(R"HTML(<!doctype html><style>
        html { writing-mode:vertical-rl }
        body { writing-mode:horizontal-tb; margin:0; font:20px/1 monospace }
    </style><span id=prefix>XX XX XX</span><div id=result style="width:40px;height:20px"></div>)HTML"), nullptr);
    layout_html_doc(&ui, document, false);
    ViewBlock* root = lam::view_require_block(static_cast<View*>(document->root.get()));
    DomElement* body = radiant_document_body_element(document);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(layout_element_css_writing_mode(root), CSS_VALUE_VERTICAL_RL);
    EXPECT_EQ(layout_element_logical_sides(root).block_start, CSS_BOX_SIDE_RIGHT);
    EXPECT_EQ(layout_block_writing_mode(root), WM_HORIZONTAL_TB);
    ASSERT_TRUE(dom_cssom_set_inline_property(body, "writing-mode", "vertical-lr"));
    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
    EXPECT_EQ(layout_block_writing_mode(root), WM_VERTICAL_LR);
    EXPECT_EQ(layout_element_css_writing_mode(root), CSS_VALUE_VERTICAL_RL);
    EXPECT_EQ(layout_element_logical_sides(root).block_start, CSS_BOX_SIDE_RIGHT);
    EXPECT_FALSE(block_context_establishes_bfc(lam::view_require_block(static_cast<View*>(body))));
    DomElement* prefix = dom_find_element_by_id(document->root->as_element(), "prefix");
    ASSERT_NE(prefix, nullptr);
    DomText* text = prefix->first_child->as_text();
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->rect, nullptr);
    EXPECT_EQ(text->rect->length, 8);
    EXPECT_EQ(text->rect->next, nullptr);
    ASSERT_TRUE(dom_cssom_set_inline_property(body, "writing-mode", "inherit"));
    ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
    EXPECT_EQ(layout_element_css_writing_mode(body), CSS_VALUE_VERTICAL_RL);
    EXPECT_EQ(layout_block_writing_mode(root), WM_VERTICAL_RL);
}

TEST_F(RadiantTextQuery, VerticalRootGeneratedBlocksParticipateInNormalFlow) {
    const struct { const char* classes; bool before, after; float child_width; float lr_width, rl_width, lr_body, rl_body; } states[] = {
        {"", true, true, 40, 104, 110, 30, 34},
        {"hide-before", false, true, 40, 79, 83, 5, 34},
        {"", true, true, 40, 104, 110, 30, 34},
        {"hide-after", true, false, 40, 79, 81, 30, 5},
        {"", true, true, 40, 104, 110, 30, 34},
        {"hide-both", false, false, 40, 54, 54, 5, 5},
        {"", true, true, 40, 104, 110, 30, 34},
        {"", true, true, 70, 134, 140, 30, 34},
        {"", true, true, 40, 104, 110, 30, 34}
    };
    for (const char* mode : {"vertical-lr", "vertical-rl"}) for (bool root_declared : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(root_declared);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>%s"
            "html::before{content:'';display:block;width:20px;height:30px;margin:0 7px 0 3px}"
            "html::after{content:'';display:block;width:10px;height:20px;margin:0 13px 0 11px}"
            "html.hide-before::before,html.hide-after::after,html.hide-both::before,html.hide-both::after{display:none}"
            "body{writing-mode:%s;margin:4px 9px 6px 5px}#child{width:40px;height:60px}"
            "</style><body id=result><div id=child></div></body>",
            root_declared ? "html{writing-mode:horizontal-tb}" : "", mode);
        DomElement* body = layout_script_source(source);
        ASSERT_NE(body, nullptr);
        ViewBlock* root = lam::view_require_block(static_cast<View*>(document->root.get()));
        DomElement* child = dom_find_element_by_id(root, "child");
        ASSERT_NE(child, nullptr);
        bool reverse = strcmp(mode, "vertical-rl") == 0;
        for (const auto& state : states) {
            SCOPED_TRACE(state.classes);
            SCOPED_TRACE(state.child_width);
            ASSERT_TRUE(root->set_attribute("class", state.classes));
            dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, root, root->parent, "class", nullptr);
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "width", state.child_width == 70 ? "70px" : "40px"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float width = reverse ? state.rl_width : state.lr_width;
            float root_x = reverse ? 320.0f - width : 0.0f;
            EXPECT_FLOAT_EQ(root->x, root_x);
            EXPECT_FLOAT_EQ(root->width, width);
            EXPECT_FLOAT_EQ(root->height, 200.0f);
            EXPECT_EQ(layout_element_css_writing_mode(root), CSS_VALUE_HORIZONTAL_TB);
            expect_box("result", root_x + (reverse ? state.rl_body : state.lr_body), 4, state.child_width, 190);
            if (state.before || state.after) ASSERT_NE(root->pseudo, nullptr);
            for (bool before : {false, true}) {
                DomElement* pseudo = before ? root->pseudo->before : root->pseudo->after;
                ASSERT_NE(pseudo, nullptr);
                if (!(before ? state.before : state.after)) {
                    EXPECT_EQ(resolve_display_value(pseudo).outer, CSS_VALUE_NONE);
                    EXPECT_EQ(pseudo->display.outer, CSS_VALUE_NONE);
                    EXPECT_EQ(pseudo->view_type, RDT_VIEW_NONE);
                    EXPECT_FLOAT_EQ(pseudo->width, 0.0f);
                    EXPECT_FLOAT_EQ(pseudo->height, 0.0f);
                    continue;
                }
                EXPECT_EQ(layout_element_css_writing_mode(pseudo), CSS_VALUE_HORIZONTAL_TB);
                float x = before ? (reverse ? width - 27.0f : 3.0f)
                    : reverse ? 11.0f : width - 23.0f;
                float visual_x = 0.0f, visual_y = 0.0f;
                view_get_visual_bounds(pseudo, &visual_x, &visual_y, nullptr, nullptr);
                EXPECT_FLOAT_EQ(visual_x, root_x + x);
                EXPECT_FLOAT_EQ(visual_y, 0.0f);
                EXPECT_FLOAT_EQ(pseudo->width, before ? 20.0f : 10.0f);
                EXPECT_FLOAT_EQ(pseudo->height, before ? 30.0f : 20.0f);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, HtmlPrincipalModeRespectsContainmentAndBodyVisibility) {
    const struct { const char* root; const char* body; CssEnum computed; WritingMode root_mode, body_mode; } flows[] = {
        {"horizontal-tb", "vertical-lr", CSS_VALUE_HORIZONTAL_TB, WM_HORIZONTAL_TB, WM_VERTICAL_LR},
        {"horizontal-tb", "vertical-rl", CSS_VALUE_HORIZONTAL_TB, WM_HORIZONTAL_TB, WM_VERTICAL_RL},
        {"vertical-lr", "horizontal-tb", CSS_VALUE_VERTICAL_LR, WM_VERTICAL_LR, WM_HORIZONTAL_TB},
        {"vertical-rl", "horizontal-tb", CSS_VALUE_VERTICAL_RL, WM_VERTICAL_RL, WM_HORIZONTAL_TB},
        {"vertical-lr", "vertical-rl", CSS_VALUE_VERTICAL_LR, WM_VERTICAL_LR, WM_VERTICAL_RL},
        {"vertical-rl", "vertical-lr", CSS_VALUE_VERTICAL_RL, WM_VERTICAL_RL, WM_VERTICAL_LR}
    };
    for (const auto& flow : flows) for (bool root_contained : {false, true}) {
        SCOPED_TRACE(flow.root);
        SCOPED_TRACE(flow.body);
        SCOPED_TRACE(root_contained);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>html{writing-mode:%s;--scope:paint}body{writing-mode:%s;margin:0}"
            "#child{width:40px;height:60px}</style><body id=result><div id=child></div></body>",
            flow.root, flow.body);
        DomElement* body = layout_script_source(source);
        ASSERT_NE(body, nullptr);
        ViewBlock* root = lam::view_require_block(static_cast<View*>(document->root.get()));
        DomElement* target = root_contained ? root->as_element() : body;
        const struct { const char* contain; bool hidden, contained; } states[] = {
            {"none", false, false}, {"paint", false, true}, {"var(--scope)", false, true},
            {"unset", false, false}, {"none", false, false}, {"none", true, false}, {"none", false, false}
        };
        for (const auto& state : states) {
            SCOPED_TRACE(state.contain);
            SCOPED_TRACE(state.hidden);
            ASSERT_TRUE(dom_cssom_set_inline_property(target, "contain", state.contain));
            ASSERT_TRUE(dom_cssom_set_inline_property(body, "display", state.hidden ? "none" : "block"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            WritingMode used_mode = state.hidden || state.contained
                ? flow.root_mode : flow.body_mode;
            EXPECT_EQ(layout_element_css_writing_mode(root), flow.computed);
            EXPECT_EQ(layout_block_writing_mode(root), used_mode);
            bool vertical_root = used_mode != WM_HORIZONTAL_TB;
            bool vertical_body = flow.body_mode != WM_HORIZONTAL_TB;
            float root_width = vertical_root ? (state.hidden ? 0.0f : 40.0f) : 320.0f;
            float body_height = vertical_body && vertical_root ? 200.0f : 60.0f;
            float root_height = vertical_root ? 200.0f : state.hidden ? 0.0f : body_height;
            EXPECT_FLOAT_EQ(root->width, root_width);
            EXPECT_FLOAT_EQ(root->height, root_height);
            EXPECT_FLOAT_EQ(root->x, used_mode == WM_VERTICAL_RL ? 320.0f - root_width : 0.0f);
            if (!state.hidden) {
                EXPECT_FLOAT_EQ(body->width, vertical_body || vertical_root ? 40.0f : 320.0f);
                EXPECT_FLOAT_EQ(body->height, body_height);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalBlockMarginsCollapseAcrossNonemptyAncestorChains) {
    const struct { const char* mode; bool vertical; bool reverse; } flows[] = {
        {"horizontal-tb", false, false}, {"vertical-lr", true, false}, {"vertical-rl", true, true}
    };
    // CSS2 §8.3.1 geometry, independently measured in Chromium for all flows.
    const struct {
        const char* name;
        const char* parent_css;
        float child_margin;
        bool chain;
        float parent_start, parent_size, child_start, after_start;
    } cases[] = {
        {"positive", "", 16, false, 16, 40, 16, 72},
        {"negative", "", -16, false, -8, 40, -8, 24},
        {"padding-start", "padding-block-start:5px", 16, false, 8, 61, 29, 85},
        {"padding-end", "padding-block-end:5px", 16, false, 16, 61, 16, 85},
        {"flow-root", "display:flow-root", 16, false, 8, 72, 24, 88},
        {"definite", "block-size:100px", 16, false, 16, 100, 16, 124},
        {"minimum", "min-block-size:10px", 16, false, 16, 40, 16, 72},
        {"mixed-chain", "", -16, true, 4, 40, 4, 40}
    };
    for (const auto& flow : flows) for (const auto& sample : cases) {
        SCOPED_TRACE(flow.mode);
        SCOPED_TRACE(sample.name);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0;font:16px/1 Ahem}"
            "#result{display:flow-root;inline-size:160px;block-size:200px;writing-mode:%s}"
            "#parent{margin-block:8px;%s}#child{inline-size:40px;block-size:%s;margin-block:%gpx}"
            "#after{inline-size:40px;block-size:20px}</style>"
            "<div id=result><div id=parent><div id=child>%s</div></div><div id=after></div></div>",
            flow.mode, sample.parent_css, sample.chain ? "auto" : "40px", sample.child_margin,
            sample.chain ? "<div style='margin-block:20px 12px;inline-size:40px;block-size:40px'></div>" : "");
        ASSERT_NE(load_script_source(source->str), nullptr);
        strbuf_free(source);
        layout_html_doc(&ui, document, false);
        auto expect_logical_box = [&](const char* id, float start, float block_size, float inline_size) {
            float physical_start = flow.reverse ? 200.0f - start - block_size : start;
            expect_box(id, flow.vertical ? physical_start : 0.0f,
                flow.vertical ? 0.0f : start,
                flow.vertical ? block_size : inline_size,
                flow.vertical ? inline_size : block_size);
        };
        expect_logical_box("parent", sample.parent_start, sample.parent_size, 160);
        expect_logical_box("child", sample.child_start, 40, 40);
        expect_logical_box("after", sample.after_start, 20, 40);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, HtmlUaSpacingFollowsComputedFlowFontAndAuthorOverrides) {
    const struct { const char* mode; const char* direction; bool vertical; bool rtl; } flows[] = {
        {"horizontal-tb", "ltr", false, false}, {"horizontal-tb", "rtl", false, true},
        {"vertical-lr", "ltr", true, false}, {"vertical-lr", "rtl", true, true},
        {"vertical-rl", "rtl", true, true}
    };
    for (const auto& flow : flows) {
        SCOPED_TRACE(flow.mode);
        SCOPED_TRACE(flow.direction);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{display:flex;font:20px/1 Ahem;writing-mode:%s;direction:%s}"
            "#result{font-size:30px}#physical{margin-left:7px;margin-block-end:9px}"
            "#reset{all:initial}#horizontal{display:flow-root;writing-mode:horizontal-tb}</style>"
            "<p id=result>XX</p><h1 id=heading>XX</h1><pre id=pre>XX</pre>"
            "<blockquote id=quote>XX</blockquote><dl><dd id=definition>XX</dd></dl>"
            "<ul id=list><li>XX<ul id=nested><li>XX</li></ul></li></ul>"
            "<ul id=hint dir=rtl><li>XX</li></ul>"
            "<ul id=css-hint dir=rtl style=direction:ltr><li>XX</li></ul>"
            "<ul id=initial-hint dir=rtl style=direction:initial><li>XX</li></ul>"
            "<ul id=inherit-hint dir=rtl style=direction:inherit><li>XX</li></ul>"
            "<div id=pair style='margin-block:11px 17px;padding-inline:13px 19px'></div>"
            "<p id=physical>XX</p><p id=reset>XX</p>"
            "<div id=horizontal><p id=orthogonal>XX</p></div>", flow.mode, flow.direction);
        DomElement* result = load_script_source(source->str);
        strbuf_free(source);
        ASSERT_NE(result, nullptr);
        layout_html_doc(&ui, document, false);
        auto spacing = [&](const char* id, bool padding, float top, float right, float bottom, float left) {
            DomElement* element = dom_find_element_by_id(document->root->as_element(), id);
            ASSERT_NE(element, nullptr) << id;
            ASSERT_NE(element->boundary(), nullptr) << id;
            // inspect resolved margins before normal-flow collapse consumes them.
            const Spacing* value = padding ? &element->boundary()->padding :
                view_boundary_flow_margin(element->boundary());
            EXPECT_FLOAT_EQ(value->top, top) << id;
            EXPECT_FLOAT_EQ(value->right, right) << id;
            EXPECT_FLOAT_EQ(value->bottom, bottom) << id;
            EXPECT_FLOAT_EQ(value->left, left) << id;
        };
        auto block_margins = [&](const char* id, float value) {
            spacing(id, false, flow.vertical ? 0.0f : value, flow.vertical ? value : 0.0f,
                flow.vertical ? 0.0f : value, flow.vertical ? value : 0.0f);
        };
        block_margins("result", 30.0f);
        block_margins("heading", 26.8f);
        block_margins("pre", 20.0f);
        block_margins("list", 20.0f);
        spacing("nested", false, 0, 0, 0, 0);
        spacing("quote", false, flow.vertical ? 40.0f : 20.0f, flow.vertical ? 20.0f : 40.0f,
            flow.vertical ? 40.0f : 20.0f, flow.vertical ? 20.0f : 40.0f);
        const float top = flow.vertical && !flow.rtl ? 40.0f : 0.0f;
        const float right = !flow.vertical && flow.rtl ? 40.0f : 0.0f;
        const float bottom = flow.vertical && flow.rtl ? 40.0f : 0.0f;
        const float left = !flow.vertical && !flow.rtl ? 40.0f : 0.0f;
        spacing("definition", false, top, right, bottom, left);
        spacing("list", true, top, right, bottom, left);
        spacing("hint", true, 0, flow.vertical ? 0.0f : 40.0f, flow.vertical ? 40.0f : 0.0f, 0);
        for (const char* id : {"css-hint", "initial-hint"})
            spacing(id, true, flow.vertical ? 40.0f : 0.0f, 0, 0, flow.vertical ? 0.0f : 40.0f);
        spacing("inherit-hint", true, top, right, bottom, left);
        bool right_start = strcmp(flow.mode, "vertical-rl") == 0;
        spacing("pair", false, flow.vertical ? 0.0f : 11.0f,
            flow.vertical ? (right_start ? 11.0f : 17.0f) : 0.0f,
            flow.vertical ? 0.0f : 17.0f,
            flow.vertical ? (right_start ? 17.0f : 11.0f) : 0.0f);
        spacing("pair", true, flow.vertical ? (flow.rtl ? 19.0f : 13.0f) : 0.0f,
            flow.vertical ? 0.0f : (flow.rtl ? 13.0f : 19.0f),
            flow.vertical ? (flow.rtl ? 13.0f : 19.0f) : 0.0f,
            flow.vertical ? 0.0f : (flow.rtl ? 19.0f : 13.0f));
        spacing("reset", false, 0, 0, 0, 0);
        spacing("orthogonal", false, 20, 0, 20, 0);
        if (flow.vertical) {
            spacing("physical", false, 0, strcmp(flow.mode, "vertical-lr") == 0 ? 9.0f : 20.0f, 0,
                strcmp(flow.mode, "vertical-lr") == 0 ? 7.0f : 9.0f);
        } else spacing("physical", false, 20, 0, 9, 7);
        const BoundaryProp* body = radiant_document_body_element(document)->boundary();
        ASSERT_NE(body, nullptr);
        EXPECT_FLOAT_EQ(body->margin.top, 8);
        EXPECT_FLOAT_EQ(body->margin.right, 8);
        EXPECT_FLOAT_EQ(body->margin.bottom, 8);
        EXPECT_FLOAT_EQ(body->margin.left, 8);
        DomElement* body_element = radiant_document_body_element(document);
        ASSERT_TRUE(dom_cssom_set_inline_property(body_element, "writing-mode", "horizontal-tb"));
        ASSERT_TRUE(dom_cssom_set_inline_property(body_element, "direction", "ltr"));
        ASSERT_TRUE(dom_cssom_set_inline_property(body_element, "font-size", "24px"));
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        spacing("result", false, 30, 0, 30, 0);
        spacing("list", false, 24, 0, 24, 0);
        spacing("list", true, 0, 0, 0, 40);
        spacing("definition", false, 0, 0, 0, 40);
        spacing("hint", true, 0, 40, 0, 0);
        for (const char* id : {"css-hint", "initial-hint", "inherit-hint"})
            spacing(id, true, 0, 0, 0, 40);
        spacing("reset", false, 0, 0, 0, 0);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, DataUrlDocumentsDecodeTheirBodyAndSelectHtmlOrXhtmlByMime) {
    const struct { const char* url; bool xhtml; } cases[] = {
        {"data:text/html,%3Cdiv%20id='result'%20data-value='a%2Bb%26amp%3Bc'%3Etext%3C/div%3E#fragment", false},
        {"data:text/html;base64,PGRpdiBpZD0icmVzdWx0IiBkYXRhLXZhbHVlPSJhK2ImYW1wO2MiPnRleHQ8L2Rpdj4=", false},
        {"data:application/xhtml+xml,%3Chtml%20xmlns='http://www.w3.org/1999/xhtml'%3E%3Cbody%3E"
         "%3Cdiv%20id='result'%20data-value='a%2Bb%26amp%3Bc'%3Etext%3C/div%3E%3C/body%3E%3C/html%3E", true},
    };
    for (const auto& entry : cases) {
        DomElement* result = load(entry.url);
        ASSERT_NE(result, nullptr) << entry.url;
        EXPECT_STREQ(result->get_attribute("data-value"), "a+b&c");
        EXPECT_EQ(document->xml_document, entry.xhtml);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
    // an omitted MIME defaults to text/plain; its markup must not become HTML elements.
    EXPECT_EQ(load("data:,%3Cdiv%20id='result'%3Enot%20HTML%3C/div%3E"), nullptr);
}

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

TEST_F(RadiantTextQuery, RebuiltSourceRangesIgnoreAnonymousTableGroups) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        #table{display:table}#row{display:table-row}#cell{display:table-cell}
        </style><div id="result"><div id="table"><div id="row"><div id="cell">text</div></div></div></div>)HTML"), nullptr);
    layout_html_doc(&ui, document, false);
    DomElement* table = dom_find_element_by_id(document->root->as_element(), "table");
    DomElement* cell = dom_find_element_by_id(document->root->as_element(), "cell");
    ASSERT_NE(table, nullptr);
    ASSERT_NE(cell, nullptr);
    ASSERT_NE(cell->parent.get(), nullptr);
    ASSERT_NE(cell->parent->parent.get(), table);

    DomElement* next_table = dom_element_create(document, "div", nullptr);
    DomElement* next_row = dom_element_create(document, "div", nullptr);
    DomElement* next_cell = dom_element_create(document, "div", nullptr);
    ASSERT_NE(next_table, nullptr);
    ASSERT_NE(next_row, nullptr);
    ASSERT_NE(next_cell, nullptr);
    ASSERT_TRUE(next_table->set_attribute("id", "table"));
    ASSERT_TRUE(next_row->set_attribute("id", "row"));
    ASSERT_TRUE(next_cell->set_attribute("id", "cell"));
    ASSERT_TRUE(next_table->append_child(next_row));
    ASSERT_TRUE(next_row->append_child(next_cell));
    EXPECT_EQ(view_state_corresponding_node(cell, table, next_table, true), next_cell);
}

TEST_F(RadiantTextQuery, ScrollRequestsReuseValidatedFlowAndHonorViewportChanges) {
    DomElement* result = load_script_source(R"HTML(<style>
        body{margin:0}#scrollport{width:160px;height:64px;overflow:auto}
        #start{height:16px}#spacer{height:384px}#result{height:16px}
        </style><div id="scrollport"><div id="start"></div><div id="spacer"></div><div id="result">end</div></div>)HTML");
    ASSERT_NE(result, nullptr);
    layout_html_doc(&ui, document, false);
    // script-only documents can start without an interactive scroll-state owner.
    dom_scroll_into_view_if_needed_bridge(result);
    ASSERT_TRUE(layout_update_document(&ui, document));
    expect_box("result", 0, 48, 160, 16);

    radiant_document_ensure_state(document, "scroll-publication-test");
    ASSERT_NE(document->state, nullptr);
    layout_html_doc(&ui, document, true);
    uint64_t layouts = document->view_tree->document_layout_count;
    DomElement* start = dom_find_element_by_id(document->root->as_element(), "start");
    ASSERT_NE(start, nullptr);
    DomElement* targets[] = {start, result};
    for (DomElement* target : targets) {
        dom_scroll_into_view_if_needed_bridge(target);
        ASSERT_TRUE(layout_update_document(&ui, document));
        EXPECT_EQ(document->view_tree->document_layout_count, layouts);
        expect_box(target == start ? "start" : "result", 0, target == start ? 0 : 48, 160, 16);
    }

    // a queued scroll cannot bypass a changed viewport's normal flow validation.
    ui.viewport_width = 640.0f;
    dom_scroll_into_view_if_needed_bridge(result);
    ASSERT_TRUE(layout_update_document(&ui, document));
    EXPECT_EQ(document->view_tree->document_layout_count, layouts + 1);
    EXPECT_FLOAT_EQ(document->view_tree->root->width, 640.0f);
    expect_box("result", 0, 48, 160, 16);
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

TEST_F(RadiantTextQuery, HiddenPositionedPopupDoesNotPaintRetainedDecoration) {
    DomElement* result = load("test/html/layout_hidden_popup.html", true);
    ASSERT_NE(result, nullptr);
    DomElement* popup = dom_find_element_by_id(result, "popup");
    ASSERT_NE(popup, nullptr);
    ui.window_width = 320.0f;
    ui.window_height = 200.0f;
    ui.create_surface(ui.window_width, ui.window_height);
    auto publish_style = [&](const char* style) {
        ASSERT_TRUE(popup->set_attribute("style", style));
        dom_notify_mutation(DOM_JS_MUTATION_INLINE_STYLE, popup, popup->parent);
        ASSERT_TRUE(layout_update_document(&ui, document));
        render_html_doc(&ui, document->view_tree, nullptr);
        ASSERT_FALSE(ui.render_failed);
    };
    for (const char* style : {"z-index:3", "z-index:0", "z-index:-3"}) {
        SCOPED_TRACE(style);
        publish_style(style);
        ASSERT_NE(popup->view_type, RDT_VIEW_NONE);
        test_radiant_expect_surface_pixel(ui.surface, 30, 65, 255, 255, 255);
        char hidden_style[64];
        snprintf(hidden_style, sizeof(hidden_style), "%s;display:none", style);
        publish_style(hidden_style);
        EXPECT_EQ(popup->view_type, RDT_VIEW_NONE);
        // hidden boxes retain DOM/style storage, but no stacking paint may reach that storage.
        test_radiant_expect_surface_pixel(ui.surface, 30, 65, 34, 51, 68);
        test_radiant_expect_surface_pixel(ui.surface, 20, 65, 34, 51, 68);
        test_radiant_expect_surface_pixel(ui.surface, 15, 80, 34, 51, 68);
    }
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

TEST_F(RadiantTextQuery, CompletedFlexReplayRetainsContentInNextCleanPass) {
    ASSERT_NE(load("test/html/layout_flex_completed_text.html"), nullptr);
    process_document_font_faces(&ui, document);
    ui.viewport_width = 320;
    ui.viewport_height = 240;
    layout_html_doc(&ui, document, false);
    DomElement* item = dom_find_element_by_id(document->root->as_element(), "wrapped");
    ASSERT_NE(item, nullptr);
    FlexFlowTestContext flow(&ui, document, item);
    auto* block = lam::view_require_block(item);
    radiant::flex_measured_flow_invalidate(item);
    flex_retained_item_begin(&flow.layout, block, 320.0f, 240.0f);
    float height = 0.0f;
    ASSERT_TRUE(flex_measure_completed_flow(&flow.layout, block, &flow.parent, item->width, &height));
    layout_flex_item_content(&flow.layout, block);
    ASSERT_EQ(flow.layout.profiler.flex_flow_reuses, 1u);
    uint64_t geometry = retained_subtree_geometry(item);
    int64_t retained = g_layout_flex_retained_count;
    auto* ancestor = radiant::layout_flow_result_capture(&flow.layout, item->parent);
    ASSERT_NE(ancestor, nullptr);
    flex_retained_forget_content(item);
    ASSERT_TRUE(radiant::layout_flow_result_apply(&flow.layout, ancestor));
    radiant::layout_flow_result_release(document->view_tree->prop_pool, ancestor);

    // the owned query expires at the pass boundary; its live committed result remains reusable.
    document->view_tree->measurement_cache_generation++;
    document->incremental_layout = true;
    ASSERT_TRUE(flex_retained_item_begin(&flow.layout, block, 320.0f, 240.0f));
    layout_flex_item_content(&flow.layout, block);
    EXPECT_EQ(g_layout_flex_retained_count, retained + 1);
    EXPECT_EQ(retained_subtree_geometry(item), geometry);
    EXPECT_FLOAT_EQ(item->height, height);
    document->incremental_layout = false;
}

TEST_F(RadiantTextQuery, CachedIntrinsicWidthPreservesCommittedFlexContent) {
    ASSERT_NE(load("test/html/layout_flex_completed_text.html"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* item = dom_find_element_by_id(document->root->as_element(), "wrapped");
    ASSERT_NE(item, nullptr);
    FlexFlowTestContext flow(&ui, document, item);
    IntrinsicSizes expected = layout_measure_intrinsic_widths(&flow.layout, item, true);
    layout_flex_item_content(&flow.layout, lam::view_require_block(item));
    ASSERT_TRUE(item->flex_item()->retained.has_content);
    uint64_t geometry = retained_subtree_geometry(item);
    uint64_t hits = flow.layout.profiler.intrinsic_cache_hits;

    // a scalar cache hit must not open a destructive measurement scope.
    IntrinsicSizes actual = layout_measure_intrinsic_widths(&flow.layout, item, true);
    EXPECT_EQ(flow.layout.profiler.intrinsic_cache_hits, hits + 1);
    EXPECT_FLOAT_EQ(actual.min_content, expected.min_content);
    EXPECT_FLOAT_EQ(actual.max_content, expected.max_content);
    EXPECT_TRUE(item->flex_item()->retained.has_content);
    EXPECT_EQ(retained_subtree_geometry(item), geometry);
    EXPECT_EQ(flow.layout.run_mode, radiant::RunMode::PerformLayout);

    item->invalidate_intrinsic_inputs();
    uint64_t misses = flow.layout.profiler.intrinsic_cache_misses;
    layout_measure_intrinsic_widths(&flow.layout, item, true);
    EXPECT_GT(flow.layout.profiler.intrinsic_cache_misses, misses);
    EXPECT_FALSE(item->flex_item()->retained.has_content);
    EXPECT_EQ(flow.layout.run_mode, radiant::RunMode::PerformLayout);
}

TEST_F(RadiantTextQuery, UnchangedStyleProbePreservesCommittedFlexContent) {
    ASSERT_NE(load("test/html/layout_flex_completed_text.html"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* item = dom_find_element_by_id(document->root->as_element(), "wrapped");
    ASSERT_NE(item, nullptr);
    FlexFlowTestContext flow(&ui, document, item);
    dom_node_resolve_style(item, &flow.layout);
    layout_flex_item_content(&flow.layout, lam::view_require_block(item));
    ASSERT_TRUE(item->flex_item()->retained.has_content);
    uint64_t geometry = retained_subtree_geometry(item);
    {
        // a read at the committed constraint does not replace any fragment or used style.
        radiant::LayoutRunModeScope probe(&flow.layout, radiant::RunMode::ComputeSize);
        dom_node_resolve_style(item, &flow.layout);
    }
    EXPECT_TRUE(item->flex_item()->retained.has_content);
    EXPECT_EQ(retained_subtree_geometry(item), geometry);
    ASSERT_TRUE(item->set_attribute("style", "font-size:24px"));
    {
        radiant::LayoutRunModeScope probe(&flow.layout, radiant::RunMode::ComputeSize);
        dom_node_resolve_style(item, &flow.layout);
    }
    EXPECT_FALSE(item->flex_item()->retained.has_content);
    EXPECT_FLOAT_EQ(item->font->font_size, 24.0f);
}

#if defined(__APPLE__) && !defined(LAMBDA_NO_GUI)
TEST_F(RadiantTextQuery, NativeResizeRetainsCompletedFrameUntilRepaint) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>body{margin:0;background:#204060}</style><div id=result></div>");
    ASSERT_NE(layout_script_source(source), nullptr);
    ASSERT_TRUE(ui_context_enable_native_graphics(&ui));
    ui.headless = false;
    radiant_platform_set_window_title(ui.window, "Lambda resize regression");
    radiant_platform_set_window_size(ui.window, 320, 200);
    radiant_platform_show_window(ui.window);
    ASSERT_TRUE(radiant_platform_present_init(ui.window));
    int initial_width, initial_height;
    radiant_platform_get_framebuffer_size(ui.window, &initial_width, &initial_height);
    int logical_width, logical_height;
    radiant_platform_get_window_size(ui.window, &logical_width, &logical_height);
    ui.viewport_width = logical_width;
    ui.viewport_height = logical_height;
    ui_context_set_device_scale(&ui, (float)initial_width / logical_width,
                                (float)initial_height / logical_height);
    ui.window_width = initial_width;
    ui.window_height = initial_height;
    ui_context_create_surface(&ui, initial_width, initial_height);
    ASSERT_NE(ui.surface.get(), nullptr);
    const int sizes[][2] = {{400, 260}, {280, 180}, {360, 240}};
    const uint32_t background = 0xff604020;
    auto paint_pattern = [&](uint32_t marker) {
        for (int y = 0; y < ui.surface->height; y++) {
            auto* row = (uint32_t*)((uint8_t*)ui.surface->pixels + (size_t)y * ui.surface->pitch);
            for (int x = 0; x < ui.surface->width; x++)
                row[x] = x >= 40 && x < 120 && y >= 32 && y < 56 ? marker : background;
        }
    };
    auto expect_pattern = [&](uint32_t marker, int height) {
        uint8_t pixels[160 * 96 * 4] = {};
        glReadBuffer(GL_FRONT);
        glReadPixels(0, height - 96, 160, 96, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        EXPECT_EQ(glGetError(), (GLenum)GL_NO_ERROR);
        unsigned changed = 0;
        for (int y = 0; y < 96; y++) for (int x = 0; x < 160; x++) {
            uint32_t wanted = x >= 40 && x < 120 && y >= 32 && y < 56 ? marker : background;
            const uint8_t* pixel = pixels + ((95 - y) * 160 + x) * 4;
            if (pixel[0] != (wanted & 0xff) || pixel[1] != ((wanted >> 8) & 0xff) ||
                pixel[2] != ((wanted >> 16) & 0xff)) changed++;
        }
        EXPECT_EQ(changed, 0u) << "retained pixels must keep their size and top-left position";
    };
    auto expect_pixel = [&](int x, int y, int height, uint32_t wanted) {
        uint8_t pixel[4] = {};
        glReadBuffer(GL_FRONT);
        glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        EXPECT_EQ(glGetError(), (GLenum)GL_NO_ERROR);
        EXPECT_EQ(pixel[0], wanted & 0xff);
        EXPECT_EQ(pixel[1], (wanted >> 8) & 0xff);
        EXPECT_EQ(pixel[2], (wanted >> 16) & 0xff);
    };
    for (unsigned pass = 0; pass < 3; pass++) {
        SCOPED_TRACE(pass);
        uint32_t marker = 0xff60e040 + pass * 0x00060507;
        paint_pattern(marker);
        radiant_platform_present(ui.window, ui.surface.get(), nullptr, false, background, 1, 1);
        ui.present_partial = true;
        radiant_platform_set_window_size(ui.window, sizes[pass][0], sizes[pass][1]);
        radiant_platform_poll_events();
        int width, height;
        radiant_platform_get_framebuffer_size(ui.window, &width, &height);
        ASSERT_TRUE(ui_context_resize_window_surface(&ui, width, height));
        // the new CPU target has not been painted; the front buffer must retain the old image.
        expect_pattern(marker, height);
        expect_pixel(width - 10, height - 11, height, background);
        EXPECT_EQ(ui.surface->width, width);
        EXPECT_EQ(ui.surface->height, height);
        EXPECT_FALSE(ui.present_partial);
        EXPECT_FALSE(ui_context_resize_window_surface(&ui, width, height));
        radiant_platform_get_window_size(ui.window, &logical_width, &logical_height);
        ui.viewport_width = logical_width;
        ui.viewport_height = logical_height;
        paint_pattern(marker);
        radiant_platform_present(ui.window, ui.surface.get(), nullptr, false, background, 1, 1);
        expect_pattern(marker, height);
    }
    // a density change must still scale the old pixels while retaining CSS geometry.
    uint32_t marker = 0xff40d080;
    paint_pattern(marker);
    radiant_platform_present(ui.window, ui.surface.get(), nullptr, false, background, 1, 1);
    ui.present_partial = true;
    ui_context_set_device_scale(&ui, ui.device_scale_x * 2, ui.device_scale_y * 2);
    radiant_platform_set_window_size(ui.window, logical_width * 2, logical_height * 2);
    radiant_platform_poll_events();
    int width, height;
    radiant_platform_get_framebuffer_size(ui.window, &width, &height);
    ASSERT_TRUE(ui_context_resize_window_surface(&ui, width, height));
    expect_pixel(160, 88, height, marker);
    expect_pixel(80, 44, height, background);
}
#endif

TEST_F(RadiantTextQuery, ReplacedIntrinsicWidthsExcludeGraphicDescendants) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>body{margin:0}#host{display:inline-flex}"
        "#result{display:block;padding:3px;border:2px solid;max-width:100px}</style>"
        "<div id=host><svg id=result width=80 height=30 viewBox='0 0 80 30'>"
        "<foreignObject width=4000 height=200><div style='width:4000px'>wide</div></foreignObject>"
        "<text id=graphic-label x=0 y=20>unbreakable_graphic_label_wider_than_the_viewport</text>"
        "</svg></div>");
    DomElement* result = layout_script_source(source);
    ASSERT_NE(result, nullptr);
    DomElement* label = dom_find_element_by_id(document->root->as_element(), "graphic-label");
    ASSERT_NE(label, nullptr);
    for (unsigned pass = 0; pass < 3; pass++) {
        SCOPED_TRACE(pass);
        if (pass == 1) {
            ASSERT_TRUE(result->set_attribute("width", "60"));
            ASSERT_TRUE(dom_text_set_content(label->first_child->as_text(),
                "an_even_longer_graphic_label_that_does_not_size_the_svg_viewport"));
        }
        ui.viewport_width = ui.window_width = pass == 2 ? 480.0f : 320.0f;
        layout_html_doc(&ui, document, true);
        FlexFlowTestContext flow(&ui, document, result);
        uint64_t requests = flow.layout.profiler.intrinsic_requests;
        IntrinsicSizes sizes = layout_measure_intrinsic_widths(&flow.layout, result, true);
        EXPECT_EQ(flow.layout.profiler.intrinsic_requests, requests + 1);
        float wanted = (pass == 0 ? 80.0f : 60.0f) + 10.0f;
        EXPECT_FLOAT_EQ(sizes.min_content, wanted);
        EXPECT_FLOAT_EQ(sizes.max_content, wanted);
        EXPECT_FLOAT_EQ(result->width, wanted);
        EXPECT_FLOAT_EQ(dom_find_element_by_id(document->root->as_element(), "host")->width, wanted);
    }
}

TEST_F(RadiantTextQuery, IntrinsicWidthRetainsCleanInputsAcrossIncrementalPasses) {
    ASSERT_NE(load("test/html/layout_flex_completed_text.html"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    DomElement* item = dom_find_element_by_id(document->root->as_element(), "wrapped");
    ASSERT_NE(item, nullptr);
    FlexFlowTestContext flow(&ui, document, item);
    IntrinsicSizes expected = layout_measure_intrinsic_widths(&flow.layout, item, true);
    flow.layout.width += 40.0f;
    layout_measure_intrinsic_widths(&flow.layout, item, true);
    flow.layout.width -= 40.0f;
    document->incremental_layout = true;
    advance_measurement_cache_generation(document->view_tree);
    radiant::layout_pass_ensure_cache(&flow.layout, item);
    uint64_t hits = flow.layout.profiler.intrinsic_cache_hits;
    IntrinsicSizes actual = layout_measure_intrinsic_widths(&flow.layout, item, true);
    EXPECT_EQ(flow.layout.profiler.intrinsic_cache_hits, hits + 1);
    EXPECT_FLOAT_EQ(actual.min_content, expected.min_content);
    EXPECT_FLOAT_EQ(actual.max_content, expected.max_content);

    auto expect_miss = [&]() {
        uint64_t misses = flow.layout.profiler.intrinsic_cache_misses;
        layout_measure_intrinsic_widths(&flow.layout, item, true);
        EXPECT_GT(flow.layout.profiler.intrinsic_cache_misses, misses);
    };
    item->invalidate_intrinsic_inputs();
    expect_miss();
    flow.layout.width += 40.0f;
    expect_miss();
    flow.layout.root_font_size += 2.0f;
    expect_miss();
    setup_font(&ui, &flow.layout.font, item->font);
    expect_miss();
    document->style_query_epoch++;
    expect_miss();
    document->view_tree->layout_generation++;
    expect_miss();
    document->incremental_layout = false;
    advance_measurement_cache_generation(document->view_tree);
    expect_miss();
}

TEST_F(RadiantTextQuery, IntrinsicWidthKeepsResourceAndOrderedEffectsPassLocal) {
    ASSERT_NE(load("test/html/layout_flex_retained_effects.html"), nullptr);
    process_document_font_faces(&ui, document);
    layout_html_doc(&ui, document, false);
    for (const char* id : {"resource", "generated", "list"}) {
        SCOPED_TRACE(id);
        DomElement* item = dom_find_element_by_id(document->root->as_element(), id);
        ASSERT_NE(item, nullptr);
        FlexFlowTestContext flow(&ui, document, item);
        IntrinsicSizes expected = layout_measure_intrinsic_widths(&flow.layout, item, true);
        document->incremental_layout = true;
        advance_measurement_cache_generation(document->view_tree);
        uint64_t misses = flow.layout.profiler.intrinsic_cache_misses;
        IntrinsicSizes actual = layout_measure_intrinsic_widths(&flow.layout, item, true);
        EXPECT_GT(flow.layout.profiler.intrinsic_cache_misses, misses);
        EXPECT_FLOAT_EQ(actual.min_content, expected.min_content);
        EXPECT_FLOAT_EQ(actual.max_content, expected.max_content);
        document->incremental_layout = false;
    }
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

TEST_F(RadiantTextQuery, FlexShellReservesDescendantHeightConstraintsForItsFooter) {
    ASSERT_NE(load("test/html/flex_shell_descendant_constraints.html"), nullptr);
    auto find = [&](const char* id) { return dom_find_element_by_id(document->root->as_element(), id); };
    auto child = [&](const char* shell, const char* suffix) {
        char id[64];
        snprintf(id, sizeof(id), "%s-%s", shell, suffix);
        return find(id);
    };
    struct Case { const char* id; float header; };
    const Case cases[] = {{"min",100.0f},{"explicit",120.0f},{"content",108.0f}};
    for (unsigned pass = 0; pass < 2; pass++) {
        layout_html_doc(&ui, document, pass != 0);
        for (const Case& entry : cases) {
            EXPECT_FLOAT_EQ(child(entry.id,"header")->height, entry.header) << entry.id << " pass " << pass;
            EXPECT_FLOAT_EQ(child(entry.id,"main")->height, 180.0f-entry.header) << entry.id << " pass " << pass;
            EXPECT_FLOAT_EQ(child(entry.id,"footer")->y, 180.0f) << entry.id << " pass " << pass;
        }
    }
    ASSERT_TRUE(find("min-tools")->set_attribute("style", "min-height:110px"));
    layout_html_doc(&ui, document, true);
    EXPECT_FLOAT_EQ(find("min-header")->height, 130.0f);
    EXPECT_FLOAT_EQ(find("min-main")->height, 50.0f);
    EXPECT_FLOAT_EQ(find("min-footer")->y, 180.0f);
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

TEST_F(RadiantTextQuery, TransformedTextControlsKeepHitAndPaintOnTheSelectedLine) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        body{margin:0;background:white}.scaled{position:absolute;left:40px;top:30px;
        transform-origin:0 0;transform:scale(.6) rotate(8deg)}
        input,textarea{display:block;box-sizing:border-box;width:240px;height:100px;
        border:0;padding:0;font:20px Arial;caret-color:#ff00ff;background:white;color:black}
        input{height:40px}
        </style><div id="result"></div><div class="scaled"><input id="input" value="first line">
        <textarea id="area">first line&#10;second line</textarea>
        <div id="rich" contenteditable="true" style="font:20px/28px Arial;width:240px;caret-color:#ff00ff">
        <div id="first-row">first line</div><div id="second-row">second line</div></div></div>)HTML"), nullptr);
    DocState* state = radiant_document_ensure_state(document, "transformed-control-caret-test");
    ASSERT_NE(state, nullptr);
    for (float scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(scale);
        ui_context_set_device_scale(&ui, scale, scale);
        ui.window_width = 320 * scale; ui.window_height = 200 * scale;
        ui.create_surface(ui.window_width, ui.window_height);
        layout_html_doc(&ui, document, false);
        for (const char* id : {"input", "area"}) {
            SCOPED_TRACE(id);
            DomElement* control = dom_find_element_by_id(document->root->as_element(), id);
            ASSERT_NE(control, nullptr);
            focus_set_programmatic(state, control);
            tc_ensure_init(control);
            RdtMatrix matrix;
            ASSERT_TRUE(view_get_client_matrix(control, &matrix));
            for (uint32_t offset : {2u, 13u}) {
                if (offset > 10 && strcmp(id, "input") == 0) continue;
                tc_set_selection_range(control, offset, offset, 0);
                EditingCaretRect caret;
                ASSERT_TRUE(editing_geometry_text_control_caret_rect(&ui, control, offset, &caret));
                float local_x = caret.x - control->x;
                float local_y = caret.y - control->y;
                float hit_x = 0.0f, hit_y = 0.0f;
                ASSERT_TRUE(rdt_matrix_project_point(&matrix, local_x, local_y + caret.height * .5f,
                    &hit_x, &hit_y));
                uint32_t hit = 0;
                ASSERT_TRUE(editing_geometry_text_control_offset_for_point(&ui, control, hit_x, hit_y, &hit));
                EXPECT_EQ(hit, offset);
                render_html_doc(&ui, document->view_tree, nullptr);
                ASSERT_FALSE(ui.render_failed);
                // sample inside the projected caret, separately from its logical hit boundary.
                float pixel_x = 0.0f, pixel_y = 0.0f;
                ASSERT_TRUE(rdt_matrix_project_point(&matrix, local_x + 1.0f, local_y + caret.height * .5f,
                    &pixel_x, &pixel_y));
                test_radiant_expect_surface_pixel(ui.surface,
                    (unsigned)floorf(pixel_x * scale), (unsigned)floorf(pixel_y * scale),
                    255, 0, 255, 80);
            }
        }
        DomElement* rich = dom_find_element_by_id(document->root->as_element(), "rich");
        EditingSurface surface;
        ASSERT_TRUE(editing_surface_from_target(rich, &surface));
        for (const char* id : {"first-row", "second-row"}) {
            SCOPED_TRACE(id);
            DomElement* row = dom_find_element_by_id(document->root->as_element(), id);
            ASSERT_NE(row, nullptr);
            DomText* text = lam::dom_require_text(row->first_child);
            ASSERT_NE(text->rect, nullptr);
            RdtMatrix matrix;
            ASSERT_TRUE(view_get_client_matrix(text, &matrix));
            float x = dom_range_glyph_x_for_byte_offset(&ui, lam::view_require_text(text), text->rect, 2) - text->x;
            float y = text->rect->y - text->y + text->rect->height * .5f;
            float hit_x = 0.0f, hit_y = 0.0f;
            ASSERT_TRUE(rdt_matrix_project_point(&matrix, x, y, &hit_x, &hit_y));
            EditingBoundary hit;
            ASSERT_TRUE(editing_geometry_hit_test_boundary(&ui, document->view_tree->root,
                &surface, hit_x, hit_y, EDITING_CLAMP_SKIP_TEXT_CONTROLS, &hit));
            EXPECT_EQ(hit.dom.node, static_cast<DomNode*>(text));
            EXPECT_EQ(hit.dom.offset, 2u);
            focus_set_programmatic(state, rich);
            state_store_caret_collapse_to_view_offset(state, static_cast<View*>(text), 2);
            update_caret_visual_position(&ui, state);
            render_html_doc(&ui, document->view_tree, nullptr);
            ASSERT_FALSE(ui.render_failed);
            float caret_x = 0.0f, caret_y = 0.0f;
            ASSERT_TRUE(rdt_matrix_project_point(&matrix, x + 1.5f, y, &caret_x, &caret_y));
            test_radiant_expect_surface_pixel(ui.surface,
                (unsigned)floorf(caret_x * scale), (unsigned)floorf(caret_y * scale),
                255, 0, 255, 80);
            // trailing and leading blank space belong to this row, including under a CSS transform.
            for (float local_x : {text->rect->x - text->x - 20.0f,
                    text->rect->x - text->x + text->rect->width + 40.0f}) {
                ASSERT_TRUE(rdt_matrix_project_point(&matrix, local_x, y, &hit_x, &hit_y));
                ASSERT_TRUE(editing_geometry_hit_test_boundary(&ui, document->view_tree->root,
                    &surface, hit_x, hit_y, EDITING_CLAMP_SKIP_TEXT_CONTROLS, &hit));
                EXPECT_EQ(hit.dom.node, static_cast<DomNode*>(text));
                EXPECT_EQ(hit.dom.offset, local_x < x ? 0u : dom_text_utf16_length(text));
            }
        }
    }
}

TEST_F(RadiantTextQuery, TextareaRevealsEditingCaretAndClipsScrolledContents) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        body{margin:0;background:white}textarea{position:absolute;left:20px;top:20px;
        box-sizing:border-box;width:180px;height:60px;border:2px solid #999;padding:4px;
        font:10px Arial;caret-color:#ff00ff;background:white;color:#ff0000}
        </style><div id="result"></div><textarea id="area">one&#10;two&#10;three&#10;four&#10;five&#10;six&#10;seven&#10;eight</textarea>)HTML"), nullptr);
    DocState* state = radiant_document_ensure_state(document, "textarea-caret-test");
    ASSERT_NE(state, nullptr);
    for (float scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(scale);
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 320; ui.viewport_height = 200;
        ui.window_width = 320 * scale; ui.window_height = 200 * scale;
        ui.create_surface(ui.window_width, ui.window_height);
        layout_html_doc(&ui, document, false);
        DomElement* area = dom_find_element_by_id(document->root->as_element(), "area");
        ASSERT_NE(area, nullptr); ASSERT_NE(area->form, nullptr);
        focus_set_programmatic(state, area);
        tc_ensure_init(area);
        uint32_t end = area->form->current_value_u16_len;
        auto expect_caret_pixels = [&](bool visible) {
            render_html_doc(&ui, document->view_tree, nullptr);
            ASSERT_FALSE(ui.render_failed);
            unsigned inside = 0, outside = 0, text_outside = 0;
            for (int y = 0; y < ui.surface->height; y++) {
                for (int x = 0; x < ui.surface->width; x++) {
                    const uint8_t* pixel = (const uint8_t*)ui.surface->pixels + y * ui.surface->pitch + x * 4;
                    bool in_content = x >= 26 * scale && x < 194 * scale && y >= 26 * scale && y < 74 * scale;
                    if (pixel[0] > 180 && pixel[1] < 40 && pixel[2] > 180) {
                        if (in_content) inside++;
                        else outside++;
                    }
                    if (pixel[0] > 180 && pixel[1] < 40 && pixel[2] < 40 && !in_content) text_outside++;
                }
            }
            EXPECT_EQ(outside, 0u);
            EXPECT_EQ(text_outside, 0u);
            if (visible) EXPECT_GT(inside, 0u);
            else EXPECT_EQ(inside, 0u);
        };
        tc_set_selection_range(area, end, end, 0);
        expect_caret_pixels(true);
        EXPECT_GT(area->form->scroll_y, 0.0f);
        tc_set_selection_range(area, 0, 0, 0);
        expect_caret_pixels(true);
        EXPECT_FLOAT_EQ(area->form->scroll_y, 0.0f);
        // dragging beyond the viewport clips its caret until the drag autoscroll tick.
        state_store_selection_start_pointer(state, area, end);
        ASSERT_TRUE(state->editing.pointer_selecting);
        ASSERT_TRUE(caret_is_visible(state));
        expect_caret_pixels(false);
        EXPECT_FLOAT_EQ(area->form->scroll_y, 0.0f);
        selection_finish_active_gesture(state);
        // inserting new lines must reveal the new blank line as well as populated lines.
        StrBuf* appended = strbuf_new();
        strbuf_append_str(appended, area->form->current_value);
        strbuf_append_str(appended, "\nnine\n");
        tc_set_value(area, appended->str, appended->length);
        strbuf_free(appended);
        expect_caret_pixels(true);
        EXPECT_GT(area->form->scroll_y, 0.0f);
    }
}

TEST_F(RadiantTextQuery, CollapsedTableBordersRetainDashDotDoubleAndColor) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        body{margin:0;background:white}table{border-collapse:collapse;margin:12px 20px}
        td{width:120px;height:30px;padding:0;border:6px solid #2864ad}
        #dashed{border-style:dashed}#dotted{border-style:dotted}#double{border-style:double}
        </style><div id="result"></div><table><tr><td id="solid"></td></tr></table>
        <table><tr><td id="dashed"></td></tr></table><table><tr><td id="dotted"></td></tr></table>
        <table><tr><td id="double"></td></tr></table>)HTML"), nullptr);
    for (float scale : {1.0f,2.0f}) {
        ui_context_set_device_scale(&ui,scale,scale);
        ui.viewport_width=320; ui.viewport_height=300;
        ui.window_width=320*scale; ui.window_height=300*scale;
        ui.create_surface(ui.window_width,ui.window_height);
        layout_html_doc(&ui,document,false);
        render_html_doc(&ui,document->view_tree,nullptr);
        ASSERT_FALSE(ui.render_failed);
        for (const char* id : {"solid","dashed","dotted","double"}) {
            SCOPED_TRACE(id);
            SCOPED_TRACE(scale);
            DomElement* cell=dom_find_element_by_id(document->root->as_element(),id);
            ASSERT_NE(cell,nullptr);
            float left=0.0f,top=0.0f;
            view_get_visual_bounds(cell,&left,&top,nullptr,nullptr);
            unsigned ink=0,gaps=0;
            for (unsigned offset=10;offset<110;offset++) {
                unsigned x=(unsigned)roundf((left+offset)*scale);
                unsigned y=(unsigned)roundf(top*scale);
                ASSERT_LT(x,(unsigned)ui.surface->width); ASSERT_LT(y,(unsigned)ui.surface->height);
                const uint8_t* pixel=(const uint8_t*)ui.surface->pixels+y*ui.surface->pitch+x*4;
                if (pixel[0]<80 && pixel[1]<140 && pixel[2]>150) ink++;
                if (pixel[0]>245 && pixel[1]>245 && pixel[2]>245) gaps++;
            }
            if (strcmp(id,"solid")==0) { EXPECT_GT(ink,90u); EXPECT_EQ(gaps,0u); }
            else if (strcmp(id,"double")==0) {
                EXPECT_GT(gaps,90u);
                test_radiant_expect_surface_pixel(ui.surface,(unsigned)roundf((left+60)*scale),
                    (unsigned)roundf((top-2)*scale),40,100,173);
                test_radiant_expect_surface_pixel(ui.surface,(unsigned)roundf((left+60)*scale),
                    (unsigned)roundf((top+2)*scale),40,100,173);
            } else { EXPECT_GT(ink,20u); EXPECT_GT(gaps,10u); }
        }
    }
}

TEST_F(RadiantTextQuery, MaxHeightAutoContainersClipTheirOverflowAfterSizing) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        body{margin:0;background:white}.box{width:120px;max-height:100px;overflow:auto;margin-bottom:20px}
        .grid{display:grid;grid-template-columns:repeat(2,1fr);gap:5px}
        .item{height:67px;background:#2864ad}#explicit{height:100px}#fits .item{height:25px}
        </style><div id="result"></div>
        <div id="grid" class="box grid"><div class="item"></div><div class="item"></div><div class="item"></div><div class="item"></div></div>
        <div id="flow" class="box"><div class="item"></div><div class="item"></div></div>
        <div id="explicit" class="box grid"><div class="item"></div><div class="item"></div><div class="item"></div><div class="item"></div></div>
        <div id="fits" class="box grid"><div class="item"></div><div class="item"></div></div>)HTML"), nullptr);
    ui.viewport_width=ui.window_width=320;
    ui.viewport_height=ui.window_height=500;
    ui.create_surface(ui.window_width,ui.window_height);
    layout_html_doc(&ui,document,false);
    render_html_doc(&ui,document->view_tree,nullptr);
    ASSERT_FALSE(ui.render_failed);
    for (const char* id : {"grid","flow","explicit"}) {
        SCOPED_TRACE(id);
        DomElement* box=dom_find_element_by_id(document->root->as_element(),id);
        ASSERT_NE(box,nullptr);
        EXPECT_FLOAT_EQ(box->height,100.0f);
        ASSERT_NE(box->scroller,nullptr);
        EXPECT_TRUE(box->scroll()->has_vt_scroll);
        EXPECT_TRUE(box->scroll()->has_clip);
        EXPECT_FLOAT_EQ(box->scroll()->clip.bottom,box->height);
        ASSERT_NE(box->scroll()->pane,nullptr);
        EXPECT_GT(box->scroll()->pane->v_max_scroll,0.0f);
        float left=0.0f,top=0.0f;
        view_get_visual_bounds(box,&left,&top,nullptr,nullptr);
        test_radiant_expect_surface_pixel(ui.surface,(unsigned)roundf(left+10),
            (unsigned)roundf(top+110),255,255,255);
    }
    DomElement* fits=dom_find_element_by_id(document->root->as_element(),"fits");
    ASSERT_NE(fits,nullptr);
    EXPECT_FLOAT_EQ(fits->height,25.0f);
    EXPECT_FALSE(fits->scroll()->has_vt_scroll);
}

TEST_F(RadiantTextQuery, ZoomedListBulletsShareTextTransformsAndScrollClipping) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        body{margin:0;background:white;font:16px/24px Arial}
        #port{position:absolute;left:20px;top:60px;width:200px;height:120px;overflow:auto}
        #content{transform:scale(1.1);transform-origin:top left}.space{height:500px}
        ul{margin:0;padding-left:30px}li{height:24px}li::marker{color:#ff00ff}
        </style><div id="result"></div><div id="port"><div id="content"><div class="space"></div>
        <ul><li>One</li><li>Two</li><li id="three">Three</li><li id="four">Four</li></ul>
        <div class="space"></div></div></div>
        <script>document.getElementById('port').scrollTop=600;</script>)HTML"), nullptr);
    ui.viewport_width=ui.window_width=320;
    ui.viewport_height=ui.window_height=200;
    ui.create_surface(ui.window_width,ui.window_height);
    layout_html_doc(&ui,document,false);
    render_html_doc(&ui,document->view_tree,nullptr);
    ASSERT_FALSE(ui.render_failed);
    for (const char* id : {"three","four"}) {
        SCOPED_TRACE(id);
        DomElement* item=dom_find_element_by_id(document->root->as_element(),id);
        ASSERT_NE(item,nullptr);
        float left=0.0f,top=0.0f,width=0.0f,height=0.0f;
        view_get_visual_bounds(item,&left,&top,&width,&height);
        ASSERT_GE(top,60.0f); ASSERT_LE(top+height,180.0f);
        unsigned ink=0;
        for (unsigned y=(unsigned)ceilf(top);y<(unsigned)floorf(top+height);y++) {
            for (unsigned x=(unsigned)ceilf(left-25);x<(unsigned)floorf(left-2);x++) {
                const uint8_t* pixel=(const uint8_t*)ui.surface->pixels+y*ui.surface->pitch+x*4;
                if (pixel[0]>180 && pixel[1]<40 && pixel[2]>180) ink++;
            }
        }
        EXPECT_GT(ink,0u);
    }
    for (unsigned y=20;y<60;y++) for (unsigned x=20;x<100;x++) {
        const uint8_t* pixel=(const uint8_t*)ui.surface->pixels+y*ui.surface->pitch+x*4;
        EXPECT_FALSE(pixel[0]>180 && pixel[1]<40 && pixel[2]>180) << x << "," << y;
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

TEST_F(RadiantTextQuery, FloatsUseLineAndBlockAxesAcrossWritingModes) {
    struct State { const char* inline_size; float available; bool negative; bool fixed; };
    const State states[] = {{"150px", 150, false, false}, {"110px", 110, false, false},
        {"110px", 110, true, false}, {"150px", 150, false, true}, {"150px", 150, false, false}};
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl", "sideways-lr", "sideways-rl"})
    for (const char* direction : {"ltr", "rtl"})
    for (const char* pattern : {"left", "right", "mixed"}) for (bool prefix : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        SCOPED_TRACE(pattern);
        SCOPED_TRACE(prefix);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flow-root;writing-mode:%s;direction:%s;"
            "inline-size:150px;padding:3px 5px 7px 11px;border:2px solid}"
            "#prefix{display:%s;inline-size:20px;block-size:17px}.float{inline-size:40px;block-size:30px;"
            "padding:3px;border:2px solid;margin-inline:5px 7px;margin-block:3px 9px;float:left}</style>"
            "<div id=result><div id=prefix></div>", mode, direction, prefix ? "block" : "none");
        const char* ids[] = {"a", "b", "c", "d"};
        for (size_t index = 0; index < 4; index++) {
            const char* side = strcmp(pattern, "mixed") == 0 ? (index % 2 ? "right" : "left") : pattern;
            strbuf_append_format(source, "<div id=%s class=float style='float:%s'></div>", ids[index], side);
        }
        strbuf_append_str(source, "</div>");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* children[4];
        for (size_t index = 0; index < 4; index++) {
            children[index] = dom_find_element_by_id(result, ids[index]);
            ASSERT_NE(children[index], nullptr);
        }
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        bool reverse_block = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "sideways-rl") == 0;
        bool reverse_line = strcmp(mode, "sideways-lr") == 0;
        bool rtl = strcmp(direction, "rtl") == 0;
        for (const State& state : states) {
            SCOPED_TRACE(state.available);
            SCOPED_TRACE(state.negative);
            SCOPED_TRACE(state.fixed);
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "inline-size", state.inline_size));
            ASSERT_TRUE(dom_cssom_set_inline_property(result, "block-size", state.fixed ? "260px" : "auto"));
            for (DomElement* child : children) {
                ASSERT_TRUE(dom_cssom_set_inline_property(child, "margin-inline", state.negative ? "-4px 7px" : "5px 7px"));
                ASSERT_TRUE(dom_cssom_set_inline_property(child, "margin-block", state.negative ? "-2px 9px" : "3px 9px"));
            }
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float start_margin = state.negative ? -4 : 5;
            float block_margin = state.negative ? -2 : 3;
            float line_left = rtl ? 7 : start_margin;
            float line_right = rtl ? start_margin : 7;
            float outer_inline = 50 + line_left + line_right;
            float outer_block = 40 + block_margin + 9;
            float per_line = floorf(state.available / outer_inline);
            float flow_extent = (prefix ? 17 : 0) + ceilf(4 / per_line) * outer_block;
            float extent = state.fixed ? 260 : flow_extent;
            // identical margin boxes have a closed-form packing result, independently checked in Chromium.
            EXPECT_FLOAT_EQ(result->width, vertical ? extent + 20 : state.available + 20);
            EXPECT_FLOAT_EQ(result->height, vertical ? state.available + 14 : extent + 14);
            for (size_t index = 0; index < 4; index++) {
                SCOPED_TRACE(ids[index]);
                float line = floorf((float)index / per_line);
                float column = (float)index - line * per_line;
                bool left = strcmp(pattern, "mixed") == 0 ? index % 2 == 0 : strcmp(pattern, "left") == 0;
                float inline_pos = left ? line_left + column * outer_inline
                    : state.available - 50 - line_right - (strcmp(pattern, "mixed") == 0 ? 0 : column * outer_inline);
                float block_pos = (prefix ? 17 : 0) + line * outer_block + block_margin;
                EXPECT_FLOAT_EQ(children[index]->x, vertical ? (reverse_block ? 13 + extent - block_pos - 40 : 13 + block_pos) : 13 + inline_pos);
                EXPECT_FLOAT_EQ(children[index]->y, vertical ? (reverse_line ? 5 + state.available - inline_pos - 50 : 5 + inline_pos) : 5 + block_pos);
                EXPECT_FLOAT_EQ(children[index]->width, vertical ? 40 : 50);
                EXPECT_FLOAT_EQ(children[index]->height, vertical ? 50 : 40);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, ClearanceSurvivesVerticalPublicationAndRetainedReflow) {
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl", "sideways-lr", "sideways-rl"})
    for (bool floated : {false, true}) for (bool margins : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(floated);
        SCOPED_TRACE(margins);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flow-root;writing-mode:%s;"
            "inline-size:150px;padding:3px 5px 7px 11px;border:2px solid}"
            "#a{float:left;inline-size:40px;block-size:30px;%s}"
            "#b{float:%s;inline-size:40px;block-size:20px;%s}</style>"
            "<div id=result><div id=a></div><div id=b></div></div>", mode,
            margins ? "margin-block:3px 9px" : "", floated ? "left" : "none",
            margins ? "margin-block:5px 7px" : "");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* children[] = {dom_find_element_by_id(result, "a"), dom_find_element_by_id(result, "b")};
        ASSERT_NE(children[0], nullptr);
        ASSERT_NE(children[1], nullptr);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        bool reverse_block = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "sideways-rl") == 0;
        bool reverse_line = strcmp(mode, "sideways-lr") == 0;
        for (const char* clear : {"none", "both", "none"}) {
            SCOPED_TRACE(clear);
            ASSERT_TRUE(dom_cssom_set_inline_property(children[1], "clear", clear));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool cleared = strcmp(clear, "both") == 0;
            float float_end = margins ? 42 : 30;
            float block_pos = cleared ? float_end + (floated && margins ? 5 : 0) : margins ? 5 : 0;
            float extent = fmaxf(float_end, block_pos + 20 + (margins ? 7 : 0));
            EXPECT_FLOAT_EQ(result->width, vertical ? extent + 20 : 170);
            EXPECT_FLOAT_EQ(result->height, vertical ? 164 : extent + 14);
            for (size_t index = 0; index < 2; index++) {
                SCOPED_TRACE(index);
                float inline_pos = index && floated && !cleared ? 40 : 0;
                float start = index ? block_pos : margins ? 3 : 0;
                float block_size = index ? 20 : 30;
                // CSS clear moves normal border edges and float margin edges past the preceding float.
                EXPECT_FLOAT_EQ(children[index]->x, vertical
                    ? (reverse_block ? 13 + extent - start - block_size : 13 + start) : 13 + inline_pos);
                EXPECT_FLOAT_EQ(children[index]->y, vertical
                    ? (reverse_line ? 115 - inline_pos : 5 + inline_pos) : 5 + start);
                EXPECT_FLOAT_EQ(children[index]->width, vertical ? block_size : 40);
                EXPECT_FLOAT_EQ(children[index]->height, vertical ? 40 : block_size);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, FlowRootsAvoidFloatMarginBoxesOnTheirContainingAxes) {
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl", "sideways-lr", "sideways-rl"})
    for (const char* direction : {"ltr", "rtl"}) for (const char* side : {"left", "right"})
    for (const char* kind : {"parallel", "orthogonal", "replaced"}) for (bool decorated : {false, true}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        SCOPED_TRACE(side);
        SCOPED_TRACE(kind);
        SCOPED_TRACE(decorated);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        bool reverse_block = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "sideways-rl") == 0;
        bool reverse_line = strcmp(mode, "sideways-lr") == 0;
        bool orthogonal = strcmp(kind, "orthogonal") == 0;
        bool replaced = strcmp(kind, "replaced") == 0;
        bool rtl = strcmp(direction, "rtl") == 0;
        bool left = strcmp(side, "left") == 0;
        const char* child_mode = orthogonal ? (vertical ? "horizontal-tb" : "vertical-lr") : mode;
        const char* margin = !decorated ? "0" : !vertical ? "7px 13px 5px 11px"
            : reverse_block ? "11px 7px 13px 5px" : "11px 5px 13px 7px";
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flow-root;writing-mode:%s;direction:%s;"
            "inline-size:200px;padding:3px 5px 7px 11px;border:2px solid}"
            "#a{float:%s;inline-size:60px;block-size:50px;%s}"
            "#b{display:%s;writing-mode:%s;width:%gpx;height:%gpx;padding:%gpx;border:%s;margin:%s}"
            "</style><div id=result><div id=a></div><%s id=b></%s></div>", mode, direction, side,
            decorated ? "margin-inline:5px 7px;margin-block:3px 9px" : "", replaced ? "block" : "flow-root", child_mode,
            vertical ? 30.0 : 80.0, vertical ? 80.0 : 30.0, decorated ? 2.0 : 0.0,
            decorated ? "1px solid" : "0", margin, replaced ? "canvas" : "div", replaced ? "canvas" : "div");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* floating = dom_find_element_by_id(result, "a");
        DomElement* child = dom_find_element_by_id(result, "b");
        ASSERT_NE(floating, nullptr);
        ASSERT_NE(child, nullptr);
        for (const char* size : {"80px", "160px", "80px"}) {
            SCOPED_TRACE(size);
            ASSERT_TRUE(dom_cssom_set_inline_property(child, vertical ? "height" : "width", size));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            float inline_size = strtof(size, nullptr) + (decorated ? 6 : 0);
            float block_size = decorated ? 36 : 30;
            float float_outer_inline = decorated ? 72 : 60;
            float float_end = decorated ? 62 : 50;
            float margin_left = decorated ? (reverse_line ? 13 : 11) : 0;
            float margin_right = decorated ? (reverse_line ? 11 : 13) : 0;
            float excluded_left = left ? float_outer_inline : 0;
            float excluded_right = left ? 0 : float_outer_inline;
            // only the BFC border box must avoid floats; its margins may occupy the excluded space.
            bool fits = inline_size + fmaxf(margin_left - excluded_left, 0.0f) +
                fmaxf(margin_right - excluded_right, 0.0f) <= 200 - excluded_left - excluded_right;
            float block_pos = fits ? (decorated ? 7 : 0) : float_end;
            if (!fits) excluded_left = excluded_right = 0;
            float inline_pos = rtl ? fminf(200 - margin_right, 200 - excluded_right) - inline_size
                : fmaxf(margin_left, excluded_left);
            float extent = fmaxf(float_end, block_pos + block_size + (decorated ? 5 : 0));
            EXPECT_FLOAT_EQ(result->width, vertical ? extent + 20 : 220);
            EXPECT_FLOAT_EQ(result->height, vertical ? 214 : extent + 14);
            EXPECT_FLOAT_EQ(child->x, vertical
                ? (reverse_block ? 13 + extent - block_pos - block_size : 13 + block_pos) : 13 + inline_pos);
            EXPECT_FLOAT_EQ(child->y, vertical
                ? (reverse_line ? 205 - inline_pos - inline_size : 5 + inline_pos) : 5 + block_pos);
            EXPECT_FLOAT_EQ(child->width, vertical ? block_size : inline_size);
            EXPECT_FLOAT_EQ(child->height, vertical ? inline_size : block_size);
            float float_inline = left ? (decorated ? (rtl ? 7 : 5) : 0)
                : 140 - (decorated ? (rtl ? 5 : 7) : 0);
            float float_block = decorated ? 3 : 0;
            EXPECT_FLOAT_EQ(floating->x, vertical
                ? (reverse_block ? 13 + extent - float_block - 50 : 13 + float_block) : 13 + float_inline);
            EXPECT_FLOAT_EQ(floating->y, vertical
                ? (reverse_line ? 145 - float_inline : 5 + float_inline) : 5 + float_block);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, DifferentSidewaysValuesEstablishIndependentFloatContexts) {
    const struct { const char* parent; const char* child; bool reverse_line; } cases[] = {
        {"vertical-lr", "sideways-lr", false}, {"sideways-lr", "vertical-lr", true},
        {"vertical-rl", "sideways-rl", false}, {"sideways-rl", "vertical-rl", false}};
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.parent);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flow-root;writing-mode:%s;inline-size:210px}"
            "#floating{float:left;inline-size:60px;block-size:50px}"
            "#child{writing-mode:%s;inline-size:70px;block-size:30px}</style>"
            "<div id=result><div id=floating></div><div id=child></div></div>", entry.parent, entry.child);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* child = dom_find_element_by_id(result, "child");
        ASSERT_NE(child, nullptr);
        for (const char* mode : {entry.child, entry.parent, entry.child}) {
            SCOPED_TRACE(mode);
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "writing-mode", mode));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool independent = strcmp(mode, entry.parent) != 0;
            float inline_pos = independent ? 60 : 0;
            EXPECT_FLOAT_EQ(child->y, entry.reverse_line ? 140 - inline_pos : inline_pos);
            EXPECT_FLOAT_EQ(result->width, 50.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalRootBodyShortcutPreservesContainedFloatExtent) {
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl", "sideways-lr", "sideways-rl"})
    for (float margin : {0.0f, 8.0f}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(margin);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><html id=result><style>html{writing-mode:%s}body{margin:%gpx}"
            "#prefix{inline-size:20px;block-size:40px}#floating{float:left;inline-size:30px;block-size:100px}"
            "#b{display:flow-root;inline-size:40px;block-size:20px}</style>"
            "<body id=body><div id=prefix></div><div id=floating></div><div id=b></div></body></html>", mode, margin);
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* body = dom_find_element_by_id(result, "body");
        ASSERT_NE(body, nullptr);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        EXPECT_FLOAT_EQ(result->width, vertical ? 140 + margin : 320);
        EXPECT_FLOAT_EQ(result->height, vertical ? 200 : 140 + margin);
        EXPECT_FLOAT_EQ(body->width, vertical ? 60 : 320 - 2 * margin);
        EXPECT_FLOAT_EQ(body->height, vertical ? 200 - 2 * margin : 60);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, ZeroInlineSizeCannotFitACrossingFloatExclusionInterval) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>body{margin:0}#result{width:400px}"
        ".float{width:250px;height:100px}#right{float:right}#left{float:left}"
        "#child{display:flow-root;width:0;height:200px}</style>"
        "<div id=result><div id=right class=float></div><div id=left class=float></div><div id=child></div></div>");
    DomElement* result = layout_script_source(source, 500, 400);
    ASSERT_NE(result, nullptr);
    DomElement* child = dom_find_element_by_id(result, "child");
    ASSERT_NE(child, nullptr);
    for (const char* height : {"200px", "80px", "200px"}) {
        SCOPED_TRACE(height);
        ASSERT_TRUE(dom_cssom_set_inline_property(child, "height", height));
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        bool intersects_both = strcmp(height, "200px") == 0;
        EXPECT_FLOAT_EQ(child->x, intersects_both ? 250 : 0);
        EXPECT_FLOAT_EQ(child->y, intersects_both ? 100 : 0);
        EXPECT_FLOAT_EQ(child->width, 0.0f);
        EXPECT_FLOAT_EQ(result->height, intersects_both ? 300 : 80);
    }
}

TEST_F(RadiantTextQuery, StretchFloatAvoidanceConsumesLeadingMarginsOnce) {
    for (bool replaced : {false, true}) for (bool rtl : {false, true})
    for (const char* size : {"width:stretch", "width:0;min-width:stretch", "width:1000px;max-width:stretch"}) {
        SCOPED_TRACE(replaced);
        SCOPED_TRACE(rtl);
        SCOPED_TRACE(size);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flow-root;width:200px;direction:%s}"
            "#floating{float:%s;width:100px;height:75px}"
            "#child{display:flow-root;%s;height:25px;padding:0 10px;border:solid;border-width:0 10px}</style>"
            "<div id=result><div id=floating></div><%s id=child></%s></div>", rtl ? "rtl" : "ltr",
            rtl ? "right" : "left", size, replaced ? "canvas" : "div", replaced ? "canvas" : "div");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* child = dom_find_element_by_id(result, "child");
        ASSERT_NE(child, nullptr);
        for (const char* margin : {"0px", "10px", "-10px", "0px"}) {
            SCOPED_TRACE(margin);
            ASSERT_TRUE(dom_cssom_set_inline_property(child, rtl ? "margin-right" : "margin-left", margin));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FLOAT_EQ(child->x, rtl ? 0 : 100);
            EXPECT_FLOAT_EQ(child->y, 0.0f);
            EXPECT_FLOAT_EQ(child->width, 100.0f);
            EXPECT_FLOAT_EQ(result->height, 75.0f);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, ClearanceAndFloatAvoidanceShareFinalInlineSpace) {
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl", "sideways-lr", "sideways-rl"})
    for (const char* direction : {"ltr", "rtl"}) for (const char* side : {"left", "right"})
    for (const char* variant : {"fixed", "margins", "auto", "wide", "replaced"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        SCOPED_TRACE(side);
        SCOPED_TRACE(variant);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        bool reverse_block = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "sideways-rl") == 0;
        bool reverse_line = strcmp(mode, "sideways-lr") == 0;
        bool rtl = strcmp(direction, "rtl") == 0;
        bool margins = strcmp(variant, "margins") == 0 || strcmp(variant, "auto") == 0;
        bool automatic = strcmp(variant, "auto") == 0;
        bool wide = strcmp(variant, "wide") == 0;
        bool replaced = strcmp(variant, "replaced") == 0;
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flow-root;writing-mode:%s;direction:%s;"
            "inline-size:200px}#floating{float:%s;inline-size:60px;block-size:50px}"
            "#child{display:%s;inline-size:%s;block-size:30px;%s}</style>"
            "<div id=result><div id=floating></div><%s id=child></%s></div>", mode, direction, side,
            replaced ? "block" : "flow-root", automatic ? "auto" : wide ? "180px" : "80px",
            margins ? "margin-inline:11px 13px;margin-block:7px 5px" : "", replaced ? "canvas" : "div", replaced ? "canvas" : "div");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* child = dom_find_element_by_id(result, "child");
        ASSERT_NE(child, nullptr);
        for (const char* clear : {"none", "left", "right", "both", "none"}) {
            SCOPED_TRACE(clear);
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "clear", clear));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool cleared = strcmp(clear, "both") == 0 || strcmp(clear, side) == 0;
            bool past_float = cleared || wide;
            float margin_left = margins ? (rtl ? 13 : 11) : 0;
            float margin_right = margins ? (rtl ? 11 : 13) : 0;
            float left = !past_float && strcmp(side, "left") == 0 ? 60 : 0;
            float right = !past_float && strcmp(side, "right") == 0 ? 60 : 0;
            float available = 200 - fmaxf(left - margin_left, 0.0f) - fmaxf(right - margin_right, 0.0f);
            float inline_size = automatic ? available - margin_left - margin_right : wide ? 180 : 80;
            float inline_pos = rtl ? fminf(200 - margin_right, 200 - right) - inline_size : fmaxf(margin_left, left);
            float block_pos = past_float ? 50 : margins ? 7 : 0;
            float extent = fmaxf(50, block_pos + 30 + (margins ? 5 : 0));
            EXPECT_FLOAT_EQ(result->width, vertical ? extent : 200);
            EXPECT_FLOAT_EQ(result->height, vertical ? 200 : extent);
            EXPECT_FLOAT_EQ(child->x, vertical
                ? (reverse_block ? extent - block_pos - 30 : block_pos) : inline_pos);
            EXPECT_FLOAT_EQ(child->y, vertical ? (reverse_line ? 200 - inline_pos - inline_size : inline_pos) : block_pos);
            EXPECT_FLOAT_EQ(child->width, vertical ? 30 : inline_size);
            EXPECT_FLOAT_EQ(child->height, vertical ? inline_size : 30);
            // a box already below the float in its clear:none hypothetical position needs no clearance.
            EXPECT_EQ(child->bound && child->boundary()->has_clearance, cleared && !wide);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, ClearanceResolvesAutomaticInlineSpaceBeforeTextWrapping) {
    for (bool rtl : {false, true}) {
        SCOPED_TRACE(rtl);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flow-root;width:200px;direction:%s;font:10px/10px Ahem}"
            "#floating{float:left;width:60px;height:50px}#child{display:flow-root}</style>"
            "<div id=result><div id=floating></div><div id=child>XXXX XXXX XXXX XXXX</div></div>", rtl ? "rtl" : "ltr");
        DomElement* result = layout_script_source(source);
        ASSERT_NE(result, nullptr);
        DomElement* child = dom_find_element_by_id(result, "child");
        ASSERT_NE(child, nullptr);
        for (const char* clear : {"none", "both", "none"}) {
            SCOPED_TRACE(clear);
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "clear", clear));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool cleared = strcmp(clear, "both") == 0;
            EXPECT_FLOAT_EQ(child->width, cleared ? 200 : 140);
            EXPECT_FLOAT_EQ(child->height, cleared ? 10 : 20);
            EXPECT_FLOAT_EQ(child->x, cleared ? 0 : 60);
            EXPECT_FLOAT_EQ(child->y, cleared ? 50 : 0);
            EXPECT_FLOAT_EQ(result->height, cleared ? 60 : 50);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, VerticalAutomaticInlineExtentIgnoresBlockMargins) {
    const struct State { float font; float width; float height; bool switch_mode; } states[] = {
        {20,20,20,false}, {20,17,31,false}, {32,17,31,false}, {20,20,20,true}, {20,20,20,false}};
    for (const char* initial_mode : {"horizontal-tb", "vertical-lr", "vertical-rl"})
    for (const char* direction : {"ltr", "rtl"}) for (float margin : {0.0f,7.0f,20.0f})
    for (const char* prefix : {"","XX","X "}) {
        bool nested = *prefix != '\0';
        SCOPED_TRACE(initial_mode);
        SCOPED_TRACE(direction);
        SCOPED_TRACE(margin);
        SCOPED_TRACE(prefix);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flow-root;writing-mode:%s;direction:%s;font:20px/1 Ahem}"
            "p{margin-block:%gpx;margin-inline:0;border:1px solid}label{white-space:pre-wrap}"
            "span{display:inline-block;writing-mode:horizontal-tb;width:20px;height:20px;line-height:1em}</style>"
            "<div id=result><p>%s%s<span id=child>0</span>%s</p><p><span id=other>0</span></p></div>",
            initial_mode, direction, margin, nested ? "<label>" : "", prefix, nested ? "</label>" : "");
        DomElement* result = layout_script_source(source, 1200, 800);
        ASSERT_NE(result, nullptr);
        DomElement* child = dom_find_element_by_id(result, "child");
        DomElement* other = dom_find_element_by_id(result, "other");
        ASSERT_NE(child, nullptr);
        ASSERT_NE(other, nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.font);
            SCOPED_TRACE(state.width);
            SCOPED_TRACE(state.height);
            const char* mode = state.switch_mode
                ? (strcmp(initial_mode,"horizontal-tb") == 0 ? "vertical-lr" : "horizontal-tb") : initial_mode;
            SCOPED_TRACE(mode);
            char value[32];
            str_fmt(value,sizeof(value),"%gpx",state.font);
            ASSERT_TRUE(dom_cssom_set_inline_property(result,"font-size",value));
            ASSERT_TRUE(dom_cssom_set_inline_property(result,"writing-mode",mode));
            for (DomElement* atom : {child,other}) {
                str_fmt(value,sizeof(value),"%gpx",state.width);
                ASSERT_TRUE(dom_cssom_set_inline_property(atom,"width",value));
                str_fmt(value,sizeof(value),"%gpx",state.height);
                ASSERT_TRUE(dom_cssom_set_inline_property(atom,"height",value));
            }
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            float line = fmaxf(state.font,vertical ? state.width : state.height);
            float block_extent = 2 * (line + 2) + 3 * margin;
            float text_advance = nested ? 2 * state.font : 0;
            // block margins affect block progression; the atomic used height owns vertical inline extent.
            EXPECT_FLOAT_EQ(result->width,vertical ? block_extent : 1200);
            EXPECT_FLOAT_EQ(result->height,vertical ? text_advance + state.height + 2 : block_extent);
            EXPECT_FLOAT_EQ(child->width,state.width);
            EXPECT_FLOAT_EQ(child->height,state.height);
            // inline wrappers share their block's coordinates; only block ancestors add origins.
            RdtLogicalPoint child_offset = view_geometry_ancestor_offset(child,result,true);
            EXPECT_FLOAT_EQ(child_offset.x,vertical
                ? (strcmp(mode,"vertical-rl") == 0 ? block_extent-margin-1-(line+state.width)/2
                    : margin+1+(line-state.width)/2)
                : strcmp(direction,"rtl") == 0 ? 1199-text_advance-state.width : 1+text_advance);
            EXPECT_FLOAT_EQ(child_offset.y,vertical
                ? (strcmp(direction,"rtl") == 0 ? 1 : 1+text_advance) : margin+1);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, CombinedDigitsKeepOneEmAutomaticInlineExtent) {
    for (const char* mode : {"vertical-lr","vertical-rl"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flow-root;writing-mode:%s;font:40px/1 Ahem}"
            "p{margin-block:40px;margin-inline:0}#child{text-combine-upright:digits 2}</style>"
            "<div id=result><p><span id=child>12</span></p></div>",mode);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        for (const char* value : {"digits 2","none","digits 3","all","digits 4","digits 2"}) {
            SCOPED_TRACE(value);
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"text-combine-upright",value));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // CSS Writing Modes 4 §9.1: this two-digit run is a single 1em typographic unit.
            float inline_extent = strcmp(value,"none") == 0 ? 80 : 40;
            EXPECT_FLOAT_EQ(result->width,120);
            EXPECT_FLOAT_EQ(result->height,inline_extent);
            EXPECT_FLOAT_EQ(child->width,40);
            EXPECT_FLOAT_EQ(child->height,inline_extent);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, InternalIdeographicSpaceDoesNotHangBeforeNestedAtomic) {
    for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flow-root;writing-mode:%s;font:20px/20px Ahem}"
            "p{margin-block:7px;margin-inline:0;border:1px solid}"
            "i{display:inline-block;writing-mode:horizontal-tb;width:17px;height:31px}</style>"
            "<div id=result><p><label>X&#x3000;<i id=child></i></label></p></div>",mode);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        bool vertical = strcmp(mode,"horizontal-tb") != 0;
        // the space is internal to the line; only a terminal suffix can hang (CSS Text Phase II).
        EXPECT_FLOAT_EQ(result->width,vertical ? 36 : 1200);
        // the empty horizontal atom uses its bottom edge as baseline, leaving Ahem's 4px descent.
        EXPECT_FLOAT_EQ(result->height,vertical ? 73 : 51);
        EXPECT_FLOAT_EQ(child->width,17);
        EXPECT_FLOAT_EQ(child->height,31);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
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

TEST_F(RadiantTextQuery, CssBidiScopesMatchTheirUnicodeControlSequences) {
    const struct { const char* mode; const char* direction; const char* open; const char* close; const char* text; } cases[] = {
        {"normal", "rtl", "", "", "ABCD"},
        {"embed", "ltr", u8"\u202a", u8"\u202c", "ABCD"},
        {"embed", "rtl", u8"\u202b", u8"\u202c", "ABCD"},
        {"bidi-override", "ltr", u8"\u202d", u8"\u202c", "ABCD"},
        {"bidi-override", "rtl", u8"\u202e", u8"\u202c", "ABCD"},
        {"isolate", "rtl", u8"\u2067", u8"\u2069", "ABCD"},
        {"isolate-override", "rtl", u8"\u2068\u202e", u8"\u202c\u2069", "ABCD"},
        {"plaintext", "ltr", u8"\u2068", u8"\u2069", u8"אב"}
    };
    for (const char* parent_direction : {"ltr", "rtl"}) {
        for (const auto& entry : cases) {
            SCOPED_TRACE(parent_direction);
            SCOPED_TRACE(entry.mode);
            SCOPED_TRACE(entry.direction);
            StrBuf* source = strbuf_new();
            strbuf_append_format(source,
                "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
                "body{margin:0;font:20px/1 Ahem}.probe{width:200px;direction:%s}"
                "#scope{unicode-bidi:%s;direction:%s}</style>"
                "<div class=probe>X <span id=scope><span id=result>%s</span></span><span id=after> 3</span></div>"
                "<div class=probe>X <span>%s<span id=control>%s</span>%s</span><span id=control-after> 3</span></div>",
                parent_direction, entry.mode, entry.direction, entry.text, entry.open, entry.text, entry.close);
            ASSERT_NE(layout_script_source(source), nullptr);
            const struct { const char* actual; const char* control; } pairs[] = {
                {"result", "control"}, {"after", "control-after"}
            };
            for (const auto& pair : pairs) {
                expect_inline_pair(pair.actual, pair.control);
            }
            ui.document = nullptr;
            free_document(document);
            document = nullptr;
        }
    }
}

TEST_F(RadiantTextQuery, CssBidiRootAndNestedScopesPreserveAtomicAndTextOrder) {
    for (const char* direction : {"ltr", "rtl"}) {
        for (bool atomic : {false, true}) {
            SCOPED_TRACE(direction);
            SCOPED_TRACE(atomic);
            StrBuf* source = strbuf_new();
            strbuf_append_format(source,
                "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
                "body{margin:0;font:20px/1 Ahem}.probe{width:200px;direction:%s}"
                "#probe{unicode-bidi:bidi-override}.atom{display:inline-block;width:20px;height:20px}</style>"
                "<div class=probe id=probe>%s<span id=result>ABCD</span><span id=after> 3</span></div>"
                "<div class=probe>%s%s<span id=control>ABCD</span><span id=control-after> 3</span>\u202c</div>",
                direction, atomic ? "<span id=atom class=atom></span>" : "",
                strcmp(direction, "rtl") == 0 ? u8"\u202e" : u8"\u202d",
                atomic ? "<span id=control-atom class=atom></span>" : "");
            ASSERT_NE(layout_script_source(source), nullptr);
            expect_inline_pair("result", "control");
            expect_inline_pair("after", "control-after");
            if (atomic) expect_inline_pair("atom", "control-atom");
            ui.document = nullptr;
            free_document(document);
            document = nullptr;
        }
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0;font:20px/1 Ahem}.probe{width:240px;direction:%s}"
            "#scope{unicode-bidi:embed;direction:rtl}#inner{unicode-bidi:bidi-override;direction:ltr}</style>"
            "<div class=probe>X <span id=scope>1 <span id=inner><span id=result>ABCD</span></span> 2</span>"
            "<span id=after> 3</span></div><div class=probe>X \u202b1 \u202d<span id=control>ABCD</span>"
            "\u202c 2\u202c<span id=control-after> 3</span></div>", direction);
        ASSERT_NE(layout_script_source(source), nullptr);
        expect_inline_pair("result", "control");
        expect_inline_pair("after", "control-after");
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, PlaintextParagraphDirectionSurvivesSoftWrapsAndResetsAtForcedBreaks) {
    const struct { const char* first; const char* second; const char* direction; } cases[] = {
        {u8"אאאאאאאא", "MMMMM", "rtl"},
        {"MMMMM", u8"אאאאאאאא", "ltr"},
        {"12345 67890", u8"אאאאאאאא", "rtl"}
    };
    for (const auto& entry : cases) {
        for (bool newline : {false, true}) {
            SCOPED_TRACE(entry.first);
            SCOPED_TRACE(newline);
            StrBuf* source = strbuf_new();
            strbuf_append_format(source,
                "<!doctype html><meta charset=utf-8><style>body{margin:0}.probe{font:16px/1 Arial;"
                "width:10ch;border:1px solid}#result{unicode-bidi:plaintext;white-space:%s}"
                ".paragraph{display:block}</style><div id=result class=probe>"
                "<span id=first>%s</span> <span id=second>%s</span>%s"
                "<span id=next-first>MMMMM</span> <span id=next-second>אאאאאאאא</span></div>"
                "<div class=probe><span id=control-paragraph class=paragraph style='direction:%s'>"
                "<span id=control-first>%s</span> <span id=control-second>%s</span></span>"
                "<span class=paragraph style='direction:ltr'><span id=control-next-first>MMMMM</span> "
                "<span id=control-next-second>אאאאאאאא</span></span></div>",
                newline ? "pre-line" : "normal", entry.first, entry.second, newline ? "\n" : "<br>",
                entry.direction, entry.first, entry.second);
            DomElement* block = layout_script_source(source);
            ASSERT_NE(block, nullptr);
            expect_inline_pair("first", "control-first");
            expect_inline_pair("second", "control-second");
            expect_inline_pair("next-first", "control-next-first");
            expect_inline_pair("next-second", "control-next-second");
            EXPECT_EQ(block->block()->direction, CSS_VALUE_LTR);
            const struct { const char* id; const char* text; } updates[] = {
                {"first", "MMMMM"}, {"control-first", "MMMMM"},
                {"second", u8"אאאאאאאא"}, {"control-second", u8"אאאאאאאא"}
            };
            for (const auto& update : updates) {
                DomElement* span = dom_find_element_by_id(document->root->as_element(), update.id);
                ASSERT_NE(span, nullptr);
                ASSERT_TRUE(dom_text_set_content(span->first_child->as_text(), update.text));
            }
            DomElement* control = dom_find_element_by_id(document->root->as_element(), "control-paragraph");
            ASSERT_NE(control, nullptr);
            ASSERT_TRUE(dom_cssom_set_inline_property(control, "direction", "ltr"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            expect_inline_pair("first", "control-first");
            expect_inline_pair("second", "control-second");
            EXPECT_EQ(block->block()->direction, CSS_VALUE_LTR);
            ui.document = nullptr;
            free_document(document);
            document = nullptr;
        }
    }
}

TEST_F(RadiantTextQuery, PlaintextP2SkipsIsolatesAndAtomicInlineContents) {
    const struct { const char* leading; const char* target; const char* trailing; const char* direction; } cases[] = {
        {"", "12345 67890", u8"<span style='unicode-bidi:isolate;direction:rtl'>אאאא</span>", "ltr"},
        {"", "12345 67890", u8"<bdi>אאאא</bdi>", "ltr"},
        {"", "12345 67890", u8"<span dir=rtl>אאאא</span>", "ltr"},
        {"", "12345 67890", u8"\u2067אאאא\u2069", "ltr"},
        {"", "12345 67890", u8"<span style='display:none'>אאאא</span>", "ltr"},
        {"", "12345 67890", u8"<span style='display:inline-block'>אאאא</span>", "ltr"},
        {u8"<span>אאאאאאאא</span><span style='display:inline-block;width:10px;height:10px'></span>",
            "MMMMM", "", "rtl"},
        {u8"<span>אאאאאאאא</span><span style='display:inline-block;width:20px;height:10px'></span>",
            "MMMMM", "", "rtl"}
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.trailing);
        SCOPED_TRACE(entry.leading);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><meta charset=utf-8><style>body{margin:0}.probe{font:16px/1 Arial;"
            "width:10ch;border:1px solid;direction:rtl}#result{unicode-bidi:plaintext}"
            "#control-box{direction:%s}</style><div id=result class=probe>%s"
            "<span id=word>%s</span> %s</div><div id=control-box class=probe>%s"
            "<span id=control-word>%s</span> %s</div>", entry.direction,
            entry.leading, entry.target, entry.trailing, entry.leading, entry.target, entry.trailing);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        expect_inline_pair("word", "control-word");
        EXPECT_EQ(block->block()->direction, CSS_VALUE_RTL);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, ResolvedBidiRunsReachRasterAndVectorPaintAcrossReflow) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
        "body{margin:0;font:20px/1 Ahem}#result{width:80px;direction:rtl;unicode-bidi:bidi-override}"
        "</style><div id=result>ABCD</div>");
    DomElement* block = layout_script_source(source);
    ASSERT_NE(block, nullptr);
    DomText* text = block->first_child->as_text();
    ASSERT_NE(text, nullptr);
    for (bool rtl : {true, false, true}) {
        SCOPED_TRACE(rtl);
        ASSERT_TRUE(dom_cssom_set_inline_property(block, "direction", rtl ? "rtl" : "ltr"));
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        RenderContext context = {};
        setup_text_paint_context(&context, text);
        RenderTextPaintScope paint(&context, lam::view_as_block(block), &context.font, &context.color);
        RenderTextPaintIterator raster(&context, lam::view_require_text(text));
        int pieces = 0;
        while (TextRect* rect = raster.next()) {
            ASSERT_LT(pieces, rtl ? 4 : 1);
            EXPECT_EQ(rect->length, rtl ? 1 : 4);
            EXPECT_FLOAT_EQ(rect->x, rtl ? 60.0f - pieces * 20.0f : 0.0f);
            EXPECT_FLOAT_EQ(rect->width, rtl ? 20.0f : 80.0f);
            EXPECT_EQ(rect->start_index, rtl ? pieces : 0);
            pieces++;
        }
        EXPECT_EQ(pieces, rtl ? 4 : 1);
        RenderTextPaintIterator vector(&context, lam::view_require_text(text));
        TextRect* rect = vector.next(true);
        ASSERT_NE(rect, nullptr);
        PaintGlyphRun run = {};
        const char* logical = nullptr;
        lam::Temp<char> content(render_text_rect_glyph_run(lam::view_require_text(text), rect,
            &context.font, context.color, 0, 0, &run, vector.reverses_clusters(), &logical));
        ASSERT_NE(content.get(), nullptr);
        EXPECT_STREQ(run.text.get(), rtl ? "DCBA" : "ABCD");
        EXPECT_STREQ(logical, "ABCD");
        EXPECT_EQ(vector.next(true), nullptr);
    }
}

TEST_F(RadiantTextQuery, BidiPaintKeepsGraphemesAndFollowingDirectionalRuns) {
    const struct { const char* mode; const char* text; const char* visual[3]; size_t count; } cases[] = {
        {"bidi-override", u8"áB🤹‍♀️C", {u8"C🤹‍♀️Bá", nullptr, nullptr}, 1},
        {"normal", u8"AאבB", {"A", u8"בא", "B"}, 3}
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.text);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0;font:20px/1 Ahem}#result{width:200px;direction:rtl;unicode-bidi:%s}"
            "</style><div id=result>%s</div>", entry.mode, entry.text);
        DomElement* block = layout_script_source(source);
        ASSERT_NE(block, nullptr);
        DomText* text = block->first_child->as_text();
        ASSERT_NE(text, nullptr);
        {
            RenderContext context = {};
            setup_text_paint_context(&context, text);
            RenderTextPaintScope paint(&context, lam::view_as_block(block), &context.font, &context.color);
            RenderTextPaintIterator vector(&context, lam::view_require_text(text));
            size_t index = 0;
            float previous_x = INFINITY;
            while (TextRect* rect = vector.next(true)) {
                ASSERT_LT(index, entry.count);
                PaintGlyphRun run = {};
                lam::Temp<char> content(render_text_rect_glyph_run(lam::view_require_text(text), rect,
                    &context.font, context.color, 0, 0, &run, vector.reverses_clusters()));
                ASSERT_NE(content.get(), nullptr);
                EXPECT_STREQ(run.text.get(), entry.visual[index]);
                EXPECT_LT(run.x, previous_x);
                previous_x = run.x;
                index++;
            }
            EXPECT_EQ(index, entry.count);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, BidiFormattingAcrossInlineBoundariesPreservesEveryPaintRun) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
        "body{margin:0;font:20px/1 Ahem}#result{width:400px}</style><div id=result>"
        "<span id=prefix>a\u202el\u202d</span><span id=one>c\u202ej\u202de\u202e</span>"
        "<span id=middle>h\u202dg\u202cf</span><span id=two>\u202ci\u202cd\u202ck\u202cb</span>"
        "<span id=suffix>\u202cm</span></div>");
    DomElement* block = layout_script_source(source);
    ASSERT_NE(block, nullptr);
    DomElement* suffix = dom_find_element_by_id(block, "suffix");
    ASSERT_NE(suffix, nullptr);
    const struct { const char* content; uint32_t last; } states[] = {
        {"\u202cm", 'm'}, {"\u202cz", 'z'}, {"\u202cm", 'm'}
    };
    struct Fragment { int start, length; float x, width; };
    const struct { const char* id; int count; Fragment fragments[4]; } queries[] = {
        {"prefix", 3, {{0, 1, 0, 20}, {1, 4, 220, 20}, {5, 3, 40, 0}}},
        {"one", 4, {{0, 1, 40, 20}, {1, 4, 180, 20}, {5, 4, 80, 20}, {9, 3, 160, 0}}},
        {"middle", 1, {{0, 9, 100, 60}}},
        {"two", 4, {{0, 4, 160, 20}, {4, 4, 60, 20}, {8, 4, 200, 20}, {12, 4, 20, 20}}},
        {"suffix", 1, {{0, 4, 240, 20}}}
    };
    for (bool rtl : {false, true}) for (const auto& state : states) {
        SCOPED_TRACE(state.content);
        SCOPED_TRACE(rtl);
        ASSERT_TRUE(dom_cssom_set_inline_property(block, "direction", rtl ? "rtl" : "ltr"));
        ASSERT_TRUE(dom_text_set_content(suffix->first_child->as_text(), state.content));
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        struct Capture { bool seen[13]; int count; uint32_t last; float origin; } capture = {{}, 0, state.last, rtl ? 400.0f - 13.0f * 20.0f : 0.0f};
        auto visit = [](void* pointer, uint32_t codepoint, FontHandle*, float x, float, float) {
            Capture* capture = (Capture*)pointer;
            if (codepoint < 'a' || codepoint > 'z') return true;
            int index = codepoint == capture->last ? 12 : (int)(codepoint - 'a');
            EXPECT_GE(index, 0);
            EXPECT_LT(index, 13);
            if (index < 0 || index >= 13) return false;
            EXPECT_FALSE(capture->seen[index]);
            capture->seen[index] = true;
            capture->count++;
            EXPECT_FLOAT_EQ(x, capture->origin + 20.0f * index) << (char)codepoint;
            return true;
        };
        for (const auto& query : queries) {
            SCOPED_TRACE(query.id);
            DomElement* span = dom_find_element_by_id(block, query.id);
            ASSERT_NE(span, nullptr);
            DomText* text = span->first_child->as_text();
            ASSERT_NE(text, nullptr);
            TextRect* rect = text->rect;
            for (int i = 0; i < query.count; i++) {
                ASSERT_NE(rect, nullptr);
                EXPECT_EQ(rect->start_index, query.fragments[i].start);
                EXPECT_EQ(rect->length, query.fragments[i].length);
                EXPECT_FLOAT_EQ(rect->x, capture.origin + query.fragments[i].x);
                EXPECT_FLOAT_EQ(rect->width, query.fragments[i].width);
                rect = rect->next;
            }
            EXPECT_EQ(rect, nullptr);
            ASSERT_TRUE(visit_text_paint_glyphs(block, text, visit, &capture));
        }
        EXPECT_EQ(capture.count, 13);
        DomText* middle = dom_find_element_by_id(block, "middle")->first_child->as_text();
        DomRange* range = dom_range_create(document->state);
        ASSERT_NE(range, nullptr);
        const char* exception = nullptr;
        for (uint32_t index = 0; index < 3; index++) {
            float caret = dom_range_glyph_x_for_byte_offset(&ui, lam::view_require_text(middle), middle->rect, 4 * index);
            EXPECT_FLOAT_EQ(caret, capture.origin + (index == 0 ? 160.0f : 120.0f));
            ASSERT_TRUE(dom_range_set_start(range, middle, 2 * index, &exception));
            ASSERT_TRUE(dom_range_set_end(range, middle, 2 * index + 1, &exception));
            ASSERT_TRUE(dom_range_resolve_layout(range));
            struct Rectangle { float x, width; int count; } rectangle = {};
            dom_range_for_each_rect(range, &ui, [](float x, float, float width, float, void* pointer) {
                Rectangle* rectangle = (Rectangle*)pointer;
                rectangle->x = x; rectangle->width = width; rectangle->count++;
            }, &rectangle);
            EXPECT_EQ(rectangle.count, 1);
            EXPECT_FLOAT_EQ(rectangle.x, capture.origin + 140.0f - 20.0f * index);
            EXPECT_FLOAT_EQ(rectangle.width, 20.0f);
        }
        dom_range_release(range);
    }
}

TEST_F(RadiantTextQuery, BidiPaintClippingMatchesTheEquivalentVisualOrder) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
        "body{margin:0;font:20px/1 Ahem}.probe{width:60px;direction:rtl;overflow:hidden;"
        "text-overflow:ellipsis;white-space:nowrap}span{unicode-bidi:bidi-override}"
        "#actual{direction:rtl}#control{direction:ltr}</style>"
        "<div id=result class=probe><span id=actual>ABCD</span></div>"
        "<div id=control-box class=probe><span id=control>DCBA</span></div>");
    ASSERT_NE(layout_script_source(source), nullptr);
    struct Glyph { uint32_t codepoint; float x; };
    struct Capture { Glyph glyphs[8]; size_t count; } captures[2] = {};
    const struct { const char* box; const char* text; } cases[] = {
        {"result", "actual"}, {"control-box", "control"}
    };
    for (size_t i = 0; i < 2; i++) {
        DomElement* block = dom_find_element_by_id(document->root->as_element(), cases[i].box);
        DomElement* span = dom_find_element_by_id(document->root->as_element(), cases[i].text);
        ASSERT_NE(block, nullptr);
        ASSERT_NE(span, nullptr);
        DomText* text = span->first_child->as_text();
        ASSERT_NE(text, nullptr);
        auto visit = [](void* pointer, uint32_t codepoint, FontHandle*, float x, float, float) {
            Capture* capture = (Capture*)pointer;
            if (capture->count == 8) return false;
            capture->glyphs[capture->count++] = {codepoint, x};
            return true;
        };
        ASSERT_TRUE(visit_text_paint_glyphs(block, text, visit, &captures[i]));
        for (size_t a = 0; a < captures[i].count; a++) {
            for (size_t b = a + 1; b < captures[i].count; b++) {
                if (captures[i].glyphs[b].x < captures[i].glyphs[a].x) {
                    Glyph swap = captures[i].glyphs[a];
                    captures[i].glyphs[a] = captures[i].glyphs[b];
                    captures[i].glyphs[b] = swap;
                }
            }
        }
    }
    ASSERT_EQ(captures[0].count, 3u);
    ASSERT_EQ(captures[1].count, captures[0].count);
    const uint32_t expected[] = {0x2026, 'B', 'A'};
    for (size_t i = 0; i < captures[0].count; i++) {
        EXPECT_EQ(captures[0].glyphs[i].codepoint, expected[i]);
        EXPECT_EQ(captures[0].glyphs[i].codepoint, captures[1].glyphs[i].codepoint);
        EXPECT_FLOAT_EQ(captures[0].glyphs[i].x, captures[1].glyphs[i].x);
    }
}

TEST_F(RadiantTextQuery, OwnedFlowSnapshotsOwnTheirBidiPaintRuns) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
        "body{margin:0;font:20px/1 Ahem}#result{width:80px;direction:rtl;unicode-bidi:bidi-override}"
        "</style><div id=result>ABCD</div>");
    DomElement* block = layout_script_source(source);
    ASSERT_NE(block, nullptr);
    DomText* text = block->first_child->as_text();
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->rect, nullptr);
    ASSERT_NE(text->rect->bidi_runs, nullptr);
    FlexFlowTestContext flow(&ui, document, block);
    auto* result = radiant::layout_flow_result_capture(&flow.layout, block);
    ASSERT_NE(result, nullptr);
    for (unsigned pass = 0; pass < 3; pass++) {
        text->rect->bidi_runs->x = 999.0f;
        ASSERT_TRUE(radiant::layout_flow_result_apply(&flow.layout, result));
        ASSERT_NE(text->rect->bidi_runs, nullptr);
        EXPECT_FLOAT_EQ(text->rect->bidi_runs->x, 0.0f);
        EXPECT_TRUE(text->rect->bidi_runs->bidi_reversed);
    }
    radiant::layout_flow_result_release(document->view_tree->prop_pool, result);
    RenderContext context = {};
    setup_text_paint_context(&context, text);
    RenderTextPaintScope paint(&context, lam::view_as_block(block), &context.font, &context.color);
    RenderTextPaintIterator vector(&context, lam::view_require_text(text));
    TextRect* rect = vector.next(true);
    ASSERT_NE(rect, nullptr);
    PaintGlyphRun run = {};
    lam::Temp<char> content(render_text_rect_glyph_run(lam::view_require_text(text), rect,
        &context.font, context.color, 0, 0, &run, vector.reverses_clusters()));
    ASSERT_NE(content.get(), nullptr);
    EXPECT_STREQ(run.text.get(), "DCBA");
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

TEST_F(RadiantTextQuery, ShrinkToFitPercentagePaddingKeepsCyclicAndUsedBasesSeparate) {
    const struct State { const char* width; bool content; } states[] = {
        {"auto",false},{"auto",true},{"80px",true},{"120px",true},
        {"80px",false},{"auto",false},{"auto",true},{"auto",false}};
    for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl"})
    for (const char* display : {"block","flex"}) for (bool floated : {false,true})
    for (const char* padding : {"25%","calc(10% + 3px)","calc(10% - 3px)"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(display);
        SCOPED_TRACE(floated);
        SCOPED_TRACE(padding);
        bool math = strcmp(padding,"25%") != 0;
        float fixed_term = !math ? 0.0f : strcmp(padding,"calc(10% + 3px)") == 0 ? 3.0f : -3.0f;
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{position:absolute;left:0;top:0;width:240px;height:180px}"
            "#host{%s}#child{writing-mode:%s;display:%s;padding:%s;border:1px solid}"
            "#leaf{display:none;width:40px;height:30px}</style><div id=result><div id=host>"
            "<div id=child><div id=leaf></div></div></div></div>",
            floated ? "float:left" : "display:inline-block",mode,display,padding);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* host = dom_find_element_by_id(result,"host");
        DomElement* child = dom_find_element_by_id(result,"child");
        DomElement* leaf = dom_find_element_by_id(result,"leaf");
        ASSERT_NE(host,nullptr); ASSERT_NE(child,nullptr); ASSERT_NE(leaf,nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.width);
            SCOPED_TRACE(state.content);
            ASSERT_TRUE(dom_cssom_set_inline_property(host,"inline-size",state.width));
            ASSERT_TRUE(dom_cssom_set_inline_property(leaf,"display",state.content ? "block" : "none"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool automatic = strcmp(state.width,"auto") == 0;
            float natural_width = (state.content ? 40.0f : 0.0f) + 2.0f * fmaxf(fixed_term,0.0f) + 2.0f;
            float host_width = automatic ? natural_width : strcmp(state.width,"80px") == 0 ? 80.0f : 120.0f;
            float pad = fmaxf(host_width * (math ? 0.1f : 0.25f) + fixed_term,0.0f);
            float decoration = 2.0f * (pad + 1.0f);
            float child_width = strcmp(mode,"horizontal-tb") == 0
                ? fmaxf(host_width,decoration) : (state.content ? 40.0f : 0.0f) + decoration;
            float child_height = (state.content ? 30.0f : 0.0f) + decoration;
            // cyclic contributions omit only percentage terms; used edges resolve after the parent width is final.
            EXPECT_FLOAT_EQ(host->width,host_width);
            EXPECT_FLOAT_EQ(host->height,child_height);
            EXPECT_FLOAT_EQ(child->width,child_width);
            EXPECT_FLOAT_EQ(child->height,child_height);
            EXPECT_FLOAT_EQ(child->boundary()->padding.left,pad);
            EXPECT_FLOAT_EQ(child->boundary()->padding.top,pad);
            if (automatic) {
                uint64_t geometry = retained_subtree_geometry(host);
                FlexFlowTestContext flow(&ui,document,host);
                IntrinsicSizes natural = layout_measure_intrinsic_widths(&flow.layout,host,true);
                EXPECT_FLOAT_EQ(natural.min_content,natural_width);
                EXPECT_FLOAT_EQ(natural.max_content,natural_width);
                EXPECT_EQ(retained_subtree_geometry(host),geometry);
                EXPECT_FLOAT_EQ(child->boundary()->padding.left,pad);
            }
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantFormControls, ForcedFlowTextIntrinsicAxesRemainCurrentInFlex) {
    const struct State { bool longer; float leading; bool fixed; bool switch_mode; bool rtl; } states[] = {
        {false,20,false,false,false},{true,20,false,false,false},
        {true,30,false,false,false},{true,0,false,false,false},
        {true,20,true,false,false},{true,20,true,true,false},
        {true,20,true,true,true},{false,20,false,false,false}};
    for (const char* initial_mode : {"horizontal-tb","vertical-lr","vertical-rl"})
    for (bool parallel : {false,true}) for (const char* direction : {"row","column"})
    for (const char* kind : {"span","button"}) for (bool br : {false,true}) {
        SCOPED_TRACE(initial_mode);
        SCOPED_TRACE(parallel);
        SCOPED_TRACE(direction);
        SCOPED_TRACE(kind);
        SCOPED_TRACE(br);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#host{position:absolute;left:0;top:0;display:flex;align-items:flex-start;flex-direction:%s}"
            "#ordinary{position:absolute;left:0;top:600px}#child,#control{display:inline-block;appearance:none;"
            "box-sizing:content-box;font:20px/20px Ahem;white-space:%s;text-align:center;"
            "border:2px solid;padding-block:1px;padding-inline:6px}</style><div id=result>"
            "<div id=host><%s id=child>%s</%s></div><div id=ordinary><%s id=control>%s</%s></div></div>",
            direction,br ? "normal" : "pre",kind,br ? "XXX<br>X" : "XXX\nX",kind,
            kind,br ? "XXX<br>X" : "XXX\nX",kind);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* host = dom_find_element_by_id(result,"host");
        DomElement* child = dom_find_element_by_id(result,"child");
        DomElement* control = dom_find_element_by_id(result,"control");
        ASSERT_NE(host,nullptr); ASSERT_NE(child,nullptr); ASSERT_NE(control,nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.longer);
            SCOPED_TRACE(state.leading);
            SCOPED_TRACE(state.fixed);
            SCOPED_TRACE(state.switch_mode);
            SCOPED_TRACE(state.rtl);
            const char* mode = state.switch_mode
                ? (strcmp(initial_mode,"horizontal-tb") == 0 ? "vertical-lr" : "horizontal-tb") : initial_mode;
            ASSERT_TRUE(dom_cssom_set_inline_property(host,"writing-mode",parallel ? mode : "horizontal-tb"));
            char font[48]; str_fmt(font,sizeof(font),"20px/%gpx Ahem",state.leading);
            for (DomElement* element : {child,control}) {
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"font",font));
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"writing-mode",mode));
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"direction",state.rtl ? "rtl" : "ltr"));
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"inline-size",state.fixed ? "120px" : "auto"));
                if (br) {
                    ASSERT_TRUE(dom_text_set_content(element->first_child->as_text(),state.longer ? "XX" : "XXX"));
                    ASSERT_TRUE(dom_text_set_content(element->last_child->as_text(),state.longer ? "XXXXX" : "X"));
                } else ASSERT_TRUE(dom_text_set_content(element->first_child->as_text(),state.longer ? "XX\nXXXXX" : "XXX\nX"));
            }
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // forced lines retain their physical axes when their parent changes formatting context.
            EXPECT_FLOAT_EQ(child->width,control->width);
            EXPECT_FLOAT_EQ(child->height,control->height);
            uint64_t geometry = retained_subtree_geometry(child);
            FlexFlowTestContext flow(&ui,document,child);
            IntrinsicSize natural = {};
            EXPECT_TRUE(layout_measure_direct_text_intrinsic_size(&flow.layout,lam::view_as_block(child),&natural));
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            float inline_extent = state.longer ? 100.0f : 60.0f;
            float block_extent = 2.0f * state.leading;
            EXPECT_FLOAT_EQ(natural.max_width,vertical ? block_extent : inline_extent);
            EXPECT_FLOAT_EQ(natural.max_height,vertical ? inline_extent : block_extent);
            EXPECT_EQ(retained_subtree_geometry(child),geometry);
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantFormControls, RangeLogicalDefaultsAndZeroContentMeasurementsStayCurrent) {
    for (bool decorated : {false,true}) for (bool ticks : {false,true}) {
        SCOPED_TRACE(decorated);
        SCOPED_TRACE(ticks);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:flow-root}#child{box-sizing:content-box;%s}"
            "</style><div id=result><input id=child type=range %s>"
            "<datalist id=ticks><option value=0><option value=50><option value=100></datalist></div>",
            decorated ? "padding:3px 7px 5px 11px;border:2px solid" : "",ticks ? "list=ticks" : "");
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl","sideways-lr","sideways-rl","horizontal-tb"})
        for (const char* inline_size : {"auto","0px","120px","auto"}) {
            SCOPED_TRACE(mode);
            SCOPED_TRACE(inline_size);
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"writing-mode",mode));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"inline-size",inline_size));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            float inline_extent = strcmp(inline_size,"auto") == 0 ? FormDefaults::RANGE_WIDTH
                : strcmp(inline_size,"0px") == 0 ? 0.0f : 120.0f;
            float block_extent = ticks ? FormDefaults::RANGE_HEIGHT_WITH_LIST : FormDefaults::RANGE_HEIGHT;
            float content_width = vertical ? block_extent : inline_extent;
            float content_height = vertical ? inline_extent : block_extent;
            EXPECT_FLOAT_EQ(child->width,content_width+(decorated ? 22.0f : 0.0f));
            EXPECT_FLOAT_EQ(child->height,content_height+(decorated ? 12.0f : 0.0f));
            uint64_t geometry = retained_subtree_geometry(child);
            float intrinsic_width = child->form->intrinsic_width;
            float intrinsic_height = child->form->intrinsic_height;
            FlexFlowTestContext flow(&ui,document,child);
            for (unsigned query = 0; query < 2; query++) {
                IntrinsicSize measured = layout_measure_form_control(&flow.layout,lam::view_as_block(child),AvailableSpace::make_indefinite());
                // the query returns physical content dimensions, including zero, without changing committed boxes.
                EXPECT_FLOAT_EQ(measured.max_width,content_width);
                EXPECT_FLOAT_EQ(measured.max_height,content_height);
                IntrinsicSize natural = layout_measure_native_control_intrinsics(&flow.layout,lam::view_as_block(child));
                EXPECT_FLOAT_EQ(natural.max_width,vertical ? block_extent : FormDefaults::RANGE_WIDTH);
                EXPECT_FLOAT_EQ(natural.max_height,vertical ? FormDefaults::RANGE_WIDTH : block_extent);
                EXPECT_EQ(retained_subtree_geometry(child),geometry);
                EXPECT_FLOAT_EQ(child->form->intrinsic_width,intrinsic_width);
                EXPECT_FLOAT_EQ(child->form->intrinsic_height,intrinsic_height);
            }
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantFormControls, FlexNativeControlSizingFollowsLiveFlowMetrics) {
    const struct State { const char* inline_size; float font; bool longer; bool switch_mode; bool rtl; bool physical; } states[] = {
        {"auto",20,false,false,false,false}, {"auto",20,true,false,false,false},
        {"0px",20,true,false,false,false}, {"120px",20,true,false,false,false},
        {"120px",24,true,false,false,false}, {"auto",24,true,true,false,false},
        {"auto",24,true,true,true,false}, {"auto",20,false,false,false,true},
        {"auto",20,false,false,false,false}};
    for (const char* initial_mode : {"horizontal-tb","vertical-lr","vertical-rl"})
    for (const char* direction : {"row","column"})
    for (const char* kind : {"range","button","text","textarea","select"})
    for (bool decorated : {false,true}) {
        SCOPED_TRACE(initial_mode);
        SCOPED_TRACE(direction);
        SCOPED_TRACE(kind);
        SCOPED_TRACE(decorated);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}.host{position:absolute;left:0;top:0;width:500px;height:500px}"
            "#host{display:flex;align-items:flex-start;flex-direction:%s}#ordinary{display:flow-root}"
            "#child,#control{font:20px/20px Ahem;writing-mode:%s;%s}</style><div id=result>",direction,initial_mode,
            decorated ? "box-sizing:content-box;padding:3px 7px 5px 11px;border:2px solid" : "");
        for (const char* id : {"child","control"}) {
            strbuf_append_format(source,"<div id=%s class=host>",strcmp(id,"child") == 0 ? "host" : "ordinary");
            if (strcmp(kind,"textarea") == 0) strbuf_append_format(source,"<textarea id=%s cols=5 rows=2>XX</textarea>",id);
            else if (strcmp(kind,"select") == 0) strbuf_append_format(source,"<select id=%s><option>XX</option></select>",id);
            else strbuf_append_format(source,"<input id=%s type=%s size=5 value=XXX>",id,kind);
            strbuf_append_str(source,"</div>");
        }
        strbuf_append_str(source,"</div>");
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        DomElement* control = dom_find_element_by_id(result,"control");
        ASSERT_NE(child,nullptr); ASSERT_NE(control,nullptr);
        for (const State& state : states) {
            SCOPED_TRACE(state.inline_size);
            SCOPED_TRACE(state.font);
            SCOPED_TRACE(state.longer);
            SCOPED_TRACE(state.switch_mode);
            SCOPED_TRACE(state.rtl);
            SCOPED_TRACE(state.physical);
            const char* mode = state.switch_mode
                ? (strcmp(initial_mode,"horizontal-tb") == 0 ? "vertical-lr" : "horizontal-tb") : initial_mode;
            char font[48]; str_fmt(font,sizeof(font),"%gpx/%gpx Ahem",state.font,state.font);
            for (DomElement* element : {child,control}) {
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"font",font));
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"writing-mode",mode));
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"direction",state.rtl ? "rtl" : "ltr"));
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"inline-size",state.inline_size));
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"width",state.physical ? "71px" : ""));
                ASSERT_TRUE(dom_cssom_set_inline_property(element,"height",state.physical ? "37px" : ""));
                if (strcmp(kind,"button") == 0 || strcmp(kind,"text") == 0)
                    ASSERT_TRUE(element->set_attribute("value",state.longer ? "XXXX\nXX" : "XXX"));
                else if (strcmp(kind,"select") == 0)
                    ASSERT_TRUE(dom_text_set_content(element->first_child->as_element()->first_child->as_text(),state.longer ? "XXXX" : "XX"));
            }
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // an unshrunk, unstretched native item uses the same formatter as its ordinary-flow control.
            EXPECT_FLOAT_EQ(child->width,control->width);
            EXPECT_FLOAT_EQ(child->height,control->height);
            EXPECT_GT(strcmp(mode,"horizontal-tb") == 0 ? control->height : control->width,0.0f);
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantFormControls, NativeFlexAutomaticMinimumUsesNaturalContentBeforeAuthoredMainSize) {
    for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl"})
    for (const char* direction : {"row","column"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(direction);
        const char* main_property = strcmp(direction,"row") == 0 ? "inline-size" : "block-size";
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flex;writing-mode:%s;flex-direction:%s;align-items:flex-start;%s:80px}"
            "#child{font:20px/20px Ahem;box-sizing:border-box;%s:150px}</style>"
            "<div id=result><input id=child type=button value=XXX></div>",mode,direction,main_property,main_property);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        for (const char* available : {"80px","90px","80px"}) {
            ASSERT_TRUE(dom_cssom_set_inline_property(result,main_property,available));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool main_horizontal = (strcmp(mode,"horizontal-tb") == 0) == (strcmp(direction,"row") == 0);
            // natural content is below both allocations; the authored 150px basis can shrink to either.
            EXPECT_FLOAT_EQ(main_horizontal ? child->width : child->height,strcmp(available,"90px") == 0 ? 90.0f : 80.0f);
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantFormControls, GridRangeIntrinsicTracksFollowCurrentPhysicalContentMetrics) {
    for (bool decorated : {false,true}) {
        SCOPED_TRACE(decorated);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{position:absolute;left:0;top:0;display:inline-grid;"
            "grid-template-columns:max-content;align-items:start;justify-items:start}#child{margin:0;%s}"
            "</style><div id=result><input id=child type=range></div>",
            decorated ? "box-sizing:content-box;padding:3px 7px 5px 11px;border:2px solid" : "");
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl","sideways-lr","sideways-rl","horizontal-tb"})
        for (const char* inline_size : {"auto","0px","120px","auto"}) {
            SCOPED_TRACE(mode);
            SCOPED_TRACE(inline_size);
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"writing-mode",mode));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"inline-size",inline_size));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // intrinsic tracks consume the native box's current axes, including collapsed inline sizes.
            EXPECT_FLOAT_EQ(result->width,child->width);
            EXPECT_FLOAT_EQ(result->height,child->height);
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantFormControls, PercentageHeightMeasurementsKeepContainingBlockDefiniteness) {
    for (bool nested : {false,true}) for (bool decorated : {false,true}) {
        SCOPED_TRACE(nested);
        SCOPED_TRACE(decorated);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{display:inline-block;height:30px;border:1px solid}"
            ".inner{display:inline-block;border:1px solid}#child{margin:2px;min-height:min-content;%s}"
            "</style><div id=result>%s<input id=child type=range>%s</div>",
            decorated ? "box-sizing:content-box;padding:3px 7px 5px 11px;border:2px solid" : "",
            nested ? "<div class=inner>" : "",nested ? "</div>" : "");
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl","horizontal-tb"})
        for (bool preferred : {true,false})
        for (float percent : {50.0f,25.0f,50.0f}) {
            SCOPED_TRACE(mode);
            SCOPED_TRACE(preferred);
            SCOPED_TRACE(percent);
            char height[32]; str_fmt(height,sizeof(height),"%g%%",percent);
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"writing-mode",mode));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"height",preferred ? height : ""));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"max-height",preferred ? "" : height));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            // cyclic block percentages behave as auto; orthogonal inline percentages retain the viewport fallback.
            float content_height = !nested ? 30.0f * percent / 100.0f
                : vertical ? 800.0f * percent / 100.0f : FormDefaults::RANGE_HEIGHT;
            if (!preferred) content_height = vertical ? FormDefaults::RANGE_WIDTH : FormDefaults::RANGE_HEIGHT;
            EXPECT_FLOAT_EQ(child->height,content_height+(decorated ? 12.0f : 0.0f));
            EXPECT_FLOAT_EQ(child->width,(vertical ? FormDefaults::RANGE_HEIGHT : FormDefaults::RANGE_WIDTH)
                +(decorated ? 22.0f : 0.0f));
            uint64_t geometry = retained_subtree_geometry(child);
            FlexFlowTestContext flow(&ui,document,child);
            IntrinsicSize measured = layout_measure_form_control(
                &flow.layout,lam::view_as_block(child),AvailableSpace::make_indefinite());
            EXPECT_FLOAT_EQ(measured.max_height,content_height);
            EXPECT_EQ(retained_subtree_geometry(child),geometry);
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantFormControls, ForcedButtonLinesRetainTheirOwnPhysicalColumnsAndInlineAlignment) {
    const struct State {
        const char* line; float advance; unsigned long_units; unsigned short_units;
        bool fixed; bool switch_mode; bool rtl; const char* align;
    } states[] = {
        {"normal",20,3,1,false,false,false,"center"},
        {"normal",20,4,2,false,false,false,"center"},
        {"normal",20,3,1,false,false,false,"center"},
        {"14px",14,3,1,false,false,false,"center"},
        {"32px",32,3,1,true,false,false,"center"},
        {"32px",32,3,1,true,true,false,"center"},
        {"normal",20,3,1,true,true,true,"center"},
        {"normal",20,3,1,true,true,true,"end"},
        {"normal",20,3,1,true,true,true,"start"},
        {"normal",20,3,1,false,false,false,"center"}};
    for (const char* initial_mode : {"horizontal-tb","vertical-lr","vertical-rl"})
    for (bool br : {false,true}) for (bool physical_padding : {false,true}) {
        SCOPED_TRACE(initial_mode);
        SCOPED_TRACE(br);
        SCOPED_TRACE(physical_padding);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{position:absolute;left:0;top:0;width:500px;height:400px}"
            "#child{appearance:none;box-sizing:content-box;font:20px/normal Ahem;writing-mode:%s;"
            "white-space:%s;%s}</style><div id=result><button id=child>%s</button></div>",
            initial_mode,br ? "normal" : "pre",
            physical_padding ? "padding:3px 7px 5px 11px;border:2px solid" : "",
            br ? "XXX<br>X" : "XXX\nX");
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        DomText* first_text = child->first_child->as_text();
        DomText* last_text = br ? child->first_child->next_sibling->next_sibling->as_text() : first_text;
        for (const State& state : states) {
            SCOPED_TRACE(state.line);
            SCOPED_TRACE(state.fixed);
            SCOPED_TRACE(state.switch_mode);
            SCOPED_TRACE(state.rtl);
            SCOPED_TRACE(state.align);
            const char* mode = state.switch_mode
                ? (strcmp(initial_mode,"horizontal-tb") == 0 ? "vertical-lr" : "horizontal-tb") : initial_mode;
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"writing-mode",mode));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"line-height",state.line));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"direction",state.rtl ? "rtl" : "ltr"));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"text-align",state.align));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"inline-size",state.fixed ? "120px" : "auto"));
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"block-size",state.fixed ? "100px" : "auto"));
            bool longer = state.long_units == 4;
            ASSERT_TRUE(dom_text_set_content(first_text,br ? (longer ? "XXXX" : "XXX")
                : (longer ? "XXXX\nXX" : "XXX\nX")));
            if (br) ASSERT_TRUE(dom_text_set_content(last_text,longer ? "XX" : "X"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            TextRect* first = first_text->rect;
            TextRect* last = br ? last_text->rect.get() : first ? first->next : nullptr;
            ASSERT_NE(first,nullptr); ASSERT_NE(last,nullptr); EXPECT_EQ(last->next,nullptr);
            // Ahem has a 20px cell; each forced line advances by its used line-height.
            float inline_extent = state.fixed ? 120.0f : state.long_units*20.0f;
            float natural = 2.0f*state.advance;
            float block_extent = state.fixed ? 100.0f : natural;
            float half_leading = (state.advance-20.0f)/2.0f;
            float center = max((block_extent-natural)/2.0f,0.0f);
            float block_start = physical_padding ? (vertical ? (strcmp(mode,"vertical-rl") == 0 ? 9.0f : 13.0f) : 5.0f) : 3.0f;
            float inline_start = physical_padding ? (vertical ? 5.0f : 13.0f) : 8.0f;
            EXPECT_FLOAT_EQ(child->width,(vertical ? block_extent : inline_extent) +
                (physical_padding ? 22.0f : vertical ? 6.0f : 16.0f));
            EXPECT_FLOAT_EQ(child->height,(vertical ? inline_extent : block_extent) +
                (physical_padding ? 12.0f : vertical ? 16.0f : 6.0f));
            const struct Line { TextRect* rect; unsigned units; unsigned index; } lines[] = {
                {first,state.long_units,0},{last,state.short_units,1}};
            for (const Line& line : lines) {
                float advance = line.units*20.0f;
                float remaining = inline_extent-advance;
                bool align_end = strcmp(state.align,"end") == 0;
                float inline_offset = strcmp(state.align,"center") == 0 ? remaining/2.0f
                    : (align_end != state.rtl ? remaining : 0.0f);
                float block_offset = block_start+center+line.index*state.advance+half_leading;
                float x = vertical ? (strcmp(mode,"vertical-rl") == 0
                    ? child->width-block_offset-20.0f : block_offset) : inline_start+inline_offset;
                float y = vertical ? inline_start+inline_offset : block_offset;
                EXPECT_FLOAT_EQ(line.rect->x,x); EXPECT_FLOAT_EQ(line.rect->y,y);
                EXPECT_FLOAT_EQ(line.rect->width,vertical ? 20.0f : advance);
                EXPECT_FLOAT_EQ(line.rect->height,vertical ? advance : 20.0f);
            }
        }
        ui.document = nullptr; free_document(document); document = nullptr;
    }
}

TEST_F(RadiantFormControls, OrthogonalButtonsExportTheirAnonymousContentBaseline) {
    for (bool vertical_parent : {false,true}) for (bool fixed_inline : {false,true,false})
    for (const char* kind : {"input","button","span"}) {
        SCOPED_TRACE(vertical_parent);
        SCOPED_TRACE(fixed_inline);
        SCOPED_TRACE(kind);
        const char* parent_mode = vertical_parent ? "vertical-lr" : "horizontal-tb";
        const char* child_mode = vertical_parent ? "horizontal-tb" : "vertical-lr";
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{writing-mode:%s;font:20px/normal Ahem;width:600px;height:400px}"
            "#child{display:inline-block;appearance:none;writing-mode:%s;font:20px/normal Ahem;"
            "box-sizing:content-box;padding:3px 7px 5px 11px;border:2px solid;%s}"
            "#marker{display:inline-block;width:0;height:0}</style><div id=result>",parent_mode,child_mode,
            fixed_inline ? (vertical_parent ? "width:200px" : "height:200px") : "");
        if (strcmp(kind,"input") == 0) strbuf_append_str(source,"<input id=child type=button value=XXX>");
        else strbuf_append_format(source,"<%s id=child>XXX</%s>",kind,kind);
        strbuf_append_str(source,"<span id=marker></span></div>");
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        DomElement* marker = dom_find_element_by_id(result,"marker");
        ASSERT_NE(child,nullptr); ASSERT_NE(marker,nullptr);
        bool button = strcmp(kind,"span") != 0;
        // the button's anonymous content supplies a baseline; ordinary atoms synthesize from margins.
        float expected = vertical_parent
            ? (button ? 13.0f + (child->width-22.0f)/2.0f : child->width/2.0f)
            : child->height - (button ? 7.0f : 0.0f);
        EXPECT_FLOAT_EQ(vertical_parent ? marker->x-child->x : marker->y-child->y,expected);
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantFormControls, ButtonBaselinesRetainLineProgressionLeadingAndContentCentering) {
    const struct State {
        const char* line; float advance; unsigned lines; bool fixed; const char* source;
        bool switch_mode = false; bool rtl = false;
        float font_size = 20.0f;
    } states[] = {
        {"normal",20,1,false,"auto"}, {"normal",20,2,false,"auto"},
        {"normal",24,2,false,"auto",false,false,24}, {"normal",20,2,false,"auto"},
        {"14px",14,2,false,"auto"}, {"0px",0,2,false,"auto"},
        {"32px",32,1,true,"auto"}, {"32px",32,2,true,"auto",true},
        {"32px",32,2,true,"auto",true,true}, {"normal",20,2,true,"first",true,true},
        {"normal",20,2,true,"first",false,true}, {"normal",20,2,false,"last"},
        {"normal",20,1,false,"auto"}};
    for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl"})
    for (const char* type : {"button","submit","reset"})
    for (bool physical_padding : {false,true}) for (float marker_width : {0.0f,1.0f}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(type);
        SCOPED_TRACE(physical_padding);
        SCOPED_TRACE(marker_width);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{position:relative;writing-mode:%s;width:600px;height:600px}"
            ".row{position:absolute;left:0;top:0;width:500px;height:400px;font:20px/normal Ahem}"
            "#child,#control{appearance:none;box-sizing:content-box;font:20px/normal Ahem;%s}"
            "#control{white-space:pre}.marker{display:inline-block;width:%gpx;height:0}</style>"
            "<div id=result><div class=row><input id=child type=%s value=XXX><span id=marker class=marker></span></div>"
            "<div class=row><button id=control>XXX</button><span id=controlmarker class=marker></span></div></div>",
            mode,physical_padding ? "padding:3px 7px 5px 11px;border:2px solid" : "",marker_width,type);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        DomElement* control = dom_find_element_by_id(result,"control");
        DomElement* marker = dom_find_element_by_id(result,"marker");
        DomElement* controlmarker = dom_find_element_by_id(result,"controlmarker");
        ASSERT_NE(child,nullptr); ASSERT_NE(control,nullptr);
        ASSERT_NE(marker,nullptr); ASSERT_NE(controlmarker,nullptr);
        const char* previous_line = "normal";
        const char* previous_source = "auto";
        const char* previous_mode = mode;
        bool previous_fixed = false;
        bool previous_rtl = false;
        float previous_font_size = 20.0f;
        for (const State& state : states) {
            SCOPED_TRACE(state.line);
            SCOPED_TRACE(state.lines);
            SCOPED_TRACE(state.fixed);
            SCOPED_TRACE(state.source);
            char font[64];
            str_fmt(font,sizeof(font),"%gpx/%s Ahem",state.font_size,state.line);
            const char* used_mode = state.switch_mode
                ? (strcmp(mode,"horizontal-tb") == 0 ? "vertical-lr" : "horizontal-tb") : mode;
            bool changed_mode = strcmp(used_mode,previous_mode) != 0;
            bool vertical = strcmp(used_mode,"horizontal-tb") != 0;
            if (changed_mode) ASSERT_TRUE(dom_cssom_set_inline_property(result,"writing-mode",used_mode));
            if (state.rtl != previous_rtl)
                ASSERT_TRUE(dom_cssom_set_inline_property(result,"direction",state.rtl ? "rtl" : "ltr"));
            for (DomElement* box : {child,control}) {
                if (strcmp(state.line,previous_line) != 0 || state.font_size != previous_font_size)
                    ASSERT_TRUE(dom_cssom_set_inline_property(box,"font",font));
                if (state.fixed != previous_fixed || changed_mode) {
                    ASSERT_TRUE(dom_cssom_set_inline_property(box,"width",state.fixed && vertical ? "100px" : "auto"));
                    ASSERT_TRUE(dom_cssom_set_inline_property(box,"height",state.fixed && !vertical ? "100px" : "auto"));
                }
                if (strcmp(state.source,previous_source) != 0)
                    ASSERT_TRUE(dom_cssom_set_inline_property(box,"baseline-source",state.source));
            }
            const char* label = state.lines == 2 ? "XXX\nX" : "XXX";
            ASSERT_TRUE(child->set_attribute("value",label));
            ASSERT_TRUE(dom_text_set_content(control->first_child->as_text(),label));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            // Ahem's .8em ascent rounds to font pixels; vertical central baselines bisect these even cells.
            float line_baseline = state.advance / 2.0f +
                (vertical ? 0.0f : roundf(.8f*state.font_size)-state.font_size/2.0f);
            float natural = state.advance * state.lines;
            float centered = state.fixed ? (100.0f-natural)/2.0f : 0.0f;
            float start = physical_padding ? (vertical ? (strcmp(used_mode,"vertical-rl") == 0 ? 9.0f : 13.0f) : 5.0f) : 3.0f;
            bool first = strcmp(state.source,"first") == 0;
            float baseline = start + centered + (first ? 0.0f : (state.lines-1)*state.advance) + line_baseline;
            const struct Pair { DomElement* box; DomElement* marker; } pairs[] = {{child,marker},{control,controlmarker}};
            for (const Pair& pair : pairs) {
                float expected = vertical && strcmp(used_mode,"vertical-rl") == 0 ? pair.box->width-baseline : baseline;
                if (vertical) expected -= marker_width/2.0f;
                EXPECT_FLOAT_EQ(vertical ? pair.marker->x-pair.box->x : pair.marker->y-pair.box->y,expected);
            }
            previous_line = state.line; previous_source = state.source; previous_fixed = state.fixed;
            previous_mode = used_mode; previous_rtl = state.rtl;
            previous_font_size = state.font_size;
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantFormControls, ButtonNormalLineHeightRetainsSelectedAndFallbackFontMetrics) {
    const struct State {
        const char* family; float size; const char* line; const char* label; bool switch_mode;
    } states[] = {
        {"Ahem",12,"normal","XXX",false}, {"Ahem",20,"normal","XXX",false},
        {"Ahem",20,"normal","あいなき",false}, {"Ahem",20,"normal","XXX",false},
        {"Ahem",24,"normal","XXX\nX",false}, {"Arial",13.333333f,"normal","あいなき",false},
        {"Arial",20,"normal","XXX\nあいなき",false}, {"Arial",20,"normal","あいなき\nXXX",false},
        {"Arial",24,"normal","あいなき\nXXX",true}, {"Arial",24,"0px","あいなき\nXXX",true},
        {"Ahem",24,"14px","XXX\nX",true}, {"Arial",20,"32px","XXX\nあいなき",false},
        {"Arial",20,"normal","XXX\nX",false}, {"Ahem",12,"normal","",false},
        {"Ahem",12,"normal","XXX",false}};
    for (const char* initial_mode : {"horizontal-tb","vertical-lr","vertical-rl"})
    for (const char* type : {"button","submit","reset"}) for (bool physical_padding : {false,true}) {
        SCOPED_TRACE(initial_mode);
        SCOPED_TRACE(type);
        SCOPED_TRACE(physical_padding);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flow-root;writing-mode:%s}"
            "#child,#control{appearance:none;font:12px/normal Ahem;%s}#control{white-space:pre}</style>"
            "<div id=result><input id=child type=%s value=XXX><button id=control>XXX</button></div>",
            initial_mode,physical_padding ? "box-sizing:content-box;padding:3px 7px 5px 11px;border:2px solid" : "",type);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        DomElement* control = dom_find_element_by_id(result,"control");
        ASSERT_NE(child,nullptr); ASSERT_NE(control,nullptr);
        ASSERT_NE(control->first_child,nullptr);
        const char* previous_mode = initial_mode;
        const char* previous_family = "Ahem";
        const char* previous_line = "normal";
        float previous_size = 12;
        for (const State& state : states) {
            SCOPED_TRACE(state.family);
            SCOPED_TRACE(state.size);
            SCOPED_TRACE(state.line);
            SCOPED_TRACE(state.label);
            const char* mode = state.switch_mode
                ? (strcmp(initial_mode,"horizontal-tb") == 0 ? "vertical-lr" : "horizontal-tb") : initial_mode;
            if (strcmp(mode,previous_mode) != 0)
                ASSERT_TRUE(dom_cssom_set_inline_property(result,"writing-mode",mode));
            if (strcmp(state.family,previous_family) != 0 || state.size != previous_size ||
                strcmp(state.line,previous_line) != 0) {
                char font[96];
                str_fmt(font,sizeof(font),"%gpx/%s %s",state.size,state.line,state.family);
                ASSERT_TRUE(dom_cssom_set_inline_property(child,"font",font));
                ASSERT_TRUE(dom_cssom_set_inline_property(control,"font",font));
            }
            ASSERT_TRUE(child->set_attribute("value",state.label));
            // an empty native label retains one primary strut; empty HTML button flow has none.
            ASSERT_TRUE(dom_text_set_content(control->first_child->as_text(),*state.label ? state.label : "X"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            EXPECT_FLOAT_EQ(vertical ? child->width : child->height,
                vertical ? control->width : control->height);
            previous_mode = mode; previous_family = state.family;
            previous_size = state.size; previous_line = state.line;
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantFormControls, ButtonForcedLinesRetainTheirLogicalIntrinsicAxes) {
    const struct State { float font; float line; bool multiline; bool switch_mode; } states[] = {
        {20,20,false,false}, {20,20,true,false}, {24,28,true,true}, {20,20,false,false}};
    for (const char* initial_mode : {"horizontal-tb","vertical-lr","vertical-rl"})
    for (const char* type : {"button","submit","reset"}) for (bool physical_padding : {false,true}) {
        SCOPED_TRACE(initial_mode);
        SCOPED_TRACE(type);
        SCOPED_TRACE(physical_padding);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flow-root;writing-mode:%s}"
            "#child{appearance:none;font:20px/20px Ahem;%s}</style>"
            "<div id=result><input id=child type=%s value=XXX></div>",initial_mode,
            physical_padding ? "box-sizing:content-box;padding:3px 7px 5px 11px;border:2px solid" : "",type);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        float previous_font = 20, previous_line = 20;
        const char* previous_mode = initial_mode;
        for (const State& state : states) {
            SCOPED_TRACE(state.font);
            SCOPED_TRACE(state.multiline);
            const char* mode = state.switch_mode
                ? (strcmp(initial_mode,"horizontal-tb") == 0 ? "vertical-lr" : "horizontal-tb") : initial_mode;
            if (strcmp(mode,previous_mode) != 0)
                ASSERT_TRUE(dom_cssom_set_inline_property(result,"writing-mode",mode));
            if (state.font != previous_font || state.line != previous_line) {
                char font[64];
                str_fmt(font,sizeof(font),"%gpx/%gpx Ahem",state.font,state.line);
                ASSERT_TRUE(dom_cssom_set_inline_property(child,"font",font));
            }
            ASSERT_TRUE(child->set_attribute("value",state.multiline ? "XXX\nX" : "XXX"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            float inline_extent = 3 * state.font;
            float block_extent = state.line * (state.multiline ? 2 : 1);
            // forced lines keep their maximum inline advance and add block advances before physical projection.
            EXPECT_FLOAT_EQ(child->width,(vertical ? block_extent : inline_extent) +
                (physical_padding ? 22 : vertical ? 6 : 16));
            EXPECT_FLOAT_EQ(child->height,(vertical ? inline_extent : block_extent) +
                (physical_padding ? 12 : vertical ? 16 : 6));
            String* whitespace = css_prop_serialize_computed_value(
                document->document_pool,child,CSS_PROPERTY_WHITE_SPACE,0);
            ASSERT_NE(whitespace,nullptr);
            EXPECT_STREQ(whitespace->chars,"pre");
            previous_font = state.font; previous_line = state.line; previous_mode = mode;
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantFormControls, ButtonWhitespaceCascadeAndTerminalLinesRemainLive) {
    const struct Label { const char* value; float preserved_units; unsigned lines; float collapsed_units; } labels[] = {
        {"",0,1,0}, {"XXX",3,1,3}, {"XXX\nX",3,2,5}, {"XXX\n",3,1,3},
        {"\nXXX",3,2,3}, {"XXX\n\nX",3,3,5}, {"\n",0,1,0}};
    for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl"}) {
        SCOPED_TRACE(mode);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flow-root;white-space:normal;writing-mode:%s}"
            "#child{appearance:none;font:20px/20px Ahem}</style>"
            "<div id=result><input id=child type=button value=XXX></div>",mode);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        for (const char* ws : {"","normal","pre","initial","inherit","unset","revert",""}) {
            SCOPED_TRACE(ws);
            ASSERT_TRUE(dom_cssom_set_inline_property(child,"white-space",ws));
            bool preserve = !*ws || strcmp(ws,"pre") == 0 || strcmp(ws,"revert") == 0;
            for (const Label& label : labels) {
                SCOPED_TRACE(label.value);
                ASSERT_TRUE(child->set_attribute("value",label.value));
                ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                float inline_extent = 20 * (preserve ? label.preserved_units : label.collapsed_units) + 16;
                float block_extent = 20 * (preserve ? label.lines : 1) + 6;
                bool vertical = strcmp(mode,"horizontal-tb") != 0;
                EXPECT_FLOAT_EQ(child->width,vertical ? block_extent : inline_extent);
                EXPECT_FLOAT_EQ(child->height,vertical ? inline_extent : block_extent);
                String* whitespace = css_prop_serialize_computed_value(
                    document->document_pool,child,CSS_PROPERTY_WHITE_SPACE,0);
                ASSERT_NE(whitespace,nullptr);
                EXPECT_STREQ(whitespace->chars,preserve ? "pre" : "normal");
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantFormControls, ReplacedRootKeepsItsMeasuredNaturalAndPhysicalSizes) {
    DomElement* root = load_script_source(
        "<!doctype html><script>document.addEventListener('DOMContentLoaded',()=>{"
        "const select=document.createElement('select');select.id='result';"
        "document.documentElement.replaceWith(select)});</script>");
    ASSERT_NE(root,nullptr);
    ASSERT_EQ(static_cast<DomNode*>(root),document->root.get());
    ui.viewport_width = ui.window_width = 1200;
    ui.viewport_height = ui.window_height = 800;
    for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl","horizontal-tb"}) {
        SCOPED_TRACE(mode);
        ASSERT_TRUE(dom_cssom_set_inline_property(root,"writing-mode",mode));
        for (bool physical : {false,true,false}) {
            SCOPED_TRACE(physical);
            ASSERT_TRUE(dom_cssom_set_inline_property(root,"box-sizing","border-box"));
            ASSERT_TRUE(dom_cssom_set_inline_property(root,"width",physical ? "71px" : "auto"));
            ASSERT_TRUE(dom_cssom_set_inline_property(root,"height",physical ? "37px" : "auto"));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool vertical = strcmp(mode,"horizontal-tb") != 0;
            float width = physical ? 71 : vertical ? 19 : 22;
            EXPECT_FLOAT_EQ(root->width,width);
            EXPECT_FLOAT_EQ(root->height,physical ? 37 : vertical ? 22 : 19);
            EXPECT_FLOAT_EQ(root->x,strcmp(mode,"vertical-rl") == 0 ? 1200-width : 0);
            EXPECT_FLOAT_EQ(root->y,0);
        }
    }
}

TEST_F(RadiantFormControls, MeterAndProgressDefaultsFollowLogicalAxesAndRetainedCascade) {
    const struct SizeState {
        const char* css;
        float width, height, inline_size, block_size;
        bool automatic;
    } sizes[] = {
        {"", -1, -1, -1, -1, false},
        {"width:71px", 71, -1, -1, -1, false},
        {"height:37px", -1, 37, -1, -1, false},
        {"width:71px;height:37px", 71, 37, -1, -1, false},
        {"inline-size:73px", -1, -1, 73, -1, false},
        {"block-size:29px", -1, -1, -1, 29, false},
        {"inline-size:73px;block-size:29px", -1, -1, 73, 29, false},
        {"width:auto;height:auto", -1, -1, -1, -1, true},
        {"", -1, -1, -1, -1, false}
    };
    for (const char* tag : {"meter", "progress"}) {
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{font:20px Arial}"
            "#child{position:absolute;left:0;top:0}</style>"
            "<div id=result><%s id=child value=.7 max=1></%s></div>", tag, tag);
        DomElement* root = layout_script_source(source, 1200, 800);
        ASSERT_NE(root, nullptr);
        DomElement* child = dom_find_element_by_id(root, "child");
        ASSERT_NE(child, nullptr);
        for (const char* appearance : {"auto", "none"})
        for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl", "horizontal-tb"})
        for (float font : {20.0f, 32.0f, 20.0f})
        for (const SizeState& size : sizes) {
            SCOPED_TRACE(tag);
            SCOPED_TRACE(appearance);
            SCOPED_TRACE(mode);
            SCOPED_TRACE(font);
            SCOPED_TRACE(size.css);
            StrBuf* style = strbuf_new();
            strbuf_append_format(style, "font:%gpx Arial", font);
            ASSERT_TRUE(root->set_attribute("style", style->str));
            strbuf_reset(style);
            strbuf_append_format(style, "writing-mode:%s;appearance:%s;%s",
                mode, appearance, size.css);
            ASSERT_TRUE(child->set_attribute("style", style->str));
            strbuf_free(style);
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool vertical = strcmp(mode, "horizontal-tb") != 0;
            float inline_size = font * (strcmp(tag, "meter") == 0 ? 5.0f : 10.0f);
            float block_size = font;
            if (size.inline_size >= 0) inline_size = size.inline_size;
            if (size.block_size >= 0) block_size = size.block_size;
            float width = vertical ? block_size : inline_size;
            float height = vertical ? inline_size : block_size;
            if (size.width >= 0) width = size.width;
            if (size.height >= 0) height = size.height;
            // authored auto suppresses the logical UA lengths; this empty absolute widget has no natural size.
            if (size.automatic) width = height = 0;
            EXPECT_FLOAT_EQ(child->width, width);
            EXPECT_FLOAT_EQ(child->height, height);
            EXPECT_FLOAT_EQ(child->font->font_size, font);
            EXPECT_FLOAT_EQ(child->inl()->vertical_align_offset, -0.2f * font);
            uint64_t geometry = retained_subtree_geometry(child);
            FlexFlowTestContext flow(&ui, document, child);
            IntrinsicSize measured = layout_measure_replaced(
                &flow.layout, lam::view_as_block(child), flow.layout.available_space);
            EXPECT_FLOAT_EQ(measured.max_width, width);
            EXPECT_FLOAT_EQ(measured.max_height, height);
            layout_measure_intrinsic_widths(&flow.layout, child, true);
            EXPECT_EQ(retained_subtree_geometry(child), geometry);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantFormControls, WidgetIntrinsicFactsRetainCurrentContentAxesAndBoxSizing) {
    for (const char* tag : {"meter", "progress"}) {
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>body{margin:0}#result{position:absolute;left:0;top:0;"
            "font:20px Arial;padding:3px;border:2px solid}</style>"
            "<%s id=result value=.7 max=1></%s>", tag, tag);
        DomElement* child = layout_script_source(source, 1200, 800);
        ASSERT_NE(child, nullptr);
        for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl"})
        for (const char* box : {"border-box", "content-box"})
        for (const char* size : {"", "width:71px;height:37px", "width:auto;height:auto", ""}) {
            SCOPED_TRACE(tag);
            SCOPED_TRACE(mode);
            SCOPED_TRACE(box);
            SCOPED_TRACE(size);
            StrBuf* style = strbuf_new();
            strbuf_append_format(style, "writing-mode:%s;box-sizing:%s;%s", mode, box, size);
            ASSERT_TRUE(child->set_attribute("style", style->str));
            strbuf_free(style);
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            bool vertical = strcmp(mode, "horizontal-tb") != 0;
            bool border_box = strcmp(box, "border-box") == 0;
            bool fixed = strcmp(size, "width:71px;height:37px") == 0;
            bool automatic = strcmp(size, "width:auto;height:auto") == 0;
            float inline_size = strcmp(tag, "meter") == 0 ? 100 : 200;
            float width = fixed ? 71 : vertical ? 20 : inline_size;
            float height = fixed ? 37 : vertical ? inline_size : 20;
            if (automatic) width = height = 10;
            else if (!border_box) { width += 10; height += 10; }
            EXPECT_FLOAT_EQ(child->width, width);
            EXPECT_FLOAT_EQ(child->height, height);
            uint64_t geometry = retained_subtree_geometry(child);
            FlexFlowTestContext flow(&ui, document, child);
            ReplacedIntrinsicFacts facts = layout_replaced_intrinsic_facts(
                &flow.layout, lam::view_as_block(child));
            EXPECT_FLOAT_EQ(facts.width, width - 10);
            EXPECT_FLOAT_EQ(facts.height, height - 10);
            EXPECT_FALSE(facts.has_natural_width);
            EXPECT_FALSE(facts.has_natural_height);
            EXPECT_FALSE(facts.has_natural_aspect_ratio);
            EXPECT_EQ(retained_subtree_geometry(child), geometry);
        }
        // authored alignment and removal must restore the em UA offset from the selected font.
        for (const char* alignment : {"7px", "baseline", ""}) {
            ASSERT_TRUE(dom_cssom_set_inline_property(child, "vertical-align", alignment));
            ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
            EXPECT_FLOAT_EQ(child->inl()->vertical_align_offset,
                strcmp(alignment, "7px") == 0 ? 7 : *alignment ? 0 : -4);
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantFormControls, PrimitiveSelectKeepsCompleteLabelsAndCurrentLogicalBoxEdges) {
    StrBuf* source = strbuf_new();
    strbuf_append_str(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
        "body{margin:0}#result{position:absolute;left:0;top:0}</style>"
        "<select id=result><option id=option>X</option></select>");
    DomElement* child = layout_script_source(source, 1200, 800);
    ASSERT_NE(child, nullptr);
    DomElement* option = dom_find_element_by_id(child, "option");
    ASSERT_NE(option, nullptr);
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl"})
    for (const char* appearance : {"auto", "none", "auto"})
    for (float font : {20.0f, 24.0f, 1.0f, 20.0f})
    for (const char* label : {"X", "XX XX", "XXXXX", "XX XX"})
    for (const char* edges : {"", "padding:0;border:0", "padding:3px 5px;border:2px solid"})
    for (const char* size : {"", "width:71px;height:37px", "inline-size:73px;block-size:29px"}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(appearance);
        SCOPED_TRACE(font);
        SCOPED_TRACE(label);
        SCOPED_TRACE(edges);
        SCOPED_TRACE(size);
        StrBuf* style = strbuf_new();
        strbuf_append_format(style, "font:%gpx Ahem;writing-mode:%s;appearance:%s;%s;%s",
            font, mode, appearance, edges, size);
        ASSERT_TRUE(child->set_attribute("style", style->str));
        strbuf_free(style);
        ASSERT_TRUE(option->set_attribute("label", label));
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        EXPECT_STREQ(dom_option_text(option), label);
        bool vertical = strcmp(mode, "horizontal-tb") != 0;
        float decoration = !*edges ? 2 : strcmp(edges, "padding:0;border:0") == 0
            ? 0 : vertical ? 10 : 14;
        float inline_size = strlen(label) * font + decoration;
        if (strcmp(appearance, "auto") == 0) inline_size += 20;
        if (strcmp(size, "width:71px;height:37px") == 0) inline_size = vertical ? 37 : 71;
        if (strcmp(size, "inline-size:73px;block-size:29px") == 0) inline_size = 73;
        EXPECT_FLOAT_EQ(vertical ? child->height : child->width, inline_size);
        if (*size) {
            float block_size = strcmp(size, "width:71px;height:37px") == 0
                ? vertical ? 71 : 37 : 29;
            EXPECT_FLOAT_EQ(vertical ? child->width : child->height, block_size);
        } else {
            float block_decoration = !*edges ? 2 : strcmp(edges, "padding:0;border:0") == 0
                ? 0 : vertical ? 14 : 10;
            // independent font-ready captures separate the native inner padding from the font cell and CSS edges.
            float block_size = font + block_decoration + (strcmp(appearance, "auto") == 0 ? 2 : 0);
            EXPECT_FLOAT_EQ(vertical ? child->width : child->height, block_size);
        }
        uint64_t geometry = retained_subtree_geometry(child);
        FlexFlowTestContext flow(&ui, document, child);
        layout_measure_intrinsic_widths(&flow.layout, child, true);
        layout_measure_form_control(&flow.layout, lam::view_as_block(child), flow.layout.available_space);
        EXPECT_EQ(retained_subtree_geometry(child), geometry);
    }
    for (const char* mode : {"horizontal-tb", "vertical-lr", "vertical-rl"})
    for (const char* appearance : {"auto", "none", "auto"}) {
        StrBuf* style = strbuf_new();
        strbuf_append_format(style, "writing-mode:%s;appearance:%s", mode, appearance);
        ASSERT_TRUE(child->set_attribute("style", style->str));
        strbuf_free(style);
        ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
        float block_size = strcmp(appearance, "auto") == 0 ? 19 : 17;
        EXPECT_FLOAT_EQ(strcmp(mode, "horizontal-tb") == 0 ? child->height : child->width, block_size);
    }
}

TEST_F(RadiantFormControls, NativeIntrinsicAxesFollowWritingModeAndRetainedPhysicalSizes) {
    const char* controls[] = {
        "<input id=child size=5 value=XX>",
        "<textarea id=child cols=5 rows=2>XX</textarea>",
        "<select id=child><option>XX</option><option>XXXX</option></select>",
        "<select id=child size=3><option>XX</option><option>XXXX</option></select>",
        "<input id=child type=button value=XX>", "<button id=child>XX</button>"};
    for (const char* control : controls) {
        SCOPED_TRACE(control);
        StrBuf* source = strbuf_new();
        strbuf_append_format(source,
            "<!doctype html><style>@font-face{font-family:Ahem;src:url('../layout/data/font/Ahem.ttf')}"
            "body{margin:0}#result{display:flow-root}#child{font:20px/20px Ahem}</style>"
            "<div id=result>%s</div>",control);
        DomElement* result = layout_script_source(source,1200,800);
        ASSERT_NE(result,nullptr);
        DomElement* child = dom_find_element_by_id(result,"child");
        ASSERT_NE(child,nullptr);
        float horizontal_width = child->width, horizontal_height = child->height;
        ASSERT_GT(horizontal_width,horizontal_height);
        for (const char* mode : {"horizontal-tb","vertical-lr","vertical-rl","sideways-lr","sideways-rl","horizontal-tb"}) {
            SCOPED_TRACE(mode);
            ASSERT_TRUE(dom_cssom_set_inline_property(result,"writing-mode",mode));
            for (bool physical : {false,true,false}) {
                SCOPED_TRACE(physical);
                ASSERT_TRUE(dom_cssom_set_inline_property(child,"box-sizing","border-box"));
                ASSERT_TRUE(dom_cssom_set_inline_property(child,"width",physical ? "71px" : "auto"));
                ASSERT_TRUE(dom_cssom_set_inline_property(child,"height",physical ? "37px" : "auto"));
                ASSERT_TRUE(dom_ensure_geometry_snapshot(document));
                bool vertical = strcmp(mode,"horizontal-tb") != 0;
                // HTML's character/row metrics follow inline/block axes; CSS physical sizes do not.
                EXPECT_FLOAT_EQ(child->width,physical ? 71 : vertical ? horizontal_height : horizontal_width);
                EXPECT_FLOAT_EQ(child->height,physical ? 37 : vertical ? horizontal_width : horizontal_height);
            }
        }
        ui.document = nullptr;
        free_document(document);
        document = nullptr;
    }
}

TEST_F(RadiantTextQuery, RetainedSvgStylesMatchFreshCascadeAfterInheritedAndLocalChanges) {
    DomElement* svg = load_script_source(R"HTML(<style>
        #parent { color:rgb(10,20,30); --paint:rgb(30,40,50); font-size:18px }
        #result { width:40px; height:30px; fill:var(--paint); stroke:currentColor }
        #shape { stroke-width:2px; opacity:.75 }
    </style><div id="parent"><svg id="result" viewBox="0 0 40 30">
        <rect id="shape" width="20" height="20"/>
    </svg></div>)HTML");
    ASSERT_NE(svg, nullptr);
    DomElement* shape = dom_find_element_by_id(document->root->as_element(), "shape");
    ASSERT_NE(shape, nullptr);
    SvgDomStyleScope fresh = {};
    for (unsigned pass = 0; pass < 4; pass++) {
        SCOPED_TRACE(pass);
        if (pass == 1) ASSERT_TRUE(svg->parent_element()->set_attribute("style",
            "color:rgb(60,70,80);--paint:rgb(90,100,110);font-size:24px"));
        if (pass == 2) ASSERT_TRUE(shape->set_attribute("style", "fill:red;stroke-width:4px;opacity:.5"));
        if (pass == 3) ASSERT_TRUE(shape->remove_attribute("style"));
        document->incremental_layout = pass != 0;
        layout_html_doc(&ui, document, pass != 0);
        for (DomElement* element : {svg, shape}) {
            ASSERT_NE(element->layout_cache, nullptr);
            ASSERT_NE(element->layout_cache->resolved_style, nullptr);
            for (const char* name : {"fill", "stroke", "stroke-width", "opacity", "font-size"}) {
                char retained[128] = {}, reference[128] = {};
                bool retained_css = false, reference_css = false;
                const char* a = svg_get_dom_presentation_property(element, name, true,
                    retained, sizeof(retained), &retained_css);
                // an instance query without a substituted parent independently selects the cascade.
                const char* b = svg_get_dom_presentation_property(element, name, true,
                    reference, sizeof(reference), &reference_css, nullptr, nullptr, &fresh);
                EXPECT_EQ(a != nullptr, b != nullptr) << name;
                EXPECT_STREQ(retained, reference) << name;
                EXPECT_EQ(retained_css, reference_css) << name;
            }
        }
        EXPECT_FLOAT_EQ(svg->width, 40.0f);
        EXPECT_FLOAT_EQ(svg->height, 30.0f);
    }
    document->incremental_layout = false;
}

TEST_F(RadiantFormControls, AuthoredButtonStylesRetainComputedInputsAcrossSizingQueries) {
    DomElement* button = load_script_source(R"HTML(<style>
        body { margin:0; font:20px Arial }
        #row { display:flex; width:200px }
        #result { font:inherit; width:50%; padding:4px; border:2px solid; flex:none }
    </style><div id="row"><button id="result">Label</button></div>)HTML");
    ASSERT_NE(button, nullptr);
    layout_html_doc(&ui, document, false);
    ASSERT_NE(button->form_control(), nullptr);
    ASSERT_NE(button->layout_cache, nullptr);
    ASSERT_NE(button->layout_cache->resolved_style, nullptr);
    FormControlProp* native = button->form;
    FlexFlowTestContext flow(&ui, document, button);
    uint32_t revision = radiant::css_resolved_style_input_revision(&flow.layout, button);
    ASSERT_NE(revision, 0u);
    uint64_t geometry = retained_subtree_geometry(button);
    for (float width : {80.0f, 160.0f, 80.0f}) {
        flow.layout.block.content_width = width;
        layout_measure_intrinsic_widths(&flow.layout, button, true);
        EXPECT_EQ(radiant::css_resolved_style_input_revision(&flow.layout, button), revision);
    }
    document->incremental_layout = true;
    layout_html_doc(&ui, document, true);
    EXPECT_EQ(retained_subtree_geometry(button), geometry);
    EXPECT_EQ(button->form.get(), native);
    EXPECT_FLOAT_EQ(button->width, 100.0f);

    DomElement* row = button->parent_element();
    ASSERT_TRUE(row->set_attribute("style", "width:120px;font-size:24px"));
    layout_html_doc(&ui, document, true);
    EXPECT_FLOAT_EQ(button->width, 60.0f);
    EXPECT_FLOAT_EQ(button->font->font_size, 24.0f);
    ASSERT_TRUE(button->set_attribute("style", "padding:8px;font-size:16px"));
    ASSERT_TRUE(dom_text_set_content(button->first_child->as_text(), "A longer label"));
    layout_html_doc(&ui, document, true);
    EXPECT_FLOAT_EQ(button->boundary()->padding.left, 8.0f);
    EXPECT_FLOAT_EQ(button->font->font_size, 16.0f);
    geometry = retained_subtree_geometry(button);
    document->incremental_layout = false;
    layout_html_doc(&ui, document, true);
    EXPECT_EQ(retained_subtree_geometry(button), geometry);
    EXPECT_EQ(button->form.get(), native);

}

TEST_F(RadiantFormControls, DefaultTextBackgroundPreservesRoundedBorders) {
    ASSERT_NE(load_script_source(R"HTML(<style>
        body{margin:0;background:#ddd}.box{position:absolute;box-sizing:border-box;
        width:100px;height:40px;border:3px solid blue;border-radius:12px;padding:0}
        .reference{left:140px;background:white}.control{left:20px}
        .ellipse{border-width:2px 4px 5px 3px;border-radius:30% 20% / 45% 30%}
        </style><div id="result"></div>
        <input class="box control" style="top:20px"><div class="box reference" style="top:20px"></div>
        <textarea class="box control" style="top:80px"></textarea><div class="box reference" style="top:80px"></div>
        <input class="box control ellipse" style="top:140px"><div class="box reference ellipse" style="top:140px"></div>
        <textarea class="box control ellipse" style="top:200px"></textarea><div class="box reference ellipse" style="top:200px"></div>)HTML"), nullptr);
    for (float scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(scale);
        ui_context_set_device_scale(&ui, scale, scale);
        ui.viewport_width = 280; ui.viewport_height = 260;
        ui.window_width = 280 * scale; ui.window_height = 260 * scale;
        ui.create_surface(ui.window_width, ui.window_height);
        layout_html_doc(&ui, document, false);
        render_html_doc(&ui, document->view_tree, nullptr);
        ASSERT_FALSE(ui.render_failed);
        for (unsigned top : {20u, 80u, 140u, 200u}) {
            SCOPED_TRACE(top);
            unsigned missing_border = 0, overwritten_exterior = 0, border_pixels = 0;
            for (unsigned y = 0; y < 40 * scale; y++) {
                for (unsigned x = 0; x < 100 * scale; x++) {
                    const uint8_t* actual = (const uint8_t*)ui.surface->pixels +
                        (unsigned)(top * scale + y) * ui.surface->pitch + (unsigned)(20 * scale + x) * 4;
                    const uint8_t* reference = actual + (unsigned)(120 * scale) * 4;
                    if (reference[0] < 20 && reference[1] < 20 && reference[2] > 235) {
                        border_pixels++;
                        if (actual[0] > 40 || actual[1] > 40 || actual[2] < 215) missing_border++;
                    }
                    if (reference[0] == 221 && reference[1] == 221 && reference[2] == 221 &&
                        (actual[0] != 221 || actual[1] != 221 || actual[2] != 221)) overwritten_exterior++;
                }
            }
            EXPECT_GT(border_pixels, 0u);
            EXPECT_EQ(missing_border, 0u);
            EXPECT_EQ(overwritten_exterior, 0u);
            test_radiant_expect_surface_pixel(ui.surface, 70 * scale, (top + 20) * scale, 255, 255, 255);
        }
    }
}

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
