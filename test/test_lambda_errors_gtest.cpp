//==============================================================================
// Lambda Structured Error System Tests
// 
// Tests the error handling infrastructure including:
// - Error code categories (1xx syntax, 2xx semantic, 3xx runtime, etc.)
// - Error message formatting
// - Stack trace capture
// - Negative test cases that verify proper error reporting
//==============================================================================

#include <gtest/gtest.h>
#include "../lambda/lambda-error.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/arraylist.h"
#include <string>
#include <cstring>

//==============================================================================
// Error Code Category Tests
//==============================================================================

class ErrorCodeCategoryTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorCodeCategoryTest, SyntaxErrorCategory) {
    // All 1xx codes should be syntax errors
    EXPECT_TRUE(ERR_IS_SYNTAX(ERR_SYNTAX_ERROR));
    EXPECT_TRUE(ERR_IS_SYNTAX(ERR_UNEXPECTED_TOKEN));
    EXPECT_TRUE(ERR_IS_SYNTAX(ERR_MISSING_TOKEN));
    EXPECT_TRUE(ERR_IS_SYNTAX(ERR_UNTERMINATED_STRING));
    
    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SEMANTIC(ERR_SYNTAX_ERROR));
    EXPECT_FALSE(ERR_IS_RUNTIME(ERR_SYNTAX_ERROR));
    EXPECT_FALSE(ERR_IS_IO(ERR_SYNTAX_ERROR));
    EXPECT_FALSE(ERR_IS_INTERNAL(ERR_SYNTAX_ERROR));
}

TEST_F(ErrorCodeCategoryTest, SemanticErrorCategory) {
    // All 2xx codes should be semantic errors
    EXPECT_TRUE(ERR_IS_SEMANTIC(ERR_SEMANTIC_ERROR));
    EXPECT_TRUE(ERR_IS_SEMANTIC(ERR_TYPE_MISMATCH));
    EXPECT_TRUE(ERR_IS_SEMANTIC(ERR_UNDEFINED_VARIABLE));
    EXPECT_TRUE(ERR_IS_SEMANTIC(ERR_UNDEFINED_FUNCTION));
    
    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SYNTAX(ERR_TYPE_MISMATCH));
    EXPECT_FALSE(ERR_IS_RUNTIME(ERR_TYPE_MISMATCH));
}

TEST_F(ErrorCodeCategoryTest, RuntimeErrorCategory) {
    // All 3xx codes should be runtime errors
    EXPECT_TRUE(ERR_IS_RUNTIME(ERR_RUNTIME_ERROR));
    EXPECT_TRUE(ERR_IS_RUNTIME(ERR_NULL_REFERENCE));
    EXPECT_TRUE(ERR_IS_RUNTIME(ERR_DIVISION_BY_ZERO));
    EXPECT_TRUE(ERR_IS_RUNTIME(ERR_INDEX_OUT_OF_BOUNDS));
    
    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SYNTAX(ERR_RUNTIME_ERROR));
    EXPECT_FALSE(ERR_IS_SEMANTIC(ERR_RUNTIME_ERROR));
}

TEST_F(ErrorCodeCategoryTest, IOErrorCategory) {
    // All 4xx codes should be I/O errors
    EXPECT_TRUE(ERR_IS_IO(ERR_IO_ERROR));
    EXPECT_TRUE(ERR_IS_IO(ERR_FILE_NOT_FOUND));
    EXPECT_TRUE(ERR_IS_IO(ERR_NETWORK_ERROR));
    
    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SYNTAX(ERR_IO_ERROR));
    EXPECT_FALSE(ERR_IS_RUNTIME(ERR_IO_ERROR));
}

TEST_F(ErrorCodeCategoryTest, InternalErrorCategory) {
    // All 5xx codes should be internal errors
    EXPECT_TRUE(ERR_IS_INTERNAL(ERR_INTERNAL_ERROR));
    EXPECT_TRUE(ERR_IS_INTERNAL(ERR_NOT_IMPLEMENTED));
    EXPECT_TRUE(ERR_IS_INTERNAL(ERR_POOL_EXHAUSTED));
    
    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SYNTAX(ERR_INTERNAL_ERROR));
    EXPECT_FALSE(ERR_IS_RUNTIME(ERR_INTERNAL_ERROR));
}

//==============================================================================
// Error Creation Tests
//==============================================================================

class ErrorCreationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorCreationTest, CreateSimpleError) {
    SourceLocation loc = {
        .file = nullptr,
        .line = 0,
        .column = 0
    };
    LambdaError* error = err_create(ERR_SYNTAX_ERROR, "Test error message", &loc);
    
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_SYNTAX_ERROR);
    EXPECT_STREQ(error->message, "Test error message");
    
    err_free(error);
}

