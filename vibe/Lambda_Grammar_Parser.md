# Lambda Grammar Parser: Hybrid Recursive-Descent + Pratt Design

- **Date:** 2026-08-20
- **Status:** **PRODUCTION CUTOVER COMPLETE FOR NORMAL LAMBDA FILES/MODULES.** P2.1 source spans, P2.2 shared construction seams, P2.3 direct AST reduction, and the normal-runner P2.4 selector are landed. With `LAMBDA_PARSER` unset (or `c`), the first-party C hybrid recursive-descent + Pratt parser builds the existing typed AST directly. `LAMBDA_PARSER=tree`/`tree-sitter` is an explicit reference/rollback mode; `compare` runs Tree-sitter syntax acceptance after the C AST is built. The REPL and a few legacy inspection paths still retain Tree-sitter fragment trees because their append-only source/span transaction is separate from the file cutover.
- **Formal authority:** **D8.1.1v3** (first-party C parser → shared typed AST pipeline), **D8.1.2v2** (Tree-sitter grammar as regenerated reference/editor artifact), **D8.2.1–D8.2.5** (one core AST, no tree rewriting, indexed compilation unit, typed pass schedule), and **D4.1.1v2** (AST/const-pool ownership). The accepted language and S2.4.3v2/S7.6.3v2 syntax rulings are unchanged.
- **Surface-syntax authority:** `lambda/tree-sitter-lambda/grammar-lambda.js` remains the complete structural reference grammar; `grammar-common.js` plus the production replacement layer in `grammar.js` describes the currently shipped parser seams. S2.4.3v2 governs greedy namespace-qualified names, and S7.6.3v2 governs query at the postfix/member tier.
- **Related:** `vibe/Lambda_Grammar_Reduce5.md` (grammar seams and Tree-sitter reference size), `doc/dev/lambda/LR_02_Parsing_AST.md` (C parser and reference builder), `lambda/runtime/parser/lambda_lexer.c`, `lambda/runtime/parser/lambda_parser.c`, `lambda/runtime/build_ast.cpp`, `lambda/runtime/ast-core.hpp`, `lambda/runtime/parse_type_pattern.cpp`, and `lambda/runtime/parse_path_expr.cpp`.
- **Proposal IDs:** CGP1–CGP17.

## 1. Proposal

Replace the production Lambda-language Tree-sitter front end with a small first-party **C parser** built from:

1. a hand-written streaming lexer;
2. recursive-descent functions for documents, declarations, statements, containers, control forms, and delimited subgrammars; and
3. a Pratt parser for prefix, postfix, and infix expressions.

The final parser will produce the **same typed Lambda AST** consumed today by the compiler and T0 interpreter. It will not retain an intermediate Tree-sitter concrete syntax tree (CST), syntax tree (ST), or a replacement syntax tree. Parsing will reduce directly into the existing `AstNode` graph through a parser-output sink. The token stream, parser stack, and bounded lookahead are temporary parsing state, not another tree.

The work is intentionally split into two decision-gated phases:

- **Phase 1 — parser POC:** implement the complete current Lambda grammar, parse every current Lambda source in the repository, measure the linked parser size, and compare parse performance with Tree-sitter. The POC does not replace the production parser and does not need to construct the production AST.
- **Phase 2 — direct AST and switch:** only if Phase 1 is much smaller and comparably fast, make source locations parser-neutral, connect the C parser to the existing AST constructors, prove AST and behavioral parity, then remove the Tree-sitter Lambda parser from the production link.

This separation prevents a large AST migration from obscuring the first question: **does a hand parser actually recover enough binary size to justify replacing Tree-sitter?**

## 2. Motivation and measured baseline

Grammar Reduction5 has already reduced the generated Lambda parser substantially, including removal of all declared GLR conflicts. The remaining object is still large because grammar-dependent tables dominate it:

| Current Lambda parser artifact | Size |
|---|---:|
| generated `parser.c` | 5,715,314 B |
| `parser.o` | 800,432 B |
| `scanner.o` | 17,072 B |
| `libtree-sitter-lambda.a` | 818,048 B |
| states / large states / symbols | 4,124 / 699 / 218 |
| declared GLR conflicts | 0 |

The v0.26.11 Tree-sitter POC did not improve this result: ABI 14 produced an 800,704-byte object and ABI 15 produced an 808,984-byte object. Roughly 704 KB of the size-optimized object is the dense table, sparse table, sparse-state map, and parse actions. Tree-sitter's compatible-state merging is already active; disabling it grows the parser object to 3.32 MB. `-Oz` reduces the current parser-plus-scanner archive only 3.64%, to 788,248 bytes. `vibe/Lambda_Grammar_Reduce5.md` records the full measurements and the 921-file CST differential check.

The remaining size is therefore architectural rather than a missed compiler or generator flag. A hand parser replaces thousands of generated states and table rows with code proportional to the grammar rules and operator table.

## 3. Goals and non-goals

### 3.1 Goals

- **G1 — current-language parity.** Accept every source accepted by the current Lambda front end and reject every intentionally invalid syntax fixture.
- **G2 — materially smaller code.** Measure the complete unique lexer/parser contribution, not source-line count or a stripped test harness.
- **G3 — comparable parse speed.** Compare on identical preloaded sources in a release build, with repeatable aggregate and per-file measurements.
- **G4 — same semantic AST.** Phase 2 must produce the same node kinds, child order, sibling links, operators, source spans, names, types/contracts, scopes, bindings, captures, and compiler-pass inputs.
- **G5 — no retained syntax tree.** The production path becomes `source → tokens → direct AST`; it does not replace a Tree-sitter CST with another stored parse tree.
- **G6 — structured diagnostics and REPL status.** The parser reports source ranges, expected tokens, and `complete` / `incomplete` / `error` status without depending on Tree-sitter `ERROR` or `MISSING` nodes.
- **G7 — preserve the grammar oracle.** The full Tree-sitter grammar remains a reference implementation, differential oracle, editor grammar, and bindings artifact even after it leaves the Lambda executable's production path.
- **G8 — one implementation of each semantic constructor.** The direct parser and temporary Tree-sitter path share extracted AST/type/path construction helpers; Phase 2 must not copy the large `build_ast.cpp` cases.

