#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mir.h"
#include "mir-gen.h"
#include "c2mir.h"
#include "../lib/strbuf.h"
// #include "../lambda/lambda.h"

// Function declarations from mir.c
MIR_context_t jit_init(void);
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, const char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
void jit_cleanup(MIR_context_t ctx);

// Setup and teardown functions
void setup(void) {
    // Runs before each test
}

void teardown(void) {
    // Runs after each test
}

TestSuite(mir_tests, .init = setup, .fini = teardown);

// Test basic MIR initialization and cleanup
Test(mir_tests, test_jit_init_cleanup) {
    MIR_context_t ctx = jit_init();
    cr_assert_not_null(ctx, "JIT context should be initialized");
    jit_cleanup(ctx);
}

// Test JIT compilation of a simple hello world program
Test(mir_tests, test_jit_compile_hello_world) {
    const char *hello_world_code = 
        "char* hello_world() {\n"
        "    return \"Hello, World!\";\n"
        "}\n";
    
    MIR_context_t ctx = jit_init();
    cr_assert_not_null(ctx, "JIT context should be initialized");
    
    // Compile the hello world code
    jit_compile_to_mir(ctx, hello_world_code, strlen(hello_world_code), "hello_world.c");
    
    // Try to generate the function
    void* func_ptr = jit_gen_func(ctx, "hello_world");
    cr_assert_not_null(func_ptr, "Function pointer should not be null after compilation");
    
    // Test the function execution
    if (func_ptr) {
        const char* (*test_func)(void) = (const char* (*)(void))func_ptr;
        const char* result = test_func();
        cr_assert_str_eq(result, "Hello, World!", "Function should return 'Hello, World!'");
    }
    
    // Clean up
    jit_cleanup(ctx);
}

// Test JIT compilation of a simple math function
Test(mir_tests, test_jit_compile_math_function) {
    const char *math_code = 
        "int add_numbers(int a, int b) {\n"
        "    return a + b;\n"
        "}\n";
    
    MIR_context_t ctx = jit_init();
    cr_assert_not_null(ctx, "JIT context should be initialized");
    
    // Compile the math code
    jit_compile_to_mir(ctx, math_code, strlen(math_code), "math.c");
    
    // Try to generate the function
    void* func_ptr = jit_gen_func(ctx, "add_numbers");
    cr_assert_not_null(func_ptr, "Function pointer should not be null after compilation");
    
    // Clean up
    jit_cleanup(ctx);
}

// #include "../lambda/lambda-embed.h"
// // Test JIT compilation of a function that returns a value
// Test(mir_tests, test_jit_compile_return_value) {
//     const char *return_code = 
//         "int get_constant() {\n"
//         "    if (sizeof(int64_t) == 8 && sizeof(uint64_t) == 8 && sizeof(int32_t) == 4 && sizeof(uint32_t) == 4 && sizeof(int16_t) == 2 && sizeof(uint16_t) == 2) {\n"
//         "        return 1;\n"
//         "    }\n"
//         "    return 0;\n"
//         "}\n";
    
//     MIR_context_t ctx = jit_init();
//     cr_assert_not_null(ctx, "JIT context should be initialized");
    
//     StrBuf *sb = strbuf_new();
//     strbuf_append_str_n(sb, lambda_lambda_h, lambda_lambda_h_len);
//     strbuf_append_str(sb, return_code);
//     // Compile the return code
//     jit_compile_to_mir(ctx, sb->str, sb->length, "return_test.c");
    
//     // Try to generate the function
//     void* func_ptr = jit_gen_func(ctx, "get_constant");
//     cr_assert_not_null(func_ptr, "Function pointer should not be null after compilation");
    
//     // If we can get the function pointer, we can try to call it
//     if (func_ptr) {
//         int (*test_func)(void) = (int (*)(void))func_ptr;
//         int result = test_func();
//         cr_assert_eq(result, 1, "Function should return 1");
//     }
    
//     // Clean up
//     jit_cleanup(ctx);
// }

// Test JIT compilation of multiple functions
Test(mir_tests, test_jit_compile_multiple_functions) {
    const char *multi_code = 
        "int multiply(int a, int b) {\n"
        "    return a * b;\n"
        "}\n"
        "int subtract(int a, int b) {\n"
        "    return a - b;\n"
        "}\n";
    
    MIR_context_t ctx = jit_init();
    cr_assert_not_null(ctx, "JIT context should be initialized");
    
    // Compile the multi function code
    jit_compile_to_mir(ctx, multi_code, strlen(multi_code), "multi.c");
    
    // Try to generate the first function
    void* multiply_ptr = jit_gen_func(ctx, "multiply");
    cr_assert_not_null(multiply_ptr, "Multiply function pointer should not be null");
    
    // Create a separate context for the second function to avoid redefinition issues
    MIR_context_t ctx2 = jit_init();
    const char *subtract_code = 
        "int subtract(int a, int b) {\n"
        "    return a - b;\n"
        "}\n";
    
    jit_compile_to_mir(ctx2, subtract_code, strlen(subtract_code), "subtract.c");
    void* subtract_ptr = jit_gen_func(ctx2, "subtract");
    cr_assert_not_null(subtract_ptr, "Subtract function pointer should not be null");
    
    // Clean up both contexts
    jit_cleanup(ctx);
    jit_cleanup(ctx2);
}

// Test error handling for non-existent function
Test(mir_tests, test_jit_nonexistent_function) {
    const char *simple_code = 
        "int simple_func() {\n"
        "    return 1;\n"
        "}\n";
    
    MIR_context_t ctx = jit_init();
    cr_assert_not_null(ctx, "JIT context should be initialized");
    
    // Compile the code
    jit_compile_to_mir(ctx, simple_code, strlen(simple_code), "simple.c");
    
    // Try to generate a function that doesn't exist
    void* func_ptr = jit_gen_func(ctx, "nonexistent_function");
    cr_assert_null(func_ptr, "Function pointer should be null for non-existent function");
    
    // Clean up
    jit_cleanup(ctx);
}

// Test with empty code
Test(mir_tests, test_jit_empty_code) {
    const char *empty_code = "";
    
    MIR_context_t ctx = jit_init();
    cr_assert_not_null(ctx, "JIT context should be initialized");
    
    // Compile empty code - should not crash
    jit_compile_to_mir(ctx, empty_code, strlen(empty_code), "empty.c");
    
    // Clean up
    jit_cleanup(ctx);
}

// Test with simple variable usage
Test(mir_tests, test_jit_with_variables) {
    const char *var_code = 
        "int use_variables() {\n"
        "    int x = 10;\n"
        "    int y = 20;\n"
        "    return x + y;\n"
        "}\n";
    
    MIR_context_t ctx = jit_init();
    cr_assert_not_null(ctx, "JIT context should be initialized");
    
    // Compile the variable code
    jit_compile_to_mir(ctx, var_code, strlen(var_code), "variables.c");
    
    // Try to generate the function
    void* func_ptr = jit_gen_func(ctx, "use_variables");
    cr_assert_not_null(func_ptr, "Function pointer should not be null after compilation");
    
    // Test the function execution
    if (func_ptr) {
        int (*test_func)(void) = (int (*)(void))func_ptr;
        int result = test_func();
        cr_assert_eq(result, 30, "Function should return 30 (10 + 20)");
    }
    
    // Clean up
    jit_cleanup(ctx);
}
