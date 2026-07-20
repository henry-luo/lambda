# Jube Hosted-Language Architecture

> **Status:** design proposal; overall direction agreed 2026-07-20
>
> **First adopter:** Python (`lang-python`)
>
> **Scope:** hosting Python, Ruby, Bash, and future languages on the unified
> Lambda runtime through Jube native modules
>
> **Related design:** `vibe/Lambda_Desing_Native_Module.md`

## 1. Decision summary

Lambda will use one host executable and one runtime architecture:

```text
lambda.exe
  ├── Lambda language and unified runtime
  ├── JavaScript/TypeScript and Radiant
  ├── Jube registry, loader, and hosted-language services
  └── zero or more Jube modules discovered from the distribution
```

The current `lambda.exe` and `lambda-jube.exe` product split will be retired.
Both the standard and full Jube distributions will ship the same `lambda.exe`
host binary. They differ only in the modules bundled beside it:

- the standard distribution excludes most hosted-language modules;
- the full Jube distribution bundles Python, Ruby, Bash, and other supported
  modules;
- separately installed modules use the same loading and registration path.

Hosted languages are not independent runtimes embedded beside Lambda. They are
front-ends and semantic layers hosted by the Lambda runtime. They should reuse
the existing Lambda implementation wherever semantics and contracts permit,
including:

- runtime system functions and helper functions;
- the Item value model and data containers;
- allocation, GC, root frames, and recovery;
- source management, module graphs, and diagnostics;
- AST allocation, common AST nodes, and common analysis passes;
- type analysis and representation selection;
- transpiler infrastructure and MIR emission;
- JIT linking, execution, and runtime import resolution.

Reuse must occur through an explicit common-runtime contract. A hosted language
must not reach into Lambda- or JavaScript-specific implementation details merely
because an internal symbol is convenient.

When Python or another hosted language can reuse a Lambda- or JS-specific
facility, that facility is reviewed case by case. The review chooses one of:

1. use the existing common facility unchanged;
2. promote and generalize it into the unified runtime;
3. expose it through a versioned hosted-language extension API;
4. keep a semantic adapter inside the hosted-language module;
5. reject the reuse because the semantics or performance contract differ.

No Python-specific field, enum, branch, import, or lifecycle hook may be added
ad hoc to the Lambda or JS core.

## 2. Goals

### 2.1 Primary goals

1. **Maximize implementation reuse.** A hosted language should reuse Lambda's
   mature runtime and compiler mechanisms instead of recreating parallel
   allocators, containers, analysis frameworks, MIR helpers, or JIT lifecycle.
2. **Keep language semantics isolated.** Python behavior remains owned by the
   Python module even when its representation and mechanics use Lambda runtime
   facilities.
3. **Protect Lambda and JS stability.** Adding a Python feature must not enlarge
   shared AST records, add JS lowering cases, alter global hot paths, or change
   core performance without a separately reviewed runtime design.
4. **Use one native-module architecture.** Static development modules,
   distribution-bundled modules, and separately installed modules register
   through the same Jube descriptors.
5. **Use one executable.** The host binary always knows how to discover, verify,
   load, and interact with Jube modules.
6. **Make capabilities explicit.** Hosted languages declare the runtime and
   compiler services they require. Compatibility is checked before module code
   runs.
7. **Let Python validate the design first.** Python exercises parsing, AST
   construction, scopes, type analysis, MIR lowering, runtime helpers, imports,
   exceptions, closures, generators, async behavior, and standard-library
   integration. It is a stronger first proof than a simple function module.

### 2.2 Non-goals

- Freezing MIR as a serialized or distributed format.
- Making native modules safe for untrusted code.
- Providing hot unload while JIT code may retain module function pointers.
- Rewriting Python semantics to match Lambda or JavaScript.
- Promoting a feature merely because Python needs it.
- Requiring every compiler detail to enter the stable base Jube ABI.
- Making all existing core components dynamically loadable in the first phase.

## 3. Governing principles

### P1. One host, different bundles

The executable is a host, not a product-specific language bundle. Distribution
composition belongs to packaging and manifests, not preprocessor branches in
core runtime files.

### P2. Hosted languages are guests of the unified runtime

Python owns Python semantics, but Lambda owns the common execution substrate.
The module should delegate mechanics to the host wherever the contract is
language-neutral.

### P3. Reuse contracts, not implementation accidents

Calling a `js_*` function directly is not acceptable reuse. Including
`transpiler.hpp`, reading `EvalContext` fields, or referencing `_lambda_rt`
directly is not an acceptable module boundary. Reusable behavior must have an
explicit owner, contract, lifetime, and compatibility policy.

### P4. Promotion requires semantic neutrality

A facility is promoted to the common runtime only if its contract can be stated
without Lambda-, JavaScript-, or Python-specific semantics. Generic naming alone
does not make a facility generic.

