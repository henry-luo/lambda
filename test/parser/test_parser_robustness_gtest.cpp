/**
 * Parser Robustness Tests
 * 
 * Tests safety and robustness of all input parsers:
 * - Empty/null input handling
 * - Deep nesting (stack safety)
 * - Large input handling  
 * - Malformed input recovery
 * - UTF-8 edge cases
 * 
 * Each parser is tested for graceful error handling without crashes.
 */

#include "parser_test_common.hpp"

using namespace parser_test;

// ============================================================================
// JSON Parser Robustness Tests
// ============================================================================

class JsonParserRobustnessTest : public ParserTestBase {};

TEST_F(JsonParserRobustnessTest, EmptyInput) {
    TestEmptyInput("json");
}

TEST_F(JsonParserRobustnessTest, MinimalValidInputs) {
    // Minimal valid JSON values - Lambda JSON parser requires container types at root
    // Bare scalars (null, true, false, numbers, strings) are not accepted as root
    EXPECT_TRUE(parse_succeeded(parse("[]", "json")));
    EXPECT_TRUE(parse_succeeded(parse("{}", "json")));
    EXPECT_TRUE(parse_succeeded(parse("[null]", "json")));
    EXPECT_TRUE(parse_succeeded(parse("[true, false]", "json")));
    EXPECT_TRUE(parse_succeeded(parse("{\"key\": \"value\"}", "json")));
    EXPECT_TRUE(parse_succeeded(parse("[1, 2, 3]", "json")));
}

TEST_F(JsonParserRobustnessTest, DeepNestedArrays) {
    // Test increasingly deep nesting - should not crash
    // Stay below MAX_DEPTH (512) for valid parsing, and slightly above to test limit
    for (int depth : {10, 100, 300, 512, 600}) {
        std::string nested = nested_json_arrays(depth);
        TestDoesNotCrash(nested.c_str(), "json");
    }
}

TEST_F(JsonParserRobustnessTest, DeepNestedObjects) {
    // Test deeply nested objects - stay near MAX_DEPTH limits
    for (int depth : {10, 100, 300, 512, 600}) {
        std::string nested = nested_json_objects(depth);
        TestDoesNotCrash(nested.c_str(), "json");
    }
}

TEST_F(JsonParserRobustnessTest, LargeArrays) {
    // Test large arrays with many elements
    for (int count : {100, 1000, 10000, 100000}) {
        std::string large = large_json_array(count);
        Input* input = parse(large.c_str(), "json");
        ASSERT_NE(input, nullptr) << "Parser crashed on array with " << count << " elements";
        EXPECT_TRUE(parse_succeeded(input)) 
            << "Failed to parse valid array with " << count << " elements";
    }
}

TEST_F(JsonParserRobustnessTest, LongStrings) {
    // Test strings of increasing length
    for (int len : {100, 1000, 10000, 100000}) {
        std::string content(len, 'x');
        std::string json = "\"" + content + "\"";
        Input* input = parse(json.c_str(), "json");
        ASSERT_NE(input, nullptr) << "Parser crashed on string of length " << len;
        EXPECT_TRUE(parse_succeeded(input)) 
            << "Failed to parse valid string of length " << len;
    }
}

TEST_F(JsonParserRobustnessTest, MalformedInputs) {
    // Malformed JSON should not crash
    TestDoesNotCrash(malformed::JSON_UNCLOSED_ARRAY, "json");
    TestDoesNotCrash(malformed::JSON_UNCLOSED_OBJECT, "json");
    TestDoesNotCrash(malformed::JSON_UNCLOSED_STRING, "json");
    TestDoesNotCrash(malformed::JSON_TRAILING_COMMA, "json");
    TestDoesNotCrash(malformed::JSON_MISSING_COLON, "json");
    TestDoesNotCrash(malformed::JSON_MISSING_COMMA, "json");
    TestDoesNotCrash(malformed::JSON_INVALID_NUMBER, "json");
    TestDoesNotCrash(malformed::JSON_UNQUOTED_KEY, "json");
}

