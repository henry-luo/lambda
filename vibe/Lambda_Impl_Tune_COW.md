# Lambda Impl Plan: COW Stage 1 + `pn`-Method Support (Tune-COW)

**Status: PLAN — 2026-07-23.**
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
bit-identical by contract. This plan **deliberately moves the aliasing line
to full C4** (Phase C3): `let`-finality becomes real, exempt-site aliasing
ends, the C4.1 probes flip from bug-pinning goldens to correct-behavior
goldens. Every observable change must be *enumerated in the phase that makes
it* and land as an explicit golden update — never as silent churn. Everything
else (representation, sharing, bit placement) stays invisible per P6.

---

## 0. Scope ledger — C4 outstanding items, in or out

Direct answer to "what else is outstanding from C4":

| Item | Source | Disposition |
|---|---|---|
| COW at mutation points (refcount/uniqueness-triggered) | C4.4 #1 | **Phase C** (as 1-bit `is_shared`, CW3) |
| `var`-param grammar | C4.4 #1 | **already parses** (`var_param_marker`, grammar.js:494/:554; `is_var_param` built at build_ast.cpp:7951, consumed at :2575, transpile-mir.cpp:394) — semantics audited in B4/C4, no grammar work expected |
| Compile check: `var` receiver for `pn` methods | C4.4 #1 | **Phase B4 — pulled forward from Stage 2.** Justification: it is a *binding-kind* check (is the receiver rooted at a `var` binding?), not an overlap analysis; and without it `frozen.increment()` stays a silent no-op — the C4.1 worst-case failure mode. Exclusivity proper stays out |
| Compile checks: var-args-only, exclusivity (all faces) | C4.4 #1 | **OUT — Stage 2**, `Lambda_Design_COW.md` §11 |
| Capture-assignment compile error (`fn` + `pn`), incl. interior mutation through captures | C4.4 #1 / C4.2a | **Phase B5** (replaces today's silent-continue and the nested-`pn` crash) |
| Object-mutation bug cluster: `pn` method with params fails to parse; default-instance mutation no-ops; second mutation lost | C4.4 #2 / C4.1 | **Phase B1–B3** — "needed under any semantics" |
| Migration audit (stdlib/fixture aliasing reliance, benchmark `var` signatures, editor/Radiant element paths) | C4.4 #3 | **Phase F** |
| Docs: delete/rewrite `Lambda_Func.md` "Mutable Captures"; aliasing/mutability section; state idioms; construction-captures teaching | C4.4 #4 / #7 | **Phase F** (non-gating, listed) |
| Formal model: store only for `var` bindings | C4.4 #5 | OUT — semantics-DSL project |
| Nested-mutation ergonomics (path borrows, `_modify`) | C4.4 #6 / §9.5.2 | OUT — needs its own design; Phase C makes the naive spelling *safe* (worst case O(spine)), which removes the urgency |
| Non-escaping nested-`pn` relaxation | C4.2a | OUT — deferred by record; interim idiom is the object form, which **Phase B unblocks** |
| Ordering fixtures: value-args-before-borrow; store-target re-resolution | C4.2c / CW19 | **Phase C5** |
| Snapshot iteration ruling | COW §11.6 | OUT — Stage 2 (candidate C4.2d) |
| `let g`/`var h` aliasing probes (canonical violation) | C4.1 | goldens **flip at Phase C3** |

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
  (profile wall was malloc/`hashmap_new`). **This code is deleted again in
  C3** — it exists so every intermediate measurement isn't dominated by a
  known-dead cost. Gate: baseline 100 %, bit-identical (pure representation).
- **A1 — CW5 counters, release-safe** (`js_exec_profile`-style): per TypeId —
  share-marks, mutations-on-unique (in-place), mutations-on-shared (copies),
  bytes copied; plus `fn_mutable_value` call rate while it still exists.
  These counters are the CW4 (saturating-count) decision data; they ship
  first so before/after spans the whole migration.
- **A2 — capture the C4.1 probe matrix as fixtures with CURRENT goldens**:
  `let g`/`var h`, var–var alias, object alias, default-instance no-op,
  second-mutation loss, frozen-receiver no-op, capture-assign behavior
  (`temp/REPRO_array_literal_alias.ls` + `temp/matrix.ls` promoted into
  `test/lambda/` with `.txt` goldens per rule 8). They pin the hybrid now and
  flip deliberately in B/C.