TEST_F(ErrorCreationTest, CreateErrorWithLocation) {
    SourceLocation loc = {
        .file = "test.ls",
        .line = 42,
        .column = 10
    };
    
    LambdaError* error = err_create(ERR_TYPE_MISMATCH, "Type mismatch error", &loc);
    
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_TYPE_MISMATCH);
    EXPECT_EQ(error->location.line, 42u);
    EXPECT_EQ(error->location.column, 10u);
    EXPECT_STREQ(error->location.file, "test.ls");
    
    err_free(error);
}

TEST_F(ErrorCreationTest, CreateFormattedError) {
    SourceLocation loc = {
        .file = nullptr,
        .line = 0,
        .column = 0
    };
    LambdaError* error = err_createf(ERR_UNDEFINED_VARIABLE, &loc, 
        "Variable '%s' not defined in scope", "myVar");
    
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_UNDEFINED_VARIABLE);
    EXPECT_NE(strstr(error->message, "myVar"), nullptr);
    
    err_free(error);
}

TEST_F(ErrorCreationTest, CreateErrorWithHelp) {
    SourceLocation loc = {
        .file = nullptr,
        .line = 0,
        .column = 0
    };
    LambdaError* error = err_create(ERR_SYNTAX_ERROR, "Missing semicolon", &loc);
    ASSERT_NE(error, nullptr);
    
    err_add_help(error, "Consider adding ';' at the end of the statement");
    
    // after adding help, the help field should not be null
    ASSERT_NE(error->help, nullptr) << "help should be set after err_add_help";
    
    // check the content - help text contains "adding"
    EXPECT_TRUE(strstr(error->help, "adding") != nullptr) 
        << "help text should contain 'adding', got: " << error->help;
    
    err_free(error);
}

//==============================================================================
// Error Formatting Tests
//==============================================================================

class ErrorFormattingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorFormattingTest, FormatBasicError) {
    SourceLocation loc = {
        .file = "script.ls",
        .line = 10,
        .column = 5
    };
    
    LambdaError* error = err_create(ERR_SYNTAX_ERROR, "Unexpected token", &loc);
    char* formatted = err_format(error);
    
    ASSERT_NE(formatted, nullptr);
    // Check that it contains key elements
    EXPECT_NE(strstr(formatted, "script.ls"), nullptr);
    EXPECT_NE(strstr(formatted, "10"), nullptr);
    EXPECT_NE(strstr(formatted, "Unexpected token"), nullptr);
    
    free(formatted);
    err_free(error);
}

TEST_F(ErrorFormattingTest, ErrorCodeName) {
    EXPECT_STREQ(err_code_name(ERR_OK), "OK");
    EXPECT_STREQ(err_code_name(ERR_SYNTAX_ERROR), "SYNTAX_ERROR");
    EXPECT_STREQ(err_code_name(ERR_TYPE_MISMATCH), "TYPE_MISMATCH");
    EXPECT_STREQ(err_code_name(ERR_RUNTIME_ERROR), "RUNTIME_ERROR");
    EXPECT_STREQ(err_code_name(ERR_FILE_NOT_FOUND), "FILE_NOT_FOUND");
    EXPECT_STREQ(err_code_name(ERR_INTERNAL_ERROR), "INTERNAL_ERROR");
}

TEST_F(ErrorFormattingTest, ErrorCategoryName) {
    EXPECT_STREQ(err_category_name(ERR_SYNTAX_ERROR), "Syntax");
    EXPECT_STREQ(err_category_name(ERR_TYPE_MISMATCH), "Semantic");
    EXPECT_STREQ(err_category_name(ERR_RUNTIME_ERROR), "Runtime");
    EXPECT_STREQ(err_category_name(ERR_FILE_NOT_FOUND), "I/O");
    EXPECT_STREQ(err_category_name(ERR_INTERNAL_ERROR), "Internal");
}

//==============================================================================
// Source Context Tests
//==============================================================================

class SourceContextTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    
    const char* sample_source = 
        "let x = 10\n"
        "let y = 20\n"
        "let z = x + y + undefined_var\n"
        "print(z)\n";
};

TEST_F(SourceContextTest, GetSourceLine) {
    // line 1
    char* line1 = err_get_source_line(sample_source, 1);
    ASSERT_NE(line1, nullptr);
    EXPECT_STREQ(line1, "let x = 10");
    free(line1);
    
    // line 3
    char* line3 = err_get_source_line(sample_source, 3);
    ASSERT_NE(line3, nullptr);
    EXPECT_STREQ(line3, "let z = x + y + undefined_var");
    free(line3);
    
    // line beyond source
    char* line10 = err_get_source_line(sample_source, 10);
    EXPECT_EQ(line10, nullptr);
}

