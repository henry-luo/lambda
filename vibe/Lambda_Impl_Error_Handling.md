# Lambda — Error Handling Implementation Plan (TE-15 / TE-16 / TE-17)

**Status:** NOT STARTED — rev 2, 2026-08-06. **Semantic decisions pending** (downgraded from
rev 1's "decision-complete"): two questions must be settled before the semantic phases can be
coded — statement-position defects in `pn`, and whether a handler may protect an expression
containing `await`. Everything else is decided. No code has moved.

**Design authority:** `vibe/Lambda_Design_Type_Enforcement.md` **TE-15** (soft-error
containment: skip to the closest safe boundary), **TE-16** (the `^ { }` handler; `let a^err`
and `if (^err)` retired), and **TE-17** (container acceptance is type-sensitive; native lanes
gate on provable infallibility — decided 2026-08-06), building on TE-9 (failed checks produce
rich error *values*), TE-13 (unified discharge surface, tightness), and §10.7/§10.8 (return
firewalls, binding checkpoints). Normative text already lands in `doc/Lambda_Formal_Semantics.md`
§7.3, §11.4, §13 invariant 7.

**Related implementation plans:** `Lambda_Impl_Int_Total.md` (C16 — deletes the *arithmetic*
origination class this machinery would otherwise have to route; land it first or accept extra
churn), `Lambda_Impl_Type_Enforce (done).md` (round-2 enforcement, whose `emit_checked_boundary`
choke point is where TE-15 attaches).

**Revision history.** Rev 2 (2026-08-06) responds to `Lambda_Review_Error_Handling.md`: phase
order inverted (the corpus migration must precede the grammar removal), the three error regimes
separated into explicit route kinds, TE-17 folded in, `may_defect` split from `can_raise`, the
landing-pad contract completed, the system-fault regime given its own treatment, and all line
anchors replaced with symbol anchors — **every line number in rev 1 had already moved** (e.g.
`closed_item_result` cited as `:11689` is at `:13975`; `transpile_local_fault_expression` cited
as `:7896` is at `:8648`). Rev 1's phase letters A–E map to rev 2's phases as
A→P2+P4, B→P2+P4, C→P5, D→P1+P5, E→P3+P4.

**What is being built, in one line.** Failure stops travelling as a value through code that
assumes success: a failed boundary **skips** to the closest enclosing acceptor whose *contract*
admits an error, and `e ^ { … ~ … }` is the form that catches the skip with the error in hand.

---

## 1. Scope

Six coupled changes:

1. **Route kinds** — the three error regimes stop sharing one routing model. Each origination
   site emits a typed route, and each destination declares which kinds it accepts (§4).
2. **TE-15 containment** — a failed deferred check jumps to the closest enclosing *safe
   boundary* rather than continuing with a substituted value.
3. **TE-17 lane gating** — acceptance is read from the destination *contract*; a value inferable
   only as `T | error` cannot enter a native lane at all (§5.2).
4. **TE-16 handler** — `e ^ { … }` with `~` bound to the error; brace-delimited. It must cover
   the system-fault regime too, not only returned error values (§6.2).
5. **Two retirements** — `let a^err = e` and prefix `if (^err)`.
6. **Effect analysis** — `may_defect` split from `can_raise`, computed as a conservative
   call-graph fixed point (§7.2).

**Out of scope / deliberately open:** statement-position defects in `pn`; handler-protected
`await`; lazy/streaming `for` bodies (KIV); flow-narrowing of any kind (TE-16 is sound by
construction and does not need it).

---

## 2. The invariants everything serves

Rev 1 stated one invariant that over-claimed. It is now three, because the third is what TE-17
buys and the first two are what §2 was actually protecting:

> **I1 — Storage.** No native lane slot ever holds an error. `ArrayNum` element storage, packed
> map-field storage, and native locals are error-free by representation: there is no Item word
> to put one in.
>
> **I2 — Operand.** No arithmetic or comparison operator in unboxed code ever receives an error
> operand. The *emitter*, not the runtime, guarantees it.
>
> **I3 — Eligibility (TE-17).** A value inferable only as `T | error` is not lane-eligible. It
> is carried boxed until the error is discharged. Lane entry requires *static* proof of
> error-freedom, so I1 and I2 hold by construction rather than by inserted guards, and **no
> error-routing edge is ever emitted inside a native lane.**

I3 is what makes this plan tractable: every routing destination is a boxed position, which is
exactly the set of positions that could already hold an error today. Phase 5 is therefore
substantially smaller than rev 1 assumed.

**What I1–I3 do not cover.** System and resource faults are untyped, so no static analysis can
gate them. They are a separate regime (§4) that unwinds through `LambdaRecoveryFrame` and
abandons any partially-built container. Rev 1's claim that "there is no dynamic unwinding" is
therefore **wrong as stated** and is corrected in §6.2: TE-15 *routing* is static, but fault
recovery demonstrably is not.

Value-propagation through unboxed lanes was considered and rejected in TE-15: it requires an
in-band sentinel (today's accidental out-of-band `i64` *is* one, and its consumer-dependent
meaning was the measured O1 divergence), or re-boxing every lane an error might cross
(re-creating the flexint ANY-poisoning), or a polled side-flag (cost on every operation).
TE-17 records the parallel rejection for *container* lanes.

---

## 3. Phase order

Rev 1's order was unlandable: it removed `let a^err` in phase A but migrated the corpus in
phase E, while claiming every phase passes the baseline. A live scan on 2026-08-06 finds **240
occurrences across 121 `.ls` files** (plus 20 `if (^…)` sites) — rev 1's "~81 files" undercounts
by a third. Removing the grammar first breaks the tree immediately.

The corrected order never has a red tree, and never mixes a syntax change with a control-flow
change in one commit:

```text
P0  Decisions        settle the two open semantics questions (§8)
 └─ P1  Infrastructure   route kinds, effect analysis, destination records, lane gating
     │                   — behaviour-neutral, no syntax change
     └─ P2  Add `^ { }`      alongside the legacy forms; both work
         └─ P3  Full handler   returned errors + system faults; migrate the corpus
             └─ P4  Retire      remove `let a^err` + prefix `^` + rewrite E228, atomically
                 └─ P5  Routing    TE-15 containment, intra- then cross-function
                     └─ P6  Fast paths   D5 rescue elision, `or`-rescue elision
```

**P4 is one atomic commit.** The grammar removal, the AST removal, and the E228 diagnostic
rewrite must land together or the compiler will teach syntax that no longer parses.

---

## 4. Route kinds — the three regimes, separated

Rev 1 placed all origination sites and all acceptors into one routing model while B4
simultaneously ruled that raised errors must never enter the skip machinery. Those cannot both
hold in one mechanism. Each route carries an explicit kind, and each destination declares the
set it accepts:

| Kind | Origin | Travels as | Accepted by |
|---|---|---|---|
| `SOFT_VALUE` | `T \| error` computations, fallible sys-funcs | an ordinary boxed value | any error-admitting contract; the batch idiom |
| `DEFECT_SKIP` | failed deferred checks, failed conversions/casts | a control edge to the closest accepting destination | destinations declaring `DEFECT_SKIP` |
| `RAISED_ERROR` | `raise`, calls to `T^E` callees | the declared error channel | E228 acknowledgment forms only |
| `FAULT` | stack overflow, OOM, resource faults | non-local unwind via `LambdaRecoveryFrame` | recovery frames only |

Without the kind, a later emitter change can silently deliver a raised error to a block landing
pad, which would break TE-16's acknowledgment taxonomy invisibly. Assert it (§9 invariant 4).

**Correction to rev 1's B4.** Rev 1 called its E228 set "exact" but omitted `or`, which both the
current validator (`validate_enforcing_calls_in_expression` in `build_ast.cpp`) and TE-13
recognize. The acknowledgment set is: a `match` arm on `error`; `e ^ { … }`; `e^`; `e or d`;
or a receiving position that textually admits error (`let x: T^`, `let x: T | error`, a declared
param/return of that shape). **`DEFECT_SKIP` does not acknowledge** — skip is automatic
containment of defects, not user engagement, so a raised error stays compile-gated and never
reaches the skip machinery.

---

## 5. P1 — Infrastructure (behaviour-neutral)

Nothing in this phase changes an observable result. It is separately landable and separately
bisectable, and it is what makes P5 small.

### 5.1 Destination records

The emitter maintains an acceptor context lexically while lowering (same shape as the
emission-time tracker in `Lambda_Impl_Online_Exception (done).md`). Each record carries — rev 1's
C4 named only the last two, and ordering among them is load-bearing:

- landing label, and the **set of route kinds accepted**;
- result/error home (where the delivered error lives);
- root-frame and number-stack checkpoints;
- task-scope depth and any required unwinding;
- recovery-frame cleanup obligation;
- **whether the region's result is observed** (§8.1 depends on this).

**Ordering contract.** The error must be adopted or rehomed into *destination-owned* storage
**before** the abandoned region is restored. If the payload or its number home belongs to the
region being torn down, restoring first hands the handler a dangling value. This is the
forced-GC test target in §9.

### 5.2 Lane-eligibility gate (TE-17) — replaces rev 1's C2

Rev 1's C2 listed acceptors *positionally*, including "container element positions (list/map/
element children)" unconditionally. That is unsatisfiable and contradicts I1: `ArrayNum`
element storage is a raw `int64_t*` / `double*` / packed byte array (`struct ArrayNum` in
`lambda.hpp` and its MIR mirror in `lambda.h`), so an `int[]` slot has no Item word for an
error. The same holds for declared map fields, `arr[i] = e`, field stores, `push`/`splice`, and
declared params and returns — all of which C2's framing missed.

