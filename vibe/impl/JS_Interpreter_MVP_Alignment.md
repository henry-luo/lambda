# JS interpreter alignment with formal MVP

**Date:** 2026-10-18; tuning proposal added 2026-10-29

**Authority:** **S1.11**, **D1.3v3**, **D2.2.5**, **D5.2–D5.3**, **D6.2.2v2**, **D8.1.3v42**, **D8.2.3–D8.2.4v2**, **D8.4.1v2**.

**Status:** direct-eval and receiver-ownership migration implemented and behavior-qualified on 2026-10-19. That aggregate Test262 run retains eight timing partials. The 2026-10-29 scalar-helper/loop-fact and runtime-only realm slices are implemented; their qualification is recorded below. Full AST/native operation-profile replacement remains outstanding.

## Audit

The retained walker is partly aligned already. It is neither the retired MIR
compiler nor a separate Lambda heap. The operation profiles remain distinct.

| Area | Live implementation | Finding |
|---|---|---|
| Values and lexical storage | `js_interp_env.h`, `runtime/gc_environment.h`, `owned_item_slot_*` | Lambda `Item`, durable slots, numeric ownership and GC tracing are shared. JS lexical metadata remains language-specific under S1.11. |
| Runtime ownership | `js_interp_execute_script`, `RuntimeExecutionScope`, `RuntimeModuleStateScope`, `RootFrame` | Same Runtime, EvalContext, heap, module-state and event-loop infrastructure as Lambda. |
| Formal MVP dynamic source | `mvp_lmd_dynamic.cpp`, `JsInterpHostOps`, `js_interp_execute_host_eval` | MVP supplies its property, call, class, array and error operations to the existing walker. Direct eval already receives live `JsInterpEnv` cells. |
| Explicit AST/AUTO | `js_interp.cpp`, native `js_*` kernels | Still uses native JS globals, object metadata, arguments and callable capabilities. These have active DOM/host/module consumers and cannot be deleted as unreachable legacy code. |
| Promotion | `mvp_lmd_compile_function_satellite`, `mvp_lmd_compile_loop_satellite` | Already targets formal MVP. Ordinary dynamic source remains interpreted. Native prototype/callable compatibility is still used by AUTO satellites. |
| Retired direct-eval bridge | Former `JsEvalBindingJournal`, `JsEvalLocalState`, bridged interpreter branches | Consumers survived after all frame/binding producers were removed with the old compiler. No live caller supplies the old direct-eval flags. Removed in this migration. |

The native profile does not justify copying Lambda's language operations:
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

## Remaining migration boundary

Explicit AST/AUTO still has a native JS realm and callable profile. This pass
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

The current forced-AST release screen is compared with the latest recorded
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

### Current helper alignment

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
- **D8.1.3v42** currently records distinct MVP/native profiles. A completed
  convergence must revise that ruling and this working record together under
  the formal-document version/index procedure. This section is a proposal and
  does not claim that migration has happened. JavaScript semantics remain
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
