# JavaScript and TypeScript Grammar Parser: First-Party C Proposal

- **Date:** 2026-08-28
- **Status:** **PROPOSED, NOT RATIFIED, NOT IMPLEMENTED.** This document changes no production parser. The Tree-sitter JavaScript production parser and TypeScript fallback/reference parser remain in place until every cutover gate in §12 passes.
- **Scope:** replace the production JavaScript and TypeScript Tree-sitter front ends with one small first-party C lexer plus hybrid recursive-descent/Pratt parser that reduces directly into the retained `JsAstNode` graph.
- **Formal linkage:** **D1.1** (a guest is grammar + AST builder + `LangProfile`), **D1.3** (guests reuse contracts below their semantic boundary), **D4.1.1v2/D4.1.4v4** (AST-pool lifetime and the closed allocator-mechanism set), **D8.1.3v9** (retained `JsScript`/JavaScript AST), **D8.2.1–D8.2.5** (one core-node catalog, no tree rewriting, indexed compilation unit, typed pass schedule), and **D8.6.4v2** (release timing methodology). A first-party JS/TS production-parser selection is not yet a formal ruling; ratification requires a new or revised **D8.1** ruling before cutover.
- **Related:** `vibe/Lambda_Grammar_Parser.md`, `vibe/jube/TS_Grammar_Reduce2.md`, `doc/dev/js/JS_02_Parsing_AST.md`, `doc/dev/js/JS_16_Testing.md`, `lambda/runtime/parser/`, `lambda/js/build_js_ast.cpp`, `lambda/js/js_scope.cpp`, `lambda/ts/`, and the checked-in reference grammars under `lambda/tree-sitter-{javascript,typescript}/`.

## 1. Proposal

Replace the two generated Tree-sitter grammar parsers in LambdaJS with one first-party C parsing front end:

1. a shared source/cursor/diagnostic substrate extracted from the proven Lambda C parser;
2. one JavaScript/TypeScript streaming lexer with explicit lexical goals;
3. recursive descent for programs, declarations, statements, patterns, classes, modules, and delimited forms;
4. one Pratt loop for JavaScript expressions;
5. a TypeScript extension layer, including one Pratt loop for type expressions; and
6. a direct `JsAstNode` sink that reuses the current JS/TS AST, scope, type, module, and early-error helpers.

The production target is:

```text
JS or TS source
  -> shared C source cursor
  -> JS/TS C lexer
  -> recursive descent + Pratt parser
  -> direct JsAstNode construction
  -> existing bind/index/early-error/type passes
  -> existing AST interpreter or MIR lowering
```

No concrete syntax tree, replacement syntax tree, or token tree survives parsing. Parser checkpoints are bounded cursor snapshots, not nodes. A failed parse publishes no executable AST.

The migration follows the decision-gated shape used by `vibe/Lambda_Grammar_Parser.md`:

- **POC gate:** prove complete admitted-syntax coverage, smaller linked parser size, and faster release parsing before changing production.
- **Direct-AST gate:** make the current Tree-sitter builder's constructors parser-neutral, build the same retained AST directly, and prove structural and behavioral equivalence.
- **Cutover gate:** switch the normal JS and TS entry points only after the focused suites and the complete `test262-baseline` pass with zero failures and zero retries.

A failed correctness, size, or speed gate stops the migration. It does not authorize syntax removal, corpus-specific branches, hard-coded fixtures, Tree-sitter-result caching, or a fallback that silently reparses ordinary production input.

## 2. Current architecture and measured baseline

### 2.1 JavaScript path

The normal JavaScript front end is currently:

```text
source
  -> js_normalize_source_for_parser()
  -> ts_parser_parse_string(tree_sitter_javascript)
  -> TSTree / TSNode
  -> build_js_ast.cpp
  -> retained JsAstNode graph
  -> js_check_early_errors()
  -> AST interpreter or MIR lowering
```

`js_transpiler_create()` owns `TSParser*` and `TSTree*`. `js_transpiler_parse()` performs a constant-length normalization pass for selected Unicode whitespace, HTML comments, soft `await`/`yield`, NUL, and tagged-template cases before Tree-sitter sees the source. `build_js_ast.cpp` then makes hundreds of `TSNode` queries and interleaves CST traversal with AST allocation, scope predeclaration, pattern binding, directive detection, module facts, and TS-specific construction.

The retained `JsAstNode` graph is already parser-neutral: `AstNode` stores `SourceSpan`, not `TSNode`. This makes the JS migration narrower than the original Lambda parser migration. The main seam to extract is CST traversal versus semantic construction, not retained-AST source ownership.

### 2.2 TypeScript path

