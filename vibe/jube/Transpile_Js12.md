# Transpile_Js12: Language Completeness — Phase 3 (Rest Destructuring, Symbol, Globals, DOM)

## 1. Executive Summary

After v11, LambdaJS covers ES6 classes, prototype OOP, multi-level closures, typed arrays, optional chaining, Map/Set, error subclasses, regex, and 90+ standard library methods. This proposal fills the remaining **language gaps** and **DOM gaps** identified in the JS_DOM_Support feature matrix.

**v12 goal:** Two parallel tracks:
- **Track A — Language:** Destructuring rest (`...rest`), `Symbol` API, `globalThis`, `encodeURIComponent` / `decodeURIComponent`
- **Track B — DOM:** `document.URL` / `document.location`, element `outerHTML`, `classList`, `dataset`, `contains`, style `cssText`

### v12 Feature Overview

| Item | Track | Priority | Est. LOC | Status | Notes |
|------|:-----:|:--------:|:--------:|:------:|-------|
| A1 Destructuring rest (`...rest`) | Lang | HIGH | ~120 | ✅ | Object & array rest in declarations, assignments, and function params |
| A2 `Symbol` API | Lang | MEDIUM | ~250 | ✅ | `Symbol()`, `Symbol.for()`, `Symbol.keyFor()`, well-known symbols |
| A3 `globalThis` | Lang | LOW | ~20 | ✅ | Alias for the JS global object |
| A4 `encodeURIComponent` / `decodeURIComponent` | Lang | MEDIUM | ~80 | ✅ | Reuse `lib/url.c` infrastructure |
| B1 `document.URL` / `document.location` | DOM | MEDIUM | ~150 | ✅ | Reuse `lib/url.c` + `lib/url_parser.c` |
| B2 Element `outerHTML` | DOM | LOW | ~60 | ✅ | Serialize element + descendants to HTML string |
| B3 Element `classList` | DOM | HIGH | ~180 | ✅ | `add`, `remove`, `toggle`, `contains`, `item`, `length` |
| B4 Element `dataset` | DOM | MEDIUM | ~100 | ✅ | `data-*` attribute proxy with camelCase conversion |
| B5 Element `contains` | DOM | LOW | ~30 | ✅ | Subtree containment check |
| B6 Style `cssText` | DOM | LOW | ~50 | ✅ | Get/set inline style as raw CSS string |

**Total estimated:** ~1,040 LOC across `transpile_js_mir.cpp`, `js_runtime.cpp`, `js_globals.cpp`, `js_dom.cpp`, `lib/url.c`

**Status:** ✅ All 10 v12 features implemented and passing (669/669 Lambda baseline, 52/52 JS tests). ~949 LOC added across 10 files.

---

## 2. Track A — Language Features

### A1: Destructuring Rest (`...rest`) — Object & Array (Priority: HIGH)

**Current state:** The AST nodes `JS_AST_NODE_REST_ELEMENT` and `JS_AST_NODE_SPREAD_ELEMENT` are parsed, and `JsSpreadElementNode` exists in `js_ast.hpp`. Array rest `[a, b, ...rest]` already works via `js_array_slice_from()`. **Object rest `{a, b, ...rest}` is parsed but not transpiled** — there is a `log_debug` placeholder: "object destructuring: rest property not yet implemented."

#### A1a: Object Rest in Declarations

**Problem:** `const {a, b, ...rest} = obj` should bind `rest` to a new object containing all properties of `obj` except `a` and `b`. Currently skipped.

**Approach:** After extracting the named properties, create a new object and copy all remaining properties.

**New runtime function** (`js_runtime.cpp`):
```c
// Create a new object with all properties from src except those in exclude_keys
Item js_object_rest(Item src, Item* exclude_keys, int exclude_count);
```

**Implementation:**
```c
extern "C" Item js_object_rest(Item src, Item* exclude_keys, int exclude_count) {
    if (get_type_id(src) != LMD_TYPE_MAP) return js_new_object();
    Map* m = it2map(src);
    TypeMap* tm = (TypeMap*)m->type;
    ShapeEntry* e = tm->shape;
    Item rest = js_new_object();
    while (e) {
        bool excluded = false;
        for (int i = 0; i < exclude_count; i++) {
            String* ek = it2s(exclude_keys[i]);
            if (e->name->len == ek->len && memcmp(e->name->chars, ek->chars, ek->len) == 0) {
                excluded = true;
                break;
            }
        }
        if (!excluded) {
            Item val = _map_read_field(m, e);
            js_property_set(rest, s2it(e->name), val);
        }
        e = e->next;
    }
    return rest;
}
```

**Transpiler changes** (`transpile_js_mir.cpp`, in object destructuring handler ~line 7383):
```
// When encountering REST_ELEMENT / REST_PROPERTY in object pattern:
1. Collect all explicit property key names extracted so far into an Item[] array
2. Emit: MIR_reg_t exclude_arr = alloca(exclude_count * sizeof(Item))
3. Store each key name Item into the array
4. rest_reg = jm_call_3(mt, "js_object_rest", MIR_T_I64,
       src_reg, exclude_arr_reg, exclude_count)
5. Bind rest_reg to the rest variable
```

#### A1b: Array Rest in Function Parameters

**Problem:** `function f(a, b, ...rest) {}` — rest parameters. The parser produces a `JsSpreadElementNode` in the params list. The transpiler handles spread in function calls but not in parameter declarations.

**Current state:** Rest parameters in function declarations may already work partially (the transpiler handles `arguments` slicing). Verify and complete:

**Transpiler changes** (`transpile_js_mir.cpp`, function parameter binding):
```
// When the last parameter node is REST_ELEMENT:
1. Bind regular params a, b to args[0], args[1]
2. rest = js_array_slice_from(arguments_array, named_param_count)
3. Bind rest variable to the sliced array
```

The runtime function `js_array_slice_from(Item arr, Item start)` already exists and handles this correctly.

#### A1c: Object Rest in Assignments

**Problem:** `({a, ...rest} = obj)` in assignment position (not declaration).

**Approach:** Same as A1a, but assign to existing variables instead of declaring new ones. The transpiler already handles assignment destructuring for arrays; extend the object path similarly.

**Code locations:**
- `lambda/js/transpile_js_mir.cpp` — object destructuring in declarations (~line 7383), assignment destructuring
- `lambda/js/js_runtime.cpp` — new `js_object_rest()` function
- `lambda/js/js_runtime.h` — declare `js_object_rest`

**Test cases:**
```javascript
// test/js/test_rest_destructuring.js
const {a, b, ...rest} = {a: 1, b: 2, c: 3, d: 4};
console.log(a);    // 1
console.log(b);    // 2
console.log(rest.c); // 3
console.log(rest.d); // 4

const [x, ...ys] = [10, 20, 30, 40];
console.log(x);           // 10
console.log(ys.length);   // 3
console.log(ys[2]);       // 40

function sum(first, ...nums) {
    return nums.reduce((a, b) => a + b, first);
}
console.log(sum(1, 2, 3, 4)); // 10

// Nested
const {a: {x: px, ...innerRest}, ...outerRest} = {a: {x: 1, y: 2, z: 3}, b: 4};
console.log(px);              // 1
console.log(innerRest.y);     // 2
console.log(outerRest.b);     // 4
```

**Estimated effort:** ~120 LOC across transpile_js_mir.cpp and js_runtime.cpp.

---

### A2: `Symbol` API (Priority: MEDIUM)

**Problem:** `Symbol` is used in many JS patterns — unique property keys, well-known protocols (`Symbol.iterator`, `Symbol.toPrimitive`), and framework-level metaprogramming.

**Approach:** Implement `Symbol` as a new runtime-level unique value type. Since Lambda's `Item` is a 64-bit tagged value, symbols can be represented as a unique integer ID with a `LMD_TYPE_SYMBOL`-like tag (Lambda already has a symbol type in its data model).

#### Design

