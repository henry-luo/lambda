# Lambda Binary Phase 6 — Shared Byte-Storage Unification Implementation Plan

> **Status:** Complete — implementation Phases 0–6 and all applicable regression gates verified
> **Plan date:** 2026-07-15
> **Design authority:** [Lambda_Type_Binary.md](Lambda_Type_Binary.md) §11
> **Predecessor:** [Lambda_Impl_Binary.md](Lambda_Impl_Binary.md) — decoded-byte semantics and the safe copy bridge, implemented through Phase 5
> **Pipeline dependency:** [Lambda_Design_Pipeline.md](Lambda_Design_Pipeline.md) PL5 — refcounted flats and zero-copy subviews

## 0. Objective and completion definition

Replace the three incompatible byte-ownership mechanisms used by Lambda
binary values, JS ArrayBuffers, and ArrayNum external views with one shared,
refcounted byte-storage substrate while preserving their separate runtime
types and semantics.

This plan is complete only when all of the following hold:

1. A storage-backed Lambda binary is an immutable `(storage, offset, length)`
   span; slicing such a binary is O(1).
2. `JsArrayBuffer` is a stable mutable handle over the same storage substrate.
3. JS typed arrays and Node Buffers continue to use ArrayNum element/view
   operations, but no live view depends on an unowned or stale raw pointer.
4. Eligible `binary ⇄ Uint8Array`/Buffer/DataView conversions share storage.
5. A later JS write cannot change an existing Lambda binary: it first performs
   copy-on-write and moves the ArrayBuffer handle to private storage.
6. Detach, resize, transfer, GC, context teardown, and nested subviews release
   storage exactly once and never dangle.
7. `SharedArrayBuffer` interop remains copying because concurrently mutable
   bytes cannot satisfy Lambda binary immutability.
8. All Phase-1–5 observable behavior and goldens remain unchanged.
9. `LMD_TYPE_BINARY` and `LMD_TYPE_ARRAY_NUM` remain distinct.

The target is storage unification, not a source-language feature release.
There is no literal, formatter, equality, ordering, indexing, or type-system
semantic change in this plan.

---

## 1. Audited current state

### 1.1 Lambda binary

- `typedef String Binary` in `lambda/lambda.h`; bytes are inline after the
  String header.
- `heap_binary_from_bytes` and `heap_binary_concat` allocate a new GC object
  and copy bytes.
- `fn_slice` copies binary slices (`lambda-vector.cpp`, with an explicit PL5
  deferral comment).
- Consumers still accept `String*` and frequently access `chars`/`len`
  directly; these accesses must be inventoried before the layout changes.
- Compiler literals are pool allocated and placed in `Transpiler::const_list`,
  so an external storage allocation cannot be introduced without an explicit
  script/pool lifetime owner.

### 1.2 ArrayNum

- Owned ArrayNum data is allocated from the GC data zone by
  `heap_data_alloc`; it can move during compaction.
- A view stores element-space geometry in `ArrayNumShape`: `offset`, `base`,
  dimensions, and strides.
- `shape->base` is traced by GC; a viewed ArrayNum owner sets `is_pinned`, and
  the compactor skips both `is_view` and `is_pinned` data.
- `is_mutable_view` allows procedural/JS write-through views while functional
  views remain read-only.
- `array_num_new_external_view` aliases a caller-provided raw data pointer and
  stores only a GC lifetime base. It does not own or refcount the bytes.

### 1.3 JS ArrayBuffer and typed arrays

- `JsArrayBuffer` directly owns `void* data`; finalization and detach call
  `mem_free`.
- Resize/transfer replace or copy allocations and update the ArrayBuffer
  wrapper state.
- Every `JsTypedArray` owns an `ArrayNum* view` over its ArrayBuffer bytes.
  Element access and bulk operations already delegate to ArrayNum helpers.
- Fixed geometry lives in `ArrayNumShape`; the typed-array wrapper refreshes
  `view->data` and length after detach/resize checks.
- Optimized/JIT paths can cache or hoist data/length assumptions. Storage
  replacement therefore needs an explicit generation/invalidation contract,
  not only a wrapper-level pointer assignment.

### 1.4 GC/finalization prerequisite

The GC data zone needs no individual frees, so most current values have no
per-object finalizer. Refcounted external storage does. The present sweep
finalizer has targeted cases but no general binary external-payload callback;
context-end cleanup alone is insufficient because a dead binary must release
its storage during a mid-execution collection.

Phase 1 therefore includes the finalization seam. Shipping a refcounted Binary
before that seam would convert ordinary garbage into a process-lifetime leak.

---

## 2. Decision ledger

- **U1 — Types stay separate.** Binary is an immutable compound scalar;
  ArrayNum is a typed numeric container; JS ArrayBuffer is a mutable identity
  handle.
- **U2 — One physical allocation abstraction.** Share `ByteStorage` allocation,
  retain/release, wrapping, and cleanup across runtimes.
- **U3 — Stable mutable handle.** Resize/detach/transfer mutate a
  `ByteBufferHandle`, never a Binary snapshot.
- **U4 — Binary snapshots are immutable.** A binary retains the current
  storage allocation, not the mutable handle.
- **U5 — COW before mutable writes.** If an ArrayBuffer's storage has another
  reference, a non-shared write clones its visible allocation before mutation.
- **U6 — Same-handle views stay coherent.** Every typed array/DataView over an
  ArrayBuffer follows the handle after COW/resize and observes the same bytes.
- **U7 — SharedArrayBuffer never aliases binary.** Both conversion directions
  copy.
- **U8 — Byte units at the substrate boundary.** ArrayNum converts its element
  offsets/lengths with checked element-size arithmetic.
- **U9 — No unowned raw view.** A raw pointer is a transient cache only; a live
  view always has a GC base, retained storage, or stable handle.
