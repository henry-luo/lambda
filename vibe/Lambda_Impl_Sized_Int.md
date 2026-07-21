# Stack API Scalar Realignment and Sized Integers — Historical Implementation Record

- **Status:** IMPLEMENTED for scalar storage and `ushr`; retained as a
  historical completion record. Item 1's value-based mixed-`u64` policy was
  implemented at the time but is now superseded by the type-directed number
  model. Its replacement is implemented in `vibe/Lambda_Impl_Numbers.md`.
- **Date:** 2026-07-16; revised 2026-07-20 for implementation of the
  scalar-storage realignment
- **Primary design:** `vibe/Lambda_Design_Stack_API.md` Phase 7 and §15.
- **Sized-integer design:** `vibe/Lambda_Type_Int_Sized.md` (§7 open items
  1–2; both tasks are retained here). Promotion table:
  `vibe/Lambda_Type_Int.md`. Number model:
  `vibe/Lambda_Semantics_Number_Model.md`.
- **Convention:** `file:line` references drift; confirm against symbol names.

This revision retains every original task and its rationale. Item 0 is the
representation/lifetime prerequisite; Item 1 records the historical
mixed-`u64` correctness and crash fix but is no longer semantic authority;
Item 2 is the completed `ushr` builtin.

---

## Item 0 — Stack API numeric-scalar realignment

### 0.0 Completion inventory (implemented 2026-07-20)

| Live path | Final representation / owner | Migration package |
|---|---|---|
| Local literal, conversion, evaluator, typed-array read | Full-width `INT64`/`UINT64` number home in the current activation | P0.1 |
| Generated internal return and imported scalar result | Caller-donated number home; callee restores its watermark | P0.2 |
| Array/list/map field, environment, module slot, typed storage | Destination-owned raw scalar payload; reads rematerialize into a current home when needed | P0.3 |
| Item-only public/opaque persistence | Immutable GC scalar cell through `lambda_item_heap_rehome()`; actual allocations are counted per numeric type | P0.5 |
| Dynamic datetime literal, conversion, and return | GC-owned `DateTime` object via `push_k()`; ordinary Item return | P0.4 |
| Static input-parser datetime | Input-arena-owned object via `MarkBuilder::createDateTime()` | P0.4 |

The fallback counter is deliberately an allocation counter, not a tag test:
`lambda_scalar_heap_rehome_count(INT64/UINT64/FLOAT)` only increases when an
ownerless boundary actually creates a GC scalar cell. It is the diagnostic
needed to retire this interim fallback later without treating every numeric
Item as GC-owned.

### 0.1 Required end state for this round

The following table is normative for this implementation plan; detailed
ownership rules and the scenario inventory remain in Stack API §15.

| Situation | `INT64` | `UINT64` | `DTIME` |
|---|---|---|---|
| Small/local value | no inline form; number home when transient, or retain verified persistent backing | no inline form; number home when transient, or retain verified persistent backing | owner-backed object: dynamic GC or static Mark Input arena |
| Wide transient value | number home | number home | owner-backed object; never a number home |
| Generated-function return | caller-donated number home | caller-donated number home | ordinary GC-owned `Item` |
| Array/environment storage | destination-owned scalar storage | destination-owned scalar storage | retain the owner-backed pointer |
| Persistent slot without natural owner | immutable GC scalar cell for now | immutable GC scalar cell for now | already owner-backed; no scalar-cell fallback |

Out-of-band `DOUBLE` continues to use number homes, caller-donated homes, and
destination-owned storage. Its ownerless persistent fallback is the same
interim immutable GC scalar cell used by `INT64` and `UINT64`.

The long-term direction is that `DOUBLE`, `INT64`, and `UINT64` have no
standalone heap representation and therefore require no numeric GC-root or
collector cases. That final step is deliberately **not** an acceptance
condition for this round. `DTIME` is excluded from that direction and remains
object-owned rather than activation-owned. Dynamic values use GC ownership;
static Mark values use Input-arena ownership. During the transition, provenance
and boundary metadata—not the numeric type tag alone—must distinguish a
number-home/owner-backed value from an ownerless persistent GC cell.

### 0.2 Non-negotiable representation and lifetime invariants

- `INT64` magnitude never selects representation. Remove the compact inline
  encoding; zero, small, boundary, and full-width values use the same rules.
