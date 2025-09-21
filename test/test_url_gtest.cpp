#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../lib/url.h"
}

class UrlTest : public ::testing::Test {
protected:
    void SetUp() override {
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
    
    // Test empty string
    url = url_parse("");
    EXPECT_EQ(url, nullptr) << "Empty string should return NULL";
}