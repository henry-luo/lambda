#ifndef DOM_ELEMENT_H
#define DOM_ELEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../../../lib/avl_tree.h"
#include "../../../lib/arena.h"
#include "../../../lib/ownership.hpp"
#include "../../../lib/strbuf.h"
#include "css_style.hpp"
#include "css_style_node.hpp"
#include "dom_node.hpp"  // Provides DomNodeType enum and utility functions
#include "../../lambda.hpp"  // Full Element definition (needed for embedded Element field)

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
typedef struct LayoutContext LayoutContext;  // From radiant/layout.hpp
typedef struct DocState DocState;  // From radiant/state_store.h
typedef struct StateStore StateStore;  // From radiant/state_store.hpp
typedef struct Url Url;  // From lib/url.h
typedef struct VectorPathProp VectorPathProp;  // From radiant/view.hpp
typedef struct MultiColumnProp MultiColumnProp;  // From radiant/view.hpp
typedef struct Runtime Runtime;  // From lambda/lambda.h
struct DomElement;

// ============================================================================
// DOM Document
// ============================================================================

typedef enum DomJsMutationKind {
    DOM_JS_MUTATION_UNKNOWN = 0,
    DOM_JS_MUTATION_CHILD_INSERT = 1,
    DOM_JS_MUTATION_CHILD_REMOVE = 2,
    DOM_JS_MUTATION_TEXT = 3,
    DOM_JS_MUTATION_ATTRIBUTE = 4,
    DOM_JS_MUTATION_STYLE = 5,
    DOM_JS_MUTATION_TREE_REPLACE = 6,
    DOM_JS_MUTATION_STYLE_REPAINT = 7
} DomJsMutationKind;

// tier-1: doc-pool, survives relayout
typedef struct DomJsMutationRecord {
    uint32_t sequence;
    DomJsMutationKind kind;
    DomNode* target;
    DomNode* parent;
    uint32_t target_id;
    uint32_t parent_id;
} DomJsMutationRecord;

#define DOM_JS_MUTATION_RECORD_CAP 64

typedef enum DomReconcileMode {
    DOM_RECONCILE_NONE = 0,
    DOM_RECONCILE_INCREMENTAL = 1,
    DOM_RECONCILE_FULL = 2,
    DOM_RECONCILE_RETAINED_FULL_LAYOUT = 3,
    DOM_RECONCILE_DESTRUCTIVE_REBUILD = 4,
    DOM_RECONCILE_DOCUMENT_REBUILD = 5
} DomReconcileMode;

// tier-1: document-owned runtime state
struct DomJsRuntime {
    void* mir_ctx;
    void* preamble_state;
    void* runtime_heap;
    void* runtime_name_pool;
    void* runtime_type_list;
    void* runtime_pool;
    void* doc_node;
    int mutation_count;
    uint32_t mutation_sequence;
    uint32_t mutation_kind_mask;
    int mutation_record_count;
    int mutation_record_overflow;
    DomJsMutationRecord mutation_records[DOM_JS_MUTATION_RECORD_CAP];
    const char* ready_state;

    DomJsRuntime() : mir_ctx(nullptr), preamble_state(nullptr), runtime_heap(nullptr),
        runtime_name_pool(nullptr), runtime_type_list(nullptr), runtime_pool(nullptr),
        doc_node(nullptr), mutation_count(0), mutation_sequence(0), mutation_kind_mask(0),
        mutation_record_count(0), mutation_record_overflow(0), mutation_records{},
        ready_state("complete") {}
};

// tier-1: document-owned viewport and render scale inputs
struct ViewportMeta {
    float given_scale;
    float scale;
    float initial_scale;
    float min_scale;
    float max_scale;
    int width;
    int height;
    float body_transform_scale;

    ViewportMeta() : given_scale(1.0f), scale(1.0f), initial_scale(1.0f),
        min_scale(0.0f), max_scale(0.0f), width(0), height(0),
        body_transform_scale(1.0f) {}
};

// tier-1: last reconciliation result exposed to diagnostics and tests
struct ReconcileLog {
    DomReconcileMode mode;
    const char* reason;
    int mutations;
    int records;
    int record_overflow;

    ReconcileLog() : mode(DOM_RECONCILE_NONE), reason("none"), mutations(0),
        records(0), record_overflow(0) {}
};

