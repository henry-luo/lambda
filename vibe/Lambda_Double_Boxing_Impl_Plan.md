# Lambda Double Boxing v3 — Implementation Plan

- **Status:** PLAN (implements the reviewed v3 design)
- **Date:** 2026-07-10
- **Design:** `Lambda_Type_Double_Boxing.md` (v3 = raw IEEE bits + single-mask discriminator + packed-immediate ±0; reviewed in its Part 7)
- **Representation contract:** `Lambda_Design_Item_Boxing.md` §6 — tagged scalar leaves, **raw container pointers (bit-identical, mask-free)**, inline immediates. Part 7 §7.2's acceptance bar governs: *if an implementation requires tagging or masking any container pointer, it has violated the design rather than implemented it.*
- **Convention:** `file:line` references drift; confirm against the symbol name.

## 0. Strategy

Per the Part 7 review: this lands as an **invariant-hardening change first and a float optimization second**. The encoding is simple; the risk is enforcing the tag-space partition across every Item producer, sentinel, raw comparison, GC classifier, and JIT lowering. So the guardrails, assertions, and representation tests land **on master, flag-independent, before any behavior changes**; the representation switch itself sits behind the `LAMBDA_SELF_TAG_FLOAT` compile flag until every gate passes.

Phases (S0–S3 continue the design doc's Part 5 numbering; P0 precedes them):

| Phase | Content | Behavior change | Flag |
|---|---|---|---|
| P0 | Baseline capture & re-measure | none | — |
| S0 | Guardrails: masks, sentinels, assertions, representation tests, audits, lint | none | independent |
| S1 | Runtime core: classifier, encode/decode, packed zeros, producers, FLOAT64 retirement | behind flag (except S1.0) | `LAMBDA_SELF_TAG_FLOAT` |
| S2 | JIT & interpreter lowerings, `MIR_EQ` reroutes | behind flag | `LAMBDA_SELF_TAG_FLOAT` |
| S3 | GC audit, hardening, benchmark sign-off, default flip | flip | default on |

**Canonical-encoding rule (all phases):** within one build configuration, every double has exactly one Item encoding — raw in-band bits, one of the two packed zeros, or the canonical boxed fallback. Mixed old/new producers are forbidden; the flag makes the transition atomic per build.

---

## P0 — Baseline capture (before any code)

- [ ] P0.1 Re-run the release benchmark suite on current master (the T0/L0 re-measure from `Lambda_Tuning_Proposal.md` — the honest-typing rooting fix landed after Result9, so the baseline must be re-established). Record per-benchmark times; this is the A of every later A/B.
- [ ] P0.2 Capture MIR dumps (`temp/mir_dump.txt`, debug build) for one container-access benchmark and one float benchmark — the "before" goldens for the S2 inspection tests.
- [ ] P0.3 Record current `push_d` call rates if `js_exec_profile` counters permit (Part 2 P0 of the tuning proposal); otherwise add a counter. Sizes the prize per benchmark.

Gate: none (measurement only). Deliverable: a dated results file under `test/benchmark/`.

---

## S0 — Guardrails and audits (zero behavior change, lands on master)

**S0 is not scaffolding for the float migration — it is the Item representation acquiring its enforcement layer** (`Lambda_Design_Item_Boxing.md` §7.3 W1 and Evolution Rule 7: every §6 invariant becomes an assertion, a debug check, or a lint rule). The sentinel collisions, raw `>> 56` scatter, open-coded payload dereferences, and representation-dependent raw equality that S0 closes are pre-existing debt; S0 lands and stays **regardless of whether v3 ever ships**.

### S0.1 Shared masks and predicates — `lambda/lambda.h`

Define next to the `EnumTypeId` (`lambda.h:83`), compiled unconditionally (used by asserts now, by the runtime at S1):

```c
#define ITEM_DBL_MASK        UINT64_C(0x6000000000000000)  // word bits 62,61 = exponent bits 10,9
#define ITEM_HIGH_BYTE_MASK  UINT64_C(0xFF00000000000000)
// a high byte is legal for a non-double encoding iff bits 6 and 5 are both clear
#define ITEM_TAG_IS_NON_DOUBLE(tag)  (((tag) & 0x60u) == 0)
```

- [ ] Mirror into the C++ side (`lambda/lambda.hpp`) or include-share; one definition, two languages.
- [ ] Comment on the enum stating the partition rule and pointing at `Lambda_Type_Double_Boxing.md` §2.3.

### S0.2 Compile-time assertions (Part 7 §7.3)

- [ ] `static_assert(sizeof(Item) == sizeof(uint64_t))`, `static_assert(sizeof(uintptr_t) == sizeof(uint64_t))`.
- [ ] `static_assert(LMD_TYPE_COUNT <= 0x20)` — valid while all TypeIds stay in the first legal block. Add the review's caveat as a comment: if a future TypeId jumps to `0x80–0x9F`, replace this with an X-macro/per-tag assertion list, **not** a weaker max-value check.
- [ ] Per-tag predicate check: an X-macro or `static_assert` list asserting `ITEM_TAG_IS_NON_DOUBLE(t)` for every `EnumTypeId` value and for the high byte of every non-float Item sentinel (`ITEM_NULL`, `ITEM_JS_UNDEFINED`, `ITEM_JS_TDZ`, `ITEM_INT`, `ITEM_TRUE/FALSE`, `ITEM_ERROR`, `ITEM_NULL_SPREADABLE`, `lambda.h:871`–`:892`).
- [ ] `static_assert(offsetof(Container, type_id) == 0)`; same pin for every raw-Item family that does **not** inherit `Container` — notably `Function` and `Path` (their `TypeId` must be byte zero; Part 7 §7.3).
- [ ] Packed-zero constants (defined here, used at S1): `ITEM_FLOAT_P0 = ((uint64_t)LMD_TYPE_FLOAT << 56)`, `ITEM_FLOAT_N0 = ITEM_FLOAT_P0 | 1`; assert both satisfy `(x & ITEM_DBL_MASK) == 0` and have the FLOAT high byte.

### S0.3 Sentinel renumbering — `lambda/js/js_runtime.h`

- [ ] `JS_DELETED_SENTINEL_VAL` (`js_runtime.h:30`, high byte `0x7E`) and `JS_ITER_DONE_SENTINEL` (`:35`, `0x7F`) → renumber to bits-6,5-clear high bytes (e.g. `0x1E…`/`0x1F…` or `0x9E…`/`0x9F…`), keeping their distinctive low-bit patterns.
- [ ] Update every comparison site (grep both names; also grep the raw hex constants in case any site open-codes them).
- [ ] Add `static_assert(ITEM_TAG_IS_NON_DOUBLE(...))` pins on both (S0.2 list).

### S0.4 Raw-pointer assertion helpers (Part 7 §7.3)

- [ ] Debug-only `assert_raw_item_pointer(ptr)`: high byte exactly zero, `DBL_MASK` clear, round-trips through `(void*)`. Wire into **both** `p2it` definitions (`lambda.h:989`, `lambda.hpp:476`).
- [ ] Same helper called at Item-visible container allocation/construction boundaries (the `heap_calloc_class`/constructor paths) — `p2it` alone is insufficient because code also builds Items through the C++ union fields (`.container`, `.array`, `.map`, …).
- [ ] Post-initialization debug check for header-bearing objects: object address == header address; header `TypeId` is a valid raw family; `get_type_id(Item{ptr})` == header TypeId; `it2p` returns the original pointer. Do **not** put header validation inside `p2it` itself (it is also an ABI helper for pointer-shaped values whose header may not exist yet — Part 7 §7.3).

### S0.5 Representation tests (Part 7 §7.4) — new `test/test_item_repr_gtest.cpp`

For each of Array, ArrayNum, Map, Object, Element, Range, Function, Type, Path:

- [ ] construct via the normal allocator; capture native pointer and Item bits; assert **exact bit identity**;
- [ ] assert high byte zero and `DBL_MASK` clear;
- [ ] assert `get_type_id()` reads the header type; assert the matching `it2*` accessor returns the original pointer; read one representative field through it.

Plus:

- [ ] the discriminator-ordering test: a synthetic in-band double word whose low 56 bits are deliberately **not** a valid address must classify as `LMD_TYPE_FLOAT` with no header read (run under ASan — a header read would fault); a real container pointer must reach the header branch with the word unmodified.
- [ ] MIR golden-inspection test: transpile a known container field load; assert the emitted MIR contains no `AND`/`XOR`/pointer reconstruction between the Item register and the field load (Part 7 §7.4 — this is a performance invariant as well as correctness). Wire into `build_lambda_config.json` + expected `.txt` goldens per repo convention.

### S0.6 Audits (deliverable: a checked-off audit table appended to this file)

- [ ] **Raw `>> 56` sweep** — ~24 sites / 11 files: classify each as (a) downstream of `get_type_id` (fine), (b) tag-literal compare provably never seeing a float Item (fine), (c) needs the `DBL_MASK` test added at S1. Include `& 0x00FF…` mask-only sites and any private tag arithmetic in the Jube runtimes (`lambda/py/`, `lambda/bash/`, …).
- [ ] **Raw-Item equality audit** — every `MIR_EQ`/`MIR_BEQ` emission on Item words (`js_mir_expression_lowering.cpp:1519`, `:1562`, `:1575`, `:5560`, `:6590`, …, plus Lambda-side `transpile-mir.cpp` equivalents): float-free proof, or marked for `MIR_DEQ`/runtime-helper reroute at S2. The three semantic traps: equal in-band doubles become raw-equal; same-payload NaNs raw-equal (must be `false`); `ITEM_FLOAT_P0` vs `_N0` raw-unequal (must be `true`).
- [ ] **Open-coded payload dereference sweep** — every read of a float payload not going through `it2d`: known: `Item::get_double()` (`lambda.hpp:173`, `*(double*)this->double_ptr`) and the C `it2d` (`lambda-data.cpp:322`); find the rest (`grep '\*(double\*)'` filtered to Item-payload use, not shaped-field slots). Each must route through the S1 two-arm decode or be proven boxed-only.
- [ ] **GC classifier audit** — `item_to_ptr` (`gc_heap.c:770`), `gc_scan_stack` (`:1569`), map-field tracing (`:1017`–`:1058`), `gc_fixup_embedded_pointers` (`:1202`): confirm none can misread a bit-62/61-set word as a pointer once such Items exist, and that GC-internal header tags (`LMD_TYPE_MAP_` etc. past `LMD_TYPE_COUNT`) are never materialized into Item high bytes.
- [ ] **Hashing/SameValueZero audit** — every value-keyed container (JS `Map`/`Set`, any Item-keyed hashmap): confirm float keys are hashed via unboxed value with NaN-canonicalization and −0→+0 normalization (required today for pointer-distinct boxed floats; must survive the rewrite — design §3.4/§3.5).

### S0.7 Lint rules — `utils/lint/rules`

- [ ] New rule (style of `no-int-cast-radiant`): flag any new 64-bit integer constant whose high-byte bit 6 or 5 is set, outside the float representation module.
- [ ] Flag any new `>> 56` comparison against an integer literal not expressed via the `EnumTypeId`.
- [ ] Run in `make lint`; whole tree must be clean at S0 exit.

### S0.8 Semantic equality — one entry point (Item Boxing Evolution Rule 9)

Raw Item bit-comparison is representation-layer code; this bug family has shipped at least three times (ArrayNum `==` representation-sensitivity, NaN identity, packed ±0). Groundwork here, enforcement grows with S1/S2:

- [ ] Inventory every raw Item comparison in C/C++ runtime code (`.item ==`, `raw ==` on Item words) — the C-side sibling of the S0.6 `MIR_EQ` audit. Classify: (a) identity-semantics types only (objects/functions — fine), (b) sentinel compares (fine once sentinels are pinned), (c) value types where bit-equality is representation-dependent → route through the canonical equality helpers (`js_strict_equal`-class / Lambda `item_equal`).
- [ ] Lint rule: flag new raw Item equality comparisons outside the representation layer and the audited allowlist (ratchet — the allowlist only shrinks).
- [ ] Each surviving raw compare carries a one-line comment naming its proof (per CLAUDE.md rule 12 — state the invariant being relied on).

**S0 gates:** `make test-lambda-baseline` 100 %, `make test-radiant-baseline` 100 %, JS gtests, `make node-baseline` unchanged, `make lint` clean. No benchmark movement expected (assert-only in release = none).

---

## S1 — Runtime core (behind `LAMBDA_SELF_TAG_FLOAT`)

### S1.0 Retire `LMD_TYPE_FLOAT64` (flag-independent prerequisite)

Design §3.7: two runtime TypeIds for one binary64 domain break canonical encoding. Coordinate with `Lambda_Semantics_Number_Model.md` Part 2 W-items (`f64` is a parse-time alias per its §3.2).

- [ ] `f642it` (`lambda.h:912`) producers → produce `LMD_TYPE_FLOAT` Items; keep `f64` as an alias resolving at parse/annotation level.
- [ ] Remove `LMD_TYPE_FLOAT64` arms from `is_numeric_type_id` (`lambda.h:882`), type switches, formatters, and the validator; `type()` output already says `float` per the alias rule — verify goldens.
- [ ] Gate: full baselines green **before** proceeding (this is user-visible surface; land separately).

### S1.1 Classifier — `Item::type_id()` / `get_type_id`

`lambda.hpp:159` (and the C-side equivalent) becomes, under the flag:

```c
if (item_bits & ITEM_DBL_MASK) return LMD_TYPE_FLOAT;   // MUST be first — before any deref
uint8_t tag = item_bits >> 56;
if (tag != LMD_TYPE_RAW_POINTER) return (TypeId)tag;
if (item_bits != 0) return *(TypeId*)(uintptr_t)item_bits;
return LMD_TYPE_NULL;                                    // preserve canonical zero-Item behavior
```

- [ ] The raw-pointer branch uses the **original word unchanged** (Part 7 §7.2 — no masking of container pointers, ever).
- [ ] Debug assert in the raw branch: high byte is zero before the header dereference.

### S1.2 Encode / decode primitives

- [ ] `flt2it(double)` (new, `lambda.h`/`lambda.hpp`): mask-test the raw bits; in-band → store raw; `d == 0.0` → `ITEM_FLOAT_P0 | signbit(d)`; else `box_float_cold(d)` (existing `push_d` path).
- [ ] `it2d` (`lambda-data.cpp:322`) and `Item::get_double()` (`lambda.hpp:173`): hot arm = mask test + bitcast; cold arm = `it2d_cold`: `p = item & MASK56; if (p <= 1) return p ? -0.0 : +0.0; return *(double*)p;`. `it2d_cold` is the **single sanctioned reader** of boxed payloads and packed zeros — retire every open-coded deref found at S0.6.
- [ ] Assertions (Part 7 §7.3): encode produces only {raw in-band bits, `ITEM_FLOAT_P0/_N0`, canonical boxed fallback} — never a raw out-of-band bit pattern; decode accepts only those same three forms.

### S1.3 Producers canonicalize

- [ ] `push_d` / `push_d_safe` (`lambda-mem.cpp:598`, `:643`) become thin wrappers over `flt2it` (nursery allocation only inside the cold arm).
- [ ] `js_make_number` (`js_runtime_value.cpp:1071`) → `flt2it`; its minus-zero guard maps onto the packed constants.
- [ ] Input parsers / MarkBuilder float construction (`mark_builder.hpp` and `lambda/input/*`) → the canonical encoder; heap Float objects remain only for the boxed fallback.
- [ ] Formatters/printers: verify all float reads go through `it2d`/`get_double` (S0.6 sweep closes stragglers).
- [ ] `gc_fixup_embedded_pointers` and the array extra-area write path: unchanged for boxed floats; verify inline-double Items in items[] are skipped (their tags fall outside FLOAT/INT64/DTIME pointer-tag checks — audit, don't assume).

### S1.4 GC-side classifier

- [ ] `item_to_ptr` (`gc_heap.c:770`): first line `if (item & ITEM_DBL_MASK) return NULL;` — an unknown high tag must never fall through to the "treat as raw pointer" arm without excluding double space (Part 7 §7.3).
- [ ] Conservative scan: inline doubles are now skipped by the early-out (cheaper than today's failing range probes); packed zeros have payloads 0/1 — never valid addresses.
- [ ] Keep semantic `MIR_T_P` rooting decisions unchanged: a raw pointer physically in an `MIR_T_I64` register is still a pointer for rooting (Part 7 §7.3); float locals remain unrooted (`should_gc_root_var`, `transpile-mir.cpp:479` — already excludes FLOAT).

### S1.5 Hashing / equality normalization

- [ ] SameValueZero hash sites: normalize NaN → one canonical pattern and −0 → +0 (clear payload bit 0 of `ITEM_FLOAT_N0`) before hashing.
- [ ] `js_strict_equal` (`js_runtime_value.cpp:1522`) is already value-based — verify unchanged behavior under the flag for: NaN≠NaN, +0===−0, in-band bit-equal fast path.

**S1 gates:** full test262, `make test-lambda-baseline` 100 %, Radiant baseline, node-baseline — each **flag on and flag off**; new property gtests (§Test matrix); ASan run of the lambda + JS gtest suites with the flag on.

---

## S2 — JIT and interpreter lowerings (behind the flag)

### S2.1 Lambda MIR transpiler — `lambda/transpile-mir.cpp`

- [ ] Replace emitted `push_d` calls with inline: `AND tmp, bits, DBL_MASK; BT in_band; <cold call>` — the stored value is the untransformed bits register (off the value's critical path).
- [ ] Unbox sites: inline mask test + scratch-slot store/load bitcast (MIR has no int↔float reinterpret; the slot dance is ~4–6 cycles; a vendored `bitcast` insn is a later micro-win, not v1).
- [ ] Float constants: emit in-band literals directly as 64-bit immediates (their IEEE bits); zero as `ITEM_FLOAT_P0/_N0`.

### S2.2 LambdaJS lowering — `lambda/js/`

- [ ] `jm_box_float` (`js_mir_calls_boxing_types.cpp:406`): same inline encode; cold call for zero/subnormal/tiny.
- [ ] `jm_box_int_reg` overflow arm (`:356`): `I2D` + inline encode (result always in-band for |n| > 2⁵³; keep the check for uniformity or prove and skip).
- [ ] `jm_emit_unbox_float` and every `it2d`-emitting site: inline two-arm decode.
- [ ] Apply the S0.6 `MIR_EQ` reroutes: each non-float-free site → `MIR_DEQ` after unbox, or the runtime helper.

### S2.3 Cross-cutting

- [ ] Apply `DBL_MASK` **only** to universal Item values — never to statically native `MIR_T_D`/`MIR_T_P` registers (Part 7 §7.3); the native fast paths must not start round-tripping through Items.
- [ ] Known-type container unboxing stays identity: re-run the S0.5 MIR golden-inspection test — container field loads must show zero new instructions (the acceptance bar).
- [ ] MIR **interpreter** mode: the same lowered MIR runs under `MIR_set_interp_interface` — run the S1/S2 gates in interp mode too (mem-ops and immediates behave identically, but verify, don't assume).
- [ ] Legacy C2MIR path (`--c2mir`): the C transpiler (`transpile.cpp`) consumes the same runtime helpers — confirm it compiles against the flagged `flt2it`/`it2d` and passes its smoke tests.

**S2 gates:** S1 gates repeated (both flag states, JIT + interp) **plus** benchmark A/B against P0: primary movers nbody / mandelbrot / navier_stokes / matmul / splay(float keys); regression canaries richards / splay / deltablue (float-light, branch-cost sensitive); MIR golden diffs reviewed.

---

## S3 — Hardening, sign-off, default flip

- [ ] S3.1 GC verification pass: `gc_fixup_embedded_pointers` skip behavior with mixed inline/boxed arrays; conservative-scan false-retention spot-check (benign over-approximation only); a GC-stress run (`gcbench`, `binarytrees`, `storage`) with the flag on under ASan.
- [ ] S3.2 Full ASan benchmark-suite run, flag on.
- [ ] S3.3 NaN/zero semantics suite green (§Test matrix); NaN-payload-as-Map-key pin; typed-array NaN round-trip bit-identity pin.
- [ ] S3.4 Release benchmark suite, flag on vs P0 baseline — publish the table; geometric-mean regression on float-light benchmarks must be ≤ noise.
- [ ] S3.5 Flip `LAMBDA_SELF_TAG_FLOAT` default on; keep the flag-off build compiling and green for one release as the escape hatch; then delete the flag and any dead interned-payload code.

**Acceptance bar (Part 7 §7.5, verbatim requirement):** all container Items bit-identical to their native pointers; all known-type accessors mask-free identity conversions; the only new work in generic container type discovery is the leading inline-double discriminator.

---

## Test matrix (new tests, mostly S0/S1)

| Test | Pins | Phase |
|---|---|---|
| `test_item_repr_gtest` container suite | raw-pointer bit identity, header dispatch, accessor identity (9 families) | S0 |
| Discriminator-ordering test | inline double classified with no memory access; container word unmodified | S0 |
| MIR golden inspection (container load) | no AND/XOR/reconstruction before field load | S0, re-run S2 |
| Encode/decode property tests | band edge `e`=0x1FF/0x200; bit identity for in-band (`Item == bits(d)`); discriminator ⟺ `e ≥ 0x200` exhaustive exponent sweep; subnormal/tiny → boxed | S1 |
| Packed-zero suite | encode(±0) yields exactly `ITEM_FLOAT_P0/_N0`; `1/decode == ±Inf`; payloads 0/1 never read as pointers | S1 |
| Inf/NaN suite | ±Inf inline round-trip; quiet/signaling NaN payload bit-preservation; `NaN !== NaN`; `0.0 === -0.0`; `Object.is(-0, 0) === false` | S1 |
| SameValueZero keys | NaN (any payload) and ±0 as `Map`/`Set` keys collapse correctly | S1 |
| Equality reroute regressions | the S0.6 `MIR_EQ` sites exercised with float operands | S2 |
| Interp-mode replicas | the above run under the MIR interpreter | S2 |
| Lambda script goldens | `test/lambda/` additions: float band-edge literals, ±0 arithmetic, Inf/NaN formatting (with `.txt` expected results, per repo rule) | S1 |

---

## Risk register

| Risk | Phase | Mitigation |
|---|---|---|
| A raw-EQ site missed by the audit changes JS equality semantics | S0/S2 | audit is a hard S0 deliverable; test262 equality clusters + targeted regressions; both flag states diffed |
| `get_type_id` branch cost on float-light workloads | S2 | richards/splay/deltablue canaries in the A/B; §2.9 variants B/C remain drop-in swaps of the primitives |
| Hidden Item producer bypasses `flt2it` (mixed encodings) | S1 | canonical-encoding asserts in decode; ASan + property tests; the flag confines any escape |
| Container-pointer masking creeps in via a "helpful" refactor | any | MIR golden-inspection test fails the build; Part 7 acceptance bar is the review checklist |
| Platform pointer tagging (TBI/MTE) leaks high bytes into Items | future | `assert_raw_item_pointer` fails loudly; per Part 7 §7.2, normalize at the allocation/ABI boundary under an explicit platform design, never by masking dereferences |
| Rollback | S3 | flag off = v1 behavior; S0 guardrails, sentinel renumbering, FLOAT64 retirement, and audits are keep-regardless |

## Sequencing notes

- S0 and S1.0 are independently landable and valuable regardless of the representation switch — start there. S0 in particular is the enforcement layer the Item design has been missing (`Lambda_Design_Item_Boxing.md` §7.3 W1 / Evolution Rules 7–10); even a decision to abandon v3 keeps all of S0.
- T1/T2/T3 of the tuning proposal (ICs, call path, literal shapes) are orthogonal and may proceed in parallel; T5 (dense-double arrays), Part 2 (stack boxing residue), and the L4/T4 nursery redesign are **re-ranked after S3** with float traffic gone (design doc §5.3).
- The numeric-nursery leak (design doc §1.4) shrinks to int64/datetime/decimal + subnormal-float residue after S3; its reclamation belongs to the nursery redesign, not this plan.
