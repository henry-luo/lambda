/**
 * JavaScript DOM API Bridge Implementation
 *
 * Bridges Lambda's Element data model and Radiant's DomElement/DomDocument
 * to provide standard DOM manipulation APIs callable from JIT-compiled JavaScript.
 *
 * Wrapping: DomElement* is stored in a Map struct with a unique type marker
 * pointer (js_dom_type_marker) in the Map::type field, and DomElement* in
 * Map::data. This gives O(1) wrap/unwrap with zero HashMap allocation per node.
 */

#include "js_dom.h"
#include "js_dom_events.h"
#include "js_xhr.h"
#include "js_cssom.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../mark_builder.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/url.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_style_node.hpp"
#include "../input/css/css_formatter.hpp"
#include "../input/css/selector_matcher.hpp"
#include "../input/html5/html5_parser.h"

#include <cstring>
#include <cctype>
#include "../../lib/mem.h"

// Forward declarations
extern "C" void heap_register_gc_root(uint64_t* slot);
extern Input* js_input;
static void js_camel_to_css_prop(const char* js_prop, char* css_buf, size_t buf_size);
static CssDeclaration* js_match_element_property(DomElement* elem, CssPropertyId prop_id, int pseudo_type);
static CssDeclaration* js_match_custom_property(DomElement* elem, const char* prop_name);
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);

// ============================================================================
// Unique type marker for DOM-wrapped Maps
// ============================================================================

// Sentinel used in Map::type to distinguish DOM wrappers from regular Maps.
// Address uniqueness is all that matters; the content is unused.
static TypeMap js_dom_type_marker = {};

// Sentinel used in Map::type to distinguish computed style wrappers.
// Map::data stores the DomElement*, Map::data_cap stores pseudo-element type (0=none, 1=before, 2=after).
static TypeMap js_computed_style_marker = {};

// Sentinel used in Map::type to distinguish document proxy objects.
// Map::data is unused (the document is accessed via _js_current_document).
static TypeMap js_document_proxy_marker = {};

// Cached singleton document proxy object
static Item js_document_proxy_item = {.item = ITEM_NULL};

// ============================================================================
// Thread-local DOM document context
// ============================================================================

static __thread DomDocument* _js_current_document = nullptr;

// Helper: increment DOM mutation counter on current document
static inline void js_dom_mutation_notify() {
    if (_js_current_document) {
        _js_current_document->js_mutation_count++;
    }
}

/**
 * Reset JS DOM state for batch mode. Clears cached document proxy and
 * document pointer so next file starts fresh.
 */
extern "C" void js_dom_batch_reset() {
    js_document_proxy_item = (Item){.item = ITEM_NULL};
    _js_current_document = nullptr;
    js_dom_events_reset();
    js_xhr_reset();
}

// ============================================================================
// Named element access on Window (HTML spec: named access on Window object)
// Walks DOM tree, registers elements with id as global properties
// ============================================================================

static void register_named_elements_recursive(DomElement* elem, Item global) {
    if (!elem) return;

    if (elem->id && elem->id[0] != '\0') {
        Item key = (Item){.item = s2it(heap_create_name(elem->id))};
        Item wrapped = js_dom_wrap_element(elem);
        js_property_set(global, key, wrapped);
        log_debug("js_dom: registered element id='%s' on global object", elem->id);
    }

    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_element()) {
            register_named_elements_recursive(child->as_element(), global);
        }
        child = child->next_sibling;
    }
}

void js_dom_register_named_elements(DomElement* root) {
    if (!root) return;
    Item global = js_get_global_this();
    register_named_elements_recursive(root, global);
}

// ============================================================================
// DOM Context Management
// ============================================================================

extern "C" void js_dom_set_document(void* dom_doc) {
    _js_current_document = (DomDocument*)dom_doc;
    if (dom_doc) {
        DomDocument* doc = (DomDocument*)dom_doc;
        if (doc->pool) {
            css_property_system_init(doc->pool);
        }
        // populate global object with element IDs (browser-like named access on Window)
        if (doc->root) {
            js_dom_register_named_elements(doc->root);
        }
    }
    log_debug("js_dom_set_document: set document=%p", dom_doc);
}

extern "C" void* js_dom_get_document(void) {
    return (void*)_js_current_document;
}

// ============================================================================
// DOM Wrapping / Unwrapping
// ============================================================================

extern "C" Item js_dom_wrap_element(void* dom_elem) {
    if (!dom_elem) return ItemNull;

    DomNode* node = (DomNode*)dom_elem;
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_DOM;
    wrapper->type = (void*)&js_dom_type_marker;  // DOM marker
    wrapper->data = dom_elem;                     // store DomNode* directly
    wrapper->data_cap = 0;

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        log_debug("js_dom_wrap_element: wrapped DomElement tag='%s' as Map=%p",
                  elem->tag_name ? elem->tag_name : "(null)", (void*)wrapper);
    } else if (node->is_text()) {
        log_debug("js_dom_wrap_element: wrapped DomText as Map=%p", (void*)wrapper);
    }

    return (Item){.map = wrapper};
}

extern "C" void* js_dom_unwrap_element(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return nullptr;

    Map* m = item.map;
    if (m->type == (void*)&js_dom_type_marker) {
        return m->data;
    }
    return nullptr;
}

extern "C" bool js_is_dom_node(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_dom_type_marker;
}

// ============================================================================
// Document Proxy Object
// ============================================================================

extern "C" bool js_is_document_proxy(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->map_kind == MAP_KIND_DOC_PROXY;
}

extern "C" Item js_get_document_object_value() {
    if (js_document_proxy_item.item != ITEM_NULL) return js_document_proxy_item;
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_DOC_PROXY;
    wrapper->type = (void*)&js_document_proxy_marker;
    wrapper->data = nullptr;
    wrapper->data_cap = 0;
    js_document_proxy_item = (Item){.map = wrapper};
    heap_register_gc_root(&js_document_proxy_item.item);
    return js_document_proxy_item;
}

// Dispatch method calls on the document proxy object.
// Routes to js_document_method which handles getElementById, querySelector, etc.
extern "C" Item js_document_proxy_method(Item method_name, Item* args, int argc) {
    return js_document_method(method_name, args, argc);
}

// Dispatch property access on the document proxy object.
// Routes to js_document_get_property which handles body, documentElement, etc.
extern "C" Item js_document_proxy_get_property(Item prop_name) {
    return js_document_get_property(prop_name);
}

// Dispatch property set on the document proxy object.
// NOTE: Must use map_put directly instead of js_property_set to avoid
// infinite recursion (js_property_set dispatches back here for MAP_KIND_DOM).
extern "C" Item js_document_proxy_set_property(Item prop_name, Item value) {
    if (get_type_id(prop_name) == LMD_TYPE_STRING) {
        String* s = it2s(prop_name);
        if (s && s->len == 5 && strncmp(s->chars, "title", 5) == 0) {
            // Store title on the proxy Map itself
            if (js_document_proxy_item.item != ITEM_NULL && js_input) {
                map_put(js_document_proxy_item.map, s, value, js_input);
            }
            return value;
        }
        // Allow setting defaultView (used by preamble: document.defaultView = window)
        if (s && s->len == 11 && strncmp(s->chars, "defaultView", 11) == 0) {
            if (js_document_proxy_item.item != ITEM_NULL && js_input) {
                map_put(js_document_proxy_item.map, s, value, js_input);
            }
            return value;
        }
    }
    return ItemNull;
}

// ============================================================================
// Computed Style Wrapping
// ============================================================================

static bool js_is_computed_style(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_computed_style_marker;
}

extern "C" bool js_is_computed_style_item(Item item) {
    return js_is_computed_style(item);
}
// Helper: serialize a CssValue to a string Item
static Item css_value_to_string_item(CssValue* val) {
    if (!val) return (Item){.item = s2it(heap_create_name(""))};

    switch (val->type) {
        case CSS_VALUE_TYPE_KEYWORD: {
            // resolve named color keywords to rgb() for getComputedStyle
            uint8_t r, g, b, a;
            if (css_named_color_to_rgba(val->data.keyword, &r, &g, &b, &a)) {
                char buf[64];
                if (a < 255) {
                    snprintf(buf, sizeof(buf), "rgba(%d, %d, %d, %g)", r, g, b, a / 255.0);
                } else {
                    snprintf(buf, sizeof(buf), "rgb(%d, %d, %d)", r, g, b);
                }
                return (Item){.item = s2it(heap_create_name(buf))};
            }
            const CssEnumInfo* info = css_enum_info(val->data.keyword);
            if (info && info->name) {
                return (Item){.item = s2it(heap_create_name(info->name))};
            }
            break;
        }
        case CSS_VALUE_TYPE_LENGTH: {
            char buf[64];
            const char* unit_str = "px";
            switch (val->data.length.unit) {
                case CSS_UNIT_PX: unit_str = "px"; break;
                case CSS_UNIT_EM: unit_str = "em"; break;
                case CSS_UNIT_REM: unit_str = "rem"; break;
                case CSS_UNIT_PERCENT: unit_str = "%"; break;
                case CSS_UNIT_VW: unit_str = "vw"; break;
                case CSS_UNIT_VH: unit_str = "vh"; break;
                case CSS_UNIT_CM: unit_str = "cm"; break;
                case CSS_UNIT_MM: unit_str = "mm"; break;
                case CSS_UNIT_IN: unit_str = "in"; break;
                case CSS_UNIT_PT: unit_str = "pt"; break;
                case CSS_UNIT_PC: unit_str = "pc"; break;
                default: unit_str = "px"; break;
            }
            double v = val->data.length.value;
            if (v == (int)v) {
                snprintf(buf, sizeof(buf), "%d%s", (int)v, unit_str);
            } else {
                snprintf(buf, sizeof(buf), "%g%s", v, unit_str);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_PERCENTAGE: {
            char buf[64];
            double v = val->data.percentage.value;
            if (v == (int)v) {
                snprintf(buf, sizeof(buf), "%d%%", (int)v);
            } else {
                snprintf(buf, sizeof(buf), "%g%%", v);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_NUMBER: {
            char buf[64];
            if (val->data.number.is_integer) {
                snprintf(buf, sizeof(buf), "%d", (int)val->data.number.value);
            } else {
                snprintf(buf, sizeof(buf), "%g", val->data.number.value);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_STRING:
            if (val->data.string) {
                // for getComputedStyle, string values are returned with quotes
                char buf[256];
                snprintf(buf, sizeof(buf), "\"%s\"", val->data.string);
                return (Item){.item = s2it(heap_create_name(buf))};
            }
            break;
        case CSS_VALUE_TYPE_COLOR: {
            char buf[64];
            snprintf(buf, sizeof(buf), "rgb(%d, %d, %d)",
                     val->data.color.data.rgba.r,
                     val->data.color.data.rgba.g,
                     val->data.color.data.rgba.b);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        default:
            break;
    }
    return (Item){.item = s2it(heap_create_name(""))};
}

extern "C" Item js_get_computed_style(Item elem_item, Item pseudo_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node || !node->is_element()) {
        log_debug("js_get_computed_style: not a DOM element");
        return ItemNull;
    }

    // determine pseudo-element type: 0=none, 1=before, 2=after
    int pseudo_type = 0;
    if (get_type_id(pseudo_item) == LMD_TYPE_STRING || get_type_id(pseudo_item) == LMD_TYPE_SYMBOL) {
        const char* pseudo_str = fn_to_cstr(pseudo_item);
        if (pseudo_str) {
            // handle both "before" and "::before" or ":before"
            while (*pseudo_str == ':') pseudo_str++;
            if (strcmp(pseudo_str, "before") == 0) pseudo_type = 1;
            else if (strcmp(pseudo_str, "after") == 0) pseudo_type = 2;
        }
    }

    // create a computed style wrapper
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_DOM;
    wrapper->type = (void*)&js_computed_style_marker;
    wrapper->data = node->as_element();     // store DomElement*
    wrapper->data_cap = pseudo_type;        // store pseudo type

    log_debug("js_get_computed_style: created wrapper for <%s> pseudo=%d",
              node->as_element()->tag_name ? node->as_element()->tag_name : "?", pseudo_type);

    return (Item){.map = wrapper};
}

// ============================================================================
// CSS var() Resolution for Custom Properties
// ============================================================================

// classify a CSS token for the consecutive-token ambiguity table
enum CssTokenClass {
    TC_IDENT,       // ident, function, url
    TC_AT_KEYWORD,  // at-keyword
    TC_HASH,        // hash
    TC_DIMENSION,   // dimension
    TC_NUMBER,      // number
    TC_PERCENTAGE,  // percentage
    TC_CDC,         // -->
    TC_LPAREN,      // (
    TC_DELIM_HASH,  // # (delimiter)
    TC_DELIM_MINUS, // - (delimiter)
    TC_DELIM_AT,    // @ (delimiter)
    TC_DELIM_DOT,   // . (delimiter)
    TC_DELIM_PLUS,  // + (delimiter)
    TC_DELIM_SLASH, // / (delimiter)
    TC_DELIM_STAR,  // * (delimiter)
    TC_OTHER
};

static CssTokenClass classify_token(const CssToken* tok) {
    switch (tok->type) {
        case CSS_TOKEN_IDENT:
        case CSS_TOKEN_IDENTIFIER:
        case CSS_TOKEN_CUSTOM_PROPERTY:
            return TC_IDENT;
        case CSS_TOKEN_FUNCTION:
        case CSS_TOKEN_VAR_FUNCTION:
        case CSS_TOKEN_CALC_FUNCTION:
        case CSS_TOKEN_COLOR_FUNCTION:
            return TC_IDENT;  // function tokens start with ident
        case CSS_TOKEN_URL:
            return TC_IDENT;  // url() starts like an ident
        case CSS_TOKEN_AT_KEYWORD:
            return TC_AT_KEYWORD;
        case CSS_TOKEN_HASH:
            return TC_HASH;
        case CSS_TOKEN_DIMENSION:
            return TC_DIMENSION;
        case CSS_TOKEN_NUMBER:
            return TC_NUMBER;
        case CSS_TOKEN_PERCENTAGE:
            return TC_PERCENTAGE;
        case CSS_TOKEN_CDC:
            return TC_CDC;
        case CSS_TOKEN_LEFT_PAREN:
            return TC_LPAREN;
        case CSS_TOKEN_DELIM:
            if (tok->data.delimiter == '#') return TC_DELIM_HASH;
            if (tok->data.delimiter == '-') return TC_DELIM_MINUS;
            if (tok->data.delimiter == '@') return TC_DELIM_AT;
            if (tok->data.delimiter == '.') return TC_DELIM_DOT;
            if (tok->data.delimiter == '+') return TC_DELIM_PLUS;
            if (tok->data.delimiter == '/') return TC_DELIM_SLASH;
            if (tok->data.delimiter == '*') return TC_DELIM_STAR;
            if (tok->data.delimiter == '%') return TC_PERCENTAGE; // bare % is percentage-like
            return TC_OTHER;
        default:
            return TC_OTHER;
    }
}

// check if two adjacent tokens need a comment inserted between them
// per CSS Syntax spec §9.2 "would-be ambiguous token pairs"
static bool tokens_need_comment(CssTokenClass left, CssTokenClass right) {
    // ident/function/url + ident/function/url/-/number/%/dim/CDC/()
    if (left == TC_IDENT) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION || right == TC_CDC ||
               right == TC_LPAREN;
    }
    // at-keyword + ident/function/url/-/number/%/dim/CDC
    if (left == TC_AT_KEYWORD) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION || right == TC_CDC;
    }
    // hash + ident/function/url/-/number/%/dim/CDC
    if (left == TC_HASH) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION || right == TC_CDC;
    }
    // dimension + ident/function/url/-/number/%/dim/CDC
    if (left == TC_DIMENSION) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION || right == TC_CDC;
    }
    // # (delimiter) + ident/function/url/-/number/%/dim
    if (left == TC_DELIM_HASH) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION;
    }
    // - (delimiter) + ident/function/url/-/number/%/dim
    if (left == TC_DELIM_MINUS) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION;
    }
    // number + ident/function/url/number/%/dim/%
    if (left == TC_NUMBER) {
        return right == TC_IDENT || right == TC_NUMBER || right == TC_PERCENTAGE ||
               right == TC_DIMENSION;
    }
    // @ (delimiter) + ident/function/url/-
    if (left == TC_DELIM_AT) {
        return right == TC_IDENT || right == TC_DELIM_MINUS;
    }
    // . (delimiter) + number/%/dim
    if (left == TC_DELIM_DOT) {
        return right == TC_NUMBER || right == TC_PERCENTAGE || right == TC_DIMENSION;
    }
    // + (delimiter) + number/%/dim
    if (left == TC_DELIM_PLUS) {
        return right == TC_NUMBER || right == TC_PERCENTAGE || right == TC_DIMENSION;
    }
    // / + *
    if (left == TC_DELIM_SLASH) {
        return right == TC_DELIM_STAR;
    }
    return false;
}

