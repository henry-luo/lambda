#include "input/input.hpp"
#include "format/format.h"
#include "format/format-markup.h"
#include "../lib/mime-detect.h"
#include "../lib/mempool.h"
#include "../lib/memtrack.h"
#include "../lib/strbuf.h"  // For string buffer
#include "../lib/str.h"     // For str_to_int64_default, str_to_double_default
#include "../lib/arena.h"   // For arena allocator
#include "../lib/file.h"    // For file_exists, file_delete, file_getcwd
#include "../lib/shell.h"   // For shell_getenv
#include "npm/npm_installer.h"  // For npm install/uninstall
#include "npm/npm_package_json.h"  // For npm_package_json_parse
#include <limits.h>  // for PATH_MAX
// Unicode support (always enabled)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#endif
#include "../lib/log.h"  // Add logging support
#include "validator/validator.hpp"  // For ValidationResult
#include "transpiler.hpp"  // For Runtime struct definition
#include "ast.hpp"  // For print_root_item declaration
#include "emit_sexpr.h"  // For --emit-sexpr command

// Error handling with stack traces
#include "lambda-error.h"
#include "lambda-stack.h"

// System info for sys.* paths
#include "sysinfo.h"

// Graph layout includes
#include "../radiant/layout_graph.hpp"
#include "../radiant/graph_to_svg.hpp"
#include "../radiant/graph_theme.hpp"
#include "input/css/dom_element.hpp"  // DomDocument, DomElement for JS DOM API
#include "input/css/css_style.hpp"   // css_property_system_init
#include "input/css/css_engine.hpp"  // CssEngine for CSS extraction
#include "input/input-graph.h"
#include "js/js_event_loop.h"        // v14: event loop drain
#include "js/js_runtime.h"           // v16: js_check_exception for exit code
#ifdef LAMBDA_PYTHON
#include "py/py_transpiler.hpp"      // Python transpiler
#endif
#ifdef LAMBDA_BASH
#include "bash/bash_transpiler.hpp"  // Bash transpiler
#include "bash/bash_runtime.h"       // bash_exit_code()
#endif
#include "ts/ts_transpiler.hpp"      // TypeScript transpiler
#ifdef LAMBDA_RUBY
#include "rb/rb_transpiler.hpp"      // Ruby transpiler
#endif

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

// strcasestr is a GNU extension not available on Windows
static const char* strcasestr(const char* haystack, const char* needle) {
    if (!needle[0]) return haystack;
    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        if (_strnicmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
    }
    return NULL;
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

// PDF rendering function from radiant (available since radiant sources are included in lambda.exe)
int render_html_to_pdf(const char* html_file, const char* pdf_file, int viewport_width = 800, int viewport_height = 1200, float scale = 1.0f);

// PNG rendering function from radiant (available since radiant sources are included in lambda.exe)
int render_html_to_png(const char* html_file, const char* png_file, int viewport_width = 1200, int viewport_height = 800, float scale = 1.0f, float pixel_ratio = 1.0f);

// JPEG rendering function from radiant (available since radiant sources are included in lambda.exe)
int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality, int viewport_width = 1200, int viewport_height = 800, float scale = 1.0f, float pixel_ratio = 1.0f);

// Document viewer function from radiant - unified viewer for all document types (HTML, PDF, Markdown, etc.)
extern int view_doc_in_window(const char* doc_file);
extern int view_doc_in_window_with_events(const char* doc_file, const char* event_file, bool headless,
                                           const char** font_dirs = nullptr, int font_dir_count = 0);

// REPL functions from main-repl.cpp
extern int lambda_repl_init();
extern void lambda_repl_cleanup();
void transpile_ast_root(Transpiler* tp, AstScript *script);

// DOM functions from radiant (for JS --document support)
extern Element* get_html_root_element(Input* input);
extern DomDocument* dom_document_create(Input* input);
extern DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);
extern CssStylesheet** extract_and_collect_css(Element* html_root, CssEngine* engine, const char* base_path, Pool* pool, int* stylesheet_count);

// MIR JIT optimization level for JS (from transpile_js_mir.cpp)
extern unsigned int g_js_mir_optimize_level;

// MIR interpreter mode (from mir.c)
extern "C" int g_mir_interp_mode;

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
#ifdef LAMBDA_C2MIR
    printf("Lambda Script REPL v1.0%s\n", use_mir ? "" : " (C2MIR)");
#else
    printf("Lambda Script REPL v1.0\n");
#endif
    printf("Type help for commands, quit to exit\n");
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
            mem_free(line);
            continue;
        }

        // Add to command history (only for first line of multi-line input)
        if (pending_input->length == 0) {
            lambda_repl_add_history(line);
        }

        // Handle REPL commands (only when not in multi-line mode)
        if (pending_input->length == 0) {
            if (strcmp(line, "quit") == 0 || strcmp(line, "q") == 0 || strcmp(line, "exit") == 0) {
                mem_free(line);
                break;
            }

            if (strcmp(line, "help") == 0 || strcmp(line, "h") == 0) {
                print_help();
                mem_free(line);
                continue;
            }

            if (strcmp(line, "clear") == 0) {
                strbuf_reset(repl_history);
                strbuf_reset(last_output);
                printf("REPL history cleared\n");
                mem_free(line);
                continue;
            }
        }

        // Append line to pending input
        if (pending_input->length > 0) {
            strbuf_append_str(pending_input, "\n");
        }
        strbuf_append_str(pending_input, line);
        mem_free(line);

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
#ifdef LAMBDA_C2MIR
            // transpile using C2MIR
            output_input = run_script(runtime, repl_history->str, script_path, false);
#endif
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
#ifdef LAMBDA_C2MIR
        output_input = run_script_with_run_main(runtime, (char*)script_path, false, run_main);
