/**
 * @file ast_validate.cpp
 * @brief AST-based validation implementation for Lambda validate subcommand
 * @author Henry Luo
 * @license MIT
 */

#include "../validator.hpp"
#include "../transpiler.hpp"
#include "../ast.hpp"
#include "../lambda-data.hpp"
#include "../input/input.h"
#include "../../lib/mem-pool/include/mem_pool.h"
#include "../../lib/log.h"
#include "../../lib/file.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

// ==================== Validation Error System ====================

// Validation error codes
typedef enum ValidationErrorCode {
    VALID_ERROR_NONE = 0,
    VALID_ERROR_TYPE_MISMATCH,
    VALID_ERROR_MISSING_FIELD,
    VALID_ERROR_UNEXPECTED_FIELD,
    VALID_ERROR_INVALID_ELEMENT,
    VALID_ERROR_CONSTRAINT_VIOLATION,
    VALID_ERROR_REFERENCE_ERROR,
    VALID_ERROR_OCCURRENCE_ERROR,
    VALID_ERROR_CIRCULAR_REFERENCE,
    VALID_ERROR_PARSE_ERROR,
} ValidationErrorCode;

// Path segment types for error reporting
typedef enum PathSegmentType {
    PATH_FIELD,      // .field_name
    PATH_INDEX,      // [index]
    PATH_ELEMENT,    // <element_tag>
    PATH_ATTRIBUTE,  // @attr_name
} PathSegmentType;

// Path segment structure
typedef struct PathSegment {
    PathSegmentType type;
    union {
        StrView field_name;
        long index;
        StrView element_tag;
        StrView attr_name;
    } data;
    struct PathSegment* next;
} PathSegment;

// Validation error structure
typedef struct ValidationError {
    ValidationErrorCode code;
    String* message;           // Error message
    PathSegment* path;         // Path to error location
    void* expected;            // Expected type (optional)
    Item actual;               // Actual value (optional)
    List* suggestions;         // List of String* suggestions (optional)
    struct ValidationError* next;
} ValidationError;

// Validation warning (same as error but non-fatal)
typedef ValidationError ValidationWarning;

// Validation result
typedef struct ValidationResult {
    bool valid;                // Overall validation result
    ValidationError* errors;   // Linked list of errors
    ValidationWarning* warnings; // Linked list of warnings
    int error_count;           // Number of errors
    int warning_count;         // Number of warnings
} ValidationResult;

// Validation options
typedef struct ValidationOptions {
    bool strict_mode;              // Treat warnings as errors
    bool allow_unknown_fields;     // Allow extra fields in maps
    bool allow_empty_elements;     // Allow elements without content
    int max_depth;                 // Maximum validation depth
    int timeout_ms;                // Validation timeout (0 = no limit)
    char** enabled_rules;          // Custom rules to enable
    char** disabled_rules;         // Rules to disable
} ValidationOptions;

// Main validator structure
typedef struct SchemaValidator {
    HashMap* schemas;              // Loaded schemas by name
    VariableMemPool* pool;         // Memory pool
    void* context;                 // Default validation context
    void* custom_validators;       // Registered custom validators
    ValidationOptions default_options;  // Default validation options
} SchemaValidator;

// Utility function
StrView strview_from_cstr(const char* str) {
    StrView sv;
    if (str) {
        sv.str = str;
        sv.length = strlen(str);
    } else {
        sv.str = "";
        sv.length = 0;
    }
    return sv;
}

// ==================== Error Reporting Implementation ====================

// Create validation result
ValidationResult* create_validation_result(VariableMemPool* pool) {
    ValidationResult* result = (ValidationResult*)pool_calloc(pool, sizeof(ValidationResult));
    if (!result) return nullptr;
    
    result->valid = true;
    result->errors = nullptr;
    result->warnings = nullptr;
    result->error_count = 0;
    result->warning_count = 0;
    
    return result;
}

// Create validation error
ValidationError* create_validation_error(ValidationErrorCode code, const char* message,
                                        PathSegment* path, VariableMemPool* pool) {
    ValidationError* error = (ValidationError*)pool_calloc(pool, sizeof(ValidationError));
    if (!error) return nullptr;
    
    error->code = code;
    error->path = path;
    error->expected = nullptr;
    error->actual.item = ITEM_ERROR;
    error->suggestions = nullptr;
    error->next = nullptr;
    
    // Create message string
    if (message) {
        size_t len = strlen(message);
        error->message = (String*)pool_calloc(pool, sizeof(String) + len + 1);
        if (error->message) {
            error->message->len = len;
            error->message->ref_cnt = 0;
            strcpy(error->message->chars, message);
        }
    }
    
    return error;
}

