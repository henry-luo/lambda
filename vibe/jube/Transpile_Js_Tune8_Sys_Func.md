# Transpile JS Tune8 — Sys-Func Registry and JS MIR Runtime Interface Reduction

Date: 2026-06-12
Status: proposal (rev 1)
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
3. **Test262 must not regress in performance.** Wall-clock time for the full test262 run, plus the per-suite breakdown for `built-ins/`, `language/expressions/`, `language/statements/`, is held within ±2% of the pre-change baseline.

The first deliverable is the **measurement harness** — until that is committed, no registry change lands.

---

## 0. Baseline Findings

These come from a static read of the registry and `js_mir_*.cpp` lowering:

| Metric | Value |
|---|---|
| Registered `js_*` runtime functions | 547 |
| Distinct `js_*` names referenced by `js_mir_*.cpp` as quoted strings | 434 |
| Registered but not string-referenced in lowering | 118 |
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
- Per-suite (`built-ins/`, `language/expressions/`, `language/statements/`, `language/module-code/`, `language/global-code/`) pass counts.
- Total wall-clock time and per-suite wall-clock (5 runs, median).
- Per-suite RSS peak.

Pinned to a single commit hash, written to `test/test262/baseline.json`. **A registry change is mergeable only if `make test-js-262` reproduces the baseline pass/skip counts exactly and total wall-clock is within ±2%.** The CI gate refuses the diff otherwise.

This harness exists in some form already (per `Transpile_Js262.md`); Tune8 only requires the JSON-format snapshot and the diff gate.

### 1.3 MIR-emit telemetry

Add a compile-time-toggleable counter in `js_mir_calls_boxing_types.cpp::jm_ensure_import` that records `{name, ret, arity, argtypes}` keys and emission count per compile. Dump to `./temp/jit_emit_stats.json` per run. This turns "which entries are emitted in practice" into measured data, replacing the static grep. It is the source of truth for **§4 dead-entry removal**.

Until §1.1, §1.2 and §1.3 are landed, the rest of this proposal is **paused**.

---

## 2. MIR Code-Volume Reductions (driven by data from §1.3)

Each candidate below has an estimated savings range, but the actual deletion only proceeds if (a) telemetry confirms the emission count, (b) the microbench shows no regression, (c) test262 deltas are zero pass/fail and ≤2% wall-clock.

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

Collapse into four entries:

```c
Item   js_binop(uint32_t op, Item a, Item b);     // ADD..POWER, AND..OR, BAND..USHR
Item   js_unop(uint32_t op, Item v);              // PLUS, MINUS, NOT, BNOT
int64_t js_relop_raw(uint32_t op, Item a, Item b); // LT, LE, GT, GE, EQ, NE, LOOSE_EQ, LOOSE_NE
int64_t js_bitop_raw(uint32_t op, int64_t a, int64_t b);
```

**Performance protection:**

- `js_add` is **not collapsed** — 16 callsites, hot, and its fast path for two int Items is one branch. Direct entry kept.
- Inside the dispatcher, the `switch (op)` becomes a constant `op` operand at the MIR callsite, so MIR sees a literal and the runtime's switch is one indirect jump. The microbench measures whether this costs more than ~1 ns vs direct.
- If even one of the four dispatchers regresses on the bench, that family stays direct. Partial collapse is allowed.

Estimated savings: ~26 registry entries, ~400 LOC of switch ladders in `js_mir_expression_lowering.cpp`. **Decision lever: §1.1 microbench.**

### 2.2 Property load/store dispatcher

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

Replace with two flag-driven entries:

```c
// kind low 8 bits: OWN | SUPER | GLOBAL | GLOBAL_LEXICAL | MODULE | WITH | PRIVATE | EVAL_LOCAL
// flags high bits: STRICT, DEFINE_DATA, IS_METHOD, IS_PROPS_FAST, NON_STRICT_SUPER, PRECHECKED
Item js_load (uint32_t kind_flags, Item recv, Item key);
Item js_store(uint32_t kind_flags, Item recv, Item key, Item value);
```

**Performance protection:**

- `kind_flags` is a constant at the MIR callsite — the runtime's first action is `switch (kind_flags & 0xff)`. On x86_64 with a dense switch (8 entries), that's one indirect branch predicted perfectly after the first call at a given site (since branch history is keyed by callsite address).
- `js_property_set` (the hot non-strict own setter) is the most-emitted entry in the engine (~59 callsites). Even a 1 ns regression here is unacceptable. The microbench measures this **first** and, if regression is non-zero, we **keep `js_property_set` direct** and only collapse the cold variants.
- The `_prechecked` and `_fast` variants exist specifically to skip a branch the runtime would otherwise take. Collapsing them into a flag means the branch comes back. Measure: if even the fast path adds a ns, we keep those direct and only collapse the cold majority.

Estimated savings: ~30 entries, ~1,500 LOC across `js_mir_statement_lowering.cpp` and `js_mir_expression_lowering.cpp` (where setter selection ladders dominate). **Decision lever: §1.1 microbench + test262 wall-clock delta.**

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

## 3. Move JS Global Builtins out of the JIT Import Table

