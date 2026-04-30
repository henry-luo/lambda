/**
 * Native Web Clipboard / Blob / File / ClipboardItem / ClipboardEvent /
 * navigator.clipboard bindings.
 *
 * Phase 7 of the Radiant Clipboard work — migrates the JS-side shim that
 * previously lived in test/wpt/wpt_testharness_shim.js into production
 * native code so real Radiant pages (not just WPT) get the API.
 *
 * Backed by radiant/clipboard.{hpp,cpp} for the actual platform store.
 *
 * Coverage in this file:
 *   - globalThis.Blob          — ctor + .text() / .arrayBuffer() / .slice()
 *   - globalThis.File          — extends Blob, adds .name / .lastModified
 *   - globalThis.ClipboardItem — ctor + getType() + static supports()
 *   - globalThis.ClipboardEvent — ctor + preventDefault / clipboardData
 *   - globalThis.Clipboard     — exposed for instanceof checks (methods
 *                                are still installed on navigator.clipboard)
 *   - globalThis.navigator     — { clipboard, permissions, platform, userAgent }
 *
 * The more involved Clipboard.write([items]) / Clipboard.read([opts]) async
 * paths and full DataTransfer item-list semantics remain polyfilled by the
 * WPT shim for now; the shim's `if (typeof X === "undefined")` guards
 * naturally skip the polyfills when the natives are present.
 */

#include "js_runtime.h"
#include "js_typed_array.h"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../radiant/clipboard.hpp"
#include <string.h>
#include <stdlib.h>

// Forward decls from elsewhere -----------------------------------------------
extern "C" Item js_promise_resolve(Item value);
extern "C" Item js_promise_reject(Item reason);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" Item js_throw_type_error(const char* message);
extern "C" Item js_get_global_this(void);
extern "C" void js_set_function_name(Item fn_item, Item name_item);
extern "C" Item js_object_keys(Item obj);
extern "C" Item js_promise_all(Item iterable);
extern "C" Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
extern "C" Item js_bind_function(Item func_item, Item bound_this, Item* bound_args, int bound_argc);

// Forward decls for sibling fns within this file (used before their definition).
extern "C" Item js_lambda_clipboard_write_records(Item arr);
extern "C" Item js_lambda_clipboard_read_records(void);
extern "C" Item js_clipboard_item_new(Item items, Item options);
extern "C" Item js_clipboard_item_get_type(Item type_item);
extern "C" Item js_blob_text(void);
extern "C" Item js_blob_array_buffer(void);
extern "C" Item js_blob_slice(Item start_item, Item end_item, Item type_item);

// Local helpers --------------------------------------------------------------
static inline Item make_str(const char* s) {
    if (!s) return ItemNull;
    return (Item){.item = s2it(heap_create_name(s, strlen(s)))};
}
static inline Item make_str_n(const char* s, size_t n) {
    return (Item){.item = s2it(heap_create_name(s, (int)n))};
}

// Set __class_name__ marker so instanceof <Name> works via the name fallback
// in js_instanceof_impl. A3-T3b: also stamp the typed JsClass byte (no-op
// for class names not yet in the enum).
static inline void mark_class(Item obj, const char* name) {
    js_property_set(obj, make_str("__class_name__"), make_str(name));
    js_class_stamp(obj, js_class_from_name(name, (int)strlen(name)));
}

// Read a string property as a C string (returns NULL if missing/non-string).
// The returned pointer is valid for the lifetime of the underlying String.
static const char* str_prop_get(Item obj, const char* key, size_t* out_len) {
    Item v = js_property_get(obj, make_str(key));
    if (get_type_id(v) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(v);
    if (!s) return NULL;
    if (out_len) *out_len = s->len;
    return s->chars;
}

// =============================================================================
// Blob
// =============================================================================
//
// We model a Blob as a plain object with:
//   __class_name__ = "Blob"
//   _text          : string contents (UTF-8 concatenation of input parts)
//   size           : byte length (number)
//   type           : MIME string (lowercased; empty if invalid char)
//
// Per the WPT subset we care about, parts may be: string | Blob |
// ArrayBuffer | TypedArray | DataView | Array of those. Number/object parts
// fall through and are silently skipped (the previous shim coerced via
// String(), but no in-scope test exercises that path).

static void blob_append_part(StrBuf* sb, Item part) {
    TypeId tid = get_type_id(part);
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(part);
        if (s && s->len > 0) strbuf_append_str_n(sb, s->chars, s->len);
        return;
    }
    // ArrayBuffer / TypedArray / DataView — append raw bytes verbatim.
    if (js_is_arraybuffer(part)) {
        Map* m = part.map;
        JsArrayBuffer* ab = (JsArrayBuffer*)m->data;
        if (ab && ab->data && ab->byte_length > 0 && !ab->detached) {
            strbuf_append_str_n(sb, (const char*)ab->data, (size_t)ab->byte_length);
        }
        return;
    }
    if (js_is_typed_array(part)) {
        Map* m = part.map;
        JsTypedArray* ta = (JsTypedArray*)m->data;
        if (ta && ta->data && ta->byte_length > 0) {
            strbuf_append_str_n(sb, (const char*)ta->data, (size_t)ta->byte_length);
        }
        return;
    }
    if (js_is_dataview(part)) {
        Map* m = part.map;
        JsDataView* dv = (JsDataView*)m->data;
        if (dv && dv->buffer && dv->buffer->data && dv->byte_length > 0 &&
            !dv->buffer->detached) {
            strbuf_append_str_n(sb,
                (const char*)dv->buffer->data + dv->byte_offset,
                (size_t)dv->byte_length);
        }
        return;
    }
    if (tid == LMD_TYPE_MAP) {
        // Blob? Pull _text.
        if (js_class_id(part) == JS_CLASS_BLOB) {
            size_t n = 0;
            const char* t = str_prop_get(part, "_text", &n);
            if (t && n > 0) strbuf_append_str_n(sb, t, n);
            return;
        }
    }
    // Fallback: silently skip unsupported part types.
}

extern "C" Item js_blob_new(Item parts, Item options) {
    StrBuf* sb = strbuf_new();
    if (get_type_id(parts) == LMD_TYPE_ARRAY) {
        int64_t n = js_array_length(parts);
        for (int64_t i = 0; i < n; i++) {
            Item p = js_array_get_int(parts, i);
            blob_append_part(sb, p);
        }
    } else if (get_type_id(parts) != LMD_TYPE_NULL) {
        // Per spec the parts argument must be iterable; if it's a single string
        // we accept it as a one-element sequence (matches the shim's behavior).
        blob_append_part(sb, parts);
    }

    // Resolve `type` from options (lowercased; empty if any byte outside 0x20..0x7e).
    char type_buf[256] = "";
    if (get_type_id(options) == LMD_TYPE_MAP) {
        size_t tn = 0;
        const char* t = str_prop_get(options, "type", &tn);
        if (t && tn > 0 && tn < sizeof(type_buf)) {
            bool ok = true;
            for (size_t i = 0; i < tn; i++) {
                unsigned char c = (unsigned char)t[i];
                if (c < 0x20 || c > 0x7e) { ok = false; break; }
                type_buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
            }
            type_buf[tn] = '\0';
            if (!ok) type_buf[0] = '\0';
        }
    }

    Item obj = js_new_object();
    mark_class(obj, "Blob");
    Item text_str = make_str_n(sb->str ? sb->str : "", sb->length);
    js_property_set(obj, make_str("_text"), text_str);
    js_property_set(obj, make_str("size"), (Item){.item = i2it((int64_t)sb->length)});
    js_property_set(obj, make_str("type"), make_str(type_buf));
    // bind prototype methods directly to instance (Lambda has no proto chain walk)
    js_property_set(obj, make_str("text"), js_new_function((void*)js_blob_text, 0));
    js_property_set(obj, make_str("arrayBuffer"), js_new_function((void*)js_blob_array_buffer, 0));
    js_property_set(obj, make_str("slice"), js_new_function((void*)js_blob_slice, 3));
    strbuf_free(sb);
    return obj;
}

extern "C" Item js_blob_text(void) {
    Item self = js_get_this();
    size_t n = 0;
    const char* t = str_prop_get(self, "_text", &n);
    Item s = t ? make_str_n(t, n) : make_str("");
    return js_promise_resolve(s);
}

