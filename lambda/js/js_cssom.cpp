/**
 * JavaScript CSSOM (CSS Object Model) Bridge Implementation
 *
 * Wraps CssStylesheet, CssRule, and CssDeclaration structures for JS access.
 * Uses the same sentinel-marker Map pattern as js_dom.cpp.
 */

#include "js_cssom.h"
#include "js_dom.h"
#include "js_runtime.h"
#include "js_class.h"
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

extern "C" void heap_register_gc_root(uint64_t* slot);
extern String* heap_create_name(const char* name, size_t len);

// Forward declaration
static Pool* get_document_pool();

// =============================================================================
// Unicode-Range Parsing & Canonical Serialization
// =============================================================================

static bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static uint32_t hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// format a code point as uppercase hex, stripping leading zeros
// e.g., 0x00ABC → "ABC", 0 → "0"
static int format_codepoint(uint32_t cp, char* buf, size_t buf_size) {
    if (cp == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    char tmp[8];
    int len = 0;
    uint32_t val = cp;
    while (val > 0 && len < 6) {
        int d = val & 0xF;
        tmp[len++] = (d < 10) ? ('0' + d) : ('A' + d - 10);
        val >>= 4;
    }
    // reverse
    for (int i = 0; i < len && (size_t)i < buf_size - 1; i++) {
        buf[i] = tmp[len - 1 - i];
    }
    buf[len] = '\0';
    return len;
}

/**
 * Parse a CSS unicode-range value and return canonical form.
 * Returns pool-allocated string like "U+ABC" or "U+A0-AF", or NULL if invalid.
 *
 * Grammar (CSS Syntax spec §9.1):
 *   <urange> = u '+' <ident-token> '?'*
 *            | u <dimension-token> '?'*
 *            | u <number-token> '?'*
 *            | u <number-token> <dimension-token>
 *            | u <number-token> <number-token>
 *            | u '+' '?'+
 *
 * In practice, the tokenizer sees these as various token sequences because
 * `u+abc` tokenizes as IDENT(u) + DELIM(+) + ... or as DIMENSION, etc.
 * We handle this by working directly on the raw text.
 */
static const char* css_parse_unicode_range_canonical(const char* input, Pool* pool) {
    if (!input || !pool) return nullptr;

    const char* p = input;
    // skip leading whitespace
    while (*p == ' ' || *p == '\t') p++;

    // must start with 'u' or 'U' (case insensitive)
    if (*p != 'u' && *p != 'U') return nullptr;
    p++;

    // skip CSS comments ONLY between 'u' and '+' (not whitespace — "u +abc" is invalid)
    while (*p == '/' && *(p+1) == '*') {
        p += 2;
        while (*p && !(*p == '*' && *(p+1) == '/')) p++;
        if (*p) p += 2;
    }

    // must have '+' next
    if (*p != '+') return nullptr;
    p++;

    // skip CSS comments ONLY (not whitespace) between '+' and value
    // per spec, spaces are not allowed: "u+ abc" is invalid
    while (*p == '/' && *(p+1) == '*') {
        p += 2;
        while (*p && !(*p == '*' && *(p+1) == '/')) p++;
        if (*p) p += 2;
    }

    // now parse the hex digits, '?' wildcards, and '-' range separator
    char hex_chars[8]; // max 6 hex + null
    int hex_count = 0;
    int wild_count = 0;

    // first, collect hex digits
    // collect up to 6 hex digits (CSS unicode-range max)
    while (is_hex_digit(*p) && hex_count < 6) {
        hex_chars[hex_count++] = *p;
        p++;
    }

    // skip CSS comments between hex chars and wildcards
    while (*p == '/' && *(p+1) == '*') {
        p += 2;
        while (*p && !(*p == '*' && *(p+1) == '/')) p++;
        if (*p) p += 2;
    }

    // then collect ? wildcards
    while (*p == '?') {
        wild_count++;
        p++;
    }

    // total must be 1-6
    int total = hex_count + wild_count;
    if (total == 0 || total > 6) return nullptr;

    // skip CSS comments after value
    while (*p == '/' && *(p+1) == '*') {
        p += 2;
        while (*p && !(*p == '*' && *(p+1) == '/')) p++;
        if (*p) p += 2;
    }

    // if we have wildcards, compute range and return
    if (wild_count > 0) {
        // no characters after '?' (except whitespace/end)
        const char* rest = p;
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest != '\0' && *rest != ';' && *rest != '}') {
            // wildcards can't be followed by anything — reject "-", hex, alpha
            return nullptr;
        }

        hex_chars[hex_count] = '\0';
        // compute start and end of wildcard range
        // "a?" → start=a0, end=af
        // "a??" → start=a00, end=aff
        uint32_t start_cp = 0;
        for (int i = 0; i < hex_count; i++) {
            start_cp = (start_cp << 4) | hex_val(hex_chars[i]);
        }
        for (int i = 0; i < wild_count; i++) {
            start_cp = start_cp << 4;
        }

        uint32_t end_cp = 0;
        for (int i = 0; i < hex_count; i++) {
            end_cp = (end_cp << 4) | hex_val(hex_chars[i]);
        }
        for (int i = 0; i < wild_count; i++) {
            end_cp = (end_cp << 4) | 0xF;
        }

        // validate range
        if (start_cp > 0x10FFFF || end_cp > 0x10FFFF) return nullptr;

        char result[32];
        char start_str[8], end_str[8];
        format_codepoint(start_cp, start_str, sizeof(start_str));
        format_codepoint(end_cp, end_str, sizeof(end_str));
        snprintf(result, sizeof(result), "U+%s-%s", start_str, end_str);
        char* out = (char*)pool_alloc(pool, strlen(result) + 1);
        strcpy(out, result);
        return out;
    }

    // no wildcards: either a single value or a range with '-'
    hex_chars[hex_count] = '\0';

    // parse the first value
    uint32_t start_cp = 0;
    for (int i = 0; i < hex_count; i++) {
        start_cp = (start_cp << 4) | hex_val(hex_chars[i]);
    }

    // check for range separator '-'
    bool has_range = false;
    uint32_t end_cp = 0;

    // skip CSS comments
    while (*p == '/' && *(p+1) == '*') {
        p += 2;
        while (*p && !(*p == '*' && *(p+1) == '/')) p++;
        if (*p) p += 2;
    }

    if (*p == '-') {
        p++;
        has_range = true;

        // skip CSS comments
        while (*p == '/' && *(p+1) == '*') {
            p += 2;
            while (*p && !(*p == '*' && *(p+1) == '/')) p++;
            if (*p) p += 2;
        }

        // parse end value hex digits
        int end_hex_count = 0;
        char end_hex[8];
        while (is_hex_digit(*p) && end_hex_count < 6) {
            end_hex[end_hex_count++] = *p;
            p++;
        }
        if (end_hex_count == 0 || end_hex_count > 6) return nullptr;
        end_hex[end_hex_count] = '\0';

        // reject if more hex chars follow (>6 total)
        if (is_hex_digit(*p)) return nullptr;

        // no wildcards allowed in range end
        if (*p == '?') return nullptr;

        for (int i = 0; i < end_hex_count; i++) {
            end_cp = (end_cp << 4) | hex_val(end_hex[i]);
        }
    }

    // validate
    if (start_cp > 0x10FFFF) return nullptr;
    if (has_range && end_cp > 0x10FFFF) return nullptr;

    // check for trailing garbage
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != ';' && *p != '}') {
        // any trailing content makes the value invalid
        return nullptr;
    }

    // format canonical output
    char result[32];
    char start_str[8];
    format_codepoint(start_cp, start_str, sizeof(start_str));

    if (has_range) {
        char end_str[8];
        format_codepoint(end_cp, end_str, sizeof(end_str));
        snprintf(result, sizeof(result), "U+%s-%s", start_str, end_str);
    } else {
        snprintf(result, sizeof(result), "U+%s", start_str);
    }

    char* out = (char*)pool_alloc(pool, strlen(result) + 1);
    strcpy(out, result);
    return out;
}