### 3.2 Non-goals

- No Lambda syntax or semantic change is part of this migration.
- The normal file/module switch is landed under D8.1.1v3; remaining Tree-sitter
  use is explicit reference/rollback or a separate REPL/inspection consumer.
- The POC does not replace Tree-sitter for JavaScript, TypeScript, Python, Ruby, Bash, LaTeX, or other guest/input grammars.
- The runtime parser does not need Tree-sitter's incremental-edit API. Tree-sitter may remain the editor-facing grammar.
- The parser will not silently accept a simpler language to win size.
- The parser will not introduce a fifth allocator mechanism; parser scratch state is bounded stack state or existing pool/arena-owned state under D4.1.1v2/D4.1.4v4.
- The frozen C2MIR path receives no special support.

## 4. Current pipeline and compatibility boundary

The cut-over Lambda file/module path is:

```text
source
  → lambda/runtime/parser/lambda_lexer.c
  → lambda/runtime/parser/lambda_parser.c (RD + Pratt)
  → lambda/runtime/build_ast.cpp direct reduction sink
  → typed AstNode graph + scopes + bindings + captures
  → AST index / analysis / T0 interpreter / MIR Direct
```

The reference path remains available explicitly:

```text
source → lambda/runtime/parse.c → Tree-sitter parser.c/scanner.c
       → TSTree / TSNode → legacy build_ast.cpp CST adapter → same AstNode graph
```

The direct path shares semantic constructors with the reference adapter. Tree-sitter is still embedded in the following non-cutover consumers:

- `AstNode` currently stores a `TSNode` in `ast-core.hpp`;
- `build_ast.cpp` accepts and traverses `TSNode` throughout;
- literal parsing and source extraction call `ts_node_start_byte`, `ts_node_end_byte`, and `ts_node_symbol`;
- structured errors consume Tree-sitter `ERROR` and `MISSING` nodes;
- MIR naming and diagnostics still read source positions from retained AST `TSNode`s;
- the REPL uses retained fragment trees and `MISSING` nodes to distinguish incomplete input from invalid input;
- explicit Tree-sitter mode and comparison validation walk reference CST nodes;
- the validator, AST dump/s-expression tools, Jube declaration parser, and metaprogramming paths call the Lambda Tree-sitter wrapper.

The current `build_ast.cpp` also combines several logically distinct jobs: CST traversal, AST allocation, local type construction, name resolution, scope/capture bookkeeping, semantic validation, and diagnostic creation. A direct parser cannot safely pretend a byte range is a `TSNode`. Phase 2 must first make the retained AST's source contract parser-neutral and then extract reusable AST constructors from CST traversal.

## 5. Target architecture

```text
source → C lexer → RD + Pratt parser → direct AST sink
                         │                     │
                         ├→ structured status  ├→ AstNode pool
                         └→ optional compare   ├→ scope/binding/type helpers
                            (Tree-sitter)      └→ existing compiler passes
```

There is one parser core and a small output-sink interface:

- the **Phase 1 sink** validates reduction order and computes a stable structural fingerprint without retaining a tree;
- the **Phase 2 sink** returns `AstNode*`-backed values and invokes the shared production constructors;
- an error or abandoned lookahead never publishes semantic side effects.

The sink is not an intermediate representation. Each completed production is reduced immediately, and child values are released from the parser stack once the parent reduction returns. No syntax-node graph survives parsing.

### 5.1 Source and parser API

The parser core has a C ABI and uses no `std::` types:

```c
typedef struct SourceSpan {
    uint32_t start_byte;
    uint32_t end_byte;
} SourceSpan;

typedef enum LambdaParseStatus {
    LAMBDA_PARSE_OK,
    LAMBDA_PARSE_INCOMPLETE,
    LAMBDA_PARSE_ERROR,
} LambdaParseStatus;

typedef struct LambdaParseError {
    SourceSpan span;
    uint64_t expected_token_bits[4];
    int actual_token;
    const char* message;
} LambdaParseError;

LambdaParseStatus lambda_parse(
    const char* source, size_t length,
    const LambdaParseSink* sink, void* sink_ctx,
    LambdaParseResult* result);
```

Byte ranges are the canonical stored location. Line and column are derived through one per-source newline index, so every AST node need not carry redundant row/column fields. A missing-token diagnostic is a zero-length range. The exact names and bit widths may change during implementation, but these invariants do not:

- source ranges are parser-neutral;
- offsets cover the original UTF-8 bytes;
- the end offset is exclusive;
- range construction is overflow-checked;
- diagnostics never borrow a temporary token buffer after parsing returns.

### 5.2 Lexer

The lexer is streaming with bounded lookahead. It owns:

- UTF-8 identifier boundaries matching the effective current grammar;
- reserved keywords and base-type spellings;
- integers, floats, sized numerics, decimals, datetime, binary, strings, symbols, and pattern introducers;
- longest-match punctuation and operators such as `...`, `.?`, `~~`, `~#`, `=>`, `|>`, `**`, `<=`, `>=`, `==`, and `!=`;
- line and block comments;
- byte offsets and newline accounting;
- explicit newline tokens where the statement parser needs to decide termination.

Spaces and comments are extras. Newline is not discarded globally: the parser decides whether it terminates content, continues an incomplete expression/type, or belongs inside a delimiter. This preserves the current `statement_end` behavior without reproducing Tree-sitter lexical states.