#endif
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
    if (!is_http_url && !file_exists(input_file)) {
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
    char* cwd_path = file_getcwd();
    if (!cwd_path) {
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
    mem_free(cwd_path);

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
            // Check if input is LaTeX and route to Lambda package converter
            if (is_latex_input) {
                printf("Using Lambda LaTeX package pipeline\n");

                // Build a Lambda script that imports the LaTeX package
                // and renders the file to HTML
                char script_buf[4096];
                const char* standalone_opt = full_document ? ", {standalone: true}" : ", null";
                snprintf(script_buf, sizeof(script_buf),
                    "import latex: .lambda.package.latex.latex\n"
                    "let ast^err = input(\"%s\", {type: \"latex\"})\n"
                    "latex.render_to_html(ast%s)\n",
                    input_file, standalone_opt);

                // Run the script using the Lambda runtime
                Runtime lambda_runtime;
                runtime_init(&lambda_runtime);
                lambda_runtime.current_dir = const_cast<char*>("./");
                lambda_runtime.import_base_dir = "./";  // resolve imports from project root, not temp/

                // Write the script to a temporary file, then execute it
                const char* tmp_script_path = "temp/_convert_latex_tmp.ls";
                write_text_file(tmp_script_path, script_buf);
                Input* script_result = run_script_mir(&lambda_runtime, nullptr, (char*)tmp_script_path, false);
                if (script_result && get_type_id(script_result->root) != LMD_TYPE_NULL
                    && get_type_id(script_result->root) != LMD_TYPE_ERROR) {
                    full_doc_output = strbuf_new_cap(8192);
                    print_root_item(full_doc_output, script_result->root);
                } else {
                    printf("Error: Lambda LaTeX package - HTML rendering failed\n");
                    if (script_result && get_type_id(script_result->root) == LMD_TYPE_ERROR) {
                        LambdaError* last_error = get_persistent_last_error();
                        if (last_error) {
                            err_print(last_error);
                            clear_persistent_last_error();
                        }
                    }
                }

                runtime_cleanup(&lambda_runtime);
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

#ifndef _WIN32
// per-script timeout support for test-batch mode
static sigjmp_buf batch_timeout_jmp;
static volatile sig_atomic_t batch_timeout_active = 0;

// MIR error recovery for batch mode: longjmp instead of exit(1)
static jmp_buf mir_error_jmp;
static volatile sig_atomic_t mir_error_active = 0;
static char mir_error_msg[256];

static void __attribute__((noreturn)) batch_mir_error_handler(MIR_error_type_t error_type, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsnprintf(mir_error_msg, sizeof(mir_error_msg), format, ap);
    va_end(ap);
    fprintf(stderr, "MIR error: %s\n", mir_error_msg);
    if (mir_error_active) {
        mir_error_active = 0;
        longjmp(mir_error_jmp, 1);
    }
    // fallback: if not in protected region, exit
    exit(1);
}

// SIGSEGV/SIGBUS recovery for batch mode: catch runtime crashes in JIT code
static sigjmp_buf batch_crash_jmp;
static volatile sig_atomic_t batch_crash_active = 0;

static void batch_crash_handler(int sig) {
    if (batch_crash_active) {
        batch_crash_active = 0;
        siglongjmp(batch_crash_jmp, sig);
    }
    // not in protected region — re-raise with default handler
    signal(sig, SIG_DFL);
    raise(sig);
}

static void batch_alarm_handler(int sig) {
    (void)sig;
    if (batch_timeout_active) {
        batch_timeout_active = 0;
        siglongjmp(batch_timeout_jmp, 1);
    }
}

// get current resident set size in bytes (for memory profiling)
static size_t get_rss_bytes() {
#ifdef __APPLE__
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
        return info.resident_size;
#elif defined(__linux__)
    FILE* f = fopen("/proc/self/statm", "r");
    if (f) {
        unsigned long pages = 0;
        fscanf(f, "%*u %lu", &pages); // second field = resident pages
        fclose(f);
        return pages * 4096;
    }
#endif
    return 0;
}
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Set console to UTF-8 for proper Unicode display on Windows
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // Store command line args for sys.proc.self.args access
    sysinfo_set_args(argc, argv);

    // Initialize lambda home path (reads LAMBDA_HOME env var if set)
    lambda_home_init();

    // Initialize logging system with config file if available
    if (file_exists("log.conf")) {
        // log.conf exists, load it
        if (log_parse_config_file("log.conf") != LOG_OK) {
            fprintf(stderr, "Warning: Failed to parse log.conf, using defaults\n");
        }
    }
    log_init("");  // Initialize with parsed config or defaults

    // Check for --no-log flag early (before any logging)
    // Strip it from argv so subcommand handlers don't see it
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-log") == 0) {
            log_disable_all();
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            i--;
        }
    }

#ifndef NDEBUG
    // suppress debug-build note in bash mode (test expected output was generated with release build)
    bool is_bash_mode = (argc >= 2 && strcmp(argv[1], "bash") == 0);
    // also suppress when auto-detecting bash mode: short flags, .sh, .sub
    if (!is_bash_mode && argc >= 2) {
        if (argv[1][0] == '-' && argv[1][1] != '-' && argv[1][1] != '\0') {
            is_bash_mode = true;
        } else if (argv[1][0] == '+' && (argv[1][1] == 'o' || argv[1][1] == 'O')) {
            is_bash_mode = true;
        } else if (argv[1][0] != '-') {
            size_t alen = strlen(argv[1]);
            if ((alen > 3 && strcmp(argv[1] + alen - 3, ".sh") == 0) ||
                (alen > 4 && strcmp(argv[1] + alen - 4, ".sub") == 0)) {
                is_bash_mode = true;
            }
        }
    }
    if (!is_bash_mode) {
        log_notice("############################################");
        log_notice("!!! Running DEBUG build of lambda.exe  !!!");
        log_notice("!!! Do NOT use it for performance test !!!");
        log_notice("############################################");
    }
#endif

    // Add trace statement at start of main
    log_debug("main() started with %d arguments", argc);

    // Initialize memory tracker early in program lifecycle
    log_debug("initializing memory tracker");
    // Check environment variable for debug mode
    const char* memtrack_env = shell_getenv("MEMTRACK_MODE");
    MemtrackMode mode = MEMTRACK_MODE_STATS;  // Default to stats mode
    if (memtrack_env && strcmp(memtrack_env, "DEBUG") == 0) {
        mode = MEMTRACK_MODE_DEBUG;
        log_debug("memory tracker in DEBUG mode");
    } else if (memtrack_env && strcmp(memtrack_env, "OFF") == 0) {
        mode = MEMTRACK_MODE_OFF;
    }
    memtrack_init(mode);
    atexit([]{ memtrack_shutdown(); });  // Ensure shutdown is called on exit
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

    // Handle Node.js package manager command
    if (argc >= 2 && strcmp(argv[1], "node") == 0) {
        log_debug("Entering Node.js command handler");

        if (argc < 3 || strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0) {
            printf("Lambda Node.js Package Manager\n\n");
            printf("Usage: %s node <command> [options]\n", argv[0]);
            printf("\nCommands:\n");
            printf("  install [pkg]           Install dependencies (or a specific package)\n");
            printf("  install -D <pkg>        Install as dev dependency\n");
            printf("  uninstall <pkg>         Remove a dependency\n");
            printf("  info <pkg>              Show package info\n");
            printf("  task <script>           Run a script from package.json\n");
            printf("  exec <pkg> [args...]    Run a package binary (like npx)\n");
            printf("\nOptions:\n");
            printf("  --production            Skip devDependencies\n");
            printf("  --dry-run               Print what would be installed\n");
            printf("  -h, --help              Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s node install              # Install from package.json\n", argv[0]);
            printf("  %s node install lodash       # Add lodash\n", argv[0]);
            printf("  %s node install -D jest      # Add jest as dev dep\n", argv[0]);
            printf("  %s node uninstall lodash     # Remove lodash\n", argv[0]);
            printf("  %s node task test            # Run 'test' script\n", argv[0]);
            printf("  %s node exec cowsay hello    # Run cowsay binary\n", argv[0]);
            log_finish();
            return 0;
        }

        const char* subcmd = argv[2];
        char* cwd = file_getcwd();

        if (strcmp(subcmd, "install") == 0) {
            NpmInstallOptions opts = {};
            const char* pkg_name = NULL;
            const char* version_range = NULL;
            bool is_dev = false;

            for (int i = 3; i < argc; i++) {
                if (strcmp(argv[i], "--production") == 0) opts.production = true;
                else if (strcmp(argv[i], "--dry-run") == 0) opts.dry_run = true;
                else if (strcmp(argv[i], "--verbose") == 0) opts.verbose = true;
                else if (strcmp(argv[i], "-D") == 0) is_dev = true;
                else if (argv[i][0] != '-') {
                    // parse name[@range]
                    pkg_name = argv[i];
                    const char* at = strchr(pkg_name, '@');
                    if (at && at != pkg_name) { // not scoped package
                        // split at @
                        static char name_buf[256];
                        int name_len = (int)(at - pkg_name);
                        snprintf(name_buf, sizeof(name_buf), "%.*s", name_len, pkg_name);
                        pkg_name = name_buf;
                        version_range = at + 1;
                    }
                }
            }

            NpmInstallResult* result;
            if (pkg_name) {
                result = npm_install_package(cwd, pkg_name, version_range, is_dev, &opts);
            } else {
                result = npm_install(cwd, &opts);
            }

            int exit_code = result->success ? 0 : 1;
            if (!result->success && result->error) {
                fprintf(stderr, "Error: %s\n", result->error);
            }
            npm_install_result_free(result);
            mem_free(cwd);
            log_finish();
            return exit_code;

        } else if (strcmp(subcmd, "uninstall") == 0) {
            if (argc < 4) {
                fprintf(stderr, "Usage: %s node uninstall <package>\n", argv[0]);
                mem_free(cwd);
                log_finish();
                return 1;
            }
            int ret = npm_uninstall(cwd, argv[3]);
            mem_free(cwd);
            log_finish();
            return ret;

        } else if (strcmp(subcmd, "task") == 0) {
            // Run a script from package.json
            if (argc < 4) {
                // List available scripts
                char pkg_path[PATH_MAX];
                snprintf(pkg_path, sizeof(pkg_path), "%s/package.json", cwd);
                NpmPackageJson* pkg = npm_package_json_parse(pkg_path);
                if (!pkg || !pkg->valid) {
                    fprintf(stderr, "No package.json found in current directory\n");
                    if (pkg) npm_package_json_free(pkg);
                    mem_free(cwd);
                    log_finish();
                    return 1;
                }
                if (pkg->script_count == 0) {
                    printf("No scripts defined in package.json\n");
                } else {
                    printf("Available scripts:\n");
                    for (int i = 0; i < pkg->script_count; i++) {
                        printf("  %-20s %s\n", pkg->scripts[i].name, pkg->scripts[i].range);
                    }
                }
                npm_package_json_free(pkg);
                mem_free(cwd);
                log_finish();
                return 0;
            }

            const char* script_name = argv[3];
            char pkg_path[PATH_MAX];
            snprintf(pkg_path, sizeof(pkg_path), "%s/package.json", cwd);
            NpmPackageJson* pkg = npm_package_json_parse(pkg_path);
            if (!pkg || !pkg->valid) {
                fprintf(stderr, "No package.json found in current directory\n");
                if (pkg) npm_package_json_free(pkg);
                mem_free(cwd);
                log_finish();
                return 1;
            }

            // Find the script
            const char* script_cmd = NULL;
            for (int i = 0; i < pkg->script_count; i++) {
                if (strcmp(pkg->scripts[i].name, script_name) == 0) {
                    script_cmd = pkg->scripts[i].range;
                    break;
                }
            }
            if (!script_cmd) {
                fprintf(stderr, "Script '%s' not found in package.json\n", script_name);
                fprintf(stderr, "Available scripts:");
                for (int i = 0; i < pkg->script_count; i++) {
                    fprintf(stderr, " %s", pkg->scripts[i].name);
                }
                fprintf(stderr, "\n");
                npm_package_json_free(pkg);
                mem_free(cwd);
                log_finish();
                return 1;
            }

            // Prepend node_modules/.bin to PATH for script execution
            char bin_path[PATH_MAX];
            snprintf(bin_path, sizeof(bin_path), "%s/node_modules/.bin", cwd);
            const char* existing_path = getenv("PATH");
            char new_path[PATH_MAX * 2];
            snprintf(new_path, sizeof(new_path), "%s:%s", bin_path, existing_path ? existing_path : "");

            ShellEnvEntry env_entries[] = {
                { "PATH", new_path },
                { NULL, NULL }
            };
            ShellOptions shell_opts = {};
            shell_opts.cwd = cwd;
            shell_opts.env = env_entries;

            // Append any extra args from CLI
            if (argc > 4) {
                // rebuild command with extra args
                char full_cmd[4096];
                int pos = snprintf(full_cmd, sizeof(full_cmd), "%s", script_cmd);
                for (int i = 4; i < argc && pos < (int)sizeof(full_cmd) - 2; i++) {
                    pos += snprintf(full_cmd + pos, sizeof(full_cmd) - pos, " %s", argv[i]);
                }
                printf("> %s\n\n", full_cmd);
                ShellResult result = shell_exec_line(full_cmd, &shell_opts);
                if (result.stdout_buf) { fwrite(result.stdout_buf, 1, result.stdout_len, stdout); }
                if (result.stderr_buf) { fwrite(result.stderr_buf, 1, result.stderr_len, stderr); }
                int exit_code = result.exit_code;
                shell_result_free(&result);
                npm_package_json_free(pkg);
                mem_free(cwd);
                log_finish();
                return exit_code;
            } else {
                printf("> %s\n\n", script_cmd);
                ShellResult result = shell_exec_line(script_cmd, &shell_opts);
                if (result.stdout_buf) { fwrite(result.stdout_buf, 1, result.stdout_len, stdout); }
                if (result.stderr_buf) { fwrite(result.stderr_buf, 1, result.stderr_len, stderr); }
                int exit_code = result.exit_code;
                shell_result_free(&result);
                npm_package_json_free(pkg);
                mem_free(cwd);
                log_finish();
                return exit_code;
            }

        } else if (strcmp(subcmd, "exec") == 0) {
            // Run a package binary (like npx)
            if (argc < 4) {
                fprintf(stderr, "Usage: %s node exec <package> [args...]\n", argv[0]);
                mem_free(cwd);
                log_finish();
                return 1;
            }

            const char* pkg_name = argv[3];

            // Check node_modules/.bin/<pkg_name> first
            char bin_path[PATH_MAX];
            snprintf(bin_path, sizeof(bin_path), "%s/node_modules/.bin/%s", cwd, pkg_name);

            if (!file_exists(bin_path)) {
                // Try installing the package first
                printf("Package '%s' not found locally, installing...\n", pkg_name);
                NpmInstallOptions install_opts = {};
                NpmInstallResult* install_result = npm_install_package(cwd, pkg_name, NULL, false, &install_opts);
                if (!install_result->success) {
                    fprintf(stderr, "Failed to install '%s'", pkg_name);
                    if (install_result->error) fprintf(stderr, ": %s", install_result->error);
                    fprintf(stderr, "\n");
                    npm_install_result_free(install_result);
                    mem_free(cwd);
                    log_finish();
                    return 1;
                }
                npm_install_result_free(install_result);
            }

            if (!file_exists(bin_path)) {
                fprintf(stderr, "No executable '%s' found in node_modules/.bin\n", pkg_name);
                mem_free(cwd);
                log_finish();
                return 1;
            }

            // Build command line with remaining args
            char cmd[4096];
            int pos = snprintf(cmd, sizeof(cmd), "%s", bin_path);
            for (int i = 4; i < argc && pos < (int)sizeof(cmd) - 2; i++) {
                pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s", argv[i]);
            }

            ShellOptions shell_opts = {};
            shell_opts.cwd = cwd;
            ShellResult result = shell_exec_line(cmd, &shell_opts);
            if (result.stdout_buf) { fwrite(result.stdout_buf, 1, result.stdout_len, stdout); }
            if (result.stderr_buf) { fwrite(result.stderr_buf, 1, result.stderr_len, stderr); }
            int exit_code = result.exit_code;
            shell_result_free(&result);
            mem_free(cwd);
            log_finish();
            return exit_code;

        } else {
            fprintf(stderr, "Unknown node command: %s\n", subcmd);
            fprintf(stderr, "Run '%s node --help' for usage.\n", argv[0]);
            mem_free(cwd);
            log_finish();
            return 1;
        }
    }

    // Handle JavaScript command
    log_debug("Checking for js command");
    if (argc >= 2 && strcmp(argv[1], "js") == 0) {
        log_debug("Entering JavaScript command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda JavaScript Transpiler v1.0\n\n");
            printf("Usage: %s js [file.js] [--document page.html]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'js' command runs the JavaScript transpiler.\n");
            printf("  If no file is provided, it runs built-in test cases.\n");
            printf("  If a file is provided, it transpiles and executes the JavaScript.\n");
            printf("\nOptions:\n");
            printf("  -h, --help              Show this help message\n");
            printf("  --document <file.html>  Load HTML document for DOM API access\n");
            printf("\nExamples:\n");
            printf("  %s js                             # Run built-in tests\n", argv[0]);
            printf("  %s js test.js                     # Transpile and run test.js\n", argv[0]);
            printf("  %s js script.js --document page.html  # Run JS with DOM access\n", argv[0]);
            log_finish();
            return 0;
        }

        Runtime runtime;
        runtime_init(&runtime);

        // Initialize stack bounds for GC conservative scanning
        lambda_stack_init();

        bool js_had_error = false;

        if (argc >= 3) {
            // Parse arguments: js [options] file.js [--document page.html]
            const char* js_file = NULL;
            const char* html_file = NULL;
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "--document") == 0 && i + 1 < argc) {
                    html_file = argv[++i];
                } else if (strcmp(argv[i], "--mir-interp") == 0) {
                    g_mir_interp_mode = 1;
                } else if (strncmp(argv[i], "--opt-level=", 12) == 0) {
                    int level = atoi(argv[i] + 12);
                    if (level >= 0 && level <= 3) g_js_mir_optimize_level = (unsigned int)level;
                } else if (argv[i][0] != '-') {
                    if (!js_file) js_file = argv[i];
                }
            }
            if (!js_file) js_file = argv[2];  // fallback

            char* js_source = read_text_file(js_file);
            if (!js_source) {
                printf("Error: Could not read file '%s'\n", js_file);
                runtime_cleanup(&runtime);
                log_finish();
                return 1;
            }

            // If --document is provided, load HTML and set up DOM context
            if (html_file) {
                // Parse HTML into Lambda Element tree
                char* html_content = read_text_file(html_file);
                if (!html_content) {
                    printf("Error: Could not read HTML file '%s'\n", html_file);
                    mem_free(js_source);
                    runtime_cleanup(&runtime);
                    log_finish();
                    return 1;
                }

                // Use heap-allocated String* for type param (flexible array member)
                String* html_type = (String*)mem_alloc(sizeof(String) + 5, MEM_CAT_SYSTEM);
                html_type->len = 4;
                memcpy(html_type->chars, "html", 5);  // includes null terminator
                Input* input = input_from_source(html_content, NULL, html_type, NULL);
                mem_free(html_content);
                mem_free(html_type);

                if (input) {
                    Element* html_root = get_html_root_element(input);
                    if (html_root) {
                        DomDocument* dom_doc = dom_document_create(input);
                        if (dom_doc) {
                            // init CSS property system before building DOM tree
                            // so inline style parsing resolves property IDs correctly
                            css_property_system_init(dom_doc->pool);
                            DomElement* dom_root = build_dom_tree_from_element(html_root, dom_doc, NULL);
                            if (dom_root) {
                                dom_doc->root = dom_root;
                                dom_doc->html_root = html_root;

                                // Extract and parse CSS from <style> elements
                                // so document.styleSheets and <style>.sheet work
                                CssEngine* css_engine = css_engine_create(dom_doc->pool);
                                if (css_engine) {
                                    int sheet_count = 0;
                                    CssStylesheet** sheets = extract_and_collect_css(
                                        html_root, css_engine, html_file, dom_doc->pool, &sheet_count);
                                    dom_doc->stylesheets = sheets;
                                    dom_doc->stylesheet_count = sheet_count;
                                    log_debug("JS document: extracted %d stylesheet(s)", sheet_count);
                                }

                                runtime.dom_doc = (void*)dom_doc;
                                log_debug("Loaded HTML document: root=<%s>", dom_root->tag_name);
                            }
                        }
                    }
                }
            }

            // Set up process.argv C-level storage (actual Lambda array built lazily)
            {
                static const char* js_argv_store[64];
                static int js_argc_store = 0;
                js_argc_store = 0;
                js_argv_store[js_argc_store++] = argv[0]; // lambda.exe
                js_argv_store[js_argc_store++] = js_file; // script path
                // Pass remaining non-option arguments
                for (int i = 2; i < argc && js_argc_store < 64; i++) {
                    if (strcmp(argv[i], "--document") == 0 && i + 1 < argc) { i++; continue; }
                    if (strcmp(argv[i], "--mir-interp") == 0) continue;
                    if (argv[i] == js_file) continue; // already added
                    if (argv[i][0] == '-') continue;
                    js_argv_store[js_argc_store++] = argv[i];
                }
                js_store_process_argv(js_argc_store, js_argv_store);
            }

            Item result = transpile_js_to_mir(&runtime, js_source, js_file);

            // JS mode: no REPL printing of last expression value
            // (JS spec: scripts don't print their completion value)

            // Track if transpilation failed or uncaught exception occurred
            if (result.item == ITEM_ERROR) {
                js_had_error = true;
            }
            if (js_check_exception()) {
                js_had_error = true;
                const char* exc_msg = js_get_exception_message();
                if (exc_msg[0]) fprintf(stderr, "Uncaught %s\n", exc_msg);
                js_clear_exception();
            }

            mem_free(js_source);
        }

        runtime_cleanup(&runtime);
        log_finish();
        return js_had_error ? 1 : 0;
    }

