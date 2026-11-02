# Lambda Error Handling

Errors are values. A handler recovers locally, postfix `^` forwards an error,
and an error-admitting receiver retains the outcome.

**Authority:** S7.4–S7.7, S11.4.2v3, and S12.1.2v2 in
[Formal Semantics](Lambda_Formal_Semantics.md).

**Implementation status, 2026-11-02:** the error-handling worktree implements
ordinary-error unification, admission-based binding acknowledgement, deferred
initializer/reassignment outcomes, and retirement of `T^E`, alongside the M1
handler changes. Package/demo migration and aggregate qualification are in
progress. Progress and current verification limits are in the
[implementation plan](../vibe/impl/Lambda_Impl_Type_Error_Handling.md).

Related: [Functions](Lambda_Func.md), [Types](Lambda_Type.md),
[Expressions](Lambda_Expr_Stam.md), and [System functions](Lambda_Sys_Func.md).

## The `error` Type

An error is a first-class value carrying `code`, `message`, source coordinates,
and an optional wrapped `source` (S7.4.4). `error` alone names the type;
`error("message")` constructs a value. Test with `value is error`, then inspect
fields such as `value.message` or `value.code`.

## Creating Errors

```lambda
let original = error("invalid input")
let wrapped = error("cannot load document", original);
[original is error, wrapped.message, wrapped.source.message]
```

`error({...})` also accepts the diagnostic record form. Constructing an error
produces a value and continues evaluation. Returning it or using it as a final
result makes it the enclosing callable's outcome, subject to its return contract.

## The `raise` Keyword

`raise err` completes the enclosing callable early. An existing error is
forwarded unchanged; a non-error operand is normalized by `error(...)` first
(S7.4.6v2). In a procedure this has the same error outcome as returning the
normalized error. In a pure function it supplies early completion without
introducing a `return` statement.

```lambda
fn checked(fail: bool) int^ {
    if (fail) raise error("failed")
    else 21
}
checked(true) ^ { ^.message }
```

`raise` is permitted whenever the return contract admits that error, including
an ordinary union. Raising and returning an error have the same admission and
acknowledgement requirements.

<a id="error-return-types-te"></a>

## Outcome Types

S7.4.2v3 uses ordinary unions for particular error outcomes:

| Type | Admitted outcome |
|---|---|
| Successful `T` | Values of `T`. |
| `T^` / `T \| error` | Values of `T` or any ordinary error. |
| `T \| E` | Values of `T` or the specified error type/pattern `E`. |
| `T \| E1 \| E2` | Values of `T` or either specified error alternative. |
| `E`, where `E` classifies errors | Only those error values. |

`T^` has the same meaning in bindings, parameters, and returns. `any` includes
errors. Aliases preserve admission: naming `error` does not create a nominal
error subtype. An ordinary map alternative stays a map.

The typed-caret form `T^E` is rejected; use `T | E` or broad `T^`.
There is no implicit broad ordinary-error allowance in a particular union.
A function must handle, convert, or disclose ordinary errors outside its
contract (S11.4.2v3). Exceptional system faults retain S7.11.
Constrained annotations currently enforce their base only (S11.4.6v3);
full predicate enforcement remains separate. Filtered handlers use the ordinary
runtime type/pattern test to select recovery.

### Error Forms in Parameters and Let Bindings

```lambda
fn risky() int^ => raise error("failed")
fn message(value: any^) string => if (value is error) value.message else "ok"
let outcome: int^ = risky();
message(outcome)
```

Capturing an outcome acknowledges it without proving success or requiring an
immediate inspection. Under S7.5.2v4/S7.7.4v2, initialization and whole-variable
reassignment share the same admission decision:

| Admission | Required behavior |
|---|---|
| Proven compatible | Retain the admitted outcome and continue. |
| Proven incompatible | Static error; handle, propagate, or change the receiving contract. |
| Explicit contract undecidable | Warn, validate at runtime, and bind/store the admitted value or validation error. |