The normal TypeScript path is not simply “Tree-sitter TypeScript to AST.” `transpile_ts_to_mir()` first calls `ts_preprocess_source()`. On its normal successful return, the preprocessor rewrites/erases TypeScript syntax into JavaScript and the result goes through the JavaScript Tree-sitter parser with the TypeScript profile bit retained. The TypeScript Tree-sitter parser plus unified JS/TS AST builder is the fallback path when preprocessing does not return a buffer.

Therefore P0 must freeze two separate compatibility oracles:

- the **shipped TS behavior oracle**: `ts_preprocess_source()` followed by the JavaScript parser/builder; and
- the **structural TS reference oracle**: `tree_sitter_typescript()` followed by the unified builder/type-lowering path.

The first C cutover preserves shipped behavior. It may use the existing AST-level enum, namespace, decorator, constructor-property, type-resolution, and type-only stripping helpers, but it may not silently expand or narrow the admitted TypeScript language. A future TypeScript-semantics expansion is a separate proposal.

The final C path retires the production source-rewrite preprocessor. Leaving `ts_preprocess_source()` in front of the new parser would retain a second lexer/parser-shaped flow and violate the single-flow goal in §6.

### 2.3 Current parser artifacts

The following snapshot was measured from the active worktree on 2026-08-28. The object/archive files are existing local build artifacts, so P0 must rebuild both old and new variants with identical pinned release/size flags before treating them as gate evidence.

| Artifact | JavaScript | TypeScript | Combined |
|---|---:|---:|---:|
| generated `src/parser.c` | 2,916,280 B | 4,336,929 B | 7,253,209 B |
| `src/parser.o` | 476,000 B | 712,272 B | 1,188,272 B |
| `src/scanner.o` | 6,632 B | 18,656 B | 25,288 B |
| grammar archive | 483,200 B | 731,496 B | 1,214,696 B |
| states / large states | 1,847 / 475 | 2,551 / 694 | — |
| symbols / external tokens | 263 / 8 | 320 / 15 | — |

The shared Tree-sitter runtime archive is 246,728 B in this snapshot, but it is not part of the JS/TS-only saving while other built-in languages still use Tree-sitter. Size reporting must distinguish:

- removal of the two JS/TS grammar archives;
- the size of the new shared C parser archive;
- the net stripped `lambda.exe` change from an A/B link; and
- any later, separately justified removal of the shared Tree-sitter runtime.

Source line count is a maintainability diagnostic, not a binary-size result.

## 3. Goals and non-goals

### 3.1 Goals

- **Admitted-language parity.** Preserve the exact syntax and early-error behavior the current JS and TS production pipelines admit. Reference grammars alone are not enough when the current AST builder rejects or reinterprets a Tree-sitter-valid CST.
- **Alignment with Lambda parsing.** Use the same source-span, bounded-lookahead, RD/Pratt, reduction-sink, fail-closed, reference-oracle, and release-measurement architecture as the Lambda C parser under **D8.1.1v5**, without importing Lambda-language semantics into JavaScript.
- **Maximum safe reuse.** Promote or generalize the Lambda parser's language-neutral mechanics and reuse current JS/TS AST constructors and semantic passes. **D1.3** requires reuse below the semantic boundary, not reuse of Lambda token or grammar accidents.
- **One JS/TS parser flow.** JavaScript is the base grammar; TypeScript is a mode plus extensions. There is no second copy of statement, declaration, class, function, pattern, list, or expression parsing.
- **Minimum retained parser code.** Common parsing flows have one owner function/table. The third near-identical case triggers extraction before it lands.
- **Direct retained AST.** The parser constructs the existing `JsAstNode`/core `AstNode` graph and preserves every downstream-observable node, span, flag, scope, binding, module fact, and type payload required by **D8.2.1–D8.2.5**.
- **Materially smaller parser.** The new combined JS/TS lexer/parser archive is at least 50% smaller than the rebuilt combined Tree-sitter JS/TS grammar-archive baseline.
- **Faster parsing.** The release C parser is measurably faster both as a recognizer and across the real source-to-validated-AST front end, using the gate in §12.3.
- **Full regression safety.** `test_js_gtest`, `test_ts_gtest`, and `test262-baseline` are fully green; Test262 has zero failures, crashes, timeouts, partial results, non-fully-passing rows, and retries.

### 3.2 Non-goals

- No JavaScript or TypeScript language feature is added, removed, or “simplified” to make the parser smaller.
- No JavaScript truthiness, coercion, object, scope, exception, or module semantics are taken from Lambda; that would violate **D1.3**.
- No new AST hierarchy, bytecode, syntax tree, or alternate compiler pass schedule is introduced.
- No work is added to the frozen C2MIR path under **D1.6**.
- No vendored Tree-sitter runtime or JavaScript/TypeScript grammar source is patched. The checked-in grammars remain read-only reference/editor artifacts. Generated `parser.c` is never hand-edited.
- No conservative-stack GC path, fifth allocation mechanism, `std::` container/string type, or debug-only performance result is admitted.
- Incremental editor parsing is not reimplemented in the C runtime parser. Tree-sitter remains available to editors and explicit reference tools.

