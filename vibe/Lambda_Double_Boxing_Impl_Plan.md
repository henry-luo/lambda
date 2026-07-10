# Lambda Double Boxing v3 — Implementation Plan

- **Status:** S2 COMPLETE — flag-gated Lambda/LambdaJS MIR float lowerings, interpreter-safe bitcast helpers, equality audit/reroutes, and flag-on/off Lambda baselines landed (2026-07-10)
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

## Implementation status — 2026-07-10

S0 guardrails are complete: shared Item double masks and static tag-space pins in `lambda/lambda.h`; raw-pointer assertions wired into both C and C++ `p2it`; allocation return paths assert raw-pointer Item representability; post-initialization header checks pin byte-zero TypeIds for raw Item families; JS sentinel high bytes moved out of double space and centralized through shared constants; `lib/lambda_typed.hpp` hole stamping now uses the same deleted sentinel; `item_to_ptr` rejects inline-double and unknown high-tag scalar words instead of treating them as raw pointers.

Added `test/test_item_repr_gtest.cpp` and wired it through `build_lambda_config.json` / Premake. The test pins raw pointer bit identity for header-shaped raw Item families on stack and through the normal GC allocator; validates the S0 discriminator-ordering safety property without pulling S1's `LMD_TYPE_FLOAT` classifier forward; and compiles `test/lambda/item_repr_container_member_load.ls` to inspect `temp/mir_dump.txt` for mask-free raw Item flow into member lookup.

Verified S0 slice: `test_item_repr_gtest`, `test_gc_heap_gtest`, `test_lambda_typed`, `test_js_gtest`, `test_lambda_gtest`, the three new S0 lint ratchets, and `make test-lambda-baseline` all pass after the allocator/header guardrails. Full repository gates were also run; their unrelated failures are recorded at the end of this section so S0 does not carry a stale "all green" claim.

S1 runtime core is complete behind `LAMBDA_SELF_TAG_FLOAT`: `f64` production canonicalizes to the `float` runtime domain, `LMD_TYPE_FLOAT64` remains only as a legacy reserved/alias tag, universal classifiers check inline-double space before raw-pointer dereference, `flt2it` / `it2d` / `Item::get_double()` own the encode/decode boundary, float producers route through the canonical helper, and JS/PDF/DOM/typed-array stragglers that dereferenced boxed float payloads now use the public numeric conversion helpers. The S1 truthiness fix preserves Lambda semantics that all numbers are truthy, including `0.0` and `NaN`.

Verified S1 slice: forced flag-on build plus focused gtests and `make test-lambda-baseline CFLAGS=-DLAMBDA_SELF_TAG_FLOAT CXXFLAGS=-DLAMBDA_SELF_TAG_FLOAT` pass; forced flag-off rebuild plus focused gtests and `make test-lambda-baseline` pass. Broad release gates not run in this S1 slice — full test262, Radiant baseline, node-baseline, and ASan remain S2/S3 hardening gates before any default flip.

S2 JIT/interpreter lowering is complete behind `LAMBDA_SELF_TAG_FLOAT`: Lambda MIR and LambdaJS now inline the self-tagged float mask/zero/cold-box decision at box sites, inline the in-band unbox dispatch, emit canonical float literal Item bits, route JS int-overflow promotion through the same float boxing helper, and link Lambda MIR direct through `MIR_set_interp_interface` in `--mir-interp` mode. Raw IEEE bit reinterpretation goes through `lambda_mir_double_bits` / `lambda_mir_bits_double`; the first S2 attempt used `MIR_ALLOCA` as a scratch-slot bitcast, but benchmark smokes exposed that as dynamic stack growth in hot loops (`matmul`/`mandelbrot` SIGBUS), so S2 now keeps the representation decision in MIR while using the leaf helpers for the bitcast itself.

Verified S2 slice: forced flag-on release/debug rebuild plus `make test-lambda-baseline CFLAGS=-DLAMBDA_SELF_TAG_FLOAT CXXFLAGS=-DLAMBDA_SELF_TAG_FLOAT` passes 3296/3296 after the bitcast fix; forced flag-off release/debug rebuild plus `make test-lambda-baseline` passes 3292/3292. Lambda `--mir-interp test/lambda/float_conversion.ls` passes in both flag states (the current log banner still says "MIR JIT compilation", but the linker path checks `g_mir_interp_mode` and uses `MIR_set_interp_interface`). JS MIR interpreter focused smoke (`tco`, `lib_ajv`) passes. A one-run release LambdaJS A/B smoke is recorded in `test/benchmark/s2_self_tag_float_ab_flag_off_2026-07-10.json` and `test/benchmark/s2_self_tag_float_ab_flag_on_2026-07-10.json`; it is an S2 implementation smoke, not the full three-run P0/S3 benchmark sign-off.

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

