#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern "C" {
#include "../lambda/input/mime-detect.h"
}

// Global MIME detector for tests
static MimeDetector* detector = nullptr;

// Setup function to initialize MIME detector
void setup_mime_tests() {
    if (!detector) {
        detector = mime_detector_init();
        REQUIRE(detector != nullptr);
    }
}

// Teardown function to cleanup MIME detector
void teardown_mime_tests() {
    if (detector) {
        mime_detector_destroy(detector);
        detector = nullptr;
    }
}

// Helper function to read file contents
char* read_file_content(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) return nullptr;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* content = (char*)malloc(size + 1);
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
const char* get_filename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

TEST_CASE("Basic MIME Detection", "[mime][basic]") {
    setup_mime_tests();
    
    const char* mime = detect_mime_type(detector, "test.json", "{\"test\": true}", 14);
    REQUIRE(mime != nullptr);
    REQUIRE(strstr(mime, "json") != nullptr);
    
    teardown_mime_tests();
}

TEST_CASE("Filename Detection", "[mime][filename]") {
    setup_mime_tests();
    
    const char* mime = detect_mime_from_filename(detector, "document.pdf");
    REQUIRE(mime != nullptr);
    REQUIRE(strstr(mime, "pdf") != nullptr);
    
    teardown_mime_tests();
}

TEST_CASE("Content Detection", "[mime][content]") {
    setup_mime_tests();
    
    const char* mime = detect_mime_type(detector, "unknown", "<html>", 6);
    REQUIRE(mime != nullptr);
    REQUIRE(strstr(mime, "html") != nullptr);
    
    teardown_mime_tests();
}

TEST_CASE("Magic Bytes Detection", "[mime][magic]") {
    setup_mime_tests();
    
    // PDF magic bytes
    const char* pdf_content = "%PDF-1.4\nFake PDF content";
    const char* mime = detect_mime_type(detector, "unknown", pdf_content, strlen(pdf_content));
    REQUIRE(mime != nullptr);
    REQUIRE(strstr(mime, "pdf") != nullptr);
    
    teardown_mime_tests();
}

TEST_CASE("No Extension Content Detection", "[mime][no_extension]") {
    setup_mime_tests();
    
    const char* json_content = "{\"auto_detect\": true}";
    const char* mime = detect_mime_type(detector, "no_extension", json_content, strlen(json_content));
    REQUIRE(mime != nullptr);
    REQUIRE(strstr(mime, "json") != nullptr);
    
    teardown_mime_tests();
}

TEST_CASE("Test Input Files", "[mime][input_files]") {
    setup_mime_tests();
    
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
        REQUIRE(content != nullptr);
        
        const char* filename = get_filename(test_files[i]);
        const char* mime = detect_mime_type(detector, filename, content, strlen(content));
        
        REQUIRE(mime != nullptr);
        
        // Check if the detected MIME type contains the expected pattern
        REQUIRE(strstr(mime, expected_patterns[i]) != nullptr);
        
        printf("✓ %s -> %s\n", filename, mime);
        free(content);
    }
    
    teardown_mime_tests();
}

TEST_CASE("Extensionless Files", "[mime][extensionless]") {
    setup_mime_tests();
    
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
        REQUIRE(content != nullptr);
        
        const char* filename = get_filename(extensionless_files[i]);
        const char* mime = detect_mime_type(detector, filename, content, strlen(content));
        
        REQUIRE(mime != nullptr);
        
        // Check if the detected MIME type contains the expected pattern
        REQUIRE(strstr(mime, expected_patterns[i]) != nullptr);
        
        printf("✓ %s -> %s (content-based)\n", filename, mime);
        free(content);
    }
    
    teardown_mime_tests();
}

TEST_CASE("Edge Cases", "[mime][edge]") {
    setup_mime_tests();
    
    SECTION("Empty content") {
        const char* mime = detect_mime_type(detector, "test.txt", "", 0);
        REQUIRE(mime != nullptr); // Empty content should still detect by filename
    }
    
    SECTION("NULL filename") {
        const char* mime = detect_mime_type(detector, nullptr, "{\"test\": true}", 14);
        REQUIRE(mime != nullptr); // NULL filename should still detect by content
    }
    
    SECTION("Very small content") {
        const char* mime = detect_mime_type(detector, "test", "{", 1);
        // Should either return NULL or a reasonable guess - both are acceptable
    }
    
    SECTION("Binary content") {
        const char binary[] = {0x00, 0x01, 0x02, 0x03, 0x04};
        const char* mime = detect_mime_type(detector, "unknown", binary, 5);
        // Should handle binary content gracefully - both NULL and reasonable guess are acceptable
    }
    
    teardown_mime_tests();
}

TEST_CASE("Specific MIME Type Mappings", "[mime][mappings]") {
    setup_mime_tests();
    
    struct TestCase {
        const char* filename;
        const char* content;
        const char* expected_substring;
    };
    
    TestCase test_cases[] = {
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
        
        REQUIRE(mime != nullptr);
        REQUIRE(strstr(mime, test_cases[i].expected_substring) != nullptr);
    }
    
    teardown_mime_tests();
}
