# Lambda `int` Type — Semantics, History, and the v5 Design

- **Status:** **v5 DECIDED (2026-08-04), detailed design IN PROGRESS (§5)** — v4 (int-ft)
  remains the implemented semantics until the v5 migration lands.
- **Date:** 2026-08-04
- **Co-Author:** Anthropic Fable
- **Scope:** the single home for the `int` type — its full design history (v1–v5), the
  extracted normative record of v4, and the active v5 design. `doc/Lambda_Formal_Semantics.md`
  §4.x now links here for int-specific semantics; general numerics (promotion lattice,
  division, decimal, printing) stay in that doc.
- **Related:** `Lambda_Type_Int_Boxing.md` (v4 boxing — superseded as int design; the
  rotation encoding it specifies was int-only and retires with v4 under §5.4's packing,
  since inline floats are raw self-tagged bits, not rotated);
  `Lambda_Impl_Int_Total.md` (v4 impl plan and landing record);
  `Lambda_Semantics_Formal2.md` C16 (v4 rulings); `test/benchmark/Result_Double_vs_Int.md`
  (the i64-vs-double measurement that motivates v5).

---

## 1. The five versions of `int` — outline

`int` has evolved through five designs. Each one fixed its predecessor's sharpest flaw and
exposed a new one; v5 is the synthesis.

**v1 — `int32`.** Concept similar to a V8 small integer: a compact machine int behind a tag.
Simple, fast, and too small — data-processing workloads routinely exceed 2³¹, and overflow
wrapped.

**v2 — `int56`.** Pushed to the maximum range representable under high-byte tagging: a 56-bit
sign-extended payload in the low bits of the Item, ALU-only box/unbox (mask+tag / shift).
Flaw: 56-bit values beyond 2⁵³ **lose precision when converted to double**, so int→float was
lossy in a corner nobody guards.

**v3 — `int53`, the flex-int scheme.** The first version to design overflow seriously:
restrict to the double-exact band ±(2⁵³−1) and **promote to double on overflow**, which
sounds useful. Actual flaws: type inference becomes complicated because `int op int` can
yield `float`, so `int` arithmetic is untypable as `int`; and a truly **unboxed int is
impossible** — the unboxed value is still a union `int53 | double`, so every native op must
tag-check and branch. Never truly native; "unboxed" flex-int is still interpreted.

**v4 — int-ft, the floating-point int** (C16, implemented 2026-08-01 through 2026-08-03).
`int` = the float64-representable integers, carried **as** an IEEE double everywhere — one
native representation (`MIR_T_D`), poison merged with float's `inf`/`nan`. Pro: `int` is
**total** under common int operations, and int↔double conversion is free. Con: **the working
int type is lost** — everything is float/double at the machine level, and under MIR (and in
general) i64 is still faster than double for pure int-centric computation
(`Result_Double_vs_Int.md`: geomean 1.33×, collatz 6.99×). The double lane also forced a
standing carve-out (G0 "exception 1") for machine quantities — 25 `emit_machine_index/count`
sites, u32-exact special cases, shadow i64 loop counters — a tell that the representation
fought the type's main job.

**v5 — int53-total** (this doc, §3/§5). Hybrid of v3 and v4: **int53 range, i64 carrier,
total via IEEE-style sentinels**. The key realization: promoting to double never actually
solved overflow — beyond 2⁵³ the integers inside a double are not contiguous
(`big + 1 == big`), against all understanding of int; v4's full sparse range was illusion.
So v5 keeps v3's honest band, keeps v4's totality (`+`, `-`, `*`, `div` closed in int),
replaces overflow-promotion with **saturation to `int.inf`**, and gets the working machine
int back for array length, index, looping, char, and byte work.

---

## 2. v4 normative record — extracted from `Lambda_Formal_Semantics.md`

> **Provenance.** The five blocks below are the int-specific sections of
> `doc/Lambda_Formal_Semantics.md` (§4 preamble, §4.1, §4.2, §4.8, §4.9), extracted
> **verbatim** on 2026-08-04; the semantics doc now stubs to here. They are the normative
> spec of **v4**, which is what is implemented today, and they remain in force until the v5
> migration lands — after which they stand as the historical record. `§n` references inside
> the extracted text use the semantics doc's numbering. The v4 Item representation (sign-bit
> rotation boxing) is specified in [`Lambda_Type_Int_Boxing.md`](Lambda_Type_Int_Boxing.md).

### 2.1 The poison symmetry and the inf/nan merge (was §4 preamble)

**The poison symmetry (C16, revised 2026-08-03).** The organizing principle of
this section: **every unbounded numeric domain is closed and total with its own
poison; classification flows up, checks only guard the way down.** Concretely:
`float`, `int`, `integer`, and `decimal` each close their arithmetic with an
`inf`/`nan` pair; every nan is unequal to everything, itself included; a value —
poison included — is a member of every superdomain on the `⊑` chain, so widening
never checks, while narrowing boundaries verify domain membership (§11.4). Only
the sized machine ints stand outside the symmetry: bounded, wrapping,
poison-free.

**`int` and `float` share one poison, spelled `inf` and `nan`.** They are the
ordinary IEEE values, stored inline exactly as `float` stores them — there is no
separate `int` representation and no `int.inf`/`int.nan` syntax. `decimal`
(which also carries `integer`) keeps its own, spelled `decimal.inf` /
`decimal.nan`. So there are **two** poison identities, not four.

Consequences, all deliberate:

| | |
|---|---|
| `type(nan)`, `type(inf)` | **`int`** — a value shared across domains types as its narrowest, the same convention that makes `type(1)` be `int` |
| `nan is int` | **true** |
| `nan is float` | true — unchanged; `int ⊑ float`, so anything accepting `float` still accepts it |
| `type(1.0 + nan)` | **`int`** — a float computation's poison types as int; sound under the lattice, visible only through `type()` |
| `let x: int = <float nan>` | **admitted** |

That last row is the one thing traded. An earlier revision made poison classify
strictly upward — `int.nan is float` true but `nan is int` false — so a foreign
nan was rejected when narrowing into `int`. With one representation there is no
foreign nan to reject, and that guard is retired.

*Why the trade is worth it.* A hand-rolled `int` poison has to be recognized
everywhere a number is handled: 45 call sites convert an int Item to `int64_t`,
and **each silently destroys nan-ness** — a sentinel goes in, an ordinary
integer comes out, with nothing to mark the loss. That is not hypothetical: it
produced `int.nan == int.nan` returning *true* (the numeric comparison lowered
the operand through one of those conversions, so its `isnan` guard never saw
it), and the same root caused boxed int arithmetic to compute in `int64`. With
the IEEE values inline, comparisons come out unordered, arithmetic propagates,
and `isnan` works — from the hardware, at every one of those sites, for free.
Retiring one narrowing guard buys back an invariant that could otherwise be
dropped in 45 places.