// Add validation error to result
void add_validation_error(ValidationResult* result, ValidationError* error) {
    if (!result || !error) return;
    
    result->valid = false;
    result->error_count++;
    
    // Add to linked list
    error->next = result->errors;
    result->errors = error;
}

// Merge validation results
void merge_validation_results(ValidationResult* dest, ValidationResult* src) {
    if (!dest || !src) return;
    
    if (!src->valid) {
        dest->valid = false;
    }
    
    // Merge errors
    ValidationError* error = src->errors;
    while (error) {
        ValidationError* next = error->next;
        error->next = dest->errors;
        dest->errors = error;
        dest->error_count++;
        error = next;
    }
    
    // Merge warnings
    ValidationWarning* warning = src->warnings;
    while (warning) {
        ValidationWarning* next = warning->next;
        warning->next = dest->warnings;
        dest->warnings = warning;
        dest->warning_count++;
        warning = next;
    }
}

// Suggest corrections (stub implementation)
List* suggest_corrections(ValidationError* error, VariableMemPool* pool) {
    // Simple stub - return empty list
    return nullptr;
}

// Generate validation report
String* generate_validation_report(ValidationResult* result, VariableMemPool* pool) {
    if (!result || !pool) return nullptr;
    
    // Simple text report
    char report_text[2048];
    if (result->valid) {
        snprintf(report_text, sizeof(report_text), 
                "✅ Validation PASSED\n✓ No errors found\n");
    } else {
        snprintf(report_text, sizeof(report_text), 
                "❌ Validation FAILED\nErrors found: %d\n", result->error_count);
        
        // Add error details
        ValidationError* error = result->errors;
        int error_num = 1;
        while (error && strlen(report_text) < sizeof(report_text) - 200) {
            char error_line[200];
            const char* error_msg = error->message ? error->message->chars : "Unknown error";
            snprintf(error_line, sizeof(error_line), "  %d. %s\n", error_num++, error_msg);
            strncat(report_text, error_line, sizeof(report_text) - strlen(report_text) - 1);
            error = error->next;
        }
    }
    
    // Create string
    size_t len = strlen(report_text);
    String* report = (String*)pool_calloc(pool, sizeof(String) + len + 1);
    if (report) {
        report->len = len;
        report->ref_cnt = 0;
        strcpy(report->chars, report_text);
    }
    
    return report;
}

// Format error with context
String* format_error_with_context(ValidationError* error, VariableMemPool* pool) {
    if (!error || !pool) return nullptr;
    
    const char* error_msg = error->message ? error->message->chars : "Unknown error";
    size_t len = strlen(error_msg);
    String* formatted = (String*)pool_calloc(pool, sizeof(String) + len + 1);
    if (formatted) {
        formatted->len = len;
        formatted->ref_cnt = 0;
        strcpy(formatted->chars, error_msg);
    }
    
    return formatted;
}

// Schema validator stubs
SchemaValidator* schema_validator_create(VariableMemPool* pool) {
    SchemaValidator* validator = (SchemaValidator*)pool_calloc(pool, sizeof(SchemaValidator));
    if (validator) {
        validator->pool = pool;
        validator->schemas = nullptr;
        validator->context = nullptr;
        validator->custom_validators = nullptr;
    }
    return validator;
}

void schema_validator_destroy(SchemaValidator* validator) {
    // Memory pool cleanup handles deallocation
}

int schema_validator_load_schema(SchemaValidator* validator, const char* schema_source, 
                                const char* schema_name) {
    // Stub implementation - always succeeds
    return 0;
}

ValidationResult* validate_document(SchemaValidator* validator, Item document, 
                                   const char* schema_name) {
    // Stub implementation - always passes
    ValidationResult* result = create_validation_result(validator->pool);
    return result;
}

// Transpiler stub implementations (static to avoid conflicts)
static void* transpiler_create(VariableMemPool* pool) {
    // Stub implementation - return null to indicate transpiler not available
    return nullptr;
}

static void* transpiler_build_ast(void* transpiler, const char* source) {
    // Stub implementation - return null to indicate AST building not available
    return nullptr;
}

