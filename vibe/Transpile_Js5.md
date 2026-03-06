# JavaScript Transpiler v5: Structural Enhancement for Benchmark Readiness

## 1. Executive Summary

This proposal defines the work needed to structurally enhance Lambda's JavaScript support so that **all 29 JS benchmarks** under `test/benchmark/` can compile and run, and the 7 currently disabled JS tests can be re-enabled. It builds on v3's runtime alignment and v4's direct MIR generation.

### Primary Goal

Run the JS versions of the larceny (11), kostya (7), and beng/CLBG (10) benchmark suites and produce correct, timed results — establishing a cross-language performance comparison with Lambda Script.

### Architecture Position

```
v1–v2: JS AST → C codegen → C2MIR → native     (removed)
v3:    Runtime alignment, GC, DOM, selectors
v4:    JS AST → direct MIR IR → native           (current)
v5:    Language coverage + typed arrays + closures (this proposal)
```

### Scope

| Area | What | Why |
|------|------|-----|
| **Control flow** | `switch`, `do...while`, `for...in`, `for...of` | 5 beng benchmarks use `for...of`; `switch` is fundamental |
| **Closures** | Captured variable lifting to heap-allocated environments | Needed by advanced_features, closures in benchmarks |
| **Typed arrays** | `Int32Array`, `Uint8Array`, `Float64Array` | 12/29 benchmarks; most benchmarks won't run without these |
| **`new` expressions** | `new Constructor(args)` | Creates typed arrays, objects, and is pervasive in JS |
| **Object property mutation** | `obj.prop = val`, `obj.prop += val` | 6+ benchmarks mutate object properties |
| **Global functions** | `parseInt`, `parseFloat`, `isNaN`, `Number()`, `String()` | 18 larceny+kostya, 10 beng benchmarks |
| **I/O & timing** | `process.stdout.write`, `process.hrtime.bigint()`, `console.log` args, `process.argv` | Every benchmark needs output and timing |
| **Destructuring** | `const [a, b] = arr`, `for (const [k, v] of ...)` | 4 benchmarks (levenshtein, fasta, knucleotide, regexredux) |
| **Missing string/array/math** | `.charCodeAt`, `String.fromCharCode`, `.toFixed`, `.fill`, `.splice`, variadic `Math.min/max` | 7+ benchmarks |
| **Re-enable disabled tests** | 7 tests currently DISABLED_ | Many should pass with v4's MIR transpiler already |

### Out of Scope (v5)

- **Modules** (`import`/`export`) — no benchmarks use them
- **Async/await, Promises, generators** — no benchmarks use them
- **Prototype chains** — hard-coded method dispatch covers all benchmark needs
- **`require('fs')` / Node.js I/O** — 3 beng benchmarks need file input; provide `--input` CLI flag instead
- **RegExp** — only 1 benchmark (regexredux.js); skip or port separately
- **BigInt arithmetic** — only 1 benchmark (pidigits.js); defer

---

## 2. Benchmark Feature Audit

### 2.1 Feature Requirements by Group

#### larceny/ (11 files) — Classical Algorithm Benchmarks

All 11 share an identical timing/output harness:

```javascript
const __t0 = process.hrtime.bigint();
// ... computation ...
const __t1 = process.hrtime.bigint();
process.stdout.write("name: PASS\n");
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
```

| File | Additional Features Beyond Harness |
|------|-------------------------------------|
| diviter.js | `let`, `while`, `+=`, `-=` — **already supported** |
| divrec.js | Recursion — **already supported** |
| pnpoly.js | Float arithmetic — **already supported** |
| gcbench.js | Object literals `{left:null, right:null}`, bitwise `<<` — **already supported** |
| ray.js | `Math.sqrt()`, float arithmetic — **already supported** |
| array1.js | `new Int32Array(n)` — **MISSING** |
| paraffins.js | `new Int32Array(n)`, `Math.trunc()`, bitwise `>>` — typed array **MISSING** |
| primes.js | `new Uint8Array(n)`, `.fill()` — typed array **MISSING** |
| puzzle.js | `new Array(n).fill()` — `new Array` **MISSING** |
| quicksort.js | `new Int32Array(n)` — typed array **MISSING** |
| triangl.js | `new Uint8Array(n).fill()`, `new Int32Array(n)` — typed array **MISSING** |

**Blocking features**: The timing harness (`process.hrtime.bigint`, BigInt, `Number()`, `process.stdout.write`) blocks all 11. Typed arrays block 6. Without typed arrays, 5/11 need only the harness.

#### kostya/ (7 files) — Computational Benchmarks

Same timing/output harness as larceny. Additional features:

| File | Additional Features |
|------|---------------------|
| collatz.js | Basic arithmetic — **already supported** |
| json_gen.js | `Math.trunc()`, `.push()`, `.join()` — **already supported** |
| base64.js | `new Uint8Array(n).fill()`, bitwise — typed array **MISSING** |
| matmul.js | `new Float64Array(n)`, `Math.trunc`, `Math.floor` — typed array **MISSING** |
| primes.js | `new Uint8Array(n)`, `.fill()` — typed array **MISSING** |
| brainfuck.js | `new Int32Array`, `new Uint8Array`, `.charCodeAt()`, `String.fromCharCode()` — typed array + string methods **MISSING** |
| levenshtein.js | `new Int32Array`, `.charCodeAt()`, array destructuring `[a,b]=[b,a]` — multiple **MISSING** |

**Blocking features**: Timing harness (all 7), typed arrays (5/7), `.charCodeAt`/`String.fromCharCode` (2/7), array destructuring (1/7).

#### beng/js/ (10 files) — Benchmarksgame (CLBG) Benchmarks

Different I/O pattern: `console.log()` + `process.argv` + `parseInt()`. No BigInt timing harness.

| File | Key Features Needed |
|------|---------------------|
| mandelbrot.js | `parseInt`, `process.argv`, bitwise — global functions **MISSING** |
| binarytrees.js | `parseInt`, `process.argv`, `Math.max`, template literals, recursion |
| fannkuch.js | `parseInt`, `process.argv`, `new Int32Array`, template literals |
| spectralnorm.js | `parseInt`, `process.argv`, `new Float64Array`, `.fill()`, `.toFixed()` |
| nbody.js | `parseInt`, `process.argv`, `.toFixed()`, object property mutation (`bi.vx -= dx * mag`), `for...of` |
| fasta.js | `parseInt`, `process.argv`, `for...of`, array destructuring in `for...of` |
| revcomp.js | `require('fs')`, `for...of`, `.split('')`, `.reverse()`, `.join('')` |
| knucleotide.js | `require('fs')`, `new Map()`, `.entries()`, spread `[...]`, `.sort()` with comparator, `for...of`, `.toFixed()` |
| regexredux.js | `require('fs')`, RegExp — **skip (v5)** |
| pidigits.js | BigInt arithmetic (`0n`, `1n`) — **skip (v5)** |

### 2.2 Consolidated Feature Priority

Ranked by benchmark count unblocked:

