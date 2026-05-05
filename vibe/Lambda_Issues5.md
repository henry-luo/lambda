# Lambda Script Issues — Phase 2 PDF Package

## Current Verification Status (2026-05-05)

Quick probes against the current `./lambda.exe` show that several items in
this document have changed since they were first recorded. This was a
spot-check, not a full audit of every issue below.

### Confirmed fixed or improved

- **#9 single-quote symbol vs double-quote string comparison**: improved.
  Comparing `'name' == "name"` now fails at compile time with a useful
  diagnostic instead of silently evaluating false:
  `comparing 'symbol' with 'string' will always be false`.
- **#14 / #29 `var` initialized with `""` or `null` cannot be reassigned**:
  appears fixed in the checked cases. `var s = ""; s = "hello"` returned
  `"hello"`, and `var x = null; x = { a: 1 }` returned `{ a: 1 }`.
- **#2 / #28 simple `pn` implicit `if/else` return**: partially improved.
  A simple `pn` whose body is only an `if/else` expression returned the
  selected branch value in the checked case.

### Confirmed still present

- **#13 empty string/null conflation**: still present. `"" == null` returned
  `true`, and `{ name: "" }.name` read back as `null`.
- **#15 / #30 `let x = if (...) ... else ...` inside `pn`**: still present.
  The checked `pn` returned `null` for the binding even when the condition was
  true, while the equivalent `fn` returned the expected value.
- **#3 multi-argument `print()`**: still present. `print("x=", 1)` compiled
  but failed at runtime with `fn_call2: cannot call non-function value`.
- **#5 chained comparisons without parentheses**: still present.
  `fn is_digit(k) { k >= 48 and k <= 57 }` failed to parse.

### Not rechecked in this pass

The remaining parser, formatter, operator, element-literal, and nested-`pn`
issues below were not rechecked during this quick status update and should be
treated as unverified unless separately tested.

## Phase 7 Addendum — Lambda Script Gotchas (2026)

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

### 28. `pn` does not implicitly return an `if/else` expression value

**Severity: HIGH** — `pn` must use explicit `return` in every branch. Otherwise, callers get `null`/stale value, causing non-terminating loops.

---

### 29. `var` declared with a "null-shaped" or empty-string initial value cannot be reassigned

**Severity: HIGH** — `var s = ""` or `var x = null` locks the var as null-typed forever; subsequent assignments are dropped with no error. Use a non-null, non-empty sentinel (e.g. `" "` or `{}`) or an int flag.

---

### 30. `let x = if (cond) a else b` returns `null` in `pn`

**Severity: HIGH** — In `pn`, `let x = if (cond) a else b` always returns `null`, even when `cond` is true. Use imperative `if`/`else` with explicit assignment instead.

---

## 1. List `+` is element-wise add, not concat (silent footgun)

**Severity: HIGH** — this caused an infinite loop with no diagnostic.

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

**Asks**:
- `+` on heterogeneous-shape lists should at least warn or raise.
- Or: make `+` on lists be concat (matching most scripting languages) and
  reserve element-wise add for explicit `vec_add` / numeric vectors.
- Documentation never explicitly says "use `++` for list concat"; the
  `Lambda_Cheatsheet.md` shows `++` only for strings.

---

## 2. `pn` does not implicitly return an `if/else` expression value

**Severity: HIGH** — also caused an infinite loop.

```lambda
pn classify(c) {
    if (c == "/") { read_name(c) }
    else          { read_other(c) }
}
```

A caller `let r = classify("/")` receives `null` (or stale stack slot),
even though the same body inside an `fn` returns the chosen branch.

**Workaround**: every branch needs an explicit `return …`:

```lambda
pn classify(c) {
    if (c == "/") { return read_name(c) }
    return read_other(c)
}
```

**Asks**:
- Either make `pn` implicitly return the body value (consistent with `fn`),
- Or have the transpiler emit a warning `"pn function reaches end of body
  without return"` for any branch that doesn't end in `return`/`raise`/loop.

The current behaviour silently propagates whatever happens to be in the
return slot, which manifests as `0`/`null` and produces non-terminating
loops downstream.

---

## 3. `print()` is single-argument; multi-arg call type-errors at runtime

```lambda
print("x=", x)
// runtime error [212]: fn_call2: cannot call non-function value
```

