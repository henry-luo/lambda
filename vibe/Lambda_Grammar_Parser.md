# Lambda Grammar Parser: Hybrid Recursive-Descent + Pratt Design

- **Date:** 2026-08-19
- **Status:** **PHASE 1 IN PROGRESS; P2.1 COMPLETE FOR THE CURRENT CST PATH; P2.2 CONSTRUCTOR EXTRACTION IN PROGRESS.** The lexer and complete recursive-descent/Pratt recognizer POC are implemented. The valid-source side of P1.3 is green; invalid-source classification and final linked-size gates remain open. At user direction, P2.2 may proceed in parallel, but P1.6 and P2.6 still forbid a production-parser switch.
- **Formal authority:** **D8.1.1v2** (current Tree-sitter grammar → typed AST pipeline and retained AST as runtime source of truth), **D8.1.2** (generated grammar discipline), **D8.2.1–D8.2.5** (one core AST, no tree rewriting, indexed compilation unit, typed pass schedule), and **D4.1.1v2** (AST/const-pool ownership). Phase 1 is an implementation POC under those current rulings. A successful Phase 2 switch requires revising D8.1.1v2 and D8.1.2 in place, with the formal-design semver bump required by the repository convention.
- **Surface-syntax authority:** `lambda/tree-sitter-lambda/grammar-lambda.js` remains the complete structural reference grammar; `grammar-common.js` plus the production replacement layer in `grammar.js` describes the currently shipped parser seams. S2.4.3v2 governs greedy namespace-qualified names, and S7.6.3v2 governs query at the postfix/member tier.
- **Related:** `vibe/Lambda_Grammar_Reduce5.md` (current grammar reductions and latest-Tree-sitter size POC), `doc/dev/lambda/LR_02_Parsing_AST.md` (current Tree-sitter/CST front end), `lambda/runtime/parse.c`, `lambda/runtime/build_ast.cpp`, `lambda/runtime/ast-core.hpp`, `lambda/runtime/parse_type_pattern.cpp`, and `lambda/runtime/parse_path_expr.cpp`.
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
- Phase 1 does not switch the runtime, construct production AST, or alter D8.1.1v2.
- The POC does not replace Tree-sitter for JavaScript, TypeScript, Python, Ruby, Bash, LaTeX, or other guest/input grammars.
- The runtime parser does not need Tree-sitter's incremental-edit API. Tree-sitter may remain the editor-facing grammar.
- The parser will not silently accept a simpler language to win size.
- The parser will not introduce a fifth allocator mechanism; parser scratch state is bounded stack state or existing pool/arena-owned state under D4.1.1v2/D4.1.4v4.
- The frozen C2MIR path receives no special support.

## 4. Current pipeline and compatibility boundary

The shipped Lambda path is:

```text
source
  → lambda/runtime/parse.c
  → Tree-sitter runtime + generated parser.c + scanner.c
  → TSTree / TSNode CST
  → lambda/runtime/build_ast.cpp
  → typed AstNode graph + scopes + bindings + captures
  → AST index / analysis / T0 interpreter / MIR Direct
```

`lambda_parse_source()` itself is thin, but Tree-sitter is embedded deeply in the next layer:

- `AstNode` currently stores a `TSNode` in `ast-core.hpp`;
- `build_ast.cpp` accepts and traverses `TSNode` throughout;
- literal parsing and source extraction call `ts_node_start_byte`, `ts_node_end_byte`, and `ts_node_symbol`;
- structured errors consume Tree-sitter `ERROR` and `MISSING` nodes;
- MIR naming and diagnostics still read source positions from retained AST `TSNode`s;
- the REPL uses `MISSING` nodes to distinguish incomplete input from invalid input;
- import discovery walks import CST nodes before parallel compilation;
- the validator, AST dump/s-expression tools, Jube declaration parser, and metaprogramming paths call the Lambda Tree-sitter wrapper.

The current `build_ast.cpp` also combines several logically distinct jobs: CST traversal, AST allocation, local type construction, name resolution, scope/capture bookkeeping, semantic validation, and diagnostic creation. A direct parser cannot safely pretend a byte range is a `TSNode`. Phase 2 must first make the retained AST's source contract parser-neutral and then extract reusable AST constructors from CST traversal.

## 5. Target architecture

```text
                                      Phase 1                 Phase 2
source → C lexer → RD + Pratt parser → validation/hash sink   direct AST sink
                         │                                      │
                         ├→ structured parse diagnostics        ├→ AstNode pool
                         └→ complete/incomplete/error            ├→ scope/binding/type helpers
                                                                └→ existing compiler passes
```

There is one parser core and a small output-sink interface:

- the **Phase 1 sink** validates reduction order and computes a stable structural fingerprint without retaining a tree;
- the **Phase 2 sink** returns `AstNode*`-backed values and invokes the shared production constructors;
- an error or abandoned lookahead never publishes semantic side effects.

The sink is not an intermediate representation. Each completed production is reduced immediately, and child values are released from the parser stack once the parent reduction returns. No syntax-node graph survives parsing.

### 5.1 Source and parser API

The parser core has a C ABI and uses no `std::` types:

```c
typedef struct LambdaSourceSpan {
    uint32_t start_byte;
    uint32_t end_byte;
} LambdaSourceSpan;

typedef enum LambdaParseStatus {
    LAMBDA_PARSE_OK,
    LAMBDA_PARSE_INCOMPLETE,
    LAMBDA_PARSE_ERROR,
} LambdaParseStatus;

typedef struct LambdaParseError {
    LambdaSourceSpan span;
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

Today those helpers take `Transpiler*` and `TSNode` because they allocate existing AST/type/path objects and attach diagnostics. P1 keeps the corresponding C reductions as source-span seams, without attempting to synthesize a `TSNode`. P2.1 replaces that diagnostic/location dependency with `LambdaSourceSpan`; P2.2 then adapts the existing helpers and constructors for the direct-AST sink. This is a prerequisite for reuse, not permission to copy their grammar into `lambda_parser.c`.

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

The checked-in runner is `bash utils/lambda_parser_perf.sh`. It regenerates the
frozen manifest outside the timed interval, preloads every source into memory,
performs one warm-up, then alternates the first parser across five measured
runs. It links the shipped Lambda Tree-sitter parser/scanner archive and
builds the C POC at the production release front-end settings (`-O3`,
`-DNDEBUG`, `-march=native`). Its results are a Phase 1 syntax-front-end
measurement only: the present C POC constructs reduction hashes rather than a
compatible `AstNode`, while Tree-sitter constructs and frees its CST. The
Phase 2 end-to-end measurement remains the decision-quality comparison.

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
test/lambda_parser_poc_perf.c
utils/lambda_parser_manifest.sh
utils/lambda_parser_perf.sh
temp/lambda-parser-poc/             # generated manifests and measurements
```

File boundaries may be consolidated if that makes the C parser smaller or clearer. Build integration goes through `build_lambda_config.json` and the normal generated build path; generated Lua and Tree-sitter vendor sources are not edited.

## 7. Phase 2 — compatible direct AST and production switch

Phase 2 starts only after the P1.6 go gate is recorded with reproducible artifacts.

### P2.0 — revise the formal pipeline ruling

Before the production default changes:

- revise **D8.1.1v2 → D8.1.1v3** so the first-party Lambda parser, rather than Tree-sitter, is the production `source → typed AST` front end;
- revise **D8.1.2 → D8.1.2v2** to keep `grammar-lambda.js` and generated Tree-sitter artifacts as the syntax oracle/editor/bindings parser, while forbidding manual edits to generated output;
- bump the formal-design document semver;
- keep S15.3 behavior unchanged: `input(f, 'lambda')` still produces the canonical `lm.` AST.

The formal update lands with the switch, not with Phase 1.

### P2.1 — make retained AST source locations parser-neutral

Replace `AstNode::TSNode node` with a parser-neutral source reference, preferably the two-offset `LambdaSourceSpan`. Migrate all retained-AST consumers:

- source-text slicing and literal conversion;
- structured semantic diagnostics;
- MIR/function debug naming and source maps;
- AST dumps and formal-semantics emission;
- interpreter literal reads;
- type/path/pattern helpers that currently accept a `TSNode` only for location.

The existing Tree-sitter builder converts each `TSNode` to `LambdaSourceSpan` at AST allocation. This compatibility step must pass unchanged before the C AST sink is enabled. It proves downstream consumers no longer require a live `TSTree` and allows the current syntax tree to be deleted immediately after AST construction.

Because core AST nodes are shared under D8.2.1, the source reference is parser-neutral for all language profiles. Tree-sitter-based guest builders fill the same range from their `TSNode`; they do not force Tree-sitter storage back into core `AstNode`.

**P2.1 checkpoint (2026-08-20):** `LambdaSourceSpan` is now the common
half-open byte-range type in `lambda/runtime/source_span.h`; every `AstNode`
stores it, and the current CST allocator populates it from the source `TSNode`.
The AST allocator has a span entry point for the direct parser, and structured
semantic errors, literal decoding in T0/MIR, MIR source-name identity, and AST
dump source extraction read the retained span rather than `AstNode::node`.
The existing type-pattern and static-path parsers now have equivalent span
entry points; their CST APIs are compatibility adapters only.
`AstNode::node` remains a transitional, zeroed-for-direct-AST compatibility
field while the CST walker remains active; the type/path helpers retain only
legacy `TSNode` adapters. No direct-AST node is yet published through the
runtime path.

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

