#ifndef DOM_ELEMENT_H
#define DOM_ELEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "../../../lib/avl_tree.h"
#include "../../../lib/arena.h"
#include "../../../lib/strbuf.h"
#include "css_style.hpp"
#include "css_style_node.hpp"
#include "dom_node.hpp"  // Provides DomNodeType enum and utility functions

/**
 * DOM Element Extension for CSS Styling
 *
 * This module extends Lambda's Element structure to support AVL tree-based
 * CSS style management. It provides efficient style resolution, cascade
 * computation, and caching for high-performance rendering.
 *
 * Integration with Radiant:
 * - Uses C++ inheritance for polymorphic node operations
 * - Provides CSS style lookup through AVL trees (O(log n))
 * - Caches computed values for performance
 * - Supports dynamic style updates
 *
 * Note: DomNode is the base class for DomElement, DomText, and DomComment,
 * providing polymorphic DOM tree operations.
 */

// Forward declarations
typedef struct Element Element;
typedef struct Input Input;
typedef struct Arena Arena;
typedef struct ViewTree ViewTree;  // From radiant/view.hpp
typedef struct RadiantState RadiantState;  // From radiant/state_store.h
typedef RadiantState StateStore;  // For backward compatibility
typedef struct Url Url;  // From lib/url.h
typedef struct VectorPathProp VectorPathProp;  // From radiant/view.hpp
typedef struct MultiColumnProp MultiColumnProp;  // From radiant/view.hpp

// Forward declaration for TexNode (unified TeX pipeline)
namespace tex { struct TexNode; }

// ============================================================================
// DOM Document
// ============================================================================

/**
 * DomDocument - Root container for DOM tree
 * Manages memory (arena) and Lambda integration (Input*)
 * Unified document structure (replaces radiant/dom.hpp Document)
 */
struct DomDocument {
    // Lambda integration
    Input* input;                // Lambda Input context for MarkEditor operations
    Pool* pool;                  // Pool for arena chunks
    Arena* arena;                // Memory arena for all DOM node allocations

    // Document content
    Url* url;                    // Document URL
    Element* html_root;          // Parsed HTML tree in Mark notation (Lambda tree)
    DomElement* root;            // Root element of DOM tree (optional)
    int html_version;            // Detected HTML version - maps to HtmlVersion enum

    // CSS stylesheets (for @font-face processing after UiContext init)
    struct CssStylesheet** stylesheets;  // Array of parsed stylesheets
    int stylesheet_count;                // Number of stylesheets
    int stylesheet_capacity;             // Capacity of stylesheet array

    // Layout and state
    ViewTree* view_tree;         // View tree after layout
    StateStore* state;           // Document state (cursor, caret, etc.)

    // Scale system for rendering
    // Layout operates in CSS logical pixels; scaling applied only during rendering
    float given_scale;           // User-specified scale factor (default 1.0), from CLI --scale
    float scale;                 // Final render scale = given_scale × pixel_ratio

    // Viewport meta tag values (from <meta name="viewport" content="...">)
    float viewport_initial_scale;  // initial-scale from viewport meta (default 1.0)
    float viewport_min_scale;      // minimum-scale from viewport meta (default 0.0 = not set)
    float viewport_max_scale;      // maximum-scale from viewport meta (default 0.0 = not set)
    int viewport_width;            // viewport width (0 = device-width, >0 = explicit pixels)
    int viewport_height;           // viewport height (0 = device-height, >0 = explicit pixels)

    // Body transform scale (from CSS transform: scale() on body element)
    float body_transform_scale;    // transform: scale() value from body CSS (default 1.0)

    // Network support (Phase 4 integration)
    struct NetworkResourceManager* resource_manager;  // Network resource coordinator (nullptr for local-only docs)
    double load_start_time;                           // Document load start timestamp (for total timeout)
    bool fully_loaded;                                // True when all network resources complete

    // Constructor
    DomDocument() : input(nullptr), pool(nullptr), arena(nullptr),
                    url(nullptr), html_root(nullptr), root(nullptr), html_version(0),
                    stylesheets(nullptr), stylesheet_count(0), stylesheet_capacity(0),
                    view_tree(nullptr), state(nullptr),
                    given_scale(1.0f), scale(1.0f),
                    viewport_initial_scale(1.0f), viewport_min_scale(0.0f), viewport_max_scale(0.0f),
                    viewport_width(0), viewport_height(0),
                    body_transform_scale(1.0f),
                    resource_manager(nullptr), load_start_time(0.0), fully_loaded(true) {}
};

