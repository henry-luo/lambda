# Grammar Module — Implementation Plan

**Date:** 2026-10-11
**Status:** IN PROGRESS — **P0–P3 implemented 2026-10-11** (§7), **P4 and P5 the same day** (§8: composition, spans, quoted names, and `lambda.grammar.xpath`, scoped by USER to parsing only); imported tokens in local islands and cross-module composition 2026-10-12 (§9); **M4 jq and M5 XQuery and RELAX NG compact 2026-10-12** (§10, with GM13 `~x`). Q1–Q6 ruled by USER on 2026-10-11 (§6), which rewrote §2 from a VMap handle to the type path; GM4.2 ruled during P2 (design §0).
**Design:** [Lambda_Design_Grammar_Parser.md](../Lambda_Design_Grammar_Parser.md) — GM1–GM4, GM4.1, GM5v2, GM5.1, GM5.2 ruled (USER 2026-10-11); GM6–GM12 proposed; GM-O1–O3, O5–O7 open. The design doc holds the reasoning; this plan holds the seams, the phases and the tests.
**Rulings touched:** none yet. GM-O1 decides where `grammar` lands in the formal spec (proposed: an S16.9 declaration ruling for the surface, a new D8.3 for the engine, one line in S15 naming `parse(text, grammar)` as the second producer of trees that reach `compile`). S16.10.1v2's barred list grows by one word (`grammar`) — Appendix K of Lambda_Design_Syntax.md is regenerated, not hand-edited.
**Scope:** the `grammar` declaration, the loader + lint, the PEG interpreter with bounded left recursion, the `parse`/`input` surface, and the first guest (XPath 3.1) as the acceptance test. jq, XQuery and RELAX NG compact follow once the engine is proven; they are listed for sequencing only.

## 1. Where we start

Every fact below was checked in the code on 2026-10-11. The plan rests on them; if one moves, the phase that cites it is re-planned, not patched.

**The declaration pipeline for `type` is the template.** A `type` declaration is compile-time only:
- Lexer: `lexer_keywords[]` (`lambda/runtime/parser/lambda_lexer.c:196-226`) is the single owner of the vocabulary; `lambda_lexer_word_bars_binding()` (`:270-306`) bars every keyword by default, so a new `LAMBDA_TOK_GRAMMAR` is barred (S16.10.1v2) unless listed as allowed. Multi-char operators are hand-coded longest-match branches in `lambda_lexer_next()` (`:523`; the `=` branch at `:687-703` handles `==`/`===`/`=>`). The token enum is `LambdaTokenKind` (`lambda_rd_parser.h:18`).
- Parser: `parse_statement()` (`lambda_parser.c:2563`) dispatches `type` at `:2613` to `parse_type_declaration()` (`:2334`), which reduces through `parse_type_aliases()` (`:2240`) as `LAMBDA_REDUCE_DECLARATION` with form `LAMBDA_REDUCTION_FORM_TYPE_ALIAS` after a `_BEGIN` context event that pre-binds the name. The statement-boundary list after `}` (`:2781`) names the tokens that may start a statement.
- AST: `AST_NODE_TYPE_STAM = 515` (`ast-core.hpp:119`) wraps a declarator with `is_type_definition`; `syntax_type_alias_begin/stam/alias()` (`build_ast.cpp:14365-14383`), dispatched from `syntax_context()` (`:14534`) and `syntax_declaration()` (`:14753`); the resolve pass (`~16513-16537`) ends in `lambda_ast_register_name()` (`:2642`) → `push_name()` (`:2615`), where the keyword bar reports E201. `ast_definition_denotes_type()` (`ast-core.hpp:1386`), `is_declaration_node()` (`ast.hpp:990`) and the dump name table (`emit_ast_dump.cpp:205`) each enumerate the declaration kinds.
- T0: `eval_expr()` (`interp.cpp:5589`) routes `TYPE_STAM` to `exec_declaration()` (`:4492`), whose case does nothing at run time; an identifier bound to a type yields its `Type*` (`:5640-5651`). The planner lists supported kinds at `interp_plan.cpp:193`, `:271`, `:1546`.
- MIR: every arm reaches `transpile_let_stam()` (`transpile-mir.cpp:22017`); the `is_type_definition` branch (`:22060`) emits the `Type*` from the module `type_list` through `mir_emit_declared_type_value()` (`:10185`) → `transpile_const_type()` (`:35633`). `prepass_create_interp_module_vars()` (`:46215`) **skips** type definitions (`:46227`), so no module slot exists for them. Ordinary module bindings get a slot through `store_global_var()` (`:8718`) → `em_store_module_slot`.
- Reference grammar: `type_stam` (`tree-sitter-lambda/grammar.js:1465-1469`), listed in the statement choices at `:533` and `:563`; `KEYWORD_NAMES` (`:47-51`) feeds `reserved` (`:314-318`).

**Diagnostics.** Codes live in `lambda/core/lambda-error-data.h` (last E2xx is `ERR_FILTER_BODY_NO_CURRENT = 238`, `:99`). Span reporter: `record_semantic_error_span(Transpiler*, SourceSpan, LambdaErrorCode, fmt, …)` (`build_ast.cpp:1531`); `record_as_static_warning()` (`:1437`) downgrades E2xx to warnings under `--static-warning`. The only existing compile-time *warning* is the sys-func shadow lint, a plain `log_warn` inside `push_name_with_spelling` (`:2576`). There is no warning catalog with codes.

**Sys funcs.** `parse` is `fn_parse2(Item str, Item type)` (`lambda-eval.cpp:6168`; `fn_parse1` at `:6304` forwards `ItemNull`), which accepts a format symbol **or an options map** — `type`, `flavor`, `sourcepos: 'spans'`, `window` (`:5910`, `:6179`) — then calls `input_from_source[_with_options]` and returns `input->root`. `input` is `fn_input2` (`:5450`). Names resolve through the single-source registry `lambda/runtime/sys_func_registry.c` (`parse` rows `:1076/:1079`, `input` rows `:668/:671`; row struct `SysFuncInfo` at `sys_func_registry.h:88`, `can_raise` at `:97` marks a `T^` return) via `get_sys_func_info()` (`build_ast.cpp:470`). Error return: `set_runtime_error(code, fmt, …)` (`lambda-eval.cpp:164`) then `return ItemError`; parsers use `set_input_parse_error()` (`:182`) over `input->parse_error_message`.

