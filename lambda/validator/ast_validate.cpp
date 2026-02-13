/**
 * @file ast_validate.cpp
 * @brief AST-based validation implementation for Lambda validate subcommand
 * @author Henry Luo
 * @license MIT
 */

#include "validator.hpp"
#include "../transpiler.hpp"
#include "../ast.hpp"
#include "../lambda-data.hpp"
#include "../input/input.hpp"
#include "../../lib/mempool.h"
#include "../../lib/log.h"
#include "../../lib/file.h"
#include "../../lib/str.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

// Forward declaration for suggest_corrections function
List* suggest_corrections(ValidationError* error, Pool* pool);

// Forward declarations for real transpiler functions from validator.cpp
extern Transpiler* transpiler_create(Pool* pool);
extern void transpiler_destroy(Transpiler* transpiler);
extern AstNode* transpiler_build_ast(Transpiler* transpiler, const char* source);

// ==================== Validation Error System ====================
// Note: ValidationResult and related structures now defined in validator.hpp

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
// Note: Error reporting functions now implemented in error_reporting.cpp
// Note: SchemaValidator C wrapper functions (schema_validator_*) are now in doc_validator.cpp

// Document validation function - uses the new SchemaValidator API
ValidationResult* validate_document(SchemaValidator* validator, Item document,
                                   const char* schema_name) {
    if (!validator || document.item == ITEM_NULL || !schema_name) {
        ValidationResult* result = create_validation_result(validator->get_pool());
        ValidationError* error = create_validation_error(
            VALID_ERROR_PARSE_ERROR,
            "Invalid validation parameters",
            nullptr,
            validator->get_pool()
        );
        add_validation_error(result, error);
        return result;
    }

    // Use the SchemaValidator to validate the document
    Item mutable_item = document;
    ConstItem const_doc = *(ConstItem*)&mutable_item;
    ValidationResult* result = validator->validate(const_doc, schema_name);

    return result;
}

// =============================================================================
// Lambda Source File Validation (AST-based)
// =============================================================================
//
// This section handles validation of Lambda source files (.ls).
// Currently provides basic parse-level validation - checks that the file can
// be parsed into a valid AST. More sophisticated semantic validation could be
// added in the future.
// =============================================================================

// Parse and validate a Lambda source file
// Returns a ValidationResult with any parse errors
static ValidationResult* validate_lambda_source(const char* source_content, Pool* pool) {
    if (!source_content || !pool) {
        ValidationResult* result = create_validation_result(pool);
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR,
                                                       "Invalid source content or memory pool", nullptr, pool);
        add_validation_error(result, error);
        return result;
    }

    ValidationResult* result = create_validation_result(pool);

    // Check for empty source
    if (strlen(source_content) == 0) {
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR,
                                                       "Empty Lambda source file", nullptr, pool);
        add_validation_error(result, error);
        return result;
    }

    // Build AST using transpiler - this validates syntax
    Transpiler* transpiler = transpiler_create(pool);
    if (transpiler) {
        AstNode* ast = transpiler_build_ast(transpiler, source_content);
        if (!ast) {
            ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR,
                                                           "Failed to parse Lambda source into AST", nullptr, pool);
            add_validation_error(result, error);
        }
        // If AST was successfully built, the source is syntactically valid
        // Future: could add semantic validation of the AST here
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

// Read file and validate Lambda content
static ValidationResult* validate_lambda_file(const char* file_path, Pool* pool) {
    if (!file_path || !pool) {
        ValidationResult* result = create_validation_result(pool);
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR,
                                                       "Invalid file path or memory pool", nullptr, pool);
        add_validation_error(result, error);
        return result;
    }

    // Read file content
    char* content = read_text_file(file_path);
    if (!content) {
        ValidationResult* result = create_validation_result(pool);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Failed to read file: %s", file_path);
        ValidationError* error = create_validation_error(VALID_ERROR_PARSE_ERROR, error_msg, nullptr, pool);
        add_validation_error(result, error);
        return result;
    }

    // Validate the source
    ValidationResult* result = validate_lambda_source(content, pool);

    free(content);
    return result;
}

// =============================================================================
// Schema-based Validation (run_ast_validation)
// =============================================================================

