/*
 * URL Parser Test Suite
 * ====================
 * 
 * Tests for the new URL parser implementation that replaces lexbor.
 * This test suite covers basic URL parsing functionality for Phase 1.
 */

#include "../lib/unit_test/include/criterion/criterion.h"
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Project includes
#include "../lib/url.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"

// Test fixture setup
static VariableMemPool* pool;

void url_setup(void) {
    pool_variable_init(&pool, 8192, 50);
}

void url_teardown(void) {
    if (pool) {
        pool_variable_destroy(pool);
        pool = NULL;
    }
}

TestSuite(url_parser, .init = url_setup, .fini = url_teardown);

// =============================================================================
// BASIC URL FUNCTIONALITY TESTS
// =============================================================================

Test(url_parser, basic_url_parsing) {
    // Test basic URL parsing functionality
    
    // Test parsing a simple HTTP URL
    Url* url = url_parse("https://example.com/path");
    cr_assert_not_null(url, "url_parse should handle absolute URLs");
    cr_assert_eq(url->scheme, URL_SCHEME_HTTPS, "Scheme should be HTTPS");
    cr_assert_str_eq(url->host->chars, "example.com", "Host should be correct");
    cr_assert_str_eq(url->pathname->chars, "/path", "Path should be correct");
    url_destroy(url);
    
    // Test parsing a file URL
    Url* file_url = url_parse("file:///tmp/test.txt");
    if (file_url) {
        cr_assert_eq(file_url->scheme, URL_SCHEME_FILE, "Scheme should be FILE");
        cr_assert_str_eq(file_url->pathname->chars, "/tmp/test.txt", "Path should be correct");
        url_destroy(file_url);
    }
    
    // Test parsing an FTP URL
    Url* ftp_url = url_parse("ftp://ftp.example.com/dir/file.txt");
    if (ftp_url) {
        cr_assert_eq(ftp_url->scheme, URL_SCHEME_FTP, "Scheme should be FTP");
        cr_assert_str_eq(ftp_url->host->chars, "ftp.example.com", "Host should be correct");
        cr_assert_str_eq(ftp_url->pathname->chars, "/dir/file.txt", "Path should be correct");
        url_destroy(ftp_url);
    }
}

Test(url_parser, error_handling) {
    // Test error handling for invalid URLs
    Url* invalid_url = url_parse("not-a-valid-url");
    cr_assert_null(invalid_url, "Invalid URL should return NULL");
    
    // Test empty URL
    Url* empty_url = url_parse("");
    cr_assert_null(empty_url, "Empty URL should return NULL");
    
    // Test NULL URL
    Url* null_url = url_parse(NULL);
    cr_assert_null(null_url, "NULL URL should return NULL");
}

Test(url_parser, scheme_detection) {
    // Test different URL schemes
    Url* http_url = url_parse("http://example.com");
    if (http_url) {
        cr_assert_eq(http_url->scheme, URL_SCHEME_HTTP, "HTTP scheme should be detected");
        url_destroy(http_url);
    }
    
    Url* mailto_url = url_parse("mailto:test@example.com");
    if (mailto_url) {
        cr_assert_eq(mailto_url->scheme, URL_SCHEME_MAILTO, "Mailto scheme should be detected");
        url_destroy(mailto_url);
    }
    
    Url* unknown_url = url_parse("custom://example.com");
    if (unknown_url) {
        cr_assert_eq(unknown_url->scheme, URL_SCHEME_UNKNOWN, "Unknown scheme should be handled");
        url_destroy(unknown_url);
    }
}

Test(url_parser, url_creation) {
    // Test URL creation and basic properties
    Url* url = url_create();
    cr_assert_not_null(url, "url_create should not return NULL");
    cr_assert_eq(url->scheme, URL_SCHEME_UNKNOWN, "Default scheme should be UNKNOWN");
    cr_assert_not_null(url->host, "Default host should be allocated");
    cr_assert_not_null(url->pathname, "Default pathname should be allocated");
    url_destroy(url);
}

// =============================================================================
// PHASE 4: RELATIVE URL RESOLUTION TESTS
// =============================================================================