- **U10 — Generation invalidates caches.** Handle storage replacement,
  detach, resize, and transfer increment a generation observed by generic and
  optimized access paths.
- **U11 — Small-inline is an optimization.** Begin with correctness using
  storage-backed values; add the proposed 64-byte inline threshold only after
  measurements. An inline value may require one promotion copy at a sharing
  boundary.
- **U12 — Owned ArrayNum migration is staged.** JS/external views converge in
  this plan. Moving every owned numeric array out of the GC data zone is a
  separate, profile-gated final phase.
- **U13 — No silent lifetime workaround.** Missing finalization, stale-pointer,
  or write-gate coverage blocks the phase; it is not patched with unconditional
  copies outside the explicit fallback matrix.

---

## 3. Target low-level interfaces

Names and packing may change during implementation, but these responsibilities
must remain separate.

### 3.1 `ByteStorage`

Add a C-compatible substrate, preferably `lib/byte_storage.{h,c}`, using
`lib/atomic.h` and the project allocators—no `std::` types.

```c
typedef void (*ByteStorageReleaseFn)(void* data, size_t capacity, void* context);

typedef struct ByteStorage {
    atomic_int32 refs;
    uint8_t* data;
    size_t capacity;
    uint32_t flags;
    ByteStorageReleaseFn release_data;
    void* release_context;
} ByteStorage;

typedef struct ByteSpan {
    ByteStorage* storage;
    size_t offset;
    size_t length;
} ByteSpan;
```

Required API:

```c
ByteStorage* byte_storage_alloc(size_t capacity, MemTrackCategory category);
ByteStorage* byte_storage_wrap(void* data, size_t capacity, uint32_t flags,
    ByteStorageReleaseFn release_data, void* context);
ByteStorage* byte_storage_retain(ByteStorage* storage);
void byte_storage_release(ByteStorage* storage);
int32_t byte_storage_ref_count(const ByteStorage* storage); // diagnostics/tests

bool byte_span_init(ByteSpan* span, ByteStorage* storage,
    size_t offset, size_t length);
const uint8_t* byte_span_data(const ByteSpan* span);
bool byte_span_subspan(const ByteSpan* source, size_t offset,
    size_t length, ByteSpan* result);
```

Rules:

- Offset-plus-length and allocation-size overflow are checked before pointer
  arithmetic.
- A span owns one retained reference only when explicitly documented; avoid
  ambiguous borrowed/owned return conventions.
- Release callbacks cover `mem_free`, future `munmap`, and foreign-runtime
  ownership. Clients never branch on allocator origin to free bytes.
- Atomic refcount underflow/overflow is asserted in debug builds.
- `data == NULL` is valid only for zero capacity or a released/detached handle,
  never for a non-empty live span.

### 3.2 `ByteBufferHandle`

Add a stable mutable handle, either in the same module or a Lambda runtime
header if memtrack/GC integration makes `lib` layering inappropriate:

```c
typedef struct ByteBufferHandle {
    ByteStorage* storage;
    size_t storage_offset;
    size_t byte_length;
    size_t max_byte_length;
    uint64_t generation;
    uint32_t flags; // detached, resizable, shared
} ByteBufferHandle;
```

Required operations:

```c
const uint8_t* byte_buffer_data_const(const ByteBufferHandle* handle);
uint8_t* byte_buffer_prepare_write(ByteBufferHandle* handle);
bool byte_buffer_resize(ByteBufferHandle* handle, size_t new_length);
void byte_buffer_detach(ByteBufferHandle* handle);
bool byte_buffer_transfer(ByteBufferHandle* src, ByteBufferHandle* dst,
    size_t new_length, bool fixed_length);
```

`byte_buffer_prepare_write` is the only non-shared path that returns mutable
bytes. It clones and swaps storage when `refs > 1` or the storage is read-only,
then increments generation. Shared-mutable storage follows SharedArrayBuffer
rules and is never exposed as Binary without copying.

`storage_offset` makes a handle over a Binary subview explicit. Public
ArrayBuffer and typed-array offsets are relative to that base. COW/resize may
materialize the visible range into a new zero-offset storage allocation, but a
release callback always receives the original root pointer.

### 3.3 Binary accessors

Replace layout-dependent `String*` use with one public internal API:

```c
const uint8_t* binary_data(const Binary* binary);
uint32_t binary_length(const Binary* binary);
bool binary_is_ascii(const Binary* binary);
ByteSpan binary_span(const Binary* binary); // retained/borrowed contract explicit

Binary* heap_binary_from_bytes(const void* data, size_t length);
Binary* heap_binary_from_storage(ByteStorage* storage,
    size_t offset, size_t length, bool is_ascii);
Binary* heap_binary_slice(Binary* source, size_t offset, size_t length);
Binary* heap_binary_copy(Binary* source);
Binary* heap_binary_concat(Binary* left, Binary* right);
```

No consumer outside the binary module may infer bytes through `String::chars`.
The current `get_safe_binary()` return type changes from `String*` to `Binary*`.

### 3.4 ArrayNum buffer-backed view

Keep `array_num_new_external_view` for truly stable borrowed FFI storage, but
add a handle-aware constructor:

```c
ArrayNum* array_num_new_buffer_view(Container* gc_base,
    ByteBufferHandle* handle, ArrayNumElemType elem_type,
    int64_t byte_offset, int64_t length, bool mutable_view);
```

`ArrayNumShape` needs enough tagged metadata to distinguish:

- a view over another GC container,
- a stable borrowed external pointer, and
- a view over `ByteBufferHandle`.

Do not overload an untagged `void*` and guess its kind. A buffer-backed view
records the last resolved generation. Generic reads refresh on mismatch;
generic writes first prepare the handle for mutation, then refresh. MIR inline
paths either perform the same guard or call a shared resolving helper.

---

## 4. Phase 0 — freeze the current contract and inventory raw access

### Work

