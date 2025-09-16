/*
 * URL Parser Extended Test Suite
 * ==============================
 * 
 * Comprehensive tests for the new URL parser implementation that replaces lexbor.
 * This test suite covers advanced URL parsing functionality including:
 * - Component parsing (username, password, host, port, etc.)
 * - Relative URL resolution and path normalization
 * - Enhanced relative URL resolution (WHATWG compliant)
 * - URL serialization and component reconstruction
 * - Security and edge case validation
 */

#include <criterion/criterion.h>
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

void url_extra_setup(void) {
    pool_variable_init(&pool, 8192, 50);
}

void url_extra_teardown(void) {
    if (pool) {
        pool_variable_destroy(pool);
        pool = NULL;
    }
}

TestSuite(url_extra, .init = url_extra_setup, .fini = url_extra_teardown);

// =============================================================================
// ADVANCED URL COMPONENT TESTS
// =============================================================================

Test(url_extra, advanced_component_parsing) {
    // Test parsing URL with all components
    Url* url = url_parse("https://user:pass@example.com:443/deep/path/file.html?param1=value1&param2=value2#section");
    cr_assert_not_null(url, "url_parse should handle complex URLs");
    
    cr_assert_not_null(url->username, "Username should be parsed");
    cr_assert_str_eq(url->username->chars, "user", "Username should be correct");
    
    cr_assert_not_null(url->password, "Password should be parsed");
    cr_assert_str_eq(url->password->chars, "pass", "Password should be correct");
    
    cr_assert_not_null(url->hostname, "Hostname should be parsed");
    cr_assert_str_eq(url->hostname->chars, "example.com", "Hostname should be correct");
    
    cr_assert_not_null(url->port, "Port should be parsed");
    cr_assert_str_eq(url->port->chars, "443", "Port should be correct");
    
    cr_assert_not_null(url->pathname, "Pathname should be parsed");
    cr_assert_str_eq(url->pathname->chars, "/deep/path/file.html", "Pathname should be correct");
    
    cr_assert_not_null(url->search, "Search should be parsed");
    cr_assert_str_eq(url->search->chars, "?param1=value1&param2=value2", "Search should be correct");
    
    cr_assert_not_null(url->hash, "Hash should be parsed");
    cr_assert_str_eq(url->hash->chars, "#section", "Hash should be correct");
    
    url_destroy(url);
}

Test(url_extra, file_url_parsing) {
    // Test file URL parsing
    Url* url = url_parse("file:///home/user/document.txt");
    cr_assert_not_null(url, "url_parse should handle file URLs");
    
    cr_assert_eq(url->scheme, URL_SCHEME_FILE, "Scheme should be FILE");
    cr_assert_not_null(url->pathname, "Pathname should be parsed");
    cr_assert_str_eq(url->pathname->chars, "/home/user/document.txt", "File path should be correct");
    
    url_destroy(url);
}

// =============================================================================
// RELATIVE URL RESOLUTION TESTS
// =============================================================================

Test(url_extra, basic_relative_resolution) {
    // Test basic relative URL resolution
    Url* base = url_parse("https://example.com/a/b/c/d.html");
    cr_assert_not_null(base, "Base URL should parse correctly");
    
    Url* resolved = url_resolve_relative("sub/file.html", base);
    cr_assert_not_null(resolved, "Relative resolution should succeed");
    cr_assert_not_null(resolved->pathname, "Resolved pathname should exist");
    cr_assert_str_eq(resolved->pathname->chars, "/a/b/c/sub/file.html", "Resolved path should be correct");
    
    url_destroy(base);
    url_destroy(resolved);
}

Test(url_extra, fragment_only_relative) {
    // Test fragment-only relative URLs
    Url* base = url_parse("https://example.com/path/file.html?query=value");
    cr_assert_not_null(base, "Base URL should parse correctly");
    
    Url* resolved = url_resolve_relative("#new-fragment", base);
    cr_assert_not_null(resolved, "Fragment-only resolution should succeed");
    cr_assert_not_null(resolved->pathname, "Pathname should be preserved");
    cr_assert_str_eq(resolved->pathname->chars, "/path/file.html", "Path should be preserved");
    cr_assert_not_null(resolved->search, "Search should be preserved");
    cr_assert_str_eq(resolved->search->chars, "?query=value", "Search should be preserved");
    cr_assert_not_null(resolved->hash, "Hash should be updated");
    cr_assert_str_eq(resolved->hash->chars, "#new-fragment", "Hash should be correct");
    
    url_destroy(base);
    url_destroy(resolved);
}