The normal path holds only the current token and bounded lookahead. Targeted ambiguity probes may checkpoint the lexer byte/line state and restore it; they may not allocate AST nodes, enter scopes, or emit diagnostics until the branch commits. There is no unrestricted backtracking engine.

### 5.3 Recursive-descent layer

Recursive descent owns forms with a decisive leading token or delimiter:

| Area | Representative forms |
|---|---|
| compilation unit | document, imports, top-level content |
| statements/declarations | `let`, `var`, `type`, `fn`, `pn`, `view`, `edit`, object type, assignment, return, raise, break, continue, `apply;` |
| control | `if`, `match`, `case`, `for`, `while`, handlers, view events/state |
| containers | arrays, maps, elements, parameter/argument lists |
| restricted sublanguages | return contracts, type patterns, view patterns, static paths, string/symbol pattern islands |
| content | statement sequences, final value expression, newline/semicolon boundaries |

The existing first-party `parse_type_pattern.cpp` and `parse_path_expr.cpp` prove that bounded hand parsing already fits the Lambda grammar. The C parser owns only the **outer placement and exact source span** of a type slot or static-path slot. Its committed span is then passed to those existing parser entry points by the direct-AST sink; it does not create a second semantic type/path grammar.

Today those helpers take `Transpiler*` and `TSNode` because they allocate existing AST/type/path objects and attach diagnostics. P1 keeps the corresponding C reductions as source-span seams, without attempting to synthesize a `TSNode`. P2.1 replaces that diagnostic/location dependency with `SourceSpan`; P2.2 then adapts the existing helpers and constructors for the direct-AST sink. This is a prerequisite for reuse, not permission to copy their grammar into `lambda_parser.c`.

### 5.4 Pratt expression layer

The Pratt parser is `parse_expr(min_binding_power, context)`. Its context carries allowed terminators and the attribute-expression restriction; it does not fork the entire grammar.

The prefix/null-denotation side handles literals, identifiers, current-item/error atoms, grouping/arrow heads, arrays, maps, elements, paths, unary operators, `raise`, `let`, `if`, `match`, `for`, and anonymous functions.

The postfix/infix loop handles:

- member and navigation fields;
- calls and named arguments;
- single- and multi-dimensional indexes;
- query `?T` / `.?T`;
- propagation and handler postfixes;
- exponentiation, multiplicative, additive, relational, equality, range, set, membership, logical, and pipe/filter operators.

One static operator table is the C parser's precedence authority. It records token kind, left/right binding power, associativity, and AST operator. Its ordering reproduces the current tight-to-loose order:

```text
member/navigation
call/index/query/propagate/handler
primary
unary
**                           (right associative)
* / div %
+ ++ -
< <= >= > lt le ge gt
== != eq ne
to
&
binary !
|
is in at
and
or
|> that
control/let/assignment forms
```

S7.6.3v2 requires query to remain on the left-associative postfix/member tier. Attribute-expression context excludes symbolic `<`, `<=`, `>=`, and `>` relations so element delimiters keep their current meaning; word relations remain available exactly as in the reference grammar.

### 5.5 Deliberate ambiguity handling

The current grammar has zero declared GLR conflicts, but several prefixes still need deterministic lookahead in a hand parser:

| Prefix | Decision rule |
|---|---|
| `(x)` versus `(x) => body` | Run a side-effect-free arrow-head probe through the matching `)` and optional restricted return contract. Commit to the arrow form only on `=>`; otherwise parse the normal parenthesized form. |
| `{...}` map versus control/function block | Expression-prefix `{` is a map. A block is parsed only after a construct that explicitly requires a body. |
| `<tag ...>` versus binary `<` | Prefix `<` begins an element; infix `<` after a left expression is relational. Element attribute/content parsing owns the matching `>`. |
| expression `if` versus statement `if` | Parse through one unified control routine, then produce the same current AST form based on the syntactic context and required `else` contract. No CST distinction may leak downstream. |
| assignment versus expression | Statement lookahead recognizes the assignable target followed by `=`; Pratt otherwise parses equality/member/index normally. |
| named argument/map item versus ordinary expression | Only argument/map contexts recognize `name:` as a keyed form. |
| dotted name versus relative path | S2.4.3v2 makes namespace `dotted_name` maximal in element/attribute-name position. A relative-path child requires the explicit `;` boundary recorded by Grammar Reduction5. |
| newline versus continuation | Delimiter depth, pending prefix/infix operator, and content context determine continuation; the decision is centralized, not copied into every rule. |
| `start(...)` | `start` is an ordinary identifier/call and receives no lexer or parser special case. The AST/system-`pn` recognition remains semantic. |

No ambiguity branch may mutate the AST sink before it commits. A parse failure discards the entire POC result or AST pool; partial semantic trees are never executed.

### 5.6 Diagnostics and recovery

Tree-sitter currently creates `ERROR` and `MISSING` nodes and `find_errors()` translates them into `LambdaError`. The C parser instead reports the error at the point where a production cannot continue:

- actual token and exact byte range;
- a compact expected-token set;
- the opening-delimiter range for an unclosed delimiter;
- whether end-of-input could complete the construct;
- a stable syntax error code and optional targeted help.

`LAMBDA_PARSE_INCOMPLETE` replaces the REPL's scan for Tree-sitter `MISSING` nodes. Files use fail-closed parsing: any syntax error prevents AST publication. Panic-mode synchronization may continue at `;`, a terminating newline, `}`, `>`, `case`, or `default` to collect additional diagnostics, but recovered nodes never enter an executable AST.

Phase 1 requires reject/status/span parity, not byte-identical Tree-sitter recovery trees. Phase 2 requires all existing structured-error tests to pass and diagnostics to retain at least the current error code, source location, missing delimiter, and actionable help.

### 5.7 Recursion and ownership

