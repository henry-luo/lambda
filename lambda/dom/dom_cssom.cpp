/**
 * JavaScript CSSOM (CSS Object Model) Bridge Implementation
 *
 * Wraps CssStylesheet, CssRule, and CssDeclaration structures for JS access.
 * Uses branded native VMaps for stylesheet/rule/declaration host objects.
 */

#include "dom_cssom.h"
#include "dom.h"
#include "../js/js_runtime.h"
#include "../js/js_class.h"
#include "../js/js_object_meta.h"
#include "../runtime/lambda-root-frame.hpp"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mem_factory.h"
#include "../../lib/mempool.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/style_epoch.hpp"
#include "../input/css/css_engine.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_style.hpp"
#include "../input/css/css_formatter.hpp"

#include <cstring>
#include "../../lib/mem_grow.hpp"

extern String* heap_create_name(const char* name, size_t len);
extern "C" Item vmap_new(void);
extern "C" const void* radiant_dom_stylesheet_host_type(void);
extern "C" const void* radiant_dom_css_rule_host_type(void);
extern "C" const void* radiant_dom_rule_style_decl_host_type(void);

// Forward declaration
static Pool* get_document_pool();
extern "C" void dom_notify_mutation(DomJsMutationKind kind, void* target, void* parent);

static void js_cssom_notify_stylesheet_mutation(CssStylesheet* stylesheet = nullptr) {
    // stylesheet edits do not touch a DOM node, but they still require post-script cascade.
    DomDocument* doc = stylesheet && stylesheet->owner_style_element
        ? stylesheet->owner_style_element->doc : (DomDocument*)dom_get_document();
    style_epoch_mark_global_change(doc);
    DomElement* owner = stylesheet ? stylesheet->owner_style_element : nullptr;
    dom_notify_mutation(DOM_JS_MUTATION_STYLE, owner, owner ? owner->parent : nullptr);
}

// =============================================================================
// Sentinel Markers for CSSOM Types
// =============================================================================

static const char js_stylesheet_vmap_marker = 0;
static const char js_css_rule_vmap_marker = 0;
static const char js_rule_decl_vmap_marker = 0;

// Legacy map markers are accepted by the predicates only so old callers fail
// through the same native unwrap path while CSSOM wrappers move to VMaps.
static TypeMap js_stylesheet_marker = {};
static TypeMap js_css_rule_marker = {};
static TypeMap js_rule_decl_marker = {};

// =============================================================================
// Type Checking
// =============================================================================

static bool js_cssom_is_host(Item item, const void* legacy_marker,
                             const void* vmap_marker,
                             const void* (*host_type)(void)) {
    if (get_type_id(item) == LMD_TYPE_VMAP) {
        return item.vmap &&
            (item.vmap->host_type == vmap_marker ||
             item.vmap->host_type == host_type()) &&
            item.vmap->host_data != nullptr;
    }
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    return item.map->type == legacy_marker;
}
JS_FORWARD_RETURN(bool, dom_is_stylesheet, (Item item), js_cssom_is_host, (item, &js_stylesheet_marker, &js_stylesheet_vmap_marker, radiant_dom_stylesheet_host_type))
JS_FORWARD_RETURN(bool, dom_is_css_rule, (Item item), js_cssom_is_host, (item, &js_css_rule_marker, &js_css_rule_vmap_marker, radiant_dom_css_rule_host_type))
JS_FORWARD_RETURN(bool, dom_is_rule_style_decl, (Item item), js_cssom_is_host, (item, &js_rule_decl_marker, &js_rule_decl_vmap_marker, radiant_dom_rule_style_decl_host_type))

// =============================================================================
// Helper: camelCase to CSS hyphenated property name
// =============================================================================

static void cssom_camel_to_css_prop(const char* js_prop, char* css_buf, size_t buf_size) {
    size_t pos = 0;
    for (const char* p = js_prop; *p && pos < buf_size - 2; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            css_buf[pos++] = '-';
            css_buf[pos++] = (char)(*p + 32);
        } else {
            css_buf[pos++] = *p;
        }
    }
    css_buf[pos] = '\0';
}


// =============================================================================
// Helper: Create a string Item
// =============================================================================

// =============================================================================
// CSSStyleSheet Wrapper
// =============================================================================

extern "C" Item dom_cssom_wrap_stylesheet(void* stylesheet) {
    if (!stylesheet) return ItemNull;

    Item wrapper = vmap_new();
    if (get_type_id(wrapper) != LMD_TYPE_VMAP || !wrapper.vmap) return ItemNull;
    // CSSOM wrappers have no Map shell; the host brand is the unwrap invariant.
    wrapper.vmap->host_type = radiant_dom_stylesheet_host_type();
    wrapper.vmap->host_data = stylesheet;

    log_debug("dom_cssom_wrap_stylesheet: wrapped CssStylesheet=%p as VMap=%p", stylesheet, (void*)wrapper.vmap);
    return wrapper;
}

static void* js_cssom_unwrap_host(Item item, bool (*is_host)(Item)) {
    if (!is_host(item)) return nullptr;
    if (get_type_id(item) == LMD_TYPE_VMAP) return item.vmap->host_data;
    return item.map->data;
}

