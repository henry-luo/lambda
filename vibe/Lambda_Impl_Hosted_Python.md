# Hosted Python — Detailed Implementation Plan

> **Status:** active implementation; product split landed, compiler boundary and
> release acceptance incomplete
>
> **Date:** 2026-07-20
>
> **Last verified against the live tree:** 2026-07-24
>
> **First hosted-language adopter:** Python (`lang-python`)
>
> **Design authority:** `vibe/Lambda_Design_Jube_Lang_Hosting.md`
>
> **Related plans:** `vibe/Lambda_Impl_Stack_Frame_Py.md`,
> `vibe/Lambda_Desing_Native_Module.md`, and
> `doc/Lambda_Jube_Runtime.md`

## Implementation progress snapshot — 2026-07-24

**Overall verdict: not complete.** The runnable product topology and the main
runtime membranes are implemented, but the proposal's final public compiler
boundary, negative/conformance coverage, platform matrix, and release evidence
are not. In particular, a passing architecture check currently means that
already-retired dependencies did not return; it does not mean that Python
depends only on public Jube headers and entry points.

### Stage status

| Stage | Live status | Main evidence or remaining gate |
|---|---|---|
| H0 | Partial | Deterministic coupling inventory and split Python GC target exist. ADR closure, dated evidence archive, and release baselines/performance evidence are missing. |
| H1 | Substantially implemented | Architecture checker with synthetic regression self-tests, strict script/golden inventory, configurable host, `requires_lang_python`, absent-module package smoke, and a fresh Lambda baseline exist. Cross-platform and release-performance evidence is still missing. |
| H2 | Partial | Size/version/capability/build-ID negotiation and partitioned service tables exist. The legacy JS-backed value table remains, and the requested ABI negative/conformance suite is not present. |
| H3 | Operational final path; checkpoint coverage incomplete | Generic alias, `run --lang`, and extension dispatch exist; `main.cpp` has no Python branch. The isolated dispatch matrix covers aliases, normalized aliases/extensions, help, `run --lang`, and deterministic duplicate-bundle selection; the full parity/registry lifecycle matrix is not archived. |
| H4 | Functional path implemented; acceptance incomplete | The 161 `py_*` runtime imports are module-owned and registered through `JubeRuntimeCatalogAPI`; core ownership is removed. Collision/signature/effect/caching conformance tests remain. |
| H5 | Functional boundary mostly implemented | Python has no `js_*` source or dynamic import and uses neutral data/root services. The complete per-operation ownership contract and the full cross-boundary stress matrix remain. |
| H6 | Functional core mostly implemented | Host activation/recovery/JIT lifecycle and common module-graph services are in use. The language session is still only a marker, the Python-owned session-state design is incomplete, and the full failure/repetition matrix is missing. |
| H7 | **Incomplete — primary architecture blocker** | H7D lifecycle finalization is implemented and H7C now has host-owned runtime-load, identity-allocation, and validated semantic move services. H7A/H7B/H7C remain open: Python still includes host AST/transpiler/data/MIR headers and constructs most raw MIR operations. |
| H8 | Partial | External `lang-python` target, manifest discovery, build-ID/integrity checks, rollback, POSIX/macOS loading, and the macOS negative-loader matrix exist. The module still imports internal host/library symbols, and Linux/Windows and static/dynamic parity remain incomplete. |
| H9 | Substantially implemented | One host target, compatibility symlink, standard/full package split, identical-host check, and absent/full smoke targets exist. Legacy `requires_jube_exe` metadata remains for Ruby/Bash, and current release performance evidence is missing. |
| H10 | Incomplete | Static Python registration is removed and runtime docs were started. Final allowlist reduction, ADR/design status, audit, platform matrix, sanitizer/stress archive, and performance closeout remain. |

The task checkboxes below intentionally remain acceptance checkboxes. A stage
is not marked complete merely because its happy-path implementation exists.

### Live verification notes

- The current corpus contains **42** `test_py_*.py` scripts and 42 matching
  goldens, not the 41 pairs used by the original plan. The GTest runner
  therefore contains 43 tests including the inventory check.
- `make check-hosted-python-architecture` initially failed because the checker
  still referenced pre-regrouping paths such as `lambda/build_ast.cpp`. The
  checker was repaired to follow `lambda/runtime/...` and now passes, including
  `--require-module-binary`.
- The regenerated coupling inventory reports 3,330 remaining entries:
  80 external includes, 107 host-layout references, 2,979 raw MIR references,
  161 runtime imports, and 3 generated-build dependencies. These are review
  records rather than 3,330 unique defects, but they make H7's incompleteness
  unambiguous.
- A clean current debug host and external macOS module build succeeds.
  `lambda-jube.exe` is a symlink to `lambda.exe`; the host has no linked Python
  library and no exported `py_*`, `transpile_py_*`, or `load_py_module`
  symbols.
- Current direct-host execution is green: `./lambda.exe --help` exits normally,
  the 43-test Python suite passes, and the forced-GC closure and generator
  gates match their goldens. Earlier timeout reports came from macOS `gtimeout`
  returning status 124 after emitting the host's full help output; it is not
  used for acceptance checks.
- The 2026-07-24 regression repair made resolved local Python calls use the
  shared borrowed-call classification, preserving caller-owned scalar return
  homes. `py_list_new` now builds its backing storage through `array_push`, so
  it is released with the runtime data zone instead of leaking a tracked
  Python-runtime allocation at GC teardown. The match golden and forced-GC
  checks cover these paths.
- `make test-jube-module-loader-negative` now runs isolated copied-bundle
  cases for a missing library, corrupt/incomplete manifests, wrong base/hosted
  ABI, wrong host build ID, missing checksum, checksum mismatch, and a missing
  entry symbol. `make test-jube-module-integrity` separately verifies that a
  tampered library is rejected before the module initializer can run. Missing
  capability/dependency and genuine initializer-failure coverage remain.
- `make build-test` now refreshes the manifest after its debug `all` build
  rewrites `lang-python`; otherwise the correctly enforced digest check would
  leave the development bundle unloadable after a test-only rebuild.
- `make test-hosted-python-architecture-checker` proves the architecture gate
  rejects synthetic reintroductions of a Python main branch, JavaScript call,
  raw frame-runtime memory load, direct register/label allocation, and stale
  manifest build ID; the live checker still passes afterwards.
- `make test-jube-language-dispatch` now copies an isolated host/module bundle
  and checks `py`, case-normalized `PY`, `run --lang python`, `.py`,
  case-normalized `.PY`, hosted help, and duplicate-bundle selection. Manifest
  discovery now normalizes aliases/extensions before descriptor registration
  and chooses the lexicographically first matching child manifest, so dispatch
  never depends on filesystem enumeration order.
- The `h7e13` host/module pair passes the full 43-test Python corpus, forced-GC
  closure/generator gates, isolated dispatch and loader-negative matrices, and
  `utils/check_hosted_python_architecture.py --require-module-binary`. It moves
  activation runtime loads plus repeated register/label identity allocation
  behind execution services; it does not close H7C's remaining raw builder
  surface.
- The `h7e14` host/module pair adds a single generic semantic-instruction
  descriptor rather than a per-opcode ABI: host-validated integer/float
  immediate moves and integer register moves are appended only through the
  opaque function service. Python integer/float literal lowering and the
  public scalar-return handoff now use that service. The full 43-test corpus,
  forced-GC closure/generator gates, isolated dispatch and loader-negative
  matrices, and the hosted-Python architecture checker pass against the
  release artifacts.
  This is an incremental H7C migration, not completion: control flow, calls,
  memory operations, and shared frame/root finalization still construct raw
  MIR through the shared emitter.
- The `h7e15` pair extends that same descriptor with destination-less jumps
  and true/false branches; it does not add a per-opcode service. Python's
  integer boxing range split, `not`, boolean short-circuiting, and conditional
  lowering now use the opaque control-flow forms. The host validates operands
  by descriptor opcode because branch forms intentionally have no destination
  register. Two consecutive full 43-test Python corpus runs, forced-GC
  closure/generator checks, dispatch, loader-negative, and architecture
  self-checker gates passed against the rebuilt release host/module pair.
  Calls, comparisons, memory operations, labels, and shared frame/root
  finalization still keep H7C incomplete.
- The `h7e16` pair moves placement of the already-opaque label identities into
  a tail-appended host service. Python no longer calls the shared label emitter;
  it can only ask the host to place a host-created label in its current opaque
  function. The architecture self-checker now rejects a raw label append.
  The release host was force-relinked after the ABI bump to ensure its embedded
  build ID matched the rebuilt module. The 43-test corpus, forced-GC
  closure/generator checks, isolated dispatch, loader-negative matrix, and
  architecture gates pass on that matching pair. Calls, comparisons, memory
  operations, and shared frame/root finalization remain H7C work.
- The current `make test-lambda-baseline` run passes all 2,104 input-baseline
  cases and all 1,492 Lambda-runner cases with zero failures. This is current
  shared-host evidence; Test262, platform, and performance acceptance remain
  separately outstanding.
- The module-boundary checker rejects the specifically retired host and
  JavaScript imports, but `nm -u` still shows non-Jube internal dependencies
  such as Lambda `Item` globals, heap/name helpers, arrays, arithmetic
  builtins, filesystem helpers, MIR, arenas, and name pools. Therefore H8.8
  and the H7 exit gate are not satisfied.
