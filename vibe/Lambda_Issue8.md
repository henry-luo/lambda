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

The same recovery failure later recurred after a multiline expression-bodied
helper returning an element comprehension. The helper compiled in isolation,
but the following valid `node_element(...) { ... }` function was diagnosed as
missing its body. Inlining the small comprehension removed the error. This
further suggests the reported block function is only where stale expression
parser state becomes visible.

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

## Double-quoted map keys produce cascading syntax errors

While building synthetic Velmt maps, JavaScript-style string keys such as
`{"data-node-id": "a"}` produced a cascade of unrelated errors at following
fields and closing braces. Lambda requires a quoted name for these fields:

```lambda
{attrs: {'data-node-id': "a", 'data-record-port': "west"}}
```

The first diagnostic should explain that double-quoted strings are values, not
map field names, and suggest the single-quoted name syntax.

## Recursive accumulator return inference can change array construction

A tail-recursive collector returned `reverse(result)` at its base case and
prepended strings during descent:

```lambda
fn collect(values, i, result: [string]) => if (i >= len(values)) reverse(result)
  else collect(values, i + 1, [values[i], *result])
```

`collect(["west", "east"], 0, [])` returned `["westeast"]`; changing only the
base case to `result` returned `["east", "west"]`. The `reverse(any) -> any`
call appears to feed an incorrect string expectation back into recursive array
construction. The explicit `[string]` parameter annotation does not prevent it.

Attempting the documented `string[]` return annotation form made AST building
dereference a null Tree-sitter node in `build_return_occurrence_type()` instead
of reporting a diagnostic. Recursive return unification must preserve the
accumulator container type, and malformed or unsupported return type syntax
must never crash the compiler.

## A local transformation named `lower` silently shadows string lowering

`graph.transform.content` exports `lower(source, label_format)`. An earlier
helper in that same module used `lower(string(value))`, intending the system
string function. Name resolution instead bound the call to the later local
transformation without any arity or useful type diagnostic. The returned array
then made an enum membership test quietly return false, so valid Graphviz HTML
alignment values disappeared.

Receiver syntax, `string(value).lower()`, selects the intended string method and
fixed the result. Either overload resolution should reject the incompatible
local call, or diagnostics/tooling should make system-function shadowing clear;
the previous behavior looked like a data or GC failure far downstream.

## Conditional Mark attributes can collapse the parser diagnostic to line 1

While composing Graphviz SVG markers, several adjacent Mark attributes used
inline `if` expressions, including a quoted attribute name:

```lambda
<path fill: if (open) "none" else color,
  stroke: if (open) color else null,
  'stroke-width': if (open) 1.2 else null>
```

The compiler reported only `paint.ls:1:1` at the file comment and marked the
whole module as erroneous. Tree-sitter's raw tree contained narrower recovery
errors around the second and third conditional attributes, but those locations
were lost by the normal diagnostic path. Computing the three values in locals
before constructing the element compiled correctly.

The grammar should either accept adjacent conditional attribute values
consistently or reject the first ambiguous attribute at its own location. AST
diagnostics should also prefer the smallest error node instead of the enclosing
file-wide recovery node.

## A map literal immediately after `if` is parsed as a statement block

The compact parser-result form below is visually consistent with a map-valued
conditional but is not accepted:

```lambda
if (valid) {valid: true, values: values}
else {valid: false, values: values}
```

The diagnostic points at `valid:` but does not explain that braces after a
condition select statement-block syntax. Calling a small map-constructor
function avoids the ambiguity. Documentation or a targeted diagnostic should
state how to return a map literal directly from an `if` expression.

## Missing dynamic Velmt attributes can evaluate to errors instead of null

The graph layout adapter used a generic optional-attribute helper that expected
both map-backed and Velmt-backed missing fields to evaluate to `null`. During a
retained Radiant render, newly queried `data-regular` and polygon fields instead
returned error values. The error values passed `value != null`, became truthy or
were sent through numeric conversion, and ordinary Mermaid nodes were routed as
large regular polygons. The runtime also logged indirect `expected a map` and
`unknown range type` messages rather than identifying the missing attribute.

Both semantic HTML source lookup and the Velmt adapter now treat an error from
dynamic indexing as absence and apply the declared fallback. Dynamic
object/element/Velmt indexing should ideally use the same missing-member
contract as maps, or expose a non-throwing attribute lookup primitive for
optional metadata.

## Explicit null Mark attributes fail optional schema type checks