// strip exterior comments from a var()-substituted value
// interior comments (within original token text) are preserved,
// but comments at the boundary of a substituted var() are replaced with /**/
static const char* strip_exterior_comments(const char* text, size_t len, Pool* pool) {
    // find and remove /* ... */ comments at boundaries
    // for now, if the text has comments, we check if they're at the very
    // start or end (from var() boundary) and collapse them to /**/
    // Interior comments are preserved as-is
    return text;  // simplification: handled during var() substitution
}

/**
 * Resolve a custom property value, substituting var() references.
 * Returns a pool-allocated string with all var() references resolved.
 * Inserts /**​/ comments between ambiguous consecutive tokens per CSS spec §9.2.
 *
 * @param elem     The element context for variable lookup
 * @param val_text The raw value text to resolve
 * @param pool     Memory pool for allocations
 * @param depth    Recursion depth to prevent infinite loops
 * @return Resolved string, or NULL on failure
 */
static const char* js_resolve_custom_property_value(DomElement* elem, const char* val_text, Pool* pool, int depth) {
    if (!val_text || !pool || depth > 10) return val_text;  // max recursion depth

    // quick check: does this value contain var(?
    if (!strstr(val_text, "var(")) return val_text;

    size_t len = strlen(val_text);
    StringBuf* result = stringbuf_new(pool);
    if (!result) return val_text;

    // we'll collect resolved segments, then do token-pair analysis
    // first pass: find and resolve all var() references
    size_t i = 0;

    // we need to collect the resolved text segments for token-pair analysis
    // strategy: build result by scanning for var(--xxx) patterns
    //   - text before var() is literal
    //   - var(--xxx) is replaced with the resolved value of --xxx
    //   - var(--xxx, fallback) uses fallback if --xxx is not defined

    // Track segments for comment insertion between var() boundaries
    struct Segment {
        const char* text;
        size_t len;
        bool from_var;  // true if this segment came from var() substitution
    };
    Segment segments[64];
    int seg_count = 0;

    while (i < len && seg_count < 63) {
        // look for var(
        const char* var_start = strstr(val_text + i, "var(");
        if (!var_start) {
            // no more var() — rest is literal
            if (i < len) {
                segments[seg_count].text = val_text + i;
                segments[seg_count].len = len - i;
                segments[seg_count].from_var = false;
                seg_count++;
            }
            break;
        }

        // literal text before var(
        size_t literal_len = var_start - (val_text + i);
        if (literal_len > 0) {
            // strip trailing exterior comments at the var() boundary per CSS spec
            const char* lit_start = val_text + i;
            size_t adj_len = literal_len;
            while (adj_len >= 4) {
                // find last */ in the segment
                size_t end_pos = adj_len;
                // check if segment ends with */  (possibly followed by whitespace)
                size_t check = adj_len;
                while (check > 0 && (lit_start[check-1] == ' ' || lit_start[check-1] == '\t'))
                    check--;
                if (check >= 2 && lit_start[check-2] == '*' && lit_start[check-1] == '/') {
                    // find matching /* backwards — but must NOT be inside a string
                    size_t search = check - 2;
                    bool found = false;
                    while (search > 0) {
                        search--;
                        if (lit_start[search] == '/' && search + 1 < check - 2 && lit_start[search + 1] == '*') {
                            adj_len = search;
                            // trim trailing whitespace after removing comment
                            while (adj_len > 0 && (lit_start[adj_len-1] == ' ' || lit_start[adj_len-1] == '\t'))
                                adj_len--;
                            found = true;
                            break;
                        }
                    }
                    if (!found) break;
                } else {
                    break;
                }
            }
            segments[seg_count].text = lit_start;
            segments[seg_count].len = adj_len;
            segments[seg_count].from_var = false;
            seg_count++;
        }

        // parse var(--name) or var(--name, fallback)
        const char* p = var_start + 4;  // skip "var("

        // skip whitespace
        while (*p == ' ' || *p == '\t') p++;

        // extract variable name (must start with --)
        if (p[0] != '-' || p[1] != '-') {
            // not a valid var() — treat as literal
            segments[seg_count].text = var_start;
            segments[seg_count].len = 4;  // "var("
            segments[seg_count].from_var = false;
            seg_count++;
            i = (var_start - val_text) + 4;
            continue;
        }

        const char* name_start = p;
        while (*p && *p != ')' && *p != ',') p++;

        size_t name_len = p - name_start;
        // trim trailing whitespace from name
        while (name_len > 0 && (name_start[name_len-1] == ' ' || name_start[name_len-1] == '\t'))
            name_len--;

        char var_name[128];
        if (name_len >= sizeof(var_name)) name_len = sizeof(var_name) - 1;
        memcpy(var_name, name_start, name_len);
        var_name[name_len] = '\0';

        // check for fallback
        const char* fallback = nullptr;
        size_t fallback_len = 0;
        if (*p == ',') {
            p++; // skip comma
            // skip whitespace
            while (*p == ' ' || *p == '\t') p++;
            fallback = p;
            // find matching closing paren, accounting for nested parens
            int paren_depth = 1;
            while (*p && paren_depth > 0) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') { paren_depth--; if (paren_depth == 0) break; }
                p++;
            }
            fallback_len = p - fallback;
            // trim trailing whitespace from fallback
            while (fallback_len > 0 && (fallback[fallback_len-1] == ' ' || fallback[fallback_len-1] == '\t'))
                fallback_len--;
        } else {
            // skip to closing paren
            int paren_depth = 1;
            while (*p && paren_depth > 0) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') { paren_depth--; if (paren_depth == 0) break; }
                p++;
            }
        }

        if (*p == ')') p++; // skip closing paren

        // resolve the variable
        CssDeclaration* var_decl = js_match_custom_property(elem, var_name);
        const char* resolved = nullptr;
        size_t resolved_len = 0;

        if (var_decl) {
            if (var_decl->value_text && var_decl->value_text_len > 0) {
                resolved = var_decl->value_text;
                resolved_len = var_decl->value_text_len;
            } else if (var_decl->value) {
                CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
                if (fmt) {
                    css_format_value(fmt, var_decl->value);
                    String* s = stringbuf_to_string(fmt->output);
                    if (s && s->chars) {
                        resolved = s->chars;
                        resolved_len = s->len;
                    }
                }
            }
        }

        // trim whitespace from resolved value
        if (resolved) {
            while (resolved_len > 0 && (*resolved == ' ' || *resolved == '\t')) {
                resolved++;
                resolved_len--;
            }
            while (resolved_len > 0 && (resolved[resolved_len-1] == ' ' || resolved[resolved_len-1] == '\t'))
                resolved_len--;
        }

        if (resolved && resolved_len > 0) {
            // recursively resolve nested var() in the resolved value
            char* resolved_copy = (char*)pool_alloc(pool, resolved_len + 1);
            if (resolved_copy) {
                memcpy(resolved_copy, resolved, resolved_len);
                resolved_copy[resolved_len] = '\0';
                const char* nested = js_resolve_custom_property_value(elem, resolved_copy, pool, depth + 1);
                if (nested) {
                    // strip exterior comments from var() result
                    // per spec, comments at boundaries of var() substitution are removed
                    const char* clean = nested;
                    size_t clean_len = strlen(clean);
                    // strip leading comment
                    while (clean_len >= 4 && clean[0] == '/' && clean[1] == '*') {
                        const char* end_comment = strstr(clean + 2, "*/");
                        if (end_comment) {
                            clean = end_comment + 2;
                            clean_len = strlen(clean);
                        } else break;
                    }
                    // strip trailing comment
                    while (clean_len >= 4 && clean[clean_len-1] == '/' && clean[clean_len-2] == '*') {
                        // find the start of this comment by searching backwards for /*
                        size_t j = clean_len - 2;
                        while (j > 0 && !(clean[j] == '/' && clean[j+1] == '*')) j--;
                        if (clean[j] == '/' && clean[j+1] == '*') {
                            clean_len = j;
                        } else break;
                    }
                    segments[seg_count].text = clean;
                    segments[seg_count].len = clean_len;
                    segments[seg_count].from_var = true;
                    seg_count++;
                }
            }
        } else if (fallback && fallback_len > 0) {
            // use fallback value
            char* fb_copy = (char*)pool_alloc(pool, fallback_len + 1);
            if (fb_copy) {
                memcpy(fb_copy, fallback, fallback_len);
                fb_copy[fallback_len] = '\0';
                const char* resolved_fb = js_resolve_custom_property_value(elem, fb_copy, pool, depth + 1);
                segments[seg_count].text = resolved_fb ? resolved_fb : fb_copy;
                segments[seg_count].len = strlen(segments[seg_count].text);
                segments[seg_count].from_var = true;
                seg_count++;
            }
        }
        // else: var() with no value and no fallback — produces nothing (empty)

        i = p - val_text;
    }

    if (seg_count == 0) return "";

    // now concatenate segments with comment insertion between ambiguous token boundaries
    // for segments that come from var() substitution, we need to check the last token
    // of the previous segment against the first token of the next segment
    for (int s = 0; s < seg_count; s++) {
        if (s > 0) {
            // check if we need a comment between previous segment and this one
            // only needed when at least one segment is from var() substitution
            if (segments[s].from_var || segments[s-1].from_var) {
                // get last token of previous segment
                const char* prev_text = segments[s-1].text;
                size_t prev_len = segments[s-1].len;
                const char* cur_text = segments[s].text;
                size_t cur_len = segments[s].len;

                if (prev_len > 0 && cur_len > 0) {
                    // tokenize the last few chars of prev and first few chars of cur
                    // to determine if they'd be ambiguous
                    char* prev_copy = (char*)pool_alloc(pool, prev_len + 1);
                    char* cur_copy = (char*)pool_alloc(pool, cur_len + 1);
                    if (prev_copy && cur_copy) {
                        memcpy(prev_copy, prev_text, prev_len);
                        prev_copy[prev_len] = '\0';
                        memcpy(cur_copy, cur_text, cur_len);
                        cur_copy[cur_len] = '\0';

                        size_t prev_tok_count = 0, cur_tok_count = 0;
                        CssToken* prev_tokens = css_tokenize(prev_copy, prev_len, pool, &prev_tok_count);
                        CssToken* cur_tokens = css_tokenize(cur_copy, cur_len, pool, &cur_tok_count);

                        if (prev_tokens && cur_tokens && prev_tok_count > 0 && cur_tok_count > 0) {
                            // find last non-whitespace token of prev
                            int last_idx = (int)prev_tok_count - 1;
                            while (last_idx >= 0 && prev_tokens[last_idx].type == CSS_TOKEN_WHITESPACE) last_idx--;
                            // skip EOF token
                            while (last_idx >= 0 && prev_tokens[last_idx].type == CSS_TOKEN_EOF) last_idx--;

                            // find first non-whitespace token of cur
                            size_t first_idx = 0;
                            while (first_idx < cur_tok_count && cur_tokens[first_idx].type == CSS_TOKEN_WHITESPACE) first_idx++;

                            if (last_idx >= 0 && first_idx < cur_tok_count &&
                                cur_tokens[first_idx].type != CSS_TOKEN_EOF) {
                                CssTokenClass left_class = classify_token(&prev_tokens[last_idx]);
                                CssTokenClass right_class = classify_token(&cur_tokens[first_idx]);

                                if (tokens_need_comment(left_class, right_class)) {
                                    stringbuf_append_str(result, "/**/");
                                }
                            }
                        }
                    }
                }
            }
        }

        // append segment text
        char* seg_copy = (char*)pool_alloc(pool, segments[s].len + 1);
        if (seg_copy) {
            memcpy(seg_copy, segments[s].text, segments[s].len);
            seg_copy[segments[s].len] = '\0';
            stringbuf_append_str(result, seg_copy);
        }
    }

    String* final_str = stringbuf_to_string(result);
    return (final_str && final_str->chars) ? final_str->chars : "";
}

