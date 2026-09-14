# Lambda Fuzz Testing — Harness and Conformance Design

**Date:** 2026-09-14

**Status:** **IMPLEMENTED** on 2026-09-14. The parser-reference contract in §3
is **DECIDED by user ruling on 2026-09-14** and ratified as **D8.1.2v3**.

**Formal linkage:** `doc/Lambda_Formal_Design.md` — **D1.6** (MIR Direct is the
only native backend), **D1.9** (malformed input must fail closed, never reach
undefined behavior), **D1.10** (invariants require executable enforcement),
**D2.5–D2.6** (native/nullable lanes and container representation),
**D3.3.1v2** (inference and representation do not change a type-error-free
result), **D4.4** (COW), **D5.3** (precise rooting), **D8.1.1v10** (explicit
T0/AUTO/eager-JIT modes), **D8.1.2v3** (parser/reference contract),
**D8.2.5v2** (front-end pass order), and **D8.6.3** (dynamic liveness oracles).
`doc/Lambda_Formal_Semantics.md` — **S1.6**, **S4–S14**, and **S16**.

**Scope:** Lambda source text through lexing, parsing, AST construction,
binding, validation, indexing/planning, T0 interpretation, MIR Direct
compilation, runtime execution, COW, and GC. Input-format parsers and Radiant
layout/render fuzzing are separate campaigns and are not folded into this
harness merely because they share `lambda.exe`.

**History:** Replaces the legacy “Lambda Script Fuzzy Testing Proposal” that
previously occupied this path. Its Tree-sitter-production pipeline, corpus
counts, syntax examples, reference-counting model, target names, and completion
claims are retained only in Git history.

## Implementation record

The maintained entry point is `test/fuzzy/lambda/run_fuzz.py`; the legacy
`test/test_fuzz.sh` and stale C++ generator/mutator were removed after a
repository-wide consumer check. The implementation is complete against the
acceptance criteria in §16:

| Responsibility | Implemented location | Verification |
|---|---|---|
| Seed roles/contracts and drivers | `corpus_manifest.tsv` | Every row records role, success/reject contract, `direct`/`run`, tiers, and determinism. |
| State-aware generation/mutation | `source_generator.py`, `source_mutator.py` | Scoped declarations, calls, closures, types, maps, query forms, valid/invalid contracts, and recorded operations. |
| Parser stability/differential | `lambda_parser_poc_diff.c`, `parser_differential.py`, `parser_review.tsv` | Repeats fail-fast/recovery parsing, retains structural-novel raw-byte inputs, uses grammar.js as first cut, and hash/status-ratchets reviewed differences. |
| Tier/lifetime matrix | `run_fuzz.py`, `lambda_process.py` | Explicit T0, eager JIT, AUTO hot scenario, forced GC, poison, and root witness preserve byte output. |
| Findings lifecycle | `run_fuzz.py` | Content/signature deduplication, bounded artifacts, replay, oracle-preserving reduction, and a non-automatic promotion review. |
| Feature/campaign ratchets | `feature_inventory.py`, `feature_inventory.lock`, `feature_campaigns.tsv`, `semantic_campaigns.tsv` | New production inventory items or unowned semantic campaigns fail **D1.10** review. |
| Automation | `Makefile`, `.github/workflows/fuzz-lambda*.yml` | PR smoke, ASan nightly, and scheduled/manual soak retain only `temp/lambda-fuzz/` artifacts. |

Current local gates:

```text
make fuzz-lambda-inventory
bash test/lambda_parser_diff.sh
python3 test/fuzzy/lambda/run_fuzz.py --verify-only --gc-stress --root-witness
make fuzz-lambda-asan duration=60
make fuzz-lambda-extended duration=3600
```

The first three commands are the deterministic implementation gates. The
ASan and extended commands are bounded campaigns; a finding is only promoted
after replay, minimization, and human root-cause review, per **D1.9**, **D1.10**,
and **D8.6.3**.

---

## 0. Executive decision

Lambda fuzz testing becomes a set of cooperating targets with explicit input
contracts and explicit oracles. It is not one shell loop in which every
non-crashing exit is called a pass.

The system has five independently useful layers:

1. a bounded parser target for arbitrary bytes with structural-feedback corpus retention;
2. grammar-guided source generation and C/Tree-sitter differential review;
3. front-end fuzzing through parse, build, bind, validation, index, inference,
   and execution planning;
4. subprocess-isolated semantic comparison across explicit T0, AUTO, and eager
   MIR Direct execution; and
5. sanitizer, forced-GC, COW, rooting, module, procedure, and concurrency
   campaigns.

The implementation order is deliberate. First make the existing harness tell
the truth. Then add coverage-guided parser fuzzing. Only after those oracles are
trusted should generated programs be used to accuse the interpreter, JIT, GC,
or optimizer of divergence.

```text
reviewed seeds + grammar.js + feature inventory
                    |
                    v
       source producer / structural mutator
                    |
          +---------+----------+
          |                    |
          v                    v
  Tree-sitter first cut   production C parser
          |                    |
          +---- disagreement --+--> manual review manifest
                    |
                    v
       front-end and execution classifiers
                    |
        +-----------+------------+
        |           |            |
        v           v            v
   tier diff    GC/sanitizer   scenario drivers
        |           |            |
        +-----------+------------+
                    |
                    v
      reproduce -> deduplicate -> minimize -> promote
```

## 1. Current-state audit

### 1.1 Superseded maintained-path audit

Before this implementation, the Make targets invoked the removed shell runner
`test/fuzzy/lambda/test_fuzzy.sh`. It performed three operations:

1. run the local `valid` and `edge_cases` corpora;
2. load small `*.ls` files recursively from `test/lambda` and `test/std`, then
   apply one of thirty shell-text mutations; and
3. run random sequences drawn from a fixed token array.

Every case is a fresh `lambda.exe` subprocess, normally with `--dry-run`.
Dry-run is a useful safety boundary because it exercises the ordinary compile
and runtime path while fabricating external I/O; it is not the cause of the
current compiler-coverage gap.

### 1.2 Superseded measured snapshot

