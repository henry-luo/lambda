/**
 * @file ast_validate.cpp
 * @brief AST-based validation implementation for Lambda validate subcommand
 * @author Henry Luo
 * @license MIT
 */

#include "validator.hpp"
#include "../validator.h"
#include "../transpiler.hpp"
#include "../ast.hpp"
#include "../lambda-data.hpp"
#include "../../lib/mem-pool/include/mem_pool.h"
#include "../../lib/log.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

// Simple validation result structure for compatibility with existing interface
typedef struct SimpleValidationResult {
    bool valid;
    int error_count;
    char** error_messages;
    VariableMemPool* pool;  // For cleanup
} SimpleValidationResult;

// Create a simple validation result
SimpleValidationResult* create_simple_validation_result(VariableMemPool* pool) {
    SimpleValidationResult* result = (SimpleValidationResult*)pool_calloc(pool, sizeof(SimpleValidationResult));
    if (!result) return nullptr;
    
    result->valid = true;
    result->error_count = 0;
    result->error_messages = nullptr;
    result->pool = pool;
    return result;
}

// Add error message to simple validation result
void add_simple_error(SimpleValidationResult* result, const char* message) {
    if (!result || !message) return;
    
    // Reallocate error messages array
    char** new_messages = (char**)pool_calloc(result->pool, sizeof(char*) * (result->error_count + 1));
    if (!new_messages) return;
    
    // Copy existing messages
    for (int i = 0; i < result->error_count; i++) {
        new_messages[i] = result->error_messages[i];
    }
    
    // Add new message
    size_t msg_len = strlen(message);
    char* new_msg = (char*)pool_calloc(result->pool, msg_len + 1);
    if (new_msg) {
        strcpy(new_msg, message);
        new_messages[result->error_count] = new_msg;
        result->error_count++;
        result->valid = false;
    }
    
    result->error_messages = new_messages;
}

// Convert AST validation result to simple validation result
SimpleValidationResult* convert_ast_result(AstValidationResult* ast_result, VariableMemPool* pool) {
    SimpleValidationResult* result = create_simple_validation_result(pool);
    if (!result) return nullptr;
    
    if (!ast_result) {
        add_simple_error(result, "Validation failed to run");
        return result;
    }
    
    result->valid = ast_result->valid;
    
    // Convert AST errors to simple error messages
    AstValidationError* error = ast_result->errors;
    while (error) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "%s (path: %s)", 
                error->message ? error->message : "Unknown error",
                error->path ? error->path : "root");
        add_simple_error(result, error_msg);
        error = error->next;
    }
    
    return result;
}

// Validate a Lambda AST using the new AST validator
SimpleValidationResult* validate_lambda_ast(AstNode* ast, VariableMemPool* pool) {
    if (!ast || !pool) {
        SimpleValidationResult* result = create_simple_validation_result(pool);
        add_simple_error(result, "Invalid AST or memory pool");
        return result;
    }
    
    // Create AST validator
    AstValidator* validator = ast_validator_create(pool);
    if (!validator) {
        SimpleValidationResult* result = create_simple_validation_result(pool);
        add_simple_error(result, "Failed to create AST validator");
        return result;
    }
    
    // Create validation context
    AstValidationContext ctx;
    ctx.pool = pool;
    ctx.current_path = "root";
    ctx.current_depth = 0;
    ctx.max_depth = 100;  // Reasonable depth limit
    
    // Validate the AST
    AstValidationResult* ast_result = validate_ast_node(validator, ast, &ctx);
    
    // Convert to simple result
    SimpleValidationResult* result = convert_ast_result(ast_result, pool);
    
    // Cleanup validator (memory pool will handle the rest)
    ast_validator_destroy(validator);
    
    return result;
}