- Every transient `INT64`/`UINT64` producer writes one raw 64-bit payload word
  to a current activation home. That word is not a GC root.
- Every generated `INT64`/`UINT64` return uses the caller-donated canonical
  home and restores the callee number watermark completely.
- A retaining internal destination copies the payload into storage it owns.
  Moving/resizing that owner rebases any interior `Item` pointer, and reading
  from movable storage copies into the consumer's activation home unless a
  stable borrow is proven.
- An Item-only boundary with no natural owner fails closed by copying a
  `DOUBLE`/`INT64`/`UINT64` payload into an immutable GC scalar cell. Each such
  fallback is counted by boundary class; it must not become the default boxer.
- Every dynamic-runtime `DTIME` constructor and producer creates a GC object;
  `MarkBuilder` creates static parser values in the Input arena. Access goes
  through datetime accessors and must not assume a one-word object layout.
- Old and new encodings must not coexist in one runtime after the migration.

### 0.3 Stages

These are work packages, not independently shippable representations.
Infrastructure may land while unreachable or behind the old producer paths,
but enabling the P0.1 number-home producers must be atomic with the P0.2,
P0.3, and P0.5 escape consumers they can reach. P0.4 may migrate datetime in
its own atomic cutover because it changes a separate semantic type.

**P0.0 — Freeze the contract and inventory the live representation — DONE.** Before
editing producers, record every current `INT64`, `UINT64`, `DOUBLE`, and
`DTIME` path in a table appended to this document:

- local/literal/conversion/typed-array producers;
- exact and dynamic generated returns, imports, discarded results, and tail
  calls;
- array/list/map/object fields, closure environments, module/global slots,
  async/generator state, task/promise/message/exception state;
- public and runtime-indirect returns, embedding/plugin boundaries, opaque
  retaining calls, singleton/TLS slots, and cross-thread handoffs;
- GC-root classifiers, marker/collector cases, number-frame relocation, and
  heap-rehome helpers.

For each path record its current representation, required target ownership,
whether it may cross `MAY_GC`, and its migration stage. Record baseline counts
for the three known `UINT64` heap-producer sites and each ownerless rehome class
so the interim fallback remains visible.

**P0.1 — Unify local `INT64`/`UINT64` representation — DONE.**

- Remove the compact inline `INT64` marker, mask, encoder, decoder,
  discriminator, GC filter, and representation-specific tests.
- Make `box_int64_value()` a full-domain number-home producer. Split legacy
  native helpers that use `INT64_ERROR == INT64_MAX` as an error transport
  from ordinary value boxing; a valid `INT64_MAX` must never become
  `ItemError`.
- Introduce one shared full-domain `UINT64` number-home boxer and route all
  transient producers through it. In particular, replace the currently
  duplicated heap allocations in numeric evaluation, typed-array reads, and
  `coerce_uint64()` rather than promoting a heap-backed helper.
- Replace `MarkReader`'s `ELEM_UINT64` signed-pointer reinterpretation with the
  shared unsigned path. Keep signedness in the `Item` tag/analysis, not in a
  distinct physical home.
- Make `get_int64()`/`get_uint64()` and all consumers accept only the new
  pointer-backed full-width representation. Retain the separate compact
  encodings for plain `INT` and `NUM_SIZED`.

**P0.2 — Add `UINT64` to the generated scalar-home ABI — DONE.** Extend the shared
representation analysis and MIR lowering, not language-specific copies:

- add `UINT64` to exact/dynamic scalar classification, result metadata,
  imported-result adoption, caller-home transfer, discard scratch, normal and
  error return lanes, and tail-home forwarding;
- add the corresponding unsigned raw-value/MIR mode wherever signed `I64` is
  currently modeled, while using the same one-word physical home;
- update both Lambda and JS internal/public body variants and all imported
  call adapters; public Item-only wrappers consume a local caller home and
  apply P0.5 before restoring it;
- add `UINT64` to number-frame adoption/rebasing. Keep the existing `DTIME`
  path working until its atomic P0.4 cutover, then remove it rather than
  leaving a mixed representation;
- do not add the hidden caller-home ABI to C2MIR. The backend is now excluded
  from supported builds and tests; its archived source is not a compatibility
  target.

