# JavaScript Transpiler v9: Language Completeness & Standard Library

## 1. Executive Summary

After v8, the LambdaJS engine passes **12/13 JetStream JS** benchmarks and **62/62** across 5 benchmark suites. The engine now handles ES6 classes, prototype-based OOP, closures, typed arrays, template literals, try/catch/finally, and basic regex via RE2.

**v9 goal:** Systematically close the remaining JavaScript language and standard library gaps so that LambdaJS can run **general-purpose JavaScript programs** — not just narrow benchmark code. The focus shifts from "make benchmarks pass" to "be a correct JS engine for real-world code."

### Current Engine Stats (Post-v8)

| Component | File | Lines | Coverage |
|-----------|------|------:|----------|
| AST definitions | `js_ast.hpp` | ~350 | 47 node types, 47 operators |
| AST builder | `build_js_ast.cpp` | ~1,600 | Tree-sitter → AST |
| Transpiler | `transpile_js_mir.cpp` | 9,308 | JS AST → MIR IR |
| Runtime | `js_runtime.cpp` | 1,846 | Operators, type dispatch, prototype chain |
| Globals | `js_globals.cpp` | 571 | Built-in functions, timing, constants |
| DOM bridge | `js_dom.cpp` | ~370 | Element wrapping, computed styles |
| Typed arrays | `js_typed_array.cpp` | ~400 | Int8–Float64 arrays |
| Scoping | `js_scope.cpp` | ~200 | Lexical scope management |
| Tests | `test_js_gtest.cpp` | 432 | 34 test cases |
| **Total** | | **~15,100** | |

### v9 Feature Gap Summary

| Category | Implemented | Missing (Blocking) | Missing (Nice-to-have) |
|----------|------------|---------------------|----------------------|
| **Language syntax** | 42 of 47 AST nodes transpiled | Object destructuring, sequence expressions, labeled statements, `delete` (real impl), regex literals | Generators, async/await, optional chaining, import/export |
| **Operators** | 45 of 47 | `delete` (stubbed as `true`) | — |
| **String methods** | 16 | `match`, `search`, `replaceAll`, `padStart`, `padEnd`, `at`, `matchAll`, `lastIndexOf`, `normalize` | `localeCompare`, `codePointAt`, `toLocaleLowerCase` |
| **Array methods** | 20 | `splice`, `shift`, `unshift`, `lastIndexOf`, `flatMap`, `reduceRight`, `Array.from`, `Array.of`, `at` | `copyWithin`, `findLast`, `findLastIndex`, `toReversed`, `toSorted` |
| **Object methods** | 3 (`keys`, `create`, `defineProperty`) | `values`, `entries`, `assign`, `freeze`, `hasOwnProperty`, `getPrototypeOf`, `setPrototypeOf` | `seal`, `is`, `fromEntries`, `getOwnPropertyNames` |
| **Math methods** | 17 + `atan2` | `asin`, `acos`, `atan`, `log2`, `hypot`, `cbrt` | `sinh`, `cosh`, `tanh`, `clz32`, `imul`, `fround` |
| **Number methods** | 2 (`toFixed`, `toString`) | `isInteger`, `isFinite`, `isNaN`, `isSafeInteger` | `toExponential`, `toPrecision` |
| **JSON** | 0 | `JSON.parse`, `JSON.stringify` | `JSON.parse` reviver, `JSON.stringify` replacer |
| **Function methods** | `call`, `apply` | `bind` | — |
| **Date** | `Date.now()`, `new Date()` | `getFullYear`, `getMonth`, `getDate`, `getTime`, `getHours`, `toISOString` | Full Date API |
| **Error types** | Generic `Error` only | `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError` | `URIError`, `EvalError` |
| **RegExp** | Via RE2 (`replace`) | Regex literals, `match`, `test`, `search`, `exec`, `matchAll` | Named groups, lookbehind |
| **Map/Set** | ❌ Not implemented | `Map`, `Set` | `WeakMap`, `WeakSet` |
| **Symbol** | Partial (`typeof` returns `"symbol"`) | `Symbol.iterator`, `Symbol.toPrimitive` | Full Symbol API |
| **Promise/async** | ❌ Not implemented | — | Promise, async/await |
| **Proxy/Reflect** | ❌ Not implemented | — | Proxy, Reflect |
| **Other** | — | `setTimeout`/`setInterval`, `encodeURIComponent`/`decodeURIComponent` | `structuredClone`, `globalThis`, `Intl` |

---

## 2. Phase A: Core Language Syntax Gaps

**Goal:** Complete the remaining AST node transpilation for language constructs that real-world JS programs depend on.
**Impact:** Enables significantly broader JS program compatibility.
**Estimated effort:** ~400 LOC.

### A1. Object Destructuring (`const {a, b} = obj`)

**Status:** Array destructuring works. Object destructuring is **parsed** (AST node `JS_AST_NODE_OBJECT_PATTERN` exists) but **not transpiled** — `OBJECT_PATTERN` never appears in `transpile_js_mir.cpp`.

**Usage frequency:** Extremely common in modern JS. Used in Node.js modules, React components, configuration extraction, function parameter patterns.

**Patterns to support:**
```javascript
// Basic
const {x, y} = point;
// Rename
const {name: userName, age: userAge} = user;
// Default values
const {port = 3000, host = "localhost"} = config;
// Nested
const {address: {city, zip}} = person;
// Rest property
const {id, ...rest} = record;
// In function parameters
function draw({x, y, color = "red"}) { ... }
// In for-of
for (const {key, value} of entries) { ... }
```

**Implementation:**
In `jm_transpile_variable_declarator()`, add an `OBJECT_PATTERN` branch parallel to the existing `ARRAY_PATTERN` branch:
```cpp
} else if (d->id && d->id->node_type == JS_AST_NODE_OBJECT_PATTERN) {
    MIR_reg_t init_val = jm_transpile_box_item(mt, d->init);
    JsObjectPatternNode* pat = (JsObjectPatternNode*)d->id;
    for (JsAstNode* prop = pat->properties; prop; prop = prop->next) {
        if (prop->node_type == JS_AST_NODE_PROPERTY) {
            JsPropertyNode* p = (JsPropertyNode*)prop;
            // key = property name, value = local binding name
            String* key_name = /* extract from p->key */;
            String* local_name = /* extract from p->value or p->key for shorthand */;
            MIR_reg_t key = jm_box_string_literal(mt, key_name->chars, key_name->len);
            MIR_reg_t val = jm_call_2(mt, "js_property_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, init_val),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            // Handle default value via ASSIGNMENT_PATTERN
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)local_name->len, local_name->chars);
            jm_set_var(mt, vname, val, LMD_TYPE_ANY);
        } else if (prop->node_type == JS_AST_NODE_REST_PROPERTY) {
            // ...rest — collect remaining keys into new object
        }
    }
}
```