1. Record a clean pre-change verification snapshot:
   - `make build`
   - focused binary scripts and decoder/output GTests
   - focused JS typed-array, ArrayBuffer resize/detach/transfer, Buffer, and
     ArrayNum view tests
   - `make test-lambda-baseline`
2. Inventory every binary layout dependency:
   - `String*` variables receiving `get_safe_binary()`
   - `binary->chars`, `binary->len`, `it2s`/`s2it` groupings that admit binary
   - formatter, validator, MarkBuilder/Reader/Editor, GC, field-slot, C2MIR, MIR,
     and FFI paths
3. Inventory every JS raw buffer read/write:
   - `ab->data`, `ta->view->data`, direct `memcpy`/`memset`
   - Buffer methods, crypto, zlib, streams, file/network adapters, DataView,
     Atomics, JIT inline indexed access, resize/detach/transfer
4. Classify each access as read, write, lifetime, or cached/hoisted pointer.
5. Add the inventory/checklist to this document when implementation starts;
   do not begin the layout change with unknown consumers.

### Exit gate

- Baselines are green or every pre-existing failure is recorded.
- Every raw access has an owner and migration phase.
- No generated file is scheduled for manual editing.

### Implementation record (2026-07-15)

Pre-change snapshot on `master` at `d1a1bcb03`:

- `make build`: pass, zero errors/warnings.
- `make test-lambda-baseline`: pass, Input `2105/2105`, Lambda Runtime
  `1270/1270`, combined `3375/3375`.
- `make test262-baseline`: pass, `40261/40261`, zero non-fully-passing,
  zero failures, zero regressions, retry time `0.0s`.
- The first Lambda gate attempt was blocked before tests because the sandbox
  could not update `.git/modules/test/yaml/config`; the authorized rerun is the
  recorded result above, not a product failure.

Binary layout/access inventory:

- Definition/allocation: `lambda/lambda.h`, `lambda/lambda.hpp`, and
  `lambda/lambda-mem.cpp` expose Binary as `String` and allocate inline bytes.
- Runtime producers/consumers: `lambda/build_ast.cpp`, `lambda/lambda-eval*.cpp`,
  `lambda/lambda-data*.cpp`, `lambda/lambda-vector.cpp`,
  `lambda/lambda-proc.cpp`, `lambda/input/input*.cpp`, `lambda/mark_builder.cpp`,
  `lambda/mark_editor.cpp`, `lambda/print.cpp`, `lambda/emit_sexpr.cpp`,
  formatter/validator code, and JS bridge code.
- ABI/lowering: `lambda/transpile.cpp`, `lambda/transpile-call.cpp`,
  `lambda/transpile-mir.cpp`, `lambda/transpile_shared.cpp`,
  `lib/item_tagged.hpp`, and `lib/lambda_typed.hpp` currently group Binary with
  `String*`, `it2s`, and string-shaped field slots.
- GC currently treats Binary as pointer-free inline data. Phase 2 must add its
  external storage edge to the new finalizer without changing the Item tag.
- Compiler literals will remain pool-owned inline Binary payloads during the
  first migration. Runtime-created non-empty Binary values become
  storage-backed. This is the explicit constant-lifetime exception until a
  script storage registry exists; all access remains behind the same Binary
  API.

JS/ArrayNum raw-access inventory:

- Lifecycle and replacement are concentrated in `lambda/js/js_typed_array.cpp`
  and context teardown in `lambda/lambda-mem.cpp`. Existing resize paths assign
  a new `ab->data` without releasing the old allocation; detach/finalization do
  release it.
- Typed-array cached data is refreshed in
  `js_typed_array_refresh_arraynum_view`; optimized indexed paths in
  `js_mir_expression_lowering.cpp` and `js_mir_statement_lowering.cpp` call
  `js_typed_array_current_data_ptr`. These are the generation-guard insertion
  points.
- Direct ArrayBuffer/DataView readers exist in `js_assert.cpp`,
  `js_child_process.cpp`, `js_clipboard.cpp`, `js_crypto.cpp`, `js_runtime.cpp`,
  `js_tls.cpp`, `js_util.cpp`, and `js_zlib.cpp`.
- Direct writers/storage creators exist in `js_clipboard.cpp`, `js_crypto.cpp`,
  `js_globals.cpp`, `js_stream.cpp`, and `js_typed_array.cpp`; accessor-based
  mutable typed-array/Buffer/file/network writers additionally occur in
  `js_buffer.cpp`, `js_fs.cpp`, `js_net.cpp`, `js_stream.cpp`, `js_tls.cpp`, and
  `js_crypto.cpp`. Phase 4 must split the current unqualified data accessor into
  const-resolution and prepare-write APIs and migrate every mutable call site.
- ArrayNum external views originate in `lambda/lambda-data-runtime.cpp`; their
  `ArrayNumShape::base` is traced by `lib/gc/gc_heap.c`, while `view->data` is a
  replaceable raw cache. Backing-kind and generation metadata belong in that
  descriptor rather than being inferred from `base`.

No generated Lua or parser source is part of the edit plan.

---

## 5. Phase 1 — land `ByteStorage` and the finalization seam, unused

### 5.1 Substrate

1. Add `lib/byte_storage.{h,c}` and wire it through
   `build_lambda_config.json`; regenerate build files with `make`.
2. Implement owned and externally wrapped storage, atomic retain/release,
   spans/subspans, checked arithmetic, and release callbacks.
3. Use a dedicated memtrack category if useful; otherwise document the chosen
   existing category. Do not hide allocations as `MEM_CAT_UNKNOWN`.
4. Add unit tests covering empty storage, nested subspans, overflow rejection,
   last-release cleanup, read-only flags, and concurrent retain/release.

### 5.2 GC and pool lifecycle

1. Add a narrow external-payload finalization callback from `gc_heap.c` into
   `lambda-mem.cpp`, or an equivalent typed callback table.
