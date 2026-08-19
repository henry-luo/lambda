# Lambda Grammar Reduce5: External Scanners for Type, String-Pattern, and Path Sub-DSLs

- **Date:** 2026-08-18
- **Status:** **TYPE/ISLAND + RETURN/WHOLE-VIEW + PATH-BODY EXTERNAL SEAMS LANDED AND BASELINE-GREEN (2026-08-19).** Production uses first-party external tokens for full type patterns, string/symbol islands, declaration return contracts, whole view patterns, and complete static path bodies. Tree-sitter retains only the `/.` / `.` path introducer; Lambda parses the full path span directly into `AstPathNode`. A contextual qualified-name-head token protects `<svg.rect>` and repeated `svg.width:` attributes without changing ordinary value member/provider syntax. `make test-lambda-baseline` passes 3,812/3,812. Differential full-grammar/span coverage and the optional base-type extraction remain outstanding.
- **Design authority:** `vibe/Lambda_Type_Pattern.md` §3 — ledger CT1v2–CT10, ALL decided (that-clause at statement-level slots only; value-annotation `^` dropped; nested map/element pure patterns; parens hold simple patterns; `type_pattern` embeds zero expressions). Formal: S11.1.2 (islands), S11.2.1 (type match), S7.5.1 (raised-channel acknowledgment), S10.1.1/S10.1.3 (unions and lexical `~`), S10.4.1–S10.4.3 (parent navigation), S10.5.1–S10.5.3 (root navigation), D8.1.1v2 (grammar → typed AST pipeline), and SO9.
- **Scope:** Lambda has three nested sub-DSLs whose interiors do not need Tree-sitter's general expression parser: **type patterns**, **string/symbol patterns**, and **paths**. Reduce5 moves their scanner-safe extents into first-party external C tokens and parses every sub-DSL interior on the Lambda side. For paths, Tree-sitter owns only the `/.` / `.` introducer so division, member access, and `.123` floats remain ordinary grammar decisions. Type patterns have three distinct entry forms: a full type pattern, a restricted view type pattern, and a restricted declaration return type.
- **Related:** `vibe/Lambda_Grammar_Reduce4.md` (size-campaign method + metrics), `lambda/tree-sitter-lambda/src/scanner.c` (first-party external scanner), Makefile grammar pipeline (`grammar.js → parser.c → ts-enum.h` via pinned CLI + `utils/update_ts_enum.sh`).
- **ID series:** `SC0`–`SC7` records the landed type/island campaign. `GR5-P0`–`GR5-P7` below records the Reduce5 extension, with landed work and outstanding follow-up tasks separated explicitly.

## 0. Architecture: two grammars, one scanner, Lambda-side parsers

**`grammar-lambda.js` — the official full grammar.** Contains the complete surface syntax including the type-pattern and string-pattern rule sets. It is the normative reference for what the language accepts (alongside the formal spec), and it generates a *reference parser* used only by the differential test target (SC5). Its `name` field is `lambda_full` so the generated `tree_sitter_lambda_full()` can link beside the production parser in a test executable. Precedent for multiple grammar files in this package: `grammar-mark.js` already lives there.

**`grammar.js` — the trimmed production grammar.** Generates the shipped `src/parser.c`. Its full annotation type-patterns, value-position string/symbol islands, whole view patterns, declaration return types, and complete dotted path bodies are external tokens. The grammar keeps the two path introducers and all ordinary statements/expressions. Qualified element/attribute names retain their structural `dotted_name` rule, but their first segment is contextual external token `dotted_name_head_token` so the parser commits before a following `.name` can be shifted as a relative path.

**`grammar-common.js` — the shared core.** Both grammar files `require()` it (the tree-sitter CLI runs under Node, so module sharing works). It exports the rule set both grammars share — all tokens, expressions, statements, precedences and conflicts fragments — and each grammar file composes `common + its type layer`: the full rules in `grammar-lambda.js`, the external-token stubs in `grammar.js`. This honors the no-duplication rule: the intentional delta between the two grammars is *only* the type layer, and a `make grammar-sync-check` target diffs the composed rule inventories to catch drift.

**`scanner.c` — first-party end-finder only.** The active tokens, beside contextual `_start`, are `TYPE_PATTERN`, `PRIMARY_TYPE_PATTERN`, `PATTERN_ISLAND`, `CONTENT_TYPE`, `VIEW_PATTERN`, `RETURN_TYPE`, `PATH_BODY`, and `DOTTED_NAME_HEAD`. The last token is a contextual collision shield, not another sub-DSL parser: it spans only the first identifier/symbol of an element/attribute qualified name and leaves the remaining dot/segments structural. `VIEW_ATOM` was removed once whole-view parsing landed. The scanner remains single-pass and stateless — scanner serialization stays empty.

**`lambda/runtime/parse_type_pattern.cpp` — the hand parser.** Recursive descent over token source text emits the **same typed AST shapes and attached `Type*` graphs** expected by the runtime; it no longer assumes a `Type*`-only AST is sufficient. Nothing inside an annotation pattern needs expression AST: the `that` predicate stays *outside* the token as a normal CST expression, so the pattern interior remains a pure contract. The target adds narrow `parse_view_pattern_text`, `parse_return_type_text`, and `parse_path_expr_text` entry points rather than broadening one parser until it accidentally consumes a surrounding Lambda construct.

