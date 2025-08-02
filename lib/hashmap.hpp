#ifndef HASHMAP_CPP_HPP
#define HASHMAP_CPP_HPP

#include <functional>
#include <memory>
#include <utility>
#include <expected>
#include <iterator>
#include <type_traits>
#include <cstring>
#include <random>
#include <cstdint>
#include <cstddef>
#include <vector>

// Forward declare the C struct to avoid including the header here
struct hashmap;

// C function declarations
extern "C" {
    struct hashmap *hashmap_new(size_t elsize, size_t cap, uint64_t seed0, 
        uint64_t seed1, 
        uint64_t (*hash)(const void *item, uint64_t seed0, uint64_t seed1),
        int (*compare)(const void *a, const void *b, void *udata),
        void (*elfree)(void *item),
        void *udata);
    
    void hashmap_free(struct hashmap *map);
    void hashmap_clear(struct hashmap *map, bool update_cap);
    size_t hashmap_count(struct hashmap *map);
    bool hashmap_oom(struct hashmap *map);
    const void *hashmap_get(struct hashmap *map, const void *item);
    const void *hashmap_set(struct hashmap *map, const void *item);
    const void *hashmap_delete(struct hashmap *map, const void *item);
    bool hashmap_iter(struct hashmap *map, size_t *i, void **item);
    uint64_t hashmap_xxhash3(const void *data, size_t len, uint64_t seed0, uint64_t seed1);
}

namespace hashmap_cpp {

// Error types for std::expected-based error handling
enum class HashMapError {
    OutOfMemory,
    KeyNotFound,
    InvalidIterator,
    InvalidOperation
};

// Error message helper
inline const char* error_message(HashMapError error) {
    switch (error) {
        case HashMapError::OutOfMemory: return "Out of memory";
        case HashMapError::KeyNotFound: return "Key not found";
        case HashMapError::InvalidIterator: return "Invalid iterator";
        case HashMapError::InvalidOperation: return "Invalid operation";
        default: return "Unknown error";
    }
}

/**
 * A modern C++ STL-compatible wrapper around the C hashmap implementation.
 * 
 * This class provides an STL-like interface with:
 * - Type safety
 * - RAII memory management
 * - STL-compatible iterators
 * - Exception safety
 * - Template-based key-value pairs
 * 
 * Template parameters:
 * - Key: The key type
 * - Value: The value type
 * - Hash: Hash function type (defaults to std::hash<Key>)
 * - KeyEqual: Key equality comparison type (defaults to std::equal_to<Key>)
 */
template<
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>
>
class HashMap {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;  // Remove const from Key
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

private:
    struct Entry {
        Key key;
        Value value;
        
        Entry() = default;
        Entry(const Key& k, const Value& v) : key(k), value(v) {}
        Entry(Key&& k, Value&& v) : key(std::move(k)), value(std::move(v)) {}
        
        template<typename... Args>
        Entry(const Key& k, Args&&... args) : key(k), value(std::forward<Args>(args)...) {}
    };

    struct hashmap* map_;
    Hash hasher_;
    KeyEqual key_equal_;
    