extern "C" Item js_computed_style_get_property(Item style_item, Item prop_name) {
    if (!js_is_computed_style(style_item)) {
        log_debug("js_computed_style_get_property: not a computed style object");
        return ItemNull;
    }

    Map* wrapper = style_item.map;
    DomElement* elem = (DomElement*)wrapper->data;
    int pseudo_type = (int)wrapper->data_cap;

    if (!elem) return (Item){.item = s2it(heap_create_name(""))};

    const char* js_prop = fn_to_cstr(prop_name);
    if (!js_prop) return (Item){.item = s2it(heap_create_name(""))};

    // handle getPropertyValue method separately
    if (strcmp(js_prop, "getPropertyValue") == 0) {
        // return a function-like marker — handled by method dispatch
        return ItemNull;
    }

    // convert camelCase JS property to CSS hyphenated property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));

    // look up the CSS property ID
    CssPropertyId prop_id = css_property_id_from_name(css_prop);
    if (prop_id == CSS_PROPERTY_UNKNOWN || prop_id == 0) {
        // check for CSS custom properties (--foo)
        // note: css_property_get_id_by_name returns 0 for not-found, CSS_PROPERTY_UNKNOWN is -1
        if (css_prop[0] == '-' && css_prop[1] == '-') {
            // on-demand matching for custom property
            CssDeclaration* decl = js_match_custom_property(elem, css_prop);
            if (decl && (decl->value || decl->value_text)) {
                const char* val = nullptr;

                // prefer raw source text (preserves comments, blocks) unless it
                // contains a backslash (which needs escape resolution via parsed value)
                bool use_raw = decl->value_text && decl->value_text_len > 0
                    && !memchr(decl->value_text, '\\', decl->value_text_len);
                if (use_raw) {
                    val = decl->value_text;
                } else if (decl->value) {
                    Pool* pool = elem->doc ? elem->doc->pool : nullptr;
                    if (!pool) return (Item){.item = s2it(heap_create_name(""))};
                    CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
                    if (!fmt) return (Item){.item = s2it(heap_create_name(""))};
                    css_format_value(fmt, decl->value);
                    String* result = stringbuf_to_string(fmt->output);
                    val = (result && result->chars) ? result->chars : "";
                } else if (decl->value_text && decl->value_text_len > 0) {
                    val = decl->value_text;
                }
                if (!val) val = "";
                // trim leading/trailing whitespace per CSS spec
                while (*val == ' ' || *val == '\t' || *val == '\n' || *val == '\r') val++;
                size_t vlen = strlen(val);
                while (vlen > 0 && (val[vlen-1] == ' ' || val[vlen-1] == '\t' || val[vlen-1] == '\n' || val[vlen-1] == '\r')) vlen--;
                Pool* pool = elem->doc ? elem->doc->pool : nullptr;
                if (pool) {
                    char* trimmed = (char*)pool_alloc(pool, vlen + 1);
                    if (trimmed) { memcpy(trimmed, val, vlen); trimmed[vlen] = '\0'; val = trimmed; }
                }

                // resolve var() references in the value
                if (val && strstr(val, "var(")) {
                    const char* resolved = js_resolve_custom_property_value(elem, val, pool, 0);
                    if (resolved) val = resolved;
                }

                return (Item){.item = s2it(heap_create_name(val))};
            }
            return (Item){.item = s2it(heap_create_name(""))};
        }
        log_debug("js_computed_style_get_property: unknown CSS property '%s' (from JS '%s')",
                  css_prop, js_prop);
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // get the specified (cascaded) value for this property
    CssDeclaration* decl = nullptr;
    if (pseudo_type == 1) {
        decl = dom_element_get_pseudo_element_value(elem, prop_id, 1); // ::before
    } else if (pseudo_type == 2) {
        decl = dom_element_get_pseudo_element_value(elem, prop_id, 2); // ::after
    } else {
        decl = dom_element_get_specified_value(elem, prop_id);
    }

    if (!decl || !decl->value) {
        // return CSS initial/default value for the property
        // CSS spec: 'content' initial value is 'normal'
        //   - on regular elements: computed value is 'normal'
        //   - on ::before/::after pseudo-elements: 'normal' computes to 'none'
        if (prop_id == CSS_PROPERTY_CONTENT) {
            if (pseudo_type == 1 || pseudo_type == 2) {
                return (Item){.item = s2it(heap_create_name("none"))};
            }
            return (Item){.item = s2it(heap_create_name("normal"))};
        }

        // if cascade hasn't happened yet, try on-demand matching
        if (!decl) {
            decl = js_match_element_property(elem, prop_id, pseudo_type);
        }

        if (!decl || !decl->value) {
            // return empty string for unset properties
            return (Item){.item = s2it(heap_create_name(""))};
        }
    }

    // CSS spec: for ::before/::after, content 'normal' computes to 'none'
    if (prop_id == CSS_PROPERTY_CONTENT && (pseudo_type == 1 || pseudo_type == 2)) {
        if (decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
            decl->value->data.keyword == CSS_VALUE_NORMAL) {
            return (Item){.item = s2it(heap_create_name("none"))};
        }
    }

    return css_value_to_string_item(decl->value);
}

// ============================================================================
// On-demand CSS selector matching for getComputedStyle
// ============================================================================

// check if a shorthand property covers the requested longhand
static bool css_shorthand_covers_longhand(CssPropertyId shorthand_id, CssPropertyId longhand_id) {
    switch (shorthand_id) {
        case CSS_PROPERTY_BACKGROUND:
            return longhand_id == CSS_PROPERTY_BACKGROUND_COLOR ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_IMAGE ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_POSITION ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_SIZE ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_REPEAT ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_ATTACHMENT ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_ORIGIN ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_CLIP;
        default:
            return false;
    }
}

/**
 * Match an element against all parsed stylesheets to find a specific property.
 * Used when CSS cascade hasn't happened yet (JS runs before cascade).
 *
 * This performs a mini-cascade for a single element + property:
 * iterates all stylesheet rules, matches selectors, and returns the
 * declaration with highest specificity for the requested property.
 */
static CssDeclaration* js_match_element_property(DomElement* elem, CssPropertyId prop_id, int pseudo_type) {
    if (!elem || !elem->doc) return nullptr;

    DomDocument* doc = elem->doc;
    if (!doc->stylesheets || doc->stylesheet_count <= 0) {
        log_debug("js_match_element_property: no stylesheets available");
        return nullptr;
    }

    Pool* pool = doc->pool;
    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) return nullptr;

    CssDeclaration* best_decl = nullptr;
    CssSpecificity best_spec = {0, 0, 0, 0, false};

    // map pseudo_type (0=none, 1=before, 2=after) to PseudoElementType
    PseudoElementType target_pseudo = PSEUDO_ELEMENT_NONE;
    if (pseudo_type == 1) target_pseudo = PSEUDO_ELEMENT_BEFORE;
    else if (pseudo_type == 2) target_pseudo = PSEUDO_ELEMENT_AFTER;

    for (int s = 0; s < doc->stylesheet_count; s++) {
        CssStylesheet* sheet = doc->stylesheets[s];
        if (!sheet) continue;

        for (size_t r = 0; r < sheet->rule_count; r++) {
            CssRule* rule = sheet->rules[r];
            if (!rule || rule->type != CSS_RULE_STYLE) continue;
            if (rule->data.style_rule.declaration_count == 0) continue;

            // try matching selector(s) against element
            bool matched = false;
            CssSpecificity match_spec = {0, 0, 0, 0, false};
            PseudoElementType matched_pseudo = PSEUDO_ELEMENT_NONE;

            // handle selector group (comma-separated)
            CssSelectorGroup* group = rule->data.style_rule.selector_group;
            CssSelector* single_sel = rule->data.style_rule.selector;

            if (group && group->selector_count > 0) {
                for (size_t si = 0; si < group->selector_count; si++) {
                    CssSelector* sel = group->selectors[si];
                    if (!sel) continue;

                    MatchResult result;
                    if (selector_matcher_matches(matcher, sel, elem, &result)) {
                        matched = true;
                        match_spec = result.specificity;
                        matched_pseudo = result.pseudo_element;
                        break;
                    }
                }
            } else if (single_sel) {
                MatchResult result;
                if (selector_matcher_matches(matcher, single_sel, elem, &result)) {
                    matched = true;
                    match_spec = result.specificity;
                    matched_pseudo = result.pseudo_element;
                }
            }

            if (!matched) continue;

            // check pseudo-element type matches what we're looking for
            if (matched_pseudo != target_pseudo) continue;

            // find the requested property in this rule's declarations
            for (size_t d = 0; d < rule->data.style_rule.declaration_count; d++) {
                CssDeclaration* decl = rule->data.style_rule.declarations[d];
                if (!decl) continue;
                if (decl->property_id != prop_id &&
                    !css_shorthand_covers_longhand(decl->property_id, prop_id)) continue;

                // compare specificity — take highest
                if (!best_decl || css_specificity_compare(match_spec, best_spec) >= 0) {
                    best_decl = decl;
                    best_spec = match_spec;
                }
            }
        }
    }

    if (best_decl) {
        log_debug("js_match_element_property: found property %d for <%s> pseudo=%d via on-demand matching",
                  prop_id, elem->tag_name ? elem->tag_name : "?", pseudo_type);
    }

    return best_decl;
}

/**
 * On-demand matching for CSS custom properties (--variable-name).
 * Matches element against all stylesheets and returns the best-matching
 * declaration for the given custom property name.
 */
static CssDeclaration* js_match_custom_property(DomElement* elem, const char* prop_name) {
    if (!elem || !elem->doc || !prop_name) return nullptr;

    DomDocument* doc = elem->doc;
    Pool* pool = doc->pool;

    CssDeclaration* best_decl = nullptr;
    CssSpecificity best_spec = {0, 0, 0, 0, false};

    // check inline custom properties first (highest specificity: 1,0,0,0)
    // inline styles are stored in elem->css_variables as a linked list
    // created by dom_element_apply_declaration when style.setProperty("--name", value) is called
    if (elem->css_variables) {
        CssCustomProp* prop = elem->css_variables;
        while (prop) {
            if (prop->name && strcmp(prop->name, prop_name) == 0) {
                // create a synthetic CssDeclaration for the inline custom property
                CssDeclaration* decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
                if (decl) {
                    decl->property_name = prop->name;
                    decl->value = (CssValue*)prop->value;
                    decl->value_text = prop->value_text;
                    decl->value_text_len = prop->value_text_len;
                    decl->specificity = {1, 0, 0, 0, false};  // inline style
                    decl->valid = true;
                    best_decl = decl;
                    best_spec = decl->specificity;
                }
                break;  // linked list: first match is the most recent (prepended)
            }
            prop = prop->next;
        }
    }

    // search stylesheets (lower specificity than inline)
    if (doc->stylesheets && doc->stylesheet_count > 0) {
        SelectorMatcher* matcher = selector_matcher_create(pool);
        if (matcher) {
            for (int s = 0; s < doc->stylesheet_count; s++) {
                CssStylesheet* sheet = doc->stylesheets[s];
                if (!sheet) continue;

                for (size_t r = 0; r < sheet->rule_count; r++) {
                    CssRule* rule = sheet->rules[r];
                    if (!rule || rule->type != CSS_RULE_STYLE) continue;
                    if (rule->data.style_rule.declaration_count == 0) continue;

                    bool matched = false;
                    CssSpecificity match_spec = {0, 0, 0, 0, false};

                    CssSelectorGroup* group = rule->data.style_rule.selector_group;
                    CssSelector* single_sel = rule->data.style_rule.selector;

                    if (group && group->selector_count > 0) {
                        for (size_t si = 0; si < group->selector_count; si++) {
                            CssSelector* sel = group->selectors[si];
                            if (!sel) continue;
                            MatchResult result;
                            if (selector_matcher_matches(matcher, sel, elem, &result)) {
                                matched = true;
                                match_spec = result.specificity;
                                break;
                            }
                        }
                    } else if (single_sel) {
                        MatchResult result;
                        if (selector_matcher_matches(matcher, single_sel, elem, &result)) {
                            matched = true;
                            match_spec = result.specificity;
                        }
                    }

                    if (!matched) continue;

                    // find matching custom property by name
                    for (size_t d = 0; d < rule->data.style_rule.declaration_count; d++) {
                        CssDeclaration* decl = rule->data.style_rule.declarations[d];
                        if (!decl || !decl->property_name) continue;
                        if (strcmp(decl->property_name, prop_name) != 0) continue;

                        if (!best_decl || css_specificity_compare(match_spec, best_spec) >= 0) {
                            best_decl = decl;
                            best_spec = match_spec;
                        }
                    }
                }
            }
        }
    }

    return best_decl;
}

// ============================================================================
// Helper: find element by ID (tree walk)
// ============================================================================

static DomElement* dom_find_by_id(DomElement* root, const char* id) {
    if (!root || !id) return nullptr;
    if (root->id && strcmp(root->id, id) == 0) return root;

    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* found = dom_find_by_id(child->as_element(), id);
            if (found) return found;
        }
        child = child->next_sibling;
    }
    return nullptr;
}

// ============================================================================
// Helper: find elements by class name (tree walk, appends to array)
// ============================================================================

static void dom_find_by_class(DomElement* root, const char* cls, Array* arr) {
    if (!root || !cls) return;
    for (int i = 0; i < root->class_count; i++) {
        if (root->class_names[i] && strcmp(root->class_names[i], cls) == 0) {
            array_push(arr, js_dom_wrap_element(root));
            break;
        }
    }
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            dom_find_by_class(child->as_element(), cls, arr);
        }
        child = child->next_sibling;
    }
}

// ============================================================================
// Helper: find elements by tag name (tree walk, appends to array)
// ============================================================================

