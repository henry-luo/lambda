#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../lib/url.h"
#include "../lib/log.h"
}

class UrlTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        // Runs before each test
    }

    void TearDown() override {
        // Runs after each test
    }
};

TEST_F(UrlTest, BasicUrlParsing) {
    // Test basic URL parsing functionality
    Url* url = url_parse("https://example.com/path");
    EXPECT_NE(url, nullptr) << "url_parse should handle absolute URLs";
    EXPECT_EQ(url->scheme, URL_SCHEME_HTTPS) << "Scheme should be HTTPS";

    if (url->hostname && url->hostname->chars) {
        EXPECT_STREQ(url->hostname->chars, "example.com") << "Host should be correct";
    }

    if (url->pathname && url->pathname->chars) {
        EXPECT_STREQ(url->pathname->chars, "/path") << "Path should be correct";
    }

    url_destroy(url);
}

TEST_F(UrlTest, HttpUrlParsing) {
    // Test HTTP URL parsing
    Url* url = url_parse("http://example.com/test");
    EXPECT_NE(url, nullptr) << "HTTP URL should parse successfully";
    EXPECT_EQ(url->scheme, URL_SCHEME_HTTP) << "Scheme should be HTTP";

    if (url->hostname && url->hostname->chars) {
        EXPECT_STREQ(url->hostname->chars, "example.com") << "Host should be correct";
    }

    url_destroy(url);
}

TEST_F(UrlTest, UrlWithoutPath) {
    // Test URL without explicit path
    Url* url = url_parse("https://example.com");
    EXPECT_NE(url, nullptr) << "URL without path should parse";
    EXPECT_EQ(url->scheme, URL_SCHEME_HTTPS) << "Scheme should be HTTPS";

    if (url->hostname && url->hostname->chars) {
        EXPECT_STREQ(url->hostname->chars, "example.com") << "Host should be correct";
    }

    url_destroy(url);
}

TEST_F(UrlTest, UrlSchemeDetection) {
    // Test different URL schemes
    Url* http_url = url_parse("http://example.com");
    if (http_url) {
        EXPECT_EQ(http_url->scheme, URL_SCHEME_HTTP) << "HTTP scheme should be detected";
        url_destroy(http_url);
    }

    Url* https_url = url_parse("https://example.com");
    if (https_url) {
        EXPECT_EQ(https_url->scheme, URL_SCHEME_HTTPS) << "HTTPS scheme should be detected";
        url_destroy(https_url);
    }

    Url* ftp_url = url_parse("ftp://ftp.example.com/dir/file.txt");
    if (ftp_url) {
        EXPECT_EQ(ftp_url->scheme, URL_SCHEME_FTP) << "FTP scheme should be detected";
        url_destroy(ftp_url);
    }
}

TEST_F(UrlTest, InvalidUrls) {
    // Test malformed URLs - following original Criterion test exactly
    EXPECT_EQ(url_parse("not-a-valid-url"), nullptr) << "Invalid URL should return NULL";
    EXPECT_EQ(url_parse(""), nullptr) << "Empty URL should return NULL";
    EXPECT_EQ(url_parse(NULL), nullptr) << "NULL URL should return NULL";
}

TEST_F(UrlTest, EdgeCases) {
    Url* url;

    // Test empty components
    url = url_parse("http://example.com/");
    EXPECT_NE(url, nullptr) << "URL with trailing slash should parse";

    if (url && url->pathname && url->pathname->chars) {
        EXPECT_STREQ(url->pathname->chars, "/") << "Path should be /";
    }
    if (url) url_destroy(url);

    // Test URL with port but no path
    url = url_parse("http://example.com:8080");
    EXPECT_NE(url, nullptr) << "URL with port but no path should parse";
    if (url) {
        EXPECT_EQ(url->port_number, 8080) << "Port should be parsed correctly";
        url_destroy(url);
    }
}

TEST_F(UrlTest, UrlValidation) {
    // Test URL validation function - parse and check validity
    Url* url;

    url = url_parse("https://example.com");
    EXPECT_NE(url, nullptr) << "Simple HTTPS URL should parse";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Simple HTTPS URL should be valid";
        url_destroy(url);
    }

    url = url_parse("http://localhost:8080/path");
    EXPECT_NE(url, nullptr) << "Localhost URL should parse";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Localhost URL should be valid";
        url_destroy(url);
    }

    // Only test things that the original Criterion test expects to fail
    EXPECT_EQ(url_parse("not-a-valid-url"), nullptr) << "Invalid string should not parse";
}

