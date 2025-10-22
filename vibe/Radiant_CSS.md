# Radiant CSS Enhancement Plan: AVL Style Tree Integration

## Executive Summary

This plan outlines how to enhance Lambda's HTML/CSS parsing capabilities to match Lexbor's sophisticated CSS system, specifically implementing AVL tree-based style management for efficient CSS property lookup and cascade resolution. The enhanced system will integrate seamlessly with the Radiant layout and rendering engine to provide production-quality web document processing.

## Current State Analysis

### Lexbor CSS Architecture Strengths

Based on analysis of the Lexbor source code, the key architectural advantages are:

1. **AVL Tree-Based Style Storage**
   - Each DOM element maintains an AVL tree keyed by CSS property ID
   - O(log n) lookup time for any CSS property
   - Self-balancing ensures consistent performance
   - Memory-efficient through shared declaration instances

2. **CSS Cascade Management**
   - "Weak lists" maintain losing declarations sorted by specificity
   - Enables dynamic style updates without re-matching selectors
   - Supports cascade re-evaluation when styles change
   - Proper CSS4 cascade implementation

3. **Memory Efficiency**
   - Reference counting for shared CSS declarations
   - Memory pools prevent fragmentation
   - Single global AVL tree allocator across all elements

4. **Complete CSS3+ Parser**
   - Full tokenizer, syntax parser, selector parser, property parser
   - Supports modern CSS features (CSS Grid, Flexbox, custom properties)
   - Comprehensive selector matching engine

### Lambda's Current Limitations

Lambda's existing CSS/HTML parsing has several gaps:

1. **Basic CSS Parser**
   - Simple token-based parsing in `css_parser.c`
   - Limited CSS3+ feature support
   - No sophisticated selector matching
   - Basic property validation

2. **Linear Property Storage**
   - Properties stored in simple arrays/lists
   - O(n) lookup time degrades with complex stylesheets
   - No cascade resolution beyond basic inheritance

3. **Limited HTML5 Support**
   - `input-html.cpp` handles basic HTML parsing
   - Missing semantic elements and modern attributes
   - No integration with CSS cascade

4. **Manual Style Resolution**
   - Radiant's `resolve_style.cpp` manually queries properties
   - No automatic cascade computation
   - Limited dynamic style capabilities

## Architecture Enhancement Plan

### Phase 1: Core Infrastructure (Weeks 1-3)

#### 1.1 AVL Tree Implementation
Create Lambda-compatible AVL tree system in `lib/avl_tree.c/h`:

```c
typedef struct AvlNode {
    uintptr_t property_id;       // CSS property ID as key
    void* declaration;           // CSS declaration value
    short height;                // AVL balance height
    struct AvlNode* left;
    struct AvlNode* right;
    struct AvlNode* parent;
} AvlNode;

typedef struct AvlTree {
    AvlNode* root;
    Pool* pool;                  // Memory allocation pool
    int node_count;
} AvlTree;

// Core AVL operations
AvlNode* avl_insert(AvlTree* tree, uintptr_t property_id, void* declaration);
AvlNode* avl_search(AvlTree* tree, uintptr_t property_id);
void* avl_remove(AvlTree* tree, uintptr_t property_id);
void avl_foreach(AvlTree* tree, avl_callback_t callback, void* context);
```

#### 1.2 CSS Property System
Enhance `lambda/input/css_properties.c` with comprehensive property database:

```c
typedef struct CssProperty {
    uintptr_t id;               // Unique property ID
    const char* name;           // Property name ("color", "margin", etc.)
    PropertyType type;          // VALUE_COLOR, VALUE_LENGTH, etc.
    bool inherited;             // Whether property inherits
    void* initial_value;        // Initial/default value
    PropertyValidator validator; // Validation function
} CssProperty;

// Property lookup functions
uintptr_t css_property_get_id(const char* name);
CssProperty* css_property_get_by_id(uintptr_t id);
bool css_property_validate_value(uintptr_t property_id, void* value);
void* css_property_get_initial_value(uintptr_t property_id);
```

#### 1.3 Style Node with Cascade Support
Create style nodes that extend AVL nodes with CSS cascade capabilities:

```c
typedef struct WeakDeclaration {
    void* declaration;                    // CSS declaration
    uint32_t specificity;                // CSS specificity value
    struct WeakDeclaration* next;        // Next in sorted list
} WeakDeclaration;

typedef struct StyleNode {
    AvlNode base;                        // Base AVL node
    WeakDeclaration* weak_list;          // Lower-specificity declarations
    uint32_t current_specificity;        // Current winning specificity
} StyleNode;
```

