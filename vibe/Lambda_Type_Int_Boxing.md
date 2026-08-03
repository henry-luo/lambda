# Lambda Int Boxing — Inline Ints in the `100` Octant via Sign-Bit Rotation

- **Status:** **IMPLEMENTED 2026-08-02** — the **sign-bit rotation scheme (§2.10)** is live,
  on the **rotate-insn patch to the vendored MIR (§4)**, which landed as `MIR_ROTR`. The **β
  exponent re-bias scheme (§2.5)** remains the recorded no-patch fallback and was not needed.
  Baseline `3717/3717`. The phase plan and its landing record live in
  `Lambda_Impl_Int_Total.md`, whose §0.3 "carrier has no legal home" blocker this design
  **dissolves** (no carrier exists to home).

  Implementation notes worth keeping with the design:
  - Only **rotate-right** was added to MIR: aarch64 has no rotate-left, and `rotl(b,1)` is
    `rotr(b,63)`, so one instruction covers boxing and unboxing.
  - Sentinel payloads: `0`→0, `+1`→1, `−1`→2 — **three, not six** (revised 2026-08-03:
    `inf`/`−inf`/`nan` are the inline IEEE values, shared with `float`). The first two are
    **bit-identical to the retired compact encoding**, which is deliberate (the two hottest
    values keep their representation) and which also masked the JS emitter bug during the
    cutover: indices 0 and 1 aliased correctly while ≥2 diverged.
  - From a *native i64* the cold path is closed and call-free: drift needs 2^257 and poison
    needs a non-finite, so the only misses are `0`, `±1` and the legacy `INT64_ERROR`
    sentinel — a 3-value map, `val + 3*(val<0)`.
  - §3.7's companions both landed: `ELEM_INT` is a double lane and `TypedItem`'s int arm
    carries `double_val`.
- **Date:** 2026-08-02
- **Co-Author:** Anthropic Fable
- **Scope:** the runtime `Item` representation of flex `int` under C16 (`int` = the
  float64-representable integers, tagged, total). Native lanes, packed arrays, and map fields
  are companions (§3.7) but their carrier was already ruled: raw `double` everywhere.
- **Design authority:** `vibe/Lambda_Semantics_Formal2.md` C16;
  `doc/Lambda_Formal_Semantics.md` §4. Representation contract:
  `Lambda_Design_Item_Boxing.md` §6. Tag-space partition:
  `Lambda_Type_Double_Boxing.md` §2.3 — **amended by this doc**: the `100` octant
  (high bytes `0x80–0x9F`) stops being generic tag headroom and becomes the int inline space.
- **Convention:** `file:line` references drift; confirm against the symbol name.
- **Related:** `Lambda_Impl_Int_Total.md` (impl plan), `Lambda_Design_Scalar_GC_Invariant.md`
  (int exits the wide-scalar world entirely, §3.6), `Lambda_Impl_Double_Boxing (done).md`
  (the float precedent, including the canonical-encoding rule this doc restates).

## 0. Summary

> **RULING 2026-08-02 (user), normative and superseding: `int` has exactly ONE native
> representation — the IEEE double.**
>
> An earlier draft of this design sanctioned an `i64` native lane "when range-proven ≤ 2⁵³".
> **That sanction is withdrawn.** A range proof is no longer a licence to represent an `int` as
> an `i64`, however sound the proof. The boxed side of this document is unaffected — the
> rotation encoding below *is* the one boxed representation — and the ruling extends the same
> single-representation discipline to the register file, shaped map fields, and every other
> carrier. The migration is `Lambda_Impl_Int_Total.md` Phase G, whose G0 states the rule and its
> two non-exceptions (a C helper may *return* `int64_t`; machine quantities that are not typed
> `int` are not int lanes).


Flex `int` gets the same deal float got in double-boxing v3: **the Item is the value**. Every
int with `2 ≤ |v| < 2²⁵⁷` is stored inline in the free `100` octant as its own IEEE bits
**rotated left by one** — the sign bit moves to bit 0, and the exponent's own top three bits
(`100` for the entire covered class) double as the octant marker. Box = `rotl 1`; unbox =
`rotr 1`; no constants, no sign test, no mask. `0`, `±1`, and the three poisons are six
sentinel patterns on the (otherwise retiring) `LMD_TYPE_INT` tag byte, exactly as `±0.0` rides
the FLOAT byte. The residue `|v| ≥ 2²⁵⁷` (≈10⁷⁷) boxes as raw inline float bits — a
reflection-only drift recorded as a one-line spec footnote.

