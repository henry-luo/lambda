
#include "../lib/strbuf.h"
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
    printf("  lambda run [--mir] <script.ls>      - Run script with main function execution\n");
    printf("  lambda validate <file> -s <schema.ls>  - Validate file against schema\n");
    printf("  lambda convert <input> -f <from> -t <to> -o <output>  - Convert between formats\n");
    printf("  lambda layout <file.html>    - Analyze HTML/CSS layout structure\n");
    printf("  lambda render <input.html> -o <output.svg|pdf|png|jpg>  - Render HTML to SVG/PDF/PNG/JPEG\n");
    printf("  lambda view [file.pdf|file.html]  - Open PDF or HTML document in viewer (default: test/html/index.html)\n");
    printf("  lambda fetch <url> [-o file]  - Fetch HTTP/HTTPS resource\n");
    printf("  lambda --help                - Show this help message\n");
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
