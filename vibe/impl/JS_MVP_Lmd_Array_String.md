# JS MVP Lambda arrays and strings

**Status:** implemented; targeted semantic/GC, benchmark and baseline checks pass,
2026-10-08. See the Result6 tuning record below for current evidence.
Scope: [JS_MVP_Lmd §18](../jube/JS_MVP_Lmd.md#18-ordinary-arrays-and-core-string-methods).
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
