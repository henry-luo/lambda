/**
 * Test tree-sitter LaTeX parser
 * Simple verification that parse_latex_ts() works correctly
 */

#include <gtest/gtest.h>
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/log.h"

// Import print functions
extern void print_item(Item item, int depth);

// External API functions
extern "C" {
    Input* input_from_source(const char* source, Url* abs_url, String* type, String* flavor);
    Url* url_parse(const char* input);
    void url_destroy(Url* url);
}

// Helper to create a Lambda String
static String* create_lambda_string(const char* text) {
    if (!text) return nullptr;
    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return nullptr;
    result->len = len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);
    return result;
}

class LatexTreeSitterTest : public ::testing::Test {
protected:
    Url* dummy_url;
    String* type_str;

    void SetUp() override {
        dummy_url = url_parse("file://./test.tex");
        type_str = create_lambda_string("latex");
    }

    void TearDown() override {
        url_destroy(dummy_url);
        free(type_str);
    }

    // Helper to parse LaTeX content
    Input* parse_latex(const char* latex_content) {
        char* latex_copy = strdup(latex_content);
        Input* input = input_from_source(latex_copy, dummy_url, type_str, nullptr);
        return input;
    }
};

TEST_F(LatexTreeSitterTest, BasicText) {
    const char* latex = "Hello world";
    
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    // Print the tree for inspection
    printf("\n=== BasicText Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
}

TEST_F(LatexTreeSitterTest, SimpleCommand) {
    const char* latex = "\\textbf{bold text}";
    
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== SimpleCommand Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
    
    // Verify structure: should have latex_document > textbf element
    TypeId root_type = get_type_id(input->root);
    EXPECT_EQ(root_type, LMD_TYPE_ELEMENT);
    
    if (root_type == LMD_TYPE_ELEMENT) {
        Element* doc = input->root.element;
        TypeElmt* doc_type = (TypeElmt*)doc->type;
        EXPECT_STREQ(doc_type->name.str, "latex_document");
        
        // Should have at least one child
        EXPECT_GT(doc->length, 0);
    }
}

TEST_F(LatexTreeSitterTest, SpacingCommand) {
    const char* latex = "word1 \\quad word2";
    
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== SpacingCommand Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
    
    // Check if \quad is converted to Symbol (60% memory savings!)
    TypeId root_type = get_type_id(input->root);
    EXPECT_EQ(root_type, LMD_TYPE_ELEMENT);
    
    if (root_type == LMD_TYPE_ELEMENT) {
        Element* doc = input->root.element;
        List* list = (List*)doc;  // Element extends List
        
        // Look for \quad in children - should be Symbol, not Element
        for (int64_t i = 0; i < list->length; i++) {
            Item child = list->items[i];
            TypeId child_type = get_type_id(child);
            
            if (child_type == LMD_TYPE_SYMBOL) {
                // Extract Symbol pointer (Symbol is typedef of String)
                Symbol* sym = (Symbol*)(child.item & 0x00FFFFFFFFFFFFFF);
                printf("Found Symbol: %.*s (Memory efficient!)\n", (int)sym->len, sym->chars);
                EXPECT_TRUE(strncmp(sym->chars, "quad", 4) == 0);
            }
        }
    }
}

TEST_F(LatexTreeSitterTest, ControlSymbol) {
    const char* latex = "Price: \\$5.00";
    
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== ControlSymbol Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
}

TEST_F(LatexTreeSitterTest, DiactricCommand) {
    const char* latex = "\\'e";  // é
    
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== DiactricCommand Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
}

TEST_F(LatexTreeSitterTest, Environment) {
    const char* latex = "\\begin{center}Centered\\end{center}";
    
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== Environment Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
    
    // Verify environment structure
    TypeId root_type = get_type_id(input->root);
    EXPECT_EQ(root_type, LMD_TYPE_ELEMENT);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
