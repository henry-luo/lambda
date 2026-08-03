# LambdaJS dynamic-call adapter spans

## Goal

Retire `Item padded_args[JS_MIR_CONTEXT_CALL_MAX_ARITY]` from the LambdaJS
runtime. Dynamic calls now pass a precisely rooted **adapter span** to the
selected compiled wrapper.

The adapter borrows the caller's generated argument-suffix span whenever that
span already has the required wrapper shape. When adaptation is required, it
uses an exact, dispatcher-owned side-root span. Existing generated scopes
publish only their source-actual extent, so a runtime-resolved target may not
claim a successor slot as an in-place padding tail. No GC-capable `Item` used
for call marshalling relies on the native C++ stack for liveness.

**No adapter storage has a hard-coded capacity.** Every owned span is reserved
with its exact runtime-required cell count. The existing 32 and 16/15 *wrapper
ABI checks* remain separate function-pointer dispatch limits; they must not
become adapter-array sizes or root-frame reservation policies.

## Scope and non-goals

This changes LambdaJS dynamic invocation through `js_invoke_fn_raw` and its
call-entry paths. It covers compiled context-ABI wrappers, the live native
non-context callback ABI, closures, missing arguments, rest parameters,
bound functions, and suspend/resume callers.

It does not change these ABI limits:

- compiled context wrappers: at most 32 declared physical formals;
- native non-context callbacks: at most 16 user operands, or 15 when an
  environment occupies the first `P0` … `P16` operand;
- the generic language call boundary: `Item* args, int argc` remains
  unbounded by these wrapper-formal limits.

Direct, statically proven JS wrapper calls already use individual operands and
do not need an adapter span.

## Current state

For a normal non-suspending MIR call `f(a, b)`, `jm_build_args_array` reserves
exactly two cells in the caller's generated side-root-frame suffix and passes
that address with `argc == 2` to `js_call_function_prerooted_args_into`.
`JsMirArgStackScope::slot_count` is the source actual count, not writable
capacity. The runtime validates only that the exact span is live.

`js_invoke_fn_with_source` first keeps an ordinary caller's source actuals in
an exact root span. The generic dispatcher reuses its own copied argument
suffix as that source span; a MIR caller already supplies its active generated
suffix. `js_invoke_fn_raw` then borrows that source for an already-shaped call,
or creates a separate exact `RootSpan` for missing-formal padding or rest
lowering. The source and wrapper spans must remain separate for rest calls:

```text
source call:       f(x, y, z)
callee:            function f(a, ...rest) { /* observes arguments */ }

arguments source:  [x, y, z], argc = 3
wrapper operands:  [x, [y, z]], argc = 2
```

The adapter design preserves that distinction while moving the wrapper
operands to exact side-root slots.

## Required invariants

1. **Source actuals are immutable for the duration of the invocation.**
   `js_pending_call_args` and `js_pending_call_argc` describe the original
   actual sequence, including surplus arguments. Adapter construction must not
   overwrite that sequence.
2. **Every adapter cell is a precise side-root slot.** Native C++ locals and
   arrays are never the only GC owner of an adapter value.
3. **A borrowed span is used only with a proven extent.** The runtime may not
   write past `argc` merely because the address lies in `side_root_base` …
   `side_root_top`.
4. **Adapter lifetime is exactly nested.** A standalone owned adapter is
   nested inside the dispatcher frame and remains live through the wrapper
   call, then releases before the caller resumes. The caller clears its
   original active argument extent at call-expression completion as it does
   today.
5. **No allocation precedes rooting of unrooted inputs.** Native callers,
   bound-call merges, and generator/async buffers must be copied to exact roots
   before any operation that can allocate.
6. **The selected wrapper sees individual operands from the adapter.** Only
   `arguments`/rest bookkeeping observes the original actual span.
7. **Root-stack restoration is exact on all exits.** Normal return, exception,
   proxy failure, async setup failure, and arity rejection restore
   `Context::side_root_top` to the same value as the current implementation.
8. **No speculative fixed adapter extent exists.** `JsCallAdapterSpan` and
   `RootSpan` take an exact count derived from this invocation. Neither may
   embed an `Item[N]`, a `MAX_ARGS` field, or a fixed pre-reserved tail.

## Implemented runtime model

Introduce an internal `JsCallAdapterSpan` abstraction in the JS runtime. It
must be backed by a small RAII side-root-span primitive, rather than by a C++
array or a type-punned local allocation.

