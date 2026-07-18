#ifndef DOM_NODE_HPP
#define DOM_NODE_HPP

#include <stdint.h>
#include <stdbool.h>
#include "../../../lib/mempool.h"
#include "../../../lib/strbuf.h"

// Color type for text color (same as in css_style.hpp)
#ifndef COLOR_TYPE_DEFINED
#define COLOR_TYPE_DEFINED
typedef union {
    uint32_t c;  // 32-bit ABGR color format
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };
} Color;
#endif

/**
 * DOM Node Management Module
 * - DomNode provides common base fields and methods
 * - DomElement, DomText, DomComment inherit from DomNode
 */

// Forward declarations
struct DomElement;
struct DomText;
struct DomComment;
struct DomDocument;
struct ViewState;
typedef struct Arena Arena;
typedef struct String String;  // Lambda String type
typedef struct Element Element;  // Lambda Element type

enum DomNodeType {
    DOM_NODE_ELEMENT = 1,     // Element node
    DOM_NODE_TEXT = 3,        // Text node
    DOM_NODE_COMMENT = 8,     // Comment node
    DOM_NODE_DOCUMENT = 9,    // Document node
    DOM_NODE_DOCTYPE = 10     // DOCTYPE declaration
};

enum DomNodeFlag : uint8_t {
    DOM_NODE_FLAG_TEXT_SYMBOL = 1u << 0,
};

typedef enum {
    RDT_VIEW_NONE = 0,
    RDT_VIEW_TEXT,
    RDT_VIEW_BR,
    RDT_VIEW_MARKER,  // List marker (bullet/number) - renders with fixed width
    // ViewSpan
    RDT_VIEW_INLINE,
    // ViewBlock
    RDT_VIEW_INLINE_BLOCK,
    RDT_VIEW_BLOCK,
    RDT_VIEW_LIST_ITEM,
    RDT_VIEW_TABLE,
    RDT_VIEW_TABLE_ROW_GROUP,
    RDT_VIEW_TABLE_ROW,
    RDT_VIEW_TABLE_CELL,
    RDT_VIEW_TABLE_COLUMN_GROUP,  // Column group (colgroup) - CSS 2.1 §17.5.1 table layer
    RDT_VIEW_TABLE_COLUMN,        // Column (col) - CSS 2.1 §17.5.1 table layer
} ViewType;

typedef struct DomNode DomNode;
typedef DomNode View;
typedef struct ViewElement ViewElement;

/**
 * DomNode - Base struct/class for all DOM nodes
 * Provides common tree structure and node operations
 * Note: This is a plain C++ struct, not a polymorphic class (no virtual methods)
 */
// tier-1: doc-pool, survives relayout
struct DomNode {
    uint32_t id;             // event/state log id, monotonic per document epoch
    DomNodeType node_type;   // Node type discriminator
    DomNode* parent;         // Parent node (nullptr at root)
    DomNode* next_sibling;   // Next sibling node (nullptr if last)
    DomNode* prev_sibling;   // Previous sibling (nullptr if first)

    // view related fields
    ViewType view_type;
    // view always has x, y, wd, hg;  otherwise, it is a property group
    float x, y, width, height;  // (x, y) relative to the BORDER box of parent block, and (width, height) forms the BORDER box of current block
    int inline_line_number;     // block-local atomic-inline line; zero until that view role assigns it
    // Tier-1 weak reference: state-store teardown clears it before the document epoch advances.
    ViewState* view_state_ref;  // canonical per-view state owned by DocState

    // source line number in the original HTML file (0 = not tracked)
    int source_line;

    // Phase 16: incremental layout support
    bool layout_dirty;                // marked for relayout in incremental mode
    uint8_t node_flags;               // compact subtype state; use node accessors
    float layout_height_contribution; // advance_y delta from this node during parent's layout

    // node_name() is for all nodes, including text and comment nodes
    // whereas tag_name is only for element nodes
    const char* node_name() const;

    // returns "tag:line" string for log messages (e.g., "div:42", "img:105")
    // uses a static buffer; not thread-safe. returns node_name() if source_line == 0.
    const char* source_loc() const;

    // type checking helpers
    inline bool is_element() const { return node_type == DOM_NODE_ELEMENT; }
    inline bool is_text() const { return node_type == DOM_NODE_TEXT; }
    inline bool is_comment() const { return node_type == DOM_NODE_COMMENT || node_type == DOM_NODE_DOCTYPE; }

    // safe downcasting helpers (implementations in dom_element.hpp after derived types are defined)
    inline DomElement* as_element() {
        return is_element() ? ((DomElement*)this) : nullptr;
    }

    inline DomText* as_text() {
        return is_text() ? ((DomText*)this) : nullptr;
    }

    inline DomComment* as_comment() {
        return is_comment() ? ((DomComment*)this) : nullptr;
    }

    inline const DomElement* as_element() const {
        return is_element() ? ((const DomElement*)this) : nullptr;
    }

    inline const DomText* as_text() const {
        return is_text() ? ((const DomText*)this) : nullptr;
    }