**Acceptance is read from the destination contract, never from syntactic position.** The
predicate needs no new machinery; both halves already exist in
[`type_contract.hpp`](../lambda/runtime/type_contract.hpp):

- `lambda_type_accepts_error(Type*)` — admission (`any`, `error`, `T | error`, `T^`);
- `lambda_type_lane_storage_desc(Type*, LaneStorageDesc*)` — representation; already "returns
  false for abstract/heterogeneous contracts that must remain boxed".

Three outcomes, applied uniformly to every write destination:

| Destination contract | Outcome |
|---|---|
| admits error | **accept** — the error is the value; batch idiom, unchanged |
| native lane, source provably infallible | **enter the lane** — branch-free, I1/I2 hold |
| native lane, source only `T \| error` | **not lane-eligible** — carried boxed until discharged |

Consequences to encode:

- **A lane store is an origination site, never a destination.** This generalizes past literals
  and is what closes half of §8.1.
- **TE-15 zone 2 narrows** to container element positions *whose element contract admits error*.
- **Typed containers are all-or-nothing.** Per-element error retention is a capability of
  Item-lane containers — the honest reading of TE-13's "typed `int[]` stays clean-only".
- **Diagnose the silent case.** An annotated `let x: int[] = …` that cannot be satisfied is a
  compile error, which is visible. An *unannotated* `[f(x) for x in xs]` silently infers
  `(int | error)[]`, boxes, and loses SIMD and the typed-array path with no source signal. Emit
  a note when a container is boxed solely because of unproven fallibility.

