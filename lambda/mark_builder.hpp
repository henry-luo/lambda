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
class ListBuilder;

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
    // Name Creation Methods (always use name_pool for deduplication)
    // ============================================================================

    /**
     * Create a name string (element names, map keys, attribute names)
     * Always uses name_pool for deduplication
     */
    String* createName(const char* name);
    String* createName(const char* name, size_t len);
    String* createNameFromStrView(StrView name);

    // ============================================================================
    // Symbol Creation Methods (arena-allocated Symbol structs)
    // ============================================================================

    /**
     * Create a symbol
     * Allocates Symbol struct from arena with ns = NULL
     */
    Symbol* createSymbol(const char* symbol);
    Symbol* createSymbol(const char* symbol, size_t len);
    Symbol* createSymbolFromStrView(StrView symbol);

    // ============================================================================
    // String Creation Methods (arena-allocates strings, NO pooling)
    // ============================================================================

    /**
     * Create an arena-allocated String (for content strings)
     * No deduplication - each call creates a new string
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
    // Item Creation Helpers
    // ============================================================================

    /**
     * Create Item from name string (uses name_pool)
     */
    Item createNameItem(const char* name);

    /**
     * Create Item from symbol string (uses name_pool for short symbols)
     */
    Item createSymbolItem(const char* symbol);

    /**
     * Create Item from content string (arena allocation)
     */
    Item createStringItem(const char* str);
    Item createStringItem(const char* str, size_t len);

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

    /**
     * Create list builder
     * Returned by value - stack-allocated
     */
    ListBuilder list();

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
     * Create an empty list
     */
    Item createList();

    /**
     * Create primitive Items
     */
    Item createInt(int64_t value);  // accepts int64, packs as int56
    Item createLong(int64_t value);
    Item createFloat(double value);
    Item createBool(bool value);
    Item createNull();

    /**
     * Create a Range Item
     * @param start Inclusive start of range
     * @param end Inclusive end of range
     * @return Range Item with type LMD_TYPE_RANGE
     */
    Item createRange(int64_t start, int64_t end);

    /**
     * Create a Type Item
     * @param type_id The type identifier (from TypeId enum)
     * @param is_literal Whether this is a literal type (default: false)
     * @param is_const Whether this is a const type (default: false)
     * @return Type Item with type LMD_TYPE_TYPE
     */
    Item createType(TypeId type_id, bool is_literal = false, bool is_const = false);

    Item createMetaType(TypeId type_id);

    // ============================================================================
    // Configuration Methods
    // ============================================================================

    /**
     * Enable/disable automatic merging of consecutive string children
     */
    void setAutoStringMerge(bool enabled) { auto_string_merge_ = enabled; }

    // ============================================================================
    // Accessors (for internal use by sub-builders)
    // ============================================================================

    Input* input() const { return input_; }
    Pool* pool() const { return pool_; }
    Arena* arena() const { return arena_; }
    NamePool* namePool() const { return name_pool_; }
    ArrayList* typeList() const { return type_list_; }
    bool autoStringMerge() const { return auto_string_merge_; }

    // ============================================================================
    // Deep Copy Methods (with smart ownership checking)
    // ============================================================================

    /**
     * Deep copy an Item to this builder's Input arena
     *
     * Performs smart ownership checking:
     * - If item data is already in this Input's arena chain, returns original (no copy)
     * - If item data is in parent Input's arena chain, returns original (no copy)
     * - If item data is external, performs deep recursive copy
     *
     * This enables efficient cross-Input data sharing while ensuring memory safety.
     *
     * @param item The item to potentially copy
     * @return Original item if already owned, or a deep copy if external
     */
    Item deep_copy(Item item);

    /**
     * Check if an Item's data is in this Input's arena chain
     *
     * Traverses the parent chain: current Input -> parent -> parent -> ...
     * Returns true if all pointer data is owned by an arena in the chain.
     *
     * @param item The item to check
     * @return true if item data is in arena chain, false if external
     */
    bool is_in_arena(Item item) const;

private:
    /**
     * Check if a pointer is owned by this Input's arena chain
     * Traverses: current arena -> parent Input's arena -> parent's parent -> ...
     *
     * @param ptr Pointer to check
     * @return true if ptr is in arena chain, false otherwise
     */
    bool is_pointer_in_arena_chain(const void* ptr) const;

    /**
     * Internal implementation of deep_copy (recursive)
     * Called by public deep_copy() after ownership check
     */
    Item deep_copy_internal(Item item);

public:
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

    // Put key-value pair with string value
    MapBuilder& put(const char* key, const char* value);

    // Put key-value pair with int64_t value
    MapBuilder& put(const char* key, int64_t value);

    // Put key-value pair with int32_t value
    MapBuilder& put(const char* key, int32_t value);

    // Put key-value pair with float value
    MapBuilder& put(const char* key, double value);

    // Put key-value pair with boolean value
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

/**
 * ListBuilder - Fluent API for constructing List nodes
 *
 * MEMORY MODEL: Stack-allocated value type
 * - Automatically destroyed when scope ends
 * - List is allocated from pool with dynamic resizing
 *
 * DIFFERENCE from ArrayBuilder:
 * - List uses list_push() which flattens nested lists and skips nulls
 * - Array uses array_append() which preserves nested arrays and nulls
 */
class ListBuilder {
private:
    MarkBuilder* builder_;      // parent builder
    List* list_;                // list being built

    friend class MarkBuilder;

    /**
     * Private constructor - use MarkBuilder::list() instead
     */
    explicit ListBuilder(MarkBuilder* builder);

public:
    /**
     * Destructor - automatic cleanup
     */
    ~ListBuilder();

    // copyable and movable (for value semantics)
    ListBuilder(const ListBuilder&) = default;
    ListBuilder& operator=(const ListBuilder&) = default;
    ListBuilder(ListBuilder&&) = default;
    ListBuilder& operator=(ListBuilder&&) = default;

    // ============================================================================
    // Push Operations (return reference for chaining)
    // ============================================================================

    /**
     * Push an Item to the list
     * Note: Nulls are skipped, nested lists are flattened
     */
    ListBuilder& push(Item item);

    /**
     * Push string value (convenience)
     */
    ListBuilder& push(const char* str);

    /**
     * Push integer value (convenience)
     */
    ListBuilder& push(int64_t value);

    /**
     * Push float value (convenience)
     */
    ListBuilder& push(double value);

    /**
     * Push boolean value (convenience)
     */
    ListBuilder& push(bool value);

    /**
     * Push multiple items from initializer list
     */
    ListBuilder& pushItems(std::initializer_list<Item> items);

    // ============================================================================
    // Finalization (returns final List)
    // ============================================================================

    /**
     * Build and return the final List Item
     */
    Item final();
};

#endif // LAMBDA_MARK_BUILDER_HPP
