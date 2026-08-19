# Lambda Impl: `that`-Clause Hoist + Type/String Pattern External Scanner

- **Date:** 2026-08-18
- **Status:** **LANDED AND GREEN (2026-08-19)** — the trimmed grammar, external scanner, and node-emitting hand parser are ACTIVE; full lambda baseline 3808/3808, error-system suite 109/109, radiant failure set identical to master (the 18 pre-existing IS1 failures). `pending-sc4/` is deleted — its contents are live. Remaining follow-ups: SC5 (differential target), SC6 (spec/doc updates), SC7 cleanup — see the sections below.
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

**Design note.** Tree-sitter offers no per-node custom data — `TSNode` is `{context[4], id, tree}` (vendored `api.h:128`); a scanner reports only a symbol and its extent. The alternative of building `Type*` inside the scanner (side table keyed by source span) and the pure-`token()`-rule variant are both recorded with rationale in **Appendix A**; adopted: end-finder scanner + `Type*`-direct hand parser. A generate-only POC of the trimmed grammar measured **parser.c 8,913,442 → 7,358,115 bytes (−17.4%)** — see the metrics table and Appendix A.

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

**Landed (compiles, dormant — nothing dispatches to it yet, so the shipped behaviour is unchanged; baseline 3808/3808 and the new files are lint-clean):**

- **Shared Type-construction surface**, `lambda/runtime/type_build.hpp` — extracted rather than copied (rule 13), each verified by a full baseline run:
  - `lookup_base_type_name(tp, StrView)` — replaced `build_base_type`'s ~30-branch if/else chain with one table, now the single source of truth for base-type keywords. `any` stays special (it records explicit-any provenance).
  - `append_shape_entry_typed(tp, String* name, Type*, …)` — the AstNode-free core of the former `append_type_shape_entry`; both map/element CST call sites now go through it.
  - `parse_occurrence_count` — was `static`, now shared.
  - `record_semantic_error` — was defined in build_ast.cpp with no declaration anywhere; promoted so the hand parser reports through the same channel.
- **Element and `fn` types** — `parse_element_type` builds `TypeElmt` (tag name, attribute shape via the shared helper, literal-only defaults per CT8v2, content-schema length); `parse_fn_type` builds `TypeFunc` with a `TypeParam` chain, required/optional counts, return contract, and the raised channel (`T^`, `T^E`) — the one place `^` survives (CT3v2/CT4). Two more helpers were extracted rather than copied for this: `apply_declared_param_type` (the ~40 lines of contract/full_type policy that `build_param_expr` used inline) and `set_fn_return_contract`.
- **Scanner fix** — `pending-sc4/scanner.c` now keeps a depth-0 `^` inside the pattern when it follows a complete type, so `let f: fn(a: int) int^` scans as one token. Previously `^` terminated the pattern, which was correct for value annotations (they no longer have `^` at all) but wrong for a fn type's return.
- **Parser core**, `parse_type_pattern.cpp` (~600 lines): span lexer (whitespace, `//` and `/* */` comments), and the three tiers — `binary` (`|` `&` `!`), `unary` (prefix `!`, occurrence suffixes `?` `+` `*` `[n]` `[n,m]` `[n+]` `[]`, the `T?[]` nullable-array case), `primary` (base types, type references via `lookup_name`, string/symbol/int/float/bool literals, `[…]` bracket types with `item_patterns`, `{…}` map types with real shape entries, `(…)` grouping and tuples). Entry points `parse_type_pattern_text` / `parse_primary_type_text` return `Type*` wrapped as a type value.

**Verification harness.** `LAMBDA_TYPE_PATTERN_SELFCHECK=1` makes `build_expr` parse every complete type annotation a SECOND time with the hand parser, straight from its source text, and compare the two `Type*` graphs structurally (`type_graph_equal`). The whole corpus therefore runs through the hand parser while the shipped grammar still uses the CST path — validation BEFORE the grammar is trimmed. Off by default, one branch when off; matches log at debug level, disagreements at error level.

| run | matching | mismatched | rejected |
|---|---:|---:|---:|
| first (no islands) | 1,100 | 25 | 69 |
| after range + alias fixes | 1,127 | 18 | 49 |
| after islands | 1,127 | 65 | 0 |
| after comparator + precedence + literal-island + operator fixes | **1,181** | **0** | **0** |
| widened to `test/std` + `test/input` (171 more files) | **+224** | **0** | **0** |

