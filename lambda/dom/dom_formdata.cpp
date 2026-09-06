/**
 * js_formdata.cpp — JS bindings for FormData (XHR/Fetch FormData API)
 *
 * Implements the FormData constructor and all its methods:
 *   append, delete, get, getAll, has, set, entries, keys, values, forEach
 *   Symbol.iterator (= entries)
 *
 * Internal layout of a FormData JS object:
 *   {
 *     _fd_entries: [[name, value], [name2, value2], ...],  // live entry list
 *     append: fn, delete: fn, get: fn, ...                 // IDL methods
 *   }
 *
 * Iterators hold a direct reference to the _fd_entries array so mutations
 * during iteration are visible (live-view iteration per spec).
 *
 * Install entry point: js_formdata_install_globals()
 * Called from: dom.cpp: dom_set_document()
 */

#include "dom.h"
#include "realm/dom_realm.h"
#include "../js/js_runtime.h"
#include "../js/js_class.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../module/radiant/radiant_input_value.hpp"
#include "../../radiant/view.hpp"
#include "../../radiant/event.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include <cstring>
#include <cctype>
#include <cinttypes>
#include <ctime>

// ============================================================================
// Forward declarations of engine APIs used here
// ============================================================================


// Helpers from dom.cpp used for form control inspection
extern "C" bool dom_get_checkedness(void* dom_elem);
extern "C" const char* dom_input_type_lower(void* dom_elem);
extern "C" bool dom_is_disabled(void* dom_elem);
extern "C" DomElement* dom_find_form_owner(void* control_ptr);

// ============================================================================
// Helpers
// ============================================================================

#define make_bool(v) ((Item){.item = b2it((v) ? 1 : 0)})
#define make_int_item(v) ((Item){.item = i2it(v)})
#define make_str make_string_item
#define make_key make_string_item

static inline Item prop_get(Item obj, const char* key) {
    return dom_realm_get(obj, make_key(key));
}

static inline void prop_set(Item obj, const char* key, Item val) {
    dom_realm_set(obj, make_key(key), val);
}

static bool fd_input_supports_dirname(const char* itype) {
    if (!itype) return false;
    return strcmp(itype, "hidden") == 0 ||
           strcmp(itype, "text") == 0 ||
           strcmp(itype, "search") == 0 ||
           strcmp(itype, "tel") == 0 ||
           strcmp(itype, "url") == 0 ||
           strcmp(itype, "email") == 0 ||
           strcmp(itype, "password") == 0 ||
           strcmp(itype, "submit") == 0;
}

static bool fd_utf8_decode_one(const char* s, uint32_t* out_cp, int* out_len) {
    if (!s || !*s) return false;
    unsigned char b0 = (unsigned char)s[0];
    if (b0 < 0x80) {
        *out_cp = b0;
        *out_len = 1;
        return true;
    }
    if ((b0 & 0xE0) == 0xC0 &&
        (s[1] && (((unsigned char)s[1] & 0xC0) == 0x80))) {
        *out_cp = ((uint32_t)(b0 & 0x1F) << 6) |
                  (uint32_t)((unsigned char)s[1] & 0x3F);
        *out_len = 2;
        return true;
    }
    if ((b0 & 0xF0) == 0xE0 &&
        (s[1] && (((unsigned char)s[1] & 0xC0) == 0x80)) &&
        (s[2] && (((unsigned char)s[2] & 0xC0) == 0x80))) {
        *out_cp = ((uint32_t)(b0 & 0x0F) << 12) |
                  ((uint32_t)((unsigned char)s[1] & 0x3F) << 6) |
                  (uint32_t)((unsigned char)s[2] & 0x3F);
        *out_len = 3;
        return true;
    }
    if ((b0 & 0xF8) == 0xF0 &&
        (s[1] && (((unsigned char)s[1] & 0xC0) == 0x80)) &&
        (s[2] && (((unsigned char)s[2] & 0xC0) == 0x80)) &&
        (s[3] && (((unsigned char)s[3] & 0xC0) == 0x80))) {
        *out_cp = ((uint32_t)(b0 & 0x07) << 18) |
                  ((uint32_t)((unsigned char)s[1] & 0x3F) << 12) |
                  ((uint32_t)((unsigned char)s[2] & 0x3F) << 6) |
                  (uint32_t)((unsigned char)s[3] & 0x3F);
        *out_len = 4;
        return true;
    }
    return false;
}
JS_FORWARD_STATIC_EXPRESSION(bool, fd_codepoint_is_rtl, (uint32_t cp), ((cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)))

static const char* fd_direction_from_auto_value(const char* value) {
    if (!value) return "ltr";
    for (const char* p = value; *p; ) {
        uint32_t cp = 0;
        int cp_len = 0;
        if (!fd_utf8_decode_one(p, &cp, &cp_len)) {
            p++;
            continue;
        }
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return "ltr";
        if (fd_codepoint_is_rtl(cp)) return "rtl";
        p += cp_len;
    }
    return "ltr";
}