typedef struct {
    CssEnum outer;
    CssEnum inner;
} DisplayValue;

typedef struct InlineProp InlineProp;
typedef struct FlexItemProp FlexFlowProp;
typedef struct GridItemProp GridItemProp;
typedef struct BoundaryProp BoundaryProp;
typedef struct BlockProp BlockProp;
typedef struct ScrollProp ScrollProp;
typedef struct PositionProp PositionProp;
typedef struct TransformProp TransformProp;
typedef struct FilterProp FilterProp;
typedef struct PseudoContentProp PseudoContentProp;
typedef struct EmbedProp EmbedProp;
typedef struct TableCellProp TableCellProp;
typedef struct TableProp TableProp;
typedef struct FormControlProp FormControlProp;
typedef struct ViewBlock ViewBlock;

// Layout cache (from radiant/layout_cache.hpp)
namespace radiant { struct LayoutCache; }

// CSS Custom Property (CSS Variable) storage
struct CssCustomProp {
    const char* name;       // Variable name (e.g., "--primary-color")
    const CssValue* value;  // Variable value
    CssCustomProp* next;    // Linked list for simple storage
};

/**
 * DomElement - DOM element with integrated CSS styling
 *
 * This structure extends the basic DOM element concept with:
 * - Specified style tree (AVL tree of CSS declarations from rules)
 * - Computed style tree (AVL tree of resolved CSS values)
 * - Version tracking for cache invalidation
 * - Parent/child relationships for inheritance
 */
struct DomElement : DomNode {
    // Basic element information
    Element* native_element;     // Pointer to native Lambda Element
    const char* tag_name;        // Element tag name (cached string)

    // Tree structure (only elements can have children)
    DomNode* first_child;        // First child node (Element, Text, or Comment)
    DomNode* last_child;         // Last child node

    // HTML/CSS style related
    uintptr_t tag_id;            // Tag ID for fast comparison (e.g., HTM_TAG_DIV)
    const char* id;              // Element ID attribute (cached)
    const char** class_names;    // Array of class names (cached)
    int class_count;             // Number of classes
    StyleTree* specified_style;  // Specified values from CSS rules (AVL tree)
    // Pseudo-element styles (::before and ::after)
    StyleTree* before_styles;    // Styles for ::before pseudo-element
    StyleTree* after_styles;     // Styles for ::after pseudo-element
    StyleTree* first_letter_styles;  // Styles for ::first-letter pseudo-element
    // we do not store computed_style;
    // Version tracking for cache invalidation
    uint32_t style_version;      // Incremented when specified styles change
    bool needs_style_recompute;  // Flag indicating computed values are stale
    bool styles_resolved;        // Flag to track if styles resolved in current layout pass
    bool float_prelaid;          // Flag to skip float during normal flow (pre-laid in float pass)
    // pseudo-class state (for :hover, :focus, etc.)
    uint32_t pseudo_state;       // Bitmask of pseudo-class states

    // document reference (provides Arena and Input*)
    DomDocument* doc;            // Parent document (provides arena and input)

    // CSS custom properties (CSS variables)
    struct CssCustomProp* css_variables;  // Hashmap of --var-name: value

    // view related fields
    DisplayValue display;

    // span properties
    FontProp* font;  // font style
    BoundaryProp* bound;  // block boundary properties
    InlineProp* in_line;  // inline specific style properties

    // Item property type indicator (fi, gi, tb, td, form share union)
    enum ItemPropType : uint8_t {
        ITEM_PROP_NONE = 0,
        ITEM_PROP_FLEX = 1,    // fi (FlexItemProp)
        ITEM_PROP_GRID = 2,    // gi (GridItemProp)
        ITEM_PROP_TABLE = 3,   // tb (TableProp)
        ITEM_PROP_CELL = 4,    // td (TableCellProp)
        ITEM_PROP_FORM = 5     // form (FormControlProp)
    } item_prop_type = ITEM_PROP_NONE;

    union {
        FlexItemProp* fi;
        GridItemProp* gi;
        TableProp* tb;  // table specific properties
        TableCellProp* td;  // table cell specific properties
        FormControlProp* form;  // form control properties
    };

