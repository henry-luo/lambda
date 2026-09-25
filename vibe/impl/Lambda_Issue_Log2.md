# Lambda Language and Runtime Issue Log 2

This log records confirmed issues encountered while implementing Lambda-facing
tools. Each entry distinguishes a Lambda runtime issue from a project-tool or
toolchain issue.

All issues below were first recorded on 2026-09-25.

## Entry format

- **IL2-I#** — short title; IDs are assigned in entry order and remain stable.
  - **Area:** parser, evaluator, MIR, GC/rooting, module system, standard library, or CLI.
  - **Reproduction:** smallest command or script.
  - **Observed / expected:** concrete divergence.
  - **Status:** open, fixed, or not-a-Lambda-issue.
  - **Resolution / ruling:** implementation link and relevant `S#` or `D#` ruling.

## IL2-I1 — `sys.proc.self.argv` retains a null after `--no-log`

- **Area:** CLI / standard library.
- **Reproduction:** `./lambda.exe --no-log test/lambda/ext/test_sysinfo_proc.ls`.
- **Observed / expected:** the argument-vector field printed
  `["./lambda.exe", "test/lambda/ext/test_sysinfo_proc.ls", null]`; the
  trailing `null` comes from the sysinfo argv setter keeping the pre-filter `argc`
  after `--no-log` is removed from `argv`. It should expose only live argument
  strings. The same mismatch can follow removal of `--mem-dump`.
- **Status:** fixed in source. The field is now named `sys.proc.self.argv`
  under S17.3.1; the naming mismatch is closed in IL2-I25.
- **Resolution / ruling:** `lambda/main.cpp` now calls `sysinfo_set_argv` after
  both early argument-filtering loops, when `argc` agrees with the compacted,
  null-terminated `argv`. S17.3.1 defines the public field and its filtered
  vector contract.

## IL2-I2 — `sys.*` access requires explicit path forcing in a procedure

- **Area:** evaluator / standard library.
- **Reproduction:** save `pn main() { print(sys.proc.self.pid) }` and run
  `./lambda.exe --no-log run <script.ls>`.
- **Observed / expected:** printing an unforced path prints the path; this is
  normal lazy-path behavior. `sys.os.platform#` inside `pn` returns `darwin`
  on this host, and `sys.proc.self.cwd#` returns the working directory.
- **Status:** not a Lambda issue; the prior entry misdiagnosed path laziness.
- **Resolution / ruling:** explicit forcing follows S2.4.2v5 and the `p#`
  contract in `doc/Lambda_Sys_Func.md` §Input.

## IL2-I3 — `lambda run` has no script-argument forwarding

- **Area:** CLI.
- **Reproduction:** `./lambda.exe --no-log run <script.ls> value`.
- **Observed / expected:** the runner rejects `value` as an unknown run option;
  the documented `run` command accepts one script path and has no `--` or
  positional-argument forwarding convention. Build/test helpers that take
  arguments cannot currently preserve their Python CLI when invoked as Lambda
  scripts.
- **Status:** open; the current port can expose fixed-target `pn` entry points
  but cannot provide Python-compatible arbitrary CLI arguments.
- **Resolution / ruling:** this CLI behavior has no `S#`/`D#` ruling and no
  documented script-argument contract. The user-facing reference is
  `doc/Lambda_CLI.md` §`run`.

## IL2-I4 — Lambda has no resumable generator protocol

- **Area:** language / evaluator.
- **Reproduction:** compare the source operations in
  `test/py/test_py_generators.py`: `yield`, `next`, `send`, `yield from`, and
  iterator exhaustion.
- **Observed / expected:** Lambda can express the same finite values and state
  transitions eagerly, but it has no suspended generator value that resumes
  at a later `next`/`send` call. The Lambda counterpart therefore models the
  counter and accumulator as explicit values; it does not exercise lazy
  generator semantics.
- **Status:** open feature gap; the misleading eager-values fixture was removed.
- **Resolution / ruling:** no `S#`/`D#` ruling defines generators. The related
  stream work is still marked pending under S14.2–S14.3, but that does not
  decide Python-style generator semantics.

## IL2-I5 — failed `cmd()` hides stderr and exit status

