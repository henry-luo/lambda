# Lambda Return-Value Convention v3 — Companion-Lane Wide Scalars and Native `T^E` Lanes

> **Status: DECIDED 2026-08-14 (user ruling) — migration P0/P1.1/P1.2/P1.3
> IMPLEMENTED 2026-08-14** behind the whole-module flag `LAMBDA_RETURN_V3`
> (default 0; v1 ABI still ships). Impl log, deviations and gate results:
> [`Lambda_Impl_Return_Value.md`] §5. Sections marked
> **[measured 2026-08-14]** below were revised from proposal fidelity to
> observed behaviour once shape 2 was emitting; §1.4 corrects §1.2's site
> split, §8 is re-costed against it, and RVO10 is new.
> Ledger **RV1–RV13** (+ measured addenda RV3a, RV10a) + open issues
> **RVO1–RVO10**. The trailing-scalar-home
> ABI is retired in favor of the companion-lane convention; the formal spec
> is revised (**D5.2.1v2**, **D2.7.2v2**, **D8.4.2v2**, spec v1.20.0), with
> the retired-ABI record in its Appendix A. The v1 ABI remains the shipping
> mechanism until migration P4 (§9); mechanism details below stay at
> proposal fidelity until then. The 2026-08-14 rulings on Windows lowering,
> LambdaJS scope, DTIME, and the error-lane encoding are folded into
> **RV12**, **RV13**, **RV8**, and **RV9** (RVO1/RVO2/RVO5/RVO6/RVO7
> retired); RVO3, RVO4, RVO8, and RVO9 remain open.
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
3. **Helper-call adopts (68%) — outside this design entirely.** §1.2 said
   "the rest adopt call results into local homes, which shape 2 converts to
   'patch only on escape'". That is **not** what they are. They sit in
   `em_call_import`, after calls to C helpers (`fn_member`,
   `lambda_name_id_to_item`, `pn_print`, …), and RV12 already states the
   pair protocol is JIT⇄JIT — so shape 2 cannot convert them. Their cause is
   unrelated to the return ABI: the caller snapshots `side_number_top`
   before the helper call and restores it immediately after, so a payload
   the helper homed in the caller's own extent would be reclaimed; the adopt
   copies it somewhere stable first. Tracked as **RVO10**.

The trailing home *argument* at call sites did drop sharply, which is the
per-call argument-register win §8 claims: havlak 229 → 36 sites, json
101 → 2, richards 63 → 1, deltablue 173 → 79 (deltablue lags because so many
of its calls target public wrappers).

Net static effect of P1 as landed: 9–39% → 7.5–35.4% of emitted MIR. The
design's larger number is still reachable, but it is gated on population 2
(RV12 slot transport for C-reachable entries) and population 3 (RVO10), not
on anything in §2–§6.

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
| helper-call adopt cluster (`em_call_import`) | remains — **68%** of all sites | **RVO10** — not a return-ABI problem |
| `em_scalar_home_*` coloring / liveness machinery | remains | the two rows above |
| `lambda_item_adopt_scalar_home` + import | remains | the two rows above |
| `INT64_ERROR` on return paths | remains | P2 (RV9) |

Static share moved 9–39% → 7.5–35.4%. The headline number in §1.2 is still
reachable, but §8's framing invited reading it as a consequence of shape 2
alone; it is not. Shape 2 delivers the *per-call argument register* and the
*body* return ritual. The remaining 86% of adopt sites need the slot
transport (§7) and RVO10 respectively — two separate pieces of work that
this design does not by itself perform.

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

**[measured 2026-08-14] — status.** P0, P1.1, P1.2 and P1.3 (in its eager
form, see RV7's note) are implemented behind `LAMBDA_RETURN_V3`, default
off. Both configurations gate green on the Lambda baseline (3718/3718 with
the flag off; the flag-on run's only failure is a known heavy-test parallel
flake, and the JS suite is 347/347 standalone). Emission fixtures now assert
both conventions rather than one. Remaining in P1: P1.4 (dynamic dispatch
still materializes `dyn_scalar_home`), P1.5 bridges, P1.6 async audit, and
P1.7's measurement half — MT7 re-ratchet, AWFY stdout diff, release timing
against the R26 canaries, RVO8/RVO9. Full log:
[`Lambda_Impl_Return_Value.md`] §5.

---

## 10. Open issues

*(RVO1/RVO2 — Windows lowering and transport-vs-home distinction —
RVO5/RVO6 — LambdaJS scope and DTIME — and RVO7 — error-lane `ItemNull`
encoding — were resolved 2026-08-14 and folded into rulings RV12, RV13,
RV8, and RV9 respectively; IDs retired, listed here only so the ledger
numbering stays traceable.)*

- **RVO3 — Interpreter/JIT bridge.** `lambda-eval.cpp` returns plain Items;
  crossing wrappers must resolve pending pairs in both directions. Audit
  all `fn->invoke` entry kinds.
- **RVO4 — Async/generator transport.** RV5 makes suspension a resolution
  point; verify the async spill tracker and generator `yield` lanes (SF14
  reference) never observe a pending Item, and decide whether `yield`
  itself may use the pair shape upward.
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
- **RVO10 — Helper-call adopts. *(new, [measured 2026-08-14])*** §1.4 finds
  **68% of all adopt sites** sit after C-helper calls in `em_call_import`,
  untouched by anything in this design. Their cause is not the return ABI:
  the caller snapshots `side_number_top` before the call and restores it
  immediately after, so a wide payload the helper homed in the caller's own
  extent (SF6) would be reclaimed — the adopt copies it somewhere stable
  first. Two candidate directions, neither yet analysed: **(a)** stop
  restoring the watermark per helper call and let the extent run to the
  frame's own epilogue, trading number-stack footprint for the whole ritual;
  **(b)** give wide-producing helpers the RV12 slot, which C *can* write —
  the possibility RV12 already notes ("a portable way for C builtins to
  produce wide results") but discounts as largely unnecessary, a judgement
  §1.4's 68% now puts in question. This is the largest remaining item in the
  adopt census and deserves its own design pass rather than a P-phase.