The following is an audit snapshot from 2026-09-14, not a permanent corpus
promise:

| Observation | Snapshot | Consequence |
|---|---:|---|
| `corpus/valid` scripts accepted | 5 / 14 | Nine positive seeds no longer establish a valid baseline. |
| `corpus/edge_cases` scripts accepted | 2 / 12 | Ten supposed edge successes now fail before their intended behavior. |
| Small repository scripts loaded as mutation seeds | 1,235 | Negative, helper, WIP, fixture-dependent, and procedural sources are mixed without roles. |
| Procedural/concurrency/PDF scripts among those seeds | about 325 | They are launched without `lambda.exe run`, so their `main` bodies are not exercised. |
| Newly added `*.ls` sources since the runner/generator update | 656 | Language growth has substantially outrun the fixed token list and examples. |

Representative corpus drift includes arithmetic use of `^` after it became
error propagation, trailing semicolons forbidden by **S16**, obsolete adjacent
string-pattern forms, and files containing multiple values without current
separation syntax.

### 1.3 Root causes

#### False success classification

`run_test` treats exit zero as a pass, selected signal-derived exit codes as a
crash, exit 124 as a timeout, and almost every other nonzero result as a pass.
That is acceptable only for an explicitly arbitrary-invalid crash campaign. It
is invalid for a positive seed, a negative fixture with an expected diagnostic,
or a differential run. The same integer exit status cannot be interpreted
without the input contract.

The selected signal list is also incomplete as an oracle. Sanitizer failures,
uncaught fatal diagnostics, output mismatches, infrastructure errors, and
resource-limit terminations need their own classifications.

#### Execution-tier blindness

The runner does not set `LAMBDA_TIER`. Under **D8.1.1v10**, the default is AUTO
and a fresh process begins in T0. Many generated cases are too small or too
short-lived to promote, so a nominal “runtime fuzz” campaign can cover parsing,
front-end analysis, and T0 while never exercising eager whole-module MIR
Direct. `LAMBDA_TIER=jit` must be an explicit matrix row; AUTO remains a
separate tiering-policy target, not a substitute for eager JIT.

The existing `test/interp/tier_sweep.py` contains the same historical trap: its
variable named `jit_out` is produced by unsetting `LAMBDA_TIER`, which now means
AUTO. The common execution helper must be corrected before it is reused as a
fuzz oracle.

#### Wrong driver and unclassified seeds

Functional scripts, procedures, modules, helpers, and negative tests have
different entry contracts. Uniformly invoking `lambda.exe file.ls` silently
misses `main()` in `proc`, `conc`, and `pdf` tests. Recursively importing every
small script also gives frequently selected weight to sources that cannot run
alone.

The semantic corpus must therefore be manifest-driven. Repository discovery is
valuable, but discovery cannot infer intent from file size.

#### Dormant and obsolete generation

The active producer is shell token soup and line-oriented `sed` mutation. Its
token list omits current literals, operators, statements, view/edit constructs,
query clauses, path forms, type forms, and error constructs. Random invalid
text still has value for lexer robustness, but it spends little time past the
parser.

`test/fuzzy/lambda/grammar_gen.cpp` and `mutator.cpp` are not linked to a main
program or called by the maintained runner. They also encode old syntax and use
`std::` containers forbidden by the project convention. They should be retired
or replaced, not treated as latent infrastructure.

#### Weak triage

The current runner has no user-specified RNG seed, no mutation trace, no tier or
driver metadata, no saved stdout/stderr bundle, and no minimizer. Crash names
use second-resolution timestamps and a reused temporary filename, permitting
overwrites. A test that cannot be reproduced exactly is only a clue, not a
regression asset.

## 2. Goals and non-goals

### 2.1 Goals

- Make every reported outcome mean one thing: accepted success, expected
  rejection, unexpected rejection, unexpected acceptance, parser discrepancy,
  semantic mismatch, sanitizer violation, crash, timeout, resource exhaustion,
  or harness failure.
- Exercise the full production front end in its normative pass order under
  **D8.2.5v2**.
- Give both T0 and eager MIR Direct unavoidable coverage and test AUTO as a
  distinct promotion policy under **D8.1.1v10**.
- Continuously enforce the boxed/native equivalence required by
  **D3.3.1v2** and representation invisibility required by **S1.6**.
- Turn GC lifetime and COW defects into deterministic failures through
  **D4.4**, **D5.3**, and **D8.6.3** stress modes.
- Detect when new tokens, AST forms, system functions, or semantic families
  have no corresponding generator or seed category, satisfying **D1.10**.
- Preserve exact reproduction and provide oracle-preserving minimization.
- Promote each fixed finding into the ordinary regression suite.

### 2.2 Non-goals

- Fuzzing does not define Lambda syntax or semantics. Formal S#/D# rulings do.
- Tree-sitter is not restored to the production path.
- The C parser is not declared infallible merely because it is the shipped
  implementation.
- The fuzzer does not restore C2MIR; **D1.6** leaves MIR Direct as the only
  native backend.
- Random rejection volume and tests per second are not treated as coverage.
- Input-format, DOM, layout, rendering, guest-language, network, and filesystem
  fuzzing do not enter the core campaign without their own driver, fixture, and
  oracle.
- The first implementation does not force the whole runtime into an in-process
  persistent loop. Process isolation remains the safe default until reset and
  ownership audits prove otherwise.

## 3. Grammar reference and parser differential

### 3.1 Authority and responsibility

The parser relationship is governed by **D8.1.2v3**:

1. Formal syntax rulings, especially **S16**, are authoritative.
2. Current `vibe/` syntax decisions fill gaps below the formal specs.
3. `lambda/tree-sitter-lambda/grammar.js` is the best structural reference and
   the first-cut verifier for generated programs.
4. The first-party C parser is the final production implementation used by the
   shipped executable.
5. `grammar.js` is known to miss some corner cases, and the C parser may also
   contain defects. Neither side wins a disagreement automatically.

`grammar-lambda.js` is stale and is not an input to the new generator, manifest,
or hash. Generated Tree-sitter `parser.c` is rebuilt from `grammar.js` and is
never edited manually.