- **Area:** standard library / error reporting.
- **Reproduction:** run a procedure containing
  `cmd("python3 -c 'import sys; print(\"hidden stderr detail\", file=sys.stderr); sys.exit(7)'")^`.
- **Observed / expected:** the process prints only
  `Error: Script execution failed: <script-path>`. `pn_cmd2` retains the child
  exit code internally but returns a payload-free `ItemError`; `shell_exec_line`
  captures stdout, not stderr. The caller should receive an error with the
  command's exit status and diagnostic text.
- **Status:** open; the Python-delegating adapters were removed.
- **Resolution / ruling:** ordinary failures must carry useful diagnostics
  under S7.4.4. The implementation is in
  `lambda/runtime/lambda-proc.cpp` (`pn_cmd2`).

## IL2-I6 — no native process execution and result API for build/test drivers

- **Area:** standard library / procedural I/O.
- **Reproduction:** `test/interp/tier_sweep.py` runs the Lambda executable with
  selected environment, timeout, captured stdout/stderr, and exit status;
  `utils/check_module_boundary.py` runs `nm -u` to inspect linked symbols.
- **Observed / expected:** Lambda exposes `cmd()` as a shell command with a
  stdout string result, but no documented structured process API for argv,
  environment, timeout, stderr, and exit status. Under the port's requirement
  to avoid shell and other language runtimes, these tasks cannot be expressed
  end to end. Lambda also has no documented native object-file symbol reader.
- **Status:** open capability gap; scripts needing process or binary inspection
  have no equivalent Lambda implementation yet.
- **Resolution / ruling:** D1.10 covers the fuzz-support inventory but does
  not prescribe process execution. No formal `S#`/`D#` ruling defines a native
  subprocess or object-file inspection API.

## IL2-I7 — forcing `sys.os.platform` reports a shutdown allocation leak

- **Area:** system information / memory management.
- **Reproduction:** put `pn main() { print(string(sys.os.platform#)) }`
  in a script under `./temp/` and run it with `./lambda.exe --no-log run`.
- **Observed / expected:** the query returns `darwin`, but shutdown reports
  `memtrack: LEAK — 1 allocations (128 bytes) still live` in the `system`
  category. A read-only sysinfo query should clean up its allocation.
- **Root cause:** `sysinfo_init` allocated a thread-local `SysinfoCache` with
  `mem_calloc`, and no normal heap teardown called `sysinfo_shutdown`. The
  separate sysinfo `Input` also owned an arena, name pool, and type registry
  that were not released before the evaluation pool ended.
- **Status:** fixed and checked on the direct script, full category script,
  and two-script heap-reset batch; each exited with zero live tracked bytes.
  The Lambda baseline passed 5,905/5,905 checks.
- **Resolution / ruling:** sysinfo now stores pool-owned cache metadata in the
  current `EvalContext` capsule, creates all values through its `Input` and
  `MarkBuilder`, and releases the Input's auxiliary resources and arena from
  `heap_destroy` before the pool ends. CWD is copied from a caller buffer into
  the Input, so it has no separately allocated temporary string. Explicit
  `sys.*` resolution refreshes TTL-managed values and clears a path's old
  formatted result if a later lookup fails. D4.1.3, D4.2.1v3, D4.2.3, and
  D5.4.2 govern the ownership boundary.

## IL2-I8 — imported port unexpectedly ran its own `main`

- **Area:** module initialization / procedural runner.
- **Reproduction:** the first version of
  `utils/lambda_equiv/patch_static_module_makefile.ls` contained both an
  exported `patch_makefile(config_path, makefile_path, root)` procedure and a
  `pn main()` that patched `build/premake/lambda-exe.make`. A separate driver
  imported it and called `patch_makefile` on a copy under `./temp/`. Even when
  the driver called no exported function, the real generated Makefile was
  patched and `sys.os.platform#` from the imported `main` reported a leak.
- **Observed / expected:** importing a module for a callable procedure should
  not execute its `main`. The generated Makefile was restored, and the
  reusable implementation was separated from its entry point.
- **Root cause:** the MIR import-cone runner set the shared context's
  `run_main` flag from the driver before calling each imported module's entry.
  The generated entry tested only that flag, so `lambda.exe run` also invoked
  an imported module's `pn main()`.
