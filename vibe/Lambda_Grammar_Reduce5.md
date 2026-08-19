# Lambda Grammar Reduce5: External Scanners for Type, String-Pattern, and Path Sub-DSLs

- **Date:** 2026-08-19
- **Status:** **TYPE/ISLAND + RETURN/WHOLE-VIEW + PATH-BODY EXTERNAL SEAMS LANDED; CONTEXTUAL `start` RETIRED; NAVIGATION UNIFIED INTO MEMBERS; GREEDY STRUCTURAL NAMESPACE NAMES LANDED; QUERY POSTFIX PRECEDENCE EXPLICIT; ZERO DECLARED GLR CONFLICTS; BASELINE-GREEN (2026-08-19); LATEST TREE-SITTER SIZE POC FINDS NO MATERIAL WIN.** Production uses first-party external tokens only for the bounded type/pattern/path seams. `start(target, args, options)` now parses through ordinary call grammar and is recognized as a concurrency system `pn` by the AST builder; the `_start` external and `start_expr` grammar node are gone. `path_parent` / `path_root` are now `member_expr.field` alternatives rather than a parallel `nav_expr` chain. S2.4.3v2 makes namespaced `dotted_name` maximal and requires `;` before a relative-path element child; the qualified-name scanner token and the former dotted-name/path conflict are gone. S7.6.3v2 puts query at the left-associative postfix/member tier, replacing the final `_expr` / `query_expr` GLR conflict with named precedence. Tree-sitter v0.26.11 ABI 14 produces the same state inventory and a parser object 272 bytes larger than the pinned v0.24.7 build; ABI 15 is 8,552 bytes larger. Differential full-grammar/span coverage and the optional base-type extraction remain outstanding.
- **Design authority:** `vibe/Lambda_Type_Pattern.md` §3 — ledger CT1v2–CT10, ALL decided (that-clause at statement-level slots only; value-annotation `^` dropped; nested map/element pure patterns; parens hold simple patterns; `type_pattern` embeds zero expressions); `vibe/Lambda_Type_Path.md` PTH16v2 (maximal namespace-qualified names; explicit `;` before relative-path content). Formal: S2.4.3v2 (names versus paths), S7.6.3v2–S7.6.4 (query at the postfix/member tier), S11.1.2 (islands), S11.2.1 (type match), S7.5.1 (raised-channel acknowledgment), S10.1.1/S10.1.3 (unions and lexical `~`), S10.4.1–S10.4.3 (parent navigation), S10.5.1–S10.5.3 (root navigation), D8.1.1v2 (grammar → typed AST pipeline), and SO9.
- **Scope:** Lambda has three nested sub-DSLs whose interiors do not need Tree-sitter's general expression parser: **type patterns**, **string/symbol patterns**, and **paths**. Reduce5 moves their scanner-safe extents into first-party external C tokens and parses every sub-DSL interior on the Lambda side. For paths, Tree-sitter owns only the `/.` / `.` introducer so division, member access, and `.123` floats remain ordinary grammar decisions. Type patterns have three distinct entry forms: a full type pattern, a restricted view type pattern, and a restricted declaration return type.
- **Related:** `vibe/Lambda_Grammar_Reduce4.md` (size-campaign method + metrics), `lambda/tree-sitter-lambda/src/scanner.c` (first-party external scanner), Makefile grammar pipeline (`grammar.js → parser.c → ts-enum.h` via pinned CLI + `utils/update_ts_enum.sh`).
- **ID series:** `SC0`–`SC7` records the landed type/island campaign. `GR5-P0`–`GR5-P7` below records the Reduce5 extension, with landed work and outstanding follow-up tasks separated explicitly.

## 0. Architecture: two grammars, one scanner, Lambda-side parsers

**`grammar-lambda.js` — the official full grammar.** Contains the complete surface syntax including the type-pattern and string-pattern rule sets. It is the normative reference for what the language accepts (alongside the formal spec), and it generates a *reference parser* used only by the differential test target (SC5). Its `name` field is `lambda_full` so the generated `tree_sitter_lambda_full()` can link beside the production parser in a test executable. Precedent for multiple grammar files in this package: `grammar-mark.js` already lives there.

**`grammar.js` — the trimmed production grammar.** Generates the shipped `src/parser.c`. Its full annotation type-patterns, value-position string/symbol islands, whole view patterns, declaration return types, and complete dotted path bodies are external tokens. The grammar keeps the two path introducers and all ordinary statements/expressions. Qualified element/attribute names use the same maximal structural `dotted_name` rule as the full grammar; the namespace separator carries the lexical and parse precedence needed to win over a simultaneously valid external path-body token.

**`grammar-common.js` — the shared core.** Both grammar files `require()` it (the tree-sitter CLI runs under Node, so module sharing works). It contains only rules both parsers actually use: ordinary tokens, expressions, statements, and shared option fragments. Each grammar then adds its replacement layer: complete structural type/view/return/path/qualified-name rules in `grammar-lambda.js`, external-token seams in `grammar.js`. Reference-only support rules such as `occurrence`, `view_pattern_union`, and `return_type_pattern` no longer appear in the production composition. `member_expr` is the sole postfix-dot chain: its `field` is an ordinary key or the special `path_parent` / `path_root` navigation operation. `make grammar-sync-check` requires every shared-core seam in both layers and rejects any rule duplicated between the core and either layer; Makefile regeneration explicitly depends on both `grammar.js` and the imported `grammar-common.js`.