#ifdef LAMBDA_PYTHON
    // Handle Python command
    log_debug("Checking for py command");
    if (argc >= 2 && strcmp(argv[1], "py") == 0) {
        log_debug("Entering Python command handler");

        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Python Transpiler v1.0\n\n");
            printf("Usage: %s py [file.py]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'py' command runs the Python transpiler.\n");
            printf("  If a file is provided, it transpiles and executes the Python code.\n");
            printf("\nOptions:\n");
            printf("  -h, --help    Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s py script.py    # Transpile and run script.py\n", argv[0]);
            log_finish();
            return 0;
        }

        Runtime runtime;
        runtime_init(&runtime);
        lambda_stack_init();

        if (argc >= 3) {
            const char* py_file = argv[2];
            char* py_source = read_text_file(py_file);
            if (!py_source) {
                printf("Error: Could not read file '%s'\n", py_file);
                runtime_cleanup(&runtime);
                log_finish();
                return 1;
            }

            Item result = transpile_py_to_mir(&runtime, py_source, py_file);

            // only print non-null results (Python scripts use print() for output)
            if (result.item != ITEM_NULL && result.item != 0) {
                TypeId result_type = get_type_id(result);
                if (result_type == LMD_TYPE_MAP || result_type == LMD_TYPE_ARRAY
                    || result_type == LMD_TYPE_ELEMENT) {
                    Pool* fmt_pool = pool_create();
                    String* json = format_json(fmt_pool, result);
                    if (json) {
                        printf("%.*s\n", json->len, json->chars);
                    }
                    pool_destroy(fmt_pool);
                } else {
                    StrBuf *output = strbuf_new_cap(256);
                    print_root_item(output, result);
                    printf("%s\n", output->str);
                    strbuf_free(output);
                }
            }

            mem_free(py_source);
        }

        runtime_cleanup(&runtime);
        log_finish();
        return 0;
    }
