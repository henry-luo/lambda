/**
 * Parser Robustness Test Common Infrastructure
 * 
 * Provides shared utilities for testing parser safety and robustness:
 * - Empty/null input handling
 * - Deep nesting (stack safety)
 * - Large input handling
 * - Malformed input recovery
 * - UTF-8 edge cases
 */

#ifndef PARSER_TEST_COMMON_HPP
#define PARSER_TEST_COMMON_HPP

#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include "../../lambda/lambda-data.hpp"
#include "../../lambda/input/input.hpp"
#include "../../lib/log.h"
#include "../../lib/url.h"

extern "C" {
    Input* input_from_source(const char* source, Url* abs_url, String* type, String* flavor);
}

namespace parser_test {

// Helper to create Lambda String from C string
inline String* make_string(const char* text) {
    if (!text) return nullptr;
    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return nullptr;
    result->len = len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);
    return result;
}

// RAII wrapper for Lambda strings
struct LambdaString {
    String* str;
    explicit LambdaString(const char* text) : str(make_string(text)) {}
    ~LambdaString() { if (str) free(str); }
    operator String*() const { return str; }
};

// Parse helper with format type
inline Input* parse(const char* source, const char* type, const char* flavor = nullptr) {
    LambdaString type_str(type);
    LambdaString flavor_str(flavor);
    return input_from_source(source, nullptr, type_str, flavor_str);
}

// Check if parse succeeded (non-null root)
inline bool parse_succeeded(Input* input) {
    return input != nullptr && input->root.item != ITEM_NULL && input->root.item != ITEM_ERROR;
}

// Check if result is null (empty input handling)
inline bool is_null_result(Input* input) {
    return input != nullptr && input->root.item == ITEM_NULL;
}

// Generate deeply nested structure for a given format
inline std::string generate_nested(const char* open, const char* close, int depth) {
    std::string result;
    for (int i = 0; i < depth; i++) result += open;
    for (int i = 0; i < depth; i++) result += close;
    return result;
}

// Generate deeply nested JSON arrays
inline std::string nested_json_arrays(int depth) {
    return generate_nested("[", "]", depth);
}

// Generate deeply nested JSON objects
inline std::string nested_json_objects(int depth) {
    std::string result;
    for (int i = 0; i < depth; i++) {
        result += "{\"k\":";
    }
    result += "1";
    for (int i = 0; i < depth; i++) {
        result += "}";
    }
    return result;
}

// Generate deeply nested XML elements
inline std::string nested_xml_elements(int depth) {
    std::string result;
    for (int i = 0; i < depth; i++) {
        result += "<e>";
    }
    result += "x";
    for (int i = 0; i < depth; i++) {
        result += "</e>";
    }
    return result;
}

// Generate deeply nested YAML indentation
inline std::string nested_yaml_maps(int depth) {
    std::string result;
    for (int i = 0; i < depth; i++) {
        result += std::string(i * 2, ' ') + "k:\n";
    }
    result += std::string(depth * 2, ' ') + "v: 1\n";
    return result;
}

// Generate deeply nested TOML inline tables
inline std::string nested_toml_tables(int depth) {
    std::string result = "x = ";
    for (int i = 0; i < depth; i++) {
        result += "{k = ";
    }
    result += "1";
    for (int i = 0; i < depth; i++) {
        result += "}";
    }
    return result;
}

// Generate large repetitive content
inline std::string large_json_array(int count) {
    std::string result = "[";
    for (int i = 0; i < count; i++) {
        if (i > 0) result += ",";
        result += std::to_string(i);
    }
    result += "]";
    return result;
}

// UTF-8 test strings
namespace utf8 {
    // Basic ASCII
    constexpr const char* ASCII = "hello world";
    
    // 2-byte UTF-8 (Latin Extended, Greek, Cyrillic)
    constexpr const char* LATIN_EXT = "café résumé naïve";
    constexpr const char* GREEK = "αβγδε";
    constexpr const char* CYRILLIC = "Привет";
    
    // 3-byte UTF-8 (CJK, symbols)
    constexpr const char* CJK = "你好世界";
    constexpr const char* JAPANESE = "こんにちは";
    constexpr const char* KOREAN = "안녕하세요";
    
    // 4-byte UTF-8 (emoji, rare CJK)
    constexpr const char* EMOJI = "🎉🚀💻🔥";
    constexpr const char* EMOJI_SEQUENCE = "👨‍👩‍👧‍👦";  // Family emoji with ZWJ
    constexpr const char* MUSICAL = "𝄞𝄢";  // Musical symbols
    
    // Mixed content
    constexpr const char* MIXED = "Hello 你好 🌍 café";
    