- **A3 — benchmark harness points**: record collatz/splay/gcbench/primes/
  array1 + the C4.3 **editor/document benchmark** (create if absent: element
  tree build → repeated small edits → serialize; this is the §9.5.1 gate).

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
  If the honest fix is the C3 aliasing-line move, record that and let C3
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

## 3. Phase C — COW core (Design §10 P1+P2)

- **C1 — the bit + helper.** `is_shared:1` in `Container.flags` spare bit
  (`lambda.h:678`; `padding[4]` confirms zero layout pressure). Runtime
  helper `fn_mark_shared(Item)` (containers only — the five-kind set; scalars
  untouched). `MarkBuilder` sets `is_shared` on static/arena containers at
  construction (CW8; generalizes `is_data_migrated` materialize-on-write).
  Set points wired: binding/assignment share (replacing anchor semantics —
  emission change is C3), construction-capture stores, JS ingress/egress
  (CW7), Radiant pin (`PersistentFieldRef`) sites.
- **C2 — mutation choke points consult the bit.** Every interior-mutation
  entry (index/field assign, `push`/`splice`/`pop`, method-receiver mutation,
  MarkEditor edits): `if (is_shared) cow_copy_level()` then mutate.
  `cow_copy_level` = one-level shallow copy + set `is_shared` on every
  *container* child (CW9); per kind: List/Array item array, ArrayNum
  `memcpy`, Map/Object data buffer + field walk, Element both natures.
  Introduced behind **`LAMBDA_COW=1`**; flag-off = today's behavior,
  bit maintained but not consulted. Gate: baseline 100 % both flag states;
  ASan; forced-GC stress (`LAMBDA_GC_FORCE_*`) flag-on — the `let g`/`var h`
  probe is the canary for a false-unique.
- **C3 — anchor retirement + the aliasing-line move (the big flip).**
  Transpiler emission at `transpile-mir.cpp:5506–5520` / `:9786–9794`:
  replace the `fn_mutable_value` wrap with share-marking; **delete** the
  deep-clone body, `MutableCloneContext`, and A0's stopgap; **remove the
  exemptions** (`mir_var_rhs_keeps_mutable_alias`) — `pn`-result, mutable
  identifier, and projection RHS now share-and-mark like everything else,
  closing the C4.1 aliasing bugs uniformly. The ANY-demotion at `:5518` goes
  with the wrapper (ex-M3(d) resolves by deletion — share-marking returns
  the same Item, no boxing, no type loss). Retire the `LAMBDA_COW` flag in
  this same phase — no two-regime limbo (the Go lesson, §9.4).
  **Enumerated observable changes:** A2 probe goldens flip to C4 semantics;
  `awfy/cd` gets its C4.3 restructuring (and `awfy/list` re-audited — both
  currently `wrong_output`-excluded); stale pre-C4 `push`/`splice` sharing
  comments corrected. Gate: baseline 100 % with the enumerated flips only;
  test262 + JS gtests (CW7 ingress marking touches the boundary); Radiant
  baseline; correctness sweep all timed rows.
- **C4 — un-share-at-borrow.** At `var`-param call sites, method-receiver
  binding (B4), and mutable-view creation (`lambda-vector.cpp:3082`/`:3180`,
  `lambda-data-runtime.cpp:647`): if the borrow root `is_shared`, COW-copy it
  first (CW16.4); after that the borrow writes raw (CW1 — no per-write cost).
- **C5 — ordering fixtures** (CW19 obligations): value args snapshot before
  any borrow's un-share/raw-write (C4.2c); store-target address resolved
  after RHS evaluation for `a[i] = g(var a)` shapes. Both as `.ls` fixtures
  with goldens.
- **C6 — boundary deep-copy utility.** The genuine deep-copy residue
  (isolate messages / heap-exit materialization) split into its own helper,
  *keeping* a visited map (eager deep copy of a DAG still needs dedup).
  During migration it also guards legacy aliased data; simplify to plain
  recursion only after the A2-flipped fixtures are green.