static CssStylesheet* unwrap_stylesheet(Item item) {
    return (CssStylesheet*)js_cssom_unwrap_host(item, dom_is_stylesheet);
}

// =============================================================================
// Font-Face Declaration Parsing (lazy on .style access)
// =============================================================================

// use the CssRule's legacy compatibility fields to cache parsed declarations
// for font-face rules. property_count stores declaration count, and the paired
// legacy pointers hold the cached shadow CssRule.
static CssRule* get_font_face_as_style_rule(CssRule* rule) {
    if (!rule || (rule->type != CSS_RULE_FONT_FACE && rule->type != CSS_RULE_PAGE)) return nullptr;

    Pool* pool = rule->pool ? rule->pool : get_document_pool();
    if (!pool) return nullptr;

    // keep lazy parsing for descriptor rules, but let the shared CSS parser
    // own declaration-list tokenization and error recovery.
    if (rule->property_count > 0 && rule->property_names) {
        return (CssRule*)rule->property_values;
    }

    const char* content = rule->data.generic_rule.content;
    if (!content) return nullptr;

    size_t decl_count = 0;
    CssDeclaration** decls = css_parse_declaration_list_text(
        content, strlen(content), pool, &decl_count);
    if (!decls || decl_count == 0) return nullptr;

    // create a shadow CssRule of type CSS_RULE_STYLE to hold the declarations
    CssRule* shadow = (CssRule*)pool_calloc(pool, sizeof(CssRule));
    if (!shadow) return nullptr;
    shadow->type = CSS_RULE_STYLE;
    shadow->pool = pool;
    shadow->data.style_rule.declarations = decls;
    shadow->data.style_rule.declaration_count = decl_count;
    shadow->data.style_rule.selector = nullptr;
    shadow->data.style_rule.selector_group = nullptr;

    // cache the shadow rule in the original font-face rule's legacy fields
    rule->property_count = decl_count;
    rule->property_values = (CssValue**)shadow;    // repurposed: stores CssRule*
    rule->property_names = (const char**)shadow;   // sentinel for "parsed"

    log_debug("get_font_face_as_style_rule: parsed %zu declarations from font-face content", decl_count);
    return shadow;
}

// =============================================================================
// CSSRule Wrapper
// =============================================================================

extern "C" Item dom_cssom_wrap_rule(void* rule, void* pool) {
    (void)pool;
    if (!rule) return ItemNull;

    Item wrapper = vmap_new();
    if (get_type_id(wrapper) != LMD_TYPE_VMAP || !wrapper.vmap) return ItemNull;
    // CSS rule wrappers have no Map shell; the host brand is the unwrap invariant.
    wrapper.vmap->host_type = radiant_dom_css_rule_host_type();
    wrapper.vmap->host_data = rule;

    log_debug("dom_cssom_wrap_rule: wrapped CssRule=%p as VMap=%p", rule, (void*)wrapper.vmap);
    return wrapper;
}

static CssRule* unwrap_rule(Item item) {
    return (CssRule*)js_cssom_unwrap_host(item, dom_is_css_rule);
}

// =============================================================================
// CSSStyleDeclaration (rule declarations) Wrapper
// =============================================================================

static Item wrap_rule_decl(CssRule* rule, Pool* pool) {
    (void)pool;
    if (!rule) return ItemNull;

    Item wrapper = vmap_new();
    if (get_type_id(wrapper) != LMD_TYPE_VMAP || !wrapper.vmap) return ItemNull;
    // Rule declaration wrappers have no Map shell; the host brand is the unwrap invariant.
    wrapper.vmap->host_type = radiant_dom_rule_style_decl_host_type();
    wrapper.vmap->host_data = rule;

    return wrapper;
}

static Item wrap_nested_declarations(CssRule* rule, Pool* pool) {
    RootFrame roots(5);
    Rooted<Item> result_root(roots,
        js_new_object_with_class(JS_CLASS_CSS_NESTED_DECLARATIONS));
    Rooted<Item> style_root(roots, wrap_rule_decl(rule, pool));
    Rooted<Item> global_root(roots, js_get_global_this());
    Rooted<Item> ctor_root(roots,
        js_get_key_cstr(global_root.get(), "CSSNestedDeclarations"));
    Rooted<Item> proto_root(roots, js_get_key_cstr(ctor_root.get(), "prototype"));
    // Class metadata does not participate in ordinary instanceof; CSSOM
    // wrappers must inherit the realm's exposed interface prototype.
    if (get_type_id(proto_root.get()) == LMD_TYPE_MAP) {
        js_set_prototype(result_root.get(), proto_root.get());
    }
    js_set_key_cstr(result_root.get(), "style", style_root.get());
    return result_root.get();
}

static CssRule* unwrap_rule_decl(Item item) {
    return (CssRule*)js_cssom_unwrap_host(item, dom_is_rule_style_decl);
}

static Pool* unwrap_rule_decl_pool(Item item) {
    CssRule* rule = unwrap_rule_decl(item);
    if (!rule) return nullptr;
    return rule->pool ? rule->pool : get_document_pool();
}

// =============================================================================
// Helper: Get the Pool from current document
// =============================================================================