### 3.2 First-cut use of `grammar.js`

For valid-by-construction generation, the Tree-sitter result is the cheap first
cut:

- a Tree-sitter-accepted candidate proceeds to the C parser and front end;
- a Tree-sitter-rejected candidate may still proceed if it is a reviewed corner
  case or belongs to the deliberately-invalid campaign;
- a generator defect is recorded separately from a language rejection; and
- no candidate is called semantically valid until the production front end
  also builds, binds, validates, and plans it.

The generator may derive rule and token inventories from `grammar.js`, but it
must also consume the reviewed-corner-case inventory. Otherwise grammar-based
generation would permanently exclude the exact syntax the reference grammar
does not implement.

### 3.3 Differential classification

| Tree-sitter | C parser | Initial classification | Required action |
|---|---|---|---|
| accept | accept | candidate-valid syntax | Continue through the front end; agreement is not proof of semantics. |
| reject | reject | candidate-invalid syntax | Keep for robustness/recovery coverage when novel. |
| accept | reject | `parser-review` | Manually adjudicate against S#/D# and current vibe records. |
| reject | accept | `parser-review` | Manually adjudicate against S#/D# and current vibe records. |

Manual review produces one of four rulings:

- `grammar-bug`: repair `grammar.js`, regenerate normally, and add the fixture;
- `c-parser-bug`: repair the Lambda-owned C parser and add the fixture;
- `reviewed-reference-gap`: production behavior is correct, the Tree-sitter
  limitation is deliberately retained for now, and the fixture records it;
- `spec-gap`: stop adjudication and request a user ruling under
  `doc/Doc_Convention.md`; do not bless either implementation.

### 3.4 Review manifest and ratchet

Reviewed discrepancies live in a tracked manifest, proposed as
`test/fuzzy/lambda/parser_review.tsv`, with these fields:

```text
case_id  source_sha256  tree_status  c_status  ruling  formal_refs  fixture  issue
```

The source itself lives in a reviewed fixture directory, not inline in the TSV.
The manifest is content-addressed so changing a fixture forces re-review. A new
unreviewed discrepancy fails the differential gate and emits a complete review
artifact. An existing reviewed discrepancy is allowed only while its hash,
statuses, and cited ruling remain unchanged.

This is the **D1.10** ratchet: known gaps are explicit; new gaps cannot hide in
a growing count or a blanket allowlist.

## 4. Target architecture

### 4.1 Shared orchestration

One orchestrator owns seed selection, deterministic randomness, process
isolation, resource limits, classification, artifacts, and reporting. Target
adapters own only how an input is invoked and what result constitutes a bug.
This avoids copying timeout and process-group handling between parser, T0, JIT,
procedure, and module runners.

The existing safe process-group behavior in `test/interp/tier_sweep.py` should
be promoted into the shared executor rather than copied. The executor must kill
the full child group where supported, close inherited pipes, cap captured
output, and distinguish an engine timeout from a harness failure.

### 4.2 Target matrix

| Target | Isolation | Input contract | Primary oracle |
|---|---|---|---|
| Lexer | isolated helper process over C API | arbitrary bytes | Always advances or returns an error; spans remain in bounds; no sanitizer failure. |
| C parser | isolated helper process over C API | arbitrary bytes and structured source | Terminates within budgets; stable status, diagnostic, metrics, and structural hash; recovery publishes no partial sink. |
| Parser differential | isolated batch or process | grammar-guided and reviewed fixtures | Agreement or a reviewed discrepancy under §3. |
| Front end | subprocess initially | expected-valid and expected-invalid source | Expected phase and diagnostic class; no partial execution after failure. |
| Semantic tier differential | subprocess | deterministic, expected-valid source | Explicit T0 and eager JIT have identical exit class and byte output; T0 fallback is zero. |
| AUTO promotion | subprocess | hot functions/loops and mixed supported/unsupported graphs | Same result as explicit tiers; expected promotion/fallback counters and no invalid cached state. |
| GC/COW/rooting | subprocess, ASan variants | allocation- and mutation-heavy valid source | Stressed output exactly matches unstressed output; no sanitizer or root-witness failure. |
| Procedure/module/resource | subprocess with scenario fixture | manifest-declared entry contract | Correct driver, hermetic fixture, stable scenario invariant. |
| Concurrency | subprocess with repeat schedule seeds | deterministic assertions or invariant checks | No crash/deadlock/leak; cancellation, scope, and message invariants hold under **S13**. |

### 4.3 Why the parser is the only direct C-API target

`lambda_lexer_next`, `lambda_rd_parse_source`, and
`lambda_rd_parse_recovering` already expose bounded C APIs. The isolated
`lambda-cst` helper invokes them on raw bytes and feeds novel structural
signatures back into a temporary corpus; their metrics and structural hash
also provide cheap determinism checks.

The complete runtime owns global compiler, module, GC, cache, and execution
state. Reusing it inside one libFuzzer process before a reset audit would trade
throughput for order-dependent false findings and hidden leaks. End-to-end
targets therefore remain subprocess-isolated initially. A later forkserver is
acceptable if it preserves the same clean-state contract.

## 5. Corpus design

### 5.1 Separate contracts

The corpus is partitioned by what the test knows, not by where a source happened
to be found:

| Role | Required metadata | Permitted oracle |
|---|---|---|
| `functional-valid` | expected output, deterministic flag | Parse/front-end success and tier equality. |
| `procedural-valid` | expected output, `run` entry | Procedure execution and tier equality. |
| `syntax-invalid` | expected parse status/diagnostic | Rejection and recovery invariants. |
| `semantic-invalid` | expected failing phase/code | Stable failure without execution. |
| `module-root` | dependency fixture set and exports | Module graph/result invariant. |
| `helper-only` | parent scenarios | Never selected as a standalone semantic seed. |
| `nondeterministic` | property oracle | Excluded from byte-equality comparisons. |
| `resource` | dry-run/fixture policy | Hermetic resource invariant. |
| `parser-review` | §3 manifest row | Reviewed C/Tree-sitter disagreement. |

