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
 * Called from: js_dom.cpp: js_dom_set_document()
 */

#include "js_dom.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../../radiant/form_control.hpp"
#include "../../radiant/text_control.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cctype>
#include <cinttypes>
#include <ctime>

// ============================================================================
// Forward declarations of engine APIs used here
// ============================================================================

extern "C" int js_check_exception(void);
extern "C" Item js_call_function(Item func, Item this_val, Item* args, int argc);
extern "C" Item js_array_method(Item arr, Item method_name, Item* args, int argc);

// Helpers from js_dom.cpp used for form control inspection
extern "C" bool js_dom_get_checkedness(void* dom_elem);
extern "C" const char* js_dom_input_type_lower(void* dom_elem);
extern "C" const char* js_dom_tag_name_raw(void* dom_elem);
extern "C" bool js_dom_is_disabled(void* dom_elem);

// ============================================================================
// Helpers
// ============================================================================

static inline Item make_js_undefined() {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static inline Item make_bool(bool v) {
    return (Item){.item = b2it(v ? 1 : 0)};
}

static inline Item make_int_item(int64_t v) {
    return (Item){.item = i2it(v)};
}

static inline Item make_str(const char* s) {
    if (!s) return ItemNull;
    return (Item){.item = s2it(heap_create_name(s))};
}

static inline Item make_key(const char* s) { return make_str(s); }

static inline Item prop_get(Item obj, const char* key) {
    return js_property_get(obj, make_key(key));
}

static inline void prop_set(Item obj, const char* key, Item val) {
    js_property_set(obj, make_key(key), val);
}

// Symbol.iterator key: "__sym_1" (Symbol.iterator = ID 1 in this engine)
static inline Item make_sym_iterator_key() {
    return make_str("__sym_1");
}

// Internal entries array key
static const char* FD_ENTRIES_KEY = "_fd_entries";

// Forward decls
static Item fd_blob_to_file(Item value, Item filename_item);

static Item fd_get_entries(Item this_fd) {
    return prop_get(this_fd, FD_ENTRIES_KEY);
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
            return js_throw_type_error("Cannot convert a Symbol value to a string");
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

// Check if an Item is a Blob/File (MAP with __class_name__ == "Blob" or "File")
static bool fd_is_blob(Item v) {
    if (get_type_id(v) != LMD_TYPE_MAP) return false;
    Item cn = prop_get(v, "__class_name__");
    if (get_type_id(cn) != LMD_TYPE_STRING) return false;
    const char* s = fn_to_cstr(cn);
    return s && (strcmp(s, "Blob") == 0 || strcmp(s, "File") == 0);
}

static Item js_fd_append(Item name_item, Item value_item, Item filename_item) {
    Item this_fd = js_get_this();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_js_undefined();

    // 3-arg form append(name, blobValue, filename) is only valid when value is a Blob/File.
    // If 3rd arg is present (not undefined) but value is not a Blob/File → TypeError.
    if (get_type_id(filename_item) != LMD_TYPE_UNDEFINED && !fd_is_blob(value_item)) {
        return js_throw_type_error("append: 3-argument form requires a Blob value");
    }

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    Item value = fd_coerce_value(value_item);
    if (js_check_exception()) return ItemNull;
    // Per spec: if value is Blob (not File) and no filename, set filename to "blob".
    // If value is Blob/File and filename was provided, convert to File with that name.
    if (fd_is_blob(value)) {
        Item cls = prop_get(value, "__class_name__");
        const char* cs = (get_type_id(cls) == LMD_TYPE_STRING) ? fn_to_cstr(cls) : nullptr;
        bool is_file = cs && strcmp(cs, "File") == 0;
        bool has_filename = (get_type_id(filename_item) != LMD_TYPE_UNDEFINED);
        if (has_filename || !is_file) {
            value = fd_blob_to_file(value, filename_item);
        }
    }

    Item pair = js_array_new(0);
    js_array_push(pair, make_str(name_cs));
    js_array_push(pair, value);
    js_array_push(entries, pair);
    log_debug("fd_append: appended '%s'", name_cs);
    return make_js_undefined();
}

static Item js_fd_delete(Item name_item) {
    Item this_fd = js_get_this();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_js_undefined();

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    // Remove all matching entries (backwards to preserve indices after splice)
    int64_t len = js_array_length(entries);
    for (int64_t i = len - 1; i >= 0; i--) {
        Item pair = js_array_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item pair_name = js_array_get_int(pair, 0);
        const char* n = fn_to_cstr(pair_name);
        if (n && strcmp(n, name_cs) == 0) {
            Item splice_args[2] = {make_int_item(i), make_int_item(1)};
            js_array_method(entries, make_key("splice"), splice_args, 2);
        }
    }
    log_debug("fd_delete: deleted entries named '%s'", name_cs);
    return make_js_undefined();
}

static Item js_fd_get(Item name_item) {
    Item this_fd = js_get_this();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return ItemNull;

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_array_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item pair_name = js_array_get_int(pair, 0);
        const char* n = fn_to_cstr(pair_name);
        if (n && strcmp(n, name_cs) == 0) {
            return js_array_get_int(pair, 1);
        }
    }
    return ItemNull;
}

static Item js_fd_getAll(Item name_item) {
    Item this_fd = js_get_this();
    Item result = js_array_new(0);
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return result;

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_array_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item pair_name = js_array_get_int(pair, 0);
        const char* n = fn_to_cstr(pair_name);
        if (n && strcmp(n, name_cs) == 0) {
            js_array_push(result, js_array_get_int(pair, 1));
        }
    }
    return result;
}