**Input parsers.** Signature `void parse_xxx(Input*, const char* src)` (`lambda/input/input-parsers.h`), dispatched by `INPUT_PARSER_MAPPINGS[]` (`input.cpp:1938`) from `input_from_source_n_with_name_parent` (`:2101`). A parser wraps its `Input` in `InputContext ctx(input, src, len)` (`input-context.hpp:19`, `builder` at `:28`) and builds with `MarkBuilder`: `element(tag)` (`mark_builder.hpp:170`), `ElementBuilder::attr` (`:368-430`), `child` (`:439`), `text` (`:444`), `final()` (`:460`); `input-rtf.cpp:451` is the smallest element-building example. `Input::create(Pool*, Url*, Input* parent)` (`input.cpp:2556` → `:2560`) registers `pool_add_cleanup` (`:2621`), which is D4.2.6's pool ownership.

**String-pattern islands.** `parse_island_body()` (`parse_type_pattern.cpp:512`, under `parse_island()` `:538`) builds `AST_NODE_PATTERN_ISLAND`; `resolve_type_pattern()` (`:1431`, `LSF_TP_ISLAND` at `~1530-1552`) compiles it with `compile_runtime_pattern()` (`re2_wrapper.cpp:809`) into a `TypePattern` (`lambda-data.hpp:1292`: anchored `re2`, lazy `re2_unanchored`, `source`). Matching: `fn_is` (`lambda-eval.cpp:2832`) → `pattern_full_match_chars()` (`re2_wrapper.cpp:1020`); `pattern_get_unanchored()` (`:1125`). **There is no "match anchored at offset, return the end" API** — nothing in `lambda/` uses `RE2::ANCHOR_START`. `lib/re2/` is vendored (AGENTS.md rule 16); `re2_wrapper.cpp` is Lambda's and is where the new call goes.

**Opaque runtime values get no new TypeId.** `EnumTypeId` (`lambda.h:87`) is closed; tasks are a `VMap` (`LMD_TYPE_VMAP`) with a custom `VMapVtable` (`lambda.hpp:951`: get/set/count/keys/key_at/value_at/destroy/trace) and a brand pointer in `host_type` (`concurrency.cpp:235`, `:689-697`). The GC traces a VMap through `vtable->trace` (`gc_heap.c:2032`) and compacts it (`:2250`); DB connections are `RuntimeResourceTable` resources, not values.

**Build and tests.** `build_lambda_config.json`: top-level `source_dirs` is non-recursive (`:17`; `lambda/runtime` `:26`, `lambda/runtime/gc` `:27`); target `lambda-rt` lists `source_patterns` (`:686-694`); the `cli` platform (`:1254`) has its own `source_dirs` (`:1257-1275`) and defines `LAMBDA_HEADLESS/NO_JS/NO_JUBE/NO_SERVE` (`:1312-1317`); test executables are declared under `test.test_suites` (`:2444`, lambda suite `:5173`, example `:5207-5216`). Fixtures: `test_lambda_gtest` runs `test/lambda/**/foo.ls` against sibling `foo.txt` (non-recursive discovery; a new subdirectory is added to `FUNCTIONAL_TEST_DIRECTORIES`, `test/test_lambda_gtest.cpp:20`); helpers are `_*`/`mod_*`; `make test-lambda-baseline` (`Makefile:1678`) is the gate. One corpus script binds the word today: `test/lambda/graph/graphviz/formatter.ls:42` (`let grammar = …`).

## 2. Design at the code level

### 2.1 A grammar is a type; so is each rule (GM5v2)

| Object | Representation | Lifetime / owner | Built by |
|---|---|---|---|
| the grammar `calc` | `TypeGrammar : Type`, kind `TYPE_KIND_GRAMMAR` — rule table, compiled token matchers, analysis results | the module: `Script`'s pool, entered in `type_list` like every declared type (D4.1.1v2) | the resolve pass of `build_ast` (P1), once per module |
| a token rule `calc.number` | the `TypePattern` (kind `TYPE_KIND_PATTERN`) its `:=` body compiles to — nothing new | same | the island compiler, unchanged |
| a syntax rule `calc.expr` | `TypeGrammarRule : Type`, kind `TYPE_KIND_GRAMMAR_RULE` — `{ TypeGrammar* grammar; uint32_t index; }` | same | the loader (P1) |

There is no run-time object and no handle. A script holds a `Type*` item (`LMD_TYPE_TYPE`) for the grammar or for a rule, exactly as it holds a `Type*` for `type T = …`: the T0 identifier read yields `decl->type` (`interp.cpp:5640`), MIR emits it as an immediate through `mir_emit_declared_type_value` → `transpile_const_type`, and `prepass_create_interp_module_vars` keeps skipping it (`:46227`) because it owns no module slot. Nothing is GC-managed, so no trace case, no compaction case, no brand. `fn_parse2` takes the grammar path when its second argument is a `Type*` whose kind is `TYPE_KIND_GRAMMAR` (entry = first rule, or the symbol named by the third argument) or `TYPE_KIND_GRAMMAR_RULE` (entry = that rule; GM5.3, after V1). `type(calc)` is `type` (S2.1.2).

### 2.2 `Grammar` (`lambda/runtime/grammar/grammar.hpp`)

```c
typedef enum { GX_SEQ, GX_CHOICE, GX_REPEAT, GX_LITERAL, GX_TOKEN, GX_RULE,
               GX_AND, GX_NOT, GX_PREC, GX_LABEL, GX_HIDDEN } GrammarExprKind;
typedef struct GrammarExpr {
    GrammarExprKind kind;
    uint32_t min, max;              // GX_REPEAT (max 0 = unbounded)
    uint32_t prec; uint8_t assoc;   // GX_PREC: level, GM_ASSOC_NONE/LEFT/RIGHT
    String* literal;                // GX_LITERAL (string domain, GM4.1)
    bool word_boundary;             // literal made of word chars (§4.2 of the design)
    uint32_t rule_index;            // GX_TOKEN / GX_RULE
    String* label;                  // GX_LABEL → attribute name
    struct GrammarExpr** items; uint32_t item_count;   // SEQ / CHOICE children; one child otherwise
    SourceSpan span;
} GrammarExpr;
typedef struct GrammarRule {
    String* name; bool is_token; bool is_space; bool nospace; bool memo; bool hidden;
    GrammarExpr* body;              // syntax rules
    Type* type;                     // the rule as a type: TypePattern (token) or TypeGrammarRule (syntax)
    bool nullable; bool left_recursive;   // analysis (P1)
    uint64_t* first;                // FIRST set as a bitset over literal/token ids (P1 lint)
} GrammarRule;
typedef struct TypeGrammar {        // Type with kind TYPE_KIND_GRAMMAR (GM5v2)
    Type base;
    String* name; ArrayList* rules;        // GrammarRule*
    HashMap* rule_by_name;                 // StrView → index
    uint32_t space_rule;                   // or GRAMMAR_NO_RULE
    uint32_t entry_rule;                   // the first rule in the block (GM5.1)
    ArrayList* imports;                    // TypeGrammar* (P4)
} TypeGrammar;
typedef struct TypeGrammarRule {    // Type with kind TYPE_KIND_GRAMMAR_RULE
    Type base;
    TypeGrammar* grammar; uint32_t index;
} TypeGrammarRule;
```

