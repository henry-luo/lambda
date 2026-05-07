# Lambda Code Semantic Analysis Framework — Proposal

**Status**: Draft proposal (by Opus 4.7)
**Stage**: 1 (C/C++ analysis of Lambda/Radiant source)
**Author**: Lambda team

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

## 1. Motivation

Lambda and Radiant are large, evolving C/C++ codebases (~200K LOC) with a
number of project-specific invariants that are not enforced by the C/C++
type system or the compiler. Examples currently maintained only by
convention, code review, and ad-hoc grep:

- Certain headers must only be included from specific directories
  (e.g. `lib/` internals not used outside `lib/`).
- `std::string`, `std::vector`, `std::map`, and most other `std::`
  containers are forbidden in Lambda/Radiant code (use `Str`, `ArrayList`,
  `HashMap` from `lib/`).
- Raw `malloc` / `calloc` / `realloc` / `free` should not appear outside
  the memory subsystem; allocation goes through `pool_calloc()`,
  `arena_alloc()`, GC, etc.
- Lambda `Item` decoding must always be guarded by `get_type_id(Item)`
  before casting to a concrete container pointer.
- In `radiant/`, layout positions/dimensions must be `float`, not `int`
  (see `make check-int-cast`).
- `printf`/`fprintf`/`std::cout` are forbidden for diagnostics; use
  `log_*` macros.

Today these are checked by a mix of: (a) human review, (b) one-off
shell/Python scripts (e.g. `check-int-cast`), and (c) compiler warnings
when expressible. This is brittle, duplicative, and hard to extend.

We propose building a **general-purpose code semantic analysis framework
inside Lambda itself**. Lambda already has:

- A document data model (typed AST-like trees of `Element`, `Map`,
  `List`, scalars) ideal for representing source ASTs.
- A query / transformation language well-suited to writing analyzers.
- Tree-sitter integration (already used for the Lambda grammar).
- `MarkBuilder` / `MarkReader` infrastructure for constructing and
  walking such trees.

This makes Lambda a natural host for a CodeQL/Semgrep-style analyzer,
where rules are written as ordinary Lambda scripts.

## 2. Goals & Non-Goals

### Goals

- **G1**. Parse C and C++ source via Tree-sitter into a Lambda
  `Element`-tree AST.
- **G2**. Define a **language-neutral semantic AST schema** so the same
  rule shapes can later target JS, Python, etc.
- **G3**. Provide a small adapter layer (Tree-sitter CST → semantic AST)
  per language; rules consume only the semantic AST.
- **G4**. Express analyses as Lambda scripts using the existing query /
  pipe / pattern-matching syntax.
- **G5**. Run analyses from CLI (`./lambda.exe analyze ...`) and from
  `make` (CI integration).
- **G6**. Cover four pilot rules on Lambda/Radiant:
  1. Header inclusion policy (which directories may include which headers).
  2. Forbidden `std::` types in Lambda/Radiant.
  3. Forbidden raw allocators.
  4. Type-id guarded `Item` casts.

### Non-Goals (Stage 1)

- Full whole-program type inference / pointer analysis.
- Inter-procedural data-flow at CodeQL fidelity.
- Cross-translation-unit linking analysis.
- IDE integration (LSP). Future stage.
- Auto-fixing / code rewriting at scale (we will allow simple patches
  but not as a primary deliverable).

## 3. Prior Art

We surveyed the most influential semantic-analysis frameworks and
distilled what to adopt and what to avoid.

### 3.1 CodeQL (GitHub)

- **Model**: source code is extracted into a relational database;
  queries are written in QL, an object-oriented logic / Datalog-like
  query language.
- **Strengths**:
  - Powerful **predicate-based** querying with recursion and
    aggregation; excellent for taint and dataflow.
  - Rich, layered AST: lexical → syntactic → semantic → dataflow → taint.
  - Strong **library of "library" predicates** (`localTaintFlow`,
    `globalTaintFlow`, control-flow graphs).
  - Reusable across languages via a shared `dataflow` framework.
