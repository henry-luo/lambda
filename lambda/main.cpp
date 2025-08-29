#include "input/input.h"
#include "format/format.h"
#include "input/mime-detect.h"
#include <unistd.h>  // for getcwd
#include <limits.h>  // for PATH_MAX
// Unicode support (always enabled)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../lib/log.h"  // Add logging support
#include "validator/validator.hpp"  // For ValidationResult
#include "transpiler.hpp"  // For Runtime struct definition

// Forward declare additional transpiler functions
extern "C" {
    char* read_text_file(const char *filename);
    void write_text_file(const char *filename, const char *content);
    TSTree* lambda_parse_source(TSParser* parser, const char* source);
}

ValidationResult* run_validation(const char *data_file, const char *schema_file, const char *input_format);
ValidationResult* exec_validation(int argc, char* argv[]);
int exec_convert(int argc, char* argv[]);
void transpile_ast(Transpiler* tp, AstScript *script);

// External function declarations
extern "C" {
    #include "../lib/url.h"
    
    // Input functions
    Input* input_from_url(String* url, String* type, String* flavor, Url* cwd);
    
    // String utility functions from lib/string.h
    #include "../lib/string.h"
    // create_string function is declared in lib/string.h
    
    // For accessing the validator's internal structure
    // typedef struct {
    //     StrView name;
    //     TypeSchema* schema;  
    // } SchemaEntry;
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
Item run_script_mir(Runtime *runtime, const char* source, char* script_path);

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
    printf("  lambda                       - Start REPL mode (default)\n");
    printf("  lambda [script.ls]           - Run a script file\n");
    printf("  lambda --mir [script.ls]     - Run with MIR JIT compilation\n");
    printf("  lambda --transpile-only [script.ls] - Transpile to C code only (no execution)\n");
    printf("  lambda validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  lambda convert <input> -f <from> -t <to> -o <output>  - Convert between formats\n");
    printf("  lambda --help                - Show this help message\n");
    printf("\nREPL Commands:\n");
    printf("  :quit, :q, :exit     - Exit REPL\n");
    printf("  :help, :h            - Show help\n");
    printf("  :clear               - Clear REPL history\n");
    printf("\nValidation Commands:\n");
    printf("  validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  validate <file>                 - Validate using doc_schema.ls (default)\n");
    printf("\nConversion Commands:\n");
    printf("  convert <input> -f <from> -t <to> -o <output>  - Convert between formats\n");
    printf("  convert <input> -t <to> -o <output>           - Auto-detect input format\n");
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
            result = run_script(runtime, repl_history->str, script_path, false);
        }
        
        // Print result
        StrBuf *output = strbuf_new_cap(256);
        print_item(output, result);
        printf("%s", output->str);
        strbuf_free(output);
        
        free(line);
    }
    
    strbuf_free(repl_history);
}

void run_script_file(Runtime *runtime, const char *script_path, bool use_mir, bool transpile_only = false) {
    Item result;
    if (use_mir) {
        result = run_script_mir(runtime, NULL, (char*)script_path);
    } else {
        result = run_script_at(runtime, (char*)script_path, false);
    }
    
    printf("##### Script '%s' executed: #####\n", script_path);
    StrBuf *output = strbuf_new_cap(256);
    print_item(output, result);
    printf("%s", output->str);
    strbuf_free(output);
    // todo: should have return value
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
    static_assert(sizeof(Item) == 8, "Item size == 8 bytes");
    static_assert(sizeof(TypedItem) == 9, "TypedItem size == 9 bytes");
    static_assert(sizeof(DateTime) == 8, "DateTime size == 8 bytes");
#else
    _Static_assert(sizeof(bool) == 1, "bool size == 1 byte");
    _Static_assert(sizeof(uint8_t) == 1, "uint8_t size == 1 byte");
    _Static_assert(sizeof(uint16_t) == 2, "uint16_t size == 2 bytes");
    _Static_assert(sizeof(uint32_t) == 4, "uint32_t size == 4 bytes");
    _Static_assert(sizeof(uint64_t) == 8, "uint64_t size == 8 bytes");
    _Static_assert(sizeof(int32_t) == 4, "int32_t size == 4 bytes");
    _Static_assert(sizeof(int64_t) == 8, "int64_t size == 8 bytes");
    _Static_assert(sizeof(Item) == 8, "Item size == 8 bytes");
#endif
    Item itm = {.item = ITEM_ERROR};
    assert(itm.type_id == LMD_TYPE_ERROR);
    assert(1.0/0.0 == INFINITY);
    assert(-1.0/0.0 == -INFINITY);
}

