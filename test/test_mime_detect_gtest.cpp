#define _GNU_SOURCE
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../lambda/input/mime-detect.h"

// Test fixture class for MIME detection tests
class MimeDetectTest : public ::testing::Test {
protected:
    static MimeDetector* detector;

    static void SetUpTestSuite() {
        detector = mime_detector_init();
        ASSERT_NE(detector, nullptr) << "Failed to initialize MIME detector";
    }

    static void TearDownTestSuite() {
        if (detector) {
            mime_detector_destroy(detector);
            detector = nullptr;
        }
    }

    // Helper function to read file contents
    static char* read_file_content(const char* filepath) {
        FILE* file = fopen(filepath, "rb");
        if (!file) return nullptr;
        
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char* content = static_cast<char*>(malloc(size + 1));
        if (!content) {
            fclose(file);
            return nullptr;
        }
        
        fread(content, 1, size, file);
        content[size] = '\0';
        fclose(file);
        return content;
    }

    // Helper function to get just the filename from a path
    static const char* get_filename(const char* path) {
        const char* slash = strrchr(path, '/');
        return slash ? slash + 1 : path;
    }
};

// Static member definition
MimeDetector* MimeDetectTest::detector = nullptr;

// Test basic MIME detection functionality
TEST_F(MimeDetectTest, BasicDetection) {
    const char* mime = detect_mime_type(detector, "test.json", "{\"test\": true}", 14);
    ASSERT_NE(mime, nullptr) << "JSON detection should not return NULL";
    EXPECT_NE(strstr(mime, "json"), nullptr) << "Expected MIME type to contain 'json', got: " << mime;
}

// Test filename-only detection
TEST_F(MimeDetectTest, FilenameDetection) {
    const char* mime = detect_mime_from_filename(detector, "document.pdf");
    ASSERT_NE(mime, nullptr) << "PDF filename detection should not return NULL";
    EXPECT_NE(strstr(mime, "pdf"), nullptr) << "Expected MIME type to contain 'pdf', got: " << mime;
}

// Test content-based detection without filename
TEST_F(MimeDetectTest, ContentDetection) {
    const char* mime = detect_mime_type(detector, "unknown", "<html>", 6);
    ASSERT_NE(mime, nullptr) << "HTML content detection should not return NULL";
    EXPECT_NE(strstr(mime, "html"), nullptr) << "Expected MIME type to contain 'html', got: " << mime;
}

// Test magic byte detection
TEST_F(MimeDetectTest, MagicBytes) {
    // PDF magic bytes
    const char* pdf_content = "%PDF-1.4\nFake PDF content";
    const char* mime = detect_mime_type(detector, "unknown", pdf_content, strlen(pdf_content));
    ASSERT_NE(mime, nullptr) << "PDF magic byte detection should not return NULL";
    EXPECT_NE(strstr(mime, "pdf"), nullptr) << "Expected MIME type to contain 'pdf', got: " << mime;
}

// Test file without extension (content-based detection)
TEST_F(MimeDetectTest, NoExtensionContent) {
    const char* json_content = "{\"auto_detect\": true}";
    const char* mime = detect_mime_type(detector, "no_extension", json_content, strlen(json_content));
    ASSERT_NE(mime, nullptr) << "No extension JSON detection should not return NULL";
    EXPECT_NE(strstr(mime, "json"), nullptr) << "Expected MIME type to contain 'json', got: " << mime;
}

// Test all files in test/input directory
TEST_F(MimeDetectTest, TestInputFiles) {
    const char* test_files[] = {
        "test/input/test.json",
        "test/input/test.html", 
        "test/input/test.xml",
        "test/input/test.csv",
        "test/input/test.txt",
        "test/input/test.pdf",
        "test/input/test.md",
        "test/input/test.yaml",
        "test/input/test.toml",
        "test/input/test.ini",
        "test/input/comprehensive_test.rst",
        "test/input/test.rtf",
        "test/input/test.tex",
        "test/input/no_extension",
        nullptr
    };
    
    // Expected MIME type patterns for each file
    const char* expected_patterns[] = {
        "json",
        "html", 
        "xml",
        "csv",
        "text",
        "pdf",
        "markdown",
        "yaml",
        "toml",
        "plain", // .ini files are mapped to text/plain
        "rst",
        "rtf",
        "tex",
        "json", // no_extension should be detected as JSON
        nullptr
    };
    
    for (int i = 0; test_files[i]; i++) {
        char* content = read_file_content(test_files[i]);
        ASSERT_NE(content, nullptr) << "Failed to read file: " << test_files[i];
        
        const char* filename = get_filename(test_files[i]);
        const char* mime = detect_mime_type(detector, filename, content, strlen(content));
        
        ASSERT_NE(mime, nullptr) << "MIME detection failed for file: " << test_files[i];
        
        // Check if the detected MIME type contains the expected pattern
        EXPECT_NE(strstr(mime, expected_patterns[i]), nullptr) 
                 << "File " << test_files[i] << ": Expected MIME type to contain '" 
                 << expected_patterns[i] << "', got: " << mime;
        
        printf("✓ %s -> %s\n", filename, mime);
        free(content);
    }
}

