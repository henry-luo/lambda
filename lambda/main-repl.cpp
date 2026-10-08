
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
#include "../lib/file.h"
#include "runtime/terminal_host.h"
#include "runtime/interp.hpp"
#include <ctype.h>

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

// Zero-cost completeness fast path: true while a bracket, string, or block
// comment is still open. Balanced input goes to the session, whose C parser
// decides completeness (an INCOMPLETE status keeps the continuation prompt).
bool repl_has_unclosed_brackets(const char* source) {
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

// Get the continuation prompt for multi-line input
const char* get_continuation_prompt() {
    return ".. ";
}



// Tab completes the identifier run before the caret against the live session
// (D8.1.1v17). A member step after `.` has no static candidate list, so it
// completes nothing rather than offering unrelated top-level names.
static size_t repl_complete(void* opaque, const char* line, size_t length,
                            TerminalCompletionAdd add, void* sink) {
    size_t start = length;
    while (start > 0 && (isalnum((unsigned char)line[start - 1]) || line[start - 1] == '_')) {
        start--;
    }
    size_t word = length - start;
    if (word == 0 || (start > 0 && line[start - 1] == '.')) return 0;
    interp_repl_session_complete((InterpReplSession*)opaque, line + start, word,
        (InterpReplCompletionAdd)add, sink);
    return word;
}

// The history file: LAMBDA_REPL_HISTORY names it (empty turns it off), else
// ~/.lambda_history. Only an interactive terminal reads or writes it, so piped
// input (tests, scripts) never lands in a user's history.
static char g_repl_history_path[1024];

static const char* repl_history_path(void) {
    const char* configured = getenv("LAMBDA_REPL_HISTORY");
    if (configured) return configured[0] ? configured : NULL;
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
#else
    const char* home = getenv("HOME");
#endif
    if (!home || !home[0]) return NULL;
    snprintf(g_repl_history_path, sizeof(g_repl_history_path), "%s/.lambda_history", home);
    return g_repl_history_path;
}

// Initialize command line editor
int lambda_repl_init(InterpReplSession* session) {
    g_repl_device = terminal_device_open();
    if (!g_repl_device) return -1;
    int rows = 0;
    int columns = 80;
    terminal_device_size(g_repl_device, &rows, &columns);
    bool is_tty = terminal_device_is_tty(g_repl_device);
    g_repl_terminal = terminal_session_open(is_tty, columns);
    if (!g_repl_terminal) {
        terminal_device_close(g_repl_device);
        g_repl_device = nullptr;
        return -1;
    }
    if (is_tty) {
        // `clear` re-initializes the session in place, so the pointer holds
        terminal_session_set_completer(g_repl_terminal, repl_complete, session);
        const char* path = repl_history_path();
        char* saved = NULL;
        if (path && file_read_all(path, MEM_CAT_SYSTEM, &saved, NULL)) {
            terminal_session_load_history(g_repl_terminal, saved);
            mem_free(saved);
        }
    }
    return 0;
}

// Clean up command line editor
void lambda_repl_cleanup() {
    if (g_repl_terminal && g_repl_device && terminal_device_is_tty(g_repl_device)) {
        const char* path = repl_history_path();
        char* history = path ? terminal_session_history_text(g_repl_terminal) : NULL;
        if (history) {
            write_text_file(path, history);
            mem_free(history);
        }
    }
    terminal_session_close(g_repl_terminal);
    g_repl_terminal = nullptr;
    terminal_device_close(g_repl_device);
    g_repl_device = nullptr;
}

void print_help() {
    printf("Lambda Script Runtime v0.3 (alpha)\n");
    printf("Usage:\n");
    printf("  lambda                       - Start a functional REPL session (default)\n");
    printf("  lambda run                   - Start a procedural REPL session (entries run with effects)\n");
    printf("  lambda <script.ls>           - Run a script file\n");
    printf("  lambda run <script.ls>              - Run script with main function execution\n");
    printf("  lambda validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  lambda convert <input> -f <from> -t <to> -o <output>  - Convert between formats\n");
    printf("  lambda layout <file.html>    - Analyze HTML/CSS layout structure\n");
    printf("  lambda render <input.html> -o <output.svg|pdf|png|jpg>  - Render HTML to SVG/PDF/PNG/JPEG\n");
    printf("  lambda view [file.pdf|file.html]  - Open a document (default: bundled lambda.doc viewer)\n");
    printf("  lambda edit <file.md|file.html|file.svg>  - Open a document in the editing application\n");
    printf("  lambda demo                  - Open the bundled lambda.doc viewer with its startup splash\n");
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
    printf("  clear                - Start a new session (drops every binding)\n");
    printf("  .env                 - List the session's bindings and values\n");
    printf("  .type <expr>         - Show an expression's static type without running it\n");
    printf("  .time <entry>        - Run an entry and report its wall time\n");
    printf("  .load <file>         - Run a file as one entry (rolled back whole on failure)\n");
    printf("  .save <file>         - Write the session's accepted entries to a file\n");
    printf("  Tab                  - Complete names (interactive terminal only)\n");
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
    printf("  demo                  - Open the bundled lambda.doc viewer with its startup splash\n");
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
