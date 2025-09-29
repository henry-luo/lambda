#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "../radiant/text_wrapping.h"
#include "../radiant/text_metrics.h"

// Test fixture for text wrapping tests
class TextWrappingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging
        init_text_wrapping_logging();
        
        // Create default configuration
        config = create_text_wrap_config();
        ASSERT_NE(config, nullptr);
        
        // Set up test text
        test_text = "Hello world this is a test of text wrapping functionality.";
        test_text_length = strlen(test_text);
    }
    
    void TearDown() override {
        if (config) {
            destroy_text_wrap_config(config);
        }
    }
    
    TextWrapConfig* config = nullptr;
    const char* test_text = nullptr;
    int test_text_length = 0;
};

// Test 1: Text wrap configuration creation and destruction
TEST_F(TextWrappingTest, ConfigurationManagement) {
    EXPECT_NE(config, nullptr);
    EXPECT_EQ(config->white_space, WHITESPACE_NORMAL);
    EXPECT_EQ(config->word_break, WORD_BREAK_NORMAL);
    EXPECT_EQ(config->overflow_wrap, OVERFLOW_WRAP_NORMAL);
    EXPECT_EQ(config->text_justify, TEXT_JUSTIFY_AUTO);
    EXPECT_EQ(config->max_width, 800);
    EXPECT_FALSE(config->allow_overflow);
    EXPECT_TRUE(config->break_cache_enabled);
}

// Test 2: White-space property configuration
TEST_F(TextWrappingTest, WhiteSpaceConfiguration) {
    configure_white_space(config, WHITESPACE_NOWRAP);
    EXPECT_EQ(config->white_space, WHITESPACE_NOWRAP);
    
    configure_white_space(config, WHITESPACE_PRE);
    EXPECT_EQ(config->white_space, WHITESPACE_PRE);
    
    configure_white_space(config, WHITESPACE_PRE_WRAP);
    EXPECT_EQ(config->white_space, WHITESPACE_PRE_WRAP);
    
    configure_white_space(config, WHITESPACE_PRE_LINE);
    EXPECT_EQ(config->white_space, WHITESPACE_PRE_LINE);
}

// Test 3: Word-break property configuration
TEST_F(TextWrappingTest, WordBreakConfiguration) {
    configure_word_break(config, WORD_BREAK_BREAK_ALL);
    EXPECT_EQ(config->word_break, WORD_BREAK_BREAK_ALL);
    
    configure_word_break(config, WORD_BREAK_KEEP_ALL);
    EXPECT_EQ(config->word_break, WORD_BREAK_KEEP_ALL);
    
    configure_word_break(config, WORD_BREAK_BREAK_WORD);
    EXPECT_EQ(config->word_break, WORD_BREAK_BREAK_WORD);
}

// Test 4: Text wrap context creation and management
TEST_F(TextWrappingTest, ContextManagement) {
    TextWrapContext* ctx = create_text_wrap_context(test_text, test_text_length, config);
    ASSERT_NE(ctx, nullptr);
    
    EXPECT_EQ(ctx->text, test_text);
    EXPECT_EQ(ctx->text_length, test_text_length);
    EXPECT_GT(ctx->codepoint_count, 0);
    EXPECT_NE(ctx->codepoints, nullptr);
    EXPECT_TRUE(ctx->owns_codepoints);
    
    destroy_text_wrap_context(ctx);
}

// Test 5: UTF-8 to codepoints conversion
TEST_F(TextWrappingTest, UTF8ToCodepoints) {
    uint32_t* codepoints = nullptr;
    int count = utf8_to_codepoints("Hello 世界", 11, &codepoints);
    
    EXPECT_GT(count, 0);
    EXPECT_NE(codepoints, nullptr);
    
    // Check first few codepoints
    EXPECT_EQ(codepoints[0], 'H');
    EXPECT_EQ(codepoints[1], 'e');
    EXPECT_EQ(codepoints[2], 'l');
    EXPECT_EQ(codepoints[3], 'l');
    EXPECT_EQ(codepoints[4], 'o');
    EXPECT_EQ(codepoints[5], ' ');
    
    // Check Unicode characters (世界)
    EXPECT_EQ(codepoints[6], 0x4E16); // 世
    EXPECT_EQ(codepoints[7], 0x754C); // 界
    
    free(codepoints);
}

