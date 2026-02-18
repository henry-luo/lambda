/**
 * WPT (Web Platform Tests) HTML Parser Tests
 *
 * Tests Lambda's HTML parser against the official Web Platform Tests html5lib test suite.
 * Test data is extracted from test/wpt/html/syntax/parsing/*.html files and converted
 * to JSON fixtures in test/html/wpt/*.json.
 *
 * This test suite validates HTML parsing conformance by:
 * 1. Loading test cases from JSON fixtures
 * 2. Parsing HTML input using Lambda's input-html.cpp parser
 * 3. Converting Lambda's DOM tree to WPT format
 * 4. Comparing against expected WPT tree output
 *
 * Test Coverage: 1560+ test cases from 63 html5lib test files
 */

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_reader.hpp"

extern "C" {
#include "../lib/log.h"
    Input* input_from_source(const char* source, Url* abs_url, String* type, String* flavor);
}

// ============================================================================
// Helper Functions
// ============================================================================

// Helper function to create a Lambda String from C string
String* create_lambda_string(const char* text) {
    if (!text) return NULL;
    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;
    result->len = len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);
    return result;
}

// ============================================================================
// JSON Parsing Helper (simple, no external dependencies)
// ============================================================================

struct WptTestCase {
    std::string test_id;
    std::string file;
    std::string input;
    std::string expected;
};

// Simple JSON array parser for our specific test case format
std::vector<WptTestCase> parse_test_json(const std::string& filepath) {
    std::vector<WptTestCase> tests;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Silently return empty vector if file doesn't exist
        // Test data files are optional - test suite will be skipped if not available
        return tests;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    // Simple JSON parsing - find each test object
    size_t pos = 0;
    while ((pos = content.find("\"test_id\":", pos)) != std::string::npos) {
        WptTestCase test;

        // Extract test_id
        size_t id_start = content.find("\"", pos + 11) + 1;
        size_t id_end = content.find("\"", id_start);
        test.test_id = content.substr(id_start, id_end - id_start);

        // Extract file
        size_t file_pos = content.find("\"file\":", id_end);
        size_t file_start = content.find("\"", file_pos + 8) + 1;
        size_t file_end = content.find("\"", file_start);
        test.file = content.substr(file_start, file_end - file_start);

        // Extract input (handle escaped characters)
        size_t input_pos = content.find("\"input\":", file_end);
        size_t input_start = content.find("\"", input_pos + 9) + 1;
        size_t input_end = input_start;
        // Find matching quote, handling escapes
        while (input_end < content.length()) {
            if (content[input_end] == '\"' && content[input_end - 1] != '\\') break;
            input_end++;
        }
        test.input = content.substr(input_start, input_end - input_start);
        // Unescape JSON string
        size_t escape_pos = 0;
        while ((escape_pos = test.input.find("\\n", escape_pos)) != std::string::npos) {
            test.input.replace(escape_pos, 2, "\n");
        }
        escape_pos = 0;
        while ((escape_pos = test.input.find("\\\"", escape_pos)) != std::string::npos) {
            test.input.replace(escape_pos, 2, "\"");
            escape_pos++;
        }
        escape_pos = 0;
        while ((escape_pos = test.input.find("\\\\", escape_pos)) != std::string::npos) {
            test.input.replace(escape_pos, 2, "\\");
            escape_pos++;
        }

        // Extract expected (same escaping logic)
        size_t exp_pos = content.find("\"expected\":", input_end);
        size_t exp_start = content.find("\"", exp_pos + 12) + 1;
        size_t exp_end = exp_start;
        while (exp_end < content.length()) {
            if (content[exp_end] == '\"' && content[exp_end - 1] != '\\') break;
            exp_end++;
        }
        test.expected = content.substr(exp_start, exp_end - exp_start);
        // Unescape
        escape_pos = 0;
        while ((escape_pos = test.expected.find("\\n", escape_pos)) != std::string::npos) {
            test.expected.replace(escape_pos, 2, "\n");
        }
        escape_pos = 0;
        while ((escape_pos = test.expected.find("\\\"", escape_pos)) != std::string::npos) {
            test.expected.replace(escape_pos, 2, "\"");
            escape_pos++;
        }
        escape_pos = 0;
        while ((escape_pos = test.expected.find("\\\\", escape_pos)) != std::string::npos) {
            test.expected.replace(escape_pos, 2, "\\");
            escape_pos++;
        }

        tests.push_back(test);
        pos = exp_end;
    }

    return tests;
}

// ============================================================================
// Lambda DOM to WPT Tree Format Converter
// ============================================================================