Repository tests are eligible semantic seeds only when they have the expected
result file and a known driver. This reuses the existing test contract instead
of assuming every small `*.ls` file is standalone. Negative directories and
helpers remain useful mutation material, but only within their declared
campaigns.

### 5.2 Seed quality

Small, feature-dense seeds are preferred over wholesale duplication of the
test tree. The long-term corpus is selected by:

- new lexer/parser edges or reduction forms;
- new AST node, validation, interpreter, MIR, runtime-helper, or GC coverage;
- a previously uncovered formal ruling or boundary class;
- a distinct failure signature; or
- a shorter source providing the same coverage as a larger seed.

Uniform random selection from 1,235 files is replaced by weighted selection
across feature and role buckets. A frequently represented feature cannot starve
a rare one merely because it has more files.

### 5.3 Feature inventory

A generated inventory compares fuzz support with these implementation surfaces:

- `LambdaTokenKind` in `lambda/runtime/parser/lambda_rd_parser.h`;
- `LambdaReductionKind` and `LambdaReductionForm` in the same header;
- `AstNodeType` and the AST-child tables;
- the system-function registry; and
- the reviewed semantic campaign map in §8.

Each row is `covered`, `unsupported-with-reason`, or `missing`. Adding a token,
reduction, AST form, or registered system function creates a failing `missing`
row until a seed/generator is added or an explicit exclusion is reviewed. This
is a structural coverage gate, not a claim that touching a token proves its
semantics.

## 6. Source generation and mutation

### 6.1 Two producers, never one ambiguous stream

The harness keeps two separate producers:

1. **Valid-by-construction producer.** Maintains scopes, names, expected types,
   effect context, function/procedure entry rules, error contracts, module
   dependencies, and deterministic-output status. It aims to reach execution.
2. **Deliberately-invalid producer.** Starts from a valid structure and breaks
   one named invariant, preserving the expected failing phase and preferably
   the expected diagnostic code.

Arbitrary byte and token soup remains only in lexer/parser robustness targets.
Its rejection rate must not dilute the valid-generation rate or be counted as
semantic coverage.

### 6.2 Structural generation

`grammar.js` supplies the first-cut structural vocabulary and production
shape. The stateful layer adds facts grammar alone cannot express:

- lexical scopes, capture sets, declaration order, and shadowing;
- `fn` versus `pn` effect context under **S12.1**;
- type annotations, inferred/erased variants, nullable lanes, and expected
  error unions;
- lvalue/place availability for mutation under **S9**;
- deterministic versus resource/concurrency-dependent expressions;
- module/import graph ownership; and
- size, recursion, allocation, and loop-fuel budgets.

Generated identifiers must resolve unless the selected invalid mutation is
specifically `undefined-name`. Generated calls must satisfy arity and effect
rules unless those are the selected fault. These constraints sharply increase
the fraction of cases reaching validation, interpretation, MIR emission, and
runtime helpers.

### 6.3 Mutation families

Mutations are recorded as operations with stable names and parameters:

- **lexical:** byte flips, truncated UTF-8, quote/comment interaction, numeric
  suffix boundaries, longest-match operators, whitespace and line starts;
- **structural syntax:** delete/duplicate/replace one reduction, move a clause,
  rebalance one delimiter, change separator form, convert block/expression
  bodies, and splice a compatible subtree;
- **binding:** alpha-renaming, capture introduction/removal, shadowing, order
  changes, recursive/mutually-recursive declarations;
- **types:** add/remove annotations, union/intersection/exclusion changes,
  nullable toggles, constrained/recursive types, container element changes;
- **representation:** `int`/`i64`/`u64`/`f32`/`float`/`decimal` boundaries,
  typed versus boxed containers, optional native fields, and poison values
  under **S4** and **D2.5–D2.6**;
- **calls/errors:** arity boundaries 0/4/5/16/17, direct/dynamic calls,
  `fn`/`pn`, user shadowing, error values, `raise`, handlers, and postfix `^`
  under **S7** and **S12**;
- **COW/lifetime:** nested aliases, path writes, append/splice/delete,
  read-modify-write handles, closure capture, return across allocation, and
  container promotion under **S9**, **D4.4**, and **D5.3**;
- **tiering:** hot-loop/call thresholds, mixed eligible and pinned functions,
  recursive edges, dynamic edges, and module-state reuse under **D8.1.1v10**;
- **query/data:** `where`, `order`, `group`, joins, windows, and streams under
  **S14**; and
- **concurrency:** task trees, cancellation points, mailbox/message sequences,
  and worker boundaries under **S13**.

Type-preserving mutations and deliberately type-breaking mutations are
different operations. The former is eligible for semantic comparison; the
latter must name its expected rejection boundary.

### 6.4 Project implementation constraints

New C/C++ fuzz support follows the C+ convention and uses `Str`, `ArrayList`,
`HashMap`, and other `lib/` facilities rather than `std::` containers. It
reuses production parser helpers and promotes reusable `static` helpers through
module headers instead of copying them. The implementation never edits
generated `parser.c`, build Lua, vendored code, or `log.conf`.

## 7. Oracles and outcome classification

### 7.1 Outcome vocabulary

Every run produces exactly one primary outcome:

| Outcome | Meaning |
|---|---|
| `success` | The expected-valid contract completed successfully. |
| `expected-reject` | The expected-invalid contract failed in its declared phase/class. |
| `unexpected-reject` | A valid/reviewed source failed. |
| `unexpected-accept` | An invalid source passed its declared rejection boundary. |
| `parser-review` | Tree-sitter and C acceptance differ and need §3 review. |
| `semantic-mismatch` | Comparable execution rows differ. |
| `crash` | Process terminated by a fatal signal or platform-equivalent exception. |
| `sanitizer` | Sanitizer/root witness reported a violation, even if exit mapping is unusual. |
| `timeout` | Engine exceeded its wall/fuel budget. |
| `resource-limit` | Memory/output/process limit was reached. |
| `infrastructure-error` | Binary, fixture, launcher, or artifact handling failed. |

The summary reports every class. Only the first two are green. Reviewed parser
gaps are visible debt, not passes hidden in the success count.

### 7.2 Parser invariants

