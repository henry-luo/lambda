# Lambda Return-Value Convention v3 — Companion-Lane Wide Scalars and Native `T^E` Lanes

> **Status: SHIPPING as of 2026-08-15.** `LAMBDA_RETURN_V3` defaults to **1**;
> the cutover was gated by running the same tree both ways — 3721 tests,
> 3715 passed, **identical failure lists**, all six failures reproduced on a
> clean tree. v2 stays buildable with `-DLAMBDA_RETURN_V3=0` until P5 deletes
> its machinery. *(Decided 2026-08-14 by user ruling; P0/P1.1/P1.2/P1.3
> implemented that day behind the flag.)* Impl log, deviations and gate results:
> [`Lambda_Impl_Return_Value.md`] §5. Sections marked
> **[measured 2026-08-14]** below were revised from proposal fidelity to
> observed behaviour once shape 2 was emitting; §1.4 corrects §1.2's site
> split, §8 is re-costed against it, and RVO10 is new.
> Ledger **RV1–RV18** (+ addenda RV3a, RV10a, RV14a, RV17a) + open issues
> **RVO1–RVO12**. **RV17/RV17a** (ruled 2026-08-15, §2.1) are what make
> **shape 4 reachable from ordinary source**: an `if` whose arms differ joins
> to `T | error` — the union — instead of widening to ANY, so a `T^E` body
> finally names its value component and can be admitted to the native lane.
> Landed for `T = float` (impl log §5). **RV18** (same day) makes the body
> producer publish its carrier so the boundary stops inferring one. **RV14/RV15/RV16** (ruled 2026-08-14, §4a) settle the C
> boundary: a C helper or sys func returns a wide scalar by pushing it on the
> number stack and returning an ordinary Item, because it owns no watermark
> to tear down; the eager per-call restore that made that unsafe is retired
> in favour of an on-demand loop-end reclaim, which a wide-capable mutable
> local's own number slot makes safe for loop-carried values. The trailing-scalar-home
> ABI is retired in favor of the companion-lane convention; the formal spec
> is revised (**D5.2.1v2**, **D2.7.2v2**, **D8.4.2v2**, spec v1.20.0), with
> the retired-ABI record in its Appendix A. The v1 ABI remains the shipping
> mechanism until migration P4 (§9); mechanism details below stay at
> proposal fidelity until then. The 2026-08-14 rulings on Windows lowering,
> LambdaJS scope, DTIME, and the error-lane encoding are folded into
> **RV12**, **RV13**, **RV8**, and **RV9** (RVO1/RVO2/RVO5/RVO6/RVO7
> retired); RVO3, RVO4, RVO8, and RVO9 remain open; RVO12 opened and CLOSED 2026-08-15 (§10).
> This design **revives SF14's original two-lane return** (implemented
> 2026-07-15, superseded 2026-07-16) with new measured evidence and a new
> pending-Item encoding that removes the defect that killed the first
> attempt. It conforms to
> **D2.8.1** (error-free lane invariant) and **D2.7.1** (no standalone scalar
> cell in the GC heap), and extends the ValueRep doctrine of
> [`Lambda_Design_Compiling_Lane.md`].
> Related: [`Lambda_Design_Stack_Frame.md`] (SF14 both versions),
> [`Lambda_Design_Compiling.md`] (LC1, call-ABI),
> [`Lambda_Design_Compiling_Dual_Func.md`] (DF8/DF9 entry equivalence),
> [`Lambda_Design_Type_Enforcement.md`] (TE-17 native-lane gating),
> [`Lambda_Semantics_Int_Type.md`] (v5 §5.8 sentinel-collision gate),
> [`Lambda_Type_Double_Boxing.md`] (§2.5 out-of-band residue, new Part 8).

---

## 0. Summary

Function returns get **per-signature shapes, chosen statically, never more
than two lanes**:

| # | Signature | Shape | Lane 2 |
|---|-----------|-------|--------|
| 1 | boxed return, provably wide-free | `[item]` | — |
| 2 | boxed return, may carry a wide scalar | `[item, scalar]` | raw 64-bit payload, live iff lane 1 is a pending Item |
| 3 | native return, infallible (TE-17) | `[native]` | — |
| 4 | native return, `^E` | `[native, error]` | error Item (`ItemNull` when no error) |

Shape 4 has a **typing precondition** as well as a signature one: the body
must name its value component, which for the canonical guarded-`raise` body
means the branch join must produce `T | error` rather than ANY (**RV17**,
§2.1). Under ANY the body proves no native carrier and the whole function
deoptimizes to boxed — shape 4 unreachable from ordinary source.

These are the shapes for entries that **own a number-frame watermark** —
Lambda `fn`/`pn`. A C helper or sys func owns none, so it needs no shape at
all: it pushes a wide scalar on the number stack and returns an ordinary
Item, which is already caller-homed (RV14, §4a). One rule decides which side
an entry falls on, and it is about watermark ownership, not language.

Two goals: **(a)** wide scalars return *inline* in a register instead of
through a caller-donated memory home, and **(b)** typed functions return in
native lanes, with `^E` carried on a second lane instead of deoptimizing to
boxed ANY or stealing domain values (`INT64_ERROR`).

The deeper principle: **classification moves from boundaries to birth
sites**. Today every boxed return dynamically classifies its result
(frame-backed or not?) and every adoption re-classifies; under v3 the only
code that knows a value is wide is the arithmetic that created it — and it
knows statically. Everything downstream forwards two registers for free.

---

## 1. History and problem statement

### 1.1 The SF14 arc

- **SF14 v1 (2026-07-15)** designed exactly shapes 1–4 above, on verified
  MIR facts (multi-value returns ride registers for MIR→MIR calls; x86-64
  caps at 2 int results, so a universal 3-lane `[item, scalar, error]` is
  impossible — and unnecessary, because `T^E` is a *sum*: the Item tag
  already discriminates value / wide-marker / error).
- **SF14 v2 (2026-07-16)** superseded it: *"the portable ARM64 handoff and
  two imported helpers made every boxed return pay for a rare
  representation."* The landed replacement classifies the returned Item
  inline and copies wide payloads into a donated slot — which **D5.2.1**
  then generalized into caller-donated canonical homes (hidden trailing ABI
  operand, liveness-colored slots, discard homes, tail-forwarding), recorded
  in the ABI as **D8.4.2**.

The v1 failure was an **implementation** defect (helper calls and a scratch
workaround on the universal return path), not a design defect. v3 removes
its cause: the pending-Item tag (§3) makes lane 2 tag-discriminated, so the
fast path emits *nothing* — no helper, no handoff, no dummy classify.

### 1.2 The 2026-08-14 census — what v2 actually costs

Method: debug-build MIR dumps (`temp/mir_dump.txt`) of 14 untyped + 8 typed
AWFY benchmarks; the emitter comment at `mir_emitter_shared.hpp`
(`em_adopt_scalar_item_value`) guarantees emitted MIR is identical in
release builds. Counted: the adopt classify-and-rehome cluster (measured at
exactly **20 instructions per site**: tag extract, 4 type compares, ±0 and
inline-double bit tests, branches, passthrough/call arms), home-address
materializations (`add home, number_base, slot`), and the trailing
`_scalar_home` parameter/argument.

| bench | MIR insns | adopt sites | home insns | share |
|---|---|---|---|---|
| nbody | 3,623 | 15 | 313 | 8.6% |
| bounce | 1,731 | 17 | 359 | 20.7% |
| sieve | 699 | 10 | 209 | 29.9% |
| havlak | 20,470 | 292 | 6,246 | 30.5% |
| richards | 10,209 | 156 | 3,290 | 32.2% |
| json | 11,852 | 183 | 3,862 | 32.6% |
| storage | 1,334 | 22 | 464 | 34.8% |
| deltablue | 22,639 | 413 | 8,763 | **38.7%** |

Key findings:

- **9–39% of all emitted MIR (geo-mean ≈ 25%) is home protocol.** The
  adopt clusters sit in the hot bodies, not wrappers (deltablue's
  `_c_recalculate`: 53 clusters in 2,065 instructions).
- **Dynamic cost on the fast path is ~11–16 executed instructions per
  boxed return** even when the result is a packed small int — the ritual is
  paid whether or not a wide scalar occurs, and in these workloads it
  essentially never occurs. This is the complaint in one number.
- **Typed annotation changes nothing**: richards2 has the same 156 sites as
  richards, deltablue2 414 vs 413. The v2 protocol is signature-blind.