// =============================================================================
// Sentinel Markers for CSSOM Types
// =============================================================================

static TypeMap js_stylesheet_marker = {};    // CSSStyleSheet wrapper
static TypeMap js_css_rule_marker = {};      // CSSStyleRule wrapper
static TypeMap js_rule_decl_marker = {};     // CSSStyleDeclaration (rule) wrapper
TypeMap js_css_namespace_marker = {}; // CSS namespace object (CSS.supports, CSS.escape)

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
    wrapper->map_kind = MAP_KIND_CSSOM;
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
// Font-Face Declaration Parsing (lazy on .style access)
// =============================================================================

// Use the CssRule's legacy compatibility fields to cache parsed declarations
// for font-face rules. property_count stores declaration count, property_names
// is repurposed to store CssDeclaration** (cast).
static CssRule* get_font_face_as_style_rule(CssRule* rule) {
    if (!rule || (rule->type != CSS_RULE_FONT_FACE && rule->type != CSS_RULE_PAGE)) return nullptr;

    Pool* pool = rule->pool ? rule->pool : get_document_pool();
    if (!pool) return nullptr;

    // check if we already parsed declarations (cached in property_count)
    if (rule->property_count > 0 && rule->property_names) {
        // already parsed - return a shadow style rule using cached data
        // we store the shadow CssRule* in property_values[0]
        return (CssRule*)rule->property_values;
    }

    // parse the content of the font-face rule
    const char* content = rule->data.generic_rule.content;
    if (!content) return nullptr;

    // the content includes the braces, e.g.: { font-family: foo; src: url(...); }
    // find the content between { and }
    const char* start = strchr(content, '{');
    const char* end = nullptr;
    if (start) {
        start++;
        end = strrchr(content, '}');
        if (!end) end = content + strlen(content);
    } else {
        // no braces — treat entire content as declarations
        start = content;
        end = content + strlen(content);
    }

    size_t decl_text_len = end - start;
    char* decl_text = (char*)pool_alloc(pool, decl_text_len + 1);
    if (!decl_text) return nullptr;
    memcpy(decl_text, start, decl_text_len);
    decl_text[decl_text_len] = '\0';

    // tokenize and parse declarations
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(decl_text, decl_text_len, pool, &token_count);
    if (!tokens || token_count == 0) return nullptr;

    // parse declarations from tokens
    CssDeclaration* decls[64];
    size_t decl_count = 0;
    int pos = 0;
    while (pos < (int)token_count && decl_count < 64) {
        // skip whitespace and semicolons
        while (pos < (int)token_count &&
               (tokens[pos].type == CSS_TOKEN_WHITESPACE ||
                tokens[pos].type == CSS_TOKEN_SEMICOLON)) {
            pos++;
        }
        if (pos >= (int)token_count || tokens[pos].type == CSS_TOKEN_EOF) break;

        // skip @-rules inside declaration blocks (e.g., @at {} or @at;)
        if (tokens[pos].type == CSS_TOKEN_AT_KEYWORD) {
            pos++;
            int bd = 0;
            while (pos < (int)token_count && tokens[pos].type != CSS_TOKEN_EOF) {
                if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                    bd = 1; pos++;
                    while (pos < (int)token_count && bd > 0) {
                        if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) bd++;
                        else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) bd--;
                        pos++;
                    }
                    break;
                } else if (tokens[pos].type == CSS_TOKEN_SEMICOLON) {
                    pos++; break;
                }
                pos++;
            }
            continue;
        }

        // skip stray RIGHT_BRACE tokens (from @-rule block parsing)
        if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE ||
            tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
            pos++;
            continue;
        }

        CssDeclaration* decl = css_parse_declaration_from_tokens(tokens, &pos, (int)token_count, pool);
        if (decl) {
            decls[decl_count++] = decl;
        }
        // skip semicolons
        if (pos < (int)token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) pos++;
    }

    if (decl_count == 0) return nullptr;

    // create a shadow CssRule of type CSS_RULE_STYLE to hold the declarations
    CssRule* shadow = (CssRule*)pool_calloc(pool, sizeof(CssRule));
    if (!shadow) return nullptr;
    shadow->type = CSS_RULE_STYLE;
    shadow->pool = pool;
    shadow->data.style_rule.declarations = (CssDeclaration**)pool_calloc(pool, sizeof(CssDeclaration*) * (decl_count + 8));
    shadow->data.style_rule.declaration_count = decl_count;
    shadow->data.style_rule.selector = nullptr;
    shadow->data.style_rule.selector_group = nullptr;
    for (size_t i = 0; i < decl_count; i++) {
        shadow->data.style_rule.declarations[i] = decls[i];
    }

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

