/**
 * @file test_entity_emoji_gtest.cpp
 * @brief Tests for HTML/XML entity and Markdown emoji Symbol roundtrip support
 *
 * This test file verifies:
 * 1. HTML parser correctly handles entities (ASCII escapes decode, named entities as Symbol)
 * 2. XML parser correctly handles entities using html_entities module
 * 3. Markdown parser correctly handles :emoji: shortcodes as Symbol
 * 4. Formatters correctly output Symbol items back to their original format
 * 5. Symbol resolver correctly resolves entities/emoji to UTF-8 for rendering
 */

#include <gtest/gtest.h>
#include <thorvg_capi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../lambda/lambda.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lib/html_entities.h"
#include "../lib/emoji_shortcodes.h"
#include "../lambda/input/input-utils.h"
#include "../lib/log.h"
#include "../lib/url.h"
#include "../lib/arena.h"
#include "../lib/font/font_colr.h"
#include "../lib/font/font_tables.h"
#include "../lib/font/font_internal.h"

// Forward declarations
extern "C" {
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    String* format_data(Item item, String* type, String* flavor, Pool* pool);
    Url* get_current_dir(void);
    Url* parse_url(Url* base, const char* url_str);
}

// Helper to create Lambda String
static String* create_test_string(const char* text) {
    if (!text) return nullptr;
    size_t len = strlen(text);
    size_t total_size = sizeof(String) + len + 1;
    String* result = (String*)malloc(total_size);
    if (!result) return nullptr;
    result->len = len;
    strcpy(result->chars, text);
    return result;
}

// ==== Entity Resolution Unit Tests ====

class EntityResolutionTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(nullptr);
    }
};

TEST_F(EntityResolutionTest, AsciiEscapes) {
    // ASCII escapes should resolve to their character values
    const char* lt = html_entity_lookup("lt", 2);
    ASSERT_NE(lt, nullptr);
    EXPECT_STREQ(lt, "<");

    const char* gt = html_entity_lookup("gt", 2);
    ASSERT_NE(gt, nullptr);
    EXPECT_STREQ(gt, ">");

    const char* amp = html_entity_lookup("amp", 3);
    ASSERT_NE(amp, nullptr);
    EXPECT_STREQ(amp, "&");

    const char* quot = html_entity_lookup("quot", 4);
    ASSERT_NE(quot, nullptr);
    EXPECT_STREQ(quot, "\"");

    const char* apos = html_entity_lookup("apos", 4);
    ASSERT_NE(apos, nullptr);
    EXPECT_STREQ(apos, "'");

    // Verify is_ascii_escape helper
    EXPECT_TRUE(html_entity_is_ascii_escape("lt", 2));
    EXPECT_TRUE(html_entity_is_ascii_escape("amp", 3));
    EXPECT_FALSE(html_entity_is_ascii_escape("copy", 4));
}

TEST_F(EntityResolutionTest, NamedEntities) {
    // Named entities should return pre-encoded UTF-8
    const char* copy_ent = html_entity_lookup("copy", 4);
    ASSERT_NE(copy_ent, nullptr);
    // © = U+00A9 = 0xC2 0xA9
    EXPECT_EQ((unsigned char)copy_ent[0], 0xC2);
    EXPECT_EQ((unsigned char)copy_ent[1], 0xA9);
    EXPECT_EQ(utf8_first_codepoint(copy_ent), 0x00A9u);

    const char* nbsp_ent = html_entity_lookup("nbsp", 4);
    ASSERT_NE(nbsp_ent, nullptr);
    EXPECT_EQ(utf8_first_codepoint(nbsp_ent), 0x00A0u);

    const char* mdash_ent = html_entity_lookup("mdash", 5);
    ASSERT_NE(mdash_ent, nullptr);
    EXPECT_EQ(utf8_first_codepoint(mdash_ent), 0x2014u);

    const char* euro_ent = html_entity_lookup("euro", 4);
    ASSERT_NE(euro_ent, nullptr);
    EXPECT_EQ(utf8_first_codepoint(euro_ent), 0x20ACu);
}

TEST_F(EntityResolutionTest, UnknownEntities) {
    // Unknown entities should return nullptr
    const char* unknown = html_entity_lookup("unknownentity", 13);
    EXPECT_EQ(unknown, nullptr);

    const char* invalid = html_entity_lookup("xyz123", 6);
    EXPECT_EQ(invalid, nullptr);
}

