#include <gtest/gtest.h>
#include <vector>
#include <random>
#include <algorithm>
#include <set>
#include <chrono>
#include <sstream>

extern "C" {
#include "../lib/avl_tree.h"
#include "../lib/mempool.h"
}

/**
 * Comprehensive AVL Tree Test Suite
 * 
 * This test suite covers all aspects of the AVL tree implementation:
 * - Basic operations (insert, search, remove)
 * - Tree balancing and rotations
 * - Traversal operations
 * - Edge cases and error conditions
 * - Performance characteristics
 * - Memory management
 * - Advanced operations
 */

class AvlTreeTest : public ::testing::Test {
protected:
    Pool* pool;
    AvlTree* tree;
    
    void SetUp() override {
        pool = pool_create(); // Create pool
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
    
    // Helper function to create test declarations
    void* create_test_value(int value) {
        int* val = (int*)pool_calloc(pool, sizeof(int));
        *val = value;
        return val;
    }
    
    // Helper function to get value from declaration
    int get_test_value(void* declaration) {
        return declaration ? *(int*)declaration : -1;
    }
    
    // Helper function to insert a range of values
    void insert_range(int start, int end) {
        if (start <= end) {
            // Ascending range
            for (int i = start; i <= end; i++) {
                AvlNode* node = avl_tree_insert(tree, i, create_test_value(i * 10));
                ASSERT_NE(node, nullptr);
            }
        } else {
            // Descending range
            for (int i = start; i >= end; i--) {
                AvlNode* node = avl_tree_insert(tree, i, create_test_value(i * 10));
                ASSERT_NE(node, nullptr);
            }
        }
    }
    
    // Helper function to verify tree structure
    void verify_tree_structure() {
        EXPECT_TRUE(avl_tree_validate(tree));
        
        AvlTreeStats stats;
        avl_tree_get_stats(tree, &stats);
        EXPECT_EQ(stats.balance_violations, 0);
    }
};

// ============================================================================
// Basic Operations Tests
// ============================================================================

TEST_F(AvlTreeTest, CreateAndDestroy) {
    EXPECT_NE(tree, nullptr);
    EXPECT_TRUE(avl_tree_is_empty(tree));
    EXPECT_EQ(avl_tree_size(tree), 0);
    EXPECT_EQ(avl_tree_height(tree), 0);
}

TEST_F(AvlTreeTest, SingleInsertAndSearch) {
    // Insert a single node
    uintptr_t key = 42;
    void* value = create_test_value(100);
    
    AvlNode* inserted = avl_tree_insert(tree, key, value);
    ASSERT_NE(inserted, nullptr);
    EXPECT_EQ(inserted->property_id, key);
    EXPECT_EQ(inserted->declaration, value);
    
    // Verify tree state
    EXPECT_FALSE(avl_tree_is_empty(tree));
    EXPECT_EQ(avl_tree_size(tree), 1);
    EXPECT_EQ(avl_tree_height(tree), 1);
    
    // Search for the node
    AvlNode* found = avl_tree_search(tree, key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, inserted);
    EXPECT_EQ(get_test_value(found->declaration), 100);
    
    // Search for non-existent key
    AvlNode* not_found = avl_tree_search(tree, 999);
    EXPECT_EQ(not_found, nullptr);
}

TEST_F(AvlTreeTest, MultipleInserts) {
    const int count = 10;
    
    // Insert multiple nodes
    for (int i = 0; i < count; i++) {
        AvlNode* node = avl_tree_insert(tree, i, create_test_value(i * 10));
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(avl_tree_size(tree), i + 1);
    }
    
    // Verify all nodes can be found
    for (int i = 0; i < count; i++) {
        AvlNode* found = avl_tree_search(tree, i);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->property_id, static_cast<uintptr_t>(i));
        EXPECT_EQ(get_test_value(found->declaration), i * 10);
    }
    
    verify_tree_structure();
}

TEST_F(AvlTreeTest, InsertDuplicateKey) {
    uintptr_t key = 42;
    void* value1 = create_test_value(100);
    void* value2 = create_test_value(200);
    
    // Insert first value
    AvlNode* node1 = avl_tree_insert(tree, key, value1);
    ASSERT_NE(node1, nullptr);
    EXPECT_EQ(avl_tree_size(tree), 1);
    
    // Insert second value with same key (should update)
    AvlNode* node2 = avl_tree_insert(tree, key, value2);
    ASSERT_NE(node2, nullptr);
    EXPECT_EQ(node1, node2); // Should be same node
    EXPECT_EQ(avl_tree_size(tree), 1); // Size shouldn't change
    
    // Verify updated value
    EXPECT_EQ(get_test_value(node2->declaration), 200);
}

