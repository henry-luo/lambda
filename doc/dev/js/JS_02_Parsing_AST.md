# LambdaJS — Parsing, AST & Front-End Validation

> **Last verified against tree:** 2026-08-30

> **Part of the [LambdaJS detailed-design set](JS_00_Overview.md).** This document describes the first-party JavaScript and TypeScript frontend: direct lexical recognition, typed AST construction, lexical-scope rebuilding, and early-error validation. LambdaJS uses no production Tree-sitter AST builder or CST adapter; vendored JS/TS grammars are test-only differential inputs. This boundary follows **D8.1.3v10**. The MIR consumer is [JS_04 — MIR Lowering](JS_04_MIR_Lowering.md), and the pipeline is [JS_01 — Compilation Pipeline](JS_01_Compilation_Pipeline.md).
>
> **Primary sources:** `lambda/js/parser/js_lexer.c`, `lambda/js/parser/js_parser.c`, `lambda/js/js_c_parser.cpp`, `lambda/js/js_c_ast_helpers.cpp`, `lambda/js/js_scope.cpp`, `lambda/js/js_early_errors.cpp`, `lambda/js/js_ast.hpp`, `lambda/js/js_print.cpp`, and `lambda/js/js_transpiler.hpp`.
> **Audience:** engine developers. **Convention:** named symbols are authoritative; line references drift.

---

## 1. Production direct frontend

`js_transpiler_parse` unconditionally delegates to `js_transpiler_parse_c`. It selects `JS_PARSE_SCRIPT` or `JS_PARSE_MODULE`, and adds `JS_PARSE_TYPESCRIPT` only for the TypeScript profile. `js_parser_parse_source` combines recursive descent for declarations, statements, patterns, classes, modules, and delimited forms with Pratt parsing for expressions and TS types.

Its reduction sink immediately creates the retained `JsAstNode` hierarchy and then rebuilds the direct scope graph, validates early errors, and publishes `AstIndex` facts. There is one AST pool (`MEM_ROLE_AST`) and one name pool for frontend-owned nodes and strings. On a syntax error, `JsParseError` carries the original-source span, expected-token bits, and message; no partial AST is published.

The vendored JS/TS grammars remain available only to the independent parser-differential test. They cannot select, build, or execute an alternate LambdaJS AST path. This keeps one production compiler/runtime boundary while preserving an external acceptance oracle, as required by **D8.6.4v2**.

## 2. AST model

Every AST node starts with `JsAstNode`: node type, inferred `Type*`, sibling link, and source span. Subtypes embed that header first. `NameEntry::node` can therefore use the common `AstNode*` contract while referring to JS nodes.

Sibling lists carry program bodies, blocks, arguments, and object properties through `base.next`. `alloc_js_ast_node` clears a pool allocation and initializes the node type/span. `js_operator_from_string` is centralized in `js_c_ast_helpers.cpp`, so direct JS/TS reductions share one source-text-to-`JsOperator` mapping.

`JsAstNodeType` covers the core statement/expression forms, classes, patterns, modules, generators, async forms, and TypeScript extension nodes. `JsForOfNode` and `JsForInNode` share a physical layout; their tags distinguish their semantics. `JsLiteralNode` retains number/string/boolean/null/undefined identity plus decimal and BigInt decode facts.

## 3. Scope rebuilding and strict mode

`js_scope.cpp` owns `JsScope` creation, push/pop, definition, lookup, parser lifecycle, and script publication. `var` definitions choose the nearest function/global scope; lexical definitions remain in the current block/module scope. Scope lookup supplies available declaration type information while direct AST helpers construct identifiers and declarations.

The direct AST records strict-mode directive facts. `js_ast_statement_list_has_use_strict_directive` and `js_ast_body_has_use_strict_directive_source` recognize the raw directive prologue, and `js_c_parser.cpp` promotes program strict mode before rebuilding scopes. Function construction preserves per-body strictness and inherits it through the scope chain. This implements the hosted JavaScript semantic boundary of **S1.11** without changing Lambda truthiness or coercion.

## 4. Early-error validation

`js_check_early_errors` runs after direct AST publication and before code generation. Its context tracks strict, generator, async, class, constructor, method, parameter, iteration, switch, label, and private-name state. A violation reports through `js_error`, sets the transpiler error state, and stops the caller from entering lowering.

The validator covers assignment/update targets, reserved identifiers, destructuring-rest constraints, lexical redeclarations, strict-mode restricted syntax, function-parameter rules, `break`/`continue` target validity, and private-name lookup. It is authoritative for parse-time JavaScript errors; scope-building diagnostics remain structural support, not a separate semantic pipeline.

## 5. Frontend-to-runtime handoff

The interpreter and MIR entry points consume `js_transpiler_build_ast`, which returns the parser-published root. They do not invoke a fallback AST builder. Script import/export metadata is recorded by direct AST helpers and owned by `js_scope.cpp` with the script lifecycle. That placement satisfies **D2.4.1–D2.4.3**: shared structural ownership is centralized, while JavaScript semantic behavior remains profile-owned.

`js_print.cpp` is a debug-only AST inspection aid. It is not part of parsing, validation, or the execution contract.

## Appendix — Source map

| File | Responsibility |
|---|---|
| `lambda/js/parser/js_lexer.c`, `js_parser.c` | Direct JS/TS lexer, recursive descent, Pratt parsing, diagnostics, and reductions. |
| `lambda/js/js_c_parser.cpp` | Parse entrypoint, direct reduction sink, scope rebuild, validation, and index publication. |
| `lambda/js/js_c_ast_helpers.cpp` | AST allocation/construction, source decoding, operators, directives, and parser-shared helpers. |
| `lambda/js/js_scope.cpp` | Transpiler and script lifecycle, scope operations, and direct module metadata ownership. |
| `lambda/js/js_early_errors.cpp` | Static JavaScript early-error validation. |
| `lambda/js/js_transpiler.hpp` | Public frontend contracts and scope/transpiler layouts. |
| `test/test_js_parser_benchmark_gtest.cpp` | Direct parser regression/benchmark coverage; the grammar differential remains test-only. |

## Related documents

- [JS_00 — Overview](JS_00_Overview.md)
- [JS_01 — Compilation Pipeline](JS_01_Compilation_Pipeline.md)
- [JS_03 — Value Model, Memory & GC Interop](JS_03_Value_Model.md)
- [JS_04 — MIR Lowering](JS_04_MIR_Lowering.md)