    static uint64_t hash_function(const void* item, uint64_t seed0, uint64_t seed1) {
        const Entry* entry = static_cast<const Entry*>(item);
        Hash hash_fn;
        
        // Use the key for hashing with improved type handling
        if constexpr (std::is_arithmetic_v<Key>) {
            return hashmap_xxhash3(&entry->key, sizeof(Key), seed0, seed1);
        } else if constexpr (std::is_same_v<Key, std::string>) {
            return hashmap_xxhash3(entry->key.c_str(), entry->key.length(), seed0, seed1);
        } else if constexpr (std::is_same_v<Key, const char*>) {
            size_t len = std::strlen(entry->key);
            return hashmap_xxhash3(entry->key, len, seed0, seed1);
        } else if constexpr (std::is_pointer_v<Key>) {
            // Hash pointer value
            uintptr_t ptr_val = reinterpret_cast<uintptr_t>(entry->key);
            return hashmap_xxhash3(&ptr_val, sizeof(ptr_val), seed0, seed1);
        } else {
            // For other types, use std::hash and combine with seeds
            auto h = hash_fn(entry->key);
            // Mix the hash with seeds for better distribution
            h ^= seed0 + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= seed1 + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    }
    
    static int compare_function(const void* a, const void* b, void* udata) {
        const Entry* entry_a = static_cast<const Entry*>(a);
        const Entry* entry_b = static_cast<const Entry*>(b);
        KeyEqual* key_equal = static_cast<KeyEqual*>(udata);
        
        if ((*key_equal)(entry_a->key, entry_b->key)) {
            return 0;
        }
        
        // For ordering, we need a consistent comparison
        if constexpr (std::is_arithmetic_v<Key>) {
            return (entry_a->key < entry_b->key) ? -1 : 1;
        } else {
            // For non-arithmetic types, use memory comparison as fallback
            // This is not ideal but ensures consistent ordering
            return std::memcmp(&entry_a->key, &entry_b->key, sizeof(Key));
        }
    }
    
    static void free_function(void* item) {
        Entry* entry = static_cast<Entry*>(item);
        entry->~Entry();
    }

public:
    // Forward declarations for proper iterator types
    class iterator;
    class const_iterator;
    
    // Proper const_iterator implementation
    class const_iterator {
        friend class HashMap;
    private:
        const HashMap* map_;
        size_t index_;
        const Entry* current_entry_;
        mutable std::pair<Key, Value> current_pair_;
        
        void advance_to_next() {
            void* item = nullptr;
            if (map_ && hashmap_iter(const_cast<struct hashmap*>(map_->map_), &index_, &item)) {
                current_entry_ = static_cast<const Entry*>(item);
                if (current_entry_) {
                    current_pair_.first = current_entry_->key;
                    current_pair_.second = current_entry_->value;
                }
            } else {
                current_entry_ = nullptr;
            }
        }
        
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, const Value>;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;
        
        const_iterator() : map_(nullptr), index_(0), current_entry_(nullptr) {}
        
        const_iterator(const HashMap* map, size_t idx = 0) 
            : map_(map), index_(idx), current_entry_(nullptr) {
            if (map_ && map_->map_) {
                advance_to_next();
            }
        }
        
        reference operator*() const {
            if (!current_entry_) {
                // Return a static error pair instead of throwing
                static const std::pair<const Key, const Value> error_pair{};
                return error_pair;
            }
            return current_pair_;
        }
        
        pointer operator->() const {
            return &(operator*());
        }
        
        const_iterator& operator++() {
            if (current_entry_) {
                advance_to_next();
            }
            return *this;
        }
        
        const_iterator operator++(int) {
            const_iterator temp = *this;
            ++(*this);
            return temp;
        }
        
        bool operator==(const const_iterator& other) const {
            return current_entry_ == other.current_entry_;
        }
        
        bool operator!=(const const_iterator& other) const {
            return !(*this == other);
        }
        
        // Safe methods that return std::expected
        std::expected<const Key*, HashMapError> key() const {
            if (!current_entry_) {
                return std::unexpected(HashMapError::InvalidIterator);
            }
            return &current_entry_->key;
        }
        
        std::expected<const Value*, HashMapError> value() const {
            if (!current_entry_) {
                return std::unexpected(HashMapError::InvalidIterator);
            }
            return &current_entry_->value;
        }
        
        std::expected<const std::pair<const Key, const Value>*, HashMapError> get() const {
            if (!current_entry_) {
                return std::unexpected(HashMapError::InvalidIterator);
            }
            return &current_pair_;
        }
        
        bool valid() const {
            return current_entry_ != nullptr;
        }
    };

    // Iterator class for STL compatibility with proper mutable access
    class iterator {
        friend class HashMap;
    private:
        HashMap* map_;
        size_t index_;
        Entry* current_entry_;
        mutable std::pair<Key, Value> current_pair_;
        
        void advance_to_next() {
            void* item = nullptr;
            if (map_ && hashmap_iter(map_->map_, &index_, &item)) {
                current_entry_ = static_cast<Entry*>(item);
                if (current_entry_) {
                    current_pair_.first = current_entry_->key;
                    current_pair_.second = current_entry_->value;
                }
            } else {
                current_entry_ = nullptr;
            }
        }
        
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, Value>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;
        
        iterator() : map_(nullptr), index_(0), current_entry_(nullptr) {}
        
        iterator(HashMap* map, size_t idx = 0) 
            : map_(map), index_(idx), current_entry_(nullptr) {
            if (map_ && map_->map_) {
                advance_to_next();
            }
        }
        
        reference operator*() const {
            if (!current_entry_) {
                // Return a static error pair instead of throwing
                static std::pair<const Key, Value> error_pair{};
                return error_pair;
            }
            // Update current_pair_ with latest values
            current_pair_.first = current_entry_->key;
            current_pair_.second = current_entry_->value;
            return current_pair_;
        }
        
        pointer operator->() const {
            return &(operator*());
        }
        
        iterator& operator++() {
            if (current_entry_) {
                advance_to_next();
            }
            return *this;
        }
        
        iterator operator++(int) {
            iterator temp = *this;
            ++(*this);
            return temp;
        }
        
        bool operator==(const iterator& other) const {
            return current_entry_ == other.current_entry_;
        }
        
        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
        
        // Allow comparison between iterator and const_iterator
        bool operator==(const const_iterator& other) const {
            return current_entry_ == other.current_entry_;
        }
        
        bool operator!=(const const_iterator& other) const {
            return !(*this == other);
        }
        
        // Conversion to const_iterator (friend-based approach would be better)
        operator const_iterator() const {
            if (current_entry_) {
                return const_iterator(map_, index_);
            }
            return const_iterator();
        }
        
        // Safe methods that return std::expected
        std::expected<const Key*, HashMapError> key() const {
            if (!current_entry_) {
                return std::unexpected(HashMapError::InvalidIterator);
            }
            return &current_entry_->key;
        }
        
        std::expected<Value*, HashMapError> value() const {
            if (!current_entry_) {
                return std::unexpected(HashMapError::InvalidIterator);
            }
            return &current_entry_->value;
        }
        
        std::expected<std::pair<const Key, Value>*, HashMapError> get() const {
            if (!current_entry_) {
                return std::unexpected(HashMapError::InvalidIterator);
            }
            return &current_pair_;
        }
        
        bool valid() const {
            return current_entry_ != nullptr;
        }
    };

    // Constructors
    explicit HashMap(size_type bucket_count = 16, 
                    const Hash& hash = Hash{}, 
                    const KeyEqual& equal = KeyEqual{})
        : hasher_(hash), key_equal_(equal) {
        
        // Generate random seeds
        std::random_device rd;
        uint64_t seed0 = rd();
        uint64_t seed1 = rd();
        
        map_ = hashmap_new(sizeof(Entry), bucket_count, seed0, seed1,
                          hash_function, compare_function, free_function, &key_equal_);
        
        if (!map_) {
            // For constructor, we still need to handle this somehow
            // We'll set map_ to nullptr and check it in all operations
            map_ = nullptr;
        }
    }
    
    // Factory method that returns std::expected for safe construction
    static std::expected<HashMap, HashMapError> create(
        size_type bucket_count = 16, 
        const Hash& hash = Hash{}, 
        const KeyEqual& equal = KeyEqual{}
    ) {
        HashMap map(bucket_count, hash, equal);
        if (!map.map_) {
            return std::unexpected(HashMapError::OutOfMemory);
        }
        return map;
    }
    
    // Copy constructor
    HashMap(const HashMap& other) 
        : hasher_(other.hasher_), key_equal_(other.key_equal_) {
        
        if (!other.map_) {
            map_ = nullptr;
            return;
        }
        
        std::random_device rd;
        uint64_t seed0 = rd();
        uint64_t seed1 = rd();
        
        map_ = hashmap_new(sizeof(Entry), other.size() * 2, seed0, seed1,
                          hash_function, compare_function, free_function, &key_equal_);
        
        if (!map_) {
            return;
        }
        
        // Copy all elements using manual iteration
        size_t iter_index = 0;
        void* item = nullptr;
        while (hashmap_iter(other.map_, &iter_index, &item)) {
            Entry* entry = static_cast<Entry*>(item);
            Entry new_entry(entry->key, entry->value);
            hashmap_set(map_, &new_entry);
            if (hashmap_oom(map_)) {
                hashmap_free(map_);
                map_ = nullptr;
                return;
            }
        }
    }
    
    // Move constructor
    HashMap(HashMap&& other) noexcept 
        : map_(other.map_), hasher_(std::move(other.hasher_)), 
          key_equal_(std::move(other.key_equal_)) {
        other.map_ = nullptr;
    }
    
    // Destructor
    ~HashMap() {
        if (map_) {
            hashmap_free(map_);
        }
    }
    
    // Copy assignment
    HashMap& operator=(const HashMap& other) {
        if (this != &other) {
            HashMap temp(other);
            swap(temp);
        }
        return *this;
    }
    
    // Move assignment
    HashMap& operator=(HashMap&& other) noexcept {
        if (this != &other) {
            if (map_) {
                hashmap_free(map_);
            }
            map_ = other.map_;
            hasher_ = std::move(other.hasher_);
            key_equal_ = std::move(other.key_equal_);
            other.map_ = nullptr;
        }
        return *this;
    }
    
    // Hash function and equality access
    hasher hash_function() const {
        return hasher_;
    }
    
    key_equal key_eq() const {
        return key_equal_;
    }
    
    // Get hash value for a key
    size_type hash_value(const Key& key) const {
        return static_cast<size_type>(hasher_(key));
    }
    
    // Check if the HashMap is valid (not in error state)
    bool valid() const {
        return map_ != nullptr;
    }
    
    // Safe element access that returns std::expected
    std::expected<Value*, HashMapError> try_at(const Key& key) {
        if (!map_) {
            return std::unexpected(HashMapError::InvalidOperation);
        }
        
        Entry search_entry(key, Value{});
        const Entry* found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (!found) {
            return std::unexpected(HashMapError::KeyNotFound);
        }
        
        return &const_cast<Entry*>(found)->value;
    }
    
    std::expected<const Value*, HashMapError> try_at(const Key& key) const {
        if (!map_) {
            return std::unexpected(HashMapError::InvalidOperation);
        }
        
        Entry search_entry(key, Value{});
        const Entry* found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (!found) {
            return std::unexpected(HashMapError::KeyNotFound);
        }
        
        return &found->value;
    }
    
    // Element access
    Value& operator[](const Key& key) {
        if (!map_) {
            // Return a static default value instead of throwing
            static Value default_value{};
            return default_value;
        }
        
        Entry search_entry(key, Value{});
        const Entry* found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (found) {
            return const_cast<Entry*>(found)->value;
        } else {
            // Insert new element with default-constructed value
            Entry new_entry(key, Value{});
            hashmap_set(map_, &new_entry);
            
            if (hashmap_oom(map_)) {
                static Value default_value{};
                return default_value;
            }
            
            // Get the inserted element
            found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
            return const_cast<Entry*>(found)->value;
        }
    }
    
    Value& operator[](Key&& key) {
        if (!map_) {
            static Value default_value{};
            return default_value;
        }
        
        Entry search_entry(key, Value{});
        const Entry* found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (found) {
            return const_cast<Entry*>(found)->value;
        } else {
            // Insert new element with default-constructed value
            Entry new_entry(std::move(key), Value{});
            hashmap_set(map_, &new_entry);
            
            if (hashmap_oom(map_)) {
                static Value default_value{};
                return default_value;
            }
            
            // Get the inserted element
            found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
            return const_cast<Entry*>(found)->value;
        }
    }
    
    Value& at(const Key& key) {
        static Value default_value{};
        auto result = try_at(key);
        return result ? *result.value() : default_value;
    }
    
    const Value& at(const Key& key) const {
        static const Value default_value{};
        auto result = try_at(key);
        return result ? *result.value() : default_value;
    }
    
    // Iterators
    iterator begin() {
        if (!map_) {
            return iterator();
        }
        return iterator(this, 0);
    }
    
    const_iterator begin() const {
        if (!map_) {
            return const_iterator();
        }
        return const_iterator(this, 0);
    }
    
    const_iterator cbegin() const {
        return begin();
    }
    
    iterator end() {
        return iterator();
    }
    
    const_iterator end() const {
        return const_iterator();
    }
    
    const_iterator cend() const {
        return end();
    }
    
    // Capacity
    bool empty() const {
        return size() == 0;
    }
    
    size_type size() const {
        if (!map_) {
            return 0;
        }
        return hashmap_count(map_);
    }
    
    // Modifiers
    void clear() {
        if (map_) {
            hashmap_clear(map_, false);
        }
    }
    
    // Safe insert operation that returns std::expected
    std::expected<std::pair<iterator, bool>, HashMapError> try_insert(const value_type& value) {
        if (!map_) {
            return std::unexpected(HashMapError::InvalidOperation);
        }
        
        // Check if key already exists
        Entry search_entry(value.first, Value{});
        const Entry* existing = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (existing) {
            // Key already exists, find the iterator and return false
            for (auto it = begin(); it != end(); ++it) {
                if (it.current_entry_ == existing) {
                    return std::make_pair(it, false);
                }
            }
            return std::make_pair(end(), false);
        }
        
        // Key doesn't exist, insert new entry
        Entry* entry = new(std::nothrow) Entry(value.first, value.second);
        if (!entry) {
            return std::unexpected(HashMapError::OutOfMemory);
        }
        
        hashmap_set(map_, entry);
        
        if (hashmap_oom(map_)) {
            delete entry;
            return std::unexpected(HashMapError::OutOfMemory);
        }
        
        // Successfully inserted, find the iterator
        for (auto it = begin(); it != end(); ++it) {
            if (it.current_entry_ == entry) {
                return std::make_pair(it, true);
            }
        }
        
        // Should not reach here
        return std::make_pair(end(), true);
    }
    
    // Exception-safe insert operation (backward compatibility)
    std::pair<iterator, bool> insert(const value_type& value) {
        auto result = try_insert(value);
        if (result) {
            return result.value();
        }
        // Return end iterator for failure
        return std::make_pair(end(), false);
    }
    
    // Safe insert with move semantics
    std::expected<std::pair<iterator, bool>, HashMapError> try_insert(value_type&& value) {
        if (!map_) {
            return std::unexpected(HashMapError::InvalidOperation);
        }
        
        // Check if key already exists
        Entry search_entry(value.first, Value{});
        const Entry* existing = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (existing) {
            // Key already exists, find the iterator and return false
            for (auto it = begin(); it != end(); ++it) {
                if (it.current_entry_ == existing) {
                    return std::make_pair(it, false);
                }
            }
            return std::make_pair(end(), false);
        }
        
        // Key doesn't exist, insert new entry
        Entry* entry = new(std::nothrow) Entry(std::move(const_cast<Key&>(value.first)), std::move(value.second));
        if (!entry) {
            return std::unexpected(HashMapError::OutOfMemory);
        }
        
        hashmap_set(map_, entry);
        
        if (hashmap_oom(map_)) {
            delete entry;
            return std::unexpected(HashMapError::OutOfMemory);
        }
        
        // Successfully inserted, find the iterator
        for (auto it = begin(); it != end(); ++it) {
            if (it.current_entry_ == entry) {
                return std::make_pair(it, true);
            }
        }
        
        return std::make_pair(end(), true);
    }
    
    // Exception-safe insert with move semantics
    std::pair<iterator, bool> insert(value_type&& value) {
        auto result = try_insert(std::move(value));
        if (result) {
            return result.value();
        }
        return std::make_pair(end(), false);
    }
    
    // Safe emplace operation
    template<typename... Args>
    std::expected<std::pair<iterator, bool>, HashMapError> try_emplace(Args&&... args) {
        if (!map_) {
            return std::unexpected(HashMapError::InvalidOperation);
        }
        
        // Create temporary entry to check if key exists
        Entry temp_entry(std::forward<Args>(args)...);
        Entry search_entry(temp_entry.key, Value{});
        const Entry* existing = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (existing) {
            // Key already exists, find the iterator and return false
            for (auto it = begin(); it != end(); ++it) {
                if (it.current_entry_ == existing) {
                    return std::make_pair(it, false);
                }
            }
            return std::make_pair(end(), false);
        }
        
        // Key doesn't exist, create and insert new entry
        Entry* entry = new(std::nothrow) Entry(std::move(temp_entry));
        if (!entry) {
            return std::unexpected(HashMapError::OutOfMemory);
        }
        
        hashmap_set(map_, entry);
        
        if (hashmap_oom(map_)) {
            delete entry;
            return std::unexpected(HashMapError::OutOfMemory);
        }
        
        // Successfully inserted, find the iterator
        for (auto it = begin(); it != end(); ++it) {
            if (it.current_entry_ == entry) {
                return std::make_pair(it, true);
            }
        }
        
        return std::make_pair(end(), true);
    }
    
    // Exception-safe emplace operation
    template<typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        auto result = try_emplace(std::forward<Args>(args)...);
        if (result) {
            return result.value();
        }
        return std::make_pair(end(), false);
    }
    
