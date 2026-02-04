/**
 * GTest-based test suite for Path functionality
 * Tests path creation, traversal, and string conversion
 */

#include <gtest/gtest.h>
#include "../lambda/transpiler.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include <cstring>

extern "C" {
#include "../lib/strbuf.h"
}

// External path API (defined in path.c)
extern "C" {
    void path_init(void);
    Path* path_get_root(PathScheme scheme);
    Path* path_append(Path* parent, const char* segment);
    Path* path_append_len(Path* parent, const char* segment, size_t len);
    const char* path_get_scheme_name(Path* path);
    bool path_is_root(Path* path);
    int path_depth(Path* path);
    void path_to_string(Path* path, void* out);
    void path_to_os_path(Path* path, void* out);
    Path* path_get_root_by_name(const char* name);
}

// Thread-local context (defined in runner.cpp)
extern __thread EvalContext* context;

// Test fixture for Path tests
class PathTest : public ::testing::Test {
protected:
    Pool* pool;
    EvalContext test_context;
    Heap test_heap;

    void SetUp() override {
        log_init(NULL);
        pool = pool_create();

        // Set up a minimal context for path operations
        test_heap.pool = pool;
        test_heap.entries = nullptr;

        memset(&test_context, 0, sizeof(test_context));
        test_context.heap = &test_heap;
        test_context.pool = pool;

        // Set thread-local context
        context = &test_context;

        // Initialize path system
        path_init();
    }

    void TearDown() override {
        context = nullptr;
        if (pool) {
            pool_destroy(pool);
        }
    }
};

// Test basic root scheme creation
TEST_F(PathTest, RootSchemeCreation) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    ASSERT_NE(file_root, nullptr);
    EXPECT_EQ(file_root->type_id, LMD_TYPE_PATH);
    EXPECT_STREQ(file_root->name, "file");
    EXPECT_TRUE(path_is_root(file_root));

    Path* http_root = path_get_root(PATH_SCHEME_HTTP);
    ASSERT_NE(http_root, nullptr);
    EXPECT_STREQ(http_root->name, "http");
    EXPECT_TRUE(path_is_root(http_root));

    Path* https_root = path_get_root(PATH_SCHEME_HTTPS);
    ASSERT_NE(https_root, nullptr);
    EXPECT_STREQ(https_root->name, "https");

    Path* sys_root = path_get_root(PATH_SCHEME_SYS);
    ASSERT_NE(sys_root, nullptr);
    EXPECT_STREQ(sys_root->name, "sys");

    Path* rel_root = path_get_root(PATH_SCHEME_REL);
    ASSERT_NE(rel_root, nullptr);
    EXPECT_STREQ(rel_root->name, ".");

    Path* parent_root = path_get_root(PATH_SCHEME_PARENT);
    ASSERT_NE(parent_root, nullptr);
    EXPECT_STREQ(parent_root->name, "..");
}

// Test path appending
TEST_F(PathTest, PathAppend) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);

    // Append "etc" to file
    Path* etc = path_append(file_root, "etc");
    ASSERT_NE(etc, nullptr);
    EXPECT_STREQ(etc->name, "etc");
    EXPECT_EQ(etc->parent, file_root);
    EXPECT_FALSE(path_is_root(etc));

    // Append "hosts" to file.etc
    Path* hosts = path_append(etc, "hosts");
    ASSERT_NE(hosts, nullptr);
    EXPECT_STREQ(hosts->name, "hosts");
    EXPECT_EQ(hosts->parent, etc);
}

// Test path depth calculation
TEST_F(PathTest, PathDepth) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    EXPECT_EQ(path_depth(file_root), 1);  // just "file"

    Path* etc = path_append(file_root, "etc");
    EXPECT_EQ(path_depth(etc), 2);  // file.etc

    Path* hosts = path_append(etc, "hosts");
    EXPECT_EQ(path_depth(hosts), 3);  // file.etc.hosts

    Path* config = path_append(hosts, "config");
    EXPECT_EQ(path_depth(config), 4);  // file.etc.hosts.config
}

// Test get scheme name
TEST_F(PathTest, GetSchemeName) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    EXPECT_STREQ(path_get_scheme_name(file_root), "file");

    Path* etc = path_append(file_root, "etc");
    EXPECT_STREQ(path_get_scheme_name(etc), "file");

    Path* hosts = path_append(etc, "hosts");
    EXPECT_STREQ(path_get_scheme_name(hosts), "file");

    Path* http_root = path_get_root(PATH_SCHEME_HTTP);
    EXPECT_STREQ(path_get_scheme_name(http_root), "http");

    Path* domain = path_append(http_root, "example.com");
    EXPECT_STREQ(path_get_scheme_name(domain), "http");
}

// Test path to string conversion
TEST_F(PathTest, PathToString) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    Path* etc = path_append(file_root, "etc");
    Path* hosts = path_append(etc, "hosts");

    StrBuf* buf = strbuf_new();

    // Test root
    path_to_string(file_root, buf);
    EXPECT_STREQ(buf->str, "file");

    // Test file.etc.hosts
    strbuf_reset(buf);
    path_to_string(hosts, buf);
    EXPECT_STREQ(buf->str, "file.etc.hosts");

    strbuf_free(buf);
}