For arbitrary input the lexer/parser must:

- never read or report a source span outside `[0, input_length]`;
- either advance, finish, or return a bounded error;
- respect recursion, token, diagnostic, and allocation budgets;
- return the same status, first error, metrics, and structural hash on a repeat
  parse from clean state;
- never invoke a reduction sink after a failed committed parse in a way that
  publishes a partial AST; and
- satisfy **D1.9** under ASan/UBSan-capable builds.

Recovery is tested separately from fail-fast parsing. Recovery may collect a
bounded report; it must not silently convert invalid input into an executable
partial program.

### 7.3 Front-end invariants

Expected-valid source must traverse the actual production sequence:

```text
parse -> build -> bind/validate -> index/infer -> plan -> selected execution
```

Expected-invalid source identifies the boundary at which it must stop. A syntax
fixture reaching execution or a type-invalid fixture reaching MIR emission is
an `unexpected-accept`, even if the process later returns an error.

Diagnostics are compared by structured code, phase, and source span where
available. Full prose may be stored for triage but should not become a brittle
oracle unless a user-facing contract explicitly requires it.

### 7.4 Semantic differential

The mandatory comparison for a deterministic valid program is:

```text
LAMBDA_TIER=interp  versus  LAMBDA_TIER=jit
```

Both runs use the same binary family, arguments, dry-run policy, fixture root,
locale/timezone policy, and bounded output capture. They compare:

- exit classification;
- stdout byte-for-byte;
- declared structured output artifact, if any; and
- selected stable counters used to prove the intended path ran.

The T0 row must report actual interpreter execution with fallback zero. A
fallback comparison is inconclusive, not parity. The eager row must explicitly
select `jit`; unsetting `LAMBDA_TIER` selects AUTO and cannot serve as the JIT
oracle under **D8.1.1v10**.

AUTO is then compared against the agreed explicit result. Its oracle also
checks expected promotion and fallback classes. Optimized/unoptimized build
variants can be added as another axis once their build artifacts are named in
`build_lambda_config.json`; the harness must not invent an undocumented
runtime knob.

### 7.5 Metamorphic properties

Where no independent evaluator exists, carefully proven transformations add
useful oracles:

- alpha-renaming of a non-exported, non-reflected binding preserves output;
- adding an erased type annotation that the original value satisfies preserves
  output under **D3.3.1v2**;
- a typed/boxed representation variant preserves output under **S1.6**;
- equality symmetry, hash consistency for equal values, and total-order laws
  hold under **S5–S6**; and
- baseline and forced-GC runs are byte-identical under **D8.6.3**.

Transformations with evaluation-order, error, resource, reflection, mutation,
or effect consequences are not assumed equivalent. Each metamorphic rule needs
a formal citation and focused proof fixture before entering the generator.

### 7.6 GC, COW, and sanitizer oracle

The lifetime matrix starts from a passing deterministic baseline and reruns it
with:

- the ASan executable;
- `LAMBDA_GC_FORCE_EVERY=1` or a recorded force interval;
- `LAMBDA_GC_POISON_FREED=1`;
- a recorded `LAMBDA_GC_FORCE_SEED` for sampled stress; and
- root-witness checking where supported.

The stressed result must match the baseline byte-for-byte before a lifetime
run can pass. This follows **D8.6.3**: a crash-only GC gate misses silent
mis-tracing and representation-dependent corruption.

UBSan should be added as a named build profile through
`build_lambda_config.json`, not handwritten build Lua. TSan belongs only to
explicit concurrency campaigns after known runtime/tool incompatibilities are
documented.

### 7.7 Timeouts and resource limits

Timeouts are findings, not automatically compiler bugs. The artifact records
whether the input contains an intentionally unbounded loop/recursion, whether
the timeout reproduces alone, and whether shrinking preserves it. Valid
termination campaigns use structural fuel bounds; adversarial nontermination
campaigns test the launcher and recovery separately.

Captured stdout/stderr, source size, allocation budget, descendant count, and
artifact size are bounded. A case that fills memory or disk before exercising
the engine is classified as resource-limit noise and minimized or discarded.

## 8. Coverage campaigns

Coverage is organized by formal contract, not only by syntax token.

### 8.1 Syntax and parser — S16, D8.1.2v3

- whitespace and strict separation, including line-start continuation;
- braces as map/object/element/body/handler delimiters;
- juxtaposition and string-pattern islands;
- `view`, `edit`, `state`, and `on` forms;
- `match`, `case`, `default`, `raise`, `apply`, and handlers;
- computed map/element keys, attributes, paths, parent/root navigation;
- query clauses and ordering combinations;
- keywords admitted as names versus words that bar binding;
- malformed UTF-8, comments, escapes, numbers, and delimiters; and
- reviewed Tree-sitter corner cases from §3.

### 8.2 Values, types, and representation — S4, S11, D2.5–D2.6

- numeric boundaries and promotion across `int`, wide integers, floats,
  decimals, imaginary values, infinities, NaN, and poison;
- nullable native scalar/field/array lanes and null transitions;
- homogeneous, heterogeneous, typed, virtual, and nested containers;
- maps, elements, nominal objects, open shapes, and computed fields;
- union, intersection, exclusion, occurrence, recursive, constrained, and
  function types; and
- inferred, explicitly typed, and erased-boxed variants of the same program.

### 8.3 Equality, order, membership, and operators — S5, S6, S8, S10

- deep equality across representations, cycles/aliases permitted by the model,
  function/type identity, and error values;
- equality/hash consistency and dedup/group behavior;
- total-order antisymmetry, transitivity, and stable sort;
- `in` versus `at`, key spaces, projections, and `len`; and
- scalar/vectorized operators, union, pipe, filter, word operators, and path
  navigation.

### 8.4 Errors, functions, and resources — S7, S12

- null/missing/error/raised-error distinctions;
- handler selection, postfix propagation, declaration-boundary skipping, and
  system-fault recovery;
- direct/dynamic calls, closures, recursive calls, default/variadic arguments,
  user shadowing, and boundary arities;