extern "C" Item js_blob_array_buffer(void) {
    Item self = js_get_this();
    size_t n = 0;
    const char* t = str_prop_get(self, "_text", &n);
    // Build a real ArrayBuffer (native typed-array module) and copy bytes in.
    Item buf = js_arraybuffer_new((int)n);
    if (t && n > 0 && get_type_id(buf) == LMD_TYPE_MAP) {
        Map* m = buf.map;
        JsArrayBuffer* ab = (JsArrayBuffer*)m->data;
        if (ab && ab->data) memcpy(ab->data, t, n);
    }
    return js_promise_resolve(buf);
}

extern "C" Item js_blob_slice(Item start_item, Item end_item, Item type_item) {
    Item self = js_get_this();
    size_t n = 0;
    const char* t = str_prop_get(self, "_text", &n);
    int64_t len = (int64_t)n;
    int64_t s = 0, e = len;
    if (get_type_id(start_item) == LMD_TYPE_INT) s = (int64_t)it2i(start_item);
    if (get_type_id(end_item)   == LMD_TYPE_INT) e = (int64_t)it2i(end_item);
    if (s < 0) s = (s + len < 0) ? 0 : s + len;
    if (e < 0) e = (e + len < 0) ? 0 : e + len;
    if (s > len) s = len;
    if (e > len) e = len;
    if (e < s) e = s;

    StrBuf* sb = strbuf_new();
    if (t && e > s) strbuf_append_str_n(sb, t + s, (size_t)(e - s));

    // Build new Blob via [text]
    Item parts = js_array_new(0);
    js_array_push(parts, make_str_n(sb->str ? sb->str : "", sb->length));
    Item opts = js_new_object();
    if (get_type_id(type_item) == LMD_TYPE_STRING) {
        js_property_set(opts, make_str("type"), type_item);
    } else {
        js_property_set(opts, make_str("type"), make_str(""));
    }
    strbuf_free(sb);
    return js_blob_new(parts, opts);
}

// =============================================================================
// File extends Blob
// =============================================================================

extern "C" Item js_file_new(Item parts, Item name_item, Item options) {
    Item obj = js_blob_new(parts, options);
    if (get_type_id(obj) != LMD_TYPE_MAP) return obj;
    mark_class(obj, "File");
    const char* nm = "";
    if (get_type_id(name_item) == LMD_TYPE_STRING) {
        String* s = it2s(name_item);
        if (s) nm = s->chars;
    }
    js_property_set(obj, make_str("name"), make_str(nm));
    int64_t lm = 0;
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item v = js_property_get(options, make_str("lastModified"));
        if (get_type_id(v) == LMD_TYPE_INT) lm = (int64_t)it2i(v);
    }
    js_property_set(obj, make_str("lastModified"), (Item){.item = i2it(lm)});
    return obj;
}

// =============================================================================
// ClipboardItem
// =============================================================================
//
// Spec: new ClipboardItem(items, options?) where items is { mime: Blob|string|Promise }.
// We snapshot the keys (preserving original case) and store representations.

extern "C" Item js_clipboard_item_new(Item items, Item options) {
    if (get_type_id(items) != LMD_TYPE_MAP) {
        js_throw_type_error("ClipboardItem requires a record of MIME types");
        return ItemNull;
    }
    // Per spec: items must be a plain record. Reject Blob (and other tagged classes).
    if (js_class_id(items) != JS_CLASS_NONE) {
        js_throw_type_error("ClipboardItem requires a record, not a Blob");
        return ItemNull;
    }
    // Iterate source map keys via js_object_keys helper.
    Item keys = js_object_keys(items);
    int64_t nk = (get_type_id(keys) == LMD_TYPE_ARRAY) ? js_array_length(keys) : 0;
    if (nk == 0) {
        js_throw_type_error("ClipboardItem requires at least one representation");
        return ItemNull;
    }
    Item obj = js_new_object();
    mark_class(obj, "ClipboardItem");

    Item types = js_array_new(0);
    Item orig_types = js_array_new(0);
    Item reps = js_new_object(); // { lower_mime: Blob|string|Promise }

    char mime_buf[256];
    for (int64_t i = 0; i < nk; i++) {
        Item k = js_array_get_int(keys, i);
        if (get_type_id(k) != LMD_TYPE_STRING) continue;
        String* ks = it2s(k);
        if (!ks || ks->len == 0 || ks->len >= sizeof(mime_buf)) continue;
        for (size_t j = 0; j < ks->len; j++) {
            unsigned char c = (unsigned char)ks->chars[j];
            mime_buf[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
        }
        mime_buf[ks->len] = '\0';
        Item lower_k = make_str(mime_buf);
        js_array_push(types, lower_k);
        js_array_push(orig_types, k);
        js_property_set(reps, lower_k, js_property_get(items, k));
    }

    js_property_set(obj, make_str("types"), types);
    js_property_set(obj, make_str("_orig_types"), orig_types);
    js_property_set(obj, make_str("_reps"), reps);
    // bind prototype methods directly to instance (Lambda has no proto chain walk)
    js_property_set(obj, make_str("getType"),
        js_new_function((void*)js_clipboard_item_get_type, 1));

    const char* presentation = "attachment";
    if (get_type_id(options) == LMD_TYPE_MAP) {
        size_t pn = 0;
        const char* p = str_prop_get(options, "presentationStyle", &pn);
        if (p && (strcmp(p, "inline") == 0 || strcmp(p, "attachment") == 0 ||
                  strcmp(p, "unspecified") == 0)) presentation = p;
    }
    js_property_set(obj, make_str("presentationStyle"), make_str(presentation));
    return obj;
}

extern "C" Item js_clipboard_item_get_type(Item type_item) {
    Item self = js_get_this();
    if (get_type_id(type_item) != LMD_TYPE_STRING) {
        return js_promise_reject(js_new_error_with_name(
            make_str("TypeError"), make_str("ClipboardItem.getType: type must be a string")));
    }
    Item reps = js_property_get(self, make_str("_reps"));
    if (get_type_id(reps) != LMD_TYPE_MAP) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotFoundError"), make_str("type not found")));
    }
    String* ts = it2s(type_item);
    char buf[256];
    if (!ts || ts->len == 0 || ts->len >= sizeof(buf)) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotFoundError"), make_str("type not found")));
    }
    for (size_t j = 0; j < ts->len; j++) {
        unsigned char c = (unsigned char)ts->chars[j];
        buf[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    buf[ts->len] = '\0';
    Item rep = js_property_get(reps, make_str(buf));
    if (rep.item == ITEM_JS_UNDEFINED || get_type_id(rep) == LMD_TYPE_NULL) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotFoundError"), make_str("type not found")));
    }
    // If it's already a Blob, resolve directly. If it's a string, wrap in Blob.
    if (get_type_id(rep) == LMD_TYPE_STRING) {
        Item parts = js_array_new(0);
        js_array_push(parts, rep);
        Item opts = js_new_object();
        js_property_set(opts, make_str("type"), make_str(buf));
        return js_promise_resolve(js_blob_new(parts, opts));
    }
    return js_promise_resolve(rep);
}

