# Lambda — Type Enforcement Implementation Plan, Round 2

**Status:** IN PROGRESS — 2026-07-31. Phases 0–4 are implemented and verified.
Phase 5 ER-S0 through ER-S6 are implemented and verified on POSIX: fault records,
nested frames, static resource origins, transaction/task boundaries, and production
crash-containment removal now use the frame ABI. ER-S7's precise-root/POSIX gate is
complete; direct Windows SEH integration verification remains outstanding on a Windows
runner.
Phase 3 (`any \ error`, honest inference, and fn firewalls) now includes the package-source
migrations and final MIR carrier fixes needed to honor inferred contracts without changing
the public procedural Item ABI. The former 232 package-script failures are resolved: all 643
Lambda GTests now pass. The standing Lambda baseline is 3,685/3,686; its sole failure is the
documented, unrelated `dom_module_props` DOM expectation. Phase 4 is complete: its E228
acknowledgment, literal bracket-map-write, `input`-schema, dynamic-ABI, and diagnostic
hardening slices are implemented and verified.
Round 1 landed in commit `274625d56` (`type enforcement impl`). This plan covers the
remaining correctness work without assuming that an AST-only distinction, one boxed
entry, or a carrier-only validator is sufficient.

**Design authority:** `vibe/Lambda_Design_Type_Enforcement.md` (TE-1–TE-14 and that
document's §10 diagnostic decisions), with the normative summary in
`doc/Lambda_Formal_Semantics.md` §11.4.
Where those documents still contradict one another, Phase 0 resolves the text before
code relies on it.

**Primary deliverables:**

1. value-aware exact numeric admission at DEFERRED boundaries;
2. enforced implicit `any \ error` contracts at fn/pn parameter boundaries and fn
   return firewalls;
3. honest error-possibility inference, including the minimum sys-function metadata it
   requires;
4. the remaining acknowledgment, diagnostic, and boundary-completeness work.

**Required phase order:**

```text
Phase 0 (semantic metadata and doc prerequisites)
    ├── Phase 1 (value-aware boundary admission)
    └── Phase 2 (C14c numeric-domain migration)
             └── Phase 3 (any \ error + inference + firewalls)
                     └── Phase 4 (remaining enforcement gaps)

Phase 5 (system-fault channel) follows its separate recovery/ABI design and ER-S0–ER-S7 order.
```

Phase 1 may proceed while Phase 2 is being prepared. Phase 3's metadata substrate may
be built earlier, but its openness/firewall behavior must not be enabled until Phase 2
has removed the runtime division-error paths that C14c declares nonexistent.

**Standing gates for every implementation phase:**

- `make build` and the relevant focused tests pass;
- `make test-lambda-baseline` passes at 100%;
- `make test262-baseline` passes with 0 failures, 0 retries, and 0 regressions;
- every new Lambda test `*.ls` has its `*.txt` golden, and negative goldens pin the
  diagnostic code/message rather than asserting only empty stdout;
- performance measurements use `make release`, never a debug binary;
- before claiming “within noise,” record the exact benchmark command, comparison
  baseline, run count, and allowed threshold in this document;
- the frozen C2MIR backend (`lambda/runtime/transpile.cpp`) receives no enforcement,
  runtime-ABI, or design work.

---

## 1. Round-1 baseline

Round 1 killed the original silent-corruption catalog. This is the substrate, not a
claim that enforcement correctness is complete:

| Boundary | Static | Dynamic | Current anchor |
|---|---|---|---|
| Declaration init (`let/var x: T = e`) | E201, including plain-`T` null rejection | checked declaration boundary | `build_ast.cpp:1188,5365`; `transpile-mir.cpp:6607-6626` |
| Named-map/union literal fields | per-field E201 + missing required field | validator path detail | `build_ast.cpp:1146`; `lambda-eval.cpp:1455-1460` |
| Reassignment | E201 | checked before commit | `build_ast.cpp:8257`; `transpile-mir.cpp:11612` |
| Call arguments | E207 for known mismatch | caller checks for declared params + boxed declared-param prologue | `build_ast.cpp:2937`; `transpile-mir.cpp:10613,10677,14492-14527` |
| Arity | E206 | dynamic count check | `build_ast.cpp:2884`; `lambda-eval.cpp:716` |
| Returns | E208 per return/tail | checked declared return | `build_ast.cpp:8456-8503`; `transpile-mir.cpp:11494,15424` |
| Typed map writes | member-form static check | transactional COW candidate | `build_ast.cpp:8204`; `lambda-eval.cpp:7021-7105` |
| Typed array writes | E201 | transactional candidate | `build_ast.cpp:8163`; `lambda-eval.cpp:7116-7154` |
| `var` inout publish | n/a | pre-state checked, `_inplace` variants | `transpile-mir.cpp:12391,15250` |

Reusable round-1 mechanisms:

- `StaticBoundaryResult` = PROVEN / REJECTED / DEFERRED
  (`build_ast.cpp:1058-1080`);
- explicit binding annotations retained as `declared_type` on
  `AstNamedNode`/`NameEntry`;
- `emit_checked_boundary` → `lambda_type_check` as the scalar boundary choke
  point;
- rich `lambda_type_error` objects with validator paths and stack traces;
- checked map/array writers whose candidate is published only after validation;
- boxed public function wrappers for declared-parameter checking.

Round 2 must preserve those properties while closing the metadata, dispatch, numeric,
and effect-inference gaps below.

---

## 2. Cross-cutting implementation invariants

These are acceptance rules for every phase.

### 2.1 Contract type and effective type stay separate

A source annotation/default contract answers what may cross an interface. The
effective type answers what inference proved for the current expression or body.
Neither field may be overwritten with the other.

This applies to:

- binding `declared_type` versus initializer/effective type;
- parameter contract versus compact/native parameter carrier;
- function return contract versus inferred success/effective return;
- runtime `ShapeEntry` physical type versus an annotated map's semantic root
  contract.

An unannotated clean function body may still infer `int`, `string`, or a precise
shape. Giving it the implicit `any \ error` firewall must not downgrade its call
result to `any`.

### 2.2 No new Item representation is implied

`any \ error` is a semantic type-system construct; no runtime value has
`any \ error` as its actual Item tag. Do not add a new Item `TypeId` merely to encode
the contract. Use one canonical internal top-with-exclusions representation (or an
equivalent semantic qualifier) and keep the existing Item tag/raw-container-pointer
ABI unchanged.

The representation must support at least:

- true top `any`;
- `any \ error`;
- the `or` result requirement `any \ {error, null}`.

A single `TYPE_ANY_SANS_ERROR` special case is not sufficient for the last form.
Whichever representation is selected, audit every `type_id == LMD_TYPE_ANY` shortcut:
pointer/qualifier identity must not be silently collapsed back to true `any`.

### 2.3 One shared operation per semantic question

Do not mirror the validator's numeric lattice in a second boundary helper.
Keep distinct APIs, backed by shared primitives:

1. static subtype/type-domain embedding;
2. runtime membership (`is`/`match`, type-directional);
3. DEFERRED boundary admission, which may exactly re-represent a numeric value;
4. explicit conversion, which owns lossy/value-changing policy.

### 2.4 Dispatch paths are observationally equivalent

Direct, boxed/dynamic, closure, imported, recursive/TCO, optional/default,
variadic, and async-relevant call routes must enforce the same parameter and return
contracts. No semantic rule may live only in the boxed wrapper unless every other
route proves the same fact before bypassing it.

### 2.5 Error in, same error out

An error arriving dynamically at a clean checked boundary is propagated unchanged.
The boundary does not replace the original diagnosis with a second type mismatch.
An error-admitting explicit target receives it as a value.

### 2.6 Checked writes remain transactional

Any conversion required by admission happens on the detached candidate or checked
value. A failed array/map/nested-path write leaves the old root, its shape, and every
COW snapshot unchanged.

---

## 3. Decision gates

These decisions must be recorded before the named implementation phase starts.
The plan does not silently choose language semantics where the design remains
ambiguous.

### DG-1 — Does exact numeric admission recurse through structural contracts?

TE-5/TE-6 say a numeric value at a DEFERRED boundary is admitted by mathematical
value. TE-10 says a named-map boundary validates deeply. Together they appear to
require:

```lambda
type P = {age: int}
let p: P = <dynamic {age: 3.0}>   // admitted as a P whose observed p.age is int 3
```

If that is intended, deep validation must return a converted/canonicalized candidate;
a boolean validator pass cannot leave `age` physically and observably float. The
source parsed map must remain unchanged.

**Recommendation:** adopt recursive, copy-on-conversion admission for named
maps/typed arrays. If the decision is scalar-only instead, narrow TE-5/TE-6 explicitly
before Phase 1 and add a negative nested-field test.

**Execution decision (2026-07-30):** Proceed with the recommended recursive,
copy-on-conversion policy. The Phase-1 structural slice implements §5.4 with detached
whole-map/array candidates, source preservation, and transactional COW publication.

### DG-2 — Does the implicit non-error top admit null?

By set meaning, `any \ error` still includes `null`. That makes the untyped default
clean of errors but not clean of absence.

**Recommendation:** keep that meaning for this round and correct the design rationale
to say `T?`-for-null applies to annotated `T`, not to unannotated parameters. Changing
the implicit default to exclude both null and error is a separate language decision.

### DG-3 — Known error passed to an implicit clean parameter

The dynamic rule is fixed: the function is not entered and the call evaluates to the
same error. The design should state whether a statically known error argument is:

- E207, following TE-2's known-mismatch rule; or
- compiled as an immediate error short-circuit without entering the function.

**Recommendation:** retain E207 for statically known error arguments and reserve
error-in/error-out for DEFERRED crossings. This matches declared clean parameters and
keeps the static/dynamic split predictable.

### DG-4 — fn versus pn return defaults

The implicit non-error parameter contract applies to both fn and pn parameters.
The revised design explicitly specifies an implicit return firewall for unannotated
`fn`; it does not yet clearly redefine an unannotated pn return.

**Recommendation:** implement the return firewall for `fn` only in Phase 3. Do not
silently change pn return/channel semantics; record a separate pn decision if desired.

### DG-5 — Decimal infinity/NaN spelling and round trip

**Resolved (2026-07-30):** decimal poison is a decimal value with canonical Lambda
source spellings `decimal.inf`, `-decimal.inf`, and `decimal.nan`; bare `inf`/`nan`
remain float. Every Lambda printer/formatter emits the decimal spelling, and the grammar
parses it back as a decimal literal. Decimal NaN follows the existing poison rule
(`x == x` is false; `x is nan` is true), while decimal infinity compares and hashes by
numeric value with its float counterpart. A `decimal` boundary accepts an existing decimal
poison but does not implicitly materialize float infinity/NaN into decimal; use the
explicit decimal spelling or decimal-domain arithmetic when that domain is intended.

### DG-6 — Checked-error payload versus fault payload

**Recommendation:** retain rich objects for checked/type/value errors. Use
pre-reserved minimal objects only for system/resource faults, including OOM while
attempting to allocate a rich diagnostic. Do not reintroduce inline code-only checked
errors.

---

## 4. Phase 0 — semantic consistency and metadata substrate

### 4.1 Correct the normative/document ledger first

Before code relies on the revised rules:

1. In `doc/Lambda_Formal_Semantics.md` §7.3, remove the statement that an
   undeclared function may infer and return `T | error`; §11.4's implicit
   `any \ error` firewall is the revised authority.
2. Reconcile numeric §§4.1/4.3 with C14c §4.7:
   - remove the claim that sized×sized `div`/`%` stay in a sized machine lane;
   - remove/rewrite `MinInt div -1 wraps` if `div` leaves the integer lane.
3. In the design's §8.1 ledger, stop saying all P0–P4 correctness work is
   complete while retained P0/P1 text includes unimplemented `or` narrowing and
   value-aware admission.
4. Replace the remaining “correctness scope is closed” sentence with an explicit
   round-1 snapshot.
5. Record DG-1–DG-6 and qualify core `any` versus validation-space `any`.

These are semantic reconciliations, not a request to implement the frozen C2MIR path.

**Progress (2026-07-30):** completed the authoritative-document reconciliation for the
implicit clean firewall, C14c's sized-`div`/`%` exception, and the Round-1 ledger status.
Implemented the initial metadata substrate: pointer-distinct internal `any` exclusion tops,
separate parameter and return contracts, and normalized error/null type-set helpers. The
metadata now survives ordinary/anonymous/forward function construction, system signatures,
module exports, and TypeScript function signatures. Focused helper coverage is green in
`TypeContractMetadataTest.*`. The shared error-acceptance path and static relation now use
the type-set helpers. The system-function registry also records passive success/error facts for
the conversion family (`int`, `int64`, `float`, `decimal`, `binary`) without changing lowering.
The AST-dump regression now exercises implicit and explicit contracts for named, anonymous,
forward, and imported function signatures; the `fn`/`pn` return-default split; inferred
effective returns; and `int`'s success/effect facts. Inferred global returns remain separate
from the established `any` ABI carrier until the later all-call-route firewall work, so this
metadata slice does not retroactively retype already-built forward calls. Phase-0 implementation
is complete; its standing exit is not yet certified because `make test-lambda-baseline` has one
deterministic, unrelated DOM `adoptNode` failure recorded below. `make test262-baseline` is
green on this tree.

### 4.2 Add first-class contract metadata

The live AST distinction is not sufficient because `TypeParam`/`TypeFunc` are what
survive into first-class function signatures.

Implement one coherent model:

1. Add a canonical internal top-with-exclusions type representation. It must be
   distinguishable from explicit `&TYPE_ANY` without adding an Item tag.
2. Add a full semantic contract to `TypeParam` (or an equivalent retained field):
   - unannotated param → implicit non-error-top contract;
   - explicit `any` → true-top contract;
   - explicit `T`/union/occurrence/map → that full contract.
3. Keep compact `TypeParam` carrier fields separate from that contract.
4. Add separate `TypeFunc` return metadata:
   - `return_contract` plus whether it was explicit/implicit;
   - effective/inferred return type used by callers and optimization;
   - existing raised-channel metadata remains separate.
5. Preserve this metadata through forward declarations, imports, closures, function
   values, dynamic signatures, and any type cloning/copying path.
6. Add one diagnostic printer for internal non-error/exclusion tops; no surface
   grammar spelling is required.

### 4.3 Add shared type-set helpers

Add and use shared helpers for:

- `type_accepts_error(T)`;
- `type_accepts_null(T)`;
- normalized union construction;
- removing `{error}` or `{error, null}` from a type;
- detecting a proven error constituent; Phase 3 retains its first expression origin,
  because canonical/global `Type` objects cannot safely own a per-expression source location;
- static boundary relation over internal top exclusions.

Do not construct the broken general `!` pattern/exclusion syntax to implement `or`.
The internal type operation must work even while user-facing general exclusion types
remain out of scope.

### 4.4 Add minimum sys-function effect metadata

The firewall needs semantic effects now even though optimized clean/open sys-function
entries remain perf-stage work.

Add the minimum registry facts:

- precise success return type where known;
- `may_return_error` for value-returning system functions;
- existing `can_raise` remains the separate enforcing channel.

Classify at least the §7.3 conversions used by the acceptance tests (`int`, `float`,
`decimal`, and peers). Do not defer this minimum classification with the later
per-parameter-type/performance retrofit.

### 4.5 Phase-0 tests and exit

Add focused truth tables for:

- true `any`, non-error top, and non-error/non-null top;
- explicit versus implicit parameter contracts after signature construction,
  forward declaration, import, and function-value capture;
- precise inferred return retained alongside an implicit firewall;
- normalized `T | error`, optional types, and `or` subtraction;
- sys-function success/effect classification.

**Exit:** metadata survives every signature path; no runtime behavior changes yet;
both standing baselines pass.

---

## 5. Phase 1 — value-aware numeric boundary admission

### 5.1 Semantics

At a DEFERRED numeric boundary:

- an exactly embedding mathematical value is admitted and represented so the target
  contract is observably true;
- an inexact, non-finite-for-integer, or out-of-range value produces the rich boundary
  error;
- static type positions remain type-directional and reject known cross-type narrowing;
- `is`/`match` remain runtime-membership operations (`3.0 is int` stays false);
- explicit conversion functions continue to own lossy conversions.

### 5.2 Shared scalar admission primitive

Create one shared primitive, conceptually:

```c
bool lambda_numeric_boundary_admit(Item value, Type* target, Item* converted);
```

It returns success separately from the converted Item; do not overload `ItemNull`,
`ItemError`, or another valid Item as a miss sentinel.

The helper belongs with the shared numeric/runtime primitives, not as a copied validator
lattice. Validator and boundary code call the same value-level primitive where their
semantics require admission.

Cover:

- float/f32/f16 → integer targets only when finite, integral, and in range;
- signed/unsigned 64-bit endpoint checks without an out-of-range C cast;
- decimal/integer → integer targets using exact decimal/bigint helpers;
- numeric → sized integer with exact range/sign checks;
- numeric → f16/f32/float only when the target value round-trips exactly under the
  formal numeric convention;
- decimal ↔ float using §4.5's shortest-round-trip meaning;
- negative zero explicitly;
- abstract `number`/`integer` targets without inventing an observable concrete tag.

For `int64`/`uint64` conversion from double, use strict power-of-two bounds rather than
comparing with rounded `INT64_MAX`/`UINT64_MAX` doubles, and never cast until the range
test has succeeded.

### 5.3 Integrate at the boundary, not in membership

`lambda_type_check` performs:

1. incoming error → return the same error;
2. numeric target/source → shared boundary admission when representation or
   value-level admission is required;
3. ordinary runtime membership/structural validation;
4. rich mismatch error.

Do not put value-aware narrowing into `lambda_type_matches`; `fn_is`, `match`, and
post-state membership must remain type-directional.

Audit every `emit_checked_boundary` consumer and the direct checked writers. The
returned converted Item must dominate any native unbox or typed store.

### 5.4 Structural admission, gated by DG-1

If DG-1 accepts recursive admission:

1. extend the validator/boundary walker with a conversion mode that returns a detached
   converted candidate;
2. recursively handle named maps, arrays/occurrences, unions, and nested path writes;
3. preserve open extra fields and their exact runtime shapes;
4. do not mutate the source parsed value;
5. publish a write candidate only after the converted whole root satisfies its
   contract.

Direct map-field and typed-array writers already consume `lambda_type_check`'s returned
item. `lambda_map_path_set_checked` and whole named-map input require explicit work;
their final boolean `lambda_type_matches` check cannot perform conversion.

If DG-1 instead selects scalar-only admission, record those structural cases as
intentional mismatches and do not teach the validator to accept without conversion.

### 5.5 Tests and exit

Required scalar matrix:

- dynamic `3.0 → int 3`, `3.5 → error`;
- integer-valued and fractional decimal → int;
- every sized signed/unsigned lower and upper boundary plus one outside each;
- f16/f32 exact and inexact cases;
- NaN, ±infinity, +0.0, and -0.0;
- `3.0 is int == false` and the equivalent `match` arm is not taken;
- static `let x: int = 3.0` remains E201;
- declaration, argument, return, reassignment, array write, and direct map-field
  write all consume the converted result.

If recursive admission is selected, add dynamic named-map, typed-array, and nested-path
cases, including source-value preservation and COW no-mutation-on-failure.

**Exit:** every selected DEFERRED boundary observes the admitted target value; membership
semantics are unchanged; both standing baselines pass.

### 5.6 Progress — scalar admission slice (2026-07-30)

Implemented the scalar part of §5.2–§5.3:

- `lambda_numeric_boundary_admit()` in `lambda/runtime/type_contract.cpp` performs
  exact admission separately from its converted `Item`, including float/decimal to
  integer, sized signed/unsigned lanes, f16/f32/float round-trip checks, decimal
  materialization, signed zero preservation, and abstract `number`/`integer`
  preservation without inventing an Item tag.
- `lambda_type_check()` now runs that helper before type-directional membership, so
  every existing checked declaration, argument, return, reassignment, typed-array
  write, and direct map-field write consumes the converted value. `lambda_type_matches`,
  and consequently `is`/`match`, remain unchanged.
- `decimal_to_uint64_exact()` rejects negative, fractional, and out-of-range decimals
  before an unsigned native cast. The abstract-integer branch deliberately preserves
  inferred bigint-decimal carriers, which keeps closure captures and `u64` persistence
  intact.
- The Lambda error-test build target now declares its actual `lambda-rt` → `radiant`
  link closure and required macOS frameworks in `build_lambda_config.json`; the
  generated test build no longer depends on a stale prelinked binary.

Coverage added or strengthened:

- `NumericBoundaryAdmissionTest.ExactScalarConversionsPreserveTargetTags` covers
  dynamic float admission/rejection, every signed/unsigned sized 8/16/32 bound and
  one-outside case, f16/f32 exactness, signed zero, NaN, and infinities.
- `proc_type_numeric_boundary_admission.ls` covers dynamic float and decimal admission
  across declaration, argument, return, reassignment, typed-array write, direct map
  write, `i64`/`u64` identity, and the unchanged `is`/`match` behavior.
- The dynamic-declaration negative fixture now pins fractional decimal → `int` E201;
  the static declaration fixture also retains the known `3.0` → `int` rejection.

### 5.7 Completion — recursive structural admission (2026-07-30)

Implemented the remaining DG-1 path in `lambda/runtime/lambda-eval.cpp`:

- `runtime_type_admit_value()` first preserves an already-valid value, then performs
  deterministic union-arm admission only when no arm already matches.
- Named-map and occurrence-array candidates are deeply detached through the existing
  mutable-clone machinery before any nested conversion. Each named field or array
  element is recursively admitted, while open map fields retain their original shape.
- A float-backed numeric array becomes a generic Item array before admitted integers
  are stored; a map field whose physical slot type changes rebuilds only the detached
  candidate shape. These preserve the observable admitted tag rather than letting a
  typed storage fast path widen it back to float.
- `lambda_map_path_set_checked()` now validates and admits the whole detached root
  after its raw nested write, and publishes it only on success. A failed fractional
  nested write therefore leaves both the owner and every COW snapshot untouched.

`proc_type_numeric_structural_admission.ls` covers dynamic named maps, nested maps,
typed arrays, an `int | string` union arm, open extra fields, source-value preservation,
successful nested COW conversion, and no mutation after a rejected fractional nested
write. The auto-discovered harness passed 1/1. Together with the scalar matrix, this
fulfills the Phase-1 behavioral exit: selected DEFERRED boundaries observe target-tagged
values and `is`/`match` remain membership-only operations.

---

## 6. Phase 2 — C14c number-domain migration

This is a semantic prerequisite for Phase 3's claim that arithmetic does not originate
errors. It is not independent of the firewall.

### 6.1 Type and emitter migration

1. Retype `div`/`%` through the same result-domain selection used by `/`:
   - flex/small integer domains → float;
   - large-integer domains → decimal;
   - truncation/remainder semantics stay as decided.
2. Update the shared numeric decision tables first, then MIR emission and result
   boxing/unboxing.
3. Remove stale sized-lane assumptions and statically rejected/converted return paths.

### 6.2 Runtime migration

1. Scalar flex and machine `div`/`%` return the selected number-domain value.
2. Computed zero divisors produce that domain's infinity/NaN, not `ItemError`.
3. Vector integer division/modulo returns the decided float/decimal-compatible result
   with per-lane poison rather than a whole-operation error.
4. Delete old integer-division-by-zero error-origin sites only after all callers and
   tests use the new result domain.

### 6.3 Decimal infinity/NaN, DG-5 resolved

1. Decimal contexts do not trap IEEE-style invalid/divide-by-zero outcomes: decimal
   arithmetic produces decimal poison instead of aborting the process or returning
   `ItemError`.
2. `decimal.inf`, `-decimal.inf`, and `decimal.nan` are parsed and printed as decimal
   values, keeping decimal poison visibly distinct from float poison.
3. Decimal NaN is recognized by `is nan`, remains unequal to every value, and occupies
   the total-order NaN band. Decimal infinity participates in numeric equality and
   canonical hashing with float infinity; formatters and JS string conversion preserve
   the decimal spelling.
4. Exact boundary admission keeps decimal poison domain-explicit: it accepts an existing
   decimal poison but does not implicitly turn a float poison into decimal.

### 6.4 Corpus and compatibility sweep

Search all Lambda sources/tests for:

- `div`/`%` flowing into int/sized-int annotations, params, returns, and array lanes;
- `div ... or default` patterns whose meaning changes because infinity/NaN are truthy;
- vector code assuming integer result arrays;
- equality/order/printing fixtures for numeric poison.

Fix by explicit conversion, retyping, or a divisor guard; do not add compatibility
workarounds that preserve the retired error result.

### 6.5 Tests and exit

Pin at least:

- `1 div 0 → inf`, `0 div 0 → nan`, and `%` equivalents;
- nonzero truncation and remainder-sign rules;
- `int div int → float` and large-integer domain → decimal;
- `1n / 0n` and large-integer `div 0` decimal poison;
- decimal poison parse/print round-trip, arithmetic propagation, equality/order, and
  infinity hashing;
- vector per-lane zero behavior;
- literal-zero compile diagnostics remain as specified;
- `div ... or 0` does not consume infinity/NaN.

**Exit:** no runtime `div`/`%` path originates an error; formal §§4.1/4.3/4.7 agree;
both standing baselines and the recorded release numeric benchmark pass.

---

## 7. Phase 3 — `any \ error`, honest inference, and fn firewalls

### 7.1 Static/inference behavior

1. Unannotated fn/pn parameters receive the retained implicit non-error-top contract.
   Explicit `any` remains true top and error-transparent.
2. Bare `var b` remains true `any`; verify and pin it.
3. Dynamic reads that currently become true `any` follow TE-5 R1:
   opaque map/member/container reads default to the non-error top.
4. Reads through explicitly-`any` provenance remain true `any` per R5. Add explicit
   provenance metadata rather than trying to reconstruct it from TypeId.
5. User-call expressions retain the callee's full effective return type. Verify the
   existing `TypeFunc::returned` path before adding new logic; do not duplicate union
   wrapping if a `T | error` `Type*` already survives.
6. Sys calls use Phase 0's success/effect metadata to produce `T | error` where they
   may return value errors.
7. Track the first error-originating expression sufficiently to report, for example,
   “call to `g` may return error.”

### 7.2 `or` narrowing

Replace the flat `TYPE_ANY` result for `or` with:

```text
type(a or b) = (type(a) - {error, null}) | type(b)
```

Use Phase 0's normalized internal type-set helpers. Required cases include:

- `(int | error) or int → int`;
- `int? or int → int`;
- `any or T → (any \ {error, null}) | T`, represented without collapsing to true
  `any`;
- `error or T → T`;
- `null or T → T`;
- unions containing both error and null;
- conservative bool handling (`false` remains possible unless separately narrowed).

### 7.3 Parameter short-circuit across every ABI

The boxed public prologue alone is insufficient. Direct local calls invoke the raw
entry, and TCO rewrites arguments directly into parameter registers.

Implement one of these equivalent strategies consistently:

1. every potentially open call route uses a checked boxed entry; or
2. direct/TCO callers perform the cheap error-tag short-circuit before entering the
   raw body, while dynamic/function-value calls use the boxed guard.

Whichever strategy is selected:

- every explicit error-admitting parameter contract (`T^`, `T | error`, `error`, or `any`)
  must receive errors and enter the body;
- implicit non-error params must not enter the body on a dynamic error;
- the call result is the exact original error;
- optional/default resolution happens in an order that cannot hide an error;
- native-return functions need a caller/wrapper path capable of returning the error
  without placing Item bits in a native result lane;
- `var` inout arguments are not detached or published when the call short-circuits;
- recursive/TCO calls obey the same rule.

The body may rely on the contract only after this dispatch audit is complete. Add the
`case error:` dead-arm lint later in Phase 4.

### 7.4 Function return firewall

For every `fn`, retain:

- explicit return contract when written;
- otherwise implicit non-error-top return contract;
- separately, the precise inferred/effective success result for callers.

Check every explicit return and implicit tail against the contract:

- proven clean → pass;
- proven error constituent → E208-family error naming the first cause and offering
  contain / disclose `| error` / impose `^`;
- genuinely dynamic body → retain the runtime return boundary;
- no effect fixpoint: assume each function's declared/implicit contract and check its
  body, including recursion and forward declarations.

`any \ error` is ledger information only. It must never select an error-incapable native
carrier unless the corresponding runtime boundary/dispatch guard proves the current
value.

Per DG-4, do not silently apply this fn return rule to pn returns.

### 7.5 Dispatch and firewall test matrix

For each applicable case, test implicit non-error, explicit `any`, and both explicit
error-admitting spellings (`T^` and `T | error`) where the surface form is available:

- direct named call;
- first-class/dynamic function value;
- closure;
- imported/public function;
- recursive and TCO call;
- optional/default parameter;
- variadic call;
- fn and pn parameter;
- `var` inout parameter;
- precise clean inferred return;
- plain declared return, `T | error`, `T^`, explicit `any`, and implicit return;
- user `T | error` caller, classified sys-function caller, `or` containment,
  `^err`, postfix `^`, and `match case error:`;
- laundered error read from an opaque container reaches the next checked boundary
  safely.

Use a body-side effect/counter in focused tests to prove that the body was not entered,
not merely that the final result happened to be an error.

**Exit:** dispatch choice cannot change error admission; explicit `any` remains the
opt-in; clean fn firewalls diagnose proven openness with its cause; `int(s) or 0`
types clean; both standing baselines pass.

### 7.6 Implementation checkpoint — call results, `or`, fn returns, and dispatch (2026-07-30)

Implemented in the first Phase 3 slice:

- `TypeFunc` now retains `may_return_error` independently of `can_raise`; classified
  system calls and user `T^` calls keep their full semantic `success | error` result
  in the AST, including first-class function values. MIR deliberately lowers those
  result sets through an Item lane rather than mistaking an abstract/union `Type*` for
  a runtime `type` object.
- `or` now computes `(left - {error, null}) | right` with the Phase 0 normalized
  type-set helpers. Its result remains physically boxed unless both operands are bool,
  so semantic narrowing cannot select a false native carrier.
- Every `fn` now checks its explicit or implicit return contract, while `pn` remains
  exempt. A proven open tail/return reports E208 with the first offending direct call;
  a genuinely dynamic return reaches the retained checked return boundary. Explicit
  `any` still deliberately admits error values.
- Direct raw calls now test potentially open arguments before native unboxing, COW root
  preparation, or body entry, then merge the exact original error with the normal Item
  result. The same tag check is used before a TCO rewrite mutates parameter registers.
  Public/dynamic/imported calls enforce the dispatch at their boxed ABI wrapper before
  optional/default resolution. This preserves a rejected `var` argument without detaching
  or publishing it.
- A parameter contract now decides admission by whether it actually contains `error`, not
  merely by whether it is literal `any`. `T | error` therefore receives an incoming error
  as a body value; its boundary skips `lambda_type_check`'s generic error-result path only
  for that already-admitted input, while ordinary bad values still return the boundary error.
- Fixed binary type construction to retain the underlying Type values, rather than their
  AST `TypeType` wrappers. Before this correction a parameter written `int | error` degraded
  to the meta-type `type`, so it could neither validate nor express error admission.
- `T^` now parses in value annotations (parameters, `let`/`var`, containers, maps, and
  function-type parameters) through a dedicated `value_error_type` CST node. Its builder
  constructs the same `TypeBinary` union as `T | error`; it does not reuse function-return
  metadata, so it cannot accidentally create a raised channel or caller obligation.
- Corrected static union containment to test each source member against the complete target.
  The former target-first order incorrectly rejected an `int | error` result assigned to the
  equivalent `int^` binding. MIR also now keeps structural/union declaration contracts in the
  boxed Item lane rather than using their internal `type` tag as a native storage carrier.
- The static acknowledgement classifier now recognizes an explicitly error-admitting binding
  (while bare `any` remains non-acknowledging), so the E228 rule follows the decided
  `T^`/`T | error` value-position contract.

New coverage: `type_or_narrowing.ls/.txt` exercises classified sys, direct `T^`, and
first-class `T^` containment; AST metadata assertions pin error provenance and clean
`or` results; `implicit_fn_error_return.ls/.txt` pins E208 and its call-origin text.
`type_param_error_short_circuit.ls/.txt` adds direct, first-class, closure, imported/public,
TCO, optional/default, variadic, native-return, explicit-`any`, `int | error`, and direct/
first-class `int^` cases; it also binds a declared `int | error` result through `int^` without
leaving the Item lane.
`proc/proc_type_param_error_short_circuit.ls/.txt` proves a rejected `pn var` argument
does not run its mutating body or change the caller's error value. The generic-array
MIR root fixture now deliberately declares its producer `any`, preserving the `array_end`
allocation path it audits.

Verification at this checkpoint: `make build`, `make build-test`, the four focused script
tests, the three focused metadata/diagnostic tests, all 12 MIR emission fixtures, and all
15 MIR ratchet probes pass. The fresh `make test262-baseline` run passes 40,261/40,261 with
0 retries. The fresh `make test-lambda-baseline` run passes its input suites (2,104/2,104),
MIR emission (12/12), ratchet (15/15), error-system (90/90), and the new focused scripts,
but ends 3,432/3,665: `dom_module_props` is the pre-existing DOM expectation failure and
232 Lambda script failures remain from unconverted package firewalls (for example
`lambda/package/graph/style.ls:parse`). Do not mark Phase 3 complete until those package
firewalls are migrated and the standing Lambda baseline is clean apart from the documented
DOM expectation.

The value-position shorthand is now implemented: `T^` accepts an incoming error as a value,
exactly like `T | error`, while function-return `T^` remains the enforcing raised-channel
spelling. This first checkpoint is superseded by the 2026-07-31 Phase 3 completion evidence:
the package migrations classify each function by its actual policy (contain locally, disclose
`| error`, or impose `^`) rather than blanket-widening returns.

---

## 8. Phase 4 — remaining enforcement completeness and diagnostics

### 8.1 E228 acknowledgment forms

Extend must-engage recognition to the decided immediate contexts:

- `^err` destructuring and postfix `^`;
- explicit error-admitting binding or parameter destination;
- explicit error-admitting declared return only for a return/tail call;
- `match` with `case error:`;
- explicit `or` rescue.

`any` does not count as acknowledgment. Keep immediate-expression tightness; a distant
error-admitting return does not acknowledge an earlier discarded enforcing call.
Update E228's message with the third suggestion and pin the complete diagnostic.

#### 8.1.1 Implementation checkpoint — immediate E228 contexts (2026-07-31)

Implemented the first Phase 4 slice:

- E228 validation now runs over the completed AST rather than recognizing only a bare
  top-level call while the body is being built. An ordinary wrapper such as `f() + 1`
  therefore cannot hide an enforcing `f()` call.
- The validator accepts only the documented immediate contexts: postfix `^`, `^err`, a
  declared error-admitting binding or parameter, a left-hand `or` rescue, an error-handling
  `match` arm, and an explicit error-admitting return/tail destination. A `T^` function
  return now compares only the success members at its declared return boundary, preserving
  the channel that the signature itself publishes.
- A known error-excluding parameter transparently returns its incoming error before its body
  runs. If the enclosing call is immediately acknowledged, that same handler also owns the
  short-circuited argument; an explicit `any` or erased dynamic destination does not gain
  this privilege.
- E228 now gives all three actionable forms: propagate with `^`, capture with `^err`, or
  recover with `or default`. `type_e228_acknowledgment.ls/.txt` covers the accepted contexts;
  `negative/semantic/unhandled_error_expression.ls/.txt` and its error-GTest assertion pin
  the complete diagnostic. The existing parameter-short-circuit fixture now uses an ordinary
  error value for its explicit-`any` and erased-closure runtime probes, because neither form
  is an E228 acknowledgment for a `T^` call.

Focused verification: `make build`, `make build-test`, the new positive script, and the new
negative error-GTest passed. The full Lambda GTest report is 638/638 (11 negative, 1 binary,
626 auto-discovered scripts), with `git diff --check` clean. `make test-lambda-baseline`
passed all 2,104 input tests and 1,567/1,568 runtime tests (3,671/3,672 overall); its sole
exception remains the unchanged `JavaScriptTests/JsFileTest.Run/dom_module_props` DOM
expectation. `make test262-baseline` passed 40,261/40,261 fully passing baseline tests with
0 non-fully-passing tests, 0 failures, 0 retries, and 0 regressions (42,889 total tests;
2,628 skipped).

### 8.2 Boundary completeness

- ✅ Static checking for literal bracket-form typed map writes (`p["field"] = e`) now
  resolves the same declared shaped field as `p.field = e` when the annotated root and key are
  known. Computed and multi-key paths remain dynamic.
- Retain the existing dynamic transactional fallback for computed keys.
- ✅ `input(url, {schema: Q})` validates a successful parsed root through the same TE-10
  `lambda_type_check` boundary used by dynamic bindings. It retains validator-path diagnostics
  and does not create a second validator/admission implementation.

### 8.3 Diagnostic and test hardening

- ✅ A shared `lambda_type_format_name()` now renders unions, optional values, occurrences,
  named maps, and internal exclusion types from their semantic structure. Static declaration,
  assignment, argument, map-field, and return diagnostics no longer reduce an extended contract
  to the internal name `type`.
- ✅ Array post-validation reports the rebuilt candidate together with the validator's first
  failing path. It no longer pairs an array-wide validator failure with only the inserted leaf.
- ✅ The round-1 type-enforcement runtime negative goldens now pin their public E201/E206
  code/message contracts, and an error-GTest runs each fixture against that text.
- ✅ A `lambda_match_lint` warning identifies `case error:` as dead when the scrutinee is an
  implicit error-excluding parameter; `value: any` remains the explicit opt-in to observe it.
- ✅ A type-value pair joined with `or` is a static E312 that directs users to `|` for a union.
- ✅ Phase-4 references name their authority document/section and completed work is written in
  delivered tense rather than as a future plan.

### 8.4 Eight-argument dynamic ABI limit

✅ Implemented 2026-07-31. A valid signature beyond a physical dynamic-dispatch ceiling is
not an E206 arity mismatch. The runtime reports `E229 UNSUPPORTED_DYNAMIC_ABI` instead,
without adding a language-semantics rule. Generic dynamic dispatch admits zero through eight
ABI arguments; a closure's captured environment consumes one physical slot, so its valid
eight-parameter signature is explicitly rejected at the seven-user-argument closure ceiling.

MIR dynamic calls with four through eight arguments now build a rooted `List` and enter the
existing checked `fn_call_into` dispatcher. This replaces the former lowering that logged a
message and returned the function value. Before native scalar unboxing, the lowering preserves
the returned `ItemError`, so an E229 (or other dynamic-call error) cannot become a fabricated
scalar value. Statically resolved calls retain their compiled ABI.

### 8.5 Validator diagnostics

✅ Implemented as the v1 diagnostic contract. `lambda_type_check` preserves the validator's
first linked error and `lambda_type_error_with_validation` publishes its formatted path in the
rich E201 object (`validator at .field` / `[index]`). This is intentionally a single stable,
specific first path; multi-error aggregation remains diagnostic-completeness debt rather than
an implicit ordering guarantee.

**Exit:** all decided E228 engagement forms work; bracket and input-schema boundaries reuse the
common machinery; negative tests pin actionable diagnostics; both standing gates are verified
against their documented status.

---

## 9. Phase 5 — system/resource fault channel (design prerequisite)

Do not implement this phase from the one-line gap description. “Transparent through fn
frames and catchable at pn `^err`” is a control-flow and recovery-ABI feature, not an
ordinary `ItemError` return.

✅ **Design prerequisite completed 2026-07-31.**
`vibe/Lambda_Design_Exec_Recovery.md` §§11.7–11.12 now specifies the nested
TLS frame ABI, native-safe local `pn` `let value^err = expression` landing semantics,
exact-before-allocation restoration, static fault records and OOM fallback,
transaction barriers, and POSIX/Windows rules. Phase 5 code follows its
ER-S0–ER-S7 order; it may not reintroduce conservative native-stack GC scanning
or use the existing outer runner's `ItemError` conversion as a substitute for a
local fault boundary.

The completed design establishes:

1. a nestable per-thread recovery-target stack rather than one overwritten jump
   buffer;
2. exact landing semantics for pn `^err` versus the global handler;
3. restoration of precise `RootFrame`, number-home, debug-frame, and other runtime
   watermarks after every non-local jump;
4. behavior across imported, nested eval/module, async, worker, and hosted-guest
   execution;
5. pre-reserved/static OOM and stack/depth fault objects that require no allocation;
6. a rule for OOM encountered while constructing an ordinary rich error;
7. explicit confirmation that conservative native-stack GC scanning is not restored;
8. platform coverage for signal/`siglongjmp`, Windows SEH/`longjmp`, and normal
   non-signal depth faults.

The existing outer runner recovery that converts stack overflow to `ItemError` is not
equivalent to a pn-local catch and must not be treated as Phase-5 completion.

**Exit:** deep recursion, OOM injection, root-frame exhaustion, and equality-depth
faults reach the selected pn/global handler without corrupting precise GC state; nested
recovery restores the outer target; platform-focused tests and both standing baselines
pass.

---

## 10. Updated gap inventory

| Gap | Design ref | Size | Phase |
|---|---|---:|---:|
| Contract/effective-type split in `TypeParam`/`TypeFunc` | TE-1/TE-3/TE-5 | M | 0 |
| Internal top exclusions + union/subtraction helpers | TE-5/TE-13 | M | 0 |
| Minimum sys-function success/error metadata | §7.3, TE-9 | M | 0 |
| Value-aware scalar numeric admission | TE-5/TE-6 | M | 1 |
| Recursive structural conversion, if DG-1 accepts it | TE-5/TE-10 | L | 1 |
| C14c scalar/vector/decimal migration | C14c, formal §4.7 | L | 2 |
| Dynamic-read defaults + sticky explicit `any` | TE-5 R1/R5 | M | 3 |
| `or` static narrowing | TE-13 | M | 3 |
| All-dispatch implicit-param short-circuit | TE-5 | L | 3 |
| Honest error inference + fn return firewalls | TE-5, §10.7 | L | 3 |
| E228 acknowledgment extensions | TE-13 | S | 4 |
| Bracket map-write static check | B7b | S | 4 |
| `input(..., {schema: Q})` convenience | TE-10 | S | 4 |
| Diagnostics and negative-golden hardening | TE-4/TE-9 | S | 4 |
| System/resource fault channel | C14 | XL; implemented on POSIX, Windows SEH integration verification pending | 5 |
| Per-param sys-function typing, clean/open specializations | perf stage | — | later |
| Witness caching, direct offsets, two-entry optimization | TE-14/perf | — | later |
| Parenthesized `is` types, general `!`, schema-`any` surface alias | pattern/validator | — | out of scope |

---

## 11. Phase closeout evidence

### 2026-07-30 — Phase 0 implementation checkpoint

- `make build` and `make build-test` passed after the final metadata changes.
- Focused `TypeContractMetadataTest.*`: 3/3 passed; the full
  `test_lambda_errors_gtest.exe` suite passed 87/87.
- The executable regression `test/lambda/type_contract_metadata.ls` produced its paired
  golden `[1, 2, 3]`; its import companion produced `[4, 7]`. The serial AST dump for the
  import companion pins the imported function's implicit contract and its precise effective
  return. It also covers the AST-dump runtime/directory invariant needed to load imports.
- `make test262-baseline` passed on the final tree: 40,261/40,261 fully passed,
  0 non-fully-passing, 0 failed, 0 retries, and 0 regressions (42,889 total tests;
  2,628 skipped).
- `make test-lambda-baseline` remains blocked at 3,654/3,655 by the deterministic,
  out-of-scope `JavaScriptTests/JsFileTest.Run/dom_module_props` expectation that same-document
  `document.adoptNode(docTextA)` detaches `docTextA`. The actual result is that it remains
  parented. Input tests passed 2,104/2,104 and all other Lambda-runtime tests passed
  1,550/1,551, including the Phase-0 metadata and import fixtures. No DOM behavior was changed
  under this type-enforcement goal.
- No benchmark is required for this metadata-only slice. Phase 1 awaits DG-1; the decimal
  portion of Phase 2 awaits DG-5.

### 2026-07-30 — Phase 1 scalar-admission checkpoint

- `make build` and `make build-test` passed after the scalar implementation and the
  Lambda error-test link-closure correction.
- `NumericBoundaryAdmissionTest.ExactScalarConversionsPreserveTargetTags` passed; the
  full `test_lambda_errors_gtest.exe` suite passed 89/89. The focused Lambda batch for
  `number_model_realign` and `proc_type_numeric_boundary_admission` passed 2/2.
- `make test-lambda-baseline` reached 3,657/3,658: all 2,104 input tests and all
  1,553 non-DOM Lambda-runtime tests passed. The only failure remains the deterministic,
  out-of-scope `JavaScriptTests/JsFileTest.Run/dom_module_props` failure recorded in
  the Phase-0 checkpoint; no DOM behavior changed here.
- `make test262-baseline` passed at the scalar checkpoint: 40,261/40,261 fully passed,
  0 non-fully-passing, 0 failed, 0 retries, and 0 regressions (42,889 total tests;
  2,628 skipped). The structural runtime slice below has its own superseding gate.
- No benchmark is required for this correctness-only scalar slice.

### 2026-07-30 — Phase 1 structural-admission completion checkpoint

- `make build` and `make build-test` passed; `git diff --check` was clean. The focused
  auto-discovered regression
  `proc_proc_type_numeric_structural_admission` passed 1/1.
- `make test-lambda-baseline` reached 3,658/3,659: all 2,104 input tests and all
  1,554 non-DOM Lambda-runtime tests passed, including 627/627 Lambda runtime scripts
  and 89/89 Lambda error tests. The sole failure remains the deterministic,
  out-of-scope `JavaScriptTests/JsFileTest.Run/dom_module_props` documented at the
  Phase-0 and scalar checkpoints; no DOM code changed in this slice.
- `make test262-baseline` passed after the structural runtime changes: 40,261/40,261
  fully passed, 0 non-fully-passing, 0 failed, 0 retries, and 0 regressions (42,889
  total tests; 2,628 skipped).
- No benchmark is required for this correctness-only phase. Phase 1 is implemented;
  its full-gate exception is limited to the pre-existing DOM expectation above. Phase 2
  remains the next implementation phase.

### 2026-07-30 — Phase 2 C14c migration completion checkpoint

- `lambda_numeric_classify()` now selects the C14c result domain for scalar and vector
  `div`/`%`: flex/small integer operands produce float, while large-integer operands
  produce decimal. Zero divisors yield their selected domain's `inf`/`nan`, per lane for
  vectors, rather than originating `ItemError`.
- The direct MIR emitter keeps C14c `div`/`%` boxed until an annotated boundary consumes
  the self-describing result. Lexical `(let ..., body)` declarations share that checked
  lowering while remaining expression-local. This avoids both raw-double-to-Item ABI moves
  and accidental publication of lexical bindings to module state.
- DG-5 is complete: `decimal.inf`, `-decimal.inf`, and `decimal.nan` round-trip through
  grammar, printers, formatters, and JS conversion; decimal contexts disable mpdecimal
  traps so invalid/divide-by-zero arithmetic propagates decimal poison. Decimal NaN obeys
  the poison equality/order rule, while decimal infinity compares and hashes numerically
  with float infinity.
- Typed `pn` array writes now preserve the legacy caller-visible mutation ABI. The direct
  store checks the physical typed-array witness and falls back safely if it was degraded;
  it does not replace the callee's borrowed root with a detached COW candidate. Scalar
  native-math calls with statically scalar inputs retain their proven float result, which
  permits the typed-array store witness without weakening vectorized math's boxed path.
  `proc_typed_array_param` now pins the caller-visible mutation; the MIR fixture pins the
  checked physical guard and the absence of a whole-array checked-store call. The reviewed
  MIR-size budgets cover that explicit guard and lock the accompanying reductions.
- Added `number_model_c14c_div_mod` coverage for result tags, zero poison, decimal
  nonzero results, typed N-D vector lanes, explicit integer retyping, and a typed float
  local. Added `decimal_poison_c14c` coverage for decimal parse/print, zero division and
  remainder poison, arithmetic propagation, equality/order, and canonical numeric hashing.
  The focused Lambda cases for both additions, native-math scalar/vector edges, and typed
  procedure-array guards passed. `test_lambda_errors_gtest.exe` passed 88/88;
  `test_lambda_gtest.exe` passed 629/629; `test_lambda_std_gtest.exe` passed 105/105;
  MIR emission passed 12/12 and the MIR ratchet passed 15/15.
- `make build`, `make build-test`, and `make release` passed. `make test-lambda-baseline`
  reached 3,659/3,660: all 2,104 input tests and 1,555/1,556 Lambda-runtime tests passed.
  The only failure is the unchanged, out-of-scope
  `JavaScriptTests/JsFileTest.Run/dom_module_props` expectation; all 629 Lambda scripts,
  all MIR emission/ratchet tests, and all type/error suites passed.
- `make test262-baseline` passed: 40,261/40,261 fully passed, 0 non-fully-passing,
  0 failed, 0 retries, and 0 regressions (42,889 total tests; 2,628 skipped).
- Release performance gate: `./lambda.exe run test/benchmark/awfy/nbody2.ls`, seven
  candidate runs (`63.412`, `74.342`, `71.636`, `67.295`, `75.223`, `75.155`, `71.581` ms;
  median `71.636` ms) versus seven runs of Result18
  `./test/benchmark/exe/lambda-v18-e406aa9b87` (`148.376`, `152.032`, `146.879`,
  `156.960`, `151.346`, `154.016`, `156.630` ms; median `152.032` ms). Every run reported
  `NBody: PASS`. The threshold was no regression versus the Result18 median; the candidate
  is 52.9% faster, so no within-noise judgment is needed.
- Phase 2 is complete. Phase 3 is the next implementation slice; its error/firewall behavior
  remains disabled until its own focused and standing gates pass.

### 2026-07-31 — Phase 3 package-migration and carrier-completeness checkpoint

- Migrated the package and benchmark functions exposed by the clean implicit `fn` firewall.
  Each migration chose a concrete result type, an explicit `any` contract, or a local fallback
  such as `or 0` according to the function's behavior; no blanket return widening was used.
  The graph, PDF, editor, math, and affected benchmark fixtures now exercise those explicit
  policies, and `proc_inferred_return_type.ls/.txt` pins the procedural inferred-return path.
- A `pn` with an exact terminal return now exposes that semantic type to static boundaries while
  its call result remains in the boxed Item ABI. The direct/imported error and string call paths
  likewise preserve their Item carrier until the appropriate boundary converts it.
- Typed local numeric arrays retain their physical `ArrayNum` carrier and guarded element
  witness. A successful checked typed-array write keeps that raw carrier rather than publishing
  a boxed replacement. When a statically scalar index crosses an opaque container carrier, MIR
  reads through representation-neutral `fn_index` and then unboxes the declared scalar result.
  These rules prevent tagged `Item` values from reaching native scalar or string consumers.
- `make build` and `make build-test` passed. The focused inferred-return, sized-array widening,
  and structural-admission tests passed; the full Lambda GTest report was 637/637
  (11 negative, 1 binary, and 625 auto-discovered scripts), with `git diff --check` clean.
- `make test-lambda-baseline` passed all 2,104 input tests and 1,565/1,566 runtime tests
  (3,669/3,670 overall). The sole runtime exception remains the unchanged, out-of-scope
  `JavaScriptTests/JsFileTest.Run/dom_module_props` expectation for same-document
  `document.adoptNode`; this type-enforcement work did not modify DOM behavior.
- `make test262-baseline` passed 40,261/40,261 fully passing baseline tests with 0
  non-fully-passing tests, 0 failures, 0 retries, and 0 regressions (42,889 total tests;
  2,628 skipped).
- Phase 3 is complete subject only to the standing, documented DOM exception. Phase 4 is next.

### 2026-07-31 — Phase 4 immediate-E228 checkpoint

- E228 now validates the completed AST, so an arbitrary expression wrapper cannot suppress an
  enforcing call. It accepts only the immediate handlers decided in TE-13: `^`, `^err`, an
  explicit error-admitting value destination, a left `or` rescue, `match case error:`, and an
  explicit error-admitting return/tail destination. The raised-return boundary compares the
  success members while leaving the declared error channel intact.
- An immediately handled call propagates that acknowledgment through a known error-excluding
  parameter only when the caller's short-circuit returns the same Item before entering the
  body. Explicit `any` and erased dynamic destinations remain non-acknowledging. The
  top-level scan is skipped after an earlier build error, because duplicate-definition recovery
  may re-link placeholders into an invalid AST list.
- New `type_e228_acknowledgment.ls/.txt` covers the accepted contexts. New
  `negative/semantic/unhandled_error_expression.ls/.txt` and its error-GTest assertion pin
  E228's complete three-form diagnostic. The parameter-short-circuit runtime fixture retains
  its explicit-`any` and erased-closure probes with ordinary error values, which test those
  destinations without falsely treating them as acknowledgments of `T^`.
- `make build`, `make build-test`, focused positive/negative runs, and
  `test_lambda_errors_gtest.exe` (91/91) passed. The full Lambda GTest report was 638/638
  (11 negative, 1 binary, 626 scripts); `git diff --check` passed.
- `make test-lambda-baseline` passed 2,104/2,104 input tests and 1,567/1,568 runtime tests
  (3,671/3,672 overall). The only exception remains the unchanged, out-of-scope
  `JavaScriptTests/JsFileTest.Run/dom_module_props` expectation. `make test262-baseline`
  passed 40,261/40,261 fully passing baseline tests, with 0 non-fully-passing tests,
  0 failures, 0 retries, and 0 regressions (42,889 total tests; 2,628 skipped).
- This is a verified Phase 4 sub-slice. Its remaining input-schema, dynamic-ABI, and diagnostic
  work is completed by the later Phase 4 checkpoints below.

### 2026-07-31 — Phase 4 literal bracket-map boundary checkpoint

- An explicitly annotated map root now resolves a single literal string bracket key through
  its declared `TypeMap` shape before an assignment is lowered. `p["age"] = "old"` therefore
  uses the same E201 static boundary as `p.age = "old"`; computed keys and multi-key indexes
  intentionally retain the existing transactional runtime boundary.
- New `negative/semantic/type_enforcement_bracket_map_write.ls/.txt` and its dedicated
  `StaticBracketTypedMapWritesRejectKnownMismatches` error-GTest assertion pin the compile-time
  E201 message. `make build`, `make build-test`, `test_lambda_errors_gtest.exe` (92/92), and
  `test_lambda_gtest.exe` (638/638) passed; `git diff --check` was clean.
- `make test-lambda-baseline` passed 2,104/2,104 input tests and 1,568/1,569 runtime tests
  (3,672/3,673 overall). Its sole exception remains the unchanged, out-of-scope
  `JavaScriptTests/JsFileTest.Run/dom_module_props` expectation. `make test262-baseline`
  passed 40,261/40,261 fully passing baseline tests with 0 non-fully-passing tests, 0 failures,
  0 retries, and 0 regressions (42,889 total tests; 2,628 skipped).
- This is a verified Phase 4 sub-slice. Its remaining input-schema, dynamic-ABI, and diagnostic
  work is completed by the later Phase 4 checkpoints below.

### 2026-07-31 — Phase 4 `input` schema-boundary checkpoint

- `input(url, {type: F, schema: Q})` now requires the optional `schema` value to be a type and,
  after a successful parse, passes the root through `lambda_type_check(..., "input schema")`.
  This reuses TE-10's conversion, admission, and first-validator-path reporting rather than
  creating an input-specific validator. A failed JSON `Person` input therefore reports E201 with
  the existing `.age` validator detail; an open named map retains its extra parsed fields.
- A named `type Q = ...` is assignment-shaped internally, as are values whose inferred type is
  `T^`. The MIR emitter now uses an explicit `is_type_definition` marker preserved at AST build
  time before materializing a type-list value. This prevents an imported raised-return value from
  being mistaken for a type alias while allowing `schema: Q` inside a `pn` options map.
- New valid/invalid JSON inputs plus `type_enforce_input_schema.ls/.txt` cover successful
  validation and open extra fields. `negative/runtime/type_enforce_input_schema.ls/.txt`, its
  dedicated runtime error-GTest, and the Lambda negative test pin the E201 message and `.age`
  path. The existing `import_error_destr` script guards the type-alias/value classification.
- `make build`, `make build-test`, `test_lambda_errors_gtest.exe` (93/93), and
  `test_lambda_gtest.exe` (640/640) passed; `git diff --check` was clean.
- `make test-lambda-baseline` passed 2,104/2,104 input tests and 1,571/1,572 runtime tests
  (3,675/3,676 overall). Its sole exception remains the unchanged, out-of-scope
  `JavaScriptTests/JsFileTest.Run/dom_module_props` expectation. `make test262-baseline`
  passed 40,261/40,261 fully passing baseline tests with 0 non-fully-passing tests, 0 failures,
  0 retries, and 0 regressions (42,889 total tests; 2,628 skipped).
- This is a verified Phase 4 sub-slice. Its remaining diagnostic and validator-contract work is
  completed by the following Phase 4 checkpoint.

### 2026-07-31 — Phase 4 diagnostic-hardening completion checkpoint

- `lambda_type_format_name()` is now the shared semantic formatter for static type-contract
  diagnostics. It renders unions, optional values, occurrences, named maps, and internal
  exclusion types without exposing the implementation carrier name `type`; the union
  declaration negative fixture pins the public result.
- Array post-validation now reports the rebuilt candidate that the validator inspected, retaining
  its first failing path instead of pairing an array-wide failure with only the inserted leaf.
  The first validator error remains the explicitly documented v1 E201 ordering contract.
- The seven round-1 runtime negative fixtures now carry exact E201/E206 golden diagnostics, and
  a single error-GTest executes each through the procedural runner. This makes boundary messages
  and validator paths part of the regression contract rather than incidental output.
- `lambda_match_lint` now warns when `case error:` targets an implicit error-excluding parameter;
  the matching `value: any` fixture proves an explicit error-admitting contract remains valid.
  Type values joined with `or` now produce E312 with the direct `|` union guidance, while ordinary
  unresolved calls retain normal boolean `or` semantics.
- `make build`, `make build-test`, the focused type-valued-`or`, implicit-parameter match, and
  math/LaTeX Lambda tests, and `git diff --check` passed. The full Lambda baseline passed
  98/98 error GTests, 643/643 Lambda GTests, and 2,104/2,104 input tests; its only failure is
  the unchanged, out-of-scope `JavaScriptTests/JsFileTest.Run/dom_module_props` DOM expectation
  (1,579/1,580 runtime tests; 3,683/3,684 overall).
- `make test262-baseline` passed 40,261/40,261 fully passing tests with 0 non-fully-passing,
  0 failed, 0 retries, and 0 regressions (42,889 total tests; 2,628 skipped).
- Phase 4 is complete subject only to that standing DOM exception. Phase 5 remains a separate
  recovery milestone; its design and ER-S0 substrate are recorded below.

### 2026-07-31 — Phase 4 dynamic-ABI checkpoint

- `E229 UNSUPPORTED_DYNAMIC_ABI` now distinguishes a valid signature that exceeds the physical
  dynamic dispatch capacity from E206 argument-count mismatch. `fn_call` and `fn_call_into`
  report a rich, stack-bearing E229 with the function name, required ABI arguments, and the
  applicable ceiling. The generic dispatcher accepts zero through eight arguments; closures
  retain one slot for their environment and therefore diagnose an eight-user-argument indirect
  closure call at the seven-user-argument ceiling.
- MIR indirect calls with four through eight source arguments now construct a rooted argument
  list and invoke `fn_call_into`. Rooting both the callee and every boxed list member protects
  against allocations during argument evaluation. An error guard before native scalar unboxing
  preserves dispatcher failures instead of turning them into zero or another fabricated scalar.
- New `type_enforce_dynamic_abi.ls/.txt` proves four- and eight-argument dynamic named-function
  calls return `[10, 36]`. New `negative/runtime/type_enforce_dynamic_abi.ls/.txt` and the
  dedicated `RuntimeError_UnsupportedDynamicAbi` error-GTest pin the exact closure E229
  diagnostic.
- `make build`, `make build-test`, `test_lambda_errors_gtest.exe` (94/94), and the focused
  `type_enforce_dynamic_abi` script test passed; `git diff --check` passed. The full Lambda
  baseline passed 2,104/2,104 input tests and 1,574/1,575 runtime tests (3,678/3,679 overall).
  Its only exception remains the unchanged, out-of-scope
  `JavaScriptTests/JsFileTest.Run/dom_module_props` same-document `adoptNode` expectation.
  `make test262-baseline` passed 40,261/40,261 fully passing tests, with 0 non-fully-passing,
  0 failed, 0 retries, and 0 regressions (42,889 total tests; 2,628 skipped).
- This is a verified Phase 4 sub-slice. Its former §8.3 diagnostic and §8.5 validator-contract
  work is completed by the preceding diagnostic-hardening checkpoint.

### 2026-07-31 — Phase 5 recovery-ABI design checkpoint

- The live recovery inventory was reconciled with the source before Phase 5 code: direct
  LambdaJS MIR, hosted-guest entry, and the event-loop guard now use the shared side-stack
  checkpoint, while every production execution entry still overwrites the one TLS
  `_lambda_recovery_point`/armed flag. That non-nestable target remains the primary blocker.
- `Lambda_Design_Exec_Recovery.md` now defines the mandatory frame LIFO, target-selection and
  transaction-barrier rules, exact snapshot fields, non-allocating fault records, `pn` `^err`
  versus global-handler semantics, async/worker/Jube limits, and POSIX/Windows discipline.
  It explicitly rejects a helper-owned `setjmp`, a fault handler spanning a suspension, and
  treating arbitrary memory faults as a language-catchable error.
- This is a design-only checkpoint: no runtime recovery source changed and no baseline claim is
  made from it. `git diff --check` passed. ER-S0 (static fault-record substrate and ordinary-error
  OOM fallback mapping tests) is the first implementation slice.

### 2026-07-31 — Phase 5 ER-S0 fault-record substrate

- `LambdaFaultRecord` now embeds a non-owning static `LambdaError`, with an explicit fault-reason
  enum and an optional `prior_error_code`. `lambda_fault_record_prepare()` and
  `lambda_fault_record_from_error_allocation_failure()` perform only fixed-field initialization;
  they allocate neither a rich error nor a fallback diagnostic.
- `LambdaError::is_static` makes the ownership rule executable: `err_free()` and
  `err_release_payload()` leave an embedded fault error and its pre-reserved message intact.
  This allows a later recovery landing to install the record as `Context::last_error` without
  passing static storage to normal heap cleanup.
- `ErrorCreationTest.FaultRecordUsesStaticErrorStorage` and
  `ErrorCreationTest.ErrorAllocationFailureBuildsStaticOomFault` pin the reason-to-error mapping,
  OOM prior-code preservation, and cleanup safety. The full error suite passed 100/100.
- This slice deliberately does **not** route `set_runtime_error()` or any C14 origin through the
  record yet: ordinary rich-error allocation remains unchanged until ER-S1/ER-S4 provide a live
  recovery frame and a safe landing owner. It is therefore substrate coverage, not a claim that
  arbitrary runtime OOM is already recoverable.
- `make build`, `make build-test`, and the two focused error tests passed. `make test-lambda-baseline`
  reached 3,685/3,686: 2,104/2,104 input tests, 100/100 error GTests, and 1,581/1,582 runtime
  tests passed; the sole failure remains the unchanged, out-of-scope
  `JavaScriptTests/JsFileTest.Run/dom_module_props` expectation. After forcing the optimized
  runtime rebuild, `make test262-baseline` passed 40,261/40,261 fully passing tests with 0
  non-fully-passing tests, 0 failures, 0 retries, and 0 regressions (42,889 total; 2,628 skipped).

### 2026-07-31 — Phase 5 ER-S1 nested-frame substrate

- `lambda/runtime/recovery_frame.h/.c` now owns the nested TLS `LambdaRecoveryFrame` LIFO.
  Each frame contains its exact `Context` owner, the shared checkpoint, an embedded static fault
  record, a platform jump buffer, capability mask, and explicit prepared/armed/landed/disarmed
  state. It is intentionally a separate runtime module: adding `LambdaFaultRecord` directly to
  `side_stack.h` leaked the legacy global `SourceLocation` into modern input code, so the frame
  header carries the fault dependency while the side-stack interface stays dependency-light.
- `LambdaRecoveryCheckpoint` now captures and restores the existing side-root and number-stack
  watermarks **and** `mir_return_lane` plus `mir_bitcast_scratch`. A fault landing restores all
  of those values before any diagnostic inspection or allocation. `lambda_recovery_frame_pop()`
  enforces strict TLS LIFO order and disarms a returned frame so no stale target can survive a
  native return.
- `SideStackRootFrameTest.RecoveryFramesRestoreNestedWatermarksAndRemainLifo` proves nested
  restoration, static OOM fault preparation, outer-frame preservation, and out-of-order-pop
  rejection. The full `test_gc_heap_gtest.exe` suite passed 63/63.
- At this checkpoint the frame was substrate only. ER-S2 subsequently migrated the runner,
  cached MIR, direct JS MIR, and both hosted-Jube target sites; the separate event-loop and
  batch-containment targets remain intentionally outside the execution-frame ABI.
- `make build`, focused and full GC tests, and `make test-lambda-baseline` passed subject only to
  the standing DOM expectation (2,104/2,104 input; 100/100 error; 1,581/1,582 runtime;
  3,685/3,686 overall). `make test262-baseline` passed 40,261/40,261 fully passing tests, with
  0 non-fully-passing, 0 failed, 0 retries, and 0 regressions (42,889 total; 2,628 skipped).

### 2026-07-31 — Phase 5 ER-S2 execution-boundary migration

- The legacy `_lambda_recovery_point` and `_lambda_recovery_armed` TLS globals are gone. The
  POSIX stack-overflow handler reads `lambda_recovery_frame_tls_top` directly, selects the nearest
  eligible local/execution frame unless an enclosing transaction barrier takes priority, stores
  only the enum-sized fault reason, and jumps to that frame's owned buffer. The Windows SEH branch
  selects the same frame shape after `_resetstkoflw`. Ineligible
  signals remain fail-stop; the POSIX handler no longer logs or allocates before the jump.
- Runner, cached Lambda MIR, direct LambdaJS MIR, and both hosted-Jube entry forms now establish
  their own direct `setjmp` checkpoint around generated execution. Frame storage is heap-backed:
  a `siglongjmp` leaves automatic objects modified after `setjmp` indeterminate, whereas the
  landing must inspect and restore the selected frame. Every normal return and landing disarms,
  pops, and releases its frame; an inner normal return therefore restores the outer target.
- `SideStackRootFrameTest.HeapFramesSurviveNestedNonLocalLanding` performs a real nested
  non-local jump, proves inner watermark restoration, and verifies that the outer frame remains
  armed after the inner frame is popped. The focused side-stack group passed 5/5 and the full
  `test_gc_heap_gtest.exe` suite passed 64/64. The established runtime stack-overflow regression
  also passed within the 100/100 error suite.
- The ER-S2 audit found no additional producer of the removed shared target in eval/import or task
  code. Their transaction, suspension, and callback semantics remain ER-S5 work; the separate
  Test262 batch and JS event-loop fault-containment buffers remain ER-S6 scope. ER-S3 still owns
  `pn` local fault lowering, and ER-S4 still owns non-allocating stack/RootFrame/OOM/equality
  origins and static fault publication.
- `make build` passed. `make test-lambda-baseline` reached 3,685/3,686: 2,104/2,104 input tests,
  100/100 error GTests, and 1,581/1,582 runtime tests passed; the sole unchanged failure remains
  `JavaScriptTests/JsFileTest.Run/dom_module_props`. After a forced release rebuild,
  `make test262-baseline` passed 40,261/40,261 fully passing tests, with 0 non-fully-passing,
  0 failed, 0 retries, and 0 regressions (42,889 total; 2,628 skipped; 122.5s).

### 2026-07-31 — Phase 5 ER-S3 native-safe `pn` `^err` lowering

- MIR now emits the platform checkpoint directly in the generated activation: `sigsetjmp` on
  POSIX and `setjmp` on Windows receive the frame's physical jump-buffer address. This avoids
  the invalid returned-helper pattern. The normal path arms, evaluates, boxes, and retires the
  frame; a landing restores all precise watermarks before copying its static fault record into
  TLS fallback storage and publishing the resulting `ItemError` for the `^err` split.
- Concurrency analysis classifies every destructuring RHS only after its may-await fixed point.
  A non-suspending RHS in a user `pn` receives the local fault frame. A suspending RHS retains
  the established ordinary `ItemError` destructuring semantics but does not retain a `jmp_buf`
  across a task poll; any C14 fault there belongs to the task execution boundary. This preserves
  the existing `wait(...)` and `io.read(...)` `^err` programs while keeping native recovery
  lifetime-safe.
- Stack and native RootFrame fault selection now accepts the nearest armed local-fault frame or
  execution frame. `SideStackRootFrameTest.NativeRootFaultChoosesLocalThenOuterFrame` proves a
  real RootFrame fault lands in the inner local frame, restores it, then reaches the outer frame
  after the inner frame is retired. `proc_local_error_destructure.ls/.txt` pins ordinary returned
  error splitting (`[null, true, error, 7, false]`), and its async companion proves a completed
  local RHS is retired before a later `wait` (`[7, false, 3]`).
- ER-S4 remains required before claiming an end-to-end local catch for every C14 source. In
  particular, the MIR side-stack/TCO overflow paths still call
  `lambda_stack_overflow_error()` and return an ordinary `ItemError`; ER-S4 must publish their
  non-allocating fault reason instead of treating that ordinary return as a local system fault.
- `make build` passed. `test_gc_heap_gtest.exe` passed 65/65, including all 6 side-stack/root-
  frame tests. The full error suite passed 100/100. `make test-lambda-baseline` reached
  3,687/3,688: input 2,104/2,104, Lambda runtime 1,583/1,584, and all 645 Lambda runtime
  scripts passed. The sole unchanged failure is
  `JavaScriptTests/JsFileTest.Run/dom_module_props`.
- After `make -B -C build/premake config=release_native lambda -j8`,
  `make test262-baseline` passed 40,261/40,261 fully passing tests with 0 non-fully-passing,
  0 failed, 0 retries, and 0 regressions (42,889 total; 2,628 skipped; 114.0s).

### 2026-07-31 — Phase 5 ER-S4 through ER-S7 POSIX recovery completion

- **ER-S4 — static resource origins.** MIR side-stack/TCO stack guards and native
  `RootFrame` exhaustion now transfer a prebuilt fault record instead of formatting an
  ordinary error after capacity is exhausted. Rich-error allocation failure raises static
  OOM with the original error code as non-allocating prior metadata. Equality-depth
  exhaustion uses the same static channel only when a procedural local `^err` frame is
  active; the pre-existing functional `let ^err` value flow remains an ordinary returned
  error. `proc_local_system_fault.ls/.txt` and `proc_local_equality_fault.ls/.txt` pin
  the static stack (`308`) and equality (`300`) local results, and
  `SideStackRootFrameTest.StaticFaultOriginsLandWithoutAllocating` covers the no-allocation
  record path.
- **ER-S5 — short-lived task and transaction boundaries.** Every scheduler poll owns an
  execution frame and retires it before parking. Its landed result is copied into the
  task-owned static record, so a later fault cannot overwrite an earlier completed task.
  Imported MIR module initializers run under transaction frames; an enclosing barrier
  takes priority over any inner local `^err` frame, retires that abandoned chain, resets
  partial module slabs, and only then forwards to the outer execution boundary. Hosted Jube
  entries now poison the abandoned guest runtime/scalar-home slots before returning the
  static result. `LambdaConcurrencyRuntime.TaskPollFaultCompletesWithDurableStaticResult`,
  `SideStackRootFrameTest.TransactionBarrierWinsOverInnerLocalFault`, and
  `conc/task_system_fault.ls/.txt` cover the task and transaction behavior. A direct
  import-cone equality-depth probe exits with `E300` before the importing `pn main`
  can print, confirming the cached-MIR transaction boundary in the real import path.
- **ER-S6 — arbitrary crash containment.** The production JS event-loop SIGSEGV guard,
  its alternate jump buffer, and its post-corruption continuation path were removed. C14
  faults continue through the active recovery frame; arbitrary memory faults are no
  longer recast as language errors. Test262's independent batch containment policy is
  unchanged.
- **ER-S7 — precise-root and platform state.** The root/number watermark recovery suite
  now has 8/8 passing `SideStackRootFrameTest` cases, including local-then-outer native
  RootFrame selection and transaction-priority local-frame retirement.
  `vibe/Lambda_Design_Stack_Rooting.md` is reconciled with the
  TLS-LIFO recovery ABI and removed event-loop signal guard. The POSIX implementation and
  tests are complete. The Windows SEH branch uses the same frame selection/landing ABI,
  but its required live integration execution has not been run on this macOS host and
  remains the sole Phase 5 verification gap.
- Focused checks passed: `make build`; `SideStackRootFrameTest.*` (8/8);
  `LambdaConcurrencyRuntime.*` (12/12); and
  `NegativeScriptTest.RuntimeError_StackOverflow` (1/1). The direct procedural probes
  produced `[null, true, 308, "Stack overflow"]`,
  `[null, true, 300, "Structural equality recursion limit exceeded"]`, and the task
  probe produced `[null, true, 308, "Stack overflow"]`.
- `make test-lambda-baseline` reached 3,690/3,691: input 2,104/2,104 and Lambda runtime
  1,586/1,587. The only failure remains the pre-existing out-of-scope
  `JavaScriptTests/JsFileTest.Run/dom_module_props`; all 648 Lambda-script tests passed.
  After a release rebuild, `make test262-baseline` passed 40,261/40,261 fully passing
  tests, with 0 non-fully-passing tests, 0 failures, 0 retries, and 0 regressions
  (42,889 total; 2,628 skipped; 119.2s).

Each phase appends a dated evidence block here when implemented. Record:

- commit(s);
- focused test commands and counts;
- `make test-lambda-baseline` final totals;
- `make test262-baseline` totals, failures, retries, and regressions;
- release benchmark command, raw runs, comparison baseline, and threshold where
  applicable;
- remaining known gaps moved to a later phase.

Do not mark Round 2 complete merely because the original round-1 corruption probes
remain green. Completion requires Phases 0–4; Phase 5 remains a separately designed
runtime-recovery milestone unless the design explicitly folds it back into this scope.