**P0.3 — Complete destination-owned integer storage — DONE.** Add `UINT64` beside
`INT64`/`DOUBLE` in array/list scalar tails, owned Item slots, typed map/object
fields, closure/dynamic environments, module/global bindings, resumable
frames, and task/promise/message/exception records. Extend relocation and
rebase logic once through shared scalar-storage helpers. Typed-array reads copy
`INT64`/`UINT64` values into the current activation home; they do not allocate
standalone heap objects. Audit JS `BigInt64Array`/`BigUint64Array` egress as
part of this stage.

**P0.4 — Remove datetime from numeric homes — DONE.** Replace dynamic runtime
datetime literals, constants, conversions, builtins, and imported producers
with a GC datetime constructor. Static input-parser output constructed through
`MarkBuilder` remains owned by the `Input` arena. Remove `DTIME` from
scalar-home return modes, caller-home metadata, side-number classification,
destination scalar tails, and number-frame relocation. Root dynamic datetime
Items across every `MAY_GC` boundary; preserve the input arena for static Mark
Items. Keep object construction and access behind shared helpers so the payload
may expand beyond 64 bits later.

**P0.5 — Make ownerless persistence explicit and measurable — DONE.** Extend the
persistent rehome boundary to create immutable GC scalar cells for
`DOUBLE`/`INT64`/`UINT64`, but call it only after the P0.0 ownership audit has
proved that no caller home or destination owner exists. Cover public/indirect
returns, embedding retention, opaque retaining native calls, singleton/TLS
slots, and ownerless cross-thread handoff. Use provenance-aware root/mark logic
while both activation homes and fallback cells exist. Add per-boundary
diagnostics or test counters; do not infer GC ownership from a numeric tag.
A known numeric value in an activation/owner home is not a root, but a slot
that retains an ownerless GC fallback must carry enough provenance to remain a
root until that retention ends. Dynamic marking must validate actual GC-heap
membership before following the recovered payload address.

**P0.6 — Remove transitional paths and enforce the cutover — DONE.** Delete compact
`INT64`, heap-backed transient `UINT64`, and number-stack `DTIME` producers,
stale classifier cases, duplicate helpers, and comments describing the old
split. Add verifier/assertion hooks that reject an inline `INT64`, a transient
heap `UINT64`, a number-home `DTIME`, unresolved scalar-home metadata, and a
retaining numeric edge with neither an owner nor the explicit fallback.

### 0.4 Verification and acceptance gates

Focused implementation checks pass: the representation gtest, the new
mixed-`u64` and `ushr` goldens, the procedure stack-frame regression, datetime,
and the LambdaJS stable-BigInt-return regression.

**Final execution record (2026-07-20).**

| Gate | Result |
|---|---|
| `make build` and `make build-test` | passed |
| `make test-lambda-baseline` | 1,389 runtime tests and 2,105 input-baseline tests passed; 0 failed |
| `make test262-baseline` | 40,261/40,261 fully passed; 0 non-fully-passing, 0 failed, 0 regressions, and 0.0s retry time |

The debug-MIR controls use the same representative implementation bodies
immediately before and after the corresponding Phase 7 lowering change. They
prove that the scalar ownership cleanup reduces instructions per frame in both
front ends, rather than moving the work into a wrapper:

| Implementation body | Before | After | Reduction |
|---|---:|---:|---:|
| Lambda `_phase7_dtime_return_107` | 31 instructions, 21 locals | 22 instructions, 15 locals | 29.0% instructions |
| LambdaJS `_js_phase7BigIntReturn_146_body` | 17 instructions, 10 locals | 15 instructions, 9 locals | 11.8% instructions |

- Rewrite `test/test_item_repr_gtest.cpp` to prove small, zero, boundary, and
  full-width `INT64`/`UINT64` values have no inline representation.
- Add repeated local, imported-result, normal/error-return, discarded-result,
  and tail-call tests for both signed and unsigned 64-bit values. Million-call
  loops must show number-stack use bounded by peak simultaneous liveness.
- Force collection at destination storage and ownerless fallback boundaries;
  cover relocation/rebase for arrays, closures, modules/globals, resumable
  state, tasks/promises, exceptions, public returns, embedding, and
  cross-thread transfer. A dynamic root holding an actual GC scalar cell must
  keep it alive, while the same numeric tag backed by a number home must never
  make the collector scan or retain that number-frame address.
- Prove dynamic datetime payloads are GC-managed, static `MarkBuilder`
  payloads are Input-arena-owned, both survive their ownership boundaries,
  none points into a number frame, and consumers make no one-word layout
  assumption.