static const char* fd_compute_dirname_direction(DomElement* elem, const char* value_hint) {
    for (DomNode* cur = (DomNode*)elem; cur; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* cur_elem = (DomElement*)cur;
        const char* dir = cur_elem->get_attribute("dir");
        if (!dir || !*dir) continue;
        if (strcasecmp(dir, "rtl") == 0) return "rtl";
        if (strcasecmp(dir, "ltr") == 0) return "ltr";
        if (strcasecmp(dir, "auto") == 0) return fd_direction_from_auto_value(value_hint);
    }
    return "ltr";
}

static inline Item make_sym_iterator_key() {
    return js_well_known_symbol_key(1);
}

// Internal entries array key
static const char* FD_ENTRIES_KEY = "_fd_entries";

// Forward decls
static Item fd_blob_to_file(Item value, Item filename_item);
JS_FORWARD_STATIC_ITEM(fd_get_entries, (Item this_fd), prop_get, (this_fd, FD_ENTRIES_KEY))

static void fd_entries_remove_at(Item entries, int64_t index) {
    if (get_type_id(entries) != LMD_TYPE_ARRAY || !entries.array ||
        index < 0 || index >= entries.array->length) {
        return;
    }
    // D6.2.2v2: _fd_entries is private FormData list storage. Calling the
    // mutable Array.prototype.splice binding would expose this internal step
    // to prototype replacement instead of applying the FormData list algorithm.
    Array* array = entries.array;
    for (int64_t i = index; i + 1 < array->length; i++) {
        array->items[i] = array->items[i + 1];
    }
    array->items[array->length - 1] = ItemNull;
    array->length--;
}

// Coerce a FormData value to string (or pass through Blob/File objects).
// Per XHR spec: all non-Blob values are stringified using the same rules
// as the HTML serializer (essentially JavaScript toString).
static Item fd_coerce_value(Item v) {
    TypeId t = get_type_id(v);
    if (t == LMD_TYPE_STRING)    return v;
    if (t == LMD_TYPE_NULL)        return make_str("null");
    if (t == LMD_TYPE_UNDEFINED)   return make_str("undefined");
    if (t == LMD_TYPE_MAP)         return v;  // Blob/File: pass through as-is
    if (t == LMD_TYPE_BOOL)   return (v.item & 0xFF) ? make_str("true") : make_str("false");
    if (t == LMD_TYPE_INT) {
        int64_t iv = it2i(v);
        // symbols are encoded as large negative ints — throw TypeError
        if (iv <= -(int64_t)(1LL << 40)) {
            return dom_realm_throw_type_error("Cannot convert a Symbol value to a string");
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%" PRId64, iv);
        return make_str(buf);
    }
    if (t == LMD_TYPE_FLOAT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%g", it2d(v));
        return make_str(buf);
    }
    const char* cs = fn_to_cstr(v);
    return make_str(cs ? cs : "");
}

// ============================================================================
// FormData methods
// ============================================================================

// Check if an Item is a Blob/File
static bool fd_is_blob(Item v) {
    JsClass cls = js_class_id(v);
    return cls == JS_CLASS_BLOB || cls == JS_CLASS_FILE;
}

static Item fd_prepare_value(Item value_item, Item filename_item) {
    JS_ASSIGN_OR_RETURN(value, fd_coerce_value(value_item));
    // Blob values become Files when a filename is supplied, or use "blob" by default.
    if (fd_is_blob(value)) {
        bool is_file = (js_class_id(value) == JS_CLASS_FILE);
        bool has_filename = (get_type_id(filename_item) != LMD_TYPE_UNDEFINED);
        if (has_filename || !is_file) {
            value = fd_blob_to_file(value, filename_item);
        }
    }
    return value;
}

static Item js_fd_append(Item name_item, Item value_item, Item filename_item) {
    Item this_fd = dom_realm_receiver();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_js_undefined();

    // 3-arg form append(name, blobValue, filename) is only valid when value is a Blob/File.
    // If 3rd arg is present (not undefined) but value is not a Blob/File → TypeError.
    if (get_type_id(filename_item) != LMD_TYPE_UNDEFINED && !fd_is_blob(value_item)) {
        return dom_realm_throw_type_error("append: 3-argument form requires a Blob value");
    }

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    JS_ASSIGN_OR_RETURN(value, fd_prepare_value(value_item, filename_item));

    Item pair = js_array_new(0);
    js_array_push(pair, make_str(name_cs));
    js_array_push(pair, value);
    js_array_push(entries, pair);
    log_debug("fd_append: appended '%s'", name_cs);
    return make_js_undefined();
}

static Item js_fd_delete(Item name_item) {
    Item this_fd = dom_realm_receiver();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_js_undefined();

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    // Remove all matching entries (backwards to preserve indices after splice)
    int64_t len = js_array_length(entries);
    for (int64_t i = len - 1; i >= 0; i--) {
        Item pair = js_elements_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item pair_name = js_elements_get_int(pair, 0);
        const char* n = fn_to_cstr(pair_name);
        if (n && strcmp(n, name_cs) == 0) {
            fd_entries_remove_at(entries, i);
        }
    }
    log_debug("fd_delete: deleted entries named '%s'", name_cs);
    return make_js_undefined();
}

static bool js_fd_entry_matches(Item pair, const char* name) {
    if (get_type_id(pair) != LMD_TYPE_ARRAY) return false;
    const char* entry_name = fn_to_cstr(js_elements_get_int(pair, 0));
    return entry_name && strcmp(entry_name, name) == 0;
}

static int64_t js_fd_find_entry(Item entries, const char* name) {
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        if (js_fd_entry_matches(js_elements_get_int(entries, i), name)) return i;
    }
    return -1;
}

