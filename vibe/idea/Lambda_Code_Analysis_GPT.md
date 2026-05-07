# Lambda Code Semantic Analysis Framework Proposal

**Status**: Draft proposal (by GPT 5.5)

## Prompt
Write a proposal to develop code semantic analysis framework under Lambda.
stage 1 is to analyze the C/C++ code of the Lambda/Radiant code itself.
use treesitter c/c++ parser to parse the source code into syntax tree.
then a simple adaptor/builder to turn the syntax tree into AST as lambda node tree.
AST can reference CodeQL or any other similar framework with best design.
AST should be able to generalize to support many languages (C/C++, JS, Python, etc.).
then use Lambda script to analyze, query, transform the AST.

Some initial test cases can be:
1. check the headers files, and certain C header file should only be used in ./lib;
2. certain C++ features, like std::string, std::vector, std::map are not used in Lambda/Radiant code;
3. malloc, calloc, free, not used;
4. type inference and verify that Lambda item access casting are always checking/based on the type_id;

Reference existing code semantic analysis frameworks, and adopt the best designs/features from them. doc them in the proposal as prior art. then propose our design.

## Overview

This proposal describes a new code semantic analysis framework built under Lambda. The first milestone is intentionally self-hosting: analyze the C/C++ source code of Lambda and Radiant themselves, then express repository rules as Lambda scripts over a queryable AST.

The framework should start simple and practical:

1. Use Tree-sitter C and C++ parsers to parse source files into concrete syntax trees.
2. Convert those syntax trees through a small language adaptor into a normalized Lambda node tree.
3. Add a language-neutral semantic layer for declarations, scopes, types, call sites, control flow, data flow, and diagnostics.
4. Let Lambda scripts query, validate, report, and eventually transform the resulting code model.

The long-term goal is a general code analysis substrate for many languages, including C/C++, JavaScript, Python, Bash, Ruby, and Lambda itself. Stage 1 focuses on C/C++ because Lambda and Radiant already need stronger enforcement of local engineering rules and implementation invariants.

## Goals

- Build a code AST as normal Lambda data so analysis rules are written in Lambda Script, not baked into C++.
- Reuse Tree-sitter for fast, robust parsing and language coverage.
- Preserve enough source fidelity for accurate diagnostics and future transforms.
- Support language-neutral queries across C/C++, JavaScript, Python, and other languages.
- Add a practical semantic layer for names, types, call sites, include relationships, and selected data-flow facts.
- Make Stage 1 useful immediately for Lambda/Radiant repository checks.
- Keep the implementation modular: parsers are replaceable frontends, normalized AST is stable, semantic overlays are incremental.

## Non-Goals

- Do not attempt full C++ compiler semantics in Stage 1.
- Do not replace Clang, CodeQL, or language servers for their strongest use cases.
- Do not require a complete compile database before simple rules can run.
- Do not build a full theorem prover for Lambda runtime correctness in the first stage.
- Do not hard-code repository checks into the AST builder. Checks should live as Lambda scripts.

## Prior Art

### CodeQL

CodeQL extracts each supported language into a database containing AST, control-flow graph, data-flow graph, and language-specific schema relations. Query libraries provide object-oriented abstractions over database tables such as expressions and statements. Results can include single locations or paths through a flow graph.

Best ideas to adopt:

- Separate extraction from query execution.
- Store a stable queryable database, not just transient parser output.
- Provide language-specific libraries on top of a language-neutral core.
- Model AST, CFG, DFG, declarations, and type information as first-class query relations.
- Return diagnostics with precise source locations, messages, severity, and optional paths.

What to avoid initially:

- Heavy extractor complexity for compiled languages.
- A separate query language when Lambda itself can serve as the query language.

### Joern and Code Property Graphs

Joern uses the Code Property Graph model: a directed, edge-labeled, attributed multigraph that merges AST, control flow, and data flow. It uses overlays to add derived representations and supports cross-language querying through a common graph abstraction.

Best ideas to adopt:

- Represent multiple views of code over the same nodes: AST edges, CFG edges, DFG edges, call edges, reference edges, include edges.
- Use overlays so Stage 1 can begin with AST plus shallow semantics, then add richer analysis without changing the base AST format.
- Keep nodes language-neutral where possible but allow language-specific properties.
- Treat findings as graph/data nodes so scripts can filter, group, transform, and format them.

