/*
 * URL Parser Extended Test Suite (Catch2)
 * ========================================
 * 
 * Comprehensive tests for the new URL parser implementation that replaces lexbor.
 * This test suite covers advanced URL parsing functionality including:
 * - Component parsing (username, password, host, port, etc.)
 * - Relative URL resolution and path normalization
 * - Enhanced relative URL resolution (WHATWG compliant)
 * - URL serialization and component reconstruction
 * - Security and edge case validation
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "../lib/url.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
}

// Global memory pool for tests
static VariableMemPool *test_pool = NULL;

// Setup function to initialize memory pool
void setup_url_extra_tests() {
    if (!test_pool) {
        MemPoolError err = pool_variable_init(&test_pool, 8192, 50);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
}

// Teardown function to cleanup memory pool
void teardown_url_extra_tests() {
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = NULL;
    }
}

TEST_CASE("Advanced Component Parsing", "[url_extra][components]") {
    setup_url_extra_tests();
    
    // Test parsing URL with all components
    Url* url = url_parse("https://user:pass@example.com:443/deep/path/file.html?param1=value1&param2=value2#section");
    REQUIRE(url != nullptr);
    
    REQUIRE(url->username != nullptr);
    REQUIRE(strcmp(url->username->chars, "user") == 0);
    
    REQUIRE(url->password != nullptr);
    REQUIRE(strcmp(url->password->chars, "pass") == 0);
    
    REQUIRE(url->hostname != nullptr);
    REQUIRE(strcmp(url->hostname->chars, "example.com") == 0);
    
    REQUIRE(url->port != nullptr);
    REQUIRE(strcmp(url->port->chars, "443") == 0);
    
    REQUIRE(url->pathname != nullptr);
    REQUIRE(strcmp(url->pathname->chars, "/deep/path/file.html") == 0);
    
    REQUIRE(url->search != nullptr);
    REQUIRE(strcmp(url->search->chars, "?param1=value1&param2=value2") == 0);
    
    REQUIRE(url->hash != nullptr);
    REQUIRE(strcmp(url->hash->chars, "#section") == 0);
    
    url_destroy(url);
    teardown_url_extra_tests();
}

TEST_CASE("File URL Parsing", "[url_extra][file]") {
    setup_url_extra_tests();
    
    Url* url = url_parse("file:///home/user/document.txt");
    REQUIRE(url != nullptr);
    
    REQUIRE(url->scheme == URL_SCHEME_FILE);
    REQUIRE(url->pathname != nullptr);
    REQUIRE(strcmp(url->pathname->chars, "/home/user/document.txt") == 0);
    
    url_destroy(url);
    teardown_url_extra_tests();
}

TEST_CASE("Basic Relative Resolution", "[url_extra][relative][basic]") {
    setup_url_extra_tests();
    
    Url* base = url_parse("https://example.com/a/b/c/d.html");
    REQUIRE(base != nullptr);
    
    Url* resolved = url_resolve_relative("sub/file.html", base);
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->pathname != nullptr);
    REQUIRE(strcmp(resolved->pathname->chars, "/a/b/c/sub/file.html") == 0);
    
    url_destroy(base);
    url_destroy(resolved);
    teardown_url_extra_tests();
}

TEST_CASE("Fragment Only Relative", "[url_extra][relative][fragment]") {
    setup_url_extra_tests();
    
    Url* base = url_parse("https://example.com/path/file.html?query=value");
    REQUIRE(base != nullptr);
    
    Url* resolved = url_resolve_relative("#new-fragment", base);
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->pathname != nullptr);
    REQUIRE(strcmp(resolved->pathname->chars, "/path/file.html") == 0);
    REQUIRE(resolved->search != nullptr);
    REQUIRE(strcmp(resolved->search->chars, "?query=value") == 0);
    REQUIRE(resolved->hash != nullptr);
    REQUIRE(strcmp(resolved->hash->chars, "#new-fragment") == 0);
    
    url_destroy(base);
    url_destroy(resolved);
    teardown_url_extra_tests();
}

TEST_CASE("Query Only Relative", "[url_extra][relative][query]") {
    setup_url_extra_tests();
    
    Url* base = url_parse("https://example.com/path/file.html#fragment");
    REQUIRE(base != nullptr);
    
    Url* resolved = url_resolve_relative("?new=query", base);
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->pathname != nullptr);
    REQUIRE(strcmp(resolved->pathname->chars, "/path/file.html") == 0);
    REQUIRE(resolved->search != nullptr);
    REQUIRE(strcmp(resolved->search->chars, "?new=query") == 0);
    
    url_destroy(base);
    url_destroy(resolved);
    teardown_url_extra_tests();
}

TEST_CASE("Absolute Path Relative", "[url_extra][relative][absolute_path]") {
    setup_url_extra_tests();
    
    Url* base = url_parse("https://example.com/old/path/file.html");
    REQUIRE(base != nullptr);
    
    Url* resolved = url_resolve_relative("/new/absolute/path.html", base);
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->pathname != nullptr);
    REQUIRE(strcmp(resolved->pathname->chars, "/new/absolute/path.html") == 0);
    
    url_destroy(base);
    url_destroy(resolved);
    teardown_url_extra_tests();
}

TEST_CASE("Dot Segment Resolution", "[url_extra][relative][dots]") {
    setup_url_extra_tests();
    
    Url* base = url_parse("https://example.com/a/b/c/d.html");
    REQUIRE(base != nullptr);
    
    Url* resolved = url_resolve_relative("../sibling.html", base);
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->pathname != nullptr);
    REQUIRE(strcmp(resolved->pathname->chars, "/a/b/sibling.html") == 0);
    
    url_destroy(base);
    url_destroy(resolved);
    teardown_url_extra_tests();
}

TEST_CASE("Complex Dot Segments", "[url_extra][relative][complex_dots]") {
    setup_url_extra_tests();
    
    Url* base = url_parse("https://example.com/a/b/c/d.html");
    REQUIRE(base != nullptr);
    
    Url* resolved = url_resolve_relative("../../other/./file.html", base);
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->pathname != nullptr);
    REQUIRE(strcmp(resolved->pathname->chars, "/a/other/file.html") == 0);
    
    url_destroy(base);
    url_destroy(resolved);
    teardown_url_extra_tests();
}

TEST_CASE("URL Serialization Roundtrip", "[url_extra][serialization]") {
    setup_url_extra_tests();
    
    const char* original = "https://example.com:8080/path?query=value#fragment";
    Url* url = url_parse(original);
    REQUIRE(url != nullptr);
    
    REQUIRE(url->href != nullptr);
    REQUIRE(strcmp(url->href->chars, original) == 0);
    
    url_destroy(url);
    teardown_url_extra_tests();
}

TEST_CASE("Component Based Construction", "[url_extra][construction]") {
    setup_url_extra_tests();
    
    Url* url = url_create();
    REQUIRE(url != nullptr);
    
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
    REQUIRE(url->scheme == URL_SCHEME_HTTPS);
    REQUIRE(strcmp(url->hostname->chars, "example.com") == 0);
    REQUIRE(strcmp(url->pathname->chars, "/test/path") == 0);
    REQUIRE(url->port_number == 443);
    
    url_destroy(url);
    teardown_url_extra_tests();
}

TEST_CASE("NULL Input Handling", "[url_extra][null_input]") {
    setup_url_extra_tests();
    
    SECTION("NULL URL parse") {
        Url* url = url_parse(NULL);
        REQUIRE(url == nullptr);
    }
    
    SECTION("Empty string") {
        Url* url = url_parse("");
        REQUIRE(url == nullptr);
    }
    
    SECTION("NULL base URL") {
        Url* url = url_resolve_relative("relative.html", NULL);
        REQUIRE(url == nullptr);
    }
    
    SECTION("NULL relative input") {
        Url* base = url_parse("https://example.com/path");
        REQUIRE(base != nullptr);
        Url* url = url_resolve_relative(NULL, base);
        REQUIRE(url == nullptr);
        url_destroy(base);
    }
    
    teardown_url_extra_tests();
}

TEST_CASE("Invalid Schemes", "[url_extra][invalid_schemes]") {
    setup_url_extra_tests();
    
    Url* url = url_parse("invalid-scheme://example.com");
    REQUIRE(url != nullptr);
    REQUIRE(url->scheme == URL_SCHEME_UNKNOWN);
    
    url_destroy(url);
    teardown_url_extra_tests();
}

TEST_CASE("Extremely Long URLs", "[url_extra][long_urls]") {
    setup_url_extra_tests();
    
    char long_url[10000];
    strcpy(long_url, "https://example.com/");
    for (int i = 0; i < 200; i++) {
        strcat(long_url, "very-long-path-segment/");
    }
    strcat(long_url, "file.html");
    
    Url* url = url_parse(long_url);
    REQUIRE(url != nullptr);
    
    if (url->pathname != NULL) {
        REQUIRE(strlen(url->pathname->chars) > 1000);
    }
    
    url_destroy(url);
    teardown_url_extra_tests();
}

TEST_CASE("Unicode and Special Characters", "[url_extra][unicode]") {
    setup_url_extra_tests();
    
    SECTION("URLs with spaces") {
        Url* url = url_parse("https://example.com/path with spaces");
        REQUIRE(url != nullptr);
        url_destroy(url);
    }
    
    SECTION("Percent-encoded URLs") {
        Url* url = url_parse("https://example.com/path%20with%20encoded%20spaces");
        REQUIRE(url != nullptr);
        url_destroy(url);
    }
    
    teardown_url_extra_tests();
}

TEST_CASE("Malformed Authority", "[url_extra][malformed]") {
    setup_url_extra_tests();
    
    SECTION("Invalid port") {
        Url* url = url_parse("https://user@:invalid:port/path");
        // Parser should either handle gracefully or reject - both are acceptable
        if (url != NULL) {
            url_destroy(url);
        }
    }
    
    SECTION("Missing authority") {
        Url* url = url_parse("https:///path/without/authority");
        if (url != NULL) {
            REQUIRE(url->pathname != nullptr);
            url_destroy(url);
        }
    }
    
    teardown_url_extra_tests();
}

TEST_CASE("Protocol Relative URLs", "[url_extra][protocol_relative]") {
    setup_url_extra_tests();
    
    Url* base = url_parse("https://current.com/current/path");
    REQUIRE(base != nullptr);
    
    Url* resolved = url_resolve_relative("//newhost.com/newpath", base);
    REQUIRE(resolved != nullptr);
    
    if (resolved->hostname != NULL) {
        REQUIRE(strcmp(resolved->hostname->chars, "newhost.com") == 0);
    }
    if (resolved->pathname != NULL) {
        REQUIRE(strcmp(resolved->pathname->chars, "/newpath") == 0);
    }
    
    url_destroy(base);
    url_destroy(resolved);
    teardown_url_extra_tests();
}

TEST_CASE("Memory Stress Test", "[url_extra][stress][memory]") {
    setup_url_extra_tests();
    
    SECTION("Basic stress test") {
        for (int i = 0; i < 100; i++) {
            Url* url = url_parse("https://example.com/test/path");
            REQUIRE(url != nullptr);
            url_destroy(url);
        }
    }
    
    SECTION("Different URL patterns") {
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
    
    teardown_url_extra_tests();
}

TEST_CASE("Resolution Stress Test", "[url_extra][stress][resolution]") {
    setup_url_extra_tests();
    
    Url* base = url_parse("https://example.com/deep/nested/path/file.html");
    REQUIRE(base != nullptr);
    
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
    teardown_url_extra_tests();
}