### 5.3 Effect analysis — `may_defect` split from `can_raise`

`FnEffectSummary.may_return_error` (`ast-core.hpp`) is today overloaded, and its consumer has
the wrong polarity: `closed_item_result` (`transpile-mir.cpp`, in the native-scalar call path)
treats a *missing* variant analysis as "trusted clean, skip the error branch". That is one half
of the measured O1 divergence. Split into three:

- **`may_defect`** — implicit TE-15/system control effect. Conservative call-graph fixed point
  over recursion, indirect calls, imports, and unknown callees. **Unknown ⇒ defect-capable.**
- **`can_raise`** — the declared raised-error channel. Unchanged.
- **returned soft-error possibility** — derived from the semantic return type, not a bit.

**`may_defect` is load-bearing for performance, not a peephole.** Under TE-17, defect-capable
implies not lane-eligible, so every unanalyzed callee can cost a native lane, transitively.
This is why P1 must precede P5 rather than follow it.

The effect is transitive in the implementation and invisible in types — signatures stay plain
`T`, the §10.7 firewall holds, and inference must never widen a signature because of defect
possibility (that would re-introduce the `| error` pandemic TE-15 rejected). TE-17's
*element*-type widening is the narrow, principled exception: it is local and observable, where
signature widening is a pandemic.

### 5.4 Representation contract

P1 and P5 consume the explicit `MirValue` representation contract from the compiling-lane
proposal rather than creating a second raw-register provenance mechanism. Rev 1's D2 proposed
asserting operand provenance directly at emission; do it through `MirValue` or not at all.