TEST_F(JsonParserRobustnessTest, Utf8Content) {
    // Test UTF-8 strings in JSON
    auto json_string = [](const char* content) {
        return std::string("{\"key\": \"") + content + "\"}";
    };
    
    TestUtf8Content(json_string(utf8::ASCII).c_str(), "json");
    TestUtf8Content(json_string(utf8::LATIN_EXT).c_str(), "json");
    TestUtf8Content(json_string(utf8::GREEK).c_str(), "json");
    TestUtf8Content(json_string(utf8::CYRILLIC).c_str(), "json");
    TestUtf8Content(json_string(utf8::CJK).c_str(), "json");
    TestUtf8Content(json_string(utf8::JAPANESE).c_str(), "json");
    TestUtf8Content(json_string(utf8::KOREAN).c_str(), "json");
    TestUtf8Content(json_string(utf8::EMOJI).c_str(), "json");
    TestUtf8Content(json_string(utf8::MIXED).c_str(), "json");
}

TEST_F(JsonParserRobustnessTest, Utf8Keys) {
    // Test UTF-8 in JSON keys
    EXPECT_TRUE(parse_succeeded(parse("{\"你好\": 1}", "json")));
    EXPECT_TRUE(parse_succeeded(parse("{\"αβγ\": 1}", "json")));
    EXPECT_TRUE(parse_succeeded(parse("{\"🔑\": 1}", "json")));
}

TEST_F(JsonParserRobustnessTest, SpecialCharacters) {
    // Test escape sequences
    EXPECT_TRUE(parse_succeeded(parse(R"({"key": "line1\nline2"})", "json")));
    EXPECT_TRUE(parse_succeeded(parse(R"({"key": "tab\there"})", "json")));
    EXPECT_TRUE(parse_succeeded(parse(R"({"key": "quote\"here"})", "json")));
    EXPECT_TRUE(parse_succeeded(parse(R"({"key": "back\\slash"})", "json")));
    EXPECT_TRUE(parse_succeeded(parse(R"({"key": "\u0041\u0042"})", "json")));
}

// ============================================================================
// XML Parser Robustness Tests
// ============================================================================

class XmlParserRobustnessTest : public ParserTestBase {};

TEST_F(XmlParserRobustnessTest, EmptyInput) {
    TestEmptyInput("xml");
}

TEST_F(XmlParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("<r/>", "xml")));
    EXPECT_TRUE(parse_succeeded(parse("<r></r>", "xml")));
    EXPECT_TRUE(parse_succeeded(parse("<root>text</root>", "xml")));
    EXPECT_TRUE(parse_succeeded(parse("<root attr=\"val\"/>", "xml")));
}

TEST_F(XmlParserRobustnessTest, DeepNesting) {
    // XML parser has depth limit - test up to and beyond it
    for (int depth : {10, 100, 256, 300}) {
        std::string nested = nested_xml_elements(depth);
        TestDoesNotCrash(nested.c_str(), "xml");
    }
}

TEST_F(XmlParserRobustnessTest, ManyElements) {
    // Generate XML with many sibling elements
    auto generate = [](int count) {
        std::string xml = "<root>";
        for (int i = 0; i < count; i++) {
            xml += "<item>" + std::to_string(i) + "</item>";
        }
        xml += "</root>";
        return xml;
    };
    
    for (int count : {100, 1000, 10000}) {
        std::string xml = generate(count);
        Input* input = parse(xml.c_str(), "xml");
        ASSERT_NE(input, nullptr) << "Parser crashed on XML with " << count << " elements";
    }
}

TEST_F(XmlParserRobustnessTest, MalformedInputs) {
    TestDoesNotCrash(malformed::XML_UNCLOSED_TAG, "xml");
    TestDoesNotCrash(malformed::XML_MISMATCHED_TAGS, "xml");
    TestDoesNotCrash(malformed::XML_UNCLOSED_ATTR, "xml");
    TestDoesNotCrash(malformed::XML_DUPLICATE_ATTR, "xml");
    TestDoesNotCrash(malformed::XML_INVALID_NAME, "xml");
}

