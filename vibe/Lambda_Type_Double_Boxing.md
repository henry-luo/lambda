# Lambda Double Boxing v2 — Inline Doubles via High-Byte Float Self-Tagging

- **Status:** PROPOSED
- **Date:** 2026-07-10
- **Co-Author:** Anthropic Fable
- **Scope:** the runtime `Item` representation of `float` (binary64) across Lambda core, LambdaJS, the MIR transpilers, and the GC. Supersedes and expands **Part 5** of `Lambda_Tuning_Proposal.md`.
- **Convention:** `file:line` references drift; confirm against the symbol name.
- **Related:** `Lambda_Tuning_Proposal.md` (Parts 1–5 — the engine-wide analysis this fix comes from), `Lambda_Semantics_Number_Model.md` (number model v2 — `int` compact band, `f64` alias rule), `doc/dev/js/JS_03_Value_Model.md`, `doc/Lambda_Formal_Semantics.md` §4 (numerics ADR).

## 0. Summary

Today every Lambda/LambdaJS `float` that exists as an `Item` is a tagged **pointer** to a heap-allocated 8-byte payload. This proposal stores effectively all real-world doubles **directly inside the 64-bit `Item` word** — no allocation, no dereference, no GC involvement — while keeping Lambda's high-byte tag scheme, int53 compact packing, containers-as-bare-pointers, and the 256-value tag space untouched.

The mechanism adapts the published "Float Self-Tagging" technique to Lambda's layout. Because Lambda's tag byte already occupies the bits where a double's sign and top exponent bits live, the adaptation needs **no rotation at all**: one integer **bias** add centers the common exponent band onto tag values discriminable by a **single bit test** (word bit 62). Encode = 1 add + 1 branch; decode = 1 test + 1 subtract + 1 bitcast. Coverage is magnitudes ≈1.5·10⁻¹⁵⁴ … 2.7·10¹⁵⁴ — effectively 100 % of doubles any real workload produces.

---

# Part 1 — Prior Art

## 1.1 NaN-boxing (JavaScriptCore, SpiderMonkey, LuaJIT)

IEEE-754 doubles reserve a huge encoding region for NaNs: any bit pattern with exponent `0x7FF` and a non-zero mantissa. A *quiet* NaN needs only one canonical pattern, leaving ~2⁵¹ patterns unused. NaN-boxing makes the 64-bit value word **be** a double whenever its bits are not in that NaN space, and encodes everything else — pointers, int32s, booleans, undefined — inside the unused NaN payload patterns.

- **JSC variant:** stores doubles offset by 2⁴⁸ so that pointers (top 16 bits zero) and doubles occupy disjoint ranges; ints live at the top of the space.
- **SpiderMonkey variant:** a 17-bit tag in the high bits; values below the NaN boundary are doubles.
- **LuaJIT:** same family; everything non-double lives inside NaN space.

Properties: a double is stored, passed, and returned with **zero boxing** — no allocation, no indirection, no GC pressure. The costs are structural: pointers shrink to 48–51 masked bits, inline ints to int32, and the whole type taxonomy must fit into NaN payload space (~a dozen clean tags). It is a *total* commitment — the encoding of every type changes at once.

## 1.2 V8 — tagged pointers + SMIs + heap numbers

V8, the fastest engine on our benchmark table, deliberately does **not** NaN-box. Its universal value is a tagged pointer: low-bit tag distinguishes a small integer ("SMI", 31/32-bit) from a heap object pointer; a double that must exist as a universal value is a heap-allocated **HeapNumber** — structurally the same "boxed double" disease Lambda has.

V8 gets away with it because its feedback-driven JIT ensures hot code almost never sees the universal representation: type feedback keeps hot doubles in raw machine registers; objects get **mutable double fields** (raw binary64 stored inline in the object); arrays get **double element kinds** (`PACKED_DOUBLE_ELEMENTS` — raw doubles in the backing store). The lesson (Tuning Proposal Part 4 §4.4): *value representation matters in inverse proportion to the strength of type specialization*. Lambda's static inference is far weaker than V8's runtime feedback, so Lambda's universal representation is load-bearing in exactly the code where V8's never appears.

## 1.3 The Float Self-Tagging paper (rotation scheme)