- [x] Mirror into the C++ side (`lambda/lambda.hpp`) or include-share; one definition, two languages.
- [x] Comment on the enum stating the partition rule and pointing at `Lambda_Type_Double_Boxing.md` §2.3.

### S0.2 Compile-time assertions (Part 7 §7.3)

- [x] `static_assert(sizeof(Item) == sizeof(uint64_t))`, `static_assert(sizeof(uintptr_t) == sizeof(uint64_t))`.
- [x] `static_assert(LMD_TYPE_COUNT <= 0x20)` — valid while all TypeIds stay in the first legal block. Add the review's caveat as a comment: if a future TypeId jumps to `0x80–0x9F`, replace this with an X-macro/per-tag assertion list, **not** a weaker max-value check.
- [x] Per-tag predicate check: an X-macro or `static_assert` list asserting `ITEM_TAG_IS_NON_DOUBLE(t)` for every `EnumTypeId` value and for the high byte of every non-float Item sentinel (`ITEM_NULL`, `ITEM_JS_UNDEFINED`, `ITEM_JS_TDZ`, `ITEM_INT`, `ITEM_TRUE/FALSE`, `ITEM_ERROR`, `ITEM_NULL_SPREADABLE`, `lambda.h:871`–`:892`).
- [x] `static_assert(offsetof(Container, type_id) == 0)`; same pin for every raw-Item family that does **not** inherit `Container` — notably `Function` and `Path` (their `TypeId` must be byte zero; Part 7 §7.3).
- [x] Packed-zero constants (defined here, used at S1): `ITEM_FLOAT_P0 = ((uint64_t)LMD_TYPE_FLOAT << 56)`, `ITEM_FLOAT_N0 = ITEM_FLOAT_P0 | 1`; assert both satisfy `(x & ITEM_DBL_MASK) == 0` and have the FLOAT high byte.

### S0.3 Sentinel renumbering — `lambda/js/js_runtime.h`

- [x] `JS_DELETED_SENTINEL_VAL` (`js_runtime.h:30`, high byte `0x7E`) and `JS_ITER_DONE_SENTINEL` (`:35`, `0x7F`) → renumber to bits-6,5-clear high bytes (e.g. `0x1E…`/`0x1F…` or `0x9E…`/`0x9F…`), keeping their distinctive low-bit patterns.
- [x] Update every comparison site (grep both names; also grep the raw hex constants in case any site open-codes them).
- [x] Add `static_assert(ITEM_TAG_IS_NON_DOUBLE(...))` pins on both (S0.2 list).

### S0.4 Raw-pointer assertion helpers (Part 7 §7.3)

- [x] Debug-only `assert_raw_item_pointer(ptr)`: high byte exactly zero, `DBL_MASK` clear, round-trips through `(void*)`. Wire into **both** `p2it` definitions (`lambda.h:989`, `lambda.hpp:476`).
- [x] Same helper called at Item-visible container allocation/construction boundaries (the `heap_calloc_class`/constructor paths) — `p2it` alone is insufficient because code also builds Items through the C++ union fields (`.container`, `.array`, `.map`, …). 2026-07-10: this is an allocation-return pointer-form check only; object header checks remain in the next item because constructors write byte-zero `TypeId` after allocation.
- [x] Post-initialization debug check for header-bearing objects: object address == header address; header `TypeId` is a valid raw family; `get_type_id(Item{ptr})` == header TypeId; `it2p` returns the original pointer. Implemented as C++ `assert_raw_item_header()` and intentionally kept out of `p2it` itself (it is also an ABI helper for pointer-shaped values whose header may not exist yet — Part 7 §7.3).

### S0.5 Representation tests (Part 7 §7.4) — new `test/test_item_repr_gtest.cpp`

For each of Array, ArrayNum, Map, Object, Element, Range, Function, Type, Path:

- [x] construct via the normal allocator; capture native pointer and Item bits; assert **exact bit identity**;
- [x] assert high byte zero and `DBL_MASK` clear;
- [x] assert `get_type_id()` reads the header type; assert the matching `it2*` accessor returns the original pointer; read one representative field through it.