- Parser recursion is capped at the existing effective nesting limit (`MAX_BUILD_DEPTH` is currently 1000); exceeding it returns a structured syntax error rather than overflowing the native stack.
- Repetition lists use the existing `ArrayList`, pool, or arena facilities only where a list cannot be reduced incrementally.
- Phase 2 AST nodes and compiler-owned constants remain in the AST/const pool under D4.1.1v2.
- Speculative lookahead is allocation-free. Committed AST construction may allocate; on failure the whole compilation pool is released.
- The lexer validates offsets and UTF-8 progress so malformed input cannot create an infinite loop.

## 6. Phase 1 — complete parser POC

Phase 1 answers only: **is the complete hand parser sufficiently smaller and comparably fast?** It is not a partial expression demo. It must implement the current whole language before its size is used to justify Phase 2.

### P1.0 — freeze the oracle and corpus

At POC start, record:

- Git commit, compiler identity, target architecture, and release flags;
- hashes of `grammar-common.js`, `grammar-lambda.js`, `grammar.js`, and the external scanner;
- current Tree-sitter parser/scanner object and live-section sizes;
- a manifest of every tracked `*.ls` source plus any runtime-generated Lambda fixture explicitly used by a test.

The current snapshot has 1,472 tracked `.ls` files, of which 921 are under `test/lambda`; the generated manifest, not these prose counts, is authoritative. Tree-sitter classifies each source as syntactically valid (`!ts_node_has_error(root)`) or invalid. Intentionally invalid/negative fixtures stay in the manifest and must remain rejected.

**Initial P1.0 artifact (2026-08-19):** `bash utils/lambda_parser_manifest.sh temp/lambda-parser-poc/manifest.tsv` produced the 1,472-source / 921-`test/lambda` snapshot above. Its header records the Git commit and SHA-256 hashes of both grammar layers and `scanner.c`; each source row records path, byte count, and SHA-256. The generated manifest remains untracked under `temp/` and is regenerated for each differential run.

### P1.1 — implement the lexer

Add the first-party lexer and focused unit tests for every token class, comment/string interaction, Unicode identifier boundary, longest-match operator, newline case, and malformed token. The lexer must always advance or return an error.

### P1.2 — implement recursive descent and Pratt parsing

Implement every structural rule in the full reference grammar. The initial parser-core checkpoint currently covers the lexer, Pratt expression backbone, bounded type/path slot placement, delimited collections, selected declarations, and `complete`/`incomplete` status; it is not a P1.3 parity result. Every completed parser function either:

- succeeds after consuming its complete production;
- reports `INCOMPLETE` only when more source can validly complete it; or
- reports `ERROR` with a range and expected-token set.

Success requires end-of-source after trailing extras. Silently stopping at a valid prefix is a failure.

The POC sink computes a stable reduction fingerprint from production kind, child fingerprints, and source range. It retains no syntax tree. Type/path reductions record the exact source span for the existing direct helpers; the Phase 1 sink does not make type/path AST objects. The fingerprint is a debugging aid for deterministic reruns; acceptance parity is the Phase 1 correctness gate because the current CST and target semantic reductions are not one-to-one.

### P1.3 — differential correctness

Run both parsers over the frozen manifest in one test process:

- every Tree-sitter-valid source must return `LAMBDA_PARSE_OK` and consume all bytes;
- every Tree-sitter-invalid syntax fixture must return `ERROR` or `INCOMPLETE` consistently with file/REPL mode;
- no source may crash, hang, exceed the recursion limit without a diagnostic, or depend on test order;
- repeat parsing must produce the same status, error range, and fingerprint;
- the existing fuzzy Lambda corpus and delimiter/operator mutations run through both parsers to expose accidental widening.

For valid sources, Phase 1 also records top-level import ranges and major production counts as cheap structural checks. Full AST equivalence belongs to Phase 2.

**P1.3 valid-source checkpoint (2026-08-19):** `bash utils/lambda_parser_diff.sh` regenerates the P1.0 manifest, builds `test/lambda_parser_poc_diff.c` under `temp/`, and runs the production Tree-sitter archive and C POC in one process. The frozen 1,472-source manifest contains 1,382 sources whose shipped Tree-sitter root has no error; the C recognizer accepts all **1,382 / 1,382** (`missing=0`). The focused POC suite has 28 tests and strict C17 compilation (`-Wall -Wextra -Werror`) is clean.

The checker also reports eight sources accepted by the C recognizer for which the Tree-sitter root carries an error. They are retained as an explicit classification queue, not silently declared parity: three shipped schema/OpenAPI sources, three positive source tests, one semantic-negative fixture, and one validator fixture. The known syntax-negative fixtures for empty parenthesized expressions and bare string `<` comparison now return non-OK. Therefore the valid-source acceptance requirement is green, but the full P1.3 reject/status gate remains open until each of the eight Tree-sitter-error sources is classified and the intended acceptance status is pinned.

### P1.4 — size measurement

Measure in release configuration with the same compiler and target:

1. raw object size;
2. live `text + const + data` section bytes;
3. static-archive member contribution;
4. stripped linked executable delta with and without the parser;
5. the same measurements under the production optimization flags and, separately, `-Oz`.

The C number includes every unique lexer, parser, operator table, diagnostic table, and non-test helper it needs. It excludes the benchmark harness and Phase 2 AST sink. The Tree-sitter comparison baseline is the current 818,048-byte Lambda parser/scanner archive. The shared Tree-sitter runtime is reported separately because other in-process grammars still require it after the Lambda switch.

**Preliminary recognizer-only snapshot (2026-08-19):** compiling the POC with `cc -std=c17 -O3 -DNDEBUG` yields 11,768 B for `lambda_lexer_release.o` and 68,848 B for `lambda_parser_release.o` (80,616 B combined Mach-O segments). The current Lambda Tree-sitter archive's `parser.o` and `scanner.o` occupy 786,512 B and 13,128 B respectively (799,640 B combined), while the archive file is 818,048 B. The recognizer-only POC is therefore about 10.1% of the current parser/scanner object footprint. This is encouraging but **not a P1.4 or P1.6 pass**: it excludes the Phase 2 direct-AST sink, parser-neutral span migration, and the final executable link.

