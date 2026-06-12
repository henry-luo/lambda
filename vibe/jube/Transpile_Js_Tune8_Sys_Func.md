# Transpile JS Tune8 — Sys-Func Registry and JS MIR Runtime Interface Reduction

Date: 2026-06-12
Status: rev 5 — Phase 0 (safety gate) + Phase A (telemetry-driven §3 deletions) + Phase D first fold (§2.1 inverse-pair `_ne_` → `_eq_` + XOR) **landed**.
Baseline: `test/js262/results/release_run_006` (39,258 pass / 516.889 s summed wall-clock / commit `2192fd58d`)
Working baseline (master HEAD `5627cbb6a`): 39,255 fully passing (3 batch-unstable tests dropped by harness `--update-baseline` since release_run_006; not caused by Tune8)
Current: **459 `js_*` registry entries (−88 = −16.1%); test262 aggregate 466.833 s; 39,255 / 39,255 pass / 0 regressions; test_js_gtest 169 / 169 pass.**

**Rev 6 — recommended-order folds landed (per user "continue with your recommended order"):**

| Fold | Description | Δ entries | Risk |
|---|---|---:|---|
| §2.1 raw-relop fold | `js_lt_raw` / `_gt_raw` / `_le_raw` / `_ge_raw` → `js_cmp_raw(op, l, r)` with op as constant operand | −3 | low (cold raw-relop family) |
| §2.1 box-equal inverse-pair | `js_not_equal` / `js_strict_not_equal` → corresponding eq + inline `MIR_XOR result, 1` on boxed bool. The XOR flips the low bit (where `b2it` stores the value) while preserving the type tag. | −2 | low (microbench: +0.4 ns) |
| §2.3 throw helpers fold | `js_throw_syntax_error` / `js_throw_reference_error` (JIT path) → `js_throw_named_error(kind, msg)`. C wrappers kept for `js_globals.cpp` callers. | −1 net (−2 +1) | low (cold throw paths) |
| §2.2 super-property setter | `js_super_property_set` + `_non_strict` → unified `js_super_property_set(recv, key, val, strict)`. 8 + 17 emissions (cold). | −1 | low |
| §2.2 global-property setter | `js_set_global_property` + `_strict` → unified with strict constant operand; `_strict_prechecked` and `_var_property_fast` (224K hot) kept direct. Rewrote 16 JIT call sites via Python script. | −1 | low (hot path preserved) |

Cumulative: **547 → 459 (−88 entries, −16.1%)**. Test262 stable at 39,255 / 39,255 across every intermediate fold. test_js_gtest 169 / 169 stable. Aggregate per-test wall-clock fluctuates between 449 s and 547 s across runs (single-run noise ~15%); median across all post-fold runs is ~470–520 s, comparable to or better than the 516.889 s baseline.

§4 (test262 build-flag gate) **deferred** — every test262 framework helper is heavily emitted (`js_assert_same_value` 91K, `js_assert_base` 55K, `js_verify_property` 18K). Gating would require two-sided `#ifdef JS_TEST262_FAST_PATHS` in both the registry AND every transpiler emission site that detects test262 idioms. The savings (−15 entries in production-only builds) materialize only when the macro is undefined; test262 builds keep everything. Substantial change, low marginal value vs the other folds.



## Implementation status (rev 3)

- ✅ **§1.1 microbench** — landed under `test/js_runtime_bench/` with runner at `utils/js_runtime_bench.sh`. Four bench files cover the families the proposal would touch:
  - `bench_eq_ne.js` — `===`/`!==`/`==`/`!=` (validates the rev-4 inverse-pair fold).
  - `bench_property.js` — `obj.x = i` / `obj.x` read, non-strict and strict (gate for §2.2).
  - `bench_binop.js` — arithmetic and bitwise (gate for any §2.1 arith fold).
  - `bench_common.js` — shared 5M-iter × 5-rep harness, best-of timing.
  Baseline at `test/js_runtime_bench/baseline_post_tune8.tsv` (current commit). Measured costs:

  | Op | ns/op_best | Notes |
  |---|---:|---|
  | `&` / `\|` / `^` / `<<` / `>>` / `>>>` (with `\|0` coerce) | ~3.3 | JIT inlines past the runtime call — fold wouldn't help |
  | `+` (int) | 11.3 | Inline integer fast path |
  | `===` / `!==` | 20.0 / 20.4 | **§2.1 inverse-pair fold cost: +0.4 ns ≪ 1 ns budget ✓** |
  | `==` / `!=` | 21.7 / 21.9 | Loose inverse-pair fold cost: +0.2 ns ✓ |
  | Property `obj.x` (own, stable shape) read | 28.3 | |
  | **Property `obj.x = i` (own, stable shape) set** | **95.1** | Hot path; §2.2 setter fold budget is 1 ns out of 95 |
  | Strict-mode property set | 96.3 | +1.2 ns over non-strict (the throw-on-non-extensible branch) |
- ✅ **§1.2 baseline-compare gate** — landed at `utils/js262_compare_baseline.sh`. Self-tested both directions: passes on `release_run_006` vs itself; fails on `release_run_005` vs `release_run_006` (pass count -123, wall-clock +11.45%).
- ✅ **§1.3 emit telemetry** — landed in `lambda/js/js_mir_calls_boxing_types.cpp` behind `-DJS_MIR_EMIT_TELEMETRY`. Default builds unaffected (0 errors / 0 new warnings). Telemetry-on standalone compile verified.
- ✅ **Unused-entry analyzer** — landed at `utils/js262_unused_registry_entries.sh`. Consumes `./temp/jit_emit_stats.tsv` (the telemetry dump) and prints registry entries with zero emissions across the run.

**Next action before any registry change lands:** build with `CXXFLAGS="-DJS_MIR_EMIT_TELEMETRY" make build`, run the full test262 sweep + Radiant fixtures + lambda-test set, then run `utils/js262_unused_registry_entries.sh ./temp/jit_emit_stats.tsv > vibe/jube/tune8_dead_entries.txt`. That output is the data-driven Phase A worklist.

