# Lambda — Error Handling Implementation Plan (TE-15 / TE-16 / TE-17 / TE-18)

**Status:** IMPLEMENTATION IN PROGRESS — rev 7, 2026-08-17. **Semantically decision-complete;
the handler/propagation grammar and legacy error-syntax retirement slices are landed.** Rev 7
records removal of `let a^err = e` and prefix `^expr` from the grammar, AST/runtime, and active
`.ls` corpus, alongside the S7.6.2v2/S7.6.3v2 postfix-primary grammar. Remaining phases cover
declaration-boundary routing, system-fault capture, and proof fixtures.

**Pre-handler prerequisite:** P1F resolves the SIGSEGV/SIGBUS and alternate-stack ownership
conflict between `lambda-stack.cpp` and batch mode (§8.3) before fault capture by `^ { }` is
implemented or tested.

**Design authority:** `vibe/Lambda_Design_Type_Enforcement.md` **TE-15** (soft-error
containment at declared contract boundaries), **TE-16** (the `^ { }` handler; `let a^err`
and `if (^err)` retired), **TE-17** (container acceptance is type-sensitive; native lanes gate
on provable infallibility — decided 2026-08-06), and **TE-18** (skip is a declaration-boundary
mechanism; the guard dominates the scope — decided 2026-08-06), building on TE-9 (failed checks
produce rich error *values*), TE-13 (unified discharge surface, tightness), and §10.7/§10.8
(return firewalls, binding checkpoints). `doc/Lambda_Formal_Semantics.md` remains normative; P0
must reconcile its §7.3, §11.4, and §13 invariant 7 wording with these accepted decisions.

**Related implementation plans:** `Lambda_Impl_Int_Total (done).md` (C16 already deletes the
*arithmetic* origination class this machinery would otherwise have to route) and
`Lambda_Impl_Type_Enforce (done).md` (round-2 enforcement, whose `emit_checked_boundary` choke
point is where TE-15 attaches).

**Revision history.** Rev 6 (2026-08-12) makes `e ^ { h }` and `e^` left-associative
postfix-primary forms at the member/query tier, gives those constructs sole ownership of their
carets, requires parentheses around a wider operand, and retires prefix-handler shorthand and
the split call/literal/binary/member handler grammar. Rev 5 (2026-08-06) assigns the
handler-local current error to `^`: bare
`^`, `^.field`, and `^[index]` are valid only inside an active handler body, while `~` keeps its
existing current-value semantics. Rev 4 (2026-08-06) incorporates the remaining review
revisions: `SOFT_VALUE`
is explicitly not a control route; declaration skips, raised errors, and faults use separate
contexts; function-boundary defect materialization is specified; fixed native stores receive a
single classification; `may_defect` excludes resource faults; fault-catching is procedural;
precedence has executable examples; V1 guard deletion is proof-scoped; P1F owns signal chaining;
and stale observed-block language is retired. Rev 3 (2026-08-06) records two user decisions:
`e ^ { … }` is an ordinary value-producing expression whose surrounding declared boundary runs
afterward; in statement
position `pn_call() ^ { … }` runs a handler body and continues; and a failed cross-frame
reassignment returns from the mutating callable without routing into the outer binding's frame.
The call invalidates later reads of each possibly-written captured binding until an explicit
caller assignment re-establishes it.
Rev 2 (2026-08-06) responds to `Lambda_Review_Error_Handling.md`: phase
order inverted (the corpus migration must precede the grammar removal), the three error regimes
separated into explicit route kinds, TE-17 folded in, `may_defect` split from `can_raise`, the
landing-pad contract completed, the system-fault regime given its own treatment, and all line
anchors replaced with symbol anchors — **every line number in rev 1 had already moved** (e.g.
`closed_item_result` cited as `:11689` is at `:13975`; `transpile_local_fault_expression` cited
as `:7896` is at `:8648`). Rev 1's phase letters A–E map to rev 2's phases as
A→P2+P4, B→P2+P4, C→P5, D→P1+P5, E→P3+P4.

**What is being built, in one line.** Soft errors remain ordinary values in expressions; a
failed declared boundary cannot establish or update its target and follows its local containment
rule; `e ^ { … ^ … }` explicitly recovers an error outcome before any surrounding boundary is
checked.

---

## 1. Scope

Six coupled changes:

1. **Outcome mechanisms** — soft values, checked-boundary skips, raised errors, and resource
   faults stop sharing one routing model. Only local checked-boundary defects use static landing
   destinations; soft errors are values, raised errors use the declared channel, and faults use
   recovery frames (§4).
2. **TE-15 containment** — a failed deferred check follows the boundary-specific rule in §7.2:
   local establishment/reassignment skips to the declaring region, parameter mismatch becomes
   the call-expression error, and return mismatch becomes the callable's defect outcome.
3. **TE-17 lane gating** — acceptance is read from the destination *contract*; a value inferable
   only as `T | error` cannot enter a native lane at all (§5.2).
4. **TE-16 handler** — brace-delimited with `^` bound to the handled error: value-producing
  `e ^ { … }` in expression context, and continuing `pn_call() ^ { …; }` in statement context.
  The handler does not rebind `~`; pipes, matches, and object contexts keep their ordinary
  current-value meaning.
   In procedural context it also covers the system-fault regime; pure `fn` handlers cover value
   and raised-error outcomes only (§6.2).
5. **Two retirements** — `let a^err = e` and prefix `if (^err)`.
6. **Effect analysis** — `may_defect` split from `can_raise`, computed as a conservative
   call-graph fixed point (§7.2).

**Out of scope / deliberately open:** lazy/streaming `for` bodies (KIV) and general type
flow-narrowing (TE-16 is sound by construction and does not need it). S3's narrow definite-state
analysis for hidden outer writes, statement-position handlers, and handler-protected `await` are
decided (§6.1, §8.1, §8.2).

---

## 2. The invariants everything serves

Rev 1 stated one invariant that over-claimed. It is now four; I3 and I4 are the TE-17/TE-18
constraints that make I1 and I2 enforceable:

> **I1 — Storage.** No native lane slot ever holds an error. `ArrayNum` element storage, packed
> map-field storage, and native locals are error-free by representation: there is no Item word
> to put one in.
>
> **I2 — Operand.** No arithmetic or comparison operator in unboxed code ever receives an error
> operand. The *emitter*, not the runtime, guarantees it.
>
> **I3 — Eligibility (TE-17).** A value inferable only as `T | error` is not lane-eligible. It
> is carried boxed until the error is discharged. Lane entry requires *static* proof of
> error-freedom, so I1 and I2 hold by construction rather than by per-use guards. A deferred
> store check branches before mutation; an error value is never represented inside a native lane.

> **I4 — Dominance (TE-18).** A declaration with a native contract guards **once, on entry**,
> and that guard dominates every use of the binding in its scope. Inside the scope the binding
> is native-lane unconditionally, with no per-use error check. Later reassignment and fixed-lane
> stores are new checked boundaries, not reasons to weaken the established binding. Local
> establishment/reassignment/store failure may skip; parameter and return checks use their
> boundary-specific outcomes (§7.2). **Expression interiors never skip.** A failed cross-frame
> reassignment stops at the mutating callable's boundary rather than jumping into the outer
> binding's declaring block. The outer binding still contains a valid `T`, but a possibly-writing
> call makes it statically unavailable to the caller until an explicit definite assignment
> re-establishes it.

I3 and I4 are what make this plan tractable. Every failed boundary adopts its error into a boxed
result/error home before transfer, and every routing source was already emitting a check. Phase
5 adds no skip edge to expression composition at all and is therefore far smaller than rev 1
assumed.

**What the lane invariants do not cover.** System and resource faults are untyped, so no static
analysis can gate them. They are a separate regime (§4) that unwinds through
`LambdaRecoveryFrame` and abandons any partially-built container. Rev 1's claim that "there is no
dynamic unwinding" is therefore **wrong as stated** and is corrected in §6.2: TE-15 *routing* is
static, but fault recovery demonstrably is not.

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
P0  Contract sync      reconcile formal semantics; lock the parse and context tables
 └─ P1  Infrastructure   effect analysis, defect destinations, lane gating
     │                   — behaviour-neutral, no syntax change and no new diagnostics
     └─ P1F Fault foundation signal/altstack ownership and recovery-frame tests
         └─ P2  Add `^ { }`      returned/raised outcomes alongside the legacy forms
             └─ P3  Full handler   add procedural fault capture; migrate corpus
                 └─ P4  Retire      remove `let a^err` + prefix `^` + rewrite E228, atomically
                     └─ P5  Routing    TE-15 containment, intra- then cross-function
                         └─ P6  Fast paths + lane notes   rescue elision, opt-in lane-loss notes