Every sane print expects varargs. The runtime error message also doesn't
mention `print` or "too many arguments" — it says "non-function value",
which sent me hunting for a missing import.

**Asks**:
- Make `print` variadic and space-join (or stringify-join) like most
  REPLs.
- At minimum, raise `"print expects 1 argument, got N"`.

---

## 4. `?` post-fix error propagation does not work in `let`

```lambda
let doc = input("file.pdf", 'pdf')?    // syntax error
let doc^err = input("file.pdf", 'pdf') // works
```

`?` is documented in `doc/Lambda_Error_Handling.md` as a propagation
operator and works in expression context, but the parser rejects it on the
right-hand side of a `let`. This is surprising because `let a^err = …` is
the common destructuring form documented elsewhere — both should work or
the docs should call out the restriction.

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

```lambda
fn is_digit(k: int) { (k >= 48) and (k <= 57) }
is_digit(ord("0"))
// error[E201]: argument 1 has incompatible type 5, expected 4
```

`ord` returns "type 5" (presumably `int64`), but the parameter annotation
`int` accepts only "type 4". The fix is to drop the annotation entirely.

**Asks**:
- Auto-widen / coerce on call (this is just `int → int64`, never lossy).
- Or expose the wider type by name (e.g. `int64`, `i64`) so users can
  annotate without guessing.
- The error message gives raw type IDs (`5`, `4`) instead of names — even
  `expected int, got int64` would save a search.

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

When an SVG subtree (`<rect width: 10; height: 5>`) is embedded inside a
larger HTML/element tree and formatted with `'html'`, the formatter drops
all attributes from non-HTML tags. Switching to `format(root, 'xml')`
preserves them. This is undocumented; users building SVG-in-HTML pages
trip over this immediately.

---

## 9. Single-quote vs double-quote strings produce non-equal values

```lambda
let t = 'name'    // symbol
let s = "name"    // string
print(t == s)     // false
```

Parser output uses string `"indirect_ref"` for the `type:` field of
indirect-reference maps. Comparing against `'indirect_ref'` (the natural
"tag" form) silently never matches. Worth documenting prominently or
adding implicit coercion for `==` between symbol and string of equal
spelling.

---

## 10. `let` bindings cannot be reassigned inside `pn`

```lambda
pn build() {
    let m = {}
    m = m_with(field, value)    // error
}
```

Easy to forget — `var` is required for any value that participates in a
loop or mutation. The error currently just says "cannot assign to let
binding" which is fine, but it would be nice if the suggestion was
"declare with `var` instead of `let`".

---

## 11. No string slicing builtin

There is no `substring(s, a, b)` or `s[a..b]`. Every parser ends up
hand-rolling:

```lambda
pn slice_str(s, a, b) {
    var out = ""
    var k = a
    while (k < b) { out = out ++ s[k]; k = k + 1 }
    return out
}
```

This is O(n²) for repeated appends because `++` allocates a new string
each iteration. For a tokenizer scanning a 250-byte content stream this
is fine, but for any larger document it becomes the bottleneck.

**Asks**:
- Add `slice(s, a, b)` / `s[a..b]` / `string_slice(s, a, b)` as a
  primitive that does a single allocation.

---

## 12. `pn main()` does not auto-print its return value

`./lambda.exe run script.ls` calls `main()` but discards the return value.
Functional scripts without `pn main()` print the top-level expression.
This dual behaviour is fine but not obvious from the help text — first
runs of `pn main() { compute() }` produce zero output and look broken.

**Asks**:
- Either auto-print the return value of `main`,
- Or make the help/REPL banner mention "use `print(...)` to emit output
  from `pn main()`".

---

```
let s = ""
print(s == null)         // -> true
print(len(s))            // -> 0
let m = { name: "" }
print(m.name)            // -> null     (NOT "")
```

`len("") == 0` but `"" == null` is `true`. This means any record field
or `var` initialised with `""` reads back as `null`, and downstream code
that branches on `field == null` takes the wrong path.

**Asks**:
- Treat `""` as a real empty string distinct from null.
- At minimum, document this prominently — it is a one-line cause of
  hours-long debugging.
```
```
---

## 14. `var` declared with a "null-shaped" initial value cannot be reassigned