| # | Feature | Benchmarks Blocked | Category |
|---|---------|-------------------|----------|
| 1 | `process.stdout.write()` | 18 (all larceny + kostya) | I/O |
| 2 | `process.hrtime.bigint()` + BigInt subtract + `Number()` | 18 | Timing |
| 3 | Typed arrays (`Int32Array`, `Uint8Array`, `Float64Array`) | 12 | Data types |
| 4 | `parseInt()` | 10 (all beng) | Global functions |
| 5 | `process.argv` | 10 (all beng) | I/O |
| 6 | `new` expressions | 12+ (typed arrays + `new Array`) | Language syntax |
| 7 | `.fill()` on arrays/typed arrays | 6 | Array methods |
| 8 | `for...of` loops | 5 (fasta, knucleotide, nbody, regexredux, revcomp) | Control flow |
| 9 | Object property mutation | 5+ (nbody, several disabled tests) | Assignment |
| 10 | `.charCodeAt()` / `String.fromCharCode()` | 2 (brainfuck, levenshtein) | String methods |
| 11 | `.toFixed()` | 3 (spectralnorm, nbody, knucleotide) | Number methods |
| 12 | Array destructuring | 4 (levenshtein, fasta, knucleotide, regexredux) | Patterns |
| 13 | `switch`/`case` | — (no benchmarks, but fundamental JS) | Control flow |
| 14 | `do...while` | — (no benchmarks, but common) | Control flow |
| 15 | Closures (captured variables) | advanced_features test, general usage | Functions |
| 16 | `.sort()` with custom comparator | 1 (knucleotide) | Array methods |
| 17 | `new Map()` | 1 (knucleotide) | Collections |
| 18 | Variadic `Math.min`/`Math.max` | 1 (binarytrees) | Math |

---

## 3. Implementation Design

### 3.1 Typed Arrays

Typed arrays are the single most impactful missing feature — 12/29 benchmarks need them.

#### Data Model

Typed arrays are fixed-size, type-homogeneous buffers. Rather than introducing new Lambda types, represent them as a `Map` with a sentinel marker (same pattern as DOM wrappers):

```c
// js_typed_array.h

typedef enum JsTypedArrayType {
    JS_TYPED_INT8,
    JS_TYPED_UINT8,
    JS_TYPED_INT16,
    JS_TYPED_UINT16,
    JS_TYPED_INT32,
    JS_TYPED_UINT32,
    JS_TYPED_FLOAT32,
    JS_TYPED_FLOAT64,
} JsTypedArrayType;

typedef struct JsTypedArray {
    JsTypedArrayType element_type;
    int length;
    int byte_length;
    void* data;   // raw buffer (heap-allocated)
} JsTypedArray;

extern char js_typed_array_marker;  // sentinel for Map.type

// Core operations
Item js_typed_array_new(JsTypedArrayType type, int length);
Item js_typed_array_get(Item ta, int index);
void js_typed_array_set(Item ta, int index, Item value);
int  js_typed_array_length(Item ta);
Item js_typed_array_fill(Item ta, Item value);
bool js_is_typed_array(Item val);
```

**Storage**: A `Map` struct with `type = &js_typed_array_marker` and `data = JsTypedArray*`. This reuses the same zero-overhead wrapper pattern as DOM nodes.

**Element access**: `get`/`set` convert between the typed buffer's native representation and Lambda `Item` values. For `Int32Array`, `get` returns `i2it((int64_t)((int32_t*)ta->data)[index])`. For `Float64Array`, `get` returns `push_d(((double*)ta->data)[index])`.

**Why not Lambda Binary?**: Lambda has `LMD_TYPE_BINARY` for raw byte data, but it lacks typed element access and the benchmarks need indexed `arr[i]` semantics with numeric type preservation. A dedicated `JsTypedArray` struct gives us the right API surface.

#### Transpiler Integration

The `new Int32Array(n)` pattern requires:

1. **`new` expression** — recognize `new Ctor(args)` in the AST (see §3.3)
2. **Constructor dispatch** — in `jm_transpile_new_expr`, dispatch on constructor name:
   - `Int32Array` → `js_typed_array_new(JS_TYPED_INT32, n)`
   - `Uint8Array` → `js_typed_array_new(JS_TYPED_UINT8, n)`
   - `Float64Array` → `js_typed_array_new(JS_TYPED_FLOAT64, n)`
   - `Array` → `js_array_new(n)` (with pre-allocation)
3. **Bracket access** — `arr[i]` on typed arrays: extend `js_property_access` to check `js_is_typed_array()` and call `js_typed_array_get`
4. **Bracket assignment** — `arr[i] = val`: extend `js_property_set` to check and call `js_typed_array_set`
5. **`.fill()` method** — add to `js_array_method` dispatcher: if `js_is_typed_array(obj)`, call `js_typed_array_fill`
6. **`.length` property** — extend `fn_len` check or handle in property access dispatcher

#### Indexed Access Path

Currently, bracket access (`obj[expr]`) goes through `js_property_access(obj, key)`. This function dispatches on `get_type_id(obj)`. For typed arrays, add a check:

```c
Item js_property_access(Item obj, Item key) {
    TypeId type = get_type_id(obj);
    if (type == LMD_TYPE_MAP) {
        Map* m = (Map*)obj.map;
        if (m->type == &js_typed_array_marker) {
            int idx = (int)item_to_int64(key);
            return js_typed_array_get(obj, idx);
        }
        // ... existing MAP dispatch ...
    }
    // ... existing dispatches ...
}
```

Similarly for `js_property_set`:

```c
void js_property_set(Item obj, Item key, Item value) {
    TypeId type = get_type_id(obj);
    if (type == LMD_TYPE_MAP) {
        Map* m = (Map*)obj.map;
        if (m->type == &js_typed_array_marker) {
            int idx = (int)item_to_int64(key);
            js_typed_array_set(obj, idx, value);
            return;
        }
        // ... existing dispatch ...
    }
    // ...
}
```

### 3.2 I/O, Timing, and Global Functions

#### 3.2.1 `process` Object

All larceny/kostya benchmarks use `process.stdout.write(str)` and `process.hrtime.bigint()`. All beng benchmarks use `process.argv`.

**Implementation approach**: Recognize `process.*` as a special global in the transpiler, similar to `document.*` and `Math.*`.

```c
// js_runtime.cpp additions

// process.stdout.write — just prints the string, no newline
Item js_process_stdout_write(Item str_item) {
    String* s = it2s(js_to_string_item(str_item));
    if (s) fwrite(s->chars, 1, s->len, stdout);
    return (Item){.item = ITEM_TRUE};
}

// process.hrtime.bigint() — monotonic nanosecond timer
// Returns as Item float (double has ~53 bits of precision,
// sufficient for ~104 days of nanosecond timing)
Item js_process_hrtime_bigint() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double ns = (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
    return push_d(ns);
}

// process.argv — returns array of CLI arguments
// Set up once at JS execution start
static Item js_process_argv_array = {.item = ITEM_NULL};

void js_set_process_argv(int argc, const char** argv) {
    Array* arr = array();
    for (int i = 0; i < argc; i++) {
        array_push(arr, s2it(heap_create_name(argv[i])));
    }
    js_process_argv_array = array_end(arr);
}

Item js_process_get_argv() {
    return js_process_argv_array;
}
```

**Transpiler dispatch**: In `jm_transpile_call_expr`, detect `process.stdout.write(...)` and emit `js_process_stdout_write(arg)`. Detect `process.hrtime.bigint()` and emit `js_process_hrtime_bigint()`.

In `jm_transpile_member_expr`, detect `process.argv` and emit `js_process_get_argv()`.

**BigInt subtraction**: Since we represent the hrtime result as a `double` (not a true BigInt), `__t1 - __t0` naturally works via `js_subtract` → `fn_sub` and `Number(x)` is a no-op (already a number). This sidesteps the need for actual BigInt support — the timing pattern works transparently.

#### 3.2.2 Global Functions

| Function | Implementation |
|----------|----------------|
| `parseInt(str, radix)` | `js_parseInt(str, radix)` → wrapper around C `strtol` |
| `parseFloat(str)` | `js_parseFloat(str)` → wrapper around C `strtod` |
| `isNaN(value)` | `js_isNaN(val)` → `isnan(it2d(js_to_number(val)))` |
| `isFinite(value)` | `js_isFinite(val)` → `isfinite(it2d(js_to_number(val)))` |
| `Number(value)` | `js_to_number(val)` — already exists in runtime |
| `String(value)` | `js_to_string_item(val)` — already exists in runtime |

