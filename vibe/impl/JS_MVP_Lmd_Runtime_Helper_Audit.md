# JS native runtime helper audit

**Date:** 2026-10-17
**Base revision:** `6c14534a7`, following the uncommitted legacy MIR retirement.
**Authority:** USER audit request; **D8.1.3v25**, **D5.3/D5.4**, **D6.2.2v2**,
**D8.4.1v2**.
**Status:** source audit and focused JS verification complete; TypeScript and
aggregate qualification boundaries are recorded below.

## Scope and evidence

Audit native JS support implementations, their inline header bodies, and C ABI
entries embedded in the compiler/interpreter. Frontend and interpreter internals
are consumers. The scan covers the retained AST/AUTO executor, MVP generated
imports, DOM, Node/Jube, GC ownership hooks, builtin catalogs, callbacks and tests.

`utils/js_runtime_helper_audit.py` combines libclang definitions and dependency
edges with an independent ast-grep inventory. It follows function addresses,
callback/data tables and template specializations. Global initializers and
implicit C++ lifetime calls are conservatively retained. Conditional source in
live functions preserves debug-only calls hidden by release preprocessing.
Neither `sys_func_registry.c` nor MVP's `imports[]` constitutes a liveness root;
actual emitted symbol operands do.

The native scan parses **47 translation units**, with no compiler diagnostics,
covering **45 implementation files** with counted bodies and **17 inline-header
files**. Lexical inspection additionally identifies **18** Windows, debug and
execution-profile bodies outside the active release configuration. Their
conditional callers, callbacks and macro aliases remain present. Cross-platform
bodies are retained; they have not been compiled on Windows in this audit.

| Disposition | Functions |
|---|---:|
| Removed: no live caller, callback, emitted import or ABI requirement | 92 |
| Retained: production/host references, callbacks, lifetime or debug checks | 4,404 |
| Retained: conditional build implementations | 18 |
| Retained: test callers only | 5 |
| Total inventoried before cleanup | 4,519 |

The active profile retains **1,226 C ABI functions**; the larger table also
counts private C++ functions, overloads, template bodies and inline methods.
“Referenced” is a conservative source classification, not execution coverage or
proof that every branch containing a caller is reachable.

## Removed code

Remove **87** parsed function bodies, including **63 C ABI entries**, plus five
methods in the entirely unincluded `js_object_pair_traversal.hpp`. Remove their
forward declarations, **44** explicit import rows, one Test262 fast-path catalog
entry and obsolete publication/realm-lookup macros and shutdown-hook plumbing.
No semantic runtime helper is added.

Examples include legacy numeric element/coercion facades, the old class-name
`instanceof` adapter, eager logical/nullish helpers, default typed-array facades
whose callers now use `*_with_ops`, the unused static-import walker, module/TLA
setters and the old async return wrapper. Shared typed-array operations,
compiled-call entries for AUTO promotion, activation hooks, interpreter dynamic
source and precise-root diagnostics remain in use.

This pass removes **1,365 net physical C/C++ source lines**, including declarations
and metadata, in addition to the preceding retirement. Comments/blank lines
were not reformatted to manufacture the reduction.

## Explicit retention decisions at the audit checkpoint

| API | Reason to retain |
|---|---|
| `js_mir_lease_session_create`, `js_mir_lease_session_close` | Existing common-cache ownership/contention tests |
| `js_dynamic_import` | Existing runtime import tests; production uses the reference-aware `_at` entry |
| `js_regex_scanner_needs_backtrack` | Scanner contract tests |
| `js_async_iterator_close_result` | Raw iterator close contract tests |
| `lambda_side_root_contains_span` | Live argument-span diagnostics under `#ifndef NDEBUG` (**D5.3/D5.4**) |

At this checkpoint the document lease adapter's lookup/adopt functions had
callers in `radiant/script_runner.cpp`, while production supplied a null session
and browser documents selected AST execution. Its tested cache session API was
retained conservatively. The follow-up below retires that dormant adapter and
its adapter-specific test; active MVP module artifact/build APIs remain.

## Verification and artifacts at the audit checkpoint