Also handle object destructuring in:
- Assignment expressions: `({a, b} = obj)`
- Function parameters (already partially handled via `PARAMETER` node)
- For-of/for-in loop variables

**Files:** `lambda/js/transpile_js_mir.cpp` — `jm_transpile_variable_declarator()`, `jm_transpile_assignment()`, `jm_transpile_for_of()`

### A2. Sequence Expressions (Comma Operator)

**Status:** Not parsed, not transpiled. Tree-sitter-javascript produces `sequence_expression` nodes for `(a, b, c)`.

**Usage:** Minified code, `for` loop update clauses (`for(i=0; ...; i++, j--)`), IIFE patterns.

**Patterns:**
```javascript
const x = (a(), b(), c());     // evaluates a(), b(), returns c()
for (let i=0, j=10; i<j; i++, j--) { ... }  // multi-update
```

**Implementation:**

In `build_js_ast.cpp`, detect `"sequence_expression"`:
```cpp
} else if (strcmp(node_type, "sequence_expression") == 0) {
    // Build as a chain: evaluate all children, return last
    // Reuse BINARY_EXPRESSION with JS_OP_COMMA, or add new JS_AST_NODE_SEQUENCE_EXPRESSION
}
```

Simplest approach: since tree-sitter represents `(a, b, c)` as nested `sequence_expression` nodes, map them to the existing `JS_AST_NODE_BINARY_EXPRESSION` with a new `JS_OP_COMMA` operator. In the transpiler, `JS_OP_COMMA` evaluates left for side effects, returns right.

**Alternative:** Add `JS_AST_NODE_SEQUENCE_EXPRESSION` with a linked list of children. Transpile by evaluating all but last for side effects, returning last.

**Files:** `lambda/js/build_js_ast.cpp`, `lambda/js/js_ast.hpp` (if adding new AST node), `lambda/js/transpile_js_mir.cpp`

### A3. Labeled Statements and `break label` / `continue label`

**Status:** Not parsed, not transpiled. Used in nested loop control flow.

**Patterns:**
```javascript
outer: for (let i = 0; i < 10; i++) {
    inner: for (let j = 0; j < 10; j++) {
        if (condition) break outer;      // break to outer loop
        if (other) continue outer;       // skip to outer loop update
    }
}
```

**Implementation:**

1. In `build_js_ast.cpp`, detect `"labeled_statement"`. Extract label name, wrap the inner statement.
2. In the transpiler, maintain a label name → `(break_label, continue_label)` mapping on the loop stack. When a `break`/`continue` with a label is encountered, look up the target and jump to it.

**Complexity:** Low — the loop stack (`mt->loop_stack`) already tracks `break_label` and `continue_label` per loop. Adding a name field to `JsLoopContext` and matching it on `break`/`continue` is straightforward.

**Files:** `lambda/js/build_js_ast.cpp`, `lambda/js/js_ast.hpp`, `lambda/js/transpile_js_mir.cpp`

### A4. `delete` Operator (Real Implementation)

**Status:** Parsed and handled in transpiler, but **stubbed** — always returns `true` without actually deleting the property.