**1,405 annotations, zero disagreements.** The harness earned its keep — every one of these was a real defect it caught:

- Ranges were built with `alloc_type_kind` (wrong `type_id`) and never populated `start`/`end`.
- **Unwrapping specialised types as if they were plain wrappers.** `TypePattern`, `TypeConstrained` and friends carry `LMD_TYPE_TYPE` but use `kind` for their own payload, so `((TypeType*)t)->type` reads an unrelated field. This bit twice: in the comparator (reporting phantom mismatches on every island) and in the parser's alias resolution, where a constrained alias silently degraded to its base type — dropping the constraint. Both now gate on `kind == TYPE_KIND_SIMPLE`.
- Type-operator precedence was flat; `&` must bind tighter than `!`, which binds tighter than `|`.
- Literal-only islands are ordinary literal unions, not compiled patterns (`build_ast.cpp` returns the body AST for exactly that case) — and the island's union node has to carry a real union type, since that node IS the annotation's type in this case.
- Type-level `&` maps to `OPERATOR_OR`, not `OPERATOR_INTERSECT` (see `build_binary_type`). Odd, but it is what the rest of the runtime sees; the hand parser reproduces it rather than diverging. Worth revisiting separately — see IS3.

**Islands: the one form whose AST is load-bearing.** Everything else in a pattern reduces to `Type*`, but a pattern island's regex is compiled LAZILY from its AST at MIR transpile time (`prepass_compile_patterns` for `type X = \(…)`, and the inline path in `transpile_pattern_island`, both calling `compile_pattern_ast`). So `parse_island` builds real AstNodes — exactly the kinds `compile_pattern_to_regex` accepts (`AST_NODE_PRIMARY`, `PATTERN_CHAR_CLASS`, `PATTERN_RANGE`, `PATTERN_SEQ`, `BINARY_TYPE`, `UNARY_TYPE`, `LIST_TYPE`, `IDENT`) — and the lexer accumulates them in source order on `Lexer::islands` so the wiring step can attach them to the AST where the transpiler will find them. **Attaching that list is the main correctness task left in wiring; an orphaned island AST means a pattern that never compiles its regex.**

**Remaining before activation:**

1. **Wiring** — dispatch from the type-expression switch in `build_ast.cpp` on the three token symbols, wrap the returned `Type*` in the minimal AstNode each slot needs, and attach the island list so `prepass_compile_patterns` still reaches it. Then activate `pending-sc4/` (copy in `scanner.c` and `grammar-trimmed.js`) and regenerate.
2. **Re-run the harness after activation** — it compares the hand parser against the CST path, so once the CST path is gone it becomes a no-op; keep a copy of the current zero-disagreement result as the pre-activation gate, and lean on `make test-grammar-diff` (SC5) afterwards.

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

## Second activation (2026-08-19) — landed