TEST_F(XmlParserRobustnessTest, Utf8Content) {
    auto xml = [](const char* content) {
        return std::string("<root>") + content + "</root>";
    };
    
    TestUtf8Content(xml(utf8::ASCII).c_str(), "xml");
    TestUtf8Content(xml(utf8::CJK).c_str(), "xml");
    TestUtf8Content(xml(utf8::JAPANESE).c_str(), "xml");
    TestUtf8Content(xml(utf8::EMOJI).c_str(), "xml");
    TestUtf8Content(xml(utf8::MIXED).c_str(), "xml");
}

TEST_F(XmlParserRobustnessTest, XmlDeclaration) {
    EXPECT_TRUE(parse_succeeded(parse("<?xml version=\"1.0\"?><r/>", "xml")));
    EXPECT_TRUE(parse_succeeded(parse("<?xml version=\"1.0\" encoding=\"UTF-8\"?><r/>", "xml")));
}

TEST_F(XmlParserRobustnessTest, CdataAndComments) {
    EXPECT_TRUE(parse_succeeded(parse("<r><![CDATA[<>&]]></r>", "xml")));
    EXPECT_TRUE(parse_succeeded(parse("<r><!-- comment --></r>", "xml")));
    EXPECT_TRUE(parse_succeeded(parse("<r><!-- multi\nline\ncomment --></r>", "xml")));
}

// ============================================================================
// YAML Parser Robustness Tests
// ============================================================================

class YamlParserRobustnessTest : public ParserTestBase {};

TEST_F(YamlParserRobustnessTest, EmptyInput) {
    TestEmptyInput("yaml");
}

TEST_F(YamlParserRobustnessTest, MinimalValidInputs) {
    // Lambda YAML parser requires container types at root (maps or sequences)
    // Bare scalars are not accepted as root
    EXPECT_TRUE(parse_succeeded(parse("key: value", "yaml")));
    EXPECT_TRUE(parse_succeeded(parse("- item", "yaml")));
    EXPECT_TRUE(parse_succeeded(parse("{}", "yaml")));
    EXPECT_TRUE(parse_succeeded(parse("[]", "yaml")));
    EXPECT_TRUE(parse_succeeded(parse("key: null", "yaml")));
    EXPECT_TRUE(parse_succeeded(parse("- true\n- false", "yaml")));
}

TEST_F(YamlParserRobustnessTest, DeepNesting) {
    for (int depth : {10, 50, 100, 200, 500}) {
        std::string nested = nested_yaml_maps(depth);
        TestDoesNotCrash(nested.c_str(), "yaml");
    }
}

TEST_F(YamlParserRobustnessTest, InlineDeepNesting) {
    // Test deeply nested inline/flow style - stay within limits
    auto nested_inline = [](int depth) {
        std::string yaml;
        for (int i = 0; i < depth; i++) yaml += "[";
        yaml += "1";
        for (int i = 0; i < depth; i++) yaml += "]";
        return yaml;
    };
    
    for (int depth : {10, 100, 300, 512}) {
        TestDoesNotCrash(nested_inline(depth).c_str(), "yaml");
    }
}

TEST_F(YamlParserRobustnessTest, ManyItems) {
    // Large list
    auto generate = [](int count) {
        std::string yaml;
        for (int i = 0; i < count; i++) {
            yaml += "- " + std::to_string(i) + "\n";
        }
        return yaml;
    };
    
    for (int count : {100, 1000, 10000}) {
        TestDoesNotCrash(generate(count).c_str(), "yaml");
    }
}

TEST_F(YamlParserRobustnessTest, MalformedInputs) {
    TestDoesNotCrash(malformed::YAML_BAD_INDENT, "yaml");
    TestDoesNotCrash(malformed::YAML_TAB_INDENT, "yaml");
    TestDoesNotCrash(malformed::YAML_UNCLOSED_QUOTE, "yaml");
    TestDoesNotCrash(malformed::YAML_INVALID_KEY, "yaml");
}

TEST_F(YamlParserRobustnessTest, Utf8Content) {
    auto yaml = [](const char* content) {
        return std::string("key: ") + content;
    };
    
    TestUtf8Content(yaml(utf8::ASCII).c_str(), "yaml");
    TestUtf8Content(yaml(utf8::CJK).c_str(), "yaml");
    TestUtf8Content(yaml(utf8::EMOJI).c_str(), "yaml");
    TestUtf8Content(yaml(utf8::MIXED).c_str(), "yaml");
}

