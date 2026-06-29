# Proposal: Lambda Typed Array 3 — Unifying the LambdaJS TypedArray Stack onto `ArrayNum`

> **Scope.** Migrate the LambdaJS typed-array stack (`JsTypedArray` / `JsArrayBuffer` / `JsDataView`, ~3,300 lines in [lambda/js/js_typed_array.cpp](../lambda/js/js_typed_array.cpp)) so that its **numeric storage and element machinery** are provided by the Lambda `ArrayNum` runtime type, instead of a second, parallel implementation. This is the "Phase 5 / JS unification" that [Lambda_Typed_Array2.md](Lambda_Typed_Array2.md) §5 deferred — now its own proposal, written against the **as-built** state of both stacks after Typed Array 2 landed (baseline 3224/3224).

> **Status:** design proposal with Phase 3a, the Phase 3b substrate slice, and the first Phase 3c descriptor slice implemented. `ELEM_UINT8_CLAMPED` now exists as the 15th `ArrayNum` kind, `ArrayNum` can describe a writable external byte-buffer view with a raw `Container*` lifetime base, and each JS `JsTypedArray` now owns a GC-marked `ArrayNum* view` descriptor over its existing byte storage. Full JS raw-path replacement is still pending. The Typed Array 2 work (N-D, views, `is_view`/`is_pinned`, the compactor view-skip, the `base: Container*` field) was built with this migration in mind and is the load-bearing prerequisite — it is now in place.

---

## 0. TL;DR

- The two stacks are **completely decoupled today** — no value ever crosses between a Lambda `ArrayNum` and a JS typed array. Unification is therefore an **internal storage refactor of the JS layer**, plus an **interop bridge** as the payoff. It is *not* a JS-observable semantics change.
- The **load-bearing constraint** is GC. JS hands raw `data` pointers to JIT code and caches them; those bytes must **never move**. But `ArrayNum`'s element data lives in the GC data zone, which the compactor **relocates** (`gc_heap.c:1203‑1231`). The two allocators are different: JS buffers are `mem_alloc` (malloc, non-moving); `ArrayNum` data is `heap_data_alloc → gc_data_alloc` (moving).
- **Resolution:** the shared substrate is the **non-moving byte buffer** (owned by `JsArrayBuffer`, malloc-backed). `ArrayNum` participates as a **read/write view over that external buffer** — exactly the case the Typed Array 2 view machinery was generalized for (`is_view`, `base: Container*`, compactor skips view/pinned data). The JS layer keeps its spec semantics; `ArrayNum` provides element typing, sizing, indexed get/set, bulk copy, fill, slice, and (bonus) vectorized math.
- **What migrates:** element-type enum, element sizing, raw get/set, `set`/`copyWithin`/`fill`/`slice`/`subarray` storage mechanics. **What stays in the JS layer:** every spec-observable behavior — write coercion (truncate/clamp/BigInt/NaN), detach/transfer, resizable + length-tracking, out-of-bounds rules, DataView endianness, Atomics, Node `Buffer`, the exotic property gate, and the lazy "native-backed Map → upgraded Map" path.
- **The prize:** once both worlds share a byte-buffer substrate, a Lambda `ArrayNum` can be handed to JS as a `Float64Array` (and a JS `Uint8Array` consumed by Lambda as an `ArrayNum`) with **zero copy** in the natural direction, plus Lambda's `+`/`sin`/`sum`/broadcasting becoming available over JS-originated data.

---

## 1. Why unify

Three reasons, in priority order:

1. **Interop is currently impossible.** A Lambda program that parses a binary blob into an `ArrayNum` and a JS module that wants to process it as a `Float32Array` cannot share the bytes — there is **no bridge** (`lambda_to_js` / `js_to_lambda` do not exist; confirmed by search). Every cross-language handoff today would be a full re-parse or serialize round-trip. A shared substrate makes zero-copy handoff possible.

2. **Duplicated maintenance surface.** Two implementations of "typed numeric buffer with N element kinds, indexed get/set, bulk copy, fill, slice" drift independently. The JS stack is ~3,300 lines ([js_typed_array.cpp](../lambda/js/js_typed_array.cpp) is 3,153; the header 164); a large fraction is element-size dispatch, raw load/store, same-type `memcpy` fast paths, and cross-type conversion loops — all of which `ArrayNum` already implements for its 15 element kinds.

3. **Feature transfer, both directions.** Unification gives JS typed arrays Lambda's vectorized math and N-D for free (where it makes sense as a Lambda-side extension), and gives Lambda `ArrayNum` the JS stack's hard-won binary-data machinery (DataView-style heterogeneous access, endianness) as a reusable primitive.

This proposal optimizes for **(2) shared maintenance first** (the safe, test-gated refactor) and **(1) interop second** (the new capability that the refactor unlocks).

---

## 2. Current state — two decoupled stacks

### 2.1 The LambdaJS stack (as built)