static Item js_fd_has(Item name_item) {
    Item this_fd = js_get_this();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_bool(false);

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";

    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_array_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item pair_name = js_array_get_int(pair, 0);
        const char* n = fn_to_cstr(pair_name);
        if (n && strcmp(n, name_cs) == 0) {
            return make_bool(true);
        }
    }
    return make_bool(false);
}

static Item js_fd_set(Item name_item, Item value_item, Item filename_item) {
    Item this_fd = js_get_this();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_js_undefined();

    // 3-arg form set(name, blobValue, filename) is only valid when value is a Blob/File.
    // If 3rd arg is present (not undefined) but value is not a Blob/File → TypeError.
    if (get_type_id(filename_item) != LMD_TYPE_UNDEFINED && !fd_is_blob(value_item)) {
        return js_throw_type_error("set: 3-argument form requires a Blob value");
    }

    const char* name_cs = fn_to_cstr(name_item);
    if (!name_cs) name_cs = "undefined";
    Item value = fd_coerce_value(value_item);
    if (js_check_exception()) return ItemNull;
    if (fd_is_blob(value)) {
        Item cls = prop_get(value, "__class_name__");
        const char* cs = (get_type_id(cls) == LMD_TYPE_STRING) ? fn_to_cstr(cls) : nullptr;
        bool is_file = cs && strcmp(cs, "File") == 0;
        bool has_filename = (get_type_id(filename_item) != LMD_TYPE_UNDEFINED);
        if (has_filename || !is_file) {
            value = fd_blob_to_file(value, filename_item);
        }
    }

    // Find first occurrence
    int64_t first_idx = -1;
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_array_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item pair_name = js_array_get_int(pair, 0);
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
        Item first_pair = js_array_get_int(entries, first_idx);
        js_array_set_int(first_pair, 1, value);

        // Remove all other occurrences (backwards to preserve splice indices)
        len = js_array_length(entries);
        for (int64_t i = len - 1; i > first_idx; i--) {
            Item pair = js_array_get_int(entries, i);
            if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
            Item pair_name = js_array_get_int(pair, 0);
            const char* n = fn_to_cstr(pair_name);
            if (n && strcmp(n, name_cs) == 0) {
                Item splice_args[2] = {make_int_item(i), make_int_item(1)};
                js_array_method(entries, make_key("splice"), splice_args, 2);
            }
        }
    }
    log_debug("fd_set: set '%s'", name_cs);
    return make_js_undefined();
}

