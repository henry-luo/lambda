# Lambda Runtime — Grammar Module

> **Last verified against tree:** 2026-10-11

> **Part of the [Lambda core-runtime detailed-design set](LR_00_Overview.md).** This document covers the grammar module: how a `grammar Name { ... }` declaration becomes a type at compile time, how its `:=` token rules reuse the string-pattern compiler, the PEG interpreter behind `parse(text, g)` — ordered choice, implicit `space`, memoization and bounded left recursion with precedence levels — the parse tree it builds, the lint, and the diagnostics.
>
> **Primary sources:** `lambda/runtime/grammar/grammar.hpp`, `grammar_load.cpp` (body parser, rule table, analysis, lint), `grammar_parse.cpp` (engine, tree conversion); the declaration path in `lambda/runtime/parser/lambda_lexer.c` / `lambda_parser.c` and `lambda/runtime/build_ast.cpp`; the type structs in `lambda/lambda-data.hpp`; the runtime arms in `lambda/runtime/lambda-eval.cpp`; the token matcher in `lambda/runtime/re2_wrapper.cpp`.
> **Design authority:** [vibe/Lambda_Design_Grammar_Parser.md](../../../vibe/Lambda_Design_Grammar_Parser.md) (GM#); implementation record in [vibe/impl/Lambda_Impl_Grammar_Parser.md §7](../../../vibe/impl/Lambda_Impl_Grammar_Parser.md). No `S#`/`D#` ruling covers grammars yet (GM-O1).
> **Audience:** engine developers. **Convention:** `file:line` references drift; confirm against the cited symbol names.

---

## 1. Purpose & scope

A grammar lets a Lambda script define a language and parse text in it into a Lambda element tree, which a Lambda function then transpiles to the `lm.` AST and `compile`s (S15.3) — D1.1's "a guest is a grammar + AST builder + LangProfile", with both halves written in Lambda. The engine is a PEG recursive-descent interpreter (GM1): no tables are generated, no conflicts exist, and the failure modes of ordered choice are caught by a load-time lint instead.

## 2. From declaration to type

**Surface.** `grammar` is a lexer keyword (`lexer_keywords[]`, `lambda_lexer.c`), barred as a binding name by `lambda_lexer_word_bars_binding`'s default arm (S16.10.1v2) and admitted as a data name by `token_is_name_word` (`lambda_parser.c`).

**Parse.** `parse_grammar_declaration` (`lambda_parser.c:2365`) takes the name and the `{`, then rewinds the lexer to just after the brace and calls `lambda_lexer_skip_block_body` (`lambda_lexer.c:413`), which skips to the matching `}` honouring quotes (with island escapes such as `"\d"`), comments and nested braces. The body therefore never reaches the Lambda lexer — the same arrangement as type slots and `parse_type_pattern_syntax`. An unterminated body at end of input fails as INCOMPLETE, so the REPL continues the entry. The reduction is `LAMBDA_REDUCE_DECLARATION` / `LAMBDA_REDUCTION_FORM_GRAMMAR`, with the name as detail token and the body span as secondary token.

**Build.** `syntax_grammar` (`build_ast.cpp:14453`) wraps a declarator of form `LSF_GRAMMAR` (tail slot: the body span) in the same `AST_NODE_TYPE_STAM` a `type Name = T` uses (D8.2.2: a form field, not a node kind). In the resolve pass `case LSF_GRAMMAR` calls `resolve_grammar` (`build_ast.cpp:15918`), which calls `grammar_compile` (`grammar_load.cpp:985`), sets `is_type_definition`, and registers the name. Because the binding is an ordinary type definition, T0's `exec_declaration` no-op, the identifier read that yields `decl->type`, MIR's `mir_emit_declared_type_value` and the prepass slot skip all apply with no grammar-specific code.

**Types.** `TypeGrammar` and `TypeGrammarRule` (`lambda-data.hpp:1315`) extend `TypeType` with kinds `TYPE_KIND_GRAMMAR` / `TYPE_KIND_GRAMMAR_RULE` (`lambda.hpp`). The `TypeType::type` payload is `&TYPE_STRING` — the domain a grammar describes — so every generic reader of a type value stays in bounds. A `:=` rule's type is its `TypePattern`; a `=` rule's is a `TypeGrammarRule` naming its grammar, index and name. The grammar, its rules, and every compiled pattern are pool-allocated and entered in the module `type_list`, so they live and die with the module like any declared type; nothing is GC-managed.

## 3. The loader (`grammar_load.cpp`)

**Body parser.** A small tokenizer (`scan_tok`) reads words, `"strings"`, `'symbols'`, integers, `:=` and punctuation, skipping comments with `type_pattern_skip_space` (promoted from `parse_type_pattern.cpp`). A rule is `[memo|nospace|hidden]* name (= | :=) body`; a modifier word directly followed by `=`/`:=` is a rule name instead. A `=` body is parsed into `GrammarExpr` nodes — choice `|` over comma sequences over prefixed (`&`, `!`, `label:`) over postfix (`? * + {n} {n+} {n,m}`) over atoms (literal, rule name, `( … )`, `prec[.left|.right](n, …)`, `hidden( … )`). A `:=` body runs to the next rule head, `;` or the end (`token_body_end`), because an island juxtaposes its parts.

**Tokens.** `compile_token_body` hands an island body to `parse_pattern_island_syntax` (`parse_type_pattern.cpp:1720`) and `resolve_type_pattern`, with `tp->current_scope` set to a private scope in which earlier token rules are published as `AST_NODE_STRING_PATTERN`/`SYMBOL_PATTERN` definitions (`publish_token_rule`). Named references therefore inline exactly as between `type` patterns, and S11.1.2v4's domain checks apply unchanged. The unanchored regex is compiled at load (`pattern_get_unanchored`) so no parse compiles lazily. A literal containing a class escape (`"\d"` in a `=` rule) becomes an anonymous token rule named by its spelling. `pattern_is_literal_set` (`re2_wrapper.cpp:404`), given the lexer's `lambda_lexer_is_ident_continue_byte`, marks literal-only word tokens (`for := 'for'`) for the word boundary (§4).

**Analysis** (`analyse`, `grammar_load.cpp:913`). Nullability is a fixpoint (a token is nullable when its pattern matches `""`). `first_refs` computes the nullable-prefix graph: rule A → B when B can be called at A's starting position. A self-edge is direct left recursion; `check_indirect_left_recursion` finds longer cycles and reports each at its lowest-numbered rule (E215, GM8). `classify_left_recursion` (`:696`) requires each left-recursive alternative to begin with the rule itself (else E215), wraps unannotated ones in an implied `prec.left(0)`, sets `lr_alt`, marks the leading reference `GM_CALL_SEED` and a trailing self-reference `GM_CALL_TRAILING` at level `n + 1` (left, bare) or `n` (right), and records `trailing_self`.

**Lint** (`lint`, `lint_expr`; GM9). Literal reordering inside a choice (a longer literal moves ahead of its own prefix, with a warning), partial capture by an earlier literal, unreachable alternatives after a nullable one (E223), nullable repetition, mixed annotated/implied precedence, non-associative chains, repeated leading rule (suggests `memo`), unused rules, a missing `space` rule, and a `space` rule that is the implicit entry. Warnings go through `record_semantic_warning_span` (`build_ast.cpp:1545`) into `tp->warnings`, which the runner prints with source context; the lint is skipped for a grammar whose body already had errors.

## 4. The engine (`grammar_parse.cpp`)

**State.** `ParseState` holds the source, position, a scratch `Arena` and a stack of scratch `PNode*`; both die at the end of the call. A rule wraps everything pushed since its entry into one element node; a token pushes a text node. Failure restores position and truncates the stack, so a failed branch costs only arena bytes.

**Matching.** `match_literal` skips `space`, compares bytes, and enforces the word boundary for word-character literals. `match_token` skips `space` (except for the space rule itself) and calls `pattern_match_prefix_at` (`re2_wrapper.cpp:1060`) — RE2 `ANCHOR_START` at the offset, leftmost-first — then applies the same boundary to literal-only word tokens. `skip_space` repeats the space pattern until it stops advancing. A `nospace` rule entered from spaced text skips space once, then disables skipping inside.

**Expressions.** `parse_expr` interprets sequences, ordered choice (GM2), greedy repetition with a no-progress stop, lookahead (which restores the furthest-failure record so its own failures do not appear in messages), labels (applied to copies of the pushed nodes, since memoized nodes are shared) and `hidden(rule)`. A hidden rule's element is replaced by its children at the call site.

**Memoization.** `memo` rules and every left-recursive rule cache `(rule, position, level | nospace)` → result in a hash table whose values point to arena records, so records stay valid across rehashing.

**Left recursion** (`parse_left_recursive`, `:356`; GM6). The seed comes from the first succeeding non-left-recursive alternative. The growth loop then tries each `lr_alt` whose precedence is at least the call level and at most the current cap: its leading reference takes the seed, the rest parses normally, and a step that advances becomes the new seed and restarts the loop. After a step whose alternative ends in the rule itself, the cap drops to `n` (left) or `n − 1` (bare, right) — the right operand already took every higher operator, and without the cap the loop itself would chain a non-associative operator. Postfix steps set no cap.

**Limits.** Rule nesting stops at 1,000 levels or when the native stack nears its guard (`lambda_stack_pointer` vs `_lambda_stack_limit + LAMBDA_STACK_THROW_HEADROOM`), reported with the position where it was hit.

**Tree.** `convert` (`:554`) turns the winning scratch tree into elements on `MarkBuilder`, post-order with an explicit work list so a 5,000-term left-associative chain needs no deep recursion. A rule becomes `<rule …>`; a literal or `"..."` token text becomes a string and a `'...'` token a symbol (GM4.2); labelled children become attributes (an array when one label repeats). Adjacent strings merge in element content (S2.6.4) — the reason GM4.2 exists. The tree lives in an `Input` from `InputManager::create_input`, owned like every other parse result (D4.2.6).

**Errors** (GM12). Every token failure records its expected text at the furthest position; the message is `grammar 'g': expected "+", "*" or end of input at 1:7, found ")"`, stored in `input->parse_error_message` and raised as E407 through `set_input_parse_error`, exactly as the format parsers do (GM5.2).

## 5. Runtime surface (`lambda-eval.cpp`)

- `fn_parse2` takes the grammar arm when its second argument is a grammar or rule type; `fn_parse_grammar` (`:6182`) picks the entry (first rule, the rule type's own, or the symbol given to `fn_parse3` (`:6228`, `SYSFUNC_PARSE3`)) and runs `grammar_parse`.
- `fn_is` (`:2871`) answers `text is g` with `grammar_accepts` (GM5.3; no tree is built).
- `fn_member` (`:7223`) answers `g.rule` with the rule's type; an unknown rule reads `null`.
- `print.cpp` prints a grammar by name and a rule as `g.rule`.

Not yet: `input(target, g)` (GM-O9) and `text is g.rule`, which the type sub-language cannot spell (GM-O8).

## 6. Tests

`test/lambda/grammar/*.ls` with goldens (registered in `FUNCTIONAL_TEST_DIRECTORIES`): `calc` (precedence traces, entry forms, membership), `json` (`nospace`, hidden token, labels), `tokens` (GM4.2 domains, keyword boundaries, lookahead, `memo`), `left_recursion` (postfix, ternary, implied `prec.left(0)`, non-associative refusal), `errors` (messages and the nesting limit), `types` (type values, members, a `pub grammar` across modules, a function-local grammar). Compile errors and every lint warning: `NegativeScriptTest.Grammar*` in `test/test_lambda_errors_gtest.cpp` over `test/lambda/negative/{semantic,syntax}/grammar_*.ls`.
