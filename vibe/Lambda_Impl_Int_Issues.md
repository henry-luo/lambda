# Lambda `int` v5 — Outstanding Implementation Issues

- **Status:** v5 runtime migration **VERIFIED**. `make test-lambda-baseline` is
  **1,629 / 1,629** and `make test262-baseline` is **40,261 / 40,261**, both with zero
  failures and zero Test262 regressions. The encoding (§5.4 packing) and native lane (§5.3
  i64) are landed; the remaining entries distinguish completed work from deliberately
  non-blocking hardening follow-ups.
- **Date:** 2026-08-04
- **Design authority:** [`Lambda_Semantics_Int_Type.md`](Lambda_Semantics_Int_Type.md) — §5 is
  the v5 spec; §-refs below point into it.
- **Convention:** `file:line` drifts; confirm against the symbol name.

---

## 0. How to read this

Issues are grouped by **defects**, **expected churn**, and non-blocking hardening follow-ups.
Each completed defect records its root cause and verification rather than only a test name.

**Baseline command:** `make test-lambda-baseline`. Individual scripts: `./lambda.exe <f>.ls`,
or `./lambda.exe run <f>.ls` for anything under `test/lambda/proc/` or with a `main()` —
**a procedural script run without `run` silently prints `null`**, which has twice been
mistaken for a failure in this migration.

---

## 1. DEFECTS — real bugs, ranked by blast radius

### I-1. Dynamic-call ABI and method signature — RESOLVED

`TypeMethod` now retains the source function type and the object-method binder transfers it to
the bound closure. Generic dynamic dispatch accepts the boxed procedural ABI as a callable value,
while the public arity wrappers retain their historic non-function diagnostics. This fixes object
methods, indirect procedure calls, and `boundary_error_chain_depth` without weakening arity
checks. The focused diagnostic and procedure tests pass, and the complete baseline is green.

### I-2. `proc_*` cluster — RESOLVED / re-blessed where v5 changes the value

The dynamic-call repairs resolve closure mutation, object counter, call-site inference, and typed
array guard execution. `proc_inferred_dual_entry` now deliberately observes saturation outside
the int53 band (`true false true false`); its golden records that v5 result. `int_large_carriers`
now produces `inf`, never the erroneous wrapped `0`.

### I-3. Vectorized int math lane conversion — RESOLVED

`fn_neg()` now converts `ELEM_INT` lanes with `lambda_int_lane_to_double()` before writing the
float result. This prevents raw i64 bits such as `-1` from being reinterpreted as subnormal
doubles; `typed_array_vectorized_math` again yields `[-1, -2, -3]`.

---

## 2. EXPECTED CHURN — v5 changes behavior on purpose; verify then re-bless

> ⚠ **Verify every diff individually. Do not bulk-regenerate.** During the v4 merge this
> discipline caught three real regressions hiding among intended diffs (`is_nan`,
> `decimal.inf == inf`, and the nan sort order).

### I-4. Sparse-tail saturation goldens (§5.5) — RESOLVED

The v5 int domain is exactly ±(2⁵³−1). The affected `int_total_c16`, `int_promotion`,
`number_model_realign`, `numeric_fastpath_edges`, `bitwise_lane_preservation`, and
`proc_int_large_carriers` goldens now assert `inf`/`-inf` at the boundary. MIR multiplication
also checks `MIR_MULO` overflow before the band check, preventing a 2⁷⁰ calculation from
wrapping to a finite lane value such as `0`.

### I-5. `ItemRepresentation` v5 contracts — RESOLVED

The representation tests retain finite int53 round trips and add explicit out-of-band saturation
coverage: `i2it(INT53_MAX + 1)` and `i2it(INT53_MIN - 1)` box to shared IEEE infinities and
their native lanes are `INT_LANE_INF`/`INT_LANE_NEG_INF`. `lambda_int_item_to_i64()` now returns
lane sentinels for shared poison rather than casting IEEE specials directly.

### I-6. Numeric admission at the int53 boundary — RESOLVED

`type_contract.cpp` now admits only finite integral int53 values into `int`; sparse `int64`,
`integer`, and float inputs above the band reject. Shared IEEE poison remains admitted as the
defined closure of the int domain, including `nan`.

### I-7. MIR emission fixtures and ratchet budgets — RESOLVED

The scalar-home, tail-forward, call-site inference, closure, and typed-array guard fixtures and
their narrowly scoped budgets now match the i64 lane: overflow/band branches appear where needed,
and retired rotation/FP traffic is absent. All 12 Lambda MIR-emission fixtures and 15 ratchet
probes pass in the final baseline.

---

## 3. CLOSEOUT AND NON-BLOCKING FOLLOW-UPS

### I-8. LJS number policy sweep (§5.6) — RESOLVED