TEST_F(YamlParserRobustnessTest, MultilineStrings) {
    const char* literal = "text: |\n  line 1\n  line 2\n  line 3";
    const char* folded = "text: >\n  line 1\n  line 2\n  line 3";
    
    EXPECT_TRUE(parse_succeeded(parse(literal, "yaml")));
    EXPECT_TRUE(parse_succeeded(parse(folded, "yaml")));
}

TEST_F(YamlParserRobustnessTest, Anchors) {
    const char* yaml = "a: &anchor\n  x: 1\nb: *anchor";
    EXPECT_TRUE(parse_succeeded(parse(yaml, "yaml")));
}

// ============================================================================
// TOML Parser Robustness Tests
// ============================================================================

class TomlParserRobustnessTest : public ParserTestBase {};

TEST_F(TomlParserRobustnessTest, EmptyInput) {
    TestEmptyInput("toml");
}

TEST_F(TomlParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("key = \"value\"", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("key = 123", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("key = true", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("key = []", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("[section]", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("[[array]]", "toml")));
}

TEST_F(TomlParserRobustnessTest, DeepNestedInlineTables) {
    for (int depth : {10, 50, 100, 200, 500}) {
        std::string nested = nested_toml_tables(depth);
        TestDoesNotCrash(nested.c_str(), "toml");
    }
}

TEST_F(TomlParserRobustnessTest, ManyKeys) {
    auto generate = [](int count) {
        std::string toml;
        for (int i = 0; i < count; i++) {
            toml += "key" + std::to_string(i) + " = " + std::to_string(i) + "\n";
        }
        return toml;
    };
    
    for (int count : {100, 1000, 10000}) {
        TestDoesNotCrash(generate(count).c_str(), "toml");
    }
}

TEST_F(TomlParserRobustnessTest, MalformedInputs) {
    TestDoesNotCrash(malformed::TOML_UNCLOSED_STRING, "toml");
    TestDoesNotCrash(malformed::TOML_INVALID_KEY, "toml");
    TestDoesNotCrash(malformed::TOML_DUPLICATE_KEY, "toml");
    TestDoesNotCrash(malformed::TOML_INVALID_DATE, "toml");
}

TEST_F(TomlParserRobustnessTest, Utf8Content) {
    EXPECT_TRUE(parse_succeeded(parse("key = \"你好世界\"", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("key = \"🎉\"", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("key = \"café\"", "toml")));
}

TEST_F(TomlParserRobustnessTest, DateTimeFormats) {
    EXPECT_TRUE(parse_succeeded(parse("dt = 2024-01-15T10:30:00Z", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("dt = 2024-01-15T10:30:00+08:00", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("d = 2024-01-15", "toml")));
    EXPECT_TRUE(parse_succeeded(parse("t = 10:30:00", "toml")));
}

// ============================================================================
// HTML Parser Robustness Tests
// ============================================================================

class HtmlParserRobustnessTest : public ParserTestBase {};

TEST_F(HtmlParserRobustnessTest, EmptyInput) {
    // HTML parser returns an empty DOM tree for empty input (valid behavior)
    Input* input = parse("", "html");
    ASSERT_NE(input, nullptr) << "HTML parser crashed on empty input";
    // HTML returns non-null for empty input (creates minimal DOM structure)
}

TEST_F(HtmlParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("<html></html>", "html")));
    EXPECT_TRUE(parse_succeeded(parse("<p>text</p>", "html")));
    EXPECT_TRUE(parse_succeeded(parse("<br>", "html")));  // Self-closing
    EXPECT_TRUE(parse_succeeded(parse("<img src=\"x\">", "html")));  // Void element
}

TEST_F(HtmlParserRobustnessTest, DeepNesting) {
    // HTML should handle deep nesting
    auto nested_divs = [](int depth) {
        std::string html;
        for (int i = 0; i < depth; i++) html += "<div>";
        html += "content";
        for (int i = 0; i < depth; i++) html += "</div>";
        return html;
    };
    
    for (int depth : {10, 100, 256, 300}) {
        TestDoesNotCrash(nested_divs(depth).c_str(), "html");
    }
}

TEST_F(HtmlParserRobustnessTest, MalformedHtml) {
    // HTML parser should be lenient
    TestDoesNotCrash("<p>unclosed paragraph", "html");
    TestDoesNotCrash("<div><span></div></span>", "html");  // Misnested
    TestDoesNotCrash("<p>text<p>text", "html");  // Implicit close
    TestDoesNotCrash("<input type=text>", "html");  // Unquoted attr
}

TEST_F(HtmlParserRobustnessTest, Utf8Content) {
    TestUtf8Content("<p>你好世界</p>", "html");
    TestUtf8Content("<p>🎉🚀💻</p>", "html");
    TestUtf8Content("<p>café résumé</p>", "html");
}

TEST_F(HtmlParserRobustnessTest, Scripts) {
    // Scripts with < and > should not confuse parser
    const char* html = "<script>if (a < b && c > d) {}</script>";
    TestDoesNotCrash(html, "html");
}

TEST_F(HtmlParserRobustnessTest, Styles) {
    const char* html = "<style>p { color: red; }</style><p>text</p>";
    TestDoesNotCrash(html, "html");
}

// ============================================================================
// Markdown Parser Robustness Tests
// ============================================================================

class MarkdownParserRobustnessTest : public ParserTestBase {};

TEST_F(MarkdownParserRobustnessTest, EmptyInput) {
    // Markdown parser returns an empty document for empty input (valid behavior)
    Input* input = parse("", "markdown");
    ASSERT_NE(input, nullptr) << "Markdown parser crashed on empty input";
    // Markdown returns non-null for empty input (creates empty document)
}

TEST_F(MarkdownParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("# Heading", "markdown")));
    EXPECT_TRUE(parse_succeeded(parse("paragraph text", "markdown")));
    EXPECT_TRUE(parse_succeeded(parse("- list item", "markdown")));
    EXPECT_TRUE(parse_succeeded(parse("```\ncode\n```", "markdown")));
}

TEST_F(MarkdownParserRobustnessTest, DeepNestedLists) {
    auto nested_list = [](int depth) {
        std::string md;
        for (int i = 0; i < depth; i++) {
            md += std::string(i * 2, ' ') + "- item\n";
        }
        return md;
    };
    
    for (int depth : {10, 50, 100, 200}) {
        TestDoesNotCrash(nested_list(depth).c_str(), "markdown");
    }
}

TEST_F(MarkdownParserRobustnessTest, LargeDocument) {
    auto generate = [](int paragraphs) {
        std::string md;
        for (int i = 0; i < paragraphs; i++) {
            md += "Paragraph " + std::to_string(i) + " with some text content.\n\n";
        }
        return md;
    };
    
    for (int count : {100, 1000, 5000}) {
        TestDoesNotCrash(generate(count).c_str(), "markdown");
    }
}

TEST_F(MarkdownParserRobustnessTest, Utf8Content) {
    TestUtf8Content("# 你好世界", "markdown");
    TestUtf8Content("这是一段中文内容", "markdown");
    TestUtf8Content("- 列表项 🎉", "markdown");
}

TEST_F(MarkdownParserRobustnessTest, ComplexFormatting) {
    const char* md = "**bold** _italic_ ~~strike~~ `code` [link](url)";
    EXPECT_TRUE(parse_succeeded(parse(md, "markdown")));
}

TEST_F(MarkdownParserRobustnessTest, Tables) {
    const char* md = "| A | B |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |";
    EXPECT_TRUE(parse_succeeded(parse(md, "markdown")));
}

// ============================================================================
// CSV Parser Robustness Tests
// ============================================================================

class CsvParserRobustnessTest : public ParserTestBase {};

TEST_F(CsvParserRobustnessTest, EmptyInput) {
    TestEmptyInput("csv");
}

TEST_F(CsvParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("a,b,c", "csv")));
    EXPECT_TRUE(parse_succeeded(parse("a,b,c\n1,2,3", "csv")));
    EXPECT_TRUE(parse_succeeded(parse("\"quoted\",value", "csv")));
}

TEST_F(CsvParserRobustnessTest, LargeDataset) {
    auto generate = [](int rows, int cols) {
        std::string csv;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (c > 0) csv += ",";
                csv += "r" + std::to_string(r) + "c" + std::to_string(c);
            }
            csv += "\n";
        }
        return csv;
    };
    
    for (auto [rows, cols] : std::vector<std::pair<int,int>>{{100, 10}, {1000, 50}, {10000, 10}}) {
        TestDoesNotCrash(generate(rows, cols).c_str(), "csv");
    }
}

