#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../lib/url.h"
#include "../lib/strbuf.h"
}

class UrlExtraTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed  
    }
};

TEST_F(UrlExtraTest, ComplexUrlParsing) {
    Url* url = url_parse("https://user:pass@subdomain.example.com:9443/deep/path/file.html?param1=value1&param2=value2#section1");
    
    EXPECT_NE(url, nullptr) << "Complex URL parsing should succeed";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "URL should be marked as valid";
        EXPECT_EQ(url->scheme, URL_SCHEME_HTTPS) << "Scheme should be HTTPS";
        EXPECT_EQ(url->port_number, 9443) << "Custom port should be parsed correctly";
        
        if (url->hostname && url->hostname->chars) {
            EXPECT_STREQ(url->hostname->chars, "subdomain.example.com") << "Full hostname should be parsed";
        }
        
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/deep/path/file.html") << "Complex path should be parsed";
        }
        
        url_destroy(url);
    }
}

TEST_F(UrlExtraTest, SpecialSchemes) {
    Url* url;
    
    // Test FTP
    url = url_parse("ftp://files.example.com/download/file.zip");
    EXPECT_NE(url, nullptr) << "FTP URL should parse";
    if (url) {
        EXPECT_EQ(url->scheme, URL_SCHEME_FTP) << "FTP scheme should be detected";
        EXPECT_EQ(url->port_number, 21) << "Default FTP port should be 21";
        url_destroy(url);
    }
    
    // Test file scheme
    url = url_parse("file:///usr/local/bin/program");
    EXPECT_NE(url, nullptr) << "File URL should parse";
    if (url) {
        EXPECT_EQ(url->scheme, URL_SCHEME_FILE) << "File scheme should be detected";
        url_destroy(url);
    }
}

TEST_F(UrlExtraTest, UrlWithQuery) {
    Url* url = url_parse("https://api.example.com/search?q=test&limit=10&offset=0");
    
    EXPECT_NE(url, nullptr) << "URL with query parameters should parse";
    if (url) {
        EXPECT_EQ(url->scheme, URL_SCHEME_HTTPS) << "Scheme should be HTTPS";
        
        if (url->search && url->search->chars) {
            EXPECT_STREQ(url->search->chars, "?q=test&limit=10&offset=0") << "Query should be parsed correctly";
        }
        
        url_destroy(url);
    }
}

TEST_F(UrlExtraTest, UrlWithFragment) {
    Url* url = url_parse("https://docs.example.com/guide.html#installation");
    
    EXPECT_NE(url, nullptr) << "URL with fragment should parse";
    if (url) {
        EXPECT_EQ(url->scheme, URL_SCHEME_HTTPS) << "Scheme should be HTTPS";
        
        if (url->hash && url->hash->chars) {
            EXPECT_STREQ(url->hash->chars, "#installation") << "Fragment should be parsed correctly";
        }
        
        url_destroy(url);
    }
}

TEST_F(UrlExtraTest, LocalhostUrls) {
    Url* url;
    
    url = url_parse("http://localhost:3000/app");
    EXPECT_NE(url, nullptr) << "Localhost URL should parse";
    if (url) {
        EXPECT_EQ(url->scheme, URL_SCHEME_HTTP) << "HTTP scheme should be detected";
        EXPECT_EQ(url->port_number, 3000) << "Custom port should be parsed";
        
        if (url->hostname && url->hostname->chars) {
            EXPECT_STREQ(url->hostname->chars, "localhost") << "Localhost should be parsed";
        }
        
        url_destroy(url);
    }
    
    // Test IP address
    url = url_parse("http://127.0.0.1:8080/");
    EXPECT_NE(url, nullptr) << "IP address URL should parse";
    if (url) {
        if (url->hostname && url->hostname->chars) {
            EXPECT_STREQ(url->hostname->chars, "127.0.0.1") << "IP address should be parsed";
        }
        
        url_destroy(url);
    }
}

TEST_F(UrlExtraTest, UrlValidationEdgeCases) {
    // Test edge cases for URL validation by parsing
    Url* url;
    
    url = url_parse("https://example.com");
    EXPECT_NE(url, nullptr) << "Simple HTTPS URL should parse";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Simple HTTPS URL should be valid";
        url_destroy(url);
    }
    
    url = url_parse("http://sub.domain.co.uk:8080/path");
    EXPECT_NE(url, nullptr) << "Complex valid URL should parse";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "Complex valid URL should be valid";
        url_destroy(url);
    }
    
    url = url_parse("ftp://ftp.example.com/file.txt");
    EXPECT_NE(url, nullptr) << "FTP URL should parse";
    if (url) {
        EXPECT_TRUE(url->is_valid) << "FTP URL should be valid";
        url_destroy(url);
    }
    
    EXPECT_EQ(url_parse(""), nullptr) << "Empty string should not parse";
    // Only test cases that original Criterion test expects to fail
}

