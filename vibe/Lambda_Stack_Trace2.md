# LambdaJS Stack Trace Migration Proposal

**Author**: Lambda Development Team
**Date**: July 2026
**Status**: Implemented foundation, compatibility follow-ups remain

## Overview

LambdaJS currently keeps a separate dynamic JS call stack for `Error.stack` and
`Error.prepareStackTrace`. That stack is maintained by runtime push/pop calls on
normal function invocation paths:

- `js_runtime_call_frame_push()`
- `js_runtime_call_frame_push_name()`
- `js_runtime_call_frame_pop()`
- `js_runtime_call_frame_pop_guarded()`
- the static `js_runtime_call_stack[]` and filename companion array

This proposal migrates LambdaJS/Node stack traces to reuse the same mechanism
used by the Lambda script runtime: MIR debug metadata plus native frame-pointer
walking at error-capture time.

The major advantage is that Lambda script stack traces have zero overhead during
normal call flow. The runtime does not push or pop stack metadata for every
function call. It builds function-address metadata after MIR linking, then walks
the native/JIT frame chain only when an error stack is needed.

Reference: `vibe/Lambda_Stack_Trace.md`.

## Implementation Update

The first implementation pass is now wired into LambdaJS:

- JS MIR compilation builds a debug-info table after `MIR_link`, mapping MIR
  function addresses back to JS display names.
- `js_runtime_call_frame_push()`, `js_runtime_call_frame_push_name()`, their
  pop helpers, the static `js_runtime_call_stack[]`, and the filename companion
  stack have been removed.
- `js_call_function()` and direct MIR calls no longer push/pop diagnostic call
  frames during normal successful execution.
- `Error.stack`, `Error.prepareStackTrace`, and `Error.captureStackTrace()` now
  use Lambda's shared frame-pointer stack capture path.
- `assert.AssertionError({ stackStartFn })` and
  `assert.AssertionError({ stackStartFunction })` trim through the provided
  function using the same native-stack-backed capture path.

Known remaining compatibility work:

- Dynamic assert source text such as `const wrapper = (fn, value) => fn(value)`
  still needs the separately discussed runtime guard. The current implementation
  keeps the syntax-only assert source fix so normal call performance is not
  traded away again.
- JS debug metadata currently carries display names only. Source filename and
  exact line/column plumbing should be completed next.
- `Error.prepareStackTrace` still receives simplified frame strings. Full
  V8-like CallSite objects remain a later compatibility phase.
- Retained/cached module, `eval`, `vm.Script`, and `new Function` debug-info
  lifetimes need a broader retained-table design before relying on native frames
  across all dynamic-code contexts.

Validation from this pass:

- `make build` passes.
- `test-assert-deep.js` passes in the focused Node official runner.
- `test-assert.js` still fails only on the known deferred dynamic assert source
  case: `fn(value)` does not yet receive original call-expression text.
- The `stackStartFn` / `stackStartFunction` assertions in `test-assert.js` no
  longer fail after `Error.captureStackTrace()` gained constructor trimming.
- `JavaScriptTests/JsFileTest.Run/error_handling` passes under the debug JS
  gtest runner.

## Current Lambda Runtime Design

The Lambda script runtime already has the desired architecture:

1. MIR compilation emits normal JIT functions with frame pointers.
2. After `MIR_link`, `build_debug_info_table()` records native address ranges
   for compiled functions.
3. `EvalContext::debug_info` points to that table.
4. When a runtime error is created, `err_capture_stack_trace()` walks the native
   frame-pointer chain, resolves return addresses through the debug table, and
   builds `StackFrame` records.

Normal function calls pay no diagnostic tax. The cost is only paid when an error
object or runtime error stack is actually captured.

## Current LambdaJS Design

LambdaJS has two stack systems that should be separated:

- `js_eval_source_*`: tracks current eval/vm source text and virtual filenames.
  This is source-context bookkeeping, not general call-stack tracking.
- `js_runtime_call_stack`: tracks dynamic JS function frames for
  `Error.prepareStackTrace`.

The expensive part is `js_runtime_call_stack`.

It is updated in both major call paths:

- `js_call_function()` pushes a `JsFunction*` frame before `js_invoke_fn()` and
  pops it after return.
