# Lambda Assignment — Structural Mutation Design

## 1. Overview

Lambda started as a pure functional engine. Procedural functions (`pn`) introduced mutation via `var` declarations and assignment statements (`assign_stam`). The current mutation support was built ad hoc for specific benchmarks (notably AWFY), resulting in structural gaps, silent data corruption, and inconsistent behavior across data types.

This proposal analyzes the current implementation, catalogs every gap and bug, and proposes a systematic fix. The scope covers `var` reassignment, array element assignment, map field assignment, and element mutation. Insertion and deletion in data collections are explicitly out of scope for this phase.

### Design Principles

1. **`let` bindings are immutable** — can only be initialized, never reassigned.
2. **Function parameters are immutable** — cannot be reassigned within the function body.
3. **`var` variables are mutable** — can be reassigned, and the runtime type may change.
4. **Assignment must never silently corrupt data** — type mismatches must be handled explicitly: either by conversion, by upgrading the container, or by raising a runtime error.
5. **Structural consistency** — the shape/type metadata of a container must always accurately reflect its actual stored data.

---

## 2. Current Architecture

### 2.1 End-to-End Assignment Flow

```
Source Code        Grammar             AST Builder           Transpiler (C)            MIR JIT           Runtime
─────────────────────────────────────────────────────────────────────────────────────────────────────────────────
var x = 5       → var_stam          → AST_NODE_VAR_STAM   → "int64_t _x = 5;"      → native i64     → C stack var

x = 10          → assign_stam       → AST_NODE_ASSIGN_    → "_x = 10;"             → native write   → C stack var
                   (identifier)        STAM                  (while: _store_i64)

arr[i] = val    → assign_stam       → AST_NODE_INDEX_     → "fn_array_set(          → call extern   → fn_array_set()
                   (index_expr)        ASSIGN_STAM            (Array*)arr,i,val);"

obj.f = val     → assign_stam       → AST_NODE_MEMBER_    → "fn_map_set(obj,         → call extern  → fn_map_set()
                   (member_expr)       ASSIGN_STAM            const_s2it(k),val);"
```

### 2.2 Variable Storage Model

Variables are **native C locals** on the stack. The transpiler emits typed declarations:

| Lambda declaration | C emission | Can hold any type? |
|-|-|-|
| `var x = 42` | `int64_t _x = 42;` | No — fixed as i64 |
| `var x = 3.14` | `double _x = 3.14;` | No — fixed as f64 |
| `var x = true` | `bool _x = true;` | No — fixed as bool |
| `var x = "hi"` | `String* _x = ...;` | No — pointer only |
| `var x = null` | `Item _x = ITEM_NULL;` | **Yes** — tagged union |
| `var x: Any = 42` | `Item _x = i2it(42);` | **Yes** — tagged union |

The `Item` type (64-bit tagged value) is the only C type that supports runtime type changes. Scalar-typed variables (`int64_t`, `double`, `bool`) are locked to their declared type.

### 2.3 Runtime Functions

| Function | File | Purpose |
|-|-|-|
| `fn_array_set(Array*, int, Item)` | lambda-eval.cpp:3122 | Array element assignment |
| `fn_map_set(Item, Item, Item)` | lambda-eval.cpp:3167 | Map field assignment |
| `array_set(Array*, int, Item)` | lambda-data.cpp:375 | Internal generic array store |
| `_store_i64(int64_t*, int64_t)` | lambda-data.cpp:251 | MIR SSA barrier for int store in while loops |
| `_store_f64(double*, double)` | lambda-data.cpp:252 | MIR SSA barrier for float store in while loops |

### 2.4 MarkEditor (Existing Structural Mutation Engine)

`MarkEditor` (mark_editor.hpp/cpp) is a full-featured mutation engine used by input parsers, with two modes:

- **EDIT_MODE_INLINE** — in-place mutation (mutable)
- **EDIT_MODE_IMMUTABLE** — copy-on-write with version history

It supports shape rebuilding on type change, new field addition, field deletion, element attribute/child mutation — everything `fn_map_set`/`fn_array_set` lack. However, it is **not wired to the assignment syntax in procedural code**.

---

## 3. Gap & Bug Analysis

### 3.1 Variable Reassignment — Type Change Not Handled

**Location:** transpile.cpp:3091–3158 (`transpile_assign_stam`)

**Problem:** When a `var` is declared with a concrete type (e.g., `var x = 42` → `int64_t _x`), the C variable type is fixed. If the variable is later assigned a value of a different type, the transpiler does not account for this:

```lambda
pn main() {
    var x = 42        // emits: int64_t _x = 42;
    x = "hello"       // emits: _x = s2it("hello");  ← C type mismatch!
}
```