- Run `make build`, `make build-test`, focused representation/GC/sized-numeric
  tests, `make test-lambda-baseline`, the Node preliminary suite, and
  `make test262-baseline` with zero failures and zero retry-only results.
  Regenerate generated headers through `make` when the public data header
  changes; do not edit generated files manually.
- Measure performance only with `make release`. Report numeric heap/fallback
  counts and number-home high-water marks before and after the cutover.

---

## Item 1 — historical value-preserving `u64` repair (superseded)

> **Superseded 2026-07-20 by the final type-directed rule.** The failure
> analysis, implementation steps, and tests below are retained as history.
> They fixed signed reinterpretation and the `INT64_MAX` crash, but the
> threshold rule `u64 ≤ INT64_MAX → int64; otherwise decimal` must now be
> removed. Every `u64` mixed with a non-sized number enters `integer`,
> regardless of magnitude; see `Lambda_Impl_Numbers.md`.

### 1.1 Verified failure surface (probed 2026-07-16, re-verified 2026-07-20, `m = 18446744073709551615u64`)

| Expression | Today | Mathematically |
|---|---|---|
| `m * 1` | `-1` | 18446744073709551615 |
| `m + 0.5` | `-0.5` | ≈1.8446744e19 |
| `m div 2` | `0` | 9223372036854775807 |
| `m % 7` | `-1` | `1` |

Already correct (must not regress): same-width u64 arithmetic
(`m + 1u64` → `0u64` wrap), unary negation (`-m` → `1u64`), comparisons
(`m > 1` → true, `m < 0` → false, `-1 == m` → false), printing, `shr`/bitwise,
and the decimal conversion path.

**Crash row (found 2026-07-20; raises Item 1 from correctness fix to crash
fix):** a `u64` valued *exactly* `INT64_MAX` (`9223372036854775807u64`)
**segfaults** in any untyped-mixed arithmetic — `k + 0`, `k * 1`, `k + 0.5`
all die with exit 139. One below the boundary folds fine; one above hits the
wrong-value rows in the table. Cause: `INT64_ERROR` is `#define`d as
`INT64_MAX` (`lambda/lambda.h`), so `push_l(INT64_MAX)` returns `ItemError`
(`lambda/lambda-mem.cpp`), and `normalize_sized()` never checks — the ladder
consumes `ItemError` tagged as `INT64` and dereferences garbage. (Plain
`i64` arithmetic reaching `INT64_MAX` errors cleanly, exit 1 — same
sentinel, but there the error is a propagated *result*, not a consumed
*operand*.) Same-width `u64` ops, comparisons, and printing at this value
are unaffected.

**Root cause:** `normalize_sized()` (`lambda/lambda-eval-num.cpp`) folds
`UINT64` into the ordinary arithmetic ladders by signed reinterpretation —
`push_l((int64_t)item.get_uint64())` — before any `INT64`/`FLOAT`/`DECIMAL`
ladder runs. For values above `INT64_MAX` this changes the mathematical
value. Only untyped-mixed expressions reach this path; the sized ladders
(`SizedIntegerValue.is_unsigned`) and the comparison paths handle `UINT64`
directly and are value-correct.

### 1.2 Decision D1 — semantic model for the fold

**Historical decision:** option B was selected and implemented on 2026-07-20,
then superseded by the final number-model decision recorded in
`Lambda_Semantics_Number_Model.md` §3.3.2.

Options considered:

- **(A) Reject:** error when a `u64 > INT64_MAX` meets an untyped operand.
  Safe but value-dependent erroring (same expression works for small u64) —
  poor ergonomics, and inconsistent with comparisons, which already succeed
  value-correctly.
- **(B) Value-preserving promotion (RECOMMENDED):** `u64 ≤ INT64_MAX`
  normalizes to `int64` exactly as today (mathematically exact); above that,
  normalize to **decimal** (unlimited/BigInt-family context — u64 is
  integral). Rationale: ordinary Lambda arithmetic operates on mathematical
  values with rank promotion (`int → int64 → decimal`, per the number
  model); the comparison paths already uphold value semantics for `u64`, so
  arithmetic folding to a *different value* is incoherent. The machinery
  exists: `decimal_item_to_mpd()` (`lambda/lambda-decimal.cpp`) already
  converts `UINT64` via `mpd_set_u64`, and every ordinary ladder already has
  a `DECIMAL` arm.