### P5. Extended APIs are deliberate and versioned

Compiler hosting requires richer access than an ordinary native function
module. Those services live in a distinct hosted-language API with opaque
handles, capability discovery, size-gated tables, and explicit compatibility.

### P6. Language-owned data stays language-owned

Python scope directives, call metadata, closure analysis, exception state,
class rules, descriptor behavior, generators, and coroutine state belong to the
Python module unless independently accepted as common runtime concepts.

### P7. No cost when a module is absent

The standard distribution must not pay Python initialization, registry, binary
size, heap, or steady-state dispatch costs. Module discovery is lazy or cheaply
cached, and module checks do not enter Lambda or JS hot paths.

### P8. Common-runtime changes are separate design decisions

A Python feature change and the runtime enhancement it requests are separate
review units. The runtime proposal establishes the generic contract first; the
Python change consumes it afterward.

### P9. Static and dynamic registration are behaviorally identical

A statically linked migration checkpoint and a dynamically loaded final module
must publish the same descriptor and pass the same conformance tests.

### P10. Source remains the language specification

MIR and native code are private, derived implementation artifacts. Module
distribution never establishes a persistent MIR compatibility promise.

## 3.1 Agreed constraints and explicit tradeoffs

The following constraints and tradeoffs are accepted as part of this design.
They are normative inputs to every follow-up ADR and implementation review.

### C1. Performance impact is defined and measured

The Jube loader necessarily adds some host code, so literal zero binary-size
change is not a realistic criterion. "No performance impact" means:

- no statistically significant Lambda or JS startup or runtime regression when
  optional modules are absent;
- no module lookup, capability check, or new indirect call in an existing
  Lambda or JS execution hot path;
- module resolution occurs only during discovery, compilation, import, or JIT
  linking and produces cached handles or direct function pointers;
- JIT-generated calls resolve once rather than passing through a per-call module
  trampoline;
- loader code size is tracked separately from optional module size;
- any measurable regression outside the approved loader budget requires
  explicit design approval.

Benchmarks must use release builds and compare the same Lambda/JS workloads with
and without the architecture change.

### C2. Reuse mechanics without merging incompatible semantics

Hosted languages should reuse allocation, GC, containers, source management,
AST machinery, analysis frameworks, MIR emission, JIT infrastructure, and
language-neutral runtime helpers.

Reuse does not imply adopting another language's observable behavior. Python
does not automatically inherit:

- JavaScript objects, prototypes, descriptors, `undefined`, proxies, or
  coercion;
- Lambda truthiness, equality, total ordering, numeric overflow, or absence
  semantics;
- Lambda purity rules as Python function semantics.

When common mechanics have different language semantics, the hosted-language
module supplies a thin semantic adapter. A small adapter is preferable to
polluting the common runtime or changing Lambda/JS behavior.

### C3. Hosted compiler compatibility is build-coupled initially

The stable Jube base ABI serves ordinary native modules. The richer hosted
compiler API is separately versioned and may require an exact host build ID
while AST, analysis, MIR, and JIT services continue to evolve.

This avoids prematurely freezing compiler internals or MIR. Compatibility may
be relaxed only after usage demonstrates a stable contract and an ADR defines
the broader compatibility promise.

### C4. The existing Jube API must be cleaned up before Python adopts it

The existing Jube host API contains JS- and Radiant-specific services, and its
current value table is substantially backed by JS object/array operations. It
is not yet the neutral hosted-language API described here.

Python must not adopt those JS-backed operations as its runtime foundation.
This project may revise the experimental Jube ABI and migrate existing in-tree
modules before third-party compatibility makes that correction expensive.
Stable base facilities, language-neutral data services, hosted compiler
services, and domain-specific JS/DOM services must have clear ownership.

### C5. AST and type reuse must not enlarge shared records by default

Python metadata belongs in Python-owned nodes, composition records, side
tables, or opaque extension payloads. Reuse of shared AST and type machinery
does not authorize adding Python fields to records allocated for every Lambda
or JS compilation.

A field, enum, or runtime type is promoted only when a reviewed
language-neutral invariant justifies its presence and its memory/cache impact
has been measured.

### C6. Cross-language values and GC are the highest correctness risk

Cross-language functions, namespaces, closures, and native resources can
outlive the compilation or heap that created them. The lifecycle and GC ADRs
must explicitly define:

- heap/runtime ownership;
- scoped and persistent roots;
- weak caches and clear callbacks;
- JIT code and captured-environment lifetime;
- module reset and per-heap cleanup;
- borrowed data-view invalidation;
- teardown ordering and failure behavior.

Forced-GC, stack recovery, circular import, runtime reset, and cross-language
lifetime tests gate each migration stage.