Plus:

- [x] the discriminator-ordering test: a synthetic in-band double word whose low 56 bits are deliberately **not** a valid address must not read a header or enter the raw-pointer branch; a real container pointer must reach the header branch with the word unmodified. The `LMD_TYPE_FLOAT` classification assertion waits for S1's flag-controlled classifier so S0 remains behavior-neutral.
- [x] MIR golden-inspection test: transpile a known container member load; assert the emitted MIR contains no `AND`/`XOR`/pointer reconstruction between the Item register and the member call (Part 7 §7.4 — this is a performance invariant as well as correctness). Wired through `test_item_repr_gtest` plus `test/lambda/item_repr_container_member_load.{ls,txt}`.

2026-07-10 note: `test_item_repr_gtest` covers header-shaped raw Item families on stack and through `gc_heap_alloc/calloc`, an S0-era non-pointer discriminator word without requiring S1 float classification, and a MIR inspection fixture that rejects pointer reconstruction around member lookup.

### S0.6 Audits (deliverable: a checked-off audit table appended to this file)

- [x] **Raw `>> 56` sweep** — ~24 sites / 11 files: classify each as (a) downstream of `get_type_id` (fine), (b) tag-literal compare provably never seeing a float Item (fine), (c) needs the `DBL_MASK` test added at S1. Include `& 0x00FF…` mask-only sites and any private tag arithmetic in the Jube runtimes (`lambda/py/`, `lambda/bash/`, …).
- [x] **Raw-Item equality audit** — every `MIR_EQ`/`MIR_BEQ` emission on Item words (`js_mir_expression_lowering.cpp:1519`, `:1562`, `:1575`, `:5560`, `:6590`, …, plus Lambda-side `transpile-mir.cpp` equivalents): float-free proof, or marked for `MIR_DEQ`/runtime-helper reroute at S2. The three semantic traps: equal in-band doubles become raw-equal; same-payload NaNs raw-equal (must be `false`); `ITEM_FLOAT_P0` vs `_N0` raw-unequal (must be `true`).
- [x] **Open-coded payload dereference sweep** — every read of a float payload not going through `it2d`: known: `Item::get_double()` (`lambda.hpp:173`, `*(double*)this->double_ptr`) and the C `it2d` (`lambda-data.cpp:322`); find the rest (`grep '\*(double\*)'` filtered to Item-payload use, not shaped-field slots). Each must route through the S1 two-arm decode or be proven boxed-only.
- [x] **GC classifier audit** — `item_to_ptr` (`gc_heap.c:770`), `gc_scan_stack` (`:1569`), map-field tracing (`:1017`–`:1058`), `gc_fixup_embedded_pointers` (`:1202`): confirm none can misread a bit-62/61-set word as a pointer once such Items exist, and that GC-internal header tags (`LMD_TYPE_MAP_` etc. past `LMD_TYPE_COUNT`) are never materialized into Item high bytes.
- [x] **Hashing/SameValueZero audit** — every value-keyed container (JS `Map`/`Set`, any Item-keyed hashmap): confirm float keys are hashed via unboxed value with NaN-canonicalization and −0→+0 normalization (required today for pointer-distinct boxed floats; must survive the rewrite — design §3.4/§3.5).

#### S0.6 Audit Table — 2026-07-10

