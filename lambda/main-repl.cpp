
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

// Include readline header for line editing functionality
// Note: For Windows, we use simple fallback instead of readline to avoid DLL dependencies
#ifndef _WIN32
#include <editline/readline.h>
#include <histedit.h>
#endif

// Forward declare readline functions to avoid header conflicts (only for non-Windows)
#ifndef _WIN32
extern "C" {
    char *readline(const char *);
    int add_history(const char *);
}
#endif

// Global EditLine state (only for non-Windows systems using libedit)
#ifndef _WIN32
static EditLine *el = NULL;
static History *hist = NULL;
static volatile sig_atomic_t editline_failed = 0;
static jmp_buf editline_jmp_buf;

// Signal handler for EditLine crashes
static void editline_crash_handler(int sig) {
    (void)sig;
    editline_failed = 1;
    longjmp(editline_jmp_buf, 1);
}
#endif

// Global prompt storage for EditLine
static const char *current_prompt = "λ> ";

#ifndef _WIN32
// Prompt function for EditLine
static const char *prompt_func(EditLine *el) {
    (void)el; // Suppress unused parameter warning
    return current_prompt;
}
#endif

// Initialize libedit (non-Windows) or setup simple fallback (Windows)
int repl_init() {
    // Only try EditLine for truly interactive terminals
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        printf("Debug: Non-interactive terminal detected, using basic input\n");
        return 0;  // Success, but no EditLine
    }
    
#ifdef _WIN32
    // Windows: use simple fallback instead of readline to avoid DLL dependencies
    printf("Debug: Using simple fallback for Windows (no readline dependency)\n");
    return 0; // Success
#else
    // Check for specific terminal types that might not work well with EditLine
    const char* term = getenv("TERM");
    if (term && (strcmp(term, "dumb") == 0 || strstr(term, "emacs") != NULL)) {
        printf("Debug: Unsupported terminal type, using basic input\n");
        return 0;
    }
    
    printf("Debug: Attempting to initialize libedit...\n");
    
    // Initialize history first
    hist = history_init();
    if (!hist) {
        printf("Warning: Failed to initialize history, using basic input\n");
        return 0; // Don't fail, just use basic input
    }
    
    // Set history size
    HistEvent ev;
    if (history(hist, &ev, H_SETSIZE, 100) == -1) {
        printf("Warning: Failed to set history size\n");
        history_end(hist);
        hist = NULL;
        return 0; // Don't fail, just use basic input
    }
    
    // Initialize EditLine
    el = el_init("lambda", stdin, stdout, stderr);
    if (!el) {
        printf("Warning: Failed to initialize EditLine, using basic input\n");
        if (hist) {
            history_end(hist);
            hist = NULL;
        }
        return 0; // Don't fail, just use basic input
    }
    
    // Set the editor to emacs mode
    if (el_set(el, EL_EDITOR, "emacs") == -1) {
        printf("Warning: Failed to set emacs mode\n");
    }
    
    // Set the history
    if (el_set(el, EL_HIST, history, hist) == -1) {
        printf("Warning: Failed to set history\n");
    }
    
    printf("Debug: libedit initialized successfully\n");
    return 0;
#endif
}

// Clean up libedit
void repl_cleanup() {
#ifndef _WIN32
    if (el) {
        el_end(el);
        el = NULL;
    }
    if (hist) {
        history_end(hist);
        hist = NULL;
    }
#endif
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
}

// Function to determine the best REPL prompt based on system capabilities
const char* get_repl_prompt() {
#ifdef _WIN32
    // Simplified Windows prompt - avoid complex console API calls
    return "> ";
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

char *repl_readline(const char *prompt) {
#ifdef _WIN32
    // Windows: use simple fallback instead of readline to avoid DLL dependencies
    printf("%s", prompt);
    fflush(stdout);
    static char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        return strdup(buffer);
    }
    return NULL;
#else
    // If EditLine failed before or is not initialized, use basic input
    if (!el || editline_failed) {
        printf("%s", prompt);
        fflush(stdout);
        static char buffer[1024];
        if (fgets(buffer, sizeof(buffer), stdin)) {
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len-1] == '\n') {
                buffer[len-1] = '\0';
            }
            return strdup(buffer);
        }
        return NULL;
    }
    
    // Check if we're in an interactive terminal
    if (!isatty(STDIN_FILENO)) {
        // Not interactive, use simple fgets
        printf("%s", prompt);
        fflush(stdout);
        static char buffer[1024];
        if (fgets(buffer, sizeof(buffer), stdin)) {
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len-1] == '\n') {
                buffer[len-1] = '\0';
            }
            return strdup(buffer);
        }
        return NULL;
    }
    
    // Set up signal handler for potential crashes
    struct sigaction old_action;
    struct sigaction new_action;
    new_action.sa_handler = editline_crash_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGSEGV, &new_action, &old_action);
    
    char *result = NULL;
    
    // Use setjmp to handle potential crashes in EditLine
    if (setjmp(editline_jmp_buf) == 0) {
        // Set the prompt
        current_prompt = prompt;
        el_set(el, EL_PROMPT, prompt_func);
        
        // Get input from EditLine with error checking
        int count;
        const char *line = el_gets(el, &count);
        
        if (line && count > 0) {
            // Remove trailing newline and make a copy
            result = strdup(line);
            if (result) {
                size_t len = strlen(result);
                if (len > 0 && result[len-1] == '\n') {
                    result[len-1] = '\0';
                }
            }
        }
    } else {
        // EditLine crashed, mark it as failed
        printf("\nWarning: EditLine crashed, switching to basic input mode\n");
        editline_failed = 1;
        
        // Clean up EditLine resources
        if (el) {
            el_end(el);
            el = NULL;
        }
        if (hist) {
            history_end(hist);
            hist = NULL;
        }
        
        // Fall back to basic input
        printf("%s", prompt);
        fflush(stdout);
        static char buffer[1024];
        if (fgets(buffer, sizeof(buffer), stdin)) {
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len-1] == '\n') {
                buffer[len-1] = '\0';
            }
            result = strdup(buffer);
        }
    }
    
    // Restore original signal handler
    sigaction(SIGSEGV, &old_action, NULL);
    
    return result;
#endif
}

int repl_add_history(const char *line) {
#ifdef _WIN32
    // Windows: readline automatically handles history in repl_readline
    // This function is a no-op for Windows
    (void)line; // Suppress unused parameter warning
    return 0;
#else
    if (!hist) {
        // If history is not initialized, just return success silently
        return 0;
    }
    
    // Don't add empty lines or commands starting with '.' to history
    if (!line || strlen(line) == 0 || line[0] == '.') {
        return 0;
    }
    
    HistEvent ev;
    if (history(hist, &ev, H_ENTER, line) == -1) {
        printf("Warning: Failed to add line to history\n");
        return -1;
    }
    
    return 0;
#endif
}