### P1.5 — performance measurement

Performance testing uses a release build only. Sources are preloaded so disk I/O and process startup do not count. One process repeatedly runs:

- Tree-sitter parse with `old_tree = NULL`, deleting each result tree;
- C parse with the POC sink, resetting parser scratch state between sources.

Use one warm-up followed by five complete measured runs, reporting the median, matching D8.6.4v2's timing discipline. Record:

- total corpus time and source MB/s;
- median and p95 per-file latency;
- the largest-source cohort separately;
- parser peak temporary bytes;
- success/error counts, with missing or retried samples forbidden.

Phase 1 compares parse stages only. Phase 2 repeats an end-to-end `Tree-sitter parse + CST→AST` versus `C parse→AST` comparison.

The standalone CST performance harness has been retired. The measurements
below are historical recognizer-only snapshots; the maintained verification
runner is `bash utils/lambda_parser_diff.sh`, which keeps the Tree-sitter
grammar isolated in the `lambda-cst` profile.

**P1.5 recognizer-only checkpoint (2026-08-20):** on the 1,472-source,
4,638,380-byte manifest, the median shipped Tree-sitter parse was **599.378
ms** (**7.38 MiB/s**) and the C RD/Pratt recognizer was **171.545 ms**
(**25.79 MiB/s**): `rd_over_tree=0.286`, or **3.49x** parser-stage speedup.
Median per-file latency was 122 us versus 21 us; p95 was 1,475 us versus 340
us. The 16 largest sources (1,115,259 bytes together) took 145.166 ms for
Tree-sitter and 81.661 ms for the C recognizer. Tree-sitter reported 1,382
OK / 90 error roots and the recognizer 1,390 OK / 1 incomplete / 81 error,
which is the documented eight-source classification difference. This clears
the numerical P1.6 speed thresholds for the recognizer-only stage, but is
**not** a Phase 1 go decision and does not predict Phase 2 AST performance.

**Latest rerun (2026-08-20):** the current tracked manifest contains 1,486
sources and 4,647,118 bytes. Across five warmed runs, Tree-sitter's median was
**690.664 ms** (**6.42 MiB/s**) and the C recognizer's median was **193.332 ms**
(**22.92 MiB/s**), for `rd_over_tree=0.280` and a **3.57x** parser-stage
speedup. Median per-file latency was 122 us versus 23 us; p95 was 1,700 us
versus 368 us. The largest-16 cohort took 157.355 ms versus 85.853 ms. The
run reported 1,396 Tree-sitter OK / 90 error roots and 1,402 C OK / 1
incomplete / 83 error; this is still a recognizer-only measurement, not a
claim about full direct-AST end-to-end timing.

### P1.6 — go/no-go gate

Phase 2 proceeds only if every mandatory gate passes:

| Gate | Required result |
|---|---|
| language corpus | 100% valid-source acceptance and negative-fixture rejection parity |
| robustness | zero crashes, hangs, silent prefix acceptance, or nondeterministic results |
| size | complete C lexer/parser static archive is **at least 50% smaller** than the like-for-like 818,048 B archive — target **≤409,024 B**; the stripped linked delta must independently confirm the saving |
| aggregate speed | C parser median complete-corpus time is no worse than **1.10×** Tree-sitter |
| tail speed | C parser p95 per-file latency is no worse than **1.20×** Tree-sitter |
| temporary memory | no material regression; any increase must be measured and justified before proceeding |

The desirable size result is ≤25% of the current archive (≤204,512 B), but it is not mandatory. The 50% gate gives “much smaller” a concrete minimum. If correctness fails, size and speed are irrelevant. If size or performance misses the gate, record the result in this document and stop; do not weaken the grammar or hard-code corpus cases.

### P1.7 — POC artifacts

Proposed implementation locations:

```text
lambda/runtime/parser/lambda_parser.h
lambda/runtime/parser/lambda_lexer.c
lambda/runtime/parser/lambda_parser.c
lambda/runtime/parser/lambda_parser_pratt.c
test/test_lambda_parser_poc_gtest.cpp
utils/lambda_parser_manifest.sh
temp/lambda-parser-poc/             # generated manifests and measurements
```

File boundaries may be consolidated if that makes the C parser smaller or clearer. Build integration goes through `build_lambda_config.json` and the normal generated build path; generated Lua and Tree-sitter vendor sources are not edited.

## 7. Phase 2 — compatible direct AST and production switch

The production selector cutover is now recorded under the P2.6 transition gate.
The Tree-sitter reference remains linked because the REPL, explicit rollback mode,
and editor/binding consumers still need it; removing that library is a separate
consumer-migration task, not a prerequisite for the normal C parser default.

### P2.0 — revise the formal pipeline ruling (landed)

`doc/Lambda_Formal_Design.md` now records D8.1.1v3 and D8.1.2v2, with the
semver bump to 1.26.3. S15.3 remains unchanged: `input(f, 'lambda')` still
produces the canonical `lm.` AST.

### P2.1 — make retained AST source locations parser-neutral

`AstNode::source_span` is now the parser-neutral two-offset `SourceSpan`; the
transitional `AstNode::node` field has been removed. Retained-AST consumers use
the span for:

- source-text slicing and literal conversion;
- structured semantic diagnostics;
- MIR/function debug naming and source maps;
- AST dumps and formal-semantics emission;
- interpreter literal reads;
- type/path/pattern helpers that currently accept a `TSNode` only for location.

The legacy CST adapter converts each `TSNode` to `SourceSpan` at AST allocation,
while the direct parser supplies the same range from its reductions. No
retained AST node requires a live `TSTree`.

