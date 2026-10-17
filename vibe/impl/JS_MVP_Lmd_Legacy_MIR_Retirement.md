# JS MVP — Legacy LJS MIR retirement

**Date:** 2026-10-17
**Base revision:** `6c14534a7`
**Authority:** USER retirement request; **D8.1.3v25**, **D5.3/D5.4**,
**D6.2.2v2**, **D8.2.3**, **D8.4.1v2**, **D8.5.1v2**.
**Status:** source retirement and release/module builds complete; runtime and
performance qualification remain outstanding.

## Scope and completion criteria

Remove the retired JavaScript MIR compiler and exclusive runtime adapters.
Preserve explicit AST/AUTO execution, MVP promotion, interpreted dynamic source,
TypeScript admission and public host/module/cache APIs. Keep shared algorithms
and precise ownership. No third-party source or Test262 harness changes.

Completion requires removal of legacy lowerer/context references, a linked
release executable and rebuilt native Node modules after shared-header changes.
Runtime conformance and benchmark timing are separate qualification gates;
they are not inferred from source removal or the preceding migration's results.

## Implementation

1. Remove all 13 legacy compiler implementation files (`js_mir_*.cpp` and
   `transpile_js_mir.cpp`) and both private compiler headers. Move shared
   prototype-literal classification to `js_ast_children.cpp`.
2. Preserve existing services in `js_execution.cpp`, `js_module_loader.cpp`,
   `js_code_cache.cpp`, `js_compile_recovery.cpp` and
   `js_execution_internal.hpp`. Shared parser recovery owns builders/source,
   with no legacy MIR transpiler or deferred-context store.
3. Execute public MIR calls and executable cached images through MVP. Remove
   legacy preamble declaration/name snapshots and raw-entry execution branches.
   DOM handler Functions retain their document Runtime's native units.
   Advance the executable-image compiler ABI to 3 after the metadata change.
4. Add a TypeScript frontend option to existing MVP execution. Reuse the
   existing `ts_type_info` helper; no new semantic runtime helper is added.
5. Remove legacy function/closure factories, code interning, generator/async
   state-machine factories and the native legacy module carrier. Preserve
   shared `js_call_entry_compiled_light` / `js_call_entry_compiled_this`:
   interpreter AUTO promotion uses these entries for MVP bodies.
6. Remove compiler-only literal/array factories, module/declaration and eval
   environment bridge wrappers, typed-element and other legacy facades, their
   import records, and private dependencies made unused by the removal.

## Source and build evidence

- Before removal, archive the old compiler sources and executable under
  `temp/js_legacy_mir_retirement/before/`.
- Purge **78** orphaned compiler objects and the affected `liblambda-rt-cpp.a`;
  archive relinking must not retain deleted object members.
- `make build-release-compile` regenerates the build configuration; subsequent
  release builds use `make -C build/premake config=release_native -j8 lambda
  node-core node-fs node-net node-crypto`.
- Final release build of `lambda`, `node-core`, `node-fs`, `node-net` and
  `node-crypto`: **exit 0**, zero compiler errors. Log:
  `temp/js_legacy_mir_retirement/qualified_build.log`.
- Source/linked-symbol audit finds no retired compiler types, lowerer names,
  MIR function/closure factories or legacy generator/module carrier entries.
  `git diff --check` and the changed documentation's local-link scan pass.
- The complete changed C/C++ source manifest, including all new service files,
  measures **213,282 → 180,249 physical lines: −33,033**. The repository LOC
  checker measures **27,249 fewer code lines** after excluding blank/comment
  lines. Evidence: `temp/js_legacy_mir_retirement/loc_check.txt` and
  `source_manifest` in the same directory.
- Executable SHA-256:
  `a57e3c5ba79b44d5668a3339a237a01f225f281208e9018458fdae28c5110839`.
  The executable and four rebuilt Node modules are pinned in
  `temp/js_legacy_mir_retirement/qualified/`; `binaries.json` records all hashes.

The obsolete `LiveMirValuesSurviveWeakTableTeardown` unit fixture is removed
with the legacy weak code-intern table it exclusively tested. Existing fixture
includes follow the shared header, and the AST-backend ownership assertion now
checks that no formal MVP realm was created. No Test262 cases, exclusions,
deadlines or expected outputs are changed.

## Qualification boundary

This request authorizes removal; no runtime tests or benchmark measurements are
run in this pass. The prior migration's **40,261/40,261** formal MIR result and
**5,568/5,568** Lambda aggregate describe the pre-retirement implementation.
Fresh runtime/TypeScript/DOM and performance qualification remains outstanding.
Ordinary MVP generated lowering remains intact, and its AUTO promotion retains
the shared compiled-call fast entries; this is a source-level preservation
statement, not a measured no-regression claim.
