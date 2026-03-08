# Splay Benchmark Performance Tuning: Results

## Summary

Added `type SplayNode` definition and typed annotations to convert runtime `fn_member()`/`fn_map_set()` calls into direct byte-offset memory loads/stores via the MIR JIT's Phase 3 direct field access optimization. Implemented **direct field store optimization** in the transpiler to eliminate `map_fill()` varargs overhead for typed map construction. **Inlined `map_with_data()`** in the transpiler to emit `heap_calloc` + direct Map header stores with compile-time constants. Eliminated redundant `memset` in `gc_heap_calloc` for slab-allocated objects. **Fixed catastrophic O(slabs) `gc_object_zone_owns`** with O(log N) binary search over sorted slab ranges — **20x speedup** (~2020ms → ~97ms release). Replaced slab free-list allocator with a **4MB bump-pointer nursery** and **JIT-inlined bump-pointer fast path** — **~31% speedup** (~83ms → ~57ms release).

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
| **+ gc_object_zone_owns O(log N) fix** | ~165 | **~83** |
| **+ Bump-pointer nursery + JIT inline alloc (#10+#11)** | ~117 | **~57** |
| Node.js (v22, V8 JIT) | 28.8 | 28.8 |
| vs Node.js (current) | — | **2.0x** |
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

#### 4. Eliminate null guards in hot loops (Medium impact, High effort) — INFRASTRUCTURE ADDED ⚠️

Every `current.key`, `current.left`, `current.right` emits a null guard:
```c
if (!current) return ITEM_NULL;  // guard
double key = *(double*)(current->data + 0);
```

**Analysis**: Added `bool skip_null_guard` parameter to both `emit_mir_direct_field_read()` and `emit_mir_direct_field_write()`. When true, the null check branch, default-value path, and associated labels are all skipped — producing tighter code (fewer branches, no wasted default MOV).

**Problem**: Cannot safely enable yet. In Lambda, typed variables CAN hold null values:
```lambda
var current: SplayNode = tree.root  // typed SplayNode, but tree.root can be null
```
A simple `AST_NODE_IDENT` check was attempted but caused the splay benchmark to crash (null deref in JIT code). Correct null guard elimination requires **flow-sensitive non-null analysis**:
- After `if (x != null)` true branch → x is non-null
- After `var x = { ... }` map literal → x is non-null  
- After function calls → unknown (could return null)
- After reassignment from field access → unknown

**Profiling** (release, 72 samples): JIT hot loop ~49%, memory allocation ~36%, fn_member ~7%. Null guards account for only a fraction of the JIT code cost — estimated **~5% impact** if fully eliminated. Not worth the complexity of flow analysis at this stage.

**Current status**: Infrastructure in place (`skip_null_guard` parameter wired through), set to `false` at all call sites. Can be enabled per-site when flow analysis is added.

#### 5. Inline small functions (Low impact, High effort)

Hot helpers like `splay_is_empty(tree)` (a single `tree.root == null` check) could be inlined by the JIT to eliminate call overhead.

| #   | Optimization                                | Expected Impact    | Effort | Status            |
| --- | ------------------------------------------- | ------------------ | ------ | ----------------- |
| 1   | Direct stores (eliminate `map_fill`)        | **~50% reduction** | Medium | **IMPLEMENTED** ✅ |
| 2   | Bump-pointer nursery                        | 30-50%             | High   | **IMPLEMENTED** ✅ (see #10+#11) |
| 3   | Inline `map_with_data` + gc_heap_calloc fix | **~12% reduction** | Low    | **IMPLEMENTED** ✅ |
| 4   | Null guard elimination                      | ~5%                | High   | Infrastructure ⚠️ |
| 5   | Function inlining                           | 3-5%               | High   | Not started       |
| 6   | gc_object_zone_owns O(log N) fix            | **~20x reduction** | Low    | **IMPLEMENTED** ✅ |

### Further Memory Allocation Optimizations

Profiling (release, 72 samples) shows **36% of execution time** in the `heap_calloc` chain. The current call path for each object is 4 functions deep:

```
heap_calloc (lambda-mem.cpp)
  → gc_heap_calloc (gc_heap.c)
    → gc_heap_alloc (gc_heap.c)
      → gc_object_zone_alloc (gc_object_zone.c)
```

Inside `gc_object_zone_alloc`: class_index lookup (linear scan of 7 classes), free-list pop or slab bump, `memset` on reuse, header init (4 fields), linked-list link into `all_objects`.

Splay benchmark allocates **~1.14M objects** in the 48-byte class (PayloadLeaf/Branch maps) and **~35K** in the 64-byte class (SplayNodes), with only **2 GC cycles**.

| #   | Optimization                                     | Expected Impact | Effort | Status      |
| --- | ------------------------------------------------ | --------------- | ------ | ----------- |
| 7   | Eliminate `all_objects` linked-list per-alloc     | ~5-10%          | Medium | Not started |
| 8   | Specialize allocator for known size class in JIT  | ~10-15%         | Low    | ✅ Done (no measurable impact in release — ThinLTO already inlines class_index) |
| 9   | Skip Container `is_heap` flag for Map types       | ~2%             | Low    | Not started |
| 10  | Inline slab bump path in JIT (MIR instructions)   | ~15-20%         | Medium | **IMPLEMENTED** ✅ |
| 11  | Bump-pointer nursery (V8-style)                   | 30-50%          | High   | **IMPLEMENTED** ✅ |

#### 7. Eliminate `all_objects` linked-list per-alloc

Every allocation writes `header->next = *all_objects; *all_objects = header;` (2 pointer writes + pointer read via `gc_heap_t*`). Sweep could instead iterate slabs sequentially (better cache locality). Saves a pointer chase and write per allocation on the hot path.

#### 8. Specialize allocator for known size class in JIT — ✅ IMPLEMENTED

The JIT knows the exact allocation size at compile time (e.g., `sizeof(Map) + byte_size` = 64 for SplayNode). Currently, every `heap_calloc()` call enters `gc_object_zone_alloc()` which runs `gc_object_zone_class_index()` — a 7-iteration linear scan. A specialized `heap_calloc_class(size, type_tag, class_index)` function with the pre-computed class index skips this scan and enters the slab path directly.

**Implementation**: Added `gc_object_zone_alloc_class()`, `gc_heap_calloc_class()`, and `heap_calloc_class()` (3-function chain vs. 4-function chain). JIT in `transpile_map()` computes class index at compile time via `gc_object_zone_class_index()` and emits `emit_call_3(mt, "heap_calloc_class", ...)`.

**Result**: ~83-87ms release (no measurable improvement over baseline ~83ms). ThinLTO (`-flto=thin`) in release builds already inlines the class index computation, making the compile-time specialization redundant for the release binary. The optimization still helps debug builds.

#### 9. Skip Container `is_heap` flag for Map types

`heap_calloc()` in lambda-mem.cpp sets `((Container*)ptr)->is_heap = 1` for container types. For Map/Object, the data is already zero-initialized and `is_heap` is at a known byte offset. The JIT could set this directly as part of its inline header stores, eliminating the conditional branch in `heap_calloc`.

#### 10. Inline slab bump path directly in JIT

Instead of calling any C function, emit the slab-bump allocation as ~5 MIR instructions directly in the JIT code: load slab→next_fresh, compare with slot_count, compute slot address, bump counter, fall back to slow C path only on slab exhaustion. This eliminates all function call overhead on the fast path.

#### 11. Bump-pointer nursery (V8-style, same as #2)

The nuclear option: replace size-class slabs with a simple bump-pointer region for young objects. `ptr = cursor; cursor += size; return ptr;` (~3 instructions). Best suited for a generational GC design overhaul.

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

### Bump-Pointer Nursery + JIT Inline Allocation (#10 + #11) — IMPLEMENTED ✅

**Problem**: The slab free-list allocator, while O(1), performs per-allocation work: free-list pop, memset zero, gc_header init (4 field writes), all_objects linked-list link, Container.is_heap flag set. Profiling showed 36% of release build execution time in the allocation chain. V8 uses a bump-pointer nursery where allocation is a single pointer addition (~3 instructions).

**Design**: Combined approach — a 4MB bump-pointer nursery as the allocation backend (replacing slab free-list pops) with the bump fast path emitted directly as MIR instructions in the JIT.

#### Part 1: Bump-Pointer Nursery (#11)

Replaced the slab free-list allocation path with a contiguous bump-pointer region:

```c
// gc_heap_t layout (bump_cursor and bump_end at hot offsets 16 and 24):
typedef struct gc_heap_t {
    mem_pool_t* pool;               // offset 0
    gc_header_t* all_objects;       // offset 8  (same cache line as cursor/end)
    uint8_t* bump_cursor;           // offset 16 ← new
    uint8_t* bump_end;              // offset 24 ← new
    gc_object_zone_t object_zone;   // offset 32
    ...
};
```

Key design decisions:
- **Block sizing**: 4MB initial, doubling on exhaustion up to 64MB cap. Registered with object zone's slab_ranges for `gc_object_zone_owns()` O(log N) compatibility.
- **Slot alignment**: Bump slots are sized to `SIZE_CLASSES[cls]` (not just 8-byte aligned) so dead bump objects can be safely added to the slab free list during GC sweep.
- **all_objects compatibility**: Every bump-allocated object is linked into the all_objects list for GC sweep. A future optimization could eliminate this.
- **Free list reuse**: `gc_heap_bump_alloc()` checks the slab free list first before bumping. After GC sweep recycles dead objects to the free list, subsequent allocations reuse those recycled slots before consuming new bump space.

```c
void* gc_heap_bump_alloc(gc_heap_t* gc, size_t slot_size, size_t alloc_size,
                         uint16_t type_tag, int cls) {
    // Fast path: check free list first (GC recycling)
    gc_object_zone_t* oz = &gc->object_zone;
    if (oz->free_lists[cls]) {
        // reuse recycled slot from free list
        ...
    }
    // Bump path: pointer addition
    uint8_t* ptr = gc->bump_cursor;
    uint8_t* new_end = ptr + slot_size;
    if (new_end > gc->bump_end) {
        // allocate new block (double size, cap at 64MB)
        ...
    }
    gc->bump_cursor = new_end;
    // init header, link all_objects
    ...
}
```

**C-level result**: ~60ms release (from ~83ms) — **~28% speedup** from the bump nursery alone.

#### Part 2: JIT Inline Bump Path (#10)

The JIT emits the bump-pointer fast path directly as MIR instructions, eliminating all function call overhead for the common case (cursor has space):

```c
// Emitted MIR pseudo-code for typed map allocation:
MIR_reg_t gc_reg;    // cached pointer to gc_heap_t, loaded at function prologue
MIR_reg_t cursor = LOAD [gc_reg + 16];           // bump_cursor
MIR_reg_t new_cursor = cursor + slot_size;        // compile-time constant
if (new_cursor UBGT [gc_reg + 24]) goto slow;     // bump_end check

// Fast path: bump succeeded (~8 MIR instructions)
STORE [gc_reg + 16] = new_cursor;                 // advance cursor
// Init gc_header_t inline:
gc_header_t* h = cursor;
STORE [h + 0] = LOAD [gc_reg + 8];               // h->next = all_objects
STORE_U16 [h + 8] = type_tag;                     // h->type_tag
STORE_U32 [h + 12] = total_size;                  // h->alloc_size
STORE [gc_reg + 8] = h;                           // all_objects = h
void* user_ptr = h + 16;                          // skip header
STORE_U8 [user_ptr + 1] = 1;                      // Container.is_heap = 1
goto done;

slow:
    user_ptr = call heap_calloc_class(size, type_tag, cls);  // C fallback
done:
    // ... set Map header fields (type_id, type ptr, data ptr, data_cap)
```

The `gc_reg` pointer is loaded once at the function prologue (via `_lambda_rt` → context → heap → gc chain) and cached in a dedicated MIR register, saved/restored across user function transpilation boundaries.

**Files changed**:
- `lib/gc_heap.h` — Added `gc_bump_block_t`, bump fields in `gc_heap_t`, `gc_heap_bump_alloc()` declaration
- `lib/gc_heap.c` — Added `gc_alloc_bump_block()`, `gc_heap_bump_alloc()`, modified `gc_heap_create()`/`gc_heap_destroy()`
- `lambda/lambda-mem.cpp` — `heap_calloc_class()` now routes to `gc_heap_bump_alloc()`
- `lambda/transpile-mir.cpp` — Added `gc_reg` to `MirTranspiler`, gc_reg loading at function prologues, save/restore across user functions, inline bump MIR emission in `transpile_map()`

**Combined result (#10+#11)**: **~57ms release** (from ~83ms baseline) — **~31% speedup**. Gap to Node.js reduced from 2.9x to **2.0x**.
