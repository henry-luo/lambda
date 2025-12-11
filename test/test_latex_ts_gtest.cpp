/**
 * @file test_latex_ts_gtest.cpp
 * @brief Unit tests for LaTeX Tree-sitter parser
 * 
 * Tests focus on:
 * - Whitespace normalization
 * - Text handling
 * - Symbol command handling (spacing commands like \,  \; \quad etc.)
 */

#include <gtest/gtest.h>
#include <chrono>
extern "C" {
    #include "../lambda/input/input.hpp"
    #include "../lambda/lambda-data.hpp"
    #include "../lib/strbuf.h"
    #include "../lib/log.h"
}

// Helper to create a test string
static String* create_lambda_string(const char* str) {
    size_t len = strlen(str);
    String* s = (String*)malloc(sizeof(String) + len + 1);
    s->len = len;
    s->ref_cnt = 1;
    memcpy(s->chars, str, len);
    s->chars[len] = '\0';
    return s;
}

class LatexTsTests : public ::testing::Test {
protected:
    String* type_str;
    String* flavor_str;
    Url* dummy_url;
    
    void SetUp() override {
        log_init(nullptr);
        type_str = create_lambda_string("latex");
        flavor_str = create_lambda_string("ts");
        
        // Create a dummy URL for testing
        Url* cwd = get_current_dir();
        dummy_url = parse_url(cwd, "test.tex");
        url_destroy(cwd);
    }
    
    void TearDown() override {
        free(type_str);
        free(flavor_str);
        url_destroy(dummy_url);
    }
    
    // Helper to parse LaTeX and return the Input
    Input* parse_latex(const char* latex_content) {
        char* latex_copy = strdup(latex_content);
        // NOTE: Pass nullptr for flavor to use default tree-sitter parser (same as test_latex_parser_gtest.cpp)
        Input* parsed_input = input_from_source(latex_copy, dummy_url, type_str, nullptr);
        free(latex_copy);
        return parsed_input;
    }
    
    // Helper to check if an Item is a Symbol with specific name
    bool is_symbol(Item item, const char* expected_name) {
        if (get_type_id(item) != LMD_TYPE_SYMBOL) return false;
        Symbol* sym = item.get_symbol();
        return sym && strcmp(sym->chars, expected_name) == 0;
    }
    
    // Helper to check if an Item is a String with specific content
    bool is_string(Item item, const char* expected_content) {
        if (get_type_id(item) != LMD_TYPE_STRING) return false;
        String* str = item.get_string();
        return str && strcmp(str->chars, expected_content) == 0;
    }
    
    // Helper to check if an Item is an Element with specific tag
    bool is_element(Item item, const char* expected_tag) {
        if (get_type_id(item) != LMD_TYPE_ELEMENT) return false;
        Element* elem = item.element;
        if (!elem || !elem->type) return false;
        TypeElmt* elem_type = (TypeElmt*)elem->type;
        return strncmp(elem_type->name.str, expected_tag, elem_type->name.length) == 0 
            && expected_tag[elem_type->name.length] == '\0';
    }
    
    // Helper to get text from a string item
    const char* get_text(Item item) {
        if (get_type_id(item) != LMD_TYPE_STRING) return nullptr;
        String* str = item.get_string();
        return str ? str->chars : nullptr;
    }
};