- **Site split**: return-side adopts (into the donated caller home; deleted
  outright by shape 2) are 30–60% of sites — havlak 115/292, deltablue
  83/413, cd2_orig 80/132. The rest adopt call results into local homes,
  which shape 2 converts to "patch only on escape" (§4).
- **70–100% of functions** carry the trailing `_scalar_home` parameter
  (havlak: 115 of 119) — one argument register per call, everywhere.

### 1.3 Diagnosis

D5.2.1's design bounds *space* by peak liveness — which it achieves — but
bounds *instructions* by nothing: because the engine cannot prove most
returns wide-free, nearly every function pays setup, classify, store, and
check for a representation that occurs in ~1% of dynamic returns. The tax
is per-boundary and dynamic; v3 makes it per-birth and static.

### 1.4 What the census actually contained — **[measured 2026-08-14]**

§1.2's last two bullets were written before shape 2 existed and split the
adopt population in two. Emitting v3 and re-running the analyzer over the
same benchmarks shows it is **three** populations, and only the first is
addressed by this design. Method: `utils/analyze_scalar_homes.py` over
`--transpile-only` dumps, flag off vs flag on, plus a per-entry-kind
breakdown. The v2 column reproduces §1.2's table exactly.

| bench | return-side, internal bodies | return-side, public `_b` wrappers | helper-call | v2 total | v3 total |
|---|---|---|---|---|---|
| nbody | 2 | 5 | 8 | 15 | 13 |
| bounce | 1 | 3 | 13 | 17 | 16 |
| sieve | 1 | 3 | 6 | 10 | 9 |
| storage | 3 | 4 | 15 | 22 | 19 |
| richards | 21 | 28 | 107 | 156 | 135 |
| json | 35 | 47 | 101 | 183 | 148 |
| havlak | 56 | 59 | 177 | 292 | 236 |
| deltablue | 34 | 49 | 330 | 413 | 379 |
| **total** | **153 (14%)** | **198 (18%)** | **757 (68%)** | **1108** | **955** |

The accounting is exact, not approximate: every entry carrying a
`_scalar_home` parameter holds exactly one return-side adopt, so
`v3 adopts == helper-call adopts + one per surviving home-param entry`
holds for all eight benchmarks with no residual.

1. **Return-side adopts in internal bodies (14%) — deleted, as designed.**
   Every internal body lost its `_scalar_home` parameter (351 → 198
   home-param entries, and the surviving 198 are precisely the public
   wrappers) and its epilogue adopt.
2. **Return-side adopts in public `_b` wrappers (18%) — deferred, not
   deleted.** These are the entries reachable from C (the
   `fn_call_boxed_N_into` trampolines and `fn->invoke`), and a C prototype
   cannot receive two MIR results on *any* platform. See RV12 below: the
   context-slot transport, not the register pair, is what unlocks these.
