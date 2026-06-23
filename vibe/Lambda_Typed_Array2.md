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

The pointer to `ArrayNumShape` is stored in the `extra` field (currently `count of extra items at end`, which is never meaningful for N-D arrays — so reuse, not extend). Alternative: add a `void* meta` field — adds 8 bytes to every `ArrayNum`. **Recommendation**: reuse `extra` since the semantic of "extra items" doesn't apply to N-D and the 1-D fast path can keep treating it as a count.

`length` for N-D arrays = `product(shape)`. `strides[i]` is the number of *elements* (not bytes) to skip when incrementing index `i`; bytes per element is already in `ELEM_TYPE_SIZE`.

### Construction

Two paths into N-D:

1. **From a 1-D array via `reshape`** — `reshape(arr, [2, 3, 4])`. If the source is C-contiguous, the result is a view (§2). Otherwise it materializes.
2. **From nested literal syntax** — `[[1, 2], [3, 4]]`. Currently this constructs a generic `Array` of `ArrayNum`. The build_ast pass (in [lambda/build_ast.cpp](../lambda/build_ast.cpp)) should detect when every inner array has the same length and `elem_type` and emit a single N-D `ArrayNum` instead. A new sysfunc `tensor([[…], […]])` provides an explicit form if literal detection is too fragile.

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

### Flag vs. new ELEM enum — recommendation

**Use a flag bit, not a new `ELEM_*` enum.** Reasoning:

| Option | Pros | Cons |
|---|---|---|
| New `ELEM_*` variants (e.g. `ELEM_VIEW_FLOAT32`) | Self-describing in one byte | Doubles the enum (13 → 26), every dispatch site must handle both forms, view-ness is orthogonal to element kind so this conflates two axes |
| Flag bit in the elem_type byte's low nibble | Element kind stays one value; view-ness is independent; one bit covers all 13 element types | Adds one bit-check to ops that care about ownership (free, resize); op-on-data code paths see no change |

The op-on-data code paths (arithmetic, reductions, indexing) only care about `elem_type` and the data pointer — they're unchanged. The only paths that branch on view-ness are:

- **Free / GC reclamation** — view doesn't own its buffer; must not `free(data)`, must drop ref on base.
- **Mutation attempts** — `array_*_set`, `fill`, in-place sort: must raise an error or COW.
- **Resize / capacity** — meaningless for views; rejected.

### Storage

A view needs two additions over a normal `ArrayNum`:

```c
// applies when ARRAY_NUM_FLAG_VIEW is set in elem_type byte
struct ArrayNumViewMeta {
    ArrayNum* base;       // ref-counted source array (for GC liveness)
    int64_t   byte_offset; // bytes into base->data where this view begins
};
```

Stored in the same slot as `ArrayNumShape` from §1 (in `extra`), since:
- A view is necessarily n-D (it has shape/strides matching the slice geometry, even when ndim=1).
- A view with `ndim=1` and unit stride is just a windowed slice — still benefits from carrying offset and base pointer.

So views always set both flags: `ARRAY_NUM_FLAG_VIEW | ARRAY_NUM_FLAG_NDIM`. The shape/strides table grows two fields:

```c
struct ArrayNumShape {
    uint8_t  ndim;
    uint8_t  is_c_contig:1;
    uint8_t  is_f_contig:1;
    uint8_t  is_view:1;        // ← when set, the next two fields are meaningful
    int64_t  offset;            // element offset within base->data
    ArrayNum* base;             // NULL for owned arrays
    int64_t  shape[];
    // strides[] follow
};
```

`base` participates in reference counting through Lambda's existing `Container.ref_cnt` mechanism — incremented when the view is created, decremented on view drop.

### Construction

Views appear from:

- **`slice(arr, start, end)`** — currently copies (`SYSFUNC_SLICE`). With contiguous source: returns a view. Add `slice(arr, start, end, copy: true)` for explicit copy.
- **Strided slicing `arr[start:end:step]`** (new syntax extension in [grammar.js](../lambda/tree-sitter-lambda/grammar.js)). Always view-able since stride is recordable.
- **Reshape on contiguous source** (§1).
- **Transpose** (§1).
- **`view(buffer, elem_type, shape)`** — explicit view over an externally-owned buffer (used by Phase 5 for `JsArrayBuffer`).

### Mutability

Views are **read-only by default**. Rationale: most slice consumers don't mutate, COW is surprising for performance-sensitive code, and a separate mutable-view path can be added later without breaking the read-only contract. Attempted writes through a view raise `ItemError` with a clear message.

If the user wants a writable copy of a view: `copy(view)`.

A future `mut_view` form (Phase 1b) can grant write-through if needed for in-place algorithms.

### Liveness

The simplest correct rule: a view holds a ref on its base array. As long as any view exists, the base buffer is kept alive. This matches NumPy's `arr.base` semantics. Releasing the last view drops the base.

For external buffers (Phase 5: `JsArrayBuffer`), the base ref points to a wrapper that owns the buffer and survives detachment checks.

### GC interaction — this is the load-bearing change