- The 2026-07-21 Lambda/Test262/package results below remain historical
  checkpoint evidence. They were not rerun as current release acceptance in
  this status audit.

The runnable product path is now a unified host plus an external Python module:

- `lambda.exe` discovers and loads `modules/lang-python/lang-python.dylib`;
  there is no separately compiled `lambda-jube.exe` runtime. The compatibility
  name is only a launcher/symlink to the same host artifact.
- Python's runtime imports, neutral data operations, opaque roots, guest
  activation/recovery, module-graph operations, and MIR lifecycle are
  negotiated Jube services. Python no longer imports the retired raw host
  lifecycle, root-frame, `_lambda_rt`, function-construction, forward-declare,
  or register-name lookup symbols.
- The standard and full-Jube packages contain bit-identical hosts. The
  2026-07-21 `make verify-jube-package` run reported SHA-256
  `b55452fc076f94fafe6460fb1997137a4e16c5f9a26810f79057f7d0564b731e`
  for both `release-standard/lambda` and `release-jube/lambda` and completed
  the standard-absent/full-Python smoke checks.
- The 2026-07-21 focused evidence was green: the then-42-test Python suite,
  closure and generator forced-GC gates, and
  `utils/check_hosted_python_architecture.py --require-module-binary`. The
  rebuilt `make test-jube-module-integrity` tamper gate also completed
  successfully.
  The shared-compiler regression gates also passed: `make test-lambda-baseline`
  reported 3,495/3,495 passed, and `make test262-baseline` reported
  40,261/40,261 fully passed with 0 failed and retry time `0.0s`.
- The only JavaScript runtime-source change in this migration worktree is the
  file-URL async-XHR ordering bug fix. It is covered by the above Test262 and
  Lambda baseline evidence; no JS design or global execution-path change is
  part of Python hosting.

### Still outstanding before closure

1. **Finish H7C as one cohesive opaque compiler-builder service.** Python
   lowering still includes the shared MIR emitter and constructs raw MIR
   instructions/operands. The completed lifecycle and selected-constructor
   services are interim extraction steps, not the final boundary. The host
   must own opaque compiler/module/function/block/value/import handles and
   frame/root/scalar-home finalization while Python retains only semantic
   lowering choices.
2. **Complete the H7A/H7B header-boundary reduction.** Python still includes
   internal AST/transpiler/MIR/data-layout headers. Replace those dependencies
   with reviewed public Jube source, AST/profile, analysis, and compiler
   projections; do not expose C++ record layouts as an ABI shortcut.
3. **Close the remaining negative and parity coverage.** Add stale/wrong-owner
   handle, use-after-finalize, invalid construction order, repeated-session,
   static/dynamic parity, loader failure/rollback, and module-retention tests.
4. **Complete release acceptance.** Add supported-platform Windows loader
   validation, sanitizer/stress evidence, and release-performance measurements.
5. **Close H10 documentation and audit work.** Reduce the permanent internal
   header allowlist, update the user/runtime/build/package documentation and
   ADR status, archive the final matrix, and review every shared/JS diff
   against the no-Lambda/JS-hot-path-impact rule.

## 1. Purpose

This plan migrates Python from a language compiled directly into the
`lambda-jube.exe` variant into the first hosted-language Jube module loaded by
the unified `lambda.exe`.

The migration is intentionally incremental. Every stage leaves a runnable,
testable product and removes a specific category of Python coupling. The final
state is:

```text
lambda.exe
  ├── Lambda and JavaScript runtimes
  ├── language-neutral Jube loader and registry
  ├── unified runtime/compiler host services
  └── lazily loads:
      modules/lang-python/
        ├── module.json
        ├── lang-python.<dylib|so|dll>
        └── Python grammar/standard-library resources
```

Python remains a Python semantic implementation. It reuses Lambda's source,
AST, analysis, allocation, GC, runtime catalog, MIR emission, JIT, recovery,
module, and diagnostic machinery through reviewed host contracts. It does not
reuse JavaScript semantics by calling `js_*` implementation functions, and it
does not add Python cases or storage to Lambda or JavaScript hot paths.

## 2. Non-negotiable implementation constraints

The following are release gates, not preferences:

1. Existing Lambda and JavaScript semantics do not change.
2. Existing Lambda and JavaScript execution hot paths gain no language lookup,
   capability check, or module trampoline.
3. With Python absent, there is no Python initialization, heap allocation, JIT
   registration, grammar loading, or module-library loading.
4. `make test-lambda-baseline` finishes with zero failed tests after every
   shared-runtime change.
5. `make test262-baseline` finishes with zero failed tests and zero retries
   after every shared-runtime or JavaScript-adjacent change.
6. Performance comparisons use release builds. Any statistically significant
   Lambda or JavaScript regression requires a separate reviewed design change;
   it is not accepted as migration overhead.
7. Python owns Python semantics, runtime helpers, AST extensions, scope rules,
   objects/classes/descriptors, exceptions, generators, coroutines, imports,
   builtins, and standard-library behavior.
8. A facility is promoted into the unified runtime only when its contract is
   language-neutral. Otherwise it stays in Python or is exposed through a
   reviewed, versioned extension API.
9. The rich hosted-compiler API is C ABI, size-gated, capability-negotiated,
   opaque, and initially coupled to the exact host build ID.
10. MIR is an in-process implementation detail, not a module distribution
    format.
11. Static registration is permitted only as a migration checkpoint. The
    completed `lang-python` is an external native module.
12. Standard and full Jube distributions contain a bit-for-bit identical
    `lambda.exe`; only their module/resource bundles differ.
13. Native modules are trusted and stay loaded for the process lifetime. Hot
    unload and untrusted-module sandboxing are out of scope.
14. Every common-runtime enhancement is reviewed and landed independently
    before Python consumes it.

## 3. Scope and non-goals

### 3.1 In scope

- clean up the current experimental Jube ABI before Python adopts it;
- add hosted-language descriptors and a language registry;
- make CLI and source-extension dispatch generic;
- add neutral data, runtime-catalog, guest-execution, module-graph, source,
  diagnostic, AST, type-analysis, and MIR-emission host services;
- move Python runtime imports out of the core system-function registry;
- remove direct Python dependencies on JS object/function helpers;
- remove Python-specific branches and declarations from shared core files;
- build and load Python as `lang-python`;
- combine the `lambda.exe` and `lambda-jube.exe` products;
- package standard and full distributions from the same host artifact;
- preserve Python behavior, rooting, recovery, and ownership work already
  implemented or planned in `Lambda_Impl_Stack_Frame_Py.md`;
- establish conformance tests reusable by later Ruby and Bash migrations.

### 3.2 Out of scope

- adding new Python language features unrelated to hosting;
- changing Python observable semantics to match Lambda or JavaScript;
- migrating Ruby or Bash in this project;
- freezing internal AST, type, MIR, or JIT structure layouts as a stable ABI;
- serializing or caching MIR across host builds;
- arbitrary third-party ABI compatibility with the current experimental Jube
  tables;
- hot unloading modules;
- sandboxing native modules;
- independently replacing the parser, JIT, GC, or module system.

## 4. Verified starting point

This inventory was checked against the working tree on 2026-07-20. Line
numbers are deliberately omitted because the implementation will move them.

### 4.1 Product and test topology

| Area | Current state | Required state |
|---|---|---|
| Python executable | `lambda-jube.exe py file.py` | `lambda.exe py file.py` |
| Jube build | monolithic target containing Python, Ruby, and Bash sources | one host plus separately built modules |
| Python test harness | invokes `lambda-jube.exe` | invokes the same `lambda.exe` used by core tests |
| Python corpus | 41 `test_py_*.py` scripts and 41 matching goldens | all retained and run through static and dynamic checkpoints |
| GC rooting lane | Python closures/generators run through `lambda-jube.exe` | module-specific Python GC lane using `lambda.exe` |
| Distribution | executable variants differ by compilation | identical host; bundles differ only by modules/resources |

### 4.2 Direct Python coupling in the host

The current host contains Python-specific compile-time knowledge:

- `lambda/main.cpp` includes Python headers and directly calls
  `transpile_py_to_mir`;
- `lambda/transpiler.hpp` declares Python entry points such as
  `load_py_module`;
- `lambda/build_ast.cpp` contains `LAMBDA_PYTHON` branches and directly loads
  Python modules;
- `lambda/sys_func_registry.c` includes Python headers and publishes Python
  runtime helpers;
- `build_lambda_config.json` adds `lambda/py`, its parser, and
  `LAMBDA_PYTHON` to the monolithic Jube executable;
- `Makefile` and `test/test_py_gtest.cpp` assume `lambda-jube.exe`.

These are migration targets. No additional Python-aware host branch may be
added while removing them.

### 4.3 Direct host and JavaScript coupling in Python

Python currently reaches into internal implementation surfaces, including:

- Lambda runtime, transpiler, data, GC, MIR, and module-registry headers;
- internal globals such as the active runtime/context and import resolver;
- raw `Runtime`, `EvalContext`, `Input`, and MIR context construction;
- direct module-registry loading and registration;
- JavaScript object, property, array, function, and runtime-input helpers.