static void dom_find_by_tag(DomElement* root, const char* tag, Array* arr) {
    if (!root || !tag) return;
    // case-insensitive comparison for HTML tags
    if (root->tag_name && strcasecmp(root->tag_name, tag) == 0) {
        array_push(arr, js_dom_wrap_element(root));
    }
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            dom_find_by_tag(child->as_element(), tag, arr);
        }
        child = child->next_sibling;
    }
}

// ============================================================================
// Helper: CSS selector parse + match
// ============================================================================

static CssSelector* parse_css_selector(const char* sel_text, Pool* pool) {
    if (!sel_text || !pool) return nullptr;
    size_t sel_len = strlen(sel_text);
    if (sel_len == 0) return nullptr;

    size_t token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, sel_len, pool, &token_count);
    if (!tokens || token_count == 0) return nullptr;

    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, (int)token_count, pool);
    return selector;
}

// ============================================================================
// Helper: recursive textContent extraction
// ============================================================================

static void collect_text_content(DomNode* node, StrBuf* sb) {
    if (!node) return;

    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text->text && text->length > 0) {
            strbuf_append_str_n(sb, text->text, (int)text->length);
        }
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        DomNode* child = elem->first_child;
        while (child) {
            collect_text_content(child, sb);
            child = child->next_sibling;
        }
    }
}

// ============================================================================
// Helper: recursive innerHTML serialization
// ============================================================================

static void collect_inner_html(DomNode* node, StrBuf* sb) {
    if (!node) return;

    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text->text && text->length > 0) {
            strbuf_append_str_n(sb, text->text, (int)text->length);
        }
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        // opening tag
        strbuf_append_char(sb, '<');
        strbuf_append_str(sb, elem->tag_name ? elem->tag_name : "unknown");
        // serialize key attributes (id, class)
        if (elem->id) {
            strbuf_append_str(sb, " id=\"");
            strbuf_append_str(sb, elem->id);
            strbuf_append_char(sb, '"');
        }
        if (elem->class_count > 0) {
            strbuf_append_str(sb, " class=\"");
            for (int i = 0; i < elem->class_count; i++) {
                if (i > 0) strbuf_append_char(sb, ' ');
                strbuf_append_str(sb, elem->class_names[i]);
            }
            strbuf_append_char(sb, '"');
        }
        strbuf_append_char(sb, '>');

        // children
        DomNode* child = elem->first_child;
        while (child) {
            collect_inner_html(child, sb);
            child = child->next_sibling;
        }

        // closing tag (skip void elements)
        const char* tag = elem->tag_name;
        if (tag && strcmp(tag, "br") != 0 && strcmp(tag, "hr") != 0 &&
            strcmp(tag, "img") != 0 && strcmp(tag, "input") != 0 &&
            strcmp(tag, "meta") != 0 && strcmp(tag, "link") != 0) {
            strbuf_append_str(sb, "</");
            strbuf_append_str(sb, tag);
            strbuf_append_char(sb, '>');
        }
    }
}

// ============================================================================
// Helper: get uppercase tag name (per DOM spec)
// ============================================================================

