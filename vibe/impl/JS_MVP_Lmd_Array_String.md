# JS MVP Lambda arrays and strings

**Status:** implemented; targeted semantic/GC, benchmark and baseline checks pass,
2026-10-08. See the §20 follow-up below for current validation status.
Scope: [JS_MVP_Lmd §§18–20](../jube/JS_MVP_Lmd.md#18-ordinary-arrays-and-core-string-methods).
Authority: **S1.11**, **D2.6.1v3**, **D3.4.3v5**, **D5.3.1–D5.3.4**.

## Implementation and shared reuse

- `mvp_lmd_mir.cpp` recognizes unshadowed `Array` construction/calls and
  `String.fromCharCode`, captures array/string methods before arguments run,
  and handles dynamic method names through the existing property dispatch.
  Builtin method values remain internal to receiver calls.
- Array construction distinguishes a single numeric length from element
  arguments. Lambda `Array`, `array_reserve_append_slots`, the existing MVP
  verbatim element-store lowering and scalar homes provide storage. Reusing
  this store retains checked failure and destination-owned scalar storage;
  Lambda's list-splicing `array_push` is not appropriate.
- Constructor holes use `ITEM_JS_DELETED_SENTINEL`, the shared non-value
  sentinel. Reads, `pop`, iteration and destructuring expose undefined;
  indexed `in`/`Object.hasOwn` distinguish it from a present undefined slot.
  Indexed deletion creates a hole and own projections skip holes. Sparse
  writes, hole literals and length growth remain excluded.
- Ordinary and typed `fill` share bounds conversion. Ordinary fill retains
  values verbatim. `pop` copies scalar payloads before clearing removed slots.
  Mutating methods invalidate immutable-array numeric facts like indexed writes.
- `join` uses the existing MIR primitive conversions to produce a string
  array, then calls Lambda `fn_join2`. Null, undefined and holes become empty
  fields; an absent/undefined separator becomes a comma. Shared `fn_join2`
  now checks size overflow and allocation failure.
- String operations reuse `Utf16Iterator`, `utf8_encode_wtf8`, MVP numeric
  conversions and concatenation. `str_repeat` is exposed in `lambda.h`,
  roots its source across allocation, and short-circuits empty input.
- Simple assignment patterns accept identifier targets and direct array
  values, preserving RHS snapshots, ordered writes, TDZ and const checks.
  They do not admit declaration patterns, nesting, rest/defaults or general
  iterator protocols.

## Helper disclosure inventory

These additions were disclosed before implementation:

| Helper | Location / purpose | Dependencies and ownership |
|---|---|---|
| `mvp_lmd_array_new` | MVP runtime; initialize a hole-bearing array | Shared `array` allocation, reserve and hole sentinel; `MAY_GC`, precise `Rooted<Array*>`, returns an Item. Existing allocation does not initialize JS holes. |
| `array_construct` | MVP compiler; shared `Array(...)` / `new Array(...)` lowering | Existing argument snapshots, numeric checks and element stores; no runtime-owned state. |
| `sequence_reference` | MVP compiler; array/string indexed access and builtin lookup | Existing readers/property dispatch and shared emitter roots; no runtime-owned state. |
| `sequence_method_call` | MVP compiler; receiver-method lowering | Existing coercion, storage, string/UTF helpers and emitter roots; no runtime-owned state. |

Existing functions extended rather than duplicated:

- `mvp_lmd_map_method` becomes `mvp_lmd_builtin_method`, adding an owner-kind
  parameter and table entries; no allocation or runtime-state dependency.
- `mvp_lmd_string_at` adds a mode and Number index for bracket, character,
  code-unit and from-code-unit operations. It copies the selected unit before
  allocating its result; the shared import remains `MAY_GC`.
- `typed_array_fill` becomes `array_fill_call`, with an ordinary-array option.
- `str_repeat` and `fn_join2` are new MVP imports of existing Lambda functions,
  with explicit GC/argument metadata. No full-JS runtime helper is imported.

## Initial evidence at Result6 capture

- `make build-release-compile`: passes; log under
  `temp/mvp_lmd_array_string/build.log`.
- No tests were added in the implementation turn. Result6 later ran 15
  interleaved release samples for the 42 previous workloads and each of six
  newly enabled targets, with fresh MIR validation and output checks.
- The six targets are `r7rs/mbrot`, `larceny/puzzle`, and Kostya `base64`,
  `json_gen`, `brainfuck`, and `levenshtein`. Their JS kernels, setup, inputs,
  and iteration counts were retained in temp adapters. Scalar/boolean result
  checks replace host output; `base64`, `brainfuck`, and `levenshtein` include
  a small result check in the measured body. See [MVP_Result6](../../test/benchmark/js_mvp_lmd/MVP_Result6.md)
  for timing boundaries, outputs, paired controls, and source hashes.
- The capture left §15.3's semantic/forced-GC and Lambda/MVP/Test262 gates
  pending, covering holes versus undefined, aliases, shadowing,
  argument side effects, scalar-tail growth/pop, UTF-16/lone surrogates,
  conversion/range edges, dynamic calls and destructuring errors.
- Result6 records frozen release binaries, pinned native MIR, self-reported
  execution times and matched outputs. Semantic unit and forced-GC checks were
  added subsequently; the current results follow below.

## Result6 tuning (§19)

Implementation and validation complete. The two new
compiler utilities were disclosed before coding:
`visit_destructured_assignments` reuses assignment transfer for
kind/range analysis without changing the AST; `string_concat` groups primitive
string additions into existing fixed-arity Lambda concatenation calls. Neither
adds runtime-owned state or a new runtime helper (**D8.2.6**, **D5.3.4**).

- Literal destructuring retains per-element kinds, numeric lanes and lengths.
  Discarded function-body swaps snapshot all RHS operands before ordered writes;
  script completions and observed RHS arrays retain materialization.
- Builtin inference preserves the unresolved bottom domain until receiver facts
  arrive. Const reads retain their kinds after the existing TDZ check.
- ASCII character results reuse `get_ascii_char_string`, now declared in the
  shared header. ASCII length and `charCodeAt` use guarded native loads;
  UTF-16, missing indices and coercion retain the existing path (**S1.11**).
- `join` scans for all-string contents and directly reuses `fn_join2` on success.
  Mixed/holey arrays retain conversion and destination-owned scalar handling.
  Append stores skip wide-number checks for proven nonnumeric values.
- A closed unit without ordinary-array constructors or deletion cannot create
  holes, so proven array receivers omit the sentinel branch. This conservative
  proof is independent of numeric contents; mixed units and generic property
  fallbacks keep the hole check.
- Definite string addition retains its string kind. Primitive concatenations
  combine in groups of at most six using `fn_strcat3`–`fn_strcat6`; returned
  builders are frozen before publication. General symbolic bounds, per-array
  density/element facts and cross-expression builder ownership remain future
  refinements; this change keeps the existing storage and ownership contracts.

### Validation and paired release evidence

- Release compilation passes: `make build-release-compile`, 0 errors and
  28 warnings. The final candidate SHA-256 is
  `1a9aabc08d566eb428d8073c0658d300c0711d26ba7c8bcef85bba37f8e8d084`;
  the exact Result6 control is
  `d8d64af7f82907a45b17f717a59e7bab257b19270eb06aea059689e673d0f046`.
- MVP tests pass **41/41**, normally and with
  `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. Four new cases cover
  inferred builtin kinds, typed/numeric row swaps, observable script completion,
  TDZ/const errors, holes/iteration/projections, aliases, empty and mixed joins,
  shadowed builtins, argument side effects, scalar-tail growth/pop, range
  errors, UTF-16/lone surrogates and concatenation order/ownership.
- The final paired run uses 15 alternating rounds plus one discarded process
  per lane, native MIR pinned throughout, and unchanged Result6 source adapters.
  All **2,160 measured and 144 discarded output checks** pass. Sources and frozen
  binaries remain unchanged. Times measure generated program execution;
  compilation and process startup are excluded. These are MVP-versus-MVP
  measurements; Node and Lambda references were not rerun.
- Paired gains: `levenshtein` **79.154×**, `base64` **5.438×**, `brainfuck`
  **2.994×**, `json_gen` **2.542×**, `integer_dense` **1.091×**, and the strings
  microbenchmark **23.133×**. `mbrot` and `puzzle` remain effectively flat.
  The 48-workload geometric gain is **1.267×**; no workload's paired two-sided
  95% bootstrap interval establishes a slowdown.
- An initial variant removed hole checks from generic property fallbacks too,
  slowing `gcbench` by about 5% and `binarytrees` by about 9%. An isolated
  release A/B restored only those checks and recovered both. The final rule
  specializes known arrays and preserves the generic fallback's control flow;
  tree performance is flat within uncertainty. This isolates the lowering
  change; the native JIT layout/register-allocation mechanism remains unprofiled.

Artifacts are under `temp/mvp_result6_tuning_20261008/`: `final.exe`,
`final_hashes.json`, `final48/comparison.json`, per-round output and fresh MIR
captures, `hole_ab/comparison.json`, `mvp-final.log`, `mvp-gc-final.log`,
`tuning.patch`, and the `final-source/` copy of changed code.
The final capture is reproducible with:

```sh
python3 temp/mvp_result6_tuning_20261008/runner.py \
  --candidate temp/mvp_result6_tuning_20261008/final.exe \
  --control temp/mvp_lmd_arrays_20261008/final/candidate.exe \
  --output temp/mvp_result6_tuning_20261008/replay --runs 15
```

`make test-lambda-baseline` passes **6,404/6,404**: 4,300 runtime tests and 2,104
input parser tests (`lambda-baseline.log`). `make test262-baseline` passes
**40,261/40,261** on a clean repeat, with zero retries, batch losses, failures
or non-fully-passing cases (`test262-baseline-clean.log`). The first run had
two Unicode identifier cases exceeding the unchanged 3-second slow gate
(3.247/3.647 seconds); they passed on retry but that run was not accepted as a
clean gate. The identical command and binary then completed those cases in
2.753/2.831 seconds, with no source, harness, threshold or worker-count changes.
Both logs and timing records are retained. The final `lambda.exe` hash matches
the frozen benchmark candidate. Result6's
original cross-engine snapshot remains historical; this section records its
subsequent tuning and targeted semantic/GC acceptance. The broader numeric
feature matrix from MVP §15 remains separate outstanding work.

## Numeric and indexing follow-up (§20)

Implementation, targeted semantic/GC checks, paired benchmarks and both
baseline gates pass, 2026-10-08. Two small performance regressions are retained
and quantified below.
Authority: **S1.11**, **D2.4.3**, **D5.3.4**, **D8.2.6**. Changes stay in
`mvp_lmd_mir.cpp`; no shared runtime or full-JS execution code changes.

### Implementation and disclosed helpers

- `remainder_zero`, a new compiler utility disclosed before coding, recognizes
  equality/inequality of remainder by a literal positive power of two against
  zero. Proven integers use a mask; general Numbers use exponent/significand
  bits. Operand evaluation and ToNumber still occur exactly once. Signed zero,
  fractions, subnormals, NaN and infinities retain JS results. It reuses the
  shared bit emitter and coercion; no native helper or owned state is added.
- Existing arithmetic lowering scales division by a positive literal power
  of two with its exact reciprocal. Multiplication has the same single
  rounding, including underflow and signed zero; arbitrary divisors retain
  division.
- `loop_reset_before`, the second disclosed compiler utility, uses the existing
  AST index to recognize a declaration in the for initializer or directly
  before the loop in the same block. Existing counted-range analysis can then
  retain bounds for nested reset counters, including inward-moving opposite
  cursors. Unique writes, activation ownership, update placement, monotonicity
  and int53 overflow checks remain mandatory. Increasing square conditions
  use a conservative square-root upper bound. No AST mutation or runtime
  state is introduced.
- Indexed string reads reuse `get_ascii_char_string`, already part of Lambda's
  initialized static ASCII table. The new MVP import is `NO_GC`, with a scalar
  argument and pointer result. Guarded ASCII byte loads retain bounds checks;
  non-ASCII strings use the existing UTF-16 helper. Immutable ASCII literals
  retain their length and contents after evaluating the receiver, preserving
  TDZ and source effects. Numeric keys cannot denote `length`, so indexed
  results retain string/undefined kinds and omit numeric append-store work.

### Evidence and limitations

- `make build-release-compile` passes: 0 errors, 28 warnings (`build4.log`).
  Exact control SHA-256:
  `1a9aabc08d566eb428d8073c0658d300c0711d26ba7c8bcef85bba37f8e8d084`.
  Exact candidate:
  `2039dbcb41b9e3ad0dae100730ddbcb8b79710c0f5da74e5e258935e32192730`.
- Rebuilt MVP tests pass **43/43** normally and under
  `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` (`tests4.log`,
  `tests4-gc.log`). Added coverage includes dynamic-reference comparisons for
  remainder-zero lowering, power-of-two scaling, loop resets/opposite cursors,
  square bounds and rejected fractional/outward cases, character result
  ownership, Unicode/surrogates, dynamic `length`, TDZ and key side effects.
- `final48/comparison.json` records 15 alternating release rounds, one discarded
  process per lane, the unchanged 48-workload manifest and identical-control
  peers. All **2,220 measured / 148 discarded** outputs match; the four target
  rows include fresh untyped Lambda references. Source and binary hashes stay
  unchanged. Native MIR is pinned and freshly captured. Timings exclude
  compilation/startup; Node and full LambdaJS are not remeasured.
- Target paired gains are **1.514× collatz**, **1.669× primes**, **1.460×
  fannkuch**, and **1.109× base64**. All four paired 95% intervals favor the
  candidate. Emitted MIR instruction counts change 200→198, 327→298, 910→869,
  and 2,366→2,191 respectively. Counts are supporting evidence, not a model of
  dynamic instruction cost. The four-row untyped comparisons are contemporaneous;
  historical Result6 references are not silently reused.
- All 48 improve **1.032×** geometrically. `map_lookup` and `json_gen` regress
  in the full run; **60 more pairs** confirm paired gains **0.980× / 0.981×**.
  Candidate/control 95% intervals are **1.009–1.027 / 1.015–1.028**; identical
  control-peer intervals span one. Their emitted operations are unchanged
  apart from address constants. The timing cause was unresolved in that round
  (the follow-up below isolates native placement); these
  small regressions are retained alongside the target gains, not classified
  as noise. Confirmation adds **360 measured / 6 discarded** matching outputs.
- Collatz and base64 remain **1.153× / 1.104×** slower than fresh untyped
  Lambda; primes and fannkuch take **0.908× / 0.940×** its time. Adaptive
  integer recurrence state and further character/append conversion reductions
  remain possible follow-ups. The broader §15 numeric-feature matrix and
  Linux/Windows validation remain separate pending work.

`make test-lambda-baseline` passes **6,407/6,407**: 4,303 runtime and 2,104
input parser tests (`lambda-baseline.log`). `make test262-baseline` passes
**40,261/40,261** on its first run, with zero retries, batch losses, failures
or non-fully-passing cases (`test262-baseline.log`). `git diff --check` passes.
The restored release `lambda.exe` matches the frozen candidate hash exactly.

Artifacts are under `temp/mvp_four_tuning_20261008/`: frozen `control.exe` and
`final.exe`, `final-source/`, `final_hashes.json`, `analysis.json`, `final48/`,
`confirm/`, `validation.json`, `tuning.patch`, runner, source/MIR captures and
per-sample outputs. Earlier stage
screens are diagnostic: stage1 had an ASCII-import ABI mismatch fixed before
stage2, and its compiler-source snapshot was not frozen. Only the final run
and confirmation provide accepted release evidence.

```sh
python3 temp/mvp_four_tuning_20261008/runner.py \
  --candidate temp/mvp_four_tuning_20261008/final.exe \
  --control temp/mvp_four_tuning_20261008/control.exe \
  --output temp/mvp_four_tuning_20261008/replay --runs 15 \
  --references --lambda-only \
  --reference-only kostya/collatz,kostya/primes,beng/fannkuch,kostya/base64
```

### Further collatz/base64 tuning and regression diagnosis

Implementation, paired benchmarks, **44/44 normal and forced-GC MVP tests**,
and both baseline gates pass, 2026-10-08.
Authority: **S1.11**, **D2.2.5**, **D2.4.3**, **D5.3.4**, **D8.2.6**.

Four compiler utilities were disclosed before coding; no runtime helper is
added. `linear_index` shares recognition of exact literal-offset loop indices.
`literal_characters` builds one frontend-owned table of the 128 ASCII character
Items, reusing Lambda's static character strings or name-pool strings. Dynamic
ASCII and literal reads retain their byte/UTF-16 and bounds guards, then load
the Item directly. The previous native character import is no longer needed.

`integer_loop_node` admits only small while loops with local Number bindings,
numeric literals, simple assignments/updates, comparisons and conditional
statements. Calls, properties, declarations, nested loops, exits and captured
or global state stay on the ordinary path. `integer_loop` reuses the existing
inline binding frame to emit a positive-int53 version after exact entry guards.
Overflow, zero writes and fractional power-of-two division restore all local
values from the iteration's entry snapshot before replaying the original
floating-point loop. Numeric condition writes are included in that snapshot;
successful termination publishes the final condition's changes. No AST or
retained representation facts are mutated and no GC-owned state is introduced.

The shared `em_numeric_op_plan` gains an explicit integer-arithmetic option.
Its default remains unchanged. MVP's boxed, statically proved and guarded-loop
paths now reuse that opcode selection; JS admission and division exactness
remain in the frontend. Tests cover partial-iteration rollback, overflow,
fractional/negative/zero/NaN entry, condition updates, zero multiplication,
rejected observable calls, affine/square bounds, do-while and post-update
reads, character ownership, NUL/DEL and Unicode. The first integer-loop test
run exposed use of the numeric plan's comparison-only opcode field; it was
fixed before performance acceptance.

The bounds refinement also fixes a representation mismatch: kind inference
proved byte reads present using the enclosing condition, while range inference
discarded that same scoped proof and chose doubles. Both now use the same
site bounds. Increasing/decreasing affine limits retain exact int53 checks;
while-body narrowing applies only before the sole update, requires that update
to belong to the same enclosing loop, and excludes do-while's untested first
iteration. Final review reproduced a wrong `undefined` result when an inner
loop updated the index before the outer condition was retested. Reusing
`enclosing_loop` rejects that stale proof; ordinary and for-of nested-loop
regressions now pass. General remainder-zero tests also use
one common-exponent guard, leaving zero/tiny/huge/nonfinite cases on a cold
path. The slower scaled-conversion experiment was discarded.

**Regression cause:** the original Map and JSON slowdowns reproduce with the
exact §19/§20 binaries. Their emitted MIR operations are unchanged. Link maps
show the hot `mvp_lmd_map_call` and `mvp_lmd_number_to_string` helpers moved
**5,280 bytes**. Their sizes remain 2,564/448 bytes; their changed instructions
are exclusively call/address relocations with identical call targets. The
same audit of `fn_strcat3` finds only relocations. This identifies executable
layout sensitivity at hot native calls, rather than extra Map/string work.
The specific cache/predictor mechanism was not isolated with hardware counters.

A diagnostic relink places **31 unchanged runtime helpers at identical
addresses and sizes** in both builds. Across 60 alternating pairs, Map medians
are **13.076/13.129 ms** and JSON medians **5.761/5.775 ms**. Candidate/control
95% intervals are **0.998–1.015** and **0.997–1.017**, with control-peer
intervals also spanning one. The original statistically significant regressions
disappear when helper placement is matched. The ordering file is retained
only as an attribution experiment; final release measurements use the normal
build configuration.

Artifacts: `temp/mvp_followup_20261008/`, including `reproduce/`, both original
and ordered link maps/binaries, `runtime.order`, `native-relocations.json`,
`ordered-runtime-layout.json`, `ordered-comparison/`, stage screens and final
frozen binaries/source hashes. The accepted control is
`2039dbcb41b9e3ad0dae100730ddbcb8b79710c0f5da74e5e258935e32192730`;
the candidate is
`da89d81a9fa893516ede62345771b82a3709fd45faed23c0c2bc7dc37f7220ca`.

**Final performance:** `final48/comparison.json` records 15 alternating rounds
across all 48 unchanged workloads, pinned native MIR, an identical-control
peer, and fresh untyped Lambda references for the two targets. All **2,190
measured / 146 discarded** output checks pass; binary/source hashes remain
unchanged. Timing is self-reported execution only, excluding compilation and
startup. Node and full LambdaJS are not remeasured. Median milliseconds:

| Workload | Prior §20 | Candidate | Untyped Lambda | Paired gain |
|---|---:|---:|---:|---:|
| collatz | 344.351 | 200.168 | 298.685 | 1.720× |
| base64 | 13.414 | 11.869 | 12.069 | 1.128× |

Candidate/control paired-bootstrap 95% intervals are **0.581–0.603** and
**0.879–0.895**. Against fresh untyped Lambda they are **0.665–0.694** and
**0.964–0.996**: collatz now takes **33.0% less time**, while base64's 1.7%
advantage is small. The 48-workload geometric gain is **1.032×**. No row's
candidate/control two-sided interval lies entirely above one. `array1` also
improves **0.662→0.309 ms** because
the scoped bounds retain its integral element/sum representation.

Additional **60-pair** runs distinguish remaining shifts:

| Comparison | Workload | Control → candidate ms | Candidate/control 95% interval |
|---|---|---:|---:|
| Final vs original §19 | map_lookup | 12.9760 → 13.0995 | 1.004–1.015 |
| Final vs original §19 | json_gen | 5.6735 → 5.6825 | 0.997–1.006 |
| Final vs prior §20 | object_growth | 3.0000 → 2.9735 | 0.983–0.997 |
| Final vs prior §20 | object_retype_escaped | 6.7920 → 6.8095 | 0.997–1.009 |

All control-peer intervals in these confirmation runs span one. The original
Map regression is reduced but persists at **1.0%** in the normal final build.
The JSON and escaped-object shifts are not statistically separated from noise
here; object-growth slightly improves. A 1.3% escaped-object slowdown in the
earlier candidate is no longer measurable in this final build. Those earlier
timings are retained under `*.before-nested-proof/` and are not final acceptance
evidence. `final-original/` and `final-object/` add
**720 measured / 12 discarded** matching outputs. No ordering-file workaround
or benchmark-source change is shipped.

The final release build passes with **0 errors / 28 warnings** (`build9.log`).
The rebuilt semantic tests pass **44/44** normally (`tests9.log`) and with
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` (`tests9-gc.log`).
`make test-lambda-baseline` passes **6,408/6,408** (4,304 runtime + 2,104
input tests). `make test262-baseline` passes **40,261/40,261**, with zero
retries, batch losses or regressions. Both gates were rerun after the nested-loop
proof fix; their final logs are `lambda-baseline.log` and
`test262-baseline.log`. `git diff --check` passes, and the restored release
`lambda.exe` matches the frozen final binary exactly.
`analysis.json` retains the compact statistics; `final-source/`,
`final_hashes.json`, `validation.json` and `tuning.patch` record provenance.
The broader §15 feature-edge matrix and Linux/Windows validation remain
separate pending work.

```sh
python3 temp/mvp_followup_20261008/runner.py \
  --candidate temp/mvp_followup_20261008/final.exe \
  --control temp/mvp_followup_20261008/control.exe \
  --output temp/mvp_followup_20261008/replay --runs 15 \
  --references --lambda-only --reference-only kostya/collatz,kostya/base64
```