The implementation must classify each use as one of:

1. already language-neutral and exposed through a reviewed host projection;
2. a common mechanic to promote behind a neutral contract;
3. Python semantic behavior to replace with a Python-owned adapter;
4. obsolete coupling to delete.

Renaming or wrapping a `js_*` function without removing its JavaScript semantic
contract does not count as promotion.

### 4.4 Existing infrastructure to reuse

The project already has mechanisms that this work must extend rather than
duplicate:

- `JubeModuleDef`, size-gated Jube tables, module lifecycle callbacks, and the
  Jube registry;
- dynamic module loading on POSIX;
- root-frame and GC APIs;
- `Item`, strings, arrays, maps, elements, ranges, VMaps, and Mark data;
- shared AST and `LangProfile` infrastructure;
- `MirEmitter` and shared frame/root/scalar-home support;
- `JitImportMetadata`, normalized call effects, and dynamic import resolution;
- module loading-state tracking and cycle detection;
- runtime recovery checkpoints and JIT lifecycle;
- build generation from `build_lambda_config.json`.

In particular, Python runtime imports must reuse the existing
`JitImportMetadata` vocabulary. A second, inconsistent call-effect system is
not acceptable.

## 5. Required ADRs and landing order

The design document identifies ten ADRs. Implementation may prototype an ADR
behind tests, but no public table or cross-boundary contract lands before its
ADR is reviewed. The ADRs are dependency ordered here:

| ADR | Decision | Must precede |
|---|---|---|
| ADR-HL-01 | `JubeLanguageDef`, callbacks, request/result records, and session lifecycle | generic language registration |
| ADR-HL-02 | `JubeHostLangAPI` discovery, versions, capabilities, build-ID policy, and errors | any compiler service table |
| ADR-HL-03 | language-neutral `JubeHostDataAPI`, ownership, rooting, and callable/namespace membranes | removal of Python `js_*` dependencies |
| ADR-HL-04 | runtime-function descriptors, call-effect metadata, registration, collision policy, and JIT resolution | moving `py_*` imports out of core |
| ADR-HL-05 | source, diagnostic, AST, binding, and type-analysis opaque APIs | compiler extraction |
| ADR-HL-06 | MIR emission, compilation, linking, execution, recovery, and lifetime contracts | dynamic Python compilation |
| ADR-HL-07 | common module graph, loading states, cycles, namespace ownership, and cross-language imports | removal of `load_py_module` branches |
| ADR-HL-08 | manifest schema, discovery paths, integrity fields, dependencies, and build compatibility | external module packaging |
| ADR-HL-09 | Windows loader parity and DLL dependency/resource resolution | Windows distribution acceptance |
| ADR-HL-10 | one-executable build graph, compatibility name, standard/full packaging, and retirement policy | final product convergence |

Every ADR must explicitly answer:

- What existing Lambda implementation is reused?
- Why is the contract language-neutral?
- Which Python behavior remains in the module?
- Which current JavaScript behavior is unchanged?
- What is the allocation, GC, ownership, error, and thread contract?
- Is the operation compile-time, link-time, import-time, or run-time?
- Does it add any branch, indirect call, data field, or initialization to an
  existing Lambda/JS hot path?
- How is version/size/capability mismatch reported?
- What conformance, baseline, and release-performance evidence is required?
- How is the change disabled or rolled back independently?

## 6. Target ownership and source layout

Exact filenames may change during review, but ownership must remain clear.
Prefer a few cohesive files over one table per operation.

```text
lambda/
  jube/
    jube.h                         stable base C ABI
    jube_registry.cpp             module discovery/registration
    jube_language.h/.cpp          language descriptors and registry
    jube_host_data.h/.cpp         neutral value/data projection
    jube_host_lang.h/.cpp         hosted API aggregation/capabilities
    jube_runtime_catalog.h/.cpp   runtime descriptors/import binding
    jube_guest_execution.h/.cpp   activation, recovery, JIT orchestration
    jube_module_graph.h/.cpp      language-neutral loading states/edges
    jube_compiler_api.h/.cpp      source/diagnostic/AST/type/MIR projections

  py/
    python_jube_module.cpp        JubeModuleDef + JubeLanguageDef
    python_session.cpp            module-owned per-session state
    python_host_adapter.cpp       calls reviewed host APIs
    python_runtime_imports.cpp    module-owned py_* descriptors
    build_py_ast.cpp              Python parser/builder policy
    transpile_py_mir.cpp          Python lowering decisions
    py_*.cpp                      Python runtime semantics

modules/
  lang-python/
    module.json
    resources/

test/
  jube/                           base/hosted API conformance
  py/                             Python behavior/goldens
  benchmark/hosted_python/        release comparison definitions/results
```

The existing `lambda/py/` source directory does not itself violate isolation.
The decisive boundary is the generated build target and allowed dependencies:
the host must compile and run without those sources, and the module must compile
against approved Jube headers rather than internal host layouts.

## 7. Workstream and dependency map

```text
H0 baseline/ADRs/inventories
 ├── H1 firewall + generic test harness
 └── H2 neutral Jube base/capability cleanup
      └── H3 language descriptor + registry + static checkpoint
           ├── H4 module-owned runtime catalog
           ├── H5 neutral data/callable membrane
           └── H6 guest lifecycle + module graph
                └── H7 compiler-service extraction
                     └── H8 external lang-python module
                          └── H9 one executable + distributions
                               └── H10 cleanup and closure
```

H4 and H5 can overlap after H3, but each has its own gate. H6 must define
lifetimes before H7 exports compiler handles. H8 cannot start until the Python
target has no forbidden internal/JS dependencies. H9 cannot remove the
monolithic compatibility path until dynamic parity is demonstrated.

## 8. Stage H0 — Freeze evidence and approve contracts

### Goal

Create a reproducible baseline and turn all hidden coupling into reviewed work
items before changing shared code.

### Tasks

- [ ] **H0.1** Land ADR-HL-01 through ADR-HL-04 before their corresponding
  APIs; prepare ADR-HL-05 through ADR-HL-10 before those phases begin.
- [ ] **H0.2** Record the exact build revision, compiler, platform, build mode,
  and commands used for baseline evidence.
- [ ] **H0.3** Capture fresh results for:
  - `make test-lambda-baseline`;
  - `make test262-baseline`, including failed and retry counts;
  - the complete Python golden suite;
  - Jube registry/loader tests;
  - Python closure/generator GC-rooting tests.
- [ ] **H0.4** Create a machine-readable Python coupling inventory containing:
  - includes outside `lambda/py`;
  - internal host types/globals referenced by Python;
  - `js_*` calls from Python;
  - `py_*` references outside Python;
  - Python preprocessor symbols outside the Python target;
  - runtime imports and their current JIT call-effect metadata;
  - direct module-registry calls;
  - parser/grammar and generated-build dependencies.
- [ ] **H0.5** Classify every inventory entry using the four outcomes in
  section 4.3 and assign it to H2–H7.
- [ ] **H0.6** Capture release startup and representative Lambda/JS runtime
  performance using the protocol in section 20.
- [ ] **H0.7** Save executable size, mapped-library list, startup allocations
  if measurable, and Jube registry initialization count with Python absent.
- [ ] **H0.8** Separate the Python-specific GC-rooting target from the core
  `test-gc-rooting` prerequisite so the standard host does not require a
  monolithic Jube executable. Retain an aggregate target that runs both.
- [ ] **H0.9** Establish a dated evidence directory under
  `test/benchmark/hosted_python/`; do not write project evidence to `/tmp`.

### Deliverables

- reviewed first-wave ADRs;
- coupling inventory with no unclassified entries;
- baseline test and performance evidence;
- explicit list of core enhancements Python will request;
- split core and hosted-Python GC test entry points.

**Implementation checkpoint (2026-07-20, h0-inventory).**
`make hosted-python-coupling-inventory` now emits a deterministic JSON inventory
from the active source tree. Every record carries a migration outcome and
owning stage. It includes Python external headers, host-layout/global accesses,
raw MIR names, JavaScript names, direct module-registry calls, Python-owned
core symbols, preprocessor tokens, runtime imports, and generated-build
dependencies. This is deliberately a reporting tool rather than an allowlist:
the current raw AST/runtime/MIR entries remain visible as H6/H7 work and do
not make the architecture gate pass by exception.

### Exit gate

- all current tests used as baselines are green;
- Test262 reports zero failed and zero retries;
- every direct Python/core/JS coupling has an owner and destination;
- performance samples have enough repetitions to distinguish noise;
- this stage has no observable runtime behavior change.

## 9. Stage H1 — Architecture firewall and test-harness preparation

### Goal

Make architectural regressions mechanically visible before moving code.

### Tasks

- [ ] **H1.1** Add an architecture check that reports:
  - Python headers or `py_*` symbols in shared core files;
  - `LAMBDA_PYTHON` outside transitional build/adapter allowlists;
  - `js_*` symbols or JS runtime headers used by `lambda/py`;
  - forbidden internal headers included by the eventual module target;
  - raw host globals accessed by the module;
  - shared structs acquiring Python-only fields or enum cases.
- [ ] **H1.2** Start with a checked-in, exact transitional allowlist generated
  from H0. Each phase must shrink it. New entries fail review.
