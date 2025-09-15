// Minimal build stub implementations
// This file provides minimal implementations for functions that are not available in cross-compilation minimal builds

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // For strlen, strcmp functions
#include <stdint.h>
}

// Forward declaration for Runtime type (avoid including full lambda.h)
struct Runtime;

extern "C" {

// Simple string functions to avoid header conflicts
static int simple_strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

static size_t simple_strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

// REPL function stubs
void print_help() {
    printf("Help not available in minimal build\n");
}

const char* get_repl_prompt() {
    return "λ> ";
}

char *repl_readline(const char *prompt) {
    static char buffer[1024];
    printf("%s", prompt);
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        // Remove newline if present
        size_t len = simple_strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        return buffer;
    }
    return NULL;
}

int repl_add_history(const char *line) {
    // No history in minimal build
    return 0;
}

// Typeset function stub
bool fn_typeset_latex_standalone(const char* input_file, const char* output_file) {
    printf("LaTeX typesetting not available in minimal build\n");
    return false;
}

// Note: endian functions le16toh and be16toh are provided by endian.h

} // extern "C"

// Undefine any system macros for endian functions to allow our implementations
#ifdef le16toh
#undef le16toh
#endif
#ifdef be16toh
#undef be16toh
#endif

// Endian function implementations for tree-sitter compatibility
extern "C" {
    uint16_t le16toh(uint16_t x) {
        return x;  // x86_64 is little-endian, so no conversion needed
    }
    
    uint16_t be16toh(uint16_t x) {
        return ((x & 0xFF) << 8) | ((x & 0xFF00) >> 8);  // Swap bytes for big-endian
    }
}

// Minimal main function for cross-compilation builds
int main(int argc, char *argv[]) {
    printf("Lambda Script (Minimal Build) v1.0\n");
    printf("This is a minimal build for cross-compilation testing.\n");
    printf("Full functionality is not available.\n\n");
    
    if (argc >= 2 && (simple_strcmp(argv[1], "--help") == 0 || simple_strcmp(argv[1], "-h") == 0)) {
        print_help();
        return 0;
    }
    
    printf("Usage: %s [--help|-h]\n", argv[0]);
    printf("For full functionality, use the native build.\n");
    
    return 0;
}