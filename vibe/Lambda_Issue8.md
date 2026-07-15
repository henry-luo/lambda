# Lambda Issues 8

Issues encountered while implementing the Lambda graph Stage 2 package.

## Compile-error path reports tracked allocations as leaks

Running a script with an intentionally unhandled fallible `input()` binding
correctly reports `E228`, but shutdown then reports two live `system`
allocations (166 bytes in the observed run):

```text
error[E228]: error from 'graph' must be handled
memtrack: LEAK - 2 allocations (166 bytes) still live at shutdown
```

Reproduction: bind `input(path, spec)` with plain `let value = ...` and run the
script through the MIR-direct CLI. The diagnostic construction or failed
compilation cleanup path appears to retain these allocations. A rejected
script should release compiler diagnostics and temporary source state before
the runtime leak check.

## Console array formatting does not escape quotes inside strings

Printing an array that contains the Mermaid initialization string
`init: {"flowchart": ...}` emits the inner double quotes verbatim:

```text
["init: {"flowchart": {"curve": "basis"}}"]
```

The result is visually ambiguous and is not valid Lambda/JSON-like notation.
The console formatter should escape embedded quotes and backslashes when a
string is rendered inside a collection.

## Function-body parser diagnostic points at valid syntax

While adding graph obstacle routing, a normal block-bodied function immediately
following an expression-bodied function that returned a map was rejected with:

```text
Function body requires => or {...}
```

The reported function already had a `{...}` body. Reformatting the preceding
map did not change the result; expressing the rejected function itself as
`=> if ...` compiled successfully. This makes a valid-looking function appear
to be missing its body and gives no indication that parser state or recovery
around the preceding expression may be involved. The parser should accept the
block form consistently, or report the token that left the preceding function
expression incomplete.

## Comprehension binding named `list` resolves incorrectly

While adding the Graphviz source-Mark parser fixture, a nested comprehension
using `for (list in values)` produced empty nested results even though `list`
was an element with children. The equivalent code worked after renaming the
binding to `group`:

```lambda
// unexpectedly empty inner arrays, with no diagnostic
[for (list in property_lists) [for (entry in model.element_children(list)) entry]]

// expected result
[for (group in property_lists) [for (entry in model.element_children(group)) entry]]
```

`list` appears to collide with a built-in/type name during nested expression
resolution. The compiler should either treat the lexical binding normally or
reject the reserved name with a clear diagnostic.
