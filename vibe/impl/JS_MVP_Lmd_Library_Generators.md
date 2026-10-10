# JS MVP: library completion and generators

**Date:** 2026-10-10. **Status:** feature implementation and correctness checks
complete; all eleven new canonical oracles pass, for 87 supported workloads.
Performance acceptance remains open for a 1.3% `log_pipeline` regression.
No published result yet.

**Design:** [JS_MVP_Lmd §30](../jube/JS_MVP_Lmd.md#30-library-completion-and-generator-based-benchmarks).

## 1. Contract and dependencies

Implement the approved eleven targets in four rounds. Preserve the canonical
sources, inputs, setup, warmup, timed regions, and exact oracles. The final
population is 87 (70 standard plus 17 microbenchmarks); `jetstream/hashmap` is
excluded. No mutable prototype links, `__proto__`, accessor execution, proxies,
VMap plain objects, async execution, or general descriptors.

Reuse Lambda kernels first (**D1.3v3**). Preserve nominal Map/shape compatibility
(**D2.6.9v3**, **D3.4.3v5/D3.4.5**), scalar ownership and precise roots
(**D5.2–D5.3**), Function and closure ABIs (**D6.2.2v2–D6.2.4**), explicit
completions (**D1.4v4**), and guarded dispatch (**D8.4.1v2**). No full LambdaJS
runtime dependency, new value tag, conservative stack scan, or vendor edit.

The frozen control is `temp/mvp_library/control.exe`, copied from §29 stage25,
SHA-256 `990bae9b78b747f85e8ec5053a15a3757c7b5f8f0ac3f868293dcd96dd7f876b`.
`temp/mvp_library/control.json` records provenance. The starting commit is
`ea2a03111f43103bba2f975d3cde9defc13815be`; the §30 design edit predates this work.

## 2. Implementation rounds

| Round | Targets | Work and dependencies | Completion gate |
|---|---|---|---|
| 1 | `jetstream/raytrace3d`, `text/prettier_ast`, `beng/pidigits` | Core BigInt operations; default/rest parameter installation; extend shared callback traversal for reduce/some; flat/includes; Lambda JSON and clock adapters; array primitive conversion | Exact target oracles and one positive self-timing record; focused ownership and error tests |
| 2 | `beng/regexredux`, `text/fast_diff`, `text/hyphen` | Shared regex kernels with negative lookahead; UTF-16 match/replace positions; array edits; first-class builtin Functions and call/bind; for-in/globalThis/Object prototype methods/lowercase | Canonical synchronous workloads, regex state/captures, holes/mutation, bound receivers |
| 3 | `text/microdiff` | Optional-chain short circuit; apply; reflection; Date/RegExp conversion; bounded data descriptors on existing nominal objects | Descriptor flags, inherited enumeration, constructor identity, source oracle |
| 4 | `text/jq_mix`, `text/jq_records`, `text/jq_bf`, `text/jq_tree` | Saved generator state on traced Function environments; general iterator protocol and close; try/catch/finally completions; binding patterns/spread; Object.create/assign and Set | Yield/resume/return/throw, nested cleanup and abrupt completions under forced GC; all four exact jq oracles |

Primary files are `lambda/js/mvp-lmd/mvp_lmd_mir.cpp`,
`mvp_lmd_runtime.{h,cpp}`, `mvp_lmd_objects.cpp`, and the existing class/host
adapters. Audit `js_ast_collect_parameter_facts` and shared AST binding helpers
before extending admission. Keep specialization of old feature-free functions;
new syntax must not change their ABI or insert runtime dispatch into hot loops.

Shared candidates: `lambda/core/lambda-decimal.hpp` (`bigint_*`),
`lambda/runtime/lambda-vector.cpp` (array operations/reduce),
`lambda/input/input-json.cpp`, `lambda/format/format-json.cpp`,
`lib/utf.h`, `lib/datetime.h`, `lib/time_util.h`, and neutral regex kernels.
Audit semantic differences, root ownership, and transitive dependencies before
calling a kernel. Extend a common option when appropriate; do not duplicate
full-JS helpers into MVP.

## 3. Disclosed helper inventory and progress

Before coding, the user was informed of:

- `mvp_lmd_bigint_binary`: JS operator/error adapter over core `bigint_*`.
- `mvp_lmd_bigint_to_string`: radix/string ownership adapter over
  `bigint_to_cstring_radix`.
- `mvp_lmd_method_owner`: shared method catalog lookup replacing repeated
  compiler type-range decisions.
- `mvp_lmd_array_flatten` and internal `flatten_into`: recursive, hole-skipping
  JS flatten over precisely rooted Lambda arrays. Lambda `fn_flatten` is a
  typed numeric storage operation and cannot implement this behavior directly.
- `mvp_lmd_native_function` and `mvp_lmd_named_set`: promoted host adapters,
  reused by library installation; the Function allocation ABI is unchanged.
- `mvp_lmd_library_initialize`, `json_parse`/`json_copy`, and
  `json_stringify`/`json_write`: shared strict parser with a JS-number option,
  owned runtime copies, shared escaping and shortest-double formatting.
  `json_skip_whitespace` confines the shared JSON parser to JSON whitespace.
- `datetime_now_ms`: shared wall clock used by Lambda `datetime_now` and the
  MVP `date_now` entry; `date_construct` and `date_value` preserve Date identity
  on nominal Maps with an internal numeric payload.
- `mvp_lmd_regexp_new`, `mvp_lmd_regexp_string_method`, and
  `mvp_lmd_regexp_initialize`: nominal RegExp/first-class native methods over
  the neutral compile frontend and matcher. Internal `regexp_buffer`,
  `regexp_exec`, `regexp_result`, and `regexp_replacement` adapt UTF-16
  positions, state, captures and replacement strings/callbacks.
- `mvp_lmd_primitive_to_number`: shared primitive coercion for native adapters;
  reused by sort, Date, RegExp state, and array edits.
- `mvp_lmd_function_method`, `mvp_lmd_function_initialize`, `function_forward`:
  call/apply/bind through the existing Function invocation ABI. Bound captures
  use a traced environment containing the target and an owned argument array.
  `mvp_lmd_program_library` exposes the per-program library state to dispatch.
- `mvp_lmd_array_edit`: one hole-preserving traversal for concat/splice/unshift.
- `mvp_lmd_for_in_keys`: existing projection plus nominal ancestry and shadowing;
  the MIR collection loop reuses the key snapshot and checks deleted properties.
- `mvp_lmd_library_class_initialize`: common nominal builtin constructor metadata;
  Object adapters `object_construct`, `object_string`, `object_own`, and
  `object_value` reuse property operations. `activate_library` factors admission.
  Existing `map_fill_reserve_data` is promoted for extended nominal payloads.
- `mvp_lmd_array_to_string`: nested array conversion with precise roots and
  cycle detection, using the shared string buffer and primitive conversions.
- `type_tree_reflag_field` and `mvp_lmd_define_data_property`: data-attribute
  transitions on shared Lambda shapes, with existing clone fallback.
- `map_shape_set_resolved`: shared Lambda packed-field mutation after the caller
  has resolved the current own field. MVP descriptor admission reuses that
  lookup; `map_shape_set` retains its existing ABI and delegates to the same
  implementation (**D3.4.3v5**, **D3.4.5**).
  Internal `map_shape_set_impl` selects lookup at compile time so both public
  entries validate once. Native disassembly exposed duplicated validation and
  an extra wrapper frame in the initial factoring. The admitted value TypeId
  is retained across lookup, avoiding a second tagged-value/container decode.
- `map_shape_delete_resolved` reuses the descriptor lookup for physical deletion;
  internal `map_shape_delete_impl` shares validation and transition logic with
  the original entry (**D3.4.3v5**, **D3.4.5**).
- `map_shape_set_text` and `map_shape_delete_text` select canonical string lookup
  in those same templates when the caller proves ordinary string keys. MVP uses
  them only in closed units without Symbol exposure; identity-aware mutation
  remains available for Symbol/private keys (**D3.4.3v5**, **D8.4.1v2**).
  Shape extension reuses one `NameMeta` read for hash, NameId and key kind.
  Shared monotone method-replacement analysis covers Arrays and Maps, including
  observed methods and destructuring targets. Reflective Object writers and
  escaped constructor aliases disable that proof; proven native Map calls keep
  their original direct method path (**D8.4.1v2**).
- `mvp_lmd_array_method_value`, `mvp_lmd_to_primitive`: first-class array methods
  on the Function ABI and ordered object conversion. `js_ast_has_optional_chain`
  promotes the existing frontend query for both emitters.
- `mvp_lmd_caught_value` and compiler `completion_cleanup`: existing Lambda
  error carriers, JS payload identity, and finally routing (**D1.4v4**).
- Generator creation/park/resume and iterator get/step/close adapters use
  shared `Activation`, carrier layout, and promoted GC lifecycle hooks. Under
  **D5.1.1v3**, MIR bodies park in place; there is no new register-spill state
  machine. Captures remain ordinary traced Lambda Functions.
- `mvp_lmd_generator_call`, `generator_resume`, `mvp_lmd_generator_park`, and
  `mvp_lmd_generator_resume_kind` now use that shared activation path. The
  existing generator/iterator carriers and GC hooks are promoted to
  `runtime/suspended_activation.*`; `mvp_lmd_function_forward` is promoted for
  generator entry and stores transient argument homes on the activation's
  number stack. Shared `Activation` copies its returned scalar into an owned
  home before releasing its stack (**D5.2–D5.3**).
- `typemap_hash_lookup_key` selects existing NameId lookup for identity keys
  and canonical UTF-8 lookup for strings. The shared Map fallback preserves
  NameId/kind/hash. `symbol_create` uses the existing Symbol/NamePool records;
  no new type tag or parallel key table (**D3.4.3v5**, **D3.4.5**).
- `mvp_lmd_iterator_get`, `mvp_lmd_iterator_step`, and
  `mvp_lmd_iterator_close`, with internal sequence factories/property checks,
  use the promoted traced iterator carrier. Generator delegation preserves
  the delegate's result identity; loops close on abrupt body exits and retain
  an existing throw over a close failure. `mvp_lmd_truthy` factors the existing
  data-descriptor conversion for native iterator consumers.
- `mvp_lmd_iterator_collect` serves `Array.from` and spread; the native
  `array_from` adapter and `mvp_lmd_call_array` reuse rooted forwarding.
  `mvp_lmd_object_create` stores a fixed prototype in an ordinary traced
  Function environment owned by the nominal extension. `mvp_lmd_object_copy`
  shares own enumerable property traversal (including symbol keys) between
  `Object.assign` and object spread. No mutable prototype link is introduced
  (**D3.4.3v5**, **D6.2.2v2**).
- Compiler `collect_pattern_binding`/`bind_pattern` reuse shared binding-pattern
  traversal and property references for destructuring. Callable metadata also
  admits own properties on arrows/methods without a constructor prototype.
- `collection_construct`/`collection_method` adapters share ordered-map
  storage for Map and Set. Existing sequence iterator helpers handle live
  collection cursors; `js_iterator_map_heap_destroy` releases abandoned cursor
  leases, using the shared carrier and precise tracing (**D5.3**).
- Named public instance fields reuse `js_script_field_initializer_ensure` and
  ordinary closure construction. `mvp_lmd_class_initialize_instance` invokes
  those Functions per instance, before base constructor bodies or immediately
  after derived `super()`, and uses existing own-field storage. Default
  constructors follow the same sequence (**D6.2.2v2–D6.2.4**).
- `mvp_lmd_array_visit` shares native map/filter/forEach/some/reduce traversal
  for borrowed methods, including String/array-like receivers. Direct MIR
  callbacks retain their existing loops. Number conversion gains decimal-prefix
  parsing and exact-integer radix formatting through core BigInt; nondecimal
  fractional formatting remains an explicit capability boundary.
  `mvp_lmd_parse_number` shares the scanner with the original one-argument
  coercion helper, and `mvp_lmd_number_to_radix_string` delegates decimal
  formatting to the original helper. Optional arguments stay out of ordinary
  coercion call sites (**D8.4.1v2**).
  `mvp_lmd_string_pad` composes shared repeat, UTF-16 range, and concatenation;
  suffix matching extends the existing search helper.
  Lowercase calls reuse `fn_lower`; the canonical hyphen workload supplies no
  locale argument. Locale-specific casing is outside this bounded surface.
- Array `shift`, `toReversed`, and `at` extend `mvp_lmd_array_edit`; character
  construction adds a validated code-point mode to `mvp_lmd_string_at`.
  `mvp_lmd_array_method_value` now accepts a callee option and continues lookup
  through the fixed Object prototype. Native array `instanceof` reuses the
  program-owned prototype identity without changing Lambda array layout
  (**D2.6.6v3**, **D3.4.3v5**).

The final frozen release is `temp/mvp_library/round4m/candidate.exe`, SHA-256
`5499bb795ecc8ea24aa49208d2b3852d5f9f44fc92c131e93cec87aa92a7e75d`.
Its provenance record retains source hashes and the implementation patch.
All evidence below is under `temp/mvp_library/`.

| Canonical targets | Latest output/backend validation |
|---|---|
| raytrace3d, prettier_ast, pidigits, regexredux, fast_diff, hyphen, microdiff | Exact Node output and positive self-timing |
| jq_mix | Full checksum **98172625** |
| jq_records | Full checksum **878885883** |
| jq_bf, jq_tree | Full checksums **478890292** and **313746104** |

All eleven rows are complete in `new11-final-round4m/comparison.json`, with
frozen inputs and pinned-MIR evidence. This is a validation-only capture; shared
correctness gates overlapped some rows. Its times are not a comparative
performance result. Generator stack creation and recycling remain a material
cost. The full seven-repeat `prior76-round4m` screen validates every prior
oracle and has a descriptive candidate/control geometric mean of **0.9992**.
The initial 31-repeat `confirm31-round4m` capture checks shape mutations;
`confirm31-short-round4m` encountered host contention. Its completed replacement
is `confirm31-short-final-round4m`: growth, escaped retyping, calls, strings and
spectralnorm all have intervals including parity. A longer text confirmation
was also stopped for contention (`confirm11-text-round4m/INTERRUPTED.md`).
Its quiet replacement, `confirm11-text-final-round4m`, is complete:

| Target | Candidate / control median | Candidate/control 95% interval |
|---|---:|---:|
| awfy/json | 1.0084 | 0.9944–1.0293 |
| text/text_search | 1.0099 | 0.9982–1.0208 |
| text/log_pipeline | **1.0133** | **1.0068–1.0478** |

Log pipeline is also slower than the control peer, with a 95% interval of
**1.0050–1.0483** (`candidate-vs-peer.json`). Overlapping separate control
intervals alone do not clear this signal. Its root cause is unresolved and
strict performance acceptance remains open (**D8.4.1v2**). The rejected round4n
catalog-grouping experiment is retained separately: MIR inspection shows that
the workload's fixed-name String calls already use direct method tokens, so
runtime catalog scans do not explain the slowdown. Source and CLI were restored
to the exact round4m hashes. No published Result snapshot was changed.

Regression repair preserves native Array traversal when the closed unit cannot
replace `Symbol.iterator`, and shape-guards ordinary Map get/set/has/delete/clear
even when iterator support is enabled. Custom iterators and own method overrides
retain their protocol/Function paths. Descriptor admission also shares its
resolved field with the Lambda physical writer. Object-to-number fallback
reuses the existing `it2d` decoder to limit cold MIR expansion; ordinary numeric
decoding remains inline. Library-enabled units share their nonnumeric conversion
through the existing primitive adapter. The shared direct-pointer guard skips
`ToPrimitive` calls for primitive operands; object hooks retain evaluation order
and owned scalar snapshots. `forEach` again has no live loop result, and error
root registration is limited to actual iterator cleanup sites. Decimal string
conversion bypasses validation of the optional radix, and ordinary writable
fields share one combined descriptor guard. Key lookup reuses the cached
canonical-name hash from `NameMeta`; unpooled strings retain byte hashing and
Symbols retain identity lookup (**D3.4.3v5**). Focused validation and release
A/B confirmation accompany these repairs (**D8.4.1v2**).
The first dynamic string-coercion experiment was reverted after two boundary
probes entered object conversion without the library's property-name pool.
The adapter now takes an explicit `library_objects` option. Known primitive
types retain direct lowering; dynamic values keep a string identity guard and
share native conversion. Basic units reject general objects before accessing
the optional name pool, while library units retain object hooks (**D8.4.1v2**).
The option follows recursive array conversion and `ToPrimitive`; a nested
unsupported object reports capability failure, and own array hooks still run.
Named hook writes select the existing receiver-aware Function ABI for implicit
native calls (**D6.2.2v2**, **D8.4.1v2**).

Shared-kernel correctness fixes include preserved JSON lone-surrogate escapes,
rooted argument snapshots with owned scalar homes, and an unaligned packed-field
fallback through Lambda field helpers. Sparse arrays retain the existing hole
sentinel. Additional helpers will be disclosed before implementation.

## 4. Validation and acceptance

The final 100-test gate passes normally in `final-lambda-baseline.log` and under
forced GC/poisoning in `final-focused-forced.log`. Canonical jq admission exposed exponential
speculative expansion of common class method names. Limit the candidate set
and reserve a caller-wide AST expansion budget before recursive lowering;
exhaustion uses the existing guarded call ABI (**D8.4.1v2**). A twelve-class
recursive method probe checks both results and generated MIR size. The new
`math_call` adapter exposes the existing Math operation table as native Function
values for dynamic lookup and spread, reusing its numeric kernels. An escaped
Math namespace uses ordinary property dispatch so mutations cannot bypass
specialized calls; intrinsic attributes use existing shape transitions.
The unchanged jq bundle now matches Node on nine small queries covering range,
map/reduce, Unicode, try/catch, updates, grouping and entry conversion
(`jq-features-mvp3.log`, `jq-features-node.log`). These probes do not validate
the four complete canonical jq workloads.

The shared activation return home exposed a scheduler lifetime bug: Lambda
destroyed the activation before adopting its wide result into task-owned
storage. The scheduler now copies first (**D5.2–D5.3**). All three affected
`await` fixtures pass with the rebuilt CLI (`lambda-activation-fixed-cli.log`).
The final aggregate rerun passes **6590/6591**, with only the reproduced §29
`edit_view_only` failure (`final-lambda-baseline.log`). Test262 passes
**40261/40261**, with zero unstable batches or regressions
and zero retries (`final-test262-baseline.log`). The rebuilt Test262 CLI's
SHA-256 matches the frozen round4m release. Rebuilding only the GTest binary is insufficient
because the script suite invokes `./lambda.exe`.

Use the main checkout and release binary for all performance measurements:

```sh
CCACHE_DIR="$PWD/temp/ccache" make build-release-compile
CCACHE_DIR="$PWD/temp/ccache" make -C build/premake config=debug_native test_js_mvp_lmd_gtest
JS_EXECUTION_BACKEND=mir ./test/test_js_mvp_lmd_gtest.exe
JS_EXECUTION_BACKEND=mir LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./test/test_js_mvp_lmd_gtest.exe
make test-lambda-baseline
make test262-baseline
```

Debug test binaries are for correctness only. Extend the
benchmark capture driver with a separate phase population/control contract;
never overwrite Result8 or §29 provenance. Freeze each candidate and its source
diff before paired captures. Check all 76 prior workload oracles, compare their
self-reported times in alternating order with an identical-control peer, and
confirm suspected regressions with longer runs. Record process/startup time
separately. New targets require positive single-record timing and exact output
parity with Node; invalid output is never performance evidence.

Acceptance requires all eleven target contracts, focused normal/forced-GC
checks, the shared Lambda and Test262 gates, and no confirmed prior-workload
regression. §29 already has a reproduced `edit_view_only` Lambda baseline
failure, unvalidated Windows clock behavior, and a pre-existing class-cache
discrepancy under **D8.4.1v2**. Keep those limits distinct from new regressions.

## 5. Accessor admission boundary

The unchanged jq bundle declares `FunctionCall.ordinary` as a getter used by
its unused tracing path. Admission must retain the declaration and preserve an
explicit unsupported completion if accessor execution is attempted. It cannot
strip the declaration, turn it into a data property, or key behavior to a
benchmark. Declaration-only admission now follows this boundary; report any
actual accessor dependency before expanding scope.