- `fn`/`pn` effect coloring and assignment restrictions; and
- dry-run-safe resource acquisition, formatting, and failure contracts.

### 8.5 Mutation and lifetime — S9, D4.4, D5.3, D8.6.3

- snapshot versus borrow behavior, nested writes, iteration during mutation,
  read-modify-write handles, and var-parameter writeback;
- roots surviving allocation, calls, error recovery, closure capture, and
  module publication;
- scalar homes and nullable payload ownership; and
- every case across baseline, forced GC, poison, and sanitizer rows.

### 8.6 Concurrency and data processing — S13, S14

- structured task scope, cancellation propagation, task failure, and cleanup;
- mailbox order and message ownership;
- worker isolation when implemented;
- group/order/join/window interactions and deterministic tie behavior; and
- finite stream/lazy evaluation with explicit fuel.

## 9. Reproduction, artifacts, minimization, and promotion

### 9.1 Run artifacts

Ephemeral runs write only below `./temp/lambda-fuzz/`. Each interesting case is
stored under a content-derived identifier and contains:

```text
case.ls
metadata.json
tree.stderr
c-parser.stderr
interp.stdout
interp.stderr
jit.stdout
jit.stderr
auto.stdout
auto.stderr
sanitizer.stderr
```

`metadata.json` records source hash, master RNG seed, worker/case ordinal,
original seed path/hash, ordered mutation trace, generator version, Git commit,
binary hash, target, driver, arguments, environment allowlist, time/memory
limits, parser classifications, tier/fallback counters, exit outcomes, and
failure signatures.

Content-derived paths prevent same-second overwrite. Output is capped with an
explicit truncation marker. Environment recording uses an allowlist so secrets
cannot leak into artifacts.

### 9.2 Reproduction contract

The runner prints one command that reconstructs a finding from its metadata.
Reproduction uses the stored source by default; replaying the mutation trace is
an additional determinism check, not a prerequisite for debugging.

A finding is confirmed only after an isolated rerun. Nondeterministic failures
record success/failure counts and scheduling seeds rather than pretending one
pass disproves the issue.

### 9.3 Deduplication

Primary keys are oracle-specific:

- crash/sanitizer: normalized top frames plus signal/check family;
- parser: status pair plus normalized diagnostic/span signature;
- semantic: differing tier pair plus normalized output/exit digest;
- timeout: target plus reproducible reduced structural signature; and
- infrastructure: launcher error family, never mixed with engine findings.

Source hash alone is not deduplication: many sources can expose one root cause,
and one source can expose different bugs under different tiers.

### 9.4 Minimization

The minimizer accepts an oracle predicate and applies hierarchical reduction:

1. delete modules/declarations/statements;
2. replace AST/reduction subtrees with smaller type-compatible forms;
3. simplify types, literals, paths, and container contents;
4. shrink identifiers, numbers, whitespace, and bytes; and
5. verify the exact signature after every accepted reduction.

A minimized crash must retain the same crash/sanitizer signature. A minimized
tier mismatch must retain the same compared rows and mismatch class. A parser
discrepancy must retain the same acceptance pair. A generic “still exits
nonzero” predicate is too weak.

### 9.5 Regression promotion

After root-cause repair:

- syntax/runtime rejection regressions join
  `test/lambda/negative/fuzzy_crashes/` with a focused GTest when that is the
  established contract;
- successful execution regressions become ordinary `*.ls` tests with the
  required matching `*.txt` expected result;
- parser-reference disagreements join the reviewed fixture manifest; and
- only the minimized, explained fixture is committed—not the entire temporary
  artifact directory.

The existing `test_lambda_errors_gtest.cpp` fuzzy-crash cases demonstrate the
promotion pattern. Permanent regressions run independently of future random
campaigns.

## 10. Command and configuration surface

The implemented user-facing contract has one entry point:

```text
make fuzz-lambda                         # deterministic PR smoke
make fuzz-lambda-extended                # longer local campaign
python3 test/fuzzy/lambda/run_fuzz.py --target parser --seconds=60 --seed=1234
python3 test/fuzzy/lambda/run_fuzz.py --target semantic --cases=128 --shard-count=4 --shard-index=2
python3 test/fuzzy/lambda/run_fuzz.py --replay ./temp/lambda-fuzz/<case>
python3 test/fuzzy/lambda/run_fuzz.py --minimize ./temp/lambda-fuzz/<case>
```

Required configuration includes target, duration or case count, master seed,
per-case timeout, shard count/index, executable(s), corpus roles, dry-run
policy, artifact root, parser corpus cap, and output cap. Defaults are printed
at startup and repeated in the run summary.

The random stream derives each case seed from `(master_seed, case_ordinal)`;
`--shard-count` advances ordinals by a fixed stride, so scheduling does not
change generated content. A deterministic case-count mode is the PR default;
wall-clock mode is suitable for soak runs.

## 11. CI tiers and metrics

### 11.1 CI schedule

| Tier | Budget | Contents | Merge behavior |
|---|---:|---|---|
| Pull request smoke | 30–60 seconds plus fixed regressions | Parser corpus, feature manifest, reviewed discrepancy ratchet, small valid generator set, explicit T0/JIT comparison | Blocks on every new finding or missing feature row. |
| Nightly | about 30 minutes per shard | Structural-feedback parser corpus, structured generation, ASan, forced GC/poison, broader tier matrix | Opens/updates deduplicated findings; no silent count baseline. |
| Weekly/manual soak | 1–6 hours | Module graphs, AUTO thresholds, large structures, concurrency schedules, slower sanitizers | Produces retained artifacts and trend report. |

Parallel workers isolate their temporary directories and processes. Sharding is
by deterministic case-seed range, so a CI failure replays locally without the
original worker count.

### 11.2 Metrics that matter

- unique C-parser structural signatures and retained parser-corpus inputs;
- feature-inventory coverage by token, reduction, AST kind, system function,
  and semantic campaign;
- valid-generation rate and the deepest phase reached;
- parser agreement counts plus every reviewed/unreviewed discrepancy;
- explicit T0/JIT comparable, fallback, mismatch, and inconclusive counts;
- baseline/forced-GC comparable and mismatch counts;
- unique crash/sanitizer/timeout signatures and median minimized size; and
- time-to-first-interesting case and corpus bytes retained per new coverage.

