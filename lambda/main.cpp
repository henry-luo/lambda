#include "input/input.hpp"
#include "format/format.h"
#include "format/format-markup.h"
#include "../lib/mime-detect.h"
#include "../lib/mempool.h"
#include "../lib/memtrack.h"
#include "../lib/strbuf.h"  // For string buffer
#include "../lib/str.h"     // For str_to_int64_default, str_to_double_default
#include "../lib/arena.h"   // For arena allocator
#include <unistd.h>  // for getcwd
#include <limits.h>  // for PATH_MAX
// Unicode support (always enabled)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../lib/log.h"  // Add logging support
#include "validator/validator.hpp"  // For ValidationResult
#include "transpiler.hpp"  // For Runtime struct definition
#include "ast.hpp"  // For print_root_item declaration

// Unified LaTeX pipeline
#include "tex/tex_document_model.hpp"

// Error handling with stack traces
#include "lambda-error.h"

// System info for sys.* paths
#include "sysinfo.h"

// Graph layout includes
#include "../radiant/layout_graph.hpp"
#include "../radiant/graph_to_svg.hpp"
#include "../radiant/graph_theme.hpp"
#include "input/input-graph.h"

// Network module includes
#include "network/network_downloader.h"
#include "network/network_resource_manager.h"
#include "network/network_thread_pool.h"

#ifdef _WIN32
// Windows compatibility shim for __intrinsic_setjmpex
// In MinGW, we'll use regular setjmp instead of the Microsoft intrinsic
#include <setjmp.h>
#include <windows.h>  // For console UTF-8 setup
extern "C" int __intrinsic_setjmpex(jmp_buf env, void* context) {
    // In practice, __intrinsic_setjmpex is similar to setjmp but with SEH support
    // For MinGW compatibility, we'll use standard setjmp
    (void)context; // Unused in MinGW version
    return setjmp(env);
}
#endif

// Forward declare additional transpiler functions
extern "C" {
    char* read_text_file(const char *filename);
    void write_text_file(const char *filename, const char *content);
    TSTree* lambda_parse_source(TSParser* parser, const char* source);
}

// Thread-local context from runner.cpp (for error handling)
extern __thread EvalContext* context;

// Accessor for persistent last error from runner.cpp
LambdaError* get_persistent_last_error();
void clear_persistent_last_error();

// ValidationResult* run_ast_validation(const char *data_file, const char *schema_file, const char *input_format);
AstValidationResult* exec_validation(int argc, char* argv[]);
int exec_convert(int argc, char* argv[]);

// Layout command implementation (Lambda HTML/CSS layout with Radiant engine)
int cmd_layout(int argc, char** argv);

// WebDriver server command implementation
int cmd_webdriver(int argc, char** argv);

// Legacy layout function from radiant (for backward compatibility)
int run_layout(const char* html_file);

// SVG rendering function from radiant (available since radiant sources are included in lambda.exe)
int render_html_to_svg(const char* html_file, const char* svg_file, int viewport_width = 1200, int viewport_height = 800, float scale = 1.0f);

// DVI rendering function for LaTeX files (TeX typesetting pipeline)
int render_latex_to_dvi(const char* latex_file, const char* dvi_file);

// Math formula rendering function (quick testing of single formulas)
int render_math_to_dvi(const char* math_formula, const char* dvi_file, bool dump_ast, bool dump_boxes);

// Math formula HTML rendering function
int render_math_to_html(const char* math_formula, const char* html_file, bool standalone);

// Math formula AST JSON rendering function (for test framework)
int render_math_to_ast_json(const char* math_formula, const char* json_file);

// PDF rendering function from radiant (available since radiant sources are included in lambda.exe)
int render_html_to_pdf(const char* html_file, const char* pdf_file, int viewport_width = 800, int viewport_height = 1200, float scale = 1.0f);

// PNG rendering function from radiant (available since radiant sources are included in lambda.exe)
int render_html_to_png(const char* html_file, const char* png_file, int viewport_width = 1200, int viewport_height = 800, float scale = 1.0f, float pixel_ratio = 1.0f);

// JPEG rendering function from radiant (available since radiant sources are included in lambda.exe)
int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality, int viewport_width = 1200, int viewport_height = 800, float scale = 1.0f, float pixel_ratio = 1.0f);

// Document viewer function from radiant - unified viewer for all document types (HTML, PDF, Markdown, etc.)
// LaTeX flavor enum (matches window.cpp)
enum LatexFlavorMain {
    LATEX_FLAVOR_AUTO_M,      // Use environment variable or default
    LATEX_FLAVOR_JS_M,        // LaTeX→HTML→CSS pipeline (latex-js)
    LATEX_FLAVOR_TEX_PROPER_M // LaTeX→TeX→ViewTree pipeline (tex-proper)
};
extern int view_doc_in_window(const char* doc_file);
extern int view_doc_in_window_with_events(const char* doc_file, const char* event_file, int flavor = LATEX_FLAVOR_AUTO_M);

// REPL functions from main-repl.cpp
extern int lambda_repl_init();
extern void lambda_repl_cleanup();
void transpile_ast_root(Transpiler* tp, AstScript *script);