## 4. Compatibility authority and frozen corpus

Before implementation, P0 records the current behavior of every source in these cohorts:

1. all positive and negative fixtures driven by `test_js_gtest`;
2. all `test/ts/*.ts` fixtures driven by `test_ts_gtest`;
3. every entry in `test/js262/test262_baseline.txt`;
4. Test262 parse-negative and early-error tests, including declared error type/phase;
5. JS sources embedded in Node, module, eval/`Function`, preamble, document, and Radiant paths;
6. targeted syntax fixtures for ASI, regex/division, templates, classes, patterns, modules, contextual keywords, and TS extensions; and
7. rejected sources generated by delimiter/operator/token mutation and fuzzing.

For each source, the frozen manifest records:

- language mode: script, module, eval, function body, or TypeScript;
- current parse/build/early-error status;
- syntax diagnostic class and byte span when rejected;
- canonical AST fingerprint when accepted;
- execution output or expected failure where applicable; and
- reference parser and build hash.

Tree-sitter acceptance by itself is only a syntax oracle. Production parity means the complete existing front end: parse, AST build, scope/module facts, and early errors. This proposal explicitly avoids the limitation of the current Lambda `compare` mode, which checks syntax acceptance but does not yet claim AST equality.

The vendored grammars remain useful as reference grammars, but they are not modified during this project. Any discovered defect that genuinely belongs upstream stops that slice for approval and the repository's auditable patch process.

## 5. Target architecture

```text
                         +---------------- reference-only ----------------+
                         | Tree-sitter JS/TS -> CST adapter -> canonical  |
                         | AST/diagnostic snapshot                         |
                         +--------------------------+-----------------------+
                                                    |
source -> ParseSource/ParseCursor -> JS/TS C lexer -> RD + Pratt parser
                                                    |
                                                    v
                                             direct AST sink
                                                    |
                  +---------------------------------+----------------------+
                  |                                 |                      |
                  v                                 v                      v
          JsAstNode AST pool              scope/binding/index       early errors/types
                  |                                 |                      |
                  +---------------------------------+----------------------+
                                                    |
                                      AST interpreter or MIR lowering
```

There is one parser core with two sinks during migration:

- a **syntax sink** that counts reductions and computes a stable structural fingerprint without retaining a tree; and
- a **direct AST sink** that immediately maps committed productions to existing AST constructors.

The sink ABI carries committed syntax facts—form, source span, operator/token, flags, and child values—so the sink never reparses a source span to discover what the parser already knew. Speculative probes never call the sink.

The AST sink may return `JsAstNode*`-backed opaque values. Short-lived descriptors for property/method/parameter/list assembly live only until their parent reduction and use existing pool/tail-rewind ownership under **D4.1.1v2/D4.1.4v4**. No partial AST is published after an error.

### 5.1 Proposed source boundary

The exact filenames are an implementation choice, but ownership should remain this narrow:

```text
lambda/runtime/parser/
  source_parser.h/.c       common cursor, checkpoint, report, reduction mechanics

lambda/js/parser/
  js_parser.h              C ABI, modes, tokens, status, metrics
  js_lexer.c               one JS/TS lexer with lexical goals
  js_parser.c              one program/RD/JS-Pratt core plus TS hooks
  js_ast_sink.cpp          parser-neutral construction into JsAstNode
  js_parser_compare.cpp    test/reference serializer and differential adapter
```

The parser core is C. The AST sink is C++17 only where the retained AST APIs require it and uses project `lib/` containers/types, never `std::` containers/types. The existing Tree-sitter directories are not part of this implementation boundary and remain untouched.

## 6. Reuse and code-minimization plan

### 6.1 Reuse from the Lambda C parser

The Lambda parser currently keeps many useful mechanics `static` inside `lambda_lexer.c` and `lambda_parser.c`. Rule 13 forbids copying those helpers into the JS parser. Before new JS parser code lands, promote/generalize the reusable shape into a C-compatible common module under `lambda/runtime/parser/` and make Lambda its first client.