**`scanner.c` — first-party end-finder only.** The seven active tokens are `TYPE_PATTERN`, `PRIMARY_TYPE_PATTERN`, `PATTERN_ISLAND`, `CONTENT_TYPE`, `VIEW_PATTERN`, `RETURN_TYPE`, and `PATH_BODY`. The former `DOTTED_NAME_HEAD` collision shield, contextual `_start` token, and `VIEW_ATOM` are removed. The scanner remains single-pass and stateless — scanner serialization stays empty.

**`lambda/runtime/parse_type_pattern.cpp` — the hand parser.** Recursive descent over token source text emits the **same typed AST shapes and attached `Type*` graphs** expected by the runtime; it no longer assumes a `Type*`-only AST is sufficient. Nothing inside an annotation pattern needs expression AST: the `that` predicate stays *outside* the token as a normal CST expression, so the pattern interior remains a pure contract. The target adds narrow `parse_view_pattern_text`, `parse_return_type_text`, and `parse_path_expr_text` entry points rather than broadening one parser until it accidentally consumes a surrounding Lambda construct.

Data flow after landing: `source → parser.c (scanner.c emits bounded type-form/path-body tokens; grammar preserves each path introducer and qualified-name structure) → build_ast.cpp → Lambda-side parser reconstructs runtime-compatible AST nodes + attached Type* data or a direct AstPathNode from the full source span → typed AST`. `AST_NODE_CONSTRAINED_TYPE` still wraps `{Type* base, AstNode* predicate}`.

**Design note.** Tree-sitter offers no per-node custom data — `TSNode` is `{context[4], id, tree}` (vendored `api.h:128`); a scanner reports only a symbol and its extent. The alternative of building `Type*` inside the scanner (side table keyed by source span) and the pure-`token()`-rule variant are both recorded with rationale in **Appendix A**; adopted: end-finder scanner + Lambda-side parser. The historical type/island campaign achieved the −17.4% parser-source reduction below; the current return/view/path size POC is recorded separately and is not implementation evidence.

**Current boundary.** Production `return_type`, `view_pattern`, and the interior of `path_expr` are external-token seams. Their structural rules and supporting `occurrence`/union/return rules live only in `grammar-lambda.js`; they are absent from the shared core and shipped rule inventory. Production `path_expr` is `choice(seq('/', '.', path_body_token), seq('.', path_body_token))`: rooted `/.` is deliberately represented by separate `'/'` and `'.'` Tree-sitter tokens. Keeping the introducer in Tree-sitter prevents the old whole-token POC from stealing division, member access, or `.123`, while the scanner removes the complete recursive path body from parser tables. Structural `dotted_name` is maximal under S2.4.3v2 in both grammars, including across extras; a relative-path element child must use the explicit `;` boundary. Provider paths (`http.api`, `sys.config`, `file./`) intentionally keep their simple identifier/member grammar and are reclassified by the direct path parser. `base_type` remains ordinary grammar because it is also a value/member/key spelling; its attempted fixed-vocabulary token collided with broader statement candidates. Statement-level bodies, expression defaults, dynamic `path[expr]`, dynamic `value.~~`/`value./`, and `that` predicates remain ordinary Lambda grammar.

### 0.1 The three sub-DSLs and their seams

| Sub-DSL | Current production seam | Reduce5 target seam | Lambda-side responsibility | Status |
|---|---|---|---|---|
| Full type pattern | `type_pattern_token` (plus primary/content variants) | retain one full-pattern token per annotation/type slot | Parse type operators, structural types, occurrences, aliases, and embedded `fn` contracts into runtime-compatible AST and `Type*` data | landed |
| String/symbol pattern | `pattern_island_token`; islands inside a full type token | retain the island token; do **not** split its nested regex-like grammar back into TS rules | Parse `\\( ... )` / `\\symbol( ... )`, preserve load-bearing island AST for lazy regex compilation | landed; differential coverage outstanding |
| View type pattern | `view_pattern_token` | one token covering its complete restricted union | Parse atoms and `|` union into the exact nodes used by view/edit construction, then stop before params, return type, `state`, or body | landed |
| Declaration return type | `return_type_token` | one token | Parse `T`, `T^`, and `T^E`; build the existing success/error contract without changing raised-channel semantics | landed |
| Path | grammar-owned `'/'`, `'.'` rooted introducer tokens or `'.'` relative introducer plus `path_body_token`; provider paths retain identifier/member grammar | one token for the complete static body after the introducer | Parse root/scheme, authority, and static segments directly into `AstPathNode`; leave provider classification and genuinely dynamic indexing/navigation in their existing outer paths | landed |

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
| static rooted/relative path body | `path_body_token` after separate `'/'`, `'.'` rooted tokens or a relative `'.'` | `parse_path_expr_text` over the complete outer span | `AstPathNode` with static `AstPathSegment[]` |
| provider path selected by a member outer AST | ordinary identifier/member grammar | `try_parse_path_expr_text` | Same `AstPathNode`; no provider-specific grammar expansion |

The direct path parser represents a **complete static path**, not merely the old initial `path_expr` leaf. The scanner and direct parser map to these reference rules:

```text
path_static_expr  := path_root_or_scheme path_static_segment*
path_root_or_scheme := '/' '.' | '.' | file | http | https | sys
path_static_segment := '.' path_key | '.~~' | './'
path_key          := identifier | symbol | non-negative integer | '*' | '**' | base_type
```

