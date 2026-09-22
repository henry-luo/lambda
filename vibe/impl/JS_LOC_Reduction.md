# Lambda Impl Proposal: JS Runtime LOC Reduction, structural round (JLS)

- **Date:** 2026-09-22
- **Status:** PROPOSAL rev 2, approved to implement (USER, 2026-09-22).
  Nothing below is implemented yet; every LOC figure is a census of the tree
  at `7a06981f8` (2026-09-21) plus an estimate of what a named consolidation
  would credit. Rev 2 records the two user rulings in §1.1 (MVP excluded,
  Node code may move to `lambda/module/`) and the Node support floor in §1.2,
  and adds JLS-15 (dead Node host namespaces, §4). Rev 3 (USER, 2026-09-22)
  withdraws JLS-14 (the regex pattern detectors are required for regex
  correctness and stay) and makes a full pass of test262 and `test_js_gtest`
  a hard gate for every batch (JLSR4, JLSR5).
- **Goal set by the user:** remove at least 10% of the JS runtime's LOC
  without a performance regression, preferring unification with and reuse of
  the Lambda runtime (AST builder, type inference, interpreter, MIR emission)
  and unification of similar JS code paths.
- **Authority:** **D8.6.4v2** (what counts as LOC reduction; no credit for
  moves out of `lambda/runtime` + `lambda/js`, formatting, or comment
  stripping), **D8.1.3v12** (JS keeps its own parser, AST walker and
  activation records), **D8.2.5v2** (`AstNode.type` is the effective-type
  authority; `AstIndex::facts` is to be retired), **D8.2.1–D8.2.6** (shared
  indexed compiler substrate), **D1.2v2–D1.5v2** and **D1.3v3** (the MVP guest
  profile and what a guest must reuse), **D5.3.4**, **D6.2.2v2**. Working
  designs: [`Lambda_Design_JS_Unified.md`](../Lambda_Design_JS_Unified.md)
  §1.2–§1.3 (phase-local LOC conservation, what counts as retirement), §3.4,
  §3.7–§3.9, §7 (rejected directions);
  [`Lambda_Proposal_JS_Unify_P7.md`](../Lambda_Proposal_JS_Unify_P7.md)
  (U-A withdrawn, U-E unstarted); [`jube/JS_Tune14.md`](../jube/JS_Tune14.md)
  §3.3–§3.4 (shared slices in flight, duplicate-work rules). The formal spec
  wins on disagreement.
- **Predecessor:** the JLR clone-extraction ledger (retired at `e3063f604`;
  recover with `git show e3063f604^:vibe/Lambda_Impl_JS_LOC_Reduce.md`). It
  landed −1,214 and measured that the small-clone vein is exhausted: only 20
  positive-net groups totalling 48 lines remain. This proposal therefore does
  not re-run that census; it targets structure.

---

## 0. Summary

| | lines |
|---|---|
| `lambda/js/**` at `7a06981f8` (`temp/js_loc.sh`: C/C++, excluding generated regex tables, `.inc`, `test_shim/`) | 149,348 |
| minus `lambda/js/mvp/` (excluded by JLSR1, not in the final release) | −10,330 |
| **metric baseline** | **139,018** |
| **10% target** | **13,902** (end at ≤ 125,116) |

**Three sources of reduction, in order of certainty:**

1. **Dead Node host code (JLS-15, credited).** The host still builds
   namespaces for `domain`, `diagnostics_channel`, `repl`, `cluster`,
   `async_hooks` and several `internal/*` specifiers, but nothing calls those
   builders and `require()` of each specifier fails today (§1.2). Only 21 of
   the 201 functions in the Node section of `js_runtime.cpp` are referenced
   from outside it. Deleting the unreachable closure is ordinary dead-code
   removal and earns full D8.6.4v2 credit.
2. **Consolidation (JLS-1 … JLS-13, credited).** −4,450 / −6,190 / −8,460
   (low/mid/high). JLS-14 is withdrawn (JLSR4).
3. **Node boundary move (§6, JLSR2).** Live Node code that is not part of the
   §1.2 floor moves to `lambda/module/node_*`: Buffer and the Node-only
   parts of process I/O. This counts for the user's metric but not for
   D8.6.4v2 credit.

Together these reach 10% in the high case (−14,530, 10.5%). The mid case
(−11,590, 8.3%) is about 2,300 lines short; §5 says where the remainder
comes from.

**Why consolidation alone does not reach 10%.** The JS runtime has already had
three rounds that removed the cheap duplication (JLR −1,214; the P5/P6
unified compiler boundary; the 2026-09-16 Node module split, which took the
tree from 174,829 to 149,348). What remains duplicated is *mechanics inside
semantically distinct walkers*, and D8.1.3v12 / JS_Unified §7.1–§7.2 rule
out merging the walkers themselves. Value-level reuse is already high: JSON
parse, date arithmetic, shortest-double formatting, base64 and the RE2 glue
all call Lambda or `lib/` code today (§2.3).

**Tune14 coordination.** Tune14 T14-2/T14-3/T14-5 are consolidating the
raw/boxed emission facades and the guarded-access leaves. JLS-5, JLS-7 and
JLS-9 are prescribed by Tune14 §3.3 as two-client extractions and land
inside those packages rather than in parallel with them.

---

## 1. Metric and accounting rules

### 1.1 User rulings (2026-09-22)

- **JLSR1 — MVP is out of scope.** `lambda/js/mvp/` is not part of the
  final release. It is neither deleted nor edited by this work, and it is
  excluded from the metric. (Its D1.2v2–D1.5v2 status is unchanged.)
- **JLSR2 — Node code may move to `lambda/module/`.** Node-specific
  implementation may leave `lambda/js` for the existing Node module source
  directories (`lambda/module/node_core/`, `node_fs/`, `node_net/`,
  `node_crypto/`, `node_zlib/`), or a new `lambda/module/node_<name>/`
  directory registered in `build_lambda_config.json` `node_modules`. Such a
  move counts toward the user's metric. It is recorded in its own ledger
  column, because D8.6.4v2 gives a move no consolidation credit.
- **JLSR3 — the Node support floor stays (§1.2).** `lambda js` follows the
  Node runtime convention and the benchmark JS corpus depends on it. No
  move, deletion or consolidation may break that floor.
- **JLSR4 — the regex pattern detectors stay.** The pattern-shape detectors
  and literal fast paths in `js_runtime.cpp` (`:15211–16425`,
  `:16676–16839`) are required for regex correctness. They are not a
  deletion or consolidation target in this plan; JLS-14 is withdrawn. Other
  packages must not change their behaviour or bypass them.
- **JLSR5 — full pass, not A/B.** Every batch must leave **all** test262
  tests and **all** `test_js_gtest` tests passing. A failure that also fails
  on the pristine tree is not waived by an A/B comparison; it must be fixed at
  its root cause (CLAUDE.md rules 1 and 18) before the batch can land, or the
  baseline must be shown to be fully green first (§1.4).

### 1.2 Node support floor

`lambda js script.js` runs a script the way `node script.js` would for the
surface below. The benchmark corpus (`test/benchmark/**/*.js`) is the
concrete consumer; the census at `7a06981f8` counts these uses:

| surface | uses in benchmark JS | owner today |
|---|---|---|
| `process.stdout.write` / `process.stderr` | 163 / 5 | host (`js_globals.cpp` Process I/O) |
| `performance.now()` | 131 | host |
| `console.log` / `console.error` | 61 / 2 | host |
| `setTimeout` (and the event-loop drain) | 18 | host (`js_event_loop.cpp`) |
| `process.argv`, `process.exit`, `process.exitCode`, `process.mainModule` | 14 / 3 / 1 / 2 | host |
| `require('fs')`, `require('path')`, `require('module')`, `require('child_process')` | 10 / 6 / 2 / 1 | node-fs, node-core, host `module` bridge |
| CommonJS `require` of relative files, `module.exports`, ES modules | pervasive | host |

Rules that follow from the floor:

1. **The floor must work when `lambda js` runs with its standard module
   set.** Code backing the floor may move into a `lambda/module/node_*`
   directory only if that module is loaded on every `lambda js` run the
   benchmarks use (node-core and node-fs are today; verify with the probe
   below before moving). Otherwise the code stays in the host.
2. **Globals that exist without any `require` stay host-owned** unless
   node-core already installs them: `process`, `console`, timers,
   `performance`, `globalThis`, `queueMicrotask`, `structuredClone`,
   `TextEncoder`/`TextDecoder`, `URL`, `atob`/`btoa`, `AbortController`.
   Many of these are Web-platform globals needed by the DOM path too.
3. **Every batch that touches Node code runs the benchmark-corpus gate**
   (§7): every JS benchmark row produces the same output as before.
4. Specifiers outside the floor (`domain`, `cluster`, `repl`,
   `diagnostics_channel`, `async_hooks`, `internal/*`) have no floor
   obligation. They may move, and if they are unreachable they may be deleted
   (JLS-15).

Reachability probe used for rule 1 and rule 4:

```bash
echo "const m=require('domain'); console.log(typeof m)" > temp/probe.js
./lambda.exe js temp/probe.js
```

At `7a06981f8`, `buffer`, `fs` and `path` resolve. `domain`,
`diagnostics_channel`, `repl`, `cluster` and `async_hooks` do not: the
loader falls through to "Error opening file: domain".

### 1.3 Metric

- **Metric:** `temp/js_loc.sh` with `lambda/js/mvp/` excluded (JLSR1),
  physical lines. The script needs `! -path '*/mvp/*'` added.
- **Baseline at `7a06981f8`:** 149,348 − 10,330 (mvp) = **139,018**.
  **10% target: 13,902 lines**, i.e. at most **125,116** after the work.
- Report the same number before and after every batch.
- **Credit rules (D8.6.4v2, JS_Unified §1.2–§1.3):** each batch has a
  non-positive governed-scope delta; new common code is funded by deletion in
  the same batch; a ledger row names the retired symbols and the surviving
  authority and records removed / added / credited net. No credit for moves
  out of `lambda/js`, comment or blank-line removal (CLAUDE.md rule 19),
  reformatting, feature flags, forwarding wrappers, or weakened tests.
- **Semantic gates (JS_Unified §5.3, JLSR5), every batch:**
  1. `make build` + `make build-test` (`make build` alone does not rebuild
     `test/*.exe`).
  2. `./test/test_js_gtest.exe`: **every test passes.** No exclusion
     filter, no pristine A/B waiver.
  3. `make test262-baseline`: **40,261/40,261 fully passing**, zero
     failures, zero non-fully-passing tests, zero retries, no crashed or
     killed batches, no "not found in batch results" rows (such a run is
     invalid: recreate `temp/` and re-run).
  4. `make test-lambda-baseline` (the only gate running memtrack).
  5. `test_js_mir_emission_gtest`, `test_js_opt_gtest`,
     `test_mir_emission_gtest` where lowering changed.
  `test_js_test262_gtest` and the test262 runner are never modified to mask
  a failure (CLAUDE.md rule 18).
- **Performance gates:** §7.

### 1.4 Gate baseline (must be green before batch 1)

JLSR5 requires a full pass, so the starting tree must itself be green;
otherwise no batch could satisfy the gate. Status at `7a06981f8`:

| suite | status |
|---|---|
| `test_js_gtest` | **green: 466/466 passed** (4 suites, fresh `make build-test`, 2026-09-22). The `hljs_highlight` failure recorded by the JLR ledger no longer reproduces. |
| `make test262-baseline` | **green: 40,261/40,261 fully passing**, 0 non-fully-passing (re-run on the unchanged tree as the first step of batch 1, 2026-09-22). |

Any pre-existing failure found here is fixed at its root cause as batch 0,
before any LOC work, and recorded in Appendix B.

---

## 2. Census: where the 149,348 lines are (139,018 without `mvp/`)

### 2.1 By area

| area | files | lines | notes |
|---|---|---|---|
| builtin runtime (`js_runtime.cpp`) | 1 | 36,538 | 647 functions, 314 intrinsic-body macros, 179 `#define`s; section census in §2.2 |
| globals / object model (`js_globals.cpp`) | 1 | 18,086 | 431 functions; section census in §2.2 |
| MIR lowering (`js_mir_*.cpp/.hpp`, `transpile_js_mir.cpp`) | 15 | 31,455 | 791 `jm_*` functions; 1,036 raw `MIR_new_*` sites vs 3,897 in `transpile-mir.cpp` |
| MVP guest profile (`mvp/`) | 8 | 10,330 | private `MvpValue` ABI, debug-only CLI selector; **excluded from the metric and untouched** (JLSR1) |
| AST interpreter (`js_interp.cpp`, `js_interp*.h*`) | 3 | 6,578 | 211 functions, 108 node cases; separate walker by D8.1.3v12 |
| front end (parser/, `js_c_parser.cpp`, `js_c_ast_helpers.*`, `js_direct_scope.cpp`, `js_scope.cpp`, `js_early_errors.cpp`, `js_ast*.{hpp,cpp}`, `js_module_ast_prebuild.cpp`, `js_emit_ast_dump.cpp`) | 13 | 13,150 | AST is already the core AST (typedefs onto `Ast*Node`, 8 JS-only kinds) |
| value / property / function kernels (`js_runtime_value.cpp`, `js_props.*`, `js_property_attrs.*`, `js_object_meta.*`, `js_runtime_function.cpp`, `js_function.hpp`, `js_class.h`, `js_coerce.*`) | 12 | 8,570 | |
| runtime state (`js_runtime_state.*`, `js_runtime_builtin_registry.cpp`, `js_builtin_catalog.*`, `js_runtime.h`, `js_runtime_internal.hpp`) | 7 | 7,283 | catalog: 1,168 rows (459 ids, 441 methods, 93 globals, 58 ctor targets) |
| regex (`js_bt_regex.*`, `js_regex_wrapper.*`, `js_regexp_compile.*`, `js_regex_router_scanner.*`, `js_regex_generated_properties.cpp`) | 9 | 4,000 | plus 5,049 lines of regex sections inside `js_runtime.cpp` |
| typed arrays + Buffer (`js_typed_array.*`, `js_buffer.cpp`, `js_typed_array_carrier.hpp`) | 4 | 5,927 | Buffer is Node's; TypedArray is ES |
| event loop, host, DOM realm, services, profiling (`js_event_loop.*`, `js_host_hooks.*`, `js_dom_*.cpp`, `js_fs_service.*`, `js_util_service.cpp`, `js_exec_profile.*`, `js_well_known_names.*`, misc headers) | 16 | 7,421 | event loop already runs on the shared `lambda_uv_loop()` |

### 2.2 Section census of the two largest files

`js_runtime.cpp` (section markers, lines per section):