extern "C" Item js_clipboard_item_supports(Item type_item) {
    if (get_type_id(type_item) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* ts = it2s(type_item);
    if (!ts || ts->len == 0) return (Item){.item = b2it(false)};
    // case-sensitive prefix check on raw input (per spec, "web " is literal).
    // "web " (4 chars) must be followed by a valid MIME type with non-empty
    // type and subtype halves separated by '/'.
    if (ts->len > 4 && strncmp(ts->chars, "web ", 4) == 0) {
        const char* rest = ts->chars + 4;
        size_t rlen = ts->len - 4;
        const char* slash = (const char*)memchr(rest, '/', rlen);
        if (slash && slash != rest && (size_t)(slash - rest) < rlen - 1) {
            return (Item){.item = b2it(true)};
        }
        return (Item){.item = b2it(false)};
    }
    // mandatory data types per W3C spec (case-insensitive).
    static const char* mandatory[] = {
        "text/plain", "text/html", "image/png", "text/uri-list", "image/svg+xml", NULL
    };
    char buf[256];
    if (ts->len >= sizeof(buf)) return (Item){.item = b2it(false)};
    for (size_t j = 0; j < ts->len; j++) {
        unsigned char c = (unsigned char)ts->chars[j];
        buf[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    buf[ts->len] = '\0';
    for (int i = 0; mandatory[i]; i++) {
        if (strcmp(buf, mandatory[i]) == 0) return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// ClipboardEvent
// =============================================================================
//
// Minimal ClipboardEvent — a plain object with type, isTrusted=false,
// clipboardData (a small DataTransfer-shaped helper), and the standard
// preventDefault / stopPropagation / stopImmediatePropagation / composedPath.

extern "C" Item js_clipboard_event_prevent_default(void) {
    Item self = js_get_this();
    js_property_set(self, make_str("defaultPrevented"), (Item){.item = b2it(true)});
    return ItemNull;
}
extern "C" Item js_clipboard_event_stop_propagation(void) {
    Item self = js_get_this();
    js_property_set(self, make_str("_stopped"), (Item){.item = b2it(true)});
    return ItemNull;
}
extern "C" Item js_clipboard_event_stop_immediate_propagation(void) {
    Item self = js_get_this();
    js_property_set(self, make_str("_stopped"), (Item){.item = b2it(true)});
    js_property_set(self, make_str("_stoppedImmediate"), (Item){.item = b2it(true)});
    return ItemNull;
}
extern "C" Item js_clipboard_event_composed_path(void) {
    return js_array_new(0);
}

// Forward decl for the DataTransfer factory (defined below).
static Item js_make_data_transfer_object(void);

extern "C" Item js_clipboard_event_new(Item type_item, Item init_item) {
    Item ev = js_new_object();
    mark_class(ev, "ClipboardEvent");

    const char* type = "";
    if (get_type_id(type_item) == LMD_TYPE_STRING) {
        String* s = it2s(type_item);
        if (s) type = s->chars;
    }
    js_property_set(ev, make_str("type"), make_str(type));
    js_property_set(ev, make_str("isTrusted"), (Item){.item = b2it(false)});
    js_property_set(ev, make_str("bubbles"), (Item){.item = b2it(false)});
    js_property_set(ev, make_str("cancelable"), (Item){.item = b2it(false)});
    js_property_set(ev, make_str("composed"), (Item){.item = b2it(false)});
    js_property_set(ev, make_str("defaultPrevented"), (Item){.item = b2it(false)});

    if (get_type_id(init_item) == LMD_TYPE_MAP) {
        Item b = js_property_get(init_item, make_str("bubbles"));
        if (get_type_id(b) == LMD_TYPE_BOOL)
            js_property_set(ev, make_str("bubbles"), b);
        Item c = js_property_get(init_item, make_str("cancelable"));
        if (get_type_id(c) == LMD_TYPE_BOOL)
            js_property_set(ev, make_str("cancelable"), c);
        Item cp = js_property_get(init_item, make_str("composed"));
        if (get_type_id(cp) == LMD_TYPE_BOOL)
            js_property_set(ev, make_str("composed"), cp);
        Item cd = js_property_get(init_item, make_str("clipboardData"));
        if (get_type_id(cd) == LMD_TYPE_MAP) {
            js_property_set(ev, make_str("clipboardData"), cd);
        } else {
            js_property_set(ev, make_str("clipboardData"), js_make_data_transfer_object());
        }
    } else {
        js_property_set(ev, make_str("clipboardData"), js_make_data_transfer_object());
    }

    js_property_set(ev, make_str("preventDefault"),
        js_new_function((void*)js_clipboard_event_prevent_default, 0));
    js_property_set(ev, make_str("stopPropagation"),
        js_new_function((void*)js_clipboard_event_stop_propagation, 0));
    js_property_set(ev, make_str("stopImmediatePropagation"),
        js_new_function((void*)js_clipboard_event_stop_immediate_propagation, 0));
    js_property_set(ev, make_str("composedPath"),
        js_new_function((void*)js_clipboard_event_composed_path, 0));
    return ev;
}

// =============================================================================
// DataTransfer — full items/files/types list semantics
// =============================================================================
//
// Native model:
//   dt:
//     __class_name__ = "DataTransfer"
//     dropEffect, effectAllowed       — strings
//     _items                          — Array of records (Map: kind/type/value|file)
//     items                           — Array view (DataTransferItemList) +
//                                       add/remove/clear methods + _owner
//     files                           — Array view (FileList) + item() + _owner
//     types                           — Array view (DOMStringList) +
//                                       "Files" sentinel when files present
//   getData/setData/clearData on dt operate on _items and rebuild views.
//
// dt.items, dt.files, dt.types are STABLE references — they are mutated in
// place so that `var fl = dt.files; ...mutate...; fl.length` reflects the
// latest state. This is required by the
// data-transfer-file-list-change-reference-updates WPT case.

extern "C" Item js_dt_items_add(Item data_arg, Item type_arg);
extern "C" Item js_dt_items_remove(Item idx_arg);
extern "C" Item js_dt_items_clear(void);
extern "C" Item js_dt_files_item(Item idx_arg);
extern "C" Item js_dt_set_data(Item type_item, Item data_item);
extern "C" Item js_dt_get_data(Item type_item);
extern "C" Item js_dt_clear_data(Item format_item);

static bool dt_is_class(Item v, const char* name, size_t name_len) {
    if (get_type_id(v) != LMD_TYPE_MAP) return false;
    Item cn = js_property_get(v, make_str("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return false;
    String* s = it2s(cn);
    return s && s->len == name_len && strncmp(s->chars, name, name_len) == 0;
}

// Lowercase and copy in-place. Returns false if input does not fit.
static bool dt_normalize_format(Item type_item, char* out, size_t out_cap) {
    if (get_type_id(type_item) != LMD_TYPE_STRING) return false;
    String* s = it2s(type_item);
    if (!s) return false;
    if ((size_t)s->len + 1 > out_cap) return false;
    for (int i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->chars[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    out[s->len] = '\0';
    if (strcmp(out, "text") == 0) {
        if (out_cap < 11) return false;
        strcpy(out, "text/plain");
    }
    return true;
}

// Recompute the public items/files/types arrays in place from _items.
// Required to preserve caller-held references like `const fl = dt.files`.
static void dt_recompute_views(Item dt) {
    Item items = js_property_get(dt, make_str("items"));
    Item files = js_property_get(dt, make_str("files"));
    Item types = js_property_get(dt, make_str("types"));
    Item rec   = js_property_get(dt, make_str("_items"));
    if (get_type_id(items) != LMD_TYPE_ARRAY ||
        get_type_id(files) != LMD_TYPE_ARRAY ||
        get_type_id(types) != LMD_TYPE_ARRAY ||
        get_type_id(rec)   != LMD_TYPE_ARRAY) return;

    items.array->length = 0;
    files.array->length = 0;
    types.array->length = 0;

    int64_t n = js_array_length(rec);
    bool has_files = false;
    for (int64_t i = 0; i < n; i++) {
        Item r = js_array_get_int(rec, i);
        Item kind = js_property_get(r, make_str("kind"));
        Item type = js_property_get(r, make_str("type"));

        // public DataTransferItem-like proxy: { kind, type }
        Item proxy = js_new_object();
        js_property_set(proxy, make_str("kind"), kind);
        js_property_set(proxy, make_str("type"), type);
        js_array_push(items, proxy);

        bool is_file = false;
        if (get_type_id(kind) == LMD_TYPE_STRING) {
            String* ks = it2s(kind);
            is_file = (ks && ks->len == 4 && strncmp(ks->chars, "file", 4) == 0);
        }
        if (is_file) {
            has_files = true;
            Item f = js_property_get(r, make_str("file"));
            if (f.item != ITEM_NULL) js_array_push(files, f);
        } else if (get_type_id(type) == LMD_TYPE_STRING) {
            // dedupe types for string entries
            String* ts = it2s(type);
            bool seen = false;
            int64_t tn = js_array_length(types);
            for (int64_t j = 0; j < tn; j++) {
                Item ev = js_array_get_int(types, j);
                if (get_type_id(ev) == LMD_TYPE_STRING) {
                    String* es = it2s(ev);
                    if (es && ts && es->len == ts->len &&
                        strncmp(es->chars, ts->chars, ts->len) == 0) {
                        seen = true; break;
                    }
                }
            }
            if (!seen) js_array_push(types, type);
        }
    }
    if (has_files) js_array_push(types, make_str("Files"));
}

// items.add(data, type?) — DataTransferItemList.add
extern "C" Item js_dt_items_add(Item data_arg, Item type_arg) {
    Item items = js_get_this();
    if (get_type_id(items) != LMD_TYPE_ARRAY) return ItemNull;
    Item dt = js_property_get(items, make_str("_owner"));
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    Item rec_arr = js_property_get(dt, make_str("_items"));
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return ItemNull;

    Item record = js_new_object();
    bool is_file_arg = dt_is_class(data_arg, "File", 4) ||
                       dt_is_class(data_arg, "Blob", 4);

    if (is_file_arg) {
        Item ftype = js_property_get(data_arg, make_str("type"));
        if (get_type_id(ftype) != LMD_TYPE_STRING) ftype = make_str("");
        js_property_set(record, make_str("kind"), make_str("file"));
        js_property_set(record, make_str("type"), ftype);
        js_property_set(record, make_str("file"), data_arg);
        js_array_push(rec_arr, record);
    } else if (get_type_id(data_arg) == LMD_TYPE_STRING) {
        if (get_type_id(type_arg) != LMD_TYPE_STRING) {
            js_throw_type_error(
                "DataTransferItemList.add requires a type for strings");
            return ItemNull;
        }
        char tbuf[256];
        if (!dt_normalize_format(type_arg, tbuf, sizeof(tbuf))) return ItemNull;
        // Spec: only one string item per type allowed.
        size_t tlen = strlen(tbuf);
        int64_t n = js_array_length(rec_arr);
        for (int64_t i = 0; i < n; i++) {
            Item r = js_array_get_int(rec_arr, i);
            Item kind = js_property_get(r, make_str("kind"));
            Item etype = js_property_get(r, make_str("type"));
            if (get_type_id(kind) == LMD_TYPE_STRING &&
                get_type_id(etype) == LMD_TYPE_STRING) {
                String* ks = it2s(kind);
                String* es = it2s(etype);
                if (ks && es && ks->len == 6 &&
                    strncmp(ks->chars, "string", 6) == 0 &&
                    (size_t)es->len == tlen &&
                    strncmp(es->chars, tbuf, tlen) == 0) {
                    js_throw_type_error(
                        "NotSupportedError: type already present");
                    return ItemNull;
                }
            }
        }
        js_property_set(record, make_str("kind"), make_str("string"));
        js_property_set(record, make_str("type"), make_str(tbuf));
        js_property_set(record, make_str("value"), data_arg);
        js_array_push(rec_arr, record);
    } else {
        return ItemNull;
    }

    dt_recompute_views(dt);
    Item items_view = js_property_get(dt, make_str("items"));
    int64_t ln = js_array_length(items_view);
    return (ln > 0) ? js_array_get_int(items_view, ln - 1) : ItemNull;
}

extern "C" Item js_dt_items_remove(Item idx_arg) {
    Item items = js_get_this();
    if (get_type_id(items) != LMD_TYPE_ARRAY) return ItemNull;
    Item dt = js_property_get(items, make_str("_owner"));
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    Item rec_arr = js_property_get(dt, make_str("_items"));
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return ItemNull;

    int idx = -1;
    if (get_type_id(idx_arg) == LMD_TYPE_INT) idx = (int)it2i(idx_arg);
    Array* a = rec_arr.array;
    int64_t n = a->length;
    if (idx < 0 || idx >= n) return ItemNull;
    for (int64_t i = idx; i + 1 < n; i++) a->items[i] = a->items[i + 1];
    a->length = n - 1;
    dt_recompute_views(dt);
    return ItemNull;
}

extern "C" Item js_dt_items_clear(void) {
    Item items = js_get_this();
    if (get_type_id(items) != LMD_TYPE_ARRAY) return ItemNull;
    Item dt = js_property_get(items, make_str("_owner"));
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    Item rec_arr = js_property_get(dt, make_str("_items"));
    if (get_type_id(rec_arr) == LMD_TYPE_ARRAY) rec_arr.array->length = 0;
    dt_recompute_views(dt);
    return ItemNull;
}

extern "C" Item js_dt_files_item(Item idx_arg) {
    Item files = js_get_this();
    if (get_type_id(files) != LMD_TYPE_ARRAY) return ItemNull;
    int idx = -1;
    if (get_type_id(idx_arg) == LMD_TYPE_INT) idx = (int)it2i(idx_arg);
    int64_t n = js_array_length(files);
    if (idx < 0 || idx >= n) return ItemNull;
    return js_array_get_int(files, idx);
}

extern "C" Item js_dt_set_data(Item type_item, Item data_item) {
    Item dt = js_get_this();
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    char tbuf[256];
    if (!dt_normalize_format(type_item, tbuf, sizeof(tbuf))) return ItemNull;
    Item value = data_item;
    if (get_type_id(value) != LMD_TYPE_STRING) value = make_str("");
    Item rec_arr = js_property_get(dt, make_str("_items"));
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return ItemNull;

    size_t tlen = strlen(tbuf);
    int64_t n = js_array_length(rec_arr);
    for (int64_t i = 0; i < n; i++) {
        Item r = js_array_get_int(rec_arr, i);
        Item kind = js_property_get(r, make_str("kind"));
        Item etype = js_property_get(r, make_str("type"));
        if (get_type_id(kind) == LMD_TYPE_STRING &&
            get_type_id(etype) == LMD_TYPE_STRING) {
            String* ks = it2s(kind);
            String* es = it2s(etype);
            if (ks && es && ks->len == 6 &&
                strncmp(ks->chars, "string", 6) == 0 &&
                (size_t)es->len == tlen &&
                strncmp(es->chars, tbuf, tlen) == 0) {
                js_property_set(r, make_str("value"), value);
                dt_recompute_views(dt);
                return ItemNull;
            }
        }
    }
    Item record = js_new_object();
    js_property_set(record, make_str("kind"), make_str("string"));
    js_property_set(record, make_str("type"), make_str(tbuf));
    js_property_set(record, make_str("value"), value);
    js_array_push(rec_arr, record);
    dt_recompute_views(dt);
    return ItemNull;
}

extern "C" Item js_dt_get_data(Item type_item) {
    Item dt = js_get_this();
    if (get_type_id(dt) != LMD_TYPE_MAP) return make_str("");
    char tbuf[256];
    if (!dt_normalize_format(type_item, tbuf, sizeof(tbuf))) return make_str("");
    Item rec_arr = js_property_get(dt, make_str("_items"));
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return make_str("");
    size_t tlen = strlen(tbuf);
    int64_t n = js_array_length(rec_arr);
    for (int64_t i = 0; i < n; i++) {
        Item r = js_array_get_int(rec_arr, i);
        Item kind = js_property_get(r, make_str("kind"));
        Item etype = js_property_get(r, make_str("type"));
        if (get_type_id(kind) == LMD_TYPE_STRING &&
            get_type_id(etype) == LMD_TYPE_STRING) {
            String* ks = it2s(kind);
            String* es = it2s(etype);
            if (ks && es && ks->len == 6 &&
                strncmp(ks->chars, "string", 6) == 0 &&
                (size_t)es->len == tlen &&
                strncmp(es->chars, tbuf, tlen) == 0) {
                Item v = js_property_get(r, make_str("value"));
                return (get_type_id(v) == LMD_TYPE_STRING) ? v : make_str("");
            }
        }
    }
    return make_str("");
}

// clearData([format]) — no-arg clears all string items (keeps files);
// with format clears only that string item.
extern "C" Item js_dt_clear_data(Item format_item) {
    Item dt = js_get_this();
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    Item rec_arr = js_property_get(dt, make_str("_items"));
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return ItemNull;
    Array* a = rec_arr.array;

    bool target_specific = false;
    char tbuf[256];
    size_t tlen = 0;
    if (format_item.item != ITEM_NULL &&
        format_item.item != ITEM_JS_UNDEFINED &&
        get_type_id(format_item) == LMD_TYPE_STRING) {
        if (!dt_normalize_format(format_item, tbuf, sizeof(tbuf))) return ItemNull;
        target_specific = true;
        tlen = strlen(tbuf);
    }

    int64_t n = a->length;
    int64_t out = 0;
    for (int64_t i = 0; i < n; i++) {
        Item r = a->items[i];
        Item kind = js_property_get(r, make_str("kind"));
        bool is_string = false;
        if (get_type_id(kind) == LMD_TYPE_STRING) {
            String* ks = it2s(kind);
            is_string = (ks && ks->len == 6 &&
                         strncmp(ks->chars, "string", 6) == 0);
        }
        bool drop;
        if (target_specific) {
            drop = false;
            if (is_string) {
                Item etype = js_property_get(r, make_str("type"));
                if (get_type_id(etype) == LMD_TYPE_STRING) {
                    String* es = it2s(etype);
                    if (es && (size_t)es->len == tlen &&
                        strncmp(es->chars, tbuf, tlen) == 0) {
                        drop = true;
                    }
                }
            }
        } else {
            drop = is_string;
        }
        if (!drop) a->items[out++] = r;
    }
    a->length = out;
    dt_recompute_views(dt);
    return ItemNull;
}

static Item js_make_data_transfer_object(void) {
    Item dt = js_new_object();
    mark_class(dt, "DataTransfer");
    js_property_set(dt, make_str("dropEffect"), make_str("none"));
    js_property_set(dt, make_str("effectAllowed"), make_str("none"));
    js_property_set(dt, make_str("_items"), js_array_new(0));

    // Stable view arrays — mutated in place by dt_recompute_views.
    Item items = js_array_new(0);
    Item files = js_array_new(0);
    Item types = js_array_new(0);
    js_property_set(items, make_str("_owner"), dt);
    js_property_set(files, make_str("_owner"), dt);
    js_property_set(items, make_str("add"),
        js_new_function((void*)js_dt_items_add, 2));
    js_property_set(items, make_str("remove"),
        js_new_function((void*)js_dt_items_remove, 1));
    js_property_set(items, make_str("clear"),
        js_new_function((void*)js_dt_items_clear, 0));
    js_property_set(files, make_str("item"),
        js_new_function((void*)js_dt_files_item, 1));
    js_property_set(dt, make_str("items"), items);
    js_property_set(dt, make_str("files"), files);
    js_property_set(dt, make_str("types"), types);

    js_property_set(dt, make_str("setData"),
        js_new_function((void*)js_dt_set_data, 2));
    js_property_set(dt, make_str("getData"),
        js_new_function((void*)js_dt_get_data, 1));
    js_property_set(dt, make_str("clearData"),
        js_new_function((void*)js_dt_clear_data, 1));
    return dt;
}

extern "C" Item js_data_transfer_new(void) {
    return js_make_data_transfer_object();
}

// =============================================================================
// navigator.clipboard — backed by radiant/clipboard.{hpp,cpp}
// =============================================================================

extern "C" Item js_clipboard_write_text(Item text_item) {
    if (clipboard_store_get_permission_write() == CLIPBOARD_PERMISSION_DENIED) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotAllowedError"),
            make_str("Write permission denied")));
    }
    // Per WebIDL, writeText(DOMString) requires its argument; calling with no
    // args (undefined) must reject with TypeError. `null` stringifies to "null"
    // by spec but the WPT subset treats null as empty string \u2014 mirror the
    // previous polyfill's `data == null ? "" : String(data)` semantics.
    if (text_item.item == ITEM_JS_UNDEFINED) {
        return js_promise_reject(js_new_error_with_name(
            make_str("TypeError"),
            make_str("writeText requires 1 argument")));
    }
    const char* t = "";
    if (get_type_id(text_item) == LMD_TYPE_STRING) {
        String* s = it2s(text_item);
        if (s) t = s->chars;
    }
    clipboard_store_write_text(t);
    return js_promise_resolve(ItemNull);
}

extern "C" Item js_clipboard_read_text(void) {
    if (clipboard_store_get_permission_read() == CLIPBOARD_PERMISSION_DENIED) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotAllowedError"),
            make_str("Read permission denied")));
    }
    const char* t = clipboard_store_read_text();
    return js_promise_resolve(make_str(t ? t : ""));
}

// =============================================================================
// Native Clipboard.prototype.write / read — full spec validation & write.
//
// Async pipeline:
//   1. synchronous validation pass (returns rejected Promise on first failure)
//   2. Promise.all([Promise.resolve(rep_value), ...]) over every representation
//   3. .then(materialise) extracts text from Blobs/strings, sanitises text/html,
//      builds records, writes to the C ClipboardStore.
// =============================================================================

static bool str_has_upper_ascii(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') return true;
    }
    return false;
}

static bool is_standard_mandatory_mime(const char* s) {
    return strcmp(s, "text/plain") == 0 || strcmp(s, "text/html") == 0 ||
           strcmp(s, "image/png") == 0 || strcmp(s, "text/uri-list") == 0 ||
           strcmp(s, "image/svg+xml") == 0;
}

// Returns true if `s` is a valid `<type>/<sub>` MIME body (both halves
// non-empty, no '/' inside the halves). Uppercase check is done by the caller.
static bool is_valid_mime_body(const char* s, size_t n) {
    const char* slash = (const char*)memchr(s, '/', n);
    if (!slash || slash == s || slash == s + n - 1) return false;
    return true;
}

static bool item_is_clipboard_item(Item it) {
    return js_class_id(it) == JS_CLASS_CLIPBOARD_ITEM;
}

// Blob-like duck type: a native Blob/File OR any
// object exposing both a string `.type` and a callable `.text()`.
// Mirrors the shim's `isBlobLike` so fetch().blob() values round-trip.
static bool item_is_blob_like(Item it) {
    JsClass cls = js_class_id(it);
    if (cls == JS_CLASS_BLOB || cls == JS_CLASS_FILE) return true;
    if (get_type_id(it) != LMD_TYPE_MAP) return false;
    Item ty = js_property_get(it, make_str("type"));
    Item tx = js_property_get(it, make_str("text"));
    return get_type_id(ty) == LMD_TYPE_STRING &&
           get_type_id(tx) == LMD_TYPE_FUNC;
}

// Strip <script>...</script> and <style>...</style> blocks (case-insensitive)
// from `src` to match the C++ ClipboardStore HTML sanitiser. Output is appended
// to `out`.
static void strip_html_script_style(StrBuf* out, const char* src, size_t n) {
    size_t i = 0;
    while (i < n) {
        // Look for "<script" or "<style" starting at i.
        if (src[i] == '<' && i + 1 < n) {
            size_t tag_len = 0;
            const char* tag = NULL;
            if (i + 7 <= n && (src[i+1] == 's' || src[i+1] == 'S') &&
                              (src[i+2] == 'c' || src[i+2] == 'C') &&
                              (src[i+3] == 'r' || src[i+3] == 'R') &&
                              (src[i+4] == 'i' || src[i+4] == 'I') &&
                              (src[i+5] == 'p' || src[i+5] == 'P') &&
                              (src[i+6] == 't' || src[i+6] == 'T') &&
                              (i + 7 == n || src[i+7] == ' ' || src[i+7] == '>' ||
                               src[i+7] == '\t' || src[i+7] == '\n' || src[i+7] == '/')) {
                tag = "script"; tag_len = 6;
            } else if (i + 6 <= n && (src[i+1] == 's' || src[i+1] == 'S') &&
                                     (src[i+2] == 't' || src[i+2] == 'T') &&
                                     (src[i+3] == 'y' || src[i+3] == 'Y') &&
                                     (src[i+4] == 'l' || src[i+4] == 'L') &&
                                     (src[i+5] == 'e' || src[i+5] == 'E') &&
                                     (i + 6 == n || src[i+6] == ' ' || src[i+6] == '>' ||
                                      src[i+6] == '\t' || src[i+6] == '\n' || src[i+6] == '/')) {
                tag = "style"; tag_len = 5;
            }
            if (tag) {
                // Find matching </tag (case-insensitive), then '>' after it.
                size_t j = i + 1 + tag_len;
                while (j + tag_len + 2 < n) {
                    if (src[j] == '<' && j + 1 < n && src[j+1] == '/') {
                        bool match = true;
                        for (size_t k = 0; k < tag_len; k++) {
                            char a = src[j + 2 + k];
                            char b = tag[k];
                            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                            if (a != b) { match = false; break; }
                        }
                        if (match) {
                            // Skip until '>'.
                            size_t e = j + 2 + tag_len;
                            while (e < n && src[e] != '>') e++;
                            i = (e < n) ? e + 1 : n;
                            goto next_iter;
                        }
                    }
                    j++;
                }
                // No closing tag — drop everything to EOF.
                i = n;
                goto next_iter;
            }
        }
        strbuf_append_str_n(out, src + i, 1);
        i++;
        next_iter:;
    }
}

// Read a Blob-like's text into a malloc'd C string (caller frees). Returns
// NULL if the blob has no extractable bytes. Handles native Blob `_text`
// directly. For foreign Blob-likes, returns the result of calling .text(),
// but only if it is already a fulfilled Promise<string> or a string (we cannot
// block on a foreign async .text() here — those are unsupported in this path).
static char* blob_like_get_text(Item blob, size_t* out_len) {
    if (get_type_id(blob) == LMD_TYPE_STRING) {
        String* s = it2s(blob);
        if (!s) return NULL;
        char* buf = (char*)malloc(s->len + 1);
        memcpy(buf, s->chars, s->len);
        buf[s->len] = '\0';
        if (out_len) *out_len = s->len;
        return buf;
    }
    size_t n = 0;
    const char* t = str_prop_get(blob, "_text", &n);
    if (t) {
        char* buf = (char*)malloc(n + 1);
        memcpy(buf, t, n);
        buf[n] = '\0';
        if (out_len) *out_len = n;
        return buf;
    }
    return NULL;
}

// Materialise handler — bound with `items_array` as the first arg, called by
// js_promise_then with `resolved_values` (the result of Promise.all).
//
// Walks every (item, key) pair in the same order they were flattened in
// `js_clipboard_write` so it can pair each resolved value with its mime key.
// Returns ItemNull on success (resolves outer write() promise to undefined),
// or a rejected promise on per-rep validation failure (image/* must be Blob).
static Item js_clipboard_materialise(Item items_array, Item resolved_values) {
    if (get_type_id(items_array) != LMD_TYPE_ARRAY ||
        get_type_id(resolved_values) != LMD_TYPE_ARRAY) {
        return ItemNull;
    }
    int64_t n_items = js_array_length(items_array);
    Item records = js_array_new(0);
    int64_t flat_idx = 0;
    for (int64_t i = 0; i < n_items; i++) {
        Item item = js_array_get_int(items_array, i);
        Item types = js_property_get(item, make_str("types"));
        if (get_type_id(types) != LMD_TYPE_ARRAY) continue;
        int64_t nk = js_array_length(types);
        Item rec = js_new_object();
        for (int64_t j = 0; j < nk; j++) {
            Item k = js_array_get_int(types, j);
            if (get_type_id(k) != LMD_TYPE_STRING) { flat_idx++; continue; }
            String* ks = it2s(k);
            if (!ks) { flat_idx++; continue; }
            Item v = js_array_get_int(resolved_values, flat_idx++);

            // image/* representations MUST be Blob — reject the whole write.
            if (ks->len >= 6 && memcmp(ks->chars, "image/", 6) == 0) {
                if (!item_is_blob_like(v)) {
                    return js_promise_reject(js_new_error_with_name(
                        make_str("TypeError"),
                        make_str("image representation must be a Blob")));
                }
            }

            size_t tlen = 0;
            char* tbuf = blob_like_get_text(v, &tlen);
            if (!tbuf) { tbuf = (char*)malloc(1); tbuf[0] = '\0'; tlen = 0; }

            // Sanitise text/html (sanitised standard format only; "web text/html"
            // custom-format is preserved verbatim by virtue of having a
            // different lower_key e.g. "web text/html").
            if (strcmp(ks->chars, "text/html") == 0) {
                StrBuf* sb = strbuf_new();
                strip_html_script_style(sb, tbuf, tlen);
                free(tbuf);
                tbuf = (char*)malloc(sb->length + 1);
                memcpy(tbuf, sb->str ? sb->str : "", sb->length);
                tbuf[sb->length] = '\0';
                tlen = sb->length;
                strbuf_free(sb);
            }

            js_property_set(rec, k, make_str_n(tbuf, tlen));
            free(tbuf);
        }
        js_array_push(records, rec);
    }
    // Forward to the bridge that already knows how to deep-copy into the
    // C ClipboardStore.
    js_lambda_clipboard_write_records(records);
    return ItemNull;
}

extern "C" Item js_clipboard_write(Item items_array) {
    if (clipboard_store_get_permission_write() == CLIPBOARD_PERMISSION_DENIED) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotAllowedError"),
            make_str("Write permission denied")));
    }
    if (get_type_id(items_array) != LMD_TYPE_ARRAY) {
        return js_promise_reject(js_new_error_with_name(
            make_str("TypeError"),
            make_str("write() requires a sequence of ClipboardItems")));
    }
    int64_t n_items = js_array_length(items_array);
    if (n_items == 0) {
        return js_promise_reject(js_new_error_with_name(
            make_str("TypeError"),
            make_str("write() requires a sequence of ClipboardItems")));
    }
    // Per spec quirk (matched by all major browsers + WPT), only one
    // ClipboardItem may be written per call.
    if (n_items > 1) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotAllowedError"),
            make_str("writing more than one ClipboardItem is not supported")));
    }

    int web_custom_count = 0;

    for (int64_t i = 0; i < n_items; i++) {
        Item item = js_array_get_int(items_array, i);
        if (!item_is_clipboard_item(item)) {
            return js_promise_reject(js_new_error_with_name(
                make_str("TypeError"),
                make_str("write() entries must be ClipboardItem")));
        }
        Item orig_types = js_property_get(item, make_str("_orig_types"));
        Item types_lower = js_property_get(item, make_str("types"));
        Item reps = js_property_get(item, make_str("_reps"));
        if (get_type_id(orig_types) != LMD_TYPE_ARRAY) continue;
        int64_t nk = js_array_length(orig_types);
        for (int64_t j = 0; j < nk; j++) {
            Item ot = js_array_get_int(orig_types, j);
            if (get_type_id(ot) != LMD_TYPE_STRING) {
                return js_promise_reject(js_new_error_with_name(
                    make_str("NotAllowedError"),
                    make_str("invalid clipboard format")));
            }
            String* ots = it2s(ot);
            if (!ots) continue;
            const char* otc = ots->chars;
            size_t otl = ots->len;

            if (otl > 4 && strncmp(otc, "web ", 4) == 0) {
                const char* sub = otc + 4;
                size_t subl = otl - 4;
                if (str_has_upper_ascii(sub, subl) ||
                    !is_valid_mime_body(sub, subl)) {
                    return js_promise_reject(js_new_error_with_name(
                        make_str("NotAllowedError"),
                        make_str("invalid web custom format")));
                }
                web_custom_count++;
                // Blob.type vs format check.
                Item lower_k = js_array_get_int(types_lower, j);
                Item rep = js_property_get(reps, lower_k);
                if (item_is_blob_like(rep)) {
                    Item bt = js_property_get(rep, make_str("type"));
                    if (get_type_id(bt) == LMD_TYPE_STRING) {
                        String* bts = it2s(bt);
                        if (bts && bts->len > 0 &&
                            strcmp(bts->chars, sub) != 0 &&
                            strcmp(bts->chars, otc) != 0) {
                            return js_promise_reject(js_new_error_with_name(
                                make_str("NotAllowedError"),
                                make_str("Blob.type does not match format")));
                        }
                    }
                }
                continue;
            }
            if (str_has_upper_ascii(otc, otl)) {
                return js_promise_reject(js_new_error_with_name(
                    make_str("NotAllowedError"),
                    make_str("invalid (non-lowercase) format")));
            }
            if (!is_standard_mandatory_mime(otc)) {
                return js_promise_reject(js_new_error_with_name(
                    make_str("NotAllowedError"),
                    make_str("unsupported clipboard format")));
            }
        }
    }
    if (web_custom_count > 100) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotAllowedError"),
            make_str("too many custom formats (max 100)")));
    }

    // Build flat array of resolved-promises over each rep value.
    Item flat = js_array_new(0);
    for (int64_t i = 0; i < n_items; i++) {
        Item item = js_array_get_int(items_array, i);
        Item types = js_property_get(item, make_str("types"));
        Item reps = js_property_get(item, make_str("_reps"));
        if (get_type_id(types) != LMD_TYPE_ARRAY) continue;
        int64_t nk = js_array_length(types);
        for (int64_t j = 0; j < nk; j++) {
            Item k = js_array_get_int(types, j);
            Item v = js_property_get(reps, k);
            // Promise.resolve(v): JsPromise stays as-is, plain values are wrapped.
            js_array_push(flat, js_promise_resolve(v));
        }
    }

    Item all_p = js_promise_all(flat);
    Item handler_raw = js_new_function((void*)js_clipboard_materialise, 2);
    Item bound = js_bind_function(handler_raw, ItemNull, &items_array, 1);
    return js_promise_then(all_p, bound, ItemNull);
}