    size_type erase(const Key& key) {
        if (!map_) {
            return 0;
        }
        
        Entry search_entry(key, Value{});
        const Entry* removed = static_cast<const Entry*>(hashmap_delete(map_, &search_entry));
        
        return removed ? 1 : 0;
    }
    
    void swap(HashMap& other) noexcept {
        std::swap(map_, other.map_);
        std::swap(hasher_, other.hasher_);
        std::swap(key_equal_, other.key_equal_);
    }
    
    // Lookup
    size_type count(const Key& key) const {
        if (!map_) {
            return 0;
        }
        
        Entry search_entry(key, Value{});
        const Entry* found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        return found ? 1 : 0;
    }
    
    iterator find(const Key& key) {
        if (!map_) {
            return end();
        }
        
        Entry search_entry(key, Value{});
        const Entry* found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (found) {
            // Find iterator pointing to found element
            for (auto it = begin(); it != end(); ++it) {
                if (it.current_entry_ == found) {
                    return it;
                }
            }
        }
        return end();
    }
    
    const_iterator find(const Key& key) const {
        if (!map_) {
            return end();
        }
        
        Entry search_entry(key, Value{});
        const Entry* found = static_cast<const Entry*>(hashmap_get(map_, &search_entry));
        
        if (found) {
            // Find iterator pointing to found element
            for (auto it = begin(); it != end(); ++it) {
                if (it.current_entry_ == found) {
                    return it;
                }
            }
        }
        return end();
    }
    