**Severity: HIGH** — silent assignment failure.

```lambda
pn demo() {
    var s = ""             // ← s is locked as null-typed
    s = "hello"
    print({ s: s })        // -> { s: null }   (assignment dropped!)

    var x = null
    x = { a: 1 }
    print({ x: x })        // -> { x: null }   (also dropped)

    var m = { x: 0 }       // ← concrete shape: works
    m = { x: 99 }
    print({ m: m })        // -> { m: { x: 99 } }
}
```

The `var` is type-inferred from its initial expression. If the initial
value is `null` (or anything that becomes null, e.g. `""` per #13),
subsequent assignments of differently-typed values are silently dropped
— no error, no warning, the variable just stays `null`.

This combines lethally with #13: `var s = ""; while (...) { s = s ++ ch }`
appears reasonable but produces an empty string forever.

**Asks**:
- Either widen the inferred type on assignment, or raise a type error.
- Silent assignment failure is the worst possible outcome.

---

## 15. `if (cond) X else Y` expression returns `null` inside `pn`

**Severity: HIGH** — combined with #2 this means `pn` cannot use
expression-style conditionals at all.

```lambda
pn demo(xs) {
    let op0 = if (len(xs) >= 1) xs[0] else null
    print({ op0: op0 })       // -> { op0: null }   even when len(xs) == 2
}

fn demo_fn(xs) {
    let op0 = if (len(xs) >= 1) xs[0] else null
    op0                       // -> { a: 1 }   (works in fn)
}
```

Same construct, different result depending on `pn` vs `fn`. Wrapping
in parens does not help; using a `var` and an imperative `if` body also
fails (because the var hits #14).

**Workaround**: write all dispatcher / branching logic as `fn`. Use `pn`
only as the outermost driver with `while`-loops and `var` whose initial
value matches the eventual shape (`var out = []`, not `var out = null`).

**Asks**:
- Make `let x = if (c) a else b` work in `pn` (it's the natural form).
- Or emit a syntax error: "if-expression value not bound in pn; use
  `if (c) { x = a } else { x = b }`".

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

```lambda
pn helper(s) { var i = 0; ...; return out }
fn caller(x) { helper(x) }
// error[E224]: procedure 'helper' cannot be called in a function
```

Once you write a `pn` somewhere, every transitive caller must also be
`pn`, even if they have no mutation. Combined with #2 / #14 / #15 this
forces `pn` callers into awkward gymnastics that defeat the purpose of
the procedural mode.

**Workaround during PDF Phase 2**: rewrote `decode_hex` and
`decode_literal` from `pn` (with `var out = ""; while …`) to `fn` using
list comprehensions + `join("")`. Functional form is cleaner anyway,
but the engine forced the change.

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

**Severity: HIGH** — silent: produces an `error` value.

```lambda
pn render(page_index: int) {
    let s = "Page " ++ string(page_index + 1)
    print({ s: s })       // -> { s: "Page <error>" }
}

pn render2(page_index) {  // no annotation
    let s = "Page " ++ string(page_index + 1)
    print({ s: s })       // -> { s: "Page 1" }
}
render(0); render2(0)
```

When `page_index` is annotated `: int` and called from a `pn` chain
with the same caller-side type, `string(page_index + 1)` returns an
error value (which prints as `<error>` and silently corrupts the
output string).

This is likely the same root cause as #6 (int4 vs int5 type mismatch
in `ord`) but reproduced through a much more innocuous code path —
just a typed parameter and an arithmetic expression.

**Asks**:
- Make `int` parameters accept and produce the same int width
  consistently across `pn` boundaries.
- Or simply: stop silently producing an error value out of arithmetic
  on a typed parameter — raise instead.

---

## 20. `pn` calling `pn` returns a stale/default record (nested-pn corruption)

**Severity: HIGH** — silent: returned record looks valid but every
field is the type's default value.

```lambda
// font.ls
pub pn from_basefont(basefont: string) {
    let s14 = standard14(basefont)              // fn returning a record
    if (s14 != _UNKNOWN) { return s14 }
    // …heuristic path returning a record with weight/style filled in…
}

pub pn resolve_font(pdf, page, name: string) {
    let dict     = resolve.page_font(pdf, page, name)
    let basefont = _basefont_or(name, dict)
    let info     = from_basefont(basefont)      // pn → pn call
    return _make_descriptor(name, info, _to_unicode_or_null(dict))
}
```

Standalone `from_basefont("Helvetica-Bold")` returns
`{ family: "…", weight: "bold", style: "normal" }`. When invoked from
within `resolve_font` (also `pn`), the `info` binding receives
`{ family: "…", weight: "normal", style: "normal" }` — the bold/italic
information is silently lost. The same call in a probe `pn main()` that
does not nest two `pn` calls works correctly.

Tested workarounds that did **not** help:
- Returning the record through more `fn` helpers (`_make_descriptor`,
  `_pick_info`).
- Eagerly destructuring `let w = info.weight; …` before any further use.
- Renaming the binding, splitting the call onto its own statement.

**Workaround that worked**: avoid the `pn → pn` call. Call the `fn`
variant (`standard14`) directly from `resolve_font` and only fall back
to the `pn` heuristic when needed:

```lambda
pub pn resolve_font(pdf, page, name) {
    …
    let stripped = _strip_subset(basefont)     // fn
    let s14      = standard14(stripped)        // fn
    let fb       = from_basefont(basefont)     // pn (still called, but result
                                               //      no longer the only path)
    let info     = _pick_info(s14, fb)         // fn helper: if (s != _UNKNOWN) s else fb
    …
}
```

This is the same family of bug as #2 / #15 — `pn` value-flow is unsafe.
Combined with #15 (`let x = if …` is null in `pn`) the safe pattern is:
**push every value-producing branch into a `fn` and have `pn` only
sequence side effects.**

**Asks**:
- Either fix `pn → pn` value return (this should be table stakes), or
- Hard-fail at compile time on `let r = some_pn(…)` inside another `pn`
  with a dedicated diagnostic ("pn return values are not safely
  consumable from another pn; route through a fn").

---

## 21. Map "spread + override" constructors silently drop fields not listed

**Severity: MEDIUM** — silent: constructed record is missing fields you
forgot to copy.

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
spread form `{*: st, key: value}` precisely for this case. It's easy to
forget to use it, and there is no warning when a record-typed value
loses a field across a constructor call.

**Workarounds**:
- Always prefer `{*: base, key: override, …}` over hand-listing fields.
- Or define the record as a typed shape so the type system catches the
  missing field.

**Asks**:
- A warning when a `fn`/`pn` returns a map literal that has a strictly
  smaller field set than a same-named map flowing through the function
  on its inputs would catch this whole class of bug.

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

**Severity: MEDIUM** — workable but surprising; forces N-fold
duplication of element literals when only one attribute varies.

```lambda
// What you'd want:
<path d: d, fill: f, stroke: s,
      'stroke-dasharray': if (has_dash) dash else "none">     // syntax error

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

For SVG/HTML emitters this means each element with N optional
attributes needs `2^N` literal forms — or the emitter must always emit
defaults like `stroke-dasharray="none"`, `fill-opacity="1"` (which
bloats output and changes existing goldens).

**Asks**:
- Either add spread/conditional attribute syntax, or
- Provide a `make_element(tag, attrs_map, children)` runtime constructor
  that takes the attribute set as an ordinary `map`.

---

## Summary of recurring themes

1. **Silent semantic differences between `pn` and `fn`** are the single
   largest source of wasted time. `pn` does not implicitly return (#2),
   `pn`'s `if/else` expression evaluates to null (#15), `pn main()`
   doesn't auto-print (#12), and `fn` cannot call `pn` (#17). Together
   they push all real logic into `fn` and reduce `pn` to a thin
   imperative shell — which is the opposite of how the docs frame the
   two modes.
2. **Null/empty conflation and var-locking** (#13, #14) silently corrupt
   data. `""` reads as null from a record field; `var s = ""` (or
   `var x = null`) becomes a permanent null sink that swallows every
   subsequent assignment with no error. These two interact catastrophically
   with #15.
3. **Operator overloading footguns** — list `+` is broadcast-add not
   concat (#1); `'sym' == \"sym\"` is false (#9). Both compile cleanly
   and silently produce wrong results.
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
3. Make `\"\" == null` false (or at least make field reads of `\"\"`
   return `\"\"`, not `null`).