- **Weaknesses for us**:
  - Heavy: requires extractor, DB, full QL toolchain.
  - Closed-ish ecosystem; QL is non-trivial to embed.
  - Slow for incremental in-editor use.
- **Adopt**: layered AST (syntactic + semantic + flow); predicate-style
  reusable libraries; the `getEnclosing*` / `getParent*` accessor
  pattern; explicit `Location` objects on every node.

### 3.2 Semgrep

- **Model**: pattern-based, AST-level matching with metavariables;
  rules are YAML with code patterns.
- **Strengths**:
  - **Concrete-syntax patterns** (`malloc(...)`) — extremely low
    barrier to authoring rules.
  - Per-language frontends mapped onto a **generic AST** (`Generic_AST`).
  - Lightweight taint mode.
- **Weaknesses**:
  - Limited cross-function reasoning.
  - Pattern language is its own DSL; harder to compose than full code.
- **Adopt**: a **generic AST** layer above per-language ASTs;
  ergonomic concrete-syntax pattern matching as a Lambda macro/helper;
  per-rule metadata (id, severity, message).

### 3.3 Clang Static Analyzer / Clang-Tidy / Clang AST matchers

- **Model**: in-compiler AST + symbolic execution.
- **Strengths**: precise C/C++ semantics (real type checker behind it);
  AST matchers are a clean DSL (`hasType`, `callee`, `forEachDescendant`).
- **Weaknesses**: bound to LLVM; rules are C++; build complexity.
- **Adopt**: the **AST matcher combinator** style as inspiration for
  Lambda query helpers (e.g. `call_expr(callee = name("malloc"))`).

### 3.4 Tree-sitter + queries (`.scm`)

- **Model**: incremental GLR-ish parser; S-expression queries match
  CST patterns with predicates.
- **Strengths**: fast, incremental, robust to syntax errors;
  language-agnostic, many existing grammars.
- **Weaknesses**: **CST not AST** — too much syntactic noise; no
  semantic information (types, scopes, symbols); no cross-file resolution.
- **Adopt**: Tree-sitter as the **front-end parser** (Stage 1 input).
  We will *not* expose CST queries to rules — instead lift to a clean
  semantic AST.

### 3.5 ast-grep

- **Model**: Tree-sitter-based pattern matcher with rewriting; rules in
  YAML.
- **Strengths**: fast, easy install, structural search/replace.
- **Weaknesses**: still mostly syntactic; weak semantic layer.
- **Adopt**: rule packaging format (id, message, severity, fix), and
  the practical observation that "Tree-sitter + good ergonomics" is
  enough for most lint-grade rules.

### 3.6 Joern / CPG (Code Property Graph)

- **Model**: unified graph combining AST, CFG, PDG; queried via Scala
  / Cypher-like Gremlin traversals.
- **Strengths**: single graph for syntax + control + data flow;
  excellent for security analysis.
- **Weaknesses**: heavy infrastructure; learning curve.
- **Adopt**: the **CPG idea** — one node identity, multiple edge kinds
  (AST / CFG / DFG) — as the *eventual* shape of our semantic graph
  (Stage 2+). Stage 1 ships AST + simple symbol/scope edges only.

### 3.7 Roslyn (C#) and Eclipse JDT

- **Model**: full compiler exposed as an API; immutable tree with
  semantic model on top.
- **Adopt**: the split between **syntactic tree** (cheap, lossless,
  stable) and **semantic model** (resolved symbols, types) layered on
  top. Each can be queried independently.

### 3.8 RTL / Coccinelle / smatch (kernel)

- **Adopt**: project-specific lint rules expressed as small DSL
  programs are highly effective for codebase-specific invariants —
  exactly our use case.

### 3.9 Summary of design influences