static Item js_fd_get(Item name_item) {
    Item this_fd = dom_realm_receiver();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return ItemNull;

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    int64_t index = js_fd_find_entry(entries, name_cs);
    if (index >= 0) return js_elements_get_int(js_elements_get_int(entries, index), 1);
    return ItemNull;
}

static Item js_fd_getAll(Item name_item) {
    Item this_fd = dom_realm_receiver();
    Item result = js_array_new(0);
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return result;

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_elements_get_int(entries, i);
        if (js_fd_entry_matches(pair, name_cs)) {
            js_array_push(result, js_elements_get_int(pair, 1));
        }
    }
    return result;
}

static Item js_fd_has(Item name_item) {
    Item this_fd = dom_realm_receiver();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_bool(false);

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    return make_bool(js_fd_find_entry(entries, name_cs) >= 0);
}

static Item js_fd_set(Item name_item, Item value_item, Item filename_item) {
    Item this_fd = dom_realm_receiver();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_js_undefined();

    // 3-arg form set(name, blobValue, filename) is only valid when value is a Blob/File.
    // If 3rd arg is present (not undefined) but value is not a Blob/File → TypeError.
    if (get_type_id(filename_item) != LMD_TYPE_UNDEFINED && !fd_is_blob(value_item)) {
        return dom_realm_throw_type_error("set: 3-argument form requires a Blob value");
    }

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";
    JS_ASSIGN_OR_RETURN(value, fd_prepare_value(value_item, filename_item));

    // Find first occurrence
    int64_t first_idx = -1;
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_elements_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item pair_name = js_elements_get_int(pair, 0);
        const char* n = fn_to_cstr(pair_name);
        if (n && strcmp(n, name_cs) == 0) {
            first_idx = i;
            break;
        }
    }

    if (first_idx < 0) {
        // Not found: append new entry
        Item pair = js_array_new(0);
        js_array_push(pair, make_str(name_cs));
        js_array_push(pair, value);
        js_array_push(entries, pair);
    } else {
        // Update the first occurrence's value
        Item first_pair = js_elements_get_int(entries, first_idx);
        js_elements_set_int(first_pair, 1, value);

        // Remove all other occurrences (backwards to preserve splice indices)
        len = js_array_length(entries);
        for (int64_t i = len - 1; i > first_idx; i--) {
            Item pair = js_elements_get_int(entries, i);
            if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
            Item pair_name = js_elements_get_int(pair, 0);
            const char* n = fn_to_cstr(pair_name);
            if (n && strcmp(n, name_cs) == 0) {
                fd_entries_remove_at(entries, i);
            }
        }
    }
    log_debug("fd_set: set '%s'", name_cs);
    return make_js_undefined();
}

static Item js_fd_forEach(Item callback, Item this_arg) {
    if (!dom_realm_is_callable(callback)) return make_js_undefined();

    Item this_fd = dom_realm_receiver();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_js_undefined();

    // Live iteration: re-read length each iteration so deletions are observed
    for (int64_t i = 0; i < js_array_length(entries); i++) {
        Item pair = js_elements_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item name_val = js_elements_get_int(pair, 0);
        Item val_val  = js_elements_get_int(pair, 1);
        // forEach callback: (value, name, formData)
        Item cb_args[3] = {val_val, name_val, this_fd};
        JS_ASSIGN_OR_RETURN(callback_result, dom_realm_call(callback, this_arg, cb_args, 3));
    }
    return make_js_undefined();
}

// ============================================================================
// Iterator implementation
// ============================================================================

// Iterator modes: 0=entries (yield [name,value]), 1=keys, 2=values
#define FD_ITER_MODE_ENTRIES 0
#define FD_ITER_MODE_KEYS    1
#define FD_ITER_MODE_VALUES  2