| lines | section | what it really holds |
|---|---|---|
| 7,731 | "ES6 Proxy" (mislabeled, `:1217`) | proxy traps, all `js_array_generic_*` algorithms, typed-array sort, class instance fields / private brands, constructor policies and intrinsic ctor bodies |
| 4,976 | "Function Functions" (`:10120`) | iterator protos, promise-with-constructor helpers, timers, AST-tier bridges, body-entry selection, RegExp symbol-method forwarders, object `toString`, the ~300 one-line intrinsic bodies |
| 4,154 | Regex support (`:15096`) | RE2 compile cache, pattern-shape detectors (kept, JLSR4), literal fast paths, RegExp object construction |
| 2,510 | Array Method Dispatcher | `js_indexed_intrinsic_algorithm`, shared Array/TypedArray callback kernels (already unified; single implementation per method, verified) |
| 2,311 | "async_hooks module stub" (`:33732`) | domain, cluster, repl, `internalBinding`, `uv.errname`, AsyncLocalStorage, async_hooks |
| 1,713 | Map/Set | one `js_collection_*` kernel for Map/Set/WeakMap/WeakSet (already unified) |
| 1,709 | Promise runtime | |
| 1,080 / 815 | String dispatcher / `replace` | table-driven `JsStringIntrinsicOp`, no `strcmp` chains |
| 979 / 784 | Generator runtime / lazy iteration | |
| 967 | diagnostics_channel | Node module semantics |
| 911 | Intl (compact) | |
| 895 | RegExp `@@match/@@replace/@@search/@@split` | |

`js_globals.cpp`:

| lines | section |
|---|---|
| 2,518 | Process I/O (`process.*`, stdio, tty) |
| 1,517 | with-scope stack and the global get/set/define family (§4 JLS-9) |
| 1,330 | constructor cache / intrinsic prototype resolution (already realm-slot backed: `js_constructor_cache_at` is a `js_realm_intrinsic_slot` alias) |
| 943 | Reflect.construct and constructor plumbing |
| 854 | MessagePort / MessageChannel (Web platform, stays) |
| 872 | test262 harness natives, debug-only (`:9666–10538`) |
| 557 / 457 / 277 / 249 | Object.keys / getOwnPropertyDescriptor / defineProperty / hasOwnProperty (§4 JLS-8) |
| 470 / 335 | JSON.stringify / JSON.parse (parse already calls `parse_json_to_item_strict`) |

### 2.3 Reuse that already exists (do not re-propose)

- AST: every core-shaped JS node is a typedef of the core node
  (`js_ast.hpp`); binding identity is consumed through `ast_index_binding_id`
  / `ast_index_binding`; `js_ast_children.cpp` covers only the 8 JS-only kinds
  and delegates the rest to `ast_visit_core_children`.
- MIR: `MirRootBinding`, `MirEnvBinding`, `MirImportEntry`, `MirValue`
  demand lowering, `em_element_address`, `em_return_shape` / companion
  transport, `em_hoist_loop_scalar_calls` are shared; JS calls 40 of the 174
  functions in `mir_emitter_shared.hpp`.
- Values: `js_double_to_string` → `lambda_finite_double_to_shortest`;
  `JSON.parse` → `parse_json_to_item_strict`; Date → `datetime_days_from_civil`;
  Buffer base64 → `lib/base64.h`; BigInt → `bigint_from_uint64`; regex →
  `lib/re2_glue.hpp`; timers → `lambda_uv_loop()`; return contracts →
  `lambda_type_union_normalized`.
- JLR batches 1–7: `js_c_take_arity`, `JS_ARRAY_FOREACH`,
  `JS_ENV_OR_UNDEFINED`, `js_alloc_env1/2/3`, the three intrinsic-body
  generics, iterator-proto and props-query macros.

---

## 3. Ruled out, and why

| direction | ruling | consequence for this proposal |
|---|---|---|
| Delete, shrink or refactor `lambda/js/mvp/` (10,330 lines) | JLSR1: not in the final release, excluded from this exercise; its D1.2v2–D1.5v2 status is unchanged | Not touched, not counted. JLS work must not edit `mvp/` sources even where they include `js_ast.hpp` or `ast-core.hpp`; if a shared header change breaks the MVP build, fix the shared side |
| One semantic interpreter for Lambda and JS | JS_Unified §7.1; D8.1.3v12 | `js_interp.cpp` stays. Only the execution shell (P7 U-E) is shareable (JLS-3) |
| One parser / CST | JS_Unified §7.2; D8.1.1v5 | `parser/js_parser.c` and the reduction protocol stay; JLS-4 only tables the sink |
| A shared structural lowering driver over AST tags | P7 U-A withdrawn (§2.1a: shared tags do not share child layouts); Tune14 §3.4 says do not resurrect it | Nothing below proposes a tag-driven common lowering |
| Removing test262 harness accelerators (872 debug-only natives, preamble/batch/snapshot machinery ≈ 650 lines) | JS_Unified §1.3: weakening the Test262 runner does not satisfy the gate | Not proposed |
| Moving code to another directory for D8.6.4v2 credit | D8.6.4v2; JS_Unified §7.6 | Moves to `lambda/module/node_*` are allowed by JLSR2 and count for the user's metric, but are ledgered separately with zero consolidation credit (§6) |
| Deleting comments/blank lines, reformatting | CLAUDE.md rule 19; D8.6.4v2 | Never |

---

## 4. Work packages

Every row: evidence in the tree, the surviving authority after the change,
estimated physical lines removed / added / credited net (low–high), the
performance class, and its gate. "MIR-identical" means the finalized MIR for
the complete `test_js_gtest` + test262 corpora is byte-identical before and
after (the `test_js_mir_emission_gtest` goldens plus a full-corpus dump diff),
which makes a compiler-side change performance-neutral by construction.

### Tier A — hygiene, no semantic surface

**JLS-1 Declaration hygiene.** `lambda/js/*.cpp` carry 1,078 single-line
`extern` declarations naming 939 distinct symbols; **720 of those symbols are
already declared in a header** (749 lines), the rest (219) are declared nowhere
and are re-typed per file (`js_runtime.cpp` 421, `js_globals.cpp` 313,
`js_typed_array.cpp` 72). There are also 219 forward `static` prototypes that
exist only because the definition sits below its first use.
*Change:* delete the 749 header-shadowing externs and include the owning
header; put the 219 header-less symbols in the module header that owns their
definition (one line each, replacing 1–6 copies); reorder the non-recursive
forward statics.
*Authority:* the module headers (`js_runtime.h`, `js_runtime_internal.hpp`,
`js_mir_internal.hpp`, `js_props.h`, …).
*Estimate:* removed 749 + ~120 statics, added ~50 includes/declarations →
**−650 / −820 / −900**. Perf: none. Gate: the §1.3 semantic gates.

**JLS-2 Generic AST dump.** `js_emit_ast_dump.cpp` (438 lines): a 150-case
kind-name switch (must stay; a name table cannot shrink honestly) and
`emit_js_dump_node`, a 231-NLOC per-kind child switch that re-describes the
child edges `js_ast_children.cpp` and `ast_visit_core_children` already own.
*Change:* dump = name table + generic child walk (`js_ast_visit_children` for
JS-only kinds, `ast_visit_core_children` for the rest) + the few kinds with
extra scalar fields.
*Authority:* the child catalogs.
*Estimate:* **−180 / −220 / −260**. Perf: none (debug tooling). Gate: the
`test_js_gtest` dump-based cases must stay byte-identical (dumps are used as
goldens).

