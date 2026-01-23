
#include "../lib/strbuf.h"
#include <tree_sitter/api.h>
#ifndef _WIN32
#include <unistd.h>  // for isatty()
#else
#include <io.h>      // for _isatty() on Windows
#include <fcntl.h>   // for file descriptor constants
#include <windows.h> // for Windows console functions
#define isatty _isatty
// Don't redefine if already defined
#ifndef STDIN_FILENO
#define STDIN_FILENO _fileno(stdin)
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO _fileno(stdout)
#endif
#endif
#include <signal.h>  // for signal handling
#include <setjmp.h>  // for setjmp/longjmp

// Include our custom command line editor
#include "../lib/cmdedit.h"

// Forward declarations for Tree-sitter parsing
extern "C" {
    TSParser* lambda_parser(void);
    TSTree* lambda_parse_source(TSParser* parser, const char* source);
}

// Result of checking statement completeness
enum StatementStatus {
    STMT_COMPLETE,      // statement is syntactically complete
    STMT_INCOMPLETE,    // statement needs more input (missing closing braces, etc.)
    STMT_ERROR          // statement has a syntax error
};

// Helper: count unclosed brackets/parens in source
// Returns true if there are unclosed brackets (meaning incomplete)
static bool has_unclosed_brackets(const char* source) {
    int brace_count = 0;   // { }
    int paren_count = 0;   // ( )
    int bracket_count = 0; // [ ]
    bool in_string = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    char string_char = 0;
    
    for (const char* p = source; *p; p++) {
        // handle comments
        if (!in_string) {
            if (!in_block_comment && p[0] == '/' && p[1] == '/') {
                in_line_comment = true;
                p++;
                continue;
            }
            if (!in_line_comment && p[0] == '/' && p[1] == '*') {
                in_block_comment = true;
                p++;
                continue;
            }
            if (in_block_comment && p[0] == '*' && p[1] == '/') {
                in_block_comment = false;
                p++;
                continue;
            }
            if (in_line_comment && *p == '\n') {
                in_line_comment = false;
                continue;
            }
        }
        
        if (in_line_comment || in_block_comment) continue;
        
        // handle strings
        if (!in_string && (*p == '"' || *p == '\'')) {
            in_string = true;
            string_char = *p;
            continue;
        }
        if (in_string) {
            if (*p == '\\' && p[1]) {
                p++;  // skip escaped character
                continue;
            }
            if (*p == string_char) {
                in_string = false;
            }
            continue;
        }
        
        // count brackets
        switch (*p) {
            case '{': brace_count++; break;
            case '}': brace_count--; break;
            case '(': paren_count++; break;
            case ')': paren_count--; break;
            case '[': bracket_count++; break;
            case ']': bracket_count--; break;
        }
    }
    
    // if still in string or comment, that's incomplete
    if (in_string || in_block_comment) return true;
    
    // if any bracket count is positive, we have unclosed brackets
    return (brace_count > 0 || paren_count > 0 || bracket_count > 0);
}

// Helper: recursively check for MISSING nodes (but not ERROR nodes)
static bool has_missing_nodes(TSNode node) {
    // check if this node is missing (parser-inserted expected token)
    if (ts_node_is_missing(node)) {
        return true;
    }
    
    // recurse into children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (has_missing_nodes(child)) {
            return true;
        }
    }
    
    return false;
}

// Check if a statement is complete, incomplete (needs continuation), or has error
StatementStatus check_statement_completeness(TSParser* parser, const char* source) {
    if (!source || !*source) {
        return STMT_COMPLETE;  // empty input is "complete"
    }
    
    // First, do a quick lexical check for unclosed brackets
    // This catches incomplete statements that Tree-sitter would report as ERROR
    if (has_unclosed_brackets(source)) {
        return STMT_INCOMPLETE;
    }
    
    // Now use Tree-sitter for more sophisticated checking
    TSTree* tree = lambda_parse_source(parser, source);
    if (!tree) {
        return STMT_ERROR;
    }
    
    TSNode root = ts_tree_root_node(tree);
    
    // If no errors at all, statement is complete
    if (!ts_node_has_error(root)) {
        ts_tree_delete(tree);
        return STMT_COMPLETE;
    }
    
    // Tree has errors - check if there are MISSING nodes (incomplete)
    if (has_missing_nodes(root)) {
        ts_tree_delete(tree);
        return STMT_INCOMPLETE;
    }
    
    // ERROR nodes without MISSING nodes = syntax error
    ts_tree_delete(tree);
    return STMT_ERROR;
}