static Pool* get_document_pool() {
    DomDocument* doc = (DomDocument*)dom_get_document();
    return doc ? doc->document_pool : nullptr;
}

// =============================================================================
// Helper: Serialize a selector group to text
// =============================================================================

static const char* serialize_selector_text(CssRule* rule, Pool* pool) {
    if (!rule || rule->type != CSS_RULE_STYLE) return "";
    if (!pool) return "";

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    CssSelector* single = rule->data.style_rule.selector;

    if (!group && !single) return "";

    CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!fmt) return "";

    // if there's a selector group, use it
    if (group) {
        const char* text = css_format_selector_group(fmt, group);
        return text ? text : "";
    }

    // single selector: create a temporary group
    CssSelectorGroup temp_group;
    temp_group.selectors = &single;
    temp_group.selector_count = 1;
    const char* text = css_format_selector_group(fmt, &temp_group);
    return text ? text : "";
}

static const char* copy_cssom_value_text(const char* value, Pool* pool) {
    if (!value || !pool) return "";
    size_t len = strlen(value);
    char* copy = (char*)pool_calloc(pool, len + 1);
    if (!copy) return "";
    memcpy(copy, value, len);
    copy[len] = '\0';
    return copy;
}

static void append_rule_declaration_text(StringBuf* buf, CssDeclaration* decl, Pool* pool) {
    if (!buf || !decl || !pool) return;

    const char* name = decl->property_name ? decl->property_name : css_property_spelling_from_code(decl->property_code);
    if (!name) return;

    stringbuf_append_str(buf, name);
    stringbuf_append_str(buf, ": ");
    stringbuf_append_str(buf, css_serialize_declaration_value(decl, pool));
    if (decl->important) {
        stringbuf_append_str(buf, " !important");
    }
}

static const char* serialize_style_rule_css_text(CssRule* rule, Pool* pool) {
    if (!rule || !pool || rule->type != CSS_RULE_STYLE) return "";

    StringBuf* buf = stringbuf_new(pool);
    if (!buf) return "";

    stringbuf_append_str(buf, serialize_selector_text(rule, pool));
    stringbuf_append_str(buf, "{");
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* decl = rule->data.style_rule.declarations[i];
        if (!decl) continue;
        if (i > 0) stringbuf_append_str(buf, " ");
        append_rule_declaration_text(buf, decl, pool);
        stringbuf_append_str(buf, ";");
    }
    stringbuf_append_str(buf, "}");

    String* result = stringbuf_to_string(buf);
    return result ? result->chars : "";
}

// =============================================================================
// CSSStyleSheet Property Access
// =============================================================================

// Receiver-explicit per-property getters (DOM3 declared-interface bindings).
extern "C" Item dom_cssom_stylesheet_get_css_rules(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    Pool* pool = get_document_pool();
    // array of wrapped CSSRule objects (excluding @charset per CSSOM spec)
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    for (size_t i = 0; i < sheet->rule_count; i++) {
        if (sheet->rules[i] && sheet->rules[i]->type == CSS_RULE_CHARSET) continue;
        array_push(arr, dom_cssom_wrap_rule(sheet->rules[i], pool));
    }
    return (Item){.array = arr};
}

extern "C" Item dom_cssom_stylesheet_get_length(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    // exclude @charset rules from length count
    size_t count = 0;
    for (size_t i = 0; i < sheet->rule_count; i++) {
        if (sheet->rules[i] && sheet->rules[i]->type == CSS_RULE_CHARSET) continue;
        count++;
    }
    return (Item){.item = i2it((int64_t)count)};
}

extern "C" Item dom_cssom_stylesheet_get_disabled(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    return sheet->disabled ? (Item){.item = ITEM_TRUE} : (Item){.item = ITEM_FALSE};
}

extern "C" bool dom_cssom_stylesheet_set_disabled(Item sheet_item, bool disabled) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return false;
    if (sheet->disabled == disabled) return true;
    sheet->disabled = disabled;
    // CSSOM's disabled flag changes which rules participate in the cascade.
    js_cssom_notify_stylesheet_mutation(sheet);
    return true;
}
JS_FORWARD_EXPRESSION(Item, dom_cssom_stylesheet_get_type, (Item sheet_item), (unwrap_stylesheet(sheet_item) ? make_string_item("text/css") : ItemNull))

extern "C" Item dom_cssom_stylesheet_get_href(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    return sheet ? make_string_item(sheet->href) : ItemNull;
}

extern "C" Item dom_cssom_stylesheet_get_title(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    return sheet ? make_string_item(sheet->title) : ItemNull;
}

extern "C" Item dom_cssom_stylesheet_index(Item sheet_item, int64_t index) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    // raw index into the rules array (charset rules included), matching the
    // legacy bracket-access path
    if (index < 0 || (size_t)index >= sheet->rule_count) return ItemNull;
    return dom_cssom_wrap_rule(sheet->rules[index], get_document_pool());
}

// =============================================================================
// CSSStyleSheet Method Dispatch
// =============================================================================