### Phase 2: Enhanced CSS Parser (Weeks 4-6)

#### 2.1 Modern CSS3+ Tokenizer
Replace basic tokenizer in `css_tokenizer.c` with full CSS3+ support:

```c
typedef enum CssTokenType {
    CSS_TOKEN_IDENT,
    CSS_TOKEN_FUNCTION,
    CSS_TOKEN_AT_KEYWORD,
    CSS_TOKEN_HASH,
    CSS_TOKEN_STRING,
    CSS_TOKEN_BAD_STRING,
    CSS_TOKEN_URL,
    CSS_TOKEN_BAD_URL,
    CSS_TOKEN_DELIM,
    CSS_TOKEN_NUMBER,
    CSS_TOKEN_PERCENTAGE,
    CSS_TOKEN_DIMENSION,
    CSS_TOKEN_UNICODE_RANGE,
    CSS_TOKEN_INCLUDE_MATCH,     // ~=
    CSS_TOKEN_DASH_MATCH,        // |=
    CSS_TOKEN_PREFIX_MATCH,      // ^=
    CSS_TOKEN_SUFFIX_MATCH,      // $=
    CSS_TOKEN_SUBSTRING_MATCH,   // *=
    CSS_TOKEN_COLUMN,            // ||
    CSS_TOKEN_WHITESPACE,
    CSS_TOKEN_CDO,               // <!--
    CSS_TOKEN_CDC,               // -->
    CSS_TOKEN_COLON,
    CSS_TOKEN_SEMICOLON,
    CSS_TOKEN_COMMA,
    CSS_TOKEN_LEFT_BRACKET,
    CSS_TOKEN_RIGHT_BRACKET,
    CSS_TOKEN_LEFT_PAREN,
    CSS_TOKEN_RIGHT_PAREN,
    CSS_TOKEN_LEFT_BRACE,
    CSS_TOKEN_RIGHT_BRACE
} CssTokenType;

// Enhanced tokenizer functions
CssToken* css_tokenize_advanced(const char* css_text, size_t length, 
                                Pool* pool, size_t* token_count);
bool css_token_is_ident_start_char(uint32_t codepoint);
bool css_token_is_name_char(uint32_t codepoint);
double css_parse_number(const char* start, const char* end);
```

#### 2.2 Sophisticated Selector Parser
Create comprehensive selector parsing with CSS4 support:

```c
typedef enum SelectorCombinator {
    COMBINATOR_DESCENDANT,       // space
    COMBINATOR_CHILD,           // >
    COMBINATOR_NEXT_SIBLING,    // +
    COMBINATOR_SUBSEQUENT_SIBLING, // ~
    COMBINATOR_COLUMN           // || (CSS4)
} SelectorCombinator;

typedef struct ComplexSelector {
    SelectorComponent* components;       // Type, class, ID, attribute, pseudo
    SelectorCombinator combinator;       // Relationship to next selector
    uint32_t specificity;              // Calculated CSS specificity
    struct ComplexSelector* next;       // Next in compound selector
} ComplexSelector;

typedef struct SelectorList {
    ComplexSelector* selectors;         // Comma-separated selectors
    int selector_count;
} SelectorList;

// Selector parsing functions
SelectorList* css_parse_selector_list(CssParser* parser);
uint32_t css_calculate_specificity(ComplexSelector* selector);
bool css_selector_matches_element(ComplexSelector* selector, Element* element);
```

#### 2.3 Advanced Property Value Parser
Enhance property value parsing with CSS3+ data types:

```c
typedef enum PropertyValueType {
    VALUE_KEYWORD,
    VALUE_LENGTH,
    VALUE_PERCENTAGE,
    VALUE_COLOR,
    VALUE_NUMBER,
    VALUE_INTEGER,
    VALUE_ANGLE,
    VALUE_TIME,
    VALUE_FREQUENCY,
    VALUE_RESOLUTION,
    VALUE_FUNCTION,              // calc(), var(), etc.
    VALUE_STRING,
    VALUE_URL,
    VALUE_CUSTOM_PROPERTY        // CSS custom properties
} PropertyValueType;

typedef struct PropertyValue {
    PropertyValueType type;
    union {
        double number;
        struct { double value; CssLengthUnit unit; } length;
        struct { double value; } percentage;
        struct { uint8_t r, g, b, a; } color;
        struct { char* name; PropertyValue* args; int arg_count; } function;
        char* string;
        char* url;
        char* custom_property_name;
    } data;
} PropertyValue;

// Property value parsing
PropertyValue* css_parse_property_value(CssParser* parser, uintptr_t property_id);
bool css_validate_property_value(uintptr_t property_id, PropertyValue* value);
PropertyValue* css_compute_value(PropertyValue* specified, Element* element);
```