**Paper:** *Float Self-Tagging* — Olivier Melançon, Marc Feeley et al. (Université de Montréal / Gambit Scheme group, ≈2024–2025; prototyped in Gambit **and in V8**). *(Verify the exact citation when picking this up; the mechanism is what matters.)*

Context is classic **low-bit tagging** (OCaml, many Schemes, V8's SMIs): the low 2–3 bits are the tag; aligned pointers self-tag via their zero low bits; doubles don't fit (they use all 64 bits) and must be heap-boxed.

The insight: a double's **high** bits (sign + top exponent bits) are highly predictable — real programs' doubles live almost entirely in a moderate magnitude band where the top few bits take only a handful of values. So:

1. **Rotate** the double's bit pattern left by *k* (the tag width). The predictable top bits land in the low-bit tag position.
2. **Designate** the tag values corresponding to the common exponent band as "this word *is* an inline double". A double in the band *tagged itself*. Encode = rotate (+ band check); decode = rotate back.
3. Out-of-band doubles (huge/tiny magnitudes, subnormals, ±0.0, ±Inf, NaN) fall back to the existing heap box. The paper measures **>99 %** of doubles self-tagging in practice.
4. A **bias** variant adds a constant to the exponent field instead of (or in addition to) rotating, repositioning the band onto mask-friendly tag values.

Why it beats NaN-boxing as a retrofit: it is a **local carve-out**. Pointers, fixnums, and the existing tag space stay untouched; you donate a few tag values to the common float band. Everything that doesn't opt in keeps working unchanged.

## 1.4 Lambda v1 — the current boxed double

Lambda's `Item` is a 64-bit word whose high byte `[63:56]` is the `TypeId` (`lambda.h:92`–`:121`). `LMD_TYPE_FLOAT` is a **tagged pointer**: `d2it(ptr)` ORs the tag onto a pointer to an 8-byte payload (`lambda.h:911`); `it2d` masks and dereferences. The payloads live in three places, with three different lifecycles (all verified against current code):

1. **The numeric nursery** (`gc_nursery_t`) — `push_d` (`lambda-mem.cpp:598`) bump-allocates an 8-byte slot (`gc_nursery_alloc_double`, `lib/gc/gc_nursery.c:109`). This is the boxing site for every runtime float temporary in both engines (`jm_box_float` → `push_d`; `js_make_number`, `js_runtime_value.cpp:1071`). Critically, the numeric nursery is **never collected, never moved, and never reset** — `lib/gc/gc_nursery.h:15`: *"Values allocated from the nursery persist until nursery_destroy()"*. Consequences:
   - float-heavy code **leaks boxed numerics monotonically** for the life of the runtime (a latent problem for long-running Radiant pages);
   - float Items need **no GC rooting** — `should_gc_root_var` already excludes `LMD_TYPE_FLOAT` (`transpile-mir.cpp:479`) — because nothing ever moves or frees the payload.
2. **GC-heap Float objects** — parsers/MarkBuilder allocate header-carrying Float objects in the object zone (traced as leaf objects, `gc_heap.c:907`). Mark-sweep managed, non-moving.
3. **Embedded payloads in container buffers** — `array_set` stores float payloads in the "extra" area of an items[] buffer; when the GC relocates the buffer, `gc_fixup_embedded_pointers` (`gc_heap.c:1202`) rebases the tagged pointers that live *in the same buffer*.

The aggregate cost (Tuning Proposal Part 1, Wall 1 + Wall 4): every float crossing a boundary is a C call + allocation; every consumer pays a cache-missing dereference; equal values are pointer-distinct (bit-comparison of Items is representation-sensitive); and the nursery population only grows. matmul 176×, navier_stokes 155×, nbody 163× vs Node trace directly to this plus the array-element story.

**v1's one virtue worth preserving:** it is uniform and simple — every non-inline scalar is "tagged pointer to payload", and all of Lambda core, four language frontends, the GC, parsers, formatters, and Radiant pattern-match on the high byte. Any replacement must keep that world legible.

---

# Part 2 — The New Design

## 2.1 The no-rotation observation

Lambda's tag is the **high byte** — exactly where a double's sign bit (63) and exponent (62:52) already live. The paper rotates to move a double's predictable high bits down to the low-bit tag position; in Lambda **they are already in the tag position**. So no rotation is needed — only a **bias** to center the common exponent band onto a tag region testable with a single bit.

## 2.2 Encoding

Let `BIAS = 0x2000_0000_0000_0000` (bit 61 = exponent-field bit 9, so the add puts `+0x200` on the 11-bit biased exponent) and `BIT62 = 0x4000_0000_0000_0000`.

```c
// encode: double → Item          (replaces push_d at boxing sites)
uint64_t u = bits(d);              // bit-identical reinterpret
uint64_t t = u + BIAS;             // one integer add
if (t & BIT62)  item = t;          // in-band: the word IS the double (self-tagged)
else            item = box_float_cold(d);   // rare: out-of-band → interned or boxed (§2.5)

// decode: Item → double          (replaces the it2d masked dereference)
if (item & BIT62)  d = bits⁻¹(item - BIAS);        // subtract + reinterpret
else               d = *(double*)(item & MASK56);  // boxed fallback

// discrimination — the single bit-mask test used everywhere:
is_inline_double(item)  ⟺  (item & BIT62) != 0
```

### Why the arithmetic is exactly right (verified by hand)

- Adding `BIAS` adds `0x200` to the exponent field `e` (bits 62:52). The result has **bit 62 set iff `e + 0x200 ∈ [0x400, 0x7FF]` iff `e ∈ [0x200, 0x5FF]`**.
- For in-band values (`e ≤ 0x5FF`), `e + 0x200 ≤ 0x7FF` fits inside the exponent field — **no carry into the sign bit**, so negative doubles keep their sign through encode/decode.
- Out-of-band values (`e ≥ 0x600`) carry into bit 63 (and can wrap mod 2⁶⁴); tiny values (`e < 0x200`) leave bit 62 clear. Either way the transient `t` has bit 62 clear and is **discarded, never stored** — no out-of-band bit pattern ever aliases a real tagged Item.
- Decode `t − BIAS` is the exact inverse mod 2⁶⁴.

| Input double | `e` | `t` high byte | bit 62 | Result |
|---|---|---|---|---|
| `1.0` (`0x3FF0…`) | 0x3FF | 0x5F | set | inline |
| `-1.0` (`0xBFF0…`) | 0x3FF | 0xDF | set | inline |
| `2⁻⁵⁰⁰` | 0x20B | 0x40 | set | inline |
| `1e-300` | ~0x01A | 0x21 | clear | boxed |
| `1e300` | ~0x7E3 | 0x9E (carry into sign) | clear (discarded) | boxed |
| `±0.0`, subnormals | 0x000–0x1FF | 0x20–0x3F | clear | interned (§2.5) |
| `±Inf`, `NaN` | 0x7FF | 0x9F / 0x1F | clear | interned (§2.5) |

### Coverage

`e ∈ [0x200, 0x5FF]` → unbiased exponent −511 … +512 → magnitudes **≈1.5·10⁻¹⁵⁴ to ≈2.7·10¹⁵⁴** — the square root of the double range in both directions. The paper's >99 % measured self-tag rate becomes effectively **100 %** here; only subnormals, zeros, infinities, NaNs, and astronomically scaled magnitudes take the fallback.

## 2.3 The occupancy invariant (one bit partitions the tag space)

> **Invariant:** word bit 62 (`0x40` in the high byte) set ⟺ the Item is an inline self-tagged double. Every TypeId, sentinel, and packed encoding keeps bit 62 **clear** — high bytes stay within `0x00–0x3F` and `0x80–0xBF`.

Inline doubles occupy exactly the high bytes `0x40–0x7F` (positive) and `0xC0–0xFF` (negative). Audit of current occupancy:

- **TypeId enum** (`lambda.h:92`–`:121`) — sequential from 0, `LMD_TYPE_COUNT < 0x1E`. ✓ Compliant, with `0x1E–0x3F` and `0x80–0xBF` free for future types.
- **Containers** — bare pointers, high byte `0x00` on canonical user-space addresses. ✓ Even under Linux LA57 (57-bit VAs), user addresses reach bit 56 at most — bit 62 of an address is unreachable. ✓
- **Packed layouts** — `ITEM_INT` int53-in-56 (high byte = `LMD_TYPE_INT`; sign extension confined to the low 56 bits by the `& 0x00FF…` mask in `i2it`, `lambda.h:904`), `NUM_SIZED` (`lambda.h:221`), bool/null/undefined/TDZ/spreadable-null (`lambda.h:871`–`:892`): all keep their TypeId in the high byte. ✓
- **✗ Two violations to renumber:** `JS_DELETED_SENTINEL_VAL = 0x7E00DEAD00DEAD00` (`js_runtime.h:30`) and `JS_ITER_DONE_SENTINEL = 0x7F00DEAD00000000` (`js_runtime.h:35`) sit in bit-62-set space and would read as inline doubles. Both are engine-internal constants chosen precisely *because* their tag bytes were unused — renumber to bit-62-clear values (e.g. high bytes `0x3E`/`0x3F`), one-line changes plus their comparison sites.
- **Enforce forever:** `static_assert(LMD_TYPE_COUNT < 0x40)` next to the enum, a comment stating the invariant, and a lint rule (§4, S0) so no future sentinel re-enters the reserved space.

## 2.4 What each primitive becomes

- **`get_type_id(item)`** — prepend one branch **before** everything else (including the container header-dereference path): `if (item & BIT62) return LMD_TYPE_FLOAT;`. This is the hottest primitive in the runtime; the test is one AND against a constant and is highly predictable per call site.
- **`it2d`** — bit-62 test → subtract + bitcast on the hot arm; masked dereference on the cold arm. In C this is free; in emitted MIR there is no int↔float bitcast instruction, so the JIT lowering uses a store/load through a scratch frame slot (~4–6 cycles of store-forwarding — still ~2 orders cheaper than the current call + cache-missing dereference). Patching a `bitcast` insn into vendored MIR is a possible later micro-win, **not** v1 scope.
- **Boxing sites** — `jm_box_float` (`js_mir_calls_boxing_types.cpp:406`), the overflow-promotion arm of `jm_box_int_reg` (`:356`), Lambda-side `push_d` emission, and `js_make_number` (`js_runtime_value.cpp:1071`) become inline `ADD + BT(bit62)` with a cold call to the fallback. The `push_d` C call disappears from all in-band traffic.
- **Type-switch sites** — any `switch (get_type_id(v))` is correct once `get_type_id` normalizes. Only code reading the **raw high byte** directly needs the §3.3 audit (measured: ~24 `>> 56` sites across 11 files — small).
- **`d2it(ptr)` (the pointer-tagging macro)** stays, serving the boxed fallback arm. New value-level primitives `flt2it(double)` / updated `it2d(Item)` become the canonical API.

## 2.5 Special values — interned, not allocated

±0.0 is extremely common but out-of-band (`e = 0`). Do **not** allocate: intern static immutable payloads and return the same tagged-pointer Items forever:

- `ITEM_FLOAT_POS0`, `ITEM_FLOAT_NEG0`, `ITEM_FLOAT_NAN`, `ITEM_FLOAT_POS_INF`, `ITEM_FLOAT_NEG_INF` — five process-lifetime constants (statically allocated payloads outside the GC heap, like the interned ASCII-char strings, `lambda-mem.cpp:202`).
- **The zero fallback must be sign-aware**: pick `NEG0` vs `POS0` by the sign bit, or minus-zero semantics silently vanish. `js_make_number`'s existing minus-zero guard carries over.
- With specials interned, the *allocating* fallback is reached only for subnormals and magnitudes beyond 10^±154 — negligible in real workloads.

## 2.6 The canonical-encoding invariant

> An in-band double is **always** encoded inline, never boxed; each special value is **always** its interned singleton. Every producer enforces this: `js_make_number`, `push_d` wrappers, both JIT lowerings, input parsers / MarkBuilder, `it2d` round-trip helpers.

Without this, one value has two representations and raw-bit comparisons diverge. Note the current baseline is *worse* — equal boxed floats are already pointer-distinct (cf. the known ArrayNum `==` representation-sensitivity) — so canonicalization strictly improves bit-comparability. The risk lives only in a *mixed* transition state, which the compile-time flag (§4, S1) confines.

## 2.7 GC interaction

- **In-band floats never touch the GC.** No allocation, no nursery growth (this also caps the never-reclaimed numeric-nursery leak, §1.4), no relocation, no rooting — an inline double is a non-pointer. The Part 3 rooting machinery and `gc_fixup_embedded_pointers` simply stop applying to them; boxed-float traffic collapses to the special/extreme residue.
- **Marking:** tag-switching mark code never treats bit-62 high bytes as pointer tags. One wart to tighten: `item_to_ptr` (`gc_heap.c:770`) treats unknown high tags ≥ `LMD_TYPE_RANGE_` as raw pointers "for safety" — inline doubles land in that branch and get (harmlessly) probed against heap ranges on every conservative-scan word. Add an early `if (item & BIT62) return NULL;` — correctness-neutral, saves probe work.
- **Conservative stack scan:** an inline double whose low bits alias a heap address could false-retain an object — the same benign over-approximation packed int53 already causes today (the conservative scan only marks, never rewrites).
- **`gc_fixup_embedded_pointers`** (`gc_heap.c:1202`) keys on tag bytes `FLOAT/INT64/DTIME`; inline doubles fall outside those tags and are correctly skipped — verify, don't assume (S3 audit).

## 2.8 Advantages over each prior scheme

| vs. | Advantage of high-byte self-tagging |
|---|---|
| **v1 boxed doubles** | Boxing cost at every boundary (array element, argument, return, untyped local, property) drops from C-call + allocation + later cache-missing dereference to ~2 ALU ops. GC-trigger pressure and the nursery leak drop to the special-value residue. Equal values become bit-identical (fixes a whole class of representation-sensitivity). |
| **NaN-boxing** | Keeps **all** of Lambda's architectural assets: 256-value uniform tag space (20+ types, `NUM_SIZED` inline sub-typed scalars), int53 compact band, containers as bare 56-bit pointers, and every existing `it2*`/`*2it` packing. NaN-boxing would force a ~50 % runtime refactor across four frontends, the GC, and every container API for benefit in exactly one type category. Self-tagging is a local carve-out: nothing that doesn't opt in changes. |
| **V8's strategy alone** (typed storage + unboxed native paths) | Complementary, not competing — but self-tagging fixes the *universal* representation, which V8 never has to because its feedback JIT hides it. Lambda's static inference cannot promise that, so the fallback representation must be cheap. Typed storage (T5 element kinds, shaped float slots) still wins inside kernels; self-tagging covers everything those can't reach. |
| **The paper's rotation scheme** | No rotation instructions at all (the tag byte is already where the predictable bits live); discrimination is **one bit test** instead of a rotate + tag-range compare; the low-bit-tag sacrifices the paper assumes (loss of the clean tag byte, of int56-class packing, of aligned-pointer self-tagging) simply don't arise. Wider band too: bias by `0x200` covers 10^±154 vs. the paper's narrower common band. |

## 2.9 Honest costs

- **Encode/decode ALU tax:** ~2 ops per float boundary crossing, plus the MIR bitcast-via-slot dance in JIT code. Orders of magnitude cheaper than allocation + dereference, but nonzero on paths where the value was *already* unboxed native (`MIR_T_D` registers) — those paths don't change and must not accidentally start round-tripping through Items.
- **One extra predictable branch in `get_type_id`** — the hottest primitive; measured A/B on float-light workloads (richards, splay) is the honest check, and the compile flag confines the experiment.
- **Two float representations, permanently.** Every float consumer keeps a boxed arm (specials + extremes). The cold arm is the *existing* code, so this is additive — but it doubles the float test matrix (in-band / out-of-band / specials) forever. §4's property tests are the mitigation.

---

# Part 3 — Potential Issues (ranked)

## 3.1 Raw-bit `MIR_EQ` on Items in JIT-lowered code — the sharpest edge

The JS transpiler emits raw integer `MIR_EQ`/`MIR_BEQ` comparisons on Item words at a dozen-plus sites (`js_mir_expression_lowering.cpp:1519`, `:1562`, `:1575`, `:5560`, `:6590`, …). Today two equal boxed floats are **never** raw-equal (distinct payload pointers), so any such site that can see floats currently answers "not equal" and falls through to a slow path. Self-tagging changes the answers twice over:

- two equal in-band doubles become raw-**equal** — usually *more* correct, but it changes which path executes;
- the interned singletons break both directions: `NaN === NaN` becomes raw-*true* (must be false), and `0.0 === -0.0` compares two *distinct* interned pointers raw-*false* (must be true).

The C-runtime path is safe — `js_strict_equal` (`js_runtime_value.cpp:1522`) unboxes via `js_get_number` and handles NaN explicitly (`:1541`, `:1571`). The exposure is entirely in inline-emitted fast paths. **Every raw-Item-equality emission site must be audited: prove it float-free, or route through `MIR_DEQ` / the runtime helper.** This audit belongs in S0 and is *not* covered by grepping for `>> 56`.

## 3.2 `get_type_id` ordering and the container path

The bit-62 test must come **first** — before the tag-0 container branch dereferences the object header. Getting the order wrong doesn't just cost cycles; a bit-62 word interpreted as a container pointer is a wild dereference. One-line rule, worth a dedicated unit test.

## 3.3 The raw high-byte reader tail

Measured surface: ~24 `>> 56` extractions across 11 files (plus the packing macros in `lambda.h` and mask-only `& 0x00FF…` sites). Each must classify as: (a) already downstream of `get_type_id` normalization — fine; (b) comparing against specific tags — fine iff it can never see a float Item; (c) genuinely needs the bit-62 test added. Same audit for the Jube guest runtimes (`lambda/py/`, etc.) and any private copies of the macros. Small but must be exhaustive — this is the S0 deliverable.

## 3.4 NaN payload canonicalization

Round-tripping a NaN with a custom payload through an Item (e.g. read from a `Float64Array`, store into an object field, read back) now collapses it to the canonical interned NaN. The JS spec permits implementation-defined NaN canonicalization (V8 canonicalizes too), but bit-twiddling test262 cases can observe it — add a targeted test and accept the V8-aligned behavior. Lambda-side semantics (`Lambda_Formal_Semantics.md` §4) treat NaN as one value; no conflict.

## 3.5 Minus-zero discipline at the fallback

All zeros are out-of-band, so `-0.0` correctness lives entirely in the cold path: sign-aware singleton selection (§2.5), and SameValueZero contexts (JS `Map`/`Set` keys) normalizing −0 → +0 before hashing. Both are "don't regress" items — boxed floats are pointer-distinct today, so hashing already unboxes — but they're easy to lose in the rewrite.

## 3.6 In-place payload mutation — verified non-issue

Audited: every `*(double*)… =` write in the tree targets a typed **field slot** in shaped map storage (`transpile-mir.cpp:1659`, `lambda-eval.cpp:5793`, `mark_editor.cpp:356`, `input.cpp:108`, …), never a shared boxed-float payload. Lambda numbers are immutable values with no identity, so copy-by-value encoding is semantically invisible. This is the property that makes the whole scheme correct — re-verify it holds whenever a new numeric mutation path lands.

## 3.7 `LMD_TYPE_FLOAT64` — a second binary64 TypeId collides with canonicalization

`LMD_TYPE_FLOAT64` exists as a distinct boxed TypeId with its own packer (`f642it`, `lambda.h:912`) for explicit `f64` literals/annotations. Number model v2 §3.2 already rules `f64` a **pure alias** of `float` ("aliases in, canon out" — `type()` never returns `f64`). Two runtime TypeIds for one value domain breaks the canonical-encoding invariant (§2.6): the same double could be an inline FLOAT or a boxed FLOAT64. **Resolution: retire `LMD_TYPE_FLOAT64` as a runtime TypeId** (keep `f64` as a parse-time alias resolving to FLOAT) **before or with S1**. Touch points: `f642it` producers, `is_numeric_type_id` (`lambda.h:882`), any switch cases on `LMD_TYPE_FLOAT64`. This aligns with the number-model migration schedule (its Part 2 W-items) — coordinate rather than duplicate.

## 3.8 Cross-frontend and interpreter coverage

`get_type_id`/`it2d` changes cover the C runtime and the MIR-interpreter path automatically (the interpreter executes the same lowered MIR; a mem-op is a mem-op). The explicit work is in the two JIT lowerings (`jm_box_float` / `jm_emit_unbox_float` on the JS side; `push_d` emission on the Lambda side). The Python/Bash/Ruby Jube runtimes consume Items through the shared headers — audit for any local high-byte arithmetic (S0 grep covers them).

## 3.9 Transition state

A build where some producers inline and some still box violates §2.6 and makes raw-bit comparisons diverge nondeterministically. The `LAMBDA_SELF_TAG_FLOAT` compile flag makes the transition atomic per build; never ship a mixed state. Persisted Items don't exist (no Item serialization), so there is no cross-version data concern.

---

# Part 4 — Interaction with the 53-bit compact `int` (number model v2)

The compact `int` band is **±(2⁵³−1)** — deliberately the JS safe-integer band (`INT56_MAX`, `lambda.h:896`; number model v2 §2.1) — packed in the low 56 bits with high byte = `LMD_TYPE_INT` (`i2it`, `lambda.h:904`). Findings:

1. **No packing change needed.** The `& 0x00FFFFFFFFFFFFFF` mask confines sign extension to the payload; the high byte is always `LMD_TYPE_INT` (< `0x40`), so packed ints — positive and negative — are bit-62-clear and compliant with §2.3 by construction.
2. **`int → float` conversion becomes allocation-free.** The band was chosen so every compact int is exactly representable in binary64; with self-tagging, the converted double is always in-band (integers up to 2⁵³ have exponents ≤ 0x434). What is today `I2D + push_d` (allocation) becomes `I2D + ADD + BT` (~3 ALU ops). This completes the number model's egress story: `int → number` is *exact* by design and now also *free*.
3. **The int-overflow promotion arm stops allocating.** `jm_box_int_reg`'s out-of-range arm (`js_mir_calls_boxing_types.cpp:356`) promotes to a boxed float via `push_d` — the only allocation in the int packing path. It becomes an inline encode; the results (magnitudes just past 2⁵³) are comfortably in-band.
4. **JS ingress needs nothing new.** Number model v2 makes JS numbers uniformly `float` (compact-int packing already removed from LambdaJS, §5.1) — which *increased* boxed-double traffic and thus the prize here. Safe-integer-valued JS numbers arrive as doubles and self-tag like any other.
5. **Type semantics untouched.** `int` and `float` remain distinct runtime types (`type(5)` = `int`; `5 is float` is the §3.4 subsumption question, answered at the type level, not by representation). No temptation to encode ints as inline doubles — that would erase the `int`/`float` distinction the semantics ADR requires.
6. **The one real change is §3.7** — retiring `LMD_TYPE_FLOAT64`, which the number model's alias rule already implies; this proposal just makes it a hard prerequisite.

---

# Part 5 — Implementation Plan

## 5.1 Phases

| Phase | Content | Risk | Gate |
|---|---|---|---|
| **S0** — occupancy + comparison audit *(land independently; zero behavior change)* | Renumber `JS_DELETED_SENTINEL_VAL` / `JS_ITER_DONE_SENTINEL` to bit-62-clear values + their comparison sites. Add `static_assert(LMD_TYPE_COUNT < 0x40)` + invariant comment on the TypeId enum. Sweep all raw `>> 56` / high-byte-mask sites (≈24 sites / 11 files) and classify per §3.3. **Audit every raw-Item `MIR_EQ`/`MIR_BEQ` emission site (§3.1): float-free proof or `MIR_DEQ` reroute plan.** Grep Jube runtimes for private tag arithmetic. Confirm GC-internal header tags never materialize into Item high bytes. | none | full baselines green (`make test-lambda-baseline`, JS gtests, Radiant) |
| **S1** — runtime level, behind `LAMBDA_SELF_TAG_FLOAT` | `get_type_id` bit-62 fast path (ordered first); `it2d`/`flt2it` two-arm forms; `js_make_number` + `push_d` wrappers canonicalize; interned specials (§2.5) with sign-aware zero; **retire `LMD_TYPE_FLOAT64`** (§3.7, coordinated with number-model W-items); producer canonicalization in parsers/MarkBuilder. | medium | test262 full + `make test-lambda-baseline` + Radiant baseline, **flag on vs. off**; new property tests (§5.2) |
| **S2** — JIT lowering fast paths | Inline encode at `jm_box_float`, `jm_box_int_reg` overflow arm, Lambda-side `push_d` emission; inline decode at `jm_emit_unbox_float` / Lambda unbox sites (bitcast via scratch slot); apply the §3.1 `MIR_DEQ` reroutes decided in S0. | medium | same gates + benchmark A/B (nbody, mandelbrot, navier_stokes, matmul, splay float keys; richards/splay as the float-light branch-cost canaries), JIT **and** MIR-interp modes |
| **S3** — GC audit + hardening; default the flag on | §2.7 verification pass (`gc_fixup_embedded_pointers` skip, `item_to_ptr` bit-62 early-out, conservative-scan behavior); ASan run of the full benchmark suite; NaN-payload canonicalization test (§3.4); then flip the default. | low | clean ASan, no baseline regressions, flag-off build kept working one release as escape hatch |

## 5.2 Test & tooling additions

- **Property tests** at the encode/decode boundary: round-trip identity over the band edges (`e` = 0x1FF/0x200/0x5FF/0x600), subnormals, ±0.0, ±Inf, quiet/signaling NaN payloads, and randomized in-band doubles (`bits⁻¹((bits(d)+BIAS)−BIAS) == d` bit-exactly; sign preservation for negatives). Table-driven, both engines.
- **Semantics pins:** `NaN === NaN` false, `0.0 === -0.0` true, `Object.is(-0, 0)` false, `Map` SameValueZero with ±0 and NaN keys, `1/-0.0 === -Infinity` — under flag on and off.
- **Lint ratchet** (precedent: `no-int-cast-radiant`): flag any new integer constant literal with bit 62 set outside the float module, and any new `>> 56` comparison against a literal not going through the TypeId enum. Run in `make lint`.
- **`get_type_id` ordering test:** a synthetic in-band double Item must return `LMD_TYPE_FLOAT` without touching memory (assert no header read — e.g. an Item whose low bits alias an unmapped address).

## 5.3 Sequencing within the tuning program

Per the tuning-proposal review: **T0/L0 re-measure → S0 (land now) → S1 → S2 + A/B → re-profile**, then re-rank the remaining memory items with floats out of the picture — T5 (still wins inside numeric kernels: raw `MIR_T_D` element loads, SIMD layout, no per-element encode), Part 2 stack boxing (int64/datetime residual only — self-tagging subsumes its float case), and the L4/T4 nursery redesign (now a small-population cleanup; the numeric-nursery leak residue is int64/datetime/decimal only). T1/T2/T3 (ICs, call path, literal shapes) are orthogonal and proceed in parallel throughout.

## 5.4 Fallback

If S2 measures worse than expected (branch cost in `get_type_id` on float-light code), the flag confines the experiment and the build reverts cleanly. The S0 sentinel renumbering, the static_assert/lint invariant, the `MIR_EQ` audit results, and the `LMD_TYPE_FLOAT64` retirement are all worth keeping regardless of the outcome.

---

# Part 6 — Cross-references

- `Lambda_Tuning_Proposal.md` — Part 1 (Wall 1/Wall 4 sizing), Part 2 (stack boxing — float case subsumed by this doc), Part 3 (rooting — inline floats need none), Part 4 (the tagging-vs-NaN-boxing verdict this design implements), Part 5 (the sketch this doc supersedes).
- `Lambda_Semantics_Number_Model.md` — §2.1 (compact band = safe-integer band), §3.2 (`f64` alias rule → §3.7 here), §5.1/§5.4 (JS number ≡ float; compact-int packing removal), Part 2 W-items (migration coordination).
- `doc/Lambda_Formal_Semantics.md` §4 — numeric semantics ADR (value domain unchanged by this proposal; representation only).
- `doc/dev/js/JS_03_Value_Model.md`, `JS_04_MIR_Lowering.md` — the value-model and lowering docs to update at S2.
- `lib/gc/` — `gc_nursery.h:15` (the never-reclaimed nursery this shrinks), `gc_heap.c:770` (`item_to_ptr`), `:1202` (`gc_fixup_embedded_pointers`).
