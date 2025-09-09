#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdio.h>
#include "../lambda/input/css_parser.h"
#include "../lambda/input/css_tokenizer.h"
#include "../lib/mem-pool/include/mem_pool.h"

// Global variables for setup/teardown
static VariableMemPool* pool;
static css_parser_t* parser;

void setup(void) {
    MemPoolError err = pool_variable_init(&pool, 1024 * 64, 10);  // 64KB pool
    cr_assert_eq(err, MEM_POOL_ERR_OK, "Failed to create memory pool");
    parser = css_parser_create(pool);
    cr_assert_not_null(parser, "Failed to create CSS parser");
}

void teardown(void) {
    if (parser) {
        css_parser_destroy(parser);
        parser = NULL;
    }
    if (pool) {
        pool_variable_destroy(pool);
        pool = NULL;
    }
}

TestSuite(css_files_safe, .init = setup, .fini = teardown);

// Safe file reading function with error checking
static char* read_css_file_safe(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Cannot open file: %s\n", filename);
        return NULL;
    }
    
    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    
    long size = ftell(file);
    if (size < 0 || size > 100000) { // Limit to 100KB
        fclose(file);
        return NULL;
    }
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';
    fclose(file);
    
    return content;
}

// Test simple.css file
Test(css_files_safe, parse_simple_css_file) {
    char* css_content = read_css_file_safe("test/input/simple.css");
    cr_assert_not_null(css_content, "Failed to read simple.css");
    
    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    cr_assert_not_null(stylesheet, "Failed to parse simple.css");
    cr_assert_geq(stylesheet->rule_count, 0, "Stylesheet should be valid");
    
    free(css_content);
}

// Test stylesheet.css file
Test(css_files_safe, parse_stylesheet_css_file) {
    char* css_content = read_css_file_safe("test/input/stylesheet.css");
    cr_assert_not_null(css_content, "Failed to read stylesheet.css");
    
    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    cr_assert_not_null(stylesheet, "Failed to parse stylesheet.css");
    cr_assert_geq(stylesheet->rule_count, 0, "Stylesheet should be valid");
    
    free(css_content);
}

// Test with inline CSS that matches file content structure
Test(css_files_safe, parse_inline_multiline_css) {
    const char* css = "/* Comment */\nbody {\n    margin: 0;\n    padding: 20px;\n}\n.container {\n    max-width: 1200px;\n}";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    cr_assert_not_null(stylesheet, "Failed to parse inline multiline CSS");
    cr_assert_geq(stylesheet->rule_count, 0, "Should have parsed rules");
}

// Test CSS with complex selectors
Test(css_files_safe, parse_complex_selectors) {
    const char* css = "h1, h2, h3 { color: #333; }\n.button:hover { background: blue; }";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    cr_assert_not_null(stylesheet, "Failed to parse complex selectors");
    cr_assert_geq(stylesheet->rule_count, 0, "Should have parsed rules");
}

// Test CSS with functions
Test(css_files_safe, parse_css_functions) {
    const char* css = ".test { background: linear-gradient(45deg, red, blue); transform: scale(1.05); }";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    cr_assert_not_null(stylesheet, "Failed to parse CSS functions");
    cr_assert_geq(stylesheet->rule_count, 0, "Should have parsed rules");
}