    inline const DomComment* as_comment() const {
        return is_comment() ? ((const DomComment*)this) : nullptr;
    }

    // static helper for tag name to ID conversion
    static uintptr_t tag_name_to_id(const char* tag_name);
    uintptr_t tag() const;
    unsigned char* text_data() const;
    // Get attribute for element nodes
    const char* get_attribute(const char* attr_name) const;
    // Check if attribute exists (works for boolean attrs stored as ITEM_NULL)
    bool has_attribute(const char* attr_name) const;

    // tree manipulation methods (implementations in dom_node.cpp)
    bool append_child(DomNode* child);
    bool remove_child(DomNode* child);
    bool insert_before(DomNode* new_node, DomNode* ref_node);

    // utility methods (implementations in dom_node.cpp)
    void print(StrBuf* buf = nullptr, int indent = 0) const;
    void free_tree();

    // view related methods =========================================
    inline View* next() { return (View*)next_sibling; }
    inline View* prev_placed_view() {
        View* prev_view = (View*)this->prev_sibling;
        while (prev_view) {
            if (prev_view->view_type) { return prev_view; }
            prev_view = (View*)prev_view->prev_sibling;
        }
        return nullptr;
    }
    inline ViewElement* parent_view() { return (ViewElement*)this->parent; }

    inline bool is_group() { return view_type >= RDT_VIEW_INLINE; }

    inline bool is_inline() {
        return view_type == RDT_VIEW_TEXT || view_type == RDT_VIEW_INLINE || view_type == RDT_VIEW_INLINE_BLOCK;
    }

    inline bool is_block() {
        return view_type == RDT_VIEW_BLOCK || view_type == RDT_VIEW_INLINE_BLOCK || view_type == RDT_VIEW_LIST_ITEM ||
            view_type == RDT_VIEW_TABLE || view_type == RDT_VIEW_TABLE_ROW_GROUP || view_type == RDT_VIEW_TABLE_ROW || view_type == RDT_VIEW_TABLE_CELL ||
            view_type == RDT_VIEW_TABLE_COLUMN_GROUP || view_type == RDT_VIEW_TABLE_COLUMN;
    }

    const char* view_name();
};

// ============================================================================
// DOM Text Node API
// ============================================================================

typedef struct TextRect TextRect;
typedef struct FontProp FontProp;

/**
 * Content type for DomText nodes
 * Indicates whether the text node contains plain text or a symbol reference
 */
enum DomTextContentType {
    DOM_TEXT_STRING = 0,    // Plain text (default)
    DOM_TEXT_SYMBOL = 1     // Symbol (HTML entity or emoji shortcode)
};

/**
 * DomText - Text node in DOM tree
 * Represents text content between elements
 *
 * Can contain either:
 * - Plain text (content_type == DOM_TEXT_STRING)
 * - Symbol reference (content_type == DOM_TEXT_SYMBOL)
 *
 * For symbols, the text/native_string fields contain the symbol name,
 * and the actual UTF-8 rendering is resolved at render time via symbol_resolver.
 *
 * Always backed by Lambda String (references chars, no copy).
 * Maintains synchronization with Lambda tree via MarkEditor through parent element.
 */
// tier-1: doc-pool, survives relayout
struct DomText : public DomNode {
    // Text-specific fields (reference to Lambda String)
    const char* text;            // Text content or symbol name (references native_string->chars)
    size_t length;               // Text length or symbol name length
    // Lambda backing (required)
    String* native_string;       // Pointer to backing Lambda String

    // view related fields
    TextRect *rect;  // first text rect
    FontProp *font;  // font for this text

    // Factories rely on zeroed arena/pool storage and write only semantic non-zero fields.
    static DomText* create(String* native_string, DomElement* parent_element);
    static DomText* create_detached(String* native_string, DomDocument* doc);
    static DomText* create_detached_copy(DomDocument* doc, const char* text, size_t len);
    static DomText* create_symbol(const char* name, size_t len, DomElement* parent_element);
    static DomText* create_in(Arena* arena);
    static DomText* create_in(Pool* pool);
    static DomText* create_in(Arena* arena, size_t inline_string_length);

    // Check if this is a symbol node
    bool is_symbol() const { return (node_flags & DOM_NODE_FLAG_TEXT_SYMBOL) != 0; }
    void set_symbol(bool value) {
        if (value) node_flags |= DOM_NODE_FLAG_TEXT_SYMBOL;
        else node_flags &= ~DOM_NODE_FLAG_TEXT_SYMBOL;
    }
};

// ============================================================================
// DomText ↔ String conversion (Phase 2: Unified DOM Tree)
// ============================================================================

// In UI mode, MarkBuilder allocates [DomText][String header][chars...] as one block.
// The String part starts immediately after the DomText struct.

// DomText* → String*: returns pointer to the inline String following DomText
inline String* dom_text_to_string(DomText* dt) {
    return (String*)((char*)dt + sizeof(DomText));
}
inline const String* dom_text_to_string(const DomText* dt) {
    return (const String*)((const char*)dt + sizeof(DomText));
}

