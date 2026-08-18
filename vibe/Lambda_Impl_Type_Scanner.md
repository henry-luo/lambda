# Lambda Impl: `that`-Clause Hoist + Type/String Pattern External Scanner

- **Date:** 2026-08-18
- **Status:** PLAN (not started)
- **Design authority:** `vibe/Lambda_Type_Pattern.md` §3 — ledger CT1v2–CT10, ALL decided (that-clause at statement-level slots only; value-annotation `^` dropped; nested map/element pure patterns; parens hold simple patterns; `type_pattern` embeds zero expressions). Formal: S11.1.2 (islands), S11.2.1→v2 at landing (match spelling), S7.5.1→v2 at landing (drop `let x: T^`), S10.1.1, S10.1.3, SO9.
- **Scope:** implement the constrained-type grammar change (P1) and move the type-pattern + string-pattern sub-language into the external C scanner (P2); establish the two-grammar architecture — `grammar-lambda.js` as the official full grammar, trimmed `grammar.js` as the optimized production grammar.
- **Related:** `vibe/Lambda_Grammar_Reduce4.md` (size-campaign method + metrics), `lambda/tree-sitter-lambda/scanner.c` (existing external scanner: contextual `start` token), Makefile grammar pipeline (`grammar.js → parser.c → ts-enum.h` via pinned CLI + `utils/update_ts_enum.sh`).
- **ID series:** `SC0`–`SC7` (stages). Suggested PR mapping: SC1 | SC2 | SC3+SC4 together | SC5 | SC6+SC7.

## 0. Architecture: two grammars, one scanner, one hand parser

**`grammar-lambda.js` — the official full grammar.** Contains the complete surface syntax including the type-pattern and string-pattern rule sets. It is the normative reference for what the language accepts (alongside the formal spec), and it generates a *reference parser* used only by the differential test target (SC5). Its `name` field is `lambda_full` so the generated `tree_sitter_lambda_full()` can link beside the production parser in a test executable. Precedent for multiple grammar files in this package: `grammar-mark.js` already lives there.

**`grammar.js` — the trimmed production grammar.** Generates the shipped `src/parser.c`. The type-pattern sub-language is replaced by external scanner tokens; everything else (statements, expressions, return types, statement-level `object_type`) is unchanged.

**`grammar-common.js` — the shared core.** Both grammar files `require()` it (the tree-sitter CLI runs under Node, so module sharing works). It exports the rule set both grammars share — all tokens, expressions, statements, precedences and conflicts fragments — and each grammar file composes `common + its type layer`: the full rules in `grammar-lambda.js`, the external-token stubs in `grammar.js`. This honors the no-duplication rule: the intentional delta between the two grammars is *only* the type layer, and a `make grammar-sync-check` target diffs the composed rule inventories to catch drift.

**`scanner.c` — three new external tokens** (beside the existing `_start`): `TYPE_PATTERN` (annotation slots), `PRIMARY_TYPE_PATTERN` (single-primary contexts: `query_expr` operand, `view_pattern` atoms), `PATTERN_ISLAND` (`\( ... )` / `\symbol( ... )` in value contexts). All single-pass and stateless — serialization stays empty.

**`lambda/runtime/parse_type_pattern.cpp` — the hand parser.** Recursive descent over token source text emitting **`Type*` directly** — one pass from span to type graph, skipping both the TS type-node tier (already gone with the token) and the AST type-node tier (`AST_NODE_LIST_TYPE`…`AST_NODE_UNARY_TYPE`, `ast-core.hpp:117–123`, become unreachable for pattern interiors). Nothing inside a pattern ever needs expression AST: the `that` predicate lives *outside* the token as a normal CST expression, so a pattern's content is pure compile-time contract. It effectively becomes the schema parser (validator `.ls` schemas are type-dominant).

Data flow after landing: `source → parser.c (scanner.c emits opaque pattern tokens, extent only) → build_ast.cpp → parse_type_pattern.cpp builds Type* from each token span → typed AST` — patterns carry `Type*`; `AST_NODE_CONSTRAINED_TYPE` wraps `{Type* base, AstNode* predicate}`.