- [ ] **H1.3** Make Python test discovery require a matching `.txt` golden for
  every `test_py_*.py`; missing or orphaned files fail rather than silently
  skip.
- [ ] **H1.4** Parameterize the Python harness by host executable and module
  directory so the same corpus can run against:
  - the current monolithic checkpoint;
  - static hosted-language registration;
  - dynamic `lang-python`.
- [ ] **H1.5** Add test labels/metadata for required module capability
  `lang-python` instead of the product-specific `requires_jube_exe`.
- [ ] **H1.6** Add a “Python absent” host test:
  - normal Lambda/JS commands succeed;
  - `.py`/`py` produces a generic missing-language diagnostic;
  - the Python library is not mapped or initialized;
  - help/discovery behavior matches ADR-HL-01/08.

### File impact

Primarily test/build scripts and a new architecture checker. Product behavior
must not change except that malformed test corpus packaging now fails loudly.

### Exit gate

- the current transitional violations are fully enumerated;
- the checker rejects a deliberately injected new core-to-Python and
  Python-to-JS dependency;
- all 41 Python scripts and goldens execute through the parameterized harness;
- Lambda and Test262 gates remain green.

## 10. Stage H2 — Clean and partition the Jube host contract

### Goal

Create a neutral base on which Python can depend without inheriting JavaScript
or Radiant semantics.

### Tasks

- [ ] **H2.1** Implement ADR-HL-02 capability discovery:
  - `api_version` and `struct_size` on evolving tables;
  - host build ID;
  - required/optional capability bitsets;
  - deterministic mismatch status and diagnostic;
  - no module callback before validation succeeds.
- [ ] **H2.2** Keep the base ABI a pure C projection. Remove public dependence
  on internal C++ layouts and minimize includes in `jube.h`.
- [ ] **H2.3** Partition current host services into:
  - stable base module/lifecycle/GC/error services;
  - neutral data services;
  - hosted-language compiler services;
  - explicitly JS scripting services;
  - explicitly DOM/Radiant services.
- [ ] **H2.4** Audit `JubeHostValueAPI`. Current operations backed by
  `js_new_object`, `js_array_*`, or `js_property_*` are not accepted as a
  neutral API merely because their field names omit “js”.
- [ ] **H2.5** Migrate existing in-tree Jube users to the corrected tables.
  Preserve their behavior with focused tests.
- [ ] **H2.6** Decide and document whether this is an experimental ABI version
  bump or an additive transition. Do not leave two ambiguous meanings for the
  same field/version.
- [ ] **H2.7** Add ABI conformance tests for:
  - undersized table;
  - newer additive tail;
  - unsupported version;
  - missing required capability;
  - wrong build ID for hosted compiler access;
  - ordinary module that requests no hosted API.
- [ ] **H2.8** Verify the registry initialization path is cold for ordinary
  Lambda/JS execution and does not initialize optional modules.

### Reuse rule

This stage reorganizes access to existing facilities. It must not replace the
underlying GC, Item model, JS runtime, or DOM implementation.

### Exit gate

- base, hosted-language, JS, and DOM ownership is unambiguous;
- existing native-module tests pass;
- Python has not yet consumed any JS-backed “neutral” operation;
- no new branch or indirect call appears in Lambda/JS hot paths;
- core baselines and release performance gates pass.

## 11. Stage H3 — Hosted-language descriptor, registry, and static checkpoint

### Goal

Route Python through the generic hosted-language path while it is still
statically linked. This isolates dispatch changes from dynamic-library issues.

### Tasks

- [ ] **H3.1** Implement ADR-HL-01 with an additive, size-gated
  `JubeLanguageDef` capability on `JubeModuleDef`.
- [ ] **H3.2** Define opaque session/config/request/result handles for script,
  module, eval, and REPL modes. The coarse compile/execute/load callbacks are
  sufficient initially.
- [ ] **H3.3** Implement a language registry indexed during module
  registration by canonical name, aliases, and normalized extensions.
- [ ] **H3.4** Resolve duplicate language names/extensions deterministically
  according to ADR-HL-01/08. Never depend on link or filesystem enumeration
  order.
- [ ] **H3.5** Make CLI dispatch generic:
  - explicit language alias (`lambda py app.py`);
  - explicit option (`lambda run --lang python app.py`);
  - source extension (`lambda app.py`);
  - generic absent/incompatible-language errors.
- [ ] **H3.6** Publish a static `lang-python` `JubeModuleDef` and
  `JubeLanguageDef` that adapt callbacks to the existing Python entry points.
- [ ] **H3.7** Remove the Python command block and Python include from
  `lambda/main.cpp`; main asks the registry to execute a language.
- [ ] **H3.8** Keep callback invocation off existing Lambda/JS paths:
  registration builds cached descriptor indexes; selected execution uses a
  cached descriptor/function pointer.
- [ ] **H3.9** Add registry tests for aliases, case/path normalization,
  extension conflicts, absent module, session create/destroy, callback errors,
  and repeated runs.
- [ ] **H3.10** Run every Python golden through the static descriptor and
  compare byte-for-byte with the starting output.

### Required invariants

- The Python adapter may temporarily call old Python internals, but only inside
  `lambda/py`; it may not create a new host-facing Python API.
- Static and future dynamic registration use the same descriptor and callback
  implementation.
- `lambda app.ls` and `lambda.exe js ...` do not query the language registry
  inside their evaluator/JIT hot paths.

### Exit gate

- `lambda.exe py file.py` reaches Python through `JubeLanguageDef`;
- `main.cpp` contains no Python include, macro branch, or direct call;
- all 41 Python goldens pass through the descriptor;
- absence and incompatibility cases are tested;
- Lambda/Test262/performance gates pass.

## 12. Stage H4 — Module-owned Python runtime catalog

### Goal

Remove Python runtime helpers from the core system-function registry while
retaining direct, metadata-correct MIR calls.

### Tasks

- [ ] **H4.1** Implement ADR-HL-04 by projecting the existing
  `JitImportMetadata` and normalized call metadata through a C-compatible
  runtime-function descriptor.
- [ ] **H4.2** Specify descriptor fields for qualified name, native symbol,
  signature, GC/re-entry behavior, result representation, argument effects,
  exception behavior, and optional language-visible name.
- [ ] **H4.3** Move the `py_*` descriptor table into
  `python_runtime_imports.cpp`.
- [ ] **H4.4** Register the module's runtime descriptors at module
  registration/activation. Validate the full table before publishing any
  entry.
- [ ] **H4.5** Use a module-qualified namespace and deterministic collision
  policy. Core names cannot be shadowed accidentally.
- [ ] **H4.6** Bind imports during compilation/JIT linking and cache the
  resulting direct address/metadata. Generated Python calls must not traverse
  a module registry trampoline per invocation.
- [ ] **H4.7** Treat missing or unaudited GC metadata conservatively or reject
  the descriptor according to ADR-HL-04; never assume “does not allocate”.
- [ ] **H4.8** Remove Python includes and helper entries from
  `lambda/sys_func_registry.c`.
- [ ] **H4.9** Add catalog tests for collision, duplicate descriptor, missing
  symbol, bad signature, invalid effects, unload prohibition, repeated module
  activation, and direct-address caching.
- [ ] **H4.10** Add generated-code checks or instrumentation proving a hot
  Python helper call resolves once and contains no registry lookup loop.

### Reuse rule

The host's existing import map, resolver, metadata normalization, MIR call
emission, rooting, and scalar-home behavior remain authoritative. The Jube
catalog is a registration projection, not a parallel resolver.

### Exit gate

- no `py_*` helper is compiled into or registered by the host core;
- Python runtime call effects are complete and tested;
- all Python, GC-rooting, Lambda, and Test262 tests pass;
- release Lambda/JS performance remains within the no-regression policy.

## 13. Stage H5 — Neutral data, callable, and namespace membrane

### Goal

Remove all Python dependence on JavaScript object/function APIs while reusing
the underlying Lambda Item, container, allocation, and GC mechanics.

### Tasks