**P2.2 checkpoint (2026-08-20):** The reduction ABI now carries a committed
form and introducer/operator token in addition to child values. Pratt binary
and postfix reductions now publish the complete expression span, including
their left child; this fixes the prior recognizer-only span shape before an AST
sink can depend on it. The C parser does not re-lex that source in the sink.
`ast_build.hpp` now owns the common prefix/infix operator classification, and
the ordinary unary constructor accepts a parser-neutral span plus a committed
operand. The Tree-sitter unary walker calls this constructor, preserving its
existing type inference while proving the seam is live. Identifier resolution,
current-item/index/error atoms, and current-parent navigation now have the
same span-native constructors and their CST adapters delegate to them. A
primary-wrapper constructor is ready for grouped direct reductions, preserving
`AST_NODE_PRIMARY` as required by D8.2.2. Spread and type negation remain
dedicated node constructors because their retained node shapes are intentionally
distinct. This is still an early constructor family: literals, containers,
calls, declarations, and scope lifecycle require extraction before P2.3.

### P2.3 — implement the direct AST sink

Each completed parser production returns an opaque `LambdaParseValue`; under the AST sink that value identifies the already-built `AstNode` or a short-lived descriptor needed by a parent production. Reduction performs the work currently done after CST traversal:

```text
token spans + child values
  → shared AST constructor
  → AstNode* with identical fields/type/source span
  → parent reduction
```

The direct path must preserve wrapper nodes that downstream code observes, including `AST_NODE_PRIMARY`, rather than “cleaning up” the tree during migration. D8.2.2 forbids syntax migration through tree rewriting. Any later AST simplification is a separate design and baseline campaign.

After the root reduction, the same compiler pass manager builds the AST index and runs the existing analysis/planning/lowering schedule. The parser does not create a second pass schedule.

### P2.4 — dual-front-end differential mode

Temporarily compile both Lambda front ends behind a test-only selector:

```text
LAMBDA_PARSER=tree-sitter
LAMBDA_PARSER=c
LAMBDA_PARSER=compare
```

`compare` parses independent compiler contexts because AST pools, scopes, names, imports, and type objects are owned state. It compares canonical, pointer-free output rather than addresses.

Extend `--emit-ast-dump` or add a dedicated AST comparator so equality covers:

- every node kind and sibling/child position;
- source span and source spelling where semantically retained;
- operator and form flags;
- interned names by bytes/length;
- type/return/parameter contracts by normalized structure;
- declaration/use binding identity by canonical node path;
- scopes, captures, concurrency analysis, and AST index ownership;
- constants and path/type/pattern payloads.

The existing AST dump is a useful start but is not yet a complete structural serializer; missing fields must be added before it is the switchover oracle.

### P2.5 — migrate every Lambda parser consumer

The switch is incomplete until these paths stop requiring Lambda `TSTree`/`TSNode`:

| Consumer | Required replacement |
|---|---|
| normal runner/transpiler | call C parse→AST entry point |
| validator/schema compilation | same direct AST entry point and structured errors |
| REPL | use parser `COMPLETE` / `INCOMPLETE` / `ERROR` result |
| import discovery | parser callback/index of top-level import ranges; do not parse a second CST |
| AST dump and s-expression emission | consume direct AST and parser-neutral spans |
| Jube declaration inspection | dedicated declaration parse result or direct AST; no Lambda CST walk |
| `input(..., 'lambda')` / `compile(ast)` bridge | preserve the S15.3 canonical `lm.` AST contract |
| tests and CLI helpers | use the new public parser API; Tree-sitter remains only in explicit reference tests/bindings |

The reference Tree-sitter grammar and language bindings may remain built for editor/tool consumers. The production Lambda executable no longer links `libtree-sitter-lambda.a`; the shared Tree-sitter runtime remains if another compiled-in language needs it.

### P2.6 — Phase 2 acceptance and switch gate

All of the following are mandatory:

- Phase 1 corpus status remains 100% green.
- Canonical AST comparison is identical for every valid manifest source.
- Every existing syntax and semantic error test passes with stable codes and correct spans.
- `make test-lambda-baseline` passes 100%; validator, REPL, imports/parallel imports, AST dump, interpreter, MIR Direct, and metaprogramming Lambda-input gates pass.
- Execution outputs match under C and Tree-sitter front ends for the complete Lambda baseline.
- Existing fuzz corpus plus malformed delimiter/token sweeps produce no crash/hang.
- In release mode, `C parse→AST` is no worse than 1.10× `Tree-sitter parse + CST→AST` on median complete-corpus time, with the same one-warm-up/five-run discipline.
- Final linked production size, including the direct AST sink and source-span support, still achieves the Phase 1 material-size objective. Report the actual executable delta; do not assume the POC object saving survives integration.
- `git diff --check`, build/lint gates, and all generated-build rules pass.

