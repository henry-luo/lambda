# Removing conservative native-stack rooting from Lambda

**Status:** proposed migration design

**Reviewed:** 2026-07-16

**Current reviewed tree:** `9402b16980e819dbd2feae1763e2cade5d327244`

**Related implementation review:** [Lambda_Stack_MIR.md](./Lambda_Stack_MIR.md)

**Related frame design:** [Lambda_Design_Stack_Frame.md](./Lambda_Design_Stack_Frame.md)

This document answers two questions:

1. What currently blocks removal of conservative native-stack rooting?
2. What procedure should be followed to remove it without creating latent
   use-after-free bugs?

The conclusion is:

> Conservative native-stack scanning is removable and should be retired, but
> it cannot be deleted safely from the current runtime. C2MIR and unaudited
> native/runtime helpers still depend on it. Retirement is a migration of every
> GC-producing execution path to a precise-root contract, followed by a global
> precise-only stress gate.

This proposal does not replace the new root or number side stacks. It completes
their migration by removing the compatibility root source that still scans raw
native stack words.

---

## 1. Current root model

At the beginning of a collection, current Lambda obtains roots from:

```text
registered individual slots
    + registered root ranges
    + exact side-root range [side_root_base, side_root_top)
    + optional explicit extra roots
    + conservative native-stack scan [SP, stack_base)
```

The first four inputs are explicit. The final input guesses whether each aligned
native-stack word might represent an `Item` or raw GC object pointer.

Current collection entry performs:

```text
setjmp(regs)                    # materialize some register state
stack_current = hardware SP
stack_base = _lambda_stack_base
gc_collect_with_root_region(..., stack_base, stack_current,
                            side_root_base, side_root_count)
```

The collector then scans every aligned word from `stack_current` to
`stack_base`, decodes it with `item_to_ptr()`, validates it against an exact GC
allocation, and marks matching objects.

The scan has two opposing failure properties:

- **over-retention:** stale or unrelated pointer-shaped words may keep dead
  objects alive;
- **under-retention:** a live value held only in a register or representation
  not captured by the scan may still be missed.

The side-root stack removes this ambiguity for generated MIR values. The scan
remains because not every other producer has migrated.

### 1.1 What removal means

Removal means deleting the collector's dependence on native stack contents:

```text
remove setjmp used only for GC register flushing
remove SP-to-stack-base root scanning
remove gc_scan_stack()
remove stack_base/stack_current parameters from collector root APIs
remove ASan poisoned-word scan handling
```

It does **not** mean:

- removing the platform native call stack;
- removing `_lambda_stack_base` if stack-overflow protection still uses it;
- removing registered roots or registered ranges;
- removing the precise side-root stack;
- scanning the number side stack;
- changing MIR's native register allocator;
- changing the non-moving GC-object-struct policy.

---

## 2. Required safety invariant

Before the conservative scan can be removed, this invariant must hold globally:

> At every legal GC point, every live GC-managed object must be reachable from
> an explicit registered root, a precise side-root slot, an async/persistent
> root structure, or another already-reachable GC object.

For a native helper, this means:

```text
value becomes live
    -> publish a precise root
    -> call anything that may collect
    -> stop using value
    -> release root
```

For generated code, it means:

```text
produce GC value
    -> publish into exact generated frame/activation storage
    -> cross allocation/call/interrupt safepoint
    -> release or overwrite only after last use
```

A value does not need its own root if it is already reachable through a rooted
owner. For example, an array element is protected by the rooted array after the
element has been stored. Before that store, a newly-created element may require
its own temporary root.

Raw backing-data pointers are not roots. The GC object that owns the backing
data must be rooted.

---

## 3. Current blockers

### B1. C2MIR is conservative-scan-only

The legacy C2MIR path emits generated C without a precise root-frame protocol.
It does not reserve side-root slots or publish all live `Item`/`Container*`
values before calls that may collect.

The existing design deliberately left C2MIR unchanged. It inherits shared
number-stack boxing but not precise GC rooting.