- [ ] **H5.1** Implement ADR-HL-03 as a narrow C projection of existing
  language-neutral data operations:
  - Item tag/type inspection;
  - scalar construction/access;
  - strings and symbols;
  - arrays, maps, elements, ranges, and VMaps where their contract is neutral;
  - allocation and rooting;
  - native-resource wrappers;
  - error/status transport.

  Initial implementation note: `JubeHostDataAPI` now carries only opaque
  active-session name construction, map writes, float construction, and JSON
  formatting.  Python no longer dereferences the activation `Input` or its
  pool/name-pool; the host validates the token against the active guest on the
  calling thread.  The remaining Item/container operations are intentionally
  not represented as ad-hoc aliases and remain H5 work.

  **Implementation update (2026-07-20, h7e2).** `JubeHostRootAPI` is a
  separately versioned hosted-language tail with opaque, stack-resident root
  frames.  It reuses Lambda's exact side-root reservation and teardown rather
  than introducing a parallel collector or allocation path.  Python's MIR
  import lowering and native runtime helpers (closures, calls, generators,
  iterators, class map writes, and list materialization) now use it; the
  dynamic module no longer imports `lambda_root_frame_*` or exposes the host
  frame layout.  The architecture verifier rejects both source-level legacy
  frames and those retired binary imports.  This table is not consulted by
  Lambda or JavaScript execution, so their generated and hot paths are
  unchanged.

  **Implementation update (2026-07-20, h7e3).** The neutral data table now
  owns traced closure-environment allocation, bounded reads/writes, and
  Python's persistent argument-stack write barrier.  The host preserves the
  existing allocation type, bounds checks, and GC barriers; Python preserves
  all closure and call semantics.  This removes direct Python dependencies on
  the active context, GC header inspection, `heap_calloc_closure_env`, and
  `owned_item_slot_*`.  The table is negotiated by size and the exact host
  build ID was advanced; source and dynamic-import checks reject those retired
  internal dependencies.  Neither Lambda nor JavaScript invokes this module
  service on its execution paths.

  **Implementation update (2026-07-20, h7e4).** Persistent Python TLS roots
  and exception/coroutine value rehoming now use session-validated Jube root
  and data services. This keeps heap selection, root registration, and
  number-stack relocation in the host while leaving Lambda and JavaScript
  execution paths unchanged.

  **Implementation update (2026-07-21, h7e5).** Python now requests map and
  callable allocation through a size-gated neutral data-service tail. The host
  retains the existing Lambda object layout, allocation class, GC ownership,
  and callable arity invariant, while Python receives only the resulting
  `Item`. This removes Python's remaining direct `heap_calloc*` use for these
  language values. The service is unused by Lambda and JavaScript paths, so it
  introduces no global runtime performance impact.
- [ ] **H5.2** Document for every operation:
  - borrowed versus owned values;
  - required root lifetime;
  - allocation/GC/re-entry behavior;
  - mutation and identity behavior;
  - thread and active-runtime requirements;
  - error result and pending-exception interaction.
- [ ] **H5.3** Separate mechanical containers from language semantics.
  JavaScript property lookup, prototypes, descriptors, coercion, `undefined`,
  proxies, arrays, and function objects remain JS services.
- [ ] **H5.4** Implement Python-owned adapters for:
  - Python object/class attribute lookup;
  - descriptors and bound methods;
  - Python list/dict semantics over approved storage;
  - Python callable identity, arity, invocation, and exception behavior;
  - Python module namespace export/import.
- [ ] **H5.5** Replace direct Python uses of `js_property_*`, `js_array_*`,
  `js_new_object`, `js_new_function`, JS reflection, and JS runtime-input
  helpers.
- [ ] **H5.6** If an existing JS-backed mechanism is genuinely neutral, land
  its promotion as a separate core change with:
  - a semantic-neutrality statement;
  - existing Lambda/JS tests;
  - new neutral API tests;
  - allocation/GC/performance evidence;
  - no JS behavior change.
- [ ] **H5.7** Add cross-boundary root/ownership stress tests for strings,
  containers, Python functions, closures, classes, module namespaces,
  exceptions, generators, and coroutines.
- [ ] **H5.8** Add symbol/import inspection to the module build: zero
  unresolved or linked `js_*` dependencies are allowed.

### Semantic acceptance tests

At minimum, exercise Python equality/truthiness, list and dict behavior,
attribute lookup, descriptor precedence, method binding, class inheritance,
exceptions, closures, imports, generators, async, and supported stdlib calls.
Tests must prove Python behavior, not merely successful Item transport.

### Exit gate

- architecture lint finds no JS header or `js_*` dependency in Python;
- Python semantic tests and forced-GC tests pass;
- JS behavior is unchanged;
- promoted neutral services have independent reviews and tests;
- no shared record gains Python-only storage.

## 14. Stage H6 — Guest execution lifecycle and common module graph

### Goal

Move host-owned activation, recovery, JIT orchestration, and module-graph
mechanics behind explicit services. Remove direct Python access to runtime
globals and JS-backed module namespaces.

**Implementation checkpoint (2026-07-20).** `JubeModuleGraphAPI` now exposes
size-gated circular-import namespace lookup and Lambda-module loading. Python
import lowering calls that service for Python package cycles and `.ls` imports,
while `.py` and `.js` selection remains in the generic language registry. The
existing `module_registry` remains the sole owner of paths, states, and
namespace records. The isolated `load_py_module` implementation now queries,
begins, and publishes through that same graph service, retaining only the
Python-owned export membrane; it no longer includes or calls the private module
registry. Imported Python source also uses `JubeSourceAPI` ownership rather
than direct file I/O. Its temporary `Runtime` bridge and the remaining compiler
internals still prevent this from being the H6 exit gate.

The host also owns creation and destruction of each guest execution activation
through `JubeGuestExecutionAPI`; the Python adapter no longer creates or cleans
up `Runtime` directly or casts a host execution context. Python's current
compiler body temporarily unwraps that opaque token internally, pending the H7
compiler service, so the token is not yet the final compiler ABI.

`JubeGuestExecutionAPI` owns MIR context creation/destruction, module
creation/finalization/loading, linking through the host import resolver, and
generated-function lookup. Python passes opaque service handles only for those
life-cycle operations and no longer calls `jit_init`, `MIR_finish`,
`MIR_load_module`, `MIR_link`, `find_func`, or `import_resolver` directly.
The service now also enters the host-owned activation, constructs the opaque
compiler input, executes generated `py_main` under the host recovery boundary,
and restores the prior thread-local state at completion. Python still owns
lowering decisions and currently uses the shared MIR emitter internally, so
this checkpoint does not claim the final opaque builder service is complete.
Normal import lowering roots its transient namespace through the existing
Jube GC root-frame service; it no longer reads the active context directly.

Nested Python JIT frames obtain the active runtime through the reviewed
`JubeGuestExecutionAPI` frame-runtime-slot service. It supplies an opaque
activation-bound address during compilation so the existing shared frame
emitter retains its proven load-before-prologue ordering without importing the
`_lambda_rt` storage symbol. The address cannot be obtained outside the active
guest execution, is never named by Python, and changes no Lambda or JavaScript
generated path; the module-boundary check rejects the retired storage symbol.

Python-owned runtime helper descriptors now register through the generic,
capability-negotiated `JubeRuntimeCatalogAPI`. The host validates names and
collisions, applies the existing conservative `JitImportMetadata` defaults,
and copies accepted entries into its normal resolver map. This happens once at
trusted-module activation and leaves Lambda and JavaScript generated calls as
their existing cached direct targets; Python no longer includes or calls the
core JIT registry directly.

Python's parser/scope state and import-lowering records now retain the opaque
execution token rather than a `Runtime*`; all import requests pass that token
to the generic graph/language registry. The only remaining `Runtime` unwrapping
is isolated at the two legacy compiler entry points while activation/recovery
services are extracted.

Normal Python script execution now also returns activation ownership to the
host via `execution_finish_guest`; restoration of the previous context and
standalone result-heap transfer no longer call the private guest-cleanup helper
from the module.

Cross-language loading now receives a host-created opaque import-execution
wrapper rather than the caller's runtime handle. The host activates that
wrapper, owns nested-context restoration, and retains a standalone activation
only until Python's registered heap-cleanup hook releases it. The generic
language dispatcher destroys all non-retained wrappers, including error paths;
the JavaScript fallback unwraps the token only inside the host. Consequently
Python import lowering has no `Runtime`, `EvalContext`, `Input`, recovery, or
raw module-registry setup path.

Nested import destruction now invokes that same finish path rather than a
separate partial cleanup, so the active Jube execution cannot outlive its
wrapper. Python also scopes its opaque data-session token across nested import
activation and restores the caller token without resetting its argument or
exception state. The package corpus and AddressSanitizer now exercise this
invariant; this was a lifecycle bug fix, not a Lambda/JS runtime redesign.

The exact hosted-compiler build ID was advanced with this additive service
shape, so stale dynamic modules fail deterministic compatibility negotiation
before registration rather than relying only on a matching calendar label.

### 14.1 Guest execution

- [ ] **H6.1** Implement the guest-execution part of ADR-HL-06.
- [ ] **H6.2** Define opaque process, language-session, compilation,
  activation, and execution handles with an explicit lifetime hierarchy.
- [ ] **H6.3** Make the host create/enter/leave:
  - runtime and heap activation;
  - `Input`/source lifetime;
  - active evaluation context;
  - root/number stack watermarks;
  - recovery checkpoints;
  - MIR/JIT context and import binding.
- [ ] **H6.4** Restore prior thread-local state on success, language error,
  compile error, recovery jump, and nested cross-language execution.
- [ ] **H6.5** Give Python a module-owned session object for Python globals,
  exception state, import state, caches, builtins, and semantic configuration.
- [ ] **H6.6** Eliminate Python reads/writes of `_lambda_rt`, raw active
  contexts, import resolver globals, and manually assembled host execution
  records.
- [ ] **H6.7** Align this lifecycle with Python stack-frame/rooting work:
  checkpoint restoration cannot bypass Python frame cleanup, root publication,
  scalar adoption, or coroutine ownership.
- [ ] **H6.8** Test repeated sessions, nested calls, failure recovery, forced
  GC, stack overflow, compile failure, runtime exception, and reset/cleanup.

### 14.2 Module graph and imports

- [ ] **H6.9** Implement ADR-HL-07 by splitting neutral module identity and
  loading-state mechanics from language-specific namespace representation.