Test(url_parser, relative_url_fragment_only) {
    // Test fragment-only relative URLs
    Url* base = url_parse("https://example.com/path/to/page?query=value");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("#newfragment", base);
    cr_assert_not_null(url, "Fragment-only relative URL should resolve");
    cr_assert(url->is_valid, "Resolved URL should be valid");
    cr_assert_str_eq(url->host->chars, "example.com", "Host should be preserved");
    cr_assert_str_eq(url->pathname->chars, "/path/to/page", "Path should be preserved");
    cr_assert_str_eq(url->search->chars, "?query=value", "Query should be preserved");
    cr_assert_str_eq(url->hash->chars, "#newfragment", "Fragment should be updated");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_query_only) {
    // Test query-only relative URLs
    Url* base = url_parse("https://example.com/path/to/page?oldquery=oldvalue#fragment");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("?newquery=newvalue", base);
    cr_assert_not_null(url, "Query-only relative URL should resolve");
    cr_assert(url->is_valid, "Resolved URL should be valid");
    cr_assert_str_eq(url->host->chars, "example.com", "Host should be preserved");
    cr_assert_str_eq(url->pathname->chars, "/path/to/page", "Path should be preserved");
    cr_assert_str_eq(url->search->chars, "?newquery=newvalue", "Query should be updated");
    cr_assert_null(url->hash, "Fragment should be cleared");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_query_with_fragment) {
    // Test query with fragment relative URLs
    Url* base = url_parse("https://example.com/path/to/page");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("?query=value#fragment", base);
    cr_assert_not_null(url, "Query+fragment relative URL should resolve");
    cr_assert(url->is_valid, "Resolved URL should be valid");
    cr_assert_str_eq(url->search->chars, "?query=value", "Query should be set");
    cr_assert_str_eq(url->hash->chars, "#fragment", "Fragment should be set");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_authority_relative) {
    // Test authority-relative URLs (protocol-relative)
    Url* base = url_parse("https://oldexample.com/path/to/page");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("//newexample.com/newpath", base);
    cr_assert_not_null(url, "Authority-relative URL should resolve");
    cr_assert(url->is_valid, "Resolved URL should be valid");
    cr_assert_eq(url->scheme, URL_SCHEME_HTTPS, "Scheme should be preserved from base");
    cr_assert_str_eq(url->host->chars, "newexample.com", "Host should be updated");
    cr_assert_str_eq(url->pathname->chars, "/newpath", "Path should be updated");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_absolute_path) {
    // Test absolute path relative URLs
    Url* base = url_parse("https://example.com/old/path?query=value");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("/new/absolute/path", base);
    cr_assert_not_null(url, "Absolute path relative URL should resolve");
    cr_assert(url->is_valid, "Resolved URL should be valid");
    cr_assert_str_eq(url->host->chars, "example.com", "Host should be preserved");
    cr_assert_str_eq(url->pathname->chars, "/new/absolute/path", "Path should be absolute");
    cr_assert_null(url->search, "Query should be cleared");
    cr_assert_null(url->hash, "Fragment should be cleared");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_path_relative) {
    // Test path-relative URLs (most common case)
    Url* base = url_parse("https://example.com/path/to/page.html");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("other.html", base);
    cr_assert_not_null(url, "Path-relative URL should resolve");
    cr_assert(url->is_valid, "Resolved URL should be valid");
    cr_assert_str_eq(url->host->chars, "example.com", "Host should be preserved");
    cr_assert_str_eq(url->pathname->chars, "/path/to/other.html", "Path should be resolved relative to base directory");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_path_with_subdirectory) {
    // Test relative paths with subdirectory navigation
    Url* base = url_parse("https://example.com/path/to/page.html");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("subdir/file.html", base);
    cr_assert_not_null(url, "Subdirectory relative URL should resolve");
    cr_assert(url->is_valid, "Resolved URL should be valid");
    cr_assert_str_eq(url->pathname->chars, "/path/to/subdir/file.html", "Path should include subdirectory");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_dot_segments) {
    // Test relative URLs with dot segments (. and ..)
    Url* base = url_parse("https://example.com/path/to/deep/page.html");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    // Test "../" navigation
    Url* url1 = url_parse_with_base("../other.html", base);
    cr_assert_not_null(url1, "Parent directory navigation should work");
    cr_assert_str_eq(url1->pathname->chars, "/path/to/other.html", "Should go up one directory");
    
    // Test "../../" navigation
    Url* url2 = url_parse_with_base("../../other.html", base);
    cr_assert_not_null(url2, "Multiple parent directory navigation should work");
    cr_assert_str_eq(url2->pathname->chars, "/path/other.html", "Should go up two directories");
    
    // Test "./" current directory
    Url* url3 = url_parse_with_base("./other.html", base);
    cr_assert_not_null(url3, "Current directory navigation should work");
    cr_assert_str_eq(url3->pathname->chars, "/path/to/deep/other.html", "Should stay in same directory");
    
    url_destroy(url1);
    url_destroy(url2);
    url_destroy(url3);
    url_destroy(base);
}