A JS typed array is **not** a bespoke heap type: it is an ordinary Lambda `Map` (`LMD_TYPE_MAP`) whose `map_kind` byte is stamped `MAP_KIND_TYPED_ARRAY` / `MAP_KIND_ARRAYBUFFER` / `MAP_KIND_DATAVIEW`, with `m->data` pointing at a native C struct, `m->type` at a sentinel `TypeMap`, and `m->data_cap = 0` flagging "native-backed" ([js_typed_array.cpp:1935‑1940](../lambda/js/js_typed_array.cpp)). The same zero-overhead wrapper pattern the DOM uses.

```c
// lambda/js/js_typed_array.h:54‑64
typedef struct JsTypedArray {
    JsTypedArrayType element_type;   // one of 11 kinds
    int   length;                    // element count
    int   byte_length;               // total bytes (recomputed for length-tracking views)
    int   byte_offset;               // offset into backing buffer
    void* data;                      // raw pointer to first element (STALE after buffer resize)
    JsArrayBuffer* buffer;           // backing buffer, or NULL for standalone (owns data)
    uint64_t buffer_item;            // original ArrayBuffer Item — identity-preserving .buffer
    bool  length_tracking;           // length re-derives from buffer on each access
    bool  is_buffer;                 // Node Buffer instance
} JsTypedArray;

// lambda/js/js_typed_array.h:34‑41
typedef struct JsArrayBuffer {
    void* data;            // malloc'd byte buffer
    int   byte_length;     // current bytes (0 after detach)
    int   max_byte_length; // resizable cap
    bool  detached, is_shared, resizable;
} JsArrayBuffer;

// lambda/js/js_typed_array.h:44‑52
typedef struct JsDataView {
    JsArrayBuffer* buffer; int byte_offset, byte_length;
    uint64_t buffer_item;  bool length_tracking;
} JsDataView;
```

Salient facts ( for the migration):

- **11 element kinds** ([js_typed_array.h:19‑31](../lambda/js/js_typed_array.h)): `JS_TYPED_INT8`, `UINT8`, `INT16`, `UINT16`, `INT32`, `UINT32`, `FLOAT32`, `FLOAT64`, `UINT8_CLAMPED`, `BIGINT64`, `BIGUINT64`. Sizes via `typed_array_element_size` ([js_typed_array.cpp:277‑292](../lambda/js/js_typed_array.cpp)).
- **Allocation is malloc-backed and non-moving:** `mem_alloc` / `mem_calloc` with `MEM_CAT_JS_RUNTIME` ([js_typed_array.cpp:1342,1345,1929](../lambda/js/js_typed_array.cpp)) → memtrack escape hatch over `malloc`/`calloc` ([lib/memtrack.h:577‑596](../lib/memtrack.h)). **Not** the GC data zone.
- **Finalization is explicit:** `gc_finalize_typed_array` / `gc_finalize_arraybuffer` run when the wrapping `Map` is swept ([lambda-mem.cpp:36‑68](../lambda/lambda-mem.cpp)); a `gc_native_seen_t` dedup set prevents double-free of a shared buffer. A view (`ta->buffer != NULL`) does **not** free the bytes — the buffer's finalizer does.
- **Indexed access** routes through an inline fast path (`js_runtime.cpp:7045`) and a general "exotic gate" (`js_runtime.cpp:2894`) that enforces ECMAScript canonical-numeric-index rules; metadata (`length`/`byteLength`/`byteOffset`/`buffer`) is synthesized from the struct, not stored ([js_runtime.cpp:2919‑2950](../lambda/js/js_runtime.cpp)).
- **Lazy upgrade:** writing a user property to a typed array repoints the Map to store the native pointer under an internal key (`__ta__`/`__ab__`/`__dv__`) and frees `m->data` for a real field buffer — a one-way transition ([JS_12_TypedArrays.md](../doc/dev/js/JS_12_TypedArrays.md) §2; `js_runtime.cpp:2880`).
- **Hard spec semantics** (all must be preserved): `Uint8ClampedArray` round-half-to-even clamping ([js_typed_array.cpp:395‑403](../lambda/js/js_typed_array.cpp)); integer 2's-complement modulo wrap ([js_typed_array.cpp:410‑421](../lambda/js/js_typed_array.cpp)); BigInt coercion that *rejects* Number ([js_typed_array.cpp:2237‑2282](../lambda/js/js_typed_array.cpp)); DataView runtime-detected endianness with conditional byte-swap ([js_typed_array.cpp:2862‑3149](../lambda/js/js_typed_array.cpp)); detach (zero length, OOB views) ([js_typed_array.cpp:1689‑1698](../lambda/js/js_typed_array.cpp)); resizable + length-tracking re-derivation per access ([js_typed_array.cpp:302‑319](../lambda/js/js_typed_array.cpp)); Atomics via GCC builtins on `SharedArrayBuffer` ([js_typed_array.cpp:1081‑1310](../lambda/js/js_typed_array.cpp)).

### 2.2 The Lambda `ArrayNum` stack (as built, post-TA2)

```c
// lambda/lambda.h:590‑604
struct ArrayNum {
    TypeId  type_id;            // LMD_TYPE_ARRAY_NUM
    uint8_t flags;              // upper nibble: is_ndim/is_view/is_pinned; lower: is_content/...
    uint8_t elem_type;          // map_kind byte — one of 14 ELEM_* kinds
    uint8_t padding[5];
    union { int64_t* items; double* float_items; void* data; };
    int64_t length;             // element count (= product(shape) for N-D)
    int64_t extra;              // ArrayNumShape* when is_ndim/is_view; else overflow count
    int64_t capacity;
};
```