- [ ] **H6.10** Reuse the existing canonicalization, cache, dependency edges,
  loading/loaded/failed states, and cycle detection where their contracts are
  neutral.
- [ ] **H6.11** Define a namespace membrane that can carry exports without
  assuming a JS object. Each source language owns name lookup and binding
  semantics on its side.
- [ ] **H6.12** Define import requests by canonical source identity, requested
  language, importer identity, mode, and diagnostics—not by a Python-only
  loader call.
- [ ] **H6.13** Replace `load_py_module` calls and Python branches in
  `build_ast.cpp` with generic module-graph/language-registry requests.
- [ ] **H6.14** Remove the Python declarations from `transpiler.hpp`.
- [ ] **H6.15** Test:
  - Python-to-Python packages and circular imports;
  - missing/failed module caching;
  - two sessions;
  - repeated imports;
  - canonical path aliases;
  - generic host-to-Python import;
  - supported cross-language value export;
  - deterministic rejection of unsupported crossings.

### Exit gate

- Python execution and imports use opaque host services;
- shared core contains no `load_py_module` or Python branch;
- module graph no longer requires a JS namespace for neutral bookkeeping;
- recovery and root state return to their exact entry watermark;
- all functional, GC, baseline, and performance gates pass.

## 15. Stage H7 — Hosted compiler-service extraction

### Goal

Allow Python to reuse the compiler pipeline without exporting internal C++
layouts or creating a second compiler framework. Extract one cohesive slice at
a time; keep the Python front end working after every slice.

### General rules

- Every handle is opaque across the module ABI.
- The host owns the memory behind host handles.
- The module must release handles in the documented nesting order.
- Every service table is versioned and size-gated.
- Compiler tables require the exact host build ID initially.
- No service accepts or returns `Runtime*`, `EvalContext*`, `Input*`,
  `AstNode*`, `MIR_context_t`, or C++ container types.
- Python extensions use Python-owned payloads/side tables. They do not enlarge
  records allocated for every Lambda or JS node.
- Existing shared builders, scopes, analysis records, emitter helpers, import
  metadata, root tracking, scalar homes, and diagnostics are wrapped or
  parameterized; they are not reimplemented.

### H7A — Source, paths, memory, names, and diagnostics

**Implementation checkpoint (2026-07-20).** The dynamic Python adapter now
uses independently versioned `JubeSourceAPI`, `JubeDiagnosticAPI`,
`JubeOutputAPI`, `JubeSessionMemoryAPI`, and `JubeGuestExecutionAPI` tables for
file source ownership/canonicalization, line-column conversion, diagnostics,
result rendering, short-lived session storage, and stack activation. These
tables expose no `Input`, pool, formatter, AST, or runtime layout. The
remaining H7A/H7D work is to move Python's compiler-owned source allocations,
diagnostics, and activation internals off direct host headers; this checkpoint
does not claim that the final compiler boundary is complete.

The Python front-end header no longer includes the monolithic
`transpiler.hpp`; it receives existing AST/name primitives through its specific
AST dependency. The architecture check guards that ratchet while the remaining
runtime and MIR implementation includes are extracted in separate review units.

- [ ] Expose source creation, canonical path, source slices, line/column maps,
  name interning, scoped compilation allocation, and diagnostic emission.
- [ ] Keep Tree-sitter Python and Python parse policy in the module.
- [ ] Replace direct Python construction/access of host `Input`, pools, and
  diagnostic internals.
- [ ] Test UTF-8 paths/source, syntax errors, source lifetime, repeated
  compilation, and diagnostic stability.
- [ ] Ratchet the internal-header allowlist after the slice.

### H7B — AST construction, binding, and type analysis

- [ ] Implement ADR-HL-05 around common AST construction and language profiles.
- [ ] Publish capability-discovered constructors/accessors for genuinely common
  node shapes.
- [ ] Keep Python-specific node kinds and metadata in Python-owned extension
  payloads or side tables referenced by opaque handles.
- [ ] Reuse common name/scope mechanics after adding only reviewed neutral
  concepts. Class/comprehension behavior remains Python profile logic unless a
  neutral scope contract is approved.
- [ ] Reuse common function/capture/type-evidence mechanisms where contracts
  match; keep Python annotations, dynamic rules, and representation decisions
  in Python hooks.
- [ ] Do not expose the host `AstNode` or type-record binary layout.
- [ ] Add AST/profile conformance tests independent of Python, followed by all
  Python scope, closure, class, comprehension, annotation, and error tests.
- [ ] Measure shared node sizes before and after; unexplained growth is a
  failed gate.

### H7C — Runtime lookup and MIR emission

**Implementation checkpoint (2026-07-20, h7d8).** Python now obtains
runtime-import effect metadata through the versioned Jube runtime catalog.
The host translates the fixed-width public record to its existing
`JitImportMetadata` contract; the shared emitter accepts a compiler-provided
lookup callback. Lambda and JavaScript install their unchanged direct registry
adapter, while Python installs the Jube adapter during module negotiation.
The lookup runs only while lowering a call and the emitted code retains the
same direct import target, so this adds no Lambda/JS execution-path branch or
lookup. The Python module no longer imports the private
`jit_import_get_metadata` resolver. Function finalization also now crosses the
same guest-execution lifecycle table, so the module no longer imports
`MIR_finish_func`. This is a compile-time lifecycle extraction only; it does
not change a Lambda/JS runtime path. Python's stack-overflow call also now
uses the existing shared emitter import cache rather than manufacturing a
separate MIR prototype in each lowering path. The remaining direct MIR-builder
symbols are deliberately left for the cohesive opaque builder extraction rather
than papered over with an ABI-unsafe wrapper.

**Implementation update (2026-07-20, h7e1).** Nested Python frames no longer
import `_lambda_rt`. The host supplies an activation-scoped opaque frame-runtime
slot through the size-gated guest-execution service, retaining the existing
load-before-prologue ordering required by shared root and scalar-home emission.
The module boundary verifier rejects the retired symbol, and Python closure,
generator, package, and forced-GC tests cover the service. The same pass fixed
a pre-existing Python TCO lowering defect: both overflow paths now use one
pointer-typed MIR prototype for `lambda_stack_overflow_error` rather than
creating duplicate same-name prototypes.

**Implementation update (2026-07-21, h7e6).** Boxed-`Item` function creation
now crosses a size-gated guest-execution service. The host constructs the MIR
signature and owns the returned function/item handles; Python receives them
only for the lifetime of the active MIR context. Python no longer calls or
imports `MIR_new_func_arr`, and the architecture verifier rejects its
reintroduction. This is a compiler-only service and does not change Lambda or
JavaScript emission or execution paths.

**Implementation update (2026-07-21, h7e7).** Forward function declarations
now use the same tail-appended, size-gated host compiler surface. This keeps
direct-call targets host-owned and removes Python's `MIR_new_forward` import.
The ABI tail ordering is explicitly protected so older negotiated execution
service offsets remain unchanged. The operation is compiler-only and has no
Lambda or JavaScript hot-path effect.

**Implementation update (2026-07-21, h7e8).** Python direct-call prototypes
now use a host-owned boxed-`Item` signature service. The host builds temporary
MIR parameter descriptors and returns a context-bound opaque prototype; Python
direct-call lowering no longer calls `MIR_new_proto_arr`. The shared emitter
still owns runtime-import prototypes until its cohesive opaque-builder
extraction. This tail-appended compiler service is
outside Lambda and JavaScript execution paths.

**Implementation update (2026-07-21, h7e9).** Named register resolution now
uses the tail-appended compiler service. The MIR function register table stays
host-private and Python receives only the numeric register identity needed for
lowering. The architecture verifier rejects direct `MIR_reg` use in Python;
this compile-time lookup does not affect Lambda or JavaScript hot paths.

**Implementation update (2026-07-21, h7e10).** Hosted lowering now records
the opaque identities of bound parameter registers in the shared compiler
state. Root liveness uses that explicit entry-state record, so the Python
module no longer imports MIR's private register-name lookup at all. Legacy
Lambda and JavaScript lowering retain their existing lookup path; the added
state is compiler-local and is never read on their execution paths. The
dynamic-module architecture gate rejects `_MIR_reg`, while Python golden and
forced-GC closure/generator tests protect the parameter-root invariant.

**Implementation update (2026-07-24, h7e11).** The activation-scoped runtime
token load that precedes Python frame setup is now emitted by the host through
a tail-appended, size-gated execution service. The host requires an active
guest activation, the host-issued frame-runtime slot, and the matching active
function relationship before it creates the address and memory-load MIR
operations; Python receives only the numeric runtime-register identity. The
architecture verifier rejects a raw frame-runtime memory load in Python.
This removes one host-memory access operation from lowering, while the broader
opaque builder/module/function/block/value/import service and the remaining
raw MIR operations still keep H7C open. The rebuilt host/module pair passes
the 43-test corpus, forced-GC closure/generator checks, isolated dispatch,
loader-negative checks, and the binary-aware architecture check.