| Source | Adopt |
|---|---|
| CodeQL | Layered AST → semantic → flow; reusable predicate libraries; uniform `Location`. |
| Semgrep | Generic cross-language AST; concrete-syntax patterns; YAML-style rule metadata. |
| Clang AST matchers | Combinator query API. |
| Tree-sitter | Front-end parser; many grammars for free. |
| ast-grep | Rule packaging, fix hints. |
| Joern/CPG | Unified node, multi-edge graph (Stage 2). |
| Roslyn | Syntactic vs semantic model split. |
| Coccinelle | Project-specific small DSLs. |

## 4. Architecture

```
                 ┌─────────────────────────────┐
  *.c / *.cpp →  │ Tree-sitter (C / C++)       │ → CST
                 └────────────┬────────────────┘
                              │   adapter (per language)
                              ▼
                 ┌─────────────────────────────┐
                 │ Generic Semantic AST        │ ← language-neutral
                 │  (Lambda Element tree)      │   schema
                 └────────────┬────────────────┘
                              │   resolvers
                              ▼
                 ┌─────────────────────────────┐
                 │ Symbol & Scope model        │ ← names, includes, types
                 └────────────┬────────────────┘
                              │   queries / transforms
                              ▼
                 ┌─────────────────────────────┐
                 │ Lambda rule scripts (*.ls)  │ → diagnostics (SARIF / text)
                 └─────────────────────────────┘
```

### 4.1 Front-end (Stage 1: C / C++)

- Use `tree-sitter-c` and `tree-sitter-cpp` (already vendored or easy
  to vendor under `lambda/tree-sitter-*`).
- One adapter per grammar in `lambda/analyze/adapter_c.cpp` /
  `adapter_cpp.cpp` that walks the CST and emits a generic AST via
  `MarkBuilder`.
- Adapters intentionally drop trivia (comments/whitespace are kept on
  attached nodes only when relevant) and normalize syntactic sugar
  (e.g. `T*` and `T *` become the same `pointer_type` node).

### 4.2 Generic Semantic AST schema

Inspired by CodeQL's `AstNode` hierarchy and Semgrep's `AST_generic`,
specialized to Lambda's element model. Every node is a Lambda element:

```
<node kind:'<kind>' lang:'c|cpp|js|py|...' loc:<location>
      ...attrs... ; children >
```

Common attributes (always present where applicable):

- `loc` — `<location file:'...' start:[line,col] end:[line,col] byte:[s,e] >`
- `id` — stable per-AST integer (for cross-references / edges).
- `kind` — abstract kind (see below).
- `lang` — source language tag.

Top-level kinds (language-neutral; per-language adapters fill them):

| Category | Kinds |
|---|---|
| Module/File | `translation_unit`, `module`, `import`, `include` |
| Declaration | `function_decl`, `var_decl`, `type_decl`, `field_decl`, `param_decl`, `using_decl`, `namespace_decl`, `class_decl`, `enum_decl` |
| Statement | `block`, `if`, `for`, `while`, `do_while`, `switch`, `case`, `return`, `break`, `continue`, `goto`, `label`, `expr_stmt`, `decl_stmt` |
| Expression | `call`, `member_access`, `index`, `name_ref`, `literal`, `binop`, `unop`, `assign`, `cast`, `ternary`, `lambda_expr`, `new`, `delete`, `sizeof` |
| Type expr | `name_type`, `pointer_type`, `reference_type`, `array_type`, `function_type`, `template_inst`, `qualified_type` |
| Pattern (for langs that have it) | `pattern_*` |
| Comment / Pragma | `comment`, `pragma`, `attribute` |

Language-specific extensions live under `lang_extra` child element so
generic rules can ignore them.

### 4.3 Symbol & Scope model

Stage 1 ships a **lexical** symbol model (no full type resolution):

- `scope` nodes attached to declarations that introduce one.
- `symbol` records keyed by `(scope_id, name)` with:
  - `kind` (function/var/type/macro/namespace/include)
  - `decl_loc`
  - `header` (for `#include` resolution)
- `name_ref` nodes carry a `resolved_to` id when the lexical resolver
  can determine it; otherwise `unresolved`.