    // block properties
    float content_width, content_height;  // width and height of the child content including padding
    BlockProp* blk;  // block specific style properties
    ScrollProp* scroller;  // handles overflow
    // block content related properties for flexbox, image, iframe
    EmbedProp* embed;
    // positioning properties for CSS positioning
    PositionProp* position;
    // CSS transform properties
    TransformProp* transform;
    // CSS filter properties
    FilterProp* filter;
    // CSS multi-column layout properties
    MultiColumnProp* multicol;
    // pseudo-element content and layout state (::before/::after)
    PseudoContentProp* pseudo;
    // vector path for PDF/SVG curve rendering
    VectorPathProp* vpath;
    // Layout cache for avoiding redundant layout computations (Taffy-inspired)
    // Stores up to 9 measurement results + 1 final layout result
    radiant::LayoutCache* layout_cache;

    // TexNode tree for RDT_VIEW_TEXNODE rendering (unified TeX pipeline)
    // When view_type == RDT_VIEW_TEXNODE, this points to the root of the TexNode tree
    // The TexNode tree IS the view tree - no conversion needed
    tex::TexNode* tex_root;

    // Constructor
    DomElement() : DomNode(DOM_NODE_ELEMENT), first_child(nullptr), last_child(nullptr), native_element(nullptr),
        tag_name(nullptr), tag_id(0), id(nullptr),
        class_names(nullptr), class_count(0), specified_style(nullptr),
        before_styles(nullptr), after_styles(nullptr), first_letter_styles(nullptr),
        style_version(0), needs_style_recompute(false), styles_resolved(false), float_prelaid(false),
        pseudo_state(0), doc(nullptr), css_variables(nullptr), display{CSS_VALUE_NONE, CSS_VALUE_NONE},
        font(nullptr), bound(nullptr), in_line(nullptr),
        item_prop_type(ITEM_PROP_NONE), fi(nullptr),
        content_width(0), content_height(0),
        blk(nullptr), scroller(nullptr), embed(nullptr), position(nullptr),
        transform(nullptr), filter(nullptr), multicol(nullptr), pseudo(nullptr),
        vpath(nullptr), layout_cache(nullptr), tex_root(nullptr) {}
};

// Pseudo-class state flags
#define PSEUDO_STATE_HOVER          (1 << 0)
#define PSEUDO_STATE_ACTIVE         (1 << 1)
#define PSEUDO_STATE_FOCUS          (1 << 2)
#define PSEUDO_STATE_VISITED        (1 << 3)
#define PSEUDO_STATE_LINK           (1 << 4)
#define PSEUDO_STATE_ENABLED        (1 << 5)
#define PSEUDO_STATE_DISABLED       (1 << 6)
#define PSEUDO_STATE_CHECKED        (1 << 7)
#define PSEUDO_STATE_INDETERMINATE  (1 << 8)
#define PSEUDO_STATE_VALID          (1 << 9)
#define PSEUDO_STATE_INVALID        (1 << 10)
#define PSEUDO_STATE_REQUIRED       (1 << 11)
#define PSEUDO_STATE_OPTIONAL       (1 << 12)
#define PSEUDO_STATE_READ_ONLY      (1 << 13)
#define PSEUDO_STATE_READ_WRITE     (1 << 14)
#define PSEUDO_STATE_FIRST_CHILD    (1 << 15)
#define PSEUDO_STATE_LAST_CHILD     (1 << 16)
#define PSEUDO_STATE_ONLY_CHILD     (1 << 17)
#define PSEUDO_STATE_FOCUS_VISIBLE  (1 << 18)  // keyboard focus (Tab/arrow keys)
#define PSEUDO_STATE_FOCUS_WITHIN   (1 << 19)  // has focused descendant
#define PSEUDO_STATE_SELECTED       (1 << 20)  // option/selection state
#define PSEUDO_STATE_TARGET         (1 << 21)  // URL fragment target
#define PSEUDO_STATE_PLACEHOLDER_SHOWN (1 << 22)  // input showing placeholder

// ============================================================================
// DOM Document Creation and Destruction
// ============================================================================

/**
 * Create a new DomDocument
 * @param input Lambda Input context (required for MarkEditor operations)
 * @return New DomDocument or NULL on failure
 */
DomDocument* dom_document_create(Input* input);