**Implementation update (2026-07-24, h7e13).** Python's repeated compiler
identity allocation now crosses two tail-appended execution services: the host
allocates registers from a public I64/F64/pointer value-kind projection and
allocates opaque labels. Python continues to own its monotonic naming counter,
preserving generated-name ordering while the host retains MIR type coercion and
identity representation. Labels cross as opaque pointers because MIR labels
are pointer-backed; the initial numeric form would have truncated them on
64-bit builds and was corrected before acceptance. The architecture verifier
rejects Python calls to `em_new_reg` and `em_new_label`. The rebuilt h7e13 pair
passes the full Python corpus, forced-GC closure/generator checks, dispatch,
loader-negative, and binary-aware architecture gates. Raw operand/instruction
construction and shared emitter internals remain H7C work.

- [ ] Expose opaque MIR builder/module/function/block/value/import handles.
- [ ] Wrap the existing `MirEmitter` operations needed by Python: function
  creation, values, branches, calls, returns, constants, roots, frames, scalar
  homes, and metadata finalization.
- [ ] Resolve runtime imports through the H4 catalog; do not accept arbitrary
  raw host symbol lookup.
- [ ] Preserve `JitImportMetadata` effects through every wrapper.
- [ ] Reuse shared return funnels, recovery, rooting, and scalar-home machinery
  established by the Python stack-frame plan.
- [ ] Keep Python lowering decisions and Python runtime helper selection in
  `lambda/py`.
- [ ] Add negative tests for stale handle, wrong owner, use after finalize,
  missing capability, incompatible build ID, and invalid construction order.
- [ ] Compare representative generated MIR/call metadata before and after,
  allowing only intentional naming/ordering differences.

### H7D — Compile, link, execute, and cleanup

**Implementation checkpoint (2026-07-20, h7d8).** The guest-execution table
now owns Python MIR context create/destroy, module create/finalize/load,
function lookup/finalization, host resolver linking, recovery-wrapped entry
execution, and activation teardown. Python therefore has no direct calls to
`jit_init`, `MIR_finish`, `MIR_finish_module`, `MIR_load_module`,
`MIR_finish_func`, `MIR_link`, or `find_func`; the architecture check rejects
the corresponding retired dynamic imports. This closes the lifecycle part of
H7D, but not H7C's opaque builder migration: Python lowering still has raw MIR
builder calls and context-layout offsets, which must move together behind the
reviewed builder service.

- [ ] Make `JubeLanguageDef.compile` orchestrate H7A–H7C services without
  constructing host internals.
- [x] Make the host own finalization, JIT linking, direct import resolution,
  recovery boundary, execution, and teardown.
- [ ] Guarantee that compilation artifacts cannot outlive their host
  compilation unless explicitly promoted through a reviewed handle.
- [ ] Ensure Python session caches never retain transient AST/MIR handles.
- [ ] Remove remaining Python includes of internal transpiler, MIR, runtime,
  GC, module-registry, and data-layout headers.
- [ ] Reduce the architecture allowlist to the approved public Jube headers
  only.

### Exit gate

- Python compiles and executes using public base/hosted Jube APIs;
- no internal C++ structure crosses the module boundary;
- no Python-only field/branch has entered shared compiler structures;
- Python still owns all Python lowering and semantic decisions;
- all conformance, Python, GC, Lambda, Test262, and release-performance gates
  pass after each slice.

## 16. Stage H8 — Build and load external `lang-python`

### Goal

Turn the statically registered hosted language into a real external module
without changing its behavior or descriptor.

### Tasks

- [ ] **H8.1** Complete ADR-HL-08 and ADR-HL-09 before shipping the dynamic
  target.
- [ ] **H8.2** Add a generated build target in
  `build_lambda_config.json`; regenerate build files through the normal `make`
  flow. Do not manually edit generated `.lua` files.
- [ ] **H8.3** Build `lang-python` as `.dylib`, `.so`, or `.dll` from
  Python-owned sources and grammar/runtime dependencies only.
- [ ] **H8.4** Generate/validate `module.json` with:
  - module/language identity and version;
  - aliases and extensions;
  - base and hosted API requirements;
  - exact compatible host build ID;
  - platform library;
  - dependencies/resources;
  - integrity metadata.
- [ ] **H8.5** Use the generic discovery and dependency path. Remove the
  single-module environment-variable assumption as the primary mechanism;
  retain any developer override only if ADR-HL-08 specifies it generically.
- [ ] **H8.6** Implement Windows library loading, symbol lookup, error
  reporting, dependency search, and process-lifetime retention with behavior
  matching POSIX.
- [ ] **H8.7** Load and validate the descriptor before publishing its language
  aliases or runtime imports. On failure, leave no partial registration.
- [ ] **H8.8** Prove the module has no unresolved dependency on internal host or
  JS symbols. Only approved exported Jube entry points may cross the boundary.
- [ ] **H8.9** Run the same language conformance and 41-script corpus twice:
  once with static registration and once with the external module.
- [ ] **H8.10** Compare output, diagnostics, exit status, GC behavior, runtime
  import metadata, and repeated-session cleanup between modes.
- [ ] **H8.11** Test missing library, corrupt/wrong manifest, checksum mismatch,
  wrong ABI, wrong build ID, missing capability/dependency, duplicate language,
  and failed initialization.
- [ ] **H8.12** Confirm the host never unloads a successfully loaded module
  while JIT code or values may hold its function pointers.

### Exit gate

- external `lang-python` passes all tests on supported macOS, Linux, and
  Windows configurations;
- static and dynamic descriptors are behaviorally identical;
- standard-host runs do not map or initialize the Python library;
- module symbol/dependency inspection is clean;
- static registration can now be removed without losing diagnostic coverage.

## 17. Stage H9 — One executable and distribution convergence

### Goal

Retire the separate runtime build and make bundling the only difference between
standard and full Jube distributions.

### Tasks

- [ ] **H9.1** Implement ADR-HL-10 in generated build configuration.
- [ ] **H9.2** Ensure the normal `lambda.exe` always contains the small generic
  Jube registry/loader and hosted-language negotiation surface.
- [ ] **H9.3** Remove Python, Ruby, Bash, and their grammars/runtimes from the
  host executable source list and defines.
- [ ] **H9.4** Remove the independently compiled `lambda-jube.exe` runtime
  target.
- [ ] **H9.5** Provide the agreed compatibility behavior:
  symlink/copy/launcher to the same host artifact for a bounded transition, or
  a clear removal diagnostic. It must not be another compiled runtime.
- [ ] **H9.6** Replace `requires_jube_exe` and `LAMBDA_JUBE_EXE` assumptions
  with host plus required-module metadata.
- [ ] **H9.7** Define:
  - standard bundle: `lambda.exe` plus its small standard module set;
  - full Jube bundle: the exact same `lambda.exe` plus `lang-python` and other
    independently built modules.
- [ ] **H9.8** Add packaging verification that hashes the host executable from
  both distributions and requires equality.
- [ ] **H9.9** Add smoke tests that:
  - standard Lambda and JS work without Python;
  - standard Python request reports a useful missing-module error;
  - adding the module directory enables Python without replacing the host;
  - full bundle runs all Python tests;
  - removing the module returns to the absent behavior.
- [ ] **H9.10** Track host binary-size change separately from module size and
  compare absent-module startup/memory with H0.

### Exit gate

- one generated host target serves both distributions;
- standard/full host hashes match;
- Python is independently addable/removable as a bundle component;
- no Python code or grammar is linked into `lambda.exe`;
- all baseline, packaging, and performance gates pass.

## 18. Stage H10 — Remove migration scaffolding and close the architecture

### Goal

Remove temporary paths, finish documentation, and prove the boundary can be
maintained.

### Tasks

- [ ] **H10.1** Delete the static Python registration adapter and its build
  switch after dynamic parity is accepted.
- [ ] **H10.2** Delete obsolete Python declarations, macros, loader branches,
  and compatibility functions from shared headers/sources.
- [ ] **H10.3** Reduce the architecture checker allowlist to zero or a small
  permanent public-header list with written justification per entry.
- [ ] **H10.4** Confirm all Python runtime descriptors live in the module and
  all Python symbols disappear from the host binary export/import inventory.
- [ ] **H10.5** Update `doc/Lambda_Jube_Runtime.md`, CLI/help documentation,
  Python runtime documentation, build instructions, packaging instructions,
  and the design/ADR status records.
- [ ] **H10.6** Document the extraction template for the next hosted language:
  descriptor, session, runtime catalog, semantic adapter, compiler services,
  manifest, conformance suite, and packaging.
- [ ] **H10.7** Run the complete final matrix in section 19 and archive the
  performance comparison in section 20.
- [ ] **H10.8** Review every shared-runtime diff and record why it is generic;
  revert or move any convenience added only for Python.
- [ ] **H10.9** Review every JS-related diff and classify it as:
  - ownership/API partitioning with unchanged behavior;
  - root-cause bug fix with regression test;
  - separately approved generic runtime enhancement.
  Any design or global-performance change blocks closure.

### Exit gate

All completion criteria in section 23 are satisfied. No “temporary” exception
is deferred to post-completion.

## 19. Test and verification matrix

### 19.1 Required on every product-code stage

```sh
make build
make test-lambda-baseline
make test262-baseline
make test-jube
git diff --check
```

The Test262 result is accepted only when the final summary explicitly reports:

```text
failed: 0
retry: 0
```

If target names change during H9, equivalent targets must retain these
properties; do not weaken the gate while renaming it.

### 19.2 Shared runtime/compiler changes

Every shared change additionally runs:

- focused unit tests for the changed API;
- Jube ABI/capability conformance;
- module loader/registry tests;
- GC-rooting and recovery tests;
- architecture lint;
- release performance comparison when the changed code is reachable by
  ordinary Lambda or JS.

The generic core change and the Python consumer should be separate commits or
review units whenever practical. The core unit must be testable without
`lang-python`.

### 19.3 Python module changes

Required coverage:

- all 41 script/golden pairs;
- parse/build/scope/type/lowering diagnostics;
- functions, closures, recursion, defaults, varargs, and kwargs;
- classes, inheritance, descriptors, and bound methods;
- list/dict/set/tuple and iteration behavior;
- exceptions and recovery;
- imports, packages, cycles, failure caching, and namespaces;
- generators, coroutines, and async behavior;
- bigint and float boundary behavior;
- forced-GC closure/generator/coroutine/module tests;
- repeated session create/execute/reset/destroy;
- static/dynamic parity until static mode is removed.

Any new `test_py_*.py` must have a matching `.txt` expected result.

### 19.4 Loader and packaging changes

Test on every supported platform:

- correct, missing, malformed, incompatible, and duplicate manifests;
- missing/corrupt library and missing entry symbol;
- dependency order and failure rollback;
- standard and full bundle discovery;
- relative distribution path independent of current directory;
- user/project/explicit path precedence;
- filenames containing spaces and non-ASCII characters;
- process-lifetime module retention;
- bit-identical host executable between bundle variants.

### 19.5 Sanitizers and stress

Run the project's supported sanitizer configuration for:

- descriptor/session lifetime;
- repeated compile/execute/destroy;
- forced GC during cross-boundary calls;
- recovery from a Python exception and stack overflow;
- circular imports and failed initialization;
- generator/coroutine suspension and resume;
- shutdown/heap cleanup with values still reachable.

No leak, use-after-free, stale handle, unbalanced root frame, or changed stack
watermark is accepted.

## 20. Release performance protocol

### 20.1 What is protected

Measure with Python absent:

- empty/short Lambda CLI startup;
- representative Lambda compile/JIT/execute workloads;
- JS CLI startup;
- representative Test262-compatible JS loops, calls, objects, arrays, and
  module workloads;
- host resident memory and mapped libraries;
- host executable size;
- registry initialization/module-discovery counts.

Also measure Python startup and representative execution to detect accidental
per-call registry overhead, but Python speed is not allowed to trade against
Lambda or JS speed.

### 20.2 Method

- use `make release`, never a debug build;
- compare the same machine, compiler, power state, and workload;
- warm filesystem caches consistently;
- randomize or alternate before/after runs;
- use at least seven measured repetitions after warm-up;
- record raw samples, median, dispersion, revision, executable hash, and
  command;
- investigate outliers rather than deleting them silently;
- inspect profiles/disassembly when a shared-path regression appears.

### 20.3 Acceptance

The acceptance criterion is no statistically significant Lambda or JavaScript
startup/runtime regression outside measurement noise. In addition:

- no optional module library is mapped in absent-Python tests;
- no Python initialization/allocation occurs;
- no module lookup/capability check is added inside an existing evaluation,
  property, call, allocation, GC, or JIT-generated hot loop;
- runtime import lookup happens at registration/compile/link time and generated
  calls use cached direct targets;
- loader code-size growth is reported separately from `lang-python`.

Any measurable regression that cannot be demonstrated as noise stops the
stage. It requires root-cause analysis and, if genuinely necessary, a separate
design review and explicit approval.

## 21. Review checkpoints requiring explicit attention

Implementation pauses for review if any of the following becomes necessary:

1. changing Lambda or JavaScript observable semantics;
2. adding a field to a record allocated in normal Lambda/JS compilation or
   execution;
3. adding a new type/tag/operator/node kind to shared core solely for Python;
4. adding a module lookup or indirect call to an existing hot path;
5. retaining a direct Python dependency on a `js_*` function;
6. exposing an internal C++ layout through the module boundary;
7. weakening exact build-ID coupling for compiler APIs;
8. permitting module unload or untrusted execution;
9. accepting a nonzero Lambda/JS performance regression;
10. introducing a Python-only source/module search rule;
11. changing the stable base Jube ABI compatibility promise;
12. changing cross-language value semantics beyond the accepted membrane.

The review must present the concrete use case, existing facility searched,
semantic differences, proposed owner/API, GC/lifetime implications, hot-path
impact, alternatives, and test/performance evidence.

## 22. Risk register

| Risk | Consequence | Mitigation/gate |
|---|---|---|
| “Neutral” API secretly retains JS semantics | Python behavior becomes coupled to JS; future JS changes break Python | H2/H5 partition, semantic tests, zero `js_*` dependency |
| Compiler API freezes internals too early | long-term ABI burden and blocked refactoring | opaque handles, exact build ID, narrow capability tables |
| Shared AST/type records grow for Python | global compile-time memory/cache regression | Python side payloads, size measurements, explicit review |
| Runtime descriptors lose GC/call effects | under-rooting, corruption, or unnecessary barriers | reuse `JitImportMetadata`, reject/audit incomplete entries |
| Per-call module trampolines | global or Python hot-path slowdown | resolve once, cache direct target, generated-code inspection |
| Namespace graph remains JS-backed | hidden semantic dependency and GC lifetime bugs | split graph state from language membrane in H6 |
| Recovery crosses module with dirty state | stale context/root/number stack | host-owned activation and failure-path tests |
| Session caches retain compiler handles | use-after-free after compilation | lifetime hierarchy, negative tests, sanitizer runs |
| Static checkpoint becomes permanent | Python remains bundled into host | H8/H10 removal is a completion requirement |
| Dynamic/Windows behavior diverges | platform-specific failures | ADR-HL-09 and shared conformance suite |
| Manifest discovery affects startup | absent-module regression | lazy indexed discovery, release startup measurements |
| Build variants accidentally return | binary drift and test gaps | one host target and package hash equality |
| Migration combines refactor with new semantics | regressions cannot be attributed | behavior-preserving stages and separate feature work |
| Existing rooting work regresses | cross-language lifetime corruption | preserve frame/root/scalar-home contracts and forced-GC gates |

## 23. Definition of done

The hosted Python implementation is complete only when all of the following
are true:

- `lambda.exe py app.py` and `lambda.exe app.py` load `lang-python` through the
  generic registry;
- Python is an external Jube native module with a manifest and exact compatible
  host build ID;
- `lambda.exe` builds, starts, and runs Lambda/JS without Python sources,
  grammar, runtime library, initialization, or allocation;
- there is no separately compiled `lambda-jube.exe` runtime;
- standard and full distributions contain a bit-for-bit identical host;
- Python runtime descriptors are module-owned and absent from the core
  system-function registry;
- Python uses no JS implementation functions or JS semantic data APIs;
- Python accesses host runtime/compiler mechanics only through reviewed public
  Jube base/hosted-language APIs;
- shared core contains no Python-specific include, macro branch, field, loader,
  or command dispatch;
- module graph, recovery, GC, rooting, and session lifetimes pass stress and
  sanitizer tests;
- static and dynamic behavior parity was demonstrated, then the static adapter
  was removed;
- all 41 Python golden tests pass;
- `make test-lambda-baseline` reports zero failed tests;
- `make test262-baseline` reports zero failed tests and zero retries;
- Jube/module/packaging/GC tests pass on supported platforms;
- release measurements show no statistically significant existing Lambda or
  JavaScript startup/runtime regression;
- architecture lint has no unexplained allowlist entry;
- every ADR and affected runtime/Python/Jube document reflects the implemented
  contract.

## 24. Recommended implementation review units

Keep changes small enough to attribute regressions. A practical landing series
is:

1. baseline evidence, test split, and architecture checker;
2. Jube API ownership partition and ABI conformance;
3. capability/build-ID negotiation;
4. language descriptor and registry without CLI behavior change;
5. generic CLI plus static Python descriptor;
6. runtime descriptor projection;
7. move Python helper catalog and remove core entries;
8. neutral scalar/container/rooting data projection;
9. Python callable/object/namespace adapters; remove JS calls;
10. guest activation/recovery/session service;
11. neutral module graph and removal of `load_py_module`;
12. source/diagnostic compiler API;
13. AST/binding/type compiler API;
14. MIR/import/compiler execution API;
15. compile Python against public module headers only;
16. external module target and POSIX loader parity;
17. Windows loader parity;
18. one-host build and test-target convergence;
19. standard/full packaging and hash checks;
20. migration cleanup, final performance evidence, and documentation.

Each review unit carries its own focused tests, architecture-lint delta, and
baseline results. Units touching code reachable by Lambda or JavaScript also
carry release performance evidence.

## 25. Final implementation principle

Python is the extracting consumer, not the owner of the extracted runtime
services. A successful implementation leaves the host more clearly layered,
but not more Python-aware:

```text
Python semantic need
        |
        v
existing neutral Lambda mechanic? ---- yes ---> expose/reuse reviewed contract
        |
        no
        v
can contract be neutral and useful? -- yes ---> separate core proposal first
        |
        no
        v
keep a thin Python-owned semantic adapter
```

If the proposed solution cannot follow one of those paths without changing
Lambda/JS behavior or hot-path cost, it is not part of this migration and must
return to design review.
