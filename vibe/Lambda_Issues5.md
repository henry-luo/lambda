# Lambda Script Issues — Phase 2 PDF Package

## Current Verification Status (2026-05-05)

Quick probes against the current `./lambda.exe` show that several items in
this document have changed since they were first recorded. This was a
spot-check, not a full audit of every issue below.

## 1. List `+` is element-wise add, not concat (silent footgun)

**Status: ✅ No Fix / by design (2026-07-01)** — `+` is intentionally
scalar-broadcast / element-wise arithmetic for arrays and lists. Use `++`
for list/array concatenation.

```lambda
let a = [1, 2]
let b = [3]
print(a + b)         // -> [4, 5]   (broadcast / pad-add)
print(a ++ b)        // -> [1, 2, 3]   (intended append)
```

In a tokenizer loop `ops = ops + [item]` does element-wise addition between
the existing list and a single-element list, returning a list of the same
shape with one element added. The list **never grows**, the loop variable
never advances past the synthetic result of the wrong `r`, and the script
spins forever without an error or warning.

**Resolution**:
- Keep `+` as element-wise arithmetic for arrays/lists.
- Document the rule explicitly: use `++` for list/array concat.

---

## 2. `pn` does not implicitly return an `if/else` expression value

**Status: ✅ Fixed (2026-07-01)** — verified against the current MIR path.
`pn` now returns the last body value for both a whole-body `if/else` and a
final `if/else` that follows procedural statements.

```lambda
pn classify(c) {
    if (c == "/") { read_name(c) }
    else          { read_other(c) }
}
```

A caller `let r = classify("/")` now receives the chosen branch value, matching
the documented rule that a `pn` returns its last expression when no explicit
`return` is used.

The remaining broken case found during verification was a final `if/else`
after an earlier procedural statement:

```lambda
pn classify(c) {
    var seen = 1
    if (c == "/") { seen + 10 }
    else          { seen + 20 }
}
```

That case previously returned `null` because the MIR lowering treated the
final `if/else` as a procedural side-effect statement and discarded boxed
branch values. It now returns the branch value (`11` or `21` in the focused
probe), with coverage in `test/lambda/proc/proc_implicit_if_return.ls`.

---

## 3. `print()` is single-argument; multi-arg call type-errors at runtime

**Status: ✅ Fixed (2026-07-01)** — `print` is variadic. Each argument is
stringified and adjacent arguments are separated by one space.

```lambda
print("x=", x)
// prints: x= 42
```

The default separator is currently a literal space. If Lambda grows a global
runtime print configuration later, that separator is the intended extension
point.

---

## 4. `?` post-fix error propagation does not work in `let`

**Status: ✅ No Fix / by design (2026-07-01)** — `?` is not Lambda's
error-propagation syntax. The current postfix propagation operator is `^`,
including on the right-hand side of a `let`.

```lambda
let doc = input("file.pdf", 'pdf')?    // wrong: stale syntax
let doc = input("file.pdf", 'pdf')^    // correct: propagate on error
let doc^err = input("file.pdf", 'pdf') // correct: capture value/error
```

The older issue came from stale examples that described `?` as propagation.
Current docs should use `^` for propagation and keep `let a^err = ...` for
explicit value/error destructuring.

---

## 5. Chained comparisons fail to parse without parentheses

```lambda
fn is_digit(k) { k >= 48 and k <= 57 }
// error[E100]: Unexpected syntax near 'k >=' [primary_expr, >, =]
// error[E100]: Unexpected syntax near 'k <=' [identifier, <, =]
```

Adding parens fixes it: `(k >= 48) and (k <= 57)`. The grammar appears to
mis-tokenise `>=`/`<=` when they appear in `expr OP expr and expr OP expr`
without grouping. This is highly unergonomic for any character-class /
range checker, of which a tokenizer has dozens.

**Asks**:
- Either tighten the precedence so the natural form parses,
- Or improve the error to suggest "wrap each comparison in parens".

---

## 6. `ord(c)` returns "wider int" that is rejected by `int` parameters

**Status: ✅ Fixed (2026-07-01)** — `ord()` now has Lambda-level return type
`int`. The C ABI still returns `int64_t`, but Unicode code points fit within
the compact `int` value range, so callers annotated with `int` can accept
`ord()` directly.

```lambda
fn is_digit(k: int) { (k >= 48) and (k <= 57) }
is_digit(ord("0"))
// true
```