- **15 element kinds:** `ELEM_INT`, `INT64`, `FLOAT`, `INT{8,16,32}`, `UINT{8,16,32}`, `FLOAT{16,32}`, `UINT64`, `FLOAT64`, `BOOL`, `UINT8_CLAMPED`. Bytes via `ELEM_TYPE_SIZE[elem_type >> 4]`.
- **Data is GC-managed and *moving*:** `heap_data_alloc → gc_data_alloc` (GC nursery data zone, [lambda-mem.cpp:407‑422](../lambda/lambda-mem.cpp), [gc_heap.c:541‑564](../lib/gc/gc_heap.c)). The compactor `gc_compact_data` **relocates** ArrayNum data nursery→tenured and rewrites the pointer ([gc_heap.c:1203‑1231](../lib/gc/gc_heap.c), pointer rewrite at `:1224`) — **unless** `is_view` (0x20) or `is_pinned` (0x40) is set, in which case it skips relocation (`if (flags & 0x60) break;` at [gc_heap.c:1209](../lib/gc/gc_heap.c)).
- **Views (TA2 §2):** zero-copy, `is_view` set, an `ArrayNumShape` side-table in `extra` carrying `offset` + `base` (typed `Container*`, deliberately not `ArrayNum*`), strides for N-D. As built, Lambda already has `is_mutable_view` for procedural write-through views; non-mutable views remain read-only. The GC tracer marks `shape->base`; the base carries `is_pinned`.

### 2.3 The two element-type enums line up almost 1:1

