# Native Lambda ports of Python build/test utilities

Run completed entry points from the repository root with
`./lambda.exe --no-log run utils/lambda_equiv/<name>.ls`. These scripts use
Lambda language and native I/O only; none invokes a shell, Node, or Python.
The original Makefile still invokes its Python utilities.
This inventory has 36 Python utilities: 15 native ports with verified current
behavior, 16 blocked by missing process/binary/compiler interfaces, and 5
still pending. An entry point explicitly described as in progress is not a
passing replacement gate.

| Python utility | Native port status |
|---|---|
| `utils/update_jube_manifest_integrity.py` | Core procedure `update_jube_manifest_integrity.ls` works; CLI argument forwarding is unavailable. Import and call `update_manifest(module_dir)`. |
| `utils/patch_static_module_makefile.py` | `patch_static_module_makefile.ls` complete for the default host invocation; core procedure is in `patch_static_module_makefile_core.ls`. |
| `utils/check_js_node_test_separation.py` | `check_js_node_test_separation.ls` complete for the repository gate. |
| `test/error_handling/check_recovery_boundaries.py` | `check_recovery_boundaries.ls` complete for the repository gate. |
| `test/interp/gen_bench.py` | `gen_bench.ls` complete for the default corpus; custom CLI options remain blocked by argument forwarding. |
| `utils/generate_premake.py` | Pending native generator rewrite. |
| `utils/generate_well_known_names.py` | `generate_well_known_names.ls` renders all eight files from the catalog data; `generate_well_known_names_check.ls` matched all eight committed outputs byte for byte. |
| `utils/test_jube_module_loader_negative.py` | Blocked on native process execution/capture. |
| `utils/test_jube_language_dispatch.py` | Blocked on native process execution/capture. |
| `utils/test_hosted_python_architecture_checker.py` | Pending native checker and self-test rewrite. |
| `utils/test_node_module_architecture_checker.py` | `test_node_module_architecture_checker.ls` covers its source and symbol predicate self-test; the separately chained binary gate remains unavailable. |
| `utils/test_premake_generator.py` | Blocked on native process execution/capture. |
| `utils/check_node_module_architecture.py` | `check_node_module_architecture.ls` covers the source/config gate; `check_node_module_architecture_report.ls` matched Python's current JSON report, including all 38 `node_core` sources. Binary modes lack a native object/import table reader. |
| `utils/check_hosted_python_architecture.py` | Pending native source checker and inventory; binary modes also lack a native object/import table reader. |
| `utils/check_static_module_architecture.py` | `check_static_module_architecture.ls` renders the default report; `_json.ls` and `_boundary_baseline.ls` cover its two JSON modes. The complete JSON inventory matched Python semantically, including all 35 weak providers. CLI argument forwarding remains unavailable. |
| `utils/check_module_boundary.py` | Blocked on native object-file symbol inspection. |
| `test/interp/tier_sweep.py` | Blocked on native process execution/capture and environment control. |
| `test/interp/refresh_lists.py` | Blocked on a native rerun of rejected scripts for fallback reasons. |
| `test/interp/run_bench.py` | Blocked on native process execution and per-run resource accounting. |
| `test/interp/repl_bench.py` | Blocked on native process execution/capture. |
| `utils/check_gc_effects.py` | Pending native source scanner rewrite. |
| `utils/check_gc_root_hazards.py` | Pending native source scanner rewrite. |
| `utils/check_state_machine.py` | `check_state_machine.ls` parses the enum and rule tables, validates all 14 complete families and 14 invariant bindings, and matched the Python checker's current output byte for byte. |
| `utils/lint/rules/structural/check_dom_editable_architecture.py` | `check_dom_editable_architecture.ls` checks the D7.2.5/D7.5.3 source boundary. It gathers all 1,250 Python-visible production sources before reading them and reports the same current registry-owner failure as Python. |
| `utils/lint/rules/structural/no_new_per_file_header.py` | Blocked on a native Git index/status reader; its decision depends on staged and untracked status. |
| `utils/lint/tidy/explicit_cast_check.py` | Blocked on a native libclang AST/type interface. |
| `utils/extract_doc_blocks.py` | `extract_doc_blocks.ls` extracts the current 534 runnable units and 5 no-run entries; every `.ls` matched byte for byte and its index matched as JSON. The formatter uses two-space indentation for `indent: 1`, so index bytes differ. This extractor is called by `check_doc_blocks.py`. |
| `utils/js_exception_catalog_census.py` | `js_exception_catalog_census.ls` and mode entry points cover the default, `--violations`, `--show-void`, and Makefile `--prefix js_ --violations` reports; the default and Makefile outputs matched Python byte for byte across all 99 helper files. Arbitrary `--root` and CLI argument forwarding remain unavailable. |
| `utils/js_callable_census.py` | `js_callable_census.ls`, `_check.ls`, and `_json.ls` cover the default, ratchet, and JSON modes. The full JSON matched Python semantically; default and `--check` text matched byte for byte. Arbitrary `--root` and CLI argument forwarding remain unavailable. |
| `test/node/node_official_report.py` | Blocked on native process execution/capture. |
| `test/fuzzy/lambda/run_fuzz.py` | Blocked on native process execution/capture and timeouts. |
| `test/fuzzy/lambda/feature_inventory.py` | `feature_inventory.ls` verifies all 515 source rows, eight campaign references, and the committed lock using native `sha256.ls`. `feature_inventory_list.ls` and `feature_inventory_print_lock.ls` cover its other output modes. |
| `utils/analyze_binary.py` | Blocked on native object-file symbol inspection. |
| `utils/struct_census.py` | Blocked on a native libclang AST/layout interface. |
| `utils/check_doc_blocks.py` | Blocked on native compiler invocation with captured diagnostics. |
| `test/dedup/check_code_dup.py` | Blocked on a native equivalent of Lizard's duplicate-block algorithm; its Python script invokes Lizard and applies policy to its output. |

`c_function_scan.ls` and `gc_source_paths.ls` are unfinished shared helpers
for the GC checks. They do not enforce either Python checker yet.

The Python language fixtures are separate from these utilities.
`test/py/test_py_basic.py` has a Lambda counterpart at
`test/lambda/proc/python_equiv_basic.ls` with its `.txt` expected result.
`test_py_closures.py` requires a mutable captured `nonlocal` variable, which
Lambda rules out under S9.1.4. `test_py_generators.py` requires suspended
`yield`/`send` generators; the related stream surface is still pending under
S14.2–S14.3. Their prior value-printing substitutes were removed. Confirmed
runtime and CLI gaps are recorded in `vibe/impl/Lambda_Issue_Log2.md`.