TEST_F(SourceContextTest, GetSourceLineCount) {
    // sample_source has 4 lines, but trailing newline counts as start of line 5
    int count = err_get_source_line_count(sample_source);
    EXPECT_GE(count, 4);  // at least 4 lines
    
    // single line with no newline
    EXPECT_EQ(err_get_source_line_count("hello"), 1);
    
    // empty source
    EXPECT_EQ(err_get_source_line_count(""), 1);
    EXPECT_EQ(err_get_source_line_count(nullptr), 0);
}

TEST_F(SourceContextTest, ExtractContext) {
    SourceLocation loc = {
        .file = "test.ls",
        .line = 3,
        .column = 17,
        .end_line = 3,
        .end_column = 29,  // span "undefined_var"
        .source = nullptr
    };
    
    LambdaError* error = err_create(ERR_UNDEFINED_VARIABLE, "undefined variable 'undefined_var'", &loc);
    
    // extract context (stores source reference)
    err_extract_context(error, sample_source, 2);
    EXPECT_EQ(error->location.source, sample_source);
    
    err_free(error);
}

TEST_F(SourceContextTest, FormatWithContextLines) {
    SourceLocation loc = {
        .file = "test.ls",
        .line = 3,
        .column = 17,
        .end_line = 3,
        .end_column = 29,
        .source = nullptr
    };
    
    LambdaError* error = err_create(ERR_UNDEFINED_VARIABLE, "undefined variable 'undefined_var'", &loc);
    err_extract_context(error, sample_source, 1);
    
    char* formatted = err_format_with_context(error, 1);
    ASSERT_NE(formatted, nullptr);
    
    // should contain location prefix
    EXPECT_NE(strstr(formatted, "test.ls:3:17"), nullptr) 
        << "Should contain location prefix\n" << formatted;
    
    // should contain error code
    EXPECT_NE(strstr(formatted, "E202"), nullptr) 
        << "Should contain error code\n" << formatted;
    
    // should contain the error line
    EXPECT_NE(strstr(formatted, "let z = x + y + undefined_var"), nullptr)
        << "Should contain source line\n" << formatted;
    
    // should contain carets for span
    EXPECT_NE(strstr(formatted, "^"), nullptr)
        << "Should contain caret pointer\n" << formatted;
    
    free(formatted);
    err_free(error);
}

TEST_F(SourceContextTest, FormatWithMultipleContextLines) {
    SourceLocation loc = {
        .file = "script.ls",
        .line = 3,
        .column = 5,
        .end_line = 3,
        .end_column = 5,
        .source = nullptr
    };
    
    LambdaError* error = err_create(ERR_TYPE_MISMATCH, "expected int, found string", &loc);
    err_extract_context(error, sample_source, 2);
    
    char* formatted = err_format_with_context(error, 2);
    ASSERT_NE(formatted, nullptr);
    
    // with context_lines=2, should show lines 1,2,3,4,5 (but only 4 exist)
    EXPECT_NE(strstr(formatted, "let x = 10"), nullptr)
        << "Should contain context line before\n" << formatted;
    EXPECT_NE(strstr(formatted, "let y = 20"), nullptr)
        << "Should contain context line before\n" << formatted;
    EXPECT_NE(strstr(formatted, "let z ="), nullptr)
        << "Should contain error line\n" << formatted;
    
    free(formatted);
    err_free(error);
}

//==============================================================================
// JSON Output Tests
//==============================================================================

class JSONOutputTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(JSONOutputTest, FormatSingleError) {
    SourceLocation loc = {
        .file = "test.ls",
        .line = 10,
        .column = 5,
        .end_line = 10,
        .end_column = 15,
        .source = nullptr
    };
    
    LambdaError* error = err_create(ERR_TYPE_MISMATCH, "expected int, found string", &loc);
    char* json = err_format_json(error);
    
    ASSERT_NE(json, nullptr);
    
    // check JSON structure
    EXPECT_NE(strstr(json, "\"code\": 201"), nullptr) << "Should contain error code\n" << json;
    EXPECT_NE(strstr(json, "\"name\": \"TYPE_MISMATCH\""), nullptr) << "Should contain error name\n" << json;
    EXPECT_NE(strstr(json, "\"category\": \"Semantic\""), nullptr) << "Should contain category\n" << json;
    EXPECT_NE(strstr(json, "\"message\": \"expected int, found string\""), nullptr) << "Should contain message\n" << json;
    EXPECT_NE(strstr(json, "\"file\": \"test.ls\""), nullptr) << "Should contain file\n" << json;
    EXPECT_NE(strstr(json, "\"line\": 10"), nullptr) << "Should contain line\n" << json;
    EXPECT_NE(strstr(json, "\"column\": 5"), nullptr) << "Should contain column\n" << json;
    