`ord` previously returned "type 5" (`int64`), while the parameter annotation
`int` accepted only "type 4". Since the widest Unicode scalar value is
`U+10FFFF`, `ord()` does not need the wider Lambda `int64` type.

**Asks**:
- Completed by narrowing `ord()`'s Lambda-level return type to `int`.
- Completed by changing compile-time type mismatch diagnostics to print type
  names instead of raw IDs, e.g. `expected int, got int64`.

---

## 7. Element literal single-line form rejects multi-statement children

```lambda
<g class: "x"; for (c in items) c >        // syntax error at '>'
<g class: "x";
   for (c in items) c
>                                           // OK
```

Documented children-block grammar suggests both should be equivalent. It
took several attempts to discover the multi-line form is required when the
children expression has any whitespace.

---

## 8. `format(elem, 'html')` strips attributes from non-HTML elements

**Status: ✅ Fixed / verified (2026-07-01)** — current `format(..., 'html')`
preserves attributes on non-HTML/SVG elements inside an HTML tree.

```lambda
let tree = <div class: "wrap";
    <svg width: 100, height: 50;
        <rect width: 10, height: 5, fill: "red">
    >
>

let html = format(tree, 'html')
// contains: <svg width="100" height="50">
// contains: <rect width="10" height="5" fill="red">
```

When an SVG subtree (`<rect width: 10; height: 5>`) is embedded inside a
larger HTML/element tree and formatted with `'html'`, the formatter drops
all attributes from non-HTML tags. Switching to `format(root, 'xml')`
preserves them. This is undocumented; users building SVG-in-HTML pages
trip over this immediately.

The current formatter walks the element shape fields for every tag rather than
filtering by known HTML tag names, so SVG and other non-HTML element attributes
are emitted as normal HTML attributes.

---

## 9. Single-quote vs double-quote strings produce non-equal values

**Status: ✅ No Fix / by design (2026-07-01)** — single-quoted literals are
`symbol` values; double-quoted literals are `string` values. Equality does not
coerce between `symbol` and `string`, even when their spelling is identical.

```lambda
let t = 'name'    // symbol
let s = "name"    // string
print(t == s)     // false
```

This is intentional: symbols are interned identifiers/tags, while strings are
text data. When a field stores a string value such as `"indirect_ref"`, compare
against a string literal, or convert explicitly with `string(...)` /
`symbol(...)` at the boundary.

---

## 10. `let` bindings cannot be reassigned inside `pn`

**Status: ✅ No Fix / by design (2026-07-01)** — `let` bindings are immutable in
both `fn` and `pn`; use `var` for any local that must be reassigned.

```lambda
pn build() {
    let m = {}
    m = m_with(field, value)    // error
}
```

The diagnostic now keeps this as a compile-time immutable-assignment error and
adds the actionable hint: ``cannot assign to let binding 'm'. declare with `var` instead of `let`.``.

---

## 11. No string slicing builtin

**Status: ✅ Fixed (2026-07-01)** — Lambda supports both `slice(string, a, b)`
and range subscript syntax `str[a to b]` for string slicing.

Original concern: without a primitive, every parser had to hand-roll:

```lambda
pn slice_str(s, a, b) {
    var out = ""
    var k = a
    while (k < b) { out = out ++ s[k]; k = k + 1 }
    return out
}
```

That O(n²) repeated-append workaround is no longer needed. Use
`slice(s, start, end)` for `[start, end)` slicing, `slice(s, start)` to slice to
the end, or `s[start to end]` for inclusive range-subscript syntax.

---

## 12. `pn main()` does not auto-print its return value

**Status: ✅ Fixed (2026-07-01)** — `./lambda.exe run script.ls` now prints a
non-null return value from `pn main()`, matching the functional script path's
auto-print behavior. `null` returns remain silent so existing print-only
procedural scripts do not emit a stray `null`.

---

## 13. Empty string/null conflation

**Status: ✅ No Fix / by design (2026-07-02)** — Lambda normalizes empty
string literal `""` and empty symbol literal `''` to `null` in its value/null
semantics. Treat user data strings and symbols as non-empty values; use `null`
for the empty case.

```lambda
let s = ""
print(s == null)         // -> true
print(len(s))            // -> 0
let m = { name: "" }
print(m.name)            // -> null     (NOT "")
```