#endif // LAMBDA_PYTHON

#ifdef LAMBDA_RUBY
    // Handle Ruby command
    log_debug("Checking for rb command");
    if (argc >= 2 && strcmp(argv[1], "rb") == 0) {
        log_debug("Entering Ruby command handler");

        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Ruby Transpiler v1.0\n\n");
            printf("Usage: %s rb [file.rb]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'rb' command runs the Ruby transpiler.\n");
            printf("  If a file is provided, it transpiles and executes the Ruby code.\n");
            printf("\nOptions:\n");
            printf("  -h, --help    Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s rb script.rb    # Transpile and run script.rb\n", argv[0]);
            log_finish();
            return 0;
        }

        Runtime runtime;
        runtime_init(&runtime);
        lambda_stack_init();

        if (argc >= 3) {
            const char* rb_file = argv[2];
            char* rb_source = read_text_file(rb_file);
            if (!rb_source) {
                printf("Error: Could not read file '%s'\n", rb_file);
                runtime_cleanup(&runtime);
                log_finish();
                return 1;
            }

            Item result = transpile_rb_to_mir(&runtime, rb_source, rb_file);

            // only print non-null results (Ruby scripts use puts for output)
            if (result.item != ITEM_NULL && result.item != 0) {
                TypeId result_type = get_type_id(result);
                if (result_type == LMD_TYPE_MAP || result_type == LMD_TYPE_ARRAY
                    || result_type == LMD_TYPE_ELEMENT) {
                    Pool* fmt_pool = pool_create();
                    String* json = format_json(fmt_pool, result);
                    if (json) {
                        printf("%.*s\n", json->len, json->chars);
                    }
                    pool_destroy(fmt_pool);
                } else {
                    StrBuf *output = strbuf_new_cap(256);
                    print_root_item(output, result);
                    printf("%s\n", output->str);
                    strbuf_free(output);
                }
            }

            mem_free(rb_source);
        }

        runtime_cleanup(&runtime);
        log_finish();
        return 0;
    }
#endif // LAMBDA_RUBY

#ifdef LAMBDA_BASH
    // Handle Bash command — either explicit "bash" subcommand or auto-detect
    log_debug("Checking for bash command");
    // auto-detect bash mode: lambda.exe -c 'cmd', lambda.exe -o option, lambda.exe script.sh
    bool bash_auto = false;
    if (argc >= 2 && strcmp(argv[1], "bash") != 0) {
        // check for short flags (bash uses -c, -e, -x, -o, etc.; Lambda uses only --long-flags)
        if (argv[1][0] == '-' && argv[1][1] != '-' && argv[1][1] != '\0') {
            bash_auto = true;
        }
        // check for +o (bash unset option syntax)
        else if (argv[1][0] == '+' && (argv[1][1] == 'o' || argv[1][1] == 'O')) {
            bash_auto = true;
        }
        // check for .sh or .sub file as first arg
        else if (argv[1][0] != '-') {
            size_t len = strlen(argv[1]);
            if ((len > 3 && strcmp(argv[1] + len - 3, ".sh") == 0) ||
                (len > 4 && strcmp(argv[1] + len - 4, ".sub") == 0)) {
                bash_auto = true;
            }
        }
    }
    int bash_base = bash_auto ? 1 : 2;  // arg offset: skip "bash" subcommand when explicit
    if (argc >= 2 && (strcmp(argv[1], "bash") == 0 || bash_auto)) {
        log_debug("Entering Bash command handler (auto=%d)", bash_auto ? 1 : 0);

        if (!bash_auto && argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Bash Transpiler v0.1\n\n");
            printf("Usage: %s bash [--posix] [file.sh]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'bash' command runs the Bash transpiler.\n");
            printf("  If a file is provided, it transpiles and executes the Bash script.\n");
            printf("\nOptions:\n");
            printf("  -h, --help    Show this help message\n");
            printf("  --posix       Enable POSIX-compatible mode (disable bash extensions)\n");
            printf("\nExamples:\n");
            printf("  %s bash script.sh          # Transpile and run script.sh\n", argv[0]);
            printf("  %s bash --posix script.sh  # Run in POSIX compatibility mode\n", argv[0]);
            log_finish();
            return 0;
        }

        Runtime runtime;
        runtime_init(&runtime);
        lambda_stack_init();

        // scan for --posix flag, -c flag, and find the script file argument
        bool bash_posix = false;
        const char* bash_file = NULL;
        const char* bash_inline_cmd = NULL;  // -c 'command string'
        int bash_args_start = -1;  // index where positional args start
        for (int i = bash_base; i < argc; i++) {
            if (strcmp(argv[i], "--posix") == 0) {
                bash_posix = true;
            } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "+o") == 0) && i + 1 < argc) {
                // -o option / +o option: set/unset shell option
                bool enable = (argv[i][0] == '-');
                i++;
                if (strcmp(argv[i], "posix") == 0) bash_posix = enable;
                else if (strcmp(argv[i], "errexit") == 0) bash_set_pending_option('e', enable);
                else if (strcmp(argv[i], "nounset") == 0) bash_set_pending_option('u', enable);
                else if (strcmp(argv[i], "xtrace") == 0) bash_set_pending_option('x', enable);
                else if (strcmp(argv[i], "pipefail") == 0) { /* bash_set_pending_option('p', enable); */ }
            } else if ((strcmp(argv[i], "-O") == 0 || strcmp(argv[i], "+O") == 0) && i + 1 < argc) {
                // -O shopt_option / +O shopt_option: set/unset shopt option (ignore for now)
                i++; // skip option name
            } else if (strcmp(argv[i], "-n") == 0) {
                // syntax check only — skip execution (not fully implemented, just consume)
            } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                bash_inline_cmd = argv[++i];
                // remaining args after -c 'cmd' are positional params ($0, $1, ...)
                if (i + 1 < argc) bash_args_start = i + 1;
                break;
            } else if (argv[i][0] == '-' && argv[i][1] != '-' && strchr(argv[i], 'c') != NULL && i + 1 < argc) {
                // combined flags like -ce, -xc, -ec: extract non-c flags and set them
                const char* flags = argv[i] + 1; // skip leading -
                for (const char* f = flags; *f; f++) {
                    if (*f == 'e') bash_set_pending_option('e', true);
                    else if (*f == 'x') bash_set_pending_option('x', true);
                    else if (*f == 'u') bash_set_pending_option('u', true);
                    // 'c' is handled by taking the next arg as inline cmd
                }
                bash_inline_cmd = argv[++i];
                if (i + 1 < argc) bash_args_start = i + 1;
                break;
            } else if (argv[i][0] == '-' && argv[i][1] != '-' && argv[i][1] != '\0') {
                // single-char flag(s) without 'c': -e, -x, -u, etc.
                const char* flags = argv[i] + 1;
                for (const char* f = flags; *f; f++) {
                    if (*f == 'e') bash_set_pending_option('e', true);
                    else if (*f == 'x') bash_set_pending_option('x', true);
                    else if (*f == 'u') bash_set_pending_option('u', true);
                }
            } else if (bash_inline_cmd == NULL && bash_file == NULL) {
                bash_file = argv[i];
                // remaining args after filename are positional params ($1, $2, ...)
                if (i + 1 < argc) bash_args_start = i + 1;
                break;
            }
        }

        // store pending positional params from remaining args (applied after heap init)
        if (bash_args_start >= 0 && bash_args_start < argc) {
            if (bash_inline_cmd) {
                // bash -c 'cmd' arg0 arg1 ... : arg0=$0, arg1=$1, ...
                // skip arg0 ($0), set $1..$N from the rest
                bash_set_pending_args((const char**)&argv[bash_args_start], argc - bash_args_start, true);
            } else {
                // bash script.sh arg1 arg2 ... : arg1=$1, arg2=$2, ...
                bash_set_pending_args((const char**)&argv[bash_args_start], argc - bash_args_start, false);
            }
        }

        if (bash_posix) {
            bash_set_posix_mode(true);
        }

        if (bash_inline_cmd) {
            // -c 'command string' — run inline command string
            // set BASH_COMMAND to the full -c string (real bash sets it to current command)
            setenv("BASH_COMMAND", bash_inline_cmd, 1);
            char* bash_source = mem_strdup(bash_inline_cmd, MEM_CAT_SYSTEM);
            // if arg0 is provided ($0), use it as the script name; otherwise use the actual executable
            const char* script_name = (bash_args_start >= 0 && bash_args_start < argc)
                ? argv[bash_args_start] : argv[0];
            Item result = transpile_bash_to_mir(&runtime, bash_source, script_name);
            (void)result;
            mem_free(bash_source);
        } else if (bash_file) {
            char* bash_source = read_text_file(bash_file);
            if (!bash_source) {
                printf("Error: Could not read file '%s'\n", bash_file);
                runtime_cleanup(&runtime);
                log_finish();
                return 1;
            }

            Item result = transpile_bash_to_mir(&runtime, bash_source, bash_file);

            if (result.item != ITEM_NULL && result.item != 0 && result.item != ITEM_ERROR) {
                TypeId result_type = get_type_id(result);
                if (result_type == LMD_TYPE_MAP || result_type == LMD_TYPE_ARRAY
                    || result_type == LMD_TYPE_ELEMENT) {
                    Pool* fmt_pool = pool_create();
                    String* json = format_json(fmt_pool, result);
                    if (json) {
                        printf("%.*s\n", json->len, json->chars);
                    }
                    pool_destroy(fmt_pool);
                }
            }

            mem_free(bash_source);
        }

        int bash_exit = bash_exit_code(bash_get_exit_code());
        runtime_cleanup(&runtime);
        log_finish();
        return bash_exit;
    }