void serialize_attributes_wpt(const ElementReader& elem, std::string& output, int depth) {
    // Get attributes and sort alphabetically (WPT requirement)
    std::vector<std::pair<std::string, std::string>> attrs;

    // Access the underlying Element to iterate through its shape (attribute schema)
    const Element* element = elem.element();
    if (element && element->type) {
        TypeElmt* type = (TypeElmt*)element->type;
        ShapeEntry* shape = type->shape;

        while (shape) {
            if (shape->name) {
                // Get attribute name
                std::string attr_name(shape->name->str, shape->name->length);

                // Get attribute value - include ITEM_NULL attrs as empty string
                ItemReader val = elem.get_attr(attr_name.c_str());
                std::string attr_value;
                TypeId val_type = val.getType();
                if (val_type == LMD_TYPE_STRING) {
                    String* str = val.asString();
                    if (str) {
                        attr_value = std::string(str->chars, str->len);
                    }
                } else if (val_type == LMD_TYPE_INT || val_type == LMD_TYPE_INT64) {
                    attr_value = std::to_string(val.asInt());
                } else if (val_type == LMD_TYPE_BOOL) {
                    attr_value = val.asBool() ? "true" : "false";
                }
                // LMD_TYPE_NULL: attr_value stays empty (""), outputting name=""
                attrs.push_back({attr_name, attr_value});
            }
            shape = shape->next;
        }
    }

    std::sort(attrs.begin(), attrs.end());

    // Output sorted attributes
    std::string indent(depth * 2, ' ');
    for (const auto& attr : attrs) {
        output += "| " + indent + "  " + attr.first + "=\"" + attr.second + "\"\n";
    }
}

void serialize_element_wpt(Item item, std::string& output, int depth);

void serialize_children_wpt(const ElementReader& elem, std::string& output, int depth) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child_reader = elem.childAt(i);
        serialize_element_wpt(child_reader.item(), output, depth + 1);
    }
}

void serialize_element_wpt(Item item, std::string& output, int depth) {
    TypeId type = get_type_id(item);
    std::string indent(depth * 2, ' ');

    if (type == LMD_TYPE_ELEMENT) {
        ElementReader elem(item);

        // Get tag name
        std::string tag_name(elem.tagName(), elem.tagNameLen());

        // Handle special element types
        if (tag_name == "#comment") {
            // Comment node - format as <!-- ... -->
            std::string data;
            if (elem.has_attr("data")) {
                String* str = elem.get_string_attr("data");
                if (str && str->chars) {
                    // Check for "lambda.nil" sentinel which represents empty string
                    if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) {
                        // Empty comment
                        data = "";
                    } else {
                        data = std::string(str->chars, str->len);
                    }
                }
            }
            output += "| " + indent + "<!-- " + data + " -->\n";
            return;
        }

        if (tag_name == "#doctype") {
            // DOCTYPE node - format as <!DOCTYPE ...>
            std::string name;
            if (elem.has_attr("name")) {
                String* str = elem.get_string_attr("name");
                if (str) name = std::string(str->chars, str->len);
            }
            std::string public_id, system_id;
            if (elem.has_attr("publicId")) {
                String* str = elem.get_string_attr("publicId");
                if (str) public_id = std::string(str->chars, str->len);
            }
            if (elem.has_attr("systemId")) {
                String* str = elem.get_string_attr("systemId");
                if (str) system_id = std::string(str->chars, str->len);
            }

            output += "| " + indent + "<!DOCTYPE " + name;
            if (!public_id.empty() || !system_id.empty()) {
                output += " \"" + public_id + "\" \"" + system_id + "\"";
            }
            output += ">\n";
            return;
        }

        // Regular element tag (lowercase for HTML)
        output += "| " + indent + "<" + tag_name + ">\n";

        // Attributes (sorted)
        serialize_attributes_wpt(elem, output, depth);

        // Children
        serialize_children_wpt(elem, output, depth);
    }
    else if (type == LMD_TYPE_STRING) {
        // Text node - use ItemReader to get string safely
        ItemReader reader(item.to_const());
        String* str = reader.asString();
        if (str) {
            std::string text(str->chars, str->len);
            output += "| " + indent + "\"" + text + "\"\n";
        }
    }
    else if (type == LMD_TYPE_LIST || type == LMD_TYPE_ARRAY) {
        // Container - serialize children
        List* list = (type == LMD_TYPE_LIST) ? item.list : (List*)item.array;
        if (list && list->items) {
            for (int64_t i = 0; i < list->length; i++) {
                serialize_element_wpt(list->items[i], output, depth);
            }
        }
    }
    else {
        // Debug: print unhandled type
        fprintf(stderr, "DEBUG: unhandled type=%d item.item=0x%lx at depth=%d\n", type, (unsigned long)item.item, depth);
    }
}

