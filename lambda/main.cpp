#include "transpiler.hpp"
// Forward declare the validation function to avoid C++ compilation issues
extern "C" void run_validation(const char *data_file, const char *schema_file, const char *input_format);
#include "input/input.h"
#include <lexbor/url/url.h>
#include <unistd.h>  // for getcwd

// Unicode support
#include "unicode_config.h"
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
#include "unicode_string.h"
#endif

// Forward declare additional transpiler functions
extern "C" {
    char* read_text_file(const char *filename);
    void write_text_file(const char *filename, const char *content);
    TSTree* lambda_parse_source(TSParser* parser, const char* source);
}

// C++ functions
void transpile_ast(Transpiler* tp, AstScript *script);

// External function declarations
extern "C" {
    lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
    Input* input_from_url(String* url, String* type, String* flavor, lxb_url_t* cwd);
    
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
    printf("  lambda --transpile-only [script.ls] - Transpile to C code only (no execution)\n");
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

void run_script_file(Runtime *runtime, const char *script_path, bool use_mir, bool transpile_only = false) {
    Item result;
    if (use_mir) {
        result = run_script_mir(runtime, NULL, (char*)script_path);
    } else {
        result = run_script_at(runtime, (char*)script_path, transpile_only);
    }
    
    printf("######### Script '%s' executed: ######################\n", script_path);
    StrBuf *output = strbuf_new_cap(256);
    print_item(output, result);
    printf("%s\n", output->str);
    strbuf_free(output);
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
    
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Initialize Unicode support
    init_unicode_support();
#endif
    
    // Handle validate command
    if (argc >= 2 && strcmp(argv[1], "validate") == 0) {
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
        
        run_validation(data_file, schema_file, input_format);
        return 0;
    }
    
    bool use_mir = false;
    bool repl_mode = false;
    bool transpile_only = false;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mir") == 0) {
            use_mir = true;
        } else if (strcmp(argv[i], "--repl") == 0) {
            repl_mode = true;
        } else if (strcmp(argv[i], "--transpile-only") == 0) {
            transpile_only = true;
        } else if (argv[i][0] != '-') {
            // This is a script file
            run_script_file(&runtime, argv[i], use_mir, transpile_only);
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
        if (transpile_only) {
            printf("Error: --transpile-only cannot be used with --repl\n");
            return 1;
        }
        run_repl(&runtime, use_mir);
    } else if (transpile_only) {
        printf("Error: --transpile-only requires a script file\n");
        print_help();
        return 1;
    } else {
        // No script file specified and no --repl flag
        printf("Error: No script file specified. Use --repl for interactive mode.\n");
        print_help();
        return 1;
    }
    
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Clean up Unicode support
    cleanup_unicode_support();
#endif
    
    return 0;
}