Data flow after landing: `source → parser.c (scanner.c emits bounded type-form/path-body tokens; grammar preserves each path introducer and qualified-name structure) → build_ast.cpp → Lambda-side parser reconstructs runtime-compatible AST nodes + attached Type* data or a direct AstPathNode from the full source span → typed AST`. `AST_NODE_CONSTRAINED_TYPE` still wraps `{Type* base, AstNode* predicate}`.

**Design note.** Tree-sitter offers no per-node custom data — `TSNode` is `{context[4], id, tree}` (vendored `api.h:128`); a scanner reports only a symbol and its extent. The alternative of building `Type*` inside the scanner (side table keyed by source span) and the pure-`token()`-rule variant are both recorded with rationale in **Appendix A**; adopted: end-finder scanner + Lambda-side parser. The historical type/island campaign achieved the −17.4% parser-source reduction below; the current return/view/path size POC is recorded separately and is not implementation evidence.

**Current boundary.** Production `return_type`, `view_pattern`, and the interior of `path_expr` are external-token seams. Their historical type-form inner rules remain in the shared/full grammar but are unreachable from the shipped parser and no longer have production CST builders. Production `path_expr` is `choice(seq('/.', path_body_token), seq('.', path_body_token))`: keeping the introducer in Tree-sitter prevents the old whole-token POC from stealing division, member access, or `.123`, while the scanner removes the complete recursive path body from parser tables. In element/attribute name positions, `dotted_name_head_token` consumes only the first segment after confirming another dotted segment follows; this prevents `<svg.rect>` from becoming tag `svg` plus path `.rect`, while preserving the existing `dotted_name(identifier, identifier, ...)` CST/AST contract. Provider paths (`http.api`, `sys.config`, `file./`) intentionally keep their simple identifier/member grammar and are reclassified by the direct path parser. `base_type` remains ordinary grammar because it is also a value/member/key spelling; its attempted fixed-vocabulary token collided with broader statement candidates. Statement-level bodies, expression defaults, dynamic `path[expr]`, dynamic `value.~~`/`value./`, and `that` predicates remain ordinary Lambda grammar.

### 0.1 The three sub-DSLs and their seams

| Sub-DSL | Current production seam | Reduce5 target seam | Lambda-side responsibility | Status |
|---|---|---|---|---|
| Full type pattern | `type_pattern_token` (plus primary/content variants) | retain one full-pattern token per annotation/type slot | Parse type operators, structural types, occurrences, aliases, and embedded `fn` contracts into runtime-compatible AST and `Type*` data | landed |
| String/symbol pattern | `pattern_island_token`; islands inside a full type token | retain the island token; do **not** split its nested regex-like grammar back into TS rules | Parse `\\( ... )` / `\\symbol( ... )`, preserve load-bearing island AST for lazy regex compilation | landed; differential coverage outstanding |
| View type pattern | `view_pattern_token` | one token covering its complete restricted union | Parse atoms and `|` union into the exact nodes used by view/edit construction, then stop before params, return type, `state`, or body | landed |
| Declaration return type | `return_type_token` | one token | Parse `T`, `T^`, and `T^E`; build the existing success/error contract without changing raised-channel semantics | landed |
| Path | grammar-owned `/.` / `.` introducer plus `path_body_token`; provider paths retain identifier/member grammar | one token for the complete static body after the introducer | Parse root/scheme, authority, and static segments directly into `AstPathNode`; leave provider classification and genuinely dynamic indexing/navigation in their existing outer paths | landed |

The three top-level DSLs are therefore type pattern, string/symbol pattern, and path. View and return types are **forms of the type-pattern DSL**, not a fourth and fifth DSL. `base_type` is shared lexical support, not a standalone sub-DSL.

### 0.2 Contract at every external boundary

- The scanner finds an unambiguous token **extent**; it does not allocate runtime objects, validate the interior, or make semantic decisions. This preserves the standalone Tree-sitter scanner ABI and keeps diagnostics in Lambda code.
- The Lambda-side parser owns exact grammar, source spans, diagnostics, and AST/`Type*` parity. It must emit the shapes existing compiler consumers use; a generic wrapper is insufficient for match arms, literal patterns, element types, or island compilation.
- A path hand parser builds `AstPathNode`/`AstPathSegment` directly, never a runtime `Path*`. `Path*` allocation remains in the interpreter/MIR execution path, where the current runtime pool and lazy-resolution state are available. Tree-sitter supplies only the outer source span that establishes path-vs-member/navigation context; the parser never walks an inner path subtree.
- A scanner token must stop before the next enclosing Lambda construct. In particular, a view pattern stops before `(` parameters, a return contract, `state`, or `{` body; a return type stops before `=>` or a function/view body; a path stops before the next expression operator.
- No semantic widening is accepted merely because an end-finder can consume it. The full grammar remains the acceptance oracle until the differential target proves parity, except for a separately documented formal-language change.