| Sweep | Evidence command | Classification | S1/S2 follow-up |
|---|---|---|---|
| Raw high-byte shifts | `rg '>>\s*56\|0x00FF\|0xFF00000000000000' lambda lib test` | `lambda/lambda.h`, `lambda/lambda.hpp`, `lib/gc/gc_heap.c`, and `test_item_repr_gtest` are representation-layer code. `lambda/lambda-mem.cpp` tag reads classify nursery teardown/boxed scalar slots, not in-band float Items. `lambda/input/css` stylesheet markers, `lambda/format/format-css.cpp`, and `lambda/main.cpp` document roots are private tagged formats and do not accept arbitrary float Items. Jube runtimes (`lambda/py/`, `lambda/rb/`, `lambda/bash/`) keep private `MASK56` constants for their own boxed payload bridges. | S1 classifier must add `ITEM_DBL_MASK` before any header/payload deref in universal Item classifiers. Private marker formats remain boxed/format-local. |
| `MIR_EQ` / `MIR_BEQ` Item-word emissions | `rg 'MIR_(EQ|BEQ)' lambda/transpile-mir.cpp lambda/js lambda/py lambda/rb lambda/bash` | Type/tag tests, error/null/sentinel branches, boolean/native comparisons, and array/type checks are float-free. Lambda semantic equality fast paths (`transpile-mir.cpp` equality lowering) and JS strict/loose equality fast paths are value-sensitive. Python/Ruby/Bash MIR equality lowering is currently language-local and boxed-value based. | S2 reroutes value-sensitive Lambda/JS Item equality to `MIR_DEQ` after unbox or runtime equality helpers. Keep type/tag/sentinel branches raw. |
| Open-coded float payload reads | `rg '\*\s*\(\s*double\s*\*|\(double\*\).*0x00FFFFFFFFFFFFFF' lambda lib test` | Field-slot reads/writes and typed-array data-buffer casts are native storage, not Item payload decode. Item payload decode stragglers include `Item::get_double()`, `format.cpp`, JS DOM/PDF postprocess direct payload casts, and `js_typed_array.cpp` numeric conversion. Float producers via `heap_alloc(... LMD_TYPE_FLOAT)` remain canonical boxed producers until S1. | S1 routes every Item decode through `it2d` / `get_double` two-arm logic, then removes or marks boxed-only direct payload reads. |
| GC classifier and fixup | `rg 'item_to_ptr|gc_scan_stack|gc_fixup_embedded_pointers|gc_mark_item' lib/gc/gc_heap.c` | `item_to_ptr` now rejects `ITEM_DBL_MASK` and unknown high tags before pointer lookup. Stack/root/map-field tracing all call `gc_mark_item`, which funnels through `item_to_ptr`. `gc_fixup_embedded_pointers` only rewrites boxed pointer payloads for explicit high-byte tags. | S1 inline doubles remain skipped by `item_to_ptr`; fixup keeps boxed fallback support and must never materialize GC-internal tags as Item high bytes. |
| Hashing / SameValueZero | `rg 'SameValueZero|hash.*Item|Item.*hash|js_strict_equal' lambda/js lambda lib` | JS Map/Set hash code in `js_runtime.cpp` already normalizes numeric keys by unboxed double, canonicalizes NaN, and maps `-0` to `+0`. `vmap.cpp` numeric keys route through `lambda_numeric_to_canonical_string` + `fn_eq`, so pointer-distinct boxed floats already collapse. Non-numeric VMap keys remain identity/sentinel raw comparisons. | S1 must preserve the numeric normalization after float encoding changes; S2 reroutes any JIT raw equality that can see floats. |

### S0.7 Lint rules — `utils/lint/rules`

- [x] New rule (style of `no-int-cast-radiant`): flag any new 64-bit integer constant whose high-byte bit 6 or 5 is set, outside the float representation module. Implemented as `no-item-high-tag-literal`.
- [x] Flag any new `>> 56` comparison against an integer literal not expressed via the `EnumTypeId`. Implemented as `no-item-tag-literal-shift`.
- [x] Run the three S0 lint ratchets in `make lint` (`no-item-high-tag-literal`, `no-item-tag-literal-shift`, `no-raw-item-equality`); all are clean with intentional suppressions recorded at proof sites.
- [ ] Whole-tree `make lint` clean at S0 exit. 2026-07-10 run is blocked by unrelated existing lint debt outside the S0 Item representation surface; see verification table below.

### S0.8 Semantic equality — one entry point (Item Boxing Evolution Rule 9)

Raw Item bit-comparison is representation-layer code; this bug family has shipped at least three times (ArrayNum `==` representation-sensitivity, NaN identity, packed ±0). Groundwork here, enforcement grows with S1/S2:

- [x] Inventory every raw Item comparison in C/C++ runtime code (`.item ==`, `raw ==` on Item words) — the C-side sibling of the S0.6 `MIR_EQ` audit. Classify: (a) identity-semantics types only (objects/functions — fine), (b) sentinel compares (fine once sentinels are pinned), (c) value types where bit-equality is representation-dependent → route through the canonical equality helpers (`js_strict_equal`-class / Lambda `item_equal`).
- [x] Lint rule: flag new raw Item equality comparisons outside the representation layer and the audited allowlist (ratchet — the allowlist only shrinks). Implemented as `no-raw-item-equality` over representation/value-map files; broader semantic sites are tracked below for S2.
- [x] Each surviving raw compare carries a one-line comment naming its proof (per CLAUDE.md rule 12 — state the invariant being relied on).

