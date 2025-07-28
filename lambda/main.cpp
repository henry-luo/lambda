#include "transpiler.h"
#include "validator/validator.h"
#include "input/input.h"
#include <lexbor/url/url.h>
#include <unistd.h>  // for getcwd

// External function declarations
extern "C" {
    lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
    Input* input_from_url(String* url, String* type, lxb_url_t* cwd);
    
    // For accessing the validator's internal structure
    typedef struct {
        StrView name;
        TypeSchema* schema;  
    } SchemaEntry;
}

// System includes for environment and string functions
#include <stdlib.h>
#include <string.h>

// Windows-specific includes for console encoding
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

// Forward declare MIR transpiler function
extern "C" Item run_script_mir(Runtime *runtime, const char* source, char* script_path);

// Forward declare readline functions to avoid header conflicts
#ifndef _WIN32
extern "C" {
    char *readline(const char *);
    int add_history(const char *);
}
#else
// Simple Windows fallback for readline functionality
char *simple_readline(const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);
    
    char *line = (char*)malloc(1024);
    if (!line) return NULL;
    
    if (fgets(line, 1024, stdin) == NULL) {
        free(line);
        return NULL;
    }
    
    // Remove trailing newline
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
        line[len-1] = '\0';
    }
    
    return line;
}

void simple_add_history(const char *line) {
    // Simple stub - no history on Windows for now
    (void)line;
}

#define readline simple_readline
#define add_history simple_add_history
#endif

void print_help() {
    printf("Lambda Script Interpreter v1.0\n");
    printf("Usage:\n");
    printf("  lambda [script.ls]           - Run a script file\n");
    printf("  lambda --mir [script.ls]     - Run with MIR JIT compilation\n");
    printf("  lambda --repl                - Start REPL mode\n");
    printf("  lambda --repl --mir          - Start REPL with MIR JIT\n");
    printf("  lambda validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  lambda --help                - Show this help message\n");
    printf("\nREPL Commands:\n");
    printf("  :quit, :q, :exit     - Exit REPL\n");
    printf("  :help, :h            - Show help\n");
    printf("  :clear               - Clear REPL history\n");
    printf("\nValidation Commands:\n");
    printf("  validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  validate <file>                 - Validate using doc_schema.ls (default)\n");
}

// Function to determine the best REPL prompt based on system capabilities
const char* get_repl_prompt() {
#ifdef _WIN32
    // Try to enable UTF-8 support on Windows
    UINT old_cp = GetConsoleOutputCP();
    UINT old_input_cp = GetConsoleCP();
    
    if (SetConsoleOutputCP(CP_UTF8) && SetConsoleCP(CP_UTF8)) {
        // Check Windows version - lambda works reliably on Windows 10+
        DWORD version = GetVersion();
        DWORD major = (DWORD)(LOBYTE(LOWORD(version)));
        
        if (major >= 10) {
            return "λ> ";  // Use lambda on Windows 10+
        } else {
            // Restore old code pages and use fallback
            SetConsoleOutputCP(old_cp);
            SetConsoleCP(old_input_cp);
            return "L> ";
        }
    } else {
        // Failed to set UTF-8, use safe ASCII prompt
        return "L> ";
    }
#else
    // On Unix-like systems, UTF-8 is usually supported
    // Check if LANG/LC_ALL suggests UTF-8 support
    const char* lang = getenv("LANG");
    const char* lc_all = getenv("LC_ALL");
    
    if ((lang && strstr(lang, "UTF-8")) || (lc_all && strstr(lc_all, "UTF-8"))) {
        return "λ> ";
    } else {
        // Fallback for non-UTF-8 locales
        return "L> ";
    }
#endif
}

void run_repl(Runtime *runtime, bool use_mir) {
    printf("Lambda Script REPL v1.0%s\n", use_mir ? " (MIR JIT)" : "");
    printf("Type :help for commands, :quit to exit\n");
    
    // Get the best prompt for this system
    const char* prompt = get_repl_prompt();
    
    StrBuf *repl_history = strbuf_new_cap(1024);
    char *line;
    int exec_count = 0;
    
    while ((line = readline(prompt)) != NULL) {
        // Skip empty lines
        if (strlen(line) == 0) {
            free(line);
            continue;
        }
        
        // Add to readline history
        add_history(line);
        
        // Handle REPL commands
        if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0 || strcmp(line, ":exit") == 0) {
            free(line);
            break;
        }
        
        if (strcmp(line, ":help") == 0 || strcmp(line, ":h") == 0) {
            print_help();
            free(line);
            continue;
        }
        
        if (strcmp(line, ":clear") == 0) {
            strbuf_reset(repl_history);
            printf("REPL history cleared\n");
            free(line);
            continue;
        }
        
        // Add current line to REPL history
        if (repl_history->length > 0) {
            strbuf_append_str(repl_history, "\n");
        }
        strbuf_append_str(repl_history, line);
        
        // Create a unique script path for each execution
        char script_path[64];
        snprintf(script_path, sizeof(script_path), "<repl-%d>", ++exec_count);
        
        // Run the accumulated script
        Item result;
        if (use_mir) {
            result = run_script_mir(runtime, repl_history->str, script_path);
        } else {
            result = run_script(runtime, repl_history->str, script_path);
        }
        
        // Print result
        StrBuf *output = strbuf_new_cap(256);
        print_item(output, result);
        printf("%s\n", output->str);
        strbuf_free(output);
        
        free(line);
    }
    
    strbuf_free(repl_history);
}