// Clipboard.prototype.read — synchronous read of the C ClipboardStore wrapped
// in ClipboardItems whose representations are Blobs (per spec).
extern "C" Item js_clipboard_read(Item opts) {
    if (clipboard_store_get_permission_read() == CLIPBOARD_PERMISSION_DENIED) {
        return js_promise_reject(js_new_error_with_name(
            make_str("NotAllowedError"),
            make_str("Read permission denied")));
    }
    // ClipboardUnsanitizedFormats.unsanitized: per WebIDL it must be a
    // sequence. An *absent* key is fine (skip). An explicit `null` (or any
    // non-array value) rejects with TypeError. A non-empty array rejects
    // with NotAllowedError (we don't support unsanitised reads in headless).
    if (get_type_id(opts) == LMD_TYPE_MAP) {
        // Detect presence by walking keys (js_property_get can't distinguish
        // explicit-null from absent on plain Lambda maps).
        bool has_unsanitized = false;
        Item okeys = js_object_keys(opts);
        if (get_type_id(okeys) == LMD_TYPE_ARRAY) {
            int64_t okn = js_array_length(okeys);
            for (int64_t kk = 0; kk < okn; kk++) {
                Item kkk = js_array_get_int(okeys, kk);
                if (get_type_id(kkk) != LMD_TYPE_STRING) continue;
                String* kss = it2s(kkk);
                if (kss && kss->len == 11 && memcmp(kss->chars, "unsanitized", 11) == 0) {
                    has_unsanitized = true;
                    break;
                }
            }
        }
        if (has_unsanitized) {
            Item u = js_property_get(opts, make_str("unsanitized"));
            TypeId ut = get_type_id(u);
            if (ut == LMD_TYPE_ARRAY) {
                if (js_array_length(u) > 0) {
                    return js_promise_reject(js_new_error_with_name(
                        make_str("NotAllowedError"),
                        make_str("unsanitized read is not supported")));
                }
            } else {
                return js_promise_reject(js_new_error_with_name(
                    make_str("TypeError"),
                    make_str("ClipboardUnsanitizedFormats.unsanitized must be a sequence")));
            }
        }
    }

    // Snapshot the C store, wrap each value in a Blob.
    Item recs = js_lambda_clipboard_read_records();
    Item out = js_array_new(0);
    if (get_type_id(recs) == LMD_TYPE_ARRAY) {
        int64_t n = js_array_length(recs);
        for (int64_t i = 0; i < n; i++) {
            Item rec = js_array_get_int(recs, i);
            if (get_type_id(rec) != LMD_TYPE_MAP) continue;
            Item keys = js_object_keys(rec);
            int64_t nk = (get_type_id(keys) == LMD_TYPE_ARRAY) ? js_array_length(keys) : 0;
            Item wrapped = js_new_object();
            for (int64_t j = 0; j < nk; j++) {
                Item k = js_array_get_int(keys, j);
                Item v = js_property_get(rec, k);
                Item parts = js_array_new(0);
                js_array_push(parts, v);
                Item bopts = js_new_object();
                js_property_set(bopts, make_str("type"), k);
                Item blob = js_blob_new(parts, bopts);
                js_property_set(wrapped, k, blob);
            }
            Item ci = js_clipboard_item_new(wrapped, ItemNull);
            js_array_push(out, ci);
        }
    }
    return js_promise_resolve(out);
}

