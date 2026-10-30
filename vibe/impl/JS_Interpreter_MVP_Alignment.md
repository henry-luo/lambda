# JS interpreter alignment with formal MVP

**Date:** 2026-10-18; tuning proposal added 2026-10-29

**Authority:** **S1.11**, **D1.3v3**, **D2.2.5**, **D5.2–D5.3**, **D6.2.2v2**, **D8.1.3v43**, **D8.2.3–D8.2.4v2**, **D8.4.1v2**.

**Status:** implementation and qualification complete.

Ordinary AST/AUTO and dynamic source consume shared MVP runtime operations under
**D8.1.3v43**. The convergence record below qualifies the final candidate;
earlier dated slices record the incremental work and its historical controls.

## Audit

The retained walker shares MVP semantic operations under **D8.1.3v43**.
Execution policy distinguishes ordinary Scripts/modules from dynamic source;
there is one JS realm/operation contract over shared Lambda storage.

| Area | Live implementation | Finding |
|---|---|---|
| Values and lexical storage | `js_interp_env.h`, `runtime/gc_environment.h`, `owned_item_slot_*` | Lambda `Item`, durable slots, numeric ownership and GC tracing are shared. JS lexical metadata remains language-specific under S1.11. |
| Runtime ownership | `js_interp_execute_script`, `RuntimeExecutionScope`, `RuntimeModuleStateScope`, `RootFrame` | Same Runtime, EvalContext, heap, module-state and event-loop infrastructure as Lambda. |
| Formal MVP dynamic source | `mvp_lmd_dynamic.cpp`, `JsInterpHostOps`, `js_interp_execute_host_eval` | MVP supplies its property, call, class, array and error operations to the existing walker. Direct eval already receives live `JsInterpEnv` cells. |
| Explicit AST/AUTO | `js_interp.cpp`, ordinary `JsInterpHostOps` | MVP objects, arrays, globals and operation kernels; native callable/storage adapters retain explicit DOM/host/module ABI consumers. |
| Promotion | `mvp_lmd_compile_function_satellite`, `mvp_lmd_compile_loop_satellite` | Already targets formal MVP. Ordinary dynamic source remains interpreted. Native prototype/callable compatibility is still used by AUTO satellites. |
| Retired direct-eval bridge | Former `JsEvalBindingJournal`, `JsEvalLocalState`, bridged interpreter branches | Consumers survived after all frame/binding producers were removed with the old compiler. No live caller supplies the old direct-eval flags. Removed in this migration. |

Shared physical storage does not imply Lambda language operations:
JavaScript coercion, equality, reference mutation, TDZ and completion semantics
remain JavaScript's (**S1.11**, **D1.3v3**). The appropriate reuse boundary is
physical storage/ownership and shared frontend facts, with explicit JS semantic
operations.

## Changes

1. Retire the old MIR binding projection, lexical projection and activation-var
   journals, their eight root-vector lanes, their compatibility accessors and
   interpreter branches, the bridged direct-eval source entry, the private-name
   snapshot needed only by that entry, the constant-write text scanner, and
   five unused generated-import entries.
   `js_builtin_eval(Item)` is now explicitly indirect. Direct callers enter
   with live environment cells, including MVP's `JsInterpHostCaller`.
2. Share eval context validation through `js_ast_collect_function_facts`.
   Native direct/indirect eval now uses the same facts as MVP-hosted eval;
   eval always parses with Script grammar. Remove the old class-initializer
   text scanner. The retained private-name parse scope remains necessary for
   early errors; it stores identities, not copies of lexical values.
3. Give `JsInterpEnv` an owned scalar home for `this`, using
   `lambda_item_adopt_scalar_home`. Ordinary calls, suspended calls and
   per-iteration environment clones use that same storage. This replaces
   the hosted path's per-call pool allocation and the native path's raw
   receiver copy. The existing six retained Item fields remain contiguous
   for common GC tracing; the scalar home is outside that traced span.
   Advance executable module-cache compiler ABI 3 to 4 because generated
   environment slot offsets change.
4. Update the formal design and interpreter design together. No ECMAScript
   coercion or completion rule changes; this implements the established
   guest-semantics and precise-ownership contracts.

## Reproduction and verification

The archived pre-change executable is
`temp/js_interp_mvp_alignment/before_lambda.exe`. The source mismatch is:

```js
class C { value = eval('({arguments: 42}).arguments'); }
console.log(new C().value);
```

Forced AST reports `SyntaxError: Invalid eval in class field initializer`;
formal MVP prints `42`. The text scanner incorrectly treats a property name as
an `arguments` reference. The shared AST fact visitor already excludes property
names and nested ordinary-function bodies while visiting template substitutions
and escaped identifiers.

The pre-change native runtime/script suite passed **210/210**. New tests exercise
AST, AUTO and formal MVP for eval context, escaped eval-created bindings and
constant writes, and numeric receivers retained by closures across collection.
Qualification on 2026-10-19:

| Check | Result | Evidence under `temp/js_interp_mvp_alignment/` |
|---|---|---|
| `make build-test` | passed | `build.log` |
| `make build-release-compile` | passed | `release_build.log` |
| Original mismatch on the pinned release executable | AST, AUTO and MIR all print `42` | `reproduction_after.json` |
| Three new regression tests | 3/3; 33 backend/source executions | `focused.log` |
| Native JS runtime/script suite | 213/213 | `script.log` |
| Formal MVP suite | 825/825 | `mvp.log` |
| Eval/receiver/private-closure tests with forced GC and freed-memory poisoning | 18/18 | `forced_gc.log` |
| `make test-lambda-baseline` | 7,892/7,892: 2,112 input + 5,780 runtime | `lambda_baseline_python313.log` |
| `make test262-baseline` | exit 0; 40,253/40,261 fully passing, eight timing partials, zero semantic failures/regressions | `test262_baseline.log` |
| Eight timing-partial RegExp cases, isolated paired batch | 8/8 before and after; 18.8 s before, 18.7 s after | `regexp_before.log`, `regexp_after.log` |
| Retired APIs | all 12 absent from production sources and linked exports | `retirement_check.json` |

The first aggregate run passed 7,891/7,892; its sole failure was the PDF output
validator importing Pillow from a Python 3.14 environment without that package.
The installed Python 3.13/Pillow environment passed the error-system suite
269/269 and then the entire aggregate gate. The command-local environment was
`PATH=/opt/homebrew/opt/python@3.13/bin:$PATH`; no source, golden, timeout or
baseline allowance was changed for this failure.

Test262 uses `LAMBDA_TEST262_EXE` to select the private
`validated_release.exe`; `release_provenance.json` records its SHA-256 and the
production diff hash. Release executables are stripped, so source absence is
the proof that the old APIs were retired, rather than a symbol-count comparison.

