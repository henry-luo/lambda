#include "validator.h"
#include "validation_exec.h"
#include <lexbor/url/url.h>
#include <unistd.h>  // for getcwd
#include <cstring>   // for C++ string functions
#include <cstdio>    // for C++ stdio functions
#include <cstdlib>   // for C++ stdlib functions
#include "../../lib/num_stack.h"

extern "C" {
    // Forward declare read_text_file from lib/file.c
    char* read_text_file(const char *filename);

    // Forward declare input_from_url
    Input* input_from_url(String* url, String* type, String* flavor, lxb_url_t* cwd);
}

extern "C" {
void run_validation(const char *data_file, const char *schema_file, const char *input_format) {
    fprintf(stderr, "TRACE: run_validation() started\n");
    fflush(stderr);
    
    printf("Lambda Validator v1.0\n");
    if (input_format) {
        printf("Validating '%s' (format: %s) against schema '%s'\n", data_file, input_format, schema_file);
    } else {
        printf("Validating '%s' (auto-detect format) against schema '%s'\n", data_file, schema_file);
    }
    
    fprintf(stderr, "TRACE: About to initialize runtime\n");
    fflush(stderr);
    
    // Initialize runtime for Lambda script parsing if needed
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = (char*)"./";
    
    fprintf(stderr, "TRACE: Runtime initialized, about to initialize validation context\n");
    fflush(stderr);
    
    // Initialize minimal Lambda context for number stack (needed by map_get when retrieving floats)
    Context validation_context = {0};
    validation_context.num_stack = num_stack_create(16);
    
    fprintf(stderr, "TRACE: About to set global context\n");
    fflush(stderr);
    
    // Set the global context for Lambda evaluation functions
    extern __thread Context* context;
    Context* old_context = context;
    context = &validation_context;
    
    fprintf(stderr, "TRACE: Global context set, about to read schema file: %s\n", schema_file);
    fflush(stderr);
    
    // Track if we created a temp_runner for cleanup
    Runner* temp_runner_ptr = NULL;
    
    // Read schema file using read_text_file from lib/file.c
    char* schema_contents = read_text_file(schema_file);
    if (!schema_contents) {
        fprintf(stderr, "TRACE: Failed to read schema file\n");
        fflush(stderr);
        return;
    }
    
    fprintf(stderr, "TRACE: Schema file read successfully, about to create memory pool\n");
    fflush(stderr);
    
    // Create memory pool for validation
    VariableMemPool* pool = NULL;
    MemPoolError pool_err = pool_variable_init(&pool, 1024 * 1024, 10); // 1MB chunks, 10% tolerance
    if (pool_err != MEM_POOL_ERR_OK || !pool) {
        printf("Error: Cannot create memory pool\n");
        free(schema_contents);
        fprintf(stderr, "TRACE: Memory pool creation failed\n");
        fflush(stderr);
        return;
    }
    
    fprintf(stderr, "TRACE: Memory pool created successfully, about to create validator\n");
    fflush(stderr);
    
    // Create validator
    SchemaValidator* validator = schema_validator_create(pool);
    if (!validator) {
        fprintf(stderr, "TRACE: Failed to create validator\n");
        fflush(stderr);
        printf("Error: Cannot create validator\n");
        pool_variable_destroy(pool);
        free(schema_contents);
        return;
    }
    
    // Load schema
    printf("Loading schema...\n");
    
    // Determine the root type based on schema file
    const char* root_type = "Document";  // Default for doc_schema.ls
    if (strstr(schema_file, "html5_schema.ls")) {
        root_type = "HTMLDocument";  // Use HTMLDocument for HTML5 schema
    } else if (strstr(schema_file, "markdown_schema.ls")) {
        root_type = "any";           // Use any for markdown schema (permissive)
    } else if (strstr(schema_file, "eml_schema.ls")) {
        root_type = "EMLDocument";   // Use EMLDocument for EML schema
    } else if (strstr(schema_file, "ics_schema.ls")) {
        root_type = "ICSDocument";   // Use ICSDocument for ICS schema
    } else if (strstr(schema_file, "vcf_schema.ls")) {
        root_type = "VCFDocument";   // Use VCFDocument for VCF schema
    } else if (strstr(schema_file, "schema_json_user_profile.ls")) {
        root_type = "UserProfileDocument";   // Use UserProfileDocument for JSON user profile schema
    }
    
    // Use the refactored schema parser
    int schema_result = schema_validator_load_schema(validator, schema_contents, root_type);
    if (schema_result != 0) {
        printf("Error: Failed to load schema\n");
        schema_validator_destroy(validator);
        pool_variable_destroy(pool);
        free(schema_contents);
        return;
    }
    
    // Parse data file using input parsing functions
    printf("Parsing data file...\n");
    Item data_item = {.item = ITEM_ERROR};

    // Convert file path to file:// URL
    char cwd_path[1024];
    if (!getcwd(cwd_path, sizeof(cwd_path))) {
        printf("Error: Cannot get current working directory\n");
        schema_validator_destroy(validator);
        pool_variable_destroy(pool);
        free(schema_contents);
        return;
    }

    char file_url[1200];
    if (data_file[0] == '/') {
        // Absolute path
        snprintf(file_url, sizeof(file_url), "file://%s", data_file);
    } else {
        // Relative path
        snprintf(file_url, sizeof(file_url), "file://%s/%s", cwd_path, data_file);
    }

    String* url_string = (String*)malloc(sizeof(String) + strlen(file_url) + 1);
    if (url_string) {
        url_string->len = strlen(file_url);
        url_string->ref_cnt = 0;
        strcpy(url_string->chars, file_url);
        
        String* type_string = NULL;
        if (input_format && strcmp(input_format, "auto-detect") != 0) {
            type_string = (String*)malloc(sizeof(String) + strlen(input_format) + 1);
            if (type_string) {
                type_string->len = strlen(input_format);
                type_string->ref_cnt = 0;
                strcpy(type_string->chars, input_format);
            }
        }
        
        // Use parse_url to create the URL (pass NULL for base to use absolute path)
        Input* input = input_from_url(url_string, type_string, NULL, NULL);
        if (input && input->root.item != ITEM_ERROR) {
            data_item = input->root;
            printf("Successfully parsed input file with format '%s'\n", 
                    input_format ? input_format : "auto-detect");
        } else {
            printf("Error: Failed to parse input file with format '%s'\n", 
                    input_format ? input_format : "auto-detect");
        }
        
        free(url_string);
        if (type_string) free(type_string);
    }
    
    if (data_item.item == ITEM_ERROR) {
        printf("Error: Failed to parse data file\n");
        schema_validator_destroy(validator);
        pool_variable_destroy(pool);
        free(schema_contents);
        return;
    }
    
    // Validate using the loaded schema
    printf("Validating data...\n");
    ValidationResult* result = validate_document(validator, data_item, root_type);
    
    if (!result) {
        printf("Error: Validation failed to run\n");
        schema_validator_destroy(validator);
        pool_variable_destroy(pool);
        free(schema_contents);
        return;
    }
    
    // Print results
    printf("\n=== Validation Results ===\n");
    if (result && result->valid) {
        printf("✅ Validation PASSED\n");
        printf("✓ Data file '%s' is valid according to schema '%s'\n", data_file, schema_file);
    } else {
        printf("❌ Validation FAILED\n");
        if (result) {
            printf("Errors found: %d\n", result->error_count);
            
            // Print error details
            ValidationError* error = result->errors;
            int error_num = 1;
            while (error) {
                const char* error_msg = "Unknown error";
                if (error->message) {
                    error_msg = error->message->chars;
                }
                printf("  Error %d: %s\n", error_num, error_msg);
                if (error->path) {
                    printf("    Path: ");
                    // Build path from root to leaf (reverse the linked list order)
                    PathSegment* segments[50];  // Max 50 levels deep
                    int segment_count = 0;
                    PathSegment* current = error->path;
                    
                    // Collect all segments
                    while (current && segment_count < 50) {
                        segments[segment_count++] = current;
                        current = current->next;
                    }
                    
                    // Print in reverse order (root to leaf)
                    char path_buffer[512] = "";
                    for (int i = segment_count - 1; i >= 0; i--) {
                        PathSegment* segment = segments[i];
                        if (segment->type == PATH_FIELD) {
                            strcat(path_buffer, ".");
                            strncat(path_buffer, segment->data.field_name.str, segment->data.field_name.length);
                        } else if (segment->type == PATH_INDEX) {
                            char index_str[20];
                            snprintf(index_str, sizeof(index_str), "[%ld]", segment->data.index);
                            strcat(path_buffer, index_str);
                        }
                    }
                    
                    if (strlen(path_buffer) > 0) {
                        printf("%s\n", path_buffer);
                    } else {
                        printf("(root)\n");
                    }
                } else {
                    printf("    Path: (root)\n");
                }
                error = error->next;
                error_num++;
            }
        }
    }
    
    // Cleanup
    if (result) {
        // TODO: Implement proper validation result cleanup
        // validation_result_destroy(result);
    }
    schema_validator_destroy(validator);
    pool_variable_destroy(pool);
    free(schema_contents);
    
    // Restore the original context and cleanup number stack
    context = old_context;
    if (validation_context.num_stack) {
        num_stack_destroy((num_stack_t*)validation_context.num_stack);
    }
}

// Validation execution function that can be called directly by tests
int exec_validation(int argc, char* argv[]) {
    // Extract validation argument parsing logic from main() function
    // This allows tests to call validation directly without spawning new processes
    
    if (argc < 2) {
        printf("Error: No file specified for validation\n");
        printf("Usage: validate [-s <schema>] [-f <format>] <file> [files...]\n");
        return 1;
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
                return 1;
            }
        } else {
            printf("Error: Unknown validation option '%s'\n", argv[i]);
            printf("Usage: validate [-s <schema>] [-f <format>] <file>\n");
            printf("Formats: auto, json, csv, ini, toml, yaml, xml, markdown, rst, html, latex, rtf, pdf, wiki, asciidoc, man, eml, vcf, ics, text\n");
            return 1;
        }
    }
    
    if (!data_file) {
        printf("Error: No input file specified\n");
        printf("Usage: validate [-s <schema>] [-f <format>] <file>\n");
        return 1;
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
        if (input_format && strcmp(input_format, "html") == 0) {
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
            schema_file = "lambda/input/doc_schema.ls";  // Default for Lambda format
        } else {
            // For other formats (json, xml, yaml, csv, ini, toml, latex, rtf, pdf, text), require explicit schema
            printf("Error: Input format '%s' requires an explicit schema file. Use -s <schema_file> option.\n", input_format);
            printf("Formats with default schemas: html, eml, ics, vcf, asciidoc, man, markdown, rst, textile, wiki\n");
            return 1;
        }
    }
    
    // Call the validation function
    run_validation(data_file, schema_file, input_format);
    return 0;
}
}