`len("") == 0` because `""` is normalized to `null`, and `len(null)` is `0`.
This means any record field or `var` initialised with `""` reads back as
`null`, and downstream code that branches on `field == null` takes the null
path by design.

**Documentation resolution**:
- Document prominently that `""` and `''` are `null` in Lambda.
- Call out that Lambda string and symbol user-data values should be treated as
  non-empty, with `null` representing the empty case.

---

## 14. `var` declared with a "null-shaped" initial value cannot be reassigned

**Status: ✅ Fixed (2026-07-02)** — unannotated `var` bindings now widen even
when their initial value is `null` or an empty text literal normalized to
`null`. Reassigned values survive both direct reads and later map/record
packing.

```lambda
pn demo() {
    var s = ""             // "" normalizes to null
    s = "hello"
    print({ s: s })        // -> { s: "hello" }

    var x = null
    x = { a: 1 }
    print({ x: x })        // -> { x: { a: 1 } }

    var m = { x: 0 }       // ← concrete shape: works
    m = { x: 99 }
    print({ m: m })        // -> { m: { x: 99 } }
}
```

Root cause: the MIR runtime variable already widened to boxed `ANY`, but the
AST name entry and later identifier nodes kept the initializer's static
`null` type. Map literals such as `{s: s}` then compiled a null-shaped field
and discarded the reassigned boxed value during field packing.

Regression coverage: `test/lambda/proc/proc_var.ls` now checks both
`var x = null; x = {a: 1}; {x: x}` and `var s = ""; s = "hello"; {s: s}`.

---

## 15. `if (cond) X else Y` expression returns `null` inside `pn`

**Status: ✅ Fixed (2026-07-02)** — `let`/`var` initializers inside `pn` now
preserve the value of expression-style `if/else` forms, including mixed
branch types that box to `ANY`.

```lambda
pn demo(xs) {
    let op0 = if (len(xs) >= 1) xs[0] else null
    print({ op0: op0 })       // -> { op0: { a: 1 } }
}

fn demo_fn(xs) {
    let op0 = if (len(xs) >= 1) xs[0] else null
    op0                       // -> { a: 1 }   (works in fn)
}
```

Root cause: the MIR procedural path deliberately discarded boxed
if-expression branch results when an `if` was used as a side-effect-only
statement. `let op0 = if (...) ... else ...` was also evaluating its RHS
through that procedural discard path, so mixed branch values were replaced
with `null` before the binding was stored.

Regression coverage: `test/lambda/proc/proc_control.ls` now checks
`let op0 = if (len(xs) >= 1) xs[0] else null` inside a `pn` and verifies that
the resulting map preserves `{ a: 1 }`.

---

## 16. `if (cond) expr` (no braces) inconsistently rejected

```lambda
fn one(el)  { if (el) [el] else [] }       // error[E100]: Missing { }
fn one2(el) { if (el) { [el] } else { [] } }   // OK
```

Brace-less if-expressions are accepted in some places (top-level
expressions, ternary-style assignments) but rejected in others (function
body tail position, list/element children). The exact rule isn't
documented; defensively bracing every branch is the safe form.

**Asks**:
- Either accept brace-less branches uniformly, or document precisely
  where braces are required.

---

## 17. `fn` cannot call `pn` (E224) — viral propagation up the call graph

**Status:  ✅ No Fix / by design** — `fn` cannot call `pn`. Functional Lambda
functions must remain pure expression contexts, while `pn` is the procedural
context that permits mutation, statement sequencing, early `return`, and other
imperative effects. A caller that needs to invoke a procedure must itself be a
`pn`.

```lambda
pn helper(s) { var i = 0; ...; return out }
fn caller(x) { helper(x) }
// error[E224]: procedure 'helper' cannot be called in a function
```

This propagation is intentional: allowing a pure `fn` to call a procedural
`pn` would leak procedural effects into expression-only code and blur the
language boundary between pure functions and procedures.

Guidance: keep reusable pure helpers as `fn`. Use `pn` for drivers and helpers
that require mutation or procedural control flow, and keep their transitive
callers procedural too.

---

## 18. Multi-line `++` chains break — newline ends the expression

**Severity: HIGH** — silent: produces a list of fragments, not a string.