The first two points follow D8.1.1v2's grammar-to-typed-AST pipeline. The form-specific rules protect S11.1.2 string/symbol islands, S11.2.1 type matching, S7.5.1 raised-channel acknowledgment, and S10.4–S10.5 path navigation.

### 0.3 Lambda-side parser structure and full-grammar mapping

Every scanner-safe sub-DSL has two deliberately separate responsibilities: `scanner.c` recognizes the exact **outer extent** in its standalone C ABI, while the Lambda-side parser recognizes the **interior** and builds typed AST. Paths retain only their grammar-level introducer. Parser rule names and test rows must map back to the full grammar, but scanner code must not share runtime allocation or diagnostic code.

| Full-grammar nonterminal / form | Production token | Lambda-side entry point | Direct result |
|---|---|---|---|
| `_type_pattern` | `type_pattern_token` | `parse_type_pattern_text` | Existing runtime-compatible type-pattern AST + `Type*` graph |
| `pattern_island` | `pattern_island_token` | island rule inside the type-pattern parser | `AstPatternIslandNode` plus its load-bearing content AST |
| `view_pattern` / `view_pattern_union` | `view_pattern_token` | `parse_view_pattern_text` | Existing atom/union node shapes consumed by view/edit construction |
| `return_type` / `return_type_pattern` | `return_type_token` | `parse_return_type_text` | Existing `TypeFunc` success/error contract representation |
| static rooted/relative path body | `path_body_token` after `/.` or `.` | `parse_path_expr_text` over the complete outer span | `AstPathNode` with static `AstPathSegment[]` |
| provider path selected by member/navigation outer AST | ordinary identifier/member grammar | `try_parse_path_expr_text` | Same `AstPathNode`; no provider-specific grammar expansion |

The direct path parser represents a **complete static path**, not merely the old initial `path_expr` leaf. The scanner and direct parser map to these reference rules:

```text
path_static_expr  := path_root_or_scheme path_static_segment*
path_root_or_scheme := '/.' | '.' | file | http | https | sys
path_static_segment := '.' path_key | '.~~' | './'
path_key          := identifier | symbol | non-negative integer | '*' | '**' | base_type
```

This does not absorb arbitrary value syntax. `path[expr]` stays an outer `AstPathIndexNode` because `expr` is Lambda expression grammar and runtime-dependent; `value.~~` / `value./` stays an `AstNavigationNode` because it applies to ordinary values and traversal context as well as paths. Static `/.a.~~./.b` produces `LPATH_SEG_PARENT` / `LPATH_SEG_ROOT` entries directly. The full grammar remains the oracle for the precise accepted spelling and precedence.

The C/C++ parser implementation must be factored by grammar rule, not by call site:

1. Extract a reusable source cursor for a bounded source span: whitespace/comments, identifiers, quoted symbols, integer literals, and delimiter/end checks. It returns source offsets, never `TSNode` children.
2. Give each grammar rule one parser function (`parse_path_root_or_scheme`, `parse_path_static_segment`, `parse_view_pattern_union`, `parse_return_type_pattern`, and so on). Public `parse_*_text` entry points compose only those rule functions.
3. Extract a shared Path-AST construction helper from today's CST builder: initialize a static path, append normal/int/wildcard/parent/root segments, and finalize authority. During migration the old CST adapter and `parse_path_expr_text` call the same helper; once parity is proven, delete the CST adapter rather than maintaining two constructors.
4. Pass the outer `TSNode` to allocation only for source span/error ownership. No path parser may call `ts_node_child_*`, inspect inner symbols, or reconstruct a member chain from Tree-sitter children.

This is also the simplification boundary. Keep `AstPathNode` + a compact `AstPathSegment[]` for a static path. Keep `AstPathIndexNode` and `AstNavigationNode` because they model genuinely dynamic Lambda expressions. `AstPathNode::file_local` is only read while the current CST builder decides whether the first segment is a file authority; after the new parser represents authority directly, remove this redundant field if its no-consumer audit remains clean. Do not introduce an AST "dynamic segment" for brackets; the existing dynamic-index wrapper is clearer and preserves expression semantics.

## SC0 — Baseline, metrics, guardrails

- Record the campaign-start baseline metrics in the table at the bottom of this doc: `src/parser.c` bytes (8,913,442 at the time), `STATE_COUNT`, `LARGE_STATE_COUNT`, `SYMBOL_COUNT`, `TOKEN_COUNT`, conflict count (extraction method per `Lambda_Grammar_Reduce4.md`). Script it as `utils/parser_stats.sh` so every stage appends a row.
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

## SC3 — External tokens in the trimmed grammar (historical initial plan)

Grammar edits (`grammar.js` composition only; `grammar-lambda.js` keeps the full rules):