| Common contract | Lambda source shape to extract | JS/TS use |
|---|---|---|
| source bounds and byte spans | checked `lexer_has`, peek, advance, `SourceSpan` construction | safe UTF-8 byte cursor and token spans |
| cursor checkpoints | `parser_probe` / `parser_prepare_probe` | arrows, async forms, patterns, TS generic/type ambiguity |
| bounded lookahead | current/next token discipline | parser-driven lexical goals without general backtracking |
| recursion/progress guards | `parser_enter` / `parser_leave`, recovery progress | malformed nesting and fuzz safety |
| structured status/report | `LambdaParseStatus`, expected-token bits, bounded reports | JS `OK` / `INCOMPLETE` / `ERROR` and exact spans |
| reduction sink/fingerprint | `parser_reduce_detail_ex`, structural hash | syntax POC and direct AST sink |
| delimited/list driver | expression-list and braced helpers | arguments, parameters, arrays, objects, imports/exports, TS members |
| operator metadata shape | table-driven binding powers | separate JS-expression and TS-type Pratt tables |
| source point conversion | `lambda_source_span_start_point()` | parser-neutral line/column diagnostics |

The common layer owns mechanics only. It does **not** own:

- Lambda token kinds or keyword tables;
- Lambda identifier semantics from `parse_lex.hpp`;
- Lambda statement termination, element/path/type grammar, or AST builders; or
- JavaScript lexical goals, ASI, reserved words, patterns, early errors, or semantic constructors.

This boundary is the concrete meaning of **D1.3** for parser reuse: share cursor, bounds, checkpoint, reduction, and report contracts; keep language semantics profile-owned.

### 6.2 Reuse from the current JS/TS front end

The following behavior is extracted from `build_js_ast.cpp` rather than rewritten in the C parser:

- AST allocation by `SourceSpan` (`alloc_js_ast_node_span()` replaces the current `TSNode`-taking allocator);
- operator spelling to `JsOperator` mapping, changed to fail closed on an unknown operator rather than falling back to addition;
- identifier and private-name escape decoding;
- string, numeric, BigInt, regex, and template cooking;
- property/method/function payload construction;
- pattern binding and declaration predeclaration;
- scope creation/definition/lookup;
- import/export fact recording;
- TS type-node construction and AST-level enum/namespace/decorator/parameter-property lowering; and
- `js_check_early_errors()`, AST indexing, interpreter admission, and MIR lowering.

Each extracted constructor accepts parser-neutral spans, tokens, flags, and child nodes. The Tree-sitter adapter and C sink call the same constructor until reference-path removal. No 5,000-line second builder is created.

### 6.3 One owner for every common flow

The parser has these mandatory single-owner functions/tables:

| Flow | Single owner |
|---|---|
| comma/closer lists | `parse_delimited_list()` with item callback and policy flags |
| blocks and statement lists | `parse_statement_list(terminator, context)` |
| binding/assignment patterns | `parse_pattern(context)` |
| formal parameters and arguments | shared delimited driver plus distinct item callbacks |
| function/method/arrow signatures | `parse_callable_head(kind, flags)` |
| class/object members | one property-name/modifier reader plus semantic mode callbacks |
| import/export specifiers | one named-specifier list parser |
| prefix/postfix/infix expressions | one JS Pratt driver and one operator table |
| TS type expressions | one TS-type Pratt driver and one operator table |
| ASI | `consume_semicolon(policy)` only |
| recovery | context stack plus one synchronization engine |

JavaScript and TypeScript entry points call the same program/statement/expression functions with a `JsParseMode` and small extension hooks. They do not fork into `parse_js_program()` and a copied `parse_ts_program()`.

Any third near-identical type/kind/case block must be extracted before the slice is accepted. Static helpers needed by a second file are promoted to a module header; they are never copied.

## 7. Lexer design

### 7.1 Parser-driven lexical goals

JavaScript cannot use a context-free “longest token wins” lexer. `/` may begin division, `/=`, or a regular-expression literal; template text switches modes around `${...}`; `}` can close a block, object, template substitution, or TS type member; and line terminators affect restricted productions.

The parser passes an explicit lexical goal to the lexer:

```c
typedef enum JsLexGoal {
    JS_LEX_DIV,
    JS_LEX_REGEXP,
    JS_LEX_TEMPLATE,
    JS_LEX_TEMPLATE_TAIL,
    JS_LEX_JSX_TEXT,
    JS_LEX_TS_TYPE,
} JsLexGoal;
```

The exact enum may change, but the invariant does not: regex/division and template decisions are owned by committed parser context, not by a heuristic source-normalization pass. The existing Tree-sitter external scanners and current normalization behavior are differential oracles.

### 7.2 Tokens and source fidelity

Every token carries:

- kind and original byte span;
- `line_terminator_before`;
- escaped-identifier/reserved-word classification;
- numeric/string/template/regex decode flags needed by the AST constructor; and
- lexical-goal/mode facts only when a later parser decision needs them.

The lexer does not rewrite the source. Original source spans remain stable for diagnostics, `Function.prototype.toString`, module records, AST dumps, and MIR source attribution.

