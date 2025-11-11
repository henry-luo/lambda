#pragma once

#include "lambda-data.hpp"
#include "element_reader.h"
#include <cstdint>
#include <functional>

/**
 * MarkReader API Family - Type-Safe Document Reading
 * 
 * Provides a fluent, type-safe C++ interface for traversing and extracting
 * data from Lambda Mark documents. All readers are stack-allocated value types
 * with automatic lifetime management (RAII).
 * 
 * MEMORY MODEL:
 * - All readers are VALUE TYPES (stack-allocated, copyable)
 * - No heap allocation for readers themselves
 * - Lightweight wrappers around Item/Pool pointers
 * - Automatic cleanup when scope ends (RAII)
 * - No manual memory management required
 * 
 * USAGE PATTERN:
 *   void format_document(Item root, Pool* pool) {
 *       MarkReader reader(root, pool);     // Stack-allocated
 *       ItemReader item = reader.getRoot(); // Returned by value
 *       
 *       if (item.isElement()) {
 *           ElementReader elem = item.asElement(); // By value
 *           // Use elem...
 *       }
 *   }  // All readers automatically destroyed
 */

// Forward declarations
class ItemReader;
class ElementReaderWrapper;  // Forward declaration
class MapReader;
class ArrayReader;
class AttributeReaderWrapper;  // Forward declaration

// ==============================================================================
// MarkReader - Document Root Reader
// ==============================================================================

/**
 * Root document reader with iteration and query capabilities
 * Stack-allocated value type, copyable
 */
class MarkReader {
private:
    Item root_;
    Pool* pool_;

public:
    // Lifecycle (value type semantics)
    explicit MarkReader(Item root, Pool* pool);
    ~MarkReader() = default;
    
    // Copyable and movable
    MarkReader(const MarkReader&) = default;
    MarkReader& operator=(const MarkReader&) = default;
    MarkReader(MarkReader&&) = default;
    MarkReader& operator=(MarkReader&&) = default;
    
    // Document access
    ItemReader getRoot() const;
    
    // Query operations (simple tag name matching for now)
    class ElementIterator {
    private:
        const MarkReader* reader_;
        const char* selector_;
        int64_t current_index_;
        void* state_;  // Internal traversal state
        
    public:
        ElementIterator(const MarkReader* reader, const char* selector);
        ~ElementIterator();
        
        bool next(ItemReader* out);
        void reset();
    };
    
    ElementIterator findAll(const char* selector) const;
    
    // Accessors
    Item root() const { return root_; }
    Pool* pool() const { return pool_; }
};

// ==============================================================================
// ItemReader - Type-Safe Item Wrapper
// ==============================================================================

/**
 * Type-safe wrapper for individual Items
 * Stack-allocated value type, lightweight (24 bytes)
 */
class ItemReader {
private:
    Item item_;
    Pool* pool_;
    TypeId cached_type_;

public:
    // Lifecycle (value type semantics)
    ItemReader();  // Default constructor for null item
    ItemReader(Item item, Pool* pool);
    ~ItemReader() = default;
    
    // Copyable and movable
    ItemReader(const ItemReader&) = default;
    ItemReader& operator=(const ItemReader&) = default;
    ItemReader(ItemReader&&) = default;
    ItemReader& operator=(ItemReader&&) = default;
    
    // Type checking
    TypeId getType() const { return cached_type_; }
    
    bool isNull() const;
    bool isString() const;
    bool isInt() const;
    bool isFloat() const;
    bool isBool() const;
    bool isElement() const;
    bool isMap() const;
    bool isArray() const;
    bool isList() const;
    
    // Safe type conversion (returns default-constructed on mismatch)
    String* asString() const;
    int64_t asInt() const;
    int32_t asInt32() const;
    double asFloat() const;
    bool asBool() const;
    ElementReaderWrapper asElement() const;
    MapReader asMap() const;
    ArrayReader asArray() const;
    
    // Convenience
    const char* cstring() const;  // Returns nullptr if not a string
    