The transpiler uses `target_node->type` (the **declaration-time type**) for all subsequent assignments. Only `LMD_TYPE_NULL` and `LMD_TYPE_ANY` targets get boxing/unboxing wrappers.

**Severity:** Critical — potential C compilation errors or undefined behavior.

**Mitigation that currently works:** If you write `var x = null`, the variable is emitted as `Item`, which can hold any type. But this is accidental, not designed.

### 3.2 `let` Reassignment Not Checked at AST Level

**Location:** build_ast.cpp:4645–4720 (`build_assign_stam`)

**Problem:** The `build_assign_stam` function looks up the target variable via `lookup_name()` but does **not** check whether the variable was declared with `var` vs `let`. The only protection is syntactic: `assign_stam` only appears inside `proc_content` (grammar.js:309). However, a `let` variable declared in an outer scope or within the same `pn` body can still be targeted by `assign_stam`:

```lambda
let y = 10
pn main() {
    y = 20         // grammar allows; AST builder does not reject
}
```

Additionally, `NameEntry` has no `is_mutable` flag, making it impossible for the AST builder to distinguish `var` from `let` declarations after the fact.

**Severity:** Medium — could allow mutation of immutable bindings.

### 3.3 Specialized Array Assignment — Silent Data Corruption

**Location:** lambda-eval.cpp:3139–3157 (`fn_array_set`)

**Problem:** When assigning to specialized arrays (`ArrayInt`, `ArrayInt64`, `ArrayFloat`), there is **zero type validation**. The value is unconditionally extracted using the array's expected accessor:

```cpp
case LMD_TYPE_ARRAY_INT: {
    ArrayInt* ai = (ArrayInt*)arr;
    ai->items[index] = (int64_t)value.get_int56();  // no type check!
    break;
}
```

Scenarios that silently corrupt data:

| Array type | Value assigned | What happens |
|-|-|-|
| `ArrayInt` | Float `3.14` | `get_int56()` extracts tagged pointer bits as integer — **garbage** |
| `ArrayInt` | String `"hi"` | `get_int56()` extracts string pointer bits — **garbage** |
| `ArrayFloat` | Int `42` | `get_double()` interprets tagged int bits as double — **garbage** |
| `ArrayInt64` | String `"hi"` | `get_int64()` dereferences heap, extracts wrong data — **garbage** |

There is **no conversion** from specialized to generic array when an incompatible type is stored.

**Severity:** Critical — silent data corruption with no error message.

### 3.4 `fn_map_set` Cannot Change Field Types or Add Fields

**Location:** lambda-eval.cpp:3167–3337 (`fn_map_set`)

**Problem:** `fn_map_set` operates within the existing shape and **never rebuilds it**:

1. **Type mismatch** (beyond the allowed INT↔FLOAT/INT↔INT64 coercions and NULL↔container transitions) → logs error and silently returns without storing. The user gets no script-level error.

2. **New field** (not in shape) → logs `"field '%s' not found in map"` and silently returns. No expansion.

3. **NULL↔container transition** stores a container pointer in a field whose shape still says `LMD_TYPE_NULL`. Subsequent reads through the shape will misinterpret the stored data.

This means maps are effectively frozen to their initial shape after creation, unless the allowed coercions happen to apply.

**Severity:** High — severely limits map mutation usefulness; shape metadata inconsistency is a correctness bug.

### 3.5 Element Child Assignment Not Supported

**Location:** lambda-eval.cpp:3122 (`fn_array_set`)

**Problem:** Elements have dual nature — map-like attributes and list-like children. `fn_array_set` only handles `LMD_TYPE_ARRAY`, `LMD_TYPE_ARRAY_INT`, `LMD_TYPE_ARRAY_INT64`, `LMD_TYPE_ARRAY_FLOAT`. It does **not** handle `LMD_TYPE_ELEMENT`.

Attempting `elem[i] = val` in procedural code will:
1. The transpiler casts to `(Array*)` and calls `fn_array_set`
2. At runtime, `arr->type_id == LMD_TYPE_ELEMENT` falls to the `default` case
3. Logs `"fn_array_set: unsupported array type"` and silently returns

**Severity:** High — element child mutation via assignment syntax is completely broken.

### 3.6 Element Attribute Type-Change Not Supported

**Location:** lambda-eval.cpp:3167 (`fn_map_set`) handles elements by treating them as maps, but inherits all the limitations from §3.4 — no shape rebuild, no type changes beyond coercions.

MarkEditor has full support for element attribute type changes via `elmt_update_attr` with shape rebuild, but this is not accessible from assignment syntax.

**Severity:** Medium — element attribute mutation is partial.

### 3.7 Capture Mutability Not Tracked

**Location:** build_ast.cpp:451