**Runtime representation:**
- Each `Symbol()` call produces a unique 64-bit ID (monotonically increasing counter)
- `Symbol.for(key)` uses a global registry (HashMap from string key → Symbol ID)
- `Symbol.keyFor(sym)` performs reverse lookup
- Well-known symbols are pre-allocated at startup with fixed IDs

**New data structure** (`js_runtime.h`):
```c
typedef struct JsSymbolRegistry {
    HashMap* name_to_id;     // string key → symbol ID
    HashMap* id_to_name;     // symbol ID → string key (for keyFor)
    uint64_t next_id;        // monotonic counter
} JsSymbolRegistry;
```

**New runtime functions** (`js_globals.cpp`):
```c
// Symbol() — create a unique symbol, optional description
extern "C" Item js_symbol_create(Item description);

// Symbol.for(key) — global registry lookup-or-create
extern "C" Item js_symbol_for(Item key);

// Symbol.keyFor(sym) — reverse lookup from global registry
extern "C" Item js_symbol_key_for(Item sym);

// typeof sym → "symbol"
// String(sym) → "Symbol(description)"
extern "C" Item js_symbol_to_string(Item sym);
```

**Well-known symbols** (pre-allocated at init, IDs 1–10):
```c
Item JS_SYMBOL_ITERATOR;      // Symbol.iterator
Item JS_SYMBOL_TO_PRIMITIVE;  // Symbol.toPrimitive
Item JS_SYMBOL_HAS_INSTANCE;  // Symbol.hasInstance
Item JS_SYMBOL_TO_STRING_TAG; // Symbol.toStringTag
```

**Transpiler changes** (`transpile_js_mir.cpp`):

