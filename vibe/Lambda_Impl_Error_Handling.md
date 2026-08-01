# Lambda — Error Handling Implementation Plan (TE-15 / TE-16)

**Status:** NOT STARTED — 2026-08-01. Design decision-complete, docs already updated, no code
has moved.

**Design authority:** `vibe/Lambda_Design_Type_Enforcement.md` **TE-15** (soft-error
containment: skip to the closest safe boundary) and **TE-16** (the `^ { }` handler;
`let a^err` and `if (^err)` retired), building on TE-9 (failed checks produce rich error
*values*), TE-13 (unified discharge surface, tightness), and §10.7/§10.8 (return firewalls,
binding checkpoints). Normative text already lands in `doc/Lambda_Formal_Semantics.md` §7.3,
§11.4, §13 invariant 7.

**Related implementation plans:** `Lambda_Impl_Int_Total.md` (C16 — deletes the *arithmetic*
origination class this machinery would otherwise have to route; land it first or accept extra
churn), `Lambda_Impl_Type_Enforce (done).md` (round-2 enforcement, whose
`emit_checked_boundary` choke point is where TE-15 attaches).

**What is being built, in one line.** Failure stops travelling as a value through code that
assumes success: a failed boundary **skips** to the closest enclosing acceptor, and
`e ^ { … ~ … }` is the form that catches the skip with the error in hand.

---

## 1. Scope

Four coupled changes:

1. **TE-15 containment** — a failed deferred check jumps to the closest enclosing *safe
   boundary* rather than continuing with a substituted value. Three zones: annotated bindings
   (skip to end of block, block yields the error), container element positions (accept the
   error as data — the batch idiom), expression interiors (skipped, never evaluated).
2. **TE-16 handler** — `e ^ { … }` with `~` bound to the error; brace-delimited.
3. **Two retirements** — `let a^err = e` and prefix `if (^err)`.
4. **Acknowledgement taxonomy** — the E228 engagement set, including the new ruling that
   *skip does not acknowledge*.

**Out of scope / deliberately open:** statement-position defects in `pn` (a discarded-value
failed write, e.g. `acc = acc + x` in a loop, must escalate rather than evaporate — recommended
raise-channel escalation, **undecided**); lazy/streaming `for` bodies (KIV); flow-narrowing of
any kind (TE-16 is sound by construction and does not need it).

---

## 2. The invariant everything serves

> An error exists only (a) boxed in an Item-typed lane or slot, or (b) transiently on a
> resolved control edge from an origination site to its destination. **Native lanes are
> error-free by construction** — no operator in unboxed code ever receives an error operand,
> and the *emitter*, not the runtime, guarantees it.

Value-propagation through unboxed lanes was considered and rejected in TE-15: it requires an
in-band sentinel (today's accidental out-of-band `i64` *is* one, and its consumer-dependent
meaning was the measured O1 divergence), or re-boxing every lane an error might cross
(re-creating the flexint ANY-poisoning), or a polled side-flag (cost on every operation).

---

## 3. Phase order

```text
Phase A (surface: grammar, retirements)
    └── Phase B (AST, typing, engagement set, diagnostics)
            └── Phase C (TE-15 containment: destinations, origination, landing pads)
                    └── Phase D (MIR: control routing, cross-fn ABI, effect bit)
                            └── Phase E (corpus migration + tests)
```