// Convert command implementation
int exec_convert(int argc, char* argv[]) {
    log_debug("exec_convert called with %d arguments", argc);
    
    if (argc < 2) {
        printf("Error: convert command requires input file\n");
        printf("Usage: lambda convert <input> [-f <from>] -t <to> -o <output>\n");
        printf("Use 'lambda convert --help' for more information\n");
        return 1;
    }
    
    // Parse arguments
    const char* input_file = NULL;
    const char* from_format = NULL;  // Optional, will auto-detect if not provided
    const char* to_format = NULL;    // Required
    const char* output_file = NULL;  // Required
    
    // Skip "convert" and parse remaining arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--from") == 0) {
            if (i + 1 < argc) {
                from_format = argv[++i];
            } else {
                printf("Error: -f option requires a format argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--to") == 0) {
            if (i + 1 < argc) {
                to_format = argv[++i];
            } else {
                printf("Error: -t option requires a format argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                printf("Error: -o option requires an output file argument\n");
                return 1;
            }
        } else if (argv[i][0] != '-') {
            // This should be the input file
            if (input_file == NULL) {
                input_file = argv[i];
            } else {
                printf("Error: Multiple input files not supported\n");
                return 1;
            }
        } else {
            printf("Error: Unknown option '%s'\n", argv[i]);
            return 1;
        }
    }
    
    // Validate required arguments
    if (!input_file) {
        printf("Error: Input file is required\n");
        return 1;
    }
    if (!to_format) {
        printf("Error: Output format (-t) is required\n");
        return 1;
    }
    if (!output_file) {
        printf("Error: Output file (-o) is required\n");
        return 1;
    }
    
    // Check if input file exists
    if (access(input_file, F_OK) != 0) {
        printf("Error: Input file '%s' does not exist\n", input_file);
        return 1;
    }
    
    log_debug("Converting '%s' from '%s' to '%s', output: '%s'", 
              input_file, from_format ? from_format : "auto", to_format, output_file);
    
    log_debug("Converting '%s' from '%s' to '%s', output: '%s'", 
              input_file, from_format ? from_format : "auto", to_format, output_file);
    
    // Create a temporary memory pool for string creation
    VariableMemPool* temp_pool;
    MemPoolError err = pool_variable_init(&temp_pool, 1024, 20);
    if (err != MEM_POOL_ERR_OK) {
        printf("Error: Failed to initialize temporary memory pool\n");
        return 1;
    }
    
    // Step 1: Parse the input file using Lambda's input system
    printf("Reading input file: %s\n", input_file);
    
    // Create absolute URL for the input file
    char cwd_path[PATH_MAX];
    if (!getcwd(cwd_path, sizeof(cwd_path))) {
        printf("Error: Failed to get current directory\n");
        pool_variable_destroy(temp_pool);
        return 1;
    }
    
    // Create file URL
    char file_url[PATH_MAX + 8];
    if (input_file[0] == '/') {
        // Absolute path
        snprintf(file_url, sizeof(file_url), "file://%s", input_file);
    } else {
        // Relative path
        snprintf(file_url, sizeof(file_url), "file://%s/%s", cwd_path, input_file);
    }
    
    // Create URL string
    String* url_string = create_string(temp_pool, file_url);
    if (!url_string) {
        printf("Error: Failed to create URL string\n");
        pool_variable_destroy(temp_pool);
        return 1;
    }
    
    // Create type string
    String* type_string = NULL;
    if (from_format) {
        type_string = create_string(temp_pool, from_format);
    } else {
        type_string = create_string(temp_pool, "auto");
    }
    
    if (!type_string) {
        printf("Error: Failed to create type string\n");
        pool_variable_destroy(temp_pool);
        return 1;
    }
        
        // Parse using Lambda's input system
        Input* input = input_from_url(url_string, type_string, NULL, NULL);
        if (!input) {
            printf("Error: Failed to parse input file\n");
            pool_variable_destroy(temp_pool);
            return 1;
        }
        
        // Check if parsing was successful
        if (input->root.type_id == LMD_TYPE_ERROR) {
            printf("Error: Failed to parse input file\n");
            pool_variable_destroy(temp_pool);
            return 1;
        }
        
        printf("Successfully parsed input file\n");
        
        // Step 2: Format the parsed data to the target format
        printf("Converting to format: %s\n", to_format);
        
        String* formatted_output = NULL;
        
        // Use the existing format functions based on target format
        if (strcmp(to_format, "json") == 0) {
            formatted_output = format_json(input->pool, input->root);
        } else if (strcmp(to_format, "xml") == 0) {
            formatted_output = format_xml(input->pool, input->root);
        } else if (strcmp(to_format, "html") == 0) {
            formatted_output = format_html(input->pool, input->root);
        } else if (strcmp(to_format, "yaml") == 0) {
            formatted_output = format_yaml(input->pool, input->root);
        } else if (strcmp(to_format, "toml") == 0) {
            formatted_output = format_toml(input->pool, input->root);
        } else if (strcmp(to_format, "ini") == 0) {
            formatted_output = format_ini(input->pool, input->root);
        } else if (strcmp(to_format, "css") == 0) {
            formatted_output = format_css(input->pool, input->root);
        } else if (strcmp(to_format, "latex") == 0) {
            formatted_output = format_latex(input->pool, input->root);
        } else if (strcmp(to_format, "rst") == 0) {
            formatted_output = format_rst_string(input->pool, input->root);
        } else if (strcmp(to_format, "org") == 0) {
            formatted_output = format_org_string(input->pool, input->root);
        } else if (strcmp(to_format, "markdown") == 0 || strcmp(to_format, "md") == 0) {
            // Format as markdown using string buffer
            StrBuf* sb = strbuf_new_cap(1024);
            format_markdown(sb, input->root);
            formatted_output = create_string(input->pool, sb->str);
            strbuf_free(sb);
        } else {
            printf("Error: Unsupported output format '%s'\n", to_format);
            printf("Supported formats: json, xml, html, yaml, toml, ini, css, latex, rst, org, markdown\n");
            pool_variable_destroy(temp_pool);
            return 1;
        }
        
        if (!formatted_output) {
            printf("Error: Failed to format output\n");
            pool_variable_destroy(temp_pool);
            return 1;
        }
        
        // Step 3: Write the output to file
        printf("Writing output to: %s\n", output_file);
        write_text_file(output_file, formatted_output->chars);
        
    printf("Conversion completed successfully!\n");
    printf("Input:  %s\n", input_file);
    printf("Output: %s\n", output_file);
    printf("Format: %s → %s\n", from_format ? from_format : "auto-detected", to_format);
    
    // Cleanup
    pool_variable_destroy(temp_pool);
    return 0;
}