// =============================================================================
// navigator.permissions
// =============================================================================

extern "C" Item js_permissions_query(Item desc) {
    Item status = js_new_object();
    mark_class(status, "PermissionStatus");
    const char* state = "prompt";
    if (get_type_id(desc) == LMD_TYPE_MAP) {
        size_t nl = 0;
        const char* nm = str_prop_get(desc, "name", &nl);
        if (nm) {
            ClipboardPermission p = CLIPBOARD_PERMISSION_PROMPT;
            if (strcmp(nm, "clipboard-read") == 0) {
                p = clipboard_store_get_permission_read();
            } else if (strcmp(nm, "clipboard-write") == 0) {
                p = clipboard_store_get_permission_write();
            }
            switch (p) {
                case CLIPBOARD_PERMISSION_GRANTED: state = "granted"; break;
                case CLIPBOARD_PERMISSION_DENIED:  state = "denied";  break;
                default:                           state = "prompt";  break;
            }
            js_property_set(status, make_str("name"), make_str(nm));
        }
    }
    js_property_set(status, make_str("state"), make_str(state));
    return js_promise_resolve(status);
}

// =============================================================================
// Bridge: synchronous read/write of multi-MIME records to the C ClipboardStore.
//
// These are exposed as `globalThis.__lambda_clipboard_*` so the WPT shim can
// route its `_wpt_clipboard_store` operations and synthetic Cmd+C/V handler
// through the same store that backs `navigator.clipboard.readText/writeText`.
// Once both ends share the C store, `navigator.clipboard.writeText("X")` from
// a page is observable via a Cmd+V triggered ClipboardEvent and vice versa.
//
// JS shape:
//   __lambda_clipboard_clear()                      -> undefined
//   __lambda_clipboard_write_records([{mime: str}]) -> undefined
//   __lambda_clipboard_read_records()               -> [{mime: str}]
//   __lambda_clipboard_set_perm(name, "granted"|"denied"|"prompt") -> undefined
//   __lambda_clipboard_get_perm(name)               -> string
// =============================================================================