#### S0.8 Raw Equality Inventory — 2026-07-10

| Bucket | Sites | Classification | Action |
|---|---|---|---|
| Sentinel/null checks | `ITEM_NULL`, `ITEM_ERROR`, `ITEM_JS_UNDEFINED`, `ItemNull.item`, and zero-Item checks across parsers, markup, CLI, and JS runtime | Sentinel identity; safe once sentinel high bytes are pinned by S0.2/S0.3. | Keep raw comparisons. Lint covers new representation-layer uses where proof comments are required. |
| Render tree identity | `lambda/render_map.cpp` doc-root, parent search, and path search comparisons | Exact object identity is required; semantic equality would be wrong. | Marked with `RAW_ITEM_EQ_OK` proof comments and covered by `no-raw-item-equality`. |
| VMap key identity fallback | `lambda/vmap.cpp` non-numeric default compare | Numeric keys use `fn_eq`; strings/symbols compare content; remaining containers/sentinels use identity. | Marked with `RAW_ITEM_EQ_OK`; S1 must verify `get_type_id` keeps numeric inline floats in the numeric branch. |
| JS semantic equality | `js_eq_raw`, `js_strict_equal`, JIT equality fast paths | Value-sensitive: NaN, `+0/-0`, and inline float raw bits are semantic traps. Existing C++ runtime already special-cases boxed floats. | S2 reroute/fixup candidate; not covered by the raw-identity lint allowlist. |
| Lambda semantic equality | `lambda/lambda-eval.cpp::fn_eq`, `lambda/transpile-mir.cpp` equality lowering | Pointer identity fast path is valid for containers but not for future inline float raw equality unless classifier excludes floats first. | S2 reroute/fixup candidate for generic Item equality. |
| Typed-array hole sentinel | `lib/lambda_typed.hpp::HoleSentinel::is` | Unique sentinel identity. | Marked with `RAW_ITEM_EQ_OK` and covered by `no-raw-item-equality`. |

**S0 verification — 2026-07-10**

| Gate | Result | Notes |
|---|---|---|
| `make test_item_repr_gtest config=debug_native -j8` | PASS | Builds the new S0 representation gtest target. |
| `./test/test_item_repr_gtest.exe --gtest_brief=1` | PASS | 5/5 tests; includes raw pointer identity, allocator identity, discriminator ordering, and MIR member-load inspection. |
| `./test/test_gc_heap_gtest.exe --gtest_brief=1` | PASS | 42/42 tests. |
| `./test/test_lambda_typed.exe --gtest_brief=1` | PASS | 15/15 tests. |
| `./test/test_js_gtest.exe --gtest_brief=1` | PASS | 308/308 tests. |
| `./test/test_lambda_gtest.exe --gtest_brief=1` | PASS | 456/456 tests, including `item_repr_container_member_load.ls`. |
| S0 lint ratchets | PASS | `make lint ARGS='--rule ^no-item-high-tag-literal$'`, `^no-item-tag-literal-shift$`, `^no-raw-item-equality$`, and `make lint ARGS='--list'` are clean. |
| `make test-lambda-baseline` | PASS | 3292/3292 total: input parsers 2105/2105, Lambda runtime 1187/1187, including `test_item_repr_gtest` 5/5. Required escalation only for the repo's `test/yaml` submodule metadata initialization under `.git`. |
| `make lint` | FAIL unrelated | New S0 rules are clean, but repo-wide lint still fails on unrelated findings: Radiant view casts, raw allocation sites in JS/build/vector/assert code, and state-store scroll mirror checks. |
| `make test-radiant-baseline` | FAIL unrelated | Layout required baseline, layout page suite, page load, and WPT CSS syntax pass. Failures are 3 UI automation tests (`test_form_state_drag`, `test_form_state_li_drag`, `test_form_textarea_scrolled_hit_test`) and 1 Radiant view command test (`PromotesCachedPngDecodeFromThumbnailToFullSize`). |
| `make node-baseline` | FAIL unrelated / unstable | Non-escalated run only failed socket preflight setup; escalated run completed with 3526/3528 passing, 1 assertion failure (`test-global-console-exists.js`, `0 !== 1`) and 1 timeout (`test-util-getcallsites-preparestacktrace.js`, 60s). No crashers. |

No S0-specific regression is indicated by the failing broad gates: they are outside the Item representation/lint surface touched by this phase, but they remain open repository gates before a final all-green merge policy can be claimed.