*Where the `int` classification applies.* `type(nan)` being `int` is a claim
about the **surface** — what `type()` and `is` report to a Lambda program. It is
deliberately **not** a claim about the internal tag an implementation reads to
decide how to decode an `Item`: poison is physically a double, and the decoder
must say so, or every integer-lowering path receives a nan it was never written
to expect. Conflating the two is not a small error. LambdaJS alone has 423 sites
that test for an int tag and then lower through an int conversion — including
`js_is_symbol()`, where a nan misread as an integer compares against the symbol
base and a `NaN` becomes a `Symbol`. The seam belongs at `type()`/`is`, which
are two functions, not at the decoder, which is thousands of call sites.

*The `float` mapping for guest languages.* A guest whose numeric type is IEEE
double maps that type to Lambda **`float`**, not to `int` and not to the
abstract `number` supertype. JavaScript has exactly one numeric type, so
`typeof NaN` is `"number"` and `typeof 1` is `"number"`: the int tag a guest
uses for integral values is a representation fast path with no surface meaning.
Lambda saying `type(nan)` is `int` while JS says `"number"` is not a
contradiction — the two languages classify the same shared value under their own
type systems, which is exactly what the shared representation is for.

*History — `int.inf` / `int.nan`, C16 original through 2026-08-03.* The retired
design gave **each of four** poison-bearing domains its own pair, spelled
`int.inf` / `-int.inf` / `int.nan`, `integer.inf` / `integer.nan`,
`decimal.inf` / `decimal.nan`, alongside float's bare `inf` / `nan`. Its
organizing rules were: *same-signed infinities are one value across all four*,
but *nans are always private* — each belonging to its origin domain alone — and
*classification flows strictly upward*, so `int.nan is float` was true while
`nan is int` was false, which is what let a narrowing boundary reject a foreign
nan. Representationally `int`'s three poisons were **sentinel patterns on the
`LMD_TYPE_INT` tag byte** (payloads 3, 4, 5), because the rotation encoding maps
a double's bits into an octant that IEEE's own `inf`/`nan` fall outside of.
Retired because the sentinel had to be re-recognized at every site that touches
a number, which it was not; the `int.inf`/`int.nan` spellings no longer parse or
print.

### 2.2 The two-tier integer model (was §4.1)

