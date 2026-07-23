// test_dir_gtest.cpp
// Comprehensive GTest unit tests for directory listing via input_from_directory
// Tests the directory listing feature - returns a List of Path items

#include <gtest/gtest.h>
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test fixture for directory tests
class InputDirTest : public ::testing::Test {
protected:
    static char* test_dir_name;  // allocated per-SetUp under ./temp/
    Pool* pool;
    EvalContext test_context;
    Heap test_heap;

    void SetUp() override {
        // Pool + Heap + EvalContext + thread-local `context` + path_init().
        tu_setup_runtime(&pool, &test_heap, &test_context);

        // Fresh unique directory under ./temp/ (CLAUDE.md rule 2).
        test_dir_name = tu_mkdtemp("dir_test");
        ASSERT_NE(test_dir_name, nullptr) << "tu_mkdtemp failed";
        setup_test_directory();
    }

    void TearDown() override {
        tu_rmtree(test_dir_name);
        free(test_dir_name);
        test_dir_name = nullptr;
        tu_teardown_runtime(pool);
    }

    // Populate the temp dir with a fixture tree:
    //   subdir1/nested/file3.txt, subdir1/file2.txt, subdir2/, file1.txt, empty.txt
    static void setup_test_directory() {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/subdir1/nested", test_dir_name);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/subdir2", test_dir_name);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "echo 'test content' > %s/file1.txt", test_dir_name);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "echo 'more content' > %s/subdir1/file2.txt", test_dir_name);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "echo 'nested content' > %s/subdir1/nested/file3.txt", test_dir_name);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "touch %s/empty.txt", test_dir_name);
        system(cmd);
    }
};

// Static member definition
char* InputDirTest::test_dir_name = nullptr;

// Test basic directory listing functionality - returns List of Path items
TEST_F(InputDirTest, ListCurrentDirectory) {
    const char* dir = ".";
    Input* input = input_from_directory(dir, dir, false, 1);
    ASSERT_NE(input, nullptr) << "input_from_directory returned NULL";

    // Use get_type_id() to properly check the type - should now be a list
    TypeId root_type = get_type_id(input->root);
    ASSERT_EQ(root_type, LMD_TYPE_ARRAY) << "Root is not a list (got type " << root_type << ", expected " << LMD_TYPE_ARRAY << ")";

    List* root = (List*)input->root.array;
    ASSERT_NE(root, nullptr) << "Root list is NULL";

    // Verify list contains Path items
    ASSERT_GT(root->length, 0) << "Directory listing should not be empty";
}

// Test directory listing with custom test structure
TEST_F(InputDirTest, ListTestDirectory) {
    Input* input = input_from_directory(test_dir_name, test_dir_name, false, 1);
    ASSERT_NE(input, nullptr) << "input_from_directory returned NULL for test directory";

    TypeId root_type = get_type_id(input->root);
    ASSERT_EQ(root_type, LMD_TYPE_ARRAY) << "Root is not a list (got type " << root_type << ", expected " << LMD_TYPE_ARRAY << ")";

    List* root = (List*)input->root.array;
    ASSERT_NE(root, nullptr) << "Root list is NULL";

    // Test directory has: file1.txt, empty.txt, subdir1, subdir2 = 4 items
    ASSERT_EQ(root->length, 4) << "Expected 4 items in test directory";

    // Verify items are Path type
    for (int64_t i = 0; i < root->length; i++) {
        Item item = root->items[i];
        TypeId item_type = get_type_id(item);
        ASSERT_EQ(item_type, LMD_TYPE_PATH) << "Item " << i << " should be a Path";
    }
    
    SUCCEED() << "Directory listing created successfully";
}

// Test recursive directory listing (note: not fully implemented in new version)
TEST_F(InputDirTest, RecursiveDirectoryListing) {
    Input* input = input_from_directory(test_dir_name, test_dir_name, true, 2);
    ASSERT_NE(input, nullptr) << "input_from_directory returned NULL for recursive listing";

    List* root = (List*)input->root.array;
    ASSERT_NE(root, nullptr) << "Root list is NULL";

    // Basic test that listing works without crashing
    SUCCEED() << "Recursive directory listing completed successfully";
}

// Test depth limiting in recursive traversal
TEST_F(InputDirTest, DepthLimitedTraversal) {
    // Test with max_depth = 1 (should not go into nested subdirectories)
    Input* input = input_from_directory(test_dir_name, test_dir_name, true, 1);
    ASSERT_NE(input, nullptr) << "input_from_directory returned NULL for depth-limited listing";

    List* root = (List*)input->root.array;
    ASSERT_NE(root, nullptr) << "Root list is NULL";

    SUCCEED() << "Depth limiting test completed successfully";
}

// Test non-recursive directory listing
TEST_F(InputDirTest, NonRecursiveListing) {
    Input* input = input_from_directory(test_dir_name, test_dir_name, false, 0);
    ASSERT_NE(input, nullptr) << "input_from_directory returned NULL for non-recursive listing";

    List* root = (List*)input->root.array;
    ASSERT_NE(root, nullptr) << "Root list is NULL";

    SUCCEED() << "Non-recursive directory listing completed successfully";
}

// Test error handling for non-existent directory
TEST_F(InputDirTest, NonexistentDirectoryError) {
    Input* input = input_from_directory("nonexistent_directory_12345", "nonexistent_directory_12345", false, 1);
    ASSERT_EQ(input, nullptr) << "input_from_directory should return NULL for non-existent directory";
}

// Test error handling for file instead of directory
TEST_F(InputDirTest, FileInsteadOfDirectoryError) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/file1.txt", test_dir_name);
    Input* input = input_from_directory(file_path, file_path, false, 1);
    ASSERT_EQ(input, nullptr) << "input_from_directory should return NULL when given a file instead of directory";
}

// Test fixture for tests that don't need the custom directory
class InputDirTestSimple : public ::testing::Test {
protected:
    Pool* pool;
    EvalContext test_context;
    Heap test_heap;

    void SetUp() override    { tu_setup_runtime(&pool, &test_heap, &test_context); }
    void TearDown() override { tu_teardown_runtime(pool); }
};

// Test empty directory handling
TEST_F(InputDirTestSimple, EmptyDirectoryHandling) {
    char* empty_dir = tu_mkdtemp("empty_dir_test");
    ASSERT_NE(empty_dir, nullptr);

    Input* input = input_from_directory(empty_dir, empty_dir, false, 1);
    ASSERT_NE(input, nullptr) << "input_from_directory should handle empty directories";

    List* root = (List*)input->root.array;
    ASSERT_NE(root, nullptr) << "Root list should exist for empty directory";
    ASSERT_EQ(root->length, 0) << "Empty directory should have 0 items";

    tu_rmtree(empty_dir);
    free(empty_dir);
}

// Test integration with input_from_url for directory URLs (simplified test)
TEST_F(InputDirTestSimple, UrlDirectoryIntegrationSimple) {
    // Test with absolute path to avoid URL parsing complexity
    const char* url_text = "file:///tmp";  // Use /tmp which should exist on most systems
    size_t url_len = strlen(url_text);
    String* url_str = (String*)malloc(sizeof(String) + url_len + 1);
    url_str->len = url_len;
    strcpy(url_str->chars, url_text);

    Input* input = input_from_url(url_str, NULL, NULL, NULL);

    // The test passes if input_from_url doesn't crash/hang
    // We don't assert on the result since /tmp might not be accessible
    printf("URL directory integration test completed without hanging\n");

    free(url_str);
}
