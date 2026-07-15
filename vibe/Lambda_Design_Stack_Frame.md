# Lambda Stack Frame Design — Precise Rooting + Frame-Scoped Number Stack

**Status:** v1 sketch + review refinements captured; open issues OS1–OS11 under discussion
**Date:** 2026-07-15
**Context:** A unified stack-frame architecture for Lambda and hosted languages (LambdaJS, future Jube guests) that addresses two runtime debts at once: (1) JIT GC rooting goes through heap-allocated shadow frames with a C call per rooted store, and (2) the numeric nursery is never collected — an unbounded leak by design. Successor-in-spirit to the G1 honest-rooting fix (`vibe/Lambda_Issue_GC_Root (fixed).md`); feeds the G2 host-API rooting clause (`vibe/Lambda_Semantics_Features.md` §1.8). Detailed current-state design lives in `doc/dev/lambda/LR_08_Memory_and_GC.md`.

---

## 1. Current design, and its issues

Facts verified in code on 2026-07-15; file:line references are to that state of the tree.

### 1.1 GC rooting today

The root set has four layers (LR_08 §6):

1. **Registered slots/ranges** — `heap_register_gc_root` / `heap_register_gc_root_range` (`lambda-mem.cpp:343`, `:357`) for BSS globals, JS module-var arrays (`js_runtime_state.cpp:186`), and runtime singletons (`js_current_this` etc.).
2. **Per-call JIT root frames** — the G1 machinery. A thread-local stack of heap-allocated `JitGcRootFrame` records, each holding a chain of 64-slot `JitGcRootBlock`s (`lambda-mem.cpp:24`). Frames materialize lazily: `heap_jit_gc_root_frame_enter` only bumps a depth counter (`:463`); the first `..._set` at that depth allocates the frame (free-list cached, max 256). The MIR-Direct transpiler emits `enter`/`exit` around every function body and a **C call per rooted store** (`store_gc_root_slot` → `emit_call_void_2("heap_jit_gc_root_frame_set", ...)`, `transpile-mir.cpp:520`). Which locals get slots is decided by the post-G1 honest-typing predicate `should_gc_root_var` (`transpile-mir.cpp:497`): `MIR_T_P`, or `type_id >= LMD_TYPE_INT64 && != LMD_TYPE_FLOAT`.
3. **Conservative C-stack scan** — `gc_scan_stack` (`lib/gc/gc_heap.c:1579`) walks every aligned stack word from SP to `_lambda_stack_base`, decodes via `item_to_ptr`, marks GC objects. Paired with a `setjmp` register flush in the driver `heap_gc_collect` (`lambda-mem.cpp:309`).
4. **Precise heap tracing** from those roots (per-`TypeId` walks).

The C2MIR (legacy) path emits **no rooting at all** — `transpile.cpp` has zero root-frame calls; it relies entirely on layer 3. So do all hand-written C runtime helpers.

**Issues:**

| # | Issue |
|---|-------|
| R-I1 | Every rooted store is a full C call. Rooted-heavy code pays call overhead where one store instruction should suffice. |
| R-I2 | Even rootless functions pay two C calls (`enter`/`exit`) per invocation. |
| R-I3 | Root frames are heap allocations (mitigated by the free-list, but still malloc traffic, pointer chasing in the scan, and a 64-slot granularity that scans unused slots). |
| R-I4 | Slot access is a linear walk of the block chain (`jit_gc_root_frame_get_block`, `lambda-mem.cpp:368`). |
| R-I5 | The conservative scan both **over-retains** (any stack word that looks like an Item pins an object) and has a documented **under-retention hole**: a value held only in a register the compiler happens not to spill is missed; the `setjmp` flush is a mitigation, not a guarantee (LR_08 §9). Plus ASan poisoning contortions. |
| R-I6 | C2MIR code and all C host functions have *only* the conservative net — the same class of exposure that produced BUG-001 on the MIR-Direct path. |
| R-I7 | G2 is unanswered: there is no rooting contract for native modules / `JubeHostAPI`; safety is emergent from the conservative scan. |