3. **Helper-call adopts (68%) — a different problem, resolved by §4a.** §1.2
   said "the rest adopt call results into local homes, which shape 2 converts
   to 'patch only on escape'". That is **not** what they are. They sit in
   `em_call_import`, after calls to C helpers (`fn_member`,
   `lambda_name_id_to_item`, `pn_print`, …), and RV12 already states the
   pair protocol is JIT⇄JIT — so shape 2 cannot convert them. Their cause is
   unrelated to the return ABI: the caller snapshots `side_number_top`
   before the helper call and restores it immediately after, so a payload
   the helper homed in the caller's own extent would be reclaimed; the adopt
   copies it somewhere stable first. **RV14 rules the restore away instead of
   the payload** — a C helper owns no watermark, so its push is already
   caller-homed and needs no rescue. (Was RVO10; superseded by RV14/RV15,
   with the surviving question recorded as RVO11.)

   Worth recording why an audit-only fix does **not** reach this population,
   because it looks like it should: the adopt is gated by a conservative
   opt-out flag (`RESULT_SCALAR_STABLE`), so marking helpers stable appears
   to dissolve it. It does not. Verified by reading the sources, these
   genuinely allocate on the number stack — `box_int64_value`,
   `box_uint64_value`, `box_int64_result_or_error`, `push_d_safe`,
   `js_profiled_push_d` (boxing *is* their job), and, the common case,
   everything that materializes a wide scalar out of non-Item storage:
   `array_num_read_item` (`ELEM_INT64` → `box_int64_value`, `ELEM_FLOAT64`
   → `push_d`) and `scalar_storage_read` ("re-home its interior scalar
   references in the current number frame before escape"), reached through
   `owned_item_slot_read`, `js_get`, `fn_string_ascii_at`. A wide scalar read
   out of storage has nowhere to live but a number home — that is what the
   number stack is *for*. Only `int2it`/`int2it_i64` (v5 `int` boxes inline)
   and `lambda_name_id_to_item` (interned name-pool symbols) are genuinely
   push-free. The population is real; the transport is what changes.

The trailing home *argument* at call sites did drop sharply, which is the
per-call argument-register win §8 claims: havlak 229 → 36 sites, json
101 → 2, richards 63 → 1, deltablue 173 → 79 (deltablue lags because so many
of its calls target public wrappers).

Net static effect of P1 as landed: 9–39% → 7.5–35.4% of emitted MIR. The
design's larger number is reached by two further mechanisms, each matched to
who owns a watermark: population 2 by the RV12 slot transport for C-reachable
entries, population 3 by RV14/RV15. Neither is in §2–§6 — shape 2 alone was
never going to deliver them.

---

## 2. The convention

**RV1 — Four shapes, two lanes maximum.** As in §0. The 3-lane variant
stays rejected (x86-64 2-int-result cap; `T^E` sum argument, both inherited
from SF14 v1's analysis). Shape-2 returns whose value is not wide write a
dummy lane-2 operand (MIR `ret` requires all results; typically
move-eliminated — SF14 v1 residual note).

**RV2 — Shape selection is static, signature-derived, and fixpoint-free.**
The shape is a pure function of the *declared* return type (plus TE-17
infallibility for shape 3 vs 4). It is never inferred per call site, never
read back from MIR state (Compiling-Lane rule), and never depends on
recursive inference outcomes — the R26/C7 lesson (per-call checks appearing
on untyped recursion) is the cautionary precedent. Untyped/ANY returns that
cannot be proven wide-free take shape 2; that is the *universal* shape every
dynamic call site may assume (§6).

**RV13 — Scope: Lambda and LambdaJS.** *(ruled 2026-08-14)* LambdaJS
adopts the same convention — it shares the return-emitter modes and the
inline-double encoding, and JS numbers make shape 2 *more* frequent there,
so the census argument is stronger, not weaker. Sequencing (whether LJS
migrates inside P1/P2 or as a follow-on slice) is an implementation-plan
detail, not a design question.

### 2.1 What makes shape 4 *reachable* — the branch-union join

**RV17 — A `T^E` body reaches the native lane only if its type names the
value component; the if-join must therefore produce `T | error`, not ANY.**
*(ruled 2026-08-15, user; implemented same day for `T = float`)*

RV2 settles shape selection from the **signature**: `fn f(x: float) float^`
declares a native return and `can_raise`, so the signature asks for shape 4.
That was never the blocker. The blocker was on the **body** side, and it was
a *typing* defect, not a codegen one.

The canonical `^E` body is a guarded branch:

```lambda
fn half(x: float) float^ {
    if (x < 0.0) { raise error("negative") } else { x / 2.0 }
}
```

The arms differ, so the if-join widened to `TYPE_ANY`. ANY is not a native
carrier, so the body could not prove a native return lane, so admission
deoptimized the whole function to a boxed Item — **and shape 4 became
unreachable from ordinary user source.** Goal (b) of §0 ("typed functions
return in native lanes, with `^E` carried on a second lane instead of
deoptimizing to boxed ANY") was defeated before codegen was consulted,
because the type system had already thrown away the fact that made it true.

**The join is the fix, and the union is the only join that works.** Each arm
*contributes* a type; a diverging (raise) arm contributes `error`, since it
never yields a value. Differing contributions join as their **union**
(`|` is union everywhere — **S10.1.1**), so the body types as `float | error`.

That type is **shape 4 spelled as a type**:

| union component | lane |
|---|---|
| `float` — the value component | lane 1, native `d` |
| `error` — the raise arms | lane 2, error Item |

which is why the union is not merely *a* precise join but the *right* one
here. **D2.8.1** ("no native lane slot ever holds an error") is the rule that
forces `T^E` to be split across two lanes at all; `T | error` is the static
statement of exactly that split, in the type system, at the point where the
branches meet. Under ANY the compiler had to rediscover the split at the
return boundary with no evidence; under the union it is carried there.

**Why neither neighbouring option qualifies** — both were implemented,
measured, and reverted before this ruling (impl log §5):

- **ANY** conflates *"may be an error"* with *"may be anything"*. It preserves
  error-ness (which is why the boxed path stayed correct) but says nothing
  about the value component, so lane 1 has no declared carrier.
- **Bare `T`** (narrowing to the non-raising arm) names the value component
  but **destroys the error signal**, so `^`/`^err` consumers stop seeing a
  failure that can still happen. Observed as `[nan, null]` — the error arm
  reaching `it2d`.

Only the union carries **both** facts, which is precisely what a two-lane
return needs: one fact per lane.

**Scope of the ruling.** The union fires only when an arm **diverges**. The
general `T1 | T2` join for merely-differing arms is *more precise* but shifts
the **E208 containment surface** — programs that let an error escape
uncontained start failing to compile (measured: 95 corpus tests) — which is a
language-surface decision, not a typing cleanup, and is deliberately not taken
here. Native admission is likewise **float-only** today: the `d` lane has a
universal `it2d` fixup for a boxed value arm, while the INT-family lanes have
no way to *tell* a boxed Item from a native `int64` at the return boundary —
both are `MIR_T_I64`, and the fixup's trigger was a register-class test.
**RV18** supplied the missing discriminator (the producer publishes its
carrier) and the RVO12 closure removed the last consumer defect, so
`int^`/`i64^` raise-arm bodies now return natively as well — all three lanes
emit shape 4 from ordinary source.

**RV17a — A can-raise call's result register is a boxed join, whatever its
declared type says; consumers must be told.** *(corollary, ruled with RV17)*

Shape 4 returns two lanes, but the call site immediately merges them into one
boxed value-or-error Item — that Item *is* what `^`, `^err`, and `or` split
on. So between the merge and the consumer, the register holds a **boxed
join** while the call's declared type still names the **success scalar**. A
consumer that trusts the declared type re-boxes the join through the scalar
lane and sends the error arm through `it2d` — the same `nan` as the bare-`T`
failure above, now arriving by a different road.

This is the same class of carrier lie the emitter already publishes for
inferred slow-body routes, so it takes the same channel rather than a new
one: **an error-lane merge publishes "returned boxed item", and boxing passes
a published join through untouched.** Explicit propagation (`f(...)^`) is
excluded — it consumes the error *at the call site*, so from there on the
declared scalar type is truthful, which is how every other reopen witness
already treats propagation.

The general rule, stated once: **a lane merge changes the carrier, so it must
publish the carrier.** Shape 4 is the first construct where an ordinary typed
call produces one, which is why the lie surfaced only when RV17 made shape 4
reachable.

**RV18 — The body producer publishes its carrier; the return boundary
consumes that fact instead of inferring one.** *(ruled 2026-08-15, user —
"option (a) for the fn tail return"; IMPLEMENTED same day)*

Implemented for the `fn` tail return:

- `MirTranspiler.body_tail_rep` (a `ValueRep`) generalizes the one-bit
  `native_body_result_is_raw` that already existed for two int paths — same
  idea, now saying *which* carrier rather than only "raw".
- `transpile_content_tail_value` publishes `VALUE_REP_ITEM` on its
  remaining path, which is a fact rather than an inspection: that path
  returns through `transpile_box_item`, the boxing funnel. This is the
  canonical braced-`fn` body, i.e. exactly what the syntactic INT proxy was
  approximating.
- `transpile_if` had the **carrier bug at its source**: `need_boxing`
  compares `type_to_mir` register classes, and since that collapses every
  non-float type to I64, `int | error` compared I64 == I64 and concluded
  "no boxing" — while both arms do store Items. A structured result
  (`if_tid == LMD_TYPE_TYPE`) now forces boxing, so the merge register's
  carrier matches what the arms actually write.
- The boundary consumes `VALUE_REP_ITEM` with **one** rule — unbox to the
  declared return lane, whatever that lane is — replacing all three proxies
  for published producers. `VALUE_REP_NONE` (not published) keeps the legacy
  cascade, so adoption is incremental and **cannot regress** a producer that
  has not opted in.

Gates identical in both flag states — 3718/3721, where the 3 failures are
`test_mir_gc_stress_gtest` JS forced-GC divergences that a **clean tree
reproduces exactly** (verified by stashing the whole working set and
rebuilding); they are instances of the known unrooted-native-locals issue,
not of this change. The float shape-4 path is unchanged.

---

## 3. Pending Items

**RV3 — Encoding: one reserved tag byte, `0x1E`, kind in the payload.**

```
pending item = (ITEM_PENDING_TAG << 56) | kind      // ITEM_PENDING_TAG = 0x1E
kind: 0 = INT64, 1 = UINT64, 2 = FLOAT (out-of-band double bits), 3 = reserved (DTIME ruled in-band — RV8)
```

- `0x1E` is the last free byte of the `000` octant (TypeIds through 0x1B,
  COUNT/HEAP_START 0x1C–0x1D, `ITEM_SENTINEL_TAG` 0x1F). Spending it is a
  deliberate exhaustion, recorded so the next tag-byte consumer knows the
  headroom now lives in the reserved `100` octant (Double_Boxing Part 8
  keeps that octant unoccupied on purpose).
- **The discriminator is 2 instructions, deterministic**:
  `and t, item, HIGH_BYTE_MASK; beq L_pending, t, 0x1E<<56`. (The
  `100`-octant sign-test alternative was 1 instruction but false-positived
  on every negative inline double; rejected.)
- Sharing 0x1F with the sentinel family was rejected: JS deleted/iter-done
  sentinels legitimately get **stored** (sparse arrays), so a high-byte-0x1F
  test would false-positive on them and force full-word compares.
- **Guard properties for free**: `0x1E ≥ LMD_TYPE_COUNT`, so `type_id()` on
  a pending Item lands outside the valid TypeId range and every existing
  bounds assert fires — a leaked pending Item dies loudly at first touch.
  Add static asserts in the `ITEM_SENTINEL_TAG` style: non-double,
  not-inline-int, `!= ITEM_SENTINEL_TAG`.

**RV3a — The wide-tag run is part of the encoding contract.**
*(**[measured 2026-08-14]**, promoted from an implementation detail because
it is a constraint on `EnumTypeId`, not a code choice.)*

Building the pair — the *other* half of the protocol, and the one §1.3 says
is the real cost — turned out substantially cheaper than the "reduced
classify" this design anticipated, because the four wide tags are
**contiguous**:

```
LMD_TYPE_INT64 = 6, UINT64 = 7, FLOAT = 8, FLOAT64 = 9
off  = tag - LMD_TYPE_INT64
if ((unsigned)off > 3) -> not wide            // one sub + one unsigned branch
```

Three consequences, all now pinned by static asserts in `lambda.h` because
each is silently broken by reordering the enum:

1. **One branch rejects everything non-wide** — packed ints, bools, nulls,
   strings, containers, raw pointers and inline doubles alike. The v2
   cluster needed four `EQ`s, two `OR`s and three branches to reach the same
   conclusion.
2. **`off` *is* the pending kind** for the two integer tags
   (`INT64 → 0`, `UINT64 → 1` = `PENDING_KIND_INT64/UINT64`), so the wide-int
   arm computes no kind at all.
3. **Tags `0x06`–`0x09` have the double-discriminator bits clear**, so the
   wide arm needs no `ITEM_DBL_MASK` test — an inline double can never land
   in the run. Only the packed ±0 encodings still need their guard, and that
   is one unsigned compare (`payload_ptr <= 1`, the same rule
   `Item::get_double()` uses).

Measured on emitted MIR: **12 instructions static, 5 on the fast path**
(`mov`, `mov`, `ursh`, `sub`, `ubgt`), against v2's 20 static / 11–16
dynamic *plus* a home materialization, a home store and an ABI parameter.
The `and`/`bne` resolution test on the caller side is the predicted 2
instructions, confirmed in the dump.

This is why RV7's birth-site classification is an optimization rather than a
prerequisite: even the fully dynamic form is cheap enough that the epilogue
can carry it, which is what P1 shipped.

**RV4 — The two protocol invariants.**

1. **A pending Item never lives in memory.** It exists only in registers,
   between a call returning and the result's first resolution point
   (consume / patch / forward). Corollary: *only direct call results can
   ever be pending* — every value loaded from a variable, container, field,
   or constant is proven-resolved by construction, because the store that
   put it there was required to patch first. Maybe-pending tracking is
   therefore local and syntactic, not a dataflow problem.
2. **At most one pending value is live at any point.** Lane 2 is a single
   location clobbered by the next call, so the compiler tracks one
   "current pending candidate", never a set.

---

## 4. Resolution protocol

**RV5 — Site taxonomy.** Where the 2-instruction test is and is not needed:

| Site | Test? | Rationale |
|---|---|---|
| consume in arithmetic / compare / truthiness | **no** | the existing tag dispatch gains a pending arm that reads lane 2 instead of dereferencing a home pointer — zero marginal instructions |
| return (tail-forward) | **no** | forward both lanes unchanged; the case that costs a full adopt cluster today drops to zero |
| store to any GC-visible slot (variable, container element, field, root, push) | **yes** | this is where invariant RV4.1 is enforced: test, and on the rare pending arm allocate the destination-owned storage (D5.2.2 taxonomy) and patch |
| pass as argument | **yes** | arguments stay single-Item; the pair protocol is **return-only** — it never infects the argument ABI |
| any subsequent call while unresolved | **yes** (patch before the call) | lane 2 dies at the next call boundary; worst case is an eager patch right after return — 2 instructions + untaken branch |
| suspension points (`await`, generator `yield`) | **yes** (resolve before suspending) | suspension is a re-homing barrier (D5.1.3); the async spill tracker (`after_call_result` hook) must see only resolved Items |
| GC safepoint | **no** | a pending Item is tag bits only and lane 2 is raw non-pointer bits (RV8) — nothing to trace (DI6); and since patching precedes any call, no `MAY_GC` boundary (D5.3.2) can observe a live pending pair |
| anything loaded from memory | **no** | already resolved by RV4.1 |

**RV6 — ValueRep carries `maybe_pending`.** Per the Compiling-Lane rule
(facts live in ValueRep, never re-derived from MIR), the emitter tracks
pending-ness statically: results of shape-1/3/4 calls are born resolved;
shape-2 results are maybe-pending until a dominating test/dispatch/patch,
after which each arm is marked resolved. The dynamic test survives only at
genuine unknown-merge points. Combined with RV5, the expected end state for
deltablue-shaped code is a handful of single-branch tests where today there
are ~400 twenty-instruction clusters.

**RV7 — Classification at birth.** Only wide-producing operations
(int64/uint64 arithmetic and conversions, out-of-band double producers per
Double_Boxing §2.5) construct pending pairs, and they know statically. Wide
returns cost 2 movs; non-wide returns cost nothing; there is no dynamic
"am I wide?" question anywhere in generated code.

> **[measured 2026-08-14]** RV7 is an *optimization over* the protocol, not
> a precondition for it. P1 shipped the dynamic form — one pair build in the
> epilogue, per function — because RV3a makes it 5 fast-path instructions,
> already below the v2 cluster's 11–16 *and* free of the home store and ABI
> parameter. Moving classification to birth sites removes those 5; it does
> not change the ABI, the descriptor, or any resolution site, so it can land
> whenever the ValueRep wideness plumbing does.
>
> Likewise **RV6's lazy `maybe_pending` propagation is a refinement, not a
> correctness requirement.** P1 resolves eagerly, immediately after each
> pair-returning call. That satisfies RV4.2 trivially (nothing can be live
> across a second call), needs no dataflow, and costs the 2-instruction test
> plus an untaken branch — which RV5 already budgets as the worst case for
> the "subsequent call while unresolved" row. RV6 then converts those tests
> from *every* call to *escape sites only*; the consume/tail-forward rows of
> RV5 are what it buys.

---

## 4a. The C boundary — watermark ownership decides the transport

**RV14 — A C helper or sys func returns a wide scalar by pushing it on the
number stack and returning an ordinary Item that points there.** *(ruled
2026-08-14)* No adopt cluster, no caller-donated home, no companion lane.
The Item is a normal boxed wide scalar; every existing consumer already
handles it.

The rule underneath is **watermark ownership**, not the implementation
language:

| entry | owns a number-frame watermark? | wide return rides |
|---|---|---|
| C helper / sys func | **no** | the number stack directly (RV14) |
| Lambda `fn` / `pn` | **yes** — torn down at return | the companion lane (RV1 shape 2) |

A C helper establishes no watermark frame (SF6), so `box_int64_value` inside
it allocates in the **calling JIT frame's** extent and the returned Item stays
valid for that frame's whole activation. A `fn`/`pn` cannot do this: its own
epilogue restores the watermark, so a pointer into its extent is dangling by
the time the caller sees it — which is exactly why shape 2 exists.

Stating it by ownership rather than by language keeps it true for the cases
that blur: a sys func implemented in Lambda has a frame and takes the
companion lane; a C helper that ever grew a frame would too. The nesting
property that makes RV14 safe is the same one: a Lambda call made *after* the
helper sets its `number_base` above the helper's payload and restores only to
there, so the payload survives.

*What this replaces.* v1 made helper results pay the full ritual — snapshot
the watermark, materialize a colored home, run the 20-instruction classify,
copy the payload down, restore. §1.4 measures that at **68% of all adopt
sites** (757 of 1108), the single largest population in the census. RV14
deletes all of it. The classify existed only because of the eager restore
below; remove the restore and there is nothing to rescue.

**RV14a — Consult the watermark effect the audit already declares.**
*(implementation corollary of RV14; independent of it, and landable first.)*

The emitter records, per import, whether the callee leaves the number-stack
watermark where it found it:

```c
entry->call.effects.number_stack =
    entry->audit.flags & JIT_IMPORT_NUMBER_STACK_PRESERVES
    ? JIT_NUMBER_STACK_PRESERVES : JIT_NUMBER_STACK_MAY_ALLOCATE;
```

`JitCallEffects.number_stack` has exactly one writer and **no reader**. The
adopt decision instead consults only `scalar_class`, i.e. "boxed Item and not
marked `RESULT_SCALAR_STABLE`". So a helper that has *declared* it cannot
leave anything above the caller's pre-call top still gets the full ritual:
watermark snapshot, colored home, 20-instruction classify, restore.

The declaration is exactly the premise RV14 reasons from, so it discharges the
adopt on its own: if the top on return equals `source_base`, the region the
restore reclaims is empty, nothing the returned Item points at can be inside
it, and anything it points at that *is* on the number stack was allocated
before the call — below `source_base`, which the restore never touches.

Gate the adopt on `scalar_class != NONE` **and**
`number_stack != PRESERVES`. Thirty registry rows declare the flag; three of
them return boxed Items and so pay the ritual today. One is
`lambda_name_id_to_item`, which havlak calls 78 times — **1,560 instructions,
7.6% of that module** — for a helper whose body cannot allocate at all:

```c
extern "C" Item lambda_name_id_to_item(NameId name_id) {
    if (name_id == NAME_ID_NONE) return ItemNull;
    NameRef name = name_pool_resolve_id(context ? context->name_pool : NULL, name_id);
    return name ? (Item){.item = s2it(name)} : ItemNull;   // interned name-pool symbol
}
```

Two cautions. First, this converts a documentation claim into a correctness
dependency — a row that declares `PRESERVES` untruthfully becomes a
use-after-free rather than a comment that is merely wrong. Pair it with a
debug assert in `lambda_side_number_alloc` that fires when it is reached from
a helper declaring `PRESERVES`, which turns a silent lie into a loud one.
Second, the enum ordering is load-bearing: `MAY_ALLOCATE` must remain value 0
so an unaudited or zero-initialized row decodes conservatively.
(`Lambda_Design_Stack_API.md` §7.1 declared it the other way round and was
corrected 2026-08-14; the shipping header was always right.)

Note also that direct Lambda-to-Lambda calls hard-code `MAY_ALLOCATE`
regardless of callee, though a `fn`/`pn` restores its own extent at its
epilogue and therefore does preserve the watermark from the caller's view.
Harmless while nothing reads the field; worth correcting alongside the reader.

**RV15 — The eager per-call watermark restore is retired; reclaim moves to
loop end, on demand.** *(ruled 2026-08-14)* Today the caller snapshots
`side_number_top` before each helper call and restores it immediately after.
That restore is what invalidates RV14's payload, so it goes. Correctness does
not depend on it — the frame epilogue already restores to `number_base`, so
nothing escapes the activation either way. It is purely a **space** bound.

Removing it moves that bound from *peak liveness* (what the home coloring in
`em_finalize_scalar_homes` buys) to *total allocations in the frame*. Bounded
code is unaffected; a loop is not:

```
for i in 1 to 1_000_000 { sum = sum + arr[i] }   // untyped, arr is int64[]
```

Each `arr[i]` reaches `array_num_read_item` → `box_int64_value` → 8 bytes,
live to the end of the function. Typed code escapes through native lanes, but
untyped loops over `int64`/`float64` storage are precisely the shape that
pushes.

So the reclaim moves to the loop back edge, emitted **only when the compiler
believes the loop accumulates**: the emitter already knows, per loop body,
whether it emitted a call whose audited result class can be a wide scalar —
the same `scalar_class != SCALAR_RETURN_NONE` test that drives the v1 adopt.
When it did, the back edge restores the watermark to the loop-entry value.
One restore per iteration replaces a 20-instruction cluster per call, and the
bound becomes peak-per-iteration, which is what the coloring was buying.

**The safety condition is loop-carried wide values.** A back-edge reclaim
kills anything still living in the reclaimed region, so it is sound only when
no loop-carried wide value lives above the loop-entry watermark. RV16 answers
this for the case that matters; the residue is **RVO11**.

**RV16 — A wide-capable mutable local owns a number slot; assignment stores
into it.** *(ruled 2026-08-14)*

```
var acc = 0i64                    // one slot, allocated at the declaration
for … { acc = acc + arr[i] }      // assignment STORES into that slot
```

The binding gets its slot once and keeps it for its scope; assigning a wide
value copies the payload into that slot and retags, rather than pushing a
fresh home. This is the D5.2.2 move — the binding becomes **destination-owned
storage** — applied to locals.

*Why it makes RV15 sound.* The slot is allocated at the declaration, which
precedes the loop, so it sits **below** the loop-entry watermark and a
back-edge reclaim cannot reach it. Every per-iteration transient — `arr[i]`'s
push — sits above and is reclaimed. Loop-carried below, per-iteration above,
which is exactly the split the reclaim needs.

The sub-cases fall out rather than needing rules of their own: a `let` or
`var` declared *inside* the body binds once per iteration, so its slot is
above the watermark and dies with the iteration, which is correct. `let` is
immutable, so a loop accumulator is necessarily `var` — the mutable form is
the only one that needs the reuse.

*Cost.* Assignment becomes a store. For a statically wide binding
(`var acc = 0i64` infers int64) that is a plain store with no classify; only
an ANY-typed mutable binding needs the 2-instruction wide test and a rare
copy. One store per assignment replaces a 20-instruction cluster per helper
call. In typed code the accumulator uses a native i64 lane and never touches
the number stack at all — the slot exists only for the boxed path.

*What it costs to build.* This machinery does not exist yet.
`MirScalarHomeBinding` associates a MIR **register** with a colored home —
transient-value coloring, not source-binding ownership — and `BindingStorage`
(`REGISTER` / `SCOPE_ENV` / `MODULE` / `PERSISTENT`) has no consumer in the
emitter, so there is no per-binding storage notion to extend. It is new, but
small, and it *subsumes* rather than adds: per-binding slots plus a bulk
back-edge reclaim replace the interference/coloring pass in
`em_finalize_scalar_homes`.

---

## 5. Lane discipline

**RV8 — Lane 2 is one raw i64; never a pointer.** Doubles travel as bitcast
bits. DECIMAL, SYMBOL, STRING, and every GC/arena-owned scalar stays an
in-band pointer Item (never pending). **DTIME included** *(ruled
2026-08-14)*: datetimes remain pointer-based, GC-managed, in-band Items —
never pending; pending kind 3 stays reserved. (Verified in-tree: DTIME
already maps to `MIR_SCALAR_RETURN_NONE` in
`em_scalar_return_mode_for_type` — plain Item returns, no adopt mode — so
the ruling is already satisfied representationally; v3 changes nothing for
datetimes.) This keeps DI6 ("GC sees only pointers") and D2.7.1 intact by
construction and removes any rooting question at the return edge —
strictly safer than v2, whose returned Items can point into frame extents.

**RV9 — The shape-4 error lane retires sentinel domain-stealing.** Lane 2
of a native `^E` return is a full error Item (**`ItemNull` = no error** —
*ruled 2026-08-14*; the check is one compare against a constant). The
encoding is unambiguous by construction: a `T^E` function returning a
legitimate `null` flows it on lane 1 (the value lane), so lane 2 `ItemNull`
can only ever mean "no error" — a debug assert that lane-2 payloads are
either `ItemNull` or ERROR-tagged pins this. This conforms to D2.8.1 — the
*value* lane never encodes error-ness — and un-deopts `can_raise` (today a
`^E` annotation forces boxed-ANY return). It also retires `INT64_ERROR`
(`== INT64_MAX == INT_LANE_INF` — the open v5 §5.8 collision) from the
return path: helpers and native bodies signal failure on the error lane,
and `INT64_MAX` returns to being a value. The emission-time
exception-state tracker (online exception-poll plan) is **superseded on
typed call edges** by the register-lane check — one signaling mechanism per
edge, decided here, not two.

---

## 6. ABI identity and dispatch

> **[measured 2026-08-14] — the descriptor is now cross-checked, not
> trusted.** RV10 names the single-source-of-truth defence but leaves it as
> discipline. In practice two derivations exist: a call site computes its
> result count from the callee's descriptor, and the callee computed its
> `nres` from the same descriptor when it was created. Nothing forced them to
> agree, and a disagreement is silent at the MIR level — precisely the v27
> havlak failure. `em_assert_callee_result_count()` closes this: whenever the
> target is a defined function in the module, its real `MIR_func->nres` is
> compared against what the call site is about to emit, and a mismatch aborts
> naming both numbers. Forward/import items are skipped and checked when the
> definition lands. Cheap enough to run unconditionally, so it is not a debug
> build's privilege.

**RV10 — One convention descriptor, consumed everywhere.** The shape is
stored once in the function record (and mirrored in ValueRep at call
sites); wrappers, `fn->invoke` entries, the interpreter bridge, and the L1
MIR module cache all read it. It is part of the cached-module ABI: the
cache key/version must include the convention revision, or a cached module
can call a recompiled one with the wrong shape. The v27 havlak wrong-answer
(boxed result fed to a raw native entry → inf sentinel) is the live example
of this bug class; the descriptor is the single-source-of-truth defense.

- **Dynamic call sites** assume shape 2 (the universal shape). Every
  function therefore has a shape-2-speaking entry: for shape-1 functions it
  is the same entry (lane 2 dummy); for shape-3/4 functions the boxed
  public entry (D8.3 dual-func) boxes the native result — producing a
  pending pair only when the value is actually wide.

  > **[measured 2026-08-14]** This is the part of §6 that the register pair
  > cannot deliver on its own. Dynamic dispatch is C-mediated end to end:
  > generated code calls `fn_callN_into`, ordinary C functions
  > (`Item fn_call1_into(Function*, Item, uint64_t*)`) that resolve the
  > callable and invoke it through a C function-pointer cast. Neither hop can
  > carry two MIR results on any platform. So "every function has a
  > shape-2-speaking entry" is true of the *convention* but is realized
  > through the RV12 companion **location**, not the register form — the same
  > conclusion §7 reaches for public wrappers, arrived at independently.
- **Entry equivalence (DF9)** extends: all entries of one function agree on
  observable results *and* on the pair protocol.
- C2MIR stays frozen on v2 single-Item returns (rule 14); the MIR
  interpreter handles `nres = 2` natively.

**RV10a — Shape and transport are separate axes.** *(**[measured
2026-08-14]**; no new field required, which is why it is recorded here
rather than as a new ruling.)* A shape-2 return can ride either lane-2
transport, and the descriptor already distinguishes them without extension:

| `shape` | `scalar_home_lane_mask` | transport |
|---|---|---|
| `ITEM_SCALAR` | `0` | v3 companion lane (register pair, or RV12 slot) |
| `ITEM_SCALAR` | non-zero | v1 trailing caller-donated home |

This falls straight out of RV12's "companion **location**, not companion
register" framing, and it is what makes a staged migration safe: a call site
reads the callee's descriptor and gets both facts at once, so "which
convention does this entry speak" is never inferred from local context —
the defence RV10 asks for. P1 uses it to keep C-reachable public wrappers on
the home transport while internal bodies move to pairs, with the
entry-equivalence check comparing descriptors rather than home masks.

---

## 7. Platforms and interop

**RV11 — MIR facts (verified in vendor tree, unpatched per rule 16).**
Multi-value returns are core MIR: results are output operands of
`MIR_CALL`; x86-64 SysV allows 2 int + 2 fp results (`mir-x86_64.c`),
aarch64 8 + 8 (`mir-aarch64.c`). Every RV1 shape fits. Two costs to note:
`mir-gen.c` (`multi_out_insn_p`) excludes multi-out call results from one
GVN/combine path (minor); and **Windows x86-64 hard-errors on `nres > 1`**
(`"Windows x86-64 doesn't support multiple return values"`,
`mir-x86_64.c:547/:660`).

**RV12 — The convention is "companion location", not "companion
register".** Semantically lane 2 is *a well-known location valid only until
the next call*. On SysV/aarch64 it is a register (MIR multi-result); on
Windows it is a fixed `EvalContext` slot *(ruled 2026-08-14: the slot **is**
the Windows lowering — v2 homes are kept on no platform; one protocol
everywhere)* — one store on the wide path, one
load on the pending arm, the 99% path pays zero either way, and the
compiler discipline (RV4–RV6) is byte-for-byte identical because both forms
die at the next call. This also gives C builtins a portable way to produce
wide results (write the context slot; no struct-return ABI coincidences
needed) — though per SF14 v1's analysis they largely don't need it: helpers
establish no watermark frame, so `push_l` inside a helper homes payloads in
the *calling* JIT frame's extent and returns ordinary caller-homed Items
(SF6 property). The pair protocol is JIT⇄JIT.

> **[measured 2026-08-14] — the slot is not a Windows special case.** RV12
> presents the context slot as the Windows lowering. Implementing P1 showed
> the constraint is broader and platform-independent: **any entry reachable
> from C cannot return two MIR results on any platform.** In-tree those are
> the public `_b` wrappers, called through the `fn_call_boxed_N_into`
> trampolines and through `fn->invoke` — and §1.4 measures them at **18% of
> all adopt sites**, the single largest block this design can still delete.
> A C prototype has no portable spelling for MIR's two-result convention, so
> for these entries the companion must be a *location* rather than a
> register — which is exactly what RV12 already ratified, just with a wider
> domain than "Windows". Nothing in the protocol changes: same pending tag,
> same single-live rule, same resolution sites, same descriptor. This is a
> scope clarification within the ratified principle, not a new ruling, and
> it leaves the formal spec text (D5.2.1v2 / D2.7.2v2 / D8.4.2v2) correct as
> written — those describe the convention, not its per-platform transport.
> Consequence for §9: **P3 stops being a Windows-only phase.** It is the
> phase that unlocks population 2 of §1.4 everywhere, and should be
> sequenced on that basis rather than on platform coverage.

*Engagement with D2.7.2's rejection of "context-global slots":* that
rejection targeted context slots as **homes** — ownership locations for
values of arbitrary extent, which break under nesting. RV12's slot is a
**transport**: single-live (RV4.2), dead at every call boundary, never the
pointee of any Item. It inherits none of the rejected failure modes.
Because it textually revisits a rejected option it was flagged for explicit
ruling rather than slipped in; that ruling landed 2026-08-14 as part of
RV12, and D2.7.2v2 records the transport-vs-home distinction as ratified.

---

## 8. What gets deleted

Census-backed, on full migration:

- every `em_adopt_scalar_item_value` cluster (20 static / 11–16 dynamic
  instructions per site; 10–413 sites per benchmark);
- the trailing `_scalar_home` parameter on ~all functions and the trailing
  home argument on ~all calls (one argument register back, everywhere);
- home materializations, the discard home, incoming-home forwarding, and
  the entire scalar-home coloring/liveness/interference machinery in
  `em_finalize_scalar_homes`;
- `lambda_item_adopt_scalar_home` and its import;
- `INT64_ERROR` on return paths (RV9), unblocking the v5 §5.8 gate.

Expected static reduction: the measured 9–39% of emitted MIR shrinks to a
residue of 2-instruction tests at unknown-merge escape sites. Number-stack
homes survive only where D5.2.2 already places destination-owned storage
(container tails, fields, envs, module tables, async frames) — those are
stores, not returns, and are untouched by this design.

**[measured 2026-08-14] — re-costed against §1.4.** The list above is
correct about *what* v3 deletes but not about *how much*, because it was
written against the two-population model §1.4 corrects. Against the real
three populations:

| item | status after P1 | gated on |
|---|---|---|
| `_scalar_home` parameter on internal bodies | **gone** (351 → 198 home-param entries; the 198 are exactly the public wrappers) | — |
| trailing home *argument* at call sites | **mostly gone** (havlak 229 → 36, json 101 → 2, richards 63 → 1) | — |
| return-side adopt cluster, internal bodies | **gone** — 14% of all adopt sites | — |
| return-side adopt cluster, public `_b` wrappers | remains — 18% of all sites | RV12 slot transport for C-reachable entries (§7, P3) |
| helper-call adopt cluster, callee declares `PRESERVES` | — | **RV14a** — already provable today, no protocol change (havlak: 78 sites, 7.6% of the module) |
| helper-call adopt cluster (`em_call_import`) | **68%** of all sites | **RV14** deletes it (§4a); the eager per-call restore goes with it |
| eager per-call watermark snapshot + restore | — | **RV15** replaces it with an on-demand loop-end reclaim |
| `em_scalar_home_*` coloring / liveness machinery | remains | RV12 slot (wrappers) + RV14 (helpers) — after both, nothing allocates a home |
| `lambda_item_adopt_scalar_home` + import | remains | same two |
| `INT64_ERROR` on return paths | remains | P2 (RV9) |

Static share moved 9–39% → 7.5–35.4% with shape 2 alone. §8's original
framing invited reading the full number as a consequence of shape 2; it is
not. Shape 2 delivers the per-call argument register and the *body* return
ritual — 14% of sites. The other 86% fall to two mechanisms outside §2–§6,
each matched to watermark ownership: the slot transport for C-reachable
Lambda entries (18%), and RV14 for C helpers (68%). With all three landed,
`lambda_item_adopt_scalar_home` and the entire home apparatus —
`em_scalar_home_new/bind/for_reg/ref`, `em_finalize_scalar_homes`, every
`_scalar_home` and `result_home` parameter — have no remaining caller.

---

## 9. Migration plan

- **P0 — encoding + guards.** `ITEM_PENDING_TAG`, static asserts, debug
  assert-on-touch in `type_id()`/accessor choke points. No behavior change.
- **P1 — shape 2 for boxed returns**, behind a build flag: emit pair
  returns, ValueRep pending tracking, RV5 resolution sites; delete adopt
  clusters and home params under the flag. Gate: lambda baseline 100%,
  forced-GC sweep clean, MT7 MIR budgets re-ratcheted (they will drop
  sharply), AWFY output diffs clean.
- **P2 — shapes 3/4 for typed returns**: un-deopt `can_raise`, error-lane
  checks, retire `INT64_ERROR` returns. Gate: typed benchmark suite; the
  R26 regression rows (tak, fannkuch, pnpoly) as canaries against
  reintroducing per-call check taxes.
- **P2.6 — read the watermark effect** (RV14a). Gate the helper-side adopt on
  `number_stack != PRESERVES` as well as `scalar_class`, and add the
  `lambda_side_number_alloc` debug assert that keeps the audit honest. A few
  lines, no ABI or protocol change, and it stands on its own whether or not
  P2.7 ever lands — it removes 7.6% of havlak's emitted MIR by itself. Gate:
  baseline + emission fixtures; the ratchet should drop.
- **P2.7 — C-helper convention** (RV14/RV15/RV16). Give wide-capable mutable
  locals their own declaration-time slot **first** (RV16) — the reclaim is
  unsound without it — then drop the eager per-call watermark
  snapshot/restore and the helper-side adopt in `em_call_import`, and add the
  loop-end reclaim, emitted only for loops the emitter saw make a
  wide-capable helper call. Settle RVO11 (unnamed wide temporaries crossing
  the back edge) *before* emitting the restore. Independent of P2 and P3 — it touches neither the
  return ABI nor any entry signature — so it can land in any order among
  them. Gate: baseline + forced-GC sweep, plus a peak-side-stack measurement
  on a million-iteration untyped loop over `int64` array elements and over
  `int64` map fields. Correctness alone does not gate this one; the point of
  RV15 is the space bound, so the gate has to measure space.
- **P3 — slot transport** (RV12 slot form) + Windows CI coverage.
  *(**[measured 2026-08-14]** — re-scoped: this is not a platform-coverage
  phase. Per §7 it is what lets C-reachable public wrappers speak the
  convention on *every* platform, and per §1.4 that is 18% of all adopt
  sites — the largest block shape 2 alone cannot reach. Its value is no
  longer contingent on shipping Windows.)*
- **P4 — ratification & deletion**: remove v2 machinery, revise D5.2.1 →
  v2, D2.7.2 → v2, D8.4.2 → v2 (+ doc semver bump), annotate SF14 with the
  v3 supersession, update LR/JS design docs.

Ordering note: P1 before P2 because shape 2 is the universal entry every
dynamic call assumes (§6); typed lanes plug into an already-correct boxed
world.

**[measured 2026-08-14] — status.** P0, P1.1, P1.2, P1.3 (in its eager form,
see RV7's note), P1.5 and P1.6 are implemented behind `LAMBDA_RETURN_V3`,
default off. Both configurations gate green on the Lambda baseline at
**3719/3719**. Emission fixtures assert both conventions rather than one.

- **RVO3 and RVO4 are closed** (§10). RVO4 was not a formality: it exposed a
  real defect in which a shape-2 call site published its raw result to the
  async spill tracker and the GC root machinery before resolving.
- **P1.4 is partial by necessity.** Its descriptor cross-check landed
  (`em_assert_callee_result_count`, see §6), but pair-returning dynamic
  entries and `nres=2` public wrappers are blocked on the RV12 slot
  transport, for the C-mediation reason recorded in §6 and §7. That work
  belongs to P3, whose scope §1.4 and §7 both re-cost.
- **Remaining in P1:** P1.7's measurement half — MT7 re-ratchet, AWFY stdout
  diff, release timing against the R26 canaries, RVO8/RVO9.

Full log: [`Lambda_Impl_Return_Value.md`] §5.

---

## 10. Open issues

*(RVO1/RVO2 — Windows lowering and transport-vs-home distinction —
RVO5/RVO6 — LambdaJS scope and DTIME — and RVO7 — error-lane `ItemNull`
encoding — were resolved 2026-08-14 and folded into rulings RV12, RV13,
RV8, and RV9 respectively; IDs retired, listed here only so the ledger
numbering stays traceable.)*

- ~~**RVO3 — Interpreter/JIT bridge.**~~ **Closed by audit, [measured
  2026-08-14].** The crossing casts `fn->ptr` to a single-Item C prototype,
  and that cast is already gated on `entry_abi` ∈ {`LAMBDA_BOXED_FUNCTION`,
  `LAMBDA_BOXED_PROCEDURE`, `HOST_ADAPTER`}. Those markers are applied only
  to entries published through a `_b` wrapper, so a pair-returning body can
  never reach the cast and a pending Item cannot cross into interpreter or
  host code — by construction, with no new code. MIR-interpreter mode needs
  nothing either: `nres = 2` is native there, and every C entry point
  (`main`, `_b` wrappers) is single-result. The guard now documents that it
  carries this invariant so a later edit cannot relax it unknowingly.
- ~~**RVO4 — Async/generator transport.**~~ **Closed, [measured
  2026-08-14]** — and it was not vacuous. Publishing a call result to the
  async spill tracker and to the root machinery happens on the *same* path
  (`em_after_resolved_call`), and the root machinery fires for every boxed
  Item result. A shape-2 call site that published its raw result would
  therefore spill a pending Item across a suspension *and* write it into a
  GC root slot, where its `0x1E` tag is outside the traceable TypeId range —
  two RV4.1 violations from one omission, latent rather than benign because
  eager resolution overwrites the slot almost immediately. Publication is now
  split so pair sites announce only the resolved register, once. The
  suspension-crossing test is
  `test/lambda/proc/wide_scalar_across_await.ls`. `yield` using the pair
  shape upward remains undecided and unneeded.
  **Generalized lesson for the remaining phases:** RV5's site taxonomy lists
  *semantic* resolution points (stores, arguments, calls, suspensions). The
  emitter has *bookkeeping* consumers that also write results to memory, and
  those are not in the table. Any new consumer of a call result must be
  checked against RV4.1, not just the ones the taxonomy names.
- **RVO8 — GVN exclusion.** Measure whether `multi_out_insn_p`'s combine
  skip costs anything on hot pair-returning loops; if so, it is a Lambda-side
  emission-shape question, not a vendor patch. *(Still open — P1 landed
  without a post-optimization measurement.)*
- **RVO9 — Dummy lane-2 cost.** Confirm the dummy operand on non-wide
  shape-2 returns is move-eliminated in practice (inspect MT7 dumps);
  if not, consider splitting shape 2 further by proven-wide-free inference
  once available (folds back into shape 1 without ABI change — the
  descriptor already carries the answer). *(Still open. Finalized MIR shows
  the expected `mov comp, 0`; whether the generator eliminates it is the
  part not yet measured. Note RV3a makes this ~1 of the 5 fast-path
  instructions, so it is now a larger fraction of a much smaller cost.)*
- ~~**RVO10 — Helper-call adopts.**~~ **Ruled 2026-08-14 → RV14/RV15 (§4a).**
  Direction (a) was taken: stop restoring the watermark per helper call, and
  let a C helper's push stand as an ordinary caller-homed Item, because a C
  helper owns no watermark to tear down. Direction (b) — giving helpers the
  RV12 slot — was not needed and would have been the more invasive of the
  two, since a helper returning a *pending* Item breaks its C-to-C callers
  (`box_int64_value` alone has 32) and would have required a second entry
  point per helper. The `WideItem { Item; uint64_t }` struct-return variant
  was rejected with it: it pessimizes the ~99% non-wide path, changes ~1400
  registry rows plus every C-to-C caller, and works only by an ABI
  coincidence (SysV `rax:rdx` / AArch64 `x0:x1` happen to match MIR's
  two-result convention, while Win64 returns 16-byte structs via hidden
  pointer) — exactly the coincidence RV12 set out to avoid.
- **RVO11 — Unnamed wide temporaries crossing a loop back edge.** *(opened
  and narrowed 2026-08-14)* **RV16 settles the named case**: a wide-capable
  mutable local owns a slot allocated at its declaration, below the
  loop-entry watermark, so the reclaim cannot reach it. What remains is
  whether a *compiler-generated* wide temporary — one that is not a source
  binding, so RV16 gives it no slot — can be live across a back edge. With
  binding promotion in place that set ought to be empty, but "ought to be" is
  the difference between a use-after-free and a slowdown if it is wrong.
  **Gate the reclaim on the emitter positively establishing the set is
  empty, not on the assumption.** The question is now bounded and local —
  "does any unnamed wide value cross this back edge?" — rather than the
  open-ended "where do loop-carried wide values live?" it started as.

  *Refinement — reclaim at statement boundaries, not back edges.* Given RV16,
  the values that survive a *statement* are exactly those stored into a
  binding (RV16 slot, allocated at the declaration) or into destination-owned
  storage (D5.2.2). Everything else a statement produced is dead at its end —
  that is what a statement boundary means in an expression-oriented language.
  So the reclaim can take its watermark at statement start and restore at
  statement end, which is **sound for the same reason as the back-edge form
  but bounds growth tighter** (per statement rather than per iteration) and
  needs no loop-structure tracking at all. The back-edge form remains the
  fallback for statements that legitimately carry a wide temporary across
  their own boundary, if any exist. This does not remove the RVO11 obligation
  — it relocates it to a place the emitter already has structure for.

  *Implementation survey, 2026-08-14.* The emitter has no loop-structure
  tracking to hang this on: `MirFrameState.root_backedge_reloads` exists but
  has no writer or reader — declared and unconsumed, like
  `JitCallEffects.number_stack` was before RV14a and like `BindingStorage`
  still is. Emitting the restore is easy (the back edge is a `MIR_JMP` the
  loop lowering already writes); deciding whether it is *safe* is what needs
  a home. The promising placement is **`em_finalize_scalar_homes`**, which
  already computes home live ranges and interference at function
  finalization: emit the back-edge restore unconditionally during lowering,
  then during finalization delete any restore whose loop body has a home live
  range crossing its edge. That inverts the problem from "prove nothing
  crosses" to "detect what crosses and back off", which is the safe
  direction, and it reuses the pass RV16 would otherwise retire — so
  sequencing matters: the coloring pass must outlive RV16 long enough to
  validate the reclaims, or the check needs its own liveness.

- **RVO11 status 2026-08-15 — narrowed to ONE unverified fact.** RV16 shipped
  (P2.7.1) and the reclaim was designed against the tree. The obligation is now
  *"does this statement leave a wide payload above the snapshot that something
  outliving the statement still points at?"* — with **bindings settled**
  (covered iff RV16 uses the broad `may_be_wide` predicate; the shipped narrow
  one does not cover them) and **container stores unverified**: whether a wide
  Item stored into an array/map is copied into destination-owned storage
  (D5.2.2) or retains a pointer into the caller's number extent. The eager
  per-call restore hides that question today; removing it exposes it. Verify
  before any reclaim ships.

  **Measured consequence for RV14/RV15/RV16 (impl log, same date): the
  prerequisite that makes the reclaim sound costs ~4× what the reclaim saves.**
  Broad RV16 adds 600–800 instructions per AWFY benchmark; RV15 recovers ~142
  in deltablue. The rulings stand for the 757-site population they were written
  against; at v3's 11 sites they are the wrong trade.

- **RVO12 — Shape 4 on the INT-family lanes.** *(opened 2026-08-15 with
  RV17)* RV17 gives an `int^`/`i64^` body the same `int | error` type that
  unlocked `float^`, but native admission is still float-only, so those
  bodies remain boxed. The gap is **not** typing and not shape selection —
  it is a missing **discriminator** at the return boundary. *(Corrected
  2026-08-15: an earlier draft of this issue said "missing conversion" —
  wrong, and misleading about where the work is. `it2l` exists and
  `emit_unbox` already handles every int type. Nothing needs writing.)*

  `emit_function_return` fixes up a boxed value arm by testing
  `MIR_reg_type(value) != MIR_T_D` and calling `it2d`. That trigger is a
  **register-class test standing in for a representation question**. On the
  float lane the proxy is faithful — a boxed Item is always `MIR_T_I64`, a
  native double always `MIR_T_D` — so the branch fires exactly when the value
  is boxed, which is why its comment can claim `it2d` is universal and the
  frame needn't carry the semantic TypeId. On the int lane the proxy is
  **degenerate**: a boxed Item and a native `int64` are both `MIR_T_I64`, so
  no register-class difference survives to test, the branch can never fire,
  and the fall-through `MIR_MOV` ships the Item's tag bits out as the int
  lane — outside int53, saturating to the inf sentinel (the havlak failure
  mode). The restriction is therefore deliberate, not provisional.

  This is a live instance of the anti-pattern
  [`Lambda_Design_Compiling_Lane.md`] already names — *never read back
  `MIR_reg_type` to learn representation* (§4.5 confines the API to physical
  questions such as move selection). The float path gets away with the
  violation because the physical class happens to encode the semantic fact;
  the int path is where the rule bites. Adding an `it2l` arm here would not
  help: it would need the same discriminator the branch lacks.

  **The boundary uses three proxies, not one** *(surveyed 2026-08-15)*. The
  `fn` tail cascade discriminates by a different approximation per lane:

  | declared lane | proxy | kind |
  |---|---|---|
  | INT | body is a block \|\| body is BINARY → assume boxed | syntactic |
  | FLOAT | `MIR_reg_type(body) != MIR_T_D` → boxed | register class |
  | fallback | `body_tid == ANY \|\| NULL` → boxed | semantic type |

  with `emit_function_return`'s `it2d` as the net behind them. Float's proxy
  is exact; **int has two proxies and both are approximations** — the
  cascade's own comment states the semantic tail type "is not evidence that
  body_result is a raw lane", which is why the syntactic heuristic exists.
  The control case is the `pn` explicit-`return` path, which uses the
  semantic proxy *alone* and supports INT/FLOAT/BOOL — which is exactly why
  `infer_proc_native_return_lane` admits int for procs today while `fn`
  bodies cannot. The difference is not the int lane; it is that the pn
  producer does not box behind the type's back.

  **(a) — carry the carrier.** Put `ValueRep` on expression results so each
  producer *states* Item-vs-lane. Retires all three proxies at once. The
  syntactic one is a live correctness hazard independent of shape 4 (any int
  body that boxes but is neither a block nor a binary is already
  mis-classified — a bug class, not a limitation), and this also repairs the
  float path's latent Compiling-Lane violation rather than leaving it
  correct-by-luck. Cost is real: `ValueRep` is presently mostly a JS-side
  citizen (18 mentions in `transpile-mir.cpp` vs heavy `js_mir_*` use), so
  Lambda-side adoption is new ground. But this is the Compiling-Lane design's
  own agenda — RVO12 pulls it forward, it does not invent it.

  **(b) — remove the need to know.** Have the branch join hand the boundary a
  native lane when the union's value component is native, so no
  discrimination is needed on either lane and the `it2d` leaves the float
  path too. It composes with RV17 in principle: the union already declares
  lane 1 = `T`, lane 2 = `error`; the merge would honour what the type says.

  **(b) is NOT independently safe, and this reverses an earlier draft's
  recommendation.** For the canonical `fn twice(x: int) int^ { if … }` the
  body wrapper is a block, so `native_body_is_block` holds and the INT branch
  fires an unconditional `emit_unbox_contract_lane`. If the merge began
  producing a native lane, that unbox would run **on a value that is already
  a lane**, and it cannot detect the difference — native int and Item are
  both `MIR_T_I64`. That is the current bug's mirror image. So (b) requires a
  carrier fact at the same boundary to suppress the proxy: a miniature (a).
  (b) also fixes only **one producer**; generic-path calls and `match`
  results — the cases `emit_function_return`'s comment names — keep theirs.

  **Sequencing: (a) scoped to the `fn` tail return first, then (b) on top.**
  The increment between "(b) plus the carrier fact it needs" and "(a) for
  this boundary" is small, and only (a) makes int shape 4 work for *all* body
  forms rather than the if-tail alone. With a truthful carrier in place, a
  lane-producing merge becomes a strict improvement instead of a new hazard.

  Scope note: (b) concerns the **callee-internal** merge. The caller-side
  merge governed by RV17a still builds a boxed join for `^err` and is
  unaffected — different merge points, no conflict between the rulings.

  ~~**RVO12 remains open.**~~ **CLOSED 2026-08-15 — but the final blocker was
  a THIRD defect, not the carrier.** With RV18 in place the INT/INT64
  admission was widened and one consumer still failed: `or` containment of a
  native `int^` error aborted the script (`maybe_value(0 - 1) or 9` — the
  error escaped to top level) while `^err` destructuring worked on every
  lane. The instruction trace pinned it: `transpile_binary_out`'s
  native-arithmetic block was gated on **operand types only**
  (`both_int || both_float || int_float`), never on the operator. A can-raise
  call whose operand witness now said INT walked `or` into the block, where
  the eager native operand fetch consumed the error lane with
  `emit_return_if_item_error` — **propagating the error `or` was about to
  contain** — converted both operands to doubles, found no `OPERATOR_OR`
  case in the switch, and fell through to the boxed `or`, which evaluated
  both operands a **second** time.

  The fix gates the block on the operator set it implements
  (ADD/SUB/MUL/DIV/POW and the six comparisons). That also fixes a latent
  master bug the trace exposed: any unsupported operator with native
  operands — e.g. `f() or 9` with an ordinary non-raising `fn f() int` —
  evaluated its operands TWICE, duplicating side effects; it went unnoticed
  because the duplicated work was usually pure and the results dead.

  **Post-mortem of the diagnosis:** the first reading of the trace
  mis-attributed the L41 tag test as a null check — `eq tag, 27` — because
  27 was assumed to be `LMD_TYPE_NULL`. It is `LMD_TYPE_ERROR` (NULL is 1).
  One enum lookup corrected the whole narrative from "missing error case in
  a null-containment path" to "error propagation emitted by the operand
  fetch", which is what the fix addresses. Verify tag constants against the
  enum before reasoning from a dump.

  Ledger of what RVO12 actually took, in order: **RV17** (the union names
  the value component) → **RV18** (the producer publishes the carrier; the
  if-merge boxes structured results) → **the operator gate** (non-arithmetic
  operators never enter the native-arith block). Each was necessary; only
  the three together were sufficient. Shape 4 now emits from ordinary source
  on all three lanes — `func d, i64` / `func i64, i64` — and
  `test/lambda/raise_arm_native_return.ls` pins destructure, `or`
  containment, and arithmetic-on-recovered for float, int, and i64.

  Interaction with **§5.8** of [`Lambda_Semantics_Int_Type.md`]: the int lane
  reaches this only after the `INT64_ERROR`/`INT_LANE_INF` collision is
  settled, since shape 4's whole premise is that the error leaves the value
  lane alone. Resolving RVO12 before that gate would re-introduce exactly
  the domain-value theft §0 lists as the thing v3 exists to stop.
