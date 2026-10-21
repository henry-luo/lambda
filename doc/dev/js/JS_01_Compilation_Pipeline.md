# LambdaJS — Compilation Pipeline

> **Last verified against tree:** 2026-10-21 (integration in progress; qualification is recorded separately).
>
> **Part of the [LambdaJS detailed-design set](JS_00_Overview.md).**
> **Authority:** D8.1.3v29, D8.2.3, D8.5.1v2 and D5.3/D5.4.
> **Primary sources:** `lambda/js/js_execution.cpp`, `lambda/js/mvp-lmd/mvp_lmd_mir.cpp`, `lambda/js/js_code_cache.cpp`, `lambda/js/js_source_module.cpp` and `lambda/js/js_compile_recovery.cpp`.

## 1. Engines and public entry points

MVP-Lmd is the formal JavaScript MIR compiler (**D8.1.3v25**). The legacy
`JsMirTranspiler`, its split lowering files and its private context headers
have been removed. Public `transpile_js_to_mir*` names remain host APIs;
they select MVP for MIR execution. `transpile_js_module_to_mir` uses the same
engine selection for modules.

`lambda.exe js app.js` defaults to AUTO (**D8.1.3v29**). Unset, empty and
`JS_EXEC_BACKEND=auto` selectors begin supported units in AST; unsupported
units and eligible hot definitions use fast MVP MIR. Explicit `mir` selects
whole-unit MVP MIR, and `ast` forces the interpreter. The redundant
`--runtime=mvp-lmd` option remains removed from Script and batch commands. A terminal
CLI Script preserves closed-unit planning unless its source or host dependencies
require the complete realm. Document, module and later-Script entries still
use broad realm admission; finer effect analysis remains in progress.
`--timing` reports execution separately from compilation. `-p` prints the
Script completion using MVP value formatting after the timed body.
See the [integration record](../../../vibe/impl/JS_MVP_Integration.md)
for the remaining work and qualification.

Default AUTO and explicit AST execution retain the shared interpreter. AUTO function
and loop promotion compile through MVP. Dynamic source (`eval`, Function-family
constructors and string timer handlers) remains interpreted. Direct eval on both
tiers resolves live `JsInterpEnv` cells and validates context through the shared
AST facts. The legacy MIR projection journals and bridged-eval entry are removed;
lexical receiver payloads use Lambda scalar homes owned by the GC environment.
Explicit AST/native and MVP object/callable profiles remain distinct; see the
[alignment audit](../../../vibe/impl/JS_Interpreter_MVP_Alignment.md). A rejected formal
MIR unit does not enter a second JavaScript MIR compiler.

TypeScript uses the first-party parser's existing profile through
`MvpLmdOptions.typescript`. An unshadowed `type(value)` call reuses
`ts_type_info`; annotations and lowered enum constructs use the shared AST.

## 2. Shared frontend

The first-party C lexer/parser reduces source directly into `JsAstNode` /
Lambda AST layouts. `js_transpiler_parse_c` runs the retained frontend schedule:
parse/build, direct scope binding, early-error validation and AST indexing.
TypeScript selects `JS_PARSE_TYPESCRIPT`; modules select `JS_PARSE_MODULE`.

`JsScript` retains source/AST identity and frontend facts. A transient
`JsTranspiler` extends that prefix with current parser/build state. AST execution
adopts the builder into a runtime-owned Script. MVP retains its frontend under
the execution/program owner. Indexed nodes and binding identities feed later
analysis; metadata belongs to that owner (**D8.2.3**).

## 3. MVP compilation schedule

`mvp_lmd_compile_unit` continues the frontend's `CompilerPassManager`:

| Pass | Worker | Result |
|---|---|---|
| `mvp-admit` | `admission_pass` | Admitted syntax, indexed functions/bindings, captures and environment layout. |
| `mvp-plan` | `representation_pass` | Scalar/array/shape facts and guarded representation/specialization plans. |
| `mvp-lower` | `lower_pass` | MIR functions, ownership frames, finalization and native linking. |

Admission/planning can run before linking, allowing modules to establish their
binding graph before guest bodies execute. `LmdCompiler` embeds the shared
`MirEmitter`; Lambda and JavaScript use the same import, call, frame and scalar
ownership primitives. See [JS_04](JS_04_MIR_Lowering.md).

## 4. Execution and lifetime

`mvp_lmd_execute_in_runtime` binds the Runtime's canonical EvalContext, heap,
name pool, DOM context and event-loop turn. The Runtime retains the MVP
execution and its native units through callable and heap finalization
(**D5.3/D5.4**). Shared AST/native functions retain their own executable
capabilities. Promoted AST definitions keep their MVP bodies in precisely
traced code-record cells; shared compiled-call entries remain available.

Standalone API clients acquire an ordinary Lambda Runtime and use the same
context initializer and runtime cleanup. The MVP execution record no longer
embeds an independent EvalContext. Optional JS capsules remain lazy.

Parser/source recovery is implemented in `js_compile_recovery.cpp`.
`JsCompileRecoveryState` owns interrupted frontend builders and source buffers.
The retired compiler's active-transpiler/MIR-context and deferred-code stores
are absent. Runtime-owned MVP units govern native-code lifetime.

## 5. Modules, host loading and code reuse

`js_source_module.cpp` owns source-module linking, namespaces, live bindings
and dependency evaluation. `js_module_loader.cpp` supplies shared path/import-map
resolution and dependency loading. `js_execution.cpp` supplies public execution,
CommonJS require, dynamic-import adapters and source-cache ingress.

`js_code_cache.cpp` stores immutable MVP code images through InputManager leases
(**D8.5.1v2**). `JsPreambleState` carries the MVP code owner and host bookkeeping;
legacy declaration snapshots, inherited name tables and source-buffer ownership
have been removed. Cached entry execution requires an MVP code image. Each
receiving realm reconstructs its own metadata and live cells.

DOM inline handlers compile into the document's retained MVP realm and install
ordinary Function values in EventTarget slots. They use Runtime ownership rather
than the retired deferred-context list.

## 6. Source map and qualification

| Source | Responsibility |
|---|---|
| `lambda/js/js_execution.cpp` | Backend selection, public APIs, host/module adapters and phase timing. |
| `lambda/js/js_compile_recovery.cpp` | Shared frontend/source interruption cleanup. |
| `lambda/js/js_module_loader.cpp` | Module paths, import maps and shared import loading. |
| `lambda/js/js_code_cache.cpp` | Immutable code images and InputManager leases. |
| `lambda/js/mvp-lmd/mvp_lmd_mir.cpp` | Formal MIR admission, planning, lowering, linking and execution owners. |
| `lambda/js/js_interp.cpp` | Explicit AST/AUTO execution and interpreted dynamic source. |
| `lambda/js/js_source_module.cpp` | Realm-owned source-module graph and namespaces. |

Design and source-size history: [JS MVP design](../../../vibe/jube/JS_MVP_Lmd.md).
Retirement evidence and outstanding qualification:
[implementation record](../../../vibe/impl/JS_MVP_Lmd_Legacy_MIR_Retirement.md).