Because core AST nodes are shared under D8.2.1, the source reference is parser-neutral for all language profiles. Tree-sitter-based guest builders fill the same range from their `TSNode`; they do not force Tree-sitter storage back into core `AstNode`.

**P2.1 checkpoint (2026-08-20):** `SourceSpan` is the common half-open
byte-range type in `lambda/runtime/source_span.h`; every `AstNode` stores it.
The legacy CST adapter and direct sink populate it from their respective parser
coordinates. Structured diagnostics, literal decoding in T0/MIR, MIR
source-name identity, and AST dump source extraction read the retained span.
Type-pattern and static-path parsers have equivalent span entry points; their
CST APIs are isolated compatibility adapters.

### P2.2 — separate traversal from AST construction

Refactor `build_ast.cpp` by extracting reusable production constructors and semantic hooks rather than copying cases into the C parser:

- AST allocation and source range;
- scalar/container/type node construction;
- operator classification and type inference;
- name interning and declaration registration;
- scope enter/leave, forward-definition handling, and capture bookkeeping;
- function/view/loop/control constructors;
- semantic-error recording.

During the dual-front-end period, the legacy CST walker and the direct parser sink call the same helpers. At the third near-identical construction shape, extract the shared helper first, per repository rule 13. Once parity is proven, delete the CST adapters; do not retain two semantic builders.

Parser lookahead must not enter a `NameScope` or register a declaration. Scope lifecycle callbacks occur only after a syntactic branch commits, and a failed compilation releases the complete AST pool.

**P2.2 completion checkpoint (2026-08-20):** The reduction ABI carries a
committed form and introducer/operator token in addition to child values.
Pratt binary and postfix reductions publish the complete expression span,
including their left child; the C parser does not re-lex that source in a
future sink. `ast_build.hpp` is now the shared construction boundary for:

- scalar literal decoding and constant/type creation from a source span;
- identifier resolution, contextual atoms, primary wrappers, and ordinary
  unary classification/type inference;
- array/map assembly, including map-spread shape handling, plus named call
  arguments;
- committed scope entry/leave, name registration, and in-place function
  forward placeholders; and
- call-boundary argument-count/type validation using a source span rather
  than a `TSNode`.
- ordinary-call `start(target, args, options)` validation, including positional
  arguments, launch arrays, and mode options, is shared by both front ends
  through that same span boundary.
- `that` expression context is published as paired reductions around the Pratt
  RHS, so direct identifier construction applies the existing scope-first,
  implicit-`~.field` lookup rule while the expression is being reduced.
- Optional typed parameters retain their scalar ABI lane but publish a nullable
  contract; omitted arguments therefore enter direct native calls as semantic
  `null`, matching the reference AST/runtime boundary (D2.5.1).

The Tree-sitter walker and direct sink use those literal/container/named-
argument and scope/forward-definition helpers, including function/procedure
bodies, view handlers, loops, branch arms, and the row-to-aggregate scope
transition of `for ... group`. The C recognizer publishes explicit reductions
for map items, named arguments, element attributes, typed assignments,
procedural stores, and branch scopes. The focused POC suite now has 31 tests;
the cutover probes cover imports, closures, loops, mutable assignments,
typed/indexed reads, and VMap methods.

P2.2 is therefore complete as a shared-construction layer. The remaining
duplicate CST traversal is intentional reference/REPL compatibility code, not a
second production semantic builder.

### P2.3 — implement the direct AST sink

#### P2.3 implementation design checkpoint (2026-08-20)

The direct sink is a parser transaction around the existing `Transpiler` AST
owner, not a second builder and not a CST-shaped compatibility tree. The
transaction has four explicit layers:

1. `LambdaAstParseContext` owns the source, `Transpiler`, current semantic
   scope, parser mode (full file or REPL fragment), and the root/last-node
   publication state. It is created only after the parser has accepted the
   source header and is discarded with the AST pool on a failed parse.
2. `LambdaParseValue` is an opaque `AstNode*` (or a short-lived list/slot
   descriptor represented by an AST-owned helper object). The parser remains
   responsible only for token placement and committed reduction order; it
   never resolves names, types, scopes, or operators.
3. The sink dispatches on `(reduction.kind, reduction.form)` and calls the
   span-native helpers in `ast_build.hpp`. Child order is the parser's
   canonical order; no sink callback re-lexes or reparses source text.
4. The root reduction publishes exactly one `AstScript` only after all
   reductions and semantic validation succeed. A failed reduction records a
   structured error and aborts the transaction; no partially built AST is
   executable or attached to a `Script`.

The first sink slice is deliberately narrow and testable: token/literal,
identifier/contextual atom, unary, group, list, array/map item, element
attribute, member, call/named argument, and binary reductions. Each slice has
an AST canonicalization fixture against the existing Tree-sitter builder
before the next slice is enabled. Declarations, functions, views, loops,
match, and control forms then reuse the same scope and forward-definition
hooks already exercised by the CST walker; their parser reductions must carry
the declaration kind, binding names, and clause boundaries explicitly rather
than making the sink infer them from a span.

`lambda_rd_build_ast(...)` is the production adapter. It initializes the
`Input`/`Script` AST owner, invokes `lambda_rd_parse_source(...)`, and returns a
fully indexed AST or structured failure. The Tree-sitter path uses the same
post-build pass manager when explicitly selected. Import discovery for normal
modules is now performed by the direct AST/import reductions; REPL fragment
parsing remains a retained Tree-sitter transaction until its source-offset
contract is migrated.

This design is implemented under D8.1.1v3 and preserves D8.2.1/D8.2.2's one
core AST/no-rewriting rules.

#### P2.3 reduction-contract design (2026-08-20)