- Release executable, four native Node modules and seven focused native test
  executables build successfully. Final incremental build: exit 0
  (`temp/js_runtime_helper_audit/final_build.log`). The final executable and
  modules are pinned under `qualified/`; `binaries.json` records their hashes.
  None of the 63 removed C ABI symbols remains in the executable.
- The fresh census reports no unexplained unreachable or unparsed candidates.
- Exception catalog validation has zero tier A/B/C violations; the host export
  check passes for the executable and the four rebuilt Node modules.
- Callable catalog validation has zero catalog errors. Its separate Tune4 ratchet
  still reports **38 builtin semantic cases**, identical in the archived
  pre-audit source and current source. It is not a new audit regression.
- An unrestricted export check also encounters the old unsupported
  `lang-python.dylib`; that image was not rebuilt or qualified. The scoped Node
  module export check passes.

| Runtime check | Result |
|---|---:|
| MVP native suite | 812/812 pass |
| Shared JS runtime/script suite | 211/211 pass |
| JS baseline, requested full MIR mode | 509/509 pass |
| Backtracking regex | 53/53 pass |
| Regex router | 4/4 pass |
| Coercion | 15/15 pass |
| Test262 preliminary runner contract | 5/5 input files pass |
| TypeScript | 8/19 pass; 11 unchanged failures |

The six JS suites pass **1,604/1,604** cases. Browser fixtures retain their AST
execution policy. The TypeScript outcomes are identical for all **19** fixtures
against the pinned pre-audit executable
`temp/js_legacy_mir_retirement/qualified/lambda.exe`. The failing typed-function
syntax is rejected before runtime execution (for example, an MVP scope error at
byte 59 in `arrow_functions.ts`). No helper removal caused these failures; the
prior retirement's TypeScript qualification remains open.

Runtime results are recorded in `temp/js_runtime_helper_audit/checks.json` and
`typescript_comparison.json`; the preliminary Test262 log is separate.
The complete per-function inventory, root chains and direct consumers are in
`temp/js_runtime_helper_audit/after/functions.tsv` and `audit.json`. Removed
function manifests, original source copies, release build logs and catalog
comparisons are in the parent directory. Reproduce the source census with:

```sh
python3 utils/js_runtime_helper_audit.py
```

No Test262 harness, exclusions, goldens or deadlines are changed. Full Lambda
and Test262 aggregate baselines and benchmark timing are separate gates and
are not inferred from this source audit or focused checks.


## Document-cache adapter retirement

**2026-10-17, USER; base `b37f6877a`; D8.1.3v25, D8.5.1v2, D5.3/D5.4.**

Remove the `JsMirLeaseSession` type, request/adoption/lease helpers, declarations,
runner state and layout setter hooks. Delete cached document-preamble and
external-classic execution branches; the existing AST document path now stands
alone. Remove `SingleFlightsCommonMirLeaseBuild` and its mode/session-specific
waiter fields. The surviving waiter calls the module build API directly.

The active module-cache implementation is source-identical to the archived
version after substituting its two private forwarding calls with the existing
`input_script_build_scope_begin` / `input_script_build_scope_complete` calls.
Module publication, dependency freshness, code ownership and image execution
are preserved. The module-cache concurrency/reuse tests and AST document-realm
isolation test remain. Layout timing's existing zero-valued cache counters are
retained for schema compatibility.

Source accounting: **490 production C/C++ lines + 75 test lines removed**;
`js_code_cache.cpp` falls from **458 to 171 lines**. The extended native support
count becomes **85,776 lines**, while the comparable 41-file helper count stays
**83,522**. Its historical **2,238-line Legacy MIR-only** removal record remains.

Static checks find no adapter symbols or fixture references in maintained
production/test source. Test declaration comparison confirms exactly the one
adapter fixture was removed. Source copies and checks are recorded under
`temp/js_document_cache_retirement/`; `source_changes.json` contains the file
counts and hashes. Release compile/link qualification passes for `lambda` and
`test_js_script_gtest` (exit 0, `build.log`); `binaries.json` records their hashes.
The linked executable contains no retired adapter symbols.
Runtime tests and benchmark timing were not run for this follow-up; the earlier
checkpoint's pass counts above describe the preceding implementation.