static Item js_fd_iter_next() {
    Item iter = dom_realm_receiver();
    Item entries  = prop_get(iter, "_i_entries");
    Item idx_item = prop_get(iter, "_i_idx");
    Item mode_item= prop_get(iter, "_i_mode");

    int64_t idx  = (get_type_id(idx_item)  == LMD_TYPE_INT) ? it2i(idx_item)  : 0;
    int     mode = (get_type_id(mode_item) == LMD_TYPE_INT) ? (int)it2i(mode_item) : 0;
    int64_t len  = (get_type_id(entries) == LMD_TYPE_ARRAY) ? js_array_length(entries) : 0;

    Item result = js_new_object();
    if (idx >= len) {
        // iteration done
        prop_set(result, "value", make_js_undefined());
        prop_set(result, "done",  make_bool(true));
        return result;
    }

    // Advance index for next call
    prop_set(iter, "_i_idx", make_int_item(idx + 1));

    Item pair     = js_elements_get_int(entries, idx);
    Item name_val = (get_type_id(pair) == LMD_TYPE_ARRAY) ? js_elements_get_int(pair, 0) : ItemNull;
    Item val_val  = (get_type_id(pair) == LMD_TYPE_ARRAY) ? js_elements_get_int(pair, 1) : ItemNull;

    Item yield_val;
    if (mode == FD_ITER_MODE_KEYS) {
        yield_val = name_val;
    } else if (mode == FD_ITER_MODE_VALUES) {
        yield_val = val_val;
    } else {
        // entries mode: yield [name, value] pair
        yield_val = js_array_new(0);
        js_array_push(yield_val, name_val);
        js_array_push(yield_val, val_val);
    }

    prop_set(result, "value", yield_val);
    prop_set(result, "done",  make_bool(false));
    return result;
}

// Symbol.iterator on the iterator itself: returns `this`
JS_FORWARD_STATIC_ITEM(js_fd_iter_self, (), dom_realm_receiver, ())

static Item fd_make_iterator(Item entries, int mode) {
    Item iter = js_new_object();
    prop_set(iter, "_i_entries", entries);
    prop_set(iter, "_i_idx",     make_int_item(0));
    prop_set(iter, "_i_mode",    make_int_item(mode));
    prop_set(iter, "next",       dom_realm_new_function(js_fd_iter_next));
    // Symbol.iterator on the iterator → returns self (so it's iterable)
    dom_realm_set(iter, make_sym_iterator_key(), dom_realm_new_function(js_fd_iter_self));
    return iter;
}

#define JS_FD_ITERATOR_WRAPPER(name, mode) \
static Item name() { \
    return fd_make_iterator(fd_get_entries(dom_realm_receiver()), mode); \
}
JS_FD_ITERATOR_WRAPPER(js_fd_entries, FD_ITER_MODE_ENTRIES)
JS_FD_ITERATOR_WRAPPER(js_fd_keys, FD_ITER_MODE_KEYS)
JS_FD_ITERATOR_WRAPPER(js_fd_values, FD_ITER_MODE_VALUES)
#undef JS_FD_ITERATOR_WRAPPER

// ============================================================================
// F-1: Populate FormData entries from an HTMLFormElement's controls
// ============================================================================

// Normalize WTF-8 lone surrogates (ED A0..BF 80..BF) to U+FFFD (EF BF BD).
// Per HTML form submission encoding rules, lone surrogates are replaced.
static Item fd_normalize_surrogates(const char* s) {
    if (!s) return make_str("");
    size_t len = strlen(s);
    bool has_surrogate = false;
    for (size_t i = 0; i + 2 < len; i++) {
        unsigned char b1 = (unsigned char)s[i];
        unsigned char b2 = (unsigned char)s[i+1];
        unsigned char b3 = (unsigned char)s[i+2];
        if (b1 == 0xED && b2 >= 0xA0 && b2 <= 0xBF && b3 >= 0x80 && b3 <= 0xBF) {
            has_surrogate = true; break;
        }
    }
    if (!has_surrogate) return make_str(s);
    // same-length replacement (3 bytes → 3 bytes), copy then patch in-place
    char* buf = (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    if (!buf) return make_str(s);
    memcpy(buf, s, len + 1);
    for (size_t i = 0; i + 2 < len; i++) {
        unsigned char b1 = (unsigned char)buf[i];
        unsigned char b2 = (unsigned char)buf[i+1];
        unsigned char b3 = (unsigned char)buf[i+2];
        if (b1 == 0xED && b2 >= 0xA0 && b2 <= 0xBF && b3 >= 0x80 && b3 <= 0xBF) {
            buf[i] = (char)0xEF; buf[i+1] = (char)0xBF; buf[i+2] = (char)0xBD;
            i += 2; // combined with loop i++ → advances 3 bytes total
        }
    }
    Item result = make_str(buf);
    mem_free(buf);
    return result;
}

// Normalize newlines in a textarea value: CR-LF → LF, standalone CR → LF.
// Per HTML spec §4.10.20.6 (form data set algorithm, step for textarea).
static Item fd_normalize_newlines(const char* s) {
    if (!s) return make_str("");
    // Fast check: no CR → return as-is
    bool has_cr = false;
    for (const char* p = s; *p; p++) { if (*p == '\r') { has_cr = true; break; } }
    if (!has_cr) return make_str(s);

    // Normalize: \r\n → \n, \r → \n
    size_t len = strlen(s);
    char* buf = (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    if (!buf) return make_str(s);
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\r') {
            buf[out++] = '\n';
            if (s[i+1] == '\n') i++; // skip following \n
        } else {
            buf[out++] = s[i];
        }
    }
    buf[out] = '\0';
    Item result = make_str(buf);
    mem_free(buf);
    return result;
}