// External function declarations
extern "C" {
    #include "../lib/url.h"
    // String utility functions from lib/string.h
    #include "../lib/string.h"
    // create_string function is declared in lib/string.h
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
Input* run_script_mir(Runtime *runtime, const char* source, char* script_path, bool run_main);

// Forward declare function with run_main support
Input* run_script_with_run_main(Runtime *runtime, char* script_path, bool transpile_only, bool run_main);

// Forward declare REPL functions from main-repl.cpp
const char* get_repl_prompt();
const char* get_continuation_prompt();
char *lambda_repl_readline(const char *prompt);
int lambda_repl_add_history(const char *line);
void print_help();

// Statement completeness check for multi-line REPL input
enum StatementStatus {
    STMT_COMPLETE,      // statement is syntactically complete
    STMT_INCOMPLETE,    // statement needs more input (missing closing braces, etc.)
    STMT_ERROR          // statement has a syntax error
};
StatementStatus check_statement_completeness(TSParser* parser, const char* source);

// Linux-specific compatibility functions
#ifdef NATIVE_LINUX_BUILD
#include <stdint.h>

// Undefine any existing macros for endianness functions
#ifdef le16toh
#undef le16toh
#endif
#ifdef be16toh
#undef be16toh
#endif

// Provide endianness functions for tree-sitter
extern "C" {
uint16_t le16toh(uint16_t little_endian_16bits) {
    return little_endian_16bits;  // Assuming little-endian host
}

uint16_t be16toh(uint16_t big_endian_16bits) {
    return __builtin_bswap16(big_endian_16bits);
}
}

// Typeset function stub for Linux builds
extern "C" bool fn_typeset_latex_standalone(const char* input_file, const char* output_file) {
    printf("Typeset functionality not available in Linux build\n");
    (void)input_file; (void)output_file; // Suppress unused parameter warnings
    return false;
}
#else
// Typeset function stub for macOS/Windows builds
extern "C" bool fn_typeset_latex_standalone(const char* input_file, const char* output_file) {
    printf("Typeset functionality not yet implemented\n");
    (void)input_file; (void)output_file; // Suppress unused parameter warnings
    return false;
}
#endif

void run_repl(Runtime *runtime, bool use_mir) {
    printf("Lambda Script REPL v1.0%s\n", use_mir ? " (MIR JIT)" : "");
    printf("Type .help for commands, .quit to exit\n");
    printf("Multi-line input: use continuation prompt (.. ) for incomplete statements\n");

    // Initialize command line editor
    if (lambda_repl_init() != 0) {
        printf("Warning: Failed to initialize readline, using basic input\n");
    }

    // Get the best prompt for this system
    const char* main_prompt = get_repl_prompt();
    const char* cont_prompt = get_continuation_prompt();

    StrBuf *repl_history = strbuf_new_cap(1024);  // accumulated script buffer
    StrBuf *pending_input = strbuf_new_cap(256);  // current multi-line input
    StrBuf *last_output = strbuf_new_cap(256);    // last output for incremental display
    char *line;
    int exec_count = 0;

    while ((line = lambda_repl_readline(pending_input->length > 0 ? cont_prompt : main_prompt)) != NULL) {
        // Skip empty lines when not in multi-line mode
        if (strlen(line) == 0 && pending_input->length == 0) {
            free(line);
            continue;
        }

        // Add to command history (only for first line of multi-line input)
        if (pending_input->length == 0) {
            lambda_repl_add_history(line);
        }

        // Handle REPL commands (only when not in multi-line mode)
        if (pending_input->length == 0) {
            if (strcmp(line, ".quit") == 0 || strcmp(line, ".q") == 0 || strcmp(line, ".exit") == 0) {
                free(line);
                break;
            }

            if (strcmp(line, ".help") == 0 || strcmp(line, ".h") == 0) {
                print_help();
                free(line);
                continue;
            }

            if (strcmp(line, ".clear") == 0) {
                strbuf_reset(repl_history);
                strbuf_reset(last_output);
                printf("REPL history cleared\n");
                free(line);
                continue;
            }
        }

        // Append line to pending input
        if (pending_input->length > 0) {
            strbuf_append_str(pending_input, "\n");
        }
        strbuf_append_str(pending_input, line);
        free(line);

        // Check if statement is complete using Tree-sitter
        StatementStatus status = check_statement_completeness(runtime->parser, pending_input->str);

        if (status == STMT_INCOMPLETE) {
            // Need more input - continue with continuation prompt
            continue;
        }

        if (status == STMT_ERROR) {
            // Syntax error - discard the pending input and let user retry
            printf("Syntax error. Input discarded.\n");
            strbuf_reset(pending_input);
            continue;
        }

        // Statement is complete - add to history and execute
        size_t saved_history_len = repl_history->length;
        if (repl_history->length > 0) {
            strbuf_append_str(repl_history, "\n");
        }
        strbuf_append_str(repl_history, pending_input->str);
        strbuf_reset(pending_input);

        // Create a unique script path for each execution
        char script_path[64];
        snprintf(script_path, sizeof(script_path), "<repl-%d>", ++exec_count);

        // Run the accumulated script
        Input* output_input = nullptr;
        if (use_mir) {
            // transpile using MIR
            output_input = run_script_mir(runtime, repl_history->str, script_path, false);
        } else {
            // transpile using C2MIR
            output_input = run_script(runtime, repl_history->str, script_path, false);
        }

        if (output_input) {
            if (output_input->root.type_id() == LMD_TYPE_ERROR) {
                // Runtime error - rollback the last input
                printf("Error during execution. Last input rolled back.\n");
                repl_history->str[saved_history_len] = '\0';
                repl_history->length = saved_history_len;
            } else {
                // Success - print only new output (incremental display)
                StrBuf *full_output = strbuf_new_cap(256);
                print_root_item(full_output, output_input->root);

                // Print only the portion after last_output
                if (full_output->length > last_output->length) {
                    // check if prefix matches
                    if (last_output->length == 0 ||
                        strncmp(full_output->str, last_output->str, last_output->length) == 0) {
                        // print only the new part
                        printf("%s", full_output->str + last_output->length);
                    } else {
                        // output structure changed, print all
                        printf("%s", full_output->str);
                    }
                } else if (full_output->length > 0) {
                    // output got shorter or same - just print it
                    printf("%s", full_output->str);
                }

                // save for next incremental display
                strbuf_reset(last_output);
                strbuf_append_str(last_output, full_output->str);
                strbuf_free(full_output);
            }
        }
    }
    printf("\n");  // print one last '\n', otherwise, may see '%' at the end of the line

    // Cleanup command line editor
    lambda_repl_cleanup();

    strbuf_free(repl_history);
    strbuf_free(pending_input);
    strbuf_free(last_output);
}

// Run a script file and return 0 on success, 1 on failure
int run_script_file(Runtime *runtime, const char *script_path, bool use_mir, bool transpile_only = false, bool run_main = false) {
    log_debug("run_script_file called: %s, use_mir=%d", script_path, use_mir);
    Input* output_input = nullptr;
    if (use_mir) {
        output_input = run_script_mir(runtime, nullptr, (char*)script_path, run_main);
    } else {
        output_input = run_script_with_run_main(runtime, (char*)script_path, false, run_main);
    }

    log_debug("run_script_file: output_input = %p", output_input);
    if (!output_input) {
        log_error("Failed to execute script: %s (output_input is NULL)", script_path);
        fprintf(stderr, "Error: Failed to execute script: %s\n", script_path);
        return 1;  // failure
    }

    log_debug("run_script_file: output_input->root.item = %llu", output_input->root.item);
    // Check if the result is an error
    if (output_input->root.type_id() == LMD_TYPE_ERROR) {
        log_debug("Script returned ItemError");

        // Print detailed error with stack trace if available
        // Use persistent error since context may have gone out of scope
        LambdaError* last_error = get_persistent_last_error();
        if (last_error) {
            err_print(last_error);
            clear_persistent_last_error();  // free after printing
        } else {
            fprintf(stderr, "Error: Script execution failed: %s\n", script_path);
        }

        // Clean up the error output (it has its own pool)
        // The Input struct was allocated from its own pool, so we just destroy the pool
        if (output_input->pool) {
            pool_destroy(output_input->pool);
        }
        // Do NOT delete output_input - it was allocated from the pool we just destroyed
        return 1;  // failure
    }

    StrBuf *output = strbuf_new_cap(256);
    print_root_item(output, output_input->root);
    log_debug("Script '%s' executed ====================", script_path);
    if (run_main) {
        // just print to debug log
        log_debug("%s", output->str);
        printf("\n");  // help end any output, otherwise, may see '%' at the end of the line
    } else {
        // printf("##### Script '%s' executed: #####\n", script_path);
        printf("%s", output->str);
        log_debug("%s", output->str);
    }
    strbuf_free(output);

    // Note: Do NOT destroy output_input->pool here!
    // The pool is shared with the Script, which is managed by the Runtime
    // Also do NOT delete output_input - it was allocated from the pool
    return 0;  // success
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
    static_assert(sizeof(ConstItem) == 8, "ConstItem size == 8 bytes");
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
    assert(itm._type_id == LMD_TYPE_ERROR);
    assert(1.0/0.0 == INFINITY);
    assert(-1.0/0.0 == -INFINITY);
}

// Convert command implementation
int exec_convert(int argc, char* argv[]) {
    log_debug("exec_convert called with %d arguments", argc);

    if (argc < 2) {
        printf("Error: convert command requires input file\n");
        printf("Usage: lambda convert <input> [-f <from>] -t <to> -o <output> [--full-document]\n");
        printf("Use 'lambda convert --help' for more information\n");
        return 1;
    }

    // Parse arguments
    const char* input_file = NULL;
    const char* from_format = NULL;  // Optional, will auto-detect if not provided
    const char* to_format = NULL;    // Required
    const char* output_file = NULL;  // Required
    bool full_document = false;      // For LaTeX to HTML: generate complete HTML with CSS
    const char* pipeline = NULL;     // Pipeline selection: "legacy" or "unified"

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
        } else if (strcmp(argv[i], "--full-document") == 0) {
            full_document = true;
        } else if (strcmp(argv[i], "--pipeline") == 0) {
            if (i + 1 < argc) {
                pipeline = argv[++i];
                if (strcmp(pipeline, "legacy") != 0 && strcmp(pipeline, "unified") != 0) {
                    printf("Error: --pipeline must be 'legacy' or 'unified'\n");
                    return 1;
                }
            } else {
                printf("Error: --pipeline option requires an argument (legacy|unified)\n");
                return 1;
            }
        } else if (strncmp(argv[i], "--pipeline=", 11) == 0) {
            // Handle --pipeline=value format
            pipeline = argv[i] + 11;
            if (strcmp(pipeline, "legacy") != 0 && strcmp(pipeline, "unified") != 0) {
                printf("Error: --pipeline must be 'legacy' or 'unified'\n");
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

    // Check if input file exists (skip check for HTTP/HTTPS URLs)
    bool is_http_url = (strncmp(input_file, "http://", 7) == 0 || strncmp(input_file, "https://", 8) == 0);
    if (!is_http_url && access(input_file, F_OK) != 0) {
        printf("Error: Input file '%s' does not exist\n", input_file);
        return 1;
    }

    log_debug("Converting '%s' from '%s' to '%s', output: '%s'",
              input_file, from_format ? from_format : "auto", to_format, output_file);

    log_debug("Converting '%s' from '%s' to '%s', output: '%s'",
              input_file, from_format ? from_format : "auto", to_format, output_file);

    // Create a temporary memory pool for string creation
    Pool* temp_pool = pool_create();
    if (!temp_pool) {
        printf("Error: Failed to initialize temporary memory pool\n");
        return 1;
    }

    // Step 1: Parse the input file using Lambda's input system
    printf("Reading input file: %s\n", input_file);

    // Create absolute URL for the input file
    char cwd_path[PATH_MAX];
    if (!getcwd(cwd_path, sizeof(cwd_path))) {
        printf("Error: Failed to get current directory\n");
        pool_destroy(temp_pool);
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
        pool_destroy(temp_pool);
        return 1;
    }

    // Create type string (handle "type:flavor" format)
    String* type_string = NULL;
    String* flavor_string = NULL;
    if (from_format) {
        // Check for colon-separated format (e.g., "graph:mermaid")
        const char* colon = strchr(from_format, ':');
        if (colon) {
            // Split into type and flavor
            size_t type_len = colon - from_format;
            char* type_buf = (char*)pool_calloc(temp_pool, type_len + 1);
            strncpy(type_buf, from_format, type_len);
            type_buf[type_len] = '\0';
            type_string = create_string(temp_pool, type_buf);
            flavor_string = create_string(temp_pool, colon + 1);
        } else {
            type_string = create_string(temp_pool, from_format);
        }
    } else {
        type_string = create_string(temp_pool, "auto");
    }

    if (!type_string) {
        printf("Error: Failed to create type string\n");
        pool_destroy(temp_pool);
        return 1;
    }

        // Parse using Lambda's input system
        Input* input = input_from_url(url_string, type_string, flavor_string, NULL);
        if (!input) {
            printf("Error: Failed to parse input file\n");
            pool_destroy(temp_pool);
            return 1;
        }

        // Check if parsing was successful
        if (input->root.type_id() == LMD_TYPE_ERROR) {
            printf("Error: Failed to parse input file\n");
            pool_destroy(temp_pool);
            return 1;
        }

        printf("Successfully parsed input file\n");

        // Capture the effective type by checking if LaTeX parsing was used
        bool is_latex_input = false;
        if (from_format && (strcmp(from_format, "latex") == 0 ||
                           strcmp(from_format, "tex") == 0 ||
                           strcmp(from_format, "latex-ts") == 0)) {
            is_latex_input = true;
        } else if (!from_format && strcmp(type_string->chars, "auto") == 0) {
            // For auto-detection, check if the file extension suggests LaTeX
            const char* filename = strrchr(input_file, '/');
            filename = filename ? filename + 1 : input_file; // Get just the filename
            const char* ext = strrchr(filename, '.');
            if (ext && (strcmp(ext, ".tex") == 0 || strcmp(ext, ".latex") == 0)) {
                is_latex_input = true;
            }
        } else if (type_string && (strcmp(type_string->chars, "latex-ts") == 0 ||
                                   strcmp(type_string->chars, "latex") == 0)) {
            // Also check the actual input type used
            is_latex_input = true;
        }

        // Step 2: Format the parsed data to the target format
        printf("Converting to format: %s\n", to_format);

        String* formatted_output = NULL;
        StrBuf* full_doc_output = NULL;  // For full document mode

        // Use the existing format functions based on target format
        bool is_yaml_input = from_format && (strcmp(from_format, "yaml") == 0 || strcmp(from_format, "yml") == 0);
        if (strcmp(to_format, "json") == 0) {
            // empty YAML document (comments-only, whitespace-only, etc.)
            if (is_yaml_input && input->doc_count == 0 && get_type_id(input->root) == LMD_TYPE_NULL) {
                full_doc_output = strbuf_new_cap(1);
                // output empty string
            }
            // multi-doc YAML: output each document as separate JSON
            else if (input->doc_count > 1 && get_type_id(input->root) == LMD_TYPE_ARRAY) {
                full_doc_output = strbuf_new_cap(256);
                Array* arr = (Array*)(input->root.item & 0x00FFFFFFFFFFFFFFULL);
                int count = arr->length;
                for (int i = 0; i < count; i++) {
                    if (i > 0) strbuf_append_char(full_doc_output, '\n');
                    String* doc_json = format_json(input->pool, arr->items[i]);
                    if (doc_json) strbuf_append_str(full_doc_output, doc_json->chars);
                }
            } else {
                formatted_output = format_json(input->pool, input->root);
            }
        } else if (strcmp(to_format, "xml") == 0) {
            formatted_output = format_xml(input->pool, input->root);
        } else if (strcmp(to_format, "html") == 0) {
            // Check if input is LaTeX and route to unified converter
            if (is_latex_input) {
                // Always use unified pipeline (doc model based) for LaTeX to HTML
                printf("Using unified LaTeX pipeline\n");

                    // Read the source file content
                    char* source_content = read_text_file(input_file);
                    if (!source_content) {
                        printf("Error: Failed to read source file for unified pipeline\n");
                        pool_destroy(temp_pool);
                        return 1;
                    }

                    // Create arena for document model
                    Pool* doc_pool = pool_create();
                    Arena* doc_arena = arena_create_default(doc_pool);

                    // Create font manager for math typesetting
                    tex::TFMFontManager* fonts = tex::create_font_manager(doc_arena);

                    // Build document model
                    tex::TexDocumentModel* doc = tex::doc_model_from_string(
                        source_content, strlen(source_content), doc_arena, fonts);

                    free(source_content);

                    if (!doc || !doc->root) {
                        printf("Error: Unified pipeline - document model creation failed\n");
                        arena_destroy(doc_arena);
                        pool_destroy(doc_pool);
                        pool_destroy(temp_pool);
                        return 1;
                    }

                    // Render to HTML
                    StrBuf* html_buf = strbuf_new_cap(8192);
                    tex::HtmlOutputOptions opts = tex::HtmlOutputOptions::defaults();
                    opts.standalone = full_document;
                    opts.pretty_print = true;

                    bool success = tex::doc_model_to_html(doc, html_buf, opts);

                    if (success && html_buf->length > 0) {
                        full_doc_output = strbuf_dup(html_buf);
                    } else {
                        printf("Error: Unified pipeline - HTML rendering failed\n");
                    }

                    strbuf_free(html_buf);
                    arena_destroy(doc_arena);
                    pool_destroy(doc_pool);
            } else {
                // Use regular HTML formatter
                formatted_output = format_html(input->pool, input->root);
            }
        } else if (strcmp(to_format, "yaml") == 0) {
            formatted_output = format_yaml(input->pool, input->root);
        } else if (strcmp(to_format, "toml") == 0) {
            formatted_output = format_toml(input->pool, input->root);
        } else if (strcmp(to_format, "ini") == 0) {
            formatted_output = format_ini(input->pool, input->root);
        } else if (strcmp(to_format, "properties") == 0) {
            formatted_output = format_properties(input->pool, input->root);
        } else if (strcmp(to_format, "css") == 0) {
            formatted_output = format_css(input->pool, input->root);
        } else if (strcmp(to_format, "latex") == 0) {
            formatted_output = format_latex(input->pool, input->root);
        } else if (strcmp(to_format, "rst") == 0) {
            formatted_output = format_markup_string(input->pool, input->root, &RST_RULES);
        } else if (strcmp(to_format, "org") == 0) {
            formatted_output = format_markup_string(input->pool, input->root, &ORG_RULES);
        } else if (strcmp(to_format, "wiki") == 0) {
            formatted_output = format_markup_string(input->pool, input->root, &WIKI_RULES);
        } else if (strcmp(to_format, "textile") == 0) {
            formatted_output = format_markup_string(input->pool, input->root, &TEXTILE_RULES);
        } else if (strcmp(to_format, "text") == 0) {
            formatted_output = format_text_string(input->pool, input->root);
        } else if (strcmp(to_format, "jsx") == 0) {
            formatted_output = format_jsx(input->pool, input->root);
        } else if (strcmp(to_format, "mdx") == 0) {
            formatted_output = format_mdx(input->pool, input->root);
        } else if (strcmp(to_format, "markdown") == 0 || strcmp(to_format, "md") == 0) {
            formatted_output = format_markup_string(input->pool, input->root, &MARKDOWN_RULES);
        } else if (strcmp(to_format, "math-ascii") == 0) {
            formatted_output = format_math_ascii(input->pool, input->root);
        } else if (strcmp(to_format, "math-latex") == 0) {
            formatted_output = format_math_latex(input->pool, input->root);
        } else if (strcmp(to_format, "math-typst") == 0) {
            formatted_output = format_math_typst(input->pool, input->root);
        } else if (strcmp(to_format, "math-mathml") == 0) {
            formatted_output = format_math_mathml(input->pool, input->root);
        } else if (strcmp(to_format, "mark") == 0) {
            // Use print_root_item to format as mark representation
            StrBuf* sb = strbuf_new_cap(1024);
            print_root_item(sb, input->root);
            formatted_output = create_string(input->pool, sb->str);
            strbuf_free(sb);
        } else {
            printf("Error: Unsupported output format '%s'\n", to_format);
            printf("Supported formats: mark, json, xml, html, yaml, toml, ini, css, jsx, mdx, latex, rst, org, wiki, textile, text, markdown, math-ascii, math-latex, math-typst, math-mathml\n");
            pool_destroy(temp_pool);
            return 1;
        }

        if (!formatted_output && !full_doc_output) {
            printf("Error: Failed to format output\n");
            pool_destroy(temp_pool);
            return 1;
        }

        // Step 3: Write the output to file
        printf("Writing output to: %s\n", output_file);
        if (full_doc_output) {
            write_text_file(output_file, full_doc_output->str);
            strbuf_free(full_doc_output);
        } else {
            write_text_file(output_file, formatted_output->chars);
        }

    printf("Conversion completed successfully!\n");
    printf("Input:  %s\n", input_file);
    printf("Output: %s\n", output_file);
    printf("Format: %s → %s\n", from_format ? from_format : "auto-detected", to_format);

    // Cleanup
    pool_destroy(temp_pool);
    return 0;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Set console to UTF-8 for proper Unicode display on Windows
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // Store command line args for sys.proc.self.args access
    sysinfo_set_args(argc, argv);

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

    // Initialize memory tracker early in program lifecycle
    log_debug("initializing memory tracker");
    // Check environment variable for debug mode
    const char* memtrack_env = getenv("MEMTRACK_MODE");
    MemtrackMode mode = MEMTRACK_MODE_STATS;  // Default to stats mode
    if (memtrack_env && strcmp(memtrack_env, "DEBUG") == 0) {
        mode = MEMTRACK_MODE_DEBUG;
        log_debug("memory tracker in DEBUG mode");
    } else if (memtrack_env && strcmp(memtrack_env, "OFF") == 0) {
        mode = MEMTRACK_MODE_OFF;
    }
    memtrack_init(mode);
    atexit(memtrack_shutdown);  // Ensure shutdown is called on exit
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
            printf("Usage: %s validate [-s <schema>] [-f <format>] [options] <file> [files...]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  -s <schema>       Schema file (required for some formats)\n");
            printf("  -f <format>       Input format (auto-detect, json, xml, html, md, yaml, csv, ini, toml, etc.)\n");
            printf("  --strict          Enable strict mode (all optional fields must be present or null)\n");
            printf("  --max-errors N    Stop validation after N errors (default: 100)\n");
            printf("  --max-depth N     Maximum validation depth for nested structures (default: 100)\n");
            printf("  --allow-unknown   Allow fields not defined in schema\n");
            printf("  -h, --help        Show this help message\n");
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
            printf("  %s validate --strict --max-errors 5 document.json\n", argv[0]);
            printf("  %s validate --max-depth 50 --allow-unknown data.xml\n", argv[0]);
            log_finish();  // Cleanup logging before exit
            return 0;
        }

        // Prepare arguments for exec_validation (skip the "validate" command)
        int validation_argc = argc - 1;  // Remove the program name
        char** validation_argv = argv + 1;  // Skip the program name, start from "validate"

        // Call the extracted validation function
        AstValidationResult* validation_result = exec_validation(validation_argc, validation_argv);

        // Convert ValidationResult to exit code
        int exit_code = 1; // Default to failure
        if (validation_result) {
            exit_code = validation_result->valid ? 0 : 1;
        }

        log_debug("exec_validation completed with result: %d", exit_code);
        log_finish();  // Cleanup logging before exit
        return exit_code;
    }

    // Handle JavaScript command
    log_debug("Checking for js command");
    if (argc >= 2 && strcmp(argv[1], "js") == 0) {
        log_debug("Entering JavaScript command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda JavaScript Transpiler v1.0\n\n");
            printf("Usage: %s js [file.js]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'js' command runs the JavaScript transpiler.\n");
            printf("  If no file is provided, it runs built-in test cases.\n");
            printf("  If a file is provided, it transpiles and executes the JavaScript.\n");
            printf("\nOptions:\n");
            printf("  -h, --help     Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s js                             # Run built-in tests\n", argv[0]);
            printf("  %s js test.js                     # Transpile and run test.js\n", argv[0]);
            log_finish();
            return 0;
        }

        Runtime runtime;
        runtime_init(&runtime);

        if (argc >= 3) {
            // Test specific JavaScript file
            const char* js_file = argv[2];
            char* js_source = read_text_file(js_file);
            if (!js_source) {
                printf("Error: Could not read file '%s'\n", js_file);
                runtime_cleanup(&runtime);
                log_finish();
                return 1;
            }

            Item result = transpile_js_to_c(&runtime, js_source, js_file);

            // printf("##### Script '%s' executed: #####\n", js_file);
            StrBuf *output = strbuf_new_cap(256);
            print_root_item(output, result);
            printf("%s\n", output->str);
            strbuf_free(output);

            free(js_source);
        }

        runtime_cleanup(&runtime);
        log_finish();
        return 0;
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
            printf("Supported Formats:\n");
            printf("  Text formats:    markdown, html, xml, json, yaml, toml, ini, csv, latex, rst, org, text\n");
            printf("  Math formats:    math-ascii, math-latex, math-typst, math-mathml\n");
            printf("  Document formats: pdf, rtf\n");
            printf("  Markup formats:  asciidoc, textile, wiki, man, mark\n");
            printf("  Data formats:    json, xml, yaml, csv, ini, properties, toml\n");
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

    // Handle layout command
    log_debug("Checking for layout command");
    if (argc >= 2 && strcmp(argv[1], "layout") == 0) {
        log_debug("Entering layout command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda HTML/CSS Layout Engine v2.0 (Lambda CSS)\n\n");
            printf("Usage: %s layout <file.html|file.tex|file.ls> [more files...] [options]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'layout' command performs HTML/CSS layout analysis using Lambda's\n");
            printf("  CSS system (separate from Lexbor-based layout). It parses HTML with\n");
            printf("  Lambda parser, applies CSS using Lambda CSS engine, and outputs layout.\n");
            printf("  For LaTeX files (.tex/.latex), it parses LaTeX, converts to HTML, then layouts.\n");
            printf("  For Lambda scripts (.ls), it evaluates the script, wraps the result in HTML, then layouts.\n");
            printf("\nSupported Formats:\n");
            printf("  .html, .htm    HTML documents\n");
            printf("  .tex, .latex   LaTeX documents (converted to HTML)\n");
            printf("  .ls            Lambda scripts (evaluated and rendered)\n");
            printf("\nOptions:\n");
            printf("  -o, --output FILE                  Output file for layout results (default: stdout)\n");
            printf("  --output-dir DIR                   Output directory for batch mode (required for multiple files)\n");
            printf("  --view-output FILE                 Custom output path for view_tree.json (single file mode)\n");
            printf("  -c, --css FILE                     External CSS file to apply (HTML only)\n");
            printf("  -vw, --viewport-width WIDTH        Viewport width in pixels (default: 1200)\n");
            printf("  -vh, --viewport-height HEIGHT      Viewport height in pixels (default: 800)\n");
            printf("  --flavor FLAVOR                    LaTeX rendering: latex-js (default), tex-proper\n");
            printf("  --continue-on-error                Continue processing on errors in batch mode\n");
            printf("  --summary                          Print summary statistics\n");
            printf("  --debug                            Enable debug output\n");
            printf("  --help                             Show this help message\n");
            printf("\nSingle File Examples:\n");
            printf("  %s layout index.html                   # Basic HTML layout\n", argv[0]);
            printf("  %s layout document.tex                 # Layout LaTeX document\n", argv[0]);
            printf("  %s layout script.ls                    # Layout Lambda script output\n", argv[0]);
            printf("  %s layout test.html --debug            # With debug output\n", argv[0]);
            printf("  %s layout page.html -c styles.css      # With external CSS\n", argv[0]);
            printf("  %s layout doc.html -vw 1024 -vh 768    # Custom viewport\n", argv[0]);
            printf("  %s layout index.html -o layout.json    # Save to file\n", argv[0]);
            printf("\nBatch Mode Examples:\n");
            printf("  %s layout *.html --output-dir /tmp/results/\n", argv[0]);
            printf("  %s layout test/layout/data/baseline/*.html --output-dir /tmp/layout/ --summary\n", argv[0]);
            printf("  %s layout file1.html file2.html --output-dir ./out --continue-on-error\n", argv[0]);
            printf("\nBatch Mode Notes:\n");
            printf("  - Multiple input files require --output-dir\n");
            printf("  - Output files are named {basename}.json in the output directory\n");
            printf("  - UiContext is initialized once and reused for all files (10x+ speedup)\n");
            log_finish();  // Cleanup logging before exit
            return 0;
        }

        // Call the new Lambda CSS-based layout command
        // Pass argc-2 and argv+2 to skip both "./lambda.exe" and "layout" arguments
        int exit_code = cmd_layout(argc - 2, argv + 2);

        log_debug("layout command completed with result: %d", exit_code);
        log_finish();  // Cleanup logging before exit
        return exit_code;
    }

    // Handle math command - quick testing of single math formulas
    log_debug("Checking for math command");
    if (argc >= 2 && strcmp(argv[1], "math") == 0) {
        log_debug("Entering math command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Math Formula Renderer v1.0\n\n");
            printf("Usage: %s math \"<formula>\" [options]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  Quickly test and debug individual LaTeX math formulas without creating\n");
            printf("  a full .tex file. Useful for development and debugging the math typesetter.\n");
            printf("\nOutput Options (can be combined):\n");
            printf("  -o, --output FILE    Default output file (DVI unless --html specified)\n");
            printf("  --output-ast FILE    Output AST as JSON file\n");
            printf("  --output-html FILE   Output HTML to file\n");
            printf("  --output-dvi FILE    Output DVI to file\n");
            printf("  --html               Use HTML as default format for -o\n");
            printf("  --standalone         Include full HTML document with CSS (for HTML output)\n");
            printf("\nDebug Options:\n");
            printf("  --dump-ast           Dump Math AST to stderr (Phase A output)\n");
            printf("  --dump-boxes         Dump TexNode box structure to stderr (Phase B output)\n");
            printf("  -h, --help           Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s math \"\\\\frac{a}{b}\"                  # Simple fraction to DVI\n", argv[0]);
            printf("  %s math \"\\\\frac{a}{b}\" --html           # Output HTML snippet\n", argv[0]);
            printf("  %s math \"\\\\frac{a}{b}\" --html --standalone -o out.html  # Full HTML\n", argv[0]);
            printf("  %s math \"\\\\sum_{i=1}^n x_i\" --dump-ast  # Show AST structure\n", argv[0]);
            printf("  %s math \"\\\\sqrt{x^2+y^2}\" --dump-boxes  # Show box layout\n", argv[0]);
            printf("  %s math \"\\\\int_0^1 f(x) dx\" -o out.dvi  # Custom output file\n", argv[0]);
            printf("\nMulti-Output Mode (for testing framework):\n");
            printf("  %s math \"\\\\frac{a}{b}\" \\\n", argv[0]);
            printf("      --output-ast out.json \\\n");
            printf("      --output-html out.html \\\n");
            printf("      --output-dvi out.dvi\n");
            printf("\nNotes:\n");
            printf("  - Formulas are rendered in display math style\n");
            printf("  - Use double backslashes (\\\\\\\\) on command line for LaTeX commands\n");
            printf("  - Check log.txt for detailed [MATH] tracing output\n");
            log_finish();
            return 0;
        }

        // Parse arguments
        const char* formula = NULL;
        const char* output_file = NULL;
        const char* ast_file = NULL;      // explicit AST output
        const char* html_file = NULL;     // explicit HTML output
        const char* dvi_file = NULL;      // explicit DVI output
        bool dump_ast = false;
        bool dump_boxes = false;
        bool output_html = false;
        bool standalone_html = false;
        bool formula_taken = false;  // Track if we've taken the formula argument

        for (int i = 2; i < argc; i++) {
            // If we haven't taken the formula yet, first non-option arg is the formula
            // (even if it starts with - like "-\bbox{...}")
            if (!formula_taken && formula == NULL &&
                strcmp(argv[i], "-o") != 0 && strcmp(argv[i], "--output") != 0 &&
                strcmp(argv[i], "--output-ast") != 0 && strcmp(argv[i], "--output-html") != 0 &&
                strcmp(argv[i], "--output-dvi") != 0 && strcmp(argv[i], "--html") != 0 &&
                strcmp(argv[i], "--standalone") != 0 && strcmp(argv[i], "--dump-ast") != 0 &&
                strcmp(argv[i], "--dump-boxes") != 0) {
                formula = argv[i];
                formula_taken = true;
                continue;
            }
            if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
                if (i + 1 < argc) {
                    output_file = argv[++i];
                } else {
                    printf("Error: -o option requires an output file argument\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "--output-ast") == 0) {
                if (i + 1 < argc) {
                    ast_file = argv[++i];
                } else {
                    printf("Error: --output-ast option requires a file argument\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "--output-html") == 0) {
                if (i + 1 < argc) {
                    html_file = argv[++i];
                } else {
                    printf("Error: --output-html option requires a file argument\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "--output-dvi") == 0) {
                if (i + 1 < argc) {
                    dvi_file = argv[++i];
                } else {
                    printf("Error: --output-dvi option requires a file argument\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "--html") == 0) {
                output_html = true;
            } else if (strcmp(argv[i], "--standalone") == 0) {
                standalone_html = true;
            } else if (strcmp(argv[i], "--dump-ast") == 0) {
                dump_ast = true;
            } else if (strcmp(argv[i], "--dump-boxes") == 0) {
                dump_boxes = true;
            } else {
                // Unknown option - but formula should already be taken
                printf("Error: Unknown option '%s'\n", argv[i]);
                log_finish();
                return 1;
            }
        }

        if (!formula) {
            printf("Error: No math formula provided\n");
            printf("Usage: %s math \"<formula>\" [options]\n", argv[0]);
            printf("Try '%s math --help' for more information.\n", argv[0]);
            log_finish();
            return 1;
        }

        int exit_code = 0;

        // Check if explicit output options are used (multi-output mode)
        bool explicit_outputs = (ast_file || html_file || dvi_file);

        if (explicit_outputs) {
            // Multi-output mode: generate only the explicitly requested outputs
            // 1. AST output
            if (ast_file) {
                exit_code = render_math_to_ast_json(formula, ast_file);
                if (exit_code != 0) {
                    fprintf(stderr, "Error: Failed to generate AST JSON\n");
                    log_finish();
                    return exit_code;
                }
            }

            // 2. HTML output
            if (html_file) {
                exit_code = render_math_to_html(formula, html_file, standalone_html);
                if (exit_code != 0) {
                    fprintf(stderr, "Error: Failed to generate HTML\n");
                    log_finish();
                    return exit_code;
                }
            }

            // 3. DVI output
            if (dvi_file) {
                exit_code = render_math_to_dvi(formula, dvi_file, false, false);
                if (exit_code != 0) {
                    fprintf(stderr, "Error: Failed to generate DVI\n");
                    log_finish();
                    return exit_code;
                }
            }
        } else if (output_html) {
            // Legacy HTML output mode
            const char* html_out = output_file ? output_file : "/tmp/lambda_math.html";
            exit_code = render_math_to_html(formula, html_out, standalone_html);
            if (exit_code == 0) {
                fprintf(stderr, "Math formula rendered to: %s\n", html_out);
            }
        } else {
            // Legacy DVI output mode (original behavior)
            const char* default_dvi = "/tmp/lambda_math.dvi";

            // If only dumping and no output file specified, pass NULL
            const char* dvi_out = (dump_ast || dump_boxes) && !output_file
                                  ? NULL : (output_file ? output_file : default_dvi);

            // If neither dump option and default output, still write DVI
            if (!dump_ast && !dump_boxes && !output_file) {
                dvi_out = default_dvi;
            }

            exit_code = render_math_to_dvi(formula, dvi_out, dump_ast, dump_boxes);
        }

        log_debug("math command completed with result: %d", exit_code);
        log_finish();
        return exit_code;
    }

    // Handle render command
    log_debug("Checking for render command");
    if (argc >= 2 && strcmp(argv[1], "render") == 0) {
        log_debug("Entering render command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda HTML Renderer v1.0\n\n");
            printf("Usage: %s render <input.html|input.tex|input.ls> -o <output.svg|output.pdf|output.png|output.jpg|output.dvi> [options]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'render' command layouts an HTML, LaTeX, or Lambda script file and renders the result as SVG, PDF, PNG, JPEG, or DVI.\n");
            printf("  It parses the input (converting LaTeX to HTML or evaluating Lambda script if needed), applies CSS styles,\n");
            printf("  calculates layout, and generates output in the specified format based on file extension.\n");
            printf("\nSupported Input Formats:\n");
            printf("  .html, .htm    HTML documents\n");
            printf("  .tex, .latex   LaTeX documents (converted to HTML for svg/pdf/png/jpg, or TeX typeset for dvi)\n");
            printf("  .ls            Lambda scripts (evaluated and rendered)\n");
            printf("  .mmd           Mermaid diagrams (rendered via graph layout)\n");
            printf("  .d2            D2 diagrams (rendered via graph layout)\n");
            printf("  .dot, .gv      GraphViz DOT files (rendered via graph layout)\n");
            printf("\nSupported Output Formats:\n");
            printf("  .svg    Scalable Vector Graphics (SVG)\n");
            printf("  .pdf    Portable Document Format (PDF)\n");
            printf("  .png    Portable Network Graphics (PNG)\n");
            printf("  .jpg    Joint Photographic Experts Group (JPEG)\n");
            printf("  .jpeg   Joint Photographic Experts Group (JPEG)\n");
            printf("  .dvi    DeVice Independent format (TeX output, LaTeX files only)\n");
            printf("\nOptions:\n");
            printf("  -o <output>              Output file path (required, format detected by extension)\n");
            printf("  -vw, --viewport-width    Viewport width in CSS pixels (default: auto-size to content)\n");
            printf("  -vh, --viewport-height   Viewport height in CSS pixels (default: auto-size to content)\n");
            printf("  -s, --scale              User zoom scale factor (default: 1.0)\n");
            printf("  --pixel-ratio            Device pixel ratio for HiDPI/Retina (default: 1.0, use 2.0 for crisp text)\n");
            printf("  --flavor <flavor>        LaTeX rendering pipeline: latex-js (default), tex-proper\n");
            printf("  --theme <name>           Color theme for graph diagrams (default: zinc-dark)\n");
            printf("                           Dark: tokyo-night, nord, dracula, catppuccin-mocha, one-dark, github-dark\n");
            printf("                           Light: github-light, solarized-light, catppuccin-latte, zinc-light\n");
            printf("  -h, --help               Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s render index.html -o output.svg        # Auto-size to content\n", argv[0]);
            printf("  %s render script.ls -o output.pdf         # Render Lambda script result\n", argv[0]);
            printf("  %s render index.html -o output.pdf        # Auto-size to content\n", argv[0]);
            printf("  %s render index.html -o output.png        # Auto-size to content\n", argv[0]);
            printf("  %s render index.html -o output.jpg        # Auto-size to content\n", argv[0]);
            printf("  %s render paper.tex -o output.dvi         # LaTeX to DVI (TeX typesetting)\n", argv[0]);
            printf("  %s render index.html -o out.svg -vw 800 -vh 600  # Custom viewport size\n", argv[0]);
            printf("  %s render diagram.mmd -o out.svg --theme tokyo-night  # Graph with theme\n", argv[0]);
            printf("  %s render index.html -o out.png -s 2.0           # Render at 2x zoom\n", argv[0]);
            printf("  %s render index.html -o out.png --pixel-ratio 2  # Crisp text on Retina\n", argv[0]);
            printf("  %s render test/page.html -o result.svg           # Render with relative paths\n", argv[0]);
            log_finish();  // Cleanup logging before exit
            return 0;
        }

        // Parse arguments
        const char* html_file = NULL;
        const char* output_file = NULL;
        int viewport_width = 0;   // 0 means use format-specific default
        int viewport_height = 0;  // 0 means use format-specific default
        float render_scale = 1.0f;  // Default user zoom scale
        float pixel_ratio = 1.0f;  // Default device pixel ratio (use 2.0 for Retina)
        const char* latex_flavor_str = NULL;  // LaTeX rendering flavor
        const char* theme_name = NULL;  // Graph theme name

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
                if (i + 1 < argc) {
                    output_file = argv[++i];
                } else {
                    printf("Error: -o option requires an output file argument\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "-vw") == 0 || strcmp(argv[i], "--viewport-width") == 0) {
                if (i + 1 < argc) {
                    i++;
                    viewport_width = (int)str_to_int64_default(argv[i], strlen(argv[i]), 0);
                    if (viewport_width <= 0) {
                        printf("Error: Invalid viewport width '%s'. Must be a positive integer.\n", argv[i]);
                        log_finish();
                        return 1;
                    }
                } else {
                    printf("Error: -vw option requires a width value\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "-vh") == 0 || strcmp(argv[i], "--viewport-height") == 0) {
                if (i + 1 < argc) {
                    i++;
                    viewport_height = (int)str_to_int64_default(argv[i], strlen(argv[i]), 0);
                    if (viewport_height <= 0) {
                        printf("Error: Invalid viewport height '%s'. Must be a positive integer.\n", argv[i]);
                        log_finish();
                        return 1;
                    }
                } else {
                    printf("Error: -vh option requires a height value\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scale") == 0) {
                if (i + 1 < argc) {
                    i++;
                    render_scale = (float)str_to_double_default(argv[i], strlen(argv[i]), 0.0);
                    if (render_scale <= 0.0f) {
                        printf("Error: Invalid scale '%s'. Must be a positive number.\n", argv[i]);
                        log_finish();
                        return 1;
                    }
                } else {
                    printf("Error: -s/--scale option requires a scale value\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "--pixel-ratio") == 0) {
                if (i + 1 < argc) {
                    i++;
                    pixel_ratio = (float)str_to_double_default(argv[i], strlen(argv[i]), 0.0);
                    if (pixel_ratio <= 0.0f) {
                        printf("Error: Invalid pixel-ratio '%s'. Must be a positive number.\n", argv[i]);
                        log_finish();
                        return 1;
                    }
                } else {
                    printf("Error: --pixel-ratio option requires a value\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "--flavor") == 0) {
                if (i + 1 < argc) {
                    latex_flavor_str = argv[++i];
                    if (strcmp(latex_flavor_str, "latex-js") != 0 && strcmp(latex_flavor_str, "tex-proper") != 0) {
                        printf("Error: unknown flavor '%s'. Use 'latex-js' or 'tex-proper'\n", latex_flavor_str);
                        log_finish();
                        return 1;
                    }
                } else {
                    printf("Error: --flavor option requires an argument\n");
                    log_finish();
                    return 1;
                }
            } else if (strcmp(argv[i], "--theme") == 0 || strcmp(argv[i], "-t") == 0) {
                if (i + 1 < argc) {
                    theme_name = argv[++i];
                } else {
                    printf("Error: --theme option requires a theme name\n");
                    printf("Available themes: tokyo-night, nord, dracula, catppuccin-mocha, one-dark,\n");
                    printf("                  github-dark, github-light, solarized-light, catppuccin-latte,\n");
                    printf("                  zinc-dark, zinc-light, dark, light\n");
                    log_finish();
                    return 1;
                }
            } else if (argv[i][0] != '-') {
                // This should be the HTML input file
                if (html_file == NULL) {
                    html_file = argv[i];
                } else {
                    printf("Error: Multiple input files not supported\n");
                    log_finish();
                    return 1;
                }
            } else {
                printf("Error: Unknown render option '%s'\n", argv[i]);
                log_finish();
                return 1;
            }
        }

        // Validate required arguments
        if (!html_file) {
            printf("Error: render command requires an HTML input file\n");
            printf("Usage: %s render <input.html> -o <output.svg|output.pdf|output.png|output.jpg>\n", argv[0]);
            printf("Use '%s render --help' for more information\n", argv[0]);
            log_finish();
            return 1;
        }

        if (!output_file) {
            printf("Error: render command requires an output file (-o option)\n");
            printf("Usage: %s render <input.html> -o <output.svg|output.pdf|output.png|output.jpg>\n", argv[0]);
            printf("Use '%s render --help' for more information\n", argv[0]);
            log_finish();
            return 1;
        }

        // Check if HTML file exists (skip check for HTTP/HTTPS URLs)
        bool is_http_url = (strncmp(html_file, "http://", 7) == 0 || strncmp(html_file, "https://", 8) == 0);
        if (!is_http_url && access(html_file, F_OK) != 0) {
            printf("Error: Input file '%s' does not exist\n", html_file);
            log_finish();
            return 1;
        }

        // Detect if input is a graph format (Mermaid, D2, DOT)
        const char* input_ext = strrchr(html_file, '.');
        bool is_graph_input = false;
        if (input_ext) {
            if (strcmp(input_ext, ".mmd") == 0 ||
                strcmp(input_ext, ".d2") == 0 ||
                strcmp(input_ext, ".dot") == 0 ||
                strcmp(input_ext, ".gv") == 0) {
                is_graph_input = true;
            }
        }

        log_debug("Rendering input '%s' to output '%s' with viewport %dx%d, scale=%.2f, pixel_ratio=%.2f",
                  html_file, output_file, viewport_width, viewport_height, render_scale, pixel_ratio);

        // Handle graph inputs - convert to SVG first, then render if needed
        if (is_graph_input) {
            log_info("Detected graph input format");

            // Read graph file
            char* graph_content = read_text_file(html_file);
            if (!graph_content) {
                printf("Error: Failed to read graph file '%s'\n", html_file);
                log_finish();
                return 1;
            }

            // Create input using InputManager directly (no URL needed for graph parsing)
            Input* input = InputManager::create_input(nullptr);
            if (!input) {
                printf("Error: Failed to create input for graph parsing\n");
                free(graph_content);
                log_finish();
                return 1;
            }
            log_debug("Created input for graph parsing, parsing content...");

            // Parse graph content
            if (strcmp(input_ext, ".mmd") == 0) {
                log_debug("Parsing Mermaid graph");
                parse_graph_mermaid(input, graph_content);
            } else if (strcmp(input_ext, ".d2") == 0) {
                log_debug("Parsing D2 graph");
                parse_graph_d2(input, graph_content);
            } else if (strcmp(input_ext, ".dot") == 0 || strcmp(input_ext, ".gv") == 0) {
                log_debug("Parsing DOT graph");
                parse_graph_dot(input, graph_content);
            }
            free(graph_content);
            log_debug("Graph parsed, checking result...");

            if (get_type_id(input->root) != LMD_TYPE_ELEMENT) {
                printf("Error: Failed to parse graph file '%s'\n", html_file);
                log_finish();
                return 1;
            }

            // Layout graph using Dagre
            GraphLayout* layout = layout_graph(input->root.element);
            if (!layout) {
                printf("Error: Failed to compute graph layout\n");
                log_finish();
                return 1;
            }

            // Generate SVG from layout with optional theme
            Item svg_item;
            if (theme_name) {
                SvgGeneratorOptions* opts = create_themed_svg_options(theme_name);
                svg_item = graph_to_svg_with_options(input->root.element, layout, opts, input);
                free(opts);
                log_info("Using theme '%s' for graph rendering", theme_name);
            } else {
                svg_item = graph_to_svg(input->root.element, layout, input);
            }
            if (get_type_id(svg_item) != LMD_TYPE_ELEMENT) {
                printf("Error: Failed to generate SVG from graph\n");
                free_graph_layout(layout);
                log_finish();
                return 1;
            }

            // Update input root to SVG
            input->root = svg_item;

            // Determine output format
            const char* output_ext = strrchr(output_file, '.');
            if (output_ext && strcmp(output_ext, ".svg") == 0) {
                // Direct SVG output using format_xml (SVG is XML)
                log_info("Writing SVG output to '%s'", output_file);
                String* svg_str = format_xml(input->pool, input->root);
                if (svg_str) {
                    write_text_file(output_file, svg_str->chars);
                    printf("Graph rendered successfully to '%s'\n", output_file);
                } else {
                    printf("Error: Failed to format SVG output\n");
                }
                free_graph_layout(layout);
                log_finish();
                return svg_str ? 0 : 1;
            } else {
                // For other formats (PDF, PNG), save SVG temp file and render it
                const char* temp_svg = "/tmp/lambda_graph_temp.svg";
                String* svg_str = format_xml(input->pool, input->root);
                if (!svg_str) {
                    printf("Error: Failed to format SVG output\n");
                    free_graph_layout(layout);
                    log_finish();
                    return 1;
                }
                write_text_file(temp_svg, svg_str->chars);

                // Render the SVG using the appropriate renderer
                int exit_code = 0;
                if (output_ext && strcmp(output_ext, ".pdf") == 0) {
                    exit_code = render_html_to_pdf(temp_svg, output_file, 0, 0, render_scale);
                } else if (output_ext && strcmp(output_ext, ".png") == 0) {
                    exit_code = render_html_to_png(temp_svg, output_file, 0, 0, render_scale, pixel_ratio);
                } else if (output_ext && (strcmp(output_ext, ".jpg") == 0 || strcmp(output_ext, ".jpeg") == 0)) {
                    exit_code = render_html_to_jpeg(temp_svg, output_file, 85, 0, 0, render_scale, pixel_ratio);
                } else {
                    printf("Error: Unsupported output format for graph rendering: %s\n", output_ext);
                    exit_code = 1;
                }

                // Clean up temp file
                unlink(temp_svg);

                if (exit_code == 0) {
                    printf("Graph rendered successfully to '%s'\n", output_file);
                }

                free_graph_layout(layout);
                log_finish();
                return exit_code;
            }
        }

        // Set environment variable for LaTeX flavor (used by load_html_doc and load_doc_by_format)
        if (latex_flavor_str) {
            if (strcmp(latex_flavor_str, "tex-proper") == 0) {
                setenv("LAMBDA_TEX_PIPELINE", "1", 1);
                log_info("LaTeX flavor set to tex-proper");
            } else {
                setenv("LAMBDA_TEX_PIPELINE", "0", 1);
                log_info("LaTeX flavor set to latex-js");
            }
        }

        // Determine output format based on file extension
        const char* ext = strrchr(output_file, '.');
        int exit_code;

        if (ext && strcmp(ext, ".dvi") == 0) {
            // DVI output - only for LaTeX files, uses TeX typesetting pipeline
            log_debug("Detected DVI output format");

            // Check if input is LaTeX
            const char* input_ext = strrchr(html_file, '.');
            if (!input_ext || (strcmp(input_ext, ".tex") != 0 && strcmp(input_ext, ".latex") != 0)) {
                printf("Error: DVI output is only supported for LaTeX input files (.tex, .latex)\n");
                printf("Input file: %s\n", html_file);
                log_finish();
                return 1;
            }

            exit_code = render_latex_to_dvi(html_file, output_file);
        } else if (ext && strcmp(ext, ".pdf") == 0) {
            // Call the PDF rendering function (pass 0 for auto-sizing)
            log_debug("Detected PDF output format");
            int pdf_width = viewport_width;   // 0 means auto-size
            int pdf_height = viewport_height; // 0 means auto-size
            exit_code = render_html_to_pdf(html_file, output_file, pdf_width, pdf_height, render_scale);
        } else if (ext && strcmp(ext, ".svg") == 0) {
            // Call the SVG rendering function (pass 0 for auto-sizing)
            log_debug("Detected SVG output format");
            int svg_width = viewport_width;   // 0 means auto-size
            int svg_height = viewport_height; // 0 means auto-size
            exit_code = render_html_to_svg(html_file, output_file, svg_width, svg_height, render_scale);
        } else if (ext && strcmp(ext, ".png") == 0) {
            // Call the PNG rendering function (pass 0 for auto-sizing)
            log_debug("Detected PNG output format");
            int png_width = viewport_width;   // 0 means auto-size
            int png_height = viewport_height; // 0 means auto-size
            exit_code = render_html_to_png(html_file, output_file, png_width, png_height, render_scale, pixel_ratio);
        } else if (ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)) {
            // Call the JPEG rendering function with default quality of 85 (pass 0 for auto-sizing)
            log_debug("Detected JPEG output format");
            int jpeg_width = viewport_width;   // 0 means auto-size
            int jpeg_height = viewport_height; // 0 means auto-size
            exit_code = render_html_to_jpeg(html_file, output_file, 85, jpeg_width, jpeg_height, render_scale, pixel_ratio);
        } else {
            printf("Error: Unsupported output format. Use .svg, .pdf, .png, .jpg, or .jpeg extension\n");
            printf("Supported formats: .svg (SVG), .pdf (PDF), .png (PNG), .jpg/.jpeg (JPEG)\n");
            log_finish();
            return 1;
        }

        log_debug("render completed with result: %d", exit_code);
        log_finish();  // Cleanup logging before exit
        return exit_code;
    }


    // Handle view command (open PDF or HTML in window)
    log_debug("Checking for view command");
    if (argc >= 2 && strcmp(argv[1], "view") == 0) {
        log_debug("Entering view command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Document Viewer v1.0\n\n");
            printf("Usage: %s view [document_file]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'view' command opens a document in an interactive window.\n");
            printf("  Supports multiple document formats with full rendering and styling.\n");
            printf("  If no file is specified, opens test/html/index.html by default.\n");
            printf("\nSupported Formats:\n");
            printf("  .pdf       Portable Document Format\n");
            printf("  .html      HyperText Markup Language\n");
            printf("  .htm       HyperText Markup Language (alternative extension)\n");
            printf("  .md        Markdown (with GitHub-like styling)\n");
            printf("  .markdown  Markdown (with GitHub-like styling)\n");
            printf("  .tex       LaTeX (converted to HTML)\n");
            printf("  .latex     LaTeX (converted to HTML)\n");
            printf("  .ls        Lambda script (evaluated and rendered)\n");
            printf("  .xml       Extensible Markup Language (CSS styled or source view)\n");
            printf("  .rst       reStructuredText (planned support)\n");
            printf("  .svg       Scalable Vector Graphics\n");
            printf("  .mmd       Mermaid diagram (graph layout)\n");
            printf("  .d2        D2 diagram (graph layout)\n");
            printf("  .dot/.gv   Graphviz DOT diagram (graph layout)\n");
            printf("  .png       Portable Network Graphics\n");
            printf("  .jpg/.jpeg JPEG Image\n");
            printf("  .gif       Graphics Interchange Format\n");
            printf("  .json      JSON (source view)\n");
            printf("  .yaml/.yml YAML (source view)\n");
            printf("  .toml      TOML (source view)\n");
            printf("  .txt       Plain text\n");
            printf("  .csv       Comma-separated values (source view)\n");
            printf("\nOptions:\n");
            printf("  --event-file <file.json>   Load simulated events from JSON file for testing\n");
            printf("  --flavor <flavor>          LaTeX rendering pipeline: latex-js (default), tex-proper\n");
            printf("\nExamples:\n");
            printf("  %s view                          # View default HTML (test/html/index.html)\n", argv[0]);
            printf("  %s view document.pdf             # View PDF in window\n", argv[0]);
            printf("  %s view page.html                # View HTML document\n", argv[0]);
            printf("  %s view README.md                # View markdown with GitHub styling\n", argv[0]);
            printf("  %s view script.ls                # View Lambda script result\n", argv[0]);
            printf("  %s view paper.tex                # View LaTeX document\n", argv[0]);
            printf("  %s view paper.tex --flavor tex-proper  # View with TeX typesetting\n", argv[0]);
            printf("  %s view config.xml               # View XML document\n", argv[0]);
            printf("  %s view data.json                # View JSON source\n", argv[0]);
            printf("  %s view flowchart.mmd            # View Mermaid diagram\n", argv[0]);
            printf("  %s view architecture.d2          # View D2 diagram\n", argv[0]);
            printf("  %s view test/input/test.pdf     # View PDF with path\n", argv[0]);
            printf("  %s view page.html --event-file events.json  # Automated testing\n", argv[0]);
            printf("\nKeyboard Controls:\n");
            printf("  ESC        Close window\n");
            printf("  Q          Quit viewer\n");
            log_finish();
            return 0;
        }

        // Parse arguments for view command
        const char* filename = NULL;
        const char* event_file = NULL;
        int latex_flavor = LATEX_FLAVOR_AUTO_M;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--event-file") == 0 && i + 1 < argc) {
                event_file = argv[++i];
            } else if (strcmp(argv[i], "--flavor") == 0 && i + 1 < argc) {
                const char* flavor = argv[++i];
                if (strcmp(flavor, "latex-js") == 0) {
                    latex_flavor = LATEX_FLAVOR_JS_M;
                } else if (strcmp(flavor, "tex-proper") == 0) {
                    latex_flavor = LATEX_FLAVOR_TEX_PROPER_M;
                } else {
                    printf("Error: unknown flavor '%s'. Use 'latex-js' or 'tex-proper'\n", flavor);
                    log_finish();
                    return 1;
                }
            } else if (argv[i][0] != '-' && filename == NULL) {
                filename = argv[i];
            }
        }

        // Default to test/html/index.html if no file specified (like radiant.exe)
        if (filename == NULL) {
            filename = "test/html/index.html";
            log_info("No file specified, using default: %s", filename);
        }

        // Check if file exists (skip check for HTTP/HTTPS URLs)
        bool is_http_url = (strncmp(filename, "http://", 7) == 0 || strncmp(filename, "https://", 8) == 0);
        if (!is_http_url && access(filename, F_OK) != 0) {
            printf("Error: File '%s' does not exist\n", filename);
            log_finish();
            return 1;
        }

        // For HTTP URLs, fetch content and determine type from Content-Type header
        const char* effective_ext = nullptr;
        char* temp_file_path = nullptr;
        const char* original_url = nullptr;  // preserve original URL for base tag injection
        if (is_http_url) {
            original_url = filename;  // save original URL before we change filename
            log_info("Fetching URL: %s", filename);
            FetchResponse* response = http_fetch(filename, nullptr);
            if (!response || !response->data || response->status_code >= 400) {
                printf("Error: Failed to fetch URL '%s'", filename);
                if (response && response->status_code >= 400) {
                    printf(" (HTTP %ld)", response->status_code);
                }
                printf("\n");
                if (response) free_fetch_response(response);
                log_finish();
                return 1;
            }

            // Get file extension from Content-Type
            effective_ext = content_type_to_extension(response->content_type);
            log_info("HTTP Content-Type: %s -> extension: %s",
                     response->content_type ? response->content_type : "(none)",
                     effective_ext ? effective_ext : "(none)");

            // Write to temp file with appropriate extension
            char temp_path[256];
            snprintf(temp_path, sizeof(temp_path), "/tmp/lambda_view_http%s", effective_ext ? effective_ext : ".html");
            FILE* f = fopen(temp_path, "wb");
            if (f) {
                // For HTML content, inject a <base> tag to preserve the original URL context
                // This ensures relative URLs (CSS, images, etc.) resolve correctly
                if (effective_ext && strcmp(effective_ext, ".html") == 0) {
                    // Find where to inject the base tag (after <head> or at start of content)
                    const char* head_tag = strcasestr(response->data, "<head");
                    const char* html_tag = strcasestr(response->data, "<html");

                    if (head_tag) {
                        // Find the end of the <head> tag
                        const char* head_end = strchr(head_tag, '>');
                        if (head_end) {
                            head_end++; // Move past the '>'
                            // Write content before insertion point
                            fwrite(response->data, 1, head_end - response->data, f);
                            // Inject base tag
                            fprintf(f, "\n<base href=\"%s\">\n", original_url);
                            // Write rest of content
                            fwrite(head_end, 1, response->size - (head_end - response->data), f);
                            log_info("Injected <base href=\"%s\"> into HTML", original_url);
                        } else {
                            fwrite(response->data, 1, response->size, f);
                        }
                    } else if (html_tag) {
                        // Find the end of the <html> tag and inject after it
                        const char* html_end = strchr(html_tag, '>');
                        if (html_end) {
                            html_end++;
                            fwrite(response->data, 1, html_end - response->data, f);
                            fprintf(f, "\n<head><base href=\"%s\"></head>\n", original_url);
                            fwrite(html_end, 1, response->size - (html_end - response->data), f);
                            log_info("Injected <head><base href=\"%s\"></head> into HTML", original_url);
                        } else {
                            fwrite(response->data, 1, response->size, f);
                        }
                    } else {
                        // No head or html tag, prepend base tag
                        fprintf(f, "<base href=\"%s\">\n", original_url);
                        fwrite(response->data, 1, response->size, f);
                        log_info("Prepended <base href=\"%s\"> to HTML", original_url);
                    }
                } else {
                    fwrite(response->data, 1, response->size, f);
                }
                fclose(f);
                temp_file_path = strdup(temp_path);
                filename = temp_file_path;
                log_debug("Saved HTTP content to: %s (%zu bytes)", temp_file_path, response->size);
            } else {
                printf("Error: Failed to create temp file for HTTP content\n");
                free_fetch_response(response);
                log_finish();
                return 1;
            }
            free_fetch_response(response);
        }

        // Detect file type by extension
        const char* ext = effective_ext ? effective_ext : strrchr(filename, '.');
        int exit_code;

        // Check if this is a graph file that needs conversion
        bool is_graph_file = ext && (strcmp(ext, ".mmd") == 0 ||
                                      strcmp(ext, ".d2") == 0 ||
                                      strcmp(ext, ".dot") == 0 ||
                                      strcmp(ext, ".gv") == 0);

        if (is_graph_file) {
            log_info("Detected graph file, converting to SVG for viewing");

            // Read graph file
            char* graph_content = read_text_file(filename);
            if (!graph_content) {
                printf("Error: Failed to read graph file '%s'\n", filename);
                log_finish();
                return 1;
            }

            // Create input for graph parsing
            Input* input = InputManager::create_input(nullptr);
            if (!input) {
                printf("Error: Failed to create input for graph parsing\n");
                free(graph_content);
                log_finish();
                return 1;
            }

            // Parse graph content based on format
            if (strcmp(ext, ".mmd") == 0) {
                log_debug("Parsing Mermaid graph");
                parse_graph_mermaid(input, graph_content);
            } else if (strcmp(ext, ".d2") == 0) {
                log_debug("Parsing D2 graph");
                parse_graph_d2(input, graph_content);
            } else if (strcmp(ext, ".dot") == 0 || strcmp(ext, ".gv") == 0) {
                log_debug("Parsing DOT graph");
                parse_graph_dot(input, graph_content);
            }
            free(graph_content);

            if (get_type_id(input->root) != LMD_TYPE_ELEMENT) {
                printf("Error: Failed to parse graph file '%s'\n", filename);
                log_finish();
                return 1;
            }

            // Layout graph using Dagre
            GraphLayout* layout = layout_graph(input->root.element);
            if (!layout) {
                printf("Error: Failed to compute graph layout\n");
                log_finish();
                return 1;
            }

            // Generate SVG from layout
            Item svg_item = graph_to_svg(input->root.element, layout, input);
            if (get_type_id(svg_item) != LMD_TYPE_ELEMENT) {
                printf("Error: Failed to generate SVG from graph\n");
                free_graph_layout(layout);
                log_finish();
                return 1;
            }

            // Format SVG and write to temp file
            String* svg_str = format_xml(input->pool, svg_item);
            if (!svg_str) {
                printf("Error: Failed to format SVG output\n");
                free_graph_layout(layout);
                log_finish();
                return 1;
            }

            const char* temp_svg = "/tmp/lambda_graph_view.svg";
            write_text_file(temp_svg, svg_str->chars);
            write_text_file("/tmp/lambda_graph_debug.svg", svg_str->chars);  // debug copy
            free_graph_layout(layout);

            // View the temp SVG file
            log_info("Opening graph SVG in viewer: %s", temp_svg);
            exit_code = view_doc_in_window_with_events(temp_svg, event_file, latex_flavor);

            // Clean up temp file after viewing
            unlink(temp_svg);

            log_debug("view command completed with result: %d", exit_code);
            log_finish();
            return exit_code;
        }

        if (ext && (strcmp(ext, ".pdf") == 0 ||
                    strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0 ||
                    strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0 ||
                    strcmp(ext, ".tex") == 0 || strcmp(ext, ".latex") == 0 ||
                    strcmp(ext, ".ls") == 0 ||
                    strcmp(ext, ".xml") == 0 || strcmp(ext, ".rst") == 0 ||
                    strcmp(ext, ".wiki") == 0 || strcmp(ext, ".svg") == 0 ||
                    strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                    strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".gif") == 0 ||
                    strcmp(ext, ".json") == 0 || strcmp(ext, ".yaml") == 0 ||
                    strcmp(ext, ".yml") == 0 || strcmp(ext, ".toml") == 0 ||
                    strcmp(ext, ".txt") == 0 || strcmp(ext, ".csv") == 0 ||
                    strcmp(ext, ".ini") == 0 || strcmp(ext, ".conf") == 0 ||
                    strcmp(ext, ".cfg") == 0 || strcmp(ext, ".log") == 0)) {
            // Use unified document viewer for all document types including PDF
            log_info("Opening document file: %s (event_file: %s, flavor: %d)", filename, event_file ? event_file : "none", latex_flavor);
            exit_code = view_doc_in_window_with_events(filename, event_file, latex_flavor);
        } else {
            printf("Error: Unsupported file format '%s'\n", ext ? ext : "(no extension)");
            printf("Supported formats: .pdf, .html, .md, .tex, .ls, .xml, .svg, .png, .jpg, .gif, .json, .yaml, .toml, .txt, .csv\n");
            if (temp_file_path) { unlink(temp_file_path); free(temp_file_path); }
            log_finish();
            return 1;
        }

        // Cleanup temp file if we created one from HTTP URL
        if (temp_file_path) {
            unlink(temp_file_path);
            free(temp_file_path);
        }

        log_debug("view command completed with result: %d", exit_code);
        log_finish();
        return exit_code;
    }

    // Handle webdriver command
    log_debug("Checking for webdriver command");
    if (argc >= 2 && strcmp(argv[1], "webdriver") == 0) {
        log_debug("Entering webdriver command handler");

        int exit_code = cmd_webdriver(argc - 2, argv + 2);

        log_debug("webdriver command completed with result: %d", exit_code);
        log_finish();
        return exit_code;
    }

    // Handle fetch command (network resource download)
    log_debug("Checking for fetch command");
    if (argc >= 2 && strcmp(argv[1], "fetch") == 0) {
        log_debug("Entering fetch command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Fetch - HTTP/HTTPS Resource Downloader\n\n");
            printf("Usage: %s fetch <url> [options]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  -o, --output <file>    Save output to file (default: stdout)\n");
            printf("  -t, --timeout <ms>     Request timeout in milliseconds (default: 30000)\n");
            printf("  -v, --verbose          Show detailed progress and timing\n");
            printf("  -h, --help             Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s fetch https://example.com                    # Fetch and print to stdout\n", argv[0]);
            printf("  %s fetch https://example.com -o page.html       # Save to file\n", argv[0]);
            printf("  %s fetch https://httpbin.org/delay/2 -t 5000    # 5 second timeout\n", argv[0]);
            printf("  %s fetch https://httpbin.org/status/200 -v      # Verbose output\n", argv[0]);
            log_finish();
            return 0;
        }

        // Parse fetch command arguments
        const char* url = NULL;
        const char* output_file = NULL;
        int timeout_ms = 30000;  // Default 30 seconds
        bool verbose = false;

        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
                output_file = argv[++i];
            } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) && i + 1 < argc) {
                i++;
                timeout_ms = (int)str_to_int64_default(argv[i], strlen(argv[i]), 0);
            } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
                verbose = true;
            } else if (argv[i][0] != '-') {
                if (url == NULL) {
                    url = argv[i];
                } else {
                    printf("Error: Multiple URLs not supported\n");
                    log_finish();
                    return 1;
                }
            } else {
                printf("Error: Unknown option '%s'\n", argv[i]);
                log_finish();
                return 1;
            }
        }

        if (!url) {
            printf("Error: No URL specified\n");
            printf("Usage: %s fetch <url> [options]\n", argv[0]);
            log_finish();
            return 1;
        }

        if (verbose) {
            printf("Fetching: %s\n", url);
            printf("Timeout: %d ms\n", timeout_ms);
        }

        // Create a NetworkResource for the download
        NetworkResource res = {0};
        res.url = strdup(url);
        res.timeout_ms = timeout_ms;
        res.state = STATE_PENDING;
        res.type = RESOURCE_HTML;  // Treat as generic content

        // Perform the download
        double start_time = (double)clock() / CLOCKS_PER_SEC;
        bool success = network_download_resource(&res);
        double end_time = (double)clock() / CLOCKS_PER_SEC;
        double elapsed_ms = (end_time - start_time) * 1000.0;

        if (success) {
            if (verbose) {
                printf("✅ Download successful\n");
                printf("   HTTP Status: %ld\n", res.http_status_code);
                printf("   Time: %.2f ms\n", elapsed_ms);
                if (res.local_path) {
                    printf("   Cached: %s\n", res.local_path);
                }
            }

            // Output content
            if (output_file) {
                if (res.local_path) {
                    // Copy from cache to output file
                    char* content = read_text_file(res.local_path);
                    if (content) {
                        write_text_file(output_file, content);
                        free(content);
                        if (verbose) {
                            printf("   Saved to: %s\n", output_file);
                        }
                    } else {
                        printf("Error: Failed to read cached content\n");
                        free(res.url);
                        log_finish();
                        return 1;
                    }
                } else {
                    printf("Error: No content available\n");
                    free(res.url);
                    log_finish();
                    return 1;
                }
            } else {
                // Print to stdout
                if (res.local_path) {
                    char* content = read_text_file(res.local_path);
                    if (content) {
                        printf("%s", content);
                        free(content);
                    }
                }
            }

            free(res.url);
            log_finish();
            return 0;
        } else {
            printf("❌ Download failed\n");
            printf("   URL: %s\n", url);
            printf("   HTTP Status: %ld\n", res.http_status_code);
            if (res.error_message) {
                printf("   Error: %s\n", res.error_message);
            }
            printf("   Retryable: %s\n", is_http_error_retryable(res.http_status_code) ? "yes" : "no");
            printf("   Time: %.2f ms\n", elapsed_ms);

            free(res.url);
            log_finish();
            return 1;
        }
    }

    // Handle run command
    log_debug("Checking for run command");
    if (argc >= 2 && strcmp(argv[1], "run") == 0) {
        log_debug("Entering run command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Script Runner v1.0\n\n");
            printf("Usage: %s run [--mir] <script>\n", argv[0]);
            printf("\nOptions:\n");
            printf("  --mir          Use MIR JIT compilation (default: tree-walking interpreter)\n");
            printf("  -h, --help     Show this help message\n");
            printf("\nDescription:\n");
            printf("  The 'run' command executes a Lambda script with run_main context enabled.\n");
            printf("  This means that if the script defines a main function, it will be\n");
            printf("  automatically executed during script execution.\n");
            printf("\nExamples:\n");
            printf("  %s run script.ls                 # Run script with tree-walking interpreter\n", argv[0]);
            printf("  %s run --mir script.ls           # Run script with MIR JIT compilation\n", argv[0]);
            log_finish();  // Cleanup logging before exit
            return 0;
        }

        // Parse run command arguments
        bool use_mir = false;
        char* script_file = NULL;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--mir") == 0) {
                use_mir = true;
            } else if (strcmp(argv[i], "--transpile-dir") == 0) {
                if (i + 1 < argc) {
                    runtime.transpile_dir = argv[++i];
                } else {
                    printf("Error: --transpile-dir requires a directory argument\n");
                    log_finish();
                    return 1;
                }
            } else if (argv[i][0] != '-') {
                if (script_file == NULL) {
                    script_file = argv[i];
                } else {
                    printf("Error: Multiple script files not supported\n");
                    log_finish();
                    return 1;
                }
            } else {
                printf("Error: Unknown run option '%s'\n", argv[i]);
                log_finish();
                return 1;
            }
        }

        if (!script_file) {
            printf("Error: run command requires a script file\n");
            printf("Usage: %s run [--mir] <script>\n", argv[0]);
            log_finish();
            return 1;
        }

        // Check if script file exists
        if (access(script_file, F_OK) != 0) {
            printf("Error: Script file '%s' does not exist\n", script_file);
            log_finish();
            return 1;
        }

        log_debug("Running script '%s' with run_main=true, use_mir=%s", script_file, use_mir ? "true" : "false");

        // Execute script with run_main enabled
        int result = run_script_file(&runtime, script_file, use_mir, false, true);  // true for run_main

        log_finish();
        return result;
    }

    bool use_mir = false;
    bool transpile_only = false;
    bool help_only = false;
    char* script_file = NULL;
    int max_errors = 0;  // 0 means use default (10)
    int optimize_level = -1;  // -1 means use default (2)

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
        else if (strcmp(argv[i], "--transpile-dir") == 0) {
            if (i + 1 < argc) {
                runtime.transpile_dir = argv[++i];
            } else {
                printf("Error: --transpile-dir requires a directory argument\n");
                help_only = true;
                ret_code = 1;
            }
        }
        else if (strcmp(argv[i], "--max-errors") == 0) {
            if (i + 1 < argc) {
                i++;
                max_errors = (int)str_to_int64_default(argv[i], strlen(argv[i]), 0);
                if (max_errors < 0) max_errors = 0;
            } else {
                printf("Error: --max-errors requires a number argument\n");
                help_only = true;
                ret_code = 1;
            }
        }
        else if (strncmp(argv[i], "--optimize=", 11) == 0) {
            // Parse --optimize=N format
            optimize_level = (int)str_to_int64_default(argv[i] + 11, strlen(argv[i] + 11), 0);
            if (optimize_level < 0 || optimize_level > 3) {
                printf("Error: --optimize level must be 0-3 (got %d)\n", optimize_level);
                help_only = true;
                ret_code = 1;
            }
        }
        else if (strcmp(argv[i], "-O0") == 0) {
            optimize_level = 0;
        }
        else if (strcmp(argv[i], "-O1") == 0) {
            optimize_level = 1;
        }
        else if (strcmp(argv[i], "-O2") == 0) {
            optimize_level = 2;
        }
        else if (strcmp(argv[i], "-O3") == 0) {
            optimize_level = 3;
        }
        else if (strcmp(argv[i], "--dry-run") == 0) {
            runtime.dry_run = true;
            g_dry_run = true;
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

    // Apply optimize_level setting to runtime
    if (optimize_level >= 0) {
        runtime.optimize_level = (unsigned int)optimize_level;
    }

    // Apply max_errors setting to runtime
    if (max_errors > 0) {
        runtime.max_errors = max_errors;
    }

    if (help_only) {
        print_help();
    }
    else if (script_file) {
        ret_code = run_script_file(&runtime, script_file, use_mir, transpile_only, false);  // false for run_main in regular execution
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

    // Note: memtrack_shutdown is called via atexit handler

    // Note: rpmalloc cleanup is handled automatically when process exits
    // since it's only used within mempool (not as global malloc replacement)

    log_finish();
    return ret_code;
}