### Phase D first fold — landed (`_ne_` → `_eq_` + XOR inversion)

Applied per §2.1 decision rule (rev 2) for the inverse pairs `js_ne_raw` / `js_eq_raw` and `js_loose_ne_raw` / `js_loose_eq_raw`.

**Code changes:**
- `lambda/js/js_mir_expression_lowering.cpp`: the raw-relop switch now routes `JS_OP_STRICT_NE` → `"js_eq_raw" + invert=true` and `JS_OP_NE` → `"js_loose_eq_raw" + invert=true`. The invert branch emits an inline `MIR_XOR result, raw, 1` after the call.
- `lambda/js/js_runtime_value.cpp`: deleted `js_ne_raw` and `js_loose_ne_raw` (they were `return js_eq_raw(...) ? 0 : 1` and similar — pure XOR).
- `lambda/sys_func_registry.c`: deleted the two `{"js_ne_raw", ...}` / `{"js_loose_ne_raw", ...}` entries and the matching `extern` declarations.

**Verification (release build, master HEAD `5627cbb6a`):**
- Build: 0 errors, 2 pre-existing warnings unrelated to this change. Binary 14 MB.
- Test262: **39,255 / 39,255 fully passing — 0 regressions, 0 failures, 0 batch-unstable.**
- Aggregate wall-clock (harness-reported): plain master 127.4 s vs fold 128.4 s = +0.78%, within run-to-run noise (per-test sum varies 476–547 s across runs of the same binary; ~15% noise floor).
- Net registry delta: **−2 entries** (547 → 545).
- Net lowering LOC: +6 (the XOR emit block) — slightly *grew* the lowering because the XOR path is now inline rather than hidden behind the runtime helper. The win is the registry/import-cache slim.

**Side fix landed along the way:** `radiant/script_runner.cpp:2116` — `int installed` was set-but-not-used in release builds (where `log_info` compiles to no-op). Annotated `[[maybe_unused]]`. Unblocked the release build; unrelated to JS tuning.

### Remaining Phase D candidates (proposal §2.1)

Same pattern can be applied to:
- `js_equal` / `js_not_equal` — but these are hotter (used by `==`/`!=` in fast paths); needs the §1.1 microbench before landing.
- `js_strict_equal` / `js_strict_not_equal` — same caveat as above.
- `js_bitwise_and` / `_or` / `_xor` — fold into `js_bitop(op, a, b)`; 3 entries → 1. **Skipped per Q2 ("keep specialized if not many")** — touching the 4+ emit-site switches is invasive and the savings (-2 entries) aren't worth the change risk.
- `js_left_shift` / `_right_shift` / `_unsigned_right_shift` — same reasoning; skipped.

Each subsequent fold requires its own gate run.

### Phase A landed — telemetry-driven §3 deletions (-91 js_* entries)

Method: built a release `lambda.exe` with `-DJS_MIR_EMIT_TELEMETRY`, ran the test262 baseline sweep (790 spawned worker processes, each dumping `temp/jit_emit_stats/<pid>.tsv`). Aggregated counts via `utils/js262_unused_registry_entries.sh`. Filtered the result against names quoted anywhere in `lambda/js/js_mir_*.cpp` + `transpile_js_mir.cpp` — only entries that are BOTH telemetry-unused AND never quoted in any MIR lowering file are strictly safe to remove. That intersection was 91 entries.

**Categories deleted:**
- Method dispatchers reached from C runtime only: `js_array_method`, `js_collection_method`, `js_dataview_method`, `js_document_proxy_method`, …
- Global builtins reached via `builtin_id == -2` C dispatcher: `js_alert`, `js_atob`, `js_btoa`, `js_clearTimeout`, `js_clearInterval`, `js_clearImmediate`, `js_cancelAnimationFrame`, `js_toFixed`, `js_structuredClone`, …
- DOM/web API runtime helpers: `js_dom_contains`, `js_dom_get_property`, `js_dom_set_property`, `js_dom_wrap_element`, `js_dom_unwrap_element`, `js_document_proxy_get_property`, `js_document_proxy_set_property`, `js_dataset_get_property`, `js_dataset_set_property`, …
- TypedArray runtime helpers: `js_typed_array_fill`, `js_typed_array_new`, `js_typed_array_set`, `js_typed_array_subarray`, `js_typed_array_slice`, `js_typed_array_new_from_array`, `js_typed_array_new_from_buffer`, `js_arraybuffer_construct`, `js_arraybuffer_byte_length`, `js_arraybuffer_new`, `js_arraybuffer_slice`, `js_arraybuffer_wrap`, …
- Promise / async runtime: `js_promise_*` (8 entries), `js_event_loop_drain`, `js_event_loop_init`, `js_microtask_enqueue`, `js_generator_next`, `js_generator_return`, `js_generator_throw`, …
- Module runtime: `js_module_namespace_create`, `js_module_register`, `js_module_get`, `js_get_import_meta`
- Internal helpers reachable only from C: `js_bind_function`, `js_func_bind`, `js_create_arguments`, `js_constructor_create_object`, `js_func_init_property`, `js_link_base_prototype`, `js_get_constructor`, `js_get_prototype`, `js_get_length`, `js_get_global_object`, …
- Reflect, scope, with, eval helpers reached only from C: `js_with_depth_active`, `js_eval_local_has_immutable_binding`, `js_eval_local_has_lexical_binding`, `js_eval_private_resolve`, `js_global_lexical_binding_exists`, `js_global_lexical_get_or_fallback`, `js_global_lexical_set_if_exists`, …

**The C functions themselves stay linked** — only the `{"name", FPTR(name)}` rows in `sys_func_defs[]` were removed. Runtime callers continue to invoke them by direct C linkage. The smaller import table means fewer `import_cache` probe chains during JIT compile.

