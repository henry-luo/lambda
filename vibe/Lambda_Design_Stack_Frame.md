# Lambda Stack Frame Design — Precise Rooting + Frame-Scoped Number Stack

**Status:** IMPLEMENTED (Lambda + LambdaJS) — SF1–SF20 and OS1–OS11 are closed for both MIR-Direct transpilers. The phase records are `vibe/Lambda_Impl_Stack_Frame.md` and `vibe/Lambda_Impl_Stack_Frame_JS.md`.
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

**Implemented refinement (2026-07-15):** root slots follow the exact static scheme above. The number frame reserves one static scratch slot only for a widened return; shared boxing helpers bump within the current function extent and the epilogue restores its saved watermark. This keeps all lifetime/GC invariants and avoids per-box native-stack allocation, but is less static than the original number-slot ideal. Telemetry distinguishes the reserved scratch count from dynamic boxes.

**SF6 — Watermarked per-thread side stacks, not in-frame slot arrays.** *(The key reshape.)*
Two per-thread contiguous regions — a **root stack** and a **number stack** — with the frame keeping only the two watermarks from the v1 header (`last_root_pointer`, `last_nursery_pointer` become saved region tops). Frame entry saves both tops, reserves the exact static root count plus any return scratch, and every exit path restores them; helper boxing may advance the number top inside that extent per the SF5 implementation refinement.

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

**OS1 — The escape re-homing table.** RESOLVED — all rows closed (2026-07-15). SF2 means a scalar's storage dies with its frame; the complete flow-by-flow table:

| Flow | Direction | Mechanism |
|---|---|---|
| argument passing (incl. varargs / spread-supplied args) | downward | **none needed** — SF19 downward safety |
| return values | upward | SF14 multi-lane register returns |
| container writes; rest-param array materialization | outliving | SF15 copy-in to the container's own tail |
| container reads; spread source reads | inward | SF16 storage-class rule (immortal → reference; GC-heap → copy) |
| closure capture + captured-var write-back | outliving | SF18 env tail region |
| error payloads (`T^E` crossing frames) | upward | error is a **plain error** (GC object) or a **wrapped map** — wide scalars inside ride SF15 container storage, GC-managed with the map *(decided 2026-07-15)* |
| JS BigInt egress | — | **not a wide scalar**: BigInt ⇔ `integer`/decimal per N1–N9, a GC heap object; no number-stack involvement *(decided 2026-07-15)* |
| suspension (`wait`/yield in async `pn`) | outliving | SF20 async-frame tail; suspension = re-homing barrier |
| module globals / REPL top-level bindings | outliving (beyond one exec) | single-run: exec base frame spans the run (imports included; results copied to the output pool at the boundary); **REPL: one persistent base watermark frame per session** — numbers accumulate per-session, strictly better than the per-process nursery *(added with nursery-retirement audit, 2026-07-15)* |

**SF19 — Argument flows are downward-safe; varargs/spread need no new mechanism.** *(decided 2026-07-15)*
Watermarks stack LIFO: a caller's number-stack region cannot pop while its callee runs, so every value the caller passes down — positional args, Lambda variadic args (`...` last-param marker, `grammar.js:566`; variadic sys-funcs; MIR vararg protos via `emit_vararg_call`, `transpile-mir.cpp:752`), JS rest/spread args, and the JS transient call-argument stack (`js_args_push`) — remains valid for the entire call. Zero re-homing on the way down. The two operations varargs/spread perform land on already-resolved rows: rest-param materialization is a container write (SF15 copy-in, sources still alive underneath), and spread reads are container reads (SF16). General principle: **only upward flows (returns) and outliving stores (containers, envs, suspension) re-home; downward flows never do.**

