// test_math_parser_gtest.cpp - Tests for the LaTeX math parser
//
// Tests parsing of LaTeX math expressions using tree-sitter-latex-math

#include <gtest/gtest.h>
#include "../lambda/input/input-math2.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/math_node.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"

using namespace lambda;

class MathParserTest : public ::testing::Test {
protected:
    Input* input;
    
    void SetUp() override {
        input = InputManager::create_input(nullptr);
    }
    
    void TearDown() override {
        // Input is managed by InputManager, no explicit cleanup needed
    }
    
    // Helper to check if result is valid
    bool isValidResult(Item result) {
        return result.item != ItemNull.item;
    }
    
    // Helper to get node type string
    const char* getNodeTypeStr(Item result) {
        if (!isValidResult(result)) return "null";
        TypeId type = get_type_id(result);
        if (type != LMD_TYPE_MAP) return "not-map";
        
        Map* map = result.map;
        ConstItem node_type = map->get("node");
        if (node_type.item == ItemNull.item) return "no-node-field";
        
        TypeId nt_type = node_type.type_id();
        Item node_type_item = *(Item*)&node_type;
        String* str = nullptr;
        if (nt_type == LMD_TYPE_SYMBOL) {
            str = node_type_item.get_symbol();
        } else if (nt_type == LMD_TYPE_STRING) {
            str = node_type_item.get_string();
        }
        return str ? str->chars : "invalid";
    }
};

// Test basic symbol parsing
TEST_F(MathParserTest, ParseSimpleSymbol) {
    Item result = parse_math("x", input);
    EXPECT_TRUE(isValidResult(result));
    EXPECT_STREQ("symbol", getNodeTypeStr(result));
}

// Test number parsing
TEST_F(MathParserTest, ParseNumber) {
    Item result = parse_math("123", input);
    EXPECT_TRUE(isValidResult(result));
    // Numbers may be parsed as symbol or number depending on grammar
}

// Test simple expression
TEST_F(MathParserTest, ParseSimpleAddition) {
    Item result = parse_math("x + y", input);
    EXPECT_TRUE(isValidResult(result));
    // Should be a row with 3 elements: x, +, y
}

// Test fraction
TEST_F(MathParserTest, ParseFraction) {
    Item result = parse_math("\\frac{1}{2}", input);
    EXPECT_TRUE(isValidResult(result));
    // May be frac node or command
}

// Test Greek letter
TEST_F(MathParserTest, ParseGreekLetter) {
    Item result = parse_math("\\alpha", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test superscript
TEST_F(MathParserTest, ParseSuperscript) {
    Item result = parse_math("x^2", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test subscript
TEST_F(MathParserTest, ParseSubscript) {
    Item result = parse_math("x_i", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test combined sub/superscript
TEST_F(MathParserTest, ParseSubSuperscript) {
    Item result = parse_math("x_i^2", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test square root
TEST_F(MathParserTest, ParseSqrt) {
    Item result = parse_math("\\sqrt{x}", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test grouping with braces
TEST_F(MathParserTest, ParseBraces) {
    Item result = parse_math("{a + b}", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test parentheses
TEST_F(MathParserTest, ParseParentheses) {
    Item result = parse_math("(a + b)", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test complex expression (quadratic formula-like)
TEST_F(MathParserTest, ParseComplexExpression) {
    Item result = parse_math("\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test sum notation
TEST_F(MathParserTest, ParseSum) {
    Item result = parse_math("\\sum_{i=1}^{n} x_i", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test integral notation
TEST_F(MathParserTest, ParseIntegral) {
    Item result = parse_math("\\int_0^\\infty e^{-x} dx", input);
    EXPECT_TRUE(isValidResult(result));
}

// Test empty input
TEST_F(MathParserTest, ParseEmpty) {
    Item result = parse_math("", input);
    // Empty input should return ItemNull or empty row
}

// Test debug print (just verify it doesn't crash)
TEST_F(MathParserTest, DebugPrintTree) {
    debug_print_math_tree("x + y");
    // If we get here without crash, test passes
    SUCCEED();
}

int main(int argc, char **argv) {
    // Initialize logging
    log_init(NULL);
    
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