// Parse and validate a Lambda source file
SimpleValidationResult* validate_lambda_source(const char* source_content, VariableMemPool* pool) {
    if (!source_content || !pool) {
        SimpleValidationResult* result = create_simple_validation_result(pool);
        add_simple_error(result, "Invalid source content or memory pool");
        return result;
    }
    
    // Create transpiler for parsing
    Transpiler* transpiler = transpiler_create(pool);
    if (!transpiler) {
        SimpleValidationResult* result = create_simple_validation_result(pool);
        add_simple_error(result, "Failed to create transpiler");
        return result;
    }
    
    // Build AST from source
    AstNode* ast = transpiler_build_ast(transpiler, source_content);
    if (!ast) {
        SimpleValidationResult* result = create_simple_validation_result(pool);
        add_simple_error(result, "Failed to parse Lambda source");
        transpiler_destroy(transpiler);
        return result;
    }
    
    // Validate the AST
    SimpleValidationResult* result = validate_lambda_ast(ast, pool);
    
    // Cleanup
    transpiler_destroy(transpiler);
    
    return result;
}

// Read file and validate Lambda content
SimpleValidationResult* validate_lambda_file(const char* file_path, VariableMemPool* pool) {
    if (!file_path || !pool) {
        SimpleValidationResult* result = create_simple_validation_result(pool);
        add_simple_error(result, "Invalid file path or memory pool");
        return result;
    }
    
    // Read file content
    extern "C" char* read_text_file(const char* filename);
    char* source_content = read_text_file(file_path);
    if (!source_content) {
        SimpleValidationResult* result = create_simple_validation_result(pool);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Failed to read file: %s", file_path);
        add_simple_error(result, error_msg);
        return result;
    }
    
    // Validate the source
    SimpleValidationResult* result = validate_lambda_source(source_content, pool);
    
    // Cleanup
    free(source_content);
    
    return result;
}

// New AST-based validation function that replaces run_validation
ValidationResult* run_ast_validation(const char* data_file, const char* schema_file, const char* input_format) {
    printf("Lambda AST Validator v2.0\n");
    printf("Validating '%s' using AST-based validation\n", data_file);
    
    if (schema_file) {
        printf("Note: Schema file '%s' ignored (AST validation uses built-in rules)\n", schema_file);
    }
    if (input_format && strcmp(input_format, "lambda") != 0) {
        printf("Note: Input format '%s' ignored (AST validation is Lambda-specific)\n", input_format);
    }
    
    // Create memory pool
    VariableMemPool* pool = pool_create(1024 * 1024); // 1MB pool
    if (!pool) {
        printf("Error: Failed to create memory pool\n");
        return nullptr;
    }
    
    // Validate the Lambda file
    SimpleValidationResult* simple_result = validate_lambda_file(data_file, pool);
    if (!simple_result) {
        printf("Error: Validation failed to run\n");
        pool_destroy(pool);
        return nullptr;
    }
    
    // Create compatibility result
    ValidationResult* result = (ValidationResult*)malloc(sizeof(ValidationResult));
    if (!result) {
        pool_destroy(pool);
        return nullptr;
    }
    
    result->valid = simple_result->valid;
    result->error_count = simple_result->error_count;
    result->warning_count = 0;
    result->errors = simple_result;  // Store simple result for cleanup
    result->warnings = nullptr;
    
    // Print results
    printf("\n=== AST Validation Results ===\n");
    if (result->valid) {
        printf("✅ Validation PASSED\n");
        printf("✓ Lambda file '%s' has valid AST structure\n", data_file);
    } else {
        printf("❌ Validation FAILED\n");
        printf("Errors found: %d\n", result->error_count);
        
        // Print error details
        for (int i = 0; i < simple_result->error_count; i++) {
            printf("  Error %d: %s\n", i + 1, simple_result->error_messages[i]);
        }
    }
    
    return result;
}

// Cleanup function for validation results
void ast_validation_result_destroy(ValidationResult* result) {
    if (!result) return;
    
    if (result->errors) {
        SimpleValidationResult* simple_result = (SimpleValidationResult*)result->errors;
        if (simple_result->pool) {
            pool_destroy(simple_result->pool);
        }
    }
    
    free(result);
}

}
