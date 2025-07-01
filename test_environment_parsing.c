// Test example showing expected LaTeX environment parsing behavior
// This demonstrates how \begin{enumerate} should be parsed as <enumerate>

#include <stdio.h>

void demonstrate_expected_parsing() {
    printf("LaTeX Environment Parsing Demonstration\n");
    printf("======================================\n\n");
    
    printf("Original LaTeX:\n");
    printf("\\begin{enumerate}\n");
    printf("    \\item First numbered item\n");
    printf("    \\item Second numbered item\n");
    printf("\\end{enumerate}\n\n");
    
    printf("Expected Lambda Element Structure:\n");
    printf("<enumerate>\n");
    printf("    <item>\"First numbered item\"</item>\n");
    printf("    <item>\"Second numbered item\"</item>\n");
    printf("</enumerate>\n\n");
    
    printf("Key Changes Made to Parser:\n");
    printf("1. \\begin{ENV_NAME} creates element with tag name 'ENV_NAME'\n");
    printf("2. Environment name is no longer stored as an argument\n");
    printf("3. \\end{ENV_NAME} commands are ignored (handled implicitly)\n");
    printf("4. Content between \\begin and \\end becomes children of the environment element\n\n");
    
    printf("Example Environments that should work:\n");
    printf("- \\begin{itemize} -> <itemize>\n");
    printf("- \\begin{enumerate} -> <enumerate>\n");
    printf("- \\begin{equation} -> <equation>\n");
    printf("- \\begin{quote} -> <quote>\n");
    printf("- \\begin{abstract} -> <abstract>\n");
    printf("- \\begin{document} -> <document>\n");
}

void demonstrate_nested_environments() {
    printf("Nested Environment Example:\n");
    printf("==========================\n\n");
    
    printf("LaTeX:\n");
    printf("\\begin{itemize}\n");
    printf("    \\item First item\n");
    printf("    \\begin{enumerate}\n");
    printf("        \\item Nested item 1\n");
    printf("        \\item Nested item 2\n");
    printf("    \\end{enumerate}\n");
    printf("    \\item Second item\n");
    printf("\\end{itemize}\n\n");
    
    printf("Expected Structure:\n");
    printf("<itemize>\n");
    printf("    <item>\"First item\"</item>\n");
    printf("    <enumerate>\n");
    printf("        <item>\"Nested item 1\"</item>\n");
    printf("        <item>\"Nested item 2\"</item>\n");
    printf("    </enumerate>\n");
    printf("    <item>\"Second item\"</item>\n");
    printf("</itemize>\n");
}

int main() {
    demonstrate_expected_parsing();
    printf("\n");
    demonstrate_nested_environments();
    
    return 0;
}