TEST_F(CsvParserRobustnessTest, QuotedFields) {
    // Fields with embedded quotes, commas, newlines
    EXPECT_TRUE(parse_succeeded(parse("\"with,comma\",normal", "csv")));
    EXPECT_TRUE(parse_succeeded(parse("\"with\"\"quote\",normal", "csv")));
    EXPECT_TRUE(parse_succeeded(parse("\"with\nnewline\",normal", "csv")));
}

TEST_F(CsvParserRobustnessTest, Utf8Content) {
    TestUtf8Content("名前,年齢\n田中,30", "csv");
    TestUtf8Content("emoji,😀\ntest,🎉", "csv");
}

TEST_F(CsvParserRobustnessTest, UnevenRows) {
    // Rows with different column counts
    TestDoesNotCrash("a,b,c\n1,2\n1,2,3,4", "csv");
}

// ============================================================================
// Mark Language Parser Robustness Tests
// ============================================================================

class MarkParserRobustnessTest : public ParserTestBase {};

TEST_F(MarkParserRobustnessTest, EmptyInput) {
    TestEmptyInput("mark");
}

TEST_F(MarkParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("(tag)", "mark")));
    EXPECT_TRUE(parse_succeeded(parse("(tag content)", "mark")));
    EXPECT_TRUE(parse_succeeded(parse("(tag :attr val)", "mark")));
}

