# Splay Benchmark Performance Tuning: Results

## Summary

Added `type SplayNode` definition and typed annotations to convert runtime `fn_member()`/`fn_map_set()` calls into direct byte-offset memory loads/stores via the MIR JIT's Phase 3 direct field access optimization.

## Performance Results

| Version | Debug Build (ms) | Release Build (ms) |
|---------|------------------:|-------------------:|
| **Baseline (untyped)** | 1647.8 | — |
| **+ Typed SplayNode** | ~735 | ~530 |
| **+ Typed SplayTree** | ~657 | ~510 |
| **Total Improvement** | **2.51x faster** | — |
| Node.js (v22, V8 JIT) | 28.8 | — |
| vs Node.js | 22.8x | 17.7x |

The original ratio was 57.2x slower than Node.js. After both optimizations: **22.8x** (debug) / **17.7x** (release).

## What Was Changed in splay.ls

### 1. Type Definition

```lambda
type SplayNode = {key: float, left: map, right: map}
```

Only the 3 hot fields are typed. `value` is excluded from the type because it's not accessed during splay operations. The field order must match the map literal order.

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
type SplayTree = {root: map}
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
pn insert_new_node(tree: SplayTree, rng) { ... }
```

This converts ~24K+ `tree.root` accesses from `fn_member()` lookups to direct byte-offset loads/stores. The `root` field is typed as `map` (not `SplayNode`) since both null and SplayNode values must be stored there.

## Why Only 2.24x (Not the Projected 15-25x)

The initial estimate of 15-25x assumed ALL field accesses would convert to direct access. In practice:

### Untyped accesses remain on the slow path

- `rng.seed` — `RngState` type couldn't be applied (integer literal in float field crashes the runtime).
- Untyped `value` parameter in `create_node` — would need generic type support.

### Payload allocation dominates

`generate_payload(depth=5)` creates 63 maps per node × ~12K nodes = ~756K maps. This allocation pressure is unaffected by type annotations. The GC and allocator overhead forms a large fraction of the total runtime.

### Null guards add overhead

Every direct field read emits a null guard (check if Container* is 0 before loading data). This adds branch instructions that are absent in V8's type-speculated code.

## Bugs Discovered During Implementation

### Bug 1: `LMD_TYPE_ANY` Size Mismatch (ROOT CAUSE of crashes)

**Problem**: When a map literal is created inside a function with untyped parameters (e.g., `pn create_node(key, value)`), the map's shape entries use `LMD_TYPE_ANY` for untyped fields. `type_info[LMD_TYPE_ANY].byte_size = sizeof(TypedItem) = 16 bytes`, while typed fields (float, map) are 8 bytes. This causes byte offset misalignment between the compile-time type definition and the runtime data buffer.

**Example**: `{key: key, left: null, right: null}` where `key` is untyped:
- Runtime shape: key@offset 0 (16 bytes, ANY), left@offset 16 (8 bytes, NULL), right@offset 24 (8 bytes, NULL)
- Type definition `{key: float, left: map, right: map}`: key@offset 0 (8 bytes), left@offset 8, right@offset 16
- Direct access reads `node.left` at offset 8, but the actual data is at offset 16 → garbage

**Fix**: Use `var node: SplayNode = {key: key, ...}` inside `create_node()` so the map is constructed using the type definition's shape.

### Bug 2: Recursive Type Definitions Cause Stack Overflow

**Problem**: `type SplayNode = {left: SplayNode, right: SplayNode}` causes infinite recursion during type resolution.

**Workaround**: Use `left: map, right: map` instead of self-reference.

### Bug 3: Integer Literal in Float-Typed Field Crashes

**Problem**: `type RngState = {seed: float}` with `{seed: 49734321}` (integer literal) causes SIGSEGV. The runtime expects a boxed float but receives a boxed int.

**Status**: Not fixed. `RngState` type annotation was removed.

### Bug 4: MIR JIT Parameter Type Inference Bug (Pre-existing)

**Problem**: In untyped functions, the presence of `(tree.root).key == key` (comparing a member access to an untyped parameter) causes the parameter `key` to be read as 0.0 even when passed a different float value. This affects the untyped version of splay.ls, causing an infinite loop.

**Minimal repro**:
```lambda
pn splay_insert(tree, key, value) {
    if (splay_is_empty(tree)) {
        tree.root = create_node(key, value)
        return 0
    }
    if ((tree.root).key == key) {   // ← presence of this line causes key=0
        return 0
    }
    return 0
}
```

**Status**: Pre-existing bug. Not introduced by our changes. The typed version avoids it because all parameters have explicit type annotations.

## Remaining Optimization Opportunities

1. **Fix Bug 3 (int→float in typed fields)**: Would allow `type RngState = {seed: float}` with integer seed values, converting `rng.seed` accesses to direct access.

2. **Fix Bug 2 (recursive types)**: Would allow `left: SplayNode` instead of `left: map`, enabling full type propagation through the tree without indirection.

3. **Reduce payload allocation**: The 756K payload maps dominate allocation time. Consider typed payload maps or reduced depth.

4. **Eliminate null guards**: In hot loops where a node has been null-checked, subsequent field accesses don't need re-checking. This requires flow-sensitive null analysis in the transpiler.