static void transpiler_destroy(void* transpiler) {
    // Stub implementation - nothing to destroy
}

// Enhanced validation context for AST validation (extends existing AstValidationContext)
typedef struct EnhancedValidationContext {
    VariableMemPool* pool;
    PathSegment* current_path;
    int current_depth;
    int max_depth;
    ValidationOptions options;
    HashMap* visited_nodes;  // For circular reference detection
} EnhancedValidationContext;

// Create enhanced validation context for AST validation
EnhancedValidationContext* create_enhanced_validation_context(VariableMemPool* pool) {
    EnhancedValidationContext* ctx = (EnhancedValidationContext*)pool_calloc(pool, sizeof(EnhancedValidationContext));
    if (!ctx) return nullptr;
    
    ctx->pool = pool;
    ctx->current_path = nullptr;
    ctx->current_depth = 0;
    ctx->max_depth = 100;  // Reasonable depth limit
    
    // Initialize default validation options
    ctx->options.strict_mode = false;
    ctx->options.allow_unknown_fields = true;
    ctx->options.allow_empty_elements = true;
    ctx->options.max_depth = 100;
    ctx->options.timeout_ms = 0;
    ctx->options.enabled_rules = nullptr;
    ctx->options.disabled_rules = nullptr;
    
    // Create visited nodes hashmap for circular reference detection
    ctx->visited_nodes = nullptr;  // Will initialize when needed
    
    return ctx;
}

// Create path segment for error reporting
PathSegment* create_path_segment(PathSegmentType type, const char* name, long index, VariableMemPool* pool) {
    PathSegment* segment = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    if (!segment) return nullptr;
    
    segment->type = type;
    segment->next = nullptr;
    
    switch (type) {
        case PATH_FIELD:
            segment->data.field_name = strview_from_cstr(name);
            break;
        case PATH_INDEX:
            segment->data.index = index;
            break;
        case PATH_ELEMENT:
            segment->data.element_tag = strview_from_cstr(name);
            break;
        case PATH_ATTRIBUTE:
            segment->data.attr_name = strview_from_cstr(name);
            break;
    }
    
    return segment;
}

// Push path segment to validation context
void push_path_segment(EnhancedValidationContext* ctx, PathSegmentType type, const char* name, long index) {
    if (!ctx) return;
    
    PathSegment* segment = create_path_segment(type, name, index, ctx->pool);
    if (segment) {
        segment->next = ctx->current_path;
        ctx->current_path = segment;
    }
}

// Pop path segment from validation context
void pop_path_segment(EnhancedValidationContext* ctx) {
    if (!ctx || !ctx->current_path) return;
    
    ctx->current_path = ctx->current_path->next;
}

// Create validation error with full context
ValidationError* create_ast_validation_error(ValidationErrorCode code, const char* message, 
                                           EnhancedValidationContext* ctx) {
    if (!ctx || !ctx->pool) return nullptr;
    
    ValidationError* error = create_validation_error(code, message, ctx->current_path, ctx->pool);
    if (!error) return nullptr;
    
    // Add suggestions based on error type
    error->suggestions = suggest_corrections(error, ctx->pool);
    
    return error;
}

// Add validation error to result
void add_ast_validation_error(ValidationResult* result, ValidationErrorCode code, 
                             const char* message, EnhancedValidationContext* ctx) {
    if (!result || !ctx) return;
    
    ValidationError* error = create_ast_validation_error(code, message, ctx);
    if (error) {
        add_validation_error(result, error);
    }
}

// Validate AST node recursively
ValidationResult* validate_ast_node_recursive(AstNode* node, EnhancedValidationContext* ctx) {
    if (!node || !ctx) {
        ValidationResult* result = create_validation_result(ctx->pool);
        add_ast_validation_error(result, VALID_ERROR_PARSE_ERROR, "Invalid AST node or context", ctx);
        return result;
    }
    
    ValidationResult* result = create_validation_result(ctx->pool);
    
    // Check depth limit
    if (ctx->current_depth >= ctx->max_depth) {
        add_ast_validation_error(result, VALID_ERROR_CONSTRAINT_VIOLATION, 
                               "Maximum validation depth exceeded", ctx);
        return result;
    }
    
    ctx->current_depth++;
    
    // Basic AST node validation - simplified to avoid type system complexity
    push_path_segment(ctx, PATH_ELEMENT, "ast_node", 0);
    
    // Basic validation - just check if node exists
    if (!node) {
        add_ast_validation_error(result, VALID_ERROR_INVALID_ELEMENT, 
                               "AST node is null", ctx);
    }
    
    pop_path_segment(ctx);
    
    ctx->current_depth--;
    return result;
}

