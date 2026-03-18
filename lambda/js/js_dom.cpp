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
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../mark_builder.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_style_node.hpp"
#include "../input/css/selector_matcher.hpp"

#include <cstring>
#include <cctype>

// Forward declarations
static void js_camel_to_css_prop(const char* js_prop, char* css_buf, size_t buf_size);
static CssDeclaration* js_match_element_property(DomElement* elem, CssPropertyId prop_id, int pseudo_type);

// ============================================================================
// Unique type marker for DOM-wrapped Maps
// ============================================================================

// Sentinel used in Map::type to distinguish DOM wrappers from regular Maps.
// Address uniqueness is all that matters; the content is unused.
static TypeMap js_dom_type_marker = {};

// Sentinel used in Map::type to distinguish computed style wrappers.
// Map::data stores the DomElement*, Map::data_cap stores pseudo-element type (0=none, 1=before, 2=after).
static TypeMap js_computed_style_marker = {};

// ============================================================================
// Thread-local DOM document context
// ============================================================================

static __thread DomDocument* _js_current_document = nullptr;

// ============================================================================
// DOM Context Management
// ============================================================================

extern "C" void js_dom_set_document(void* dom_doc) {
    _js_current_document = (DomDocument*)dom_doc;
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
    wrapper->type = (void*)&js_computed_style_marker;
    wrapper->data = node->as_element();     // store DomElement*
    wrapper->data_cap = pseudo_type;        // store pseudo type

    log_debug("js_get_computed_style: created wrapper for <%s> pseudo=%d",
              node->as_element()->tag_name ? node->as_element()->tag_name : "?", pseudo_type);

    return (Item){.map = wrapper};
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
    if (prop_id == CSS_PROPERTY_UNKNOWN) {
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
                if (!decl || decl->property_id != prop_id) continue;

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
    char* upper = (len < sizeof(buf)) ? buf : (char*)malloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        upper[i] = (char)toupper((unsigned char)tag_name[i]);
    }
    upper[len] = '\0';
    String* result = heap_create_name(upper);
    if (upper != buf) free(upper);
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
        if (!selector) return ItemNull;

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

    log_debug("js_document_get_property: unknown property '%s'", prop);
    return ItemNull;
}

// ============================================================================
// Element Property Access
// ============================================================================

extern "C" Item js_dom_get_property(Item elem_item, Item prop_name) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node) {
        log_debug("js_dom_get_property: not a DOM node");
        return ItemNull;
    }

    const char* prop = fn_to_cstr(prop_name);
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
        }
        return value;
    }

    // generic property set via setAttribute
    const char* val_str = fn_to_cstr(value);
    if (val_str) {
        dom_element_set_attribute(elem, prop, val_str);
    }
    return value;
}

extern "C" Item js_dom_set_style_property(Item elem_item, Item prop_name, Item value) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        log_debug("js_dom_set_style_property: not a DOM element");
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
        log_debug("js_dom_set_style_property: set cssText='%.50s' on <%s>",
                  val_str, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    // build a single-declaration inline style string: "property: value"
    char style_decl[256];
    snprintf(style_decl, sizeof(style_decl), "%s: %s", css_prop, val_str);

    // apply as inline style (highest cascade priority)
    dom_element_apply_inline_style(elem, style_decl);
    elem->styles_resolved = false;  // mark for re-cascading

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
        log_debug("js_dom_get_style_property: not a DOM element");
        return (Item){.item = s2it(heap_create_name(""))};
    }

    const char* js_prop = fn_to_cstr(prop_name);
    if (!js_prop) return (Item){.item = s2it(heap_create_name(""))};

    // convert camelCase JS property to CSS property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));

    // look up the CSS property ID
    CssPropertyId prop_id = css_property_id_from_name(css_prop);
    if (prop_id == CSS_PROPERTY_UNKNOWN) {
        log_debug("js_dom_get_style_property: unknown CSS property '%s'", css_prop);
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // get the specified value for this property
    CssDeclaration* decl = dom_element_get_specified_value(elem, prop_id);
    if (!decl || !decl->value) {
        return (Item){.item = s2it(heap_create_name(""))};
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
        default:
            break;
    }

    return (Item){.item = s2it(heap_create_name(""))};
}

// ============================================================================
// Element Method Dispatcher
// ============================================================================

extern "C" Item js_dom_element_method(Item elem_item, Item method_name, Item* args, int argc) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        log_error("js_dom_element_method: not a DOM element");
        return ItemNull;
    }

    const char* method = fn_to_cstr(method_name);
    if (!method) {
        log_error("js_dom_element_method: invalid method name");
        return ItemNull;
    }

    log_debug("js_dom_element_method: '%s' on <%s>", method,
              elem->tag_name ? elem->tag_name : "?");

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
        // detach child from current parent if any
        if (child_node->parent) {
            DomNode* old_parent = child_node->parent;
            if (old_parent->is_element()) {
                old_parent->remove_child(child_node);
            }
        }
        // use DomNode::append_child which handles all node types
        ((DomNode*)elem)->append_child(child_node);
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
        return args[0];  // return the removed child
    }

    // insertBefore(newChild, refChild)
    if (strcmp(method, "insertBefore") == 0) {
        if (argc < 2) return ItemNull;
        DomNode* new_child = (DomNode*)js_dom_unwrap_element(args[0]);
        DomNode* ref_child = (DomNode*)js_dom_unwrap_element(args[1]);
        if (!new_child) return ItemNull;
        // detach from old parent
        if (new_child->parent) {
            new_child->parent->remove_child(new_child);
        }
        ((DomNode*)elem)->insert_before(new_child, ref_child);
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

    log_debug("js_dom_element_method: unknown method '%s'", method);
    return ItemNull;
}