```text
JsCallAdapterSpan
  actual_items, actual_count     // immutable source actuals for arguments
  invoke_items, invoke_count     // contiguous wrapper ABI operands
  ownership mode                 // borrowed or owned
  exact root-span reservation    // absent for pure borrowing
```

`RootSpan` beside `RootFrame` reserves an exact requested runtime count of
contiguous side-root cells. `js_root_span_items` is the one conversion point
that exposes those cells as an `Item` span and asserts the existing
`Item`/root-cell size and alignment contract. Callers never cast native arrays
to a rooted `Item*` span.

The adapter has two modes:

| Mode | Use | Wrapper operands |
|---|---|---|
| Borrowed actual | Non-rest target with enough source actuals. | Original caller suffix (or rooted native-source copy), using only the declared formal prefix. |
| Owned adapter | Missing formal arguments, rest transformation, bound/native/suspend source requiring conversion, or any ambiguous ownership case. | New exact rooted span containing the fixed prefix, padding, and/or rest array. |

The common no-adaptation MIR case remains allocation-free: it passes the
caller suffix directly and avoids both source copying and adapter allocation.

## Call-span provenance and capacity contract

`args_prerooted` continues to prove that an input span is live in the
side-root stack. The implementation deliberately treats it as a **read-only
source** capability: its extent is exactly `argc`, and it is never extended by
the runtime. This is important because a neighbouring generated-frame slot may
belong to another lexical call scope even when it is adjacent in memory.

The dispatcher distinguishes the two required provenance cases without
changing the public `js_call_function_into` ABI:

- a MIR caller supplies the live generated source suffix and borrows it when
  no transformation is needed;
- a native/runtime caller is copied into the generic dispatcher's exact root
  suffix, which is then reused as the immutable source span. Bound merges and
  other native temporary spans are copied by `js_invoke_fn_with_source` before
  adapter construction.

There is intentionally no caller-tail mode in this implementation. A dynamic
callee's required arity is not known at MIR lowering, and reserving a general
tail would be speculative capacity. If a future call lowering can prove both a
specific target and a named tail owned by that one call scope, it may add an
exact tail mode; it must not infer ownership from `side_root_top`.

## Adapter construction

Perform construction only after dynamic-call semantics have selected the final
`JsFunction` and final actual sequence. Proxy traps, bound-function argument
merging, constructor behavior, and special-call routing may replace either the
callee or the actual list before the wrapper ABI is known.

1. Establish exact ownership of the source actuals.

   - For `js_call_function_prerooted_args_into`, borrow the live generated
     suffix after validating its exact extent.
   - For ordinary native/runtime callers, the generic dispatcher copies source
     actuals into its exact root suffix before dispatch work, then reuses that
     suffix as the immutable `arguments` source. Native temporary/bound spans
     entering `js_invoke_fn_with_source` are copied into an exact `RootSpan`.
   - For generator/async suspend paths, use the persistent environment-backed
     source as immutable input; allocate an owned adapter if transformation is
     required.

2. Root the function, receiver, and saved dynamic-dispatch state in the normal
   dispatcher frame. `js_invoke_fn_with_source` additionally roots the selected
   function through source copying and adapter construction. No path grows an
   arbitrary caller frame at runtime. A read-only arity check may precede
   rooting only if it cannot allocate and the callee is already proven live;
   all mutable work occurs after these roots exist.

3. Compute the target wrapper's physical-formal count and ABI limit.

   - Context-ABI wrapper: reject a declared physical count above 32 before
     reservation.
   - Native non-context wrapper: apply the 16/15 operand limit, including its
     closure environment.
   - Rest parameter: the final formal operand is the materialized rest array.

4. Select a mode.

   - Non-rest, `actual_count >= formal_count`: borrow actuals and dispatch the
     first `formal_count` cells.
   - Non-rest, `actual_count < formal_count`: reserve an owned exact adapter
     of `formal_count` cells. Copy
     the fixed prefix from source actuals, fill omissions with `undefined`, and
     use that span for the wrapper.

5. For rest parameters, preserve the source span regardless of capacity.

   - If there are surplus actuals, always use a distinct owned adapter span:
     copy regular operands and place the newly materialized rest array in the
     final adapter cell. This protects the original `arguments` elements.
   - The implementation uses the owned-adapter path for every rest function,
     including an empty rest array, so the immutable source span is never
     modified.
   - Root the newly created rest array in its final adapter cell before any
     later allocation or call.

6. Set `js_pending_call_args` and `js_pending_call_argc` to the immutable
   source actual span/count. Invoke the wrapper with `invoke_items` and
   `invoke_count`.