**Removal condition:** choose one of:

1. upgrade C2MIR completely to the same frame, rooting, return, error, and
   unwind contract as MIR-Direct; or
2. retire/disable C2MIR before enabling a globally precise-only collector.

Keeping current C2MIR while deleting the conservative scan is not a valid
option.

### B2. Native C/C++ helpers have no complete local-root contract

Native helpers commonly hold:

- `Item` locals;
- raw `Array*`, `Map*`, `Element*`, `Object*`, `Function*`, or `String*`
  pointers;
- newly allocated objects not yet linked into a rooted owner;
- arguments across nested runtime calls;
- intermediate containers during construction.

Any call to `heap_calloc()`, data allocation, or another transitively allocating
helper may trigger collection. Today, a native local often survives because its
bits happen to be on the scanned stack.

**Removal condition:** every native value live across a may-GC boundary must be
covered by an explicit root/handle contract.

### B3. `LambdaRootGuard` is only a pilot, not a runtime-wide discipline

The current C++ RAII guard is the right primitive shape:

```cpp
LambdaRootGuard roots((Context*)context);
roots.root(item);
```

It appends exact roots above the current side-root watermark and restores the
incoming watermark when destroyed. Current use is limited to a small typed-array
view-construction pilot in `lambda-data-runtime.cpp`.

The existence of the guard therefore does not imply complete host-helper
coverage.

**Removal condition:** make the guard/handle contract universal for allocating
C++ helpers, and provide an equivalent structured API for C helpers.

### B4. Function arguments are not automatically a native-helper contract

A MIR caller may have rooted an argument, but a native helper cannot safely
assume that every possible caller did so. The same helper may be reached from:

- MIR-Direct;
- C2MIR;
- the evaluator/interpreter;
- JS or another language runtime;
- Jube/native modules;
- a callback or async continuation.

Furthermore, objects created inside the helper are not protected by the
caller's frame.

**Removal condition:** APIs that may collect must express argument lifetime as
one of:

- a `Handle`-style borrowed value whose caller is required to root it;
- a value rooted by the callee at entry;
- an immutable/non-GC scalar;
- an explicitly documented no-GC input interval.

### B5. There is no enforced may-GC/no-GC effect system

Rooting is required across calls that may collect, but this property is not
encoded consistently in function signatures or checked transitively.

An apparently harmless helper can become may-GC later because a nested callee
starts allocating. Manual local reasoning then becomes stale.

**Removal condition:** establish and enforce:

```text
NO_GC      function cannot directly or transitively collect
MAY_GC     caller must publish all live roots before the call
GC_POINT   explicit allocator/collector boundary
```

The annotation mechanism may begin as comments/macros plus linting, but final
retirement should have static or generated verification rather than convention
alone.

### B6. The evaluator and non-MIR execution paths use native locals

Not every Lambda operation is executed inside a MIR-Direct function frame.
Parsing/evaluation bridges, procedural evaluation, validation, conversion,
runtime constructors, and fallback paths use hand-written native code.

Registered global roots do not protect temporary locals in these paths.

**Removal condition:** inventory and migrate every execution path that can call
the GC allocator while holding GC values.

### B7. Polyglot and JS host runtimes have broad persistent roots but incomplete
temporary-root proof

JS, Python, Ruby, Bash, concurrency, and other subsystems register many globals,
queues, module arrays, callbacks, and async structures as stable roots/ranges.
That is necessary and should remain.

It does not prove that every temporary local inside thousands of lines of native
runtime helpers is precise. A global root protects only the value currently in
that registered slot/range.

**Removal condition:** audit persistent storage separately from transient
native locals, and verify both.

### B8. Jube/native modules do not yet have a mandatory handle ABI

`JubeHostAPI` exposes root registration facilities, but a module can still hold
raw values across an allocating host call without a statically enforced
`Handle`/root lifetime.

Safety is therefore dependent on module discipline and the conservative
backstop.

**Removal condition:** make precise rooting part of the public host ABI and
version/validate modules against it.

### B9. Async and callback lifetimes cross native activations

