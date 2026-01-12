// test_tex_conditional_gtest.cpp - Tests for TeX Conditional Processing
//
// Tests for the conditional processing system based on TeXBook Chapter 20.

#include <gtest/gtest.h>
#include "../lambda/tex/tex_conditional.hpp"
#include "../lambda/tex/tex_macro.hpp"
#include "../lib/arena.h"
#include "../lib/mempool.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class ConditionalTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    MacroProcessor* macros;
    ConditionalProcessor* processor;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        macros = new MacroProcessor(arena);
        processor = new ConditionalProcessor(arena, macros);
    }

    void TearDown() override {
        delete processor;
        delete macros;
        arena_destroy(arena);
        pool_destroy(pool);
    }
};

// ============================================================================
// ConditionalType Tests
// ============================================================================

TEST_F(ConditionalTest, ConditionalTypeValues) {
    // verify conditional type enum values exist
    ConditionalType t1 = ConditionalType::If;
    ConditionalType t2 = ConditionalType::Ifx;
    ConditionalType t3 = ConditionalType::Ifcat;
    ConditionalType t4 = ConditionalType::Ifnum;
    ConditionalType t5 = ConditionalType::Ifdim;
    ConditionalType t6 = ConditionalType::Ifodd;
    ConditionalType t7 = ConditionalType::Ifvmode;
    ConditionalType t8 = ConditionalType::Ifhmode;
    ConditionalType t9 = ConditionalType::Ifmmode;
    ConditionalType t10 = ConditionalType::Ifinner;

    EXPECT_NE(static_cast<int>(t1), static_cast<int>(t2));
    EXPECT_NE(static_cast<int>(t3), static_cast<int>(t4));
    EXPECT_NE(static_cast<int>(t5), static_cast<int>(t6));
    EXPECT_NE(static_cast<int>(t7), static_cast<int>(t8));
    EXPECT_NE(static_cast<int>(t9), static_cast<int>(t10));
}

TEST_F(ConditionalTest, ExtendedConditionalTypes) {
    // verify extended conditional types
    ConditionalType t1 = ConditionalType::Ifvoid;
    ConditionalType t2 = ConditionalType::Ifhbox;
    ConditionalType t3 = ConditionalType::Ifvbox;
    ConditionalType t4 = ConditionalType::Ifeof;
    ConditionalType t5 = ConditionalType::Iftrue;
    ConditionalType t6 = ConditionalType::Iffalse;
    ConditionalType t7 = ConditionalType::Ifcase;
    ConditionalType t8 = ConditionalType::Ifdefined;

    EXPECT_NE(static_cast<int>(t1), static_cast<int>(t2));
    EXPECT_NE(static_cast<int>(t3), static_cast<int>(t4));
    EXPECT_NE(static_cast<int>(t5), static_cast<int>(t6));
    EXPECT_NE(static_cast<int>(t7), static_cast<int>(t8));
}

// ============================================================================
// ConditionalState Tests
// ============================================================================

TEST_F(ConditionalTest, ConditionalStateInit) {
    ConditionalState state;
    state.type = ConditionalType::Ifnum;
    state.result = true;
    state.nesting_level = 1;
    state.skip_else = false;

    EXPECT_EQ(state.type, ConditionalType::Ifnum);
    EXPECT_TRUE(state.result);
    EXPECT_EQ(state.nesting_level, 1);
    EXPECT_FALSE(state.skip_else);
}

TEST_F(ConditionalTest, ConditionalStateFalse) {
    ConditionalState state;
    state.type = ConditionalType::Ifdim;
    state.result = false;
    state.nesting_level = 2;
    state.skip_else = true;

    EXPECT_EQ(state.type, ConditionalType::Ifdim);
    EXPECT_FALSE(state.result);
    EXPECT_EQ(state.nesting_level, 2);
    EXPECT_TRUE(state.skip_else);
}

// ============================================================================
// ConditionalStack Tests
// ============================================================================

TEST_F(ConditionalTest, StackInitiallyEmpty) {
    ConditionalStack stack;
    stack.states = nullptr;
    stack.count = 0;
    stack.capacity = 0;

    EXPECT_TRUE(stack.empty());
    EXPECT_EQ(stack.count, 0);
}

