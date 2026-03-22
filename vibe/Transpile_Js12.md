# Transpile_Js12: Language Completeness — Phase 3 (Rest Destructuring, Symbol, Globals, DOM)

## 1. Executive Summary

After v11, LambdaJS covers ES6 classes, prototype OOP, multi-level closures, typed arrays, optional chaining, Map/Set, error subclasses, regex, and 90+ standard library methods. This proposal fills the remaining **language gaps** and **DOM gaps** identified in the JS_DOM_Support feature matrix.

**v12 goal:** Two parallel tracks:
- **Track A — Language:** Destructuring rest (`...rest`), `Symbol` API, `globalThis`, `encodeURIComponent` / `decodeURIComponent`
- **Track B — DOM:** `document.URL` / `document.location`, element `outerHTML`, `classList`, `dataset`, `contains`, style `cssText`

### v12 Feature Overview

| Item | Track | Priority | Est. LOC | Notes |
|------|:-----:|:--------:|:--------:|-------|
| A1 Destructuring rest (`...rest`) | Lang | HIGH | ~120 | Object & array rest in declarations, assignments, and function params |
| A2 `Symbol` API | Lang | MEDIUM | ~250 | `Symbol()`, `Symbol.for()`, `Symbol.keyFor()`, well-known symbols |
| A3 `globalThis` | Lang | LOW | ~20 | Alias for the JS global object |
| A4 `encodeURIComponent` / `decodeURIComponent` | Lang | MEDIUM | ~80 | Reuse `lib/url.c` infrastructure |
| B1 `document.URL` / `document.location` | DOM | MEDIUM | ~150 | Reuse `lib/url.c` + `lib/url_parser.c` |
| B2 Element `outerHTML` | DOM | LOW | ~60 | Serialize element + descendants to HTML string |
| B3 Element `classList` | DOM | HIGH | ~180 | `add`, `remove`, `toggle`, `contains`, `item`, `length` |
| B4 Element `dataset` | DOM | MEDIUM | ~100 | `data-*` attribute proxy with camelCase conversion |
| B5 Element `contains` | DOM | LOW | ~30 | Subtree containment check |
| B6 Style `cssText` | DOM | LOW | ~50 | Get/set inline style as raw CSS string |

**Total estimated:** ~1,040 LOC across `transpile_js_mir.cpp`, `js_runtime.cpp`, `js_globals.cpp`, `js_dom.cpp`, `lib/url.c`

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
