# Proposal: Lambda Typed Array Phase 2 — N-D, Views, Vectorized Math, JS Unification

## Status of Phase 1 (baseline for this proposal)

The Phase 1 work (see [Lambda_Typed_Array.md](Lambda_Typed_Array.md)) is **landed and operational**:

- `LMD_TYPE_ARRAY_NUM` unifies the former `ARRAY_INT` / `ARRAY_INT64` / `ARRAY_FLOAT` into one type.
- `ArrayNum.elem_type` (upper 4 bits of the byte at `Container.flags`) selects one of 13 element kinds: `ELEM_INT`, `ELEM_INT64`, `ELEM_FLOAT`, `ELEM_INT{8,16,32}`, `ELEM_UINT{8,16,32}`, `ELEM_FLOAT{16,32}`, `ELEM_UINT64`, `ELEM_FLOAT64`. Lower 4 bits remain Container boolean flags (`is_content`, `is_spreadable`, `is_heap`, `is_data_migrated`).
- Storage is a tagged union: `int64_t* items` (ELEM_INT/INT64), `double* float_items` (ELEM_FLOAT), or `void* data` (compact elements). Bytes per element are looked up from `ELEM_TYPE_SIZE[elem_type >> 4]`.
- **Vectorized arithmetic is already implemented** in [lambda-vector.cpp](../lambda/lambda-vector.cpp) — `+`, `-`, `*`, `/`, `%`, `^` work element-wise between `ArrayNum`/`ArrayNum`, `ArrayNum`/`scalar`, and `ArrayNum`/`Range`/`Array` across all 13 element kinds, with scalar broadcast and type promotion.
- Aggregations: `sum`, `avg`, `min`, `max`, `abs` (array form), `cumsum`, `cumprod`, `argmin`, `argmax`, `dot`, `sort`, `fill`, `slice` dispatch on `LMD_TYPE_ARRAY_NUM`.
- `for`, `in`, `index_of` already iterate `ArrayNum` (see [lambda-eval.cpp:3037, 3203, 3288](../lambda/lambda-eval.cpp)).

In parallel, **LambdaJS carries a second, independent typed-array stack**: `JsTypedArray`, `JsArrayBuffer`, `JsDataView` ([js/js_typed_array.h](../lambda/js/js_typed_array.h)). It implements 11 element types (the 8 standard JS ones plus `Uint8ClampedArray`, `BigInt64Array`, `BigUint64Array`), with ArrayBuffer/DataView, `subarray`, `slice`, `set`, `fill`, `reverse`, length-tracking views over resizable buffers, and atomics. **This duplicates much of what `ArrayNum` should provide** and is the long-term consolidation target of Phase 5 in this proposal.

## Scope of this proposal

Five extensions to typed-array support, ordered by independence:

1. **N-dimensional arrays** — shape/strides on `ArrayNum`, broadcasting across shapes, axis-aware reductions, reshape/transpose.
2. **Read-only views** — `ArrayNum` instances whose storage aliases another buffer, with a base reference for liveness. Same `elem_type` as the source; distinguished by a flag bit, not a new `ELEM_*` enum.
3. **Vectorized scalar math** — make `sin`, `cos`, `sqrt`, `log`, `exp`, `clip`, `round`, `sign`, `floor`, `ceil`, and the rest of the unary numeric `SYSFUNC_*` set broadcast over `ArrayNum` (and `Range`/`Array`).
4. **Iteration sites** — extend pipe (`|>`), `where`, `for … in`, comprehensions, and predicate forms (`some`, `every`, `filter`) to iterate `ArrayNum` directly, returning typed arrays where the result type is recoverable.
5. **JS TypedArray unification (Phase 2)** — collapse `JsTypedArray` onto `ArrayNum`, replacing the parallel storage with a thin JS-facing wrapper that shares buffers and views with Lambda values.

Phases 1–4 are mutually mostly independent; phase 5 depends on phases 1–3.

---

## 1. N-Dimensional Arrays

### Goal

Promote `ArrayNum` from a flat numeric vector to an n-dimensional tensor while keeping the 1-D fast path unchanged.

### Storage

Two-track layout, gated by one flag bit in the `elem_type` byte:

```c
// reuse one of the lower-4-bit flag slots in the elem_type byte
#define ARRAY_NUM_FLAG_NDIM   0x01   // (currently unused bit in Container.flags)
#define ARRAY_NUM_FLAG_VIEW   0x02   // see §2
```

- **1-D path (no flag set)** — current `ArrayNum` layout unchanged. `length` is the only dimension. No allocation, dispatch, or per-element overhead added.
- **N-D path (`ARRAY_NUM_FLAG_NDIM` set)** — a side-table holds shape/strides/offset:

```c
struct ArrayNumShape {
    uint8_t  ndim;            // 2..8 typical; cap at 32
    uint8_t  is_c_contig:1;   // contiguous in row-major (no strides skip)
    uint8_t  is_f_contig:1;   // contiguous in column-major
    int64_t  offset;          // element offset into base data
    int64_t  shape[];         // length = ndim
    // followed in same allocation by: int64_t strides[ndim]  (in *elements*, not bytes)
};
```

**Storage location (confirmed):** the pointer to `ArrayNumShape` is stored in `ArrayNum.extra`. The existing semantic of `extra` (count of overflow items at end) is never meaningful for N-D arrays, so the slot is reused — no struct growth, no impact on the 1-D fast path. The N-D dispatch is gated by `Container.flags.is_view` for views, and by a separate `flags.is_ndim` bit (introduced in §2) for owned N-D arrays.

`length` for N-D arrays = `product(shape)`. `strides[i]` is the number of *elements* (not bytes) to skip when incrementing index `i`; bytes per element is already in `ELEM_TYPE_SIZE`.

### Construction

Two paths into N-D, **both implicit — no new `tensor(...)` builtin**:

1. **From a 1-D array via `reshape`** — `reshape(arr, [2, 3, 4])`. If the source is C-contiguous, the result is a view (§2). Otherwise it materializes.