### Tier B — duplicate drivers and passes (JS_Unified §1.3: "duplicate compile/link/cleanup paths")

**JLS-3 One JS compile driver and one execution shell (P7 U-E).**
Twenty entry functions, 1,564 NLOC, each re-sequence parse → validate →
analyze/plan → lower → finalize → link → execute with local variations:
`transpile_js_to_mir_core_profile_len` (411), `transpile_js_module_to_mir`
(289), `js_builtin_eval_execute` (348), `js_new_function_from_string_kind`
(206), `instantiate_js_preamble` (68), `execute_compiled_js_in_current_realm`
(51), and eleven thin `transpile_js_to_mir_*` / `compile_js_mir_*` wrappers.
The AST tier adds its own shell (`js_interp_execute_script`,
`_execute_es_module_script`, `_load_es_module`, `_instantiate_es_module`, …:
10 functions, 255 NLOC) and `js_mir_execute_{ast_script,ast_module,
retained_ast_script}` re-wrap it. Six MIR load/link call sites exist in
`lambda/js` against one owner in `runner.cpp`.
*Change:* one `js_compile_unit(JsCompileRequest*)` whose request carries the
goal (script / module / direct eval / indirect eval / `Function` body /
preamble / test262-native / TypeScript) and preamble seed; one
`js_execute_unit` owning prepare → bind → activate → link → execute →
finish-turn → drain with the realm-init / loop-init / turn hooks U-E names;
the wrappers become request constructors or disappear. The AST backend keeps
its walker and becomes one goal of the same shell.
*Authority:* the new driver pair; `runner.cpp` for link/execute mechanics.
*Estimate:* removed ~1,100, added ~350 → **−450 / −600 / −800**. Perf:
compile-time only; gate MIR-identical + `utils/capture_ast_tune_timing.sh
--suite js` median not worse than base. Coordinate with Tune14 T14-6
(cache/prebuild lifetime), which touches the same entry points.

**JLS-4 Table the reduction sink; let the direct-scope pass delegate.**
`js_c_parser.cpp` (2,101 lines) is the sink for the C parser's reduction
protocol: `js_c_reduce` is a 942-NLOC, 61-case switch in which most cases are
"take N children with arity check, call `build_js_X_from_children`". The
protocol itself stays (D8.1.3v12; it has three consumers in `test/`).
`js_direct_scope.cpp` (893 lines) reconstructs the binding graph after
parsing with 35 `direct_walk_*` functions, one per node family, although its
own header comment and `js_ast.hpp` say a walker should keep only the cases
it cares about and delegate the rest to `js_ast_visit_children`.
*Change:* a `{kind, form} → {builder, min_arity, max_arity}` table for the
uniform reductions (literal, identifier, unary, binary, call, member, array,
object, sequence, conditional, spread, statement kinds); keep hand-written
cases only where child shapes differ. In `js_direct_scope.cpp`, keep the
binding-policy cases (function, class, var/let/const, patterns, catch, for
heads, switch, import/export) and route every other kind through the child
catalog.
*Authority:* the reduction table + `js_c_ast_helpers.cpp`; the child catalog.
*Estimate:* **−350 / −550 / −800**. Perf: parse-time only; gate
`test_js_c_parser_gtest`, `test_js_parser_benchmark_gtest` (no slowdown),
MIR-identical.

### Tier C — compiler mechanics shared with the Lambda substrate

**JLS-5 Numeric-fact propagation mechanics (Tune14 §3.3 slice A).** JS-side
inference is 3,128 NLOC across `js_mir_function_collection_class_inference.cpp`
and `js_mir_calls_boxing_types.cpp`: parameter inference from call sites
(`jm_infer_param_types`, `jm_infer_indexed`, seven `jm_infer_*direct_alias*`
helpers), return inference (`jm_infer_return_type`, already joining through
`lambda_type_union_normalized`), numeric-return dependency propagation
(thirteen `jm_numeric_return_*` functions), the P9 float-widening pre-scan
(`jm_prescan_float_widening` + the `widen_to_float` name set), and native
return classification. Lambda owns the same mechanics under other names:
`infer_param_types_batched`, `mir_callsite_join_*`, `infer_return_type`,
`mir_prewiden_loop_bindings`, `mir_widen_binding_to_any`. Both walk the same
`AstIndex` binding / use / def relationships. Tune14 §3.3 A.2 prescribes
exactly this: "if both profiles need the same propagation mechanics, extract
those mechanics and replace the corresponding existing core path in the same
slice", while A.3 keeps the *joins and admission rules* language-owned
(Number guard + F64 for JS; integer default, nullable contracts and exact
call-edge policy for Lambda). D8.2.5v2 keeps facts on `AstNode.type` and the
declaring node.
*Change:* one binding-fact propagation engine (worklist over the index's
use/def edges, alias tracking, loop pre-scan walk, call-edge argument
collection) with a profile hook for the lattice; JS deletes its walkers and
keeps its rules. Land it as T14-2 work, not beside it.
*Authority:* the shared engine; `Type*` operations in `type_contract.cpp`.
*Estimate:* removed ~1,400, added ~350 → **−700 / −1,000 / −1,400**. Perf:
compiler-side; gate MIR-identical over both corpora (any lane change shows up
as a MIR diff), then the Tune14 §5.2 paired-release protocol on the 63-row
JS corpus. Highest-value and highest-coordination item in this proposal.

**JLS-6 Closure-capture analysis mechanics.** Inside
`js_mir_analyze_and_plan` (1,765 physical lines, the largest function in
`lambda/js`), phases 1.5, 1.7, 1.7.5, 1.7b, 1.7c and 1.7d
(`js_mir_module_batch_lowering.cpp:1930–2801`, ~870 lines) compute which
binding each nested function captures, transitive captures, shared scope
environments, and parent-env reuse; ~60 `jm_*capture*` / `jm_*scope_env*`
helpers serve them. Lambda computes the first half of that
(`analyze_captures`, `add_capture`, `collect_captures_from_node`,
`mark_capture_mutable`) over the same index, and the index already carries
`interp_capture_owner` / `interp_capture_slot` (`ast-core.hpp:790–794`).
*Change:* one capture-set computation (binding → set of capturing functions,
written-after-capture flag, first-capture position) published on the index
during bind; JS keeps its environment-layout policy (scope envs, per-iteration
cells, TDZ, parent links) as consumers of that fact. Split the remaining
phases of `js_mir_analyze_and_plan` into named pass functions while there;
that is not credited but is required by the CompilerPassManager it already
registers with (`:3790–3806`).
*Authority:* the shared capture fact on `AstIndex`.
*Estimate:* **−300 / −450 / −650**. Perf: compile-time; gate MIR-identical.

**JLS-7 Emitter stacks and root-slot helpers.** `js_mir_hashmap_scope_utils.cpp`
(1,006 NLOC) owns var-scope stacks, loop-label / try-context / with / for-of
iterator stacks, resumable-local slots, GC root-slot bookkeeping and
register/label allocation; twenty of those helpers have a same-named twin in
`transpile-mir.cpp` (`push_scope`/`pop_scope`, `find_var[_by_binding]`,
`create/update_gc_root_slot`, `should_gc_root_var`, `module_slot_load/store_item`,
`ensure_import`, `register_local_func`, `new_reg`/`new_label`, `box_float[_cold]`,
`bits_double`, `uext8`). Individually small (3–25 NLOC each, ~300 NLOC on the
JS side), they are the reason `JsMirTranspiler` carries its own copies of the
stacks `MirEmitter` already models for frames, roots and env bindings.
*Change:* promote the generic stack/scope/root-slot mechanics into
`mir_emitter_shared.hpp` as the second client of the frame/root machinery
that already lives there; keep `jm_set_var` (72 NLOC, JS hoisting semantics)
and `jm_function_value` (146) JS-owned. Overlaps Tune14 T14-3 slice B
(`em_element_address` family); land the storage-access part there.
*Authority:* `mir_emitter_shared.hpp`.
*Estimate:* **−250 / −350 / −500** (Lambda-side deletions are uncredited).
Perf: compile-time; gate MIR-identical + `test_mir_gc_stress_gtest`.

