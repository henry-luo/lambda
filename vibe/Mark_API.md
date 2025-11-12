# Mark API Proposal: MarkBuilder and MarkReader

**Version:** 1.1
**Date:** November 13, 2025
**Status:** Implemented

## Executive Summary

This document proposes a new, comprehensive C++-based API for building and reading Lambda's Mark data model. The API consists of two complementary families:

- **MarkBuilder**: A fluent, type-safe C++ class interface for constructing Mark documents in input parsers
- **MarkReader**: A read-only, efficient C++ class interface for traversing and extracting data in formatters

The proposal addresses current pain points in the Lambda codebase:
- Manual memory management and pool allocation scattered across parsers
- Repetitive boilerplate for creating elements, maps, and arrays
- Type-unsafe access to data structures requiring manual type checking
- Inconsistent patterns between different input parsers
- Limited abstraction for document traversal in formatters

**Key Design Decisions:**
- **C++ Classes** with member functions instead of C-style function pointers
- **RAII** for automatic resource management
- **Fluent API** with method chaining for readable construction
- **Type Safety** through strong typing and runtime validation
- **Zero Breaking Changes** to existing data structures

---

## Table of Contents

1. [Background & Motivation](#background--motivation)
2. [Current State Analysis](#current-state-analysis)
3. [Design Principles](#design-principles)
4. [MarkBuilder API](#markbuilder-api)
5. [MarkReader API Family](#markreader-api-family)
6. [Implementation Plan](#implementation-plan)
7. [Migration Strategy](#migration-strategy)
8. [Examples & Use Cases](#examples--use-cases)
9. [Performance Considerations](#performance-considerations)
10. [Future Extensions](#future-extensions)

---

## Background & Motivation

### What is Mark?

Mark is Lambda's static data notation, analogous to JSON in JavaScript. It represents structured documents with:
- **Primitives**: strings, numbers, booleans, dates, symbols
- **Collections**: lists, arrays, maps
- **Elements**: tagged structured data with attributes and children (like DOM nodes)

Mark serves as the intermediate representation for all document formats processed by Lambda's input parsers (JSON, XML, HTML, Markdown, YAML, LaTeX, etc.) and output formatters.

### Current Challenges

#### Input Parser Issues

Current parsers directly manipulate low-level data structures:

```cpp
// Verbose element creation
Element* element = elmt_pooled(input->pool);
TypeElmt* element_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
element->type = element_type;
arraylist_append(input->type_list, element_type);
element_type->type_index = input->type_list->length - 1;
String* tag_name = input_create_string(input, "div");
StrView* nv = (StrView*)pool_calloc(input->pool, sizeof(StrView));
nv->str = tag_name->chars;
nv->length = tag_name->len;
element_type->name = *nv;

// Manual map construction with shape management
map_put(mp, key, value, input);  // Manages TypeMap, ShapeEntry, data packing
```

**Problems:**
- 10+ lines of boilerplate per element
- Pool allocation scattered everywhere
- Type management is manual and error-prone
- No fluent chaining or builder pattern
- Inconsistent patterns across 30+ parsers

#### Formatter Issues

Formatters manually check types and traverse structures:

```cpp
TypeId type = get_type_id(item);
if (type == LMD_TYPE_ELEMENT) {
    Element* elem = item.element;
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    const char* tag = elem_type->name.str;

    // Traverse children
    for (int i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId child_type = get_type_id(child);
        // More manual type checking...
    }
}
```

**Problems:**
- Repetitive type checking boilerplate
- Unsafe pointer casts
- No abstraction for common traversal patterns
- Limited existing ElementReader adoption

---

## Current State Analysis

### Input Parsers (`lambda/input/`)

**Analyzed 30+ input parsers including:**
- `input-json.cpp`, `input-xml.cpp`, `input-yaml.cpp`
- `input-html.cpp`, `input-markup.cpp`, `input-latex.cpp`
- `input-pdf.cpp`, `input-rtf.cpp`, `input-csv.cpp`

**Common Patterns:**
1. **String Creation**: `create_input_string()` or `input_create_string()`
2. **Element Creation**: `input_create_element(input, "tag")`
3. **Attribute Setting**: `input_add_attribute_to_element()` or direct `map_put()`
4. **Array Construction**: `array_pooled()` + `array_append()`
5. **Map Construction**: `map_pooled()` + `map_put()`
6. **Pool Allocation**: `pool_alloc()`, `pool_calloc()` everywhere

**Observations:**
- Every parser implements similar construction logic
- Pool management is manual and verbose
- No type safety at construction time
- Reference counting is inconsistent

### Formatters (`lambda/format/`)

**Analyzed 20+ formatters including:**
- `format-json.cpp`, `format-xml.cpp`, `format-html.cpp`
- `format-markdown.cpp`, `format-latex.cpp`, `format-yaml.cpp`

**Common Patterns:**
1. **Type Checking**: `get_type_id(item)` + switch/if statements
2. **Element Traversal**: Manual loops over `element->items[]`
3. **Map Iteration**: Walking `ShapeEntry` linked lists
4. **String Extraction**: Direct `String*` pointer access
5. **Recursive Descent**: Manual recursion for tree traversal

**Observations:**
- ElementReader exists but underutilized (only in `element_reader.cpp`)
- Most formatters do manual traversal
- Type-unsafe pointer manipulation
- Repetitive traversal patterns

### Memory Management

**Pool System** (`lib/mempool.h`):
- Arena-based allocation using rpmalloc
- `pool_create()`, `pool_alloc()`, `pool_calloc()`, `pool_free()`
- Pool destroyed in bulk (efficient)
- Used extensively but manually

**String Management**:
- Custom `String` type with `ref_cnt` (up to 1024 refs)
- `EMPTY_STRING` singleton for empty strings
- String deduplication through NamePool (optional)

**Type System**:
- `TypeId` enum for runtime type identification
- `TypeInfo` table with size/name metadata
- Type shapes (`ShapeEntry`) for structured data
- `ArrayList* type_list` tracks all types in Input

---

## Design Principles

### 1. **Fluent & Ergonomic**
- Builder pattern with method chaining
- Minimal boilerplate for common tasks
- Readable, self-documenting code

### 2. **Memory Safe**
- Automatic pool management
- Reference counting handled automatically
- No manual pointer arithmetic

### 3. **Type Safe**
- Strong typing at API boundaries
- Compile-time type checking where possible
- Runtime type validation with clear errors

### 4. **Performance First**
- Zero-copy where possible
- Efficient pool allocation
- Minimal overhead over manual code

### 5. **Consistent**
- Uniform API across all parsers and formatters
- Predictable naming conventions (camelCase for methods)
- Standard patterns for common operations

### 6. **C++ Best Practices**
- RAII for resource management (constructors/destructors)
- Move semantics where appropriate
- Deleted copy constructors to prevent accidents
- Member functions instead of free functions
- Modern C++ idioms (initializer lists, nullptr, auto)

### 7. **Backward Compatible**
- Gradual migration path
- Interoperable with existing code
- No breaking changes to core data structures

---

## MarkBuilder API

### Overview

MarkBuilder provides a fluent interface for constructing Mark documents in input parsers. It manages:
- Pool allocation and lifetime
- Type system management
- Reference counting
- String interning (optional)
- Input context management

### Core Classes

```cpp
// Forward declarations
class ElementBuilder;
class MapBuilder;
class ArrayBuilder;

/**
 * Main builder context for constructing Mark documents
 *
 * MEMORY MODEL:
 * - MarkBuilder itself is STACK-ALLOCATED in the parser function scope
 * - Automatically destroyed when parser function exits (RAII)
 * - Only the FINAL Mark data (Element, Map, Array, String) is pool-allocated
 * - Pool allocation happens only when build() is called
 * - No manual memory management needed for the builder
 *
 * USAGE PATTERN:
 *   void parse_json(Input* input, const char* json) {
 *       MarkBuilder builder(input);  // Stack allocation
 *       Item result = builder.map()  // Returns MapBuilder by value
 *           .put("key", "value")     // Fluent chaining by reference
 *           .build();                // Pool-allocates final Map
 *       input->root = result;
 *   }  // builder automatically destroyed here
 */
class MarkBuilder {
private:
    Input* input_;              // Input context (pool, type_list, name_pool, etc.)
    Pool* pool_;                // Memory pool (cached from input) - for Mark data only
    NamePool* name_pool_;       // String interning pool
    ArrayList* type_list_;      // Type registry
    StringBuf* sb_;             // Shared string buffer for temp work

    // Builder state (stack-allocated, transient)
    Element* current_element_;  // Current element being built (for nesting)
    Map* current_map_;          // Current map being built
    Array* current_array_;      // Current array being built

    // Configuration
    bool auto_string_merge_;    // Auto-merge adjacent strings in lists
    bool intern_strings_;       // Use name pool for string deduplication

public:
    // Lifecycle (stack-based RAII)
    explicit MarkBuilder(Input* input);
    ~MarkBuilder();  // Auto cleanup when out of scope

    // Disable copy and move (stack-only, no transfer)
    MarkBuilder(const MarkBuilder&) = delete;
    MarkBuilder& operator=(const MarkBuilder&) = delete;
    MarkBuilder(MarkBuilder&&) = delete;
    MarkBuilder& operator=(MarkBuilder&&) = delete;

    // Finalization
    Item finalize();

    // String creation (pool-allocated for final Mark data)
    String* createString(const char* str);
    String* createString(const char* str, size_t len);
    String* createStringFromBuf(StringBuf* sb);
    static String* emptyString();

    // Sub-builder creation (returns stack references, not heap allocated)
    // These are lightweight wrappers that borrow the MarkBuilder context
    ElementBuilder element(const char* tag_name);
    MapBuilder map();
    ArrayBuilder array();

    // Direct Item creation (convenience - pool-allocates final data)
    Item createElement(const char* tag_name);
    Item createMap();
    Item createArray();
    Item createStringItem(const char* str);
    Item createInt(int64_t value);
    Item createFloat(double value);
    Item createBool(bool value);
    Item createNull();

    // Configuration
    void setAutoStringMerge(bool enabled) { auto_string_merge_ = enabled; }
    void setInternStrings(bool enabled) { intern_strings_ = enabled; }

    // Accessors
    Pool* pool() const { return pool_; }
    Input* input() const { return input_; }
};

/**
 * Fluent builder for Element construction
 *
 * MEMORY MODEL:
 * - ElementBuilder is VALUE TYPE, returned by value from MarkBuilder
 * - Lives on the STACK within the calling scope
 * - No heap allocation for the builder itself
 * - Only the final Element is pool-allocated when build() is called
 *
 * USAGE PATTERN:
 *   ElementBuilder elem = builder.element("div");
 *   elem.attr("class", "container").text("Hello");
 *   Item result = elem.build();  // Pool allocates Element here
 */
class ElementBuilder {
private:
    MarkBuilder* builder_;      // Non-owning reference to parent builder
    Element* element_;          // Work-in-progress (pool-allocated incrementally)
    TypeElmt* element_type_;    // Type metadata (pool-allocated)
    ElementBuilder* parent_;    // For nested elements (stack reference)

public:
    explicit ElementBuilder(MarkBuilder* builder, const char* tag_name);
    ~ElementBuilder() = default;  // Stack cleanup only

    // Value type - can be copied/moved on stack
    ElementBuilder(const ElementBuilder&) = default;
    ElementBuilder& operator=(const ElementBuilder&) = default;
    ElementBuilder(ElementBuilder&&) = default;
    ElementBuilder& operator=(ElementBuilder&&) = default;

    // Attribute setters (fluent interface - returns reference for chaining)
    ElementBuilder& attr(const char* key, Item value);
    ElementBuilder& attr(const char* key, const char* value);
    ElementBuilder& attr(const char* key, int64_t value);
    ElementBuilder& attr(const char* key, double value);
    ElementBuilder& attr(const char* key, bool value);

    // Child management (fluent interface - returns reference for chaining)
    ElementBuilder& child(Item child);
    ElementBuilder& text(const char* text);
    ElementBuilder& text(const char* text, size_t len);
    ElementBuilder& children(std::initializer_list<Item> items);

    // Nested element building
    ElementBuilder beginChild(const char* tag_name);
    ElementBuilder& end();

    // Finalization (pool-allocates final Element)
    Item build();

    // Accessors
    Element* element() const { return element_; }
    MarkBuilder* builder() const { return builder_; }
};

/**
 * Fluent builder for Map construction
 *
 * MEMORY MODEL:
 * - MapBuilder is VALUE TYPE, returned by value from MarkBuilder
 * - Lives on the STACK within the calling scope
 * - Only the final Map is pool-allocated when build() is called
 */
class MapBuilder {
private:
    MarkBuilder* builder_;      // Non-owning reference
    Map* map_;                  // Work-in-progress (pool-allocated)
    TypeMap* map_type_;         // Type metadata (pool-allocated)

public:
    explicit MapBuilder(MarkBuilder* builder);
    ~MapBuilder() = default;  // Stack cleanup only

    // Value type - can be copied/moved on stack
    MapBuilder(const MapBuilder&) = default;
    MapBuilder& operator=(const MapBuilder&) = default;
    MapBuilder(MapBuilder&&) = default;
    MapBuilder& operator=(MapBuilder&&) = default;

    // Key-value setters (fluent interface - returns reference for chaining)
    MapBuilder& put(const char* key, Item value);
    MapBuilder& put(const char* key, const char* value);
    MapBuilder& put(const char* key, int64_t value);
    MapBuilder& put(const char* key, double value);
    MapBuilder& put(const char* key, bool value);
    MapBuilder& putNull(const char* key);

    // Finalization (pool-allocates final Map)
    Item build();

    // Accessors
    Map* map() const { return map_; }
    MarkBuilder* builder() const { return builder_; }
};

/**
 * Fluent builder for Array construction
 *
 * MEMORY MODEL:
 * - ArrayBuilder is VALUE TYPE, returned by value from MarkBuilder
 * - Lives on the STACK within the calling scope
 * - Only the final Array is pool-allocated when build() is called
 */
class ArrayBuilder {
private:
    MarkBuilder* builder_;      // Non-owning reference
    Array* array_;              // Work-in-progress (pool-allocated)

public:
    explicit ArrayBuilder(MarkBuilder* builder);
    ~ArrayBuilder() = default;  // Stack cleanup only

    // Value type - can be copied/moved on stack
    ArrayBuilder(const ArrayBuilder&) = default;
    ArrayBuilder& operator=(const ArrayBuilder&) = default;
    ArrayBuilder(ArrayBuilder&&) = default;
    ArrayBuilder& operator=(ArrayBuilder&&) = default;

    // Append operations (fluent interface - returns reference for chaining)
    ArrayBuilder& append(Item item);
    ArrayBuilder& append(const char* str);
    ArrayBuilder& append(int64_t value);
    ArrayBuilder& append(double value);
    ArrayBuilder& append(bool value);
    ArrayBuilder& appendItems(std::initializer_list<Item> items);

    // Finalization (pool-allocates final Array)
    Item build();

    // Accessors
    Array* array() const { return array_; }
    MarkBuilder* builder() const { return builder_; }
};
```

### MarkBuilder Class API

The MarkBuilder API uses C++ classes with member functions. All builders are **stack-allocated value types** with automatic lifetime management. Only the final Mark data (Element, Map, Array, String) is allocated from the Input's memory pool.

**Memory Model:**
```cpp
void parse_document(Input* input, const char* data) {
    MarkBuilder builder(input);     // Stack-allocated, auto-destroyed at scope exit

    // Sub-builders are also stack-allocated (returned by value)
    ElementBuilder elem = builder.element("doc");  // Stack value
    elem.attr("version", "1.0");

    Item result = elem.build();     // Pool-allocates final Element here
    input->root = result;
}  // builder, elem automatically destroyed (stack unwind)
```

```cpp
// ==============================================================================
// MarkBuilder Class - Main Document Builder
// ==============================================================================

/**
 * Create a new MarkBuilder from Input context
 * Stack-allocated, inherits pool, type_list, name_pool from Input
 *
 * USAGE:
 *   MarkBuilder builder(input);  // Stack allocation
 */
MarkBuilder::MarkBuilder(Input* input);

/**
 * Destructor - automatically cleans up when out of scope
 * Only cleans up builder state, not the pool-allocated Mark data
 */
MarkBuilder::~MarkBuilder();

// String Creation Methods (pool-allocates strings for final Mark data)
String* MarkBuilder::createString(const char* str);
String* MarkBuilder::createString(const char* str, size_t len);
String* MarkBuilder::createStringFromBuf(StringBuf* sb);
static String* MarkBuilder::emptyString();

// Builder Creation Methods (returns stack-allocated value types)
ElementBuilder MarkBuilder::element(const char* tag_name);
MapBuilder MarkBuilder::map();
ArrayBuilder MarkBuilder::array();

// Direct Item Creation (convenience - pool-allocates final data immediately)
Item MarkBuilder::createElement(const char* tag_name);
Item MarkBuilder::createMap();
Item MarkBuilder::createArray();
Item MarkBuilder::createStringItem(const char* str);
Item MarkBuilder::createInt(int64_t value);
Item MarkBuilder::createFloat(double value);
Item MarkBuilder::createBool(bool value);
Item MarkBuilder::createNull();

// Configuration Methods
void MarkBuilder::setAutoStringMerge(bool enabled);
void MarkBuilder::setInternStrings(bool enabled);

// ==============================================================================
// ElementBuilder Class - Fluent Element Construction
// ==============================================================================

/**
 * Construct element builder for given tag name
 * Returned by value - stack-allocated
 */
ElementBuilder::ElementBuilder(MarkBuilder* builder, const char* tag_name);

// Attribute Setters (return reference for chaining)
ElementBuilder& ElementBuilder::attr(const char* key, Item value);
ElementBuilder& ElementBuilder::attr(const char* key, const char* value);
ElementBuilder& ElementBuilder::attr(const char* key, int64_t value);
ElementBuilder& ElementBuilder::attr(const char* key, double value);
ElementBuilder& ElementBuilder::attr(const char* key, bool value);

// Child Management (return reference for chaining)
ElementBuilder& ElementBuilder::child(Item child);
ElementBuilder& ElementBuilder::text(const char* text);
ElementBuilder& ElementBuilder::text(const char* text, size_t len);
ElementBuilder& ElementBuilder::children(std::initializer_list<Item> items);

// Nested Element Building
ElementBuilder ElementBuilder::beginChild(const char* tag_name);
ElementBuilder& ElementBuilder::end();

// Finalization (pool-allocates and returns final Element)
Item ElementBuilder::build();

// ==============================================================================
// MapBuilder Class - Fluent Map Construction
// ==============================================================================

/**
 * Construct map builder
 * Returned by value - stack-allocated
 */
MapBuilder::MapBuilder(MarkBuilder* builder);

// Key-Value Setters (return reference for chaining)
MapBuilder& MapBuilder::put(const char* key, Item value);
MapBuilder& MapBuilder::put(const char* key, const char* value);
MapBuilder& MapBuilder::put(const char* key, int64_t value);
MapBuilder& MapBuilder::put(const char* key, double value);
MapBuilder& MapBuilder::put(const char* key, bool value);
MapBuilder& MapBuilder::putNull(const char* key);

// Finalization (pool-allocates and returns final Map)
Item MapBuilder::build();

// ==============================================================================
// ArrayBuilder Class - Fluent Array Construction
// ==============================================================================

/**
 * Construct array builder
 * Returned by value - stack-allocated
 */
ArrayBuilder::ArrayBuilder(MarkBuilder* builder);

// Append Operations (return reference for chaining)
ArrayBuilder& ArrayBuilder::append(Item item);
ArrayBuilder& ArrayBuilder::append(const char* str);
ArrayBuilder& ArrayBuilder::append(int64_t value);
ArrayBuilder& ArrayBuilder::append(double value);
ArrayBuilder& ArrayBuilder::append(bool value);
ArrayBuilder& ArrayBuilder::appendItems(std::initializer_list<Item> items);

// Finalization (pool-allocates and returns final Array)
Item ArrayBuilder::build();
```

### Usage Examples

#### Example 1: Simple Element Creation

```cpp
// Old way (10+ lines boilerplate with manual pool allocation)
Element* element = elmt_pooled(input->pool);
TypeElmt* element_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
element->type = element_type;
arraylist_append(input->type_list, element_type);
element_type->type_index = input->type_list->length - 1;
// ... more boilerplate for tag name, attributes, children

// New way (fluent chain with stack-allocated builder)
MarkBuilder builder(input);  // Stack-allocated
Item elem = builder.element("div")
    .attr("class", "container")  // Returns reference
    .text("Hello, World!")
    .build();  // Pool-allocates final Element
// builder automatically destroyed when scope ends
```

#### Example 2: Nested Document Structure

```cpp
MarkBuilder builder(input);  // Stack-allocated builder

// All intermediate builders are stack-allocated
// Only final Item is pool-allocated
Item doc = builder.element("html")
    .beginChild("head")
        .child(
            builder.element("title")
                .text("My Document")
                .build()
        )
        .end()
    .beginChild("body")
        .child(
            builder.element("h1")
                .attr("id", "title")
                .text("Welcome")
                .build()
        )
        .end()
    .build();
// All builders cleaned up automatically
```

#### Example 3: JSON-like Map Construction

```cpp
MarkBuilder builder(input);  // Stack-allocated

// MapBuilder returned by value, lives on stack
Item json_obj = builder.map()
    .put("name", "John Doe")
    .put("count", 42)
    .put("active", true)
    .build();  // Pool-allocates final Map
```

#### Example 4: Array Building

```cpp
MarkBuilder builder(input);  // Stack-allocated

// ArrayBuilder returned by value, lives on stack
Item arr = builder.array()
    .append("apple")
    .append("banana")
    .append("cherry")
    .build();  // Pool-allocates final Array
```

#### Example 5: Practical XML Parser

```cpp
void parse_xml_element(MarkBuilder& builder, const char** xml) {
    // Parse opening tag
    String* tag_name = parse_tag_name(xml);

    // ElementBuilder lives on stack
    ElementBuilder elem = builder.element(tag_name->chars);

    // Parse attributes
    while (parse_attribute(xml, &attr_name, &attr_value)) {
        elem.attr(attr_name, attr_value);  // Chaining by reference
    }

    // Parse children
    while (!is_closing_tag(*xml)) {
        if (is_text(*xml)) {
            char* text = parse_text_content(xml);
            elem.text(text);
        } else if (is_opening_tag(*xml)) {
            // Recursive element parsing
            parse_xml_element(builder, xml);
            // Child automatically added to current element
        }
    }

    Item result = elem.build();  // Pool-allocates final Element
    // elem destroyed when function returns
}
```

---

## MarkReader API Family

### Overview

MarkReader provides a read-only, type-safe interface for traversing and extracting data from Mark documents in formatters. It consists of several reader components:

- **MarkReader**: Root document reader with iteration and query capabilities
- **ItemReader**: Type-safe wrapper for individual Items
- **ElementReader**: Stack-based element traversal with integrated attribute access (no pool required)
- **MapReader**: Stack-based map traversal (no pool required)
- **ArrayReader**: Stack-based array traversal (no pool required)

**All readers are completely pool-free for traversal operations.** Pool is only needed when extracting text content into a StringBuf.

### Architecture

```
MarkReader (Document Root - stack-allocated, NO POOL)
    ├── MarkReader(Item root)  // Constructor - no pool needed
    ├── getRoot() → ItemReader (by value)
    ├── findAll(selector) → Iterator<ItemReader>
    └── root() → Item

ItemReader (Type-Safe Item Wrapper - stack-allocated, NO POOL)
    ├── ItemReader(Item item)  // Constructor - no pool needed
    ├── getType() → TypeId
    ├── asString() → String*
    ├── asInt() → int64_t
    ├── asElement() → ElementReader (by value)
    ├── asMap() → MapReader (by value)
    ├── asArray() → ArrayReader (by value)
    └── isNull() → bool

ElementReader (Element-Specific - stack-allocated, NO POOL)
    ├── ElementReader(const Element*)  // Constructor - no pool needed
    ├── tagName() → const char*
    ├── attrCount() → int64_t
    ├── childCount() → int64_t
    ├── childAt(index) → ItemReader (by value)
    ├── children() → Iterator<ItemReader>  // No pool needed
    ├── findChild(tag) → ItemReader (by value)
    ├── textContent(StringBuf*) → void  // Pool only for StringBuf
    ├── has_attr(key) → bool  // Attribute access (consolidated)
    ├── get_attr_string(key) → const char*  // Attribute access
    ├── get_attr(key) → ItemReader  // Attribute access
    └── isEmpty() → bool

MapReader (Map Traversal - stack-allocated, NO POOL)
    ├── MapReader(Map* map)  // Constructor - no pool needed
    ├── get(key) → ItemReader (by value)
    ├── has(key) → bool
    ├── keys() → KeyIterator
    ├── entries() → EntryIterator
    └── size() → int64_t

ArrayReader (Array Traversal - stack-allocated, NO POOL)
    ├── ArrayReader(Array* array)  // Constructor - no pool needed
    ├── get(index) → ItemReader (by value)
    ├── length() → int64_t
    └── items() → Iterator<ItemReader>
```

### Core Reader Classes

```cpp
// ==============================================================================
// MarkReader - Document Root Reader
// ==============================================================================

/**
 * MEMORY MODEL: Stack-allocated value type, POOL-FREE
 * - Lightweight wrapper around Item root
 * - No heap allocation for the reader itself
 * - NO POOL NEEDED - completely pool-free for traversal
 * - Automatic cleanup when scope ends (RAII)
 */
class MarkReader {
private:
    Item root_;

public:
    /**
     * Create reader from root Item
     * @param root The root Item to read from
     * NO POOL PARAMETER - completely pool-free!
     */
    explicit MarkReader(Item root);
    ~MarkReader() = default;
    MarkReader(const MarkReader&) = default;
    MarkReader& operator=(const MarkReader&) = default;
    MarkReader(MarkReader&&) = default;
    MarkReader& operator=(MarkReader&&) = default;

    /**
     * Get root item as ItemReader (returned by value)
     */
    ItemReader getRoot() const;

    /**
     * Find all elements matching selector (simple tag name for now)
     * Returns iterator over ElementReader instances
     */
    ElementIterator findAll(const char* selector) const;

    // Accessors
    Item root() const { return root_; }
};

// ==============================================================================
// ItemReader - Type-Safe Item Wrapper
// ==============================================================================

/**
 * MEMORY MODEL: Stack-allocated value type, POOL-FREE
 * - Lightweight wrapper (16 bytes: Item + TypeId)
 * - No heap allocation
 * - NO POOL NEEDED - completely pool-free!
 */
class ItemReader {
private:
    Item item_;
    TypeId cached_type_;

public:
    /**
     * Create reader from Item
     * @param item The Item to read
     * NO POOL PARAMETER - pool-free!
     */
    ItemReader();  // Default constructor for null item
    explicit ItemReader(Item item);
    ~ItemReader() = default;
    ItemReader(const ItemReader&) = default;
    ItemReader& operator=(const ItemReader&) = default;
    ItemReader(ItemReader&&) = default;
    ItemReader& operator=(ItemReader&&) = default;

    /**
     * Get the type of the item
     */
    TypeId getType() const { return cached_type_; }

    /**
     * Type checking predicates
     */
    bool isNull() const;
    bool isString() const;
    bool isInt() const;
    bool isFloat() const;
    bool isBool() const;
    bool isElement() const;
    bool isMap() const;
    bool isArray() const;
    bool isList() const;

    /**
     * Safe type conversion (returns by value for readers)
     * Returns default-constructed reader if type mismatch
     */
    String* asString() const;
    int64_t asInt() const;
    int32_t asInt32() const;
    double asFloat() const;
    bool asBool() const;
    ElementReader asElement() const;  // Returns by value
    MapReader asMap() const;
    ArrayReader asArray() const;

    /**
     * Convenience: Get C string directly (returns nullptr if not a string)
     */
    const char* cstring() const;

    // Accessors
    Item item() const { return item_; }
};

// ==============================================================================
// ElementReader - Stack-Based Element Reader (NO POOL)
// ==============================================================================

/**
 * MEMORY MODEL: Stack-allocated value type, POOL-FREE
 * - Wraps Element* pointer
 * - Caches element metadata (tag name, child count, etc.)
 * - NO POOL STORED - completely pool-free for traversal
 * - Attributes consolidated into ElementReader (no separate AttributeReader)
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
    // Constructors - NO POOL PARAMETER
    ElementReader();  // Default constructor for invalid element
    explicit ElementReader(const Element* element);
    explicit ElementReader(Item item);

    ~ElementReader() = default;

    // Copyable and movable (shallow copy - just pointers)
    ElementReader(const ElementReader&) = default;
    ElementReader& operator=(const ElementReader&) = default;
    ElementReader(ElementReader&&) = default;
    ElementReader& operator=(ElementReader&&) = default;

    /**
     * Element properties
     */
    const char* tagName() const { return tag_name_; }
    int64_t tagNameLen() const { return tag_name_len_; }
    bool hasTag(const char* tag_name) const;
    int64_t childCount() const { return child_count_; }
    int64_t attrCount() const { return attr_count_; }
    bool isEmpty() const;
    bool isTextOnly() const;

    /**
     * Child access - NO POOL NEEDED
     */
    ItemReader childAt(int64_t index) const;
    ItemReader findChild(const char* tag_name) const;

    /**
     * Text extraction - ONLY operation that needs external StringBuf
     * Caller must provide a StringBuf for text accumulation
     */
    void textContent(StringBuf* sb) const;  // Caller provides StringBuf
    void allText(StringBuf* sb) const;      // Alias for textContent

    /**
     * Get child element by tag name (first match)
     * Returns by value
     */
    ElementReader findChildElement(const char* tag_name) const;

    /**
     * Check if element has any child elements (not just text)
     */
    bool hasChildElements() const;

    /**
     * Attribute access - CONSOLIDATED INTO ElementReader
     * No separate AttributeReader needed!
     */
    bool has_attr(const char* key) const;
    const char* get_attr_string(const char* key) const;
    ItemReader get_attr(const char* key) const;

    /**
     * Iteration - NO POOL NEEDED
     */
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

    ChildIterator children() const;         // NO POOL PARAMETER
    ElementChildIterator childElements() const;

    // Accessors
    bool isValid() const { return element_ != nullptr; }
    const Element* element() const { return element_; }
};
     */
    bool has(const char* key) const;
    const char* getString(const char* key) const;
    ItemReader getItem(const char* key) const;

    /**
     * Get attribute with default value
     */
    const char* getStringOr(const char* key, const char* default_value) const;
    // Accessors
    bool isValid() const { return element_ != nullptr; }
    const Element* element() const { return element_; }
};

// ==============================================================================
// MapReader - Map Traversal (NO POOL)
// ==============================================================================

/**
 * MEMORY MODEL: Stack-allocated value type, POOL-FREE
 * - Wraps Map* and TypeMap* pointers
 * - NO POOL STORED - completely pool-free
 */
class MapReader {
private:
    Map* map_;
    TypeMap* map_type_;

public:
    /**
     * Create from Map pointer
     * @param map Map to read from
     * NO POOL PARAMETER - pool-free!
     */
    MapReader();  // Default constructor for null map
    explicit MapReader(Map* map);

    /**
     * Create MapReader from Item (validates type)
     * Returns by value
     */
    static MapReader fromItem(Item item);

    ~MapReader() = default;
    MapReader(const MapReader&) = default;
    MapReader& operator=(const MapReader&) = default;
    MapReader(MapReader&&) = default;
    MapReader& operator=(MapReader&&) = default;

    /**
     * Get value by key as ItemReader (returned by value)
     * NO POOL NEEDED
     */
    ItemReader get(const char* key) const;

    /**
     * Check if key exists
     */
    bool has(const char* key) const;

    /**
     * Get map size (number of entries)
     */
    int64_t size() const;
    bool isEmpty() const { return size() == 0; }

    /**
     * Iterator classes for map traversal - NO POOL NEEDED
     */
    class KeyIterator {
    private:
        const MapReader* reader_;
        ShapeEntry* current_field_;
    public:
        explicit KeyIterator(const MapReader* reader);
        bool next(const char** key);
        void reset();
    };

    class ValueIterator {
    private:
        const MapReader* reader_;
        ShapeEntry* current_field_;
    public:
        explicit ValueIterator(const MapReader* reader);
        bool next(ItemReader* value);
        void reset();
    };

    class EntryIterator {
    private:
        const MapReader* reader_;
        ShapeEntry* current_field_;
    public:
        explicit EntryIterator(const MapReader* reader);
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

// ==============================================================================
// ArrayReader - Array Traversal (NO POOL)
// ==============================================================================

/**
 * MEMORY MODEL: Stack-allocated value type, POOL-FREE
 * - Wraps Array* pointer
 * - NO POOL STORED - completely pool-free
 */
class ArrayReader {
private:
    Array* array_;

public:
    /**
     * Create from Array pointer
     * @param array Array to read from
     * NO POOL PARAMETER - pool-free!
     */
    ArrayReader();  // Default constructor for null array
    explicit ArrayReader(Array* array);

    /**
     * Create from Item with type validation
     */
    static ArrayReader fromItem(Item item);

    ~ArrayReader() = default;
    ArrayReader(const ArrayReader&) = default;
    ArrayReader& operator=(const ArrayReader&) = default;
    ArrayReader(ArrayReader&&) = default;
    ArrayReader& operator=(ArrayReader&&) = default;

    /**
     * Array access - NO POOL NEEDED
     */
    ItemReader get(int64_t index) const;
    int64_t length() const;
    bool isEmpty() const { return length() == 0; }

    /**
     * Iterator for array traversal - NO POOL NEEDED
     */
    class Iterator {
    private:
        const ArrayReader* reader_;
        int64_t index_;
    public:
        explicit Iterator(const ArrayReader* reader);
        bool next(ItemReader* item);
        void reset();
        int64_t currentIndex() const { return index_; }
    };

    Iterator items() const;

    // Accessors
    Array* array() const { return array_; }
    bool isValid() const { return array_ != nullptr; }
};

// ==============================================================================
// ArrayReader - Array Traversal
// ==============================================================================

/**
 * MEMORY MODEL: Stack-allocated value type, POOL-FREE
 * - Wraps Array* pointer
 * - NO POOL STORED - completely pool-free
 */
class ArrayReader {
private:
    Array* array_;

public:
    /**
     * Create from Array pointer
     * @param array Array to read from
     * NO POOL PARAMETER - pool-free!
     */
    ArrayReader();  // Default constructor for null array
    explicit ArrayReader(Array* array);

    /**
     * Create from Item with type validation
     */
    static ArrayReader fromItem(Item item);

    ~ArrayReader() = default;
    ArrayReader(const ArrayReader&) = default;
    ArrayReader& operator=(const ArrayReader&) = default;
    ArrayReader(ArrayReader&&) = default;
    ArrayReader& operator=(ArrayReader&&) = default;

    /**
     * Array access - NO POOL NEEDED
     */
    ItemReader get(int64_t index) const;
    int64_t length() const;
    bool isEmpty() const { return length() == 0; }

    /**
     * Iterator for array traversal - NO POOL NEEDED
     */
    class Iterator {
    private:
        const ArrayReader* reader_;
        int64_t index_;
    public:
        explicit Iterator(const ArrayReader* reader);
        bool next(ItemReader* item);
        void reset();
        int64_t currentIndex() const { return index_; }
    };

    Iterator items() const;

    // Accessors
    Array* array() const { return array_; }
    bool isValid() const { return array_ != nullptr; }
};
```

### Usage Examples

#### Example 1: Safe Type Checking and Conversion

```cpp
// Old way
TypeId type = get_type_id(item);
if (type == LMD_TYPE_STRING) {
    String* str = (String*)item.pointer;
    // Use str
}

// New way (reader is stack-allocated, NO POOL NEEDED)
ItemReader reader(item);  // No pool parameter!
if (reader.isString()) {
    const char* str = reader.cstring();
    // Use str
}
```

#### Example 2: Element Traversal

```cpp
// Old way (manual iteration with type checking)
Element* elem = item.element;
for (int i = 0; i < elem->length; i++) {
    Item child = elem->items[i];
    TypeId child_type = get_type_id(child);
    if (child_type == LMD_TYPE_ELEMENT) {
        Element* child_elem = child.element;
        // Process child_elem
    }
}

// New way (stack-allocated readers, NO POOL)
ItemReader reader(item);  // No pool
ElementReader elem = reader.asElement();
ElementReader::ElementChildIterator iter = elem.childElements();  // No pool!
ElementReader child;
while (iter.next(&child)) {
    // Process child (stack-allocated)
}
// All readers automatically cleaned up
```

#### Example 3: Map Access

```cpp
// Old way (manual shape traversal)
Map* map = item.map;
TypeMap* map_type = (TypeMap*)map->type;
ShapeEntry* field = map_type->shape;
while (field) {
    if (strcmp(field->name->str, "name") == 0) {
        void* data = ((char*)map->data) + field->byte_offset;
        String* str = *(String**)data;
        // Use str
    }
    field = field->next;
}

// New way (stack-allocated readers, NO POOL)
MapReader map = MapReader::fromItem(item);  // No pool
ItemReader value = map.get("name");  // Returned by value
if (value.isString()) {
    const char* str = value.cstring();
    // Use str
}
// Readers automatically cleaned up
```

#### Example 4: Practical JSON Formatter (POOL-FREE)

```cpp
void format_json_value(StringBuf* sb, const ItemReader& reader) {
    if (reader.isNull()) {
        stringbuf_append_str(sb, "null");
    } else if (reader.isBool()) {
        stringbuf_append_str(sb, reader.asBool() ? "true" : "false");
    } else if (reader.isInt()) {
        stringbuf_append_format(sb, "%" PRId64, reader.asInt());
    } else if (reader.isFloat()) {
        stringbuf_append_format(sb, "%g", reader.asFloat());
    } else if (reader.isString()) {
        format_json_string(sb, reader.cstring());
    } else if (reader.isArray()) {
        // ArrayReader returned by value, lives on stack - NO POOL NEEDED
        ArrayReader arr = reader.asArray();
        stringbuf_append_char(sb, '[');
        ArrayReader::Iterator iter = arr.items();
        ItemReader item;
        bool first = true;
        while (iter.next(&item)) {
            if (!first) stringbuf_append_char(sb, ',');
            format_json_value(sb, item);
            first = false;
        }
        stringbuf_append_char(sb, ']');
    } else if (reader.isMap()) {
        // MapReader returned by value, lives on stack
        MapReader map = reader.asMap();
        stringbuf_append_char(sb, '{');
        MapReader::EntryIterator iter = map.entries();
        const char* key;
        ItemReader value;
        bool first = true;
        while (iter.next(&key, &value)) {
            if (!first) stringbuf_append_char(sb, ',');
            stringbuf_append_format(sb, "\"%s\":", key);
            format_json_value(sb, value);
            first = false;
        }
        stringbuf_append_char(sb, '}');
    }
}
```

#### Example 5: HTML Formatter with ElementReader

```cpp
void format_html_element(StringBuf* sb, const ElementReader& elem) {
    const char* tag = elem.tagName();

    // Opening tag
    stringbuf_append_format(sb, "<%s", tag);

    // Attributes - directly accessed from ElementReader
    if (elem.attrCount() > 0) {
        const TypeMap* map_type = (const TypeMap*)elem.element()->type;
        const ShapeEntry* field = map_type->shape;

        while (field) {
            const char* attr_name = field->name->str;
            ItemReader attr_value = elem.get_attr(attr_name);

            stringbuf_append_format(sb, " %s=\"", attr_name);
            format_html_string(sb, attr_value.cstring());
            stringbuf_append_char(sb, '"');

            field = field->next;
        }
    }

    stringbuf_append_char(sb, '>');

    // Children
    ElementReader::ChildIterator child_iter = elem.children();
    ItemReader child;
    while (child_iter.next(&child)) {
        if (child.isString()) {
            format_html_string(sb, child.cstring());
        } else if (child.isElement()) {
            ElementReader child_elem = child.asElement();  // By value
            format_html_element(sb, child_elem);
        }
    }

    // Closing tag
    stringbuf_append_format(sb, "</%s>", tag);
}
```

---

## Implementation Plan

### Phase 1: Core MarkBuilder (Weeks 1-2)

**Deliverables:**
- `lambda/mark_builder.hpp` - API header (C++ classes)
- `lambda/mark_builder.cpp` - Implementation
- Basic fluent API for elements, maps, arrays
- String creation with optional interning
- Pool integration

**Tasks:**
1. Design and implement core class structures
2. Implement lifecycle functions (constructors, destructors, finalize)
3. Implement string creation methods
4. Implement ElementBuilder fluent API with method chaining
5. Implement MapBuilder fluent API with method chaining
6. Implement ArrayBuilder fluent API with method chaining
7. Write unit tests (can use existing C++ test framework)

### Phase 2: Core MarkReader (Weeks 3-4)

**Deliverables:**
- `lambda/mark_reader.hpp` - API header (C++ classes)
- `lambda/mark_reader.cpp` - Implementation
- ItemReader with type-safe access
- MapReader with iteration
- ArrayReader with iteration

**Tasks:**
1. Design and implement reader class structures
2. Implement ItemReader with type checking methods
3. Implement MapReader with key iteration
4. Implement ArrayReader with item iteration
5. Enhance existing ElementReader with new methods
6. Write unit tests

### Phase 3: Integration & Testing (Week 5)

**Deliverables:**
- Integrated test suite
- Performance benchmarks
- Documentation and examples

**Tasks:**
1. Create comprehensive test cases
2. Performance comparison with manual code
3. Memory leak detection and validation
4. Write usage documentation
5. Create migration examples

### Phase 4: Pilot Migration (Weeks 6-8)

**Deliverables:**
- Migrate 3-5 input parsers (JSON, XML, YAML)
- Migrate 3-5 formatters (JSON, XML, HTML)
- Validation of API design

**Tasks:**
1. Migrate `input-json.cpp` to MarkBuilder
2. Migrate `input-xml.cpp` to MarkBuilder
3. Migrate `input-yaml.cpp` to MarkBuilder
4. Migrate `format-json.cpp` to MarkReader
5. Migrate `format-xml.cpp` to MarkReader
6. Collect feedback and refine API

### Phase 5: Full Rollout (Weeks 9-12)

**Deliverables:**
- All parsers migrated
- All formatters migrated
- Deprecated old patterns

**Tasks:**
1. Migrate remaining input parsers
2. Migrate remaining formatters
3. Update documentation
4. Remove deprecated code
5. Final performance validation

---

## Migration Strategy

### Backward Compatibility

**Key Principles:**
1. **No breaking changes** to existing data structures (`Item`, `Element`, `Map`, `Array`)
2. **Interoperable** - MarkBuilder output can be used by old code
3. **Gradual adoption** - parsers can be migrated one at a time
4. **Old code still compiles** - new API is additive

### Migration Path for Input Parsers

**Step 1: Add MarkBuilder Alongside Existing Code**

```cpp
// Old code still works
Element* elem = input_create_element(input, "div");

// New code can be used in parallel (stack-allocated builder)
MarkBuilder builder(input);
Item elem_new = builder.element("div").build();
```

**Step 2: Migrate Function by Function**

```cpp
// Before (manual pool allocation, 10+ lines boilerplate)
static Item parse_element(Input* input, const char** xml) {
    Element* elem = input_create_element(input, tag_name);
    // ... manual construction with pool_alloc calls
    return (Item){.raw_pointer = elem};
}

// After (stack-allocated builder, automatic cleanup)
static Item parse_element(Input* input, const char** xml) {
    MarkBuilder builder(input);  // Stack-allocated
    ElementBuilder elem = builder.element(tag_name);  // Stack-allocated
    // ... fluent construction
    return elem.build();  // Pool-allocates final Element
    // builder and elem automatically destroyed
}
```

**Step 3: Remove Old Helper Functions**

Once fully migrated, remove parser-specific helpers like:
- `input_create_element()` → use `builder.element()`
- `input_add_attribute_to_element()` → use `elem.attr()`
- Custom map/array builders → use `MapBuilder`/`ArrayBuilder`
- Manual pool allocation calls → handled by `.build()`

### Migration Path for Formatters

**Step 1: Wrap Existing Code with Readers**

```cpp
// Before (manual type checking with unsafe casts)
TypeId type = get_type_id(item);
if (type == LMD_TYPE_STRING) {
    String* str = (String*)item.pointer;
    format_string(sb, str);
}

// After (stack-allocated reader, type-safe)
ItemReader reader(item, pool);  // Stack-allocated
if (reader.isString()) {
    format_string(sb, reader.asString());
}
// reader automatically destroyed
```

**Step 2: Refactor to Use Reader APIs**

```cpp
// Before (manual loop, unsafe pointer access)
Element* elem = item.element;
for (int i = 0; i < elem->length; i++) {
    Item child = elem->items[i];
    // manual traversal with type checking
}

// After (stack-allocated reader with iterator)
ElementReader elem(item, pool);  // Stack-allocated
ElementReader::Iterator iter = elem.children();
ItemReader child;
while (iter.next(&child)) {
    // clean iteration with type safety
}
// elem and iter automatically destroyed
```

**Step 3: Remove Manual Type Checking**

Once fully migrated, remove:
- Manual `get_type_id()` calls → use `reader.isXxx()` methods
- Unsafe pointer casts → use `reader.asXxx()` methods
- Manual loop iterations over raw arrays → use iterators
- Pool* parameter passing → stored in readers

---

## Examples & Use Cases

### Use Case 1: JSON Parser (Input)

**Current Implementation** (`input-json.cpp`):

```cpp
static Item parse_value(Input *input, const char **json) {
    skip_whitespace(json);
    switch (**json) {
        case '{':
            return {.raw_pointer = parse_object(input, json)};
        case '[':
            return {.raw_pointer = parse_array(input, json)};
        case '"': {
            String* str = parse_string(input, json);
            return str ? (str == &EMPTY_STRING ? (Item){.item = ITEM_NULL} : (Item){.item = s2it(str)}) : (Item){.item = 0};
        }
        // ... more cases
    }
}

static Map* parse_object(Input *input, const char **json) {
    if (**json != '{') return NULL;
    Map* mp = map_pooled(input->pool);
    (*json)++; // skip '{'

    while (**json) {
        String* key = parse_string(input, json);
        skip_whitespace(json);
        if (**json != ':') return mp;
        (*json)++;
        skip_whitespace(json);
        Item value = parse_value(input, json);
        map_put(mp, key, value, input);
        // ... handle commas
    }
    return mp;
}
```

**With MarkBuilder**:

```cpp
static Item parse_value(MarkBuilder& builder, const char **json) {
    skip_whitespace(json);
    switch (**json) {
        case '{':
            return parse_object(builder, json);
        case '[':
            return parse_array(builder, json);
        case '"': {
            String* str = parse_string(builder, json);
            return builder.createStringItem(str->chars);
        }
        // ... more cases
    }
}

static Item parse_object(MarkBuilder& builder, const char **json) {
    if (**json != '{') return builder.createNull();

    MapBuilder map = builder.map();  // Stack-allocated
    (*json)++; // skip '{'

    while (**json) {
        String* key = parse_string(builder, json);
        skip_whitespace(json);
        if (**json != ':') return map.build();
        (*json)++;
        skip_whitespace(json);

        Item value = parse_value(builder, json);
        map.put(key->chars, value);  // Fluent API by reference
        // ... handle commas
    }
    return map.build();  // Pool-allocates final Map
    // map automatically destroyed
}
```

**Benefits:**
- Cleaner, more readable code
- Automatic memory management
- Fewer lines of code
- Type-safe construction
- No manual pool allocation

### Use Case 2: XML Parser (Input)

**Current Implementation** (`input-xml.cpp`):

```cpp
static Item parse_element(Input *input, const char **xml) {
    String* tag_name = parse_tag_name(input, xml);
    Element* element = input_create_element(input, tag_name->chars);

    // Parse attributes
    parse_attributes(input, element, xml);

    // Parse children
    while (**xml && !is_closing_tag(*xml)) {
        if (is_text(*xml)) {
            String* text = parse_text_content(input, xml);
            list_push((List*)element, (Item){.item = s2it(text)});
        } else if (is_opening_tag(*xml)) {
            Item child = parse_element(input, xml);
            list_push((List*)element, child);
        }
    }

    return (Item){.raw_pointer = element};
}
```

**With MarkBuilder**:

```cpp
static Item parse_element(MarkBuilder& builder, const char **xml) {
    String* tag_name = parse_tag_name(builder, xml);
    ElementBuilder elem = builder.element(tag_name->chars);  // Stack-allocated

    // Parse attributes
    while (parse_attribute(xml, &attr_name, &attr_value)) {
        elem.attr(attr_name, attr_value);  // Fluent API by reference
    }

    // Parse children
    while (**xml && !is_closing_tag(*xml)) {
        if (is_text(*xml)) {
            char* text = parse_text_content(builder, xml);
            elem.text(text);
        } else if (is_opening_tag(*xml)) {
            Item child = parse_element(builder, xml);
            elem.child(child);
        }
    }

    return elem.build();  // Pool-allocates final Element
    // elem automatically destroyed
}
```

**Benefits:**
- Fluent API makes structure clear
- No manual type management
- Builder handles ref counting
- Easier to maintain
- Automatic cleanup via RAII

### Use Case 3: JSON Formatter (Output)

**Current Implementation** (`format-json.cpp`):

```cpp
static void format_item(StringBuf* sb, Item item) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_NULL:
            stringbuf_append_str(sb, "null");
            break;
        case LMD_TYPE_BOOL:
            stringbuf_append_str(sb, item.bool_val ? "true" : "false");
            break;
        case LMD_TYPE_STRING:
            format_string(sb, get_string(item));
            break;
        case LMD_TYPE_MAP: {
            Map* mp = item.map;
            TypeMap* map_type = (TypeMap*)mp->type;
            stringbuf_append_char(sb, '{');
            format_json_map_contents(sb, map_type, mp->data, 0);
            stringbuf_append_char(sb, '}');
            break;
        }
        // ... more cases
    }
}
```

**With MarkReader**:

```cpp
static void format_item(StringBuf* sb, const ItemReader& reader) {
    if (reader.isNull()) {
        stringbuf_append_str(sb, "null");
    } else if (reader.isBool()) {
        stringbuf_append_str(sb, reader.asBool() ? "true" : "false");
    } else if (reader.isString()) {
        format_string(sb, reader.cstring());
    } else if (reader.isMap()) {
        MapReader map = reader.asMap();  // Stack-allocated
        stringbuf_append_char(sb, '{');

        MapReader::EntryIterator iter = map.entries();
        const char* key;
        ItemReader value;
        bool first = true;
        while (iter.next(&key, &value)) {
            if (!first) stringbuf_append_char(sb, ',');
            stringbuf_append_format(sb, "\"%s\":", key);
            format_item(sb, value);
            first = false;
        }

        stringbuf_append_char(sb, '}');
        // map and iter automatically destroyed
    }
    // ... more cases
}
```

**Benefits:**
- Type-safe access without casts
- Clean iteration without manual loops
- No direct pointer manipulation
- More maintainable code

### Use Case 4: HTML Formatter (Output)

**Current Implementation** (`format-html.cpp`):

```cpp
static void format_element(StringBuf* sb, Item item) {
    if (get_type_id(item) != LMD_TYPE_ELEMENT) return;

    Element* elem = item.element;
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    const char* tag = elem_type->name.str;

    stringbuf_append_format(sb, "<%s", tag);

    // Attributes
    TypeMap* map_type = (TypeMap*)elem_type;
    ShapeEntry* field = map_type->shape;
    while (field) {
        void* data = ((char*)elem->data) + field->byte_offset;
        if (is_simple_type(field->type->type_id)) {
            stringbuf_append_format(sb, " %.*s=\"",
                (int)field->name->length, field->name->str);
            format_attribute_value(sb, data, field->type->type_id);
            stringbuf_append_char(sb, '"');
        }
        field = field->next;
    }

    stringbuf_append_char(sb, '>');

    // Children
    for (int i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId child_type = get_type_id(child);
        if (child_type == LMD_TYPE_STRING) {
            format_html_string(sb, get_string(child));
        } else if (child_type == LMD_TYPE_ELEMENT) {
            format_element(sb, child);
        }
    }

    stringbuf_append_format(sb, "</%s>", tag);
}
```

**With MarkReader**:

```cpp
static void format_element(StringBuf* sb, const ElementReader& elem) {
    const char* tag = elem.tagName();

    stringbuf_append_format(sb, "<%s", tag);

    // Attributes - directly accessed from ElementReader
    if (elem.attrCount() > 0) {
        const TypeMap* map_type = (const TypeMap*)elem.element()->type;
        const ShapeEntry* field = map_type->shape;

        while (field) {
            const char* attr_name = field->name->str;
            ItemReader attr_value = elem.get_attr(attr_name);

            if (attr_value.isString() || attr_value.isInt() || attr_value.isBool()) {
                stringbuf_append_format(sb, " %s=\"", attr_name);
                format_attribute_value(sb, attr_value);
                stringbuf_append_char(sb, '"');
            }

            field = field->next;
        }
    }

    stringbuf_append_char(sb, '>');

    // Children
    ElementReader::Iterator child_iter = elem.children();
    ItemReader child;
    while (child_iter.next(&child)) {
        if (child.isString()) {
            format_html_string(sb, child.cstring());
        } else if (child.isElement()) {
            ElementReader child_elem = child.asElement();  // By value
            format_element(sb, child_elem);
        }
    }

    stringbuf_append_format(sb, "</%s>", tag);
}
```

**Benefits:**
- No manual ShapeEntry traversal
- Clean iterator-based loops
- Type-safe attribute access
- Easier to read and maintain
- Stack-allocated readers, automatic cleanup

---

## Performance Considerations

### Memory Overhead

**MarkBuilder:**
- Builder structures are small (~64-128 bytes)
- Allocated from pool, freed in bulk
- No additional heap allocations per-item
- String interning reduces duplicate strings

**Estimated Overhead:** < 1% memory increase

**MarkReader:**
- Reader wrappers are stack-allocated or pooled
- Zero-copy - no data duplication
- Iterators maintain minimal state

**Estimated Overhead:** < 0.5% memory increase

### Performance Benchmarks

**Target Metrics:**
- Builder API should be within 5% of manual construction
- Reader API should be within 3% of direct access
- No performance regression on large documents (> 1MB)

**Optimization Strategies:**
1. **Inline small functions** for hot paths
2. **Cache type IDs** to avoid repeated lookups
3. **Minimize allocations** - reuse StringBuf instances
4. **Pool allocation** for all temporary objects
5. **Lazy evaluation** where possible

### Profiling Plan

1. **Baseline**: Measure current parser/formatter performance
2. **Implementation**: Measure MarkBuilder/Reader performance
3. **Comparison**: Identify any hot spots > 5% slower
4. **Optimization**: Tune critical paths
5. **Validation**: Ensure no regressions

---

## Future Extensions

### Phase 2 Enhancements

1. **XPath/CSS Selectors**
   ```c
   // Query elements by complex selectors
   ElementIterator* mark_reader_query(MarkReader* reader, const char* selector);
   // Examples: "div.container p", "article > header h1"
   ```

2. **Transformation API**
   ```c
   // Map/filter/reduce over documents
   Item mark_reader_map(MarkReader* reader, ItemTransformFn fn);
   Item mark_reader_filter(MarkReader* reader, ItemPredicateFn fn);
   ```

3. **Streaming API**
   ```c
   // Event-driven parsing for large documents
   typedef void (*MarkEventHandler)(MarkEvent event, void* context);
   MarkStreamReader* mark_stream_reader_create(MarkEventHandler handler);
   ```

4. **Schema Validation**
   ```c
   // Validate during construction
   MarkBuilder* mark_builder_with_schema(Input* input, Schema* schema);
   bool mark_builder_validate(MarkBuilder* builder);
   ```

5. **Pretty Printing Control**
   ```c
   // Fine-grained formatting control in readers
   typedef struct FormatOptions {
       int indent_size;
       bool compact_arrays;
       bool escape_unicode;
   } FormatOptions;
   ```

6. **Patch/Diff API**
   ```c
   // Compute differences between documents
   MarkDiff* mark_diff(Item old_doc, Item new_doc);
   Item mark_patch(Item doc, MarkDiff* diff);
   ```

### Integration with Existing Systems

1. **Validator Integration**: Use MarkReader in `validator.cpp`
2. **Transpiler Integration**: Generate MIR code from MarkBuilder
3. **WASM Bindings**: Export MarkBuilder/Reader to JavaScript
4. **Foreign Function Interface**: Expose API to other languages (Python, Rust)

---

## Appendices

### Appendix A: Complete API Reference

See inline documentation in code sections above.

### Appendix B: Type System Reference

**TypeId Enumeration:**
```c
enum EnumTypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,
    LMD_TYPE_BOOL,
    LMD_TYPE_INT,           // 32-bit integer
    LMD_TYPE_INT64,         // 64-bit integer
    LMD_TYPE_FLOAT,         // 64-bit double
    LMD_TYPE_DECIMAL,       // Arbitrary precision decimal
    LMD_TYPE_NUMBER,        // Generic number
    LMD_TYPE_DTIME,         // DateTime
    LMD_TYPE_SYMBOL,        // Symbol (interned string)
    LMD_TYPE_STRING,        // String
    LMD_TYPE_BINARY,        // Binary data
    LMD_TYPE_LIST,          // Heterogeneous list
    LMD_TYPE_RANGE,         // Integer range
    LMD_TYPE_ARRAY_INT,     // Homogeneous int array
    LMD_TYPE_ARRAY_INT64,   // Homogeneous int64 array
    LMD_TYPE_ARRAY_FLOAT,   // Homogeneous float array
    LMD_TYPE_ARRAY,         // Generic array
    LMD_TYPE_MAP,           // Map/Dictionary
    LMD_TYPE_ELEMENT,       // DOM-like element
    LMD_TYPE_TYPE,          // Type descriptor
    LMD_TYPE_FUNC,          // Function
    LMD_TYPE_ANY,           // Any type
    LMD_TYPE_ERROR,         // Error value
};
```

### Appendix C: Error Handling

**Strategy:**
- Return default-constructed readers for invalid operations (check with isNull())
- Use `ITEM_ERROR` for error Items in legacy code
- Log warnings for type mismatches
- No exceptions (C-compatible, RAII for cleanup)

**Example:**
```cpp
ItemReader reader(item, pool);  // Stack-allocated
if (reader.isString()) {
    const char* str = reader.cstring();
    // str is valid
} else {
    // Handle type mismatch
    LOG_WARN("Expected string, got %d", reader.getType());
}
// reader automatically destroyed
```

### Appendix D: Thread Safety

**Current Status:**
- Lambda runtime is single-threaded per context
- Pool allocations are not thread-safe
- No synchronization primitives

**MarkBuilder/Reader:**
- **Not thread-safe** by design
- Each thread should have its own builder/reader instances
- Read-only access to shared documents is safe

**Future:** Consider thread-safe pool allocators for multi-threaded formatters.

### Appendix E: Glossary

- **Mark**: Lambda's static data notation (like JSON)
- **Item**: Tagged union representing any Mark value (64-bit)
- **Element**: DOM-like node with tag, attributes, children
- **Pool**: Arena-based memory allocator (rpmalloc wrapper)
- **TypeId**: Runtime type identifier (8-bit enum)
- **ShapeEntry**: Map/Element field descriptor in linked list
- **NamePool**: String interning system for deduplication
- **StringBuf**: Mutable string builder for temporary strings
- **Fluent API**: Method chaining pattern (e.g., `builder.attr().child().build()`)

---

## Conclusion

The proposed MarkBuilder and MarkReader APIs provide a modern, type-safe, and ergonomic interface for working with Lambda's Mark data model. By addressing current pain points in input parsers and formatters, this API will:

1. **Reduce boilerplate** by 70-80% in typical parser code
2. **Improve type safety** through structured access patterns
3. **Enhance maintainability** with consistent, readable code
4. **Maintain performance** within 5% of manual implementations
5. **Enable gradual migration** without breaking existing code

The implementation plan spans 12 weeks with clear milestones and deliverables. Pilot migrations will validate the design before full rollout across 50+ parsers and formatters.

**Next Steps:**
1. Review and approve this proposal
2. Begin Phase 1 implementation (MarkBuilder core)
3. Create test infrastructure
4. Pilot migration with JSON/XML parsers
5. Iterate based on feedback

---

**Document Version:** 1.0
**Last Updated:** November 11, 2025
**Authors:** Lambda Development Team
**Status:** Awaiting Approval