- **Status:** fixed in the MIR import-cone runner and module-entry emission;
  a build completed, but a runtime regression check has not been run.
- **Resolution / ruling:** imported entries now initialize with `run_main`
  false, and MIR emits the automatic `main` call only for an entry script.
  The interpreter already initializes imported modules without calling their
  `main`. D7.2.1 and D7.2.2 distinguish module initialization from entry
  execution; S12.1.1v2 places procedural top-level work in `main()`.

## IL2-I9 — Python mutable closures have no Lambda closure equivalent

- **Area:** language semantics.
- **Reproduction:** `test/py/test_py_closures.py` returns `increment()` from
  `make_counter()`, and each call mutates a captured `nonlocal count`.
- **Observed / expected:** Lambda closures capture immutable snapshots;
  captured assignment is a compile error. A caller-passed counter can produce
  the same numbers but does not preserve the Python closure's API or state.
- **Status:** intentional semantic difference; the misleading fixture was
  removed.
- **Resolution / ruling:** S9.1.4 explicitly forbids mutable captured state.

## IL2-I10 — JSON formatter has no ASCII-escaping option

- **Area:** JSON formatting / standard library.
- **Reproduction:** format `{name: "café", emoji: "😀"}` with
  `format(value, {type: "json", indent: 2})`; compare the output bytes with
  Python's default `json.dumps(value, indent=2)`.
- **Observed / expected:** Lambda emits UTF-8 characters, while Python's
  default escapes non-ASCII code points as `\uXXXX` (including surrogate pairs
  for supplementary characters). The manifest updater requires byte-equivalent
  JSON output, so its Lambda port explicitly performs Unicode escaping after
  formatting.
- **Status:** missing formatter option; the port implements the conversion in
  Lambda itself.
- **Resolution / ruling:** no `S#`/`D#` ruling sets a JSON ASCII-escape policy.
  `doc/Lambda_Sys_Func.md` §`format(data)` documents type and indent options
  but no `ensure_ascii` equivalent.

## IL2-I11 — stale `int64(x)` documentation names no constructor

- **Area:** type conversion / documentation.
- **Reproduction:** compile `pn main() { let n = int64(2166136261); print(n) }`.
- **Observed / expected:** the compiler reports E204, `unknown type 'int64';
  did you mean 'i64'?`, although `doc/Lambda_Sys_Func.md` §Type Conversion
  previously listed `int64(x)` as the explicit widening constructor. An `i64`
  literal such as `2166136261i64` compiles and the catalog generator uses it.
- **Status:** fixed documentation mismatch. Current API docs use `i64(x)`.
- **Resolution / ruling:** S17.5.1 names all callable sized-integer
  conversions and makes `i64(x)` the signed full-width spelling. The runtime
  registry already uses `i64`; no constructor alias was added.

## IL2-I12 — `find` offsets disagree with string slicing after UTF-8 text

- **Area:** string search and indexing.
- **Reproduction:** before the fix, `find("éabc", "abc")^[0].index` returned
  `2`, while `index_of("éabc", "abc")` returned `1`; `slice("éabc", 2, 5)`
  returned `"bc"`.
- **Observed / expected:** `find` reports a byte offset, but `index_of` and
  `slice` use character positions. Passing a match index to `slice` loses the
  first character after non-ASCII text. The native ports now use `index_of`
  for source offsets.
- **Status:** fixed in source; the literal and pattern search paths now return
  code-point indices, and the system-function reference states their unit.
- **Resolution / ruling:** S17.4.1 makes `find` offsets use S2.5.8's string
  code-point index domain, shared with `index_of`, subscripts, and `slice`.
  Both search paths count code points between successive visible matches;
  zero-width pattern search advances a whole code point.

## IL2-I13 — `fill` loses the declared element type

- **Area:** system-function type inference.
- **Reproduction:** `var words: i64[] = fill(64, 0i64)` fails with E201 because
  the call is inferred as `array[num]`. An array comprehension of `0i64`
  values satisfies the same `i64[]` declaration.
- **Observed / expected:** the documented `fill(n, value: as T) T[]` relation
  should preserve the element type for this call.