// String* → DomText*: reverse conversion (caller must ensure String is preceded by DomText)
inline DomText* string_to_dom_text(String* s) {
    return (DomText*)((char*)s - sizeof(DomText));
}
inline const DomText* string_to_dom_text(const String* s) {
    return (const DomText*)((const char*)s - sizeof(DomText));
}

// Ensure String alignment: sizeof(DomText) must be a multiple of 4 (for String.len uint32_t)
static_assert(sizeof(DomText) % alignof(uint32_t) == 0,
              "DomText size must be aligned for inline String.len field");

// ============================================================================
// DomText Node Creation API
// ============================================================================

/**
 * JS/Jube bridge shim for DomText::create().
 * @param native_string Pointer to Lambda String (will be referenced, not copied)
 * @param parent_element Parent DomElement (provides document context)
 * @return New DomText or NULL on failure
 */
DomText* dom_text_create(String* native_string, DomElement* parent_element);

/**
 * JS/Jube bridge shim for DomText::create_detached().
 * Used by JS createTextNode — the node will be parented later via appendChild/insertBefore.
 * @param native_string Pointer to Lambda String (will be referenced, not copied)
 * @param doc DomDocument (provides arena for allocation)
 * @return New DomText or NULL on failure
 */
DomText* dom_text_create_detached(String* native_string, DomDocument* doc);

/**
 * Copy UTF-8 bytes into a document-arena String. DOM mutation paths share this
 * allocator so live text nodes never retain temporary or JS-heap storage.
 */
String* dom_document_create_string(DomDocument* doc, const char* text, size_t len);

/**
 * Destroy a DomText node
 * @param text_node Text node to destroy
 */
void dom_text_destroy(DomText* text_node);

/**
 * Get text content
 * @param text_node Text node
 * @return Text content string
 */
const char* dom_text_get_content(DomText* text_node);

/**
 * Set text content (backed - updates Lambda String via MarkEditor)
 * @param text_node Text node
 * @param text New text content
 * @return true on success, false on failure
 */
bool dom_text_set_content(DomText* text_node, const char* text);

/**
 * Check if text node is backed by Lambda String (always true now)
 * @param text_node Text node
 * @return true if backed, false otherwise
 */
bool dom_text_is_backed(DomText* text_node);

/**
 * Get child index of text node in parent's Lambda Element
 * Validates cached index and rescans if necessary
 * @param text_node Text node (must be backed)
 * @return Child index or -1 on error
 */
int64_t dom_text_get_child_index(DomText* text_node);

/**
 * Remove text node from parent (syncs with Lambda)
 * Removes from both DOM tree and Lambda Element's children array
 * @param text_node Text node to remove
 * @return true on success, false on failure
 */
bool dom_text_remove(DomText* text_node);

// ============================================================================
// DOM Comment/DOCTYPE Node API
// ============================================================================

/**
 * DomComment - Comment, DOCTYPE, or XML declaration node
 * Represents comments (<!-- -->), DOCTYPE declarations, and XML declarations
 * Always backed by Lambda Element (tag "!--" or "!DOCTYPE")
 */
// tier-1: doc-pool, survives relayout
struct DomComment : public DomNode {
    // Comment-specific fields
    const char* tag_name;        // Node name: "!--" for comments, "!DOCTYPE" for DOCTYPE
    const char* content;         // Full content/text (points to native_element's String child)
    size_t length;               // Content length
    Element* native_element;     // Pointer to backing Lambda Element (tag "!--" or "!DOCTYPE")

    // Factories rely on zeroed arena storage and write only semantic non-zero fields.
    static DomComment* create(Element* native_element, DomElement* parent_element);
    static DomComment* create_detached(Element* native_element, DomDocument* doc);
};

/** Detached bridge factory retained for JS/Jube callers. */
DomComment* dom_comment_create_detached(Element* native_element, DomDocument* doc);

/**
 * Destroy a DomComment node
 * @param comment_node Comment/DOCTYPE node to destroy
 */
void dom_comment_destroy(DomComment* comment_node);

/**
 * Get child index of a comment node in parent's items array
 * @param comment_node Comment node to check
 * @return Index in parent's items array, or -1 if not found
 */
int64_t dom_comment_get_child_index(DomComment* comment_node);

/**
 * Set comment content (updates Lambda Element)
 * @param comment_node Comment node to update
 * @param new_content New content string
 * @return true on success, false on failure
 */
bool dom_comment_set_content(DomComment* comment_node, const char* new_content);

/**
 * Remove a comment node (updates Lambda tree)
 * @param comment_node Comment node to remove
 * @return true on success, false on failure
 */
bool dom_comment_remove(DomComment* comment_node);

/**
 * Check if comment is backed by Lambda Element (always true now)
 * @param comment_node Comment node to check
 * @return true if backed, false otherwise
 */
bool dom_comment_is_backed(DomComment* comment_node);

/**
 * Get comment/DOCTYPE content
 * @param comment_node Comment node
 * @return Content string
 */
const char* dom_comment_get_content(DomComment* comment_node);


#endif // DOM_NODE_HPP