`ArrayList`/`HashMap`/`String` from `lib/` (AGENTS.md rule 3). Both type structs sit beside `TypePattern` (`lambda-data.hpp:1292`) and take the next two `TypeKind` values; everything is allocated from the Script's pool with `pool_calloc` and entered in `type_list`, so the existing type ownership rules apply with no new case anywhere in the GC.

### 2.3 The engine (`grammar_parse.cpp`)

```c
typedef struct ParseState {
    Grammar* g; const char* src; size_t len; size_t pos;
    uint32_t depth;                 // guard at GRAMMAR_MAX_DEPTH (same number as LAMBDA_RD_MAX_DEPTH)
    size_t furthest; ArrayList* expected_at_furthest;   // §8 of the design
    Arena* scratch;                 // memo entries, child lists, expected set — destroyed with the parse
    HashMap* memo;                  // (rule, pos, level) → MemoEntry {Item result; size_t end; bool growing}
    MarkBuilder* out;               // builds the tree into the Input
    bool spans;                     // sourcepos: 'spans'
} ParseState;
```

- `parse_expr(ParseState*, GrammarExpr*, ChildList*) → bool` is the interpreter proper: a switch over `GrammarExprKind`, position restored on failure, children appended on success. Repetition stops when an iteration does not advance (the nullable-repetition guard).
- `parse_rule(ParseState*, uint32_t rule, uint32_t level) → Item` handles `space` before and after the body (unless `nospace`), memoization, the seed-growing loop of design §5.2 for left-recursive rules, and element construction: one `ElementBuilder` per rule, tag = rule name, children in order, labels as attributes, `hidden` splicing into the caller's `ChildList`.
- Tokens: `GX_LITERAL` is `memcmp` at `pos` plus the word-boundary test — no regex. `GX_TOKEN` calls the new `pattern_match_prefix_at(TypePattern*, const char*, size_t len, size_t pos, size_t* end)` (P2) which is `re2_unanchored->Match(text, pos, len, RE2::ANCHOR_START, &m, 1)`.
- The scratch `Arena` is created per `parse` call and destroyed at its end (D4.1.4v5: whole-arena release, one owner). The output tree lives in the `Input`, which the caller's pool owns (D4.2.6) — exactly as every other `input_from_source` result.
- Failure is a `bool` plus `pos` reset; only the furthest position keeps its expected set, so the hot path allocates nothing.

### 2.4 File layout

```
lambda/runtime/grammar/
  grammar.hpp          TypeGrammar / TypeGrammarRule / GrammarRule / GrammarExpr, loader + engine API
  grammar_load.cpp     AST → TypeGrammar; nullable/FIRST analysis; left-recursion classification; literal reordering; lint
  grammar_parse.cpp    ParseState, parse_expr, parse_rule, tree building, error report; grammar_parse() entry for fn_parse2 / fn_input2
```

Added to `source_dirs` and to `lambda-rt`'s `source_patterns` **and** to the `cli` platform's `source_dirs` (GM-O7: the module is part of `lambda-cli`; it depends only on core, io and re2). No `LAMBDA_NO_*` guard.

## 3. Phases

Each phase ends green on `make build`, `make test-lambda-baseline`, and the JIT golden sweep (`LAMBDA_EXEC_BACKEND=jit` on the gtest harness) once P2 exists. Fixtures go under `test/lambda/grammar/` (new entry in `FUNCTIONAL_TEST_DIRECTORIES`), one `.ls` + `.txt` per behaviour (AGENTS.md rule 8); negative cases under `test/lambda/negative/grammar_*.ls` with the error code in the golden. The lint gets its own gtest, `test/test_grammar_lint_gtest.cpp`, declared in the lambda suite of `build_lambda_config.json`.

### P0 — Surface prerequisites (no engine)

1. `lambda_lexer.c`: add `{"grammar", LAMBDA_TOK_GRAMMAR}` to `lexer_keywords[]`; it is barred by `default` in `lambda_lexer_word_bars_binding()`, which is the intent (GM5v2). Add `LAMBDA_TOK_DEFINE` for `:=` in the `:` branch of `lambda_lexer_next()`, longest-match like `=>`. `:=` has no prior meaning anywhere in the language, so lexing it unconditionally is safe; the parser rejects it outside a grammar block with "`:=` is a grammar token rule; outside `grammar { }` write `:` or `=`".
2. Rename the binding in `test/lambda/graph/graphviz/formatter.ls:42` (`let grammar` → `let dot_grammar`); its golden is unaffected unless it prints the name.
3. `grammar.js`: add `'grammar'` to `KEYWORD_NAMES`; add a `grammar_stam` rule to the statement choices at `:533`/`:563` (body parsed loosely — the reference grammar is the first-cut verifier, D8.1.2v3, not the production parser). `make generate-grammar`.
4. Regenerate Appendix K of `Lambda_Design_Syntax.md` (65 → 66 barred words).
5. Ledger: open a `GM` area in `vibe/Lambda_Issue_Ledger.md` only when the first defect lands (there is no `LR_14` yet; §6 Q6).

**Exit:** `let grammar = 1` is E201; `grammar` as an element tag/map key/member still works (S16.10.2); baseline green.

### P1 — Declaration → AST → `Grammar` (loader + lint, compile-time only)

Parser (`lambda_parser.c`):
1. `parse_statement()` dispatches `LAMBDA_TOK_GRAMMAR` (next to `type` at `:2613`) to a new `parse_grammar_declaration(parser, is_public)`: name, `{`, then rules until `}`. A rule is `name :=` (token rule: hand the lexer to `parse_island_body()` with the string domain forced; the body ends at the next rule head or `}`) or `[memo] [nospace] [hidden] name =` (syntax rule: a parsing-expression parser with the precedence `|` < `,` < postfix `? * + {n} {n+} {n,m}` < prefix `& !` < atom). **Rule delimiting:** a rule ends where the next rule head begins — the two-token lookahead `IDENT (= | :=)` — or at `}`; `;` between rules is accepted and ignored (S16.9.3: `;` is statement separation, and a rule is a statement of the block). Line breaks carry no meaning (S16.1.1), which this satisfies because the delimiter is the rule head, not the newline. `prec`, `prec.left`, `prec.right`, `hidden`, `memo`, `nospace`, `space` are recognized by spelling inside the block only; they are not lexer keywords.
2. Reductions: context events `LAMBDA_REDUCTION_FORM_GRAMMAR_BEGIN/_END` (so the name is pre-bound like a type alias and the block has a scope), `LAMBDA_REDUCE_DECLARATION` with form `LAMBDA_REDUCTION_FORM_GRAMMAR`; each rule reduces as `LAMBDA_REDUCE_GRAMMAR_RULE` with forms `_TOKEN` / `_SYNTAX`, and parsing-expression nodes as `LAMBDA_REDUCE_GRAMMAR_EXPR` with one form per `GrammarExprKind`. Add `LAMBDA_TOK_GRAMMAR` to the `}` statement-boundary list (`:2781`).