---

## S1 — Runtime core (behind `LAMBDA_SELF_TAG_FLOAT`)

### S1.0 Retire `LMD_TYPE_FLOAT64` (flag-independent prerequisite)

Design §3.7: two runtime TypeIds for one binary64 domain break canonical encoding. Coordinate with `Lambda_Semantics_Number_Model.md` Part 2 W-items (`f64` is a parse-time alias per its §3.2).

- [x] `f642it` producers → produce `LMD_TYPE_FLOAT` Items via `lambda_float_ptr_to_item`; keep `f64` as an alias resolving at parse/annotation level.
- [x] Retire live `LMD_TYPE_FLOAT64` runtime use: `is_numeric_type_id` excludes it, type metadata aliases it to `float`, `type()` / formatter-visible names stay `float`, and the MIR legacy constant path emits the canonical `LMD_TYPE_FLOAT` tag. The enum value remains reserved only for compatibility with stale artifacts.
- [x] Gate: S1 Lambda baselines green before proceeding — flag-on `make test-lambda-baseline CFLAGS=-DLAMBDA_SELF_TAG_FLOAT CXXFLAGS=-DLAMBDA_SELF_TAG_FLOAT` and flag-off `make test-lambda-baseline` both pass.

### S1.1 Classifier — `Item::type_id()` / `get_type_id`

`lambda.hpp:159` (and the C-side equivalent) becomes, under the flag:

```c
if (item_bits & ITEM_DBL_MASK) return LMD_TYPE_FLOAT;   // MUST be first — before any deref
uint8_t tag = item_bits >> 56;
if (tag != LMD_TYPE_RAW_POINTER) return (TypeId)tag;
if (item_bits != 0) return *(TypeId*)(uintptr_t)item_bits;
return LMD_TYPE_NULL;                                    // preserve canonical zero-Item behavior
```

- [x] The raw-pointer branch uses the **original word unchanged** (Part 7 §7.2 — no masking of container pointers, ever).
- [x] Debug assert in the raw branch: high byte is zero before the header dereference.

### S1.2 Encode / decode primitives

- [x] `flt2it(double)` (new, `lambda.h`/`lambda.hpp`): mask-test the raw bits; in-band → store raw; `d == 0.0` → `ITEM_FLOAT_P0 | signbit(d)`; else `box_float_cold(d)` (existing boxed path).
- [x] `it2d` (`lambda-data.cpp`) and `Item::get_double()` (`lambda.hpp`): hot arm = mask test + bitcast; cold arm decodes packed zeros and boxed fallback. Boxed payload reads now route through these helpers or typed native storage, not ad hoc Item payload dereferences.
- [x] Assertions (Part 7 §7.3): encode produces only {raw in-band bits, `ITEM_FLOAT_P0/_N0`, canonical boxed fallback} — never a raw out-of-band bit pattern; decode accepts only those same three forms.

### S1.3 Producers canonicalize

- [x] `push_d` / `push_d_safe` become thin wrappers over `flt2it` (nursery allocation only inside the cold arm).
- [x] `js_make_number` → `flt2it`; its minus-zero guard maps onto the packed constants.
- [x] Input parsers / MarkBuilder float construction (`mark_builder.cpp` and `lambda/input/*`) → the canonical encoder; heap Float objects remain only for the boxed fallback.
- [x] Formatters/printers: verified float reads go through `it2d`/`get_double` or typed native storage; S1 closes the PDF/JS/DOM/typed-array payload-deref stragglers found by the S0.6 sweep.
- [x] `gc_fixup_embedded_pointers` and the array extra-area write path: unchanged for boxed floats; inline-double Items in `items[]` are skipped because they never present pointer-tag high bytes.

### S1.4 GC-side classifier

- [x] `item_to_ptr` (`gc_heap.c`): early-outs on `ITEM_DBL_MASK`; an unknown high tag never falls through to the "treat as raw pointer" arm without excluding double space (Part 7 §7.3).
- [x] Conservative scan: inline doubles are skipped by the early-out; packed zeros have payloads 0/1 and never become valid addresses.
- [x] Keep semantic `MIR_T_P` rooting decisions unchanged: a raw pointer physically in an `MIR_T_I64` register is still a pointer for rooting (Part 7 §7.3); float locals remain unrooted (`should_gc_root_var` already excludes FLOAT).

### S1.5 Hashing / equality normalization

