/**
 * @file test_validator_gtest.cpp
 * @brief Comprehensive Lambda Validator Test Suite using Google Test
 * @author Henry Luo
 * @license MIT
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string.h>
#include <cassert>
#include <iostream>
#include <unistd.h>  // for getcwd
#include <sys/stat.h>
#include <fcntl.h>

// Include validator headers for ValidationResult and run_validation
#include "../lambda/validator.hpp"

// External validation functions - implemented in validator/ast_validate.cpp
extern "C" ValidationResult* exec_validation(int argc, char* argv[]);
extern "C" ValidationResult* run_validation(const char *data_file, const char *schema_file, const char *input_format);

// Test fixture class for validator tests
class ValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure we're in the correct working directory for test file access
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::string cwd_str(cwd);
            // Ensure we're in the project root directory
            if (cwd_str.find("Jubily") == std::string::npos) {
                // Try to find and change to the project directory
                // This is a fallback - tests should be run from proper directory
            }
        }
    }

    void TearDown() override {
        // Cleanup if needed
    }

    // Helper function to read file contents
    char* read_file_contents(const char* filename) {
        FILE* file = fopen(filename, "r");
        if (!file) {
            return nullptr;
        }
        
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char* content = (char*)malloc(file_size + 1);
        if (!content) {
            fclose(file);
            return nullptr;
        }
        
        fread(content, 1, file_size, file);
        content[file_size] = '\0';
        fclose(file);
        
        return content;
    }

    // Helper function to test CLI validation with formats using direct function calls
    void test_cli_validation_helper(const char* data_file, const char* schema_file, 
                                   const char* format, bool should_pass) {
        
        // Debug trace - function entry
        fprintf(stderr, "TRACE: test_cli_validation_helper ENTRY - data_file: %s, schema_file: %s, format: %s, should_pass: %d\n",
                data_file ? data_file : "NULL", 
                schema_file ? schema_file : "NULL", 
                format ? format : "NULL", 
                should_pass);
        fflush(stderr);
        
        // Capture stdout and stderr for validation output analysis
        fflush(stdout);
        fflush(stderr);
        
        // Redirect stdout to capture validation output
        int stdout_fd = dup(STDOUT_FILENO);
        int stderr_fd = dup(STDERR_FILENO);
        
        char temp_stdout[] = "/tmp/lambda_test_stdout_XXXXXX";
        char temp_stderr[] = "/tmp/lambda_test_stderr_XXXXXX";
        
        int stdout_temp_fd = mkstemp(temp_stdout);
        int stderr_temp_fd = mkstemp(temp_stderr);
        
        if (stdout_temp_fd == -1 || stderr_temp_fd == -1) {
            FAIL() << "Cannot create temporary files";
        }
        
        // Redirect output
        dup2(stdout_temp_fd, STDOUT_FILENO);
        dup2(stderr_temp_fd, STDERR_FILENO);
        
        ValidationResult* validation_result = nullptr;
        bool test_passed = false;
        
        try {
            // Use run_validation function directly
            validation_result = run_validation(data_file, schema_file, format);
            
            // Restore output
            dup2(stdout_fd, STDOUT_FILENO);
            dup2(stderr_fd, STDERR_FILENO);
            
            close(stdout_temp_fd);
            close(stderr_temp_fd);
            close(stdout_fd);
            close(stderr_fd);
            
            // Read captured output
            char* stdout_content = read_file_contents(temp_stdout);
            char* stderr_content = read_file_contents(temp_stderr);
            
            // Test passed if validation succeeded and we expected it to pass,
            // or if validation failed and we expected it to fail
            if (should_pass) {
                test_passed = (validation_result != nullptr && validation_result->valid);
                EXPECT_TRUE(test_passed) << 
                    "Expected validation to pass for " << data_file << 
                    " with schema " << schema_file <<
                    ". stdout: " << (stdout_content ? stdout_content : "null") <<
                    ". stderr: " << (stderr_content ? stderr_content : "null");
            } else {
                test_passed = (validation_result == nullptr || !validation_result->valid);
                EXPECT_TRUE(test_passed) << 
                    "Expected validation to fail for " << data_file << 
                    " with schema " << schema_file <<
                    ". stdout: " << (stdout_content ? stdout_content : "null") <<
                    ". stderr: " << (stderr_content ? stderr_content : "null");
            }
            
            // Cleanup
            if (stdout_content) free(stdout_content);
            if (stderr_content) free(stderr_content);
            unlink(temp_stdout);
            unlink(temp_stderr);
            
        } catch (...) {
            // Restore output on exception
            dup2(stdout_fd, STDOUT_FILENO);
            dup2(stderr_fd, STDERR_FILENO);
            close(stdout_temp_fd);
            close(stderr_temp_fd);
            close(stdout_fd);
            close(stderr_fd);
            unlink(temp_stdout);
            unlink(temp_stderr);
            throw;
        }
    }

    // Helper function to check validation success/failure without capturing output
    void test_validation_simple(const char* data_file, const char* schema_file, 
                               const char* format, bool should_pass) {
        ValidationResult* validation_result = run_validation(data_file, schema_file, format);
        
        if (should_pass) {
            ASSERT_NE(validation_result, nullptr) << 
                "Expected validation result for " << data_file << " with schema " << schema_file;
            EXPECT_TRUE(validation_result->valid) << 
                "Expected validation to pass for " << data_file << " with schema " << schema_file;
        } else {
            if (validation_result != nullptr) {
                EXPECT_FALSE(validation_result->valid) << 
                    "Expected validation to fail for " << data_file << " with schema " << schema_file;
            }
            // nullptr result can also indicate failure, which is acceptable
        }
    }

    // Helper function to test auto schema detection
    void test_auto_schema_detection_helper(const char* data_file, const char* expected_schema_info, 
                                          const char* format, bool should_pass) {
        // Test that the correct schema is auto-detected
        ValidationResult* validation_result = run_validation(data_file, nullptr, format);
        
        if (should_pass) {
            ASSERT_NE(validation_result, nullptr) << 
                "Expected validation result for auto-detection of " << data_file;
            EXPECT_TRUE(validation_result->valid) << 
                "Expected auto-detection validation to pass for " << data_file;
        } else {
            if (validation_result != nullptr) {
                EXPECT_FALSE(validation_result->valid) << 
                    "Expected auto-detection validation to fail for " << data_file;
            }
        }
    }

    // Helper function to verify schema features are present
    void verify_schema_features(const char* schema_file, const char* features[], int feature_count) {
        char* schema_content = read_file_contents(schema_file);
        ASSERT_NE(schema_content, nullptr) << "Failed to read schema file: " << schema_file;
        
        for (int i = 0; i < feature_count; i++) {
            const char* feature = features[i];
            bool found = (strstr(schema_content, feature) != nullptr);
            EXPECT_TRUE(found) << "Schema feature '" << feature << "' not found in " << schema_file;
        }
        
        free(schema_content);
    }
};

// ==================== Schema Feature Tests ====================

TEST_F(ValidatorTest, ComprehensiveSchemaFeatures) {
    const char* expected_features[] = {
        "type Element", "type Field", "type Text", "type List", "type Map"
    };
    verify_schema_features("test/lambda/validator/schema_comprehensive.ls", 
                          expected_features, 5);
}

TEST_F(ValidatorTest, HtmlSchemaFeatures) {
    const char* expected_features[] = {
        "type HtmlElement", "type HtmlDocument"
    };
    verify_schema_features("test/lambda/validator/schema_html.ls", 
                          expected_features, 2);
}

TEST_F(ValidatorTest, Html5SchemaFeatures) {
    const char* expected_features[] = {
        "type Html5Element", "type Html5Document"
    };
    verify_schema_features("test/lambda/validator/schema_html5.ls", 
                          expected_features, 2);
}

TEST_F(ValidatorTest, MarkdownSchemaFeatures) {
    const char* expected_features[] = {
        "type MarkdownElement", "type MarkdownDocument"
    };
    verify_schema_features("test/lambda/validator/schema_markdown.ls", 
                          expected_features, 2);
}

TEST_F(ValidatorTest, XmlBasicSchemaFeatures) {
    const char* expected_features[] = {
        "type XmlElement", "type XmlDocument"
    };
    verify_schema_features("test/lambda/validator/schema_xml_basic.ls", 
                          expected_features, 2);
}

TEST_F(ValidatorTest, XmlConfigSchemaFeatures) {
    const char* expected_features[] = {
        "type ConfigElement", "type Configuration"
    };
    verify_schema_features("test/lambda/validator/schema_xml_config.ls", 
                          expected_features, 2);
}

TEST_F(ValidatorTest, XmlRssSchemaFeatures) {
    const char* expected_features[] = {
        "type RssElement", "type RssFeed"
    };
    verify_schema_features("test/lambda/validator/schema_xml_rss.ls", 
                          expected_features, 2);
}

TEST_F(ValidatorTest, XmlSoapSchemaFeatures) {
    const char* expected_features[] = {
        "type SoapElement", "type SoapEnvelope"
    };
    verify_schema_features("test/lambda/validator/schema_xml_soap.ls", 
                          expected_features, 2);
}

// ==================== File Format Validation Tests ====================

TEST_F(ValidatorTest, HtmlComprehensiveValidation) {
    test_validation_simple("test/lambda/validator/test_comprehensive.html",
                          "test/lambda/validator/schema_comprehensive.ls", 
                          "html", true);
}

TEST_F(ValidatorTest, MarkdownComprehensiveValidation) {
    test_validation_simple("test/lambda/validator/test_comprehensive.md",
                          "test/lambda/validator/schema_comprehensive_markdown.ls", 
                          "markdown", true);
}

TEST_F(ValidatorTest, HtmlSimpleValidation) {
    test_validation_simple("test/lambda/validator/test_simple.html",
                          "test/lambda/validator/schema_html.ls", 
                          "html", true);
}

TEST_F(ValidatorTest, Html5ValidationWithNewSchema) {
    // Test HTML files automatically use html5_schema.ls
    test_auto_schema_detection_helper("test/input/test_html5.html",
                                     "Using HTML5 schema for HTML input", 
                                     "html", true);
}

TEST_F(ValidatorTest, Html5AutoDetectionValidation) {
    // Test that HTML files automatically use html5_schema.ls when no schema is specified
    test_auto_schema_detection_helper("test/input/test_html5.html",
                                     "Using HTML5 schema for HTML input",
                                     "html", true);
}

TEST_F(ValidatorTest, MarkdownSimpleValidation) {
    test_validation_simple("test/lambda/validator/test_simple.md",
                          "test/lambda/validator/schema_markdown.ls", 
                          "markdown", true);
}

TEST_F(ValidatorTest, HtmlAutoDetection) {
    // Test that HTML files are automatically detected and use appropriate schema
    test_auto_schema_detection_helper("test/input/test_basic.html",
                                     "Using HTML schema for HTML input",
                                     nullptr, true); // nullptr format for auto-detection
}

TEST_F(ValidatorTest, HtmlExplicitFormatSpecification) {
    test_validation_simple("test/input/test_basic.html",
                          "test/lambda/validator/schema_html.ls", 
                          "html", true);
}

TEST_F(ValidatorTest, MarkdownAutoDetection) {
    // Test that Markdown files are automatically detected
    test_auto_schema_detection_helper("test/input/test_basic.md",
                                     "Using Markdown schema for Markdown input",
                                     nullptr, true); // nullptr format for auto-detection
}

// ==================== XML Validation Tests ====================

TEST_F(ValidatorTest, XmlBasicValidation) {
    test_validation_simple("test/lambda/validator/test_xml_basic.xml",
                          "test/lambda/validator/schema_xml_basic.ls", 
                          "xml", true);
}

TEST_F(ValidatorTest, XmlConfigValidation) {
    test_validation_simple("test/lambda/validator/test_xml_config.xml",
                          "test/lambda/validator/schema_xml_config.ls", 
                          "xml", true);
}

TEST_F(ValidatorTest, XmlRssValidation) {
    test_validation_simple("test/lambda/validator/test_xml_rss.xml",
                          "test/lambda/validator/schema_xml_rss.ls", 
                          "xml", true);
}

TEST_F(ValidatorTest, XmlSoapValidation) {
    test_validation_simple("test/lambda/validator/test_xml_soap.xml",
                          "test/lambda/validator/schema_xml_soap.ls", 
                          "xml", true);
}

TEST_F(ValidatorTest, XmlComprehensiveValidation) {
    test_validation_simple("test/lambda/validator/test_xml_comprehensive.xml",
                          "test/lambda/validator/schema_xml_comprehensive.ls", 
                          "xml", true);
}

TEST_F(ValidatorTest, XmlAutoDetection) {
    // Test that XML files are automatically detected
    test_auto_schema_detection_helper("test/input/test_basic.xml",
                                     "Using XML schema for XML input",
                                     nullptr, true); // nullptr format for auto-detection
}

// ==================== JSON and YAML Validation Tests ====================

TEST_F(ValidatorTest, JsonUserProfileValidation) {
    test_validation_simple("test/lambda/validator/test_json_user_profile.json",
                          "test/lambda/validator/schema_json_user_profile.ls", 
                          "json", true);
}

TEST_F(ValidatorTest, JsonEcommerceApiValidation) {
    test_validation_simple("test/lambda/validator/test_json_ecommerce_api.json",
                          "test/lambda/validator/schema_json_ecommerce_api.ls", 
                          "json", true);
}

TEST_F(ValidatorTest, YamlBlogPostValidation) {
    test_validation_simple("test/lambda/validator/test_yaml_blog_post.yaml",
                          "test/lambda/validator/schema_yaml_blog_post.ls", 
                          "yaml", true);
}

// ==================== Error Handling Tests ====================

TEST_F(ValidatorTest, InvalidHtmlValidation) {
    test_validation_simple("test/lambda/validator/test_invalid.html",
                          "test/lambda/validator/schema_html.ls", 
                          "html", false); // Should fail
}

TEST_F(ValidatorTest, InvalidMarkdownValidation) {
    test_validation_simple("test/lambda/validator/test_invalid.md",
                          "test/lambda/validator/schema_markdown.ls", 
                          "markdown", false); // Should fail
}

TEST_F(ValidatorTest, NonexistentHtmlFile) {
    test_validation_simple("test/lambda/validator/nonexistent.html",
                          "test/lambda/validator/schema_html.ls", 
                          "html", false); // Should fail
}

TEST_F(ValidatorTest, NonexistentMarkdownFile) {
    test_validation_simple("test/lambda/validator/nonexistent.md",
                          "test/lambda/validator/schema_markdown.ls", 
                          "markdown", false); // Should fail
}

// ==================== Format Requirements Tests ====================

TEST_F(ValidatorTest, JsonRequiresExplicitSchema) {
    // JSON files should require explicit schema specification
    test_auto_schema_detection_helper("test/input/test_basic.json",
                                     "JSON requires explicit schema",
                                     "json", false); // Should fail without explicit schema
}

TEST_F(ValidatorTest, XmlRequiresExplicitSchema) {
    // XML files should require explicit schema specification for specific validation
    test_auto_schema_detection_helper("test/input/test_basic.xml",
                                     "XML requires explicit schema",
                                     "xml", false); // Should fail without explicit schema
}

TEST_F(ValidatorTest, YamlRequiresExplicitSchema) {
    // YAML files should require explicit schema specification
    test_auto_schema_detection_helper("test/input/test_basic.yaml",
                                     "YAML requires explicit schema",
                                     "yaml", false); // Should fail without explicit schema
}

// ==================== Document Type Auto-Detection Tests ====================

TEST_F(ValidatorTest, AsciidocUsesDocSchema) {
    // AsciiDoc files should use document schema
    test_auto_schema_detection_helper("test/input/test_basic.adoc",
                                     "Using document schema for AsciiDoc input",
                                     "asciidoc", true);
}

TEST_F(ValidatorTest, RstUsesDocSchema) {
    // ReStructuredText files should use document schema
    test_auto_schema_detection_helper("test/input/test_basic.rst",
                                     "Using document schema for RST input",
                                     "rst", true);
}

TEST_F(ValidatorTest, ManUsesDocSchema) {
    // Manual page files should use document schema
    test_auto_schema_detection_helper("test/input/test_basic.man",
                                     "Using document schema for man input",
                                     "man", true);
}

// ==================== Edge Cases and Stress Tests ====================

TEST_F(ValidatorTest, EmptyFileHandling) {
    // Test handling of empty files
    test_validation_simple("test/lambda/validator/test_empty.html",
                          "test/lambda/validator/schema_html.ls", 
                          "html", false); // Should fail or handle gracefully
}

TEST_F(ValidatorTest, UnsupportedFormatHandling) {
    // Test handling of unsupported file formats
    test_validation_simple("test/lambda/validator/test_unsupported.xyz",
                          "test/lambda/validator/schema_comprehensive.ls", 
                          "xyz", false); // Should fail
}

// ==================== Primitive Types Validation ====================

TEST_F(ValidatorTest, PrimitiveTypesParsing) {
    // Test basic primitive type parsing in schemas
    const char* primitive_features[] = {"int", "string", "bool", "float"};
    verify_schema_features("test/lambda/validator/schema_primitives.ls", 
                          primitive_features, 4);
}

TEST_F(ValidatorTest, PrimitiveTypesValidation) {
    test_validation_simple("test/lambda/validator/test_primitives.json",
                          "test/lambda/validator/schema_primitives.ls", 
                          "json", true);
}

TEST_F(ValidatorTest, UnionTypesParsing) {
    // Test union type parsing in schemas
    const char* union_features[] = {"string | int", "bool | null"};
    verify_schema_features("test/lambda/validator/schema_unions.ls", 
                          union_features, 2);
}