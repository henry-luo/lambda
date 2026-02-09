#pragma once

#include "lambda-data.hpp"
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
 * - Lightweight wrappers around raw Element/Map/Array pointers
 * - NO Pool* required - readers are completely pool-free!
 * - Automatic cleanup when scope ends (RAII)
 * - No manual memory management required
 *
 * USAGE PATTERN:
 *   void format_document(Item root) {
 *       MarkReader reader(root);            // Stack-allocated, no pool needed
 *       ItemReader item = reader.getRoot(); // Returned by value
 *
 *       if (item.isElement()) {
 *           ElementReaderWrapper elem = item.asElement(); // By value, no pool
 *           // Traverse children
 *           auto iter = elem.children();     // No pool needed
 *           ItemReader child;
 *           while (iter.next(&child)) {
 *               // Process child...
 *           }
 *       }
 *   }  // All readers automatically destroyed
 *
 * TEXT EXTRACTION:
 *   Pool* pool = ...;  // Pool only needed for text extraction
 *   StringBuf* sb = stringbuf_new(pool);
 *   elem.textContent(sb);  // Extracts text into provided StringBuf
 *   String* text = stringbuf_to_string(sb);
 */

// Forward declarations
class ItemReader;
class ElementReader;  // Forward declaration
class MapReader;
class ArrayReader;

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

public:
    // Lifecycle (value type semantics)
    explicit MarkReader(Item root);
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
    TypeId cached_type_;

public:
    // Lifecycle (value type semantics)
    ItemReader();  // Default constructor for null item
    explicit ItemReader(ConstItem item);
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
    bool isSymbol() const;
    bool isInt() const;
    bool isFloat() const;
    bool isBool() const;
    bool isElement() const;
    bool isMap() const;
    bool isArray() const;
    bool isList() const;
    bool isDatetime() const;

    // Safe type conversion (returns default-constructed on mismatch)
    String* asString() const;
    String* asSymbol() const;  // Returns the symbol's String* representation
    int64_t asInt() const;
    int32_t asInt32() const;
    double asFloat() const;
    bool asBool() const;
    DateTime asDatetime() const;
    ElementReader asElement() const;  // No pool needed - returns stack-based wrapper
    MapReader asMap() const;
    ArrayReader asArray() const;

    // Convenience
    const char* cstring() const;  // Returns nullptr if not a string

    // Accessors
    Item item() const { return item_; }
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

public:
    // Lifecycle (value type semantics)
    MapReader();  // Default constructor for null map
    explicit MapReader(Map* map);

    // Create from Item with type validation
    static MapReader fromItem(Item item);

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
    bool isValid() const { return map_ != nullptr; }
};

/**
 * Type-safe wrapper for Array access
 * Stack-allocated value type
 */
class ArrayReader {
private:
    Array* array_;

public:
    // Lifecycle (value type semantics)
    ArrayReader();  // Default constructor for null array
    explicit ArrayReader(Array* array);

    // Create from Item with type validation
    static ArrayReader fromItem(Item item);

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
    bool isValid() const { return array_ != nullptr; }
};

/**
 * Pure stack-based element reader (no pool allocation)
 * Stores element pointer and cached metadata directly as members
 */
class ElementReader {
private:
    const Element* element_;         // Underlying element (read-only)
    const TypeElmt* element_type_;   // Cached element type info
    const char* tag_name_;           // Cached tag name (null-terminated)
    int64_t tag_name_len_;           // Tag name length
    int64_t child_count_;            // Number of child items
    int64_t attr_count_;             // Number of attributes

public:
    // Lifecycle
    ElementReader();  // Default constructor for invalid element
    explicit ElementReader(const Element* element);
    explicit ElementReader(Item item);

    ~ElementReader() = default;

    // Copyable and movable (shallow copy - just pointers)
    ElementReader(const ElementReader&) = default;
    ElementReader& operator=(const ElementReader&) = default;
    ElementReader(ElementReader&&) = default;
    ElementReader& operator=(ElementReader&&) = default;

    // Element properties
    const char* tagName() const { return tag_name_; }
    int64_t tagNameLen() const { return tag_name_len_; }
    bool hasTag(const char* tag_name) const;
    int64_t childCount() const { return child_count_; }
    int64_t attrCount() const { return attr_count_; }
    bool isEmpty() const;
    bool isTextOnly() const;

    // Child access (no longer needs pool)
    ItemReader childAt(int64_t index) const;
    ItemReader findChild(const char* tag_name) const;
    void textContent(StringBuf* sb) const;

    // New methods from proposal
    ElementReader findChildElement(const char* tag_name) const;
    bool hasChildElements() const;
    void allText(StringBuf* sb) const;

    // Iteration (no longer needs pool)
    class ChildIterator {
    private:
        const ElementReader* reader_;
        int64_t index_;

    public:
        explicit ChildIterator(const ElementReader* reader);
        bool next(ItemReader* item);
        void reset();
    };

    class ElementChildIterator {
    private:
        const ElementReader* reader_;
        int64_t index_;

    public:
        ElementChildIterator(const ElementReader* reader);
        bool next(ElementReader* elem);
        void reset();
    };

    ChildIterator children() const;
    ElementChildIterator childElements() const;

    // Attribute access (consolidated from AttributeReader)
    bool has_attr(const char* key) const;
    const char* get_attr_string(const char* key) const;
    ItemReader get_attr(const char* key) const;

    // Typed attribute accessors
    String* get_string_attr(const char* attr_name) const;
    int64_t get_int_attr(const char* attr_name, int64_t default_val = 0) const;
    bool get_bool_attr(const char* attr_name, bool default_val = false) const;

    // Accessors
    bool isValid() const { return element_ != nullptr; }
    const Element* element() const { return element_; }
};