---

## 6. P2/P3 — The handler

### 6.1 Surface

- **Add the braced infix handler.** `expr ^ { … }`. The discriminator is lexical — `^` followed
  by `{` is a handler, `^` followed by anything else is the existing postfix propagate — but
  **lexical is not a complete grammar specification**: postfix propagation currently attaches
  specifically to `call_expr` (`optional(field('propagate', '^'))` in `grammar.js`), so the
  handler's precedence and associativity must be stated explicitly and tested (§9).
  - `f()^ - 1` must still parse as propagate-then-subtract, which **parses today and evaluates
    to 41** (verified 2026-08-01). The bare-expression alternative would silently reinterpret it
    as rescue-with-`-1` and yield 42.
  - Handler position must parse `{ … }` as a **block, not a map literal**; fn bodies already
    establish that precedent, so reuse the same rule.
  - Keep the type-level `^` untouched.
  - `~` needs no new token: `current_expr` already exists in `grammar.js`.
- **`make generate-grammar`.** Never hand-edit `parser.c`. Expect `ts-enum.h` churn.

### 6.2 System-fault capture — the gap rev 1 missed

The old destructure is **not** merely syntactic sugar. For eligible procedural expressions it
installs a `LambdaRecoveryFrame` with a native `setjmp` landing point
(`transpile_local_fault_expression` / `transpile_error_destructure` in `transpile-mir.cpp`), and
`AstNamedNode.local_fault_safe` prevents that frame from spanning an async suspension. So rev
1's claim that the handler "can lower to today's contagion semantics initially" is incomplete:
contagion catches returned `ItemError` values but **not** native system/resource faults.

Before P4 can retire the old form, P3 must specify and implement:

- how `e ^ { … }` installs or reuses `LambdaRecoveryFrame`;
- whether a handler may protect an expression containing `await` (**open — §8.2**);
- if allowed, how fault recovery is represented across task polls;
- how the frame is retired on each exit: normal completion, handler entry, `return`, `raise`,
  `continue`.

### 6.3 AST and typing

- **Handler node + `~` binding.** Model on the `match`-arm path, which already binds `~` per arm
  and narrows it (`AST_NODE_MATCH_ARM`; arm classification in `match_arm_is_error_handler`).
  Inside the handler `~` is the **error**, typed as the operand's error constituents (`E1 | E2`,
  or `error` when undeclared). Implement `~` through a **nested current-value context stack** —
  the existing match/pipe machinery is not sufficient for reliable innermost shadowing or
  precise error typing. Shadowing rule: innermost `~` wins.
- **Typing rule**, mirroring `or`: `type(e ^ h) = (type(e) \ error) | type(h)`. So
  `let a: T = e ^ h` requires `h : T` or a diverging `h`; unannotated bindings infer the union.
  Reuse the existing `or`-narrowing implementation (`lambda_type_remove_exclusions` /
  `lambda_type_union_normalized`) rather than writing a parallel one.
- **Handler contract, stated in terms of normal completion.** The handler either produces a
  value of the expected type or **does not complete normally** — `raise` or `return`. Rev 1 also
  listed "letting the enclosing block skip", but that is a *runtime* possibility, not a static
  divergence, and cannot discharge the typing obligation. `raise`/`return` inside the handler
  act on the enclosing frame (this is what makes it `let … else`). A handler that can do neither
  is a compile error.

### 6.4 Corpus migration (P3, before the retirement)

240 occurrences across 121 `.ls` files, plus 20 `if (^…)` sites, across `test/std`
(`negative/unhandled_error.ls`, `negative/error_propagation.ls`,
`integration/error_safe_pipeline.ls`, `core/statements/error_handling.ls`,
`core/datatypes/error_basic.ls`, `core/operators/error_propagation_op.ls`),
`test/benchmark/beng` (`regexredux`, `knucleotide`, `revcomp` and their `2.ls` variants),
`test/lambda`, `test/ui/rte_prototype.ls`, and `lambda/package` (`math/render.ls`,
`latex/latex.ls`, `graph/transform/content.ls`, `graph/mermaid/config.ls`).

Mechanical shape: `let a^err = e; if (^err) { H } else { B }` → `let a = e ^ { H }` then `B`;
`if (^err)` → `if (err is error)`. Sites relying on `local_fault_safe` recovery need §6.2
complete first — identify them before starting, they are not mechanical. Per repo rule, every
new/renamed `*.ls` keeps its `*.txt` golden in step.