### Tier D — JS runtime kernel unification (runtime-visible, needs paired release runs)

**JLS-8 One [[DefineOwnProperty]], one [[GetOwnProperty]], one OrdinaryOwnPropertyKeys.**
55 functions / 2,683 NLOC across `js_globals.cpp`, `js_props.cpp`,
`js_property_attrs.cpp`. Two define-property kernels:
`js_object_define_property` (`js_globals.cpp:7863`, 228 NLOC) with its five
`js_define_property_validate_*` helpers, `_collect_existing_state` and
`_apply_validated_descriptor` (~560 NLOC together) versus
`js_define_own_property_from_descriptor_impl` (`js_props.cpp:974`, 180 NLOC,
6 callers). Two descriptor readers: `js_object_get_own_property_descriptor`
(294 NLOC) versus `js_get_own_property_descriptor_impl` + `js_ordinary_get_own_ex`
(105). Six own-keys entry points (~770 NLOC): `js_object_keys` (130 NLOC, 15
callers), `js_object_get_own_property_names` (140, 9), `js_for_in_keys` (129,
3), `js_map_own_string_keys` (91, 4), `js_reflect_own_keys` (61, 14),
`js_property_ops_own_property_names` (46, 2), plus the array-companion,
typed-array and Error key appenders.
*Change:* one ValidateAndApplyPropertyDescriptor in `js_props.cpp` (the spec's
algorithm, with the array-exotic and companion-index cases as explicit
arms), one OrdinaryGetOwnProperty, one OrdinaryOwnPropertyKeys with a filter
mask (string / symbol / enumerable-only / index-first / include-companion) and
thin callers.
*Authority:* `js_props.cpp`.
*Estimate:* **−700 / −900 / −1,200**. Perf: property definition is hot in
deltablue/richards/typescript rows and in `bench_property.js`; the shaped-slot
and companion fast paths must survive as the first arms of the kernel. Gate:
test262 (this is the most conformance-sensitive area), `bench_property.js`,
Tune14 §5.2 paired release on the 63 rows.

**JLS-9 One reference-resolution kernel for global and `with` access.**
`js_globals.cpp:14465–15986` (1,517 lines, ~60 functions) implements
resolve-then-operate for globals and `with` objects as separate entry points
per operation and per pre-check: `js_get_global_property{,_after_with_lookup,
_strict,_reference}`, `js_set_global_property{_impl,_after_with_lookup,
_after_with_lookup_impl,_strict_prechecked,_var_fast}`,
`js_define_global_{property_v,var_property,var_property_fast_absent,
var_properties_bulk_absent,eval_var_property,function_property}`,
`js_global_lexical_{refresh,find,binding_exists,get_or_fallback,set_if_exists,
declare}`, `js_probe_with_binding{,_from}`, `js_capture_with_binding{,_from}`.
The MIR identifier read path (`jm_emit_identifier_read`, 288 NLOC) and the
assignment path carry their own parallel fallback chains (module const →
preamble → Annex B → global), which is how the `b52021681` read/write
asymmetry regression happened.
*Change:* one `js_resolve_reference(name, mode) → {kind, base, key, cell}`
used by get / set / define / delete / typeof, and one MIR-side resolver used
by both the read and the write lowering. Keep the two hot fast entries
(`js_get_global_property`, `js_set_global_var_property_fast`) as inline
wrappers over the kernel.
*Authority:* the resolver.
*Estimate:* **−300 / −400 / −600**. Perf: global reads dominate untyped
benchmark rows; gate paired release + MIR-identical for the lowering half.
Overlaps Tune14 T14-5 (ordinary named reads); sequence after it.

**JLS-10 Keyed-set and native-method installer family.** 111 distinct
`js_{set,get,put,define,install}_*` Item-returning entry points are defined
across four files (53 lizard-visible ones total 1,588 NLOC). Beyond JLS-8/9
the residue is three "install a native method" variants
(`js_set_native_method`, `js_install_native_method`,
`js_globals_set_native_method`), and the `js_set_key_cstr` (206 uses) /
`js_set_key_default` / `js_set_native_key` / `js_set_name_key` /
`js_set_key_policy` spellings of one keyed data-property write.
*Change:* one installer taking a `JsBuiltinMethodSpec` (the catalog row type
already exists) and one keyed-write kernel with a policy argument.
*Estimate:* **−150 / −250 / −400**. Perf: realm initialisation only; gate
test262 + cold-start row of Tune14 §5.2 (startup must not grow).

**JLS-11 Exotic-object carriers, GC hooks, lazy state records.** Six
`Js*MapCarrier` structs and fifteen per-kind `*_gc_trace` / `*_heap_destroy`
hooks (generator, iterator, collection, regex, promise vmap, async frame,
suspended activation, function) repeat the same "Map header + payload
pointer + trace + destroy" shape; `js_runtime_state.cpp:424–513` has six
near-identical lazy `js_*_state_ensure` allocators.
*Change:* one native-payload carrier with a per-kind `{trace, destroy}`
table indexed by the existing `map_kind` / `js_meta`; one templated lazy
record allocator.
*Estimate:* **−200 / −300 / −400**. Perf: GC trace dispatch stays a table
lookup; gate `test_mir_gc_stress_gtest`, forced-GC test262 spot runs.
Warning: touching `JsRuntimeState` layout requires rebuilding the
`modules/node-*.dylib` binaries on both sides of any A/B (they bake in its
offsets).

**JLS-12 Generator signal, creation and async-hooks stamping.**
`js_gen_{,return_,throw_}signal` × `{make, is, value}` are nine functions for
one tagged signal; `js_generator_create_{current,mir,ast}` are three creators;
`js_async_hooks_{enter,restore,get_current,stamp}_resource` are reached from
both the promise runtime (`:29903–`) and the node section (`:33733–`) through
separate wrappers.
*Estimate:* **−120 / −200 / −300**. Perf: generator resume path; gate
`test_js_opt_gtest` generator cases + paired release (fannkuch/richards-style
rows do not use generators; the risk is async rows).

**JLS-13 UTF-16 index helpers onto `lib/utf.h`.** Fourteen JS-local UTF-16
walkers (`js_utf16_idx_to_byte`, `js_utf16_len`, `js_utf16_code_unit_at`,
`js_utf16_index_from_byte`, `js_str_substring_utf16`,
`js_string_expand_utf16_subject`, `js_next_utf16_code_unit`,
`js_compare_strings_utf16`, three lone-surrogate scanners, …) sit next to
`lib/utf.h`'s `utf8_to_utf16_offset`, `utf16_to_utf8_offset`,
`utf8_to_utf16_length`, `utf16_decode_pair`. JS needs WTF-8 tolerance (lone
surrogates are legal in JS strings), so the lib helpers may need a
surrogate-tolerant variant; JLR13 already showed `utf8_encode` vs
`js_c_wtf8_encode` differ by exactly that line, so unify by adding the
tolerant flag once, not by copying.
*Estimate:* **−100 / −150 / −250**. Perf: string rows (`bench_binop.js`,
levenshtein, string-heavy AWFY rows); gate paired release.