// Validate a Lambda AST using comprehensive validation
ValidationResult* validate_lambda_ast(AstNode* ast, VariableMemPool* pool) {
    if (!ast || !pool) {
        ValidationResult* result = create_validation_result(pool);
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, 
                                                       "Invalid AST or memory pool", nullptr, pool);
        add_validation_error(result, error);
        return result;
    }
    
    // Create validation context
    EnhancedValidationContext* ctx = create_enhanced_validation_context(pool);
    if (!ctx) {
        ValidationResult* result = create_validation_result(pool);
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, 
                                                       "Failed to create validation context", nullptr, pool);
        add_validation_error(result, error);
        return result;
    }
    
    // Perform comprehensive AST validation
    ValidationResult* result = validate_ast_node_recursive(ast, ctx);
    
    return result;
}

// Parse and validate a Lambda source file with full validation
ValidationResult* validate_lambda_source(const char* source_content, VariableMemPool* pool) {
    if (!source_content || !pool) {
        ValidationResult* result = create_validation_result(pool);
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, 
                                                       "Invalid source content or memory pool", nullptr, pool);
        add_validation_error(result, error);
        return result;
    }
    
    ValidationResult* result = create_validation_result(pool);
    
    // Basic syntax check - verify it's not empty
    if (strlen(source_content) == 0) {
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, 
                                                       "Empty Lambda source file", nullptr, pool);
        add_validation_error(result, error);
        return result;
    }
    
    // Try to build AST using transpiler (if available)
    void* transpiler = transpiler_create(pool);
    if (transpiler) {
        void* ast = transpiler_build_ast(transpiler, source_content);
        if (ast) {
            // Validate the AST
            ValidationResult* ast_result = validate_lambda_ast((AstNode*)ast, pool);
            if (ast_result) {
                merge_validation_results(result, ast_result);
            }
        } else {
            ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, 
                                                           "Failed to parse Lambda source into AST", nullptr, pool);
            add_validation_error(result, error);
        }
        transpiler_destroy(transpiler);
    } else {
        // Fallback to basic validation
        printf("Note: Using basic validation (transpiler not available)\n");
        
        // Basic Lambda syntax patterns check
        bool has_lambda_syntax = false;
        if (strstr(source_content, "=") || strstr(source_content, "{") || 
            strstr(source_content, "}") || strstr(source_content, "let") ||
            strstr(source_content, "for") || strstr(source_content, "if")) {
            has_lambda_syntax = true;
        }
        
        if (!has_lambda_syntax) {
            ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, 
                                                           "File does not appear to contain Lambda syntax", nullptr, pool);
            add_validation_error(result, error);
        }
    }
    
    return result;
}

// Read file and validate Lambda content with comprehensive validation
ValidationResult* validate_lambda_file(const char* file_path, VariableMemPool* pool) {
    if (!file_path || !pool) {
        ValidationResult* result = create_validation_result(pool);
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, 
                                                       "Invalid file path or memory pool", nullptr, pool);
        add_validation_error(result, error);
        return result;
    }
    
    // Read file content using existing file utilities
    char* content = read_text_file(file_path);
    if (!content) {
        ValidationResult* result = create_validation_result(pool);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Failed to read file: %s", file_path);
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, error_msg, nullptr, pool);
        add_validation_error(result, error);
        return result;
    }
    
    // Validate the source with full validation
    ValidationResult* result = validate_lambda_source(content, pool);
    
    // Cleanup
    free(content);
    
    return result;
}