TEST_F(ConditionalTest, StackPushPop) {
    ConditionalStack stack;
    stack.states = (ConditionalState*)arena_alloc(arena, sizeof(ConditionalState) * 8);
    stack.count = 0;
    stack.capacity = 8;

    ConditionalState state1;
    state1.type = ConditionalType::If;
    state1.result = true;
    state1.nesting_level = 1;
    state1.skip_else = false;

    stack.push(state1);
    EXPECT_FALSE(stack.empty());
    EXPECT_EQ(stack.count, 1);

    ConditionalState* top = stack.top();
    EXPECT_NE(top, nullptr);
    EXPECT_EQ(top->type, ConditionalType::If);
    EXPECT_TRUE(top->result);

    ConditionalState popped = stack.pop();
    EXPECT_TRUE(stack.empty());
    EXPECT_EQ(popped.type, ConditionalType::If);
}

TEST_F(ConditionalTest, StackMultiplePush) {
    ConditionalStack stack;
    stack.states = (ConditionalState*)arena_alloc(arena, sizeof(ConditionalState) * 8);
    stack.count = 0;
    stack.capacity = 8;

    ConditionalState state1;
    state1.type = ConditionalType::If;
    state1.result = true;
    state1.nesting_level = 1;
    state1.skip_else = false;
    stack.push(state1);

    ConditionalState state2;
    state2.type = ConditionalType::Ifnum;
    state2.result = false;
    state2.nesting_level = 2;
    state2.skip_else = true;
    stack.push(state2);

    EXPECT_EQ(stack.count, 2);

    ConditionalState* top = stack.top();
    EXPECT_EQ(top->type, ConditionalType::Ifnum);
    EXPECT_FALSE(top->result);

    stack.pop();
    top = stack.top();
    EXPECT_EQ(top->type, ConditionalType::If);
    EXPECT_TRUE(top->result);
}

TEST_F(ConditionalTest, StackDeepNesting) {
    ConditionalStack stack;
    stack.states = (ConditionalState*)arena_alloc(arena, sizeof(ConditionalState) * 16);
    stack.count = 0;
    stack.capacity = 16;

    // push 10 states
    for (int i = 0; i < 10; i++) {
        ConditionalState state;
        state.type = (ConditionalType)(i % 8);
        state.result = (i % 2 == 0);
        state.nesting_level = i + 1;
        state.skip_else = false;
        stack.push(state);
    }

    EXPECT_EQ(stack.count, 10);

    // pop and verify
    for (int i = 9; i >= 0; i--) {
        ConditionalState* top = stack.top();
        EXPECT_EQ(top->nesting_level, i + 1);
        stack.pop();
    }

    EXPECT_TRUE(stack.empty());
}

// ============================================================================
// Mode Flag Tests
// ============================================================================

TEST_F(ConditionalTest, SetVerticalMode) {
    processor->set_vertical_mode(true);
    processor->set_horizontal_mode(false);
    processor->set_math_mode(false);
    // no direct accessor, but we can check through conditionals
    // just verify the API is callable
    EXPECT_TRUE(true);
}

TEST_F(ConditionalTest, SetHorizontalMode) {
    processor->set_vertical_mode(false);
    processor->set_horizontal_mode(true);
    processor->set_math_mode(false);
    EXPECT_TRUE(true);
}

TEST_F(ConditionalTest, SetMathMode) {
    processor->set_vertical_mode(false);
    processor->set_horizontal_mode(false);
    processor->set_math_mode(true);
    EXPECT_TRUE(true);
}

