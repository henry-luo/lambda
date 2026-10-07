# JS MVP Lmd — JavaScript MIR on the untyped Lambda substrate

**Date:** 2026-10-07

**Status:** IMPLEMENTED MVP INCLUDING INTEGER TUNING — user decision
2026-10-07, in worktree `temp/js-mvp-lmd`, branch `codex/js-mvp-lmd`.
Section 9 records integer tuning and its validation. The four design goals
in §1 remain fixed. Appendix A preserves the implementation and measurements
before this phase; §9.6 records the integer subtype's release comparison.

**Implementation destination:** `lambda/js/mvp-lmd/`

**Scope:** existing JS parsing and AST analysis; new MIR lowering and execution
for basic scalars, dense arrays, variable assignment, control flow, and functions.

## 1. Objective and fixed constraints

Execute the current JS subset as efficiently as possible using untyped
Lambda's representation and compilation machinery, with minimal emitted MIR
and minimal new runtime code. Measure these properties independently; sharing
code alone does not establish a performance improvement.

### Governing design goals — USER, 2026-10-07

1. **Keep emitted MIR minimal.** Emit only instructions, checks, conversions,
   roots, wrappers, and function bodies needed by the admitted program. Reuse
   existing analysis to eliminate redundant work before MIR emission.
2. **Keep new helpers and helper code minimal.** Minimize both the number of
   helpers and their total implementation, including internal utility functions,
   adapters, and new shared code. Reuse audited Lambda/lib primitives. A single
   large dispatcher is not an improvement over several small helpers merely
   because it reduces the exported symbol count.
3. **Implement exactly the current scope.** Section 3 is the capability
   boundary. Do not implement, design for, reserve hooks for, or plan future
   expansion. Unsupported features need only recognition and a diagnostic.
4. **Keep the MVP as efficient as possible.** Keep proven scalar computations
   native, avoid unnecessary calls and allocations, and use direct storage and
   call paths where current-scope proofs permit. Validate choices on release
   builds with unchanged source and output oracles.

These goals apply together. Do not reduce MIR by moving every operation into
a C dispatcher, reduce helper code by duplicating long algorithms at every MIR
site, or gain speed by changing JS results. Correct admitted behavior, precise
ownership, and the ban on existing JS runtime helpers remain constraints
(**S1.11**, **D2.4.3**, **D5.3**, **D8.2.6**). If speed and size trade off,
record the measured runtime, emitted-instruction count, and added helper code;
retain extra machinery only when it earns its cost on an in-scope workload.

The requested constraints are:

1. Keep the current JS parser, AST, binding, validation, and applicable analysis.
2. Emit **new MIR** under `lambda/js/mvp-lmd/`; do not invoke the current JS
   lowering, JS interpreter, or the private-value MVP under `lambda/js/mvp/`.
3. Base execution on untyped Lambda's `Item`, native scalar lanes, containers,
   precise roots, memory ownership, and shared MIR emitter.
4. Support JS `var` declaration and assignment, including runtime type changes.
5. Call **no existing JS runtime helper**, directly or transitively.
6. Present every proposed runtime helper and its dependencies for review before
   implementing it. Section 7 is that review inventory.

This is an explicit experimental selector, not a replacement for the current
JS engine. A source outside the admitted subset is reported as unsupported;
there is no fallback into either existing JS execution engine.

### Authority and scope

The [formal semantics](../../doc/Lambda_Formal_Semantics.md) and
[formal design](../../doc/Lambda_Formal_Design.md) govern the proposal:

| Concern | Authority | Application here |
|---|---|---|
| Hosted language semantics | **S1.11**, **D1.1** | Admitted programs retain JS meaning; unsupported capabilities are explicit. |
| Shared substrate and values | **D1.2v2–D1.3v3** | Use host `Item`, heap, execution ownership, and MIR infrastructure. |
| Integer runtime subtype | **D2.2.5**, **S4.1.1–S4.1.2**, **S4.9.1** | Admit Lambda `int` under JS Number inside MVP; retain JS overflow and the existing host numeric FFI contract. |
| Representation versus conversion | **D2.4.1–D2.4.3** | Native representation never substitutes for JS coercion. |
| Returned failures | **D1.4v4**, **D8.4.3v2** | Failures return through every frame and its cleanup. |
| Scalar lifetime and rooting | **D5.2.1v3–D5.2.3**, **D5.3.1–D5.3.5** | Shared companion returns, destination ownership, and precise roots. |
| AST and passes | **D8.2.1–D8.2.6** | Existing indexed AST and pass manager; new lowering consumes resolved facts. |
| Specialization | **D8.3.1v2**, **D8.4.1v2** | Use proven native operations and necessary local guards; no automatic body cloning, feedback vectors, or mutable inline caches. |

The experimental lane and its integer-tuning phase are recorded here. The
new runtime-subtype decision is **D2.2.5**; Lambda language semantics and
full LambdaJS behavior remain governed by their existing rules. The
implementation uses the boundary and ten helpers recorded below. Section 9
adds no runtime helper.

## 2. What “based on untyped Lambda” means

The new lane uses Lambda's physical execution substrate with a JS semantic
profile. It does not translate JavaScript into Lambda source, relabel JS nodes
as Lambda expressions, or pass JS operations to Lambda language builtins.

```mermaid
flowchart TD
    source[JavaScript source] --> front[Existing JS parser, binding, validation, AST analysis]
    front --> admit[MVP admission and representation planning]
    admit --> lower[New mvp-lmd MIR lowering]
    shared[Shared Lambda MirEmitter and value plans] --> lower
    lower --> mir[Host MIR library and native code]
    mir --> core[Lambda Item, heap, roots, scalar ownership]
    mir --> helpers[Reviewed mvp-lmd semantic helpers]
    helpers --> core
```

Untyped source may compile to native integer, double, and Boolean registers;
§9 introduces the integer Number subtype. Unknown or changing values use
`Item`. Preserve JS semantics at every join,
store, call, and return; an inferred Number is a representation opportunity,
not a declaration restricting what a JS variable may later contain.

### Existing assets and limits

- [`js_c_parser.cpp`](../../lambda/js/js_c_parser.cpp) exposes
  `js_transpiler_parse_c()`, which schedules parse/build, bind, validate, and
  index in the existing `CompilerPassManager`.
- [`js_ast.hpp`](../../lambda/js/js_ast.hpp) already aliases many core AST
  layouts. Use its `AstBindingId`/`FunctionId` infrastructure and shared child
  enumeration; do not implement another spelling-based binder.
- [`mir_emitter_shared.hpp`](../../lambda/runtime/mir_emitter_shared.hpp)
  contains numeric opcode plans, `MirValue`/representation work, float
  boxing/unboxing, call/return plans, and rooting infrastructure. These are
  compiler utilities; they are not JS runtime helpers.
- [`lambda-data-runtime.cpp`](../../lambda/runtime/lambda-data-runtime.cpp)
  supplies ordinary `Array` allocation. Its `array_get()` returns Lambda
  `null` out of bounds, so it cannot implement a JS indexed read unchanged.
- [`collection_runtime.cpp`](../../lambda/runtime/collection_runtime.cpp)
  distinguishes verbatim storage from Lambda spreading and capture. Only
  physical storage operations with compatible ownership are reuse candidates.
- [`lib/utf.h`](../../lib/utf.h) provides UTF codecs, WTF-8 encoding, and UTF-16
  length utilities. They avoid copying the JS string runtime; each reused
  operation still needs a contract check, especially for lone surrogates.

The retained frontend supplies the indexed AST, resolved `NameEntry` identities,
strictness and early errors. The MVP also reuses `js_ast_visit_children`,
`js_ast_collect_parameter_facts`, and `js_ast_collect_function_facts`. It does
not instantiate `JsMirTranspiler` or consume old environment/MIR emission
state. Three named MVP passes continue the same `CompilerPassManager` after
frontend indexing (**D8.2.4v2–D8.2.6**): admission/binding collection,
representation planning, and lowering/finalization/linking. New planning
facts live beside the retained AST; the AST's semantic authority is unchanged.
Only the analysis consumed by this scope is retained.

## 3. Current admission boundary

This is the proposal's current concrete interpretation of basic scalars,
dense arrays, control flow, and functions. The new minimization goals do not
silently narrow this scope or weaken its semantics. The right column is an
exclusion list, not an expansion backlog: no corresponding runtime machinery,
adapter, reserved field, or extension interface belongs in this MVP.

| Area | Current scope | Out of scope |
|---|---|---|
| Scalars | `undefined`, `null`, Boolean, Number, String; NaN, infinities, signed zero | BigInt, Symbol, boxed primitive objects |
| Scalar operators | Arithmetic including `%` and `**`; unary `+ - ! ~`; bitwise and shifts; relational, loose and strict equality; `typeof`, `void`; short-circuit `&&`, `||`, `??`; conditional and comma expressions | Object/array/function-to-primitive coercion; `instanceof`, `in`, `delete` |
| Variables | `var`, `let`, `const`; simple identifiers; assignment, compound assignment, logical assignment, prefix/postfix update; changing runtime types | Destructuring; unresolved-name writes creating sloppy globals |
| Arrays | Dense literals, empty arrays, nested arrays, mixed values, indexed reads/writes, append at current length, `.length` reads and shrinking | Holes, sparse writes, named properties other than length, descriptors, prototypes, array constructors and methods |
| Control flow | Blocks, `if`/`else`, `while`, `do`/`while`, classic `for`, `switch` with fallthrough, `break`, `continue`, labels, `return` | `for-in`, `for-of`, generators, async/await, throw/try/catch/finally |
| Functions | Ordinary declarations/expressions and arrows with simple parameters; recursion and mutual recursion; function values, reassignment, indirect calls; access to program bindings | Captured enclosing function or block/loop locals, defaults/rest/spread, observable `this`/`arguments`/`new.target`, constructors, methods/classes |
| Program | One JS script with execution-owned top-level bindings; existing strictness/early-error checks | Modules, imports, eval, Function constructors, `with`, DOM, Node APIs, general global object |

String support includes literal preservation, concatenation, comparison,
primitive conversion, UTF-16 `.length`, and indexed code-unit reads. String
methods are out of scope. This is not an ASCII-only profile.