extern "C" Item dom_cssom_insert_rule(Item sheet_item, Item text_arg, Item index_arg) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    // index omitted (undefined/null) defaults to append-at-end per CSSOM
    TypeId index_type = get_type_id(index_arg);
    bool has_index = index_type == LMD_TYPE_INT || index_type == LMD_TYPE_INT64 ||
                     index_type == LMD_TYPE_FLOAT;
    Item args[2] = {text_arg, index_arg};
    int argc = has_index ? 2 : 1;
    (void)args; (void)argc;
    if (argc < 1) return ItemNull;

    String* rule_string = it2s(args[0]);
    const char* rule_text = rule_string ? rule_string->chars : fn_to_cstr(args[0]);
    size_t rule_length = rule_string ? rule_string->len : (rule_text ? strlen(rule_text) : 0);
    if (!rule_text) return ItemNull;

    int index = (argc >= 2) ? (int)it2i(args[1]) : (int)sheet->rule_count;

    // validate index
    if (index < 0 || (size_t)index > sheet->rule_count) {
        log_error("js_cssom_stylesheet_method insertRule: index %d out of range [0, %zu]", index, sheet->rule_count);
        return ItemNull;
    }

    // parse the rule text
    Pool* pool = sheet->pool ? sheet->pool : get_document_pool();
    if (!pool) return ItemNull;

    CssRule* new_rule = css_parse_rule_text(rule_text, rule_length, pool);
    if (!new_rule) {
        log_error("js_cssom_stylesheet_method insertRule: failed to parse rule '%s'", rule_text);
        return ItemNull;
    }

    // ensure capacity
    if (sheet->rule_count >= sheet->rule_capacity) {
        size_t new_cap = sheet->rule_capacity ? sheet->rule_capacity * 2 : 8;
        CssRule** new_rules = (CssRule**)pool_calloc(pool, new_cap * sizeof(CssRule*));
        if (sheet->rules) {
            memcpy(new_rules, sheet->rules, sheet->rule_count * sizeof(CssRule*));
        }
        sheet->rules = new_rules;
        sheet->rule_capacity = new_cap;
    }

    // shift rules to make room
    for (size_t i = sheet->rule_count; i > (size_t)index; i--) {
        sheet->rules[i] = sheet->rules[i - 1];
    }
    sheet->rules[index] = new_rule;
    sheet->rule_count++;

    log_debug("js_cssom_stylesheet_method insertRule: inserted at index %d, count=%zu", index, sheet->rule_count);
    js_cssom_notify_stylesheet_mutation(sheet);
    return (Item){.item = i2it((int64_t)index)};
}

extern "C" Item dom_cssom_delete_rule(Item sheet_item, Item index_arg) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    Item args[1] = {index_arg};
    int argc = 1;
    (void)args; (void)argc;
    if (argc < 1) return ItemNull;

    int index = (int)it2i(args[0]);
    if (index < 0 || (size_t)index >= sheet->rule_count) {
        log_error("js_cssom_stylesheet_method deleteRule: index %d out of range", index);
        return ItemNull;
    }

    // shift rules down
    for (size_t i = (size_t)index; i < sheet->rule_count - 1; i++) {
        sheet->rules[i] = sheet->rules[i + 1];
    }
    sheet->rule_count--;

    log_debug("js_cssom_stylesheet_method deleteRule: removed index %d, count=%zu", index, sheet->rule_count);
    js_cssom_notify_stylesheet_mutation(sheet);
    return ItemNull;
}

// =============================================================================
// CSSStyleRule Property Access
// =============================================================================

// Receiver-explicit per-property getters (DOM3 declared-interface bindings).
extern "C" Item dom_cssom_rule_get_selector_text(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (rule->type != CSS_RULE_STYLE) return make_string_item("");
    const char* sel_text = serialize_selector_text(rule, pool);
    // CSS Nesting: nested rules get '& ' prefix
    if (rule->parent && sel_text && sel_text[0] != '\0') {
        // Always prepend '& ' for nested selectors
        size_t len = strlen(sel_text);
        char* nested_text = (char*)pool_calloc(pool, len + 3);
        memcpy(nested_text, "& ", 2);
        memcpy(nested_text + 2, sel_text, len + 1);
        return make_string_item(nested_text);
    }
    return make_string_item(sel_text);
}

extern "C" Item dom_cssom_rule_get_style(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (rule->type == CSS_RULE_STYLE || rule->type == CSS_RULE_NESTED_DECLARATIONS) {
        return wrap_rule_decl(rule, pool);
    }
    // font-face and page rules also expose .style
    if (rule->type == CSS_RULE_FONT_FACE || rule->type == CSS_RULE_PAGE) {
        CssRule* shadow = get_font_face_as_style_rule(rule);
        if (shadow) {
            return wrap_rule_decl(shadow, pool);
        }
    }
    return ItemNull;
}

extern "C" Item dom_cssom_rule_get_css_rules(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (rule->type == CSS_RULE_STYLE) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        size_t nr_count = rule->data.style_rule.nested_rule_count;
        CssRule** nr = rule->data.style_rule.nested_rules;
        for (size_t i = 0; i < nr_count; i++) {
            if (!nr[i]) continue;
            if (nr[i]->type == CSS_RULE_NESTED_DECLARATIONS) {
                array_push(arr, wrap_nested_declarations(nr[i], pool));
            } else {
                array_push(arr, dom_cssom_wrap_rule(nr[i], pool));
            }
        }
        return (Item){.array = arr};
    }
    return ItemNull;
}

