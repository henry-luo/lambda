/*
 * URL Parser Test Suite (Catch2)
 * ==============================
 * 
 * Tests for the new URL parser implementation that replaces lexbor.
 * This test suite covers basic URL parsing functionality for Phase 1.
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
void setup_url_tests() {
    if (!test_pool) {
        MemPoolError err = pool_variable_init(&test_pool, 8192, 50);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
}

// Teardown function to cleanup memory pool
void teardown_url_tests() {
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = NULL;
    }
}

TEST_CASE("Basic URL Parsing", "[url][basic]") {
    setup_url_tests();
    
    SECTION("Simple HTTP URL") {
        Url* url = url_parse("https://example.com/path");
        REQUIRE(url != nullptr);
        REQUIRE(url->scheme == URL_SCHEME_HTTPS);
        REQUIRE(strcmp(url->host->chars, "example.com") == 0);
        REQUIRE(strcmp(url->pathname->chars, "/path") == 0);
        url_destroy(url);
    }
    
    SECTION("File URL") {
        Url* file_url = url_parse("file:///tmp/test.txt");
        if (file_url) {
            REQUIRE(file_url->scheme == URL_SCHEME_FILE);
            REQUIRE(strcmp(file_url->pathname->chars, "/tmp/test.txt") == 0);
            url_destroy(file_url);
        }
    }
    
    SECTION("FTP URL") {
        Url* ftp_url = url_parse("ftp://ftp.example.com/dir/file.txt");
        if (ftp_url) {
            REQUIRE(ftp_url->scheme == URL_SCHEME_FTP);
            REQUIRE(strcmp(ftp_url->host->chars, "ftp.example.com") == 0);
            REQUIRE(strcmp(ftp_url->pathname->chars, "/dir/file.txt") == 0);
            url_destroy(ftp_url);
        }
    }
    
    teardown_url_tests();
}

TEST_CASE("URL Error Handling", "[url][error]") {
    setup_url_tests();
    
    SECTION("Invalid URL") {
        Url* invalid_url = url_parse("not-a-valid-url");
        REQUIRE(invalid_url == nullptr);
    }
    
    SECTION("Empty URL") {
        Url* empty_url = url_parse("");
        REQUIRE(empty_url == nullptr);
    }
    
    SECTION("NULL URL") {
        Url* null_url = url_parse(NULL);
        REQUIRE(null_url == nullptr);
    }
    
    teardown_url_tests();
}

TEST_CASE("URL Scheme Detection", "[url][scheme]") {
    setup_url_tests();
    
    SECTION("HTTP scheme") {
        Url* http_url = url_parse("http://example.com");
        if (http_url) {
            REQUIRE(http_url->scheme == URL_SCHEME_HTTP);
            url_destroy(http_url);
        }
    }
    
    SECTION("Mailto scheme") {
        Url* mailto_url = url_parse("mailto:test@example.com");
        if (mailto_url) {
            REQUIRE(mailto_url->scheme == URL_SCHEME_MAILTO);
            url_destroy(mailto_url);
        }
    }
    
    SECTION("Unknown scheme") {
        Url* unknown_url = url_parse("custom://example.com");
        if (unknown_url) {
            REQUIRE(unknown_url->scheme == URL_SCHEME_UNKNOWN);
            url_destroy(unknown_url);
        }
    }
    
    teardown_url_tests();
}

TEST_CASE("URL Creation", "[url][creation]") {
    setup_url_tests();
    
    Url* url = url_create();
    REQUIRE(url != nullptr);
    REQUIRE(url->scheme == URL_SCHEME_UNKNOWN);
    REQUIRE(url->host != nullptr);
    REQUIRE(url->pathname != nullptr);
    url_destroy(url);
    
    teardown_url_tests();
}

TEST_CASE("Relative URL Fragment Only", "[url][relative][fragment]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path/to/page?query=value");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("#newfragment", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->is_valid);
    REQUIRE(strcmp(url->host->chars, "example.com") == 0);
    REQUIRE(strcmp(url->pathname->chars, "/path/to/page") == 0);
    REQUIRE(strcmp(url->search->chars, "?query=value") == 0);
    REQUIRE(strcmp(url->hash->chars, "#newfragment") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Query Only", "[url][relative][query]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path/to/page?oldquery=oldvalue#fragment");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("?newquery=newvalue", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->is_valid);
    REQUIRE(strcmp(url->host->chars, "example.com") == 0);
    REQUIRE(strcmp(url->pathname->chars, "/path/to/page") == 0);
    REQUIRE(strcmp(url->search->chars, "?newquery=newvalue") == 0);
    REQUIRE(url->hash == nullptr);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Query with Fragment", "[url][relative][query_fragment]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path/to/page");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("?query=value#fragment", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->is_valid);
    REQUIRE(strcmp(url->search->chars, "?query=value") == 0);
    REQUIRE(strcmp(url->hash->chars, "#fragment") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Authority Relative", "[url][relative][authority]") {
    setup_url_tests();
    
    Url* base = url_parse("https://oldexample.com/path/to/page");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("//newexample.com/newpath", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->is_valid);
    REQUIRE(url->scheme == URL_SCHEME_HTTPS);
    REQUIRE(strcmp(url->host->chars, "newexample.com") == 0);
    REQUIRE(strcmp(url->pathname->chars, "/newpath") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Absolute Path", "[url][relative][absolute_path]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/old/path?query=value");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("/new/absolute/path", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->is_valid);
    REQUIRE(strcmp(url->host->chars, "example.com") == 0);
    REQUIRE(strcmp(url->pathname->chars, "/new/absolute/path") == 0);
    REQUIRE(url->search == nullptr);
    REQUIRE(url->hash == nullptr);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Path Relative", "[url][relative][path]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path/to/page.html");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("other.html", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->is_valid);
    REQUIRE(strcmp(url->host->chars, "example.com") == 0);
    REQUIRE(strcmp(url->pathname->chars, "/path/to/other.html") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Path with Subdirectory", "[url][relative][subdir]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path/to/page.html");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("subdir/file.html", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->is_valid);
    REQUIRE(strcmp(url->pathname->chars, "/path/to/subdir/file.html") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Dot Segments", "[url][relative][dots]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path/to/deep/page.html");
    REQUIRE(base != nullptr);
    
    SECTION("Parent directory navigation") {
        Url* url1 = url_parse_with_base("../other.html", base);
        REQUIRE(url1 != nullptr);
        REQUIRE(strcmp(url1->pathname->chars, "/path/to/other.html") == 0);
        url_destroy(url1);
    }
    
    SECTION("Multiple parent directory navigation") {
        Url* url2 = url_parse_with_base("../../other.html", base);
        REQUIRE(url2 != nullptr);
        REQUIRE(strcmp(url2->pathname->chars, "/path/other.html") == 0);
        url_destroy(url2);
    }
    
    SECTION("Current directory navigation") {
        Url* url3 = url_parse_with_base("./other.html", base);
        REQUIRE(url3 != nullptr);
        REQUIRE(strcmp(url3->pathname->chars, "/path/to/deep/other.html") == 0);
        url_destroy(url3);
    }
    
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Dot Segments Beyond Root", "[url][relative][dots_root]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/single/page.html");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("../../../other.html", base);
    REQUIRE(url != nullptr);
    REQUIRE(strcmp(url->pathname->chars, "/other.html") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Complex Path Resolution", "[url][relative][complex]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/a/b/c/d/page.html");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("../../.././e/../f/./g.html", base);
    REQUIRE(url != nullptr);
    REQUIRE(strcmp(url->pathname->chars, "/a/f/g.html") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Empty Input", "[url][relative][empty]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path?query=value#fragment");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("", base);
    REQUIRE(url != nullptr);
    REQUIRE(strcmp(url->href->chars, base->href->chars) == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Whitespace Handling", "[url][relative][whitespace]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("  other.html  ", base);
    REQUIRE(url != nullptr);
    REQUIRE(strcmp(url->pathname->chars, "/other.html") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL Absolute URL Input", "[url][relative][absolute_input]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/path");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("http://other.com/absolute", base);
    REQUIRE(url != nullptr);
    REQUIRE(strcmp(url->host->chars, "other.com") == 0);
    REQUIRE(url->scheme == URL_SCHEME_HTTP);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL File Scheme", "[url][relative][file]") {
    setup_url_tests();
    
    Url* base = url_parse("file:///home/user/documents/file.txt");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("../images/photo.jpg", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->scheme == URL_SCHEME_FILE);
    REQUIRE(strcmp(url->pathname->chars, "/home/user/images/photo.jpg") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Relative URL with Port", "[url][relative][port]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com:8443/path");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("other.html", base);
    REQUIRE(url != nullptr);
    REQUIRE(url->port_number == 8443);
    REQUIRE(strcmp(url->port->chars, "8443") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Directory Path Resolution", "[url][directory]") {
    setup_url_tests();
    
    Url* base = url_parse("file:///Users/henryluo/Projects/lambda/test/input/");
    REQUIRE(base != nullptr);
    
    Url* url = url_parse_with_base("test.csv", base);
    REQUIRE(url != nullptr);
    REQUIRE(strcmp(url->pathname->chars, "/Users/henryluo/Projects/lambda/test/input/test.csv") == 0);
    
    url_destroy(url);
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("File vs Directory Resolution", "[url][file_vs_dir]") {
    setup_url_tests();
    
    SECTION("File base (no trailing slash)") {
        Url* file_base = url_parse("file:///path/to/file.txt");
        REQUIRE(file_base != nullptr);
        
        Url* file_resolved = url_parse_with_base("other.txt", file_base);
        REQUIRE(file_resolved != nullptr);
        REQUIRE(strcmp(file_resolved->pathname->chars, "/path/to/other.txt") == 0);
        
        url_destroy(file_base);
        url_destroy(file_resolved);
    }
    
    SECTION("Directory base (trailing slash)") {
        Url* dir_base = url_parse("file:///path/to/dir/");
        REQUIRE(dir_base != nullptr);
        
        Url* dir_resolved = url_parse_with_base("other.txt", dir_base);
        REQUIRE(dir_resolved != nullptr);
        REQUIRE(strcmp(dir_resolved->pathname->chars, "/path/to/dir/other.txt") == 0);
        
        url_destroy(dir_base);
        url_destroy(dir_resolved);
    }
    
    teardown_url_tests();
}

TEST_CASE("Nested Directory Resolution", "[url][nested_dir]") {
    setup_url_tests();
    
    Url* base = url_parse("https://example.com/deep/nested/directory/");
    REQUIRE(base != nullptr);
    
    SECTION("Simple file in same directory") {
        Url* url1 = url_parse_with_base("file.txt", base);
        REQUIRE(url1 != nullptr);
        REQUIRE(strcmp(url1->pathname->chars, "/deep/nested/directory/file.txt") == 0);
        url_destroy(url1);
    }
    
    SECTION("Subdirectory navigation") {
        Url* url2 = url_parse_with_base("subdir/file.txt", base);
        REQUIRE(url2 != nullptr);
        REQUIRE(strcmp(url2->pathname->chars, "/deep/nested/directory/subdir/file.txt") == 0);
        url_destroy(url2);
    }
    
    SECTION("Parent directory navigation") {
        Url* url3 = url_parse_with_base("../file.txt", base);
        REQUIRE(url3 != nullptr);
        REQUIRE(strcmp(url3->pathname->chars, "/deep/nested/file.txt") == 0);
        url_destroy(url3);
    }
    
    url_destroy(base);
    teardown_url_tests();
}

TEST_CASE("Root Directory Edge Cases", "[url][root_edge]") {
    setup_url_tests();
    
    Url* root_base = url_parse("file:///");
    REQUIRE(root_base != nullptr);
    
    Url* resolved = url_parse_with_base("file.txt", root_base);
    REQUIRE(resolved != nullptr);
    REQUIRE(strcmp(resolved->pathname->chars, "/file.txt") == 0);
    
    url_destroy(root_base);
    url_destroy(resolved);
    teardown_url_tests();
}

TEST_CASE("URL Memory Management", "[url][memory]") {
    setup_url_tests();
    
    SECTION("Proper allocation and deallocation") {
        Url* url = url_parse("https://example.com/test");
        if (url) {
            REQUIRE(url->host != nullptr);
            REQUIRE(url->pathname != nullptr);
            url_destroy(url);
        }
    }
    
    SECTION("Destroying NULL URL") {
        url_destroy(NULL); // Should not crash
    }
    
    teardown_url_tests();
}
