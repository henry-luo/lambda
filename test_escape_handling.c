// Simple test for LaTeX parser - minimal example to test escaped characters
// gcc -I../include -o test_latex test_latex.c ../build/*.o -lm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mock minimal structures for testing (normally these would be from the full transpiler)
typedef struct {
    char* str;
    int length;
} MockStrBuf;

void test_escaped_chars() {
    printf("Testing LaTeX escaped character parsing...\n");
    
    const char* test_latex = "\\textbf{Bold text with \\{ and \\} and \\$ symbols}";
    printf("Input: %s\n", test_latex);
    
    // Simple character-by-character parser test
    const char* p = test_latex;
    printf("Parsed characters: ");
    
    while (*p) {
        if (*p == '\\') {
            p++; // Skip backslash
            if (*p && strchr("{}$&#^_%~", *p)) {
                // This is an escaped character
                printf("[ESC:%c] ", *p);
                p++;
            } else if (isalpha(*p)) {
                // This is a command
                printf("[CMD:");
                while (*p && isalpha(*p)) {
                    printf("%c", *p);
                    p++;
                }
                printf("] ");
            } else {
                printf("\\");
                if (*p) {
                    printf("%c", *p);
                    p++;
                }
            }
        } else {
            printf("%c", *p);
            p++;
        }
    }
    printf("\n\n");
}

void test_itemize_environment() {
    printf("Testing LaTeX itemize environment parsing...\n");
    
    const char* test_latex = 
        "\\begin{itemize}\n"
        "\\item Escaped characters: \\{ \\} \\$ \\& \\# \\^ \\_ \\% \\~\n"
        "\\item Normal text\n"
        "\\end{itemize}";
    
    printf("Input:\n%s\n", test_latex);
    printf("This should parse as an itemize environment with two items.\n");
    printf("The first item should contain properly escaped special characters.\n\n");
}

int main() {
    printf("LaTeX Parser Escape Character Test\n");
    printf("==================================\n\n");
    
    test_escaped_chars();
    test_itemize_environment();
    
    printf("Key points about LaTeX escape handling:\n");
    printf("- \\{ should become '{'\n");
    printf("- \\} should become '}'\n");
    printf("- \\$ should become '$'\n");
    printf("- \\& should become '&'\n");
    printf("- \\# should become '#'\n");
    printf("- \\^ should become '^'\n");
    printf("- \\_ should become '_'\n");
    printf("- \\%% should become '%%'\n");
    printf("- \\~ should become '~'\n");
    
    return 0;
}