- Initial externals: `[$._start, $.type_pattern_token, $.primary_type_pattern_token, $.pattern_island_token]`. The landed slice also added `content_type_token` and `view_atom_token`; its current complete list is in §0.
- `constrained_type` base → `$.type_pattern_token`. `query_expr` query field → `$.primary_type_pattern_token` (preserves `data?int | x` value-union precedence — the primary variant never consumes `|`). `view_pattern` atoms → `$.view_atom_token` (atoms are `element_type | identifier | base_type`, all primaries; keep the grammar-level `|` union between atom tokens until GR5-P3 replaces the whole form). `primary_expr`'s `$.pattern_island` → `$.pattern_island_token`.
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
- **Scanner fix** — the activated `src/scanner.c` keeps a depth-0 `^` inside the pattern when it follows a complete type, so `let f: fn(a: int) int^` scans as one token. Previously `^` terminated the pattern, which was correct for value annotations (they no longer have `^` at all) but wrong for a `fn` type's return.
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

**Activation result (completed 2026-08-19):**

1. **Wiring landed** — `build_ast.cpp` dispatches the active token symbols, returns the runtime-compatible AST shapes, and attaches island AST so `prepass_compile_patterns` can still compile lazy regexes. `pending-sc4/` has been deleted; production `scanner.c` and `grammar.js` are the source of truth.
2. **Post-activation direction** — the old self-check was the pre-activation parity gate. The durable gate is `make test-grammar-diff` (SC5), expanded by `GR5-P6` to cover the new view, return, and path token seams.

## SC5 — Differential verification (recommended, cheap once SC2 exists)

- Test-only executable links the production parser and the reference `tree_sitter_lambda_full`.
- For every `.ls` under `test/` (scripts and schemas): assert accept/reject parity between the two parsers, and for each accepted file compare the SOURCE SPAN of every production pattern token against the span of the corresponding full-grammar type subtree. `GR5-P6` extends this to a whole view-pattern subtree, return-type subtree, and path-expression subtree. Span equality is exactly the property the scanner's terminator rules must preserve — no dual type-building or output comparison needed.
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

## Reduce5 implementation record and remaining work

`GR5-P2`, `GR5-P3`, and `GR5-P4` are production external-token seams. GR5-P4 deliberately retains only Tree-sitter's unambiguous `/.` / `.` path introducer and uses a contextual qualified-name head to protect namespace syntax. Scanner branches are bounded and side-effect free on successful tokens; `parse_type_pattern.cpp` owns return/view interiors and `parse_path_expr.cpp` owns the path interior. The runtime baseline close-out is complete; the remaining work is parity automation and broader differential coverage.

### GR5-P0 — Focused corpus: partially landed

- Regenerate the production parser before every comparison; do not use the stale checked-in generated source at the package root as a baseline.
- Added `test/lambda/grammar_reduce5_scanner.ls/.txt`, covering a whole view union, a named element view, union/nullable/raised declaration returns, and a rooted static path. Existing `path.ls`, `path_v2.ls`, `type_syntax_edges.ls`, `transpile_error_ret_types.ls`, and `view_template.ls` supply the complementary focused regression rows.
- Record acceptance/rejection and AST shape from the full grammar. This is the required oracle for every later extraction.

### GR5-P1 — Keep the two-grammar seam explicit: partial

- Move only the candidate full-rule definitions into `grammar-lambda.js`; production `grammar.js` gets the corresponding external-token seam. `grammar-common.js` must not grow a production-only dependency.
- Extend `make grammar-sync-check` so its allow-list names the new form tokens and the removed full-rule inventories. The full grammar remains normative surface syntax and test-only generated artifacts stay under `./temp/`.
- Preserve the shared `[$.dotted_name, $.path_expr]` conflict in the full grammar. The direct path parser does not make this lexical grammar conflict redundant; removing it from shared core changes the reference grammar and invalidates the parity oracle.

### GR5-P2 — Extract declaration return types: landed

- Introduce `return_type_token` only at declaration/function/view return slots. Its scanner recognizes the restricted contract grammar and stops before `=>`, `state`, `{` body, or the next enclosing declaration delimiter.
- Implement `parse_return_type_text` separately from `parse_type_pattern_text`. It must produce the existing success type, optional error type, and raised-channel fields used by `TypeFunc`; `T^` and `T^E` retain their current meanings and acknowledgment behavior under S7.5.1.
- After the last direct user has migrated, retire grammar-level `return_type_pattern`, `return_occurrence_type`, `occurrence`, and `occurrence_count` only if no non-return production still references them. Do not remove them merely because an external declaration lists them.

**Implementation:** `grammar.js` maps `return_type` to `return_type_token`; `scanner.c` accepts a restricted named/base atom pattern and commits only before `{`, `=>`, or view `state`, preventing `raise error(...)` from being misclassified. Its first atom rejects declaration and control keywords (including `else`): during GLR recovery, `else {` otherwise looked like a named return contract and broke graph/Jube/PDF scripts. An incomplete `|`, `&`, or `!` tail rejects the entire token rather than committing its left identifier; that keeps an expression pipeline such as `parts |> join("")` out of the type scanner. This preserves the declaration-boundary contract without changing the S7.5.1 raised-return semantics. `parse_return_type_text` builds the existing `TypeFunc` contract through the shared `build_function_return_contract_node`; the old production CST return/occurrence builders were removed.

### GR5-P3 — Extract whole view type patterns: landed