static Item js_fd_forEach(Item callback, Item this_arg) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();

    Item this_fd = js_get_this();
    Item entries = fd_get_entries(this_fd);
    if (get_type_id(entries) != LMD_TYPE_ARRAY) return make_js_undefined();

    // Live iteration: re-read length each iteration so deletions are observed
    for (int64_t i = 0; i < js_array_length(entries); i++) {
        Item pair = js_array_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item name_val = js_array_get_int(pair, 0);
        Item val_val  = js_array_get_int(pair, 1);
        // forEach callback: (value, name, formData)
        Item cb_args[3] = {val_val, name_val, this_fd};
        js_call_function(callback, this_arg, cb_args, 3);
        if (js_check_exception()) return ItemNull;
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
    Item iter = js_get_this();
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

    Item pair     = js_array_get_int(entries, idx);
    Item name_val = (get_type_id(pair) == LMD_TYPE_ARRAY) ? js_array_get_int(pair, 0) : ItemNull;
    Item val_val  = (get_type_id(pair) == LMD_TYPE_ARRAY) ? js_array_get_int(pair, 1) : ItemNull;

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
static Item js_fd_iter_self() {
    return js_get_this();
}

static Item fd_make_iterator(Item entries, int mode) {
    Item iter = js_new_object();
    prop_set(iter, "_i_entries", entries);
    prop_set(iter, "_i_idx",     make_int_item(0));
    prop_set(iter, "_i_mode",    make_int_item(mode));
    prop_set(iter, "next",       js_new_function((void*)js_fd_iter_next, 0));
    // Symbol.iterator on the iterator → returns self (so it's iterable)
    js_property_set(iter, make_sym_iterator_key(), js_new_function((void*)js_fd_iter_self, 0));
    return iter;
}

static Item js_fd_entries() {
    return fd_make_iterator(fd_get_entries(js_get_this()), FD_ITER_MODE_ENTRIES);
}

static Item js_fd_keys() {
    return fd_make_iterator(fd_get_entries(js_get_this()), FD_ITER_MODE_KEYS);
}

static Item js_fd_values() {
    return fd_make_iterator(fd_get_entries(js_get_this()), FD_ITER_MODE_VALUES);
}

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
    char* buf = (char*)malloc(len + 1);
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
    free(buf);
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
    char* buf = (char*)malloc(len + 1);
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
    free(buf);
    return result;
}