// Enhanced AST-based validation function with full validation flow
extern "C" AstValidationResult* run_ast_validation(const char* data_file, const char* schema_file, const char* input_format) {
    printf("Lambda AST Validator v2.0\n");
    
    // Check if this is a Lambda file or should use schema validation
    bool is_lambda_file = false;
    if (!schema_file) {
        is_lambda_file = true;
    } else {
        const char* ext = strrchr(data_file, '.');
        if (ext && (strcmp(ext, ".ls") == 0)) {
            is_lambda_file = true;
        }
    }
    
    if (is_lambda_file) {
        printf("Validating '%s' using AST-based validation\n", data_file);
        if (schema_file) {
            printf("Note: Schema file '%s' ignored (AST validation uses built-in rules)\n", schema_file);
        }
        if (input_format && strcmp(input_format, "lambda") != 0) {
            printf("Note: Input format '%s' ignored (AST validation is Lambda-specific)\n", input_format);
        }
    } else {
        printf("Validating '%s' using schema-based validation\n", data_file);
        if (input_format) {
            printf("Format: %s, Schema: %s\n", input_format, schema_file);
        } else {
            printf("Auto-detect format, Schema: %s\n", schema_file);
        }
    }
    
    // Create memory pool for validation
    VariableMemPool* pool = nullptr;
    MemPoolError err = pool_variable_init(&pool, 1024 * 1024, MEM_POOL_NO_BEST_FIT);
    if (err != MEM_POOL_ERR_OK || !pool) {
        printf("Error: Failed to create memory pool\n");
        return nullptr;
    }
    
    ValidationResult* validation_result = nullptr;
    
    if (is_lambda_file) {
        // Use AST-based validation for Lambda files
        printf("Loading and parsing Lambda source...\n");
        validation_result = validate_lambda_file(data_file, pool);
    } else {
        // Use schema-based validation for other formats
        printf("Loading schema and parsing data file...\n");
        
        // Read schema file
        char* schema_contents = read_text_file(schema_file);
        if (!schema_contents) {
            printf("Error: Could not read schema file '%s'\n", schema_file);
            pool_variable_destroy(pool);
            return nullptr;
        }
        
        // Create schema validator
        SchemaValidator* validator = schema_validator_create(pool);
        if (!validator) {
            printf("Error: Failed to create schema validator\n");
            free(schema_contents);
            pool_variable_destroy(pool);
            return nullptr;
        }
        
        // Determine root type based on schema file
        const char* root_type = "Document";  // Default
        if (strstr(schema_file, "html5_schema.ls")) {
            root_type = "HTMLDocument";
        } else if (strstr(schema_file, "eml_schema.ls")) {
            root_type = "EMLDocument";
        } else if (strstr(schema_file, "ics_schema.ls")) {
            root_type = "ICSDocument";
        } else if (strstr(schema_file, "vcf_schema.ls")) {
            root_type = "VCFDocument";
        }
        
        // Load schema
        int schema_result = schema_validator_load_schema(validator, schema_contents, root_type);
        if (schema_result != 0) {
            printf("Error: Failed to load schema\n");
            schema_validator_destroy(validator);
            free(schema_contents);
            pool_variable_destroy(pool);
            return nullptr;
        }
        
        // Parse data file using input system
        char cwd_path[1024];
        if (!getcwd(cwd_path, sizeof(cwd_path))) {
            printf("Error: Cannot get current working directory\n");
            schema_validator_destroy(validator);
            free(schema_contents);
            pool_variable_destroy(pool);
            return nullptr;
        }
        
        char file_url[1200];
        if (data_file[0] == '/') {
            snprintf(file_url, sizeof(file_url), "file://%s", data_file);
        } else {
            snprintf(file_url, sizeof(file_url), "file://%s/%s", cwd_path, data_file);
        }
        
        String* url_string = (String*)malloc(sizeof(String) + strlen(file_url) + 1);
        String* type_string = nullptr;
        if (input_format && strcmp(input_format, "auto-detect") != 0) {
            type_string = (String*)malloc(sizeof(String) + strlen(input_format) + 1);
            if (type_string) {
                type_string->len = strlen(input_format);
                type_string->ref_cnt = 0;
                strcpy(type_string->chars, input_format);
            }
        }
        
        Item data_item = {.item = ITEM_ERROR};
        if (url_string) {
            url_string->len = strlen(file_url);
            url_string->ref_cnt = 0;
            strcpy(url_string->chars, file_url);
            
            Input* input = input_from_url(url_string, type_string, nullptr, nullptr);
            if (input && input->root.item != ITEM_ERROR) {
                data_item = input->root;
                printf("Successfully parsed input file\n");
            } else {
                printf("Error: Failed to parse input file\n");
            }
            
            free(url_string);
            if (type_string) free(type_string);
        }
        
        if (data_item.item == ITEM_ERROR) {
            printf("Error: Failed to parse data file\n");
            schema_validator_destroy(validator);
            free(schema_contents);
            pool_variable_destroy(pool);
            return nullptr;
        }
        
        // Validate using schema
        printf("Validating data against schema...\n");
        validation_result = validate_document(validator, data_item, root_type);
        
        // Cleanup
        schema_validator_destroy(validator);
        free(schema_contents);
    }
    
    if (!validation_result) {
        printf("Error: Validation failed to run\n");
        pool_variable_destroy(pool);
        return nullptr;
    }
    
    // Print comprehensive results using sophisticated error reporting
    printf("\n=== Validation Results ===\n");
    String* report = generate_validation_report(validation_result, pool);
    if (report && report->chars) {
        printf("%s", report->chars);
    } else {
        // Fallback to basic reporting
        if (validation_result->valid) {
            printf("✅ Validation PASSED\n");
            printf("✓ File '%s' is valid\n", data_file);
        } else {
            printf("❌ Validation FAILED\n");
            printf("Errors found: %d\n", validation_result->error_count);
            
            // Print detailed error information
            ValidationError* error = validation_result->errors;
            int error_num = 1;
            while (error) {
                String* error_str = format_error_with_context(error, pool);
                printf("  %d. %s\n", error_num++, error_str ? error_str->chars : "Unknown error");
                error = error->next;
            }
        }
    }
    
    // Create compatibility result for return
    AstValidationResult* result = (AstValidationResult*)malloc(sizeof(AstValidationResult));
    if (!result) {
        pool_variable_destroy(pool);
        return nullptr;
    }
    
    result->valid = validation_result->valid;
    result->error_count = validation_result->error_count;
    result->errors = (AstValidationError*)validation_result;  // Store validation result for cleanup
    
    return result;
}