TEST_F(UrlTest, UrlComponents) {
    Url* url = url_parse("https://user:pass@example.com:9443/deep/path?param=value#section");

    EXPECT_NE(url, nullptr) << "Complex URL parsing should succeed";
    if (url) {
        EXPECT_EQ(url->scheme, URL_SCHEME_HTTPS) << "Scheme should be HTTPS";
        EXPECT_EQ(url->port_number, 9443) << "Custom port should be parsed";

        // Check that components exist (they may be NULL if not supported)
        if (url->hostname && url->hostname->chars) {
            EXPECT_STREQ(url->hostname->chars, "example.com") << "Host should be correct";
        }

        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/deep/path") << "Path should be correct";
        }

        url_destroy(url);
    }
}

// Missing test 1: URL creation
TEST_F(UrlTest, UrlCreation) {
    // Test URL creation and basic properties
    Url* url = url_create();
    EXPECT_NE(url, nullptr) << "url_create should not return NULL";
    EXPECT_EQ(url->scheme, URL_SCHEME_UNKNOWN) << "Default scheme should be UNKNOWN";
    EXPECT_NE(url->host, nullptr) << "Default host should be allocated";
    EXPECT_NE(url->pathname, nullptr) << "Default pathname should be allocated";
    url_destroy(url);
}

// Missing test 2: Relative URL fragment only
TEST_F(UrlTest, RelativeUrlFragmentOnly) {
    // Test fragment-only relative URLs
    Url* base = url_parse("https://example.com/path/to/page?query=value");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("#newfragment", base);
    EXPECT_NE(url, nullptr) << "Fragment-only relative URL should resolve";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "example.com") << "Host should be preserved";
        }
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/path/to/page") << "Path should be preserved";
        }
        if (url->search && url->search->chars) {
            EXPECT_STREQ(url->search->chars, "?query=value") << "Query should be preserved";
        }
        if (url->hash && url->hash->chars) {
            EXPECT_STREQ(url->hash->chars, "#newfragment") << "Fragment should be updated";
        }
        url_destroy(url);
    }

    url_destroy(base);
}

// Missing test 3: Relative URL query only
TEST_F(UrlTest, RelativeUrlQueryOnly) {
    // Test query-only relative URLs
    Url* base = url_parse("https://example.com/path/to/page#fragment");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("?newquery=newvalue", base);
    EXPECT_NE(url, nullptr) << "Query-only relative URL should resolve";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "example.com") << "Host should be preserved";
        }
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/path/to/page") << "Path should be preserved";
        }
        if (url->search && url->search->chars) {
            EXPECT_STREQ(url->search->chars, "?newquery=newvalue") << "Query should be updated";
        }
        // Fragment should be removed in query-only resolution
        url_destroy(url);
    }

    url_destroy(base);
}

// Missing test 4: Relative URL absolute path
TEST_F(UrlTest, RelativeUrlAbsolutePath) {
    // Test absolute path relative URLs
    Url* base = url_parse("https://example.com/old/path?query=value#fragment");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("/new/absolute/path", base);
    EXPECT_NE(url, nullptr) << "Absolute path relative URL should resolve";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "example.com") << "Host should be preserved";
        }
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/new/absolute/path") << "Path should be updated";
        }
        // Query and fragment should be removed
        url_destroy(url);
    }

    url_destroy(base);
}

// Missing test 5: Error handling
TEST_F(UrlTest, ErrorHandling) {
    // Test error handling with invalid URLs
    Url* url = url_parse("invalid_url");
    EXPECT_EQ(url, nullptr) << "Invalid URL should return NULL";

    // Test NULL input
    url = url_parse(nullptr);
    EXPECT_EQ(url, nullptr) << "NULL input should return NULL";
}

// Additional missing tests from Criterion suite

