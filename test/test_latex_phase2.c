#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

// External C function from typeset_latex.c
extern bool fn_typeset_latex_standalone(const char* input_file, const char* output_file);

// Test utility functions
static bool file_exists(const char* filename) {
    return access(filename, F_OK) == 0;
}

static void test_pdf_generation(void) {
    printf("=== Testing PDF Generation ===\n");
    
    const char* input_file = "test/input/basic_test.tex";
    const char* output_file = "test/output/basic_test.pdf";
    
    // Check input file exists
    if (!file_exists(input_file)) {
        printf("❌ Input file does not exist: %s\n", input_file);
        return;
    }
    
    printf("📄 Input file: %s\n", input_file);
    printf("📁 Output file: %s\n", output_file);
    
    // Clean up any existing output
    unlink(output_file);
    
    // Test PDF generation
    printf("🔄 Generating PDF...\n");
    bool result = fn_typeset_latex_standalone(input_file, output_file);
    
    if (result) {
        printf("✅ Typeset function returned success\n");
        
        // Check if output file was created
        if (file_exists(output_file)) {
            printf("✅ PDF file created successfully: %s\n", output_file);
        } else {
            printf("❌ PDF file was not created\n");
        }
    } else {
        printf("❌ Typeset function failed\n");
    }
}

static void test_svg_generation(void) {
    printf("\n=== Testing SVG Generation ===\n");
    
    const char* input_file = "test/input/basic_test.tex";
    const char* output_file = "test/output/basic_test.svg";
    
    printf("📄 Input file: %s\n", input_file);
    printf("📁 Output file: %s\n", output_file);
    
    // Clean up any existing output
    unlink(output_file);
    
    // Test SVG generation
    printf("🔄 Generating SVG...\n");
    bool result = fn_typeset_latex_standalone(input_file, output_file);
    
    if (result) {
        printf("✅ Typeset function returned success\n");
        
        // Check if output file was created
        if (file_exists(output_file)) {
            printf("✅ SVG file created successfully: %s\n", output_file);
        } else {
            printf("❌ SVG file was not created\n");
        }
    } else {
        printf("❌ Typeset function failed\n");
    }
}

static void test_enhanced_document(void) {
    printf("\n=== Testing Enhanced LaTeX Document ===\n");
    
    const char* input_file = "test/input/enhanced_test.tex";
    const char* output_file = "test/output/enhanced_test.pdf";
    
    if (!file_exists(input_file)) {
        printf("❌ Enhanced test file does not exist: %s\n", input_file);
        return;
    }
    
    printf("📄 Input file: %s\n", input_file);
    printf("📁 Output file: %s\n", output_file);
    
    // Clean up any existing output
    unlink(output_file);
    
    // Test enhanced document generation
    printf("🔄 Generating enhanced PDF...\n");
    bool result = fn_typeset_latex_standalone(input_file, output_file);
    
    if (result) {
        printf("✅ Enhanced typeset function returned success\n");
        
        // Check if output file was created
        if (file_exists(output_file)) {
            printf("✅ Enhanced PDF file created successfully: %s\n", output_file);
        } else {
            printf("❌ Enhanced PDF file was not created\n");
        }
    } else {
        printf("❌ Enhanced typeset function failed\n");
    }
}

int main() {
    printf("🚀 LaTeX Typeset Phase 2 Testing\n");
    printf("==================================\n");
    
    test_pdf_generation();
    test_svg_generation();
    test_enhanced_document();
    
    printf("\n🎯 Phase 2 Testing Complete!\n");
    return 0;
}