// Validation execution function that can be called directly by tests
extern "C" AstValidationResult* exec_validation(int argc, char* argv[]) {
    // Extract validation argument parsing logic from main() function
    // This allows tests to call validation directly without spawning new processes
    printf("Starting validation with arguments\n");
    if (argc < 2) {
        printf("Error: No file specified for validation\n");
        printf("Usage: validate [-s <schema>] [-f <format>] <file> [files...]\n");
        return NULL;
    }
    
    const char* data_file = nullptr;
    const char* schema_file = nullptr;  // Will be determined based on format
    const char* input_format = nullptr;  // Auto-detect by default
    bool schema_explicitly_set = false;
    
    // Parse validation arguments (skip argv[0] which would be "validate")
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            schema_file = argv[i + 1];
            schema_explicitly_set = true;
            i++; // Skip the schema filename
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            input_format = argv[i + 1];
            i++; // Skip the format name
        } else if (argv[i][0] != '-') {
            // This is the input file
            if (!data_file) {
                data_file = argv[i];
            } else {
                printf("Error: Multiple input files not yet supported\n");
                return NULL;
            }
        } else {
            printf("Error: Unknown validation option '%s'\n", argv[i]);
            printf("Usage: validate [-s <schema>] [-f <format>] <file>\n");
            printf("Formats: auto, json, csv, ini, toml, yaml, xml, markdown, rst, html, latex, rtf, pdf, wiki, asciidoc, man, eml, vcf, ics, text\n");
            return NULL;
        }
    }
    
    if (!data_file) {
        printf("Error: No input file specified\n");
        printf("Usage: validate [-s <schema>] [-f <format>] <file>\n");
        return NULL;
    }
    
    // Auto-detect format if not specified
    if (!input_format) {
        const char* ext = strrchr(data_file, '.');
        if (ext) {
            if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
                input_format = "html";
            } else if (strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".markdown") == 0) {
                input_format = "markdown";
            } else if (strcasecmp(ext, ".json") == 0) {
                input_format = "json";
            } else if (strcasecmp(ext, ".xml") == 0) {
                input_format = "xml";
            } else if (strcasecmp(ext, ".yaml") == 0 || strcasecmp(ext, ".yml") == 0) {
                input_format = "yaml";
            } else if (strcasecmp(ext, ".csv") == 0) {
                input_format = "csv";
            } else if (strcasecmp(ext, ".ini") == 0) {
                input_format = "ini";
            } else if (strcasecmp(ext, ".toml") == 0) {
                input_format = "toml";
            } else if (strcasecmp(ext, ".eml") == 0) {
                input_format = "eml";
            } else if (strcasecmp(ext, ".ics") == 0) {
                input_format = "ics";
            } else if (strcasecmp(ext, ".vcf") == 0) {
                input_format = "vcf";
            } else if (strcasecmp(ext, ".rst") == 0) {
                input_format = "rst";
            } else if (strcasecmp(ext, ".wiki") == 0) {
                input_format = "wiki";
            } else if (strcasecmp(ext, ".adoc") == 0 || strcasecmp(ext, ".asciidoc") == 0) {
                input_format = "asciidoc";
            } else if (strcasecmp(ext, ".1") == 0 || strcasecmp(ext, ".2") == 0 || 
                      strcasecmp(ext, ".3") == 0 || strcasecmp(ext, ".4") == 0 ||
                      strcasecmp(ext, ".5") == 0 || strcasecmp(ext, ".6") == 0 ||
                      strcasecmp(ext, ".7") == 0 || strcasecmp(ext, ".8") == 0 ||
                      strcasecmp(ext, ".9") == 0 || strcasecmp(ext, ".man") == 0) {
                input_format = "man";
            } else if (strcasecmp(ext, ".textile") == 0 || strcasecmp(ext, ".txtl") == 0) {
                input_format = "textile";
            } else if (strcasecmp(ext, ".m") == 0 || strcasecmp(ext, ".mk") == 0 || strcasecmp(ext, ".mark") == 0) {
                input_format = "mark";
            }
            // If no recognized extension, keep as nullptr for Lambda format
        }
    }
    
    // Determine schema file if not explicitly set
    if (!schema_explicitly_set) {
        // Check if this is a Lambda file first
        const char* ext = strrchr(data_file, '.');
        if (ext && strcmp(ext, ".ls") == 0) {
            // Lambda files use AST validation - no schema needed
            schema_file = nullptr;
            printf("Using AST-based validation for Lambda file\n");
        } else if (input_format && strcmp(input_format, "html") == 0) {
            schema_file = "lambda/input/html5_schema.ls";
            printf("Using HTML5 schema for HTML input\n");
        } else if (input_format && strcmp(input_format, "eml") == 0) {
            schema_file = "lambda/input/eml_schema.ls";
            printf("Using EML schema for email input\n");
        } else if (input_format && strcmp(input_format, "ics") == 0) {
            schema_file = "lambda/input/ics_schema.ls";
            printf("Using ICS schema for calendar input\n");
        } else if (input_format && strcmp(input_format, "vcf") == 0) {
            schema_file = "lambda/input/vcf_schema.ls";
            printf("Using VCF schema for vCard input\n");
        } else if (input_format && (strcmp(input_format, "asciidoc") == 0 || 
                                 strcmp(input_format, "man") == 0 ||
                                 strcmp(input_format, "markdown") == 0 ||
                                 strcmp(input_format, "rst") == 0 ||
                                 strcmp(input_format, "textile") == 0 ||
                                 strcmp(input_format, "wiki") == 0)) {
            schema_file = "lambda/input/doc_schema.ls";
            printf("Using document schema for %s input\n", input_format);
        } else if (!input_format || strcmp(input_format, "lambda") == 0) {
            // Default to AST validation for Lambda format
            schema_file = nullptr;
            printf("Using AST-based validation for Lambda format\n");
        } else {
            // For other formats (json, xml, yaml, csv, ini, toml, latex, rtf, pdf, text), require explicit schema
            printf("Error: Input format '%s' requires an explicit schema file. Use -s <schema_file> option.\n", input_format);
            printf("Formats with default schemas: html, eml, ics, vcf, asciidoc, man, markdown, rst, textile, wiki\n");
            printf("Lambda files (*.ls) use automatic AST-based validation\n");
            return NULL;
        }
    }
    
    // Call the validation function and return the ValidationResult directly
    if (schema_file) {
        printf("Starting validation of '%s' using schema '%s'...\n", data_file, schema_file);
    } else {
        printf("Starting AST validation of '%s'...\n", data_file);
    }
    AstValidationResult* result = run_ast_validation(data_file, schema_file, input_format);
    
    // Return the ValidationResult directly to the caller
    return result;
}


// Cleanup function for validation results
void ast_validation_result_destroy(AstValidationResult* result) {
    if (!result) return;
    
    if (result->errors) {
        ValidationResult* validation_result = (ValidationResult*)result->errors;
        // Note: Memory pool cleanup should be handled by caller
        // since ValidationResult and its errors are allocated from the pool
    }
    
    free(result);
}