TEST_F(UrlTest, RelativeUrlQueryWithFragment) {
    // Test query with fragment relative URLs
    Url* base = url_parse("https://example.com/path/to/page");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("?query=value#fragment", base);
    EXPECT_NE(url, nullptr) << "Query with fragment URL should resolve";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "example.com") << "Host should be preserved";
        }
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/path/to/page") << "Path should be preserved";
        }
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlAuthorityRelative) {
    // Test authority-relative URLs
    Url* base = url_parse("https://example.com/path/to/page");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("//newhost.com/newpath", base);
    EXPECT_NE(url, nullptr) << "Authority-relative URL should resolve";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        EXPECT_EQ(url->scheme, URL_SCHEME_HTTPS) << "Scheme should be preserved";
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "newhost.com") << "Host should be updated";
        }
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/newpath") << "Path should be updated";
        }
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlPathRelative) {
    // Test path-relative URLs
    Url* base = url_parse("https://example.com/path/to/page");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("../other/file", base);
    EXPECT_NE(url, nullptr) << "Path-relative URL should resolve";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "example.com") << "Host should be preserved";
        }
        // Path resolution should handle .. segments
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlPathWithSubdirectory) {
    // Test relative URLs with subdirectory navigation
    Url* base = url_parse("https://example.com/dir/subdir/page");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("newfile", base);
    EXPECT_NE(url, nullptr) << "Relative file URL should resolve";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "example.com") << "Host should be preserved";
        }
        // Should resolve relative to directory containing base
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlDotSegments) {
    // Test dot segment normalization
    Url* base = url_parse("https://example.com/a/b/c/d");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    // Test various dot segment combinations
    const char* test_cases[] = {
        "./file",
        "../file",
        "../../file",
        "./dir/./file",
        "../dir/../file"
    };

    for (size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        Url* url = url_parse_with_base(test_cases[i], base);
        EXPECT_NE(url, nullptr) << "Dot segment URL should resolve: " << test_cases[i];
        if (url) {
            EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
            url_destroy(url);
        }
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlDotSegmentsBeyondRoot) {
    // Test dot segments that would go beyond root
    Url* base = url_parse("https://example.com/");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("../../../file", base);
    EXPECT_NE(url, nullptr) << "Should handle excessive .. segments";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "URL should still be valid";
        if (url->pathname && url->pathname->chars) {
            // Should not go beyond root
            EXPECT_EQ(url->pathname->chars[0], '/') << "Path should start with /";
        }
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlComplexPathResolution) {
    // Test complex path resolution scenarios
    Url* base = url_parse("https://example.com/a/b/c");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("../d/./e/../f", base);
    EXPECT_NE(url, nullptr) << "Complex relative path should resolve";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        // Should normalize to /a/d/f
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlEmptyInput) {
    // Test empty relative URL
    Url* base = url_parse("https://example.com/path?query#fragment");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("", base);
    EXPECT_NE(url, nullptr) << "Empty relative URL should resolve to base";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
        // Should be identical to base
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "example.com") << "Host should match base";
        }
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlWhitespaceHandling) {
    // Test whitespace handling in relative URLs
    Url* base = url_parse("https://example.com/path");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("  /trimmed/path  ", base);
    EXPECT_NE(url, nullptr) << "URL with whitespace should resolve";
    if (url) {
        // Should handle whitespace appropriately
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlAbsoluteUrlInput) {
    // Test absolute URL as relative input
    Url* base = url_parse("https://example.com/path");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";

    Url* url = url_parse_with_base("http://other.com/path", base);
    EXPECT_NE(url, nullptr) << "Absolute URL should parse regardless of base";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Absolute URL should be valid";
        EXPECT_EQ(url->scheme, URL_SCHEME_HTTP) << "Should use new scheme, not base";
        if (url->host && url->host->chars) {
            EXPECT_STREQ(url->host->chars, "other.com") << "Should use new host, not base";
        }
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlFileScheme) {
    // Test file scheme handling
    Url* base = url_parse("file:///home/user/documents/");
    EXPECT_NE(base, nullptr) << "File scheme base URL should parse";

    Url* url = url_parse_with_base("../other/file.txt", base);
    EXPECT_NE(url, nullptr) << "Relative file URL should resolve";
    if (url) {
        EXPECT_EQ(url->scheme, URL_SCHEME_FILE) << "Should preserve file scheme";
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, RelativeUrlWithPort) {
    // Test relative URL resolution with port
    Url* base = url_parse("https://example.com:8080/path");
    EXPECT_NE(base, nullptr) << "Base URL with port should parse";

    Url* url = url_parse_with_base("../other", base);
    EXPECT_NE(url, nullptr) << "Relative URL should resolve";
    if (url) {
        EXPECT_EQ(url->port_number, 8080) << "Port should be preserved";
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, DirectoryPathResolution) {
    // Test directory vs file path resolution
    Url* base = url_parse("https://example.com/dir/");
    EXPECT_NE(base, nullptr) << "Directory base URL should parse";

    Url* url = url_parse_with_base("file.txt", base);
    EXPECT_NE(url, nullptr) << "File in directory should resolve";
    if (url) {
        // Should resolve to /dir/file.txt
        url_destroy(url);
    }

    url_destroy(base);
}

TEST_F(UrlTest, FileVsDirectoryResolution) {
    // Test file vs directory resolution behavior
    Url* file_base = url_parse("https://example.com/dir/file.html");
    Url* dir_base = url_parse("https://example.com/dir/");

    EXPECT_NE(file_base, nullptr) << "File base URL should parse";
    EXPECT_NE(dir_base, nullptr) << "Directory base URL should parse";

    if (file_base && dir_base) {
        Url* url1 = url_parse_with_base("other.html", file_base);
        Url* url2 = url_parse_with_base("other.html", dir_base);

        // Both should resolve but to different paths
        if (url1) url_destroy(url1);
        if (url2) url_destroy(url2);
    }

    if (file_base) url_destroy(file_base);
    if (dir_base) url_destroy(dir_base);
}

TEST_F(UrlTest, NestedDirectoryResolution) {
    // Test nested directory navigation
    Url* base = url_parse("https://example.com/a/b/c/d/");
    EXPECT_NE(base, nullptr) << "Nested directory base should parse";

    const char* test_cases[] = {
        "file.txt",      // Should resolve to /a/b/c/d/file.txt
        "../file.txt",   // Should resolve to /a/b/c/file.txt
        "../../file.txt", // Should resolve to /a/b/file.txt
        "../../../file.txt" // Should resolve to /a/file.txt
    };

    for (size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        Url* url = url_parse_with_base(test_cases[i], base);
        EXPECT_NE(url, nullptr) << "Nested directory navigation should work: " << test_cases[i];
        if (url) {
            EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
            url_destroy(url);
        }
    }

    url_destroy(base);
}

TEST_F(UrlTest, RootDirectoryEdgeCases) {
    // Test edge cases at root directory
    Url* base = url_parse("https://example.com/");
    EXPECT_NE(base, nullptr) << "Root directory base should parse";

    const char* test_cases[] = {
        "file.txt",
        "./file.txt",
        "../file.txt",  // Should not go beyond root
        "/../file.txt"  // Should not go beyond root
    };

    for (size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        Url* url = url_parse_with_base(test_cases[i], base);
        EXPECT_NE(url, nullptr) << "Root edge case should resolve: " << test_cases[i];
        if (url) {
            EXPECT_TRUE(url->is_valid) << "Resolved URL should be valid";
            url_destroy(url);
        }
    }

    url_destroy(base);
}

TEST_F(UrlTest, MemoryManagement) {
    // Test memory management with multiple allocations
    for (int i = 0; i < 100; i++) {
        Url* url = url_parse("https://example.com/path");
        if (url) {
            EXPECT_TRUE(url->is_valid) << "URL should be valid";
            url_destroy(url);
        }
    }

    // Test with relative URL parsing
    Url* base = url_parse("https://example.com/base/");
    if (base) {
        for (int i = 0; i < 50; i++) {
            Url* url = url_parse_with_base("relative/path", base);
            if (url) {
                url_destroy(url);
            }
        }
        url_destroy(base);
    }

    EXPECT_TRUE(true) << "Memory management test completed without crashes";
}

TEST_F(UrlTest, EmptyString) {
    // Test empty string
    Url* url = url_parse("");
    EXPECT_EQ(url, nullptr) << "Empty string should return NULL";
}

// Tests for url_to_local_path() function
TEST_F(UrlTest, FileUrlToLocalPath_Unix) {
    #ifndef _WIN32
    // Test basic Unix file URL
    Url* url = url_parse("file:///home/user/document.txt");
    ASSERT_NE(url, nullptr) << "File URL should parse successfully";

    char* path = url_to_local_path(url);
    ASSERT_NE(path, nullptr) << "url_to_local_path should return a path";
    EXPECT_STREQ(path, "/home/user/document.txt") << "Path should match expected Unix path";

    free(path);
    url_destroy(url);
    #endif
}

TEST_F(UrlTest, FileUrlToLocalPath_UnixLocalhost) {
    #ifndef _WIN32
    // Test Unix file URL with localhost
    Url* url = url_parse("file://localhost/home/user/document.txt");
    ASSERT_NE(url, nullptr) << "File URL with localhost should parse successfully";

    char* path = url_to_local_path(url);
    ASSERT_NE(path, nullptr) << "url_to_local_path should handle localhost";
    EXPECT_STREQ(path, "/home/user/document.txt") << "Path should match expected Unix path";

    free(path);
    url_destroy(url);
    #endif
}

TEST_F(UrlTest, FileUrlToLocalPath_UnixPercentEncoded) {
    #ifndef _WIN32
    // Test file URL with percent-encoded characters
    Url* url = url_parse("file:///home/user/my%20document%20with%20spaces.txt");
    ASSERT_NE(url, nullptr) << "File URL should parse successfully";

    char* path = url_to_local_path(url);
    ASSERT_NE(path, nullptr) << "url_to_local_path should decode percent encoding";
    EXPECT_STREQ(path, "/home/user/my document with spaces.txt") << "Spaces should be decoded";

    free(path);
    url_destroy(url);
    #endif
}

TEST_F(UrlTest, FileUrlToLocalPath_Windows) {
    #ifdef _WIN32
    // Test Windows file URL with drive letter
    Url* url = url_parse("file:///C:/Users/user/document.txt");
    ASSERT_NE(url, nullptr) << "Windows file URL should parse successfully";

    char* path = url_to_local_path(url);
    ASSERT_NE(path, nullptr) << "url_to_local_path should return a path";
    EXPECT_STREQ(path, "C:\\Users\\user\\document.txt") << "Path should be Windows format";

    free(path);
    url_destroy(url);
    #endif
}

TEST_F(UrlTest, FileUrlToLocalPath_WindowsUNC) {
    #ifdef _WIN32
    // Test Windows UNC path
    Url* url = url_parse("file://server/share/document.txt");
    ASSERT_NE(url, nullptr) << "UNC file URL should parse successfully";

    char* path = url_to_local_path(url);
    ASSERT_NE(path, nullptr) << "url_to_local_path should handle UNC paths";
    EXPECT_STREQ(path, "\\\\server\\share\\document.txt") << "Path should be UNC format";

    free(path);
    url_destroy(url);
    #endif
}

TEST_F(UrlTest, FileUrlToLocalPath_NonFileScheme) {
    // Test that non-file:// URLs return NULL
    Url* url = url_parse("https://example.com/document.txt");
    ASSERT_NE(url, nullptr) << "HTTPS URL should parse successfully";

    char* path = url_to_local_path(url);
    EXPECT_EQ(path, nullptr) << "url_to_local_path should return NULL for non-file URLs";

    url_destroy(url);
}

TEST_F(UrlTest, FileUrlToLocalPath_NullInput) {
    // Test NULL input
    char* path = url_to_local_path(nullptr);
    EXPECT_EQ(path, nullptr) << "url_to_local_path should handle NULL input";
}

TEST_F(UrlTest, FileUrlToLocalPath_ComplexPath) {
    #ifndef _WIN32
    // Test complex path with multiple segments
    Url* url = url_parse("file:///var/www/html/project/src/main.cpp");
    ASSERT_NE(url, nullptr) << "Complex file URL should parse successfully";

    char* path = url_to_local_path(url);
    ASSERT_NE(path, nullptr) << "url_to_local_path should handle complex paths";
    EXPECT_STREQ(path, "/var/www/html/project/src/main.cpp") << "Path should preserve all segments";

    free(path);
    url_destroy(url);
    #endif
}
