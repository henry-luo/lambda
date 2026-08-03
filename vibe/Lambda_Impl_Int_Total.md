# Lambda — Total `int` Implementation Plan (C16 × Rotation Boxing)

**Status:** IN PROGRESS — rewritten 2026-08-02 around the selected representation:
**sign-bit-rotation inline ints** (`Lambda_Type_Int_Boxing.md` §2.10, normative for the
encoding; its §4 MIR rotate patch is prerequisite P1; β re-bias is the recorded no-patch
fallback). The earlier plan's §0.3 carrier blocker is dissolved — no cell exists to home — and
its A1/A2 items are superseded by Phases P/R0/R1 below. Landed work (§0.1) is green and kept.

**Progress 2026-08-02: Phases P and R0 are DONE and green.** Baseline 3710/3716 with the same
6 pre-existing `js_*` ratchet failures and no new ones; the suite grew by the 6 new int
representation tests. Landed:

| Item | What landed |
|---|---|
| **P1 MIR rotate** | `MIR_ROTR` (64-bit) added to the vendored MIR — enum (`mir.h`), descriptor row (`mir.c`), interpreter label + `ROTR64()` `SCASE` (`mir-interp.c`), x86-64 `SHOP (MIR_ROTR, "D3 /1", "C1 /1")` **plus an immediate bypass in machinize** so a constant rotate keeps its imm8 form instead of being forced through `cl`, and aarch64 `RORV` + a new `SO` pattern letter driving `EXTR Rd,Rn,Rn,#imm` (matcher + encoder + the letter table's doc block). Only rotate-**right** exists, deliberately: aarch64 has no rotate-left, and `rotl(b,1) ≡ rotr(b,63)` covers boxing. `libmir.a` rebuilt; `ref/mir` + `include/mir.h` synced. Verified by `temp/mir_rotr_smoke.c` (130 checks: JIT immediate form, JIT register form, and the `rotl(b,1)`/`rotr(b,1)` round trip). |
| **P2 sentinels** | `ITEM_SENTINEL_TAG` (`0x1F`) introduced; both JS sentinels moved off `0x9E`/`0x9F` onto it with distinct payloads, so the `100` octant is now unoccupied. |
| **P3 asserts** | Tag-space partition comment rewritten to state all three octant roles; `ITEM_INT_OCTANT` / `ITEM_TAG_IS_NOT_INLINE_INT` added; the `LMD_TYPE_COUNT <= 0x20` assert kept but re-motivated as a **hard ceiling** (0x80–0x9F is no longer tag headroom) and paired with a `LMD_CONTAINER_HEAP_START` bound plus sentinel-tag asserts. |
| **R0.1 accessors** | `lambda_int_item_to_i64` added beside `lambda_int_item_value`; **all 68 raw `get_int56()` reads migrated** across 22 files, classified by consumer (double-feeding sites → `_value`, index/count/i64-storage → `_to_i64`). `it2i` was already a funnel, so its several-hundred callers needed no change. |
| **R0.2 emission** | `emit_box_int` documented as *the* box funnel (peer: `emit_unbox(.., LMD_TYPE_INT)`). **`emit_unbox_int_mask` deleted** with its 6 call sites: it masked values that were *already* native `int64`, so it was not a representation dependency at all — only a name that looked like one, and a live bug (a negative `offset`/`limit` masked into a huge positive, emptying the array; the runtime already clamps negatives correctly). |
| **R0.3 tests** | 6 int representation tests in `test/test_item_repr_gtest.cpp`, written against the canonical encoder/accessors rather than raw bits so they survive the cutover: round-trip through both accessors, canonical encoding (equal ⇒ bit-identical, distinct ⇒ never colliding), out-of-double-space, no collision with internal sentinels, poison distinctness/classification, and container store without borrowed tail storage (`extra == 0` — the §0.3 defect as an assertion). |

**Phase R1 — the encoding cutover — is LANDED.** Baseline **3700/3716**. Ints are now carried
as rotated IEEE bits at every magnitude; `2147483647 * 2147483647` stays `int` (O1 closed),
`type(1.0)` is still `float`, poison prints `int.inf`/`int.nan`, and no int value allocates.

| R1 item | What landed |
|---|---|
| **R1.1 encoders** | `ITEM_INT_CLASS_MASK`/`_BITS`, `lambda_rotr64`, `lambda_int_box_double`, `lambda_int_unbox_double`, the six sentinels (`ITEM_INT_ZERO`/`ONE`/`NEG_ONE`/`INF`/`NEG_INF`/`NAN`), and the `>= 2^257` drift arm — all header-only so `core/` needs no runtime link. **`i2it` is now total** (`lambda_int_box_double((double)v)`); its `ITEM_ERROR` overflow arm — the O1 mechanism — is gone. |
| **R1.2 dispatch** | `type_id()` gained the inline-int arm (sign test after the double test). `int2it` reimplemented as the canonical cold-path encoder (no allocation); `int2it_i64` added for callers holding a native i64. |
| **R1.3 poison** | Sentinel payloads 3/4/5; `LAMBDA_INT_VALUE_IS_*` re-expressed over the *double* value; printing extracted to one shared `print_int_value` used by both the Item visitor and the TypedItem printer. |
| **R1.4 pack path** | `pack_compact_int` packs through `lambda_int_box_double`, so interpreter and JIT cannot disagree about the boundary. |
| **R1.5 JIT** | `emit_box_int_double` (class test + `MIR_ROTR`, cold → `int2it`); `emit_box_int` converts i64→double first. **Both flexint promote lanes now box as `int`** instead of float — the O1 fix in generated code. |
| **emitter sweep** | The cutover surfaced *five* more open-coded compact-encoding sites that R0.2 had not reached, each found by a failing test: the JS `jm_box_int_const`/`jm_box_int_reg` pair plus three `ITEM_INT_TAG \| reg` index boxings (array-spread, spread-call, collection inference) — this was ~180 JS failures, since indices 0/1 alias the new sentinels and only index ≥2 diverges; the JIT's `MIR_INDEX_RESULT_BOXED_INT` packed-lane load; and the Python and Ruby transpilers' own box helpers. **No open-coded int tag/mask remains in any emitter.** |
| **storage** | `TypedItem`'s int arm carries `double_val` (D3, pulled forward — the ANY map-field path was silently storing rotated bits); `type_info` is now sized by `LAMBDA_TAG_SPACE_SIZE` because it is indexed by the *tag byte*, which reserved sentinel tags can reach (ASan caught this as a global-buffer-overflow). |
| **sentinel hygiene** | `ITEM_JS_JUBE_LAZY_SENTINEL` was a magic payload on the **int** tag and only survived because the old compact encoding round-tripped it as a value. It is now `i2it(ITEM_JS_JUBE_LAZY_MAGIC)` — a real int value — so property storage round-trips it by construction under any encoding. |
| **goldens** | `int_promotion`, `number_model_realign`, `numeric_fastpath_edges` updated: every changed line is `float` → `int` with the **value unchanged** — exactly C16 ruling 1 + the O1 close-out. |

**A5, D1, R1.6 and the budget re-baseline all landed the same day. Baseline is
`3717/3717` — fully green, which is *better than the starting state* (`3710/3716`, with six
`js_*` ratchet probes already red on master).**

| Item | What landed |
|---|---|
| **A5 — `INT64_ERROR` boundary** | `fn_len`, `index_of`, `fn_idiv_i` and the shift builtins report failure in-band as `INT64_ERROR` (`= INT64_MAX`); the retired compact encoder rejected that value *by accident of its range check*, which is the only reason those errors ever surfaced. The test is now explicit, and folded into `emit_box_int`'s fast-path predicate (`in_class & (val != INT64_ERROR)`) so it costs one extra compare rather than a second branch. `emit_box_int_double`, the hot arithmetic path, stays free of it — a native double lane cannot carry the sentinel. The sentinel convention itself is still owed a proper retirement. |
| **D1 — `ELEM_INT` is a double lane** | Ruled option (b), now real: the packed `int` lane is float64-backed, so per-lane poison is the IEEE special and needs no lane sentinel. Converted: the four `ArrayNum` accessors, `array_int_set`/`array_int_fill`/`array_int_get`, `write_arr_elem_from_double` (which **still rounds** — the int lane holds integers, so a fractional store rounds exactly as the i64 lane did, with poison passing through untouched), the reduction dispatch, `fn_fill`, `fn_unique`, the negate path, histogram/label, `ensure_typed_array`, specialize→generic, and **three separate JIT store paths plus four indexed-load policies** (two new result kinds, `*_FROM_DOUBLE`). The i64 SIMD kernels for vector arithmetic are now gated to `ELEM_INT64`; the int lane takes the representation-neutral path, which is a known Phase F performance item. |
| **R1.6 — deletions** | `LMD_TYPE_INT_BIG` (enum slot, assert, `ITEM_INT_BIG`, `get_type_name` arm), the `INT_BIG` arm of `lambda_item_uses_scalar_home` **and of `lambda_item_adopt_scalar_home`** — so **`int` is formally out of the scalar-home world**, which now covers exactly `int64`, `uint64` and cell-backed tiny floats, as it did before C16. `get_int56` is deleted outright. The compact tag/mask constants in the JS, Python and Ruby emitters were deleted too — the compiler proved them dead (`-Wunused-const-variable`), which is the tidiest possible confirmation that no open-coded encoding survives. |
| **MIR budgets** | Re-baselined, 22 values across 12 probes, after first slimming `emit_box_int` twice: folding the A5 guard into the class predicate, then replacing the cold-path runtime call with an inline closed-form sentinel map — which removed the added **safepoint and scalar home** the call had introduced into every function that boxes an int. Measured against master by stashing: the six `js_*` probes' numbers are **identical** on master, so that growth is entirely pre-existing and none of it is attributable to this work; the six `lambda_*` probes plus the `scalar_home_donation` fixture are this work's, from the rotation sequence replacing mask/or. Expect Phase B (B1) to shrink these materially and require another re-baseline. |
| **gates** | `test/lambda/int_total_c16.ls` + golden pins the plan's §4 properties end to end: totality at the band edge, the O1 repro, poison in all four forms, poison through a container, **per-lane poison in a packed `int[]`** (only expressible because of D1), a sparse-band value round-tripping through a list *and* a map field, and `type()` separating int from float at every magnitude. The C++ representation suite gained the values past the old band (2⁵³, 2⁵⁴, 2⁶²) that the retired encoding could not hold at all. |

**Phases B, C, E and F all landed 2026-08-02. Baseline `3717/3717`; MIR ratchet 15/15.**

| Item | What landed |
|---|---|
| **B1 — flexint dual lane deleted** | `int ⊕ int` is now one `DADD`/`DSUB`/`DMUL`. The range check, the branch and both boxing arms are gone, because C16 left no boundary to test. The `I2D` on each operand is exact **by construction** — any `int` that fits an i64 lane is a float64-representable integer, that being the definition of the domain — so only the arithmetic rounds, once, exactly as the interpreter's pack path does. Emission shrank sharply where int arithmetic lives: `_twice_#` 84→58, `_closed_gap_#`/`_open_gap_#` 80→57, `_accumulate_#` 113→90, one js_script probe −243. Budgets tightened to lock it in. |
| **B1 — shift band test deleted** | The shift builtin still promoted past 2⁵³. Removed: scaling by a power of two moves the exponent and leaves the mantissa alone, so a valid `int` shifts to a valid `int` at any magnitude. `bitwise_lane_preservation` now also prints the exact value (2⁶² as `4611686018427387904`, not the lossy float rendering). |
| **B3 — overflow class retired** | `LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT` deleted; an int result carries the IEEE rule a float does. The field was write-only, so this is a classification correction plus the comments that cited it. |
| **C1 — lexer split by exponent sign** | `positive_exponent_part` / `negative_exponent_part` in `grammar.js`; an integer-spelled mantissa with a non-negative exponent joins `integer` (`10e1` is int 100) and a negative exponent stays float (`10e-1`). The `n`/`m` suffix and imaginary rules take the new token too. Both int-literal *value* parsers had to learn the exponent as well (`build_ast.cpp` via a new shared `lambda_parse_int_literal`, and the JIT's `parse_int_literal`) — strtoll stops at the `e`. |
| **C2 — band diagnostic** ⚠ **REVISED 2026-08-03** | Originally: `1e16` is a compile error, forcing `1.0e308` where a float is meant. **That rule is withdrawn.** An exponent now makes a literal a float, as in C, Python, Java, Go, Rust, Swift, Ruby and Lua — none of which admits an exponent in an integer literal, and all of which type `1e2` as floating-point. The old split made `1e16`/`1e100` fail to parse while the identical `1.0e16` parsed, a distinction no other language draws. It costs nothing to concede: `int` is a **subset** of float admitted by membership, so `let n: int = 1e2` still binds an `int` and `xs[1e0]` still indexes; only `type(1e2)` changes, to `float`. The band now applies to the integer spelling alone. The `.0` migration it forced on `numeric_fastpath_edges`, `vector_performance` and `std/boundary/numeric_limits` is no longer required (those spellings remain valid floats). |
| **E1 — admission by integrality** | `contract_numeric_admit_signed` gained a dedicated `int` arm. It no longer routes through int64 (which cannot even hold the domain) nor gates on ±(2⁵³−1): any finite integral value admits at any magnitude. |
| **E2 — poison admission** | A shared infinity re-tags into int's own (ruling 14 — same value), a foreign nan rejects like `3.5`. Pinned in `test_lambda_errors_gtest`, whose pre-C16 assertions that both were refused are now the C16 rule with the reasoning recorded. |
| **E3 — `is` lattice** | Verified already conformant: `5 is int64` is false, `i32 ⊑ int ⊑ integer` holds, `int ⊑ float` is definitional. No change needed. |
| **E4 — elision int→float arm** | **Does not apply, and must not be added.** Its premise was that both carriers are doubles so admission is a pure retag — true for the β and erasure designs, false for rotation: an int Item is `rotl(bits,1)` and a float Item is raw bits, so the boundary genuinely converts (and sentinels are a table lookup). §3 T-A1's original "Proven ≠ redundant" reasoning stands unchanged. |
| **F1 — range-proven i64 lane** ⚠ **SUPERSEDED BY G0** | For a native-int consumer of ADD/SUB the i64 result is used directly when it lands within ±(2⁵³−1), where it is exact and therefore identical to the double answer; otherwise the double result is narrowed. This was the design's range-proven lane, discharged at run time. **The G0 ruling withdraws that sanction, so F1 is to be deleted by Phase G, not preserved** — with the native lane already a double there is nothing for it to optimize, and it is itself an int-in-an-i64. It stays in the tree only until G1 lands. |
| **F2 — re-measure** | Release build, AWFY suite, median of 3, measured against a stashed pre-C16 build of the same tree. |

**F2 numbers (AWFY, exec_ms, MIR engine).** Geometric mean **+0.5%** — the correctness work is
performance-neutral overall. Recovered by F1: `permute` 1.63→1.62 (was 1.69 before F1),
`towers` 4.21→4.35 (was 4.53). Improved: `sieve` −3%, `nbody` −3%, `json` −2%, `cd` −2%,
`havlak` −2%. **Open regression: `mandelbrot` 81.3→87.9 (+8%), stable across re-runs and not
recovered by F1** — it is float-dominated, so the cause is not int arithmetic itself and wants
its own profile before more lane work. Two further F1 opportunities remain unexplored: MUL has
no cheap exactness proof and always takes the double narrowing, and D1 gated the i64 SIMD
kernels for `ELEM_INT` vector arithmetic to `ELEM_INT64`, leaving the int lane on the
representation-neutral path.

**Also fixed en route (pre-existing, unrelated to C16):** five vendored tree-sitter grammars
(`bash`, `javascript`, `latex-math`, `ruby`, `typescript`) shipped a `parser.c` generated for an
older tree-sitter alongside a newer `src/tree_sitter/parser.h` that no longer declares
`TSFieldMapSlice`. It was invisible only because their `.a` artifacts predated the drift; the
first clean rebuild (`make release`, already failing on master for this reason) removed them and
neither release nor debug could build. Synced each to the compatible header, so a clean build
works again.

**Remaining:** the `mandelbrot` profile above; the two F1 opportunities; and one C16-completeness
gap — **a declared-`int` native i64 lane cannot carry poison**, so `int.inf` admitted at such a
boundary degrades on unbox. The boundary itself is correct (E2); the lane is not, and the
design's answer is the default double lane (§2). Two Radiant `puppertino` failures are
pre-existing on master (verified by stash) and unrelated.

**Supersedes:** the 2026-08-01 revision of this file (compact+cell carrier plan) and
`Lambda_Impl_Int_C16.md`.

**Design authority:** semantics — `vibe/Lambda_Semantics_Formal2.md` C16 (+C17 rejection),
normative in `doc/Lambda_Formal_Semantics.md` §3–§5, §11.4; **representation —
`vibe/Lambda_Type_Int_Boxing.md`** (encoding, sentinels, drift, MIR patch);
representation contract — `Lambda_Design_Item_Boxing.md` §6; scalar ownership —
`Lambda_Design_Scalar_GC_Invariant.md` (int **exits** its scope entirely).
Failure containment is TE-15/TE-16, implemented separately.

**Performance authority / companion:** `vibe/Lambda_Tune_Typed_Vs_C2MIR.md` (M1–M8). C16
deletes M2 outright (Phase B). **Land this plan before re-baselining Result18 typed columns.**

**The change in one line.** Flex `int` = the float64-representable integers, total in
`double`; boxed ints are their own rotated IEEE bits — one `rotl`/`rotr` from the native
lane, no cells, no number stack, no scalar homes, at any magnitude.

---

## 0. Current state

### 0.1 Done and green (kept from the prior revision)

| Item | What landed | Fate under rotation |
|---|---|---|
| **B4 — `div`/`%` stay `int`** | classifier no longer routes int `div`/`%` to float (nor `integer` to decimal); `int_integral_division` serves both arms; truncation/remainder semantics C14c's | **kept as-is** |
| **A3 — int poison** | `int.inf`/`-int.inf`/`int.nan` as reserved compact payloads above `INT53_MAX` (`LAMBDA_INT_VALUE_IS_POISON` family, [lambda.h:1280](../lambda/lambda.h:1280)) | **semantics kept; representation migrates** to sentinel payloads 3/4/5 in R1.3 |
| **B5 — vector lanes** | per-lane poison for vectorized `div`/`%` (`[6,0,8] div 0` → `[int.inf, int.nan, int.inf]`) | **semantics kept; mechanism simplifies** under D1's double lane — IEEE hardware produces the specials, the sentinel-payload writes delete |
| **Poison printing** | three-site duplication extracted to `print_packed_int_elem` (`core/print.cpp`) | **reworked** in R1.3/D1 (lane-aware; sentinel decode) |
| **D2 — C14c cleanup** | `int(…)` wrappers removed from brainfuck/brainfuck2/json2/json_gen2; hang root-caused away; the div/%-sweep **cancelled** | kept |
| **A6** | `INT53_MAX` comment rewritten to "carrier capacity" | **needs one more pass** in R1.6: the compact carrier dies, so `INT53_MAX`'s surviving roles are the *literal/parser band* (rulings 6+9) and the *i64 range-proof bound* (F1) — say exactly that |
| **A2 carrier (inert)** | `LMD_TYPE_INT_BIG` + `int2it` + `box_int_number_stack` + rehoming predicate arm — built, never produced | **scheduled for deletion** (R1.6), not completion |
| **Miscompile fix** | `function_return_may_defer` extended with `function_body_may_check_boundary` (interior checked boundary vs native return lane) | kept; separate concern |

### 0.2 Superseded items → new home

- **A1** (`i2it` overflow arm → cell): dissolved. Under rotation, `i2it` from an `int64`
  source is **total** — every i64 magnitude < 2⁶³ ≪ 2²⁵⁷ is in-band — so the
  `: ITEM_ERROR` arm ([lambda.h:1279](../lambda/lambda.h:1279)) and the O1 divergence die by
  construction in R1.1.
- **A2** (second tag), **§0.3** (carrier home), and the five container escape arms: dissolved
  / cancelled. Record: prior revision in git history; analysis in `Lambda_Type_Int_Boxing.md`
  §2.1–§2.2.
- **A4/A5** (read-site audits): reshaped into the R0.1 accessor funnel — same site inventory,
  now a mechanical centralization instead of a per-site classification.
- **B2** (`pack_compact_int` computing in double): absorbed into R1.4.

### 0.3 Method note (revised for a representation cutover)

The prior revision's lesson — audit readers before flipping producers — generalizes. A
representation change cannot be flipped site-by-site (the canonical-encoding rule forbids
mixed producers), so the plan follows the **double-boxing v3 template**
(`Lambda_Impl_Double_Boxing (done).md`): guardrails and centralization first with **zero
behavior change** (R0), then the encoding switch behind a transition flag (R1, `LAMBDA_ROT_INT`),
then hardening and flag deletion. After R0, every producer and consumer goes through a
canonical helper, so the flag switches one place per direction, and the §0.4-style empirical
audit ("flip, run `test_lambda_gtest`, misreaders announce themselves") still applies — at
helper granularity instead of site granularity.

---

## 1. Representation summary (normative spec: `Lambda_Type_Int_Boxing.md` §3)

| value | boxed encoding |
|---|---|
| `2 ≤ \|v\| < 2²⁵⁷`, integral | `rotl(bits, 1)` — the `100` octant; positives even, negatives odd (sign at bit 0) |
| `0` / `+1` / `−1` | `ITEM_INT \| 0` / `\| 1` / `\| 2` (0 and +1 **bit-identical to today's compact**) |
| `int.inf` / `−int.inf` / `int.nan` | `ITEM_INT \| 3` / `\| 4` / `\| 5` |
| `\|v\| ≥ 2²⁵⁷` | raw double bits (inline float) — reflection drift, spec footnote |

Core sequences: box = class check `(b & 0x7000000000000000) == 0x4000000000000000` → `rotl 1`
(slow path: sentinel select / drift); unbox = bit-63 test → `rotr 1` (sentinel path: one
`payload ≤ 5` compare, à la `Item::get_double`'s `double_ptr <= 1`); unbox→i64 adds
`cvttsd2si`. `type_id()`: `DBL_MASK → FLOAT; (int64)item < 0 → INT; else high byte`.
Literal `0`/`±1`/poison **compile to sentinel constants** — no runtime class check for
constant operands. No int value at any magnitude allocates or touches a scalar home.

---

## 2. Phase order

```text
Phase P (prerequisites: MIR rotate, sentinel relocation, asserts)
    └── Phase R0 (centralize accessors/emitters + guardrails — no behavior change)
            └── Phase R1 (the encoding cutover, flag-gated; deletions; flag removal)
                    └── Phase B (arithmetic: flexint dual-lane deletion, overflow-class retirement)
                            ├── Phase C (frontend: lexer, literals, printing)   [independent of R*/B — can run any time]
                            ├── Phase D (storage: ELEM_INT double lane, map int fields)
                            └── Phase E (boundaries: admission, lattice, elision)
                                    └── Phase F (perf: re-measure)  →  Phase G (one native repr: double)
```

### Phase P — prerequisites (each independently landable now)

- **P1. MIR rotate patch** (`Lambda_Type_Int_Boxing.md` §4). Add `MIR_ROTL`/`MIR_ROTR`
  (64-bit): insn enum ([ref/mir/mir.h:110](../ref/mir/mir.h:110)), descriptor rows
  ([ref/mir/mir.c:217](../ref/mir/mir.c:217)), interp label+`SCASE`
  ([ref/mir/mir-interp.c:1001](../ref/mir/mir-interp.c:1001), [:1323](../ref/mir/mir-interp.c:1323)),
  x86-64 `SHOP` rows — ROL/ROR are the shifts' own `D3`/`C1` family at `/0`,`/1`
  ([ref/mir/mir-gen-x86_64.c:1745](../ref/mir/mir-gen-x86_64.c:1745)) — and aarch64
  `EXTR Rd,Rn,Rn,#imm` patterns ([ref/mir/mir-gen-aarch64.c:1707](../ref/mir/mir-gen-aarch64.c:1707);
  `rotl 1` ≡ `ror 63`). Rebuild `mac-deps/mir/libmir.a`
  ([build_lambda_config.json:140](../build_lambda_config.json:140)); keep `include/mir.h` in
  sync; add a MIR-level rotate smoke test. Until P1 lands, `emit_int_box/unbox` (R0.2) may
  emit the `LSH+URSH+OR` 3-insn fallback behind the same interface.
- **P2. Relocate the JS sentinels** `ITEM_JS_DELETED_SENTINEL` (`0x9E…`) and
  `ITEM_JS_ITER_DONE_SENTINEL` (`0x9F…`) ([lambda.h:1138](../lambda/lambda.h:1138)) to free
  low-tag bytes `0x1E`/`0x1F` (runtime-only constants; `LMD_TYPE_COUNT` = 0x1D today, fits).
  Hard prerequisite for the `type_id()` sign arm — unrelocated they'd classify as int.
- **P3. Assert restructure** at [lambda.h:162-165](../lambda/lambda.h:162): replace
  `LMD_TYPE_COUNT ≤ 0x20` with per-tag `ITEM_TAG_IS_NON_DOUBLE` assertions (as its own
  comment anticipated) **plus** a new invariant: no TypeId may enter `0x80–0x9F` — that range
  is now the int inline space, not tag headroom. Amend `Lambda_Type_Double_Boxing.md` §2.3
  and the `Lambda_Design_Item_Boxing.md` taxonomy (new row: *inline rotated int*).

### Phase R0 — centralize and guard (zero behavior change; every edit testable alone)

- **R0.1. Accessor funnel.** Two canonical read forms, compact-implemented for now:
  `lambda_int_item_value(Item) → double` (exists, [lambda.hpp:300](../lambda/lambda.hpp:300))
  and new `lambda_int_item_to_i64(Item) → int64` for index/count consumers (debug-asserts
  classified-finite input; poison consumers classify first, existing discipline). Migrate all
  raw `get_int56()` payload reads and ad-hoc re-box sequences to them: ~90 sites overall —
  14 `it2i` consumers in `transpile-mir.cpp`, 23 in `lambda-eval-num.cpp`, 13 in
  `lambda-vector.cpp`, remainder in print/compare/convert paths.
  **`transpile.cpp` (39 uses) is the FROZEN C2MIR path — do not touch** (CLAUDE rule 14).
- **R0.2. Emission funnel.** All JIT int box/unbox and int type-test sequences go through
  shared `emit_int_box` / `emit_int_unbox` / `emit_int_type_test` helpers (today emitting the
  compact sequences). Inventory anchors: the inline INT53 re-box at
  [transpile-mir.cpp:10510](../lambda/runtime/transpile-mir.cpp:10510), the arg-site special
  case at [:11382](../lambda/runtime/transpile-mir.cpp:11382), and every emitted
  "high-byte == INT" compare.
- **R0.3. Guardrails + baseline** (double-boxing S0 shape): representation round-trip tests
  (value ↔ Item for band edges, 0/±1, poison, 2⁵³±, 2¹⁰⁰-scale, drift boundary 2²⁵⁷);
  canonical-encoding assertions (one encoding per value); capture MIR budgets and perf
  baseline (release build).

### Phase R1 — the cutover (behind `LAMBDA_ROT_INT`; S1/S2/S3 shape; flag deleted at the end)

- **R1.1. Encoders.** New box/unbox in `lambda.h`: class-check macro, `rotl`/`rotr` forms,
  the six sentinels (payload table §1), drift arm. `i2it(int64)` reimplemented **total**
  (0/±1 → sentinels; else convert+rotate; no range check needed from i64 sources — the
  `ITEM_ERROR` arm dies here, closing O1; verify with `temp/overflow_fn_test3.ls` and the
  `show(2147483647 * 2147483647)` repro, both must show one consistent int answer).
- **R1.2. Dispatch.** `type_id()` sign arm ([lambda.hpp:118](../lambda/lambda.hpp:118));
  GC `item_to_ptr`/marker: `100` octant and INT byte are never pointers (SG7 early-out
  extends); `emit_int_type_test` switches to octant-or-sentinel-byte (two compares).
- **R1.3. Poison migration.** `ITEM_INT_NAN/INF/NEG_INF` and the `LAMBDA_INT_VALUE_IS_*`
  family ([lambda.h:1280-1299](../lambda/lambda.h:1280)) move from 2⁵⁴-based payloads to
  sentinel payloads 3/4/5 with a `payload ≤ 5` classifier; `LAMBDA_INT_IS_ENCODABLE` retires;
  printing reads sentinels (spec §4.6 spellings unchanged).
- **R1.4. Interpreter pack path.** `pack_compact_int_or_float`
  ([lambda-eval-num.cpp:234](../lambda/runtime/lambda-eval-num.cpp:234), callers :535-539,
  :581) becomes "compute in `double`, pack via rotation" — absorbing old B2; interpreter and
  JIT can no longer disagree about rounding.
- **R1.5. JIT lowering flip.** `emit_int_box/unbox` emit `MIR_ROTL`/`MIR_ROTR` sequences
  (+ sentinel screens); constant operands fold to sentinel/rotated immediates at emission
  time.
- **R1.6. Deletions + flag removal.** Delete: compact packing producers, `LMD_TYPE_INT_BIG`
  (slot, `type_id()` normalization, `get_type_name` arm), `int2it`
  ([lambda.h:1777](../lambda/lambda.h:1777)), `box_int_number_stack`, the INT arm of
  `lambda_item_uses_scalar_home` ([lambda.hpp:284](../lambda/lambda.hpp:284)), the JIT
  symbol-table registration, `get_int56`'s remaining internal uses. Re-pass the `INT53_MAX`
  comment (§0.1/A6 note). Run the full gate set, then delete `LAMBDA_ROT_INT` — no
  escape-hatch flag survives, per the double-boxing precedent.

### Phase B — arithmetic (unchanged goals; now lands on the rotation base)

- **B1. Delete the flexint dual-lane emission** — the main perf event and largest single
  deletion: `mir_is_flexint_int_arith` ([transpile-mir.cpp:4645](../lambda/runtime/transpile-mir.cpp:4645)),
  `mir_emit_flexint_native_int` ([:4672](../lambda/runtime/transpile-mir.cpp:4672)), the
  boxing arms in `transpile_binary_out` ([:4680-4840](../lambda/runtime/transpile-mir.cpp:4680)).
  `int ⊕ int` becomes one `DADD`/`DSUB`/`DMUL`; keep `native_int_out` as the carrier
  selector. Expect `test/mir/mir_budgets.json` (MT7, 0% slack) to move; unexplained jumps
  mean a missing gate. **Triage the 6 pre-existing ratchet failures first.**
- **B3. Overflow classification.** Retire `LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT` for the flex
  tier ([lambda-number.hpp:40](../lambda/runtime/lambda-number.hpp:40), set at :183, :205,
  :281; consumers [transpile-mir.cpp:4085](../lambda/runtime/transpile-mir.cpp:4085), :10454).
  The int arm saturates to poison only at the float-range extremes (IEEE does it for free).
- B2 absorbed into R1.4; B4/B5 landed (§0.1).

### Phase C — frontend (independent — can proceed in parallel with everything above)

- **C1. Lexer** ([grammar.js:24-28](../lambda/tree-sitter-lambda/grammar.js:24)): split
  `exponent_part` by sign — integer-spelled mantissa with non-negative exponent joins
  `integer_literal`; negative exponent stays float. `make generate-grammar`; never hand-edit
  `parser.c`. ⚠ The int literal *parser* must accept exponent form or `10e1` fails to value.
- **C2. Literal band diagnostic**: unsuffixed int-form literals outside ±(2⁵³−1) are compile
  errors (`1e16` errors; `1.0e16`/`1e16n` are the fixes). Unchanged by rotation — the band is
  a frontend rule (ruling 9), not a carrier bound.
- **C3. Parser data homes unchanged** (ruling 6): int iff within ±(2⁵³−1), else int64, else
  decimal.
- **C4. Inference**: classifier-internal (B3); verify `int ⊕ int` yields clean `int` static
  type with no ANY downgrade (feeds E4/A1-class elision).
- **C5. Printing** (spec §4.6): finite ints print as integers at every magnitude; poison
  spellings parseable. Golden churn expected only where escapes printed float-formatted.

### Phase D — storage companions (both **ruled** 2026-08-02)

- **D1. `ELEM_INT` = double lane** (option (b), user-ruled; supersedes the old (a)
  recommendation). Backing store becomes f64; the read at
  [lambda-data-runtime.cpp:488](../lambda/runtime/lambda-data-runtime.cpp:488) boxes via
  rotation and can no longer produce `ITEM_ERROR`; no widen-on-store machinery; stores of
  in-band values are a plain double write. Per-lane poison = actual IEEE specials (the B5
  sentinel-payload writes delete); printing is lane-type-aware (rework
  `print_packed_int_elem`). Sweep: ArrayNum factory/SIMD/copy/`==` paths (⚠ representation
  sensitivity noted in the typed-array work), `lambda/io` static-values arm, format side.
  `ELEM_INT` stays a distinct elem type — semantics tag, shared backing.
- **D3. Map `int` fields = double payload words** (user-ruled). `_map_read_field`'s INT arm
  ([lambda-data-runtime.cpp:2167](../lambda/runtime/lambda-data-runtime.cpp:2167)) changes
  from `i2it(*(int64_t*))` to box-from-double; store side writes the double; **JIT
  shaped-field access emission switches int fields to `MIR_T_D` moves** — aligning field
  loads with the native lane (no convert on load). Same pass covers `TypedItem`'s int arm
  (`int_val` → `double_val`), validator numeric arm, `mark_builder`/`mark_reader`, and the
  static input-arena map storage.

### Phase E — boundaries

- **E1. Admission = integrality** (any finite integral double passes an `int` boundary), not
  `INT53_MAX`. Confirm validator + `lambda_type_check` numeric arms.
- **E2. Poison admission** (rulings 12+14): narrowing verifies membership — shared `inf`
  passes and re-tags (sentinel 3/4), foreign nan rejects; sized-int boundaries admit no
  poison; widening needs no check (poison classifies upward).
- **E3. `is` lattice** (ruling 10): drop `int ⊑ int64`; chain `i32 ⊑ int ⊑ integer`;
  `int ∥ int64`; visible flip `5 is int64` → false — enumerate goldens.
- **E4. Elision int→float arm.** Under rotation this is literal: widening an in-band int to
  float is `rotr 1` (the raw bits) — a pure retag, so `lambda_boundary_is_redundant`
  ([type_contract.hpp:31](../lambda/runtime/type_contract.hpp:31)) gains the int→float arm.
  Guard the trap: float→int remains a narrowing with its check.

### Phase G — one native representation: retire every legacy int64 carrier

**Goal (user, 2026-08-02): there is exactly ONE native representation for `int`, the IEEE
double.** Phases A–F made the *boxed* Item single-representation; the register file, the
shaped-field layout and several ancillary carriers still hold ints as `int64_t`. That
disagreement between int's semantic carrier (double) and its physical carriers (i64) is the
same duality C16 set out to delete, just moved from the Item to everything around it.

It is also a single root cause with three visible symptoms, all currently open: poison cannot
ride a declared-`int` parameter, E4's elision cannot be enabled, and int arithmetic pays
conversions at every lane edge (the `towers`/`permute` shape that F1 only partly recovered).

#### G0. THE RULE — one native representation, no exceptions

**Every native representation of `int` is the IEEE double. There is no second one.**

This **supersedes** the design's earlier §2 sanction of "an `i64` lane when range-proven"
(user ruling, 2026-08-02). That sanction is withdrawn: a range proof is no longer a licence to
represent an `int` as an `i64`, however sound the proof. Phases A–F removed int's dual
*boxed* representation; Phase G removes its dual *native* representation, and the rule is the
same one that made the boxed side tractable — one value, one encoding.

The rule reaches the **C signatures too** (user ruling, tightened 2026-08-02). An earlier
draft of this section let a C helper keep an `int64_t` return and convert at the boundary; that
is withdrawn, because it is exactly the split that made the surface feel arbitrary — some sys
funcs returning `i64`, some returning `int`, with nothing but history deciding which.

**The rule that removes the inconsistency: a sys func's C return type mirrors its declared
Lambda return type.** Not a case-by-case judgement — a mapping:

| declared Lambda type | C return type |
|---|---|
| `int` | `double` |
| `int64` | `int64_t` |

After that, `fn_int64` returning `int64_t` is not an inconsistency to explain away; it is the
rule, because its Lambda type *is* `int64`.

Concretely, of the 11 `C_RET_INT64` rows in `sys_func_registry.c`, **ten convert and one
stays**:

- convert to `double` (declared `&TYPE_INT`): `fn_len`, `fn_index_of`, `fn_last_index_of`,
  `fn_ord`;
- convert (declared `&TYPE_ANY`, self-describing, must not mint an i64-flavoured int):
  `fn_band`, `fn_bor`, `fn_bxor`, `fn_bnot`, `fn_shl`, `fn_shr` (and `fn_ushr`);
- stays `int64_t`: `fn_int64`, declared `&TYPE_INT64`.

`ord()` needs no special case: a code point in a double is exact (user, point 2).

**This forces the `INT64_ERROR` retirement rather than merely enabling it.** The sentinel is
`INT64_MAX`, which has no meaning once the return is a `double`, so the shim A5 had to add
inside `emit_box_int` cannot survive the signature change — the failure signal must become a
real one. That is a benefit: it removes the last compatibility shim from the boxing path.

**Scope, stated so the rule is not over-applied (user clarification, 2026-08-02).** This rule
governs the representation of *Lambda `int` values*. It says nothing about the implementation
language's own integers. Lambda, LambdaJS and Radiant remain free to use `int`, `int32_t`,
`int64_t`, `size_t` and friends throughout their internal C/C++ — for lengths, indices, loop
variables, offsets, capacities, counters — wherever those quantities are **not visible to the
user as Lambda values**. Radiant's layout loops, the GC's object indices, a parser's byte
cursor: none of these are `int` lanes and none of them change.

So the test is *typing and visibility*, not *range*: if the language hands the value to a user
program as an `int`, its native form is a double. An `i64` holding something the user sees as
an `int` is a defect, and no range argument excuses it. An `i64` counting bytes inside a layout
pass is just C.

#### G0.2 Why `i64` was never the answer to the range worry

Worth recording, because it is the argument that settles the trade rather than merely accepting
it (user, 2026-08-02): **when range is genuinely the concern, `i64` is not future-proof
either.** It moves the ceiling from 2⁵³ to 2⁶³ and stops. Both are finite; the choice between
them is a choice of which finite ceiling, not a choice between bounded and unbounded.

Lambda already has the unbounded answer, and it is a *type*, not a lane: `integer`
(mpdec-backed, arbitrary precision). So the future direction for a genuinely unbounded count is
**`fn_length() -> integer`** alongside `len() -> int`, not a wider machine word behind `len()`.
That keeps `int` cheap and exact where the ceiling is irrelevant, and puts the unbounded case
in the type that actually delivers it.

#### G0.1 The two costs, accepted with eyes open

The user has accepted both; they are recorded here so nobody later mistakes them for
oversights and reintroduces an i64 lane to "fix" them.

**Range.** `int` as a double is exact only to 2⁵³. For the converted sys funcs this is not
reachable — a length, an index and a code point are all bounded far below it — so the cost is
theoretical there. Where it is real is user arithmetic above 2⁵³, and that is already C16's
specified rounding, not a new limitation.

**A double loop counter.** Not ideal, and accepted. Worth separating the two halves of the
cost, because only one of them is real: the increment and compare themselves are `DADD`/`DLE`,
which cost what `ADD`/`LE` cost on any modern core. The real cost is at *indexed uses*, where
`a[i]` needs a `cvttsd2si` per access.

That second half is recoverable **without** an exemption, by classic strength reduction: keep
the loop's induction variable a double (it is an `int`), and maintain a parallel machine offset
incremented by the element size. The offset is address arithmetic — a machine quantity, not an
`int` — so it is permitted by the rule above, and it removes the per-access conversion
entirely. This is the mitigation Phase G should reach for if the loop cost measures badly,
instead of re-opening the lane question.

#### G1. `type_to_mir`: the JIT native lane — IN PROGRESS (2026-08-03), tree is RED

`type_to_mir` returns `MIR_T_D` for `LMD_TYPE_INT` and `lambda_value_rep` returns
`VALUE_REP_F64`. Every script in `test/lambda/`, `test/lambda/graph/` and
`test/benchmark/jetstream/` generates valid MIR — zero operand-mode errors, and zero
crashers. The suite stands at **3622 / 3717** against a green HEAD, so **95 regressions
remain**.

##### The contract, and how to find every site that breaks it

`transpile_expr` returns a value in its node's **native lane**; the caller boxes. After G0
that lane is `MIR_T_D` for `int` as well as `float`. A site that hard-codes "int means i64"
is therefore a **producer bug to fix**, not a place to coerce at — coercing there only moves
the wrong assumption downstream.

Finding them one MIR crash at a time does not converge. Assert the contract instead:

```c
static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node) {
    MIR_reg_t out = transpile_expr_impl(mt, node);
    TypeId tid = get_effective_type(mt, node);
    if ((tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT) &&
            MIR_reg_type(mt->ctx, out, mt->em.func) != MIR_T_D)
        log_error("node_type=%d tid=%d returned non-double reg=%s", ...);
    return out;
}
```

Three companion probes finish the picture, and all four are worth reinstating rather than
re-deriving: a check in `emit_insn` that every D-domain instruction's register sources are
doubles; a `__LINE__`-carrying wrapper on `emit_machine_index` that reports **double
narrowing** (its own `midx` output arriving back as input); and the same on `emit_box_int`.
Run them **per script** — `log.txt` rotates, so a bulk run's zero count is not evidence.

##### Producer bugs found and fixed

| site | what was wrong |
|---|---|
| `transpile_for`, both the outer and nested index bindings | `set_var(name, counter, MIR_T_I64, LMD_TYPE_INT)` bound the raw machine counter as the user-visible `int`. The counter stays i64 (G0 exception 1); the **binding** is now a separate `MIR_T_D` register fed by `I2D`. |
| `AST_NODE_LAST_INDEX` | returned a boxed Item for a node typed `int`. Now returns the double. |
| `AST_NODE_CURRENT_INDEX` (`~#`) | `pipe_index_reg` **already holds an Item** — an array pipe boxes its counter, a map pipe stores the *key*, which is not a number. Boxing again (I2D over tagged bits, then the int encoder) produced the encoding as a value. Passes through. |
| `get_effective_type`, early `elem_type` return | promised the element type for an array subscript. Its own comment already warned this "makes the consumer box an already-boxed Item" — true for `int` now, because an out-of-range read joins in `ItemNull` and the double lane cannot hold it. Returns ANY for an int element. |
| `get_effective_type`, `ARRAY` + int index | a comment stated the type is ANY; no `return` implemented it, so the node kept its AST element type. |
| `emit_index_value` | narrowed to a machine index, so every consumer narrowed an already-narrowed value. Now returns **int's** lane (unboxing an ANY-typed index expression, which a nested `a[b[i]]` legitimately produces), and narrowing happens once, at the consumer. |
| `emit_checked_index_load` slow arm, the `pidx` join loop | narrowed a value that was already machine. |

##### Tolerance guards: added during bring-up, now all deleted

Three guards absorbed contract violations instead of fixing them, and all three are gone:
`emit_box_int` passing an `i64` through, `emit_box` overriding the declared rep from the
register class, and `emit_machine_index` early-returning on a non-double. With them removed
the corpus is still clean, which is the actual proof that the producers are right. **Do not
reintroduce them** — a violation must surface as a MIR error.

What legitimately remains are *conversions*, used where a value genuinely arrives in another
lane (an ANY-typed operand, a branch join, a machine quantity crossing into an `int`):
`emit_scalar_native_lane` (any lane → the shared double), `emit_machine_index` (double →
range-guarded machine i64, G0 exception 1 only), `emit_machine_count` (expression → machine
i64 for container offsets/limits), and `emit_box_machine_int`.

##### Two semantic repairs the flip forced

1. *`float → int` is not statically refutable.* C16 makes `int` the float64-representable
   integers — a **subset** of float — and admission a membership test. `((n + 2) / 3) * 4` is
   typed float because `/` is, yet every value it produces there is an int.
   `static_boundary_relation` now defers the whole numeric tower
   (`boundary_numeric_admission_is_dynamic`), exactly as it already did for
   `TYPE_NUMBER`/`TYPE_INTEGER`. The `var x: int = <float>` store arm followed: it used to
   truncate inside a loop and widen the variable to ANY outside one, both artifacts of the
   i64 lane; it is now a plain move in the shared lane.
2. *An out-of-bounds read must yield `ItemNull`, which a double register cannot hold.* The
   retired i64 lane smuggled the null's bits through a "native int" register — type-unsound
   but functional. `emit_checked_index_load` downgrades
   `MIR_INDEX_RESULT_NATIVE_INT_FROM_DOUBLE` to the boxed kind when the OOB answer is null,
   and `get_effective_type` reports ANY to match. This costs the native fast path on
   int-array reads; proving in-bounds to recover it is future work.

Also deduplicated: the sysfunc C return convention was decided in **two** drifted places,
now `sysfunc_c_ret_type_id` / `sysfunc_c_ret_mir_type`.

##### Result: 3716 / 3717

From 95 regressions to **1**. What closed the bulk of it was not more lane
plumbing but two findings:

1. **86 of the 95 were never executed.** `test_lambda_gtest` runs procedural
   scripts through `lambda.exe run`, and one crasher takes its whole batch down
   — `proc/cow_alias.ls` passed a raw double where `array_set_cow`'s `int64_t`
   index parameter goes, and the batch process died there, silently costing
   every `proc/` and `conc/` script after it. **The MIR sweep had missed them
   entirely**: procedural scripts only compile `main()` under `run`, so
   `./lambda.exe <file>` never exercised them. Sweep proc/conc with `run`.
2. **The literal convention** (below) plus the producer fixes cleared the rest.

Producer bugs fixed in this round: the COW setter index; `AST_NODE_RETURN_STAM`
typed as its returned expression rather than ANY (its register is unreachable
filler); `INDEX_ASSIGN_STAM`/`MEMBER_ASSIGN_STAM` inheriting their RHS type when
their emitters return an assignment-result Item; `emit_function_return` not
boxing a double handed to an Item-returning frame; and the `var x = 42; x = 3.14`
widening, which must still widen to ANY for an **inferred** var while an
**annotated** `var x: int` keeps its type and admits by membership.

One defect this work introduced and then fixed: `emit_function_return` ran
`it2d` over an **error Item** when the frame returned a double, turning the
error into NaN. The real cause was that `function_return_may_defer` considered
interior declaration boundaries but not **parameter** boundaries — so
`fn f(v: int) int` kept the native double return lane even though its parameter
check can fail. `function_params_may_check_boundary` now gives such functions
the error lane.

##### The one remaining failure is the owed A5 work

`error_propagation` expects `type(len(err))` and `type(index_of(err, …))` to be
`error`; both return `int`. `fn_len` reports failure as `INT64_ERROR`
(`= INT64_MAX`), a convention that worked only because the retired compact
encoder rejected that value *by accident of its range check*. A double lane
cannot reject it — `9223372036854775807` is a perfectly good `int` under C16, so
the error is silently absorbed.

This is the sentinel retirement A5 already flagged as owed, and it needs an ABI
decision rather than a patch: an int-returning sys func whose C result is a raw
double has **no channel** for an error. The options are (a) a callsite argument
check that propagates the error before the call, which makes the node's type
`int | error` rather than `int`; or (b) an error channel alongside the double
return, as `RETURN_LANE_ERROR` already does for user functions. The test is
correct as written and should stay failing until one is chosen.


#### G1 (original plan text)

`type_to_mir` ([transpile-mir.cpp:383](../lambda/runtime/transpile-mir.cpp:383)) returns
`MIR_T_I64` for `int`; it must return `MIR_T_D`. This is an **ABI change**, not a local edit:
`type_to_mir` feeds MIR function prototypes, so every generated function's int parameters and
returns change register class, including the boxed `_b` wrappers and cross-module calls.

Scope measured 2026-08-02: 25 `type_to_mir` call sites, ~150 `LMD_TYPE_INT` sites in
`transpile-mir.cpp`, 20 `mir_is_native_scalar_value_type` consumers.

Sequence it like the R0/R1 cutover, which worked: centralize every "int is an i64 register"
assumption behind a helper with **zero behavior change**, then flip `type_to_mir` behind a
transition flag, then delete the flag. The zero-golden-churn gate applies again — this is a
representation change with no semantic content. The emitter sweep that bit hardest during R1 is
already done.

#### G2. Shaped map/element/object fields — UNPROVEN

A declared `int` field still stores `int64_t`:
`_map_read_field` reads `i2it(*(int64_t*)field_ptr)`
([lambda-data-runtime.cpp:2173](../lambda/runtime/lambda-data-runtime.cpp:2173)), and the layout
comes from `type_info[LMD_TYPE_INT] = {sizeof(int64_t), ...}`
([lambda-data.cpp:262](../lambda/core/lambda-data.cpp:262)) — whose comment still says "64-bit
to store 56-bit value". Same width, so no layout churn; the change is the field's *type* and
every read/write pair, plus the JIT's shaped-field access emission (int fields become `MIR_T_D`
moves, which also removes a convert on load).

Note this is a **different carrier** from `TypedItem`, which R1 already converted: `TypedItem`
backs `ANY` fields, `type_info` backs declared ones.

`int[]` (`ELEM_INT`) is already double — D1 did it.

#### G3. `Range` — **RULED 2026-08-03**: band-limited, no struct change

An `int` range is limited to bounds within ±(2⁵³ − 1); outside that it is an
out-of-range error, and `1n to N` (an `integer` range) is the spelling for
larger sequences. See `Lambda_Formal_Semantics.md` §4.8.

This is the plan's option (b), and it closes G3 **without touching `Range`**:
with the bounds band-limited, `start`, `end` and `length` are all proven i64 and
stay as they are. The remaining work is the boundary check itself, plus the
error.

The reason it is the right ruling rather than the convenient one: a range needs
a well-defined successor (`x + 1` distinct from `x`), which in binary64 holds
exactly to 2⁵³. Above it the failure is *silent* — at 2⁷⁰ the spacing is 2¹⁸,
so `big + 2 == big` and `len(big to big + 2)` is 1. Every step there is correct
arithmetic; the band check is what stops the language from answering a question
the user did not mean to ask.

Original framing, retained for the record:

#### G3 (superseded framing) — DECISION REQUIRED, currently UNPROVEN

`struct Range { int64_t start, end, length; }`
([lambda.h:801](../lambda/lambda.h:801)). A range is an int-domain object: `1 to n` yields int
elements, and iteration boxes `i2it(range->start + i)`. With `int` now reaching 2²⁵⁷, `start`
and `end` are unproven i64s.

But `length` is an iteration count, and a double `length` would be strange for the loop
machinery. The honest options are (a) `start`/`end` become double while `length` stays a proven
i64 with "an iterable range is bounded by memory" as the named invariant, or (b) ranges are
*defined* to be band-limited, making all three proven. **This is a semantic decision, not a
mechanical one, and should be ruled before the code moves.**

#### G4. Const pool and AST literal storage

Int literals are appended to the module const pool as `int64_t`
(`arraylist_append(tp->const_list, &item_type->int64_val)`,
[build_ast.cpp:3870](../lambda/runtime/build_ast.cpp:3870)). C2 keeps literals inside
±(2⁵³−1), so the *values* are safe — but under G0 that is no longer the point: this is storage
for a value the language calls an `int`, so it becomes a double like every other. Being in band
means the conversion is exact and the migration is mechanical, not that the `i64` may stay.
Confirm the const-pool *read* side boxes through the encoder rather than reconstructing a
payload.

#### G5. `LambdaNumericRuntimePart.signed_value` — UNPROVEN

`part->signed_value = lambda_int_item_to_i64(item)` for `LAMBDA_NUM_INT`
([lambda-number-runtime.hpp:51](../lambda/runtime/lambda-number-runtime.hpp:51)). This is the
decode struct behind comparison and arithmetic classification, so an int above 2⁶³ truncates
*before* any comparison happens. It already has a `float_value` member; `int` should decode into
it, with the part kind reporting the int domain.

#### G6. Guest-language transpilers — UNPROVEN

JS, Ruby and Python each carry their own int lane assumptions (45 / 40 / 25 `LMD_TYPE_INT`
sites). R1 fixed their *boxing*; their native lanes are untouched. Their range checks are guest
semantics and must survive — JS symbol encoding below `-JS_SYMBOL_BASE`, Python/Ruby bigint
promotion — so this is "change the lane, keep the guest rule", exactly as the R1 boxing fix was.

#### G7. E4 becomes available — the payoff

With int and float both `MIR_T_D`, an `int → float` boundary in native code is a **no-op**, so
`lambda_boundary_is_redundant` ([build_ast.cpp:1366](../lambda/runtime/build_ast.cpp:1366)) may
finally return true for it. Two constraints the arm must respect, or it will corrupt values:

- The **boxed** path still converts (int is `rotl(bits,1)`, float is raw bits), so the elision
  is conditioned on the *lane*, not on the type pair alone.
- `float → int` remains a narrowing and keeps its check. E4 is one-directional.

Phase E recorded that E4 does **not** apply today for exactly this reason; G7 is the change that
makes the recorded premise true rather than aspirational.

#### G8. Sysfunc C signatures — convert ten, keep one

Apply the G0 mapping: `C_RET_INT64` → `C_RET_DOUBLE` for every row whose declared Lambda type
is `int` or `ANY`, leaving `fn_int64` alone. Ten functions change signature
(`fn_len`, `fn_index_of`, `fn_last_index_of`, `fn_ord`, `fn_band`, `fn_bor`, `fn_bxor`,
`fn_bnot`, `fn_shl`, `fn_shr`/`fn_ushr`); the registry's `C_RET_*` tag, the emitted call's
return register class, and each function body change together.

Retire `INT64_ERROR` in the same pass — it cannot ride a `double` return, so this is forced,
not optional. Each converted function needs a real failure channel; the natural one is the
boxed `Item` return that most of the registry already uses (`C_RET_ITEM`), which also lets the
error carry a payload instead of a magic magnitude. With the sentinel gone, delete the A5 guard
from `emit_box_int` — its whole purpose was to keep that magnitude meaning "error".

**Out-of-range is a soft error, not poison** (user ruling, 2026-08-02). When a length cannot be
represented as an `int`, `len()` returns a **soft out-of-range error** under TE-15
(`Lambda_Design_Type_Enforcement.md`), which skips to the closest safe boundary. It must not
return `int.inf` or `int.nan`: the spec's own "interior vs ingress" line (§4, extracted during
the C17 debate) puts poison strictly *inside* number math, while `error()` marks
parsing/casting/admission boundaries — and a count that will not fit its declared result type is
an admission failure, not an arithmetic one.

Note this path is unreachable for today's collections: 2⁵³ elements exceeds any addressable
array by orders of magnitude. It exists for the cases that motivate `fn_length()` above —
lazy, virtual or streamed collections whose count is not bounded by memory — so build the
error path now and let it stay cold.

#### G9. Gates

- Zero golden churn through G1/G2/G5/G6 — they are representation-only.
- The C16 completeness case that is currently failing becomes a test: `fn f(x: int)` called with
  `int.inf` must round-trip, which no i64 lane can do.
- Re-run the AWFY comparison. G1 should recover the `towers`/`permute` conversion cost and let
  F1 revert from load-bearing default to the optimization it was designed to be.
- **Profile `mandelbrot` first** (open from F2: +8%, stable, float-dominated, not recovered by
  F1). If its cause is unrelated to the int lane, G1 will not fix it, and that is worth knowing
  before touching ~250 sites.

### Phase F — performance

- **F1. Range-proven `i64` lanes** for loop counters and indices (extends the landed native
  counted `for`). This is `INT53_MAX`'s surviving runtime role: the proof bound below which
  i64 and double arithmetic agree exactly. Unproven ⇒ double ⇒ still correct.
- **F2. Re-measure**: `run_benchmarks.py -e mir,c2mir,go --typed`; update Result18 typed
  columns + the tuning doc's per-class scorecard. Expected movement: sieve-class (M2 gone
  from the body), fib-class (M2 gone from `n-1`). Also record boxed-unbox deltas: to-double
  now 1-op (was sign-extend+`SCVTF`), to-index pays `cvttsd2si` (F1 mitigates). Release
  build only.

---

## 3. Convergence with the tuning work (updated where rotation changes the row)

| Tuning item | What this plan does to it |
|---|---|
| **M2** — flexint lane boxes declared-int arithmetic | **Deleted** (B1). `int ⊕ int` is one `DADD`. |
| **A2** — checked-unboxed int arithmetic plumbing | Survives as the carrier selector; its cold lane becomes **rotation box** — a carrier change at the *same* type (previously box-float-then-`it2i`, a type change). |
| **A2** — unwired consumers | Subsumed: with `double` the default native carrier, unboxed *is* the lane. |
| **M1/A1** — boundary elision | Amplified: honest `int ⊕ int : int` types (C4) + the E4 int→float retag arm — now literally `rotr`. |
| **M3/A3 + D2** — native/scalar returns | Payoff grows: pure-int bodies are provably non-raising **and** their returns are self-contained words (no scalar-home threading ever) — more functions qualify for native returns with no error lane. Implement after this plan. |
| **B1 (tuning)** — native counted `for` | ⚠ Its i64 induction variable is an int-typed value in an i64, which **G0 forbids**. Phase G must move it to a double lane; if that costs measurably, the answer is a better loop lowering, not an exemption. |
| **M7/C1/C2** — typed arrays | `ELEM_INT` lane decision now **ruled** (D1 = double lane); the `i2it → ITEM_ERROR` read hole closes structurally. |
| **O1** — INT53 divergence | **Closed** at R1.1 (`i2it` total). |
| "Don't add declared types to benchmark sources" | Keep until F2 re-measures. |

## 4. Gates

- `make test-lambda-baseline` 100% per phase; `make build-test` green; ratchet triage before B1.
- **Zero-golden-churn gate for P/R0/R1**: the rotation cutover is semantics-neutral, so any
  golden diff before Phase C/E is a defect by definition — this is the strongest single check
  the plan has; use it.
- R0.3 representation round-trip suite; canonical-encoding assertions on in debug builds.
- New `*.ls` tests + goldens: totality (`(2⁵³−1) + (2⁵³−1)` stays int, exact), `div`/`%`
  poison both domains, poison print/round-trip, sentinel values through containers/maps/
  closures/forced GC (the old §0.3 repro `show(2147483647 * 2147483647)` becomes a test),
  a 2¹⁰⁰-scale int in list/map/`int[]`, drift boundary behavior at 2²⁵⁷, `10e1`/`10.e1`/
  `1e16`, lattice flips, narrowing nan rejection, widening poison flow-through.
- MIR budgets re-baselined per phase **with justification** (R1.5 changes sequences, B1
  shrinks them; both must be explained).
- **Equivalence properties asserted directly**: erasing the F1 `i64` lane changes no result;
  boxed and native consumption of the same expression agree (O1 close-out via
  `temp/overflow_fn_test3.ls`); raw-word equality ⟺ value equality for int Items
  (canonical encoding).
- Total-order/hash spot-checks: int vs float same-value compare and map-key behavior
  unchanged across the cutover.

## 5. Open before coding

- **P1 approval** — the MIR fork precedent (five table rows + `libmir.a` rebuild). Fallback
  if declined: β re-bias (`Lambda_Type_Int_Boxing.md` §2.5) — same plan shape, different
  R1.1/R1.5 sequences, no P1.
- Sentinel payload assignment confirmation (§1 table; note 0/+1 keep today's bit patterns —
  any stale compact reader is still correct for exactly those two values during review).
- Whether `Lambda_Type_Numbers.md` / `Lambda_Semantics_Number_Model.md` take the same
  amendment pass (both predate C16; the latter's `is`-lattice section is stale per ruling 10).
- Bitwise/shift domain over large ints (C16 open item; recommendation on record: define
  within ±(2⁵³−1), `error()` outside).
- ruling 13 residue: `integer.inf`/`integer.nan` + `integer div integer → integer` share the
  mpdec specials unblock with `decimal.inf`/`decimal.nan` — independent of this plan.