TEST_F(MarkParserRobustnessTest, DeepNesting) {
    auto nested_mark = [](int depth) {
        std::string mark;
        for (int i = 0; i < depth; i++) mark += "(e ";
        mark += "x";
        for (int i = 0; i < depth; i++) mark += ")";
        return mark;
    };
    
    for (int depth : {10, 100, 200, 256}) {
        TestDoesNotCrash(nested_mark(depth).c_str(), "mark");
    }
}

TEST_F(MarkParserRobustnessTest, Utf8Content) {
    TestUtf8Content("(p 你好世界)", "mark");
    TestUtf8Content("(emoji 🎉🚀)", "mark");
}

// ============================================================================
// RTF Parser Robustness Tests
// ============================================================================

class RtfParserRobustnessTest : public ParserTestBase {};

TEST_F(RtfParserRobustnessTest, EmptyInput) {
    TestEmptyInput("rtf");
}

TEST_F(RtfParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("{\\rtf1}", "rtf")));
    EXPECT_TRUE(parse_succeeded(parse("{\\rtf1 Hello}", "rtf")));
    EXPECT_TRUE(parse_succeeded(parse("{\\rtf1\\ansi}", "rtf")));
}

TEST_F(RtfParserRobustnessTest, DeepNesting) {
    auto nested_rtf = [](int depth) {
        std::string rtf = "{\\rtf1";
        for (int i = 0; i < depth; i++) rtf += "{";
        rtf += "text";
        for (int i = 0; i < depth; i++) rtf += "}";
        rtf += "}";
        return rtf;
    };
    
    for (int depth : {10, 100, 200, 256}) {
        TestDoesNotCrash(nested_rtf(depth).c_str(), "rtf");
    }
}

TEST_F(RtfParserRobustnessTest, MalformedRtf) {
    // Unclosed braces
    TestDoesNotCrash("{\\rtf1 {text", "rtf");
    // Missing rtf header
    TestDoesNotCrash("{some text}", "rtf");
}

// ============================================================================
// JSX Parser Robustness Tests
// ============================================================================

class JsxParserRobustnessTest : public ParserTestBase {};

TEST_F(JsxParserRobustnessTest, EmptyInput) {
    TestEmptyInput("jsx");
}

TEST_F(JsxParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("<div/>", "jsx")));
    EXPECT_TRUE(parse_succeeded(parse("<div></div>", "jsx")));
    EXPECT_TRUE(parse_succeeded(parse("<Component prop={value}/>", "jsx")));
}