| JS kind | `ArrayNum` kind | Note |
|---|---|---|
| `JS_TYPED_INT8` | `ELEM_INT8` | exact |
| `JS_TYPED_UINT8` | `ELEM_UINT8` | exact |
| `JS_TYPED_INT16` | `ELEM_INT16` | exact |
| `JS_TYPED_UINT16` | `ELEM_UINT16` | exact |
| `JS_TYPED_INT32` | `ELEM_INT32` | exact |
| `JS_TYPED_UINT32` | `ELEM_UINT32` | exact |
| `JS_TYPED_FLOAT32` | `ELEM_FLOAT32` | exact |
| `JS_TYPED_FLOAT64` | `ELEM_FLOAT64` | exact (also Lambda's default `ELEM_FLOAT`) |
| `JS_TYPED_BIGINT64` | `ELEM_INT64` | **storage** identical — the element is a 64-bit lane by spec. *Values* are `BigInt` = `LMD_TYPE_DECIMAL` (mpdecimal), converted at the boundary, not truncated (see §4). |
| `JS_TYPED_BIGUINT64` | `ELEM_UINT64` | same; the load **must** be unsigned-aware — high-bit lanes reconstruct the full u64, not a negative value (see §4). |
| `JS_TYPED_UINT8_CLAMPED` | `ELEM_UINT8_CLAMPED` | 1-byte storage; store path clamps with round-half-to-even |

The element-kind gap is now closed. See §4.

---

## 3. The load-bearing constraint: GC, moving vs non-moving

This is the single fact that dictates the architecture. The naïve idea — "make `JsTypedArray` store an `ArrayNum*` and let `ArrayNum` own the bytes" — is **unsafe**:

- `ArrayNum` data lives in the **moving** GC data zone; the compactor relocates it and rewrites the owner's pointer ([gc_heap.c:1224](../lib/gc/gc_heap.c)).
- JS hands raw `data` pointers to JIT code and to `memcpy`/`memmove` bulk fast paths, and caches `ta->data`. Those consumers are **not** updated by the GC. After a compaction, every cached JS pointer into ArrayNum-owned bytes would dangle.
- The JS engine's "non-moving" guarantee ([JS_03_Value_Model.md:82](../doc/dev/js/JS_03_Value_Model.md)) applies to **object structs** (the object zone, swept-not-moved), **not** to data-zone buffers — which *are* compacted ([gc_heap.c:4‑9](../lib/gc/gc_heap.c)).

There are two ways to get a non-moving byte buffer; the proposal picks the first:

| Option | Mechanism | Verdict |
|---|---|---|
| **A. Substrate = malloc buffer; `ArrayNum` is a *view* over it** | The shared bytes are `mem_alloc`'d (as JS buffers already are). The `ArrayNum` header sits in the **object zone** (non-moving), carries `is_view`, and points its `data` at the external buffer; the compactor already skips view data (`flags & 0x60`). | **Chosen.** Reuses TA2 view machinery verbatim. No new pinning policy. The base (`JsArrayBuffer`) already lives non-moving. |
| B. Substrate = GC data zone; pin it (`is_pinned`) | Keep bytes in the moving zone but exempt them from relocation. | Rejected as the default: TA2 deliberately **never clears `is_pinned`** (pin-clearing deferred), so every JS-backed array would permanently pin a data-zone slab → fragmentation. Acceptable only for the *Lambda→JS* interop direction (§7), opt-in. |

**Consequence:** `JsArrayBuffer` remains the owner of a **malloc-backed, non-moving** byte buffer. `ArrayNum` becomes a *typed window* over that buffer (`is_view`, `base` = the `JsArrayBuffer`'s `Map`). The roles invert from the naïve sketch: **the JS buffer is the substrate; `ArrayNum` is the view** — which is exactly the shape TA2 §2 prepared (`base: Container*`, view-skip in the compactor).

---

## 4. Element-type unification

**`ELEM_UINT8_CLAMPED` is now the 15th element kind** (1 byte, same storage as `ELEM_UINT8`). Rationale, carried from TA2 §5:

- The clamp is a **write-coercion** difference (NaN→0, saturate to [0,255], round-half-to-even), not a read or storage difference. Reads are identical to `UINT8`.
- A flag-bit alternative (reuse `ELEM_UINT8` + a `clamped` bit) is rejected: it forces a per-write branch into the hot store loop and muddies `ELEM_TYPE_SIZE` indexing. A distinct kind keeps the clamp in one place — the JS store path — and lets every other ArrayNum op treat it exactly as `UINT8`.

**BigInt64 / BigUint64** map to `ELEM_INT64` / `ELEM_UINT64` for *storage*, and this is **lossless for the element's defined range** — not a truncating shortcut. The two layers are distinct:

- **Element layer (lane):** a `BigInt64Array` element is *by ECMAScript definition exactly a 64-bit lane*. A wider BigInt is wrapped on store by the spec's `BigInt.asIntN(64, v)` / `asUintN(64, v)` — the same wrap V8/SpiderMonkey apply, not a Lambda-specific loss. So an `ELEM_INT64`/`ELEM_UINT64` lane holds the full 64 bits the element is ever allowed to carry.
- **Value layer:** a JS `BigInt` in LambdaJS is *not* an int64 — it is a Lambda **decimal** (`LMD_TYPE_DECIMAL`, mpdecimal, 38 significant digits ≈ 126 bits; `bigint_from_int64` builds an `mpd_t`, [lambda-decimal.cpp:983](../lambda/lambda-decimal.cpp)). Arbitrary-precision BigInts live here and never pass through a typed-array lane.

The value↔lane conversion is a **boundary** concern that stays in the JS get/set wrappers (never in `ArrayNum`, which just holds the 8-byte lane). Two boundary rules must be carried over verbatim:

- **store:** decimal BigInt → `js_bigint_as_int_n(…, 64, …)` / `…_as_uint_n` (the spec-mandated 64-bit wrap), which also *rejects Number* with a `TypeError` → write the lane ([js_typed_array.cpp:2237‑2282](../lambda/js/js_typed_array.cpp)).
- **load (unsigned-aware — correctness-critical):** `BigInt64` and `BigUint64` lanes ≤ `INT64_MAX` use `bigint_from_int64`; a `BigUint64` lane with the high bit set (> `INT64_MAX`) is reconstructed via `bigint_from_string("%llu", v)` so `0xFFFF…FFFF` reads back as `18446744073709551615n`, **not** `-1n` ([js_typed_array.cpp:2194‑2207](../lambda/js/js_typed_array.cpp)). A naive `bigint_from_int64((int64_t)lane)` for `BigUint64` would silently corrupt the top half of the unsigned range — so the migration must preserve this branch, not "simplify" it.

No new element kind needed; the 64-bit lanes already exist.

After this, **`ArrayNum` covers every JS element type**, and `ELEM_TYPE_SIZE` plus the existing per-kind load/store cover all sizing.

---

## 5. Target architecture

```
        JS API surface  (Js… exotic Maps, prototype methods, exotic gate)
        Uint8Array(…) · ta[i] · .set() · .subarray() · DataView · Atomics
                              │  thin wrapper (JS identity + spec coercion)
                              ▼
   JsTypedArray  =  { ArrayNum* view ;  buffer_item ; length_tracking ; is_buffer }
                              │  storage + element typing delegated
                              ▼
   ArrayNum (is_view)  ──base──►  JsArrayBuffer  ── owns ──►  raw byte buffer
   elem_type, length,            { detached, resizable,        (mem_alloc,
   offset, data ptr               is_shared, max_byte_length }  NON-MOVING)
                              ▲
   JsDataView  ──────────────┘  (byte-level: endianness, heterogeneous access —
                                 reads the same raw buffer directly, no ArrayNum)
```

Concretely, each struct changes as follows:

- **`JsArrayBuffer`** — unchanged in spirit: owns the malloc byte buffer + JS state (`detached`/`resizable`/`is_shared`/`max_byte_length`). It becomes the **lifetime base** of every `ArrayNum` view over it. Its `Map` wrapper is what `ArrayNumShape.base` points to, stored as a raw `Container*` (matching existing Lambda views); the GC tracer already marks that raw pointer from the shape side-table. Because a `Map`'s `data` field is the native payload (`JsArrayBuffer*`), not the byte buffer itself, external views also carry an explicit raw byte-base pointer when constructed.
- **`JsTypedArray`** — its storage fields (`element_type`, `length`, `byte_length`, `byte_offset`, `data`) collapse into a single **`ArrayNum* view`**. Element typing, sizing, indexed get/set, bulk copy/fill/slice all delegate to `ArrayNum`. It keeps the **JS-only** fields: `buffer_item` (identity-preserving `.buffer`), `length_tracking`, `is_buffer`. Because `JsTypedArray` is native data behind a Map, the GC must gain an explicit mark edge from the wrapper/native finalization path to this `ArrayNum* view`; otherwise the view can be swept while the JS Map survives. A standalone (`new Uint8Array(8)`) typed array becomes: allocate a fresh non-moving buffer + an owning-less `ArrayNum` view over it (so there is exactly one storage path, not two).
- **`JsDataView`** — essentially unchanged. DataView is *inherently heterogeneous* (it reads Int8/Int16/Float64/BigInt64 from the same bytes at arbitrary offsets with explicit endianness), which `ArrayNum`'s single-`elem_type` model does not represent. DataView keeps reading the raw `JsArrayBuffer.data` directly. It benefits from unification only indirectly (shared buffer substrate, shared detach/resize logic).
- **`ArrayNum`** — gains **two capabilities** beyond what TA2 shipped:
  1. **Views over an external (non-`ArrayNum`) base** — implemented as `array_num_new_external_view(base, data_base, elem_type, byte_offset, length, mutable_view)`. `base` is the raw `Container*` lifetime edge stored in `ArrayNumShape.base`; `data_base` is the raw non-moving byte buffer, so `view->data` aliases `data_base + byte_offset`. The view finalizer frees nothing.
  2. **Writable external views** — the runtime already has an `is_mutable_view` flag and generic mutable-view guards for procedural write-through views. JS typed arrays should reuse that capability for views whose base is a `JsArrayBuffer`, while preserving read-only Lambda-side interop views. The stale specialized `array_float_set`, `array_int_set`, and `array_float_set_item` guards now check `!is_mutable_view` instead of rejecting every view.

What this deletes from the JS layer once delegation is wired: the per-element-size raw load/store dispatch, the same-type `memcpy` and cross-type conversion loops in `set`/`slice`/`copyWithin`/`fill` (replaced by `ArrayNum` bulk ops with a JS coercion shim), and the standalone-buffer bookkeeping. Estimated ~800–1,200 of the ~3,300 lines become thin delegations; the rest (spec semantics, exotic gate, Atomics, DataView, Node Buffer) stays.

---

## 6. The hard problems, and how each is resolved

### 6.1 ArrayBuffer = bytes viewed at many types; `ArrayNum` = one type

A single `ArrayBuffer` is simultaneously viewable as `Float64Array`, `Uint8Array`, and a `DataView`. `ArrayNum` has one `elem_type`. **Resolution:** the *substrate is bytes* (the `JsArrayBuffer`), and each typed array is a **separate `ArrayNum` view** with its own `elem_type` and `byte_offset` over the shared bytes. N views with N element types over one buffer = the JS model, expressed as N `ArrayNum` views sharing one `base`. DataView needs no `ArrayNum` at all. This is consistent and needs no new concept beyond external-base views.

### 6.2 Write coercion stays at the JS boundary

`ArrayNum` keeps **strict** numeric semantics (a store of an out-of-range value is the caller's problem). All JS coercion — integer 2's-complement wrap, `Uint8Clamped` round-half-to-even, NaN→0 for ints, `ToNumber`/`ToBigInt`, BigInt-rejects-Number — stays in the **JS set path** ([js_typed_array.cpp:380‑455, 2237‑2342](../lambda/js/js_typed_array.cpp)), which computes the final lane value and then calls the `ArrayNum` raw store. `ELEM_UINT8_CLAMPED` is the one place a coercion leaks into `ArrayNum`, and only because the clamp is intrinsic to the type's identity.

### 6.3 Detach, transfer, resizable, length-tracking, OOB

These are **buffer-level** JS states with no `ArrayNum` analog — and they should *stay* JS-level, driving the view rather than living in it:

- **Detach / transfer** ([js_typed_array.cpp:1552‑1558, 1689‑1698](../lambda/js/js_typed_array.cpp)): the `JsArrayBuffer` drops/zeroes its bytes. Every dependent `ArrayNum` view must then read as out-of-bounds. Implement by having the JS wrapper consult buffer liveness before every access (the JS get/set already gates on `js_typed_array_is_out_of_bounds`); the `ArrayNum` view's `length`/`capacity`/`data` are re-derived from the buffer, not trusted-cached. This matches the existing rule that view `data` is stale after resize and must be re-fetched ([js_typed_array.cpp:2357](../lambda/js/js_typed_array.cpp)).
- **Resizable + length-tracking** ([js_typed_array.cpp:302‑319](../lambda/js/js_typed_array.cpp)): keep `length_tracking` on the JS wrapper; on each access the wrapper recomputes element count from `buffer->byte_length` and refreshes the `ArrayNum` view's `length`/`capacity`/`data`. Make this a single helper, e.g. `js_typed_array_refresh_arraynum_view(ta)`, and require indexed, bulk, and inline fast paths to call it after OOB/detach checks. `ArrayNumShape` does **not** grow a resize concept; the buffer remains the source of truth.
- **OOB views**: reads → `undefined`, writes → silent no-op (typed array) or `TypeError` (DataView). These divergent error semantics live in the JS get/set/DataView paths, unchanged.

The principle: **the JS wrapper owns mutable buffer state and refreshes a cheap `ArrayNum` view descriptor per operation; `ArrayNum` owns element typing and the actual byte poke.**

### 6.4 Writable views

TA2's initial view design was read-only, but the as-built runtime already has `is_mutable_view` for procedural write-through views and the generic `array_num_set_item` / `array_num_set_nd` guards check it. JS should reuse that bit for `JsArrayBuffer`-backed views. Two caveats are implementation-critical:

- Some older specialized setters still reject `arr->is_view` unconditionally; route JS through the generic mutable-aware setter path or harmonize those guards before exposing writable external views.
- The writability policy should still be explicit: JS-owned views are mutable from JS; JS→Lambda interop views should be handed to Lambda as read-only unless an API deliberately asks for shared mutation.

### 6.5 DataView endianness

Out of scope for `ArrayNum`. DataView reads/writes the raw buffer with explicit, runtime-detected endianness ([js_typed_array.cpp:2862‑2877](../lambda/js/js_typed_array.cpp)); `ArrayNum` stores native-endian lanes. Keep DataView as a byte-level reader over the shared `JsArrayBuffer`. (A future, optional `ArrayNum` "byte view" reader could subsume it, but that is not part of this migration.)

### 6.6 Atomics / SharedArrayBuffer

Stay JS-layer, layered on the shared-buffer primitive. Atomics operate on raw aligned lanes via GCC builtins ([js_typed_array.cpp:1081‑1126](../lambda/js/js_typed_array.cpp)); they need the buffer address and a lane offset/size, all available from the `JsArrayBuffer` + the view's `byte_offset`/`elem_type`. No `ArrayNum` change required; `SharedArrayBuffer` is a `JsArrayBuffer` with `is_shared=true` and never participates in Lambda compaction (it's malloc-backed).

### 6.7 Node `Buffer`, lazy Map upgrade, identity

`is_buffer` (Node `Buffer`) and the lazy "native-backed Map → upgraded Map with `__ta__` key" path ([js_runtime.cpp:2880](../lambda/js/js_runtime.cpp)) are untouched — they operate on the wrapper Map, above the storage layer. `.buffer` identity continues via `buffer_item`.

---

## 7. The payoff: Lambda ↔ JS interop

Once both worlds share a byte-buffer substrate, interop becomes expressible. The two directions have different costs because of GC:

- **JS → Lambda (zero-copy, natural).** A `JsTypedArray` is *already* an `ArrayNum` view over a non-moving buffer. Handing it to Lambda code is handing over that `ArrayNum` (read-only from Lambda's side, to respect JS ownership). No copy. Lambda's `sum`/`+`/`sin`/broadcasting/reshape operate directly. This is the high-value, low-risk direction.
- **Lambda → JS (copy by default; pin opt-in).** A Lambda-native `ArrayNum`'s bytes live in the **moving** GC data zone. Exposing them to JS zero-copy would require they never move while JS holds a pointer. Two paths:
  - **Default: copy-on-cross.** Allocate a fresh `JsArrayBuffer` (non-moving) and `memcpy` the bytes. Safe, simple, O(n). Recommended default.
  - **Opt-in: pin-on-cross.** Set `is_pinned` on the Lambda `ArrayNum` for the lifetime of the JS view (zero-copy), accepting that — because TA2 defers pin-clearing — the slab stays pinned until collected. Suitable for large, hot, short-lived handoffs; gated behind an explicit API so the fragmentation cost is a caller's choice.

This asymmetry should be **documented, not hidden**: JS→Lambda is free; Lambda→JS is a copy unless you opt into pinning.

---

## 8. What migrates vs what stays

| Concern | Migrates to `ArrayNum` | Stays in the JS layer |
|---|---|---|
| Element-type enum + sizing | ✅ (`ELEM_*` + `ELEM_TYPE_SIZE`) | — |
| Raw indexed get/set (byte poke) | ✅ (`array_num_get`/raw store) | the `ToNumber`/`ToBigInt`/clamp/wrap coercion that precedes it |
| `set` / `copyWithin` / `slice` / `subarray` / `fill` storage mechanics | ✅ (bulk copy / view / fill) | spec argument coercion, species constructor, OOB throwing |
| Standalone buffer allocation | ✅ (one non-moving buffer + view) | — |
| `ArrayBuffer` byte ownership, detach, transfer, resize | partial (buffer is the view `base`) | ✅ all mutable buffer state + lifecycle |
| Length-tracking / OOB length | — | ✅ (re-derive, refresh view descriptor per access) |
| DataView (endianness, heterogeneous) | — | ✅ entirely |
| Atomics / SharedArrayBuffer | — | ✅ entirely |
| BigInt value semantics | 64-bit lane only (`ELEM_INT64`/`UINT64`) | ✅ value is `LMD_TYPE_DECIMAL`; `asIntN/asUintN(64)` wrap on store; **unsigned-aware** reconstruct on load |
| Exotic property gate, canonical index, prototype methods | — | ✅ entirely |
| Node `Buffer`, lazy Map upgrade, `.buffer` identity | — | ✅ entirely |
| GC finalization | simplified (view frees nothing; buffer frees bytes) | the `gc_native_seen_t` dedup pattern (reused) |

---

## 9. Migration plan (incremental, test-gated)

Every phase keeps `make test-js-baseline` / test262 typed-array conformance green. Order is substrate-first so each step is independently shippable.

**Phase 3a — element-type parity.**
**Implemented.** `ELEM_UINT8_CLAMPED` (15th kind) uses the round-half-to-even clamp in the *store* path only; reads alias `UINT8`. Now `ArrayNum` covers all 11 JS element kinds. No JS behavior change yet. *Test:* existing ArrayNum suite + a clamp unit test.

**Phase 3b — `ArrayNum` external-base + writable views.**
**Implemented as the substrate slice.** `array_num_new_external_view(Container* base, void* data_base, ArrayNumElemType, int64_t byte_offset, int64_t length, bool mutable_view)` produces an `is_view` `ArrayNum` aliasing `data_base + byte_offset`; the shape stores a raw `Container*` base for GC tracing, and the view owns/frees no bytes. The older specialized setters that rejected all views now honor `is_mutable_view`. *Test:* descriptor-level unit test for raw base, byte offset, shape offset, mutability, and alignment; existing mutable-view Lambda tests remain the write-through gate.

**Phase 3c — back `JsTypedArray` storage with an `ArrayNum` view.**
**Descriptor slice implemented.** `JsTypedArray` now carries an `ArrayNum* view` alongside the legacy storage fields; standalone and ArrayBuffer-backed constructors create a writable external view over the same non-moving bytes, refresh its length/offset for length-tracking or detached buffers, and the GC now marks the view through a JS-native Map trace callback. Remaining Phase 3c work: route `js_typed_array_raw_get_item`/`raw_store_number`/`raw_reverse` and the same-type `memcpy` paths through `ArrayNum`, then retire the duplicate fields in Phase 3d. Keep `buffer_item`/`length_tracking`/`is_buffer`. *Test:* full `test/js/typed_arrays.*` + the `regression_js54_*` set + test262 typed-array pages.

**Phase 3d — delete the duplicated storage code.**
Remove `js_typed_array_raw_*` element-size dispatch and the cross-type conversion loops now served by `ArrayNum`. Retire the fallback flag. *Test:* same suite, plus a line-count check that the duplication is gone.

**Phase 3e — interop bridge (the payoff).**
`js_to_lambda_array(Item js_ta) → ArrayNum` (zero-copy view, read-only) and `lambda_array_to_js(ArrayNum, kind) → Item` (copy by default; `…_pinned` opt-in). Expose at the Lambda↔JS embedding boundary. *Test:* new round-trip tests (parse blob in Lambda → process as `Float32Array` in JS → read back).

**Deferred (not this proposal):** DataView subsumption into an `ArrayNum` byte-reader; making `SharedArrayBuffer`/Atomics share a Lambda primitive; N-D typed arrays surfaced to JS.

---

## 10. Risks & compatibility

- **The cached-pointer hazard (primary).** Any JS consumer caching a raw `data` pointer across a potential GC must be audited. Because the substrate stays malloc-backed (non-moving) and the `ArrayNum` is only a *view* (its data isn't relocated), the hazard is contained — but Phase 3c must verify no path accidentally routes JS bytes through the moving data zone. Mitigation: the substrate allocator does not change in 3a–3d; only the *descriptor* over it does.
- **Detached/OOB divergence.** Typed array (silent no-op) vs DataView (throw) must be preserved exactly. Kept by leaving get/set in the JS layer; the `ArrayNum` view is only consulted after the JS OOB gate passes.
- **BigInt rejects Number.** Must remain a `TypeError`, not a silent truncation — kept in the JS store path.
- **Species / prototype-swap regressions.** The `regression_js54_*` tests exist precisely for these; they are the gate for Phase 3c.
- **Performance.** The `memcpy` same-type fast paths must remain `memcpy` (now via `ArrayNum` bulk copy). Benchmark with `LAMBDA_JS_TA_RAW_FAST` on/off during 3c. Indexed `ta[i]` must not regress — the inline fast path ([js_runtime.cpp:7045](../lambda/js/js_runtime.cpp)) should call the `ArrayNum` raw accessor with the same shape.

**Test safety net.** `test/js/typed_arrays.{js,txt}` (all 11 kinds, ArrayBuffer/DataView/resize/transfer/detach/length-tracking/endianness/BigInt), `test/js/regression_js54_p{0,2,3,4,8}_*`, the JIT destructure-swap test, and the test262 typed-array conformance pages. The migration is **representation-internal**, so a green suite is strong evidence of behavioral preservation.

---

## 11. Non-goals

- **No JS-observable behavior change.** Same coercion, same errors, same iteration, same Atomics, same Node `Buffer`. This is a storage refactor + an interop addition.
- **No DataView rewrite.** DataView stays byte-level; subsuming it is deferred.
- **No moving-GC interop.** Lambda→JS zero-copy requires pinning; the default is copy. We do not make the GC pin automatically.
- **No N-D typed arrays in JS.** JS typed arrays stay 1-D; `ArrayNum` N-D (TA2 §1) is a Lambda-side capability not surfaced through the JS API here.
- **No `SharedArrayBuffer` unification.** Atomics/shared buffers remain a JS-only concern over the shared primitive.

---

## 12. Open questions

1. **Standalone typed array** — always allocate a `JsArrayBuffer` substrate (uniform, one storage path), or keep a buffer-less fast path for the common `new Uint8Array(n)` case? Uniform is simpler; buffer-less saves one allocation per standalone array. (Leaning: uniform, measure, optimize if it shows.)
2. **Pin-clearing for Lambda→JS** — TA2 defers pin-clearing. If pinned interop proves common, do we revisit view-count tracking on the base to clear `is_pinned` when the last JS view dies? (Deferred until there's evidence.)

---

## 13. Success criteria

- `ArrayNum` covers all 11 JS element kinds (incl. clamped), and it can describe mutable external views over non-moving byte buffers; `make test-lambda-baseline` stays green through 3a–3b.
- `JsTypedArray` storage is a single `ArrayNum` view; `js_typed_array.cpp` shrinks by the duplicated raw-storage code (target: −800 lines or more) with **zero** JS test regressions (full `test/js` typed-array suite + `regression_js54_*` + test262 typed-array pages green).
- A blob parsed into a Lambda `ArrayNum` is consumed by JS as a `Float32Array` (zero-copy) and a JS `Uint8Array` is read by Lambda as an `ArrayNum`, demonstrated by a round-trip test.
- No measurable regression on indexed `ta[i]` and same-type bulk copy benchmarks.

---

## Appendix A — current JS typed-array stack: file/line index

For implementers, the surfaces touched by this migration:

| Surface | Location |
|---|---|
| Structs (`JsTypedArray`/`JsArrayBuffer`/`JsDataView`), element enum | [js_typed_array.h:19‑64](../lambda/js/js_typed_array.h) |
| Element sizing | [js_typed_array.cpp:277‑292](../lambda/js/js_typed_array.cpp) |
| Raw load/store + coercion (truncate/clamp/wrap) | [js_typed_array.cpp:380‑455](../lambda/js/js_typed_array.cpp) |
| Length / byteLength / byteOffset / OOB (length-tracking) | [js_typed_array.cpp:302‑341](../lambda/js/js_typed_array.cpp) |
| Constructors (standalone / from buffer / from array / smart) | [js_typed_array.cpp:1919‑2166](../lambda/js/js_typed_array.cpp) |
| Element get/set | [js_typed_array.cpp:2168‑2342](../lambda/js/js_typed_array.cpp) |
| `fill` / `set` / `slice` / `subarray` / `reverse` | [js_typed_array.cpp:2376‑2734](../lambda/js/js_typed_array.cpp) |
| ArrayBuffer construct / resize / transfer / detach | [js_typed_array.cpp:1341‑1558, 1689‑1698](../lambda/js/js_typed_array.cpp) |
| DataView construct + get/set + endianness | [js_typed_array.cpp:2762‑3154](../lambda/js/js_typed_array.cpp) |
| Atomics + `SharedArrayBuffer` | [js_typed_array.cpp:704‑1310](../lambda/js/js_typed_array.cpp) |
| Map wrapper / native-backed detection / lazy upgrade | [js_typed_array.cpp:1897‑1942](../lambda/js/js_typed_array.cpp); [js_runtime.cpp:2880](../lambda/js/js_runtime.cpp) |
| Exotic property gate (indexed access, metadata) | [js_runtime.cpp:2894‑3050](../lambda/js/js_runtime.cpp) |
| Inline indexed fast path | [js_runtime.cpp:7045‑7070](../lambda/js/js_runtime.cpp) |
| Constructor registration/caching | [js_globals.cpp:16493‑16600](../lambda/js/js_globals.cpp) |
| GC finalization + dedup | [lambda-mem.cpp:36‑109](../lambda/lambda-mem.cpp) |
| Design docs | [JS_03_Value_Model.md](../doc/dev/js/JS_03_Value_Model.md), [JS_12_TypedArrays.md](../doc/dev/js/JS_12_TypedArrays.md) |

## Appendix B — `ArrayNum` surfaces this migration extends

| Surface | Location |
|---|---|
| `ArrayNum` struct + flags (`is_view`/`is_pinned`) | [lambda.h:556‑604](../lambda/lambda.h) |
| `ArrayNumShape` (`offset`, `base: Container*`, strides) | [lambda.h](../lambda/lambda.h) (TA2 §1) |
| Element kinds + `ELEM_TYPE_SIZE` | [lambda.h](../lambda/lambda.h) |
| Data allocation (moving GC data zone) | [lambda-mem.cpp:407‑422](../lambda/lambda-mem.cpp), [gc_heap.c:541‑564](../lib/gc/gc_heap.c) |
| Compactor view/pinned skip | [gc_heap.c:1203‑1231](../lib/gc/gc_heap.c) |
| View construction / leading-axis view | [lambda-data-runtime.cpp](../lambda/lambda-data-runtime.cpp) (TA2 §2) |
| Raw element get | [lambda-data-runtime.cpp:346‑395](../lambda/lambda-data-runtime.cpp) |