    // Accessors
    Item item() const { return item_; }
    Pool* pool() const { return pool_; }
};

// ==============================================================================
// MapReader - Map Traversal
// ==============================================================================

/**
 * Type-safe wrapper for Map access
 * Stack-allocated value type
 */
class MapReader {
private:
    Map* map_;
    TypeMap* map_type_;
    Pool* pool_;

public:
    // Lifecycle (value type semantics)
    MapReader();  // Default constructor for null map
    MapReader(Map* map, Pool* pool);
    
    // Create from Item with type validation
    static MapReader fromItem(Item item, Pool* pool);
    
    ~MapReader() = default;
    
    // Copyable and movable
    MapReader(const MapReader&) = default;
    MapReader& operator=(const MapReader&) = default;
    MapReader(MapReader&&) = default;
    MapReader& operator=(MapReader&&) = default;
    
    // Map access
    ItemReader get(const char* key) const;
    bool has(const char* key) const;
    int64_t size() const;
    bool isEmpty() const { return size() == 0; }
    
    // Iterators
    class KeyIterator {
    private:
        const MapReader* reader_;
        ShapeEntry* current_field_;
        
    public:
        KeyIterator(const MapReader* reader);
        bool next(const char** key);
        void reset();
    };
    
    class ValueIterator {
    private:
        const MapReader* reader_;
        ShapeEntry* current_field_;
        
    public:
        ValueIterator(const MapReader* reader);
        bool next(ItemReader* value);
        void reset();
    };
    
    class EntryIterator {
    private:
        const MapReader* reader_;
        ShapeEntry* current_field_;
        
    public:
        EntryIterator(const MapReader* reader);
        bool next(const char** key, ItemReader* value);
        void reset();
    };
    
    KeyIterator keys() const;
    ValueIterator values() const;
    EntryIterator entries() const;
    
    // Accessors
    Map* map() const { return map_; }
    Pool* pool() const { return pool_; }
    bool isValid() const { return map_ != nullptr; }
};

// ==============================================================================
// ArrayReader - Array Traversal
// ==============================================================================

/**
 * Type-safe wrapper for Array access
 * Stack-allocated value type
 */
class ArrayReader {
private:
    Array* array_;
    Pool* pool_;

public:
    // Lifecycle (value type semantics)
    ArrayReader();  // Default constructor for null array
    ArrayReader(Array* array, Pool* pool);
    
    // Create from Item with type validation
    static ArrayReader fromItem(Item item, Pool* pool);
    
    ~ArrayReader() = default;
    
    // Copyable and movable
    ArrayReader(const ArrayReader&) = default;
    ArrayReader& operator=(const ArrayReader&) = default;
    ArrayReader(ArrayReader&&) = default;
    ArrayReader& operator=(ArrayReader&&) = default;
    
    // Array access
    ItemReader get(int64_t index) const;
    int64_t length() const;
    bool isEmpty() const { return length() == 0; }
    
    // Iterator
    class Iterator {
    private:
        const ArrayReader* reader_;
        int64_t index_;
        
    public:
        Iterator(const ArrayReader* reader);
        bool next(ItemReader* item);
        void reset();
        int64_t currentIndex() const { return index_; }
    };
    
    Iterator items() const;
    
    // Accessors
    Array* array() const { return array_; }
    Pool* pool() const { return pool_; }
    bool isValid() const { return array_ != nullptr; }
};

// ==============================================================================
// ElementReaderWrapper - C++ Wrapper for C ElementReader
// ==============================================================================

/**
 * C++ wrapper around the existing C ElementReader API
 * Provides additional convenience methods and RAII semantics
 * Note: The underlying ::ElementReader is from element_reader.h (C API)
 */
class ElementReaderWrapper {
private:
    ::ElementReader* reader_;  // C struct from element_reader.h
    Pool* pool_;
    bool owns_reader_;  // Whether we own the reader and should free it

public:
    // Lifecycle
    ElementReaderWrapper();  // Default constructor for invalid element
    explicit ElementReaderWrapper(const Element* element, Pool* pool);
    explicit ElementReaderWrapper(Item item, Pool* pool);
    