### Clang AST and LibTooling

Clang's AST is very source-faithful for C/C++ and includes detailed declarations, types, implicit casts, templates, and bindings. It is excellent for precise refactoring tools and compiler-level semantics.

Best ideas to adopt:

- Preserve source constructs rather than immediately lowering away parentheses, casts, declarations, and macro-relevant structure.
- Distinguish declarations, statements, expressions, types, and declaration contexts.
- Keep source ranges on every node.
- Consider an optional Clang importer later for high-precision C++ analysis.

What to avoid initially:

- Depending on complete compiler setup for every basic rule.
- Exposing a C++-only AST shape as the universal model.

### Tree-sitter

Tree-sitter is a fast, incremental parsing library that produces concrete syntax trees and has parsers for many languages. Lambda already embeds Tree-sitter for Lambda, JavaScript, TypeScript, LaTeX, Bash, Python, and Ruby related work.

Best ideas to adopt:

- Use Tree-sitter as the default parser frontend for broad language coverage.
- Keep parse errors and partial trees so analysis can still report useful findings in incomplete files.
- Use parser node kinds and fields to build small adaptors per language.
- Use Tree-sitter queries for cheap lexical/structural prefilters where useful.

### Semgrep and ast-grep

Semgrep and ast-grep focus on approachable structural matching and rewriting. They make common code review and security rules easy to author without full compiler semantics.

Best ideas to adopt:

- Provide a simple pattern layer for common structural checks.
- Make rules small, testable, and repository-local.
- Support automated rewrites once source-preserving edits are reliable.
- Allow rule metadata: id, message, severity, category, examples, and fix suggestions.

### srcML

srcML represents source code as XML for exploration, analysis, manipulation, and XPath queries across multiple languages.

Best ideas to adopt:

- Keep a human-inspectable serialized form of the code AST.
- Make source code analysis feel like data processing.
- Preserve comments, includes, declarations, and source text where needed for transformation.

### Language Server Protocol

LSP is not a static analysis database, but it provides good product-level ideas: diagnostics, code actions, document synchronization, cancellation, progress, and stale-result handling.

Best ideas to adopt later:

- Emit diagnostics in a format that can map naturally to editor diagnostics and SARIF.
- Support incremental re-analysis for editor integration.
- Support code actions and workspace edits once the transform layer is mature.

## Proposed Design

### Core Pipeline

```text
source files
  -> language discovery
  -> Tree-sitter parse
  -> language adaptor
  -> normalized Lambda AST
  -> semantic overlays
  -> Lambda query scripts
  -> findings, reports, transforms
```

Stage 1 should expose a command shaped like this:

```bash
./lambda.exe code scan lambda radiant --rules lambda/package/code/rules/lambda_repo.ls
./lambda.exe code ast radiant/layout_grid.cpp -o temp/layout_grid.code.json
```

The exact CLI can evolve, but the design should keep three operations separate:

| Operation | Purpose |
|-----------|---------|
| `code ast` | parse source files and emit normalized AST data |
| `code index` | build semantic overlays and persist a query database |
| `code scan` | run Lambda rule scripts and emit findings |

### Major Components

| Component | Responsibility |
|-----------|----------------|
| `CodeLanguageRegistry` | maps extensions and file hints to parser/adaptor pairs |
| `CodeParser` | wraps Tree-sitter parser creation, parse errors, and source ranges |
| `CodeAstBuilder` | converts Tree-sitter CST into normalized Lambda node tree |
| `CodeSemanticBuilder` | derives scopes, declarations, refs, types, calls, and flow edges |
| `CodeIndex` | stores files, nodes, edges, symbols, diagnostics, and analysis metadata |
| `CodeRuleRunner` | loads Lambda rule scripts and passes the code index as data |
| `CodeReporter` | formats findings as text, JSON, SARIF-like JSON, or editor diagnostics |
| `CodeTransform` | later phase for validated source edits and codemods |

Suggested source layout:

```text
lambda/code/
  code_ast.hpp
  code_ast.cpp
  code_parser.hpp
  code_parser.cpp
  code_semantic.hpp
  code_semantic.cpp
  code_index.hpp
  code_index.cpp
  adaptors/
    code_c.cpp
    code_cpp.cpp
    code_js.cpp
    code_python.cpp
lambda/package/code/
  query.ls
  rules/
    lambda_repo.ls
    c_memory.ls
    cpp_style.ls
    item_type_guard.ls
test/code/
  fixtures/
  expected/
```

Build-system changes should be made through `build_lambda_config.json`, then regenerated through the normal `make` flow.

## Normalized AST Model

The normalized AST should be a Lambda node tree, not a C++ class hierarchy exposed directly to rules. Each node should have a compact stable schema:

```text
CodeNode = {
  id: int,
  kind: symbol,
  lang: symbol,
  file: int,
  range: SourceRange,
  name: string?,
  type: TypeRef?,
  value: any?,
  flags: [symbol],
  children: [CodeNode],
  props: Map
}
```

`kind` should use language-neutral names where possible:

| Category | Common node kinds |
|----------|-------------------|
| File | `translation_unit`, `module`, `script` |
| Declarations | `function_decl`, `method_decl`, `var_decl`, `param_decl`, `type_decl`, `field_decl`, `namespace_decl` |
| Statements | `block_stmt`, `if_stmt`, `switch_stmt`, `case_stmt`, `for_stmt`, `while_stmt`, `return_stmt`, `break_stmt`, `continue_stmt`, `expr_stmt` |
| Expressions | `call_expr`, `member_expr`, `index_expr`, `cast_expr`, `binary_expr`, `unary_expr`, `assign_expr`, `conditional_expr`, `literal_expr`, `name_expr` |
| Types | `builtin_type`, `pointer_type`, `reference_type`, `array_type`, `function_type`, `qualified_type`, `template_type` |
| Preprocessor | `include_directive`, `macro_define`, `macro_call`, `ifdef_block` |
| Comments | `comment` |

Language-specific Tree-sitter node names should be retained in `props.ts_kind` so rules can drop down when needed.

### Source Model

Every node and finding should carry source information:

```text
SourceRange = {
  start_byte: int,
  end_byte: int,
  start_line: int,
  start_col: int,
  end_line: int,
  end_col: int
}
```

The framework should also store source snippets by file id, but rules should query structured nodes first and only use text as a fallback.

### Edge Model

AST containment is not enough for semantic analysis. The index should support typed edges:

| Edge | Meaning |
|------|---------|
| `child` | syntactic containment |
| `next_sibling` | source order among siblings |
| `declares` | scope declares a symbol |
| `refers_to` | reference resolves to declaration |
| `calls` | call expression targets function/method candidate |
| `has_type` | expression or declaration has inferred type |
| `cfg_next` | possible next control-flow node |
| `dfg_next` | value/data dependency |
| `includes` | file includes another file |
| `dominates` | guard dominates a checked node or block |
| `finding_at` | finding attaches to source node |

This is a Code Property Graph style model, but serialized as Lambda-friendly arrays/maps first. A compact binary or packed representation can come later if needed.

### Semantic Overlays

The base AST should be cheap to build. More expensive analysis should be layered as overlays:

| Overlay | Stage | Contents |
|---------|-------|----------|
| `parse` | 1 | files, parse errors, normalized AST, comments, includes |
| `symbols` | 1 | scopes, declarations, local references, simple macro names |
| `types` | 1 | shallow C/C++ declarator types, typedefs, struct fields, Lambda Item facts |
| `calls` | 1 | direct calls, function-like macros, member-call names |
| `cfg` | 1.5 | block-local control-flow edges |
| `dataflow` | 2 | local value flow and argument-to-parameter flow |
| `interproc` | 3 | cross-function summaries and path queries |
| `transform` | 3 | source edit planning, conflict detection, rewrite previews |

The key design point is that rules can ask for the overlays they need. A header restriction rule only needs `parse`; an Item type guard rule needs `symbols`, `types`, and local `cfg`.

## C/C++ Stage 1 Adaptor

Stage 1 should add Tree-sitter C and C++ parsers and build enough C/C++ semantics to support Lambda/Radiant repository checks.

### Frontend Inputs