About **30 entries** are JS standard globals — `setTimeout`, `setInterval`, `setImmediate`, the matching `clear*` and `cancelAnimationFrame`, `requestAnimationFrame`, `parseInt`, `parseFloat`, `isFinite`, `isNaN`, `encodeURI`, `encodeURIComponent`, `decodeURI`, `decodeURIComponent`, `structuredClone`, `toFixed`, `js_string_fromCharCode`, `js_string_fromCodePoint`, `js_string_fromCharCode_array`, `js_string_fromCodePoint_array`. **These are invoked through `js_call_function` after a property lookup on the global object — MIR never emits their names as imports.**

**Action:** split `sys_func_registry.c`'s JS section into two tables:

| Table | Contents | Consumer |
|---|---|---|
| `js_mir_imports[]` | Functions whose name is emitted into MIR by `jm_ensure_import` | the JIT import resolver |
| `js_global_builtins[]` | Functions installed as properties of the global object | runtime global-object setup |

A function can appear in **both** if it's also emitted directly (e.g. `js_string_method` is a dispatcher and a global, in some embeddings). The §1.3 telemetry tells us which.

**Performance protection:**

- Zero JIT lowering changes. Pure plumbing split.
- `js_call_function`'s path through the global object is unchanged — same function pointer is stored.

Estimated savings: ~30 entries removed from the JIT import table (so the import cache hashmap is ~5% smaller and JIT compile saves a few hundred ns per source file).

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

## 5. Expected Impact (gated by §1)

| Lever | Registry Δ | Lowering LOC Δ | Risk to test262 |
|---|---:|---:|---|
| §2.1 binop/relop/bitop fold | −26 | −400 | low (microbench-gated) |
| §2.2 load/store unify | −30 | −1,500 | **medium** — hottest path |
| §2.3 throw/check fold | −7 | −150 | low (cold) |
| §2.4 scope-frame fold | −23 | −300 | medium (eval/with/private) |
| §2.5 dispatcher promotion | −15 | −200 | low (telemetry-gated) |
| §2.6 dead variants | −10 | 0 | none (telemetry-confirmed) |
| §3 global builtins move | −30 | 0 | none |
| §4 test262 gate (prod only) | −15 | 0 | none in test262 build |
| **Total (upper bound)** | **~−155** (547 → ~392) | **~−2,550** | mixed |

Plus secondary wins from a smaller `import_cache` hashmap in `js_mir_calls_boxing_types.cpp` — fewer distinct `(name, ret, arity, argtypes)` keys means shorter probe chains during JIT compilation.

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
- `make test-js-262` — pass/skip counts identical to `baseline.json`; total wall-clock within ±2%; no individual suite worse than ±3%.
- `make test-radiant-baseline` — must remain 100% pass.

Any failure → revert the commit, treat as a real regression, do not "tune around" the dispatcher.

---

## 7. Open Questions

- **Q1.** When `kind_flags` is a constant operand at every MIR callsite of a unified `js_load` / `js_store`, does MIR's optimizer fold the runtime-side switch enough to eliminate the dispatch cost? If MIR inlining of imports is not in place, the dispatch is *always* paid. Check `transpile_js_mir.cpp` for any inline pragma usage and decide whether to land MIR's inlining first.
- **Q2.** Some "specialized" entries (`js_set_global_property_strict_prechecked`, `js_set_global_var_property_fast`) skip a check the generic does. The runtime cost of re-doing that check inside a unified entry must be measured per call **and** integrated over a typical test262 run, because the per-call cost is small but the call count is huge in module init.
- **Q3.** Confirm that the JS engine's global-object initialization can resolve function pointers from a separate `js_global_builtins[]` table without restructuring `js_runtime` init. If not, the §3 split needs a small init refactor first.
- **Q4.** Does the test262 harness expose per-test wall-clock? If only per-suite, the ±2% gate is the right granularity. If per-test, a stricter check (no individual test more than 2σ slower) is preferable.

---

## 8. Out of Scope

- Changes to the AST builder (Tune6 already covers).
- Changes to MIR import-cache structure (could be a future Tune9).
- Restructuring the Lambda-side `sys_funcs[]` non-JS entries (math, string, datetime); only the JS section of `sys_func_defs[]` and the runtime import table is in scope.
- Changes to method dispatcher internals (`js_string_method` etc.); only their *usage* is in scope.

---

## Appendix A — Data sources

- Registry: `lambda/sys_func_registry.c` (2,751 LOC). Static count of `{"js_*"` lines: 547.
- Lowering: `lambda/js/js_mir_*.cpp` (12 files, 37,981 LOC). Static count of `"js_*"` quoted-string references: 434 distinct names across 920 `jm_call_N` callsites.
- Call-arity distribution: `jm_call_1` 348, `jm_call_2` 201, `jm_call_0` 167, `jm_call_3` 145, `jm_call_4` 54, `jm_call_5` 3, `jm_call_6` 2.
- Emission helper: `js_mir_calls_boxing_types.cpp::jm_ensure_import` (proto cache keyed by `name#r%d#n%d#a%d#<arg types>`).

These numbers are the static baseline. Phase 0 §1.3 supersedes them with measured runtime emission counts; all sizing decisions in §2–§4 are taken from the measured data, not the static count.