Async tasks, event queues, promises, mailboxes, closures, and callbacks can
outlive the native frame that created them. Many have registered persistent
slots/ranges already, but the handoff interval matters:

```text
create value in local
    -> enqueue/store into registered activation
    -> local frame returns
```

The value must remain rooted until the persistent owner has taken responsibility.

**Removal condition:** audit every handoff boundary and require an explicit
ownership transition.

### B10. Raw pointer and boxed `Item` representations share machine words

MIR and native code frequently carry pointers physically as 64-bit integer
words. The semantic distinction between:

- an inline scalar;
- a boxed `Item`;
- a raw GC object pointer;
- a raw non-GC backing-data pointer;

is not recoverable from a native local's C/MIR storage type alone.

**Removal condition:** rooting decisions must retain semantic `TypeId`/ownership
metadata until the value is placed in an explicit root. Do not infer roots from
`MIR_T_I64` or `uint64_t` alone.

### B11. Current tests are allowed to pass through accidental conservative roots

A missing precise root may remain on the native stack and be marked by
`gc_scan_stack()`. Normal tests can therefore pass while the precise-root model
is incomplete.

**Removal condition:** add precise-only and forced-GC test modes that do not use
conservative candidates to preserve objects.

### B12. Mixed activation chains prevent a trivial per-entrypoint switch

A MIR-Direct function may call an unaudited native helper; that helper may call
back into generated code; GC may occur at any nested level. The top-level
execution mode does not describe the complete active call chain.

**Removal condition:** either audit the entire runtime globally or maintain a
sound activation capability protocol proving that no conservative-dependent
frame is active. A simple `if (is_mir_direct) skip_scan` flag is unsafe.

---

## 4. Target end state

The final root model should be:

```text
registered stable slots/ranges
    + generated side-root frames
    + native helper root guards/handles
    + async/persistent activation roots
    + GC object graph tracing
```

The collector should not inspect untyped native stack memory.

### 4.1 Generated code

- Lambda MIR-Direct and JS MIR publish exact roots in generated frame slots.
- Root slots are reserved/restored structurally.
- Async suspension re-homes values into registered activation storage.
- C2MIR either implements the same contract or is not available in precise-only
  builds/runs.

### 4.2 Native code

- Every may-GC native API accepts handles or roots its inputs.
- Newly allocated objects are rooted before the next legal GC point unless
  already stored in a rooted owner.
- Persistent values use registered slots/ranges with explicit lifetime.
- C++ uses RAII guards.
- C uses a structured begin/root/end API that cannot silently skip restoration.
- No-GC functions are transitively verified.

### 4.3 Collector

The root phase becomes:

```text
mark registered slots
mark registered ranges
mark exact side-root range
mark explicit extra roots
trace marked object graph
```

No `setjmp` root flush, native stack bounds, raw-word scan, or ASan poisoned-word
logic remains in GC.

---

## 5. Recommended removal procedure

### Phase 0 — Freeze the contract and inventory all GC points

Create one authoritative list of operations that may collect:

- `heap_calloc()` / GC object allocation;
- data-zone allocation that can invoke the collection callback;
- explicit `heap_gc_collect()`;
- runtime helpers transitively calling either allocator;
- language callbacks that may execute arbitrary user code;
- interrupt/event-loop paths that may allocate;
- error construction/reporting paths that may allocate.

For each exported/internal helper, classify it as `NO_GC` or `MAY_GC`.

Deliverables:

- may-GC call graph or generated report;
- list of current execution engines and entrypoints;
- list of persistent root stores/ranges and their lifetimes;
- list of C/C++ helper files containing allocations;
- explicit ownership rules for arguments, returns, and newly-created values.

Do not begin by deleting `gc_scan_stack()`.

### Phase 1 — Add migration diagnostics while keeping behavior safe

Add two development modes.

#### 1A. Shadow conservative-root reporting

During root marking:

1. mark all precise roots first;
2. conservatively scan the native stack;
3. count objects that were unmarked before the conservative scan;
4. record representative object type/address and collection context;
5. still mark them, preserving current safety.