Graphviz annotations gained optional font attributes through a direct Mark
constructor:

```lambda
<annotation 'font-name': optional(known, "font-name"),
  'font-size': optional(known, "font-size")>
```

When the values were `null`, the attributes remained structurally present.
Schema validation then reported that each optional attribute had the wrong
type, even though dynamic reads returned `null`. Building a map containing only
non-null fields and spreading it into the element avoided the errors.

The distinction between an absent attribute and an explicitly null attribute
is useful, but Mark formatting and ordinary dynamic reads make the two cases
look identical. The element literal documentation and schema diagnostic should
make this distinction clear, or the schema API should offer an explicit policy
for treating null optional attributes as absent.

## A comparison after a multiline comprehension is rejected

This valid-looking final expression in a function block failed at `> 0`:

```lambda
len([for (item in styles where contains(allowed, item))
  item]) > 0
```

Assigning the comprehension to `matched` was not sufficient; the final
comparison also had to be parenthesized as `(len(matched) > 0)`. The diagnostic
did identify the comparison, but did not explain why a comparison as a block's
final expression required grouping here. The grammar should accept the
expression consistently or suggest parentheses at the actual ambiguous
boundary.

## Parent-relative module imports are parsed as path expressions

Graph conformance runners in sibling test directories needed one shared module
from their parent directory. The intuitive import below is rejected:

```lambda
import conformance: ..conformance
```

The compiler reports `Unexpected syntax near '..' [path_parent]`. Relative
module names support `.same_directory` and nested descendants, while `..` is
reserved for path/parent expressions; there is no documented parent-relative
module spelling and the diagnostic does not suggest one. The helper had to move
under the absolute `lambda.package.graph` namespace to remain shared. Lambda
should either support parent-relative imports or explicitly diagnose this as an
unsupported module path and point to the available import forms.
## Runtime map attribute spread creates a nested element child

While lowering optional Graphviz annotation fonts, this construction:

```lambda
let attrs = {"font-name": "Helvetica"};
<annotation *:attrs>
```

produced an element containing a `[null nested map]` child instead of a
`font-name` attribute. There was no parse or runtime error, and ordinary
literal attribute spreads make the syntax look valid. Until runtime map
attribute spreading is defined or rejected clearly, element constructors must
spell dynamic optional attributes explicitly.

## Added map lanes fail only in retained custom-layout JIT calls

Adding nullable `shape_width` and `shape_height` fields to graph node maps
worked in direct Lambda calls to `layout.compute()`, but the same module called
through Radiant's retained custom-layout function produced error-valued route
coordinates and invalid placements. Making both fields numeric did not change
the retained failure. No Lambda error identified the record-shape mismatch; the
observable messages were downstream `unknown range type: int, error` and
`CUSTOM_LAYOUT_LAMBDA_PLACEMENT` errors. The graph package currently carries
fixed-shape ratios in its already-established port record instead. The retained
JIT should either support compatible map-shape extension or reject the call at
the actual field boundary.

## Structurizr package syntax errors are reported at the next declaration or file root

Several malformed but plausible constructs in the Structurizr Lambda modules
produced diagnostics far from the cause:

```lambda
[for (block in blocks, child in children(block)) child]
<c4-workspace; ...>
for (view in views) ...
```

Lambda requires nested generators as repeated `for` expressions, quoted Mark
names for hyphenated tags, and non-keyword local names (for example `diagram`
instead of `view`). These errors were variously reported at the next `fn`, at
the closing element `>`, or as an unexpected comment at line 1. A complex
element constructor also masked the useful reserved-name diagnostic for a
local named `element` until the constructor was simplified.

The parser should retain the earliest failing node and report the specific
rule: use `for (...) for (...)`, quote `<'hyphenated-tag'>`, or rename the
reserved binder. In a multi-statement `fn` block, nested `if` branches also
require braces; the diagnostic for that case is clear once earlier recovery
has not consumed the surrounding function.

## Quoted strings are rejected as hyphenated map keys

Structurizr deployment projection needed temporary edge maps carrying source
spans. Bracket access and Mark attributes accept quoted hyphenated names, but
the analogous map literal does not:

```lambda
{"source-start": relation["source-start"]}
```

This reports `Unexpected syntax near '"source-start":'`; changing the key to
the quoted symbol `'source-start'` works. The diagnostic should say that map
keys use names or symbols, or map literals should accept string keys
consistently with bracket access.
