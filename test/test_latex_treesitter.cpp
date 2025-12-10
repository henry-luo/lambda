/**
 * Test tree-sitter LaTeX parser
 * Simple verification that parse_latex_ts() works correctly
 */

#include <gtest/gtest.h>
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/print.cpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/log.h"

// External parser function
extern "C" {
    void parse_latex_ts(Input* input, const char* latex_string);
}

class LatexTreeSitterTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    Input* input;

    void SetUp() override {
        pool = pool_create(1024 * 1024);  // 1MB pool
        arena = arena_create(1024 * 1024); // 1MB arena
        input = input_create(pool, arena);
    }

    void TearDown() override {
        input_destroy(input);
        arena_destroy(arena);
        pool_destroy(pool);
    }
};

TEST_F(LatexTreeSitterTest, BasicText) {
    const char* latex = "Hello world";
    
    parse_latex_ts(input, latex);
    
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    // Print the tree for inspection
    printf("\n=== BasicText Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
}

TEST_F(LatexTreeSitterTest, SimpleCommand) {
    const char* latex = "\\textbf{bold text}";
    
    parse_latex_ts(input, latex);
    
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== SimpleCommand Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
    
    // Verify structure: should have latex_document > textbf element
    TypeId root_type = get_type_id(input->root);
    EXPECT_EQ(root_type, LMD_TYPE_ELEMENT);
    
    if (root_type == LMD_TYPE_ELEMENT) {
        Element* doc = input->root.element;
        EXPECT_STREQ(doc->tag_name->chars, "latex_document");
        
        // Should have at least one child
        EXPECT_GT(doc->list.length, 0);
    }
}

TEST_F(LatexTreeSitterTest, SpacingCommand) {
    const char* latex = "word1 \\quad word2";
    
    parse_latex_ts(input, latex);
    
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== SpacingCommand Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
    
    // Check if \quad is converted to Symbol (60% memory savings!)
    TypeId root_type = get_type_id(input->root);
    EXPECT_EQ(root_type, LMD_TYPE_ELEMENT);
    
    if (root_type == LMD_TYPE_ELEMENT) {
        Element* doc = input->root.element;
        
        // Look for \quad in children - should be Symbol, not Element
        for (int64_t i = 0; i < doc->list.length; i++) {
            Item child = doc->list.items[i];
            TypeId child_type = get_type_id(child);
            
            if (child_type == LMD_TYPE_SYMBOL) {
                Symbol* sym = child.symbol;
                printf("Found Symbol: %s (Memory efficient!)\n", sym->chars);
                EXPECT_TRUE(strcmp(sym->chars, "quad") == 0 || strcmp(sym->chars, "\\quad") == 0);
            }
        }
    }
}

TEST_F(LatexTreeSitterTest, ControlSymbol) {
    const char* latex = "Price: \\$5.00";
    
    parse_latex_ts(input, latex);
    
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== ControlSymbol Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
}

TEST_F(LatexTreeSitterTest, DiactricCommand) {
    const char* latex = "\\'e";  // é
    
    parse_latex_ts(input, latex);
    
    ASSERT_NE(input->root.item, ITEM_NULL);
    
    printf("\n=== DiactricCommand Tree ===\n");
    print_item(input->root, 0);
    printf("\n");
}

TEST_F(LatexTreeSitterTest, Environment) {
    const char* latex = "\\begin{center}Centered\\end{center}";
    
    parse_latex_ts(input, latex);
    
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
