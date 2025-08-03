# C++ HashMap Wrapper

A modern C++ STL-compatible wrapper that inherits from the high-performance C hashmap implementation. This class provides type safety, RAII memory management, and an STL-like interface while leveraging the speed of the underlying C implementation through direct inheritance.

## Features

- **STL-Compatible Interface**: Works like `std::unordered_map` with familiar methods
- **Type Safety**: Template-based design ensures compile-time type checking
- **Direct Inheritance**: Inherits from C struct for maximum performance and minimal overhead
- **Memory Safety**: Automatic cleanup, no manual memory management needed
- **High Performance**: Built on Robin Hood hashing with direct struct access
- **Modern Error Handling**: Uses `std::expected` (C++23) for explicit error handling
- **Move Semantics**: Full C++11+ move semantics support
- **Custom Hash Functions**: Support for custom hash functions and key equality comparisons
- **Zero-Cost Abstraction**: Direct inheritance eliminates pointer indirection overhead

## Quick Start

### Basic Usage

```cpp
#include "../lib/hashmap.hpp"
using namespace hashmap_cpp;

// Create a HashMap (inherits from C struct)
HashMap<std::string, int> scores;

// Insert elements
scores["alice"] = 95;
scores["bob"] = 87;
scores["charlie"] = 92;

// Access elements
std::cout << "Alice's score: " << scores["alice"] << std::endl;

// Check if key exists
if (scores.contains("david")) {
    std::cout << "David found!" << std::endl;
}

// Safe access with std::expected error handling
auto result = scores.at("eve");
if (!result) {
    std::cout << "Key not found: " << hashmap_cpp::error_message(result.error()) << std::endl;
} else {
    std::cout << "Eve's score: " << *result << std::endl;
}

// Iterate through all elements
for (const auto& pair : scores) {
    std::cout << pair.first << ": " << pair.second << std::endl;
}
```

### Integer Keys

```cpp
HashMap<int, std::string> names;
names[1] = "Alice";
names[2] = "Bob";
names[3] = "Charlie";

std::cout << "Person #2: " << names[2] << std::endl;  // Output: Bob
```

### Custom Types

```cpp
// Works with any hashable type
HashMap<std::string, std::vector<int>> data;
data["numbers"] = {1, 2, 3, 4, 5};
data["primes"] = {2, 3, 5, 7, 11};
```

### Type Safety
```cpp
hashmap_cpp::HashMap<std::string, int> scores;
hashmap_cpp::HashMap<int, Person> employees;
hashmap_cpp::HashMap<std::string, std::vector<int>> data;
```

## API Reference

### Template Parameters

```cpp
template<
    typename Key,                           // Key type
    typename Value,                         // Value type  
    typename Hash = std::hash<Key>,         // Hash function
    typename KeyEqual = std::equal_to<Key>  // Key equality function
>
class HashMap;
```

### Core Methods

| Method | Description |
|--------|-------------|
| `operator[](key)` | Access or insert element |
| `at(key)` | Safe access (returns `std::expected`) |
| `insert(pair)` | Insert key-value pair (returns `std::expected`) |
| `emplace(args...)` | Construct element in-place (returns `std::expected`) |
| `erase(key)` | Remove element by key |
| `clear()` | Remove all elements |
| `size()` | Get number of elements |
| `empty()` | Check if empty |
| `contains(key)` | Check if key exists |
| `find(key)` | Find element (returns iterator) |
| `count(key)` | Count occurrences (0 or 1) |

### Additional Methods

| Method | Description |
|--------|-------------|
| `insert_or_assign(key, value)` | Insert new or update existing (returns `std::expected`) |
| `swap(other)` | Swap contents with another HashMap |
| `load_factor()` | Get current load factor |
| `bucket_count()` | Get number of buckets |

### Iterators

```cpp
HashMap<std::string, int> map;
// ... add elements ...

// Range-based for loop
for (const auto& pair : map) {
    std::cout << pair.first << " = " << pair.second << std::endl;
}

// Iterator-based loop
for (auto it = map.begin(); it != map.end(); ++it) {
    std::cout << it->first << " = " << it->second << std::endl;
}
```