### Withdrawn

**JLS-14 Regex pattern-shape detectors — WITHDRAWN (JLSR4).** Rev 1 and 2
proposed removing `js_runtime.cpp:15211–16425` and `:16676–16839` (1,379
lines) as correctness-neutral accelerators. That premise was wrong: the
detectors are required for regex correctness. They stay, and no package may
alter or bypass them. Estimate 0.

---

### Dead code

**JLS-15 Unreachable Node host namespaces.** After `bc68bf228`, node-core
resolves Node specifiers through `resolve_host_namespace`
(`lambda/jube/jube_registry.cpp`, `jube_host_node_resolve_host_namespace`),
whose table lists only `buffer`, `module`, `url` and `util`. The host still
defines builders for the other specifiers node-core asks for:
`js_get_domain_namespace`, `js_get_cluster_namespace`,
`js_get_repl_namespace`, `js_get_diagnostics_channel_namespace`,
`js_get_async_hooks_namespace`, and the `js_get_internal_*_namespace` family
(`js_runtime.cpp:35250–35991`). None of them has a caller anywhere in
`lambda/`, `test/` or `modules/`, being `extern "C"` is why no
unused-function warning fired, and the runtime probe in §1.2 confirms each
specifier is unreachable. The Node section of `js_runtime.cpp`
(`:32765–36043`, 3,278 lines) defines 201 functions; only 21 are referenced
from outside the section:

- live and kept: `js_async_hooks_{enter,restore,get_current,stamp}_resource`,
  `js_async_hooks_emit_{before,after,destroy,promise_resolve}_resource`,
  `js_async_hooks_{queue_destroy,drain_destroy_queue,after_gc,is_gc_tracker}`
  (promise and event-loop stamping), `js_als_capture_context`,
  `js_als_context_call[_args]` (promise job context),
  `js_get_node_module_namespace`, `js_module_get[_builtin]`,
  `js_is_vm_context_error`, `js_string_equals`, `js_string_items_equal`;
- everything else is reachable only from the dead builders: the whole of
  diagnostics_channel (967 lines), domain, cluster, repl,
  `internalBinding`/`uv.errname`, and the public AsyncLocalStorage /
  `createHook` / AsyncResource surfaces.

*Change:* delete the dead builders; delete every function whose only
references lie inside the deleted set (iterate to a fixed point); keep the
live 21 and their callees; delete their `JsRuntimeState` records
(`js_async_local_storage_state_ensure`, `js_readline_state_ensure` and any
record left with no reader). Remove the now-empty
`NODE_CORE_HOST_NAMESPACE` bridges in `node_core_module.cpp` and their
`provides` entries only for specifiers that already fail, so no working
specifier changes behaviour. Do the same census tree-wide (Appendix A) for
other `extern "C"` roots left behind by `bc68bf228`.
*Method:* mark each candidate `static` in a scratch build with
`-Werror=unused-function` (the release flags already carry it) to let the
compiler prove the closure, then delete.
*Authority:* none needed; the specifiers have no implementation after the
change, as they have none reachable today.
*Estimate:* **−2,000 / −2,400 / −2,800** credited. Perf: none (unreachable
code). Gates: §1.2 floor probe, the benchmark-corpus gate (§7),
the §1.3 semantic gates (full `test_js_gtest` and test262 pass), and a grep that the deleted symbols appear in
no `modules/*.dylib` import table (`nm -u modules/*/*.dylib`).

---

## 5. Budget

Metric baseline 139,018 (mvp excluded); 10% = 13,902.

| source | packages | low | mid | high |
|---|---|---|---|---|
| A hygiene | JLS-1, JLS-2 | −830 | −1,040 | −1,160 |
| B drivers | JLS-3, JLS-4 | −800 | −1,150 | −1,600 |
| C compiler mechanics | JLS-5, JLS-6, JLS-7 | −1,250 | −1,800 | −2,550 |
| D runtime kernels | JLS-8 … JLS-13 | −1,570 | −2,200 | −3,150 |
| dead Node host code | JLS-15 | −2,000 | −2,400 | −2,800 |
| **credited subtotal** | | **−6,450** | **−8,590** | **−11,260** |
| boundary move, uncredited (§6) | JLS-16, JLS-17 | −2,800 | −3,000 | −3,270 |
| **metric total** | | **−9,250 (6.7%)** | **−11,590 (8.3%)** | **−14,530 (10.5%)** |

Reading: the high case clears 10% and the mid case is about 2,300 lines
short of it. The first two batches (§8: JLS-15, hygiene, Buffer
move) are the most certain and deliver roughly −5,000 to −6,000 on their
own. If the metric lands short after the last batch, the remainder comes from
the post-Tune14 second round (§10), not from relaxing the Node floor, the
regex detectors, or the test gates.

---

## 6. Boundary track (JLSR2; counted for the metric, not credited under D8.6.4v2)

Live Node code that is outside the §1.2 floor moves to `lambda/module/`.
Node modules link with `-undefined dynamic_lookup` (macOS) /
`--allow-shlib-undefined` (Linux) / `lambda-host.lib` (Windows), so moved
code may keep calling exported host functions (`js_array_new`,
`js_set_key_default`, the typed-array kernels) directly. The host side must
not call back into a module symbol; it reaches module code only through the
Jube specifier and global-installer tables.

**JLS-16 Move Buffer to node-core.** `js_buffer.cpp` (2,468 lines) is reached
only through `js_get_buffer_namespace`, which the Jube host-namespace table
lists (`jube_registry.cpp:3682`); no `js_buffer_*` function has a caller
outside the file, and node-core already owns the `buffer` specifier and the
`Buffer` global installer (`node_core_buffer_global`).
*Change:* move the file to `lambda/module/node_core/node_buffer.cpp` with a
`node_buffer_namespace()` builder like its siblings; point
`node_core_buffer_namespace` and `node_core_buffer_global` at it; delete the
`buffer` row of the host-namespace table and the `extern` in
`jube_registry.cpp`; add the file to node-core's `source_files`. Any typed-
array helper it used as a `static` inside `lambda/js` is exported through
`js_typed_array.h`, never copied (CLAUDE.md rule 13).
*Estimate:* −2,400 to −2,470 from the metric. Perf: `Buffer` is not used by
the benchmark corpus; gate `require('buffer')`, `Buffer.from/alloc/concat`,
the `test_js_gtest` Buffer cases, and `nm -u` on the new dylib.

**JLS-17 Node-only process surface to node-core.** The "Process I/O"
section of `js_globals.cpp` (`:1218`, 2,518 lines) holds three kinds of code:

| group | NLOC | disposition |
|---|---|---|
| Date (`js_date_*`) and `performance` (`js_performance_*`), 48 functions | 968 | stays: Date is ECMAScript, `performance.now` is the floor |
| argv, exit / exitCode / before-exit, env, versions, stdio write, process object assembly | rest | stays: floor |
| IPC (`js_process_ipc_*`, `send`, `disconnect`, `set_connected`), 26 functions | 358 | moves |
| process event emitter (`on`/`once`/`emit`/`listeners`), 10 functions | 106 | moves, except the `exit`/`beforeExit` delivery the floor needs |
| stdin reading and piping, 8 functions | 95 | moves |
| `process.binding` stub | 16 | moves |