extern "C" Item js_cssom_wrap_rule(void* rule, void* pool) {
    if (!rule) return ItemNull;

    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_CSSOM;
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
    wrapper->map_kind = MAP_KIND_CSSOM;
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
    if (!decl || !pool) return "";

    // for custom properties (--foo) or pending-substitution values (containing
    // var()), prefer raw source text (preserves comments, blocks faithfully)
    // unless it contains a backslash (needs escape resolution via parsed value)
    bool is_custom = decl->property_id == CSS_PROPERTY_CUSTOM
        || (decl->property_name && decl->property_name[0] == '-' && decl->property_name[1] == '-');
    bool has_var = decl->value_text && strstr(decl->value_text, "var(");

    if ((is_custom || has_var) && decl->value_text && decl->value_text_len > 0
        && !memchr(decl->value_text, '\\', decl->value_text_len)) {
        return decl->value_text;
    }

    // use parsed value (normalizes standard properties, resolves escapes)
    if (decl->value) {
        CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
        if (fmt) {
            css_format_value(fmt, decl->value);
            String* result = stringbuf_to_string(fmt->output);
            if (result && result->chars && result->chars[0] != '\0') {
                return result->chars;
            }
        }
    }

    // last resort: raw source text (even with backslash, better than empty)
    if (decl->value_text && decl->value_text_len > 0) {
        return decl->value_text;
    }

    return "";
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
        // return an array of wrapped CSSRule objects (excluding @charset per CSSOM spec)
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        for (size_t i = 0; i < sheet->rule_count; i++) {
            if (sheet->rules[i] && sheet->rules[i]->type == CSS_RULE_CHARSET) continue;
            array_push(arr, js_cssom_wrap_rule(sheet->rules[i], pool));
        }
        return (Item){.array = arr};
    }

    if (strcmp(prop, "length") == 0) {
        // exclude @charset rules from length count
        size_t count = 0;
        for (size_t i = 0; i < sheet->rule_count; i++) {
            if (sheet->rules[i] && sheet->rules[i]->type == CSS_RULE_CHARSET) continue;
            count++;
        }
        return (Item){.item = i2it((int64_t)count)};
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

    if (strcmp(prop, "style") == 0) {
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

    // cssRules on CSSStyleRule — returns nested rules (CSS Nesting)
    if (strcmp(prop, "cssRules") == 0 || strcmp(prop, "rules") == 0) {
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
                    // Wrap as plain Map with __class_name__ and style property
                    // (not a js_css_rule_marker wrapper, so __class_name__ is readable
                    // by js_instanceof_classname via low-level map_get)
                    Item style_decl = wrap_rule_decl(nr[i], pool);
                    // Build a plain JS object: { __class_name__: "CSSNestedDeclarations", style: ... }
                    Item nd_obj = js_new_object();
                    Item class_key = make_string_item("__class_name__");
                    Item class_val = make_string_item("CSSNestedDeclarations");
                    js_property_set(nd_obj, class_key, class_val);
                    js_class_stamp(nd_obj, JS_CLASS_CSS_NESTED_DECLARATIONS);  // A3-T3b
                    Item style_key = make_string_item("style");
                    js_property_set(nd_obj, style_key, style_decl);
                    array_push(arr, nd_obj);
                } else {
                    array_push(arr, js_cssom_wrap_rule(nr[i], pool));
                }
            }
            return (Item){.array = arr};
        }
        return ItemNull;
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
        // Use String* to get actual length (handles embedded NULLs)
        String* val_string = it2s(value);
        const char* new_text = val_string ? val_string->chars : fn_to_cstr(value);
        size_t new_text_len = val_string ? val_string->len : (new_text ? strlen(new_text) : 0);
        if (!new_text || new_text_len == 0) return value;

        Pool* pool = (rule && rule->pool) ? rule->pool : get_document_pool();
        if (!pool) return value;

        // tokenize
        size_t token_count = 0;
        CssToken* tokens = css_tokenize(new_text, new_text_len, pool, &token_count);
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

        // reject if there are unconsumed tokens (e.g. non-printable code points)
        while (pos < (int)token_count && (tokens[pos].type == CSS_TOKEN_WHITESPACE ||
               tokens[pos].type == CSS_TOKEN_EOF || tokens[pos].type == CSS_TOKEN_COMMENT)) {
            pos++;
        }
        if (pos < (int)token_count) {
            log_debug("js_cssom_rule_set_property: leftover tokens in selectorText '%s'", new_text);
            return value;  // silently ignore - invalid selector
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

    // search declarations for this property — last matching wins (CSS cascade)
    CssPropertyId prop_id = css_property_id_from_name(css_prop);

    CssDeclaration* last_match = nullptr;
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
            last_match = decl;
        }
    }

    if (last_match) {
        const char* val = serialize_declaration_value(last_match, pool);
        log_debug("js_cssom_rule_decl_get_property: '%s' -> '%s'", prop, val);
        return make_string_item(val);
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
    if (!rule || (rule->type != CSS_RULE_STYLE && rule->type != CSS_RULE_NESTED_DECLARATIONS)) return value;

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

    // special handling for unicode-range descriptor (font-face)
    if (strcmp(css_prop, "unicode-range") == 0) {
        const char* canonical = css_parse_unicode_range_canonical(val_str, pool);
        if (!canonical) {
            // invalid unicode-range — silently ignore (per CSSOM spec)
            log_debug("js_cssom_rule_decl_set_property: invalid unicode-range '%s'", val_str);
            return value;
        }

        // create a declaration with the canonical value
        CssDeclaration* new_decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
        if (!new_decl) return value;
        new_decl->property_name = (char*)pool_alloc(pool, strlen(css_prop) + 1);
        if (new_decl->property_name) strcpy((char*)new_decl->property_name, css_prop);
        new_decl->value_text = canonical;
        new_decl->value_text_len = strlen(canonical);
        new_decl->valid = true;
        new_decl->property_id = css_property_id_from_name(css_prop);

        // find and replace or append
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* d = rule->data.style_rule.declarations[i];
            if (!d) continue;
            if (d->property_name && strcmp(d->property_name, css_prop) == 0) {
                rule->data.style_rule.declarations[i] = new_decl;
                log_debug("js_cssom_rule_decl_set_property: replaced unicode-range = '%s'", canonical);
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
        log_debug("js_cssom_rule_decl_set_property: added unicode-range = '%s'", canonical);
        return value;
    }

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
    if (!rule || (rule->type != CSS_RULE_STYLE && rule->type != CSS_RULE_NESTED_DECLARATIONS)) return ItemNull;

    const char* method = fn_to_cstr(method_name);
    if (!method) return ItemNull;

    if (strcmp(method, "getPropertyValue") == 0) {
        if (argc < 1) return make_string_item("");
        // delegate to property getter with CSS property name (not camelCase)
        return js_cssom_rule_decl_get_property(decl_item, args[0]);
    }

    if (strcmp(method, "setProperty") == 0) {
        if (argc < 2) return ItemNull;
        // delegate to the property setter with the CSS property name
        return js_cssom_rule_decl_set_property(decl_item, args[0], args[1]);
    }

    if (strcmp(method, "removeProperty") == 0) {
        if (argc < 1) return make_string_item("");
        const char* prop = fn_to_cstr(args[0]);
        if (!prop) return make_string_item("");

        CssRule* rm_rule = unwrap_rule_decl(decl_item);
        if (!rm_rule || (rm_rule->type != CSS_RULE_STYLE && rm_rule->type != CSS_RULE_NESTED_DECLARATIONS)) return make_string_item("");

        // convert camelCase if needed
        char css_prop[128];
        cssom_camel_to_css_prop(prop, css_prop, sizeof(css_prop));
        CssPropertyId prop_id = css_property_id_from_name(css_prop);

        // find and remove
        for (size_t i = 0; i < rm_rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* d = rm_rule->data.style_rule.declarations[i];
            if (!d) continue;
            bool match = false;
            if (prop_id != CSS_PROPERTY_UNKNOWN && d->property_id == prop_id) match = true;
            else if (d->property_name && strcmp(d->property_name, css_prop) == 0) match = true;
            if (match) {
                const char* old_val = serialize_declaration_value(d, unwrap_rule_decl_pool(decl_item));
                // shift remaining declarations
                for (size_t j = i; j + 1 < rm_rule->data.style_rule.declaration_count; j++) {
                    rm_rule->data.style_rule.declarations[j] = rm_rule->data.style_rule.declarations[j + 1];
                }
                rm_rule->data.style_rule.declaration_count--;
                return make_string_item(old_val);
            }
        }
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
    if (!doc) return ItemNull;

    // if no stylesheets array exists yet, create one for the empty <style> element
    if (!doc->stylesheets || doc->stylesheet_count <= 0) {
        Pool* pool = doc->pool;
        if (!pool) return ItemNull;

        CssStylesheet* sheet = (CssStylesheet*)pool_calloc(pool, sizeof(CssStylesheet));
        if (!sheet) return ItemNull;
        sheet->pool = pool;
        sheet->rule_capacity = 16;
        sheet->rules = (CssRule**)pool_calloc(pool, sheet->rule_capacity * sizeof(CssRule*));
        sheet->rule_count = 0;

        if (!doc->stylesheets) {
            doc->stylesheet_capacity = 4;
            doc->stylesheets = (CssStylesheet**)pool_calloc(pool, doc->stylesheet_capacity * sizeof(CssStylesheet*));
        }
        doc->stylesheets[doc->stylesheet_count++] = sheet;
        return js_cssom_wrap_stylesheet(sheet);
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
        // empty <style> element — create an empty stylesheet and add it to the document
        log_debug("js_cssom_get_style_element_sheet: creating empty sheet for <style> index %d", style_index);
        Pool* pool = doc->pool;
        if (!pool) return ItemNull;

        CssStylesheet* sheet = (CssStylesheet*)pool_calloc(pool, sizeof(CssStylesheet));
        if (!sheet) return ItemNull;
        sheet->pool = pool;
        sheet->rule_capacity = 16;
        sheet->rules = (CssRule**)pool_calloc(pool, sheet->rule_capacity * sizeof(CssRule*));
        sheet->rule_count = 0;

        // expand document stylesheet array if needed
        if (doc->stylesheet_count >= doc->stylesheet_capacity) {
            int new_cap = doc->stylesheet_capacity > 0 ? doc->stylesheet_capacity * 2 : 8;
            CssStylesheet** new_arr = (CssStylesheet**)pool_calloc(pool, new_cap * sizeof(CssStylesheet*));
            if (!new_arr) return ItemNull;
            if (doc->stylesheets && doc->stylesheet_count > 0) {
                memcpy(new_arr, doc->stylesheets, doc->stylesheet_count * sizeof(CssStylesheet*));
            }
            doc->stylesheets = new_arr;
            doc->stylesheet_capacity = new_cap;
        }
        doc->stylesheets[doc->stylesheet_count++] = sheet;
        return js_cssom_wrap_stylesheet(sheet);
    }

    return js_cssom_wrap_stylesheet(doc->stylesheets[sheet_idx]);
}

// =============================================================================
// CSS Namespace Object (CSS.supports, CSS.escape)
// =============================================================================

extern "C" bool js_is_css_namespace(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_css_namespace_marker;
}

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
    if (!pool) pool = pool_create();
    bool free_pool = (pool != get_document_pool());

    // ensure CSS property system is initialized so property lookups work
    css_property_system_init(pool);

    bool result = false;

    if (argc >= 2) {
        // two-argument form: CSS.supports(property, value)
        String* prop_s = it2s(args[0]);
        String* val_s = it2s(args[1]);
        if (!prop_s || !val_s) {
            if (free_pool) pool_destroy(pool);
            return (Item){.item = b2it(false)};
        }

        // check if property is known (custom properties always pass)
        char prop_buf[256];
        size_t prop_len = prop_s->len < 255 ? prop_s->len : 255;
        memcpy(prop_buf, prop_s->chars, prop_len);
        prop_buf[prop_len] = '\0';

        bool is_custom = (prop_len >= 2 && prop_buf[0] == '-' && prop_buf[1] == '-');
        if (!is_custom) {
            CssPropertyId pid = css_property_id_from_name(prop_buf);
            if (pid == CSS_PROPERTY_UNKNOWN || pid == 0) {
                if (free_pool) pool_destroy(pool);
                return (Item){.item = b2it(false)};
            }
        }

        // try parsing "property: value" as a CSS declaration
        char decl_text[1024];
        int n = snprintf(decl_text, sizeof(decl_text), "%.*s: %.*s",
                         (int)prop_len, prop_buf,
                         (int)(val_s->len < 700 ? val_s->len : 700), val_s->chars);
        if (n <= 0 || n >= (int)sizeof(decl_text)) {
            if (free_pool) pool_destroy(pool);
            return (Item){.item = b2it(false)};
        }

        size_t token_count = 0;
        CssToken* tokens = css_tokenize(decl_text, strlen(decl_text), pool, &token_count);
        if (tokens && token_count > 0) {
            int pos = 0;
            CssDeclaration* decl = css_parse_declaration_from_tokens(tokens, &pos, (int)token_count, pool);
            result = (decl != NULL);
        }
    } else {
        // single-argument form: CSS.supports("(property: value)")
        // or CSS.supports("property: value")
        String* cond_s = it2s(args[0]);
        if (!cond_s) {
            if (free_pool) pool_destroy(pool);
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

        // try to parse as a declaration
        char decl_buf[1024];
        size_t copy_len = len < sizeof(decl_buf) - 1 ? len : sizeof(decl_buf) - 1;
        memcpy(decl_buf, text, copy_len);
        decl_buf[copy_len] = '\0';

        size_t token_count = 0;
        CssToken* tokens = css_tokenize(decl_buf, copy_len, pool, &token_count);
        if (tokens && token_count > 0) {
            int pos = 0;
            CssDeclaration* decl = css_parse_declaration_from_tokens(tokens, &pos, (int)token_count, pool);
            result = (decl != NULL);
        }
    }

    if (free_pool) pool_destroy(pool);
    return (Item){.item = b2it(result)};
}

extern "C" Item js_css_namespace_method(Item obj, Item method_name, Item* args, int argc) {
    (void)obj;
    String* name = it2s(method_name);
    if (!name) return ItemNull;

    if (name->len == 8 && strncmp(name->chars, "supports", 8) == 0) {
        return js_css_supports(args, argc);
    }

    if (name->len == 6 && strncmp(name->chars, "escape", 6) == 0) {
        // CSS.escape(ident) — serialize a CSS identifier
        if (argc < 1) return (Item){.item = s2it(heap_create_name(""))};
        String* ident = it2s(args[0]);
        if (!ident) return (Item){.item = s2it(heap_create_name(""))};

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
        return (Item){.item = s2it(heap_create_name(buf, (size_t)out))};
    }

    log_debug("js_css_namespace_method: unknown method '%.*s'", (int)name->len, name->chars);
    return ItemNull;
}

// CSS namespace object is managed in js_runtime.cpp (needs access to builtin enum)
