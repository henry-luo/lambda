# Lambda Script Issues — Math Package, makeStackedDelim Port (2026-06-20)

Issues in the Lambda **language / parser / MIR transpiler / runtime** encountered
while porting MathLive's `makeStackedDelim` (extensible vertical-bar delimiters)
into `lambda/package/math/atoms/delimiters.ls`. Continues the numbering from
`Lambda_Issues5.md` (last issue #30).

These are pure-`.ls` package edits (no C++ rebuild), so every issue below is
reproducible against the current `./lambda.exe` by running a `.ls` script.

**Meta-observation that cost the most time:** every *parser* issue here
(#31, #32, #33) is **error-recovered silently** — the parser logs the error to
stderr but keeps going and builds a malformed AST, so the package still appears
to load (a probe script importing it still prints its result). One of them
(#33) turned a malformed function into an **infinite loop** at render time
(13 min at 100% CPU). Combined with a test harness that read a stale cached
report, this masked a totally broken package as "passing" for an extended
period. **Lesson: a non-zero parse-error count must be treated as fatal —
grep the render's stderr for `error[E` / `operand mode` before trusting any
downstream result.**

---

## 31. Bare map literal as an `if`/`else` branch fails to parse

**Severity: HIGH** (silent — error-recovers, see meta note) — A map literal
placed directly as the then- or else-branch of an `if` is mis-parsed: the
parser cannot tell the leading `{` from a block opener.

```lambda
fn f(x) {
    if (x > 0) {a: 1, b: 2}      // BAD
    else {a: 3, b: 4}
}
f(1)
```

```
temp/r1.ls:2:17: error[E100]: Unexpected syntax near 'a: 1, b:' [identifier, :, primary_expr, ,, identifier]
1 error(s) found.
```

**Workaround (confirmed):** wrap the map in a block that starts with a `let`
binding (which makes `{` unambiguously a block) and return the binding:

```lambda
fn f(x) {
    if (x > 0) { let r = {a: 1, b: 2}; r }   // OK -> "{ a: 1, b: 2}"
    else { let r = {a: 3, b: 4}; r }
}
```

Note the else-branch may be a plain block (`{ let e = ...; ... }`) — only the
branch whose value *is* a bare map literal needs the wrapper. This is the same
gotcha previously noted for big-op work; documented here with the exact
diagnostic and a confirmed minimal workaround.

**Status: UNFIXED (workaround only).**

---

## 32. `(let x = {...}, expr)` paren-comma branch form does not parse

**Severity: MEDIUM** — The "paren-comma" idiom `(let x = ..., x)` — which works
as a *function-call argument* / sub-expression in many places — does **not**
work as an `if`-branch or function body when the binding is a map. The parser
reports the function body as missing its arrow/brace:

```lambda
fn f(x) {
    if (x > 0) (let r = {a: 1}, r)   // BAD
    else (let r = {a: 3}, r)
}
```

```
temp/r1b.ls:1:1: error[E100]: Function body requires '=>' or '{...}'
1 error(s) found.
```

**Workaround:** use the block form from #31 (`{ let r = {...}; r }`).

**Status: UNFIXED (workaround only).**

---

## 33. Multi-line `++` continuation silently produces `error` elements

**Severity: CRITICAL** (silent data corruption + can cause infinite loops) —
Breaking a chained `++` (list concatenation) across lines, with `++` at the
start of continuation lines, does **not** parse as one expression. No error is
reported. The newline terminates the expression after the first operand; each
subsequent `++ <list>` line is parsed as a *prefix* `++` applied to the list,
which evaluates to the literal value `error`. The result is a malformed nested
list, not the intended flat concatenation.

```lambda
fn build() {
    [1, 2]
        ++ [3]
        ++ [4, 5]
}
string(len(build())) ++ " : " ++ string(build())
```

Produces (no error, no warning):

```
"3 : [[1, 2], error, error]"
```

Expected `5 : [1, 2, 3, 4, 5]`. Instead `len` is 3 and the tail elements are
the sentinel value `error`.

In the real code this made `stk_entries(0)` return 3 entries instead of 4 with
`error` placeholders; the downstream walk then either produced an empty result
or (in a different arrangement) spun in an **infinite loop** when iterating /
recursing over the malformed structure.

**Workaround (confirmed):** keep the whole chain on one line, or bind the
operands first:

```lambda
fn build() { let a = [1,2]; let c = [4,5]; a ++ [3] ++ c }   // OK -> "5 : [1, 2, 3, 4, 5]"
```

(For contrast, a *function-call argument list* wrapped across lines parses fine
— e.g. `f(a, b,\n   c)` — because it is inside parentheses. The problem is
specifically a top-level binary operator at the start of a continuation line.)

**Status: UNFIXED (workaround only).** This one is the most dangerous because it
corrupts data with zero diagnostics.

---

## 34. MIR transpiler infers `double` params as `int` (cross-function)

**Severity: HIGH** (runtime failure) — A function compiled to MIR rejected a
floating-point argument at a call site, expecting an integer:

```
func _render_vertical_mult_8907: in instruction 'call':
  unexpected operand mode for operand #4. Got 'double', expected 'int'
```

This occurred when a function (`make_stacked_delim`) was declared with float
parameters (`h`, `d`) whose *first* use inside the body was an ambiguous
arithmetic expression (`let rht = h + d`), the function also forwarded those
params into a **recursive** helper that mixed an `int` loop index with the
float accumulators, and the whole thing was called from another function with
float literals. The transpiler's param-type inference settled on `int` for the
float param and emitted a call that the MIR verifier rejected at JIT time.

The render aborts (the case produces no output); it is not a parse error.

**Workaround (confirmed to fix the real case):** do **not** pass the float
values as parameters. Read them from module-level `let` constants *inside* the
function, so their type is unambiguously `double`:

```lambda
let BAR1_H = 0.606
let BAR1_D = 0.0 - 0.00599

fn make_stacked_delim(level) {     // was (level, h, d)
    let h = BAR1_H                 // known-float module constant
    let d = BAR1_D
    let rht = h + d
    ...
}
```

**Status: Fixed (2026-07-02).** Reproduced with a compact cross-function
script before the fix:

```lambda
fn stack_helper(level: int, h: float, d: float, acc: float) float {
    if (level <= 0) {
        acc + h + d
    } else {
        stack_helper(level - 1, h, d, acc + h + d + level)
    }
}

fn make_stacked_delim(level: int, h: float, d: float) float {
    let rht = h + d
    stack_helper(level, h, d, rht)
}

make_stacked_delim(3, 0.606, 0.0 - 0.00599)
```

Before the fix, this failed during MIR verification with:

```
func _stack_helper_0: in instruction 'mov':
  unexpected operand mode for operand #1. Got 'double', expected 'int'
```

Root cause: the tail-recursive rewrite creates unreachable dummy values for the
rewritten branch, but those dummies still participate in enclosing `if` result
typing and MIR validation. The TCO path used an integer-shaped dummy and the
terminal-branch handling in `transpile_if` wrote `MOV 0` into a result register
that could be `double`. MIR correctly rejected that operand-mode mismatch.

Fix: TCO now uses the resolved MIR local parameter type when converting
recursive arguments, returns a type-shaped dummy for rewritten tail calls, and
`transpile_if` writes terminal-branch dummies with `DMOV 0.0` when the `if`
result register is `double`.

Regression coverage: `test/lambda/transpile_float_tco_cross_call.ls`.

---

## 35. Parse errors are non-fatal; broken functions load and run

**Severity: HIGH** (masks #31–#33) — When a `.ls` module has a parse error
(E100), `./lambda.exe` prints the diagnostic to stderr **but continues**: it
JIT-compiles and runs the script, and a downstream `import` of the broken
module still succeeds enough that a probe expression evaluates. There is no
non-zero exit specifically for "this module failed to parse," and the broken
function silently degrades (returns wrong data, or hangs).

Observed: a probe that does `import math: lambda.package.math.math` and prints
`"PKG_OK"` printed `"PKG_OK"` even while `delimiters.ls` had the issue-#31 parse
error — the broken `stk_walk` simply wasn't exercised by the probe. Only a
script that actually *rendered a vertical bar* surfaced the failure (as a hang,
issue #33 / an MIR error, issue #34).

**Mitigation (workflow, not a code fix):**
1. After any package edit, render a script that exercises the changed code path
   (not just an `import`), and `grep` its stderr for `error[E`, `operand mode`,
   `Unexpected syntax`.
2. Treat `1 error(s) found.` as fatal regardless of exit code.

**Status: UNFIXED (diagnostic/robustness gap).**

## 36. `parse()` leaks its `parse://inline` URL at shutdown

**Severity: MEDIUM** (memtrack leak) — Every `parse(str, {...})` call leaked one
`Url` (96 B) plus its ~5 component `String`s, reported at shutdown:

```
memtrack-console: ERR! memtrack: LEAK — 6 allocations (175 bytes) still live at shutdown
memtrack-console: ERR! memtrack:   temp: 6 allocs, 175 bytes
memtrack-console: ERR! memtrack:     temp line 45: 1 allocs, 96 bytes   <- url.c url_create (sizeof(Url))
memtrack-console: ERR! memtrack:     temp line 23: 5 allocs, 79 bytes   <- url.c url_create_string (components)
```

**Correction to the earlier draft of this doc:** this does **NOT** set exit
code 1. A successful `./lambda.exe script.ls` returns **0** even with the leak
printed. (The earlier "exit code 1" observation came from the *broken/hung*
package state during issues #31/#33, not from the leak.) So exit code *is* a
usable success signal; the leak only pollutes stderr.

**Root cause (chain of three):**
1. `fn_parse2` (`lambda-eval.cpp:2225`) creates `Url* dummy_url =
   url_parse("parse://inline")` and passes it to `input_from_source`. It never
   frees it — by design, because…
2. …`create_input(abs_url)` stores the pointer in `input->url` and tracks the
   `Input` in the `InputManager` singleton, whose destructor `~InputManager()`
   `url_destroy()`s every `input->url`. So the URL is *owned* by the Input.
3. But `InputManager::destroy_global()` — the only thing that runs
   `~InputManager()` — was **defined and declared yet never called anywhere**.
   So the singleton (and every tracked input's URL + its pooled data) outlived
   the process and showed up as a shutdown leak.

**Fix:**
- `main.cpp` `lambda_main_finish()`: call `InputManager::destroy_global()`
  before `memtrack_shutdown()`, so the destructor frees all tracked input URLs
  and the input global pool.
- Calling `destroy_global()` then **exposed two latent double-frees** in
  `input_from_url` (`input.cpp`): the `file://` path (was unconditionally
  `url_destroy(abs_url)` after handing `abs_url` to `create_input`, which now
  owns it → use-after-free at `~InputManager`) and the `sys://` path (same).
  Fixed both to free `abs_url` **only when input creation failed**
  (`if (!input) url_destroy(abs_url)`), matching the already-correct pattern in
  `input_from_target` (`input.cpp:879`).

Result: `parse()` / math render / `convert` (json, xml, yaml, md, toml, csv)
all run **leak-free, no ASan errors, exit 0**. Verified across 5+ runs and the
full math gate (823/921, unchanged).

**Status: ✅ FIXED.**

## 37. `./lambda.exe layout` URL leaks + a masked double-free

**Severity: MEDIUM** (leak) / **HIGH** (the masked UAF) — Two URL-ownership bugs
in `radiant/cmd_layout.cpp`, exposed while fixing #36.

**37a — `cwd` leak (the actual `layout` leak):** `cmd_layout` creates
`Url* cwd = get_current_dir()` (`cmd_layout.cpp:6735`) as the base for resolving
each input file's URL, but **never freed it** (`url_destroy(cwd)` appeared 0
times). This was the real leak seen on a simple `.html` layout (1 Url + its
component Strings). *(My earlier guess that it was `dom_doc->url` was wrong —
`dom_doc->url` aliases `input_url`, which `layout_single_file` already frees.)*
**Fix:** `url_destroy(cwd)` in the cleanup section before `return`.

**37b — `input_url` double-free / use-after-free (was masked):** `layout_single_file`
frees `input_url` (the per-file URL) at `cmd_layout.cpp:6483` **and** calls
`InputManager::destroy_global()` at 6542. Several loaders (`load_markdown_doc`,
latex, wiki, xml, script) pass `input_url` straight into `input_from_source`, so
the tracked `Input` *owns that exact pointer* (`input->url == input_url`).
Result: 6483 frees it, then 6542's `~InputManager()` frees it again →
heap-use-after-free → **SIGABRT (exit 134)** for `./lambda.exe layout x.md`
(and `.tex`/`.wiki`/`.xml`/`.ls`). HTML didn't crash because its loader resolves
a *separate* URL for the input. Confirmed pre-existing (reproduces with all my
changes reverted). **Fix:** added `InputManager::detach_url(Url*)` — nulls
`input->url` on any tracked Input that owns the pointer — and call it before
`url_destroy(input_url)` in `layout_single_file`. Doc-independent, so it also
covers the case where doc creation failed after the input parsed. Now html,
txt, svg, xml, wiki, ls layout are all leak-free and exit 0; markdown/latex no
longer crash.

**Status: ✅ FIXED (37a + 37b).**

- **Residual: markdown/latex layout leaks ~48 `FontFaceDescriptor`s — SEPARATE,
  pre-existing, UNFIXED.** Once 37b stopped the crash, markdown/latex layout
  reaches shutdown and reports ~48 `MEM_CAT_LAYOUT` allocations (~2 KB) from
  `radiant/font_face.cpp:215-231` — font-face descriptors registered when the
  embedded-math fonts load, never freed (`font_context_reset_document_fonts`
  doesn't release them). This is a **font-subsystem** lifecycle bug, not a URL
  leak; it was always there, just hidden behind the UAF. The math `Runtime`
  itself is fine (freed via `dom_document_destroy`→`lambda_runtime`). Converting
  a UAF crash into a clean exit + reported leak is a strict improvement; the
  descriptor leak is left for a dedicated font-lifecycle change. Does not affect
  exit code.

- **`expected a map, got data of type array` on package load.** Loading the
  math package (without `--no-log`) prints several:

  ```
  [ERR!] expected a map, got data of type array
  ```

  to the log during import. They do not abort loading and the renderer works;
  they look like a type-check noise path in the package's data setup.
  **Status: pre-existing, low priority.**

- **Cold start ≈ 9 s.** A single math render (package load + MIR JIT) takes
  ~9 s wall-clock cold. Relevant for test harnesses: a per-case timeout below
  ~10 s produces *false* "hang" reports. To bisect a genuine hang, batch many
  cases into one process (one startup) and use a generous (≥30 s) timeout, or
  amortise startup across cases.

---

## Summary table

| #   | Layer   | Issue                                                               | Repro        | Fix status                                  |
| --- | ------- | ------------------------------------------------------------------- | ------------ | ------------------------------------------- |
| 31  | parser  | bare map as `if` branch → E100                                      | minimal ✅    | workaround (block form)                     |
| 32  | parser  | `(let x={...}, x)` branch form → E100                               | minimal ✅    | workaround (block form)                     |
| 33  | parser  | multi-line `++` → `error` elements, no diagnostic                   | minimal ✅    | workaround (one line)                       |
| 34  | MIR     | float param inferred `int` → JIT verify error                       | real only ⚠️ | workaround (constants inside)               |
| 35  | runtime | parse errors non-fatal, broken module runs                          | observed ✅   | workflow mitigation                         |
| 36  | runtime | `parse()` leaks `parse://inline` URL (InputManager never torn down) | minimal ✅    | **FIXED** (destroy_global + 2 double-frees) |
| 37a | runtime | `layout` leaks `cwd` URL (never freed)                              | observed ✅   | **FIXED** (url_destroy(cwd))                |
| 37b | runtime | `layout x.md`/`.tex`/… UAF — `input_url` double-free (was masked)   | observed ✅   | **FIXED** (InputManager::detach_url)        |
| —   | layout  | markdown/latex leak ~48 FontFaceDescriptors (font subsystem)        | observed ✅   | unfixed, pre-existing, separate             |
| —   | runtime | `expected a map, got array` on load                                 | observed ✅   | unfixed, low pri                            |
| —   | env     | ~9 s cold JIT start (test-timeout trap)                             | observed ✅   | n/a                                         |

**Note on exit code:** contrary to this doc's first draft, no memtrack leak sets
exit code 1 — a successful run returns 0 regardless. Exit code *is* a reliable
gate signal; the leaks only add stderr noise.