**Design note — rejected alternative: scanner-built `Type*` via payload side table.** Tree-sitter offers no per-node custom data: `TSNode` is `{context[4], id, tree}` (vendored `api.h:128`), and an external scanner's only outputs are `result_symbol` plus the token extent. The sole escape hatch is the scanner's own `payload` (a side table keyed by byte span, plus a per-parse global to reach the `Transpiler`, since `external_scanner.create()` takes no arguments — `parser.c:373`). Rejected because: (1) speculative scanning — during GLR ambiguity and error recovery the scanner runs for tokens that never reach the final tree (the runtime even special-cases external tokens in recovery, `parser.c:557`), forcing idempotent entries and deferred diagnostics; (2) scanner state serialization is a 1024-byte buffer (`parser.h:14`) — a pointer table must live outside the serialized state, off the API's intent; (3) `scanner.c` must stay standalone-compilable for the package's bindings builds (Cargo/npm/py scaffolding exists) — including lambda runtime headers breaks them; (4) context plumbing via globals. The single-pass goal is achieved instead by `Type*`-direct emission in the hand parser; the only duplicated work is the scanner's linear end-finding scan over tiny spans. If profiling ever says otherwise, a span-keyed cache can be added behind the same entry points without changing the architecture.

**What stays in the main grammar** (deliberately, per the ledger): `return_type`/`return_type_pattern` (already closed over `base_type | identifier` + `occurrence` + `| & !` — no `primary_type` reference, verified); statement-level `object_type`/`view_stam` bodies with full `attr_type` (expression defaults, constrained fields, CT6 object-level `that_constraint`); `base_type` keywords and literals in value contexts; `occurrence`/`occurrence_count` (used by return types).

## SC0 — Baseline, metrics, guardrails

- Record baseline metrics in the table at the bottom of this doc: `src/parser.c` bytes (8,913,442 today), `STATE_COUNT`, `LARGE_STATE_COUNT`, `SYMBOL_COUNT`, `TOKEN_COUNT`, conflict count (extraction method per `Lambda_Grammar_Reduce4.md`). Script it as `utils/parser_stats.sh` so every stage appends a row.
- Verify which parser.c is live: Makefile builds `lambda/tree-sitter-lambda/src/parser.c`; a stale `parser.c` sits at the package root — confirm and ignore/remove from consideration.
- `make test-lambda-baseline` must be 100% green before any change; also run the validator suite (schemas exercise the type grammar hardest).
- Add corpus `.ls` tests (with expected `.txt`, per repo rule) pinning CURRENT behavior for every edge the migration must preserve: multi-line annotations (newline after a trailing `|` continues vs. newline before `|` terminates — the `'statement_end'` precedence behavior); `data?int | x` (query takes primary `int`, `| x` stays value union); `(T*)[2]` and `int?[]` (no occurrence chaining; nullable-array); islands in value position (`let p = \(d[3])`); legacy `T that (p)` spellings; `case T: `/`case T { }` arms. These are the oracle for SC3's terminator rules and SC5's span diff.
- Collect representative type-syntax ERROR samples and their current messages (the hand parser must reach message parity — R4).

## SC1 — Grammar hoist (still one grammar.js; lands CT1v2/CT2/CT3v2/CT6/CT8v2/CT10)

Grammar edits:

1. Rename `_type_expr` → `_type_pattern`; remove `constrained_type` from `unary_type`; add the top-tier rule: `constrained_type: $ => prec.right(seq(field('base', $._type_pattern), optional(seq('that', field('constraint', $._expr)))))`.
2. Point the statement-level annotation slots at `constrained_type`: `type_assign` RHS, `assign_expr` let annotation, `parameter`, `object_type`'s `attr_type` type field, `match_arm` case pattern.
3. Delete `value_error_type` and collapse `_value_type_expr` into `_type_pattern` (CT3v2). Return types untouched (CT4).
4. Nested purity (CT8v2): `map_type_item`, `content_type`, `list_type`/`array_type` elements, `fn_param` → `_type_pattern`. Split `attr_type` into the statement form (unchanged: full `_attr_expr` default, `constrained_type` type) and a nested `pattern_attr_type` for `element_type` (type is `_type_pattern`, default restricted to `_non_null_literal`); alias both to `attr` for node-name compatibility.
5. `that_constraint` loses its parens: `seq('that', field('constraint', $._expr))` (CT6).
6. Parens purity (CT10): confirm `list_type`/grouping recurse into `_type_pattern` only (no constrained re-entry exists once step 1 removes it from the tier).
7. Sweep `conflicts`/`precedences` for entries referencing deleted rules; expect the `case T that pred { body }` boundary to need at most one conflict declaration (same expr-then-`{` shape as the match scrutinee).

Runtime edits:

- `build_constrained_type` (`build_ast.cpp:7756`): the constraint child is now a direct `_expr` field (no parenthesized wrapper node) — adjust field access; AST shape (base + constraint) unchanged downstream.
- Delete `build_value_error_type` (`build_ast.cpp:8050`) and its `sym_value_error_type` dispatch case (~11792); keep `build_declared_error_type` (returns still use it).
- `make generate-grammar` regenerates parser.c + `ts-enum.h` (sym values shift; the update script handles it — never hand-edit either).

Corpus migration:

- `let accepted_value: int^` → `let accepted_value: int | error` (the only value-annotation `^` in the corpus).
- `test/lambda/constrained_type.ls`: legacy `that (p)` spellings remain valid (predicate merely parenthesized) — keep them as regression rows; add unparenthesized forms, the CT2 tricky case (`int that ~ > 0 | null` → predicate owns `| null`), named-alias composition (`type Pos = ...; [Pos]`, `Pos | null`), object-body `that name != "admin"` without parens. Update expected `.txt` files.
- Docs and spec bumps deferred to SC6 (grammar/impl first, spec text lands with the release of the surface).

Acceptance: `make test-lambda-baseline` + validator suite 100%; metrics row appended (expect a first drop from severing type↔expr mutual recursion).

## SC2 — Grammar split (scaffolding only; parser output unchanged)

- Factor `grammar.js` into `grammar-common.js` (shared core) + a thin `grammar.js` composing `common + full type layer`. Verify the refactor is a no-op: regenerate and hash-compare `src/grammar.json` (or at minimum re-run the full corpus) — byte-identical output is the goal since composition order affects symbol numbering.
- Create `grammar-lambda.js` = `common + full type layer` with `name: 'lambda_full'`. This file is now the **official full grammar** — header comment states its normative role and that `grammar.js` is the optimized production form.
- Make targets: `generate-grammar` (unchanged, production); `generate-grammar-full` — stages a temp package under `./temp/ts-lambda-full/` (copies `tree-sitter.json`/`package.json`, places `grammar-lambda.js` as its `grammar.js`), runs the pinned CLI (`node_modules/.bin/tree-sitter generate`), builds a static lib for test-only linking. Never on the default build path; output stays under `./temp/`.
- `grammar-sync-check`: compares the rule-name inventory of the two compositions; the allowed delta is exactly the type layer (checked list lives in the script).

Acceptance: production build byte-stable; `generate-grammar-full` produces a working reference parser; sync check green.

## SC3 — External tokens in the trimmed grammar

Grammar edits (`grammar.js` composition only; `grammar-lambda.js` keeps the full rules):

- `externals: [$._start, $.type_pattern_token, $.primary_type_pattern_token, $.pattern_island_token]`.
- `constrained_type` base → `$.type_pattern_token`. `query_expr` query field → `$.primary_type_pattern_token` (preserves `data?int | x` value-union precedence — the primary variant never consumes `|`). `view_pattern` atoms → `$.primary_type_pattern_token` (atoms are `element_type | identifier | base_type`, all primaries; keep the grammar-level `|` union between atom tokens; alias for node-name compatibility). `primary_expr`'s `$.pattern_island` → `$.pattern_island_token`.
- Drop the type layer from the trimmed composition: `primary_type`, `unary_type`, `binary_type`, `negation_type`, `nullable_array_type`, `occurrence_type` (keep bare `occurrence`/`occurrence_count` — return types use them), `range_type`, `list_type`, `array_type`, `map_type`/`map_type_item`, `element_type`/`pattern_attr_type`, `fn_type`/`fn_param`, `pattern_island` and the whole island sub-grammar (`pattern_unary_type`, `pattern_occurrence_type`, `pattern_negation_type`, `concat_type`, `string_binary_type`, `grouped_type`, `pattern_char_class`, `_pattern_expr`), plus the type-expr precedence block and related conflicts.
- Regenerate; `ts-enum.h` picks up the token syms.

Scanner implementation (`scanner.c`; extend the existing file — enum grows, `scan` dispatches on `valid_symbols`; contexts never overlap so gating is unambiguous):