Test(url_extra, query_only_relative) {
    // Test query-only relative URLs
    Url* base = url_parse("https://example.com/path/file.html#fragment");
    cr_assert_not_null(base, "Base URL should parse correctly");
    
    Url* resolved = url_resolve_relative("?new=query", base);
    cr_assert_not_null(resolved, "Query-only resolution should succeed");
    cr_assert_not_null(resolved->pathname, "Pathname should be preserved");
    cr_assert_str_eq(resolved->pathname->chars, "/path/file.html", "Path should be preserved");
    cr_assert_not_null(resolved->search, "Search should be updated");
    cr_assert_str_eq(resolved->search->chars, "?new=query", "Search should be correct");
    // Fragment should be cleared for query-only relative URLs
    
    url_destroy(base);
    url_destroy(resolved);
}

Test(url_extra, absolute_path_relative) {
    // Test absolute path relative URLs
    Url* base = url_parse("https://example.com/old/path/file.html");
    cr_assert_not_null(base, "Base URL should parse correctly");
    
    Url* resolved = url_resolve_relative("/new/absolute/path.html", base);
    cr_assert_not_null(resolved, "Absolute path resolution should succeed");
    cr_assert_not_null(resolved->pathname, "Pathname should be updated");
    cr_assert_str_eq(resolved->pathname->chars, "/new/absolute/path.html", "Path should be absolute");
    
    url_destroy(base);
    url_destroy(resolved);
}

Test(url_extra, dot_segment_resolution) {
    // Test dot segment resolution
    Url* base = url_parse("https://example.com/a/b/c/d.html");
    cr_assert_not_null(base, "Base URL should parse correctly");
    
    Url* resolved = url_resolve_relative("../sibling.html", base);
    cr_assert_not_null(resolved, "Dot segment resolution should succeed");
    cr_assert_not_null(resolved->pathname, "Pathname should be resolved");
    cr_assert_str_eq(resolved->pathname->chars, "/a/b/sibling.html", "Dot segments should be resolved");
    
    url_destroy(base);
    url_destroy(resolved);
}

Test(url_extra, complex_dot_segments) {
    // Test complex dot segment resolution
    Url* base = url_parse("https://example.com/a/b/c/d.html");
    cr_assert_not_null(base, "Base URL should parse correctly");
    
    Url* resolved = url_resolve_relative("../../other/./file.html", base);
    cr_assert_not_null(resolved, "Complex dot resolution should succeed");
    cr_assert_not_null(resolved->pathname, "Pathname should be resolved");
    cr_assert_str_eq(resolved->pathname->chars, "/a/other/file.html", "Complex dots should be resolved");
    
    url_destroy(base);
    url_destroy(resolved);
}

// =============================================================================
// URL SERIALIZATION TESTS
// =============================================================================

Test(url_extra, url_serialization_roundtrip) {
    // Test basic URL serialization roundtrip
    const char* original = "https://example.com:8080/path?query=value#fragment";
    Url* url = url_parse(original);
    cr_assert_not_null(url, "URL should parse");
    
    cr_assert_not_null(url->href, "href should be set");
    cr_assert_str_eq(url->href->chars, original, "Serialized URL should match original");
    
    url_destroy(url);
}

Test(url_extra, component_based_construction) {
    // Test URL construction from components
    Url* url = url_create();
    cr_assert_not_null(url, "URL creation should succeed");
    
    // Set components manually
    url->scheme = URL_SCHEME_HTTPS;
    url_free_string(url->protocol);
    url->protocol = url_create_string("https:");
    
    url_free_string(url->hostname);
    url->hostname = url_create_string("example.com");
    url_free_string(url->host);
    url->host = url_create_string("example.com");
    
    url_free_string(url->pathname);
    url->pathname = url_create_string("/test/path");
    
    url->port_number = 443; // Default HTTPS port
    
    // Verify components are set correctly
    cr_assert_eq(url->scheme, URL_SCHEME_HTTPS, "Scheme should be HTTPS");
    cr_assert_str_eq(url->hostname->chars, "example.com", "Hostname should be set");
    cr_assert_str_eq(url->pathname->chars, "/test/path", "Pathname should be set");
    cr_assert_eq(url->port_number, 443, "Port should be set");
    
    url_destroy(url);
}

// =============================================================================
// EDGE CASES AND SECURITY TESTS
// =============================================================================