- **Status:** open inference mismatch; native SHA-256 uses an explicitly typed
  comprehension for its word arrays.
- **Resolution / ruling:** S11.4.9 specifies that the registry type relation
  constructs an array over the argument's inferred type.

## IL2-I14 — raised error in a `for` iterable is silently skipped

- **Area:** procedural error propagation.
- **Reproduction:** `pn bad() { raise error("scan failed") }` and
  `pn main() { for (value in bad()) { print(value) }; print("after\\n") }`
  exit successfully and print `after`.
- **Observed / expected:** the iterable error should surface instead of
  becoming an empty iteration. This was encountered while scanning the
  feature inventory; the port now declares `string[]^` and engages `^` at
  calls so failures propagate.
- **Status:** open; the exact interaction with an unannotated `pn` return
  needs investigation.
- **Resolution / ruling:** S7.4.2 requires immediate engagement of raised
  errors, and S7.4.4 requires diagnostics at checked-channel failures.

## IL2-I15 — source-tree traversal silently truncates during nested scans

- **Area:** procedural iteration / path traversal.
- **Reproduction:** in `./temp/`, iterate `for (path in \\.lambda.**)`, count
  `.c`/`.cpp`/`.h`/`.hpp`/`.ls` files, and print the count after the loop.
  The count is `1078`. In the same loop, read each file with
  `input(path, "text")^` and call a Lambda helper that collects `index_of`
  positions in an array; the count stops at `476` with exit status 0. Reading
  and a direct `index_of` call alone still count `1078`. A fuller native port
  of the DOM editable source checker stopped around 40 files and missed its
  registry owner.
- **Observed / expected:** the loop should visit every matching path or report
  a failure. Returning a successful partial scan makes a source gate unsound.
  The incomplete DOM checker was removed. The same audit found that the native
  exception-catalog scanner visited only `21` of `99` `.c`/`.cpp` helper files
  and reported zero violations. It now collects the same four extension/path
  groups as Python before reading and scans all `99`; both its default and
  Makefile outputs match Python byte for byte. The first
  Node architecture `--report` counted only `6` of `38` `node_core` sources.
  Its Python implementation gathers and sorts paths before scanning them; a
  native port of that same two-stage traversal now counts all `38` sources
  and matches the Python report as JSON.
  A restored DOM editable architecture gate now gathers all 1,250
  Python-visible source paths before scanning and reports the same current
  registry-owner failure as Python.
- **Status:** open; the cause within nested iteration, allocation, or error
  propagation has not been isolated.
- **Resolution / ruling:** no formal `S#`/`D#` ruling defines recursive path
  iteration. S7.4.2 governs engaged errors; D4's precise lifetime rules are
  relevant if this turns out to be an iterator-rooting defect.

## IL2-I16 — JSON `indent: 1` formats with two spaces

- **Area:** JSON formatter.
- **Reproduction:** compare `format([{x: 1}], {type: "json", indent: 1})`
  with Python `json.dumps([{"x": 1}], indent=1)`.
- **Observed / expected:** Lambda indents the nested object and key with two
  and four spaces, respectively; Python uses one and two. The native
  documentation-block extractor produces the same 539 JSON index entries and
  identical 534 `.ls` files as the Python extractor, but the index bytes
  differ solely because of indentation.
- **Status:** open formatter option mismatch.
- **Resolution / ruling:** no formal `S#`/`D#` ruling fixes JSON indentation.
  `doc/Lambda_Sys_Func.md` §`format(data)` accepts a numeric `indent` option;
  the formatter should honor the requested width or document its constraint.

## IL2-I17 — no native Git index or compiler-AST inspection API

- **Area:** build/test tooling APIs.
- **Reproduction:** `utils/lint/rules/structural/no_new_per_file_header.py`
  distinguishes newly staged or untracked Radiant headers through
  `git status --porcelain`; `utils/struct_census.py` and
  `utils/lint/tidy/explicit_cast_check.py` inspect libclang cursor kinds,
  declarations, and record layouts.
- **Observed / expected:** Lambda has documented file traversal and text I/O,
  but no native Git index/status reader or compiler AST and layout interface.
  A text search cannot preserve these tools' decisions, and invoking `git` or
  clang through a shell would violate the native-port requirement.
