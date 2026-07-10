# Lambda Double Boxing v3 — Inline Doubles via High-Byte Float Self-Tagging

- **Status:** PROPOSED — reviewed (Part 7); implementation plan at `Lambda_Double_Boxing_Impl_Plan.md`
- **Date:** 2026-07-10
- **Co-Author:** Anthropic Fable
- **Scope:** the runtime `Item` representation of `float` (binary64) across Lambda core, LambdaJS, the MIR transpilers, and the GC. Supersedes and expands **Part 5** of `Lambda_Tuning_Proposal.md`.
- **Convention:** `file:line` references drift; confirm against the symbol name.
- **Related:** `Lambda_Design_Item_Boxing.md` (**the Item-representation design record this extension must preserve** — tagged scalar leaves, raw container pointers, and the invariants in its §6), `Lambda_Tuning_Proposal.md` (Parts 1–5 — the engine-wide analysis this fix comes from), `Lambda_Semantics_Number_Model.md` (number model v2 — `int` compact band, `f64` alias rule), `doc/dev/js/JS_03_Value_Model.md`, `doc/Lambda_Formal_Semantics.md` §4 (numerics ADR).

## 0. Summary

Today every Lambda/LambdaJS `float` that exists as an `Item` is a tagged **pointer** to a heap-allocated 8-byte payload. This proposal stores effectively all real-world doubles **directly inside the 64-bit `Item` word** — no allocation, no dereference, no GC involvement — while keeping Lambda's high-byte tag scheme, int53 compact packing, containers-as-bare-pointers, and the 256-value tag space untouched.

The mechanism adapts the published "Float Self-Tagging" technique to Lambda's layout. Because Lambda's tag byte already occupies the bits where a double's sign and top exponent bits live, the adaptation needs **no rotation, no bias, and no shift**: an in-band double is stored as its **raw IEEE-754 bits** — the Item *is* the double — and discrimination is a **single mask test**: `(item & 0x6000_0000_0000_0000) != 0`, i.e. "either of the exponent's top two bits is set" (`e ≥ 0x200`). Encode = the mask check only (the stored value is untransformed); decode = a **pure bitcast**. The tag-space rule becomes: every non-double encoding keeps high-byte bits 6 and 5 **both clear** (high bytes `0x00–0x1F ∪ 0x80–0x9F`). Coverage is **all magnitudes ≥ ≈1.5·10⁻¹⁵⁴, including ±Inf and NaN**; ±0.0 is handled as a **packed immediate** (no memory, §2.5), leaving only subnormals and underflow-tiny values on the allocating fallback. (Two stricter variants — a two-bit XOR test and a bias-add — are retained as alternatives, §2.9.)

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

## 2.1 The no-rotation, no-bias, single-mask observation

Lambda's tag is the **high byte** — exactly where a double's sign bit (63) and exponent (62:52) already live. The paper rotates to move a double's predictable high bits down to the low-bit tag position; in Lambda **they are already in the tag position**. So no rotation is needed.

Can detection be a plain mask test on the *raw* bits? Two facts bound the design space:

- A single mask-compare `(x & M) == C` recognizes only a *subcube* (a set where the masked bits take one fixed pattern). The symmetric band `e ∈ [0x200, 0x5FF]` is "exponent bits 10, 9 **differ**" — an XOR set, not a subcube — so it cannot be one mask test. (A bias-add's carry, or a shift+XOR, is the minimum machinery for *that* band.)
- But **widening the band to `e ∈ [0x200, 0x7FF]`** — "exponent bit 10 **or** bit 9 set", `e ≥ 0x200` — *is* a subcube complement, recognized by one mask test: `(x & 0x6000…) != 0`. And the widened band is strictly better: it additionally inlines all huge magnitudes, **±Inf, and NaN**, leaving only zeros, subnormals, and underflow-tiny values (< 2⁻⁵¹¹) on the fallback path.

So the simplest scheme is also the widest: store raw bits, test two exponent bits with one AND.

## 2.2 Encoding

Let `DBL_MASK = 0x6000_0000_0000_0000` (word bits 62 and 61 = exponent bits 10 and 9).

```c
// discrimination — ONE mask test:
is_inline_double(x)  ⟺  (x & DBL_MASK) != 0        // i.e. e ≥ 0x200

// encode: double → Item          (replaces push_d at boxing sites)
uint64_t u = bits(d);              // bit-identical reinterpret
if (u & DBL_MASK)  item = u;               // in-band: stored RAW — the Item IS the double
else               item = box_float_cold(d);   // rare: zero → packed immediate; subnormal/tiny → boxed (§2.5)

// decode: Item → double          (replaces the it2d masked dereference)
if (item & DBL_MASK)  d = bits⁻¹(item);              // pure bitcast, zero arithmetic
else                  d = it2d_cold(item);   // packed-zero check, then boxed dereference (§2.5)
```

### Why the arithmetic is exactly right (verified by hand)

- The exponent field is bits 62:52, so `e ≥ 0x200` ⟺ exponent bit 10 or bit 9 set ⟺ word bit 62 or 61 set ⟺ `(u & DBL_MASK) != 0`. No carries, no shifts, no sign-bit interaction — positives and negatives are symmetric.
- Out-of-band doubles (`e < 0x200`: ±0.0, subnormals, magnitudes < 2⁻⁵¹¹) fail the check and are **never stored raw** — so although their high bytes would alias TypeId space (a subnormal's high byte is `0x00–0x1F`!), no such bit pattern ever exists as an Item. The cold path interns or boxes them (§2.5).
- On ARM64 the discriminator is **one instruction**: `0x6000…` is a valid logical immediate (a run of two ones), so `tst x, #0x6000000000000000` + `b.ne` — no constant materialization at all. On x86-64 the constant is hoisted once (`movabs`) and each site is `test + jnz`. In MIR: one `AND`-with-immediate + one branch.

| Input double | `e` | raw high byte | bits 62,61 | Result |
|---|---|---|---|---|
| `1.0` (`0x3FF0…`) | 0x3FF | 0x3F | 01 | inline (raw) |
| `-1.0` (`0xBFF0…`) | 0x3FF | 0xBF | 01 | inline (raw) |
| `2.0` (`0x4000…`) | 0x400 | 0x40 | 10 | inline (raw) |
| `2⁻⁵⁰⁰` | 0x20B | 0x20 | 01 | inline (raw) |
| `1e300` | ~0x7E3 | 0x7E | 11 | inline (raw) |
| `±Inf`, `NaN` | 0x7FF | 0x7F / 0xFF | 11 | **inline (raw)** |
| `±0.0` | 0x000 | 0x00 / 0x80 | 00 | **packed immediate (§2.5)** — no memory |
| `1e-300`, subnormals | 0x000–0x1FF | 0x00–0x1F / 0x80–0x9F | 00 | boxed (only remaining allocation) |

### Coverage

`e ∈ [0x200, 0x7FF]` → **every double with magnitude ≥ 2⁻⁵¹¹ (≈1.5·10⁻¹⁵⁴), with no upper limit — including ±Inf and every NaN payload**. The paper's >99 % measured self-tag rate becomes effectively **100 %** here. ±0.0 is out-of-band but **packed as an immediate scalar** (§2.5) — no memory involvement — so the *allocating* fallback shrinks to subnormals and nonzero magnitudes below 10⁻¹⁵⁴ only. Consequences: Inf/NaN-producing arithmetic (overflow, 0/0) and all zero handling stay allocation-free, and NaN payload bits are preserved exactly.

## 2.3 The occupancy invariant (two masked bits partition the tag space)

> **Invariant:** high-byte bit 6 or bit 5 set ⟺ the Item is an inline self-tagged double. Every TypeId, sentinel, and packed encoding keeps high-byte bits 6 and 5 **both clear** — high bytes stay within `0x00–0x1F ∪ 0x80–0x9F` (64 values; the price of the single-mask test and the widened band — still 2.3× the ~28 tags in use).

Inline doubles occupy the high bytes `0x20–0x7F` (positive, incl. +Inf/NaN) and `0xA0–0xFF` (negative). Audit of current occupancy:

- **TypeId enum** (`lambda.h:83`–`:123`) — sequential from 0; counted: `LMD_TYPE_COUNT` = 27 (`0x1B`), `LMD_CONTAINER_HEAP_START` = 28 (`0x1C`). ✓ Compliant with the `0x20` wall, with **three spare sequential slots** (`0x1D–0x1F`); type 29+ must jump to the second free block (`0x80–0x9F`) via explicit enum values.
- **Containers** — bare pointers, high byte `0x00` on canonical user-space addresses. ✓ Even under Linux LA57 (57-bit VAs), user addresses reach bit 56 at most — high byte ≤ `0x01`, bits 6,5 = 00. ✓
- **Packed layouts** — `ITEM_INT` int53-in-56 (high byte = `LMD_TYPE_INT` = 5; sign extension confined to the low 56 bits by the `& 0x00FF…` mask in `i2it`, `lambda.h:904`), `NUM_SIZED` (`lambda.h:221`), bool/null/undefined/TDZ/spreadable-null (`lambda.h:871`–`:892`): all keep their TypeId (< 0x20) in the high byte. ✓ **JS Symbols** are packed as `LMD_TYPE_INT`-tagged negative ints (`JS_SYMBOL_BASE`, `js_runtime.h:810`) — high byte 5, compliant. ✓
- **✗ Two violations to renumber:** `JS_DELETED_SENTINEL_VAL = 0x7E…` (`js_runtime.h:30`) and `JS_ITER_DONE_SENTINEL = 0x7F…` (`js_runtime.h:35`) have bits 6,5 = 11 and would read as inline doubles (they'd decode as ~1e300-class values). Both are engine-internal constants chosen precisely *because* their tag bytes were unused — renumber to free high bytes (e.g. `0x1E`/`0x1F` or `0x9E`/`0x9F`), one-line changes plus their comparison sites.
- **Enforce forever:** next to the enum — `static_assert(LMD_TYPE_COUNT <= 0x20)` and a comment stating the bits-6,5-clear rule; compile-time asserts on the two sentinels' high bytes (`(hb & 0x60) == 0`); and a lint rule (§5.2, S0) so no future tag or sentinel enters double space.

## 2.4 What each primitive becomes

- **`get_type_id(item)`** — prepend one branch **before** everything else (including the container header-dereference path): `if (item & DBL_MASK) return LMD_TYPE_FLOAT;`. This is the hottest primitive in the runtime; the test is a single AND-with-constant (one `tst`-immediate on ARM64) and highly predictable per call site.
- **`it2d`** — the same mask test → **pure bitcast** on the hot arm (zero arithmetic — the Item bits *are* the double bits); masked dereference on the cold arm. In C the bitcast is free; in emitted MIR there is no int↔float bitcast instruction, so the JIT lowering uses a store/load through a scratch frame slot (~4–6 cycles of store-forwarding — still ~2 orders cheaper than the current call + cache-missing dereference). Patching a `bitcast` insn into vendored MIR is a possible later micro-win, **not** v1 scope.
- **Boxing sites** — `jm_box_float` (`js_mir_calls_boxing_types.cpp:406`), the overflow-promotion arm of `jm_box_int_reg` (`:356`), Lambda-side `push_d` emission, and `js_make_number` (`js_runtime_value.cpp:1071`) become inline `AND + BT` on the raw bits with a cold call to the fallback — and the stored value is the raw bits themselves, so the check is entirely off the critical path of the value (better ILP than the bias form, where the ADD produces the stored word). The `push_d` C call disappears from all in-band traffic. A pleasant side effect: compile-time float constants embed in MIR as their literal IEEE bit patterns, directly readable in `temp/mir_dump.txt`.
- **Type-switch sites** — any `switch (get_type_id(v))` is correct once `get_type_id` normalizes. Only code reading the **raw high byte** directly needs the §3.3 audit (measured: ~24 `>> 56` sites across 11 files — small).
- **`d2it(ptr)` (the pointer-tagging macro)** stays, serving the boxed fallback arm. New value-level primitives `flt2it(double)` / updated `it2d(Item)` become the canonical API.

## 2.5 Special values — packed or inline, never allocated

**±Inf and NaN are in-band under the widened mask** (`e = 0x7FF` → bits 62,61 = 11) — they self-tag like any other double, payload bits preserved exactly. No interning, no allocation, no canonicalization on the value path. The only out-of-band doubles are `e < 0x200`: ±0.0, subnormals, and nonzero magnitudes < 2⁻⁵¹¹.

**±0.0 — packed immediates, no memory at all.** Zero is too common in float arithmetic (accumulator init, comparisons, defaults) to pay any fallback cost. Storing it *raw* in-band is **provably impossible**: `+0.0`'s bit pattern is the all-zero word — already `ITEM_UNDEFINED`, the load-bearing "absent/zeroed-memory" sentinel (calloc'd buffers, empty slots, map misses) — and no mask test can accept it anyway (`(x & M) != 0` rejects the zero word by definition; the `== C` form forces `C = 0`, contradicting in-band doubles *setting* bits 62/61; and splitting ±0 from subnormals, both `e = 0`, would need all 52 mantissa bits examined). So ±0 gets the next-best representation — a **packed scalar**, exactly like `int`/`bool`:

- `ITEM_FLOAT_P0 = (LMD_TYPE_FLOAT << 56) | 0` and `ITEM_FLOAT_N0 = (LMD_TYPE_FLOAT << 56) | 1`. Both encodings are provably unclaimed today — `d2it` null-guards (`lambda.h:911`), so a FLOAT-tagged Item with payload 0 or 1 cannot currently exist.
- **No allocation, no interned statics, no dereference** — zero is a pure register value. `get_type_id` needs no change (the FLOAT high byte takes the normal tag path). `it2d`'s cold arm (`lambda.h:1314`, one function — the single choke point) adds one compare: `p = item & MASK56; if (p <= 1) return p ? -0.0 : +0.0; return *(double*)p;` — addresses 0/1 are never valid payloads, so the test is airtight.
- **Sign lives in payload bit 0**, so minus-zero semantics are explicit by construction, and SameValueZero/hash normalization of −0 → +0 is literally "clear bit 0". `js_make_number`'s existing minus-zero guard maps onto the two constants.
- Encode cold path: `if (d == 0.0) return ITEM_FLOAT_P0 | signbit(d);` — v1 keeps this inside the cold C helper; inlining it at JIT boxing sites is a later micro-win if zero-heavy profiles justify it.

With zeros packed, the **allocating** fallback is reached only for subnormals and nonzero magnitudes below 10⁻¹⁵⁴ (61 bits of sign+exponent+mantissa can't fit a 56-bit payload, so these stay boxed) — negligible in real workloads; underflow-heavy numeric kernels are the only plausible producer.

## 2.6 The canonical-encoding invariant

> An in-band double is **always** encoded inline, never boxed; ±0.0 is **always** its packed immediate (`ITEM_FLOAT_P0`/`_N0`), never boxed. Every producer enforces this: `js_make_number`, `push_d` wrappers, both JIT lowerings, input parsers / MarkBuilder, `it2d` round-trip helpers.

Without this, one value has two representations and raw-bit comparisons diverge. Note the current baseline is *worse* — equal boxed floats are already pointer-distinct (cf. the known ArrayNum `==` representation-sensitivity) — so canonicalization strictly improves bit-comparability. The risk lives only in a *mixed* transition state, which the compile-time flag (§4, S1) confines.

## 2.7 GC interaction

- **In-band floats never touch the GC.** No allocation, no nursery growth (this also caps the never-reclaimed numeric-nursery leak, §1.4), no relocation, no rooting — an inline double is a non-pointer. The Part 3 rooting machinery and `gc_fixup_embedded_pointers` simply stop applying to them; boxed-float traffic collapses to the special/extreme residue.
- **Marking:** tag-switching mark code never treats double-space high bytes as pointer tags. One wart to tighten: `item_to_ptr` (`gc_heap.c:770`) treats unknown high tags ≥ `LMD_TYPE_RANGE_` as raw pointers "for safety" — inline doubles land in that branch and get (harmlessly) probed against heap ranges on every conservative-scan word. Add an early `if (item & DBL_MASK) return NULL;` — correctness-neutral, saves probe work.
- **Conservative stack scan:** an inline double whose low bits alias a heap address could false-retain an object — the same benign over-approximation packed int53 already causes today (the conservative scan only marks, never rewrites).
- **`gc_fixup_embedded_pointers`** (`gc_heap.c:1202`) keys on tag bytes `FLOAT/INT64/DTIME`; inline doubles fall outside those tags and are correctly skipped — verify, don't assume (S3 audit).

## 2.8 Advantages over each prior scheme

| vs. | Advantage of high-byte self-tagging |
|---|---|
| **v1 boxed doubles** | Boxing cost at every boundary (array element, argument, return, untyped local, property) drops from C-call + allocation + later cache-missing dereference to ~2 ALU ops. GC-trigger pressure and the nursery leak drop to the special-value residue. Equal values become bit-identical (fixes a whole class of representation-sensitivity). |
| **NaN-boxing** | Keeps **all** of Lambda's architectural assets: 256-value uniform tag space (20+ types, `NUM_SIZED` inline sub-typed scalars), int53 compact band, containers as bare 56-bit pointers, and every existing `it2*`/`*2it` packing. NaN-boxing would force a ~50 % runtime refactor across four frontends, the GC, and every container API for benefit in exactly one type category. Self-tagging is a local carve-out: nothing that doesn't opt in changes. |
| **V8's strategy alone** (typed storage + unboxed native paths) | Complementary, not competing — but self-tagging fixes the *universal* representation, which V8 never has to because its feedback JIT hides it. Lambda's static inference cannot promise that, so the fallback representation must be cheap. Typed storage (T5 element kinds, shaped float slots) still wins inside kernels; self-tagging covers everything those can't reach. |
| **The paper's rotation scheme** | No rotation, no bias, no shift: the tag byte is already where the predictable bits live, and in-band doubles are stored as their raw IEEE bits — encode/decode transform nothing. Discrimination is one AND-with-mask instead of a rotate + tag-range compare; the low-bit-tag sacrifices the paper assumes (loss of the clean tag byte, of int53-class packing, of aligned-pointer self-tagging) simply don't arise. Far wider band too: everything ≥ 10⁻¹⁵⁴ including ±Inf/NaN, vs. the paper's narrower common band. |

## 2.9 Alternatives considered: the two-bit XOR and bias variants

Three schemes were worked through in sequence, each strictly simpler than the last; all share raw-vs-boxed duality, non-allocating ±0 handling, and the S1–S3 plan:

| | **A: single-mask, wide band (primary)** | B: two-bit XOR, symmetric band | C: bias-add (Tuning Proposal Part 5 sketch) |
|---|---|---|---|
| stored form | raw IEEE bits | raw IEEE bits | `bits(d) + BIAS` (transformed) |
| encode transform | none | none | `ADD` (on the stored value's critical path) |
| decode transform | none — pure bitcast | none — pure bitcast | `SUB` |
| discrimination | `AND DBL_MASK` — **1 op** (`tst`-imm on ARM64) | `SHL+XOR+AND` (~2 ops) | `AND BIT62` (1 op, 64-bit constant) |
| in-band coverage | `e ≥ 0x200`: **all \|d\| ≥ 2⁻⁵¹¹ incl. ±Inf, NaN** | `e ∈ [0x200, 0x5FF]` | `e ∈ [0x200, 0x5FF]` |
| fallback population | ±0 (packed immediate, no memory); subnormals + \|d\| < 10⁻¹⁵⁴ boxed | + huge magnitudes, ±Inf, NaN (interned) | + huge magnitudes, ±Inf, NaN (interned) |
| non-double tag space | `0x00–0x1F ∪ 0x80–0x9F` (64 values) | 128 values, 4 fragments | `0x00–0x3F ∪ 0x80–0xBF` (128, contiguous) |
| sentinels `0x7E`/`0x7F` | renumber | compliant as-is | renumber |
| NaN payloads | preserved inline | collapsed to interned singleton | collapsed to interned singleton |

Why A wins: the symmetric band `e ∈ [0x200, 0x5FF]` is "exponent bits 10,9 differ" — an XOR set that no single mask-compare can recognize (B pays a shift+XOR; C pays an add/sub on every value crossing to convert it into one bit via carry). Widening to `e ≥ 0x200` turns the predicate into "bit 62 or 61 set" — one mask — **and** enlarges coverage: overflow-producing arithmetic (`1e300 * 1e300`), ±Inf, and NaN all stay allocation-free, and NaN payload bits survive round-trips (retiring what was issue §3.4). The costs are mild: tag space halves to 64 high-byte values (2.3× current use), and the two hole-squatting sentinels move (one-line renumbers).

Fall back to B only if the 64-value tag budget ever becomes a genuine constraint; C is dominated on every axis except contiguous enum headroom and is kept for the historical record.

## 2.10 Honest costs

- **Discriminator ALU tax:** one AND + branch per float boundary crossing and per `get_type_id` call (a single `tst`-immediate on ARM64), plus the MIR bitcast-via-slot dance in JIT code. Orders of magnitude cheaper than allocation + dereference, but nonzero on paths where the value was *already* unboxed native (`MIR_T_D` registers) — those paths don't change and must not accidentally start round-tripping through Items.
- **One extra predictable branch in `get_type_id`** — the hottest primitive; measured A/B on float-light workloads (richards, splay) is the honest check, and the compile flag confines the experiment.
- **Halved TypeId space:** 64 legal high-byte values (`0x00–0x1F ∪ 0x80–0x9F`) with 3 sequential slots left below the `0x20` wall; the 28th-plus future type takes an explicit value in `0x80–0x9F`. Still 2.3× current use, but the budget is now real — new-type reviews must check it (the static_assert enforces).
- **Two float representations, permanently.** Every float consumer keeps a boxed arm (specials + extremes). The cold arm is the *existing* code, so this is additive — but it doubles the float test matrix (in-band / out-of-band / specials) forever. §4's property tests are the mitigation.

---

# Part 3 — Potential Issues (ranked)

## 3.1 Raw-bit `MIR_EQ` on Items in JIT-lowered code — the sharpest edge

The JS transpiler emits raw integer `MIR_EQ`/`MIR_BEQ` comparisons on Item words at a dozen-plus sites (`js_mir_expression_lowering.cpp:1519`, `:1562`, `:1575`, `:5560`, `:6590`, …). Today two equal boxed floats are **never** raw-equal (distinct payload pointers), so any such site that can see floats currently answers "not equal" and falls through to a slow path. Self-tagging changes the answers twice over:

- two equal in-band doubles become raw-**equal** — usually *more* correct, but it changes which path executes;
- the specials break both directions: `NaN === NaN` becomes raw-*true* whenever the two NaNs carry the same payload bits — which hardware-produced quiet NaNs always do (must be false); and `0.0 === -0.0` compares two *distinct* packed constants (`ITEM_FLOAT_P0` vs `_N0`) raw-*false* (must be true).

The C-runtime path is safe — `js_strict_equal` (`js_runtime_value.cpp:1522`) unboxes via `js_get_number` and handles NaN explicitly (`:1541`, `:1571`). The exposure is entirely in inline-emitted fast paths. **Every raw-Item-equality emission site must be audited: prove it float-free, or route through `MIR_DEQ` / the runtime helper.** This audit belongs in S0 and is *not* covered by grepping for `>> 56`.

## 3.2 `get_type_id` ordering and the container path

The inline-double test must come **first** — before the tag-0 container branch dereferences the object header. Getting the order wrong doesn't just cost cycles; a double-space word interpreted as a container pointer is a wild dereference. One-line rule, worth a dedicated unit test.

## 3.3 The raw high-byte reader tail

Measured surface: ~24 `>> 56` extractions across 11 files (plus the packing macros in `lambda.h` and mask-only `& 0x00FF…` sites). Each must classify as: (a) already downstream of `get_type_id` normalization — fine; (b) comparing against specific tags — fine iff it can never see a float Item; (c) genuinely needs the inline-double test added. Same audit for the Jube guest runtimes (`lambda/py/`, etc.) and any private copies of the macros. Small but must be exhaustive — this is the S0 deliverable.

## 3.4 NaN as a value key — hashing, not canonicalization

Under the widened band NaN is inline and its payload bits are **preserved exactly** — the payload-loss concern of earlier variants dissolves (a `Float64Array` NaN round-trips bit-identically; spec-clean). The residual issue moves to **value-keyed containers**: JS `Map`/`Set` use SameValueZero, where *every* NaN is the same key — so a raw-bits hash would send two different-payload NaNs to different buckets while equality says they match. Fix at the hash boundary only: normalize any NaN (and −0 → +0, which SameValueZero also requires) to a canonical pattern before hashing. Boxed floats already force this discipline today (pointer-distinct equal values), so this is "keep the existing normalization," not new machinery — but add the NaN-with-payload-as-Map-key test to pin it. Lambda-side semantics (`Lambda_Formal_Semantics.md` §4) treat NaN as one value; no conflict.

## 3.5 Minus-zero discipline at the fallback

All zeros are out-of-band, so `-0.0` correctness lives entirely in the cold path: sign-aware packed-constant selection (`ITEM_FLOAT_P0 | signbit`, §2.5), and SameValueZero contexts (JS `Map`/`Set` keys) normalizing −0 → +0 before hashing — with the packed forms that normalization is literally "clear payload bit 0". Both are "don't regress" items — boxed floats are pointer-distinct today, so hashing already unboxes — but they're easy to lose in the rewrite. One new audit line: no existing code may treat a FLOAT-tagged Item with payload 0/1 as a pointer — `it2d_cold` is the single sanctioned reader; sweep for any open-coded `*(double*)(item & MASK56)`.

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

1. **No packing change needed.** The `& 0x00FFFFFFFFFFFFFF` mask confines sign extension to the payload; the high byte is always `LMD_TYPE_INT` (= 5, bits 6,5 = 00), so packed ints — positive and negative — sit outside double space and are compliant with §2.3 by construction.
2. **`int → float` conversion becomes allocation-free.** The band was chosen so every compact int is exactly representable in binary64; with self-tagging, every *nonzero* converted int is in-band (|n| ≥ 1 ⟹ `e ≥ 0x3FF`; a JIT that proves the operand nonzero may skip the check entirely), and zero maps to the packed immediate (§2.5). What is today `I2D + push_d` (allocation) becomes `I2D` + the mask check, with the resulting bits stored unchanged. This completes the number model's egress story: `int → number` is *exact* by design and now also *free*.
3. **The int-overflow promotion arm stops allocating.** `jm_box_int_reg`'s out-of-range arm (`js_mir_calls_boxing_types.cpp:356`) promotes to a boxed float via `push_d` — the only allocation in the int packing path. It becomes an inline encode; the results (magnitudes just past 2⁵³) are comfortably in-band.
4. **JS ingress needs nothing new.** Number model v2 makes JS numbers uniformly `float` (compact-int packing already removed from LambdaJS, §5.1) — which *increased* boxed-double traffic and thus the prize here. Safe-integer-valued JS numbers arrive as doubles and self-tag like any other.
5. **Type semantics untouched.** `int` and `float` remain distinct runtime types (`type(5)` = `int`; `5 is float` is the §3.4 subsumption question, answered at the type level, not by representation). No temptation to encode ints as inline doubles — that would erase the `int`/`float` distinction the semantics ADR requires.
6. **The one real change is §3.7** — retiring `LMD_TYPE_FLOAT64`, which the number model's alias rule already implies; this proposal just makes it a hard prerequisite.

---

# Part 5 — Implementation Plan

## 5.1 Phases

| Phase | Content | Risk | Gate |
|---|---|---|---|
| **S0** — occupancy + comparison audit *(land independently; zero behavior change)* | Renumber `JS_DELETED_SENTINEL_VAL` / `JS_ITER_DONE_SENTINEL` to bits-6,5-clear high bytes (§2.3) + their comparison sites. Add `static_assert(LMD_TYPE_COUNT <= 0x20)` + the bits-6,5-clear invariant comment on the TypeId enum, and compile-time asserts pinning the sentinels. Sweep all raw `>> 56` / high-byte-mask sites (≈24 sites / 11 files) and classify per §3.3. **Audit every raw-Item `MIR_EQ`/`MIR_BEQ` emission site (§3.1): float-free proof or `MIR_DEQ` reroute plan.** Grep Jube runtimes for private tag arithmetic. Confirm GC-internal header tags never materialize into Item high bytes. | none | full baselines green (`make test-lambda-baseline`, JS gtests, Radiant) |
| **S1** — runtime level, behind `LAMBDA_SELF_TAG_FLOAT` | `get_type_id` inline-double fast path (ordered first); `it2d`/`flt2it` two-arm forms; `js_make_number` + `push_d` wrappers canonicalize; packed-immediate ±0 (`ITEM_FLOAT_P0`/`_N0`) + `it2d_cold` payload-≤1 check (§2.5); **retire `LMD_TYPE_FLOAT64`** (§3.7, coordinated with number-model W-items); producer canonicalization in parsers/MarkBuilder. | medium | test262 full + `make test-lambda-baseline` + Radiant baseline, **flag on vs. off**; new property tests (§5.2) |
| **S2** — JIT lowering fast paths | Inline encode at `jm_box_float`, `jm_box_int_reg` overflow arm, Lambda-side `push_d` emission; inline decode at `jm_emit_unbox_float` / Lambda unbox sites (bitcast via scratch slot); apply the §3.1 `MIR_DEQ` reroutes decided in S0. | medium | same gates + benchmark A/B (nbody, mandelbrot, navier_stokes, matmul, splay float keys; richards/splay as the float-light branch-cost canaries), JIT **and** MIR-interp modes |
| **S3** — GC audit + hardening; default the flag on | §2.7 verification pass (`gc_fixup_embedded_pointers` skip, `item_to_ptr` DBL_MASK early-out, conservative-scan behavior); ASan run of the full benchmark suite; NaN-key hashing tests (§3.4); then flip the default. | low | clean ASan, no baseline regressions, flag-off build kept working one release as escape hatch |

## 5.2 Test & tooling additions

- **Property tests** at the encode/decode boundary: round-trip identity over the band edge (`e` = 0x1FF vs 0x200), subnormals, ±Inf, quiet/signaling NaN payloads (now bit-preserved inline), and randomized in-band doubles. The strongest property is **bit identity**: for every in-band `d`, the Item word `== bits(d)` exactly (encode stores raw) and decode is the identity bitcast; plus discriminator agreement — `(bits(d) & DBL_MASK) != 0` ⟺ `e(d) ≥ 0x200` — over exhaustive exponent sweeps. For ±0.0: encode(±0) yields exactly `ITEM_FLOAT_P0`/`_N0`, decode returns the correctly-signed zero (`1/decode = ±Inf`), and the two packed words are never misread as pointers. Table-driven, both engines.
- **Semantics pins:** `NaN === NaN` false, `0.0 === -0.0` true, `Object.is(-0, 0)` false, `Map` SameValueZero with ±0 and NaN keys, `1/-0.0 === -Infinity` — under flag on and off.
- **Lint ratchet** (precedent: `no-int-cast-radiant`): flag any new 64-bit integer constant whose high-byte bit 6 or 5 is set (i.e. lands in double space) outside the float module, and any new `>> 56` comparison against a literal not going through the TypeId enum. Run in `make lint`.
- **`get_type_id` ordering test:** a synthetic in-band double Item must return `LMD_TYPE_FLOAT` without touching memory (assert no header read — e.g. an Item whose low bits alias an unmapped address).

## 5.3 Sequencing within the tuning program

Per the tuning-proposal review: **T0/L0 re-measure → S0 (land now) → S1 → S2 + A/B → re-profile**, then re-rank the remaining memory items with floats out of the picture — T5 (still wins inside numeric kernels: raw `MIR_T_D` element loads, SIMD layout, no per-element encode), Part 2 stack boxing (int64/datetime residual only — self-tagging subsumes its float case), and the L4/T4 nursery redesign (now a small-population cleanup; the numeric-nursery leak residue is int64/datetime/decimal only). T1/T2/T3 (ICs, call path, literal shapes) are orthogonal and proceed in parallel throughout.

## 5.4 Fallback

If S2 measures worse than expected (branch cost in `get_type_id` on float-light code), the flag confines the experiment and the build reverts cleanly — and §2.9's variants B/C remain available as drop-in swaps of the encode/decode/discriminate primitives if the tag-space budget (B) or something unforeseen ever demands it. The S0 sentinel renumbering, asserts/lint invariant, `MIR_EQ` audit results, and the `LMD_TYPE_FLOAT64` retirement are all worth keeping regardless of the outcome.

---

# Part 6 — Cross-references

- `Lambda_Design_Item_Boxing.md` — **the Item-representation design record** (tagged leaves / raw containers / inline immediates, the measured direct-string-pointer counter-experiment, and the §6 runtime/GC invariants). This proposal is the "local evolution" its §7.1 anticipates; the raw-container ABI it documents is the non-negotiable constraint of Part 7 §7.2.
- `Lambda_Double_Boxing_Impl_Plan.md` — the detailed implementation plan (phases, work items, assertions, tests, gates) derived from Part 5 and the Part 7 guardrails.
- `Lambda_Tuning_Proposal.md` — Part 1 (Wall 1/Wall 4 sizing), Part 2 (stack boxing — float case subsumed by this doc), Part 3 (rooting — inline floats need none), Part 4 (the tagging-vs-NaN-boxing verdict this design implements), Part 5 (the sketch this doc supersedes).
- `Lambda_Semantics_Number_Model.md` — §2.1 (compact band = safe-integer band), §3.2 (`f64` alias rule → §3.7 here), §5.1/§5.4 (JS number ≡ float; compact-int packing removal), Part 2 W-items (migration coordination).
- `doc/Lambda_Formal_Semantics.md` §4 — numeric semantics ADR (value domain unchanged by this proposal; representation only).
- `doc/dev/js/JS_03_Value_Model.md`, `JS_04_MIR_Lowering.md` — the value-model and lowering docs to update at S2.
- `lib/gc/` — `gc_nursery.h:15` (the never-reclaimed nursery this shrinks), `gc_heap.c:770` (`item_to_ptr`), `:1202` (`gc_fixup_embedded_pointers`).

---

# Part 7 — Review of V3 and Implementation Guardrails

## 7.1 Assessment

V3 is the strongest version of this proposal so far, and it is the version I would implement experimentally.

The design now has a clean hot-path story:

- the Item stores the double's raw IEEE-754 bits;
- classification is one mask test, with no rotation, shift, bias, or decode arithmetic;
- ordinary finite values, huge values, infinities, and NaNs are inline;
- `+0.0` and `-0.0` are allocation-free packed immediates;
- Lambda's compact integers, tagged scalar pointers, and raw container pointers retain their existing physical forms.

This is a particularly good fit for Lambda because it improves the one weak storage class—boxed binary64—without replacing the rest of the Item model with NaN-boxing. It preserves the useful asymmetry documented in `Lambda_Design_Item_Boxing.md`: type-first scalar leaves remain tagged, while data-first containers remain directly dereferenceable raw pointers.

The primary risk is no longer the float encoding itself. The mask partition is simple. The risk is enforcing that partition across every Item producer, sentinel, raw comparison, GC classifier, and JIT lowering path. V3 should therefore be implemented as an invariant-hardening change first and a float optimization second.

Two descriptions in the earlier parts should be read precisely:

1. V3 does **not** leave all 256 high-byte values available to non-double tags. It reserves every high byte with bit 6 or bit 5 set, leaving 64 legal non-double high bytes: `0x00–0x1F` and `0x80–0x9F`. That is adequate today, but it is a real architectural budget.
2. V3 removes the **zero allocation fallback**, not every representational fallback. As currently specified, subnormals and nonzero magnitudes below `2^-511` still use the boxed-float path. This is practically negligible, but the implementation and documentation should not claim that all binary64 values are allocation-free.

Before treating the document as an implementation specification, rename its remaining V2 title and remove stale mechanics from earlier prose—for example, Part 4 still says `I2D + ADD + BT`, while V3 uses the mask test and stores the resulting double bits unchanged. These are editorial leftovers, not design defects, but they can misdirect implementation.

Subject to those qualifications, V3 is preferable to both the earlier XOR-band version and the bias version: it has the cheapest discriminator, the widest inline domain, exact raw-bit decoding, and the least machinery on every normal float boundary.

## 7.2 The raw-pointer invariant is non-negotiable

V3 must preserve the current container representation exactly:

```text
Item bits == uintptr_t(container)
it2p(item) == container
known container access == direct dereference of that same pointer
```

No container TypeId is ORed into the pointer. No `DBL_MASK`, high-byte mask, subtraction, or decompression operation is added before `it2arr`, `it2map`, `it2obj`, `it2elmt`, or the other container accessors dereference it.

The required pointer constraint is stronger than merely passing the double test:

```text
(pointer_bits & DBL_MASK) == 0                 // must not look like a double
(pointer_bits & 0xFF00_0000_0000_0000) == 0   // required by current raw-pointer dispatch
```

The second condition matters. A pointer with high byte `0x80–0x9F` would not collide with V3 double space, but `Item::type_id()` would interpret that byte as an Item tag rather than dereference the container header. Lambda's raw-pointer ABI therefore requires an **exactly zero high byte**, not merely bits 6 and 5 clear.

This is valid for ordinary low user-space allocator addresses on the currently supported 64-bit platforms. AArch64 TBI/MTE tags, pointer authentication bits, or any future high-address allocator mode must not silently enter Items. If such a platform configuration is supported later, pointers must be normalized once at the allocation/ABI boundary under an explicit platform design. The runtime must never make them appear valid by masking every container dereference; that would hide an unsupported address model and destroy the raw-pointer performance property.

The normalized classifier must retain this order:

```c
if ((item_bits & DBL_MASK) != 0) return LMD_TYPE_FLOAT;

uint8_t tag = (uint8_t)(item_bits >> 56);
if (tag != LMD_TYPE_RAW_POINTER) return (TypeId)tag;

if (item_bits != 0) return *(TypeId*)(uintptr_t)item_bits;
return LMD_TYPE_NULL;  // preserve the runtime's canonical zero-Item behavior
```

The double test must be first because an inline double can otherwise reach the tag/raw-pointer logic. The raw-pointer branch must use the original Item word unchanged.

## 7.3 Assertions to add before enabling V3

The assertions should live in shared representation helpers so interpreter, MIR, LambdaJS, and Jube paths enforce the same contract.

### Compile-time representation assertions

Add compile-time checks equivalent to:

```c
ITEM_DBL_MASK       == UINT64_C(0x6000000000000000)
ITEM_HIGH_BYTE_MASK == UINT64_C(0xFF00000000000000)

sizeof(Item) == sizeof(uint64_t)
sizeof(uintptr_t) == sizeof(uint64_t)
offsetof(Container, type_id) == 0

ITEM_TAG_IS_NON_DOUBLE(tag) == (((tag) & 0x60u) == 0)
```

Then assert the predicate for every materialized `TypeId` and non-float sentinel. `LMD_TYPE_COUNT <= 0x20` is a good guard while all TypeIds remain in the first legal block, but it cannot enforce the design after any future TypeId jumps to `0x80–0x9F`. At that point use an X-macro/generated tag list or explicit per-tag assertions; do not weaken the check to a maximum-value comparison.

Also pin these layout properties:

- `Container.type_id` remains byte zero;
- every raw Item family that does not inherit `Container`, notably `Function` and `Path`, also begins with its `TypeId` at byte zero;
- packed-zero Items have the `LMD_TYPE_FLOAT` high byte, payloads exactly `0` and `1`, and do not intersect `DBL_MASK`;
- all JS internal sentinels satisfy `ITEM_TAG_IS_NON_DOUBLE(high_byte)`.

### Runtime pointer assertions

Add a debug-only helper conceptually equivalent to:

```c
assert_raw_item_pointer(ptr):
    if (ptr == NULL) return;
    bits = (uintptr_t)ptr;
    assert((bits & ITEM_HIGH_BYTE_MASK) == 0);
    assert((bits & ITEM_DBL_MASK) == 0);
    assert((void*)bits == ptr);
```

Call it from both the C and C++ `p2it()` helpers. That alone is not sufficient because the codebase also constructs Items through `.container`, `.array`, `.map`, and other union fields. The same helper should therefore be used at Item-visible container allocation/construction boundaries and in debug checks around direct raw-pointer field writes that bypass `p2it()`.

After a header-bearing object has been initialized, add a separate debug assertion that:

```text
address of object == address of its TypeId header
header TypeId is a valid raw Item family
get_type_id(Item{raw pointer}) == header TypeId
it2p(Item{raw pointer}) == original pointer
```

Do not make `p2it()` itself dereference the pointer to validate a container header: it is also an ABI helper for pointer-shaped values, and some call sites may not yet have initialized the header. Address validity and header validity are separate checks.

### Classifier and GC assertions

- In `get_type_id`, assert in the raw branch that the high byte is zero before dereferencing the header.
- In `item_to_ptr`, test `DBL_MASK` first and return `NULL` for inline doubles. An unknown high tag must never fall through to the old "treat as raw pointer for safety" behavior without first excluding double space.
- In debug GC scans, assert that an Item classified as a raw container round-trips without masking and that its header TypeId is valid before tracing it.
- Keep inline doubles classified as non-pointers even when their low 56 bits happen to resemble a heap address.
- Keep semantic `MIR_T_P` information for raw GC-managed pointers. Physical storage in an `MIR_T_I64` register does not make a raw pointer an Item or a double.

### JIT and accessor assertions

- Apply `DBL_MASK` only to universal Item values. Never apply Item classification to a statically native `MIR_T_D` or `MIR_T_P` value.
- Keep known-type container unboxing as an identity conversion. Generated MIR for `it2arr(item)->length`, for example, should load from the original pointer register, not from a masked temporary.
- Assert that float boxing produces either raw in-band bits, one of the two packed-zero Items, or the canonical boxed fallback—never a raw out-of-band float bit pattern.
- Assert that float decoding accepts only those same three forms.
- Preserve one canonical encoding per double within a feature-flag configuration; mixed old/new producers are forbidden.

## 7.4 Tests that specifically protect raw containers

Before changing float behavior, add representation tests covering at least Array, ArrayNum, Map, Object, Element, Range, Function, Type, and Path:

1. construct the value through its normal allocator;
2. capture the native pointer and the resulting Item bits;
3. assert exact bit identity between them;
4. assert the Item high byte is zero and `DBL_MASK` is clear;
5. assert `get_type_id()` reads the header type;
6. assert every matching `it2*` accessor returns the original pointer;
7. read a representative field through the accessor to prove direct dereference still works.

Add a small MIR/golden inspection test for a known container load so an accidental `AND`, `XOR`, or pointer reconstruction before the field load is visible. This is a performance invariant as well as a correctness invariant.

The float discriminator ordering test should use an inline-double word whose low bits are deliberately not a valid address. `get_type_id()` must return `LMD_TYPE_FLOAT` without attempting a header read. Conversely, a real raw container pointer must reach the header branch without any pointer modification.

## 7.5 Recommended landing order and acceptance bar

Land the guardrails before the representation switch:

1. define the shared masks and tag-validity predicate;
2. renumber colliding sentinels and add compile-time tag assertions;
3. add raw-pointer address, header-layout, identity, and accessor assertions/tests;
4. harden `get_type_id` and GC ordering under the feature flag;
5. implement canonical float encode/decode in the runtime;
6. migrate interpreter and JIT producers/consumers;
7. enable V3 only after equality, hashing, GC, ASan, and release benchmarks pass.

The acceptance bar for the raw-pointer scheme is strict: all container Items remain bit-identical to their native pointers, all known-type accessors remain mask-free identity conversions, and the only new work paid by generic container type discovery is the leading inline-double discriminator. If an implementation requires tagging or masking every container pointer, it has violated the V3 design rather than implemented it.