The standard Test262 gate is mixed AST/MVP: 34,334 AST and 5,895 MIR assignments
plus 32 MIR-native cases. Its eight Unicode property-escape RegExp cases passed
semantically but took 3.23–4.74 s against the existing 3 s threshold in the
seven-worker run. All eight passed the runner's individual retries. A subsequent
single-worker batch of the same cases passed on both archived pre-change and
new release executables with essentially equal elapsed time. This diagnostic
does not establish a fully clean aggregate timing gate; the eight partials
remain recorded. No Test262 harness, allowlist, timeout or timing threshold was
modified. Original diagnostics are preserved beside the logs.

`make formal-index` regenerated the ruling index, with the existing duplicate
DO25 warning. `git diff --check` passes. Production C/C++ changes remove a net
655 lines. This is correctness/ownership qualification, not a performance or
cross-platform result.

## Migration boundary recorded before convergence (2026-10-18)

The following audit motivated the completed convergence below. At that point,
explicit AST/AUTO still had a native JS realm and callable profile. This pass
retires the defunct compiler-specific eval protocol; it does not remove all
native `js_*` support or claim a single object representation across both
profiles. Live entry points include `js_execution.cpp`'s classic, CommonJS and
ES-module dispatch; `js_event_loop.cpp`'s string timers;
`lambda/dom/realm/dom_realm.cpp`'s callback adapter; and the function/loop
satellites in `js_interp.cpp`. A complete profile migration must jointly cover:

- retained classic Scripts, harness reuse and global lexical declarations;
- static/dynamic modules, CommonJS, live exports and Lambda imports;
- direct native and DOM callbacks, function identity and prototype mutation;
- arguments aliasing, class/private state, async/generator reentry;
- AUTO call promotion and loop handoff using the same realm identities.

Changing only the root executor to the MVP dynamic-source profile would be
incorrect: its admission excludes static imports/exports and CommonJS, it
wraps AST callables, and it disables ordinary AUTO promotion. Those differences
need migration of their actual owners and call sites, followed by the same
behavioral gates. Removing the native profile before that work would drop live
features rather than retire obsolete compiler support.

## Benchmark analysis and tuning proposal — 2026-10-29

### Evidence and limits

The original forced-AST release screen was compared with the latest recorded
[MVP_Result10](../../test/benchmark/js_mvp_lmd/MVP_Result10.md), whose candidate
was pinned to MIR. The local [comparison](../../temp/benchmark_ast/comparison_20261029.md)
and [JSON](../../temp/benchmark_ast/comparison_20261029.json) preserve the row
results, binary hashes, selectors and capture filenames. The current executable
is from `efde5985702014d2ebe0e6be790628d3b352a8ea`, SHA-256
`c65a1464ded7466a26ae7a25d9f7da9bc65d91d29ee58e553b2431b99d0b67e1`.

Of 71 standard workloads, 61 produced timings, six timed out, and four jq rows
have no completed measurement. All 61 measured AST rows are slower: geometric
mean **80.13x**, median **71.98x**. These are one current sample against historical
MVP medians, with differing wrappers/timing boundaries and no full output-oracle
qualification. They establish where to investigate, not the speedup achievable
by changing helpers. Timeout rows are excluded, so the aggregate describes only
the completed subset; Result10's 17 microbenchmarks were also not rerun.

| Workload | AST / recorded MVP | What to investigate |
|---|---:|---|
| `r7rs/sum`, `r7rs/sumfp` | 983.60x, 1042.46x | local reads/writes, arithmetic, boxing, AST dispatch |
| `larceny/array1`, `kostya/matmul` | 1032.73x, 891.23x | indexed access plus loop/local/temporary overhead |
| `r7rs/fib`, `r7rs/tak`, `r7rs/cpstak` | 239.78x, 526.07x, 704.71x | recursive calls, activations and lexical records |
| `awfy/richards`, `awfy/deltablue` | 46.49x, 22.19x | property/reference operations and method calls |
| `beng/pidigits`, `beng/regexredux` | 2.02x, 2.89x | native arithmetic/library work amortizes walker overhead |
| `text/hyphen`, `text/microdiff` | 6.19x, 10.81x | mixed string/library and interpreted work |

These are workload-based hypotheses, not current CPU-profile attribution.
In particular, MIR emits native numeric instructions and keeps proven values
in registers (`mvp_lmd_mir.cpp`, `binary_value` and its integer paths).
The AST path evaluates child nodes, propagates completions, manages live
temporaries and invokes boxed helpers. Runtime-helper convergence alone cannot
recover all of that difference.

Correction to the initial capture interpretation: jq rows have a 600-second
timeout floor in `run_benchmarks.py`; the command-line 30 seconds did not apply
to them. The interrupted `jq_mix` attempt is not evidence of a broken timeout
handler. The six actual capped rows are `awfy/havlak`, `kostya/collatz`,
`larceny/diviter` (30 seconds), and `text/log_pipeline`, `text/text_search`,
`text/three_way_merge` (10 seconds).

### Helper alignment at the proposal checkpoint

Source audit against the current checkout:

| Area | Ordinary AST | MVP / hosted AST | Proposed disposition |
|---|---|---|---|
| Value ownership and environments | `Item`, `JsInterpEnv`, owned scalar slots, exact roots | same substrate; hosted eval links live cells | Keep the shared substrate and JS binding semantics. |
| Primitive numeric arithmetic | `js_interp_binary` calls `js_add`, `js_subtract`, etc.; `js_numeric_number_pair` already provides a Number head | `mvp_lmd_dynamic_binary` calls those same scalar helpers after MVP coercion; MIR also emits native arithmetic | Reuse the existing scalar algorithms. Do not add a third arithmetic implementation. |
| Object coercion and general operators | native `js_*` conversion/property/call policy | `mvp_lmd_to_primitive`, MVP property/call policy, BigInt/error handling | Converge on the MVP semantic kernels once operand representations and realm ownership match. |
| Properties and arrays | `js_get`/`js_set`, `js_get_key_default`, native array/typed-array kernels | `mvp_lmd_property_with_receiver`, `mvp_lmd_array_index_property`, MVP array helpers | One shared reference/property implementation, including guarded indexed operations and its general fallback. |
| Calls, constructors, classes | native `JsFunction` capabilities and class state | `dynamic_invoke` calls `mvp_lmd_class_invoke`; AST functions are wrapped through the host adapter | Migrate identity, receiver/new-target and class/private ownership together; retain normal per-callee invoke/construct capabilities. |
| Globals, libraries and modules | native realm and live module/DOM consumers | adapter state uses `MvpLmdProgram` and its library/realm ownership | Make runtime services usable without compiling an MVP program, then migrate all entry points to that owner. |

The reuse seam already exists: `JsInterpHostOps` in
[`js_interp.hpp`](../../lambda/js/js_interp.hpp), populated in
[`mvp_lmd_dynamic.cpp`](../../lambda/js/mvp-lmd/mvp_lmd_dynamic.cpp).
Its property, invocation, arrays, iterators, class, error and conversion
operations already execute MVP dynamic source through the retained walker.
The target is **the same JS runtime helpers and realm identities for AST and
MIR**, with AST versus MIR determining execution, not object semantics.