extern "C" Item dom_cssom_rule_get_css_text(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (!pool) return make_string_item("");
    if (rule->type == CSS_RULE_STYLE) {
        return make_string_item(serialize_style_rule_css_text(rule, pool));
    }
    CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!fmt) return make_string_item("");
    const char* text = css_format_rule(fmt, rule);
    return make_string_item(text);
}

extern "C" Item dom_cssom_rule_get_type(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    // CSSOM rule type constants
    int type_num = 0;
    switch (rule->type) {
        case CSS_RULE_STYLE:     type_num = 1; break;
        case CSS_RULE_CHARSET:   type_num = 2; break;
        case CSS_RULE_IMPORT:    type_num = 3; break;
        case CSS_RULE_MEDIA:     type_num = 4; break;
        case CSS_RULE_FONT_FACE: type_num = 5; break;
        case CSS_RULE_PAGE:      type_num = 6; break;
        case CSS_RULE_KEYFRAMES: type_num = 7; break;
        case CSS_RULE_KEYFRAME:  type_num = 8; break;
        case CSS_RULE_NAMESPACE: type_num = 10; break;
        case CSS_RULE_SUPPORTS:  type_num = 12; break;
        case CSS_RULE_LAYER:     type_num = 16; break;
        default:                 type_num = 0; break;
    }
    return (Item){.item = i2it((int64_t)type_num)};
}

extern "C" Item dom_cssom_rule_get_parent_rule(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (rule->parent) {
        return dom_cssom_wrap_rule(rule->parent, pool);
    }
    return ItemNull;
}

extern "C" Item dom_cssom_rule_set_selector_text(Item rule_item, Item value) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;

    if (rule->type == CSS_RULE_STYLE) {
        // Use String* to get actual length (handles embedded NULLs)
        String* val_string = it2s(value);
        const char* new_text = val_string ? val_string->chars : fn_to_cstr(value);
        size_t new_text_len = val_string ? val_string->len : (new_text ? strlen(new_text) : 0);
        if (!new_text || new_text_len == 0) return value;

        Pool* pool = (rule && rule->pool) ? rule->pool : get_document_pool();
        if (!pool) return value;

        CssSelectorGroup* new_group = css_parse_selector_group_text(new_text, new_text_len, pool);
        if (!new_group || new_group->selector_count == 0) {
            log_debug("js_cssom_rule_set_property: failed to parse selectorText '%s'", new_text);
            return value;  // silently ignore
        }

        // replace the rule's selectors
        rule->data.style_rule.selector_group = new_group;
        if (new_group->selector_count == 1) {
            rule->data.style_rule.selector = new_group->selectors[0];
        } else {
            rule->data.style_rule.selector = nullptr;
        }

        log_debug("js_cssom_rule_set_property: updated selectorText to '%s'", new_text);
        return value;
    }

    return value;
}

// =============================================================================
// CSSStyleDeclaration (rule) Property Access
// =============================================================================

extern "C" Item dom_cssom_rule_decl_get_property(Item decl_item, Item prop_name) {
    CssRule* rule = unwrap_rule_decl(decl_item);
    if (!rule || (rule->type != CSS_RULE_STYLE && rule->type != CSS_RULE_NESTED_DECLARATIONS)) return make_string_item("");

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return make_string_item("");

    Pool* pool = unwrap_rule_decl_pool(decl_item);
    if (!pool) pool = get_document_pool();

    // special: length
    if (strcmp(prop, "length") == 0) {
        return (Item){.item = i2it((int64_t)rule->data.style_rule.declaration_count)};
    }

    // special: cssText — serialize all declarations
    if (strcmp(prop, "cssText") == 0) {
        if (!pool) return make_string_item("");
        StringBuf* buf = stringbuf_new(pool);
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* d = rule->data.style_rule.declarations[i];
            if (!d) continue;
            if (i > 0) stringbuf_append_str(buf, " ");
            append_rule_declaration_text(buf, d, pool);
            stringbuf_append_str(buf, ";");
        }
        String* result = stringbuf_to_string(buf);
        return make_string_item(result ? result->chars : "");
    }

    // convert camelCase to CSS property
    char css_prop[128];
    cssom_camel_to_css_prop(prop, css_prop, sizeof(css_prop));

    // search declarations for this property — last matching wins (CSS cascade)
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);

    CssDeclaration* last_match = nullptr;
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* decl = rule->data.style_rule.declarations[i];
        if (!decl) continue;

        // match by property ID or by name (for custom properties)
        bool match = false;
        if (prop_id != CSS_PROPERTY_UNKNOWN && decl->property_code == prop_id) {
            match = true;
        } else if (decl->property_name && strcmp(decl->property_name, css_prop) == 0) {
            match = true;
        }

        if (match) {
            last_match = decl;
        }
    }

    if (last_match) {
        const char* val = css_serialize_declaration_value(last_match, pool);
        log_debug("dom_cssom_rule_decl_get_property: '%s' -> '%s'", prop, val);
        return make_string_item(val);
    }

    // not found — return empty string (per CSSOM spec)
    log_debug("dom_cssom_rule_decl_get_property: '%s' not found in rule", prop);
    return make_string_item("");
}