```lambda
fn make_label(x, y) {
    let xform = "matrix(1 0 0 1 " ++ util.fmt_num(x)
                 ++ " " ++ util.fmt_num(y) ++ ")"
    do_something(xform)
}
// xform ends up = "matrix(1 0 0 1 12"   (truncated!)
// `++ " " ++ ...` becomes a separate expression that errors
// fn body becomes [error_value, do_something(...)]   — a list of 2!
```

The newline after `util.fmt_num(x)` terminates the `let` binding
expression. The next line begins with `++` and parses as a binary
operator with no left-hand side — that produces an error value.
Worse, because both `let xform = …` and the trailing
`do_something(xform)` are now top-level statements in the function body,
the implicit return becomes a 2-element list `[error, result]` instead
of the intended single result.

**Workaround**: wrap the entire chain in parens:

```lambda
let xform = ("matrix(1 0 0 1 " ++ util.fmt_num(x)
              ++ " " ++ util.fmt_num(y) ++ ")")
```

**Asks**:
- A line ending in a binary operator (`++`, `+`, `and`, `or`, `==`,
  `?:`, etc.) should consume the next line as a continuation — every
  scripting language does this.
- Or at least raise a syntax error at the `++` with no LHS, instead of
  silently producing an error value that propagates as data.

**Confirmed in Phase 3**: same bug applies to multi-line `or` chains.
A `pub fn handles(opr) { (a == "x") or (a == "y")\n  or (a == "z") }`
parses as two statements; the trailing `or (...)` becomes an
"undefined variable 'or'" runtime error inside MIR. Wrapping the entire
`or` chain in an outer `(...)` fixes it.

---

## 19. `pn` parameter with `: int` annotation breaks `string(p + 1)`

**Status: ✅ Fixed / verified (2026-07-02)** — current MIR direct execution
handles `string(page_index + 1)` correctly when `page_index` is a `pn`
parameter annotated as `: int`.

```lambda
pn render(page_index: int) {
    let s = "Page " ++ string(page_index + 1)
    print({ s: s })       // -> { s: "Page 1" }
}

pn render2(page_index) {  // no annotation
    let s = "Page " ++ string(page_index + 1)
    print({ s: s })       // -> { s: "Page 1" }
}

pn main() {
    render(0)
    render2(0)
}
```

Verified repro output: both the annotated and unannotated forms print
`{ s: "Page 1" }` under `./lambda.exe run`.

Regression coverage: `test/lambda/proc/proc_param_type_infer.ls` now checks
`pn test_typed_int_string(n: int) { "Page " ++ string(n + 1) }`.

---

## 20. `pn` calling `pn` returns a stale/default record (nested-pn corruption)

**Status: ✅ Fixed / not reproducible (2026-07-02)** — current MIR direct
execution preserves record fields returned from a child `pn` and consumed by a
parent `pn`. A focused nested-`pn` probe now returns `weight: "bold"` through
both the direct child call and the parent binding.

```lambda
pn child_font(name: string) {
    let weight = if (name == "Helvetica-Bold") { "bold" } else { "normal" }
    {family: "Helvetica", weight: weight, style: "normal"}
}

pn parent_font(name: string) {
    let info = child_font(name)
    {family: info.family, weight: info.weight, style: info.style, info: info}
}
```

Verified output:

```lambda
{ direct: { family: "Helvetica", weight: "bold", style: "normal"}}
{ nested: { family: "Helvetica", weight: "bold", style: "normal",
            info: { family: "Helvetica", weight: "bold", style: "normal"}}}
```

Note: the original PDF `font.ls` path has already been rewritten to use `fn`
helpers (`from_basefont`, `resolve_font`, `_make_descriptor`) rather than the
old `pn -> pn` chain that exposed this issue.

Regression coverage: `test/lambda/proc/proc_nested_pn_record.ls` verifies
that a parent `pn` can bind a record returned from a child `pn` without losing
non-default fields.

---

## 21. Map "spread + override" constructors silently drop fields not listed

**Status: ✅ No Fix / by design (2026-07-02)** — map literals construct exactly
the fields listed in the literal. They do not implicitly copy fields from an
input state record. Use map spread when the intent is "copy this record, then
override these fields."

```lambda
fn _with(st, tm, tlm, name, size, info, leading, in_text) {
    {
        in_text:    in_text,
        tm:         tm,
        font_name:  name,
        font_size:  size,
        // …only the fields explicitly listed survive…
    }
}
```