Do not implement this by merely installing the current host table. Several
existing AST optimizations explicitly require `!js_interp_host`: dense and
typed-array reads/writes, parameter-leaf calls/comparisons and function-environment
elision. Hosted literals also bypass the native literal cache. Port the
representation-independent eligibility facts and shared fast kernels as part
of convergence, or alignment can initially make the interpreter slower.
The adapter's dynamic-source admission and promotion policy also differ from
ordinary Script/module execution, as the migration boundary above records.

### Ordered implementation slices

1. **Establish a controlled baseline.** Freeze one release executable and one
   source manifest; alternate forced AST and forced MIR using identical kernel,
   setup, repetition count, timing region and output oracle. Start with `sum`,
   `matmul`, `fib`, `richards`, `microdiff` and `regexredux`, then cover the full
   population. Separately profile these representatives to distinguish node
   dispatch, binding access, property/coercion helpers, roots, allocation and GC.
   Keep timeout/missing rows explicit. Historical Result10 remains context;
   a before/after interpreter comparison must use pinned old/new executables.

2. **Make the MVP runtime independently usable by the interpreter.** Reuse the
   existing library/realm state and host-ops contract; separate runtime-owned
   services from `MvpLmdProgram` compilation ownership where necessary. Do not
   compile an empty MIR module merely to initialize a realm. Audit the
   `LAMBDA_NO_MIR` boundary: current program stubs exclude dynamic classes,
   realm creation and dynamic import. Runtime convergence must preserve a
   usable interpreter-only build. First qualify ordinary Scripts/globals,
   property/coercion behavior, arrays and calls in an MVP-backed AST realm.
   Then migrate modules/CommonJS, DOM/host callbacks, async/generator/class
   ownership and AUTO satellites before changing the normal entry points.
   Keep one retained AST walker, one operation contract and one set of shared
   helpers; compatibility adapters are transitional and need named live users.

3. **Reduce local and temporary costs without changing JS semantics.** Extend
   existing immutable scope/AST plans with direct activation slots for locals
   that cannot escape or be observed through eval, `with`, mapped arguments,
   closures or suspension. Keep observable bindings in their existing live
   cells. The current parameter-leaf shortcut and empty-environment elision
   are useful starting points, but do not cover ordinary mutable loop locals.
   Use an activation-owned exact-root window for temporaries and destination-owned
   numeric homes; avoid opening a new root window for every binary operation
   when its values already have planned live slots. A value must remain rooted
   while evaluating another operand that can allocate or reenter JS. Only
   proven non-allocating primitive operations may bypass that work
   (**D5.3.3**, **D8.2.4v2**).

4. **Share fast numeric and indexed kernels.** Preserve the existing Number
   fast heads, adding destination-owned result paths only where profiling shows
   boxing/root traffic matters. Preserve binary64 rounding, signed zero,
   NaN, overflow widening, BigInt separation and coercion order (**D2.2.5**).
   Reuse shared numeric-key array/typed-array operations without constructing
   property strings. Dense reads/writes already exist in the AST lane; broaden
   eligibility only with evidence and preserve holes, descriptors, prototypes,
   receiver semantics, detachment and coercion through the shared fallback.
   Static keys and immutable plan facts are allowed; mutable per-site shape,
   callee or method caches are forbidden by **D8.4.1v2**.

5. **Trim remaining walker/call work using profiles.** Move
   `js_interp_loop_needs_per_iteration_env`'s AST fact/capture scans from loop
   entry to the shared immutable plan. It currently scans once per loop entry,
   not every iteration. Extend safe activation/environment elision through
   the normal invoke kernel (**D6.2.2v2**), then split cold suspension/eval state
   from the frequently copied frame if measured. Reuse the existing tuning
   [J2/J4/J6/J7 plan](Lambda_Impl_JS_Interp_Tune.md); its historical profile
   percentages and speed projections are not measurements of this build.
   J1/J3/J5 already have implementations, so they are not new work items.

Runtime alignment is the architectural priority; direct local slots and
temporary/boxing costs are the first performance targets suggested by the
numeric-loop results. Do not use AUTO promotion as evidence of improvement
to the explicitly pinned interpreter. No speedup target is asserted before
the controlled baseline and profiles exist.

### Acceptance and retirement

- Each runtime slice needs forced-AST/forced-MIR behavioral comparisons for
  coercion order, getters/proxies, descriptors/prototypes, sparse/typed arrays,
  strict writes, receiver/new-target, eval/closures/arguments and class/private
  state. Preserve exact GC roots across callbacks and exceptions; run relevant
  forced-GC and poisoning checks. Add module/DOM/async cases at their migration
  boundary, rather than assuming dynamic eval qualifies those entry points.
- Run the JS runtime/script and MVP suites, `make test-lambda-baseline` and
  `make test262-baseline` for shared-runtime changes. Report explicit AST
  coverage separately: the configured mixed-tier Test262 gate is not proof of
  AST-only parity. Do not change harnesses or baselines to hide regressions.
- Compare pinned before/after AST builds and retain MIR as a control, with
  repeated alternating release samples, matching outputs and source/binary
  hashes. Record startup separately from steady execution; preserve MVP
  performance and identify any losing rows before widening the rollout.
- After consumers migrate, reuse the
  [runtime helper liveness audit](JS_MVP_Lmd_Runtime_Helper_Audit.md) and
  `utils/js_runtime_helper_audit.py` to remove unused native profile helpers,
  including indirect callback/import roots. A `js_*` name alone is not evidence
  of legacy code: shared primitive algorithms remain live and should stay.
- The former **D8.1.3v42** profile distinction is superseded by
  **D8.1.3v43**, revised with this working record and the generated index.
  The ordered slices above record the proposal; the convergence section below
  records its implementation and qualification. JavaScript semantics remain
  separate from Lambda-language coercion/mutation (**S1.11**).

## First implementation slice — 2026-10-29

This slice implements shared scalar dispatch, Number temporary handling and
the J2 loop-environment fact. The larger realm, object/call, local-slot and
activation-window migrations remain outstanding. **D8.1.3v42** continues to
describe two operation profiles; no normative semantics or tier-selection
rule changes in this slice.

- `js_binary_operator` is the common scalar dispatch used by ordinary AST
  execution and the primitive tail of `mvp_lmd_dynamic_binary`. Compound
  assignment maps its operator once and uses that same dispatch. Existing
  arithmetic, comparison and bitwise kernels remain the implementation;
  MVP object coercion, BigInt handling and realm-specific errors stay ahead
  of the shared primitive tail (**S1.11**, **D8.1.3v42**).