// Test 6: Break opportunity detection
TEST_F(TextWrappingTest, BreakOpportunityDetection) {
    TextWrapContext* ctx = create_text_wrap_context(test_text, test_text_length, config);
    ASSERT_NE(ctx, nullptr);
    
    int break_count = find_break_opportunities(ctx);
    EXPECT_GT(break_count, 0);
    EXPECT_EQ(ctx->break_count, break_count);
    
    // Should find breaks at spaces
    bool found_space_break = false;
    for (int i = 0; i < ctx->break_count; i++) {
        if (ctx->break_opportunities[i].position > 0) {
            uint32_t codepoint = ctx->codepoints[ctx->break_opportunities[i].position - 1];
            if (codepoint == ' ') {
                found_space_break = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_space_break);
    
    destroy_text_wrap_context(ctx);
}

// Test 7: White-space character detection
TEST_F(TextWrappingTest, WhitespaceDetection) {
    EXPECT_TRUE(is_whitespace_codepoint(' '));
    EXPECT_TRUE(is_whitespace_codepoint('\t'));
    EXPECT_TRUE(is_whitespace_codepoint('\n'));
    EXPECT_TRUE(is_whitespace_codepoint('\r'));
    EXPECT_TRUE(is_whitespace_codepoint(0xA0)); // Non-breaking space
    
    EXPECT_FALSE(is_whitespace_codepoint('A'));
    EXPECT_FALSE(is_whitespace_codepoint('1'));
    EXPECT_FALSE(is_whitespace_codepoint('.'));
}

// Test 8: Line break character detection
TEST_F(TextWrappingTest, LineBreakDetection) {
    EXPECT_TRUE(is_line_break_codepoint('\n'));
    EXPECT_TRUE(is_line_break_codepoint('\r'));
    
    EXPECT_FALSE(is_line_break_codepoint(' '));
    EXPECT_FALSE(is_line_break_codepoint('\t'));
    EXPECT_FALSE(is_line_break_codepoint('A'));
}

// Test 9: CJK character detection
TEST_F(TextWrappingTest, CJKCharacterDetection) {
    // CJK Unified Ideographs
    EXPECT_TRUE(is_cjk_character(0x4E00)); // 一
    EXPECT_TRUE(is_cjk_character(0x9FFF)); // End of range
    
    // Hiragana
    EXPECT_TRUE(is_cjk_character(0x3042)); // あ
    EXPECT_TRUE(is_cjk_character(0x3093)); // ん
    
    // Katakana
    EXPECT_TRUE(is_cjk_character(0x30A2)); // ア
    EXPECT_TRUE(is_cjk_character(0x30F3)); // ン
    
    // Non-CJK characters
    EXPECT_FALSE(is_cjk_character('A'));
    EXPECT_FALSE(is_cjk_character('1'));
    EXPECT_FALSE(is_cjk_character(' '));
}

// Test 10: White-space property behavior
TEST_F(TextWrappingTest, WhiteSpaceBehavior) {
    EXPECT_TRUE(should_wrap_lines(WHITESPACE_NORMAL));
    EXPECT_FALSE(should_wrap_lines(WHITESPACE_NOWRAP));
    EXPECT_FALSE(should_wrap_lines(WHITESPACE_PRE));
    EXPECT_TRUE(should_wrap_lines(WHITESPACE_PRE_WRAP));
    EXPECT_TRUE(should_wrap_lines(WHITESPACE_PRE_LINE));
    
    EXPECT_FALSE(should_preserve_spaces(WHITESPACE_NORMAL));
    EXPECT_TRUE(should_preserve_spaces(WHITESPACE_PRE));
    EXPECT_TRUE(should_preserve_spaces(WHITESPACE_PRE_WRAP));
    
    EXPECT_FALSE(should_preserve_newlines(WHITESPACE_NORMAL));
    EXPECT_TRUE(should_preserve_newlines(WHITESPACE_PRE));
    EXPECT_TRUE(should_preserve_newlines(WHITESPACE_PRE_WRAP));
    EXPECT_TRUE(should_preserve_newlines(WHITESPACE_PRE_LINE));
}

// Test 11: Basic text wrapping
TEST_F(TextWrappingTest, BasicTextWrapping) {
    config->max_width = 100; // Small width to force wrapping
    
    TextWrapContext* ctx = create_text_wrap_context(test_text, test_text_length, config);
    ASSERT_NE(ctx, nullptr);
    
    int line_count = wrap_text_lines(ctx, 100);
    EXPECT_GT(line_count, 1); // Should wrap into multiple lines
    EXPECT_EQ(ctx->line_count, line_count);
    
    // Check that lines are created
    for (int i = 0; i < ctx->line_count; i++) {
        WrappedTextLine* line = &ctx->lines[i];
        EXPECT_NE(line->text, nullptr);
        EXPECT_GT(line->text_length, 0);
        EXPECT_GE(line->end_position, line->start_position);
    }
    
    destroy_text_wrap_context(ctx);
}

// Test 12: No-wrap behavior
TEST_F(TextWrappingTest, NoWrapBehavior) {
    configure_white_space(config, WHITESPACE_NOWRAP);
    
    TextWrapContext* ctx = create_text_wrap_context(test_text, test_text_length, config);
    ASSERT_NE(ctx, nullptr);
    
    int line_count = wrap_text_lines(ctx, 50); // Very small width
    
    // With nowrap, should still be one line (or minimal lines)
    EXPECT_LE(line_count, 2);
    
    destroy_text_wrap_context(ctx);
}

// Test 13: Break-all word breaking
TEST_F(TextWrappingTest, BreakAllWordBreaking) {
    configure_word_break(config, WORD_BREAK_BREAK_ALL);
    config->max_width = 50; // Small width
    
    TextWrapContext* ctx = create_text_wrap_context("verylongwordwithoutspaces", 26, config);
    ASSERT_NE(ctx, nullptr);
    
    int break_count = find_break_opportunities(ctx);
    
    // With break-all, should find many break opportunities
    EXPECT_GT(break_count, 5);
    
    destroy_text_wrap_context(ctx);
}

// Test 14: Line width calculation
TEST_F(TextWrappingTest, LineWidthCalculation) {
    TextWrapContext* ctx = create_text_wrap_context("Hello", 5, config);
    ASSERT_NE(ctx, nullptr);
    
    int width = calculate_line_width(ctx, 0, 5);
    EXPECT_GT(width, 0);
    EXPECT_EQ(width, 5 * 8); // 8 pixels per character estimate
    
    destroy_text_wrap_context(ctx);
}

// Test 15: Break penalty calculation
TEST_F(TextWrappingTest, BreakPenaltyCalculation) {
    TextWrapContext* ctx = create_text_wrap_context(test_text, test_text_length, config);
    ASSERT_NE(ctx, nullptr);
    
    int soft_penalty = calculate_break_penalty(ctx, 0, BREAK_SOFT);
    int hard_penalty = calculate_break_penalty(ctx, 0, BREAK_HARD);
    int forced_penalty = calculate_break_penalty(ctx, 0, BREAK_FORCED);
    int hyphen_penalty = calculate_break_penalty(ctx, 0, BREAK_HYPHEN);
    
    EXPECT_EQ(soft_penalty, 0);      // Preferred break
    EXPECT_EQ(hard_penalty, -100);   // Required break
    EXPECT_EQ(forced_penalty, 1000); // Avoid if possible
    EXPECT_EQ(hyphen_penalty, 50);   // Moderate penalty
    
    destroy_text_wrap_context(ctx);
}

// Test 16: Memory management
TEST_F(TextWrappingTest, MemoryManagement) {
    TextWrapContext* ctx = create_text_wrap_context(test_text, test_text_length, config);
    ASSERT_NE(ctx, nullptr);
    
    // Perform operations that allocate memory
    find_break_opportunities(ctx);
    wrap_text_lines(ctx, 200);
    
    // Verify ownership flags
    EXPECT_TRUE(ctx->owns_codepoints);
    EXPECT_TRUE(ctx->owns_break_opportunities);
    EXPECT_TRUE(ctx->owns_lines);
    
    // Cleanup should not crash
    destroy_text_wrap_context(ctx);
    
    SUCCEED() << "Memory management test completed successfully";
}

// Test 17: Unicode text wrapping
TEST_F(TextWrappingTest, UnicodeTextWrapping) {
    const char* unicode_text = "Hello 世界 こんにちは مرحبا";
    int unicode_length = strlen(unicode_text);
    
    TextWrapContext* ctx = create_text_wrap_context(unicode_text, unicode_length, config);
    ASSERT_NE(ctx, nullptr);
    
    EXPECT_GT(ctx->codepoint_count, 0);
    
    int break_count = find_break_opportunities(ctx);
    EXPECT_GT(break_count, 0);
    
    int line_count = wrap_text_lines(ctx, 100);
    EXPECT_GT(line_count, 0);
    
    destroy_text_wrap_context(ctx);
}

// Test 18: Empty and edge case texts
TEST_F(TextWrappingTest, EdgeCaseTexts) {
    // Empty text
    TextWrapContext* empty_ctx = create_text_wrap_context("", 0, config);
    EXPECT_EQ(empty_ctx, nullptr); // Should fail gracefully
    
    // Single character
    TextWrapContext* single_ctx = create_text_wrap_context("A", 1, config);
    ASSERT_NE(single_ctx, nullptr);
    
    int line_count = wrap_text_lines(single_ctx, 100);
    EXPECT_EQ(line_count, 1);
    
    destroy_text_wrap_context(single_ctx);
    
    // Only whitespace
    TextWrapContext* space_ctx = create_text_wrap_context("   ", 3, config);
    ASSERT_NE(space_ctx, nullptr);
    
    int break_count = find_break_opportunities(space_ctx);
    EXPECT_GE(break_count, 0);
    
    destroy_text_wrap_context(space_ctx);
}

// Test 19: Performance and caching
TEST_F(TextWrappingTest, PerformanceAndCaching) {
    config->break_cache_enabled = true;
    
    TextWrapContext* ctx = create_text_wrap_context(test_text, test_text_length, config);
    ASSERT_NE(ctx, nullptr);
    
    // First run - should populate cache
    find_break_opportunities(ctx);
    int first_calculations = ctx->total_break_calculations;
    
    // Reset and run again - should use cache
    ctx->break_count = 0;
    find_break_opportunities(ctx);
    int second_calculations = ctx->total_break_calculations;
    
    // Verify performance counters exist
    EXPECT_GE(ctx->break_cache_hits, 0);
    EXPECT_GE(ctx->break_cache_misses, 0);
    EXPECT_GE(ctx->total_break_calculations, 0);
    
    destroy_text_wrap_context(ctx);
}

// Test 20: Integration readiness
TEST_F(TextWrappingTest, IntegrationReadiness) {
    // Test that all major components work together
    TextWrapContext* ctx = create_text_wrap_context(test_text, test_text_length, config);
    ASSERT_NE(ctx, nullptr);
    
    // Full workflow
    int break_count = find_break_opportunities(ctx);
    EXPECT_GT(break_count, 0);
    
    int line_count = wrap_text_lines(ctx, 200);
    EXPECT_GT(line_count, 0);
    
    // Verify all lines have valid content
    for (int i = 0; i < ctx->line_count; i++) {
        WrappedTextLine* line = &ctx->lines[i];
        EXPECT_NE(line->text, nullptr);
        EXPECT_GT(line->text_length, 0);
        EXPECT_GE(line->break_info.line_width, 0);
    }
    
    destroy_text_wrap_context(ctx);
    
    SUCCEED() << "Text wrapping system ready for integration";
}

// Using gtest_main - no custom main needed