Unicode identifiers and escaped keywords use JavaScript's rules, not the Lambda identifier helper. Common UTF-8 bounds/progress code is shared; JS classification remains JS-owned under **D1.3**. Invalid UTF-8, Unicode escapes, numeric separators, legacy octal forms, string escapes, regex flags, and template escapes receive targeted positive and negative tests.

### 7.3 Automatic semicolon insertion

Line terminators are trivia plus a token flag, not ordinary statement tokens. `consume_semicolon(policy)` is the only ASI authority. Its policy covers:

- explicit `;`;
- `}` and EOF insertion;
- restricted line breaks after `return`, `throw`, `break`, `continue`, `yield`, and `async` forms;
- postfix `++`/`--` line-break restrictions; and
- statement forms that cannot continue with the next token.

No statement parser implements its own ASI variation. ASI decisions are recorded in differential diagnostics because many Test262 parse-negative tests depend on them.

### 7.4 Bounded state and ownership

The lexer is streaming with bounded lookahead. Checkpoints copy byte offset, line/column, lexical goal, template/brace depth, and current token state. They allocate no AST nodes and emit no diagnostics until commit.

Token text is a borrowed slice of the synchronously owned source. Cooked names/literals are allocated only by the AST sink in the existing AST/name pools. This preserves **D4.1.1v2** and adds no allocator mechanism under **D4.1.4v4**.

## 8. Parser design

### 8.1 Recursive-descent ownership

Recursive descent owns constructs with decisive introducers or delimiters:

| Area | Forms |
|---|---|
| unit | scripts, modules, hashbang, directive prologue |
| declarations | function, class, `var`/`let`/`const`, import/export, TS declarations |
| statements | block, expression, `if`, switch, loops, try/catch/finally, return/throw, labels, with, debugger |
| patterns | identifiers, array/object patterns, rest, defaults, binding versus assignment context |
| callable/class | parameters, methods, accessors, fields, static blocks, decorators/modifiers |
| literals/containers | arrays, objects, templates, regex, optional JSX if admitted by the frozen production corpus |
| TS extensions | annotations, assertions, `as`, `satisfies`, non-null, interfaces, aliases, enums, namespaces, modifiers |

Context is an enum/bitset passed to shared functions. It does not select a second large flow.

### 8.2 JavaScript Pratt expressions

`parse_expression(min_bp, context)` owns the expression precedence ladder. A table maps token/form to left/right binding power, associativity, `JsOperator`, and context restrictions.

The prefix side handles literals, identifiers, `this`, `super`, arrays, objects, functions/arrows, classes, `new`, `import`, `yield`, `await`, unary/update forms, and grouping/pattern probes. The postfix/infix loop handles calls, members, subscripts, optional chaining, tagged templates, postfix updates, exponentiation, arithmetic, shifts, relations, equality, bitwise/logical/nullish operators, conditional, assignment, and sequence.

Special precedence restrictions—such as unary expression on the left of exponentiation, nullish mixing with `&&`/`||`, optional-chain restrictions under `new`, and assignment-target validity—are table flags or focused validators. They do not fork the Pratt loop.

### 8.3 Patterns and arrow ambiguity

Array/object syntax is shared structurally but interpreted by context as expression, binding pattern, or assignment pattern. `parse_pattern(context)` is one recursive flow used by declarations, parameters, catch, `for` heads, assignments, and TS parameters.

Parenthesized expressions versus arrow heads use a side-effect-free checkpoint. The probe parses only the callable-head grammar and commits only when `=>` is present. It does not allocate nodes, define names, or emit a diagnostic before commitment.

The same checkpoint rule governs contextual `async`, TS type parameters, and generic-call/type-assertion ambiguity. General backtracking and GLR are out of scope.

### 8.4 TypeScript as extensions, not a second parser

`JsParseMode` selects JavaScript script/module or TypeScript. TS mode enables focused hooks in the shared flows:

- modifiers/decorators around declarations, class members, and parameters;
- optional/type annotations after binding names and parameters;
- type parameters and type arguments;
- TS-only declarations and module forms;
- `as`, `satisfies`, non-null, assertion, and instantiation expressions; and
- TS property-parameter and field rules.

Type syntax is parsed by `parse_ts_type(min_bp, terminators)`, a separate Pratt table because its operators and ambiguity rules are not JavaScript expression semantics. It returns existing `TsTypeNode*`-compatible sink values. It is called only from committed TS type slots; it is not a second program/statement parser.

The production sink reproduces the current TS lowering behavior at AST level. It does not preserve the old source-rewrite pipeline merely to reuse its output. During comparison, the preprocessor path remains an explicit reference selector until parity is proved.

### 8.5 Strict mode, scopes, and early errors