- `js_binary_number_pair` admits only INT/FLOAT Numbers and non-reentrant
  numeric operators. Both AST profiles use it. A Number left operand owns
  its scalar payload locally while the right operand evaluates; a successful
  Number pair avoids an expression root window. A non-Number operand or
  reference operator retains the rooted semantic path (**D2.2.5**, **D5.3.3**).
- `js_ast_publish_extension_facts` seals the per-iteration-environment decision
  on the loop's `NameScope`. The interpreter reads it instead of rescanning
  AST function/eval/capture facts on every loop entry. The existing capture
  algorithm is moved, not duplicated; cached AST instances retain the same
  immutable fact. AST cache ABI is 2 for both cache request paths
  (**D8.2.4v2**, **D8.4.1v2**).

The realm-entry investigation confirmed that `mvp_lmd_program.cpp` already
contains MIR-free ownership infrastructure, but library planning/instantiation
and class/module services still depend on `mvp_lmd_mir.cpp`. Replacing normal
AST entry with the dynamic-source adapter is therefore not part of this slice.

### Controlled release measurements

Evidence is under `temp/js_interp_alignment_impl/`. The before executable is
the release binary recorded above; the after executable is SHA-256
`d7f30cd80c9243ca1f982427736972371439962fdc22e462998b48bb3aed11e5`.
`paired_ast.json` and `paired_mir.json` use the existing paired runner, seven
alternating pairs plus one warmup per row, identical source and selectors,
and matching normalized outputs. Binary, build-source and native-module
manifests stayed stable during both campaigns. The filter includes `fibfp`
and `sumfp`, yielding eight rows.

| Workload | Before AST median ms | After AST median ms | Time reduction |
|---|---:|---:|---:|
| `r7rs/fib` | 210.455 | 208.785 | 0.79% |
| `r7rs/fibfp` | 211.573 | 208.292 | 1.55% |
| `r7rs/sum` | 264.249 | 248.444 | 5.98% |
| `r7rs/sumfp` | 26.369 | 24.793 | 5.98% |
| `awfy/richards` | 3369.321 | 3366.881 | 0.07% |
| `beng/regexredux` | 8.618 | 8.629 | -0.12% |
| `kostya/matmul` | 4760.395 | 4608.856 | 3.18% |
| `text/microdiff` | 1358.718 | 1219.073 | 10.28% |

The eight-row AST geometric mean time reduction is 3.53%. This is a focused
before/after result, not a new 71-row publication or full-runtime convergence
claim. The initial MIR control geometric mean is +0.33%. A subsequent
21-pair isolated follow-up (`paired_mir_followup.json`) puts `fib` at -0.25%
and `fibfp` at -0.69%, resolving the initial `fib` +2.47% signal. `regexredux`
remains +1.80% by median (2.930 to 2.983 ms); its 95% bootstrap interval for
the ratio is 0.9984–1.0299 and includes no change. Before/after MIR is identical
after a bijective normalization of aligned raw/String addresses
(`regexredux_mir_comparison.json`). This rules out a changed emitted MIR
sequence, not native layout effects or timing variation; a blanket claim of
no MIR regression is not made.
The sampled stripped baseline executable retains a macOS `sample` artifact,
but most private interpreter symbols are unresolved, so no source-level CPU
percentage is inferred from it.

### Qualification

- Release build passed.
- Twelve CLI executions across AST/MIR and ordinary/dynamic source passed,
  then all twelve passed again with `LAMBDA_GC_FORCE_EVERY=1` and
  `LAMBDA_GC_POISON_FREED=1`.
- Added GTest coverage for Number scalar-home lifetime, signed zero/NaN,
  numeric coercion order, mixed BigInt/Symbol errors, compound assignments,
  and escaping ordinary/eval/class/for-in/for-of iteration bindings.
- The two new GTests also passed in the debug build with forced collection
  and freed-memory poisoning (`focused_forced_gc.log`).
- `make test-lambda-baseline` completed **8,449/8,454**. All JS suites passed:
  runtime **569/569**, MVP **847/847**, interpreter/script **254/254**. Four
  web-server tests failed because the sandbox denied loopback binding; all
  four passed outside the sandbox. `docx_docx_business_plans` exceeded the
  aggregate's 60-second limit and passed its isolated rerun in 31.445 seconds.
  These focused reruns resolve the observed failures but do not rewrite the
  original aggregate result (`lambda_baseline.log`,
  `serve_outside_sandbox.log`, `docx_isolated.log`).
- `make test262-baseline` passed **40,261/40,261**, zero retries, partials or
  regressions, in 50.0 seconds against the pinned after release executable
  (`test262_baseline.log`). The configured assignment was 39,668 AST bodies,
  590 MIR bodies and three MIR-native cases; this is mixed-tier qualification,
  not an AST-only conformance claim.
- `git diff --check` passed. No baseline, golden, timeout, Test262 harness or
  vendor source was changed. The October 19 qualification above remains
  separate from these results.

## Runtime-only realm slice — 2026-10-29

The MVP realm can now initialize without parsing or compiling an empty Script.
`mvp_lmd_program.cpp` owns builtin shape and slot planning, intrinsic publication,
physical initialization, module-controller ownership and execution retirement.
MIR still computes source-dependent iterator/RegExp/process optimization facts;
the runtime consumes those facts, while open AST realms use conservative defaults.
The physical setup implementation is shared, rather than copied into an AST lane
(**D8.1.3v42**, **D8.2.3–D8.2.4v2**).

Three existing consumers use the new boundary:

- MVP `$262.createRealm()` creates its library and globals directly, with no
  compiler function table, parsed bootstrap AST, lowering passes or generated code.
- Native AST `$262.createRealm()` now uses the same factory. The previous partial
  object reused parent constructors and lacked a functioning realm-local
  `evalScript`. The parent keeps its existing global/intrinsic identities; each
  child owns independent MVP globals, prototypes and persistent lexical bindings.
- Cached MIR Script startup initializes its root realm through the factory,
  replacing its empty-MIR bootstrap before entering the cached child Script.

Dynamic class construction and callable-property materialization also moved out
of the compiler file. Native helpers and AST wrappers use shared materialization;
generated functions supply their source-derived name and constructor-layout facts
from MIR. AST wrapper names continue to come from the retained interpreter's
callable metadata. This removes the former `LAMBDA_NO_MIR` class/realm exclusions
without adding another evaluator or changing tier selection (**D8.1.3v42**).

Cross-realm qualification exposed a native-catch boundary defect: generic MVP
errors had no observable native Error shape, and the property-ownership probe
assumed every map-tagged carrier had a shape. Native catch now reuses
`mvp_lmd_caught_value` to materialize those errors with the current realm's
constructor before exposing the Map lane; ownership probing also rejects
unshaped carriers. The detached typed-array reproduction and its eight Test262
cases are retained in the evidence directory. No Test262 runner, allowlist or
baseline is changed (**S1.11**, **D5.3**).