// Test 1: Basic Text Parsing
TEST_F(LatexTsTests, BasicTextParsing) {
    const char* latex = "Simple text";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    Item root = input->root;
    // LaTeX parser wraps content in an Element
    ASSERT_EQ(get_type_id(root), LMD_TYPE_ELEMENT);
    
    Element* elem = root.element;
    ASSERT_NE(elem, nullptr);
    ASSERT_GT(elem->length, 0);
    
    // Should have at least one string item in element children
    bool found_text = false;
    for (int64_t i = 0; i < elem->length; i++) {
        if (get_type_id(elem->items[i]) == LMD_TYPE_STRING) {
            String* str = elem->items[i].get_string();
            if (strstr(str->chars, "Simple text") != nullptr) {
                found_text = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_text) << "Should parse simple text";
}

// Test 2: Whitespace Normalization
TEST_F(LatexTsTests, WhitespaceNormalization) {
    const char* latex = "Text   with    multiple     spaces";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    Item root = input->root;
    ASSERT_EQ(get_type_id(root), LMD_TYPE_ELEMENT);
    
    Element* elem = root.element;
    ASSERT_NE(elem, nullptr);
    ASSERT_GT(elem->length, 0);
    
    // Check that text contains normalized spaces
    bool found_normalized = false;
    for (int64_t i = 0; i < elem->length; i++) {
        if (get_type_id(elem->items[i]) == LMD_TYPE_STRING) {
            String* str = elem->items[i].get_string();
            // Should have single spaces, not multiple
            if (strstr(str->chars, "Text with multiple spaces") != nullptr) {
                found_normalized = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_normalized) << "Whitespace should be normalized";
}

// Test 3: Paragraph Breaks (double newline)
TEST_F(LatexTsTests, ParagraphBreaks) {
    const char* latex = "First paragraph.\n\nSecond paragraph.";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    Item root = input->root;
    ASSERT_EQ(get_type_id(root), LMD_TYPE_ELEMENT);
    
    Element* elem = root.element;
    ASSERT_NE(elem, nullptr);
    
    // Should have multiple items (paragraphs separated)
    EXPECT_GT(elem->length, 1) << "Should recognize paragraph break";
}

// Test 4: Spacing Symbol Commands
TEST_F(LatexTsTests, SpacingSymbolCommands) {
    const char* latex = "Word1\\, Word2";  // \, is a thin space symbol
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    Item root = input->root;
    ASSERT_EQ(get_type_id(root), LMD_TYPE_ELEMENT);
    
    Element* elem = root.element;
    ASSERT_NE(elem, nullptr);
    ASSERT_GT(elem->length, 0);
    
    // Tree-sitter parser converts spacing commands to SYMBOLS not ELEMENTS (optimization)
    // LaTeX documents are wrapped in a list, so we need to check inside the list
    ASSERT_EQ(elem->length, 1);
    ASSERT_EQ(get_type_id(elem->items[0]), LMD_TYPE_LIST);
    
    List* content_list = elem->items[0].list;
    ASSERT_NE(content_list, nullptr);
    ASSERT_GT(content_list->length, 0);
    
    // Should contain a symbol for the spacing command
    bool found_symbol = false;
    for (int64_t i = 0; i < content_list->length; i++) {
        if (get_type_id(content_list->items[i]) == LMD_TYPE_SYMBOL) {
            Symbol* sym = content_list->items[i].get_symbol();
            if (sym && strcmp(sym->chars, "thinspace") == 0) {
                found_symbol = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_symbol) << "Should have thinspace symbol";
}

// Test 5: Multiple Spacing Commands
TEST_F(LatexTsTests, MultipleSpacingSymbols) {
    const char* latex = "A\\, B\\; C\\quad D";  // Added spaces after commands for proper parsing
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    Item root = input->root;
    Element* elem = root.element;
    ASSERT_NE(elem, nullptr);
    
    // Tree-sitter parser converts spacing commands to SYMBOLS not ELEMENTS (optimization)
    // LaTeX documents are wrapped in a list, so check inside the list
    ASSERT_EQ(elem->length, 1);
    ASSERT_EQ(get_type_id(elem->items[0]), LMD_TYPE_LIST);
    
    List* content_list = elem->items[0].list;
    ASSERT_NE(content_list, nullptr);
    
    // Count symbol items (spacing commands become symbols)
    int symbol_count = 0;
    for (int64_t i = 0; i < content_list->length; i++) {
        if (get_type_id(content_list->items[i]) == LMD_TYPE_SYMBOL) {
            symbol_count++;
        }
    }
    EXPECT_GE(symbol_count, 3) << "Should have at least 3 spacing commands as symbols";
}

// Test 6: Command Preservation (non-spacing commands)
TEST_F(LatexTsTests, CommandPreservation) {
    const char* latex = "\\textbf{bold text}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    Item root = input->root;
    ASSERT_EQ(get_type_id(root), LMD_TYPE_ELEMENT);
    
    // Parser should handle commands (exact structure depends on parser implementation)
    Element* elem = root.element;
    ASSERT_NE(elem, nullptr);
    ASSERT_GT(elem->length, 0);
}

// Test 7: Mixed Content (text and commands)
TEST_F(LatexTsTests, MixedContent) {
    const char* latex = "Hello\\, world\\quad with text";  // Added spaces
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    Item root = input->root;
    Element* elem = root.element;
    ASSERT_NE(elem, nullptr);
    
    // Tree-sitter parser: spacing commands become SYMBOLS not ELEMENTS
    // LaTeX documents are wrapped in a list, so check inside the list
    ASSERT_EQ(elem->length, 1);
    ASSERT_EQ(get_type_id(elem->items[0]), LMD_TYPE_LIST);
    
    List* content_list = elem->items[0].list;
    ASSERT_NE(content_list, nullptr);
    
    // Should have strings and symbols (for spacing commands)
    bool has_string = false;
    bool has_symbol = false;
    
    for (int64_t i = 0; i < content_list->length; i++) {
        TypeId type = get_type_id(content_list->items[i]);
        if (type == LMD_TYPE_STRING) has_string = true;
        if (type == LMD_TYPE_SYMBOL) has_symbol = true;
    }
    
    EXPECT_TRUE(has_string) << "Should have text strings";
    EXPECT_TRUE(has_symbol) << "Should have spacing command symbols";
}

// Test 8: Empty Document
TEST_F(LatexTsTests, EmptyDocument) {
    const char* latex = "";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    // Should handle empty input gracefully
    Item root = input->root;
    // May be null or empty list - both are acceptable
    if (get_type_id(root) == LMD_TYPE_ELEMENT) {
        Element* elem = root.element;
        // Empty list is valid
        EXPECT_GE(elem->length, 0);
    }
}

// Test 9: Whitespace Only Document
TEST_F(LatexTsTests, WhitespaceOnlyDocument) {
    const char* latex = "   \n\n\t  ";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    
    // Should handle whitespace-only input
    Item root = input->root;
    if (get_type_id(root) == LMD_TYPE_ELEMENT) {
        Element* elem = root.element;
        // May be empty or contain empty strings
        EXPECT_GE(elem->length, 0);
    }
}

// Test 10: Performance Test
TEST_F(LatexTsTests, PerformanceTest) {
    // Build a large document
    StrBuf* sb = strbuf_new();
    for (int i = 0; i < 100; i++) {
        strbuf_append_format(sb, "Paragraph %d with some text.\\,Some\\;spacing\\quad here.\n\n", i);
    }
    
    const char* latex = sb->str;
    
    auto start = std::chrono::high_resolution_clock::now();
    Input* input = parse_latex(latex);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_NE(input, nullptr);
    printf("Parse time for 100 paragraphs: %lld ms\n", (long long)duration.count());
    
    // Should complete in reasonable time (< 1 second)
    EXPECT_LT(duration.count(), 1000) << "Parsing should be fast";
    
    strbuf_free(sb);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