#endif // LAMBDA_BASH

    // Handle TypeScript command
    log_debug("Checking for ts command");
    if (argc >= 2 && strcmp(argv[1], "ts") == 0) {
        log_debug("Entering TypeScript command handler");

        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda TypeScript Transpiler v0.1\n\n");
            printf("Usage: %s ts <file.ts>\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'ts' command runs the TypeScript transpiler.\n");
            printf("  TypeScript types are preserved as first-class Lambda types\n");
            printf("  for runtime introspection and native code generation.\n");
            printf("\nOptions:\n");
            printf("  -h, --help    Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s ts script.ts    # Transpile and run script.ts\n", argv[0]);
            log_finish();
            return 0;
        }

        Runtime runtime;
        runtime_init(&runtime);
        lambda_stack_init();

        if (argc >= 3) {
            const char* ts_file = argv[2];
            char* ts_source = read_text_file(ts_file);
            if (!ts_source) {
                printf("Error: Could not read file '%s'\n", ts_file);
                runtime_cleanup(&runtime);
                log_finish();
                return 1;
            }

            Item result = transpile_ts_to_mir(&runtime, ts_source, ts_file);

            // only print non-null results
            if (result.item != ITEM_NULL && result.item != 0) {
                TypeId result_type = get_type_id(result);
                if (result_type == LMD_TYPE_MAP || result_type == LMD_TYPE_ARRAY
                    || result_type == LMD_TYPE_ELEMENT) {
                    Pool* fmt_pool = pool_create();
                    String* json = format_json(fmt_pool, result);
                    if (json) {
                        printf("%.*s\n", json->len, json->chars);
                    }
                    pool_destroy(fmt_pool);
                } else {
                    StrBuf *output = strbuf_new_cap(256);
                    print_root_item(output, result);
                    printf("%s\n", output->str);
                    strbuf_free(output);
                }
            }

            mem_free(ts_source);
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

    // Handle math command - moved to Lambda script
    log_debug("Checking for math command");
    if (argc >= 2 && strcmp(argv[1], "math") == 0) {
        printf("The 'math' command has been moved to Lambda script.\n");
        printf("Use: %s run <script.ls> to render math formulas.\n", argv[0]);
        log_finish();
        return 1;
    }

    // Handle render command
    log_debug("Checking for render command");
    if (argc >= 2 && strcmp(argv[1], "render") == 0) {
        log_debug("Entering render command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda HTML Renderer v1.0\n\n");
            printf("Usage: %s render <input.html|input.tex|input.ls> -o <output.svg|output.pdf|output.png|output.jpg> [options]\n", argv[0]);
            printf("\nDescription:\n");
            printf("  The 'render' command layouts an HTML, LaTeX, or Lambda script file and renders the result as SVG, PDF, PNG, JPEG, or DVI.\n");
            printf("  It parses the input (converting LaTeX to HTML or evaluating Lambda script if needed), applies CSS styles,\n");
            printf("  calculates layout, and generates output in the specified format based on file extension.\n");
            printf("\nSupported Input Formats:\n");
            printf("  .html, .htm    HTML documents\n");
            printf("  .tex, .latex   LaTeX documents (converted to HTML for rendering)\n");
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
            printf("\nOptions:\n");
            printf("  -o <output>              Output file path (required, format detected by extension)\n");
            printf("  -vw, --viewport-width    Viewport width in CSS pixels (default: auto-size to content)\n");
            printf("  -vh, --viewport-height   Viewport height in CSS pixels (default: auto-size to content)\n");
            printf("  -s, --scale              User zoom scale factor (default: 1.0)\n");
            printf("  --pixel-ratio            Device pixel ratio for HiDPI/Retina (default: 1.0, use 2.0 for crisp text)\n");
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
        if (!is_http_url && !file_exists(html_file)) {
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
                mem_free(graph_content);
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
            mem_free(graph_content);
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
                mem_free(opts);
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
                file_delete(temp_svg);

                if (exit_code == 0) {
                    printf("Graph rendered successfully to '%s'\n", output_file);
                }

                free_graph_layout(layout);
                log_finish();
                return exit_code;
            }
        }

        // Determine output format based on file extension
        const char* ext = strrchr(output_file, '.');
        int exit_code;

        if (ext && strcmp(ext, ".dvi") == 0) {
            printf("Error: DVI output is no longer supported. Use .svg, .pdf, .png, or .jpg instead.\n");
            log_finish();
            return 1;
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
            printf("\nExamples:\n");
            printf("  %s view                          # View default HTML (test/html/index.html)\n", argv[0]);
            printf("  %s view document.pdf             # View PDF in window\n", argv[0]);
            printf("  %s view page.html                # View HTML document\n", argv[0]);
            printf("  %s view README.md                # View markdown with GitHub styling\n", argv[0]);
            printf("  %s view script.ls                # View Lambda script result\n", argv[0]);
            printf("  %s view paper.tex                # View LaTeX document\n", argv[0]);
            printf("  %s view config.xml               # View XML document\n", argv[0]);
            printf("  %s view data.json                # View JSON source\n", argv[0]);
            printf("  %s view flowchart.mmd            # View Mermaid diagram\n", argv[0]);
            printf("  %s view architecture.d2          # View D2 diagram\n", argv[0]);
            printf("  %s view test/input/test.pdf     # View PDF with path\n", argv[0]);
            printf("  %s view page.html --event-file events.json  # Automated testing\n", argv[0]);
            printf("  %s view page.html --event-file events.json --headless  # Headless testing (no window)\n", argv[0]);
            printf("\nKeyboard Controls:\n");
            printf("  ESC        Close window\n");
            printf("  Q          Quit viewer\n");
            log_finish();
            return 0;
        }

        // Parse arguments for view command
        const char* filename = NULL;
        const char* event_file = NULL;
        bool headless = false;
        const char* font_dirs[16];
        int font_dir_count = 0;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--event-file") == 0 && i + 1 < argc) {
                event_file = argv[++i];
            } else if (strcmp(argv[i], "--headless") == 0) {
                headless = true;
            } else if (strcmp(argv[i], "--font-dir") == 0 && i + 1 < argc) {
                if (font_dir_count < 16) {
                    font_dirs[font_dir_count++] = argv[++i];
                } else {
                    i++; // skip argument
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
        if (!is_http_url && !file_exists(filename)) {
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
                temp_file_path = mem_strdup(temp_path, MEM_CAT_TEMP);
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
                mem_free(graph_content);
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
            mem_free(graph_content);

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

            const char* temp_svg = "./temp/lambda_graph_view.svg";
            write_text_file(temp_svg, svg_str->chars);
#ifndef NDEBUG
            write_text_file("./temp/lambda_graph_debug.svg", svg_str->chars);  // debug copy
#endif
            free_graph_layout(layout);

            // View the temp SVG file
            log_info("Opening graph SVG in viewer: %s", temp_svg);
            exit_code = view_doc_in_window_with_events(temp_svg, event_file, headless, font_dirs, font_dir_count);

            // Clean up temp file after viewing
            file_delete(temp_svg);

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
            log_info("Opening document file: %s (event_file: %s)", filename, event_file ? event_file : "none");
            exit_code = view_doc_in_window_with_events(filename, event_file, headless, font_dirs, font_dir_count);
        } else {
            printf("Error: Unsupported file format '%s'\n", ext ? ext : "(no extension)");
            printf("Supported formats: .pdf, .html, .md, .tex, .ls, .xml, .svg, .png, .jpg, .gif, .json, .yaml, .toml, .txt, .csv\n");
            if (temp_file_path) { file_delete(temp_file_path); mem_free(temp_file_path); }
            log_finish();
            return 1;
        }

        // Cleanup temp file if we created one from HTTP URL
        if (temp_file_path) {
            file_delete(temp_file_path);
            mem_free(temp_file_path);
        }

        fprintf(stderr, "view command completed with result: %d\n", exit_code);
        log_debug("view command completed with result: %d", exit_code);
        log_finish();
        return exit_code;
    }

    // Handle serve command (HTTP/HTTPS server)
    log_debug("Checking for serve command");
    if (argc >= 2 && strcmp(argv[1], "serve") == 0) {
        log_debug("Entering serve command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda HTTP Server\n\n");
            printf("Usage: %s serve [options] [handler.ls]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  -p, --port <port>      HTTP port (default: 3000)\n");
            printf("  --host <addr>          Bind address (default: 0.0.0.0)\n");
            printf("  -d, --dir <path>       Static file directory (default: .)\n");
            printf("  --ssl-port <port>      HTTPS port (0 to disable, default: 0)\n");
            printf("  --ssl-cert <file>      SSL certificate file\n");
            printf("  --ssl-key <file>       SSL private key file\n");
            printf("  --workers <n>          Worker pool size (default: 4)\n");
            printf("  --cors                 Enable CORS (allow all origins)\n");
            printf("  -h, --help             Show this help message\n");
            printf("\nExamples:\n");
            printf("  %s serve                                # Serve current dir on port 3000\n", argv[0]);
            printf("  %s serve -p 8080 -d ./public            # Serve ./public on port 8080\n", argv[0]);
            printf("  %s serve handler.ls                     # Run Lambda script handler\n", argv[0]);
            printf("  %s serve -p 3000 --cors handler.ls      # With CORS enabled\n", argv[0]);
            log_finish();
            return 0;
        }

        // Parse serve command arguments
        int port = 3000;
        const char *host = "0.0.0.0";
        const char *static_dir = NULL;
        const char *handler_file = NULL;
        const char *ssl_cert = NULL;
        const char *ssl_key = NULL;
        int ssl_port = 0;
        int workers = 4;
        bool enable_cors = false;

        for (int i = 2; i < argc; i++) {
            if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
                port = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
                host = argv[++i];
            } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) && i + 1 < argc) {
                static_dir = argv[++i];
            } else if (strcmp(argv[i], "--ssl-port") == 0 && i + 1 < argc) {
                ssl_port = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--ssl-cert") == 0 && i + 1 < argc) {
                ssl_cert = argv[++i];
            } else if (strcmp(argv[i], "--ssl-key") == 0 && i + 1 < argc) {
                ssl_key = argv[++i];
            } else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
                workers = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--cors") == 0) {
                enable_cors = true;
            } else if (argv[i][0] != '-') {
                handler_file = argv[i];
            }
        }

        // Placeholder: server initialization will be connected in Phase 5
        // when the io.http module is fully operational
        log_info("serve command: port=%d host=%s workers=%d", port, host, workers);
        if (static_dir) log_info("serve static dir: %s", static_dir);
        if (handler_file) log_info("serve handler: %s", handler_file);
        if (enable_cors) log_info("serve CORS enabled");
        if (ssl_port > 0) log_info("serve SSL port: %d", ssl_port);

        // TODO: Phase 5 — instantiate Server, configure, and run
        //   Server *srv = server_create(&config);
        //   server_set_pool_size(srv, workers);
        //   if (enable_cors) server_use(srv, middleware_cors(), NULL);
        //   if (static_dir) server_set_static(srv, "/", static_dir);
        //   if (handler_file) { /* load Lambda handler */ }
        //   server_start(srv, port);
        //   server_run(srv);

        fprintf(stderr, "Error: 'serve' command not yet fully implemented.\n");
        fprintf(stderr, "Server infrastructure is ready in lambda/serve/.\n");
        log_finish();
        return 1;
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
        res.url = mem_strdup(url, MEM_CAT_TEMP);
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
                        mem_free(content);
                        if (verbose) {
                            printf("   Saved to: %s\n", output_file);
                        }
                    } else {
                        printf("Error: Failed to read cached content\n");
                        mem_free(res.url);
                        log_finish();
                        return 1;
                    }
                } else {
                    printf("Error: No content available\n");
                    mem_free(res.url);
                    log_finish();
                    return 1;
                }
            } else {
                // Print to stdout
                if (res.local_path) {
                    char* content = read_text_file(res.local_path);
                    if (content) {
                        printf("%s", content);
                        mem_free(content);
                    }
                }
            }

            mem_free(res.url);
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

            mem_free(res.url);
            log_finish();
            return 1;
        }
    }

    // Handle test-batch command: run multiple scripts in one process for test performance
    if (argc >= 2 && strcmp(argv[1], "test-batch") == 0) {
        log_debug("Entering test-batch command handler");

        bool use_mir = true;
        int batch_timeout = 60; // default per-script timeout in seconds
        for (int i = 2; i < argc; i++) {
#ifdef LAMBDA_C2MIR
            if (strcmp(argv[i], "--c2mir") == 0) {
                use_mir = false;
            } else
#endif
            if (strcmp(argv[i], "--no-log") == 0) {
                // already handled early in main()
            } else if (strncmp(argv[i], "--timeout=", 10) == 0) {
                batch_timeout = atoi(argv[i] + 10);
                if (batch_timeout <= 0) batch_timeout = 60;
            }
        }

        char line[1024];
        while (fgets(line, sizeof(line), stdin)) {
            // trim trailing whitespace
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' '))
                line[--len] = '\0';
            if (len == 0) continue;

            // parse optional "run " prefix for procedural scripts
            bool run_main = false;
            char* script_path = line;
            if (strncmp(line, "run ", 4) == 0) {
                run_main = true;
                script_path += 4;
                while (*script_path == ' ') script_path++;
            }
            if (*script_path == '\0') continue;

            printf("\x01" "BATCH_START %s\n", script_path);
            fflush(stdout);

            if (!file_exists(script_path)) {
                fprintf(stderr, "Error: Script file '%s' does not exist\n", script_path);
                fflush(stdout);
                printf("\x01" "BATCH_END 1\n");
                fflush(stdout);
                continue;
            }

            int result;