Consequences: **no int value at any magnitude ever allocates** — no number-stack cell, no
scalar home, no GC involvement, no container escape arms. The compact 56-bit form,
`LMD_TYPE_INT_BIG`, `box_int_number_stack`, and the entire §0.3 escape-arm plan are deleted or
cancelled. C16 semantics are untouched: `type(1.0)` stays `float`, poison spellings and the
`is`-lattice survive verbatim, goldens do not churn.

## 1. The geometry every scheme lives in

From `Lambda_Type_Double_Boxing.md` §2.3, the 64-bit pattern space partitions by the top three
bits (`ITEM_DBL_MASK` = bits 62–61):

| top-3 | high bytes | contents |
|---|---|---|
| `001`–`011`, `101`–`111` | — | inline floats: **raw untransformed double bits** (6 octants, 3×2⁶²) |
| `000` | `0x00–0x1F` | all TypeId tags, raw container pointers (bit-identical, immovable), sentinels |
| `100` | `0x80–0x9F` | **free** (2⁶¹ patterns). As doubles these are the negative-tiny values, which float diverts to cells/sentinels. Only squatters: two JS sentinels at `0x9E`/`0x9F` (must relocate, §3.8) |

Constraints that shape everything:

1. **C16 requires tagged-not-erased** — `type(1)` = `int` ≠ `type(1.0)` = `float` — so int
   cannot simply reuse the float encodings.
2. **An inline double is tagless**: all 64 bits are the value. There is no "same bits,
   different tag" in one word.
3. **Pigeonhole:** the integral doubles number ≈1.9×2⁶² — nearly twice the entire non-float
   quarter. No encoding of any kind can inline the full int domain.
4. **The 61-bit budget:** one octant = 2⁶¹ patterns. A double is 64 bits; every octant tenant
   sheds exactly 3 bits. Sign (1) and full mantissa (52) are non-negotiable for an integer
   type, leaving 8 effective exponent bits = **256 binades**. Every viable scheme is a choice
   of *where* the 3 bits are shed; every failed scheme below shed a bit the domain needs.
5. **The residue must stay representable, not saturate.** The int domain (all integral doubles
   + specials) is the unique integer domain *closed* under binary64 arithmetic; capping it at
   the carrier boundary would force per-op saturation checks in the native lane or observable
   interpreter/JIT divergence. So above the inline range the value drifts to the float
   encoding rather than poisoning.

## 2. Schemes considered, in discussion order

### 2.1 Two-tag: compact payload + `LMD_TYPE_INT_BIG` double cell (built, inert)

Compact 56-bit sign-extended payload under the `INT` byte for `|v| ≤ 2⁵³`; a second tag byte
(`INT_BIG`, normalized to `int` by `type_id()`) carrying a pointer to a double cell above.
The direct analog of `d2it`, one byte over.

- **Pros:** exact float-precedent structure; compact hot path untouched; already implemented
  (encoder, accessor, rehoming, JIT symbol).
- **Cons:** cell lifetime — the number-stack cell dangles on container escape, needing the
  five destination-owned escape arms (`array_set`, `owned_item_slot_store`,
  `scalar_storage_read`, `list_relocate_owned_tail`, `TypedItem`); a raw-tag/normalized-tag
  footgun at every predicate; a second int representation through every numeric path; user
  verdict: "too many runtime int types".
- **Verdict:** rejected (user, 2026-08-02). Machinery to be deleted.

### 2.2 One-tag, payload-discriminated cell

Same compact + cell duality, but the cell pointer hides *inside* the `INT` payload
(bits 55–53 discriminate compact / poison / `ptr>>3`), so no second TypeId exists.

- **Pros:** kills the extra TypeId and its switch-audit surface; hot compact path unchanged
  (the existing poison classification already screens the cold side).
- **Cons:** keeps the cell and therefore the entire escape-arm/lifetime problem of §2.1;
  keeps two int representations.
- **Verdict:** superseded — an improvement on §2.1, obsoleted by cell-free schemes.

### 2.3 Full erasure: int as a value class (JS model)

