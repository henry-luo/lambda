#include "validator.h"
#include <lexbor/url/url.h>
#include <unistd.h>  // for getcwd

// Forward declare read_text_file from lib/file.c
char* read_text_file(const char *filename);

// Forward declare input_from_url
Input* input_from_url(String* url, String* type, lxb_url_t* cwd);

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
            }
            // If no recognized extension, keep as NULL for Lambda format
        }
    }
    
    // Parse data file using input_from_url function
    printf("Parsing data file...\n");
    Item data_item = ITEM_ERROR;
    
    if (!input_format || strcmp(input_format, "lambda") == 0) {
        // Parse as Lambda script directly - need to read file contents
        char* data_contents = read_text_file(data_file);
        if (!data_contents) {
            printf("Error: Cannot read Lambda script file\n");
            schema_validator_destroy(validator);
            pool_variable_destroy(pool);
            free(schema_contents);
            return;
        }
        data_item = run_script(&runtime, data_contents, (char*)data_file);
        free(data_contents);
    } else {
        // Set up minimal context for parsing non-Lambda formats
        Runner* temp_runner = malloc(sizeof(Runner));
        if (!temp_runner) {
            printf("Error: Cannot allocate Runner\n");
            schema_validator_destroy(validator);
            pool_variable_destroy(pool);
            free(schema_contents);
            return;
        }
        temp_runner_ptr = temp_runner;  // Track for cleanup
        runner_init(&runtime, temp_runner);
        
        // Create a minimal script structure for context setup
        temp_runner->script = (Script*)calloc(1, sizeof(Script));
        if (!temp_runner->script) {
            printf("Error: Cannot allocate script structure\n");
            free(temp_runner);
            schema_validator_destroy(validator);
            pool_variable_destroy(pool);
            free(schema_contents);
            return;
        }
        
        // Initialize minimal script components needed for context
        temp_runner->script->ast_pool = NULL;
        temp_runner->script->type_list = NULL;
        // Create an empty const_list to prevent null pointer dereference
        temp_runner->script->const_list = (ArrayList*)malloc(sizeof(ArrayList));
        if (temp_runner->script->const_list) {
            temp_runner->script->const_list->data = NULL;
            temp_runner->script->const_list->length = 0;  
            temp_runner->script->const_list->_alloced = 0;
        } else {
            printf("Error: Cannot allocate const_list\n");
            free(temp_runner->script);
            free(temp_runner);
            schema_validator_destroy(validator);
            pool_variable_destroy(pool);
            free(schema_contents);
            return;
        }
        
        // Set up the context for parsing
        runner_setup_context(temp_runner);
        
        // Use input_from_url for other formats
        // Convert file path to file:// URL
        char cwd_path[1024];
        if (!getcwd(cwd_path, sizeof(cwd_path))) {
            printf("Error: Cannot get current working directory\n");
            runner_cleanup(temp_runner);
            free(temp_runner->script);
            free(temp_runner);
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
            Input* input = input_from_url(url_string, type_string, NULL);
            if (input && input->root != ITEM_ERROR) {
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
        
        // Note: Don't cleanup temp_runner here since context is still needed for validation
        // We'll clean it up at the end of the function
    }
    
    if (data_item == ITEM_ERROR) {
        printf("Error: Failed to parse data file\n");
        schema_validator_destroy(validator);
        pool_variable_destroy(pool);
        free(schema_contents);
        return;
    }
    
    // Validate using the loaded schema
    printf("Validating data...\n");
    LambdaItem lambda_item = {.item = data_item};
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
    
    // Cleanup temp_runner if we created one for non-Lambda formats
    if (temp_runner_ptr) {
        runner_cleanup(temp_runner_ptr);
        free(temp_runner_ptr->script);
        free(temp_runner_ptr);
    }
    
    schema_validator_destroy(validator);
    pool_variable_destroy(pool);
    free(schema_contents);
}