// Get the selected option values from a <select> element.
// Returns them as an array of strings (for multiple select).
static void fd_append_select_entries(Item entries, DomElement* select_elem) {
    const char* name = dom_element_get_attribute(select_elem, "name");
    if (!name || !*name) return; // no name → excluded from submission

    bool is_multiple = dom_element_has_attribute(select_elem, "multiple");

    // Walk option children to find selected ones
    DomNode* child = select_elem->first_child;
    while (child) {
        DomElement* ce = child->is_element() ? (DomElement*)child : nullptr;
        if (ce && ce->tag_name && strcasecmp(ce->tag_name, "option") == 0 &&
            dom_element_has_attribute(ce, "selected") &&
            !dom_element_has_attribute(ce, "disabled")) {
            // Spec: option value = value attr if present, else text content
            const char* opt_val = dom_element_get_attribute(ce, "value");
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
            bool og_disabled = dom_element_has_attribute(ce, "disabled");
            DomNode* ogchild = ce->first_child;
            while (ogchild) {
                DomElement* oce = ogchild->is_element() ? (DomElement*)ogchild : nullptr;
                if (oce && oce->tag_name && strcasecmp(oce->tag_name, "option") == 0 &&
                    dom_element_has_attribute(oce, "selected") &&
                    !dom_element_has_attribute(oce, "disabled") && !og_disabled) {
                    const char* opt_val = dom_element_get_attribute(oce, "value");
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

// Recursively walk the form's DOM subtree, collecting form data entries.
static void fd_walk_form_controls(Item entries, DomNode* node) {
    while (node) {
        if (!node->is_element()) {
            node = node->next_sibling;
            continue;
        }
        DomElement* elem = (DomElement*)node;
        const char* tag = elem->tag_name ? elem->tag_name : "";

        if (strcasecmp(tag, "input") == 0) {
            const char* name = dom_element_get_attribute(elem, "name");
            if (name && *name && !js_dom_is_disabled(elem)) {                const char* itype = js_dom_input_type_lower(elem);
                // excluded from form data: type=submit, reset, button, image
                bool excluded = (strcmp(itype, "submit") == 0 || strcmp(itype, "reset") == 0 ||
                                 strcmp(itype, "button") == 0 || strcmp(itype, "image") == 0);
                if (!excluded) {
                    Item name_item = fd_normalize_surrogates(name);
                    if (strcmp(itype, "checkbox") == 0 || strcmp(itype, "radio") == 0) {
                        if (js_dom_get_checkedness(elem)) {
                            const char* val = dom_element_get_attribute(elem, "value");
                            Item pair = js_array_new(0);
                            js_array_push(pair, name_item);
                            js_array_push(pair, fd_normalize_surrogates(val ? val : "on"));
                            js_array_push(entries, pair);
                        }
                    } else if (strcmp(itype, "file") == 0) {
                        // type=file: include an empty File stub (no files selected)
                        Item pair = js_array_new(0);
                        js_array_push(pair, name_item);
                        js_array_push(pair, fd_make_file_stub());
                        js_array_push(entries, pair);
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
                            // type=hidden or other non-text-control: use attribute value
                            val = dom_element_get_attribute(elem, "value");
                            if (!val) val = "";
                        }
                        Item pair = js_array_new(0);
                        js_array_push(pair, name_item);
                        js_array_push(pair, fd_normalize_surrogates(val));
                        js_array_push(entries, pair);
                        // dirname: if the control has a dirname attribute, add a directionality entry
                        const char* dirname = dom_element_get_attribute(elem, "dirname");
                        if (dirname && *dirname) {
                            // per spec: always "ltr" in headless (no bidi algorithm)
                            Item dir_pair = js_array_new(0);
                            js_array_push(dir_pair, make_str(dirname));
                            js_array_push(dir_pair, make_str("ltr"));
                            js_array_push(entries, dir_pair);
                        }
                    }
                }
            }
        } else if (strcasecmp(tag, "textarea") == 0) {
            const char* name = dom_element_get_attribute(elem, "name");
            if (name && *name && !js_dom_is_disabled(elem)) {
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
            }
        } else if (strcasecmp(tag, "select") == 0) {
            if (!js_dom_is_disabled(elem)) {
                fd_append_select_entries(entries, elem);
            }
        } else if (strcasecmp(tag, "button") == 0) {
            // buttons are excluded from form data by default (only included on submit)
        } else if (strcasecmp(tag, "fieldset") == 0) {
            // recurse into fieldset unless it's disabled (disabled fieldset disables children)
            if (!dom_element_has_attribute(elem, "disabled")) {
                fd_walk_form_controls(entries, elem->first_child);
            }
        }

        // Recurse into non-replaced children (except fieldset which is handled above,
        // and datalist which is not form-associated per spec)
        if (strcasecmp(tag, "fieldset") != 0 &&
            strcasecmp(tag, "input") != 0 &&
            strcasecmp(tag, "select") != 0 &&
            strcasecmp(tag, "textarea") != 0 &&
            strcasecmp(tag, "datalist") != 0) {
            fd_walk_form_controls(entries, elem->first_child);
        }

        node = node->next_sibling;
    }
}

// ============================================================================
// FormData constructor
// ============================================================================

static void fd_install_methods(Item fd_obj) {
    prop_set(fd_obj, "append",  js_new_function((void*)js_fd_append,  3));
    prop_set(fd_obj, "delete",  js_new_function((void*)js_fd_delete,  1));
    prop_set(fd_obj, "get",     js_new_function((void*)js_fd_get,     1));
    prop_set(fd_obj, "getAll",  js_new_function((void*)js_fd_getAll,  1));
    prop_set(fd_obj, "has",     js_new_function((void*)js_fd_has,     1));
    prop_set(fd_obj, "set",     js_new_function((void*)js_fd_set,     3));
    prop_set(fd_obj, "entries", js_new_function((void*)js_fd_entries, 0));
    prop_set(fd_obj, "keys",    js_new_function((void*)js_fd_keys,    0));
    prop_set(fd_obj, "values",  js_new_function((void*)js_fd_values,  0));
    prop_set(fd_obj, "forEach", js_new_function((void*)js_fd_forEach, 2));
    // Symbol.iterator → same as entries()
    js_property_set(fd_obj, make_sym_iterator_key(), js_new_function((void*)js_fd_entries, 0));
    // class marker for instanceof checks
    prop_set(fd_obj, "__class_name__", make_str("FormData"));
}

// ============================================================================
// Blob and File constructors
// ============================================================================

// Compute the byte-size of a Blob parts array (parts is an Array of strings/Blobs).
// Strings count UTF-8 byte length; Blob/File parts use their .size property.
static int64_t blob_compute_size(Item parts) {
    if (get_type_id(parts) != LMD_TYPE_ARRAY) return 0;
    int64_t total = 0;
    int64_t plen = js_array_length(parts);
    for (int64_t i = 0; i < plen; i++) {
        Item p = js_array_get_int(parts, i);
        TypeId pt = get_type_id(p);
        if (pt == LMD_TYPE_STRING) {
            const char* s = fn_to_cstr(p);
            if (s) total += (int64_t)strlen(s);
        } else if (pt == LMD_TYPE_MAP) {
            // nested Blob/File: add its size
            Item sz = prop_get(p, "size");
            if (get_type_id(sz) == LMD_TYPE_INT) total += it2i(sz);
        } else {
            // coerce to string and use its byte length
            const char* s = fn_to_cstr(p);
            if (s) total += (int64_t)strlen(s);
        }
    }
    return total;
}

// Read options.type (a string) → returns owned string or "".
static const char* blob_options_type(Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP) return "";
    Item t = prop_get(options, "type");
    if (get_type_id(t) != LMD_TYPE_STRING) return "";
    const char* s = fn_to_cstr(t);
    return s ? s : "";
}

// new Blob([parts], { type })
static Item js_blob_construct(Item parts, Item options) {
    Item obj = js_new_object();
    prop_set(obj, "__class_name__", make_str("Blob"));
    int64_t size = (get_type_id(parts) == LMD_TYPE_UNDEFINED) ? 0 : blob_compute_size(parts);
    prop_set(obj, "size", make_int_item(size));
    prop_set(obj, "type", make_str(blob_options_type(options)));
    Item ctor = prop_get(js_get_global_this(), "Blob");
    if (get_type_id(ctor) != LMD_TYPE_UNDEFINED) prop_set(obj, "constructor", ctor);
    return obj;
}

// Current epoch milliseconds.
static int64_t now_epoch_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// new File([parts], name, { type, lastModified })
static Item js_file_construct(Item parts, Item name, Item options) {
    Item obj = js_new_object();
    prop_set(obj, "__class_name__", make_str("File"));

    int64_t size = (get_type_id(parts) == LMD_TYPE_UNDEFINED) ? 0 : blob_compute_size(parts);
    prop_set(obj, "size", make_int_item(size));
    prop_set(obj, "type", make_str(blob_options_type(options)));

    const char* name_cs = (get_type_id(name) == LMD_TYPE_UNDEFINED) ? "" : fn_to_cstr(name);
    prop_set(obj, "name", make_str(name_cs ? name_cs : ""));

    int64_t lm = now_epoch_ms();
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item lm_item = prop_get(options, "lastModified");
        if (get_type_id(lm_item) == LMD_TYPE_INT) lm = it2i(lm_item);
        else if (get_type_id(lm_item) == LMD_TYPE_FLOAT) lm = (int64_t)it2d(lm_item);
    }
    prop_set(obj, "lastModified", make_int_item(lm));

    Item ctor = prop_get(js_get_global_this(), "File");
    if (get_type_id(ctor) != LMD_TYPE_UNDEFINED) prop_set(obj, "constructor", ctor);
    return obj;
}

// Convert a Blob value to a File when filename is provided to FormData.append/set.
// If value is already a File, returns a clone with name=filename. If value is a Blob
// (not a File), returns a new File wrapping it with name=filename.
// If filename is undefined and value is a Blob (not File), returns a File named "blob".
static Item fd_blob_to_file(Item value, Item filename_item) {
    // value must be a Blob/File MAP at this point
    bool has_filename = (get_type_id(filename_item) != LMD_TYPE_UNDEFINED);
    Item cls = prop_get(value, "__class_name__");
    const char* cls_s = (get_type_id(cls) == LMD_TYPE_STRING) ? fn_to_cstr(cls) : nullptr;
    bool is_file = cls_s && strcmp(cls_s, "File") == 0;

    Item file = js_new_object();
    prop_set(file, "__class_name__", make_str("File"));

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

    Item ctor = prop_get(js_get_global_this(), "File");
    if (get_type_id(ctor) != LMD_TYPE_UNDEFINED) prop_set(file, "constructor", ctor);
    return file;
}

// Create a File stub for an empty file input.
static Item fd_make_file_stub() {
    Item obj = js_new_object();
    prop_set(obj, "__class_name__", make_str("File"));
    prop_set(obj, "size",           make_int_item(0));
    prop_set(obj, "name",           make_str(""));
    prop_set(obj, "type",           make_str("application/octet-stream"));
    prop_set(obj, "lastModified",   make_int_item(now_epoch_ms()));
    Item ctor = prop_get(js_get_global_this(), "File");
    if (get_type_id(ctor) != LMD_TYPE_UNDEFINED) prop_set(obj, "constructor", ctor);
    return obj;
}

// Constructor: new FormData([form])
static Item js_formdata_construct(Item first) {
    // Per WebIDL: new FormData() is fine; new FormData(nonFormElement) throws TypeError.
    // undefined is treated the same as no argument (the optional argument is absent).
    TypeId ft = get_type_id(first);
    if (ft != LMD_TYPE_UNDEFINED) {
        // null, string, number, non-form MAP → TypeError
        bool is_form_elem = false;

        if (ft == LMD_TYPE_MAP) {
            // Check if it is a DOM element whose tag is "form"
            // js_dom_unwrap_element returns void* = DomNode*; must check is_element() first
            void* node_raw = js_dom_unwrap_element(first);
            DomNode* node = (DomNode*)node_raw;
            if (node && node->is_element()) {
                DomElement* elem = (DomElement*)node;
                if (elem->tag_name && strcasecmp(elem->tag_name, "form") == 0) {
                    is_form_elem = true;
                }
            }
        }

        if (!is_form_elem) {
            return js_throw_type_error("FormData argument must be an HTMLFormElement");
        }
    }

    // Create the FormData object
    Item fd_obj = js_new_object();
    Item entries = js_array_new(0);
    prop_set(fd_obj, FD_ENTRIES_KEY, entries);
    fd_install_methods(fd_obj);

    // F-1: populate from form controls when form element was provided
    if (get_type_id(first) != LMD_TYPE_UNDEFINED) {
        void* node_raw = js_dom_unwrap_element(first);
        DomNode* node = (DomNode*)node_raw;
        if (node && node->is_element()) {
            DomElement* form_elem = (DomElement*)node;
            fd_walk_form_controls(entries, form_elem->first_child);
            log_debug("js_formdata_construct: populated from <form>, entries=%lld",
                      (long long)js_array_length(entries));
        }
    }

    log_debug("js_formdata_construct: created FormData");
    return fd_obj;
}

// ============================================================================
// Global installation
// ============================================================================

extern "C" void js_formdata_install_globals(void) {
    Item global = js_get_global_this();
    Item ctor_fn = js_new_function((void*)js_formdata_construct, 1);
    prop_set(global, "FormData", ctor_fn);

    // Install Blob constructor: new Blob(parts, options)
    Item blob_ctor_fn = js_new_function((void*)js_blob_construct, 2);
    prop_set(global, "Blob", blob_ctor_fn);

    // Install File constructor: new File(parts, name, options)
    Item file_ctor_fn = js_new_function((void*)js_file_construct, 3);
    prop_set(global, "File", file_ctor_fn);

    // Also install on window if it exists
    Item window = prop_get(global, "window");
    if (get_type_id(window) == LMD_TYPE_MAP) {
        prop_set(window, "FormData", ctor_fn);
        prop_set(window, "Blob", blob_ctor_fn);
        prop_set(window, "File", file_ctor_fn);
    }
    log_debug("js_formdata_install_globals: FormData, Blob, File installed on global");
}