- Source roots: `lambda/`, `radiant/`, and optionally `lib/`.
- File kinds: `.c`, `.h`, `.cpp`, `.hpp`.
- Exclusions: generated parser files, vendored dependencies, build outputs, and `test/` unless a rule explicitly opts in.
- Optional compile hints: include directories and platform defines from `build_lambda_config.json`.

### C/C++ Node Mapping

The adaptor should normalize the common C/C++ constructs first:

| Tree-sitter C/C++ concept | Normalized node |
|---------------------------|-----------------|
| `translation_unit` | `translation_unit` |
| `preproc_include` | `include_directive` |
| `function_definition` | `function_decl` with body |
| `declaration` | `var_decl`, `field_decl`, `type_decl`, or `function_decl` |
| `parameter_declaration` | `param_decl` |
| `struct_specifier`, `class_specifier` | `type_decl` |
| `field_declaration` | `field_decl` |
| `call_expression` | `call_expr` |
| `field_expression` | `member_expr` |
| `subscript_expression` | `index_expr` |
| `cast_expression`, `static_cast_expression` | `cast_expr` |
| `if_statement`, `switch_statement` | `if_stmt`, `switch_stmt` |
| `case_statement` | `case_stmt` |
| `return_statement` | `return_stmt` |

### Minimal C/C++ Type Model

Stage 1 only needs shallow types:

```text
TypeRef = {
  kind: symbol,
  name: string?,
  qualifiers: [symbol],
  pointee: TypeRef?,
  args: [TypeRef],
  decl: node_id?
}
```

Examples:

- `Item` -> `{ kind: named, name: "Item" }`
- `String*` -> `{ kind: pointer, pointee: { kind: named, name: "String" } }`
- `const char*` -> `{ kind: pointer, pointee: { kind: builtin, name: "char", qualifiers: [const] } }`
- `std::vector<int>` -> `{ kind: template, name: "std::vector", args: [...] }`

This is enough for banned type checks and for many Lambda Item guard checks. Full overload resolution, template instantiation, and macro expansion can be deferred.

## Lambda Query Layer

Rules should be Lambda scripts that consume the code index as data. The query package should provide helpers so most rules are declarative:

```text
let banned_std_types = ["std::string", "std::vector", "std::map"]

code.nodes(index)
  ?{ kind: 'type_ref }
  | where .qualified_name in banned_std_types
  | finding(.range, "lambda.cpp.no_std_container", "Use Lambda lib types instead")
```

The actual Lambda syntax can be adjusted to current language features, but the package should expose concepts like:

| Helper | Purpose |
|--------|---------|
| `code.files(index)` | iterate files |
| `code.nodes(index, kind?)` | iterate AST nodes |
| `code.children(node, kind?)` | traverse AST children |
| `code.ancestors(node, kind?)` | inspect parents |
| `code.descendants(node, kind?)` | inspect subtree |
| `code.refs(index, decl)` | find references to declaration |
| `code.calls(index, name)` | find calls by resolved or textual name |
| `code.includes(index, header)` | find include directives |
| `code.has_guard(index, node, fact)` | ask whether a type/value fact dominates node |
| `code.finding(node, id, message, severity)` | produce a finding |

Rule files should be normal Lambda modules. Rule metadata should be data:

```text
{
  id: "lambda.cpp.no_std_container",
  severity: "error",
  title: "Do not use std containers in Lambda/Radiant runtime code",
  roots: ["lambda", "radiant"],
  requires: ["parse", "symbols", "types"]
}
```

## Initial Stage 1 Checks

### 1. Header Usage Boundaries

Some C system headers should be isolated to `lib/` or to dedicated portability wrappers. The rule should inspect `include_directive` nodes and fail when a restricted header appears outside allowed roots.

Example policy data:

```text
restricted_headers = [
  { header: "windows.h", allowed_roots: ["lib"] },
  { header: "unistd.h", allowed_roots: ["lib"] },
  { header: "dirent.h", allowed_roots: ["lib"] },
  { header: "sys/stat.h", allowed_roots: ["lib"] }
]
```

The exact list should be finalized from Lambda's portability policy. The important design is that the policy is data, not code.

Why AST beats text search here:

- Ignores comments and strings.
- Distinguishes system includes from local includes.
- Can report normalized header names and source ranges.
- Can later trace transitive include exposure.

### 2. Banned C++ Standard Library Features

Lambda/Radiant runtime code should not use `std::string`, `std::vector`, `std::map`, or similar `std::` container/string types. Stage 1 should detect:

- Qualified identifiers in types: `std::string`, `std::vector<T>`, `std::map<K,V>`.
- Namespace aliases that resolve to `std`.
- `using std::string` and `using namespace std`.
- Constructor calls and variable declarations that instantiate banned types.

Suggested default policy:

```text
banned_cpp_symbols = [
  "std::string",
  "std::wstring",
  "std::u8string",
  "std::vector",
  "std::map",
  "std::unordered_map",
  "std::set",
  "std::unordered_set"
]
```

Rules should run against `lambda/` and `radiant/` first. Test code can be covered by a separate policy if needed.

### 3. Direct Allocation API Use

Runtime code should not call raw allocation APIs such as `malloc`, `calloc`, `realloc`, and `free` directly. The rule should detect `call_expr` nodes by resolved name where possible and textual callee fallback otherwise.

Allowed alternatives include Lambda's local allocation helpers, such as pool, arena, GC nursery, or tracked memory helpers.

The rule should support allowlists because `lib/` may intentionally implement wrappers. For example:

```text
raw_alloc_policy = {
  banned: ["malloc", "calloc", "realloc", "free"],
  allowed_roots: ["lib"],
  allowed_functions: ["mem_alloc", "mem_calloc", "mem_free"]
}
```

### 4. Lambda Item Type Guard Verification

This is the most semantic Stage 1 check. Lambda's `Item` is a tagged value, and casts/accessors should be justified by `type_id` facts. The analysis should infer local facts from guards and verify that Item projections are guarded.

#### Guard Facts

Recognize conditions such as:

```cpp
TypeId type_id = get_type_id(item);
if (type_id == LMD_TYPE_STRING) { ... }
if (get_type_id(item) == LMD_TYPE_ARRAY) { ... }
switch (get_type_id(item)) { case LMD_TYPE_MAP: ... }
if (IS_NUMERIC_ID(type_id)) { ... }
```

These produce facts:

```text
item has_type LMD_TYPE_STRING in then-branch
item has_type LMD_TYPE_ARRAY in then-branch
item has_type LMD_TYPE_MAP in switch case
item has_trait numeric in then-branch
```

#### Sensitive Accesses

Recognize operations that require a fact:

| Access pattern | Required fact |
|----------------|---------------|
| `((String*)...)` from an `Item` payload | `Item` is string or symbol, depending on helper contract |
| `((Array*)...)` | `Item` is array |
| `((Map*)...)` | `Item` is map |
| `((Element*)...)` | `Item` is element |
| `it2d(item)` | `Item` is numeric or helper explicitly handles all numeric cases |
| `get_int(item)` or equivalent | `Item` is int |
| direct container field access from Item-derived pointer | matching container type |

The first implementation should be conservative and explain unknowns. A finding should say which access was not proven safe and what guard would satisfy it.

#### Local Type Inference

Stage 1 should infer enough facts to avoid noisy findings:

- Variables assigned from `get_type_id(item)` are aliases for the item's type id.
- Variables initialized from constructors like `i2it(...)` have known Item result type.
- Branch conditions add facts to dominated blocks.
- `return` and `continue` split negative facts. Example: after `if (get_type_id(x) != LMD_TYPE_ARRAY) return;`, following code can assume `x` is an array.
- Helper functions can provide summaries. Example: a function named `is_string_item(Item x)` can be manually annotated as returning a type guard for `x`.

This check is a stepping stone toward deeper verification work described in `vibe/idea/Lambda_Semantics.md`, especially table consistency, dispatch structure extraction, and semantic equivalence testing.

## Result Model

Rules should return findings as data:

```text
Finding = {
  rule_id: string,
  severity: symbol,
  message: string,
  file: string,
  range: SourceRange,
  node: node_id,
  related: [FindingLocation],
  facts: Map,
  fix: CodeFix?
}
```

Output formats:

- Human-readable terminal report.
- JSON for regression tests.
- SARIF-like JSON for CI and editor tooling.
- Optional Lambda data output for downstream transformation.

## Test Strategy

### Unit Tests

- Parse small C/C++ snippets and verify normalized nodes.
- Verify source ranges and include extraction.
- Verify type declarator normalization.
- Verify guard fact extraction from `if`, `switch`, and early-return patterns.

### Golden Tests

Add fixtures under `test/code/fixtures/` and expected findings under `test/code/expected/`:

| Fixture | Expected result |
|---------|-----------------|
| `restricted_header.cpp` | finding for disallowed include outside `lib/` |
| `std_container.cpp` | findings for `std::string`, `std::vector`, `std::map` |
| `raw_alloc.cpp` | findings for `malloc`, `calloc`, `free` |
| `item_guard_good.cpp` | no findings |
| `item_guard_bad.cpp` | findings for unguarded Item access |
| `item_guard_switch.cpp` | no findings for guarded switch cases |
| `item_guard_early_return.cpp` | no finding after negative guard return |

### Repository Tests

Add make targets once the scanner is implemented:

```bash
make test-code-analysis          # run all code analysis fixture tests
make check-lambda-code-policy    # scan lambda/ and radiant/ with repo rules
```

Stage 1 should allow baseline files for existing violations. The baseline should be explicit and shrinking, not hidden in the scanner.

## Implementation Phases

### Phase 0: Design and Parser Wiring

- Add this proposal and finalize Stage 1 rule policy.
- Add Tree-sitter C and C++ parser dependencies.
- Add build config entries through `build_lambda_config.json`.
- Confirm parser generation and static library build on macOS and Linux.

### Phase 1: AST Extraction

- Implement `code ast` for C/C++ files.
- Build normalized nodes for files, includes, declarations, statements, expressions, and comments.
- Emit JSON or Lambda data for inspection.
- Add golden tests for node kinds and source ranges.

### Phase 2: Query Package and Simple Rules

- Implement Lambda helpers for node traversal and finding construction.
- Implement header boundary rule.
- Implement banned `std::` symbol rule.
- Implement raw allocation call rule.
- Add fixture tests and repository scan target.

### Phase 3: Shallow Semantics

- Add scopes, declarations, references, direct call edges, and shallow TypeRef inference.
- Add namespace and `using` handling needed for `std::` checks.
- Add function summaries for selected local helpers.
- Reduce false positives in repository scan.

### Phase 4: Item Type Guard Analysis

- Add local CFG for functions.
- Add type guard fact extraction.
- Add dominance-sensitive verification for Item casts/accessors.
- Add good/bad fixtures and repository baseline.

### Phase 5: Multi-Language Expansion

- Add JavaScript and Python adaptors over existing Tree-sitter parsers.
- Map common declarations, calls, imports, and member access into the same node model.
- Add language-neutral call and import queries.
- Start with structural rules, then add language-specific semantic overlays.

### Phase 6: Transform Support

- Add source-preserving edit planning.
- Add conflict detection for overlapping edits.
- Support simple codemods such as include replacement and API migration.
- Add preview/report mode before applying edits.

## Open Design Questions

- Should the persisted index be pure Lambda data, JSON, packed binary, or a custom mmap-friendly store?
- Should Stage 1 include `lib/` in normal policy scans or only as an allowlisted implementation root?
- Which C headers are truly restricted to `lib/`, and which are allowed in platform-specific runtime files?
- Should generated files like `lambda/parse.c` be excluded by default?
- Should C/C++ semantic precision later come from a Clang importer, compile commands, or continued Tree-sitter-based inference?
- How much of the rule library should be declarative data versus Lambda functions?

## Recommended Initial Deliverable

The first useful deliverable should be small but end-to-end:

1. Parse C/C++ files with Tree-sitter.
2. Emit normalized Lambda AST data with source ranges.
3. Run Lambda rules over the AST.
4. Report findings for the three structural checks: restricted headers, banned `std::` types, and raw allocation calls.
5. Add fixture tests and a repository scan command.

After that, add Item type guard analysis as the first real semantic rule. That sequence proves the framework is useful quickly, while preserving a path toward CodeQL/Joern-style semantic overlays and multi-language support.
