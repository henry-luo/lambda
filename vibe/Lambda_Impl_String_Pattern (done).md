# Lambda String Pattern `\(...)` Migration — Implementation Plan

**Date:** 2026-08-07
**Status:** IMPLEMENTED — all focused and required baseline gates pass
**Design authority:** `vibe/Lambda_Design_String_Pattern.md` (SP1–SP16, rev 5).
Semantics anchors: S10.1.1 (`|` union everywhere), S11.1.2 (delimited
pattern islands), S11.1.3 (range membership), S11.2.1 (match arms),
D3.1.1v2/D3.1.2 (first-class type values, representational comparison).
**Deferred by ruling:** SPO1 (class-name namespace — multi-letter/Unicode
class names) is **out of scope for this plan**; only the rev-4 single-letter
set `d w s a` + `.` + `...` ships. The grammar work below must not paint
SPO1 into a corner (P0.3).
**SPO4 resolved here (§4): atomic cutover, no dual-parse window** — the active
in-repo Lambda pattern corpus and docs were migrated together; a transition
grammar costs more than it saves.
**Scope addition (2026-08-07): P5 folds in the string-range runtime gap**
(`fn_to` integer-only) so the SP5 rev-4a `to`-overload is real in all three
contexts, not just parseable.

---

## 1. Current-state census (verified 2026-08-07)

### 1.1 Grammar (`lambda/tree-sitter-lambda/grammar.js`)

| Anchor | What | Fate |
|--------|------|------|
| `pattern_char_class` (:1236) | tokens `\d \w \s \a \.` | retired; replaced by bare `d w s a .` inside the island (SP2) |
| `concat_type` (:1255) | whitespace concat, `prec.dynamic(-1)` guard against `int[2+]` | moves inside the island; guard deleted from open grammar (SP8) |
| `grouped_type` (:1244) | `!? ( _string_type_expr ) occurrence?` | becomes island-interior grouping |
| `range_type` (:1144) | `literal to literal` in `primary_type` (:1149) — **purposely overloaded** (SP5 rev 4a): one denotation (the set of consecutive values), three compilations — expr = literal-array shorthand (`fn_to` → `Range`), type = membership (annotations, match arms), pattern = RE2 char class; `'range_to'` prec class arbitrates vs the value-range operator | **UNCHANGED in the open grammar** (P0.2a): stays in `primary_type` for all literal operands; the island interior also reaches it and compiles string-operand ranges as char classes. Only the class tokens leave the open grammar |
| `pattern_char_class` in `primary_type` (:1157) | pattern atoms directly in the **open** type grammar ("unified into type system") — `fn f(c: \d)` parses today | removed from `primary_type`; classes are island-only (SP5) |
| `_string_type_expr` (:1264) | the pattern expression choice | becomes the island interior; no longer in `type_assign`'s open choice |
| `type_assign` (:1299) | `choice(_type_expr, _string_type_expr)` | reverts to `_type_expr` only — the island is a primary type |
| precedence ladder (:220–230) | pattern rules interleaved with open type rules | pattern rules drop out of the shared ladder |

### 1.2 AST builder (`lambda/runtime/build_ast.cpp`)

| Anchor | What | Fate |
|--------|------|------|
| `cst_has_pattern_nodes` (:6019) | recursive CST scan detecting pattern definitions by content | **deleted** — detection becomes "delimiter present" (SP10) |
| `build_let_and_type_stam` (:6034) | routes `type X = ...` to pattern build when scan fires | routes on island node symbol instead |
| `build_string_pattern(tp, child, **false**)` (:6059) | `is_symbol` **hardcoded false** — symbol patterns are unwired at the definition site today | wired from the delimiter tag; `\symbol(...)` is **new functionality**, not a migration |
| `build_pattern_char_class` (:11435), `build_concat_type` (:11463), `build_string_pattern` (:11599) | pattern AST constructors | survive with new CST shapes |

### 1.3 Runtime (unchanged by design)

- `TypePattern` (`lambda/lambda-data.hpp:838`): `is_symbol` flag already
  exists; anchored RE2 + lazy unanchored RE2 + `source` string.
