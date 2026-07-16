# Lambda + LambdaJS — Outstanding Issues Ledger

**Status:** living rollup — index of everything open, with pointers to owning ledgers/docs; update as items close.
**Date:** 2026-07-16
**Scope:** Lambda core runtime + LambdaJS. Python has its own ledger (`vibe/Lambda_Design_Stack_Frame_Python.md` PO1–PO9 + `vibe/Lambda_Impl_Stack_Frame_Py.md`); Radiant is tracked in `vibe/radiant/`.
**Sources:** full Known-Issues sweeps of `doc/dev/lambda/LR_01–13` and `doc/dev/js/JS_01–16` (2026-07-15/16), the JO ledger (`vibe/Lambda_Design_Stack_Frame_JS.md`), and session-verified corrections. ⚠️ The tree is moving fast — items marked **[verified]** were re-checked in code this session; unmarked items are as the doc sweeps reported them and may have moved.

**Closed context (not in this ledger):** the stack-frame architecture is fully IMPLEMENTED — SF1–SF20/OS1–OS11 for Lambda and JS, SF15-J JS-array props migration included (`vibe/Lambda_Impl_Stack_Frame*.md` all carry implementation records). Rooting-shadow-frame overhead, the numeric-nursery leak, scalar re-homing, and the JS-array `extra` collision are done.

---

## 1. Top design gaps (need their own design doc / ADR before code)

