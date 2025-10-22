#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>
#include "../lambda/input/css/css_tokenizer.h"
#include "../lambda/input/css/css_property_value_parser.h"
#include "../lambda/input/css/css_selector_parser.h"
#include "../lambda/input/css/css_style.h"
#include "../lib/mempool.h"

// Test fixture for comprehensive CSS file parsing tests
class CssAllFilesTest : public ::testing::Test {
protected:
    Pool* pool;
    std::vector<std::string> css_files;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
        
        // Discover all CSS files in test/input directory
        discoverCssFiles();
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper function to read entire file content
    static char* readFileContent(const char* filepath) {
        FILE* file = fopen(filepath, "r");
        if (!file) {
            printf("Failed to open file: %s\n", filepath);
            return nullptr;
        }

        fseek(file, 0, SEEK_END);
        long length = ftell(file);
        fseek(file, 0, SEEK_SET);

        if (length <= 0) {
            fclose(file);
            return nullptr;
        }

        char* content = (char*)malloc(length + 1);
        if (!content) {
            fclose(file);
            return nullptr;
        }

        size_t read_size = fread(content, 1, length, file);
        content[read_size] = '\0';
        fclose(file);

        return content;
    }

    // Discover all CSS files in the test/input directory
    void discoverCssFiles() {
        const char* input_dir = "./test/input";
        DIR* dir = opendir(input_dir);
        if (!dir) {
            // Try alternative path from project root
            input_dir = "test/input";
            dir = opendir(input_dir);
        }
        
        if (!dir) {
            printf("Warning: Could not open test/input directory\n");
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            const char* name = entry->d_name;
            size_t name_len = strlen(name);
            
            // Check if file ends with .css
            if (name_len > 4 && strcmp(name + name_len - 4, ".css") == 0) {
                std::string full_path = std::string(input_dir) + "/" + name;
                css_files.push_back(full_path);
            }
        }
        
        closedir(dir);
    }

    // Validate CSS file parsing capabilities
    void validateCssFileParsing(const char* file_path, const char* file_name) {
        // Read the CSS file content
        char* css_content = readFileContent(file_path);
        ASSERT_NE(css_content, nullptr) << "Should be able to read CSS file: " << file_name;

        size_t content_length = strlen(css_content);
        EXPECT_GT(content_length, 0) << "CSS file should not be empty: " << file_name;

        // Test 1: CSS Tokenization
        size_t token_count;
        CSSToken* tokens = css_tokenize(css_content, content_length, pool, &token_count);
        EXPECT_NE(tokens, nullptr) << "Should tokenize CSS file: " << file_name;
        EXPECT_GT(token_count, (size_t)0) << "Should produce tokens for: " << file_name;

        // Test 2: Property Value Parser Creation
        CssPropertyValueParser* prop_parser = css_property_value_parser_create(pool);
        EXPECT_NE(prop_parser, nullptr) << "Property parser should be created for: " << file_name;
        if (prop_parser) {
            css_property_value_parser_destroy(prop_parser);
        }

        // Test 3: Selector Parser Creation
        CSSSelectorParser* sel_parser = css_selector_parser_create(pool);
        EXPECT_NE(sel_parser, nullptr) << "Selector parser should be created for: " << file_name;
        if (sel_parser) {
            css_selector_parser_destroy(sel_parser);
        }

        // Test 4: Memory safety - ensure no crashes with large files
        if (content_length > 10000) {
            // For large files, test chunked processing
            size_t chunk_size = content_length / 4;
            char* chunk = (char*)malloc(chunk_size + 1);
            if (chunk) {
                strncpy(chunk, css_content, chunk_size);
                chunk[chunk_size] = '\0';
                
                size_t chunk_tokens;
                CSSToken* chunk_result = css_tokenize(chunk, chunk_size, pool, &chunk_tokens);
                EXPECT_NE(chunk_result, nullptr) << "Should handle large file chunks: " << file_name;
                
                free(chunk);
            }
        }

        free(css_content);
    }

    // Test enhanced CSS features in file content
    void validateEnhancedCssFeatures(const char* file_path, const char* file_name) {
        char* css_content = readFileContent(file_path);
        if (!css_content) return;

        // Look for modern CSS features and test they parse correctly
        std::vector<std::string> modern_features = {
            "column-",       // Multi-column layout
            "transform:",    // CSS transforms
            "animation:",    // CSS animations
            "transition:",   // CSS transitions
            "flex",          // Flexbox
            "grid",          // CSS Grid
            "var(",          // CSS variables
            "calc(",         // CSS calc function
            "rgb(",          // RGB color function
            "hsl(",          // HSL color function
            "hwb(",          // HWB color function (new)
            "lab(",          // Lab color function (new)
            "lch(",          // LCH color function (new)
            "oklab(",        // OKLab color function (new)
            "oklch(",        // OKLCH color function (new)
            "blur(",         // Filter functions
            "brightness(",
            "contrast(",
            "drop-shadow(",
            "grayscale(",
            "hue-rotate(",
            "invert(",
            "opacity(",
            "saturate(",
            "sepia("
        };

        for (const auto& feature : modern_features) {
            if (strstr(css_content, feature.c_str()) != nullptr) {
                // Found modern feature - ensure it tokenizes properly
                size_t token_count;
                CSSToken* tokens = css_tokenize(css_content, strlen(css_content), pool, &token_count);
                EXPECT_NE(tokens, nullptr) << "Should parse modern CSS feature '" << feature 
                                          << "' in file: " << file_name;
                break; // Only need to test once per file
            }
        }

        free(css_content);
    }
};