```cpp
capture->is_mutable = false; // TODO: detect mutations
```

When a `var` is captured by a closure, the `CaptureInfo::is_mutable` flag is never set to `true`. This means:
- Mutations to captured variables may not work correctly in closures
- The transpiler cannot make correct decisions about capture-by-value vs capture-by-reference

**Severity:** Medium — affects closures that capture mutable variables.

### 3.8 No Parameter Reassignment Guard

**Location:** build_ast.cpp:4645–4720 (`build_assign_stam`)

**Problem:** Function parameters are typed identifiers in scope. `build_assign_stam` does a `lookup_name()` which can find a function parameter. There is no check to prevent `param = new_value`.

**Severity:** Medium — violates the design principle that function parameters are immutable.

---

## 4. Proposed Design

### 4.1 Variable Reassignment with Type Change

> **Status: IMPLEMENTED** — merged into `ast.hpp`, `build_ast.cpp`, `transpile.cpp`

#### 4.1.1 Strategy: Smart Type Analysis (Concrete When Possible, Item When Needed)

Rather than always emitting `var` as `Item` (which has performance overhead), the transpiler performs **static type analysis** during AST building. Each `var` is analyzed to determine whether it can use a fast concrete C type or must be widened to `Item`.

**Rules:**

1. **Non-annotated `var`, all assignments type-consistent:** use the concrete C type (zero overhead).
2. **Non-annotated `var`, assignments have inconsistent types:** widen to `Item` (the `type_widened` flag on `NameEntry`).
3. **Annotated `var` (`var x: int = ...`):** use the declared type. All assignments must follow that type. Static type errors for incompatible types. Numeric coercion allowed (int↔float↔int64↔decimal).
4. **Runtime-typed value (ANY/NULL) assigned to annotated var:** allowed — uses runtime cast (unboxing) at the C level.

```lambda
var x = 42          // → int64_t _x = 42;       (concrete, fast)
x = x + 1           // → _x = _x + 1;           (still int, no widening)

var y = 42           // → Item _y = i2it(42);     (widened because assignment below changes type)
y = "hello"          // → _y = s2it(...);         (Item assignment, type change OK)

var z: int = 42      // → int64_t _z = 42;       (annotated as int, stays int)
z = z / 2            // → _z = (int64_t)(z / 2);  (float→int coercion allowed)
// z = "hello"       // ERROR: cannot assign string value to var 'z' of type int
```

#### 4.1.2 Implementation: NameEntry Type Tracking

Three new fields on `NameEntry` (in `ast.hpp`):

```cpp
typedef struct NameEntry {
    String* name;
    AstNode* node;
    struct NameEntry* next;
    AstImportNode* import;
    struct NameScope* scope;
    bool is_mutable;           // true for var, false for let/param
    bool has_type_annotation;  // true if explicit type annotation (var x: int = ...)
    bool type_widened;         // true if type was widened to Item due to inconsistent assignments
} NameEntry;
```

Back-pointers for transpiler access:
- `AstNamedNode::entry` → the `NameEntry*` for var/let declarations
- `AstAssignStamNode::target_entry` → the `NameEntry*` for assignment targets

#### 4.1.3 AST Builder: Type Analysis in `build_assign_stam`

When processing a simple var reassignment (`x = expr`):

1. Look up the `NameEntry` for the target variable.
2. Compare the value's inferred type with the variable's declared/inferred type.
3. If types differ:
   - **Annotated var:** check compatibility with numeric coercion. Report static error for incompatible types (e.g., string → int). Allow ANY/NULL (runtime cast).
   - **Non-annotated var:** set `entry->type_widened = true`. The variable's declaration and all references will use `Item` storage.

```cpp
// In build_assign_stam, simplified logic:
if (var_tid != val_tid) {
    if (entry->has_type_annotation) {
        bool compatible = (val_tid == LMD_TYPE_ANY || val_tid == LMD_TYPE_NULL)
                       || types_compatible(value_type, var_type)
                       || (both_numeric(var_tid, val_tid));  // int↔float↔int64↔decimal
        if (!compatible)
            record_type_error(tp, line, "cannot assign %s to var '%s' of type %s", ...);
    } else if (!entry->type_widened) {
        if (var_tid != LMD_TYPE_NULL && var_tid != LMD_TYPE_ANY)
            entry->type_widened = true;
    }
}
```

#### 4.1.4 Transpiler: Widened Variable Handling

The transpiler checks `type_widened` at three points:

| Transpiler site | Normal var | Widened var |
|-|-|-|
| **Declaration** (`transpile_assign_expr`) | `int64_t _x = 42;` | `Item _x = i2it(42);` |
| **Assignment** (`transpile_assign_stam`) | `_x = new_val;` | `_x = i2it(new_val);` (box to Item) |
| **Reference** (`transpile_primary_expr`) | `_x` (native) | `it2i(_x)` (unbox from Item) |
| **Boxing** (`transpile_box_item`) | `i2it(_x)` (box native) | `_x` (already Item, skip boxing) |