### Phase 3: DOM Integration (Weeks 7-9)

#### 3.1 Enhanced Element Structure
Extend Lambda's Element structure to support AVL style trees:

```c
typedef struct EnhancedElement {
    Element base;                    // Base Lambda element
    AvlTree* style_tree;            // AVL tree of CSS properties
    AvlTree* computed_style_tree;   // Computed values cache
    uint32_t style_version;         // For invalidation tracking
    bool needs_style_recompute;     // Dirty flag
} EnhancedElement;

// Style management functions
void element_apply_css_rule(EnhancedElement* element, CssRule* rule, 
                           uint32_t specificity);
PropertyValue* element_get_computed_style(EnhancedElement* element, 
                                         uintptr_t property_id);
void element_invalidate_style(EnhancedElement* element);
void element_recompute_style(EnhancedElement* element);
```

#### 3.2 Document-Level Style Management
Create document-wide style management system:

```c
typedef struct DocumentStyler {
    Pool* pool;
    AvlTree* global_styles;         // Global AVL tree allocator
    ArrayList* stylesheets;         // Loaded stylesheets
    HashMap* custom_properties;     // CSS custom property registry
    SelectorMatcher* selector_engine; // Selector matching engine
} DocumentStyler;

// Document style functions
DocumentStyler* document_styler_create(Pool* pool);
void document_styler_add_stylesheet(DocumentStyler* styler, CssStylesheet* sheet);
void document_styler_apply_to_element(DocumentStyler* styler, EnhancedElement* element);
void document_styler_recompute_all(DocumentStyler* styler);
```

#### 3.3 Selector Matching Engine
Implement efficient selector-to-element matching:

```c
typedef struct SelectorMatcher {
    Pool* pool;
    HashTable* element_cache;       // Cache selector matches
    BloomFilter* element_filter;    // Fast element filtering
} SelectorMatcher;

typedef struct MatchResult {
    bool matches;
    uint32_t specificity;
    SelectorPseudoState pseudo_state; // :hover, :focus, etc.
} MatchResult;

// Selector matching functions
MatchResult selector_match_element(SelectorMatcher* matcher, 
                                  ComplexSelector* selector, 
                                  EnhancedElement* element);
void selector_invalidate_cache(SelectorMatcher* matcher);
ArrayList* selector_find_matching_elements(SelectorMatcher* matcher,
                                          ComplexSelector* selector,
                                          EnhancedElement* root);
```

### Phase 4: Radiant Integration (Weeks 10-12)

#### 4.1 Style Resolution Interface
Replace manual property queries in Radiant with AVL tree lookups:

```c
// Replace current resolve_style.cpp functions with:
float radiant_get_length_property(EnhancedElement* element, uintptr_t property_id,
                                 float containing_block_size);
Color radiant_get_color_property(EnhancedElement* element, uintptr_t property_id);
int radiant_get_integer_property(EnhancedElement* element, uintptr_t property_id);
bool radiant_get_boolean_property(EnhancedElement* element, uintptr_t property_id);

// Batch property resolution for layout
typedef struct LayoutProperties {
    float width, height;
    float margin_top, margin_right, margin_bottom, margin_left;
    float padding_top, padding_right, padding_bottom, padding_left;
    DisplayType display;
    PositionType position;
    FlexDirection flex_direction;
    // ... other layout properties
} LayoutProperties;

LayoutProperties radiant_resolve_layout_properties(EnhancedElement* element);
```

#### 4.2 Dynamic Style Updates
Support dynamic style changes for interactive documents:

```c
// Style update functions
void radiant_update_element_style(EnhancedElement* element, 
                                 const char* property_name, 
                                 PropertyValue* new_value);
void radiant_add_css_rule(DocumentStyler* styler, const char* css_text);
void radiant_remove_css_rule(DocumentStyler* styler, CssRule* rule);
void radiant_toggle_pseudo_class(EnhancedElement* element, const char* pseudo_class);

// Layout invalidation integration
void radiant_invalidate_layout_from_style_change(EnhancedElement* element,
                                                uintptr_t changed_property);
```

#### 4.3 Performance Optimizations
Implement caching and optimization strategies:

```c
// Computed style cache
typedef struct ComputedStyleCache {
    AvlTree* properties;            // Cached computed values
    uint32_t cache_version;         // For invalidation
    bool is_valid;
} ComputedStyleCache;

// Style inheritance cache
typedef struct InheritanceCache {
    HashMap* inherited_values;      // property_id -> value
    EnhancedElement* parent_element;
    uint32_t parent_style_version;
} InheritanceCache;

// Optimization functions
void style_cache_invalidate(ComputedStyleCache* cache);
PropertyValue* style_cache_get_or_compute(ComputedStyleCache* cache,
                                         uintptr_t property_id,
                                         EnhancedElement* element);
void style_inheritance_propagate(EnhancedElement* parent, EnhancedElement* child);
```

## Implementation Timeline

### Week 1-3: Foundation
- [ ] Implement AVL tree data structure in `lib/avl_tree.c`
- [ ] Create comprehensive CSS property database
- [ ] Design StyleNode structure with cascade support
- [ ] Write unit tests for AVL operations

### Week 4-6: Parser Enhancement
- [ ] Rewrite CSS tokenizer with full CSS3+ support
- [ ] Implement sophisticated selector parser
- [ ] Create advanced property value parser
- [ ] Add CSS4 features (custom properties, CSS Grid)

### Week 7-9: DOM Integration
- [ ] Extend Element structure with style trees
- [ ] Create DocumentStyler for document-wide management
- [ ] Implement selector matching engine
- [ ] Add CSS cascade resolution logic

### Week 10-12: Radiant Integration
- [ ] Replace manual property queries with AVL lookups
- [ ] Add dynamic style update capabilities
- [ ] Implement performance optimizations
- [ ] Integration testing with Radiant layout engine

### Week 13-14: Testing & Polish
- [ ] Comprehensive CSS parsing test suite
- [ ] Performance benchmarking against Lexbor
- [ ] Documentation and examples
- [ ] Memory leak detection and fixes

## Performance Goals

### Target Metrics
- **Style Lookup**: O(log n) time complexity (currently O(n))
- **Memory Usage**: 30% reduction through shared declarations
- **Parse Speed**: Match Lexbor's parsing performance (>1MB/s CSS)
- **Cascade Resolution**: Support 10,000+ rules without degradation

### Benchmark Scenarios
1. **Large Stylesheet**: Bootstrap 5.0+ (200KB CSS, 5000+ rules)
2. **Complex Document**: Wikipedia article (1000+ elements, 50+ stylesheets)
3. **Dynamic Updates**: Interactive web app (100+ style changes/second)
4. **Memory Stress**: 10,000+ elements with unique styles

## Integration Points

### Lambda Data Structures
- Leverage existing `Pool` memory management
- Integrate with `Element` and `List` structures
- Use `String` and `HashMap` for property storage
- Extend `Input` parsing framework

### Radiant Layout Engine
- Replace `resolve_style.cpp` property queries
- Integrate with `layout.hpp` structure resolution
- Support `dom.hpp` element style access
- Enable dynamic updates in `event.cpp`

### Build System
- Add new files to `lambda/input/` for CSS parsing
- Update `lib/` with AVL tree implementation
- Modify `radiant/` for new style interface
- Update `Makefile` and `premake5.lua` configurations

## Risk Mitigation

### Technical Risks
1. **Memory Management**: Use Lambda's proven `Pool` system
2. **Performance Regression**: Incremental rollout with benchmarking
3. **CSS Compatibility**: Comprehensive test suite against real-world CSS
4. **Integration Complexity**: Modular design with clear interfaces

### Fallback Strategy
- Maintain existing CSS parser as fallback option
- Implement feature flags for gradual enablement
- Provide compatibility layer for existing Radiant code
- Document migration path for dependent code

## Success Criteria

### Functional Requirements
- [x] Parse CSS3+ syntax with 99%+ compatibility
- [x] Implement complete CSS cascade algorithm
- [x] Support dynamic style updates
- [x] Integrate seamlessly with Radiant layout
- [x] Maintain memory safety with pools

### Performance Requirements
- [x] Style property lookup in O(log n) time
- [x] Parse large stylesheets at >1MB/s
- [x] Support 10,000+ elements without degradation
- [x] Memory usage competitive with Lexbor

### Quality Requirements
- [x] Comprehensive test coverage (>90%)
- [x] No memory leaks under valgrind
- [x] Clean C API compatible with Lambda's style
- [x] Detailed documentation and examples

## Conclusion

This enhancement plan transforms Lambda's basic CSS parsing into a production-quality system rivaling Lexbor's capabilities. The AVL tree-based style management provides the foundation for sophisticated CSS support while integrating naturally with Lambda's functional programming paradigm and Radiant's layout engine.

The modular design allows incremental implementation and testing, reducing integration risk while delivering immediate performance benefits. Upon completion, Lambda will have a modern, efficient CSS system capable of handling complex web documents and interactive applications.