This does not absorb arbitrary value syntax. `path[expr]` stays an outer `AstPathIndexNode` because `expr` is Lambda expression grammar and runtime-dependent; `value.~~` / `value./` stays an `AstNavigationNode` because it applies to ordinary values and traversal context as well as paths. Static `/.a.~~./.b` produces `LPATH_SEG_PARENT` / `LPATH_SEG_ROOT` entries directly. The full grammar remains the oracle for the precise accepted spelling and precedence.

The C/C++ parser implementation must be factored by grammar rule, not by call site:

1. Extract a reusable source cursor for a bounded source span: whitespace/comments, identifiers, quoted symbols, integer literals, and delimiter/end checks. It returns source offsets, never `TSNode` children.
2. Give each grammar rule one parser function (`parse_path_root_or_scheme`, `parse_path_static_segment`, `parse_view_pattern_primary`, `parse_view_pattern`, `parse_return_type_pattern`, and so on). Public `parse_*_text` entry points compose only those rule functions.
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

- Historical initial externals: `[$._start, $.type_pattern_token, $.primary_type_pattern_token, $.pattern_island_token]`. The landed type slice also added `content_type_token` and `view_atom_token`; `_start` was retired in the third activation and `VIEW_ATOM` was retired when whole-view scanning landed. The current complete list is in §0.
- `constrained_type` base → `$.type_pattern_token`. `query_expr` query field → `$.primary_type_pattern_token` (preserves `data?int | x` value-union precedence — the primary variant never consumes `|`). `view_pattern` atoms → `$.view_atom_token` (atoms are `element_type | identifier | base_type`, all primaries; keep the grammar-level `|` union between atom tokens until GR5-P3 replaces the whole form). `primary_expr`'s `$.pattern_island` → `$.pattern_island_token`.
- Drop the type layer from the trimmed composition: `primary_type`, `unary_type`, `binary_type`, `negation_type`, `nullable_array_type`, `occurrence_type`, `range_type`, `list_type`, `array_type`, `map_type`/`map_type_item`, `element_type`/`pattern_attr_type`, `fn_type`/`fn_param`, `pattern_island` and the whole island sub-grammar (`pattern_unary_type`, `pattern_occurrence_type`, `pattern_negation_type`, `concat_type`, `string_binary_type`, `grouped_type`, `pattern_char_class`, `_pattern_expr`), plus the type-expr precedence block and related conflicts. Bare `occurrence`/`occurrence_count` remained during initial staging while production return types used them; GR5-P2 later moved all four return-support rules into the reference layer.
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

`GR5-P2`, `GR5-P3`, and `GR5-P4` are production external-token seams. GR5-P4 deliberately retains only Tree-sitter's unambiguous `/.` / `.` path introducer; maximal structural `dotted_name` now protects namespace syntax under S2.4.3v2. Scanner branches are bounded and side-effect free on successful tokens; `parse_type_pattern.cpp` owns return/view interiors and `parse_path_expr.cpp` owns the path interior. The runtime baseline close-out is complete; the remaining work is parity automation and broader differential coverage.

### GR5-P0 — Focused corpus: partially landed

- Regenerate the production parser before every comparison; do not use the stale checked-in generated source at the package root as a baseline.
- Added `test/lambda/grammar_reduce5_scanner.ls/.txt`, covering a whole view union, a named element view, union/nullable/raised declaration returns, and a rooted static path. Existing `path.ls`, `path_v2.ls`, `type_syntax_edges.ls`, `transpile_error_ret_types.ls`, and `view_template.ls` supply the complementary focused regression rows.
- Record acceptance/rejection and AST shape from the full grammar. This is the required oracle for every later extraction.

### GR5-P1 — Keep the two-grammar seam explicit: landed

- Move only the candidate full-rule definitions into `grammar-lambda.js`; production `grammar.js` gets the corresponding external-token seam. `grammar-common.js` must not grow a production-only dependency.
- Extend `make grammar-sync-check` so its allow-list names the new form tokens and the removed full-rule inventories. The full grammar remains normative surface syntax and test-only generated artifacts stay under `./temp/`.
- Historical landing: the full/reference grammar preserved `[$.dotted_name, $.path_expr]` while production used a contextual qualified-name head. The fifth activation superseded both: `dotted_name` now wins structurally by language rule, so the declared conflict and production-only head token are removed.

**Implementation:** `grammar-common.js` now has 107 genuinely shared rules after `start_expr` and `nav_expr` were removed. `grammar-lambda.js` owns the 41-rule structural replacement layer, including full qualified names, paths, view unions, occurrences, and return contracts. `grammar.js` owns nine production replacement rules backed by seven external tokens (the path introducer/body composition accounts for the extra rule). The sync guard rejects overlap between the shared core and either layer and requires all nine seam names in both.

### GR5-P2 — Extract declaration return types: landed

- Introduce `return_type_token` only at declaration/function/view return slots. Its scanner recognizes the restricted contract grammar and stops before `=>`, `state`, `{` body, or the next enclosing declaration delimiter.
- Implement `parse_return_type_text` separately from `parse_type_pattern_text`. It must produce the existing success type, optional error type, and raised-channel fields used by `TypeFunc`; `T^` and `T^E` retain their current meanings and acknowledgment behavior under S7.5.1.
- After the last production user has migrated, move grammar-level `return_type_pattern`, `return_occurrence_type`, `occurrence`, and `occurrence_count` to the reference layer; retain them there as the structural oracle.