One representation (float's, raw bits); `int` survives only as a static type plus a value
class — `type()`/`is`/print classify by integrality.

- **Pros:** maximal simplification; zero encodings beyond float's; no cells; boxing is free;
  completes C16 rulings 12/14's membership direction.
- **Cons:** amends C16 semantics: `type(1.0)` → `int`, `print(1.0)` → `1`, JSON `2.0`
  round-trips as `2`, nan identities merge, golden churn everywhere. Rejects the standing
  "tagged, not erased" ruling.
- **Verdict:** rejected — the semantic price was declined; C16 stands as ruled.

### 2.4 Literal `ITEM_INT_MASK`: raw bits, two self-tagged masks

Int inline as raw untransformed double bits in a second masked space.

- **Verdict:** **impossible** — same value ⇒ same bits ⇒ indistinguishable from the float;
  and by counting (constraint 3), even a transformed full-domain space cannot exist. Two
  full-width self-tagged double spaces cannot share 64 bits.

### 2.5 β exponent re-bias — **retained alternative** (no MIR patch needed)

Additive per-sign constants relocate the 256-binade window `e ∈ [1023, 1278)` into the `100`
octant, sign moved to bit 60:

```
positive:  item = b + 0x4010000000000000     → [0x8000…, 0x8FF0…)
negative:  item = b − 0x2FF0000000000000     → [0x9000…, 0x9FF0…)
sentinels: INT_ZERO + 3 poisons in the shaved top slots (4 total)
box   = sign test + cmov constant + add   (3 ops; 1 add at sign-proven JIT sites)
unbox = screen cmp (does double duty) + sub + movq   (arithmetic core: 1 sub)
```

Coverage ±[1, 2²⁵⁵); residue drifts to raw float bits. Mantissa bytes stored unchanged (the
constants have zero low bits); only the top 12 bits are re-labeled.

- **Pros:** covers **±1 in-band** (the re-bias re-aligns exponent 1023 to slot 0 — the one
  thing no mask/class scheme achieves); only 4 sentinels; uses only ADD/SUB — **no MIR
  changes**; halves of the octant stay value-ordered.
- **Cons:** 3-op generic box (per-sign constant select is forced: source halves are
  `0x8000…` apart, destination halves only `0x1000…` apart); 2 ops worse than rotation on
  box, ties on unbox.
- **Verdict:** fallback. Selected only if the MIR rotate patch is declined.

### 2.6 Single constant across the `000`+`100` octant pair

One subtract (`b − 0x3FF0…`) maps both signs at once — the two destination octants are
`0x8000…` apart, so the sign bit rides in place. Box and unbox are each **one op**.

- **Pros:** the cheapest arithmetic possible; sign-blind.
- **Cons:** the positive half lands in the `000` octant: boxed `1` = `0x0` =
  **`ITEM_NULL` exactly**; arbitrary int payloads are indistinguishable from every
  tagged-pointer type below `0x10` and from raw container pointers (immovable). The free
  slice of `000` (~3 tag bytes) covers only 48 binades — smaller than the compact band.
- **Verdict:** dead on occupancy. (Float itself is why: it owns all three sign-symmetric
  octant pairs, which is what makes *its* boxing sign-blind; the last pair's positive member
  was spent on tags at the encoding's birth.)

### 2.7 Sign-in-place + mask field

`[s][INT MASK][payload]` with the mask OR/XOR-ed below the sign.

- **Verdict:** dead by a two-line proof: escaping float space forces bits 62–61 = `00`, and
  sign-in-place then forces the positive high byte ≤ `0x1F` — *the tag octant is by
  definition "sign 0, top exponent bits 00"*. Positives are structurally confined to
  tag/pointer bytes regardless of mask choice. Any workable scheme must **relocate the sign**.

### 2.8 Logical shift by 3

`item = (b >> 3) | 0x8000…`; unbox `item << 3`. Sign transported automatically; 2-op box,
1-op unbox, sign-blind.

- **Pros:** elegant, no constants, no test.
- **Cons:** sheds its 3 budget bits at the **bottom of the mantissa** — restored as zeros, so
  exactness requires the low 3 mantissa bits to be zero, which integrality guarantees only for
  `|v| < 2⁵⁰`. That is *below the compact band* and below live values (µs timestamps ≈
  1.7×10¹⁵, ns timestamps, snowflake IDs) — the drift would erode `type()` on real data.
- **Verdict:** dead on coverage. (Instructive: IEEE mantissas are left-aligned, so small ints
  live in the *high* mantissa bits; the low bits are the last an integer touches, at 2⁵⁰.)

### 2.9 Aligned-class XOR marker, low bits in place

Top-4 marker {`1000` positive, `1001` negative}, low 60 bits stored unchanged; unbox = XOR a
per-class constant. Reversibility requires the overwritten bits constant across the class ⇒ an
*aligned* exponent class `e ∈ [1024, 1280)`.

- **Pros:** ±[2, 2²⁵⁷) coverage; cheap XOR transforms; mantissa fully in place.
- **Cons:** exponent 1023 (`01111111111`) differs from 1024 (`10000000000`) in **every bit** —
  the IEEE bias puts 1.0 maximally misaligned against every mask boundary — so **±1 fall out
  of the class** and must be sentineled; still needs per-class constants (2 ops), so it beats
  β by little while costing ±1.
- **Verdict:** superseded by §2.10, which achieves the same class with fewer ops.

### 2.10 Sign-bit rotation — **SELECTED**

The observation: every double with `|v| ∈ [2, 2²⁵⁷)` has biased exponent `e ∈ [1024, 1280)`,
and all of 1024–1279 begin `100` in binary. **The octant marker is the exponent's own top
three bits.** Rotate the sign out of the way and the encoding is finished:

```
box:    item = rotl(b, 1)        ; [e₁₀e₉e₈=100][e₇..e₀][m₅₁..m₀][s] — in the 100 octant by construction
unbox:  b    = rotr(item, 1)     ; exact restore — no mask, no constant, nothing to clear
```

One op each way, sign-blind, zero constants. In-band guard (box side, subsumes every special
case — zero, ±1, poison, residue — in one sign-blind test):

```
(b & 0x7000000000000000) == 0x4000000000000000        ; bits 62–60 == 100
```

- **Pros:** 1-op box + 1-op unbox (the floor); widest coverage of any viable scheme
  (±[2, 2²⁵⁷) in-band, ±[0, 2²⁵⁷) exact with sentinels); no dedicated zero screen on the box
  side (the class check covers it — float pays a `== 0.0` compare every box; int does not);
  all 64 double bits preserved (the 3-bit rent is paid by *class restriction*, not by
  discarding stored bits).
- **Cons:** ±1 are sentinels (6 total vs β's 4) — mitigated: literal `0`/`±1` compile to
  sentinel constants with zero runtime cost, so only *runtime-computed* ±1 pays the sentinel
  branch, at roughly β's generic-box cost; raw-item order interleaves signs by bit 0
  (numeric compare unboxes anyway); **requires a rotate insn the vendored MIR lacks (§4)** —
  unpatched, the 3-insn fallback loses the unbox race to β and flips the verdict.
- **Verdict:** selected, conditional on the MIR patch (§4), which measured out to five
  table-row edits.

### Deciding table

| | §2.10 rotation | §2.5 β re-bias |
|---|---|---|
| box (in-band) | **`rotl` — 1 op** | test+cmov+add — 3 ops (1 at sign-proven sites) |
| unbox | **`rotr` — 1 op** | `sub` — 1 op |
| in-band range | ±[2, **2²⁵⁷**) | ±[1, 2²⁵⁵) |
| sentinels | 0, ±1, 3 poisons (6) | 0, 3 poisons (4) |
| box-side zero screen | none (class check subsumes) | none (range check subsumes) |
| MIR requirement | **rotate patch** | none |
| raw-item layout | signs interleaved (bit 0) | halves contiguous, value-ordered |

## 3. Selected scheme — full specification

### 3.1 Encoding map

| value | encoding | notes |
|---|---|---|
| `2 ≤ \|v\| < 2²⁵⁷`, integral | `rotl(bits, 1)` — in `[0x8000…, 0xA000…)` | positives even (bit 0 = 0), negatives odd |
| `0` | `ITEM_INT \| 0` | **bit-identical to today's compact 0** |
| `+1` | `ITEM_INT \| 1` | **bit-identical to today's compact +1** |
| `−1` | `ITEM_INT \| 2` | changes (compact −1 was sign-extended all-ones) |
| `inf`, `−inf`, `nan` | **raw IEEE bits — inline, exactly as `float` stores them** | **REVISED 2026-08-03**: no int-specific sentinel. See below. |
| `\|v\| ≥ 2²⁵⁷`, integral | `inf` / `−inf` by sign | **REVISED 2026-08-03**: saturates (`Lambda_Formal_Semantics.md` §4.9); the earlier β-drift to raw float bits is retired |

Worked examples: `2.0 → 0x8000000000000000`; `3.0 (0x4008…) → 0x8010…`;
`−2.0 (0xC000…) → 0x8000…0001`; `1.0 (0x3FF0…)` fails the class (`011` prefix) → sentinel.

**Canonical-encoding rule (restating the double-boxing rule):** every int value has exactly
one Item encoding — rotated in-band bits, one of the three sentinels (`0`, `±1`), or the
inline IEEE bits for `inf`/`−inf`/`nan`. Mixed producers are forbidden.

**Why `inf` and `nan` are not sentinels (2026-08-03).** `int` and `float` share one poison
representation, so the box path stores the ordinary IEEE bit patterns and everything that
already understands a double understands them: comparisons come out unordered, arithmetic
propagates, `isnan` works. The three sentinels this removes were not free — a hand-rolled nan
has to be recognized at every site that handles a number, and **45 call sites convert an int
Item to `int64_t`, each silently destroying nan-ness**. That is what made `int.nan == int.nan`
return *true* (the numeric comparison lowered its operand through one of those conversions, so
its `isnan` guard never saw it). `0` and `±1` must stay sentinels regardless: their exponents
(0 and 1023) put them in the `000`/`011` octants, so rotation cannot tag them, and storing
int 1 as raw `0x3FF0…` would make it bit-identical to float `1.0`.

`inf` and `nan` need no sentinel for the mirror-image reason: their exponent is 2047, whose
top three bits are `111`, so their raw bits already sit in the inline-float octants. They are
the one family the rotation scheme did not need to relocate — the earlier design relocated
them anyway, and that was the mistake.

**History — the retired sentinel rows.** Until 2026-08-03 the map carried three more, and
`int` had its own poison values spelled `int.inf` / `-int.inf` / `int.nan`:

| value | encoding | note |
|---|---|---|
| `int.inf` | `ITEM_INT \| 3` | replaced the earlier 2⁵⁴-based poison payloads |
| `−int.inf` | `ITEM_INT \| 4` | |
| `int.nan` | `ITEM_INT \| 5` | |
| `\|v\| ≥ 2²⁵⁷`, integral | raw double bits | **β-drift**: reflection reported `float`; carried as a spec footnote |

Six sentinels rather than three, and a fourth region whose values kept their magnitude but
lost their tag. Both are gone: the poison rows because `int` and `float` now share one
representation, and the drift row because §4.9 saturates instead
(`Lambda_Formal_Semantics.md` §4 records the semantic side of both).

#### 3.1.1 The residue never returns to `000`

Worth stating separately, because the other two out-of-class families (0/±1, poison) *do* come
back to the `000` octant as `INT`-byte sentinels and the symmetry invites the wrong guess:
**integral `|v| ≥ 2²⁵⁷` stays in the float octants.** It is not tagged, not celled, not
sentineled — the box path simply returns the value's own IEEE bits, which by construction land
in `01x`/`11x` (`e ≥ 1280` ⇒ word top-3 = `s`,`1`,`e₉`). Nothing about the word distinguishes
it from the same magnitude produced as a `float`, so `type()` reports `float`. That is the
whole of the drift; there is no fourth region and no third int representation.

The three sites that realize it (all one arm each, all pre-existing):

| site | behavior on the residue |
|---|---|
| `lambda_int_box_double` | class check misses, all six sentinel compares miss, falls through to `return bits` |
| `lambda_int_unbox_double` | the `item_bits & ITEM_DBL_MASK` arm returns the raw double |
| `Item::type_id()` | the `DBL_MASK` arm fires first ⇒ `LMD_TYPE_FLOAT` |

Consequences, in the order they matter:

- **No allocation, still exact.** The residue is the one family that would otherwise have
  needed a cell; keeping raw bits is what makes §3.6's "int exits the wide-scalar world" hold
  at *every* magnitude rather than merely below 2²⁵⁷.
- **SUPERSEDED 2026-08-03 — this drift no longer occurs.** `Lambda_Formal_Semantics.md` §4.9
  rules that an `int` result which cannot be carried as an `int` **saturates to `±int.inf`**,
  following IEEE's overflow convention. So 2²⁵⁷ is now the point where `int` arithmetic
  *closes*, not where it silently changes type, and `int` arithmetic is closed in `int` at
  every magnitude. The paragraph below is retained to record what the encoding alone would
  have done, and why saturation was worth adding on top of it.
- **Value round-trips, tag does not.** `unbox(box(v)) == v` bit-exactly, but the Item's static
  type is lost across the boundary. Any code that recovers a TypeId from a *word* (reflection,
  print, `is`, serialization) must accept `float` here; code that carries the static type
  alongside the word is unaffected.
- **GC is unaffected either way** — §3.4's early-out covers the `100` octant and the `INT`
  byte, and a float-octant word was never a pointer to begin with.
- **Reachability.** Only ~257 deliberate doublings get there; no arithmetic on realistic data
  crosses 10⁷⁷ by accident, which is why this was accepted as a footnote rather than paid for
  with a cell. **A literal can no longer reach it at all** (2026-08-03): the revised literal
  rule makes any exponent spelling a *float*, so the integer spelling is capped at the
  ingestion band ±(2⁵³−1) and `1e300` is a float literal rather than an out-of-band int. The
  earlier rule admitted `1e300` as an integer spelling, which is why this bullet used to name
  a literal as a second route into the drift region.

### 3.2 Operation sequences

```
box(v):     class check (and+cmp) → rotl 1                      ; slow path: payload-0..5 sentinel or drift
unbox(item): bit-63 test (in-band vs sentinel, given known int)
             in-band  → rotr 1 → movq                            ; 2 ops + transfer
             sentinel → payload ≤ 5 decode (one cmp + small table/cmov chain)
unbox→i64:  unbox→double + cvttsd2si                             ; only for machine quantities (offsets, counts), never to represent an `int`
```

The sentinel screen mirrors float's `double_ptr <= 1` trick (`Item::get_double`): payloads
0–5 are impossible pointers/rotations, so one unsigned compare classifies the family.

### 3.3 Dispatch

`type_id()` gains one arm: `DBL_MASK → FLOAT; (int64)item < 0 → INT; else high byte`.
The sign test is valid because bits 62–61 are known clear after the mask miss. Sentinels carry
the `INT` byte and ride the existing high-byte path — no new arm. **Prerequisite:** the JS
sentinels vacate the octant first (§3.8).

### 3.4 GC

An int Item is never a pointer at any magnitude. `item_to_ptr`/the marker return NULL for the
`100` octant and the `INT` byte unconditionally — the SG7 early-out extends to int for free.

### 3.5 Poison

The three poisons are sentinels; `int.inf` unboxes to `+∞`, so ruling 13/14 identities hold
bit-exactly (`int.inf == inf` via value comparison; nans unshared). The
`LAMBDA_INT_VALUE_IS_POISON`-family macros over 2⁵⁴-based payloads are replaced by
small-payload compares. Poison printing/parsing (spec §4.6) is unchanged.

### 3.6 What this deletes (and what it exits)

- Compact 56-bit packing (`i2it` producers), `get_int56` read sites (~90) → migrate to §3.2.
- `LMD_TYPE_INT_BIG` (tag slot, `type_id()` normalization, `get_type_name`), `int2it` cell
  encoder, `box_int_number_stack`, the cell branch of `lambda_int_item_value`.
- The `INT_BIG` arm of `lambda_item_uses_scalar_home` — **int exits the wide-scalar world**:
  no number-stack transients, no caller-donated homes, no destination-owned payload words.
  The five container escape arms planned in `Lambda_Impl_Int_Total.md` §0.3 are **cancelled**.
  Scalar homes remain the province of `int64`/`uint64`/subnormal floats, as before C16.
- The flexint dual-lane JIT emission (B1 of the impl plan) proceeds unchanged on top.

### 3.7 Storage companions (already ruled, recorded here)

Typed storage never boxes: native lanes are `MIR_T_D`; **map `int` fields store raw double
payload words**; **`ELEM_INT` is a double lane** (D1 option (b) — reads box via §3.2 and can
no longer produce `ITEM_ERROR`; no widen-on-store machinery). `TypedItem`'s int arm stores the
value in `double_val`. Parser data homes (C16 ruling 6) unchanged.

### 3.8 Prerequisites and ordering

1. Relocate `ITEM_JS_DELETED_SENTINEL` (`0x9E00DEAD00DEAD00`) and
   `ITEM_JS_ITER_DONE_SENTINEL` (`0x9F00DEAD00000000`) to free low-tag bytes (`0x1E`/`0x1F`);
   they are runtime-only constants. Without this they decode as large negative ints.
2. Replace the `LMD_TYPE_COUNT ≤ 0x20` static assert (`lambda.h`) with per-tag assertions, as
   its own comment anticipated, plus asserts that no tag byte enters `0x80–0x9F`.
3. Land the MIR rotate patch (§4); until then the emitter may ship the 3-insn fallback behind
   the same interface, at β-parity cost.
4. Then flip producers one at a time per the impl plan's audit-before-flip method.

## 4. The MIR rotate patch

The vendored MIR has **no rotate**: the insn set tops out at `LSH/RSH/URSH`
(`ref/mir/mir.h:110`); neither backend has a rotate encoding; the combiner fuses only
addresses and cmp+branch pairs (`ref/mir/mir-gen.c:46`). MIR links as a prebuilt archive
(`mac-deps/mir/libmir.a`, `build_lambda_config.json:140`) with `ref/mir` the pristine source
snapshot — this is Lambda's first local MIR patch.

Patch surface (sized against the `LSH` template; add `MIR_ROTL`/`MIR_ROTR`, 64-bit only):

| file | edit |
|---|---|
| `ref/mir/mir.h` (insn enum, near `:110`) | two enum entries |
| `ref/mir/mir.c:217` | two descriptor rows (`{MIR_ROTL, "rotl", {INT\|OUT, INT, INT}}`) |
| `ref/mir/mir-interp.c:1001/:1323` | label-table entries + `SCASE` evals (`(a<<n)\|(a>>((64-n)&63))`) |
| `ref/mir/mir-gen-x86_64.c:1745` | one `SHOP` row each — ROL/ROR share the shifts' encoding family: `D3 /0`,`C1 /0` and `D3 /1`,`C1 /1` (shifts already use `D3/C1` with `/4`,`/7`) |
| `ref/mir/mir-gen-aarch64.c:1707` | pattern rows — rotate-by-imm is `EXTR Rd,Rn,Rn,#imm` (`rotl 1` ≡ `ror 63`); `RORV` for by-reg |

Then rebuild `libmir.a` and keep `include/mir.h` in sync. Unpatched fallback:
`rotl1 = LSH+URSH+OR` — 3 uops, ~2 cycles (shifts run in parallel), which ties β's generic box
but loses unbox (3 uops vs 1) — the hotter direction; hence the patch is what makes §2.10 beat
§2.5. The interpreter's C build is indifferent (clang folds the idiom into `ROL`).

## 5. Relation to C16 and open items

**Semantics untouched by the encoding.** `type()`/`is`/print behavior, poison spellings and
the `is`-lattice are exactly C16-as-ruled; goldens do not churn. The one new observable the
encoding introduced — reflection reporting `float` for integral values `≥ 2²⁵⁷` (§3.1.1
drift) — has since been **removed by ruling**: `Lambda_Formal_Semantics.md` §4.9 saturates
such results to `±int.inf`, so an `int` cannot become a `float` by growing. The encoding's
2²⁵⁷ boundary is now where `int` overflows rather than where it changes type, which is also
why `int` saturates earlier than `float` (2²⁵⁷ vs ≈2¹⁰²⁴) — an encoding property, not a
domain one.

**Literal rules revised separately (2026-08-03), not by this encoding.** An exponent now makes
a literal a *float* — `type(1e2)` is `float`, matching C, Python, Java, Go, Rust, Swift, Ruby
and Lua, none of which admits an exponent in an integer literal. The earlier sign-split rule
(`10e1` int, `10e-1` float) made `1e16` and `1e100` fail to parse while the identical `1.0e16`
compiled. Nothing in this document depends on which spellings are `int`: the encoding acts on
*values*, and C16 makes `int` a subset of float admitted by membership, so `let n: int = 1e2`
still boxes 100 through the rotation exactly as `let n: int = 100` does. See
`Lambda_Formal_Semantics.md` §4.2 and `Lambda_Impl_Int_Total.md` C2.

Open items:

1. MIR patch approval — the fork-precedent decision (§4). Fallback: β (§2.5) with no patch.
2. Sentinel payload assignment confirmation (§3.1 proposal; note `0`/`+1` keep today's bits).
3. Measure runtime-computed ±1 boxing frequency after landing (informational; literals free).
4. `Lambda_Type_Double_Boxing.md` §2.3 and `Lambda_Design_Item_Boxing.md` taxonomy amendments
   (new storage-class row: "inline rotated int"), plus `LR_03`/`JS_03` mirrors.
5. Re-baseline MIR budgets and the Result18 typed columns only after B1 lands on top of this.