**Transpiler**: Detect these by identifier name in `jm_transpile_call_expr`:

```
if callee is Identifier "parseInt"   → jm_call_2(..., "js_parseInt", ...)
if callee is Identifier "parseFloat" → jm_call_1(..., "js_parseFloat", ...)
if callee is Identifier "isNaN"      → jm_call_1(..., "js_isNaN", ...)
if callee is Identifier "Number"     → jm_call_1(..., "js_to_number", ...)
if callee is Identifier "String"     → jm_call_1(..., "js_to_string_item", ...)
```

### 3.3 `new` Expressions

Needed by: typed arrays, `new Array(n)`, `new Map()`, and eventually classes.

#### AST Extension

Add to `js_ast.hpp`:

```c
JS_AST_NODE_NEW_EXPRESSION,
```

Add struct:

```c
typedef struct JsNewExpressionNode {
    JsAstNode base;
    JsAstNode* callee;        // constructor identifier/member
    JsAstNode* arguments;     // linked list of arguments
    int argument_count;
} JsNewExpressionNode;
```

#### AST Builder

In `build_js_ast.cpp`, handle `new_expression` from tree-sitter. The grammar structure is:

```
(new_expression
  constructor: (identifier)
  arguments: (arguments ...))
```

#### Transpiler

In `jm_transpile_expression`, add `JS_AST_NODE_NEW_EXPRESSION` case:

```c
case JS_AST_NODE_NEW_EXPRESSION: {
    JsNewExpressionNode* new_expr = (JsNewExpressionNode*)expr;
    if (new_expr->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        const char* name = ((JsIdentifierNode*)new_expr->callee)->name->chars;
        // Typed array constructors
        if (strcmp(name, "Int32Array") == 0)    return emit_typed_array_new(JS_TYPED_INT32, ...);
        if (strcmp(name, "Uint8Array") == 0)    return emit_typed_array_new(JS_TYPED_UINT8, ...);
        if (strcmp(name, "Float64Array") == 0)  return emit_typed_array_new(JS_TYPED_FLOAT64, ...);
        // Plain Array
        if (strcmp(name, "Array") == 0)         return emit_array_new_sized(...);
        // Map
        if (strcmp(name, "Map") == 0)           return emit_map_new(...);
        // Generic: call constructor and return object
        return emit_generic_new(new_expr);
    }
}
```

### 3.4 Control Flow Extensions

#### 3.4.1 `switch` / `case` / `default`

**AST additions**:

```c
JS_AST_NODE_SWITCH_STATEMENT,
JS_AST_NODE_SWITCH_CASE,
JS_AST_NODE_SWITCH_DEFAULT,

typedef struct JsSwitchNode {
    JsAstNode base;
    JsAstNode* discriminant;    // expression to match
    JsAstNode* cases;           // linked list of JsSwitchCaseNode
} JsSwitchNode;

typedef struct JsSwitchCaseNode {
    JsAstNode base;
    JsAstNode* test;            // NULL for default
    JsAstNode* consequent;      // linked list of statements
} JsSwitchCaseNode;
```

**MIR codegen**: Evaluate discriminant once. For each case, emit `js_strict_equal(disc, test)` + conditional branch. Fall-through semantics: each case body flows into the next unless a `break` is encountered. The `break` label targets the end of the switch.

```
         disc_reg = transpile(discriminant)
         case1_test = transpile(test1)
         BEQ js_strict_equal(disc_reg, case1_test), case1_body
         case2_test = transpile(test2)
         BEQ js_strict_equal(disc_reg, case2_test), case2_body
         JMP default_body (or end)
case1_body:
         transpile(consequent1)       // falls through unless break
case2_body:
         transpile(consequent2)
default_body:
         transpile(default_consequent)
end:
```

Push a break label onto the loop stack so `break` within switch cases exits correctly.

#### 3.4.2 `do...while`

**AST addition**:

```c
JS_AST_NODE_DO_WHILE_STATEMENT,

typedef struct JsDoWhileNode {
    JsAstNode base;
    JsAstNode* body;
    JsAstNode* test;
} JsDoWhileNode;
```

**MIR codegen**: Straightforward — emit body first, then test, then conditional jump back to body start. Push continue/break labels onto loop stack.

```
continue_label:
    transpile(body)
    test_reg = transpile(test)
    BT js_is_truthy(test_reg), continue_label
break_label:
```

#### 3.4.3 `for...in` / `for...of`

These require iteration protocol support.

**`for...of` on arrays/typed arrays** (covers 5 benchmarks):

```javascript
for (const elem of array) { ... }
// Desugars to:
for (let _i = 0; _i < array.length; _i++) { const elem = array[_i]; ... }
```

**`for...of` with destructuring** (covers 3 benchmarks):

```javascript
for (const [key, val] of entries) { ... }
// Desugars to:
for (let _i = 0; _i < entries.length; _i++) {
    const _tmp = entries[_i];
    const key = _tmp[0]; const val = _tmp[1];
    ...
}
```

**AST additions**:

```c
JS_AST_NODE_FOR_OF_STATEMENT,
JS_AST_NODE_FOR_IN_STATEMENT,

typedef struct JsForOfNode {
    JsAstNode base;
    JsAstNode* left;       // variable declaration or pattern
    JsAstNode* right;      // iterable expression
    JsAstNode* body;
} JsForOfNode;

// Same struct works for for...in
typedef JsForOfNode JsForInNode;
```

**`for...in` on objects**: Iterate over keys of a Map/object via `js_object_keys()` → array, then index loop.

**MIR codegen for `for...of`**:

```
    right_reg = transpile(right)
    len_reg = fn_len(right_reg)
    idx_reg = 0
loop_start:
    BF (idx_reg < len_reg), loop_end
    elem_reg = js_property_access(right_reg, idx_reg)
    // bind elem_reg to left variable (or destructure)
    transpile(body)
continue_label:
    idx_reg += 1
    JMP loop_start
loop_end:
```

### 3.5 Closures

True closures require **capturing** variables from outer scopes into a heap-allocated environment that the inner function can access at runtime. Currently, inner functions are compiled as independent MIR functions with no access to outer locals.

#### Design: Environment Passing

Follow the Lambda transpiler's closure pattern from `transpile-mir.cpp`:

1. **Detect captured variables**: During function collection pre-pass, walk each inner function's body and record variable references that resolve to an outer scope.

2. **Allocate environment struct**: For each function with captured variables, emit a `heap_calloc` for an `Item[]` environment array.

3. **Store captured values**: Before creating the function reference, store each captured variable into the environment array at a known offset.

4. **Modify inner function signature**: Add an `Item env` parameter. The inner function loads captured variables from `env[offset]` instead of from local registers.

5. **Create closure**: Use `js_new_closure(func_ptr, param_count, env)` instead of `js_new_function(func_ptr, param_count)`.

```c
// js_runtime.h additions

typedef struct JsClosure {
    void* func_ptr;
    int param_count;
    Item* env;          // heap-allocated environment
    int env_size;
} JsClosure;

Item js_new_closure(void* func_ptr, int param_count, Item* env, int env_size);
```

**Transpiler changes**:

```c
// In jm_collect_functions pre-pass, add:
static void jm_analyze_captures(JsMirTranspiler* jmt, JsFunctionNode* func) {
    // Walk function body, for each identifier reference:
    //   if not in func's own params/locals AND found in outer scope → mark as captured
    // Store capture list on func_node (use an arena-allocated array)
}

// In jm_define_function:
if (func has captures) {
    // Add 'env' parameter: MIR_T_I64
    // For each captured variable reference in body:
    //   instead of jm_find_var, load from env[capture_index]
}

// At call site where closure is created:
if (func has captures) {
    // emit: env = heap_calloc(n * sizeof(Item))
    // emit: env[0] = outer_var_0_reg
    // emit: env[1] = outer_var_1_reg
    // emit: js_new_closure(func_ref, param_count, env, n)
} else {
    // emit: js_new_function(func_ref, param_count)
}
```

**Mutable captures**: When a captured variable is mutated in either the outer or inner scope, both must share the same storage. The environment slot acts as this shared location:
- Outer scope: after capture, reads/writes go to `env[i]` instead of the local register
- Inner scope: reads/writes from `env[i]`

This is the "cells" pattern used by most closure implementations.

#### `js_call_function` Update

The current `js_call_function` dispatches via a `switch(arg_count)` limited to 4 args, and doesn't pass the environment. Two changes needed:

1. **Pass environment as first argument**: When calling a closure, prepend `env` to the argument list. The compiled inner function receives `(Item env, Item arg0, Item arg1, ...)`.

2. **Extend max arity**: The hard limit of 4 arguments is insufficient. Extend to at least 8, or switch to a model where args 0-7 are passed individually and args 8+ are passed via an `Item args[]` array parameter.

### 3.6 Object Property Mutation

Currently, the transpiler handles `x = val` for simple variables and compound assignments, but `obj.prop = val` and `arr[i] = val` are only partially wired.

**The runtime function `js_property_set` already handles the operation correctly.** The gap is purely in the transpiler.

#### Transpiler Fix

In `jm_transpile_assignment`, when the LHS is a `JS_AST_NODE_MEMBER_EXPRESSION`:

```c
case JS_AST_NODE_MEMBER_EXPRESSION: {
    JsMemberNode* member = (JsMemberNode*)assign->left;
    MIR_reg_t obj_reg = jm_transpile_expression(jmt, member->object);
    MIR_reg_t val_reg = jm_transpile_expression(jmt, assign->right);

    if (member->computed) {
        // arr[expr] = val
        MIR_reg_t key_reg = jm_transpile_expression(jmt, member->property);
        jm_call_void_3(jmt, "js_property_set", obj_reg, key_reg, val_reg);
    } else {
        // obj.prop = val
        MIR_reg_t key_reg = jm_box_string_literal(jmt, member->property_name);
        jm_call_void_3(jmt, "js_property_set", obj_reg, key_reg, val_reg);
    }

    // For compound assignments (+=, -=, etc.):
    // 1. get old = js_property_access(obj, key)
    // 2. new = js_add(old, rhs)  (or appropriate op)
    // 3. js_property_set(obj, key, new)
    return val_reg;
}
```

This unblocks:
- nbody.js (`bi.vx -= dx * mag`)
- The disabled `advanced_features.js` test
- Object-as-accumulator patterns in `array_methods.js`

### 3.7 Destructuring (Array)

Array destructuring is used by 4 benchmarks. Object destructuring is not used by any benchmark.

#### AST Builder: Array Pattern

The tree-sitter grammar produces `array_pattern` nodes for destructuring. Add handling in `build_js_ast.cpp`:

```c
// For variable declaration: const [a, b] = expr
// Build an JsArrayPatternNode with a list of identifier/rest elements
```

#### Transpiler: Destructure to Indexed Access

In `jm_transpile_var_declaration`, when the declarator's name is an `ARRAY_PATTERN`:

```c
// const [a, b, c] = expr
//
// Desugar to:
//   _tmp = transpile(expr)
//   a = js_property_access(_tmp, i2it(0))
//   b = js_property_access(_tmp, i2it(1))
//   c = js_property_access(_tmp, i2it(2))

MIR_reg_t tmp = jm_transpile_expression(jmt, init);
for (int i = 0; i < pattern->element_count; i++) {
    if (pattern->elements[i] is REST_ELEMENT) {
        // ...rest = arr.slice(i)
    } else {
        MIR_reg_t idx = jm_box_int_const(jmt, i);
        MIR_reg_t elem = jm_call_2(jmt, "js_property_access", tmp, idx);
        jm_set_var(jmt, element_name, elem);
    }
}
```

For `for...of` with destructuring (`for (const [k, v] of entries)`), apply the same pattern to foreach element extracted by the `for...of` desugaring.

### 3.8 Additional String/Array/Math Methods

#### String Methods to Add

| Method | Implementation | Benchmarks |
|--------|----------------|------------|
| `.charCodeAt(i)` | `js_string_charCodeAt(str, i)` → returns int of UTF-16 code unit | brainfuck, levenshtein |
| `String.fromCharCode(n)` | `js_string_fromCharCode(n)` → returns single-char string | brainfuck |
| `.toFixed(digits)` | `js_number_toFixed(num, digits)` → `snprintf(buf, "%.Nf", ...)` | spectralnorm, nbody, knucleotide |
| `.toString()` | `js_to_string_item(val)` — already exists | pidigits |

Add `.charCodeAt` and `.toFixed` to the `js_string_method` dispatcher. Add `String.fromCharCode` as a global function detected by the transpiler (similar to `parseInt`).

`.toFixed` is a **number method**, not a string method. Detect `expr.toFixed(n)` in the transpiler's method call path: when the receiver type is unknown, emit a call to `js_toFixed(receiver, n)` which checks type and formats.

#### Array Methods to Add

| Method | Implementation | Benchmarks |
|--------|----------------|------------|
| `.fill(value)` | `js_array_fill(arr, val)` / `js_typed_array_fill(ta, val)` | 6 benchmarks |
| `.splice(start, deleteCount, ...items)` | `js_array_splice(arr, start, del, items, items_count)` | — (useful but not benchmark-critical) |
| `.shift()` / `.unshift()` | `js_array_shift(arr)` / `js_array_unshift(arr, val)` | — |
| `Array.isArray(val)` | `js_is_array(val)` → check `LMD_TYPE_ARRAY` | — |
| `.sort(compareFn)` | Extend `js_array_method` sort case to accept callback | knucleotide |

For `.fill()`, add to `js_array_method` dispatcher. Also handle typed arrays:

```c
if (strcmp(method, "fill") == 0) {
    if (js_is_typed_array(receiver)) {
        return js_typed_array_fill(receiver, args[0]);
    }
    // Regular array fill
    Array* arr = (Array*)get_array(receiver);
    for (int i = 0; i < arr->length; i++) {
        arr->items[i] = args[0];
    }
    return receiver;
}
```

#### Math: Variadic `min`/`max`

Currently `Math.min` and `Math.max` accept exactly 2 arguments. Extend `js_math_method` to accept an args array and iterate:

```c
if (strcmp(method, "min") == 0 || strcmp(method, "max") == 0) {
    if (argc == 0) return push_d(strcmp(method, "min") == 0 ? INFINITY : -INFINITY);
    Item result = args[0];
    for (int i = 1; i < argc; i++) {
        result = strcmp(method, "min") == 0
            ? fn_min2(result, args[i])
            : fn_max2(result, args[i]);
    }
    return result;
}
```

### 3.9 `console.log` Multi-Argument + `console.error`/`warn`

The current `js_console_log` handles a single argument. Benchmarks use `console.log(templateLiteral)` (single arg), but disabled tests and general JS code use multiple arguments.

**Already handled**: The MIR transpiler's `console.log` codegen already iterates all arguments and calls `js_console_log` per argument. The runtime function prints its argument plus a newline. However, with multiple args, each gets a separate newline — incorrect.

**Fix**: Change the transpiler's multi-arg console.log to:
1. Call `js_console_log_start()` (no-op or print nothing)
2. For each arg: call `js_console_log_arg(arg)` (print value + space, no newline)
3. Call `js_console_log_end()` (print newline)