### 1.2 Number nursery today

Compound scalars (int64, uint64, double, DateTime, decimal) are tagged pointers because the inline `Item` payload is **56 bits** (high byte = TypeId). The current homes:

- **Doubles are already out** — `push_d` is `flt2it` (`lambda-mem.cpp:688`), the double-boxing-v2 inline encoding (`ITEM_DBL_MASK`); only tiny/subnormal out-of-band doubles fall to `box_float_cold` heap boxing.
- **int64 / uint64 / DateTime** go through `push_l` / `push_k` into the **numeric nursery**: `gc_nursery_alloc_long` etc. (`lib/gc/gc_nursery.c:101`), a bump allocator over 32 KB blocks whose header states the trade-off plainly: *"Replaces num_stack with a simpler, non-frame-coupled allocator. Values allocated from the nursery persist until nursery_destroy() — no frame-based reset"* (`gc_nursery.h:14–15`). Pointers are stable forever; nothing is ever reclaimed until the whole nursery dies.
- DateTime is exactly one packed 64-bit word (`lib/datetime.h:18`), so all nursery types are uniform 8-byte values.
- The old frame-coupled `num_stack` is gone; only its vocabulary survives (`push_*` names, stale comments at `lambda.h:1461`, `lambda-data-runtime.cpp:148`). Both transpilers share this runtime — C2MIR emits `push_d`/`push_l` calls via its boxing table (`transpile.cpp:75`).

**Issues:**

| # | Issue |
|---|-------|
| N-I1 | **Never collected.** Long-running processes leak every boxed numeric temporary ever created (flagged in `vibe/Lambda_Type_Double_Boxing.md`). The historical fix direction (frame-coupled reset) was abandoned because scalars escape frames. |
| N-I2 | Scalars participate in GC rooting (`INT64`/`DTIME` are rootable TypeIds) even though they are semantically pure values — rooting traffic and root-set noise for things that should never be GC's concern. |
| N-I3 | Every boxing is a C call. |
| N-I4 | Nursery pointers are shared across frames, giving value-semantics scalars an accidental identity and lifetime coupling. |

---

## 2. Prior art

What each system contributes to the new direction:

- **SpiderMonkey exact rooting** (`JS::Rooted<T>` / `JS::Handle<T>`). Root slots are stack-resident objects chained into a per-context linked list threaded through the native frames; the collector walks the chain. Mozilla migrated *off* conservative stack scanning onto this (~2012) precisely to escape the over/under-retention problems in R-I5. `Handle` is the API-boundary discipline — the direct model for our G2 clause. **Take: chained stack-resident roots are proven at production scale; the RAII handle class is the host-function story.**
- **OCaml `CAMLparam`/`CAMLlocal`.** C stubs declare frame-local root blocks that macro-chain into a TLS list (`caml_local_roots`). Decades of evidence that hand-written host functions can carry precise rooting with a couple of macros. **Take: the C-helper pattern, and its known failure mode (forgetting the macro) — which our conservative-scan backstop covers during migration (SF9).**
- **Lua's value stack.** All VM values live on a per-coroutine contiguous stack; the C API works exclusively via push/pop with integer watermarks (`lua_gettop`/`lua_settop`); host functions receive and return values *on the stack*. **Take: the watermark model for the number stack, and the proof that host functions integrate cleanly when the "stack" is a per-thread region rather than the native frame.**
- **LLVM shadow-stack GC strategy** (`llvm.gcroot` lowering). Same chained-frame shape generated by a compiler; its documented cost center is exactly the redundant stores — motivating our static per-function sizing and inline stores. **Take: the codegen-emitted variant works; keep stores minimal.**
- **V8 `HandleScope`.** Arena of handles with scope watermarks. V8 needs double indirection because its GC *moves* objects; Lambda's collector is non-moving ("Dual-Zone Non-Moving Mark-and-Sweep", LR_08 §2), so our slots can hold direct Item bits — strictly cheaper than V8's model. **Take: scope-watermark ergonomics; and the reason we get away with less.**
- **JVM/.NET precise stack maps.** The theoretical end-game: zero runtime stores, GC reads register/stack liveness from compiler-emitted maps. Requires deep codegen support that MIR does not provide (single-tier, no map emission). **Rejected for Jube — the shadow-frame family is the attainable point on the curve.** (Consistent with G5: no tiering/deopt ambitions.)