The PDF text state had a `fill` field added later (for the non-stroking
colour). Every subsequent `Tf` (set-font) op called `_with` to build a
new state record, and `_with` did not list `fill` — so the new record
silently dropped the colour that `rg` had set just before. Result: text
rendered black even when the surrounding code looked correct.

This is "user error" in one sense, but the Lambda map syntax has a
spread form `{*:st, key: value}` precisely for this case. It's easy to
forget to use it, and there is no warning when a record-typed value
loses a field across a constructor call. Verified on 2026-07-02: the
plain-map shorthand `{st, key: value}` does not parse; use `{*:st, ...}`.

Changing the runtime semantics would be wrong: a fresh map literal must remain a
fresh constructor, and silently preserving fields from an input parameter would
make map construction context-dependent. The fix is to make the intended update
form prominent in the docs and use it consistently in state-update helpers.

**Resolution**:
- Always prefer `{*:base, key: override, …}` over hand-listing fields.
- Or define the record as a typed shape so the type system catches the
  missing field.
- Documented this more prominently in `doc/Lambda_Data.md` and
  `doc/Lambda_Expr_Stam.md`: fresh map literals do not preserve omitted fields;
  spread the base value first for copy-with-override updates.

**Possible future improvement**:
- An opt-in lint warning could flag a helper that accepts a map-like parameter
  and returns a smaller map literal without spreading it. This should not be a
  runtime warning/error because smaller map construction is valid Lambda code and
  many intentional constructors would otherwise become noisy.

---

## 22. `array ++ list-comprehension-result` errors at the *next* `format()`

**Severity: HIGH** — silent: the `++` succeeds, but the resulting value
poisons whatever consumes it, with the diagnostic pointing at an
unrelated call site.

```lambda
fn _format_dash(arr) {
    let parts = (for (n in arr) util.fmt_num(n))
    parts | join(",")           // runtime error [201]:
                                // fn_join: unsupported operand types: array and element
}
```

The list-comprehension `(for (n in arr) ...)` produces a value whose
runtime type is *not* `array`, even when `arr` is. Piping that into
`join` (which only accepts `array of string`) raises the cryptic
`unsupported operand types: array and element` error.

Worse, the same mismatch happens at the higher level when the caller
does:

```lambda
paths = paths ++ (for (e in emit) wrap(e))
```

`paths` is `array`, the comprehension result is `list`, and `++`
silently produces a hybrid value that errors out at the *next* time
something tries to format it (often many lines later, in a totally
unrelated call), with no hint that the offending `++` was the culprit.

**Workarounds**:
- Materialise comprehension results as plain array literals via index
  access: `if (len(emit) == 1) [wrap(emit[0])]`.
- Or wrap the entire chain in a procedural `var out = []; while … {
  out = out ++ [item] }`.

**Asks**:
- Either unify `array` and `list` types so `++` works across them, or
- Raise the error at the `++` site, not at the eventual consumer.
- Document `(for x in xs) f(x)` as returning `list`, not `array`.

---

## 23. Element literal attribute set is fully static — no spread / no conditional

**Status: Still open (2026-07-02)** — attribute spread is still unsupported,
and the intended inline `if` expression form for attribute values still fails
because of the current grammar. This is workable but surprising; it forces
N-fold duplication of element literals when only one attribute varies.

```lambda
// What you'd want:
<path d: d, fill: f, stroke: s,
      'stroke-dasharray': if (has_dash) dash else "none">     // syntax error

// Current accepted workaround:
<path d: d, fill: f, stroke: s,
      'stroke-dasharray': (if (has_dash) dash else "none")>

// Also accepted:
<path d: d, fill: f, stroke: s,
      'stroke-dasharray': if (has_dash) { dash } else { "none" }>

// What works: branch the entire element:
if (has_extras) {
    <path d: d, fill: f, stroke: s,
          'stroke-dasharray': dash, 'fill-opacity': fo>
}
else {
    <path d: d, fill: f, stroke: s>
}
```

There is no spread operator (`{*: extras}`) for attributes, no
conditional attribute (`attr?: value when cond`), and no way to build
a dynamic attribute set programmatically and apply it to an element
literal.

**Diagnosis (2026-07-02)**:
- The grammar intends to allow `if_expr` in attributes:
  `_attr_expr` includes `$.if_expr`.
- But `if_expr` parses its unbraced `then` and `else` arms using full
  `$._expr`, not an attribute-safe expression grammar.