The MIR JIT SSA workaround (`_store_i64`/`_store_f64`) is disabled for widened vars since they are `Item`-typed and don't trigger the MIR optimizer bug.

### 4.2 Immutability Enforcement

> **Status: IMPLEMENTED** — merged into `build_ast.cpp`

#### 4.2.1 `is_mutable` on `NameEntry`

Set `is_mutable = true` only for `var` declarations in `build_var_stam`. All other bindings (`let`, function parameters) default to `false`.

#### 4.2.2 Validate in `build_assign_stam`

```cpp
NameEntry* entry = lookup_name(tp, target_name);
if (entry && !entry->is_mutable) {
    record_semantic_error(tp, assign_node, ERR_IMMUTABLE_ASSIGNMENT,
        "cannot assign to immutable binding '%s' (declared with 'let' or as function parameter)",
        entry->name->chars);
}
```

This provides a clear compile-time error (error code 211), closing BUG-5 and BUG-6.

### 4.3 Specialized Array Assignment

> **Status: IMPLEMENTED** — merged into `lambda-eval.cpp`, `lambda-data-runtime.cpp`

#### 4.3.1 Strategy: In-Place Conversion with Runtime Type Check

Rather than changing the `fn_array_set` signature (which would require transpiler and MIR changes), the solution converts specialized arrays **in place**. All array types share the same struct layout (`Container + items_ptr + length + extra + capacity`), so we can safely change `type_id` and swap the items buffer without changing the struct pointer. The caller's variable still points to the same allocation.

**Type compatibility rules in `fn_array_set`:**

| Array Type | Value Type | Action |
|-|-|-|
| `ArrayInt` | INT | Store directly (fast path) |
| `ArrayInt` | INT64 (fits int56) | Store directly |
| `ArrayInt` | INT64 (overflow) | Convert → generic, then `array_set` |
| `ArrayInt` | FLOAT/STRING/other | Convert → generic, then `array_set` |
| `ArrayInt64` | INT or INT64 | Store directly (widen int → int64) |
| `ArrayInt64` | other | Convert → generic, then `array_set` |
| `ArrayFloat` | FLOAT | Store directly |
| `ArrayFloat` | INT or INT64 | Store as double (widen) |
| `ArrayFloat` | other | Convert → generic, then `array_set` |
| `Array` | any | `array_set` (already generic) |
| `List`/`Element` | any | `array_set` (already generic `Item*`) |

#### 4.3.2 In-Place Conversion: `convert_specialized_to_generic()`

```cpp
static void convert_specialized_to_generic(Array* arr) {
    // 1. Allocate new Item buffer with capacity = len*2 + 4
    //    (extra room for float/int64 values stored at end of buffer)
    // 2. Convert each element based on old type:
    //    ArrayInt:   new_items[i] = i2it(old_items[i])  (packs into Item directly)
    //    ArrayInt64: store int64 in extra area, pointer in main slot
    //    ArrayFloat: store double in extra area, pointer in main slot
    // 3. Free old items buffer (all specialized arrays use malloc)
    // 4. Set arr->type_id = LMD_TYPE_ARRAY
    // 5. arr->items = new_items (same struct pointer, new buffer)
}
```

No signature change needed. No transpiler changes needed. No MIR registration changes needed.

#### 4.3.3 Specialized Getter Fallback

After runtime conversion, the transpiler's statically-emitted `array_int_get()` / `array_float_get()` calls would read the generic `Item*` buffer as a specialized type — producing garbage. Fix: each specialized getter checks `type_id` at the top and falls back to `array_get()`:

```cpp
Item array_int_get(ArrayInt *array, int index) {
    if (array->type_id != LMD_TYPE_ARRAY_INT)
        return array_get((Array*)array, index);  // converted to generic
    // ... existing specialized path
}
```

This adds a single comparison per access — negligible cost — and correctly handles arrays that were converted at runtime.

### 4.4 Map Field Assignment with Type Change

#### 4.4.1 Strategy: Leverage MarkEditor for Shape Rebuild

Rather than duplicating MarkEditor's shape-rebuild logic in `fn_map_set`, the runtime should delegate to MarkEditor when a type change or new-field scenario is detected:

```
fn_map_set(map_item, key, value):
    find field in shape
    if field found AND same type:
        in-place store (current behavior — fast path)
    if field found AND different type:
        delegate to MarkEditor::map_update(map, key, value) in inline mode
    if field NOT found:
        error (no field addition in this phase)
```

