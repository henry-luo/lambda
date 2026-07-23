# Lambda Impl Plan: COW Stage 1 + `pn`-Method Support (Tune-COW)

**Status: IMPLEMENTED — 2026-07-23; Result11 provenance recorded below.**
**Implements:** `vibe/Lambda_Design_COW.md` **Stage 1** (CW19: basic COW +
performance, NO exclusivity enforcement) plus the C4 outstanding items that
belong with it — foremost **proper `pn`-method support** (the C4.1 bug
cluster). Successor of `vibe/Lambda_Impl_Tune.md` (M1/M2 landed there; the
ex-M3 anchor is retired here).
**Semantic authority:** `doc/Lambda_Formal_Semantics.md` §9 (C4);
decision record `vibe/Lambda_Semantics_Formal.md` C4–C4.4.
**Convention:** `file:line` refs verified 2026-07-23; they drift — confirm
against symbol names.

**Governing invariant — different from Impl_Tune's.** Impl_Tune was
bit-identical by contract. This plan deliberately moves the aliasing line to
the **C4 core-container subset**: Lambda-native `Array`, `Map`, `Object`,
`Element`, and `VMap`. ArrayNum COW/views and JS↔Lambda interop remain on
their current behavior until Stage 2. For the in-scope kinds, `let`-finality
becomes real, exempt-site aliasing ends, and the C4.1 probes flip from
bug-pinning goldens to correct-behavior goldens. Every observable change
must be enumerated in the phase that makes it and land as an explicit golden
update. Everything else stays invisible per P6.

---

## 0. Scope ledger — C4 outstanding items, in or out

Direct answer to "what else is outstanding from C4":

| Item | Source | Disposition |
|---|---|---|
| COW at mutation points (refcount/uniqueness-triggered) | C4.4 #1 | **Phases C–E** (bit 0 of `cow_state`, CW3) for Array/Map/Object/Element/VMap |
| Raw-vs-COW mutation API boundary | COW rev 6 / CW21 | **IN — C0/C3.** Existing in-place APIs remain unchanged; new Lambda-only `_cow` wrappers return the possibly replaced owner and delegate to raw mutation |
| VMap COW | COW rev 6 / CW9 | **IN — C2/C3.** `vmap_set()` remains raw/host-compatible; `vmap_set_cow()` uses the backend snapshot/detach hook; task handles and non-snapshot host backends reject `_cow` mutation |
| ArrayNum COW, views, representation-invariant equality | COW §9/CW15–CW16 | **OUT — Stage 2.** Keep the native specialized path; no generic COW branch/walk |
| JS↔Lambda COW/buffer ownership | COW §9.3/CW17 | **OUT — Stage 2.** test262/Node remain regression gates only |
| `var`-param grammar | C4.4 #1 | **already parses** (`var_param_marker`, grammar.js:494/:554; `is_var_param` built at build_ast.cpp:7951, consumed at :2575, transpile-mir.cpp:394) — semantics audited in B4/C4, no grammar work expected |
| Compile check: `var` receiver for `pn` methods | C4.4 #1 | **Phase B4 — pulled forward from Stage 2.** Justification: it is a *binding-kind* check (is the receiver rooted at a `var` binding?), not an overlap analysis; and without it `frozen.increment()` stays a silent no-op — the C4.1 worst-case failure mode. Exclusivity proper stays out |
| Compile checks: var-args-only, exclusivity (all faces) | C4.4 #1 | **OUT — Stage 2**, `Lambda_Design_COW.md` §11 |
| Capture-assignment compile error (`fn` + `pn`), incl. interior mutation through captures | C4.4 #1 / C4.2a | **Phase B5** (replaces today's silent-continue and the nested-`pn` crash) |
| Object-mutation bug cluster: `pn` method with params fails to parse; default-instance mutation no-ops; second mutation lost | C4.4 #2 / C4.1 | **Phase B1–B3** — "needed under any semantics" |
| Migration audit (stdlib/fixture aliasing reliance, benchmark `var` signatures, editor/Radiant element paths) | C4.4 #3 | **Phase F** |
| Docs: delete/rewrite `Lambda_Func.md` "Mutable Captures"; aliasing/mutability section; state idioms; construction-captures teaching | C4.4 #4 / #7 | **Phase F** (non-gating, listed) |
| Formal model: store only for `var` bindings | C4.4 #5 | OUT — semantics-DSL project |
| Nested-mutation correctness vs ergonomics | C4.4 #6 / §9.5.2 | **Correct owner/spine writeback IN — C0/C3.** New path-borrow / `_modify` syntax remains OUT |
| Non-escaping nested-`pn` relaxation | C4.2a | OUT — deferred by record; interim idiom is the object form, which **Phase B unblocks** |
| Ordering fixtures: value-args-before-borrow; store-target re-resolution | C4.2c / CW19 | **Phase C5** |
| Snapshot iteration ruling | COW §11.6 | OUT — Stage 2 (candidate C4.2d) |
| `let g`/`var h` aliasing probes (canonical violation) | C4.1 | in-scope goldens **flip at Phase E** |

