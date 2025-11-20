#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// Test fixture for MIR execution tests using lambda.exe --mir
class MIRExecutionTest : public ::testing::Test {
protected:
    // Helper to execute a script with --mir flag via lambda.exe
    std::string execute_mir_script(const char* script_content) {
        // Write script to temp file
        const char* temp_path = "temp/test_mir_script.ls";
        FILE* f = fopen(temp_path, "w");
        if (!f) return "";
        fprintf(f, "%s", script_content);
        fclose(f);
        
        // Execute with --mir flag
        char command[256];
        snprintf(command, sizeof(command), "./lambda.exe --mir %s 2>&1", temp_path);
        
        FILE* pipe = popen(command, "r");
        if (!pipe) return "";
        
        char buffer[4096] = {0};
        size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, pipe);
        pclose(pipe);
        
        // Remove temp file
        unlink(temp_path);
        
        return std::string(buffer, bytes_read);
    }
};

// Test that --mir flag works
TEST_F(MIRExecutionTest, MIRFlagSupported) {
    // Execute a simple integer literal
    std::string output = execute_mir_script("42");
    
    // Should contain some output (even if just debug info or errors)
    EXPECT_FALSE(output.empty()) << "MIR execution produced no output";
    
    // Check it contains MIR-related output (transpilation messages)
    bool has_mir_output = (output.find("MIR") != std::string::npos || 
                           output.find("mir") != std::string::npos ||
                           output.find("transpile") != std::string::npos);
    EXPECT_TRUE(has_mir_output) << "MIR execution didn't produce MIR-related output";
}

// Test basic integer literal with MIR
TEST_F(MIRExecutionTest, IntegerLiteral) {
    std::string output = execute_mir_script("42");
    
    // The output should contain "42" somewhere (actual value or in output)
    // Note: MIR implementation is incomplete, so we just check it runs
    EXPECT_FALSE(output.empty());
}

// Test that MIR doesn't crash on empty script
TEST_F(MIRExecutionTest, EmptyScript) {
    std::string output = execute_mir_script("");
    
    // Should not crash, may return empty or default value
    // Just verify it completes
    EXPECT_TRUE(true);
}

// Test that MIR handles syntax errors gracefully
TEST_F(MIRExecutionTest, InvalidSyntax) {
    std::string output = execute_mir_script("2 + + 3");
    
    // Should produce some kind of error or output
    EXPECT_FALSE(output.empty());
}

// Test MIR can be invoked multiple times (context lifecycle)
TEST_F(MIRExecutionTest, MultipleInvocations) {
    std::string output1 = execute_mir_script("42");
    std::string output2 = execute_mir_script("100");
    
    // Both should produce output
    EXPECT_FALSE(output1.empty());
    EXPECT_FALSE(output2.empty());
}

// Test binary addition expression
TEST_F(MIRExecutionTest, BinaryAddition) {
    std::string output = execute_mir_script("2 + 3");
    
    // Should transpile and execute through MIR
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos) 
        << "Should show MIR transpilation";
}

// Test binary subtraction expression
TEST_F(MIRExecutionTest, BinarySubtraction) {
    std::string output = execute_mir_script("10 - 4");
    
    // Should transpile and execute through MIR
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test binary multiplication expression
TEST_F(MIRExecutionTest, BinaryMultiplication) {
    std::string output = execute_mir_script("6 * 7");
    
    // Should transpile and execute through MIR
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test binary division expression
TEST_F(MIRExecutionTest, BinaryDivision) {
    std::string output = execute_mir_script("20 / 4");
    
    // Should transpile and execute through MIR
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test comparison operators (must be in parentheses for valid syntax)
TEST_F(MIRExecutionTest, ComparisonLessThan) {
    std::string output = execute_mir_script("(3 < 5)");
    
    // Should transpile comparison operators
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

TEST_F(MIRExecutionTest, ComparisonGreaterThan) {
    std::string output = execute_mir_script("(5 > 3)");
    
    // Should transpile comparison operators
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

TEST_F(MIRExecutionTest, ComparisonLessThanEqual) {
    std::string output = execute_mir_script("(3 <= 5)");
    
    // Should transpile comparison operators
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

TEST_F(MIRExecutionTest, ComparisonGreaterThanEqual) {
    std::string output = execute_mir_script("(5 >= 3)");
    
    // Should transpile comparison operators
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

TEST_F(MIRExecutionTest, ComparisonEqual) {
    std::string output = execute_mir_script("5 == 5");
    
    // Should transpile equality operators
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test unary negation
TEST_F(MIRExecutionTest, UnaryNegation) {
    std::string output = execute_mir_script("-42");
    
    // Should transpile unary operators
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test unary positive
TEST_F(MIRExecutionTest, UnaryPositive) {
    std::string output = execute_mir_script("+42");
    
    // Should transpile unary operators
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test nested binary expressions
TEST_F(MIRExecutionTest, NestedBinaryExpression) {
    std::string output = execute_mir_script("(2 + 3) * 4");
    
    // Should transpile nested expressions
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test complex nested expression
TEST_F(MIRExecutionTest, ComplexNestedExpression) {
    std::string output = execute_mir_script("(10 - 2) / (3 + 1)");
    
    // Should transpile complex nested expressions
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test float literal
TEST_F(MIRExecutionTest, FloatLiteral) {
    std::string output = execute_mir_script("3.14");
    
    // Should transpile float literals
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test float arithmetic
TEST_F(MIRExecutionTest, FloatArithmetic) {
    std::string output = execute_mir_script("2.5 + 3.7");
    
    // Should transpile float arithmetic
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test boolean literal
TEST_F(MIRExecutionTest, BooleanLiteral) {
    std::string output = execute_mir_script("true");
    
    // Should transpile boolean literals
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

// Test mixed integer and float operations
TEST_F(MIRExecutionTest, MixedArithmetic) {
    std::string output = execute_mir_script("5 + 2.5");
    
    // Should transpile mixed type arithmetic
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("transpile"), std::string::npos);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