**SF18 — Closure capture: the env is an SF15-style container with a statically-sized tail.** *(decided 2026-07-15)*
Facts: envs are GC-heap arrays of 8-byte Item cells populated by boxing each capture at closure creation (`transpile-mir.cpp:2357`), and Lambda closures have **persistent mutable captured state** — assignments in a `pn` closure body re-box and write back to the env cell on every mutation (`transpile-mir.cpp:9244`). Today both paths produce immortal-nursery pointers; under SF2 they would dangle (frame's number stack dies, closure outlives it). Resolution — no new mechanism, envs join SF15:
- **Env-owned tail region, statically sized.** `cap_count` is known at transpile time and envs never grow: reserve one 8-byte tail slot per potentially-wide capture (ANY / int64 / uint64 / DateTime / out-of-band double); narrow statically-typed captures reserve none. Capture and write-back write the payload into the capture's **own** tail slot and store the rebased tagged Item in the cell — in-place overwrite is safe because each capture owns its slot. Payload lifetime = env lifetime, GC-managed for free; SF15 invariant holds.
- **Reads follow SF16 unchanged: copy out** (or direct unboxed load into a register when immediately consumed). No "safe within the call" reference carve-out: write-back creates aliasing — `let a = captured_x; captured_x = other` would mutate `a` behind its back through a tail-slot reference, a value-semantics violation. One uniform rule: GC-heap storage → copy.
- **Concurrency already fenced at the language level**: K13 forbids mutable capture in `start` (negative test `start_mutable_capture.ls`).
- **JS uses the same mechanism AND the same double-boxing model** *(user correction 2026-07-15)*: JS numbers share Lambda's inline-double encoding, and the **out-of-band residue (tiny/subnormal doubles failing `ITEM_DBL_MASK`) becomes a number-stack resident in JS exactly as in Lambda** — replacing today's `box_float_cold` GC-heap boxing. JS envs therefore reserve tail slots for number-typed captures too. Side effect: `box_float_cold` retires, closing the last path by which a plain double could touch the GC — SF2 becomes literally complete for doubles.
- Verification item: env GC tracing today (bare `heap_calloc` Item array, conservative data-buffer fallback) — a tail region is harmless under conservative tracing (raw payloads at worst false-pin); if envs ever get precise tracing, the layout needs an explicit cell-count/tail split like containers' `extra`.
Closes OS1's capture row and OS2's env half.

**SF14 — Return values: two logical lanes, not number-stack round-trips.** *(decided 2026-07-15; portable ABI refined during implementation)*
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

**Implemented portable ABI (2026-07-15):** native MIR multi-result lowering on ARM64 lost the primary lane when a nested call fed the return. The logical lane shapes above remain, but their physical transport is one normal machine return plus `Context::mir_return_lane` for the secondary scalar/error bits. The primary value is held in the function's one reserved number scratch slot across epilogue cleanup, the watermark is restored, and the secondary lane is published immediately before return. Callers consume and re-home it at once. This preserves the two-lane semantics on all backends without relying on the faulty multi-result path. Language-level native `i64^` returns consequently preserve the full domain; only legacy C system-function calls retain the historical sentinel boundary.

**OS2 — The 56-bit problem: where does a wide scalar live inside a container?** RESOLVED for containers — see SF15/SF16. Lambda already implements the container-owned side region: wide scalars live in the **tail of the container's own `items` buffer** (growing down from `items[capacity-1]`), Item slots hold tagged pointers into that tail, and `extra` counts them (`lambda-eval.cpp:5628`, `lambda.h:702`). The **closure-env half is RESOLVED too** — envs use the same tail-region mechanism, statically sized (SF18). OS2 fully closed.

**SF15 — Unify the `extra` mechanism; JS arrays migrate off the raw-pointer overload.** *(IMPLEMENTED 2026-07-15; JS resolution amended 2026-07-15 — reserved tail slot; the JsArray-subtype preference is REJECTED)*
`extra` is triply overloaded today: (1) Lambda generic Array/List/Element — wide-scalar tail count; (2) ArrayNum `is_ndim`/`is_view` — `ArrayNumShape*` (`lambda.h:718`); (3) JS arrays — companion props `Map*` (`MAP_KIND_ARRAY_PROPS`, `lambda.h:637`, `:747`). Unification: meaning (1) becomes canonical for generic containers. **ArrayNum's overload does not conflict** — it stores unboxed natives, never boxed wide-scalar Items, so the shape pointer stays. Only JS arrays truly collide (a JS array can need props *and*, under polyglot interop, wide scalars).

**SF15-J — JS props migrate into a flag-gated reserved tail slot** *(IMPLEMENTED 2026-07-15; replaces the earlier "JsArray subtype" preference)*:
- **Why the subtype loses: identity.** `Array` headers are shared by reference across the Lambda/JS boundary, and the discriminator problem is symmetric — TypeId alone cannot distinguish a props-bearing array, so *either* design needs a flag bit, which already solves the read side (flag clear → no props → `undefined`, no invalid field read, no boundary reallocation). The real differentiator is the **write side**: JS assigning a named prop to a Lambda-born array. A bigger `JsArray` struct forces a new header allocation — an identity split (two objects, diverging mutations; `items` indirection lets *buffers* move, never *headers*). The side-table escape (Array* → props map) reintroduces the WeakMap pin-forever problem. The reserved-slot design promotes in place: growth machinery already relocates the items buffer while the header stays put (`expand_list`, `js_array_install_runtime_items`) — same header, both sides keep their reference.
- **Mechanism:** the companion props map (`MAP_KIND_ARRAY_PROPS` / `MAP_KIND_ARRAY_SPARSE`) is stored as a **tagged MAP Item in a reserved tail slot anchored at `items[capacity-1]`**, gated by a new Container flag `CONTAINER_FLAG_JS_PROPS`; the wide-scalar tail grows down *beneath* it. `extra` becomes uniformly **a count of tail entries** (the props slot counts as 1; wide-scalar count = `extra - 1` when the flag is set), so every shared helper's arithmetic — `length + extra + 2 > capacity` growth checks (`lambda-data.cpp:720`), concat totals, clone sizing — is correct by construction; today those same expressions treat a `Map*` as a count.
- **The flag is load-bearing (SF12 reasoning):** no code path may interpret a tail word as a pointer without `CONTAINER_FLAG_JS_PROPS`. All accounting lives in central accessors (`js_array_props` / `js_array_set_props` / a `tail_reserved()` anchor helper); the `+1` never appears inline at a use site.
- **GC gets simpler, not more complex:** today the trace walk guesses — `gc_mark_possible_item(extra)` (`gc_heap.c:1004`), the same raw-word-as-pointer ambiguity SF12 banned from the root stack. After migration the props slot is a tagged Item traced *precisely* behind the flag, and the conservative guess is deleted. No new struct: hardcoded trace offsets (`gc_heap.c:929–1145`) and both transpilers' emitted member offsets are untouched.
- **Lazy allocation:** the slot is reserved on first prop attach; flag set ⟺ slot holds a valid tagged Map Item. Common-case JS arrays (indexed elements only) pay nothing. Memory vs the subtype is identical (8 bytes per props-bearing array); indexed load vs fixed-offset field is noise.
- **Implementation record:** `vibe/Lambda_Impl_Stack_Frame_JS2.md`; the checked census and provenance audit are in `vibe/Lambda_Impl_Stack_Frame_JS2_Inventory.md`. The migration is complete, including precise tracing, both buffer-growth paths, dense-boundary auditing, and the polyglot int64/DateTime/out-of-band-double matrix, so the phase-2 J3d prerequisite is cleared.
**Hard invariant** (new): *a container's wide-scalar Items point only into its own buffer* — enforced at every clone/concat/copy-in site. ⚠️ Verification item: the concat path (`lambda-eval.cpp:350`) memcpys Item slots without copying/rebasing tail payloads — result Items point into the source arrays' tails; if a source is collected while the result lives, they dangle. Latent today; fixed by the invariant.

**SF16 — Reads of wide scalars out of containers: reference iff immortal storage, else copy.** *(decided 2026-07-15)*
Two-path read: containers in **const-pool or input-arena storage** (non-GC, storage outlives any reader — same lifetime contract as input strings) return a tagged reference into the tail region, zero copy. Containers on the **GC heap copy out to the number stack** — *even if never mutated*: the discriminator is **storage class, not mutability**. An immutable GC container can be collected while a reference is outstanding (the wide-scalar Item is an interior pointer into the data buffer; precise tracing has no liveness edge from it to the container), and mutable containers additionally relocate their buffers on growth. Mechanism: an `is_immortal`-class container flag (16-bit `flags` field exists) set at construction by input parsers and the const-pool builder; one flag test per boxed-scalar read. JS follows the same rules: JS numbers share Lambda's double-boxing (SF18), so JS wide-scalar reads = out-of-band doubles plus polyglot int64/DateTime.

**OS3 — Async, generators, and suspended `pn` frames.** RESOLVED — see SF20.

**SF20 — Suspended frames: the async frame is one more tail-bearing container; suspension is a re-homing barrier.** *(decided 2026-07-15)*
Facts (in-flight `start` implementation): an async `pn` gets a heap `LambdaAsyncFrame` (`concurrency.cpp:77`) — an `Item* slots` array GC-registered as a root range — allocated from the **plain C heap** (`mem_calloc`, `concurrency.cpp:980`), owned per-task as a linked list with a cursor that *reuses* frames across suspensions (one frame per async-call position). The transpiler **writes through**: every async-local write boxes into the frame (`async_store_var`, `transpile-mir.cpp:1448`); resume reloads all slots (`async_restore_vars`). Suspension is a normal return (SF17-consistent). Today `emit_box` produces immortal-nursery pointers so slots survive suspension; under SF2 they would point into a popped (or worse, another thread's) number-stack region.
Resolution — same shape as SF15/SF18:
- **Tail-bearing frame, statically sized**: `async_next_slot` is a compile-time counter; the frame's single `mem_calloc` block carries the Item slots plus one 8-byte tail entry per potentially-wide async local. `async_store_var` of a wide scalar writes the payload into the slot's own tail entry and stores the rebased Item; in-place overwrite safe (each slot owns its entry). Only the **Item region** is registered as a GC root range — the raw-payload tail is never scanned.
- **Suspension-barrier invariant**: nothing reachable from a suspended state machine points into *any* thread's side stacks. This is what makes Stage-B cross-thread resume safe — and the shared malloc heap is what makes the frame readable from the resuming thread (thread-local side stacks never could be).
- **Heap allocation is structurally required, not incidental**: task lifetimes are **not LIFO** (task A suspends, B suspends, A completes first) — a stack allocator needs reverse-order release; suspension/completion order is arbitrary. The unifying principle: **LIFO lifetimes → side stacks; non-LIFO lifetimes (containers, envs, async frames) → owned storage.** A per-task arena for frames/slots/tails (freed wholesale at task completion) is a legitimate later locality optimization — everything in it shares the task's lifetime.
- **Restore follows SF16: copy out** (or direct native loads via `emit_unbox`). No reference-into-tail carve-out — write-through mutates tail entries in place; `let b = a` aliases would observe later mutations (same argument as SF18).
- **Cleanup that falls out**: async-slotted vars currently get *both* an async slot and a side root slot (`update_gc_root_slot` does both) — redundant; the GC-registered frame is their root. Async vars skip root-slot allocation.
- **Write-through vs. write-at-suspend** is an optimization, not correctness: write-through costs a C call per async-local write; persisting only suspension-point-live locals (which K2-R splitting identifies) is cheaper. Start with write-through; optimize with liveness later.
- Adjacent, already covered: mailbox `send`/`receive` deep-copies per K13 (wide scalars land in message storage under container rules); generator `yield` values go upward via SF14 lanes; JS async state gets the same treatment incl. out-of-band doubles (SF18).
- ⚠️ Verification item: **temporaries live across a mid-expression `wait`** (`a + wait(t) + b`) — confirm state-machine compilation spills them into async slots, not just named locals.

**OS4 — Non-local unwinding.** RESOLVED — see SF17. Lambda's convention (no longjmp, no exceptions; every error incl. JS exceptions propagates as return values through normal epilogues) collapses the general problem; the residue is three verified longjmp sites plus a transpiler discipline.

**SF17 — Unwind/watermark contract.** *(decided 2026-07-15)*
Lambda's error convention — no longjmp, no exception throw (except thread abortion); errors and JS exceptions propagate as return values through proper stack unwinding — means watermark restores normally always execute. The complete exception list, verified in code:
1. **Stack-overflow guard**: SIGSEGV handler `siglongjmp`s over the whole live JIT frame stack to `_lambda_recovery_point` (`runner.cpp:1543`). 2. **Batch crash/timeout recovery** (`main.cpp:1318`, `:1329`, dev-only). 3. **Batch MIR-error recovery** (`main.cpp:1305`, transpile-time, no frames active).
Contract: each site snapshots both watermarks where the recovery point is armed (`sigsetjmp`) and restores on the recovery branch — two pointer stores; since these boundaries sit at top-of-execution, restore ≈ reset-to-base. (Successor to today's `jit_gc_root_frame_cache_clear`-on-heap-reset, `lambda-mem.cpp:797` — strictly less machinery.)
Residual rules:
- **Thread abortion**: per-thread regions die with the thread; on worker/isolate **reuse**, reset both tops to base and discard root-stack contents before any GC on that thread (O(1) — a design win vs. walking frame chains).
- **Single-epilogue discipline** (load-bearing): all returns in JIT code — normal, error propagation, `?`, raise — branch to one epilogue block per function, so watermark restore is structural, not per-site. Epilogue **ordering**: scope cleanup (auto-close/errdefer, `transpile_task_scope_unwind`) runs *before* watermark restore — cleanup calls can allocate/GC while the in-flight return/error value still needs its root slot.
- **Foreign unwinding never crosses JIT frames**: C++ exceptions from parsers/deps are contained at the module boundary (already de-facto fatal through MIR frames — no unwind tables; now stated as a rule).
- **Between-scripts/REPL reset**: `runtime_reset_heap` resets both tops.

**OS5 — Region sizing policy.** CLOSED for Lambda + JS (2026-07-15). Separate reservations are 16 MiB for roots and 64 MiB for numbers. Static-slot telemetry is available through `LAMBDA_MIR_LOG_FRAME_SLOTS`; explicit frame checks report stack overflow; POSIX demand paging and Windows reserve/commit preserve stable addresses; the GC driver decommits unused pages above the live watermarks after collection.
Lambda and JS deliberately share this profile because they execute in one context and one pair of virtual reservations. JS has thinner scalar traffic, but a second language-specific mapping would not reduce committed RSS; the fixed reservations are virtual-address budgets. A future script/page/worker-isolate matrix remains an optional tuning refinement.

**OS6 — C2MIR scope.** CLOSED — **C2MIR is kept exactly as-is; no code is touched** *(decided 2026-07-15)*. It inherits the new boxing passively (it calls `push_l`/`push_k` by name; the shared implementations re-home into the exec base frame underneath — script-lifetime numbers, matching its current nursery behavior), and its rooting remains conservative-scan-only, as today. No watermark emission, no helper migration, no emitter change. If C2MIR is ever reopened in the future, that reopening means **upgrading its entire implementation to this design** (frames, watermarks, multi-lane returns, honest rooting) — it must follow the current design anyway; there is no half-step worth building.

**OS7 — Migration order and acceptance gates.** CLOSED for Lambda phase 1 (2026-07-15). Stages 0–3 and the RAII pilot landed; the implementation record contains the final gate evidence. Broad host-helper migration and conservative-scan retirement remain intentionally coupled to the JS phase rather than being incomplete Lambda work.

**OS8 — Deep recursion budget.** Per-frame watermark reservations mean recursion depth × (root+number slots) of side-stack usage. The regions must exhaust *cleanly* (raise the same catchable stack-overflow error as the C-stack guard) and the StackOverflow test semantics must be preserved: fail fast, never balloon.

**OS9 — Scalar pointer-identity audit.** CLOSED for phase 1 (2026-07-15). VMap wide integer and DateTime keys now hash/compare by value, inserted keys/values are stabilized, updates retain the stable stored key, and reads copy wide scalars out. The permanent regression exercises both key types. JS-specific object-key work remains in the JS phase.

**OS10 — End-state of `gc_nursery` and the `push_*` vocabulary.** CLOSED (2026-07-15) — every nursery client has a home: temporaries → number stack; container reads/writes → SF16/SF15; captures → SF18; async locals → SF20; returns → SF14; errors → wrapped map; args → SF19; module globals/REPL → base-frame row above. The numeric nursery and `box_float_cold` were deleted; out-of-band doubles now use the number stack. The *data* nursery in `gc_data_zone` is unrelated and stays. C2MIR and the MIR interpreter inherit retirement through the shared `push_l`/`push_k` implementations and box into the runner's execution-base extent. The existing `push_l`/`push_d`/`push_k` names remain for ABI compatibility and are documented as number-stack boxing helpers rather than nursery APIs.

**OS11 — MIR-interpreter mode.** CLOSED (2026-07-15). The MIR interpreter executes the same generated prologues/epilogues and shared TLS helpers; the complete `proc_stack_frame` wide-scalar/capture/container/error/async/VMap regression matches its native-MIR golden output under `--mir-interp`.

---

## 5. Summary

### 5.1 The three stack-like mechanisms

The Lambda/JS runtime, under this design, runs on three stack-like mechanisms with distinct lifetimes and owners:

| # | Mechanism | Lifetime shape | Holds | GC's view |
|---|---|---|---|---|
| 1 | **Native C stack** | LIFO, per thread | execution frames, native locals, spilled registers | conservative scan during migration (SF9); eventually invisible |
| 2 | **Side stacks** (per thread, watermarked): **GC root stack** + **number stack** | LIFO, tied to native frames — entry saves both watermarks, every exit restores them (SF6) | root stack: tagged Item bits, the precise root set; number stack: raw 8-byte scalar payloads (wide int64/uint64, DateTime, out-of-band doubles) | root stack scanned precisely as `[base, top)`; number stack **never scanned** (SF12) |
| 3 | **Async stack frames** (`LambdaAsyncFrame`) | **non-LIFO**, task-shaped — heap-allocated, task-owned, reused across suspensions | suspended state: async locals as Item slots + wide-scalar tail (SF20) | Item region registered as root range; tail never scanned |

The governing principle that assigns every value a home: **LIFO lifetimes live on the side stacks; non-LIFO lifetimes (containers, closure envs, async frames) own their scalars in tail regions of their own allocation.** Values cross between the tiers by copy — downward argument flows need none (SF19), upward returns ride register lanes (SF14), and every outliving store re-homes into the destination's own storage (SF15/SF16/SF18/SF20).

The C stack and the side stacks grow and shrink in lockstep (every native frame reserves its statically-known side-stack slots at entry); the async frames deliberately do not — task suspension/completion order is arbitrary, which is exactly why they are heap-allocated and why suspension is a re-homing barrier (SF20).

### 5.2 Before / after

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

---

## 6. Appendix A — why not NaN-boxing (the int64 comparison)

A natural question: would NaN-boxing — the value representation of JavaScriptCore, SpiderMonkey, and LuaJIT — have avoided the wide-scalar problem this design solves? **No: NaN-boxing makes it strictly worse**, and the comparison clarifies why Lambda's representation plus the side-stack architecture is the stronger combination.

**The structural limit.** NaN-boxing hides non-double values in the IEEE 754 NaN payload space (~2⁵¹ free bit-patterns once arithmetic is canonicalized to one NaN). After the type tag, **48–51 payload bits** remain — the representation that makes *every double* inline structurally cannot make *any* full-width int64/uint64 inline; the two claims compete for the same bits.

**What NaN-boxing runtimes actually do with int64:**
- **JSC / SpiderMonkey**: the language dodges it — JS numbers are doubles, with an inline **int32** fast path; wider values lose precision into doubles (allowed above 2⁵³ by JS semantics). True 64-bit+ integers (**BigInt**) are **heap-allocated GC objects** — the same shape as Lambda's `integer` type decision (N1–N9), and correctly *not* a wide scalar here either.
- **LuaJIT**: FFI `int64_t` values are **boxed cdata on the GC heap** — every int64 arithmetic result allocates (a known perf trap its sink optimizations fight).
- **WASM engines**: sidestep via static typing — i64 lives unboxed in typed slots, boxed only at the JS boundary.

The complete menu is: shrink to int32 inline, lose precision into doubles, box on the GC heap, or escape via static types. There is no inline-int64 option in the NaN space.

**Head-to-head:**

| | NaN-boxing | Lambda (high-byte tag + `ITEM_DBL_MASK`) |
|---|---|---|
| doubles inline | **all**, incl. tiny/subnormal | all except tiny/subnormal residue (out-of-band → number stack, SF18) |
| inline integer width | 32 typical (≤51 theoretical) | **56 bits** (`int56`; SF10 small-int64) |
| pointer payload | 48 bits (fragile under 5-level paging / high VA) | 56 bits |
| wide int64/uint64 / DateTime home | **GC heap** (BigInt, cdata): alloc + trace + collect per value | **number stack / owned tails**: no GC involvement (SF2) |

The trade is exactly one-dimensional: NaN-boxing's genuine advantage is the tiny-double residue (it has none; we have a rare out-of-band case). In exchange it gives up 8 bits of integer/pointer payload and — decisively — has **no non-GC home for wide scalars**: every NaN-boxing runtime pays heap allocation and collection for each boxed int64, the exact cost structure SF2 eliminates.

The lesson cuts the other way from the question: were Lambda NaN-boxed, the side stacks would be *more* necessary, not less — SF10's inline fast path would shrink from 56 to ~48 bits, pushing more values onto the wide path. Wider inline range than any NaN-boxer, plus a frame-scoped value home for the residue that NaN-boxing runtimes lack (their equivalents *are* scalars-in-the-GC), is the stronger pair.

---

## 7. Appendix B — the static-host comparison (typescript-go / Go)

The other instructive bracket: **typescript-go** ("Corsa", the TypeScript 7 native compiler ported to Go, ~10× faster than the JS-hosted compiler). Where Appendix A shows the *dynamic-representation alternative* (NaN-boxing), tsgo shows the *static-host escape* — and the contrast independently validates several SF decisions.

**The framing difference: tsgo dodges the problem class.** tsgo is a compiler, not a runtime — it never executes a TS value. Its "values" are AST nodes, types, and symbols; the only JS-semantics numbers it touches are the checker's constant folding, done on plain unboxed Go `float64`. Go being statically typed, an `int64` lives naked in struct fields, locals, and returns — the "escape via static types" row from Appendix A's menu, applied wholesale. Lambda cannot take that exit: a dynamically-typed `Item` carries its type at runtime, which is why the 56-bit ceiling, the number stack, and the tail regions exist.

**Where the runtime-level comparison is genuinely informative:**

- **Stacks.** Goroutine stacks are contiguous and **grown by copying** — possible only because the Go compiler emits **precise stack maps** (per-frame pointer bitmaps), so the runtime can rewrite every pointer into a moved stack. That is the JVM-class solution §2 rejected as unattainable on MIR; the side stacks are the attainable substitute. Both projects rejected the same alternative for the same reason: Go abandoned segmented stacks (the "hot split" problem) for contiguous-with-copy; SF13 rejected chained segments for reserve-and-commit. Same lesson — *don't fragment a stack* — resolved with the mechanism each runtime can afford: **Go moves (because it is precise); Lambda reserves (because its pointers must stay put).**
- **Rooting.** Go is fully precise everywhere (no conservative scanning since ~1.4) via total compiler cooperation. SF9's migration — precise side stacks now, conservative scan as a shrinking backstop over C helpers — is the honest path to the same destination for a runtime whose codegen and host functions grew up without maps.
- **Boxing.** Lambda's representation is *denser* than Go's dynamic escape hatch: a Go interface value is a **two-word fat pointer** whose data word cannot hold non-pointer payloads, so boxing into `interface{}` generally heap-allocates. A tagged 64-bit Item with 56-bit inline payloads is allocation-free for the common cases. The tsgo team's answer was to avoid interface dispatch in hot paths entirely — one concrete `Node` struct with a kind discriminant and per-kind payload, slab-allocated for locality — which is **precisely the shape of `vibe/Lambda_Design_Unified_AST.md`** (one node-kind space, struct unification, pool allocation), arrived at independently for the same reason: data-oriented concrete structs beat host-language dynamic dispatch.
- **Returns.** Go's `(T, error)` multiple return values are the language-level twin of SF14's `[scalar, error]` lanes — convergence, not coincidence: both start from *errors are values, there is no unwinding* (Lambda's no-longjmp convention ≙ Go's no-exceptions model), so the error rides the return path in registers. SF14 essentially rediscovered Go's calling convention from MIR's register constraints.
- **Concurrency.** tsgo's ~10× is roughly half native code, half goroutine parallelism over effectively immutable shared ASTs — the same bet as K13's thread-agnostic value semantics; Lambda's isolates + per-thread side stacks are the runtime-level expression of "the data is read-only by then."

**Punchline.** tsgo's speed comes not from cleverer boxing but from (a) *deleting* dynamic value representation via a statically-typed host, and (b) parallelism over immutable data. Neither is available to a dynamic-language runtime, but the ledger already encodes the transferable halves: SF5 static frame sizing and honest typing ("be as static as a dynamic runtime can be"), SF14 (Go's return convention), side stacks (the precise-stacks substitute absent compiler stack maps). And where Lambda goes beyond its host-level comparator: Go would heap-box an `interface{}`-held int64; Lambda's number stack and tails keep even the dynamic wide-scalar case out of the GC entirely.

---

## 8. Appendix C — the compilation-target comparison (WASM)

WASM appears in Appendix A as one menu row ("escape via static types"). The full comparison is richer — three architectures in one, including a near-literal twin of SF20.

**Core WASM: the static escape, plus a two-stack split with a familiar shape.** Core WASM values are statically typed (`i32`/`i64`/`f32`/`f64`), unboxed, validated ahead of time — no boxing problem exists (the Go/tsgo exit, Appendix B). But its stack architecture rhymes with this design: WASM locals and the operand stack are **unaddressable** — no pointer can be formed to them; engines keep them in registers/native frames outside the sandbox — so C/C++-to-WASM toolchains maintain a **separate shadow stack in linear memory** (`__stack_pointer`) for address-taken data. Two stacks, split for *sandboxing* rather than GC precision, but structurally the same move as C-stack vs. side stacks: **separate what the machine controls from what programs can point into.**

**Host references: the handle discipline, standardized.** Pre-GC WASM cannot store an `externref` in linear memory at all — host references live only in locals, globals, and engine-tracked **tables**. Table indices *are* the handle model: the SF8/G2 RAII-guard/N-API discipline, imposed by the type system rather than by convention.

**WASM-GC: dynamic guests re-encounter this document's entire problem, with fewer tools.** The GC extension provides engine-managed structs/arrays, rooted via the engines' existing precise stack maps (the JVM-class solution §2 rejected as unattainable on MIR). Its inline-scalar story is **`i31ref`** — 31 bits inline in a reference. Compare: Lambda 56 (SF10), NaN-boxing ~48, WASM-GC 31. Everything wider becomes a **GC-heap struct box** (Kotlin/Wasm heap-boxes `Long`), and there is no side-stack remedy available: reference-typed values cannot live in linear memory, so a boxed scalar has no non-GC home. A dynamic language on WASM-GC lives with scalars-in-the-GC — the exact cost structure SF2 eliminates. **Lambda-on-MIR with side stacks is strictly better equipped for wide scalars than Lambda-on-WASM-GC would be.**

**Suspension: Asyncify is SF20 as a whole-program transform.** Lacking stack switching, WASM's async story was **Asyncify**: at every potential suspension point, spill all live locals into a linear-memory structure, unwind by returning through every frame, and on resume re-enter and rewind, reloading the locals. That is precisely the SF20 suspension barrier — *at suspend, live values re-home into owned heap storage; nothing points into any stack* — implemented mechanically over the whole program. Its known costs (code bloat, spill traffic) are what motivated the **stack-switching / JSPI proposals**: first-class movable stacks, i.e. the fiber alternative Lambda's concurrency design v1 considered and rejected in favor of K2-R state machines. WASM's trajectory validates both halves: the barrier concept works, and its whole-program cost is the argument for compiling suspension into state machines instead.

**Multi-value returns.** WASM shipped multi-value functions years ago — SF14's lane shape, third independent convergence (Go, MIR, WASM), each derived from the same premise that results should ride registers, not memory.

**Punchline.** WASM as a *target* means any dynamic language standing on it faces this design's problems with weaker tools: 31-bit inline ints, mandatory GC boxing for wide scalars, handle-only host references, and Asyncify-grade suspension costs. The three appendices bracket Lambda's position completely: NaN-boxing (A) is the dynamic-representation alternative that loses on integers; Go (B) is the static-host escape unavailable to a dynamic runtime; WASM (C) is the portable target whose GC extension would hand back every problem the SF ledger just solved.