- Full `$._expr` includes relational `>`, so in
  `"none">` the closing element delimiter can be consumed as a binary
  operator inside the else expression. Tree-sitter then produces an `ERROR`
  node, and Lambda reports `error[E100]: Missing { } for if-statement`.
- Parentheses or braced arms work because they create an explicit boundary
  before the element-closing `>`.

For SVG/HTML emitters this means each element with N optional
attributes needs `2^N` literal forms — or the emitter must always emit
defaults like `stroke-dasharray="none"`, `fill-opacity="1"` (which
bloats output and changes existing goldens).

**Asks**:
- Modify the grammar so attribute-context `if_expr` arms use an
  attribute-safe expression grammar, allowing
  `attr: if (cond) a else b` to parse correctly before the element `>`.
- Either add spread/conditional attribute syntax, or
- Provide a `make_element(tag, attrs_map, children)` runtime constructor
  that takes the attribute set as an ordinary `map`.

### 24. Empty `else if`/`else` block in `pn` silently breaks the loop

**Severity: HIGH** — Adding an empty branch body (even just a comment) in a `pn` loop causes the loop to break downstream, often with all output disappearing and no error.

```lambda
pn driver(op) {
  if (op == "a") { ... }
  else if (op == "b") { /* comment only */ }   // BAD: breaks loop
  else { ... }
}
```

**Workaround:** Always put a no-op statement in the body, e.g. `st = st`.

**Discovered:** While adding marker-op branches (BMC/EMC/etc.) in PDF interp driver.

---

### 25. Sentinel string compare bug — `var s = " "` compares unequal to literal `" "`

**Severity: HIGH** — Initializing a `var` with a string literal (e.g. `var s = " "`) and later comparing `s == " "` returns FALSE, even when both print as a single space. This breaks sentinel logic for state machines.

**Workaround:** Use an explicit int flag (e.g. `has_pending_clip = 0/1`) to track state, not sentinel strings.

---

### 26. Multi-line `++`/`or`/`and` chains break — newline ends the expression

**Severity: HIGH** — A line ending in `++`, `or`, `and` (or any binary op) does NOT continue to the next line. The trailing `++ x` / `or x` becomes a bare expression that errors. For `++`, function body silently becomes a 2-element list `[error, real_result]`.

**Workaround:** Wrap the whole chain in parens:

```lambda
let s = ("a" ++
     "b" ++
     "c")
```

---

## Summary of recurring themes

1. **Silent semantic differences between `pn` and `fn`** are the single
   largest source of wasted time. `pn` does not implicitly return (#2),
   `pn`'s `if/else` expression evaluates to null (#15), `pn main()`
   doesn't auto-print (#12), and `fn` cannot call `pn` (#17). Together
   they push all real logic into `fn` and reduce `pn` to a thin
   imperative shell — which is the opposite of how the docs frame the
   two modes.
2. **Empty text normalization and var-locking** (#13, #14) silently surprise
   users when undocumented. `""` and `''` are null by design; `var s = ""`
   (or `var x = null`) becomes a permanent null sink that swallows every
   subsequent assignment with no error. These two interact catastrophically
   with #15.
3. **Operator overloading gotchas** — list `+` is by-design broadcast /
   element-wise add rather than concat (#1); `'sym' == \"sym\"` is false
   (#9). The list case must be documented clearly as `++` for concat.
4. **Error messages with raw type IDs** (`type 5 vs type 4`, #6) and
   misleading messages (`\"non-function value\"` for arity errors, #3)
   send users hunting in the wrong direction.
5. **Missing string-slice primitive** (#11) forces every parser to
   hand-roll a quadratic helper; combined with #14 the natural form
   `var out = \"\"; while … { out = out ++ ch }` produces an empty
   string forever instead of an O(n²) slowdown.
6. **Parser quirks** — chained comparisons need parens (#5), `?` rejected
   in `let` (#4), single-line element literals limited (#7), brace-less
   `if` accepted in some positions but not others (#16) — each is a
   small diversion individually but they add up.

### Top-3 fixes that would have saved the most time during PDF Phase 2

1. Make `var x = null`/`var s = \"\"` either widen on assignment or hard
   error. Silent assignment failure is the worst possible outcome.
2. Make `pn` either implicitly return its body value, or error at
   compile time when a branch reaches the end without `return`.
3. Prominently document that `\"\"` and `''` normalize to `null`, and avoid
   using them as mutable string/symbol sentinels.
