#include <catch2/catch_test_macros.hpp>
#include <string.h>
#include <stdio.h>
#include "../lambda/input/css_parser.h"
#include "../lambda/input/css_tokenizer.h"
#include "../lib/mem-pool/include/mem_pool.h"

// Global variables for setup/teardown
static VariableMemPool* pool = nullptr;
static css_parser_t* parser = nullptr;

// Setup function
static void setup_css_parser() {
    if (!pool) {
        MemPoolError err = pool_variable_init(&pool, 1024 * 64, 10);  // 64KB pool
        REQUIRE(err == MEM_POOL_ERR_OK);
        parser = css_parser_create(pool);
        REQUIRE(parser != nullptr);
    }
}

// Teardown function
static void teardown_css_parser() {
    if (parser) {
        css_parser_destroy(parser);
        parser = nullptr;
    }
    if (pool) {
        pool_variable_destroy(pool);
        pool = nullptr;
    }
}

// Safe file reading function with error checking
static char* read_css_file_safe(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Cannot open file: %s\n", filename);
        return nullptr;
    }
    
    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return nullptr;
    }
    
    long size = ftell(file);
    if (size < 0 || size > 100000) { // Limit to 100KB
        fclose(file);
        return nullptr;
    }
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return nullptr;
    }
    
    char* content = (char*)malloc(size + 1);
    if (!content) {
        fclose(file);
        return nullptr;
    }
    
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';
    fclose(file);
    
    return content;
}

// Test simple.css file
TEST_CASE("CSS Files Safe - parse simple css file", "[css][files][safe]") {
    setup_css_parser();
    
    char* css_content = read_css_file_safe("test/input/simple.css");
    REQUIRE(css_content != nullptr);
    
    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count >= 0);
    
    free(css_content);
    teardown_css_parser();
}

// Test stylesheet.css file
TEST_CASE("CSS Files Safe - parse stylesheet css file", "[css][files][safe]") {
    setup_css_parser();
    
    char* css_content = read_css_file_safe("test/input/stylesheet.css");
    REQUIRE(css_content != nullptr);
    
    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count >= 0);
    
    free(css_content);
    teardown_css_parser();
}

// Test with inline CSS that matches file content structure
TEST_CASE("CSS Files Safe - parse inline multiline css", "[css][files][safe]") {
    setup_css_parser();
    
    const char* css = "/* Comment */\nbody {\n    margin: 0;\n    padding: 20px;\n}\n.container {\n    max-width: 1200px;\n}";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count >= 0);
    
    teardown_css_parser();
}

// Test CSS with complex selectors
TEST_CASE("CSS Files Safe - parse complex selectors", "[css][files][safe]") {
    setup_css_parser();
    
    const char* css = "h1, h2, h3 { color: #333; }\n.button:hover { background: blue; }";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count >= 0);
    
    teardown_css_parser();
}

// Test CSS with functions
TEST_CASE("CSS Files Safe - parse css functions", "[css][files][safe]") {
    setup_css_parser();
    
    const char* css = ".test { background: linear-gradient(45deg, red, blue); transform: scale(1.05); }";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count >= 0);
    
    teardown_css_parser();
}

// Test complete_css_grammar.css file
TEST_CASE("CSS Files Safe - parse complete css grammar file", "[css][files][safe]") {
    setup_css_parser();
    
    char* css_content = read_css_file_safe("test/input/complete_css_grammar.css");
    REQUIRE(css_content != nullptr);
    
    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count >= 0);
    
    free(css_content);
    teardown_css_parser();
}

// Test css_functions_sample.css file
TEST_CASE("CSS Files Safe - parse css functions sample file", "[css][files][safe]") {
    setup_css_parser();
    
    char* css_content = read_css_file_safe("test/input/css_functions_sample.css");
    REQUIRE(css_content != nullptr);
    
    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count >= 0);
    
    free(css_content);
    teardown_css_parser();
}

// Test stylesheet_3_0.css file
TEST_CASE("CSS Files Safe - parse stylesheet 3 0 file", "[css][files][safe]") {
    setup_css_parser();
    
    char* css_content = read_css_file_safe("test/input/stylesheet_3_0.css");
    REQUIRE(css_content != nullptr);
    
    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count >= 0);
    
    free(css_content);
    teardown_css_parser();
}