- **Status:** open capability gaps; the affected scripts remain unported.
- **Resolution / ruling:** D8 governs Lambda compilation, but it defines no
  script-facing Git or compiler-introspection API. No formal `S#` ruling
  supplies one.

## IL2-I18 — `sort(vec, key)` ignores the key for map rows

- **Area:** collection sorting.
- **Reproduction:** sort `[{name: "js_tla_enter_module", effect: "PRESERVES"},
  {name: "js_throw_value", effect: "SETS"},
  {name: "lambda_unit_const_at", effect: "PRESERVES"}]` with
  `sort(rows, ~.name)`. Lambda puts `js_throw_value` after `lambda_unit_const_at`.
- **Observed / expected:** ordering by `name` puts `js_throw_value` before
  `js_tla_enter_module`. The result instead groups the maps by `effect`,
  consistent with the key function being ignored. The exception census sorts
  a string array of names and looks rows up by name to preserve its report.
- **Status:** open runtime/documentation mismatch.
- **Resolution / ruling:** no formal `S#`/`D#` ruling specifies `sort`'s key
  callback. `doc/Lambda_Sys_Func.md` §Collection Functions explicitly
  documents `sort(vec, fn)` as sorting by the key function.

## IL2-I19 — string paths with spaces fail `exists` and `input`

- **Area:** filesystem I/O.
- **Reproduction:** the existing file
  `lambda/tree-sitter-lambda/src/tree_sitter/parser copy.h` returns `false`
  from `exists("lambda/tree-sitter-lambda/src/tree_sitter/parser copy.h")`;
  `input` on the same string raises E400. The equivalent path literal
  `\.lambda.'tree-sitter-lambda'.src.tree_sitter.'parser copy.h'` returns
  `true` from `exists` and reads 7,624 bytes.
- **Observed / expected:** the documented `exists(target)` and `input(target)`
  accept file targets, and the string spelling names an existing file. The
  static-module inventory retains globbed path values to read such files.
- **Root cause:** the URL scanner correctly encoded the space as `%20`, but
  `target_exists`, `target_is_dir`, `target_to_local_path`, and both local-file
  `input` dispatches passed the encoded URL pathname to filesystem calls.
- **Status:** fixed in local-file target conversion and input dispatch; a
  build completed, but a runtime regression check has not been run.
- **Resolution / ruling:** file URL pathnames now pass through the existing
  `url_to_local_path` decoder before filesystem access. URL strings remain
  encoded in URL objects; the decoded path is temporary caller-owned storage.
  No formal `S#`/`D#` ruling specifies this conversion; `doc/Lambda_Sys_Func.md`
  §`exists(target)` documents file targets.

## IL2-I20 — path values fail when stored directly in map fields

- **Area:** path value storage / runtime.
- **Reproduction:** collect `[{label: string(path), target: path}]` while
  iterating `\.lambda.input.css.**`. The first `target` reads as type
  `error`, and `exists(records[0].target)` returns `false`. Storing the path
  directly in an array and then accessing `paths[0]` retains type `path`
  and `exists` returns `true`.
- **Observed / expected:** a path value should retain its type when assigned
  to a map field. The static-module inventory stores path values in a flat
  array and sorts them natively; its complete JSON output matches Python.
- **Status:** open runtime value-storage defect; the exact map/GC cause has
  not been isolated.
- **Resolution / ruling:** D2 defines the tagged value representation and D4
  governs runtime container ownership; neither permits a path value to
  silently turn into an error on map insertion.

## IL2-I21 — no documented procedural regular-expression API

- **Area:** source-analysis system functions.
- **Reproduction:** `utils/js_callable_census.py`, `utils/check_gc_effects.py`,
  and `utils/check_hosted_python_architecture.py` use counted regex matches
  and captures over source text. `doc/Lambda_Sys_Func.md` documents string
  search and type-pattern matching but no runtime regex compile/search/findall
  or capture API for a procedural script.
- **Observed / expected:** these utilities require either a native regex
  surface or a task-specific lexical parser written in Lambda. The DOM and
  static-module ports use lexical scanners for their narrower patterns.
- **Status:** open missing system-function surface; no claim that the source
  census scripts are equivalent until their full pattern behavior is ported.