7. Destroy adapter and dispatcher spans in reverse order on every exit.

## Entry-path integration

### Generic dispatcher

`js_invoke_fn_with_source` establishes an immutable exact-root source span,
then `js_invoke_fn_raw` selects the adapter operand span used by the existing
context and non-context function-pointer dispatch switches. The unconditional
local `padded_args[32]` declaration and all writes to it are removed.

The 32-entry context switch/template family remains unchanged; it reads
`adapter.invoke_items[index]`. The native `P0` … `P16` paths likewise read the
adapter span after their own 16/15 arity validation.

### Specialized call entries

`js_call_entry_ordinary` may keep its no-adaptation fast path when actual and
declared shape already match. Any missing-formal, rest, bound, proxy, async,
or otherwise adapted call must route through the shared adapter builder rather
than duplicating padding logic. The generic and specialized lanes must have one
semantic authority for `arguments` source retention and adapter construction.

### MIR lowering

`JsMirArgStackScope` and `jm_build_args_array` already expose the exact source
extent, and `jm_call_function_into` continues to select the existing
prerooted helper for that live span. No caller-tail capacity is exposed or
reserved for an unresolved dynamic target.

Keep the existing generator/async path separate: it produces persistent
environment storage after suspension and must not claim generated-suffix tail
capacity. It naturally uses borrowed actuals or an owned adapter.

### Native and library callers

Public `js_call_function`/`js_call_function_into` remain source-compatible.
Their non-prerooted values are rooted by the dispatcher, and an owned adapter
is selected when padding or rest conversion is needed. This includes
runtime-library callbacks, Node-like host APIs, Jube/native-interface
trampolines, and bound-function merge paths.

## Implementation record

1. Added `RootSpan`, an RAII exact-contiguous reservation over the canonical
   side-root stack. The zero-size case creates no frame; all nonzero adapter
   spans are sized from the invocation's actual or formal count.
2. Added `JsCallAdapterSpan`. The ordinary matching-shape path borrows its
   source span; missing formals and every rest call receive an owned exact
   span. The rest array is installed in its adapter root before its element
   pushes can allocate.
3. Added `js_invoke_fn_with_source`. It roots the callee and establishes an
   immutable source-actual span before `js_invoke_fn_raw` can allocate an
   adapter. The generic dispatcher reuses its existing exact argument-root
   suffix rather than making a second source copy.
4. Removed the fixed `padded_args[]` buffer. The context 32-formal and native
   16/15-formal limits are now checked before an exact adapter reservation;
   they are dispatch ABI limits only.
5. Kept specialized ordinary entries on their matching-shape fast path.
   `js_entry_needs_generic` already routes missing-formal and rest shapes to
   the generic adapter authority.

## Regression coverage

`test/mir/js/dynamic_adapter_span.js` exercises dynamic callees with one and
all formals missing, an empty rest parameter, and a rest callee with 40 object
actuals. It verifies that `arguments` retains the original count and indexed
actuals while the rest array receives the independently adapted tail. The same
fixture invokes a rest callback through `Array.prototype.map`, covering the
native temporary callback entry. Its MIR sidecar confirms the source arguments
remain a generated root-frame suffix. `test/js/dynamic_adapter_span.js`
requires that fixture and has a `.txt` golden for ordinary JS discovery.

The fixture runs in the MIR emission suite and is automatically included in
the JIT, forced-GC, randomized-GC, and MIR-interpreter corpus stress sweep.
Existing JS baseline coverage continues to cover bound calls, proxy calls,
constructors, async/generator execution, and the native callback interfaces.

## Acceptance criteria

- `rg "padded_args" lambda/js` returns no runtime marshalling buffer.
- No dynamic-call `Item` adapter value depends solely on a native C++ stack
  array for GC reachability.
- `JsCallAdapterSpan` and `RootSpan` contain no fixed argument-capacity
  constant or `Item[N]` storage; every reservation is exact for that
  invocation.
- All existing `arguments`, default-parameter, rest, bound, proxy, async, and
  generator semantics remain unchanged.
- Every side-root frame/spans restores exactly across normal and exceptional
  exits; nested calls do not overwrite caller argument cells.
- Borrowed-caller-suffix mode is used for ordinary no-adaptation dynamic calls;
  every adaptation uses an exact owned span until a future lowering can prove
  an explicit, call-owned tail.
- The 32 context-wrapper and 16/15 native-wrapper ABI boundaries remain checked
  and documented.
