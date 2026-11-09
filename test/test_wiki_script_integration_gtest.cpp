// Script-backed Wiki formatting belongs to the Lambda integration suite (D9.1.1v3).
#include <gtest/gtest.h>
#include "test_input_test_helpers.hpp"
#include "test_wiki_script_helper.hpp"
#include "test_markup_emitter_fixture.hpp"
#include "../lambda/input/input.hpp"
#include "lib/log.h"
#include "lib/url.h"
#include <stdlib.h>
#include <string.h>

class WikiWriterIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override { log_init(nullptr); }
};
class MarkupTests : public WikiWriterIntegrationTest {};
class WikiTests : public WikiWriterIntegrationTest {};
class FormatAdapterTest : public WikiWriterIntegrationTest {};

static const char* parse_to_wiki_script(const char* content, const char* filename) {
    MarkupTestInput parsed = parse_test_markup(content, filename);
    if (!parsed.input) return nullptr;
    const char* output = format_wiki_script(parsed.input->pool, parsed.input->root);
    free(parsed.source);
    return output;
}

TEST_F(MarkupTests, MarkupWikiDetection) {

    const char* wiki_content = "= Main Heading =\n\n"
        "This is wiki format content.\n\n"
        "== Subheading ==\n\n"
        "* List item 1\n"
        "* List item 2\n\n"
        "'''Bold text''' and ''italic text''.\n\n"
        "[[Link|Link text]]";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = create_lambda_string("wiki");

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.wiki", cwd);

    // Make a mutable copy of the content
    char* content_copy = strdup(wiki_content);


    // Parse the content
    Input* parsed_input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse Wiki markup content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back
    const char* formatted = format_wiki_script(parsed_input->pool, root_item);

    ASSERT_NE(formatted, nullptr) << "Failed to format Wiki markup data";
    ASSERT_GT(strlen(formatted), 0U) << "Formatted markup should not be empty";


    // Clean up
    free(content_copy);
    // free(type_str); // disabled due to Pool API interaction
    // free(flavor_str); // disabled due to Pool API interaction
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(WikiTests, WikiRoundtrip) {

    const char* wiki_content = "= Lambda Test Page =\n\n"
        "This is a test of Wiki markup.\n\n"
        "== Section 1 ==\n\n"
        "This section contains:\n\n"
        "* '''Bold text'''\n"
        "* ''Italic text''\n"
        "* <code>Code snippets</code>\n\n"
        "== Section 2 ==\n\n"
        "=== Subsection ===\n\n"
        "Here's a link: [[Main Page|home page]]\n\n"
        "And an external link: [https://example.com Example Site]\n\n"
        "== Lists ==\n\n"
        "# First item\n"
        "# Second item\n"
        "# Third item\n";

    String* type_str = create_lambda_string("wiki");
    String* flavor_str = NULL;

    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.wiki", cwd);

    char* wiki_copy = strdup(wiki_content);

    Input* parsed_input = input_from_source(wiki_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse Wiki content";

    Item root_item = parsed_input->root;

    const char* formatted_wiki = format_wiki_script(parsed_input->pool, root_item);

    ASSERT_NE(formatted_wiki, nullptr) << "Failed to format Wiki data";
    ASSERT_GT(strlen(formatted_wiki), 0U) << "Formatted Wiki should not be empty";


    free(wiki_copy);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(FormatAdapterTest, WikiTemplateRoundtripKeepsSource) {
    const char* content =
        "{{main|Main article}}\n\n"
        "{{nested{{template}}example}}\n\n"
        "{{#switch: value\n| case1 = result1\n| default result\n}}\n\n"
        "{{outer|{{inner|x}}|[[Page|label]]}}\n\n"
        "{{{1|fallback}}}\n";
    const char* wiki = parse_to_wiki_script(content, "test.wiki");
    ASSERT_NE(wiki, nullptr);
    EXPECT_TRUE(test_input_contains(wiki, "{{main|Main article}}"));
    EXPECT_TRUE(test_input_contains(wiki, "{{nested{{template}}example}}"));
    EXPECT_TRUE(test_input_contains(wiki,
        "{{#switch: value\n| case1 = result1\n| default result\n}}"));
    EXPECT_TRUE(test_input_contains(wiki, "{{outer|{{inner|x}}|[[Page|label]]}}"));
    EXPECT_TRUE(test_input_contains(wiki, "{{{1|fallback}}}"));

    String* json = test_parse_to_format(content, "test.wiki", "json");
    ASSERT_NE(json, nullptr);
    int vars = 0;
    for (const char* p = json->chars; (p = strstr(p, "\"$\": \"var\"")); p++) vars++;
    EXPECT_EQ(vars, 5);
    EXPECT_TRUE(test_input_contains(json->chars, "\"name\": \"main\""));
    EXPECT_TRUE(test_input_contains(json->chars, "\"args\": \"Main article\""));
    EXPECT_TRUE(test_input_contains(json->chars, "\"source\": \"{{main|Main article}}\""));
    EXPECT_TRUE(test_input_contains(json->chars, "\"name\": \"outer\""));
    EXPECT_TRUE(test_input_contains(json->chars, "\"args\": \"{{inner|x}}|[[Page|label]]\""));
    EXPECT_TRUE(test_input_contains(json->chars, "\"data-wiki-parameter\": \"true\""));
    EXPECT_TRUE(test_input_contains(json->chars, "\"default\": \"fallback\""));

    String* html = test_parse_to_format(content, "test.wiki", "html");
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(test_input_contains(html->chars, ">{{main|Main article}}</var>"));
    EXPECT_TRUE(test_input_contains(html->chars, ">{{{1|fallback}}}</var>"));
}

TEST_F(FormatAdapterTest, WikiNowikiDoesNotLoadTemplates) {
    const char* content =
        "<nowiki>{{not a template}}</nowiki>\n\n"
        "<nowiki>\n'''not bold'''\n{{not loaded}}\n</nowiki>\n";
    String* json = test_parse_to_format(content, "test.wiki", "json");
    ASSERT_NE(json, nullptr);
    EXPECT_FALSE(test_input_contains(json->chars, "\"$\": \"var\""));
    EXPECT_TRUE(test_input_contains(json->chars, "data-wiki-nowiki"));
    EXPECT_TRUE(test_input_contains(json->chars, "{{not loaded}}"));
    EXPECT_FALSE(test_input_contains(json->chars, "\"$\": \"strong\""));

    const char* wiki = parse_to_wiki_script(content, "test.wiki");
    ASSERT_NE(wiki, nullptr);
    EXPECT_TRUE(test_input_contains(wiki, "<nowiki>{{not a template}}</nowiki>"));
    EXPECT_TRUE(test_input_contains(wiki, "{{not loaded}}\n</nowiki>"));
}

class FormatMarkupParityTest : public WikiWriterIntegrationTest {
protected:
    const char* format_parsed(const char* source, const char* format) {
        MarkupTestInput parsed = parse_test_markup(source, "parity_test.md");
        if (!parsed.input || strcmp(format, "wiki")) return nullptr;
        const char* output = format_wiki_script(parsed.input->pool, parsed.input->root);
        free(parsed.source);
        return output;
    }
};

TEST_F(FormatMarkupTest, WikiHeading) {
    Item item = build_heading_paragraph();
    const char* out = format_wiki_script(pool_, item);
    ASSERT_NE(out, nullptr);
    // Wiki h1: = Hello World =
    EXPECT_NE(strstr(out, "= Hello World ="), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiInline) {
    Item item = build_inline_formatting();
    const char* out = format_wiki_script(pool_, item);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "'''bold'''"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "''italic''"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "<code>code</code>"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiLink) {
    Item item = build_link();
    const char* out = format_wiki_script(pool_, item);
    ASSERT_NE(out, nullptr);
    // Wiki link: [url text]
    EXPECT_NE(strstr(out, "[https://example.com here]"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiUnorderedList) {
    Item item = build_unordered_list();
    const char* out = format_wiki_script(pool_, item);
    ASSERT_NE(out, nullptr);
    // Wiki uses * for unordered list items (depth repetition)
    EXPECT_NE(strstr(out, "* Item one"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiOrderedList) {
    Item item = build_ordered_list();
    const char* out = format_wiki_script(pool_, item);
    ASSERT_NE(out, nullptr);
    // Wiki uses # for ordered list items (depth repetition)
    EXPECT_NE(strstr(out, "# First"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiTable) {
    Item item = build_table();
    const char* out = format_wiki_script(pool_, item);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "{|"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "|}"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "Name"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiTablePreservesSupportedAttributes) {
    ElementBuilder row = mb_->element("tr");
    row.attr("class", "highlight");
    row.child(mb_->element("td").attr("colspan", "2").text("wide").final());
    ElementBuilder table = mb_->element("table");
    table.attr("class", "custom");
    table.child(row.final());
    const char* out = format_wiki_script(pool_, table.final());
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "class=\"custom\""), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "class=\"highlight\""), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "colspan=\"2\""), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupParityTest, WikiHeading) {
    const char* out = format_parsed("# Title\n", "wiki");
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "Title"), nullptr) << "Output: " << out;
}