An inferred receiver retains possible errors; inference imprecision alone does
not require a warning. A deferred failure keeps the declared target strict,
while the effective type includes error. Reassignment replaces the old value.
Typed element/field stores retain their own contract.

```lambda
fn opaque(value) => value
let value: int = opaque("bad") // E241 warning; runtime check binds an error
let recovered = value ^ { 42 };
[value is error, recovered]    // [true, 42]
```

A handler on the initializer runs before annotation validation. To recover
that later failure, handle the bound outcome as above (S7.7.7v4).

## Error Handling at Call Sites

| Form | Purpose |
|---|---|
| `let outcome = f()` / `let outcome: T^ = f()` | Retain the success/error outcome. |
| `f() ^ { fallback }` | Recover from an error and preserve non-errors. |
| `f() or^ { fallback }` | Recover from an error or a falsy value. |
| `f() ^E { recovery }` | Handle matching ordinary errors; retain unmatched ones. |
| `f()^` | Forward an error to the enclosing callable; otherwise yield success. |
| `outcome is error` | Classify an error explicitly. |
| `outcome is (error \| null)` | Classify either error or null. |
| `match outcome { ... }` | Branch on the original outcome. |

The design applies one acknowledgement discipline regardless of whether an
error was raised, returned, or produced by validation. An unacknowledged fallible
result cannot be overwritten by a later result (S7.5.5). Retaining a captured
outcome does not require mandatory inspection.

## Error Propagation (`^` Operator)

Postfix `e^` evaluates `e` once. On error it completes the enclosing callable
with that error; otherwise it yields the non-error value (S7.6.3v3). The
receiving return contract must admit the forwarded error.

```lambda
fn step(fail: bool) int^ {
    if (fail) raise error("step failed")
    else 20
}
fn total(fail: bool) int^ => step(fail)^ + 1
total(false) ^ { 0 }
```

## Error Handling (`e ^ { … }`)

A handler evaluates its operand once. Non-errors pass through unchanged,
including `null`, `false`, and empty text. On error, its body runs with `^`
bound to that error; the body's normal result becomes the handler result
(S7.6.1v8). A fresh body error remains an outcome for the actual receiver.
`e ^ { ^ }` preserves the error; it does not soften it or prove success.

```lambda
fn choose_number(fail: bool) int^ {
    if (fail) raise error("number failed")
    else 21
}
let success = choose_number(false) ^ { 0 }
let recovered = choose_number(true) ^ { 0 };
[success, recovered]
```

The handler does not bind `~`; an enclosing pipe, match, constraint, or view
keeps its current-value context. The opening brace belongs on the introducer's
line. The optional success arm has been removed; use `match` for distinct
success/error transformations, evaluating the original operand once.

```lambda
fn choose_number(fail: bool) int^ {
    if (fail) raise error("number failed")
    else 21
}
let outcome: int^ = choose_number(false)
match outcome {
    case error: "failed: " ++ ~.message
    default: "value: " ++ ~
}
```

### Procedure Handlers

Procedure handlers produce the same success/recovery values as pure-function
handlers, including after suspension (S7.6.7v5). They can appear in a binding
or become the final normal result of a procedure.

```lambda
pn value() int { 21 }
pn failed() int^ { raise error("failed") }
pn main() {
    let a = value() ^ { 0 }
    let b = failed() ^ { 7 };
    [a, b]
}
```

An explicit `return` inside the handler exits the enclosing procedure. Ordinary
`raise`, propagation, and permitted loop-control transfers keep their targets.
A final normal result replaces earlier results, including with `null`;
declarations produce no result and leave the previous result intact
(S12.1.2v2). Scope cleanup and native system faults retain their separate rules.

### Combined Error and Falsy Fallback

`e or^ { body }` evaluates its body for errors, `null`, `false`, and `""`.
Zero, NaN, and empty collections pass through. The local `^` is the exact
triggering value, so it is not necessarily an error (S7.6.8).