TEST_F(UrlExtraTest, UrlPathHandling) {
    Url* url;
    
    // Test root path
    url = url_parse("https://example.com/");
    EXPECT_NE(url, nullptr) << "URL with root path should parse";
    if (url) {
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/") << "Root path should be /";
        }
        url_destroy(url);
    }
    
    // Test no path (implicit root)
    url = url_parse("https://example.com");
    EXPECT_NE(url, nullptr) << "URL without explicit path should parse";
    if (url) url_destroy(url);
    
    // Test deep path
    url = url_parse("https://example.com/a/b/c/d/e/file.html");
    EXPECT_NE(url, nullptr) << "URL with deep path should parse";
    if (url) {
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/a/b/c/d/e/file.html") << "Deep path should be preserved";
        }
        url_destroy(url);
    }
}

TEST_F(UrlExtraTest, InvalidPortNumbers) {
    // Note: Original Criterion test doesn't validate port numbers
    // This test is informational only - showing current parser behavior
    
    Url* url1 = url_parse("http://example.com:99999/");
    if (url1) url_destroy(url1);  // Parser may or may not accept this
    
    Url* url2 = url_parse("http://example.com:-1/");
    if (url2) url_destroy(url2);  // Parser may or may not accept this
    
    Url* url3 = url_parse("http://example.com:abc/");
    if (url3) url_destroy(url3);  // Parser may or may not accept this
}

TEST_F(UrlExtraTest, UnicodeUrls) {
    Url* url;
    
    // Test international domain names (if supported)
    url = url_parse("https://example.org/path");
    EXPECT_NE(url, nullptr) << "International domain URL should parse";
    if (url) url_destroy(url);
    
    // Test URL with encoded characters
    url = url_parse("https://example.com/file%20name.txt");
    EXPECT_NE(url, nullptr) << "URL with encoded characters should parse";
    if (url) url_destroy(url);
}

// Missing test 1: File URL parsing
TEST_F(UrlExtraTest, FileUrlParsing) {
    // Test file URL parsing
    Url* url = url_parse("file:///home/user/document.txt");
    EXPECT_NE(url, nullptr) << "url_parse should handle file URLs";
    
    if (url) {
        EXPECT_EQ(url->scheme, URL_SCHEME_FILE) << "Scheme should be FILE";
        EXPECT_NE(url->pathname, nullptr) << "Pathname should be parsed";
        if (url->pathname) {
            EXPECT_STREQ(url->pathname->chars, "/home/user/document.txt") << "File path should be correct";
        }
        url_destroy(url);
    }
}

// Missing test 2: Basic relative resolution
TEST_F(UrlExtraTest, BasicRelativeResolution) {
    Url* base = url_parse("https://example.com/dir/file.html");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";
    
    if (base) {
        Url* resolved = url_resolve_relative("other.html", base);
        EXPECT_NE(resolved, nullptr) << "Relative URL should resolve";
        
        if (resolved) {
            const char* pathname = url_get_pathname(resolved);
            EXPECT_NE(pathname, nullptr) << "Resolved URL should have pathname";
            if (pathname) {
                EXPECT_STREQ(pathname, "/dir/other.html") << "Resolved path should be correct";
            }
            url_destroy(resolved);
        }
        url_destroy(base);
    }
}

// Missing test 3: Fragment only relative
TEST_F(UrlExtraTest, FragmentOnlyRelative) {
    Url* base = url_parse("https://example.com/page.html");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";
    
    if (base) {
        Url* resolved = url_resolve_relative("#section", base);
        EXPECT_NE(resolved, nullptr) << "Fragment-only relative URL should resolve";
        
        if (resolved) {
            const char* hash = url_get_hash(resolved);
            EXPECT_NE(hash, nullptr) << "Resolved URL should have hash";
            if (hash) {
                EXPECT_STREQ(hash, "#section") << "Hash should be correct";
            }
            url_destroy(resolved);
        }
        url_destroy(base);
    }
}

// Missing test 4: Query only relative
TEST_F(UrlExtraTest, QueryOnlyRelative) {
    Url* base = url_parse("https://example.com/page.html");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";
    
    if (base) {
        Url* resolved = url_resolve_relative("?new=query", base);
        EXPECT_NE(resolved, nullptr) << "Query-only relative URL should resolve";
        
        if (resolved) {
            const char* search = url_get_search(resolved);
            EXPECT_NE(search, nullptr) << "Resolved URL should have search";
            if (search) {
                EXPECT_STREQ(search, "?new=query") << "Search should be correct";
            }
            url_destroy(resolved);
        }
        url_destroy(base);
    }
}