// Enhanced AST-based validation function with full validation flow
ValidationResult* run_ast_validation(const char* data_file, const char* schema_file,
                                    const char* input_format, ValidationOptions* options) {
    printf("Lambda AST Validator v2.0\n");;

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

    // Print validation options if provided
    if (options) {
        printf("Validation options:\n");
        printf("  - Strict mode: %s\n", options->strict_mode ? "enabled" : "disabled");
        printf("  - Max errors: %d\n", options->max_errors);
        printf("  - Max depth: %d\n", options->max_depth);
        printf("  - Allow unknown fields: %s\n", options->allow_unknown_fields ? "yes" : "no");
    }

    // Create memory pool for validation
    Pool* pool = pool_create();
    if (!pool) {
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
            pool_destroy(pool);
            return nullptr;
        }

        // Create schema validator
        SchemaValidator* validator = schema_validator_create(pool);
        if (!validator) {
            printf("Error: Failed to create schema validator\n");
            free(schema_contents);
            pool_destroy(pool);
            return nullptr;
        }

        // Determine root type based on schema file
        const char* root_type = nullptr;
        if (strstr(schema_file, "html5_schema.ls")) {
            root_type = "HTMLDocument";
        } else if (strstr(schema_file, "eml_schema.ls")) {
            root_type = "EMLDocument";
        } else if (strstr(schema_file, "ics_schema.ls")) {
            root_type = "ICSDocument";
        } else if (strstr(schema_file, "vcf_schema.ls")) {
            root_type = "VCFDocument";
        }

        // For custom schemas, try to extract the root type name from the source
        // Strategy: Look for a type named "Document" first, otherwise use the last defined type
        if (!root_type) {
            // First, try to find a type named "Document"
            const char* doc_search = schema_contents;
            while ((doc_search = strstr(doc_search, "type ")) != nullptr) {
                doc_search += 5; // Skip "type "
                // Skip whitespace
                while (*doc_search == ' ' || *doc_search == '\t') {
                    doc_search++;
                }
                // Check if this is "Document"
                if (strncmp(doc_search, "Document", 8) == 0 &&
                    (doc_search[8] == ' ' || doc_search[8] == '=' || doc_search[8] == '\t')) {
                    root_type = "Document";
                    log_info("Found Document type in schema, using as root");
                    break;
                }
                doc_search++;
            }

            // If no "Document" type found, extract the LAST type definition
            // (typically the root/aggregating type is defined last)
            if (!root_type) {
                const char* last_type_start = nullptr;
                const char* search_pos = schema_contents;

                while ((search_pos = strstr(search_pos, "type ")) != nullptr) {
                    last_type_start = search_pos + 5; // Skip "type "
                    search_pos += 5;
                }

                if (last_type_start) {
                    // Skip whitespace
                    while (*last_type_start == ' ' || *last_type_start == '\t' || *last_type_start == '\n') {
                        last_type_start++;
                    }
                    // Extract type name (until whitespace or '=')
                    const char* name_end = last_type_start;
                    while (*name_end && *name_end != ' ' && *name_end != '\t' &&
                           *name_end != '\n' && *name_end != '=') {
                        name_end++;
                    }
                    if (name_end > last_type_start) {
                        // Copy the type name
                        size_t name_len = name_end - last_type_start;
                        char* extracted_name = (char*)pool_calloc(pool, name_len + 1);
                        if (extracted_name) {
                            memcpy(extracted_name, last_type_start, name_len);
                            extracted_name[name_len] = '\0';
                            root_type = extracted_name;
                            log_info("Using last type definition as root: %s", root_type);
                        }
                    }
                }
            }
        }

        // If still no root type, use default
        if (!root_type) {
            root_type = "Document";
        }

        // Load schema (this will extract all type definitions)
        int schema_result = schema_validator_load_schema(validator, schema_contents, root_type);
        if (schema_result != 0) {
            printf("Error: Failed to load schema\n");
            schema_validator_destroy(validator);
            free(schema_contents);
            pool_destroy(pool);
            return nullptr;
        }

        // Parse data file using input system
        char cwd_path[1024];
        if (!getcwd(cwd_path, sizeof(cwd_path))) {
            printf("Error: Cannot get current working directory\n");
            schema_validator_destroy(validator);
            free(schema_contents);
            pool_destroy(pool);
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
                str_copy(type_string->chars, type_string->len + 1, input_format, type_string->len);
            }
        }

        Item data_item = {.item = ITEM_ERROR};
        if (url_string) {
            url_string->len = strlen(file_url);
            url_string->ref_cnt = 0;
            str_copy(url_string->chars, url_string->len + 1, file_url, url_string->len);

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
            pool_destroy(pool);
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
        pool_destroy(pool);
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

    // Return the validation result directly
    return validation_result;
}