- Replace the grammar-level `view_pattern` / `view_pattern_union` tree with one `view_pattern_token`, not a token per atom plus a grammar-level union. The token owns the complete restricted atom-and-`|` syntax.
- Implement `parse_view_pattern_text` with exact AST-node parity for element, identifier/base-type, occurrence, and union forms. It must distinguish a model pattern from the following parameter list, return contract, `state` clause, or body brace.
- Keep `view_atom_token` only until the whole-token replacement proves equivalent; then remove it and its scanner branch rather than paying for dormant externals.

**Implementation:** `grammar.js` maps `view_pattern` to `view_pattern_token`; `parse_view_pattern_text` builds element/name/base atoms and registered `|` binary nodes. The scanner declines a leading `name:` prefix and stops before params, return/state/body syntax. The formerly dormant `view_atom_token` external and scanner branch are removed.

### GR5-P4 — External path body plus direct path AST parser: landed

- Implement `parse_path_expr_text` as reusable rule functions plus a shared Path-AST builder. It creates `AstPathNode` and its compact segment array directly from the complete source span selected by the outer AST builder. Cover logical root `/.field`, relative `.field`, schemes/authority, identifiers, symbols, integer keys, `*`/`**`, base-type-named fields, `.~~`, and `./`; reject the retired bare `/`, bare `.`, and `/field` forms under S2.4.1v2.
- Preserve the AST distinctions required by S10.4.1–S10.4.3 and S10.5.1–S10.5.3: static path `.~~`/`./` becomes typed path segments, while `value.~~`/`value./` remains `AstNavigationNode`; `path[expr]` remains `AstPathIndexNode`. Neither navigation form becomes an ordinary string field or observable parent/root pointer.
- Simplify only after consumer audit: remove the CST-only `file_local` field when authority construction makes it redundant; retain `AstPathIndexNode` and `AstNavigationNode`. Decide whether `base_type_token` is a dedicated external or a scanner-internal lexical helper. It is included in the size POC because it reduced tables, but it is not independently justified until all remaining value/type uses are audited.

**Implementation:** production `path_expr` keeps `/.` or `.` in Tree-sitter and replaces the complete remaining dotted body with `path_body_token`. `scanner.c` recognizes identifier, symbol, integer, wildcard, parent, and root segments and stops before brackets/operators; `parse_path_expr.cpp` maps the full outer span plus schemes/authority directly to `AstPathNode`. `build_static_path_ast` is also the legacy CST finalizer and `AstPathNode::file_local` is removed. `build_member_expr`, `build_navigation_or_path_expr`, and `path[expr]` try the same parser before their legacy collectors, so `http.api`, `sys.config`, and `file./` remain provider paths without expanding grammar. The earlier whole-token POC failed because it accepted bare `/`, bare `.`, and `.123` and let `.name` compete directly with member access. Keeping only the introducer in grammar removes those collisions while still externalizing the recursive path DSL. A production-only `dotted_name_head_token` resolves the remaining qualified-name collision at the first segment; it is aliased back to `identifier`, so namespace AST consumers retain their prior child structure. `namespace.ls` and `namespace_v2.ls` pin simple and repeated qualified attributes.

### GR5-P5 — Consolidate string/symbol pattern coverage: outstanding

- Keep `pattern_island_token` as the one exterior boundary for `\\( ... )` and `\\symbol( ... )`; do not move the island's recursive interior back into Tree-sitter.
- Add corpus and differential rows for escaped delimiters, char classes, nested grouping, range/occurrence/negation/union, literals, and both domains. Ensure islands embedded in a full type pattern and islands used as a value produce the same load-bearing AST needed by lazy regex compilation (S11.1.2).

### GR5-P6 — Verify language and AST parity: outstanding

- Extend `make test-grammar-diff` to compare accepted/rejected files and source spans for the five production token seams: full type, string/symbol island, whole view pattern, declaration return type, and path body; also compare direct-path AST shape against the full grammar.
- Add a focused AST-shape comparison or canonical dump for consumers that dispatch by node kind (`match`, literal patterns, element patterns, Jube interfaces, path evaluation). Parser acceptance alone cannot catch the prior generic-node failure mode.
- For every scanner entry, add table-driven boundary tests whose rows are named after the matching full-grammar rule. For every Lambda-side entry, add rule-level parser tests and composition tests; a top-level `parse_*_text` pass is insufficient if an interior rule can no longer be mapped to its full-grammar counterpart.
- For each landing slice run `make generate-grammar`, the focused tests, `make test-lambda-baseline`, validator coverage, and the relevant Jube/Radiant checks. Record the known Radiant baseline failure *set* rather than treating its existing failures as a new pass criterion.

### GR5-P7 — Land, measure, and close out safely: landed

- Generate `parser.c` and `ts-enum.h` only through `make generate-grammar`; never hand-edit generated parser output. Remove dead grammar rules, scanner branches, aliases, conflicts, and builders only after the differential and runtime gates pass.
- Re-measure the generated production parser and `libtree-sitter-lambda.a` with the same compiler flags, recording scanner object growth as well as parse-table reduction. A size regression or semantic mismatch rolls the individual seam back to the full grammar while the other landed seams remain intact.
- Update this document, the grammar header comments, and user-facing type/path references with the actual landed boundary. Revise formal specification text only if accepted language or semantics change; cite the affected S# ruling in the change.