- Shared machinery: leading-extras skip (`lexer->advance(skip=true)` for whitespace, plus `//` and block comments — reuse the pattern from the `start` scan); string/symbol literal states with escapes; bracket depth tracking for `() [] {} <>`; identifier/keyword reader (reuse `is_identifier_start/continue` already in the file); `lexer->mark_end` discipline for lookahead-then-retreat (the `that` keyword needs word-boundary confirmation before deciding termination).
- `TYPE_PATTERN` terminators at depth 0: keyword `that` (word boundary); `,` `=` `;` `)` `}` `:`; newline — terminate unless the pattern is syntactically incomplete (empty so far, or trailing top-level `|` `&` `!` or `to`), mirroring the `'statement_end'` precedence behavior pinned by SC0 corpus; `{` and `(` — consume (open depth) only where a primary may start (pattern start or right after `|` `&` `!` `,`-at-depth `to` or an opening bracket), otherwise terminate (match-arm body brace, view params). Inside depth > 0 everything is pattern bytes, including `:` (map fields), `=` + literal (nested attr defaults, CT8v2), and comments. `^` is never valid inside an annotation pattern (CT3v2) — always terminate on it; the hand parser then rejects it with a teaching message pointing at `T | error`.
- `PRIMARY_TYPE_PATTERN`: one primary form only — identifier or base-type keyword (+ optional occurrence suffix), a literal, a balanced `( ) [ ] { } < >` group, or an island; never consumes `|` `&` `!` `that` or a second primary.
- `PATTERN_ISLAND`: `\(` or `\symbol(` opening tag (single-token, no interior space per the grammar), then balanced to the closing `)` with string states.
- Keep the scanner purely lexical: it finds the END of a pattern; it never validates structure — that is the hand parser's job (clean error split: scanner mis-termination shows up as a parse error at the delimiter; malformed pattern interior shows up as a hand-parser diagnostic inside the token span).

Acceptance: grammar generates cleanly; corpus files tokenize with expected spans (temporary debug dump comparing token spans against SC0 expectations). SC3 lands together with SC4 — token leaves are unbuildable until the hand parser exists.

## SC4 — Hand parser (`lambda/runtime/parse_type_pattern.cpp` + `.hpp`)

- Entry points return `Type*` directly: `Type* parse_type_pattern(Transpiler*, TSNode token)`, `Type* parse_primary_type(Transpiler*, TSNode token)`, `Type* parse_pattern_island(Transpiler*, TSNode token)`. Dispatch sites in `build_ast.cpp` (~11760) wrap the result in the minimal `AstNode` their slot requires (a single generic `AST_NODE_TYPE` carrier where a node child is structurally needed; `AST_NODE_CONSTRAINED_TYPE` holds `{Type* base, AstNode* predicate}`). Unresolved type references (`[Pos]`) record name refs resolved exactly as today's identifier type nodes are — resolution timing unchanged.
- Implements exactly the grammar `grammar-lambda.js` specifies (the normative reference): primary tier (base types, identifiers/type refs, literals, ranges `X to Y`, list/tuple `( )`, array `[ ]`, map `{ }`, element `< >`, islands) → occurrence + nullable-array + no-chaining rule → negation `!` → binary `| & !` → `fn` types; island interior per S11.1.2 (atoms `d w s a . ...`, literals, ranges, grouping, occurrence, negation, whitespace concatenation, `\symbol` domain tag).
- Reuse, don't copy (repo rule 13): the `Type*` constructors (TypeMap/TypeArray/union/shape registration) that today's CST builders call are the same ones the hand parser calls; where a CST builder's body is mostly constructor logic (e.g. `build_occurrence_type`, `build_pattern_char_class`), extract the constructor core into a shared helper. Literal parsing (numbers with suffixes, strings/escapes, symbols, datetime, binary) reuses existing helpers — do not re-implement lexing that `build_ast`/input already owns. The base-type keyword table gets one C source of truth; a unit test cross-checks it against the full grammar's `node-types.json` so the JS and C lists cannot drift silently.
- AST type-node tier retirement: audit consumers of `AST_NODE_LIST_TYPE`/`ARRAY_TYPE`/`MAP_TYPE`/`ELMT_TYPE`/`FUNC_TYPE`/`BINARY_TYPE`/`UNARY_TYPE` (transpilers, validator, interpreter). Where a consumer only reads `.type` (the `Type*` contract), delete the structural walk; keep a node kind only where codegen genuinely materializes a runtime type value from structure. Deletion list produced during impl; anything kept gets a comment naming its consumer.
- Diagnostics: token start position + interior offset → file line/col; message prefix `type-pattern:`; reach parity with the SC0 error-sample collection, including the `^`-in-annotation teaching message.
- Delete the now-dead CST type builders after the switch — with an explicit keep-list for anything still reachable from grammar-level paths (return types, statement `object_type` fields, `base_type` in value exprs).