// Get the continuation prompt for multi-line input
const char* get_continuation_prompt() {
    return ".. ";
}



// Initialize command line editor
int lambda_repl_init() {
    // Use our custom cmdedit which handles all platforms
    return repl_init();  // Our cmdedit's repl_init function
}

// Clean up command line editor
void lambda_repl_cleanup() {
    repl_cleanup();  // Our cmdedit's cleanup function
}

void print_help() {
    printf("Lambda Script Interpreter v1.0\n");
    printf("Usage:\n");
    printf("  lambda                       - Start REPL mode (default)\n");
    printf("  lambda [script.ls]           - Run a script file\n");
    printf("  lambda --mir [script.ls]     - Run with MIR JIT compilation\n");
    printf("  lambda --transpile-only [script.ls] - Transpile to C code only (no execution)\n");
    printf("  lambda --max-errors N [script.ls]   - Set max type errors before stopping (default: 10)\n");
    printf("  lambda run [--mir] <script.ls>      - Run script with main function execution\n");
    printf("  lambda validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  lambda convert <input> -f <from> -t <to> -o <output>  - Convert between formats\n");
    printf("  lambda layout <file.html>    - Analyze HTML/CSS layout structure\n");
    printf("  lambda render <input.html> -o <output.svg|pdf|png|jpg>  - Render HTML to SVG/PDF/PNG/JPEG\n");
    printf("  lambda view [file.pdf|file.html]  - Open PDF or HTML document in viewer (default: test/html/index.html)\n");
    printf("  lambda fetch <url> [-o file]  - Fetch HTTP/HTTPS resource\n");
    printf("  lambda --help                - Show this help message\n");
    printf("\nScript Options:\n");
    printf("  --mir                        - Use MIR JIT compilation instead of interpreter\n");
    printf("  --transpile-only             - Transpile to C code without execution\n");
    printf("  --max-errors N               - Stop after N type errors (default: 10, 0 = unlimited)\n");
    printf("\nScript Commands:\n");
    printf("  run [--mir] <script>         - Execute script with run_main enabled\n");
    printf("                               - This automatically runs the main function if defined\n");
    printf("\nREPL Commands:\n");
    printf("  .quit, .q, .exit     - Exit REPL\n");
    printf("  .help, .h            - Show help\n");
    printf("  .clear               - Clear REPL history\n");
    printf("\nValidation Commands:\n");
    printf("  validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  validate <file>                 - Validate using doc_schema.ls (default)\n");
    printf("\nConversion Commands:\n");
    printf("  convert <input> -f <from> -t <to> -o <output>  - Convert between formats\n");
    printf("  convert <input> -t <to> -o <output>           - Auto-detect input format\n");
    printf("\nLayout Commands:\n");
    printf("  layout <file.html>             - Analyze HTML/CSS layout and display view tree\n");
    printf("\nRendering Commands:\n");
    printf("  render <input.html> -o <output.svg|pdf|png|jpg>  - Layout HTML and render to SVG/PDF/PNG/JPEG format\n");
    printf("\nViewer Commands:\n");
    printf("  view <file.pdf>       - Open PDF document in interactive viewer window\n");
    printf("  view <file.html>      - Open HTML document in interactive browser window\n");
    printf("\nNetwork Commands:\n");
    printf("  fetch <url>           - Fetch URL and print to stdout\n");
    printf("  fetch <url> -o file   - Fetch URL and save to file\n");
    printf("  fetch <url> -v        - Fetch with verbose progress output\n");
}

// Function to determine the best REPL prompt based on system capabilities
const char* get_repl_prompt() {
#ifdef _WIN32
    // On Windows 10+ with UTF-8 support, use lambda symbol
    // SetConsoleOutputCP(CP_UTF8) is called in terminal_init
    return "λ> ";
#else
    // On Unix-like systems, UTF-8 is usually supported
    // Check if LANG/LC_ALL suggests UTF-8 support
    const char* lang = getenv("LANG");
    const char* lc_all = getenv("LC_ALL");

    if ((lang && strstr(lang, "UTF-8")) || (lc_all && strstr(lc_all, "UTF-8"))) {
        return "λ> ";
    } else {
        // Fallback to just '>'
        return "> ";
    }
#endif
}

char *lambda_repl_readline(const char *prompt) {
    // Use our custom cmdedit which handles all platforms uniformly
    return repl_readline(prompt);
}

int lambda_repl_add_history(const char *line) {
    // Use our custom cmdedit history function
    return repl_add_history(line);
}
