# Native Lambda port of Python build and test utilities

**Status:** in progress, 2026-09-25. This is an implementation record, not a
language or architecture ruling.

## Goal and scope

Implement the Python tasks used by Lambda build and test workflows as Lambda
scripts. Each port must perform its work in Lambda: launching Python, Node, or
a shell from a Lambda wrapper does not count. Keep the original Python tool as
the behavioral reference until the native port covers its required modes.

The inventory in [the port README](../../utils/lambda_equiv/README.md) has 36
Python utilities. As of this snapshot, **15 have verified native behavior for
the modes stated below, 5 have unfinished native implementations, and 16 are
blocked by missing native capabilities or substantial algorithms**. These
counts describe porting progress, not wholesale replacement of the Python
tools. The Makefile still runs the Python versions.

Run a completed fixed-mode entry point from the repository root with
`./lambda.exe --no-log run utils/lambda_equiv/<name>.ls`. The ports and shared
helpers are under [`utils/lambda_equiv/`](../../utils/lambda_equiv/). Several
Python command-line options cannot yet be forwarded to `lambda run`; where
useful, separate `.ls` entry points expose fixed modes. The missing argument
contract is recorded as IL2-I3 in
[Lambda_Issue_Log2.md](Lambda_Issue_Log2.md).

## Native behavior implemented and checked

The evidence column states what was compared or exercised. It does not claim
coverage for modes absent from that column.

| Python utility | Lambda implementation and evidence |
|---|---|
| `utils/update_jube_manifest_integrity.py` | `update_jube_manifest_integrity.ls` exposes `update_manifest(module_dir)` and native JSON/ASCII escaping; arbitrary CLI arguments remain unavailable. |
| `utils/patch_static_module_makefile.py` | `patch_static_module_makefile.ls` and its core cover the default host invocation. |
| `utils/check_js_node_test_separation.py` | `check_js_node_test_separation.ls` covers the repository gate. |
| `test/error_handling/check_recovery_boundaries.py` | `check_recovery_boundaries.ls` covers the repository gate. |
| `test/interp/gen_bench.py` | `gen_bench.ls` generates the default corpus; custom options remain unavailable. |
| `utils/generate_well_known_names.py` | Generator and check entry points render all eight files; the check matched committed bytes. |
| `utils/test_node_module_architecture_checker.py` | Native self-test covers its source and symbol predicates; the chained binary gate is still unavailable. |
| `utils/check_node_module_architecture.py` | Source/config gate and report entry points; the JSON report matched Python, including 38 `node_core` sources. Binary modes remain unavailable. |
| `utils/check_static_module_architecture.py` | Default, JSON, and boundary-baseline entry points; the JSON inventory matched Python semantically, including 35 weak providers. Arbitrary CLI arguments remain unavailable. |
| `utils/check_state_machine.py` | Checks 14 complete families and 14 invariant bindings; current output matched Python byte for byte. |
| `utils/lint/rules/structural/check_dom_editable_architecture.py` | Native gate gathers all 1,250 Python-visible production sources and reports the same current registry-owner failure. The D7.2.5/D7.5.3 boundary is the policy being checked. |
| `utils/extract_doc_blocks.py` | Extracts 534 runnable units and 5 no-run entries; generated `.ls` files matched byte for byte and the index matched as JSON. Its JSON indentation differs in bytes. |
| `utils/js_exception_catalog_census.py` | Default, violations, show-void, and Makefile prefix modes have entry points; default and Makefile output matched Python byte for byte across 99 helper files. Arbitrary root/CLI options remain unavailable. |
| `utils/js_callable_census.py` | Default, check, and JSON modes have entry points; text matched byte for byte and full JSON matched semantically. Arbitrary root/CLI options remain unavailable. |
| `test/fuzzy/lambda/feature_inventory.py` | Checks 515 source rows, eight campaign references, and the committed lock with a native SHA-256 helper; list and print-lock modes have entry points. |

The reusable work includes path traversal, source lexing, text/JSON encoding,
and SHA-256 helpers. The native scripts use Lambda I/O and do not delegate
their task to another runtime.

## Work still outstanding

### Five unfinished implementations

| Python utility | Remaining work |
|---|---|
| `utils/generate_premake.py` | Implement the complete config-to-Premake generation in Lambda, then compare generated files. Generated `.lua` files must still come from `build_lambda_config.json`, not hand edits. |
| `utils/check_hosted_python_architecture.py` | Port the source boundary predicates and full inventory; binary modes also need native object/import inspection. |
| `utils/test_hosted_python_architecture_checker.py` | Port its self-test against the native checker after that checker exists. |
| `utils/check_gc_effects.py` | Complete lexical function discovery, registry extraction, and transitive NO_GC call-graph audit. The Python checker currently reports real violations; a native port must preserve that failure, not turn it into a pass. |
| `utils/check_gc_root_hazards.py` | Complete the exact-root structural predicates and compare every detected violation and function count with Python. |

`c_function_scan.ls` and `gc_source_paths.ls` are **unfinished helpers** for the
two GC checks. The cleaner and delimiter matcher were developed against a
large JS source, but full function extraction has not finished or matched the
Python function inventory. No GC checker entry point is treated as a gate.
An incomplete root-hazard entry point was removed because it counted functions
without checking hazards.