Views introduce a dependency the current GC does not model. Today, `LMD_TYPE_ARRAY_NUM_` is in the *"no outgoing Item pointers — nothing to trace"* fast path in [lib/gc/gc_heap.c:856](../lib/gc/gc_heap.c#L856), and compaction [gc_heap.c:1187](../lib/gc/gc_heap.c#L1187) freely relocates the `items` buffer because it assumes nothing else aliases it. Both assumptions break for views. Three things must change in [lib/gc/gc_heap.c](../lib/gc/gc_heap.c) and [lambda/lambda-mem.cpp](../lambda/lambda-mem.cpp):

#### 5a. Tracing — view marks its base

`LMD_TYPE_ARRAY_NUM_` moves out of the no-trace list. The trace function reads the elem_type byte and, when `ARRAY_NUM_FLAG_NDIM` is set, dereferences `extra` to find the `ArrayNumShape`. If `is_view` is set on the shape, the trace marks `shape->base` as a GC root:

```c
case LMD_TYPE_ARRAY_NUM_: {
    uint8_t* p = (uint8_t*)obj;
    uint8_t elem_byte = p[1];
    if (elem_byte & ARRAY_NUM_FLAG_NDIM) {
        ArrayNumShape* shape = *(ArrayNumShape**)(p + 24);  // extra slot
        if (shape && shape->is_view && shape->base) {
            gc_mark_object(gc, (gc_header_t*)shape->base);
        }
    }
    break;
}
```

The 1-D owned path stays in the no-trace fast path (flag bit not set).

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

The pin bit lives in `ArrayNum.elem_type` byte's low nibble (one more flag bit alongside `ARRAY_NUM_FLAG_NDIM` and `ARRAY_NUM_FLAG_VIEW`). Set on first view creation; never cleared (cleared would require view-count tracking, which adds machinery for no real benefit — pinned arrays simply stop being relocated, which is fine).

A side effect worth calling out: while the data buffer is pinned, the `ArrayNum` *container* (the header) can still move freely. Only the `data`/`items`/`float_items` pointer's *target* is pinned. The pin bit is read by the compaction path, not by tracing.

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

6. **Boolean mask indexing** (new): `arr[mask]` where `mask` is a `bool[]` (currently absent — would need a `ELEM_BOOL` or repurpose `ELEM_UINT8` with `0`/`1` values, see Phase 5 alignment). For Phase 2, defer; add it once boolean arrays exist.

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

For N-D arrays (§1), iteration semantics need a decision: does `for row in matrix` yield rows or scalars? **Recommendation**: matches NumPy — `for x in nd_arr` iterates the leading axis, so each `x` is an `(ndim-1)`-D view. Scalar iteration requires `for x in flatten(arr)`.

---

## 5. Phase 2: Unify LambdaJS Typed Arrays with ArrayNum

### Current duplication

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

Phase 2b — sequential:
   §2 (views) + GC upgrade          — ~3 weeks; GC tracer/finalizer/compactor
                                       all need extending (see §2 GC interaction).
                                       The latent compact-elem compaction bug
                                       gets fixed in the same pass.
   §1 (n-d)                         — ~3 weeks, broadcasting + format/output;
                                       reuses the shape side-table slot §2 added.

Phase 2c — depends on 2a + 2b:
   §5 (JS unification)              — ~3 weeks; mostly mechanical once primitives exist
```

Each phase keeps the 1-D, owned-buffer fast path identical to today, so existing benchmarks should not regress.

## Open questions

1. **Element-type byte layout** — §2's GC interaction needs three flag bits in the low nibble: `ARRAY_NUM_FLAG_NDIM`, `ARRAY_NUM_FLAG_VIEW`, `ARRAY_NUM_FLAG_PINNED`. Container already uses 4 boolean flags there (`is_content`, `is_spreadable`, `is_heap`, `is_data_migrated`). For `ArrayNum`, `is_spreadable` and `is_content` are arguably meaningless; reclaiming them gets us to 3 free bits. Decision needed before any of §1/§2 lands.
2. **Mutable views** — defer or include? Read-only is safer; mutable enables in-place algorithms (e.g. `arr[i:j] = arr[k:l]`). Recommendation: defer to Phase 2d.
3. **`ELEM_BOOL`** — needed for boolean mask indexing in §4. Adding a 14th element type vs. repurposing `ELEM_UINT8` with `{0, 1}` invariant. Recommendation: dedicated `ELEM_BOOL` with 1-byte storage, separate from `ELEM_UINT8` because reductions (`any`, `all`) and bool-specific output formatting want type clarity.
4. **`tensor(...)` literal sugar vs. nested `[]`** — auto-detection of nested literals as N-D is convenient but fragile (one mixed-length inner row ruins it). Explicit `tensor(...)` is clearer for the language; nested literals stay as `Array of ArrayNum`. Recommendation: ship explicit form first, add auto-detect later if user feedback wants it.
5. **N-D iteration semantics** — `for x in nd_arr` yields leading-axis slices (NumPy semantics) vs. yields scalars (current 1-D semantics). The first is more consistent with NumPy; the second is what existing user code expects. Recommendation: leading-axis slices for N-D, scalars for 1-D — they're consistent with `arr[i]` returning a slice for N-D and a scalar for 1-D.

## Non-goals

- SIMD intrinsics — deferred to a separate perf-tuning pass.
- Lazy / fused evaluation graphs (à la NumPy `numexpr` or JAX). Stay eager.
- GPU / accelerator interop.
- Complex numbers (`ELEM_COMPLEX64/128`) — would compose cleanly with this design but is its own proposal.
- Sparse arrays.
- Memory-mapped file backing — falls out almost free once views over external buffers exist (§5 enables it), but no first-class API in this proposal.

## Success criteria

1. Every existing test in `test/lambda/typed_array_*.ls`, `compact_typed_arrays.ls`, `array_float.ls` continues to pass unchanged.
2. New tests:
   - N-D construction, reshape, transpose, broadcasting, axis reductions.
   - View aliasing: writes to source visible through view; freeing source from under live view is impossible.
   - Vectorized `sin(arr)`, `clip(arr, 0, 1)`, etc. produce element-wise results.
   - `arr |> filter(x -> x > 0) |> sum` runs without intermediate `Array` boxing (verify via debug counter).
   - JS `Float32Array` shares storage with Lambda `ArrayNum f32` view (Phase 5).
3. 1-D arithmetic benchmark (`a + b` for length-1M `f64[]`) does not regress measurably vs. Phase 1 baseline.
4. Memory: a 1-D owned `ArrayNum` allocation footprint is unchanged from today.