TEST_F(ConditionalTest, SetInnerMode) {
    processor->set_inner_mode(true);
    EXPECT_TRUE(true);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST_F(ConditionalTest, IsConditionalCommand) {
    EXPECT_TRUE(is_conditional_command("\\if", 3));
    EXPECT_TRUE(is_conditional_command("\\ifx", 4));
    EXPECT_TRUE(is_conditional_command("\\ifnum", 6));
    EXPECT_TRUE(is_conditional_command("\\ifdim", 6));
    EXPECT_TRUE(is_conditional_command("\\ifodd", 6));
    EXPECT_TRUE(is_conditional_command("\\ifvmode", 8));
    EXPECT_TRUE(is_conditional_command("\\ifhmode", 8));
    EXPECT_TRUE(is_conditional_command("\\ifmmode", 8));
    EXPECT_TRUE(is_conditional_command("\\iftrue", 7));
    EXPECT_TRUE(is_conditional_command("\\iffalse", 8));
}

TEST_F(ConditionalTest, IsNotConditionalCommand) {
    EXPECT_FALSE(is_conditional_command("\\def", 4));
    EXPECT_FALSE(is_conditional_command("\\let", 4));
    EXPECT_FALSE(is_conditional_command("\\hbox", 5));
    EXPECT_FALSE(is_conditional_command("if", 2));  // no backslash
    EXPECT_FALSE(is_conditional_command("text", 4));
}

// ============================================================================
// Eval Functions (direct testing)
// ============================================================================

TEST_F(ConditionalTest, EvalIfTrueCase) {
    // \if compares character codes
    // \if aa should be true (same character)
    const char* input = "aa then \\fi";
    size_t pos = 0;
    size_t len = strlen(input);

    bool result = processor->eval_if(input, &pos, len);
    EXPECT_TRUE(result);
}

TEST_F(ConditionalTest, EvalIfFalseCase) {
    // \if ab should be false (different characters)
    const char* input = "ab then \\fi";
    size_t pos = 0;
    size_t len = strlen(input);

    bool result = processor->eval_if(input, &pos, len);
    EXPECT_FALSE(result);
}

TEST_F(ConditionalTest, EvalIfnumLessThan) {
    // \ifnum 1<2 should be true
    const char* input = "1<2 true\\fi";
    size_t pos = 0;
    size_t len = strlen(input);

    bool result = processor->eval_ifnum(input, &pos, len);
    EXPECT_TRUE(result);
}

TEST_F(ConditionalTest, EvalIfnumEqual) {
    // \ifnum 5=5 should be true
    const char* input = "5=5 true\\fi";
    size_t pos = 0;
    size_t len = strlen(input);

    bool result = processor->eval_ifnum(input, &pos, len);
    EXPECT_TRUE(result);
}

TEST_F(ConditionalTest, EvalIfnumGreaterThan) {
    // \ifnum 10>5 should be true
    const char* input = "10>5 true\\fi";
    size_t pos = 0;
    size_t len = strlen(input);

    bool result = processor->eval_ifnum(input, &pos, len);
    EXPECT_TRUE(result);
}

TEST_F(ConditionalTest, EvalIfnumFalse) {
    // \ifnum 1>2 should be false
    const char* input = "1>2 false\\fi";
    size_t pos = 0;
    size_t len = strlen(input);

    bool result = processor->eval_ifnum(input, &pos, len);
    EXPECT_FALSE(result);
}

TEST_F(ConditionalTest, EvalIfoddTrue) {
    // \ifodd 3 should be true
    const char* input = "3 true\\fi";
    size_t pos = 0;
    size_t len = strlen(input);

    bool result = processor->eval_ifodd(input, &pos, len);
    EXPECT_TRUE(result);
}

TEST_F(ConditionalTest, EvalIfoddFalse) {
    // \ifodd 4 should be false
    const char* input = "4 false\\fi";
    size_t pos = 0;
    size_t len = strlen(input);

    bool result = processor->eval_ifodd(input, &pos, len);
    EXPECT_FALSE(result);
}

// ============================================================================
// Processor Tests
// ============================================================================

TEST_F(ConditionalTest, ProcessorCreation) {
    // processor is created in SetUp
    EXPECT_NE(processor, nullptr);
}

TEST_F(ConditionalTest, ProcessEmptyInput) {
    size_t out_len = 0;
    char* result = processor->process("", 0, &out_len);

    EXPECT_NE(result, nullptr);
    EXPECT_EQ(out_len, 0);
}

TEST_F(ConditionalTest, ProcessNoConditionals) {
    const char* input = "Hello World";
    size_t len = strlen(input);
    size_t out_len = 0;

    char* result = processor->process(input, len, &out_len);

    EXPECT_NE(result, nullptr);
    EXPECT_EQ(out_len, len);
    EXPECT_EQ(strncmp(result, input, len), 0);
}

TEST_F(ConditionalTest, EvaluateConditionalBasic) {
    const char* input = "\\iftrue test\\fi";
    size_t len = strlen(input);
    bool result = false;

    size_t consumed = processor->evaluate_conditional(input, 0, len, &result);

    EXPECT_GT(consumed, 0);
    EXPECT_TRUE(result);
}

TEST_F(ConditionalTest, EvaluateConditionalFalse) {
    const char* input = "\\iffalse test\\fi";
    size_t len = strlen(input);
    bool result = true;  // should become false

    size_t consumed = processor->evaluate_conditional(input, 0, len, &result);

    EXPECT_GT(consumed, 0);
    EXPECT_FALSE(result);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