The reverted first attempt (see git history of this doc) failed on runtime type matching. The root cause dissolved the "Type*-direct" half of the design: **value-position types are consumed by AST node KIND** — the MIR transpiler routes `AST_NODE_ELMT_TYPE`/`MAP_TYPE`/… to `const_type(type_index)` but sends a generic `AST_NODE_TYPE` down the base-type path (dropping an element's tag name, so `?<p>` matched every element); match arms walk `AstBinaryNode` children for or-patterns; literal arms emit from literal-typed `AST_NODE_PRIMARY` nodes. So the hand parser was rewritten to **emit the same AST node shapes the CST builders produced**, with the `Type*` graph attached — `parse_type_pattern_text` returns an `AstNode*`, and every tier mirrors its CST counterpart's node kind, fields, wrapping, and `type_list`/`const_list` registration.

Defects found and fixed on the way to green, each with the constraint it revealed:

- **`&LIT_INT`/`&LIT_BOOL` re-read source through the node's TSNode span** (`parse_int_literal(mt->source, node->node)`), and every hand node spans the whole token. Numeric/string literals therefore carry value-bearing types: pooled `TypeInt64` under `LMD_TYPE_INT`, payload `TypeFloat`, const-pool `TypeString`. Bool keeps `&LIT_BOOL` — a lone `case true:` token reads correctly; a bool inside a larger pattern would mis-emit (no corpus use).
- **`ast_static_literal_item` had the same span dependency** for `LMD_TYPE_INT`: `type Byte = 0 to 255` re-parsed the whole token as the bound (range became [0,0]). It now prefers the payload for any non-`&LIT_INT` type — the same dichotomy the transpiler's literal emitter uses.
- **The tree-sitter lexer's skip-advance moves the token START unconditionally** (`lexer.c:235`), even after `mark_end`; a start past the end clamps the token to zero width (`lexer.c:427-428`). Both activation rounds' mysterious zero-width tokens came from `':'`-peek loops that advanced with `skip=true`. Lookahead past the token must advance with `skip=false`.
- **Symbols are `Symbol` (ns before chars), not `String`** — allocating the right struct but filling through `String::chars` puts the characters at the wrong offset, and every symbol comparison reads garbage (`case 'info':` matched nothing).
- **Alias references wrap in a plain `TypeType`** — `match_arm_is_error_handler` blind-casts an arm's type as `(TypeType*)`, so a raw `TypePattern` (whose second word is `pattern_index`) reads a small int as a pointer — SEGV. `build_identifier` always wrapped type/pattern definitions; the hand parser now does the same.
- **The Jube interface reader walks the raw CST**: `jube_node_is(attr_type, "fn_type")` classified fn-typed members. With one opaque token it classifies from the token TEXT (`jube_text_is_fn_type` + `jube_parse_fn_type_text` in `jube_interface.cpp`). This single dependency took down the whole radiant/graphviz/mermaid/structurizr cluster (43 tests) via "field binding lacks a getter".
- **Scanner refinements**: `fn` joined `to` as a pattern-continuation word (`fn(a: int) int` is one token); the content/view name-decline generalized from bare words to any lone name-like atom, covering quoted field names (`'type': string`); view/edit patterns scan ONLY a bare word or one balanced `<...>` element, so a view body `{` can never be swallowed as a map pattern.
- **Diagnostic parity** (predicted as R4): conceptual base-type spellings (`int64`) fail with the canonical suggestion via the promoted `record_unknown_base_type`; genuinely unknown names stay LENIENT (ANY + warning) so `?unknown` queries degrade gracefully — both behaviors are load-bearing in the negative suites; island char classes reject shadowing bindings ("pattern class 'd' is reserved…"); and the `'<' ambiguous with element syntax` hint now recognizes the trimmed grammar's wider ERROR span (`< "b"`, not a lone `<`).

## Open issues to follow up

- **IS1 — a member expression cannot be element content.** `<h1 data.title>` does not parse: the element's `content` captures the bare identifier `data` and `.title` lands in an ERROR node (`(element (identifier) (content (primary_expr (identifier))) (ERROR (identifier)))`). PRE-EXISTING and unrelated to this campaign — verified by stashing every local change and rebuilding, where it fails identically. It is the direct cause of 18 `test_ui_automation_gtest` failures (all `todo*` plus two `editable_editors_prosemirror_*`), which reach it through `test/lambda/ui/todo.ls:259`. Suspected cause: inside an element, `.` lexes as `_path_prefix` (starting a `path_expr`) instead of continuing a `member_expr`; the grammar still declares a `[$.dotted_name, $.path_expr]` conflict in that neighbourhood. Consequence for planning: `make test-radiant-baseline` is NOT green on master (it also carries 12 pre-existing render pixel regressions in filters/iframes/form controls), so it cannot be used as a pass/fail gate for this work — compare failure SETS instead.

- **IS2 — newline termination is context-dependent, and the scanner cannot reproduce that.** Today `let n: int⏎ | string = 7` parses (the `let` is still unfinished, so the LR parser keeps going and `|` continues the annotation) while `type N = int⏎| string` does NOT (the type statement ends at the line break). The difference is parser state, which a lexical scanner cannot observe. The adopted scanner rule — at depth 0 a newline ends the pattern unless the pattern is unfinished or the next line opens with `|`, `&`, `!`, or `to` — preserves every currently-valid program but additionally ACCEPTS the multi-line `type N = int⏎| string`. Two consequences to handle when SC4 lands: (a) SC5's span diff will report this as a difference against the full grammar, so it must be whitelisted or the full grammar taught the same rule; (b) the widening should be a deliberate, documented surface change (it removes an inconsistency between `let` and `type` annotations) rather than an accident — worth a line in `doc/Lambda_Type.md`. `type T = int⏎[3]` still terminates, so a following array-literal statement is never swallowed.

- **IS3 — type-level `&` builds `OPERATOR_OR`.** `build_binary_type` maps a type expression's `&` to `OPERATOR_OR` while `|` maps to `OPERATOR_UNION` and `!` to `OPERATOR_EXCLUDE`; `OPERATOR_INTERSECT` is used for the island form but not for ordinary type intersection. Pre-existing, and the hand parser deliberately reproduces it (diverging would change what every downstream consumer sees). Whether intersection is meant to be `OPERATOR_INTERSECT` — and what, if anything, currently depends on the `OPERATOR_OR` spelling — is worth settling on its own.

## Risks

- **R1 — Newline termination fidelity.** The scanner must reproduce the `'statement_end'` behavior for depth-0 newlines. Mitigation: SC0 corpus pins current behavior; SC5 span diff catches any drift across the whole test tree.
- **R2 — `{`/`(` primary-start heuristic.** Match-arm block bodies and view params depend on it. Same mitigations as R1; fallback is requiring `:` arms for constrained patterns (noted in the design doc).
- **R3 — Node-name compatibility.** Downstream `build_ast` switches key on syms that disappear; the SC3/SC4 landing is atomic per PR, and aliases keep `view_pattern`/`attr` shapes stable.
- **R4 — Error-message regression.** Hand-parser diagnostics must reach parity with tree-sitter ERROR nodes for type syntax; SC0's sample collection is the checklist, and schema files (validator) are the sensitive consumer.
- **R5 — Grammar drift between full and trimmed.** `grammar-common.js` factoring + `grammar-sync-check`; the delta list is explicit.
- **R6 — CST consumers beyond build_ast.** Audit for tree-sitter queries/highlighting/tooling that read type-node structure (none known in-repo; verify before SC3).
- **R7 — Reference-parser staging.** The temp-package `generate-grammar-full` flow must use the pinned CLI and write only under `./temp/` (offline-safe, per the Makefile's pinned-CLI rationale).

## Metrics table (filled per stage)

| Stage | parser.c | parser.o | STATE_COUNT | LARGE_STATE | SYMBOL_COUNT | conflicts |
|-------|---------:|---------:|------------:|------------:|-------------:|----------:|
| SC0 baseline | 8,913,442 | 1,311,368 | 6,401 | 1,055 | 254 | 6 |
| SC1 hoist (landed) | 8,176,888 | 1,213,240 | 5,818 | 1,004 | 255 | 2 |
| SC2 split (landed) | 8,176,888 | 1,213,240 | 5,818 | 1,004 | 255 | 2 |
| **SC4 ACTIVE (landed 2026-08-19)** | **7,360,111** | **1,033,408** | **5,032** | **903** | 225 | 2 |

Final deltas vs the pre-campaign baseline: **parser.c −17.4%, parser.o −21.2%, states −21.4%.** The runtime side is `parse_type_pattern.cpp` (987 lines) + `scanner.c` (527 lines, five external tokens) replacing ~15 CST type builders (~1,400 lines deleted from build_ast.cpp).

SC1 is the `that`-hoist plus the `value_error_type` deletion: **−8.3% source, −7.5% object, −9.1% states**, and four of the six declared GLR conflicts became unnecessary once the type layer stopped reaching into the expression grammar (removed; they measured identically either way). SC2 is a verified no-op — identical numbers across all five counters, which is the strongest available evidence that factoring the grammar into `grammar-common.js` + a type layer changed no language. SC3 measured on the parked grammar: **−17.4% source, −21.5% object, −21.4% states against baseline.**

Two findings worth keeping:

- **Seam rules must be inlined.** The four names the shared core references (`_type_pattern`, `_primary_type`, `_view_atom_type`, `_value_island`) are pure aliases, but leaving them as ordinary hidden rules cost ~850 large states and 254 KB of `parser.o`. They are listed in `inline:` in `grammar-common.js`; without that the split is a regression, not a no-op.
- **Dormant externals are not free.** Declaring the three scanner tokens without wiring them into rules widens every parse-table row: large states 1,004 → 1,854, `parser.o` +254 KB. That is why SC3 is parked as files rather than half-activated in the shipped grammar.

## Appendix A — Recorded alternatives for the scanner/`Type*` split

**A1 — Scanner builds `Type*` (recorded per review, not adopted).** The C scanner parses the pattern during its scan and allocates `Type*` immediately — scan/parse once. Since tree-sitter cannot attach data to nodes, the built `Type*` is handed over via a side table in the scanner payload keyed by source range/position, consulted by `build_ast` when it reaches the token. Not adopted because: (1) speculative scanning — during GLR ambiguity and error recovery the scanner runs over text that never becomes a token in the final tree (recovery even special-cases external tokens, vendored `parser.c:557`), forcing idempotent table entries and deferred diagnostics; (2) scanner serialization is a 1024-byte buffer (`parser.h:14`) — a pointer table lives outside the serialized state, off the API's intent; (3) `external_scanner_create()` takes no arguments (`parser.c:373`) — reaching the `Transpiler` requires a per-parse global; (4) `scanner.c` must stay standalone-compilable for the package's bindings builds, so it cannot include lambda runtime headers. Revisitable as a pure optimization later: a span-keyed cache behind the same `parse_type_pattern` entry points.

**A2 — Adopted: TS external scanner (end-finder) + C hand parser emitting `Type*`.** Scan once (linear end-finding, no allocation, standalone), parse once (span → `Type*` directly, full runtime context, good diagnostics). The only duplicated work is re-reading the token's few characters.

**A3 — Pure `token()` rule instead of a C scanner (evaluated, not adopted).** A `token()` rule compiles into the lexer's regular DFA, which cannot count: balanced nesting (`{a: {b: [int]}}`, island groups `\(("a"|"b")[2])`) is not a regular language, so a token rule needs bounded-depth unrolling — a silent cliff at depth K. Additional DFA awkwardness: the `that` stop-word (an identifier-run atom would swallow it; longest-match must be shaped so adjacent atoms cannot join), newline termination (`\n` before an operator ends the pattern, after an operator continues it — encodable but fragile), and comments inside patterns must become part of the token. A middle form — a 4–5 rule balanced-blob mini-grammar (parser recursion handles unbounded depth, no C code) — was also considered; it loses control of newline termination because `/\s/` extras consume the newline before the parser can act. Conclusion: the external C scanner (~330 lines as written in `pending-sc4/scanner.c`) is the robust carrier, and the measurements hold for it exactly — external tokens contribute nothing to parser.c.

## Appendix B — SC0 findings that constrain the scanner

Empirically pinned in `test/lambda/type_syntax_edges.ls` before any change:

- **Newline handling is context-dependent in the current grammar.** `let n: int⏎ | string = 7` parses (the `let` is unfinished, so the parser keeps going), while `type N = int⏎| string` does NOT (the type statement ends at the line break). A lexical scanner cannot see that difference, so the adopted rule is: at depth 0 a newline ends the pattern unless the pattern is unfinished or the next line opens with `|`, `&`, `!`, or `to`. This preserves every currently-valid program and additionally accepts the multi-line `type N = int⏎| string`, removing an inconsistency. `type T = int⏎[3]` still terminates, so a following array-literal statement is never swallowed.
- **`data?int | other` is a query followed by a VALUE union** (evaluates to `[1,2,9]`), which is why the query operand needs its own primary-only token.
- **Comments live inside annotations** (`{a: int, // c⏎ b: string}`), so the scanner skips them at any depth.
- **Two radiant suites were already red before this work.** `test_ui_automation_gtest` fails 18 tests and `test_radiant_render.js --baseline` reports 12 pixel regressions on an unmodified checkout; the UI failure set is byte-identical before and after (verified by stashing). The UI ones trace to `test/lambda/ui/todo.ls:259`, where `<h1 data.title>` — a member expression as element content — does not parse; the render ones are compositing diffs (filters, iframes, form controls). Neither is grammar-hoist related, but the first is a real parser bug worth its own fix.
- **Object/element constraint predicates are not evaluated at all** (SO9; `lambda-eval.cpp` keeps `is` base-type-only "until validator predicate evaluation ships"). The parenthesized and bare `that` forms behave identically, which is how CT6 was verified not to change behaviour. `test/lambda/object_constraint.ls` is an orphaned golden that encodes the unimplemented behaviour and is not run by the harness.