The next sink slice keeps clause data explicit instead of making the sink
reparse a `for`, `match`, handler, or declaration span. The reduction ABI is
extended with a small set of syntax forms (`MATCH_ARM`, `FOR_BINDING`,
`FOR_WHERE`, `FOR_GROUP`, `FOR_ORDER`, `FOR_LIMIT`, `FOR_OFFSET`, `IMPORT`,
`TYPE_DECL`, and `VIEW_DECL`). These forms are still syntax-only: names and
expressions arrive as committed token/child values, while shared AST
constructors own type checks, scope transitions, and diagnostics. A `for`
reduction is a list of clause reductions followed by its body; a match arm is
`pattern` (or an explicit default marker) plus a body; a handler carries the
operand, error body, and optional value body. This makes clause ordering and
scope boundaries observable without adding a second CST-shaped intermediate.

The sink now implements the full current reduction family: handlers,
match/if/while control, `for` clauses, imports, type declarations, views,
anonymous arrows, typed bindings, and procedural stores. Each family is
fail-closed: an unsupported reduction rejects the direct parse rather than
silently falling back to Tree-sitter.

Each completed parser production returns an opaque `LambdaParseValue`; under the AST sink that value identifies the already-built `AstNode` or a short-lived descriptor needed by a parent production. Reduction performs the work currently done after CST traversal:

```text
token spans + child values
  → shared AST constructor
  → AstNode* with identical fields/type/source span
  → parent reduction
```

The direct path must preserve wrapper nodes that downstream code observes, including `AST_NODE_PRIMARY`, rather than “cleaning up” the tree during migration. D8.2.2 forbids syntax migration through tree rewriting. Any later AST simplification is a separate design and baseline campaign.

The direct sink starts from the P2.2 shared scalar, atom, collection,
named-argument, scope, declaration, and call-validation seams, then composes
member/call resolution, binary special forms, element/object,
function/view/loop/control, and assignment stores through the same
`build_ast.cpp` constructors. No semantic case is copied into the C parser.

After the root reduction, the same compiler pass manager builds the AST index and
runs the existing analysis/planning/lowering schedule. The parser does not
create a second pass schedule. This is the path used by the normal runner after
the production cutover.

### P2.4 — dual-front-end differential mode

Temporarily compile both Lambda front ends behind a test-only selector:

```text
LAMBDA_PARSER=tree-sitter
LAMBDA_PARSER=c
LAMBDA_PARSER=compare
```

`compare` currently parses the direct AST and then runs Tree-sitter as a syntax
acceptance oracle. It does not claim canonical AST equality: AST pools, scopes,
names, imports, and type objects are owned state, and a complete pointer-free
serializer is not yet a cutover gate. The explicit Tree-sitter mode remains the
rollback path.

Extend `--emit-ast-dump` or add a dedicated AST comparator so equality covers:

- every node kind and sibling/child position;
- source span and source spelling where semantically retained;
- operator and form flags;
- interned names by bytes/length;
- type/return/parameter contracts by normalized structure;
- declaration/use binding identity by canonical node path;
- scopes, captures, concurrency analysis, and AST index ownership;
- constants and path/type/pattern payloads.

The existing AST dump is a useful future differential tool but is not the
runtime selector's source of truth.

### P2.5 — migrate every Lambda parser consumer

The normal file/module switch is complete. These consumers are tracked
separately because they intentionally retain reference-parser state:

| Consumer | Required replacement |
|---|---|
| normal runner/transpiler | **landed:** C parse→AST by default; `tree` is explicit rollback |
| validator/schema compilation | **landed for normal script compilation:** shared direct AST path |
| REPL | **transitional:** retained fragment trees still provide append-only source offsets and completeness |
| import discovery | **landed for direct modules:** import reductions feed normal module loading; reference scan remains in explicit Tree mode |
| AST dump and s-expression emission | **transitional:** direct AST/span support is available; legacy CST tools remain |
| Jube declaration inspection | **transitional:** reference parser is retained |
| `input(..., 'lambda')` / `compile(ast)` bridge | preserve the S15.3 canonical `lm.` AST contract |
| tests and CLI helpers | default direct; `LAMBDA_PARSER=tree` and `compare` provide the reference paths |

The reference Tree-sitter grammar and language bindings remain built for
editor/tool consumers. The production executable still links the Lambda
reference archive because REPL and legacy tools use it; this does not change the
normal C parser default.

### P2.6 — Phase 2 acceptance and switch gate

The cutover gate used for the normal runner is:

- the direct parser accepts the current positive corpus and rejects malformed
  syntax without a crash or silent fallback;
- focused direct-vs-reference execution probes pass for imports, closures,
  loops, procedures, mutable/index/member assignments, typed/indexed reads, and
  VMap methods;
- the C parser POC suite, build, and generated-build rules pass;
- the default runner path is direct C, with `tree` as an explicit rollback and
  `compare` as a syntax differential check;
- parser-neutral spans and shared AST constructors are used by both front ends;
- `git diff --check` is clean.

The full canonical AST serializer, release timing ratchet, and removal of the
Tree-sitter archive remain follow-up gates for deleting the reference path. They
are not prerequisites for the production-default cutover recorded by D8.1.1v3.

## 8. Performance and size interpretation

The two phases answer different performance questions:

| Measurement | Purpose |
|---|---|
| Phase 1 C recognizer versus Tree-sitter parse | Is the parser architecture itself viable? |
| Phase 2 C parse→AST versus Tree-sitter parse+CST→AST | Does skipping the CST improve the real front-end path? |
| full compiler parse-through-link timing | Ensure the switch does not disturb the D8.6.4v2 compiler timing ratchets |

Tree-sitter creates a CST while the C POC sink does not. That is not an accidental benchmark advantage; eliminating the CST is part of the proposed architecture. Phase 2 nevertheless repeats the end-to-end comparison because direct semantic construction, names, types, and scopes are real costs that Phase 1 intentionally excludes.