The OUT rows above are consolidated — with pick-up triggers and grouping
(Stage 2 / needs-design / different project) — as
**`vibe/Lambda_Design_COW.md` Appendix B**, which is the checklist for
declaring C4 fully done once this plan completes.

---

## 1. Phase A — instrumentation, fixtures, stopgap

- **A0 — stopgap scalar early-return in `fn_mutable_value`** (ex-M3 remedy
  (a), approved shape): type-test before creating the `MutableCloneContext`;
  scalars return unchanged, no hashmap. Extract the container-set predicate
  shared with `clone_mutable_item`'s dispatch (`lambda-eval.cpp:6065`) per
  CLAUDE rule 13. Expected: collatz 6 358 → ≈ 2 s class immediately
  (profile wall was malloc/`hashmap_new`). Its generic-container role is
  removed in Phase E; a narrow ArrayNum-specialized guard/path may remain
  until Stage 2. Gate: baseline 100 %, bit-identical (pure representation).
- **A1 — CW5 counters, release-safe** (`js_exec_profile`-style): per TypeId —
  share-marks, mutations-on-unique (in-place), mutations-on-shared (copies),
  bytes copied; VMap backend and host-rejection counts; plus
  `fn_mutable_value` call rate while it still exists.
  These counters are the CW4 (saturating-count) decision data; they ship
  first so before/after spans the whole migration. COW mutation counters live
  in `_cow` wrappers/cold helpers only; raw JS/in-place stores neither pay for
  nor contaminate them.
- **A2 — capture the C4.1 probe matrix as fixtures with CURRENT goldens**:
  `let g`/`var h`, var–var alias, object alias, default-instance no-op,
  second-mutation loss, frozen-receiver no-op, capture-assign behavior
  (`temp/REPRO_array_literal_alias.ls` + `temp/matrix.ls` promoted into
  `test/lambda/` with `.txt` goldens per rule 8). They pin the hybrid now and
  flip deliberately in B/E.
- **A3 — benchmark harness points**: record collatz/splay/gcbench/primes/
  array1 + the C4.3 **editor/document benchmark** (create if absent: element
  tree build → repeated small edits → serialize; this is the §9.5.1 gate).
- **A4 — performance acceptance protocol (DECIDED, designer, 2026-07-23):**
  release builds only, matched binaries/configuration, three-run medians, and
  output correctness checked before a timing enters an aggregate. Do not use
  a blanket per-benchmark percentage cutoff. Record branch/helper/allocation
  deltas for COW paths and explain every affected-row regression. Result11
  must be materially better than Result10 overall and target Result9-class or
  better Lambda/MIR performance (§4 and §7).

## 2. Phase B — `pn`-method support (the C4.1 cluster)

Ground truth from code (2026-07-23): methods live in the type body's
methods section (`grammar.js:1154` — `repeat(choice(fn_stam, fn_expr_stam,
that_constraint))`), and `fn_stam` *does* accept `choice('fn','pn')` with
parameters (`grammar.js:577–584`) — so the "pn method with params fails to
parse" failure is **probably not surface grammar**; suspect the AST/method-
table layer. Method calls run through `fn_member`'s OBJECT arm
(`lambda-eval.cpp:3423`): field miss → method-table walk → **bound closure**
via `to_closure_named(compiled_fn, arity, boxed-self-as-closure_env, name)`;
`fn_call` passes `closure_env` as the `_self` first argument.

- **B1 — root-cause and fix the param'd-`pn`-method failure.** Reproduce
  `pn add(n: int)` in a type body; determine which layer rejects it (parse
  tree ERROR vs `build_ast` method-table construction — param walk at
  `build_ast.cpp:7951`/`:8020`, `TypeMethod` arity, method `compiled_fn`
  compilation with a `_self`-prefixed signature). Fix at the root cause; if
  grammar after all, edit `grammar.js` + `make generate-grammar` (never
  `parser.c`). Add the root-cause comment at the fix point (rule 12).