This keeps the fast path for same-type assignment (the common case in hot loops) while gaining MarkEditor's shape-rebuild capability for type changes.

#### 4.4.2 Shape Rebuild Flow

When MarkEditor rebuilds a shape for type change:

1. Create `ShapeBuilder`, import existing shape
2. Remove old field entry
3. Add new field entry with new type
4. Finalize → get new `ShapeEntry*` from `ShapePool` (deduplicated)
5. Allocate new data buffer with new byte layout
6. Copy compatible fields from old data to new data (at new offsets)
7. **Convert the changed field's value** (not silently zero it — fix the MarkEditor bug):
   - INT → FLOAT: `(double)old_int`
   - FLOAT → INT: `(int64_t)old_double` (truncation, with warning)
   - Scalar → container: store the new container pointer
   - Container → scalar: decrement old ref count, store new scalar
8. Free old data buffer, update map's `type` and `data` pointers

#### 4.4.3 Fix NULL↔Container Shape Inconsistency

The current `fn_map_set` allows NULL↔container transitions without updating the shape. This creates metadata inconsistency. With the MarkEditor delegation approach, this case is handled correctly: the shape is rebuilt with the field's actual type.

For performance, a narrower fix: treat NULL fields as "polymorphic slots" that always store as `void*` (8 bytes), which is compatible with both NULL (null pointer) and container types (pointer). The shape entry's type stays `LMD_TYPE_NULL` but the byte size is `sizeof(void*)`. This avoids shape rebuild for the common NULL→container pattern but requires careful handling in readers.

**Recommendation:** Use the MarkEditor delegation approach for correctness. Optimize the NULL↔container fast path later if profiling shows it matters.

### 4.5 Element Mutation

Elements have dual nature: **attributes** (map-like, accessed via shape) and **children** (list-like, accessed via items array).

#### 4.5.1 Heap-Allocated Elements (Created in Script)

For elements created in procedural code (heap-allocated), follow the same patterns as maps and arrays:

**Attribute assignment** (`elem.attr = val`):
- Same as map field assignment (§4.4)
- `fn_map_set` already handles `LMD_TYPE_ELEMENT` for same-type attributes
- With MarkEditor delegation, type-changing attribute assignment works too

**Child assignment** (`elem[i] = val`):
- Extend `fn_array_set` to handle `LMD_TYPE_ELEMENT`:

```cpp
case LMD_TYPE_ELEMENT: {
    // Element extends List — children are in items array
    Element* el = (Element*)arr;
    // Bounds check against el->length (inherited from List)
    // Store value as Item in el->items[index]
    // Handle ref counting for old and new values
    array_set((Array*)el, index, value);
    break;
}
```

Since Element extends List and children are generic Items, no type conversion is needed — just store the Item.

#### 4.5.2 Markup-Sourced Elements (From Input Parsers)

Elements parsed from markup (JSON, XML, HTML, etc.) live in the markup buffer and may have specific memory layout requirements. For these, mutation should go through **MarkEditor in inline mode**:

**Detection:** Markup-sourced elements have a flag or memory region indicator. Check `Container::flags` for a markup-origin bit.

**Attribute assignment:**
```
fn_map_set(elem_item, key, value):
    if is_markup_element(elem):
        use MarkEditor::elmt_update_attr(elem, key, value)
    else:
        use standard map-set path (§4.4)
```

**Child assignment:**
```
fn_array_set(elem_ref, index, value):
    if is_markup_element(elem):
        use MarkEditor::elmt_replace_child(elem, index, value)
    else:
        direct items[index] store
```

#### 4.5.3 MarkEditor Integration

MarkEditor operates in two modes. For procedural assignment:

- **Default to inline mode** (in-place mutation) — procedural code expects mutation side effects
- MarkEditor is already thread-safe for single-writer scenarios
- Immutable mode (COW) is reserved for functional transformations, not relevant for procedural assignment

The runtime needs a **global or per-context MarkEditor instance** accessible from `fn_map_set`/`fn_array_set`:

```cpp
// In lambda runtime context
MarkEditor* get_inline_editor();  // lazily initialized, inline mode
```

### 4.6 Capture Mutability

#### 4.6.1 Detect Mutations in Captured Variables

In `build_ast.cpp`, after building the full function body, scan `assign_stam` nodes to see if any target a captured variable:

```cpp
// During build_assign_stam:
NameEntry* entry = lookup_name(tp, target_name);
if (entry && entry->scope != tp->current_scope) {
    // This is a captured variable being mutated
    CaptureInfo* capture = find_capture(tp, target_name);
    if (capture) capture->is_mutable = true;
}
```

#### 4.6.2 Mutable Captures Use Indirection

For mutable captures, the transpiler must emit capture-by-reference (pointer indirection) rather than capture-by-value:

```c
// Immutable capture (current): Item _captured_x = outer_x;
// Mutable capture (proposed): Item* _captured_x_ref = &outer_x;
//   Access:  *_captured_x_ref
//   Assign:  *_captured_x_ref = new_val;
```

This ensures mutations in the closure are visible in the outer scope and vice versa.

---

## 5. Implementation Plan

### Phase 1: Foundation & Safety (Correctness) — ✅ DONE

| # | Task | Files | Status |
|-|-|-|-|
| 1.1 | Add `is_mutable`, `has_type_annotation`, `type_widened` to `NameEntry` | ast.hpp | ✅ Done |
| 1.2 | Add `entry` back-pointer to `AstNamedNode`, `target_entry` to `AstAssignStamNode` | ast.hpp | ✅ Done |
| 1.3 | Set `is_mutable` and `has_type_annotation` in `build_var_stam` | build_ast.cpp | ✅ Done |
| 1.4 | Validate mutability in `build_assign_stam` (reject `let` and param targets) | build_ast.cpp | ✅ Done |
| 1.5 | Type analysis in `build_assign_stam` (widening for non-annotated, type checking for annotated) | build_ast.cpp | ✅ Done |
| 1.6 | Add type validation to `fn_array_set` for specialized arrays (reject with error instead of corrupt) | lambda-eval.cpp | ✅ Done (Phase 3) |
| 1.7 | Add `LMD_TYPE_ELEMENT` case to `fn_array_set` | lambda-eval.cpp | ✅ Done (Phase 3) |

### Phase 2: `var` Type Flexibility — ✅ DONE

| # | Task | Files | Status |
|-|-|-|-|
| 2.1 | Emit widened `var` declarations as `Item` in transpiler (`transpile_assign_expr`) | transpile.cpp | ✅ Done |
| 2.2 | Update `transpile_assign_stam` to box values for widened `var` targets | transpile.cpp | ✅ Done |
| 2.3 | Update `transpile_primary_expr` to unbox widened var references | transpile.cpp | ✅ Done |
| 2.4 | Update `transpile_box_item` to skip boxing for widened var references | transpile.cpp | ✅ Done |
| 2.5 | Non-widened vars keep concrete C types (zero overhead) | transpile.cpp | ✅ Done (default behavior) |

### Phase 3: Array Type Conversion — ✅ DONE

| # | Task | Files | Status |
|-|-|-|-|
| 3.1 | Implement `convert_specialized_to_generic()` in-place conversion | lambda-eval.cpp | ✅ Done |
| 3.2 | Wire type checking into `fn_array_set` — check value type, convert on mismatch | lambda-eval.cpp | ✅ Done |
| 3.3 | Add `LMD_TYPE_LIST` and `LMD_TYPE_ELEMENT` cases to `fn_array_set` | lambda-eval.cpp | ✅ Done |
| 3.4 | Add runtime type fallback to `array_int_get`, `array_int64_get`, `array_float_get` | lambda-data-runtime.cpp | ✅ Done |
| 3.5 | Test: `proc_array_type_convert.ls` (8 test cases) | test/lambda/proc/ | ✅ Done |

**Key insight:** No signature change was needed. Since all array types share the same struct layout, conversion happens in place — the struct pointer stays the same, only `type_id` and items buffer change. Specialized getters check `type_id` at runtime to handle converted arrays.

### Phase 4: Map Shape Rebuild

| # | Task | Files | Priority |
|-|-|-|-|
| 4.1 | Create runtime accessor for MarkEditor (inline mode) | lambda-eval.cpp | High |
| 4.2 | Delegate type-changing `fn_map_set` to MarkEditor::map_update | lambda-eval.cpp | High |
| 4.3 | Fix MarkEditor `map_rebuild_with_new_shape` to convert values instead of zeroing | mark_editor.cpp | High |
| 4.4 | Fix NULL↔container shape inconsistency (route through MarkEditor) | lambda-eval.cpp | Medium |

### Phase 5: Element Mutation

| # | Task | Files | Priority |
|-|-|-|-|
| 5.1 | Add markup-origin detection flag to Container | lambda.hpp | Medium |
| 5.2 | Route markup-element attribute sets through MarkEditor | lambda-eval.cpp | Medium |
| 5.3 | Route markup-element child sets through MarkEditor | lambda-eval.cpp | Medium |
| 5.4 | Handle heap-element child assignment directly (List-based store) | lambda-eval.cpp | High |

### Phase 6: Capture Mutability

| # | Task | Files | Priority |
|-|-|-|-|
| 6.1 | Detect mutable captures in `build_assign_stam` | build_ast.cpp | Medium |
| 6.2 | Set `CaptureInfo::is_mutable` flag | build_ast.cpp | Medium |
| 6.3 | Emit pointer indirection for mutable captures in transpiler | transpile.cpp | Medium |