Type information at Stage 1 is **syntactic**:
- Types are recorded as `name_type` strings ("`std::vector<int>`",
  "`Item`"), normalized but not unified to a canonical type.
- Sufficient for the four pilot rules.

Stage 2 will add a real semantic type model (typedef chase, template
instantiation, qualified-name resolution).

### 4.4 Rule API (Lambda)

Rules are ordinary Lambda scripts that import a small standard library
`analyze`:

```lambda
import 'analyze' as a

// Rule: forbidden std:: types in Lambda/Radiant
fn rule_no_std_containers(unit) {
  for n in a.descendants(unit, kind: 'name_type') {
    let name = n.name
    if name starts_with 'std::' and
       a.file_in(n, ['lambda/', 'radiant/']) and
       not a.file_in(n, ['lib/']) {
      a.report(
        id:       'lambda/no-std-containers',
        severity: 'error',
        loc:      n.loc,
        message:  "Use lib/ equivalents instead of " + name)
    }
  }
}
```

Provided helpers (initial set):

- `a.parse(file)` → translation unit
- `a.descendants(node, kind?: ..., where?: pred)`
- `a.ancestors(node)`
- `a.children(node)` / `a.parent(node)`
- `a.match(node, pattern)` — concrete-syntax pattern (Semgrep-style)
- `a.calls(node, callee_name)` — convenience for `kind:'call'`
- `a.file_in(node, prefixes)`
- `a.report(...)` — emit a diagnostic
- `a.symbol_of(name_ref)` — resolved symbol or null
- `a.includes_of(unit)` — list of `#include` edges

Concrete-syntax patterns (sugar over `descendants`):

```lambda
// matches malloc(...) calls
let mallocs = a.match(unit, "malloc($ARGS)")
```

Patterns are compiled once into matcher objects; metavariables
(`$NAME`, `$ARGS`, `$EXPR`) bind to subtrees.

### 4.5 Drivers & output

- CLI: `./lambda.exe analyze --rules rules/*.ls --files 'lambda/**/*.{c,cpp,h,hpp}'`
- Output formats: human-readable text (default), JSON, **SARIF 2.1**
  (for GitHub code scanning).
- `make analyze` target wired into CI; baseline allowlist for legacy
  violations so we can ratchet down.

## 5. Pilot Rules (Stage 1 deliverables)

Each pilot rule ships as `analyze/rules/<id>.ls` with a corresponding
test fixture pair (a `.cpp` input + an expected diagnostics `.txt`).

### R1. Header inclusion policy

- Spec: headers under `lib/internal/**` must only be `#include`d from
  files under `lib/**`. Generalizable to a config table:
  ```lambda
  let policy = [
    {dir: 'lib/internal/', allowed_from: ['lib/']},
    {dir: 'radiant/private/', allowed_from: ['radiant/']},
  ]
  ```
- Implementation: walk `include` nodes, resolve target path (relative
  to project root), apply policy.

### R2. Forbidden `std::` containers in Lambda/Radiant

- Spec: any `name_type` whose normalized name starts with `std::` and
  is in the configured deny-list (`std::string`, `std::vector`,
  `std::map`, …) is an error if the file lives under `lambda/` or
  `radiant/`. `lib/` and `test/` are exempt.
- Catches both declarations (`std::vector<int> v`) and explicit types
  in casts / template args.

### R3. Forbidden raw allocators

- Spec: `call` to any of `malloc`, `calloc`, `realloc`, `free` outside
  `lib/mempool*`, `lib/arena*`, and a small allow-list.
- Bonus: `new` / `delete` similarly restricted in chosen subtrees.

### R4. Type-id guarded `Item` casts

- Spec: a cast/conversion of an `Item` to a concrete container pointer
  (e.g. `(List*)`, `(Map*)`, `(Element*)`) must be **dominated** by a
  check on `get_type_id(item)` for the matching `LMD_TYPE_*` constant.