- **Flex `int`** — **C16 (2026-08-01), superseding this tier's promotion arm of
  C3**: the integers exactly representable in IEEE binary64 — every integer in
  the contiguous band **±(2⁵³ − 1)** (JavaScript's safe-integer bound) plus the
  sparse representable integers beyond it — extended by the closure points
  `inf`, `-inf`, and `nan`, which `int` **shares with `float`** as the ordinary
  IEEE values (see the poison symmetry above; there is no `int.inf`/`int.nan`
  spelling). `int` is otherwise a **distinct runtime type, not a hint erased
  into float**: `type()`, printing, `is`, and the validator keep the int/float
  distinction for ordinary values, and float contact still yields float (§4.3).
  Arithmetic (`+`, `-`, `*`, `div`, `%`) is correctly-rounded binary64
  arithmetic, **closed and total over the domain**: exact within the contiguous
  band, correctly rounded above it, saturating to `±inf` where the value can no
  longer be carried as an int (§4.9). **There is no overflow promotion — int
  arithmetic never changes type.** Negation and `abs` are total (the domain is
  symmetric). Poison behaves as elsewhere: `nan is int` → true (§5.1's
  "unequal, not untypeable"), and every nan is unequal to everything including
  itself. Same-signed infinities are **one value across every poison-bearing
  domain** (`inf == decimal.inf`). Classification runs *up* the `⊑` chain —
  `nan is float` → true — and, since `int` and `float` now share one poison
  representation, `nan is int` is true as well; the narrowing rejection of a
  foreign nan retires with the distinction it guarded. The `is` lattice is
  total over full domains, poison included: **`i32 ⊑ int ⊑ integer`** (and onward to
  `decimal`), `int ⊑ float` definitionally; **`int ∥ int64`** — sparse int
  values exceed the int64 range, and odd int64 values above 2⁵³ are not int.
- **Machine ints** (`i8`…`i64`, `u8`…`u64`): Go-aligned — runtime overflow
  **wraps** (two's complement); constant/literal overflow is a **compile
  error**; division by zero yields the entered domain's poison per C14c/C16
  (`i8 div 0` → `inf`, `i64 div 0` → `integer.inf` — a deliberate
  divergence from Go's panic; number math stays in number). Ordinary machine
  arithmetic wraps; `div` and `%` leave the machine lane under §4.7 rather
  than applying a machine `MinInt div -1` rule.

*Rationale.* `int ⊂ float64` is definitional, not an embedding theorem: an int
*is* a binary64 value that happens to be integral, so the JIT holds ints in
double registers with zero observable difference (P6) and LambdaJS's numeric
model coincides with Lambda's. Totality is the point: `int ⊕ int : int` is a
*true* statement of inference rather than an approximation with a runtime
escape; an `int` annotation can never introduce a runtime failure into all-int
arithmetic (annotations stay monotone); and TE-15's containment edges retreat
to genuine casts and parses. The deliberate cost is precision, not domain:
computed results above 2⁵³ may round silently. The guideline that pays for it:
values that must be exact *and* may exceed the band belong in `i64`, `integer`,
or `decimal` — and §4.2 routes parsed data there already, so inexactness can
enter computed values only, never ingested ones. Rejected alternatives: the
historical 53-bit band with float promotion (made every declared-int boundary
a deferred runtime check and int arithmetic untypable as int); automatic
overflow into `integer` (C3's original rejection stands); erasing int into
float entirely (loses the type/format/validator distinctions data processing
needs). [C3 (historical), C16]

### 2.3 Literals are strict; data always fits (was §4.2)

- **Which unsuffixed spellings are `int` is lexical, never value-selected**
  (the same rule as the `n` suffix): a numeric literal is `int` iff it has
  **no decimal point and no exponent**. `10`, `0x1F` are `int`; `10.`, `10.0`,
  `10.e1`, `10e1` and `10e-1` are `float`. (C16, revised)
  - An **exponent makes the literal a float**, matching every language that
    distinguishes the two — C, C++, Java, C#, Python, Go, Rust, Swift, Kotlin,
    Ruby, PHP, Lua and Scheme all type `1e2` as floating-point, and none admits
    an exponent in an integer literal. An earlier revision of C16 split the
    exponent by sign so `10e1` was `int`; that made `1e16` and `1e100`
    **compile errors** while the identical `1.0e16` compiled, a distinction no
    other language draws and one that forced a `.0` on ordinary magnitudes.
  - Conceding the convention costs nothing here, which is why it is conceded:
    `int` is the float64-representable integers, a **subset** of float admitted
    by membership (§4.1), so `let n: int = 1e2` still binds 100 as an `int`,
    and `xs[1e0]` still indexes. Only `type(1e2)` changes, from `int` to
    `float` — and it now agrees with Python and Go.
- An unsuffixed `int`-form literal outside the contiguous band **±(2⁵³ − 1)**
  is a **compile error** — never a silent conversion, and **even where the
  value is sparsely representable** (`18014398509481984` = 2⁵⁴ errors although
  it is a valid `int` value): a literal of that magnitude expresses
  `i64`/`integer` intent, and admitting only the contiguous band keeps the
  rule teachable (C16). The band applies to the **integer spelling only**: a
  literal with an exponent is a float and has no band, so `1e16` and `1e100`
  are ordinary float literals. Suffixes (`i64`, `u64`, sized
  numeric suffixes, `n`, `m`, float form) express intent explicitly. The suffix alone names the type: `n` is `integer`
  always (`1n`, `1e3n`), `m` is `decimal` always (`100m`, `1.5m`, `1.5e-2m`).
  A fractional or negative-exponent spelling with `n` (`1.0n`, `1e-3n`) is a
  **compile error** naming `m` — `n` requires an integer-valued spelling.
- **Data cannot be rejected**: input parsers place integer tokens in the
  smallest exact home — `int` iff within ±(2⁵³ − 1), else `int64`, else
  `decimal` — never silently in float. The `int` home is the contiguous band,
  not the sparse representability test: ingestion stays deterministic, and
  large exact values (64-bit IDs, epoch nanoseconds) keep exact carriers.
  [C3, C13, C16]

### 2.4 `int` ranges are band-limited; use `integer` beyond (was §4.8)

An `int` range `a to b` requires its bounds to lie within **±(2⁵³ − 1)**;
outside that, it is an **out-of-range error**. A range over larger values is
written with `integer` bounds — `1n to N`.

*Rationale.* This is not a conservative approximation of what `int` can hold —
it is exactly where a range of ints is **meaningful**. A range is a sequence of
consecutive integers, so it needs a well-defined successor: `x + 1` must differ
from `x`. In binary64 that holds precisely up to 2⁵³. Above it the spacing
between representable values exceeds 1, so consecutive integers no longer all
exist, and "the range from `a` to `b`" stops denoting a sequence at all.

The failure is silent rather than loud, which is what makes the rule worth
stating. At 2⁷⁰ the spacing is 2¹⁸, so:

```
let big = 4503599627370496 * 262144   // 2^70, a perfectly valid `int`
big + 2 == big                        // true -- 2 is below the spacing
len(big to big + 2)                   // 1, not 3
```

Nothing here is a bug: every step is correct binary64 arithmetic, and `len`
correctly reports a one-element range. That is the point — an out-of-range
error at the range boundary converts an answer that is *quietly wrong for the
user's intent* into one the language refuses to guess at.

`int` keeps its full domain elsewhere; only ranges are band-limited, because
only ranges depend on successor. Where genuinely large sequences are wanted,
`integer` is exact at every magnitude and `1n to N` says so. [C16, 2026-08-03]

> **v5 note:** the "quietly wrong" example above (`big + 2 == big`) is the very
> observation that motivates v5's decision to make the band the *whole* domain,
> not just the range rule — see §3.

### 2.5 `int` overflow saturates to `±inf`, following IEEE (was §4.9)

An `int` operation whose result cannot be carried as an `int` returns
**`±inf`**, keeping the sign. `nan` is *not* used for overflow.

*Rationale — this is IEEE's own distinction, not a new rule.* Binary64 already
separates the two cases, and Lambda's float arithmetic inherits it:

| | |
|---|---|
| `1.0e308 * 10.0` | `inf` — **overflow** |
| `-1.0e308 * 10.0` | `-inf` — sign preserved |
| `0.0 * inf` | `nan` — **invalid**, no value exists |
| `inf - inf` | `nan` — invalid |

Overflow has a definite sign and direction: the answer is larger than anything
representable, which `inf` states exactly. `nan` means *no answer exists*, and
using it for overflow would throw away the sign and make a merely-too-large
value unordered with everything — when in truth it is greater than every finite
value. `int` follows the same split: saturation gives `±inf`, while the
indeterminate forms (`0 * inf`, `inf - inf`) give `nan`.

**`int` saturates earlier than `float`, and that is an encoding property, not a
domain one.** A `float` overflows only past ≈1.8 × 10³⁰⁸ (2¹⁰²⁴); an `int` Item
can carry magnitudes below **2²⁵⁷** (≈2.3 × 10⁷⁷), because that is the range the
tagging scheme reserves for the int lane (`Lambda_Type_Int_Boxing.md` §2.10). So
the same product may be a finite `float` and an `inf`. The two domains close
at different points; both close.

*Consequence: `int` arithmetic is closed in `int`.* `int * int` is an `int` at
every magnitude — a value, or `±inf`, or `nan`, never something else.
This supersedes the encoding's one accepted wart: results at or above 2²⁵⁷ used
to leave the int lane and read back as `float`, so an `int` could silently
become a `float` by growing. It now saturates instead, and the type is stable
under arithmetic. [C16, 2026-08-03]

---

## 3. The v5 design — int53-total

**The realization that unlocks v5.** Promoting int to double (v3), or *being* double (v4),
seems to solve value overflow — but it does not. Beyond 2⁵³, the integers inside a double
are no longer contiguous: `big + 1 == big`, against all understanding of what an integer
is. That is why v4 already had to band-limit ranges (§2.4) and reject sparse literals
(§2.3) — the "full" sparse range was illusion, and each of those rules was a patch over it.
So promotion to double does not truly solve overflow; it just trades a loud edge for a
quiet one, and pays for the trade with unboxing headaches (v3) or the loss of the machine
int (v4).

**The v5 rules:**

1. **`int` is the int53 band** — ±(2⁵³ − 1), the range that is truly safe, contiguous, and
   maps exactly to double. No sparse tail: v4's extended domain is dropped as misleading.
2. **`int` is total under common ops** — `+`, `-`, `*`, `div` (and `%`, negation, `abs`)
   are closed in `int`. This is v4's one great idea, kept.
3. **Overflow saturates to `int.inf`** — no promotion to double, no wraparound, no
   undefined behavior. Copying IEEE double's overflow discipline (sign-preserving `±inf`;
   `nan` reserved for indeterminate forms) is strictly better than int32/int64's wrap or
   C's UB. §2.5's overflow rationale carries over verbatim; only the saturation point
   moves (2⁵³ instead of the encoding's 2²⁵⁷).
4. **The unboxed carrier is i64: int53 values plus a few sentinels.** int32/int64 cannot
   afford sentinels — every bit pattern is a value. int53-in-i64 has ~2⁶³ spare patterns,
   so `int.inf`, `-int.inf`, `int.nan` live *in the value domain* as reserved i64 values,
   aligned with IEEE semantics. This is the structural fix for the v4-sentinel bug class:
   the old sentinels were Item-encoding patterns that `it2i`-style lowering silently
   destroyed; v5's sentinels **survive lowering because they are i64 values** — lowering
   int → i64 is the identity.
5. **The working int type returns.** Array length, index, looping, char, and byte
   operations get a real machine integer again — the G0 "exception 1" carve-out (25
   `emit_machine_index/count` sites, u32-exact special cases, shadow i64 loop counters)
   dissolves, because int *is* the machine quantity.
6. **Costs, accepted with eyes open:** (1) range limited to int53 — sufficient for common
   use; beyond it, use `integer` (or `i64`/`decimal` for exact large data, per §2.3's
   ingestion rule, which stands); (2) packing is int64-based, not as tight as V8's Smi32;
   (3) `int → float` needs a real conversion again — not free as in v4; (4) totality is
   no longer hardware-free — band checks and poison checks are explicit branches (§4, §5.3).
   All bearable; none is a perf cliff or a design pitfall.
7. **Why an explicit `int` at all, instead of JS's bare `number`?** JS's approach is
   workable for a dynamic language — and even there, every serious engine secretly
   maintains v1 (Smi) underneath and pays for it in deopts. Lambda wants full static
   typing, so a proper built-in `int` type is essential: the integer lives in the type
   system where the compiler can see it, not in a speculation layer.

**One-line essence.** v4 said: *int is a double that happens to be integral* — totality
free from silicon, machine int lost. v5 says: *int is an i64 that borrows IEEE's edge
semantics* — machine kept, totality bought with well-predicted branches. Since `int`
exists *for* the machine cases, v5 aligns the representation with the type's purpose;
v4 aligned it with the type's edge cases.

---

## 4. Version comparison — pros and cons

| | v1 int32 | v2 int56 | v3 int53 flex | v4 int-ft | v5 int53-total | (ref) V8 Smi | (ref) Python int |
|---|---|---|---|---|---|---|---|
| Domain | ±2³¹ | ±2⁵⁵ | ±(2⁵³−1) | f64-representable ints | ±(2⁵³−1) + poison | ±2³⁰ typical | unbounded ℤ |
| Total in type | no (wraps) | no (wraps) | **no** (leaves for float) | **yes** (IEEE) | **yes** (sentinels) | no (leaves for HeapNumber) | **yes** (by unboundedness; `div 0` raises) |
| Overflow behavior | wrap | wrap | promote to double | saturate ±inf @2²⁵⁷ | saturate ±int.inf @2⁵³ | box HeapNumber | cannot overflow — digit array grows |
| Contiguous (x+1 ≠ x) | yes | yes | yes | **only to 2⁵³** | yes | yes | yes, at every magnitude |
| int→double exact | yes | **no** (>2⁵³ loses) | yes | trivially | yes | yes | only to 2⁵³ (`float()` errors past 2¹⁰²⁴) |
| Unboxed native lane | i32 | i64 | **impossible** (tagged union) | double | **i64** | i31/i32 | **impossible in principle** (unbounded value can't fit a lane) |
| Machine work (index, bits, mod) | native | native | interpreted | convert-op-convert | **native** | native | object protocol — slowest column |
| int→float cost | convert | convert | convert | **free** | convert | convert | convert, lossy >2⁵³ |
| Inference of `int op int` | int | int | **int\|float** (viral) | int | **int** | (dynamic) | int — true, via unboundedness (`/` → float) |
| Per-op checks | none | none | tag branch | **none** (hardware) | band check (+poison at ingress) | **overflow check + deopt path** | digit-size dispatch + refcount ± allocation |
| Fatal flaw | too small | lossy →double | untypable, unboxable | working int lost | — (costs, no flaw known) | can't back a semantic int type | performance; no native lane possible |

**On the overflow-check cost — and yes, V8 pays it too.** v5's band check per `+`/`-`/`*`
is a real cost relative to int32/int64, which get "totality" only by wrapping — an answer
that is *wrong* rather than closed. And the check is not a novel tax: **V8 checks for Smi
overflow on every speculative small-int operation** on the hottest arithmetic path in the
world. In the baseline tiers, a Smi add is a tagged add followed by a jump-on-overflow to
a slow path; in TurboFan, `CheckedSmiTagAdd`-style ops **deoptimize** on overflow. The
difference is only the *response*: V8 escapes to a different representation (allocate a
HeapNumber, or deopt the whole function), while v5 stays in-type and returns `int.inf`.
Staying in-type is what lets `int op int : int` remain a true static statement — the
property v3 lost and V8 never had. The realistic per-op cost is one well-predicted,
never-taken-in-hot-code branch (add/sub can't overflow i64 from in-band operands, so the
band test alone suffices; `*` needs a two-tier check, §5.3), and the compiler's
finiteness dataflow elides operand poison checks inside proven regions.

**Python int — the opposite pole, and an instructive prior case.** Python 3's `int` is the
other honest answer to overflow: **totality by unboundedness** rather than by poison — a
variable-length digit array that simply grows, so overflow does not exist, contiguity holds
at every magnitude, and `int op int : int` is a *true* statement, the same property v5 buys
with sentinels. Three things make it instructive here:

1. **Its history recapitulates v3.** Python 2 had machine `int` plus bignum `long` with
   **auto-promotion on overflow** — exactly v3's promote-on-overflow pattern, with the same
   disease: the result type changes under arithmetic. Python 3 cured it by unifying
   *upward* (everything becomes the bignum); v5 cures it by sealing the band and closing
   *in-type*. Both eliminate promotion; they walk away from it in opposite directions.
2. **Even the all-exact design concedes `/`.** Python 3's true division is `int / int →
   float` — the unbounded integers still leave their domain for quotients. Lambda draws
   the identical line (`/` → float, `div` stays int), which is reassuring: the split is
   not an artifact of v5's bounded band, it is where the integer/real seam genuinely lies.
3. **The price is structural, not incidental.** CPython has **no unboxed int at all** —
   every int is a heap object behind a pointer (softened by the −5…256 small-int cache
   and 3.12's compact-int fast path; PyPy tags small ints, proving the mitigation is an
   engine heroic, not the design). This is not an implementation shortfall to be tuned
   away: an unbounded value *cannot* have a fixed-width native lane, so "machine work
   through the object protocol" is the permanent cost of totality-by-unboundedness — the
   "bigint/integer: performance issue" flaw class in its purest form.

**Lambda already contains Python's design point — as `integer`, not `int`.** The tower
`i32 ⊑ int ⊑ integer` splits Python's one type into a fast, bounded, poison-closed core
(v5 `int`) and an exact, unbounded escape (`integer`, mpdec-backed, with its own
`integer.inf`/`integer.nan`). Where Python makes every integer pay the bignum price so
that no integer ever overflows, Lambda lets the common case run in a machine register and
reserves the digit array for values that ask for it (`1n`, ingestion of >2⁵³ data, §2.3).
v5 does not need to *be* Python because the tower already has Python in it.

**What each transition bought:** v1→v2 range; v2→v3 double-exactness; v3→v4 totality and
typability; v4→v5 the machine int back, without giving totality up. v5 is the first
version with no known *structural* flaw — its costs are enumerable and bounded, which is
what "most well-rounded" means here.

### 4.1 Further prior art — the rest of the design space

**Lua 5.3 — the v4→v5 transition, already shipped by someone else.** Lua spent two
decades as v4 (every number a double, through 5.2) and added a machine-integer subtype in
5.3 for exactly §1's reasons: 64-bit identifiers, bitwise work, and platforms where the
double detour hurts. Its residual rules map almost 1:1 onto v5's checklist: `int op int →
int` (but **wrapping**, where v5 chooses poison); `/` always float while `//` stays int —
the same seam as Lambda's `/` vs `div`; `1 == 1.0` true with float table keys normalized
to int — v5's §5.5 hashing question, answered the same way; and float→int conversion
errors unless the value is integral ("number has no integer representation") — admission
by membership. Lua is v5's nearest cousin: identical split, wrap in the one cell where v5
does something better.

**Swift — proof that per-op overflow checks ship.** Swift **traps** on every default
integer overflow (`&+` opts into wrap) in an AOT language with no speculative JIT, and is
considered fast — the strongest single citation that v5's band-check-per-op is affordable:
the check compiles to the same add-plus-branch v5 needs, and Swift pays it on every
arithmetic op in all production code. Semantically it is the "loud partial" alternative:
where v5 stays total by returning `int.inf`, Swift halts the program. Lambda's TE-15
soft-skip world has no place for a trap, so poison keeps totality — but Swift settles the
cost question on its own.

**Julia — v3's disease, named precisely.** Julia wraps `Int64` and its documentation
defends the choice on **type stability**: promotion-on-overflow would make `Int + Int`
return `Union{Int, BigInt}` and destroy inference — the exact argument that killed v3
here, made independently. Julia accepted wrap to keep stability; v5 keeps stability and
refuses wrap. Same diagnosis, stronger cure.

**Rust and Zig — saturation exists, but clamps to the wrong value.** Rust ships a
per-callsite menu (`checked_`, `wrapping_`, `saturating_`, `overflowing_` families); Zig
builds `+%` (wrap) and `+|` (saturate) into the syntax, with plain `+` overflow a
safety-checked error. Their saturating forms clamp to `MAX`/`MIN` — a **legal value**, so
an overflowed result impersonates a real one and is undetectable after the fact. v5's
clamp target is a **sentinel**: detectable, propagating, ordered like IEEE inf. Same
mechanism, opposite observability — the one-row contrast that justifies poison over plain
saturation. (Rust's debug-checked/release-wrap split is also a cost datapoint: the checks
were judged affordable for every debug build ever compiled.)

**OCaml — tagging made free by donating one bit.** OCaml's native int is 63-bit with the
low bit as tag, and arithmetic runs **on the tagged form** through algebraic identities
(`a + b - 1` for add, and kin) — the compute-on-boxed trick worth remembering when §5.4
weighs encodings. The price is silent wrap at a nonstandard width, a documented footgun.
OCaml stakes out the far end of v5's axis: zero per-op cost, zero overflow safety.

**Dart — both of v5's neighbors, empirically rejected by one language.** Dart 1 had
Python's answer (arbitrary-precision int); Dart 2 retreated to fixed int64 wrap for
performance — totality-by-unboundedness withdrawn from a production typed language. Its
web backend meanwhile still compiles `int` to JS doubles: v4 semantics, 2⁵³ caveats in
the manual, and a standing native/web behavioral divergence. One language, evidence
against both adjacent design points.

**JavaScriptCore / SpiderMonkey — v3 industrialized.** Both NaN-box a dual int32/double
lane and branch per op on "are both still int32?", overflowing into the double lane. It
works — at the cost of the speculative-JIT superstructure (type feedback, deoptimization)
that exists in large part to hoist exactly those branches. This confirms v3's rejection
from the industrial side: the dual-lane union is viable only if you build a V8 around it.

**Haskell, Ruby, Smalltalk — the tower and the lineage.** Haskell's `Int`/`Integer` pair
is the direct precedent for Lambda's `int`/`integer` split — bounded-fast beside
exact-unbounded, both first-class. Ruby (a Lambda guest) unified `Fixnum`/`Bignum` under
the one name `Integer` in 2.4 — tier-hiding, the same move as Lambda decimal's N7 — with
tagged fixnums promoting underneath, the Smalltalk-descended pattern V8's Smi also comes
from.

**The unoccupied cell.** Sorting the whole list by overflow response: **wrap** (C†, Go,
Julia, OCaml, Lua 5.3), **trap** (Swift, Zig safe modes), **saturate-to-legal** (Rust/Zig
operators, DSP arithmetic), **escape the type** (Smalltalk lineage, Python 2, V8 Smi, JS
NaN-boxers), **unbounded** (Python 3, Haskell `Integer`), **be a double** (v4, Dart web).
**Saturate-to-sentinel — bounded, total, in-type, detectable — is the cell no language
occupies.** Its precedent is not a language at all: IEEE 754 itself closes a bounded
domain with `inf`/`nan` twice, in binary and in decimal floating point, and Lambda's
`decimal` already follows the latter. v5 imports that closure discipline into the integer
domain — §2.5's argument carried to its conclusion. († C is the worst cell: signed
overflow is undefined behavior — not even wrap is promised.)

---

## 5. v5 detailed design — IN PROGRESS

> Sections marked **PROPOSED** carry a concrete default awaiting ruling; **OPEN** items
> need a decision before implementation. This section is being filled in together.

### 5.1 Value domain, poison identity, and lane sentinels — RULED 2026-08-04

Domain: `{ n ∈ ℤ : |n| ≤ 2⁵³ − 1 }` plus the closure points, carried natively in i64.

**Why int53, not int56 — RULED 2026-08-04.** The 56-bit packed encoding (§5.4) makes an
int56 sub-variant *available* — bigger range at zero representation cost — and it is
rejected. int56 does not buy much: int53 already covers every working-int range (§5.3's
discharge argument needs nothing above 2⁵³). What int56 would *cost* is the clean seam
with float: every int53 value converts to double **exactly**, so mixed int/float
arithmetic promotes without loss — a genuinely clean promotion — and `int ⊑ float` holds
under the exact-embedding lattice rule, so inference widens int into float contexts with
no check and no caveat. int56 values in (2⁵³, 2⁵⁵) round on promotion, which both
reintroduces v2's silent-loss edge and breaks the embedding — `int ⋢ float` — poisoning
the inference lattice to gain range nobody needs. The band IS the subtyping edge.

**Poison identity: v5 keeps v4's merge (user ruling).** `int`'s `inf`/`nan` ARE `float`'s
— one identity, boxed as the inline IEEE bits exactly as today. Everything §2.1 decided
survives v5 **untouched at the surface**: `type(nan)` is `int` via the `fn_type`/`is`
seam, `nan is int` true, the spellings stay `inf`/`nan` (no `int.inf` revival in parser
or printer), `inf == decimal.inf`, hashing normalization, the goldens. The first draft of
this section proposed distinct value-domain sentinels and a revert of the merge; that is
withdrawn — it would have re-litigated settled semantics for no benefit.

**Lane sentinels are PRIVATE to the i64 lane.** IEEE bits cannot live in an i64, so
*within the native lane only*, poison is represented by reserved i64 values, converted at
box/unbox and never escaping:

| lane value | means | boxes to |
|---|---|---|
| finite n, \|n\| ≤ 2⁵³−1 | itself | 56-bit packed int Item (§5.4) |
| `INT64_MAX` | `+inf` | the inline IEEE `+inf` (float-shared) |
| `INT64_MIN + 1` | `-inf` | inline IEEE `-inf` |
| `INT64_MIN` | `nan` | inline IEEE `nan` |

Unboxing inverts the map (the IEEE-special test already exists as `DBL_MASK` + exponent).
Because boxed poison is an inline-float Item, `get_type_id()` keeps reporting FLOAT for
it and the surface seam keeps answering `int` — the v4 architecture, unchanged. Note the
hardware nearly agrees with this mapping: aarch64 `fcvtzs` already saturates ±inf to
`INT64_MAX`/`INT64_MIN` — only nan needs an explicit branch (it converts to 0 on ARM, to
`INT64_MIN` on x86), and that branch is the ingress check §5.3 requires anyway.

Degradation property: the sentinels sit at the i64 extremes, ≥ 2⁶³ − 2⁵⁴ from the band,
so raw arithmetic on a sentinel that leaked past a forgotten check lands far out of band
and re-poisons at the next band check instead of laundering into a finite value (sign may
be lost; poison-ness is not). Laundering needs two poisons meeting in unchecked
arithmetic — §5.2 requires operand checks at exactly those sites. A bonus that falls out of
placing the sentinels at the two's-complement extremes: **negation and `abs` are
branch-free total.** `-(INT64_MIN)` wraps to `INT64_MIN` — nan stays nan;
`-(INT64_MAX)` = `INT64_MIN + 1` — `+inf` becomes `-inf`; `-(INT64_MIN+1)` =
`INT64_MAX` — `-inf` becomes `+inf`; and the symmetric band closes every finite case.
Plain machine `neg`/`abs`, no sentinel branch, no `INT64_MIN` anomaly — the classic
two's-complement trap becomes the mechanism that propagates poison correctly.

### 5.2 Poison algebra — the lane must branch what IEEE gave free

With the poison *identity* shared (§5.1), every equality, ordering, `type()`, printing,
and cross-domain question is already answered by float's spec — nothing to re-decide.
What remains is **operational**: the i64 lane must *produce* IEEE-aligned results by
explicit branching, because integer silicon has no propagation. The table to ratify
(operand classes finite / ±inf / nan; ops `+ - * div % neg abs == <`):

- `inf + finite = inf`, `inf + inf = inf`, `inf - inf = nan`, `inf * 0 = nan`,
  `inf * x≠0 = ±inf` by sign; `x div 0` → `±inf` for x≠0, `0 div 0` → `nan`,
  `x % 0` → `nan`, `inf div inf = nan`; nan in → nan out, everywhere.
- Comparisons: i64 compares are total in hardware, so nan-unorderedness (including
  `nan == nan` → false) is an explicit poison pre-check where v4's DCMP gave it free.
  Total order for sort: nan last, as already ruled.
- `INT53_MAX + 1` → `inf` — the loud edge at the honest boundary. (v4 gave exact 2⁵³
  here and stayed quietly non-contiguous above; this is the one behavioral delta class.)
- Missing IEEE rows to include when ratifying: `x % ±inf = x` (fmod convention),
  `±inf % x = nan`, `0 ** 0 = 1` (consensus), `inf ** 0 = 1`.
- **RULED 2026-08-04 — bitwise and shifts (`& | ^ << >>`)**: native i64 (the collatz
  class), same band rule as `*` — a result out of band saturates to `int.inf` by sign;
  poison operands checked at ingress. Code that *wants* wrapping bit-mixing belongs in
  `i32`/`i64`, which wrap by design.
- **RULED 2026-08-04 — `**`/pow**: compute, then band-saturate like `*`.
- **RULED 2026-08-04 — arithmetic sysfuncs returning int stay total.** `sum()` and kin
  return `int.inf` on overflow, never an error — interior number math stays in number
  (C17). This deliberately does **not** disturb the standing 2026-08-02 ruling that an
  out-of-range `len()` is a **soft error**: `len` is a *measurement* whose failure is a
  count refusing its result type — an admission failure at the ingress side of C17's
  line — while `sum` is *arithmetic*, interior by definition. Same line, two sides.
- **RULED 2026-08-04 — aggregate overflow follows mathematical-value semantics.** The
  result of `sum()` (and kin) is the **true mathematical sum** if in band, `±inf` by the
  true sum's sign otherwise, `nan` if any element is nan. Intermediate excursions past
  the band do *not* saturate — the alternative (left-fold semantics) would make `inf`
  depend on element order and let SIMD/pairwise reassociation legally diverge from the
  scalar fold. Mathematical-value is reassociation-stable and SIMD-safe, which unblocks
  the ELEM_INT kernel rebuild. Implementation: i64 accumulator with checked adds;
  overflow falls to a wider/cold path; one final band admission.

### 5.3 Native lane and check placement — PROPOSED

Native lane: `MIR_T_I64`. Check architecture mirrors the existing boundary discipline —
**check at ingress, prove inside**:

| Op | Checks | Note |
|---|---|---|
| `+`, `-` | 1 band check on result | in-band operands can't overflow i64 (max 2⁵⁴); out-of-band result → saturate by sign |
| `*` | `__builtin_mul_overflow` + band check | in-band products reach ~2¹⁰⁶; i64-overflow ⇒ saturate by sign(a)⊕sign(b) |
| `div`, `%` | divisor-zero check | mandatory regardless — i64 division by zero traps in hardware (v4's fdiv dodged this) |
| `==`, `<` … | poison pre-check | i64 compares are total; nan-unordered must branch |
| ingress (params, unboxed Items, returns) | operand poison check | required for `inf−inf=nan`, `inf×0=nan` — result checks alone cannot synthesize these |
| proven-finite regions | **none beyond the result band check** | each op's band check proves its result finite-or-poison for the next op |

**The working-int checks discharge statically — v5's structural edge over Smi (user
point, confirmed).** For the roles `int` exists for, the band check is not merely cheap,
it is *provably absent*:

- **Indexing**: any in-bounds index is in-band by construction — no addressable object
  reaches 2⁵³ elements, so `len < 2⁵³` always, and the bounds check subsumes the band
  check. Zero added cost over v1-style machine indexing.
- **Loop counters**: `for i` bounded by an in-band `n` proves `i + 1 ≤ n` by induction —
  zero checks in the loop body. 2⁵³ increments at 1ns each is ~104 days; unreachable.
- **Length arithmetic**: sums and products of lengths of live objects stay far in band
  and are provable from the operands' own bounds.

V8 cannot do this: its 2³⁰ Smi ceiling *is* reachable by real programs (typed arrays
index to 2³²−1, counters and ID spaces pass 10⁹), so every Smi op keeps its dynamic
overflow check and deopt path forever. int53's ceiling is unreachable by any memory
object, so the working-int paths are check-free **by proof, not by speculation**. The
residual checks live only on unproven general arithmetic — exactly where they belong.
One caveat the tower already handles: epoch *nanoseconds* (~2⁶⁰) exceed the band and are
routed to `int64` at ingestion (§2.3); milliseconds fit (2⁵³ ms ≈ 285k years).

**RULED 2026-08-04 — int·int `cmp`, `+`, `-`, `*`, `div` are emitted INLINE in MIR.**
No runtime calls on the int arithmetic path: the emission is [sentinel branch(es) where
unproven] + the plain machine op + [band branch], with the cold arms materializing the
sentinel constants inline (or in one shared per-function cold block). This is v5's
answer to v4's single-DADD emission: the hot path is one integer instruction plus
never-taken branches, and `neg`/`abs` need no branches at all (§5.1). Expect the MIR
ratchet budgets to churn in both directions — unproven arithmetic gains branch insns,
boxing/unboxing loses the rotation and all FP traffic — so re-baselining is a migration
step, not a regression signal.

- OPEN: where the finiteness-proof dataflow lives (AST type facts vs emitter local
  state), and which existing machinery (`mir_is_native_int_arith`, the boundary-proof
  path) it extends.
- **RULED 2026-08-04 — machine quantities stay plain `int64_t`/`uint64_t`; only the
  LANE gets a distinct spelling.** (User: "for machine quantity, can you just use int64
  or uint64?" — yes, and it is the right polarity.) The special name marks the special
  contract: a **lane value** may hold the three sentinels, so it alone is spelled
  distinctly (`IntLane`); indexes, lengths, offsets, capacities and all internal
  C/C++ keep `int64_t`/`uint64_t`/`size_t` exactly as today — zero churn, and consistent
  with the standing G0 scope rule that internal machine quantities were never int lanes.
  Rationale for the polarity: the machine spelling is everywhere (thousands of sites)
  and means what every C programmer expects; the lane is rare (sysfunc boundaries,
  box/unbox, arithmetic emission helpers) and dangerous — misreading one is the `it2i`
  bug class, this time `+inf` read as `INT64_MAX` elements. Two implementation notes:
  (i) prefer the **struct-wrapped** form `typedef struct { int64_t v; } IntLane;`
  over a bare typedef — a bare typedef is an alias the compiler will not police, while
  the single-member struct makes lane↔machine mixing a *compile error* at hand-written
  C sites (where the historical bugs lived) and still passes in one register under both
  SysV and AAPCS64; (ii) the registry's `FPTR` casts and JIT-emitted calls erase C types
  entirely, so for those boundaries the fence remains the conversion funnel pair
  (lane→machine = ingress check; machine→lane = identity) plus a grep gate over sysfunc
  signatures — the struct buys enforcement precisely where casts don't erase it.

### 5.4 Boxed encoding — 56-bit ALU packing, fully specified (RECOMMENDED)

The boxed Item form: **high byte = `LMD_TYPE_INT` tag, low 56 bits = the value in
two's-complement, sign-extended on read** — v2's encoding, made safe by v5's band.

```
box(n)   = (LMD_TYPE_INT << 56) | (n & MASK56)      // AND + OR; aarch64: bfi ≈ 1-2 insns
unbox(i) = ((int64_t)(i << 8)) >> 8                 // sign-extend low 56
                                                    // aarch64: sbfx  — ONE instruction
                                                    // x86-64:  shl 8; sar 8
```

Why this is the right shape for v5:

- **No FP unit anywhere on the int path.** Box and unbox are 1–3 integer ALU ops staying
  in integer registers. The alternative (keep v4's rotation form) makes every box/unbox
  an FP round-trip — `scvtf`+rotate / rotate+`fcvtzs`, crossing register files — and
  leaves the boxed and native representations disagreeing, v3's dual-representation
  headache in new clothes. For a *working* int (indices boxed into containers, map
  fields, protocol values) the ALU form wins outright.
- **Capacity**: int53 needs 54 bits (sign + magnitude); the payload has 56 — two spare.
  **v2's fatal flaw cannot recur**: the >2⁵³ values whose double-conversion lost
  precision are simply not in the domain anymore. v5 = v2's encoding + v3's band + v4's
  totality, each version contributing the part it got right.
- **Payload invariant: only finite band values ever occupy the 56-bit form.** Poison
  boxes as the shared inline IEEE bits (§5.1), never as a packed payload — so a packed
  int Item always decodes to a valid finite int53, and the lane sentinels are handled
  entirely by the box/unbox conversion, cold branch.
- **Canonical**: one representation per value; all int53 values round-trip exactly.
- **Tag space**: the `100` octant (high bytes 0x80–0x9F) that rotation occupied returns
  to reserved headroom. **Correction to an earlier claim in this doc and the boxing
  doc's banner**: inline floats were never rotated — they are raw self-tagged bits
  (`lambda_float_ptr_to_item`: `if (bits & ITEM_DBL_MASK) return bits`). Rotation was
  int-only, so under this option it retires *entirely*; the vendored `MIR_ROTR` patch
  simply becomes unused (kept — un-patching vendor state churns for nothing, and the
  instruction is generally useful).
- Compute-on-boxed (OCaml-style tagged arithmetic) is *not* a goal: with a high-byte tag,
  carries from negative payloads cross into the tag byte, so ops would need masking
  anyway — and a 1-insn unbox makes it moot.

OPEN: formal ruling (a) vs (b); on (a), whether the freed octant is re-reserved or left
as assert-guarded headroom.

### 5.5 Surface-semantics deltas vs v4 — nearly empty (revised after §5.1's ruling)

- **Poison surface: NO deltas.** `type()`, `is`, spellings, printing, equality, order,
  hashing — all inherited from the kept merge. No golden churn from poison.
- **The sparse tail is dropped** — the one real behavioral delta class. Computed results
  with |v| > 2⁵³−1 saturate to `inf` where v4 produced exact-or-rounded sparse values:
  `INT53_MAX + 1` → `inf` (v4: 2⁵³); `2^30 * 2^30` → `inf` (v4: exact 2⁶⁰). Loud where
  v4 was quietly non-contiguous. Goldens touching sparse values (`int_total_c16.ls`'s
  2⁵⁴/2⁶² rows, the repr gtests' sparse cases) change; nothing else does.
- §2.4's range band and §2.3's sparse-literal rejection become vacuous in the good way —
  the whole domain is the band.
- **RULED 2026-08-04 — `int ⊑ int64` does not hold, due to the sentinels.** `int`'s
  domain includes `inf`/`-inf`/`nan`, which have no `int64` home (int64 is poison-free,
  every bit pattern a finite value), so the embedding fails and `int ∥ int64` stands.
  Every *finite* int is an int64 value; the types stay parallel.
- **Narrowing into `int` tightens (accepted 2026-08-04)** — a delta for the golden
  sweep: under v4,
  `int64`/`integer`/`float` values that were *sparse representables* (2⁵⁴, 2⁶⁰, …)
  admitted into `int` by membership; under v5 the membership test is the band, so they
  reject as admission errors. Parsed data is unaffected (§2.3 already routes >2⁵³ to
  `int64`); only explicit narrowing of large values changes.
- Literal band, lexical int-spelling, `1e2`-is-float, ingestion homes: unchanged.

### 5.6 Guest boundary and the LJS number policy — RULED (direction)

**Cross-language mapping (user ruling, restating N1/N3 — unchanged by v5):** `int` is a
Lambda-only concept. Lambda `int` entering JS becomes a JS number (double) — exact
always, since int53 ⊂ exactly-representable doubles; the egress conversion is one
`scvtf`. JS numbers entering Lambda are always `float`. Poison crosses as itself: a
boxed poison *is* an inline-float Item (§5.1), so `js_is_symbol()` never sees an
INT-tagged special — the first draft's sentinel-leak concern is dissolved **by
construction**, because lane sentinels cannot exist outside the lane.

**LJS internal policy (user ruling): JS number = double, full stop; no small-int
representation.** `int` (or bare machine i64) appears inside LJS only as *working*
values — loop counters, index registers, lengths in flight — that are never boxed into
JS-visible Items. Why this costs almost nothing here when it would be prohibitive in V8:
**Lambda's doubles box inline** (self-tagged raw bits, no allocation), so the
heap-allocation pressure that forces Smi on V8 does not exist. The JS engine's reason
for a small-int type is absent; only its complexity would remain.

LJS is already ~90% of the way there (N1: `js_make_number` returns `flt2it`
unconditionally; every arithmetic operator infers FLOAT, including int−int). The
remaining cleanup is enumerable, from the 2026-08-04 census: the ~10 JIT lowering sites
that box lengths/indices as INT Items (`jm_box_int_reg` callers), module int consts
(`MCONST_INT`), and protocol returns like `Buffer.compare`'s `i2it(-1)` — sweep them to
float boxing. End state: **an INT-tagged Item inside LJS means Symbol (≤ −2⁴⁰) or
internal protocol, never a JS-visible number** — which makes `js_is_symbol` sound by
construction and shrinks the 577-site `it2i` audit to the protocol subset. The
bool-literal-typed-as-INT bug (found and fixed 2026-08-04: `let flag = false` routed
into P6's native numeric lane, `typeof` said "number") is a live specimen of exactly the
misalignment class this policy eliminates.

### 5.7 What carries over from v4 unchanged

The v4 semantic work that is representation-independent stands: the literal band and
lexical `int`-spelling rule (§2.3), ingestion homes, saturation-not-promotion and the
inf-vs-nan overflow split (§2.5's rationale), admission by membership at narrowing
boundaries, `len`/error-family/TE-15/16 semantics, `i2it`'s band asserts, and the
`Result_Double_vs_Int.md` harness. The frozen C2MIR path is untouched (rule 14) — its
emitted C calls `it2i`/`i2it`, whose signatures survive both §5.4 options.

### 5.8 Migration order and gates — OPEN (sketch)

1. Ratify §5.1 encodings + §5.2 algebra table + §5.4 boxing choice (design-complete gate).
2. **Price the checks before building**: port band+poison checks into 2–3 C2MIR int
   benchmarks (`sum` = tight accumulate, `collatz` = modulo class, `bounce` = mixed
   int-index/float-payload) — converts `Result_Double_vs_Int.md`'s 1.33× ceiling into a
   realistic v5 number, including the mixed int→float feed cost that table does not
   measure. Interleave binaries when measuring (sequential A/B on this tree has shown
   2× phantom drift).
3. Boxed encoding + `it2i`/`i2it`/sentinels (§5.4) with the item-repr gtests extended to
   sentinel round-trips.
4. Native lane flip `MIR_T_D` → `MIR_T_I64` with §5.3 checks; G7's elision retires;
   `emit_machine_index` collapses to a band check (no D2I); the 25-site G0-exception
   surface and shadow loop counters dissolve.
5. Surface deltas (§5.5): parser/printer, `type()`/`is`, goldens, semantics-doc stubs
   updated to point at the v5 sections as normative.
6. Gates: both baselines green; JS suite green (no INT-tagged poison can reach JS by
   §5.6); AWFY correctness sweep; the priced benchmarks re-run against the real build.
   **Plus one subtle pre-cutover gate: grep-verify zero surviving `INT64_ERROR`
   compares.** The retired sentinel *was* `INT64_MAX` — which is now the `+inf` lane
   value, so any legacy compare that survived the A5/G8 retirement would silently treat
   `+inf` as an error. The retirement is believed complete; the gate makes it proven.

Companion representation decisions carried by the lane flip (mechanical, recorded here):
- **Shaped map/element int fields and `TypedItem`'s int arm** store the lane i64
  (sentinels included); field reads box through the §5.1 conversion. This reverts D3's
  double words alongside G1's lane.
- **`int[]` (ELEM_INT) stores lane i64 — RULED 2026-08-04.** Store semantics keep
  today's shape: fractional doubles `llround` on store (as now), out-of-band values
  store the `±inf` sentinel, poison passes through; reads box via §5.1.

Second-order consequences that come out in the wash (no separate workstreams):
- **G3 dissolves** — `Range{int64_t start,end,length}` is *naturally correct* under v5;
  the double-length dilemma existed only because v4's int reached 2²⁵⁷.
- **G4 dissolves** — const-pool int literals as `int64_t` are the right storage again.
- **G8 reverts cleanly** — the "C return mirrors Lambda type" rule now maps `int` →
  `int64_t`, so the four converted sysfuncs (`fn_len`, `fn_index_of`,
  `fn_last_index_of`, `fn_ord`) go back to `C_RET_INT64`; the INT64_ERROR sentinel stays
  retired (failures ride boxed Items, as landed).
- **ELEM_INT returns to i64 storage**, re-enabling the SIMD kernels D1 had to gate to
  ELEM_INT64 — a recorded Phase F regression, repaired by the lane change itself. Poison
  in a packed `int[]` is stored as lane sentinels and boxes to IEEE on read.
- **G7's elision retires** with the shared lane it was predicated on; `emit_machine_index`
  collapses to a bare band check (no D2I); the shadow-i64 loop counters unify with their
  bindings.
- `i2it(int64)` stays total: out-of-band input saturates per §2.5 (never an error Item) —
  preserving the O1-class safety property that closed this file's history.

Expected surface: the v4 migration (G1 + merge endgame) touched ~28 files and surfaced
~a dozen latent bugs; budget the same order of magnitude here.
