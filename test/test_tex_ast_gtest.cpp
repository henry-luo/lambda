// test_tex_ast_gtest.cpp - Unit tests for TeX AST building and traversal

#include <gtest/gtest.h>
#include "../lambda/tex/tex_ast.hpp"
#include "../lambda/tex/tex_ast_builder.hpp"
#include "../lib/arena.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class TexAstTest : public ::testing::Test {
protected:
    Arena arena;

    void SetUp() override {
        arena_init(&arena, 64 * 1024);
    }

    void TearDown() override {
        arena_destroy(&arena);
    }

    // Helper to create text node
    TexNode* make_text(const char* text) {
        TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
        node->type = TexNodeType::Text;
        node->text.content = text;
        node->text.length = strlen(text);
        node->next = nullptr;
        node->loc = { 0, 0, 0, 0 };
        return node;
    }

    // Helper to create command node
    TexNode* make_command(const char* name) {
        TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
        node->type = TexNodeType::Command;
        node->command.name = name;
        node->command.args = nullptr;
        node->command.arg_count = 0;
        node->next = nullptr;
        node->loc = { 0, 0, 0, 0 };
        return node;
    }

    // Helper to create math node
    TexNode* make_math(TexNode* content, bool display) {
        TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
        node->type = TexNodeType::Math;
        node->math.content = content;
        node->math.display = display;
        node->next = nullptr;
        node->loc = { 0, 0, 0, 0 };
        return node;
    }

    // Helper to create group node
    TexNode* make_group(TexNode* content) {
        TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
        node->type = TexNodeType::Group;
        node->group.content = content;
        node->next = nullptr;
        node->loc = { 0, 0, 0, 0 };
        return node;
    }
};

// ============================================================================
// Node Type Tests
// ============================================================================

TEST_F(TexAstTest, TextNodeCreation) {
    TexNode* node = make_text("Hello");

    EXPECT_EQ(node->type, TexNodeType::Text);
    EXPECT_STREQ(node->text.content, "Hello");
    EXPECT_EQ(node->text.length, 5);
}

TEST_F(TexAstTest, CommandNodeCreation) {
    TexNode* node = make_command("frac");

    EXPECT_EQ(node->type, TexNodeType::Command);
    EXPECT_STREQ(node->command.name, "frac");
    EXPECT_EQ(node->command.arg_count, 0);
}

TEST_F(TexAstTest, MathNodeCreation) {
    TexNode* content = make_text("x");
    TexNode* node = make_math(content, false);

    EXPECT_EQ(node->type, TexNodeType::Math);
    EXPECT_EQ(node->math.display, false);
    EXPECT_NE(node->math.content, nullptr);
}

TEST_F(TexAstTest, GroupNodeCreation) {
    TexNode* content = make_text("abc");
    TexNode* node = make_group(content);

    EXPECT_EQ(node->type, TexNodeType::Group);
    EXPECT_NE(node->group.content, nullptr);
}

// ============================================================================
// Node Linking Tests
// ============================================================================

TEST_F(TexAstTest, NodeChaining) {
    TexNode* a = make_text("a");
    TexNode* b = make_text("b");
    TexNode* c = make_text("c");

    a->next = b;
    b->next = c;

    // Traverse the chain
    int count = 0;
    for (TexNode* n = a; n; n = n->next) {
        count++;
    }
    EXPECT_EQ(count, 3);
}

TEST_F(TexAstTest, CommandWithArgs) {
    TexNode* cmd = make_command("frac");

    // Create argument nodes
    TexNode* args[2];
    args[0] = make_group(make_text("a"));
    args[1] = make_group(make_text("b"));

    cmd->command.args = (TexNode**)arena_alloc(&arena, 2 * sizeof(TexNode*));
    cmd->command.args[0] = args[0];
    cmd->command.args[1] = args[1];
    cmd->command.arg_count = 2;

    EXPECT_EQ(cmd->command.arg_count, 2);
    EXPECT_NE(cmd->command.args[0], nullptr);
    EXPECT_NE(cmd->command.args[1], nullptr);
}

// ============================================================================
// AST Traversal Tests
// ============================================================================