static String* uppercase_tag_name(const char* tag_name) {
    if (!tag_name) return heap_create_name("");
    size_t len = strlen(tag_name);
    // allocate temp on stack for short names
    char buf[64];
    char* upper = (len < sizeof(buf)) ? buf : (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    for (size_t i = 0; i < len; i++) {
        upper[i] = (char)toupper((unsigned char)tag_name[i]);
    }
    upper[len] = '\0';
    String* result = heap_create_name(upper);
    if (upper != buf) mem_free(upper);
    return result;
}

// ============================================================================
// Document Method Dispatcher
// ============================================================================

extern "C" Item js_document_method(Item method_name, Item* args, int argc) {
    if (!_js_current_document || !_js_current_document->root) {
        log_error("js_document_method: no document set");
        return ItemNull;
    }

    const char* method = fn_to_cstr(method_name);
    if (!method) {
        log_error("js_document_method: invalid method name");
        return ItemNull;
    }

    DomDocument* doc = _js_current_document;
    DomElement* root = doc->root;

    log_debug("js_document_method: '%s' with %d args", method, argc);

    // getElementById(id)
    if (strcmp(method, "getElementById") == 0) {
        if (argc < 1) return ItemNull;
        const char* id = fn_to_cstr(args[0]);
        if (!id) return ItemNull;
        DomElement* found = dom_find_by_id(root, id);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // getElementsByClassName(className)
    if (strcmp(method, "getElementsByClassName") == 0) {
        if (argc < 1) return ItemNull;
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return ItemNull;
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        dom_find_by_class(root, cls, arr);
        return (Item){.array = arr};
    }

    // getElementsByTagName(tagName)
    if (strcmp(method, "getElementsByTagName") == 0) {
        if (argc < 1) return ItemNull;
        const char* tag = fn_to_cstr(args[0]);
        if (!tag) return ItemNull;
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        dom_find_by_tag(root, tag, arr);
        return (Item){.array = arr};
    }

    // querySelector(selector)
    if (strcmp(method, "querySelector") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text) return ItemNull;

        Pool* pool = doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) {
            // per DOM spec, throw SyntaxError for invalid selectors
            Item err_name = (Item){.item = s2it(heap_create_name("SyntaxError"))};
            Item err_msg = (Item){.item = s2it(heap_create_name("is not a valid selector"))};
            js_throw_value(js_new_error_with_name(err_name, err_msg));
            return ItemNull;
        }

        SelectorMatcher* matcher = selector_matcher_create(pool);
        DomElement* found = selector_matcher_find_first(matcher, selector, root);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // querySelectorAll(selector)
    if (strcmp(method, "querySelectorAll") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text) return ItemNull;

        Pool* pool = doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) {
            // return empty array
            Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
            arr->type_id = LMD_TYPE_ARRAY;
            arr->items = nullptr;
            arr->length = 0;
            arr->capacity = 0;
            return (Item){.array = arr};
        }

        SelectorMatcher* matcher = selector_matcher_create(pool);
        DomElement** results = nullptr;
        int count = 0;
        selector_matcher_find_all(matcher, selector, root, &results, &count);

        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        for (int i = 0; i < count; i++) {
            array_push(arr, js_dom_wrap_element(results[i]));
        }
        return (Item){.array = arr};
    }

    // createElement(tagName)
    if (strcmp(method, "createElement") == 0) {
        if (argc < 1) return ItemNull;
        const char* tag = fn_to_cstr(args[0]);
        if (!tag) return ItemNull;

        // use MarkBuilder to create a proper Lambda Element
        MarkBuilder builder(doc->input);
        Item elem_item = builder.element(tag).final();

        // build DomElement wrapper
        Element* elem = elem_item.element;
        DomElement* dom_elem = dom_element_create(doc, tag, elem);
        return js_dom_wrap_element(dom_elem);
    }

    // createElementNS(namespace, tagName) — treat same as createElement, ignoring namespace
    if (strcmp(method, "createElementNS") == 0) {
        if (argc < 2) return ItemNull;
        // args[0] = namespace URI (ignored), args[1] = qualified tag name
        const char* tag = fn_to_cstr(args[1]);
        if (!tag) return ItemNull;

        MarkBuilder builder(doc->input);
        Item elem_item = builder.element(tag).final();

        Element* elem = elem_item.element;
        DomElement* dom_elem = dom_element_create(doc, tag, elem);
        return js_dom_wrap_element(dom_elem);
    }

    // createTextNode(text)
    if (strcmp(method, "createTextNode") == 0) {
        if (argc < 1) return ItemNull;
        const char* text = fn_to_cstr(args[0]);
        if (!text) return ItemNull;

        // create a detached String-backed DomText node (no parent yet)
        String* str = heap_create_name(text);
        DomText* text_node = dom_text_create_detached(str, doc);
        if (!text_node) {
            log_error("js_document_method: createTextNode failed for '%s'", text);
            return ItemNull;
        }
        return js_dom_wrap_element(text_node);
    }

    // normalize() — delegate to root element's normalize
    if (strcmp(method, "normalize") == 0) {
        if (doc->root) {
            Item root_item = js_dom_wrap_element(doc->root);
            js_dom_element_method(root_item, (Item){.item = s2it(heap_create_name("normalize"))}, nullptr, 0);
        }
        return ItemNull;
    }

    // document.write(text) / document.writeln(text)
    // Appends the text content to the document body as a text node.
    // Note: real document.write parses HTML and inserts at the current parse point,
    // but for CSS2.1 tests this simplified approach works since tests only write
    // simple text ("PASS", "FAIL", CSS property values, etc.)
    if (strcmp(method, "write") == 0 || strcmp(method, "writeln") == 0) {
        if (argc < 1) return ItemNull;
        const char* text = fn_to_cstr(args[0]);
        if (!text) return ItemNull;

        // find <body> element
        DomElement* body = nullptr;
        DomNode* child = doc->root->first_child;
        while (child) {
            if (child->is_element()) {
                DomElement* e = child->as_element();
                if (e->tag_name && strcmp(e->tag_name, "body") == 0) {
                    body = e;
                    break;
                }
            }
            child = child->next_sibling;
        }
        if (!body) {
            log_debug("js_document_method: write - no body element found");
            return ItemNull;
        }

        // create a text node and append to body
        String* str = heap_create_name(text);
        DomText* text_node = dom_text_create_detached(str, doc);
        if (text_node) {
            ((DomNode*)body)->append_child((DomNode*)text_node);
            log_debug("js_document_method: write '%s' appended to body", text);
        }

        return ItemNull;
    }

    // v12b: createDocumentFragment()
    if (strcmp(method, "createDocumentFragment") == 0) {
        // create a lightweight container element with a special tag
        MarkBuilder builder(doc->input);
        Item frag_item = builder.element("#document-fragment").final();
        Element* frag_elem = frag_item.element;
        DomElement* dom_frag = dom_element_create(doc, "#document-fragment", frag_elem);
        return js_dom_wrap_element(dom_frag);
    }

    // v12b: createComment(data)
    if (strcmp(method, "createComment") == 0) {
        const char* text = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        if (!text) text = "";

        // create a Lambda Element with comment tag for backing
        MarkBuilder builder(doc->input);
        Item comment_item = builder.element("!--").text(text).final();
        Element* comment_elem = comment_item.element;

        DomComment* comment_node = dom_comment_create_detached(comment_elem, doc);
        if (!comment_node) return ItemNull;
        return js_dom_wrap_element(comment_node);
    }

    // v12b: importNode(node [, deep]) — deep clone a node
    if (strcmp(method, "importNode") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* source = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!source || !source->is_element()) return ItemNull;
        bool deep = (argc >= 2) ? js_is_truthy(args[1]) : false;
        Item source_item = js_dom_wrap_element(source);
        Item deep_arg = (Item){.item = b2it(deep ? 1 : 0)};
        Item method_str = (Item){.item = s2it(heap_create_name("cloneNode"))};
        return js_dom_element_method(source_item, method_str, &deep_arg, 1);
    }

    // v12b: adoptNode(node) — detach from current parent
    if (strcmp(method, "adoptNode") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* node = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!node) return ItemNull;
        if (node->parent) {
            node->parent->remove_child(node);
        }
        return args[0];
    }

    // EventTarget interface on document
    if (strcmp(method, "addEventListener") == 0) {
        if (argc >= 2) {
            js_dom_add_event_listener(js_get_document_object_value(), args[0], args[1], argc > 2 ? args[2] : ItemNull);
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (strcmp(method, "removeEventListener") == 0) {
        if (argc >= 2) {
            js_dom_remove_event_listener(js_get_document_object_value(), args[0], args[1], argc > 2 ? args[2] : ItemNull);
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (strcmp(method, "dispatchEvent") == 0) {
        if (argc >= 1) {
            return js_dom_dispatch_event(js_get_document_object_value(), args[0]);
        }
        return (Item){.item = ITEM_FALSE};
    }

    log_debug("js_document_method: unknown method '%s'", method);
    return ItemNull;
}

// ============================================================================
// Document Property Access
// ============================================================================

extern "C" Item js_document_get_property(Item prop_name) {
    if (!_js_current_document || !_js_current_document->root) {
        log_debug("js_document_get_property: no document set");
        return ItemNull;
    }

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    DomDocument* doc = _js_current_document;
    DomElement* root = doc->root;

    // documentElement — the root <html> element
    if (strcmp(prop, "documentElement") == 0) {
        return root ? js_dom_wrap_element(root) : ItemNull;
    }

    // body — the <body> element
    if (strcmp(prop, "body") == 0) {
        DomNode* child = root ? root->first_child : nullptr;
        while (child) {
            if (child->is_element()) {
                DomElement* elem = child->as_element();
                if (elem->tag_name && strcasecmp(elem->tag_name, "body") == 0) {
                    return js_dom_wrap_element(elem);
                }
            }
            child = child->next_sibling;
        }
        return ItemNull;
    }

    // head — the <head> element
    if (strcmp(prop, "head") == 0) {
        DomNode* child = root ? root->first_child : nullptr;
        while (child) {
            if (child->is_element()) {
                DomElement* elem = child->as_element();
                if (elem->tag_name && strcasecmp(elem->tag_name, "head") == 0) {
                    return js_dom_wrap_element(elem);
                }
            }
            child = child->next_sibling;
        }
        return ItemNull;
    }

    // title — text of first <title> element
    if (strcmp(prop, "title") == 0) {
        // search in <head> first
        DomNode* child = root ? root->first_child : nullptr;
        while (child) {
            if (child->is_element()) {
                DomElement* elem = child->as_element();
                if (elem->tag_name && strcasecmp(elem->tag_name, "head") == 0) {
                    DomNode* hchild = elem->first_child;
                    while (hchild) {
                        if (hchild->is_element()) {
                            DomElement* title_elem = hchild->as_element();
                            if (title_elem->tag_name &&
                                strcasecmp(title_elem->tag_name, "title") == 0) {
                                StrBuf* sb = strbuf_new_cap(64);
                                collect_text_content((DomNode*)title_elem, sb);
                                String* result = heap_create_name(sb->str ? sb->str : "");
                                strbuf_free(sb);
                                return (Item){.item = s2it(result)};
                            }
                        }
                        hchild = hchild->next_sibling;
                    }
                }
            }
            child = child->next_sibling;
        }
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // v12: URL — full document URL as string
    if (strcmp(prop, "URL") == 0) {
        Url* url = doc->url;
        if (url) {
            const char* href = url_get_href(url);
            if (href) return (Item){.item = s2it(heap_create_name(href))};
        }
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // readyState — always "complete" since scripts run after parse
    if (strcmp(prop, "readyState") == 0) {
        return (Item){.item = s2it(heap_create_name("complete"))};
    }

    // compatMode
    if (strcmp(prop, "compatMode") == 0) {
        return (Item){.item = s2it(heap_create_name("CSS1Compat"))};
    }

    // characterSet / charset
    if (strcmp(prop, "characterSet") == 0 || strcmp(prop, "charset") == 0) {
        return (Item){.item = s2it(heap_create_name("UTF-8"))};
    }

    // contentType
    if (strcmp(prop, "contentType") == 0) {
        return (Item){.item = s2it(heap_create_name("text/html"))};
    }

    // nodeType — DOCUMENT_NODE = 9
    if (strcmp(prop, "nodeType") == 0) {
        return (Item){.item = i2it(9)};
    }

    // nodeName
    if (strcmp(prop, "nodeName") == 0) {
        return (Item){.item = s2it(heap_create_name("#document"))};
    }

    // styleSheets — collection of parsed CSSStyleSheet objects
    if (strcmp(prop, "styleSheets") == 0) {
        return js_cssom_get_document_stylesheets();
    }

    // ownerDocument — the document itself has no owner (returns null)
    if (strcmp(prop, "ownerDocument") == 0) {
        return ItemNull;
    }

    // defaultView — returns window (the global object)
    // Sizzle accesses document.defaultView for getComputedStyle
    if (strcmp(prop, "defaultView") == 0) {
        // Return stored window object from the proxy map directly (avoid recursion)
        if (js_document_proxy_item.item != ITEM_NULL &&
            js_document_proxy_item.map->data_cap > 0) {
            Item val = map_get(js_document_proxy_item.map, prop_name);
            if (val.item != ITEM_NULL && get_type_id(val) != LMD_TYPE_UNDEFINED) {
                return val;
            }
        }
        return ItemNull;
    }

    log_debug("js_document_get_property: unknown property '%s'", prop);
    return ItemNull;
}

// ============================================================================
// Element Property Access
// ============================================================================

extern "C" Item js_dom_get_property(Item elem_item, Item prop_name) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    const char* prop = fn_to_cstr(prop_name);
    if (!node) {
        log_debug("js_dom_get_property: not a DOM node");
        return ItemNull;
    }

    if (!prop) return ItemNull;

    // Text node properties
    if (node->is_text()) {
        DomText* text_node = node->as_text();
        if (strcmp(prop, "data") == 0 || strcmp(prop, "nodeValue") == 0 || strcmp(prop, "textContent") == 0) {
            return text_node->text ? (Item){.item = s2it(heap_create_name(text_node->text))}
                                   : (Item){.item = s2it(heap_create_name(""))};
        }
        if (strcmp(prop, "length") == 0) {
            return (Item){.item = i2it((int64_t)text_node->length)};
        }
        if (strcmp(prop, "nodeType") == 0) {
            return (Item){.item = i2it(3)}; // TEXT_NODE
        }
        if (strcmp(prop, "nodeName") == 0) {
            return (Item){.item = s2it(heap_create_name("#text"))};
        }
        if (strcmp(prop, "parentNode") == 0 || strcmp(prop, "parentElement") == 0) {
            DomNode* parent = text_node->parent;
            if (parent && parent->is_element()) {
                return js_dom_wrap_element(parent->as_element());
            }
            return ItemNull;
        }
        if (strcmp(prop, "nextSibling") == 0) {
            DomNode* sib = text_node->next_sibling;
            if (!sib) return ItemNull;
            return js_dom_wrap_element((void*)sib);
        }
        if (strcmp(prop, "previousSibling") == 0) {
            DomNode* sib = text_node->prev_sibling;
            if (!sib) return ItemNull;
            return js_dom_wrap_element((void*)sib);
        }
        log_debug("js_dom_get_property: unknown text node property '%s'", prop);
        return ItemNull;
    }

    // Comment node properties
    if (node->is_comment()) {
        DomComment* comment_node = node->as_comment();
        if (strcmp(prop, "data") == 0 || strcmp(prop, "nodeValue") == 0 || strcmp(prop, "textContent") == 0) {
            return comment_node->content ? (Item){.item = s2it(heap_create_name(comment_node->content))}
                                         : (Item){.item = s2it(heap_create_name(""))};
        }
        if (strcmp(prop, "nodeType") == 0) {
            return (Item){.item = i2it(8)}; // COMMENT_NODE
        }
        if (strcmp(prop, "nodeName") == 0) {
            return (Item){.item = s2it(heap_create_name("#comment"))};
        }
        if (strcmp(prop, "length") == 0) {
            return (Item){.item = i2it((int64_t)comment_node->length)};
        }
        if (strcmp(prop, "parentNode") == 0 || strcmp(prop, "parentElement") == 0) {
            DomNode* parent = comment_node->parent;
            if (parent && parent->is_element()) {
                return js_dom_wrap_element(parent->as_element());
            }
            return ItemNull;
        }
        log_debug("js_dom_get_property: unknown comment node property '%s'", prop);
        return ItemNull;
    }

    // Element properties below — safe to cast
    DomElement* elem = node->as_element();
    if (!elem) {
        log_debug("js_dom_get_property: node is not an element for property '%s'", prop);
        return ItemNull;
    }

    // tagName (uppercased per spec)
    if (strcmp(prop, "tagName") == 0) {
        return (Item){.item = s2it(uppercase_tag_name(elem->tag_name))};
    }

    // id
    if (strcmp(prop, "id") == 0) {
        return (Item){.item = elem->id ? s2it(heap_create_name(elem->id))
                                        : s2it(heap_create_name(""))};
    }

    // className (space-joined class list)
    if (strcmp(prop, "className") == 0) {
        if (elem->class_count == 0) return (Item){.item = s2it(heap_create_name(""))};
        StrBuf* sb = strbuf_new_cap(64);
        for (int i = 0; i < elem->class_count; i++) {
            if (i > 0) strbuf_append_char(sb, ' ');
            strbuf_append_str(sb, elem->class_names[i]);
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // textContent (recursive text extraction)
    if (strcmp(prop, "textContent") == 0) {
        StrBuf* sb = strbuf_new_cap(128);
        collect_text_content((DomNode*)elem, sb);
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // innerHTML (recursive HTML serialization of children)
    if (strcmp(prop, "innerHTML") == 0) {
        StrBuf* sb = strbuf_new_cap(256);
        DomNode* child = elem->first_child;
        while (child) {
            collect_inner_html(child, sb);
            child = child->next_sibling;
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // v12: outerHTML (element itself + children)
    if (strcmp(prop, "outerHTML") == 0) {
        StrBuf* sb = strbuf_new_cap(256);
        collect_inner_html((DomNode*)elem, sb);
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // nodeType
    if (strcmp(prop, "nodeType") == 0) {
        return (Item){.item = i2it((int64_t)elem->node_type)};
    }

    // childElementCount
    if (strcmp(prop, "childElementCount") == 0) {
        int count = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) count++;
            child = child->next_sibling;
        }
        return (Item){.item = i2it((int64_t)count)};
    }

    // children (array of child DOM elements only)
    if (strcmp(prop, "children") == 0) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) {
                array_push(arr, js_dom_wrap_element(child->as_element()));
            }
            child = child->next_sibling;
        }
        return (Item){.array = arr};
    }

    // parentElement
    if (strcmp(prop, "parentElement") == 0) {
        DomNode* parent = (DomNode*)elem->parent;
        if (parent && parent->is_element()) {
            return js_dom_wrap_element(parent->as_element());
        }
        return ItemNull;
    }

    // parentNode (includes text nodes — returns any parent)
    if (strcmp(prop, "parentNode") == 0) {
        DomNode* parent = (DomNode*)elem->parent;
        if (parent && parent->is_element()) {
            return js_dom_wrap_element(parent->as_element());
        }
        return ItemNull;
    }

    // ownerDocument — returns the document proxy for any element
    if (strcmp(prop, "ownerDocument") == 0) {
        return js_get_document_object_value();
    }

    // contentDocument / contentWindow — for iframe elements, return the main
    // document proxy so scripts like `iframe.contentDocument.createElement()`
    // work without crashing.  Not a real sub-document, but sufficient for
    // crash-safety tests and simple DOM manipulation.
    if (strcmp(prop, "contentDocument") == 0 || strcmp(prop, "contentWindow") == 0) {
        return js_get_document_object_value();
    }

    // firstChild (any node type, not just elements)
    if (strcmp(prop, "firstChild") == 0) {
        DomNode* child = elem->first_child;
        if (!child) return ItemNull;
        if (child->is_element()) return js_dom_wrap_element(child->as_element());
        // wrap text node
        return js_dom_wrap_element((DomElement*)(void*)child);
    }

    // lastChild (any node type)
    if (strcmp(prop, "lastChild") == 0) {
        DomNode* child = elem->last_child;
        if (!child) return ItemNull;
        if (child->is_element()) return js_dom_wrap_element(child->as_element());
        return js_dom_wrap_element((DomElement*)(void*)child);
    }

    // nextSibling (any node type)
    if (strcmp(prop, "nextSibling") == 0) {
        DomNode* sib = ((DomNode*)elem)->next_sibling;
        if (!sib) return ItemNull;
        if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
        return js_dom_wrap_element((DomElement*)(void*)sib);
    }

    // previousSibling (any node type)
    if (strcmp(prop, "previousSibling") == 0) {
        DomNode* sib = ((DomNode*)elem)->prev_sibling;
        if (!sib) return ItemNull;
        if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
        return js_dom_wrap_element((DomElement*)(void*)sib);
    }

    // firstElementChild
    if (strcmp(prop, "firstElementChild") == 0) {
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) return js_dom_wrap_element(child->as_element());
            child = child->next_sibling;
        }
        return ItemNull;
    }

    // lastElementChild
    if (strcmp(prop, "lastElementChild") == 0) {
        DomNode* child = elem->last_child;
        while (child) {
            if (child->is_element()) return js_dom_wrap_element(child->as_element());
            child = child->prev_sibling;
        }
        return ItemNull;
    }

    // nextElementSibling
    if (strcmp(prop, "nextElementSibling") == 0) {
        DomNode* sib = ((DomNode*)elem)->next_sibling;
        while (sib) {
            if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
            sib = sib->next_sibling;
        }
        return ItemNull;
    }

    // previousElementSibling
    if (strcmp(prop, "previousElementSibling") == 0) {
        DomNode* sib = ((DomNode*)elem)->prev_sibling;
        while (sib) {
            if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
            sib = sib->prev_sibling;
        }
        return ItemNull;
    }

    // childNodes (all children including text nodes)
    if (strcmp(prop, "childNodes") == 0) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) {
                array_push(arr, js_dom_wrap_element(child->as_element()));
            } else {
                // wrap text/comment nodes
                array_push(arr, js_dom_wrap_element((DomElement*)(void*)child));
            }
            child = child->next_sibling;
        }
        return (Item){.array = arr};
    }

    // children (array of child DOM elements only)
    if (strcmp(prop, "children") == 0) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) {
                array_push(arr, js_dom_wrap_element(child->as_element()));
            }
            child = child->next_sibling;
        }
        return (Item){.array = arr};
    }

    // length (for NodeList / HTMLCollection-like results)
    if (strcmp(prop, "length") == 0) {
        int count = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) count++;
            child = child->next_sibling;
        }
        return (Item){.item = i2it((int64_t)count)};
    }

    // offsetWidth / offsetHeight / clientWidth / clientHeight — stub returns
    // In CSS2.1 tests, these are primarily used as layout flush triggers.
    // Since our pipeline runs scripts before layout, return 0 as a stub.
    if (strcmp(prop, "offsetWidth") == 0 || strcmp(prop, "offsetHeight") == 0 ||
        strcmp(prop, "clientWidth") == 0 || strcmp(prop, "clientHeight") == 0) {
        log_debug("js_dom_get_property: stub %s=0 on <%s>",
                  prop, elem->tag_name ? elem->tag_name : "?");
        return (Item){.item = i2it(0)};
    }

    // data (text node content) — check if the wrapped node is actually a DomText
    if (strcmp(prop, "data") == 0) {
        DomNode* node = (DomNode*)elem;  // may be DomText wrapped as DomElement*
        if (node->is_text()) {
            DomText* text_node = node->as_text();
            if (text_node->text && text_node->length > 0) {
                String* s = heap_strcpy((char*)text_node->text, text_node->length);
                return (Item){.item = s2it(s)};
            }
            return (Item){.item = s2it(heap_create_name(""))};
        }
        return ItemNull;
    }

    // nodeName — tag name for elements, "#text" for text nodes
    if (strcmp(prop, "nodeName") == 0) {
        DomNode* node = (DomNode*)elem;
        if (node->is_text()) {
            return (Item){.item = s2it(heap_create_name("#text"))};
        }
        return (Item){.item = s2it(uppercase_tag_name(elem->tag_name))};
    }

    // HTMLStyleElement.sheet — associated CSSStyleSheet (doesn't require native_element)
    if (strcmp(prop, "sheet") == 0 && elem->tag_name && strcasecmp(elem->tag_name, "style") == 0) {
        return js_cssom_get_style_element_sheet(elem_item);
    }

    // fall back to native element attribute access
    if (elem->native_element) {
        const char* attr_val = dom_element_get_attribute(elem, prop);
        if (attr_val) {
            return (Item){.item = s2it(heap_create_name(attr_val))};
        }
    }

    log_debug("js_dom_get_property: unknown property '%s' on <%s>",
              prop, elem->tag_name ? elem->tag_name : "?");
    return ItemNull;
}

// ============================================================================
// Element Property Set
// ============================================================================