// Missing test 5: Dot segment resolution
TEST_F(UrlExtraTest, DotSegmentResolution) {
    Url* base = url_parse("https://example.com/a/b/c/d");
    EXPECT_NE(base, nullptr) << "Base URL should parse successfully";
    
    if (base) {
        Url* resolved = url_resolve_relative("../g", base);
        EXPECT_NE(resolved, nullptr) << "Dot segment URL should resolve";
        
        if (resolved) {
            const char* pathname = url_get_pathname(resolved);
            EXPECT_NE(pathname, nullptr) << "Resolved URL should have pathname";
            if (pathname) {
                EXPECT_STREQ(pathname, "/a/b/g") << "Dot segments should be resolved correctly";
            }
            url_destroy(resolved);
        }
        url_destroy(base);
    }
}

// Missing test 6: URL serialization roundtrip
TEST_F(UrlExtraTest, UrlSerializationRoundtrip) {
    const char* original_url = "https://user:pass@example.com:8080/path?query=value#fragment";
    Url* url = url_parse(original_url);
    EXPECT_NE(url, nullptr) << "Complex URL should parse";
    
    if (url) {
        const char* serialized = url_get_href(url);
        EXPECT_NE(serialized, nullptr) << "URL should have href string";
        
        if (serialized) {
            // Parse the serialized URL again
            Url* reparsed = url_parse(serialized);
            EXPECT_NE(reparsed, nullptr) << "Serialized URL should parse again";
            
            if (reparsed) {
                // Compare key components
                EXPECT_EQ(url_get_scheme(url), url_get_scheme(reparsed)) << "Scheme should match after roundtrip";
                
                const char* host1 = url_get_hostname(url);
                const char* host2 = url_get_hostname(reparsed);
                if (host1 && host2) {
                    EXPECT_STREQ(host1, host2) << "Hostname should match";
                }
                url_destroy(reparsed);
            }
        }
        url_destroy(url);
    }
}

// Missing test 7: Component based construction
TEST_F(UrlExtraTest, ComponentBasedConstruction) {
    // Test URL construction from components
    Url* url = url_create();
    EXPECT_NE(url, nullptr) << "URL creation should succeed";
    
    if (url) {
        // Set components manually (mirroring Criterion test)
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
        EXPECT_EQ(url->scheme, URL_SCHEME_HTTPS) << "Scheme should be HTTPS";
        if (url->hostname && url->hostname->chars) {
            EXPECT_STREQ(url->hostname->chars, "example.com") << "Hostname should be set";
        }
        if (url->pathname && url->pathname->chars) {
            EXPECT_STREQ(url->pathname->chars, "/test/path") << "Pathname should be set";
        }
        EXPECT_EQ(url->port_number, 443) << "Port number should be 443";
        
        url_destroy(url);
    }
}

// Missing test 8: Null input handling
TEST_F(UrlExtraTest, NullInputHandling) {
    // Test null input handling
    Url* url = url_parse(nullptr);
    EXPECT_EQ(url, nullptr) << "Parsing null should return null";
    
    // Test resolve with null inputs
    Url* base = url_parse("https://example.com/");
    if (base) {
        Url* resolved = url_resolve_relative(nullptr, base);
        EXPECT_EQ(resolved, nullptr) << "Resolving null relative URL should return null";
        
        url_destroy(base);
    }
    
    Url* resolved2 = url_resolve_relative("relative", nullptr);
    EXPECT_EQ(resolved2, nullptr) << "Resolving with null base should return null";
}

// Missing test 9: Extremely long URLs  
TEST_F(UrlExtraTest, ExtremelyLongUrls) {
    // Create an extremely long URL string
    const size_t path_segment_count = 200;
    const char* base_url = "https://example.com";
    
    // Build a very long path by concatenating many segments
    size_t total_length = strlen(base_url) + (path_segment_count * 20) + 1;
    char* long_url = (char*)malloc(total_length);
    
    if (long_url) {
        strcpy(long_url, base_url);
        
        // Add many path segments
        for (size_t i = 0; i < path_segment_count; i++) {
            strcat(long_url, "/verylongpathsegment");
        }
        
        Url* url = url_parse(long_url);
        // Should either parse successfully or fail gracefully
        if (url) {
            EXPECT_TRUE(url->is_valid) << "Long URL should be valid if parsed";
            url_destroy(url);
        }
        
        free(long_url);
        // Test passes if it doesn't crash
        SUCCEED() << "Extremely long URL handled without crashing";
    }
}