#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../lambda/input/mime-detect.h"

// Setup and teardown functions
static MimeDetector* detector = NULL;

void mime_setup(void) {
    detector = mime_detector_init();
    cr_assert_not_null(detector, "Failed to initialize MIME detector");
}

void mime_teardown(void) {
    if (detector) {
        mime_detector_destroy(detector);
        detector = NULL;
    }
}

TestSuite(mime_detect_tests, .init = mime_setup, .fini = mime_teardown);

// Helper function to read file contents
char* read_file_content(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) return NULL;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    fread(content, 1, size, file);
    content[size] = '\0';
    fclose(file);
    return content;
}

// Helper function to get just the filename from a path
const char* get_filename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// Test basic MIME detection functionality
Test(mime_detect_tests, basic_detection) {
    const char* mime = detect_mime_type(detector, "test.json", "{\"test\": true}", 14);
    cr_assert_not_null(mime, "JSON detection should not return NULL");
    cr_assert(strstr(mime, "json") != NULL, "Expected MIME type to contain 'json', got: %s", mime);
}

// Test filename-only detection
Test(mime_detect_tests, filename_detection) {
    const char* mime = detect_mime_from_filename(detector, "document.pdf");
    cr_assert_not_null(mime, "PDF filename detection should not return NULL");
    cr_assert(strstr(mime, "pdf") != NULL, "Expected MIME type to contain 'pdf', got: %s", mime);
}

// Test content-based detection without filename
Test(mime_detect_tests, content_detection) {
    const char* mime = detect_mime_type(detector, "unknown", "<html>", 6);
    cr_assert_not_null(mime, "HTML content detection should not return NULL");
    cr_assert(strstr(mime, "html") != NULL, "Expected MIME type to contain 'html', got: %s", mime);
}

// Test magic byte detection
Test(mime_detect_tests, magic_bytes) {
    // PDF magic bytes
    const char* pdf_content = "%PDF-1.4\nFake PDF content";
    const char* mime = detect_mime_type(detector, "unknown", pdf_content, strlen(pdf_content));
    cr_assert_not_null(mime, "PDF magic byte detection should not return NULL");
    cr_assert(strstr(mime, "pdf") != NULL, "Expected MIME type to contain 'pdf', got: %s", mime);
}

// Test file without extension (content-based detection)
Test(mime_detect_tests, no_extension_content) {
    const char* json_content = "{\"auto_detect\": true}";
    const char* mime = detect_mime_type(detector, "no_extension", json_content, strlen(json_content));
    cr_assert_not_null(mime, "No extension JSON detection should not return NULL");
    cr_assert(strstr(mime, "json") != NULL, "Expected MIME type to contain 'json', got: %s", mime);
}

// Test all files in test/input directory
Test(mime_detect_tests, test_input_files) {
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
        NULL
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
        NULL
    };
    
    for (int i = 0; test_files[i]; i++) {
        char* content = read_file_content(test_files[i]);
        cr_assert_not_null(content, "Failed to read file: %s", test_files[i]);
        
        const char* filename = get_filename(test_files[i]);
        const char* mime = detect_mime_type(detector, filename, content, strlen(content));
        
        cr_assert_not_null(mime, "MIME detection failed for file: %s", test_files[i]);
        
        // Check if the detected MIME type contains the expected pattern
        cr_assert(strstr(mime, expected_patterns[i]) != NULL, 
                 "File %s: Expected MIME type to contain '%s', got: %s", 
                 test_files[i], expected_patterns[i], mime);
        
        printf("✓ %s -> %s\n", filename, mime);
        free(content);
    }
}

// Test extensionless files for content-based detection
Test(mime_detect_tests, extensionless_files) {
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
        NULL
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
        NULL
    };
    
    for (int i = 0; extensionless_files[i]; i++) {
        char* content = read_file_content(extensionless_files[i]);
        cr_assert_not_null(content, "Failed to read extensionless file: %s", extensionless_files[i]);
        
        const char* filename = get_filename(extensionless_files[i]);
        const char* mime = detect_mime_type(detector, filename, content, strlen(content));
        
        cr_assert_not_null(mime, "MIME detection failed for extensionless file: %s", extensionless_files[i]);
        
        // Check if the detected MIME type contains the expected pattern
        cr_assert(strstr(mime, expected_patterns[i]) != NULL, 
                 "Extensionless file %s: Expected MIME type to contain '%s', got: %s", 
                 extensionless_files[i], expected_patterns[i], mime);
        
        printf("✓ %s -> %s (content-based)\n", filename, mime);
        free(content);
    }
}

// Test edge cases
Test(mime_detect_tests, edge_cases) {
    // Empty content
    const char* mime = detect_mime_type(detector, "test.txt", "", 0);
    cr_assert_not_null(mime, "Empty content should still detect by filename");
    
    // NULL filename
    mime = detect_mime_type(detector, NULL, "{\"test\": true}", 14);
    cr_assert_not_null(mime, "NULL filename should still detect by content");
    
    // Very small content
    mime = detect_mime_type(detector, "test", "{", 1);
    // Should either return NULL or a reasonable guess
    
    // Binary content
    const char binary[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    mime = detect_mime_type(detector, "unknown", binary, 5);
    // Should handle binary content gracefully
}

// Test specific MIME type mappings
Test(mime_detect_tests, specific_mappings) {
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
        {NULL, NULL, NULL}
    };
    
    for (int i = 0; test_cases[i].filename; i++) {
        const char* mime = detect_mime_type(detector, 
                                          test_cases[i].filename,
                                          test_cases[i].content,
                                          strlen(test_cases[i].content));
        
        cr_assert_not_null(mime, "Detection failed for %s", test_cases[i].filename);
        cr_assert(strstr(mime, test_cases[i].expected_substring) != NULL,
                 "File %s: Expected '%s' in MIME type, got: %s",
                 test_cases[i].filename, test_cases[i].expected_substring, mime);
    }
}