// ============================================================================
// Removal Tests
// ============================================================================

TEST_F(AvlTreeTest, RemoveFromEmptyTree) {
    void* removed = avl_tree_remove(tree, 42);
    EXPECT_EQ(removed, nullptr);
    EXPECT_TRUE(avl_tree_is_empty(tree));
}

TEST_F(AvlTreeTest, RemoveSingleNode) {
    uintptr_t key = 42;
    void* value = create_test_value(100);
    
    // Insert and then remove
    avl_tree_insert(tree, key, value);
    EXPECT_EQ(avl_tree_size(tree), 1);
    
    void* removed = avl_tree_remove(tree, key);
    EXPECT_EQ(removed, value);
    EXPECT_EQ(avl_tree_size(tree), 0);
    EXPECT_TRUE(avl_tree_is_empty(tree));
    
    // Verify node is gone
    AvlNode* found = avl_tree_search(tree, key);
    EXPECT_EQ(found, nullptr);
}

TEST_F(AvlTreeTest, RemoveLeafNode) {
    insert_range(1, 7);
    
    // Remove a leaf node
    void* removed = avl_tree_remove(tree, 1);
    EXPECT_NE(removed, nullptr);
    EXPECT_EQ(get_test_value(removed), 10);
    EXPECT_EQ(avl_tree_size(tree), 6);
    
    // Verify node is gone
    EXPECT_EQ(avl_tree_search(tree, 1), nullptr);
    
    // Verify remaining nodes
    for (int i = 2; i <= 7; i++) {
        EXPECT_NE(avl_tree_search(tree, i), nullptr);
    }
    
    verify_tree_structure();
}

TEST_F(AvlTreeTest, RemoveNodeWithOneChild) {
    insert_range(1, 7);
    
    // Remove a node with one child
    void* removed = avl_tree_remove(tree, 6);
    EXPECT_NE(removed, nullptr);
    EXPECT_EQ(avl_tree_size(tree), 6);
    
    verify_tree_structure();
}

TEST_F(AvlTreeTest, RemoveNodeWithTwoChildren) {
    insert_range(1, 7);
    
    // Remove root (likely has two children)
    AvlNode* root = tree->root;
    uintptr_t root_key = root->property_id;
    
    void* removed = avl_tree_remove(tree, root_key);
    EXPECT_NE(removed, nullptr);
    EXPECT_EQ(avl_tree_size(tree), 6);
    
    // Verify node is gone
    EXPECT_EQ(avl_tree_search(tree, root_key), nullptr);
    
    verify_tree_structure();
}

TEST_F(AvlTreeTest, RemoveAllNodes) {
    const int count = 10;
    insert_range(1, count);
    
    // Remove all nodes in random order
    std::vector<int> keys;
    for (int i = 1; i <= count; i++) {
        keys.push_back(i);
    }
    
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(keys.begin(), keys.end(), g);
    
    for (int i = 0; i < count; i++) {
        void* removed = avl_tree_remove(tree, keys[i]);
        EXPECT_NE(removed, nullptr);
        EXPECT_EQ(avl_tree_size(tree), count - i - 1);
        verify_tree_structure();
    }
    
    EXPECT_TRUE(avl_tree_is_empty(tree));
}

// ============================================================================
// Tree Balancing Tests
// ============================================================================

TEST_F(AvlTreeTest, LeftRotation) {
    // Insert in ascending order to trigger left rotations
    for (int i = 1; i <= 7; i++) {
        avl_tree_insert(tree, i, create_test_value(i * 10));
        verify_tree_structure(); // Should stay balanced
    }
    
    // Tree should be balanced
    AvlTreeStats stats;
    avl_tree_get_stats(tree, &stats);
    EXPECT_LE(stats.height, 4); // Should be well-balanced
}

TEST_F(AvlTreeTest, RightRotation) {
    // Insert in descending order to trigger right rotations
    for (int i = 7; i >= 1; i--) {
        avl_tree_insert(tree, i, create_test_value(i * 10));
        verify_tree_structure(); // Should stay balanced
    }
    
    // Tree should be balanced
    AvlTreeStats stats;
    avl_tree_get_stats(tree, &stats);
    EXPECT_LE(stats.height, 4); // Should be well-balanced
}