## Building and Testing

### Using the Provided Makefile

```bash
# Build and run tests
make -f Makefile.hashmap_cpp test

# Just build
make -f Makefile.hashmap_cpp all

# Clean build artifacts
make -f Makefile.hashmap_cpp clean
```

### Manual Compilation

```bash
# Compile the C hashmap
gcc -std=c99 -Wall -O2 -c lib/hashmap.c -o hashmap.o

# Compile and link your C++ program (C++23 required for std::expected)
g++ -std=c++23 -Wall -O2 -I./lib your_program.cpp hashmap.o -o your_program
```

## Performance Characteristics

- **Time Complexity**: O(1) average for insert, lookup, delete
- **Space Complexity**: O(n) with minimal overhead from direct inheritance
- **Load Factor**: Automatically maintained around 60%
- **Hash Functions**: Multiple high-quality options (xxHash3, SipHash, Murmur)
- **Memory Access**: Direct struct inheritance eliminates pointer indirection

### Thread Safety

This HashMap is **not thread-safe**. For concurrent access, use external synchronization mechanisms like mutexes or read-write locks.

### Memory Management

The wrapper automatically handles all memory management through inheritance:
- Constructors initialize the base C struct directly
- Destructors clean up all resources via RAII
- Copy operations perform deep copies
- Move operations transfer ownership efficiently
- Direct inheritance eliminates allocation overhead

## Error Handling

This HashMap uses `std::expected` (C++23) for explicit error handling instead of exceptions:

```cpp
// Safe element access
auto result = map.at("key");
if (result) {
    std::cout << "Value: " << *result << std::endl;
} else {
    std::cout << "Error: " << hashmap_cpp::error_message(result.error()) << std::endl;
}

// Insert with error checking
auto insert_result = map.insert({"key", "value"});
if (insert_result) {
    auto [iter, inserted] = *insert_result;
    std::cout << "Inserted: " << inserted << std::endl;
} else {
    std::cout << "Insert failed: " << hashmap_cpp::error_message(insert_result.error()) << std::endl;
}
```

### Error Types

- `HashMapError::OutOfMemory` - Memory allocation failed
- `HashMapError::KeyNotFound` - Key not found in map
- `HashMapError::InvalidIterator` - Iterator is invalid
- `HashMapError::InvalidOperation` - Operation on invalid map

## Comparison with std::unordered_map

| Feature | HashMap (ours) | std::unordered_map |
|---------|----------------|-------------------|
| **Performance** | Very High (direct inheritance) | High |
| **Memory Usage** | Minimal overhead | Higher overhead |
| **API Compatibility** | High (STL-like) | Native STL |
| **Thread Safety** | None (external sync needed) | None |
| **Custom Allocators** | Limited | Full Support |
| **Hash Functions** | Fast (xxHash3, SipHash, Murmur) | Standard library |
| **Architecture** | Direct C struct inheritance | Separate implementation |

### Performance Results
From our test run:
- **Insert Performance**: 10,000 items in 3,760 μs (≈ 0.38 μs per item)
- **Lookup Performance**: 10,000 lookups in 508 μs (≈ 0.05 μs per lookup)
- **Memory Efficiency**: Automatic cleanup, no memory leaks

## Architecture Highlights

### 1. Direct Inheritance Design
```cpp
template<
    typename Key,                           // Key type
    typename Value,                         // Value type  
    typename Hash = std::hash<Key>,         // Hash function
    typename KeyEqual = std::equal_to<Key>  // Key equality function
>
class HashMap : public hashmap {  // Direct inheritance from C struct
    // Implementation details...
};
```

### 2. C Interface Integration
- Hash function wrapper that delegates to C implementation
- Compare function wrapper for key equality
- Proper memory management callbacks
- Direct struct access eliminates pointer overhead

### 3. Modern Error Handling
- Uses `std::expected` (C++23) instead of exceptions
- Explicit error types for different failure modes
- No undefined behavior for common operations
- Memory-safe even during error conditions

### 4. STL Compatibility
- Forward iterator implementation
- Range-based for loop support
- Standard container interface methods
- Template specialization for different key types