Callable ingress now resolves an AST function's defining realm before deciding
whether to wrap it. Native AST callbacks retain their native operation profile
when called by a child realm, including captured bindings and implicit errors;
MVP-created AST functions retain their existing hosted profile. Native builtin
functions still bypass wrapper selection without a realm scan. Native Error
carriers capture their creating realm's prototype through the existing traced
property storage. This preserves both native-parent callback errors and native
host errors created in an MVP realm when a later caller checks `instanceof`
(**D8.1.3v42**, **D6.2.2v2**).

Realm slots remain registered exact roots, and retained Script ownership destroys
the heap before the nominal metadata and AST source it can reference
(**D5.3**, **D5.4.1**). No conservative stack scanning or inline caches were added
(**D8.4.1v2**). This does not revise the formal distinction between native and MVP
operation profiles: normal AST literals/calls still use the native profile, while
child-realm source uses the existing MVP-backed AST host contract.

Evidence is under `temp/js_interp_realm_alignment/`. The control is the first
slice's pinned `after_release.exe`; the candidate and no-MIR executables have
separate hashes. The first aggregate attempt overlapped a release relink of the
shared CLI and is retained as `lambda_baseline_superseded.log`; it is excluded
from final qualification. Subsequent aggregate validation is serialized with
builds.

Remaining alignment work includes ordinary AST entry migration, module/CommonJS
and DOM callback ownership, AUTO satellites, and preserving native numeric/indexed
fast paths under the shared operation contract. MVP dynamic imports still use
MIR module lowering; the no-MIR build reports that capability boundary explicitly.
The native no-MIR Script interpreter remains available.

### Realm-slice qualification

- Release and `lambda-nomir` builds pass. The pinned release executable is
  SHA-256 `772dccde0342758439aee80fa174e9a4cb7b7ceeee8ed59243eca3461a107110`;
  the no-MIR executable is
  `08ea1d0333e24c1f55d9b4b8fcc06d22da3b9c8a963fe8b88446a9e9034cb0bd`.
- Twenty release probes across AST/MIR and ten no-MIR probes pass, including
  ordinary and forced-GC runs. They cover independent constructors/prototypes,
  persistent bindings, closures, private classes, builtin collections, and
  native-parent/MVP-host Error identity. The no-MIR symbol audit finds no
  undefined MIR or MVP compiler entrypoints.
- The four focused debug GTests pass with forced collection and freed-memory
  poisoning. The structural realm test verifies there is no generated code,
  compiler function table, parsed bootstrap AST or completed lowering stage.
- `make test-lambda-baseline` completes **8,453/8,457**. Runtime JS is
  **569/569**, MVP **848/848**, interpreter/script **256/256**, and MIR GC
  stress **350/350**. Only four loopback listener tests fail in the sandbox;
  all **4/4** pass in the isolated outside-sandbox rerun. DOCX passes within
  the aggregate limit. The aggregate result remains distinct from its reruns.
- `make test262-baseline` passes **40,261/40,261**, with zero regressions,
  retries or timing partials, in 54.1 seconds against the pinned candidate.
  The configured assignment remains 39,668 AST bodies, 590 MIR bodies and
  three MIR-native cases: mixed-tier evidence, not AST-only conformance.
- Earlier qualification found eight detached-buffer Test262 crashes and two
  host-error identity regressions. The final runtime fixes pass their focused
  reproductions; the failed attempts remain archived as
  `test262_baseline_before_fix.log` and
  `lambda_baseline_before_prototype_capture.log`. No harness, golden, baseline,
  timeout or vendor source was changed.

### Realm-slice paired release measurements

The control is the first slice's pinned release executable
`d7f30cd80c9243ca1f982427736972371439962fdc22e462998b48bb3aed11e5`.
The existing paired runner alternates execution order and retains stdout hashes,
source identities, binary hashes, and build/native-module manifests. All three
campaigns completed with valid pairs, matching outputs and stable provenance.

| Campaign | Samples | Candidate/control time | Interpretation |
|---|---:|---:|---|
| Same eight general workloads, AST pinned | 7 pairs + 1 warmup per row | 0.99495 geometric mean | -0.50%; every row's 95% interval includes 1 |
| Same eight workloads, MIR pinned | 7 pairs + 1 warmup per row | 1.00059 geometric mean | +0.06%; broadly unchanged |
| Create and evaluate 32 independent realms, MIR parent | 21 pairs + 2 warmups | 0.93683 median ratio | 87.823 → 82.275 ms, 6.32% less time |

The targeted realm workload checks independent globals/constructors, child Array
prototype identity, persistent declarations and Map results; its exact normalized
output is `192`. Its 95% paired-bootstrap ratio interval is **0.92216–0.94248**.
The parent is pinned to MIR because the old native AST realm stub cannot satisfy
the same semantic oracle. Child source remains interpreted on both sides. This
measures creation plus child evaluation, not isolated allocation or all AST code.

The ordinary-workload results do not establish an additional general interpreter
speedup. AST `microdiff` is -4.93% by median but its interval includes 1; the MIR
row is -0.98% with interval 0.98351–0.99738. MIR `regexredux` is +1.84% with interval
0.99008–1.04116. The remaining general-workload intervals also include no change.
Artifacts: `paired_ast.json`, `paired_mir.json`, `paired_realm.json`, and
`qualification.json` under `temp/js_interp_realm_alignment/`. This is a focused
qualification of the realm boundary, not a replacement for the 71-row benchmark
publication or completion of ordinary AST operation-profile migration.

## Ordinary Script/Module convergence (2026-10-29)

The candidate routes ordinary AST and AUTO execution through the same
`JsInterpHostOps` contract as MVP dynamic source. Script/module slabs and
promotion remain execution policy; objects, arrays, properties, coercion,
classes, iterators and invocation use MVP operations (**D8.1.3v43**, **S1.11**).
An execution-owned `JsScript::runtime_ops` restores the defining realm for
native callbacks, modules, async/generator resumptions and AUTO satellites;
cached AST images contain no runtime pointer (**D8.2.4v2**, **D8.5.1v7**).

Boundary repairs retain public class `new.target`, share Symbol allocation and
native reflection identity, apply the defining realm's primitive boxing once
through the invoke kernel, preserve class-owned `name` elements, synchronize
global data stores with module slabs, and delete primitive properties through
the shared boxing/descriptor path. Ordinary calls reuse the existing activation
and numeric indexed writes reuse MVP array/typed-array kernels. No mutable
per-site callee or shape cache is introduced (**D6.2.2v2**, **D8.4.1v2**).

The first controlled release comparison is diagnostic, before the later call
and indexed-write changes. Seven alternating pairs match output on all eight
rows: fib 1.1958, fibfp 1.2119, sum 1.0563, sumfp 1.0392, Richards 0.6726,
regexredux 0.3641, matmul 1.1363 and microdiff 0.3374 (candidate/control execution
time). Release sample profiles identify binding reads/eval lookup in numeric
loops and repeated call/root setup in Fibonacci. These measurements justify
the activation/local work; they do not qualify the final candidate. The initial
regexredux sample ended before collection and needs a repeated-source profile.

