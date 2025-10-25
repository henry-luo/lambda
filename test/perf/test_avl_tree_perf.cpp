#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <set>
#include "../../lib/avl_tree.h"
#include "../../lib/mempool.h"

// Test fixture for AVL tree performance tests
class AvlTreePerfTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        tree = avl_tree_create(pool);
        ASSERT_NE(tree, nullptr);
    }
    
    void TearDown() override {
        if (tree) {
            avl_tree_destroy(tree);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }
    
    // Helper to create test values
    void* create_test_value(int value) {
        int* ptr = (int*)pool_alloc(pool, sizeof(int));
        *ptr = value;
        return ptr;
    }
    
    int get_test_value(void* ptr) {
        return ptr ? *(int*)ptr : 0;
    }
    
    // Simple tree structure validation (lightweight for performance tests)
    void verify_basic_structure() {
        EXPECT_TRUE(avl_tree_validate(tree));
    }
    
    Pool* pool = nullptr;
    AvlTree* tree = nullptr;
};

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(AvlTreePerfTest, RandomOperations_Small) {
    const int count = 500;  // Small test for quick validation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> key_dist(1, count * 2);
    std::uniform_int_distribution<> op_dist(0, 2); // 0=insert, 1=search, 2=remove
    
    std::set<int> inserted_keys;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < count; i++) {
        int op = op_dist(gen);
        int key = key_dist(gen);
        
        switch (op) {
            case 0: // Insert
                avl_tree_insert(tree, key, create_test_value(key));
                inserted_keys.insert(key);
                break;
                
            case 1: // Search
                avl_tree_search(tree, key);
                break;
                
            case 2: // Remove
                if (!inserted_keys.empty()) {
                    auto it = inserted_keys.begin();
                    std::advance(it, gen() % inserted_keys.size());
                    avl_tree_remove(tree, *it);
                    inserted_keys.erase(it);
                }
                break;
        }
        
        // Only verify at the end for performance
        if (i == count - 1) {
            verify_basic_structure();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    printf("Performed %d random operations in %lld microseconds\n", count, duration.count());
    verify_basic_structure();
}

TEST_F(AvlTreePerfTest, RandomOperations_Medium) {
    const int count = 2000;  // Medium test
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> key_dist(1, count * 2);
    std::uniform_int_distribution<> op_dist(0, 2);
    
    std::set<int> inserted_keys;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < count; i++) {
        int op = op_dist(gen);
        int key = key_dist(gen);
        
        switch (op) {
            case 0: // Insert
                avl_tree_insert(tree, key, create_test_value(key));
                inserted_keys.insert(key);
                break;
                
            case 1: // Search
                avl_tree_search(tree, key);
                break;
                
            case 2: // Remove
                if (!inserted_keys.empty()) {
                    auto it = inserted_keys.begin();
                    std::advance(it, gen() % inserted_keys.size());
                    avl_tree_remove(tree, *it);
                    inserted_keys.erase(it);
                }
                break;
        }
        
        // Verify occasionally
        if (i % 500 == 0 && i > 0) {
            verify_basic_structure();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    printf("Performed %d random operations in %lld microseconds\n", count, duration.count());
    verify_basic_structure();
}

TEST_F(AvlTreePerfTest, RandomOperations_Large) {
    const int count = 10000;  // Large test for comprehensive performance testing
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> key_dist(1, count * 2);
    std::uniform_int_distribution<> op_dist(0, 2);
    
    std::set<int> inserted_keys;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < count; i++) {
        int op = op_dist(gen);
        int key = key_dist(gen);
        
        switch (op) {
            case 0: // Insert
                avl_tree_insert(tree, key, create_test_value(key));
                inserted_keys.insert(key);
                break;
                
            case 1: // Search
                avl_tree_search(tree, key);
                break;
                
            case 2: // Remove
                if (!inserted_keys.empty()) {
                    auto it = inserted_keys.begin();
                    std::advance(it, gen() % inserted_keys.size());
                    avl_tree_remove(tree, *it);
                    inserted_keys.erase(it);
                }
                break;
        }
        
        // Verify less frequently for large tests
        if (i % 2000 == 0 && i > 0) {
            verify_basic_structure();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    printf("Performed %d random operations in %lld microseconds\n", count, duration.count());
    verify_basic_structure();
}

TEST_F(AvlTreePerfTest, BulkInsert_Performance) {
    const int count = 50000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < count; i++) {
        avl_tree_insert(tree, i, create_test_value(i));
        
        // Only verify occasionally
        if (i % 10000 == 0 && i > 0) {
            verify_basic_structure();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    printf("Bulk inserted %d nodes in %lld microseconds\n", count, duration.count());
    EXPECT_EQ(avl_tree_size(tree), count);
    verify_basic_structure();
}

TEST_F(AvlTreePerfTest, BulkSearch_Performance) {
    const int count = 10000;
    
    // First, insert nodes
    for (int i = 0; i < count; i++) {
        avl_tree_insert(tree, i, create_test_value(i));
    }
    
    // Now time the searches
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < count; i++) {
        AvlNode* found = avl_tree_search(tree, i);
        EXPECT_NE(found, nullptr);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    printf("Bulk searched %d nodes in %lld microseconds\n", count, duration.count());
}