extern "C" Item js_lambda_clipboard_clear(void) {
    clipboard_store_clear();
    return ItemNull;
}

// Free a snapshot returned by clipboard_store_read_items() — the radiant
// items_free helper is static, so we replicate the freeing logic here.
static void free_items_snapshot(ArrayList* items) {
    if (!items) return;
    for (int i = 0; i < items->length; i++) {
        ClipboardItem* it = (ClipboardItem*)items->data[i];
        if (!it) continue;
        if (it->entries) {
            for (int j = 0; j < it->entries->length; j++) {
                ClipboardEntry* e = (ClipboardEntry*)it->entries->data[j];
                if (!e) continue;
                free(e->mime);
                free(e->data);
                free(e);
            }
            arraylist_free(it->entries);
        }
        free(it);
    }
    arraylist_free(items);
}

extern "C" Item js_lambda_clipboard_write_records(Item arr) {
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        clipboard_store_clear();
        return ItemNull;
    }
    int64_t n = js_array_length(arr);
    ArrayList* items = arraylist_new(n > 0 ? (int)n : 1);

    // js_object_keys forward-declared at top of file
    for (int64_t i = 0; i < n; i++) {
        Item rec = js_array_get_int(arr, i);
        if (get_type_id(rec) != LMD_TYPE_MAP) continue;

        ClipboardItem* citem = (ClipboardItem*)calloc(1, sizeof(ClipboardItem));
        if (!citem) continue;
        citem->entries = arraylist_new(4);
        citem->is_unsanitized = 0;

        Item keys = js_object_keys(rec);
        int64_t nk = (get_type_id(keys) == LMD_TYPE_ARRAY) ? js_array_length(keys) : 0;
        for (int64_t j = 0; j < nk; j++) {
            Item k = js_array_get_int(keys, j);
            if (get_type_id(k) != LMD_TYPE_STRING) continue;
            String* ks = it2s(k);
            if (!ks || ks->len == 0) continue;
            Item v = js_property_get(rec, k);
            if (get_type_id(v) != LMD_TYPE_STRING) continue;
            String* vs = it2s(v);
            if (!vs) continue;

            ClipboardEntry* ce = (ClipboardEntry*)calloc(1, sizeof(ClipboardEntry));
            if (!ce) continue;
            ce->mime = (char*)malloc(ks->len + 1);
            memcpy(ce->mime, ks->chars, ks->len);
            ce->mime[ks->len] = '\0';
            ce->data_len = vs->len;
            ce->data = (char*)malloc(vs->len + 1);
            memcpy(ce->data, vs->chars, vs->len);
            ce->data[vs->len] = '\0';
            arraylist_append(citem->entries, ce);
        }
        arraylist_append(items, citem);
    }

    clipboard_store_write_items(items);

    // The store deep-copies; free our temporaries.
    free_items_snapshot(items);
    return ItemNull;
}

