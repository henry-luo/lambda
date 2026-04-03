/**
 * JavaScript CSSOM (CSS Object Model) Bridge Implementation
 *
 * Wraps CssStylesheet, CssRule, and CssDeclaration structures for JS access.
 * Uses the same sentinel-marker Map pattern as js_dom.cpp.
 */

#include "js_cssom.h"
#include "js_dom.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_style.hpp"
#include "../input/css/css_formatter.hpp"

#include <cstring>
#include <cctype>

// Forward declaration
static Pool* get_document_pool();

// =============================================================================
// Sentinel Markers for CSSOM Types
// =============================================================================

static TypeMap js_stylesheet_marker = {};    // CSSStyleSheet wrapper
static TypeMap js_css_rule_marker = {};      // CSSStyleRule wrapper
static TypeMap js_rule_decl_marker = {};     // CSSStyleDeclaration (rule) wrapper

// =============================================================================
// Type Checking
// =============================================================================

extern "C" bool js_is_stylesheet(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_stylesheet_marker;
}

extern "C" bool js_is_css_rule(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_css_rule_marker;
}

extern "C" bool js_is_rule_style_decl(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_rule_decl_marker;
}

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

static Item make_string_item(const char* str) {
    return (Item){.item = s2it(heap_create_name(str ? str : ""))};
}

// =============================================================================
// CSSStyleSheet Wrapper
// =============================================================================

extern "C" Item js_cssom_wrap_stylesheet(void* stylesheet) {
    if (!stylesheet) return ItemNull;

    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->type = (void*)&js_stylesheet_marker;
    wrapper->data = stylesheet;  // CssStylesheet*
    wrapper->data_cap = 0;

    log_debug("js_cssom_wrap_stylesheet: wrapped CssStylesheet=%p as Map=%p", stylesheet, (void*)wrapper);
    return (Item){.map = wrapper};
}

static CssStylesheet* unwrap_stylesheet(Item item) {
    if (!js_is_stylesheet(item)) return nullptr;
    return (CssStylesheet*)item.map->data;
}

// =============================================================================
// CSSRule Wrapper
// =============================================================================

extern "C" Item js_cssom_wrap_rule(void* rule, void* pool) {
    if (!rule) return ItemNull;

    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->type = (void*)&js_css_rule_marker;
    wrapper->data = rule;               // CssRule*
    wrapper->data_cap = 0;

    log_debug("js_cssom_wrap_rule: wrapped CssRule=%p as Map=%p", rule, (void*)wrapper);
    return (Item){.map = wrapper};
}

static CssRule* unwrap_rule(Item item) {
    if (!js_is_css_rule(item)) return nullptr;
    return (CssRule*)item.map->data;
}

static Pool* unwrap_rule_pool(Item item) {
    CssRule* rule = unwrap_rule(item);
    if (!rule) return nullptr;
    return rule->pool ? rule->pool : get_document_pool();
}

// =============================================================================
// CSSStyleDeclaration (rule declarations) Wrapper
// =============================================================================

static Item wrap_rule_decl(CssRule* rule, Pool* pool) {
    if (!rule) return ItemNull;

    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->type = (void*)&js_rule_decl_marker;
    wrapper->data = rule;               // CssRule* (we access declarations from it)
    wrapper->data_cap = 0;

    return (Item){.map = wrapper};
}

static CssRule* unwrap_rule_decl(Item item) {
    if (!js_is_rule_style_decl(item)) return nullptr;
    return (CssRule*)item.map->data;
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
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    return doc ? doc->pool : nullptr;
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

// =============================================================================
// Helper: Serialize a declaration value to text
// =============================================================================

static const char* serialize_declaration_value(CssDeclaration* decl, Pool* pool) {
    if (!decl || !decl->value || !pool) return "";

    CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!fmt) return "";

    css_format_value(fmt, decl->value);
    String* result = stringbuf_to_string(fmt->output);
    return (result && result->chars) ? result->chars : "";
}

// =============================================================================
// CSSStyleSheet Property Access
// =============================================================================