- Direct MIR calls bypass `js_call_function()`, so
  `js_mir_expression_lowering.cpp` emits `js_runtime_call_frame_push_name()` and
  `js_runtime_call_frame_pop_guarded()` around direct calls.

That means normal successful calls pay runtime overhead even when no error is
created. This is exactly what the Lambda runtime design avoids.

## Proposed Design

Move LambdaJS stack identity from per-call runtime arrays into MIR debug
metadata, then capture JS stack traces from the native/JIT frame chain on
demand.

### 1. Extend MIR debug info for JavaScript

Keep `FuncDebugInfo` as the shared low-level address mapping, but extend the
data carried for JS functions. The metadata should include:

- JS display name, not only MIR internal name.
- Source filename.
- Function start line and column.
- Optional call-site/source-span data if available later.
- A language/runtime tag so the formatter can distinguish Lambda script frames,
  LambdaJS frames, and native runtime frames.
- Function kind flags where useful: ordinary function, method, class
  constructor, async/generator wrapper, generated state-machine helper, native
  optimized numeric version.

LambdaJS already has the information needed at compile time:

- `JsFuncCollected::name`
- `JsFuncCollected::node`
- `JsFuncCollected::func_item`
- `JsFuncCollected::native_func_item`
- tree-sitter locations via `node->base.node`
- function parent relationships via `parent_index`

The migration should register both boxed and native optimized MIR functions:

- `fc->func_item` maps to the JS display name.
- `fc->native_func_item` maps to the same display name, with an internal flag if
  the formatter needs to hide the `_n` helper name.
- generator/async state-machine functions map either to the user function name
  or to hidden/internal frames depending on Node compatibility.
- `js_main` maps to a script/module frame and may be hidden by default.

### 2. Build JS debug info after MIR link

Today the Lambda script runner calls `build_debug_info_table()` after MIR link.
LambdaJS should do the same for JS-compiled modules and store the result in the
active `EvalContext::debug_info`, or in a shared debug-info table reachable from
the JS runtime state.

The preferred shape is to reuse the existing `EvalContext::debug_info` path so
`err_capture_stack_trace()` remains the single native frame walker.

For retained preambles, cached modules, and `vm.Script`/dynamic `Function`
compilation, the debug-info lifetime must follow the MIR context lifetime, just
like the existing retained AST/source buffers.

### 3. Add a JS stack capture wrapper

Add a JS-facing helper that captures native frames and converts them into the
current LambdaJS stack representation:

```c
Item js_capture_error_stack(Item error_obj, Item error_name, Item message);
```

Internally it should:

1. call the shared native stack capture helper against `context->debug_info`;
2. filter frames to JavaScript-visible frames;
3. format the default `Error.stack` string;
4. if `Error.prepareStackTrace` is callable, pass the captured frames to it.

The first implementation can preserve the current simplified behavior by
passing an array of frame strings to `prepareStackTrace`. A later compatibility
phase can replace those strings with proper V8-like CallSite objects.

### 4. Remove per-call push/pop from normal JS calls

Once JS `Error.stack` uses native stack capture, remove the normal-call
instrumentation:

- remove `js_runtime_call_frame_push_name()` emission around direct MIR calls;
- remove `js_runtime_call_frame_pop_guarded()` emission;
- remove `js_runtime_call_frame_push()` and `js_runtime_call_frame_pop()` from
  `js_call_function()` paths;
- remove the sys-function registry entries for the deleted helpers.

This restores the desired property: normal JS calls do not update diagnostic
state.

### 5. Retain source-context bookkeeping separately

Do not remove `js_eval_source_*` as part of this migration.

That stack still has a different job: it supplies virtual filenames, line
offsets, column offsets, and source snippets for `eval`, REPL, `vm.Script`, and
`Function`-created code. It should become an input to debug metadata for dynamic
compilation and a fallback for source-context formatting, not a dynamic call
stack.

## What Still Needs `js_runtime_call_stack`?

After function-name and filename bookkeeping moves into MIR debug metadata, the
remaining uses of `js_runtime_call_stack` are diagnostic only:

- building the current `Error.prepareStackTrace(error, frames)` argument;
- giving dynamic call chains to `Error.stack` for callbacks and REPL-loaded code.

No core JavaScript execution semantics should depend on it.

Other similarly named stacks are separate and should remain:

