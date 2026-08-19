# Lambda Design: Type Patterns — Occurrence, String Islands, Constrained Types

- **Date:** 2026-02-03 (occurrence proposal); refreshed 2026-08-18 (current doc conventions; added constrained-type hoisting §3)
- **Status:** §1 occurrence syntax and §2 string-pattern alignment LANDED (grammar `occurrence`/`occurrence_count`, pattern islands per S11.1.2); §3 constrained-type hoisting FULLY LANDED 2026-08-19 — grammar hoist, external scanner, and hand parser all ACTIVE in production (3808/3808 green; parser.c −17.4%, parser.o −21.2% vs pre-campaign; see the impl plan's metrics) (CT1v2 statement-level slots only; CT3v2 drops value-annotation `^`; CT8v2 nested map/element are pure patterns; CT9/CT10 resolved; none open)
- **Scope:** the type-pattern sub-language — occurrence quantifiers, string/symbol pattern islands, and where the `that` constraint clause sits in the grammar; parser-size consequences and the external-scanner direction for type patterns
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S11.1.1–S11.1.3 (bracket types, pattern islands, range types), S11.2.1 (match arms incl. constrained-arm spelling), S10.1.1 (`|` is union everywhere), S10.1.3 (`~` scoping innermost-wins), S10.3.1 (keyword operators), S7.4.5 (system fn failures are values, never `T^E`), S7.5.1 (`T^` must-engage receiving positions), SO9 (constrained-type predicate enforcement unowned)
- **Related:** `vibe/Lambda_Grammar_Reduce4.md` (parser-size campaign; states × symbols levers), `vibe/Lambda_Type_String_Pattern.md` / `Lambda_Type_String_Pattern2.md` (island deliberations), `doc/Lambda_Type.md` §Constrained Types, `doc/Lambda_Expr_Stam.md` §Filter (`that`)
- **ID series:** `CT#` (constrained-type hoisting decisions, §3)
- **Implementation plan:** `vibe/Lambda_Grammar_Reduce5.md` (stages SC0–SC7: hoist, grammar split — `grammar-lambda.js` official full grammar / trimmed `grammar.js` production, external scanner tokens, hand parser, differential verification)

## 1. Occurrence quantifiers (decided 2026-02, landed)

Occurrence modifiers let a type pattern state how many times a type appears in sequence/collection context. Basic `T?` / `T*` / `T+` predate this doc; the 2026-02 decision added counted forms.

### 1.1 Options considered (deliberation record)

| Option | Example | Verdict |
|--------|---------|---------|
| `T{min, max}` regex style | `int{2, 5}` | ❌ collides with map literal/type syntax `{a: int}`; braces overloaded |
| `T[min, max]` array style | `int[2, 5]` | ✅ chosen — matches C-family array types, clean exact form, no literal collision |
| `T[min:max]` slice style | `int[2:5]` | ❌ colon overloaded (map entries), awkward exact case `int[5:5]` |
| `T[min to max]` native `to` | `int[2 to 5]` | ⚠️ readable but verbose; `to` inside brackets unusual |

### 1.2 As landed

Grammar: `occurrence: '?' | '+' | '*' | occurrence_count` and `occurrence_count: '[]' | '[n]' | '[n, m]' | '[n+]'` (`grammar.js` `occurrence`/`occurrence_count`; integers only — no expressions inside the brackets).

| Syntax | Meaning | Shorthand equivalence |
|--------|---------|----------------------|
| `T[n]` | exactly n | — |
| `T[min, max]` | min..max inclusive | — |
| `T[n+]` | at least n | `T*` = `T[0+]`, `T+` = `T[1+]`, `T?` = `T[0, 1]` |
| `T[]` | any count | added at implementation |

Deviations from the original proposal, per the landed grammar:

- **No occurrence chaining** (regex-style stacking is rejected): `int[3]*` and `int[2]?` do not parse — group explicitly: `(int[3])*`, `(int[2])?` (`grammar.js` comment at `occurrence_type`).
- **`int?[]` is special-cased** as `nullable_array_type`: nullable binds before the array count, so `int?[]` is an array of nullable ints, not an optional array.
- `[n, *]` from the option survey was dropped in favor of `[n+]`.

```lambda
type IPv4 = int[4]                 // exactly 4 integers
type Coordinates = float[2, 3]     // 2D or 3D
type Polygon = Point[3+]           // at least 3 points
type RGB = (int[3])                // grouped; (T*)[2]-style grouping is the chaining escape
fn validate_range(values: int[2]) bool { values[0] <= values[1] }
```

## 2. String pattern alignment (decided, landed as islands)

The 2026-02 proposal applied the same occurrence forms to string patterns written directly in type position (`"a"[3]`, `("0" to "9")[4]`). What landed refines this per **S11.1.2**: string/symbol structural patterns are **delimited islands** — `\( ... )` for the string domain, `\symbol( ... )` for the symbol domain — rather than bare literals in the open type grammar.

Inside an island: quoted literals are strings; `d` `w` `s` `a` `.` `...` are the reserved pattern atoms; whitespace is concatenation; and the ordinary union/grouping/occurrence/negation/`to` rules apply — so §1's occurrence forms carry over unchanged. The island tag is part of the type value: matching checks the value domain before content (a string never satisfies a symbol pattern). The island is self-delimiting, which matters for §3.5's scanner direction.

```lambda
type Phone = \(d[3] "-" d[4])                  // 555-1234
type Date = \(d[4] "-" d[2] "-" d[2])          // 2026-08-18
type Lower = \(("a" to "z")[2, 8])             // 2..8 lowercase letters
```

| Regex | Lambda island | Meaning |
|-------|---------------|---------|
| `a{3}` | `\("a"[3])` | exactly 3 |
| `a{2,5}` | `\("a"[2, 5])` | 2 to 5 |
| `\d{4}` | `\(d[4])` | 4 digits |

## 3. Constrained types: hoisting `that` to the annotation top level (proposal, 2026-08-18)

### 3.1 Motivation

1. **Syntax consistency.** Value-level `that` takes an unparenthesized predicate (`items that ~ > 0`); type-level `that` today requires `that (expr)`. Hoisting the clause to the top of the annotation lets both levels share one shape: `X that predicate-over-~`, with `~` scoping already unified by S10.1.3.
2. **Removes the paren crutch.** The mandatory parens exist because `constrained_type` currently lives inside `unary_type`, where predicate value-operators collide with type operators that may follow (`|`, `&`, `!`, `[n]`, `?`, `+`, `*` exist on both sides; the grammar comment blames index expressions). Making the constrained form terminal in its slot dissolves the collision class instead of fencing it.
3. **Parser size.** `constrained_type: unary_type 'that' '(' _expr ')'` makes the full expression tables reachable from every type context (~10 annotation slots), multiplying LR states via type↔expr mutual recursion. Hoisting severs the type→expr edge. Parse tables are 92% of the 8.9 MB `parser.c` (Reduce4 stats); states × symbols is the lever.
4. **Enables type-pattern extraction.** With `that` hoisted and the nested forms closed (CT3v2, CT8v2, CT10), the `type_pattern` sub-language is fully expression-free, so it moves whole — string/symbol islands included — to an external C scanner token + hand parser, the next big size reduction (§3.5 P2).

### 3.2 Decision ledger

- **CT1v2 — Positions (decided; revised with CT8v2).** `constrained_type := type_pattern [ 'that' _expr ]` is admitted only at *statement-level* annotation slots: `type T = CT` (type_assign RHS); `let`/`assign_expr` annotations; fn/pn `parameter` annotations; `object_type` statement fields (its `attr_type` stays in the main grammar); and `case CT` match-arm patterns. Everything *inside* a `type_pattern` is pattern-only — both positional nesting (array/list elements, union/intersection operands, occurrence operands, `content_type`) and nested `name:` slots: anonymous `map_type` fields (`{a: type_pattern}`), `element_type` attrs (`<tag a: type_pattern>`), and `fn_type` params (`fn(a: type_pattern) T`) admit no `that` clause. Nest via a named type (CT10). Memorable rule: *`that` may follow a declaration's `:`, `type =`, or `case` — never inside a pattern.*
- **CT2 — Predicate extent + the tricky case (decided; doc prominently).** The predicate is an ordinary unparenthesized `_expr` extending to the slot's delimiter (`,` `=` `:` `;` `)` `}` body-`{`, statement end). `that` splits the annotation exactly once: type pattern left, predicate right. **Consequence**: `type T = int that ~ > 0 | null` parses as `int that ((~ > 0) | null)` — per S10.1.1 `|` is union on *both* sides of the fence, and after `that` it is the value-side union inside the predicate, never type alternation. To union a constrained type, name it: `type Pos = int that ~ > 0` then `type T = Pos | null` (CT10). This is the same greedy-RHS behavior value-level `that` already documents (`Lambda_Expr_Stam.md` §filter); it becomes one consistent rule. Mitigation: a lint for a type-looking operand (`null`, base-type keyword, occurrence form) as a top-level `|`/`&` operand inside a `that` predicate.
- **CT3v2 — `^` removed from value annotations entirely (decided; supersedes CT3v1's keep-bare-`T^`).** Value-position `T^` was *just* `T | error` — no side channel (`build_value_error_type`, `build_ast.cpp:8050`, desugars to plain `OPERATOR_UNION`: "`^` in a value position has no side channel"). It is also the fifth meaning of one glyph: `^` already means postfix propagation (`propagate_expr`), the `expr ^ { }` handler, and the current error inside a handler body — and since types are first-class, the collision is reachable and *silent*: `type T = int^` (type context → `int | error`) vs `let T2 = int^` (value context → propagation on the type value, a no-op yielding `int`). Ruling: **drop the shorthand — `value_error_type` is deleted; `T | error` is the sole value-annotation spelling** (`let x: int | error`, `fn (a: int | error)`, `{a: int | error}`). `^`/`^E` survive only in `return_type` — where `^` is not union sugar but the raised-channel declaration (enables `raise`, creates the caller must-engage obligation; S7.4.5, S7.5.1, `Lambda_Error_Handling.md`) — including `fn_type` returns (`fn(a: int) int^` describes a function value's channel). Migration: the corpus has exactly one value-position use (`let accepted_value: int^`); explicit-`E` value annotations were never used. S7.5.1's receiving-position list drops the `let x: T^` spelling at landing (v2).
- **CT4 — Return types unchanged (decided).** `return_type` stays `return_type_pattern`-only — no bare `that` (the `^` channel metadata and body-`{` adjacency make it the worst slot; the existing grammar comment notes simplifying `return_type` "substantially reduced the parser size"). Constrained returns go through a named type.
- **CT5 — `is`/`as` unchanged (decided).** Their RHS is `_expr` (types are first-class values, S10.1.1), so `x is int that (~ > 0)` is and remains `(x is int) that …` — the value filter. Constrained tests use named types (`type Pos = int that ~ > 0; x is Pos`). The hoist makes this existing behavior de jure: type-level `that` never appears in value-operator reach.
- **CT6 — Object-level constraint loses parens too (decided).** `that_constraint` in `object_type` bodies becomes `'that' _expr`, delimited by `,` / `;` / `}` like its `fn_stam` neighbors: `type User { name: string that len(~) > 0; that name != "admin" }`.
- **CT7 — Backward compatibility (decided; verified).** `T that (p)` remains valid — the predicate merely happens to be a parenthesized expression. The only spelling that changes meaning is a constrained type followed by more type syntax (`T that (p) | U`, `[T that (p)]`): grep of `test/lambda/*.ls` (58 `that (` sites) found zero such uses — all are value filters or already-top-level annotations. SO9 notes predicate enforcement is still unowned, so the surface is cheap to move now.
- **CT8v2 — Nested map/element types are pure patterns (decided; extended beyond attr defaults).** Inside `type_pattern`, anonymous `map_type` and `element_type` are pure pattern forms: their fields take `type_pattern` only (no `that` clause, per CT1v2) and element attr defaults are restricted to **literals** (the nested form loses `= _attr_expr`). Statement-level `object_type` and `view_stam` keep full `attr_type` — expression defaults, constrained fields, and the CT6 object-level `that_constraint` — because they live in the main grammar, where their richness costs the pattern sub-language nothing. Consequence: together with CT3v2 and CT10, `type_pattern` embeds **zero expressions**, so the P2 scanner token needs no expression re-entry of any kind.
- **CT9 — Nested `^` (resolved by CT3v2).** Moot: deleting `value_error_type` removes `^` from value-position type syntax at every depth — nested spellings (`[int^]`, `{a: int^}`) cease to exist along with the top-level ones. Spell `[int | error]`.
- **CT10 — Parens hold simple type patterns only (decided).** A parenthesized group inside `type_pattern` contains a **simple type pattern** — no constrained type in `( ... )` (grouping like `(T*)[2]` and tuple `(T, U)` are unaffected). There is no inline nesting of `that` anywhere: composing a constrained type into a union or container goes through a named alias (`type Pos = int that ~ > 0; type Scores = [Pos]`). Consequence: `type_pattern` embeds no expressions at all (with CT8v2), so a P2 scanner token covers it whole — no segment splicing, and the hand parser never sees an expression.

### 3.3 Grammar sketch (P1)

```js
// type_pattern = today's _type_expr minus constrained_type:
// unary tier (occurrence, nullable_array, negation, primary), binary tier (| & !), fn_type.
// value_error_type is DELETED (CT3v2) — `^` never appears in value-position type syntax;
// _value_type_expr collapses into _type_expr. return_type keeps `^`/`^E` (the raised channel).
// Parenthesized groups inside type_pattern hold simple type patterns only (CT10).

constrained_type: $ => prec.right(seq(
  field('base', $._type_pattern),
  optional(seq('that', field('constraint', $._expr))),   // CT2: no parens
)),

// statement-level annotation slots (type_assign, assign_expr, parameter, object_type
// attr fields, match_arm case) reference constrained_type; everything inside a pattern —
// map_type fields, element_type attrs, fn_type params, positional slots — is
// _type_pattern only (CT1v2/CT8v2). that_constraint: seq('that', field('constraint', $._expr)).  // CT6
```

Match arms: `case int that ~ > 0 : …` and `case int that ~ > 0 { … }` both work — the predicate-then-`{` boundary is the same expr-then-`{` shape the match scrutinee already exercises (`'match' _expr '{'`, greedy `prec.right`); expect at most a conflict declaration, no new mechanism.

### 3.4 Worked examples

```lambda
type Positive = int that ~ > 0
type Percentage = int that 0 <= ~ and ~ <= 100
type NonEmpty = string that len(~) > 0
type User { name: string that len(~) > 0, age: int that ~ >= 0; that name != "admin" }
let score: int that 0 <= ~ <= 100 = 95
fn grade(s: int that ~ >= 0) string { match s { case int that ~ >= 90: "A" default: "B" } }
type Pos = int that ~ > 0
type MaybePos = Pos | null                   // composing a constrained type: name it (CT10)
type Scores = [Pos]                          // positional nesting: named alias only (CT1, CT10)
```

### 3.5 Implementation phases

- **P0** — this doc. All CT decisions signed off (CT1v2–CT10; none open).
- **P1 — Hoist (grammar-only).** Restructure per §3.3; drop parens from `that_constraint`; delete `value_error_type` and collapse `_value_type_expr` into `_type_expr` (CT3v2); update `build_ast.cpp` constrained-type handling (shape unchanged: base + constraint expr) and remove `build_value_error_type`. Migrate the zero-to-few affected spellings (incl. the one `let accepted_value: int^` → `int | error`); update `test/lambda/constrained_type.ls` + expected `.txt`. **Measure**: `parser.c` bytes, `STATE_COUNT`, `LARGE_STATE_COUNT` before/after — the type↔expr severing may capture much of the size win alone.
- **P2 — Scanner extraction: `type_pattern` and islands together (committed by CT8v2/CT10).** Move the whole pattern sub-language to `scanner.c` (the scanner infrastructure existed; its former contextual `start` token was later retired by S13.1.1v2 and Grammar Reduce5's third activation); the hand parser for token contents goes in `build_ast.cpp`. Two external tokens: **(a) `type_pattern`** in annotation slots — one balanced token tracking `()[]{}<>` depth + string/comment state, terminating at depth-0 `that`, `,`, `=`, `:`, `;`, `)`, `}`, statement end (`^` cannot occur in an annotation pattern per CT3v2); `{` at depth 0 opens a `map_type` only where a primary may start, else terminates (body brace). **(b) `pattern_island`** for `\( ... )` / `\symbol( ... )` in *value* contexts — islands are first-class in `primary_expr`, so they keep their own self-delimited token; inside an annotation they are just bytes of the `type_pattern` token. This deletes the island's parallel sub-grammar (`pattern_unary_type`, `pattern_occurrence_type`, `pattern_negation_type`, `concat_type`, `string_binary_type`, `grouped_type`, `pattern_char_class`, `_pattern_expr`) and the open type grammar's annotation reachability in one move. Costs accepted: the CST loses internal type structure (highlighting/queries/corpus tests see one token; schema `.ls` files are type-dominant, so the hand parser effectively becomes the schema parser); type-syntax diagnostics move to the hand parser. **Measure** after landing with the same counters as P1.

### 3.6 Spec/doc updates on landing (P1)

Per the repo's spec-citation rule: S11.2.1 revised in place (v2 + semver bump) — the ruling's example spelling `case int that (~ > 0):` becomes `case int that ~ > 0:`; semantics (predicate arm, `~` = matched value, S10.1.3 scoping) unchanged. S7.5.1 revised (v2) — the receiving-position list drops the `let x: T^` spelling; `T | error` remains the acknowledging form (CT3v2; acknowledgment semantics unchanged). `doc/Lambda_Type.md` §Constrained Types rewritten with CT2's tricky case called out; `doc/Lambda_Error_Handling.md` scrubbed of value-annotation `T^` (return-channel sections unchanged); `doc/Lambda_Expr_Stam.md` filter section cross-references the unified rule; `doc/Lambda_Reference.md` / cheatsheet touched where `that (` or value `T^` appears.