Phases A–B are a self-contained, separately-landable surface change (the handler can lower to
today's contagion semantics initially). Phase C is the semantic change. Phase D is where the
performance property arrives. **Do not merge C into A/B** — a syntax change plus a control-flow
change in one commit makes bisecting a behavioural regression impossible.

### Phase A — surface

- **A1. Retire the prefix `^` operator.** It is the unary-operator choice at
  [grammar.js:542](../lambda/tree-sitter-lambda/grammar.js:542)
  (`choice('not', '!', '-', '+', '^', '*')`). Removing it is what buys the parse headroom for
  A3; `x is error` is the replacement (verified working 2026-08-01).
- **A2. Retire the destructure.** Grammar at
  [grammar.js:702](../lambda/tree-sitter-lambda/grammar.js:702)
  (`'^', field('error', choice($.identifier, $.symbol))`); AST side is `FIELD_ERROR` handling
  in `build_assign_expr` ([build_ast.cpp:5691-5697](../lambda/runtime/build_ast.cpp:5691)) and
  the second binding it creates at [:5863-5865](../lambda/runtime/build_ast.cpp:5863).
  `AstNamedNode.error_name` and its emitter uses go with it. **`pub x^err = …` disappears
  too** — check the module/export path for a dedicated branch.
- **A3. Add the braced infix handler.** `expr ^ { … }`. The discriminator is purely lexical:
  `^` followed by `{` is a handler; `^` followed by anything else (or nothing) is the existing
  postfix propagate ([grammar.js:466](../lambda/tree-sitter-lambda/grammar.js:466)). This
  preserves `f()^ - 1` as propagate-then-subtract, which **parses today and evaluates to 41**
  (verified) — the bare-expression alternative would silently reinterpret it as
  rescue-with-`-1` and yield 42.
  - Handler position must parse `{ … }` as a **block, not a map literal**; fn bodies already
    establish that precedent, so reuse the same rule rather than inventing one.
  - Keep the type-level `^` untouched
    ([:1080](../lambda/tree-sitter-lambda/grammar.js:1080), `:1139`).
  - `~` needs no new token: `current_expr` already exists
    ([grammar.js:535](../lambda/tree-sitter-lambda/grammar.js:535), `choice('~#', '~')`).
- **A4. `make generate-grammar`.** Never hand-edit `parser.c`. Expect `ts-enum.h` churn.

**Exit check for Phase A:** the retired forms fail to parse with a clear diagnostic, and every
existing `^`-propagate site still compiles unchanged.

### Phase B — AST, typing, diagnostics

- **B1. Handler node + `~` binding.** Model on the `match`-arm path, which already binds `~`
  per arm and narrows it (`AST_NODE_MATCH_ARM`,
  [build_ast.cpp:5516](../lambda/runtime/build_ast.cpp:5516); arm classification at
  [:9084](../lambda/runtime/build_ast.cpp:9084) `match_arm_is_error_handler`). Inside the
  handler `~` is the **error**, typed as the operand's error constituents (`E1 | E2`, or
  `error` when undeclared). Shadowing rule: innermost `~` wins (a handler inside a `match` arm
  shadows the arm's).
- **B2. Typing rule**, mirroring `or`:
  `type(e ^ h) = (type(e) \ error) | type(h)`. So `let a: T = e ^ h` requires `h : T` or a
  diverging `h`; unannotated bindings infer the union. Reuse the existing `or`-narrowing
  implementation (`lambda_type_remove_error_and_null` / `lambda_type_union_normalized` in
  `build_ast.cpp`) rather than writing a parallel one.
- **B3. Handler contract.** The handler either produces a value of the expected type or
  **diverges** — `raise`, `return`, or letting the enclosing block skip. `raise`/`return`
  inside the handler act on the enclosing frame (this is what makes it `let … else`). A
  handler that can do neither is a compile error.
- **B4. Engagement set (E228).** Acknowledgement for a raised `T^E` is exactly: a `match` arm
  on `error`; `e ^ { … }`; `e^`; or a receiving position that textually admits error
  (`let x: T^`, `let x: T | error`, a declared param/return of that shape). Remove the
  destructure from the set. **Add the new ruling: TE-15's skip does not acknowledge** — skip is
  automatic containment of *defects*, not user engagement, so a raised error stays
  compile-gated and never reaches the skip machinery.
- **B5. Rewrite the E228 diagnostic.** [build_ast.cpp:9078-9081](../lambda/runtime/build_ast.cpp:9078)
  advertises the retired form verbatim: *"use '%s(...)^' to propagate, 'let result^err = %s(...)'
  to capture, or '%s(...) or default' to recover"*. It must offer `%s(...) ^ { … }` instead.
  **This must land in the same commit as A2** or the compiler will teach syntax that no longer
  parses. (A sibling message was previously fixed at `:5199`; re-grep — line numbers moved with
  the 2026-08-01 tuning work.)

### Phase C — TE-15 containment

- **C1. Destination stack.** The emitter maintains the acceptor context lexically while
  lowering (same shape as the online-exception emission-time tracker in
  `Lambda_Impl_Online_Exception (done).md`). Destinations are static; there is no dynamic
  unwinding, and skips are always intra-function branches.
- **C2. Acceptor set** (zone 2 + engagement forms): container element positions (list/map/
  element children); `or`-left operands; `^ { }` handlers; postfix `^`; `match`/`is` scrutinee
  positions; positions typed `any`, `error`, `T | error`, `T^`; unannotated bindings (which
  infer the union and hold the value boxed). **The smallest enclosing block is the destination
  of last resort.** The fn body is the outermost block, so an uncontained defect becomes the
  function's result on §7.3's unenumerated system channel — **inference must never widen a
  signature because of defect possibility** (that would re-introduce the `| error` pandemic
  TE-15 explicitly rejected).
- **C3. Origination sites — a closed set**, each already computing its failure condition:
  the `lambda_type_check` boundaries funnelled through `emit_checked_boundary`
  ([transpile-mir.cpp:2443](../lambda/runtime/transpile-mir.cpp:2443), with the
  `emit_return_if_item_error` pairing at :2477-2478 and :2498-2499), fallible converters and
  sys-funcs, and calls to open/`^` callees. **Routing retargets existing branches — happy-path
  cost is zero.** (C16 deletes what would have been a fourth class, the flex-int promote edge.)
- **C4. Landing pads.** One per region containing origination sites: receive/box the error,
  root it, and restore the side-stack watermarks (`lambda_restore_number_frame_top` and the
  root-frame restore) — the existing single-funnel epilogue machinery generalized per block.
  Cross-check against the `return_value` + single `L2` epilogue structure the tuning doc's A3
  section describes; the same "one insertion point" property should hold per region.
- **C5. Sequence-point semantics.** Origination stops evaluation of the containing expression;
  operands and effects to its right never run. Vacuous inside `fn` (no effects to observe),
  meaningful inside `pn`. **Corollary to document at implementation time: strict left-to-right
  evaluation order becomes normative**, not incidental.

### Phase D — MIR and the ABI

- **D1. Routing vs contagion.** Boxed paths may keep value contagion ("error in, error out",
  already implemented in the helpers) wherever it is unobservable — i.e. pure `fn` code. `pn`
  bodies must emit control routing on **both** boxed and unboxed paths so the two agree on
  which effects ran. The test target is the equivalence itself (§5).
- **D2. Native-lane invariant** (§2) enforced at emission: no operator in unboxed code
  receives an error operand. `any`-provenance data cannot smuggle one in, because every
  `any`→native transition is already a checked boundary (TE-5 R3).
- **D3. Cross-function.** Interior routing reaching the outermost region becomes an error
  return; the *call site* is then an origination site in the caller. Boxed-returning calls
  carry the error in the result Item; native-returning calls use the existing context error
  lane (`FN_ERROR_LANE_CONTEXT_ITEM` — the same out-of-band lane the tuning doc's A3 section
  found already exists for `can_raise` native returns). One load-and-branch after the call: the
  Swift-`throws` shape.
- **D4. Effect bit + the polarity fix.** A callee whose compiled body provably contains **zero**
  origination sites needs no caller-side branch. `FnEffectSummary.may_return_error`
  ([ast-core.hpp:702-709](../lambda/runtime/ast-core.hpp:702)) already exists to carry it, but
  **today's gate has the wrong polarity**: `closed_item_result`
  ([transpile-mir.cpp:11689](../lambda/runtime/transpile-mir.cpp:11689)) treats a *missing*
  variant analysis as "trusted clean, skip the error branch". That is one half of the measured
  O1 divergence. It must become: **unknown ⇒ defect-capable; branch-free only on proof.** The
  effect is transitive in the implementation and invisible in types — signatures stay plain
  `T`, the §10.7 firewall holds.
- **D5. Rescue fast path.** In `let a: T = e ^ { 0 }`, the origination edge branches straight
  to the handler; **no error object need be materialized on a rescued path** (keep the
  origination log line as the breadcrumb). Rescue becomes cheaper than today's boxed
  round-trip, and `a` stays in its native lane throughout.

### Phase E — corpus migration and tests

- **E1. Migrate the corpus.** ~81 `.ls` files use the retired forms, across `test/std`
  (`negative/unhandled_error.ls`, `negative/error_propagation.ls`,
  `integration/error_safe_pipeline.ls`, `core/statements/error_handling.ls`,
  `core/datatypes/error_basic.ls`, `core/operators/error_propagation_op.ls`),
  `test/benchmark/beng` (`regexredux`, `knucleotide`, `revcomp` and their `2.ls` variants),
  `test/lambda`, `test/ui/rte_prototype.ls`, and `lambda/package` (`math/render.ls`,
  `latex/latex.ls`, `graph/transform/content.ls`, `graph/mermaid/config.ls`). Mechanical shape:
  `let a^err = e; if (^err) { H } else { B }` → `let a = e ^ { H }` then `B`. Per repo rule,
  every new/renamed `*.ls` keeps its `*.txt` golden in step.
- **E2. New tests.** Per zone: binding failure collapses the block to the error; a `for` body
  yields per-item errors and keeps iterating (the batch theorem); a container child keeps the
  error as an element; interior code after an origination is provably not evaluated (observable
  in `pn` via a side effect); handler diverging via `raise`/`return`/`continue`; `~` shadowing
  inside a `match` arm; `f()^ - 1` still parses as propagate-then-subtract.
- **E3. Negative tests.** Each retired form produces a clear diagnostic; the rewritten E228 text
  is asserted verbatim; `let x = raising_call()` still fails; **skip does not satisfy E228**.

---

## 4. Gates

- `make test-lambda-baseline` 100% per phase (`make test-radiant-baseline` unaffected).
- MIR budgets (`test/mir/mir_budgets.json`, MT7 0% slack) re-baselined per phase with
  justification. Phase C/D should be roughly **budget-neutral on the happy path** — routing
  retargets existing branches. A material growth means checks are being *added* rather than
  retargeted, which is the failure mode to catch.
- Forced-GC sweep after Phase C: landing pads introduce new join points where a boxed error is
  live; the P3 harness from `Lambda_Impl_MIR_Emission` is the right instrument.
- Re-run the typed benchmark columns after Phase D — D5 should show up on error-rescue-heavy
  rows, and nothing else should move.

## 5. Invariants to assert as tests

1. **Contagion ≡ routing wherever observable.** The same program compiled boxed and unboxed
   must agree on result *and* on which effects ran. This is the DF9-shaped property for this
   work and the single most valuable test.
2. **No binding holds a placeholder for a failure** (spec §13 invariant 7, as amended): every
   binding either has a real value or was never established.
3. **Native lanes never hold an error** — assert at emission (a debug-build check on operand
   provenance), not only by testing outputs.
4. **Skip is not acknowledgement** — a raised error may never reach a landing pad.
5. **Erasing the effect bit must not change results** — D4's branch-free optimization is
   unobservable, only slower when absent.

## 6. Open before coding

- **Statement-position defects in `pn`** (the hardest open item): a failed reassignment whose
  block value is discarded must not evaporate leaving a stale binding. Recommended: escalate to
  the raise channel or abort the loop. **Undecided — settle before Phase C.**
- Whether `or`-left rescue should also skip error-object materialization (D5 generalized).
- The exact engagement-set text once E228's acknowledgment forms are finalized.
- Lazy/streaming `for` bodies: where containment materializes for deferred evaluation (KIV).
