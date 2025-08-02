#include <iostream>
#include <string>
#include <vector>
#include <chrono>

// Include the C header first and then undefine the conflicting typedef
extern "C" {
#include "../lib/hashmap.h"
}
#undef HashMap

// Now include our C++ wrapper
#include "../lib/hashmap.hpp"

struct Person {
    std::string name;
    int age;
    std::string email;
    
    Person() = default;
    Person(const std::string& n, int a, const std::string& e) 
        : name(n), age(a), email(e) {}
};

// Custom hash function for strings
struct StringHash {
    std::size_t operator()(const std::string& s) const {
        return std::hash<std::string>{}(s);
    }
};

int main() {
    std::cout << "=== Advanced C++ HashMap Usage Examples ===\n\n";
    
    // Example 1: Basic string-to-int mapping
    std::cout << "1. Basic string-to-int mapping:\n";
    hashmap_cpp::HashMap<std::string, int> scores;
    scores["Alice"] = 95;
    scores["Bob"] = 87;
    scores["Charlie"] = 92;
    
    std::cout << "Scores: ";
    // Note: Iterator demonstration - in a full implementation we'd have better iterator support
    std::cout << "Alice=" << scores["Alice"] << " ";
    std::cout << "Bob=" << scores["Bob"] << " ";
    std::cout << "Charlie=" << scores["Charlie"] << " ";
    std::cout << "\n\n";
    
    // Example 2: Using complex value types
    std::cout << "2. Complex value types (vectors):\n";
    hashmap_cpp::HashMap<std::string, std::vector<int>> data;
    data["fibonacci"] = {1, 1, 2, 3, 5, 8, 13};
    data["primes"] = {2, 3, 5, 7, 11, 13};
    data["squares"] = {1, 4, 9, 16, 25};
    
    // Show the data (simplified since our iterator implementation is basic)
    std::cout << "fibonacci: ";
    for (int val : data["fibonacci"]) {
        std::cout << val << " ";
    }
    std::cout << "\nprimes: ";
    for (int val : data["primes"]) {
        std::cout << val << " ";
    }
    std::cout << "\nsquares: ";
    for (int val : data["squares"]) {
        std::cout << val << " ";
    }
    std::cout << "\n\n";
    
    // Example 3: Using custom objects
    std::cout << "3. Custom objects as values:\n";
    hashmap_cpp::HashMap<std::string, Person> people;
    people["emp001"] = Person("John Doe", 30, "john@company.com");
    people["emp002"] = Person("Jane Smith", 28, "jane@company.com");
    people["emp003"] = Person("Bob Wilson", 35, "bob@company.com");
    
    std::cout << "Employee directory:\n";
    // Simplified display since our iterator is basic
    if (people.contains("emp001")) {
        const Person& p1 = people["emp001"];
        std::cout << "emp001: " << p1.name << " (age " << p1.age << ", " << p1.email << ")\n";
    }
    if (people.contains("emp002")) {
        const Person& p2 = people["emp002"];
        std::cout << "emp002: " << p2.name << " (age " << p2.age << ", " << p2.email << ")\n";
    }
    if (people.contains("emp003")) {
        const Person& p3 = people["emp003"];
        std::cout << "emp003: " << p3.name << " (age " << p3.age << ", " << p3.email << ")\n";
    }
    std::cout << "\n";
    
    // Example 4: Integer keys
    std::cout << "4. Integer keys for lookup table:\n";
    hashmap_cpp::HashMap<int, std::string> httpCodes;
    httpCodes[200] = "OK";
    httpCodes[404] = "Not Found";
    httpCodes[500] = "Internal Server Error";
    httpCodes[403] = "Forbidden";
    
    std::vector<int> testCodes = {200, 404, 418, 500};
    for (int code : testCodes) {
        if (httpCodes.contains(code)) {
            std::cout << "HTTP " << code << ": " << httpCodes[code] << "\n";
        } else {
            std::cout << "HTTP " << code << ": Unknown\n";
        }
    }
    std::cout << "\n";
    
    // Example 5: Safe access with error handling
    std::cout << "5. Safe access with error handling:\n";
    hashmap_cpp::HashMap<std::string, double> prices;
    prices["apple"] = 1.99;
    prices["banana"] = 0.89;
    prices["orange"] = 2.49;
    
    std::vector<std::string> items = {"apple", "grape", "banana", "mango"};
    for (const std::string& item : items) {
        try {
            double price = prices.at(item);
            std::cout << item << ": $" << price << "\n";
        } catch (const std::out_of_range&) {
            std::cout << item << ": Not available\n";
        }
    }
    std::cout << "\n";
    
    // Example 6: Performance demonstration
    std::cout << "6. Performance test (inserting and retrieving 10000 items):\n";
    
    hashmap_cpp::HashMap<int, std::string> perfTest;
    
    // Insert 10000 items
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        perfTest[i] = "value_" + std::to_string(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto insertTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Retrieve all items
    start = std::chrono::high_resolution_clock::now();
    int sum = 0;
    for (int i = 0; i < 10000; ++i) {
        if (perfTest.contains(i)) {
            sum += i;
        }
    }
    end = std::chrono::high_resolution_clock::now();
    auto lookupTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Inserted 10000 items in " << insertTime.count() << " μs\n";
    std::cout << "Looked up 10000 items in " << lookupTime.count() << " μs\n";
    std::cout << "Final size: " << perfTest.size() << " items\n";
    std::cout << "Checksum: " << sum << "\n\n";
    
    // Example 7: Memory management demonstration
    std::cout << "7. Memory management (automatic cleanup):\n";
    {
        hashmap_cpp::HashMap<std::string, std::vector<int>> tempMap;
        tempMap["data1"] = std::vector<int>(1000, 42);
        tempMap["data2"] = std::vector<int>(1000, 84);
        std::cout << "Created temporary map with " << tempMap.size() << " large vectors\n";
        // Map and all vectors will be automatically cleaned up when scope ends
    }
    std::cout << "Temporary map automatically cleaned up\n\n";
    
    std::cout << "=== All examples completed successfully! ===\n";
    
    return 0;
}