Parsing recognizes directive prologues and retains the exact raw/cooked facts needed for strict-mode selection. Static semantic rules stay in `js_check_early_errors()` unless a rule is inseparable from parsing. Moving an existing early error into the parser requires a differential test proving identical error type, phase, and location.

The current CST builder pre-scans a scope's CST to predeclare bindings before building identifiers. The direct path replaces that dependency with a parser-neutral AST/fact pass:

1. build the complete structural subtree directly;
2. collect declaration and scope facts from the retained AST through the common child-enumeration contract;
3. bind identifiers and compute strict/TDZ/capture/module facts; and
4. run the existing early-error and indexing schedule.

This moves toward the single indexed compilation-unit/pass contract of **D8.2.4/D8.2.5**. It must not create a JS-private alternate pass schedule or change lowering semantics.

## 9. Direct AST construction

### 9.1 Constructor extraction

Before the direct sink is implemented, split `build_js_ast.cpp` into:

- a temporary Tree-sitter CST adapter;
- parser-neutral JS AST constructors/decoders;
- parser-neutral TS AST/type/lowering constructors; and
- existing scope/module/validation services.

Extraction precedes new construction. A constructor is shared by both adapters before the C sink may call it. This proves the seam and prevents two permanent builders.

### 9.2 Reduction contract

A committed reduction carries all syntax facts its constructor needs:

```c
typedef struct JsParseReduction {
    JsReductionKind kind;
    JsReductionForm form;
    SourceSpan span;
    JsToken introducer;
    JsToken secondary;
    uint32_t flags;
    const JsParseValue* children;
    uint32_t child_count;
} JsParseReduction;
```

Names, operators, property kinds, declaration kinds, method flags, optional/computed/static/async/generator flags, and TS annotations are explicit. The sink never scans the reduction span to rediscover them.

Lists use one builder that keeps head and tail; repeated tail walks are forbidden. Abandoned speculative work never reaches the sink. An unsupported committed reduction fails closed and is counted; it never falls back after partial AST construction.

### 9.3 Structural parity

The direct AST must preserve:

- every node kind, child/sibling order, and source span;
- operators, literal payloads, raw/cooked template text, regex pattern/flags, and identifier spelling;
- JS form flags, strict/directive facts, patterns, private names, and class home relationships;
- scope kinds, declarations, use/def identity, captures, TDZ and hoisting facts;
- imports, exports, module kind, and interpreter module plans;
- TypeScript type nodes, type registry facts, and AST-level lowered forms where the shipped path observes them; and
- the shared AST index and `LangProfile` ownership required by **D8.2.1–D8.2.5**.

The canonical comparator is pointer-free. It identifies scopes, bindings, functions, classes, and names by stable paths/bytes rather than addresses. The existing partial debug printer is not sufficient evidence.

## 10. Diagnostics, recovery, and selectors

### 10.1 Diagnostics

The C parser reports:

- `OK`, `INCOMPLETE`, or `ERROR`;
- actual token and exact original-source byte span;
- bounded expected-token bits;
- stable syntax code and message;
- opener span for unclosed delimiters/templates/comments; and
- line/column derived from the common source-span helper.

The ordinary compiler pass is fail-fast and publishes no AST after syntax failure. A separate syntax-only recovery pass may collect multiple diagnostics with a null sink. It must guarantee forward progress and may synchronize only at context-owned boundaries. It never executes or validates a partial AST.

Negative Test262 tests must continue to report the declared parse/early phase and error class. `test_js_test262_gtest` is never changed to mask a parser discrepancy.

### 10.2 Differential selectors

During migration, test/debug builds provide one selector for both languages:

```text
JS_PARSER=tree       # existing JS or TS reference pipeline
JS_PARSER=c          # first-party C parser and direct AST
JS_PARSER=compare    # both, with explicit comparison level
```

The exact name may change to fit existing CLI conventions. Required behavior does not:

- POC compare checks full-source acceptance/status and diagnostic class/span.
- Direct-AST compare checks the canonical AST/fact serialization as well.
- The C result is never replaced with the Tree-sitter result on disagreement.
- A mismatch is a test failure with a minimized source artifact under `./temp/`.
- Normal production has no environment lookup on the hot path after cutover; selector choice is established once per compilation unit.

## 11. Migration plan

### P0 — Freeze baselines and measurement harness

- Record compiler, flags, platform, source hashes, grammar hashes, artifact sizes, link maps, corpus manifests, and current results.
- Rebuild the two grammar archives under identical release/size flags.
- Add a release parser benchmark that preloads all source bytes and excludes file IO, execution, process startup, cleanup, logging, and scheduler time.
- Add canonical AST/fact serialization for the current Tree-sitter path.
- Add differential manifests for JS, shipped TS preprocessing, and structural TS reference parsing.