TEST_F(TexAstTest, CountTextNodes) {
    // Build: "Hello " + math("x") + " World"
    TexNode* t1 = make_text("Hello ");
    TexNode* m = make_math(make_text("x"), false);
    TexNode* t2 = make_text(" World");

    t1->next = m;
    m->next = t2;

    // Count text nodes
    int text_count = 0;
    for (TexNode* n = t1; n; n = n->next) {
        if (n->type == TexNodeType::Text) {
            text_count++;
        }
    }
    EXPECT_EQ(text_count, 2);
}

TEST_F(TexAstTest, FindMathNodes) {
    TexNode* t1 = make_text("before ");
    TexNode* m1 = make_math(make_text("a"), false);
    TexNode* t2 = make_text(" middle ");
    TexNode* m2 = make_math(make_text("b"), true);  // display math
    TexNode* t3 = make_text(" after");

    t1->next = m1;
    m1->next = t2;
    t2->next = m2;
    m2->next = t3;

    // Find display math
    bool found_display = false;
    for (TexNode* n = t1; n; n = n->next) {
        if (n->type == TexNodeType::Math && n->math.display) {
            found_display = true;
            break;
        }
    }
    EXPECT_TRUE(found_display);
}

// ============================================================================
// Subscript/Superscript Tests
// ============================================================================

TEST_F(TexAstTest, SuperscriptNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Superscript;
    node->script.base = make_text("x");
    node->script.script = make_text("2");
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::Superscript);
    EXPECT_NE(node->script.base, nullptr);
    EXPECT_NE(node->script.script, nullptr);
}

TEST_F(TexAstTest, SubscriptNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Subscript;
    node->script.base = make_text("x");
    node->script.script = make_text("i");
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::Subscript);
}

TEST_F(TexAstTest, SubSuperscriptNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::SubSuperscript;
    node->subsuper.base = make_text("x");
    node->subsuper.subscript = make_text("i");
    node->subsuper.superscript = make_text("2");
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::SubSuperscript);
    EXPECT_NE(node->subsuper.subscript, nullptr);
    EXPECT_NE(node->subsuper.superscript, nullptr);
}

// ============================================================================
// Fraction Tests
// ============================================================================

TEST_F(TexAstTest, FractionNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Fraction;
    node->fraction.numerator = make_text("a");
    node->fraction.denominator = make_text("b");
    node->fraction.style = FractionStyle::Normal;
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::Fraction);
    EXPECT_EQ(node->fraction.style, FractionStyle::Normal);
}

TEST_F(TexAstTest, BinomialNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Fraction;
    node->fraction.numerator = make_text("n");
    node->fraction.denominator = make_text("k");
    node->fraction.style = FractionStyle::Binomial;
    node->next = nullptr;

    EXPECT_EQ(node->fraction.style, FractionStyle::Binomial);
}

// ============================================================================
// Radical Tests
// ============================================================================

TEST_F(TexAstTest, SqrtNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Radical;
    node->radical.radicand = make_text("x");
    node->radical.index = nullptr;  // square root
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::Radical);
    EXPECT_EQ(node->radical.index, nullptr);
}

TEST_F(TexAstTest, NthRootNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Radical;
    node->radical.radicand = make_text("x");
    node->radical.index = make_text("3");  // cube root
    node->next = nullptr;

    EXPECT_NE(node->radical.index, nullptr);
}

// ============================================================================
// Environment Tests
// ============================================================================

TEST_F(TexAstTest, EnvironmentNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Environment;
    node->environment.name = "align";
    node->environment.content = make_text("a &= b");
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::Environment);
    EXPECT_STREQ(node->environment.name, "align");
}

TEST_F(TexAstTest, MatrixEnvironment) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Environment;
    node->environment.name = "pmatrix";
    node->environment.content = make_text("a & b \\\\ c & d");
    node->next = nullptr;

    EXPECT_STREQ(node->environment.name, "pmatrix");
}

// ============================================================================
// Source Location Tests
// ============================================================================