```

**P0 is documentation plus parse fixtures, not codegen.** `doc/Lambda_Formal_Semantics.md` must
say declaration-boundary/declaring-block containment, type-sensitive container acceptance, the
two handler contexts, and procedural-only fault capture before implementation begins. The formal
spec remains the semantic authority; the plan may not knowingly carry a contradictory summary.
P0 also locks diagnostic classes: hard errors for provable contract mismatch, illegal handler
context/`await`, and reading an S3-invalidated binding; warnings for deferred same-frame
reassignment/store with a syntactic tail to abandon; a structured runtime report only when such
a deferred failure actually fires; and opt-in lane-loss notes only in P6. Allocate stable codes
after auditing the live diagnostic registry rather than inventing numbers in this document.

**P4 is one atomic commit.** The grammar removal, the AST removal, and the E228 diagnostic
rewrite must land together or the compiler will teach syntax that no longer parses.

---

## 4. Outcome mechanisms — four regimes, no shared acceptor stack

Rev 1 placed all origination sites and acceptors into one routing model. Rev 4 makes the stronger
separation explicit: `SOFT_VALUE` is a classification, **not a control route**, and the other
three mechanisms do not share destinations.

| Kind | Origin | Operational mechanism | Terminal/consumer |
|---|---|---|---|
| `SOFT_VALUE` | `T \| error` computations, fallible sys-funcs, a defect materialized at a call boundary | ordinary boxed expression result; existing contagion | normal contextual typing; error-admitting storage; engagement form |
| `DEFECT_SKIP` | failed runtime checks at local establishment/reassignment boundaries, plus the dynamic residue of fixed native stores | static intra-function branch carrying a rich error | the affected binding/container's declaring-block landing pad, or the current function result |
| `RAISED_ERROR` | `raise`, calls to `T^E` callees | existing declared error lane/return ABI | E228 acknowledgment forms only |
| `FAULT` | stack overflow, OOM, comparison-depth/resource faults | dynamic non-local unwind through `LambdaRecoveryFrame` | an eligible procedural recovery frame or the execution boundary |

There is consequently no generic “accepted route-kind set.” The emitter owns a
`DefectDestination` stack only for checked-boundary skips; E228 owns raised-channel engagement;
ordinary expression lowering owns soft values; and the runtime TLS stack owns faults. Route kind
is compile-time edge metadata, not a new tag added to the rich error payload.

**Cross-function transition.** A `DEFECT_SKIP` that reaches the current function boundary is
returned through the boxed result or `FN_ERROR_LANE_CONTEXT_ITEM`. At the caller it materializes
as a `SOFT_VALUE` call result; it does not remain a skip edge and does not jump to a caller block.
Only a caller-local checked boundary consuming that result can originate a new `DEFECT_SKIP`.
For a `T^E` call, the declared error lane remains E228-gated, so its static edge is never
retargeted to a declaration landing pad. A `FAULT` is transparent to that lane and unwinds only
through recovery frames. Assert that separation (§9 invariant 4).

**Correction to rev 1's B4.** Rev 1 called its E228 set "exact" but omitted `or`, which both the
current validator (`validate_enforcing_calls_in_expression` in `build_ast.cpp`) and TE-13
recognize. The acknowledgment set is: a `match` arm on `error`; `e ^ { … }`; `e^`; `e or d`;
or a receiving position that textually admits error (`let x: T^`, `let x: T | error`, a declared
param/return of that shape). **`DEFECT_SKIP` does not acknowledge** — skip is automatic
containment of defects, not user engagement, so a raised error stays compile-gated and never
reaches the skip machinery.

---

## 5. P1 — Infrastructure (behaviour-neutral)

Nothing in this phase changes an observable result. P1 introduces and computes the metadata,
decision APIs, and shadow assertions below, but leaves current branches, diagnostics, and lane
choices intact. P5 switches lowering to those decisions. This keeps P1 separately landable and
bisectable while making P5 small.

### 5.1 Defect-destination records

The emitter maintains a `DefectDestination` context lexically while lowering (same shape as the
emission-time tracker in `Lambda_Impl_Online_Exception (done).md`). It is consulted only by a
failed local checked boundary; soft values, raised errors, and faults never query it. Each
record carries:

- the declaring-block landing label and boundary kind (`establish`, `reassign`, `store`); for a
  store, the region is the declaring block of the container root binding;
- result/error home (where the delivered rich error lives);
- root-frame and number-stack checkpoints;
- task-scope depth and any required unwinding;
- recovery-frame cleanup obligation when the branch leaves a protected procedural region;
- diagnostic identity: binding/store name and source region for case 7/S1 reporting.

TE-18 deliberately does **not** route by whether a block result is observed. A same-frame failure
lands at the block that declares the affected binding; a per-item declaration inside a discarded
`for_stam` body may therefore end that iteration with only the origination/report breadcrumb.
That is ordinary discard after containment, not permission to route to a different lexical block.

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
[`type_contract.hpp`](../../lambda/runtime/type_contract.hpp):

- `lambda_type_accepts_error(Type*)` — admission (`any`, `error`, `T | error`, `T^`);
- `lambda_type_lane_storage_desc(Type*, LaneStorageDesc*)` — representation; already "returns
  false for abstract/heterogeneous contracts that must remain boxed".

The outcome also depends on whether representation is still being inferred or is fixed by a
declared destination. P1 records this decision in shadow/assert mode; P5 activates it:

| Destination/source | Outcome |
|---|---|
| contract admits error | **accept** — the error is the value; batch idiom, unchanged |
| inferred container + source `T \| error` | choose an Item-lane `(T \| error)[]`; retain per-item errors |
| fixed native destination + source provably `T \| error` | **compile error** — explicit fallibility must be discharged before the write |
| fixed native destination + dynamic/unproven source | emit one checked store boundary; success enters the lane, failure is S1 `DEFECT_SKIP` plus report; the failed store itself does not mutate |
| fixed native destination + source provably infallible | enter the lane branch-free; I1/I2 hold |

Consequences to encode:

- **A fixed lane store is a checked boundary, never an error destination.** Statically explicit
  fallibility is rejected; only the dynamic/unproven residue can originate S1 `DEFECT_SKIP`.
- **TE-15 zone 2 narrows** to container element positions *whose element contract admits error*.
- **Typed containers are all-or-nothing.** Per-element error retention is a capability of
  Item-lane containers — the honest reading of TE-13's "typed `int[]` stays clean-only".
- **Diagnose the silent case in P6, not behaviour-neutral P1.** An annotated
  `let x: int[] = …` that cannot be satisfied is already a compile error. An unannotated
  `[f(x) for x in xs]` may infer `(int | error)[]` and lose the typed-array path; an opt-in
  performance note explains that demotion after correctness lands.

### 5.3 Effect analysis — `may_defect` split from `can_raise`

`FnEffectSummary.may_return_error` (`ast-core.hpp`) is today overloaded, and its consumer has
the wrong polarity: `closed_item_result` (`transpile-mir.cpp`, in the native-scalar call path)
treats a *missing* variant analysis as "trusted clean, skip the error branch". That is one half
of the measured O1 divergence. Split into three:

- **`may_defect`** — the callable may complete with an **escaping compiler-inserted boundary
  defect** despite a plain success signature. Compute it after local declaration containment and
  handlers: a defect fully rescued before the function boundary does not set the bit. The
  call-graph fixed point covers recursion, indirect calls, imports, outer-frame reassignment, and
  unknown callees. **Unknown ⇒ may return a defect.**
- **`can_raise`** — the declared raised-error channel. Unchanged.
- **returned soft-error possibility** — derived from the semantic return type, not a bit.

`may_defect` explicitly excludes `FAULT`. Stack/OOM/resource faults unwind through recovery
frames and require no caller-side result branch; including them would make nearly every allocating
or recursive function defect-capable and destroy TE-17 eligibility without improving correctness.
If a fault summary is later useful for diagnostics, it must be a separate non-ABI property.

For S3 enforcement, retain the exact sets of outer bindings a statically known local callable may
read and may write; booleans are not enough because availability is tracked per binding. The
write set invalidates bindings after the call. If the read set intersects bindings already
invalidated by an earlier call, invoking the callable is itself an illegal later read. This
metadata drives only definite-state diagnostics and never selects a landing pad or changes the
runtime ABI. An indirect callable with erased sets must conservatively check/read and invalidate
the bounded possible sets, or be rejected when either set cannot be bounded soundly.

**`may_defect` is load-bearing for performance, not a peephole.** Under TE-17, an
escaping-defect-capable call result is not lane-eligible until its error outcome is discharged, so
every unanalyzed callee can cost a native lane, transitively.
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

### 5.5 P1F — fault foundation (behaviour-changing, separately landable)

P1F resolves H1 before handler fault capture exists. Lambda must have one process-wide signal
dispatcher and one alternate-stack owner per thread, not two last-installer-wins regimes:

- centralize SIGSEGV/SIGBUS dispatch and per-thread `sigaltstack` lifecycle in the Lambda recovery
  subsystem;
- represent batch/test crash containment as an `LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY` frame
  instead of installing a competing `main.cpp` handler and alternate stack;
- distinguish a recognized stack-guard fault from an arbitrary access violation; local language
  frames may claim only the former, while execution containment or the prior handler owns the
  latter;
- save and chain a pre-existing non-Lambda handler when no eligible Lambda frame claims the signal;
- make installation idempotent and restoration owner-checked, so initialization order cannot
  change which frame receives a stack fault;
- add standalone, batch, nested-local-handler, no-local-handler, and prior-handler-chaining tests
  before P3 relies on the result.

Do not combine P1F with P1: signal ownership is observable crash/fault behavior and deserves its
own bisect point. H2/H3/H4 remain the portability/performance audit in §8.3.

---

## 6. P2/P3 — The handler

### 6.1 Surface — expression and statement contexts

- **Add the braced handler in two context-selected forms.** `primary ^ { … }` is a value-producing
  expression. `pn_call() ^ { …; }` in statement position protects a procedural call, executes the
  statement body on error, and then continues if that body completes normally. Per
  **S7.6.2v2/S7.6.3v2**, handler and propagation are left-associative postfix-primary forms at the
  same logical tier as member (`.`) and query (`?`) access. The prefix spelling `^ { … } expr`
  does not exist. The handler/propagation construct owns its mandatory caret; `call_expr` has no
  optional `propagate` field. A wider operand must be parenthesized. The handled result is itself
  primary-like, so subsequent member, index, query, call, propagation, and handler operations
  continue through the ordinary postfix chain. The statement form may retain a narrow contextual
  production, but it must share the same surface grammar. Expression-versus-statement AST
  classification remains context-selected and must be tested (§9).
  - `f()^ - 1` must still parse as propagate-then-subtract, which **parses today and evaluates
    to 41** (verified 2026-08-01). The bare-expression alternative would silently reinterpret it
    as rescue-with-`-1` and yield 42.
  - Handler position must parse `{ … }` as a **block, not a map literal**; fn bodies already
    establish that precedent, so reuse the same rule.
  - Context, not a trailing semicolon, selects the form. The statement production is valid only
    under `_statement` and its operand must resolve to a `pn` call. A discarded handled `fn` call
    remains an expression-form expression statement; a `pn` call cannot be smuggled into an
    expression by writing a value-looking handler body.
  - Keep the type-level `^` untouched.
  - Add a `current_error_expr` grammar symbol for bare `^`. It is semantically valid only while
    building an active handler body; `^.` and `^[...]` reuse ordinary member/index productions.
    Nested handling is written `operand ^ { ... }`; a leading `^ { ... }` is not a handler.
    `~` keeps the existing `current_expr` token and is not rebound by the handler.
- **No contextual boundary fusion.** In `let v: T = e ^ { h }`, lower the complete handler
  expression first and then run the ordinary declaration boundary. A failure of that outer
  `T` check skips exactly like `let v: T = e`; it is not delivered back into `h`. The same rule
  applies to reassignment. The handler is value-level error-match shorthand, not a hidden cast.
- **Binding rule.** The handler consumes one primary expression on its left and shares the
  left-associative postfix tier with member/query access. Binary and pipe operators therefore
  bind outside an unparenthesized handler; parentheses explicitly widen the protected operand.
  P0 fixtures are normative for the grammar implementation:

  | Source | Required parse |
  |---|---|
  | `a + b ^ { h }` | `a + (b ^ { h })` |
  | `(a + b) ^ { h }` | handle the complete addition |
  | `a |> f() ^ { h }` | `a |> (f() ^ { h })` |
  | `(a |> f()) ^ { h }` | handle the complete pipe expression |
  | `f(a ^ { h }, b)` | handle first argument only |
  | `e ^ { h }.field` | `(e ^ { h }).field` |
  | `e.field ^ { h }` | `(e.field) ^ { h }` |
  | `e ^ { h1 } ^ { h2 }` | `(e ^ { h1 }) ^ { h2 }` |
  | `f()^ - 1` | `(f()^) - 1` |
  | `pn_call() ^ { recover(); }` | statement handler when used as `_statement` |
- **Grammar shape (landed 2026-08-17).** The call/literal/binary/member and prefix-handler
  productions are replaced by one `handler_expr` over `primary_expr`; its result is admitted to
  the postfix/primary chain; `call_expr` no longer owns an optional caret; and propagation uses
  the same operand/precedence model. The generated parser and `ts-enum.h` were regenerated from
  `grammar.js`; `parser.c` remains generated output.

### 6.2 System-fault capture — the remaining gap after syntax retirement

The retired destructure had been carrying a separate native-fault path. That path is removed with
the legacy AST/runtime lowering; the remaining work is to decide whether and how a braced handler
installs a `LambdaRecoveryFrame` with a native `setjmp` landing point. Ordinary contagion catches
returned `ItemError` values but **not** native system/resource faults.

**Context rule.** Native `FAULT` capture is procedural, matching the formal system channel: a
handler in `pn` context (including the statement form) installs a recovery frame; a handler in
pure `fn` context catches soft/raised error outcomes but resource faults remain transparent to the
nearest procedural or execution boundary. This is a visible `fn`/`pn` distinction, not the
operand-shape-dependent capability split rejected in §8.2. The recovery frame protects only the
operand. Retire it before executing the handler body, so a fault raised by the handler propagates
to the next outer frame rather than recursively re-entering itself.

**Fault classification rule.** A language handler may claim only faults the runtime has already
classified as Lambda resource/system outcomes: a stack-guard overflow, an explicit allocation
failure, or a comparison/depth limit. An arbitrary SIGSEGV/SIGBUS, failed internal invariant, or
unknown native crash is not an `error` value and must continue to the prior handler or execution
boundary. Fault delivery must use a pre-reserved/static payload and perform no allocation; in
particular, OOM recovery cannot allocate its own rich diagnostic.

Before the fault-capture phase can land, P3 must specify and implement:

- how a procedural `e ^ { … }` installs or reuses `LambdaRecoveryFrame`, and how pure `fn`
  lowering proves that it did not install one;
- reject a handler whose protected operand may contain `await` (§8.2); no jump buffer may cross
  a task poll;
- how the frame is retired on each exit: normal completion, handler entry, `return`, `raise`,
  `continue`.

### 6.3 AST and typing

- **Handler node + `^` binding.** Model the handler error as a distinct current-error AST node,
  separate from `AST_NODE_CURRENT_ITEM`/`AST_NODE_CURRENT_INDEX`. Form the handled-error type from
  every statically possible operand outcome: error constituents of the value type, the declared
  raised channel, and generic `error` when `may_defect` is set.
  In procedural fault-capable context, union generic `error` for the unchecked system channel as
  well. Normalization may therefore collapse a more precise union to `error`; do not promise
  subtype precision the runtime cannot preserve. Lower `^` through the existing nested handler
  error register/context stack; the innermost active handler wins. Do **not** route ordinary `~`
  nodes through that register: `~` remains the current item/index or object model according to
  the enclosing pipe, match, constraint, or view context. A plain handler introduces no new `~`
  binding.
- **Expression-form typing**, mirroring `or`: `type(e ^ h) = (type(e) \ error) | type(h)`.
  Unannotated bindings infer that union. A surrounding declared `T` applies the ordinary static
  proven/rejected/deferred rules to the complete union; there is no handler-specific requirement
  that `h : T`, and a deferred mismatch is checked only after the handler expression completes.
  Reuse the existing `or`-narrowing implementation (`lambda_type_remove_exclusions` /
  `lambda_type_union_normalized`) rather than writing a parallel one.
- **Statement-form typing.** The protected `pn` call's result is discarded. The handler body may
  complete normally and has no result-type obligation; control then continues after the handled
  statement. `raise`/`return` inside either form still act on the enclosing frame. Letting an
  enclosing block skip is a runtime possibility, not static divergence.

### 6.4 Corpus migration (P3, landed 2026-08-17)

All active `.ls` declarations and prefix error tests were migrated. Value/error capture now uses
a braced handler that returns the current error as data when inspection is required; value-only
recovery uses a braced handler or `or`. Boolean error checks use `is error`. No active corpus file
contains `let a^err = e`, `pub a^err = e`, or prefix `^expr` syntax. Historical design notes retain
the old spellings only when documenting the retired design.

**Grammar and corpus retirement landed (2026-08-17).** The implementation now follows
**S7.6.2v2/S7.6.3v2**: `handler_expr` and `propagate_expr` are mandatory-caret,
left-associative postfix-primary forms at the member/query tier; `call_expr` no
longer owns propagation; and the obsolete call/literal/binary/member plus every
`handler_prefix_*` production is removed. The legacy `^err` destructure and
prefix `^` error test are now removed as well; active code uses braced handlers,
postfix propagation, or `is error`.

---

## 7. P4/P5 — Retirement (landed) and routing

### 7.1 P4 — the atomic retirement (landed 2026-08-17)

- The prefix `^` operator is removed from the unary-operator choice in `grammar.js`; `x is error`
  is the replacement.
- The braced handler and propagation grammar conforms to **S7.6.2v2/S7.6.3v2**: `call_expr`
  has no optional caret, `handler_expr` is a mandatory-caret postfix operation over
  `primary_expr`, its result continues through the ordinary postfix chain, and propagation uses
  the same operand and precedence model.
- `handler_member_expr`, `handler_literal_expr`, `handler_binary_expr`, and every
  `handler_prefix_*` production are removed, along with their conflicts, generated symbols, AST
  dispatch cases, and prefix-handler fixtures. The nested-handler fixture uses
  `operand ^ { ... }`.
- The destructure production, `FIELD_ERROR` AST handling, synthetic error binding,
  `AstNamedNode.error_name`, and the dedicated `pub` path are removed. **`pub x^err = …` no
  longer exists.**
- The E228 diagnostic now offers `%s(...) ^ { … }` and `or` recovery; it no longer advertises
  the retired capture spelling.

### 7.2 P5 — containment

- **Boundary disposition table (TE-18, I4).** All entries reuse the failure condition already
  computed by `emit_checked_boundary`; only their terminal differs:

  | Boundary | Failed-check disposition |
  |---|---|
  | local `let` / `var` establishment | `DEFECT_SKIP` to that binding's declaring block; binding is never established |
  | same-frame reassignment | `DEFECT_SKIP` to the binding's declaring block; old value retained and case-7 diagnostics emitted |
  | `for x: T` iteration variable | `DEFECT_SKIP` to the current iteration-body result; later iterations continue |
  | declared parameter | callee is not entered; the call expression becomes `SOFT_VALUE` in the caller |
  | declared return | current callable completes with its implicit defect outcome; caller observes `SOFT_VALUE` |
  | fixed native element/field store | S1 `DEFECT_SKIP` to the container root binding's declaring block; failed store is atomic and reported |
  | future `cast … as T` | use an inline declared-region rule specified with that feature; do not infer one from expression syntax |

  The local cases retarget existing branches rather than adding checks. **Expression interiors
  are not skip sites**: a fallible operand makes the expression `T | error`, which by I3 is not
  lane-eligible and is computed boxed, where existing contagion gives the right answer.
- **Soft-value origins are not routed.** Fallible converters/sys-funcs and calls to open callees
  return boxed errors into ordinary expression lowering. A `T^E` call uses its raised channel.
  Fixed native stores follow §5.2 instead: explicit fallibility is rejected, while a dynamic
  checked-store failure originates S1 `DEFECT_SKIP`. (C16 deletes the flex-int promote class.)
- **Fix V1 before relying on I4.** `fn_array_set` silently calls
  `convert_specialized_to_generic` on a mismatched element store (`lambda-eval.cpp`), changing a
  declared `int[]`'s representation underneath a live binding. Today's mitigation is a runtime
  elem-type guard on every inline read path (see `mir_store_may_change_elem_type`'s comment in
  `transpile-mir.cpp`), which means guarded bodies are **not** unconditionally native today.
  Route stores through the existing checked-root path (`lambda_array_set_checked*` or its shared
  successor) whenever the `MirValue`/binding carries a declared container contract; do **not**
  globally change `fn_array_set`, because inference-only arrays may still widen. Delete a per-read
  element guard only when the incoming `MirValue` carries an unbroken declared-lane witness and
  every alias/call/store path preserves it. Unknown, inference-only, escaped, or invalidated
  arrays retain the guard. This proof-scoped deletion makes I4 true without trusting a raw
  `ArrayNum` tag as a semantic contract.
- **V2: add the missing declared loop-variable syntax in P2.** `loop_expr` currently annotates
  only the key of `for k, v in e` (`index_type`, an identifier rather than a type expression).
  Extend the value variable to `for x: T in e` using `_value_type_expr`, add key/value ambiguity
  fixtures, and lower its per-iteration guard to the iteration-body defect destination.
- **V3 / TE-18 case 7: reassignment is its own ruling, with a diagnostic obligation.** `x = e`
  on a declared binding guards and, on failure, skips to the end of `x`'s declaring block while
  retaining the old value (§10.8) — without this the entry guard does not dominate the scope. But
  unlike a declaration, the skip here leaves an existing binding holding a stale value *and*
  abandons code the user wrote after it, so it must be reported rather than merely contained.
  Three tiers:
  - **compile error** where the RHS is provably `T | error` — the existing binding rule
    (`let x: int = a()`) applied to reassignment; no new rule, just confirm it fires here;
  - **compile warning** where the RHS is only deferred-fallible **and the block has statements
    after the reassignment** — gate on there being a tail to abandon, so a reassignment in final
    position warns about nothing;
  - **runtime report above breadcrumb severity**, naming both consequences: which binding kept
    its previous value and which declaring region was abandoned. A syntactic tail count may be
    included when exact, but loops/branches must not claim a dynamic statement count they cannot
    know.

  `x = e ^ { … }` does **not** automatically suppress these diagnostics. The handler first
  produces the RHS value and the ordinary `x` boundary runs afterward. If handling makes the RHS
  provably clean, no assignment warning remains; a deferred mismatch at the assignment boundary
  still reports and skips normally.
- **V6: the `for` skip target is the iteration body, not the loop.** Per-item skip keeps the
  batch running; the loop's *result* is `(T | error)[]`, Item-lane per TE-17. The declared
  iteration binding is native at every use; unrelated body expressions may still be fallible.
- **Value consumers are not defect destinations.** `or`-left operands, `^ { }`, postfix `^`,
  `match`/`is` scrutinees, error-admitting contracts, and unannotated bindings consume or retain
  `SOFT_VALUE`; they are not entries on `DefectDestination`. A local boundary defect lands only
  at its declaring block. Reaching the function boundary materializes the error as the function's
  implicit defect outcome, as §4 specifies.
- **Sequence points — TE-18 retires rev 1's ruling.** Because expression interiors no longer
  skip, effects to the right of an error-valued operand **run normally**. Rev 1 made "strict
  left-to-right evaluation order becomes normative" a corollary; that was forced by interior
  skip and no longer follows. Evaluation order returns to an ordinary design choice.
- **Routing vs contagion — largely dissolved.** Expressions use contagion in both `fn` and `pn`,
  so there are no longer two lowerings that must be proven to agree on which effects ran.
  Routing exists only at local checked boundaries, where the region is the same in both. §9
  invariant 1 narrows to those boundaries accordingly.
- **Cross-function.** A defect reaching the outermost region becomes an error return. Boxed
  callees carry it in the result Item; native callees use the existing context error lane
  (`FN_ERROR_LANE_CONTEXT_ITEM`). One load-and-branch after the call: the Swift-`throws` shape.
  That branch materializes a caller-side `SOFT_VALUE`; it never jumps directly to a caller
  declaration pad. A later caller boundary check may originate a new local skip.
  **Correct rev 1's "happy-path cost is zero"**: intra-function retargeting is free, but an
  effectful cross-function native call adds exactly this branch.
- **Cross-frame reassignment exception.** If an inner callable fails while assigning a declared
  `var` captured from an outer frame, it returns the error at its own boundary. The call site does
  not route to the captured binding's declaring block. Regardless of success or failure, a call
  that may write captured `x` makes `x` unavailable for subsequent caller reads until an explicit
  definite assignment re-establishes it; §8.1 S3 owns this compile-time rule.
- **Effect bit.** A callee with `may_defect == false` needs no caller-side branch (§5.3).

### 7.3 P6 — fast paths (optional, after correctness)

- **Rescue elision.** In `let a: T = e ^ { 0 }`, an error edge originating while evaluating `e`
  may branch straight to the handler and avoid materializing an error object (keep the
  origination log line as the breadcrumb). This never includes failure of the surrounding `T`
  boundary, which runs after the handler expression. The optimization is valid **only when `^`
  is unused in the handler** and the operand origin supports a status-only path. After the outer
  boundary succeeds, `a` enters its native lane.
- Whether `or`-left rescue should also elide materialization (same conditions).
- **Lane-loss explanation.** Add the opt-in performance note deferred from §5.2 when an inferred
  container is boxed solely because an element/callee is not proven error-free. It must name the
  first fallibility cause and suggest local discharge; it is not emitted in P1 or by default.

---

## 8. Closed decisions and non-blocking residue

### 8.1 Statement-position defects in `pn` — CLOSED 2026-08-06 by TE-18 case 8

The earlier “nearest observed block” proposal is withdrawn. It mixed value observation with
declaration scope and required a liveness policy to choose control destinations. **TE-18 case 8
resolves the issue syntactically:** the skip destination is the end of the block that **declares** the
binding whose establishment or assignment failed — not the innermost enclosing block. Control
structures (`while`, `if`, plain blocks, `for_stam`) create no regions; only declarations do.

`acc = acc + f(x)` inside a loop, with `var acc` declared in the fn body, therefore skips to the
end of the *fn body* — exiting the loop, returning the defect outcome, taking the stale binding
out of scope, and issuing one runtime report instead of n. A `let` declared *inside* the iteration
body skips to the end of that body and the loop continues, so the batch idiom and V6 are
untouched. In a discarded `for_stam`, that per-item body result may itself be discarded; the rich
origination breadcrumb remains. No destination record carries an observation/liveness bit.

**Sub-cases S1–S3, decided 2026-08-06** (TE-18):

- **S1, element/field stores.** No binding is assigned, and a partially-mutated container is not
  scoped away by block exit — it may be aliased or passed in, so scope exit cannot restore the
  pre-operation state. **Report** using case 7's three tiers and skip to the declaring block of
  the container root binding. The failed checked store is atomic and leaves its target slot
  unchanged; “partial state” means earlier successful mutations in the enclosing operation remain
  committed. TE-17 narrows the runtime case to `any`-sourced and unproven-callee stores.
- **S2, module-level `var`.** The declaring block is the module body, so a failed reassignment
  aborts module initialization. Deliberate: a module with half-established top-level state must
  not become importable.
- **S3, cross-frame reassignment** from a closure to an outer-frame `var`. Failure stops at the
  mutating callable's boundary and becomes its error result. The caller does **not** re-apply the
  outer binding's declaring-block skip. The runtime binding therefore still contains a valid value
  of its declared type, but the call statically invalidates later caller reads of every captured
  outer binding it may write.

  ```lambda
  mutate()
  use(x)       // compile error: x may have changed invisibly inside mutate
  ```

  Legal alternatives are `mutate()` with no later caller read of `x`, or
  `x = mutate(); use(x)`, where the returned outcome explicitly re-establishes `x`. A later
  assignment clears the invalid state only if its RHS does not read the invalidated binding;
  `x = x` remains an error. Reads include direct values, arguments, member/index bases, later
  closure capture, and invoking an existing closure whose captured-read set contains `x`.

  Implement this as definite-state analysis over the caller CFG. At a merge, `x` remains invalid
  if any reachable predecessor contains the hidden write without re-establishment; loop backedges
  carry the state into later iterations. The rule affects compilation only, never routing. For a
  known callee use its exact captured-read/write sets (§5.3): reads of already-invalid bindings
  are rejected before the call, and possible writes invalidate after it returns. For an indirect
  call, conservatively apply the bounded possible sets or reject it when either set was erased.
  Runtime reports/breadcrumbs remain unchanged. The inner callable is still defect-capable, so
  **`may_defect` treats outer-frame reassignment as an effect** (§5.3).

### 8.2 Handler-protected `await` — DECIDED 2026-08-06: reject

`e ^ { … }` over a possibly-suspending operand is a **compile error**, reusing the existing
`MayAwaitScan`. A recovery frame's jump buffer records a context inside the current JIT
activation, and `await` unwinds to the scheduler and resumes on a different frame — so the buffer
would point at dead stack. The retired destructure used `local_fault_safe` to degrade quietly to
value-error-only handling; the handler path must diagnose the suspension instead.

Rejected: silently splitting the capability by operand shape. Follow-on (not now): segment the
protected region at each `await` and arm one frame per segment, so no buffer crosses a poll —
backward-compatible with code written under the rejection.

### 8.3 `setjmp` inventory and hazards (surveyed 2026-08-06)

The handler inherits `LambdaRecoveryFrame`, so the plan depends on that machinery being sound.
Lambda's own `setjmp` users, excluding vendored code and tests:

| Site | Purpose | Storage |
|---|---|---|
| `recovery_frame.h` + `lambda-stack.cpp` + `transpile-mir.cpp` | the language-level fault frame | **TLS** (`__thread lambda_recovery_frame_tls_top`) |
| `sys_func_registry.c` | exports `sigsetjmp`/`setjmp` as a JIT-callable symbol | — |
| `main.cpp` | batch harness: timeout, MIR error, crash | 3 × process-global |
| `radiant/script_runner.cpp` | JS execution guard | 1 × process-global |
| `radiant/cmd_layout.cpp` | layout crash guard | 1 × process-global |
| `lib/image.c`, `radiant/render_{output,img,svg}.cpp` | libpng/libjpeg error paths | vendor-mandated, unrelated |

- **H1 (owned by P1F). Two SIGSEGV/SIGBUS regimes with no chaining.**
  `install_signal_handler` in `lambda-stack.cpp` calls `sigaction(SIGSEGV/SIGBUS, …, NULL)` —
  third argument NULL, so it neither saves nor forwards to a prior handler. Batch mode in
  `main.cpp` installs its own crash handler for the same signals and *does* save and restore
  them. Last installer wins, and nothing chains, so one regime silently disables the other.
  Whether a stack overflow in JIT code lands in the armed `LambdaRecoveryFrame` (language-level
  recovery) or in `batch_crash_jmp` (harness) depends on install order. **Consequence for this
  plan: `^ { }` fault capture may behave differently under `make test` batch mode than
  standalone.** Both also install their own `sigaltstack`, the second replacing the first. P1F's
  single-owner design in §5.5 must land before §9 fault-capture tests.
- **H2 (latent). Process-global jump buffers vs the TLS design.** The recovery frame is correctly
  `__thread`; `js_exec_jmpbuf`, `layout_crash_jmpbuf`, and `main.cpp`'s three are plain `static`.
  Single-threaded today, but under the concurrency work (RC1–RC8 isolates, JT1–JT7 JS threading)
  two threads in guarded execution would clobber each other and longjmp into a dead frame.
  Cheapest fix is `__thread`.
- **H3 (fragility, test it).** Calling `setjmp` through an imported symbol is formally UB
  (C11 7.13.1.1 restricts where it may appear). It works because `sigsetjmp` is a genuine
  out-of-line function whose saved context is the *caller's* — the MIR activation, which is
  exactly what is wanted, and the registry comment says so deliberately. Two fragilities: it
  breaks wherever `setjmp` is a builtin or macro (the `__intrinsic_setjmpex` shim in `main.cpp`
  suggests this already bit on MSVC), and **MIR must treat the call as returning twice** — any
  local live across the checkpoint must be memory-backed or the second return reads a stale
  register. Add an emission test for the second property; it is silent when wrong.
- **H4 (perf, measure before P3).** `sigsetjmp(env, 1)` saves the signal mask, i.e. a
  `sigprocmask` syscall per armed frame. Acceptable at today's braced-handler frequency; if `^ { }`
  becomes the primary error-handling form and arms frames routinely, this lands on a warm path.
  Either audit whether `savemask=0` is safe inside protected regions, or arm lazily.

### 8.4 Non-blocking residue

- Whether TE-17's `T | error` demotion is transitive through containers: does discharging
  re-narrow `(int | error)[]` to `int[]` in place, or only by copy? Copy is the safe default;
  in-place needs the COW exclusivity rules.
- Lazy/streaming `for` bodies: with a typed-lane destination and a streaming source,
  representation cannot be chosen before consumption. Boxed-until-proven is consistent with
  TE-17 and is the presumed answer (KIV).
- Editorial wording and examples for E228's already-fixed acknowledgment set (§4); this must not
  reopen which forms acknowledge a raised call.

---

## 9. Gates and invariants

**Per-phase gates.**

- P0: the formal-spec wording and every required parse in §6.1 are reviewed together; no
  implementation phase starts against contradictory normative text.
- `make test-lambda-baseline` 100% every phase (`make test-radiant-baseline` unaffected).
- P1F additionally runs standalone and batch signal/recovery tests in both initialization orders,
  with a pre-installed handler and nested local/execution frames. A recognized stack-guard fault
  must reach the eligible procedural frame; an arbitrary access violation, tested in an isolated
  subprocess, must not.
- `make test262-baseline` additionally after any change to the shared MIR call ABI or
  `Context.mir_return_lane` — expected mainly in P5, and in P1 only if ABI plumbing lands there.
- MIR budgets (`test/mir/mir_budgets.json`, MT7 0% slack) re-baselined per phase with
  justification. Local declaration routing should be roughly budget-neutral because it retargets
  existing branches. Cross-function native calls may add the one `may_defect` branch; growth
  elsewhere means checks are being duplicated rather than retargeted.
- Forced-GC sweep after P5: landing pads introduce new join points where a boxed error is live;
  use the harness described by `Lambda_Design_MIR_Emission_Test.md`.
- Re-run the typed benchmark columns after P5 and P6. P1's `may_defect` analysis is the
  performance-critical piece under TE-17 — regressions there show up as lost native lanes, not
  as extra branches.

**Invariants to assert as tests.**

1. **Boxing/routing invisibility.** The same program compiled boxed and unboxed must agree on
   result and effects. Checked-boundary defects may use a local branch or boxed join internally,
   but a cross-function defect always materializes as the same call-result value before
   caller-local handling. This is the DF9-shaped property and the single most valuable test.
2. **No binding holds a placeholder for a failure** (spec §13 invariant 7, as amended): every
   binding either has a real value or was never established. After a failed cross-frame
   reassignment, the captured binding's previous valid value counts as the former, but S3 makes
   it statically unavailable for later caller reads until definite re-establishment.
3. **Native lanes never hold an error** (I1) and **no operator receives one** (I2) — asserted at
   emission through the `MirValue` contract, not only by testing outputs.
4. **Mechanisms stay separate** — a `RAISED_ERROR` never reaches a `DefectDestination`;
   `SOFT_VALUE` never queries one; `FAULT` reaches only recovery frames.
5. **Forcing every function to `may_defect = true` must not change observable results.** Note
   the polarity: rev 1 said "erasing the effect bit must not change results", which is backwards
   — *clearing* the bit removes necessary checks and is unsafe. Only the conservative direction
   is a valid differential.

**Test additions beyond the per-zone tests.**

- A handler grammar/precedence matrix: calls, operators, pipes, member access, indexing,
  parentheses, nested handlers, handler-local `^`/`^.`/`^[...]`, `~` current-value behavior,
  `or`, `f()^ - 1`, and expression-versus-statement classification.
- Expression boundary order: `let v: T = e ^ { h }` evaluates the whole handler first, then checks
  `T`; its mismatch is not caught by `h`. Statement form runs `H` only on `pn_call()` failure and
  continues after `H` completes.
- The exact handler binding table from §6.1, asserted as AST shapes rather than only parse success.
- Negative context parses/builds: a statement handler whose operand is not a `pn` call, a `pn`
  call forced into expression form, and a handler-protected operand that may `await`.
- `^` typing across a returned union error, declared `^E`, implicit `may_defect`, and procedural
  system fault, including normalization to generic `error` when precision cannot be preserved;
  `~` remains independent and follows its enclosing current-value context.
- Typed-container rejection versus inferred/error-admitting acceptance (TE-17): explicit
  `T | error` store rejection, inferred Item-lane widening, dynamic checked-store success/failure,
  failed-store atomicity, and earlier-mutation partial state.
- V1 proof split: declared-root stores never despecialize and proof-carrying reads lose their
  guard; inference-only, escaped, unknown-call, and invalidated arrays retain widening/guards.
- System/resource faults are captured by procedural handlers and remain transparent through pure
  `fn` handlers; a fault in the handler body reaches the next outer frame; OOM delivery allocates
  nothing; arbitrary native crashes are not converted to language errors; async/`await` is rejected.
- Task-scope cleanup and forced-GC tests proving handler error payloads survive restoration
  (§5.1's ordering contract).
- A differential mode compiling once with normal effect analysis and once with every callee
  conservatively `may_defect`, comparing values and observable effects (invariant 5); separately
  prove that forcing resource-fault capability does not add caller-side result branches.
- Function-boundary transition: a callee-local `DEFECT_SKIP` becomes a caller `SOFT_VALUE`; it
  cannot jump directly to a caller declaration landing pad, while a later caller check may
  originate a new local skip.
- Per zone: binding failure collapses the block to the error; a `for` body yields per-item errors
  and keeps iterating (the batch theorem, on an Item-lane container); statements after a failed
  declaration boundary are not evaluated, while expression interiors retain ordinary contagion;
  handler exit via `raise`/`return`; innermost handler-error `^` and independent `~` shadowing
  inside a `match`/pipe arm.
- Cross-frame reassignment: `mutate(); use(x)` is a compile error;
  `mutate()` with no later `x` read and `x = mutate(); use(x)` are legal; merges, loop backedges,
  RHS self-reads, argument/member/index reads, existing-closure reads, and bounded indirect-call
  read/write sets preserve the same definite-state rule without changing runtime routing.
- Negative: each retired form produces a clear diagnostic; the rewritten E228 text is asserted
  verbatim; `let x = raising_call()` still fails; **skip does not satisfy E228**.