---

## 3. The v1 design, and review refinements

### 3.1 v1 sketch (as proposed)

```
frame {
    last_root_pointer;      // link/watermark for GC roots
    last_nursery_pointer;   // link/watermark for the number stack
    [ N slots of GC root ][ N slots of number stack ]   // auto-reserved on each frame
}
```

- First N−1 slots directly usable; on reaching the Nth, `alloca()` a bigger block on the same native frame and let slot N point at it (items in the overflow block carry a distinguishing tag).
- N is decided by profiling and lives in a **named constant** — never hardcoded.
- Root-allocation code as simple as possible; inlined into host functions.
- **Numbers out of GC entirely**: double, int64/uint64, DateTime are always copied by value. Stack vars, numeric expressions, fn/pn params and returns push/pop number-stack slots; assignment into a collection always copies.

The principles this encodes (adopted as the design ledger):

- **SF1 — Frame-scoped, heap-free hot path.** Rooting and numeric temporaries live with the executing frame; no heap allocation, no C call, on the common path.
- **SF2 — Scalars are values, out of GC.** double/int64/uint64/DateTime never enter the root set or the trace; their lifetime is frame-shaped, and every escape is a copy *by design* (aligned with C4 value semantics and the K13 thread-agnostic capture rule — this architecture is concurrency-ready).
- **SF3 — Primitives inline everywhere.** The push/root operations must be a few instructions, cheap enough that no hotness decision is ever needed.
- **SF4 — Tunable named constants.** All reserved sizes are `constexpr`/`#define` named constants set from profiling, e.g. `FRAME_ROOT_SLOTS_DEFAULT`.

### 3.2 Refinements (agreed in review 2026-07-15)

**SF5 — Static frame sizing for JIT code; no dynamic overflow, no slot tagging, no mid-function alloca.**
The transpiler already knows each function's exact rooted-slot count at compile time (`mt->jit_root_next` is a compile-time counter; number-slot counts are equally static). So JIT frames are sized *exactly* at entry — the Nth-slot-overflow mechanism, its special tagging, and its `alloca()` all disappear for generated code. This also dodges a known landmine: the codebase deliberately avoids `MIR_ALLOCA` ("MIR inlining ALLOCA bug on ARM64", `js_mir_function_collection_class_inference.cpp:2859`, `js_mir_expression_lowering.cpp:12369`; "dynamic, so it grows the stack per box/unbox in hot loops", `transpile-mir.cpp:1090`). If a dynamic path is ever needed, it is a *counted* extension record linked into the same chain — never per-item tags.

**SF6 — Watermarked per-thread side stacks, not in-frame slot arrays.** *(The key reshape.)*
Two per-thread contiguous regions — a **root stack** and a **number stack** — with the frame keeping only the two watermarks from the v1 header (`last_root_pointer`, `last_nursery_pointer` become saved region tops). Frame entry saves both tops and bumps them by the static counts; every exit path restores them.

Why side stacks beat in-frame arrays:

1. **The host-function problem.** `push_l`/`push_k` are called from hundreds of C runtime helpers. A C helper cannot reach into its JIT caller's native frame, and the helper's own C frame dies on return — in-frame number slots are unreachable exactly where most boxing happens. With side stacks, `push_l` is an inline bump of the TLS number-stack top; the value survives until the *owning* frame restores its watermark.
2. **The collector's root walk becomes trivial**: the live root set is precisely `[region_base, root_top)` — one linear, contiguous, precise scan. No chain walk, no per-frame headers to decode, no unused-slot scanning (vs. today's 64-slot block granularity).
3. **No overflow mechanism at all**: region growth handles it. Addresses must stay stable (outstanding Items point into the number stack), so growth is virtual reservation + demand paging (SF13) — never a moving `realloc`.
4. Both stacks are inherently per-thread/per-isolate (`__thread`, like all current GC state) — clean under K11–K18 tasks and RC2 page isolates.

**SF12 — Two strictly separate regions.** *(decided 2026-07-15)*
The root stack and number stack are separate mappings, never zones of one region. The collector precisely scans the root stack as `[base, top)` — every slot is tagged Item bits — and must never see the number stack, whose slots are **raw** 8-byte payloads (a raw double/int64 bit pattern can masquerade as a plausible tagged pointer). Sharing a region would require per-slot discrimination tags — exactly the complexity SF6 exists to eliminate. They also grow to different profiles (number traffic ≫ root traffic). Both are uniform `uint64_t` arrays (double, int64/uint64, DateTime are all exactly 64 bits), so the implementation is symmetric: two TLS tops, two watermarks per frame.