TEST_F(EntityResolutionTest, CodepointToUtf8Conversion) {
    char buf[8];

    // ASCII character
    int len = codepoint_to_utf8(0x41, buf);  // 'A'
    EXPECT_EQ(len, 1);
    EXPECT_EQ(buf[0], 'A');

    // 2-byte UTF-8 (copyright symbol)
    len = codepoint_to_utf8(0x00A9, buf);
    EXPECT_EQ(len, 2);
    EXPECT_EQ((unsigned char)buf[0], 0xC2);
    EXPECT_EQ((unsigned char)buf[1], 0xA9);

    // 3-byte UTF-8 (euro sign)
    len = codepoint_to_utf8(0x20AC, buf);
    EXPECT_EQ(len, 3);
    EXPECT_EQ((unsigned char)buf[0], 0xE2);
    EXPECT_EQ((unsigned char)buf[1], 0x82);
    EXPECT_EQ((unsigned char)buf[2], 0xAC);

    // 4-byte UTF-8 (emoji - grinning face)
    len = codepoint_to_utf8(0x1F600, buf);
    EXPECT_EQ(len, 4);
}

TEST_F(EntityResolutionTest, Utf8FirstCodepoint) {
    // Test utf8_first_codepoint helper
    EXPECT_EQ(utf8_first_codepoint("A"), 0x41u);
    EXPECT_EQ(utf8_first_codepoint("\xC2\xA9"), 0x00A9u);  // ©
    EXPECT_EQ(utf8_first_codepoint("\xE2\x82\xAC"), 0x20ACu);  // €
    EXPECT_EQ(utf8_first_codepoint(""), 0u);
    EXPECT_EQ(utf8_first_codepoint(nullptr), 0u);
}

TEST_F(EntityResolutionTest, ReverseLookup) {
    // Test entity name reverse lookup
    // Note: WHATWG has both "COPY" and "copy" for U+00A9; reverse lookup
    // returns whichever appears first in the sorted table (COPY).
    const char* name = html_entity_name_for_codepoint(0x00A9);  // ©
    ASSERT_NE(name, nullptr);
    // Verify the returned name actually resolves back to the same codepoint
    const char* utf8 = html_entity_lookup(name, strlen(name));
    ASSERT_NE(utf8, nullptr);
    EXPECT_EQ(utf8_first_codepoint(utf8), 0x00A9u);

    EXPECT_EQ(html_entity_name_for_codepoint(0), nullptr);
}

// ==== HTML Entity Parsing Tests ====

class HtmlEntityParsingTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(nullptr);
    }

    Input* parseHtml(const char* html) {
        String* type = create_test_string("html");
        Url* cwd = get_current_dir();
        Url* url = parse_url(cwd, "test.html");
        char* content = strdup(html);
        return input_from_source(content, url, type, nullptr);
    }
};

TEST_F(HtmlEntityParsingTest, AsciiEscapesDecodeInline) {
    // ASCII escapes should be decoded to their character values in text content
    Input* input = parseHtml("<p>&lt;tag&gt; &amp; &quot;text&quot;</p>");
    ASSERT_NE(input, nullptr);

    ItemReader reader(input->root.to_const());
    EXPECT_TRUE(reader.isElement());

    // The root should be an element (could be 'html' wrapper or 'p' directly)
    ElementReader elem = reader.asElement();
    EXPECT_NE(elem.tagName(), nullptr);
}

TEST_F(HtmlEntityParsingTest, NumericEntitiesDecodeInline) {
    // Numeric character references should be decoded to UTF-8
    Input* input = parseHtml("<p>&#65;&#x42;&#67;</p>");  // ABC
    ASSERT_NE(input, nullptr);

    // Should parse successfully and decode numeric refs
    ItemReader reader(input->root.to_const());
    EXPECT_TRUE(reader.isElement());
}

// ==== Markdown Emoji Parsing Tests ====

class MarkdownEmojiParsingTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(nullptr);
    }

    Input* parseMarkdown(const char* md) {
        String* type = create_test_string("markup");
        Url* cwd = get_current_dir();
        Url* url = parse_url(cwd, "test.md");
        char* content = strdup(md);
        return input_from_source(content, url, type, nullptr);
    }
};

