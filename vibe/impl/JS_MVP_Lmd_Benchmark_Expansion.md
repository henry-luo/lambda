# JS MVP: sixteen-benchmark expansion and self-timing

**Date:** 2026-10-09; validation updated 2026-10-10.

**Status:** implementation complete; all 76 workload oracles and timing contracts pass on macOS. Full aggregate acceptance remains pending the existing Lambda baseline failure and the platform/design checks in §8.

**Design:** [JS_MVP_Lmd §29](../jube/JS_MVP_Lmd.md#29-benchmark-expansion-and-script-self-timing).

**Control:** [MVP_Result8](../../test/benchmark/js_mvp_lmd/MVP_Result8.md), with its exact binary, sources and per-row timing contracts.

## 1. Scope and authority

Complete the original eight proposed additions and eight further targets. If
all pass, coverage becomes **76 workloads: 59 standard + 17 microbenchmarks**,
up from Result8's **60: 43 + 17**. The current standard inventory contains 71
unique IDs; 12 remain outside this phase. The three numerical Julia targets
previously passed correctness checks, but their timing mismatch prevented their
inclusion in the measured population.

The implementation follows **D1.3v3** (shared substrate), **D2.6.9v3** (nominal
Lambda Map objects), **D3.4.3v5/D3.4.5** (shape transitions and packed layouts),
**D5.2–D5.3** (scalar ownership and precise roots), **D6.2.2v2–D6.2.4**
(call/construct entries and closure environments), **D7.3.1–D7.3.4** (host/module
boundary), and **D8.4.1v2/D8.4.3v2** (dispatch and explicit completions).
See the [formal design](../../doc/Lambda_Formal_Design.md). This plan does not
amend these rulings or claim full ECMAScript conformance.

Keep ordinary objects and Map on Lambda storage, canonical UTF-8 key identity,
and Lambda-compatible nominal ancestry. JS mutable captures continue to reuse
the existing Function/environment machinery; Lambda captures remain immutable.
No full LambdaJS runtime helper may enter the dependency graph, directly or
transitively. No VMap, conservative roots, new value tags or vendor changes.

Excluded: `__proto__`, reassignment of prototype links, accessors, proxies,
general property descriptors, private/static initialization blocks, BigInt,
RegExp, general modules/Node APIs, DOM, eval and async. Sloppy calls with a
primitive receiver fail explicitly until wrapper objects are supported; strict
functions retain primitive receivers. The iterable addition is
bounded to existing arrays and ordered Map entry views, without user-defined
`Symbol.iterator` dispatch. Unsupported operations remain explicit admission or
capability failures, never silently substituted behavior.

## 2. Target and acceptance matrix

The first eight rows are the original proposal; the last eight expand it.
Canonical IDs and inputs come from
[the current inventory](../../test/benchmark/js_mvp_lmd_manifest_v1.json).

| # | Canonical ID | Required support / audit | Workload and correctness contract |
|---:|---|---|---|
| 1 | `awfy/deltablue` | Public static class fields | Original bundle, initializer order, native constraint-solver verification |
| 2 | `awfy/json` | Static fields; UTF-16 `substring` | Original bundled JS parser and result verification; native `JSON.parse` is unnecessary |
| 3 | `text/text_search` | `Array.map` | Preserve corpus preparation before the timer and exact search outputs |
| 4 | `text/three_way_merge` | Literal `String.split` | Preserve input preparation, original merge work and output oracle |
| 5 | `text/log_pipeline` | Split, string slice/indexOf; primitive `Number`/`String` | Original record counts, pipeline outputs and timed boundary |
| 6 | `julia/parse_integers` | Timing integration; audit existing numeric path | Untimed verification/warmup, one timed workload, expected result vector |
| 7 | `julia/iteration_pi_sum` | Timing integration | Same Julia wrapper and expected result vector |
| 8 | `julia/matrix_statistics` | Timing integration | Same Julia wrapper and expected result vector |
| 9 | `awfy/havlak` | Admission, correctness and runtime audit | `innerBenchmarkLoop(1)`: 1,605 loops and 5,213 nodes; bundled `Set` is a user class |
| 10 | `beng/fasta` | `parseInt`, script arguments, real console output | Default N=1,000; exact headers and sequence output, including original ALU data |
| 11 | `beng/revcomp` | UTF-8 file read, uppercase, in-place reverse | Canonical FASTA input read before timer; exact reverse-complement output |
| 12 | `beng/knucleotide` | Map-entry spread, comparator sort, startsWith, toFixed | Canonical input; exact frequencies and counts, numeric ordering and lexical ties |
| 13 | `julia/formatted_output` | Synchronous UTF-8 writes and formatting | 100,000 lines; result `[1177795, 584298900, 391, 100000]`, including actual writes |
| 14 | `jetstream/cube3d` | Ordinary constructors/this, Object construction, named array properties, Math.round | Original setup and `runIteration` workload; retain native checks and result oracle |
| 15 | `jetstream/navier_stokes` | Constructor-created closures/methods; ordinary receiver semantics | One timed frame; shared post-timing frame-15/checksum and density-digest oracle |
| 16 | `jetstream/splay` | Constructor own properties, prototype data methods, Math.random, callable performance.now | 8,000 setup nodes; original 50 runs of 80 modifications; post-timing tree verification |

Havlak remains an audit target until execution confirms support; do not invent a
new feature merely to attach one to that row. Record additional blockers found
in complete sources before enlarging the admitted surface. Do not strip unused
library declarations to make admission pass.

## 3. Timing and host contract

### 3.1 Reuse the Lambda clock

Lower `performance.now()` to `(pn_clock() - execution_origin_seconds) * 1000`.
`pn_clock` is declared in `lambda/lambda.h` and implemented in
`lambda/runtime/lambda-proc.cpp`. Initialize the origin once per execution,
before user code; retain fractional milliseconds and monotonic differences.
Reuse `lib/time_util.h` where applicable. Do not introduce a second timer
implementation or import full-JS performance helpers.

The implementation replaces the Windows `timespec_get(..., TIME_UTC)` fallback
with the existing shared `time_now_seconds` platform clock, preserving Lambda's
seconds unit. Windows behavior still needs platform-specific validation.

Splay reads `performance.now` as a value. Supply a stable callable through the
existing Function call machinery, with both value reads and calls supported;
a call-only compiler recognition is insufficient. Initialize timing state only
for executions using the host binding; ordinary programs should gain no hot-loop
branch, allocation or extra per-call root.

### 3.2 Preserve script timing boundaries

The MVP CLI `--timing` currently measures the entire generated entry after
compilation. It includes setup and any in-script warmup. The new 16 run their
canonical self-timed scripts **without `--timing`**. Keep the existing CLI option
for old wrappers and diagnostics; it must not add a second `__TIMING__` record
to these runs.

Require exactly one finite, positive `__TIMING__` value in milliseconds, a
successful exit, and a passing semantic oracle. Reject missing, duplicate,
malformed, zero-resolution or partial-failure records. Do not fall back to
process elapsed time. The current `parse_timing` returns the first match:
extend shared parsing with explicit strict validation for this profile.

Freeze setup, warmup, repetitions and oracle placement from the canonical
source/wrapper. Retain compilation and process wall time separately; disclose
that Node lazy compilation/tiering inside the timed region is included, while
MVP initial MIR compilation is outside it. Do not add a benchmark-name-specific
timed entry or increase iteration counts for just one engine.

Special contracts:

- Julia uses `microVerify(microWorkload(...))` before timing, one timed workload,
  and post-timing verification from `julia/micro_common.js` and `expected.json`.
- Text corpus setup stays outside the timer. BENG file reads stay outside; its
  result formatting and stdout remain where the source puts them inside it.
- `formatted_output` performs all **391 writes** to `/dev/null` or Windows
  `NUL` in the timed workload. A checksum-only replacement is not equivalent.
- Navier–Stokes reuses `jetstream_post_timing_oracle`: 14 additional frames
  after the timed first frame, native checksum 77 and density digest
  **-257786486**. Never time those additional oracle frames by accident.
- Splay setup remains outside timing. Its source expects seeded randomness,
  but the current generic Node wrapper does not supply it. Freeze one shared
  deterministic random fixture for MVP and Node, with seed and wrapper hash;
  label this as a new measurement contract. Call `SplayTearDown` afterward to
  verify count/order. Retain the benchmark's internal clock reads.

### 3.3 Explicit host environment and real I/O

Provide the bounded benchmark environment: `console.log`, `performance.now`,
`process.argv`, `process.platform`, `process.stdout.write`, and synchronous
`require('fs')` reads/writes used by these sources. These are explicitly supplied
host/module bindings, not additions to the engine's core globals allowlist
(**D7.3.1–D7.3.4**). Audit the existing embedding/bootstrap and module boundary
before choosing the adapter. General CommonJS loading is outside this phase.
If this requires changing a formal boundary, obtain a ruling before coding it.

Audit `pn_print`, `pn_output2/3`, `fn_input1/2` and the shared stream/file core
first. Select raw UTF-8 text explicitly; Lambda's automatic input parsing is
not `readFileSync(path, 'utf8')`. Output must be length-aware, preserve embedded
NULs, use JS scalar conversion, and perform real writes. Keep read/write errors
as explicit completions (**D8.4.3v2**). Never hardcode input contents, paths or
synthetic successful writes in the runtime.

## 4. Implementation stages

Stages 1–4 have source changes under validation; the mutable-cache discrepancy and aggregate gates remain open. Runtime paths below are relative to
`lambda/js/mvp-lmd/` unless qualified otherwise.

### Stage 0 — freeze contracts and audit reuse

- Freeze Result8's accepted candidate as the new control using its JSON
  provenance and SHA-256
  `5f0ce0ed8d41059e76a65ac9f972f95c0daed9d42ccedb1b419c0c779d10ce16`.
  An arbitrary current `lambda.exe` is not a substitute.
- Generate/hash all 16 complete JS sources, wrappers and data inputs using
  `run_benchmarks.py` expansion and JetStream wrapper helpers. Keep Result8's
  exact 60 workload wrappers for regression comparisons.
- Record each unsupported syntax/API, shared Lambda candidate, semantic
  difference, allocation effect, error ABI and transitive dependency closure.
- Resolve the host binding interface and the property-cache issue in §8.
  Disclose the final new-helper inventory before implementation.

Exit: reproducible 16-row contracts, verified control provenance and explicit
feature/helper boundaries. No support count is advanced at this stage.

### Stage 1 — timing, host output and the first three additions

Files: `mvp_lmd_mir.cpp`, `mvp_lmd.h`, `mvp_lmd_runtime.{h,cpp}`,
`lambda/main.cpp`, shared clock/stream code, and benchmark runners.

Integrate execution-scoped host bindings, `pn_clock`, callable timer values and
real scalar console output. Pass script arguments through the CLI/execution
boundary without changing existing invocations. Keep host setup outside script
timing and ensure successive executions do not share origin or mutable state.
Add strict timing validation to shared runner code. Admit and measure Julia
`parse_integers`, `iteration_pi_sum`, `matrix_statistics` with their canonical
warmup and oracles.

Exit: 3/16 timing-valid additions; timer/output tests pass; old entry-timing
contracts and no-feature fast paths remain intact.

### Stage 2 — static fields, strings, conversions and array callbacks

Files: `mvp_lmd_mir.cpp` (`walk`, `class_plan`, `mvp_lmd_class_new`,
`sequence_method_call`, `sequence_bounds`, `array_foreach_call`, conversions),
`mvp_lmd_runtime_classes.cpp`, `mvp_lmd_objects.cpp`, shared string routines.

- Public static fields initialize once in source order, with the class's inner
  name/receiver available and outer lexical TDZ respected. Separate writable,
  enumerable fields from protected constructor metadata and nonenumerable
  methods. Preserve inherited reads, subclass shadowing, deletion/reinsertion
  and roots across allocating initializers.
- Implement `substring`, string `slice`, literal `split`, `indexOf`,
  `startsWith` and uppercase with JS bounds/coercions. Audit `fn_substring`,
  `fn_split_literal_items`, `fn_split`, `fn_index_of_raw`, `fn_upper` and
  `lib/utf.h` first. Lambda substring/search use code points; JS uses UTF-16
  code units. The current literal-split fast path is nonempty ASCII only.
  Reuse ASCII paths, then share UTF-16 traversal including lone surrogates.
  Preserve Lambda split semantics (**S17.1.1**), including its distinct null
  separator behavior, through an explicit option or narrow adapter.
- Generalize the existing callback traversal for `Array.map`: snapshot length,
  skip holes, preserve holes in the result, read live elements, pass
  `(value, index, array)` and `thisArg`, and propagate callback failures.
  Avoid duplicating `forEach` traversal and call/root logic.
- Reuse existing `to_number`/`to_string` lowering for primitive `Number` and
  `String` calls. Add `parseInt` only with audited radix, prefix, whitespace,
  trailing-junk and NaN semantics; strict Lambda numeric parsing is not a
  drop-in replacement.

Exit: DeltaBlue, JSON and the three text targets complete the original **8/16**;
feature edge fixtures and unchanged Lambda defaults pass.

### Stage 3 — I/O, mutable array operations and numeric formatting

Files: MVP runtime/compiler above, `lambda/runtime/lambda-proc.cpp`,
`lambda/runtime/lambda-eval.cpp`, existing shared collection/formatting modules.

Implement the bounded host I/O surface from §3.3. Add in-place `reverse`, stable
comparator `sort`, Map-entry spread and `Number.toFixed`. Audit `fn_reverse`,
`fn_sort1/2`, `fn_sort_by_keys` and shared numeric formatting before adding
code: Lambda copying behavior/default order and JS callback semantics differ.
Reuse a shared algorithm with options or a thin adapter where possible.

Reverse retains array identity, aliases and holes. Sort preserves stability,
hole/undefined placement, observable comparator calls and explicit failures;
root the comparator and values during allocation/callbacks. Map-entry spread
uses existing ordered iteration/storage and materializes observable entry-pair
arrays. Do not add a second Map order table. `toFixed` needs JS rounding and
range behavior, not an unqualified C formatting shortcut.

Exit: FASTA, revcomp, knucleotide and formatted_output reach **12/16** with
exact output and actual I/O. The null-device output case also checks write
count/bytes/digest, not just the returned checksum.

### Stage 4 — ordinary construction and prototype data properties

Files: compiler function/receiver/new lowering, `mvp_lmd_runtime_classes.cpp`,
`mvp_lmd_objects.cpp`, existing Function and precise container tracing code.

- Reuse existing Function call/construct entries, receiver ABI,
  `mvp_lmd_class_invoke`, constructor-result handling and closure environments
  where applicable (**D6.2.2v2–D6.2.4**). Object returns replace a constructed
  receiver; primitive returns do not. Arrows remain nonconstructible; class
  constructors remain distinct from ordinary callable functions.
- Support member receivers and strict/sloppy ordinary calls as admitted by
  the sources. Navier's bare-call `this.result` needs the execution's sloppy
  global receiver; constructor-created closures must retain their bindings.
  Preserve lexical arrow `this` and existing class `super` behavior.
- Store constructor own data properties and `.prototype` data fields/methods
  using Lambda maps/shapes. Keep instance ancestry aligned with TypeNominal.
  Changes to prototype data must affect already-created instances; same-shape
  method replacement must not leave a stale direct-call target. Prototype-link
  reassignment remains excluded and must fail visibly.
- Use the existing, precisely traced Map attribute face on Array for named
  properties (**D2.6.6v3**). Install its shared shape lazily; preserve indexed
  storage/length and the layout of arrays without named properties.
- Audit Lambda numeric/random primitives for `Math.round`, `Math.random` and
  `Object()`/`new Object` construction paths. JS round ties toward positive
  infinity and preserves negative zero; generic C `round` is insufficient.
  The deterministic Splay fixture belongs to the shared benchmark environment.

Exit: Cube3D, Navier–Stokes and Splay reach **15/16**; prototype mutation,
receiver semantics and GC ownership pass without regressing class fast paths.

### Stage 5 — Havlak and aggregate acceptance

Run the complete Havlak bundle with its canonical inner count and native oracle.
Use the existing class/closure/array paths; investigate concrete failures before
adding surface area. Complete the **16/16** contracts, full 60-row regression
matrix and shared-runtime gates. Update the shipped feature/admission table
only after these gates; a capability implemented in source is not a measured
benchmark pass.

## 5. Proposed helper inventory

This is advance disclosure of likely additions, not a fixed implementation list.
Before coding, replace conditional entries with exact names, signatures,
dependencies and allocation/error effects and notify the user of each addition,
including compiler utilities. Reuse/promote an existing helper before adding one.

| Area | Preferred reuse/extension | Conditional new helper responsibility |
|---|---|---|
| Timing | Import `pn_clock`; existing per-execution state and Function entry | No new clock implementation; a callable ABI adapter only if existing entries cannot express the host binding |
| Static fields/construction | `class_plan`, `mvp_lmd_class_new`, class property/invoke/result paths | Shared ordinary-construction planner only if required after factoring existing class lowering |
| Strings | Existing Lambda string functions, `sequence_bounds`, UTF-16 iterator, current string-at logic | One shared JS UTF-16 range/search adapter if a compatible option cannot cover the semantic difference |
| Callbacks/arrays | Generalize `array_foreach_call`; shared reverse/sort core and ordered Map iteration | Shared comparator-call bridge or iterable materializer only where existing call/iteration lowering cannot be reused |
| Numeric conversion/format | Existing conversion lowering and Lambda/lib parsing/formatting | Narrow JS parseInt/toFixed adapters after semantic audit; no copied full-JS implementation |
| Host services | Shared clock, stream/file operations and module/embedding boundary | A bounded host-call adapter with explicit completions, if required by the existing ABI |
| Named array properties | Existing extension/tracing hooks plus Lambda Map storage | Lazy property-store access/trace helper only if no suitable existing extension exists |
| Harness | Canonical expansion, timing, output-oracle and paired-statistics helpers | One checked-in expansion driver; strict timing parsing should extend the shared implementation |

Adapters must preserve rooted argument spans, destination-owned scalar storage
and ordinary error returns. No wrapper may conceal a full LambdaJS helper in
its dependency closure. Keep profiling counters out of performance runs.

## 6. Planned validation

Add focused cases to `test/test_js_mvp_lmd_gtest.cpp` during implementation:

| Area | Required edge coverage |
|---|---|
| Timer/host | Monotonic differences, fractional units, per-execution origin, timer value/call, absent/duplicate timing rejection, real stdout/file errors, UTF-8 and embedded NULs |
| Static fields | Ordering, self-reference/TDZ, inherited read and shadowing, reflection, delete/reinsert, initializer failure/GC |
| Strings/conversion | Empty/missing/null separators, limits, negative/swapped/NaN/infinite bounds, surrogate pairs/lone surrogates, search offsets, parseInt radices/junk, unchanged Lambda defaults |
| Arrays/Map | Callback mutation and holes, thisArg/captures, reverse aliases, stable sort ties/holes/undefined and failures, entry order and fresh pair arrays |
| Formatting/I/O | toFixed rounding/range/nonfinite values, output bytes and write batches, input errors, argument paths and platform sink |
| Construction | Return override, arrows/classes, strict/sloppy/member this, lexical captures, function own fields, prototype replacement of data methods on existing instances |
| Storage | Named properties vs indexed length, deletion/retyping/aliases, retained containers after forced collection, scalar ownership |
| Exclusions | Explicit rejection of prototype-link mutation, descriptors/accessors/proxies and unsupported iterator/module surfaces |

Run focused cases normally and with forced GC. New Lambda semantic-option
fixtures require both `.ls` and `.txt` goldens. The full LambdaJS Test262 gate
checks shared-runtime regressions; it does not establish MVP Test262 coverage.
Do not alter the Test262 runner to suppress failures.

Commands for implementation closeout, from the repository root (actual runs
and deviations are recorded below):

```sh
make build-release-compile
make build-test
./test/test_js_mvp_lmd_gtest.exe
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./test/test_js_mvp_lmd_gtest.exe
make test-lambda-baseline
make test262-baseline
make build-release-compile
python3 test/benchmark/verify_js_mvp_manifest.py --manifest test/benchmark/js_mvp_lmd_manifest_v1.json
```

Read [Developer Guide §7](../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas)
before building. Never use `make release` in a worktree. Restore and hash-check
the release binary after gates; use the exact accepted binary for measurement.
Regenerate the manifest with `generate_js_mvp_manifest.py --profile mvp_lmd_v1`
when wrapper contracts change; generated membership is not a support result.

## 7. Performance protocol and artifacts

Create `test/benchmark/js_mvp_lmd/expansion.py`, reusing
`run_benchmarks.py`, `run_paired_benchmarks.py` and the existing MVP capture
drivers. Keep two populations: `new16` with canonical script timers, and
`prior60` with frozen Result8 wrappers. The Result8 control lacks new host
features and must not be run on incompatible new wrappers.

The driver records exact commands and fails if any required oracle or timing
contract is incomplete. Add `--lambda-reference` to capture canonical Lambda
ports; their annotation and workload/timing differences are recorded per row:

```sh
JS_EXEC_BACKEND=mir JS_EXECUTION_BACKEND=mir JS_MIR_INTERP=0 \
LAMBDA_JS_LARGE_INTERP=0 LAMBDA_EXEC_BACKEND=jit LAMBDA_TIER=jit \
python3 test/benchmark/js_mvp_lmd/expansion.py \
  --candidate lambda.exe \
  --control temp/mvp_expansion/control.exe \
  --population all --runs 15 --out temp/mvp_expansion/screen
```

Freeze the control at that path in Stage 0 and record the candidate hash before
running. The driver runs MVP/Node/canonical untyped Lambda references for new
rows, labeling any typed-only port or unmatched workload rather than calling it
untyped. Verify full result vectors/output, not merely exit status or the timing
marker. Existing whitespace-normalized output helpers are insufficient for
byte-sensitive BENG/I/O rows; strip only the timing record for their exact check.

Use fresh processes, identical warmup policy, balanced candidate/control/
identical-control-peer order, raw samples and paired confidence intervals. Run
on an otherwise quiet host with no concurrent builds/tests. Screen all prior
60 rows, then confirm suspected regressions with 30–90 paired rounds and
control/control noise checks. Retain separate longer-run artifacts rather than
silently replacing selected cells of a snapshot.

Pay particular attention to CD, Richards, Bounce, map_lookup, object_delete,
json_gen, dense arrays and scalar/recursive workloads. Inspect emitted MIR and
allocation/root counts separately when attributing a change. New host services
must not impose unconditional allocation, receiver checks or property metadata
on existing programs. No confirmed slowdown is accepted on the strength of a
better geometric mean. Noisy intervals remain unresolved until adequately
remeasured; a short screen cannot prove zero regression.

Retain executable/source/wrapper/input hashes, revision plus patch, runtime
versions, command/environment, timings, process costs, raw stdout/stderr,
oracle outcomes and gate logs under `temp/mvp_expansion/`. Do not splice new
self-timed numbers into historical Result8 cells. Publish a new result only
after acceptance; preserve Results3–8 and their provenance.

## 8. Open checks and completion criteria

1. **Host/module boundary:** finalize the execution-scoped injection API and
   fs/process facade under **D7.3.1–D7.3.4**. No implicit expansion of core
   globals or general Node/module support.
2. **Existing mutable property caches:** `MvpLmdPropertyCache` and
   `mvp_lmd_cache_property` currently hold mutable site state, while
   **D8.4.1v2** prohibits it. Record this as an implementation discrepancy,
   not a sanctioned exception. Resolve the affected paths with compile-predicted
    guards and shared fallback before relying on them for mutable static or
    prototype data; confirm class performance against the frozen control.
    New static Function properties and ordinary-constructor prototype data use
    shared dispatch and do not populate this state. The pre-existing class-site
    caches remain a formal-design discrepancy; this phase does not sanction them.
3. **Named array properties:** the audit selected the existing Map attribute face on Array (**D2.6.6v3**), already traced and compacted by GC. Named writes lazily install a shared shape; no extra array fields or side table. Validate index/method fast paths against the control.
4. **UTF-16 and numeric semantics:** ASCII benchmark success is insufficient
   for admitted public methods. Complete the stated edge matrix or explicitly
   narrow admission; preserve Lambda defaults when sharing functions.
5. **Portability and comparability:** validate Windows monotonic clock behavior,
   freeze the Splay random contract, and audit each Lambda port's workload and
   timer before reporting ratios. Report unvalidated platforms separately.

The phase is complete when all **16 new targets** pass their exact correctness
and timing contracts, **60 previous workloads** pass their oracles and paired
regression gates, focused normal/forced-GC and both baseline suites pass, and
the final helper/dependency inventory and evidence artifacts match the measured
release. Until then, report completed stages and remaining blockers explicitly.


## 9. Implementation log (2026-10-09–10)

The accepted Result8 executable was frozen at `temp/mvp_expansion/control.exe`
and its SHA-256 matches §4. The new `expansion.py` runner freezes
canonical new sources and preserves the exact prior 60 wrappers, rejects invalid
self-times, checks output bytes and MIR evidence, and uses shared paired statistics.
Splay uses the existing Octane Jenkins RNG body (seed 49734321) in a shared lexical
`Math.random` binding; the benchmark body is retained and teardown follows timing.

Implemented in source, pending aggregate validation:

- Explicit `MvpLmdHost` injection supplies clock, scalar output, argv/platform and
  bounded synchronous `fs` operations. `pn_clock` reuses `time_now_seconds`.
  Output uses the existing WTF-8 encoder; input is bounded to valid UTF-8 and
  malformed input fails explicitly. General replacement decoding is deferred.
- Public static fields, UTF-16 substring/slice/search/split, Array.map, primitive
  Number/String, parseInt, toFixed and Math.round. Lambda ASCII/string paths and
  existing numeric formatting remain the first choice; UTF-16 traversal and
  half-up fixed rounding are explicit shared additions.
- Reverse, stable comparator sorting through the shared index sorter, and bounded
  array/ordered-Map spread. `iterable_source` factors the existing for-of selection.
- Ordinary constructors reuse the Function receiver ABI and nominal Map ancestry.
  Lazy prototype/static metadata occupies three precise closure-environment slots.
  Live nominal instances trace their constructor owner via the neutral
  `TypeNominalExtension::owner` hook (**D5.3**, **D6.2.2v2–D6.2.4**); it adds no
  conservative scan or global root retention. Named array fields use the existing
  attribute face (**D2.6.6v3**). Shared SplitMix64 preserves Lambda's pure API while
  MVP keeps execution-local mutable state.

New helper inventory (names disclosed during implementation):

| Area | Helpers / factored entry points | Effect and dependency |
|---|---|---|
| Host | `mvp_lmd_host_initialize`, `host_function`, `host_set`, `host_clock`, `host_output`, `host_console_log`, `host_stdout_write`, `host_require`, `host_read_file`, `host_write_file`, `host_utf8_bytes` | Explicit host ABI; rooted allocating/error completions; shared clock, file and UTF libraries |
| Strings | `mvp_lmd_primitive_to_string`, `mvp_lmd_string_range`, `mvp_lmd_string_search`, `mvp_lmd_string_split`, `string_bound`; `utf8_canonical_slice`, `utf16_find` | Shared UTF-16 traversal; allocating string/array adapters return Item errors |
| Numbers | `radix_digit`, `parse_radix_digits`, `mvp_lmd_parse_integer`, `mvp_lmd_number_to_fixed`, `lambda_finite_double_to_fixed` | Shared parsing and finite formatting; raw parse is allocation-free; toFixed returns explicit error |
| Arrays | `array_reverse_in_place`, `mvp_lmd_array_sort`, `array_sort_compare`, `mvp_lmd_array_spread`, private `array_store_owned`, compiler `iterable_source` | Shared storage/sort/ordered traversal; callbacks use precise spans and explicit completion; outlined owned-store path retains roots without enlarging immediate stores |
| Construction | `mvp_lmd_function_prepare`, `mvp_lmd_nominal_initialize`, `mvp_lmd_class_values`, `mvp_lmd_nominal_owner`, `lambda_shape_nominal_owner`, compiler `object_construct` | Lazy metadata; existing Function environment and shared nominal tracing |
| Global bindings | compiler `global_binding_access` | Execution-local sloppy receiver properties through existing property get/set/has; strict writes and missing reads retain ReferenceError |
| Random | `math_splitmix64` | Factored Lambda primitive, caller-owned seed; no allocation |

Early release probes passed UTF-16 ranges/search/split, Array.map mutation/hole
traversal, and numeric parse/rounding fixtures. The first complete-target screen
found a clock adapter representation error (a number was passed to a pointer-tag
macro); this was corrected to use the shared scalar publisher. It also exposed
Function receivers missing from generic property-write dispatch. These are
implementation findings, not accepted benchmark measurements. No new coverage
count or no-regression claim is made until §8 is satisfied.

The ordinary focused suite now passes **76/76**. Full-source probes exposed and
fixed inline-receiver precedence at script/static-initializer scope, intrinsic
bindings without lexical NameEntry records, and shared deletion replay dropping
non-enumerable metadata. Cube3D additionally required Math.PI and implicit sloppy
global writes. Numeric array keys retain the direct path when proved indices;
other numeric keys defer name conversion until the named-property branch.
Sort/spread reuse the array-store scalar-home adoption path, and comparator
arguments adopt borrowed scalar tails before callbacks. Receiver-observing
ordinary functions retain the call prologue when inlined code cannot perform
the required receiver conversion. Shared baselines and release regression
measurements are still pending.

Validation checkpoint: the current ordinary and forced-GC focused runs both
pass **76/76** (`focused-fifth.log`, `focused-forced-final.log`). The Lambda
baseline passes **6,534/6,535**; `edit_view_only` changes a text leaf during the
interpreted math round-trip. An isolated release build of committed
`0744312409e82bd66009722cca6fceee396baa6e`, without this phase's patch, reproduces
the same failure, and both binaries pass the fixture under pinned JIT
(`edit-head-comparison.json`). This is an existing shared-runtime failure,
not a passing aggregate gate. The initial Test262 run recovered to
**40,261/40,261** after two worker aborts. A quiet full replay also recovered to
**40,261/40,261**, but its Unicode-identifier batch stalled. The identical
600-test batch passes **600/600** without retries on both the isolated committed
build and candidate (`test262-affected-comparison.json`). Full-suite batch
stability remains unresolved; recovered totals are not a clean gate.

All **16 new targets** pass exact output and script-timing checks in the release,
pinned-MIR three-round screen, including their canonical Lambda reference
oracles (`temp/mvp_expansion/new16-screen/comparison.json`). Each row retains
raw output, MIR evidence and source/input hashes. Navier–Stokes and Splay's
Lambda timings remain explicitly unmatched (§3.2); annotated ports are labeled.
This establishes execution/timing coverage, not regression acceptance or a
published result.

The first prior-60 screen exposed two regressions. Named-array fallback merged
numeric reads into boxed `Item` values in `triangl`; the existing write analysis
now proves when numeric nonindices cannot name an own array field, retaining
native optional numeric reads and the unsigned bounds guard. Named writes keep
the generic property path. `object_delete` replay bypassed the shared external
root dispatcher, creating shapes different from the compiler predictions.
`type_tree_follow` now reuses `type_tree_step` (**D3.4.3v5**); shape counters match
the control again, and a focused assertion checks deletion/literal shape
identity. No new runtime helper was added for either correction.

The capture driver also restored Result8's typed JSON oracle for the prior 60
CLI wrappers; comparing their rendered strings directly had falsely rejected
six rows in every lane. Exact byte checks for new self-timed scripts remain
unchanged. Failed screens are retained separately; final measurements must use
the complete corrected driver and binary.

Once those six output oracles were restored, the complete screen also exposed
extra property-key calls on immutable String methods and override dispatch on
Array methods in units without possible method replacement. The former inserted
allocating safepoints into `levenshtein`'s inner loop; the latter expanded the call/root
paths in `brainfuck` and `base64`. String/typed-array names retain their existing
conversion path. Method-write detection now participates in the existing
monotone kind solver: possible replacements widen builtin return types, while
unmodified methods retain direct calls. This also corrects a reproduced `push`
replacement returning a string being coerced to NaN, and a numeric `join`
replacement crashing through an inferred String lane. Units with possible
overrides still capture the callee before argument effects and retain guarded
dispatch (**D8.4.1v2**). Focused cases cover replacement through an alias and
replacement while evaluating call arguments.

The remaining Map lookup screen showed a small native-runtime difference with
unchanged MIR. Reordering property/receiver checks did not establish a benefit
against the identical-control noise lane; both experiments were discarded.
Known ordered-Map builtin calls now select the existing method token directly:
the admitted runtime already rejects shadowing those method names, so repeated
property dispatch is unnecessary (**D8.4.1v2**). Own data fields and unknown
receivers retain shared lookup. The existing rejection checks and a focused MIR
assertion cover this admission-dependent shortcut; performance confirmation is
still required before acceptance.

Scalar ownership review then reproduced corruption when a reversed tiny-float
array was overwritten: elements moved, but the store still chose payload homes
by logical index. The store now reuses the element's actual owned tail offset,
which shared growth rebases, or appends a new owned home. Repeated retyping
reclaims retired homes through the existing `fn_slice` copy once tail storage
exceeds twice the live length; array identity, holes and named fields survive.
Incoming borrowed payloads are captured before either copy or growth
(**D5.2.2v3**, **D5.3.2–D5.3.4**). This adds no helper or immediate-value fast-path
work. The new regression covers reversal, shared copies, sorting, growth,
shrink/regrow, retained values and bounded storage after retyping.

The stage20 release is frozen with source patch/provenance under
`temp/mvp_expansion/stage20/` (SHA-256
`7d1334b60b19d4a646c70cdee5866650b72b375dba39017b2eb48beaccb347b4`).
Normal and forced-GC focused checks pass **77/77**. The latest complete Lambda
baseline remains **6,534/6,535**, with only the independently reproduced
`edit_view_only` failure (`lambda-baseline-stage19.log`). It predates the
MVP-only scalar-store correction; the complete MVP suite was rerun afterward.
The full Test262 baseline now passes **40,261/40,261** with **zero retries and
zero worker failures** in 84.8 seconds (`test262-stage20.log`), using the same
cases/default timeouts and three workers. This is the clean gate; the earlier
recovered totals remain diagnostic history. Validation provenance is collected
in `validation-stage20.json`. Final paired measurements remain pending.

The complete stage20 prior-60 screen passed every oracle. Ninety paired rounds
then confirmed small regressions in `object_growth`, `map_iteration` and
`puzzle`; the apparent `brainfuck` and `bounce` differences did not confirm.
An experiment pruning generic collection branches and redundant scalar-key
canonicalization reduced MIR size but did not remove the slowdowns; it was
reverted (stage21 artifacts remain diagnostic). Neither object/Map probe
collected, excluding the new nominal-owner trace as their cause.

Native assembly from release bitcode exposed a shared store-path cost: the
owned-scalar correction enlarged `mvp_lmd_array_store`'s frame from 192 to
352 bytes and introduced a stack check on immediate writes. The disclosed
private `array_store_owned` helper now contains that existing slow path;
the public fast path is frameless and tail-calls it only when needed. This
retains scalar ownership and precise-root rules (**D5.2–D5.3**), with no second
storage implementation. Stage22 normal and forced-GC checks pass **77/77**;
thirty-round paired confirmation removes the `object_growth` and
`map_iteration` regressions (`confirmation-stage22/comparison.json`).

`puzzle` retained extra cold paths in recursive numeric-index writes. The
compiler now uses the existing named-key branch as its validity guard and
omits the impossible `length` assignment branch for numeric keys. Negative,
fractional, NaN and UINT32_MAX keys still reach named storage; string `length`
retains resize semantics. Focused cases cover these boundaries. This uses
existing kind facts (**D8.2.4v2**, **D8.4.1v2**), without a new helper or a
benchmark-specific range assumption.

Stage23 passed all **76 workload oracles**: a complete 15-round prior-60
screen plus a three-round new-16 self-timing/reference screen. The corrected
`puzzle` matched Result8 in the separate 30-round confirmation. The full screen
flagged `crypto_sha1`, and 90 rounds confirmed a 1.5% slowdown. Its SHA word
indices are nonnegative inside the `j >= 16` branch, but only the enclosing
loop bounds survived range analysis. The existing scoped range query now also
intersects relational `if`/`else` bounds for counted bindings whose two writes
both lie outside the guard. It reuses the shared AST containment query and
affine-bound logic (**D8.2.4v2**). Mutations inside either branch suppress the
refinement; focused value/MIR checks cover both cases. Index lowering intersects
the emitted value's interval with the read site's interval, retaining both
inline payload and conditional facts. No helper was added. A 90-round stage25
confirmation measures `crypto_sha1` at **13.997 vs 13.951 ms** (candidate/control
95% interval **0.9975–1.0088**, identical-control interval **0.9964–1.0054**),
without a confirmed slowdown. This bounds the measurement; it does not prove
zero difference.

## 10. Final release validation (2026-10-10)

The implementation is frozen in `temp/mvp_expansion/stage25/`, including its
source patch and helper sources. Release SHA-256:
`990bae9b78b747f85e8ec5053a15a3757c7b5f8f0ac3f868293dcd96dd7f876b`.
`source-verification-stage25.json` confirms that the working implementation and
root `lambda.exe` match this freeze; subsequent documentation updates are separate.

- Normal and forced-GC focused suites: **77/77** each. Forced collection and
  freed-memory poisoning remain enabled for the latter (**D5.3**).
- Full LambdaJS Test262 baseline: **40,261/40,261**, zero worker failures,
  retries or regressions, using three workers and unchanged cases/timeouts
  (`test262-stage25.log`). This checks shared-runtime compatibility, not MVP
  Test262 conformance.
- Previous **60/60 workloads** pass their exact Result8 oracles in the final
  15-round, release, pinned-MIR capture. No candidate/control two-sided 95%
  interval excludes parity on the slower side. The descriptive execution-time
  geometric mean is **0.9874×** Result8; small regressions remain possible within
  the intervals. The separate 90-round SHA-1 confirmation above remains separate
  from the matrix (`prior60-stage25/comparison.json`).
- New **16/16 targets** pass exact JS output, script-timing and canonical Lambda
  reference oracles in the final three-round screen, with one discarded warmup
  per lane (`new16-stage25/comparison.json`). Annotated ports are labeled;
  Navier–Stokes and Splay's Lambda workloads remain unmatched (§3.2). These are
  coverage/performance screens, not robust comparative rankings.
- Lambda baseline remains **6,534/6,535**, with the independently reproduced
  committed-HEAD `edit_view_only` failure described above. Shared source has not
  changed since that baseline; later changes and their focused rechecks are
  confined to MVP. This is still a failing aggregate gate.

The [final evidence report](../../temp/mvp_expansion/final-stage25.md) contains
both complete matrices, timing boundaries and links to raw captures; its
[JSON index](../../temp/mvp_expansion/final-stage25.json) records capture hashes
and gates. Windows clock validation and the pre-existing class-cache discrepancy
in §8 remain open. Results3–8 are unchanged; no new numbered result is published
by this phase.