The ordinary Number globals `NaN` and `Infinity`, and `undefined`, resolve
through the retained binding facts and an explicit minimal global record.
Local lexical shadowing remains valid. The three global constants are
read-only: sloppy writes are ignored and strict writes fail with TypeError.
Global lexical/function declarations conflicting with these restricted names
fail with SyntaxError; function-local declarations may shadow them. This small
record follows [GlobalDeclarationInstantiation](https://tc39.es/ecma262/multipage/ecmascript-language-scripts-and-modules.html#sec-globaldeclarationinstantiation)
under **S1.11**, without implementing a general global object. Do not recognize an intrinsic by spelling at
an arbitrary use site. An undeclared identifier read is a ReferenceError;
`typeof` on an unresolvable name returns `"undefined"`, while a TDZ read still
fails. Sloppy implicit global creation remains an unsupported capability.

Noncapturing functions can still be passed, returned, stored in arrays, and
called through variables. Every evaluated function expression creates a fresh
function identity. A reference to a binding in the program's global scope uses the execution's
program slots. A function referencing a top-level block/loop lexical binding
is rejected as a local capture; there are no per-iteration cells. Mutable local
closures are out of scope; implement neither cells nor snapshot emulation.

Unsupported syntax is diagnosed across the entire retained unit, including
unexecuted function bodies, before execution. Runtime-dependent unsupported
operations return a capability failure when encountered. Such a failure is
distinct from a JS TypeError/ReferenceError/RangeError and makes that execution
ineligible for a correctness or timing success. Already-performed effects are
not replayed or rolled back.

## 4. JS behavior that the shared substrate must preserve

### 4.1 Scalars and operators

Lambda `int` is an internal runtime subtype of JS Number under **D2.2.5**.
Finite integer values can use canonical INT Items throughout variables,
arrays, and calls, and native i64 registers inside proven regions. Other
Numbers use canonical FLOAT Items and binary64 registers. JS still observes
one Number type; mixed INT/FLOAT values retain JS numeric equality and
conversion. The precise domain, widening rules, and implementation work are
the integer-tuning phase in §9. No new Item tag or numeric source syntax is
introduced, and Lambda integer saturation never substitutes for JS arithmetic.

ToInt32 truncates Numbers in the guarded inclusive range ±2⁵³ and keeps the
low 32 bits; other values retain the existing `fmod` path. Remainder by a
positive literal power of two up to 2⁵³ uses a mask only for exact integer
dividends in [1, 2⁵³−1]. Negative values, both zeros, fractions, NaN, infinities,
and larger values retain `fmod`, preserving signed zero and Number rounding
(**S1.11**, **D2.4.3**). The same bounded integer emitter validates array indices
and lengths. Section 9 extends integer reuse without integer loop clones or
new numeric runtime helpers.

Scalar conversion must distinguish null, undefined, Boolean, Number, and String.
For example, `null + 1` is `1`, `undefined + 1` is NaN, and `"2" + 1` is `"21"`.
Guarded numeric entries test for Number; they never coerce arguments to make
a guard pass (**D2.4.3**, **D8.3.1v2**).

JS truthiness is emitted directly from the admitted value domain: zero, NaN,
false, null, undefined, and the empty string are falsy; arrays and functions
are truthy. Lambda's truthiness (**S3.1**) is not reused. Logical operators
return their selected operand, not its Boolean conversion.

Strict equality compares scalar values and array/function identity; it never
uses Lambda structural container equality (**S5.1**, **S9.1.5v2**). Loose
equality implements primitive conversions and null/undefined equivalence.
An operation that would require an excluded object's primitive conversion
reports the capability boundary, rather than guessing a numeric result.

Bitwise lowering performs ECMAScript ToInt32/ToUint32 and masks the shift count.
It must handle NaN, infinities, and out-of-range finite doubles before any
machine conversion. `%` is numeric remainder; `**` needs its JS special cases
around NaN, infinities, negative zero, and negative bases. A raw C cast or
unqualified `pow()` is not a semantic implementation.

These admitted conversions follow
[ECMAScript abstract operations](https://tc39.es/ecma262/multipage/abstract-operations.html#sec-type-conversion)
under **S1.11**; the MVP excludes object conversion protocols explicitly.

### 4.2 Assignment and binding lifetime

The JS binding model remains intact:

- `var` is function/program scoped, instantiated as undefined before the body,
  and is not reinitialized by a later `var x;`. Its initializer executes where
  written. Repeated declarations refer to the binding selected by the binder.
- `let` and `const` retain block scope and TDZ checks. A const binding prevents
  rebinding; it does not freeze an array reached through that binding.
- Parameters are local bindings. Missing arguments become undefined; extra
  arguments are evaluated in order even when the body does not use them.
- Program variables live in execution-owned slots shared by functions from
  that execution. They are not Lambda module-level `var`s (**S9.1.7**).
- Assignment copies a scalar value or an array/function reference. There is
  no Lambda COW capture, snapshot, or inout-parameter rewrite (**S9.1.2–S9.1.4**).

For example, both of these must work without source annotations:

```js
var x;
x = 1;
x = "two";
x = [3];

var a = [1];
var b = a;
b[0] = 7;                 // a[0] is 7
function change(v) { v[0] = 9; }
change(a);                // b[0] is 9
```

The compiler plans a stable carrier at joins and loop headers. If a binding
may hold multiple kinds, its complete generic implementation stores Item.
Proven Number regions keep native registers while their proof holds; they do
not require a second copy of the function or loop. Reassignment never
truncates a value to fit an earlier register choice.

Computed assignments use a reference plan: evaluate receiver and key once,
canonicalize the key in JS order, retain the reference, then evaluate the RHS
and store. Compound assignments additionally load the old value before the
RHS. Calls in the RHS may grow or shrink the same array; therefore retain its
owner and index, not a backing-buffer address. `continue` in a classic `for`
targets the update expression; switch discriminants and case tests preserve
their specified evaluation order.

### 4.3 Dense arrays

Use one array representation: ordinary host `Array` with boxed Item elements.
Do not implement ArrayNum promotion, representation transitions, or a private
heap. Arrays may contain any
admitted value, including other arrays and functions; aliasing and cycles are
legal within this JS execution. The shared collector must trace them precisely.

The admitted invariant is that every index below `length` has an own data
element. A stored undefined is an element, not a hole. All arrays originate in
this lane; foreign containers and prototype mutation cannot enter through its
host boundary. Consequently, generated accesses need no descriptor, prototype,
proxy, sparse-storage, or native-lane dispatch. Retain only receiver-type,
key, bounds, and ownership checks not already discharged by analysis.

| Operation | Behavior |
|---|---|
| `[a, b]` | Evaluate left to right and store each value once; no spreading, null dropping, or COW capture. |
| `a[i]`, `0 <= i < length` | Read its own element. |
| `a[i]`, canonical index at/above length | Return undefined; the admitted environment has no inherited numeric properties. |
| `a[i] = v`, `i < length` | Mutate the existing shared array and return `v`. |
| `a[a.length] = v` | Append one element, grow storage if needed, return `v`. |
| `a[i] = v`, `i > length` | Capability failure: this would create holes. |
| `a.length = n`, valid `n <= length` | Shrink or keep the visible prefix; truncated values cease to be reachable through the array. Assignment evaluates to the RHS. |
| `a.length = n`, valid `n > length` | Capability failure: length growth creates holes. |
| Invalid array length | RangeError, distinguished from unsupported valid growth. |

Numeric `-0` is index zero; the string `"-0"` is not. Canonical index strings
such as `"0"` are admitted; `"01"`, negative indices, and other named keys are
outside this profile. Index `2^32 - 1` is not an array index. Both dot and
computed `"length"` access are supported. Key evaluation/conversion runs once;
objects requiring ToPropertyKey are unsupported. These boundaries follow the
[ECMAScript array algorithms](https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-array-exotic-objects).

Fast stores of immediate Items or stable heap references can emit MIR directly
after ownership/bounds proofs. An out-of-band FLOAT needs destination-owned
scalar storage; growth must rebase that storage correctly. Never store a
number-stack pointer into an array or retain `items` across a MAY_GC call.
Likewise, reading a scalar home must materialize its value into a native
register or independently owned destination before the owner can be mutated,
resized, or collected. `var x = a[0]; a[0] = y;` must not change `x` through a
borrowed pointer to the array's old scalar home.

Length shrink needs no new runtime helper. For the admitted primitive RHS,
emit conversion, finite/integral/range validation, the no-growth check, and
the length store; preserve the original RHS as the assignment result. The
ordinary Array collector traces only the visible prefix, so shrinking does
not require clearing the entire removed range or reallocating the buffer.
Preserve the shared scalar-tail metadata for retained values and overwrite
an appended slot before publishing its new length. This is a property of the
single admitted storage layout, not a general array-resize implementation.

### 4.4 Functions and completions

Emit **one semantic body per source function**. Choose native parameters and
results only when all admitted incoming edges prove their contracts; otherwise
use Item where needed and keep proven local computations native. Do not emit
a boxed/native pair of full bodies by default or clone loops speculatively.

Stable declarations used only as direct callees have a closed incoming domain:
a joint fixed point unions parameter, binding, and return kinds, including
recursive calls, missing arguments, parameter writes, and implicit undefined
returns. Nested functions have separate return summaries. Unresolved recursive
domains widen to the generic domain before representation selection.

A closed function whose parameters and result are each proven Number or
Boolean gets one native entry within the shared operand limit: individual
scalar operands after `Context*` and the execution-owned program pointer,
and a native result with an error companion (**D8.4.2v2**, **D5.2.1v3**).
Section 9 selects a finite integer carrier only where the complete incoming
domain and return analysis justify it; a Number result that can widen uses
double or Item. An indirect boxed call accepts both numeric runtime subtypes.
All other functions keep one boxed `Item* + argc` entry. Functions observed as
values or reassigned retain generic incoming parameters. Direct calls still
use their return summaries where the target is stable. Every argument is
evaluated in source order, including discarded extras; no argument is coerced
to satisfy a native entry. There are no entry adapters or cloned bodies.
Unobservable declaration function allocations are omitted; function expressions
retain fresh identity.

An explicit self-call directly in return position in a native body evaluates
all arguments into snapshots, assigns parameters, and jumps to the body entry.
The re-entry point runs local declaration initialization again while retaining
the activation's precise root/scalar storage. Other calls retain the ordinary
call path; mutual recursion and calls beneath arithmetic are not rewritten.
Each actual function entry retains the shared native-stack limit check.

An indirect call checks the MVP callable ABI, loads its boxed entry, and emits
`MIR_CALL` through that pointer using the same call emitter as direct calls.
There is no new C call-dispatch helper. It calls no `js_call`,
`lambda_dynamic_call`, or old MVP dispatcher. Callability failure goes to the
shared cold error path; arbitrary host function values cannot enter this lane.

Function declaration instantiation uses the existing binding/validation facts.
Do not assume a declaration's target stays constant: JS can assign a different
function to its binding. Direct call eligibility requires a stable target or
an exact identity guard. Capture-free arrows and ordinary functions may share
the physical call path because this scope excludes observation of their
receiver/constructor distinctions. No missing `this` behavior is fabricated.

One normal-return cleanup path restores root and number watermarks. Boxed MIR
returns use **D5.2.1v3**'s companion lane when needed; native returns carry the
required companion error lane. C-reachable wrappers use the existing declared
companion-slot protocol. A pending scalar never enters an array, binding,
argument slot, or persistent table. Resolve/own it at the relevant store.

Runtime semantic errors and capability failures return ERROR-tagged Items
through every caller (**D1.4v4**, **D8.4.3v2**). The MVP terminates on
such a failure because catch/finally is out of scope. It still needs correct cleanup
and a stable error category/site; it does not construct JS Error objects.

## 5. MIR lowering strategy

Preserve the existing AST and semantic-analysis results. Continue the existing
pass manager without a shadow AST:

1. Existing parse/build → bind → validate → index.
2. `mvp-admit`: binding/function collection, reused parameter/function facts,
   whole-unit admission, and capture checks.
3. `mvp-plan`: joint monotone parameter/binding/return inference, dominance
   and observability proofs, native versus Item representation and function ABI.
4. `mvp-lower`: forward declarations, one MIR body per source function, shared
   root/scalar-home insertion, finalization and linking.

The public semantic type remains on its existing AST authority; lowering plans
carry physical choices and guards (**D2.4.1**, **D8.2.5v3**). Under §9, JS Number
can refine to the Lambda integer runtime subtype; Number evidence alone does
not prove integrality or range, and an integer refinement does not select
Lambda's saturating operator semantics (**D2.2.5**). Facts coupled to old
backend storage are converted at an explicit analysis seam, not treated as
portable merely because their fields have similar names.

The desired hot form is native arithmetic over native loop state, with boxing
at real generic boundaries. Mixed-type assignment retains a complete Item
path within the one body. Where an operation needs a runtime tag guard, its
miss performs that operation's admitted conversion or reports the excluded
capability; it never restarts the body or replays prior effects. Do not build
a multi-version planner, deoptimizer, interpreter tier, or feedback system.

Truthiness, primitive tag classification, immediate equality, branches,
assignment, arithmetic on proven Numbers, existing dense-element reads, and
direct calls are compiler emission responsibilities. Do not create one C
runtime call per such operation. Keep branch results native, discard unused function-body expression values,
and use native binding destinations to avoid Item materialization
(**D8.2.6**). A small compiler value record tracks native/boxed kinds and
constant Numbers; §9 extends its facts while reusing existing INT/FLOAT
encodings and introducing no private value format.

### MIR and runtime-code minimization decisions

| Decision | Required emitted form |
|---|---|
| Proven Number arithmetic | Native arithmetic; no coercion helper, Item round trip, or redundant error check. |
| Proven integer Number arithmetic | Native integer arithmetic where range and zero-sign facts suffice; shared local guards only where needed, JS double arithmetic on a miss (§9). |
| Branch-only result | Direct comparison/branch; no materialized boxed Boolean. |
| Assignment | Write the planned destination directly; do not produce an unused temporary or result. Preserve RHS effects. |
| Scalar conversion | Handle constant-time primitive cases locally; use a small string/formatting leaf only for variable-length work. |
| Dense array read | Owner/type proof, necessary key/bounds check, load, scalar ownership if needed. No general property kernel. |
| Dense array creation | Existing `array()` and, when useful, one checked `array_reserve_append_slots()` call; no new allocation wrapper. |
| Array literal | Reserve known capacity once and evaluate/store elements in order; do not grow once per element. |
| Dense array write | Direct store for proven immediate/reference cases; reviewed slow leaf only for growth or scalar-home ownership. |
| Array length shrink | Required conversion/validation and a length store; no resize helper, tail-clearing loop, or buffer copy. |
| Function calls | Native operands/results for proven closed scalar functions; otherwise one boxed entry. Checked indirect MIR call, no C dispatch hop or adapter. |
| Self-tail calls | Snapshot arguments, assign parameters, reinitialize locals, and jump inside the existing native body. |
| Function/loop bodies | One body; no automatic generic/specialized clones. |
| GC roots | Existing liveness/effect-driven root stores at MAY_GC calls; no root frame for a proven zero-root body (**D5.3.1**). |
| Failure checks | Check fallible results once; share cold failure blocks by category/site needs. No error check on an infallible raw result. |

Keep existing analyses only when this table or scope validation consumes their
results. Small compile-time emission helpers can share repeated shapes without
adding runtime calls. Do not introduce a generic operation protocol or virtual
dispatch layer to organize a finite set of current-scope operations.

## 6. Memory ownership and the no-JS-runtime boundary

### Shared host services

Use a fresh ordinary host `EvalContext`, the host heap, existing root/number
stacks, source ownership, and the host MIR lifecycle. The selector branches
before full-JS realm/global/prototype initialization. No second Runtime, event
loop, allocator framework, collector, or root-stack owner is introduced
(**D1.3v3**, **D1.5v2**).

Generated roots and final stores are owned by `MirEmitter`. New native helpers
use `RootFrame` / `Rooted` and audited source/destination ownership. Unknown
calls remain MAY_GC. Wide-capable mutable bindings reserve reusable scalar
homes rather than allocating one per assignment (**D5.2.2v3**, **D5.3**).

One existing execution owner retains the source/AST, compiled code, program
bindings, and literal tables until execution and host result inspection finish.
Reuse its lifetime and root facilities; add no cross-execution callable lease
system or module loader. Program bindings are execution-local slots, never
process globals or baked mutable pointers (**D1.8**, **D5.4**).

### Shared collector and teardown boundaries

The shared collector and finalizer discriminate `FN_ENTRY_ABI_JS_FUNCTION`
before entering JS function trace/compact/destroy callbacks. MVP functions use
the ordinary `Function` layout with `FN_ENTRY_ABI_MVP_LMD`, no closure fields,
and no private `JsFunction` payload (**D1.3v3**, **D5.3.1**). Ordinary Array
teardown enters JS item cleanup only for storage marked as JS-owned; whole-heap
JS side-table cleanup runs only when a JS-runtime context capsule exists.
The MVP creates no such capsule. These small ownership checks cover collection
and teardown without adding a callback registry or guest runtime framework.

### Dependency enforcement

Maintain an explicit import/helper manifest for this lane. It covers generated
MIR imports, new helper dependencies, indirect function targets, and lifecycle
callbacks. Permit existing `js_*` frontend analysis calls only during
compilation; execution, collection, teardown, and error handling must never
enter existing JS runtime helpers or `lambda/js/mvp/`.

Do not call a renamed wrapper around a forbidden helper or copy its body into
this directory. Reuse non-JS physical primitives after audit. If a useful
physical operation exists only as a static in another owner, propose promoting
it to the proper shared module and update both clients; review that change
explicitly. JS-specific existing semantic helpers remain excluded.

Check both the static dependency closure and execution traces in focused tests.
The host binary may still link full LambdaJS for its other selector; absence
of `js_*` symbols from that binary is neither required nor a meaningful test.

## 7. Runtime helper inventory

The implementation uses exactly the **10 helpers** listed in §7.1, with the
reviewed semantic and ownership responsibilities. There is no array-creation,
array-resize, or C call-dispatch wrapper. `mvp_lmd_runtime.h` declares the ten
entries; the compiler's concrete import table records argument/result,
allocation, rooting, and exception contracts. Helpers return a resolved Item
or ERROR; raw numeric/ordering leaves are audited NO_GC.

Before adding a helper, identify the in-scope operation that needs it, the
existing primitives searched, the smallest missing semantic/ownership work,
and its effect on emitted MIR and call frequency. A forwarding wrapper needs
a concrete ABI, ownership, or completion adaptation; naming consistency is
not a reason to add one. Internal helper functions and new shared utility
code count toward the same helper-code budget. Factor genuinely shared work;
do not collapse unrelated operations into a mode-switched dispatcher.

`Item` results below are success-or-ERROR completions. Raw results are allowed
only after an infallible, transitive NO_GC audit. All other calls are MAY_GC;
side outputs are consumed only after checking successful completion.

### 7.1 New semantic/runtime entry points

| Helper | Purpose / result | Why generated MIR alone is insufficient | Dependencies and admission |
|---|---|---|---|
| `mvp_lmd_fail(kind, site) -> Item` | Capability, TypeError, ReferenceError, RangeError, and execution diagnostics | Error allocation and source ownership | Shared `err_create_heap`/ERROR carrier; no JS Error factory. MAY_GC. |
| `mvp_lmd_string_to_number(String*) -> double` | String arm of primitive ToNumber | Variable-length numeric grammar and whitespace handling | `lib/str` conversion behind JS grammar checks; invalid numeric text returns NaN. Other primitive arms stay in MIR. Infallible NO_GC; no Item boxing in this leaf. |
| `mvp_lmd_number_to_string(double) -> Item` | Number arm of primitive ToString | Formatting and string allocation | Shared finite-double formatter/string allocation; only JS Number spelling is new. Boolean/null/undefined use literal strings in MIR; String is already its own result. MAY_GC. |
| `mvp_lmd_string_concat(Item, Item) -> Item` | Concatenate already-converted strings | Precise roots, immutable result, and completion adaptation | Reuse the audited `fn_strcat`/string-freeze physical path; do not copy its allocation/growth/copy algorithm or invoke Lambda coercion. MAY_GC. |
| `mvp_lmd_string_compare(Item, Item) -> int64` | UTF-16 code-unit ordering/equality | Variable-length scan over shared WTF-8 storage | Shared codecs; equal code-unit sequences must compare equal even if byte encodings differ. NO_GC. |
| `mvp_lmd_string_at(Item, uint32 index) -> Item` | One UTF-16 code unit or undefined | Allocating the one-unit string, including lone surrogates | Shared UTF/WTF-8 primitives and string allocation. MAY_GC. |
| `mvp_lmd_number_pow(double, double) -> double` | JS Number exponentiation | IEEE/ECMAScript special-case policy around the math primitive | Platform `pow` behind explicit JS rules. Infallible NO_GC. |
| `mvp_lmd_string_key(String*) -> int64` | String-key classification only | Canonical decimal-index scan | Shared byte/number primitives; return index, `-1` for length, or `-2` for an excluded key. These are internal machine results, never JS values. Numeric keys are classified in MIR; the caller constructs any capability failure. Infallible NO_GC. |
| `mvp_lmd_array_store(Item owner, uint32 index, Item value) -> Item` | Overwrite or append and return the assigned value | Growth and destination-owned out-of-band scalars | Audited shared reserve/store primitives; no COW, splice, or capture. Unsupported gaps fail before mutating length. MAY_GC. |
| `mvp_lmd_function_new(code_id, execution_owner) -> Item` | New callable with identity and compiled-entry metadata | Managed lifetime and source/code ownership | Shared heap/callable layout plus the neutral GC seam in §6; no JsFunction allocation. MAY_GC. |

The current scope determines every helper's argument domain; it does not need
cases for unsupported JS types. Emit proven primitive conversions and numeric
keys locally, and call the string leaves only for their actual variable-length
work. Dynamic `+` selects numeric MIR or primitive-to-string and concatenation;
it does not call existing `js_add`, Lambda `fn_add`, or a new opcode dispatcher.
Do not inline a complete string parser/comparison loop at each call site to
make the helper inventory shorter.

The callable uses the existing shared Function layout. The array store checks
`array_reserve_append_slots()` and reuses `lambda_item_adopt_scalar_home()`;
its tail home for each index is reused across overwrites. Buffer growth uses
the shared rebasing path. No unchecked mutator reports successful growth.

### 7.2 Shared non-JS runtime dependencies

| Candidate family | Intended reuse | Check before admitting it |
|---|---|---|
| `heap_calloc`, `heap_data_alloc`, `heap_strcpy` | Host-managed objects, buffers, strings | Exact root coverage, checked size/allocation outcome, callback closure |
| `array()`, `array_reserve_append_slots()`, and audited array store leaves | Dense physical allocation/growth/storage | No COW/capture/spreading; FLOAT home ownership; explicit failure handling; no JS callback path. `array_set`/`array_push_verbatim` are candidates only where their complete preconditions hold. |
| `fn_strcat` / `fn_string_freeze` | Existing string allocation/copy and immutable publication | Already-converted strings only; rooted arguments/results, overflow/error-sentinel handling, and no mutation visible through an alias. No Lambda `fn_string` coercion. |
| `flt2it`, `scalar_storage_read`, and shared scalar-home/return transport | Canonical Number storage and owned reads | Preserve negative zero/NaN; keep number-home lifetime and companion ABI correct; never retain a mutable owner's scalar-home pointer |
| Side-stack/root-frame primitives from `side_stack.h` | Exact generated/native roots and cleanup | Reuse through shared emitter/root facilities, not a private root implementation |
| `err_create_heap` and error-site support | Explicit ERROR completion | No pending-error channel or JS Error construction; safe allocation failure |
| `lambda_finite_double_to_shortest` | Already-classified finite Number formatting | New JS wrapper owns nonfinite and formatting policy |
| `str_to_double`, UTF/WTF-8 codecs, `utf8_to_utf16_length` | Primitive parsing and string operations | No assumption that generic parsing or byte length already implements JS |
| `fmod`, `pow`, `trunc` and necessary C/lib byte primitives | Infallible numeric/physical leaves | Domain behavior, defined conversion, signed zero, and transitive NO_GC |
| Shared stack-budget and execution-owner primitives | Recursion limits, literals/program slots, source/code lifetime for this run | No new module loader, cross-execution lease system, JS realm initialization, callback dispatch, or hidden runtime helper |

The generated import manifest must enumerate the concrete symbols selected
from these families. This table does not whitelist an entire module. MIR
construction/link APIs, AST utilities, and `lib` containers used only while
compiling belong to a separate compile-time dependency manifest.

### 7.3 Work implemented without new runtime helpers

Emit constants, scalar tags, truthiness, nullish checks, `typeof` dispatch,
TDZ/const guards, strict equality, primitive loose-equality dispatch, Boolean
operators, numeric arithmetic/comparisons, bitwise conversion, assignments,
control flow, direct/indirect calls, dense in-range reads, array length loads,
and validated length shrink in MIR. Call existing array allocation/reserve
primitives directly. Existing string length primitives serve the UTF-16 scan.

At every error branch call only the reviewed error constructor. At every
allocation/call use the shared root machinery. Do not add `mvp_lmd_add`,
`mvp_lmd_if`, or a generic opcode interpreter merely to reduce emitter code.

## 8. Acceptance and evaluation

The MVP's compatibility population is defined by §3, not by whichever tests
happen to pass. Select matching existing JS fixtures and Test262 cases before
timing; record pass, failure, static rejection, and runtime capability failure
separately. Do not modify `test_js_test262_gtest` or weaken its oracles to admit
this profile. Full Test262 conformance is outside the MVP's scope.

Required behavior checks include:

- var hoisting/redeclaration, TDZ, const assignment versus array mutation,
  program slots, and type-changing assignment at branches and loop backedges;
- `-0`, NaN, infinities, zero division, shift wrapping, primitive coercion,
  string code-unit equality/indexing, embedded NULs and lone surrogates;
- array aliases through parameters/returns, nested/cyclic arrays, mixed
  elements, boundary keys, dense append, length shrink, and rejected holes;
- receiver/key/RHS evaluation order, including RHS calls that resize the
  assignment's array, prefix/postfix result differences, and short circuiting;
- recursion, mutual recursion, missing/extra arguments, function reassignment,
  expression identity, indirect calls, and function values held by arrays;
- labeled loop/switch exits, `for` continue/update behavior, and single cleanup
  on every normal/error return;
- forced/poisoned GC during nested calls, string conversion, array growth,
  scalar-home escape, and callable finalization, with zero existing-JS-runtime
  entries including trace/compact/destroy callbacks.

Use a host test adapter to invoke a selected function and inspect its Item
result while its execution owner remains live. It is test infrastructure, not
a silently injected JS builtin. Node/full LambdaJS receive the same admitted
source and argument values; preserve Number bit-sensitive checks and identity
checks rather than comparing only printed output.

Benchmark release builds only (`make release`), with instrumentation disabled
for timing. Compare this lane with full JS MIR and Node using identical JS
source, input, loop counts, and result oracles. Report workload execution and
end-to-end startup/compile time separately, alongside memory, MIR size, and
new versus removed source lines. Untyped Lambda ports are secondary references
whose algorithm/layout differences must be disclosed.

For each changed operation, inspect the emitted MIR and record instruction,
call, branch, conversion, and root-store counts; note body/adapter count and
hot-loop helper calls. Count new helper implementation across this directory
and shared owners, including internal functions and adapters. Count semantic
code reductions, never removed comments/blank lines or compressed formatting.
Do not use fewer exported helper names as a proxy for less helper code.

An added guard, wrapper, allocation, helper, or code copy must explain which
current-scope obligation or measured bottleneck requires it. Remove redundant
work shown by those checks. A speed gain must pass correctness/forced-GC gates;
a size reduction must not hide extra runtime dispatch or an unexplained release
regression. Keep performance instrumentation in the test/diagnostic path and
disabled during timing, not as a new runtime feedback mechanism.

Begin with scalar arithmetic/recursion, dense array traversal and append,
mixed-assignment controls, and small string operations. Publish all preselected
rows, including unsupported/failed ones. No Node parity, percentage gain, or
63-workload coverage target is asserted before a baseline exists. This lane
initially adds code; genuine consolidation is measured only when shared
extractions replace existing duplicated physical implementations.

Because the planned extraction and GC seams affect shared infrastructure,
implementation acceptance includes `make test-lambda-baseline` and the
unchanged full-JS Test262 baseline, in addition to MVP-specific tests. Add any
new Lambda `*.ls` regression with its corresponding expected `*.txt`.

## 9. Integer-tuning phase

**Design accepted and implemented:** USER, 2026-10-07.
This phase tunes the scalar, assignment, dense-array, control-flow, and
function features already admitted by §3. Its governing rule is **D2.2.5**;
all four minimization/efficiency goals in §1 apply. The phase adds Lambda
`int` as a JS runtime subtype, including at boxed storage and call boundaries.
Integer registers alone, followed by unconditional FLOAT boxing at every
boundary, do not complete this phase.

### 9.1 Runtime subtype and observable behavior

The runtime/type-planning relation is **Lambda `int` ⊑ JS Number**. Reuse the
existing Lambda type and encodings; introduce neither a new TypeId nor a JS
`int` keyword. This is an internal refinement of the retained JS Number
contract (**D2.4.1–D2.4.3**), not a source annotation or a restriction on later
assignment.

| Numeric case | MVP representation and contract |
|---|---|
| Finite integer in ±(2⁵³−1), excluding `-0` | Existing `LMD_TYPE_INT` / INT Item; unboxed finite i64 when proven. Zero in this carrier means JS `+0`. |
| Fractions, `-0`, and finite Numbers outside that band | Existing FLOAT Item / binary64. This includes exactly representable values such as `2⁵³`; leaving the int band is not JS infinity. |
| NaN and ±Infinity | Shared merged-poison values, still canonically FLOAT-tagged at Item boundaries under **D2.2.2**. Native integer fast paths admit only finite band values, never Lambda lane sentinels. |

Lambda's full `int` domain includes merged poison (**S4.1.1**, **S4.2.1**);
the INT tag specifically establishes a finite integer (**D2.2.2**). A FLOAT
tag does not establish fractionality: existing double computations may yield
integral values. Keep each runtime subtype's canonical construction path;
do not add a universal float-to-int normalization pass or inspect every
double result merely to retag it.

`typeof` reports `"number"` for both carriers. Strict/loose equality, ordering,
truthiness, switch matching, string conversion, and numeric array-key/length
validation use the JS numeric domain across both tags. In particular, INT 1
and FLOAT 1 compare strictly equal, ±0 compare equal, and NaN compares unequal
to itself. Raw Item/tag equality cannot decide mixed numeric equality.
ToNumber accepts an INT value without semantic conversion; producing a
double operand from a finite INT is exact. FLOAT-to-INT narrowing requires
an integral, finite, in-band value with no negative zero; truncation is not
an admission test. These requirements follow **S1.11**, **D2.4.3**, and
[ECMAScript Number operations](https://tc39.es/ecma262/multipage/ecmascript-data-types-and-values.html#sec-ecmascript-language-types-number-type).

This is an internal runtime relation. The semantic JS-to-Lambda numeric FFI
continues to export Number as Lambda `float` under **S4.9.1**, even when its
internal Item is INT. Host diagnostic inspection of an execution-owned Item
does not constitute a Lambda-language import. No new guest bridge is added.

### 9.2 Arithmetic and widening

The integer subtype is **not closed under JS operators**. `int + int` may
produce FLOAT; division may produce a fraction; negating zero produces `-0`.
Preserve JS binary64 rounding at each source operation. Never keep an
out-of-band exact integer intermediate through later arithmetic, reassociate
expressions, or apply Lambda's int53 saturation (**S4.1.2**) to a JS result.
The [safe-integer boundary](https://tc39.es/ecma262/multipage/numbers-and-dates.html#sec-number.max_safe_integer)
is an optimization boundary, not a JS overflow boundary (**S1.11**).

| Operation | Integer path and required exit |
|---|---|
| Numeric `+`, `-`, updates | For two finite int53 operands, a machine i64 add/subtract cannot overflow i64. Keep an INT result only if it is in band; otherwise evaluate the double operation from the saved operands. Elide the band check when ranges prove it. Dynamic `+` retains string-concatenation dispatch. |
| `*` | Use integer multiplication when range facts prove a safe product and exclude negative zero. Any dynamic path must detect machine overflow as well as int53 overflow and handle zero sign. Otherwise retain the existing double multiply; never check only an already-wrapped product. |
| `/` | Keep double division unless a nonzero divisor, exact quotient, and correct zero sign are proven. An even, nonnegative in-band integer divided by two may use a 64-bit shift with those facts; general JS `/` never becomes ToInt32 division. |
| `%` | Integer remainder requires finite integral operands and nonzero divisor. A zero result from a negative dividend is FLOAT `-0`; zero divisor follows JS NaN behavior without a hardware division fault. Reuse the existing guarded power-of-two optimization where applicable. |
| Unary `-` | Negate finite nonzero INT directly; zero produces FLOAT `-0`. |
| Comparisons | Compare two finite INTs directly; widen INT exactly for mixed INT/FLOAT comparisons, retaining NaN and signed-zero rules. |
| Bitwise/shifts | Reuse JS 32-bit conversion, truncation, and masked shift-count rules; an INT operand avoids double conversion. Signed and unsigned results both fit int53 and can be INT. Lambda's general integer shift semantics are not substituted. |
| `**` | Retain the existing JS double/power leaf; a general integer exponentiation implementation is not needed for this phase. |

For a runtime-dependent boxed integer addition, the conceptual lowering is:

```text
evaluate left and right once in JS order
if both values are INT:
    sum = machine_i64_add(unbox(left), unbox(right))
    if sum is in int53: result = canonical_int_item(sum)
    else: result = canonical_float_item(double(left) + double(right))
else:
    use the existing admitted JS addition path
```

Branch-local temporaries preserve the original operands; a miss performs
only the pending operation and rejoins the same body. It never reruns source
effects, restarts a loop, invokes another JS engine, or boxes an out-of-band
machine integer through a saturating Lambda constructor. For example,
`9007199254740991 + 1` must become FLOAT `9007199254740992`, and
`(9007199254740992 + 1) - 9007199254740992` must remain zero.

### 9.3 Planning, storage, and functions

Extend the existing `mvp-plan` facts with integer membership, conservative
bounds, and possible negative zero. Do not change the parser or invent a
second semantic AST/type authority. Literal admission uses the Number already
produced by the JS parser, preserving its rounding. In-band integer literals,
lengths, and integer-producing operators can construct INT values directly.
Do not change the string-to-number helper's double ABI just to obtain INTs.

Propagate facts through assignments, branch joins, loop backedges, and stable
direct calls. Account for all writes, missing/extra arguments, implicit
returns, recursion, and calls that can mutate program bindings. Loops and
recursive analysis need a terminating conservative fixed point; a failed
range proof selects the existing Number/Item route. Compiler-side range
arithmetic must itself be checked. No benchmark-name or input-specific rule
is permitted.

Plan one carrier per binding region/join: finite native integer where every
incoming edge proves it, double for native Number regions that may widen,
and Item for dynamic storage. A runtime guard returning INT or FLOAT joins
through a carrier capable of both. Do not route that result back into an
unconditionally integer destination or narrow a later fractional assignment.
Hoist or eliminate guards only with an invariant covering every relevant
write and backedge. Keep profitable integer state across an entire loop;
double-to-int-to-double conversion around each addition is not the objective.

Program slots, mixed locals, dense array elements, generic arguments, and
boxed returns must all preserve INT Items and accept FLOAT Items. Arrays
remain ordinary mixed Item arrays; introduce no ArrayInt/ArrayNum promotion.
Integer stores are immediate, with no number home or GC pointer. FLOAT stores
and escapes retain existing scalar-home ownership, including after INT/FLOAT
overwrites, array growth, and calls (**D5.2**, **D5.3**).

A closed direct function may use native finite integer operands/results only
when its complete facts establish that contract. If its result may widen,
select double or Item for that result and convert at the actual return edge.
Unknown/indirect calls retain the existing boxed ABI and preserve numeric
subtypes. Every function still has one body and one chosen entry; self-tail
reentry preserves the same parameter/local initialization and carrier rules.
Reuse the existing error companion; no new success/error sentinel or adapter
is introduced (**D5.2.1v3**, **D8.4.2v2**).

### 9.4 Reuse and helper budget

**New runtime helpers: zero.** The ten-entry inventory in §7 remains
the complete inventory. Reuse these existing pieces after their contracts
are checked:

- Lambda's INT tag, payload, finite unboxing, and canonical in-band boxing
  (**D2.2.2**); bypass its out-of-band saturation arm by proving/guarding first.
- Existing integer MIR operations, int53 range checks, and conversion emission.
  Promote useful physical emitters from `transpile-mir.cpp` into the shared
  emitter and update both callers; do not copy its static implementations or
  reuse Lambda-specific operator/error policy.
- Existing primitive dispatch shapes for the shared Number test accepting
  INT/FLOAT. Extract repeated classification/conversion shapes before adding
  another per-kind copy; do not add an arithmetic dispatcher runtime helper.
- Existing array/slot storage, roots, and call/return machinery. INT requires
  no allocation; ownership paths must continue to handle FLOAT correctly.
- The existing double string-formatting, string-to-number, `fmod`, and power
  leaves. Convert finite INT exactly at double-only leaf boundaries; add no
  separate integer formatting/parser library or helper family.

Count changes to existing runtime helpers, internal utility code, shared
owners, compiler code, and emitted MIR separately. Zero new exported helpers
does not permit growing unmeasured helper bodies. Any runtime helper found
necessary during implementation must first be added to §7 with its precise
contract and code/dependency cost for the review required by §1.

### 9.5 Implementation order and acceptance

Implement this phase in three steps, keeping the baseline in Appendix A:

1. **Runtime subtype throughout the scope.** Add INT construction and the
   common Number classification; update every primitive consumer, mutable
   slot, dense-array path, and generic/native call boundary. Initially retain
   double arithmetic where needed. Verify actual INT storage and passage
   through calls, not only equivalent printed output.
2. **Integer arithmetic and proofs.** Add the shared checked paths and bounded
   native loop/call planning from §9.2–§9.3. Prefer proven integer regions;
   retain a guarded path only where its generated code and release results
   justify the cost. No function/loop clones, feedback system, deoptimizer,
   or additional language capability.
3. **Correctness, MIR, and release comparison.** Complete all gates below
   before recording the phase as implemented or claiming a speedup.

Behavior tests must exercise both INT and FLOAT representations of equal
Numbers, both sides of ±(2⁵³−1), arithmetic across `2⁵³`, machine-multiply
overflow, fractions, NaN/infinities, and every signed-zero-producing operator.
Use reciprocal checks for zero sign within the admitted scope. Cover mixed
numeric equality/typeof/coercion, bitwise wrapping, keys/lengths, changing
assignment, branch/backedge widening, prefix/postfix snapshots, and exactly-once
operand effects when guards miss. Check intermediate rounding, not just final
mathematical values. Test direct/indirect calls, missing arguments, recursion,
tail reentry, and values stored/returned through aliased dense arrays.

Run focused tests normally and with forced GC plus freed-memory poisoning.
Inspect immediate INT storage and transitions to owned wide FLOAT storage,
and verify no Lambda lane sentinel reaches a slot or return. Run
`make test-lambda-baseline` and the unchanged Test262 baseline after shared
emitter changes; retain all existing failure oracles and timeouts.

Build release and compare the pre-phase MVP executable with the tuned MVP,
LambdaJS, untyped Lambda, and Node using the pinned-MIR procedure in Appendix A.
Use identical benchmark sources, inputs, and correctness oracles and report
median **self-reported execution time** over the rotating five-run comparison;
startup/compile time cannot replace it. Record binary/source hashes and fresh
MIR evidence. Keep the seven current benchmark rows, including unchanged or
regressed rows; `sum`/`diviter` target bounded integer loops and `collatz`
exercises mixed arithmetic/guard costs, without assuming a static bound on
its intermediate values or promising a particular gain.

Also measure small in-scope dense-array and indirect-call workloads that
carry numeric Items, to establish the value of the runtime subtype beyond
register-only loops. Record instructions, branches, conversions, helper calls,
root/home operations, body count, and helper implementation size. Completion
requires passing semantic/ownership gates, demonstrated INT use across storage
and calls, and measured justification for retained tuning machinery. The
implementation and release evidence are recorded in §9.6.

### 9.6 Implementation and release evidence — 2026-10-07

The implementation satisfies **D2.2.5** using existing INT Items, with no new
TypeId or runtime helper. Integer literals, bitwise results, lengths, guarded
arithmetic results, and proven native returns retain INT across program slots,
mixed locals, dense arrays, and generic calls. Primitive Number dispatch
accepts INT/FLOAT, and finite INT-to-double conversion is exact. Existing
FLOAT homes, precise roots, and the error companion remain unchanged
(**D2.4.3**, **D5.2.1v3**, **D5.3**).

Range planning uses a terminating finite-interval fixed point; an unresolved
cycle or continued expansion after sixteen range changes widens to unknown.
Counted-loop proofs require one initializer, one additive write, and a monotone induction
update on every completed iteration. Nested loops, `do`/`while`, bindings
written in the condition, and bodies with `break`/`continue` do not receive this
proof. Endpoint, trip-count, and product calculations use exact i64 arithmetic with checks
before multiplication; rounded compiler-side double arithmetic must not
underestimate an int53 endpoint. Unknown ranges keep the Number/Item route.

Closed native functions use a coherent numeric register region: retain i64
when all native numeric bindings and numeric returns prove finite integer
membership; otherwise keep their numeric bindings in double. This avoids
repeated conversions of invariant integer parameters inside floating-point
loops. Constants are emitted in the selected region's carrier; boxed finite
integer literals use the existing canonical INT constructor at compile time.
Generic bodies retain independently proven local integer bindings. There is
still one body and one entry per source function, with no adapters or clones.

Guarded boxed addition, subtraction, remainder, and comparisons reuse saved
operands and join through Item/Boolean. A miss performs the double operation
once. Multiplication uses i64 only with a complete safe-product/zero-sign proof;
division and power retain double. Collatz receives no assumed intermediate
bound, so its numeric loop remains floating-point. Recursive range growth also
widens; this phase does not turn Fibonacci into an integer worker merely
because the tested input happens to produce a safe result (**S1.11**).

The three shared finite boxing, unboxing, and band-check emitters add 24 lines
to the shared compiler header. Their extraction removes 21 net lines from
Lambda's compiler and preserves its emitted instruction budgets. The MVP
compiler grows from 1,779 to 2,146 lines (**367 net lines**); function-level MIR
counts are recorded below and in the result archive. Runtime helper count and
code remain **10 helpers / 181 physical implementation lines**, including the
function allocator: zero new or enlarged
runtime helpers. Compiler-side emission utilities are not runtime helpers.

All **20 focused test groups** pass normally and with collection forced at
every allocation plus freed-memory poisoning. Tests inspect actual INT/FLOAT
Items in arrays and generic calls; cover widening, negative zero, intermediate
rounding, machine-product overflow, postfix snapshots, exactly-once effects,
implicit undefined returns, and invalidated loop proofs; and assert that a
bounded integer sum emits no double add/subtract. Shared-emitter validation
passes **6,284/6,284 Lambda/input baseline tests**, including the unchanged
MIR-size ratchet, and **40,261/40,261 unchanged full-JS Test262 baseline cases**
with zero regressions. Test262 is a full-JS nonregression check; the focused
suite validates this MVP subset. No harness, oracle, budget, or timeout was
relaxed.

[`integer_mir_20261007.json`](../../test/benchmark/js_mvp_lmd/integer_mir_20261007.json)
archives 225 checked timed samples and 36 fresh hashed MIR artifacts, plus
warm-up frame telemetry, implementation/source/binary hashes, and the complete
before/after static audit. The preserved pre-phase release executable matches
the optimized record in Appendix A. All three Lambda lanes are pinned to
native MIR; Node uses V8. Each engine has an untimed warm-up, followed by five
rotating timed rounds in fresh processes. Times are median **self-reported
execution milliseconds** with the same boundaries as Appendix A; startup and
initial compilation are excluded, and Node's workload-time tiering is included.

The two additional fixtures append 20,000 integer Items and traverse them
50 times, and make 200,000 calls through a function variable while accumulating
their numeric results. JS sources are identical across JS engines; the
unannotated Lambda ports use ordinary Lambda semantics and `push` for dense
append. Collatz's existing Lambda port uses `shr` where JS divides by two.
These remain port comparisons, not isolation of the back end's cost.

| Kernel | MVP before | MVP integer | Speedup | LambdaJS MIR | Untyped Lambda MIR | Node v24.7.0 |
|---|---:|---:|---:|---:|---:|---:|
| r7rs/fib2 | 1.289 | 1.291 | 1.00× | 1.782 | 1.277 | 1.283 |
| r7rs/fibfp2 | 1.275 | 1.282 | 0.99× | 1.672 | 1.888 | 1.356 |
| r7rs/sum2 | 0.677 | 0.252 | 2.69× | 0.656 | 0.255 | 0.737 |
| r7rs/tak2 | 0.117 | 0.117 | 1.00× | 0.341 | 0.122 | 0.312 |
| larceny/diviter | 600.719 | 253.543 | 2.37× | 648.633 | 252.250 | 383.018 |
| larceny/divrec | 0.640 | 0.638 | 1.00× | 15.053 | 1.167 | 7.615 |
| kostya/collatz | 480.511 | 481.496 | 1.00× | 1335.798 | 280.328 | 1180.324 |
| js_mvp_lmd/integer_dense | 5.672 | 3.520 | 1.61× | 19.256 | 13.741 | 3.810 |
| js_mvp_lmd/integer_indirect | 2.083 | 1.482 | 1.41× | 6.498 | 4.929 | 1.168 |

Sum and iterative division improve **2.69×** and **2.37×**, reaching the
untyped Lambda ports' execution times without increasing either kernel's MIR
instruction count. Dense numeric Items improve **1.61×** and indirect calls
**1.41×**. All four workloads beat their pre-phase control in every paired
round. Fib/fibfp, Takeuchi, recursive division, and Collatz are essentially
unchanged. Collatz still trails the integer Lambda port; its JS division and
unproven intermediate bounds remain relevant differences. All samples are
retained without outlier removal; few-percent differences between engines
are not evidence of a general advantage.

Static counts below include cold/error paths. Conversions count native
integer/double MIR conversions. Calls include shared helpers and indirect
user calls; the archive records every target. Root slots/stores and scalar
homes are emitter frame-plan counts, not executed operations.

| MVP body | Instructions before → after | Branches | Conversions | Calls | Root slots/stores | Homes |
|---|---:|---:|---:|---:|---:|---:|
| sum | 29 → 29 | 4 → 4 | 0 → 0 | 0 → 0 | 0/0 → 0/0 | 0 → 0 |
| diviter division | 30 → 30 | 4 → 4 | 0 → 0 | 0 → 0 | 0/0 → 0/0 | 0 → 0 |
| dense Items | 639 → 618 | 209 → 196 | 10 → 11 | 27 → 25 | 3/11 → 3/11 | 7 → 6 |
| indirect callback | 246 → 278 | 83 → 93 | 1 → 3 | 10 → 10 | 2/2 → 2/2 | 3 → 3 |
| indirect workload | 508 → 510 | 167 → 164 | 2 → 5 | 21 → 19 | 4/7 → 4/7 | 7 → 6 |

The integer kernels have no helper call or root/home operations in their
bodies. The guarded callback adds 32 instructions and ten branches while
the complete indirect workload improves 1.41× and loses one scalar home;
that measured tradeoff justifies retaining the local guard. Dense traversal
removes 21 instructions and one home. Fib/fibfp, Takeuchi, and divrec retain
their pre-phase kernel instruction counts; Collatz retains 55 instructions.
All 18 MVP/control modules were checked against their source function count:
one MIR body per source function, plus the generated program entry.

Reproduce with the preserved pre-phase release executable:

```sh
python3 test/benchmark/js_mvp_lmd/compare_existing.py --runs 5 --integer-items \
  --output temp/mvp_lmd_integer/measured/comparison.json \
  --mvp-control temp/mvp_lmd_integer/before.exe \
  --control-record test/benchmark/js_mvp_lmd/optimized_mir_20261007.json
```

Build and validation logs are retained under `temp/mvp_lmd_integer/`.

## 10. Review boundary

The implementation remains opt-in and limited to §3. Its ten helpers, total
helper code, shared ownership changes, and release measurements are reviewable
below. No further runtime helper or language capability is part of this MVP.
The integer runtime subtype is implemented under **D2.2.5** within §9's
proof and fallback limits.

## Appendix A. Implementation and validation

This appendix records the implementations before §9. All JS Numbers in those
archived builds use FLOAT Items at boxed boundaries. Their binaries, validation
totals, and benchmark results are historical; current results are in §9.6.

```text
lambda/js/mvp-lmd/
    mvp_lmd.h                 execution-owned API and MIR dump
    mvp_lmd_mir.cpp            frontend passes, plans, MIR, execution lifecycle
    mvp_lmd_runtime.h          ten helper contracts
    mvp_lmd_runtime.cpp        nine semantic/storage helpers
```

`mvp_lmd_function_new` resides beside its compile-time code table in
`mvp_lmd_mir.cpp`. The public execution handle retains AST, MIR, program slots,
heap, and published result until `mvp_lmd_destroy()`. It requires an unbound
runtime thread and does not expose foreign callables or persistent closures.

```sh
./lambda.exe js --runtime=mvp-lmd script.js
./lambda.exe js --runtime=mvp-lmd -p 'function f(x){return x+1} f(2)'
./lambda.exe js --runtime=mvp-lmd --timing -p 'function f(x){return x+1} f(2)'
make -C build/premake config=debug_native test_js_mvp_lmd_gtest
./test/test_js_mvp_lmd_gtest.exe
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./test/test_js_mvp_lmd_gtest.exe
```

The selector branches before JS realm/document initialization. Build source
sets and the focused test are declared in `build_lambda_config.json`; the
baseline Makefile list includes the test. Release builds include this lane.
The generated MIR/link resolver permits precisely these shared imports in
addition to the ten entries in §7.1:

- `array`, `array_reserve_append_slots`;
- `owned_item_slot_store`, `lambda_item_adopt_scalar_home`;
- `utf8_to_utf16_length`, `fmod`;
- `lambda_side_stack_ensure_for`, `lambda_stack_is_exhausted`.

Shared physical changes are small: promote the existing WTF-8/UTF-16 iterator and comparison
to `lib/utf` and update its old JS callers; promote the existing deferred
argument-root-span emission to the shared emitter; allow pending scalar bits
to land directly in a reusable caller home (**D5.2.1v3**, **D5.3.1**); gate JS
collector/finalizer entry by actual ownership. The shared Array allocator now
checks its allocation before dereferencing it. No vendor source is changed.
Chained labels required fixes in the first-party JS parser and validator so
all immediate labels target the underlying iteration correctly (**S1.11**).

The focused test covers numeric/coercion/UTF-16 edges, mutable and read-only
bindings, TDZ before late program initialization and direct switch-case entry,
dense aliases/cycles/growth, assignment snapshots, control/completion values,
Annex B function publication, recursion/indirect calls,
whole-unit rejection, bounded scalar homes, one body per function, import
isolation, and exclusion of registered JS GC callbacks. Validation on 2026-10-07:

| Gate | Result |
|---|---|
| Focused MVP tests | 14/14 groups pass, normally and with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. |
| `LANG=C LC_ALL=C make test-lambda-baseline` | 6,278/6,278 pass, including 2,104 input-parser cases. The default UTF-8 locale exposes three unchanged REPL tests whose oracles assume an ASCII prompt; all 50 REPL tests pass with the expected locale. |
| `make test262-baseline` | 40,261/40,261 baseline entries fully pass, zero regressions. One earlier Unicode test exceeded the batch timing threshold under overlapping load, passed its isolated retry, and fully passed the final run. No harness, baseline list, timeout, or oracle was changed. |
| Release host / package | `make release` compiled the optimized host; after restoring the checkout's ignored PDF fixtures, `make prepare-release` completed. The release selector also passed a forced-GC script with nested arrays, tiny Number returns, strings, and function values. |

The ten helpers occupy **181 physical implementation lines**: 170 in
`mvp_lmd_runtime.cpp`, including its internal whitespace predicate and file
setup, plus 11 for `mvp_lmd_function_new`. This is a conservative code-size
inventory, not a reduction obtained by deleting blank/comment lines. The
promoted UTF-16 iterator and comparison add no second algorithm: their former
private JS implementations were removed and both clients use `lib/utf`. Shared ownership
checks and compile-time emitter extractions are separately visible in the diff.

The focused MIR dumps compare an otherwise identical addition function reached
by a stable direct call and by a function variable. Counts are pre-JIT,
including prologue, epilogue and cold stack/error paths:

| Function body | Instructions | Calls | Branches | Numeric conversions |
|---|---:|---:|---:|---:|
| `f(x) { return x + 1 }`, direct-only `f(2)` | 128 | 1 | 14 | 0 |
| Same body, observed through `var g=f; g(2)` | 369 | 9 | 72 | 1 |

The one call in the direct-only body is the cold shared side-stack reservation
path. The numeric operation has no semantic helper call. Each dump contains
exactly one program body and one source-function body; no adapters or body
clones. The numeric dump imports no string conversion/concat helper and
allocates no unobservable function object. The generic body preserves String
arguments rather than coercing them to satisfy a numeric entry. The shared
emitter inserts precise roots only at MAY_GC boundaries; the hot numeric loop
has no runtime helper or root-store call.

The initial implementation's recursive scalar audit exposed a minimization gap. Parameter
kinds reach a fixed point, but there are no function-return-kind summaries:
`kind()` treats call expressions as unknown, and `call()` returns a boxed
value with `K_ANY`. Thus even numeric `fib` loses its Number proof at the two
recursive results. Its addition emits tag dispatch, primitive coercion and
cold string-concatenation paths. These string helpers are not called by the
numeric benchmark. Calls also box arguments into a rooted span, unbox them
on entry, resolve companion returns into owned homes, and restore frame
watermarks. Full LambdaJS's numeric worker passes/returns native doubles,
with an error companion, and emits a direct numeric addition. These are
visible code-generation differences, not a CPU-profile attribution.

For both fib/fibfp, the MVP recursive body has 444 pre-JIT MIR instructions
and 190 locals, versus 54 instructions and 29 locals in full LambdaJS's native
worker. Counts exclude declarations and labels, include cold/error paths,
and do not count executed instructions. Full JS also emits its boxed body
and adapter; MVP has only its single body. Both MVP recursive bodies have
the same instruction count and numeric representation: JS integer-looking
and floating-looking literals are both Numbers (**S1.11**). At `n=27`, naive Fibonacci enters its function 635,621
times, magnifying the call-boundary overhead.

That initial implementation had no explicit tail-recursion elimination: a return expression was
evaluated through the ordinary call emitter and shared return cleanup. The
benchmark expression `return fib(n-1) + fib(n-2)` is not tail-recursive;
addition remains after both calls, so ordinary tail-call elimination would
not help these two kernels. Missing return-kind propagation and the boxed
direct-call ABI were the relevant limitations for this comparison
(**D2.4.1–D2.4.3**, **D8.4.2v2**).

The initial release measurement used `test/benchmark/js_mvp_lmd/run.py --runs 3`: one
warm-up, then the median of three fresh processes per kernel/engine, logging
disabled. The host is optimized `release_native` on macOS arm64; Node is
v24.7.0. Every row checks its numeric result. Kernels and inputs are identical;
full JS/Node print through their console boundary while MVP uses its host
script-result printer. Times include process startup, parse/compile and
execution; they are not isolated kernel times.

**Backend correction:** this initial measurement did not force LambdaJS to
MIR. Its LambdaJS column uses the default/AUTO policy and is historical;
it cannot establish performance against MIR-only LambdaJS. The runner now
uses the corrected native-MIR environment pins. The verified self-reported
MIR comparison below supersedes these earlier runtime-performance claims.

| Kernel | MVP-Lmd (ms) | LambdaJS default/AUTO (ms) | Node (ms) | Checked result |
|---|---:|---:|---:|---:|
| numeric | 82.17 | 698.70 | 93.79 | 1249999987500000 |
| dense_array | 79.56 | 250.80 | 32.70 | 781246875000 |
| calls | 26.88 | 53.01 | 28.41 | 676500 |
| strings | 14.49 | 15.40 | 23.65 | 10000000 |

These four small rows support the MVP's feasibility and expose its limits;
they do not establish general Node parity. In particular, dense-array traversal
is slower than Node and the string row is dominated by startup. The lane adds
an implementation alongside full LambdaJS; it does not yet reduce the host's
overall runtime code. The benchmark runner rejects a host newer than its
release-build stamp so a debug rebuild is not timed accidentally.

### Recorded pinned-MIR results — 2026-10-07

The initial four-engine result record is
[`pinned_mir_20261007.json`](../../test/benchmark/js_mvp_lmd/pinned_mir_20261007.json).
MVP-Lmd, LambdaJS and untyped Lambda all execute native MIR JIT code in this
record; Node executes V8, not MIR. The archive preserves all 140 timed samples,
the effective backend flags, release-binary/source hashes and hashes of the
21 warm-up MIR artifacts. Its table below reports self-reported execution
time, excluding process startup, parsing and initial compilation.

The current comparison uses only the runtime's `__TIMING__` report. Run
`python3 test/benchmark/js_mvp_lmd/compare_existing.py --runs 5`; raw samples,
source/binary hashes and timing boundaries are written to
`temp/mvp_lmd_self_timing_mir/comparison.json`. Wall time is retained as diagnostic
evidence and is never substituted for missing execution timing. All 140 timed
results pass their numeric oracle or the original benchmark's PASS check.

**Backend correction, 2026-10-07:** the first execution-time runner set
`JS_EXECUTION_BACKEND=mir`, which this checkout does not read. The effective
selector is `JS_EXEC_BACKEND`; without it, LambdaJS uses default/AUTO admission
and is not pinned to whole-module MIR (**D8.1.3v24**). MVP was native MIR by
construction and Lambda was correctly pinned with `LAMBDA_EXEC_BACKEND=jit`.
The original self-timed artifact under `temp/mvp_lmd_self_timing/` is retained
but its LambdaJS column is superseded by the corrected rerun below.

The corrected runner explicitly sets `JS_EXEC_BACKEND=mir`,
`LAMBDA_EXEC_BACKEND=jit`, `JS_MIR_INTERP=0`, and
`LAMBDA_JS_LARGE_INTERP=0`. This excludes AST/AUTO and MIR interpreter fallback
from the timed execution paths. Each untimed warm-up must produce a fresh
finalized MIR artifact for each of the three Lambda lanes; missing artifacts
fail the run. All 21 artifacts are retained and hashed in the result JSON,
along with the effective backend flags. Timed samples disable logging and
artifact generation. No product or runtime code changed for this correction.

MVP's optional `--timing` reports monotonic elapsed time immediately around
the generated program entry, after parsing, planning, MIR generation/linking,
JIT compilation and execution-owner initialization. It stops before scalar
result publication, printing and teardown. The API accepts an optional
`double* execution_ms`; an untimed execution makes no clock call. The measured
entry includes its own frame/binding initialization and the call to the
benchmark `main()`. The other three lanes use their original benchmark files
and existing timers: `performance.now()` for JS/Node and `clock()` for Lambda.
Those scopes enclose the kernel/workload call and exclude result checking and
printing. Node's lazy compilation/tiering during that workload remains timed.
MVP retains the JS kernel/workload unchanged and replaces only its unsupported
Node timing/output wrapper. No JS clock builtin, generated MIR import, runtime
helper, or language capability was added (**S1.11**); the ten-helper inventory
and helper implementation count remain unchanged.

The same release host runs all Lambda lanes on macOS arm64; Node is v24.7.0.
One warm-up per engine precedes five rotating timed rounds in fresh processes.
Median **self-reported execution milliseconds**, lower is better:

| Existing JS kernel | MVP-Lmd (MIR) | LambdaJS (MIR) | Untyped Lambda port (MIR) | Node v24.7.0 (V8) |
|---|---:|---:|---:|---:|
| r7rs/fib2 | 4.359 | 1.757 | 1.278 | 1.394 |
| r7rs/fibfp2 | 4.442 | 1.772 | 1.929 | 1.407 |
| r7rs/sum2 | 0.683 | 0.665 | 0.255 | 0.827 |
| r7rs/tak2 | 0.720 | 0.341 | 0.123 | 0.366 |
| larceny/diviter | 647.500 | 664.288 | 264.538 | 395.090 |
| larceny/divrec | 13.483 | 15.832 | 1.158 | 7.900 |
| kostya/collatz | 1470.157 | 1446.953 | 292.652 | 1281.655 |

These execution medians supersede the earlier end-to-end comparisons for
claims about runtime performance. Untyped Lambda is faster than MVP on all
seven rows in this batch. MVP is about 2.1–2.5× slower than native LambdaJS
on Fibonacci and Takeuchi; sum/diviter/Collatz are within about 3%, and MVP
is about 1.17× faster on divrec. Node is faster than MVP on six rows; MVP has
the lower median on sum. The Lambda values remain comparisons of
unannotated ports, with the numeric/algorithm differences described below.

The corrected batch is more stable than the first, but still has scheduling
variation. For example, Collatz MVP samples range from 1365.924 to 1482.088 ms,
and Node from 1251.220 to 1404.594 ms. Do not infer a general advantage from
the few-percent differences. All samples are retained without
outlier removal. The new timer's release smoke check and all 14 focused MVP
test groups pass, normally and under forced GC with freed-memory poisoning.

### Optimized pinned-MIR results — 2026-10-07

The current implementation adds joint return inference, single-body native
scalar entries, explicit self-tail-call elimination, zero-storage-frame
elision, and guarded integer operations described in §4. These follow
**D2.4.1–D2.4.3**, **D5.2.1v3**, **D5.3.1–D5.3.2**, and **D8.4.2v2**;
they change no language scope or semantic ruling. Generic entry behavior,
explicit error companions, and precise GC ownership remain intact. Call
effects remain conservatively MAY_GC; numeric bodies eliminate frames because
their live values and results no longer need root slots or scalar homes.

No runtime helper was added or enlarged. The inventory remains ten helpers
and 181 physical implementation lines. The MIR compiler grows by 191 net
lines. The shared call builder now accepts an optional companion result, and
the existing Lambda direct-call emitter uses it too, removing its duplicated
operand assembly: the shared header shrinks by 17 net lines. Array key/length
validation and integer fast paths share one bounded-conversion emitter.

[`optimized_mir_20261007.json`](../../test/benchmark/js_mvp_lmd/optimized_mir_20261007.json)
records 175 checked timed samples: seven workloads, five engines including
the original MVP control, and five rotating rounds after one warm-up each.
The control executable's SHA-256 must match the original release record.
All four Lambda executable/lane combinations produced fresh finalized MIR
artifacts during warm-up, giving 28 hashed artifacts. The same native-MIR
environment pins and execution-timer boundaries as the initial record apply;
Node uses V8. All samples are retained, and both executables and every source
remain unchanged through the run. The original control is the implementation
committed as `c883aaeec`.

Median self-reported execution milliseconds, lower is better:

| Kernel | MVP before | MVP optimized | MVP speedup | LambdaJS MIR | Untyped Lambda MIR | Node v24.7.0 |
|---|---:|---:|---:|---:|---:|---:|
| r7rs/fib2 | 4.046 | 1.280 | 3.16× | 1.726 | 1.259 | 1.320 |
| r7rs/fibfp2 | 4.087 | 1.262 | 3.24× | 1.717 | 1.851 | 1.345 |
| r7rs/sum2 | 0.663 | 0.670 | 0.99× | 0.654 | 0.248 | 0.747 |
| r7rs/tak2 | 0.719 | 0.117 | 6.15× | 0.328 | 0.120 | 0.305 |
| larceny/diviter | 595.939 | 565.507 | 1.05× | 625.221 | 246.181 | 375.933 |
| larceny/divrec | 12.220 | 0.611 | 20.00× | 14.831 | 1.128 | 7.240 |
| kostya/collatz | 1343.617 | 474.156 | 2.83× | 1323.352 | 277.038 | 1167.463 |

The optimized MVP is faster than its control in all five paired rounds for
six kernels. Sum is effectively unchanged (two of five rounds faster), as
expected for a kernel whose existing hot loop already uses native doubles.
The other iterative division loop also remains floating-point; its modest
gain does not imply that the integer-loop gap is closed. Relative to native
LambdaJS, MVP now has lower medians on six rows and is within about 3% on sum.
Its Fibonacci and Takeuchi medians are close to untyped Lambda, while sum,
diviter, and Collatz still trail the integer ports. These remain five-sample
measurements, with the port/Number differences stated above (**S1.11**, **S4.1.1**).

Finalized MIR confirms the intended changes. Counts include cold/error paths
and exclude labels/declarations; they are not executed instruction counts.

| MVP kernel body | Instructions before → after | Locals before → after | Calls in optimized body |
|---|---:|---:|---|
| fib / fibfp | 444 → 39 | 190 → 27 | Two native self-calls; no helper or side-stack reservation |
| sum | 93 → 29 | 50 → 24 | None |
| tak | 909 → 62 | 426 → 46 | Three native self-calls; final self-call becomes a jump |
| diviter division | 113 → 30 | 62 → 25 | None |
| divrec division helper | 213 → 30 | 105 → 23 | None; self-recursion becomes a loop |
| divrec remainder | 171 → 25 | 83 → 19 | None; self-recursion becomes a loop |
| collatz length | 107 → 55 | 61 → 42 | `fmod` remains only on the guarded remainder miss |

Fibonacci gains come from type propagation and the native call boundary;
its recursive addition is not a tail call. Collatz's fast path uses a machine
integer only for the proven remainder operation, then returns to binary64.
No speculative integer loop, body clone, or replacement of JS division with
a truncating 32-bit shift was introduced.

Validation: all 17 focused groups pass normally and with collection forced at
every allocation plus freed-memory poisoning. Tests cover recursive/mutual
return inference, missing/mixed arguments and implicit returns, extra-argument
effects, native error propagation, tiny Numbers and signed zero, tail-call
parameter swaps/local reinitialization/allocation, and guarded-integer misses.
The stack-exhaustion test uses nonterminating non-tail recursion rather than
assuming a host-independent failing depth; entry guards use the shared
recoverable stack budget and unwind headroom. The Lambda baseline passes
6,281/6,281 under `LANG=C LC_ALL=C`; the unchanged Test262 baseline passes
40,261/40,261 with zero regressions. A release forced-GC smoke check also passes.

Reproduce the paired run with the archived original release executable:

```sh
python3 test/benchmark/js_mvp_lmd/compare_existing.py --runs 5 \
  --output temp/mvp_lmd_optimized/comparison.json \
  --mvp-control temp/mvp_lmd_before_opt.exe \
  --control-record test/benchmark/js_mvp_lmd/pinned_mir_20261007.json
```

Omitting the two control options performs the usual four-engine comparison.
Build/validation logs remain under `temp/mvp_opt_*.log`; the archived result
contains the before/after function-level MIR audit.

### Earlier end-to-end measurements (historical)

The earlier surveys compared the seven admitted existing JS kernels with
full LambdaJS, canonical unannotated Lambda ports and then Node. Their raw
artifacts below retain the original process-wall timing definition; the
LambdaJS columns were not correctly pinned to MIR and use default/AUTO.
The current runner instead reports the verified execution comparison above. The original
three-engine survey below preceded the Node follow-up. The JS algorithm, arguments, and iteration counts
remain unchanged; only the Node timing/output wrapper is replaced. Lambda
also retains its kernel and workload, with its timing/PASS wrapper replaced
by printing `benchmark()`. Original benchmark files are unchanged.

The same release executable runs all three lanes on macOS arm64. One warm-up
per engine precedes five timed rounds with rotating engine order. Every run
matches its numeric oracle. These are median **end-to-end milliseconds**,
including startup, compilation, execution, printing, and cleanup:

| Existing JS kernel | MVP-Lmd | LambdaJS | Untyped Lambda port |
|---|---:|---:|---:|
| r7rs/fib2 | 13.75 | 12.72 | 10.59 |
| r7rs/fibfp2 | 13.81 | 12.65 | 11.13 |
| r7rs/sum2 | 9.57 | 14.58 | 12.17 |
| r7rs/tak2 | 12.58 | 12.22 | 10.27 |
| larceny/diviter | 586.43 | 597.32 | 315.15 |
| larceny/divrec | 23.00 | 26.15 | 11.60 |
| kostya/collatz | 1263.13 | 1291.22 | 282.26 |

MVP is about 1.52× faster than LambdaJS on sum and 1.14× on divrec, within
roughly 2–3% on tak/diviter/Collatz, and about 8–9% slower on the two Fibonacci
rows. Untyped Lambda is faster on six of seven rows; MVP wins sum by about
1.27×. Small rows include a substantial startup/compilation floor, so these
ratios do not isolate execution speed. The two engines' few-percent differences
on the longer loops should not be read as a general performance advantage.

“Untyped” here means no source type annotations, not disabled inference.
Lambda's ordinary integer semantics still apply (**S4.1.1**); JS retains its
Number semantics (**S1.11**). In particular, the Lambda Collatz port uses `shr`
where JS divides by two. These source/representation differences prevent
attributing the port ratios solely to the compiler or shared runtime.

Raw samples, generated wrapper sources and source/binary hashes are recorded
under `temp/mvp_lmd_existing/`; `comparison.json` records that the release
binary and all source files were unchanged through the measurement. No new
runtime helper or capability was added for this comparison.

The Node follow-up uses **Node v24.7.0**, the same generated JS source as full
LambdaJS, one warm-up, and five timed rounds rotating all four engines. All
140 timed results match their numeric oracles. Node end-to-end medians are:

| Existing JS kernel | Node (ms) |
|---|---:|
| r7rs/fib2 | 21.90 |
| r7rs/fibfp2 | 22.15 |
| r7rs/sum2 | 22.03 |
| r7rs/tak2 | 20.98 |
| larceny/diviter | 379.83 |
| larceny/divrec | 32.52 |
| kostya/collatz | 1205.60 |

Node startup dominates the four roughly 21–22 ms rows; these are not isolated
kernel comparisons. A separate release benchmark was observed consuming a
CPU during the follow-up. Node diviter samples range from 366.22 to 865.38 ms,
and Collatz from 1186.08 to 3728.16 ms. Those longer-row medians are provisional
under this contention. All samples, including outliers, and the freshly
measured four-engine controls are retained in
`temp/mvp_lmd_existing_node/comparison.json`; they are a separate batch from
the original three-engine table above. No background process was changed.

## Appendix B. Relevant existing evidence

- [JS MVP Runtime](JS_MVP_Runtime.md): the existing private-value MVP is a
  separate experiment; its representation/runtime are not reused here.
- [Untyped Lambda reference audit](../../test/benchmark/js_mvp/untyped_reference_20260929/README.md):
  port workload/layout differences prevent interpreting JS/Lambda ratios as
  predicted compiler speedups.
- [Shared-emitter and guarded-loop evidence](../../test/benchmark/js_mvp/untyped_tuning_20260929/README.md):
  prior measured reuse successes and rejected experiments; new work should
  account for those outcomes rather than repeat them unexamined.
- [Earlier unification investigation](../Lambda_Proposal_JS_Unify_P7.md):
  shared AST tags alone do not establish removable lowering duplication.

## Appendix C. Superseded representation decision

**Superseded 2026-10-07 by USER and D2.2.5; current rule: §4.1 and §9.**

~~All JS Numbers use Lambda's canonical FLOAT Item encoding at boxed
boundaries; native Number computations use binary64. Machine integers
implement indices, guarded ToInt32 conversion, and guarded power-of-two
remainders, but never change the observable Number type or import Lambda
int53 poison/overflow rules.~~

The former rule kept the first implementation's numeric dispatch small.
The accepted phase now also uses canonical INT Items through mutable storage
and calls. The obligations to preserve observable Number behavior and avoid
Lambda saturation remain in force.