### Phase 7: Testing

| # | Task | Files | Priority |
|-|-|-|-|
| 7.1 | Test: var type change (int→string, int→float, etc.) | test/lambda/proc/ | High |
| 7.2 | Test: let/param reassignment rejected | test/lambda/proc/ | High |
| 7.3 | Test: ArrayInt assignment with float/string → conversion | test/lambda/proc/ | High |
| 7.4 | Test: map field type change | test/lambda/proc/ | High |
| 7.5 | Test: element attribute mutation | test/lambda/proc/ | High |
| 7.6 | Test: element child assignment | test/lambda/proc/ | High |
| 7.7 | Test: NULL↔container map field transitions | test/lambda/proc/ | Medium |
| 7.8 | Test: markup-sourced element mutation via MarkEditor | test/lambda/proc/ | Medium |

---

## 6. Detailed Bug Catalog

### BUG-1: `fn_array_set` Silent Data Corruption on Type Mismatch — ✅ FIXED

**File:** lambda-eval.cpp:3139–3157  
**Trigger:** Assign float to `ArrayInt`, string to `ArrayInt`, int to `ArrayFloat`, etc.  
**Effect:** `get_int56()`/`get_double()` extract raw tagged-pointer bits — stores garbage.  
**Fix:** Phase 3 — `fn_array_set` now checks value type. On mismatch, `convert_specialized_to_generic()` converts the array in place, then stores via `array_set()`.

### BUG-2: `fn_array_set` Rejects Element Children — ✅ FIXED

**File:** lambda-eval.cpp:3159 (default case)  
**Trigger:** `elem[i] = val` in procedural code.  
**Effect:** Logs error, silently returns. Assignment is lost.  
**Fix:** Phase 3 — added `LMD_TYPE_LIST` and `LMD_TYPE_ELEMENT` cases that use `array_set()` directly (both have `Item* items`).

### BUG-3: `fn_map_set` NULL↔Container Shape Inconsistency

**File:** lambda-eval.cpp:3247  
**Trigger:** Assign container to a field whose shape says `LMD_TYPE_NULL`.  
**Effect:** Shape metadata says NULL but actual data is a container pointer. Readers using shape type will misinterpret.  
**Fix:** Phase 4.4 (route through MarkEditor for shape rebuild).

### BUG-4: MarkEditor Zeroes Values on Type Change

**File:** mark_editor.cpp:496  
**Trigger:** `map_rebuild_with_new_shape` when a field changes type.  
**Effect:** Old value is silently zeroed instead of converted.  
**Fix:** Phase 4.3 (add value conversion logic during rebuild).

### BUG-5: `let` Variables Can Be Reassigned in `pn` — ✅ FIXED

**File:** build_ast.cpp:4645–4720  
**Trigger:** `let x = 5` followed by `x = 10` inside a `pn`.  
**Effect:** No error; mutation of immutable binding.  
**Fix:** Phase 1.3–1.4 (`is_mutable` flag + validation in `build_assign_stam`). Now reports error E211.

### BUG-6: Function Parameters Can Be Reassigned — ✅ FIXED

**File:** build_ast.cpp:4645–4720  
**Trigger:** `pn foo(x) { x = 10 }`  
**Effect:** No error; parameter is mutated (may cause downstream issues).  
**Fix:** Phase 1.4 (validate mutability in `build_assign_stam`). Now reports error E211.

### BUG-7: Capture `is_mutable` Never Set

**File:** build_ast.cpp:451  
**Trigger:** `var x = 0; fn() { x = 1 }` — closure captures `x` as immutable.  
**Effect:** Mutations to captured `var` may not propagate correctly.  
**Fix:** Phase 6.1–6.2 (detect and flag mutable captures).

### BUG-8: `var` With Scalar Type Cannot Be Reassigned to Different Type — ✅ FIXED

**File:** transpile.cpp:3091–3158  
**Trigger:** `var x = 42; x = "hi"` — `_x` is `int64_t`, value is `String*`.  
**Effect:** C type mismatch; potential undefined behavior or compile error.  
**Fix:** Phase 2.1–2.4 (smart type widening — widened vars use `Item` storage, non-widened vars keep concrete C types).

---

## 7. Test Plan

### 7.1 Variable Reassignment Tests

```lambda
// test: var_type_change.ls
pn main() {
    // T1: int → float
    var x = 42
    x = 3.14
    print(x)          // expect: 3.14

    // T2: int → string
    var y = 10
    y = "hello"
    print(y)          // expect: hello

    // T3: string → int
    var z = "world"
    z = 99
    print(z)          // expect: 99

    // T4: null → int → string → null
    var w = null
    w = 42
    print(w)          // expect: 42
    w = "changed"
    print(w)          // expect: changed
    w = null
    print(w)          // expect: null

    // T5: bool → int
    var b = true
    b = 1
    print(b)          // expect: 1
}
```