// Get the selected option values from a <select> element.
// Returns them as an array of strings (for multiple select).
static void fd_append_select_entries(Item entries, DomElement* select_elem) {
    const char* name = select_elem->get_attribute("name");
    if (!name || !*name) return; // no name → excluded from submission

    bool is_multiple = select_elem->has_attribute("multiple");

    // Walk option children to find selected ones
    DomNode* child = select_elem->first_child;
    while (child) {
        DomElement* ce = child->is_element() ? (DomElement*)child : nullptr;
        // Live selectedness, not the `selected` content attribute: that
        // attribute is only the *default* selection, so a user-chosen option
        // submitted the page's initial one (F21). dom_option_is_selected falls
        // back to the attribute when nothing has selected anything yet.
        if (ce && ce->tag_name && strcasecmp(ce->tag_name, "option") == 0 &&
            dom_option_is_selected(ce) &&
            !ce->has_attribute("disabled")) {
            // Spec: option value = value attr if present, else text content
            const char* opt_val = ce->get_attribute("value");
            if (!opt_val) {
                // fall back to text content
                DomNode* tn = ce->first_child;
                if (tn && tn->is_text()) {
                    opt_val = ((DomText*)tn)->text ? ((DomText*)tn)->text : "";
                } else {
                    opt_val = "";
                }
            }
            Item pair = js_array_new(0);
            js_array_push(pair, fd_normalize_surrogates(name));
            js_array_push(pair, fd_normalize_surrogates(opt_val));
            js_array_push(entries, pair);
            if (!is_multiple) break; // single select: first selected wins
        }
        // optgroup children
        if (ce && ce->tag_name && strcasecmp(ce->tag_name, "optgroup") == 0) {
            bool og_disabled = ce->has_attribute("disabled");
            DomNode* ogchild = ce->first_child;
            while (ogchild) {
                DomElement* oce = ogchild->is_element() ? (DomElement*)ogchild : nullptr;
                if (oce && oce->tag_name && strcasecmp(oce->tag_name, "option") == 0 &&
                    dom_option_is_selected(oce) &&
                    !oce->has_attribute("disabled") && !og_disabled) {
                    const char* opt_val = oce->get_attribute("value");
                    if (!opt_val) {
                        DomNode* tn = oce->first_child;
                        opt_val = (tn && tn->is_text() && ((DomText*)tn)->text)
                            ? ((DomText*)tn)->text : "";
                    }
                    Item pair = js_array_new(0);
                    js_array_push(pair, fd_normalize_surrogates(name));
                    js_array_push(pair, fd_normalize_surrogates(opt_val));
                    js_array_push(entries, pair);
                    if (!is_multiple) goto done_select;
                }
                ogchild = ogchild->next_sibling;
            }
        }
        child = child->next_sibling;
    }
    done_select:;
}

// forward declaration (defined after fd_install_methods)
static Item fd_make_file_stub();