Call these **scan-exclusive candidates**.

A scan-exclusive candidate is not automatically a bug; it can be a stale false
positive. It is a review target showing where conservative scanning changes the
mark set.

Useful counters:

```text
gc_precise_root_count
gc_conservative_candidate_words
gc_conservative_new_objects
gc_conservative_new_objects_by_type
gc_conservative_scan_bytes
```

Debug output must use the existing logging API and a distinctive prefix.

#### 1B. Precise-only collector mode

Add a test-only/runtime-debug switch that skips conservative marking entirely.
It should be usable by each test harness and execution engine.

The mode must be visibly reported in logs/test summaries. It must never silently
fall back to the scan after a failure.

### Phase 2 — Add legal forced-GC stress

Run collection at aggressively varied **legal** may-GC boundaries, especially
immediately before allocations and nested runtime calls.

Do not invent impossible GC points in the middle of an allocator after it has
created a result but before that result can be returned. The stress mode must
respect the same safepoint contract the final runtime will use.

Recommended schedules:

- collect before every eligible allocation;
- collect every Nth allocation for multiple N values;
- randomized deterministic allocation schedules with logged seeds;
- collect at callback/async handoff boundaries;
- collect during exception/error propagation;
- collect under deep recursion and large expression frames.

Run both shadow-scan and precise-only variants.

### Phase 3 — Complete the native root API

Keep `LambdaRootGuard` as the C++ frame primitive, then add handle shapes needed
for API clarity.

Proposed concepts:

```text
LambdaRootGuard       owns a dynamic root watermark extent
LambdaHandle          borrowed read-only rooted Item
LambdaMutableHandle   rooted slot whose Item may change
LambdaPersistentRoot  registered root with explicit unregister/lifetime
```

For C modules, add an explicit structured equivalent, for example:

```c
LambdaRootFrame roots = lambda_root_frame_begin(context);
lambda_root_frame_push(&roots, item);
...
lambda_root_frame_end(&roots);
```

Exact names are implementation decisions. The required properties are:

- no heap allocation on normal root push;
- bounds checking consistent with generated side-root frames;
- correct nesting with generated calls;
- exact restoration on every normal/error path;
- non-copyable RAII ownership in C++;
- explicit cleanup structure in C;
- support for mutable rooted values;
- clear distinction between temporary and persistent roots.

Current GC object structs are non-moving, so a copied `Item` root protects the
native local's pointee. The API should nevertheless avoid assuming that copied
roots would be sufficient for a future moving-object collector; mutable handle
slots make the contract clearer.

### Phase 4 — Migrate native helpers bottom-up

Start with leaf allocators/constructors and work upward through their callers.

For each function:

1. identify every GC-managed argument and local;
2. identify first definition and last use;
3. identify every intervening may-GC call;
4. root before the first such call;
5. root newly allocated results before the next such call;
6. transfer ownership when storing into a rooted container/activation;
7. release the temporary root only after the last use/transfer;
8. test with forced GC and precise-only mode.

Priority order:

1. core object/container constructors and mutators;
2. shared string, map, array, element, VMap, error, and function helpers;
3. evaluator/interpreter and procedural runtime;
4. JS runtime helpers;
5. async, scheduler, event-loop, promise, and callback helpers;
6. Python, Ruby, Bash, document, validator, and conversion bridges;
7. Jube/native module boundary;
8. remaining platform and optional-module helpers.

#### Native audit example

Unsafe shape:

```cpp
Item helper(Item input) {
    Map* map = create_map();       // map is only a native local
    Item value = create_value();   // may collect
    map_set(map, value);           // stale map is possible
    return {.item = (uint64_t)map};
}
```

Precise shape:

```cpp
Item helper(Context* runtime, Item input) {
    LambdaRootGuard roots(runtime);
    if (!roots.root(input)) return ItemError;

    Map* map = create_map();
    Item map_item = {.item = (uint64_t)map};
    if (!roots.root(map_item)) return ItemError;

    Item value = create_value();
    if (!roots.root(value)) return ItemError;

    map_set(map, value);
    return map_item;
}
```

