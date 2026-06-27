# Lambda Type: Error

## Status

Current implementation note, updated 2026-06-27.

This document describes how Lambda represents and owns `LMD_TYPE_ERROR` values in the core Lambda runtime, and how that differs from LambdaJS exception handling.

## Summary

Lambda has two related but separate error concepts:

| Concept | Representation | Owner | Purpose |
|---------|----------------|-------|---------|
| Error item | `Item` tagged as `LMD_TYPE_ERROR` | value owner / GC if pointer payload is heap-owned | In-band script/runtime value |
| Structured diagnostic | `LambdaError*` | manual or GC depending on `is_heap` | Message, code, location, stack trace, cause |

The important rule is:

- Errors constructed inside script runtime as error values must be heap-managed: `LambdaError::is_heap = true`, boxed with `err2it(error)`, and traced/finalized by GC.
- Errors constructed outside the script runtime remain manually owned: parser errors, schema/validator diagnostics, CLI/reporting diagnostics, and other host-side diagnostics must continue to use `err_create()`/`err_free()`.

## Runtime Type Representation

`LMD_TYPE_ERROR` is a built-in `TypeId` in `lambda/lambda.h`. There are two valid item shapes:

| Shape | Item bits | Meaning |
|-------|-----------|---------|
| Sentinel | high byte `LMD_TYPE_ERROR`, low 56 bits `0` | Generic error with no attached struct |
| Structured pointer | high byte `LMD_TYPE_ERROR`, low 56 bits `LambdaError*` | Error value with structured payload |

Helpers:

- `ItemError` / `ITEM_ERROR` create the sentinel form.
- `err2it(LambdaError*)` tags a `LambdaError*` as an error item.
- `it2err(Item)` extracts the low 56-bit pointer from a tagged error item.
- `get_type_id(item) == LMD_TYPE_ERROR` remains the common test for both forms.

The GC must decode `LMD_TYPE_ERROR` as a tagged pointer type, not as a raw container pointer. `lib/gc/gc_heap.c:item_to_ptr()` masks the high type byte for error items before checking whether the pointer is a GC object.

## `LambdaError`

`LambdaError` lives in `lambda/lambda-error.h` and carries structured diagnostics:

```c
typedef struct LambdaError {
    LambdaErrorCode code;
    bool is_heap;
    char* message;
    SourceLocation location;
    StackFrame* stack_trace;
    char* help;
    void* details;
    struct LambdaError* cause;
} LambdaError;
```

`is_heap` is the ownership boundary.

| `is_heap` | Allocation | Release path |
|-----------|------------|--------------|
| `false` | `err_create()` / system allocator | `err_free()` manually frees payload and struct |
| `true` | `err_create_heap()` / Lambda GC heap | GC finalizer releases payload; GC frees object storage |

For heap-owned errors, `err_free()` is intentionally a no-op. This prevents a manual free from releasing GC-owned storage. The GC calls `err_gc_destroy()` to release external payload fields such as `message`, `help`, `stack_trace`, and non-heap `cause`.

## Script Runtime Errors

The Lambda system function `error(message)` is registered as `fn_error` in `lambda/sys_func_registry.c`.

At runtime, `fn_error()` does two things:

1. It calls `set_runtime_error()` so the existing CLI/reporting path still has a manually owned `context->last_error`.
2. It creates a heap-owned `LambdaError` with `err_create_heap()`, captures a stack trace, and returns `err2it(error)`.

This keeps old reporting behavior while making the script-visible error value GC-managed.

The runtime installs the heap allocator and error GC hooks in `heap_init()` / `heap_init_with_pool()`:

- `err_set_heap_allocator(heap_calloc)`
- `gc->error_trace = err_gc_trace`
- `gc->error_destroy = err_gc_destroy`

## GC Behavior

Error items participate in GC in three places:

1. Rooting

   MIR locals whose type can hold heap references are registered as GC roots. `lambda/transpile-mir.cpp:should_gc_root_var()` explicitly includes `LMD_TYPE_ERROR`.

2. Marking

   `lib/gc/gc_heap.c:item_to_ptr()` extracts a heap pointer from `LMD_TYPE_ERROR` tagged items. If the low 56 bits are zero, the sentinel marks nothing. If the pointer is outside the GC heap, it is ignored by `is_gc_object()`.

3. Tracing and finalization

   `gc_trace_object()` calls `gc->error_trace` for `LMD_TYPE_ERROR` heap objects. Current tracing marks a heap-owned `cause` chain. `gc_finalize_dead_object()` calls `gc->error_destroy` for dead heap errors so external payload fields are released during sweep.

Manual, outside-heap `LambdaError*` values are not GC objects. They must not rely on GC for lifetime.

## External and Host-Side Errors

Not every `LambdaError` is a script value. Some errors are diagnostics produced outside normal script runtime:

- Parser and AST builder diagnostics use `err_create()` and store `LambdaError*` in transpiler error lists. Examples include `build_ast.cpp:record_type_error()` and `record_semantic_error()`.
- Input parser paths may surface `LMD_TYPE_ERROR` sentinels when conversion or field mapping fails.
- Schema validators may reject an `LMD_TYPE_ERROR` item as invalid data and add validation diagnostics.
- CLI/reporting code uses `context->last_error` / `persistent_last_error` to print structured diagnostics after the runtime context has unwound.

