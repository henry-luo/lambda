# LambdaJS: shared analysis, callable definitions and search leaves

> **Implemented — 2026-09-10.** Tasks 5, 6 and 7 of the JS
> tuning follow-up to [the struct-authority record](../Lambda_Design_Structs_JS.md).
> Source base: `6460b0f51`, which commits tasks 3 and 4.
> Authority: **D1.3**, **D2.4.3**, **D5.2.3**, **D5.3.1–D5.3.4**,
> **D5.4.2–D5.4.3**, **D6.2.1–D6.2.3v2**, **D8.2.4**, **D8.4.3v2**,
> **D8.6.1–D8.6.3**. No semantic or design ruling changes.

## Task 5: shared effect and scalar-ownership analysis

`JitImportMetadata` already owns helper collection, reentry, exception and
number-stack effects. Its compact flags now also describe the possible wide
result lane. `jit_scalar_return_class_for_type` is the C-compatible projection
used by Lambda return inference, JS return inference and the sys-function
metadata fallback. `jit_import_scalar_return_class` resolves an audited helper
result into the same `ScalarReturnClass` consumed by the shared emitter.

The previous adopter accepted that class but emitted tests for all three wide
lanes whenever it was nonempty. It now emits only the admitted I64, U64 or F64
classification, with a common adoption body and unchanged watermark restoration.
The dynamic case still handles every wide lane. This is representation analysis,
not permission to change coercion (**D2.4.3**, **D5.2.3**, **D8.4.3v2**).

The six JS binary arithmetic helpers share one audited catalog initializer.
Their wide Number results are doubles; string results, errors and Decimal-backed
BigInts have separate GC ownership. Their `MAY_GC`, reentry and possible-exception
contracts remain conservative. The five previously unaudited rows now explicitly
publish boxed arguments and results as well. Their shared `CALLER_OWNED` result
fact preserves their existing caller-extent lifetime instead of introducing a
new copy-and-reclaim sequence. It is independent of `NUMBER_STACK_PRESERVES`:
these helpers can allocate fresh scalar payloads. The emitter consumes this fact
for either language. Addition retains its existing per-call reclaim and benefits
from F64-only classification. Module/property/environment reads retain their
borrowed-result handling. No global helper-adoption skip or new loop-reclamation
policy is introduced (**D5.2.3**, **D5.3.2–D5.3.4**).

The first experiment audited all six as generic boxed results, adding 47 MIR
instructions to Ackermann's body and a repeatable approximately 4.6% slowdown.
Publishing the existing caller ownership for subtraction/multiplication/division/
modulo/power removes that newly introduced work. It also removes the initially
observed 15-instruction increase in the numeric-call MIR probe; its committed
budget is unchanged. This preserves the previous number-stack space policy,
including addition's eager reclaim, rather than trading a timing win for new
unbounded wide-addition accumulation.

## Task 6: metadata per function definition

The starting tree already had `JsCallableCode` sharing, but its MIR weak table
linearly searched live records on creation and final release. Ordinary MIR and
AST constructors also allocated a temporary per-value code record before replacing
it with the definition record. Both redundant paths are removed.

MIR records use the existing library HashMap, keyed by compiler-selected entry,
context, module state and arity. This preserves the existing definition-entry
boundary; it does not use source text to merge different definitions. AST records
use the shared `AstFunctionId` from the retained Script's `AstIndex`. Their records
and metadata live in that Script's pool (**D6.2.1**, **D8.2.4**).

Validation exposed an interpreter-only constructor for class-field initializer
ASTs: it created an unindexed function at every class evaluation. Both AST and
MIR now call one retained initializer builder. It caches by the field's shared
`AstNodeId`, creates a normal strict function containing a return statement, and
appends it through `ast_index_append_profile`. Repeated class evaluations share
the definition while retaining their own captured bindings. The Script owns the
small initializer lookup table; no new AST layout or private identity system is
introduced.

Each function still has its own identity, captured environment, mutable properties,
prototype, receiver and invocation state. Sharing definition metadata never merges
JS closure values or imports Lambda's snapshot-capture semantics into JS
(**D1.3**, **D6.2.2v2–D6.2.3v2**).

Each weak MIR record remembers its owning table. Finalization removes it from
that table even when another realm is active. Table teardown detaches live records
before freeing the buckets; their remaining function references own eventual
release. This replaces the old active-realm lookup during finalization and adds
no GC root or process-global cache (**D5.3.3**, **D5.4.2–D5.4.3**).

The per-function object size is unchanged. A shared MIR definition gains one
owner pointer and a HashMap entry; this trades modest per-definition storage for
constant-time lookup and removes temporary allocation per function value. Native
builtins and bound values retain their existing specialized record ownership.

## Task 7: compatible leaf algorithms

Lambda `contains` and string index search, JS string search, and Buffer byte
search now call the existing bounded `str_find`/`str_rfind` leaves in `lib/str.c`. Forward search benefits from
the library's first-byte scan and two-byte filter. Buffer's UCS-2 alignment branch
stays in its adapter. No vendor code or search algorithm was copied or changed.