**Phase-C measurement targets** (release, rule 10): collatz ≤ ~700 ms
(R9 355 + M2-landed adds; remainder = M4, tracked at R6b); `splay` recovers
its 2.3×; gcbench/binarytrees improve (literal churn still allocs — R3/R7
territory, don't over-promise); **editor/document benchmark within noise of
pre-C4** (the C4.3 acceptance); counters show copies-taken ≪ share-marks.

## 4. Phase D — JIT fast path + static elision (Design §10 P3)

- Inline the `is_shared` test at hot mutation sites (load flags byte, `BT`)
  instead of the C-helper round trip; cold arm calls `cow_copy_level`.
- CW2 static elision: provably-fresh containers (literal not yet
  stored/passed; result of a copy the transpiler just emitted) skip the test
  entirely. Fold in ex-M3 remedy (c): a definite-scalar static type is
  decisive in `mir_expr_may_return_container` (`:327`) regardless of target
  type — no share-marking emission for provably-scalar RHS.
- MIR-emission budgets: regen `test/mir/mir_budgets.json` (manual lift with
  rationale, per MT7 discipline).

## 5. Phase E — views & boundaries, Stage-1 slice (Design §10 P4/P4b)

- MarkEditor / `edit_bridge` mutation entries honor the bit (may already be
  covered by C2's choke points — audit, don't duplicate).
- **CW17 detach-at-wrap**: Lambda→JS writable TypedArray egress copies once
  at wrapper creation when `is_shared`; JS-owned buffers never consult the
  bit (hot stores stay branch-free). Ingress marking landed in C1.
- **CW16.1 ArrayNum `==` representation-invariance fix**: value-equal
  Array vs ArrayNum (and ArrayNum vs ArrayNum across elem kinds) must
  compare equal — the Typed-Array-4 representation-sensitivity is a bug by
  the §9 ruling; folds into the OI-1 equality surface. Fixture: same values
  through both representations, `==` and `!=` both directions.
- Radiant pin audit (CW7): every `PersistentFieldRef` acquisition marks;
  Radiant baseline 100 %.

## 6. Phase F — migration audit + docs (C4.4 #3/#4/#7; non-gating docs)

- Sweep stdlib/fixtures for aliasing reliance (the C3 flip surfaces them as
  golden diffs — each gets a deliberate fix-or-annotate decision).
- Benchmark sources gain honest `var` annotations where they relied on the
  `pn` exemption (graph benchmarks: havlak/splay-class).
- Docs: rewrite `doc/Lambda_Func.md` "Mutable Captures" (aspirational,
  A10 family) to the C4 rules; state-idioms section; construction-captures
  teaching examples (§9.3's porting trap, the cd.ls lesson).

## 7. Rollout, gates, exit

**Order: A → B → C → D → E → F.** B before C so COW-on-objects lands on a
working object substrate; A0 keeps interim numbers honest; C is one
regime-flip commit (C3) inside an otherwise incremental sequence.

Per-phase gate (constant): `make build-test` clean → `make
test-lambda-baseline` 100 % (golden flips only where enumerated) → ASan on
touched suites → correctness sweep on all timed benchmark rows → counters +
affected-benchmark numbers recorded in this doc's completion record.
C-phases add: test262 + JS gtest suites, Radiant baseline, forced-GC stress
with the `let g`/`var h` canary.

**Exit = Result11** (absorbed from `Lambda_Impl_Tune.md` §5): full benchmark
protocol (3-run median, release, 180 s), output-correctness sweep
in-protocol, publish fresh (not in-place), then re-rank
`vibe/Lambda_Tuning_Proposal.md`'s R-queue on the new floor — R6b (M4) is
expected to be the next dominant Lambda-side mechanism after this plan.

## 8. Risks

1. **Golden churn discipline** — the C3 flip touches many fixtures; the
   enumerated-changes rule is the guard; any *unenumerated* diff is a bug.
2. **Editor/element perf** — one-level copies on huge-fan-out element nodes
   are O(width); the editor benchmark gates C3, and the §9.5.1 chunked-node
   question stays open if it fails.
3. **JS-boundary conservatism** (CW7 marks unconditionally) — counters
   decide whether a finer contract is ever worth designing.
4. **B2/B3 hypotheses may be wrong** — they are stated as hypotheses;
   root-cause before fixing (rule 1), and if the object bugs turn out to
   live in the C3 seam, B records that and C closes them.
5. **Un-checked borrow overlap (Stage 1 by design)** — `f(x, x)` still
   aliases until Stage 2; the C4.1 fixtures that cover writer-writer overlap
   stay pinned at current behavior with a `// stage-2` marker, not flipped.

---

*Cross-refs:* design `vibe/Lambda_Design_COW.md` (CW1–CW20);
predecessor record `vibe/Lambda_Impl_Tune.md` (M1/M2, ex-M3 stub);
semantics `doc/Lambda_Formal_Semantics.md` §9 + `vibe/Lambda_Semantics_Formal.md`
C4.1–C4.4/C4.2a–c; tuning ledger `vibe/Lambda_Tuning_Proposal.md` (R6b = M4);
benchmarks `test/benchmark/Overall_Result10.md`.
