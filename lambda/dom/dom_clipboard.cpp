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

#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_typed_array.h"
#include "../js/js_class.h"
#include "dom_events.h"
#include "../lambda-data.hpp"
#include "../module/radiant/radiant_dom_bridge.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../radiant/event.hpp"
#include <string.h>
#include <stdlib.h>

// Forward decls from elsewhere -----------------------------------------------

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
    // registration keys outlive the allocating property writes that publish them;
    // GC-managed strings can be reclaimed before ToPropertyKey finishes.
    return js_name_item(s, strlen(s));
}
static inline Item make_str_n(const char* s, size_t n) {
    return js_name_item(s, (int)n);
}

template <typename Target>
JS_FORWARD_STATIC_VOID( js_clipboard_set_method, (Item object, const char* name, Target target), js_set_key_default, (object, make_str(name), js_new_native_function(target)))
#define JS_CLIPBOARD_REJECT(type_name, message) \
    return js_promise_reject(js_new_error_with_name(make_str(type_name), make_str(message)))
// Browser-visible wrapper identity belongs to the active JS realm. This
// prevents one document's constructors or drag payload from crossing into
// another document's heap while retaining ordinary TLS loads on hot paths.
#define g_blob_proto (js_runtime_state.clipboard.blob_prototype)
#define g_file_proto (js_runtime_state.clipboard.file_prototype)
#define g_clipboard_item_proto (js_runtime_state.clipboard.clipboard_item_prototype)
#define g_clipboard_event_proto (js_runtime_state.clipboard.clipboard_event_prototype)
#define g_data_transfer_proto (js_runtime_state.clipboard.data_transfer_prototype)
#define g_file_list_proto (js_runtime_state.clipboard.file_list_prototype)
#define g_drag_data_transfer (js_runtime_state.clipboard.drag_data_transfer)
#define g_clipboard_generation (js_runtime_state.clipboard.generation)
JS_FORWARD_STATIC_EXPRESSION(bool, clipboard_ensure_roots, (void), (js_active_runtime_state && js_root_range_ensure_registered(&js_runtime_state.clipboard.roots)))

static void attach_known_prototype(Item obj, Item proto) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return;
    TypeId pt = get_type_id(proto);
    if (pt == LMD_TYPE_MAP || pt == LMD_TYPE_FUNC || pt == LMD_TYPE_ARRAY ||
        pt == LMD_TYPE_ELEMENT) {
        js_set_prototype(obj, proto);
    }
}

// Read a string property as a C string (returns NULL if missing/non-string).
// The returned pointer is valid for the lifetime of the underlying String.
static const char* str_prop_get(Item obj, const char* key, size_t* out_len) {
    Item v = js_get_key_default(obj, make_str(key));
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
//   metadata class = Blob
//   _text          : string contents (UTF-8 concatenation of input parts)
//   size           : byte length (number)
//   type           : MIME string (lowercased; empty if invalid char)
//
// Per the WPT subset we care about, parts may be: string | Blob |
// ArrayBuffer | TypedArray | DataView | Array of those. Number/object parts
// fall through and are silently skipped (the previous shim coerced via
// String(), but no in-scope test exercises that path).
JS_FORWARD_STATIC_ITEM(blob_reject_shared_buffer_part, (Item part), js_throw_invalid_arg_type, ("sources", "string, Blob, ArrayBuffer, TypedArray, or DataView", part))

static bool blob_part_has_shared_backing(Item part) {
    if (js_is_sharedarraybuffer(part)) return true;
    if (get_type_id(part) != LMD_TYPE_MAP) return false;
    if (js_is_typed_array(part) || js_is_dataview(part)) {
        Item buffer = js_get_key_cstr(part, "buffer");
        if (js_is_sharedarraybuffer(buffer)) return true;
    }
    if (js_is_typed_array(part)) {
        JsTypedArray* ta = js_get_typed_array_ptr(part.map);
        if (ta && js_arraybuffer_shared(ta->buffer)) return true;
    }
    if (js_is_dataview(part)) {
        JsDataView* dv = js_get_dataview_ptr(part);
        if (dv && js_arraybuffer_shared(dv->buffer)) return true;
    }
    return false;
}

static Item blob_append_part(StrBuf* sb, Item part) {
    TypeId tid = get_type_id(part);
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(part);
        if (s && s->len > 0) strbuf_append_str_n(sb, s->chars, s->len);
        return js_status_ok();
    }
    // ArrayBuffer / TypedArray / DataView — append raw bytes verbatim.
    if (js_is_arraybuffer(part)) {
        // SharedArrayBuffer-backed Blob parts must be rejected before copying;
        // otherwise WebIDL callers can observe shared memory or crash on view metadata.
        if (blob_part_has_shared_backing(part)) return blob_reject_shared_buffer_part(part);
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(part);
        const uint8_t* data = js_arraybuffer_data_const(ab);
        if (ab && data && js_arraybuffer_length(ab) > 0 && !js_arraybuffer_detached(ab)) {
            strbuf_append_str_n(sb, (const char*)data, (size_t)js_arraybuffer_length(ab));
        }
        return js_status_ok();
    }
    if (js_is_typed_array(part)) {
        if (blob_part_has_shared_backing(part)) return blob_reject_shared_buffer_part(part);
        const char* data = (const char*)js_typed_array_current_data_ptr(part);
        int byte_length = js_typed_array_byte_length(part);
        if (data && byte_length > 0) {
            strbuf_append_str_n(sb, data, (size_t)byte_length);
        }
        return js_status_ok();
    }
    if (js_is_dataview(part)) {
        if (blob_part_has_shared_backing(part)) return blob_reject_shared_buffer_part(part);
        JsDataView* dv = js_get_dataview_ptr(part);
        const uint8_t* data = dv ? js_arraybuffer_data_const(dv->buffer) : NULL;
        if (dv && data && dv->byte_length > 0 &&
            !js_arraybuffer_detached(dv->buffer)) {
            strbuf_append_str_n(sb,
                (const char*)data + dv->byte_offset,
                (size_t)dv->byte_length);
        }
        return js_status_ok();
    }
    if (tid == LMD_TYPE_MAP) {
        // Blob? Pull _text.
        if (js_class_id(part) == JS_CLASS_BLOB) {
            size_t n = 0;
            const char* t = str_prop_get(part, "_text", &n);
            if (t && n > 0) strbuf_append_str_n(sb, t, n);
            return js_status_ok();
        }
    }
    // Fallback: silently skip unsupported part types.
    return js_status_ok();
}

static Item js_blob_new_with_class(Item parts, Item options, JsClass class_id) {
    StrBuf* sb = strbuf_new();
    if (get_type_id(parts) == LMD_TYPE_ARRAY) {
        int64_t n = js_array_length(parts);
        for (int64_t i = 0; i < n; i++) {
            Item p = js_elements_get_int(parts, i);
            Item append_result = blob_append_part(sb, p);
            if (item_is_error(append_result)) {
                strbuf_free(sb);
                return append_result;
            }
        }
    } else if (get_type_id(parts) != LMD_TYPE_NULL) {
        // Per spec the parts argument must be iterable; if it's a single string
        // we accept it as a one-element sequence (matches the shim's behavior).
        Item append_result = blob_append_part(sb, parts);
        if (item_is_error(append_result)) {
            strbuf_free(sb);
            return append_result;
        }
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

    Item obj = js_new_object_with_class(class_id);
    attach_known_prototype(obj, g_blob_proto);
    Item text_str = make_str_n(sb->str ? sb->str : "", sb->length);
    js_set_key_cstr(obj, "_text", text_str);
    js_set_key_cstr(obj, "size", (Item){.item = i2it((int64_t)sb->length)});
    js_set_key_cstr(obj, "type", make_str(type_buf));
    // bind prototype methods directly to instance (Lambda has no proto chain walk)
    js_clipboard_set_method(obj, "text", js_blob_text);
    js_clipboard_set_method(obj, "arrayBuffer", js_blob_array_buffer);
    js_clipboard_set_method(obj, "slice", js_blob_slice);
    strbuf_free(sb);
    return obj;
}
JS_FORWARD_ITEM(js_blob_new, (Item parts, Item options), js_blob_new_with_class, (parts, options, JS_CLASS_BLOB))

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
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(buf);
        uint8_t* data = js_arraybuffer_prepare_write(ab);
        if (data) memcpy(data, t, n);
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
        js_set_key_cstr(opts, "type", type_item);
    } else {
        js_set_key_cstr(opts, "type", make_str(""));
    }
    strbuf_free(sb);
    return js_blob_new(parts, opts);
}