// Validation execution function that can be called directly by tests
ValidationResult* exec_validation(int argc, char* argv[]) {
    // Extract validation argument parsing logic from main() function
    // This allows tests to call validation directly without spawning new processes
    printf("Starting validation with arguments\n");
    if (argc < 2) {
        printf("Error: No file specified for validation\n");
        printf("Usage: validate [-s <schema>] [-f <format>] [--strict] [--max-errors N] [--max-depth N] [--allow-unknown] <file> [files...]\n");
        return NULL;
    }

    const char* data_file = nullptr;
    const char* schema_file = nullptr;  // Will be determined based on format
    const char* input_format = nullptr;  // Auto-detect by default
    bool schema_explicitly_set = false;

    // Validation options
    bool strict_mode = false;
    int max_errors = 100;
    int max_depth = 100;
    bool allow_unknown = false;

    // Parse validation arguments (skip argv[0] which would be "validate")
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            schema_file = argv[i + 1];
            schema_explicitly_set = true;
            i++; // Skip the schema filename
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            input_format = argv[i + 1];
            i++; // Skip the format name
        } else if (strcmp(argv[i], "--strict") == 0) {
            strict_mode = true;
        } else if (strcmp(argv[i], "--max-errors") == 0 && i + 1 < argc) {
            max_errors = (int)str_to_int64_default(argv[i + 1], strlen(argv[i + 1]), 0);
            if (max_errors <= 0) max_errors = 100;
            i++;
        } else if (strcmp(argv[i], "--max-depth") == 0 && i + 1 < argc) {
            max_depth = (int)str_to_int64_default(argv[i + 1], strlen(argv[i + 1]), 0);
            if (max_depth <= 0) max_depth = 100;
            i++;
        } else if (strcmp(argv[i], "--allow-unknown") == 0) {
            allow_unknown = true;
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
            printf("Usage: validate [-s <schema>] [-f <format>] [--strict] [--max-errors N] [--max-depth N] [--allow-unknown] <file>\n");
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
            size_t ext_len = strlen(ext);
            if (str_ieq_const(ext, ext_len, ".html") || str_ieq_const(ext, ext_len, ".htm")) {
                input_format = "html";
            } else if (str_ieq_const(ext, ext_len, ".md") || str_ieq_const(ext, ext_len, ".markdown")) {
                input_format = "markdown";
            } else if (str_ieq_const(ext, ext_len, ".json")) {
                input_format = "json";
            } else if (str_ieq_const(ext, ext_len, ".xml")) {
                input_format = "xml";
            } else if (str_ieq_const(ext, ext_len, ".yaml") || str_ieq_const(ext, ext_len, ".yml")) {
                input_format = "yaml";
            } else if (str_ieq_const(ext, ext_len, ".csv")) {
                input_format = "csv";
            } else if (str_ieq_const(ext, ext_len, ".ini")) {
                input_format = "ini";
            } else if (str_ieq_const(ext, ext_len, ".toml")) {
                input_format = "toml";
            } else if (str_ieq_const(ext, ext_len, ".eml")) {
                input_format = "eml";
            } else if (str_ieq_const(ext, ext_len, ".ics")) {
                input_format = "ics";
            } else if (str_ieq_const(ext, ext_len, ".vcf")) {
                input_format = "vcf";
            } else if (str_ieq_const(ext, ext_len, ".rst")) {
                input_format = "rst";
            } else if (str_ieq_const(ext, ext_len, ".wiki")) {
                input_format = "wiki";
            } else if (str_ieq_const(ext, ext_len, ".adoc") || str_ieq_const(ext, ext_len, ".asciidoc")) {
                input_format = "asciidoc";
            } else if (str_ieq_const(ext, ext_len, ".1") || str_ieq_const(ext, ext_len, ".2") ||
                      str_ieq_const(ext, ext_len, ".3") || str_ieq_const(ext, ext_len, ".4") ||
                      str_ieq_const(ext, ext_len, ".5") || str_ieq_const(ext, ext_len, ".6") ||
                      str_ieq_const(ext, ext_len, ".7") || str_ieq_const(ext, ext_len, ".8") ||
                      str_ieq_const(ext, ext_len, ".9") || str_ieq_const(ext, ext_len, ".man")) {
                input_format = "man";
            } else if (str_ieq_const(ext, ext_len, ".textile") || str_ieq_const(ext, ext_len, ".txtl")) {
                input_format = "textile";
            } else if (str_ieq_const(ext, ext_len, ".m") || str_ieq_const(ext, ext_len, ".mk") || str_ieq_const(ext, ext_len, ".mark")) {
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

    // Create validation options from parsed arguments
    ValidationOptions opts;
    opts.strict_mode = strict_mode;
    opts.allow_unknown_fields = allow_unknown;
    opts.allow_empty_elements = true;
    opts.max_depth = max_depth;
    opts.max_errors = max_errors;
    opts.timeout_ms = 0;
    opts.enabled_rules = nullptr;
    opts.disabled_rules = nullptr;

    ValidationResult* result = run_ast_validation(data_file, schema_file, input_format, &opts);

    // Return the ValidationResult directly to the caller
    return result;
}


// Simple wrapper function for tests that need direct validation
extern "C" ValidationResult* run_validation(const char *data_file, const char *schema_file, const char *input_format) {
    if (!data_file) {
        printf("Error: No data file specified\n");
        return nullptr;
    }

    printf("Running validation for %s (schema: %s, format: %s)\n",
           data_file, schema_file ? schema_file : "auto", input_format ? input_format : "auto");

    // Use default validation options
    ValidationOptions opts;
    opts.strict_mode = false;
    opts.allow_unknown_fields = true;
    opts.allow_empty_elements = true;
    opts.max_depth = 100;
    opts.max_errors = 100;
    opts.timeout_ms = 0;
    opts.enabled_rules = nullptr;
    opts.disabled_rules = nullptr;

    return run_ast_validation(data_file, schema_file, input_format, &opts);
}
