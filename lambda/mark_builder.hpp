#ifndef LAMBDA_MARK_BUILDER_HPP
#define LAMBDA_MARK_BUILDER_HPP

#include "lambda-data.hpp"
#include "name_pool.hpp"
#include "../lib/mempool.h"
#include "../lib/strbuf.h"
#include "../lib/stringbuf.h"
#include <initializer_list>

// Forward declarations
class MarkBuilder;
class ElementBuilder;
class MapBuilder;
class ArrayBuilder;

/**
 * MarkBuilder - Fluent API for constructing Mark documents in input parsers
 *
 * MEMORY MODEL:
 * - MarkBuilder itself is STACK-ALLOCATED in the parser function scope
 * - Automatically destroyed when parser function exits (RAII)
 * - Mark data (Element, Map, Array, String) is allocated from Input's ARENA
 * - Arena allocation is FAST (bump-pointer, O(1)) with zero per-allocation overhead
 * - All arena data lives until Input's arena is reset/destroyed
 * - No manual memory management needed for the builder or its created data
 *
 * ARENA vs POOL:
 * - Arena: Used for Mark structures (String, Map, Element, primitives)
 * - Pool: Used internally by map_put/elmt_put for dynamic data buffers
 *
 * USAGE PATTERN:
 *   void parse_json(Input* input, const char* json) {
 *       MarkBuilder builder(input);  // Stack allocation
 *       Item result = builder.map()  // Returns MapBuilder by value
 *           .put("key", "value")     // Fluent chaining by reference
 *           .build();                // Arena-allocates final Map
 *       input->root = result;
 *   }  // builder automatically destroyed here
 *      // Arena data lives on in input until arena_reset/arena_destroy
 */
class MarkBuilder {
private:
    Input* input_;              // input context (pool, arena, type_list, name_pool, etc.)
    Pool* pool_;                // memory pool (cached from input) - for compatibility
    Arena* arena_;              // arena allocator (cached from input) - primary allocator
    NamePool* name_pool_;       // string interning pool
    ArrayList* type_list_;      // type registry

    bool auto_string_merge_;    // automatically merge consecutive strings
    bool intern_strings_;       // use string interning

public:
    /**
     * Construct MarkBuilder from Input context
     * Stack-allocated, RAII cleanup
     */
    explicit MarkBuilder(Input* input);

    /**
     * Destructor - automatic cleanup (RAII)
     */
    ~MarkBuilder();

    // non-copyable (to prevent accidental copies)
    MarkBuilder(const MarkBuilder&) = delete;
    MarkBuilder& operator=(const MarkBuilder&) = delete;

    // movable (for return value optimization)
    MarkBuilder(MarkBuilder&&) = default;
    MarkBuilder& operator=(MarkBuilder&&) = default;

    // ============================================================================
    // String Creation Methods (arena-allocates strings for final Mark data)
    // ============================================================================

    /**
     * Create an arena-allocated String from C string
     * Uses string interning if enabled
     */
    String* createString(const char* str);
    String* createString(const char* str, size_t len);

    /**
     * Create String from StringBuf (takes ownership of content)
     */
    String* createStringFromBuf(StringBuf* sb);

    /**
     * Get empty string singleton
     */
    static String* emptyString();

    // ============================================================================
    // Builder Creation Methods (returns stack-allocated value types)
    // ============================================================================

    /**
     * Create element builder for given tag name
     * Returned by value - stack-allocated
     */
    ElementBuilder element(const char* tag_name);

    /**
     * Create map builder
     * Returned by value - stack-allocated
     */
    MapBuilder map();

    /**
     * Create array builder
     * Returned by value - stack-allocated
     */
    ArrayBuilder array();

    // ============================================================================
    // Direct Item Creation (convenience - arena-allocates final data immediately)
    // ============================================================================

    /**
     * Create an empty element with given tag name
     */
    Item createElement(const char* tag_name);

    /**
     * Create an empty map
     */
    Item createMap();

    /**
     * Create an empty array
     */
    Item createArray();

    /**
     * Create string Item
     */
    Item createStringItem(const char* str);
    Item createStringItem(const char* str, size_t len);

    /**
     * Create primitive Items
     */
    Item createInt(int32_t value);
    Item createLong(int64_t value);
    Item createFloat(double value);
    Item createBool(bool value);
    Item createNull();

    // ============================================================================
    // Configuration Methods
    // ============================================================================

    /**
     * Enable/disable automatic merging of consecutive string children
     */
    void setAutoStringMerge(bool enabled) { auto_string_merge_ = enabled; }

    /**
     * Enable/disable string interning
     */
    void setInternStrings(bool enabled) { intern_strings_ = enabled; }

    // ============================================================================
    // Accessors (for internal use by sub-builders)
    // ============================================================================