2. **Nested literal syntax `[[1, 2], [3, 4]]` is auto-detected as N-D.** Two-stage detection:
   - **Static (AST builder)**: [lambda/build_ast.cpp](../lambda/build_ast.cpp) walks the literal. If every inner array has the same length and homogeneous numeric `elem_type`, the AST node carries an `is_ndim_candidate` flag and shape `(outer_len, inner_len, …)`. The transpiler emits a single N-D `ArrayNum` allocation + flat fill, not a nested `Array` of `ArrayNum`.
   - **Dynamic (runtime construction)**: When the structure isn't statically resolvable (e.g. `[row1, row2, row3]` where `row1` etc. are runtime-computed `ArrayNum`s), the array constructor at runtime checks: if all children are `ArrayNum` of the same `elem_type` and equal length, **promote to N-D `ArrayNum`** by allocating a contiguous buffer and copying rows in. Otherwise fall back to the current generic `Array of Items` behavior.

   The runtime check costs one pass over the children at construction time. For homogeneous numeric matrices, the payoff is large: contiguous storage, vectorized ops without per-row dispatch. For irregular content, the fall-back path is unchanged from today.

   Edge cases (handled by the fall-back to generic `Array`):
   - Mixed inner lengths (jagged) — stays heterogeneous.
   - Mixed `elem_type` across rows — stays heterogeneous (could promote with type-coercion, but that's surprising; keep it explicit).
   - Mixed inner types (some `ArrayNum`, some scalar) — stays heterogeneous.

   No syntactic distinction — `[[1, 2], [3, 4]]` is the only N-D literal. Whether it's actually stored as 2-D `ArrayNum` or as `Array of ArrayNum` depends on detection success; the user-visible value semantics are equivalent under indexing and iteration.

### Operations to extend

| Op | Current | N-D extension |
|---|---|---|
| Element access `arr[i]` | 1-D index | `arr[i, j, k]` walks strides; partial index returns a view (a row, slab, etc.) |
| Arithmetic `+ - * / % ^` | Length-matched vectors | **Broadcasting**: shapes align from the right; size-1 dims stretch; missing leading dims treated as 1. NumPy semantics. |
| Reductions `sum/avg/min/max/cumsum/cumprod` | Whole array | Add optional `axis` argument: `sum(arr, axis: 0)`. Default keeps whole-array behavior. |
| `reshape(arr, shape)` | — | Returns view if contiguous, else copy. Total element count must match. |
| `transpose(arr)` / `transpose(arr, perm)` | — | View with permuted shape/strides. |
| `flatten(arr)` | — | Copy as 1-D. `ravel(arr)` returns view if already contiguous. |
| `expand_dims`, `squeeze` | — | Cheap view ops on shape/strides. |
| `concat`, `stack`, `split` | — | New sysfuncs; require axis arg. |
| `dot` | 1-D · 1-D scalar | Extend to `matmul` for 2-D · 2-D, with the usual broadcasting prefix rules. |

### Broadcasting

Implementation lives in [lambda/lambda-vector.cpp](../lambda/lambda-vector.cpp). The current per-element loop is replaced by a stride-walking nested loop when either operand is N-D. For the common 1-D fast path (both operands 1-D, same length, contiguous), branch out to the existing tight loop — N-D adds zero overhead to the 1-D case.

A small `BroadcastIter` helper precomputes:
- Output shape (element-wise `max(a_shape[i], b_shape[i])` after right-alignment).
- Effective strides per operand, with `0` strides for size-1 dims that get stretched.

Out-of-bounds index or shape-mismatch raises an error at the boundary, not silently produces garbage.

### Type promotion across N-D

Same rules as 1-D (already in `lambda-vector.cpp`): result element type is the wider of the two operands, with int → float promotion when mixed. No changes needed; the per-element loop body is unchanged.

### Format / output

`format-json` and `format-yaml` (in [lambda/format/](../lambda/format/)) need to emit nested arrays for N-D. The current 1-D path prints `[1, 2, 3]`; N-D becomes nested per shape. Output `repr` should include shape for debug printing: `<arr_num f32 shape=(2,3) [[1, 2, 3], [4, 5, 6]]>`.

### Memory cost

- 1-D: zero overhead (no flag set, no shape allocation).
- N-D: one `malloc` of `sizeof(ArrayNumShape) + 2 * ndim * sizeof(int64_t)` per array. For a 4-D tensor, that's 8 + 64 = 72 bytes of metadata regardless of data size.

### GC note (forward reference)

The `ArrayNumShape*` stored in `extra` becomes reachable from the GC trace path when `ARRAY_NUM_FLAG_NDIM` is set. For pure N-D arrays (no view) the shape carries no GC roots — the trace function reads the flag and skips. For views, the shape carries `base`, which must be marked. Full details in §2 / GC interaction.

---

## 2. Read-Only Views

### Goal

`ArrayNum` instances whose `data`/`items`/`float_items` pointer aliases another `ArrayNum`'s buffer (or any externally owned numeric buffer). No copy on slice; lifetime tied to the source.

### Flag bits live in `Container.flags` upper nibble (now free)

A view is a generic concept that should apply to any container, not just `ArrayNum`. A future `Array` view (slice of a generic Item list) wants the same semantics: aliased storage, base-ref liveness, mutation rejection. So the view flag belongs in `Container.flags` so any container can carry it.

`Container` has been refactored to make the upper nibble of `flags` available: `map_kind` moved out of the bitfield into its own byte at offset 2, and the explicit padding bytes were added to lock down the layout. Current state at [lambda.h:524-537](../lambda/lambda.h#L524):

```c
struct Container {
    TypeId type_id;            // byte 0
    union {
        uint8_t flags;          // byte 1 — only lower nibble currently used
        struct {
            uint8_t is_content:1;       // bit 0
            uint8_t is_spreadable:1;    // bit 1
            uint8_t is_heap:1;          // bit 2
            uint8_t is_data_migrated:1; // bit 3
            // bits 4-7: FREE (was map_kind, now moved to its own byte)
        };
    };
    uint8_t map_kind;          // byte 2 — per-type discriminator (map_kind / elem_type / unused)
    uint8_t padding[5];        // bytes 3-7 — explicit padding to 8-byte alignment
};
// first 8-byte-aligned field in derived types lives at offset 8 — unchanged
```

Per-derived-type byte 2:
- `Map` / `Object` / `Element`: `map_kind` (MapKind enum)
- `ArrayNum`: `elem_type` (ArrayNumElemType enum)
- `Range` / `List`: unused (no per-type discriminator needed)

The view, n-dim, and pin flags are **orthogonal to container type** (any of these properties could apply to any container), so they belong in the type-agnostic `flags` byte, not in byte 2. With the upper nibble freed, no new byte is needed — extend the existing bitfield:

```c
struct Container {
    TypeId type_id;
    union {
        uint8_t flags;
        struct {
            // — lifecycle / allocation flags (lower nibble) —
            uint8_t is_content:1;       // bit 0
            uint8_t is_spreadable:1;    // bit 1
            uint8_t is_heap:1;          // bit 2
            uint8_t is_data_migrated:1; // bit 3
            // — layout / storage flags (upper nibble, freed by map_kind move) —
            uint8_t is_ndim:1;          // bit 4  shape side-table in `extra`
            uint8_t is_view:1;          // bit 5  aliases another container's storage (implies is_ndim)
            uint8_t is_pinned:1;        // bit 6  has live views; data buffer must not be relocated by compactor
            uint8_t flags_reserved:1;   // bit 7  reserved for future use
        };
    };
    uint8_t map_kind;
    uint8_t padding[5];
};
```

**Zero new fields, zero ABI impact.** Bit reads stay in the same byte; hot-path checks become `if (flags & VIEW_MASK)` against existing memory. The GC's hardcoded `p[1]` read covers all four new bits.

#### Where is the padding I claimed?

The 6 bytes of implicit padding I referred to earlier (between byte 1 and the first 8-byte-aligned field at offset 8) are now **explicit** in the refactored layout. The user's refactor materialized them as `uint8_t map_kind` at byte 2 + `uint8_t padding[5]` at bytes 3-7. The total header size stays 8 bytes; the first field at offset 8 (pointer or int64_t in derived structs) is unchanged. The GC's hardcoded byte-offset reads at [gc_heap.c:872-875](../lib/gc/gc_heap.c#L872) keep working:

```c
void* items_ptr = *(void**)(p + 8);
int64_t length  = *(int64_t*)(p + 16);
int64_t extra   = *(int64_t*)(p + 24);
int64_t capacity= *(int64_t*)(p + 32);
```

The refactor is already landed (per your note "i've update Container and derived types, to make padding explicit"). Adding the new flag bits is a bitfield-only change — no struct size change, no offset change, no audit of `sizeof()` consumers needed.

### Per-element-type view granularity

Within `ArrayNum`, the view flag is orthogonal to `elem_type` — a view of any element kind (int8 / float32 / …) uses the same `is_view` flag in `Container.flags`, same shape side-table, same base-ref machinery. No `ELEM_VIEW_*` variants needed.

### What changes in code

- **Op-on-data paths** (arithmetic, reductions, indexing): unchanged — they only care about `elem_type` and the data pointer.
- **Branches on view-ness** — the only paths that need to check `is_view`:
  - **Free / GC reclamation**: view doesn't own its buffer; must not `free(data)`, must drop ref on base.
  - **Mutation attempts** (`array_*_set`, `fill`, in-place sort): raise `ItemError`.
  - **Resize / capacity**: meaningless for views; rejected.

### Storage

Per §1, view metadata lives in the shape side-table, which is stored in the `extra` field of `ArrayNum`. A view is necessarily n-D — even when `ndim=1`, it needs to carry the base pointer and offset. So views always allocate a shape struct (set the n-d flag in `elem_type` byte), and set `Container.is_view = 1`:

```c
struct ArrayNumShape {
    uint8_t  ndim;
    uint8_t  is_c_contig:1;
    uint8_t  is_f_contig:1;
    int64_t  offset;            // element offset within base->data (only meaningful for views)
    Container* base;            // NULL for owned arrays; set when the parent Container has is_view=1
    int64_t  shape[];
    // strides[] follow
};
```

`base` is typed as `Container*` rather than `ArrayNum*` so the same shape struct can describe views of generic `Array` in the future (Phase 2d). `base` participates in GC tracing (see GC interaction below) and in `Container.ref_cnt`.

### Construction

Views appear from:

- **`slice(arr, start, end)`** — currently copies (`SYSFUNC_SLICE`). With contiguous source: returns a view. Add `slice(arr, start, end, copy: true)` for explicit copy.
- **Range-indexed slicing `arr[start to end]`** — reuses Lambda's existing `to` range syntax ([grammar.js:340](../lambda/tree-sitter-lambda/grammar.js#L340), `range: $ => seq($._expr, 'to', $._expr)`). When `arr[range]` is evaluated and `arr` is `ArrayNum` (or `Array` in Phase 2d), the indexer returns a view spanning the range's elements. No new syntax required.
- **Reshape on contiguous source** (§1).
- **Transpose** (§1).
- **`view(buffer, elem_type, shape)`** — explicit view over an externally-owned buffer (used by Phase 5 for `JsArrayBuffer`).

**Strided / step slicing — design only, no implementation in this proposal.** When needed in the future, it will be exposed as a sysfunc:

```lambda
slice(arr, range, step)   // e.g. slice(arr, 0 to 10, 2) → every 2nd element, indices 0,2,4,6,8,10
```

The implementation reuses the same view machinery (a view with non-unit stride is just a stride value > element-size in the shape side-table; the existing `is_c_contig` bit becomes false). No grammar change. This is captured as a forward-compatible design choice — Phase 2 does not ship it, but the view machinery is built so a future step-aware `slice` requires only sysfunc-level work, not core changes.

### Mutability — read-only only in this phase

Views are **read-only**. Period — no COW, no opt-in mutable form in Phase 2. Attempted writes through a view raise `ItemError` with a clear message. If the user wants a writable copy: `copy(view)`.

Mutable views and mutable typed arrays in general are explicitly **deferred to Phase 2d** (the final phase of this proposal's roadmap). Rationale:

- Lambda's functional core treats values as immutable; mutation primitives are scoped to specific contexts (markup editing, `MarkEditor`).
- Read-only views cover the dominant use cases: slicing, reshape, transpose, JS interop without copy.
- A clean read-only-first design lets us validate the GC/liveness machinery (see GC interaction below) before adding mutation hazards.
- When mutable views land, they reuse all the view machinery — just lift the write-rejection check.

### Liveness — interaction with parser-produced typed arrays (`is_heap` audit)

A view holds a ref on its base array; as long as any view exists, the base buffer is kept alive. This matches NumPy's `arr.base` semantics. But this only works if the base is **GC-managed**, which is exactly what `Container.is_heap` distinguishes.

#### Existing `is_heap` semantics — audit results

Eight total references in the tree. Two **setters**, three **readers**, three **declarations/comments**:

**Setters** — `is_heap = 1` is *only* applied by the GC-backed allocators:
- [lambda-mem.cpp:382](../lambda/lambda-mem.cpp#L382) in `heap_calloc`: any container allocated via `heap_calloc` gets `is_heap = 1` (skipping `LMD_TYPE_FUNC` and `LMD_TYPE_TYPE` which have different byte-1 layouts).
- [lambda-mem.cpp:400](../lambda/lambda-mem.cpp#L400) in `heap_calloc_class`: same logic for the JIT bump-pointer fast path.
- [transpile-mir.cpp:4357](../lambda/transpile-mir.cpp#L4357): MIR-emitted JIT code that newly heap-allocates a container.

**Readers** — all in [lambda-eval.cpp](../lambda/lambda-eval.cpp) `map_rebuild_for_type_change`:
- Line 4919: `bool use_pool = !container->is_heap;` — markup containers (input-arena) get rebuilt via runtime pool (not GC zone), to avoid corrupting input pool memory.
- Line 5036: `if (container->is_heap) { /* old_data in GC data zone — reclaimed by GC */ }` — vs. the `is_data_migrated` / pool path for non-heap.
- The header comment at [line 4844](../lambda/lambda-eval.cpp#L4844) spells out the rule: *"For markup containers (`!is_heap`), uses runtime pool instead of calloc/free to avoid corrupting input pool memory."*

**Parser allocation pattern** — parsers never call `heap_calloc`. They go through `arena_alloc` against the input arena. Confirmed at [mark_builder.cpp:861](../lambda/mark_builder.cpp#L861) for `ArrayNum` specifically:

```cpp
ArrayNum* new_arr = (ArrayNum*)arena_alloc(arena_, size);
```

So **`is_heap` defaults to 0 for parser-produced `ArrayNum`** (zero-initialized via the arena's calloc semantics; never explicitly set). This matches your direction #2 — no code change needed to enforce it.

#### View implications

Because parser arrays live in an input arena that dies independently of the GC, a view onto them is structurally dangerous: when the arena is dropped, the view's `data` pointer dangles. The view's `base` ref (which only participates in GC tracing) does not extend the arena's lifetime.

**Decision: views on `is_heap = 0` bases are rejected.** The view constructor checks `base->is_heap`; if zero, it raises `ItemError` with a message like `"cannot view arena-backed array; copy() first"`. Rationale:

- **Auto-copy on view-create** would silently turn a `view()` into a `copy()`, hiding cost and breaking the "view shares storage" mental model.
- **Promote-then-view** (copy to heap, swap base, redirect arena reference) is correct but complex, and changes a parser-output value's identity in confusing ways.
- **Explicit rejection** surfaces the arena boundary: user writes `slice(copy(parsed_arr), 1 to 5)` once they know they want a view. Common pipeline patterns that consume the parsed array eagerly (`sum(parsed_arr)`, `parsed_arr |> map(...)`) never trip this path because they iterate in place, no view created.

This decision keeps `is_heap = 0` for parsers (your direction #2) without requiring the view machinery to grapple with arena lifecycles.

For external buffers (deferred §5: `JsArrayBuffer`), the base ref will point to a heap-allocated wrapper that owns the buffer — that wrapper has `is_heap = 1`, so views work normally.

### GC interaction — this is the load-bearing change

Views introduce a dependency the current GC does not model. Today, `LMD_TYPE_ARRAY_NUM_` is in the *"no outgoing Item pointers — nothing to trace"* fast path in [lib/gc/gc_heap.c:856](../lib/gc/gc_heap.c#L856), and compaction [gc_heap.c:1187](../lib/gc/gc_heap.c#L1187) freely relocates the `items` buffer because it assumes nothing else aliases it. Both assumptions break for views. Three things must change in [lib/gc/gc_heap.c](../lib/gc/gc_heap.c) and [lambda/lambda-mem.cpp](../lambda/lambda-mem.cpp):

#### 5a. Tracing — view marks its base

`LMD_TYPE_ARRAY_NUM_` moves out of the no-trace list. The trace function reads `Container.flags` (the existing byte 1) and, when `is_view` (bit 5) is set, dereferences `extra` to find the `ArrayNumShape`, then marks `shape->base` as a GC root:

```c
#define CONTAINER_FLAG_IS_VIEW   (1u << 5)   // bit 5 of Container.flags

case LMD_TYPE_ARRAY_NUM_: {
    uint8_t* p = (uint8_t*)obj;
    uint8_t flags = p[1];                                    // existing flags byte
    if (flags & CONTAINER_FLAG_IS_VIEW) {
        ArrayNumShape* shape = *(ArrayNumShape**)(p + 24);   // extra slot
        if (shape && shape->base) {
            gc_mark_object(gc, (gc_header_t*)shape->base);
        }
    }
    break;
}
```

The 1-D owned path and non-view N-D arrays stay in the no-trace fast path (`is_view` bit not set). When a future generic-`Array` view lands (Phase 2d), `LMD_TYPE_ARRAY_` similarly reads `flags & CONTAINER_FLAG_IS_VIEW` and traces `shape->base` before doing its existing Item-array trace.

#### 5b. Finalization — view never frees its aliased buffer

Today an `ArrayNum` finalizer (implicit through the GC's free of `items`/`data`/`float_items`) blindly frees the buffer. For views, the buffer belongs to `shape->base`, not to the view. Finalization branches on `is_view`:

- **Owned array**: free `data`/`items` as today, then free `shape` (if N-D).
- **View**: free *only* the `shape` struct. The data pointer is borrowed; freeing it would corrupt the base. Decrement `shape->base->ref_cnt` (or rely on tracing GC to reclaim it once unreachable, depending on which side of the hybrid model owns reclamation).

The destructor pattern already exists for `JsArrayBuffer` ([lambda-mem.cpp:36](../lambda/lambda-mem.cpp#L36)) via `gc_native_seen_t` to handle shared natives — reuse the same idea: the shape struct goes through `gc_native_seen_seen_or_add` so finalization is idempotent even if the base is reached multiple times.

#### 5c. Compaction — base buffers with live views must not move

This is the hard one. Lambda's GC compaction ([gc_heap.c:1187-1202](../lib/gc/gc_heap.c#L1187)) relocates the `items` buffer from the nursery to the tenured data zone. If a view's `data` pointer points into that buffer, the view's pointer is silently dangling after the copy.

Three options:

| Option | Mechanism | Cost | Recommendation |
|---|---|---|---|
| **(a) Pin** | Mark base arrays with live views as "pinned"; compaction skips them. | One bit on the base array; tiny fragmentation hit. | **Recommended.** Simplest; matches the rarity of long-lived view chains. |
| **(b) Interior offsets** | View stores `{base, byte_offset}`, computes data pointer per access via `base->data + byte_offset`. | One add per element access in tight loops; defeats the cached-pointer benefit. | Reject — kills hot-loop perf. |
| **(c) Patch views** | Compactor maintains a `base → views[]` back-reference and patches each view's `data` after moving. | Extra pointer storage on every viewed-base; complex during concurrent ops. | Reject — too much machinery for the use case. |

The pin bit lives in `Container.flags` bit 6 (`is_pinned`), set on the **base** when a view is first created. Never cleared — clearing would require view-count tracking, which adds machinery for no real benefit; pinned arrays simply stop being relocated, which is fine for the lifetime of the process.

A side effect worth calling out: while the data buffer is pinned, the `ArrayNum` *container header* can still move freely. Only the `data`/`items`/`float_items` pointer's *target* is pinned. The pin bit is read by the compaction path, not by tracing.

Phase 2d generic-Array views work identically: a viewed `Array` sets `is_pinned`, and the compactor skips relocation of its `items` buffer.

#### 5d. The shape side-table itself

`ArrayNumShape` is heap-allocated metadata, separate from the GC's tracked-object heap. Two choices for its lifecycle:

- **Pool-allocated, owned by the ArrayNum**: freed in the ArrayNum finalizer. Simple. Recommended.
- **GC-allocated as a `gc_native` object**: traceable, reuses existing `gc_native_seen_t` dedup. Heavier; only worth it if shapes get shared between arrays (they generally won't — reshape produces a fresh shape).

Recommendation: pool-allocate via `pool_calloc`, free with the `ArrayNum`. The base ref inside the shape is the only GC-relevant pointer, and it's reached through the trace path described in §5a.

#### 5e. Existing latent bug worth fixing in the same pass

The current compactor at [gc_heap.c:1192](../lib/gc/gc_heap.c#L1192) computes `size = capacity * 8` — this assumes 8-byte elements. It's been wrong since `ELEM_INT8`/`ELEM_UINT8`/`ELEM_INT16`/`ELEM_FLOAT16` etc. landed in Phase 1 — a compact array gets over-copied by 2-8× during compaction, reading off the end of the source buffer. It hasn't bitten because compaction targets are rare and the over-read usually hits initialized neighboring bytes. While touching the compactor for views, fix this by computing `size = capacity * ELEM_TYPE_SIZE[elem_type >> 4]`.

#### 5f. Phase 5 (JS unification) implication

After unification, `JsArrayBuffer`'s `data` becomes the base storage that an `ArrayNum` view aliases. The two existing destructor paths in [lambda-mem.cpp](../lambda/lambda-mem.cpp) — `gc_finalize_arraybuffer` and `gc_finalize_typed_array` — collapse into the unified ArrayNum finalization above, with the shared-buffer wrapper acting as the "base". `JsArrayBuffer.detached = true` becomes equivalent to nulling the shared buffer's data pointer; view-side accesses must check and raise `TypeError` per JS semantics. The existing `gc_native_seen_t` dedup machinery already handles the multi-ref case.

---

## 3. Vectorized Scalar Math

### Goal

Every unary numeric sysfunc that today takes a single `Item` and returns one should also accept an `ArrayNum` (or `Range`, or homogeneous `Array`) and return a new `ArrayNum` with the same `elem_type` (or promoted appropriately).

### Functions in scope

From [lambda/lambda.h](../lambda/lambda.h):

| Already scalar | Vectorize to ArrayNum |
|---|---|
| `fn_abs` | ✓ partial — already handles arrays per existing code; verify all element kinds |
| `fn_sign` | ✓ |
| `fn_round`, `fn_floor`, `fn_ceil`, `fn_trunc` | ✓ |
| `fn_sqrt`, `fn_cbrt` | ✓ → result always `ELEM_FLOAT` for int input |
| `fn_log`, `fn_log2`, `fn_log10`, `fn_log1p` | ✓ → float result |
| `fn_exp`, `fn_exp2`, `fn_expm1` | ✓ → float result |
| `fn_sin`, `fn_cos`, `fn_tan`, `fn_asin`, `fn_acos`, `fn_atan` | ✓ → float result |
| `fn_sinh`, `fn_cosh`, `fn_tanh`, `fn_asinh`, `fn_acosh`, `fn_atanh` | ✓ → float result |
| `fn_neg` | ✓ — preserves elem_type (rejected for unsigned types) |

Add a new sysfunc:

- **`clip(arr, lo, hi)`** — element-wise `max(lo, min(hi, x))`. Takes `lo`/`hi` as scalars (broadcast) or arrays (broadcast per §1).

Binary float-typed: `pow`, `atan2`, `hypot`, `fmod`, `copysign` — already broadcast through `vec_pow` etc. for some; audit and fill gaps.

### Implementation pattern

A single helper in [lambda/lambda-vector.cpp](../lambda/lambda-vector.cpp) covers the whole family:

```cpp
typedef double (*UnaryF64Fn)(double);
typedef int64_t (*UnaryI64Fn)(int64_t);

// dispatch one scalar fn over an ArrayNum, producing a new ArrayNum
Item vec_unary_f64(ArrayNum* in, UnaryF64Fn f);   // result always ELEM_FLOAT
Item vec_unary_same(ArrayNum* in, UnaryI64Fn fi, UnaryF64Fn fd); // preserves elem_type
```

Each `fn_*` adds a one-line dispatch at the top:

```cpp
Item fn_sin(Item a) {
    if (get_type_id(a) == LMD_TYPE_ARRAY_NUM) return vec_unary_f64(a.array_num, sin);
    if (get_type_id(a) == LMD_TYPE_RANGE)     return vec_unary_f64_range(a.range, sin);
    // ... existing scalar path ...
}
```

Total surface: ~30 sysfuncs × 2 lines of dispatch + 2 helper functions. The hot loops are inside the helpers.

### Result element type

- **Always-float ops** (`sin`, `cos`, `log`, `exp`, `sqrt`, …): produce `ELEM_FLOAT`. Special case: `ELEM_FLOAT32` input could produce `ELEM_FLOAT32` output if the caller wants to stay in single-precision; default to `ELEM_FLOAT` (double) for predictability.
- **Type-preserving ops** (`abs`, `neg`, `sign`, `floor`, `ceil`, `round`, `trunc`): preserve `elem_type`. `abs` on signed types must check for `INT_MIN` and either error or widen.
- **Mixed-type binary** (`pow`, `atan2`): apply the same promotion rules as `vec_add` etc.

### SIMD / autovectorization

Out of scope for this proposal but the loops are SIMD-friendly. A follow-up can specialize hot paths (f32, f64) with `#pragma omp simd` or explicit intrinsics. Even without SIMD, calling `sin` once per element in a tight loop beats the current "no array support, must `for x in arr` and call scalar `sin(x)`" by 5-10× from removed Item boxing alone.

---

## 4. Iteration Sites — Pipe, Where, For, Comprehensions

### Current state

- `for x in arr` already works (eval.cpp dispatches on `LMD_TYPE_ARRAY_NUM`).
- `in` / `index_of` already iterate `ArrayNum`.
- `where` (filter), `some`, `every`, `map`, `reduce`: dispatch needs auditing — `SYSFUNC_REDUCE` exists; mapping a function over `ArrayNum` likely falls through to generic `Array` path, which boxes every element.

### Goal

Make pipe (`arr |> filter(p) |> map(f) |> sum`) execute on `ArrayNum` *without boxing intermediate results back to a generic `Array`*. The whole pipeline should stay in typed-array land when possible.

### Specific changes

1. **`map(arr, fn)` returns `ArrayNum`** when:
   - `arr` is `ArrayNum`.
   - `fn` is a recognized unary numeric sysfunc (see §3) — direct call to the vectorized form.
   - `fn` is a user-defined lambda whose return type the type inferencer narrowed to numeric.
   - Fallback: result is generic `Array`.

2. **`filter(arr, p)` / `where`** returns `ArrayNum` of the same `elem_type` as input. Iterate, evaluate predicate per element, build into a typed-array builder that pre-grows in chunks. Predicate result must be boolean; on numeric arrays, `arr |> filter(x -> x > 0)` is common enough to optimize.

3. **`reduce(arr, fn, init)`** — already exists; verify it iterates `ArrayNum` natively without boxing.

4. **List comprehensions** (`[x * 2 for x in arr]`): the transpiler ([lambda/transpile.cpp](../lambda/transpile.cpp)) should detect when:
   - Source is `ArrayNum`.
   - Body produces a numeric expression with known element type.
   - Then emit a typed-array build path instead of a generic `Array` push loop.

5. **Pipe (`|>`) fusion** — the simplest level: each pipe stage that takes an `ArrayNum` and produces an `ArrayNum` runs as one tight loop. Stage fusion across pipes (e.g. `arr |> map(f) |> filter(p)` becoming one pass) is a future optimization; the immediate win is just not boxing between stages.

6. **Boolean mask indexing** (new): `arr[mask]` where `mask` is a `bool[]`. This requires a new element kind `ELEM_BOOL` (14th in the enum, 1 byte per element). Distinct from `ELEM_UINT8` because:
   - Reductions like `any(mask)` / `all(mask)` want bool-specific semantics.
   - Output formatters print `true`/`false`, not `0`/`1`.
   - Coercion rules differ (any non-zero source → `true`, not just `1`).

   Mask indexing returns a new `ArrayNum` (not a view — non-contiguous selection can't alias). Adding `ELEM_BOOL` is straightforward — it slots into existing dispatch the same way the compact integer types did.

### Iteration protocol

Internally, add a tiny iterator helper for `ArrayNum`:

```cpp
struct ArrayNumIter {
    ArrayNum* arr;
    int64_t   index;
    ArrayNumElemType elem_type;  // cached for hot loop
};
Item array_num_iter_next(ArrayNumIter* it);  // returns ItemError on exhaustion
```

Used by `filter`, `some`, `every`, `for-in`, comprehensions. The cached `elem_type` avoids re-dispatching per element.

For N-D arrays (§1), iteration semantics: **`for x in nd_arr` iterates the leading axis**, so each `x` is an `(ndim-1)`-D view (matches NumPy). Scalar iteration over an N-D array requires `for x in flatten(arr)`. For 1-D, `for x in arr` continues to yield scalars as today.

---

## 5. JS TypedArray Unification — Design Kept, Implementation Deferred

**Status: design preserved, implementation explicitly deferred to a later release.**

Per your direction, the unification of `JsTypedArray`/`JsArrayBuffer`/`JsDataView` onto `ArrayNum` is no longer part of this proposal's deliverables. The design below remains as the **target architecture** so that Phase 2a–2c choices stay JS-compatible. Specific constraints carried forward:

- **View semantics must support views over externally-owned buffers**, not just other `ArrayNum`s. The `base` field is typed `Container*` (not `ArrayNum*`) precisely so a future JS-buffer wrapper can serve as base.
- **Read-only-by-default** matches JS semantics for views into detached buffers and aligns with the eventual `Uint8Array.prototype.slice()` return-a-copy / `subarray()` return-a-view distinction.
- **Element coercion lives at API boundaries, not in ArrayNum ops.** Lambda keeps strict numeric semantics; the JS wrapper applies truncation / clamping / NaN-coercion before delegating writes.
- **`ELEM_UINT8_CLAMPED`** is *not* added in this proposal but reserved as element kind #15 (or kind #14 if `ELEM_BOOL` doesn't ship). The flag-only alternative (re-using `ELEM_UINT8` with a `clamped` flag) is rejected because the arithmetic semantics genuinely differ and a per-op clamp branch hurts the hot loop.
- **`gc_native_seen_t` dedup pattern** already used by `gc_finalize_arraybuffer`/`gc_finalize_typed_array` is reusable for shared-buffer finalization.

The duplication table, element-type map, target architecture diagram, migration order, and compatibility/risk discussion below remain as **the spec for future work**.

### Current duplication (still applies)

| Concern | Lambda `ArrayNum` | LambdaJS `JsTypedArray` |
|---|---|---|
| Storage | Owned buffer in `data`/`items`/`float_items` | Owned buffer or window into `JsArrayBuffer` |
| Element types | 13 (`ELEM_INT…ELEM_FLOAT64`) | 11 (`JS_TYPED_INT8`…`BIGUINT64`) — almost a subset |
| Views | None today; this proposal adds them | `subarray`, `slice`, length-tracking views |
| Backing buffer | Implicit (owned) | Explicit `JsArrayBuffer` with detach/resize/transfer |
| Arithmetic | Element-wise `+ - * / %` via `vec_*` | None — JS spec doesn't define array arithmetic |
| Iteration | `for-in` etc. | JS iterator protocol via property access |
| Format | `format-json` etc. | JS `toString` / `JSON.stringify` |

The element-type mappings are nearly 1:1:

| JS | ArrayNum |
|---|---|
| `JS_TYPED_INT8` | `ELEM_INT8` |
| `JS_TYPED_UINT8` | `ELEM_UINT8` |
| `JS_TYPED_INT16` | `ELEM_INT16` |
| `JS_TYPED_UINT16` | `ELEM_UINT16` |
| `JS_TYPED_INT32` | `ELEM_INT32` |
| `JS_TYPED_UINT32` | `ELEM_UINT32` |
| `JS_TYPED_FLOAT32` | `ELEM_FLOAT32` |
| `JS_TYPED_FLOAT64` | `ELEM_FLOAT64` / `ELEM_FLOAT` |
| `JS_TYPED_BIGINT64` | `ELEM_INT64` |
| `JS_TYPED_BIGUINT64` | `ELEM_UINT64` |
| `JS_TYPED_UINT8_CLAMPED` | **gap** — needs a new element kind or a flag |

Only `Uint8ClampedArray` lacks an `ArrayNum` analog. Add `ELEM_UINT8_CLAMPED` (14th element kind) or a clamped-arithmetic flag bit.

### Target architecture

```
┌─────────────────────────────────────────────────────────────┐
│                  JS API surface (Js… types)                 │
│   Uint8Array.from(…), .subarray(), .set(), DataView, etc.   │
└─────────────────────────────────────────────────────────────┘
                            │  thin wrapper
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  ArrayNum  (this proposal §§1-4: shape, views, arithmetic)  │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│   Numeric buffer (raw bytes + length + ref count)           │
│   reused by both Lambda values and JsArrayBuffer            │
└─────────────────────────────────────────────────────────────┘
```

Concretely:

- `JsArrayBuffer` becomes a thin wrapper holding a `ref` to a shared buffer object plus JS-specific state (`detached`, `is_shared`, `resizable`, `max_byte_length`).
- `JsTypedArray` becomes a wrapper holding a ref to an `ArrayNum` view (with `ARRAY_NUM_FLAG_VIEW` set) plus JS-specific identity-preservation fields (`buffer_item`, `is_buffer`, `length_tracking`).
- Operations that exist in both stacks call into the `ArrayNum` impl: `set` → typed-array set, `slice` → ArrayNum view, `fill` → existing `fn_fill`, `reverse` → in-place op on `ArrayNum` (writable variant).

### Migration order

1. **Add `ELEM_UINT8_CLAMPED`** (or flag) — `ArrayNum` now covers every JS element type.
2. **Build `ArrayNum` view machinery (§2)** — JS `subarray`/`slice` will rely on this.
3. **Refactor `JsArrayBuffer` to wrap a shared buffer** — same buffer can underlie a Lambda `ArrayNum` and a JS `Uint8Array` simultaneously.
4. **Replace `JsTypedArray.data` with `ArrayNum*`** — JS read/write ops delegate. The JS layer keeps its own struct for identity/`buffer_item`/length-tracking, but storage moves.
5. **Delete duplicated paths**: `js_typed_array_raw_*`, `js_typed_array_set_from`, `js_typed_array_fill` — replaced by `ArrayNum` equivalents with JS-compatible coercion rules at the boundary.
6. **Cross-language interop**: `Lambda → JS` passes an `ArrayNum` to JS code as a `Uint8Array`/`Float32Array` view *with zero copy*. `JS → Lambda` passes a `JsTypedArray` as `ArrayNum` likewise.

### Compatibility & risks

- JS coercion rules (e.g. assigning a value out of range gets truncated for unsigned types, clamped for `Uint8Clamped`, set to NaN for floats from non-numeric) must be applied at the JS-API boundary, not inside `ArrayNum` ops, which keep Lambda's strict semantics.
- Detached ArrayBuffer semantics: a detached `JsArrayBuffer` must invalidate all dependent `ArrayNum` views. Implement by having the shared buffer drop its data pointer; view ops check for null and raise.
- BigInt vs int64: JS `BigInt64Array` exposes `BigInt` values, not numbers. The wrapper layer converts; storage is identical.
- SharedArrayBuffer + Atomics: keep as JS-only concerns layered on the shared-buffer primitive.

---

## Implementation order (recommended)

```
Phase 2a — independent, ship in any order:
   §3 (vectorize scalar math)       — ~1 week, low risk
   §4 (iteration sites)             — ~1 week, mostly transpiler work
   ELEM_BOOL                         — small, can land alongside §3 or §4

Phase 2b — sequential:
   §2 (views) + GC upgrade           — ~3 weeks; GC tracer/finalizer/compactor
                                       all need extending (see §2 GC interaction).
                                       New flag bits go into Container.flags upper
                                       nibble (already freed by the recent map_kind
                                       move); no struct size change. The latent
                                       compact-elem compaction bug gets fixed in
                                       the same pass.
   §1 (n-d)                          — ~3 weeks, broadcasting + format/output;
                                       reuses the shape side-table slot §2 added.

Phase 2c — optional polish:
   ELEM_BOOL mask-indexing UX        — once §2 views are stable

Phase 2d — deferred to a future release (NOT in this proposal):
   Mutable views / mutable typed arrays
   Generic `Array` views (extending §2 view machinery to LMD_TYPE_ARRAY)
   JS TypedArray unification (§5 design preserved but not implemented now)
```

Each phase keeps the 1-D, owned-buffer fast path identical to today, so existing benchmarks should not regress.

## Open questions

All open questions from prior rounds have been resolved. Implementation can proceed against the spec as written.

## Resolved (per your decisions)

- ✅ `ArrayNumShape*` stored in `ArrayNum.extra` (your #1).
- ✅ View flag lives in `Container.flags` upper nibble (`is_view` bit 5), applies to any container — Array gets views in Phase 2d (your #2 of first round). After the `map_kind` move, the upper nibble of `flags` is free for `is_ndim`/`is_view`/`is_pinned` — no new byte needed.
- ✅ JS unification design preserved as future spec; implementation deferred (your #3 of first round). Phase 2 design constraints listed in §5.
- ✅ `is_heap` flag remains meaningful for ArrayNum; parser-produced typed arrays leave it at 0 (your #4 of first round, confirmed via audit in §2 Liveness — parsers use `arena_alloc`, never `heap_calloc`). Views on `is_heap = 0` bases are rejected with explicit error.
- ✅ Mutable views/arrays deferred to Phase 2d (your #5 of first round).
- ✅ `ELEM_BOOL` added as a new 14th element kind (your #6 of first round).
- ✅ N-D `for x in arr` yields leading-axis slices, NumPy-style (your #7 of first round).
- ✅ Slicing syntax: `arr[start to end]` using existing range grammar (your #8 of first round).
- ✅ Step in slicing: design only as `slice(arr, range, step)` sysfunc, **no implementation in this proposal** (your #1 of second round).
- ✅ N-D construction: auto-detect at AST builder (static) and runtime (dynamic). **No `tensor(...)` builtin** (your #3 of second round).
- ✅ No `flags2` byte. The `Container` refactor moved `map_kind` to byte 2 and made padding explicit (`uint8_t padding[5]` at bytes 3-7), freeing the upper nibble of the existing `flags` byte for `is_ndim`/`is_view`/`is_pinned` + 1 reserved bit (your #4 of second round + final clarification).
- ✅ N-D promotion at runtime: **eager**. Homogeneous numeric children → promoted to N-D `ArrayNum` at construction time, single contiguous memcpy. No lazy promote-on-first-op (your #1 of third round).
- ✅ Pin clearing: **deferred to future**. Once set on a base by first view creation, `is_pinned` is never cleared in this proposal. Pinned-fragmentation as a real issue is a future concern; if it bites, add view-count tracking then (your #2 of third round).

## Non-goals

- SIMD intrinsics — deferred to a separate perf-tuning pass.
- Lazy / fused evaluation graphs (à la NumPy `numexpr` or JAX). Stay eager.
- GPU / accelerator interop.
- Complex numbers (`ELEM_COMPLEX64/128`) — would compose cleanly with this design but is its own proposal.
- Sparse arrays.
- Memory-mapped file backing — falls out almost free once views over external buffers exist (§5 enables it), but no first-class API in this proposal.
- Mutable views (Phase 2d).
- JS TypedArray runtime unification (Phase 2d; design preserved in §5).

## Success criteria

1. Every existing test in `test/lambda/typed_array_*.ls`, `compact_typed_arrays.ls`, `array_float.ls` continues to pass unchanged.
2. New tests:
   - N-D construction, reshape, transpose, broadcasting, axis reductions.
   - View aliasing: writes to source visible through view; freeing source from under live view is impossible.
   - Vectorized `sin(arr)`, `clip(arr, 0, 1)`, etc. produce element-wise results.
   - `arr |> filter(x -> x > 0) |> sum` runs without intermediate `Array` boxing (verify via debug counter).
   - JS `Float32Array` shares storage with Lambda `ArrayNum f32` view (Phase 5).
3. 1-D arithmetic benchmark (`a + b` for length-1M `f64[]`) does not regress measurably vs. Phase 1 baseline.
4. Memory: a 1-D owned `ArrayNum` allocation footprint is unchanged from today (new flag bits go into the existing `flags` byte; the shape side-table is only allocated for N-D or view arrays).
5. Container ABI: `sizeof(Container)`, `sizeof(Array)`, `sizeof(Map)`, `sizeof(Element)`, `sizeof(ArrayNum)` are unchanged by this proposal (new flags occupy already-free bits in `flags`; the recent `map_kind`-move refactor has its own static_asserts).