int main(int argc, char *argv[]) {
    // Initialize logging system with config file if available
    if (access("log.conf", F_OK) == 0) {
        // log.conf exists, load it
        if (log_parse_config_file("log.conf") != LOG_OK) {
            fprintf(stderr, "Warning: Failed to parse log.conf, using defaults\n");
        }
    }
    log_init("");  // Initialize with parsed config or defaults
    
    // Add trace statement at start of main
    log_debug("main() started with %d arguments", argc);
    
    // Run basic assertions
    log_debug("About to run assertions");
    run_assertions();
    log_debug("Assertions completed");
    
    // Parse command line arguments
    log_debug("Parsing command line arguments");
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        log_finish();  // Cleanup logging before exit
        return 0;
    }
    
    // Initialize runtime (needed for all operations)
    log_debug("About to initialize runtime");
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = const_cast<char*>("./");
    log_debug("Runtime initialized");

    // Initialize utf8proc Unicode support (always enabled)
    log_debug("About to initialize utf8proc Unicode support");
    init_utf8proc_support();
    log_debug("utf8proc Unicode support initialized");

    // Handle validate command
    log_debug("Checking for validate command");
    if (argc >= 2 && strcmp(argv[1], "validate") == 0) {
        log_debug("Entering validate command handler");
        
        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Validator v1.0\n\n");
            printf("Usage: %s validate [-s <schema>] [-f <format>] <file> [files...]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  -s <schema>    Schema file (required for some formats)\n");
            printf("  -f <format>    Input format (auto-detect, json, xml, html, md, yaml, csv, ini, toml, etc.)\n");
            printf("  -h, --help     Show this help message\n");
            printf("\nDefault Schemas:\n");
            printf("  html           - html5_schema.ls\n");
            printf("  eml            - eml_schema.ls\n");
            printf("  ics            - ics_schema.ls\n");
            printf("  vcf            - vcf_schema.ls\n");
            printf("  asciidoc       - doc_schema.ls\n");
            printf("  man            - doc_schema.ls\n");
            printf("  markdown       - doc_schema.ls\n");
            printf("  rst            - doc_schema.ls\n");
            printf("  textile        - doc_schema.ls\n");
            printf("  wiki           - doc_schema.ls\n");
            printf("  lambda         - doc_schema.ls\n");
            printf("\nRequire Explicit Schema (-s option):\n");
            printf("  json, xml, yaml, csv, ini, toml, latex, rtf, pdf, text\n");
            printf("\nExamples:\n");
            printf("  %s validate document.ls\n", argv[0]);
            printf("  %s validate -s custom_schema.ls document.ls\n", argv[0]);
            printf("  %s validate -f html input.html  # Uses html5_schema.ls automatically\n", argv[0]);
            printf("  %s validate -f html -s schema.ls input.html\n", argv[0]);
            log_finish();  // Cleanup logging before exit
            return 0;
        }
        
        // Prepare arguments for exec_validation (skip the "validate" command)
        int validation_argc = argc - 1;  // Remove the program name
        char** validation_argv = argv + 1;  // Skip the program name, start from "validate"
        
        // Call the extracted validation function
        ValidationResult* validation_result = exec_validation(validation_argc, validation_argv);
        
        // Convert ValidationResult to exit code
        int exit_code = 1; // Default to failure
        if (validation_result) {
            exit_code = validation_result->valid ? 0 : 1;
        }
        
        log_debug("exec_validation completed with result: %d", exit_code);
        log_finish();  // Cleanup logging before exit
        return exit_code;
    }
    
    // Handle convert command
    log_debug("Checking for convert command");
    if (argc >= 2 && strcmp(argv[1], "convert") == 0) {
        log_debug("Entering convert command handler");
        
        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Format Converter v1.0\n\n");
            printf("Usage: %s convert <input> [-f <from>] -t <to> -o <output>\n", argv[0]);
            printf("\nOptions:\n");
            printf("  -f <from>      Input format (auto-detect if omitted)\n");
            printf("  -t <to>        Output format (required)\n");
            printf("  -o <output>    Output file path (required)\n");
            printf("  -h, --help     Show this help message\n");
            printf("\nSupported Formats:\n");
            printf("  Text formats:    markdown, html, xml, json, yaml, toml, ini, csv, latex, rst, org\n");
            printf("  Document formats: pdf, rtf, text\n");
            printf("  Markup formats:  asciidoc, textile, wiki, man\n");
            printf("  Data formats:    json, xml, yaml, csv, ini, toml\n");
            printf("\nCommon Conversions:\n");
            printf("  markdown → html:   %s convert doc.md -t html -o doc.html\n", argv[0]);
            printf("  json → yaml:       %s convert data.json -t yaml -o data.yaml\n", argv[0]);
            printf("  html → markdown:   %s convert page.html -t markdown -o page.md\n", argv[0]);
            printf("  xml → json:        %s convert config.xml -t json -o config.json\n", argv[0]);
            printf("\nAuto-detection Examples:\n");
            printf("  %s convert document.md -t html -o output.html\n", argv[0]);
            printf("  %s convert -f markdown data.txt -t json -o data.json\n", argv[0]);
            log_finish();  // Cleanup logging before exit
            return 0;
        }
        
        // Prepare arguments for exec_convert (skip the "convert" command)
        int convert_argc = argc - 1;  // Remove the program name
        char** convert_argv = argv + 1;  // Skip the program name, start from "convert"
        
        // Call the convert function
        int exit_code = exec_convert(convert_argc, convert_argv);
        
        log_debug("exec_convert completed with result: %d", exit_code);
        log_finish();  // Cleanup logging before exit
        return exit_code;
    }
    
    bool use_mir = false;
    bool transpile_only = false;
    bool help_only = false;
    char* script_file = NULL;

    // Parse arguments
    int ret_code = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mir") == 0) {
            use_mir = true;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            help_only = true;
        }
        else if (strcmp(argv[i], "--transpile-only") == 0) {
            transpile_only = true;
        } 
        else if (argv[i][0] != '-') {
            // This is a script file
            script_file = argv[i];
            
        } 
        else {
            // Unknown option
            printf("Error: Unknown option '%s'\n", argv[i]);
            help_only = true;
            ret_code = 1;
        }
    }
    
    if (help_only) {
        print_help();
    }
    else if (script_file) {
        run_script_file(&runtime, script_file, use_mir, transpile_only);
        // todo: inspect return value
    }
    else if (transpile_only) { // without a script file
        printf("Error: --transpile-only requires a script file\n");
        print_help();
        ret_code = 1;
    } else {
        // start REPL mode by default (with or without MIR)
        run_repl(&runtime, use_mir);
    }
    
    cleanup_utf8proc_support();
    log_finish();
    return ret_code;
}