    bool contains(const Key& key) const {
        return count(key) > 0;
    }
    
    // Hash policy
    float load_factor() const {
        // The C implementation doesn't expose this directly
        // We can estimate it
        return static_cast<float>(size()) / bucket_count();
    }
    
    size_type bucket_count() const {
        // The C implementation doesn't expose bucket count directly
        // We'll estimate based on size
        return size() > 0 ? size() * 2 : 16;
    }
    
    // Additional STL compatibility methods
    std::pair<iterator, iterator> equal_range(const Key& key) {
        iterator it = find(key);
        if (it != end()) {
            iterator next = it;
            ++next;
            return std::make_pair(it, next);
        }
        return std::make_pair(end(), end());
    }
    
    std::pair<const_iterator, const_iterator> equal_range(const Key& key) const {
        const_iterator it = find(key);
        if (it != end()) {
            const_iterator next = it;
            ++next;
            return std::make_pair(it, next);
        }
        return std::make_pair(end(), end());
    }
    
    // Insert or assign (C++17 compatibility)
    template<typename M>
    std::pair<iterator, bool> insert_or_assign(const Key& key, M&& obj) {
        auto it = find(key);
        if (it != end()) {
            auto value_result = it.value();
            if (value_result) {
                *value_result.value() = std::forward<M>(obj);
            }
            return std::make_pair(it, false);
        }
        return insert(std::make_pair(key, std::forward<M>(obj)));
    }
    