“Zero crashes” is necessary but insufficient. Tests per second is a throughput
diagnostic, not a correctness or coverage target. Fixed percentages such as
“80% parser coverage” are replaced by measured baselines and no-regression
ratchets because uninstrumented glue and defensive error paths make arbitrary
global percentages misleading.

## 12. Implementation plan

All phases below are implemented. The maintained operational guide is
`test/fuzzy/lambda/README.md`; this section remains the design-to-code audit
trail, with the concrete mappings recorded above.

### Phase 0 — restore trust in the existing runner

1. Refresh every `valid` and `edge_cases` source to current **S16** syntax and
   give each an explicit expected contract.
2. Replace “all handled errors pass” with the outcome vocabulary in §7.
3. Add `--seed`, case-count mode, stable artifact IDs, metadata, stdout/stderr
   capture, and no-overwrite behavior.
4. Discover only manifest-classified seeds; select functional versus `run`
   driver correctly.
5. Add explicit `interp`, `jit`, and `auto` selection. Correct the false JIT row
   in `test/interp/tier_sweep.py` and share its safe process executor.
6. Start timed mutation duration after corpus validation, not before it.
7. Retire the duplicate legacy `test/test_fuzz.sh` and dormant C++ generator
   claims after confirming no Make/CI consumer remains.
8. Update Make help and the stale historical fuzz document pointer.

**Exit gate:** every positive seed passes its declared driver in explicit T0
and eager JIT; every negative seed fails in its declared phase; an unexpected
nonzero exit cannot increase the pass count; identical seeds cannot overwrite
artifacts; a printed replay command reproduces a synthetic failure.

### Phase 1 — parser target and differential review

1. Add an in-process target around the lexer, fail-fast parser, and recovering
   parser.
2. Seed it with the refreshed corpus, repository syntax fixtures, and minimized
   historical parser crashes.
3. Feed coverage-guided mutations through both Tree-sitter and C parsing.
4. Establish `parser_review.tsv` and manually adjudicate the initial mismatch
   inventory under §3.
5. Extend the existing parser differential script rather than creating a third
   unrelated acceptance checker.

**Exit gate:** arbitrary input is stable under sanitizer runs; all reviewed
fixtures retain their classifications; any new C/Tree-sitter disagreement
fails with a complete manual-review artifact.

### Phase 2 — stateful generation and feature ratchet

1. Implement scope/effect/type-aware generation based on `grammar.js` plus the
   reviewed fixture inventory.
2. Implement structured mutation with recorded operations.
3. Add the token/reduction/AST/system-function feature inventory.
4. Measure and improve valid-generation rate and deepest phase reached.

**Exit gate:** every supported feature row has at least one reviewed seed or
generator path; generated-valid samples pass both parser review and the
production front end at a useful rate; deliberately-invalid samples fail at
their named boundary.

### Phase 3 — semantic and lifetime matrices

1. Run deterministic valid programs under explicit T0, eager JIT, and AUTO.
2. Reject interpreter fallback from comparable rows.
3. Add inferred/typed/erased and representation-lane variants under
   **D3.3.1v2**.
4. Wire the existing ASan binary, forced-GC interval/seed, poison, and root
   witness into baseline-equality runs under **D8.6.3**.
5. Add oracle-preserving minimization and signature deduplication.

**Exit gate:** the smoke corpus has byte-identical explicit-tier results and
baseline/GC-stress results; intentionally injected mismatch and UAF probes are
detected by the correct classifier.

### Phase 4 — scenario and continuous campaigns

1. Add hermetic procedure, module, resource, query/stream, and concurrency
   scenarios.
2. Add deterministic CI sharding and coverage-corpus merging.
3. Add the nightly and weekly schedules in §11.
4. Automate minimized-regression proposal output while retaining human review
   before committing fixtures.

**Exit gate:** every scoped semantic family in §8 is covered or explicitly
deferred with a reason; long campaigns retain bounded, replayable artifacts and
do not pollute the source corpus with untriaged files.

## 13. Proposed file layout

```text
test/fuzzy/lambda/
├── run_fuzz.py                      # single orchestrator
├── corpus_manifest.tsv              # seed role and driver contracts
├── parser_review.tsv                # reviewed C/Tree-sitter disagreements
├── parser_differential.py           # grammar/C and structural-signature adapter
├── source_generator.py              # stateful valid/invalid producer
├── source_mutator.py                # recorded structured mutations
├── feature_inventory.py             # D1.10 production-surface ratchet
├── semantic_campaigns.tsv           # formal-family coverage ledger
├── corpus/
│   ├── valid/
│   ├── edge_cases/
│   └── parser_review/
└── README.md                         # commands and triage workflow

temp/lambda-fuzz/                    # untracked run artifacts only
```

Names may change during implementation, but responsibilities do not. There is
one orchestrator, one shared subprocess executor, one reviewed parser-gap
manifest, and no committed raw crash dump directory masquerading as a
regression suite.

## 14. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Treating `grammar.js` as complete hides its corner cases. | Include reviewed corner-case fixtures in generation and route every differential mismatch through §3. |
| Treating C acceptance as truth preserves production parser bugs. | Formal-spec adjudication; neither parser wins automatically. |
| Invalid text dominates and never reaches runtime. | Separate arbitrary-invalid and valid-by-construction budgets and metrics. |
| Generated “valid” programs contain unresolved names/effect/type errors. | Stateful scope/effect/type generation and front-end phase classification. |
| T0 silently falls back to JIT and manufactures parity. | Require execution counters and fallback zero for comparable T0 rows. |
| AUTO is mistaken for eager JIT. | Always set `LAMBDA_TIER=jit` for the eager oracle; test AUTO separately. |
| Persistent in-process runtime retains global state. | Limit in-process fuzzing to audited parser APIs; use subprocess/fork isolation elsewhere. |
| Nondeterminism creates false semantic mismatches. | Manifest determinism, hermetic environment, invariant oracles, schedule seeds, and confirmation reruns. |
| Timeout minimization changes the cause. | Require isolated reproduction and preserve the target/signature/resource profile. |
| Artifact output leaks secrets or fills disk. | Environment allowlist, output caps, per-run quotas, and `./temp/lambda-fuzz/` cleanup policy. |
| Corpus grows without increasing coverage. | Coverage/feature novelty admission and periodic subsumption. |
| Fuzzer code duplicates production helpers. | Search/promote shared helpers and enforce the C+ project conventions. |

