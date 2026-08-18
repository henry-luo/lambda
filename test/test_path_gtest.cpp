/**
 * GTest-based test suite for Path functionality
 * Tests path creation, traversal, and string conversion
 */

#include <gtest/gtest.h>
#include "../lambda/lambda-data.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/shell.h"
#include "../lib/mem.h"
#include <cstring>

extern "C" {
#include "../lib/strbuf.h"
}

// External path API (defined in path.c)
extern "C" {
    void path_init(void);
    void path_reset(void);
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

static Pool* path_test_pool = nullptr;

static Pool* test_path_pool_provider(void) {
    return path_test_pool;
}

// Test fixture for Path tests
class PathTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        log_init(NULL);
        pool = pool_create();
        // Path allocation is selected by its registered provider, so this
        // core/data test must not manufacture a runtime TLS context.
        path_test_pool = pool;
        path_register_pool_provider(test_path_pool_provider);

        // Initialize path system
        path_init();
    }

    void TearDown() override {
        path_reset();
        path_test_pool = nullptr;
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

    Path* logical_root = path_get_root(PATH_SCHEME_LOGICAL);
    ASSERT_NE(logical_root, nullptr);
    EXPECT_STREQ(path_get_scheme_name(logical_root), "/");
    EXPECT_TRUE(path_is_root(logical_root));
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

    // Explicit file roots use the new file./ spelling.
    path_to_string(file_root, buf);
    EXPECT_STREQ(buf->str, "file./");

    // Test explicit file path spelling.
    strbuf_reset(buf);
    path_to_string(hosts, buf);
    EXPECT_STREQ(buf->str, "file./.etc.hosts");

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

    Path* logical_root = path_get_root_by_name("/");
    ASSERT_NE(logical_root, nullptr);
    EXPECT_EQ(path_get_scheme(logical_root), PATH_SCHEME_LOGICAL);

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

    // Segment with dot is quoted while retaining the file authority.
    path_to_string(dotfile, buf);
    EXPECT_STREQ(buf->str, "file./.home.'.bashrc'");

    strbuf_free(buf);
}

// Test path with segment containing hyphen
TEST_F(PathTest, HyphenSegment) {
    Path* file_root = path_get_root(PATH_SCHEME_FILE);
    Path* usr = path_append(file_root, "usr");
    Path* local = path_append(usr, "local-bin");

    StrBuf* buf = strbuf_new();

    // Segment with hyphen is quoted while retaining the file authority.
    path_to_string(local, buf);
    EXPECT_STREQ(buf->str, "file./.usr.'local-bin'");

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

    // Build a deep explicit file path.
    const char* segments[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
    for (int i = 0; i < 10; i++) {
        current = path_append(current, segments[i]);
        ASSERT_NE(current, nullptr);
    }

    EXPECT_EQ(path_depth(current), 11);  // file + 10 segments
    EXPECT_STREQ(current->name, "j");

    StrBuf* buf = strbuf_new();
    path_to_string(current, buf);
    EXPECT_STREQ(buf->str, "file./.a.b.c.d.e.f.g.h.i.j");
    strbuf_free(buf);
}

TEST_F(PathTest, RootParentAndTypedIntegerOperations) {
    Path* logical = path_get_root(PATH_SCHEME_LOGICAL);
    Path* a = path_append(logical, "a");
    Path* b = path_append(a, "b");
    Path* parent = path_select_parent(pool, b);
    Path* root = path_select_root(pool, b);
    Path* relative = path_get_root(PATH_SCHEME_REL);
    Path* relative_parent = path_select_parent(pool, relative);
    Path* integer = path_extend_int(pool, a, 1);
    Path* numeric_name = path_append(a, "1");
    StrBuf* buf = strbuf_new();

    path_to_string(parent, buf);
    EXPECT_STREQ(buf->str, "/.a");
    strbuf_reset(buf);
    path_to_string(root, buf);
    EXPECT_STREQ(buf->str, "/");
    strbuf_reset(buf);
    path_to_string(relative_parent, buf);
    EXPECT_STREQ(buf->str, ".~~");
    strbuf_reset(buf);
    path_to_string(integer, buf);
    EXPECT_STREQ(buf->str, "/.a.1");
    strbuf_reset(buf);
    path_to_string(numeric_name, buf);
    EXPECT_STREQ(buf->str, "/.a.'1'");
    strbuf_free(buf);
}

TEST_F(PathTest, LogicalRootDefaultQualification) {
    Path* logical = path_append(path_get_root(PATH_SCHEME_LOGICAL), "a");
    Path* qualified = path_qualify_default(pool, logical);
    ASSERT_NE(qualified, nullptr);
    EXPECT_EQ(path_get_scheme(logical), PATH_SCHEME_LOGICAL);
    EXPECT_EQ(path_get_scheme(qualified), PATH_SCHEME_FILE);
    StrBuf* buf = strbuf_new();
    path_to_string(logical, buf);
    EXPECT_STREQ(buf->str, "/.a");
    strbuf_reset(buf);
    path_to_string(qualified, buf);
    EXPECT_STREQ(buf->str, "file./.a");
    strbuf_free(buf);
}

TEST_F(PathTest, StructuralEqualityAndHash) {
    Path* left = path_append(path_get_root(PATH_SCHEME_LOGICAL), "a");
    Path* right = path_append(path_get_root(PATH_SCHEME_LOGICAL), "a");
    Path* other = path_append(path_get_root(PATH_SCHEME_LOGICAL), "b");
    EXPECT_TRUE(path_equal(left, right));
    EXPECT_FALSE(path_equal(left, other));
    EXPECT_EQ(path_hash(left, 11, 29), path_hash(right, 11, 29));
    EXPECT_NE(path_hash(left, 11, 29), path_hash(other, 11, 29));
}

TEST_F(PathTest, NamedFileAuthority) {
    char* hostname = shell_get_hostname();
    ASSERT_NE(hostname, nullptr);
    Path* named = path_new_authority(pool, PATH_SCHEME_FILE, hostname);
    ASSERT_NE(named, nullptr);
    Path* child = path_append(named, "tmp");
    StrBuf* buf = strbuf_new();
    path_to_string(child, buf);
    StrBuf* expected = strbuf_new();
    strbuf_append_str(expected, "file.");
    bool quote_authority = hostname[0] == '\0' ||
        !((hostname[0] >= 'A' && hostname[0] <= 'Z') ||
          (hostname[0] >= 'a' && hostname[0] <= 'z') || hostname[0] == '_');
    for (const char* c = hostname + 1; *c && !quote_authority; c++) {
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
              (*c >= '0' && *c <= '9') || *c == '_')) quote_authority = true;
    }
    if (quote_authority) strbuf_append_char(expected, '\'');
    strbuf_append_str(expected, hostname);
    if (quote_authority) strbuf_append_char(expected, '\'');
    strbuf_append_str(expected, ".tmp");
    EXPECT_STREQ(buf->str, expected->str);
    EXPECT_TRUE(path_file_authority_is_local(child));
    strbuf_free(expected);
    strbuf_free(buf);
    mem_free(hostname);
}

TEST_F(PathTest, RemoteFileAuthorityStaysConstructibleButNotLocal) {
    Path* remote = path_new_authority(pool, PATH_SCHEME_FILE, "other-host");
    ASSERT_NE(remote, nullptr);
    EXPECT_FALSE(path_file_authority_is_local(remote));
    StrBuf* buf = strbuf_new();
    path_to_os_path(remote, buf);
    EXPECT_EQ(buf->length, 0u);
    strbuf_free(buf);
}