These paths are manually owned unless they explicitly construct a heap error. This is deliberate: parser, input, validation, and reporting diagnostics can outlive or live outside the script heap.

## RetItem and Error Propagation

Native functions that can raise use `Ret*` structs:

```c
typedef struct RetItem { Item value; LambdaError* err; } RetItem;
```

`ri_err(error)` returns `ITEM_ERROR` plus the structured pointer in `.err`. This is a native ABI for can-raise helpers, not the same thing as a boxed script error value.

`item_to_ri(item)` preserves a real `LambdaError*` when an error item carries one. Pointer-less `ItemError` remains represented as a boolean sentinel in `.err`.

## Practical Rules

Use `err_create_heap()` only when creating an error object that is itself returned to, stored by, or reachable from script runtime values.

Use `err_create()` when creating diagnostics for parser, AST, schema, validator, CLI, host integration, or other outside-runtime paths.

Use `err2it()` only for a `LambdaError*` whose lifetime is valid for the item:

- heap error: OK, GC manages it;
- manual error: only safe if some outside owner guarantees the pointer remains valid and will not be freed while the item can be observed.

Do not call `err_free()` to dispose a heap-owned error. Let GC finalize it.

Do not put manually owned diagnostics into long-lived script containers unless their lifetime is explicitly managed elsewhere.

## Current Limitations

- `LambdaError::message`, `help`, and `stack_trace` are still external/system allocations, even for heap-owned errors. The GC finalizer releases those payloads when the heap error dies.
- `details` has no generic finalization policy today. If a future heap error stores owned data in `details`, it must add an ownership rule or type-specific finalizer.
- Parser/validator diagnostics are not script error values. They should not be assumed to be GC-rooted just because they use `LambdaError`.
- `context->last_error` remains a manual reporting path. It is intentionally separate from heap-owned script error values.

## Appendix: LambdaJS Error Handling

LambdaJS does not use `LMD_TYPE_ERROR` as its primary exception model. It implements JavaScript exceptions as JS values plus a pending-exception side channel.

### JS Error Objects

LambdaJS constructs JS `Error` objects as ordinary JS objects, usually maps stamped with `JS_CLASS_ERROR`.

Key constructors in `lambda/js/js_runtime_state.cpp`:

- `js_new_error(message)`
- `js_new_error_with_stack(message, stack)`
- `js_new_error_with_name(name, message)`
- `js_new_error_with_name_stack(name, message, stack)`

These objects typically carry JS properties such as:

- `message`
- `stack`
- prototype linkage to `Error.prototype`, `TypeError.prototype`, `RangeError.prototype`, and similar constructors

They are regular GC-managed JS/Lambda heap objects, not `LambdaError` structs.

### Throw State

LambdaJS uses a pending-exception state in `JsRuntimeState`:

| State | Meaning |
|-------|---------|
| `js_exception_pending` | whether a JS exception is pending |
| `js_exception_value` | the thrown JS value |
| `js_exception_msg_buf` | cached text for reporting/logging |

`js_throw_value(value)` sets the pending flag and stores the thrown value. `js_check_exception()` tests the flag. `js_clear_exception()` clears it and returns the thrown value.

`js_exception_value` is registered as a GC root in `js_runtime_set_input()`, so a pending thrown JS object remains alive while the engine unwinds.

### JIT Propagation

LambdaJS does not use C++ exceptions or `longjmp` for normal JS `throw`. MIR-lowered code emits explicit checks:

- runtime helper throws by calling `js_throw_value(...)`;
- caller checks `js_check_exception()`;
- generated control flow branches to exception/unwind labels;
- `catch`/`finally`, iterator close, promise rejection, and generator paths use `js_clear_exception()` to capture or transform the thrown value.

This model is side-channel exception state plus normal function returns. It is intentionally different from core Lambda's language-level error-as-value model.

### Comparison with Core Lambda

| Aspect | Core Lambda | LambdaJS |
|--------|-------------|----------|
| Language model | Error values and `T^E` return typing | ECMAScript `throw` / `try` / `catch` |
| Primary runtime carrier | `LMD_TYPE_ERROR` item, optionally `LambdaError*` | JS object/value in `js_exception_value` |
| Structured native diagnostic | `LambdaError` | Usually JS `Error` object properties |
| Propagation | In-band returns, `ItemError`, `Ret*`, `raise`, `?` | Pending exception flag checked by generated code |
| GC root | Error item roots / heap error trace | `js_exception_value` root plus ordinary JS object graph |
| Outside-runtime diagnostics | Manual `LambdaError*` | Mostly not applicable; JS parser/early errors are reported by JS pipeline |

LambdaJS can still encounter core Lambda `ItemError` in host/runtime helper boundaries, but JavaScript-observable exceptions are expected to be JS values. A JS `TypeError` is not a core `LMD_TYPE_ERROR`; it is a JS object thrown through the LambdaJS pending-exception machinery.