TEST_F(JsxParserRobustnessTest, DeepNesting) {
    auto nested_jsx = [](int depth) {
        std::string jsx;
        for (int i = 0; i < depth; i++) jsx += "<div>";
        jsx += "text";
        for (int i = 0; i < depth; i++) jsx += "</div>";
        return jsx;
    };
    
    for (int depth : {10, 100, 256, 300}) {
        TestDoesNotCrash(nested_jsx(depth).c_str(), "jsx");
    }
}

TEST_F(JsxParserRobustnessTest, Expressions) {
    // JSX with embedded expressions
    EXPECT_TRUE(parse_succeeded(parse("<div>{value}</div>", "jsx")));
    EXPECT_TRUE(parse_succeeded(parse("<div>{a + b}</div>", "jsx")));
    EXPECT_TRUE(parse_succeeded(parse("<div>{fn(x)}</div>", "jsx")));
}

// ============================================================================
// INI Parser Robustness Tests
// ============================================================================

class IniParserRobustnessTest : public ParserTestBase {};

TEST_F(IniParserRobustnessTest, EmptyInput) {
    TestEmptyInput("ini");
}

TEST_F(IniParserRobustnessTest, MinimalValidInputs) {
    EXPECT_TRUE(parse_succeeded(parse("key=value", "ini")));
    EXPECT_TRUE(parse_succeeded(parse("[section]\nkey=value", "ini")));
    EXPECT_TRUE(parse_succeeded(parse("; comment\nkey=value", "ini")));
}

TEST_F(IniParserRobustnessTest, ManyKeys) {
    auto generate = [](int count) {
        std::string ini = "[section]\n";
        for (int i = 0; i < count; i++) {
            ini += "key" + std::to_string(i) + "=value" + std::to_string(i) + "\n";
        }
        return ini;
    };
    
    for (int count : {100, 1000, 10000}) {
        TestDoesNotCrash(generate(count).c_str(), "ini");
    }
}

TEST_F(IniParserRobustnessTest, ManySections) {
    auto generate = [](int count) {
        std::string ini;
        for (int i = 0; i < count; i++) {
            ini += "[section" + std::to_string(i) + "]\n";
            ini += "key=value\n";
        }
        return ini;
    };
    
    for (int count : {100, 1000}) {
        TestDoesNotCrash(generate(count).c_str(), "ini");
    }
}

TEST_F(IniParserRobustnessTest, Utf8Content) {
    TestUtf8Content("[节]\n键=你好", "ini");
    TestUtf8Content("[section]\nkey=🎉", "ini");
}

// ============================================================================
// PDF Parser Robustness Tests (limits already increased)
// ============================================================================

class PdfParserRobustnessTest : public ParserTestBase {};

TEST_F(PdfParserRobustnessTest, EmptyInput) {
    // PDF parser returns structure even for empty input (valid behavior)
    Input* input = parse("", "pdf");
    ASSERT_NE(input, nullptr) << "PDF parser crashed on empty input";
    // PDF returns non-null for empty/invalid input (may contain error info)
}

TEST_F(PdfParserRobustnessTest, InvalidPdf) {
    // Non-PDF content should not crash
    TestDoesNotCrash("not a pdf file", "pdf");
    TestDoesNotCrash("%PDF-invalid", "pdf");
}

// ============================================================================
// VCF (vCard) Parser Robustness Tests
// ============================================================================

class VcfParserRobustnessTest : public ParserTestBase {};

TEST_F(VcfParserRobustnessTest, EmptyInput) {
    // VCF parser returns empty card list for empty input (valid behavior)
    Input* input = parse("", "vcf");
    ASSERT_NE(input, nullptr) << "VCF parser crashed on empty input";
    // VCF returns non-null for empty input (creates empty card list)
}

TEST_F(VcfParserRobustnessTest, MinimalValidInputs) {
    const char* minimal = "BEGIN:VCARD\nVERSION:3.0\nN:Doe;John\nFN:John Doe\nEND:VCARD";
    EXPECT_TRUE(parse_succeeded(parse(minimal, "vcf")));
}