**Implementation:** `grammar.js` maps `return_type` to `return_type_token`; `scanner.c` accepts a restricted named/base atom pattern and commits only before `{`, `=>`, or view `state`, preventing `raise error(...)` from being misclassified. Its first atom rejects declaration and control keywords (including `else`): during GLR recovery, `else {` otherwise looked like a named return contract and broke graph/Jube/PDF scripts. An incomplete `|`, `&`, or `!` tail rejects the entire token rather than committing its left identifier; that keeps an expression pipeline such as `parts |> join("")` out of the type scanner. This preserves the declaration-boundary contract without changing the S7.5.1 raised-return semantics. `parse_return_type_text` builds the existing `TypeFunc` contract through the shared `build_function_return_contract_node`; the old production CST return/occurrence builders were removed.

### GR5-P3 — Extract whole view type patterns: landed

- Replace the grammar-level `view_pattern` / `view_pattern_union` tree with one `view_pattern_token`, not a token per atom plus a grammar-level union. The token owns the complete restricted atom-and-`|` syntax.
- Implement `parse_view_pattern_text` with exact AST-node parity for element, identifier/base-type, occurrence, and union forms. It must distinguish a model pattern from the following parameter list, return contract, `state` clause, or body brace.
- Keep `view_atom_token` only until the whole-token replacement proves equivalent; then remove it and its scanner branch rather than paying for dormant externals.

**Implementation:** `grammar.js` maps `view_pattern` to `view_pattern_token`; `parse_view_pattern_text` builds element/name/base primaries and registered `|` binary nodes. Its internal `parse_view_pattern_primary` and scanner-side `scan_view_pattern_primary` helpers map directly to the full grammar's `_view_pattern_primary` rule. The scanner declines a leading `name:` prefix and stops before params, return/state/body syntax. The formerly dormant `view_atom_token` external and scanner branch are removed.

### GR5-P4 — External path body plus direct path AST parser: landed

- Implement `parse_path_expr_text` as reusable rule functions plus a shared Path-AST builder. It creates `AstPathNode` and its compact segment array directly from the complete source span selected by the outer AST builder. Cover logical root `/.field`, relative `.field`, schemes/authority, identifiers, symbols, integer keys, `*`/`**`, base-type-named fields, `.~~`, and `./`; reject the retired bare `/`, bare `.`, and `/field` forms under S2.4.1v2.
- Preserve the AST distinctions required by S10.4.1–S10.4.3 and S10.5.1–S10.5.3: static path `.~~`/`./` becomes typed path segments, while `value.~~`/`value./` remains `AstNavigationNode`; `path[expr]` remains `AstPathIndexNode`. Neither navigation form becomes an ordinary string field or observable parent/root pointer.
- Simplify only after consumer audit: remove the CST-only `file_local` field when authority construction makes it redundant; retain `AstPathIndexNode` and `AstNavigationNode`. Decide whether `base_type_token` is a dedicated external or a scanner-internal lexical helper. It is included in the size POC because it reduced tables, but it is not independently justified until all remaining value/type uses are audited.

**Implementation:** production `path_expr` keeps rooted `'/'`, `'.'` as two separate Tree-sitter tokens (or one `'.'` for a relative path) and replaces the complete remaining dotted body with `path_body_token`. `scanner.c` recognizes identifier, symbol, integer, wildcard, parent, and root segments and stops before brackets/operators; `parse_path_expr.cpp` maps the full outer span plus schemes/authority directly to `AstPathNode`. `build_static_path_ast` is also the legacy CST finalizer and `AstPathNode::file_local` is removed. `build_member_expr`, `build_navigation_or_path_expr`, and `path[expr]` try the same parser before their legacy collectors, so `http.api`, `sys.config`, and `file./` remain provider paths without expanding grammar. The earlier whole-token POC failed because it accepted bare `/`, bare `.`, and `.123` and let `.name` compete directly with member access. Keeping only the introducer in grammar removes those collisions while still externalizing the recursive path DSL. The first landed checkpoint used a production-only `dotted_name_head_token` to resolve the qualified-name collision at the first segment. S2.4.3v2 supersedes that workaround: both grammars use a maximal structural `dotted_name`, the conflict declaration and scanner branch are removed, and `namespace.ls` pins both `<svg .rect>` and the explicit `<svg; .rect>` content boundary.

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

**Measured production result (2026-08-19):** after `make generate-grammar`, contextual-start removal, navigation/member unification, structural greedy namespace names, and explicit query postfix precedence, the landed external boundary is `parser.c` **5,715,314** bytes, `parser.o` **800,432** bytes, `scanner.o` **17,072** bytes, and `libtree-sitter-lambda.a` **818,048** bytes. It has 4,124 states, 699 large states, 218 symbols, 109 tokens, seven external symbols, and zero declared GLR conflicts. Replacing the final `_expr` / `query_expr` conflict with the S7.6.3v2 named precedence saves 1,137,149 generated-C bytes, 149,408 parser-object/archive bytes, 738 states, and 99 large states from the preceding greedy-namespace checkpoint. Focused static-path, provider-root, parent/root navigation, qualified-name, whitespace-qualified-name, explicit relative-path-child, and arrow-body query-precedence fixtures pass. `base_type_token` remains deferred because its experimental fixed-vocabulary scanner collided with broader statement candidates. The differential/AST-shape work listed in GR5-P5/P6 remains deliberately outstanding follow-up.

## Second activation (2026-08-19) — landed