The 818,048-byte archive is the fair current **Lambda grammar** baseline. Do not claim removal of the shared Tree-sitter runtime while JavaScript or other built-in parsers still link it. Conversely, if a future minimal Lambda-only executable can drop the runtime, report that as a separate additional saving.

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| grammar drift between C and Tree-sitter | keep `grammar-lambda.js` as oracle; differential manifest and fuzz gate every grammar change |
| hand parser accepts only the happy-path corpus | include negative fixtures, delimiter/operator mutation, full-source consumption, and randomized corpus tests |
| arrow/element/block ambiguities cause backtracking or semantic side effects | bounded side-effect-free probes and explicit commit points only |
| direct AST differs in invisible metadata | canonical comparator covers types, bindings, scopes, captures, spans, constants, and index ownership |
| fake/synthetic `TSNode` leaks into production | parser-neutral span migration is a Phase 2 prerequisite; no synthetic Tree-sitter nodes |
| diagnostics regress | structured expected-token/range model plus existing error-test gate |
| REPL continuation regresses | first-class `INCOMPLETE` status and dedicated multiline fixtures |
| parser is small only because Phase 1 omits expensive work | Phase 2 repeats linked-size and end-to-end timing gates including the AST sink |
| recursion overflow or malformed UTF-8 loop | explicit recursion budget, progress invariant, overflow-checked offsets, fuzzing |
| two permanent AST builders emerge | share extracted constructors during comparison; delete CST adapters after switch |
| editor/tooling loses incremental parsing | retain Tree-sitter grammar/bindings for editor/reference use; replace only runtime Lambda parsing |

## 10. Documentation and formal-spec updates

The normal production switch is now landed. The formal and working records were
updated together:

1. `doc/Lambda_Formal_Design.md`: D8.1.1v3, D8.1.2v2, semver, implementation status.
2. `doc/dev/lambda/LR_01_Compilation_Pipeline.md`: production source→AST pipeline and explicit reference selector.
3. `doc/dev/lambda/LR_02_Parsing_AST.md`: C lexer/Pratt/RD parser and direct AST sink, with the reference CST adapter retained.
4. `vibe/Lambda_Grammar_Reduce5.md`: grammar seams remain the Tree-sitter oracle; the C parser is the normal runtime front end.
5. `vibe/Lambda_Repl.md`: explicitly records its transitional retained Tree-sitter fragment transaction.
6. Build/developer documentation: identifies `grammar-lambda.js` as reference/editor grammar and the C files as the production Lambda parser; generated Tree-sitter files remain never-hand-edited.

Language-reference documents do not need a syntax rewrite because the accepted language is unchanged. S15.3 does not need a semantic revision unless the canonical `lm.` AST contract changes, which this design forbids.

## 11. Decision ledger

| ID | Proposed decision | Status |
|---|---|---|
| **CGP1** | Production target is a first-party C hybrid recursive-descent + Pratt parser. | landed for normal files/modules |
| **CGP2** | The final runtime parser builds the existing typed AST directly and retains no replacement syntax tree. | landed for normal files/modules |
| **CGP3** | Phase 1 implements the complete current language before size/performance can authorize Phase 2. | landed |
| **CGP4** | Phase 1 does not switch production or construct the final AST. | historical phase rule |
| **CGP5** | `grammar-lambda.js` remains the structural syntax oracle and editor/bindings grammar. | landed |
| **CGP6** | Expressions use one Pratt operator table; declarations/statements/delimited forms use recursive descent. | landed |
| **CGP7** | Ambiguity uses bounded, allocation-free lookahead, not general backtracking or GLR. | landed |
| **CGP8** | Source ranges become parser-neutral before direct AST integration; synthetic `TSNode` is forbidden. | landed for direct AST |
| **CGP9** | Existing type/path parser logic and AST constructors are shared, not duplicated. The C parser supplies only committed source spans at these seams. | landed |
| **CGP10** | Phase 1's material-size gate is at least 50% smaller than the current 818,048-byte parser/scanner archive. | proposed |
| **CGP11** | Comparable Phase 1 performance means aggregate ≤1.10× and p95 ≤1.20× Tree-sitter in release mode. | proposed |
| **CGP12** | Phase 2 requires canonical AST, diagnostic, execution, baseline, size, and end-to-end timing parity. | follow-up for reference-path removal |
| **CGP13** | Tree-sitter may remain for editor/reference tooling and other languages, but leaves the production Lambda parse path after the switch gate. | normal path switched; archive retained for REPL/tools |
| **CGP14** | REPL completeness becomes an explicit parser result, not a recovered-tree inspection. | follow-up |
| **CGP15** | The formal D8.1 rulings are revised only with the successful Phase 2 production switch. | landed: D8.1.1v3/D8.1.2v2 |
| **CGP16** | A failed size, correctness, or performance gate stops the migration without weakening the language or hard-coding corpus cases. | landed |
| **CGP17** | Phase 2 adapts existing type/path parsers from `TSNode` diagnostics to parser-neutral spans; synthetic `TSNode` and a copied C type/path grammar are forbidden. | landed |

## 12. Immediate next action

The C lexer, RD/Pratt parser, direct AST sink, parser-neutral spans, and shared
type/path seams are checked in under `lambda/runtime/parser/`,
`lambda/runtime/build_ast.cpp`, and `lambda/runtime/ast_build.hpp`. The normal
runner now defaults to the C parser and has explicit Tree-sitter rollback and
syntax-compare modes. Focused POC and direct-vs-reference execution probes are
green, including the mutable/index assignment and VMap method regressions that
closed the final cutover gaps. Remaining work is deliberately outside this
cutover: migrate the REPL append-only fragment transaction and legacy AST/Jube
inspection tools, then measure canonical AST and release timing parity before
removing the reference Lambda Tree-sitter archive.