// Recursively walk document order, retaining only controls owned by `form`.
// This includes controls associated through form="..." outside the subtree.
static void fd_walk_form_controls(Item entries, DomNode* node, DomElement* form) {
    while (node) {
        if (!node->is_element()) {
            node = node->next_sibling;
            continue;
        }
        DomElement* elem = (DomElement*)node;
        const char* tag = elem->tag_name ? elem->tag_name : "";
        bool is_form_control = strcasecmp(tag, "input") == 0 ||
            strcasecmp(tag, "textarea") == 0 ||
            strcasecmp(tag, "select") == 0 ||
            strcasecmp(tag, "button") == 0;
        if (is_form_control && form &&
            dom_find_form_owner((void*)elem) != form) {
            node = node->next_sibling;
            continue;
        }

        if (strcasecmp(tag, "input") == 0) {
            const char* name = elem->get_attribute("name");
            if (name && *name && !dom_is_disabled(elem)) {                const char* itype = dom_input_type_lower(elem);
                // excluded from form data: type=submit, reset, button, image
                bool excluded = (strcmp(itype, "submit") == 0 || strcmp(itype, "reset") == 0 ||
                                 strcmp(itype, "button") == 0 || strcmp(itype, "image") == 0);
                if (!excluded) {
                    Item name_item = fd_normalize_surrogates(name);
                    if (strcmp(itype, "checkbox") == 0 || strcmp(itype, "radio") == 0) {
                        if (dom_get_checkedness(elem)) {
                            const char* val = elem->get_attribute("value");
                            Item pair = js_array_new(0);
                            js_array_push(pair, name_item);
                            js_array_push(pair, fd_normalize_surrogates(val ? val : "on"));
                            js_array_push(entries, pair);
                        }
                    } else if (strcmp(itype, "file") == 0) {
                        Item files = radiant_input_files(elem);
                        int64_t file_count = get_type_id(files) == LMD_TYPE_ARRAY
                            ? js_array_length(files) : 0;
                        if (file_count == 0) {
                            Item pair = js_array_new(0);
                            js_array_push(pair, name_item);
                            js_array_push(pair, fd_make_file_stub());
                            js_array_push(entries, pair);
                        } else {
                            for (int64_t file_index = 0; file_index < file_count;
                                 file_index++) {
                                Item pair = js_array_new(0);
                                js_array_push(pair, name_item);
                                js_array_push(pair, js_elements_get_int(files, file_index));
                                js_array_push(entries, pair);
                            }
                        }
                    } else {
                        // text, number, email, url, tel, search, password, hidden, etc.
                        // Use tc_ensure_init for initialized text controls; fall back to
                        // the 'value' HTML attribute for hidden/uninitialized controls.
                        const char* val = nullptr;
                        if (tc_is_text_control(elem)) {
                            tc_ensure_init(elem);
                            val = (elem->form && elem->form->current_value)
                                ? elem->form->current_value : "";
                        } else {
                            // Every other value state (range, hidden, color, the
                            // date family) keeps its live value in the input
                            // value store, which is what `input.value` reads and
                            // what the range slider and stepUp() write. Reading
                            // the content attribute here submitted the *default*
                            // instead, so a moved slider posted its start value.
                            // The store seeds itself from that attribute, so this
                            // is still the attribute when nothing has written.
                            val = radiant_input_live_value(elem);
                            if (!val) val = "";
                        }
                        Item pair = js_array_new(0);
                        js_array_push(pair, name_item);
                        js_array_push(pair, fd_normalize_surrogates(val));
                        js_array_push(entries, pair);
                        // dirname: if the control has a dirname attribute, add a directionality entry
                        const char* dirname = elem->get_attribute("dirname");
                        if (dirname && *dirname && fd_input_supports_dirname(itype)) {
                            // per spec: always "ltr" in headless (no bidi algorithm)
                            Item dir_pair = js_array_new(0);
                            js_array_push(dir_pair, make_str(dirname));
                            js_array_push(dir_pair,
                                make_str(fd_compute_dirname_direction(elem, val)));
                            js_array_push(entries, dir_pair);
                        }
                    }
                }
            }
        } else if (strcasecmp(tag, "textarea") == 0) {
            const char* name = elem->get_attribute("name");
            if (name && *name && !dom_is_disabled(elem)) {
                tc_ensure_init(elem);
                const char* val = elem->form && elem->form->current_value
                    ? elem->form->current_value : "";
                // normalize newlines first, then surrogates
                Item nl_item = fd_normalize_newlines(val);
                const char* nl_str = fn_to_cstr(nl_item);
                Item pair = js_array_new(0);
                js_array_push(pair, fd_normalize_surrogates(name));
                js_array_push(pair, fd_normalize_surrogates(nl_str ? nl_str : ""));
                js_array_push(entries, pair);
                const char* dirname = elem->get_attribute("dirname");
                if (dirname && *dirname) {
                    Item dir_pair = js_array_new(0);
                    js_array_push(dir_pair, make_str(dirname));
                    js_array_push(dir_pair,
                        make_str(fd_compute_dirname_direction(elem, val)));
                    js_array_push(entries, dir_pair);
                }
            }
        } else if (strcasecmp(tag, "select") == 0) {
            if (!dom_is_disabled(elem)) {
                fd_append_select_entries(entries, elem);
            }
        } else if (strcasecmp(tag, "button") == 0) {
            // buttons are excluded from form data by default (only included on submit)
        } else if (strcasecmp(tag, "fieldset") == 0) {
            // recurse into fieldset unless it's disabled (disabled fieldset disables children)
            if (!elem->has_attribute("disabled")) {
                fd_walk_form_controls(entries, elem->first_child, form);
            }
        }

        // Recurse into non-replaced children (except fieldset which is handled above,
        // and datalist which is not form-associated per spec)
        if (strcasecmp(tag, "fieldset") != 0 &&
            strcasecmp(tag, "input") != 0 &&
            strcasecmp(tag, "select") != 0 &&
            strcasecmp(tag, "textarea") != 0 &&
            strcasecmp(tag, "datalist") != 0) {
            fd_walk_form_controls(entries, elem->first_child, form);
        }

        node = node->next_sibling;
    }
}