**SF13 — Virtual reservation + demand paging; explicit limit checks.** *(decided 2026-07-15)*
Each region is a large up-front virtual mapping (initial budget e.g. 64 MB number / 16 MB root per thread — named constants per SF4):
- **macOS/Linux**: one anonymous `PROT_READ|PROT_WRITE` `mmap` of the full budget; the OS demand-pages, so resident = touched pages while 64-bit virtual space is free. No explicit commit step.
- **Windows**: `VirtualAlloc(MEM_RESERVE)` + `MEM_COMMIT` as the watermark advances — a small new platform shim in `lib/` (nothing wraps this today; libuv doesn't).
- **Overflow detection**: explicit compare-and-branch, amortized by SF5 — one check at frame entry covers all of that frame's statically-counted slots; no per-push check in JIT code. C helpers pushing dynamically get the check inlined in the push helper. Exhaustion raises the same catchable stack-overflow error as the C-stack guard (OS8 fail-fast). A trailing guard page is optional hardening, not the primary mechanism — signal-only recovery is too fragile to lean on.
- **Shrinking**: watermark pops never return physical pages; the GC driver `madvise(MADV_FREE)`/`MEM_RESET`s above a high-water mark after deep-recursion spikes.
- **Chained fixed segments** are rejected for the main design (a frame's contiguous static slot block forces wasted segment tails, and the root walk degrades from one linear scan to per-segment) and kept only as the documented fallback for a hypothetical target without cheap virtual reservation.

Cost of the reshape: slot addresses are TLS-relative rather than frame-pointer-relative. The JIT loads the two tops into registers at entry (one TLS read each) and uses register-relative stores thereafter — effectively the same store cost as in-frame slots.

**SF7 — No hotness detection.**
The v1 open question "how do we know a function is hot?" is dissolved rather than answered: make the primitives unconditionally cheap. JIT code emits the raw ops inline by construction; C host functions get `static inline` push helpers and a small C+ RAII root guard (the `Rooted` shape) that clang inlines at `-O2`. At 1–3 instructions there is nothing left for a hotness heuristic to save, and profile-guided tiering is an explicit non-goal (G5). If per-function counters are ever wanted, the JS exec-profile hook (`js_exec_profile_note_mir_call`) is the existing place to hang them.

**SF8 — The RAII root guard is the G2 answer.**
The same handle class that C helpers use internally becomes the explicit rooting clause of `JubeHostAPI` v1: native modules receive `Handle`-style views and use the guard for anything held across a call that can collect. One discipline, designed once, spanning runtime helpers and the module ABI.

**SF9 — Conservative-scan retirement is a migration, not a switch.**
End state: scalars out of GC + precise contiguous root stack ⇒ the conservative C-stack scan (with its ASan hacks, false pinning, and spill-reliance hole) can be deleted. But today it is the *only* protection for every C helper holding a raw `Container*` local. Sequence: land the side stacks (correctness immediate, conservative scan still on), migrate helpers to the guard incrementally, retire the scan per-module once its coverage is redundant. The scan is the safety net that makes SF8's migration incremental instead of a flag-day.

**SF10 — Small-int64 inlining companion.**
Pack int64 values that fit 56 bits inline in the Item (the same trick as `LMD_TYPE_INT`'s `get_int56`), leaving the number stack to carry only genuinely wide int64/uint64, DateTime, and container copy-outs. Doubles are already inline (v2). This shrinks number-stack traffic to a trickle before the architecture even lands — worth doing first as an independent step.

**SF11 — Roots hold only true references.**
With scalars out of GC, the rooting predicate tightens to "GC-managed pointer types and `ANY`" — `INT64`/`DTIME` leave `should_gc_root_var`. Root-set noise drops; the honest-typing invariant from the G1 fix becomes even more literal.

### 3.3 Mechanics sketch (to be elaborated per §4)

- **TLS state**: `root_stack_top`, `num_stack_top` (plus region base/limit for growth checks).
- **JIT prologue**: load both tops; save as the frame's two watermarks; bump by the function's static counts. **Epilogue (every return path, as today's `exit` sites)**: restore both. A function with zero rooted slots and zero number slots emits *nothing*.
- **Rooted store**: one register-relative store. **Boxing** (`push_l` successor): inline bump + store + tag; the slow path (region full → commit/grow) is a rarely-taken call.
- **Collector**: precise linear scan of `[root_base, root_top)`; number stack invisible to GC; conservative C-stack scan retained during migration (SF9).
- **Unwind**: every recovery boundary (REPL recovery, watchdog recovery kits, stack-overflow guard) snapshots and restores both tops — see OS4.
- **C2MIR**: generated C can use the same TLS inline helpers — see OS6.

---

## 4. Open issues (OS ledger — to tackle next)

**OS1 — The escape re-homing table.** SF2 means a scalar's storage dies with its frame; every cross-frame flow of a boxed-scalar Item needs an explicit re-homing mechanism. Deliverable: a flow-by-flow table covering — reads out of containers (**copy-out** onto the current number stack — never return a pointer into container storage, else `let x = m.n; drop m` dangles), writes into containers (**copy-in**, see OS2), closure capture into envs, error payloads (`T^E` values crossing frames), varargs/spread, and the JS boundary (JS number ⇔ float is uniform per N1–N9, but int64/BigInt egress paths must re-home). The **return-value** row is RESOLVED — see SF14.

**SF14 — Return values: multi-lane register returns, not number-stack round-trips.** *(decided 2026-07-15)*
Two options were weighed: (1) callee pushes the scalar through the number stack (Lua-style claim-by-bump), vs. (2) a widened return carrying the scalar in a second lane. **Option 2 wins for JIT→JIT calls**, on verified MIR facts:
- MIR multi-value returns ride **registers** for MIR-to-MIR calls, beyond the C ABI: ARM64 up to 8 int results x0–x7 (`mir-gen-aarch64.c:983`); **x86-64 exactly 2** int results RAX:RDX — a 3rd int result is a hard codegen error ("can not handle this combination of return values", `mir-gen-x86_64.c:977`). So `[item, scalar]` is free; **`[item, scalar, error]` is impossible on x86-64** — the universal 3-lane variant is rejected.
- The **error lane is redundant next to an Item lane** (errors already travel as `ItemError` in the tag — two channels = two sources of truth) but **valuable as the second lane of native returns**: today `can_raise` forces any `^E` function to return boxed ANY (`transpile-mir.cpp:11525`), and native paths use domain-stealing sentinels (`INT64_ERROR`). Per-signature return shapes, chosen at transpile time, never 3 lanes:

| Signature | Shape |
|---|---|
| returns ANY/boxed, no wide scalar | `[item]` (unchanged) |
| returns ANY (incl. `ANY^E`), may carry wide scalar | `[item, scalar]` — tag says whether lane 2 is live |
| native scalar, cannot raise | `[scalar]` (exists today) |
| native scalar, `^E` | `[scalar, error]` — un-deopts `can_raise`, retires sentinels |

Note `ANY^E` with a possible wide scalar does **not** need `[item, scalar, error]`: `T^E` is a sum, not a product — a return is a value *or* an error, never both — and the Item lane's tag byte discriminates all three mutually exclusive outcomes (plain value / wide-scalar marker with payload in lane 2 / `ItemError`). The dedicated error lane exists only for native returns because a raw scalar has no tag bits to encode "error instead" — which is precisely the gap the `INT64_ERROR` sentinel papers over today.

- Why option 2 beats option 1: the common case (returned scalar consumed immediately in arithmetic) touches memory **zero** times vs. option 1's store+load; the escape case ties (one push, but paid only when needed, decided by the caller — the only party that knows if the value escapes); and option 1's claim-by-bump has a clobber window (value unclaimed between callee watermark-restore and caller claim; `f() + g()` interleavings) that becomes a forever-invariant for both transpilers. Option 2 dissolves the OS1 return row instead of implementing it.
- **C helpers need no change**: helpers establish no watermark frame, so `push_l` inside a helper homes the value in the *calling JIT frame's* region — returned tagged pointers are caller-homed by construction (an SF6 property). Two-lane protocol is JIT→JIT only; the transpiler already distinguishes helper calls from Lambda-fn calls per call site.
- Residuals: ANY-returning protos become 2-lane (non-scalar returns write a dummy lane-2 operand — MIR ret requires all results; typically move-eliminated); MIR interpreter handles `nres=2` (multi-results are core MIR; only `mir2c` lacks them); C2MIR keeps today's boxed single-Item returns (frozen path).

**OS2 — The 56-bit problem: where does a wide scalar live inside a container?** A full 64-bit int64 cannot fit an 8-byte tagged Item slot — this is *why* the heap boxing exists. Candidate mechanisms: (a) container-owned side buffer for wide scalars, freed with the container (keeps them out of the *global* GC while non-dangling; reads copy out per OS1); (b) wide (16-byte) slots for maps/envs — memory cost, layout churn; (c) heap-box only the wide residue — partially contradicts SF2, but SF10 makes the residue rare. Same question for closure envs. Decide per container kind; ArrayNum already stores unboxed natives and is unaffected.

**OS3 — Async, generators, and suspended `pn` frames.** A suspended state machine has **no native stack frame** — its rooted locals and number slots cannot live on the side stacks across a suspension point. The `async_slot`/`async_store_var` machinery already gives async locals a heap home; the design must define the sync-home (side stacks) vs suspended-home (heap state machine) split, and the rule for values in flight at a `wait`/yield boundary. Interacts directly with `start` tasks (K11–K18) and the JS event loop. Likely rule: at suspension, live scalars/roots are copied into the state machine; on resume, re-established — i.e. suspension is itself an escape point in the OS1 table.

**OS4 — Non-local unwinding catalog.** With heap frames, a missed exit leaks; with side-stack watermarks, a missed restore leaves the tops pointing into dead territory — the next scan walks garbage (worse than a leak). Catalog every path that bypasses normal epilogues: REPL error recovery, the JS watchdog recovery-kit chain (JT4b), the signal-based C-stack-overflow guard, any `longjmp` in vendored deps. Contract: each recovery boundary snapshots both tops on entry and restores on catch. Verify whether any current path unwinds across JIT frames without a boundary (if so, that is a latent bug *today* for the depth counter too).

**OS5 — Region sizing policy.** Mechanism now decided (SF12 separate regions, SF13 virtual reservation + demand paging + explicit checks); remaining opens: the actual per-thread budget constants (profile-driven per SF4), whether budgets differ for main isolate vs. worker tasks vs. Radiant page isolates (RC2; K15 workers already reserve 256 MB C stacks on the same virtual-cost-is-free philosophy), and the madvise/decommit cadence in the GC driver.

**OS6 — C2MIR scope.** The generated-C path can adopt the same TLS inline helpers with a small emitter change (it already routes all boxing through `push_*`). Decide: migrate it (cheap, removes R-I6 for generated code) or leave it conservative-only as a frozen path. Leaning migrate-the-boxing, keep rooting conservative (C2MIR is legacy; U21 keeps it mechanical-only).

**OS7 — Migration order and acceptance gates.** Staged: (0) SF10 small-int64 inlining, independent; (1) side-stack **rooting** for MIR-Direct behind a flag, conservative scan still on — gates: full lambda+JS baselines, `RuntimeError_StackOverflow` fail-fast, deltablue, havlak+push BUG-001 repro, AWFY within noise on release; (2) number stack + `push_*` rehoming, nursery retired — gates add long-run memory ceiling test (the N-I1 leak becomes measurable and must be gone); (3) helper migration to the RAII guard, module by module; (4) conservative-scan retirement per module. Each stage lands alone and soaks, per the P0.2 precedent.

**OS8 — Deep recursion budget.** Per-frame watermark reservations mean recursion depth × (root+number slots) of side-stack usage. The regions must exhaust *cleanly* (raise the same catchable stack-overflow error as the C-stack guard) and the StackOverflow test semantics must be preserved: fail fast, never balloon.

**OS9 — Scalar pointer-identity audit.** Confirm nothing depends on boxed-scalar pointer identity (map keys, caches, interning, `is`-style comparisons). The formal semantics mandates value equality for scalars, and copies-everywhere makes identity unobservable — but verify VMap host paths, JS object keys, and the ArrayNum `==` representation-sensitivity issue (task_38782787) don't hide an identity assumption.

**OS10 — End-state of `gc_nursery` and the `push_*` vocabulary.** When stage 2 lands: delete the numeric nursery entirely (the *data* nursery in `gc_data_zone` is unrelated and stays); rename or re-document `push_l`/`push_d`/`push_k` so the API finally matches its mechanics (the current names are fossils of the pre-nursery num_stack — third naming era, get it right this time).

**OS11 — MIR-interpreter mode.** Runtime helpers are shared, so interp-executed MIR calls the same TLS primitives — but confirm the interpreter establishes frame watermarks identically for interpreted frames (U26 keeps the MIR interp; it must not become a rooting hole).

---

## 5. Summary

| | Today | This design |
|---|---|---|
| Rooted store | C call into heap block chain | 1 inline store |
| Rootless function overhead | 2 C calls | zero |
| Root walk at GC | chain + block walk, 64-slot granularity, plus snapshot copy | one linear scan of `[base, top)` |
| Boxed int64/DateTime | C call, bump into never-freed nursery | inline bump, freed at frame exit |
| Scalars in GC | rooted + traced | invisible to GC (SF2) |
| C host functions | conservative scan only | inline push + RAII guard (→ G2 clause) |
| Conservative scan | load-bearing | backstop → retired per-module (SF9) |

Verdict from review: feasible, well-precedented (SpiderMonkey/OCaml chained roots; Lua watermarked value stack), and faster than the current machinery on every axis — *provided* OS1 (escape re-homing) and OS3 (suspended frames) are designed before any code lands, and OS4 (unwind contract) lands with stage 1.