    Input* input() const { return input_; }
    Pool* pool() const { return pool_; }
    Arena* arena() const { return arena_; }
    NamePool* namePool() const { return name_pool_; }
    ArrayList* typeList() const { return type_list_; }
    bool autoStringMerge() const { return auto_string_merge_; }
    bool internStrings() const { return intern_strings_; }

    // ============================================================================
    // Internal Helpers (for ElementBuilder/MapBuilder to call legacy functions)
    // ============================================================================

    /**
     * Internal helper to add attribute to existing element
     * Wraps the static elmt_put() function
     */
    void putToElement(Element* elmt, String* key, Item value);

    /**
     * Internal helper to add key-value to existing map
     * Wraps the static map_put() function
     */
    void putToMap(Map* map, String* key, Item value);
};

/**
 * ElementBuilder - Fluent API for constructing Element nodes
 *
 * MEMORY MODEL: Stack-allocated value type
 * - Automatically destroyed when scope ends
 * - Final Element is arena-allocated when final() is called
 */
class ElementBuilder {
private:
    MarkBuilder* builder_;      // parent builder
    String* tag_name_;          // element tag name
    Element* elmt_;             // element being built (pool-allocated)
    // TypeMap* attr_type_;        // attribute type descriptor
    ElementBuilder* parent_;    // for nested elements (stack reference)

    friend class MarkBuilder;

    /**
     * Private constructor - use MarkBuilder::element() instead
     */
    ElementBuilder(MarkBuilder* builder, const char* tag_name);

public:
    /**
     * Destructor - automatic cleanup
     */
    ~ElementBuilder();

    // copyable and movable (for value semantics)
    ElementBuilder(const ElementBuilder&) = default;
    ElementBuilder& operator=(const ElementBuilder&) = default;
    ElementBuilder(ElementBuilder&&) = default;
    ElementBuilder& operator=(ElementBuilder&&) = default;

    // ============================================================================
    // Attribute Setters (return reference for chaining)
    // ============================================================================

    /**
     * Set attribute with Item value
     */
    ElementBuilder& attr(const char* key, Item value);

    /**
     * Set attribute with string value (convenience)
     */
    ElementBuilder& attr(const char* key, const char* value);

    /**
     * Set attribute with integer value (convenience)
     */
    ElementBuilder& attr(const char* key, int64_t value);

    /**
     * Set attribute with float value (convenience)
     */
    ElementBuilder& attr(const char* key, double value);

    /**
     * Set attribute with boolean value (convenience)
     */
    ElementBuilder& attr(const char* key, bool value);

    // NEW: String* key overloads (avoid re-creating strings parsers already have)
    /**
     * Set attribute with String* key and Item value
     */
    ElementBuilder& attr(String* key, Item value);

    /**
     * Set attribute with String* key and string value
     */
    ElementBuilder& attr(String* key, const char* value);

    /**
     * Set attribute with String* key and integer value
     */
    ElementBuilder& attr(String* key, int64_t value);

    /**
     * Set attribute with String* key and float value
     */
    ElementBuilder& attr(String* key, double value);

    /**
     * Set attribute with String* key and boolean value
     */
    ElementBuilder& attr(String* key, bool value);

    // Explicit nullptr overloads to avoid ambiguity (delegate to const char* version)
    ElementBuilder& attr(std::nullptr_t, Item value) { return attr((const char*)nullptr, value); }
    ElementBuilder& attr(std::nullptr_t, const char* value) { return attr((const char*)nullptr, value); }
    ElementBuilder& attr(const char* key, std::nullptr_t) { return attr(key, (const char*)nullptr); }

    // ============================================================================
    // Child Management (return reference for chaining)
    // ============================================================================

    /**
     * Add a child Item
     */
    ElementBuilder& child(Item item);

    /**
     * Add text content (creates String item)
     */
    ElementBuilder& text(const char* text);
    ElementBuilder& text(const char* text, size_t len);

    /**
     * Add multiple children from initializer list
     */
    ElementBuilder& children(std::initializer_list<Item> items);

    // ============================================================================
    // Nested Element Building
    // ============================================================================

    /**
     * Begin a nested child element
     * Returns new ElementBuilder by value (stack-allocated)
     */
    ElementBuilder beginChild(const char* tag_name);

    /**
     * End nested element and return to parent
     * Returns reference to parent for chaining
     */
    ElementBuilder& end();

    // ============================================================================
    // Finalization (returns final Element from arena)
    // ============================================================================

    /**
     * Build and return the final Element Item
     * Element structure was arena-allocated during construction
     */
    Item final();
};