static void fd_collect_form_controls(Item entries, DomElement* form) {
    if (!form) return;
    DomDocument* doc = form->doc;
    bool connected = false;
    if (doc && doc->root) {
        for (DomNode* node = (DomNode*)form; node; node = node->parent) {
            if (node == (DomNode*)doc->root) {
                connected = true;
                break;
            }
        }
    }
    fd_walk_form_controls(entries,
        connected ? doc->root->first_child : form->first_child, form);
}

// ============================================================================
// FormData constructor
// ============================================================================

static void fd_install_methods(Item fd_obj) {
    prop_set(fd_obj, "append",  dom_realm_new_function(js_fd_append));
    prop_set(fd_obj, "delete",  dom_realm_new_function(js_fd_delete));
    prop_set(fd_obj, "get",     dom_realm_new_function(js_fd_get));
    prop_set(fd_obj, "getAll",  dom_realm_new_function(js_fd_getAll));
    prop_set(fd_obj, "has",     dom_realm_new_function(js_fd_has));
    prop_set(fd_obj, "set",     dom_realm_new_function(js_fd_set));
    prop_set(fd_obj, "entries", dom_realm_new_function(js_fd_entries));
    prop_set(fd_obj, "keys",    dom_realm_new_function(js_fd_keys));
    prop_set(fd_obj, "values",  dom_realm_new_function(js_fd_values));
    prop_set(fd_obj, "forEach", dom_realm_new_function(js_fd_forEach));
    // Symbol.iterator → same as entries()
    dom_realm_set(fd_obj, make_sym_iterator_key(), dom_realm_new_function(js_fd_entries));
}

// Current epoch milliseconds.
static int64_t now_epoch_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Convert a Blob value to a File when filename is provided to FormData.append/set.
// If value is already a File, returns a clone with name=filename. If value is a Blob
// (not a File), returns a new File wrapping it with name=filename.
// If filename is undefined and value is a Blob (not File), returns a File named "blob".
static Item fd_blob_to_file(Item value, Item filename_item) {
    // value must be a Blob/File MAP at this point
    bool has_filename = (get_type_id(filename_item) != LMD_TYPE_UNDEFINED);
    bool is_file = (js_class_id(value) == JS_CLASS_FILE);

    Item file = dom_realm_new_object_of_class(JS_CLASS_FILE);

    Item sz = prop_get(value, "size");
    prop_set(file, "size", get_type_id(sz) == LMD_TYPE_INT ? sz : make_int_item(0));

    Item ty = prop_get(value, "type");
    prop_set(file, "type", get_type_id(ty) == LMD_TYPE_STRING ? ty : make_str(""));

    const char* fname = nullptr;
    if (has_filename) fname = fn_to_cstr(filename_item);
    else if (is_file) {
        Item nm = prop_get(value, "name");
        if (get_type_id(nm) == LMD_TYPE_STRING) fname = fn_to_cstr(nm);
    }
    if (!fname) fname = is_file ? "" : "blob";
    prop_set(file, "name", make_str(fname));

    int64_t lm = 0;
    Item lm_item = prop_get(value, "lastModified");
    if (get_type_id(lm_item) == LMD_TYPE_INT) lm = it2i(lm_item);
    else lm = now_epoch_ms();
    prop_set(file, "lastModified", make_int_item(lm));

    Item ctor = prop_get(dom_realm_global(), "File");
    if (get_type_id(ctor) != LMD_TYPE_UNDEFINED) prop_set(file, "constructor", ctor);
    return file;
}

// Create a File stub for an empty file input.
static Item fd_make_file_stub() {
    Item obj = dom_realm_new_object_of_class(JS_CLASS_FILE);
    prop_set(obj, "size",           make_int_item(0));
    prop_set(obj, "name",           make_str(""));
    prop_set(obj, "type",           make_str("application/octet-stream"));
    prop_set(obj, "lastModified",   make_int_item(now_epoch_ms()));
    Item ctor = prop_get(dom_realm_global(), "File");
    if (get_type_id(ctor) != LMD_TYPE_UNDEFINED) prop_set(obj, "constructor", ctor);
    return obj;
}

