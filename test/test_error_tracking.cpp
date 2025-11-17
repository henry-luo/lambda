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