**Current code ([line 3432](lambda/js/transpile_js_mir.cpp#L3432)):**
```cpp
case JS_OP_DELETE:
    return jm_box_int_const(mt, 1); // simplified: always true
```

**Correct behavior:**
- `delete obj.prop` → remove the property from the object, return `true`
- `delete obj[expr]` → same with computed key
- `delete variable` → return `false` (can't delete variables)
- `delete nonExistent` → return `true`

**Implementation:** Add a `js_delete_property(obj, key)` runtime function that removes the key from the Map via `map_remove()` or similar. In the transpiler, detect `delete expr` where `expr` is a `MEMBER_EXPRESSION` and emit the call.

**Files:** `lambda/js/js_runtime.cpp`, `lambda/js/transpile_js_mir.cpp`

### A5. Regex Literals in AST

**Status:** Per v8 doc, regex-dna and crypto-aes work via existing RE2 infrastructure and `String.replace()`. However, **regex literals** (`/pattern/flags`) are **not parsed as AST nodes** — they are not recognized in `build_js_ast.cpp`. The v8 benchmarks likely used string patterns rather than literal regex.

**Patterns commonly used in JS:**
```javascript
const re = /^[a-z]+$/i;
if (re.test(str)) { ... }
const matches = str.match(/\d+/g);
const result = str.replace(/foo/g, "bar");
```

**Implementation:**

Tree-sitter-javascript produces `"regex"` nodes with pattern and flags. Add to `build_js_ast.cpp`:
```cpp
} else if (strcmp(node_type, "regex") == 0) {
    // Extract pattern from /pattern/flags
    // Create a JS_AST_NODE_LITERAL with type REGEX, or
    // Add JS_AST_NODE_REGEX_LITERAL as a new node type
}
```

In the transpiler, compile regex literals to calls that create a pre-compiled RE2 pattern object. Add `regex.test()`, `regex.exec()`, and regex-aware `String.match()`.

**Files:** `lambda/js/build_js_ast.cpp`, `lambda/js/js_ast.hpp`, `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`

---

## 3. Phase B: Essential Standard Library — Object & JSON

**Goal:** Implement `JSON.parse()`/`JSON.stringify()` and complete `Object` methods.
**Impact:** JSON is the most critical missing built-in — virtually every real JS program uses it.
**Estimated effort:** ~500 LOC.

### B1. `JSON.parse(str)` and `JSON.stringify(obj)`

**Status:** Completely absent. No `JSON` reference anywhere in the JS engine.

**Usage:** Ubiquitous. Data exchange, configuration, serialization, API communication, deep cloning (`JSON.parse(JSON.stringify(obj))`).

**Implementation strategy:** Lambda already has a full JSON parser (`lambda/input/input-json.cpp`) and JSON formatter (`lambda/format/`). Wire them into the JS runtime:

```c
extern "C" Item js_json_parse(Item str) {
    String* s = it2s(js_to_string(str));
    if (!s) return ItemNull;
    // Use Lambda's existing JSON parser
    Input* input = input_create(runtime);
    Item result = input_parse_json(input, s->chars, s->len);
    // Convert Lambda data structures to JS objects if needed
    return result;
}

extern "C" Item js_json_stringify(Item value, Item replacer, Item space) {
    // Use Lambda's existing JSON formatter
    StrBuf* buf = strbuf_new();
    format_json(value, buf, /* indent from space arg */);
    String* result = heap_strcpy(buf->str, buf->length);
    strbuf_free(buf);
    return (Item){.item = s2it(result)};
}
```

**Transpiler dispatch** (in `jm_transpile_call()`):
```cpp
// JSON.parse(str)
if (obj_name == "JSON" && prop_name == "parse") {
    return jm_call_1(mt, "js_json_parse", ...);
}
// JSON.stringify(value, replacer?, space?)
if (obj_name == "JSON" && prop_name == "stringify") {
    return jm_call_3(mt, "js_json_stringify", ...);
}
```

**Key consideration:** Lambda's internal data representation uses `Map` for objects and `Array` for arrays — these are the same types that the JS engine uses. So `JSON.parse()` should return data that is directly usable as JS objects without conversion, provided the JSON parser creates Lambda Maps/Arrays.

**Files:** `lambda/js/js_globals.cpp`, `lambda/js/transpile_js_mir.cpp`, `lambda/sys_func_registry.c`

### B2. `Object.values(obj)` and `Object.entries(obj)`

**Status:** `Object.keys()` is implemented. `Object.values()` and `Object.entries()` are not.

**Implementation:** Mirror `js_object_keys()` — walk `ShapeEntry` linked list:

```c
extern "C" Item js_object_values(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return js_array_new(0);
    Map* m = obj.map;
    Item result = js_array_new(0);
    ShapeEntry* p = (ShapeEntry*)m->type->first;
    while (p) {
        if (p->key) {
            Item val = map_get(m, p->key);
            js_array_push(result, val);
        }
        p = p->next;
    }
    return result;
}

extern "C" Item js_object_entries(Item obj) {
    // Returns array of [key, value] pairs
    if (get_type_id(obj) != LMD_TYPE_MAP) return js_array_new(0);
    Map* m = obj.map;
    Item result = js_array_new(0);
    ShapeEntry* p = (ShapeEntry*)m->type->first;
    while (p) {
        if (p->key) {
            Item pair = js_array_new(2);
            js_array_set(pair, make_int(0), (Item){.item = s2it(p->key)});
            js_array_set(pair, make_int(1), map_get(m, p->key));
            js_array_push(result, pair);
        }
        p = p->next;
    }
    return result;
}
```

### B3. `Object.assign(target, ...sources)`

**Status:** Not implemented. Very commonly used for shallow object merging.

```c
extern "C" Item js_object_assign(Item target, Item* sources, int count) {
    if (get_type_id(target) != LMD_TYPE_MAP) return target;
    for (int i = 0; i < count; i++) {
        Item source = sources[i];
        if (get_type_id(source) != LMD_TYPE_MAP) continue;
        Map* m = source.map;
        ShapeEntry* p = (ShapeEntry*)m->type->first;
        while (p) {
            if (p->key) {
                Item val = map_get(m, p->key);
                js_property_set(target, (Item){.item = s2it(p->key)}, val);
            }
            p = p->next;
        }
    }
    return target;
}
```

### B4. `Object.freeze(obj)` and `Object.isFrozen(obj)`

**Status:** Not implemented. Used for immutability patterns, configuration objects, library constants.

**Implementation:** Add a `frozen` flag to the Map struct or store it as a hidden property. `Object.freeze` sets the flag. `js_property_set` checks the flag before modifying.

**Minimal approach:** Store `__frozen__` as a boolean property. In `js_property_set()`, check for this flag before any write:
```c
if (get_type_id(obj) == LMD_TYPE_MAP) {
    Item frozen = map_get(obj.map, "__frozen__");
    if (js_is_truthy(frozen)) return obj; // silently ignore writes
}
```

### B5. `obj.hasOwnProperty(key)` and `Object.hasOwn(obj, key)`

**Status:** Not implemented.

```c
extern "C" Item js_has_own_property(Item obj, Item key) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return make_bool(false);
    String* k = it2s(js_to_string(key));
    if (!k) return make_bool(false);
    Item val = map_get(obj.map, k);  // direct lookup, no prototype chain
    return make_bool(val.item != ItemNull.item);
}
```

Add as both:
- Transpiler-recognized `obj.hasOwnProperty(key)` → `js_has_own_property(obj, key)`
- `Object.hasOwn(obj, key)` → `js_has_own_property(obj, key)` static dispatch

### B6. `Object.getPrototypeOf(obj)` and `Object.setPrototypeOf(obj, proto)`

**Status:** Internal C functions `js_get_prototype()` and `js_set_prototype()` exist but aren't exposed to JS code.

**Implementation:** Register as transpiler-dispatched `Object.getPrototypeOf()` / `Object.setPrototypeOf()` calls → delegate to existing runtime functions.

**Files:** `lambda/js/js_globals.cpp`, `lambda/js/js_runtime.cpp`, `lambda/js/transpile_js_mir.cpp`, `lambda/sys_func_registry.c`

---

## 4. Phase C: Essential Standard Library — Array & String Completion

**Goal:** Fill the most critical gaps in Array and String method dispatchers.
**Impact:** Unblocks programs using common array mutation and string formatting patterns.
**Estimated effort:** ~400 LOC.

### C1. `Array.prototype.splice(start, deleteCount, ...items)`

**Status:** Not implemented. Critical for in-place array manipulation.

**Usage:** DOM manipulation libraries, state management, data transformation.

```c
extern "C" Item js_array_splice(Item arr, int start, int delete_count,
                                 Item* items, int item_count) {
    Array* a = arr.array;
    if (start < 0) start = a->length + start;
    if (start < 0) start = 0;
    if (start > a->length) start = a->length;
    if (delete_count < 0) delete_count = 0;
    if (start + delete_count > a->length) delete_count = a->length - start;

    // Save deleted elements for return value
    Item deleted = js_array_new(delete_count);
    for (int i = 0; i < delete_count; i++) {
        js_array_set(deleted, make_int(i), a->items[start + i]);
    }

    // Shift elements and insert new items
    int shift = item_count - delete_count;
    int new_len = a->length + shift;
    // ... (resize array, memmove, insert items)

    return deleted;
}
```

### C2. `Array.prototype.shift()` and `unshift(item)`

**Status:** Not implemented. Used in queue-based algorithms, richards benchmark (mentioned in v8 doc as needed).

```c
// shift: remove and return first element
extern "C" Item js_array_shift(Item arr) {
    Array* a = arr.array;
    if (a->length == 0) return ItemNull;
    Item first = a->items[0];
    memmove(&a->items[0], &a->items[1], (a->length - 1) * sizeof(Item));
    a->length--;
    return first;
}

// unshift: prepend item, return new length
extern "C" Item js_array_unshift(Item arr, Item item) {
    Array* a = arr.array;
    // Ensure capacity, memmove existing items right by 1, insert at [0]
    a->length++;
    return make_int(a->length);
}
```

### C3. `Array.prototype.lastIndexOf(item, fromIndex?)`

**Status:** `indexOf` exists, `lastIndexOf` does not. Scans from end to start.

### C4. `Array.prototype.flatMap(callback)`

**Status:** `flat` (one-level) and `map` are both implemented. `flatMap` combines them.

```c
// flatMap: map each element, then flatten one level
// In js_array_method:
if (method == "flatMap") {
    Item mapped = /* call map with callback */;
    return /* call flat(1) on mapped */;
}
```

### C5. `Array.from(iterable)` and `Array.of(...items)`

**Status:** Not implemented. `Array.from()` is commonly used to convert array-like objects.

```c
extern "C" Item js_array_from(Item iterable) {
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_ARRAY) {
        // Shallow copy
        Array* src = iterable.array;
        Item result = js_array_new(src->length);
        memcpy(result.array->items, src->items, src->length * sizeof(Item));
        return result;
    }
    if (tid == LMD_TYPE_STRING) {
        // Split string into array of single characters
        String* s = it2s(iterable);
        Item result = js_array_new(s->len);
        for (int i = 0; i < (int)s->len; i++) {
            String* ch = heap_strcpy(&s->chars[i], 1);
            js_array_push(result, (Item){.item = s2it(ch)});
        }
        return result;
    }
    // Handle typed arrays, Map, Set when available
    return js_array_new(0);
}
```

**Transpiler dispatch:**
```cpp
if (obj_name == "Array" && prop_name == "from") → js_array_from
if (obj_name == "Array" && prop_name == "of")   → build array from args
```

### C6. `Array.prototype.reverse()` — Fix In-place Mutation

**Status:** Implemented but **deviates from spec** — returns a new reversed array instead of mutating in-place. JS spec says `reverse()` modifies the original array.

**Fix:** Change the implementation in `js_array_method()` to swap elements in-place:
```c
if (method == "reverse") {
    Array* a = arr.array;
    for (int i = 0, j = a->length - 1; i < j; i++, j--) {
        Item tmp = a->items[i];
        a->items[i] = a->items[j];
        a->items[j] = tmp;
    }
    return arr;  // return same array
}
```

### C7. `String.prototype.match(pattern)` and `matchAll(pattern)`

**Status:** Not in `js_string_method`. The v8 doc says regex benchmarks work, but via Lambda's `fn_replace` — `match()` itself is absent.

**Implementation:** Delegate to Lambda's RE2 wrapper:
```c
if (method == "match") {
    // If pattern is a regex literal string or RegExp object
    // Use re2_find_all(str, pattern) for global, re2_partial_match for non-global
    return re2_find_all(str, pattern_str);
}
```

### C8. `String.prototype.search(pattern)`

Returns the index of the first match, or -1.

### C9. `String.prototype.replaceAll(pattern, replacement)`

```c
if (method == "replaceAll") {
    return fn_replace_all(str, args[0], args[1]);
}
```

### C10. `String.prototype.padStart(targetLen, padStr)` and `padEnd`

```c
if (method == "padStart") {
    String* s = it2s(str);
    int target = (int)js_get_number(args[0]);
    String* pad = argc > 1 ? it2s(js_to_string(args[1])) : heap_create_name(" ");
    if (s->len >= target) return str;
    // Build padded string
    int pad_needed = target - s->len;
    StrBuf* buf = strbuf_new();
    for (int i = 0; i < pad_needed; i++) {
        strbuf_append_char(buf, pad->chars[i % pad->len]);
    }
    strbuf_append_str_n(buf, s->chars, s->len);
    String* result = heap_strcpy(buf->str, buf->length);
    strbuf_free(buf);
    return (Item){.item = s2it(result)};
}
```

### C11. `String.prototype.at(index)` and `Array.prototype.at(index)`

Supports negative indexing: `arr.at(-1)` returns last element.

### C12. `Array.prototype.reduceRight(callback, initialValue)`

Same as `reduce` but iterates in reverse.

**Files:** `lambda/js/js_runtime.cpp` (method dispatchers), `lambda/js/js_globals.cpp`, `lambda/js/transpile_js_mir.cpp` (static dispatch for `Array.from`, `Array.of`)

---

## 5. Phase D: Function.prototype.bind & Error Subclasses

**Goal:** Implement `bind()` for partial application / `this` binding, and add proper Error subclasses.
**Impact:** `bind` is critical for callback-heavy code (event handlers, React class components, array method callbacks).
**Estimated effort:** ~200 LOC.

### D1. `Function.prototype.bind(thisArg, ...partialArgs)`

**Status:** Not implemented. `call` and `apply` exist.

**Usage:** Event handlers, method borrowing, partial application, class component methods in React.

```javascript
const greet = person.greet.bind(person);
setTimeout(greet, 1000);  // 'this' is preserved

const add5 = add.bind(null, 5);  // partial application
add5(3);  // 8
```

**Implementation:**

Create a new `JsBoundFunction` struct or extend `JsFunction`:
```c
typedef struct {
    TypeId type_id;       // LMD_TYPE_FUNC
    void* func_ptr;       // Points to js_bound_call trampoline
    int param_count;      // Remaining params after partial args
    Item* env;            // NULL (bound functions are special)
    int env_size;
    Item prototype;
    // Bound-specific fields:
    Item target_func;     // Original function to call
    Item bound_this;      // Fixed 'this' value
    Item* bound_args;     // Partial arguments
    int bound_arg_count;
} JsBoundFunction;

extern "C" Item js_function_bind(Item func, Item this_arg, Item* args, int arg_count) {
    JsBoundFunction* bf = pool_calloc(sizeof(JsBoundFunction));
    bf->type_id = LMD_TYPE_FUNC;
    bf->func_ptr = (void*)js_bound_trampoline;
    bf->target_func = func;
    bf->bound_this = this_arg;
    bf->bound_arg_count = arg_count;
    if (arg_count > 0) {
        bf->bound_args = pool_calloc(arg_count * sizeof(Item));
        memcpy(bf->bound_args, args, arg_count * sizeof(Item));
    }
    return make_func_item(bf);
}

// Trampoline called when bound function is invoked
static Item js_bound_trampoline(Item* env_or_args, ...) {
    JsBoundFunction* bf = /* recover from context */;
    // Concatenate bound_args + new args
    // Call js_call_function(bf->target_func, bf->bound_this, all_args, total_count)
}
```

**Alternative (simpler):** Detect `.bind()` in the transpiler like `.call()` and `.apply()`. Create a closure that captures `this` and partial args:
```cpp
if (prop_name == "bind") {
    // Create a wrapper closure that calls the original function with bound this
    MIR_reg_t fn = jm_transpile_box_item(mt, m->object);
    MIR_reg_t this_arg = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
    MIR_reg_t bound_args = jm_build_args_array(mt, call->arguments->next, arg_count - 1);
    return jm_call_3(mt, "js_function_bind", MIR_T_I64, fn, this_arg, bound_args, arg_count - 1);
}
```

### D2. Error Subclasses (`TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`)

**Status:** Only generic `Error` via `js_new_error()`. Real JS code uses:`throw new TypeError("expected a string")`.

**Implementation:**
```c
extern "C" Item js_new_typed_error(Item type_name, Item message) {
    Item obj = js_new_object();
    js_property_set(obj, make_string("name"), type_name);
    js_property_set(obj, make_string("message"), message);
    // Optionally set .stack for debugging
    return obj;
}
```

In transpiler, detect `new TypeError(msg)`, `new RangeError(msg)`, etc. as built-in constructors:
```cpp
if (ctor_name == "TypeError" || ctor_name == "RangeError" || ...) {
    MIR_reg_t type_str = jm_box_string_literal(mt, ctor_name, ctor_len);
    MIR_reg_t msg = first_arg ? first_arg : jm_emit_null(mt);
    return jm_call_2(mt, "js_new_typed_error", MIR_T_I64, type_str, msg);
}
```

**Files:** `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`, `lambda/js/transpile_js_mir.cpp`

---

## 6. Phase E: Map and Set Collections

**Goal:** Implement ES6 `Map` and `Set` built-in collections.
**Impact:** Used by many JS libraries and applications for efficient key-value storage and unique collections.
**Estimated effort:** ~350 LOC.

### E1. `Map` Collection

**Status:** Not implemented. JS `Map` differs from plain objects: any value (including objects) can be a key, maintains insertion order, has `.size`, `.get()`, `.set()`, `.has()`, `.delete()`, `.forEach()`, `.keys()`, `.values()`, `.entries()`, `.clear()`.

**Implementation strategy:** Use Lambda's existing `HashMap` from `lib/hashmap.h` as the backing store, wrapped in a Map with a type marker (similar to typed arrays and DOM wrappers).

```c
typedef struct {
    struct hashmap* hm;  // lib/hashmap.h hash map
    int size;
} JsMap;

extern "C" Item js_map_new() {
    JsMap* m = pool_calloc(sizeof(JsMap));
    m->hm = hashmap_new(sizeof(JsMapEntry), 0, 0, 0, js_map_hash, js_map_cmp, NULL, NULL);
    m->size = 0;
    // Wrap as a Map with type marker
    Map* wrapper = heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type = &js_map_type_marker;
    wrapper->data = (void*)m;
    return (Item){.map = wrapper};
}
```

**Methods to implement:**
| Method | Implementation |
|--------|---------------|
| `map.set(key, value)` | `hashmap_set`, return `map` for chaining |
| `map.get(key)` | `hashmap_get`, return value or `undefined` |
| `map.has(key)` | `hashmap_get != NULL` |
| `map.delete(key)` | `hashmap_delete` |
| `map.clear()` | `hashmap_clear` |
| `map.size` | Property, read from `JsMap.size` |
| `map.forEach(callback)` | Iterate hashmap, call callback with `(value, key, map)` |
| `map.keys()` | Return array of keys (simplified — no iterator) |
| `map.values()` | Return array of values |
| `map.entries()` | Return array of `[key, value]` pairs |
| `new Map(iterable)` | Construct from `[[key,val], ...]` array |

### E2. `Set` Collection

**Implementation:** Similar to `Map` but stores only keys (values are ignored).

**Methods:** `add`, `has`, `delete`, `clear`, `size`, `forEach`, `keys`, `values`, `entries`.

**Files:** `lambda/js/js_globals.cpp` (or new `js_collections.cpp`), `lambda/js/js_runtime.h`, `lambda/js/transpile_js_mir.cpp`, `lambda/sys_func_registry.c`

---

## 7. Phase F: Number Static Methods & Date Instance Methods

**Goal:** Complete `Number` static methods and add essential `Date` instance methods.
**Estimated effort:** ~200 LOC.

### F1. `Number.isInteger()`, `Number.isFinite()`, `Number.isNaN()`, `Number.isSafeInteger()`

**Status:** Global `isNaN()` and `isFinite()` exist, but `Number.isNaN()` (no type coercion) and `Number.isInteger()` do not.

```c
extern "C" Item js_number_is_integer(Item val) {
    TypeId tid = get_type_id(val);
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64) return make_bool(true);
    if (tid == LMD_TYPE_FLOAT) {
        double d = it2f(val);
        return make_bool(isfinite(d) && d == floor(d));
    }
    return make_bool(false);
}

extern "C" Item js_number_is_nan(Item val) {
    // Unlike global isNaN(), Number.isNaN() does NOT coerce
    if (get_type_id(val) != LMD_TYPE_FLOAT) return make_bool(false);
    return make_bool(isnan(it2f(val)));
}

extern "C" Item js_number_is_finite(Item val) {
    TypeId tid = get_type_id(val);
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64) return make_bool(true);
    if (tid == LMD_TYPE_FLOAT) return make_bool(isfinite(it2f(val)));
    return make_bool(false);
}
```

### F2. Essential Date Methods

**Status:** `new Date()` creates `{_time: ms_since_epoch}`. `getTime()` is inlined to `js_date_now()` in the transpiler. No other Date methods.

**Methods to add:**
```c
extern "C" Item js_date_get_time(Item date_obj) {
    return js_property_get(date_obj, make_string("_time"));
}

extern "C" Item js_date_get_full_year(Item date_obj) {
    double ms = js_get_number(js_property_get(date_obj, make_string("_time")));
    time_t t = (time_t)(ms / 1000.0);
    struct tm* tm = localtime(&t);
    return make_int(tm->tm_year + 1900);
}

// Similarly: getMonth, getDate, getHours, getMinutes, getSeconds
```

**Files:** `lambda/js/js_globals.cpp`, `lambda/js/js_runtime.h`, `lambda/js/transpile_js_mir.cpp`

---

## 8. Phase G: Miscellaneous Built-ins

**Goal:** Fill remaining gaps for commonly used globals and utility functions.
**Estimated effort:** ~200 LOC.

### G1. `setTimeout` / `setInterval` (Synchronous Shim)

LambdaJS is single-threaded with no event loop. For benchmark compatibility:
```c
extern "C" Item js_setTimeout(Item callback, Item delay) {
    // Execute immediately (no actual scheduling)
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, ItemNull, NULL, 0);
    }
    return make_int(0);  // return fake timer ID
}
```

### G2. `encodeURIComponent` / `decodeURIComponent`

```c
extern "C" Item js_encode_uri_component(Item str) {
    String* s = it2s(js_to_string(str));
    StrBuf* buf = strbuf_new();
    for (int i = 0; i < (int)s->len; i++) {
        unsigned char c = s->chars[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            strbuf_append_char(buf, c);
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            strbuf_append_str(buf, hex);
        }
    }
    String* result = heap_strcpy(buf->str, buf->length);
    strbuf_free(buf);
    return (Item){.item = s2it(result)};
}
```

### G3. `Array.prototype.toString()` and `Object.prototype.toString()`

**Status:** `js_to_string` for arrays returns `"[object Array]"` (TODO comment in code). Should return comma-separated elements like JS spec (`[1,2,3].toString()` → `"1,2,3"`).

Fix in `js_to_string()`:
```c
case LMD_TYPE_ARRAY: {
    Array* a = value.array;
    StrBuf* buf = strbuf_new();
    for (int i = 0; i < a->length; i++) {
        if (i > 0) strbuf_append_char(buf, ',');
        Item elem_str = js_to_string(a->items[i]);
        String* s = it2s(elem_str);
        if (s) strbuf_append_str_n(buf, s->chars, s->len);
    }
    String* result = heap_strcpy(buf->str, buf->length);
    strbuf_free(buf);
    return (Item){.item = s2it(result)};
}
```

### G4. `Array.prototype.sort()` — Replace O(n²) with O(n log n)

**Status:** Uses insertion sort. Correct but O(n²) for large arrays.

**Fix:** Replace with `qsort_r()` (macOS) or `qsort_s()`:
```c
static int js_sort_compare(const void* a, const void* b, void* ctx) {
    Item* comparator = (Item*)ctx;
    if (comparator->item == ItemNull.item) {
        // Default: lexicographic comparison
        Item sa = js_to_string(*(Item*)a);
        Item sb = js_to_string(*(Item*)b);
        return strcmp(it2s(sa)->chars, it2s(sb)->chars);
    }
    // Custom comparator
    Item args[2] = {*(Item*)a, *(Item*)b};
    Item result = js_call_function(*comparator, ItemNull, args, 2);
    return (int)js_get_number(result);
}
```

### G5. `String.prototype.slice()` — Negative Index Fix

**Status:** Uses same implementation as `substring()`. JS spec says `slice(-2)` counts from end, while `substring(-2)` clamps to 0. They have different semantics.

**Fix:** Add a separate `slice` handler that correctly supports negative indices:
```c
if (method == "slice") {
    int start = (int)js_get_number(args[0]);
    int end = argc > 1 ? (int)js_get_number(args[1]) : s->len;
    if (start < 0) start = s->len + start;
    if (end < 0) end = s->len + end;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    // Extract substring
}
```

**Files:** `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`, `lambda/js/transpile_js_mir.cpp`

---

## 9. Phase H: Performance & Correctness Improvements

**Goal:** Address known spec deviations and performance bottlenecks.
**Estimated effort:** ~300 LOC.

### H1. `instanceof` — Proper Constructor Chain Matching

**Status:** Uses non-standard `__class_name__` string marker. Should walk the prototype chain and compare constructor references.

**Current behavior:** `instanceof` checks if any prototype in the chain has a `__class_name__` matching the constructor name string. This breaks for:
- Same-named classes from different scopes
- Anonymous constructor functions
- `Symbol.hasInstance` (future)

**Fix:**
```c
extern "C" Item js_instanceof_proper(Item obj, Item constructor) {
    Item ctor_proto = js_property_get(constructor, make_string("prototype"));
    if (ctor_proto.item == ItemNull.item) return make_bool(false);
    Item proto = js_get_prototype(obj);
    for (int depth = 0; depth < 32; depth++) {
        if (proto.item == ItemNull.item) return make_bool(false);
        if (proto.item == ctor_proto.item) return make_bool(true);
        proto = js_get_prototype(proto);
    }
    return make_bool(false);
}
```

### H2. `this` Binding — Arrow Functions

**Status:** `this` is a global variable set/restored around `js_call_function`. Arrow functions should capture `this` from the enclosing scope (lexical `this`), not from the call site.

**Current behavior unclear:** If arrow functions don't call `js_set_this`, they inherit the enclosing `this` by reading `js_current_this`. This is correct IF the arrow is called synchronously within its defining scope. But if the arrow is stored and called later (e.g., in a callback), the `this` value may have changed.

**Fix:** Capture `this` as a closure variable for arrow functions:
```cpp
// In function compilation, when processing an arrow function:
if (fn->is_arrow) {
    // Add current 'this' to the closure capture list
    // Store js_get_this() at the time of arrow creation
}
```

### H3. `js_invoke_fn` Max Args Extension (7 → 16)

**Status:** `js_invoke_fn` uses a switch-case trampoline with typed function pointer casts `P0`–`P8`. Maximum 7 user arguments (8 with env for closures).

**Impact:** Any JS function with more than 7 parameters fails silently.

**Fix:** Extend the switch to `P16`, or use a variadic calling convention:
```c
// Add cases P9 through P16:
case 9: return ((P9)fn->func_ptr)(a0, a1, a2, a3, a4, a5, a6, a7, a8);
// ...
case 16: return ((P16)fn->func_ptr)(a0, a1, ..., a15);
```

### H4. `charCodeAt` — UTF-16 Correctness

**Status:** Returns byte value (ASCII/Latin1 only). JS `charCodeAt` returns UTF-16 code units.

**Fix:** For characters within BMP (U+0000 to U+FFFF), decode UTF-8 to get the Unicode code point. For characters outside BMP, return surrogate pairs.

### H5. `console.log` — Object Inspection

**Status:** Objects print as `"[object Object]"`. Should output a JSON-like representation for debugging.

**Fix:** Use a recursive printer that formats objects as `{ key: value, ... }` and arrays as `[ elem, ... ]`, with cycle detection (max depth).

### H6. `__proto__` Exclusion from `Object.keys()`

**Status:** `__proto__` is stored as a regular map property. `Object.keys()` includes it, which is incorrect per JS spec (non-enumerable).

**Fix:** Filter `"__proto__"` in `js_object_keys()`:
```c
while (p) {
    if (p->key && strcmp(p->key->chars, "__proto__") != 0 &&
        strcmp(p->key->chars, "__class_name__") != 0) {
        // include key
    }
    p = p->next;
}
```

**Files:** `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`, `lambda/js/transpile_js_mir.cpp`

---

## 10. Phase I: Optional — Advanced Features

These features are lower priority but would significantly expand LambdaJS's compatibility with modern JavaScript codebases.

### I1. Optional Chaining (`?.`)

**Status:** Not parsed, not transpiled.

```javascript
const name = user?.address?.city;
obj?.method?.(args);
arr?.[index];
```

**Implementation:** Tree-sitter-javascript produces `optional_chain_expression` nodes. In the transpiler, check if the left-hand side is null/undefined before accessing the property.

### I2. Nullish Coalescing Assignment (`??=`), Logical Assignment (`&&=`, `||=`)

**Status:** `??` is implemented. The compound forms `??=`, `&&=`, `||=` are ES2021 additions.

### I3. Generators and `yield`

**Status:** `JsFunctionNode` has `is_generator` field (always `false`). Not parsed or transpiled.

**Complexity:** High — requires coroutine-like state machine transform. Consider a simple iterator protocol first.

### I4. `async`/`await`

**Status:** `JsFunctionNode` has `is_async` field (always `false`). Not parsed or transpiled.

**Complexity:** Very high — requires Promise implementation and event loop. Not practical for a synchronous runtime.

### I5. ES Modules (`import`/`export`)

**Status:** Not parsed, not transpiled. Would require a module loader and resolution system.

### I6. `Symbol.iterator` and Iterable Protocol

**Status:** `for...of` works on arrays. Custom iterables (objects implementing `Symbol.iterator`) don't work.

### I7. WeakMap / WeakRef

**Status:** Not implemented. Useful for caches and avoiding memory leaks.

---

## 11. Implementation Priority & Dependencies

```
Phase A (Language syntax)        ──→ Broader program compatibility    [P0, MEDIUM effort]
  ├── A1 Object destructuring     (HIGH priority — extremely common)
  ├── A2 Sequence expressions     (MEDIUM — minified code, for-loops)
  ├── A3 Labeled statements       (LOW — rare but blocking)
  ├── A4 delete (real impl)       (LOW — correctness)
  └── A5 Regex literals           (MEDIUM — enables /pattern/ syntax)

Phase B (JSON + Object)          ──→ Critical for any real program    [P0, MEDIUM effort]
  ├── B1 JSON.parse/stringify     (HIGHEST priority — ubiquitous)
  ├── B2 Object.values/entries    (HIGH — very common)
  ├── B3 Object.assign            (HIGH — merging/cloning)
  ├── B4 Object.freeze            (MEDIUM — immutability)
  ├── B5 hasOwnProperty           (MEDIUM — property checking)
  └── B6 Object.getPrototypeOf    (LOW — advanced OOP)

Phase C (Array + String)         ──→ Complete standard library core   [P0, MEDIUM effort]
  ├── C1 splice                   (HIGH — in-place array modification)
  ├── C2 shift / unshift          (HIGH — queue operations)
  ├── C3 lastIndexOf              (MEDIUM)
  ├── C4 flatMap                  (MEDIUM)
  ├── C5 Array.from / Array.of    (MEDIUM)
  ├── C6 reverse (fix in-place)   (LOW — correctness fix)
  ├── C7 String.match             (HIGH — regex usage)
  ├── C8 String.search            (MEDIUM)
  ├── C9 String.replaceAll        (MEDIUM)
  ├── C10 padStart / padEnd       (MEDIUM)
  ├── C11 at() for Array+String   (LOW)
  └── C12 reduceRight             (LOW)

Phase D (bind + Error types)     ──→ Callback & error patterns       [P1, LOW effort]
  ├── D1 Function.bind            (HIGH — callback code)
  └── D2 Error subclasses         (MEDIUM — typed errors)

Phase E (Map + Set)              ──→ ES6 collections                  [P1, MEDIUM effort]
  ├── E1 Map                      (HIGH — efficient key-value)
  └── E2 Set                      (MEDIUM — unique collections)

Phase F (Number + Date)          ──→ Utility completeness             [P2, LOW effort]
  ├── F1 Number static methods    (LOW)
  └── F2 Date instance methods    (LOW)

Phase G (Misc built-ins)         ──→ Correctness & quality-of-life   [P2, LOW effort]
  ├── G1 setTimeout shim          (LOW)
  ├── G2 encodeURIComponent       (MEDIUM)
  ├── G3 Array/Object toString    (MEDIUM — debugging)
  ├── G4 Sort O(n log n)          (LOW — performance)
  └── G5 String.slice fix         (LOW — correctness)

Phase H (Correctness)            ──→ Spec compliance                  [P2, MEDIUM effort]
  ├── H1 instanceof fix           (MEDIUM)
  ├── H2 Arrow function this      (MEDIUM)
  ├── H3 Max args 7→16           (LOW)
  ├── H4 charCodeAt UTF-16        (LOW)
  ├── H5 console.log objects      (LOW — debugging)
  └── H6 __proto__ key filtering  (LOW — correctness)

Phase I (Advanced)               ──→ Modern JS compat                 [P3, HIGH effort]
  ├── I1 Optional chaining ?.     (MEDIUM for modern code)
  ├── I2 ??=, &&=, ||=            (LOW)
  ├── I3 Generators               (HIGH effort, LOW priority)
  ├── I4 async/await              (VERY HIGH effort, LOW priority)
  ├── I5 ES Modules               (HIGH effort, MEDIUM priority)
  ├── I6 Symbol.iterator          (MEDIUM effort)
  └── I7 WeakMap/WeakRef          (MEDIUM effort)
```

### Recommended Execution Order

**Step 1 — Maximum impact per LOC (Phase A1 + B1 + C1–C2):**
1. Object destructuring → unblocks most modern JS code patterns
2. `JSON.parse` / `JSON.stringify` → unblocks data-driven programs
3. `splice`, `shift`, `unshift` → unblocks array-heavy algorithms

**Step 2 — Complete core standard library (Phase B2–B5 + C7–C10):**
4. `Object.values`, `Object.entries`, `Object.assign`, `hasOwnProperty`
5. `String.match`, `String.search`, `String.replaceAll`, `padStart`/`padEnd`

**Step 3 — Functions and errors (Phase D):**
6. `Function.prototype.bind` → unblocks callback patterns
7. Error subclasses → better error messages

**Step 4 — Collections (Phase E):**
8. `Map` and `Set` → ES6 code compatibility

**Step 5 — Correctness fixes (Phase G–H):**
9. `Array.toString`, `Sort O(n log n)`, `String.slice`, `instanceof`, `__proto__` filtering

**Step 6 — Modern syntax (Phase I — optional):**
10. Optional chaining, generators, async/await (if needed)

---

## 12. Estimated LOC & File Impact

| Phase | Estimated LOC | Files Modified |
|-------|----------:|----------------|
| A: Language syntax | ~400 | `build_js_ast.cpp`, `js_ast.hpp`, `transpile_js_mir.cpp` |
| B: JSON + Object | ~500 | `js_globals.cpp`, `transpile_js_mir.cpp`, `sys_func_registry.c` |
| C: Array + String | ~400 | `js_runtime.cpp`, `js_globals.cpp`, `transpile_js_mir.cpp` |
| D: bind + Error | ~200 | `js_runtime.cpp`, `js_globals.cpp`, `transpile_js_mir.cpp` |
| E: Map + Set | ~350 | `js_globals.cpp` (or new `js_collections.cpp`), `js_runtime.h`, `transpile_js_mir.cpp` |
| F: Number + Date | ~200 | `js_globals.cpp`, `transpile_js_mir.cpp` |
| G: Misc built-ins | ~200 | `js_runtime.cpp`, `js_globals.cpp` |
| H: Correctness | ~300 | `js_runtime.cpp`, `js_globals.cpp`, `transpile_js_mir.cpp` |
| **Total (P0+P1)** | **~1,850** | |
| **Total (all)** | **~2,550** | |

---

## 13. Test Plan

### Unit Tests (add to `test/test_js_gtest.cpp`)

```cpp
// Phase A: Object destructuring
TEST(JsTranspiler, ObjectDestructuringBasic) {
    run_js("const {x, y} = {x: 1, y: 2}; console.log(x + y)"); // 3
}
TEST(JsTranspiler, ObjectDestructuringRename) {
    run_js("const {name: n} = {name: 'hi'}; console.log(n)"); // hi
}
TEST(JsTranspiler, ObjectDestructuringDefault) {
    run_js("const {a = 10} = {}; console.log(a)"); // 10
}

// Phase B: JSON
TEST(JsTranspiler, JsonParse) {
    run_js("const obj = JSON.parse('{\"a\":1}'); console.log(obj.a)"); // 1
}
TEST(JsTranspiler, JsonStringify) {
    run_js("console.log(JSON.stringify({a:1, b:'hi'}))"); // {"a":1,"b":"hi"}
}

// Phase C: Array methods
TEST(JsTranspiler, ArraySplice) {
    run_js("const a = [1,2,3,4]; a.splice(1,2); console.log(a.length)"); // 2
}
TEST(JsTranspiler, ArrayShiftUnshift) {
    run_js("const a = [1,2,3]; a.shift(); a.unshift(0); console.log(a[0])"); // 0
}

// Phase D: bind
TEST(JsTranspiler, FunctionBind) {
    run_js("function add(a,b){return a+b} const add5=add.bind(null,5); console.log(add5(3))"); // 8
}

// Phase E: Map
TEST(JsTranspiler, MapBasic) {
    run_js("const m = new Map(); m.set('a',1); console.log(m.get('a'))"); // 1
}
```

### Integration Tests

```bash
# Test with real-world JS snippets
echo 'const {a, b, ...rest} = {a:1, b:2, c:3, d:4};
console.log(a, b, JSON.stringify(rest))' | ./lambda.exe js -
# Expected: 1 2 {"c":3,"d":4}

echo 'const data = JSON.parse("[1,2,3]");
console.log(data.map(x => x * 2).join(","))' | ./lambda.exe js -
# Expected: 2,4,6

echo 'const m = new Map([["a",1],["b",2]]);
console.log(m.size, m.has("a"), m.get("b"))' | ./lambda.exe js -
# Expected: 2 true 2
```

### Regression

```bash
make build-test && make test-lambda-baseline
python3 test/benchmark/jetstream/run_jetstream_ljs.py 1  # All 12 JetStream still pass
```

---

## 14. Design Principles

1. **Prioritize by real-world usage frequency.** Object destructuring and `JSON.parse/stringify` are used in virtually every modern JS program. Generators and async/await are important but can wait.

2. **Reuse Lambda infrastructure.** JSON parsing → `input-json.cpp`. JSON formatting → `format/`. Regex → `re2_wrapper.hpp`. HashMap → `lib/hashmap.h`. Don't reinvent.

3. **Correctness over completeness.** Fix `Array.reverse()` (in-place), `String.slice()` (negative index), `instanceof` (constructor chain), and `__proto__` filtering before adding new features.

4. **Test each feature in isolation.** Each new method or syntax feature gets its own GTest case in `test_js_gtest.cpp`.

5. **Maintain backward compatibility.** All 12/13 JetStream JS benchmarks and 62/62 suite benchmarks must continue passing after each phase.

6. **No thread safety requirement.** The engine is single-threaded. Global state (`js_current_this`, module vars) remains acceptable.

7. **Fail gracefully.** Unknown methods log a warning and return `undefined` rather than crashing. This allows partial compatibility with code that uses features not yet implemented.