    // Edge cases
    constexpr const char* BOM = "\xEF\xBB\xBF" "text";  // UTF-8 BOM
    constexpr const char* NULL_CHAR = "a\0b";  // Embedded null (use with length)
    
    // Invalid UTF-8 sequences (for error handling tests)
    constexpr const char* INVALID_CONT = "\x80\x81";  // Continuation bytes without start
    constexpr const char* TRUNCATED_2 = "\xC3";  // Start of 2-byte but truncated
    constexpr const char* TRUNCATED_3 = "\xE4\xB8";  // Start of 3-byte but truncated
    constexpr const char* TRUNCATED_4 = "\xF0\x9F";  // Start of 4-byte but truncated
    constexpr const char* OVERLONG = "\xC0\xAF";  // Overlong encoding of '/'
}

// Malformed input generators
namespace malformed {
    // JSON
    constexpr const char* JSON_UNCLOSED_ARRAY = "[1, 2, 3";
    constexpr const char* JSON_UNCLOSED_OBJECT = "{\"key\": \"value\"";
    constexpr const char* JSON_UNCLOSED_STRING = "{\"key\": \"value";
    constexpr const char* JSON_TRAILING_COMMA = "[1, 2, 3,]";
    constexpr const char* JSON_MISSING_COLON = "{\"key\" \"value\"}";
    constexpr const char* JSON_MISSING_COMMA = "[1 2 3]";
    constexpr const char* JSON_INVALID_NUMBER = "[1.2.3]";
    constexpr const char* JSON_UNQUOTED_KEY = "{key: \"value\"}";
    
    // XML
    constexpr const char* XML_UNCLOSED_TAG = "<root><child>";
    constexpr const char* XML_MISMATCHED_TAGS = "<root></child>";
    constexpr const char* XML_UNCLOSED_ATTR = "<root attr=\"value>";
    constexpr const char* XML_DUPLICATE_ATTR = "<root a=\"1\" a=\"2\"/>";
    constexpr const char* XML_INVALID_NAME = "<123invalid/>";
    
    // YAML
    constexpr const char* YAML_BAD_INDENT = "a:\n  b:\n c:";  // Inconsistent indent
    constexpr const char* YAML_TAB_INDENT = "a:\n\tb: 1";  // Tab indent (discouraged)
    constexpr const char* YAML_UNCLOSED_QUOTE = "key: \"value";
    constexpr const char* YAML_INVALID_KEY = "- key: value\n  : invalid";
    
    // TOML
    constexpr const char* TOML_UNCLOSED_STRING = "key = \"value";
    constexpr const char* TOML_INVALID_KEY = "123 = \"value\"";
    constexpr const char* TOML_DUPLICATE_KEY = "key = 1\nkey = 2";
    constexpr const char* TOML_INVALID_DATE = "date = 2024-13-45";
}

// Base test fixture with common setup
class ParserTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(nullptr);
    }
    
    void TearDown() override {
        // Cleanup handled by InputManager singleton
    }
    
    // Test that parsing doesn't crash and returns something
    void TestDoesNotCrash(const char* source, const char* type, const char* flavor = nullptr) {
        Input* input = parse(source, type, flavor);
        // Just verify we didn't crash - input may be null for invalid input
        ASSERT_NE(input, nullptr) << "Parser returned null Input pointer";
    }
    
    // Test that empty/null input returns null Item without crash
    void TestEmptyInput(const char* type, const char* flavor = nullptr) {
        // Empty string
        {
            Input* input = parse("", type, flavor);
            ASSERT_NE(input, nullptr) << "Parser crashed on empty string";
            EXPECT_TRUE(is_null_result(input)) 
                << "Empty input should return null for " << type;
        }
        
        // Whitespace only
        {
            Input* input = parse("   \n\t  ", type, flavor);
            ASSERT_NE(input, nullptr) << "Parser crashed on whitespace";
        }
    }
    
    // Test that deep nesting errors gracefully instead of crashing
    void TestDeepNesting(const std::string& nested, const char* type, 
                         const char* flavor = nullptr, int expected_depth = 512) {
        Input* input = parse(nested.c_str(), type, flavor);
        ASSERT_NE(input, nullptr) << "Parser crashed on deep nesting";
        // Parser should either succeed (for reasonable depth) or error gracefully
        // It must NOT stack overflow
    }
    
    // Test that UTF-8 content is handled
    void TestUtf8Content(const char* utf8_content, const char* type, 
                        const char* flavor = nullptr) {
        Input* input = parse(utf8_content, type, flavor);
        ASSERT_NE(input, nullptr) << "Parser crashed on UTF-8 content: " << utf8_content;
    }
};

} // namespace parser_test

#endif // PARSER_TEST_COMMON_HPP