2. The callback must run both for mid-execution sweep and context teardown.
3. Finalizers null released fields so context teardown cannot double-release an
   object already finalized during sweep.
4. Add a script/constant ownership registry for refcounted literal storage, or
   keep constants inline until such a registry exists. A pool-allocated header
   may not silently own an unregistered external allocation.
5. Add GC-stress tests that create and discard many storage owners and prove
   release counts reach zero before context teardown.

The fix-point comment required by `AGENTS.md` belongs where the GC invokes the
external finalizer: explain that refcounted bytes live outside the data zone and
must be released during sweep, not merely at process/context shutdown.

### Exit gate

- ByteStorage tests pass under repeated retain/release and GC stress.
- Leak instrumentation shows no surviving storage after last owner release.
- Runtime behavior is otherwise unchanged because no value uses the substrate.
- `make build` and `make test-lambda-baseline` pass.

### Implementation record (2026-07-15)

- Added `lib/byte_storage.{h,c}` with checked borrowed spans, atomic
  retain/release, owned and callback-wrapped allocations, explicit read-only /
  shared-mutable / external flags, and last-release cleanup.
- The allocation API accepts the caller's `MemCategory`; wrapped storage uses
  the existing `MEM_CAT_CONTAINER` category for its descriptor. No allocation
  is hidden under `MEM_CAT_UNKNOWN`.
- Added a general external-payload destroy seam to `gc_heap_t`. Sweep and heap
  teardown both invoke it; the sweep fix point documents why zone reclamation
  is insufficient for refcounted bytes.
- Added five ByteStorage tests (empty, nested spans, overflow/OOB, callback
  ownership/read-only flags, and contended retain/release) plus the full
  44-test GC suite, including mid-sweep and teardown callback delivery.
  Focused result: `49/49` pass.
- The seam remains unused by runtime values in this phase, so observable value
  behavior is unchanged. Binary registration occurs with the Phase-2 header.
- `make build` completed with zero errors/warnings, and
  `make test-lambda-baseline` passed `3375/3375`.

---

## 6. Phase 2 — migrate Binary behind accessors and enable O(1) slices

### 6.1 Representation

1. Replace `typedef String Binary` with a dedicated Binary header carrying
   length, ASCII metadata, flags, storage, and byte offset.
2. Initially make runtime-created non-empty binaries storage-backed. This keeps
   the first migration mechanically uniform; add small-inline storage only in
   Phase 6 after measurements.
3. Keep the Item tag and `x2it` representation unchanged.
4. Change `get_binary()`/`get_safe_binary()` and binary runtime declarations to
   return `Binary*`.
5. Add debug validation for span bounds and non-null storage.

### 6.2 Producers

Migrate all producers to the constructors rather than constructing headers:

- decoded compiler literals and const-pool registration,
- Mark input,
- `binary(x)` conversions,
- file/network/process reads,
- binary concatenation and slicing,
- MarkBuilder/deep-copy paths,
- JS copy-bridge fallback paths.

Compiler constants must follow the ownership decision from Phase 1. If a
script registry owns their storage, release it in the same script teardown path
that destroys `const_list`; do not attach cleanup to an unrelated global.

### 6.3 Consumers

Migrate all consumers to `binary_data`/`binary_length`:

- equality and total ordering,
- `len`, indexing, membership, iteration, concatenation, and string conversion,
- canonical printing and every formatter,
- raw output and file/network writes,
- validator and shaped-field access,
- MarkReader/Editor and input/document deep copy,
- C2MIR and MIR runtime call signatures,
- JS slot writers that store Lambda-shaped binary fields.

Run a final grep for `String*` binary variables and direct `chars`/`len`
accesses. Each remaining hit needs a comment proving it handles source text,
not decoded Binary layout.

### 6.4 O(1) subviews

1. Change binary range slicing to `heap_binary_slice`.
2. Flatten nested slices directly onto the root `ByteStorage` offset.
3. Preserve current inclusive/exclusive range behavior exactly as tested.
4. Add `binary.copy` only if the public API is already approved; otherwise add
   an internal forced-copy helper and leave the surface for the Tier-2 API
   decision.
5. Keep concatenation allocating a new contiguous block. Rope/builder work is
   outside this phase.

### Tests

- Existing binary decode, element, Mark round-trip, output, and formatter tests
  must pass unchanged.
- Add nested-slice identity/storage tests, embedded-NUL slices, empty/full
  slices, OOB clamps, equality across different offsets, and tiny-view/large-
  owner lifetime tests.
- Add a GC test where only a subview remains live.
- Verify both the normal runtime and the Jube C2MIR path when available; the
  core executable no longer exposes `--c2mir`.

### Exit gate

- Binary observable output is byte-for-byte unchanged.
- Storage-backed slicing performs no payload copy (assert allocation/copy
  counters in a descriptor-level test, not timing).
- `make build`, focused tests, and `make test-lambda-baseline` pass.
- Regenerate `lambda/lambda-embed.h` through the build target; never edit it.

### Implementation record (2026-07-15)

- Replaced the String alias with a dedicated accessor-backed `Binary` header.
  Compiler constants and arena-built Mark values remain explicitly inline
  because those owners have no individual external-payload finalizer; all
  runtime-created non-empty binaries use retained `ByteStorage` spans.
- Migrated compiler and MIR/C2MIR ABI lowering, shaped slots, formatters,
  input/output, MarkBuilder/Editor, procedural/runtime operations, and the JS
  copy bridge to `Binary*` plus `binary_data`/`binary_length`. `Item` character
  access now dispatches Binary independently instead of assuming String layout.
- Runtime finalization releases storage-backed Binary payloads during both GC
  sweep and heap teardown. Allocation-failure descriptors remain safe for the
  same finalizer.
- `heap_binary_slice` flattens storage-backed nested slices to one root storage
  offset without copying. Inline compiler/arena values perform the documented
  one-time promotion copy at that ownership boundary; concatenation remains a
  contiguous allocation.