`jm_box_int_reg`, `jm_box_int_double`, and `MCONST_INT` now use float Item boxing. Native i64
values remain only unboxed working registers. `Buffer.compare` returns its public -1/0/1 result
through `js_make_number`, and `test/node/buffer_basic` now pins `typeof Buffer.compare(...)` as
`number`. The MIR fixture `js_number_item_policy` forbids `int2it` when a native index crosses
the JS Item boundary.

### I-9. Python / Ruby transpiler native lanes — excluded from this closeout

The user explicitly excluded Python-suite failures from this pass. Python's targeted
arbitrary-precision overflow test still passes; its unrelated import and stdlib failures are not
used as acceptance evidence here. The Ruby suite currently fails before execution because its
scripts are routed to the Lambda parser, so it cannot diagnose a Ruby numeric lane. Neither guest
suite changes the verified Lambda/JS v5 result.

### I-10. Guest-boundary contract (§5.6) — RESOLVED

`test/js/regression_v5_guest_number_poison.js` passes `Infinity`, `-Infinity`, and `NaN` through
a guest call and asserts each remains a JavaScript `number` with its IEEE identity. Together with
the MIR policy fixture, it prevents lane poison and ordinary native counters from escaping as
Lambda int Items.

### I-11. `IntLane` typedef (§5.3) — non-blocking hardening

The design calls for `typedef struct { int64_t v; } IntLane;` so lane-vs-machine mixing is a
compile error at hand-written C sites. Currently only the *emitter* is protected, by
`BoxedReg`/`LaneReg` in `transpile-mir.cpp`. The C runtime still passes bare `int64_t` for both
meanings — which is exactly the confusion that produced I-3 and the `array_int_set` bug.

### I-12. Benchmark pricing (§5.8 step 2) — v5 snapshot recorded

On the final release binary, five interleaved runs of `sum`, `collatz`, and `bounce` all passed.
Median in-script times were **28.641 ms**, **7,874.32 ms**, and **0.640 ms**, respectively.
This is a v5 snapshot, not an invented before/after claim: `Result_Double_vs_Int.md` remains the
separate i64-vs-double ceiling.

---

## 4. OPEN QUESTIONS (design, not defects)

- **I-14.** `lib_codemirror.js` overflows the 256 MB js-cli execution stack under ASan
  (388 KB minified bundle). My earlier "v5 regression" call on it was based on an **invalid
  A/B** (v5 debug+ASan vs v4 *release*). Re-test against a v4 **debug** build before treating
  it as v5-caused; ASan frames are far larger.
- **I-15. RESOLVED 2026-08-04.** Release `./lambda.exe run test/benchmark/awfy/cd.ls --no-log`
  prints `collisions=4305`, `CD: PASS`, and returns `4305` (timing 874.418 ms in the confirming
  run). The earlier silent observation was stale, not a remaining lane defect. (`awfy_json` is a
  *pre-existing* `any \ error` parameter failure, verified independent of v5.)
- **I-16.** §5.2's full poison-algebra table is implemented for `+ − *` but has never been
  written out as a test matrix. `div`/`%`/comparison arms are implemented ad hoc.

---

## 5. TRAPS — hard-won, do not rediscover

1. **MIR's `S` suffix means 32-BIT, not "signed".** `MIR_GES`/`MIR_LES` are 32-bit compares;
   band checks must use `MIR_GE`/`MIR_LE`. Symptom when wrong: `1 + 2` returns `inf`.
2. **A boxed Item and an int lane value are BOTH `MIR_T_I64`.** The register class can no longer
   discriminate them (v4's `MIR_T_D` lane did, by accident). The discriminator is the node's
   static type plus `transpile_expr`'s contract. `MIR_T_P` cannot help: `MIR_new_func_reg`
   rejects it, and `mir_reg_type_for_alloc` folds it to `MIR_T_I64` anyway.
3. **`make build` does NOT rebuild the external Jube module dylibs.** `modules/*/…dylib`
   (node-core, node-fs, node-net, node-zlib, lang-python) inline the Item encoding. A stale one
   cost **200 JS test failures** and presented as a SIGSEGV in `Item::type_id()`. Any change to
   `lambda.h`'s encoding must run:
   `make build-node-core build-node-fs build-node-net build-node-zlib build-lang-python`.
4. **A statically int-typed expression can still yield a non-INT Item tag.** `ndim()` declares
   `int` but returns an int64 Item (a tagged *pointer*). The int unbox must **test the tag**,
   not assume a packed int — v4 got this free via the `it2d` runtime call, which dispatched.
5. **Shaped int fields share float's DOUBLE slot** and must stay that way for now. Splitting
   them to an i64 slot regressed the tree by 93 tests: object construction, `map_put`, the
   validator and TypedItem all write those slots and were not converted. Only the JIT's lane
   conversion changes.
6. **`core/` must not link `runtime/`.** The lane↔double converters are header-only in
   `lambda.h` (placed *after* `lambda_int_unbox_double`); the `_c` wrappers in runtime exist
   purely so the JIT registry has callable symbols.
7. **`proc_*` scripts need `./lambda.exe run`**; without it they silently print `null`.