No parser implementation begins until the oracle can distinguish parse rejection, AST-build rejection, early error, execution failure, retry, and timeout.

### P1 — Extract and validate common parser mechanics

- Promote language-neutral cursor/checkpoint/report/reduction/list/progress helpers from `lambda/runtime/parser/` into one shared C module.
- Make the Lambda C parser consume that module without behavior change.
- Run the focused Lambda parser suite and Lambda baseline before using it from JS.
- Extract parser-neutral source-span constructors and literal/name/operator helpers from the JS CST builder.

This phase proves reuse rather than copying.

### P2 — Complete JS/TS C recognizer POC

- Implement the shared lexer, JS recursive-descent/Pratt parser, and TS hooks/type Pratt parser with the syntax sink only.
- Cover every frozen positive source and every frozen rejection.
- Differentially compare both directions; C-only acceptance is a failure unless the source is explicitly classified as a pre-existing reference-parser defect and separately approved.
- Fuzz lexical modes, delimiters, ASI, patterns, and TS ambiguity with progress/recursion assertions.
- Measure size and recognizer speed using §12.

Failure of the size or speed gate stops before AST integration.

### P3 — Extract constructors and implement direct AST sink

- Separate CST traversal from JS/TS construction.
- Add span/token/children constructors shared by the Tree-sitter adapter and C sink.
- Build scopes, bindings, module facts, indexes, TS facts, and early errors through the authoritative post-build schedule.
- Add canonical AST/fact comparison and focused execution parity.
- Keep both parser paths available only in test/debug/reference profiles.

### P4 — Production comparison campaign

- Run all JS and TS focused suites in `tree`, `c`, and `compare` modes.
- Run the complete Test262 baseline repeatedly in release mode and require zero retries.
- Run module, eval, `Function`, preamble, document/Radiant, batch-reset, and crash-recovery paths.
- Attribute every parser/front-end timing and binary-size delta.
- Minimize every mismatch under `./temp/`; never add corpus-specific recognition.

### P5 — Cutover and build isolation

Only after §12 passes:

- make the C parser the default for normal JS and TS compilation;
- remove `TSParser*`/`TSTree*` from `JsTranspiler` production state;
- retire `ts_preprocess_source()` from the production TypeScript path;
- remove JS/TS Tree-sitter grammar archives and headers from normal `build_lambda_config.json` profiles, then regenerate build files with `make`;
- retain the vendored grammars and generated sources unchanged for editor/reference tooling and an isolated verifier target; and
- retain the shared Tree-sitter runtime while any other built-in parser needs it.

The Tree-sitter CST adapter is deleted from production only after the reference target can build independently. No `.lua` build file or generated `parser.c` is manually edited.

## 12. Verification and hard acceptance gates

### 12.1 Correctness gate

All of the following are mandatory:

| Gate | Required result |
|---|---|
| C recognizer differential | zero unexplained JS/TS acceptance or rejection mismatches |
| canonical AST/fact differential | zero structural, span, binding, scope, type, class, or module mismatches |
| syntax/early-error goldens | exact error phase/class and approved span/message behavior |
| focused C parser tests | 100%, including fuzz regressions and malformed-input progress |
| `test_js_gtest` | all tests pass; current discovered GTest count is 357 |
| `test_ts_gtest` | all tests pass; current discovered count is 19 |
| `make test-js262-prelim` | pass |
| `make test262-baseline` | live manifest 100%; currently 40,288 entries; zero failure/crash/timeout/partial/non-full/retry |
| build/config | release and test builds pass; generated build config contains the intended production/reference split |
| hygiene | `git diff --check` and relevant lint gates pass |

The live manifest count is read at gate time; 40,288 is a 2026-08-28 snapshot, not a hard-coded runner expectation.

`test262-baseline` is not “green enough” with retries or a reduced manifest. A retry means the production gate failed because parser instability can be batch-order-, memory-, or timing-dependent.

### 12.2 Size gate

Use the same compiler, target, flags, LTO/dead-strip policy, and clean worktree source for A/B variants.

Required results:

1. the new combined JS/TS C lexer/parser archive is **at least 50% smaller** than the rebuilt combined Tree-sitter JS/TS grammar archives;
2. the stripped release `lambda.exe` is smaller, with the link map attributing the delta to the parser switch;
3. no large parser table or generated CST adapter is moved to another linked object to satisfy the archive metric; and
4. the shared Tree-sitter runtime is reported separately and is not claimed as saved while other languages link it.

Against the current non-authoritative archive snapshot, the provisional 50% ceiling is 607,348 B. P0 replaces that number with the clean, pinned A/B baseline.

### 12.3 Performance gate

Performance testing uses `make release`; debug results are invalid. The benchmark follows **D8.6.4v2** methodology: identical preloaded manifests, one warm-up, five complete measured runs, median comparison, and no file IO/execution/process/cleanup time.