- Added descriptor-level tests for nested storage identity, embedded NULs,
  full/empty/OOB slices, zero-copy allocation/copy counters, refcounts, and a
  GC cycle where only the subview survives. New focused result: `3/3` pass;
  existing binary bridge/round-trip/output coverage passes `6/6`.
- The generated embedded header was refreshed by `make build`. The core CLI no
  longer exposes `--c2mir`; the Jube C2MIR gate remains in the final ABI gate.
- `make test-lambda-baseline` passed Input `2105/2105`, Lambda Runtime
  `1273/1273`, combined `3378/3378`, with zero failures.

---

## 7. Phase 3 — move JsArrayBuffer onto `ByteBufferHandle`

This phase changes JS ownership but deliberately keeps binary interop copying.
That isolates ArrayBuffer lifecycle correctness before COW sharing is enabled.

### 7.1 JsArrayBuffer migration

1. Replace direct `void* data` ownership with an embedded or owned
   `ByteBufferHandle`.
2. Introduce accessors for const data, mutable prepared data, byte length,
   maximum length, generation, and detach/shared/resizable state.
3. Migrate ArrayBuffer creation, slice, resize, grow, detach, transfer, and
   finalization to handle operations.
4. Fix every old-storage replacement to release the previous reference. No
   assignment may orphan the old allocation.
5. Preserve ECMAScript ordering of argument coercion, detach checks, species
   construction, and errors. Storage refactoring must not reorder observable JS
   side effects.

### 7.2 TypedArray/DataView/Buffer refresh

1. Add `array_num_new_buffer_view` and use it for every JS typed array.
2. Store the handle generation last resolved by the ArrayNum descriptor.
3. Route generic reads through a generation-aware resolver.
4. Route all non-shared writes through one prepare-write resolver even though
   storage is still unique in this phase.
5. Audit Buffer, DataView, crypto, compression, string-decoder, file/network,
   and stream code for cached direct data pointers.
6. Keep SharedArrayBuffer/Atomics on explicit shared-mutable storage and atomic
   access paths.

### 7.3 Optimized-path invalidation

1. Update MIR/JS inline typed-array get/set paths to guard handle generation or
   call the common resolver.
2. Treat resize, detach, transfer, and later COW as storage-invalidating effects
   in loop/hoist analysis.
3. Extend the existing resize/detach/transfer hoist regressions so a storage
   swap inside a loop cannot leave an old pointer live.

The root-cause comment at the invalidation point should state that the
ArrayNum descriptor caches handle-derived geometry/data and therefore cannot
survive a generation-changing buffer operation without refresh.

### Exit gate

- Existing ArrayBuffer, resizable ArrayBuffer, SharedArrayBuffer, DataView,
  typed-array, Buffer, Atomics, and hoist-invalidating regressions pass.
- Storage allocation/release counters balance across resize, detach, transfer,
  and GC.
- The old binary copy bridge still passes, proving no semantic broadening yet.
- `make test-lambda-baseline` and the relevant Test262 baseline gate pass with
  zero regressions.

### Implementation record (2026-07-15)

- `JsArrayBuffer` now embeds `ByteBufferHandle`; allocation, resize, grow,
  detach, transfer, fixed-length transfer, and finalization use the handle API.
  Old storage is released only after a replacement is ready, and every
  identity-preserving storage change advances the generation.
- Every JS TypedArray is an `array_num_new_buffer_view`. Generic and MIR access
  resolves the live handle; the MIR store path prepares writable storage and
  typed-array pointer hoisting is disabled across storage-invalidating effects.
- Fixed-source transfer and coercion-triggered resize regressions found during
  the full Test262 gate were repaired at their root: fixed transfers no longer
  inherit a stale maximum, and a value-coercion resize refreshes ArrayNum
  geometry before the store.
- Focused resize/detach/transfer, DataView, TypedArray bulk, Buffer, crypto, and
  the five exposed Test262 regressions pass. The full final gates are recorded
  in §15 after all phases.

---

## 8. Phase 4 — enable zero-copy binary/JS sharing with COW

### 8.1 Binary to JS

For storage-backed Binary:

1. Retain its storage.
2. Create a fixed, non-shared ArrayBuffer handle whose `storage_offset` and
   `byte_length` describe exactly the binary span. Never shift the storage root
   pointer and lose the address required by its release callback.
3. Wrap it as Uint8Array or Node Buffer using existing constructors/prototypes.
4. The first JS write calls `byte_buffer_prepare_write`; because Binary retains
   the original storage, the handle clones before mutation.

Inline Binary, if introduced later, performs a one-time promotion/copy at this
boundary. That is the only allowed size-policy fallback.

### 8.2 JS to Binary

For Uint8Array, Uint8ClampedArray, Buffer, and DataView:

1. Resolve the current visible byte span after detach/OOB checks.
2. If backed by a non-shared ByteStorage, retain the current allocation and
   create an immutable Binary span using the view's byte offset/length.
3. Later JS writes COW the ArrayBuffer handle; resize swaps its storage; detach
   drops only its reference. The Binary snapshot remains unchanged.
4. For SharedArrayBuffer, unsupported foreign mutable storage, or a handle that
   cannot provide a retained stable allocation, use the explicit copy fallback.

### 8.3 Write-path audit

COW is correct only if **every** mutable path prepares storage first. Audit and
test at least:

- scalar typed-array assignment and DataView setters,
- `.set`, `.fill`, `.copyWithin`, `.reverse`, `.sort`, and same-type bulk copy,
- Node Buffer index writes, `write*`, `fill`, `copy`, swaps, and encoding writes,
- crypto random-fill and other crypto APIs that write supplied buffers,
- zlib/compression output into caller-provided buffers,
- file/network reads into mutable buffers,
- MIR inline stores and optimized bulk paths,
- resizable/shared buffer special paths and Atomics.