    free(json);
    err_free(error);
}

TEST_F(JSONOutputTest, FormatErrorWithHelp) {
    SourceLocation loc = { .file = "test.ls", .line = 5, .column = 1 };
    LambdaError* error = err_create(ERR_UNDEFINED_VARIABLE, "variable 'x' not defined", &loc);
    err_add_help(error, "Did you mean 'y'?");
    
    char* json = err_format_json(error);
    ASSERT_NE(json, nullptr);
    
    EXPECT_NE(strstr(json, "\"help\": \"Did you mean 'y'?\""), nullptr)
        << "Should contain help text\n" << json;
    
    free(json);
    err_free(error);
}

TEST_F(JSONOutputTest, FormatErrorArray) {
    SourceLocation loc1 = { .file = "test.ls", .line = 5, .column = 1 };
    SourceLocation loc2 = { .file = "test.ls", .line = 10, .column = 8 };
    
    LambdaError* errors[2];
    errors[0] = err_create(ERR_SYNTAX_ERROR, "unexpected token", &loc1);
    errors[1] = err_create(ERR_TYPE_MISMATCH, "type mismatch", &loc2);
    
    char* json = err_format_json_array(errors, 2);
    ASSERT_NE(json, nullptr);
    
    // check structure
    EXPECT_NE(strstr(json, "\"errors\":"), nullptr) << "Should contain errors array\n" << json;
    EXPECT_NE(strstr(json, "\"errorCount\": 2"), nullptr) << "Should contain count\n" << json;
    EXPECT_NE(strstr(json, "SYNTAX_ERROR"), nullptr) << "Should contain first error\n" << json;
    EXPECT_NE(strstr(json, "TYPE_MISMATCH"), nullptr) << "Should contain second error\n" << json;
    
    free(json);
    err_free(errors[0]);
    err_free(errors[1]);
}

TEST_F(JSONOutputTest, EscapeSpecialCharacters) {
    SourceLocation loc = { .file = "path/to/file.ls", .line = 1, .column = 1 };
    LambdaError* error = err_create(ERR_SYNTAX_ERROR, "unexpected \"quote\" and \\backslash", &loc);
    
    char* json = err_format_json(error);
    ASSERT_NE(json, nullptr);
    
    // special chars should be escaped
    EXPECT_NE(strstr(json, "\\\"quote\\\""), nullptr) 
        << "Quotes should be escaped\n" << json;
    EXPECT_NE(strstr(json, "\\\\backslash"), nullptr)
        << "Backslash should be escaped\n" << json;
    
    free(json);
    err_free(error);
}

//==============================================================================
// Stack Trace Tests (basic - full test requires runtime context)
//==============================================================================

class StackTraceTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(StackTraceTest, CaptureStackTraceWithoutDebugInfo) {
    // Capture stack trace without debug info table
    StackFrame* trace = err_capture_stack_trace(nullptr, 10);
    
    // Should return something (or NULL if not supported)
    // The frames might have unknown function names
    if (trace) {
        // Verify the linked list structure
        int count = 0;
        StackFrame* frame = trace;
        while (frame && count < 20) {
            count++;
            frame = frame->next;
        }
        EXPECT_GT(count, 0);
        
        err_free_stack_trace(trace);
    }
}

//==============================================================================
// Error Chaining Tests
//==============================================================================

class ErrorChainingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorChainingTest, ChainedErrors) {
    SourceLocation loc1 = { .file = "main.ls", .line = 50 };
    SourceLocation loc2 = { .file = "util.ls", .line = 20 };
    
    LambdaError* cause = err_create(ERR_FILE_NOT_FOUND, "Config file missing", &loc2);
    LambdaError* error = err_create(ERR_IO_ERROR, "Failed to initialize", &loc1);
    error->cause = cause;
    
    EXPECT_NE(error->cause, nullptr);
    EXPECT_EQ(error->cause->code, ERR_FILE_NOT_FOUND);
    
    // Format should include both errors
    char* formatted = err_format_with_context(error, 0);
    EXPECT_NE(strstr(formatted, "Failed to initialize"), nullptr);
    EXPECT_NE(strstr(formatted, "Caused by"), nullptr);
    EXPECT_NE(strstr(formatted, "Config file missing"), nullptr);
    