/**
 * Destroy a DomDocument and all its nodes
 * @param document Document to destroy
 */
void dom_document_destroy(DomDocument* document);

// ============================================================================
// DOM Element Creation and Destruction
// ============================================================================

/**
 * Create a new DomElement
 * @param doc Parent document (provides arena and input)
 * @param tag_name Element tag name (e.g., "div", "span")
 * @param native_element Pointer to backing Lambda Element (required)
 * @return New DomElement or NULL on failure
 */
DomElement* dom_element_create(DomDocument* doc, const char* tag_name, Element* native_element);

/**
 * Destroy a DomElement
 * @param element Element to destroy
 */
void dom_element_destroy(DomElement* element);

/**
 * Initialize a DomElement structure (for stack-allocated elements)
 * @param element Element to initialize
 * @param doc Parent document (provides arena and input)
 * @param tag_name Element tag name
 * @param native_element Pointer to backing Lambda Element (required)
 * @return true on success, false on failure
 */
bool dom_element_init(DomElement* element, DomDocument* doc, const char* tag_name, Element* native_element);

/**
 * Clear all data from a DomElement (without freeing the structure)
 * @param element Element to clear
 */
void dom_element_clear(DomElement* element);

// ============================================================================
// Attribute Management
// ============================================================================

/**
 * Set an element attribute
 * @param element Target element
 * @param name Attribute name
 * @param value Attribute value
 * @return true on success, false on failure
 */
bool dom_element_set_attribute(DomElement* element, const char* name, const char* value);

/**
 * Get an element attribute
 * @param element Target element
 * @param name Attribute name
 * @return Attribute value or NULL if not found
 */
const char* dom_element_get_attribute(DomElement* element, const char* name);

/**
 * Remove an element attribute
 * @param element Target element
 * @param name Attribute name
 * @return true if attribute was removed, false if not found
 */
bool dom_element_remove_attribute(DomElement* element, const char* name);

/**
 * Check if element has an attribute
 * @param element Target element
 * @param name Attribute name
 * @return true if attribute exists, false otherwise
 */
bool dom_element_has_attribute(DomElement* element, const char* name);

/**
 * Get all attribute names from element
 * @param element Target element
 * @param count Output parameter for number of attributes
 * @return Array of attribute names (from element's shape, pool-allocated)
 */
const char** dom_element_get_attribute_names(DomElement* element, int* count);

// ============================================================================
// Class Management
// ============================================================================

/**
 * Add a CSS class to an element
 * @param element Target element
 * @param class_name Class name to add
 * @return true on success, false on failure
 */
bool dom_element_add_class(DomElement* element, const char* class_name);

/**
 * Remove a CSS class from an element
 * @param element Target element
 * @param class_name Class name to remove
 * @return true if class was removed, false if not found
 */
bool dom_element_remove_class(DomElement* element, const char* class_name);

/**
 * Check if element has a CSS class
 * @param element Target element
 * @param class_name Class name to check
 * @return true if class exists, false otherwise
 */
bool dom_element_has_class(DomElement* element, const char* class_name);

/**
 * Toggle a CSS class on an element
 * @param element Target element
 * @param class_name Class name to toggle
 * @return true if class is now present, false if removed
 */
bool dom_element_toggle_class(DomElement* element, const char* class_name);

// ============================================================================
// Inline Style Support
// ============================================================================

/**
 * Parse and apply inline style attribute to an element
 * @param element Target element
 * @param style_text Inline style text (e.g., "color: red; font-size: 14px")
 * @return Number of declarations applied
 */
int dom_element_apply_inline_style(DomElement* element, const char* style_text);

/**
 * Get inline style text from an element
 * @param element Source element
 * @return Inline style text or NULL if none
 */
const char* dom_element_get_inline_style(DomElement* element);

/**
 * Remove inline styles from an element
 * @param element Target element
 * @return true if inline styles were removed, false otherwise
 */
bool dom_element_remove_inline_styles(DomElement* element);

// ============================================================================
// Style Management
// ============================================================================

/**
 * Apply a CSS declaration to an element
 * @param element Target element
 * @param declaration CSS declaration to apply
 * @return true on success, false on failure
 */
bool dom_element_apply_declaration(DomElement* element, CssDeclaration* declaration);

// Timing functions for profiling CSS cascade
void reset_dom_element_timing();
void log_dom_element_timing();