extern "C" Item js_cssom_stylesheet_get_property(Item sheet_item, Item prop_name) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    log_debug("js_cssom_stylesheet_get_property: '%s'", prop);

    Pool* pool = get_document_pool();

    if (strcmp(prop, "cssRules") == 0 || strcmp(prop, "rules") == 0) {
        // return an array of wrapped CSSRule objects
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        for (size_t i = 0; i < sheet->rule_count; i++) {
            array_push(arr, js_cssom_wrap_rule(sheet->rules[i], pool));
        }
        return (Item){.array = arr};
    }

    if (strcmp(prop, "length") == 0) {
        return (Item){.item = i2it((int64_t)sheet->rule_count)};
    }

    if (strcmp(prop, "disabled") == 0) {
        return sheet->disabled ? (Item){.item = ITEM_TRUE} : (Item){.item = ITEM_FALSE};
    }

    if (strcmp(prop, "type") == 0) {
        return make_string_item("text/css");
    }

    if (strcmp(prop, "href") == 0) {
        return make_string_item(sheet->href);
    }

    if (strcmp(prop, "title") == 0) {
        return make_string_item(sheet->title);
    }

    // numeric index access: sheet[0], sheet[1], etc.
    // (when transpiler resolves bracket access as property)
    {
        char* endp;
        long idx = strtol(prop, &endp, 10);
        if (*endp == '\0' && idx >= 0 && (size_t)idx < sheet->rule_count) {
            return js_cssom_wrap_rule(sheet->rules[idx], pool);
        }
    }

    log_debug("js_cssom_stylesheet_get_property: unknown property '%s'", prop);
    return ItemNull;
}

// =============================================================================
// CSSStyleSheet Method Dispatch
// =============================================================================

extern "C" Item js_cssom_stylesheet_method(Item sheet_item, Item method_name, Item* args, int argc) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;

    const char* method = fn_to_cstr(method_name);
    if (!method) return ItemNull;

    log_debug("js_cssom_stylesheet_method: '%s' argc=%d", method, argc);

    if (strcmp(method, "insertRule") == 0) {
        if (argc < 1) return ItemNull;

        const char* rule_text = fn_to_cstr(args[0]);
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

        size_t token_count = 0;
        CssToken* tokens = css_tokenize(rule_text, strlen(rule_text), pool, &token_count);
        if (!tokens || token_count == 0) {
            log_error("js_cssom_stylesheet_method insertRule: failed to tokenize rule '%s'", rule_text);
            return ItemNull;
        }

        CssRule* new_rule = css_parse_rule_from_tokens(tokens, (int)token_count, pool);
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
        return (Item){.item = i2it((int64_t)index)};
    }

    if (strcmp(method, "deleteRule") == 0) {
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
        return ItemNull;
    }

    log_debug("js_cssom_stylesheet_method: unknown method '%s'", method);
    return ItemNull;
}

// =============================================================================
// CSSStyleRule Property Access
// =============================================================================

extern "C" Item js_cssom_rule_get_property(Item rule_item, Item prop_name) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    CssRule* rule_for_pool = unwrap_rule(rule_item);
    Pool* pool = (rule_for_pool && rule_for_pool->pool) ? rule_for_pool->pool : get_document_pool();

    log_debug("js_cssom_rule_get_property: '%s' (rule type=%d)", prop, rule->type);

    if (strcmp(prop, "selectorText") == 0) {
        if (rule->type != CSS_RULE_STYLE) return make_string_item("");
        const char* sel_text = serialize_selector_text(rule, pool);
        return make_string_item(sel_text);
    }

    if (strcmp(prop, "style") == 0) {
        if (rule->type != CSS_RULE_STYLE) return ItemNull;
        return wrap_rule_decl(rule, pool);
    }

    if (strcmp(prop, "cssText") == 0) {
        if (!pool) return make_string_item("");
        CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
        if (!fmt) return make_string_item("");
        const char* text = css_format_rule(fmt, rule);
        return make_string_item(text);
    }

    if (strcmp(prop, "type") == 0) {
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

    if (strcmp(prop, "parentRule") == 0) {
        if (rule->parent) {
            return js_cssom_wrap_rule(rule->parent, pool);
        }
        return ItemNull;
    }

    log_debug("js_cssom_rule_get_property: unknown property '%s'", prop);
    return ItemNull;
}

// =============================================================================
// CSSStyleRule Property Set (selectorText)
// =============================================================================

extern "C" Item js_cssom_rule_set_property(Item rule_item, Item prop_name, Item value) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    if (strcmp(prop, "selectorText") == 0 && rule->type == CSS_RULE_STYLE) {
        const char* new_text = fn_to_cstr(value);
        if (!new_text) return value;

        Pool* pool = (rule && rule->pool) ? rule->pool : get_document_pool();
        if (!pool) return value;

        // tokenize
        size_t token_count = 0;
        CssToken* tokens = css_tokenize(new_text, strlen(new_text), pool, &token_count);
        if (!tokens || token_count == 0) {
            log_debug("js_cssom_rule_set_property: failed to tokenize selectorText '%s'", new_text);
            return value;  // per spec, silently ignore parse failures
        }

        // parse selector group
        int pos = 0;
        CssSelectorGroup* new_group = css_parse_selector_group_from_tokens(tokens, &pos, (int)token_count, pool);
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

    log_debug("js_cssom_rule_set_property: unknown/unsupported property '%s'", prop);
    return value;
}