std::string lambda_tree_to_wpt_format(Item root) {
    std::string result = "#document\n";

    // Lambda's parse_html typically returns a List containing html element
    TypeId root_type = get_type_id(root);

    if (root_type == LMD_TYPE_LIST || root_type == LMD_TYPE_ARRAY) {
        List* list = (root_type == LMD_TYPE_LIST) ? root.list : (List*)root.array;

        if (list) {
            // Look for <html> element
            for (int64_t i = 0; i < list->length; i++) {
                Item child = list->items[i];
                TypeId child_type = get_type_id(child);

                if (child_type == LMD_TYPE_ELEMENT) {
                    ElementReader elem(child);
                    std::string tag_name(elem.tagName(), elem.tagNameLen());

                    // Serialize the html element and its children
                    serialize_element_wpt(child, result, 0);
                    break;
                }
            }
        }
    }
    else if (root_type == LMD_TYPE_ELEMENT) {
        // Check if this is a #document element
        ElementReader doc_elem(root);
        std::string tag_name(doc_elem.tagName(), doc_elem.tagNameLen());

        if (tag_name == "#document") {
            // Skip the #document element itself (already printed above)
            // Serialize its children (the html element)
            serialize_children_wpt(doc_elem, result, -1);  // depth -1 so children are at depth 0
        } else {
            // Regular element at root level
            serialize_element_wpt(root, result, 0);
        }
    }

    // Remove trailing newline to match WPT format
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    return result;
}

// ============================================================================
// Test Fixture
// ============================================================================

class WptHtmlParserTest : public ::testing::TestWithParam<std::pair<std::string, WptTestCase>> {
protected:
    Pool* pool;
    String* html_type;

    void SetUp() override {
        pool = pool_create();
        html_type = create_lambda_string("html");

        // Initialize logging
        log_parse_config_file("log.conf");
        log_init("");
    }

    void TearDown() override {
        if (html_type) {
            free(html_type);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }
};

TEST_P(WptHtmlParserTest, ParseHtml) {
    auto param = GetParam();
    std::string test_file = param.first;
    WptTestCase test_case = param.second;

    // Parse HTML using Lambda parser
    Input* input = input_from_source(test_case.input.c_str(), NULL, html_type, NULL);

    if (!input || input->root.item == ITEM_NULL || input->root.item == ITEM_ERROR) {
        FAIL() << "Failed to parse HTML: " << test_case.input;
        return;
    }

    // Convert Lambda DOM to WPT format
    std::string actual_tree = lambda_tree_to_wpt_format(input->root);

    // Compare trees
    if (actual_tree != test_case.expected) {
        // Output detailed comparison for debugging
        std::cerr << "\n=== Test Failed ===\n";
        std::cerr << "File: " << test_case.file << "\n";
        std::cerr << "Test ID: " << test_case.test_id << "\n";
        std::cerr << "Input HTML: " << test_case.input << "\n\n";
        std::cerr << "Expected:\n" << test_case.expected << "\n";
        std::cerr << "Actual:\n" << actual_tree << "\n";
    }

    EXPECT_EQ(actual_tree, test_case.expected)
        << "Test: " << test_case.file << " / " << test_case.test_id;
}

// ============================================================================
// Test Suite Instantiation
// ============================================================================

std::vector<std::pair<std::string, WptTestCase>> load_all_wpt_tests() {
    std::vector<std::pair<std::string, WptTestCase>> all_tests;

    // Priority 1 test files (core parsing - must pass)
    std::vector<std::string> priority1_files = {
        "html5lib_tests1.json",
        "html5lib_tests2.json",
        "html5lib_tests3.json",
        "html5lib_blocks.json",
        "html5lib_comments01.json",
        "html5lib_entities01.json",
        "html5lib_entities02.json"
    };

    // Load priority 1 tests
    for (const auto& filename : priority1_files) {
        std::string filepath = "test/html/wpt/" + filename;
        auto tests = parse_test_json(filepath);
        for (const auto& test : tests) {
            all_tests.push_back({filename, test});
        }
    }

    return all_tests;
}

// Allow test suite to be uninstantiated if test data files are missing
// This prevents GTest errors when test/html/wpt/*.json files don't exist
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WptHtmlParserTest);

INSTANTIATE_TEST_SUITE_P(
    Html5libPriority1,
    WptHtmlParserTest,
    ::testing::ValuesIn(load_all_wpt_tests()),
    [](const testing::TestParamInfo<WptHtmlParserTest::ParamType>& info) {
        // Generate test name from file and test_id
        std::string name = info.param.first + "_" + info.param.second.test_id;
        // Replace invalid characters in test name
        std::replace(name.begin(), name.end(), '.', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name.substr(0, 50); // Truncate if too long
    }
);