// Helper: convert camelCase JS property name to CSS hyphenated form
// e.g., "fontFamily" → "font-family", "borderWidth" → "border-width"
// "cssFloat" → "float", "display" → "display"
static void js_camel_to_css_prop(const char* js_prop, char* css_buf, size_t buf_size) {
    // special cases
    if (strcmp(js_prop, "cssFloat") == 0) {
        snprintf(css_buf, buf_size, "float");
        return;
    }
    if (strcmp(js_prop, "cssText") == 0) {
        snprintf(css_buf, buf_size, "cssText");
        return;
    }

    size_t out = 0;
    for (size_t i = 0; js_prop[i] && out < buf_size - 2; i++) {
        char c = js_prop[i];
        if (c >= 'A' && c <= 'Z') {
            css_buf[out++] = '-';
            css_buf[out++] = (char)(c + 32);  // to lowercase
        } else {
            css_buf[out++] = c;
        }
    }
    css_buf[out] = '\0';
}

// Helper: parse class_names from space-separated string, updates elem->class_names/class_count
static void parse_class_names(DomElement* elem, const char* class_str) {
    if (!elem || !class_str) return;

    // count classes
    int count = 0;
    const char* p = class_str;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        count++;
        while (*p && *p != ' ') p++;
    }

    // allocate class_names array in the document arena
    Pool* pool = elem->doc ? elem->doc->pool : nullptr;
    const char** names = nullptr;
    if (count > 0 && pool) {
        names = (const char**)pool_alloc(pool, count * sizeof(const char*));
        int idx = 0;
        p = class_str;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            const char* start = p;
            while (*p && *p != ' ') p++;
            size_t len = p - start;
            char* cname = (char*)pool_alloc(pool, len + 1);
            memcpy(cname, start, len);
            cname[len] = '\0';
            names[idx++] = cname;
        }
    }

    elem->class_names = names;
    elem->class_count = count;
    elem->styles_resolved = false;  // mark for re-cascading
}

extern "C" Item js_dom_set_property(Item elem_item, Item prop_name, Item value) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node) {
        log_debug("js_dom_set_property: not a DOM node");
        return ItemNull;
    }

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    // text node .data property
    if (node->is_text() && strcmp(prop, "data") == 0) {
        DomText* text_node = node->as_text();
        const char* new_text = fn_to_cstr(value);
        if (new_text) {
            size_t len = strlen(new_text);
            String* s = heap_strcpy((char*)new_text, len);
            text_node->native_string = s;
            text_node->text = s->chars;
            text_node->length = len;
            log_debug("js_dom_set_property: set text node data='%.30s'", new_text);
        }
        return value;
    }

    // remaining properties require an element node
    if (!node->is_element()) {
        log_debug("js_dom_set_property: node is not an element for property '%s'", prop);
        return ItemNull;
    }
    DomElement* elem = node->as_element();

    // className
    if (strcmp(prop, "className") == 0) {
        const char* class_str = fn_to_cstr(value);
        if (class_str) {
            parse_class_names(elem, class_str);
            // also update the native element attribute
            dom_element_set_attribute(elem, "class", class_str);
            js_dom_mutation_notify();
            log_debug("js_dom_set_property: set className='%s' on <%s>",
                      class_str, elem->tag_name ? elem->tag_name : "?");
        }
        return value;
    }

    // id
    if (strcmp(prop, "id") == 0) {
        const char* id_str = fn_to_cstr(value);
        if (id_str && elem->doc && elem->doc->pool) {
            size_t len = strlen(id_str);
            char* id_copy = (char*)pool_alloc(elem->doc->pool, len + 1);
            memcpy(id_copy, id_str, len);
            id_copy[len] = '\0';
            elem->id = id_copy;
            dom_element_set_attribute(elem, "id", id_str);
            js_dom_mutation_notify();
            log_debug("js_dom_set_property: set id='%s' on <%s>",
                      id_str, elem->tag_name ? elem->tag_name : "?");
        }
        return value;
    }

    // textContent
    if (strcmp(prop, "textContent") == 0) {
        const char* text_str = fn_to_cstr(value);
        if (text_str) {
            // remove all children and add a single text node
            // first, detach all children
            DomNode* child = elem->first_child;
            while (child) {
                DomNode* next = child->next_sibling;
                child->parent = nullptr;
                child->next_sibling = nullptr;
                child->prev_sibling = nullptr;
                child = next;
            }
            elem->first_child = nullptr;
            elem->last_child = nullptr;
            // create a text node with the new content
            String* s = heap_create_name(text_str);
            DomText* text_node = dom_text_create(s, elem);
            if (text_node) {
                ((DomNode*)text_node)->parent = (DomNode*)elem;
                elem->first_child = (DomNode*)text_node;
                elem->last_child = (DomNode*)text_node;
            }
            log_debug("js_dom_set_property: set textContent on <%s>",
                      elem->tag_name ? elem->tag_name : "?");
            js_dom_mutation_notify();
        }
        return value;
    }

    // v12b: innerHTML setter — parse HTML and replace children
    if (strcmp(prop, "innerHTML") == 0) {
        const char* html_str = fn_to_cstr(value);
        if (!html_str) return ItemNull;

        // 1. Remove all existing children
        DomNode* child = elem->first_child;
        while (child) {
            DomNode* next = child->next_sibling;
            child->parent = nullptr;
            child->next_sibling = nullptr;
            child->prev_sibling = nullptr;
            child = next;
        }
        elem->first_child = nullptr;
        elem->last_child = nullptr;

        // 2. Empty string → done (cleared children)
        if (html_str[0] == '\0') return value;

        // 3. Parse HTML fragment
        DomDocument* doc = elem->doc;
        if (!doc || !doc->input) return value;

        Html5Parser* parser = html5_fragment_parser_create(
            doc->pool, doc->arena, doc->input);
        if (!parser) return value;

        html5_fragment_parse(parser, html_str);
        Element* body_elem = html5_fragment_get_body(parser);
        if (!body_elem) return value;

        // 4. Convert parsed Lambda Elements to DOM nodes and append
        for (size_t i = 0; i < body_elem->length; i++) {
            TypeId type = get_type_id(body_elem->items[i]);
            if (type == LMD_TYPE_ELEMENT) {
                build_dom_tree_from_element(
                    body_elem->items[i].element, doc, elem);
                // build_dom_tree_from_element already appends to parent
            } else if (type == LMD_TYPE_STRING) {
                String* s = it2s(body_elem->items[i]);
                DomText* text_node = dom_text_create(s, elem);
                if (text_node) {
                    // link text node into parent's sibling chain
                    text_node->parent = elem;
                    if (!elem->first_child) {
                        elem->first_child = text_node;
                        elem->last_child = text_node;
                    } else {
                        DomNode* last = elem->last_child;
                        last->next_sibling = text_node;
                        text_node->prev_sibling = last;
                        elem->last_child = text_node;
                    }
                }
            }
        }

        log_debug("js_dom_set_property: set innerHTML on <%s>",
                  elem->tag_name ? elem->tag_name : "?");
        js_dom_mutation_notify();
        return value;
    }

    // generic property set via setAttribute
    // Handle boolean values for HTML boolean attributes (multiple, disabled,
    // checked, selected, hidden, etc.): true → setAttribute, false → removeAttribute
    TypeId val_type = get_type_id(value);
    if (val_type == LMD_TYPE_BOOL) {
        // Check low byte for true/false (ITEM_TRUE/ITEM_FALSE macros lack
        // outer parens, so direct == comparison has operator precedence issues)
        bool is_true = (value.item & 0xFF) != 0;
        if (is_true) {
            dom_element_set_attribute(elem, prop, "");
        } else {
            dom_element_remove_attribute(elem, prop);
        }
        js_dom_mutation_notify();
        return value;
    }
    const char* val_str = fn_to_cstr(value);
    if (val_str) {
        dom_element_set_attribute(elem, prop, val_str);
        js_dom_mutation_notify();
    }
    return value;
}