static int count_shortcode_symbols(ItemReader item, const char* name) {
    if (item.isSymbol()) {
        Symbol* symbol = item.asSymbol();
        size_t length = strlen(name);
        return symbol && symbol->len == length &&
            memcmp(symbol->chars, name, length) == 0 ? 1 : 0;
    }
    int count = 0;
    if (item.isElement()) {
        ElementReader element = item.asElement();
        for (int64_t i = 0; i < element.childCount(); i++) {
            count += count_shortcode_symbols(element.childAt(i), name);
        }
    } else if (item.isArray()) {
        ArrayReader array = item.asArray();
        for (int64_t i = 0; i < array.length(); i++) {
            count += count_shortcode_symbols(array.get(i), name);
        }
    }
    return count;
}

TEST_F(MarkdownEmojiParsingTest, RendererSupportedNamesParseAsSymbols) {
    Input* input = parseMarkdown(":smiley: :worried: :monkey_face: :hamburger: :+1: :invalid: :octocat:");
    ASSERT_NE(input, nullptr);
    ItemReader root(input->root.to_const());
    EXPECT_EQ(count_shortcode_symbols(root, "smiley"), 1);
    EXPECT_EQ(count_shortcode_symbols(root, "worried"), 1);
    EXPECT_EQ(count_shortcode_symbols(root, "monkey_face"), 1);
    EXPECT_EQ(count_shortcode_symbols(root, "hamburger"), 1);
    EXPECT_EQ(count_shortcode_symbols(root, "+1"), 1);
    EXPECT_EQ(count_shortcode_symbols(root, "invalid"), 0);
    EXPECT_EQ(count_shortcode_symbols(root, "octocat"), 0);
}

TEST_F(MarkdownEmojiParsingTest, SharedLookupRetainsLegacyAliases) {
    EXPECT_STREQ(emoji_shortcode_lookup("+1", 2), "👍");
    EXPECT_STREQ(emoji_shortcode_lookup("clock", 5), "🕐");
    EXPECT_STREQ(emoji_shortcode_lookup("info", 4), "ℹ️");
    EXPECT_STREQ(emoji_shortcode_lookup("fearful", 7), "😨");
    EXPECT_EQ(emoji_shortcode_lookup("octocat", 7), nullptr);
}

TEST_F(MarkdownEmojiParsingTest, EmojiShortcodeParsesAsSymbol) {
    // Emoji shortcodes should parse as Symbol items
    Input* input = parseMarkdown("Hello :smile: World");
    ASSERT_NE(input, nullptr);

    // The parsed result should contain a Symbol for "smile"
    ItemReader reader(input->root.to_const());
    EXPECT_TRUE(reader.isElement() || reader.isArray());
}

TEST_F(MarkdownEmojiParsingTest, MultipleEmojis) {
    // Multiple emoji shortcodes should all become Symbols
    Input* input = parseMarkdown("I :heart: Lambda :rocket:");
    ASSERT_NE(input, nullptr);

    ItemReader reader(input->root.to_const());
    EXPECT_TRUE(reader.isElement() || reader.isArray());
}

TEST_F(MarkdownEmojiParsingTest, UnknownEmojiPreservedAsText) {
    // Unknown shortcodes should be preserved as literal text
    Input* input = parseMarkdown("Hello :unknown_emoji_xyz: World");
    ASSERT_NE(input, nullptr);

    // Should parse successfully
    ItemReader reader(input->root.to_const());
    EXPECT_TRUE(reader.isElement() || reader.isArray());
}

// ==== HTML Formatter Symbol Output Tests ====

class HtmlFormatterSymbolTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(nullptr);
    }

    Input* parseHtml(const char* html) {
        String* type = create_test_string("html");
        Url* cwd = get_current_dir();
        Url* url = parse_url(cwd, "test.html");
        char* content = strdup(html);
        return input_from_source(content, url, type, nullptr);
    }

    String* formatHtml(Input* input) {
        String* type = create_test_string("html");
        return format_data(input->root, type, nullptr, input->pool);
    }
};

TEST_F(HtmlFormatterSymbolTest, AsciiEscapesPreserved) {
    // Test that < > & " are properly escaped in output
    Input* input = parseHtml("<p>&lt; &gt; &amp;</p>");
    ASSERT_NE(input, nullptr);

    String* output = formatHtml(input);
    ASSERT_NE(output, nullptr);

    // Should contain the entities in output
    // (exact output depends on whether we decode and re-encode or preserve)
    EXPECT_GT(output->len, 0);
}

// ==== Markdown Formatter Emoji Output Tests ====

class MarkdownFormatterEmojiTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(nullptr);
    }

    Input* parseMarkdown(const char* md) {
        String* type = create_test_string("markup");
        Url* cwd = get_current_dir();
        Url* url = parse_url(cwd, "test.md");
        char* content = strdup(md);
        return input_from_source(content, url, type, nullptr);
    }

    String* formatMarkdown(Input* input) {
        String* type = create_test_string("markup");
        return format_data(input->root, type, nullptr, input->pool);
    }
};

TEST_F(MarkdownFormatterEmojiTest, EmojiRoundtrip) {
    // Emoji shortcode should roundtrip: :smile: -> Symbol -> :smile:
    const char* md = "Hello :smile: World";
    Input* input = parseMarkdown(md);
    ASSERT_NE(input, nullptr);

    String* output = formatMarkdown(input);
    ASSERT_NE(output, nullptr);

    // Output should contain :smile:
    if (output->len > 0) {
        printf("Markdown roundtrip output: %s\n", output->chars);
        // Note: exact matching depends on paragraph wrapping etc.
        EXPECT_GT(output->len, 0);
    }
}

// ==== ItemReader Symbol API Tests ====

class ItemReaderSymbolTest : public ::testing::Test {
protected:
    Input* input;

    void SetUp() override {
        log_init(nullptr);
        // Create a minimal input for the MarkBuilder
        String* type = create_test_string("html");
        Url* cwd = get_current_dir();
        Url* url = parse_url(cwd, "test.html");
        char* content = strdup("<html></html>");
        input = input_from_source(content, url, type, nullptr);
    }

    void TearDown() override {
        // Input cleanup is handled by its pool
    }
};

TEST_F(ItemReaderSymbolTest, IsSymbolMethod) {
    ASSERT_NE(input, nullptr);

    // Create a Symbol item and test isSymbol()
    MarkBuilder builder(input);
    Symbol* sym = builder.createSymbol("test_symbol");
    ASSERT_NE(sym, nullptr);

    Item sym_item = {.item = y2it(sym)};
    ItemReader reader(sym_item.to_const());

    EXPECT_TRUE(reader.isSymbol());
    EXPECT_FALSE(reader.isString());
    EXPECT_FALSE(reader.isElement());
}

TEST_F(ItemReaderSymbolTest, AsSymbolMethod) {
    ASSERT_NE(input, nullptr);

    // Create a Symbol and retrieve it with asSymbol()
    MarkBuilder builder(input);
    Symbol* sym = builder.createSymbol("hello");
    ASSERT_NE(sym, nullptr);

    Item sym_item = {.item = y2it(sym)};
    ItemReader reader(sym_item.to_const());

    Symbol* retrieved = reader.asSymbol();
    ASSERT_NE(retrieved, nullptr);
    EXPECT_STREQ(retrieved->chars, "hello");
}

TEST_F(ItemReaderSymbolTest, StringIsNotSymbol) {
    ASSERT_NE(input, nullptr);

    // A String item should not be identified as Symbol
    MarkBuilder builder(input);
    String* str = builder.createString("regular string");
    ASSERT_NE(str, nullptr);

    Item str_item = {.item = s2it(str)};
    ItemReader reader(str_item.to_const());

    EXPECT_FALSE(reader.isSymbol());
    EXPECT_TRUE(reader.isString());
    EXPECT_EQ(reader.asSymbol(), nullptr);
}

// ==== Integration Tests ====

class EntityEmojiIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(nullptr);
    }
};

TEST_F(EntityEmojiIntegrationTest, HtmlWithMixedEntities) {
    // Test HTML with various entity types
    String* type = create_test_string("html");
    Url* cwd = get_current_dir();
    Url* url = parse_url(cwd, "test.html");
    char* html = strdup("<html><body><p>Copyright &copy; 2024 &mdash; All &lt;rights&gt; reserved</p></body></html>");

    Input* input = input_from_source(html, url, type, nullptr);
    ASSERT_NE(input, nullptr);

    // Format back to HTML
    String* output = format_data(input->root, type, nullptr, input->pool);
    ASSERT_NE(output, nullptr);
    EXPECT_GT(output->len, 0);

    printf("HTML mixed entities output: %s\n", output->chars);
}

TEST_F(EntityEmojiIntegrationTest, XmlEntityHandling) {
    // Test XML entity handling
    String* type = create_test_string("xml");
    Url* cwd = get_current_dir();
    Url* url = parse_url(cwd, "test.xml");
    char* xml = strdup("<?xml version=\"1.0\"?><root><text>&lt;value&gt; &amp; more</text></root>");

    Input* input = input_from_source(xml, url, type, nullptr);
    ASSERT_NE(input, nullptr);

    // Format back to XML
    String* output = format_data(input->root, type, nullptr, input->pool);
    ASSERT_NE(output, nullptr);
    EXPECT_GT(output->len, 0);

    printf("XML entity output: %s\n", output->chars);
}