The global `process` object is already assembled through
`jube_specifier_resolve("process")` (`js_globals.cpp:100`), and node-core's
`node_process.cpp` already installs `hrtime`.
*Change:* move the four Node-only groups into `node_process.cpp`'s
installer; the host keeps a minimal exit-event hook.
*Precondition:* node-core is loaded on every `lambda js` run used by the
benchmarks. If any run can execute without it, those runs lose IPC, stdin
and listeners only, never the floor.
*Estimate:* −400 / −600 / −800 from the metric. Perf: startup only; gate the
benchmark-corpus run, the child-process IPC cases in `test_js_gtest`, and
the cold-start figure from Tune14 §5.2.
*Not a move:* splitting Date out of `js_globals.cpp` into its own file is
worth doing for navigation but changes no count.

**Stays in the host** (floor or Web platform): console, timers and the event
loop, `performance`, `module`/`require`, `url`, `util` (node-core bridges to
the host `util` namespace), `structuredClone`, `TextEncoder`/`TextDecoder`,
`atob`/`btoa`, `AbortController`, MessagePort/MessageChannel,
`js_fs_service.cpp` and `js_util_service.cpp` (host services by design), and
the test262 natives (`js_globals.cpp:9666–10538`, harness accelerators; the
test262 runner must not be weakened).

---

## 7. Performance non-regression protocol

- **Release builds only** (`make release`; verify the ~8 MB binary; remember
  `make test-lambda-baseline` overwrites `lambda.exe` with a debug build and
  `make release` deletes `test/*.exe`).
- **Compiler-side packages (JLS-2 … JLS-7):** the primary gate is
  MIR-identical: dump finalized MIR for the complete `test_js_gtest` and
  test262 corpora before and after, diff byte-for-byte (labels excluded as in
  MT7); `test_js_mir_emission_gtest` / `test_mir_ratchet_gtest` must pass with
  zero slack. Then compiler time: `./utils/capture_ast_tune_timing.sh --suite
  js --label <label>`, one warm-up, five runs, median must not rise (the
  D8.6.4v2 protocol). A MIR-identical change cannot regress runtime.
- **Runtime-side packages (JLS-8 … JLS-13):** Tune14 §5.2 verbatim: at least
  11 alternating control/candidate pairs per package on the affected rows and
  representative controls, then the full 63-row JS corpus paired against the
  C14 control before closeout; report `G` (geomean of row ratios) and `T`
  (ratio of sums), win counts, worst loss, peak RSS and cold-start separately.
  Micro-benchmarks `test/js_runtime_bench/bench_{property,regexp,binop,eq_ne}.js`
  for JLS-8/9/13. Acceptance: no row worse than −2% outside noise and
  `G ≥ 0.98`; any row beyond that is investigated, not averaged away.
- **Node floor / benchmark-corpus gate (JLSR3), every batch that touches
  Node code (JLS-15, JLS-16, JLS-17) and every closeout:** run every JS
  benchmark row (`test/benchmark/**/*.js`) with a release `lambda js` before
  and after; stdout and exit status must be byte-identical row for row, apart
  from timing lines the benchmark prints. Also run the §1.2 probe for each
  floor specifier and global (`process.stdout.write`, `process.argv`,
  `process.exit`, `performance.now`, `setTimeout`, `console.log`,
  `require('fs'|'path'|'module'|'child_process')`). A floor failure blocks
  the batch; it is never waived as "Node-only".
- **Module ABI:** after a move, `nm -u modules/<m>/<m>.dylib` must list only
  exported host symbols, and both sides of any A/B are rebuilt (the dylibs
  are not rebuilt by `make build`).
- **One benchmark run at a time**; verify nothing else is running before
  timing; never time under `test262-baseline` load.

---

## 8. Sequencing

Each batch is one review-sized change with a non-positive delta, its own
ledger row and its own gate run. `lambda/js/mvp/` is never edited (JLSR1).
Order by certainty, then by Tune14 conflicts:

1. **Batch 1 — dead Node host code:** JLS-15. −2,000 to −2,800. First,
   because it is deletion of unreachable code with the smallest semantic
   surface; it also shrinks the files every later batch edits.
2. **Batch 2 — Buffer move:** JLS-16, then JLS-17 if its precondition holds.
   −2,800 to −3,270 from the metric. Rebuild node-core; run the floor gate.
3. **Batch 3 — hygiene and Tune14-independent kernels:** JLS-1, JLS-2,
   JLS-11, JLS-12, JLS-13. −1,500 to −2,100. JLS-1 goes after batches 1–2 so
   it does not re-type externs for code that is about to leave.
4. **Batch 4 — property kernels:** JLS-8 and JLS-10. −850 to −1,600.
   Conformance-heavy; run test262 twice (once with forced GC on the affected
   directories).
5. **Batch 5 — drivers:** JLS-3 and JLS-4. −800 to −1,600. After Tune14 T14-6
   lands, since T14-6 edits the same entry points.
6. **Batch 6 — inside Tune14:** JLS-5 as T14-2 slice A, JLS-7 as T14-3 slice
   B, JLS-9 after T14-5. Tune14 §3.3 already prescribes these as two-client
   extractions; running them in parallel would produce the "copied third
   variant" §3.4 forbids.
7. **Batch 7:** JLS-6 (capture facts).

After batches 1–3 the expected metric is −6,300 to −8,170 (4.5–5.9%).
Refresh the metric after every batch and append the row to Appendix B.

---

## 9. Process traps (carried from the JLR ledger and memory)

- A rewrite script must exclude the helper it just inserted (JLR17 recursed
  into its own helper; compiled clean, crashed at run time).
- `temp/` gets deleted by some test targets; a test262 run whose failures say
  "not found in batch results" is invalid. Recreate `temp/` and re-run.
- `make build` does not rebuild `test/*.exe`; run `make build-test` before
  any suite. `make release` deletes `test/*.exe`.
- `make node-regression-gate` is stale in this tree (its `node-*.dylib` live
  under `temp/`); never read its exit status as a verdict.
- `JS_DEFINE_ERROR_CTOR_CALL_BODY` pastes its function name; mechanical
  rewrites that pass the parameter instead fail at link time.
- `log_info` is a no-op in release; instrument with `log_error` or counters.
- New `Input` fields must be hand-initialised (pool-allocated, not zeroed).
- `modules/node-*.dylib` bake in `JsRuntimeState` offsets: rebuild both sides
  of every A/B that touches that struct (JLS-11).
- Check the core before adding a helper to it (JLR13 found the fourth copy of
  `bigint_from_uint64` only through a redefinition error).
- `extern "C"` hides dead code from `-Wunused-function`: JLS-15's builders
  sat unreferenced since `bc68bf228`. Prove a deletion closure by making
  candidates `static` in a scratch release build, not by grep alone.
- A Node module calls host symbols through dynamic lookup; the host must
  never call a module symbol, or `lambda.exe` stops linking without the
  dylib.

---

## 10. Decisions and open questions

Resolved (USER, 2026-09-22):

- Node-specific code may move to `lambda/module/` subdirectories and that
  counts for the metric (JLSR2).
- `lambda/js/mvp/` is excluded and not touched (JLSR1).
- The Node support floor used by `lambda js` and the benchmark corpus stays
  (JLSR3, §1.2).
