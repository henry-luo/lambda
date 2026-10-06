
#include "../lib/strbuf.h"
#include "../lib/memtrack.h"
#include "runtime/lambda-error.h"
#include <string.h>
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

#include "../lib/terminal_device.h"
#include "runtime/terminal_host.h"

static TerminalDevice* g_repl_device = nullptr;
static TerminalSession* g_repl_terminal = nullptr;

static int repl_device_raw(void* device, bool enable) {
    return terminal_device_set_raw((TerminalDevice*)device, enable);
}

static int repl_device_size(void* device, int* rows, int* columns) {
    return terminal_device_size((TerminalDevice*)device, rows, columns);
}

static int repl_device_wait(void* device, int timeout_ms) {
    return terminal_device_wait((TerminalDevice*)device, timeout_ms);
}

static int64_t repl_device_read(void* device, char* bytes, size_t capacity) {
    return terminal_device_read((TerminalDevice*)device, bytes, capacity);
}

static int64_t repl_device_write(void* device, const char* bytes, size_t length) {
    return terminal_device_write((TerminalDevice*)device, bytes, length);
}

// Only a missing/failed package mount uses this deliberately plain recovery
// path. The normal REPL never enters cmdedit's native editor loop.
static char* repl_basic_readline(const char* prompt) {
    if (isatty(STDOUT_FILENO)) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    StrBuf* line = strbuf_new_cap(128);
    if (!line) return nullptr;
    int next = 0;
    while ((next = fgetc(stdin)) != EOF && next != '\n') {
        if (next != '\r') strbuf_append_char(line, (char)next);
    }
    if (next == EOF && line->length == 0) {
        strbuf_free(line);
        return nullptr;
    }
    char* result = (char*)mem_alloc(line->length + 1, MEM_CAT_SYSTEM);
    if (result) memcpy(result, line->str, line->length + 1);
    strbuf_free(line);
    return result;
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

// Check if a statement is complete, incomplete (needs continuation), or has error
StatementStatus check_statement_completeness(const char* source) {
    if (!source || !*source) {
        return STMT_COMPLETE;  // empty input is "complete"
    }

    // First, do a quick lexical check for unclosed brackets
    // This catches incomplete statements before the direct parser reports them
    if (has_unclosed_brackets(source)) {
        return STMT_INCOMPLETE;
    }

    // The direct parser owns the definitive syntax check. Once delimiters are
    // balanced, let the evaluation path report a committed parse failure.
    return STMT_COMPLETE;
}

void print_repl_syntax_error(const char* source) {
    (void)source;
    fputs("Syntax error.\n", stderr);
}

// Get the continuation prompt for multi-line input
const char* get_continuation_prompt() {
    return ".. ";
}



// Initialize command line editor
int lambda_repl_init() {
    g_repl_device = terminal_device_open();
    if (!g_repl_device) return -1;
    int rows = 0;
    int columns = 80;
    terminal_device_size(g_repl_device, &rows, &columns);
    g_repl_terminal = terminal_session_open(
        terminal_device_is_tty(g_repl_device), columns);
    if (!g_repl_terminal) {
        terminal_device_close(g_repl_device);
        g_repl_device = nullptr;
        return -1;
    }
    return 0;
}

// Clean up command line editor
void lambda_repl_cleanup() {
    terminal_session_close(g_repl_terminal);
    g_repl_terminal = nullptr;
    terminal_device_close(g_repl_device);
    g_repl_device = nullptr;
}

void print_help() {
    printf("Lambda Script Runtime v0.3 (alpha)\n");
    printf("Usage:\n");
    printf("  lambda                       - Start REPL mode (default)\n");
    printf("  lambda <script.ls>           - Run a script file\n");
    printf("  lambda run <script.ls>              - Run script with main function execution\n");
    printf("  lambda validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  lambda convert <input> -f <from> -t <to> -o <output>  - Convert between formats\n");
    printf("  lambda layout <file.html>    - Analyze HTML/CSS layout structure\n");
    printf("  lambda render <input.html> -o <output.svg|pdf|png|jpg>  - Render HTML to SVG/PDF/PNG/JPEG\n");
    printf("  lambda view <file.pdf|file.html>  - Open PDF or HTML document in viewer (default: test/ui/doc_viewer.ls)\n");
    printf("  lambda edit <file.md|file.html|file.svg>  - Open a document in the editing application\n");
    printf("  lambda demo                  - Open the bundled document viewer demo (test/ui/doc_viewer.html)\n");
    printf("  lambda fetch <url> [-o file]  - Fetch HTTP/HTTPS resource\n");
    printf("  lambda --help                - Show this help message\n");
    printf("\nScript Options:\n");
    printf("  --max-errors N               - Stop after N type errors (default: 10, 0 = unlimited)\n");
    printf("  --no-drain                   - Return without draining spawned tasks\n");
    printf("  --optimize=N                 - MIR JIT optimization level (0=debug/stack-trace, 1=basic, 2=full)\n");
    printf("  --dry-run                    - Skip real IO; return fabricated results for network/filesystem ops\n");
    printf("  --static-warning             - Relaxed mode: report static type errors as warnings and keep running\n");
    printf("                                 (syntax errors still fail; result may contain error values)\n");
    printf("\nDiagnostic Options (global; place before the subcommand):\n");
    printf("  --mem-dump[=PATH]            - On exit, write the memory-context snapshot as JSON\n");
    printf("                                 (default: ./temp/mem_snapshot.json) and log a MEMCTX leak report\n");
    printf("\nScript Commands:\n");
    printf("  run <script>                 - Runs the main() procedure if defined\n");
    printf("\nREPL Commands:\n");
    printf("  quit, q, exit        - Exit REPL\n");
    printf("  help, h              - Show help\n");
    printf("  clear                - Clear REPL history\n");
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
    printf("  edit <file.md>        - Author a document (Markdown, HTML, SVG) and save it back\n");
    printf("  demo                  - Same as 'view test/ui/doc_viewer.html'\n");
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

TerminalReadResult lambda_repl_readline(const char *prompt) {
    // The host writes frame bytes directly; drain stdio output from the last
    // evaluation first so redirected prompts cannot overtake its result.
    fflush(stdout);
    if (!g_repl_terminal || !g_repl_device) {
        char* line = repl_basic_readline(prompt);
        return {line ? TERMINAL_READ_LINE : TERMINAL_READ_EOF, line};
    }
    TerminalTransport transport = {};
    transport.device = g_repl_device;
    transport.is_tty = terminal_device_is_tty(g_repl_device);
    transport.set_raw = repl_device_raw;
    transport.size = repl_device_size;
    transport.wait = repl_device_wait;
    transport.read = repl_device_read;
    transport.write = repl_device_write;
    return terminal_session_readline(g_repl_terminal, &transport, prompt);
}