- **(C) Documented reinterpretation:** cheapest; keeps today's behavior as
  spec. Rejected in recommendation: it enshrines `m * 1 ≠ m` while
  `m == m` and `m > 1` hold — an equality/arithmetic split no user can
  predict.

Consequences of B to document with the change: `m * 1` yields a *decimal*
(BigInt-family), not a u64 — consistent with "untyped arithmetic exits the
fixed-width domain" (same as `i8 + 1` behavior through promotion). Mixed
float (`m + 0.5`) follows the ladder's decimal/float rule (float embeds into
decimal per number model v2).

### 1.3 Stages

**P1.0 — Confirm D1** — **DONE 2026-07-20.** B confirmed by the user;
recorded in `Lambda_Type_Int_Sized.md` (§1 decision 8, §7 item 1 flipped to
decided/implementation-pending).

**P1.1 — Audit all `LMD_TYPE_UINT64` consumption sites — DONE.** Grep scope: all
of `lambda/` **including `lambda/js/`** (verified 2026-07-20:
`js_globals.cpp`, `js_runtime_value.cpp`, `js_runtime_internal.hpp` touch
UINT64 on the BigUint64Array egress path — classify rather than exclude;
expected *value-preserving*). Known core files: `lambda-eval-num.cpp`
(~19 sites: normalize, sized ladders, comparisons, unary, builtins switch),
`lambda-eval.cpp`, `lambda-data.cpp`, `lambda-data-runtime.cpp`,
`lambda-decimal.cpp`, `print.cpp`, `emit_sexpr.cpp`, `build_ast.cpp`,
`transpile-mir.cpp`, `transpile-call.cpp`, `validator/`,
`module/radiant/radiant_module.cpp`. Classify each site:
*value-preserving* (ok) / *width-domain op* (sized ladder — ok) /
*signed-reinterpreting* (fix alongside `normalize_sized`). Deliverable: a
short classification table appended to this doc. **Dedup flag (CLAUDE rule
13):** three identical `heap_calloc(sizeof(uint64_t), LMD_TYPE_UINT64)`
blocks exist — `push_u64` (`lambda-eval-num.cpp`), the `ELEM_UINT64`
typed-array read and `coerce_uint64` (both `lambda-data-runtime.cpp`);
P0.1 replaces all three with the shared transient `UINT64` number-home boxer.
P1.1 classified `normalize_sized()`, `fn_int()`, and `fn_decimal()` as the
ordinary-arithmetic value-preserving folds; `decimal_from_uint64()` and
`decimal_idiv()` as exact decimal preservation; sized arithmetic/bitwise and
typed storage as intentional width-domain operations; and the remaining raw
`uint64_t` uses as destination backing or native ABI transport. No default
heap-backed transient producer remains.

**P1.2 — Implement — DONE.** In `normalize_sized()`'s `UINT64` arm:

```text
v = item.get_uint64()
if v <= INT64_MAX:  item = box_int64_value((int64_t)v); type = INT64
else:               item = <decimal Item from u64, unlimited ctx>; type = DECIMAL
```

**Amended 2026-07-20: `box_int64_value`, NOT `push_l`.** Under P0.1,
`box_int64_value()` is the canonical full-domain number-home producer.
`push_l()` currently returns `ItemError` for `INT64_MAX` (the §1.1 crash row),
so legacy sentinel-bearing native-result adapters must remain separate from
ordinary value boxing. Below the sentinel the current encodings are
bit-identical, so the `≤ INT64_MAX` semantic arm changes behavior only at
exactly `INT64_MAX` (crash → correct value); after P0.1 all magnitudes use the
same number-home representation. Reuse the existing u64→mpd conversion
(`decimal_item_to_mpd` / `mpd_set_u64` + `decimal_push_result`); do not add a
second u64→decimal encoder. Apply the same guard to any other reinterpreting
sites found in P1.1, checking each for the same sentinel hazard. Use the shared
P0.1 `UINT64` boxer for values that stay unsigned; do not restore any of the
three heap allocation copies. Add the root-cause comment at the fix point
(CLAUDE rule 12).