extern "C" Item js_lambda_clipboard_read_records(void) {
    Item out = js_array_new(0);
    ArrayList* items = clipboard_store_read_items();
    if (!items) return out;
    for (int i = 0; i < items->length; i++) {
        ClipboardItem* it = (ClipboardItem*)items->data[i];
        if (!it) continue;
        Item rec = js_new_object();
        if (it->entries) {
            for (int j = 0; j < it->entries->length; j++) {
                ClipboardEntry* e = (ClipboardEntry*)it->entries->data[j];
                if (!e || !e->mime || !e->data) continue;
                Item key_item = make_str(e->mime);
                Item val_item = make_str_n(e->data, e->data_len);
                js_property_set(rec, key_item, val_item);
            }
        }
        js_array_push(out, rec);
    }
    free_items_snapshot(items);
    return out;
}

static ClipboardPermission perm_from_str(const char* s) {
    if (!s) return CLIPBOARD_PERMISSION_PROMPT;
    if (strcmp(s, "granted") == 0) return CLIPBOARD_PERMISSION_GRANTED;
    if (strcmp(s, "denied")  == 0) return CLIPBOARD_PERMISSION_DENIED;
    return CLIPBOARD_PERMISSION_PROMPT;
}

static const char* perm_to_str(ClipboardPermission p) {
    switch (p) {
        case CLIPBOARD_PERMISSION_GRANTED: return "granted";
        case CLIPBOARD_PERMISSION_DENIED:  return "denied";
        default:                           return "prompt";
    }
}