This is illustrative. Real code should avoid redundant rooting when a proven
rooted owner already owns the value.

### Phase 5 — Verify generated engines

#### Lambda MIR-Direct

- audit root predicates for raw pointers and boxed GC `Item` types;
- verify every allocation/call result that survives another may-GC point;
- verify root-slot lifetime across branches, loops, errors, and cleanup;
- verify rootless-frame elision does not omit required roots;
- verify async spill ownership and suspension handoff;
- verify main, handlers, view functions, and generated wrappers.

#### JavaScript MIR

- repeat the same checks for ordinary functions, methods, constructors,
  generators, async functions, module entrypoints, and `js_main`;
- distinguish persistent JS runtime state from transient native helper locals;
- stress module loading, promises, events, typed arrays, and Node compatibility.

#### MIR interpreter

The MIR interpreter executes the same generated side-root prologues/epilogues,
but still calls shared native helpers. Test it independently in precise-only
mode.

#### C2MIR

Recommended choice: upgrade the entire generated-frame contract rather than
adding isolated root calls. Required scope includes:

- exact root frame reservation/restoration;
- root publication for variables and temporaries;
- may-GC call boundaries;
- return/error ownership;
- async/capture behavior if supported;
- unwind/reset integration;
- precise-only regression coverage.

A partial C2MIR patch would preserve hidden dependence and should not be used as
the retirement gate.

### Phase 6 — Migrate persistent and asynchronous ownership

Audit every registered slot/range and async root container for:

- stable backing address while registered;
- correct count/capacity after growth;
- unregister before storage destruction/movement;
- initialization of unused entries to non-pointer values;
- write ordering before a value becomes visible to callbacks;
- root ownership during resize/copy;
- handoff from local temporary to persistent slot;
- reset behavior between scripts, tests, workers, and reused contexts.

Registered ranges should remain a supported precise mechanism. They are not the
same as conservative scanning and do not need removal.

### Phase 7 — Enforce the host/module ABI

Update the Jube/native host contract so a module cannot legitimately keep a raw
GC pointer across a may-GC call without one of:

- a host-provided handle;
- a temporary root frame;
- a persistent registered root;
- an API guarantee that the interval is no-GC.

Recommended enforcement:

- version the rooting-capable host ABI;
- expose root-frame/handle operations directly;
- document which callbacks may collect;
- reject or isolate modules using an older conservative-dependent ABI in
  precise-only mode;
- provide examples for temporary, mutable, persistent, and async roots;
- add module-level forced-GC tests.

### Phase 8 — Make precise-only mode a mandatory gate

Run all relevant suites with the native-stack scan disabled:

```text
Lambda core baseline
Lambda extended/unit tests
MIR interpreter mode
C2MIR suite after migration
JS/test262 baseline with zero retries
Node compatibility baseline
async/concurrency tests
Python/Ruby/Bash/Jube tests
document/layout paths that execute Lambda/JS
forced-GC stress variants
ASan/UBSan variants
release-build smoke and performance tests
```

Tests must include:

- values held across one and several nested allocations;
- raw container pointers;
- newly allocated values before container insertion;
- function arguments and return values;
- errors and cleanup calls;
- closures and captured values;
- async suspension/resumption;
- callback/event-loop handoff;
- persistent root-range resize/destruction;
- deep recursion and stack overflow recovery;
- worker/thread context reuse;
- GC during module initialization.

The acceptance criterion is not necessarily zero scan-exclusive candidates in
shadow mode because stale words can be legitimate false positives. The gate is:

1. every scan-exclusive candidate observed in targeted stress is classified or
   eliminated;
2. all suites pass in precise-only mode;
3. static rooting analysis reports no may-GC hazards;
4. no execution engine remains documented as conservative-only.

### Phase 9 — Staged rollout

Recommended rollout sequence:

1. land diagnostics with conservative marking still authoritative;
2. migrate core helpers and run precise-only in focused CI;
3. migrate language/async/host subsystems;
4. upgrade or retire C2MIR;
5. run precise-only for all CI suites while production still keeps fallback;
6. make precise-only the default in development builds;
7. make precise-only the default in release builds with an emergency diagnostic
   fallback for one transition period;
8. remove the fallback after stable coverage;
9. delete conservative scanner code and obsolete API parameters.

Do not enable/disable scanning merely from the top-level script engine. Mixed
native/generated activation chains make that test insufficient.

### Phase 10 — Delete the conservative path

After all gates pass:

1. remove `setjmp(regs)` from `heap_gc_collect()` when no longer needed for root
   capture;
2. stop reading/passing `stack_current` and GC `stack_base`;
3. remove `stack_base` and `stack_current` parameters from
   `gc_collect_with_root_region()` and `gc_collect()`;
4. remove `gc_scan_stack()`;
5. remove its ASan poison-detection helpers/attributes;
6. remove conservative scan profiling counters and compatibility flags;
7. retain stack-bound initialization required for stack-overflow protection;
8. update GC documentation and frame reports;
9. rerun the complete precise-only acceptance matrix;
10. measure GC pause time, retained-object counts, and RSS before/after.

---

## 6. Static analysis and linting recommendation

The migration should add a rooting-hazard analysis rather than depend only on
tests. A useful first version can operate over C/C++ source annotations and the
call graph.

It should flag:

```text
GC pointer local defined
    + may-GC call before last use
    + no active precise root/handle
```

It should understand:

- `Item` and raw GC object pointer types;
- owner/backing-pointer relationships;
- `LambdaRootGuard` scope;
- registered persistent slots;
- may-GC transitive calls;
- no-GC regions;
- ownership transfer into rooted containers;
- async handoff;
- return escape.

High-value warnings include:

- raw `Container*` live across `heap_calloc()`;
- newly allocated object followed by another allocation before rooting;
- local `Item` overwritten without updating its mutable root;
- root guard destroyed before cleanup/error calls;
- dynamic registered range whose backing address can move;
- host callback retaining a borrowed handle past its valid scope;
- C cleanup path missing root-frame restoration.

Static analysis should be a required CI gate before final deletion.

---

## 7. Performance expectations

Removing conservative scanning eliminates per-collection work proportional to
the active native stack depth:

```text
O(words between SP and stack_base)
```

Expected benefits:

- no `setjmp` register-flush overhead in the GC driver;
- no raw native-stack word walk;
- fewer false-positive live objects;
- less mark/trace/survivor work caused by false retention;
- simpler ASan behavior;
- GC correctness independent of optimizer spill choices.

Costs that remain:

- generated side-root stores;
- precise side-root scanning;
- registered slot/range scanning;
- host guard pushes;
- object graph tracing;
- number-frame operations.

Therefore removal should improve GC-heavy workloads and retention precision. It
does not by itself remove MIR instruction growth or side-root publication cost
on non-GC-heavy workloads.

Measure separately:

- total execution time;
- GC count and total pause time;
- native-stack bytes formerly scanned;
- exact side-root slots scanned;
- objects marked only by the old conservative pass;
- live bytes after collection;
- peak and steady-state RSS;
- generated MIR/code size;
- test262 sync/async wall time.

All performance measurements must use release builds.

---

## 8. Actions that are explicitly unsafe

Do not:

- simply comment out `gc_scan_stack()` and rely on normal tests;
- assume all MIR locals are spilled to the native stack;
- assume `setjmp` captures every caller-saved live value;
- assume a MIR caller always roots every native helper argument;
- treat registered globals as coverage for local temporaries;
- root raw backing-data pointers instead of their owning GC object;
- clear the scan only for a top-level MIR entrypoint while unaudited native
  helpers remain active;
- keep current C2MIR enabled under a precise-only collector;
- add ad-hoc `volatile` variables as a rooting mechanism;
- add more conservative pointer heuristics as a substitute for explicit roots;
- delete `_lambda_stack_base` without preserving stack-overflow handling;
- declare completion before forced-GC precise-only suites pass.