- `compile_pattern_ast` / `pattern_full_match` / `pattern_get_unanchored`
  (`lambda/runtime/re2_wrapper.hpp`): compilation and matching are
  syntax-agnostic (they consume the pattern AST). Consumers:
  `lambda-eval.cpp`, `transpile-mir.cpp`, `validator/validate.cpp`,
  `module/py/py_stdlib.cpp`.
- `TypePattern.source` is re-rendered for error messages — must render the
  **new** surface syntax after migration (P1.5).

### 1.4 Migration corpus

Scripts with real pattern syntax (grep `\\[dwsa.]`, LaTeX/math hits are
false positives inside string content):
`test/lambda/string_pattern.ls`, `string_pattern_ops.ls`,
`test_string_pattern_integration.ls`, `match_string_pattern.ls`,
`find_replace_options.ls`, `wip/enterprise_log_analysis.ls`
(+ audit `type_pattern.ls`, `phase7_pattern_sort.ls` for stragglers).
Docs: `doc/Lambda_Type.md` (primary), `doc/Lambda_Reference.md`,
`doc/Lambda_Cheatsheet.md`, `doc/Lambda_Sys_Func.md`,
`doc/Lambda_Validator_Guide.md`, `doc/Doc_Schema.md` (audit for real usage).
Note: the doc-shown `string X = ...` / `symbol X = ...` definition forms are
**not implemented** (grammar has no such statement; `string`/`symbol` are
base-type keywords only, grammar.js:1093) — their retirement (SP10) is a
docs-only change.

---

## 2. Phases

### P0 — Grammar cutover (`grammar.js` → `make generate-grammar`) — COMPLETE

