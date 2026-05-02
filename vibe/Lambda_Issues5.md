# Lambda Script Issues — Phase 2 PDF Package

Issues encountered while implementing `lambda/package/pdf/` (content-stream
tokenizer, font handling, text rendering). Each entry shows a minimal repro
and the workaround that unblocked the work.

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