---

## 7. P4/P5 — Retirement and routing

### 7.1 P4 — the atomic retirement

- Retire the prefix `^` operator: the unary-operator choice in `grammar.js`
  (`choice('not', '!', '-', '+', '^', '*')`). Removing it is what buys the parse headroom;
  `x is error` is the replacement (verified working 2026-08-01).
- Retire the destructure: the `'^', field('error', …)` production in `grammar.js`; the
  `FIELD_ERROR` handling in `build_assign_expr` and the second binding it creates
  (`build_ast.cpp`); `AstNamedNode.error_name` and its emitter uses. **`pub x^err = …`
  disappears too** — the module/export path has a dedicated branch (`build_ast.cpp`, in the pub
  declaration path).
- **Rewrite the E228 diagnostic.** It advertises the retired form verbatim: *"use '%s(...)^' to
  propagate, 'let result^err = %s(...)' to capture, or '%s(...) or default' to recover"*
  (`build_ast.cpp`, in the enforcing-call validation path). It must offer `%s(...) ^ { … }`
  instead, and keep the `or` form.

### 7.2 P5 — containment

- **Origination sites — a closed set**, each already computing its failure condition: the
  `lambda_type_check` boundaries funnelled through `emit_checked_boundary` (with the
  `emit_return_if_item_error` pairings alongside it), fallible converters and sys-funcs, calls
  to open/`^` callees, and — added by TE-17 — **native-lane stores**. Routing retargets existing
  branches. (C16 deletes what would have been a further class, the flex-int promote edge.)
- **Acceptor set** = destinations whose contract admits error (§5.2), plus the engagement forms:
  `or`-left operands, `^ { }` handlers, postfix `^`, `match`/`is` scrutinee positions, positions
  typed `any` / `error` / `T | error` / `T^`, and unannotated bindings. **The smallest enclosing
  *observed* block is the destination of last resort** — see §8.1, this is not yet safe as
  stated. The fn body is the outermost block, so an uncontained defect becomes the function's
  result on §7.3's unenumerated system channel.
- **Sequence-point semantics.** Origination stops evaluation of the containing expression;
  operands and effects to its right never run. Vacuous inside `fn`, meaningful inside `pn`.
  **Corollary to document at implementation time: strict left-to-right evaluation order becomes
  normative**, not incidental.
- **Routing vs contagion.** Boxed paths may keep value contagion ("error in, error out", already
  implemented in the helpers) wherever it is unobservable — i.e. pure `fn` code. `pn` bodies must
  emit control routing so the two agree on which effects ran. The test target is the equivalence
  itself (§9 invariant 1).
- **Cross-function.** Interior routing reaching the outermost region becomes an error return; the
  *call site* is then an origination site in the caller. Boxed-returning calls carry the error in
  the result Item; native-returning calls use the existing context error lane
  (`FN_ERROR_LANE_CONTEXT_ITEM`). One load-and-branch after the call: the Swift-`throws` shape.
  **Correct rev 1's "happy-path cost is zero"**: intra-function retargeting is free, but an
  effectful cross-function native call adds exactly this branch.
- **Effect bit.** A callee with `may_defect == false` needs no caller-side branch (§5.3).

### 7.3 P6 — fast paths (optional, after correctness)

- **Rescue elision.** In `let a: T = e ^ { 0 }`, the origination edge branches straight to the
  handler and no error object need be materialized (keep the origination log line as the
  breadcrumb). Valid **only when `~` is unused in the handler** and the origin supports a
  status-only path — rev 1 stated this unconditionally. `a` stays in its native lane throughout.
- Whether `or`-left rescue should also elide materialization (same conditions).

---

## 8. Open before P1 completes

### 8.1 Statement-position defects in `pn` — blocking P5

A failed reassignment whose block value is discarded must not evaporate leaving a stale binding
(`acc = acc + x` in a loop). **"The smallest enclosing block" is not always a safe destination:
if that block's result is discarded, the defect vanishes and an earlier binding stays visible.**