AST (`build_ast.cpp`, `ast-core.hpp`):
3. `AST_NODE_GRAMMAR_STAM` wrapping a declarator with a new `is_grammar_definition` flag; child nodes `AST_NODE_GRAMMAR_RULE` and `AST_NODE_GRAMMAR_EXPR` carry the block as data (D8.2.2: syntax variance as fields, no rewriting). The declarator sets `is_type_definition` (GM5v2: a grammar *is* a type), so `ast_definition_denotes_type()`, the T0 no-op in `exec_declaration`, the identifier read, MIR's `is_type_definition` branch and the prepass skip all apply unchanged; register the kind in `is_declaration_node()` and the dump table, add `LSF_GRAMMAR_STAM` in `ast_build.hpp` and `syntax_grammar_begin/stam/rule/expr()` dispatched from `syntax_context()` / `syntax_declaration()` / `syntax_reduce()`.
4. **Token-rule references.** The block's `_BEGIN` pushes a scope in which every `:=` rule is bound as a type-pattern name, so `resolve_type_pattern()` resolves `ident := "\a" word*` references to sibling token rules with no change to the island resolver, and the S11.1.2v4 domain checks apply unchanged (a `'x'` fragment in a `:=` body is the ordinary mixed-domain error). The scope is popped at `_END`; token rules are not visible outside the block in V1.
5. Resolve pass: `resolve_grammar_stam()` calls `grammar_load(Script*, AstNode*) → Grammar*` (`grammar_load.cpp`), stores it on the declarator, and registers the name through `lambda_ast_register_name()` (E201 for barred names, E209 for duplicates, both for free).

Loader + lint (`grammar_load.cpp`):
6. Build the `TypeGrammar` from the AST and enter it, and one `TypeGrammarRule` per syntax rule, in `type_list`; resolve rule references by name (`HashMap`). Errors follow the script parser's scheme (GM5.2): codes allocated in `lambda-error-data.h` after `ERR_FILTER_BODY_NO_CURRENT = 238` — `ERR_GRAMMAR_UNDEFINED_RULE`, `ERR_GRAMMAR_INDIRECT_LEFT_RECURSION`, `ERR_GRAMMAR_SYMBOL_TOKEN` (hint `write "x"`, GM4.1), `ERR_GRAMMAR_DEFINE_OUTSIDE_BLOCK` — reported through `record_semantic_error_span` with the rule's span, messages kept in a static catalog like `lambda_parser.c`'s `error_*` strings.
7. Analysis: nullable (fixpoint), FIRST sets as bitsets over the literal/token universe, the **nullable-prefix graph** and its cycles (DFS): a self-edge marks `left_recursive`; a longer cycle is `ERR_GRAMMAR_INDIRECT_LEFT_RECURSION` naming the cycle (GM8).
8. Literal reordering inside a `GX_CHOICE` whose alternatives are pure literals: stable sort longest-first, report each move.
9. Lint (design §6): prefix capture (literal, and FIRST-containment for non-literals), unreachable alternative, nullable repetition, left recursion without `prec`, non-associative chain, missing `space`, entry is `space`, `memo` suggestion, unused rule. Warnings go through the channel the script compiler already uses — a `log_warn` line with the rule's span, the way `lambda_shadow_lint` warns (`build_ast.cpp:2576`) — with the fixed prefix `grammar lint:` and a fixed message per kind (GM5.2: no new catalog, no tags; the gtest asserts on the message text as the existing diagnostics tests do). Errors go through `record_semantic_error_span`; warnings never use `record_as_static_warning` because they are never errors.

Tests:
- `test/test_grammar_lint_gtest.cpp`: one grammar string per lint kind and per error code; asserts the error code or the warning message and the rule name. It links `lambda-lib` like `test_lambda_gtest`.
- `--emit-ast-dump` shows `GRAMMAR_STAM` with its rules (the corpus compile scan in memory `corpus-compile-scan` covers the dump).
- Negative fixtures: `negative/grammar_undefined_rule.ls`, `grammar_indirect_left_recursion.ls`, `grammar_symbol_token.ls`, `grammar_define_outside_block.ls`.

**Exit:** a script holding the design's §1 `calc` grammar compiles with no warnings; `calc` and `calc.expr` evaluate to type values in both tiers (`type(calc)` is `type`), with `parse` not yet accepting them; lint gtest green.

### P2 — Both tiers, `parse(text, g)` and `parse(text, g, 'rule')`

1. Tiers: nothing new to execute. The declarator's `is_type_definition` makes `exec_declaration()` a no-op and the identifier read yield the `TypeGrammar*` (`interp.cpp:5640-5651`); MIR's branch at `transpile-mir.cpp:22060` finds it in `type_list` through `mir_emit_declared_type_value()`. `interp_plan.cpp` lists the new node kind at `:193`, `:271`, `:1546`. Verify `.member` on a type value: `calc.expr` must yield the `TypeGrammarRule*` — if member access on `LMD_TYPE_TYPE` has no arm today, add one for `TYPE_KIND_GRAMMAR` only (§4).
2. `pub grammar`: falls out of `AST_NODE_PUB_STAM` wrapping, as for `pub type`; a fixture imports a grammar from a `mod_*` module.
3. `re2_wrapper.cpp`: `pattern_match_prefix_at()` as in §2.3; a unit test in the existing re2 gtest covers offset, empty match, and a pattern that cannot match at `pos` but can later (must return false).
4. Sys funcs (GM5.1): `fn_parse2` takes the grammar path when `type` is a `Type*` of kind `TYPE_KIND_GRAMMAR`, entry = first rule; a new `fn_parse3(str, g, rule)` with a `SYSFUNC_PARSE3` row in `sys_func_registry.c` and `lambda.h` names the entry by symbol (undefined rule → the same `ERR_PARSE_ERROR` path with "no rule 'x' in grammar calc"). `fn_input2`/`fn_input3` the same for the file form. All keep `can_raise`. The `sourcepos: 'spans'` option (GM11) is not in this phase: it needs the options-map form, which GM5.1 defers.
5. `grammar_parse(TypeGrammar*, uint32_t entry, Input*, src, len)` in `grammar_parse.cpp` per §2.3, without left recursion yet (a left-recursive rule is rejected at this phase with an explicit "P3" diagnostic rather than mis-parsed). Whole-input requirement. A failure is reported exactly as every input parser reports one (GM5.2): the furthest-failure message (`expected ")" or "+" at 1:7`) goes into `input->parse_error_message`, `parse_failed` is set, and `fn_parse2` raises through `set_input_parse_error` (`lambda-eval.cpp:182`) with `ERR_PARSE_ERROR`.

