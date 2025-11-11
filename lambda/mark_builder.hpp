#ifndef LAMBDA_MARK_BUILDER_HPP
#define LAMBDA_MARK_BUILDER_HPP

#include "lambda-data.hpp"
#include "name_pool.h"
#include "../lib/mempool.h"
#include "../lib/strbuf.h"
#include "../lib/stringbuf.h"
#include "../lib/arraylist.h"
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
    Input* input_;              // input context (pool, type_list, name_pool, etc.)
    Pool* pool_;                // memory pool (cached from input) - for Mark data only
    NamePool* name_pool_;       // string interning pool
    ArrayList* type_list_;      // type registry
    StringBuf* sb_;             // shared string buffer for temp work
    
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
    // String Creation Methods (pool-allocates strings for final Mark data)
    // ============================================================================
    
    /**
     * Create a pool-allocated String from C string
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
    // Direct Item Creation (convenience - pool-allocates final data immediately)
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
    Item createInt(int64_t value);
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
    NamePool* namePool() const { return name_pool_; }
    ArrayList* typeList() const { return type_list_; }
    StringBuf* stringBuf() const { return sb_; }
    bool autoStringMerge() const { return auto_string_merge_; }
    bool internStrings() const { return intern_strings_; }
};

/**
 * ElementBuilder - Fluent API for constructing Element nodes
 * 
 * MEMORY MODEL: Stack-allocated value type
 * - Automatically destroyed when scope ends
 * - Only final Element is pool-allocated when build() is called
 */
class ElementBuilder {
private:
    MarkBuilder* builder_;      // parent builder
    String* tag_name_;          // element tag name
    ArrayList* children_;       // child items (temp storage)
    Map* attributes_;           // attribute map (pool-allocated)
    TypeMap* attr_type_;        // attribute type descriptor
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
    // Finalization (pool-allocates and returns final Element)
    // ============================================================================
    
    /**
     * Build and return the final Element Item
     * Pool-allocates the Element structure
     */
    Item build();
};

/**
 * MapBuilder - Fluent API for constructing Map nodes
 * 
 * MEMORY MODEL: Stack-allocated value type
 * - Automatically destroyed when scope ends
 * - Only final Map is pool-allocated when build() is called
 */
class MapBuilder {
private:
    MarkBuilder* builder_;      // parent builder
    Map* map_;                  // map data (pool-allocated)
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
    
    // ============================================================================
    // Finalization (pool-allocates and returns final Map)
    // ============================================================================
    
    /**
     * Build and return the final Map Item
     * Pool-allocates the Map structure
     */
    Item build();
};

/**
 * ArrayBuilder - Fluent API for constructing Array nodes
 * 
 * MEMORY MODEL: Stack-allocated value type
 * - Automatically destroyed when scope ends
 * - Only final Array is pool-allocated when build() is called
 */
class ArrayBuilder {
private:
    MarkBuilder* builder_;      // parent builder
    ArrayList* items_;          // temp storage for items
    
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
    // Finalization (pool-allocates and returns final Array)
    // ============================================================================
    
    /**
     * Build and return the final Array Item
     * Pool-allocates the Array structure
     */
    Item build();
};

#endif // LAMBDA_MARK_BUILDER_HPP