1. **`Symbol()` call:** dispatch to `js_symbol_create`
2. **`Symbol.for(key)` call:** dispatch to `js_symbol_for`
3. **`Symbol.keyFor(sym)` call:** dispatch to `js_symbol_key_for`
4. **`Symbol.iterator` property access:** return pre-allocated constant
5. **`typeof sym`:** return `"symbol"` when Item is a symbol type
6. **Property keyed by symbol:** extend `js_property_set` / `js_property_get` to accept symbol-typed keys (store as numeric key in a separate slot or use the symbol's ID as a string key prefix like `"@@sym:42"`)

**Symbol as property key — implementation strategy:**

To avoid invasive changes to `TypeMap`/`ShapeEntry` (which expect `String*` keys), use a **symbol key encoding**:
```c
// When setting obj[sym] = val:
// Convert symbol ID to an internal string key: "@@sym:<id>"
// This avoids collision with any user string key
static String* js_symbol_to_property_key(Item sym) {
    char buf[32];
    snprintf(buf, sizeof(buf), "@@sym:%llu", js_symbol_id(sym));
    return heap_create_name(buf, strlen(buf));
}
```

This piggybacks on existing property infrastructure while keeping symbols unique and non-enumerable (check prefix in `Object.keys` to exclude them).

**Code locations:**
- `lambda/js/js_globals.cpp` — symbol registry + runtime functions
- `lambda/js/js_runtime.h` — function declarations, `JsSymbolRegistry`
- `lambda/js/transpile_js_mir.cpp` — compile-time dispatch for `Symbol()`, `Symbol.for()`, `Symbol.iterator` etc.
- `lambda/js/js_runtime.cpp` — extend `typeof` dispatch, property key handling

**Test cases:**
```javascript
// test/js/test_symbol.js
const s1 = Symbol("foo");
const s2 = Symbol("foo");
console.log(s1 === s2);             // false (unique)
console.log(typeof s1);             // "symbol"
console.log(String(s1));            // "Symbol(foo)"

const gs1 = Symbol.for("app.id");
const gs2 = Symbol.for("app.id");
console.log(gs1 === gs2);           // true (same registry entry)
console.log(Symbol.keyFor(gs1));    // "app.id"
console.log(Symbol.keyFor(s1));     // undefined (not in registry)

// Symbol as property key
const key = Symbol("myKey");
const obj = {};
obj[key] = 42;
console.log(obj[key]);              // 42
console.log(Object.keys(obj).length); // 0 (symbols not enumerable)

// Well-known symbols
console.log(typeof Symbol.iterator);     // "symbol"
console.log(typeof Symbol.toPrimitive);  // "symbol"
```

**Estimated effort:** ~250 LOC across js_globals.cpp, js_runtime.cpp, transpile_js_mir.cpp.

---

### A3: `globalThis` (Priority: LOW)

**Problem:** `globalThis` is the standard way to access the global object in any JS environment (ES2020). Some libraries use it for universal compatibility.

**Approach:** `globalThis` is a read-only global that evaluates to the JS global scope object. Since LambdaJS uses a flat global scope (not a global object), the simplest approach is to create a lazily-initialized global object that mirrors top-level bindings.

**Implementation — minimal viable approach:**

Create a singleton global object at startup that proxies property access to global variables:

**New runtime function** (`js_globals.cpp`):
```c
static Item js_global_this = {0};

extern "C" Item js_get_global_this() {
    if (js_global_this.u64 == 0) {
        js_global_this = js_new_object();
        // Populate with standard globals
        js_property_set(js_global_this, s2it("undefined"), ITEM_JS_UNDEFINED);
        js_property_set(js_global_this, s2it("NaN"), js_make_number(NAN));
        js_property_set(js_global_this, s2it("Infinity"), js_make_number(INFINITY));
        // Math, JSON, Object, Array, etc. are added as needed
    }
    return js_global_this;
}
```

**Transpiler changes** (`transpile_js_mir.cpp`):

In the global variable resolution path, recognize `globalThis` as a special identifier:
```cpp
// In jm_transpile_identifier_or_global:
if (nl == 10 && strncmp(n, "globalThis", 10) == 0) {
    return jm_call_0(mt, "js_get_global_this", MIR_T_I64);
}
```

**Code locations:**
- `lambda/js/js_globals.cpp` — `js_get_global_this()`
- `lambda/js/js_runtime.h` — declare it
- `lambda/js/transpile_js_mir.cpp` — identifier resolution

**Test cases:**
```javascript
// test/js/test_global_this.js
console.log(typeof globalThis);           // "object"
console.log(globalThis !== undefined);    // true
console.log(globalThis.NaN !== globalThis.NaN); // true (NaN !== NaN)
```

**Estimated effort:** ~20 LOC.

---

### A4: `encodeURIComponent` / `decodeURIComponent` (Priority: MEDIUM)

**Problem:** These are fundamental global functions for URL encoding/decoding, used in any web-related JS code.

**Approach:** Reuse `lib/url.c` infrastructure. The file already has `hex_to_int()` and `url_decode()` (static). We will:
1. Add **public** `url_encode_component()` and `url_decode_component()` functions to `lib/url.c` / `lib/url.h`
2. Call them from `js_globals.cpp` runtime functions
3. Wire compile-time dispatch in the transpiler

#### Step 1: New functions in `lib/url.c`

```c
// Percent-encode a string per RFC 3986 Section 2.3
// Unreserved characters (not encoded): A-Z a-z 0-9 - _ . ~ ! ' ( ) *
// All other bytes are encoded as %XX
char* url_encode_component(const char* str, size_t len);

// Percent-decode a string: %XX → byte
// Returns a newly allocated string. Caller must free.
char* url_decode_component(const char* str, size_t len, size_t* out_len);
```

The `encodeURIComponent` unreserved set per ECMAScript spec (Section 19.2.6.5):
```
A-Z a-z 0-9 - _ . ~ ! ' ( ) *
```

**Implementation** (`lib/url.c`):
```c
char* url_encode_component(const char* str, size_t len) {
    if (!str) return NULL;
    // Worst case: every byte becomes %XX (3 bytes)
    char* encoded = malloc(len * 3 + 1);
    if (!encoded) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~' || c == '!' || c == '\'' ||
            c == '(' || c == ')' || c == '*') {
            encoded[j++] = c;
        } else {
            encoded[j++] = '%';
            encoded[j++] = "0123456789ABCDEF"[c >> 4];
            encoded[j++] = "0123456789ABCDEF"[c & 0x0F];
        }
    }
    encoded[j] = '\0';
    return encoded;
}

char* url_decode_component(const char* str, size_t len, size_t* out_len) {
    if (!str) return NULL;
    char* decoded = malloc(len + 1);
    if (!decoded) return NULL;
    size_t i = 0, j = 0;
    while (i < len) {
        if (str[i] == '%' && i + 2 < len) {
            int high = hex_to_int(str[i + 1]);
            int low  = hex_to_int(str[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded[j++] = (char)((high << 4) | low);
                i += 3;
                continue;
            }
        }
        decoded[j++] = str[i++];
    }
    decoded[j] = '\0';
    if (out_len) *out_len = j;
    return decoded;
}
```

Note: `hex_to_int()` is currently `static` in `url.c`. It should be made non-static (or duplicated) so `url_decode_component` can use it alongside the existing `url_decode`.

**Declare in** `lib/url.h`:
```c
char* url_encode_component(const char* str, size_t len);
char* url_decode_component(const char* str, size_t len, size_t* out_len);
```

#### Step 2: JS runtime wrappers (`js_globals.cpp`)

```c
extern "C" Item js_encodeURIComponent(Item str_item) {
    String* s = it2s(js_to_string(str_item));
    char* encoded = url_encode_component(s->chars, s->len);
    if (!encoded) return ITEM_JS_UNDEFINED;
    Item result = s2it(heap_create_name(encoded, strlen(encoded)));
    free(encoded);
    return result;
}

extern "C" Item js_decodeURIComponent(Item str_item) {
    String* s = it2s(js_to_string(str_item));
    size_t decoded_len = 0;
    char* decoded = url_decode_component(s->chars, s->len, &decoded_len);
    if (!decoded) return ITEM_JS_UNDEFINED;
    Item result = s2it(heap_create_name(decoded, decoded_len));
    free(decoded);
    return result;
}
```

#### Step 3: Transpiler dispatch (`transpile_js_mir.cpp`)

Add to the global function resolution block (after `isFinite` / `isNaN` / `parseInt` / `parseFloat`):
```cpp
if (nl == 19 && strncmp(n, "encodeURIComponent", 19) == 0 && argc >= 1) {
    MIR_reg_t arg = jm_transpile_box_item(mt, call->arguments);
    return jm_call_1(mt, "js_encodeURIComponent", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
}
if (nl == 19 && strncmp(n, "decodeURIComponent", 19) == 0 && argc >= 1) {
    MIR_reg_t arg = jm_transpile_box_item(mt, call->arguments);
    return jm_call_1(mt, "js_decodeURIComponent", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
}
```

Note: both `encodeURIComponent` and `decodeURIComponent` are 19 characters (excluding null terminator). `encodeURIComponent` is 18 chars, `decodeURIComponent` is 18 chars — verify exact lengths during implementation.

**Code locations:**
- `lib/url.c` — new public `url_encode_component`, `url_decode_component`; promote `hex_to_int` to non-static
- `lib/url.h` — declare new functions
- `lambda/js/js_globals.cpp` — `js_encodeURIComponent`, `js_decodeURIComponent`
- `lambda/js/js_runtime.h` — declare JS wrappers
- `lambda/js/transpile_js_mir.cpp` — compile-time dispatch

**Test cases:**
```javascript
// test/js/test_encode_uri.js
console.log(encodeURIComponent("hello world"));           // "hello%20world"
console.log(encodeURIComponent("foo@bar.com"));           // "foo%40bar.com"
console.log(encodeURIComponent("a=1&b=2"));               // "a%3D1%26b%3D2"
console.log(encodeURIComponent("café"));                  // "caf%C3%A9"
console.log(encodeURIComponent("-_.~!*'()"));             // "-_.~!*'()" (unreserved)

console.log(decodeURIComponent("hello%20world"));         // "hello world"
console.log(decodeURIComponent("foo%40bar.com"));         // "foo@bar.com"
console.log(decodeURIComponent("a%3D1%26b%3D2"));         // "a=1&b=2"

// Round-trip
const original = "hello world & café!";
console.log(decodeURIComponent(encodeURIComponent(original)) === original); // true
```

**Estimated effort:** ~80 LOC across url.c, js_globals.cpp, transpile_js_mir.cpp.

---

## 3. Track B — DOM Enhancements

### B1: `document.URL` / `document.location` (Priority: MEDIUM)

**Problem:** `document.URL` and `document.location` are commonly accessed in web JS code. `document.location` also provides sub-properties like `.href`, `.hostname`, `.pathname`, `.search`, `.hash`.

**Approach:** Reuse the `Url` struct from `lib/url.c` and `lib/url_parser.c`. The Radiant engine already has a base URL concept for resolving relative URLs in HTML. Expose it to JS as `document.URL` (string) and `document.location` (object with component properties).

#### Representation

Create a **location wrapper** using the same type-marker pattern as DOM elements:

```c
// js_dom.cpp
static TypeMap js_location_marker = {};

Item js_dom_create_location(const char* url_string) {
    Url* url = url_parse(url_string);
    if (!url) url = url_create(); // empty fallback
    Map* wrapper = heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type = (void*)&js_location_marker;
    wrapper->data = url;
    return (Item){.map = wrapper};
}

bool js_is_location(Item item) {
    return get_type_id(item) == LMD_TYPE_MAP &&
           item.map->type == (void*)&js_location_marker;
}
```

#### Location Property Access

```c
Item js_location_get_property(Item location, const char* prop, int prop_len) {
    Url* url = (Url*)it2map(location)->data;
    if (!url) return ITEM_JS_UNDEFINED;

    if (MATCH("href"))     return s2it(heap_create_name(url_get_href(url)));
    if (MATCH("hostname")) return s2it(heap_create_name(url_get_hostname(url)));
    if (MATCH("host"))     return s2it(heap_create_name(url_get_host(url)));
    if (MATCH("pathname")) return s2it(heap_create_name(url_get_pathname(url)));
    if (MATCH("search"))   return s2it(heap_create_name(url_get_search(url)));
    if (MATCH("hash"))     return s2it(heap_create_name(url_get_hash(url)));
    if (MATCH("protocol")) return s2it(heap_create_name(url_get_protocol(url)));
    if (MATCH("port"))     return s2it(heap_create_name(url_get_port(url)));
    if (MATCH("origin"))   return s2it(heap_create_name(url_get_origin(url)));
    if (MATCH("toString")) {
        // Return a function that returns href
    }
    return ITEM_JS_UNDEFINED;
}
```

#### Document Property Dispatch

In the existing document property handler in `js_dom.cpp`, add:

```c
// In js_dom_document_get_property:
if (MATCH("URL")) {
    // Return the document's URL as a string
    const char* url = radiant_get_document_url(dom_context);
    return url ? s2it(heap_create_name(url, strlen(url))) : s2it(heap_create_name("about:blank", 11));
}
if (MATCH("location")) {
    const char* url = radiant_get_document_url(dom_context);
    return js_dom_create_location(url ? url : "about:blank");
}
```

#### Integration with Radiant

The Radiant engine's `DomNode` tree root or the HTML context should store the document URL. Add a getter if not already present:

```c
// radiant/view.hpp or radiant context
const char* radiant_get_document_url(void* context);
```

If the document is loaded from a file, this returns the `file://` URL. If loaded from an HTTP source (via `input-http`), it returns the original URL.

**Code locations:**
- `lambda/js/js_dom.cpp` — location wrapper, document property dispatch
- `lambda/js/js_dom.h` — declare new functions
- `lib/url.c`, `lib/url.h` — already complete, reuse `url_parse()`, `url_get_*()` getters
- `radiant/` — expose document URL from context if not already available

**Test cases:**
```javascript
// test/js/test_document_url.js (requires HTML context)
console.log(typeof document.URL);              // "string"
console.log(typeof document.location);         // "object"
console.log(document.location.protocol);       // "file:" or "http:"
console.log(document.location.pathname);       // "/path/to/file.html"
console.log(document.location.href === document.URL); // true
```

**Estimated effort:** ~150 LOC across js_dom.cpp, js_dom.h. Zero changes needed in lib/url.c (existing API sufficient).

---

### B2: Element `outerHTML` (Priority: LOW)

**Problem:** `element.outerHTML` returns the serialized HTML of an element including its tag, attributes, and children. Currently only `innerHTML` (getter only) is implemented.

**Approach:** Serialize the element as HTML: open tag with attributes → `innerHTML` → close tag.

**Implementation** (`js_dom.cpp`):

```c
Item js_dom_get_outer_html(void* dom_node) {
    DomElement* el = (DomElement*)dom_node;
    StrBuf sb;
    strbuf_init(&sb, 256);

    // Opening tag
    strbuf_append_char(&sb, '<');
    strbuf_append_str(&sb, el->tag_name);

    // Attributes
    for (int i = 0; i < el->attr_count; i++) {
        strbuf_append_char(&sb, ' ');
        strbuf_append_str(&sb, el->attrs[i].name);
        strbuf_append_str(&sb, "=\"");
        // Escape attribute value (& → &amp;, " → &quot;, etc.)
        html_escape_attribute(&sb, el->attrs[i].value);
        strbuf_append_char(&sb, '"');
    }
    strbuf_append_char(&sb, '>');

    // innerHTML (existing function)
    js_dom_serialize_children(&sb, dom_node);

    // Closing tag (skip for void elements: br, hr, img, input, etc.)
    if (!is_void_element(el->tag_name)) {
        strbuf_append_str(&sb, "</");
        strbuf_append_str(&sb, el->tag_name);
        strbuf_append_char(&sb, '>');
    }

    String* result = heap_create_name(sb.str, sb.len);
    strbuf_free(&sb);
    return s2it(result);
}
```

**Integration:** Add to the element property dispatcher in `js_dom.cpp`:
```c
if (MATCH("outerHTML")) return js_dom_get_outer_html(dom_node);
```

**Code locations:**
- `lambda/js/js_dom.cpp` — `js_dom_get_outer_html`, element property dispatch

**Test cases:**
```javascript
// test/js/test_outer_html.js
var div = document.createElement("div");
div.setAttribute("id", "test");
div.setAttribute("class", "foo bar");
div.textContent = "hello";
console.log(div.outerHTML); // '<div id="test" class="foo bar">hello</div>'

var br = document.createElement("br");
console.log(br.outerHTML);  // '<br>'
```

**Estimated effort:** ~60 LOC.

---

### B3: Element `classList` (Priority: HIGH)

**Problem:** `classList` is the standard API for manipulating CSS classes on elements. It's one of the most frequently used DOM APIs in modern web code. Currently marked as ❌ in JS_DOM_Support.md.

**Approach:** Return a wrapper object from `element.classList` that exposes `add`, `remove`, `toggle`, `contains`, `item`, and `length`. The wrapper holds a reference to the DOM element and operates on its `class` attribute.

#### Representation

```c
// js_dom.cpp
static TypeMap js_classlist_marker = {};

Item js_dom_create_classlist(void* dom_elem) {
    Map* wrapper = heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type = (void*)&js_classlist_marker;
    wrapper->data = dom_elem;
    return (Item){.map = wrapper};
}
```

#### classList Methods Dispatcher

```c
Item js_classlist_method(Item classlist, Item method_name, Item* args, int argc) {
    DomElement* el = (DomElement*)it2map(classlist)->data;
    String* method = it2s(method_name);
    const char* cls_str = dom_get_attribute(el, "class");

    if (MATCH("add")) {
        // For each arg, add class if not present
        for (int i = 0; i < argc; i++) {
            String* cls = it2s(js_to_string(args[i]));
            if (!classlist_has(cls_str, cls->chars, cls->len)) {
                // Append " className" to existing class attribute
                dom_classlist_add(el, cls->chars, cls->len);
            }
        }
        return ITEM_JS_UNDEFINED;
    }
    if (MATCH("remove")) {
        for (int i = 0; i < argc; i++) {
            String* cls = it2s(js_to_string(args[i]));
            dom_classlist_remove(el, cls->chars, cls->len);
        }
        return ITEM_JS_UNDEFINED;
    }
    if (MATCH("toggle")) {
        // toggle(cls) — add if absent, remove if present. Returns new state.
        String* cls = it2s(js_to_string(args[0]));
        bool has = classlist_has(cls_str, cls->chars, cls->len);
        if (has) dom_classlist_remove(el, cls->chars, cls->len);
        else     dom_classlist_add(el, cls->chars, cls->len);
        return b2it(!has);
    }
    if (MATCH("contains")) {
        String* cls = it2s(js_to_string(args[0]));
        return b2it(classlist_has(cls_str, cls->chars, cls->len));
    }
    if (MATCH("item")) {
        int idx = (int)js_to_number_raw(args[0]);
        // Split class string by spaces, return idx-th token
        return classlist_item_at(cls_str, idx);
    }
    return ITEM_JS_UNDEFINED;
}

Item js_classlist_get_property(Item classlist, const char* prop, int prop_len) {
    if (MATCH("length")) {
        DomElement* el = (DomElement*)it2map(classlist)->data;
        const char* cls_str = dom_get_attribute(el, "class");
        return i2it(classlist_count(cls_str));
    }
    return ITEM_JS_UNDEFINED;
}
```

#### Helper Functions

```c
// Check if class_str contains cls (whitespace-separated token match)
static bool classlist_has(const char* class_str, const char* cls, size_t cls_len);

// Add class to element's "class" attribute
static void dom_classlist_add(DomElement* el, const char* cls, size_t cls_len);

// Remove class from element's "class" attribute
static void dom_classlist_remove(DomElement* el, const char* cls, size_t cls_len);

// Count whitespace-separated tokens
static int classlist_count(const char* class_str);

// Return nth token as Item string
static Item classlist_item_at(const char* class_str, int idx);
```

**Integration:** In element property dispatch:
```c
if (MATCH("classList")) return js_dom_create_classlist(dom_node);
```

In method call dispatch, detect classlist wrapper:
```c
if (js_is_classlist(receiver)) {
    return js_classlist_method(receiver, method_name, args, argc);
}
```

**Code locations:**
- `lambda/js/js_dom.cpp` — classList wrapper, methods, helpers
- `lambda/js/js_dom.h` — declare new functions

**Test cases:**
```javascript
// test/js/test_classlist.js
var el = document.createElement("div");
el.setAttribute("class", "foo bar");

console.log(el.classList.length);          // 2
console.log(el.classList.contains("foo")); // true
console.log(el.classList.contains("baz")); // false

el.classList.add("baz");
console.log(el.classList.contains("baz")); // true
console.log(el.classList.length);          // 3

el.classList.remove("foo");
console.log(el.classList.contains("foo")); // false
console.log(el.classList.length);          // 2

var result = el.classList.toggle("bar");
console.log(result);                       // false (was present, now removed)
console.log(el.classList.contains("bar")); // false

result = el.classList.toggle("bar");
console.log(result);                       // true (was absent, now added)
console.log(el.classList.contains("bar")); // true

console.log(el.classList.item(0));         // "bar" or "baz" (order depends on implementation)
```

**Estimated effort:** ~180 LOC.

---

### B4: Element `dataset` (Priority: MEDIUM)

**Problem:** `element.dataset` provides a `DOMStringMap` proxy for `data-*` attributes. `el.dataset.userId` maps to `data-user-id` attribute. Widely used in modern web code.

**Approach:** Return a wrapper object that intercepts property get/set and maps to `data-*` attributes on the element.

#### camelCase ↔ kebab-case Conversion

```c
// "userId" → "user-id" → "data-user-id"
static void camel_to_data_attr(const char* camel, size_t len, StrBuf* sb) {
    strbuf_append_str(sb, "data-");
    for (size_t i = 0; i < len; i++) {
        if (camel[i] >= 'A' && camel[i] <= 'Z') {
            strbuf_append_char(sb, '-');
            strbuf_append_char(sb, camel[i] + 32); // to lowercase
        } else {
            strbuf_append_char(sb, camel[i]);
        }
    }
}

// "data-user-id" → "userId"
static String* data_attr_to_camel(const char* attr, size_t len) {
    // Skip "data-" prefix (5 chars)
    StrBuf sb;
    strbuf_init(&sb, len);
    bool next_upper = false;
    for (size_t i = 5; i < len; i++) {
        if (attr[i] == '-') {
            next_upper = true;
        } else if (next_upper) {
            strbuf_append_char(&sb, attr[i] - 32); // to uppercase
            next_upper = false;
        } else {
            strbuf_append_char(&sb, attr[i]);
        }
    }
    String* result = heap_create_name(sb.str, sb.len);
    strbuf_free(&sb);
    return result;
}
```

#### Dataset Wrapper

```c
static TypeMap js_dataset_marker = {};

Item js_dom_create_dataset(void* dom_elem) {
    Map* wrapper = heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type = (void*)&js_dataset_marker;
    wrapper->data = dom_elem;
    return (Item){.map = wrapper};
}

// Get: el.dataset.userId → getAttribute("data-user-id")
Item js_dataset_get_property(Item dataset, const char* prop, int prop_len) {
    DomElement* el = (DomElement*)it2map(dataset)->data;
    StrBuf sb;
    strbuf_init(&sb, prop_len + 6);
    camel_to_data_attr(prop, prop_len, &sb);
    const char* val = dom_get_attribute(el, sb.str);
    strbuf_free(&sb);
    return val ? s2it(heap_create_name(val, strlen(val))) : ITEM_JS_UNDEFINED;
}

// Set: el.dataset.userId = "123" → setAttribute("data-user-id", "123")
void js_dataset_set_property(Item dataset, const char* prop, int prop_len, Item value) {
    DomElement* el = (DomElement*)it2map(dataset)->data;
    StrBuf sb;
    strbuf_init(&sb, prop_len + 6);
    camel_to_data_attr(prop, prop_len, &sb);
    String* val = it2s(js_to_string(value));
    dom_set_attribute(el, sb.str, val->chars);
    strbuf_free(&sb);
}
```

**Integration:** In element property dispatch:
```c
if (MATCH("dataset")) return js_dom_create_dataset(dom_node);
```

In the property access chain handler, detect dataset wrapper:
```c
if (js_is_dataset(receiver)) {
    return js_dataset_get_property(receiver, prop, prop_len);
}
```

**Code locations:**
- `lambda/js/js_dom.cpp` — dataset wrapper, camelCase conversion, property dispatch

**Test cases:**
```javascript
// test/js/test_dataset.js
var el = document.createElement("div");
el.setAttribute("data-user-id", "42");
el.setAttribute("data-role-name", "admin");

console.log(el.dataset.userId);       // "42"
console.log(el.dataset.roleName);     // "admin"
console.log(el.dataset.missing);      // undefined

el.dataset.newProp = "hello";
console.log(el.getAttribute("data-new-prop")); // "hello"
```

**Estimated effort:** ~100 LOC.

---

### B5: Element `contains` (Priority: LOW)

**Problem:** `element.contains(other)` checks if `other` is a descendant of `element` (or is `element` itself). Simple utility but used by many libraries (event delegation, focus management).

**Approach:** Walk from `other` up through `parentNode` until we find `element` or reach the root.

**Implementation** (`js_dom.cpp`):

```c
extern "C" Item js_dom_contains(Item self_item, Item other_item) {
    if (!js_is_dom_node(self_item) || !js_is_dom_node(other_item))
        return b2it(false);

    void* self_node = it2map(self_item)->data;
    void* other_node = it2map(other_item)->data;

    // element.contains(element) → true
    if (self_node == other_node) return b2it(true);

    // Walk up from other to root
    DomNode* current = (DomNode*)other_node;
    while (current->parent) {
        current = current->parent;
        if ((void*)current == self_node) return b2it(true);
    }
    return b2it(false);
}
```

**Integration:** In element method dispatch:
```c
if (MATCH("contains") && argc >= 1) {
    return js_dom_contains(receiver, args[0]);
}
```

**Code locations:**
- `lambda/js/js_dom.cpp` — `js_dom_contains`, method dispatch

**Test cases:**
```javascript
// test/js/test_contains.js
var parent = document.createElement("div");
var child = document.createElement("span");
var grandchild = document.createElement("em");
parent.appendChild(child);
child.appendChild(grandchild);

console.log(parent.contains(child));       // true
console.log(parent.contains(grandchild));  // true
console.log(parent.contains(parent));      // true
console.log(child.contains(parent));       // false
```

**Estimated effort:** ~30 LOC.

---

### B6: Style `cssText` (Priority: LOW)

**Problem:** `element.style.cssText` gets/sets the raw inline style string. Useful for bulk style manipulation or reading back the full inline style.

**Approach:**
- **Getter:** Serialize all inline style properties on the element to a CSS string: `"color: red; font-size: 14px;"`
- **Setter:** Parse the CSS string and apply each declaration to the element's inline styles.

**Implementation** (`js_dom.cpp`):

**Getter:**
```c
Item js_dom_get_css_text(void* dom_node) {
    DomElement* el = (DomElement*)dom_node;
    const char* style_attr = dom_get_attribute(el, "style");
    if (!style_attr) return s2it(heap_create_name("", 0));
    return s2it(heap_create_name(style_attr, strlen(style_attr)));
}
```

**Setter:**
```c
void js_dom_set_css_text(void* dom_node, Item value) {
    DomElement* el = (DomElement*)dom_node;
    String* css = it2s(js_to_string(value));
    dom_set_attribute(el, "style", css->chars);
    // Invalidate any cached computed styles
}
```

**Integration:** In the style property chain handler (where `el.style.xxx` is dispatched):
```c
// Detect el.style.cssText get
if (MATCH("cssText")) {
    return is_setter ? js_dom_set_css_text(dom_node, value)
                     : js_dom_get_css_text(dom_node);
}
```

**Code locations:**
- `lambda/js/js_dom.cpp` — style property dispatch

**Test cases:**
```javascript
// test/js/test_css_text.js
var el = document.createElement("div");
el.style.color = "red";
el.style.fontSize = "14px";
console.log(el.style.cssText); // "color: red; font-size: 14px;" (or equivalent)

el.style.cssText = "background: blue; margin: 10px;";
console.log(el.style.cssText); // "background: blue; margin: 10px;"
```

**Estimated effort:** ~50 LOC.

---

## 4. Implementation Order

Recommended implementation sequence based on dependencies and impact:

| Phase | Items | Rationale |
|:-----:|-------|-----------|
| 1 | A1 (rest destructuring) | High impact, unblocks spec compliance; no dependencies |
| 2 | A4 (encodeURI/decodeURI) | High utility, reuses existing lib/url.c; straightforward |
| 3 | B3 (classList) | Most impactful DOM feature; many web libs depend on it |
| 4 | B5 (contains) | Tiny, simple subtree walk; often used alongside classList |
| 5 | B1 (document.URL/location) | Reuses lib/url.c; moderate impact |
| 6 | B4 (dataset) | Moderate impact, straightforward wrapper pattern |
| 7 | A2 (Symbol) | Largest item; lays groundwork for Symbol.iterator |
| 8 | B2 (outerHTML), B6 (cssText) | Low priority, small scope |
| 9 | A3 (globalThis) | Trivial to implement, low urgency |

## 5. Testing Strategy

Each feature gets:
1. **Unit test file:** `test/js/test_<feature>.js` with expected output `.txt`
2. **Baseline integration:** Add to `make test-js-baseline` suite
3. **Edge cases:** null/undefined inputs, empty strings, missing properties

**Baseline gate:** All existing 52 JS baseline tests must continue to pass after each feature lands.

## 6. Documentation Updates

After implementation, update:
- `doc/JS_DOM_Support.md` — flip ❌/⚠️ to ✅ for each completed feature
- Add new methods/properties to the relevant tables
- Update benchmark notes if any new JetStream tests pass

## 7. Summary of File Changes

| File | Changes |
|------|---------|
| `lib/url.c` | Add public `url_encode_component`, `url_decode_component`; promote `hex_to_int` |
| `lib/url.h` | Declare new functions |
| `lambda/js/js_runtime.h` | Declare `js_object_rest`, `js_encodeURIComponent`, `js_decodeURIComponent`, `js_get_global_this`, `js_symbol_*` |
| `lambda/js/js_runtime.cpp` | `js_object_rest` implementation |
| `lambda/js/js_globals.cpp` | Symbol registry, `globalThis`, encode/decode URI wrappers |
| `lambda/js/transpile_js_mir.cpp` | Rest destructuring emit, Symbol dispatch, globalThis, encode/decode dispatch |
| `lambda/js/js_dom.cpp` | Location wrapper, classList, dataset, outerHTML, contains, cssText |
| `lambda/js/js_dom.h` | Declare DOM additions |
| `test/js/test_*.js` + `.txt` | New test files per feature |

---

## 8. v12b — DOM Expansion: Document Methods, innerHTML Setter, Element Methods, Style Methods

After the initial v12 features (Track A language + Track B DOM) landed, the remaining ❌ items in `JS_DOM_Support.md` are addressed here. This continuation focuses on completing the DOM API surface.

### v12b Feature Overview

| Item | Category | Priority | Est. LOC | Status | Notes |
|------|:--------:|:--------:|:--------:|:------:|-------|
| C1 `createDocumentFragment` | Document | HIGH | ~60 | ✅ | Returns a lightweight container for batch DOM mutation |
| C2 `createComment` | Document | LOW | ~30 | ✅ | Create a comment node |
| C3 `importNode` | Document | LOW | ~40 | ✅ | Clone a node from another document context |
| C4 `adoptNode` | Document | LOW | ~30 | ✅ | Transfer a node into this document |
| D1 `innerHTML` setter | Element | HIGH | ~80 | ✅ | Parse HTML string, replace children with parsed fragment |
| E1 `replaceChild` | Element | MEDIUM | ~20 | ✅ | Replace old child with new child |
| E2 `insertAdjacentHTML` | Element | MEDIUM | ~80 | ✅ | Parse HTML and insert at position (`beforebegin`, `afterbegin`, `beforeend`, `afterend`) |
| E3 `insertAdjacentElement` | Element | LOW | ~30 | ✅ | Insert element at position (no parsing) |
| E4 `remove` | Element | HIGH | ~10 | ✅ | Self-removal from parent |
| E5 `toggleAttribute` | Element | LOW | ~15 | ✅ | Toggle boolean attribute presence |
| F1 `style.setProperty` | Style | MEDIUM | ~25 | ✅ | Set CSS property with optional priority |
| F2 `style.removeProperty` | Style | MEDIUM | ~30 | ✅ | Remove CSS property, return old value |

**Total estimated:** ~450 LOC across `js_dom.cpp`, `js_dom.h`, `transpile_js_mir.cpp`, `sys_func_registry.c`

**Status:** ✅ All 12 v12b features implemented and passing (669/669 Lambda baseline tests). ~270 LOC added across 4 files.

---

### C1: `document.createDocumentFragment()` (Priority: HIGH)

**Problem:** `DocumentFragment` is a lightweight container used as a staging area for batch DOM mutations. Code like `fragment.appendChild(a); fragment.appendChild(b); parent.appendChild(fragment)` moves all fragment children into the parent in one operation, avoiding repeated reflows.

**Current state:** Not implemented. `createDocumentFragment` is not in `js_document_method()`.

**Approach:** A `DocumentFragment` is essentially a parentless `DomElement` with a synthetic tag name (e.g., `"#document-fragment"`). The key behavior is that when a fragment is passed to `appendChild`/`insertBefore`, its *children* are moved (not the fragment itself).

**New runtime helper** (`js_dom.cpp`):

```c
// Create a document fragment — a parentless container element
// Used in js_document_method under "createDocumentFragment"
static Item js_dom_create_document_fragment(DomDocument* doc) {
    MarkBuilder builder(doc->input);
    Item elem_item = builder.element("#document-fragment").final();
    DomElement* frag = dom_element_create(doc, "#document-fragment", elem_item.element);
    return js_dom_wrap_element(frag);
}
```

**Modify `appendChild` / `insertBefore`** to detect fragments:

When the child argument is a fragment (tag_name == `"#document-fragment"`), iterate through its children and move each one individually:

```c
// In appendChild handler, before the current append logic:
if (child_node->is_element()) {
    DomElement* child_elem = child_node->as_element();
    if (child_elem->tag_name && strcmp(child_elem->tag_name, "#document-fragment") == 0) {
        // move all fragment children to the target
        DomNode* fc = child_elem->first_child;
        Item last_appended = ItemNull;
        while (fc) {
            DomNode* next = fc->next_sibling;
            child_elem->remove_child(fc);
            ((DomNode*)elem)->append_child(fc);
            last_appended = js_dom_wrap_element(fc);
            fc = next;
        }
        return last_appended;
    }
}
```

**Document method dispatch:**

```c
// In js_document_method:
if (strcmp(method, "createDocumentFragment") == 0) {
    return js_dom_create_document_fragment(doc);
}
```

**No transpiler changes needed** — `document.createDocumentFragment()` already routes through `js_document_method` via the existing `jm_is_document_call` dispatch path.

**Code locations:**
- `lambda/js/js_dom.cpp` — fragment creation + modify `appendChild`/`insertBefore` for fragment unpacking

**Estimated effort:** ~60 LOC.

---

### C2: `document.createComment(text)` (Priority: LOW)

**Problem:** Creates a comment node (`<!-- text -->`). Rarely used in application code but needed for spec compliance and some templating libraries.

**Current state:** `DomComment` struct exists. `dom_comment_create()` exists but requires a backing Lambda Element with tag `"!--"`. No JS-level API.

**Approach:** Create a detached comment node analogous to `dom_text_create_detached`.

**Implementation** (`js_dom.cpp`, in `js_document_method`):

```c
if (strcmp(method, "createComment") == 0) {
    if (argc < 1) return ItemNull;
    const char* text = fn_to_cstr(args[0]);
    if (!text) text = "";

    // create a Lambda Element with tag "!--" to back the comment
    MarkBuilder builder(doc->input);
    Item comment_elem = builder.element("!--").final();

    // create DomComment backed by this element
    // need a temporary parent context — use root
    DomComment* comment = dom_comment_create(comment_elem.element, doc->root);
    if (!comment) return ItemNull;

    // set the comment text content
    size_t len = strlen(text);
    char* content_copy = (char*)arena_alloc(doc->arena, len + 1);
    memcpy(content_copy, text, len);
    content_copy[len] = '\0';
    comment->content = content_copy;
    comment->length = len;

    // detach from the temporary parent
    comment->parent = nullptr;
    if (doc->root) {
        ((DomNode*)doc->root)->remove_child((DomNode*)comment);
    }

    return js_dom_wrap_element(comment);
}
```

**No transpiler changes needed.**

**Estimated effort:** ~30 LOC.

---

### C3: `document.importNode(node, deep)` (Priority: LOW)

**Problem:** Copies a node from another document into this document's context. In a single-document system like LambdaJS, this is effectively the same as `cloneNode(deep)`.

**Implementation** (`js_dom.cpp`, in `js_document_method`):

```c
if (strcmp(method, "importNode") == 0) {
    if (argc < 1) return ItemNull;
    bool deep = (argc > 1) ? js_is_truthy(args[1]) : false;
    // delegate to cloneNode on the source node
    DomNode* source = (DomNode*)js_dom_unwrap_element(args[0]);
    if (!source || !source->is_element()) return ItemNull;
    Item source_item = js_dom_wrap_element(source);
    Item deep_arg = (Item){.item = b2it(deep ? 1 : 0)};
    Item method_str = (Item){.item = s2it(heap_create_name("cloneNode"))};
    return js_dom_element_method(source_item, method_str, &deep_arg, 1);
}
```

**No transpiler changes needed.**

**Estimated effort:** ~40 LOC.

---

### C4: `document.adoptNode(node)` (Priority: LOW)

**Problem:** Removes a node from its current parent and adopts it into this document. In a single-document system, this just detaches the node from its parent.

**Implementation** (`js_dom.cpp`, in `js_document_method`):

```c
if (strcmp(method, "adoptNode") == 0) {
    if (argc < 1) return ItemNull;
    DomNode* node = (DomNode*)js_dom_unwrap_element(args[0]);
    if (!node) return ItemNull;
    // detach from current parent
    if (node->parent) {
        node->parent->remove_child(node);
    }
    return args[0];
}
```

**No transpiler changes needed.**

**Estimated effort:** ~30 LOC.

---

### D1: `innerHTML` Setter (Priority: HIGH)

**Problem:** `element.innerHTML = "<p>Hello</p>"` should parse the HTML string, remove the element's current children, and replace them with the parsed fragment. Currently only the getter is implemented.

**Current state:** The getter uses `collect_inner_html()`. The setter falls through to `js_dom_set_property` which treats it as a generic attribute (wrong behavior). The HTML5 fragment parser (`html5_fragment_parser_create`, `html5_fragment_parse`, `html5_fragment_get_body`) already exists and is used by the markdown parser.

**Approach:** Parse the HTML string using the fragment parser, convert the resulting Lambda Elements into DOM nodes using `build_dom_tree_from_element`, then replace the element's children.

**Implementation** (`js_dom.cpp`, add to `js_dom_set_property`):

```c
// innerHTML setter — parse HTML and replace children
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
    //    Iterate body_elem children (the parsed fragment content)
    for (size_t i = 0; i < body_elem->length; i++) {
        TypeId type = get_type_id(body_elem->items[i]);
        if (type == LMD_TYPE_ELEMENT) {
            DomElement* child_dom = build_dom_tree_from_element(
                body_elem->items[i].element, doc, elem);
            // build_dom_tree_from_element already appends to parent
        } else if (type == LMD_TYPE_STRING) {
            String* s = it2s(body_elem->items[i]);
            DomText* text_node = dom_text_create(s, elem);
            // dom_text_create already sets parent
        }
    }

    log_debug("js_dom_set_property: set innerHTML on <%s>",
              elem->tag_name ? elem->tag_name : "?");
    return value;
}
```

**Header includes needed** in `js_dom.cpp`:
```c
#include "../input/html5/html5_parser.h"
```

**Forward declaration** needed in `js_dom.cpp`:
```c
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);
```

**No transpiler changes needed** — innerHTML set goes through the existing `js_property_set` → `js_dom_set_property` path.

**Code locations:**
- `lambda/js/js_dom.cpp` — innerHTML setter in `js_dom_set_property`

**Test cases:**
```javascript
var div = document.createElement("div");
div.innerHTML = "<p>Hello</p><span>World</span>";
console.log(div.children.length);        // 2
console.log(div.children[0].tagName);    // "P"
console.log(div.children[0].textContent); // "Hello"
console.log(div.children[1].tagName);    // "SPAN"

div.innerHTML = "";
console.log(div.children.length);        // 0

div.innerHTML = "plain text";
console.log(div.textContent);            // "plain text"
```

**Estimated effort:** ~80 LOC.

---

### E1: `element.replaceChild(newChild, oldChild)` (Priority: MEDIUM)

**Problem:** Replaces `oldChild` with `newChild` in the element's child list. Returns the removed old child.

**Current state:** Not implemented. The tree primitives `insert_before` and `remove_child` already exist — `replaceChild` is a composition of these two.

**Implementation** (`js_dom.cpp`, add to `js_dom_element_method`):

```c
// replaceChild(newChild, oldChild)
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
```

**No transpiler changes needed** — method calls on DOM elements already route through `js_dom_element_method`.

**Estimated effort:** ~20 LOC.

---

### E2: `element.insertAdjacentHTML(position, text)` (Priority: MEDIUM)

**Problem:** Parses `text` as HTML and inserts the resulting nodes at one of four positions relative to the element:
- `"beforebegin"` — before the element itself (as previous sibling)
- `"afterbegin"` — inside the element, before its first child
- `"beforeend"` — inside the element, after its last child (same as `innerHTML +=`)
- `"afterend"` — after the element itself (as next sibling)

**Approach:** Reuse the same fragment parsing pipeline as innerHTML setter: `html5_fragment_parser_create` → `html5_fragment_parse` → `html5_fragment_get_body` → `build_dom_tree_from_element`. Then insert the resulting nodes at the correct position.

**Implementation** (`js_dom.cpp`, add to `js_dom_element_method`):

```c
// insertAdjacentHTML(position, text)
if (strcmp(method, "insertAdjacentHTML") == 0) {
    if (argc < 2) return ItemNull;
    const char* position = fn_to_cstr(args[0]);
    const char* html_str = fn_to_cstr(args[1]);
    if (!position || !html_str || !elem->doc) return ItemNull;

    DomDocument* doc = elem->doc;

    // parse the HTML fragment
    Html5Parser* parser = html5_fragment_parser_create(
        doc->pool, doc->arena, doc->input);
    if (!parser) return ItemNull;
    html5_fragment_parse(parser, html_str);
    Element* body_elem = html5_fragment_get_body(parser);
    if (!body_elem) return ItemNull;

    // determine target parent and reference node based on position
    DomElement* target_parent = nullptr;
    DomNode* ref_node = nullptr; // insert before this node (nullptr = append)

    if (strcasecmp(position, "beforebegin") == 0) {
        // insert before this element, into this element's parent
        if (!elem->parent || !elem->parent->is_element()) return ItemNull;
        target_parent = elem->parent->as_element();
        ref_node = (DomNode*)elem;
    } else if (strcasecmp(position, "afterbegin") == 0) {
        // insert inside this element, before first child
        target_parent = elem;
        ref_node = elem->first_child;
    } else if (strcasecmp(position, "beforeend") == 0) {
        // insert inside this element, after last child (append)
        target_parent = elem;
        ref_node = nullptr;
    } else if (strcasecmp(position, "afterend") == 0) {
        // insert after this element, into this element's parent
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
            // build dom tree with nullptr parent (detached), then insert manually
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
```

**Note:** `build_dom_tree_from_element` with `parent=nullptr` creates a detached subtree. Need to verify this works or adjust accordingly. If `build_dom_tree_from_element` requires a parent, create a temporary fragment container and then move children.

**No transpiler changes needed.**

**Estimated effort:** ~80 LOC.

---

### E3: `element.insertAdjacentElement(position, element)` (Priority: LOW)

**Problem:** Like `insertAdjacentHTML` but takes an existing DOM element instead of an HTML string. No parsing needed.

**Implementation** (`js_dom.cpp`, add to `js_dom_element_method`):

```c
// insertAdjacentElement(position, newElement)
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
```

**No transpiler changes needed.**

**Estimated effort:** ~30 LOC.

---

### E4: `element.remove()` (Priority: HIGH)

**Problem:** `element.remove()` removes the element from its parent. Equivalent to `element.parentNode.removeChild(element)` but more concise. Widely used in modern web code.

**Implementation** (`js_dom.cpp`, add to `js_dom_element_method`):

```c
// remove() — self-removal from parent
if (strcmp(method, "remove") == 0) {
    DomNode* node = (DomNode*)elem;
    if (node->parent) {
        node->parent->remove_child(node);
    }
    return ItemNull;
}
```

**No transpiler changes needed.**

**Estimated effort:** ~10 LOC.

---

### E5: `element.toggleAttribute(name)` / `element.toggleAttribute(name, force)` (Priority: LOW)

**Problem:** Toggles a boolean attribute. If `force` is provided, adds the attribute if `force` is true, removes if false. Returns whether the attribute is present after the call.

**Implementation** (`js_dom.cpp`, add to `js_dom_element_method`):

```c
// toggleAttribute(name [, force])
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
```

**No transpiler changes needed.**

**Estimated effort:** ~15 LOC.

---

### F1: `element.style.setProperty(property, value [, priority])` (Priority: MEDIUM)

**Problem:** The programmatic way to set a CSS property. Unlike `element.style.prop = val`, this accepts CSS property names (kebab-case) and an optional `"important"` priority string.

**Current state:** `element.style.X = val` is handled by `js_dom_set_style_property` which converts camelCase to CSS properties. `setProperty` needs similar logic but skips the camelCase conversion (since the property name is already in CSS format).

**Approach:** This is a method call on the `style` object. The pattern `obj.style.setProperty(...)` is a three-level chain: `obj` → `style` → `setProperty(...)`. We need transpiler-level detection for `obj.style.setProperty(...)` and `obj.style.removeProperty(...)`.

**New runtime function** (`js_dom.cpp`):

```c
extern "C" Item js_dom_style_method(Item elem_item, Item method_name, Item* args, int argc) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    const char* method = fn_to_cstr(method_name);
    if (!method) return ItemNull;

    // setProperty(property, value [, priority])
    if (strcmp(method, "setProperty") == 0) {
        if (argc < 2) return ItemNull;
        const char* css_prop = fn_to_cstr(args[0]);
        const char* val_str = fn_to_cstr(args[1]);
        if (!css_prop || !val_str) return ItemNull;

        // build declaration string, optionally with !important
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
        dom_element_apply_inline_style(elem, style_decl);
        elem->styles_resolved = false;
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
        if (prop_id != CSS_PROPERTY_UNKNOWN) {
            CssDeclaration* decl = dom_element_get_specified_value(elem, prop_id);
            if (decl && decl->specificity.inline_style) {
                // serialize old value (reuse getter logic)
                Item prop_item = (Item){.item = s2it(heap_create_name(css_prop))};
                old_val = js_dom_get_style_property(elem_item, prop_item);
            }
            // remove the declaration from the style tree
            style_tree_remove(elem->specified_style, prop_id);
        }
        elem->styles_resolved = false;
        return old_val;
    }

    log_debug("js_dom_style_method: unknown method '%s'", method);
    return ItemNull;
}
```

**Transpiler changes** (`transpile_js_mir.cpp`):

Detect `obj.style.setProperty(...)` / `obj.style.removeProperty(...)` — three-level chained method call:

```cpp
// In the method call dispatch section, before the generic receiver-type dispatch:
// Detect: obj.style.setProperty(...) or obj.style.removeProperty(...)
if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (m->object && m->object->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* outer = (JsMemberNode*)m->object;
        if (outer->property && outer->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* mid = (JsIdentifierNode*)outer->property;
            if (mid->name && mid->name->len == 5 &&
                strncmp(mid->name->chars, "style", 5) == 0 &&
                m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* method_id = (JsIdentifierNode*)m->property;
                const char* mn = method_id->name->chars;
                int ml = method_id->name->len;
                if ((ml == 11 && strncmp(mn, "setProperty", 11) == 0) ||
                    (ml == 14 && strncmp(mn, "removeProperty", 14) == 0)) {
                    MIR_reg_t obj = jm_transpile_box_item(mt, outer->object);
                    MIR_reg_t method_str = jm_box_string_literal(mt, mn, ml);
                    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
                    return jm_call_4(mt, "js_dom_style_method", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, method_str),
                        MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                }
            }
        }
    }
}
```

**Code locations:**
- `lambda/js/js_dom.cpp` — `js_dom_style_method()` implementation
- `lambda/js/js_dom.h` — declare `js_dom_style_method`
- `lambda/js/transpile_js_mir.cpp` — chained call detection for `style.setProperty` / `style.removeProperty`
- `lambda/sys_func_registry.c` — register `js_dom_style_method`

**Note:** `style_tree_remove()` must exist or be added to remove a specific property from the AVL tree. If it doesn't exist, we can use `dom_element_remove_inline_styles()` followed by re-apply, but that's less efficient. Verify `style_tree_remove` exists in the CSS engine.

**Test cases:**
```javascript
var el = document.createElement("div");
el.style.setProperty("color", "red");
console.log(el.style.color);                  // "red"

el.style.setProperty("font-size", "14px", "important");
console.log(el.style.fontSize);               // "14px"

var old = el.style.removeProperty("color");
console.log(old);                             // "red"
console.log(el.style.color);                  // ""
```

**Estimated effort:** F1 + F2 combined ~55 LOC runtime + ~25 LOC transpiler.

---

### F2: `element.style.removeProperty(property)` (Priority: MEDIUM)

Handled together with F1 above in `js_dom_style_method()`. See section F1 for the combined implementation.

---

## 9. v12b Implementation Order

| Phase | Items | Rationale |
|:-----:|-------|-----------|
| 1 | E4 (remove) | Trivial, 10 LOC, high usage |
| 2 | E1 (replaceChild) | Simple composition of existing primitives |
| 3 | D1 (innerHTML setter) | High impact; enables fragment-based DOM building |
| 4 | C1 (createDocumentFragment) | Enables batch mutations; requires extend `appendChild`/`insertBefore` |
| 5 | E2 (insertAdjacentHTML) | Reuses innerHTML setter's fragment parsing pipeline |
| 6 | E3 (insertAdjacentElement) | Simple positional insert, no parsing |
| 7 | F1+F2 (style.setProperty/removeProperty) | Moderate impact; needs transpiler + runtime |
| 8 | E5 (toggleAttribute) | Trivial |
| 9 | C2 (createComment) | Low priority |
| 10 | C3+C4 (importNode/adoptNode) | Low priority, simple wrappers |

## 10. v12b Summary of File Changes

| File | Changes |
|------|---------|
| `lambda/js/js_dom.cpp` | `createDocumentFragment`, `createComment`, `importNode`, `adoptNode` in document method; innerHTML setter in property setter; `replaceChild`, `insertAdjacentHTML`, `insertAdjacentElement`, `remove`, `toggleAttribute` in element method; fragment-aware `appendChild`/`insertBefore`; `js_dom_style_method()` |
| `lambda/js/js_dom.h` | Declare `js_dom_style_method` |
| `lambda/js/transpile_js_mir.cpp` | Detect `obj.style.setProperty(...)` / `obj.style.removeProperty(...)` chained calls |
| `lambda/sys_func_registry.c` | Register `js_dom_style_method` |
| `test/js/test_*.js` + `.txt` | New test files for each feature |

## 11. Implementation Status

**v12 (Track A + Track B):** ✅ Complete — all 10 features landed, 669/669 baseline tests pass.

**v12b (DOM expansion):** ✅ Complete — all 12 features landed, 669/669 baseline tests pass.

Files modified for v12b:
- `lambda/js/js_dom.cpp` — ~230 LOC: createDocumentFragment, createComment, importNode, adoptNode, innerHTML setter, replaceChild, insertAdjacentHTML, insertAdjacentElement, remove, toggleAttribute, js_dom_style_method, fragment-aware appendChild/insertBefore
- `lambda/js/js_dom.h` — declared `js_dom_style_method`
- `lambda/js/transpile_js_mir.cpp` — ~15 LOC: `obj.style.setProperty()`/`removeProperty()` chained call dispatch
- `lambda/sys_func_registry.c` — registered `js_dom_style_method`
- `doc/JS_DOM_Support.md` — 12 status entries flipped from ❌/⚠️ to ✅