Test(url_parser, relative_url_dot_segments_beyond_root) {
    // Test that .. navigation doesn't go beyond root
    Url* base = url_parse("https://example.com/single/page.html");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("../../../other.html", base);
    cr_assert_not_null(url, "Excessive parent navigation should not fail");
    cr_assert_str_eq(url->pathname->chars, "/other.html", "Should not go beyond root directory");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_complex_path_resolution) {
    // Test complex path resolution with mixed segments
    Url* base = url_parse("https://example.com/a/b/c/d/page.html");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("../../.././e/../f/./g.html", base);
    cr_assert_not_null(url, "Complex path should resolve");
    cr_assert_str_eq(url->pathname->chars, "/a/f/g.html", "Complex path should be normalized correctly");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_empty_input) {
    // Test empty input (should return copy of base)
    Url* base = url_parse("https://example.com/path?query=value#fragment");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("", base);
    cr_assert_not_null(url, "Empty input should resolve to base copy");
    cr_assert_str_eq(url->href->chars, base->href->chars, "Should be identical to base URL");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_whitespace_handling) {
    // Test that leading/trailing whitespace is handled
    Url* base = url_parse("https://example.com/path");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("  other.html  ", base);
    cr_assert_not_null(url, "Whitespace in relative URL should be handled");
    cr_assert_str_eq(url->pathname->chars, "/other.html", "Whitespace should be trimmed");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_absolute_url_input) {
    // Test that absolute URLs are not resolved against base
    Url* base = url_parse("https://example.com/path");
    cr_assert_not_null(base, "Base URL should parse successfully");
    
    Url* url = url_parse_with_base("http://other.com/absolute", base);
    cr_assert_not_null(url, "Absolute URL should parse independently");
    cr_assert_str_eq(url->host->chars, "other.com", "Should use absolute URL's host, not base");
    cr_assert_eq(url->scheme, URL_SCHEME_HTTP, "Should use absolute URL's scheme");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_file_scheme) {
    // Test relative URL resolution with file:// scheme
    Url* base = url_parse("file:///home/user/documents/file.txt");
    cr_assert_not_null(base, "Base file URL should parse successfully");
    
    Url* url = url_parse_with_base("../images/photo.jpg", base);
    cr_assert_not_null(url, "Relative file URL should resolve");
    cr_assert_eq(url->scheme, URL_SCHEME_FILE, "Should preserve file scheme");
    cr_assert_str_eq(url->pathname->chars, "/home/user/images/photo.jpg", "File path should resolve correctly");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, relative_url_with_port) {
    // Test relative URL resolution preserves port
    Url* base = url_parse("https://example.com:8443/path");
    cr_assert_not_null(base, "Base URL with port should parse successfully");
    
    Url* url = url_parse_with_base("other.html", base);
    cr_assert_not_null(url, "Relative URL should resolve with port preserved");
    cr_assert_eq(url->port_number, 8443, "Port should be preserved");
    cr_assert_str_eq(url->port->chars, "8443", "Port string should be preserved");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, directory_path_resolution) {
    // Test the specific bug case: directory paths ending with '/'
    Url* base = url_parse("file:///Users/henryluo/Projects/lambda/test/input/");
    cr_assert_not_null(base, "Base directory URL should parse successfully");
    
    Url* url = url_parse_with_base("test.csv", base);
    cr_assert_not_null(url, "Relative URL should resolve against directory");
    cr_assert_str_eq(url->pathname->chars, "/Users/henryluo/Projects/lambda/test/input/test.csv", 
                     "Directory path resolution should preserve all directory segments");
    
    url_destroy(url);
    url_destroy(base);
}