- [x] SameValueZero hash sites: preserve existing unboxed-double hashing, NaN canonicalization, and −0 → +0 normalization after inline-float encoding.
- [x] `js_strict_equal` is value-based — verified unchanged behavior under the flag through `test_js_gtest` and Lambda baseline coverage, including NaN/zero-sensitive JS equality paths.

**S1 implementation gates — 2026-07-10:** PASS flag-on `make test-lambda-baseline CFLAGS=-DLAMBDA_SELF_TAG_FLOAT CXXFLAGS=-DLAMBDA_SELF_TAG_FLOAT` 3296/3296; PASS flag-off `make test-lambda-baseline` 3292/3292; PASS focused flag-on/off representation, Lambda, and JS gtests. Full test262, Radiant baseline, node-baseline, and ASan remain S2/S3 release hardening gates, not blockers for S1 runtime-core completion.

**S1 verification — 2026-07-10**

| Gate | Result | Notes |
|---|---|---|
| Forced flag-on build | PASS | `make -C build/premake config=debug_native lambda test_lambda_std_gtest test_lambda_gtest test_js_gtest test_item_repr_gtest -B -j8 CFLAGS=-DLAMBDA_SELF_TAG_FLOAT CXXFLAGS=-DLAMBDA_SELF_TAG_FLOAT`. |
| Flag-on focused gtests | PASS | `test_item_repr_gtest` 9/9, `test_lambda_std_gtest` 106/106, `test_lambda_gtest` 456/456, `test_js_gtest` 308/308. |
| Flag-on `make test-lambda-baseline CFLAGS=-DLAMBDA_SELF_TAG_FLOAT CXXFLAGS=-DLAMBDA_SELF_TAG_FLOAT` | PASS | 3296/3296 total: input parsers 2105/2105, Lambda runtime 1191/1191, including `test_item_repr_gtest` 9/9. Required escalation only for the repo's `test/yaml` submodule metadata initialization under `.git`. |
| Forced flag-off rebuild | PASS | Release and debug targets rebuilt without `LAMBDA_SELF_TAG_FLOAT` after the flag-on run to avoid stale flagged objects. |
| Flag-off focused gtests | PASS | `test_item_repr_gtest` 5/5, `test_lambda_std_gtest` 106/106, `test_lambda_gtest` 456/456, `test_js_gtest` 308/308. |
| Flag-off `make test-lambda-baseline` | PASS | 3292/3292 total: input parsers 2105/2105, Lambda runtime 1187/1187, including `test_item_repr_gtest` 5/5. Required escalation only for the repo's `test/yaml` submodule metadata initialization under `.git`. |

---

## S2 — JIT and interpreter lowerings (behind the flag)

### S2.1 Lambda MIR transpiler — `lambda/transpile-mir.cpp`

- [x] Replace emitted `push_d` calls with inline: mask-test the raw bits, return in-band bits directly, encode packed signed zero, and call `push_d` only for the cold boxed fallback.
- [x] Unbox sites: inline `ITEM_DBL_MASK` dispatch; in-band values call `lambda_mir_bits_double`, while packed zeros and boxed fallback route through `it2d`. S2 explicitly avoids `MIR_ALLOCA` scratch-slot bitcasts because they allocate dynamically inside hot loops.
- [x] Float constants: emit canonical Item bits directly under `LAMBDA_SELF_TAG_FLOAT`, including `f64` literals through the same `float` runtime encoding.

### S2.2 LambdaJS lowering — `lambda/js/`

- [x] `jm_box_float` (`js_mir_calls_boxing_types.cpp`): same inline mask/zero/cold-box encode, with `lambda_mir_double_bits` for the raw bit reinterpretation.
- [x] `jm_box_int_reg` overflow arm: `I2D` now flows through `jm_box_float`, so overflow promotion shares the self-tagged float encoding instead of bypassing through the profiled `push_d` helper.
- [x] `jm_emit_unbox_float` and every `it2d`-emitting site: inline in-band decode dispatch; packed zeros and boxed fallback still route through `it2d`.
- [x] Apply the S0.6 `MIR_EQ` reroutes: typed numeric equality already uses `MIR_DEQ` / `MIR_DNE`; unknown boxed JS comparisons call `js_eq_raw` / `js_loose_eq_raw`, whose identical-float fast path rejects NaN and whose non-identical path falls through to `js_strict_equal` / `js_equal` for `+0/-0` and boxed/inline mixtures. Lambda generic equality falls through to `fn_eq` / `fn_ne`; type/tag/sentinel/string identity comparisons remain raw by proof.