    iterator erase(const_iterator pos) {
        if (pos != end()) {
            Key key = pos.key();
            auto it = find(key);
            if (it != end()) {
                ++it; // Get next iterator
                erase(key);
                return it;
            }
        }
        return end();
    }
    
    iterator erase(const_iterator first, const_iterator last) {
        std::vector<Key> keys_to_erase;
        for (auto it = first; it != last; ++it) {
            keys_to_erase.push_back(it.key());
        }
        
        iterator result = end();
        for (const auto& key : keys_to_erase) {
            auto it = find(key);
            if (it != end()) {
                result = it;
                ++result;
                erase(key);
            }
        }
        return result;
    }
    
    // Comparison operators
    bool operator==(const HashMap& rhs) const {
        if (size() != rhs.size()) {
            return false;
        }
        
        for (const auto& pair : *this) {
            auto it = rhs.find(pair.first);
            if (it == rhs.end() || it->second != pair.second) {
                return false;
            }
        }
        
        return true;
    }
    
    bool operator!=(const HashMap& rhs) const {
        return !(*this == rhs);
    }
};

// Non-member functions
template<typename Key, typename Value, typename Hash, typename KeyEqual>
void swap(HashMap<Key, Value, Hash, KeyEqual>& lhs, 
          HashMap<Key, Value, Hash, KeyEqual>& rhs) noexcept {
    lhs.swap(rhs);
}

template<typename Key, typename Value, typename Hash, typename KeyEqual>
bool operator==(const HashMap<Key, Value, Hash, KeyEqual>& lhs,
                const HashMap<Key, Value, Hash, KeyEqual>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    
    for (const auto& pair : lhs) {
        auto it = rhs.find(pair.first);
        if (it == rhs.end() || it->second != pair.second) {
            return false;
        }
    }
    
    return true;
}

template<typename Key, typename Value, typename Hash, typename KeyEqual>
bool operator!=(const HashMap<Key, Value, Hash, KeyEqual>& lhs,
                const HashMap<Key, Value, Hash, KeyEqual>& rhs) {
    return !(lhs == rhs);
}

} // namespace hashmap_cpp

#endif // HASHMAP_CPP_HPP