TEST_F(TexAstTest, SourceLocationTracking) {
    TexNode* node = make_text("test");
    node->loc.start_line = 5;
    node->loc.start_col = 10;
    node->loc.end_line = 5;
    node->loc.end_col = 14;

    EXPECT_EQ(node->loc.start_line, 5);
    EXPECT_EQ(node->loc.start_col, 10);
    EXPECT_EQ(node->loc.end_line, 5);
    EXPECT_EQ(node->loc.end_col, 14);
}

// ============================================================================
// Delimiter Tests
// ============================================================================

TEST_F(TexAstTest, DelimiterNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::Delimiter;
    node->delimiter.codepoint = '(';
    node->delimiter.is_left = true;
    node->delimiter.size = DelimiterSize::Auto;
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::Delimiter);
    EXPECT_EQ(node->delimiter.codepoint, '(');
    EXPECT_TRUE(node->delimiter.is_left);
}

TEST_F(TexAstTest, MatchingDelimiters) {
    TexNode* left = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    left->type = TexNodeType::Delimiter;
    left->delimiter.codepoint = '(';
    left->delimiter.is_left = true;

    TexNode* content = make_text("x");

    TexNode* right = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    right->type = TexNodeType::Delimiter;
    right->delimiter.codepoint = ')';
    right->delimiter.is_left = false;

    left->next = content;
    content->next = right;

    // Verify chain
    EXPECT_EQ(left->next, content);
    EXPECT_EQ(content->next, right);
}

// ============================================================================
// Operator Tests
// ============================================================================

TEST_F(TexAstTest, BinaryOperatorNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::BinOp;
    node->binop.op = '+';
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::BinOp);
    EXPECT_EQ(node->binop.op, '+');
}

TEST_F(TexAstTest, RelationOperatorNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::RelOp;
    node->relop.op = '=';
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::RelOp);
}

TEST_F(TexAstTest, BigOperatorNode) {
    TexNode* node = (TexNode*)arena_alloc(&arena, sizeof(TexNode));
    node->type = TexNodeType::BigOp;
    node->bigop.name = "sum";
    node->bigop.has_limits = true;
    node->bigop.lower = make_text("i=1");
    node->bigop.upper = make_text("n");
    node->next = nullptr;

    EXPECT_EQ(node->type, TexNodeType::BigOp);
    EXPECT_STREQ(node->bigop.name, "sum");
    EXPECT_TRUE(node->bigop.has_limits);
}

// ============================================================================
// AST Visitor Pattern Test
// ============================================================================

// Simple visitor that counts nodes
class NodeCounter {
public:
    int total = 0;
    int text_nodes = 0;
    int math_nodes = 0;
    int command_nodes = 0;

    void visit(TexNode* node) {
        while (node) {
            total++;
            switch (node->type) {
                case TexNodeType::Text:
                    text_nodes++;
                    break;
                case TexNodeType::Math:
                    math_nodes++;
                    if (node->math.content) {
                        visit(node->math.content);
                    }
                    break;
                case TexNodeType::Command:
                    command_nodes++;
                    for (int i = 0; i < node->command.arg_count; i++) {
                        visit(node->command.args[i]);
                    }
                    break;
                case TexNodeType::Group:
                    if (node->group.content) {
                        visit(node->group.content);
                    }
                    break;
                default:
                    break;
            }
            node = node->next;
        }
    }
};

TEST_F(TexAstTest, VisitorPattern) {
    // Build: text + math(cmd + text) + text
    TexNode* t1 = make_text("Hello ");

    TexNode* cmd = make_command("frac");
    cmd->command.args = (TexNode**)arena_alloc(&arena, 2 * sizeof(TexNode*));
    cmd->command.args[0] = make_group(make_text("a"));
    cmd->command.args[1] = make_group(make_text("b"));
    cmd->command.arg_count = 2;

    TexNode* m = make_math(cmd, false);
    TexNode* t2 = make_text(" World");

    t1->next = m;
    m->next = t2;

    NodeCounter counter;
    counter.visit(t1);

    EXPECT_EQ(counter.text_nodes, 4);  // "Hello ", "a", "b", " World"
    EXPECT_EQ(counter.math_nodes, 1);
    EXPECT_EQ(counter.command_nodes, 1);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