### C7. Identical distributions require genuinely external modules

For the standard and full distributions to contain the same `lambda.exe`,
hosted-language grammars, runtimes, standard libraries, lowerers, and optional
native dependencies must not be linked into the host binary.

Static module registration is permitted only as an incremental development
checkpoint. Windows dynamic-loading parity, dependency packaging, manifest
validation, and host/module compatibility checks are blocking requirements for
the final distribution architecture.

### C8. Native Jube modules are trusted and remain loaded

Manifest checksums and signatures protect distribution integrity; they do not
sandbox native code. Jube modules execute with the host process's authority.

V1 keeps loaded modules resident for process lifetime. Hot unload is excluded
because JIT code, roots, callbacks, and native objects may retain module
function or data pointers.

### C9. Retiring `lambda-jube.exe` requires compatibility handling

Existing scripts, CI configurations, documentation, and user workflows may
invoke `lambda-jube.exe`. The name remains temporarily as a compatibility
symlink, copy, or forwarding launcher with a documented deprecation window.
It must not remain a separately compiled runtime variant.

### C10. Migration proceeds through independently valid checkpoints

The architecture will not land as a big-bang refactor. Each checkpoint keeps a
working runtime and complete regression gates:

1. generic language registration and dispatch;
2. a static Python Jube descriptor;
3. module-owned runtime imports;
4. removal of Python's JS-semantic dependencies;
5. extraction of common compiler services;
6. dynamic `lang-python`;
7. unified standard/full packaging.

The governing rule is: **maximize reuse below the semantic boundary without
making Lambda or JS carry another language's design or performance costs.**

## 4. Target system

### 4.1 Runtime and distribution shape

```text
Standard distribution
├── bin/lambda.exe
├── lib/                         host dependencies
└── modules/
    └── <small standard set>

Full Jube distribution
├── bin/lambda.exe               bit-for-bit same host build
├── lib/                         host dependencies
└── modules/
    ├── lang-python/
    │   ├── module.json
    │   ├── lang-python.<dylib|so|dll>
    │   └── module resources
    ├── lang-ruby/
    ├── lang-bash/
    └── other Jube modules
```

The same runtime build ID appears in the executable and its bundled modules.
Packaging tests should verify that the host executable in standard and full
distributions is identical.

The `lambda-jube.exe` name may remain temporarily as a compatibility
symlink/copy or launcher warning, but it must not remain a separately compiled
runtime variant.

### 4.2 Module discovery

The host discovers manifests without executing module code. Search order is:

1. modules explicitly selected by the command line;
2. the distribution's module directory relative to `lambda.exe`;
3. project-local modules;
4. user-installed modules;
5. paths explicitly configured by the user.

The final path spelling and precedence are part of the general Jube loader
design. Hosted-language discovery must not add a Python-only search mechanism.

Manifests declare:

- module name and version;
- Jube base ABI version;
- hosted-language API version and required capabilities;
- compatible host build ID or build-ID range;
- language names, aliases, and extensions;
- platform library name;
- dependencies and checksums;
- optional resources such as grammar queries or standard-library sources.

Loading remains lazy. `lambda script.ls` should not load or initialize Python.
`lambda py app.py`, executing a `.py` file, or importing a Python module causes
the Python module to be resolved and loaded.

### 4.3 CLI behavior

The CLI dispatches through the language registry:

```text
lambda app.py
lambda py app.py
lambda run --lang python app.py
```

Aliases such as `py` are declared by the language module, not hardcoded in
`main.cpp`. Help output can list discovered languages. If the Python module is
absent, the error is a generic language-resolution error that identifies the
missing module; the core does not contain a Python fallback.

## 5. Architecture layers

### 5.1 Layer A: Jube base ABI

The base ABI serves all native modules. It remains small, stable, additive, and
pure C. It covers:

- module identity and lifecycle;
- capability discovery;
- Item transport;
- GC roots and weak references;
- common value construction and inspection;
- errors and diagnostics;
- native object wrapping;
- public functions, types, namespaces, and interfaces.

Ordinary native modules should not need the hosted-language API.

### 5.2 Layer B: unified runtime services

These are language-neutral services already implemented by Lambda or promoted
after review:

- Item tags and scalar boxing;
- strings, arrays, maps, elements, ranges, and VMaps;
- allocation classes and GC;
- root frames and side stacks;
- source buffers and path handling;
- module cache and dependency graph;
- recovery boundaries;
- runtime system-function catalog;
- JIT import resolution;
- diagnostics, logging, and test hooks.

The implementation may remain internal, but the callable projection exposed to
modules must have a documented contract.

### 5.3 Layer C: hosted-language compiler API

Hosted languages require compiler-oriented services. They receive an optional
`JubeHostLangAPI` from the host after compatibility checks.

