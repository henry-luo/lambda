// npm package.json parsing: the parsed JSON must outlive the parse.
//
// `exports_item`, `imports_item` and `raw_item` point into the parsed JSON, and
// resolution (npm_resolve_exports) reads them after parsing returns. The parse
// used to destroy its pool on return, so those reads found freed memory; the
// package now owns the pool until npm_package_json_free (D4.2.6;
// vibe/Memory_Context.md §16.4).

#include <gtest/gtest.h>
#include <cstring>
#include "../lambda/module/npm/npm_package_json.h"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/mem_context.h"

static uint32_t live_allocator_count() {
    MemSnapshot* snap = mem_snapshot_capture(NULL);
    uint32_t count = snap ? snap->count : 0;
    mem_snapshot_free(snap);
    return count;
}

// Allocates and fills blocks of the sizes a freed parse leaves behind, so a
// dangling read would see these bytes instead of the parsed JSON.
struct MemoryChurn {
    void* blocks[64];
    MemoryChurn() {
        for (int i = 0; i < 64; i++) {
            size_t size = (i % 2) ? 4096 : 256;
            blocks[i] = mem_alloc(size, MEM_CAT_TEMP);
            if (blocks[i]) memset(blocks[i], 0x5A, size);
        }
    }
    ~MemoryChurn() {
        for (int i = 0; i < 64; i++) mem_free(blocks[i]);
    }
};

class NpmPackageJsonTest : public ::testing::Test {
protected:
    void SetUp() override { log_init(NULL); }
};

TEST_F(NpmPackageJsonTest, StringExportsResolveAfterParse) {
    NpmPackageJson* pkg = npm_package_json_parse_string(
        "{\"name\": \"pkg\", \"exports\": \"./main.js\"}");
    ASSERT_NE(pkg, nullptr);
    ASSERT_TRUE(pkg->has_exports);
    EXPECT_NE(pkg->parse_pool, nullptr);  // the package owns the parsed JSON
    MemoryChurn churn;
    const char* conditions[] = {"import", "default"};
    const char* resolved = npm_resolve_exports(pkg, ".", conditions, 2);
    ASSERT_NE(resolved, nullptr);
    EXPECT_STREQ(resolved, "./main.js");
    npm_package_json_free(pkg);
}

TEST_F(NpmPackageJsonTest, MapExportsResolveAfterParse) {
    NpmPackageJson* pkg = npm_package_json_parse_string(
        "{\"name\": \"pkg\", \"exports\": {"
        "\".\": {\"import\": \"./esm.js\", \"require\": \"./cjs.js\"},"
        "\"./sub\": \"./sub.js\"}}");
    ASSERT_NE(pkg, nullptr);
    ASSERT_TRUE(pkg->has_exports);
    MemoryChurn churn;
    const char* import_conditions[] = {"import", "default"};
    const char* require_conditions[] = {"require", "default"};
    const char* root = npm_resolve_exports(pkg, ".", import_conditions, 2);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(root, "./esm.js");
    const char* cjs = npm_resolve_exports(pkg, ".", require_conditions, 2);
    ASSERT_NE(cjs, nullptr);
    EXPECT_STREQ(cjs, "./cjs.js");
    const char* sub = npm_resolve_exports(pkg, "./sub", import_conditions, 2);
    ASSERT_NE(sub, nullptr);
    EXPECT_STREQ(sub, "./sub.js");
    npm_package_json_free(pkg);
}

TEST_F(NpmPackageJsonTest, FreeReleasesTheParsedJson) {
    // one round first, so lazily created process-wide owners are not counted
    npm_package_json_free(npm_package_json_parse_string("{\"name\": \"warm\"}"));
    uint32_t before = live_allocator_count();
    for (int i = 0; i < 16; i++) {
        NpmPackageJson* pkg = npm_package_json_parse_string(
            "{\"name\": \"pkg\", \"exports\": {\".\": \"./index.js\"}}");
        ASSERT_NE(pkg, nullptr);
        npm_package_json_free(pkg);
    }
    EXPECT_EQ(live_allocator_count(), before);
}