#ifndef _WIN32
            if (batch_timeout > 0) {
                struct sigaction sa, old_sa;
                sa.sa_handler = batch_alarm_handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;
                sigaction(SIGALRM, &sa, &old_sa);
                batch_timeout_active = 1;
                if (sigsetjmp(batch_timeout_jmp, 1) == 0) {
                    alarm(batch_timeout);
                    result = run_script_file(&runtime, script_path, use_mir, false, run_main);
                    alarm(0);
                    batch_timeout_active = 0;
                } else {
                    // timed out
                    fprintf(stderr, "Timeout: script '%s' exceeded %ds limit\n", script_path, batch_timeout);
                    result = 124; // same as coreutils timeout exit code
                }
                sigaction(SIGALRM, &old_sa, NULL);
            } else {
                result = run_script_file(&runtime, script_path, use_mir, false, run_main);
            }
#else
            result = run_script_file(&runtime, script_path, use_mir, false, run_main);
#endif
            fflush(stdout);

            printf("\x01" "BATCH_END %d\n", result);
            fflush(stdout);

            // Reset heap/nursery/name_pool so the next test starts clean.
            // Must happen BEFORE pool_destroy: name_pool was allocated from
            // the script pool, so pool_destroy would unmap its memory.
            runtime_reset_heap(&runtime);

            // Reset path scheme roots. The scheme_roots[] global stores
            // Path* pointers allocated from the heap pool, which was just
            // destroyed by runtime_reset_heap. Without this reset the next
            // script's path_get_root() would use dangling pointers, leading
            // to corrupted path traversal (infinite loop / SIGSEGV).
            path_reset();

            // Clean up accumulated scripts to prevent state corruption across runs.
            // Keep the parser (expensive to recreate) but free all script data.
            if (runtime.scripts) {
                for (int i = 0; i < runtime.scripts->length; i++) {
                    Script *script = (Script*)runtime.scripts->data[i];
                    if (script->source) mem_free((void*)script->source);
                    if (script->syntax_tree) ts_tree_delete(script->syntax_tree);
                    if (script->pool) pool_destroy(script->pool);
                    if (script->type_list) arraylist_free(script->type_list);
                    if (script->jit_context) jit_cleanup(script->jit_context);
                    script->decimal_ctx = NULL;
                    mem_free(script);
                }
                runtime.scripts->length = 0;
            }
        }

        runtime_cleanup(&runtime);
        log_finish();
        return 0;
    }

    // Handle js-test-batch command: run multiple JS scripts in one process for test performance
    if (argc >= 2 && strcmp(argv[1], "js-test-batch") == 0) {
        // batch mode always disables logging for maximum throughput
        log_disable_all();

        int batch_timeout = 10; // default per-script timeout in seconds
        bool hot_reload = true; // persistent heap between tests (default: on)
        for (int i = 2; i < argc; i++) {
            if (strncmp(argv[i], "--timeout=", 10) == 0) {
                batch_timeout = atoi(argv[i] + 10);
                if (batch_timeout <= 0) batch_timeout = 10;
            } else if (strcmp(argv[i], "--no-hot-reload") == 0) {
                hot_reload = false;
            } else if (strncmp(argv[i], "--opt-level=", 12) == 0) {
                int level = atoi(argv[i] + 12);
                if (level >= 0 && level <= 3) g_js_mir_optimize_level = (unsigned int)level;
            } else if (strcmp(argv[i], "--mir-interp") == 0) {
                g_mir_interp_mode = 1;
            }
        }

        Runtime runtime;
        runtime_init(&runtime);
        lambda_stack_init();

#ifndef _WIN32
        // Set up alternate signal stack for SIGSEGV/SIGBUS recovery.
        // Without this, stack overflow crashes kill the process because the
        // signal handler tries to run on the same overflowed stack and triggers
        // a double fault. With sigaltstack, the handler runs on a separate
        // allocation and can safely siglongjmp back to the recovery point.
        static char alt_stack_mem[131072];  // 128KB generous alt stack
        stack_t alt_stack;
        alt_stack.ss_sp = alt_stack_mem;
        alt_stack.ss_size = sizeof(alt_stack_mem);
        alt_stack.ss_flags = 0;
        sigaltstack(&alt_stack, NULL);
#endif

        // Set up a persistent EvalContext with pre-initialized heap (hot reload mode).
        // Enables the reusing_context fast-path in transpile_js_to_mir,
        // avoiding heap/nursery/name_pool teardown+recreation between tests.
        EvalContext batch_context;
        memset(&batch_context, 0, sizeof(EvalContext));
        if (hot_reload) {
            context = &batch_context;
            batch_context.nursery = gc_nursery_create(0);
            heap_init();
            batch_context.pool = batch_context.heap->pool;
            batch_context.name_pool = name_pool_create(batch_context.pool, nullptr);
            batch_context.type_list = arraylist_new(64);
        }

        // Preamble state for two-module MIR split (harness compiled once per batch)
        JsPreambleState preamble;
        memset(&preamble, 0, sizeof(preamble));
        bool has_preamble = false;
        int preamble_var_checkpoint = 0;
        int batch_crash_count = 0;
        int batch_test_count = 0;  // diagnostic: track how many tests processed
        bool batch_in_test = false; // true between BATCH_START and BATCH_END (for crash recovery)
        char* saved_harness_src = NULL;  // kept for recompilation after crash recovery

#ifndef _WIN32
        // Install signal handlers ONCE for the entire batch loop.
        // Catches SIGSEGV, SIGBUS, SIGABRT, and SIGTRAP.
        extern MIR_error_func_t g_batch_mir_error_handler;
        g_batch_mir_error_handler = batch_mir_error_handler;
        struct sigaction crash_sa, old_segv_sa, old_bus_sa, old_abrt_sa, old_trap_sa;
        crash_sa.sa_handler = batch_crash_handler;
        sigemptyset(&crash_sa.sa_mask);
        crash_sa.sa_flags = SA_ONSTACK;
        sigaction(SIGSEGV, &crash_sa, &old_segv_sa);
        sigaction(SIGBUS, &crash_sa, &old_bus_sa);
        sigaction(SIGABRT, &crash_sa, &old_abrt_sa);
        sigaction(SIGTRAP, &crash_sa, &old_trap_sa);
#endif

        char line[4096];
        while (fgets(line, sizeof(line), stdin)) {
            // trim trailing whitespace
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' '))
                line[--len] = '\0';
            if (len == 0) continue;

            // Handle harness protocol: harness:<length>
            // Compiles harness source once as preamble; function objects persist as module vars.
            if (strncmp(line, "harness:", 8) == 0) {
                size_t harness_len = (size_t)atol(line + 8);
                if (harness_len == 0 || harness_len > 10 * 1024 * 1024) continue;
                char* harness_src = (char*)mem_alloc(harness_len + 1, MEM_CAT_SYSTEM);
                if (!harness_src) continue;
                size_t total_read = 0;
                while (total_read < harness_len) {
                    size_t n = fread(harness_src + total_read, 1, harness_len - total_read, stdin);
                    if (n == 0) break;
                    total_read += n;
                }
                harness_src[total_read] = '\0';
                int ch = fgetc(stdin);
                if (ch != '\n' && ch != EOF) ungetc(ch, stdin);

                // Destroy any previous preamble state
                if (has_preamble) {
                    preamble_state_destroy(&preamble);
                    has_preamble = false;
                }

                memset(&preamble, 0, sizeof(preamble));
                Item pres = transpile_js_to_mir_preamble(&runtime, harness_src, "<harness>", &preamble);

                // Save harness source for recompilation after crash recovery
                if (saved_harness_src) mem_free(saved_harness_src);
                saved_harness_src = harness_src;  // take ownership instead of freeing

                if (preamble.mir_ctx) {
                    has_preamble = true;
                    preamble_var_checkpoint = preamble.module_var_count;
                }
                continue;
            }

            char* script_path = line;
            if (*script_path == '\0') continue;

            // support inline source protocol: source:<name>:<length>
            // reads <length> bytes of JS source directly from stdin
            char* js_source = NULL;
            bool inline_source = false;
            if (strncmp(line, "source:", 7) == 0) {
                // parse source:<name>:<length>
                char* name_start = line + 7;
                char* colon = strrchr(name_start, ':');
                if (!colon) continue;
                *colon = '\0';
                script_path = name_start;
                size_t source_len = (size_t)atol(colon + 1);
                if (source_len == 0 || source_len > 10 * 1024 * 1024) continue; // sanity check
                js_source = (char*)mem_alloc(source_len + 1, MEM_CAT_SYSTEM);
                if (!js_source) continue;
                size_t total_read = 0;
                while (total_read < source_len) {
                    size_t n = fread(js_source + total_read, 1, source_len - total_read, stdin);
                    if (n == 0) break;
                    total_read += n;
                }
                js_source[total_read] = '\0';
                // consume trailing newline after source blob
                int ch = fgetc(stdin);
                if (ch != '\n' && ch != EOF) ungetc(ch, stdin);
                inline_source = true;
            }

            batch_test_count++;
            batch_in_test = true;
            printf("\x01" "BATCH_START %s\n", script_path);
            fflush(stdout);

            size_t rss_before = get_rss_bytes();
            struct timeval tv_start, tv_end;
            gettimeofday(&tv_start, NULL);

            if (!inline_source) {
                if (!file_exists(script_path)) {
                    fprintf(stderr, "Error: Script file '%s' does not exist\n", script_path);
                    printf("\x01" "BATCH_END 1 0\n");
                    fflush(stdout);
                    continue;
                }

                js_source = read_text_file(script_path);
                if (!js_source) {
                    fprintf(stderr, "Error: Could not read file '%s'\n", script_path);
                    printf("\x01" "BATCH_END 1 0\n");
                    fflush(stdout);
                    continue;
                }
            }

            int result = 0;