Conceptually it contains sub-APIs:

```c
typedef struct JubeHostLangAPI {
    uint32_t api_version;
    uint32_t struct_size;

    const JubeSourceAPI* source;
    const JubeDiagnosticAPI* diagnostic;
    const JubeAstAPI* ast;
    const JubeTypeAPI* type;
    const JubeRuntimeCatalogAPI* runtime;
    const JubeMirAPI* mir;
    const JubeGuestExecutionAPI* execution;
    const JubeModuleGraphAPI* module;
} JubeHostLangAPI;
```

This sketch defines ownership, not final field layout. Each sub-API needs its own
review before ABI commitment.

The hosted-language API follows these rules:

- C ABI only at the module boundary;
- opaque handles instead of C++ structure layouts;
- `api_version` and `struct_size` on every independently evolving table;
- additive evolution within a version;
- explicit feature/capability queries;
- no guest-specific fields;
- documented allocation, ownership, thread, GC, and error behavior;
- no exposure of internal globals;
- no promise that emitted MIR survives outside the active compilation.

Because compiler services evolve faster than the base ABI, a bundled language
module may require an exact host build ID. This is preferable to pretending MIR
and compiler internals have a stable long-term ABI. Compatibility may be relaxed
later only after experience demonstrates a stable boundary.

### 5.4 Layer D: hosted-language module

The Python module owns:

- Tree-sitter Python and Python parsing policy;
- Python AST node extensions and scope rules;
- Python semantic and type-analysis hooks;
- Python lowering decisions;
- Python runtime helper implementations;
- Python objects, classes, descriptors, and method binding;
- Python exception state and hierarchy;
- generators, coroutines, and Python async semantics;
- imports, packages, builtins, and standard-library modules;
- Python-facing diagnostics and formatting;
- module-local caches and lifecycle state.

The module uses Layer B and Layer C services but does not modify their record
layouts or add Python cases to their implementation without review.

## 6. Hosted-language descriptor

`JubeModuleDef` gains a size-gated hosted-language capability. The exact ABI is
designed in its own implementation ADR, but the required behavior is:

```c
typedef struct JubeLanguageDef {
    uint32_t abi_version;
    uint32_t struct_size;

    const char* name;
    const char* const* aliases;
    int32_t alias_count;
    const char* const* extensions;
    int32_t extension_count;

    uint64_t required_host_capabilities;

    int (*create_session)(const JubeLanguageSessionConfig* config,
                          JubeLanguageSession** out);
    void (*destroy_session)(JubeLanguageSession* session);

    int (*compile)(JubeLanguageSession* session,
                   const JubeLanguageCompileRequest* request,
                   JubeLanguageCompileResult* result);

    int (*execute)(JubeLanguageSession* session,
                   const JubeLanguageExecuteRequest* request,
                   JubeLanguageExecuteResult* result);

    int (*load_module)(JubeLanguageSession* session,
                       const JubeLanguageModuleRequest* request,
                       Item* namespace_out);

    void (*runtime_reset)(void);
} JubeLanguageDef;
```

The host owns orchestration and common lifecycle. The module owns language
semantics. A coarse `compile` callback is required for migration; finer staged
callbacks may be added only when they enable real reuse and have a reviewed
contract.

The request/result structures use opaque handles and carry:

- source text and canonical path;
- execution mode: script, module, eval, or REPL;
- arguments and environment explicitly supplied by the host;
- diagnostics sink;
- runtime/heap activation handle;
- compilation options;
- result value and status.

No `Runtime*`, `EvalContext*`, `Input*`, `MIR_context_t`, `AstNode*`, or other
internal C++ layout crosses the stable module boundary.

## 7. Reusing the Lambda compiler and runtime

### 7.1 Runtime function catalog

The monolithic global import table is replaced or supplemented by descriptor
registration. Every callable runtime helper has metadata describing:

- stable import name or runtime-function ID;
- boxed/native signature;
- return representation;
- argument ownership and borrowing;
- whether it can allocate or collect;
- whether it can re-enter script;
- exception behavior;
- number-stack behavior;
- purity and procedural effects where applicable.

Hosted-language MIR lowering resolves a common helper through
`JubeRuntimeCatalogAPI`. It does not copy a declaration or guess the helper's GC
behavior.

Python-private helpers are registered by `lang-python`, for example under
`lang.python.*`. They are not public system functions and do not appear in the
core registry when the module is absent.

A Python builtin may reuse a Lambda system function only when the Python module
preserves Python semantics around it. For example, a common byte-copy helper can
usually be reused directly; Python `len`, equality, truthiness, iteration, and
attribute access normally require Python dispatch before reaching a common
container primitive.

### 7.2 Common value and data API

The current Jube value table is backed substantially by JavaScript object and
array functions. It is therefore a JS bridge, not the common data API for
hosted languages.