// Test all CSS files can be tokenized successfully
TEST_F(CssAllFilesTest, ParseAllCssFilesBasic) {
    ASSERT_GT(css_files.size(), 0) << "Should find at least one CSS file in test/input";
    
    for (const auto& file_path : css_files) {
        // Extract filename for better error messages
        std::string file_name = file_path.substr(file_path.find_last_of("/\\") + 1);
        
        validateCssFileParsing(file_path.c_str(), file_name.c_str());
    }
}

// Test enhanced CSS features in discovered files
TEST_F(CssAllFilesTest, ParseEnhancedCssFeatures) {
    for (const auto& file_path : css_files) {
        std::string file_name = file_path.substr(file_path.find_last_of("/\\") + 1);
        
        validateEnhancedCssFeatures(file_path.c_str(), file_name.c_str());
    }
}

// Test specific known CSS framework files
TEST_F(CssAllFilesTest, ParseKnownCssFrameworks) {
    std::vector<std::string> framework_files = {
        "bootstrap.css",
        "tailwind.css", 
        "bulma.css",
        "foundation.css",
        "normalize.css"
    };
    
    for (const auto& framework : framework_files) {
        // Look for this framework file in our discovered files
        auto it = std::find_if(css_files.begin(), css_files.end(), 
                              [&framework](const std::string& path) {
                                  return path.find(framework) != std::string::npos;
                              });
        
        if (it != css_files.end()) {
            validateCssFileParsing(it->c_str(), framework.c_str());
            
            // Framework files should have substantial content
            char* content = readFileContent(it->c_str());
            if (content) {
                EXPECT_GT(strlen(content), 1000) << "Framework file should be substantial: " << framework;
                free(content);
            }
        }
    }
}

// Test complete CSS grammar file specifically
TEST_F(CssAllFilesTest, ParseCompleteCssGrammarFile) {
    auto grammar_file = std::find_if(css_files.begin(), css_files.end(), 
                                    [](const std::string& path) {
                                        return path.find("complete_css_grammar.css") != std::string::npos;
                                    });
    
    if (grammar_file != css_files.end()) {
        validateCssFileParsing(grammar_file->c_str(), "complete_css_grammar.css");
        
        // This file should contain comprehensive CSS features
        char* content = readFileContent(grammar_file->c_str());
        if (content) {
            // Verify it contains enhanced features we added
            EXPECT_TRUE(strstr(content, "column-") != nullptr) << "Should contain multi-column layout";
            EXPECT_TRUE(strstr(content, "transform:") != nullptr) << "Should contain transform properties";
            EXPECT_TRUE(strstr(content, "hwb(") != nullptr || 
                       strstr(content, "lab(") != nullptr ||
                       strstr(content, "oklch(") != nullptr) << "Should contain modern color functions";
            
            free(content);
        }
    }
}

// Test CSS functions sample file specifically  
TEST_F(CssAllFilesTest, ParseCssFunctionsSampleFile) {
    auto functions_file = std::find_if(css_files.begin(), css_files.end(), 
                                      [](const std::string& path) {
                                          return path.find("css_functions_sample.css") != std::string::npos;
                                      });
    
    if (functions_file != css_files.end()) {
        validateCssFileParsing(functions_file->c_str(), "css_functions_sample.css");
        
        char* content = readFileContent(functions_file->c_str());
        if (content) {
            // Should contain various CSS functions
            bool has_functions = strstr(content, "calc(") != nullptr ||
                               strstr(content, "rgb(") != nullptr ||
                               strstr(content, "url(") != nullptr ||
                               strstr(content, "var(") != nullptr;
            EXPECT_TRUE(has_functions) << "CSS functions sample should contain function examples";
            
            free(content);
        }
    }
}

// Test parser robustness with malformed CSS
TEST_F(CssAllFilesTest, ParserRobustnessTest) {
    // Test with intentionally problematic CSS
    const char* problematic_css[] = {
        "/* Unclosed comment",
        "{ orphaned: brace; }",
        ".class-without-brace color: red;",
        "@media (broken { display: block; }",
        "property-without-value;",
        "color: rgb(300, 400, 500);", // Invalid RGB values
        "transform: rotate(invalid);",
        ""  // Empty string
    };
    
    for (const char* css : problematic_css) {
        if (strlen(css) == 0) continue;
        
        size_t token_count;
        CSSToken* tokens = css_tokenize(css, strlen(css), pool, &token_count);
        // Should not crash, even with malformed CSS
        EXPECT_NE(tokens, nullptr) << "Should handle malformed CSS: " << css;
    }
}

// Performance test with large CSS content
TEST_F(CssAllFilesTest, LargeCssPerformanceTest) {
    // Find the largest CSS file
    std::string largest_file;
    size_t largest_size = 0;
    
    for (const auto& file_path : css_files) {
        struct stat st;
        if (stat(file_path.c_str(), &st) == 0) {
            if ((size_t)st.st_size > largest_size) {
                largest_size = st.st_size;
                largest_file = file_path;
            }
        }
    }
    
    if (!largest_file.empty() && largest_size > 5000) {
        // Test performance with the largest file
        auto start = std::chrono::high_resolution_clock::now();
        
        validateCssFileParsing(largest_file.c_str(), "largest_css_file");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // Should complete within reasonable time (5 seconds for large files)
        EXPECT_LT(duration.count(), 5000) << "Large CSS file parsing should complete in reasonable time";
    }
}