void run_script_file(Runtime *runtime, const char *script_path, bool use_mir) {
    Item result;
    if (use_mir) {
        result = run_script_mir(runtime, NULL, (char*)script_path);
    } else {
        result = run_script_at(runtime, (char*)script_path);
    }
    
    StrBuf *output = strbuf_new_cap(256);
    print_item(output, result);
    printf("%s\n", output->str);
    strbuf_free(output);
}

// Utility function to read file contents
char* read_file_contents(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filename);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Read file contents
    char* contents = (char*)malloc(size + 1);
    if (!contents) {
        printf("Error: Cannot allocate memory for file contents\n");
        fclose(file);
        return NULL;
    }
    
    size_t bytes_read = fread(contents, 1, size, file);
    contents[bytes_read] = '\0';
    
    fclose(file);
    return contents;
}

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
    runtime.current_dir = const_cast<char*>("./");
    
    // Track if we created a temp_runner for cleanup
    Runner* temp_runner_ptr = nullptr;
    
    // Read schema file
    char* schema_contents = read_file_contents(schema_file);
    if (!schema_contents) {
        return;
    }
    
    // Create memory pool for validation
    VariableMemPool* pool = nullptr;
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
            }
            // If no recognized extension, keep as nullptr for Lambda format
        }
    }
    
    // Parse data file using input_from_url function
    printf("Parsing data file...\n");
    Item data_item = ITEM_ERROR;
    
    if (!input_format || strcmp(input_format, "lambda") == 0) {
        // Parse as Lambda script directly - need to read file contents
        char* data_contents = read_file_contents(data_file);
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
        Runner* temp_runner = new Runner();
        temp_runner_ptr = temp_runner;  // Track for cleanup
        runner_init(&runtime, temp_runner);
        
        // Create a minimal script structure for context setup
        temp_runner->script = (Script*)calloc(1, sizeof(Script));
        if (!temp_runner->script) {
            printf("Error: Cannot allocate script structure\n");
            delete temp_runner;
            schema_validator_destroy(validator);
            pool_variable_destroy(pool);
            free(schema_contents);
            return;
        }
        
        // Initialize minimal script components needed for context
        temp_runner->script->ast_pool = nullptr;
        temp_runner->script->type_list = nullptr;
        // Create an empty const_list to prevent null pointer dereference
        temp_runner->script->const_list = (ArrayList*)malloc(sizeof(ArrayList));
        if (temp_runner->script->const_list) {
            temp_runner->script->const_list->data = nullptr;
            temp_runner->script->const_list->length = 0;  
            temp_runner->script->const_list->_alloced = 0;
        } else {
            printf("Error: Cannot allocate const_list\n");
            free(temp_runner->script);
            delete temp_runner;
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
            delete temp_runner;
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
            
            String* type_string = nullptr;
            if (input_format && strcmp(input_format, "auto-detect") != 0) {
                type_string = (String*)malloc(sizeof(String) + strlen(input_format) + 1);
                if (type_string) {
                    type_string->len = strlen(input_format);
                    type_string->ref_cnt = 0;
                    strcpy(type_string->chars, input_format);
                }
            }
            
            // Use parse_url to create the URL (pass NULL for base to use absolute path)
            Input* input = input_from_url(url_string, type_string, nullptr);
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
                if (error->message && error->message->chars) {
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
        delete temp_runner_ptr;
    }
    
    schema_validator_destroy(validator);
    pool_variable_destroy(pool);
    free(schema_contents);
}

void run_assertions() {
#ifdef __cplusplus
    static_assert(sizeof(bool) == 1, "bool size == 1 byte");
    static_assert(sizeof(uint8_t) == 1, "uint8_t size == 1 byte");
    static_assert(sizeof(uint16_t) == 2, "uint16_t size == 2 bytes");
    static_assert(sizeof(uint32_t) == 4, "uint32_t size == 4 bytes");
    static_assert(sizeof(uint64_t) == 8, "uint64_t size == 8 bytes");
    static_assert(sizeof(int32_t) == 4, "int32_t size == 4 bytes");
    static_assert(sizeof(int64_t) == 8, "int64_t size == 8 bytes");
    static_assert(sizeof(Item) == sizeof(double), "Item size == double size");
    static_assert(sizeof(LambdaItem) == sizeof(Item), "LambdaItem size == Item size");
#else
    _Static_assert(sizeof(bool) == 1, "bool size == 1 byte");
    _Static_assert(sizeof(uint8_t) == 1, "uint8_t size == 1 byte");
    _Static_assert(sizeof(uint16_t) == 2, "uint16_t size == 2 bytes");
    _Static_assert(sizeof(uint32_t) == 4, "uint32_t size == 4 bytes");
    _Static_assert(sizeof(uint64_t) == 8, "uint64_t size == 8 bytes");
    _Static_assert(sizeof(int32_t) == 4, "int32_t size == 4 bytes");
    _Static_assert(sizeof(int64_t) == 8, "int64_t size == 8 bytes");
    _Static_assert(sizeof(Item) == sizeof(double), "Item size == double size");
    _Static_assert(sizeof(LambdaItem) == sizeof(Item), "LambdaItem size == Item size");
#endif
    LambdaItem itm = {.item = ITEM_ERROR};
    assert(itm.type_id == LMD_TYPE_ERROR);
    assert(1.0/0.0 == INFINITY);
    assert(-1.0/0.0 == -INFINITY);
}

int main(int argc, char *argv[]) {
    // Run basic assertions
    run_assertions();
    
    // If no arguments provided, show help by default
    if (argc == 1) {
        print_help();
        return 0;
    }
    
    // Parse command line arguments
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        return 0;
    }
    
    // Initialize runtime (needed for all operations)
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = const_cast<char*>("./");
    
    // Handle validate command
    if (argc >= 2 && strcmp(argv[1], "validate") == 0) {
        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Validator v1.0\n\n");
            printf("Usage: %s validate [-s <schema>] [-f <format>] <file> [files...]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  -s <schema>    Schema file (default: doc_schema.ls, html5_schema.ls for HTML)\n");
            printf("  -f <format>    Input format (auto-detect, json, xml, html, md, yaml, csv, ini, toml, etc.)\n");
            printf("  -h, --help     Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s validate document.ls\n", argv[0]);
            printf("  %s validate -s custom_schema.ls document.ls\n", argv[0]);
            printf("  %s validate -f html input.html  # Uses html5_schema.ls automatically\n", argv[0]);
            printf("  %s validate -f html -s schema.ls input.html\n", argv[0]);
            return 0;
        }
        
        if (argc < 3) {
            printf("Error: No file specified for validation\n");
            printf("Usage: %s validate [-s <schema>] [-f <format>] <file> [files...]\n", argv[0]);
            printf("Use '%s validate --help' for more information.\n", argv[0]);
            return 1;
        }
        
        const char* data_file = nullptr;
        const char* schema_file = nullptr;  // Will be determined based on format
        const char* input_format = nullptr;  // Auto-detect by default
        bool schema_explicitly_set = false;
        
        // Parse validation arguments
        for (int i = 2; i < argc; i++) {
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
                printf("Usage: %s validate [-s <schema>] [-f <format>] <file>\n", argv[0]);
                printf("Formats: auto, json, csv, ini, toml, yaml, xml, markdown, rst, html, latex, rtf, pdf, wiki, asciidoc, man, eml, vcf, ics, text\n");
                return 1;
            }
        }
        
        if (!data_file) {
            printf("Error: No input file specified\n");
            printf("Usage: %s validate [-s <schema>] [-f <format>] <file>\n", argv[0]);
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
            } else {
                schema_file = "lambda/input/doc_schema.ls";  // Default schema
            }
        }
        
        run_validation(data_file, schema_file, input_format);
        return 0;
    }
    
    bool use_mir = false;
    bool repl_mode = false;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mir") == 0) {
            use_mir = true;
        } else if (strcmp(argv[i], "--repl") == 0) {
            repl_mode = true;
        } else if (argv[i][0] != '-') {
            // This is a script file - run it
            run_script_file(&runtime, argv[i], use_mir);
            return 0;
        } else {
            // Unknown option
            printf("Error: Unknown option '%s'\n", argv[i]);
            print_help();
            return 1;
        }
    }
    
    // If --repl was specified, start REPL
    if (repl_mode) {
        run_repl(&runtime, use_mir);
    } else {
        // No script file specified and no --repl flag
        printf("Error: No script file specified. Use --repl for interactive mode.\n");
        print_help();
        return 1;
    }
    
    return 0;
}