Add a language-neutral data API as an additive host capability. It projects
Lambda runtime operations for:

- scalar construction and inspection;
- string creation and stable/borrowed views;
- Lambda arrays and numeric arrays;
- maps and elements;
- shape-independent key/value access where supported;
- VMap/native object construction;
- iteration over common containers;
- explicit copying when a data-zone view cannot survive allocation.

Its semantics must not include JavaScript `undefined`, prototype traversal,
property descriptors, proxies, or ECMAScript coercion.

Python implements its object and attribute protocol on top of these primitives.
It does not use `js_new_object`, `js_property_get`, `js_new_function`, or other
JS-specific behavior.

### 7.3 AST construction

The host exposes common AST construction and traversal through `JubeAstAPI`.
The goal is to reuse:

- AST arenas and source spans;
- common literal, identifier, call, block, branch, loop, function, assignment,
  return, and collection shapes where their structural contract matches;
- name interning and scope-independent symbol identity;
- generic traversal and diagnostic utilities;
- reusable analysis pass drivers.

Common AST records contain only common fields. Python-only metadata is stored in
Python-owned nodes or an opaque language-extension payload owned and destroyed
by the Python module.

Forbidden examples include:

- adding Python `global`/`nonlocal` flags to shared `NameEntry`;
- adding Python argument counts to every shared call node;
- adding Python class/comprehension scope values to a global scope enum;
- adding Python literal kinds that JS and Lambda lowering must switch over;
- adding Python payload aliases to shared unions.

If multiple languages independently need a concept, it can be proposed for
promotion based on a common invariant rather than copied into the base record.

### 7.4 Type analysis

Hosted languages may reuse:

- the common Type representation where it accurately models runtime values;
- union construction and normalization;
- numeric representation analysis;
- container element/key/value inference;
- function call-shape analysis;
- control-flow and liveness infrastructure;
- representation selection for MIR calls.

Python semantic types that do not match Lambda types remain Python-owned
analysis descriptors. They can map to a common runtime representation without
becoming new global `TypeId` values.

Adding a new core runtime type is a language-design decision affecting Lambda,
JS interop, GC, formatting, equality, and ordering. It requires a formal
runtime/type-system review and cannot originate as a Python compiler shortcut.

### 7.5 MIR emission

The hosted-language MIR API should reuse the shared emitter's proven mechanics:

- compilation-unit and function lifecycle;
- registers, labels, blocks, and calls;
- import creation and caching;
- ABI call normalization;
- precise-root candidate tracking;
- liveness and root-slot assignment;
- side-stack frames and overflow checks;
- scalar boxing/unboxing;
- return homes and exception polling;
- module finalization, linking, function lookup, and cleanup;
- debug dumps and compiler telemetry.

The module supplies language-specific lowering; the host supplies mechanics.

`JubeMirAPI` uses opaque compilation, function, register, label, and import
handles. It may be build-ID-coupled initially. It does not expose a serialized
MIR format and does not permit a module to retain internal handles after
compilation ends.

Direct inclusion of `mir_emitter_shared.hpp`, direct use of `MIR_context_t`, or
access to `_lambda_rt` is a migration state, not the final module contract.

### 7.6 Execution and recovery

The host owns:

- activating or reusing an evaluation heap;
- installing the current runtime context;
- entering and leaving root/number side stacks;
- signal/stack-overflow recovery boundaries;
- JIT context lifetime;
- notifying modules before heap cleanup;
- restoring the previous context after guest execution.

The Python module must not construct `EvalContext` manually or modify global
runtime pointers. `JubeGuestExecutionAPI` brackets execution and supplies the
opaque handles needed by compiler and runtime services.

### 7.7 Module graph and imports

The host owns generic source identity, cache entries, loading state, dependency
edges, and circular-load detection. The Python module owns:

- Python search-path rules;
- package and `__init__` behavior;
- relative import semantics;
- Python module namespace construction;
- Python errors for unresolved imports.

Cross-language loading calls the generic Jube registry. Lambda must not contain
`load_py_module()` branches, and Python must not use JS namespace objects to
publish exports.

## 8. Case-by-case promotion review

### 8.1 Trigger

A promotion review is required when a hosted-language change would:

- include a Lambda- or JS-specific header;
- call an unexposed core or JS function;
- add a field or enum to a shared structure;
- add a hosted-language branch to a core pass;
- add an import to the global runtime table;
- change Item, GC, AST, type, JIT, module, or event-loop behavior;
- add work to a Lambda/JS hot path;
- require a shared global or thread-local variable;
- alter core build dependencies or distribution size.

### 8.2 Required analysis

The proposal must answer:

1. What Python behavior needs the facility?
2. What existing Lambda or JS behavior is being reused?
3. Are the observable semantics actually identical?
4. Can an adapter remain inside the Python module?
5. Is there a language-neutral invariant?
6. Which other current or plausible consumers benefit?
7. What are the ownership, lifetime, GC, error, and threading contracts?
8. Does the API expose data-zone pointers or internal layouts?
9. Is the call on a compilation, startup, or runtime hot path?
10. What binary-size, memory, startup, and steady-state costs result when the
    Python module is absent?
11. How is the API versioned and tested?
12. Can the enhancement be implemented and reviewed separately from the Python
    feature?

### 8.3 Possible outcomes

**Use existing common runtime.** The existing contract is already neutral and
complete.

**Promote to common runtime.** Move/refactor implementation under a neutral
name and owner. Existing Lambda and JS users migrate through the same API so
the generic path is continuously exercised.

**Expose through hosted-language API.** Keep the implementation internal but
publish an opaque, versioned capability. This is appropriate for compiler/JIT
services whose internals should remain private.

**Keep a module adapter.** Reuse lower-level primitives while Python retains
semantic translation.

**Reject.** Duplicate a small amount of language-specific policy rather than
polluting a shared contract or slowing unrelated languages.

### 8.4 Review and landing policy

- The common-runtime design lands before the Python consumer.
- The core change has neutral tests independent of Python.
- The Python change contains no opportunistic core edits.
- Shared API additions are additive and size-gated.
- Performance-sensitive changes include release-build measurements.
- Changes affecting Lambda semantics require the appropriate language ADR.
- Changes affecting JS semantics retain the full Test262 and JS regression
  gates.

## 9. Python-first module

### 9.1 Module identity

Recommended module identity:

```text
module:     lang-python
language:   python
aliases:    py
extensions: .py
```

The source can remain under `lambda/py/` during migration to avoid a
non-functional mass move. Build isolation matters more than directory spelling.
The module should become its own build target and library before dynamic loading
is attempted.

### 9.2 Python module contents

The module target contains:

- Python Tree-sitter grammar binding;
- Python AST declarations and builder;
- Python scopes and analysis;
- Python MIR lowering;
- Python runtime, bigint adapter, classes, builtins, async, and stdlib;
- Python-private runtime import descriptors;
- the `JubeModuleDef` and `JubeLanguageDef`;
- Python module tests and optional resources.

It does not export its private helper symbols as host APIs.

### 9.3 Immediate coupling to remove

The migration must remove:

- `py_*` entries and Python headers from `sys_func_registry.c`;
- Python command handling from `main.cpp`;
- `load_py_module()` declarations and branches in shared transpiler/AST files;
- `LAMBDA_PYTHON` branches in shared AST and runtime sources;
- Python calls to `js_property_get`, `js_new_object`, `js_new_function`,
  `js_array_*`, and `js_runtime_set_input`;
- direct Python manipulation of `EvalContext`, `Input`, `_lambda_rt`, and
  global module registries;
- Python-only fields or aliases in shared AST records;
- Python tests that make a core-only test target build the full module bundle.

### 9.4 Reuse inventory

Before defining the hosted-language API, inventory every current Python
dependency and classify it:

| Dependency | Likely disposition |
|---|---|
| Item tags and scalar packing | base Jube/common data ABI |
| Heap allocation and root frames | base Jube GC API |
| Pools, arenas, and name interning | hosted compiler API |
| Common AST nodes and analysis | hosted AST/type APIs |
| Shared MIR emitter | hosted MIR API |
| JIT init/link/find/finish | guest execution/MIR API |
| File/path operations | source API |
| Module cache/circular loading | module graph API |
| JS object/property helpers | replace with common data API + Python adapter |
| JS function wrapper | replace with Python-owned callable representation |
| Lambda system helpers | runtime catalog when semantics match |
| Python operators/classes/builtins | remain module-private |

The API is extracted from this inventory. It must not be designed from a
speculative wish list.

## 10. Migration plan

### Phase 0: architecture firewall

- Adopt this design as the governing boundary.
- Freeze new Python branches and fields in shared Lambda/JS structures.
- Add dependency and symbol lint rules.
- Split Python forced-GC tests into a Jube-module test target.
- Capture standard-distribution startup, binary-size, Lambda, and JS
  performance baselines.

**Exit gate:** no new Python feature can increase core coupling.

### Phase 1: one host executable

- Build Jube registry and loading support into the standard `lambda.exe`.
- Implement Windows dynamic loading parity.
- Move variant differences from source selection to distribution manifests.
- Add generic language lookup and CLI dispatch.
- Introduce runtime build-ID and capability checks.
- Keep `lambda-jube.exe` only as temporary compatibility packaging.

**Exit gate:** a single host binary runs correctly with an empty, standard, or
full module directory.

### Phase 2: Python static Jube descriptor