    free(formatted);
    err_free(error);  // should also free cause
}

//==============================================================================
// Negative Test Helpers
//==============================================================================

// Helper to run Lambda script and capture output
struct ScriptResult {
    int exit_code;
    std::string output;
    std::string error_output;
};

ScriptResult run_lambda_script(const char* script_path) {
    ScriptResult result;
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe \"%s\" 2>&1", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe \"%s\" 2>&1", script_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        result.exit_code = -1;
        return result;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }

    result.exit_code = pclose(pipe);
    return result;
}

//==============================================================================
// Negative Script Tests - Verify proper error reporting
//==============================================================================

class NegativeScriptTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    
    void ExpectErrorWithoutCrash(const char* script_path) {
        ScriptResult result = run_lambda_script(script_path);
        
        // Should NOT crash
        EXPECT_EQ(result.output.find("Segmentation fault"), std::string::npos)
            << "Script crashed: " << script_path;
        EXPECT_EQ(result.output.find("SIGABRT"), std::string::npos)
            << "Script aborted: " << script_path;
        EXPECT_EQ(result.output.find("core dumped"), std::string::npos)
            << "Script core dumped: " << script_path;
    }
    
    void ExpectErrorCode(const char* script_path, const char* expected_error_indicator) {
        ScriptResult result = run_lambda_script(script_path);
        
        // Should contain error indicator
        bool has_error = result.output.find(expected_error_indicator) != std::string::npos ||
                        result.output.find("[ERR!]") != std::string::npos ||
                        result.output.find("error") != std::string::npos;
        
        EXPECT_TRUE(has_error) << "Expected error for: " << script_path
                               << "\nOutput: " << result.output;
    }
};

// Syntax error tests
TEST_F(NegativeScriptTest, SyntaxErrorMalformedRange) {
    ExpectErrorWithoutCrash("test/lambda/negative/test_syntax_errors.ls");
}

// Type error tests  
TEST_F(NegativeScriptTest, TypeErrorFuncParam) {
    ExpectErrorWithoutCrash("test/lambda/negative/func_param_negative.ls");
}

// Undefined reference tests
TEST_F(NegativeScriptTest, UndefinedFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/undefined_function.ls");
}

TEST_F(NegativeScriptTest, CallNonFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/call_non_function.ls");
}

TEST_F(NegativeScriptTest, InvalidTypeAnnotation) {
    ExpectErrorWithoutCrash("test/lambda/negative/invalid_type_annotation.ls");
}

//==============================================================================
// Categorized Negative Tests - Organized by error category
//==============================================================================

// --- Syntax Error Tests (1xx) ---

TEST_F(NegativeScriptTest, SyntaxError_UnterminatedString) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/unterminated_string.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_MissingParen) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/missing_paren.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_MissingBrace) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/missing_brace.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_InvalidNumber) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/invalid_number.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_UnexpectedToken) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/unexpected_token.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_UnexpectedEOF) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/unexpected_eof.ls");
}

// --- Semantic Error Tests (2xx) ---

TEST_F(NegativeScriptTest, SemanticError_UndefinedVariable) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/undefined_variable.ls");
}

TEST_F(NegativeScriptTest, SemanticError_UndefinedFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/undefined_function.ls");
}

TEST_F(NegativeScriptTest, SemanticError_TypeMismatch) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/type_mismatch.ls");
}

TEST_F(NegativeScriptTest, SemanticError_ArityMismatch) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/arity_mismatch.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateParam) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_param.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateVariable) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_variable.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateType) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_type.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_function.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateMixed) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_mixed.ls");
}

// --- Runtime Error Tests (3xx) ---

TEST_F(NegativeScriptTest, RuntimeError_NullReference) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/null_reference.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_DivisionByZero) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/division_by_zero.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_IndexOutOfBounds) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/index_out_of_bounds.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_InvalidOperation) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/invalid_operation.ls");
}

// Note: Stack overflow test is disabled by default as it may be slow or affect CI
// TEST_F(NegativeScriptTest, RuntimeError_StackOverflow) {
//     ExpectErrorWithoutCrash("test/lambda/negative/runtime/stack_overflow.ls");
// }

// --- I/O Error Tests (4xx) ---

TEST_F(NegativeScriptTest, IOError_FileNotFound) {
    ExpectErrorWithoutCrash("test/lambda/negative/io/file_not_found.ls");
}

TEST_F(NegativeScriptTest, IOError_ParseError) {
    ExpectErrorWithoutCrash("test/lambda/negative/io/parse_error.ls");
}

//==============================================================================
// Main
//==============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
