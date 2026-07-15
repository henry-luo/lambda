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

The same failure recurred in `graphviz/normalize.ls` for a block-bodied
`engine_diagnostics(source, state)` helper following expression-bodied rank
constraint helpers. The compiler reported `E100` at the module's first comment
instead of at that function. Rewriting only the helper as an expression-bodied
wrapper plus helper function compiled, confirming that neither its calls nor
its types caused the rejection.

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

## Dynamic map spread becomes a nested null-key map

While implementing Graphviz normalization, spreading a map returned by
`map(array)` inside another map literal did not flatten its fields:

```lambda
let dynamic = map(["shape", "box"])
{*:dynamic, id: "a"}
```

The result retained `id`, but inspection showed `dynamic` as a nested map under
a null key. Spreading the same map as an element's attribute set also loses its
fields. Iterating the dynamic map works, and building one flat key/value array
before calling `map(...)` works in isolation, but the resulting dynamic map
still cannot cross an element-spread boundary. Map spread should have the same
flattening semantics for statically shaped and dynamically constructed maps, or
report that the operand cannot be spread.

## Resolved: retained Radiant layout crashes during document teardown

While verifying Graphviz Stage 3, `make test-lambda-baseline` reproducibly
segfaulted in `lambda.exe test-batch` when a sub-batch began with
`test/lambda/radiant_custom_layout_bfc.ls`. Running the failed tranche alone
reported the same process-level crash before the first script result:

```text
Segmentation fault: 11  ./lambda.exe test-batch --no-log --timeout=60
Script not found in batch results: test/lambda/radiant_custom_layout_bfc.ls
```

The crash was not caused by the retained callback or runtime reset. The
`radiant.layout()` module function created a stack-local `UiContext`, built a
view tree whose font handles borrowed allocations from that context, then
destroyed the context while retaining the document. `radiant.free()` later
dereferenced those dangling font handles in `view_teardown_visit_node()`.
Release optimization made the use-after-free deterministic.

The module now attaches one reusable headless `UiContext` to the document's
native resource list. The context remains alive through repeated reflow passes
and is destroyed after the view tree during document teardown. Repeated
`radiant.layout()` calls also use Radiant's reflow path instead of replacing and
leaking the existing `ViewTree` shell. The release binary and retained
`test-batch` both pass the BFC and flow custom-layout fixtures.

The harness still reports every later script in a crashed sub-batch as a
separate missing-result failure. It should preserve one process-crash diagnostic
for the unexecuted entries instead of presenting them as unrelated missing
scripts.

## Recursive function parameters are overwritten after descent

While sanitizing nested Graphviz HTML labels, a recursive function read element
attributes after recursively processing its children. The attributes were
present before recursion, but resolved as `null` afterward under MIR Direct:

```lambda
fn walk(value) {
  let children = [for (child in value) walk(child)];
  <td align: value.align; for (child in children) child>
}
```

Capturing `value.align` in a local before the recursive calls did not help: the
local was also overwritten, including when descent happened through a second
helper and when the field was passed as another helper argument. This suggests
recursive re-entry reuses or overwrites caller parameter and local slots instead
of keeping each frame's bindings alive. The JIT should preserve function frames
across recursion. Graph rich-label sanitization currently retains safe table
structure but omits cell alignment and span attributes until this is fixed.

## Resolved: retained custom layout did not switch the MIR runtime global

The Graphviz render fixture initially produced an SVG while logging repeated
`group_by_keys: invalid rows/keys/aliases/runtime` errors and missing node
placements. The retained Radiant callback installed a temporary
`EvalContext` in the thread-local `context`, but MIR-generated helpers obtain
their pool through the separate `_lambda_rt` global. The callback therefore
entered Lambda with two different runtime identities.

The callback now saves, switches, and restores `_lambda_rt` together with
`context` and `input_context`. `graphviz/render.ls` covers grouping inside the
custom layout through final SVG and Graph Scene adaptation.

## Incremental release rebuild can produce a runtime that returns null

After `make test-lambda-baseline` rebuilt a marked release tree incrementally,
the restored `lambda.exe` returned only `null` for ordinary scripts, including
`test/lambda/expr.ls`, at MIR optimization levels 0, 1, and 2. The preserved
debug executable and the previously packaged `release/lambda` both executed
the same scripts correctly. Rebuilding through `make build` restored a working
debug executable and allowed the baseline to proceed.

This points to the incremental release/configuration path or a release-only
runtime defect, not the graph package. The release gate should execute a small
known script before backing up or restoring `lambda.exe`, and the optimized
runtime still needs an isolated clean-build reproduction.

## Multiline iterator calls can obscure comprehension parse errors

While implementing Graphviz route intersection checks, a comprehension used a
multiline function call directly as its iterator and added a `where` clause:

```lambda
[for (point in polygon_intersections(start.x, start.y,
  finish.x, finish.y, vertices) where point.scale <= 1.0) point]
```

The parser reported an unclosed `{` at the enclosing function rather than
identifying the ambiguous iterator boundary. Assigning the function result to a
local moved the diagnostic but the filtered comprehension still failed. A
boolean projection consumed by `any()` compiles:

```lambda
any([for (point in intersections) point.scale <= 1.0])
```

The grammar or error recovery should either accept the filtered form or report
the `for` iterator as the failure location.