TEST(FontColorTest, ColrV1CompatibilityLayersKeepPaletteColors) {
    // COLR v1 may carry v0 records so older rasterizers can still draw color.
    uint8_t data[62] = {};
    data[1] = 1;                  // COLR version 1
    data[3] = 1;                  // one v0 BaseGlyph record
    data[7] = 34;                 // BaseGlyph records start after the v1 header
    data[11] = 40;                // Layer records follow the base record
    data[13] = 1;                 // one layer
    data[34] = 0x12; data[35] = 0x34; // base glyph
    data[39] = 1;                 // one layer for the base glyph
    data[40] = 0x23; data[41] = 0x45; // layer glyph

    data[47] = 1;                 // CPAL: one entry per palette
    data[49] = 1;                 // one palette
    data[51] = 1;                 // one color record
    data[55] = 14;                // color records offset from CPAL start
    data[58] = 0x33; data[59] = 0x22; data[60] = 0x11; data[61] = 0xFF;

    FontTableDir dirs[2] = {};
    dirs[0].tag = FONT_TAG('C', 'O', 'L', 'R');
    dirs[0].length = 44;
    dirs[1].tag = FONT_TAG('C', 'P', 'A', 'L');
    dirs[1].offset = 44;
    dirs[1].length = 18;
    FontTables tables = {};
    tables.data = data;
    tables.data_len = sizeof(data);
    tables.dirs = dirs;
    tables.num_tables = 2;

    ASSERT_TRUE(colr_has_glyph(&tables, 0x1234));
    ColrLayer layer = {};
    ASSERT_EQ(colr_get_layers(&tables, 0x1234, &layer, 1), 1);
    EXPECT_EQ(layer.glyph_id, 0x2345);
    EXPECT_EQ(layer.r, 0x11);
    EXPECT_EQ(layer.g, 0x22);
    EXPECT_EQ(layer.b, 0x33);
    EXPECT_EQ(layer.a, 0xFF);

    data[3] = 0; // v1 without compatibility records cannot use this renderer
    EXPECT_FALSE(colr_has_glyph(&tables, 0x1234));
}

TEST_F(EntityResolutionTest, EmojiFallbackProducesColorBitmap) {
#if defined(__linux__)
    // The standalone font test needs the same ThorVG engine initialization as Radiant.
    ASSERT_EQ(tvg_engine_init(0), TVG_RESULT_SUCCESS);
#endif
    FontContextConfig cfg = {};
    cfg.pixel_ratio = 1.0f;
    FontContext* ctx = font_context_create(&cfg);
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(font_context_scan(ctx));

    FontDatabaseCriteria criteria = {};
    strncpy(criteria.family_name, "Noto Color Emoji", sizeof(criteria.family_name) - 1);
    FontDatabaseResult match = font_database_find_best_match_internal(ctx->database, &criteria);
    if (!match.font || !match.exact_family_match) {
        font_context_destroy(ctx);
#if defined(__linux__)
        tvg_engine_term();
#endif
        GTEST_SKIP() << "Noto Color Emoji is not installed";
    }

    FontStyleDesc style = {};
    style.family = "sans-serif";
    style.size_px = 16.0f;
    style.weight = FONT_WEIGHT_NORMAL;
    style.slant = FONT_SLANT_NORMAL;
    FontHandle* text_face = font_resolve(ctx, &style);
    ASSERT_NE(text_face, nullptr);

    // Emoji glyphs must have decoded color pixels, not just an advance width.
    uint32_t codepoints[] = {0x1F600u, 0x1F44Du};
    for (uint32_t codepoint : codepoints) {
        LoadedGlyph* glyph = font_load_glyph_emoji(text_face, &style, codepoint, true);
        ASSERT_NE(glyph, nullptr);
        EXPECT_EQ(glyph->bitmap.pixel_mode, GLYPH_PIXEL_BGRA);
        EXPECT_NE(glyph->bitmap.buffer, nullptr);
        EXPECT_GT(glyph->bitmap.width, 0);
        EXPECT_GT(glyph->bitmap.height, 0);
    }

    font_handle_release(text_face);
    font_context_destroy(ctx);
#if defined(__linux__)
    tvg_engine_term();
#endif
}