**P1.3 — Tests — DONE.** `test/lambda/sized_numeric_u64_mixed.ls` + `.txt`
golden (CLAUDE rule 8): the four failure rows above, boundary values
(`9223372036854775807u64` and `9223372036854775808u64` mixed with int/float —
the former rows are **crash-regression pins**: today they segfault),
`≤ INT64_MAX` cases proving the int64 fold is unchanged, and re-assertions of
the already-correct surface (same-width wrap, `-m`, comparisons) to pin
against regression.

**Gates:** focused gtest for the new script + existing sized-numeric tests +
the Item 0 representation/GC gates + `make test-lambda-baseline` 100%.

### 1.4 Risks

- **Behavior change is confined to the currently-garbage path**: only
  `u64 > INT64_MAX` mixed with untyped operands changes, and today those
  results are wrong values, not depended-upon semantics. `≤ INT64_MAX` is
  bit-identical except exactly `INT64_MAX`, which today segfaults — any
  change there is an improvement.
- **Result-type surprise**: expressions gain a decimal result where they had
  a (wrong) int64. Documented in D1; goldens make it explicit.
- **Perf**: one unsigned compare on the `UINT64` normalize arm — cold path
  (u64 is a carrier type), negligible.

---

## Item 2 — `ushr` (JS `>>>` behavior) as a builtin

### 2.1 Decision D2 — semantics (implemented)

Builtin only — **no `>>>` grammar token** (avoids parser churn; Lambda
bitwise is already builtin-based). Name: `ushr(a, n)`.

| Operand | Behavior | Result type |
|---|---|---|
| `u8/u16/u32/u64` | identical to `shr` (already logical) | operand type |
| `i8/i16/i32/i64` | reinterpret bits in the unsigned counterpart width, logical shift | unsigned counterpart (`i32` → `u32`, …) |
| plain `int` | JS alignment: ToUint32 (reinterpret low 32 bits), logical shift | `u32` |

Reference behavior: `ushr(-1i32, 1)` → `2147483647u32` (JS `-1 >>> 1`);
`ushr(8, 1)` → `4u32`. A negative count is an error and a count at least the
operand width returns zero, matching the existing `shr` width-edge behavior.

### 2.2 Stages

**P2.0 — Confirm D2 — DONE**, especially the plain-`int` → `u32` row (the
alternative — operating in u64 — diverges from JS `>>>` and is rejected in
the recommendation since JS porting is the entire motivation).

**P2.1 — Implement — DONE.** Register `ushr` wherever `shr` lives — verified
2026-07-20: a sibling registry row beside `SYSFUNC_SHR` in
`lambda/sys_func_registry.c`, a runtime helper beside `fn_shr`/`fn_shr_item`
(`lambda-eval-num.cpp`), and the typed MIR Direct lowering. Implementation
is a thin composition: unsigned-reinterpret the left operand's width, then
delegate to the existing typed logical-shift path — no new shift engine.
Keep the MIR lowering symmetric with `shr`'s compact-operand handling so
`NUM_SIZED` operands don't collapse (the historical `emit_unbox(..., INT)`
bug class).

**P2.2 — Tests — DONE.** `test/lambda/sized_numeric_ushr.ls` + golden: the table
rows above, `u64` max value, shift-by-0 and width-edge counts, mixed
compact/plain operands, and a JS cross-check comment row
(`node -e "console.log(-1 >>> 1)"`).

**P2.3 — Docs — DONE.** Add to `doc/Lambda_Sys_Func.md`; update
`Lambda_Type_Int_Sized.md` §3 (bitwise table + drop §7 item 2).

**Gates:** focused gtest + `make test-lambda-baseline` 100%.

### 2.3 Priority

The optional JS-porting convenience was accepted in this round and is now part
of the documented bitwise surface. It remains a builtin, not grammar syntax.

---

## Sequencing summary

1. **DONE:** inventory and prepare generated return, destination-storage, and
   ownerless-fallback consumers before enabling full-domain transient homes.
2. **DONE:** atomically enable `INT64`/`UINT64` homes, then remove incompatible
   inline and transient-heap paths.
3. **DONE:** cut datetime over to GC objects and remove its scalar-home lane.
4. **DONE historically; superseded:** the D1 threshold fold landed with its
   regression suite and is now replacement scope in `Lambda_Impl_Numbers.md`.
5. **DONE:** land `ushr`, tests, and user-facing documentation.
6. **DONE:** synchronize the design records; final command results belong in
   §0.4 below.