    // Wrap existing C ElementReader
    explicit ElementReaderWrapper(::ElementReader* reader, Pool* pool, bool take_ownership = false);
    
    ~ElementReaderWrapper();
    
    // Copy constructor and assignment (creates new C reader)
    ElementReaderWrapper(const ElementReaderWrapper& other);
    ElementReaderWrapper& operator=(const ElementReaderWrapper& other);
    
    // Move semantics (transfers ownership)
    ElementReaderWrapper(ElementReaderWrapper&& other) noexcept;
    ElementReaderWrapper& operator=(ElementReaderWrapper&& other) noexcept;
    
    // Element properties
    const char* tagName() const;
    int64_t tagNameLen() const;
    bool hasTag(const char* tag_name) const;
    int64_t childCount() const;
    int64_t attrCount() const;
    bool isEmpty() const;
    bool isTextOnly() const;
    
    // Child access
    ItemReader childAt(int64_t index) const;
    ItemReader findChild(const char* tag_name) const;
    String* textContent() const;
    
    // New methods from proposal
    ElementReaderWrapper findChildElement(const char* tag_name) const;
    bool hasChildElements() const;
    String* allText() const;
    
    // Iteration
    class ChildIterator {
    private:
        const ElementReaderWrapper* reader_;
        int64_t index_;
        
    public:
        ChildIterator(const ElementReaderWrapper* reader);
        bool next(ItemReader* item);
        void reset();
    };
    
    class ElementChildIterator {
    private:
        const ElementReaderWrapper* reader_;
        int64_t index_;
        
    public:
        ElementChildIterator(const ElementReaderWrapper* reader);
        bool next(ElementReaderWrapper* elem);
        void reset();
    };
    
    ChildIterator children() const;
    ElementChildIterator childElements() const;
    
    // Accessors
    ::ElementReader* cReader() const { return reader_; }
    Pool* pool() const { return pool_; }
    bool isValid() const { return reader_ != nullptr; }
    const Element* element() const;
};

// ==============================================================================
// AttributeReader Extensions (C++ API additions)
// ==============================================================================

/**
 * C++ wrapper around attribute access
 */
class AttributeReaderWrapper {
private:
    ::AttributeReader* attr_reader_;  // C struct from element_reader.h
    Pool* pool_;
    bool owns_reader_;

public:
    // Lifecycle
    AttributeReaderWrapper();  // Default constructor
    explicit AttributeReaderWrapper(const ElementReaderWrapper& elem);
    explicit AttributeReaderWrapper(::AttributeReader* attr_reader, Pool* pool, bool take_ownership = false);
    
    ~AttributeReaderWrapper();
    
    // Copy and move
    AttributeReaderWrapper(const AttributeReaderWrapper& other);
    AttributeReaderWrapper& operator=(const AttributeReaderWrapper& other);
    AttributeReaderWrapper(AttributeReaderWrapper&& other) noexcept;
    AttributeReaderWrapper& operator=(AttributeReaderWrapper&& other) noexcept;
    
    // Attribute access
    bool has(const char* key) const;
    const char* getString(const char* key) const;
    ItemReader getItem(const char* key) const;
    
    // With defaults
    const char* getStringOr(const char* key, const char* default_value) const;
    int64_t getIntOr(const char* key, int64_t default_value) const;
    
    // Iterator
    class Iterator {
    private:
        const AttributeReaderWrapper* reader_;
        ShapeEntry* current_field_;
        
    public:
        Iterator(const AttributeReaderWrapper* reader);
        bool next(const char** key, ItemReader* value);
        void reset();
    };
    
    Iterator iterator() const;
    
    // Accessors
    ::AttributeReader* cReader() const { return attr_reader_; }
    Pool* pool() const { return pool_; }
    bool isValid() const { return attr_reader_ != nullptr; }
};