---

## 9. Acceptance checklist

### Rooting architecture

- [ ] Lambda MIR-Direct exact-root audit complete.
- [ ] JavaScript MIR exact-root audit complete.
- [ ] MIR interpreter precise-only audit complete.
- [ ] C2MIR upgraded completely or removed from precise-only builds.
- [ ] Evaluator/procedural/fallback paths migrated.
- [ ] Native helper root API complete for C and C++.
- [ ] Jube/native module handle ABI enforced.
- [ ] Async and persistent handoffs audited.
- [ ] Registered ranges have stable-address/lifetime proof.
- [ ] No-GC/may-GC contract enforced transitively.

### Diagnostics

- [ ] Shadow conservative-root reporting available.
- [ ] Precise-only collector mode available.
- [ ] Forced-GC legal-safepoint schedules available.
- [ ] Static rooting-hazard analysis enabled in CI.
- [ ] No unexplained required conservative-only roots remain.

### Verification

- [ ] Lambda baseline passes in precise-only mode.
- [ ] Lambda extended/unit tests pass.
- [ ] MIR interpreter tests pass.
- [ ] C2MIR tests pass after migration, if retained.
- [ ] test262 baseline passes with zero failures and zero retries.
- [ ] Node compatibility baseline passes.
- [ ] Async/concurrency and worker reuse tests pass.
- [ ] Polyglot/Jube tests pass.
- [ ] Forced-GC matrix passes.
- [ ] ASan/UBSan matrix passes.
- [ ] Release smoke/performance comparison is recorded.

### Final deletion

- [ ] GC no longer receives native stack bounds.
- [ ] `gc_scan_stack()` removed.
- [ ] GC-specific `setjmp` flush removed.
- [ ] ASan conservative-scan helpers removed.
- [ ] Stack-overflow bounds remain functional.
- [ ] Documentation updated to precise-only root model.

---

## 10. Source map

| Concern | Current source/design |
|---|---|
| GC driver, register flush, stack bounds | `lambda/lambda-mem.cpp` |
| Conservative scan and root marking | `lib/gc/gc_heap.c` |
| Collector root API | `lib/gc/gc_heap.h` |
| Side-stack reservation and watermarks | `lib/side_stack.c`, `lib/side_stack.h` |
| Runtime `Context` side-stack fields | `lambda/lambda.h` |
| C++ host-helper guard | `lambda/lambda.hpp` |
| Current pilot guard use | `lambda/lambda-data-runtime.cpp` |
| Lambda MIR root emission | `lambda/transpile-mir.cpp` |
| JavaScript MIR root emission | `lambda/js/js_mir_hashmap_scope_utils.cpp` and JS MIR lowering files |
| C2MIR generation | `lambda/transpile.cpp` |
| Persistent root registration | `lambda/lambda-mem.cpp`, language runtime state files |
| Async task roots | `lambda/concurrency.cpp` and JS event-loop/runtime files |
| Jube host API | `lambda/jube/jube_registry.cpp` and public Jube API definitions |
| Existing migration rationale | `vibe/Lambda_Design_Stack_Frame.md` |
| Implemented frame status | `vibe/Lambda_Impl_Stack_Frame.md` |
| Before/after MIR review | `vibe/Lambda_Stack_MIR.md` |

---

## 11. Final recommendation

Proceed with removal as a dedicated multi-stage migration, beginning with
diagnostics and host-root APIs rather than collector deletion.

The critical path is:

```text
instrument scan-exclusive roots
    -> add precise-only/forced-GC modes
    -> enforce may-GC + handle contracts
    -> migrate native helpers and async handoffs
    -> upgrade or retire C2MIR
    -> pass global precise-only gates
    -> delete conservative scan
```

The conservative scan should remain enabled until this chain is complete. Once
complete, retaining it would add scan cost and false retention without providing
required correctness coverage, so it should then be removed rather than kept as
a permanent fallback.