### Sixteen blocked ports

| Missing capability or algorithm | Affected Python utilities |
|---|---|
| Native process execution with argv, environment, timeout, stdout/stderr, exit status, and where needed resource accounting | `utils/test_jube_module_loader_negative.py`; `utils/test_jube_language_dispatch.py`; `utils/test_premake_generator.py`; `test/interp/tier_sweep.py`; `test/interp/refresh_lists.py`; `test/interp/run_bench.py`; `test/interp/repl_bench.py`; `test/node/node_official_report.py`; `test/fuzzy/lambda/run_fuzz.py`; `utils/check_doc_blocks.py`. The process being run by these drivers is normally Lambda or another required build/test executable; the Lambda port must not invoke a shell, Node, or Python to do its own work. |
| Native object-file symbols and import-table inspection | `utils/check_module_boundary.py`; `utils/analyze_binary.py`. Binary modes of the partially ported architecture checkers also need this. |
| Native Git staged/untracked status | `utils/lint/rules/structural/no_new_per_file_header.py`. |
| Native compiler AST, types, and record-layout information | `utils/lint/tidy/explicit_cast_check.py`; `utils/struct_census.py`. |
| Lizard-compatible duplicate-block discovery and family/location semantics | `test/dedup/check_code_dup.py`. |

These rows account for all 16 blocked utilities. A text-only approximation or
a call through `cmd()` would not preserve their decisions under the native-port
requirement. No replacement gate should be enabled until it produces the same
result on the supported inputs.

### Remaining parity and integration work

- Add a script-argument mechanism or native fixed entry points for every
  required Python invocation, including custom roots and options. The CLI
  argument behavior has no formal `S#`/`D#` ruling yet; see the issue log.
- Match binary modes of the Node and hosted-Python architecture tools and the
  binary portion of the Node checker self-test.
- Resolve byte-level JSON index formatting for `extract_doc_blocks.py` if its
  output must be byte-identical. The issue is Lambda's `indent: 1` behavior,
  recorded in the issue log; semantic JSON equality was already checked.
- Wire only fully equivalent native entry points into build/test commands,
  and retain the Python commands as the reference until each comparison is
  complete. No Makefile migration has been done.

## Runtime findings and verification

The porting work exposed several Lambda defects and missing surfaces. Their
reproductions, status, and rulings are in
[Lambda_Issue_Log2.md](Lambda_Issue_Log2.md): missing script arguments and
structured process results, absent binary/Git/compiler-AST APIs, source-tree
iteration truncation, path value and UTF-8 offset mismatches, JSON formatting
differences, missing procedural regex, and slow string-heavy array growth.
IL2-I8's imported-`main` execution is fixed in the MIR module-entry path:
imports now initialize without the driver's `run_main` flag, while the driver
still invokes its own `main` when requested. D7.2.1 and D7.2.2 govern the
module initialization boundary; S12.1.1v2 governs procedural `main` work.
IL2-I19's string-path handling is fixed at the URL-to-filesystem boundary:
the URL scanner encodes spaces, and local-file operations now decode the
pathname before accessing a file. No formal `S#`/`D#` ruling covers that
conversion; `doc/Lambda_Sys_Func.md` documents the accepted file targets.
S7.4.4 governs useful error payloads; D2 and D4 govern value representation
and ownership where path storage or collection lifetime is implicated. The
log distinguishes observed defects from capability gaps and intentional
language differences.

One runtime defect was fixed during the ports: `transpile_for` now restores
the pre-loop return state on a reachable loop exit. Without it, a `return`
after a possibly empty `for` could be omitted in MIR Direct. The fix is in
[`transpile-mir.cpp`](../../lambda/runtime/transpile-mir.cpp), with
[`for_return_after_empty.ls`](../../test/lambda/proc/for_return_after_empty.ls)
and its `.txt` golden. The behavior follows S12.1.2 and the MIR result
boundary in D8.2.6. The forced-JIT case matched its golden, and the Lambda
baseline passed **5,905/5,905** combined cases. A subsequent `make release`
completed successfully; no complete GC-checker run was established from the
unfinished scanner.

The three `test/py/test_py_*.py` language fixtures are separate from the 36
utilities. `test_py_basic.py` has
[`python_equiv_basic.ls`](../../test/lambda/proc/python_equiv_basic.ls) and a
golden. The closure fixture needs Python's mutable `nonlocal` capture, which
S9.1.4 forbids in Lambda. The generator fixture needs resumable `yield` and
`send`; the related stream surface remains pending under S14.2–S14.3. Eager
value-printing substitutes were removed because they did not exercise those
semantics.

## Suggested continuation order

1. Finish and compare the source-only GC and hosted-Python checkers. Match
   Python's file inventory, function count, diagnostics, and exit result;
   investigate root causes of scanner failures before changing runtime code.
2. Complete the Premake generator and its output comparison without editing
   generated `.lua` files by hand.
3. Implement the missing native process, binary, Git, and compiler inspection
   surfaces required by the blocked tools, then port each driver and its
   supported CLI modes.
4. Recheck each port against its Python reference and migrate build/test
   invocations only after equivalent behavior is demonstrated.