- Implementation: simple intra-procedural check using AST positions
  and an `if`/`switch` ancestor walk:
  - From the cast site, walk up until we find an `if` or `switch`
    whose condition references `get_type_id(<same expr>)` compared
    against the expected constant; or a preceding `assert(get_type_id(...) == ...)`.
  - This is deliberately conservative; false positives go on a
    per-file baseline.
- Stage 2 will replace this with proper control-flow domination.

## 6. Implementation Plan

### Phase 1.0 — Skeleton (small)
- Add `lambda/analyze/` directory with `adapter_c.cpp`,
  `adapter_cpp.cpp`, `analyze_main.cpp`.
- Vendor `tree-sitter-c` and `tree-sitter-cpp` under
  `lambda/tree-sitter-c` / `-cpp` (or via `build_lambda_config.json`).
- Wire `./lambda.exe analyze` subcommand.

### Phase 1.1 — Generic AST schema
- Define schema in `doc/dev/Code_AST_Schema.md`.
- Implement adapter for C; emit AST as Lambda elements via
  `MarkBuilder`.
- Snapshot tests: `test/analyze/c/*.c` ↔ `*.ast.txt`.

### Phase 1.2 — Rule library + 4 pilot rules
- `analyze/lib/analyze.ls` exporting helpers from §4.4.
- Implement R1–R4. Each with input/expected fixtures.

### Phase 1.3 — C++ adapter
- Re-use C adapter where grammar overlaps; add C++-only constructs.
- Re-run R2 / R3 / R4 across full Lambda/Radiant tree; produce baseline.

### Phase 1.4 — CI integration
- `make analyze` target; SARIF output.
- Ratcheting baseline file under `test/analyze/baseline.json`.

### Phase 1.5 — Documentation
- `doc/dev/Code_Analysis_Guide.md`: how to write a rule.
- Rule reference page generated from rule metadata.

### Stage 2 (out of scope for this proposal, sketched)
- Real semantic type model (typedef chase, template instantiation).
- Cross-TU symbol resolution via `compile_commands.json`.
- Control-flow & simple data-flow graphs (CPG-style edges).
- Additional language adapters: JS (reuse Lambda's JS infra), Python.
- Taint-flow library.
- LSP server for in-editor diagnostics.

## 7. Risks & Mitigations

- **R: Tree-sitter C++ grammar gaps on heavy template code.**
  M: adapter degrades gracefully — emit `unknown` nodes; rules skip
  unknown subtrees; record stats.
- **R: AST schema churn breaks rules.**
  M: schema is versioned (`schema_version` on `translation_unit`);
  helpers in `analyze.ls` are the public API, not raw kinds.
- **R: False positives on type-id rule (R4) without real CFG.**
  M: ship as **warning** initially; baseline existing code; promote
  to **error** after Stage 2 CFG lands.
- **R: Performance on full repo.**
  M: per-file parallelism (one file = one Tree-sitter parse + one rule
  pass); cache parsed ASTs keyed by file mtime+hash.

## 8. Success Criteria

- All four pilot rules run on the full Lambda/Radiant tree in CI in
  under 60 s on a developer laptop.
- Zero new violations allowed on PRs touching files under `lambda/`
  and `radiant/`.
- A new rule can be authored and wired into CI in **< 50 lines of
  Lambda** including tests.
- Rule output is SARIF-clean and renders in GitHub code scanning.

## 9. Open Questions

1. Do we vendor `tree-sitter-c` / `-cpp` directly, or fetch via the
   existing `setup-*-deps.sh` flow?
2. Should the rule API be a Lambda module (`import 'analyze'`) or a
   set of system functions under `sys.analyze.*`? (Leaning: module.)
3. SARIF version: 2.1.0 only, or also produce a simple JSON for
   internal tooling? (Leaning: both, SARIF as primary.)
4. How aggressively do we normalize C++ qualified names in Stage 1
   (e.g. `::std::vector` vs `std::vector`)? (Leaning: aggressive
   normalization; rules see canonical form.)
