// Test error tracking infrastructure with GTest
#include <gtest/gtest.h>
#include "lambda/input/input_context.hpp"
#include "lambda/input/parse_error.hpp"
#include "lambda/input/source_tracker.hpp"
#include "lambda/input/input.hpp"
#include "lambda/lambda.h"
#include <cstring>

using namespace lambda;

TEST(ErrorTrackingTests, SourceLocation) {
    SourceLocation loc(10, 5, 12);
    EXPECT_EQ(loc.offset, 10u);
    EXPECT_EQ(loc.line, 5u);
    EXPECT_EQ(loc.column, 12u);
    EXPECT_TRUE(loc.isValid());

    SourceLocation invalid(0, 0, 0);
    EXPECT_FALSE(invalid.isValid());
}

TEST(ErrorTrackingTests, SourceTracker) {
    const char* source = "line 1\nline 2\nline 3";
    SourceTracker tracker(source, strlen(source));

    EXPECT_EQ(tracker.line(), 1u);
    EXPECT_EQ(tracker.column(), 1u);
    EXPECT_EQ(tracker.current(), 'l');

    // Advance to newline
    tracker.advance(6);  // "line 1"
    EXPECT_EQ(tracker.current(), '\n');

    tracker.advance(1);  // Move past newline
    EXPECT_EQ(tracker.line(), 2u);
    EXPECT_EQ(tracker.column(), 1u);

    // Test line extraction
    std::string line1 = tracker.extractLine(1);
    EXPECT_EQ(line1, "line 1");

    std::string line2 = tracker.extractLine(2);
    EXPECT_EQ(line2, "line 2");
}

TEST(ErrorTrackingTests, ParseErrorList) {
    ParseErrorList errors(5);  // Max 5 errors

    SourceLocation loc1(0, 1, 5);
    errors.addError(loc1, "Test error 1");
    EXPECT_EQ(errors.errorCount(), 1u);
    EXPECT_TRUE(errors.hasErrors());

    SourceLocation loc2(10, 2, 3);
    errors.addWarning(loc2, "Test warning");
    EXPECT_EQ(errors.errorCount(), 1u);
    EXPECT_EQ(errors.warningCount(), 1u);

    // Test formatting
    std::string formatted = errors.formatErrors();
    EXPECT_FALSE(formatted.empty());
    EXPECT_NE(formatted.find("Test error 1"), std::string::npos);
    EXPECT_NE(formatted.find("Test warning"), std::string::npos);
}

TEST(ErrorTrackingTests, InputContext) {
    // Create Input using InputManager for proper initialization
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);

    const char* source = "test source\nline 2";
    InputContext ctx(input, source, strlen(source));

    EXPECT_TRUE(ctx.hasTracker());
    EXPECT_EQ(ctx.input(), input);

    // Add error at current position
    ctx.addError("Test error from context");
    EXPECT_TRUE(ctx.hasErrors());
    EXPECT_EQ(ctx.errorCount(), 1u);

    // Add error at specific location
    SourceLocation loc(5, 1, 6);
    ctx.addWarning(loc, "Test warning at specific location");
    EXPECT_EQ(ctx.errorCount(), 1u);  // Still 1 error
    EXPECT_TRUE(ctx.hasWarnings());

    std::string formatted = ctx.formatErrors();
    EXPECT_FALSE(formatted.empty());

    // Note: Input cleanup is handled by InputManager
}

TEST(ErrorTrackingTests, VariadicErrorFormatting) {
    // Test variadic addError at current position
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);

    const char* source = "line 1\nline 2\nline 3";
    InputContext ctx(input, source, strlen(source));

    // Test variadic error formatting
    int line_num = 42;
    int col_num = 15;
    ctx.addError("Parse error at line %d, column %d", line_num, col_num);

    EXPECT_TRUE(ctx.hasErrors());
    EXPECT_EQ(ctx.errorCount(), 1u);

    std::string formatted = ctx.formatErrors();
    EXPECT_NE(formatted.find("Parse error at line 42, column 15"), std::string::npos);
}