#ifndef _WIN32
            // Per-test crash recovery via sigsetjmp.
            // Signal handlers are installed once before the while loop.
            // batch_crash_active remains 1 throughout the loop for
            // between-test crash protection.
            batch_crash_active = 1;

            int crash_sig = sigsetjmp(batch_crash_jmp, 1);
            if (crash_sig != 0) {
                // Recovered from crash signal (handler set batch_crash_active=0; re-enable)
                batch_crash_active = 1;
                if (!batch_in_test) {
                    // Crash happened between tests (in cleanup code).
                    // Exit the batch — remaining tests will be retried individually.
                    printf("\x01" "BATCH_EXIT between_test_crash signal=%d tests=%d\n",
                            crash_sig, batch_test_count);
                    fflush(stdout);
                    break;
                }
                fprintf(stderr, "Crash: script '%s' caught signal %d (recovered)\n", script_path, crash_sig);
                alarm(0);
                batch_timeout_active = 0;
                mir_error_active = 0;
                result = 128 + crash_sig;
            } else if (batch_timeout > 0) {
                struct sigaction sa, old_sa;
                sa.sa_handler = batch_alarm_handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;
                sigaction(SIGALRM, &sa, &old_sa);
                batch_timeout_active = 1;
                mir_error_active = 1;
                if (setjmp(mir_error_jmp) == 0) {
                    if (sigsetjmp(batch_timeout_jmp, 1) == 0) {
                        alarm(batch_timeout);
                        Item res = has_preamble
                            ? transpile_js_to_mir_with_preamble(&runtime, js_source, script_path, &preamble)
                            : transpile_js_to_mir(&runtime, js_source, script_path);
                        alarm(0);
                        batch_timeout_active = 0;
                        mir_error_active = 0;
                        if (res.item == ITEM_ERROR || js_check_exception()) {
                            result = 1;
                        }
                    } else {
                        // timed out
                        mir_error_active = 0;
                        fprintf(stderr, "Timeout: script '%s' exceeded %ds limit\n", script_path, batch_timeout);
                        result = 124;
                    }
                } else {
                    // MIR error recovery
                    alarm(0);
                    batch_timeout_active = 0;
                    result = 1;
                }
                sigaction(SIGALRM, &old_sa, NULL);
            } else {
                mir_error_active = 1;
                if (setjmp(mir_error_jmp) == 0) {
                    Item res = has_preamble
                        ? transpile_js_to_mir_with_preamble(&runtime, js_source, script_path, &preamble)
                        : transpile_js_to_mir(&runtime, js_source, script_path);
                    mir_error_active = 0;
                    if (res.item == ITEM_ERROR || js_check_exception()) {
                        result = 1;
                    }
                } else {
                    // MIR error recovery
                    result = 1;
                }
            }
#else
            Item res = has_preamble
                ? transpile_js_to_mir_with_preamble(&runtime, js_source, script_path, &preamble)
                : transpile_js_to_mir(&runtime, js_source, script_path);
            if (res.item == ITEM_ERROR || js_check_exception()) {
                result = 1;
            }