Five JS string methods share one conversion/position adapter: `indexOf`,
`lastIndexOf`, `includes`, `startsWith` and `endsWith`. JS owns receiver/search
coercion, RegExp rejection, default arguments, clamping and UTF-16 indices;
Lambda retains its own language contract. Prefix/suffix predicates restrict the
leaf to the one candidate span (**D1.3**, **D2.4.3**).

The regression comparison exposed existing JS adapter defects: code-point indices
for astral text, negative empty-needle reverse positions, omitted-argument
predicate results, and Buffer rejection of numeric-array storage and reverse
search with float-represented Numbers. The adapter now reuses the existing WTF-8
UTF-16 expansion for non-ASCII subjects, including matches inside surrogate pairs.
Buffer array admission uses `js_is_js_array`; numeric reverse search accepts both
Number representations. Source and destination ownership uses precise roots
through allocating/coercing operations (**D5.3.3**).

The `fast-diff` library then exposed an existing JSON leaf defect: the common
escaper treated adjacent WTF-8 high/low surrogates as two lone surrogates. It now
uses the shared UTF-16 pair decoder and UTF-8 encoder for a valid pair, preserving
escaping for lone surrogates. The library regression's expected output was
regenerated from Node; the entire output now matches Node, including both emoji
strings, rather than preserving the previous mixed UTF-8/escape spelling.

ASCII string search allocates no expansion. Non-ASCII searches currently expand
through the existing helper; a no-copy UTF-16-aware search adapter remains a
possible follow-up. The sharing here covers compatible search leaves, not all
string, sorting, numeric or array algorithms from the wider proposal.

## Validation and measurements

Tests added:

- Node-golden scalar results, retained subnormals, borrowed module scalars,
  coercion/reentry, BigInt, special Numbers, throws and recovery.
- Definition sharing with independent captures/properties/prototypes, methods,
  field factories, dynamic Function definitions, generators and multiple realms.
- String and Buffer search over ASCII, embedded NUL, BMP, astral and lone-surrogate
  text, positions/defaults, coercion order and UCS-2 buffers. Lambda checks its
  distinct code-point indices and null-for-absence contract.
- Native tests assert shared AST metadata, independent environments, weak-table
  teardown with live MIR values, and independence of result ownership and call
  effects. The three new JS scripts are in the permanent forced-GC sweep.

Final validation on macOS arm64:

| Gate | Result |
|---|---:|
| Lambda/Input debug baseline | 5,262 / 5,276 |
| Final release Lambda suite | 863 / 863 |
| Final release JS suite | 483 / 497 |
| JS AST/runtime native tests | 113 / 113 |
| MIR forced-GC sweep | 123 / 123 |
| Lambda / JS MIR emission | 88 / 88 and 23 / 23 |
| MIR size ratchet | 16 / 16; existing budgets unchanged |
| Final release Test262 baseline, including async | 40,261 / 40,261; no regressions |
| Five ownership/search regressions × GC intervals 0, 1, 7, 31, 100, with poison | 25 / 25 |
| String-buffer/escaping native tests | 39 / 39 |

The 14 suite failures are the seven previously observed DNS/socket/HTTP/TLS cases
registered twice. They remain failures; the test runner and expected files were
not weakened. The only existing golden changed is `lib_fast_diff.txt`, generated
from Node after the JSON surrogate-pair fix described above. The final bounded
prefix/suffix adjustment was checked by the release JS suite, focused tests and
Test262 after the debug baseline.

`check_gc_root_hazards.py` passes. `check_gc_effects.py` remains failing with 47
findings. Running the same checker against the committed versions of the changed
source files produces the same findings after accounting for the removed helper
name and line shifts; this change adds none. Its pre-existing diagnostics include
`js_literal_shape`, `js_set_function_source` and unresolved/native-member paths.
This is not a clean transitive NO_GC-audit claim (**D5.3.2**).

The dynamic checks exercise executed paths under exact roots; they do not prove
every possible lifetime path (**D8.6.3**). The unmodified Test262 baseline uses its
configured mixture of AST, MIR, module and JS-harness execution.

### Release provenance

The control is the pinned task-3/4 release from `6460b0f51`. During this work,
another agent advanced HEAD to `17258a542` with Lambda-only type/rooting repairs,
optional root-witness instrumentation and documentation. These committed changes
are retained. They do not edit JS implementation files. The candidate is that
HEAD plus this uncommitted implementation. The comparisons are incremental
release diagnostics; the source delta is not limited to this task, so they are
not a formally isolated A/B proof or a new Result42 snapshot.

- Control SHA-256: `2dfd5719f8245ac31f300d87ecc7eab475f5f76e596558cc805c9a63d8906a47`.
- Candidate SHA-256: `469de925dbf1e417f1bfe09e6bfd435e0981dd2c8ed36dc61786887b7c03cc60`.
- Artifacts: `temp/js2-analysis-code-leaf/`; binary/source provenance, raw paired
  samples, output hashes, GC comparisons, full logs, source patch and checkpoint.