Or simpler: build the multi-arg version in the runtime:

```c
Item js_console_log_multi(Item* args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) fputc(' ', stdout);
        String* s = it2s(js_to_string_item(args[i]));
        if (s) fwrite(s->chars, 1, s->len, stdout);
    }
    fputc('\n', stdout);
    return ItemNull;
}
```

---

## 4. Re-Enable Disabled Tests

The v4 MIR transpiler implemented many features that were missing when these tests were first disabled (under the old C codegen path). Based on the v4 gap analysis, the following features are now implemented:

| Feature | Status in v4 MIR |
|---------|-----------------|
| `let`/`const` | **Implemented** |
| `===`/`!==` | **Implemented** (all 22 binary operators) |
| `&&`/`||`/`!` | **Implemented** |
| Ternary `? :` | **Implemented** |
| `break`/`continue` | **Implemented** |
| Object literals | **Implemented** |
| Array literals | **Implemented** |
| Bracket subscript | **Implemented** (computed member access) |
| Dot property access | **Implemented** |
| Function expressions | **Implemented** |
| Arrow functions | **Implemented** |
| Template literals | **Implemented** |

Tests that should pass now or with minimal fixes:

| Test | Expected Status | Remaining Blockers |
|------|----------------|--------------------|
| `basic_expressions.js` | **Should pass** | None — all features are in v4 |
| `functions.js` | **Should pass** | None — function expressions, arrows, indirect calls all in v4 |
| `control_flow.js` | **Should pass** | None — ternary, break, continue, recursion all in v4 |
| `es6_features.js` | **Needs closures, classes** | `this`, `class`, `new`, mutable closures |
| `advanced_features.js` | **Needs closures, obj mutation** | `this`, closures with mutable state, `obj.prop = val` |
| `error_handling.js` | **Needs try/catch, new Error** | `try/catch` not functional, `new` not implemented |
| `array_methods.js` | **Needs obj mutation** | Inline anonymous callbacks should work; `obj.prop = val` assignment, ternary in callbacks |

**Action**: Run each disabled test with the current v4 transpiler. Re-enable those that pass. For those that fail, identify the exact failing assertion and categorize the blocker.

---

## 5. File Structure

### New Files

```
lambda/js/
├── js_typed_array.h          # JsTypedArray struct, type enum, function declarations
├── js_typed_array.cpp         # Typed array creation, get/set, fill, length (~300 lines)
├── js_process.cpp             # process.stdout.write, process.hrtime.bigint, process.argv (~100 lines)
├── js_globals.cpp             # parseInt, parseFloat, isNaN, isFinite, Number(), String() (~150 lines)
```

### Modified Files

```
lambda/js/
├── js_ast.hpp                 # Add: new_expression, switch, do_while, for_of, for_in node types + structs
├── build_js_ast.cpp           # Add: AST builders for new_expression, switch, do_while, for_of, for_in, array_pattern
├── transpile_js_mir.cpp       # Add: handlers for new nodes; closure capture; obj mutation; global function dispatch; process.* dispatch
├── js_runtime.cpp             # Add: .charCodeAt, String.fromCharCode, .toFixed, .fill, console.log multi-arg; extend js_call_function arity
├── js_runtime.h               # Declarations for new runtime functions
├── js_transpiler.hpp          # Declarations for new extern "C" functions

lambda/
├── mir.c                      # Register new js_* functions in func_list[] / import_resolver

test/
├── test_js_gtest.cpp          # Re-enable passing disabled tests; add benchmark smoke tests
├── js/benchmark_smoke.js      # Smoke test: exercises typed arrays, process.*, parseInt, closures, for...of
├── js/benchmark_smoke.txt     # Expected output
```

---

## 6. Implementation Phases

### Phase 5a: Re-enable Disabled Tests + Fix Immediate Gaps (3 days)

1. Run each disabled test with current v4 transpiler; re-enable those that pass
2. Fix object property mutation (`obj.prop = val`) in transpiler — unblocks advanced tests
3. Fix `console.log` multi-argument handling
4. Extend `js_call_function` max arity from 4 to 8
5. Fix prefix/postfix `++`/`--` distinction (if any test depends on it)

**Success criteria**: At least 3 of the 7 disabled tests re-enabled and passing.

### Phase 5b: I/O, Timing, and Global Functions (2 days)

1. Implement `process.stdout.write`, `process.hrtime.bigint()`, `process.argv`
2. Implement `parseInt`, `parseFloat`, `isNaN`, `isFinite`, `Number()`, `String()`
3. Wire `process.*` recognition in transpiler (similar to `document.*`)
4. Wire global function recognition in transpiler
5. Register all new functions in `mir.c`

**Success criteria**: `collatz.js`, `diviter.js`, `divrec.js`, `pnpoly.js`, `ray.js` produce correct timed output. These 5 benchmarks need no typed arrays — they run with basic arithmetic + the timing harness.

### Phase 5c: `new` Expressions + Typed Arrays (3 days)

1. Add `JS_AST_NODE_NEW_EXPRESSION` to AST + builder + transpiler
2. Implement `JsTypedArray` data structure and runtime functions (new/get/set/length/fill)
3. Wire typed array constructor dispatch in `new` expression handler
4. Extend `js_property_access` and `js_property_set` for typed array indexed access
5. Add `.fill()` to array method dispatcher (regular + typed)
6. Implement `new Array(n)` as pre-sized regular array

**Success criteria**: All 11 larceny + 7 kostya benchmarks compile and produce correct output.

### Phase 5d: Control Flow Extensions (2 days)

1. Add `switch`/`case`/`default` to AST + builder + transpiler
2. Add `do...while` to AST + builder + transpiler
3. Add `for...of` to AST + builder + transpiler (desugar to indexed loop)
4. Add `for...in` to AST + builder + transpiler (desugar to `Object.keys` + indexed loop)

**Success criteria**: `for...of` on arrays and typed arrays works for nbody.js, fasta.js, revcomp.js.

### Phase 5e: Closures (3 days) — ✅ DONE

1. ✅ Implement capture analysis in function collection pre-pass — `jm_analyze_captures` in transpile_js_mir.cpp
2. ✅ Implement environment allocation and variable capture — `jm_transpile_func_expr` allocates env, stores captured vars
3. ✅ Modify inner function codegen to load from environment — `_js_env` param, `jm_define_function` loads from env
4. ✅ Implement `js_new_closure` and update `js_call_function` for closure dispatch — `JsFunction` struct with env/env_size
5. ✅ Handle mutable captures via environment cells — env slots updated on write
6. ✅ Fix direct-call optimization to skip closures (capture_count > 0) — both call-expression and object method paths

**Implementation notes**: Closures work by lifting captured variables into a heap-allocated `Item*` environment. `js_new_closure(func_ptr, param_count, env, env_size)` creates a `JsFunction` with the env attached. `js_invoke_fn` prepends env items as the first arguments when calling. The direct-call optimization path now checks `fc->capture_count == 0` before bypassing the closure dispatch.

**Success criteria**: ✅ `advanced_features.js` and `es6_features.js` disabled tests pass. Closures returning functions work correctly.

### Phase 5f: Destructuring + Remaining Methods (2 days) — ✅ DONE