```lambda
let missing = null or^ { 7 }
let empty_text = "" or^ { "fallback" }
let zero = 0 or^ { 99 };
[missing, empty_text, zero]
```

The operator has ordinary `or` precedence and left association. The body runs
once and its result passes through even when that result is another error or
falsy value. Ordinary `or` only defaults falsy values and preserves errors.

### Filtered Handlers and Chains

A named selector uses the same type/pattern classification as `is` or `match`.
A matching ordinary error runs the body; a non-error or unmatched error passes
through unchanged. Only matched alternatives are discharged (S7.6.9).

```lambda
type First = error that ~.message == "first"
type Second = error that ~.message == "second"
let recovered: any^ = error("first") ^First {
    error("second")
} ^Second {
    7
};
recovered
```

Chains associate left to right. The next handler sees the preceding result,
including a new error made by recovery. Use one `match` when each branch should
classify only the original outcome. The named selector and opening brace stay
on the introducer's line. Reified system-fault filtering remains a separate
design question.

## Compile-Time Enforcement

Declared types are admission contracts (S11.4.1v5). A known error-capable source
cannot enter a clean receiver without recovery or valid narrowing. A declared
return must admit its possible ordinary outcomes; unannotated pure functions
retain their non-error default (S11.4.2v3).

E228 diagnoses an unacknowledged ordinary-error outcome, E208 an incompatible
function return, and E241 deferred local admission. An inferred binding accepts
and retains its complete outcome. A handler never grants acknowledgement to
unrelated calls in its body or scope.

## Error Code Categories

Errors are categorized by numeric code ranges:

| Range | Category | Description |
|-------|----------|-------------|
| **1xx** | Syntax Errors | Lexical and grammatical errors during parsing |
| **2xx** | Semantic Errors | Type checking, compilation errors |
| **3xx** | Runtime Errors | Execution-time failures |
| **4xx** | I/O Errors | File, network, external resource errors |
| **5xx** | Internal Errors | Unexpected internal states |

### Common Error Codes

| Code | Name | Description |
|------|------|-------------|
| 201 | `type_mismatch` | Type incompatibility |
| 202 | `undefined_variable` | Reference to undefined variable |
| 203 | `undefined_function` | Reference to undefined function |
| 206 | `argument_count_mismatch` | Wrong number of arguments |
| 228 | `unhandled_error` | Error from can-raise function not handled |
| 301 | `null_reference` | Null dereference |
| 302 | `index_out_of_bounds` | Array index out of range |
| 304 | `division_by_zero` | Division or modulo by zero |
| 318 | `user_error` | User-defined error via `error()` |
| 319 | `cancelled` | Cooperative task cancellation |
| 320 | `mailbox_full` | A bounded task mailbox has no capacity |
| 401 | `file_not_found` | File does not exist |
| 402 | `file_access_denied` | Permission denied |
| 405 | `network_error` | Network operation failed |
| 407 | `parse_error` | Error parsing input format |
| 411 | `http_error` | HTTP request error |

User-created errors (via `error("message")`) default to code 318 (`user_error`). The full enumeration is `lambda/runtime/lambda-error.h`.

---

## Checking for Errors

`value is error` tests for an error value. `value is (error | null)` tests
either outcome once; `false`, empty text, and zero do not match that union.
A user can declare `type empty = error | null;` as an ordinary alias; no
`empty` keyword or predefined alias exists (S7.5.7).

## Error Truthiness and Defaults

Errors have no Boolean truth value (S3). Ordinary `or` preserves an error;
`or^` is the explicit combined fallback. Use `e ^ { ... }` when only errors
should select recovery, and explicit classification when absence also matters.

## Error Wrapping

`error("context", ^)` inside a handler constructs a fresh error wrapping the
current one. Use `raise error("context", ^)` for early completion, or make the
wrapped error the handler result and retain/forward it under the receiving
contract. The original error stays available through `.source` (S7.4.4).