static void fd_append_submitter_entry(Item entries, DomElement* elem) {
    if (!elem || dom_is_disabled(elem)) return;

    const char* tag = elem->tag_name ? elem->tag_name : "";
    const char* name = elem->get_attribute("name");
    if (!name || !*name) return;

    if (strcasecmp(tag, "input") == 0) {
        const char* itype = dom_input_type_lower(elem);
        if (strcmp(itype, "submit") != 0) return;
        const char* val = elem->get_attribute("value");
        if (!val) val = "";
        Item pair = js_array_new(0);
        js_array_push(pair, fd_normalize_surrogates(name));
        js_array_push(pair, fd_normalize_surrogates(val));
        js_array_push(entries, pair);
        const char* dirname = elem->get_attribute("dirname");
        if (dirname && *dirname && fd_input_supports_dirname(itype)) {
            Item dir_pair = js_array_new(0);
            js_array_push(dir_pair, make_str(dirname));
            js_array_push(dir_pair,
                make_str(fd_compute_dirname_direction(elem, val)));
            js_array_push(entries, dir_pair);
        }
        return;
    }

    if (strcasecmp(tag, "button") == 0) {
        const char* type = elem->get_attribute("type");
        if (type && *type && strcasecmp(type, "submit") != 0) return;
        const char* val = elem->get_attribute("value");
        if (!val) val = "";
        Item pair = js_array_new(0);
        js_array_push(pair, fd_normalize_surrogates(name));
        js_array_push(pair, fd_normalize_surrogates(val));
        js_array_push(entries, pair);
    }
}

// Constructor: new FormData([form[, submitter]])
static Item js_formdata_construct(Item first, Item submitter) {
    // Per WebIDL: new FormData() is fine; new FormData(nonFormElement) throws TypeError.
    // undefined is treated the same as no argument (the optional argument is absent).
    TypeId ft = get_type_id(first);
    if (ft != LMD_TYPE_UNDEFINED) {
        // null, string, number, non-form MAP → TypeError
        bool is_form_elem = false;

        // DOM3 wrappers are host VMaps, so receiver identity must be decided by
        // unwrapping rather than by the obsolete map-shell representation.
        void* node_raw = dom_unwrap_element(first);
        DomNode* node = (DomNode*)node_raw;
        if (node && node->is_element()) {
            DomElement* elem = (DomElement*)node;
            if (elem->tag_name && strcasecmp(elem->tag_name, "form") == 0) {
                is_form_elem = true;
            }
        }

        if (!is_form_elem) {
            return dom_realm_throw_type_error("FormData argument must be an HTMLFormElement");
        }
    }

    // Create the FormData object
    Item fd_obj = dom_realm_new_object_of_class(JS_CLASS_FORM_DATA);
    Item entries = js_array_new(0);
    prop_set(fd_obj, FD_ENTRIES_KEY, entries);
    fd_install_methods(fd_obj);

    // F-1: populate from form controls when form element was provided
    if (get_type_id(first) != LMD_TYPE_UNDEFINED) {
        void* node_raw = dom_unwrap_element(first);
        DomNode* node = (DomNode*)node_raw;
        if (node && node->is_element()) {
            DomElement* form_elem = (DomElement*)node;
            fd_collect_form_controls(entries, form_elem);
            void* submitter_raw = dom_unwrap_element(submitter);
            DomNode* submitter_node = (DomNode*)submitter_raw;
            if (submitter_node && submitter_node->is_element()) {
                fd_append_submitter_entry(entries, (DomElement*)submitter_node);
            }
            log_debug("js_formdata_construct: populated from <form>, entries=%lld",
                      (long long)js_array_length(entries));
        }
    }

    log_debug("js_formdata_construct: created FormData");
    return fd_obj;
}

extern "C" Item js_formdata_collect_form_entries(void* form_elem, void* submitter_elem) {
    Item entries = js_array_new(0);
    DomNode* form_node = (DomNode*)form_elem;
    if (!form_node || !form_node->is_element()) return entries;

    DomElement* form_dom = (DomElement*)form_node;
    fd_collect_form_controls(entries, form_dom);
    if (submitter_elem) {
        DomNode* submitter_node = (DomNode*)submitter_elem;
        if (submitter_node->is_element()) {
            fd_append_submitter_entry(entries, (DomElement*)submitter_node);
        }
    }
    return entries;
}

// ============================================================================
// Global installation
// ============================================================================

extern "C" void js_formdata_install_globals(void) {
    Item global = dom_realm_global();
    // D6.2.2v2: global constructor bindings carry [[Construct]] themselves;
    // no compiler name interception remains to supply it.
    Item ctor_fn = js_new_native_constructor(js_formdata_construct);
    prop_set(global, "FormData", ctor_fn);

    // C1.3: the Blob/File fallback constructors that used to live here were
    // unreachable. js_register_clipboard_globals() installs the full-spec
    // Blob/File during global bootstrap, which always runs before this
    // installer (dom_set_document -> js_formdata_install_globals), so the
    // "not yet defined" guards never fired.
    Item blob_ctor_fn = prop_get(global, "Blob");
    Item file_ctor_fn = prop_get(global, "File");

    // Also install on window if it exists
    Item window = prop_get(global, "window");
    if (get_type_id(window) == LMD_TYPE_MAP) {
        prop_set(window, "FormData", ctor_fn);
        prop_set(window, "Blob", blob_ctor_fn);
        prop_set(window, "File", file_ctor_fn);
    }
    log_debug("js_formdata_install_globals: FormData, Blob, File installed on global");
}
