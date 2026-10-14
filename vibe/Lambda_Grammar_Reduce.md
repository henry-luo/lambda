# Lambda Grammar Reduction: Techniques and Measured Results

- **Status:** historical engineering reference; consolidated 2026-10-14 from `Lambda_Grammar_Reduce1.md`–`Lambda_Grammar_Reduce5.md`.
- **Scope:** how to reduce a generated Tree-sitter parser, what worked, what failed, and the measurements behind those conclusions. Implementation plans and code-level migration details are omitted.
- **Formal linkage:** [D8.1.1v18 and D8.1.2v3](../doc/Lambda_Formal_Design.md#d81-structure) govern the current C-parser pipeline and Tree-sitter's reference role. Historical syntax choices below do not override the [formal semantics](../doc/Lambda_Formal_Semantics.md).
- **Related:** [C parser design](Lambda_Grammar_Parser.md), [type-pattern design](Lambda_Type_Pattern.md), [documentation authority](../doc/Doc_Convention.md).

Lambda now uses its first-party C lexer and recursive-descent/Pratt parser in
production. Tree-sitter remains a structural reference and verifier; its parser
size is no longer a production optimization target. The useful legacy of these
campaigns is the method: measure table structure, remove unnecessary distinctions,
and preserve language and AST behavior while changing the grammar representation.

All numbers below are historical observations, not fresh benchmarks. The grammar
evolved between campaigns, so their reductions must not be added into one total.
Exact before/after pairs take precedence over the old rounded summaries. Source
lines, generated-C bytes, object bytes, and archive bytes are different metrics;
none establishes a parsing-speed improvement by itself.

## 1. Understand the size driver

The generated parse tables, rather than the lexer or handwritten grammar length,
dominated Lambda's Tree-sitter parser:

| Historical snapshot | Dense parse table | Sparse parse table | Combined share of generated source |
|---|---:|---:|---:|
| 340,587-line parser | 208,906 lines | 107,827 lines | 93.0% |
| 349,123-line parser | 186,400 lines | 135,924 lines | 92.3% |

At the first snapshot the main lexer occupied only **0.5%** of the source. Its
dense table had **2,433 × 244 = 593,652 cells**, only **26.6%** populated.
Moving an intricate literal regex into a scanner therefore offered little benefit
unless it also removed parser symbols, states, or recursive grammar structure.

The main levers are:

- **Fewer states:** remove duplicated expression families, unnecessary ambiguity,
  and recursive sublanguages that need not participate in the outer parser.
- **Fewer symbols:** consolidate terminals with the same grammar role and remove
  redundant wrappers. A hidden rule can still contribute a symbol.
- **Different table representation:** fewer distinct actions can move states from
  dense to sparse tables. This threshold effect makes gains nonlinear.

Dense-table storage is approximately `large states × symbols × entry size`, plus
the separate sparse tables and action data. Grammar rule count alone is a poor
predictor: fewer rules can yield more states, and more hidden rules can enable
substantial state sharing.

## 2. What worked

### Share structural grammar and remove avoidable ambiguity

Type patterns and string/symbol patterns originally duplicated grouping, ranges,
set operators, and occurrence modifiers. Sharing their structural rules removed
parallel parser paths; context-specific validity could be checked after parsing.
The follow-up record reports **860K → 361K generated lines**, about **58% less**,
and roughly **23 MB → 11 MB** of source. This is a reported post-unification
checkpoint, not a controlled attribution of every saved byte. The original
proposal had predicted only 7–10%.

Sharing syntax does not make two domains semantically identical. In particular,
today's string/symbol islands have their own restrictions under **S11.1.2v4**;
the historical shared-operator inventory is not the current language definition.

Another successful change removed `expr_no_pipe` and `binary_expr_no_pipe`,
which duplicated the expression hierarchy to keep an infix `where` from consuming
a loop's `where` clause. Giving those roles distinct spellings enabled **359K →
330K lines**, about **8% less**, with **223/223** baseline tests passing. At that
time the infix filter was renamed `that`; filtering is now `|:` (**S10.1.6**),
while `that` is a single-value proviso (**S10.1.5v3**).

The reusable lesson is to resolve a genuine syntactic distinction once, rather
than carry a nearly identical expression grammar through every affected context.
A language change needs its own justification and ruling; parser size alone is
no longer a reason to change Lambda syntax.

### Control inlining, repetition, and recursive grammar layers

An early combined experiment made three changes: stopped inlining five
high-alternative rules, represented optional loop clauses as a repeated clause
choice, and layered the type grammar to localize conflicts.

| Metric | Before | After | Reduction |
|---|---:|---:|---:|
| Generated source | 10.5 MB | 4.2 MB | about 60% |
| Generated lines | 340,587 | 141,978 | 58.3% |
| Parser object | 1.6 MB | 676 KB | about 58% |
| States | 6,348 | 2,629 | 58.6% |
| Large states | 2,433 | 826 | 66.1% |
| Symbols | 244 | 252 | increased by 8 |
| Tokens | 124 | 124 | unchanged |
| Declared conflicts | 6 | 4 | three-way conflict removed |

**223/223** baseline tests passed. This measured the bundle; it does not establish
an individual percentage for any of its three changes. The extra symbols were
more than offset by the state reduction.

Inlining has no universal best setting. Expanding a broad choice at many use
sites can multiply states; inlining a trivial alias can instead remove expensive
indirection. Similarly, repeated clauses can share transitions, but any required
clause order or uniqueness must still be enforced. Counting optional clauses as
`2^n` is a hypothesis about combinations, not a formula for generated LR states.

### Consolidate tokens by grammar role

The strongest isolated lexical change combined **20 base-type keywords** into
one terminal. Four words also used in other grammar roles stayed separate.
Generated source fell **20.2%**; symbols fell **256 → 237**, tokens **124 → 104**,
and large states **2,052 → 1,482**, while total states barely changed
(**6,265 → 6,264**). Both narrower dense rows and migration to sparse rows mattered.

The same principle worked for named constants, wildcard variants, and redundant
token wrappers. Let the parser recognize one syntactic category, then distinguish
its members from their source text. Audit all uses first: shared operators and
keywords can require a common terminal identity across grammar contexts.

For delimited literals, a single **envelope token** also worked: capture the
complete datetime or binary literal and let the existing semantic parser validate
and decode its contents. This removes exposed internal symbols and productions;
merely moving equivalent lexer code elsewhere would not give the same result.
Malformed contents must continue to produce diagnostics.

The later lexical/structural campaign recorded these isolated results. All sizes
are generated-C bytes; percentages use each row's own starting size.

| Tuning | Before | After | Bytes saved | Reduction |
|---|---:|---:|---:|---:|
| Flatten type layers | 10,608,627 | 10,607,520 | 1,107 | 0.01% |
| Consolidate base-type keywords | 10,631,709 | 8,482,463 | 2,149,246 | 20.22% |
| Merge match-arm/default forms | 8,482,463 | 8,480,465 | 1,998 | 0.02% |
| Merge direct/recursive query forms | 8,480,465 | 8,399,363 | 81,102 | 0.96% |
| Share type/string/symbol declaration structure | 8,399,363 | 8,375,586 | 23,777 | 0.28% |
| Combine `true`/`false`/`inf`/`nan` tokens | 8,375,586 | 8,232,241 | 143,345 | 1.71% |
| Capture datetime as one envelope | 8,232,241 | 7,976,424 | 255,817 | 3.11% |
| Capture binary as one envelope | 7,976,424 | 7,558,156 | 418,268 | 5.24% |
| Combine wildcard character-class atoms | 7,558,156 | 7,525,508 | 32,648 | 0.43% |
| Reuse raise-expression structure in statements | 7,525,508 | 7,520,133 | 5,375 | 0.07% |
| Consolidate path-prefix tokens | 7,520,133 | 7,312,649 | 207,484 | 2.76% |
| Remove redundant named `import` wrapper | 7,312,649 | 7,272,412 | 40,237 | 0.55% |
| Combine path wildcard tokens | 7,272,412 | 7,250,003 | 22,409 | 0.31% |
| Share spread/unary-expression structure | 7,250,003 | 7,162,013 | 87,990 | 1.21% |
| Reuse integer token for numeric member keys | 7,162,013 | 7,001,274 | 160,739 | 2.24% |

The flattening row has a separate baseline; the old cumulative summary mixed
nearby snapshots. The contiguous sequence beginning with keyword consolidation
is **10,631,709 → 7,001,274 bytes (34.1% less)**. The final parser had **5,643
states, 516 large states, 215 symbols, and 96 tokens**. Early rows preserved the
recorded pre-existing failure sets; the declaration merge and subsequent rows
reported **605/605** baseline tests passing.

Two useful details explain why superficial complexity is misleading:

- Binary-envelope capture moved large states **1,469 → 598**, with symbols
  **227 → 222**. The size gain cannot be explained by fewer lexer lines alone.
- A single visible named-constant token saved about **143 KB**; a hidden token
  plus visible wrapper saved only about **35 KB**. An extra wrapper can erase
  much of a token-consolidation benefit.

### Extract bounded sublanguages

The 2026-08-19 campaign moved type patterns, string/symbol islands, restricted
view/return forms, and static path bodies behind external-token boundaries. An
end-finder selected the source extent; a Lambda-side parser handled the interior.
This removed recursive subgrammar structure from the outer LR tables.

First, hoisting constraint expressions outside the pure-pattern portion broke
type/expression mutual recursion. It saved **8.3% of source**, **7.5% of object
size**, and **9.1% of states**. Splitting the grammar files afterward was a
measured no-op; organization alone produced no size gain.

The full campaign milestones follow. These are historical stage labels, not an
active implementation checklist. Object sizes exclude the scanner.

| Stage | Source bytes | Object bytes | States | Large states | Symbols | Declared conflicts |
|---|---:|---:|---:|---:|---:|---:|
| SC0: baseline | 8,913,442 | 1,311,368 | 6,401 | 1,055 | 254 | 6 |
| SC1: constraint hoist | 8,176,888 | 1,213,240 | 5,818 | 1,004 | 255 | 2 |
| SC2: grammar-file split | 8,176,888 | 1,213,240 | 5,818 | 1,004 | 255 | 2 |
| SC4: type/island extraction active | 7,360,111 | 1,033,408 | 5,032 | 903 | 225 | 2 |
| Whole return/view forms | 7,318,818 | 1,015,848 | 5,003 | 903 | 219 | 2 |
| Path-body extraction | 6,998,990 | 968,768 | 4,881 | 818 | 221 | 1 |
| Ordinary-call `start` | 6,850,715 | 950,336 | 4,851 | 804 | 219 | 1 |
| Navigation/member unification | 6,796,457 | 941,368 | 4,842 | 784 | 218 | 1 |
| Maximal namespace names | 6,852,463 | 949,840 | 4,862 | 798 | 218 | 1 |
| Explicit query postfix precedence | 5,715,314 | 800,432 | 4,124 | 699 | 218 | 0 |

Against SC0, type/island activation alone saved **17.4% source**, **21.2% object**,
and **21.4% states**. The complete campaign saved **35.9% source**, **39.0% object**,
and **35.6% states**. Its final scanner was **17,072 bytes** and parser-plus-scanner
archive **818,048 bytes**. Compared with the same-build fresh SC4 archive of
**1,044,432 bytes**, the final archive saved **21.7%**, including scanner growth.

The last precedence fix alone saved **1,137,149 source bytes**, **149,408 object
bytes**, **738 states**, and **99 large states**. Query already belonged on the
postfix/member tier (**S7.6.3v2–S7.6.4**); expressing that association explicitly
removed an unnecessary GLR fork. Named precedences needed an explicit ordering
against the competing expression rule, not just placement in a general list.

Two other changes illustrate the distinction between language design and size
tuning. Ordinary-call `start` reused argument/options syntax (**S13.1.1v2**).
Maximal namespace names followed **S2.4.3v3** and removed a scanner workaround,
but **increased** source by 56,006 bytes and object size by 8,472 bytes. Correct
language structure and simpler ownership can justify a measured size increase.

## 3. What failed, or gave little return

| Experiment | Recorded result | Lesson |
|---|---|---|
| Replace restricted attribute expressions with full expressions | About **10% larger**, to 11.6 MB; 3 new GLR conflicts and 330+ extra large states | Post-parse validation cannot undo ambiguity the parser must first carry between element delimiters and comparisons. |
| Group primary alternatives under hidden rules | One version failed generation; retry **+88 KB (+0.83%)** | Rearranging alternatives need not change token lookahead; extra symbols can widen tables. |
| Inline content/statement expression rules in the later grammar | About **0.7% larger** | Inlining broad choices can duplicate states even when it removes symbols. |
| Flatten the later type hierarchy | Only **1,107 bytes saved**; states **+73**, large states **−89**, symbols **−6** | Cleaner grammar is not automatically smaller; the predicted 10–15% gain did not materialize. |
| Move ranges to another grammar tier | Same conflict at another rule | Relocating a conflict is not resolving it. |
| Merge match forms in the first attempt | About **0.7% larger**; later narrower merge saved only **0.02%** | Similar-looking prefixes do not guarantee useful state sharing. |
| Reuse raise structure | **0.07% smaller**, same conflict count | Removing source duplication may merely rename the remaining ambiguity. |
| Wrap shared unary operators in a new combined `token()` | **36 test failures** | Preserve terminal sharing with binary/operator contexts; the structural merge without the token wrapper worked. |
| Scan a complete path including its ambiguous introducer | Accepted unintended bare prefixes and `.123`; collided with member access | Leave the disambiguating introducer in the outer grammar and extract the bounded body. Historical spellings are not current path rules. |
| Externalize base-type vocabulary indiscriminately | Lexical collisions with broader statement candidates | A smaller size-only prototype is not a correct parser. |

Early isolated un-inlining probes were also small or adverse: `_statement`
**+400 lines**, `_content_expr` **−162**, `_attr_expr` **−922**, `_expr_stam`
**no change**, and `_non_null_literal` **−922** (only **−833** extra when combined
with token consolidation). The early keyword-token probe saved **23K lines
(6.3%)**, with large states **2,586 → 801** and symbols **255 → 234**. These
different outcomes from later campaigns are evidence to remeasure combinations,
not universal rankings of the same edits.

A size-only return/path/base-type/whole-view prototype promised **6.0% less source**
and **7.6% less archive size** versus its SC4 baseline, but its broad scanner
accepted the wrong language. Those figures describe opportunity, not a landed
result. Wider expression/statement unification, further loop-rule merging, and
element-delimiter redesign had proposals but no validated result in these records.

## 4. Scanner lessons worth retaining

External extraction works best for a **bounded, self-contained sublanguage**.
It is not a general replacement for the outer parser's context.

- **Separate extent-finding from meaning.** Keep scanning free of runtime
  allocation and semantic side effects; build and validate the interior afterward.
  Speculative scanning and error recovery make scanner-owned semantic objects or
  span-keyed side tables difficult to manage correctly.
- **Respect each surrounding form.** A full type, a query operand, a return
  contract, and a view pattern can have different stopping rules. Do not let a
  broad scanner swallow an enclosing body, parameter list, or expression operator.
- **Balanced nesting needs real nesting logic.** A regular token regex cannot
  express arbitrary balanced groups. Bounded unrolling creates a hidden depth
  limit; a tiny recursive blob grammar can still lose significant separators to
  whitespace handling. Quoting, escapes, comments, and incomplete input matter.
- **Parser context can exceed scanner context.** Historical newline behavior
  differed between unfinished annotations and complete declarations; the scanner
  widened acceptance while trying to preserve both. Treat such drift explicitly.
  Current line-break behavior is governed by **S16.1.1**, not that old heuristic.
- **Preserve the whole AST contract.** Equivalent type data alone was insufficient:
  matching, literal emission, element-tag checks, and delayed pattern compilation
  depended on node kinds and children. Audit tooling and other CST consumers too.
- **Inline trivial boundary aliases when measurement supports it.** Leaving three
  seam aliases as ordinary hidden rules cost roughly **850 large states** and
  **254 KB of parser object** in this campaign—the reverse of the broad-rule
  un-inlining result.
- **Do not predeclare unused externals.** A dormant three-token experiment moved
  large states **1,004 → 1,854** and added **254 KB** to the object. Land each
  scanner token with its grammar use and interior parser, then remove dead tokens.
- **Count the entire replacement.** A smaller generated parser can conceal growth
  in the scanner or interior parser. Source-span equality and negative tests are
  essential checks on whether the extracted token consumed the right text.

## 5. Generator upgrades, compiler flags, and compression

The final 2026-08-19 investigation compared pinned Tree-sitter **0.24.7** with
**0.26.11** on the same grammar and host/compiler flags. The latter was the version
tested then; this is not a claim about the latest version today.

| Generator / ABI / state merging | Source bytes | Object bytes | Object with `-Oz` | States / large states |
|---|---:|---:|---:|---:|
| 0.24.7 / ABI 14 / enabled | 5,715,314 | 800,432 | 776,872 | 4,124 / 699 |
| 0.26.11 / ABI 14 / enabled | 5,720,937 | 800,704 | 777,000 | 4,124 / 699 |
| 0.26.11 / ABI 15 / enabled | 5,721,141 | 808,984 | 785,288 | 4,124 / 699 |
| 0.26.11 / ABI 14 / disabled | 25,381,679 | 3,321,000 | not measured | 17,810 / 3,032 |

The version upgrade saved nothing: ABI 14 added **272 bytes (0.03%)** and ABI 15
added **8,552 bytes (1.07%)** to the default object. Both newer parsers matched
the pinned parser's exact observed CSTs on **921/921 files**. Disabling compatible
state merging grew the object **314.9%**; the stock optimization was already
doing substantial work.

In the size-optimized object, dense/sparse tables, the sparse-state map, and parse
actions occupied about **704,426 bytes**, **92.6%** of live sections. Compiler
flags could shrink code but could not remove those grammar-dependent constants.
`-Oz` reduced the complete parser/scanner archive **818,048 → 788,248 bytes
(3.64%)** and the stripped test dylib **843,112 → 810,096 bytes (3.92%)**.
Other tested optimization/section/stripping options gave no material advantage.

The parser object gzipped to **79,209 bytes**, demonstrating disk redundancy,
not an executable representation. Custom table decompression would add startup
work, writable storage, synchronization, and a nonstandard runtime boundary,
while losing read-only file-backed sharing. Packaging compression captures much
of the transfer benefit already. No custom compressed-table runtime was adopted.

Replacing LR tables with a recursive-descent/Pratt parser was the architectural
exit once these levers were exhausted; its separate design and measurements live
in [Lambda_Grammar_Parser.md](Lambda_Grammar_Parser.md). Tree-sitter's incremental
parsing and recovery remain capabilities to account for when making that choice.

## 6. A repeatable tuning method

1. **Pin the baseline.** Record grammar revision, generator/ABI, compiler flags,
   generated artifact actually built, source/object/archive sizes, states, large
   states, symbols, tokens, and declared conflicts. Use fresh generated output.
2. **Locate the dominant storage.** Inspect dense/sparse tables and object sections
   before optimizing lexer complexity or handwritten grammar length.
3. **Change one cause at a time.** Prioritize duplicated structure, same-role token
   consolidation, and missing precedence. Measure combinations separately; do not
   attribute a bundle's result to one edit or sum percentages across baselines.
4. **Verify behavior beyond acceptance.** Compare accepted and rejected inputs,
   token/source spans, precedence, diagnostics, AST shape, and execution results.
   Include schemas and non-compiler CST consumers. A historical structural
   self-check reached **1,405 annotations with zero disagreements**, but the first
   activation still exposed AST-shape dependencies.
5. **State evidence limits.** The **921-file** check tested generator-version CST
   parity; it did not complete the separate full-grammar/extracted-token span and
   AST differential coverage, which remained outstanding in the campaign record.
   Preserve named baseline failure sets rather than interpreting a changed test
   count as proof of improvement.
6. **Keep the useful tradeoff.** Retain clear grammar simplifications even when
   their size benefit is negligible; reject semantic regressions even when smaller.
   Production performance measurements require a release build. Regenerate
   reference artifacts from grammar sources and leave the vendored runtime alone.

For current Lambda, any parser disagreement is reviewed against the formal syntax
rulings and working design records (**D8.1.2v3**). Neither Tree-sitter nor the C
parser is an unquestioned acceptance oracle. These historical measurements justify
techniques to investigate; they create no new size target or pending tuning work.