// =============================================================================
// CSSStyleDeclaration (rule) Property Set
// =============================================================================

extern "C" Item dom_cssom_rule_decl_set_property(Item decl_item, Item prop_name, Item value) {
    CssRule* rule = unwrap_rule_decl(decl_item);
    if (!rule || (rule->type != CSS_RULE_STYLE && rule->type != CSS_RULE_NESTED_DECLARATIONS)) return value;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return value;

    String* value_string = it2s(value);
    const char* val_str = value_string ? value_string->chars : fn_to_cstr(value);
    size_t val_len = value_string ? value_string->len : (val_str ? strlen(val_str) : 0);
    if (!val_str) val_str = "";

    Pool* pool = rule->pool;
    if (!pool) pool = get_document_pool();
    if (!pool) return value;

    // convert camelCase to CSS property
    char css_prop[128];
    cssom_camel_to_css_prop(prop, css_prop, sizeof(css_prop));

    // special handling for unicode-range descriptor (font-face)
    if (strcmp(css_prop, "unicode-range") == 0) {
        const char* canonical = css_parse_unicode_range_canonical(val_str, val_len, pool);
        if (!canonical) {
            // invalid unicode-range — silently ignore (per CSSOM spec)
            log_debug("dom_cssom_rule_decl_set_property: invalid unicode-range '%s'", val_str);
            return value;
        }

        // create a declaration with the canonical value
        CssDeclaration* new_decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
        if (!new_decl) return value;
        new_decl->property_name = (char*)pool_alloc(pool, strlen(css_prop) + 1);
        if (new_decl->property_name) strcpy((char*)new_decl->property_name, css_prop); // UNSAFE_LIBC_OK: dst allocated with strlen(css_prop)+1
        new_decl->value_text = canonical;
        new_decl->value_text_len = strlen(canonical);
        new_decl->valid = true;
        new_decl->property_code = css_property_code_from_name(css_prop);

        // find and replace or append
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* d = rule->data.style_rule.declarations[i];
            if (!d) continue;
            if (d->property_name && strcmp(d->property_name, css_prop) == 0) {
                rule->data.style_rule.declarations[i] = new_decl;
                log_debug("dom_cssom_rule_decl_set_property: replaced unicode-range = '%s'", canonical);
                js_cssom_notify_stylesheet_mutation();
                return value;
            }
        }
        // append
        size_t count = rule->data.style_rule.declaration_count;
        CssDeclaration** new_decls = (CssDeclaration**)pool_calloc(pool, (count + 1) * sizeof(CssDeclaration*));
        if (new_decls) {
            if (rule->data.style_rule.declarations && count > 0)
                memcpy(new_decls, rule->data.style_rule.declarations, count * sizeof(CssDeclaration*));
            new_decls[count] = new_decl;
            rule->data.style_rule.declarations = new_decls;
            rule->data.style_rule.declaration_count = count + 1;
        }
        log_debug("dom_cssom_rule_decl_set_property: added unicode-range = '%s'", canonical);
        js_cssom_notify_stylesheet_mutation();
        return value;
    }

    // parse the property/value pair through the shared CSS fragment parser.
    CssDeclaration* new_decl = css_parse_property_declaration(
        css_prop, strlen(css_prop), val_str, val_len, pool);
    if (!new_decl) {
        // parse error — silently ignore (per CSSOM spec)
        log_debug("dom_cssom_rule_decl_set_property: parse error for '%s: %s'", css_prop, val_str);
        return value;
    }
    // Retain authored token text for custom properties, var() values, and the
    // parse-failure fallback; ordinary CSSOM reads use the parsed canonical value.
    new_decl->value_text = copy_cssom_value_text(val_str, pool);
    new_decl->value_text_len = strlen(new_decl->value_text);

    // find and replace existing declaration with same property
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* d = rule->data.style_rule.declarations[i];
        if (!d) continue;
        bool match = false;
        if (prop_id != CSS_PROPERTY_UNKNOWN && d->property_code == prop_id) match = true;
        else if (d->property_name && strcmp(d->property_name, css_prop) == 0) match = true;

        if (match) {
            rule->data.style_rule.declarations[i] = new_decl;
            log_debug("dom_cssom_rule_decl_set_property: replaced '%s' = '%s'", css_prop, val_str);
            js_cssom_notify_stylesheet_mutation();
            return value;
        }
    }

    // not found — append new declaration
    size_t count = rule->data.style_rule.declaration_count;
    CssDeclaration** new_decls = (CssDeclaration**)pool_calloc(pool, (count + 1) * sizeof(CssDeclaration*));
    if (new_decls) {
        if (rule->data.style_rule.declarations && count > 0) {
            memcpy(new_decls, rule->data.style_rule.declarations, count * sizeof(CssDeclaration*));
        }
        new_decls[count] = new_decl;
        rule->data.style_rule.declarations = new_decls;
        rule->data.style_rule.declaration_count = count + 1;
    }
    log_debug("dom_cssom_rule_decl_set_property: added '%s' = '%s'", css_prop, val_str);
    js_cssom_notify_stylesheet_mutation();
    return value;
}

// =============================================================================
// CSSStyleDeclaration (rule) Method Dispatch
// =============================================================================