TEST_F(AvlTreeTest, LeftRightRotation) {
    // Insert pattern that triggers left-right rotation
    avl_tree_insert(tree, 10, create_test_value(100));
    avl_tree_insert(tree, 5, create_test_value(50));
    avl_tree_insert(tree, 7, create_test_value(70));
    
    verify_tree_structure();
}

TEST_F(AvlTreeTest, RightLeftRotation) {
    // Insert pattern that triggers right-left rotation
    avl_tree_insert(tree, 5, create_test_value(50));
    avl_tree_insert(tree, 10, create_test_value(100));
    avl_tree_insert(tree, 8, create_test_value(80));
    
    verify_tree_structure();
}

TEST_F(AvlTreeTest, RandomInsertionBalancing) {
    const int count = 100;
    std::vector<int> keys;
    for (int i = 1; i <= count; i++) {
        keys.push_back(i);
    }
    
    // Shuffle keys for random insertion
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(keys.begin(), keys.end(), g);
    
    // Insert all keys
    for (int key : keys) {
        avl_tree_insert(tree, key, create_test_value(key * 10));
        verify_tree_structure();
    }
    
    // Check final balance
    AvlTreeStats stats;
    avl_tree_get_stats(tree, &stats);
    EXPECT_LE(stats.height, 8); // log2(100) ≈ 6.6, so 8 is reasonable
    EXPECT_EQ(stats.balance_violations, 0);
}

// ============================================================================
// Traversal Tests
// ============================================================================

TEST_F(AvlTreeTest, InOrderTraversal) {
    insert_range(5, 1); // Insert: 5, 4, 3, 2, 1
    insert_range(6, 10); // Insert: 6, 7, 8, 9, 10
    
    std::vector<uintptr_t> visited;
    
    int count = avl_tree_foreach_inorder(tree, [](AvlNode* node, void* context) -> bool {
        std::vector<uintptr_t>* v = static_cast<std::vector<uintptr_t>*>(context);
        v->push_back(node->property_id);
        return true;
    }, &visited);
    
    EXPECT_EQ(count, 10);
    EXPECT_EQ(visited.size(), 10);
    
    // Should be in ascending order
    for (size_t i = 0; i < visited.size(); i++) {
        EXPECT_EQ(visited[i], i + 1);
    }
}

TEST_F(AvlTreeTest, PreOrderTraversal) {
    insert_range(1, 7);
    
    std::vector<uintptr_t> visited;
    
    int count = avl_tree_foreach_preorder(tree, [](AvlNode* node, void* context) -> bool {
        std::vector<uintptr_t>* v = static_cast<std::vector<uintptr_t>*>(context);
        v->push_back(node->property_id);
        return true;
    }, &visited);
    
    EXPECT_EQ(count, 7);
    EXPECT_EQ(visited.size(), 7);
    
    // First element should be root
    EXPECT_EQ(visited[0], tree->root->property_id);
}

TEST_F(AvlTreeTest, PostOrderTraversal) {
    insert_range(1, 7);
    
    std::vector<uintptr_t> visited;
    
    int count = avl_tree_foreach_postorder(tree, [](AvlNode* node, void* context) -> bool {
        std::vector<uintptr_t>* v = static_cast<std::vector<uintptr_t>*>(context);
        v->push_back(node->property_id);
        return true;
    }, &visited);
    
    EXPECT_EQ(count, 7);
    EXPECT_EQ(visited.size(), 7);
    
    // Last element should be root
    EXPECT_EQ(visited.back(), tree->root->property_id);
}

TEST_F(AvlTreeTest, TraversalEarlyExit) {
    insert_range(1, 10);
    
    std::vector<uintptr_t> visited;
    
    int count = avl_tree_foreach_inorder(tree, [](AvlNode* node, void* context) -> bool {
        std::vector<uintptr_t>* v = static_cast<std::vector<uintptr_t>*>(context);
        v->push_back(node->property_id);
        return v->size() < 5; // Stop after 5 elements
    }, &visited);
    
    EXPECT_EQ(count, 5);
    EXPECT_EQ(visited.size(), 5);
}

// ============================================================================
// Min/Max and Predecessor/Successor Tests
// ============================================================================

TEST_F(AvlTreeTest, MinMaxOperations) {
    EXPECT_EQ(avl_tree_min(tree), nullptr);
    EXPECT_EQ(avl_tree_max(tree), nullptr);
    
    insert_range(5, 15);
    
    AvlNode* min_node = avl_tree_min(tree);
    AvlNode* max_node = avl_tree_max(tree);
    
    ASSERT_NE(min_node, nullptr);
    ASSERT_NE(max_node, nullptr);
    
    EXPECT_EQ(min_node->property_id, 5);
    EXPECT_EQ(max_node->property_id, 15);
}