Evidence lives under `temp/js_interp_proposal_goal/`: pinned controls,
`runtime_paired_ast.json`, release sample profiles, 528/528 forced-AST script
results, the initial aggregate Test262 failure and focused recovery logs. The
initial Test262 run passed 40,098/40,261, with 162 semantic failures and one slow
large-global case. All 162 semantic failures passed the focused rerun after
the boxing/name/delete repairs. The final qualification below supersedes that
intermediate result; the diagnostic timing capture is not a performance claim
for the final candidate.


### Completed tuning and retirement

- Immutable function/scope plans assign direct activation slots to synchronous
  locals whose activation cannot escape. Strict bodies may call helpers;
  sloppy numeric bodies require primitive-only source and Number actuals.
  Eval, with, arguments, closures, suspension, unsupported scope forms and
  oversized plans retain live cells. AUTO loop handoff keeps its live-cell path.
  Planned temporaries borrow an exact activation root window, own scalar homes,
  and retain snapshots across RHS mutation, GC and coercion callbacks
  (**D5.3.3**, **D8.2.4v2**).
- Shared Number heads, indexed dense/typed-array operations, literal reuse and
  guarded string append remain eligible with MVP operations. Ordinary native
  AST calls reuse the invoke kernel's activation roots. Native constructor
  ingress verifies that an advertised rooted argument span is actually rooted;
  unrooted host/species arguments take the existing copied-root path.
- Array reduce shares one generic reducer with frontend-supplied length,
  indexed-property and Call operations. Dense reads retain the shared storage
  admission and tracing; other reads use MVP HasProperty/Get and callback
  entries. Native observers keep their ABI adapter.
  The reducer roots the initial accumulator before observable length access.
  Array visitor callback arguments use reusable precise root spans. Native HasOwnProperty reflects MVP descriptors, including accessors.
- Open realm globals reuse the shared JS dictionary carrier, avoiding retained
  packed-shape clones for large global declaration sets. Native own-definition
  ingress publishes window.event's property as well as its live host value.
  Lazy host markers use the shared storage writer without recursive guest Get.
- The helper audit removes only the three unreachable single-value console
  facades and obsolete MVP Symbol-registry lookup/allocation. Multi-argument
  console callbacks, host/module ABI adapters and test-covered cache APIs stay.
  Symbols share the stable Input-owned identity service across both ABIs.
- Release profiles show binding lookup and call/root work in numeric workloads.
  They do not establish frame copying as a dominant cost. The proposal's
  conditional J4 cold-frame split is therefore not admitted; no speculative
  frame rewrite or mutable per-site cache is added (**D8.4.1v2**).


### Final runtime qualification — 2026-10-30

Evidence is under `temp/js_interp_proposal_goal/`. The release candidate is
`final_release.exe`, SHA-256
`e3f61347e2e28a2316361585fd071f472ac94f14798522e29e6467cc5f6e0dcc`;
the pre-goal control is `control_release.exe`, SHA-256
`772dccde0342758439aee80fa174e9a4cb7b7ceeee8ed59243eca3461a107110`
(clean `841f7583c`). `source_manifest.json` and `source.patch` retain source and
binary provenance. This control already includes the earlier scalar/realm
slices; the new comparison isolates ordinary convergence and subsequent tuning.

| Gate | Final result | Evidence |
|---|---|---|
| `make test-lambda-baseline` | **8,464/8,464**, including 848 MVP, 569 configured JS, 263 runtime/script, 121 optimization and 350 GC stress cases | `lambda_baseline_generator_final.log`, `baseline_verified/` |
| Forced AST script suite | **528/528** | `ast_files_final.log` |
| Selected ownership boundaries with forced GC and freed-memory poisoning | **28/28** (25 selected boundaries, two generator resource/depth cases and native reducer ingress) | `gc_final.log`, `gc_delegated_homes_final.log`, `gc_nested_delegation_final.log`, `gc_reduce_ingress_final.log` |
| `make test262-baseline`, pinned release | exit zero; **40,261/40,261 fully passing**, zero partials or semantic regressions | `test262_final.log` |
| No-MIR build and JS script population | builds; **525/528** pass, three unchanged excluded Lambda-module import cases | `build_nomir_final.log`, `nomir_ast_files.log`, `nomir_lambda_import_controls.json` |
| No-MIR eval/classes, closures/arguments, async/generators and independent realms | **16/16** smoke executions | `nomir_smoke.log` |
| No-MIR delegated numeric/depth reproducers | exact fractional/signed-zero and 2,048-deep nested results pass | `nomir_delegated_homes.log`, `nomir_nested_delegation.log` |
| No-MIR static/dynamic ESM, CommonJS and child-realm import/constructor identity | exact output checks pass; no MIR entry points linked | `nomir_module.log`, `nomir_realm_module.log`, `nomir_mir_symbols.json` |
| Helper retirement | 48 TUs, zero diagnostics or unreachable candidates; one implicit RAII constructor reviewed | `helper_audit_final/` |

The first aggregate run lacked sandbox localhost access and retained four server
failures. The final complete baseline runs with localhost access and passes all
152 server cases. The final Test262 run has no partials. Its first run retained one BMP
RegExp timing partial, which passed isolated retry; one fresh serialized
baseline passes all 40,261 fully before timed samples begin. Both logs remain
under `test262_before_serial_recheck.log` and `test262_final.log`. Earlier batch-instability
observations are superseded by this result; the configured assignment remains
mixed-tier evidence, not AST-only parity. No Test262 harness, baseline, timeout
or expectation is changed.

The no-MIR executable is SHA-256
`e9527d526300c301b95a30e680a9df31d5112c886b7cf36dec50c365d06955ae`.
All three failing script cases import `.ls` modules. The pinned pre-change
no-MIR executable rejects them with the identical message, “Importing a Lambda
module from JavaScript is excluded from this build.” JS module loading, including
child-realm dynamic import, is qualified separately and passes.


### Measured field-store refinement — 2026-10-30

The first fully qualified candidate's seven-pair MIR screen found Richards
**1.0554×** and microdiff **1.0263×** versus the pre-goal control. Every packed
MVP data write was invoking the native global-binding observer. Open realm
globals now use native dictionary storage, whose foreign write ingress already
performs that synchronization; packed MVP fields have no native module-slot
bridge. Remove the redundant observer from that writer and retain it in native
dictionary ingress. The shared named-data writer still owns tracing and physical
storage; no semantic operation or per-site cache changes.

