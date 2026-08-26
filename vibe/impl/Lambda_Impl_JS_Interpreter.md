# LambdaJS AST Interpreter — P2/P3 Implementation Record

**Date:** 2026-08-26

**Status:** PARTIALLY IMPLEMENTED — the synchronous P2 core and the admitted
P3 breadth below are implemented; mixed-tier, suspension, and default-policy
phases remain open.

**Design authority:** `doc/Lambda_Formal_Design.md` **D1.3**, **D1.5**,
**D1.7**, **D5.3.2–D5.3.3**, **D6.2.2v2**, **D6.2.3v2**,
**D8.1.3v8**, **D8.2.4**, **D8.4.1v2**, and **D8.4.3v2**. The working
design is `vibe/Lambda_Design_JS_Interpreter.md` P2.

## Delivered boundary

`JS_EXECUTION_BACKEND=ast` parses, binds, indexes, and retains a `JsScript`
before executing its shared AST. `JsScript : Script` is catalogued by the
same `Runtime` that owns Lambda scripts. The interpreter obtains the
runtime's canonical `EvalContext`, allocates in its heap, and prepares the
script's own module-state slab. It therefore shares runtime, context, module
registry, event-loop owner, and GC heap with Lambda without importing Lambda
language semantics (**D1.3**, **D1.7**, **D8.1.3v8**).

AST functions are `JsFunction` objects with an explicit AST body kind. Their
normal calls and construction enter the established `fn->invoke` and
`fn->construct` kernels; AST evaluation is not a parallel call dispatcher
(**D6.2.2v2**). Function closures retain a traced `JsInterpEnv` chain with
mutable cells. This is JavaScript lexical capture by reference, while Lambda
snapshot capture remains confined to its own rule (**D6.2.3v2**).

The P2 walker implements literals, identifiers, unary/binary/logical/
conditional/sequence expressions, simple `var`/`let`/`const` declarations,
blocks, ordinary and arrow functions, calls/constructors, arrays/objects,
property references, assignment/update, `if`/`while`/`do`/`for`,
return/throw/break/continue, and `try`/`catch`/`finally`. Native callbacks
call back through the same dynamic JS function kernel. Function declarations
are instantiated once, `const` and TDZ writes are enforced, and loop-header
lexicals receive a fresh environment before the update so closures preserve
per-iteration identity.

## Admitted P3 surface

The same walker now also executes destructuring/default/rest patterns, array/
object/call spread, synchronous `for-in`/`for-of` with iterator closing,
switch and labels, optional chaining/logical assignment/delete, regex,
templates/tagged templates, and object methods/accessors. `with` uses the
existing object-environment APIs; AST closures created inside it capture the
same traced environment stack as compiled functions.

Classes use the existing class function and prototype kernels: public and
private methods/accessors are installed through the normal property descriptor
path, instance-field initializers are stored in runtime class metadata and
execute at construction time, and static fields/blocks execute with class
`this`. A traced class-private environment retains the evaluated class for
member bodies and nested closures; private keys, brands, private fields, and
private `in` use the shared runtime kernels. Direct eval projects those
retained private pairs through the existing eval-private bridge. Implicit and explicit derived
construction reuse the established class construct capability. `super()` uses
its derived-`this` and live-superclass helpers, then initializes the derived
fields; `super` property references use the same lexical-home-class property
helpers for class/object methods, accessors, statics, and arrows. This is all
within **D8.1.3v8**; it does not create a second JS object, class, iterator,
or call model.

## Lifetime and rejection guarantees

`JsInterpEnv` is a GC-traced raw record: its outer edge and every `Item` cell
are marked. Active native frames hold exact object roots, and all operands
that cross a MAY_GC boundary occupy `RootFrame` or `RootSpan` slots
(**D1.5**, **D5.3.2–D5.3.3**). The focused suite forces collection after a
closure escapes and then calls it again.

Admission occurs after realm setup but before declarations or user code. The
forced AST backend rejects unsupported forms with a normal JavaScript error;
it never silently executes a second MIR copy after observable work
(**D8.1.3v8**, **D8.4.3v2**). The unset backend deliberately remains the
existing whole-script MIR policy.

## Validation

`test_js_script_gtest` covers:

- retained `JsScript` ownership and shared module-state identity;
- mutable closures surviving forced GC;
- explicit backend selection;
- control/property references and structured completions;
- arrow lexical `this`, ordinary construction, and native builtin callbacks;
- declaration identity, per-iteration closure cells, and `const` writes;
- direct/indirect eval with interpreted-cell writeback, eval-created function
  vars, global lexical synchronization, and shared runtime identity.
- ordinary and arrow-lexical `new.target` through the common construct state.
- explicit derived constructors and public `super` calls/references, including
  fields after `super()`, accessors, statics, arrows, and object-method homes.
- materialized runtime `arguments` objects, mapped sloppy parameter aliases,
  strict/non-simple unmapped objects, and escaped arrow lookup after nested
  calls.
- private fields, methods/accessors, static private elements, private `in`,
  direct eval, and nested functions retaining their class-private lexical
  identity.
- synchronous CommonJS `require()` resolution, cache identity, private wrapper
  bindings, module-registry publication, and restoration of the caller slab.

The expanded focused suite additionally covers patterns, iterator loops,
labels, `with` and escaped `with` closures, templates/tagged templates,
spread, optional/logical/delete operations, regex, object accessors, and
class methods/accessors/fields/statics/implicit inheritance and explicit
`super` dispatch.

## Deferred work

The remaining P3–P6 work is intentionally excluded: ES modules,
T0/T1 shared environment ABI and
promotion, heapified async/generator continuations, and default AUTO
selection. Those forms are explicitly rejected by admission rather than
approximated.