TEST_F(AvlTreeTest, PredecessorSuccessor) {
    insert_range(1, 10);
    
    // Test predecessor
    AvlNode* node5 = avl_tree_search(tree, 5);
    ASSERT_NE(node5, nullptr);
    
    AvlNode* pred = avl_tree_predecessor(node5);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->property_id, 4);
    
    // Test successor
    AvlNode* succ = avl_tree_successor(node5);
    ASSERT_NE(succ, nullptr);
    EXPECT_EQ(succ->property_id, 6);
    
    // Test edge cases
    AvlNode* min_node = avl_tree_min(tree);
    EXPECT_EQ(avl_tree_predecessor(min_node), nullptr);
    
    AvlNode* max_node = avl_tree_max(tree);
    EXPECT_EQ(avl_tree_successor(max_node), nullptr);
}

// ============================================================================
// Advanced Operations Tests
// ============================================================================

TEST_F(AvlTreeTest, BulkInsert) {
    const int count = 50;
    std::vector<uintptr_t> keys;
    std::vector<void*> values;
    
    for (int i = 1; i <= count; i++) {
        keys.push_back(i);
        values.push_back(create_test_value(i * 10));
    }
    
    int inserted = avl_tree_bulk_insert(tree, keys.data(), values.data(), count);
    EXPECT_EQ(inserted, count);
    EXPECT_EQ(avl_tree_size(tree), count);
    
    // Verify all insertions
    for (int i = 1; i <= count; i++) {
        AvlNode* node = avl_tree_search(tree, i);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(get_test_value(node->declaration), i * 10);
    }
    
    verify_tree_structure();
}

TEST_F(AvlTreeTest, TreeCloning) {
    insert_range(1, 10);
    
    Pool* new_pool = pool_create();
    ASSERT_NE(new_pool, nullptr);
    
    // Clone without value cloning
    AvlTree* cloned = avl_tree_clone(tree, new_pool, nullptr);
    ASSERT_NE(cloned, nullptr);
    
    EXPECT_EQ(avl_tree_size(cloned), avl_tree_size(tree));
    
    // Verify all nodes are present
    for (int i = 1; i <= 10; i++) {
        AvlNode* original = avl_tree_search(tree, i);
        AvlNode* cloned_node = avl_tree_search(cloned, i);
        
        ASSERT_NE(original, nullptr);
        ASSERT_NE(cloned_node, nullptr);
        EXPECT_EQ(original->property_id, cloned_node->property_id);
        EXPECT_EQ(original->declaration, cloned_node->declaration); // Same pointer
    }
    
    avl_tree_destroy(cloned);
    pool_destroy(new_pool);
}

