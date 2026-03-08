# Splay Benchmark Performance Tuning: Results

## Summary

Added `type SplayNode` definition and typed annotations to convert runtime `fn_member()`/`fn_map_set()` calls into direct byte-offset memory loads/stores via the MIR JIT's Phase 3 direct field access optimization. Implemented **direct field store optimization** in the transpiler to eliminate `map_fill()` varargs overhead for typed map construction. **Inlined `map_with_data()`** in the transpiler to emit `heap_calloc` + direct Map header stores with compile-time constants. Eliminated redundant `memset` in `gc_heap_calloc` for slab-allocated objects. **Fixed catastrophic O(slabs) `gc_object_zone_owns`** with O(log N) binary search over sorted slab ranges — **20x speedup** (~2020ms → ~97ms release).

## Performance Results

| Version | Debug Build (ms) | Release Build (ms) |
|---------|------------------:|-------------------:|
| **Baseline (untyped)** | 1647.8 | — |
| **+ Typed SplayNode** | ~735 | ~530 |
| **+ Typed SplayTree** | ~657 | ~510 |
| **+ Recursive types & RngState** | ~655 | ~480 |
| **+ value field in type (correct)** | ~5400 | ~4500 |
| **+ Direct field stores (opt #1)** | ~4019 | ~4000 |
| **+ Typed payload maps** | — | ~2280 |
| **+ Inline map_with_data + gc_heap fix (opt #3)** | ~2780 | ~2020 |
| **+ gc_object_zone_owns O(log N) fix** | ~165 | **~97** |
| Node.js (v22, V8 JIT) | 28.8 | 28.8 |
| vs Node.js (current) | — | **3.4x** |
| vs Node.js (before opt) | — | 156x |

The direct field store optimization (eliminating `map_fill()` varargs) gives a **~50% reduction** in runtime when combined with typed payload maps. Adding type annotations to `generate_payload` (PayloadLeaf, PayloadBranch types) enables the optimization to cover all ~768K map allocations (not just the ~12K SplayNodes).

## What Was Changed in splay.ls

### 1. Type Definition

```lambda
type SplayNode = {key: float, left: SplayNode, right: SplayNode, value: map}
type SplayTree = {root: SplayNode}
type RngState = {seed: float}
type PayloadLeaf = {arr: array, str: float}
type PayloadBranch = {left_p: map, right_p: map}
```

All SplayNode fields are typed. The field order must match the map literal order in `create_node`. `left` and `right` use the recursive `SplayNode` type (enabled by the Bug 2 fix). `RngState` types the RNG seed as float (enabled by the Bug 3 fix). `value` is typed as `map` so the payload is properly stored. `PayloadLeaf` and `PayloadBranch` type the payload maps so `generate_payload` also benefits from the direct field store optimization.

### 2. Typed Map Constructor

```lambda
pn create_node(key: float, value) {
    var node: SplayNode = {key: key, left: null, right: null, value: value}
    return node
}
```

**Critical**: The `SplayNode` annotation on the intermediate variable is required so the runtime data buffer uses the type's byte offsets. Without it, untyped parameters cause fields to be stored as `LMD_TYPE_ANY` (16-byte `TypedItem`) instead of native 8-byte values, misaligning all subsequent field offsets.

### 3. Typed Local Variables

All local variables holding splay nodes are annotated:

```lambda
var dummy: SplayNode = create_node(0.0, null)
var left: SplayNode = dummy
var right: SplayNode = dummy
var current: SplayNode = tree.root
var tmp: SplayNode = current.left
```

### 4. Typed Function Parameters

```lambda
pn count_nodes(node: SplayNode) { ... }
pn splay_find_max(node: SplayNode) { ... }
```

### 5. SplayTree Type Definition and Annotations

```lambda
type SplayTree = {root: SplayNode}
```

Applied to the tree constructor and all functions taking a `tree` parameter:

```lambda
pn splay_tree_new() {
    var tree: SplayTree = {root: null}
    return tree
}

pn splay(tree: SplayTree, key: float) { ... }
pn splay_insert(tree: SplayTree, key: float, value) { ... }
pn splay_remove(tree: SplayTree, key: float) { ... }
pn splay_find(tree: SplayTree, key: float) { ... }
pn splay_find_greatest_less_than(tree: SplayTree, key: float) { ... }
pn insert_new_node(tree: SplayTree, rng: RngState) { ... }
```

This converts ~24K+ `tree.root` accesses from `fn_member()` lookups to direct byte-offset loads/stores. The `root` field is typed as `SplayNode` (enabled by the recursive type fix).

## Current Performance Gap vs V8 (79x)

### What was optimized

- **Direct field reads/writes**: Phase 3 direct access converts `fn_member`/`fn_map_set` to byte-offset loads/stores for typed variables.
- **Direct field stores in constructors**: Optimization #1 (implemented) eliminates `map_fill()` varargs + `set_fields()` loop for typed map literals. The transpiler emits direct MIR stores at compile-time byte offsets into the inline data buffer.
- **Typed payload maps**: Adding `PayloadLeaf` and `PayloadBranch` types extends the optimization to cover all ~768K map allocations, not just the ~12K SplayNodes.

### Remaining bottlenecks

1. **GC allocation overhead**: Each map still goes through `heap_calloc` → slab free-list pop + memset + header init. V8 uses a bump-pointer nursery (~3 instructions).
2. **Null guards on every field access**: Each `current.key`, `current.left`, etc. emits a branch to check for null before loading data. V8's speculative optimization eliminates these.
3. **No function inlining**: Every function call (splay_, create_node, etc.) has full C calling convention overhead. V8 inlines hot functions.
4. **No escape analysis**: Lambda heap-allocates every map. V8 can sometimes stack-allocate short-lived objects.

## Bugs Discovered During Implementation

### Bug 1: `LMD_TYPE_ANY` Size Mismatch (ROOT CAUSE of crashes)

**Problem**: When a map literal is created inside a function with untyped parameters (e.g., `pn create_node(key, value)`), the map's shape entries use `LMD_TYPE_ANY` for untyped fields. `type_info[LMD_TYPE_ANY].byte_size = sizeof(TypedItem) = 16 bytes`, while typed fields (float, map) are 8 bytes. This causes byte offset misalignment between the compile-time type definition and the runtime data buffer.

**Example**: `{key: key, left: null, right: null}` where `key` is untyped:
- Runtime shape: key@offset 0 (16 bytes, ANY), left@offset 16 (8 bytes, NULL), right@offset 24 (8 bytes, NULL)
- Type definition `{key: float, left: map, right: map}`: key@offset 0 (8 bytes), left@offset 8, right@offset 16
- Direct access reads `node.left` at offset 8, but the actual data is at offset 16 → garbage

**Fix**: Use `var node: SplayNode = {key: key, ...}` inside `create_node()` so the map is constructed using the type definition's shape.

### Bug 2: Recursive Type Definitions Fail to Resolve (FIXED)

**Problem**: `type SplayNode = {left: SplayNode, right: SplayNode}` — the self-reference `SplayNode` isn't in scope during type body building (`push_name` happens after `build_expr`), so fields resolve to `TYPE_ANY` (16 bytes), breaking direct field access optimization.

**Fix**: Pre-register the type name with a placeholder `TypeType(TypeMap)` in `build_assign_expr()` BEFORE building the type body. Self-references now resolve to the pre-registered entry. The final `push_name` is skipped for type definitions to avoid duplicates.

**Files changed**: `lambda/build_ast.cpp` — `build_assign_expr()`

### Bug 3: Integer Literal in Float-Typed Field Crashes (FIXED)

**Problem**: `type RngState = {seed: float}` with `{seed: 49734321}` (integer literal) causes SIGSEGV. In `set_fields()`, the `LMD_TYPE_FLOAT` case calls `item.get_double()` which dereferences `double_ptr` — but for int Items, the value is packed inline as int56, so `double_ptr` is garbage.

**Fix**: Added type coercion in `set_fields()`: check `get_type_id(item)` before unpacking. If INT/INT64, convert via `(double)item.get_int56()` / `(double)item.get_int64()`. Similarly for `LMD_TYPE_INT` receiving float/bool values.

**Files changed**: `lambda/lambda-data.cpp` — `set_fields()` LMD_TYPE_FLOAT and LMD_TYPE_INT cases

### Bug 4: MIR JIT Parameter Type Inference Bug (FIXED)

**Problem**: In untyped `pn` functions, `infer_param_type()` in the MIR transpiler gathers evidence about how parameters are used in the function body. When an untyped parameter appears in a comparison like `(tree.root).key == key`, the `INFER_NUMERIC_USE` flag is set. For procedural functions without float literals in the body, the heuristic `INFER_NUMERIC_USE && !INFER_FLOAT_CONTEXT → INT` aggressively infers the parameter as `int`. This causes the native function version to read the parameter's float bits as an integer → 0 or garbage.

**Minimal repro**:
```lambda
pn splay_insert(tree, key, value) {
    if (splay_is_empty(tree)) {
        tree.root = create_node(key, value)
        return 0
    }
    if ((tree.root).key == key) {   // ← comparison triggers INFER_NUMERIC_USE → INT
        return 0
    }
    return 0
}
```

**Root cause**: `gather_evidence()` sets `INFER_NUMERIC_USE` for both arithmetic (`+`,`-`,`*`,`/`) and comparisons (`==`,`<`,`>`). Comparisons are polymorphic — they don't prove a parameter is int. But the decision heuristic at `infer_param_type()` treats any `NUMERIC_USE` as evidence for INT.

**Fix**: Added `INFER_ARITH_USE` flag (=32) set only for arithmetic operations. Changed the heuristic to require `INFER_ARITH_USE` (not just `INFER_NUMERIC_USE`) before inferring INT — comparison-only usage no longer triggers the INT inference.

**Files changed**: `lambda/transpile-mir.cpp` — `gather_evidence()`, `infer_param_type()`
**Test added**: `test/lambda/proc/proc_param_type_infer.ls` — verifies float params in comparisons with map fields

## Remaining Optimization Opportunities

### How `create_node` Currently Allocates

When the JIT compiles `var node: SplayNode = {key: key, left: null, right: null, value: value}`, it emits two runtime calls:

1. **`map_with_data(type_index)`** — single combined GC allocation
   - Looks up the pre-built `TypeMap` by index (shape is shared, not per-node)
   - Allocates `sizeof(Map) + byte_size` = 32 + 32 = 64 bytes in one `heap_calloc`
   - Data buffer is placed inline immediately after the Map struct (no separate allocation)
   - Falls into the 96-byte GC slab size class (including 16-byte `gc_header_t`)

2. **`map_fill(m, key, null, null, value)`** — writes fields via C varargs
   - Iterates the type's shape entries, calls `set_fields()` with a per-field `switch(type_id)`

| Component | Bytes |
|-----------|-------|
| `gc_header_t` (GC header) | 16 |
| `Map` struct (type_id, flags, type ptr, data ptr, data_cap) | 32 |
| Inline data (key:8 + left:8 + right:8 + value:8) | 32 |
| **Total per slab slot** | **80 → 96-byte size class** |

### V8 Comparison

V8's `new SplayTree.Node(key, value)` uses hidden classes and a bump-pointer nursery:
- Allocation: single pointer bump (~3 instructions)
- `left`/`right` inherited from prototype (free until mutated, then hidden class transition)
- Field writes: direct stores at known offsets (inlined by TurboFan)

| Operation | V8 | Lambda |
|-----------|-----|--------|
| Allocation | Bump pointer (~3 instr) | Slab free-list pop + memset + header init + linked-list link |
| Shape lookup | Inline in IC (none) | `type_list->data[type_index]` (one indirection) |
| Field writes | Direct stores (inlined) | varargs + `set_fields()` loop with per-field switch |
| Left/right init | Prototype chain (free) | Explicit null writes via memset(0) |
| Total calls | 0 (fully inlined) | 2 (`map_with_data` + `map_fill`) |

### Optimization Opportunities (Ranked by Impact)

#### 1. ~~Eliminate `map_fill` varargs overhead~~ — IMPLEMENTED ✅

For typed maps with known shape at compile time, the transpiler now emits **direct stores** instead of calling `map_fill`. The optimization is triggered when:
- The map literal's TypeMap has a shape merged from a named type (`has_named_shape` flag)
- OR the TypeMap has `struct_name` set (named type definition)
- AND all fields have supported types (float, int, bool, string, container types)

**Implementation**: `transpile_map()` in `transpile-mir.cpp` loads the `data` pointer from the Map struct (offset 16) and emits type-specific MIR stores at known byte offsets for each field:
- FLOAT: `DMOV [data_ptr + offset], val` (native double)
- INT/BOOL: `MOV [data_ptr + offset], val` (native int64)
- Container (MAP, LIST, etc.): `MOV [data_ptr + offset], raw_ptr` (tag-stripped Container*)
- NULL: skipped (data is zero-initialized by `heap_calloc`)

**Files changed**:
- `lambda/transpile-mir.cpp` — `transpile_map()`: direct store path for typed maps
- `lambda/build_ast.cpp` — shape merging: sets `has_named_shape` flag on literal TypeMaps
- `lambda/lambda-data.hpp` — `TypeMap`: added `has_named_shape` boolean field

**Result**: ~50% reduction when all hot-path maps are typed (4500ms → 2280ms release).

```c
// Before: map_fill(m, key_item, null_item, null_item, value_item)  // varargs + switch loop
// After:
Map* m = map_with_data(type_index);
*(double*)(m->data + 0)  = key;      // key: float at offset 0
// left/right: skipped (already 0)   // heap_calloc zero-initializes
*(void**)(m->data + 24)  = payload;  // value: map at offset 24
```

#### 2. Bump-pointer nursery allocator (High impact, High effort)

The slab allocator's free-list pop is O(1) but still involves: pop → memset zero → init gc_header → link to all_objects. A bump-pointer nursery would reduce allocation to:

```c
void* ptr = cursor; cursor += size; return ptr;  // ~3 instructions
```

This is what makes V8 fast at exactly this benchmark. The splay benchmark was literally designed to measure young-generation GC efficiency.

#### 3. ~~Inline `map_with_data` for known types~~ — IMPLEMENTED ✅

Since `type_index` is a compile-time constant, the JIT now emits the allocation inline instead of calling `map_with_data()`. Combined with the Opt #1 direct field stores, the entire typed map construction becomes: one `heap_calloc` + header stores + direct field stores, with zero runtime function calls.

**Implementation**: `transpile_map()` in `transpile-mir.cpp` emits:
1. `heap_calloc(sizeof(Map) + byte_size, LMD_TYPE_MAP)` — single GC allocation with compile-time constant size
2. Direct Map header stores: `type_id` (offset 0), `type` pointer (offset 8), `data` pointer (offset 16), `data_cap` (offset 24)
3. Data pointer computed as `m + sizeof(Map)` via MIR ADD instruction (no runtime lookup)

Additionally, `gc_heap_calloc()` in `lib/gc_heap.c` was optimized to skip the redundant `memset` for slab-allocated objects (≤256 bytes), since `gc_object_zone_alloc` already returns zeroed memory.

**Files changed**:
- `lambda/transpile-mir.cpp` — `transpile_map()`: inline allocation path
- `lib/gc_heap.c` — `gc_heap_calloc()`: skip redundant memset for small objects

**Result**: ~12% further reduction (2280ms → 2020ms release).

```c
// Before: Map* m = map_with_data(type_index);     // runtime lookup + alloc
// After (emitted by JIT):
Map* m = heap_calloc(64, LMD_TYPE_MAP);             // size = compile-time constant
*(uint8_t*)m = LMD_TYPE_MAP;                         // type_id at offset 0
*(TypeMap**)(m + 8) = KNOWN_TYPE_PTR;                // type pointer = constant
char* data = (char*)m + 32;                           // data = m + sizeof(Map)
*(char**)(m + 16) = data;                             // store data pointer
*(int*)(m + 24) = 32;                                 // data_cap = constant
*(double*)(data + 0) = key;                           // field stores (from opt #1)
```

#### 4. Eliminate null guards in hot loops (Medium impact, High effort)

Every `current.key`, `current.left`, `current.right` emits a null guard:
```c
if (!current) return ITEM_NULL;  // guard
double key = *(double*)(current->data + 0);
```

In the splay loop, `current` has already been null-checked. Flow-sensitive null analysis could eliminate redundant guards.

#### 5. Inline small functions (Low impact, High effort)

Hot helpers like `splay_is_empty(tree)` (a single `tree.root == null` check) could be inlined by the JIT to eliminate call overhead.

| # | Optimization | Expected Impact | Effort | Status |
|---|-------------|----------------|--------|--------|
| 1 | Direct stores (eliminate `map_fill`) | **~50% reduction** | Medium | **IMPLEMENTED** ✅ |
| 2 | Bump-pointer nursery | 30-50% | High | Not started |
| 3 | Inline `map_with_data` + gc_heap_calloc fix | **~12% reduction** | Low | **IMPLEMENTED** ✅ |
| 4 | Null guard elimination | 5-10% | High | Not started |
| 5 | Function inlining | 3-5% | High | Not started |
| 6 | gc_object_zone_owns O(log N) fix | **~20x reduction** | Low | **IMPLEMENTED** ✅ |

### GC Bug: O(slabs) Ownership Check — The Hidden Catastrophe

**Problem**: `gc_object_zone_owns()` was called for every pointer during GC mark-sweep tracing (via `is_gc_object()` → `gc_mark_item()` → `gc_trace_object()`). The original implementation iterated ALL slabs across ALL 7 size classes to check if a pointer fell within any slab's address range:

```c
// BEFORE: O(S×C) per check — S slabs × C=7 classes
int gc_object_zone_owns(gc_object_zone_t* oz, void* ptr) {
    for (int cls = 0; cls < 7; cls++) {
        gc_object_slab_t* slab = oz->slabs[cls];
        while (slab) {  // iterate ALL slabs in class
            if (p >= slab->base && p < slab_end) return 1;
            slab = slab->next;
        }
    }
    return 0;
}
```

With ~780K map allocations in the splay benchmark and 128 slots per slab, the 96-byte class alone accumulates ~6000+ slabs. During GC tracing, each of the ~8000 live SplayNodes has 4 fields to trace, and each field check iterates all slabs. This creates O(live_objects × fields × slabs) = millions of slab walks per GC cycle.

**Profiling evidence**: `sample` on macOS showed `gc_object_zone_owns` at 99.5% of total execution time (1522/1529 samples).

**Fix**: Replaced linear slab scan with a sorted array of `(base, end)` ranges + binary search:

1. Added `gc_slab_range_t` sorted array to `gc_object_zone_t`
2. Each `allocate_slab()` binary-inserts the new slab's range into the sorted array
3. Added global `min_addr`/`max_addr` for O(1) fast rejection of non-slab pointers
4. `gc_object_zone_owns()` now does: min/max check → binary search sorted ranges

```c
// AFTER: O(1) fast rejection + O(log S) binary search
int gc_object_zone_owns(gc_object_zone_t* oz, void* ptr) {
    if (p < oz->min_addr || p >= oz->max_addr) return 0;  // O(1) reject
    // binary search sorted slab_ranges array — O(log S)
    size_t lo = 0, hi = oz->range_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (ranges[mid].end <= p) lo = mid + 1;
        else if (ranges[mid].base > p) hi = mid;
        else return 1;  // found
    }
    return 0;
}
```

**Files changed**:
- `lib/gc_object_zone.h` — Added `gc_slab_range_t`, range array fields, min/max bounds
- `lib/gc_object_zone.c` — Added `register_slab_range()`, replaced `gc_object_zone_owns()`

**Result**: **~20x speedup** (2020ms → ~97ms release, 2780ms → ~165ms debug). Gap to Node.js reduced from 70x to **3.4x**.