**OI-1 — Value equality & ordering contract.** *Substantially narrowed by session verification.* The operator surface is in good shape **[verified]**: `fn_eq` is structural and cross-rank-exact, `fn_lt_scalar` and `total_cmp` are raw-byte on strings and mutually consistent, `array_num_eq` is width-correct and NaN-aware, and the utf8proc casefold comparators are markup-parser-only (LR_03 #1, LR_05 #1, LR_05 #3 as written are stale — see §7). What actually remains:
- (a) **`item_deep_equal` dedup**: reimplement over a new `fn_eq_strict` (cross-rank promotion + cross-seq branches disabled; no `set_runtime_error` on depth cap; moved out of `lambda-data.cpp` for the LAMBDA_STATIC link) — its only caller is Radiant no-op elision (`radiant/event.cpp:2102`), where current gaps cost missed elision (always-rebuild for map-bearing outputs), never wrong answers **[verified]**.
- (b) **VMap key eq/hash consistency**: `fn_eq(1, 1.0)` is TRUE — verify whether VMap lookup/hashing agrees across numeric ranks (the Python `hash(1)==hash(1.0)` invariant; JS SameValueZero analog). If not: rank-normalized key hashing, or an explicit "map keys use strict equality" rule in the formal semantics.
- (c) **`decimal_cmp` failure-as-equality**: LR_04 #4 says conversion failure returns 0 (equal); verify `decimal_cmp_items` and fix to error/unordered.
- (d) Cohort question for decimals (Java BigDecimal 2.0-vs-2.00 trap) — confirm `==`/`<`/key behavior agree for equal-value different-scale decimals.

**OI-2 — JS object model: internal metadata + GC lifetime.** Two intertwined debts across JS_06–JS_10: (a) the **marker→shape-flag migration is half-done** — class identity, accessors, iterators, Promise branding still ride `__class_name__`/`__ctor__`/`__arr__` string keys beside the typed `JsClass`/`ShapeEntry` scheme; (b) **pools never GC-reclaimed** — JsFunction cached wrappers (module-lifetime), generator pool (4096, index collision on churn), promise pool (never freed; reactions capped at 8), and **WeakMap/WeakSet/WeakRef have no weak semantics** (needs ephemeron support). Overlaps JO1/JO2/JO6. *Recorded 2026-07-16 (no detailing yet): map-field holes (deleted-slot tombstones, `ITEM_JS_DELETED_SENTINEL` written in place with the `ShapeEntry` retained) and sparse-array holes (hole sentinels in `items[]` + the `MAP_KIND_ARRAY_SPARSE` companion) are two conventions over one concept — consider them together and unify on one mechanism if possible when the object-model representation work happens.*

**OI-3 — ESM correctness.** Named-import live bindings are snapshot-only (spec divergence); circular ESM sees placeholder `undefined` instead of TDZ ReferenceError (no SCC/`[[CycleRoot]]`); top-level await is single-shot first-await-only; `js_await_sync` busy-drains to `undefined` (JS_09 §1–3, §6). Lambda side has the same gap from the other direction: cross-language import skips `pub` vars "until live binding support" (LR_01 #11) — one cross-language design.

**OI-4 — RegExp semantics.** RE2 leftmost-longest ≠ JS leftmost-greedy; heuristic routing can silently yield wrong captures; the backtracking engine bails to "no match" at its 8M-step budget (JS_11 §1, §4). Needs an explicit decision: own backtracking engine as primary vs. proven-equivalence routing.

**OI-5 — MIR value-representation contract (Lambda MIR-Direct).** No single canonical contract for type↔representation at each boundary (LR_07 closing verdict). Concrete casualties: INT64 arithmetic never native (`push_l_safe` false-positive tag range ~[2.88e17, 3.60e17]); FLOAT→INT widening truncates in loops / boxes outside; indirect/closure calls >3 args return wrong values; typed-array construction diverges from C2MIR (LR_07 #1/#3/#4/#5); errors silently coerce to 0/0.0/false when unboxed. SF10 small-int64 inlining landed; the contract doc would close the family.

**OI-6 — Codegen quality cluster (JS).** Destination-passing lowering (66–88% of MIR is MOVs, JS_04 §1/JS_15 §5.5); shape-based polymorphic inline caching (JS_15 §5.3 — see design record below); ADD-inference recovery, realm-scoped intrinsic-prototype cache, and float register residency are **in flight via Tune-6** (`vibe/Lambda_Impl_JS_Tune.md` Tracks A/D/B). **De-pointered relocatable MIR** (~59 baked realm pointers) blocks artifact caching (JS_01 §9.8/JS_15 §6) — design exists (MIR cache MC1–MC8, P1–P5 prerequisite).
*PIC design record (withdrawn from Tune-6 on 2026-07-16 — judged too complicated for that plan; needs its own design pass before code):* the constraints (single-tier JIT, no code patching, no deopt — G5) force a **data-driven** cache, not a code-patched one: per-call-site side-table entries `{ TypeMap* shape, void* target, uint32_t guard_version }` × 2 ways, owned by the compiled module — realm-scoped by construction, which is what the earlier reverted process-global prototype cache got wrong (leaked one realm's prototype into another). Open decision: **invalidation granularity** — (a) one per-realm version bumped on any prototype/method mutation (cheapest; thrashes under test262's constant prototype mutation) vs (b) per-shape version counters (+8 B per TypeMap; recommended). Companion fix: the **duplicate-class-name full deopt** (JS_07 §7.7) — re-key the shaped construction/devirtualization fast paths by constructor/`TypeMap` identity instead of class-name strings; deliberately targeted, must not grow into the OI-2 marker migration. Mandatory fixtures inherited from the reverted widening attempt: `Object.defineProperty`/`Object.seal` on cached-shape objects, two same-named classes exercising both fast paths, prototype mutation after cache warm. Benchmarks: richards/deltablue; dup-name construction microbench.

**OI-7 — Node compat majors.** Async `fs` runs synchronously inline (event loop not integrated); stream internals are stubs (K27 shared stream core is the settled design that fixes this); `vm` does not isolate (security-relevant); crypto lacks asymmetric primitives; `fs.watch`/`child_process.fork` missing (JS_14 §1–4, §6–7).

**OI-8 — DOM fidelity.** No on-read layout flush (mutate-then-read `offsetWidth` sees stale pixels); framework-blocking API gaps fail as silent `undefined`; O(n) listener + wrapper storage degrades quadratically; no text segmentation/Bidi (JS_13 §1, §2, §4, §6).

**OI-9 — Unboxed scalar storage in maps and arrays (DEFERRED design; predecisions recorded 2026-07-16).** The end-state beyond Tune-6's Track B: shaped slots (and array elements) as a *guaranteed, inline-addressable* raw representation. **Deferred by decision to a much later effort** — it generalizes beyond doubles to all scalar kinds, in maps *and* arrays (the ArrayNum elements-kind family), and is designed once, not piecemeal. Candidate doc: `vibe/Lambda_Design_Shaped_Slots.md`.
- **Starting point (verified):** guarded native storage partially exists — constructor-typed FLOAT/INT class fields store raw natives in `map->data`, read/written by `js_get_slot_f`/`js_set_slot_f` (`js_runtime.cpp:3159`) — but each access is a C call that re-derives the `ShapeEntry` and re-validates the type, and it fires only for ctor-typed fields on statically-known receivers; object literals and inference-missed fields stay boxed (the slow half of the nbody gap). The promotion: the shape *pledges* the representation → one up-front shape guard (the Tune-6 Track-C `TypeMap*` compare) + inline MIR loads/stores, extended to shape-inferred object-literal fields.
- **Open design questions:** (1) transition policy on a non-conforming write — shape-transition + storage migration vs poison-and-deopt via per-shape versions; (2) write-path blast radius — every generic and polyglot writer honors the pledge or transitions (miss one and a tagged Item lands where GC expects raw bits — the SF12 corruption class); (3) GC/shape coherence during transition; (4) **scope: fields-only vs fields + elements-kind** — ArrayNum-backed all-numeric dense JS arrays (the "boxed array elements" bottleneck + auto-vectorization unlock) collide with the SF15-J props slot and ArrayNum's `ArrayNumShape*` `extra` overload, so elements-kind must be an explicit decision, not drift.
- **DECIDED — no in-band tombstones in unboxed scalar storage, for any type** *(2026-07-16)*. Per-type in-band options were surveyed (out-of-domain patterns for bool/int56/pointer; V8-style hole-NaN + store canonicalization for double; nothing sound for true int64) and rejected wholesale as error-prone and a performance tax: sentinel checks on hot read/write paths, store canonicalization funding a rare operation, a second meaning for raw bits (the SF12 ambiguity class), and domain-stealing sentinels are the LR_03 #4 bug family, already rejected once by SF14. **Absence is always out-of-band**: delete/uninitialized forces a transition back to boxed representation (or per-shape-version poison); holey arrays stay boxed.
- **DECIDED — adopt the ArrayNum raw-storage discipline** (element-width-aware compaction, data buffers never scanned as Items) rather than inventing new rules; ArrayNum is the in-tree prior art and JS typed arrays already ride it.
- Cross-refs: the OI-2 rider (unify map-field tombstones + sparse-array holes) feeds the same design; Tune-6 B1's measurements are the go/no-go input, and B1's flush-to-slot discipline gets cheaper under this representation.

---

## 2. JS stack-frame residue — JO ledger (owned by `vibe/Lambda_Design_Stack_Frame_JS.md`)

One line each; full statements + refs in the JO doc.

- **JO1–JO6 root-registration de-pinning**: promise pool wholesale-rooted/never freed; generator pool no GC reclamation; async-context table re-pins de-rooted suspended envs; per-require module-var root ranges accumulate; per-item roots on growth paths (timers, DOM wrapper cache, namespaces); module-lifetime cached wrappers. One doctrine ("GC-owned records; roots for true globals only") covers the family.
- **JO7–JO9 suspension machinery**: 64-state cap + over/under-counting yield heuristics are correctness-load-bearing; hand-rolled generator vreg spill compensates for the register allocator; `js_await_sync` can't suspend (owned by concurrency K-series).
- **JO10 TCO/C-stack**: self-recursion only, silent 1M cap; interpreter mode has no TCO at all.
- **JO11–JO12 recovery residue**: ~55 MB MIR code pages leak per crash recovery; event-loop SIGSEGV band-aid masks timer-callback heap corruption + un-audited timer scratch-`EvalContext` re-entry watermark binding (plausibly the same bug — audit first).
- **JO13 float register residency**: boxing-traffic codegen half (allocation half retired by side stacks).

Also standing from the JS phase-2 plan: **phase-3 helper migration** to the RAII root guard and **conservative-scan retirement** (per-module, once coverage is redundant — Python is currently also a blocker, see its PO ledger).

## 3. Lambda core — per-doc outstanding (from the LR_01–13 sweep)

MAJOR items in bold; selected moderates. Per-doc numbering = the doc's own Known Issues section.

- **LR_01 (pipeline/CLI/REPL)**: **#1 `sys://` paths never resolved in maps/elements — workaround masks a real map-walk segfault** (`runner.cpp:1329`); **#9 `init_module_import` layout-coupled pointer walk** silently corrupts on layout change; #3 unescaped LaTeX-bridge filename injection; #8 O(n²) REPL re-execution + repeated side effects; #4 `target_equal` hash-only collision; #11 namespace export gaps (→ OI-3); #14 `g_template_registry` process-global.
- **LR_02 (parse/AST)**: **#1 unknown binary operator silently defaults to `OPERATOR_ADD`**; **#2 numeric promotion relies on undocumented TypeId enum order (no static assert)**; #4 relational result type must stay in lockstep with vectorized codegen; #11 `~` in match-arm bodies can be missed; #13 `parse.c` ignores parse errors.
- **LR_03 (value model)**: #4 fragile sentinels/coercions (`it2d` NaN-poison, `it2l` INT64_MAX sentinel collision, `it2b` omits SYMBOL/INT64/DECIMAL); #5 overloaded tags (BigInt on DECIMAL; `JsAccessorPair` reads as FUNC without flag check); #7 `vmap_from_array` duplicate-compare dead branch (likely meant ARRAY_NUM). *(#1 equality re-scoped → OI-1; see §7.)*
- **LR_04 (numbers)**: **#3 trapping `mpd_get_ssize` can SIGFPE**; **#4 `decimal_cmp` returns equal on conversion failure** (→ OI-1c); #1 "unlimited" decimal is a 200-digit cap; #6 UINT64 reinterpreted as signed in `normalize_sized`; #5 float↔decimal via `%.17g` string detour; #7 INT64⊕INT64 overflow UB in non-builtin fallback.
- **LR_05 (strings/vectors)**: **#2 `ndim` ≤ 32 unchecked → fixed `[32]` stack-buffer overrun** in broadcast/stride helpers; #5 `index_to_item` truncates int64→int (pipe `~#` past 2³¹); #6 `fn_label` raw malloc/free; #7 fixed median/otsu caps. *(#1 ArrayNum `==` FIXED [verified]; #3 two-orderings stale — see §7.)*
- **LR_06 (C2MIR, frozen)**: **#2 GROUP BY unimplemented (hard-stop)**; **#3 typed-array support more complete than MIR Direct — port direction is INTO MIR Direct**; #6 separate inference engine w/ silent caps. *(Backend frozen per OS6 — issues matter only via the port items.)*
- **LR_07 (MIR Direct)**: → OI-5 (the #1/#3/#4/#5 family + cross-cutting contract). Plus #6 `get_effective_type` narrows only IDENTs; #12 OOB index semantics differ by type; #14 magic `heap->gc` offset 8.
- **LR_08 (memory/GC)**: **#1 decimal `mpd_t` finalization gap — per-cycle leak** (needs finalization callback); **#6 hard-coded struct byte offsets in trace/compaction** silently corrupt on layout change; #7 shape-pool >64-field chain returns NULL w/ only a warn; #10 re-entrant allocation during GC skips collection unguarded.
- **LR_09 (builtins)**: #1 replace-in-file procedures disabled (registry key collision; needs `first_param_type` disambiguation); #2 `SysFuncInfo` lacks data-driven native-arg conventions (forces inline special-casing; also blocks #1); #5 `fn_index` swallows invalid indices.
- **LR_10 (errors)**: #3 `err_free_stack_trace` leaks strdup'd frame names; #4 hard-coded 64 KB last-function span mis-attributes deep frames; #2 error-code/table drift renders UNKNOWN_ERROR.
- **LR_11 (Mark API)**: **#2 MarkReader traversal stubbed** (leaks state, direct-children-only scan → silently wrong deep selections); **#4 ui_mode arena-provenance landmine** (wrong `pool_free` corrupts rpmalloc, no diagnostic); #3 render_map mutates while iterating; #5 truncate-vs-error cap inconsistency; #6 shallow PATH deep_copy.
- **LR_12 (procedural)**: #1 `fetch` returns bare String (drops status/headers); #2 `pn_push`/`pn_splice` swallow type errors invisibly; #3 TCO fully implemented but hard-disabled + unconditional stack checks; #7 ad-hoc effect surface (no capability system).
- **LR_13 (validator)**: **#6 advertised options unenforced** (`strict_mode`, `--allow-unknown`, warnings never emitted); #2 suggestions built but never surfaced (small high-value fix); #4 fragile strstr root-type selection; #5 `MAX_UNION_TYPES=32` drops members silently; #7 placeholder helpers.

## 4. LambdaJS — per-doc outstanding (from the JS_01–16 sweep)

Items already rolled into §1/§2 are not repeated.

- **JS_01 (pipeline)**: §9.5 phase-ordering invariants comment-only/unenforced; §9.7 unguarded fixed stacks (`loop_stack[32]`, `try_ctx_stack[16]`); §9.1 45–55 MB per-compile transpiler struct + silent `func_entries[32768]` truncation; interpreter link mode lacks TCO (→ JO10); §9.6 module/eval/parallel link-path divergence.
- **JS_02 (parse/front-end)**: **§2 heuristic source rewriting before Tree-sitter** (regex-literal `<!--` corner mis-rewrites; grammar-level fix would delete the hack); **§2.2 `for await (let [...])` misparse band-aid** (`let` spelling only); §4 unknown operator falls back to `JS_OP_ADD`; §3 two divergent redeclaration checks.
- **JS_03 (value model)**: **§1 Symbol/number share `LMD_TYPE_INT`** (negative-int overlap; dedicated packed tag would fix); §2 no small-BigInt fast path (every `0n` is an mpd_t alloc); §4 module-var ceiling 2048 silently drops; §6 `js_batch_reset` manual ~30-call fan-out (= JS_16 §2).
- **JS_04 (lowering)**: **§3 generator spill hand-rolled vs register allocator** (→ JO8); **§4 per-scope strict mode approximated by source string-scan**; **§5 eval tier selection is textual, not AST-driven**; §6 INT×INT via doubles.
- **JS_05 (functions/closures)**: **§1 for-of `let` per-iteration capture is fragile manual bookkeeping** (stale-capture risk); §2 closures >16 captures lose write-back (arrays sized [512] but loop fills 16); **§4 inliner type assumptions fragile** (widening attempt reverted after Object.defineProperty regressions); §6 `with` defeats function cache + fast paths; §5 TCO (→ JO10).
- **JS_06/07 (objects/classes)**: **marker-scheme debt** (→ OI-2); **§7.1 named class-expression inner-name scope leak**; **§7.3 TypedArray subclass `@@species` resize not fully wired**; **§7.4 private brand is a `__brand_` string approximation**; §7.7 duplicate class name forces full deopt for all same-named classes; §6 TypeMap capacity-32 hash silently stops inserting (= JS_15 KI-8).
- **JS_08 (iterators/generators)**: **§1/§2 64-state cap + fragile yield counting** (→ JO7); §3 IteratorClose skipped beyond depth-16 try-context nesting; §5 generator pool (→ JO2); §6 dual synthetic-iterator schemes (→ OI-2 markers).
- **JS_09 (async/modules)**: **§1–§3 TLA/live-bindings/circular-ESM** (→ OI-3); **§7 SIGSEGV band-aid masking timer-callback heap corruption** (→ JO12); §4 fixed pools + reactions cap 8 (→ JO1); §8 Promise `__class_name__` marker; **JS_15 §7.3: Promise constructor does not invoke the executor (octane/pdfjs)** — concrete open bug.
- **JS_10 (builtins)**: **§2 `globalThis` is a snapshot, not live** (spec divergence); **§3 WeakMap/WeakSet/WeakRef not weak** (→ OI-2); **§5 `process.exit`/`abort` hard-kill the embedder** (no interceptable hook); §7 symbol-registry keys truncate at 127 bytes.
- **JS_11 (regexp)**: → OI-4; plus §2 direct-RE2-fallback lookbehind silently stripped; §6 fixed capture ceilings (256 groups) silently lose captures.
- **JS_12 (typed arrays)**: **§4 resizable-buffer refresh discipline is convention, not construction** (stale `view->data` after resize realloc); **§7 pending-new-target is global, not per-construction** (`Reflect.construct` 3rd arg not honored); §5 species-resize name-matched not identity-matched; §6 freeze-over-length-tracking under-specified; §3 ToUint8Clamp implemented twice.
- **JS_13 (web/DOM)**: → OI-8; plus §3 `OffscreenCanvas` compile-time interception can't be shadowed by user globals (a general interception-seam pattern).
- **JS_14 (node)**: → OI-7; plus §5 `ERR_*` codes incomplete (err.code checks unreliable); §9 npm client gaps (no dedupe/workspaces/lifecycle scripts; `exports` conditions omit `require`).
- **JS_15 (perf)**: → OI-6; plus KI-7 test262 fast-path sites not compiled out of production; §7.3 octane/box2d wrong result (uninvestigated); 6 heavy macro-benchmarks time out (downstream of §5 gaps).
- **JS_16 (testing)**: **§7 negative-test scoring passes any "Error" substring** — a `SyntaxError` expectation passes on any thrown error, masking conformance regressions; §2 residual unreset statics leak across batch tests (detection exists, root cause doesn't); §3 crash-recovery code-page leak (→ JO11); §1 batch-vs-single flake class.

## 5. Settled designs awaiting implementation (no new decisions needed)

| Work | Design | Unblocks |
|---|---|---|
| Unified AST Phases 0–5 | U1–U26 + impl plan (Phase 0/P0.1 Lambda slice done) | shared emitter/inference, guest ports (Python PO8), OI-5 partially |
| K27 shared stream core | concurrency design §11 | OI-7 streams (the node-baseline linchpin), fs/event-loop integration |
| De-pointered MIR P1–P5 | MIR cache MC1–MC8 | OI-6 artifact caching |
| JS threading P1–P3 | JT1–JT7 | worker isolation/watchdog; feeds `vm` realm isolation |
| Concurrency Stage A/B (`start`, K11–K18) | v3 adopted | JO9 real suspension; actor/mailbox K20 |
| Stack-frame Python port P0–P4 | PS1–PS10 + `Lambda_Impl_Stack_Frame_Py.md` | PO1–PO6; unblocks SF9 scan retirement |

## 6. Cross-cutting hygiene themes (one policy each, not per-site fixes)

- **Silent fixed caps with inconsistent failure modes** — dozens across both runtimes (closure captures 16, generator states 63, promise reactions 8, TypeMap hash 32, union types 32, module vars 2048/1024, regex groups 256, …). One grow-or-error doctrine retires the class.
- **Layout-coupled raw offsets** — GC trace/compaction (LR_08 #6) and `init_module_import` (LR_01 #9); static-assert guards or generated offset tables.
- **Two masked memory-safety bugs** — the event-loop SIGSEGV band-aid (JS_09 §7/JO12) and the `sys://` map-walk segfault workaround (LR_01 #1). Both are acknowledged suppressed root causes.
- **Two parallel inference engines** (C2MIR vs MIR Direct, LR_06 #6/LR_07 #8) — unified-AST call-site inference is the designed resolution.
- **`SysFuncInfo` registry expressiveness** (LR_07 cross-cutting, LR_09 #1/#2) — data-driven arg/ret conventions would delete inline special-casing.

## 7. Session-verified corrections to the doc-recorded issues (2026-07-16)

**All applied to the docs on 2026-07-16** (LR_03 #1 re-aimed; LR_05 #1/#3 marked fixed/stale with current refs; LR_09 #3/#4 updated; `doc/dev/Python_Runtime.md` given a stale banner + point fixes). Kept here as the record of what changed and why:

- **LR_03 #1 (item_deep_equal)**: mis-aimed — `item_deep_equal` is not language equality; its only caller is Radiant no-op elision and its gaps are conservative (missed elision, not wrong answers). Language `==` is `fn_eq`, which is structural and cross-rank-exact. Re-aim the issue at the elision path (→ OI-1a).
- **LR_05 #1 (ArrayNum `==` wrong-width memcmp)**: **FIXED** — `array_num_eq` now uses per-type element width, is NaN-correct, and value-compares across element types (`lambda-eval.cpp:1265`).
- **LR_05 #3 / LR_09 #3 (two string orderings)**: stale at the operator level — `==`, `<`, and `total_cmp` are all raw-byte and mutually consistent; utf8proc casefold comparators are used only by the markup parser. If collation is ever wanted, add it as an explicit collator API governing equality *and* order together (SQL/XQuery model), never by changing operators.
- **Stack-frame-era claims throughout LR_07 #9, LR_08 #2–#5/#8**: closed by the implemented SF architecture (already excluded above; listed here for completeness when editing those docs).