TEST_F(AvlTreeTest, TreeMerging) {
    // Create two trees
    AvlTree* tree2 = avl_tree_create(pool);
    ASSERT_NE(tree2, nullptr);
    
    // Fill first tree with odd numbers
    for (int i = 1; i <= 10; i += 2) {
        avl_tree_insert(tree, i, create_test_value(i * 10));
    }
    
    // Fill second tree with even numbers
    for (int i = 2; i <= 10; i += 2) {
        avl_tree_insert(tree2, i, create_test_value(i * 10));
    }
    
    // Merge trees
    int merged = avl_tree_merge(tree, tree2, nullptr);
    EXPECT_EQ(merged, 5); // 5 even numbers
    EXPECT_EQ(avl_tree_size(tree), 10);
    
    // Verify all numbers are present
    for (int i = 1; i <= 10; i++) {
        AvlNode* node = avl_tree_search(tree, i);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(get_test_value(node->declaration), i * 10);
    }
    
    verify_tree_structure();
    avl_tree_destroy(tree2);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(AvlTreeTest, PerformanceInsert) {
    const int count = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 1; i <= count; i++) {
        avl_tree_insert(tree, i, create_test_value(i));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    EXPECT_EQ(avl_tree_size(tree), count);
    
    // Should complete within reasonable time (adjust as needed)
    EXPECT_LT(duration.count(), 100000); // 100ms
    
    printf("Inserted %d nodes in %ld microseconds\n", count, duration.count());
}

TEST_F(AvlTreeTest, PerformanceSearch) {
    const int count = 10000;
    insert_range(1, count);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 1; i <= count; i++) {
        AvlNode* node = avl_tree_search(tree, i);
        ASSERT_NE(node, nullptr);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should complete within reasonable time
    EXPECT_LT(duration.count(), 50000); // 50ms
    
    printf("Searched %d nodes in %ld microseconds\n", count, duration.count());
}

TEST_F(AvlTreeTest, PerformanceRandomOperations) {
    const int count = 1000;
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
        
        // Periodically verify structure
        if (i % 100 == 0) {
            verify_tree_structure();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    printf("Performed %d random operations in %ld microseconds\n", count, duration.count());
    verify_tree_structure();
}

// ============================================================================
// Edge Cases and Error Handling Tests
// ============================================================================

TEST_F(AvlTreeTest, NullParameterHandling) {
    // Test null tree
    EXPECT_EQ(avl_tree_insert(nullptr, 1, create_test_value(10)), nullptr);
    EXPECT_EQ(avl_tree_search(nullptr, 1), nullptr);
    EXPECT_EQ(avl_tree_remove(nullptr, 1), nullptr);
    EXPECT_TRUE(avl_tree_is_empty(nullptr));
    EXPECT_EQ(avl_tree_size(nullptr), 0);
    EXPECT_EQ(avl_tree_height(nullptr), 0);
    
    // Test null callbacks
    EXPECT_EQ(avl_tree_foreach_inorder(tree, nullptr, nullptr), 0);
    
    // Test null pool
    EXPECT_EQ(avl_tree_create(nullptr), nullptr);
}

TEST_F(AvlTreeTest, LargeKeyValues) {
    // Test with maximum uintptr_t values
    uintptr_t large_key = UINTPTR_MAX;
    uintptr_t large_key2 = UINTPTR_MAX - 1;
    
    AvlNode* node1 = avl_tree_insert(tree, large_key, create_test_value(100));
    AvlNode* node2 = avl_tree_insert(tree, large_key2, create_test_value(200));
    
    ASSERT_NE(node1, nullptr);
    ASSERT_NE(node2, nullptr);
    
    EXPECT_EQ(avl_tree_search(tree, large_key), node1);
    EXPECT_EQ(avl_tree_search(tree, large_key2), node2);
    
    verify_tree_structure();
}

TEST_F(AvlTreeTest, SingleNodeOperations) {
    uintptr_t key = 42;
    avl_tree_insert(tree, key, create_test_value(100));
    
    // Test min/max on single node
    AvlNode* min_node = avl_tree_min(tree);
    AvlNode* max_node = avl_tree_max(tree);
    
    EXPECT_EQ(min_node->property_id, key);
    EXPECT_EQ(max_node->property_id, key);
    EXPECT_EQ(min_node, max_node);
    
    // Test predecessor/successor on single node
    EXPECT_EQ(avl_tree_predecessor(min_node), nullptr);
    EXPECT_EQ(avl_tree_successor(max_node), nullptr);
}

// ============================================================================
// Memory and Statistics Tests
// ============================================================================

TEST_F(AvlTreeTest, TreeStatistics) {
    // Empty tree stats
    AvlTreeStats stats;
    avl_tree_get_stats(tree, &stats);
    
    EXPECT_EQ(stats.node_count, 0);
    EXPECT_EQ(stats.height, 0);
    EXPECT_EQ(stats.balance_violations, 0);
    
    // Balanced tree stats
    insert_range(1, 15);
    avl_tree_get_stats(tree, &stats);
    
    EXPECT_EQ(stats.node_count, 15);
    EXPECT_GT(stats.height, 0);
    EXPECT_EQ(stats.balance_violations, 0);
    EXPECT_GT(stats.average_depth, 0);
    EXPECT_LE(stats.min_depth, stats.max_depth);
    
    printf("Tree stats: nodes=%d, height=%d, avg_depth=%.2f, min_depth=%d, max_depth=%d\n",
           stats.node_count, stats.height, stats.average_depth, stats.min_depth, stats.max_depth);
}

TEST_F(AvlTreeTest, TreeValidation) {
    // Valid tree
    insert_range(1, 10);
    EXPECT_TRUE(avl_tree_validate(tree));
    
    // Empty tree is valid
    avl_tree_clear(tree);
    EXPECT_TRUE(avl_tree_validate(tree));
    
    // Single node is valid
    avl_tree_insert(tree, 42, create_test_value(100));
    EXPECT_TRUE(avl_tree_validate(tree));
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}