- `js_eval_source_*`: eval/vm/REPL source context.
- `js_with_stack`: lexical `with` environment capture and restore.
- `js_domain_stack`: Node domain/async error routing.
- async resource queues and captured domain stacks in the event loop.
- pending assert call source text, which is for Node assert message generation,
  not stack-frame tracking.

Therefore the goal should be to delete the entire `js_runtime_call_stack` once
`Error.stack`, `Error.prepareStackTrace`, and `Error.captureStackTrace` can use a
native-stack-backed frame capture path.

## Migration Plan

### Phase 1: Metadata plumbing

- Add JS function debug metadata registration in the JS MIR transpiler.
- Register both `func_item` and `native_func_item` for each collected function.
- Populate filename and source start line/column from tree-sitter nodes.
- Preserve metadata lifetime for retained MIR contexts, modules, preambles, and
  `vm.Script`-compiled functions.

### Phase 2: JS stack capture API

- Add a JS-specific wrapper around the shared frame-pointer walker.
- Filter internal frames such as stack-capture helpers, `js_call_function()`,
  `js_invoke_fn()`, and generated thunks.
- Format default `Error.stack` from captured frames.
- Keep the existing lexical stack string as fallback only when native capture
  has no JS frames.

### Phase 3: `prepareStackTrace` compatibility

- Replace `js_error_prepare_stack_trace()` so it consumes captured native frames
  instead of `js_runtime_call_stack[]`.
- Initially pass the same kind of frame strings the current implementation
  passes.
- Later add V8-like CallSite objects if official Node tests require methods such
  as `getFileName()`, `getLineNumber()`, `getFunctionName()`, or `isConstructor()`.

### Phase 4: `captureStackTrace`

- Replace the current no-op `Error.captureStackTrace()` stub with real capture.
- Support `constructorOpt` by dropping frames above the named constructor when
  metadata can identify it.
- Respect `Error.stackTraceLimit` where practical.

### Phase 5: Delete runtime call-stack instrumentation

- Remove direct-call `js_runtime_call_frame_push_name()` and guarded pop
  emission.
- Remove `js_runtime_call_frame_push()`/`pop()` from `js_call_function()`.
- Remove `js_runtime_call_stack[]`, `js_runtime_call_stack_filename[]`, depth,
  root registration, and helper declarations.
- Remove sys-function registry entries for deleted helpers.

## Risks And Open Points

- The shared `FuncDebugInfo` currently has weak source-location support. The JS
  migration should not stop at function names; it should populate source file
  and start location as part of the same change.
- Inlined or direct MIR calls must still appear as real native frames. If a call
  is optimized into no call at all, it cannot appear in a runtime stack without
  explicit deopt metadata. That is acceptable for now; this migration targets
  actual call frames.
- Native optimized JS helper functions with `_n` names must map back to the JS
  function name, otherwise stack output will leak implementation details.
- Dynamic code (`eval`, `new Function`, `vm.Script`, REPL `.load`) needs
  metadata lifetime checks because source filenames and source buffers may be
  retained separately from ordinary files.
- Async stack traces are out of scope. This proposal replaces synchronous
  dynamic call-stack tracking. Node-style async causality can be added later as
  an async-context feature, not by restoring per-call synchronous push/pop.

## Validation

Functional gates:

- focused Node tests around `Error.stack`, `Error.prepareStackTrace`,
  `Error.captureStackTrace`, REPL pretty custom stacks, and assert stack output;
- existing `test-assert.js` and `test-assert-deep.js`;
- `make test262-baseline`, because assert and error behavior affects both Node
  and js262.

Performance gates:

- release build only;
- compare `make test262-baseline` wall time before and after removal of
  push/pop instrumentation;
- confirm no new non-fully-passing js262 retry-only tests;
- add a focused microbenchmark for deep JS calls that do not throw, to prove the
  normal call path has no stack-trace bookkeeping overhead.

## Decision

Adopt the Lambda runtime mechanism for LambdaJS stack traces:

- stack frame identity is compile-time/JIT metadata;
- stack capture happens only when an error stack is requested;
- normal JS calls do not push or pop diagnostic frames.

This should let us remove `js_runtime_call_frame_push_name()` immediately after
the new capture path is wired, and should make the entire
`js_runtime_call_stack` removable once `Error.prepareStackTrace` and
`Error.captureStackTrace` use captured native frames.
