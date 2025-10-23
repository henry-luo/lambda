#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "lambda/lambda.h"
}

static void test_single_css(const char* css_content, const char* description) {
    printf("\n=== Testing: %s ===\n", description);
    printf("Input CSS: %s\n", css_content);
    
    // Parse CSS
    Item parsed = lambda_parse_string(css_content, "css");
    if (parsed.item == ITEM_ERROR) {
        printf("❌ Parse failed\n");
        return;
    }
    
    // Format back to CSS
    Item formatted = lambda_format(parsed, "css");
    if (formatted.item == ITEM_ERROR) {
        printf("❌ Format failed\n");
        return;
    }
    
    String* result_str = (String*)formatted.pointer;
    if (result_str) {
        printf("Output CSS: %s\n", result_str->chars);
    } else {
        printf("❌ No output string\n");
    }
}

int main() {
    printf("Testing specific CSS issues after string merging fix...\n");
    
    // Test URL function
    test_single_css("url(\"test-image.png\")", "URL function with quoted string");
    test_single_css("url(test-image.png)", "URL function without quotes");
    
    // Test calc with operators
    test_single_css("calc(10px + 5px)", "calc function with + operator");
    test_single_css("calc(100% - 20px)", "calc function with - operator");
    
    // Test CSS variables
    test_single_css("var(--primary-color)", "CSS variable with -- prefix");
    test_single_css("var(--spacing, 10px)", "CSS variable with fallback");
    
    // Test linear gradient
    test_single_css("linear-gradient(to right, #007bff, #0056b3)", "linear-gradient function");
    
    return 0;
}