// tier-1: opaque services kept out of the public document spine
struct DomDocumentServices {
    void* mem_ctx;
    void* cached_css_engine;
    void* keyframe_registry;
    uint32_t element_count;
    uint32_t ext_allocations;
    uint32_t layout_cache_allocations;

    DomDocumentServices() : mem_ctx(nullptr), cached_css_engine(nullptr),
        keyframe_registry(nullptr), element_count(0), ext_allocations(0),
        layout_cache_allocations(0) {}
};

static inline const char* dom_reconcile_mode_name(DomReconcileMode mode) {
    switch (mode) {
        case DOM_RECONCILE_INCREMENTAL: return "incremental";
        case DOM_RECONCILE_FULL: return "full";
        case DOM_RECONCILE_RETAINED_FULL_LAYOUT: return "retained_full_layout";
        case DOM_RECONCILE_DESTRUCTIVE_REBUILD: return "destructive_rebuild";
        case DOM_RECONCILE_DOCUMENT_REBUILD: return "document_rebuild";
        case DOM_RECONCILE_NONE:
        default: return "none";
    }
}

/**
 * DomDocument - Root container for DOM tree
 * Manages memory (arena) and Lambda integration (Input*)
 * Unified document structure (replaces radiant/dom.hpp Document)
 */
// tier-1: doc-pool, survives relayout
struct DomDocument {
    // Lambda integration
    Input* input;                // Lambda Input context for MarkEditor operations
    Pool* pool;                  // Pool for arena chunks
    Arena* arena;                // Memory arena for all DOM node allocations
    DomDocumentServices services;

    // Document content
    Url* url;                    // Document URL
    Element* html_root;          // Parsed HTML tree in Mark notation (Lambda tree)
    DomElement* root;            // Root element of DOM tree (optional)
    int html_version;            // Detected HTML version - maps to HtmlVersion enum
    uint32_t next_node_id;        // next DomNode id for event/state logs (0 reserved)

    // CSS stylesheets (for @font-face processing after UiContext init)
    struct CssStylesheet** stylesheets;  // Array of parsed stylesheets
    int stylesheet_count;                // Number of stylesheets
    int stylesheet_capacity;             // Capacity of stylesheet array

    // Layout and state
    ViewTree* view_tree;         // View tree after layout
    StateStore* state_store;     // Per-document state store owner
    DocState* state;             // Compatibility pointer to state_store->doc_state

    ViewportMeta viewport;

    // Network support (Phase 4 integration)
    struct NetworkResourceManager* resource_manager;  // Network resource coordinator (nullptr for local-only docs)
    double load_start_time;                           // Document load start timestamp (for total timeout)
    bool fully_loaded;                                // True when all network resources complete

    // Reactive UI: retained Lambda runtime for event handler execution
    Runtime* lambda_runtime;     // Retained runtime (heap, JIT context) for reactive UI sessions

    // Native extensions retain runtime-backed values through document resources.
    struct DomDocumentResource* resources;

    // Reactive UI: cached CSS for rebuild_lambda_doc optimization
    struct CssStylesheet** cached_inline_sheets;  // Parsed inline <style> stylesheets (cached)
    int cached_inline_sheet_count;                // Number of cached inline stylesheets

    // Reactive UI: Element* → DomElement* map for incremental DOM rebuild
    struct hashmap* element_dom_map;              // maps Lambda Element* to its DomElement wrapper

    // Phase 15: Skip blanket styles_resolved reset during incremental layout
    bool skip_style_reset;

    // Phase 16: Incremental layout mode — skip pool recreate, skip clean subtrees
    bool incremental_layout;

    DomJsRuntime js;

    // Last DOM reconcile result. Tests assert this instead of parsing log.txt,
    // so fallback/state-retention coverage can distinguish broad reflow from
    // destructive document rebuild.
    ReconcileLog reconcile;

    // JS/meta requested document navigation. The loader follows this after
    // load-time scripts and refresh metadata have been processed.
    char* pending_navigation_url;

    // Document charset (from <meta charset> or HTTP Content-Type), for CSS fallback encoding
    const char* document_charset;     // e.g. "windows-1251", nullptr means UTF-8

    // JS-requested viewport scroll offsets. Captured after script execution and
    // applied to the root viewport scroller after layout establishes scroll ranges.
    float pending_viewport_scroll_x;
    float pending_viewport_scroll_y;
    DomElement* pending_scroll_into_view_target;

