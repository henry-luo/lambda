# LambdaJS — MIR Lowering and Native Helpers

> **Last verified against tree:** 2026-10-17 (source and release-link inspection; runtime qualification is recorded separately).
>
> **Part of the [LambdaJS detailed-design set](JS_00_Overview.md).**
> **Authority:** S1.11, D8.1.3v25, D8.2.3, D8.4.1v2, D8.4.3v2 and D5.3/D5.4.
> **Primary sources:** `lambda/js/mvp-lmd/mvp_lmd_mir.cpp`, its `mvp_lmd_*` runtime files, and `lambda/runtime/mir_emitter_shared.hpp`.

## 1. Compiler boundary

MVP-Lmd supplies the formal JavaScript MIR path (**D8.1.3v25**). The legacy LJS
MIR emitter and its `JsMirTranspiler` state have been removed. Public execution
selection and the retained frontend are described in
[JS_01](JS_01_Compilation_Pipeline.md).

`mvp_lmd_compile_unit` runs admission, representation planning and lowering.
`MvpLmdProgram` owns unit analysis and code; `LmdCompiler` owns the current
function's emission state. Its `MirEmitter` is shared with Lambda. Retained
analysis selects representations and guards; emission consumes those facts
(**D8.2.3**, **D8.4.1v2**).

## 2. Values, operations and storage

`LmdValue` carries a MIR register and its known representation/kinds. Proven
scalar computations use native lanes. Unknown values and language boundaries
use Lambda `Item`. Conversion and representation are distinct operations
(**D2.4.1–D2.4.3**); source assignments can change a binding's kind.

Expression, reference and statement lowering share ordinary Lambda arrays,
maps, shapes and nominal records. Stable layouts permit direct field/element
access; guards retain observable property behavior, coercion order and fallback
for special storage. Shared shapes and JS policy follow **D2.6.6v3**, **D3.4.7**
and **S1.11**. The working design records the admission and optimization details:
[JS MVP design](../../../vibe/jube/JS_MVP_Lmd.md).

## 3. Calls, closures and roots

Known functions can use direct generated calls. Dynamic functions enter their
stored capabilities; MVP, AST and native callables preserve their owning ABI.
MVP closures reuse Lambda Function/environment ownership with mutable JS cells
(**D6.2.2v2**). Scalar result homes and precise frame/root publication use the
shared emitter (**D5.3/D5.4**).

Shared `js_call_entry_compiled_light` / `js_call_entry_compiled_this` entries
remain for interpreter definitions promoted to MVP. Their flags and owner
checks describe a shared compiled body. The removed LJS MIR factories and weak
code-intern table no longer construct values.

## 4. Errors, completion and suspension

Returned Item failures preserve the existing error/completion lane
(**D8.4.3v2**). JS owns evaluation order, throws, catch/finally completion and
strict-reference behavior (**S1.11**). Precise ownership spans native callbacks,
property operations and suspended activations.

MVP generators, async functions and modules use shared suspended-activation,
Promise and source-module services. The legacy MIR state-machine constructors
and its native module carrier have been removed. Shared AST and MVP carrier
bodies retain their own entry functions.

## 5. Dynamic source and TypeScript

`mvp_lmd_dynamic.cpp` adapts live bindings, globals, receivers, calls and foreign
values to the retained AST interpreter. `eval` and Function-family bodies are
interpreted (**D8.1.3v25**); ordinary formal MIR source does not fall back to LJS.

TypeScript admission is a frontend option on the same compiler. Its unshadowed
`type(value)` intrinsic calls the existing `ts_type_info` helper. The option adds
no new semantic runtime helper and no TypeScript-specific check to generated
ordinary JavaScript loops.

## 6. Native helper ownership

The `mvp_lmd_*` support files implement/adapt property and class operations,
arrays, primitive conversions, builtins, regex protocols, iterators, async work,
proxies/reflection, dynamic source and host services. They reuse shared JS and
Lambda algorithms. Common helper reachability includes interpreter and host
bridges; source-file ownership alone does not establish exclusive engine use.

The retirement removes compiler-only literal/array factories, declaration and
eval-environment bridge wrappers, typed-element facades, old function factories
and their import records. Shared algorithms and MVP promotion call entries are
retained. Pre-retirement size estimates and their attribution limits are in
[design §36.1](../../../vibe/jube/JS_MVP_Lmd.md#361-mir-compiler-and-native-helper-source-size).

Build evidence and runtime/performance qualification are recorded separately in
[the retirement implementation record](../../../vibe/impl/JS_MVP_Lmd_Legacy_MIR_Retirement.md).