// =============================================================================
// CSSStyleDeclaration (rule) Property Access
// =============================================================================

extern "C" Item js_cssom_rule_decl_get_property(Item decl_item, Item prop_name) {
    CssRule* rule = unwrap_rule_decl(decl_item);
    if (!rule || rule->type != CSS_RULE_STYLE) return make_string_item("");

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
        CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
        if (!fmt) return make_string_item("");
        StringBuf* buf = stringbuf_new(pool);
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* d = rule->data.style_rule.declarations[i];
            if (!d) continue;
            if (i > 0) stringbuf_append_str(buf, " ");
            const char* name = d->property_name ? d->property_name : css_property_get_name(d->property_id);
            if (name) {
                stringbuf_append_str(buf, name);
                stringbuf_append_str(buf, ": ");
                // serialize value
                stringbuf_reset(fmt->output);
                css_format_value(fmt, d->value);
                String* val_str = stringbuf_to_string(fmt->output);
                if (val_str && val_str->chars) {
                    stringbuf_append_str(buf, val_str->chars);
                }
                if (d->important) {
                    stringbuf_append_str(buf, " !important");
                }
                stringbuf_append_str(buf, ";");
            }
        }
        String* result = stringbuf_to_string(buf);
        return make_string_item(result && result->chars ? result->chars : "");
    }

    // convert camelCase to CSS property
    char css_prop[128];
    cssom_camel_to_css_prop(prop, css_prop, sizeof(css_prop));

    // search declarations for this property
    CssPropertyId prop_id = css_property_id_from_name(css_prop);

    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* decl = rule->data.style_rule.declarations[i];
        if (!decl) continue;

        // match by property ID or by name (for custom properties)
        bool match = false;
        if (prop_id != CSS_PROPERTY_UNKNOWN && decl->property_id == prop_id) {
            match = true;
        } else if (decl->property_name && strcmp(decl->property_name, css_prop) == 0) {
            match = true;
        }

        if (match) {
            const char* val = serialize_declaration_value(decl, pool);
            log_debug("js_cssom_rule_decl_get_property: '%s' -> '%s'", prop, val);
            return make_string_item(val);
        }
    }

    // not found — return empty string (per CSSOM spec)
    log_debug("js_cssom_rule_decl_get_property: '%s' not found in rule", prop);
    return make_string_item("");
}

// =============================================================================
// CSSStyleDeclaration (rule) Property Set
// =============================================================================