extern "C" Item dom_cssom_rule_decl_remove_property(Item decl_item, Item prop_arg) {
    CssRule* guard_rule = unwrap_rule_decl(decl_item);
    if (!guard_rule || (guard_rule->type != CSS_RULE_STYLE &&
            guard_rule->type != CSS_RULE_NESTED_DECLARATIONS)) {
        return make_string_item("");
    }
    Item args[1] = {prop_arg};
    int argc = 1;
    (void)args; (void)argc;
    if (argc < 1) return make_string_item("");
    const char* prop = fn_to_cstr(args[0]);
    if (!prop) return make_string_item("");

    CssRule* rm_rule = unwrap_rule_decl(decl_item);
    if (!rm_rule || (rm_rule->type != CSS_RULE_STYLE && rm_rule->type != CSS_RULE_NESTED_DECLARATIONS)) return make_string_item("");

    // convert camelCase if needed
    char css_prop[128];
    cssom_camel_to_css_prop(prop, css_prop, sizeof(css_prop));
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);

    // find and remove
    for (size_t i = 0; i < rm_rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* d = rm_rule->data.style_rule.declarations[i];
        if (!d) continue;
        bool match = false;
        if (prop_id != CSS_PROPERTY_UNKNOWN && d->property_code == prop_id) match = true;
        else if (d->property_name && strcmp(d->property_name, css_prop) == 0) match = true;
        if (match) {
            const char* old_val = css_serialize_declaration_value(
                d, unwrap_rule_decl_pool(decl_item));
            // shift remaining declarations
            for (size_t j = i; j + 1 < rm_rule->data.style_rule.declaration_count; j++) {
                rm_rule->data.style_rule.declarations[j] = rm_rule->data.style_rule.declarations[j + 1];
            }
            rm_rule->data.style_rule.declaration_count--;
            js_cssom_notify_stylesheet_mutation();
            return make_string_item(old_val);
        }
    }
    return make_string_item("");
}

// open-name membership for rule declarations: `in` answers from the CSS
// property table without invoking a getter
extern "C" Item dom_cssom_decl_css_has(Item decl_item, Item prop_name) {
    (void)decl_item;
    const char* prop = fn_to_cstr(prop_name);
    if (!prop || !prop[0]) return (Item){.item = b2it(false)};
    char css_prop[128];
    cssom_camel_to_css_prop(prop, css_prop, sizeof(css_prop));
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);
    return (Item){.item = b2it(prop_id != CSS_PROPERTY_UNKNOWN && prop_id != 0)};
}


// =============================================================================
// document.styleSheets
// =============================================================================

extern "C" Item dom_cssom_get_document_stylesheets(void) {
    DomDocument* doc = (DomDocument*)dom_get_document();
    if (!doc || !doc->stylesheets || doc->stylesheet_count <= 0) {
        // return empty array
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        return (Item){.array = arr};
    }

    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    for (int i = 0; i < doc->stylesheet_count; i++) {
        array_push(arr, dom_cssom_wrap_stylesheet(doc->stylesheets[i]));
    }

    return (Item){.array = arr};
}

// =============================================================================
// HTMLStyleElement .sheet
// =============================================================================

static CssStylesheet* js_cssom_create_inline_stylesheet(DomElement* elem) {
    if (!elem || !elem->doc || !elem->doc->document_pool) return nullptr;

    Pool* pool = elem->doc->document_pool;
    StrBuf* css_text = strbuf_new_cap(32);
    if (!css_text) return nullptr;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!child->is_text()) continue;
        DomText* text = child->as_text();
        if (text && text->text && text->length > 0) {
            strbuf_append_str_n(css_text, text->text, text->length);
        }
    }

    // CSSOM exposes the sheet immediately after a style node is connected;
    // the post-script stylesheet rescan happens too late for libraries that
    // insert rules during the same script turn.
    CssEngine* engine = css_engine_create(pool);
    CssStylesheet* sheet = engine
        ? css_parse_stylesheet(engine, css_text->str ? css_text->str : "", "<inline-style>")
        : nullptr;
    if (engine) css_engine_destroy(engine);
    strbuf_free(css_text);
    if (!sheet) return nullptr;

    sheet->owner_style_element = elem;
    if (!lam::pool_grow_array(pool, &elem->doc->stylesheets,
                              &elem->doc->stylesheet_capacity,
                              elem->doc->stylesheet_count + 1, 4)) {
        return nullptr;
    }
    elem->doc->stylesheets[elem->doc->stylesheet_count++] = sheet;
    return sheet;
}

extern "C" Item dom_cssom_get_style_element_sheet(Item elem_item) {
    DomElement* elem = (DomElement*)dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    // must be a <style> element
    if (!elem->tag_name || strcasecmp(elem->tag_name, "style") != 0) {
        return ItemNull;
    }

    DomDocument* doc = elem->doc;
    if (!doc) return ItemNull;

    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* sheet = doc->stylesheets[i];
        if (sheet && sheet->owner_style_element == elem) {
            return dom_cssom_wrap_stylesheet(sheet);
        }
    }

    CssStylesheet* sheet = js_cssom_create_inline_stylesheet(elem);
    return sheet ? dom_cssom_wrap_stylesheet(sheet) : ItemNull;
}