1. ✅ Implement array destructuring in variable declarations — `JsArrayPatternNode` AST, `JS_AST_NODE_ARRAY_PATTERN` case in `jm_transpile_var_decl`
2. ✅ Implement destructuring in `for...of` left-hand side — rewritten `jm_transpile_for_of` handles `array_pattern` directly and inside `variable_declaration`
3. ✅ Add `.charCodeAt()`, `String.fromCharCode()`, `.toFixed()` — already implemented in prior sessions
4. ✅ Add `.sort()` with custom comparator callback — insertion sort via `js_invoke_fn`; default sort uses JS-spec lexicographic comparison (`js_to_string` + `strncmp`)
5. ✅ Add variadic `Math.min`/`Math.max` — loop over all args with `fn_min2`/`fn_max2`; handles 0 args (Infinity/-Infinity)
6. ✅ Add `.fill()` method dispatch — wired `fill` case in `js_array_method` to existing `js_array_fill`
7. ✅ Add `js_array_slice_from` runtime helper for rest/spread destructuring patterns

**Implementation notes**: Array destructuring supports identifier elements (via `js_property_access`), rest elements (`...rest` via `js_array_slice_from`), and default values (`= expr` via `ITEM_NULL_VAL` comparison). For-of destructuring creates a temp `_destr_elem` loop variable and destructures after each iteration. Tree-sitter-javascript puts `array_pattern` directly as the `left` field of `for...of` (not wrapped in `variable_declaration`).

**Success criteria**: ✅ `destructuring.js` test passes (31/31 JS tests). All additional string/number/array methods work.

### Phase 5g: Benchmark Harness + Testing (2 days)

1. Create benchmark runner script: runs each `.js` benchmark, captures output + timing
2. Add smoke test covering all new features
3. Build + fix any remaining issues
4. Document which benchmarks pass/fail and why (for remaining gaps like regex, BigInt, fs)
5. Create `make test-js-benchmarks` target

**Success criteria**: 25+ of 29 benchmarks produce correct output. The 4 excluded (regexredux, pidigits, knucleotide fs-dependent, revcomp fs-dependent) are documented with specific reasons.

---

## 7. Benchmark Target Matrix

Expected status after v5 completion:

| Benchmark | Status | Notes |
|-----------|--------|-------|
| **larceny/diviter.js** | ✅ PASS | Basic arithmetic |
| **larceny/divrec.js** | ✅ PASS | Recursion |
| **larceny/pnpoly.js** | ✅ PASS | Float arithmetic |
| **larceny/gcbench.js** | ✅ PASS | Object creation, recursion |
| **larceny/ray.js** | ✅ PASS | Math.sqrt, float |
| **larceny/array1.js** | ✅ PASS | Int32Array (Phase 5c) |
| **larceny/paraffins.js** | ✅ PASS | Int32Array, Math.trunc (Phase 5c) |
| **larceny/primes.js** | ✅ PASS | Uint8Array, .fill (Phase 5c) |
| **larceny/puzzle.js** | ✅ PASS | new Array, .fill (Phase 5c) |
| **larceny/quicksort.js** | ✅ PASS | Int32Array (Phase 5c) |
| **larceny/triangl.js** | ✅ PASS | Uint8Array, Int32Array (Phase 5c) |
| **kostya/collatz.js** | ✅ PASS | Basic arithmetic |
| **kostya/json_gen.js** | ✅ PASS | .push, .join, Math.trunc |
| **kostya/base64.js** | ✅ PASS | Uint8Array, bitwise (Phase 5c) |
| **kostya/matmul.js** | ✅ PASS | Float64Array (Phase 5c) |
| **kostya/primes.js** | ✅ PASS | Uint8Array, .fill (Phase 5c) |
| **kostya/brainfuck.js** | ✅ PASS | Typed arrays, .charCodeAt, String.fromCharCode (Phase 5c+5f) |
| **kostya/levenshtein.js** | ✅ PASS | Int32Array, .charCodeAt, array destructuring (Phase 5c+5f) |
| **beng/mandelbrot.js** | ✅ PASS | parseInt, process.argv, bitwise (Phase 5b) |
| **beng/binarytrees.js** | ✅ PASS | parseInt, template literals, recursion (Phase 5b) |
| **beng/fannkuch.js** | ✅ PASS | Int32Array, parseInt (Phase 5b+5c) |
| **beng/spectralnorm.js** | ✅ PASS | Float64Array, .fill, .toFixed (Phase 5c+5f) |
| **beng/nbody.js** | ✅ PASS | obj.prop mutation, for...of, .toFixed (Phase 5a+5d+5f) |
| **beng/fasta.js** | ✅ PASS | for...of, array destructuring (Phase 5d+5f) |
| **beng/revcomp.js** | ⚠️ NEEDS STDIN | Uses `require('fs')` for input; provide `--input` CLI flag |
| **beng/knucleotide.js** | ⚠️ NEEDS MAP+STDIN | `new Map`, `require('fs')`, spread, .sort(comparator) |
| **beng/regexredux.js** | ❌ SKIP | Requires RegExp — out of scope for v5 |
| **beng/pidigits.js** | ❌ SKIP | Requires BigInt arithmetic — out of scope for v5 |

**Expected final count: 24–25 PASS, 2 NEEDS WORKAROUND, 2 SKIP**

For the 2 "NEEDS STDIN" benchmarks, a potential workaround: add a `--input file.txt` CLI flag that reads the input file and makes it available as a global string variable, bypassing `require('fs')`.

---

## 8. Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Closure environment lifetime doesn't interact well with GC | Medium | Closures hold stale references | Use `heap_calloc` for environments; GC traces them via conservative stack scan |
| Typed array performance overhead from Map wrapper dispatch | Low | Benchmarks run slowly due to type checks | The `js_is_typed_array` check is a single pointer comparison; negligible |
| `for...of` desugaring misses edge cases (sparse arrays, iterators) | Low | Incorrect iteration on non-benchmark code | Only handles Array and typed array — sufficient for all benchmarks |
| BigInt timing harness emits incorrect values as floats | Low | Timing numbers slightly off | `double` has ~53 bits precision; nanosecond timing for <104 days fits exactly |
| `switch` fall-through semantics are wrong | Medium | Incorrect switch behavior | Carefully implement: each case body falls through by default; `break` jumps to end label |
| `process.argv` index mismatch (Node.js has node/script in argv[0:1]) | Low | Wrong argument parsed | Match Node.js convention: argv[0]="lambda.exe", argv[1]=script_path, argv[2+]=user args |
| Inner function MIR ordering with closures | Medium | Closure function defined before environment is available | Post-order function collection already handles this; environment is created at the call site, not at function definition |

---

## 9. Summary

| Deliverable | Impact | Effort |
|-------------|--------|--------|
| Re-enable disabled tests | +3–5 passing tests | 3 days |
| I/O + timing harness + global functions | Unblocks 18 larceny+kostya benchmarks | 2 days |
| `new` expressions + typed arrays | Unblocks 12 benchmark typed array usage | 3 days |
| Control flow (`switch`, `do-while`, `for-of`) | 5 beng benchmarks, language completeness | 2 days |
| Closures | Correctness for real-world JS, 2 disabled tests | 3 days |
| Destructuring + string/number/array methods | 4 benchmarks, language breadth | 2 days |
| Benchmark harness + integration testing | Validates everything | 2 days |
| **Total** | **24–25 of 29 benchmarks running** | **~17 days** |

The v5 work transforms Lambda's JS transpiler from a "demo subset" into a **benchmark-capable execution engine** covering ES5+ core with select ES6 features — sufficient for algorithmic programming, computational benchmarks, and DOM manipulation.

---

## 10. Implementation Progress

### 10.1 Phase 5a–5b Status (Completed)

All v5 foundational features have been implemented according to the plan:

| Feature | Status | Files |
|---------|--------|-------|
| AST extensions (new_expression, switch, do_while, for_of, for_in) | ✅ Done | `js_ast.hpp`, `build_js_ast.cpp` |
| Typed arrays (Int32Array, Uint8Array, Float64Array) | ✅ Done | `js_typed_array.h`, `js_typed_array.cpp` |
| Process I/O (stdout.write, hrtime.bigint, argv) | ✅ Done | `js_globals.cpp` |
| Global functions (parseInt, parseFloat, isNaN, Number, String) | ✅ Done | `js_globals.cpp` |
| MIR transpiler handlers for new AST nodes | ✅ Done | `transpile_js_mir.cpp` |
| MIR function registration (~25 new functions) | ✅ Done | `mir.c` |
| JsAstNode layout fix (field order matching AstNode) | ✅ Done | `js_ast.hpp` |
| Object creation via Lambda Map + map_put | ✅ Done | `js_runtime.cpp` |
| JSON output formatting for JS results | ✅ Done | `main.cpp`, `format-json.cpp` |
| Re-enabled tests: basic_expressions, control_flow | ✅ Done | `test_js_gtest.cpp` |

### 10.2 Phase 5c–5d: Bug Fixes & Feature Hardening (Completed)

A comprehensive bug-fix pass was performed after the initial 24-test milestone. A feature probe discovered 6 broken features. Investigation revealed 8 distinct bugs across the AST builder, transpiler, and runtime — all fixed and covered by 6 new test files.

#### Bugs Fixed

| # | Feature | Root Cause | Fix | Files Modified |
|---|---------|-----------|-----|----------------|
| 1 | **for...of loop body never executes** | AST builder used tree-sitter node type name `"for_in_statement"` (same for both for-in and for-of) to distinguish them — always produced `FOR_IN_STATEMENT` | Check tree-sitter `"operator"` field (`"in"` vs `"of"`) instead of node type name | `build_js_ast.cpp` |
| 2 | **for...of length comparison fails** | `fn_len` returns boxed `Item`; MIR loop compared raw `int64_t` index against boxed value | Changed to `js_array_length` which returns raw `int64_t` | `transpile_js_mir.cpp` |
| 3 | **Bitwise operators crash (SIGSEGV)** | Comments in object literals: `ts_node_named_child_count` includes comment nodes; accessing `"key"`/`"value"` fields on comment nodes crashes | Skip `"comment"` nodes in `build_js_object_expression` | `build_js_ast.cpp` |
| 4 | **Typed array indexed access returns null** | `js_property_get`/`js_property_set` didn't check for typed arrays (Maps with sentinel marker) before general Map dispatch | Added `js_is_typed_array()` check before DOM/computed-style checks | `js_runtime.cpp` |
| 5 | **Typed array `.length` returns 0** | `.length` optimization in transpiler called `fn_len()` which returns 0 for `LMD_TYPE_MAP`; typed arrays are Maps | Created `js_get_length()` that checks typed arrays first, then delegates to `fn_len` | `transpile_js_mir.cpp`, `js_runtime.cpp`, `js_runtime.h`, `mir.c` |
| 6 | **`Object.keys()` returns null** | No transpiler recognition of `Object.keys(obj)` as a static method call | Added pattern detection after `Math.<method>` handler; emits `js_object_keys` call | `transpile_js_mir.cpp` |
| 7 | **`toFixed()` returns null** | No number type branch in method dispatch; numbers fell through to generic property access | Created `js_number_method` dispatcher (handles `toFixed`, `toString`); added `INT`/`FLOAT` branch in type dispatch | `transpile_js_mir.cpp`, `js_globals.cpp`, `js_runtime.h`, `mir.c` |
| 8 | **Spread element (`...arr`) doesn't expand** | Two bugs: (a) `build_js_expression` had no handling for `"spread_element"` tree-sitter node type; (b) `jm_transpile_array` didn't check for `JS_AST_NODE_SPREAD_ELEMENT` | Added `spread_element` AST builder; added spread-aware array transpilation with MIR loop using `js_array_push` | `build_js_ast.cpp`, `transpile_js_mir.cpp` |
| 9 | **Unsigned right shift (`>>>`) UB on negatives** | `(uint32_t)(-1.0)` is undefined behavior in C/C++ | Cast through `int32_t` first: `(uint32_t)(int32_t)value` | `js_runtime.cpp` |

#### New Test Files (6)

| Test File | Coverage |
|-----------|----------|
| `for_of_loop.js` | for-of over arrays (numbers, strings, single element, empty array) |
| `bitwise_ops.js` | AND, OR, XOR, NOT, left/right shift, unsigned right shift, comments in objects |
| `typed_arrays.js` | Uint8Array, Int32Array, Float64Array creation, indexed get/set, `.length` property |
| `number_methods.js` | `toFixed` on float/int with different precisions, `toString` |
| `object_static.js` | `Object.keys` on objects, empty objects, for-of iteration over keys |
| `spread_element.js` | Spread at beginning/middle/end of array, multiple spreads, empty spread |

#### Key Design Decisions

**`js_get_length` vs removing `.length` optimization**: The `.length` member expression optimization (compile-time shortcut to avoid `js_property_access`) was preserved for performance but changed from `fn_len()` to `js_get_length()`. The new function checks `js_is_typed_array()` first (returning `js_typed_array_length`), then falls back to `fn_len` for all standard Lambda types (Array, List, String, Element, etc.). This keeps the fast path while correctly handling typed arrays.

**Spread element dual-path array construction**: Arrays with spread elements use `js_array_new(0)` + `js_array_push` (dynamic growth), while arrays without spread use the original `js_array_new(n)` + `js_array_set` (pre-allocated). This avoids penalizing the common case.

**for-of/for-in AST discrimination**: Tree-sitter uses a single `"for_in_statement"` grammar rule for both `for...in` and `for...of`. The correct way to distinguish them is to read the `"operator"` named field from the tree-sitter node (`ts_node_child_by_field_name(node, "operator")`), which yields `"in"` or `"of"`.

### 10.3 Phase 5e–5f: Closures + Destructuring + Methods (Completed)

#### Phase 5e: Closure Direct-Call Fix

Closures were mostly implemented during prior work (capture analysis, environment allocation, env passing via `js_invoke_fn`). The remaining gap was the **direct-call optimization path** — when the transpiler knows a function at compile time, it bypasses `js_call_function` and calls the MIR function directly. This optimization didn't pass the closure environment, causing closures called directly to lose their captured variables.

**Fix**: Added `fc->capture_count == 0` guard before the direct-call optimization in two places:
1. Regular call expressions (~line 1840 in `transpile_js_mir.cpp`)
2. Object method calls (~line 2685 in `transpile_js_mir.cpp`)

Closures with captures now always go through the `js_invoke_fn` dispatch path, which correctly prepends env items as arguments.

#### Phase 5f: Destructuring Implementation

**AST Builder** (`build_js_ast.cpp`):
- Added `array_pattern` handler in `build_js_expression` — creates `JsArrayPatternNode` with elements linked list
- Elements can be: identifiers, `JsSpreadElementNode` (rest `...x`), or `JsAssignmentPatternNode` (defaults `x = val`)
- Variable declarator detection: identifies `array_pattern`/`object_pattern` id nodes and routes to `build_js_expression` instead of `build_js_identifier`
- Scope registration: only registers simple `JS_AST_NODE_IDENTIFIER` ids, skips pattern nodes

**Transpiler** (`transpile_js_mir.cpp`):
- `jm_transpile_var_decl` — added `JS_AST_NODE_ARRAY_PATTERN` case:
  - Evaluates init expression into a source register
  - Iterates pattern elements: `IDENTIFIER` → `js_property_access(src, index)`, `SPREAD_ELEMENT` → `js_array_slice_from(src, index)`, `ASSIGNMENT_PATTERN` → access + compare against `ITEM_NULL_VAL` + conditional default