extern "C" Item js_cssom_rule_decl_set_property(Item decl_item, Item prop_name, Item value) {
    CssRule* rule = unwrap_rule_decl(decl_item);
    if (!rule || rule->type != CSS_RULE_STYLE) return value;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return value;

    const char* val_str = fn_to_cstr(value);
    if (!val_str) val_str = "";

    Pool* pool = rule->pool;
    if (!pool) pool = get_document_pool();
    if (!pool) return value;

    // convert camelCase to CSS property
    char css_prop[128];
    cssom_camel_to_css_prop(prop, css_prop, sizeof(css_prop));

    // parse the value as a CSS declaration: "property: value"
    char decl_text[512];
    snprintf(decl_text, sizeof(decl_text), "%s: %s", css_prop, val_str);

    size_t token_count = 0;
    CssToken* tokens = css_tokenize(decl_text, strlen(decl_text), pool, &token_count);
    if (!tokens || token_count == 0) return value;

    int pos = 0;
    CssDeclaration* new_decl = css_parse_declaration_from_tokens(tokens, &pos, (int)token_count, pool);
    if (!new_decl) {
        // parse error — silently ignore (per CSSOM spec)
        log_debug("js_cssom_rule_decl_set_property: parse error for '%s: %s'", css_prop, val_str);
        return value;
    }

    // find and replace existing declaration with same property
    CssPropertyId prop_id = css_property_id_from_name(css_prop);
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* d = rule->data.style_rule.declarations[i];
        if (!d) continue;
        bool match = false;
        if (prop_id != CSS_PROPERTY_UNKNOWN && d->property_id == prop_id) match = true;
        else if (d->property_name && strcmp(d->property_name, css_prop) == 0) match = true;

        if (match) {
            rule->data.style_rule.declarations[i] = new_decl;
            log_debug("js_cssom_rule_decl_set_property: replaced '%s' = '%s'", css_prop, val_str);
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
    log_debug("js_cssom_rule_decl_set_property: added '%s' = '%s'", css_prop, val_str);
    return value;
}

// =============================================================================
// CSSStyleDeclaration (rule) Method Dispatch
// =============================================================================

extern "C" Item js_cssom_rule_decl_method(Item decl_item, Item method_name, Item* args, int argc) {
    CssRule* rule = unwrap_rule_decl(decl_item);
    if (!rule || rule->type != CSS_RULE_STYLE) return ItemNull;

    const char* method = fn_to_cstr(method_name);
    if (!method) return ItemNull;

    if (strcmp(method, "getPropertyValue") == 0) {
        if (argc < 1) return make_string_item("");
        // delegate to property getter with CSS property name (not camelCase)
        return js_cssom_rule_decl_get_property(decl_item, args[0]);
    }

    if (strcmp(method, "setProperty") == 0) {
        if (argc < 2) return ItemNull;
        // TODO: implement setProperty for dynamic rule modification
        log_debug("js_cssom_rule_decl_method: setProperty not yet implemented");
        return ItemNull;
    }

    if (strcmp(method, "removeProperty") == 0) {
        if (argc < 1) return make_string_item("");
        // TODO: implement removeProperty
        log_debug("js_cssom_rule_decl_method: removeProperty not yet implemented");
        return make_string_item("");
    }

    log_debug("js_cssom_rule_decl_method: unknown method '%s'", method);
    return ItemNull;
}

// =============================================================================
// document.styleSheets
// =============================================================================

extern "C" Item js_cssom_get_document_stylesheets(void) {
    DomDocument* doc = (DomDocument*)js_dom_get_document();
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
        array_push(arr, js_cssom_wrap_stylesheet(doc->stylesheets[i]));
    }

    return (Item){.array = arr};
}

// =============================================================================
// HTMLStyleElement .sheet
// =============================================================================

extern "C" Item js_cssom_get_style_element_sheet(Item elem_item) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    // must be a <style> element
    if (!elem->tag_name || strcasecmp(elem->tag_name, "style") != 0) {
        return ItemNull;
    }

    DomDocument* doc = elem->doc;
    if (!doc || !doc->stylesheets || doc->stylesheet_count <= 0) {
        return ItemNull;
    }

    // We need to find the stylesheet associated with this <style> element.
    // The stylesheets are collected from <style> elements in document order,
    // so we count which <style> element this is (0-based index among <style>s)
    // and map it to the corresponding stylesheet.
    //
    // Note: linked stylesheets (<link rel="stylesheet">) may also be in the array,
    // occupying earlier indices. We need to account for those.

    // Count linked stylesheets (those with href set)
    int linked_count = 0;
    for (int i = 0; i < doc->stylesheet_count; i++) {
        if (doc->stylesheets[i] && doc->stylesheets[i]->href) {
            linked_count++;
        }
    }

    // Find this <style> element's index among all <style> elements in doc order.
    // We do a simple tree walk to count.
    int style_index = -1;
    int current_style_idx = 0;

    // helper: recursively walk DOM tree counting <style> elements
    struct StyleCounter {
        static bool find_style(DomNode* node, DomElement* target, int* current, int* found) {
            if (!node) return false;
            if (node->is_element()) {
                DomElement* el = node->as_element();
                if (el->tag_name && strcasecmp(el->tag_name, "style") == 0) {
                    if (el == target) {
                        *found = *current;
                        return true;
                    }
                    (*current)++;
                }
                // recurse into children via linked list
                DomNode* child = el->first_child;
                while (child) {
                    if (find_style(child, target, current, found)) return true;
                    child = child->next_sibling;
                }
            }
            return false;
        }
    };

    if (doc->root) {
        StyleCounter::find_style(doc->root, elem, &current_style_idx, &style_index);
    }

    if (style_index < 0) {
        log_debug("js_cssom_get_style_element_sheet: <style> element not found in tree");
        return ItemNull;
    }

    // The stylesheet array has linked stylesheets first, then inline styles.
    // Map style_index to the actual array index.
    int sheet_idx = linked_count + style_index;
    if (sheet_idx >= doc->stylesheet_count) {
        log_debug("js_cssom_get_style_element_sheet: sheet_idx %d >= count %d", sheet_idx, doc->stylesheet_count);
        return ItemNull;
    }

    return js_cssom_wrap_stylesheet(doc->stylesheets[sheet_idx]);
}