## 15. Alternatives considered

### Expand the shell token list only

Rejected as the main strategy. It would improve lexical variety but would not
repair false-pass classification, wrong drivers, tier blindness, corpus roles,
or reproduction. Token soup remains useful only for the arbitrary-input parser
lane.

### Treat `grammar.js` as the sole oracle

Rejected by **D8.1.2v3**. It is the best first cut and should guide generation,
but known corner cases would be mislabeled or excluded. The reviewed fixture
manifest preserves those cases without pretending the reference is complete.

### Treat the C parser as the sole oracle

Rejected by **D8.1.2v3**. It is the final shipped implementation, so every valid
execution candidate must pass it, but using implementation behavior to define
the language would fossilize C-parser defects. Differential review against
formal rulings is more work and is the only trustworthy resolution.

### Put the complete runtime in one persistent libFuzzer process immediately

Deferred. The throughput could be much higher, but runtime globals, module
registries, MIR state, recovery state, and GC ownership have not yet been
proven reset-safe. Parser-only in-process fuzzing captures the early benefit;
subprocesses preserve correctness for the rest.

### Use every repository script as an equal seed

Rejected. It confuses availability with executability and statistically
overweights common features. Manifest roles plus feature/coverage weighting
retain real-world programs without losing their driver and fixture contracts.

### Compare only AUTO with T0

Rejected. AUTO can remain entirely in T0 for cold fuzz inputs, and the current
tier sweep already demonstrates how an unset tier can be mislabeled JIT.
Explicit eager JIT is the semantic oracle row; AUTO tests promotion policy.

## 16. Acceptance criteria for the completed design

The enhanced Lambda fuzz system is complete when all of the following hold:

1. Every seed has a declared role, driver, determinism policy, and oracle.
2. Positive corpus drift fails immediately; expected-invalid outcomes are not
   counted as generic passes.
3. `grammar.js` performs first-cut verification, every C/Tree-sitter
   disagreement is reviewed, and new discrepancies are ratcheted.
4. Explicit T0 and eager MIR Direct run every comparable semantic seed and
   agree byte-for-byte with zero T0 fallback.
5. AUTO promotion is tested independently from eager JIT.
6. Forced-GC/poison output matches baseline and ASan participates in periodic
   campaigns under **D8.6.3**.
7. New tokens, reductions, AST kinds, and system functions cannot land with an
   unnoticed fuzz-coverage hole under **D1.10**.
8. Every interesting result has a deterministic artifact, replay command,
   oracle-preserving minimizer, and stable signature.
9. Every repaired root cause is promoted into an ordinary permanent regression
   fixture, including the required `.txt` for each new successful `.ls` test.
10. PR, nightly, and soak campaigns are bounded, hermetic, and report coverage
    and disagreement quality rather than only throughput and crash count.

At that point the fuzzer becomes a conformance system for a changing language,
not a stale collection of random syntax substitutions.

## Appendix A — Audited implementation anchors

These anchors explain the 2026-09-14 snapshot; line numbers may drift, so the
named symbols and targets are the verification points.

| Area | Current anchor | Audit significance |
|---|---|---|
| Maintained runner | `test/fuzzy/lambda/run_fuzz.py`, `test/interp/lambda_process.py` | Deterministic seed contracts, process isolation, classification, artifacts, replay, and minimization. |
| Make integration | `Makefile`, `fuzz-lambda`, `fuzz-lambda-extended`, `fuzz-lambda-asan` | Builds the required executable matrix and invokes the shared runner. |
| Parser API | `lambda/runtime/parser/lambda_rd_parser.h`, `lambda_rd_parse_source`, `lambda_rd_parse_recovering` | Isolated byte-input target with stable metrics, recovery, and structural-signature feedback. |
| Reference grammar | `lambda/tree-sitter-lambda/grammar.js` | Best first-cut structural reference under D8.1.2v3. |
| Parser differential | `test/lambda_parser_diff.sh`, `test/lambda_parser_poc_diff.c`, `parser_differential.py` | grammar.js first cut, C-parser production comparison, reviewed differences, stability, and structural feedback. |
| Tier sweep | `test/interp/tier_sweep.py`, `test/interp/lambda_process.py` | Explicit interpreter/JIT/AUTO selection and reusable process isolation. |
| Production pipeline | `lambda/runtime/runner.cpp` | Parse/build/bind/validate, T0, and MIR Direct phase boundaries. |
| ASan build | `build_lambda_config.json`, `lambda-debug-asan.exe`; `Makefile`, `fuzz-lambda-asan` | ASan binary participates in the same declared seed and fuzz matrix. |
| GC stress | `lambda/runtime/lambda-mem.cpp` | Implements force interval/seed, freed-memory poison, and root witness. |
| Regressions | `test/test_lambda_errors_gtest.cpp`; `test/lambda/negative/fuzzy_crashes/` | Existing destination for minimized rejection/crash cases. |

## Appendix B — Superseded recommendations

The following claims from the previous revision of this document are superseded
by this design:

- Tree-sitter is not the production Lambda parse path.
- `grammar-lambda.js` is not the current reference input.
- A handled parse/runtime error is not universally a fuzz pass.
- The corpus is not “54 seeds”; repository discovery currently reaches more
  than a thousand sources with different roles.
- The dormant C++ generator/mutator are not an implemented generation phase.
- Lambda uses GC and precise roots, not reference counting.
- `make test-fuzzy` is not the maintained Make target.
- ASan, differential testing, minimization, structural feedback, and extended
  CI are implemented phases rather than future recommendations.

Historical measurements and crash discoveries in the old document may still
motivate seed reuse, but they are not current acceptance evidence.