    // Constructor
    DomDocument() : input(nullptr), pool(nullptr), arena(nullptr),
                    url(nullptr), html_root(nullptr), root(nullptr), html_version(0),
                    next_node_id(1),
                    stylesheets(nullptr), stylesheet_count(0), stylesheet_capacity(0),
                    view_tree(nullptr), state_store(nullptr), state(nullptr),
                    resource_manager(nullptr), load_start_time(0.0), fully_loaded(true),
                    lambda_runtime(nullptr), resources(nullptr),
                    cached_inline_sheets(nullptr), cached_inline_sheet_count(0),
                    element_dom_map(nullptr),
                    skip_style_reset(false),
                    incremental_layout(false),
                    pending_navigation_url(nullptr),
                    document_charset(nullptr),
                    pending_viewport_scroll_x(0.0f), pending_viewport_scroll_y(0.0f),
                    pending_scroll_into_view_target(nullptr) {}

    bool init(Input* input);
    void destroy();
};

typedef void (*DomDocumentResourceDestroyFn)(void* data);

// tier-1: doc-pool, survives relayout
typedef struct DomDocumentResource {
    void* data;
    DomDocumentResourceDestroyFn destroy;
    DomDocumentResource* next;
} DomDocumentResource;

bool dom_document_add_resource(DomDocument* document, void* data,
                               DomDocumentResourceDestroyFn destroy);