// =============================================================================
// File extends Blob
// =============================================================================

extern "C" Item js_file_new(Item parts, Item name_item, Item options) {
    Item obj = js_blob_new_with_class(parts, options, JS_CLASS_FILE);
    if (get_type_id(obj) != LMD_TYPE_MAP) return obj;
    attach_known_prototype(obj, g_file_proto);
    const char* nm = "";
    if (get_type_id(name_item) == LMD_TYPE_STRING) {
        String* s = it2s(name_item);
        if (s) nm = s->chars;
    }
    js_set_key_cstr(obj, "name", make_str(nm));
    int64_t lm = 0;
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item v = js_get_key_cstr(options, "lastModified");
        if (get_type_id(v) == LMD_TYPE_INT) lm = (int64_t)it2i(v);
    }
    js_set_key_cstr(obj, "lastModified", (Item){.item = i2it(lm)});
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
        return js_throw_type_error("ClipboardItem requires a record of MIME types");
    }
    // Per spec: items must be a plain record. Reject Blob (and other tagged classes).
    if (js_class_id(items) != JS_CLASS_NONE) {
        return js_throw_type_error("ClipboardItem requires a record, not a Blob");
    }
    // Iterate source map keys via js_object_keys helper.
    Item keys = js_object_keys(items);
    int64_t nk = (get_type_id(keys) == LMD_TYPE_ARRAY) ? js_array_length(keys) : 0;
    if (nk == 0) {
        return js_throw_type_error("ClipboardItem requires at least one representation");
    }
    Item obj = js_new_object_with_class(JS_CLASS_CLIPBOARD_ITEM);
    attach_known_prototype(obj, g_clipboard_item_proto);

    Item types = js_array_new(0);
    Item orig_types = js_array_new(0);
    Item reps = js_new_object(); // { lower_mime: Blob|string|Promise }

    char mime_buf[256];
    for (int64_t i = 0; i < nk; i++) {
        Item k = js_elements_get_int(keys, i);
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
        js_set_key_default(reps, lower_k, js_get_key_default(items, k));
    }

    js_set_key_cstr(obj, "types", types);
    js_set_key_cstr(obj, "_orig_types", orig_types);
    js_set_key_cstr(obj, "_reps", reps);
    // bind prototype methods directly to instance (Lambda has no proto chain walk)
    js_clipboard_set_method(obj, "getType", js_clipboard_item_get_type);

    const char* presentation = "attachment";
    if (get_type_id(options) == LMD_TYPE_MAP) {
        size_t pn = 0;
        const char* p = str_prop_get(options, "presentationStyle", &pn);
        if (p && (strcmp(p, "inline") == 0 || strcmp(p, "attachment") == 0 ||
                  strcmp(p, "unspecified") == 0)) presentation = p;
    }
    js_set_key_cstr(obj, "presentationStyle", make_str(presentation));
    return obj;
}