Recommended: track whether a region's result is observed (§5.1's destination record); never land
a defect in a discarded-value region; route to the closest observed or error-preserving
destination; if none exists, exit through the function's implicit defect channel. **Do not call
this "raise-channel escalation"** — that would convert a defect into a user-raised error and
contradict TE-16's acknowledgment taxonomy.

TE-17 closes part of this already: a native-lane store is an origination site, never a
destination, so `arr[i] = f(x)` on `int[]` cannot evaporate — it is a compile-time obligation.
The residue is the discarded-result *block* case. **Undecided.**

### 8.2 Handler-protected `await` — blocking P3

`local_fault_safe` exists precisely because a recovery frame must not span an async suspension.
Either the handler inherits that restriction (simplest; `e ^ { … }` over an `await`-containing
expression is a compile error) or fault recovery must be represented across task polls. **
Undecided** — the first option is recommended and should be confirmed.

### 8.3 Non-blocking residue

- Whether TE-17's `T | error` demotion is transitive through containers: does discharging
  re-narrow `(int | error)[]` to `int[]` in place, or only by copy? Copy is the safe default;
  in-place needs the COW exclusivity rules.
- Lazy/streaming `for` bodies: with a typed-lane destination and a streaming source,
  representation cannot be chosen before consumption. Boxed-until-proven is consistent with
  TE-17 and is the presumed answer (KIV).
- The exact engagement-set text once E228's acknowledgment forms are finalized.

---

## 9. Gates and invariants

**Per-phase gates.**

- `make test-lambda-baseline` 100% every phase (`make test-radiant-baseline` unaffected).
- `make test262-baseline` additionally after any change to the shared MIR call ABI or
  `Context.mir_return_lane` — P1 and P5 both touch it.
- MIR budgets (`test/mir/mir_budgets.json`, MT7 0% slack) re-baselined per phase with
  justification. P5 should be roughly **budget-neutral on the happy path** — routing retargets
  existing branches. Material growth means checks are being *added* rather than retargeted,
  which is the failure mode to catch.
- Forced-GC sweep after P5: landing pads introduce new join points where a boxed error is live;
  the P3 harness from `Lambda_Impl_MIR_Emission` is the right instrument.
- Re-run the typed benchmark columns after P5 and P6. P1's `may_defect` analysis is the
  performance-critical piece under TE-17 — regressions there show up as lost native lanes, not
  as extra branches.

**Invariants to assert as tests.**

1. **Contagion ≡ routing wherever observable.** The same program compiled boxed and unboxed must
   agree on result *and* on which effects ran. The DF9-shaped property for this work, and the
   single most valuable test.
2. **No binding holds a placeholder for a failure** (spec §13 invariant 7, as amended): every
   binding either has a real value or was never established.
3. **Native lanes never hold an error** (I1) and **no operator receives one** (I2) — asserted at
   emission through the `MirValue` contract, not only by testing outputs.
4. **Route kinds are respected** — a `RAISED_ERROR` may never reach a `DEFECT_SKIP` landing pad.
5. **Forcing every function to `may_defect = true` must not change observable results.** Note
   the polarity: rev 1 said "erasing the effect bit must not change results", which is backwards
   — *clearing* the bit removes necessary checks and is unsafe. Only the conservative direction
   is a valid differential.

**Test additions beyond the per-zone tests.**

- A handler grammar/precedence matrix: calls, operators, pipes, member access, indexing,
  parentheses, nested handlers, `or`, and `f()^ - 1`.
- Typed-container rejection versus untyped/error-admitting container acceptance (TE-17), both
  directions, including `arr[i] = f(x)` on `int[]` versus `(int | error)[]`.
- System/resource-fault capture by the new handler; explicit async/`await` handler behaviour.
- Task-scope cleanup and forced-GC tests proving handler error payloads survive restoration
  (§5.1's ordering contract).
- A differential mode compiling once with normal effect analysis and once with every callee
  conservatively `may_defect`, comparing values and observable effects (invariant 5).
- Per zone: binding failure collapses the block to the error; a `for` body yields per-item errors
  and keeps iterating (the batch theorem, on an Item-lane container); interior code after an
  origination is provably not evaluated (observable in `pn` via a side effect); handler diverging
  via `raise`/`return`; `~` shadowing inside a `match` arm.
- Negative: each retired form produces a clear diagnostic; the rewritten E228 text is asserted
  verbatim; `let x = raising_call()` still fails; **skip does not satisfy E228**.