Acceptance: `make test-lambda-baseline` and `make test-radiant-baseline` (layout uses schemas) 100%; validator suite 100%; metrics row appended — this is the big drop (type-layer states + island sub-grammar + annotation reachability all leave the tables).

## SC5 — Differential verification (recommended, cheap once SC2 exists)

- Test-only executable links the production parser and the reference `tree_sitter_lambda_full`.
- For every `.ls` under `test/` (scripts and schemas): assert accept/reject parity between the two parsers, and for each accepted file compare the SOURCE SPAN of every production pattern token against the span of the corresponding full-grammar type subtree. Span equality is exactly the property the scanner's terminator rules must preserve — no dual type-building or output comparison needed.
- Ship as `make test-grammar-diff`, opt-in (not in default `make test`), running in CI where the temp full-parser build is acceptable.

## SC6 — Spec, docs, project conventions

- Formal spec (repo rule 17): S11.2.1 revised in place → v2 + semver bump (`case int that (~ > 0):` → `case int that ~ > 0:`; semantics unchanged); S7.5.1 → v2 (drop the `let x: T^` spelling from the receiving-position list; `T | error` remains the acknowledging form).
- Docs: `doc/Lambda_Type.md` §Constrained Types rewritten (unparenthesized form, CT2 tricky case called out, named-alias composition); `doc/Lambda_Error_Handling.md` scrubbed of value-annotation `T^` (return-channel sections untouched); `doc/Lambda_Expr_Stam.md` filter section cross-references the unified `that` rule; `doc/Lambda_Reference.md` + cheatsheet touched where `that (` or value `T^` appears.
- Project conventions: update the CLAUDE.md grammar bullet — `grammar-lambda.js` is the official full grammar, `grammar.js` + `scanner.c` + `parse_type_pattern.cpp` are the production implementation; `parser.c` stays never-hand-edited; `scanner.c` is first-party (the vendored-code rule covers the tree-sitter *runtime*, not this package).
- `vibe/Lambda_Type_Pattern.md` §3 status → LANDED with date; project memory updated.

## SC7 — Metrics, cleanup, close-out

- Final metrics table: SC0 → SC1 → SC4 rows (bytes, STATE_COUNT, LARGE_STATE_COUNT, SYMBOL_COUNT, conflicts). No promised number — record actuals; Reduce4's analysis (parse tables ≈ 92% of the file; states × symbols) predicts the type layer's share leaves the tables.
- Remove dead weight: conflicts/precedences/dynamic-precedence entries tied to deleted rules; unused `prec.dynamic` on the survivors; stale aliases.
- `make lint`, full `make test`, `make layout suite=baseline`.

## Risks

- **R1 — Newline termination fidelity.** The scanner must reproduce the `'statement_end'` behavior for depth-0 newlines. Mitigation: SC0 corpus pins current behavior; SC5 span diff catches any drift across the whole test tree.
- **R2 — `{`/`(` primary-start heuristic.** Match-arm block bodies and view params depend on it. Same mitigations as R1; fallback is requiring `:` arms for constrained patterns (noted in the design doc).
- **R3 — Node-name compatibility.** Downstream `build_ast` switches key on syms that disappear; the SC3/SC4 landing is atomic per PR, and aliases keep `view_pattern`/`attr` shapes stable.
- **R4 — Error-message regression.** Hand-parser diagnostics must reach parity with tree-sitter ERROR nodes for type syntax; SC0's sample collection is the checklist, and schema files (validator) are the sensitive consumer.
- **R5 — Grammar drift between full and trimmed.** `grammar-common.js` factoring + `grammar-sync-check`; the delta list is explicit.
- **R6 — CST consumers beyond build_ast.** Audit for tree-sitter queries/highlighting/tooling that read type-node structure (none known in-repo; verify before SC3).
- **R7 — Reference-parser staging.** The temp-package `generate-grammar-full` flow must use the pinned CLI and write only under `./temp/` (offline-safe, per the Makefile's pinned-CLI rationale).

## Metrics table (filled per stage)

| Stage | parser.c bytes | STATE_COUNT | LARGE_STATE_COUNT | SYMBOL_COUNT | conflicts |
|-------|---------------:|------------:|------------------:|-------------:|----------:|
| SC0 baseline | 8,913,442 | (record) | (record) | (record) | (record) |
| SC1 hoist | | | | | |
| SC4 scanner | | | | | |
