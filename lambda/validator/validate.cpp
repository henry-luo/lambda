#include "validator.h"
#include <lexbor/url/url.h>
#include <unistd.h>  // for getcwd
#include <cstring>   // for C++ string functions
#include <cstdio>    // for C++ stdio functions
#include <cstdlib>   // for C++ stdlib functions

extern "C" {
    // Forward declare read_text_file from lib/file.c
    char* read_text_file(const char *filename);

    // Forward declare input_from_url
    Input* input_from_url(String* url, String* type, String* flavor, lxb_url_t* cwd);
}

extern "C" {
void run_validation(const char *data_file, const char *schema_file, const char *input_format) {
    printf("Lambda Validator v1.0\n");
    if (input_format) {
        printf("Validating '%s' (format: %s) against schema '%s'\n", data_file, input_format, schema_file);
    } else {
        printf("Validating '%s' (auto-detect format) against schema '%s'\n", data_file, schema_file);
    }
    
    // Initialize runtime for Lambda script parsing if needed
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = (char*)"./";
    
    // Track if we created a temp_runner for cleanup
    Runner* temp_runner_ptr = NULL;
    
    // Read schema file using read_text_file from lib/file.c
    char* schema_contents = read_text_file(schema_file);
    if (!schema_contents) {
        return;
    }
    
    // Create memory pool for validation
    VariableMemPool* pool = NULL;
    MemPoolError pool_err = pool_variable_init(&pool, 1024 * 1024, 10); // 1MB chunks, 10% tolerance
    if (pool_err != MEM_POOL_ERR_OK || !pool) {
        printf("Error: Cannot create memory pool\n");
        free(schema_contents);
        return;
    }
    
    // Create validator
    SchemaValidator* validator = schema_validator_create(pool);
    if (!validator) {
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
    } else if (strstr(schema_file, "eml_schema.ls")) {
        root_type = "EMLDocument";   // Use EMLDocument for EML schema
    } else if (strstr(schema_file, "ics_schema.ls")) {
        root_type = "ICSDocument";   // Use ICSDocument for ICS schema
    } else if (strstr(schema_file, "vcf_schema.ls")) {
        root_type = "VCFDocument";   // Use VCFDocument for VCF schema
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
                    // TODO: Print the validation path
                    printf("(path information)\n");
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
}
}