The reverted first attempt (see git history of this doc) failed on runtime type matching. The root cause dissolved the "Type*-direct" half of the design: **value-position types are consumed by AST node KIND** — the MIR transpiler routes `AST_NODE_ELMT_TYPE`/`MAP_TYPE`/… to `const_type(type_index)` but sends a generic `AST_NODE_TYPE` down the base-type path (dropping an element's tag name, so `?<p>` matched every element); match arms walk `AstBinaryNode` children for or-patterns; literal arms emit from literal-typed `AST_NODE_PRIMARY` nodes. So the hand parser was rewritten to **emit the same AST node shapes the CST builders produced**, with the `Type*` graph attached — `parse_type_pattern_text` returns an `AstNode*`, and every tier mirrors its CST counterpart's node kind, fields, wrapping, and `type_list`/`const_list` registration.

Defects found and fixed on the way to green, each with the constraint it revealed:

- **`&LIT_INT`/`&LIT_BOOL` re-read source through the node's TSNode span** (`parse_int_literal(mt->source, node->node)`), and every hand node spans the whole token. Numeric/string literals therefore carry value-bearing types: pooled `TypeInt64` under `LMD_TYPE_INT`, payload `TypeFloat`, const-pool `TypeString`. Bool keeps `&LIT_BOOL` — a lone `case true:` token reads correctly; a bool inside a larger pattern would mis-emit (no corpus use).
- **`ast_static_literal_item` had the same span dependency** for `LMD_TYPE_INT`: `type Byte = 0 to 255` re-parsed the whole token as the bound (range became [0,0]). It now prefers the payload for any non-`&LIT_INT` type — the same dichotomy the transpiler's literal emitter uses.
- **The tree-sitter lexer's skip-advance moves the token START unconditionally** (`lexer.c:235`), even after `mark_end`; a start past the end clamps the token to zero width (`lexer.c:427-428`). Both activation rounds' mysterious zero-width tokens came from `':'`-peek loops that advanced with `skip=true`. Lookahead past the token must advance with `skip=false`.
- **Symbols are `Symbol` (ns before chars), not `String`** — allocating the right struct but filling through `String::chars` puts the characters at the wrong offset, and every symbol comparison reads garbage (`case 'info':` matched nothing).
- **Alias references wrap in a plain `TypeType`** — `match_arm_is_error_handler` blind-casts an arm's type as `(TypeType*)`, so a raw `TypePattern` (whose second word is `pattern_index`) reads a small int as a pointer — SEGV. `build_identifier` always wrapped type/pattern definitions; the hand parser now does the same.
- **The Jube interface reader walks the raw CST**: `jube_node_is(attr_type, "fn_type")` classified fn-typed members. With one opaque token it classifies from the token TEXT (`jube_text_is_fn_type` + `jube_parse_fn_type_text` in `jube_interface.cpp`). This single dependency took down the whole radiant/graphviz/mermaid/structurizr cluster (43 tests) via "field binding lacks a getter".

## Third activation (2026-08-19) — contextual `start` retired

The final production external that was not an end-finder belonged to concurrency:
`_start` scanned a contextual word and fed the dedicated grammar node
`start_expr: 'start' call_expr`. That special surface could express only a target
call. Adding launch policy would have required a second bespoke option grammar,
despite maps and positional arguments already being ordinary Lambda syntax.

S13.1.1v2–S13.1.3v2 therefore replace the keyword with the concurrency system
procedure `start(target, args = [], options = {})`. The accepted task forms are
`start(child)`, `start(child, [value])`, and
`start(child, [value], {mode: 'task'})`. The options literal reserves
`'thread'` and `'process'`; those modes currently fail explicitly with E501
instead of silently running as a same-context task.

The grammar reduction is deliberately paired with a semantic intrinsic:

- `grammar-common.js` no longer declares `start_expr` or places it in
  `primary_expr`; `grammar.js` and `scanner.c` no longer declare or scan
  `_start`; generated `ts-enum.h` no longer exposes `SYM_START_EXPR`.
- `start` is registered as a variadic system `pn`, so its source is an ordinary
  `call_expr`. After normal name resolution and argument construction,
  `build_ast.cpp` rewrites only the unshadowed system procedure into
  `AstStartNode`. Structured ownership, scope-exit join/cancel, escape marking,
  and the K13 mutable-capture rejection therefore retain their existing node
  instead of being inferred later from a generic call.
- Literal argument arrays reuse the ordinary user-call parameter validator.
  MIR accepts a general array value and normalizes packed `ArrayNum` storage to
  the boxed `List::items` launch ABI; directly reinterpreting `[index]` as a
  `List*` was the root cause of the initial one-argument task crash.
- Removing `_start` exposed an accidental scanner dependency: its failed probe
  had been skipping whitespace before a following first-class `\(...)` pattern
  island. The scanner entry now owns that whitespace invariant explicitly while
  deliberately not invoking the slash-aware extras scanner, which would steal
  rooted paths. The full baseline caught both affected island fixtures.
- Existing concurrency fixtures were migrated mechanically from
  `start child(a, b)` to `start(child, [a, b])`. Focused proof covers omitted
  args, one packed-int argument, explicit `{mode: 'task'}`, scope exit, existing
  negative capture/context/target checks, and an unsupported-mode diagnostic.

Measured against the immediately prior production grammar, this activation
reduces the generated parser/archive by the amounts recorded above. More
importantly, adding launch policy no longer grows the grammar: new policy fields
belong to the options map and the `start` semantic validator, not Tree-sitter.
- **Scanner refinements**: `fn` joined `to` as a pattern-continuation word (`fn(a: int) int` is one token); the content/view name-decline generalized from bare words to any lone name-like atom, covering quoted field names (`'type': string`); view/edit patterns scan ONLY a bare word or one balanced `<...>` element, so a view body `{` can never be swallowed as a map pattern. The return scanner now rejects first-atom statement/control words such as `else`; it cannot consume `else {` while the GLR parser is recovering a declaration boundary. It also rejects a type connective with no RHS atom, which prevents the expression pipeline `parts |> join("")` from being tokenized as a bare alias return type; `grammar_reduce5_scanner` pins this boundary.
- **Diagnostic parity** (predicted as R4): conceptual base-type spellings (`int64`) fail with the canonical suggestion via the promoted `record_unknown_base_type`; genuinely unknown names stay LENIENT (ANY + warning) so `?unknown` queries degrade gracefully — both behaviors are load-bearing in the negative suites; island char classes reject shadowing bindings ("pattern class 'd' is reserved…"); and the `'<' ambiguous with element syntax` hint now recognizes the trimmed grammar's wider ERROR span (`< "b"`, not a lone `<`).

## Fourth activation (2026-08-19) — navigation merged into `member_expr`

`nav_expr` duplicated the same left-recursive postfix chain as `member_expr`:
both had an `object`, a dot, and a one-step suffix. The only distinction was that
the navigation node put `path_parent` (`~~`) and `path_root` (`/`) under an
`operation` field while ordinary member access put keys under `field`. The merge
makes `path_parent` and `path_root` two more `member_expr.field` alternatives.
It does not alter source syntax or semantics: S10.4.1–S10.4.3 still define
postfix parent navigation, S10.5.1–S10.5.3 still define postfix root navigation,
and S2.4.1v2–S2.4.5v2 still define their path forms.

- The grammar now has one `member_expr` CST node. Its ordinary-field and
  navigation-field alternatives deliberately retain their former precedence
  tiers: collapsing them to one tier made attribute binary expressions stop at
  `cfg` and reject the following `.field`. `primary_expr`, `ts-enum.h`,
  `grammar.json`, and `node-types.json` no longer carry `nav_expr` or
  `field_operation`.
- `build_member_expr` first preserves static provider/path recognition, then
  classifies a special field as `AstNavigationNode`; ordinary fields continue
  through `build_field_expr`. This ordering is required for `file./` to remain
  a static file-provider root instead of a dynamic root operation.
- The regular regeneration target now depends on `grammar-common.js`; before
  this correction, editing the imported shared grammar could leave generated
  parser artifacts stale unless callers forced the target.
- `make generate-grammar`, `make grammar-sync-check` (107 shared rules; full
  layer 41, production layer 9), full-reference generation, and direct
  `path_v2`, `parent_access`, and `path` fixtures pass. The retained
  navigation precedence restores the affected chart, math, Mermaid, and
  Graphviz parse trees; the clean close-out is Input 2,104/2,104 plus Lambda
  Runtime 1,709/1,709, total **3,813/3,813**.

## Fifth activation (2026-08-19) — greedy namespaced `dotted_name` landed

S2.4.3v2 resolves the apparent `dotted_name` / relative-`path_expr` ambiguity
as a language rule: a namespace-qualified name in an element tag or attribute
name position is maximal. Once the `ns.name` shape begins, the parser consumes
the complete dotted name before considering element content. Grammar extras do
not terminate it, so `<svg.rect>` and `<svg .rect>` both name the qualified
`svg.rect` tag.

A user who intends `.rect` as a relative-path child must write the element
content boundary explicitly:

```lambda
<svg; .rect>
```

Landed grammar and AST consequences:

- `dotted_name` has winning structural precedence in both the full and
  production grammars. Its namespace separator also has lexical precedence
  over the external path body at the shared dot. This is maximal name parsing,
  not a GLR ambiguity.
- `[$.dotted_name, $.path_expr]` is removed from the reference conflict list.
- The production-only `dotted_name_head_token`, its scanner branch, and its
  alias are removed; ordinary structural identifiers retain the existing
  `dotted_name(identifier, identifier, ...)` CST/AST contract.
- The AST builder canonicalizes named dotted-name segments instead of copying
  the whole source span, so extras accepted around `.` do not become part of
  the element or attribute identity.
- `path_body_token` remains: this ruling concerns the element name/content
  boundary, not the already-landed path-body reduction.
- `<h1 data.title>` is a separate content/member-expression problem.
  `dotted_name` is not a content production, so greedy namespace parsing does
  not solve or redefine that case.

## Sixth activation (2026-08-19) — query uses named postfix precedence

S7.6.3v2 places query access on the same left-associative postfix-primary tier
as member access. The former `[$._expr, $.query_expr]` declaration did not
encode that binding rule; it asked Tree-sitter's GLR engine to retain both
interpretations of an input such as:

```lambda
() => x?int
```

Those interpretations are `() => (x?int)` and `(() => x)?int`. The language
chooses the first because an arrow body extends through tighter postfix access;
querying the function value requires the explicit `(() => x)?int` spelling.

The shared precedence order now names `'query_expr'` immediately beside
`'member'`, `query_expr` is `prec.left('query_expr', ...)`, and the narrow
`['query_expr', $._expr]` ordering makes the arrow-body decision explicit.
Tree-sitter only compares named precedences that share a declared order, so
putting query in the main postfix list alone is not sufficient. The obsolete
GLR conflict is removed from both grammars. `query.ls` pins the association by
calling `() => [1, "a", 2]?int` as a function and observing its query result.

## Open issues to follow up

- **IS1 — a member expression cannot be element content.** `<h1 data.title>` does not parse: the element's `content` captures the bare identifier `data` and `.title` lands in an ERROR node (`(element (identifier) (content (primary_expr (identifier))) (ERROR (identifier)))`). PRE-EXISTING and unrelated to this campaign — verified by stashing every local change and rebuilding, where it fails identically. It is the direct cause of 18 `test_ui_automation_gtest` failures (all `todo*` plus two `editable_editors_prosemirror_*`), which reach it through `test/lambda/ui/todo.ls:259`. This is a content-level `member_expr` / relative-`path_expr` boundary; the fifth decision removes `dotted_name` from the diagnosis because dotted names occur only in element/attribute name positions. Consequence for planning: `make test-radiant-baseline` is NOT green on master (it also carries 12 pre-existing render pixel regressions in filters/iframes/form controls), so it cannot be used as a pass/fail gate for this work — compare failure SETS instead.

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
| **GR5 path body + qualified-name shield + split rooted tokens (2026-08-19)** | **6,998,990** | **968,768** | **4,881** | **818** | **221** | **1** |
| **GR5 contextual `start` retired (2026-08-19)** | **6,850,715** | **950,336** | **4,851** | **804** | **219** | **1** |
| **GR5 navigation as member fields (2026-08-19)** | **6,796,457** | **941,368** | **4,842** | **784** | **218** | **1** |
| **GR5 greedy structural namespace names (2026-08-19)** | **6,852,463** | **949,840** | **4,862** | **798** | **218** | **1** |
| **GR5 explicit query postfix precedence (2026-08-19)** | **5,715,314** | **800,432** | **4,124** | **699** | **218** | **0** |

GR5's current deltas versus the pre-campaign baseline are **parser.c −35.9%, parser.o −39.0%, states −35.6%**. Its archive is 818,048 bytes versus the same-build fresh SC4 archive of 1,044,432 bytes (−226,384 bytes, −21.7%); the archive includes the scanner growth for type/view/return end-finders and `path_body_token`, but no contextual-start or qualified-name collision scanner. Production AST construction no longer rebuilds return/occurrence/view interiors from a Tree-sitter CST and no longer walks rooted/relative path interiors to construct `AstPathNode`.

SC1 is the `that`-hoist plus the `value_error_type` deletion: **−8.3% source, −7.5% object, −9.1% states**, and four of the six declared GLR conflicts became unnecessary once the type layer stopped reaching into the expression grammar (removed; they measured identically either way). SC2 is a verified no-op — identical numbers across all five counters, which is the strongest available evidence that factoring the grammar into `grammar-common.js` + a type layer changed no language. SC3 measured on the parked grammar: **−17.4% source, −21.5% object, −21.4% states against baseline.**

Two findings worth keeping:

- **Seam rules must be inlined.** The three pure aliases the shared core still references (`_type_pattern`, `_primary_type`, `_char_pattern`) cost ~850 large states and 254 KB of `parser.o` when left as ordinary hidden rules. They are listed in `inline:` in `grammar-common.js`; without that the split is a regression, not a no-op. The former `_view_atom_type` seam moved entirely into the reference layer with `view_pattern`.
- **Dormant externals are not free.** Declaring scanner tokens without wiring them into rules widens every parse-table row: the historical three-token experiment moved large states 1,004 → 1,854 and added 254 KB to `parser.o`. This is why each GR5 seam must land atomically, rather than predeclaring future token kinds in the shipped grammar.

### Latest Tree-sitter and table-compression POC (2026-08-19)

**Question.** Before replacing the Lambda front end with a hybrid recursive-descent + Pratt parser, determine whether the latest Tree-sitter generator or a stock table-compression/build option can materially reduce the remaining ~800 KB parser. This is a size investigation only: it changes neither Lambda syntax nor the production parser. D8.1.1v2 still specifies the Tree-sitter grammar → typed AST pipeline; adopting another front end would require revising that formal ruling, while this POC does not.

**Reproducible setup.** The production grammar was generated by the pinned `tree-sitter 0.24.7` (`5e8760bf462ce7b19b3d2396d5b7860f3906a297`). The official upstream v0.26.11 tag (`64402de2857cc197ecc4ca3bc144ea91fda7e72e`) was checked out under `./temp/tree-sitter-v0.26.11`; a temp-only Rust driver called its actual `tree-sitter-generate` crate, avoiding any changes to the vendored runtime or production generator. v0.26.11 is the upstream latest release at the time of measurement. The upstream generate reference documents ABI 15 as the current default and ABI 14 as supported; its `--disable-optimization` option disables merging compatible parse states, rather than enabling an additional compression mode. Sources: [Tree-sitter releases](https://github.com/tree-sitter/tree-sitter/releases), [Tree-sitter generate reference](https://tree-sitter.github.io/tree-sitter/cli/generate.html).

All objects below were compiled on the same host with Apple Clang and the same `-Isrc -std=c11 -fPIC` base flags. `default` matches the grammar package's current compile command; `-Oz` adds size optimization. Generated `parser.c` and objects remain under `./temp/ts-size-poc/`.

| Generator / ABI / mode | `parser.c` | `parser.o` default | `parser.o -Oz` | states | large states | result versus pinned default |
|---|---:|---:|---:|---:|---:|---:|
| pinned v0.24.7 / ABI 14 / optimized | 5,715,314 | 800,432 | 776,872 | 4,124 | 699 | baseline |
| latest v0.26.11 / ABI 14 / optimized | 5,720,937 | 800,704 | 777,000 | 4,124 | 699 | **+272 B (+0.03%)** |
| latest v0.26.11 / ABI 15 / optimized | 5,721,141 | 808,984 | 785,288 | 4,124 | 699 | **+8,552 B (+1.07%)** |
| latest v0.26.11 / ABI 14 / optimization disabled | 25,381,679 | 3,321,000 | — | 17,810 | 3,032 | **+2,520,568 B (+314.9%)** |

The latest generator therefore provides **no parser-size reduction** for this grammar. ABI 14 preserves exactly the same 4,124-state/699-large-state inventory and is 272 object bytes larger. ABI 15 is also not a size feature; it adds 8,552 bytes here. Disabling Tree-sitter's optimizer demonstrates that the default compatible-state merging is already doing substantial work: it removes 13,686 states and about 2.52 MB from this object. There is no stronger stock generator compression switch left to turn on.

**CST compatibility check.** A paired-parser harness linked the pinned parser beside each latest parser and compared exact `ts_node_string` output for every `test/lambda/**/*.ls` file. Both v0.26.11 ABI 14 and ABI 15 matched the pinned parser on **921/921 files**. This confirms the version comparison is over equivalent observed CSTs for the repository corpus; it does not make ABI 15 smaller.

**Where the 800 KB lives.** The default object has 786,505 bytes of live Mach-O sections, including 742,880 bytes of read-only constants, so debug symbols and relocations are not the root cause. In the `-Oz` object these four generated arrays occupy approximately 704,426 bytes, **92.6%** of its 761,037 live bytes:

| Generated data | Approximate bytes |
|---|---:|
| dense `ts_parse_table` | 304,764 |
| sparse `ts_small_parse_table` | 335,802 |
| sparse-state map | 13,700 |
| parse actions | 50,160 |

Tree-sitter already selects between a dense table for the 699 large states and a compact sparse table for the remaining states. Compiler optimization can shrink the lookup code, but it cannot optimize away grammar-dependent constant rows.

**Available size levers.** Recompiling both parser and scanner with `-Oz` reduced the static archive from 818,048 to 788,248 bytes (**−29,800 B, −3.64%**); the stripped test dylib fell from 843,112 to 810,096 bytes (**−33,016 B, −3.92%**). `-O2`, `-Os`, function/data sections, and stripping did no better materially. This is a safe build-size polish candidate, but it does not change the architectural conclusion.

The `-Oz` parser object compresses to 79,209 bytes with gzip, showing high on-disk redundancy, but stock Tree-sitter and the native linker do not execute compressed parse-table sections. A custom scheme would have to decompress roughly 700 KB at startup into writable memory, add synchronization and allocator/error paths, give up file-backed read-only sharing, and maintain a nonstandard generator/runtime seam. Normal application/archive packaging already captures most of that disk-transfer benefit. This is not a good replacement for simplifying the parser itself.

**Decision from this POC.** Do not upgrade Tree-sitter for size and do not build a custom compressed-table runtime. Keep `-Oz` as an optional small build improvement. If an ~800 KB Lambda grammar object remains unacceptable, a bounded hybrid recursive-descent + Pratt POC is justified: its value proposition is removing generated LR tables, not recovering a missed Tree-sitter flag. The next POC should preserve the typed AST contract and compare acceptance, diagnostics, source spans, and AST/CST-derived output across the same corpus before D8.1.1v2 is changed. Error recovery and editor-grade incremental parsing are the main Tree-sitter capabilities that must be priced explicitly, rather than assumed free in the hand parser.

### Historical size-only POC: return, path, base type, and whole-view candidates

The following **historical size-only** POC was generated in `./temp/` with Tree-sitter 0.24.7, then built through the grammar package's `make libtree-sitter-lambda.a` using `cc -Isrc -std=c11 -fPIC`. It measures parser-table opportunity; its candidate scanner branches deliberately reused broad type scanning and were **not semantically correct implementation code**. It is retained to explain the rejected `base_type_token` opportunity, not as the production result above.

| Candidate production seam | `parser.c` | `libtree-sitter-lambda.a` | Delta vs fresh baseline |
|---|---:|---:|---:|
| Fresh current production baseline | 7,360,111 | 1,044,432 | — |
| `return_type_token` | 7,320,839 | 1,030,912 | −39,272 source; −13,520 archive |
| return + `path_expr_token` | 6,967,913 | 975,304 | −392,198 source; −69,128 archive |
| return + path + `base_type_token` | 6,919,082 | 967,216 | −441,029 source; −77,216 archive |
| return + path + base + whole `view_pattern_token`; move now-unreferenced occurrence rules to full grammar | **6,917,462** | **965,392** | **−442,649 source (−6.014%); −79,040 archive (−7.568%)** |

The final candidate's generated parser had 4,851 states, 814 large states, 221 symbols, 110 tokens, and 10 external symbols; its objects were `parser.o` 950,408 bytes and `scanner.o` 14,440 bytes. Production does **not** claim that candidate result: its whole `path_expr_token` accepted ambiguous bare prefixes and `.123`, while `base_type_token` caused another lexical collision. The landed `path_body_token` keeps the rooted `'/'`, `'.'` tokens or relative `'.'` introducer in grammar and therefore captures most of the path table reduction without those regressions. A separate external `view_pattern_union` token was intentionally not measured: whole `view_pattern_token` subsumes it, while a dormant extra external would add parse-table cost. `occurrence` and `occurrence_count` likewise do not need tokens once return types leave production grammar; they simply have no production direct consumer.

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