- **P0.1** New opening tokens: `token('\\(')` and `token('\\symbol(')`
  (single tokens, no lexer state; `(` is not a class char so nothing
  collides post-SP2). One shared island rule:
  `pattern_island: seq(choice('\\(', '\\symbol('), _pattern_expr, ')')` with
  the tag captured as a field. The island is a **`primary_type` alternative**
  — patterns become usable inline anywhere a type is (`"5" is \(d+)`,
  `case \(d+):`), not only in `type` definitions. (Design bonus consistent
  with SP14's self-contained-literal rationale; call it out in the PR.)
- **P0.2** Interior: rename `_string_type_expr` → `_pattern_expr`, reachable
  **only** from the island. Move `concat_type`, `grouped_type`,
  occurrence-in-pattern inside.
- **P0.2a** **`range_type` unchanged** (SP5 rev 4a — the `X to Y` overload
  is purposeful and stays): it remains in `primary_type` for all literal
  operands (`0 to 255`, `"a" to "z"`, match arms), AND is reachable from the
  island interior, where the AST builder compiles string-operand ranges as
  RE2 char classes (existing behavior, build_ast.cpp:6510). No split, no new
  parse errors for ranges. The `'range_to'` prec entry is untouched.
  *(The pre-existing runtime gap — `fn_to` is integer-only, so expr-space
  `"a" to "z"` errors — is folded into this plan as **P5**.)*
- **P0.2b** Drop `concat_type`'s `prec.dynamic(-1)` and the pattern entries
  from the shared precedence ladder (:220–230) — the island has its own
  ladder.
- **P0.3** Char classes: `pattern_char_class: choice('d','w','s','a','.')`
  plus the `...` any-string spelling, as island-interior rules. Exact
  single-letter class spellings are recognized as classes; longer identifiers
  remain named pattern references. Keep the rule shape string-alternation
  (`choice(...)`) so SPO1 can later add multi-letter names without
  re-architecting.
- **P0.4** String literals only inside the island; `'...'` symbol literals
  are **not** a parse alternative there (SP13) — excluded at the grammar
  level, with the AST builder owning the friendly diagnostic (P1.4).
- **P0.5** Delete retired surface: `\d`-style tokens and `pattern_char_class`
  from `primary_type`, and `_string_type_expr` from `type_assign`.
  (`range_type` stays — P0.2a.)
- **P0.6** `that_constraint` untouched (SP9 rev 4).
- **Gate:** `make generate-grammar` clean (no new conflicts);
  `parser.c`/`ts-enum.h` regenerate; parser size expected to **shrink**
  (concat/dynamic-prec removal) — record before/after size in the PR.

### P1 — AST builder (`lambda/runtime/build_ast.cpp`) — COMPLETE

- **P1.1** Delete `cst_has_pattern_nodes`; `build_let_and_type_stam` routes
  on the island node symbol. Multi-declare `type A = \(...), B = \(...)`
  keeps working (cursor loop unchanged).
- **P1.2** Wire `is_symbol` from the island tag — replace the hardcoded
  `false` at :6059. This is the **first time symbol patterns are definable**;
  treat the whole symbol path as new-feature work, not a port:
  `pattern_full_match` receives symbol values' content;
  `matches(item, T)` (D3.2.1) must check the **domain first**
  (string vs symbol TypeId) then content — a string never matches a
  `\symbol(...)` type and vice versa.
- **P1.3** Update `build_pattern_char_class` for the new single-letter CST
  tokens; update `build_concat_type` / `build_string_pattern` for island
  shapes. Reserved-name rule (SP3): inside the island, `d w s a` lex as
  classes by construction; add an AST-builder diagnostic for the shadowing
  case (user defined `type d = ...` and references it inside a pattern —
  tell them the name is reserved in pattern scope).
- **P1.4** Diagnostics: symbol literal inside island → "patterns are
  content-only; use \symbol(...) for the symbol domain, string literals for
  content" (SP13/SP15); unterminated island → the bounded missing-`)` error.
- **P1.5** `TypePattern.source` rendering: emit the new surface syntax
  (bare classes, tag prefix) so error messages and any type printing match
  what users write.
- **P1.6** SP7 (literal-union equivalence): normalize a **literal-only**
  pattern (`\("a" | "b")` — unions of string literals, no classes/occurrence/
  concat) into the same `Type` representation as the bare union
  `"a" | "b"` at build time. Cheap, and makes SP7 hold under D3.1.2's
  representational comparison for free. Patterns with any structural
  construct compile to `TypePattern` as today.
- **Gate (historical):** debug build; the final full gate is recorded in §7
  after the corpus migration.

### P2 — Runtime seam for symbol patterns — COMPLETE

- **P2.1** `compile_pattern_ast`: no regex-level change (content language is
  identical); confirm `is_symbol` only gates the domain check, not the
  compiled RE2.
- **P2.2** `is` / `match` dispatch (`lambda-eval.cpp`, `transpile-mir.cpp`):
  domain check before `pattern_full_match_chars` for symbol values (symbols'
  char content is available — reuse the existing chars accessor).
- **P2.3** `find`/`replace`/`split` (partial-match consumers): **string
  patterns only**, unchanged. Passing a `\symbol(...)` type where a search
  pattern is expected → `ItemError` + `log_error` (these functions operate
  on strings; a symbol-domain pattern is a domain error, not a silent
  content match). If a real use case appears later, relax by ruling, not by
  accident.
- **P2.4** Validator (`validate.cpp`) and Python module (`py_stdlib.cpp`)
  consume `TypePattern` opaquely — audit both for `is_symbol` assumptions;
  expected no-op.
- **Gate:** targeted GTest for the new dispatch (extend the suite covering
  pattern ops); no behavior change for string patterns.

### P3 — Corpus migration (tests) — COMPLETE

- **P3.1** Mechanically rewrite each active in-repo corpus script: wrap each
  structural pattern definition in `\(...)`, strip backslashes from classes
  (`type digits = \d+` → `type digits = \(d+)`). Goldens (`*.txt`) should
  be **unchanged** unless they contain printed pattern sources — verify,
  and where sources print, update per P1.5.
- **P3.2** New tests (CLAUDE.md rule 8 — each new `.ls` gets its `.txt`):
  - `symbol_pattern.ls` — `\symbol(...)` definitions; `is`/`match` on
    symbols; domain rejection (string vs `\symbol(...)` and the converse);
    content-only reuse of a named string pattern inside `\symbol(...)`
    (SP15); `find()` with a symbol pattern → error (P2.3).
  - `string_pattern_syntax.ls` — island edge cases: nested groups,
    `\( ... )?` occurrence on groups, literals containing `\n`/`\\`/`"`
    escapes and `(`/`)` characters, `...` any-string, `!d` negation,
    `to`-ranges, inline `is \(d+)`, multi-declare, SP7 equivalence
    (`("a"|"b") == type-of \("a"|"b")` via `is` behavior both ways);
    **range-overload regression** — `case 90 to 100:`, `type Byte = 0 to
    255`, and `let x: "a" to "z"` (range type in annotation position) all
    still parse (P0.2a: no split); `\("a" to "z")` compiles as a char class.
  - Negative diagnostics: old bare syntax now errors (one guarded script or
    transpile-error test per `transpile_error_ret_types.ls` pattern);
    symbol literal inside island; reserved-name shadowing message.
- **P3.3** Audit `type_pattern.ls`, `phase7_pattern_sort.ls`, validator
  schema corpus (`test/lambda/validator/*.ls`), and the active JetStream
  regex fixture for stragglers; no removed bare pattern syntax remains in
  those Lambda-language fixtures.
- **Gate:** `make test-lambda-baseline` 100% (per CLAUDE.md; verify flaky
  heavies standalone per the known parallel-flakiness caveat before calling
  regression).

### P4 — Docs and formal spec (SP12) — COMPLETE

- **P4.1** `doc/Lambda_Type.md`: rewrite §String Patterns — delimiter,
  bare classes, `\symbol(...)`; §Symbol Patterns → enumerations as bare
  literal unions + structural via `\symbol(...)`; delete the unimplemented
  `string X =`/`symbol X =` forms.
- **P4.2** Sweep `doc/Lambda_Reference.md`, `doc/Lambda_Cheatsheet.md`,
  `doc/Lambda_Sys_Func.md` (find/replace/split examples),
  `doc/Lambda_Validator_Guide.md`, `doc/Doc_Schema.md`.
- **P4.3** Formal spec ruling (rule 17): add the surface-syntax S-point under
  §S11 (delimited island, tag-carries-domain, content-only composition,
  literal-union equivalence; `that (...)` parens reaffirmed) + semver bump;
  design doc ledger already carries the decision record.
- **Gate:** doc grep for `\\[dwsa.]` in Lambda docs returns only
  LaTeX/regex-of-other-languages contexts.

### P5 — String-range values: the third leg of the `to` overload — COMPLETE

SP5 rev 4a rules `X to Y` one denotation with three compilations; two of the
three work today (type membership for ints, RE2 char class in patterns) but
the **value-space reading for strings is unimplemented**:
`fn_to` (`lambda-eval.cpp:624`) handles int64 operands only and falls to
`log_error("unknown range type") → NULL` for `"a" to "z"`. Since
`build_range_type` (`build_ast.cpp:7048`) routes type-position ranges
through the same evaluation, `case "a" to "z":` match arms and
`x is ("a" to "z")` fail at runtime too. This phase closes the gap so the
overload is real in all three contexts.

- **P5.1** **Semantics ruling first** (rule 17): the S4.8 area covers int
  ranges (successor guard); the adopted S11.1.3 string-range ruling states:
  operands must each be a **single Unicode codepoint** string (multi-char or
  mixed int/string operands → `ItemError`, same refuse-don't-guess posture
  as the 2⁵³ guard); the range denotes the sequence of consecutive
  **codepoints** start..end, each materializing as a one-char string. This
  matches the RE2 char-class compilation (`[a-z]` is a codepoint range), so
  `\("a" to "z")` and `x is ("a" to "z")` accept exactly the same
  single-char set — the coherence that justifies the overload. Update both
  the formal spec (with SP12's S-point or alongside it) and this doc.
- **P5.2** **Representation decision**: implemented option (a): `Range`
  keeps `start/end/length` as codepoints plus an `is_char` discriminator, so
  `(…) is range` remains true. Type-position bounds use a `TypeRange` contract
  carrying the static bounds; one-character strings materialize on access,
  never eagerly.
- **P5.3** `fn_to`: accept the single-codepoint string case per P5.1;
  keep the existing int path and error taxonomy untouched.
- **P5.4** Membership: `is` / match arms on char ranges = value is a
  single-char string whose codepoint lies in [start, end] — one predicate
  shared with the validator path. Verify MIR Direct emission for range-arm
  dispatch needs no new opcodes (it evaluates through `fn_to` + membership
  like int ranges).
- **P5.5** Tests (rule 8): `string_range.ls` + golden — materialization
  (`len("a" to "e")`, indexing, `for` iteration), membership
  (`case "a" to "z":`, `"m" is ("a" to "z")`, `"aa" is …` false), error
  cases (multi-char operand, mixed `1 to "z"`), and the coherence check:
  same acceptance as `\("a" to "z")` for every tested value.
- **Gate:** baseline stays 100%; the P3.2 range-overload regression cases
  extend to cover value-space materialization.

## 3. Dependency order

P0 → P1 → {P2, P3.1} → P3.2/P3.3 → P4. P0+P1+P3.1 landed in the **same
change** (atomic cutover, §4) — the tree did not build a green baseline
between them. P2, P4, and P5 landed in the same implementation era. **P5 is
runtime-only and independent of the
syntax cutover** — it can land before, with, or after P0–P4, but P5.1's
semantics ruling should be settled alongside SP12's spec update so the
S-points land together; P5.5's coherence test needs the island syntax, so
  the *tests* trailed P0 and are now landed.

## 4. SPO4 resolution: atomic cutover

Dual-parse (accepting both syntaxes during a window) is rejected: the active
pattern corpus and docs are all in-repo; the bare form's grammar entanglement
(shared precedence ladder, dynamic precedence, content-detection heuristic)
is exactly what SP8/SP10 delete, and keeping both alive reproduces every
hazard the design removes. One PR: P0+P1+P2+P3, docs following. External
scripts break loudly (parse error at the old syntax) with an unambiguous
rewrite rule — acceptable pre-1.0.

## 5. Risks and watch-items

| # | Risk | Mitigation |
|---|------|------------|
| R1 | Single-letter class tokens vs `identifier` inside the island (tree-sitter keyword capture subtleties, e.g. pattern ref `dd` vs class `d` + junk) | P0 gate: grammar conflict check + `string_pattern_syntax.ls` cases `dd`-style names; classes win only on exact single-letter match |
| R2 | `\symbol(` is a two-part-looking token; tree-sitter must not offer `\(` + `symbol` as an alternative parse | single `token('\\symbol(')` with lexical precedence over `\(`; test `\symbol (x)` with a space **fails** (tag is part of the token — document it) |
| R3 | Goldens silently depending on old `source` rendering | P3.1 explicitly diffs goldens; P1.5 lands before P3 |
| R4 | Symbol-domain dispatch regressing string-pattern hot paths (`is`/`match` are on the transpiler fast path) | domain check is a TypeId compare before the existing call; MIR emission diff via `temp/mir_dump.txt` on a pattern-using script |
| R5 | Validator schemas in the wild using bare syntax | in-repo corpus audited in P3.3; external breakage accepted per §4 |
| R6 | SPO1 lock-in | P0.3 keeps class tokens a plain `choice(...)` — adding `digit`/`letter` later is additive |

## 6. Explicitly out of scope

- SPO1 class-name namespace (deferred by ruling — this plan ships `d w s a . ...` only).
- Any `that`-clause change (SP9 rev 4: parens stay).
- Regex-engine features (captures, flags, lazy quantifiers); future `\tag(...)` variants (SP16 — each needs its own ruling).
- C2MIR path (frozen, rule 14) — pattern dispatch changes touch MIR Direct only.

## 7. Verification ledger (2026-08-07)

- `make generate-grammar` — PASS; regenerated `parser.c`, Tree-sitter grammar
  artifacts, and `lambda/runtime/ts-enum.h`.
- `make build` — PASS; 0 build errors.
- Focused pattern corpus — PASS: `string_pattern`, `string_pattern_ops`,
  `string_pattern_syntax`, `string_range`, and `symbol_pattern`.
- Negative diagnostics — PASS in `test_lambda_errors_gtest`: old bare syntax,
  symbol literals in islands, and reserved class bindings are covered.
- `make build-test` — PASS; 0 build errors.
- `make test-lambda-baseline` — PASS: input 2104/2104, Lambda 1522/1522,
  combined 3626/3626.
- `make test262-baseline` — PASS: 40261/40261 fully passing, 0 failures,
  0 retries, and 0 regressions against the recorded baseline commit
  `673e9bacbe28590f501e2dcd817aadcc31899191`.

The implementation is closed: syntax, AST/MIR lowering, symbol-domain
dispatch, partial-operation rejection, literal-union normalization, string
ranges, migrated corpus, formal rulings, and required gates are synchronized.