**Measured production result (2026-08-19):** after `make generate-grammar` and a forced grammar-archive rebuild, the landed external boundary is `parser.c` **6,999,409** bytes, `parser.o` **967,528** bytes, `scanner.o` **19,440** bytes, and `libtree-sitter-lambda.a` **987,512** bytes. It has 4,851 states, 814 large states, 222 symbols, 111 tokens, and nine external symbols (including `_start`). Relative to the initial path-body result, the qualified-name shield removes another 35,924 parser-source bytes, 2,872 parser-object bytes, 11 states, and 10 large states. The shield plus exact shared symbol/extras lexing adds 2,768 scanner-object bytes, leaving the archive 104 bytes smaller overall. Focused rooted/relative/provider paths, `.123`, member access, division, return boundaries, path indexing, and namespace fixtures are green. `make test-lambda-baseline` passes Input 2,104/2,104 plus Lambda Runtime 1,708/1,708, total **3,812/3,812**. `base_type_token` remains deferred because its experimental fixed-vocabulary scanner collided with broader statement candidates. The differential/AST-shape work listed in GR5-P5/P6 remains deliberately outstanding follow-up.

## Second activation (2026-08-19) — landed

The reverted first attempt (see git history of this doc) failed on runtime type matching. The root cause dissolved the "Type*-direct" half of the design: **value-position types are consumed by AST node KIND** — the MIR transpiler routes `AST_NODE_ELMT_TYPE`/`MAP_TYPE`/… to `const_type(type_index)` but sends a generic `AST_NODE_TYPE` down the base-type path (dropping an element's tag name, so `?<p>` matched every element); match arms walk `AstBinaryNode` children for or-patterns; literal arms emit from literal-typed `AST_NODE_PRIMARY` nodes. So the hand parser was rewritten to **emit the same AST node shapes the CST builders produced**, with the `Type*` graph attached — `parse_type_pattern_text` returns an `AstNode*`, and every tier mirrors its CST counterpart's node kind, fields, wrapping, and `type_list`/`const_list` registration.

Defects found and fixed on the way to green, each with the constraint it revealed:

- **`&LIT_INT`/`&LIT_BOOL` re-read source through the node's TSNode span** (`parse_int_literal(mt->source, node->node)`), and every hand node spans the whole token. Numeric/string literals therefore carry value-bearing types: pooled `TypeInt64` under `LMD_TYPE_INT`, payload `TypeFloat`, const-pool `TypeString`. Bool keeps `&LIT_BOOL` — a lone `case true:` token reads correctly; a bool inside a larger pattern would mis-emit (no corpus use).
- **`ast_static_literal_item` had the same span dependency** for `LMD_TYPE_INT`: `type Byte = 0 to 255` re-parsed the whole token as the bound (range became [0,0]). It now prefers the payload for any non-`&LIT_INT` type — the same dichotomy the transpiler's literal emitter uses.
- **The tree-sitter lexer's skip-advance moves the token START unconditionally** (`lexer.c:235`), even after `mark_end`; a start past the end clamps the token to zero width (`lexer.c:427-428`). Both activation rounds' mysterious zero-width tokens came from `':'`-peek loops that advanced with `skip=true`. Lookahead past the token must advance with `skip=false`.
- **Symbols are `Symbol` (ns before chars), not `String`** — allocating the right struct but filling through `String::chars` puts the characters at the wrong offset, and every symbol comparison reads garbage (`case 'info':` matched nothing).
- **Alias references wrap in a plain `TypeType`** — `match_arm_is_error_handler` blind-casts an arm's type as `(TypeType*)`, so a raw `TypePattern` (whose second word is `pattern_index`) reads a small int as a pointer — SEGV. `build_identifier` always wrapped type/pattern definitions; the hand parser now does the same.
- **The Jube interface reader walks the raw CST**: `jube_node_is(attr_type, "fn_type")` classified fn-typed members. With one opaque token it classifies from the token TEXT (`jube_text_is_fn_type` + `jube_parse_fn_type_text` in `jube_interface.cpp`). This single dependency took down the whole radiant/graphviz/mermaid/structurizr cluster (43 tests) via "field binding lacks a getter".
- **Scanner refinements**: `fn` joined `to` as a pattern-continuation word (`fn(a: int) int` is one token); the content/view name-decline generalized from bare words to any lone name-like atom, covering quoted field names (`'type': string`); view/edit patterns scan ONLY a bare word or one balanced `<...>` element, so a view body `{` can never be swallowed as a map pattern. The return scanner now rejects first-atom statement/control words such as `else`; it cannot consume `else {` while the GLR parser is recovering a declaration boundary. It also rejects a type connective with no RHS atom, which prevents the expression pipeline `parts |> join("")` from being tokenized as a bare alias return type; `grammar_reduce5_scanner` pins this boundary.
- **Diagnostic parity** (predicted as R4): conceptual base-type spellings (`int64`) fail with the canonical suggestion via the promoted `record_unknown_base_type`; genuinely unknown names stay LENIENT (ANY + warning) so `?unknown` queries degrade gracefully — both behaviors are load-bearing in the negative suites; island char classes reject shadowing bindings ("pattern class 'd' is reserved…"); and the `'<' ambiguous with element syntax` hint now recognizes the trimmed grammar's wider ERROR span (`< "b"`, not a lone `<`).

## Open issues to follow up

- **IS1 — a member expression cannot be element content.** `<h1 data.title>` does not parse: the element's `content` captures the bare identifier `data` and `.title` lands in an ERROR node (`(element (identifier) (content (primary_expr (identifier))) (ERROR (identifier)))`). PRE-EXISTING and unrelated to this campaign — verified by stashing every local change and rebuilding, where it fails identically. It is the direct cause of 18 `test_ui_automation_gtest` failures (all `todo*` plus two `editable_editors_prosemirror_*`), which reach it through `test/lambda/ui/todo.ls:259`. Suspected cause: inside an element, `.` lexes as `_path_prefix` (starting a `path_expr`) instead of continuing a `member_expr`; the grammar still declares a `[$.dotted_name, $.path_expr]` conflict in that neighbourhood. Consequence for planning: `make test-radiant-baseline` is NOT green on master (it also carries 12 pre-existing render pixel regressions in filters/iframes/form controls), so it cannot be used as a pass/fail gate for this work — compare failure SETS instead.

- **IS2 — newline termination is context-dependent, and the scanner cannot reproduce that.** Today `let n: int⏎ | string = 7` parses (the `let` is still unfinished, so the LR parser keeps going and `|` continues the annotation) while `type N = int⏎| string` does NOT (the type statement ends at the line break). The difference is parser state, which a lexical scanner cannot observe. The adopted scanner rule — at depth 0 a newline ends the pattern unless the pattern is unfinished or the next line opens with `|`, `&`, `!`, or `to` — preserves every currently-valid program but additionally ACCEPTS the multi-line `type N = int⏎| string`. The landing consequences remain: (a) SC5's span diff reports this as a difference against the full grammar unless it is whitelisted or the full grammar adopts the same rule; (b) the widening must remain a deliberate, documented surface change (it removes an inconsistency between `let` and `type` annotations), not an accident. `type T = int⏎[3]` still terminates, so a following array-literal statement is never swallowed.

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
| GR5 return/view production | 7,318,818 | 1,015,848 | 5,003 | 903 | 219 | 2 |
| **GR5 path body + qualified-name shield (2026-08-19)** | **6,999,409** | **967,528** | **4,851** | **814** | **222** | **1** |

GR5's current deltas versus the pre-campaign baseline are **parser.c −21.5%, parser.o −26.2%, states −24.2%**. Its archive is 987,512 bytes versus the same-build fresh SC4 archive of 1,044,432 bytes (−56,920 bytes, −5.4%); the archive includes the scanner growth for type/view/return end-finders, `path_body_token`, and the qualified-name collision shield. Production AST construction no longer rebuilds return/occurrence/view interiors from a Tree-sitter CST and no longer walks rooted/relative path interiors to construct `AstPathNode`.

SC1 is the `that`-hoist plus the `value_error_type` deletion: **−8.3% source, −7.5% object, −9.1% states**, and four of the six declared GLR conflicts became unnecessary once the type layer stopped reaching into the expression grammar (removed; they measured identically either way). SC2 is a verified no-op — identical numbers across all five counters, which is the strongest available evidence that factoring the grammar into `grammar-common.js` + a type layer changed no language. SC3 measured on the parked grammar: **−17.4% source, −21.5% object, −21.4% states against baseline.**

Two findings worth keeping:

- **Seam rules must be inlined.** The four names the shared core references (`_type_pattern`, `_primary_type`, `_view_atom_type`, `_value_island`) are pure aliases, but leaving them as ordinary hidden rules cost ~850 large states and 254 KB of `parser.o`. They are listed in `inline:` in `grammar-common.js`; without that the split is a regression, not a no-op.
- **Dormant externals are not free.** Declaring scanner tokens without wiring them into rules widens every parse-table row: the historical three-token experiment moved large states 1,004 → 1,854 and added 254 KB to `parser.o`. This is why each GR5 seam must land atomically, rather than predeclaring future token kinds in the shipped grammar.

### Historical size-only POC: return, path, base type, and whole-view candidates

The following **historical size-only** POC was generated in `./temp/` with Tree-sitter 0.24.7, then built through the grammar package's `make libtree-sitter-lambda.a` using `cc -Isrc -std=c11 -fPIC`. It measures parser-table opportunity; its candidate scanner branches deliberately reused broad type scanning and were **not semantically correct implementation code**. It is retained to explain the rejected `base_type_token` opportunity, not as the production result above.

| Candidate production seam | `parser.c` | `libtree-sitter-lambda.a` | Delta vs fresh baseline |
|---|---:|---:|---:|
| Fresh current production baseline | 7,360,111 | 1,044,432 | — |
| `return_type_token` | 7,320,839 | 1,030,912 | −39,272 source; −13,520 archive |
| return + `path_expr_token` | 6,967,913 | 975,304 | −392,198 source; −69,128 archive |
| return + path + `base_type_token` | 6,919,082 | 967,216 | −441,029 source; −77,216 archive |
| return + path + base + whole `view_pattern_token`; move now-unreferenced occurrence rules to full grammar | **6,917,462** | **965,392** | **−442,649 source (−6.014%); −79,040 archive (−7.568%)** |

The final candidate's generated parser had 4,851 states, 814 large states, 221 symbols, 110 tokens, and 10 external symbols; its objects were `parser.o` 950,408 bytes and `scanner.o` 14,440 bytes. Production does **not** claim that candidate result: its whole `path_expr_token` accepted ambiguous bare prefixes and `.123`, while `base_type_token` caused another lexical collision. The landed `path_body_token` keeps the `/.` / `.` introducer in grammar and therefore captures most of the path table reduction without those regressions. A separate external `view_pattern_union` token was intentionally not measured: whole `view_pattern_token` subsumes it, while a dormant extra external would add parse-table cost. `occurrence` and `occurrence_count` likewise do not need tokens once return types leave production grammar; they simply have no production direct consumer.

## Appendix A — Recorded alternatives for the scanner/`Type*` split

**A1 — Scanner builds `Type*` (recorded per review, not adopted).** The C scanner parses the pattern during its scan and allocates `Type*` immediately — scan/parse once. Since tree-sitter cannot attach data to nodes, the built `Type*` is handed over via a side table in the scanner payload keyed by source range/position, consulted by `build_ast` when it reaches the token. Not adopted because: (1) speculative scanning — during GLR ambiguity and error recovery the scanner runs over text that never becomes a token in the final tree (recovery even special-cases external tokens, vendored `parser.c:557`), forcing idempotent table entries and deferred diagnostics; (2) scanner serialization is a 1024-byte buffer (`parser.h:14`) — a pointer table lives outside the serialized state, off the API's intent; (3) `external_scanner_create()` takes no arguments (`parser.c:373`) — reaching the `Transpiler` requires a per-parse global; (4) `scanner.c` must stay standalone-compilable for the package's bindings builds, so it cannot include lambda runtime headers. Revisitable as a pure optimization later: a span-keyed cache behind the same `parse_type_pattern` entry points.

**A2 — Adopted: TS external scanner (end-finder) + C hand parser emitting `Type*`.** Scan once (linear end-finding, no allocation, standalone), parse once (span → `Type*` directly, full runtime context, good diagnostics). The only duplicated work is re-reading the token's few characters.

**A3 — Pure `token()` rule instead of a C scanner (evaluated, not adopted).** A `token()` rule compiles into the lexer's regular DFA, which cannot count: balanced nesting (`{a: {b: [int]}}`, island groups `\(("a"|"b")[2])`) is not a regular language, so a token rule needs bounded-depth unrolling — a silent cliff at depth K. Additional DFA awkwardness: the `that` stop-word (an identifier-run atom would swallow it; longest-match must be shaped so adjacent atoms cannot join), newline termination (`\n` before an operator ends the pattern, after an operator continues it — encodable but fragile), and comments inside patterns must become part of the token. A middle form — a 4–5 rule balanced-blob mini-grammar (parser recursion handles unbounded depth, no C code) — was also considered; it loses control of newline termination because `/\s/` extras consume the newline before the parser can act. Conclusion: the external C scanner in `src/scanner.c` is the robust carrier, and the measurements hold for it exactly — external tokens contribute nothing to parser.c.

## Appendix B — SC0 findings that constrain the scanner

Empirically pinned in `test/lambda/type_syntax_edges.ls` before any change:

- **Newline handling is context-dependent in the current grammar.** `let n: int⏎ | string = 7` parses (the `let` is unfinished, so the parser keeps going), while `type N = int⏎| string` does NOT (the type statement ends at the line break). A lexical scanner cannot see that difference, so the adopted rule is: at depth 0 a newline ends the pattern unless the pattern is unfinished or the next line opens with `|`, `&`, `!`, or `to`. This preserves every currently-valid program and additionally accepts the multi-line `type N = int⏎| string`, removing an inconsistency. `type T = int⏎[3]` still terminates, so a following array-literal statement is never swallowed.
- **`data?int | other` is a query followed by a VALUE union** (evaluates to `[1,2,9]`), which is why the query operand needs its own primary-only token.
- **Comments live inside annotations** (`{a: int, // c⏎ b: string}`), so the scanner skips them at any depth.
- **Two radiant suites were already red before this work.** `test_ui_automation_gtest` fails 18 tests and `test_radiant_render.js --baseline` reports 12 pixel regressions on an unmodified checkout; the UI failure set is byte-identical before and after (verified by stashing). The UI ones trace to `test/lambda/ui/todo.ls:259`, where `<h1 data.title>` — a member expression as element content — does not parse; the render ones are compositing diffs (filters, iframes, form controls). Neither is grammar-hoist related, but the first is a real parser bug worth its own fix.
- **Object/element constraint predicates are not evaluated at all** (SO9; `lambda-eval.cpp` keeps `is` base-type-only "until validator predicate evaluation ships"). The parenthesized and bare `that` forms behave identically, which is how CT6 was verified not to change behaviour. `test/lambda/object_constraint.ls` is an orphaned golden that encodes the unimplemented behaviour and is not run by the harness.