/**
 * Apply a CSS rule to an element
 * @param element Target element
 * @param rule CSS rule with declarations
 * @param specificity Selector specificity for cascade resolution
 * @return Number of declarations applied
 */
int dom_element_apply_rule(DomElement* element, CssRule* rule, CssSpecificity specificity);

/**
 * Apply a CSS rule to a pseudo-element (::before or ::after)
 * @param element Target element (the originating element)
 * @param rule CSS rule with declarations
 * @param specificity Selector specificity for cascade resolution
 * @param pseudo_element Which pseudo-element (PSEUDO_ELEMENT_BEFORE or PSEUDO_ELEMENT_AFTER)
 * @return Number of declarations applied
 */
int dom_element_apply_pseudo_element_rule(DomElement* element, CssRule* rule,
                                          CssSpecificity specificity, int pseudo_element);

/**
 * Get the specified value for a CSS property
 * @param element Target element
 * @param property_id Property to look up
 * @return Specified CSS declaration or NULL if not set
 */
CssDeclaration* dom_element_get_specified_value(DomElement* element, CssPropertyId property_id);

/**
 * Get the specified value for a CSS property on a pseudo-element
 * @param element Target element (the originating element)
 * @param property_id Property to look up
 * @param pseudo_element Which pseudo-element (PSEUDO_ELEMENT_BEFORE or PSEUDO_ELEMENT_AFTER)
 * @return Specified CSS declaration or NULL if not set
 */
CssDeclaration* dom_element_get_pseudo_element_value(DomElement* element,
                                                     CssPropertyId property_id, int pseudo_element);

/**
 * Check if element has ::before pseudo-element content
 * @param element Target element
 * @return true if element has ::before content, false otherwise
 */
bool dom_element_has_before_content(DomElement* element);

/**
 * Check if element has ::after pseudo-element content
 * @param element Target element
 * @return true if element has ::after content, false otherwise
 */
bool dom_element_has_after_content(DomElement* element);

/**
 * Get content string for a pseudo-element
 * @param element Target element
 * @param pseudo_element Which pseudo-element (PSEUDO_ELEMENT_BEFORE or PSEUDO_ELEMENT_AFTER)
 * @return Content string or NULL if none
 */
const char* dom_element_get_pseudo_element_content(DomElement* element, int pseudo_element);

/**
 * Get pseudo-element content with counter resolution
 * Extended version that handles counter() and counters() functions
 * @param counter_context Pointer to CounterContext (from radiant/layout_counters.hpp)
 * @param arena Arena for allocating result string
 */
const char* dom_element_get_pseudo_element_content_with_counters(
    DomElement* element, int pseudo_element, void* counter_context, Arena* arena);

/**
 * Remove a CSS property from an element
 * @param element Target element
 * @param property_id Property to remove
 * @return true if property was removed, false if not found
 */
bool dom_element_remove_property(DomElement* element, CssPropertyId property_id);

// ============================================================================
// Pseudo-Class State Management
// ============================================================================

/**
 * Set a pseudo-class state flag
 * @param element Target element
 * @param pseudo_state Pseudo-state flag(s) to set
 */
void dom_element_set_pseudo_state(DomElement* element, uint32_t pseudo_state);

/**
 * Clear a pseudo-class state flag
 * @param element Target element
 * @param pseudo_state Pseudo-state flag(s) to clear
 */
void dom_element_clear_pseudo_state(DomElement* element, uint32_t pseudo_state);

/**
 * Check if a pseudo-class state is set
 * @param element Target element
 * @param pseudo_state Pseudo-state flag to check
 * @return true if state is set, false otherwise
 */
bool dom_element_has_pseudo_state(DomElement* element, uint32_t pseudo_state);

/**
 * Toggle a pseudo-class state flag
 * @param element Target element
 * @param pseudo_state Pseudo-state flag to toggle
 * @return true if state is now set, false if cleared
 */
bool dom_element_toggle_pseudo_state(DomElement* element, uint32_t pseudo_state);

// ============================================================================
// DOM Tree Navigation
// ============================================================================

/**
 * Get element parent
 * @param element Target element
 * @return Parent element or NULL if none
 */
DomElement* dom_element_get_parent(DomElement* element);

/**
 * Get first child element
 * @param element Target element
 * @return First child or NULL if none
 */