Add a debug assertion in the raw mutable-data accessor: non-shared storage with
`refs > 1` may not be returned writable before COW.

### 8.4 Required interop matrix

| Source | Target | Expected sharing rule |
|---|---|---|
| storage-backed binary | Uint8Array / Buffer | share until first JS write, then COW |
| binary subview | Uint8Array / Buffer | share exact offset/length; no parent-chain copy |
| Uint8Array / Uint8ClampedArray / Buffer | binary | immutable snapshot of visible range |
| DataView | binary | immutable snapshot of DataView range |
| resizable ArrayBuffer view | binary | snapshot current allocation/range; later resize does not change binary |
| detached view | binary | existing error/null behavior; never revive storage |
| SharedArrayBuffer view | binary | copy |
| binary | SharedArrayBuffer | copy |

### Tests

- Preserve the existing mutation-isolation regression output.
- Add descriptor-level storage-identity assertions before mutation and
  storage-divergence assertions after COW.
- Cover multiple typed arrays/DataViews over one handle: after COW all JS views
  still see each other's writes while the Binary does not.
- Cover nested binary subviews, non-zero typed-array offsets, empty ranges,
  embedded NULs, detach, transfer, fixed and length-tracking resizable views,
  Buffer methods, GC between sharing and mutation, and repeated COW avoidance
  after the handle becomes unique.
- Add failure-injection tests for allocation failure during COW: the original
  storage and values remain valid, with no partially swapped handle.

### Exit gate

- Eligible conversions demonstrate zero payload copies with instrumentation.
- Mutation isolation remains exact.
- SharedArrayBuffer cases demonstrate an intentional copy.
- Focused JS/Node tests, `make test-lambda-baseline`, Test262 baseline, and
  `make node-baseline` pass with no storage-related regression.

### Implementation record (2026-07-15)

- Storage-backed Binary-to-Uint8Array/Buffer creates a fixed handle over the
  exact retained span. Uint8Array, Uint8ClampedArray, Buffer, and DataView
  convert back by retaining the current non-shared visible range. Inline
  constants and SharedArrayBuffer views use the instrumented copy fallback.
- `byte_buffer_prepare_write` is the sole non-shared mutable allocation gate.
  It clones shared/read-only storage before mutation and leaves the old handle
  completely unchanged on allocation failure. Debug builds assert that a
  shared non-SAB allocation is never returned raw for writing.
- The writer audit covers scalar/bulk TypedArray operations, DataView setters,
  Buffer writes/fill/copy/swaps/encodings, crypto, zlib, TextEncoder,
  structured clone, streams, fs, net, and MIR stores. All sibling JS views
  resolve through the one handle after COW.
- Descriptor tests prove storage identity before mutation, divergence after
  COW, repeated unique writes without re-cloning, exact subview offsets, and
  failure atomicity. The expanded bridge proves sibling TypedArray/DataView
  coherence, non-zero offsets, Buffer isolation, and mandatory
  SharedArrayBuffer snapshot copying.

---

## 9. Phase 5 — complete required ArrayNum substrate convergence

The JS typed-array path already uses ArrayNum operations. This phase removes
remaining ownership ambiguity and makes the shared layer reusable beyond JS.

### Work

1. Make `ArrayNumShape` view-source kind explicit; never infer whether `base`
   means ArrayNum, JS Map, or external storage from pointer shape.
2. Ensure buffer-backed views resolve data and bounds from
   `ByteBufferHandle`, with checked element-size conversion.
3. Keep `array_num_new_external_view` only for stable borrowed storage whose
   lifetime is pinned by `gc_base`; document that it cannot survive a moving or
   replaceable external pointer.
4. Add a retained-ByteStorage view constructor for FFI/mmap/cross-isolate uses
   that have no mutable handle.
5. Consolidate byte-preserving copy/reverse/fill helpers so JS and Lambda
   ArrayNum users do not grow parallel raw-pointer implementations.
6. Review `is_pinned`: buffer-backed external views should not pin unrelated GC
   data. Ordinary views over GC-data-zone ArrayNums keep current pinning until
   the optional owned migration.
7. Document the resulting three legal backing modes in the ArrayNum design doc
   when code lands: GC-zone owned, stable borrowed external, and retained
   ByteStorage/ByteBufferHandle.

### Exit gate

- All ArrayNum scalar, N-D, strided, mutable/read-only view, bulk-operation,
  image, and JS typed-array regressions pass.
- GC tracing keeps the semantic base/handle alive; refcounting keeps the byte
  allocation alive; neither mechanism is substituted for the other.
- No ArrayNum view contains an unowned pointer to replaceable storage.
- `make test-lambda-baseline` and the relevant JS baseline gates pass.

### Implementation record (2026-07-15)

- `ArrayNumShape.backing_kind` now explicitly distinguishes GC-owned, GC-view,
  stable borrowed external, buffer-handle, and retained-ByteStorage modes.
  `base` remains the semantic GC edge; `backing` owns or locates external bytes.
- `array_num_init_derived_view` flattens element offsets and propagates the
  backing contract. GC views pin only their real GC data owner, handle-backed
  views inherit handle/generation without pinning the wrapper, and retained
  storage children take their own reference.
- `array_num_new_storage_view` provides the checked FFI/mmap/cross-isolate
  constructor, rejects mutable views over read-only storage, and releases its
  retained reference in both mid-sweep and context teardown finalization.
  Borrowed external views are documented as stable-pointer-only.
- Existing byte-preserving copy/reverse/fill kernels remain the shared Lambda/JS
  implementation. Focused storage tests pass `6/6`; `make test-lambda-baseline`
  passes Input `2105/2105`, Lambda Runtime `1276/1276`, combined `3381/3381`.

---

## 10. Phase 6 — profile-gated allocation policy and large owned ArrayNum

This phase is optional for declaring binary/JS zero-copy complete. It decides
where the common substrate improves owned numeric arrays instead of assuming
atomic refcounting is universally cheaper.