- **B2 — default-instance mutation no-op.** Hypothesis to verify: a
  default-constructed `<Counter>` reads fields from ShapeEntry defaults while
  its `data` buffer is unmaterialized/null, so the method's field write lands
  nowhere (or in a buffer reads never consult). Fix for this phase: **eager
  per-instance data materialization at construction** (small, correct now).
  Note the Phase-C refinement recorded, not implemented here: type default
  buffer as born-shared static (CW8) + COW-on-first-write would make
  materialization lazy — optimization only, do after C.
- **B3 — lost second mutation under aliasing.** Reproduce the C4.1 probe
  (alias exists → second `c.increment()` lost); root-cause (likely the same
  materialization/copy seam as B2 interacting with the anchor exemptions).
  If the honest fix is the Phase-E aliasing-line move, record that and let E
  close it — do not band-aid.
- **B4 — receiver is a `var` borrow.** Desugar `pn` method receiver to an
  implicit leading `var self` param (`is_var_param`); implement the
  **receiver binding-kind check**: `pn`-method call on a `let`-rooted or
  temporary receiver = compile error with the teaching message ("mutating
  method needs a `var` binding"). `fn` methods get read-only `self` (plain
  param). This turns the silent no-op into a loud error — the pulled-forward
  slice of the three checks (ledger, §0).