### 7.2 Immutability Enforcement Tests

```lambda
// test: let_immutable.ls — should produce compile errors
pn main() {
    let x = 42
    // x = 10         // ERROR: cannot assign to immutable binding 'x'
    print(x)
}

// test: param_immutable.ls — should produce compile errors
pn foo(x) {
    // x = 10         // ERROR: cannot assign to immutable binding 'x'
    print(x)
}
```

### 7.3 Specialized Array Tests

```lambda
// test: array_type_convert.ls
pn main() {
    // T1: ArrayInt — assign float → converts to generic
    var a = [1, 2, 3]       // ArrayInt
    a[1] = 3.14             // should convert to generic Array
    print(a)                 // expect: [1, 3.14, 3]

    // T2: ArrayFloat — assign string → converts to generic
    var b = [1.1, 2.2]
    b[0] = "hi"
    print(b)                 // expect: [hi, 2.2]

    // T3: ArrayInt — same type stays specialized
    var c = [10, 20, 30]
    c[2] = 99
    print(c)                 // expect: [10, 20, 99]
}
```

### 7.4 Map Type Change Tests

```lambda
// test: map_type_change.ls
pn main() {
    // T1: int field → string
    var m = { name: "Alice", age: 30 }
    m.age = "thirty"
    print(m.age)             // expect: thirty

    // T2: null field → int
    var m2 = { x: null }
    m2.x = 42
    print(m2.x)              // expect: 42

    // T3: int → float
    var m3 = { val: 10 }
    m3.val = 3.14
    print(m3.val)            // expect: 3.14
}
```

### 7.5 Element Mutation Tests

```lambda
// test: element_mutation.ls
pn main() {
    // T1: element attribute assignment
    var el = <div class="old">Hello</div>
    el.class = "new"
    print(el.class)          // expect: new

    // T2: element child assignment
    var el2 = <ul>[<li>A</li>, <li>B</li>]</ul>
    el2[0] = <li>X</li>
    print(el2[0])            // expect: <li>X</li>

    // T3: heap-created element mutation
    var el3 = <span>"text"</span>
    el3[0] = "new text"
    print(el3[0])            // expect: new text
}
```

---

## 8. Summary

The current mutation support in Lambda's procedural code has **8 identified bugs** ranging from silent data corruption (specialized array assignment) to metadata inconsistency (map shape after NULL transitions) to missing enforcement (let/param immutability). The root causes are:

1. **Ad hoc implementation** — `fn_array_set` and `fn_map_set` were built for specific benchmark patterns, not general mutation
2. **No type-change support in the transpiler** — `var` variables are emitted as fixed C types
3. **MarkEditor not connected to assignment syntax** — the full mutation engine exists but is only used by input parsers

### Implemented (Phase 1, 2, & 3)

The following are now implemented and passing all 243 baseline tests:

- **Smart type analysis for `var` assignment** (§4.1): The transpiler analyzes all assignments to each `var` during AST building. If types are consistent, the variable uses its concrete C type (zero overhead). If types are inconsistent, the variable is widened to `Item` storage. Type-annotated vars enforce their declared type with static error reporting. Numeric coercion (int↔float↔int64↔decimal) is allowed for annotated vars.

- **Immutability enforcement** (§4.2): `let` bindings and function parameters now produce a compile-time error (E211) if assigned to in procedural code. Only `var` variables are mutable.

- **NameEntry tracking** (§4.1.2): Three new fields (`is_mutable`, `has_type_annotation`, `type_widened`) on `NameEntry` enable the type analysis without modifying AST types in place. Back-pointers (`AstNamedNode::entry`, `AstAssignStamNode::target_entry`) give the transpiler access to the analysis results.

- **Array type conversion** (§4.3): `fn_array_set` now validates value types before storing. When assigning an incompatible type to a specialized array (e.g., float to `ArrayInt`), the array is converted to a generic `Array` in place — the struct pointer stays the same, only `type_id` and the items buffer change. Specialized getters (`array_int_get`, etc.) check `type_id` at runtime to handle converted arrays. Element and List children are also supported.

### Remaining (Phase 4–7)

The remaining phases address deeper mutation scenarios:
- **Specialized array → generic conversion** on type-incompatible element assignment
- **Map shape rebuild via MarkEditor** for field type changes
- **Element mutation** for both heap-allocated and markup-sourced elements
- **Mutable capture** support for closures

These are deferred for a separate implementation cycle.

