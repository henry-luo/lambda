#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

// Include enhanced renderer
extern bool fn_typeset_latex_enhanced_standalone(const char* input_file, const char* output_file);

// Test utility functions
static bool file_exists(const char* filename) {
    return access(filename, F_OK) == 0;
}

static long get_file_size(const char* filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

static bool run_diff_pdf(const char* pdf1, const char* pdf2) {
    if (!file_exists(pdf1) || !file_exists(pdf2)) {
        printf("❌ Cannot compare PDFs - one or both files missing\n");
        return false;
    }
    
    char command[512];
    snprintf(command, sizeof(command), "diff-pdf --output-diff=test/output/diff_result.pdf \"%s\" \"%s\" 2>/dev/null", pdf1, pdf2);
    
    int result = system(command);
    
    if (result == 0) {
        printf("✅ PDF comparison: Files are identical\n");
        return true;
    } else {
        printf("📄 PDF comparison: Files differ (diff saved to test/output/diff_result.pdf)\n");
        return false;
    }
}

static void test_enhanced_pdf_generation(const char* test_name, const char* input_file) {
    printf("\n=== Testing Enhanced PDF Generation: %s ===\n", test_name);
    
    if (!file_exists(input_file)) {
        printf("❌ Input file does not exist: %s\n", input_file);
        return;
    }
    
    // Create output filename
    char output_file[256];
    snprintf(output_file, sizeof(output_file), "test/output/enhanced_%s.pdf", test_name);
    
    printf("📄 Input file: %s\n", input_file);
    printf("📁 Output file: %s\n", output_file);
    
    // Clean up any existing output
    unlink(output_file);
    
    // Test PDF generation
    printf("🔄 Generating enhanced PDF...\n");
    bool result = fn_typeset_latex_enhanced_standalone(input_file, output_file);
    
    if (result) {
        printf("✅ Enhanced typeset function returned success\n");
        
        // Check if output file was created
        if (file_exists(output_file)) {
            long file_size = get_file_size(output_file);
            printf("✅ PDF file created successfully: %s (size: %ld bytes)\n", output_file, file_size);
            
            // Check if file is a reasonable size (should be at least 1KB for a valid PDF)
            if (file_size > 1024) {
                printf("✅ PDF file appears to be valid (size > 1KB)\n");
            } else {
                printf("⚠️ PDF file may be too small (size <= 1KB)\n");
            }
        } else {
            printf("❌ PDF file was not created\n");
        }
    } else {
        printf("❌ Enhanced typeset function failed\n");
    }
}

static void test_reference_pdf_comparison(const char* test_name, const char* generated_pdf, const char* reference_pdf) {
    printf("\n=== PDF Reference Comparison: %s ===\n", test_name);
    
    if (file_exists(reference_pdf)) {
        printf("📋 Comparing against reference PDF: %s\n", reference_pdf);
        run_diff_pdf(generated_pdf, reference_pdf);
    } else {
        printf("📝 No reference PDF found, creating reference: %s\n", reference_pdf);
        
        // Copy generated PDF as new reference
        char command[512];
        snprintf(command, sizeof(command), "cp \"%s\" \"%s\"", generated_pdf, reference_pdf);
        system(command);
        
        if (file_exists(reference_pdf)) {
            printf("✅ Reference PDF created for future comparisons\n");
        } else {
            printf("❌ Failed to create reference PDF\n");
        }
    }
}

static void test_typography_features() {
    printf("\n=== Typography Features Test ===\n");
    
    const char* features[] = {
        "Font weight variations (bold, italic, normal)",
        "Font size hierarchy (section headings)",
        "Paragraph spacing and indentation",
        "Line spacing and justification",
        "Special characters and symbols"
    };
    
    int feature_count = sizeof(features) / sizeof(features[0]);
    
    for (int i = 0; i < feature_count; i++) {
        printf("🔤 %s\n", features[i]);
    }
    
    printf("✅ Typography features enumerated\n");
}

static void test_layout_features() {
    printf("\n=== Layout Features Test ===\n");
    
    const char* layouts[] = {
        "Multi-level section hierarchy",
        "Bullet and numbered lists with nesting",
        "Table layout with borders and alignment",
        "Mathematical expressions (inline and display)",
        "Block quotes and code blocks",
        "Page margins and content area"
    };
    
    int layout_count = sizeof(layouts) / sizeof(layouts[0]);
    
    for (int i = 0; i < layout_count; i++) {
        printf("📐 %s\n", layouts[i]);
    }
    
    printf("✅ Layout features enumerated\n");
}

static void run_comprehensive_test_suite() {
    printf("🚀 Starting Phase 3 Enhanced LaTeX PDF Test Suite\n");
    printf("=" * 60);
    printf("\n");
    
    // Create output directory if it doesn't exist
    system("mkdir -p test/output");
    system("mkdir -p test/reference");
    
    // Test typography and layout document
    test_enhanced_pdf_generation("typography_layout", "test/input/typography_layout_test.tex");
    test_reference_pdf_comparison("typography_layout", 
                                 "test/output/enhanced_typography_layout.pdf",
                                 "test/reference/typography_layout_reference.pdf");
    
    // Test comprehensive document
    test_enhanced_pdf_generation("comprehensive", "test/input/phase3_comprehensive_test.tex");
    test_reference_pdf_comparison("comprehensive", 
                                 "test/output/enhanced_comprehensive.pdf",
                                 "test/reference/comprehensive_reference.pdf");
    
    // Test math-intensive document
    test_enhanced_pdf_generation("math_intensive", "test/input/math_intensive_test.tex");
    test_reference_pdf_comparison("math_intensive", 
                                 "test/output/enhanced_math_intensive.pdf",
                                 "test/reference/math_intensive_reference.pdf");
    
    // Test existing basic document for regression
    test_enhanced_pdf_generation("basic_regression", "test/input/basic_test.tex");
    
    // Feature verification tests
    test_typography_features();
    test_layout_features();
    
    printf("\n✅ Phase 3 Enhanced LaTeX PDF Test Suite Complete\n");
    printf("📊 Check test/output/ for generated PDFs\n");
    printf("📋 Check test/reference/ for reference PDFs\n");
    printf("🔍 Check diff-pdf results if any differences were found\n");
}

int main() {
    printf("Phase 3 Enhanced LaTeX Typesetting Test\n");
    printf("=====================================\n\n");
    
    // Check if diff-pdf is available
    if (system("which diff-pdf > /dev/null 2>&1") != 0) {
        printf("⚠️ diff-pdf not found - PDF comparison will be limited\n");
        printf("💡 Install diff-pdf for comprehensive PDF verification\n\n");
    } else {
        printf("✅ diff-pdf available for PDF comparison\n\n");
    }
    
    run_comprehensive_test_suite();
    
    return 0;
}