extern "C" Item js_dom_set_style_property(Item elem_item, Item prop_name, Item value) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        // not a DOM element — fall back to normal property set on obj.style
        Item style_key = (Item){.item = s2it(heap_create_name("style"))};
        Item style_obj = js_property_get(elem_item, style_key);
        if (style_obj.item != ITEM_NULL && get_type_id(style_obj) == LMD_TYPE_MAP) {
            return js_property_set(style_obj, prop_name, value);
        }
        return ItemNull;
    }

    const char* js_prop = fn_to_cstr(prop_name);
    const char* val_str = fn_to_cstr(value);
    if (!js_prop || !val_str) return ItemNull;

    // convert camelCase JS property to CSS property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));

    // handle cssText special case: replace entire inline style
    if (strcmp(css_prop, "cssText") == 0) {
        dom_element_remove_inline_styles(elem);
        if (val_str[0]) {
            dom_element_apply_inline_style(elem, val_str);
        }
        js_dom_mutation_notify();
        log_debug("js_dom_set_style_property: set cssText='%.50s' on <%s>",
                  val_str, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    // CSSOM §6.7.3: setting a property to empty string removes it
    if (!val_str[0]) {
        CssPropertyId prop_id = css_property_id_from_name(css_prop);
        if (prop_id != CSS_PROPERTY_UNKNOWN && elem->specified_style) {
            style_tree_remove_property(elem->specified_style, prop_id);
            elem->styles_resolved = false;
            js_dom_mutation_notify();
        }
        log_debug("js_dom_set_style_property: removed %s (CSS: %s) on <%s>",
                  js_prop, css_prop, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    // build a single-declaration inline style string: "property: value"
    char style_decl[256];
    snprintf(style_decl, sizeof(style_decl), "%s: %s", css_prop, val_str);

    // validate: reject values with invalid non-ASCII codepoints (CSS Syntax §4.2)
    for (size_t i = 0; val_str[i]; ) {
        unsigned char b = (unsigned char)val_str[i];
        if (b < 0x80) {
            i++;
        } else {
            UnicodeChar uc = css_parse_unicode_char(val_str + i, strlen(val_str + i));
            if (uc.byte_length == 0 || !css_is_name_char_unicode(uc.codepoint)) {
                log_debug("js_dom_set_style_property: rejecting value with invalid codepoint U+%04X at byte offset %zu (byte=0x%02X)", uc.codepoint, i, b);
                return value;  // silently reject per CSSOM spec
            }
            i += uc.byte_length;
        }
    }

    // apply as inline style (highest cascade priority)
    dom_element_apply_inline_style(elem, style_decl);
    elem->styles_resolved = false;  // mark for re-cascading
    js_dom_mutation_notify();

    log_debug("js_dom_set_style_property: set %s='%s' (CSS: %s) on <%s>",
              js_prop, val_str, css_prop, elem->tag_name ? elem->tag_name : "?");
    return value;
}

// ============================================================================
// Style Property Read (elem.style.X)
// ============================================================================

extern "C" Item js_dom_get_style_property(Item elem_item, Item prop_name) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        // not a DOM element — fall back to normal property access on obj.style
        Item style_key = (Item){.item = s2it(heap_create_name("style"))};
        Item style_obj = js_property_get(elem_item, style_key);
        if (style_obj.item != ITEM_NULL && get_type_id(style_obj) == LMD_TYPE_MAP) {
            return js_property_get(style_obj, prop_name);
        }
        return (Item){.item = s2it(heap_create_name(""))};
    }

    const char* js_prop = fn_to_cstr(prop_name);
    if (!js_prop) return (Item){.item = s2it(heap_create_name(""))};

    // convert camelCase JS property to CSS property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));

    // v12: cssText getter — return the raw inline style string
    if (strcmp(css_prop, "cssText") == 0) {
        const char* inline_style = dom_element_get_inline_style(elem);
        return (Item){.item = s2it(heap_create_name(inline_style ? inline_style : ""))};
    }

    // look up the CSS property ID
    CssPropertyId prop_id = css_property_id_from_name(css_prop);
    if (prop_id == CSS_PROPERTY_UNKNOWN) {
        log_debug("js_dom_get_style_property: unknown CSS property '%s'", css_prop);
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // get the specified value for this property
    CssDeclaration* decl = dom_element_get_specified_value(elem, prop_id);
    if (!decl || !decl->value) {
        // shorthand fallback: if the property is a shorthand (e.g. padding, margin),
        // try the first longhand (e.g. padding-top) since shorthands are expanded
        if (css_property_is_shorthand(prop_id)) {
            char longhand[128];
            snprintf(longhand, sizeof(longhand), "%s-top", css_prop);
            CssPropertyId lh_id = css_property_id_from_name(longhand);
            if (lh_id != CSS_PROPERTY_UNKNOWN) {
                decl = dom_element_get_specified_value(elem, lh_id);
            }
        }
        if (!decl || !decl->value) {
            return (Item){.item = s2it(heap_create_name(""))};
        }
    }

    // only return values that came from inline styles (element.style.X should
    // only reflect inline styles, not stylesheet rules)
    if (!decl->specificity.inline_style) {
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // convert the CSS value back to a string
    CssValue* val = decl->value;
    switch (val->type) {
        case CSS_VALUE_TYPE_KEYWORD: {
            const CssEnumInfo* info = css_enum_info(val->data.keyword);
            if (info && info->name) {
                return (Item){.item = s2it(heap_create_name(info->name))};
            }
            break;
        }
        case CSS_VALUE_TYPE_LENGTH: {
            char buf[64];
            const char* unit_str = "";
            switch (val->data.length.unit) {
                case CSS_UNIT_PX: unit_str = "px"; break;
                case CSS_UNIT_EM: unit_str = "em"; break;
                case CSS_UNIT_REM: unit_str = "rem"; break;
                case CSS_UNIT_PERCENT: unit_str = "%"; break;
                case CSS_UNIT_VW: unit_str = "vw"; break;
                case CSS_UNIT_VH: unit_str = "vh"; break;
                case CSS_UNIT_CM: unit_str = "cm"; break;
                case CSS_UNIT_MM: unit_str = "mm"; break;
                case CSS_UNIT_IN: unit_str = "in"; break;
                case CSS_UNIT_PT: unit_str = "pt"; break;
                case CSS_UNIT_PC: unit_str = "pc"; break;
                default: unit_str = "px"; break;
            }
            double v = val->data.length.value;
            if (v == (int)v) {
                snprintf(buf, sizeof(buf), "%d%s", (int)v, unit_str);
            } else {
                snprintf(buf, sizeof(buf), "%g%s", v, unit_str);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_PERCENTAGE: {
            char buf[64];
            double v = val->data.percentage.value;
            if (v == (int)v) {
                snprintf(buf, sizeof(buf), "%d%%", (int)v);
            } else {
                snprintf(buf, sizeof(buf), "%g%%", v);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_NUMBER: {
            char buf[64];
            if (val->data.number.is_integer) {
                snprintf(buf, sizeof(buf), "%d", (int)val->data.number.value);
            } else {
                snprintf(buf, sizeof(buf), "%g", val->data.number.value);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_STRING:
            if (val->data.string) {
                return (Item){.item = s2it(heap_create_name(val->data.string))};
            }
            break;
        case CSS_VALUE_TYPE_CUSTOM:
            if (val->data.custom_property.name) {
                return (Item){.item = s2it(heap_create_name(val->data.custom_property.name))};
            }
            break;
        default:
            break;
    }

    return (Item){.item = s2it(heap_create_name(""))};
}

// ============================================================================
// Element Method Dispatcher
// ============================================================================

extern "C" Item js_dom_element_method(Item elem_item, Item method_name, Item* args, int argc) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node) {
        log_error("js_dom_element_method: not a DOM element");
        return ItemNull;
    }
    DomElement* elem = node->as_element(); // may be nullptr for text/comment nodes

    const char* method = fn_to_cstr(method_name);
    if (!method) {
        log_error("js_dom_element_method: invalid method name");
        return ItemNull;
    }

    log_debug("js_dom_element_method: '%s' on <%s>", method,
              node->node_name() ? node->node_name() : "?");

    // v12b: remove() — self-removal from parent (works on any node type)
    if (strcmp(method, "remove") == 0) {
        if (node->parent) {
            node->parent->remove_child(node);
        }
        return ItemNull;
    }

    // v12: contains(other) → boolean (works on any node type)
    if (strcmp(method, "contains") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        return js_dom_contains(elem_item, args[0]);
    }

    // All remaining methods require an element node
    if (!elem) {
        log_debug("js_dom_element_method: '%s' called on non-element node, ignored", method);
        return ItemNull;
    }

    // getAttribute(name) → string or null
    if (strcmp(method, "getAttribute") == 0) {
        if (argc < 1) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return ItemNull;
        const char* val = dom_element_get_attribute(elem, attr_name);
        return val ? (Item){.item = s2it(heap_create_name(val))} : ItemNull;
    }

    // setAttribute(name, value)
    if (strcmp(method, "setAttribute") == 0) {
        if (argc < 2) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        const char* attr_val = fn_to_cstr(args[1]);
        if (!attr_name || !attr_val) return ItemNull;
        dom_element_set_attribute(elem, attr_name, attr_val);
        js_dom_mutation_notify();
        return ItemNull;
    }

    // hasAttribute(name) → boolean
    if (strcmp(method, "hasAttribute") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return (Item){.item = ITEM_FALSE};
        bool has = dom_element_has_attribute(elem, attr_name);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    // removeAttribute(name)
    if (strcmp(method, "removeAttribute") == 0) {
        if (argc < 1) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return ItemNull;
        dom_element_remove_attribute(elem, attr_name);
        js_dom_mutation_notify();
        return ItemNull;
    }

    // querySelector(selector) — from this element
    if (strcmp(method, "querySelector") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) return ItemNull;

        SelectorMatcher* matcher = selector_matcher_create(pool);
        DomElement* found = selector_matcher_find_first(matcher, selector, elem);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // querySelectorAll(selector) — from this element
    if (strcmp(method, "querySelectorAll") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);

        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;

        if (!selector) return (Item){.array = arr};

        SelectorMatcher* matcher = selector_matcher_create(pool);
        DomElement** results = nullptr;
        int count = 0;
        selector_matcher_find_all(matcher, selector, elem, &results, &count);
        for (int i = 0; i < count; i++) {
            array_push(arr, js_dom_wrap_element(results[i]));
        }
        return (Item){.array = arr};
    }

    // matches(selector) → boolean
    if (strcmp(method, "matches") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) return (Item){.item = ITEM_FALSE};

        Pool* pool = elem->doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) return (Item){.item = ITEM_FALSE};

        SelectorMatcher* matcher = selector_matcher_create(pool);
        MatchResult result;
        bool matched = selector_matcher_matches(matcher, selector, elem, &result);
        return (Item){.item = b2it(matched ? 1 : 0)};
    }

    // closest(selector) → element or null
    if (strcmp(method, "closest") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) return ItemNull;

        SelectorMatcher* matcher = selector_matcher_create(pool);
        MatchResult mresult;
        DomElement* current = elem;
        while (current) {
            if (selector_matcher_matches(matcher, selector, current, &mresult)) {
                return js_dom_wrap_element(current);
            }
            DomNode* parent = current->parent;
            current = (parent && parent->is_element()) ? parent->as_element() : nullptr;
        }
        return ItemNull;
    }

    // appendChild(child) — appends a child element or text node
    if (strcmp(method, "appendChild") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* child_node = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!child_node) {
            log_error("js_dom_element_method appendChild: argument is not a DOM node");
            return ItemNull;
        }
        // v12b: DocumentFragment support — move children instead of the fragment itself
        if (child_node->is_element()) {
            DomElement* child_elem = child_node->as_element();
            if (child_elem->tag_name && strcmp(child_elem->tag_name, "#document-fragment") == 0) {
                DomNode* frag_child = child_elem->first_child;
                while (frag_child) {
                    DomNode* next = frag_child->next_sibling;
                    child_elem->remove_child(frag_child);
                    ((DomNode*)elem)->append_child(frag_child);
                    frag_child = next;
                }
                js_dom_mutation_notify();
                return args[0];
            }
        }
        // detach child from current parent if any
        if (child_node->parent) {
            DomNode* old_parent = child_node->parent;
            if (old_parent->is_element()) {
                old_parent->remove_child(child_node);
            }
        }
        // use DomNode::append_child which handles all node types
        ((DomNode*)elem)->append_child(child_node);
        js_dom_mutation_notify();
        return args[0];  // return the appended child
    }

    // removeChild(child) — removes a child node (element or text)
    if (strcmp(method, "removeChild") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* child_node = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!child_node) {
            log_error("js_dom_element_method removeChild: argument is not a DOM node");
            return ItemNull;
        }
        ((DomNode*)elem)->remove_child(child_node);
        js_dom_mutation_notify();
        return args[0];  // return the removed child
    }

    // insertBefore(newChild, refChild)
    if (strcmp(method, "insertBefore") == 0) {
        if (argc < 2) return ItemNull;
        DomNode* new_child = (DomNode*)js_dom_unwrap_element(args[0]);
        DomNode* ref_child = (DomNode*)js_dom_unwrap_element(args[1]);
        if (!new_child) return ItemNull;
        // v12b: DocumentFragment support — move children instead of the fragment itself
        if (new_child->is_element()) {
            DomElement* new_elem = new_child->as_element();
            if (new_elem->tag_name && strcmp(new_elem->tag_name, "#document-fragment") == 0) {
                DomNode* frag_child = new_elem->first_child;
                while (frag_child) {
                    DomNode* next = frag_child->next_sibling;
                    new_elem->remove_child(frag_child);
                    ((DomNode*)elem)->insert_before(frag_child, ref_child);
                    frag_child = next;
                }
                js_dom_mutation_notify();
                return args[0];
            }
        }
        // detach from old parent
        if (new_child->parent) {
            new_child->parent->remove_child(new_child);
        }
        ((DomNode*)elem)->insert_before(new_child, ref_child);
        js_dom_mutation_notify();
        return args[0];
    }

    // hasChildNodes() → boolean
    if (strcmp(method, "hasChildNodes") == 0) {
        bool has = (elem->first_child != nullptr);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    // normalize() — merge adjacent text nodes
    if (strcmp(method, "normalize") == 0) {
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_text()) {
                DomText* text = child->as_text();
                // merge consecutive text nodes
                while (child->next_sibling && child->next_sibling->is_text()) {
                    DomText* next_text = child->next_sibling->as_text();
                    // concatenate text content
                    size_t new_len = text->length + next_text->length;
                    char* combined = (char*)pool_alloc(elem->doc->pool, new_len + 1);
                    if (text->text && text->length > 0)
                        memcpy(combined, text->text, text->length);
                    if (next_text->text && next_text->length > 0)
                        memcpy(combined + text->length, next_text->text, next_text->length);
                    combined[new_len] = '\0';
                    // update main text node
                    String* s = heap_strcpy(combined, new_len);
                    text->native_string = s;
                    text->text = s->chars;
                    text->length = new_len;
                    // remove the next text node
                    DomNode* remove_node = child->next_sibling;
                    ((DomNode*)elem)->remove_child(remove_node);
                }
            }
            child = child->next_sibling;
        }
        return ItemNull;
    }

    // cloneNode(deep) — clone element (and optionally children)
    if (strcmp(method, "cloneNode") == 0) {
        bool deep = (argc > 0) ? js_is_truthy(args[0]) : false;
        // create a new element with same tag
        DomElement* clone = dom_element_create(elem->doc, elem->tag_name, elem->native_element);
        if (!clone) return ItemNull;
        // copy id and class
        clone->id = elem->id;
        clone->class_names = elem->class_names;
        clone->class_count = elem->class_count;
        clone->tag_id = elem->tag_id;
        // copy attributes via native element
        // deep clone: recursively clone children
        if (deep) {
            DomNode* child = elem->first_child;
            while (child) {
                if (child->is_element()) {
                    // recursively clone child element
                    Item child_wrapped = js_dom_wrap_element(child->as_element());
                    Item child_clone = js_dom_element_method(child_wrapped, method_name, args, argc);
                    DomNode* cloned_child = (DomNode*)js_dom_unwrap_element(child_clone);
                    if (cloned_child) {
                        ((DomNode*)clone)->append_child(cloned_child);
                    }
                } else if (child->is_text()) {
                    // clone text node
                    DomText* text = child->as_text();
                    String* s = text->native_string;
                    DomText* text_clone = dom_text_create(s, clone);
                    if (text_clone) {
                        ((DomNode*)clone)->append_child((DomNode*)text_clone);
                    }
                }
                child = child->next_sibling;
            }
        }
        return js_dom_wrap_element(clone);
    }

    // v12b: replaceChild(newChild, oldChild)
    if (strcmp(method, "replaceChild") == 0) {
        if (argc < 2) return ItemNull;
        DomNode* new_child = (DomNode*)js_dom_unwrap_element(args[0]);
        DomNode* old_child = (DomNode*)js_dom_unwrap_element(args[1]);
        if (!new_child || !old_child) return ItemNull;
        // detach new_child from its current parent
        if (new_child->parent) {
            new_child->parent->remove_child(new_child);
        }
        // insert new before old, then remove old
        ((DomNode*)elem)->insert_before(new_child, old_child);
        ((DomNode*)elem)->remove_child(old_child);
        return args[1]; // return removed old child
    }

    // v12b: toggleAttribute(name [, force])
    if (strcmp(method, "toggleAttribute") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return (Item){.item = ITEM_FALSE};

        bool has = dom_element_has_attribute(elem, attr_name);
        bool should_have;
        if (argc >= 2) {
            should_have = js_is_truthy(args[1]);
        } else {
            should_have = !has; // toggle
        }

        if (should_have && !has) {
            dom_element_set_attribute(elem, attr_name, "");
        } else if (!should_have && has) {
            dom_element_remove_attribute(elem, attr_name);
        }
        return (Item){.item = b2it(should_have ? 1 : 0)};
    }

    // v12b: insertAdjacentElement(position, newElement)
    if (strcmp(method, "insertAdjacentElement") == 0) {
        if (argc < 2) return ItemNull;
        const char* position = fn_to_cstr(args[0]);
        DomNode* new_node = (DomNode*)js_dom_unwrap_element(args[1]);
        if (!position || !new_node) return ItemNull;

        // detach from old parent
        if (new_node->parent) {
            new_node->parent->remove_child(new_node);
        }

        if (strcasecmp(position, "beforebegin") == 0) {
            if (elem->parent && elem->parent->is_element())
                elem->parent->insert_before(new_node, (DomNode*)elem);
        } else if (strcasecmp(position, "afterbegin") == 0) {
            ((DomNode*)elem)->insert_before(new_node, elem->first_child);
        } else if (strcasecmp(position, "beforeend") == 0) {
            ((DomNode*)elem)->append_child(new_node);
        } else if (strcasecmp(position, "afterend") == 0) {
            if (elem->parent && elem->parent->is_element())
                elem->parent->insert_before(new_node, elem->next_sibling);
        }
        return args[1]; // return the inserted element
    }

    // v12b: insertAdjacentHTML(position, text)
    if (strcmp(method, "insertAdjacentHTML") == 0) {
        if (argc < 2) return ItemNull;
        const char* position = fn_to_cstr(args[0]);
        const char* html_str = fn_to_cstr(args[1]);
        if (!position || !html_str || !elem->doc) return ItemNull;

        DomDocument* doc = elem->doc;
        if (!doc->input) return ItemNull;

        // parse the HTML fragment
        Html5Parser* parser = html5_fragment_parser_create(
            doc->pool, doc->arena, doc->input);
        if (!parser) return ItemNull;
        html5_fragment_parse(parser, html_str);
        Element* body_elem = html5_fragment_get_body(parser);
        if (!body_elem) return ItemNull;

        // determine target parent and reference node based on position
        DomElement* target_parent = nullptr;
        DomNode* ref_node = nullptr;

        if (strcasecmp(position, "beforebegin") == 0) {
            if (!elem->parent || !elem->parent->is_element()) return ItemNull;
            target_parent = elem->parent->as_element();
            ref_node = (DomNode*)elem;
        } else if (strcasecmp(position, "afterbegin") == 0) {
            target_parent = elem;
            ref_node = elem->first_child;
        } else if (strcasecmp(position, "beforeend") == 0) {
            target_parent = elem;
            ref_node = nullptr;
        } else if (strcasecmp(position, "afterend") == 0) {
            if (!elem->parent || !elem->parent->is_element()) return ItemNull;
            target_parent = elem->parent->as_element();
            ref_node = elem->next_sibling;
        } else {
            log_error("insertAdjacentHTML: invalid position '%s'", position);
            return ItemNull;
        }

        // build DOM nodes from parsed fragment and insert
        for (size_t i = 0; i < body_elem->length; i++) {
            TypeId type = get_type_id(body_elem->items[i]);
            if (type == LMD_TYPE_ELEMENT) {
                DomElement* child_dom = build_dom_tree_from_element(
                    body_elem->items[i].element, doc, nullptr);
                if (child_dom) {
                    if (ref_node)
                        ((DomNode*)target_parent)->insert_before((DomNode*)child_dom, ref_node);
                    else
                        ((DomNode*)target_parent)->append_child((DomNode*)child_dom);
                }
            } else if (type == LMD_TYPE_STRING) {
                String* s = it2s(body_elem->items[i]);
                DomText* text_node = dom_text_create_detached(s, doc);
                if (text_node) {
                    if (ref_node)
                        ((DomNode*)target_parent)->insert_before((DomNode*)text_node, ref_node);
                    else
                        ((DomNode*)target_parent)->append_child((DomNode*)text_node);
                }
            }
        }
        return ItemNull;
    }

    // EventTarget interface
    if (strcmp(method, "addEventListener") == 0) {
        if (argc >= 2) {
            js_dom_add_event_listener(elem_item, args[0], args[1], argc > 2 ? args[2] : ItemNull);
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (strcmp(method, "removeEventListener") == 0) {
        if (argc >= 2) {
            js_dom_remove_event_listener(elem_item, args[0], args[1], argc > 2 ? args[2] : ItemNull);
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (strcmp(method, "dispatchEvent") == 0) {
        if (argc >= 1) {
            return js_dom_dispatch_event(elem_item, args[0]);
        }
        return (Item){.item = ITEM_FALSE};
    }

    log_debug("js_dom_element_method: unknown method '%s'", method);
    return ItemNull;
}

// ============================================================================
// classList API (v12)
// ============================================================================

extern "C" Item js_classlist_method(Item elem_item, Item method_name, Item* args, int argc) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        log_error("js_classlist_method: not a DOM element");
        return ItemNull;
    }

    const char* method = fn_to_cstr(method_name);
    if (!method) return ItemNull;

    log_debug("js_classlist_method: '%s' on <%s>", method,
              elem->tag_name ? elem->tag_name : "?");

    // add(className, ...)
    if (strcmp(method, "add") == 0) {
        for (int i = 0; i < argc; i++) {
            const char* cls = fn_to_cstr(args[i]);
            if (cls) dom_element_add_class(elem, cls);
        }
        js_dom_mutation_notify();
        return ItemNull;
    }

    // remove(className, ...)
    if (strcmp(method, "remove") == 0) {
        for (int i = 0; i < argc; i++) {
            const char* cls = fn_to_cstr(args[i]);
            if (cls) dom_element_remove_class(elem, cls);
        }
        js_dom_mutation_notify();
        return ItemNull;
    }

    // toggle(className [, force]) → boolean
    if (strcmp(method, "toggle") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return (Item){.item = ITEM_FALSE};

        if (argc >= 2) {
            // force parameter: add if truthy, remove if falsy
            bool force = js_is_truthy(args[1]);
            if (force) {
                dom_element_add_class(elem, cls);
                js_dom_mutation_notify();
                return (Item){.item = ITEM_TRUE};
            } else {
                dom_element_remove_class(elem, cls);
                js_dom_mutation_notify();
                return (Item){.item = ITEM_FALSE};
            }
        }
        // no force: toggle
        bool result = dom_element_toggle_class(elem, cls);
        js_dom_mutation_notify();
        return (Item){.item = b2it(result ? 1 : 0)};
    }

    // contains(className) → boolean
    if (strcmp(method, "contains") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return (Item){.item = ITEM_FALSE};
        bool has = dom_element_has_class(elem, cls);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    // item(index) → string or null
    if (strcmp(method, "item") == 0) {
        if (argc < 1) return ItemNull;
        int64_t idx = it2i(args[0]);
        if (idx < 0 || idx >= elem->class_count) return ItemNull;
        return (Item){.item = s2it(heap_create_name(elem->class_names[idx]))};
    }

    // replace(oldClass, newClass) → boolean
    if (strcmp(method, "replace") == 0) {
        if (argc < 2) return (Item){.item = ITEM_FALSE};
        const char* old_cls = fn_to_cstr(args[0]);
        const char* new_cls = fn_to_cstr(args[1]);
        if (!old_cls || !new_cls) return (Item){.item = ITEM_FALSE};
        if (!dom_element_has_class(elem, old_cls)) return (Item){.item = ITEM_FALSE};
        dom_element_remove_class(elem, old_cls);
        dom_element_add_class(elem, new_cls);
        js_dom_mutation_notify();
        return (Item){.item = ITEM_TRUE};
    }

    // toString() → space-separated class string
    if (strcmp(method, "toString") == 0) {
        if (elem->class_count == 0) return (Item){.item = s2it(heap_create_name(""))};
        StrBuf* sb = strbuf_new_cap(64);
        for (int i = 0; i < elem->class_count; i++) {
            if (i > 0) strbuf_append_char(sb, ' ');
            strbuf_append_str(sb, elem->class_names[i]);
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    log_debug("js_classlist_method: unknown method '%s'", method);
    return ItemNull;
}

extern "C" Item js_classlist_get_property(Item elem_item, Item prop_name) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    // length
    if (strcmp(prop, "length") == 0) {
        return (Item){.item = i2it((int64_t)elem->class_count)};
    }

    // value — space-separated class string
    if (strcmp(prop, "value") == 0) {
        if (elem->class_count == 0) return (Item){.item = s2it(heap_create_name(""))};
        StrBuf* sb = strbuf_new_cap(64);
        for (int i = 0; i < elem->class_count; i++) {
            if (i > 0) strbuf_append_char(sb, ' ');
            strbuf_append_str(sb, elem->class_names[i]);
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // numeric index → item(index)
    // (not common but classList[0] should work)

    log_debug("js_classlist_get_property: unknown property '%s'", prop);
    return ItemNull;
}

// ============================================================================
// dataset API (v12)
// ============================================================================

// Helper: convert camelCase to data-kebab-case attribute name
// e.g., "fooBar" → "data-foo-bar"
static void camel_to_data_attr(const char* camel, char* buf, size_t buf_size) {
    size_t pos = 0;
    // prefix with "data-"
    const char* prefix = "data-";
    size_t plen = 5;
    if (buf_size <= plen) { buf[0] = '\0'; return; }
    memcpy(buf, prefix, plen);
    pos = plen;

    for (const char* p = camel; *p && pos < buf_size - 2; p++) {
        if (isupper((unsigned char)*p)) {
            buf[pos++] = '-';
            buf[pos++] = (char)tolower((unsigned char)*p);
        } else {
            buf[pos++] = *p;
        }
    }
    buf[pos] = '\0';
}

// Helper: convert data-kebab-case attribute name to camelCase
// e.g., "data-foo-bar" → "fooBar"
static void data_attr_to_camel(const char* attr, char* buf, size_t buf_size) {
    // skip "data-" prefix
    const char* src = attr;
    if (strncmp(src, "data-", 5) == 0) src += 5;

    size_t pos = 0;
    bool next_upper = false;
    for (; *src && pos < buf_size - 1; src++) {
        if (*src == '-') {
            next_upper = true;
        } else {
            buf[pos++] = next_upper ? (char)toupper((unsigned char)*src) : *src;
            next_upper = false;
        }
    }
    buf[pos] = '\0';
}

extern "C" Item js_dataset_get_property(Item elem_item, Item prop_name) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    char attr_name[256];
    camel_to_data_attr(prop, attr_name, sizeof(attr_name));

    const char* val = dom_element_get_attribute(elem, attr_name);
    if (val) {
        return (Item){.item = s2it(heap_create_name(val))};
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_dataset_set_property(Item elem_item, Item prop_name, Item value) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    const char* val_str = fn_to_cstr(value);
    if (!prop || !val_str) return ItemNull;

    char attr_name[256];
    camel_to_data_attr(prop, attr_name, sizeof(attr_name));

    dom_element_set_attribute(elem, attr_name, val_str);
    return value;
}

// ============================================================================
// location API (v12) — document.URL / document.location
// ============================================================================

extern "C" Item js_location_get_property(Item prop_name) {
    if (!_js_current_document) {
        log_debug("js_location_get_property: no document set");
        return (Item){.item = s2it(heap_create_name(""))};
    }

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return (Item){.item = s2it(heap_create_name(""))};

    Url* url = _js_current_document->url;
    if (!url) {
        log_debug("js_location_get_property: document has no URL");
        return (Item){.item = s2it(heap_create_name(""))};
    }

    if (strcmp(prop, "href") == 0) {
        const char* href = url_get_href(url);
        return (Item){.item = s2it(heap_create_name(href ? href : ""))};
    }
    if (strcmp(prop, "protocol") == 0) {
        const char* proto = url_get_protocol(url);
        return (Item){.item = s2it(heap_create_name(proto ? proto : ""))};
    }
    if (strcmp(prop, "hostname") == 0) {
        const char* hostname = url_get_hostname(url);
        return (Item){.item = s2it(heap_create_name(hostname ? hostname : ""))};
    }
    if (strcmp(prop, "port") == 0) {
        const char* port = url_get_port(url);
        return (Item){.item = s2it(heap_create_name(port ? port : ""))};
    }
    if (strcmp(prop, "pathname") == 0) {
        const char* pathname = url_get_pathname(url);
        return (Item){.item = s2it(heap_create_name(pathname ? pathname : ""))};
    }
    if (strcmp(prop, "search") == 0) {
        const char* search = url_get_search(url);
        return (Item){.item = s2it(heap_create_name(search ? search : ""))};
    }
    if (strcmp(prop, "hash") == 0) {
        const char* hash = url_get_hash(url);
        return (Item){.item = s2it(heap_create_name(hash ? hash : ""))};
    }
    if (strcmp(prop, "host") == 0) {
        const char* host = url_get_host(url);
        return (Item){.item = s2it(heap_create_name(host ? host : ""))};
    }
    if (strcmp(prop, "origin") == 0) {
        const char* origin = url_get_origin(url);
        return (Item){.item = s2it(heap_create_name(origin ? origin : ""))};
    }

    log_debug("js_location_get_property: unknown property '%s'", prop);
    return (Item){.item = s2it(heap_create_name(""))};
}

// ============================================================================
// Node.contains() (v12)
// ============================================================================

extern "C" Item js_dom_contains(Item elem_item, Item other_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    DomNode* other = (DomNode*)js_dom_unwrap_element(other_item);
    if (!node || !other) return (Item){.item = ITEM_FALSE};

    // a node contains itself per spec
    if (node == other) return (Item){.item = ITEM_TRUE};

    // walk up from other's parent
    DomNode* current = other->parent;
    while (current) {
        if (current == node) return (Item){.item = ITEM_TRUE};
        current = current->parent;
    }
    return (Item){.item = ITEM_FALSE};
}

// ============================================================================
// style.setProperty() / style.removeProperty() (v12b)
// ============================================================================

extern "C" Item js_dom_style_method(Item elem_item, Item method_name, Item* args, int argc) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        // check if this is a CSSOM rule — transpiler routes rule.style.setProperty() here too
        if (js_is_css_rule(elem_item)) {
            // get the .style wrapper from the rule
            Item style_prop = (Item){.item = s2it(heap_create_name("style"))};
            Item style_decl = js_cssom_rule_get_property(elem_item, style_prop);
            if (js_is_rule_style_decl(style_decl)) {
                return js_cssom_rule_decl_method(style_decl, method_name, args, argc);
            }
        }
        return ItemNull;
    }

    const char* method = fn_to_cstr(method_name);
    if (!method) return ItemNull;

    // setProperty(property, value [, priority])
    if (strcmp(method, "setProperty") == 0) {
        if (argc < 2) return ItemNull;
        const char* css_prop = fn_to_cstr(args[0]);
        const char* val_str = fn_to_cstr(args[1]);
        if (!css_prop || !val_str) return ItemNull;

        char style_decl[256];
        if (argc >= 3) {
            const char* priority = fn_to_cstr(args[2]);
            if (priority && strcasecmp(priority, "important") == 0) {
                snprintf(style_decl, sizeof(style_decl), "%s: %s !important", css_prop, val_str);
            } else {
                snprintf(style_decl, sizeof(style_decl), "%s: %s", css_prop, val_str);
            }
        } else {
            snprintf(style_decl, sizeof(style_decl), "%s: %s", css_prop, val_str);
        }
        int applied = dom_element_apply_inline_style(elem, style_decl);
        elem->styles_resolved = false;
        log_debug("js_dom_style_method: setProperty '%s: %s' on <%s>",
                  css_prop, val_str, elem->tag_name ? elem->tag_name : "?");
        return ItemNull;
    }

    // removeProperty(property) — returns old value
    if (strcmp(method, "removeProperty") == 0) {
        if (argc < 1) return (Item){.item = s2it(heap_create_name(""))};
        const char* css_prop = fn_to_cstr(args[0]);
        if (!css_prop) return (Item){.item = s2it(heap_create_name(""))};

        // get old value before removing
        CssPropertyId prop_id = css_property_id_from_name(css_prop);
        Item old_val = (Item){.item = s2it(heap_create_name(""))};
        if (prop_id != CSS_PROPERTY_UNKNOWN && elem->specified_style) {
            CssDeclaration* decl = dom_element_get_specified_value(elem, prop_id);
            if (decl && decl->specificity.inline_style) {
                // serialize old value via the getter
                Item prop_item = (Item){.item = s2it(heap_create_name(css_prop))};
                old_val = js_dom_get_style_property(elem_item, prop_item);
            }
            // remove the declaration from the style tree
            style_tree_remove_property(elem->specified_style, prop_id);
        }
        elem->styles_resolved = false;
        log_debug("js_dom_style_method: removeProperty '%s' on <%s>",
                  css_prop, elem->tag_name ? elem->tag_name : "?");
        return old_val;
    }

    log_debug("js_dom_style_method: unknown method '%s'", method);
    return ItemNull;
}