- `jm_transpile_for_of` — completely rewritten to handle destructuring:
  - Detects `array_pattern` both directly as `left` and inside `variable_declaration`
  - Creates temp `_destr_elem` loop variable, pre-creates all pattern variable registers
  - After fetching each element, destructures it via `js_property_access` / `js_array_slice_from`

**Runtime** (`js_runtime.cpp`):
- `js_array_slice_from(arr, start_item)` — new helper for rest destructuring, creates new array from index to end
- Sort rewrite: with comparator → insertion sort via `js_invoke_fn`; without comparator → JS-spec lexicographic sort using `js_to_string` + `strncmp`
- Variadic `Math.min`/`Math.max` — loop over all args with `fn_min2`/`fn_max2`; 0 args returns `Infinity`/`-Infinity`
- `.fill()` method dispatch — wired `fill` case in `js_array_method` to existing `js_array_fill`

#### New Test Files (Phase 5e–5f)

| Test File | Coverage |
|-----------|----------|
| `destructuring.js` | Basic array destructuring, rest element, partial (out-of-bounds → null), for-of destructuring, for-of with rest, sort with comparator (asc/desc), default sort (lexicographic), variadic Math.min/max, Array.fill |
| `closures.js` | Basic closure capture, multiple captures, mutable accumulator, closure as callback, arrow function closures, function composition, closure over loop variable, closures in map/filter, running sum, immediate invocation |
| `sort_destr_methods.js` | Arrow comparator sort, closure-created comparator, lexicographic default sort, string sort, Math.min/max 0–4 args, Array.fill, rest destructuring with computation, for-of dot product, multiple sequential destructurings, destructuring from function return, for-of with rest, sort stability |

### 10.4 Test Results

**JS GTest suite: 33/33 pass** (up from 24 → 30 → 33)

All 33 tests (32 file-based + 1 command interface) pass:

| Test | Status | Category |
|------|--------|----------|
| simple_test | ✅ | Core |
| arithmetic | ✅ | Core |
| console_log | ✅ | Core |
| variables | ✅ | Core |
| control_flow_basic | ✅ | Core |
| functions_basic | ✅ | Core |
| basic_expressions | ✅ | Core (re-enabled) |
| functions | ✅ | Core (re-enabled) |
| control_flow | ✅ | Core (re-enabled) |
| advanced_features | ✅ | Core (re-enabled) |
| es6_features | ✅ | Core (re-enabled) |
| error_handling | ✅ | Core (re-enabled) |
| array_methods | ✅ | Core (re-enabled) |
| string_methods | ✅ | Methods |
| math_object | ✅ | Methods |
| array_methods_v3 | ✅ | Methods |
| dom_basic | ✅ | DOM (re-enabled) |
| switch_statement | ✅ | Control flow |
| do_while | ✅ | Control flow |
| for_in_loop | ✅ | Control flow |
| operators_extra | ✅ | Operators |
| global_functions | ✅ | Global functions |
| template_literals | ✅ | ES6 |
| for_of_loop | ✅ | Phase 5c–5d — for-of fix |
| bitwise_ops | ✅ | Phase 5c–5d — comment crash + bitwise fix |
| typed_arrays | ✅ | Phase 5c–5d — typed array access + length fix |
| number_methods | ✅ | Phase 5c–5d — toFixed dispatch fix |
| object_static | ✅ | Phase 5c–5d — Object.keys fix |
| spread_element | ✅ | Phase 5c–5d — spread element fix |
| destructuring | ✅ | **Phase 5f** — array destructuring, sort, Math variadic, fill |
| closures | ✅ | **Phase 5e** — closure capture, composition, loop closures |
| sort_destr_methods | ✅ | **Phase 5f** — sort comparators, rest patterns, for-of destructuring |

**Lambda baseline: 609/609 pass** (all suites green)

### 10.5 Remaining Work

| Phase | Deliverable | Status |
|-------|-------------|--------|
| 5a | Re-enable disabled tests + fix immediate gaps | ✅ Done |
| 5b | I/O, timing, global functions | ✅ Done |
| 5c | `new` expressions + typed array wiring in transpiler | ✅ Done (via 5c–5d bug fix pass) |
| 5d | Control flow: switch, do-while, for-of, for-in | ✅ Done |
| 5e | Closures (capture analysis, environment passing) | ✅ Done |
| 5f | Destructuring + .charCodeAt, .toFixed, .sort, Math variadic, .fill | ✅ Done |
| 5g | Benchmark harness + integration testing | Not started |

---

## 11. Design Decisions

### 11.1 JS Object/Map Representation: Lambda Map + `map_put()`

**Decision**: JS objects use native Lambda `Map` with the `map_put()` API for dynamic property insertion.

**Context**: JS objects need dynamic key insertion (`obj.newKey = value`), which Lambda's normal compile-time shape system doesn't support. Three approaches were evaluated:

#### Approaches Considered

| Approach | Pros | Cons |
|----------|------|------|
| **HashMap wrapper** | Simple, O(1) insert | Incompatible with Lambda's type system; `Map.type=NULL` sentinel crashed `print_item` |
| **VMap** | Existing Lambda type, works with print | Separate type from Map; doesn't unify runtime representations; user rejected |
| **Lambda Map + `map_put()`** | Unified runtime type; reuses shape infrastructure; works with all Lambda operations (print, format_json, map_get, fn_map_set) | Requires `Input*` context for `map_put`; shapes grow linearly per key addition |

#### Chosen: Lambda Map + `map_put()`

The design principle is: **JS objects should be indistinguishable from Lambda maps at runtime.** This means:
- Lambda's `print_root_item`, `format_json`, `map_get`, `fn_map_set` all work on JS objects without special-casing
- No new types or type markers needed
- GC manages JS objects identically to Lambda maps

#### How It Works

**Object creation** (`js_new_object`):
```c
Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
m->type_id = LMD_TYPE_MAP;
m->type = &EmptyMap;  // global singleton for empty maps
return (Item){.map = m};
```

**Property set** (`js_property_set`):
1. If the key already exists in the shape chain → `fn_map_set()` for in-place update
2. If the key is new → `map_put(m, str_key, value, js_input)` to append a new `ShapeEntry`

**Property get** (`js_property_get`):
- Calls `map_get(m, key)` — standard Lambda map lookup via shape chain

**The `Input*` requirement**: `map_put()` needs an `Input*` for arena allocation of new `ShapeEntry` nodes and data buffer resizing. A global `js_input` is created from `context->pool` at JS execution startup:

```c
// In transpile_js_to_mir(), before JIT execution:
Input* js_input = Input::create(context->pool);
js_runtime_set_input(js_input);
```

The `js_runtime_set_input(void*)` function takes `void*` for C linkage compatibility (since `js_runtime.h` is included from both C and C++ translation units).

#### Shape Growth Model

Each `map_put()` call on an empty map:
1. Allocates a new `TypeMap` (replacing `EmptyMap`)
2. Creates a `ShapeEntry` for the key with name, type, and byte offset
3. Allocates a data buffer for the value

Subsequent `map_put()` calls append to the `ShapeEntry` chain and resize the data buffer. This is O(n) per insertion for n existing keys, but JS objects in benchmarks are small (typically <10 keys), so this is acceptable.

#### JSON Output Formatting

JS command output uses `format_json()` (from `lambda/format/format-json.cpp`) for map/array/element results, producing standard JSON with quoted keys:

```json
{"sum": 10, "total": 45, "max": 10}
```

Scalar results (numbers, strings, booleans) continue to use `print_root_item()`. This is controlled in `main.cpp`'s JS command handler based on `get_type_id(result)`.

The JSON formatter was also updated globally to include a space after colons (`"key": value` instead of `"key":value`) for consistency with standard JSON pretty-printing conventions.