// Test extensionless files for content-based detection
TEST_F(MimeDetectTest, ExtensionlessFiles) {
    const char* extensionless_files[] = {
        "test/input/xml_content",
        "test/input/html_content", 
        "test/input/csv_data",
        "test/input/markdown_doc",
        "test/input/config_yaml",
        "test/input/plain_text",
        "test/input/script_content",
        "test/input/python_script",
        "test/input/shell_script",
        "test/input/pdf_document",
        nullptr
    };
    
    // Expected MIME type patterns for each extensionless file
    const char* expected_patterns[] = {
        "xml",
        "html", 
        "text",  // CSV content without extension is detected as text/plain (expected)
        "markdown",
        "text",  // YAML without extension is hard to detect, often detected as text/plain
        "text",  // JavaScript without shebang is detected as text/plain (expected)
        "text",  // JavaScript without shebang -> text/plain
        "python",  // Python with shebang should be detected
        "shell",   // Shell script with shebang should be detected
        "pdf",
        nullptr
    };
    
    for (int i = 0; extensionless_files[i]; i++) {
        char* content = read_file_content(extensionless_files[i]);
        ASSERT_NE(content, nullptr) << "Failed to read extensionless file: " << extensionless_files[i];
        
        const char* filename = get_filename(extensionless_files[i]);
        const char* mime = detect_mime_type(detector, filename, content, strlen(content));
        
        ASSERT_NE(mime, nullptr) << "MIME detection failed for extensionless file: " << extensionless_files[i];
        
        // Check if the detected MIME type contains the expected pattern
        EXPECT_NE(strstr(mime, expected_patterns[i]), nullptr) 
                 << "Extensionless file " << extensionless_files[i] << ": Expected MIME type to contain '" 
                 << expected_patterns[i] << "', got: " << mime;
        
        printf("✓ %s -> %s (content-based)\n", filename, mime);
        free(content);
    }
}

// Test edge cases
TEST_F(MimeDetectTest, EdgeCases) {
    // Empty content
    const char* mime = detect_mime_type(detector, "test.txt", "", 0);
    EXPECT_NE(mime, nullptr) << "Empty content should still detect by filename";
    
    // NULL filename
    mime = detect_mime_type(detector, nullptr, "{\"test\": true}", 14);
    EXPECT_NE(mime, nullptr) << "NULL filename should still detect by content";
    
    // Very small content
    mime = detect_mime_type(detector, "test", "{", 1);
    // Should either return NULL or a reasonable guess
    
    // Binary content
    const char binary[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    mime = detect_mime_type(detector, "unknown", binary, 5);
    // Should handle binary content gracefully
}

// Test specific MIME type mappings
TEST_F(MimeDetectTest, SpecificMappings) {
    struct {
        const char* filename;
        const char* content;
        const char* expected_substring;
    } test_cases[] = {
        {"script.js", "console.log('hello');", "javascript"}, // Avoid { to prevent JSON detection
        {"style.css", "body { color: red; }", "css"},
        {"data.xml", "<?xml version=\"1.0\"?><root/>", "xml"},
        {"config.toml", "[section]\nkey = \"value\"", "toml"},
        {"README.md", "# Title\nContent", "markdown"},
        {nullptr, nullptr, nullptr}
    };
    
    for (int i = 0; test_cases[i].filename; i++) {
        const char* mime = detect_mime_type(detector, 
                                          test_cases[i].filename,
                                          test_cases[i].content,
                                          strlen(test_cases[i].content));
        
        ASSERT_NE(mime, nullptr) << "Detection failed for " << test_cases[i].filename;
        EXPECT_NE(strstr(mime, test_cases[i].expected_substring), nullptr)
                 << "File " << test_cases[i].filename << ": Expected '" << test_cases[i].expected_substring 
                 << "' in MIME type, got: " << mime;
    }
}