Tests (`test/lambda/grammar/`):
- `calc_layered.ls` (the §1 language written with repetition instead of left recursion), `json.ls` (a JSON grammar, parsed tree formatted and compared), `entry_rule.ls` (`parse(text, g)` vs `parse(text, g, 'rule')`, unknown rule error), `space_nospace.ls`, `lookahead.ls`, `labels_hidden.ls` (attributes and splicing), `pub_import.ls` + `mod_calc.ls`, `type_value.ls` (`type(calc)`, `calc.expr` is a type, `calc.number` is the pattern), `errors.ls` (furthest-failure message, whole-input failure, `^` propagation per S7.10).
- Tier parity: the same fixtures under `LAMBDA_EXEC_BACKEND=jit`; `LAMBDA_GC_FORCE_EVERY` sweep over `json.ls` (the tree is `Input`-owned; the types are pool-owned — the sweep proves neither is touched by a collection).

**Exit:** all P2 fixtures green in both tiers; baseline green.

### P3 — Left recursion and precedence (GM6–GM8)

1. `parse_rule()` grows seeds for `left_recursive` rules: memo entry `(rule, pos, level)` with `growing = true` and `result = fail`; non-left-recursive alternatives first; then the growth loop over `GX_PREC` alternatives admissible at `level` (`prec ≥ level`), restarting from the first alternative after each growth, stopping when a pass does not advance. Trailing self-references are called at `prec + 1` (left / bare `prec`) or `prec` (right); middle self-references at 0.
2. Memo lifetime: entries live in the scratch arena; the table is cleared between top-level `parse` calls, never shared.
3. Lint additions from P1 become live: left recursion without `prec` (treated as `prec(0)`), non-associative chain.

Tests: `prec_traces.ls` pins the two §5.2 traces (`a + b * c`, `a * b + c`), unary minus (`-a * b`, `-a ^ b`), right associativity (`a ^ b ^ c`), postfix/call/index as left-recursive alternatives, the ternary with a middle operand, a non-associative chain failing to parse, and a mixed grammar where a `prec` number on one rule does not leak into another (GM7). `prec_vs_layered.ls` parses the same inputs with the P2 layered grammar and the P3 `prec` grammar and asserts identical trees modulo rule names — the check that the growth loop is precedence climbing and not something else.

**Exit:** every row of the design's §5.3 table that says "supported" has a fixture line; `test_grammar_lint_gtest` covers the two lint rows.

### P4 — Composition and the remaining surface

