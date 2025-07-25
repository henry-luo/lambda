#include "transpiler.h"

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
    printf("  lambda --help                - Show this help message\n");
    printf("\nREPL Commands:\n");
    printf("  :quit, :q, :exit     - Exit REPL\n");
    printf("  :help, :h            - Show help\n");
    printf("  :clear               - Clear REPL history\n");
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
    
    // Initialize runtime
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = const_cast<char*>("./");
    
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