After the gate passes, make the C parser the default and keep Tree-sitter compare mode for one transition interval. Then remove the Lambda Tree-sitter library from the production target and delete dead CST-specific builder code. If a release blocker appears, switching the test/build selector back to Tree-sitter is the rollback; the formal ruling is not marked implemented until the C default is green.

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

Creating this proposal changes no formal ruling. If Phase 2 switches the production parser, update together:

1. `doc/Lambda_Formal_Design.md`: D8.1.1v3, D8.1.2v2, semver, implementation footnotes/status.
2. `doc/dev/lambda/LR_01_Compilation_Pipeline.md`: production source→AST pipeline.
3. `doc/dev/lambda/LR_02_Parsing_AST.md`: rewrite the stale Tree-sitter/CST design around the C lexer, Pratt/RD parser, direct AST sink, source spans, and reference grammar.
4. `vibe/Lambda_Grammar_Reduce5.md`: close the size campaign with the Phase 1 measurements and eventual production/reference split.
5. `vibe/Lambda_Repl.md`: replace Tree-sitter `MISSING`-node completeness behavior with parser status.
6. Build/developer documentation: identify `grammar-lambda.js` as reference/editor grammar and the C files as the production Lambda parser; retain the rule that generated Tree-sitter files are never hand-edited.

Language-reference documents do not need a syntax rewrite because the accepted language is unchanged. S15.3 does not need a semantic revision unless the canonical `lm.` AST contract changes, which this design forbids.

## 11. Decision ledger

| ID | Proposed decision | Status |
|---|---|---|
| **CGP1** | Production target is a first-party C hybrid recursive-descent + Pratt parser. | proposed |
| **CGP2** | The final runtime parser builds the existing typed AST directly and retains no replacement syntax tree. | proposed |
| **CGP3** | Phase 1 implements the complete current language before size/performance can authorize Phase 2. | proposed |
| **CGP4** | Phase 1 does not switch production or construct the final AST. | proposed |
| **CGP5** | `grammar-lambda.js` remains the structural syntax oracle and editor/bindings grammar. | proposed |
| **CGP6** | Expressions use one Pratt operator table; declarations/statements/delimited forms use recursive descent. | proposed |
| **CGP7** | Ambiguity uses bounded, allocation-free lookahead, not general backtracking or GLR. | proposed |
| **CGP8** | Source ranges become parser-neutral before direct AST integration; synthetic `TSNode` is forbidden. | proposed |
| **CGP9** | Existing type/path parser logic and AST constructors are shared, not duplicated. The C parser supplies only committed source spans at these seams. | proposed |
| **CGP10** | Phase 1's material-size gate is at least 50% smaller than the current 818,048-byte parser/scanner archive. | proposed |
| **CGP11** | Comparable Phase 1 performance means aggregate ≤1.10× and p95 ≤1.20× Tree-sitter in release mode. | proposed |
| **CGP12** | Phase 2 requires canonical AST, diagnostic, execution, baseline, size, and end-to-end timing parity. | proposed |
| **CGP13** | Tree-sitter may remain for editor/reference tooling and other languages, but leaves the production Lambda parse path after the switch gate. | proposed |
| **CGP14** | REPL completeness becomes an explicit parser result, not a recovered-tree inspection. | proposed |
| **CGP15** | The formal D8.1 rulings are revised only with the successful Phase 2 production switch. | proposed |
| **CGP16** | A failed size, correctness, or performance gate stops the migration without weakening the language or hard-coding corpus cases. | proposed |
| **CGP17** | Phase 2 adapts existing type/path parsers from `TSNode` diagnostics to parser-neutral spans; synthetic `TSNode` and a copied C type/path grammar are forbidden. | proposed |

## 12. Immediate next action

The P1.1 lexer, P1.2 whole-language recognizer, manifest generator, and reproducible P1.3 valid-source differential are checked in under `lambda/runtime/parser/`, `test/`, and `utils/`; the production parser selector is unchanged. The P2.1 source-span migration is landed in parallel, including span-native type/path helpers. P2.2 has started with complete Pratt reduction ranges/form metadata plus shared unary/operator, identifier, and contextual-atom constructors used by the CST walker. Next, extract literals, containers/calls, declarations, and scope-lifecycle constructors before connecting the direct-AST sink; meanwhile classify and pin the eight Tree-sitter-error / C-accepted sources and finish the linked-size gate. Publish no production-switch conclusion until P1.6 and P2.6 are green.
