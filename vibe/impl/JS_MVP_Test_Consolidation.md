# MVP runtime test consolidation

**Date:** 2026-10-19  
**Authority:** **S1.11**, **D5.2–D5.3**, **D8.1.3v26**. No semantic or design ruling changes.  
**Scope:** overlapping feature smoke tests in `test/test_js_mvp_lmd_gtest.cpp`.

The checkout contained **825** registered tests at the start, rather than the
earlier reported 817. This pass reduces it to **815**. The ten retired
registrations contained 70 script executions: 40 distinct edge-case executions
move to existing focused cases, and 30 redundant executions are removed. One
additional identical `super`/TDZ execution is removed from
`PropertyExpansionLiteralHome`, leaving its copy in
`PropertyExpansionReceiverInitialization`: **31 fewer script executions** in
total. The Math case is a grouping consolidation; all four of its executions
remain. This is a bounded overlap cleanup, not a claim that 815 is minimal.
All **991** explicit C++ `ASSERT_*`/`EXPECT_*` sites remain; the source inventory
is saved in `temp/js_mvp_dedup/inventory.json`.

## Coverage ownership

`test_js_gtest` and Test262 remain the general script and ECMAScript conformance
gates. Both normally select a mixture of AST and MIR execution, so their green
totals alone do not establish replacement MIR coverage. This pass uses retained
MVP/MIR tests as the replacement evidence and changes no backend selection,
Test262 runner, baseline, timeout or exclusion.

The native suite continues to protect Lambda scalar homes and precise rooting
(**D5.2–D5.3**), published ABI, public-entry lifetime, realm and module identity,
host integration, MIR emission and dynamic-source execution profiles
(**D8.1.3v26**). JavaScript's guest semantics remain distinct under **S1.11**.
Similar sources executed through a public entry, a compiled function, hosted
eval or a suspended interpreter activation are not assumed interchangeable.

## Removal map

All names below are in `JsMvpLmd` unless qualified otherwise. Moved scripts keep
their original assertions and separate `run()` calls, which create fresh
executions. Prototype mutations and globals therefore do not leak between them.

| Retired case | Retained coverage and edge cases | Removed executions |
|---|---|---:|
| `RejectsUnsupportedUnitBeforeExecution` | The old rejection smoke test now contained only successful programs. Accessors: `PropertyExpansionAccessors`; hoisted captures: `ClosureInitializationAndConst`; defaults: `DefaultAndRestParameters`; receivers: `OrdinaryReceiversAndNamedArrays`; holes: `OrdinaryArrayHolesFillAndJoin`; `with`: `WithLexicalAndObjectEnvironments`; async functions/generators: `AsyncAwaitMirAndPreciseOwnership`, `AsyncGeneratorSerialRequestsAndReentry`. Keep the block-function capture in `SharedEscapingClosures` and top-level try completion in `ExplicitCatchFinallyCompletions`. | 8 |
| `PublicInstanceFields` | `CoreClassFieldsAndStaticBlocks` already checks base/derived initialization order and ancestry. Its existing program now also checks a derived field overwriting the base field. Static self-instantiation, per-instance effects, absent initializers, default derived constructors and captured field arrows move there. | 1 |
| `StringsUseUtf16` | `CharacterCodesAndAsciiReuse` already checks surrogate indices, length, embedded NUL and out-of-bounds reads; its existing indexed-reader program also checks a numeric string key. Primitive concatenation, number formatting and UTF-16 comparison move to `Utf16RangesSearchAndSplit`. | 3 |
| `ArrayCapabilityAndRangeFailures` | `PropertyExpansionSparseArrays`, `SparseArrayGrowthAndSlice` and `OrdinaryArrayHolesFillAndJoin` cover hole creation, length extension, the non-index `4294967295` key and negative length. Noncanonical `01`/`-0` keys and fractional/NaN length rejection move to `PropertyExpansionSparseArrays`. | 4 |
| `ObjectAndMapScopeBoundary` | Getter behavior remains in `PropertyExpansionAccessors`; the empty loop in `JsFormalMir.LegacyForInInitializerRunsBeforeSourceEvenWhenEmpty` now increments its checked counter if entered. Distinct prototype forms, Map overrides/escaped calls, object-key conversion and builtin shadowing move respectively to `PropertyExpansionPrototypes`, `MapKeysAndMutation`, `ObjectProjectionAndCanonicalKeys` and `CoreGlobalBindings`. The inherited `toString` check joins the existing object-key program. | 3 |
| `LiveMapIteration` | `PropertyExpansionNativeMapIteration` already covers straight entry traversal and polymorphic Array/Map iteration. Live update/reinsertion, clear during value traversal, nested keys/values, distinct pair identities and labeled control flow move there. | 2 |
| `ClassesAndLexicalReceivers` | Lexical `super`, captured `this`, and receiver TDZ remain in `PropertyExpansionLiteralHome` and `PropertyExpansionReceiverInitialization`. Nested arrow mutation and escaped `new.target` move to the latter; constructor/super/static inheritance and call errors move to `ClassAncestryAndReflection`. | 4 |
| `MathNamespaceCallsAndSpread` | Move mutation, readonly PI behavior, computed/escaped calls and spread to `MathNamespaceMetadataAndOldLanes`, beside the existing namespace and signed-zero checks. | 0 |
| `UriWithoutInterpreterRealmCaches` | `UriSharedEncodingDecodingAndMetadata` already checks ASCII/reserved characters and astral UTF-8 round trips; `UriMalformedAndThrownIdentity` covers malformed escapes. Keep the distinct `%00` assertion in the encoding case. The retired case did not actually inspect realm caches. | 3 |
| `CollectionIteratorEscapedNextBrand` | `CollectionIteratorPhysicalBrandAndRetention` already contains the identical escaped-next brand test and checks values/exhaustion. Move the computed `next` lookup there. | 2 |

## Validation

Qualification logs are under `temp/js_mvp_dedup/`:

| Check | Result | Evidence |
|---|---|---|
| Ten original cases before editing | 10/10 | `before_focused.log` |
| Affected cases with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` | 18/18, in two batches | `forced_gc.log`, `forced_gc_completion.log` |
| `make test-lambda-baseline` | 7,882/7,882: 2,112 input + 5,770 runtime | `lambda_baseline.log`, `lambda_summary.json` |
| MVP suite within that baseline | 815/815; zero disabled tests | `mvp_result.json` |
| JS script and native runtime suites within that baseline | `test_js_gtest` 510/510; `test_js_script_gtest` 213/213 | `lambda_baseline.log` |
| `make build-release-compile` | passed | `release_build.log`, `release_provenance.json` |
| `make test262-baseline` | exit 0; 40,254/40,261 fully passing, seven retry-only cases, zero semantic failures/regressions | `test262_baseline.log`, `test262_result.json`, `test262_batch_kills.txt` |

The Lambda baseline used the installed Python 3.13/Pillow environment via
`PATH=/opt/homebrew/opt/python@3.13/bin:$PATH`, as in the preceding interpreter
alignment qualification. No golden or expected result was changed.

Test262 used `LAMBDA_TEST262_EXE` with the pinned `validated_release.exe`;
`release_provenance.json` records its build, source commit and SHA-256. The seven
retry-only cases are generated Unicode property-escape RegExp tests, all among
the eight partial cases in the preceding interpreter qualification. All seven
passed the runner's isolated retries. This does not establish a completely
clean aggregate timing/stability gate; no allowance or timeout was changed.
