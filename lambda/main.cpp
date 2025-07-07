
#include "transpiler.h"

// Forward declare readline functions to avoid header conflicts
extern "C" {
    char *readline(const char *);
    int add_history(const char *);
}
// extern "C" {}

void print_help() {
    printf("Lambda Script Interpreter\n");
    printf("Usage:\n");
    printf("  lambda [script.ls]    - Run a script file\n");
    printf("  lambda               - Start REPL mode\n");
    printf("  lambda --help        - Show this help message\n");
    printf("\nREPL Commands:\n");
    printf("  :quit, :q, :exit     - Exit REPL\n");
    printf("  :help, :h            - Show help\n");
    printf("  :clear               - Clear REPL history\n");
}

void run_repl(Runtime *runtime) {
    printf("Lambda Script REPL v1.0\n");
    printf("Type :help for commands, :quit to exit\n");
    
    StrBuf *repl_history = strbuf_new_cap(1024);
    char *line;
    int exec_count = 0;
    
    while ((line = readline("λ> ")) != NULL) {
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
        Item result = run_script(runtime, repl_history->str, script_path);
        
        // Print result
        StrBuf *output = strbuf_new_cap(256);
        print_item(output, result);
        printf("%s\n", output->str);
        strbuf_free(output);
        
        free(line);
    }
    
    strbuf_free(repl_history);
    printf("Goodbye!\n");
}

void run_script_file(Runtime *runtime, const char *script_path) {
    Item result = run_script_at(runtime, (char*)script_path);
    
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
    
    // Parse command line arguments
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        return 0;
    }
    
    // Initialize runtime
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = const_cast<char*>("./");
    
    if (argc == 1) {
        // No arguments - start REPL
        run_repl(&runtime);
    } else if (argc == 2) {
        // One argument - run script file
        run_script_file(&runtime, argv[1]);
    } else {
        // Too many arguments
        printf("Error: Too many arguments\n");
        print_help();
        runtime_cleanup(&runtime);
        return 1;
    }
    
    runtime_cleanup(&runtime);
    return 0;
}