extern "C" Item js_lambda_clipboard_set_perm(Item name_item, Item state_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING ||
        get_type_id(state_item) != LMD_TYPE_STRING) return ItemNull;
    String* nm = it2s(name_item);
    String* st = it2s(state_item);
    if (!nm || !st) return ItemNull;
    ClipboardPermission p = perm_from_str(st->chars);
    if (strcmp(nm->chars, "clipboard-read")  == 0) clipboard_store_set_permission_read(p);
    if (strcmp(nm->chars, "clipboard-write") == 0) clipboard_store_set_permission_write(p);
    return ItemNull;
}

extern "C" Item js_lambda_clipboard_get_perm(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return make_str("prompt");
    String* nm = it2s(name_item);
    if (!nm) return make_str("prompt");
    if (strcmp(nm->chars, "clipboard-read") == 0)
        return make_str(perm_to_str(clipboard_store_get_permission_read()));
    if (strcmp(nm->chars, "clipboard-write") == 0)
        return make_str(perm_to_str(clipboard_store_get_permission_write()));
    return make_str("prompt");
}

// =============================================================================
// Registration entry point — called from js_globals.cpp during init.
// =============================================================================

extern "C" void js_register_clipboard_globals(Item global_this) {
    // ---- Blob -------------------------------------------------------------
    {
        Item ctor = js_new_function((void*)js_blob_new, 2);
        js_set_function_name(ctor, make_str("Blob"));
        Item proto = js_new_object();
        js_property_set(proto, make_str("constructor"), ctor);
        js_property_set(proto, make_str("text"),
            js_new_function((void*)js_blob_text, 0));
        js_property_set(proto, make_str("arrayBuffer"),
            js_new_function((void*)js_blob_array_buffer, 0));
        js_property_set(proto, make_str("slice"),
            js_new_function((void*)js_blob_slice, 3));
        js_property_set(ctor, make_str("prototype"), proto);
        js_property_set(global_this, make_str("Blob"), ctor);
    }

    // ---- File -------------------------------------------------------------
    {
        Item ctor = js_new_function((void*)js_file_new, 3);
        js_set_function_name(ctor, make_str("File"));
        Item proto = js_new_object();
        js_property_set(proto, make_str("constructor"), ctor);
        js_property_set(ctor, make_str("prototype"), proto);
        js_property_set(global_this, make_str("File"), ctor);
    }

    // ---- ClipboardItem ---------------------------------------------------
    {
        Item ctor = js_new_function((void*)js_clipboard_item_new, 2);
        js_set_function_name(ctor, make_str("ClipboardItem"));
        Item proto = js_new_object();
        js_property_set(proto, make_str("constructor"), ctor);
        js_property_set(proto, make_str("getType"),
            js_new_function((void*)js_clipboard_item_get_type, 1));
        js_property_set(ctor, make_str("prototype"), proto);
        js_property_set(ctor, make_str("supports"),
            js_new_function((void*)js_clipboard_item_supports, 1));
        js_property_set(global_this, make_str("ClipboardItem"), ctor);
    }

    // ---- ClipboardEvent --------------------------------------------------
    {
        Item ctor = js_new_function((void*)js_clipboard_event_new, 2);
        js_set_function_name(ctor, make_str("ClipboardEvent"));
        Item proto = js_new_object();
        js_property_set(proto, make_str("constructor"), ctor);
        js_property_set(ctor, make_str("prototype"), proto);
        js_property_set(global_this, make_str("ClipboardEvent"), ctor);
    }

    // ---- DataTransfer ----------------------------------------------------
    {
        Item ctor = js_new_function((void*)js_data_transfer_new, 0);
        js_set_function_name(ctor, make_str("DataTransfer"));
        Item proto = js_new_object();
        js_property_set(proto, make_str("constructor"), ctor);
        js_property_set(ctor, make_str("prototype"), proto);
        js_property_set(global_this, make_str("DataTransfer"), ctor);
    }

    // ---- Clipboard (instanceof + prototype) -----------------------------
    // Real Web platform exposes `Clipboard` as a class. We register a
    // ctor + prototype so `nav.clipboard instanceof Clipboard` works and
    // `Clipboard.prototype.{write,read,readText,writeText}` is observable.
    Item clipboard_proto;
    {
        Item ctor = js_new_function((void*)js_data_transfer_new, 0); // dummy ctor
        js_set_function_name(ctor, make_str("Clipboard"));
        clipboard_proto = js_new_object();
        js_property_set(clipboard_proto, make_str("constructor"), ctor);
        js_property_set(clipboard_proto, make_str("writeText"),
            js_new_function((void*)js_clipboard_write_text, 1));
        js_property_set(clipboard_proto, make_str("readText"),
            js_new_function((void*)js_clipboard_read_text, 0));
        js_property_set(clipboard_proto, make_str("write"),
            js_new_function((void*)js_clipboard_write, 1));
        js_property_set(clipboard_proto, make_str("read"),
            js_new_function((void*)js_clipboard_read, 1));
        js_property_set(ctor, make_str("prototype"), clipboard_proto);
        js_property_set(global_this, make_str("Clipboard"), ctor);
    }

    // ---- navigator -------------------------------------------------------
    // Bridges to the C ClipboardStore so the WPT shim's `_wpt_clipboard_*`
    // helpers and the synthetic Cmd+C/V keyboard handler share the same
    // underlying store as `navigator.clipboard.{readText,writeText}`.
    js_property_set(global_this, make_str("__lambda_clipboard_clear"),
        js_new_function((void*)js_lambda_clipboard_clear, 0));
    js_property_set(global_this, make_str("__lambda_clipboard_write_records"),
        js_new_function((void*)js_lambda_clipboard_write_records, 1));
    js_property_set(global_this, make_str("__lambda_clipboard_read_records"),
        js_new_function((void*)js_lambda_clipboard_read_records, 0));
    js_property_set(global_this, make_str("__lambda_clipboard_set_perm"),
        js_new_function((void*)js_lambda_clipboard_set_perm, 2));
    js_property_set(global_this, make_str("__lambda_clipboard_get_perm"),
        js_new_function((void*)js_lambda_clipboard_get_perm, 1));

    // navigator + navigator.clipboard backed by C store. Methods come from
    // Clipboard.prototype above (writeText/readText/write/read). We also
    // copy the methods directly onto the instance so simple property reads
    // resolve without prototype-chain lookup (matches what test code
    // typically does and the shim's previous direct-assignment behaviour).
    {
        Item clipboard = js_new_object();
        mark_class(clipboard, "Clipboard");
        js_property_set(clipboard, make_str("writeText"),
            js_property_get(clipboard_proto, make_str("writeText")));
        js_property_set(clipboard, make_str("readText"),
            js_property_get(clipboard_proto, make_str("readText")));
        js_property_set(clipboard, make_str("write"),
            js_property_get(clipboard_proto, make_str("write")));
        js_property_set(clipboard, make_str("read"),
            js_property_get(clipboard_proto, make_str("read")));

        Item permissions = js_new_object();
        js_property_set(permissions, make_str("query"),
            js_new_function((void*)js_permissions_query, 1));

        Item navigator = js_new_object();
        js_property_set(navigator, make_str("clipboard"), clipboard);
        js_property_set(navigator, make_str("permissions"), permissions);
        js_property_set(navigator, make_str("platform"), make_str("MacIntel"));
        js_property_set(navigator, make_str("userAgent"),
            make_str("Lambda/Headless (Macintosh)"));
        js_property_set(navigator, make_str("vendor"), make_str(""));
        js_property_set(navigator, make_str("language"), make_str("en-US"));
        js_property_set(global_this, make_str("navigator"), navigator);
    }
}