TEST(ErrorTrackingTests, VariadicWarningFormatting) {
    // Test variadic addWarning
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);

    InputContext ctx(input);

    // Test variadic warning with multiple parameters
    const char* field_name = "username";
    int max_length = 50;
    int actual_length = 75;
    ctx.addWarning("Field '%s' exceeds max length of %d (got %d)",
                   field_name, max_length, actual_length);

    EXPECT_TRUE(ctx.hasWarnings());
    EXPECT_EQ(ctx.warningCount(), 1u);

    std::string formatted = ctx.formatErrors();
    EXPECT_NE(formatted.find("Field 'username' exceeds max length of 50 (got 75)"),
              std::string::npos);
}

TEST(ErrorTrackingTests, VariadicNoteFormatting) {
    // Test variadic addNote
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);

    InputContext ctx(input);

    // Test variadic note with multiple types
    int row_count = 1250;
    int col_count = 8;
    double parse_time = 3.14;
    ctx.addNote("Parsed %d rows with %d columns in %.2f seconds",
                row_count, col_count, parse_time);

    EXPECT_EQ(ctx.errorCount(), 0u);  // Notes don't count as errors

    std::string formatted = ctx.formatErrors();
    EXPECT_NE(formatted.find("Parsed 1250 rows with 8 columns in 3.14 seconds"),
              std::string::npos);
}

TEST(ErrorTrackingTests, VariadicWithLocation) {
    // Test variadic methods with explicit SourceLocation
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);

    const char* source = "first line\nsecond line\nthird line";
    InputContext ctx(input, source, strlen(source));

    // Test variadic error with location
    SourceLocation loc1(11, 2, 1);  // Start of second line
    ctx.addError(loc1, "Invalid token '%s' at position %d", "@@", 11);

    // Test variadic warning with location
    SourceLocation loc2(23, 3, 1);  // Start of third line
    ctx.addWarning(loc2, "Deprecated syntax on line %d", 3);

    // Test variadic note with location
    SourceLocation loc3(0, 1, 1);  // Start of first line
    ctx.addNote(loc3, "Processing section %d of %d", 1, 5);

    EXPECT_EQ(ctx.errorCount(), 1u);
    EXPECT_EQ(ctx.warningCount(), 1u);

    std::string formatted = ctx.formatErrors();
    EXPECT_NE(formatted.find("Invalid token '@@' at position 11"), std::string::npos);
    EXPECT_NE(formatted.find("Deprecated syntax on line 3"), std::string::npos);
    EXPECT_NE(formatted.find("Processing section 1 of 5"), std::string::npos);
}

TEST(ErrorTrackingTests, VariadicComplexFormatting) {
    // Test complex format strings with multiple types
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);

    InputContext ctx(input);

    // Test with various format specifiers
    ctx.addError("Error: expected %s but got %s at offset 0x%X", "STRING", "NUMBER", 0xFF);
    ctx.addWarning("Column mismatch: row %d has %d columns (expected %d)", 42, 5, 8);
    ctx.addNote("Statistics: %.1f%% complete (%d/%d items)", 75.5, 3, 4);

    std::string formatted = ctx.formatErrors();
    EXPECT_NE(formatted.find("expected STRING but got NUMBER at offset 0xFF"), std::string::npos);
    EXPECT_NE(formatted.find("row 42 has 5 columns (expected 8)"), std::string::npos);
    EXPECT_NE(formatted.find("75.5% complete (3/4 items)"), std::string::npos);
}

TEST(ErrorTrackingTests, VariadicEdgeCases) {
    // Test edge cases for variadic methods
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);

    InputContext ctx(input);

    // Empty format string
    ctx.addError("");
    EXPECT_EQ(ctx.errorCount(), 1u);

    // Single parameter
    ctx.addWarning("Warning: %d", 123);
    EXPECT_EQ(ctx.warningCount(), 1u);

    // Long message
    ctx.addNote("This is a very long note message with parameter %d and another %s and more %d",
                1, "text", 2);

    std::string formatted = ctx.formatErrors();
    EXPECT_FALSE(formatted.empty());
    EXPECT_NE(formatted.find("This is a very long note message"), std::string::npos);
}