Against the preceding candidate, 11 alternating MIR pairs give Richards
**0.92521×** (95% interval **0.90169–0.93869**) and microdiff **0.96949×**
(**0.96101–0.97714**), with matching oracles and peer-control confirmation.
`sync_refinement_ab/comparison.json` retains the raw samples and provenance.
Focused AST/MIR globals tests pass, and the shared-runtime regression case now
checks `var` writes through both an identifier and `globalThis` across tiers.
The final gates above qualify this refinement; the final population capture
below uses its pinned executable.

### Foreign presence queries — 2026-10-30

The broad diagnostic capture exposed a second shared-kernel cost: a foreign
`HasProperty` created a JavaScript descriptor object before returning a boolean.
After open globals moved to native dictionaries, every unresolved/global binding
read entered this path. Descriptorless own-property queries did the same work.
The existing `js_has_own_property` already provides passive presence, exotic
handling and virtual native metadata without constructing a guest descriptor.
Reuse it for the boolean operation; keep proxy Has dispatch first and retain the
prototype walk on a miss. Explicit descriptor requests keep the descriptor path
(**D8.1.3v43**, **D6.2.2v2**).

`NativePresenceQueriesDoNotAllocateGuestDescriptors` fails before this repair
and passes afterward. It queries an accessor without invoking its getter and
verifies that repeated warmed Has/Own checks add no GC objects. The callback and
cross-tier global-write regression cases also pass. Evidence:
`presence_before.log`, `presence_after.log`.

Three-pair diagnostic comparisons against the pre-goal control show
formatted-output **1.1164×** and parse-integers **1.1070×**, down from the earlier
**3.5964×** and **3.4400×** screens; raytrace falls from **1.9639×** to **1.1667×**.
Fasta remains **2.0652×** and is retained as a loss. These diagnostic rows do not
replace the final full-population qualification. Raw samples and hashes:
`presence_refinement_ab/comparison.json`; the preceding capture is archived in
`pre_presence_refinement/`.


### Delegated-call numeric ownership — 2026-10-30

The completed preceding capture (`pre_generator_bridge_refinement/`) retained
AST exceptions in `jq_mix` and `jq_tree`, distinct from control timeouts. A
depth-twelve, one-round jq tree returns checksum **12286** in the pinned control
and fails in the candidate. A smaller pair of zipped delegated generators also
reproduces the failure. The failed allocator breakpoint shows **65,536 live
numeric homes out of 65,536 capacity** (`jq_numeric_failure.log`).

The shared Promise/iterator call bridge copied each argument into a fresh number
home without retiring that window. Repeated `next(undefined)` calls exhausted
the parked caller activation. Delegate to the existing
`mvp_lmd_function_forward` kernel, which snapshots arguments, roots callbacks,
retires its numeric window and preserves the returned scalar. An optional
import program retains the bridge's exact native integer conversion and
callable identity service. This reuses one ownership kernel rather than adding
a second restoration scheme (**D5.3.3**, **D6.2.2v2**, **D8.1.3v43**).

`DelegatedCallsRetireArgumentHomes` exercises 40,000 zipped steps with fractional
yields and signed-zero returns in AST and MIR. The reduced jq tree and the
standalone fractional reproducer pass after the repair. The final runtime gates above include this repair. The preceding capture remains
archived as intermediate evidence; the final population capture is recorded below.

### Delegation stack ownership — 2026-10-30

The next capture still exposed an AST exception in `jq_mix`. The full failing
sequence stops at the native stack guard while advancing nested generators
(`jq_mix_rare_errors_full.log`). Hosted AST delegation had retained each
iterator callback bridge on its caller's native stack. A generator that resumes
a 2,048-deep delegated chain reproduces the regression: the preceding candidate
throws `RangeError`, while the pinned control passes
(`generator_nested_deep_before.json`).

Extract MVP's existing synchronous delegation loop into
`mvp_lmd_generator_delegate` and use it from both park entry points. Each
delegating body now drives the protocol on its own activation. The AST adapter
translates its return/throw completion signals at that boundary; cached `next`,
live abrupt-method lookup, result-object identity and precise roots retain the
same shared protocol (**D5.3.3**, **D6.2.2v2**, **D8.1.3v43**). Remove the obsolete
hosted branches from the external recursive driver.

`NestedDelegationUsesTheDelegatingActivation` passes in AST and MIR and checks
the depth reproducer, fractional and signed-zero values, cached `next`, delegated
result identity and return/throw/finally behavior. The focused generator suite
passes **17/17**; the new case also passes with collection forced at every
allocation and freed-memory poisoning. The exact release jq implode and repeat
diagnostics each return **1024**; the reduced jq tree returns **12286**. The
aggregate gates and no-MIR qualification above include this repair. The full
controlled release capture is recorded below.

The first refreshed baseline passes **8,463/8,463**, but forced collection catches
a cold-marker defect in `ForwardsAstGeneratorReturnThroughDelegatedIterator`.
Recognizing a return signal queried the throw marker first; creating that
previously absent marker could collect the temporary return signal. Signal
recognition now reads the existing realm slot without allocating. An absent
marker cannot identify a signal of that kind. Creation still uses the rooted
shared signal constructor (**D5.3.3**). The failing forced-GC result is retained
in `gc_delegate_missing_return_before.log`. All **15/15** forced-GC generator
cases pass afterward (`gc_delegate_marker_after.log`), and the final runtime
gates above include this ownership repair.


### Shared reducer operation ingress — 2026-10-30

The first closed population capture retained a MIR hyphenation loss: 57.489
versus 48.461 ms, or **1.1863** candidate/control. That workload invokes a tiny
`reduce` for each character. Warmed optimized sampling attributes substantial
work to the reducer and callback/root path; this is diagnostic evidence from
the preceding candidate, not final timing evidence.

The shared reducer had entered native length/property and callback adapters for
MVP receivers. Pass its existing `JsArrayOps` for length, HasProperty/Get,
callability, errors and Call. Extract the common MVP callback adapter before its
third use; arrays, collections and typed arrays now share it. Ordinary dense
reads retain the shared storage admission and trace events, and every iteration
revalidates after callbacks (**D8.1.3v43**, **D6.2.2v2**).

Own the initial accumulator before observable length access. The
`SharedReduceOwnsInitialValueBeforeObservableLength` regression supplies an
unrooted native argument array, drops the external initial-value root, and
collects in both length/index getters and the callback. Both reduce directions
pass in AST and MIR with forced collection and freed-memory poisoning
(**D5.3.3**).

Intermediate seven-pair isolation against the preceding candidate recovered
4.35% through property operations and about 5.34% with the callback operation
included. Artifacts are `array_property_ops_ab/` and `array_call_ops_ab/`.
These isolate adapter cost; they do not establish full recovery against the
original pre-goal control. A further combined-present-element experiment showed
no added benefit and removed an established optimization trace. The unchanged
optimization test caught that regression; the experiment was reverted. Its
failed baseline is retained in `rejected_present_trace_baseline/`. The final
full-population results below own the performance claims for the final source.


### Final controlled release measurements — 2026-10-30