#endif
            mem_free(js_source);
            fflush(stdout);

            // Print uncaught exception to stdout for batch capture
            if (result == 1 && js_check_exception()) {
                const char* exc_msg = js_get_exception_message();
                if (exc_msg[0]) printf("Uncaught %s\n", exc_msg);
                js_clear_exception();
                fflush(stdout);
            }

            gettimeofday(&tv_end, NULL);
            long elapsed_us = (tv_end.tv_sec - tv_start.tv_sec) * 1000000L + (tv_end.tv_usec - tv_start.tv_usec);
            size_t rss_after = get_rss_bytes();
            printf("\x01" "BATCH_END %d %ld %zu %zu\n", result, elapsed_us, rss_before, rss_after);
            fflush(stdout);
            batch_in_test = false;

            // Memory management: each SIGSEGV/SIGBUS crash recovery via longjmp
            // leaks ~55MB (MIR code pages, AST, temporaries that skip cleanup).
            // After too many crashes, exit the batch to prevent OOM.
            // Also exit if RSS exceeds the limit (from cumulative leaks).
            // Raised from 3/512MB to 10/4GB to reduce batch-lost partial passes:
            // batches with 3-9 crashers now survive instead of dying early.
            // 4GB per worker is safe: 12 workers × 4GB = 48GB theoretical max,
            // but memory-hungry tests (like RegExp CharacterClassEscapes) only
            // exist in a few batches, so actual peak is much lower.
            static const size_t RSS_LIMIT = 4096UL * 1024 * 1024; // 4 GB
            static const int MAX_CRASH_COUNT = 10;

            if (hot_reload) {
                // Restore context (longjmp on timeout/crash may leave it dangling)
                context = &batch_context;

                if (result == 124 || result >= 128) {
                    // Crash or timeout recovery: reset heap and continue, but
                    // track crash count to cap memory leaks from longjmp.
                    batch_crash_count++;
                    if (batch_crash_count >= MAX_CRASH_COUNT || rss_after > RSS_LIMIT) {
                        // Too many crashes or RSS too high — exit batch.
                        // Use \x01 protocol so the parent can capture this diagnostic.
                        printf("\x01" "BATCH_EXIT crash_count=%d limit=%d RSS=%zuMB limit=%zuMB tests=%d\n",
                                batch_crash_count, MAX_CRASH_COUNT,
                                rss_after / (1024*1024), RSS_LIMIT / (1024*1024), batch_test_count);
                        fflush(stdout);
                        break;
                    }
                    js_batch_reset();
                    heap_destroy();
                    // Clean up deferred MIR contexts from previous tests — heap objects
                    // referencing their code pages are now gone after heap_destroy().
                    jm_cleanup_deferred_mir();
                    if (batch_context.nursery) gc_nursery_destroy(batch_context.nursery);
                    memset(&batch_context, 0, sizeof(EvalContext));
                    context = &batch_context;
                    batch_context.nursery = gc_nursery_create(0);
                    heap_init();
                    batch_context.pool = batch_context.heap->pool;
                    batch_context.name_pool = name_pool_create(batch_context.pool, nullptr);
                    batch_context.type_list = arraylist_new(64);

                    // Heap destroyed — recompile preamble for subsequent tests.
                    if (has_preamble) {
                        preamble_state_destroy(&preamble);
                        has_preamble = false;
                    }
                    if (saved_harness_src) {
                        memset(&preamble, 0, sizeof(preamble));
                        Item pres = transpile_js_to_mir_preamble(&runtime, saved_harness_src, "<harness>", &preamble);
                        if (pres.item != ITEM_ERROR) {
                            has_preamble = true;
                            preamble_var_checkpoint = preamble.module_var_count;
                        }
                    }
                } else if (rss_after > RSS_LIMIT) {
                    // Normal test but RSS too high — exit to prevent OOM.
                    printf("\x01" "BATCH_EXIT rss_exceeded RSS=%zuMB limit=%zuMB tests=%d\n",
                            rss_after / (1024*1024), RSS_LIMIT / (1024*1024), batch_test_count);
                    fflush(stdout);
                    break;
                } else if (has_preamble) {
                    js_batch_reset_to(preamble_var_checkpoint);
                } else {
                    js_batch_reset();
                }
            } else {
                // Normal mode: transpile_js_to_mir created/destroyed its own heap.
                // Just reset JS runtime state between tests.
                js_batch_reset();
                runtime_reset_heap(&runtime);
            }
        }

#ifndef _WIN32
        // Restore signal handlers after loop exit
        batch_crash_active = 0;
        sigaction(SIGSEGV, &old_segv_sa, NULL);
        sigaction(SIGBUS, &old_bus_sa, NULL);
        sigaction(SIGABRT, &old_abrt_sa, NULL);
        sigaction(SIGTRAP, &old_trap_sa, NULL);
#endif

        // Diagnostic: log if the batch exited the while loop unexpectedly
        if (feof(stdin)) {
            // Normal completion — stdin fully consumed
        } else if (ferror(stdin)) {
            printf("\x01" "BATCH_DIAG stdin_error=%d tests=%d\n", ferror(stdin), batch_test_count);
            fflush(stdout);
        } else {
            printf("\x01" "BATCH_DIAG unexpected_exit tests=%d\n", batch_test_count);
            fflush(stdout);
        }

        // Clean up deferred MIR contexts from test runs (must happen before heap teardown)
        jm_cleanup_deferred_mir();

        // Clean up preamble MIR context (must happen before heap teardown)
        if (has_preamble) {
            preamble_state_destroy(&preamble);
            has_preamble = false;
        }
        if (saved_harness_src) { mem_free(saved_harness_src); saved_harness_src = NULL; }

        // tear down persistent heap (hot reload mode only)
        if (hot_reload) {
            context = &batch_context;
            heap_destroy();
            if (batch_context.nursery) gc_nursery_destroy(batch_context.nursery);
            context = NULL;
        }

        runtime_cleanup(&runtime);
        return 0;
    }

    // Handle --emit-sexpr command (Phase 4: Redex baseline verification bridge)
    if (argc >= 3 && strcmp(argv[1], "--emit-sexpr") == 0) {
        const char* sexpr_path = argv[2];
        log_debug("Emitting s-expressions for '%s'", sexpr_path);
        int result = emit_sexpr_file(sexpr_path);
        log_finish();
        return result;
    }

    // Handle run command
    log_debug("Checking for run command");
    if (argc >= 2 && strcmp(argv[1], "run") == 0) {
        log_debug("Entering run command handler");

        // Check for help first
        if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
            printf("Lambda Script Runner v1.0\n\n");
#ifdef LAMBDA_C2MIR
            printf("Usage: %s run [--c2mir] <script>\n", argv[0]);
            printf("\nOptions:\n");
            printf("  --c2mir        Use C2MIR JIT compilation (default: MIR Direct)\n");
#else
            printf("Usage: %s run <script>\n", argv[0]);
            printf("\nOptions:\n");
#endif
            printf("  -h, --help     Show this help message\n");
            printf("\nDescription:\n");
            printf("  The 'run' command executes a Lambda script with run_main context enabled.\n");
            printf("  This means that if the script defines a main function, it will be\n");
            printf("  automatically executed during script execution.\n");
            printf("\nExamples:\n");
            printf("  %s run script.ls                 # Run script with MIR Direct JIT (default)\n", argv[0]);
#ifdef LAMBDA_C2MIR
            printf("  %s run --c2mir script.ls         # Run script with C2MIR JIT compilation\n", argv[0]);
#endif
            log_finish();  // Cleanup logging before exit
            return 0;
        }

        // Parse run command arguments
        bool use_mir = true;  // MIR Direct is default
        char* script_file = NULL;

        for (int i = 2; i < argc; i++) {
#ifdef LAMBDA_C2MIR
            if (strcmp(argv[i], "--c2mir") == 0) {
                use_mir = false;
            } else if (strcmp(argv[i], "--transpile-dir") == 0) {
                if (i + 1 < argc) {
                    runtime.transpile_dir = argv[++i];
                    use_mir = false;  // --transpile-dir is for C code inspection: force C2MIR
                } else {
                    printf("Error: --transpile-dir requires a directory argument\n");
                    log_finish();
                    return 1;
                }
            } else
#endif
            if (strcmp(argv[i], "--mir") == 0) {
                use_mir = true;  // backward compat (already default)
            } else if (strcmp(argv[i], "--mir-interp") == 0) {
                g_mir_interp_mode = 1;
            } else if (strcmp(argv[i], "--no-log") == 0) {
                // already handled early in main()
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
#ifdef LAMBDA_C2MIR
            printf("Usage: %s run [--c2mir] <script>\n", argv[0]);
#else
            printf("Usage: %s run <script>\n", argv[0]);
#endif
            log_finish();
            return 1;
        }

        // Check if script file exists
        if (!file_exists(script_file)) {
            printf("Error: Script file '%s' does not exist\n", script_file);
            log_finish();
            return 1;
        }

        log_debug("Running script '%s' with run_main=true, use_mir=%s", script_file, use_mir ? "true" : "false");

        // Execute script with run_main enabled
        int result = run_script_file(&runtime, script_file, use_mir, false, true);  // true for run_main

        runtime_cleanup(&runtime);
        log_finish();
        return result;
    }

    bool use_mir = true;  // MIR Direct is default
    bool transpile_only = false;
    bool help_only = false;
    char* script_file = NULL;
    int max_errors = 0;  // 0 means use default (10)
    int optimize_level = -1;  // -1 means use default (2)

    // Parse arguments
    int ret_code = 0;
    for (int i = 1; i < argc; i++) {
#ifdef LAMBDA_C2MIR
        if (strcmp(argv[i], "--c2mir") == 0) {
            use_mir = false;
        }
        else if (strcmp(argv[i], "--transpile-only") == 0) {
            transpile_only = true;
        }
        else if (strcmp(argv[i], "--transpile-dir") == 0) {
            if (i + 1 < argc) {
                runtime.transpile_dir = argv[++i];
                use_mir = false;  // --transpile-dir is for C code inspection: force C2MIR
            } else {
                printf("Error: --transpile-dir requires a directory argument\n");
                help_only = true;
                ret_code = 1;
            }
        }
        else
#endif
        if (strcmp(argv[i], "--mir") == 0) {
            use_mir = true;  // backward compat (already default)
        }
        else if (strcmp(argv[i], "--mir-interp") == 0) {
            g_mir_interp_mode = 1;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            help_only = true;
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
        else if (strcmp(argv[i], "--no-log") == 0) {
            // already handled early in main()
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

    // Clean up runtime (dumps profiling data if LAMBDA_PROFILE=1)
    runtime_cleanup(&runtime);

    // Note: memtrack_shutdown is called via atexit handler

    // Clean up rpmalloc (release thread and global state)
    mempool_cleanup();

    log_finish();
    return ret_code;
}