- Add `JubeLanguageDef` and the minimum reviewed hosted-language API.
- Build Python as a separate static module target for the migration checkpoint.
- Register it through the same descriptor path used by dynamic modules.
- Route Python CLI execution through the generic language dispatcher.

**Exit gate:** behavior is unchanged, but core dispatch contains no Python
command branch.

### Phase 3: private runtime imports

- Add module-owned runtime-import descriptors with complete call/GC metadata.
- Move all Python JIT helper registration out of the global registry.
- Qualify and resolve imports through the Jube module registry.
- Verify precise rooting through the shared emitter contract.

**Exit gate:** a core build has no `py_*` JIT imports; Python tests remain green.

### Phase 4: common data and execution services

- Add the reviewed language-neutral data API.
- Add guest execution/context lifecycle services.
- Replace Python's JS helper calls and direct runtime globals.
- Move Python namespaces and callables to Python-owned representations backed
  by common Item/GC primitives.

**Exit gate:** Python contains no dependency on JS runtime semantics.

### Phase 5: compiler service extraction

- Extract source, AST, type, runtime catalog, MIR emission, and module-graph
  services based on the dependency inventory.
- Move Python through opaque service interfaces.
- Keep Python extension metadata entirely module-owned.
- Remove shared `LAMBDA_PYTHON` branches.

**Exit gate:** the Python module compiles against Jube base and hosted-language
headers rather than arbitrary Lambda compiler headers.

### Phase 6: dynamic `lang-python`

- Produce `lang-python.<dylib|so|dll>` and its manifest.
- Bundle the Python grammar and resources with the module.
- Run static/dynamic parity tests.
- Remove static Python registration from the host.

**Exit gate:** adding or removing the Python module directory enables or
disables Python without changing `lambda.exe`.

### Phase 7: distribution convergence

- Package the same `lambda.exe` in standard and full distributions.
- Remove the separate Jube build variant and defines.
- Retire `lambda-jube.exe` after a documented compatibility window.
- Apply the proven hosted-language pattern to Ruby and Bash.

**Exit gate:** distribution composition is entirely module-based.

## 11. Dependency rules

Automated checks should enforce:

### Core rules

- Core runtime/compiler files do not include `lambda/py/**`.
- Core sources do not reference `py_*`, `Py*`, `.py`, or Python parser symbols,
  except generic manifest/test data where explicitly allowed.
- `lambda.exe` links no Python grammar or Python runtime symbols when the module
  is absent.
- Shared AST and type records contain no hosted-language-specific fields.

### Python module rules

- Python does not include JS runtime headers or call `js_*`.
- Python includes only its own headers, approved public Jube headers, and
  explicitly approved hosted-compiler SDK headers.
- Python does not access runtime globals directly.
- Python private imports are declared in its module descriptor.
- Python-held Items follow the Jube root-frame and persistent-root contracts.

### Build rules

- The host target does not enumerate hosted languages in source code.
- The build/packaging configuration generates bundled-module registration or
  manifests.
- Core-only test targets do not depend on full Jube module builds.
- Standard/full packaging verifies host-binary identity.

## 12. Performance policy

The architecture must not impose a global performance tax.

1. Module discovery is lazy or cached outside execution hot paths.
2. No per-operation module lookup is added to Lambda or JS value operations.
3. Registry lookup occurs at import, compile, or link time and produces cached
   pointers/handles for execution.
4. Hosted-language capability checks occur at module load/session creation.
5. Common helpers preserve current direct-call or JIT-import performance after
   resolution.
6. Promoted APIs do not add indirect calls to existing hot paths unless release
   benchmarks demonstrate acceptable impact and the design review approves it.
7. A missing hosted-language module consumes no runtime heap and initializes no
   guest state.
8. Standard distribution binary size excludes hosted-language implementations
   and grammars.

Every performance-sensitive promotion records:

- before/after release-build benchmarks;
- startup time with no optional modules;
- Lambda and JS representative workloads;
- compilation time if compiler APIs are affected;
- memory and binary-size differences;
- whether the cost is paid once or per operation.

## 13. Lifecycle, GC, and errors

### 13.1 Module lifecycle

- ABI and build compatibility are checked before `init`.
- Modules are loaded once and remain loaded for process lifetime in v1.
- Runtime/session reset is distinct from process shutdown.
- Heap cleanup notifications occur while the heap is still active.
- Module caches identify the heap/runtime they belong to.

### 13.2 GC

- Temporary native call arguments use scoped root frames.
- Persistent module-held Items use registered roots.
- Weak caches use the weak-root API and clear callbacks.
- Borrowed data-zone pointers never survive an allocation.
- Module-owned native resources use reviewed finalizers that do not allocate or
  call back into script.
- Generated MIR uses the common precise-root analysis and call-effect metadata.