extern "C" Item js_clipboard_item_get_type(Item type_item) {
    Item self = js_get_this();
    Item gen = js_get_key_cstr(self, "_clipboard_generation");
    if (get_type_id(gen) == LMD_TYPE_INT && (int64_t)it2i(gen) != g_clipboard_generation) {
        JS_CLIPBOARD_REJECT("DataError", "clipboard item is stale");
    }
    if (get_type_id(type_item) != LMD_TYPE_STRING) {
        JS_CLIPBOARD_REJECT("TypeError", "ClipboardItem.getType: type must be a string");
    }
    Item reps = js_get_key_cstr(self, "_reps");
    if (get_type_id(reps) != LMD_TYPE_MAP) {
        JS_CLIPBOARD_REJECT("NotFoundError", "type not found");
    }
    String* ts = it2s(type_item);
    char buf[256];
    if (!ts || ts->len == 0 || ts->len >= sizeof(buf)) {
        JS_CLIPBOARD_REJECT("NotFoundError", "type not found");
    }
    for (size_t j = 0; j < ts->len; j++) {
        unsigned char c = (unsigned char)ts->chars[j];
        buf[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    buf[ts->len] = '\0';
    Item rep = js_get_key_default(reps, make_str(buf));
    if (rep.item == ITEM_JS_UNDEFINED || get_type_id(rep) == LMD_TYPE_NULL) {
        JS_CLIPBOARD_REJECT("NotFoundError", "type not found");
    }
    // If it's already a Blob, resolve directly. If it's a string, wrap in Blob.
    if (get_type_id(rep) == LMD_TYPE_STRING) {
        Item parts = js_array_new(0);
        js_array_push(parts, rep);
        Item opts = js_new_object();
        js_set_key_cstr(opts, "type", make_str(buf));
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
    js_set_key_cstr(self, "defaultPrevented", (Item){.item = b2it(true)});
    return ItemNull;
}
extern "C" Item js_clipboard_event_stop_propagation(void) {
    Item self = js_get_this();
    js_set_key_cstr(self, "_stopped", (Item){.item = b2it(true)});
    return ItemNull;
}
extern "C" Item js_clipboard_event_stop_immediate_propagation(void) {
    Item self = js_get_this();
    js_set_key_cstr(self, "_stopped", (Item){.item = b2it(true)});
    js_set_key_cstr(self, "_stoppedImmediate", (Item){.item = b2it(true)});
    return ItemNull;
}
JS_FORWARD_ITEM(js_clipboard_event_composed_path, (void), js_array_new, (0))

// Forward decl for the DataTransfer factory (defined below).
static Item js_make_data_transfer_object(void);

extern "C" Item js_clipboard_event_new(Item type_item, Item init_item) {
    Item ev = js_new_object_with_class(JS_CLASS_CLIPBOARD_EVENT);
    attach_known_prototype(ev, g_clipboard_event_proto);

    const char* type = "";
    if (get_type_id(type_item) == LMD_TYPE_STRING) {
        String* s = it2s(type_item);
        if (s) type = s->chars;
    }
    js_set_key_cstr(ev, "type", make_str(type));
    js_set_key_cstr(ev, "isTrusted", (Item){.item = b2it(false)});
    js_set_key_cstr(ev, "bubbles", (Item){.item = b2it(false)});
    js_set_key_cstr(ev, "cancelable", (Item){.item = b2it(false)});
    js_set_key_cstr(ev, "composed", (Item){.item = b2it(false)});
    js_set_key_cstr(ev, "defaultPrevented", (Item){.item = b2it(false)});

    if (get_type_id(init_item) == LMD_TYPE_MAP) {
        Item b = js_get_key_cstr(init_item, "bubbles");
        if (get_type_id(b) == LMD_TYPE_BOOL)
            js_set_key_cstr(ev, "bubbles", b);
        Item c = js_get_key_cstr(init_item, "cancelable");
        if (get_type_id(c) == LMD_TYPE_BOOL)
            js_set_key_cstr(ev, "cancelable", c);
        Item cp = js_get_key_cstr(init_item, "composed");
        if (get_type_id(cp) == LMD_TYPE_BOOL)
            js_set_key_cstr(ev, "composed", cp);
        Item cd = js_get_key_cstr(init_item, "clipboardData");
        if (get_type_id(cd) == LMD_TYPE_MAP) {
            js_set_key_cstr(ev, "clipboardData", cd);
        } else {
            js_set_key_cstr(ev, "clipboardData", js_make_data_transfer_object());
        }
    } else {
        js_set_key_cstr(ev, "clipboardData", js_make_data_transfer_object());
    }

    js_clipboard_set_method(ev, "preventDefault", js_clipboard_event_prevent_default);
    js_clipboard_set_method(ev, "stopPropagation", js_clipboard_event_stop_propagation);
    js_clipboard_set_method(ev, "stopImmediatePropagation",
        js_clipboard_event_stop_immediate_propagation);
    js_clipboard_set_method(ev, "composedPath", js_clipboard_event_composed_path);
    return ev;
}

// =============================================================================
// DataTransfer — full items/files/types list semantics
// =============================================================================
//
// Native model:
//   dt:
//     metadata class = DataTransfer
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
extern "C" Item js_dt_items_item(Item idx_arg);
extern "C" Item js_dt_items_remove(Item idx_arg);
extern "C" Item js_dt_items_clear(void);
extern "C" Item js_dt_item_get_as_file(void);
extern "C" Item js_dt_item_get_as_string(Item callback);
extern "C" Item js_dt_files_item(Item idx_arg);
extern "C" Item js_dt_set_data(Item type_item, Item data_item);
extern "C" Item js_dt_get_data(Item type_item);
extern "C" Item js_dt_clear_data(Item format_item);
JS_FORWARD_ITEM(js_file_list_new, (void), js_throw_type_error, ("Illegal constructor"))

static bool dt_is_class(Item v, const char* name, size_t name_len) {
    if (get_type_id(v) != LMD_TYPE_MAP) return false;
    JsClass cls = js_class_from_name(name, (int)name_len);
    return cls != JS_CLASS_NONE && js_class_id(v) == cls;
}

static bool dt_index_arg(Item value, int* out_idx) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        if (out_idx) *out_idx = (int)it2i(value);
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        double number = it2d(value);
        // DataTransfer list indexes are public JS Numbers, now boxed as FLOAT.
        if (number != number || number != (double)(int)number) return false;
        if (out_idx) *out_idx = (int)number;
        return true;
    }
    return false;
}

static bool dt_record_kind_is(Item r, const char* kind, size_t kind_len) {
    if (get_type_id(r) != LMD_TYPE_MAP) return false;
    Item k = js_get_key_cstr(r, "kind");
    if (get_type_id(k) != LMD_TYPE_STRING) return false;
    String* s = it2s(k);
    return s && (size_t)s->len == kind_len &&
        strncmp(s->chars, kind, kind_len) == 0;
}

static bool dt_record_string_type_is(Item record, const char* type, size_t type_len) {
    if (!dt_record_kind_is(record, "string", 6)) return false;
    Item format = js_get_key_cstr(record, "type");
    if (get_type_id(format) != LMD_TYPE_STRING) return false;
    String* s = it2s(format);
    return s && (size_t)s->len == type_len &&
        strncmp(s->chars, type, type_len) == 0;
}

// Lowercase and copy in-place. Returns false if input does not fit.
static bool dt_normalize_format(Item type_item, char* out, size_t out_cap) {
    if (get_type_id(type_item) != LMD_TYPE_STRING) return false;
    String* s = it2s(type_item);
    if (!s) return false;
    if ((size_t)s->len + 1 > out_cap) return false;
    for (int i = 0; i < (int)s->len; i++) {
        unsigned char c = (unsigned char)s->chars[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    out[s->len] = '\0';
    if (strcmp(out, "text") == 0) {
        if (out_cap < 11) return false;
        snprintf(out, out_cap, "%s", "text/plain");
    }
    return true;
}

// Recompute the public items/files/types arrays in place from _items.
// Required to preserve caller-held references like `const fl = dt.files`.
static void dt_recompute_views(Item dt) {
    Item items = js_get_key_cstr(dt, "items");
    Item files = js_get_key_cstr(dt, "files");
    Item types = js_get_key_cstr(dt, "types");
    Item rec   = js_get_key_cstr(dt, "_items");
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
        Item r = js_elements_get_int(rec, i);
        Item kind = js_get_key_cstr(r, "kind");
        Item type = js_get_key_cstr(r, "type");

        // public DataTransferItem-like proxy: { kind, type }
        Item proxy = js_new_object();
        js_set_key_cstr(proxy, "kind", kind);
        js_set_key_cstr(proxy, "type", type);
        js_set_key_cstr(proxy, "_record", r);
        js_clipboard_set_method(proxy, "getAsFile", js_dt_item_get_as_file);
        js_clipboard_set_method(proxy, "getAsString", js_dt_item_get_as_string);
        js_array_push(items, proxy);

        bool is_file = dt_record_kind_is(r, "file", 4);
        if (is_file) {
            has_files = true;
            Item f = js_get_key_cstr(r, "file");
            if (f.item != ITEM_NULL) js_array_push(files, f);
        } else if (get_type_id(type) == LMD_TYPE_STRING) {
            // dedupe types for string entries
            String* ts = it2s(type);
            bool seen = false;
            int64_t tn = js_array_length(types);
            for (int64_t j = 0; j < tn; j++) {
                Item ev = js_elements_get_int(types, j);
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

extern "C" Item js_dt_item_get_as_file(void) {
    Item item = js_get_this();
    if (get_type_id(item) != LMD_TYPE_MAP) return ItemNull;
    Item record = js_get_key_cstr(item, "_record");
    if (!dt_record_kind_is(record, "file", 4)) return ItemNull;
    Item file = js_get_key_cstr(record, "file");
    return file.item == 0 ? ItemNull : file;
}

extern "C" Item js_dt_item_get_as_string(Item callback) {
    Item item = js_get_this();
    if (get_type_id(item) != LMD_TYPE_MAP) return make_js_undefined();
    Item record = js_get_key_cstr(item, "_record");
    if (!dt_record_kind_is(record, "string", 6)) return make_js_undefined();
    if (!js_is_callable(callback)) return make_js_undefined();
    Item value = js_get_key_cstr(record, "value");
    if (get_type_id(value) != LMD_TYPE_STRING) value = make_str("");
    js_call_function(callback, make_js_undefined(), &value, 1);
    return make_js_undefined();
}

// items.add(data, type?) — DataTransferItemList.add
extern "C" Item js_dt_items_add(Item data_arg, Item type_arg) {
    Item items = js_get_this();
    if (get_type_id(items) != LMD_TYPE_ARRAY) return ItemNull;
    Item dt = js_get_key_cstr(items, "_owner");
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    Item rec_arr = js_get_key_cstr(dt, "_items");
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return ItemNull;

    Item record = js_new_object();
    bool is_file_arg = dt_is_class(data_arg, "File", 4) ||
                       dt_is_class(data_arg, "Blob", 4);

    if (is_file_arg) {
        Item ftype = js_get_key_cstr(data_arg, "type");
        if (get_type_id(ftype) != LMD_TYPE_STRING) ftype = make_str("");
        js_set_key_cstr(record, "kind", make_str("file"));
        js_set_key_cstr(record, "type", ftype);
        js_set_key_cstr(record, "file", data_arg);
        js_array_push(rec_arr, record);
    } else if (get_type_id(data_arg) == LMD_TYPE_STRING) {
        if (get_type_id(type_arg) != LMD_TYPE_STRING) {
            return js_throw_type_error(
                "DataTransferItemList.add requires a type for strings");
        }
        char tbuf[256];
        if (!dt_normalize_format(type_arg, tbuf, sizeof(tbuf))) return ItemNull;
        // Spec: only one string item per type allowed.
        size_t tlen = strlen(tbuf);
        int64_t n = js_array_length(rec_arr);
        for (int64_t i = 0; i < n; i++) {
            Item r = js_elements_get_int(rec_arr, i);
            if (dt_record_string_type_is(r, tbuf, tlen)) {
                return js_throw_type_error(
                    "NotSupportedError: type already present");
            }
        }
        js_set_key_cstr(record, "kind", make_str("string"));
        js_set_key_cstr(record, "type", make_str(tbuf));
        js_set_key_cstr(record, "value", data_arg);
        js_array_push(rec_arr, record);
    } else {
        return ItemNull;
    }

    dt_recompute_views(dt);
    Item items_view = js_get_key_cstr(dt, "items");
    int64_t ln = js_array_length(items_view);
    return (ln > 0) ? js_elements_get_int(items_view, ln - 1) : ItemNull;
}

extern "C" Item js_dt_items_item(Item idx_arg) {
    Item items = js_get_this();
    if (get_type_id(items) != LMD_TYPE_ARRAY) return ItemNull;
    int idx = -1;
    dt_index_arg(idx_arg, &idx);
    int64_t n = js_array_length(items);
    if (idx < 0 || idx >= n) return ItemNull;
    return js_elements_get_int(items, idx);
}

extern "C" Item js_dt_items_remove(Item idx_arg) {
    Item items = js_get_this();
    if (get_type_id(items) != LMD_TYPE_ARRAY) return ItemNull;
    Item dt = js_get_key_cstr(items, "_owner");
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    Item rec_arr = js_get_key_cstr(dt, "_items");
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return ItemNull;

    int idx = -1;
    dt_index_arg(idx_arg, &idx);
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
    Item dt = js_get_key_cstr(items, "_owner");
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    Item rec_arr = js_get_key_cstr(dt, "_items");
    if (get_type_id(rec_arr) == LMD_TYPE_ARRAY) rec_arr.array->length = 0;
    dt_recompute_views(dt);
    return ItemNull;
}

extern "C" Item js_dt_files_item(Item idx_arg) {
    Item files = js_get_this();
    if (get_type_id(files) != LMD_TYPE_ARRAY) return ItemNull;
    int idx = -1;
    dt_index_arg(idx_arg, &idx);
    int64_t n = js_array_length(files);
    if (idx < 0 || idx >= n) return ItemNull;
    return js_elements_get_int(files, idx);
}

extern "C" Item js_dt_set_data(Item type_item, Item data_item) {
    Item dt = js_get_this();
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    char tbuf[256];
    if (!dt_normalize_format(type_item, tbuf, sizeof(tbuf))) return ItemNull;
    Item value = data_item;
    if (get_type_id(value) != LMD_TYPE_STRING) value = make_str("");
    Item rec_arr = js_get_key_cstr(dt, "_items");
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return ItemNull;

    size_t tlen = strlen(tbuf);
    int64_t n = js_array_length(rec_arr);
    for (int64_t i = 0; i < n; i++) {
        Item r = js_elements_get_int(rec_arr, i);
        if (dt_record_string_type_is(r, tbuf, tlen)) {
            js_set_key_cstr(r, "value", value);
            dt_recompute_views(dt);
            return ItemNull;
        }
    }
    Item record = js_new_object();
    js_set_key_cstr(record, "kind", make_str("string"));
    js_set_key_cstr(record, "type", make_str(tbuf));
    js_set_key_cstr(record, "value", value);
    js_array_push(rec_arr, record);
    dt_recompute_views(dt);
    return ItemNull;
}

extern "C" Item js_dt_get_data(Item type_item) {
    Item dt = js_get_this();
    if (get_type_id(dt) != LMD_TYPE_MAP) return make_str("");
    char tbuf[256];
    if (!dt_normalize_format(type_item, tbuf, sizeof(tbuf))) return make_str("");
    Item rec_arr = js_get_key_cstr(dt, "_items");
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return make_str("");
    size_t tlen = strlen(tbuf);
    int64_t n = js_array_length(rec_arr);
    for (int64_t i = 0; i < n; i++) {
        Item r = js_elements_get_int(rec_arr, i);
        if (dt_record_string_type_is(r, tbuf, tlen)) {
            Item v = js_get_key_cstr(r, "value");
            return (get_type_id(v) == LMD_TYPE_STRING) ? v : make_str("");
        }
    }
    return make_str("");
}

// clearData([format]) — no-arg clears all string items (keeps files);
// with format clears only that string item.
extern "C" Item js_dt_clear_data(Item format_item) {
    Item dt = js_get_this();
    if (get_type_id(dt) != LMD_TYPE_MAP) return ItemNull;
    Item rec_arr = js_get_key_cstr(dt, "_items");
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
        Item kind = js_get_key_cstr(r, "kind");
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
                Item etype = js_get_key_cstr(r, "type");
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
    RootFrame roots(4);
    Rooted<Item> dt_root(roots, ItemNull);
    Rooted<Item> items_root(roots, ItemNull);
    Rooted<Item> files_root(roots, ItemNull);
    Rooted<Item> types_root(roots, ItemNull);
    dt_root.set(js_new_object_with_class(JS_CLASS_DATA_TRANSFER));
    attach_known_prototype(dt_root.get(), g_data_transfer_proto);
    js_set_key_cstr(dt_root.get(), "dropEffect", make_str("none"));
    js_set_key_cstr(dt_root.get(), "effectAllowed", make_str("none"));
    js_set_key_cstr(dt_root.get(), "_items", js_array_new(0));

    // Stable view arrays — mutated in place by dt_recompute_views.
    items_root.set(js_array_new(0));
    files_root.set(js_array_new_with_class(0, JS_CLASS_FILE_LIST));
    types_root.set(js_array_new(0));
    // FileList is array-backed internally, but its Web IDL prototype and brand
    // must survive input.files assignment and DataTransfer view recomputation.
    if (get_type_id(g_file_list_proto) == LMD_TYPE_MAP) {
        js_set_prototype(files_root.get(), g_file_list_proto);
    }
    js_set_key_cstr(items_root.get(), "_owner", dt_root.get());
    js_set_key_cstr(files_root.get(), "_owner", dt_root.get());
    js_clipboard_set_method(items_root.get(), "add", js_dt_items_add);
    js_clipboard_set_method(items_root.get(), "item", js_dt_items_item);
    js_clipboard_set_method(items_root.get(), "remove", js_dt_items_remove);
    js_clipboard_set_method(items_root.get(), "clear", js_dt_items_clear);
    js_set_key_cstr(dt_root.get(), "items", items_root.get());
    js_set_key_cstr(dt_root.get(), "files", files_root.get());
    js_set_key_cstr(dt_root.get(), "types", types_root.get());

    js_clipboard_set_method(dt_root.get(), "setData", js_dt_set_data);
    js_clipboard_set_method(dt_root.get(), "getData", js_dt_get_data);
    js_clipboard_set_method(dt_root.get(), "clearData", js_dt_clear_data);
    // The public views are published only after all native method allocation;
    // retain each owner explicitly because native locals are not GC roots.
    return dt_root.get();
}
JS_FORWARD_ITEM(js_data_transfer_new, (void), js_make_data_transfer_object, ())

// CE-3 follow-up (Radiant_Design_Content_Editable.md §6.1 / §8): build a
// DataTransfer pre-populated with text/plain and/or text/html records, for
// the InputEvent {insertFromPaste|insertFromDrop|deleteByDrag} dispatch path.
// Either string may be null/empty — only non-empty records are added. The
// items/files/types views are recomputed once at the end.
extern "C" Item js_data_transfer_new_with_strings(const char* text_plain,
                                                  const char* text_html)
{
    Item dt = js_make_data_transfer_object();
    Item rec_arr = js_get_key_cstr(dt, "_items");
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return dt;
    if (text_plain && *text_plain) {
        Item record = js_new_object();
        js_set_key_cstr(record, "kind", make_str("string"));
        js_set_key_cstr(record, "type", make_str("text/plain"));
        js_set_key_cstr(record, "value", make_str(text_plain));
        js_array_push(rec_arr, record);
    }
    if (text_html && *text_html) {
        Item record = js_new_object();
        js_set_key_cstr(record, "kind", make_str("string"));
        js_set_key_cstr(record, "type", make_str("text/html"));
        js_set_key_cstr(record, "value", make_str(text_html));
        js_array_push(rec_arr, record);
    }
    dt_recompute_views(dt);
    return dt;
}

// Read a text/<mime> record's value out of a DataTransfer's _items array.
// Returns NULL if absent. The returned pointer is owned by the JS string.
static const char* dt_read_record(Item dt, const char* mime) {
    Item rec_arr = js_get_key_cstr(dt, "_items");
    if (get_type_id(rec_arr) != LMD_TYPE_ARRAY) return NULL;
    int64_t n = js_array_length(rec_arr);
    for (int64_t i = 0; i < n; i++) {
        Item r = js_elements_get_int(rec_arr, i);
        if (get_type_id(r) != LMD_TYPE_MAP) continue;
        Item type = js_get_key_cstr(r, "type");
        if (get_type_id(type) != LMD_TYPE_STRING) continue;
        String* ts = it2s(type);
        if (!ts || strcmp(ts->chars, mime) != 0) continue;
        Item val = js_get_key_cstr(r, "value");
        if (get_type_id(val) != LMD_TYPE_STRING) return "";
        String* vs = it2s(val);
        return vs ? vs->chars : "";
    }
    return NULL;
}

// Stage 4C Phase B: dispatch a synthetic clipboard event (paste / copy / cut)
// to a DOM element with a store-backed `clipboardData`, so script-owned rich
// editors that use addEventListener('paste'|'copy'|'cut') work under
// `lambda.exe view` (native has no real OS clipboard-event delivery there).
//   - paste: clipboardData is pre-populated from the C clipboard store
//     (text/plain + text/html); the handler reads via getData().
//   - copy/cut: clipboardData starts empty; whatever the handler writes via
//     setData() is persisted back into the store after dispatch.
// Returns true if the handler called preventDefault().
extern "C" bool js_dispatch_clipboard_event_to_element(Item target_item, const char* type) {
    bool is_paste = (strcmp(type, "paste") == 0);
    Item dt;
    if (is_paste) {
        // clipboard_store_read_mime returns a pointer into a single reused
        // buffer — the second call invalidates the first, so copy text/plain
        // before reading text/html.
        const char* plain_raw = clipboard_store_read_mime("text/plain");
        char* plain = (plain_raw && *plain_raw) ? strdup(plain_raw) : NULL;
        const char* html = clipboard_store_read_mime("text/html");
        dt = js_data_transfer_new_with_strings(plain, html);
        free(plain);
    } else {
        dt = js_make_data_transfer_object();
    }
    Item ev = js_create_event(type, /*bubbles=*/1, /*cancelable=*/1);
    js_set_key_cstr(ev, "clipboardData", dt);
    dom_dispatch_event(target_item, ev);
    bool prevented = radiant_dom_event_default_prevented(ev);
    if (!is_paste) {
        const char* plain = dt_read_record(dt, "text/plain");
        const char* html  = dt_read_record(dt, "text/html");
        if (html && *html) clipboard_store_write_html(html, plain ? plain : "");
        else if (plain && *plain) clipboard_store_write_text(plain);
    }
    return prevented;
}

// Stage 4C Phase B: HTML5 drag-and-drop JS event dispatch. Radiant's native
// drag machinery only invokes Lambda-template handlers, and only on elements
// carrying a `dropzone` attribute; script editors that use
// addEventListener('dragstart'|'dragover'|'drop') with a DataTransfer never see
// those. A single DataTransfer persists across the whole gesture
// (dragstart→dragover→drop→dragend) so setData() in dragstart is visible to
// getData() in drop, matching the browser. The Item lives in a GC root
// owned by the active context's exact root range.

extern "C" void js_drag_session_begin(void) {
    if (!js_active_runtime_state) return;
    if (!clipboard_ensure_roots()) return;
    g_drag_data_transfer = js_data_transfer_new();
}

extern "C" void js_drag_session_end(void) {
    // Native drag state is valid without a JavaScript document; only a bound
    // JS Runtime owns the DataTransfer root that this cleanup can clear.
    if (!js_active_runtime_state) return;
    // keep the root registered; clearing to null makes GC ignore the slot.
    g_drag_data_transfer.item = ITEM_NULL;
}

// Dispatch one synthetic DragEvent (dragstart/dragover/drop/dragend/...) to a
// DOM element carrying the session DataTransfer plus clientX/clientY. Returns
// true if the handler called preventDefault() (used to gate whether a drop is
// allowed, per HTML5: drop fires only when the preceding dragover was
// canceled).
extern "C" bool js_dispatch_drag_event_to_element(Item target_item,
        const char* type, double client_x, double client_y) {
    if (!js_active_runtime_state) return false;
    // The session DataTransfer must be allocated inside the JS runtime context
    // (js_new_object needs the active runtime heap), so open it here — the
    // caller enters the ctx scope before dispatching. dragstart always starts a
    // fresh session; later points reuse it (or begin lazily if one was missed).
    if (type && strcmp(type, "dragstart") == 0) {
        js_drag_session_begin();
    } else if (g_drag_data_transfer.item == ITEM_NULL) {
        js_drag_session_begin();
    }
    Item ev = js_create_native_drag_event(type, client_x, client_y,
        g_drag_data_transfer, false, false, false, false);
    dom_dispatch_event(target_item, ev);
    return radiant_dom_event_default_prevented(ev);
}

// =============================================================================
// navigator.clipboard — backed by radiant/clipboard.{hpp,cpp}
// =============================================================================

extern "C" Item js_clipboard_write_text(Item text_item) {
    if (clipboard_store_get_permission_write() == CLIPBOARD_PERMISSION_DENIED) {
        JS_CLIPBOARD_REJECT("NotAllowedError", "Write permission denied");
    }
    // Per WebIDL, writeText(DOMString) requires its argument; calling with no
    // args (undefined) must reject with TypeError. `null` stringifies to "null"
    // by spec but the WPT subset treats null as empty string \u2014 mirror the
    // previous polyfill's `data == null ? "" : String(data)` semantics.
    if (text_item.item == ITEM_JS_UNDEFINED) {
        JS_CLIPBOARD_REJECT("TypeError", "writeText requires 1 argument");
    }
    const char* t = "";
    if (get_type_id(text_item) == LMD_TYPE_STRING) {
        String* s = it2s(text_item);
        if (s) t = s->chars;
    }
    clipboard_store_write_text(t);
    g_clipboard_generation++;
    return js_promise_resolve(ItemNull);
}

extern "C" Item js_clipboard_read_text(void) {
    if (clipboard_store_get_permission_read() == CLIPBOARD_PERMISSION_DENIED) {
        JS_CLIPBOARD_REJECT("NotAllowedError", "Read permission denied");
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
JS_FORWARD_STATIC_EXPRESSION(bool, is_standard_mandatory_mime, (const char* s), (strcmp(s, "text/plain") == 0 || strcmp(s, "text/html") == 0 || strcmp(s, "image/png") == 0 || strcmp(s, "text/uri-list") == 0 || strcmp(s, "image/svg+xml") == 0))

// Returns true if `s` is a valid `<type>/<sub>` MIME body (both halves
// non-empty, no '/' inside the halves). Uppercase check is done by the caller.
static bool is_valid_mime_body(const char* s, size_t n) {
    const char* slash = (const char*)memchr(s, '/', n);
    if (!slash || slash == s || slash == s + n - 1) return false;
    return true;
}
JS_FORWARD_STATIC_EXPRESSION(bool, item_is_clipboard_item, (Item it), (js_class_id(it) == JS_CLASS_CLIPBOARD_ITEM))

// Blob-like duck type: a native Blob/File OR any
// object exposing both a string `.type` and a callable `.text()`.
// Mirrors the shim's `isBlobLike` so fetch().blob() values round-trip.
static bool item_is_blob_like(Item it) {
    JsClass cls = js_class_id(it);
    if (cls == JS_CLASS_BLOB || cls == JS_CLASS_FILE) return true;
    if (get_type_id(it) != LMD_TYPE_MAP) return false;
    Item ty = js_get_key_cstr(it, "type");
    Item tx = js_get_key_cstr(it, "text");
    return get_type_id(ty) == LMD_TYPE_STRING &&
           js_is_callable(tx);
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

// Read a Blob-like's text into a mem_alloc'd C string (caller mem_free()s). Returns
// NULL if the blob has no extractable bytes. Handles native Blob `_text`
// directly. For foreign Blob-likes, returns the result of calling .text(),
// but only if it is already a fulfilled Promise<string> or a string (we cannot
// block on a foreign async .text() here — those are unsupported in this path).
static char* blob_like_get_text(Item blob, size_t* out_len) {
    if (get_type_id(blob) == LMD_TYPE_STRING) {
        String* s = it2s(blob);
        if (!s) return NULL;
        char* buf = (char*)mem_alloc(s->len + 1, MEM_CAT_JS_RUNTIME);
        memcpy(buf, s->chars, s->len);
        buf[s->len] = '\0';
        if (out_len) *out_len = s->len;
        return buf;
    }
    size_t n = 0;
    const char* t = str_prop_get(blob, "_text", &n);
    if (t) {
        char* buf = (char*)mem_alloc(n + 1, MEM_CAT_JS_RUNTIME);
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
        Item item = js_elements_get_int(items_array, i);
        Item types = js_get_key_cstr(item, "types");
        if (get_type_id(types) != LMD_TYPE_ARRAY) continue;
        int64_t nk = js_array_length(types);
        Item rec = js_new_object();
        for (int64_t j = 0; j < nk; j++) {
            Item k = js_elements_get_int(types, j);
            if (get_type_id(k) != LMD_TYPE_STRING) { flat_idx++; continue; }
            String* ks = it2s(k);
            if (!ks) { flat_idx++; continue; }
            Item v = js_elements_get_int(resolved_values, flat_idx++);

            // image/* representations MUST be Blob — reject the whole write.
            if (ks->len >= 6 && memcmp(ks->chars, "image/", 6) == 0) {
                if (!item_is_blob_like(v)) {
                    JS_CLIPBOARD_REJECT("TypeError", "image representation must be a Blob");
                }
            }

            size_t tlen = 0;
            char* tbuf = blob_like_get_text(v, &tlen);
            if (!tbuf) { tbuf = (char*)mem_alloc(1, MEM_CAT_JS_RUNTIME); tbuf[0] = '\0'; tlen = 0; }

            // Sanitise text/html (sanitised standard format only; "web text/html"
            // custom-format is preserved verbatim by virtue of having a
            // different lower_key e.g. "web text/html").
            if (ks->len == 9 && memcmp(ks->chars, "text/html", 9) == 0) {
                StrBuf* sb = strbuf_new();
                strip_html_script_style(sb, tbuf, tlen);
                mem_free(tbuf);
                tbuf = (char*)mem_alloc(sb->length + 1, MEM_CAT_JS_RUNTIME);
                memcpy(tbuf, sb->str ? sb->str : "", sb->length);
                tbuf[sb->length] = '\0';
                tlen = sb->length;
                strbuf_free(sb);
            }

            js_set_key_default(rec, k, make_str_n(tbuf, tlen));
            mem_free(tbuf);
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
        JS_CLIPBOARD_REJECT("NotAllowedError", "Write permission denied");
    }
    if (get_type_id(items_array) != LMD_TYPE_ARRAY) {
        JS_CLIPBOARD_REJECT("TypeError", "write() requires a sequence of ClipboardItems");
    }
    int64_t n_items = js_array_length(items_array);
    if (n_items == 0) {
        JS_CLIPBOARD_REJECT("TypeError", "write() requires a sequence of ClipboardItems");
    }
    // Per spec quirk (matched by all major browsers + WPT), only one
    // ClipboardItem may be written per call.
    if (n_items > 1) {
        JS_CLIPBOARD_REJECT("NotAllowedError", "writing more than one ClipboardItem is not supported");
    }

    int web_custom_count = 0;

    for (int64_t i = 0; i < n_items; i++) {
        Item item = js_elements_get_int(items_array, i);
        if (!item_is_clipboard_item(item)) {
            JS_CLIPBOARD_REJECT("TypeError", "write() entries must be ClipboardItem");
        }
        Item orig_types = js_get_key_cstr(item, "_orig_types");
        Item types_lower = js_get_key_cstr(item, "types");
        Item reps = js_get_key_cstr(item, "_reps");
        if (get_type_id(orig_types) != LMD_TYPE_ARRAY) continue;
        int64_t nk = js_array_length(orig_types);
        for (int64_t j = 0; j < nk; j++) {
            Item ot = js_elements_get_int(orig_types, j);
            if (get_type_id(ot) != LMD_TYPE_STRING) {
                JS_CLIPBOARD_REJECT("NotAllowedError", "invalid clipboard format");
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
                    JS_CLIPBOARD_REJECT("NotAllowedError", "invalid web custom format");
                }
                web_custom_count++;
                // Blob.type vs format check.
                Item lower_k = js_elements_get_int(types_lower, j);
                Item rep = js_get_key_default(reps, lower_k);
                if (item_is_blob_like(rep)) {
                    Item bt = js_get_key_cstr(rep, "type");
                    if (get_type_id(bt) == LMD_TYPE_STRING) {
                        String* bts = it2s(bt);
                        if (bts && bts->len > 0 &&
                            strcmp(bts->chars, sub) != 0 &&
                            strcmp(bts->chars, otc) != 0) {
                            JS_CLIPBOARD_REJECT("NotAllowedError", "Blob.type does not match format");
                        }
                    }
                }
                continue;
            }
            if (str_has_upper_ascii(otc, otl)) {
                JS_CLIPBOARD_REJECT("NotAllowedError", "invalid (non-lowercase) format");
            }
            if (!is_standard_mandatory_mime(otc)) {
                JS_CLIPBOARD_REJECT("NotAllowedError", "unsupported clipboard format");
            }
        }
    }
    if (web_custom_count > 100) {
        JS_CLIPBOARD_REJECT("NotAllowedError", "too many custom formats (max 100)");
    }

    // Build flat array of resolved-promises over each rep value.
    Item flat = js_array_new(0);
    for (int64_t i = 0; i < n_items; i++) {
        Item item = js_elements_get_int(items_array, i);
        Item types = js_get_key_cstr(item, "types");
        Item reps = js_get_key_cstr(item, "_reps");
        if (get_type_id(types) != LMD_TYPE_ARRAY) continue;
        int64_t nk = js_array_length(types);
        for (int64_t j = 0; j < nk; j++) {
            Item k = js_elements_get_int(types, j);
            Item v = js_get_key_default(reps, k);
            // Promise.resolve(v): JsPromise stays as-is, plain values are wrapped.
            js_array_push(flat, js_promise_resolve(v));
        }
    }

    Item all_p = js_promise_all(flat);
    Item handler_raw = js_new_native_function(js_clipboard_materialise);
    Item bound = js_bind_function(handler_raw, ItemNull, &items_array, 1);
    return js_promise_then(all_p, bound, ItemNull);
}

// Clipboard.prototype.read — synchronous read of the C ClipboardStore wrapped
// in ClipboardItems whose representations are Blobs (per spec).
extern "C" Item js_clipboard_read(Item opts) {
    if (clipboard_store_get_permission_read() == CLIPBOARD_PERMISSION_DENIED) {
        JS_CLIPBOARD_REJECT("NotAllowedError", "Read permission denied");
    }
    // ClipboardUnsanitizedFormats.unsanitized: per WebIDL it must be a
    // sequence. An *absent* key is fine (skip). An explicit `null` (or any
    // non-array value) rejects with TypeError. A non-empty array rejects
    // with NotAllowedError (we don't support unsanitised reads in headless).
    if (get_type_id(opts) == LMD_TYPE_MAP) {
        // Detect presence by walking keys (js_get_key_default can't distinguish
        // explicit-null from absent on plain Lambda maps).
        bool has_unsanitized = false;
        Item okeys = js_object_keys(opts);
        if (get_type_id(okeys) == LMD_TYPE_ARRAY) {
            int64_t okn = js_array_length(okeys);
            for (int64_t kk = 0; kk < okn; kk++) {
                Item kkk = js_elements_get_int(okeys, kk);
                if (get_type_id(kkk) != LMD_TYPE_STRING) continue;
                String* kss = it2s(kkk);
                if (kss && kss->len == 11 && memcmp(kss->chars, "unsanitized", 11) == 0) {
                    has_unsanitized = true;
                    break;
                }
            }
        }
        if (has_unsanitized) {
            Item u = js_get_key_cstr(opts, "unsanitized");
            TypeId ut = get_type_id(u);
            if (ut == LMD_TYPE_ARRAY) {
                if (js_array_length(u) > 0) {
                    JS_CLIPBOARD_REJECT("NotAllowedError", "unsanitized read is not supported");
                }
            } else {
                JS_CLIPBOARD_REJECT("TypeError", "ClipboardUnsanitizedFormats.unsanitized must be a sequence");
            }
        }
    }

    // Snapshot the C store, wrap each value in a Blob.
    Item recs = js_lambda_clipboard_read_records();
    Item out = js_array_new(0);
    if (get_type_id(recs) == LMD_TYPE_ARRAY) {
        int64_t n = js_array_length(recs);
        for (int64_t i = 0; i < n; i++) {
            Item rec = js_elements_get_int(recs, i);
            if (get_type_id(rec) != LMD_TYPE_MAP) continue;
            Item keys = js_object_keys(rec);
            int64_t nk = (get_type_id(keys) == LMD_TYPE_ARRAY) ? js_array_length(keys) : 0;
            Item wrapped = js_new_object();
            for (int64_t j = 0; j < nk; j++) {
                Item k = js_elements_get_int(keys, j);
                Item v = js_get_key_default(rec, k);
                if (get_type_id(k) == LMD_TYPE_STRING &&
                    get_type_id(v) == LMD_TYPE_STRING) {
                    String* ks = it2s(k);
                    String* vs = it2s(v);
                    if (ks && vs && ks->len == 9 &&
                        memcmp(ks->chars, "text/html", 9) == 0) {
                        StrBuf* sb = strbuf_new();
                        strip_html_script_style(sb, vs->chars, vs->len);
                        v = make_str_n(sb->str ? sb->str : "", sb->length);
                        strbuf_free(sb);
                    }
                }
                Item parts = js_array_new(0);
                js_array_push(parts, v);
                Item bopts = js_new_object();
                js_set_key_cstr(bopts, "type", k);
                Item blob = js_blob_new(parts, bopts);
                js_set_key_default(wrapped, k, blob);
            }
            Item ci = js_clipboard_item_new(wrapped, ItemNull);
            js_set_key_cstr(ci, "_clipboard_generation", (Item){.item = i2it(g_clipboard_generation)});
            js_array_push(out, ci);
        }
    }
    return js_promise_resolve(out);
}

// =============================================================================
// navigator.permissions
// =============================================================================

extern "C" Item js_permissions_query(Item desc) {
    Item status = js_new_object_with_class(JS_CLASS_PERMISSION_STATUS);
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
            js_set_key_cstr(status, "name", make_str(nm));
        }
    }
    js_set_key_cstr(status, "state", make_str(state));
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
    g_clipboard_generation++;
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
                mem_free(e->mime);
                mem_free(e->data);
                mem_free(e);
            }
            arraylist_free(it->entries);
        }
        mem_free(it);
    }
    arraylist_free(items);
}

extern "C" Item js_lambda_clipboard_write_records(Item arr) {
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        clipboard_store_clear();
        g_clipboard_generation++;
        return ItemNull;
    }
    int64_t n = js_array_length(arr);
    ArrayList* items = arraylist_new(n > 0 ? (int)n : 1);

    // js_object_keys forward-declared at top of file
    for (int64_t i = 0; i < n; i++) {
        Item rec = js_elements_get_int(arr, i);
        if (get_type_id(rec) != LMD_TYPE_MAP) continue;

        ClipboardItem* citem = (ClipboardItem*)mem_calloc(1, sizeof(ClipboardItem), MEM_CAT_JS_RUNTIME);
        if (!citem) continue;
        citem->entries = arraylist_new(4);
        citem->is_unsanitized = 0;

        Item keys = js_object_keys(rec);
        int64_t nk = (get_type_id(keys) == LMD_TYPE_ARRAY) ? js_array_length(keys) : 0;
        for (int64_t j = 0; j < nk; j++) {
            Item k = js_elements_get_int(keys, j);
            if (get_type_id(k) != LMD_TYPE_STRING) continue;
            String* ks = it2s(k);
            if (!ks || ks->len == 0) continue;
            Item v = js_get_key_default(rec, k);
            if (get_type_id(v) != LMD_TYPE_STRING) continue;
            String* vs = it2s(v);
            if (!vs) continue;

            ClipboardEntry* ce = (ClipboardEntry*)mem_calloc(1, sizeof(ClipboardEntry), MEM_CAT_JS_RUNTIME);
            if (!ce) continue;
            ce->mime = (char*)mem_alloc(ks->len + 1, MEM_CAT_JS_RUNTIME);
            memcpy(ce->mime, ks->chars, ks->len);
            ce->mime[ks->len] = '\0';
            ce->data_len = vs->len;
            ce->data = (char*)mem_alloc(vs->len + 1, MEM_CAT_JS_RUNTIME);
            memcpy(ce->data, vs->chars, vs->len);
            ce->data[vs->len] = '\0';
            arraylist_append(citem->entries, ce);
        }
        arraylist_append(items, citem);
    }

    clipboard_store_write_items(items);
    g_clipboard_generation++;

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
                js_set_key_default(rec, key_item, val_item);
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

// Every global interface installed below is a native constructor with a fresh
// prototype object, cross-linked and published on globalThis under its own
// name. The prototype is written through `out_proto` before this frame ends —
// the callers' g_*_proto slots are registered GC roots — so a caller can go on
// adding methods to it or chaining it.
template <typename Ctor>
static Item js_clipboard_install_interface(Item global, const char* name,
                                           Ctor constructor, Item* out_proto) {
    JS_ROOTS(roots,
        ctor_root, js_new_native_constructor(constructor),
        proto_root, ItemNull);
    js_set_function_name(ctor_root.get(), make_str(name));
    proto_root.set(js_new_object());
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_set_key_cstr(ctor_root.get(), "prototype", proto_root.get());
    if (out_proto) *out_proto = proto_root.get();
    js_set_key_cstr(global, name, ctor_root.get());
    return ctor_root.get();
}

extern "C" void js_register_clipboard_globals(Item global_this) {
    if (!clipboard_ensure_roots()) return;
#define JS_CLIPBOARD_BLOB_METHODS(M) \
    M("text", js_blob_text) M("arrayBuffer", js_blob_array_buffer) M("slice", js_blob_slice)
#define JS_CLIPBOARD_INSTALL_PROTO_METHOD(name, target) \
    js_clipboard_set_method(proto_root.get(), name, target);
    // D5.3/D5.4.3: registration allocates while publishing each property, so
    // constructor/prototype locals must remain exact roots until publication.
    RootFrame global_roots(1);
    Rooted<Item> global_root(global_roots, global_this);
    // ---- Blob -------------------------------------------------------------
    js_clipboard_install_interface(global_root.get(), "Blob", js_blob_new, &g_blob_proto);
#define JS_CLIPBOARD_INSTALL_BLOB_METHOD(name, target) \
    js_clipboard_set_method(g_blob_proto, name, target);
    JS_CLIPBOARD_BLOB_METHODS(JS_CLIPBOARD_INSTALL_BLOB_METHOD)
#undef JS_CLIPBOARD_INSTALL_BLOB_METHOD

    // ---- File -------------------------------------------------------------
    js_clipboard_install_interface(global_root.get(), "File", js_file_new, &g_file_proto);
    if (get_type_id(g_blob_proto) == LMD_TYPE_MAP) {
        js_set_prototype(g_file_proto, g_blob_proto);
    }

    // ---- ClipboardItem ---------------------------------------------------
    {
        Item ctor = js_clipboard_install_interface(global_root.get(), "ClipboardItem",
            js_clipboard_item_new, &g_clipboard_item_proto);
        js_clipboard_set_method(g_clipboard_item_proto, "getType", js_clipboard_item_get_type);
        js_clipboard_set_method(ctor, "supports", js_clipboard_item_supports);
    }

    // ---- FileList --------------------------------------------------------
    {
        RootFrame roots(4);
        Rooted<Item> ctor_root(roots,
            js_new_native_constructor(js_file_list_new));
        Rooted<Item> proto_root(roots, ItemNull);
        Rooted<Item> method_root(roots, ItemNull);
        Rooted<Item> array_proto_root(roots, ItemNull);
        js_set_function_name(ctor_root.get(), make_str("FileList"));
        proto_root.set(js_new_object());
        js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
        method_root.set(js_new_native_function(js_dt_files_item));
        js_set_key_cstr(proto_root.get(), "item", method_root.get());
        js_set_key_default(proto_root.get(), js_well_known_symbol_key(4),
            make_str("FileList"));
        array_proto_root.set(
            js_get_intrinsic_prototype_for_class(JS_CLASS_ARRAY));
        if (get_type_id(array_proto_root.get()) == LMD_TYPE_MAP) {
            js_set_prototype(proto_root.get(), array_proto_root.get());
        }
        js_initialize_native_constructor_prototype(ctor_root.get(),
            proto_root.get());
        // D5.4.3/D6.2.2v2: FileList arrays and the realm constructor must share
        // the same precisely rooted prototype; otherwise a nursery relocation
        // splits instanceof identity from the public constructor property.
        g_file_list_proto = proto_root.get();
        js_set_key_cstr(global_root.get(), "FileList", ctor_root.get());
    }

    // ---- ClipboardEvent --------------------------------------------------
    js_clipboard_install_interface(global_root.get(), "ClipboardEvent",
        js_clipboard_event_new, &g_clipboard_event_proto);

    // ---- DataTransfer ----------------------------------------------------
    js_clipboard_install_interface(global_root.get(), "DataTransfer",
        js_data_transfer_new, &g_data_transfer_proto);

    // ---- Clipboard (instanceof + prototype) -----------------------------
    // Real Web platform exposes `Clipboard` as a class. We register a
    // ctor + prototype so `nav.clipboard instanceof Clipboard` works and
    // `Clipboard.prototype.{write,read,readText,writeText}` is observable.
    RootFrame clipboard_roots(1);
    Rooted<Item> clipboard_proto_root(clipboard_roots, ItemNull);
    {
        Item clipboard_proto = ItemNull;
        // js_data_transfer_new is a placeholder body: Clipboard is exposed for
        // instanceof and prototype observability, never constructed directly.
        js_clipboard_install_interface(global_root.get(), "Clipboard",
            js_data_transfer_new, &clipboard_proto);
        clipboard_proto_root.set(clipboard_proto);
        js_clipboard_set_method(clipboard_proto_root.get(), "writeText", js_clipboard_write_text);
        js_clipboard_set_method(clipboard_proto_root.get(), "readText", js_clipboard_read_text);
        js_clipboard_set_method(clipboard_proto_root.get(), "write", js_clipboard_write);
        js_clipboard_set_method(clipboard_proto_root.get(), "read", js_clipboard_read);
    }

    // ---- navigator -------------------------------------------------------
    // Bridges to the C ClipboardStore so the WPT shim's `_wpt_clipboard_*`
    // helpers and the synthetic Cmd+C/V keyboard handler share the same
    // underlying store as `navigator.clipboard.{readText,writeText}`.
    js_clipboard_set_method(global_root.get(), "__lambda_clipboard_clear",
        js_lambda_clipboard_clear);
    js_clipboard_set_method(global_root.get(), "__lambda_clipboard_write_records",
        js_lambda_clipboard_write_records);
    js_clipboard_set_method(global_root.get(), "__lambda_clipboard_read_records",
        js_lambda_clipboard_read_records);
    js_clipboard_set_method(global_root.get(), "__lambda_clipboard_set_perm",
        js_lambda_clipboard_set_perm);
    js_clipboard_set_method(global_root.get(), "__lambda_clipboard_get_perm",
        js_lambda_clipboard_get_perm);

    // navigator + navigator.clipboard backed by C store. Methods come from
    // Clipboard.prototype above (writeText/readText/write/read). We also
    // copy the methods directly onto the instance so simple property reads
    // resolve without prototype-chain lookup (matches what test code
    // typically does and the shim's previous direct-assignment behaviour).
    {
        RootFrame roots(3);
        Rooted<Item> clipboard_root(roots,
            js_new_object_with_class(JS_CLASS_CLIPBOARD));
        Rooted<Item> permissions_root(roots, ItemNull);
        Rooted<Item> navigator_root(roots, ItemNull);
        js_set_key_cstr(clipboard_root.get(), "writeText", js_get_key_cstr(clipboard_proto_root.get(), "writeText"));
        js_set_key_cstr(clipboard_root.get(), "readText", js_get_key_cstr(clipboard_proto_root.get(), "readText"));
        js_set_key_cstr(clipboard_root.get(), "write", js_get_key_cstr(clipboard_proto_root.get(), "write"));
        js_set_key_cstr(clipboard_root.get(), "read", js_get_key_cstr(clipboard_proto_root.get(), "read"));

        permissions_root.set(js_new_object());
        js_clipboard_set_method(permissions_root.get(), "query", js_permissions_query);

        navigator_root.set(js_new_object());
        js_set_key_cstr(navigator_root.get(), "clipboard", clipboard_root.get());
        js_set_key_cstr(navigator_root.get(), "permissions", permissions_root.get());
        js_set_key_cstr(navigator_root.get(), "platform", make_str("MacIntel"));
        js_set_key_cstr(navigator_root.get(), "userAgent", make_str("Lambda/Headless (Macintosh)"));
        // Browser capability probes call appName before inspecting SVG support;
        // leaving this legacy Navigator string absent makes ordinary method
        // access throw before the probe can select its rendering path.
        js_set_key_cstr(navigator_root.get(), "appName", make_str("Netscape"));
        // Legacy UA-sniffing libraries still call string methods on
        // Navigator.appVersion; keep it present and consistent with this host.
        js_set_key_cstr(navigator_root.get(), "appVersion", make_str("5.0 (Macintosh) Lambda/Headless"));
        js_set_key_cstr(navigator_root.get(), "vendor", make_str(""));
        js_set_key_cstr(navigator_root.get(), "language", make_str("en-US"));
        // Radiant exposes PointerEvent input in both interactive and headless
        // hosts, so feature detection must advertise at least one touch-capable
        // pointer; otherwise libraries never register their pointer handlers.
        js_set_key_cstr(navigator_root.get(), "maxTouchPoints", (Item){.item = i2it(1)});
        js_set_key_cstr(global_root.get(), "navigator", navigator_root.get());
    }
#undef JS_CLIPBOARD_INSTALL_PROTO_METHOD
#undef JS_CLIPBOARD_BLOB_METHODS
}