- **Resolution / ruling:** no formal `S#`/`D#` ruling defines a procedural
  regex API. `doc/Lambda_Sys_Func.md` §String Functions omits one.

## IL2-I22 — MIR Direct loses a return after a possibly empty `for`

- **Area:** procedural return / MIR Direct.
- **Reproduction:** under `LAMBDA_TIER=jit`, define
  `pn result() string { for (at in []) { return "hit" }; return "" }`.
  `type(result())` was `null`. Without the `for`, it was `string`. An unused
  map-assignment procedure initially exposed this defect by causing the
  default tier planner to fall back to whole-module JIT; the map assignment
  itself was not the cause.
- **Root cause:** `transpile_for` left `mt->block_returned` set by a return in
  the loop body. The loop exit is reachable with zero iterations, but the
  later return saw that stale flag and skipped its MIR `ret`; the fallthrough
  value became `null`.
- **Resolution:** `transpile_for` now restores the pre-loop return state at
  the loop exit. `test/lambda/proc/for_return_after_empty.ls` covers empty
  and nonempty iteration, with a matching `.txt` golden. The forced-JIT
  result matches that golden; `make test-lambda-baseline` passed 5,905/5,905
  combined cases. The native callable census now matches Python's full JSON
  inventory and default/`--check` text output.
- **Status:** fixed.
- **Resolution / ruling:** S12.1.2 says a `return` inside `for` exits the
  function when it runs; D8.2.6 governs the MIR result and return boundary.

## IL2-I23 — no native Lizard-compatible duplicate-block engine

- **Area:** source-analysis tooling.
- **Reproduction:** `test/dedup/check_code_dup.py` runs the external Lizard
  `-Eduplicate -l cpp` analysis and then applies its configured file and block
  exclusions to Lizard's duplicate families. Lambda has no documented
  Lizard-compatible duplicate-block scanner or source token/family API.
- **Observed / expected:** omitting Lizard would change which duplicate
  families are found and make the ratchet ineffective. Calling Lizard through
  `cmd()` would violate the native-port requirement. A native Lambda clone
  detector would need to match Lizard's family and location semantics before
  it could replace the Python driver.
- **Status:** open capability gap; no equivalent Lambda gate yet.
- **Resolution / ruling:** no formal `S#`/`D#` ruling defines a source-clone
  API or Lizard compatibility. The ratchet behavior is defined by
  `test/dedup/check_code_dup.py` and its configuration.

## IL2-I24 — string-heavy array growth degrades on large source scans

- **Area:** collection performance in procedural Lambda.
- **Reproduction:** in a release build, reading and splitting the 1.66 MB
  `lambda/js/js_runtime.cpp` takes about 0.002 seconds, but appending its
  34,562 string lines one by one with `push(copied, row)` did not complete
  within 30 seconds. Appending 50,000 integers took about 0.005 seconds.
  The C/C++ source cleaner likewise stalled while pushing every cleaned line;
  joining batches of 128 lines completed the cleaner in about 0.14 seconds.
- **Observed / expected:** `push` with many strings has severe scaling that
  blocks straightforward source scanners. The exact runtime or GC cause has
  not been isolated; the release-only comparison rules out debug-build cost.
- **Status:** open performance defect, with a batched scanner implementation
  in progress. No GC checker is counted as ported on this basis.
- **Resolution / ruling:** no formal `S#`/`D#` ruling specifies collection
  operation complexity. D2 and D4 govern the value and ownership model, but
  do not explain this scaling.

## IL2-I25 — `sys.proc.self.argv` differs from the former implementation

- **Area:** system information / documentation.
- **Reproduction:** before the fix, `sys.proc.self.argv` resolved to null;
  the implementation exposed the same argument vector as `args` instead.
- **Observed / expected:** `lambda/runtime/sysinfo.cpp` used `args`, while
  `doc/Lambda_Data.md` and `vibe/Lambda_IO_Sysinfo.md` documented `argv`.
- **Status:** fixed; `argv` is the only public field, with no `args` alias.
- **Resolution / ruling:** S17.3.1 names `sys.proc.self.argv`, defines its
  filtered command-line vector, and excludes the old alias. The resolver and
  system-information examples now use that spelling.