- **B5 — capture-assignment compile error.** Assignment to a captured name —
  including interior mutation (`b[0] = …`, `b.f = …`) through a capture — is
  a compile error in both `fn` and `pn` closures, message per C4 Round 3
  ("captures are immutable — use an object with a `pn` method, or a `var`
  parameter"). Replaces the silent-continue family and the nested-`pn`
  codegen crash (`unknown add type: 0, 5`).
- **B6 — tests.** Extend `method_call.ls` / `object.ls` / `object_default.ls`
  and add `proc_object_counter.ls`: the C4.2 `Counter` example verbatim
  (typed fields, defaults, `pn increment()` zero-arg, `pn add(n: int)`
  param'd, `fn double()`), default-constructed and literal-constructed
  instances, sequential mutations, method-on-`let` error case, inheritance
  chain method. `.txt` goldens per rule 8. Gate: baseline 100 % with only
  the enumerated golden flips (B2/B3 rows).

## 3. Phase C — correctness substrate, still behind the flag

- **C0 — mutation owner/writeback ABI first (DECIDED).** Define
  `cow_prepare_write(Item old) -> Item replacement`. Every mutation lowering
  must own an assignable root or parent slot and install the replacement
  before writing. For `t.nodes[i].value`, preserve the owner chain in MIR
  registers/stack state and propagate copied children into copied parents;
  do not allocate a heap path descriptor. Direct MIR stores may temporarily
  route through the audited slow path during bring-up.
- **C0a — freeze the raw/COW API boundary (CW21).** Inventory all existing
  update-in-place entry points and their Lambda, JS, Radiant, and host callers.
  `array_set()`, `fn_array_set()`, `fn_map_set()`, `vmap_set()`,
  `js_array_set()`, `js_property_set()`, `js_set_shaped_slot()`, and the raw
  push/splice/pop family keep their signatures and behavior. They never test
  or mark `cow_state`. Add replacement-returning Lambda-only entry points:

  ```c
  Item array_set_cow(Item owner, int64_t index, Item value);
  Item map_set_cow(Item owner, Item key, Item value);
  Item vmap_set_cow(Item owner, Item key, Item value);
  ```

  Add matching `_cow` wrappers for push/splice/pop and editor mutations.
  Store-only wrappers return the updated owner. Operations with a semantic
  result return the owner and write the result through an out parameter.
  Wrappers share `cow_prepare_write()` and delegate to raw implementations;
  no mutation algorithm is copied. Delegate at the highest existing raw layer
  that already owns bounds checks, representation conversion, errors, and
  storage details (`fn_array_set` before `array_set` when those semantics are
  required).
- **C0b — precise-rooting contract (DECIDED).** A copy allocates. Root source,
  replacement, incoming value, and every live owner-chain Item with
  `RootFrame` / `Rooted`; reload pointers after any possible compaction.
  Add forced-GC fixtures for root mutation and a three-level nested write.
- **C1 — state + helpers.** Define `COW_STATE_SHARED = 1u << 0` in the
  prepared `Container.cow_state` byte (offset 4; public header remains eight
  bytes). New runtime objects start unique; mark is monotonic/idempotent;
  a copied destination clears the shared bit; source stays shared; static
  and arena containers are treated as shared. Helpers operate only on
  `Array`, `Map`, `Object`, `Element`, and `VMap`; definite scalars and
  ArrayNum do not enter them.
- **C2 — one-level copy primitives.**
  - Array copies its Item slots and marks container children shared.
  - Map/Object copy value storage while sharing `TypeMap`/`ShapeEntry`
    (DECIDED). `map_set_cow()` detaches/transitions Lambda-shared metadata
    before delegating to `fn_map_set()`; do not change JS raw shape paths.
  - Element copies both list and map facets under the same rule.
  - VMap gains the decided vtable snapshot/detach hook. Only
    `vmap_set_cow()` consults it. Existing `vmap_set()` and
    `vmap_host_set_by_item()` keep their JS/host in-place behavior. The
    HashMap backend clones table and insertion-order storage in one pass,
    preserving capacity/hash metadata where safe, and marks container keys
    **and** values shared. A unique, COW-capable VMap delegates directly to
    `vmap_set()`. Task handles and host backends without snapshot-stable value
    storage reject `_cow` mutation before raw delegation.
    Fixtures/counters prove: unique HashMap VMap mutation takes zero
    snapshots; the first shared mutation takes exactly one snapshot and
    preserves the old value; immutable/host-capability mutation invokes no
    external setter and reports rejection.
- **C3 — implement wrappers, then migrate Lambda callers only.**
  `array_set_cow()` / `map_set_cow()` / `vmap_set_cow()` and the collection
  operation wrappers perform prepare-write, Lambda capture marking, raw
  delegation, and replacement return. Refactor Lambda MIR/runtime index/field
  assignment, push/splice/pop, method receivers, and MarkEditor/edit-bridge
  paths to install the returned owner. Do not migrate JS transpiler/runtime
  call sites. Wire Lambda share events: binding/assignment (emission flip
  remains E), construction/insertion, return/capture, and retained Lambda
  `Item` ownership. Do not add JS-boundary marking. There is no special
  `PersistentFieldRef` rule; current uses are raw characters, not Items.
- **C4 — un-share-at-borrow for in-scope containers.** At `var`-param call
  sites and method-receiver binding, install a unique replacement before the
  callee receives its raw borrow. ArrayNum mutable-view behavior is unchanged
  until Stage 2.
- **C5 — ordering fixtures** (CW19 obligations): value args snapshot before
  any borrow's un-share/raw-write (C4.2c); store-target address resolved
  after RHS evaluation for `a[i] = g(var a)` shapes.
- **C6 — deep-copy and ArrayNum compatibility residue.** Split genuine
  boundary deep-copy (isolate messages / heap-exit materialization) into its
  own utility with a visited map. Preserve a narrow ArrayNum-specific
  `fn_mutable_value`/clone path until Stage 2; do not delete or replace it
  with the generic Item-container copier.

All C work remains behind `LAMBDA_COW=1`; both flag states retain current
observable semantics. Gate: Lambda and Radiant baselines, focused VMap and
nested-spine fixtures, ASan, and forced-GC stress. Add JS alias-preservation
fixtures for arrays, ordinary objects, shaped objects, globals, and host
VMaps: mutating through one JS alias must remain visible through the other,
and profiling must show no `_cow` call/branch in the JS store path. The
`let g`/`var h` probe remains the false-unique canary but does not flip yet.

## 4. Phase D — final hot path and performance gate

- Unique mutation: inline one byte load/test/branch on `cow_state`; no helper
  call, allocation, child scan, or path-object allocation. The cold arm calls
  the copy helper, delegates the update to the raw mutator, and installs the
  returned Item.
- Restore every direct MIR array/field store with the inline guard and cold
  branch; no permanent helper-only store path. This is Lambda `_cow`
  lowering only—JS direct/raw stores are not changed.
- CW2 static elision: provably-fresh containers skip the test. A
  definite-scalar RHS emits no share helper; a known container emits an
  inline OR; only dynamically typed values may call a helper.
- Regenerate `test/mir/mir_budgets.json` with rationale, then run the A4
  matched release A/B. **Phase E is blocked until the unique-mutation path
  has the intended instruction/allocation shape and every affected-row
  regression is explained.**
- Verify the JS MIR/runtime store inventory still resolves exclusively to raw
  setters. LambdaJS array/property mutation microbenchmarks must show no new
  COW instruction, helper, allocation, or aliasing change.

Expected release targets remain: collatz ≤ ~700 ms; splay recovers its 2.3×;
gcbench/binarytrees improve without over-promising literal/GC work; the
editor/document benchmark stays within noise of pre-C4; counters show
copies-taken ≪ share-marks. These focused targets prevent aggregate gains
from hiding a COW-specific regression.

**Overall Result11 target:** use the exact Result9/10 clean-release,
three-run-median, output-correctness-checked protocol. Result10 Lambda/MIR is
8.42× Node across its 52-row like-for-like population; Result9 is 4.80× on
those rows, with a 4.31× published all-timed deduplicated headline. Result11
must be materially better than Result10 overall and should be close to or
better than the corresponding Result9 result. This is an outcome target, not
a license to hide a hot-path regression in the geometric mean.

## 5. Phase E — semantic flip for Stage-1 kinds only

- Replace the generic-container `fn_mutable_value` anchor with share-marking
  at the binding/assignment emission sites; remove applicable
  `mir_var_rhs_keeps_mutable_alias` exemptions. The ANY-demotion goes with
  the generic wrapper. Retain the narrow ArrayNum compatibility path from C6.
- Retire `LAMBDA_COW` only after D passes, so there is no shipped slow regime.
- **Enumerated observable changes:** in-scope A2 probe goldens flip to C4
  semantics; `awfy/cd` receives its C4.3 restructuring; stale pre-C4
  `push`/`splice` sharing comments are corrected. ArrayNum/view and JS
  boundary behavior do not change in this phase.
- Gate: Lambda and Radiant baselines, ASan, forced-GC, correctness sweep over
  all timed rows, and release A/B. test262, Node, and JS gtests are
  shared-runtime regression gates only; they do not expand Stage-1 scope.

## 6. Phase F — migration audit + docs (C4.4 #3/#4/#7; non-gating docs)

- Sweep stdlib/fixtures for aliasing reliance (the Phase-E flip surfaces them as
  golden diffs — each gets a deliberate fix-or-annotate decision).
- Benchmark sources gain honest `var` annotations where they relied on the
  `pn` exemption (graph benchmarks: havlak/splay-class).
- Docs: rewrite `doc/Lambda_Func.md` "Mutable Captures" (aspirational,
  A10 family) to the C4 rules; state-idioms section; construction-captures
  teaching examples (§9.3's porting trap, the cd.ls lesson).

## 7. Rollout, gates, exit

**Order: A → B → C → D → E → F.** B before C so COW-on-objects lands on a
working object substrate; A0 keeps interim numbers honest; C establishes
correctness behind the flag, D proves the final hot path, and only E flips
observable semantics.

Per-phase gate (constant): `make build-test` clean → `make
test-lambda-baseline` 100 % (golden flips only where enumerated) → ASan on
touched suites → correctness sweep on all timed benchmark rows → counters +
affected-benchmark numbers recorded in this doc's completion record.
C/E add Radiant baseline and forced-GC stress with the `let g`/`var h`
canary. test262, Node, and JS gtests run as shared-runtime regression gates;
no JS boundary semantic change is accepted in Stage 1. The focused JS
alias-preservation fixtures and LambdaJS raw-store performance probes are
required in C/D because shared storage code is touched even though JS policy
is not.

**Exit = Result11** (absorbed from `Lambda_Impl_Tune.md` §5): full benchmark
protocol (3-run median, release, 180 s), output-correctness sweep
in-protocol, publish fresh (not in-place). The plan does not close merely
because correctness passes: the overall Lambda/MIR result must be materially
better than Result10 and target Result9-class or better, with focused COW
hot paths free of unexplained regressions. Then re-rank
`vibe/Lambda_Tuning_Proposal.md`'s R-queue on the new floor — R6b (M4) is
expected to be the next dominant Lambda-side mechanism after this plan.

## 8. Risks

1. **Golden churn discipline** — the Phase-E flip touches many fixtures; the
   enumerated-changes rule is the guard; any *unenumerated* diff is a bug.
2. **Editor/element perf** — one-level copies on huge-fan-out element nodes
   are O(width); the editor benchmark gates D/E, and the §9.5.1 chunked-node
   question stays open if it fails.
3. **VMap backend opacity** — a header-only copy is incorrect. The vtable
   snapshot hook and explicit rejection of mutable non-snapshot backends are
   required before VMap can be declared supported.
4. **B2/B3 hypotheses may be wrong** — they are stated as hypotheses;
   root-cause before fixing (rule 1), and if the object bugs turn out to
   live in the COW/anchor seam, B records that and C/E close them.
5. **Un-checked borrow overlap (Stage 1 by design)** — `f(x, x)` still
   aliases until Stage 2; the C4.1 fixtures that cover writer-writer overlap
   stay pinned at current behavior with a `// stage-2` marker, not flipped.
6. **Deferred performance-critical surfaces** — ArrayNum and JS↔Lambda
   ownership are intentionally unchanged. Any accidental generic COW branch
   in ArrayNum or observable buffer-boundary change is a Stage-1 regression.
7. **Raw-path contamination** — adding COW logic to `array_set()`,
   `fn_array_set()`, `fn_map_set()`, `vmap_set()`, or JS setters would change
   JS reference semantics and tax its hottest stores. CW21 requires new
   wrappers and a call-site inventory; a raw mutator containing a COW check is
   a design violation.

## 9. Completion record — 2026-07-23

### Delivered implementation

- Added the Stage-1 COW state and one-level detachment for Lambda `Array`,
  `Map`, `Object`, `Element`, and snapshot-capable `VMap`; raw LambdaJS and
  host setters remain raw.
- Added replacement-returning COW wrappers, VMap snapshot/rejection handling,
  precise-rooted nested owner-spine writeback, and release-safe COW counters.
- Fixed `pn` method receiver handling, parameterized methods, default-instance
  mutation, immutable capture diagnostics, and explicit `var` receiver checks.
- Migrated DeltaBlue, Splay, and CD to ownership-correct patterns. CD now uses
  read–modify–write for its voxel vectors and is timed by the standard runner.
- Added COW alias, ordering, VMap, raw-JS, forced-GC, MIR structure/budget, and
  nested Element snapshot regressions. The document gate builds 256 Element
  nodes, retains a snapshot, performs 2,048 nested edits, and serializes HTML.

### Verification

- `make test-lambda-baseline`: **1,474 / 1,474 passed**.
- Forced-GC alias, MIR emission, and MIR budget sentinels passed.
- COW profile for the document gate preserves the snapshot (`draft`) while the
  edited document becomes `published`; only the required shallow copies occur.
- COW profile for release Splay shows zero share marks/copies and zero legacy
  `fn_mutable_value` calls.
- The 40,261-entry Test262 baseline completed against the release executable.
- The Node baseline isolated one existing domain-runtime failure,
  `test-domain-emit-error-handler-stack.js` (active-domain expectation); its
  raw JavaScript array/object/map alias fixture remains green and no COW path
  is entered by the failing test.
- Fresh release Result11 data and report are published as
  `test/benchmark/benchmark_results_v11.json` and
  `test/benchmark/Overall_Result11.md`.

### Result11 comparison provenance

Result11 was run on the same Darwin arm64 host with a clean release build and
three-run medians, but the available Node changed from Result10's Node 22.13.0
to Node 24.7.0; QuickJS is unavailable. The checked-in Result10 JSON also
does not contain the report's revised values (for example, its raw MIR values
are collatz 7.47s and Splay 7.32s). Against that reproducible JSON, Result11
improves the COW-targeted absolute MIR timings: collatz 7.47s → 1.66s, gcbench
501ms → 381ms, array1 25.8ms → 3.81ms, primes 78.1ms → 55.3ms, and Splay 7.32s
→ 2.22s. The published cross-engine ratio is therefore not a like-for-like
Result10 comparison and must not be used to attribute a COW regression.

The Radiant baseline still reports its pre-existing failures in form/CSS
layout, `pdf_text_selection_copy`, `radiant_view_markdown_iframe`, and
`LoadsMarkdownIntoIframeAfterLinkClickWithNoLog`; page-load and render
baselines pass. No Radiant implementation files were changed by this work.

---

*Cross-refs:* design `vibe/Lambda_Design_COW.md` (CW1–CW21);
predecessor record `vibe/Lambda_Impl_Tune.md` (M1/M2, ex-M3 stub);
semantics `doc/Lambda_Formal_Semantics.md` §9 + `vibe/Lambda_Semantics_Formal.md`
C4.1–C4.4/C4.2a–c; tuning ledger `vibe/Lambda_Tuning_Proposal.md` (R6b = M4);
benchmarks `test/benchmark/Overall_Result9.md` and
`test/benchmark/Overall_Result10.md`.