- The regex pattern detectors stay; JLS-14 is withdrawn (JLSR4).
- All test262 tests and all `test_js_gtest` tests must pass after every batch
  (JLSR5).

Open:

1. **Tune14 ownership.** JLS-5, JLS-7 and JLS-9 are Tune14 §3.3 slices. Track
   them here with a cross-reference, or only in `JS_Tune14.md` with this file
   holding the LOC ledger row?
2. **`js_mir_analyze_and_plan` split (JLS-6).** Splitting the 1,765-line
   function into pass functions is uncredited but is the precondition for
   the CompilerPassManager registration it already performs. Is an
   uncredited restructuring acceptable inside a credited batch as long as the
   batch delta stays non-positive?
3. **Closing the mid-case gap.** If the metric is short of 13,902 after
   batch 7, schedule a second consolidation round after Tune14 (raw/boxed
   facade residue, call-expression lowering, the with/global remainder)
   against a fresh census?

---

## Appendix A — Measurement commands

```bash
zsh temp/js_loc.sh                                   # the metric (add ! -path '*/mvp/*', JLSR1)
lizard -l cpp -l c --csv lambda/js/*.cpp lambda/js/*.hpp lambda/js/parser/*.c   # per-function NLOC
grep -c '^extern ' lambda/js/*.cpp | sort -t: -k2 -rn                          # JLS-1 census
awk '/^\/\/ =====/{getline t; print NR": "t}' lambda/js/js_runtime.cpp        # section markers
./utils/capture_ast_tune_timing.sh --suite js --label <label>                  # compiler time
```

Note: lizard's CSV parses `js_runtime.cpp` only partially (208 of 647
functions, because of the `JS_ROOTS(...)` and intrinsic-body macros); use
line-span counts, not NLOC, for that file.

## Appendix B — Ledger

| batch | package | removed | added | credited net | moved out (JLSR2) | metric after | gates |
|---|---|---|---|---|---|---|---|
| — | baseline `7a06981f8` (mvp excluded) | — | — | — | — | 139,018 | test262 40,261/40,261; `test_js_gtest` 466/466 |
| 1 | JLS-15 dead Node host code | 3,438 | 18 | **−3,420** | 0 | **135,598** | `test_js_gtest` 466/466; test262 40,261/40,261 fully passing; benchmark corpus 109/109 rows identical apart from timing lines (§B.1); Node dylib imports all resolve; `test-lambda-baseline` 5,727/5,740, the 13 failures pre-existing (§B.1) |

### B.1 Batch 1 record (JLS-15)

**Method.** Source scanning proved unreliable for this job: token-pasted
definitions (`JS_FORWARD_*`, `js_dynamic_##token##_call_body`) and
MIR imports resolved by name string both hide references. The deletions were
therefore driven by the linker and the compiler:

1. a scratch link of the debug objects with `-Wl,-dead_strip -Wl,-map` and
   without `-export_dynamic` (link `build/premake/lambda.make`, not
   `lambda-exe.make`, whose archive is stale), listing every stripped
   `lambda/js` symbol;
2. removal of candidates imported by any `modules/*/*.dylib` (`nm -u`),
   referenced outside `lambda/js` (tests, `main.cpp`, the system-function
   registry), named in any string literal, or merely inlined (the debug build
   inlines small callees, so a stripped out-of-line copy is not proof of
   death: each candidate was checked for real call sites);
3. deletion, then rebuild iterations with `-Werror=unused-function` until no
   static became newly unused.

**Removed.**
- The unreachable Node namespace builders and everything reachable only from
  them in `js_runtime.cpp`: diagnostics_channel, domain module surface,
  cluster, repl, `internalBinding`/`uv.errname`, AsyncLocalStorage /
  AsyncResource / `createHook` public surfaces, `internal/*` builders, and 12
  `JS_MODULE_RUNTIME_*` namespace slots.
- Dead `JsRuntimeState` records left by the `bc68bf228` Node split, with their
  allocators, trace spans and destroy code: `JsReadlineState`,
  `JsTlsNativeState`, `JsStreamState` (a 45-Item traced span),
  `JsAssertState` and the node:test mock registry, `JsNetNativeState`,
  `JsClusterState`, `JsAsyncLocalStorageState`; the never-set
  `domain_namespace` and `internal_test_binding_warning_scheduled` fields;
  the uncalled cluster-online host hook.
- The unreachable mock scheduler (nothing could enable it) and 20 other
  exported functions with no caller in any binary.

**Kept on purpose.**
- Domain stack propagation: node-core's events module passes emitter domains
  into `js_domain_call_function`, and `process.domain` is observable.
- Async-hooks resource stamping: it writes properties user code can observe.
- `js_als_capture_context` / `js_als_context_call[_args]`: they are Jube host
  API entries. With no instance registrable, they now reduce to their exact
  empty-state behaviour (capture still returns an empty array, because
  node-core stores it on listener records).

**Hazard caught.** Removing `domain_namespace` from `JsPromiseRuntimeState`
left its traced span declared as 3 contiguous Items, which would have scanned
the `int` counters behind it; the span is now 2. Any future field removal in a
`JsRuntimeState` record must re-check the `visit(...)` span counts in
`js_runtime_state.cpp`.

**Node-floor gate.** All 109 non-MVP benchmark scripts were run with a release
`lambda js` on the batch-1 tree and on the unchanged tree (changes stashed,
release and all five dylibs rebuilt on each side): exit codes identical
(96 × 0, 8 × 1, 5 × timeout on both), 30 outputs byte-identical, 79 differing
only on timing lines, 0 other differing lines, and identical stderr error
counts. Performance: batch 1 removes unreachable code only; the one executed
path it changes (AsyncLocalStorage context calls) now does strictly less work.

**Official benchmark rows.** `python3 test/benchmark/run_benchmarks.py -e
lambdajs -n 1 --typed --no-save -t 180` on the batch-1 release build: all 63
LambdaJS rows completed with a time, no `FAIL` marker and no timeout. The 13
non-zero exits in the raw 109-file sweep above are all outside that list and
fail identically on the unchanged tree: helpers and unbundled sources
(`awfy_helper`, `deltablue2`, `json2`; the runner uses the `*_bundle.js`
files), the Octane suite (not in the LambdaJS row set; five time out, three
throw), the Node-driven `run_jetstream_node.js` (needs `child_process`), and
`prettier_ast_preprocess.js` (needs a local `ref/` checkout). Note the runner
only checks exit status and the scripts' own `FAIL` markers; it does not diff
output against a golden.

**`make test-lambda-baseline`.** 5,727 of 5,740 passed and no leak was
reported. The 13 failures all reproduce, identically, on the unchanged tree
(changes stashed, debug rebuilt): 12 `test_lambda_gtest` scripts
(`radiant_custom_layout_bfc`, `radiant_custom_layout_flow`,
`radiant_vmap_projection`, five `mermaid_*`, three `graphviz_*`,
`structurizr_render`; `radiant_vmap_projection` prints `tag: false` where the
golden says `true`, after a `CUSTOM_LAYOUT_LAMBDA_EXCEPTION`) and the
`test_mir_ratchet_gtest` probe `js_tune6_exact_collection` (`js_main` 11,984 ->
12,629 instructions, +645, same on both trees). They are upstream of this work
and outside the JLSR5 gate; the MIR budget was not edited to hide the growth.

**Side effect found.** Running `test/benchmark/text` rewrites the tracked file
`test/benchmark/text/hyphen_patterns.json`; restore it after benchmark runs.