Timing uses release binaries and alternating control/candidate order. Source
checksums and host/power details are in the JSON records. Peak resident memory
uses Darwin `wait4` per child process, five alternating pairs; it measures the
whole process, including compilation, rather than isolated metadata bytes.

### Standard JS probes

Eleven alternating pairs per row; all observable stdout hashes match. Ratios
are candidate/control, so lower is faster. The median of the paired ratios is
also shown because host drift makes a ratio of two separate medians misleading
for some rows. Both are diagnostics, not significance tests.

| Probe | Control ms | Candidate ms | Ratio of medians | Median paired ratio | Candidate wins |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | 13.041 | 12.990 | 0.996 | 0.990 | 7/11 |
| r7rs/fibfp | 15.876 | 15.207 | 0.958 | 0.979 | 7/11 |
| r7rs/tak | 0.356 | 0.351 | 0.986 | 1.005 | 5/11 |
| r7rs/cpstak | 0.691 | 0.702 | 1.017 | 1.005 | 5/11 |
| r7rs/ack | 84.043 | 83.047 | 0.988 | 1.013 | 4/11 |
| awfy/nbody | 529.078 | 555.076 | 1.049 | 1.008 | 4/11 |
| beng/binarytrees | 23.355 | 23.783 | 1.018 | 1.005 | 5/11 |
| kostya/matmul | 347.923 | 330.626 | 0.950 | 0.979 | 8/11 |
| larceny/array1 | 17.515 | 16.525 | 0.943 | 0.971 | 8/11 |
| larceny/gcbench | 600.644 | 599.201 | 0.998 | 0.990 | 6/11 |
| larceny/quicksort | 151.092 | 136.497 | 0.903 | 0.983 | 9/11 |

The final Ackermann body has 176 MIR instructions versus the control's 180
(the intermediate regression had 227). Its final ratio of medians is 0.988;
the median paired ratio is 1.013. The earlier reproducible extra-adoption cost
is removed, without claiming a further numeric speedup. N-body remains mixed:
1.049 by separate medians and 1.008 by paired ratios. Further attribution needs
profiling on a stable host rather than selecting the more favorable statistic.

### Search and factory probes

Seven alternating pairs per row; every run matches the control's observable
output. Searches run 500 times over 262,144 `a` bytes followed by `needle`, with
construction outside the timed region. Factories cycle through 64 definition
sites, create/call 30,000 functions and retain a live value from each site.

| Probe | Control ms | Candidate ms | Ratio of medians | Median paired ratio | Candidate wins |
|---|---:|---:|---:|---:|---:|
| factory-captured.js | 57.969 | 61.709 | 1.065 | 0.991 | 4/7 |
| factory-plain.js | 56.237 | 60.156 | 1.070 | 0.982 | 6/7 |
| search-string.js | 256.314 | 5.839 | 0.023 | 0.022 | 7/7 |
| search-buffer.js | 261.692 | 6.208 | 0.024 | 0.023 | 7/7 |
| search-lambda.ls | 259.923 | 5.125 | 0.020 | 0.022 | 7/7 |

The long-search improvement is consistent across every pair: about 44× for
JS strings, 42× for Buffer and 51× for Lambda `contains`. This is a favorable
long forward-search workload, not a claim about all string operations.

Factory results are inconclusive as a general timing win: the ratios of separate
medians are about 6–7% slower, while median paired ratios are about 1–2% faster.
The captured factory wins four pairs and the plain factory six. The structural
results—shared definition metadata, eliminated temporary allocation, indexed
lookup and correct weak ownership—are independently verified.

| Factory peak RSS | Control MiB | Candidate MiB |
|---|---:|---:|
| factory-captured.js | 85.672 | 85.016 |
| factory-plain.js | 82.609 | 82.531 |

The committed-base diff removes 41 net physical C/C++ lines from
`lambda/runtime` + `lambda/js` (+366/−407), counting comments and whitespace.
The JSON leaf repair adds 14 lines in `lib`. This bounded change does not
re-run or claim the historical whole-AST retirement/timing ratchets in
**D8.6.4v2**.

## Remaining follow-ups

- Avoid non-ASCII expansion where a UTF-16-aware byte search can preserve code-unit
  semantics without copying; retain surrogate-half matching and precise roots.
- Profile MIR callable lookup/metadata initialization on larger and smaller
  definition populations before choosing further cache specialization.
- Extend audited result/effect facts to more helpers, with borrowed/owned-result
  and reentry proofs. Effect-based guard hoisting and loop reclamation need their
  own invalidation/lifetime proof (**D5.2.3**, **D5.3**, **D8.4.3v2**).
- Share additional compatible algorithm families after checking each
  profile's coercion, ordering, indexing and error rules (**D1.3**, **D2.4.3**).

The wider JSCU36–JSCU43 proposal remains broader than these three implemented
tasks. The outstanding static GC-audit and environment/network failures above
remain visible; neither is masked by this work.