DomElement* dom_element_get_first_child(DomElement* element);
DomElement* dom_element_get_last_child(DomElement* element);

/**
 * Get next sibling element
 * @param element Target element
 * @return Next sibling or NULL if none
 */
DomElement* dom_element_get_next_sibling(DomElement* element);

/**
 * Get previous sibling element
 * @param element Target element
 * @return Previous sibling or NULL if none
 */
DomElement* dom_element_get_prev_sibling(DomElement* element);

/**
 * Link child element to parent in DOM sibling chain only.
 * Use this when the child is ALREADY in the parent's Lambda tree.
 * Does NOT modify the Lambda tree - only updates DOM navigation pointers.
 *
 * Typical use case: Building DOM wrappers from existing Lambda tree structure
 * where parent-child relationships already exist in the Lambda data.
 *
 * @param parent Parent element
 * @param child Child element to link (must already exist in parent's Lambda tree)
 * @return true on success, false on error
 */
bool dom_element_link_child(DomElement* parent, DomElement* child);

/**
 * Append child element to parent, updating BOTH Lambda tree AND DOM sibling chain.
 * Use this when adding a NEW child that is NOT yet in the parent's Lambda tree.
 *
 * Requires both parent and child to have Lambda backing (native_element).
 * Creates a new parent-child relationship in both the Lambda tree structure
 * and the DOM navigation chain.
 *
 * For children already in the Lambda tree, use dom_element_link_child() instead.
 *
 * @param parent Parent element (must have Lambda backing)
 * @param child Child element (must have Lambda backing)
 * @return true on success, false on failure
 */
bool dom_element_append_child(DomElement* parent, DomElement* child);

/**
 * Append text content as backed DomText node
 * Creates Lambda String and adds to parent's children array via MarkEditor
 * @param parent Parent element (must be backed)
 * @param text Text content to append
 * @return New DomText node or NULL on failure
 */
DomText* dom_element_append_text_backed(DomElement* parent, const char* text);

/**
 * Remove a child element
 * @param parent Parent element
 * @param child Child element to remove
 * @return true on success, false if child not found
 */
bool dom_element_remove_child(DomElement* parent, DomElement* child);

/**
 * Insert a child element before another child
 * @param parent Parent element
 * @param new_child Child element to insert
 * @param reference_child Child before which to insert
 * @return true on success, false on failure
 */
bool dom_element_insert_before(DomElement* parent, DomElement* new_child, DomElement* reference_child);

// ============================================================================
// Structural Queries
// ============================================================================

/**
 * Check if element matches a structural position
 * @param element Target element
 * @return true if element is first child of its parent
 */
bool dom_element_is_first_child(DomElement* element);

/**
 * Check if element is last child
 * @param element Target element
 * @return true if element is last child of its parent
 */
bool dom_element_is_last_child(DomElement* element);

/**
 * Check if element is only child
 * @param element Target element
 * @return true if element is only child of its parent
 */
bool dom_element_is_only_child(DomElement* element);

/**
 * Get element's index among siblings (0-based)
 * @param element Target element
 * @return Index among siblings, or -1 if no parent
 */
int dom_element_get_child_index(DomElement* element);

/**
 * Count all children (elements, text nodes, comments)
 * @param element Target element
 * @return Total number of child nodes
 */
int dom_element_child_count(DomElement* element);

/**
 * Count only element children (excludes text nodes and comments)
 * @param element Target element
 * @return Number of child elements
 */
int dom_element_count_child_elements(DomElement* element);

/**
 * Check if element matches nth-child formula
 * @param element Target element
 * @param a Coefficient in an+b formula
 * @param b Constant in an+b formula
 * @return true if element matches nth-child(an+b)
 */
bool dom_element_matches_nth_child(DomElement* element, int a, int b);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Get element statistics
 * @param element Element to analyze
 * @param specified_count Output: number of specified properties
 * @param computed_count Output: number of computed properties
 * @param total_declarations Output: total number of declarations in cascade
 */
void dom_element_get_style_stats(DomElement* element, int* specified_count,
    int* computed_count, int* total_declarations);

/**
 * Clone a DomElement (deep copy)
 * @param source Element to clone
 * @param pool Memory pool for new element
 * @return Cloned element or NULL on failure
 */
DomElement* dom_element_clone(DomElement* source, Pool* pool);

#endif // DOM_ELEMENT_H