### 13.3 Errors

- No C++ exception, `longjmp`, or guest-language unwind crosses the Jube module
  boundary.
- Module callbacks return explicit status and diagnostic information.
- Python exception propagation remains inside Python runtime semantics.
- The host translates boundary failures into generic CLI/module errors.
- JS pending-exception behavior is not reused as the Python exception model.

## 14. Testing and acceptance gates

### 14.1 Core host gates

- `make build`
- `make test-lambda-baseline`: zero failures
- `make test262-baseline`: zero failures and zero retries
- core Jube loader and ABI mismatch tests
- module-absent and missing-language tests
- standard-distribution startup and size checks
- architecture lint and forbidden-symbol checks

### 14.2 Python module gates

- all Python unit and baseline tests;
- imports, packages, circular imports, and cross-language module tests;
- forced-GC precise-root tests;
- stack-overflow/recovery tests;
- static/dynamic descriptor parity;
- module load/reset/heap-cleanup lifecycle tests;
- ABI/build-ID rejection tests;
- no-JS-dependency symbol audit.

### 14.3 Distribution gates

- identical `lambda.exe` in standard and full bundles;
- standard bundle runs Lambda/JS/Radiant without optional modules;
- full bundle discovers and runs all hosted languages;
- removing `lang-python` causes a clean missing-module error;
- adding it back requires no executable rebuild;
- module manifests are readable and verifiable without loading code;
- platform parity on macOS, Linux, and Windows.

### 14.4 Performance gates

- no statistically meaningful Lambda or JS runtime regression when optional
  modules are absent;
- no global startup regression beyond the approved loader budget;
- Python compilation/runtime performance is measured separately;
- dynamic module loading cost is paid once and reported independently.

## 15. Completion criteria

The Python-first architecture is complete when:

1. `lambda.exe` is the only runtime executable architecture.
2. Standard and full distributions differ only by bundled modules/resources.
3. Python is a `lang-python` Jube module loadable without rebuilding the host.
4. The core contains no Python includes, parser symbols, runtime imports,
   feature flags, CLI branches, AST fields, or type cases.
5. Python contains no direct JavaScript runtime dependency.
6. Python uses the common runtime and hosted compiler APIs for shared mechanics.
7. All Python-private semantics and helpers remain module-owned.
8. Every common-runtime enhancement has an independent design/review record.
9. Core Lambda and JS correctness and performance gates remain unchanged.
10. Ruby, Bash, and future languages can adopt the same contract without adding
    new language-specific branches to the host.

## 16. Decisions recorded

| Topic | Decision |
|---|---|
| First hosted-language adopter | Python |
| Host executable | one `lambda.exe` |
| Distribution difference | bundled Jube modules/resources only |
| `lambda-jube.exe` | transitional compatibility name, then retired |
| Hosted-language model | front-end/semantic module over unified Lambda runtime |
| Reuse policy | maximize reuse through explicit common or extended APIs |
| Reuse boundary | share mechanics; preserve each language's observable semantics |
| Lambda/JS-specific reuse | reviewed case by case |
| Python dependence on JS helpers | rejected |
| Python fields/branches in shared core | rejected |
| Compiler API | separate versioned `JubeHostLangAPI` with opaque handles |
| Hosted compiler compatibility | exact host build ID initially |
| MIR compatibility | private and build-coupled initially; never a distribution format |
| Python runtime imports | module-owned descriptors, absent from core registry |
| Module loading | lazy, manifest-driven, same path for bundled/installed modules |
| Existing Jube ABI | may be revised now to establish neutral/runtime/compiler boundaries |
| Absent-module performance | no statistically significant Lambda/JS regression |
| Hot-path policy | no per-operation module lookup or trampoline |
| Static language modules | migration checkpoint only |
| Module trust | trusted native code; checksums are integrity, not sandboxing |
| Hot unload | out of scope for v1 |
| Migration style | independently valid, fully tested checkpoints |
| Core enhancement landing | separate reviewed change before hosted-language consumer |

## 17. Follow-up design work

This proposal establishes architecture and policy. The following require focused
implementation ADRs before code lands:

1. `JubeLanguageDef` ABI and session lifecycle.
2. `JubeHostLangAPI` capability/version negotiation.
3. Language-neutral `JubeHostDataAPI`.
4. Runtime-function descriptor and call-effect catalog.
5. Opaque AST/type/compiler service APIs.
6. Opaque MIR emission and guest execution services.
7. Generic module graph and cross-language namespace contract.
8. Manifest schema, build-ID compatibility, and distribution paths.
9. Windows dynamic loading parity.
10. Build and packaging convergence on one `lambda.exe`.

Each ADR should use Python as the extracting consumer while proving that the
resulting contract contains no Python-specific semantics.
