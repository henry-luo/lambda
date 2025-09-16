#include "../lib/unit_test/include/criterion/criterion.h"
#include "../lambda/format/format.h"
#include "../lambda/input/input.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/mem-pool/include/mem_pool.h"

extern "C" String* format_data(Item item, String* type, String* flavor, VariableMemPool* pool);
extern "C" Input* input_from_source(const char* source, Url* url, String* type, String* flavor);

// Helper to create Lambda String objects
String* create_lambda_string(const char* text) {
    if (!text) return NULL;
    
    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;
    
    result->len = len;
    strcpy(result->chars, text);
    return result;
}

TestSuite(ascii_formatter_standalone);

Test(ascii_formatter_standalone, basic_addition) {
    // Create a memory pool
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Should create variable pool");
    
    // Create type and flavor strings
    String* type_str = create_lambda_string("math");
    String* flavor_str = create_lambda_string("ascii");
    
    // Parse a simple ASCII math expression
    const char* source = "x + y";
    printf("Parsing source: '%s'\n", source);
    Input* input = input_from_source(source, nullptr, type_str, flavor_str);
    cr_assert_not_null(input, "Should parse successfully");
    cr_assert_neq(input->root.type, ITEM_UNDEFINED, "Should have valid root");
    
    // Debug: Print AST structure
    TypeId root_type = get_type_id(input->root);
    printf("Root type: %d\n", root_type);
    if (root_type == LMD_TYPE_ELEMENT) {
        Element* elem = (Element*)input->root.pointer;
        if (elem) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            const char* element_name = (elmt_type && elmt_type->name.str) ? elmt_type->name.str : "unknown";
            printf("Root element: %s (length: %ld)\n", element_name, elem->length);
            
            for (int i = 0; i < elem->length; i++) {
                TypeId child_type = get_type_id(elem->items[i]);
                printf("  Child %d type: %d\n", i, child_type);
                if (child_type == LMD_TYPE_STRING) {
                    String* str = (String*)elem->items[i].pointer;
                    if (str && str->len > 0) {
                        printf("    String value: '%s'\n", str->chars);
                    }
                }
            }
        }
    }
    
    // Format using standalone ASCII formatter
    String* result = format_math_ascii_standalone(pool, input->root);
    cr_assert_not_null(result);
    cr_assert_not_null(result->chars);
    
    // Check that the result contains the expected ASCII math
    cr_assert(strstr(result->chars, "+") != NULL, "Result should contain '+' operator");
    
    printf("Input: x + y\n");
    printf("Formatted: %s\n", result->chars);
    
    free(type_str);
    free(flavor_str);
    pool_variable_destroy(pool);
}

Test(ascii_formatter_standalone, function_call) {
    // Create a memory pool
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Should create variable pool");
    
    // Create type and flavor strings
    String* type_str = create_lambda_string("math");
    String* flavor_str = create_lambda_string("ascii");
    
    // Parse a function call
    const char* source = "sin(x)";
    Input* input = input_from_source(source, nullptr, type_str, flavor_str);
    cr_assert_not_null(input, "Should parse successfully");
    cr_assert_neq(input->root.type, ITEM_UNDEFINED, "Should have valid root");
    
    // Format using standalone ASCII formatter
    String* result = format_math_ascii_standalone(pool, input->root);
    cr_assert_not_null(result);
    cr_assert_not_null(result->chars);
    
    // Check that the result contains the expected function
    cr_assert(strstr(result->chars, "sin") != NULL, "Result should contain 'sin' function");
    
    printf("Input: sin(x)\n");
    printf("Formatted: %s\n", result->chars);
    
    free(type_str);
    free(flavor_str);
    pool_variable_destroy(pool);
}

Test(ascii_formatter_standalone, greek_letters) {
    // Create a memory pool
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Should create variable pool");
    
    // Create type and flavor strings
    String* type_str = create_lambda_string("math");
    String* flavor_str = create_lambda_string("ascii");
    
    // Parse Greek letters expression
    const char* source = "alpha + beta";
    Input* input = input_from_source(source, nullptr, type_str, flavor_str);
    cr_assert_not_null(input, "Should parse successfully");
    cr_assert_neq(input->root.type, ITEM_UNDEFINED, "Should have valid root");
    
    // Format using standalone ASCII formatter
    String* result = format_math_ascii_standalone(pool, input->root);
    cr_assert_not_null(result);
    cr_assert_not_null(result->chars);
    
    // Check that the result contains the expected Greek letters
    cr_assert(strstr(result->chars, "alpha") != NULL, "Result should contain 'alpha'");
    cr_assert(strstr(result->chars, "beta") != NULL, "Result should contain 'beta'");
    cr_assert(strstr(result->chars, "+") != NULL, "Result should contain '+' operator");
    
    printf("Input: alpha + beta\n");
    printf("Formatted: %s\n", result->chars);
    
    free(type_str);
    free(flavor_str);
    pool_variable_destroy(pool);
}