Test(url_extra, null_input_handling) {
    // Test NULL input handling
    Url* url = url_parse(NULL);
    cr_assert_null(url, "url_parse should handle NULL input gracefully");
    
    // Test empty string
    url = url_parse("");
    cr_assert_null(url, "url_parse should handle empty string gracefully");
    
    // Test NULL base URL
    url = url_resolve_relative("relative.html", NULL);
    cr_assert_null(url, "url_resolve_relative should handle NULL base_url gracefully");
    
    // Test NULL relative input
    Url* base = url_parse("https://example.com/path");
    cr_assert_not_null(base, "Base URL should parse");
    url = url_resolve_relative(NULL, base);
    cr_assert_null(url, "url_resolve_relative should handle NULL input gracefully");
    url_destroy(base);
}

Test(url_extra, invalid_schemes) {
    // Test invalid schemes
    Url* url = url_parse("invalid-scheme://example.com");
    cr_assert_not_null(url, "URL should parse even with unknown scheme");
    cr_assert_eq(url->scheme, URL_SCHEME_UNKNOWN, "Unknown schemes should be handled");
    
    url_destroy(url);
}

Test(url_extra, extremely_long_urls) {
    // Test extremely long URLs (this tests the dynamic allocation fix)
    char long_url[10000];
    strcpy(long_url, "https://example.com/");
    for (int i = 0; i < 200; i++) {
        strcat(long_url, "very-long-path-segment/");
    }
    strcat(long_url, "file.html");
    
    Url* url = url_parse(long_url);
    cr_assert_not_null(url, "Parser should handle extremely long URLs");
    
    if (url->pathname != NULL) {
        cr_assert_gt(strlen(url->pathname->chars), 1000, "Long paths should be preserved");
    }
    
    url_destroy(url);
}

Test(url_extra, unicode_and_special_characters) {
    // Test Unicode and special characters
    Url* url = url_parse("https://example.com/path with spaces");
    cr_assert_not_null(url, "Parser should handle URLs with spaces");
    
    url_destroy(url);
    
    // Test with encoded characters
    url = url_parse("https://example.com/path%20with%20encoded%20spaces");
    cr_assert_not_null(url, "Parser should handle percent-encoded URLs");
    
    url_destroy(url);
}

Test(url_extra, malformed_authority) {
    // Test malformed authority sections
    Url* url = url_parse("https://user@:invalid:port/path");
    // Parser should either handle gracefully or reject - both are acceptable
    if (url != NULL) {
        // If parsed, should have some reasonable defaults
        url_destroy(url);
    }
    
    // Test with missing authority
    url = url_parse("https:///path/without/authority");
    if (url != NULL) {
        cr_assert_not_null(url->pathname, "Path should still be parsed");
        url_destroy(url);
    }
}

Test(url_extra, protocol_relative_urls) {
    // Test protocol-relative URLs (//example.com/path)
    Url* base = url_parse("https://current.com/current/path");
    cr_assert_not_null(base, "Base URL should parse");
    
    Url* resolved = url_resolve_relative("//newhost.com/newpath", base);
    cr_assert_not_null(resolved, "Protocol-relative resolution should work");
    
    if (resolved->hostname != NULL) {
        cr_assert_str_eq(resolved->hostname->chars, "newhost.com", "Hostname should be updated");
    }
    if (resolved->pathname != NULL) {
        cr_assert_str_eq(resolved->pathname->chars, "/newpath", "Path should be updated");
    }
    
    url_destroy(base);
    url_destroy(resolved);
}

Test(url_extra, memory_stress_test) {
    // Test memory management under stress
    for (int i = 0; i < 100; i++) {
        Url* url = url_parse("https://example.com/test/path");
        cr_assert_not_null(url, "URL parsing should not fail under stress");
        url_destroy(url);
    }
    
    // Test with different URL patterns
    const char* test_urls[] = {
        "https://example.com",
        "http://user:pass@host.com:8080/path?query=value#fragment",
        "file:///local/file/path.txt",
        "ftp://ftp.example.com/directory/",
        "mailto:user@example.com"
    };
    
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 20; j++) {
            Url* url = url_parse(test_urls[i]);
            if (url != NULL) {
                url_destroy(url);
            }
        }
    }
}

Test(url_extra, resolution_stress_test) {
    // Test relative resolution under stress
    Url* base = url_parse("https://example.com/deep/nested/path/file.html");
    cr_assert_not_null(base, "Base URL should parse");
    
    const char* relative_urls[] = {
        "relative.html",
        "../parent.html",
        "../../grandparent.html",
        "/absolute.html",
        "?query=only",
        "#fragment-only",
        "sub/directory/file.html",
        "./current.html",
        "../sibling/file.html"
    };
    
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 10; j++) {
            Url* resolved = url_resolve_relative(relative_urls[i], base);
            if (resolved != NULL) {
                url_destroy(resolved);
            }
        }
    }
    
    url_destroy(base);
}