### 10.1 Small-inline Binary

1. Benchmark header/allocation cost and choose the inline threshold; 64 bytes is
   the design starting point, not a hard-coded conclusion.
2. Add inline representation only if it improves representative binary-heavy
   workloads and total memory.
3. Keep all access behind the same Binary accessors.
4. Verify the promotion/copy boundary and ensure it is never described as
   universal zero-copy for small inline values.

### 10.2 Large owned ArrayNum

1. Measure GC compaction/pinning cost, allocation throughput, and cross-runtime
   copy volume for large numeric arrays.
2. If justified, add an explicit ByteStorage-backed owned ArrayNum mode above a
   measured threshold.
3. Do not overload `data`/`extra` without a tag. Prefer an explicit backing
   descriptor even if it grows the header; update all MIR/C2MIR field offsets
   atomically.
4. Functional views remain read-only; procedural mutable views follow their
   base. If immutable sharing is introduced, mutation uses the same COW
   substrate rather than an ArrayNum-specific copy implementation.
5. Refcount-backed arrays no longer require permanent GC pinning. GC-zone arrays
   retain current behavior.

### Performance rules

- Use release builds only (`make release`).
- Compare outer wall time, allocation counts, bytes copied, peak memory, and GC
  compaction/pinning metrics.
- A representation change does not land on microbenchmark wins alone; Lambda,
  JS, document/image, and binary-stream workloads must show no material loss.

### Allocation-policy decision (2026-07-15)

- Measurements used a release build only. The Binary/JS interop+COW matrix ran
  in `0.02s` warm (`0.34s` first cold import/cache run); representative N-D
  image and linalg workloads each ran in `0.01s`. Descriptor counters also
  prove that nested Binary slices allocate/copy no payload and eligible
  handle sharing retains rather than copies.
- No measured allocation, GC-pinning, or cross-runtime-copy bottleneck
  justified another representation. Compiler/pool constants therefore remain
  inline for owner-lifetime reasons, while runtime non-empty Binary stays
  uniformly storage-backed; no arbitrary byte threshold is introduced.
- Owned ArrayNum remains on the GC data-zone fast path. The explicit retained
  storage view supplies FFI/mmap/cross-isolate reuse without charging every
  local numeric array an atomic refcount. A future owned migration requires a
  workload demonstrating the missing benefit and can reuse the landed tag.

---

## 11. Verification stack

Each phase runs its focused tests first and the real gate before closeout.

### 11.1 Substrate and lifecycle

- ByteStorage unit tests: retain/release, spans, overflow, callback ownership,
  concurrency, read-only wrapping.
- GC stress: dead Binary, live subview, dead ArrayBuffer, detach then GC,
  transfer then GC, context teardown after mid-cycle sweep.
- Allocation-failure tests for clone/resize/transfer/COW atomicity.

### 11.2 Lambda binary

- Existing `binary_decode`, `binary_elements`, `binary_mark_roundtrip`, and
  byte-exact output tests.
- Decoder error cases and formatter goldens unchanged.
- New nested subview, storage identity, tiny-view/large-owner, and GC tests.
- MIR Direct plus Jube C2MIR coverage where available.

### 11.3 ArrayNum and JS

- ArrayNum view/GC/mutable-view/N-D/strided/bulk-operation tests.
- Typed arrays, DataView, Buffer, ArrayBuffer detach/resize/transfer,
  SharedArrayBuffer, Atomics, and loop-hoist invalidation regressions.
- Test262 slices for ArrayBuffer, resizable ArrayBuffer, typed arrays, DataView,
  SharedArrayBuffer, and Atomics before the full baseline.
- Node preliminary Buffer/crypto/zlib/stream/file coverage before
  `make node-baseline`.

### 11.4 Required repository gates

```bash
make build
make build-test
make test-lambda-baseline
make test262-baseline
make node-baseline
```

Run `make test-jube`/the Jube C2MIR gate when the changed runtime ABI is compiled
there. Regenerate `lambda/lambda-embed.h` through its Make target after
`lambda.h` changes. Never update a baseline merely to hide a storage regression.

### Final verification record (2026-07-15)

- `make build`: passed with zero errors and zero warnings. The final ABI audit
  also registered the dedicated `it2x` Binary unbox helper in the named JIT
  import table; generated code no longer has to rely on the old String import.
- `make test-lambda-baseline`: Input `2105/2105`, Lambda Runtime `1276/1276`,
  combined `3381/3381`; the new Binary storage suite is `6/6`.
- `make test262-baseline`: `40261/40261` fully passing, zero non-fully-passing,
  zero failures, zero regressions, and `0.0s` retry time.
- `make node-baseline`: `3528/3528` passing, zero failures, missing tests,
  timeouts, crashes, or regressions. The runner's timing-only slow-list edit
  was discarded rather than changing policy from one host timing sample.
- `make build-jube`: passed after the final ABI/import change. Five Jube Binary
  smokes (`binary_decode`, `binary_elements`, `binary_mark_roundtrip`,
  `binary_js_bridge`, and procedural `proc_binary_output`) matched their
  existing goldens exactly.
- `make test-jube` was also attempted. Its current legacy C2MIR test is not an
  available C2MIR gate: it passes `--c2mir` to `test-batch`, but the current CLI
  ignores that flag in this mode and logs MIR Direct execution. The resulting
  12 object/Radiant failures are the known unsupported/skipped MIR cases, while
  all Binary cases pass. The remaining target failures are unrelated existing
  Jube harness issues (Python shutdown memtrack output and an uninstantiated
  Bash official parameter suite); the target's embedded Test262 and Node runs
  pass. No baseline or storage workaround was added to conceal them.

---

## 12. Expected file map