// =============================================================================
// CSS Namespace Object (CSS.supports, CSS.escape)
// =============================================================================
JS_FORWARD_RETURN(bool, dom_is_css_namespace, (Item item), js_object_has_class, (item, JS_CLASS_CSS_NAMESPACE))

/**
 * CSS.supports(property, value) — two-argument form.
 * Returns true if the property is known and the value parses successfully.
 *
 * CSS.supports(conditionText) — single-argument form.
 * Parses "(property: value)" condition text.
 */
static Item js_css_supports(Item* args, int argc) {
    if (argc < 1) return (Item){.item = b2it(false)};

    Pool* pool = get_document_pool();
    if (!pool) pool = mem_pool_create(NULL, MEM_ROLE_CSS, "js.cssom");
    bool free_pool = (pool != get_document_pool());

    // ensure CSS property system is initialized so property lookups work
    css_property_system_init(pool);

    bool result = false;

    if (argc >= 2) {
        // two-argument form: CSS.supports(property, value)
        String* prop_s = it2s(args[0]);
        String* val_s = it2s(args[1]);
        if (!prop_s || !val_s) {
            if (free_pool) mem_pool_destroy(pool);
            return (Item){.item = b2it(false)};
        }

        // check if property is known (custom properties always pass). Keep the
        // complete JS string; CSS parsing is length-aware and must not truncate.
        size_t prop_len = prop_s->len;
        char* prop_buf = (char*)pool_alloc(pool, prop_len + 1);
        if (!prop_buf) {
            if (free_pool) mem_pool_destroy(pool);
            return (Item){.item = b2it(false)};
        }
        memcpy(prop_buf, prop_s->chars, prop_len);
        prop_buf[prop_len] = '\0';

        bool is_custom = (prop_len >= 2 && prop_buf[0] == '-' && prop_buf[1] == '-');
        if (!is_custom) {
            CssPropertyCode pid = css_property_code_from_name(prop_buf);
            if (pid == CSS_PROPERTY_UNKNOWN || pid == 0) {
                if (free_pool) mem_pool_destroy(pool);
                return (Item){.item = b2it(false)};
            }
        }

        CssDeclaration* decl = css_parse_property_declaration(
            prop_buf, prop_len, val_s->chars, val_s->len, pool);
        result = decl != NULL;
    } else {
        // single-argument form: CSS.supports("(property: value)")
        // or CSS.supports("property: value")
        String* cond_s = it2s(args[0]);
        if (!cond_s) {
            if (free_pool) mem_pool_destroy(pool);
            return (Item){.item = b2it(false)};
        }

        const char* text = cond_s->chars;
        size_t len = cond_s->len;

        // strip outer parens if present: "(property: value)" → "property: value"
        if (len >= 2 && text[0] == '(') {
            // find matching closing paren
            if (text[len - 1] == ')') {
                text++;
                len -= 2;
            }
        }

        // skip leading whitespace
        while (len > 0 && (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')) {
            text++;
            len--;
        }

        CssDeclaration* decl = css_parse_declaration_text(text, len, pool);
        result = css_declaration_is_supported(decl);
    }

    if (free_pool) mem_pool_destroy(pool);
    return (Item){.item = b2it(result)};
}
JS_FORWARD_ITEM(dom_css_supports_operation, (Item* args, int argc), js_css_supports, (args, argc))

extern "C" Item dom_css_escape_operation(Item* args, int argc) {
    // CSS.escape(ident) — serialize a CSS identifier. The intrinsic target
    // selects this operation before invocation; spelling is metadata only
    // under D6.2.2v2.
    if (argc < 1) return js_name_item("");
    String* ident = it2s(args[0]);
    if (!ident) return js_name_item("");

    // simple CSS serialization: escape special chars in ident
    // per CSSOM §2: https://drafts.csswg.org/cssom/#serialize-an-identifier
    char buf[1024];
    int out = 0;
    for (size_t i = 0; i < ident->len && out < (int)sizeof(buf) - 10; i++) {
        unsigned char ch = (unsigned char)ident->chars[i];
        if (i == 0 && ch >= '0' && ch <= '9') {
            // escape first digit: \3N
            out += snprintf(buf + out, sizeof(buf) - out, "\\%x ", ch);
        } else if (ch == 0) {
            buf[out++] = '\\';
            buf[out++] = 'f';
            buf[out++] = 'f';
            buf[out++] = 'f';
            buf[out++] = 'd';
            buf[out++] = ' ';
        } else if ((ch >= 0x01 && ch <= 0x1f) || ch == 0x7f) {
            out += snprintf(buf + out, sizeof(buf) - out, "\\%x ", ch);
        } else if (i == 0 && ch == '-' && ident->len == 1) {
            buf[out++] = '\\';
            buf[out++] = '-';
        } else if (ch == '-' || ch == '_' || (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch >= 0x80) {
            buf[out++] = (char)ch;
        } else {
            buf[out++] = '\\';
            buf[out++] = (char)ch;
        }
    }
    return js_name_item(buf, (size_t)out);
}

// CSS namespace object is managed in js_runtime.cpp (needs access to builtin enum)