// Test path to OS path conversion (Unix style)
TEST_F(PathTest, PathToOsPath) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    Path* etc = path_append(file_root, "etc");
    Path* hosts = path_append(etc, "hosts");

    StrBuf* buf = strbuf_new();

    // file.etc.hosts -> /etc/hosts
    path_to_os_path(hosts, buf);
    EXPECT_STREQ(buf->str, "/etc/hosts");

    strbuf_free(buf);
}

// Test relative path to OS path
TEST_F(PathTest, RelativePathToOsPath) {
    Path* rel_root = path_get_root(PATH_SCHEME_REL);
    Path* src = path_append(rel_root, "src");
    Path* main = path_append(src, "main.cpp");

    StrBuf* buf = strbuf_new();

    // .src.main.cpp -> ./src/main.cpp
    path_to_os_path(main, buf);
    EXPECT_STREQ(buf->str, "./src/main.cpp");

    strbuf_free(buf);
}

// Test HTTP URL path
TEST_F(PathTest, HttpUrlPath) {
    Path* http_root = path_get_root(PATH_SCHEME_HTTP);
    Path* domain = path_append(http_root, "example.com");
    Path* api = path_append(domain, "api");
    Path* users = path_append(api, "users");

    StrBuf* buf = strbuf_new();

    // http.example.com.api.users -> http://example.com/api/users
    path_to_os_path(users, buf);
    EXPECT_STREQ(buf->str, "http://example.com/api/users");

    strbuf_free(buf);
}

// Test sys path
TEST_F(PathTest, SysPath) {
    Path* sys_root = path_get_root(PATH_SCHEME_SYS);
    Path* env = path_append(sys_root, "env");
    Path* home = path_append(env, "HOME");

    StrBuf* buf = strbuf_new();

    // sys.env.HOME -> sys://env/HOME
    path_to_os_path(home, buf);
    EXPECT_STREQ(buf->str, "sys://env/HOME");

    strbuf_free(buf);
}

// Test path_get_root_by_name
TEST_F(PathTest, GetRootByName) {
    Path* file_root = path_get_root_by_name("file");
    ASSERT_NE(file_root, nullptr);
    EXPECT_STREQ(file_root->name, "file");

    Path* http_root = path_get_root_by_name("http");
    ASSERT_NE(http_root, nullptr);
    EXPECT_STREQ(http_root->name, "http");

    Path* sys_root = path_get_root_by_name("sys");
    ASSERT_NE(sys_root, nullptr);
    EXPECT_STREQ(sys_root->name, "sys");

    Path* rel_root = path_get_root_by_name(".");
    ASSERT_NE(rel_root, nullptr);
    EXPECT_STREQ(rel_root->name, ".");

    // Unknown scheme should return NULL
    Path* unknown = path_get_root_by_name("unknown");
    EXPECT_EQ(unknown, nullptr);
}

// Test special characters in segment (quoting in string representation)
TEST_F(PathTest, SpecialCharacterSegments) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    Path* home = path_append(file_root, "home");
    Path* dotfile = path_append(home, ".bashrc");

    StrBuf* buf = strbuf_new();

    // Segment with dot should be quoted
    path_to_string(dotfile, buf);
    EXPECT_STREQ(buf->str, "file.home.'.bashrc'");

    strbuf_free(buf);
}

// Test path with segment containing hyphen
TEST_F(PathTest, HyphenSegment) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    Path* usr = path_append(file_root, "usr");
    Path* local = path_append(usr, "local-bin");

    StrBuf* buf = strbuf_new();

    // Segment with hyphen should be quoted
    path_to_string(local, buf);
    EXPECT_STREQ(buf->str, "file.usr.'local-bin'");

    strbuf_free(buf);
}

// Test null and edge cases
TEST_F(PathTest, NullAndEdgeCases) {
    // NULL path
    EXPECT_EQ(path_get_scheme_name(nullptr), nullptr);
    EXPECT_FALSE(path_is_root(nullptr));
    EXPECT_EQ(path_depth(nullptr), 0);

    // NULL segment append should return parent
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    Path* result = path_append(file_root, nullptr);
    EXPECT_EQ(result, file_root);

    // Empty segment should return parent
    result = path_append_len(file_root, "test", 0);
    EXPECT_EQ(result, file_root);
}

// Test path_append_len with explicit length
TEST_F(PathTest, AppendWithLength) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);

    // Only take first 3 chars from "testing"
    Path* seg = path_append_len(file_root, "testing", 3);
    ASSERT_NE(seg, nullptr);
    EXPECT_STREQ(seg->name, "tes");
}

// Test deep path construction
TEST_F(PathTest, DeepPath) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    Path* current = file_root;

    // Build a deep path: file.a.b.c.d.e.f.g.h.i.j
    const char* segments[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
    for (int i = 0; i < 10; i++) {
        current = path_append(current, segments[i]);
        ASSERT_NE(current, nullptr);
    }

    EXPECT_EQ(path_depth(current), 11);  // file + 10 segments
    EXPECT_STREQ(current->name, "j");

    StrBuf* buf = strbuf_new();
    path_to_string(current, buf);
    EXPECT_STREQ(buf->str, "file.a.b.c.d.e.f.g.h.i.j");
    strbuf_free(buf);
}