### S2.3 Cross-cutting

- [x] Apply `DBL_MASK` **only** to universal Item values — never to statically native `MIR_T_D`/`MIR_T_P` registers (Part 7 §7.3); native numeric fast paths stay native and only box/unbox at Item boundaries.
- [x] Known-type container unboxing stays identity: the S0.5 MIR golden-inspection test still covers member lookup, and the release-run guard now skips stale dump inspection when the current `lambda.exe` does not emit `temp/mir_dump.txt`.
- [x] MIR **interpreter** mode: Lambda MIR direct now links with `MIR_set_interp_interface` when `g_mir_interp_mode` is set and uses `find_func` instead of JIT-generating `main`; bitcasts use runtime helpers so interpreter mode does not depend on `MIR_ALLOCA`.
- [x] Legacy C2MIR path (`--c2mir`): the C transpiler consumes the same `flt2it`/`it2d` helpers. Focused C2MIR smoke compiles and runs; `box_unbox`, `box_unbox_negative`, and `item_repr_container_member_load` pass, while `box_unbox_advanced` and `float_conversion` retain old output-format mismatches (`0.2` vs `0.19999999999999998`, scientific notation), not representation failures.

**S2 gates — 2026-07-10:** PASS S1 gates repeated in both flag states; PASS focused JIT/interp smokes; PASS one-run release LambdaJS A/B smoke for S2's named movers/canaries. The full P0 three-run benchmark snapshot remains unchecked above and is still required before S3/default flip sign-off.

**S2 verification — 2026-07-10**

| Gate | Result | Notes |
|---|---|---|
| Forced flag-on release/debug rebuild | PASS | `make -C build/premake config=release_native lambda -B -j8 CFLAGS=-DLAMBDA_SELF_TAG_FLOAT CXXFLAGS=-DLAMBDA_SELF_TAG_FLOAT` and forced debug rebuild of the Lambda baseline executables. |
| Flag-on `make test-lambda-baseline CFLAGS=-DLAMBDA_SELF_TAG_FLOAT CXXFLAGS=-DLAMBDA_SELF_TAG_FLOAT` | PASS | 3296/3296 total: input parsers 2105/2105, Lambda runtime 1191/1191, including `test_item_repr_gtest` 9/9. Required escalation only for the repo's `test/yaml` submodule metadata initialization under `.git`. |
| Forced flag-off release/debug rebuild | PASS | Rebuilt without `LAMBDA_SELF_TAG_FLOAT` after flag-on runs to avoid stale flagged objects. |
| Flag-off `make test-lambda-baseline` | PASS | 3292/3292 total: input parsers 2105/2105, Lambda runtime 1187/1187, including `test_item_repr_gtest` 5/5. Required the same submodule metadata escalation. |
| Lambda MIR interpreter smoke | PASS | `./lambda.exe --mir-interp test/lambda/float_conversion.ls` passes in both flag states; the user-facing log string remains generic but the link path uses `MIR_set_interp_interface`. |
| JS MIR interpreter focused smoke | PASS | `JS_MIR_INTERP=1 ./test/test_js_gtest.exe --gtest_brief=1 --gtest_filter='JavaScriptTests/JsFileTest.Run/tco:JavaScriptTests/JsFileTest.Run/lib_ajv'` passes 2/2. |
| C2MIR focused smoke | PARTIAL / unchanged | 3/5 focused cases pass; the two failures are longstanding output-format drift, not self-tagged float representation crashes. |
| S2 A/B benchmark smoke | PASS / informative | One release run, LambdaJS only: `awfy/mandelbrot` 15001ms → 354ms (42.3x), `awfy/nbody` 9918ms → 578ms (17.2x), `kostya/matmul` 25102ms → 792ms (31.7x), `jetstream/nbody` 7372ms → 247ms (29.9x), `jetstream/richards` 7396ms → 647ms (11.4x), `jetstream/deltablue` 23189ms → 1980ms (11.7x). No-flag `jetstream/navier_stokes` and `jetstream/splay` timed out; flag-on completed at 36273ms and 1540ms respectively. Artifacts: `test/benchmark/s2_self_tag_float_ab_flag_off_2026-07-10.json`, `test/benchmark/s2_self_tag_float_ab_flag_on_2026-07-10.json`. |

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
