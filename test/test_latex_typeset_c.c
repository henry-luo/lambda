/**
 * @file test_latex_typeset_c.c
 * @brief Pure C test for LaTeX typeset pipeline without Lambda engine
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// External C function from typeset_latex.c
bool fn_typeset_latex_standalone(const char* input_file, const char* output_file);

// Test utility functions
static bool file_exists(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

static void cleanup_test_files(void) {
    remove("test_output.pdf");
    remove("test_output.svg");
    remove("test_output.html");
}

static bool test_standalone_pdf_generation(void) {
    printf("Testing PDF generation...\n");
    cleanup_test_files();
    
    // Create a simple LaTeX test file
    FILE* test_file = fopen("test_simple.tex", "w");
    if (!test_file) {
        fprintf(stderr, "Failed to create test file\n");
        return false;
    }
    fprintf(test_file, "\\documentclass{article}\n\\begin{document}\nHello, World!\n\\end{document}\n");
    fclose(test_file);
    
    // Test PDF generation
    bool result = fn_typeset_latex_standalone("test_simple.tex", "test_output.pdf");
    if (!result) {
        fprintf(stderr, "LaTeX typeset failed\n");
        remove("test_simple.tex");
        return false;
    }
    
    // Check if output file was created
    bool exists = file_exists("test_output.pdf");
    if (!exists) {
        fprintf(stderr, "PDF output file was not created\n");
        remove("test_simple.tex");
        return false;
    }
    
    printf("PDF generation test passed!\n");
    
    // Cleanup
    remove("test_simple.tex");
    cleanup_test_files();
    return true;
}

static bool test_standalone_svg_generation(void) {
    printf("Testing SVG generation...\n");
    cleanup_test_files();
    
    // Create a simple LaTeX test file
    FILE* test_file = fopen("test_simple.tex", "w");
    if (!test_file) {
        fprintf(stderr, "Failed to create test file\n");
        return false;
    }
    fprintf(test_file, "\\documentclass{article}\n\\begin{document}\nTest SVG\n\\end{document}\n");
    fclose(test_file);
    
    // Test SVG generation
    bool result = fn_typeset_latex_standalone("test_simple.tex", "test_output.svg");
    if (!result) {
        fprintf(stderr, "LaTeX typeset failed\n");
        remove("test_simple.tex");
        return false;
    }
    
    // Check if output file was created
    bool exists = file_exists("test_output.svg");
    if (!exists) {
        fprintf(stderr, "SVG output file was not created\n");
        remove("test_simple.tex");
        return false;
    }
    
    printf("SVG generation test passed!\n");
    
    // Cleanup
    remove("test_simple.tex");
    cleanup_test_files();
    return true;
}

static bool test_standalone_html_generation(void) {
    printf("Testing HTML generation...\n");
    cleanup_test_files();
    
    // Create a simple LaTeX test file
    FILE* test_file = fopen("test_simple.tex", "w");
    if (!test_file) {
        fprintf(stderr, "Failed to create test file\n");
        return false;
    }
    fprintf(test_file, "\\documentclass{article}\n\\begin{document}\nTest HTML\n\\end{document}\n");
    fclose(test_file);
    
    // Test HTML generation
    bool result = fn_typeset_latex_standalone("test_simple.tex", "test_output.html");
    if (!result) {
        fprintf(stderr, "LaTeX typeset failed\n");
        remove("test_simple.tex");
        return false;
    }
    
    // Check if output file was created
    bool exists = file_exists("test_output.html");
    if (!exists) {
        fprintf(stderr, "HTML output file was not created\n");
        remove("test_simple.tex");
        return false;
    }
    
    printf("HTML generation test passed!\n");
    
    // Cleanup
    remove("test_simple.tex");
    cleanup_test_files();
    return true;
}

static bool test_invalid_input(void) {
    printf("Testing invalid input handling...\n");
    cleanup_test_files();
    
    // Test with non-existent input file
    bool result = fn_typeset_latex_standalone("nonexistent.tex", "test_output.pdf");
    if (result) {
        fprintf(stderr, "Should have failed with non-existent input file\n");
        cleanup_test_files();
        return false;
    }
    
    printf("Invalid input test passed!\n");
    cleanup_test_files();
    return true;
}

static bool test_comprehensive_latex_file(void) {
    printf("Testing comprehensive LaTeX file...\n");
    cleanup_test_files();
    
    // Create a more comprehensive LaTeX test file
    FILE* test_file = fopen("test_comprehensive.tex", "w");
    if (!test_file) {
        fprintf(stderr, "Failed to create test file\n");
        return false;
    }
    fprintf(test_file, 
        "\\documentclass{article}\n"
        "\\usepackage{amsmath}\n"
        "\\title{Test Document}\n"
        "\\author{Test Author}\n"
        "\\begin{document}\n"
        "\\maketitle\n"
        "\\section{Introduction}\n"
        "This is a test document with $E = mc^2$.\n"
        "\\subsection{Mathematics}\n"
        "\\begin{equation}\n"
        "\\int_0^\\infty e^{-x} dx = 1\n"
        "\\end{equation}\n"
        "\\end{document}\n");
    fclose(test_file);
    
    // Test PDF generation with comprehensive LaTeX
    bool result = fn_typeset_latex_standalone("test_comprehensive.tex", "test_output.pdf");
    if (!result) {
        fprintf(stderr, "Comprehensive LaTeX typeset failed\n");
        remove("test_comprehensive.tex");
        return false;
    }
    
    // Check if output file was created
    bool exists = file_exists("test_output.pdf");
    if (!exists) {
        fprintf(stderr, "PDF output file was not created for comprehensive LaTeX\n");
        remove("test_comprehensive.tex");
        return false;
    }
    
    printf("Comprehensive LaTeX test passed!\n");
    
    // Cleanup
    remove("test_comprehensive.tex");
    cleanup_test_files();
    return true;
}

int main(void) {
    printf("Starting LaTeX typeset pipeline tests...\n\n");
    
    bool all_passed = true;
    
    all_passed &= test_standalone_pdf_generation();
    all_passed &= test_standalone_svg_generation();
    all_passed &= test_standalone_html_generation();
    all_passed &= test_invalid_input();
    all_passed &= test_comprehensive_latex_file();
    
    if (all_passed) {
        printf("\nAll tests passed!\n");
        return 0;
    } else {
        printf("\nSome tests failed!\n");
        return 1;
    }
}