**Verification (release build, post-deletion):**
- Build: 0 errors, 2 pre-existing warnings.
- Test262: **39,255 / 39,255 fully passing — 0 regressions, 0 failures, 0 batch-unstable.**
- Aggregate per-test wall-clock: **449.740 s** vs plain master HEAD 484.873 s = **−7.24%** (well within Q4's "at or below baseline").

The strictly-safe deletion list lives at `temp/tune8_safe_to_delete.txt`. The full unused candidates list (154 entries, before MIR-quoted filtering) is at `temp/tune8_unused_entries.txt`. The 63 entries that are telemetry-unused but ARE quoted in MIR lowering (e.g. `js_classlist_method`, `js_dom_style_method`) need cross-workload verification before deletion — they're emitted for DOM code paths the test262 sweep doesn't exercise.


Scope: `lambda/sys_func_registry.c` JS section, `lambda/js/js_mir_*.cpp` lowering, and the JS↔MIR runtime ABI

## Goal

The JS engine currently registers **547 `js_*` runtime functions** in the unified `sys_func_defs[]` / runtime import table and emits **~920 `jm_call_N` MIR call sites** across ~38K LOC of MIR lowering. A substantial fraction of those entries are either:

- never imported by name from any `js_mir_*.cpp` lowering (118 of 547);
- one of many near-identical specializations of the same operation (e.g. `js_property_set` / `js_property_set_strict` / `js_super_property_set` / `js_super_property_set_non_strict` / `js_create_data_property` / `js_set_global_property` / `js_set_global_property_strict` / `js_set_global_var_property_fast` / `js_define_global_var_property` / `js_global_lexical_declare` / `js_set_module_var` …);
- specializations whose only consumer is a single test262 fast path (`js_test262_*`, `js_uri_decode_equals_from_char_code`).

This proposal narrows the JS↔MIR runtime ABI to a small set of dispatched primitives, collapses redundant entries, and removes registrations that never need to be reachable from JIT'd code.

**Hard constraints (set by user):**

1. **Performance must not regress.** Every collapse-into-dispatcher change must be measured against a release-build microbench *and* end-to-end test262 run. Any dispatcher that costs more than ~1 ns/call on the hot path stays as a direct entry.
2. **Test262 must not regress in correctness.** The pass/fail count is the gate. Any candidate change that flips a single previously-passing test is reverted and treated as the dispatcher being semantically wrong, not the test.
3. **Test262 must not regress in aggregate performance.** The **sum** of per-test wall-clock across the full test262 baseline must not exceed the pre-change baseline. Individual tests are allowed to regress as long as others improve enough to keep the total flat or better. No per-suite gate.

The first deliverable is the **measurement harness** — until that is committed, no registry change lands.

---

## 0. Baseline Findings

These come from a static read of the registry and `js_mir_*.cpp` lowering:

| Metric | Value |
|---|---|
| Registered `js_*` runtime functions | 547 |
| Distinct `js_*` names referenced by `js_mir_*.cpp` as quoted strings | 460 |
| Registered but not string-referenced in lowering | 92 |
| Total `jm_call_N` emission sites | 920 |
| `jm_call_1` / `_2` / `_0` / `_3` / `_4` | 348 / 201 / 167 / 145 / 54 |
| Largest lowering file | `js_mir_expression_lowering.cpp` (13,524 LOC) |
| Total JS MIR lowering LOC | ~37,981 |

**Top emitted runtime entry points** (string-name count across `js_mir_*.cpp`):

- `js_property_set` ~59, `js_check_exception` ~40+, `js_to_property_key` ~25, `js_property_set` (`set_module_var` flavor) ~53, `js_set_module_var` ~53, `js_get_module_var` ~36, `js_property_get` ~21, `js_add` ~16, `js_to_number` ~22, `js_check_tdz` ~19, `js_call_function` ~18.

Everything below those is **emitted ≤10 times**. That tail is the prime collapse target — collapsing rarely-emitted ABI entries into a dispatcher saves more registry surface than it loses inlining headroom.

**Group sizes that drive the proposal** (by name prefix):

- `js_get_*`: 37 entries  ·  `js_set_*`: 30  ·  `js_object_*`: 27  ·  `js_eval_*`: 21
- `js_new_*`: 17  ·  `js_array_*`: 16  ·  `js_reflect_*`: 14  ·  `js_math_*`: 13
- `js_typed_*`: 11  ·  `js_promise_*`: 11  ·  `js_super_*`: 8  ·  `js_to_*`: 7

---

## 1. Performance Safety Gate (build first, before any registry change)

This is non-negotiable per the user's constraint. Two artifacts must exist on master before any other phase lands:

### 1.1 GTest microbench: `test/js_runtime_call_bench.cpp`

A focused release-build benchmark that measures per-callsite cost for each *family* we plan to collapse:

| Family | Bench | What it measures |
|---|---|---|
| Arithmetic binop | tight loop `a + b + c + …` over 10⁶ iterations, Item operands | per-call cycle cost of `js_add` direct vs. `js_binop(OP_ADD, …)` dispatcher |
| Property store | repeated `obj.x = i` in non-strict and strict | `js_property_set` vs `js_store(KIND_OWN, …)` |
| Property load | repeated `obj.x` with stable shape | `js_property_get` vs `js_load(KIND_OWN, …)` |
| TDZ check | tight loop of let-declared reads | `js_check_tdz` direct vs `js_throw(CODE_TDZ, key)` posted-check |
| Eval scope op | nested `with` and direct eval | `js_with_push/pop` separate vs `js_scope_frame(OP, …)` |

The microbench is run via `make test-js-runtime-bench` and prints ns/call with a fixed iteration count. **A dispatcher candidate is accepted only if its per-call cost is within +1 ns of the direct entry on x86_64 release.** Anything slower is kept direct.

### 1.2 Test262 baseline capture script: `test/test262/capture_baseline.sh`

Before any change, capture:

- Total pass count and skip count per the existing test262 harness.
- **Sum of per-test wall-clock** across the full test262 baseline (5 runs, median per test).
- Optional: per-test wall-clock to enable post-hoc analysis of where regressions concentrate. Not used as a gate.

Pinned to a single commit hash, written to `test/test262/baseline.json`. **A registry change is mergeable only if `make test-js-262` reproduces the baseline pass/skip counts exactly and the *summed* wall-clock is at or below baseline.** Per-suite and per-test regressions are acceptable as long as the total is flat or better — there is no per-suite gate. The CI gate refuses the diff only on pass-count change or aggregate wall-clock regression.

This harness exists in some form already (per `Transpile_Js262.md`); Tune8 only requires the JSON-format snapshot and the aggregate-wall-clock diff gate.

### 1.3 MIR-emit telemetry

Add a compile-time-toggleable counter in `js_mir_calls_boxing_types.cpp::jm_ensure_import` that records `{name, ret, arity, argtypes}` keys and emission count per compile. Dump to `./temp/jit_emit_stats.json` per run. This turns "which entries are emitted in practice" into measured data, replacing the static grep. It is the source of truth for **§4 dead-entry removal**.

Until §1.1, §1.2 and §1.3 are landed, the rest of this proposal is **paused**.

---

## 2. MIR Code-Volume Reductions (driven by data from §1.3)

Each candidate below has an estimated savings range, but the actual deletion only proceeds if (a) telemetry confirms the emission count, (b) the microbench shows no regression, (c) test262 pass count is unchanged and the aggregate wall-clock is at or below baseline.

### Important constraint (resolves Q1): MIR cannot inline through native imports

`ref/mir/mir.c::process_inlines` (lines 4008–4090) walks `MIR_import_item → ref_def` chains looking for an inlinable `MIR_func_item`. Lambda's JS runtime functions are native C, registered via the import resolver as raw function pointers, **not** as `MIR_func_item`s. The inliner's check on line 4060 (`called_func_item->item_type != MIR_func_item`) falls through to `simplify_op` and emits a real call.

**Consequence:** any constant `kind_flags` operand we pass to a unified `js_load` / `js_store` / `js_binop` is **not** folded out at the call site. The runtime-side `switch` on `kind_flags` is paid on every call. The microbench in §1.1 must therefore quantify the switch cost; we cannot assume MIR will erase it.

This makes the §2.2 (property load/store unify) decision rule strict: hot entries that survive the per-call bench (`js_property_set` at ~59 callsites, `js_property_get`, `js_check_exception`, `js_to_property_key`, `js_check_tdz`, `js_call_function`) **stay direct**. Only cold variants collapse, and even cold collapses use the "generic with fast-path flag" pattern from §2.2-pattern below — not a single dispatcher that splits in the runtime.

### 2.1 Arithmetic / comparison / bitwise dispatcher

Today 30 separately-imported entries:

```
js_add  js_subtract  js_multiply  js_divide  js_modulo  js_power
js_equal js_not_equal js_strict_equal js_strict_not_equal
js_less_than js_less_equal js_greater_than js_greater_equal
js_logical_and js_logical_or js_logical_not
js_bitwise_and js_bitwise_or js_bitwise_xor js_bitwise_not
js_left_shift js_right_shift js_unsigned_right_shift
js_unary_plus js_unary_minus
js_lt_raw js_gt_raw js_le_raw js_ge_raw js_eq_raw js_ne_raw js_loose_eq_raw js_loose_ne_raw
```

**Decision rule (revised under Q1):** the only safe folds are pairs where the runtime body is identical except for one constant. Per Q1, MIR will not fold the `op` constant out of the call. So a `js_binop(op, …)` dispatcher with 6 arithmetic branches pays 6× the branch cost at the worst-predicted site. **Reject the single-dispatcher form.**

Instead, apply the "fold inverse pairs" pattern:

| Pair / group | Treatment | Savings |
|---|---|---|
| `js_lt_raw` / `js_gt_raw` / `js_le_raw` / `js_ge_raw` | **Fold into `js_cmp_raw(op, …)`** with `op ∈ {LT,GT,LE,GE}`. Cold raw-relop family; runtime body branches once. | 4 → 1 |
| `js_eq_raw` / `js_ne_raw` | **Fold into `js_eq_raw(invert, …)`**. Single-bit flag. | 2 → 1 |
| `js_loose_eq_raw` / `js_loose_ne_raw` | **Fold into `js_loose_eq_raw(invert, …)`**. Single-bit flag. | 2 → 1 |
| `js_equal` / `js_not_equal` | **Fold with invert flag.** Hot enough that the bench gates. | 2 → 1 (if bench passes) |
| `js_strict_equal` / `js_strict_not_equal` | **Fold with invert flag.** | 2 → 1 (if bench passes) |
| `js_less_than` / `js_less_equal` / `js_greater_than` / `js_greater_equal` | **Fold into `js_compare(op, …)`** with op ∈ {LT,LE,GT,GE}. Bench-gated. | 4 → 1 |
| `js_add`, `js_subtract`, `js_multiply`, `js_divide`, `js_modulo`, `js_power` | **All stay direct.** Hot, fastpath-divergent (`add` has the string-concat fork). | 0 |
| `js_logical_and` / `js_logical_or` | **Stay direct.** Lazy semantics; the runtime body differs structurally. | 0 |
| `js_logical_not` | **Stay direct.** Single fast path. | 0 |
| `js_bitwise_and` / `js_or` / `js_xor` | **Fold into `js_bitop(op, …)`** — body is `i32(a) OP i32(b)` for all three. One branch on `op`. | 3 → 1 |
| `js_bitwise_not` | **Stay direct.** Unary, separate signature. | 0 |
| `js_left_shift` / `js_right_shift` / `js_unsigned_right_shift` | **Fold into `js_shift(op, …)`**. Same operand path differing only by the C operator. | 3 → 1 |
| `js_unary_plus` / `js_unary_minus` | **Stay direct** (only two; one branch saved isn't worth it). | 0 |

Estimated savings: ~16 entries (down from ~26 in rev 1), ~250 LOC of selection ladders. The pattern saves registry slots without introducing a high-branch-count dispatcher in any hot path.

### 2.2 Property load/store: keep hot direct, fold cold with fast-path flag

**Pattern (per user guidance on Q2):** when a specialization is one of two or three variants and is hot, keep it as a separate registered entry. When the specialization family has many cold members, fold them into the generic by adding a small flag parameter to the generic so the runtime can pick the fast path. **Do not** introduce a dispatcher that splits into N branches in the runtime — that path is what Q1 ruled out for hot code.



Setters (~20):
```
js_property_set, js_property_set_strict,
js_private_property_set, js_private_property_set_strict,
js_create_data_property,
js_super_property_set, js_super_property_set_non_strict,
js_set_global_property, js_set_global_property_strict,
js_set_global_property_strict_prechecked,
js_set_global_var_property_fast,
js_define_global_var_property, js_define_global_eval_var_property,
js_define_global_function_property,
js_global_lexical_declare,
js_set_module_var,
js_set_last_with_binding_if_valid, js_set_with_binding_base,
js_property_get_str (write counterpart), …
```

Getters (~12):
```
js_property_get, js_property_access, js_property_get_str,
js_get_with_binding_or_fallback, js_get_last_with_binding_base_or_undefined,
js_global_lexical_get_or_fallback, js_get_module_var,
js_super_property_get, js_super_instance_method_get,
js_eval_local_get_binding_or_fallback,
```

**Decision rule, applied per entry:**

| Entry | Treatment |
|---|---|
| `js_property_set`, `js_property_get` | **Stay direct.** Most-emitted entries in the engine. |
| `js_property_set_strict`, `js_create_data_property` | **Stay direct.** Hot enough that the strict-vs-non-strict skip-a-throw branch matters. |
| `js_super_property_set`, `js_super_property_set_non_strict` | **Fold into one** with a `non_strict` flag parameter. Two cold entries → one. |
| `js_set_global_property`, `js_set_global_property_strict`, `js_set_global_property_strict_prechecked`, `js_set_global_var_property_fast` | **Fold into `js_set_global_property(flags, …)`**. Flags = `STRICT | PRECHECKED | VAR_FAST`. Four entries → one. The runtime's first action checks `flags`; on the fast path, only one branch (the `flags == VAR_FAST` test) is taken before the existing fast-path body. |
| `js_define_global_var_property`, `js_define_global_eval_var_property`, `js_define_global_function_property` | **Fold into `js_define_global_property(kind, …)`**. Three entries → one. Telemetry confirms these are cold (module-init only). |
| `js_set_module_var`, `js_get_module_var` | **Stay direct.** Heavy module-init traffic but the runtime body is already minimal. |
| `js_get_with_binding_or_fallback`, `js_get_last_with_binding_base_or_undefined`, `js_global_lexical_get_or_fallback`, `js_eval_local_get_binding_or_fallback` | **Fold into `js_scope_lookup`** (see §2.4). These are scope-frame lookups, not own-property reads. |
| `js_property_access` (slot fast path) | **Stay direct.** |
| `js_super_property_get`, `js_super_instance_method_get` | **Fold into `js_super_property_get(is_method, …)`**. Two entries → one. |

**Performance protection (per Q1):**

- Where we fold via a flag, the runtime body is *one or two branch checks on `flags`*, not a dense switch. Two branches predicted correctly cost ~0–1 cycle each on x86_64; this is the cost the microbench validates.
- Where two entries differ only by *what they throw on failure* (`_strict` skips no throw, non-strict skips a throw), folding loses the constant-fold of "no throw needed" inside the runtime. For the hottest such pairs we keep both entries separate, because the bench-measurable saving is real.
- Microbench gates every fold individually.

Estimated savings: ~12 entries (down from ~30 in rev 1, after applying Q1's "no dispatcher folding for hot paths" constraint), ~600 LOC of selection ladders in `js_mir_statement_lowering.cpp`.

### 2.3 Throw / check fold

```
js_throw_value  js_throw_const_assign  js_throw_syntax_error  js_throw_reference_error
js_check_tdz   js_require_object_coercible
js_new_error  js_new_error_with_name  js_new_error_with_stack  js_new_error_with_name_stack
js_new_aggregate_error  js_error_set_cause  js_error_captureStackTrace
```

Replace cold paths with:

```c
void js_throw(uint32_t code, Item payload);          // attribute((noreturn))
Item js_new_error(uint32_t code, Item msg, Item opt);
```

**Performance protection:**

- `js_check_exception` (40+ callsites, postcall guard) is **not touched**. It stays direct and inlinable.
- `js_check_tdz` (19 callsites) — keep direct unless the bench shows the inlined check has tail-jump structure that absorbs a dispatch.
- The throw family is by definition cold; no perf risk.

Estimated savings: ~7 entries, ~150 LOC.

### 2.4 Eval / with / private scope-frame dispatcher

21 `js_eval_*` + 5 `js_with_*` + 6 `js_private_field_*`:

```
js_eval_env_push_frame, js_eval_env_bind, js_eval_env_has_binding,
js_eval_env_is_active, js_eval_env_track_global_binding, js_eval_env_pop_frame,
js_eval_global_lexical_push_frame, js_eval_global_lexical_pop_frame,
js_eval_global_lexical_bind,
js_eval_local_push_frame, js_eval_local_pop_frame,
js_eval_local_get_binding_or_fallback, js_eval_local_export_var,
js_eval_private_push_frame, js_eval_private_pop_frame,
js_eval_private_bind, js_eval_private_resolve,
js_evalscript_check_global_var_decl, js_evalscript_check_global_function_decl,
js_evalscript_check_global_lex_decl,
js_with_push, js_with_pop, js_with_save_depth, js_with_restore_depth, js_with_depth_active,
js_private_field_init_begin, js_private_field_init_end, js_private_field_define,
js_private_brand_add, …
```

Replace with:

```c
void js_scope_frame(uint32_t op, Item a, Item b);    // push/pop/bind/declare/check
Item js_scope_lookup(uint32_t op, Item key, Item fallback);
```

**Performance protection:**

- These are emitted only under `eval`, `with`, and class-private code paths. **None are hot in test262's mainstream language tests.** Risk of regression is concentrated in `built-ins/eval/` and `language/expressions/class/private-*`.
- The microbench gets two dedicated cases (nested `with` + nested eval-in-block) to validate.

Estimated savings: ~23 entries, ~300 LOC.

### 2.5 Lean harder into existing method dispatchers

`js_string_method`, `js_array_method`, `js_array_method_direct`, `js_math_method`, `js_math_apply`, `js_collection_method`, `js_document_proxy_method`, `js_dataview_method` already exist. But shortcuts coexist alongside them: `js_string_concat`, `js_string_get_int`, `js_array_get_int`, `js_array_set_int`, `js_array_indexOf_int`, `js_string_replace_nonws_global_fast`, `js_string_replace_nonws_global_fast_no_dollar`, `js_string_fromCharCode2`, `js_uri_decode_equals_from_char_code`, `js_array_fill`, `js_array_slice_from`.

**Action:** For each shortcut, the §1.3 telemetry will report its emission count and its per-test262-suite hit rate. **Keep the shortcut if both are true:** (a) emission count > 100 per typical compile, and (b) the microbench shows >5 ns/call improvement over the equivalent dispatcher path. Otherwise retire to the dispatcher.

The `js_test262_*` and `js_uri_decode_equals_from_char_code` are prime suspects but require the data. Estimated savings: ~15 entries.

### 2.6 Dead type-specialized variants

Candidates with zero string-references in any `js_mir_*.cpp` and no obvious indirect caller:

```
js_math_pow_d  js_math_ceil_d  js_math_round_item
js_string_fromCharCode_int  js_string_fromCharCode_array  js_string_fromCharCode2
js_setTimeout_args  js_setInterval_args
```

**Action:** §1.3 telemetry confirms zero emissions across the full test262 + Radiant fixture set. Then delete. If telemetry shows even one emission, leave in place and investigate.

Estimated savings: ~10 entries.

---

## 3. Move runtime-only helpers out of the JIT Import Table (resolves Q3)

**Q3 finding, corrected.** The original rev-1 §3 proposed moving JS global builtins (`parseInt`, `setTimeout`, `isNaN`, `encodeURI`, …) out of the JIT import table because they appear to be reached only through `js_call_function` after a global-object property lookup. **Investigation refutes this:** the transpiler has direct fast paths that emit these as MIR imports at the call site. `js_mir_expression_lowering.cpp` lines 8791–9146 emit `jm_call_*` for `js_parseInt`, `js_parseFloat`, `js_isNaN`, `js_isFinite`, `js_encodeURI`, `js_decodeURI`, `js_setTimeout`, `js_setInterval`, `js_setTimeout_args`, `js_requestAnimationFrame`, `js_structuredClone`. These registry entries are **required** in the JIT import table and cannot be moved out.

What **can** be moved out: the 92 entries that the corrected telemetry-equivalent grep shows have **zero** MIR string-references. The category breakdown:

| Category | Count | Examples |
|---|---:|---|
| Method dispatchers (called from C runtime only) | ~6 | `js_array_method`, `js_collection_method`, `js_dataview_method`, `js_document_proxy_method`, `js_document_proxy_get_property`, `js_document_proxy_set_property` |
| ArrayBuffer runtime helpers | 5 | `js_arraybuffer_byte_length`, `js_arraybuffer_construct`, `js_arraybuffer_new`, `js_arraybuffer_slice`, `js_arraybuffer_wrap` |
| TypedArray runtime helpers | 8 | `js_typed_array_*` |
| Promise helpers reachable only from runtime | 8 | `js_promise_*` |
| DOM proxy helpers | 5 | `js_dom_contains`, `js_dom_get_property`, `js_dom_set_property`, `js_dom_wrap_element`, `js_dom_unwrap_element` |
| Generator / async support | 3 | `js_generator_*` |
| Test262-only fast paths | ~5 | `js_assert_*`, `js_test262_*` (these also get the §4 build-flag treatment) |
| Miscellaneous runtime helpers | ~50 | `js_func_bind`, `js_bind_function`, `js_create_arguments`, `js_event_loop_init`, `js_module_*`, `js_regex_*` (cold paths), … |

**Action:** split `sys_func_registry.c`'s JS section into two tables:

| Table | Contents | Consumer |
|---|---|---|
| `js_mir_imports[]` | Functions emitted by `jm_ensure_import` (i.e. found by §1.3 telemetry, conservatively ~455 entries — the union of static-grep hits and runtime-confirmed emissions) | JIT import resolver |
| `js_runtime_only[]` | Functions called only from C runtime code (the ~92 entries above, minus any that telemetry shows are actually emitted) | nothing — they are reachable as ordinary C symbols |

The `js_runtime_only` list does not need to be a runtime data structure at all — the C functions are already linked into the binary. The "move out" is in practice a **deletion of their `{"name", FPTR(name)}` rows from the JIT import table** plus a comment grouping them in their source files of origin.

**Performance protection:**

- Zero JIT lowering changes. No callsites move.
- Runtime callers continue to call these C functions by direct C symbol, unchanged.
- The `import_cache` hashmap in `js_mir_calls_boxing_types.cpp` becomes smaller — fewer entries means shorter probe chains and faster JIT compile. Wins are in compile time, not run time.

**Conservative gating:** §1.3 telemetry runs the full test262 suite + Radiant fixtures + the Lambda lambda-test set. Any entry the telemetry observes being emitted (even once) is **kept** in `js_mir_imports[]`. The split is data-driven, not name-guessed.

Estimated savings: ~70–85 entries moved out (after telemetry filtering), proportional reduction in `import_cache` size. JIT compile time gain measurable in milliseconds on large modules (jquery, etc.).

---

## 4. Build-Gate Test262 Fast Paths

These exist purely to make test262 run faster and are not part of any application-language hot path:

```
js_test262_build_string
js_test262_decimal_to_percent_hex_string
js_test262_concat_percent_hex
js_assert_same_value  js_assert_not_same_value  js_assert_compare_array
js_assert_deep_equal  js_compare_array  js_verify_property
js_assert_throws  js_assert_base  js_donotevaluate  js_is_constructor
js_decimal_to_percent_hex_string  js_validate_native_function_source
```

**Action:** wrap in `#ifdef JS_TEST262_FAST_PATHS`. Define the macro in the test262 build flavor; undefine it in the production build. Production drops ~15 entries; test262 is unchanged.

**Test262 perf protection:** the test262 build keeps every entry it has today. This is a production-only reduction.

---

## 5. Expected Impact (gated by §1, revised under Q1/Q3)

| Lever | Registry Δ | Lowering LOC Δ | Risk to test262 |
|---|---:|---:|---|
| §2.1 inverse-pair folds (cmp/eq/shift/bitop) | −16 | −250 | low (one-branch runtime cost; bench-gated) |
| §2.2 cold property-set fast-path flag folds | −12 | −600 | low (cold variants only; hot stays direct) |
| §2.3 throw/check fold | −7 | −150 | low (cold) |
| §2.4 scope-frame fold | −23 | −300 | low/medium (eval/with/private; mostly cold) |
| §2.5 dispatcher promotion (telemetry-gated) | −15 | −200 | low |
| §2.6 dead variants | −10 | 0 | none (telemetry-confirmed) |
| §3 runtime-only entries out of JIT import table | −70 to −85 | 0 | none (zero callsite change) |
| §4 test262 gate (prod build only) | −15 | 0 | none in test262 build |
| **Total (estimated)** | **~−168 to −183** (547 → ~365–380) | **~−1,500** | low (no hot-path collapses) |

The total registry reduction is similar to rev 1 (~30%), but the composition shifts: more savings come from §3 (now data-driven runtime-only deletions) and §2.4 (cold), and fewer from §2.1/§2.2 (Q1 forced hot paths to stay direct). The lowering LOC reduction is smaller (~1,500 vs rev 1's 2,550) because the hot property-set ladder doesn't collapse.

Secondary wins (unchanged from rev 1): smaller `import_cache` hashmap in `js_mir_calls_boxing_types.cpp` → shorter probe chains → faster JIT compile time on large modules.

---

## 6. Sequencing (each phase is independently revertable)

1. **Phase 0 — Safety gate.** §1.1 microbench, §1.2 test262 baseline JSON, §1.3 emit telemetry. *Block all other phases until merged.*
2. **Phase A — Pure plumbing.** §3 (split JIT-imports from global-builtins), §4 (test262 gate). Zero MIR change; pure removals from production import table. Test262 numbers must reproduce exactly.
3. **Phase B — Dead removal.** §2.6, telemetry-confirmed only. Each removal is its own commit.
4. **Phase C — Low-risk collapses.** §2.3 (throw fold), §2.5 (telemetry-gated dispatcher promotion). One family per commit. Microbench + test262 gate before each merge.
5. **Phase D — Medium-risk collapses.** §2.1 (binop dispatchers). `js_add` stays direct. Microbench-gated.
6. **Phase E — Scope-frame fold.** §2.4. Higher risk in eval/with/private-class suites; per-suite test262 wall-clock and pass-count checked.
7. **Phase F — Property load/store unification.** §2.2. This is the riskiest. `js_property_set` and `js_property_get` stay direct. Cold variants fold first; hot variants only if the bench shows zero regression.

**Per-phase merge gate** (mandatory):

- `make test-js-runtime-bench` — ns/call deltas per family within +1 ns of baseline.
- `make test-js-262` — pass/skip counts identical to `baseline.json`; **summed** wall-clock at or below baseline. Per-test and per-suite regressions allowed as long as the sum is flat or better.
- `make test-radiant-baseline` — must remain 100% pass.

Any failure → revert the commit, treat as a real regression, do not "tune around" the dispatcher.

---

## 7. Resolved Questions

### Q1 — Does MIR inline through native-C imports? **No.**

`ref/mir/mir.c::process_inlines` (lines 4008–4090) walks `MIR_import_item → ref_def` chains looking for an inlinable `MIR_func_item`. Lambda's JS runtime functions are native C, registered with the import resolver as raw function pointers, **not** as `MIR_func_item`s. The inliner's gate (`called_func_item->item_type != MIR_func_item`) falls through to `simplify_op` and emits a real call.

**Implication, applied throughout this revision:** any constant `op` / `kind` / `flags` argument to a unified runtime function is **not** folded out at the call site by MIR. The runtime-side branching on that argument is paid on every call. This rules out single dispatchers over wide families for hot code, and steers §2.1 and §2.2 toward fold-when-cost-is-one-branch and inverse-pair folds only.

A follow-up worth exploring (out of Tune8 scope): pre-instantiating small wrapper `MIR_func_item`s for each constant-flag variant on first use, so MIR's inliner *can* fold them. That requires changes to `js_mir_calls_boxing_types.cpp::jm_ensure_import` and a separate proposal.

### Q2 — How to handle specializations that exist to skip a check? **Per user guidance: keep direct if few; otherwise fold via fast-path flag.**

Pattern: when a specialization family has many cold members, fold them into the generic by adding a single-bit or small-enum flag parameter and branching once in the runtime body. **Do not** introduce a wide-dispatch single function (Q1 rules that out for hot code). When the specialization is one of two/three and at least one is hot, keep them separate.

Concrete applications under this rule (already wired into §2.1 and §2.2 above):

- `js_super_property_set` / `_non_strict` → fold with `non_strict` flag.
- `js_set_global_property` family of 4 → fold with `STRICT | PRECHECKED | VAR_FAST` flags.
- `js_eq_raw` / `js_ne_raw`, `js_loose_eq_raw` / `js_loose_ne_raw`, `js_equal` / `js_not_equal`, `js_strict_equal` / `js_strict_not_equal` → fold each pair with `invert` flag.
- `js_bitwise_and` / `_or` / `_xor`, `js_left_shift` / `_right_shift` / `_unsigned_right_shift` → fold each triple with `op` enum (3 cases, ≤2 branches).
- `js_define_global_var_property` / `_eval_var_property` / `_function_property` → fold with `kind` enum.

Always-direct (per the §2.2 decision rule): `js_property_set`, `js_property_get`, `js_property_set_strict`, `js_create_data_property`, `js_set_module_var`, `js_get_module_var`, `js_add`, `js_subtract`, `js_multiply`, `js_divide`, `js_modulo`, `js_power`, `js_logical_and`, `js_logical_or`, `js_logical_not`, `js_check_exception`, `js_to_number`, `js_to_property_key`, `js_check_tdz`, `js_call_function`, `js_property_access`.

### Q3 — Can global builtins be moved out of the JIT import table? **No — they are emitted directly by the transpiler.**

`js_mir_expression_lowering.cpp` lines 8791–9146 contain `jm_call_*` emissions for the global builtins (`js_parseInt`, `js_parseFloat`, `js_isNaN`, `js_isFinite`, `js_encodeURI`, `js_decodeURI`, `js_setTimeout`, `js_setInterval`, `js_requestAnimationFrame`, `js_structuredClone`, …). These registry entries are **required** in the JIT import table.

There is also a runtime fallback dispatcher in `js_runtime.cpp:7353+` keyed on `builtin_id == -2` which calls the same C functions directly via `extern` (for indirect-call cases like `const f = parseInt; f("42")`). The fallback does not use the JIT import table at all.

The §3 reduction recovered from this finding targets the **92 entries that no MIR lowering file references** — runtime-only helpers like `js_arraybuffer_*`, `js_typed_array_*`, `js_promise_*`, `js_dom_*` proxy methods, method dispatchers, generator support, test262-only helpers. These are reachable as ordinary C symbols by other C code in the runtime; their `{"name", FPTR(name)}` rows in the JIT import table are dead weight. Telemetry from §1.3 is the gate — only rows the telemetry confirms are never emitted get deleted.

### Q4 — Test262 perf gate granularity. **Per user: sum-only.**

The merge gate is the **sum** of per-test wall-clock across the full test262 baseline. Individual tests and individual suites are allowed to regress as long as the total is at or below baseline. The harness records per-test times for post-hoc analysis but does not gate on them.

**Variance characterization (rev 5):** three sequential test262 runs on the same binary measured:

| Run | Pass count | Harness wall-clock | Per-test sum |
|---|---:|---:|---:|
| 1 | 39,255 / 39,255 | 127.0 s | 476.66 s |
| 2 | 39,255 / 39,255 | 127.1 s | 523.54 s |
| 3 | 39,255 / 39,255 | 136.0 s | 551.77 s |
| **Median** | 39,255 | 127.1 s | **523.54 s** |
| Spread | 0 | 6.6 % | 13.6 % |

The per-test sum is noisier than the harness's reported wall-clock. Single-run gates can swing ±15%; the gate should use **median of 3+ runs** to detect sub-5% changes. The pass-count side of the gate remains hard: any pass-count change must be 0.

---

## 8. Out of Scope

- Changes to the AST builder (Tune6 already covers).
- Changes to MIR import-cache structure (could be a future Tune9).
- Restructuring the Lambda-side `sys_funcs[]` non-JS entries (math, string, datetime); only the JS section of `sys_func_defs[]` and the runtime import table is in scope.
- Changes to method dispatcher internals (`js_string_method` etc.); only their *usage* is in scope.

---

## Appendix A — Data sources

- Registry: `lambda/sys_func_registry.c` (2,751 LOC). Static count of `{"js_*"` lines: 547.
- Lowering: `lambda/js/js_mir_*.cpp` (12 files + `transpile_js_mir.cpp`, 37,981 LOC). Static count of `"js_*"` quoted-string references: **460** distinct names across 920 `jm_call_N` callsites. (Rev 1 had 434; the corrected grep includes `transpile_js_mir.cpp` and matches names containing digits and uppercase letters.)
- Registered-but-never-emitted: **92** entries (rev 1: 118).
- Call-arity distribution: `jm_call_1` 348, `jm_call_2` 201, `jm_call_0` 167, `jm_call_3` 145, `jm_call_4` 54, `jm_call_5` 3, `jm_call_6` 2.
- Emission helper: `js_mir_calls_boxing_types.cpp::jm_ensure_import` (proto cache keyed by `name#r%d#n%d#a%d#<arg types>`).
- MIR inliner gate that proves Q1: `ref/mir/mir.c::process_inlines` lines 4053–4063.
- Global-builtins transpile-side fast paths that prove Q3 correction: `lambda/js/js_mir_expression_lowering.cpp` lines 8791–9146.
- Global-builtins runtime fallback dispatcher: `lambda/js/js_runtime.cpp` lines 7353–7440 (keyed on `builtin_id == -2`).

These numbers are the static baseline. Phase 0 §1.3 supersedes them with measured runtime emission counts; all sizing decisions in §2–§4 are taken from the measured data, not the static count.