// tier-1: doc-pool, survives relayout
typedef struct {
    CssEnum outer;
    CssEnum inner;
    bool list_item;  // true when 'list-item' keyword present (generates ::marker)
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

extern const BlockProp BLOCK_PROP_DEFAULT;
extern const BoundaryProp BOUNDARY_PROP_DEFAULT;
extern const FontProp FONT_PROP_DEFAULT;
extern const InlineProp INLINE_PROP_DEFAULT;
extern const ScrollProp SCROLL_PROP_DEFAULT;
extern const PositionProp POSITION_PROP_DEFAULT;
extern const EmbedProp EMBED_PROP_DEFAULT;
extern const TransformProp TRANSFORM_PROP_DEFAULT;
extern const FilterProp FILTER_PROP_DEFAULT;
extern const MultiColumnProp MULTICOL_PROP_DEFAULT;
extern const FlexItemProp FLEX_ITEM_PROP_DEFAULT;
extern const GridItemProp GRID_ITEM_PROP_DEFAULT;
extern const TableProp TABLE_PROP_DEFAULT;
extern const TableCellProp TABLE_CELL_PROP_DEFAULT;
extern const FormControlProp FORM_CONTROL_PROP_DEFAULT;

// tier-1: doc-pool, survives relayout
typedef struct LayoutFragmentBox {
    float x, y, width, height;
    int fragment_index;
    int column_index;
    int row_index;
    struct LayoutFragmentBox* next;
} LayoutFragmentBox;

// Layout cache (from radiant/layout_cache.hpp)
namespace radiant { struct LayoutCache; }

// CSS Custom Property (CSS Variable) storage
// tier-1: doc-pool, survives relayout
struct CssCustomProp {
    const char* name;       // Variable name (e.g., "--primary-color")
    const CssValue* value;  // Variable value
    const char* value_text; // Raw value text for faithful CSSOM serialization
    size_t value_text_len;  // Length of value_text
    CssCustomProp* next;    // Linked list for simple storage
};

enum DomElementFlag : uint32_t {
    ELMT_FLAG_NEEDS_STYLE_RECOMPUTE = 1u << 0,
    ELMT_FLAG_STYLES_RESOLVED = 1u << 1,
    ELMT_FLAG_FLOAT_PRELAID = 1u << 2,
    ELMT_FLAG_HAS_CACHED_INTRINSIC_WIDTHS = 1u << 3,
    ELMT_FLAG_MEASURING_INTRINSIC_WIDTH = 1u << 4,
    ELMT_FLAG_HAS_PENDING_SCROLL_X = 1u << 5,
    ELMT_FLAG_HAS_PENDING_SCROLL_Y = 1u << 6,
    ELMT_FLAG_HAS_INLINE_FRAGMENT_UNION = 1u << 7,
    ELMT_FLAG_HAS_ANCESTOR_FRAGMENT_UNION = 1u << 8,
    ELMT_FLAG_HAS_COLLAPSED_LINE_FRAGMENT_UNION = 1u << 9,
    ELMT_FLAG_HAS_SPLIT_INLINE_FRAGMENT_UNION = 1u << 10,
    ELMT_FLAG_PARENT_ITEM_KIND_SHIFT = 11,
    ELMT_FLAG_PARENT_ITEM_KIND_MASK = 3u << ELMT_FLAG_PARENT_ITEM_KIND_SHIFT,
    ELMT_FLAG_ROLE_KIND_SHIFT = 13,
    ELMT_FLAG_ROLE_KIND_MASK = 7u << ELMT_FLAG_ROLE_KIND_SHIFT,
    ELMT_FLAG_SYNTHETIC = 1u << 16,
};

enum FragmentUnionKind : uint8_t {
    FRAGMENT_UNION_INLINE = 0,
    FRAGMENT_UNION_ANCESTOR,
    FRAGMENT_UNION_COLLAPSED_LINE,
    FRAGMENT_UNION_SPLIT_INLINE,
    FRAGMENT_UNION_COUNT,
};

enum PseudoStyleKind : uint8_t {
    PSEUDO_STYLE_BEFORE = 0,
    PSEUDO_STYLE_AFTER,
    PSEUDO_STYLE_FIRST_LETTER,
    PSEUDO_STYLE_MARKER,
    PSEUDO_STYLE_PLACEHOLDER,
    PSEUDO_STYLE_COUNT,
};

// tier-1: doc-pool, survives relayout
struct FragmentUnion {
    float min_x, max_x, min_y, max_y;
};

// tier-1: doc-pool, survives relayout
struct DomElementExt {
    FragmentUnion frags[FRAGMENT_UNION_COUNT];
    uint8_t fragment_presence_mask;
    StyleTree* pseudo_styles[PSEUDO_STYLE_COUNT];
    MultiColumnProp* multicol;
    VectorPathProp* vpath;
    FilterProp* backdrop_filter;
    void* custom_layout_paint;
    LayoutFragmentBox* layout_fragments;
    int layout_fragment_count;
    DomElement* shadow_host;
    DomElement* shadow_root;
    float pending_element_scroll_x;
    float pending_element_scroll_y;
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
// tier-1: doc-pool, survives relayout
struct DomElement : DomNode {
    // Factories rely on zeroed arena/pool storage and write only semantic non-zero fields.
    static DomElement* create(DomDocument* doc, const char* tag_name, Element* backing);
    static DomElement* create_in(Arena* arena);
    static DomElement* create_in(Pool* pool);
    static DomElement* create_in(DomElement* storage, DomDocument* doc,
                                 const char* tag_name, Element* backing);

    // Resolved props point into the shorter-lived view pool by unified-tree design;
    // every relayout resets and rebuilds them before consumers may read the tree.
    // === Embedded Lambda Element (at known offset from DomNode base) ===
    // In UI mode, this IS the Lambda Element. Otherwise, data is copied from the
    // original Element during create(). MarkEditor operates on
    // this embedded value, so mutations happen in-place.
    Element elmt;

    // Basic element information
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
    DomElementExt* ext;          // rare DOM/view state, allocated lazily from doc pool
    // we do not store computed_style;
    // Version tracking for cache invalidation
    uint32_t style_version;      // Incremented when specified styles change
    uint32_t elmt_flags;         // compact element state; use the accessors below
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

    // CSS Text soft hyphen fragments can contribute to an inline element's
    // border-box union without producing an additional DOM text rect.

    // collapsed text can create an anonymous line fragment that affects ancestor
    // inline decorations without contributing to this element's own DOMRect.

    // line-edge collapsible whitespace may leave a zero-width inline fragment
    // on a real line even though its text node has no visible rect.

    // block-in-inline splitting creates anonymous line fragments for every inline
    // ancestor in the split chain. These store the content-area union; each span's
    // own border/padding is applied when its DOMRect is computed.

    enum ParentItemKind : uint8_t {
        PARENT_ITEM_NONE = 0,
        PARENT_ITEM_FLEX = 1,
        PARENT_ITEM_GRID = 2,
    };

    enum RoleKind : uint8_t {
        ROLE_NONE = 0,
        ROLE_TABLE = 1,
        ROLE_CELL = 2,
        ROLE_FORM = 3,
    };

    // Parent-item and own-role storage are independent because CSS permits a
    // table or form control to participate in a flex/grid parent.
    // Reads must enter through the tagged accessors below; direct members are
    // reserved for mutation after the corresponding tag has been established.
    union {
        FlexItemProp* fi;
        GridItemProp* gi;
    };
    union {
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
    // CSS transitions: persistent per-element snapshot of the last-applied used
    // values of transitionable properties, plus back-pointers to running
    // transition instances. Allocated lazily from doc->pool (survives view-pool
    // relayout, unlike in_line/bound/transform which are view-pool allocated).
    // Opaque here (radiant/view.hpp owns the type) to avoid a header dep.
    void* transition_state;
    // CSS filter properties
    FilterProp* filter;
    // CSS backdrop-filter properties
    // pseudo-element content and layout state (::before/::after)
    PseudoContentProp* pseudo;
    // vector path for PDF/SVG curve rendering
    // Layout cache for avoiding redundant layout computations (Taffy-inspired)
    // Stores up to 9 measurement results + 1 final layout result
    radiant::LayoutCache* layout_cache;

    bool flag(DomElementFlag value) const { return (elmt_flags & value) != 0; }
    void set_flag(DomElementFlag value, bool enabled) {
        if (enabled) elmt_flags |= value;
        else elmt_flags &= ~value;
    }

    bool needs_style_recompute() const { return flag(ELMT_FLAG_NEEDS_STYLE_RECOMPUTE); }
    void set_needs_style_recompute(bool value) { set_flag(ELMT_FLAG_NEEDS_STYLE_RECOMPUTE, value); }
    bool styles_resolved() const { return flag(ELMT_FLAG_STYLES_RESOLVED); }
    void set_styles_resolved(bool value) { set_flag(ELMT_FLAG_STYLES_RESOLVED, value); }
    bool float_prelaid() const { return flag(ELMT_FLAG_FLOAT_PRELAID); }
    void set_float_prelaid(bool value) { set_flag(ELMT_FLAG_FLOAT_PRELAID, value); }
    bool has_cached_intrinsic_widths() const { return flag(ELMT_FLAG_HAS_CACHED_INTRINSIC_WIDTHS); }
    void set_has_cached_intrinsic_widths(bool value) { set_flag(ELMT_FLAG_HAS_CACHED_INTRINSIC_WIDTHS, value); }
    bool measuring_intrinsic_width() const { return flag(ELMT_FLAG_MEASURING_INTRINSIC_WIDTH); }
    void set_measuring_intrinsic_width(bool value) { set_flag(ELMT_FLAG_MEASURING_INTRINSIC_WIDTH, value); }
    bool is_synthetic() const { return flag(ELMT_FLAG_SYNTHETIC); }
    void set_synthetic(bool value) { set_flag(ELMT_FLAG_SYNTHETIC, value); }
    bool has_pending_element_scroll_x() const { return flag(ELMT_FLAG_HAS_PENDING_SCROLL_X); }
    void set_has_pending_element_scroll_x(bool value) { set_flag(ELMT_FLAG_HAS_PENDING_SCROLL_X, value); }
    bool has_pending_element_scroll_y() const { return flag(ELMT_FLAG_HAS_PENDING_SCROLL_Y); }
    void set_has_pending_element_scroll_y(bool value) { set_flag(ELMT_FLAG_HAS_PENDING_SCROLL_Y, value); }
    bool has_inline_fragment_union() const { return has_fragment_union(FRAGMENT_UNION_INLINE); }
    void set_has_inline_fragment_union(bool value) { set_has_fragment_union(FRAGMENT_UNION_INLINE, value); }
    bool has_ancestor_fragment_union() const { return has_fragment_union(FRAGMENT_UNION_ANCESTOR); }
    void set_has_ancestor_fragment_union(bool value) { set_has_fragment_union(FRAGMENT_UNION_ANCESTOR, value); }
    bool has_collapsed_line_fragment_union() const { return has_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE); }
    void set_has_collapsed_line_fragment_union(bool value) { set_has_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE, value); }
    bool has_split_inline_fragment_union() const { return has_fragment_union(FRAGMENT_UNION_SPLIT_INLINE); }
    void set_has_split_inline_fragment_union(bool value) { set_has_fragment_union(FRAGMENT_UNION_SPLIT_INLINE, value); }

    ParentItemKind parent_item_kind() const {
        return (ParentItemKind)((elmt_flags & ELMT_FLAG_PARENT_ITEM_KIND_MASK) >>
                                ELMT_FLAG_PARENT_ITEM_KIND_SHIFT);
    }
    void set_parent_item_kind(ParentItemKind kind) {
        elmt_flags = (elmt_flags & ~ELMT_FLAG_PARENT_ITEM_KIND_MASK) |
                     ((uint32_t)kind << ELMT_FLAG_PARENT_ITEM_KIND_SHIFT);
    }
    RoleKind role_kind() const {
        return (RoleKind)((elmt_flags & ELMT_FLAG_ROLE_KIND_MASK) >> ELMT_FLAG_ROLE_KIND_SHIFT);
    }
    void set_role_kind(RoleKind kind) {
        elmt_flags = (elmt_flags & ~ELMT_FLAG_ROLE_KIND_MASK) |
                     ((uint32_t)kind << ELMT_FLAG_ROLE_KIND_SHIFT);
    }
    int item_prop_debug_kind() const {
        return role_kind() != ROLE_NONE ? 2 + (int)role_kind() : (int)parent_item_kind();
    }
    FlexItemProp* flex_item() const { return parent_item_kind() == PARENT_ITEM_FLEX ? fi : nullptr; }
    GridItemProp* grid_item() const { return parent_item_kind() == PARENT_ITEM_GRID ? gi : nullptr; }
    TableProp* table_prop() const { return role_kind() == ROLE_TABLE ? tb : nullptr; }
    TableCellProp* cell_prop() const { return role_kind() == ROLE_CELL ? td : nullptr; }
    FormControlProp* form_control() const { return role_kind() == ROLE_FORM ? form : nullptr; }

    const BlockProp* block() const;
    const BoundaryProp* boundary() const;
    const FontProp* fontp() const;
    const InlineProp* inl() const;
    const ScrollProp* scroll() const;
    const PositionProp* positionp() const;
    const EmbedProp* embedp() const;
    const TransformProp* transformp() const;
    const FilterProp* filterp() const;
    BlockProp* block_mut();
    BoundaryProp* boundary_mut();
    FontProp* font_mut();
    InlineProp* inline_mut();
    ScrollProp* scroll_mut();
    PositionProp* position_mut();
    EmbedProp* embed_mut();
    TransformProp* transform_mut();
    FilterProp* filter_mut();

    BlockProp* ensure_block(ViewTree* tree);
    BoundaryProp* ensure_boundary(ViewTree* tree);
    FontProp* ensure_font(ViewTree* tree);
    InlineProp* ensure_inline(ViewTree* tree);
    ScrollProp* ensure_scroll(ViewTree* tree);
    PositionProp* ensure_position(ViewTree* tree);
    EmbedProp* ensure_embed(ViewTree* tree);
    TransformProp* ensure_transform(ViewTree* tree);
    FilterProp* ensure_filter(ViewTree* tree);
    MultiColumnProp* ensure_multicol(ViewTree* tree);
    FlexItemProp* ensure_flex_item(ViewTree* tree);
    GridItemProp* ensure_grid_item(ViewTree* tree);
    TableProp* ensure_table(ViewTree* tree);
    TableCellProp* ensure_cell(ViewTree* tree);
    FormControlProp* ensure_form(ViewTree* tree);

    BoundaryProp* ensure_boundary(LayoutContext* lycon);
    BlockProp* ensure_block(LayoutContext* lycon);
    FontProp* ensure_font(LayoutContext* lycon);
    InlineProp* ensure_inline(LayoutContext* lycon);
    ScrollProp* ensure_scroll(LayoutContext* lycon);
    PositionProp* ensure_position(LayoutContext* lycon);
    EmbedProp* ensure_embed(LayoutContext* lycon);
    TransformProp* ensure_transform(LayoutContext* lycon);
    FilterProp* ensure_filter(LayoutContext* lycon);
    MultiColumnProp* ensure_multicol(LayoutContext* lycon);

    bool set_attribute(const char* name, const char* value);
    const char* get_attribute(const char* name);
    bool remove_attribute(const char* name);
    bool has_attribute(const char* name);
    const char** attribute_names(int* count);
    bool add_class(const char* class_name);
    bool remove_class(const char* class_name);
    bool has_class(const char* class_name) const;
    bool toggle_class(const char* class_name);
    DomElement* parent_element() const;
    DomElement* first_child_element() const;
    DomElement* last_child_element() const;
    DomElement* next_sibling_element() const;
    DomElement* prev_sibling_element() const;
    using DomNode::append_child;
    using DomNode::insert_before;
    using DomNode::remove_child;
    bool link_child(DomElement* child);
    bool append_child(DomElement* child);
    bool remove_child(DomElement* child);
    bool insert_before(DomElement* new_child, DomElement* reference_child);
    bool is_first_child();
    bool is_last_child();
    bool is_only_child();
    int child_index();
    int child_count();
    int count_child_elements();
    bool matches_nth_child(int a, int b);
    DomText* append_text(const char* text_content);
    DomComment* append_comment(const char* comment_content);

    DomElementExt* ensure_ext() {
        if (!ext && doc && doc->pool) {
            ext = (DomElementExt*)pool_calloc(doc->pool, sizeof(DomElementExt));
            if (ext) doc->services.ext_allocations++;
        }
        return ext;
    }
    StyleTree* pseudo_style(PseudoStyleKind kind) const { return ext ? ext->pseudo_styles[kind] : nullptr; }
    StyleTree** pseudo_style_slot(PseudoStyleKind kind) {
        DomElementExt* value = ensure_ext();
        return value ? &value->pseudo_styles[kind] : nullptr;
    }
    void set_pseudo_style(PseudoStyleKind kind, StyleTree* style) {
        if (style || ext) *pseudo_style_slot(kind) = style;
    }
    bool has_fragment_union(FragmentUnionKind kind) const {
        return ext && (ext->fragment_presence_mask & (1u << kind));
    }
    void set_has_fragment_union(FragmentUnionKind kind, bool value) {
        if (!value && !ext) return;
        DomElementExt* data = ensure_ext();
        if (value) data->fragment_presence_mask |= (1u << kind);
        else data->fragment_presence_mask &= ~(1u << kind);
    }
    const FragmentUnion* fragment_union(FragmentUnionKind kind) const {
        return ext ? &ext->frags[kind] : nullptr;
    }
    FragmentUnion* ensure_fragment_union(FragmentUnionKind kind) {
        DomElementExt* data = ensure_ext();
        return data ? &data->frags[kind] : nullptr;
    }
    MultiColumnProp* multicol_prop() const { return ext ? ext->multicol : nullptr; }
    void set_multicol_prop(MultiColumnProp* value) { if (value || ext) ensure_ext()->multicol = value; }
    VectorPathProp* vector_path() const { return ext ? ext->vpath : nullptr; }
    void set_vector_path(VectorPathProp* value) { if (value || ext) ensure_ext()->vpath = value; }
    FilterProp* backdrop_filter_prop() const { return ext ? ext->backdrop_filter : nullptr; }
    FilterProp** backdrop_filter_slot() { return &ensure_ext()->backdrop_filter; }
    void set_backdrop_filter_prop(FilterProp* value) { if (value || ext) ensure_ext()->backdrop_filter = value; }
    void* custom_layout_paint_prop() const { return ext ? ext->custom_layout_paint : nullptr; }
    void set_custom_layout_paint_prop(void* value) { if (value || ext) ensure_ext()->custom_layout_paint = value; }
    LayoutFragmentBox* layout_fragment_list() const { return ext ? ext->layout_fragments : nullptr; }
    void set_layout_fragment_list(LayoutFragmentBox* value) { if (value || ext) ensure_ext()->layout_fragments = value; }
    int layout_fragments_count() const { return ext ? ext->layout_fragment_count : 0; }
    int& layout_fragments_count_ref() { return ensure_ext()->layout_fragment_count; }
    DomElement* shadow_host_element() const { return ext ? ext->shadow_host : nullptr; }
    void set_shadow_host_element(DomElement* value) { if (value || ext) ensure_ext()->shadow_host = value; }
    DomElement* shadow_root_element() const { return ext ? ext->shadow_root : nullptr; }
    void set_shadow_root_element(DomElement* value) { if (value || ext) ensure_ext()->shadow_root = value; }
    float pending_scroll_x() const { return ext ? ext->pending_element_scroll_x : 0.0f; }
    void set_pending_scroll_x(float value) { ensure_ext()->pending_element_scroll_x = value; }
    float pending_scroll_y() const { return ext ? ext->pending_element_scroll_y : 0.0f; }
    void set_pending_scroll_y(float value) { ensure_ext()->pending_element_scroll_y = value; }
    void reset_view_ext() {
        if (!ext) return;
        // Doc-pooled storage outlives view results, so every relayout must drop
        // geometry and paint produced by the previous view-pool epoch.
        ext->fragment_presence_mask = 0;
        memset(ext->frags, 0, sizeof(ext->frags));
        ext->layout_fragments = nullptr;
        ext->layout_fragment_count = 0;
        ext->custom_layout_paint = nullptr;
    }

};

// ============================================================================
// DomElement ↔ Element conversion (Phase 1: Unified DOM Tree)
// ============================================================================

// DomElement remains non-standard-layout because both its base and derived class
// contain storage. __builtin_offsetof is stable on the supported compilers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

// DomElement* → Element*: returns pointer to the embedded Element within DomElement
inline Element* dom_element_to_element(DomElement* de) { return &de->elmt; }
inline const Element* dom_element_to_element(const DomElement* de) { return &de->elmt; }

// Synthetic layout-only nodes deliberately have no Lambda-tree identity even
// though they carry the same embedded storage for a uniform object layout.
inline Element* dom_element_backing(DomElement* de) {
    return de && !de->is_synthetic() ? dom_element_to_element(de) : nullptr;
}
inline const Element* dom_element_backing(const DomElement* de) {
    return de && !de->is_synthetic() ? dom_element_to_element(de) : nullptr;
}

// Element* → DomElement*: reverse conversion (caller must ensure Element is embedded in a DomElement)
inline DomElement* element_to_dom_element(Element* e) {
    return (DomElement*)((char*)e - offsetof(DomElement, elmt));
}
inline const DomElement* element_to_dom_element(const Element* e) {
    return (const DomElement*)((const char*)e - offsetof(DomElement, elmt));
}

// DomElement* ↔ DomNode*: same address (DomNode is at offset 0 via inheritance)
inline DomNode* dom_element_to_node(DomElement* de) { return static_cast<DomNode*>(de); }
inline DomElement* node_to_dom_element(DomNode* dn) { return static_cast<DomElement*>(dn); }

inline void dom_element_retain_tag_name(DomElement* element, lam::PoolPtr<const char> tag_name) {
    lam::PersistentFieldRef<const char, lam::PoolDomain> field(element->tag_name);
    field.set(tag_name);
}

inline void dom_element_retain_tag_name(DomElement* element, lam::PoolPtr<char> tag_name) {
    dom_element_retain_tag_name(element, lam::borrow_const(tag_name));
}

inline void dom_element_retain_id(DomElement* element, lam::PoolPtr<const char> id) {
    lam::PersistentFieldRef<const char, lam::PoolDomain> field(element->id);
    field.set(id);
}

inline void dom_element_retain_id(DomElement* element, lam::PoolPtr<char> id) {
    dom_element_retain_id(element, lam::borrow_const(id));
}

inline void dom_element_clear_id(DomElement* element) {
    lam::PersistentFieldRef<const char, lam::PoolDomain> field(element->id);
    field.clear();
}

inline void dom_element_retain_class_names(DomElement* element, lam::PoolPtr<const char*> class_names) {
    lam::PersistentFieldRef<const char*, lam::PoolDomain> field(element->class_names);
    field.set(class_names);
}

inline void dom_element_clear_class_names(DomElement* element) {
    lam::PersistentFieldRef<const char*, lam::PoolDomain> field(element->class_names);
    field.clear();
}

// Ensure embedded Element is 8-byte aligned (required for pointer fields in Element)
static_assert(offsetof(DomElement, elmt) % 8 == 0,
              "Embedded Element must be 8-byte aligned within DomElement");

#pragma GCC diagnostic pop

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
#define PSEUDO_STATE_DRAG           (1 << 23)  // element being dragged
#define PSEUDO_STATE_DRAG_OVER      (1 << 24)  // element is a drag-over target

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
 * JS/Jube bridge shim for DomElement::create().
 * @param doc Parent document (provides arena and input)
 * @param tag_name Element tag name (e.g., "div", "span")
 * @param native_element Pointer to backing Lambda Element; null creates a synthetic node
 * @return New DomElement or NULL on failure
 */
DomElement* dom_element_create(DomDocument* doc, const char* tag_name, Element* native_element);

/**
 * Destroy a DomElement
 * @param element Element to destroy
 */
void dom_element_destroy(DomElement* element);

/**
 * Clear all data from a DomElement (without freeing the structure)
 * @param element Element to clear
 */
void dom_element_clear(DomElement* element);

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