Test(url_parser, file_vs_directory_resolution) {
    // Test difference between file and directory base paths
    
    // File base (no trailing slash) - should exclude filename
    Url* file_base = url_parse("file:///path/to/file.txt");
    cr_assert_not_null(file_base, "File base URL should parse");
    
    Url* file_resolved = url_parse_with_base("other.txt", file_base);
    cr_assert_not_null(file_resolved, "File relative resolution should work");
    cr_assert_str_eq(file_resolved->pathname->chars, "/path/to/other.txt", 
                     "File base should exclude filename from resolution");
    
    // Directory base (trailing slash) - should preserve all segments
    Url* dir_base = url_parse("file:///path/to/dir/");
    cr_assert_not_null(dir_base, "Directory base URL should parse");
    
    Url* dir_resolved = url_parse_with_base("other.txt", dir_base);
    cr_assert_not_null(dir_resolved, "Directory relative resolution should work");
    cr_assert_str_eq(dir_resolved->pathname->chars, "/path/to/dir/other.txt", 
                     "Directory base should preserve all directory segments");
    
    url_destroy(file_base);
    url_destroy(file_resolved);
    url_destroy(dir_base);
    url_destroy(dir_resolved);
}

Test(url_parser, nested_directory_resolution) {
    // Test nested directory resolution with various relative paths
    Url* base = url_parse("https://example.com/deep/nested/directory/");
    cr_assert_not_null(base, "Nested directory base should parse");
    
    // Simple file in same directory
    Url* url1 = url_parse_with_base("file.txt", base);
    cr_assert_not_null(url1, "Simple file resolution should work");
    cr_assert_str_eq(url1->pathname->chars, "/deep/nested/directory/file.txt", 
                     "Simple file should resolve in same directory");
    
    // Subdirectory navigation
    Url* url2 = url_parse_with_base("subdir/file.txt", base);
    cr_assert_not_null(url2, "Subdirectory navigation should work");
    cr_assert_str_eq(url2->pathname->chars, "/deep/nested/directory/subdir/file.txt", 
                     "Subdirectory should be added to directory path");
    
    // Parent directory navigation
    Url* url3 = url_parse_with_base("../file.txt", base);
    cr_assert_not_null(url3, "Parent directory navigation should work");
    cr_assert_str_eq(url3->pathname->chars, "/deep/nested/file.txt", 
                     "Parent navigation should work from directory");
    
    url_destroy(base);
    url_destroy(url1);
    url_destroy(url2);
    url_destroy(url3);
}

Test(url_parser, root_directory_edge_cases) {
    // Test edge cases with root directory
    Url* root_base = url_parse("file:///");
    cr_assert_not_null(root_base, "Root directory should parse");
    
    Url* resolved = url_parse_with_base("file.txt", root_base);
    cr_assert_not_null(resolved, "Root directory resolution should work");
    cr_assert_str_eq(resolved->pathname->chars, "/file.txt", 
                     "File should resolve directly under root");
    
    url_destroy(root_base);
    url_destroy(resolved);
}



Test(url_parser, memory_management) {
    // Test that URLs are properly allocated and freed
    Url* url = url_parse("https://example.com/test");
    if (url) {
        // Verify the URL is properly allocated
        cr_assert_not_null(url->host, "Host should be allocated");
        cr_assert_not_null(url->pathname, "Path should be allocated");
        
        // Test destruction (should not crash)
        url_destroy(url);
    }
    
    // Test destroying NULL URL (should not crash)
    url_destroy(NULL);
}