| Area | Likely files | Responsibility |
|---|---|---|
| Shared storage | `lib/byte_storage.h`, `lib/byte_storage.c`, `lib/atomic.h`, `lib/memtrack.*` | allocation, refcount, spans, release callbacks, diagnostics |
| Build | `build_lambda_config.json` | add sources/tests; generated Lua remains untouched |
| Value ABI | `lambda/lambda.h`, `lambda/lambda.hpp`, generated `lambda/lambda-embed.h` | Binary header/accessors; handle/view declarations; runtime ABI |
| Binary allocation/lifecycle | `lambda/lambda-mem.cpp`, `lambda/lambda-data-runtime.cpp`, `lambda/lambda-vector.cpp` | constructors, finalizers, slices, concat/copy |
| Compiler/constants | `lambda/ast.hpp`, `lambda/build_ast.cpp`, `lambda/runner.cpp`, transpilers | const storage registry/lifetime and ABI lowering |
| Consumers | `lambda/print.cpp`, `lambda/format/`, validator, Mark reader/builder/editor, `lambda-proc.cpp` | use Binary accessors, preserve semantics |
| ArrayNum views | `lambda/lambda-data-runtime.cpp`, `lambda/lambda-vector.cpp`, `lib/gc/gc_heap.c` | backing-kind metadata, handle/storage resolution, tracing/pinning |
| JS buffers/views | `lambda/js/js_typed_array.{h,cpp}`, `lambda/js/js_buffer.cpp`, `lambda/js/js_runtime.cpp` | stable handle, COW, generation, interop, optimized paths |
| External writers | JS crypto/zlib/streams/file/network modules found in Phase 0 | prepare-write contract |
| Tests | `test/lib/`, `test/lambda/`, `test/js/`, `test/node/`, relevant GTests | lifecycle, identity, COW, semantics, regression gates |
| Docs | `Lambda_Type_Binary.md`, ArrayNum/JS typed-array docs, runtime design docs | record the as-built representation after each phase |

This map is a starting inventory, not permission to touch every file. Each
implementation slice should remain narrow and should reuse existing helpers
instead of creating a third byte-copy/view path.

---

## 13. Sequencing, commit boundaries, and rollback

Recommended commit sequence:

1. **Storage primitive + tests**, unused.
2. **GC/pool finalization seam + stress tests**, unused.
3. **Binary representation/accessor migration**, behavior-preserving.
4. **Binary O(1) slices**, focused semantic/performance change.
5. **ArrayBuffer handle migration**, copy bridge retained.
6. **ArrayNum generation-aware buffer views**, copy bridge retained.
7. **Zero-copy bridge + COW**, one interop face at a time:
   Uint8Array, Buffer, Uint8ClampedArray, then DataView.
8. **External writer audit and optimized-path closeout**.
9. **Optional allocation-policy tuning**, only with release measurements.

Do not combine the Binary ABI migration and zero-copy enablement in one
unreviewable change. The copy bridge is the rollback boundary: after the new
storage/handle layers land, interop can remain copying until every writer and
generation guard is proven.

If a phase regresses semantics, restore the copy bridge or the previous
consumer access path while keeping already-verified lower layers. Do not add a
type-specific shadow buffer or bypass COW as a workaround.

---

## 14. Risk register

| Risk | Root cause | Required mitigation |
|---|---|---|
| Binary changes after JS mutation | write path bypasses COW | one mutable accessor; debug shared-write assertion; exhaustive writer audit |
| Typed array uses freed/old bytes | cached raw pointer survives handle swap | generation guard/resolver in generic and JIT paths; invalidation regressions |
| Storage leak | GC/pool owner never releases external ref | Phase-1 finalization seam; release counters; mid-GC tests |
| Double release | detach/transfer/finalizer share ambiguous ownership | move/retain API with explicit consuming semantics; null fields after release |
| Same-ArrayBuffer views diverge after COW | COW attached to one typed-array descriptor instead of handle | COW swaps the shared handle; every view resolves through it |
| SharedArrayBuffer violates immutability | mutable concurrent storage retained as Binary | mandatory snapshot copy in both directions |
| Element/byte offset bug | ArrayNum shape uses elements, storage uses bytes | checked conversion by element size; non-zero-offset and multi-width tests |
| Tiny slice retains huge storage unexpectedly | zero-copy subview keeps root allocation | explicit copy helper; retention tests; later measured heuristic only |
| Atomic refcount overhead regresses small arrays | all ArrayNums forced onto refcount storage | staged/profile-gated owned migration; retain GC-zone fast path |
| Compiler literal leak | pool header owns unregistered external storage | explicit script registry or inline constants until registry lands |
| C2MIR/generated ABI drift | `lambda.h` layout changes without regeneration | regenerate embed header; Jube C2MIR gate |

---

## 15. Completion checklist

- [x] Phase 0 raw-access and baseline inventory recorded
- [x] ByteStorage/span API implemented and unit-tested
- [x] Mid-GC and context-end external storage finalization implemented
- [x] Compiler constant storage ownership explicit
- [x] Binary has a dedicated accessor-backed representation
- [x] All binary producers/consumers migrated; no layout-dependent `String*`
- [x] Binary storage-backed slices are O(1)
- [x] JsArrayBuffer uses a stable ByteBufferHandle
- [x] ArrayNum buffer views are generation-aware and owner-safe
- [x] All mutable JS/Node write paths prepare writable storage
- [x] Eligible binary/typed-array/DataView conversions share storage
- [x] COW preserves Binary immutability and same-handle JS view coherence
- [x] Detach/resize/transfer/GC/failure paths release exactly once
- [x] SharedArrayBuffer conversion copies are explicit and tested
- [x] Existing Phase-1–5 behavior/goldens remain unchanged
- [x] Lambda, Test262, and Node gates pass; Jube ABI build/Binary smokes pass and the legacy C2MIR gate limitation is recorded
- [x] As-built runtime/ArrayNum/JS design docs updated
- [x] Optional small-inline/large-ArrayNum policy decided from release measurements