All captures have exited zero and closed with stable source corpus, binaries,
native modules, contract manifest and source inputs. Every successful sample
matches its output oracle. The 71 standard and 17 micro contracts replay the
recorded Result10 sources, timings and oracles; only the retired CLI selector
is removed. No benchmark body, golden, timeout or Test262 harness is changed.

The runner alternates AST/MIR order by row and round and alternates the
candidate/control/control-peer order. Each sample uses a fresh process, with
no warmups. Eight representatives have seven pairs; all 88 contracts have
up to three pairs, with a 30-second process cutoff and an explicit stop after
an entirely unusable first round. Builds, tests and profiling finish outside
the timed campaign. Ratios below are candidate/control execution time.

| Mode | Complete paired rows | Standard | Micro | Geometric mean ratio |
|---|---:|---:|---:|---:|
| AST | 77/88 | 60/71 | 17/17 | 0.74342 |
| MIR | 84/88 | 67/71 | 17/17 | 1.00166 |

AST uses **25.66% less execution time** on its 77 complete paired rows.
The standard-only ratio is **0.74266** and micro-only ratio **0.74609**.
MIR is **+0.17%** overall on its 84 complete rows (standard **1.00270**,
micro **0.99760**). These are population point estimates, not a claim of
equivalence for every workload or a speedup across all 88 contracts.

| Seven-pair representative | AST ratio | MIR ratio |
|---|---:|---:|
| awfy/richards | 0.6597 | 1.0041 |
| beng/regexredux | 0.3585 | 1.0039 |
| kostya/matmul | 0.5393 | 0.9993 |
| r7rs/fib | 0.7687 | 0.9907 |
| r7rs/fibfp | 0.7719 | 0.9953 |
| r7rs/sum | 0.6818 | 0.9963 |
| r7rs/sumfp | 0.7615 | 1.0000 |
| text/microdiff | 0.4553 | 1.0227 |

Important remaining losses are retained. AST Fasta is **1.9604**, strings
**1.4342**, Splay **1.2472**, sieve **1.2433**, raytrace3d **1.1754** and
MVP calls **1.1642**. MIR hyphenation is **1.1008**: **49.283 → 54.250 ms**,
with a three-pair 95% interval **1.0988–1.1216** and a control-peer interval
**1.1021–1.1233**. Its preceding candidate was 1.1863; the operation-ingress
refinement reduces the loss but does not eliminate it. MIR microdiff is
**1.0208** in the full screen and **1.0227** in the seven-pair sample.
Tiny differences in three-pair screens need longer confirmation. All row
ratios, both control comparisons and intervals remain in the raw JSON.

AST has 78 candidate-complete and 78 control-complete rows, with 77 in common.
The unmatched rows are Havlak (candidate completes, controls time out) and
matrix statistics (one candidate timeout). The remaining incomplete AST rows
are Collatz, diviter, four jq workloads, log pipeline, text search and three-way
merge. MIR has only the four jq timeout-limited rows. Every non-success status
is a timeout; no unexpected exit or oracle failure is accepted. Incomplete rows
are excluded from aggregates. The process cutoff includes startup/compilation,
so it is not an execution-time comparison for an unfinished workload.

On the 77 same-campaign common rows, candidate AST/MIR is **59.17×**
by geometric mean (**63.85×** median). Alignment does not close the interpreter/JIT
gap. Historical Result10 MIR timings remain per-row context, not the causal
control for this change.

Startup is measured separately with 21 pairs per mode on source `0`.
AST median process wall time is **7.6896 → 9.1609 ms** (+1.4713 ms); MIR is
**7.1878 → 7.2223 ms**. Zero-duration MIR execution samples use the recorded
wall-time fallback only in this startup experiment. They are not substituted
into steady execution ratios.

Artifacts: `performance_summary.json`, `performance_summary.md`,
`population_{ast,mir}.json`, `population_{ast,mir}_focused.json`, `startup.json`,
`performance_pipeline.json` and `source_manifest.json` under
`temp/js_interp_proposal_goal/`. Failed/interrupted/intermediate captures stay
archived and are excluded from final claims.

### Final optimized profiles and J4 disposition

Nine final-source AST workloads use the same optimized release objects with
local symbols retained. Each is sampled for four seconds at one-millisecond
intervals, after timing has exited. Repeated/scaled Lambda-owned drivers keep
the selected algorithm active; processes are intentionally stopped afterward.
Formatted output repeats the original checked driver twenty times. These are
CPU diagnostics, not additional performance timings.

The parser identifies the active JS execution worker, excluding the blocked
main coordinator and idle workers. It subtracts immediate children to derive
self samples; their totals equal the worker sample count in every profile.

| Workload | Worker samples | Named copy self samples |
|---|---:|---:|
| sum | 2824 | 1 |
| fib | 3265 | 0 |
| matmul | 3388 | 0 |
| richards | 3352 | 0 |
| microdiff | 3360 | 3 |
| regexredux | 3413 | 26 |
| formatted_output | 3351 | 6 |
| matrix_statistics | 3346 | 0 |
| fasta | 3419 | 15 |

Named memcpy/memmove/bcopy helpers account for **0–0.762%** of worker self
samples. They include data movement unrelated to frames. Sum attributes
**43.1%** to walker/inlined work, fib **23.6%** to calls/roots/realm, and
matrix statistics **25.8%** to binding/environment helpers. Regexredux is
dominated by native backtracking: `bt_match`, `bt_run` and `bt_match_disj`
account for **74.1%** together. Fasta also retains hashing, property, allocation
and binding work; its measured loss is not evidence for a cold-frame rewrite.

**J4: condition not triggered.** The measured profiles do not establish frame
copying as a dominant cost, so the proposal's conditional cold-state split is
not implemented. The completed immutable local/temporary plans and invoke
changes remain bounded by their precise-root and observation rules
(**D5.3.3**, **D6.2.2v2**, **D8.1.3v43**, **D8.4.1v2**).

Inlined copies remain attributed to enclosing walker symbols; these samples
do not prove that all copy cost is absent. Symbol-name categories are coarse
(native regexp backtracking is classified under other runtime/system), and
single four-second samples are qualitative. The additional warmed MIR hyphen
profiles use ten seconds of warmup and four seconds of sampling per lane.
The final candidate shows call/root, property, regexp and unresolved JIT work,
but the stripped control prevents complete function-level causal attribution
of the remaining 10.1% loss. No universal MIR preservation claim is made.

Artifacts: `profiles_final.json`, `profile_final_*.txt`, `profile_final_build.json`,
`profile_findings.json`, `profile_assessment.json`, `profiles_hyphen_mir.json`
and `profile_hyphen_mir_*.txt`. The assessment records raw-profile hashes, the
qualified source-manifest hash, evidence and limits. The requirement audit maps
all eleven proposal requirements; the completion audit checks pins, source
closure, gates, complete captures, profile provenance and document checks.