/**
 * MapBuilder - Fluent API for constructing Map nodes
 *
 * MEMORY MODEL: Stack-allocated value type
 * - Automatically destroyed when scope ends
 * - Final Map structure is arena-allocated on construction
 * - Internal data buffers use pool (for dynamic resizing)
 */
class MapBuilder {
private:
    MarkBuilder* builder_;      // parent builder
    Map* map_;                  // map data (arena-allocated structure)
    TypeMap* map_type_;         // map type descriptor

    friend class MarkBuilder;

    /**
     * Private constructor - use MarkBuilder::map() instead
     */
    explicit MapBuilder(MarkBuilder* builder);

public:
    /**
     * Destructor - automatic cleanup
     */
    ~MapBuilder();

    // copyable and movable (for value semantics)
    MapBuilder(const MapBuilder&) = default;
    MapBuilder& operator=(const MapBuilder&) = default;
    MapBuilder(MapBuilder&&) = default;
    MapBuilder& operator=(MapBuilder&&) = default;

    // ============================================================================
    // Key-Value Setters (return reference for chaining)
    // ============================================================================

    /**
     * Put key-value pair with Item value
     */
    MapBuilder& put(const char* key, Item value);

    /**
     * Put key-value pair with existing String* key (for parsers)
     */
    MapBuilder& put(String* key, Item value);

    /**
     * Put key-value pair with string value (convenience)
     */
    MapBuilder& put(const char* key, const char* value);

    /**
     * Put key-value pair with integer value (convenience)
     */
    MapBuilder& put(const char* key, int64_t value);

    /**
     * Put key-value pair with float value (convenience)
     */
    MapBuilder& put(const char* key, double value);

    /**
     * Put key-value pair with boolean value (convenience)
     */
    MapBuilder& put(const char* key, bool value);

    /**
     * Put null value for key
     */
    MapBuilder& putNull(const char* key);

    /**
     * Put key-value pair with String* key and string value
     */
    MapBuilder& put(String* key, const char* value);

    /**
     * Put key-value pair with String* key and integer value
     */
    MapBuilder& put(String* key, int64_t value);

    /**
     * Put key-value pair with String* key and float value
     */
    MapBuilder& put(String* key, double value);

    /**
     * Put key-value pair with String* key and boolean value
     */
    MapBuilder& put(String* key, bool value);

    // Explicit nullptr overloads to avoid ambiguity (delegate to const char* version)
    MapBuilder& put(std::nullptr_t, Item value) { return put((const char*)nullptr, value); }
    MapBuilder& put(std::nullptr_t, const char* value) { return put((const char*)nullptr, value); }
    MapBuilder& put(const char* key, std::nullptr_t) { return put(key, (const char*)nullptr); }

    // ============================================================================
    // Finalization (returns final Map from arena)
    // ============================================================================

    /**
     * Build and return the final Map Item
     * Map structure was arena-allocated during construction
     */
    Item final();
};

/**
 * ArrayBuilder - Fluent API for constructing Array nodes
 *
 * MEMORY MODEL: Stack-allocated value type
 * - Automatically destroyed when scope ends
 * - Final Array is arena/pool-allocated (via array_pooled)
 */
class ArrayBuilder {
private:
    MarkBuilder* builder_;      // parent builder
    Array* array_;              // array being built (pool-allocated via array_pooled)

    friend class MarkBuilder;

    /**
     * Private constructor - use MarkBuilder::array() instead
     */
    explicit ArrayBuilder(MarkBuilder* builder);

public:
    /**
     * Destructor - automatic cleanup
     */
    ~ArrayBuilder();

    // copyable and movable (for value semantics)
    ArrayBuilder(const ArrayBuilder&) = default;
    ArrayBuilder& operator=(const ArrayBuilder&) = default;
    ArrayBuilder(ArrayBuilder&&) = default;
    ArrayBuilder& operator=(ArrayBuilder&&) = default;

    // ============================================================================
    // Append Operations (return reference for chaining)
    // ============================================================================

    /**
     * Append an Item to the array
     */
    ArrayBuilder& append(Item item);

    /**
     * Append string value (convenience)
     */
    ArrayBuilder& append(const char* str);

    /**
     * Append integer value (convenience)
     */
    ArrayBuilder& append(int64_t value);

    /**
     * Append float value (convenience)
     */
    ArrayBuilder& append(double value);

    /**
     * Append boolean value (convenience)
     */
    ArrayBuilder& append(bool value);

    /**
     * Append multiple items from initializer list
     */
    ArrayBuilder& appendItems(std::initializer_list<Item> items);

    // ============================================================================
    // Finalization (returns final Array)
    // ============================================================================

    /**
     * Build and return the final Array Item
     * Array was pool-allocated via array_pooled during construction
     */
    Item final();
};

#endif // LAMBDA_MARK_BUILDER_HPP