Measure two distinct questions:

| Measurement | Old path | New path | Hard gate |
|---|---|---|---|
| recognizer | Tree-sitter parse to accepted/rejected CST status | C syntax sink | aggregate median <= 0.90x old; p95 <= 1.00x old |
| real front end | Tree-sitter parse + CST-to-AST + early errors/types | C parse + direct AST + same early errors/types | aggregate median <= 0.90x old; p95 <= 1.00x old |

Report JS, TS, Test262, large-library, and combined cohorts separately. Also report source bytes, files, tokens, AST nodes, median, p95, and worst regressions. A faster aggregate may not hide a material syntax-family regression; any cohort over 1.05x requires root-cause analysis and explicit approval even if the combined hard gate passes.

Cache hits, omitted failures, changed manifests, parser-result memoization, reduced diagnostics, or moving time into an unmeasured preprocessor cannot satisfy the gate.

### 12.4 Build-removal proof

After cutover, verify normal release and test production profiles:

- do not reference `tree_sitter_javascript` or `tree_sitter_typescript` symbols;
- do not link `libtree-sitter-javascript.a` or `libtree-sitter-typescript.a`;
- do not allocate `TSParser`/`TSTree` for JS/TS compilation;
- do not invoke `ts_preprocess_source()` on the production TS path; and
- still build the isolated reference verifier from unchanged vendored sources.

Symbol audit and link-map evidence are required; source grep alone is insufficient.

## 13. Risks and mitigations

| Risk | Mitigation |
|---|---|
| regex/division or template lexical-goal drift | parser-driven goals; focused mutation corpus; Tree-sitter external scanner as read-only oracle |
| ASI/restricted-production regressions | one ASI authority; parse-negative Test262 classification and exact line-break fixtures |
| pattern/expression/arrow ambiguity causes backtracking or side effects | bounded checkpoints; no sink calls before commit; recursion/progress budgets |
| TS becomes a copied second parser | mode + extension hooks; one JS statement/expression flow; type grammar isolated to committed type slots |
| direct AST changes invisible metadata | pointer-free comparator includes spans, scopes, bindings, types, module/class facts, and indexes |
| current CST builder semantics are copied into parser | extract shared constructors first; keep parsing syntax-only and semantic passes authoritative |
| preprocessor removal changes TypeScript behavior | shipped-preprocessor oracle plus structural-TS oracle; execution parity for all TS fixtures |
| parser is smaller only because it accepts less | bidirectional positive/negative differential and full Test262 baseline before size is considered |
| parser is faster only because work moved out of timing | measure both recognizer and complete source-to-validated-AST front ends |
| malformed input loops or overflows | checked cursor arithmetic, progress invariant, recursion budget, fuzzing, ASan/UBSan-focused runs |
| diagnostics regress | structured span/expected-token model and negative-phase/error-class goldens |
| editor consumers lose incremental parsing | keep unchanged Tree-sitter JS/TS grammars for editor/reference profiles |
| vendor boundary is crossed | no vendor edits; stop for approval if a defect genuinely belongs upstream |

## 14. Proposed decisions

1. Production JS and TS share one first-party C lexer and one RD/Pratt parser core.
2. JavaScript owns the base program/statement/expression grammar; TypeScript is an extension mode, not a copied parser.
3. The parser reduces directly into the retained `JsAstNode` graph and retains no replacement syntax tree.
4. Language-neutral cursor/checkpoint/report/reduction mechanics are extracted from the Lambda C parser and reused; Lambda grammar semantics are not.
5. Current JS/TS AST constructors, literal/name decoders, scope/module helpers, type builders, and early-error passes are shared between the temporary CST adapter and direct sink.
6. Common delimited, statement-list, pattern, callable-head, property, ASI, and recovery flows each have one owner.
7. The normal TypeScript source-rewrite preprocessor is retired at final cutover; TS transformations occur through shared AST constructors/lowering.
8. Tree-sitter JS/TS stays unchanged as editor/reference tooling and an isolated differential oracle.
9. Cutover requires canonical AST/fact parity, at least 50% smaller combined parser archives, at least 10% faster aggregate release parsing/front-end time with no p95 regression, fully green JS/TS suites, and a zero-retry full Test262 baseline.
10. A failed gate stops the project without hard-coding, weakening syntax, masking tests, or silently falling back after partial parsing.

## 15. Immediate next action

Implement P0 only: add the frozen JS/TS parser manifest, canonical AST/fact serializer, clean A/B size script, and release parser/front-end benchmark. Do not begin the C parser or edit build linkage until the existing production behavior—including the TypeScript preprocessor path and negative Test262 classifications—is reproducible as an oracle.