1. `import other;` inside a block (P1's parser accepts it; the loader resolves it in P4): the imported `Grammar*` is a module-scope binding resolved by name at load time (the importer must be in scope — same rule as any identifier). Rules are merged by name, local wins, `space` included; the lint runs on the composed table.
2. `memo` on arbitrary rules (the mechanism exists since P3; this phase exposes the word and the lint suggestion).
3. REPL: a `grammar` declaration in a session goes through the same T0 path as `type` (D8.1.1: T0 session); verify `.type` lists it with the other declared types and `.load` of a module that redeclares it replaces the type cleanly.
4. `sourcepos: 'spans'` (GM11) once an options-map form of `parse` exists; the attribute payload (`start`/`end` offsets) differs from the markdown/html highlight-span payload behind the same option name, which the doc for `parse` must say.

Tests: `compose_override.ls` (XPath-in-XQuery shape with a tiny pair of grammars), `memo_quadratic.ls` (a grammar that backtracks quadratically without `memo`, with a size that finishes either way but a golden that only checks the tree).

### P5 — First guest: XPath 3.1

The acceptance test for "the spec transcribes nearly line by line" (design §3 reason 1) and for GM4's "the transpiler is an ordinary Lambda function".

1. `xpath.ls` grammar transcribed from the W3C EBNF (≈80 productions), with the published lookahead notes expressed as `&`/`!` and the operator ladder as `prec` alternatives.
2. `xpath_to_lm.ls`: a Lambda function from the parse tree to `lm.` elements (S15.1) for the subset that maps onto Lambda paths (S2.4), predicates (`|:`), arithmetic and comparison; unsupported constructs raise a typed error naming the production. `compile(ast)` (S15.3) closes the loop; a fixture evaluates a dozen XPath expressions over a parsed XML document and compares with hand-computed results.
3. Placement: a bundled package in the `lambda.grammar.xpath` namespace, located where `doc/Lambda_Packages.md` puts bundled packages (Q4 confirms the namespace).

jq (bison grammar with `%left/%right`, the acceptance test for P3), XQuery (parser-driven lexing stress), and RELAX NG compact (`nospace`) are sequenced after XPath and each gets its own impl section when started; they are not planned in detail here.

## 4. Issues with the design, found while planning

- **Token references inside islands need a scope.** The island resolver resolves names as types in scope (`resolve_type_pattern`); P1 step 4 binds `:=` rules into a block scope to reuse it unchanged. With GM5v2 this is no longer a trick: the rules *are* types, so binding them in scope is the ordinary thing. If the scope collides with the pre-bound name of the grammar itself, the block scope is pushed *after* the grammar name is registered.
- **Member access on a type value.** `calc.expr` reads a member of an `LMD_TYPE_TYPE` item. Whether `.member` on a type has an arm today (object types expose fields/methods, S16.10.2) is checked at the start of P2; if not, one arm for `TYPE_KIND_GRAMMAR` resolving `rule_by_name` is added — a read of an existing immutable table, not a new mechanism.
- **Printing a grammar.** `format(calc)` goes through the type printer; a `TypeGrammar` prints its name like a nominal type does, and a `TypeGrammarRule` prints `calc.expr`. Checked in `type_value.ls`.
- **`sourcepos: 'spans'` already exists for markdown/html** (`lambda-eval.cpp:6179`, highlight spans). Deferred with the options form (P4 step 4); the payload difference is documented there.
- **The `}` statement-boundary list and `grammar` inside element content.** `<grammar>` is a legal tag (S16.10.2); the parser must only take the declaration reading at statement start (S16.2 line-start classification), as it does for `type`.
- **`type` keyword inside the block.** A rule named `type` (XQuery) is a data name in the block, but the lexer still emits `LAMBDA_TOK_TYPE`; the rule-head lookahead `IDENT (= | :=)` must accept keyword tokens as rule names (`token_is_name_word`, `lambda_parser.c` ~396 — the same two-site trap the keyword ruling recorded: accept at the head *and* at every reference).

## 5. Out of scope, recorded for later

- Error recovery / partial trees (design §8: no consumer).
- Explicit level references `expr@3` (GM-O2); semantic predicates (GM-O6); symbol-domain tokens (GM-O3, narrowed); a symbol as rule-name escape (GM-O3 remainder).
- Packrat-by-default (GM-O5): measure on the XQuery grammar first.
- Token rules visible outside the block as types (`"x" is calc.number`): attractive, cheap once P1 step 4 exists, not needed by a guest.
- A formatter from a grammar tree back to text (the reason `space` is a rule, not a skip): later.

## 6. Rulings (USER, 2026-10-11)

| ID | Question | Ruling |
|---|---|---|
| **Q1** | The runtime value of a grammar | **A grammar is a special `type` under Lambda, and each of its rules is a special type too** → GM5v2. The plan's VMap-handle design was dropped the same day; §2 is the type path. |
| **Q2** | Call surface | **`parse(text, g)` and `parse(text, g, 'rule')` first; with no rule named, the first rule in the grammar is the entry** → GM5.1. The options-map form and `sourcepos: 'spans'` wait (P4 step 4). |
| **Q3** | Diagnostics | **Align with the existing Lambda input and script parsers** → GM5.2: script-parser E-code scheme and reporters at compile time, `ERR_PARSE_ERROR` with `parse_error_message` at run time, `log_warn` lint lines; no new catalog or tags. |
| **Q4** | Guest package namespace | **`lambda.grammar`** (`lambda.grammar.xpath`, `lambda.grammar.jq`, …). |
| **Q5** | XPath (P5) before composition (P4) | **Yes.** P5 follows P3; P4 when XQuery starts. |
| **Q6** | Doc/dev and ledger home | **Yes** to a new `LR_14_Grammar_Module.md` with its own ledger section, written at the end of P2. |

Still open, in the design doc: GM-O1 (where `grammar` is ratified in the formal spec), GM-O2 (explicit level references), GM-O3 (symbol as rule-name escape), GM-O5 (packrat by default), GM-O6 (semantic predicates), GM-O7 (`lambda-cli` membership — assumed yes by §2.4).

## 7. Implementation record (2026-10-11)

### 7.1 What landed

| Phase | Result |
|---|---|
| P0 | `grammar` keyword (`lexer_keywords[]`, barred by default, admitted as a data name in `token_is_name_word`); `formatter.ls` binding renamed `dot_grammar`; `grammar.js` reserves `grammar` and reads `grammar_stam` with an opaque `grammar_body` token (`make generate-grammar` regenerated); Appendix K of Lambda_Design_Syntax.md is 66 barred words. |
| P1 | `parse_grammar_declaration` + `lambda_lexer_skip_block_body` hand the body over as one span; `syntax_grammar` / `resolve_grammar` in `build_ast.cpp`; `grammar_compile` in `grammar_load.cpp` (body parser, token rules through `parse_pattern_island_syntax` + `resolve_type_pattern`, rule references, nullable fixpoint, nullable-prefix graph, left-recursion classification, literal reordering, lint). |
| P2 | `TypeGrammar` / `TypeGrammarRule` in `lambda-data.hpp`; `fn_parse2` grammar arm and `fn_parse3` (`SYSFUNC_PARSE3`, appended to the enum); `fn_member` arm for `g.rule`; `fn_is` arm for `text is g` (GM5.3); print arm; `pattern_match_prefix_at`; the engine in `grammar_parse.cpp`. |
| P3 | Seed growing with per-rule levels, plus the growth cap of design §5.2 step 5. |

Build: `lambda/runtime/grammar` added to the top-level `source_dirs`, `lambda-rt`'s patterns, the `cli` platform and the wasm list; `grammar_load.cpp` to `lambda-runtime-full` (its `build_ast.cpp` calls it).

### 7.2 Departures from the phase text

- **No `:=` token in the Lambda lexer** (P0 step 1). The body never reaches it: it is skipped to the matching brace and parsed by the loader, as type slots are (`parse_type_pattern.hpp`'s SC4 note). A main-lexer `:=` would also have met island escapes (`"\d"`) the main lexer rejects outside islands.
- **No new AST node kind** (P1 step 3). The declaration is the same `TYPE_STAM` + declarator as `type Name = T`, with form `LSF_GRAMMAR` and `is_type_definition` set: D8.2.2 prefers a field to a node kind, and GM5v2 makes it literally a type declaration. T0 and MIR needed no change.
- **Lint tests live in `test_lambda_errors_gtest.cpp`** (`NegativeScriptTest.Grammar*`, 10 cases over `test/lambda/negative/{semantic,syntax}/grammar_*.ls`), not a new `test_grammar_lint_gtest`: that harness already runs scripts with stderr merged, and warnings reach stderr.
- **Warnings channel.** GM5.2 named "the warning channel the script compiler already uses". The shadow lint's `log_warn` only reaches `log.txt`, so the lint uses the runner's printed warning list (`tp->warnings`, `err_print_warning`) through a new `record_semantic_warning_span`; the runner's summary line now reads `N warning(s).`, with the relaxed-mode suffix only under `--static-warning`.
- **Error codes** (GM5.2: existing scheme): body syntax E100, symbol literal E103, unknown rule E204 (rules are types), duplicate rule E209, left recursion through another rule or a nullable prefix E215; lint warnings E200, unreachable alternatives E223. A failed parse is E407 via `set_input_parse_error`.
- **No hash table for rules.** Rule lookup is linear over interned names: grammars are small, and a table would need a pool cleanup to free.
- **`input(target, g)` not done** (P2 step 4 listed it); design GM-O9.
- **RE2 unit test not added** (P2 step 3): no wrapper gtest exists; offset, empty and later-only matches are exercised by the fixtures.

### 7.3 Corrections found by implementing

- **Keyword tokens need the literal word boundary.** `for := 'for'` matched the prefix of `format`; an island has no lookahead, so the design's `!"\w"` advice could not work. A `:=` rule whose pattern is only word-character literals now gets the boundary (`pattern_is_literal_set` in `re2_wrapper.cpp`, beside `pattern_is_char_set`, with the lexer's `lambda_lexer_is_ident_continue_byte` as the byte test — the one word predicate the loader, the engine and the boundary share). Design §4.2.
- **The growth loop chained non-associative operators.** `prec(2, rel, "<", rel)` still parsed `1 < 2 < 3`, because the loop re-admitted the same alternative with the grown seed. Fixed by the growth cap (design §5.2 step 5); `left_recursion.ls` pins both the refusal and `1 < 2 and 2 < 3`.
- **Adjacent token strings merge** in element content (S2.6.4, `mark_builder.cpp` ~500): `"for", name: id, "in"` gave `"forin"`. USER ruled GM4.2 (symbol rules emit symbols, which never merge); `=`-rule symbol literals remain open as GM-O3.

### 7.4 Evidence

- Fixtures `test/lambda/grammar/{calc,json,tokens,left_recursion,errors,types}.ls` (+ `mod_arith.ls`): green on `test_lambda_gtest` under `LAMBDA_EXEC_BACKEND=interp`, `jit` and `auto`; `json.ls` identical under `LAMBDA_GC_FORCE_EVERY=1` in both tiers.
- `NegativeScriptTest.Grammar*`: 10/10.
- `make test-lambda-baseline` (LANG=en_US.UTF-8, 2026-10-11 22:3x, tree = this work plus the separate session's `transpile-mir.cpp` fix, since committed as b6fa56bfb): 6,662/6,663; the one failure, `edit_view_only` (`<button disabled>` vs `disabled=""`), fails identically on pristine HEAD fa339486d. Without a UTF-8 `LANG` the three `LambdaReplSessionTests` prompt checks fail (`>` instead of `λ>`), on HEAD too.
- Forced-JIT sweep (not a gate): `latex_test_latex_phase3_corpus` (E308) and four `math_test_math_*` ("map literal spread requires a map-like value") fail under `LAMBDA_EXEC_BACKEND=jit` only; the other session A/B-built them against HEAD's `transpile-mir.cpp` with this work present and saw identical failures. Not A/B-tested against a tree without this work; no package source uses the word `grammar` (one comment), and nothing here touches lowering.
- Stress: a 5,000-term `+` chain parses to a depth-5,001 tree (iterative conversion); 3,000 nested parentheses stop at the 1,000-rule nesting bound with `maximum grammar nesting exceeded at 1:501`.
- REPL: an unterminated body is INCOMPLETE (`..` continuation), then evaluates.

### 7.5 Known issues

- **JIT type-alias reads in imported modules** (found here, not grammar-specific): under forced JIT a module-level type alias read inside a function of an *imported* module was wrong (`0`/`null`), so a `pub grammar` used there read `null` (2026-10-06 builds onward). Fixed the same day by a separate session in `transpile-mir.cpp` (local master b6fa56bfb) (an expression typed as a type value is no longer unwrapped to its payload lane; fixture `test/lambda/import_type_values.ls`). `types.ls` covers the grammar case (`sum_tree` in `mod_arith.ls`) on all three tiers.
- `text is g.rule` does not parse (the type sub-language reads no dotted names); design GM-O8.
- `make build-test` fails to link two Radiant test executables (`render_paint_boundary.o`, `layout_counters.o` undefined symbols) — unrelated to this change; `test_lambda_gtest` and `test_lambda_errors_gtest` build and pass.

## 8. P4 and P5 record (2026-10-11)

USER rulings first: GM4.3 (`'x'` in a `=` rule is a rule name), GM5.4 (`text is g.rule` is not valid syntax — `let r = g.rule; text is r`; `input(target, g)` not supported), GM11v2 (spans via a third-argument options map), and the P5 scope (the XPath guest only parses into a Lambda tree, Tree-sitter style; no evaluation, no `lm.` lowering — `compile`, S15.3, is unimplemented).

### 8.1 What landed

| Item | Result |
|---|---|
| GM4.3 | `quoted_name` in `grammar_load.cpp`: a `'symbol'` atom is a rule reference, and before `:` a label. The old "a symbol is never a token" error is gone; `grammar_symbol_literal.ls` became `grammar_quoted_rule_name.ls` (an unknown quoted name is E204). |
| GM5.4 | Verified on T0 and JIT: `let r = calc.expr` then `"1 + 2" is r` (fixture `types.ls`). Nothing to build. |
| GM11v2 | `fn_parse_grammar` reads `{rule, spans}` from a map third argument; `grammar_parse(…, spans)` adds `start`/`end` attributes in `convert`. An element's start is its first child's start (`wrap`), so leading whitespace is excluded. |
| Composition | `parse_import` / `compose_imports` / `clone_expr`: imported rules are cloned with their analysis undone (implied `prec` wrappers unwrapped, call modes and indexes reset) and their references resolved again by name, so local overrides reach them; a later import overrides an earlier one; an import written before every local rule supplies the entry; only local rules are linted and imported spans report at the `import`. `rule_head_at` treats `import name` as a statement boundary. |
| REPL | Multi-line grammar entries continue (`..`) and evaluate; `g`, `g.x`, `parse` work across entries. `.env` lists the grammar as `null`, as it does for every `type` alias (pre-existing). |
| P5 | `lmd/package/grammar/xpath.ls` → `import lambda.grammar.xpath` gives `pub grammar xpath`. One left-recursive `Expr` with 18 levels (or … postfix), `left`/`op`/`right`, `operand`, `base`+`predicate`/`args`/`lookup` labels; operators and keywords as symbol tokens; structural punctuation hidden; named nodes for steps, literals, function calls, FLWOR-lite, constructors, types and kind tests. Comparisons and `to` use `prec.none` (non-associative, as the spec requires). |

### 8.2 Found while doing it

- **`prec.none` (GM6.1, design GM-O10).** XPath's comparisons are deliberately non-associative, and the lint warned on every import of the package. `prec.none(n, …)` means bare `prec` without the warning.
- **`$` made literals keyword-like.** `$` is a Lambda identifier byte, so `"$"` got the word boundary and failed before a name (`$x`). `keyword_char` excludes it.
- **Island references did not count as uses.** A token rule named only inside another rule's island (`space := ("\s" | xcomment)+`) was reported unused; `mark_island_refs` walks the resolved island.
- **Error messages named hidden helpers** (`_lparen`). A single-literal token rule now displays as its literal (`"("`, `'or'`); the expected-set cap rose from 16 to 32 so a decisive entry (`'return'`) is not cut.

### 8.3 Evidence

- `test/lambda/grammar/{compose,xpath}.ls` new; `tokens.ls` (quoted names) and `types.ls` (GM5.4 idiom) extended; all 9 grammar fixtures green under `interp`, `jit` and `auto`.
- `NegativeScriptTest.Grammar*`: 11/11 (adds `GrammarImportUnknownGrammar`, renames to `GrammarQuotedNameIsARuleName`).
- `make test-lambda-baseline` (LANG=en_US.UTF-8): 6,665/6,666; the one failure is `edit_view_only`, which fails identically on fa339486d.
- `xpath.ls` identical under `LAMBDA_GC_FORCE_EVERY=1`.
- XPath sweep (63 expressions in `xpath.ls`): paths, axes, abbreviations, wildcards, `Q{}` names, literals, all operator levels, arrows, FLWOR-lite, quantifiers, `if`, inline functions, maps, arrays, lookups, kind tests, comments; six spec violations rejected with furthest-failure messages.

### 8.4 Still open

GM-O1 (formal-spec placement), GM-O2 (explicit levels), GM-O5 (packrat default), GM-O6 (semantic predicates), ~~local `:=` rules naming imported `:=` rules~~ (done 2026-10-12, §9), nested XPath comments, and the guests after XPath (jq, XQuery, RELAX NG compact).

## 9. Follow-up: imported token rules in local islands (2026-10-12)

| Item | Result |
|---|---|
| Local `:=` naming an imported `:=` | Local `:=` rules no longer compile while the body parses: `parse_rule` queues them (`PendingToken`), and `compile_token_rules` runs after `compose_imports`. It first publishes every imported `:=` copy left in the composed table (exactly those no local rule overrides) into the private token scope, from the island AST each rule now keeps (`GrammarRule::pattern_body`, set by `publish_token_rule`), then compiles the local ones in source order. So an island sees the imports wherever the `import` is written, the local rules written before it, and a local override instead of the imported rule. |
| Composition across modules (found by this fix) | Name lookups compare interned pointers, and each module has its own name pool, so `import g` of a grammar declared in another module (`import lambda.grammar.xpath`, then `grammar xq { import xpath … }`) could neither reference an imported rule by name (E204 "no rule named 'integer'"), nor override one, nor find the imported entry. P4 tested composition within one module only. `compose_imports` and `clone_expr` now intern imported rule names, references and the entry name into the importer's pool. |
| Still open | A local override does not reach the islands of imported `:=` rules: those were compiled where declared and inline the declaring grammar's definitions (design §4.5). |

Evidence: `compose.ls` gains `tagged` (override then reference), `tag_first` (a `:=` written before the import), `two_imports` (a later import's token wins) and `xq` (a cross-module XQuery-shaped extension: an overridden `IntegerLiteral`, an `ExtensionExpr` inside an overridden `ParenthesizedExpr`, and `qname := ncname ":" ncname` over XPath's token). All grammar fixtures green under `interp`, `jit` and `auto`; `compose.ls` identical under `LAMBDA_GC_FORCE_EVERY=1`; `NegativeScriptTest.Grammar*` 11/11. The full Lambda baseline was not rerun: the change is confined to `grammar_compile`.

## 10. M4 jq and M5 XQuery, RELAX NG compact (2026-10-12)

USER ruling first: **GM13** — `~x` matches `x` where the previous token ended (Tree-sitter's `token.immediate`), for jq's string interpolation and XQuery's direct constructors. It was chosen after comparing with Tree-sitter over the two `nospace` alternatives (a `spaced` exit modifier, per-rule `nospace`); USER picked the `~` spelling over `immediate(x)`. The three guests follow the P5 scope (parse into a Lambda tree, Tree-sitter style; no evaluation).

### 10.1 What landed

| Item | Result |
|---|---|
| GM13 `~x` | Loader: `GT_TILDE`; `parse_postfix` marks the atom (`GrammarExpr::immediate`), accepting only a literal or a rule name (a group is E100), and `resolve_refs` rejects `~` on a `=` rule (E200). Engine: `match_literal` and `match_token(…, immediate)` skip no `space` for a marked reference. A token has no memo entry, so the mark lives on the call site and the memo keys are unchanged. |
| `lambda.grammar.jq` | `lmd/package/grammar/jq.ls`, from jq 1.7.1's `parser.y` and `lexer.l`. One left-recursive `Exp` whose levels are bison's precedence lines in order (FUNCDEF 1 … `try`/`catch` 13–14); `Term` is jq's Term (postfix chain with optional `?`); `ObjVal` is ExpD; `Program`, `Module`, `Import`, `FuncDef`, destructuring with `?//`, `reduce`/`foreach`/`if`/`try`/`label`. Strings: `_dq, (~text \| Interpolation)*, ~_dq`. Keywords are excluded from identifiers by `!keyword`; `$__loc__` by `!~"\w"`. |
| `lambda.grammar.rnc` | `lmd/package/grammar/rnc.ls`, from the OASIS compact-syntax spec. `Group` / `Interleave` / `Choice` are repetitions over a memoized `_Particle` (the spec forbids mixing them without parentheses; without `memo` the three-way retry is exponential in nesting depth); name classes, datatypes with parameters and `-` exceptions, `grammar`/`div`/`include`/`external`/`parent`, annotations, follow annotations, documentation comments, `\keyword` identifiers. A literal's segments are `~` text tokens between hidden quotes, so `"a" ~ 'b'` merges to `"ab"` by S2.6.4. |
| `lambda.grammar.xquery` | `lmd/package/grammar/xquery.ls`: `import lambda.grammar.xpath`, then `grammar xquery { Module = …; import xpath; … }` (a local rule before the import keeps `Module` the entry). Overrides `_ExprSingle` (a hidden hook added to `xpath.ls`; XPath's tree is unchanged), `_Primary`, `Binding`, `LetBinding`, `InlineFunctionExpr`. Prolog (setters, namespaces, imports, then declarations), full FLWOR with window clauses, switch, typeswitch, try/catch, direct constructors with `~` text, computed constructors, ordered/unordered, validate, pragmas, string constructors. |

### 10.2 Found while doing it

- **String-constructor text cannot be a separate token.** Its delimiters (``]``` `` and `` `{ ``) start with characters the text may contain, and a greedy text token overshoots (`a]]``` read `a]]`` as text). The pieces include their delimiters (`sc_start := "``[" chars "`{"`), with the text written as an automaton for "contains neither delimiter" so RE2 finds the boundary inside one token. CDATA, comments and PIs are single tokens for the same reason.
- **Word boundaries use Lambda's identifier bytes**, which exclude `-` and `.`; RNC and XQuery names contain both. Identifier exclusion therefore checks the following byte itself (`!(keyword, !~name_char)` in RNC); structural keywords match the front of a name like `element-x` and recover by backtracking.
- **jq's `try A catch B`**: the body is a middle self-reference, read at level 0 (§5.2 step 4), so the grammar accepts `try a | b catch c`, which jq rejects. Every program jq accepts parses the same way.

### 10.3 Evidence

- Fixtures `test/lambda/grammar/{jq,rnc,xquery}.ls` (52, 12 and 37 inputs, plus 7, 4 and 5 error cases); `tokens.ls` gains `~` cases; `NegativeScriptTest.GrammarImmediateMarksATokenRule` and `GrammarImmediateMarksOneAtom`. All 14 grammar fixtures green under `interp`, `jit` and `auto`, and identical under `LAMBDA_GC_FORCE_EVERY=1`; `NegativeScriptTest.Grammar*` 13/13; `xpath.txt` unchanged by the `_ExprSingle` hook.

### 10.4 Still open

Nested `(: :)` comments in XPath and XQuery (needs a recursive `space`); end-tag names checked against start tags (no backreferences; GM-O6); string text and references kept raw (no decoding); RNC `\x{…}` escapes; the local-override-in-imported-island limit (§9).
