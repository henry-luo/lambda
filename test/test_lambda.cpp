#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // for getcwd and chdir
#include <assert.h>
#include <string.h>
#include <tree_sitter/api.h>
#include <mpdecimal.h>

// Include C header and declare extern C functions from the lambda project
#include "../lambda/lambda.h"
extern "C" {
#include "../lib/strbuf.h"
#include "../lib/num_stack.h"
}

// Missing function implementations to avoid linking conflicts
Context* create_test_context(void) {
    Context* ctx = (Context*)calloc(1, sizeof(Context));
    if (!ctx) return NULL;
    
    // Initialize basic context fields
    ctx->decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    if (ctx->decimal_ctx) {
        mpd_defaultcontext(ctx->decimal_ctx);
    }
    
    // Initialize num_stack and heap to avoid crashes
    ctx->num_stack = num_stack_create(1024);
    ctx->heap = NULL;
    
    return ctx;
}


extern "C" const TSLanguage *tree_sitter_lambda(void);

TSParser* lambda_parser(void) {
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_lambda());
    return parser;
}

TSTree* lambda_parse_source(TSParser* parser, const char* source_code) {
    TSTree* tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    return tree;
}

// Forward declarations for C interface functions from the lambda runtime
typedef struct Runtime Runtime;

// C++ function declarations
void runtime_init(Runtime* runtime);
void runtime_cleanup(Runtime* runtime);
Item run_script_at(Runtime *runtime, char* script_path, bool transpile_only);

extern "C" {
    char* read_text_file(const char* file_path);
    void write_text_file(const char* file_path, const char* content);
    const TSLanguage *tree_sitter_lambda(void);
    num_stack_t* num_stack_create(size_t capacity);
    void format_item(StrBuf *strbuf, Item item, int depth, char* indent);
}

// Simple C-compatible Runtime structure definition
// This should match the actual Runtime structure
typedef struct Runtime {
    void* scripts;     // ArrayList* scripts
    void* parser;      // TSParser* parser
    char* current_dir;
} Runtime;

// Setup and teardown functions
void setup(void) {
    // Runs before each test
}

void teardown(void) {
    // Runs after each test
}

TestSuite(lambda_tests, .init = setup, .fini = teardown);

// Function to trim whitespace from the end of a string
void trim_trailing_whitespace(char* str) {
    if (!str) return;
    
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

// Helper function to test a lambda script against expected output
void test_lambda_script_against_file(const char* script_path, const char* expected_output_path) { 
    // Initialize runtime
    Runtime runtime;
    runtime_init(&runtime);  runtime.current_dir = (char*)"";
    
    // Run the script
    Item ret = run_script_at(&runtime, (char*)script_path, false);
    printf("TRACE: test runner - ret: %llu\n", ret.item);
    
    StrBuf* output_buf = strbuf_new_cap(1024);
    // Cast uint64_t to Item (which is uint64_t in C)
    format_item(output_buf, ret, 0, (char*)" ");
    printf("TRACE: test runner - formatted output: '%s'\n", output_buf->str);
    
    // Extract script name from path for output file
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;
    
    // Create output filename by replacing .ls with .txt
    char output_filename[512];
    snprintf(output_filename, sizeof(output_filename), "test_output/%s", script_name);
    char* dot = strrchr(output_filename, '.');
    if (dot) { strcpy(dot, ".txt"); }
    
    // Save actual output to test_output directory
    write_text_file(output_filename, output_buf->str);
    printf("TRACE: Saved actual output to %s\n", output_filename);
        
    // Read expected output to verify the file exists
    char* expected_output = read_text_file(expected_output_path);
    
    cr_assert_neq(expected_output, NULL, "Failed to read expected output file: %s", expected_output_path);
    
    // Check that the script ran without error (assuming 0 means error)
    cr_assert_neq(ret.item, 0ULL, "Lambda script returned error. Script: %s", script_path);

    // Verify the expected output matches the actual output
    cr_assert_eq(strcmp(expected_output, output_buf->str), 0,
                 "Output does not match expected output for script: %s\nExpected:\n%s\nGot:\n%s",
                 script_path, expected_output, output_buf->str);
    printf("expect length: %d, got length: %d\n", (int)strlen(expected_output), (int)strlen(output_buf->str));
    assert(strlen(expected_output) == output_buf->length);
    
    free(expected_output);  strbuf_free(output_buf);
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_single_ls) {
    test_lambda_script_against_file("test/lambda/single.ls", "test/lambda/single.txt");
}

Test(lambda_tests, test_value_ls) {
    test_lambda_script_against_file("test/lambda/value.ls", "test/lambda/value.txt");
}

Test(lambda_tests, test_simple_expr_ls) {
    test_lambda_script_against_file("test/lambda/simple_expr.ls", "test/lambda/simple_expr.txt");
}

Test(lambda_tests, test_expr_ls) {
    test_lambda_script_against_file("test/lambda/expr.ls", "test/lambda/expr.txt");
}

Test(lambda_tests, test_decimal_ls) {
    test_lambda_script_against_file("test/lambda/decimal.ls", "test/lambda/decimal.txt");
}

Test(lambda_tests, test_box_unbox_ls) {
    test_lambda_script_against_file("test/lambda/box_unbox.ls", "test/lambda/box_unbox.txt");
}

Test(lambda_tests, test_sys_fn_ls) {
    test_lambda_script_against_file("test/lambda/sys_fn.ls", "test/lambda/sys_fn.txt");
}

Test(lambda_tests, test_expr_stam_ls) {
    test_lambda_script_against_file("test/lambda/expr_stam.ls", "test/lambda/expr_stam.txt");
}

Test(lambda_tests, test_numeric_expr_ls) {
    test_lambda_script_against_file("test/lambda/numeric_expr.ls", "test/lambda/numeric_expr.txt");
}

Test(lambda_tests, test_array_float_ls) {
    test_lambda_script_against_file("test/lambda/array_float.ls", "test/lambda/array_float.txt");
}

Test(lambda_tests, test_comparison_expr_ls) {
    test_lambda_script_against_file("test/lambda/comparison_expr.ls", "test/lambda/comparison_expr.txt");
}

Test(lambda_tests, test_unicode_ls) {
    test_lambda_script_against_file("test/lambda/unicode.ls", "test/lambda/unicode.txt");
}

Test(lambda_tests, test_type_ls) {
    test_lambda_script_against_file("test/lambda/type.ls", "test/lambda/type.txt");
}

Test(lambda_tests, test_func_ls) {
    test_lambda_script_against_file("test/lambda/func.ls", "test/lambda/func.txt");
}

Test(lambda_tests, test_int64_ls) {
    test_lambda_script_against_file("test/lambda/int64.ls", "test/lambda/int64.txt");
}

Test(lambda_tests, test_input_dir_ls) {
    test_lambda_script_against_file("test/lambda/input_dir.ls", "test/lambda/input_dir.txt");
}