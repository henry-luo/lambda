#include <iostream>
#include <string>
#include <cassert>

// Include the C header first and then undefine the conflicting typedef
extern "C" {
#include "../lib/hashmap.h"
}
#undef HashMap

// Now include our C++ wrapper
#include "../lib/hashmap.hpp"

// Don't use 'using' declaration - use full qualification instead

void test_basic_operations() {
    std::cout << "Testing basic operations...\n";
    
    // Create a HashMap with string keys and int values
    hashmap_cpp::HashMap<std::string, int> map;
    
    // Test insertion
    map["apple"] = 5;
    map["banana"] = 3;
    map["cherry"] = 8;
    
    // Test size
    assert(map.size() == 3);
    assert(!map.empty());
    
    // Test access
    assert(map["apple"] == 5);
    assert(map["banana"] == 3);
    assert(map["cherry"] == 8);
    
    // Test at() method
    try {
        assert(map.at("apple") == 5);
        std::cout << "✓ at() method works correctly\n";
    } catch (const std::exception& e) {
        std::cout << "✗ at() method failed: " << e.what() << "\n";
    }
    
    // Test out of range
    try {
        map.at("nonexistent");
        std::cout << "✗ at() should have thrown exception\n";
    } catch (const std::out_of_range& e) {
        std::cout << "✓ at() correctly throws out_of_range\n";
    }
    
    std::cout << "Basic operations test completed.\n\n";
}

void test_integer_keys() {
    std::cout << "Testing integer keys...\n";
    
    hashmap_cpp::HashMap<int, std::string> map;
    
    map[1] = "one";
    map[2] = "two";
    map[3] = "three";
    
    assert(map.size() == 3);
    assert(map[1] == "one");
    assert(map[2] == "two");
    assert(map[3] == "three");
    
    std::cout << "Integer keys test completed.\n\n";
}

void test_contains_and_find() {
    std::cout << "Testing contains and find...\n";
    
    hashmap_cpp::HashMap<std::string, int> map;
    map["key1"] = 100;
    map["key2"] = 200;
    
    // Test contains
    assert(map.contains("key1"));
    assert(map.contains("key2"));
    assert(!map.contains("key3"));
    
    // Test count
    assert(map.count("key1") == 1);
    assert(map.count("key3") == 0);
    
    std::cout << "Contains and find test completed.\n\n";
}

void test_erase() {
    std::cout << "Testing erase operations...\n";
    
    hashmap_cpp::HashMap<std::string, int> map;
    map["a"] = 1;
    map["b"] = 2;
    map["c"] = 3;
    
    assert(map.size() == 3);
    
    // Erase existing key
    size_t erased = map.erase("b");
    assert(erased == 1);
    assert(map.size() == 2);
    assert(!map.contains("b"));
    
    // Erase non-existing key
    erased = map.erase("nonexistent");
    assert(erased == 0);
    assert(map.size() == 2);
    
    std::cout << "Erase operations test completed.\n\n";
}

void test_clear() {
    std::cout << "Testing clear operation...\n";
    
    hashmap_cpp::HashMap<int, std::string> map;
    map[1] = "one";
    map[2] = "two";
    map[3] = "three";
    
    assert(map.size() == 3);
    assert(!map.empty());
    
    map.clear();
    
    assert(map.size() == 0);
    assert(map.empty());
    
    std::cout << "Clear operation test completed.\n\n";
}

void test_copy_and_move() {
    std::cout << "Testing copy and move operations...\n";
    
    hashmap_cpp::HashMap<std::string, int> original;
    original["x"] = 10;
    original["y"] = 20;
    
    // Test copy constructor
    hashmap_cpp::HashMap<std::string, int> copied(original);
    assert(copied.size() == 2);
    assert(copied["x"] == 10);
    assert(copied["y"] == 20);
    
    // Test copy assignment
    hashmap_cpp::HashMap<std::string, int> assigned;
    assigned = original;
    assert(assigned.size() == 2);
    assert(assigned["x"] == 10);
    assert(assigned["y"] == 20);
    
    // Test move constructor
    hashmap_cpp::HashMap<std::string, int> moved(std::move(original));
    assert(moved.size() == 2);
    assert(moved["x"] == 10);
    assert(moved["y"] == 20);
    
    std::cout << "Copy and move operations test completed.\n\n";
}

void test_insert_or_assign() {
    std::cout << "Testing insert_or_assign...\n";
    
    hashmap_cpp::HashMap<std::string, int> map;
    
    // Insert new key
    auto result = map.insert_or_assign("new_key", 42);
    assert(result.second == true);  // true means inserted
    assert(map["new_key"] == 42);
    
    // Assign to existing key
    result = map.insert_or_assign("new_key", 100);
    assert(result.second == false); // false means assigned (key existed)
    assert(map["new_key"] == 100);
    
    std::cout << "insert_or_assign test completed.\n\n";
}

void demonstrate_usage() {
    std::cout << "=== HashMap C++ Wrapper Demo ===\n\n";
    
    // Create a HashMap for storing user information
    hashmap_cpp::HashMap<std::string, std::string> users;
    
    // Add some users
    users["john_doe"] = "John Doe";
    users["jane_smith"] = "Jane Smith";
    users["bob_wilson"] = "Bob Wilson";
    
    std::cout << "Users in the system:\n";
    std::cout << "Total users: " << users.size() << "\n";
    
    // Access users
    std::cout << "User john_doe: " << users["john_doe"] << "\n";
    std::cout << "User jane_smith: " << users["jane_smith"] << "\n";
    
    // Check if user exists
    if (users.contains("alice_brown")) {
        std::cout << "alice_brown exists\n";
    } else {
        std::cout << "alice_brown does not exist\n";
    }
    
    // Add new user
    users["alice_brown"] = "Alice Brown";
    std::cout << "Added alice_brown, total users now: " << users.size() << "\n";
    
    // Remove a user
    users.erase("bob_wilson");
    std::cout << "Removed bob_wilson, total users now: " << users.size() << "\n";
    
    std::cout << "\n=== Demo completed ===\n\n";
}

int main() {
    try {
        demonstrate_usage();
        
        test_basic_operations();
        test_integer_keys();
        test_contains_and_find();
        test_erase();
        test_clear();
        test_copy_and_move();
        test_insert_or_assign();
        
        std::cout << "🎉 All tests passed! The C++ HashMap wrapper is working correctly.\n";
        
    } catch (const std::exception& e) {
        std::cout << "❌ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