TEST_F(VcfParserRobustnessTest, ManyCards) {
    auto generate = [](int count) {
        std::string vcf;
        for (int i = 0; i < count; i++) {
            vcf += "BEGIN:VCARD\nVERSION:3.0\n";
            vcf += "N:Person" + std::to_string(i) + ";Name\n";
            vcf += "FN:Name Person" + std::to_string(i) + "\n";
            vcf += "END:VCARD\n";
        }
        return vcf;
    };
    
    for (int count : {10, 100, 1000}) {
        TestDoesNotCrash(generate(count).c_str(), "vcf");
    }
}

TEST_F(VcfParserRobustnessTest, ShortPropertyNames) {
    // Test that short property names like N, FN, TEL work (was a bug)
    const char* vcf = 
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "N:Doe;John\n"
        "FN:John Doe\n"
        "TEL:+1234567890\n"
        "END:VCARD";
    
    Input* input = parse(vcf, "vcf");
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(parse_succeeded(input)) 
        << "VCF with short property names (N, FN, TEL) should parse successfully";
}

TEST_F(VcfParserRobustnessTest, Utf8Content) {
    const char* vcf = 
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "N:田中;太郎\n"
        "FN:田中太郎\n"
        "END:VCARD";
    
    TestUtf8Content(vcf, "vcf");
}

// ============================================================================
// ICS (iCalendar) Parser Robustness Tests
// ============================================================================

class IcsParserRobustnessTest : public ParserTestBase {};

TEST_F(IcsParserRobustnessTest, EmptyInput) {
    // ICS parser returns empty calendar for empty input (valid behavior)
    Input* input = parse("", "ics");
    ASSERT_NE(input, nullptr) << "ICS parser crashed on empty input";
    // ICS returns non-null for empty input (creates empty calendar structure)
}

TEST_F(IcsParserRobustnessTest, MinimalValidInputs) {
    const char* minimal = 
        "BEGIN:VCALENDAR\n"
        "VERSION:2.0\n"
        "BEGIN:VEVENT\n"
        "DTSTART:20240115T100000Z\n"
        "DTEND:20240115T110000Z\n"
        "SUMMARY:Test Event\n"
        "END:VEVENT\n"
        "END:VCALENDAR";
    
    EXPECT_TRUE(parse_succeeded(parse(minimal, "ics")));
}

TEST_F(IcsParserRobustnessTest, DateTimeFields) {
    // Test that 2-digit date/time fields work (was a bug where fields < 4 chars were skipped)
    const char* ics = 
        "BEGIN:VCALENDAR\n"
        "VERSION:2.0\n"
        "BEGIN:VEVENT\n"
        "DTSTART:20240101T093000Z\n"  // Month=01, Day=01, Hour=09, Min=30, Sec=00
        "DTEND:20240115T173059Z\n"    // Various 2-digit values
        "SUMMARY:Event\n"
        "END:VEVENT\n"
        "END:VCALENDAR";
    
    Input* input = parse(ics, "ics");
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(parse_succeeded(input))
        << "ICS with 2-digit date/time fields should parse successfully";
}

TEST_F(IcsParserRobustnessTest, ManyEvents) {
    auto generate = [](int count) {
        std::string ics = "BEGIN:VCALENDAR\nVERSION:2.0\n";
        for (int i = 0; i < count; i++) {
            ics += "BEGIN:VEVENT\n";
            ics += "DTSTART:20240115T100000Z\n";
            ics += "DTEND:20240115T110000Z\n";
            ics += "SUMMARY:Event " + std::to_string(i) + "\n";
            ics += "END:VEVENT\n";
        }
        ics += "END:VCALENDAR";
        return ics;
    };
    
    for (int count : {10, 100, 500}) {
        TestDoesNotCrash(generate(count).c_str(), "ics");
    }
}

TEST_F(IcsParserRobustnessTest, Utf8Content) {
    const char* ics = 
        "BEGIN:VCALENDAR\n"
        "VERSION:2.0\n"
        "BEGIN:VEVENT\n"
        "DTSTART:20240115T100000Z\n"
        "SUMMARY:会议 🎉\n"
        "DESCRIPTION:这是一个测试事件\n"
        "END:VEVENT\n"
        "END:VCALENDAR";
    
    TestUtf8Content(ics, "ics");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
