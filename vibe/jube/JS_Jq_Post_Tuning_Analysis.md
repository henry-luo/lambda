# jq performance after generator promotion and activation tuning

**Date:** 2026-11-11

**Status:** measured investigation and proposals; no production changes

**Source:** `f379c940c` (`lambda js tuning`)

This follows [Result55 analysis](JS_Jq_Result55_Analysis.md) and the
[completed optimization](../impl/JS_MVP_Lmd_Jq_Optimization%20%28done%29.md).
Generator bodies can now execute in MIR, but the remaining costs are mostly
activation ownership and generic JavaScript operations around those bodies.

The proposals preserve same-address native activations and precise roots
(D6.1.1v2, D6.1.3), immutable guarded specialization (D7.4.1), shared JS
semantics (D8.4.4), and checked promotion with original callable identity
(D8.4.5). This investigation changes no ruling.

## 1. Measurement scope

All measurements used macOS arm64 release builds, sequentially, without
overlapping builds or other benchmark jobs. The normal executable SHA256 is
`6f912a185277915b53251307fc601b3e60334769d2c25701f9d63e23b19b07de`.
Node was v24.7.0. Runtime timings exclude parsing the jq filter and loading
input, matching the benchmark driver.

Fresh one-round versions retain the canonical inputs and filters and change
only `rounds` to 1. They are diagnostics, not replacements for the canonical
15/10/7-round scores. Checksums matched Node in every configuration.

| One-round workload | Node | MIR release | MIR / Node | Checksum |
|---|---:|---:|---:|---:|
| records | 0.133 s | 2.829 s | 21.3× | 100474837 |
| bf | 0.269 s | 16.351 s | 60.9× | 2046 |
| tree | 0.688 s | 25.349 s | 36.8× | 393214 |

`mix` was also split into its 27 individual filters, preserving their input
sizes and applying the original `digest`. Their runtime sum was 28.944 s in
MIR and 0.531 s in Node. This excludes the enclosing seven-round reduction
and does not claim to reproduce its complete execution time.

Most configurations have one measured process run. The method microbenchmark
has one initial and three subsequent passes. Large effects are supported by
exact operation counts; small timing differences need repeated qualification
before choosing production constants.

Artifacts and scripts are under `temp/js_jq_followup/`. Two private release
executables link a copied runtime archive with a diagnostic `activation.cpp`:

- `lambda-diag` supplies within-binary warm-budget and idle-pool comparisons.
  Its first counter report used `log_info`, which NDEBUG removes. A counter
  symbol remains in the image, so elimination of counter overhead is not
  assumed; these are diagnostic release timings, not an uninstrumented build.
- `lambda-diag-counts` retains aggregate counters through `log_notice`.
  Counts and its timings are identified separately below. It records no
  per-event log stream.

The normal binary, production sources, benchmark fixtures, vendor sources,
and sibling projects were unchanged. No regression suite was needed for
these diagnostic-only changes.

## 2. There are three different activation problems

### 2.1 Ackermann creates an enormous simultaneously live set

The isolated `(5 | b_ack)` evaluates `ack(3;5)` and returns 253. It consumed
11.926 s, **41.2% of the isolated mix runtime sum**.

The counter build measured:

| Counter | Value |
|---|---:|
| Activations created | 657,933 |
| Peak live activations | 657,933 |
| New stack reservations | 657,933 |
| Stack-pool reuse | 0 |
| Resumes | 1,400,743 |
| Compactions / subsequent restorations | 657,384 / 657,384 |
| Bytes copied into cold images | 1,041,492,528 |

Each activation currently reserves 2 MiB of native stack, two 512 KiB side
regions, and a guard page. The reservation API performs `mmap` and
`mprotect`; releasing the reservation performs `munmap`. The peak count
therefore represents roughly **1.9 TiB of virtual reservations**, not physical
RAM. The saved images alone receive about 993 MiB of copied payload.

Almost every activation is compacted once and restored once. An idle pool
cannot reuse a stack while its activation is live. Increasing the warm budget
from 32 to 128 to 512 MiB produced 11.340, 11.293, and 11.424 s in the counter
build. Even at 512 MiB, 651,924 activations were compacted. A larger budget
does little for this case.

The relevant code is `activation.cpp:stacks_acquire`, `activation_compact`,
`activation_restore`, and `stacks_release`. This is an allocation and
representation cost of the stackful generator backend; compiling the body
does not remove it.

**Cross-engine check: the population comes from the evaluated code.** A
follow-up instrumented the common generator prototype in Node, counting first
`next()` calls and `done` completions. Running the same isolated jq filter
produced exactly 657,933 starts, 657,933 completions, and a peak of 657,933
unfinished generators. No generator `return` or `throw` was called. These are
logical execution counts, not a claim about Node's physical allocation layout.

Two independent implementations clarify the amplification:

| Implementation of ack(3,5) | Total calls / generator starts | Peak unfinished executions |
|---|---:|---:|
| Ordinary scalar recursion | 42,438 | 255 call frames |
| Direct recursive generator using `yield*` and `for...of` | 42,438 | 42,438 generators |
| jqjs interpreter | 657,933 | 657,933 generators |

A producer that yields a result remains suspended while its consumer uses
that result to recurse. Completion occurs only when the producer is advanced
again. This keeps earlier generator computations pending across subsequent
recursive work; scalar calls have already returned. jqjs further represents
pipes, conditionals, operators, argument binding, and function evaluation as
additional generators (`makeUserFuncFromNode`, `PipeNode.apply`,
`OperatorNode.apply`, and `IfNode.apply/evalIf`).

Thus Lambda is not adding hundreds of thousands of extra logical generators
in this case. The generator-heavy evaluator supplies the population, and
Lambda's native activation representation supplies much of its cost.
An engine may optimize physical representation when unobservable; the
mathematical Ackermann function does not require this generator population.

Evidence: `node-ack-generators.js` and `node-ack-generators.stdout`. The
counting wrappers add native call frames, so this diagnostic used
`node --stack-size=4096`; the initial default-stack instrumentation overflowed.
It was used only for counts, not timing. All three implementations returned
253. No benchmark or vendor source was edited.

### 2.2 Recursive streaming repeatedly evicts a live working set

`b_repeat(1024)` creates only 3,095 activations, with 2,070 live at peak, but
resumes them 1,053,739 times.

| Counter-build configuration | Runtime | Compactions | Restorations |
|---|---:|---:|---:|
| 32 MiB warm budget | 1.545 s | 586,872 | 585,162 |
| 128 MiB warm budget | 0.271 s | 0 | 0 |
| 512 MiB warm budget | 0.266 s | 0 | 0 |

The measured peak warm extent with the larger budget was **97.03 MiB**.
The current 32 MiB threshold and 16 MiB trim target cannot retain this live
chain. It cycles through discard, image copy, reclaim, and restore.

The first private build showed similar effects for `pyramid`, `upto`, and
`explode/implode`. Raising only the budget from 32 to 128 MiB reduced the
27-filter runtime sum from 29.585 to 23.030 s (**1.28×**). Raising the idle
pool from 32 to 512 without changing the warm budget gave 28.875 s.

A budget change has a real memory tradeoff. The warm budget bounds accounted
parked extents, not total process memory, active stacks, cold images, or JIT
memory. Whole-process footprint snapshots are retained in the artifacts;
they include substantial whole-script JIT memory and are not an isolated
measurement of activation memory.

### 2.3 records/tree/bf compact mostly abandoned generators

The one-round counter runs show another source of work:

| Workload | Created | Peak live | New reservations | Compacted | Restored | Destroyed cold without another resume |
|---|---:|---:|---:|---:|---:|---:|
| records | 829,739 | 15,173 | 81,760 | 50,805 | 70 | 50,735 |
| tree | 4,849,778 | 12,660 | 653,443 | 625,393 | 16 | 625,377 |
| bf | 4,335,810 | 5,938 | 1,215,566 | 556,632 | 663 | 555,969 |

These counts establish that most cold images in these workloads are never
used again. They do not prove that every such generator was already
unreachable when it was compacted. The implementation retains a suspended
activation until its generator owner is collected; the owner's finalizer
destroys it without executing the generator body. First-value consumers such
as `key.apply(v, conf).next().value` are concrete sources of incomplete
generators in jqjs.

The runtime pays for image creation and page discard, then pays to free the
image and reservation at finalization. The prior optimization removed cold
restoration during abandonment, but these earlier costs remain.

The idle pool of 32 also overflows during destruction waves. Increasing it
to 512 improved the one-round bf diagnostic from 16.382 to 13.674 s (1.20×),
records from 2.844 to 2.665 s, and tree from 25.923 to 25.751 s. This is a
useful bf lever, with limited evidence of benefit elsewhere.

**Collecting more often is not sufficient.** The existing stress knob was
used as a release diagnostic, not as a proposed production setting:

| records configuration | Runtime | Collections | Peak live activations |
|---|---:|---:|---:|
| Normal counter build | 2.872 s | 23 | 15,173 |
| Force every 100,000 public GC allocations | 2.822 s | 65 | 6,432 |
| Force every 10,000 allocations | 3.567 s | 658 | 689 |
| Every 10,000, plus pool 512 | 3.323 s | 658 | 689 |

The aggressive case spent about 840 ms marking. It still compacted 22,913
activations: GC currently trims parked stacks before finishing reachability
and sweep. Moving that trim later avoids copying objects about to die, but
most normal-run compactions were budget-triggered outside GC, so that change
alone has limited reach. Activation allocations/images also do not currently
use the collector's external-allocation accounting API.

### 2.4 Making each state smaller

A follow-up compiled `sizeof` constants from the current production layouts
using the release flags, without linking or changing the runtime:

| Record | Bytes |
|---|---:|
| `Activation` | 752 |
| `ActivationStacks` | 120 |
| Root `LambdaRecoveryFrame` | 480 |
| `JsGeneratorMapCarrier` | 200 |
| **Total of these records** | **1,552** |

`ActivationAmbient` is 400 bytes **within** `Activation`, including its
256-byte hook extension; it is not an additional allocation in this table.
The root recovery frame is separately allocated by
`lambda_recovery_frame_begin_for`. The table excludes allocator overhead,
captured environments, arguments, referenced objects, additional recovery
frames, resident stack pages, and saved images. Ackermann's cold images
average 1,584 bytes. Reducing state by 1 KiB per generator would save about
643 MiB at its measured peak; this is arithmetic, not an implemented saving.

Within D6.1.1v2, useful candidates are compact optional metadata, sharing
immutable definition/realm facts, co-allocation of records whose lifetimes
coincide, pooled image storage, and activation arenas with guarded stack
slots. Co-allocation reduces allocation overhead but does not itself remove
payload bytes. Arenas can amortize reservation/setup costs but do not remove
live stack images. Smaller stack/side-region classes require proven capacity
or a correct growth mechanism; changing the constants alone can create new
stack-overflow failures. Cross-switch mutable state and recovery ownership
must remain intact.

A more substantial alternative is an explicit resumable JS frame: resume
position, flags, environment, values live across a yield, and any active
handler/delegation state. A MIR resume entry would return on yield, then
continue from that position on the next call. Native helper frames would
finish before suspension. References would be precisely traced from the
frame, and outliving numeric values would own their payloads (D6.1.3).
This removes the need for a private native stack, its parked image, and
page-management operations for each admitted generator.

That alternative conflicts with D6.1.1v2's native-stack ownership and ban on
suspension transforms. It is an **unadopted design option**, requiring an
explicit ruling change, not routine tuning. The existing stackful design
was chosen to unify suspension across interpreters, JITs, tasks, and async
execution. Reintroducing a compact generator backend adds control-flow and
cross-tier maintenance costs; `throw`, `return`, `finally`, delegation,
closures, realms, and precise GC need qualification. A limited MIR
synchronous-generator prototype could measure that tradeoff without
claiming that all generator frames fit a fixed small size.

Evidence: `state-sizes.cpp`, `state-sizes.ll`, and `state-sizes-build.log`
under the investigation artifact directory. No backend change was made.

## 3. Property dispatch dominates a separate mix hotspot

`group_by` accounts for 5.427 s, or **18.7% of the isolated mix runtime**.
Its counter run created 196,633 activations but needed only 22 reservations,
with zero compaction or restoration.

An eight-second, 5 ms-interval worker sample of repeated `group_by` produced
1,285 leaf samples. The symbol-retaining release image has the same executable
text as the normal candidate, as verified in the preceding qualification.

- 80.62% of samples were attributed to property dispatch.
- 89.42% were underneath the sorting comparator; this overlaps property
  dispatch and must not be added to it.
- 0.54% were attributed to activation management and 1.48% to GC.
- `mvp_lmd_foreign_property`, `js_get_key_core`, global-environment lookup,
  key classification, and shape lookup were prominent.

The path repeatedly crosses `mvp_lmd_class_property` → foreign-property
adaptation → shared JS lookup. Comparators read fields such as `a.key` and
`b.key`, invoke `compareValues`, and read global helpers. Source-known record
shapes and callable targets are not carried far enough through `map` results
and sorting callback parameters to eliminate these open operations.

There is also a concrete method-specialization cliff at
`mvp_lmd_mir.cpp:4834`. The compiler scans **all classes in the program** for
a matching method spelling. If it finds a fifth candidate, it throws away
the entire prediction set. jqjs defines 48 classes and 30 generator `apply`
methods.

A controlled microbenchmark calls the same `C0.apply` 500,000 times. Adding
unused classes with the same method spelling changes MIR's subsequent-pass
median from approximately **7.22 ms with four classes to 16.94 ms with five**.
Thirty-two classes take 16.55 ms. The receiver and result are unchanged.
This isolates a compiler-wide candidate cutoff, rather than observed runtime
polymorphism at the call site.

## 4. AUTO still runs important ordinary methods and callbacks in AST

The one-round records AUTO diagnostic took 4.890 s with admission logging,
against 2.829 s for MIR. Of 99 definitions reaching the admission check,
50 were eligible and 49 were rejected. These are definition counts, not
time-weighted call counts. Rejections included 15 method shapes, 12 arrow
shapes, 10 compound values/patterns, and three nested callables.

Concrete rejected operations include:

- Ordinary `FunctionCall.apply` and `FunctionCall.paths`.
- Arithmetic/comparison `Operator.combine` implementations.
- The sorting comparator arrows in `sort_by` and `group_by`.
- `sort_by`'s generator wrapper containing nested callables.
- Some generator `apply`/`paths` bodies with unsupported patterns.

`js_interp.cpp:5962` admits public classic-script generator methods while
continuing to exclude ordinary classic-script methods. Extending this gate
alone does not cover methods such as `FunctionCall.apply`, which also create
a captured nested function.

The method microbenchmark takes about 77 ms in AUTO versus 7.22 ms in MIR
with four classes. A separate descending numeric sort at length 4,096 makes
exactly 82,304 comparisons in both tiers, but takes 53.54 ms in AUTO versus
2.13 ms in MIR. These isolate substantial remaining call/AST costs.

An initially suspected quadratic sorting path was **ruled out for ordinary
arrays**: AST, AUTO, and MIR all exercised the shared heapsort in this probe.
The native generic array-like insertion-sort implementation was not involved.
Node made 4,095 comparisons on this already reversed input; an adaptive
stable sort is a separate possible improvement, not evidence that Lambda's
observed ordinary-array sort is quadratic.

## 5. Recommended implementation order

### A. Improve static property and callable facts

This is the broadest immediate compiler target. Narrow method candidates
from the receiver's source provenance before applying the small guard-chain
budget. An unrelated fifth class must not discard a valid prediction for a
known receiver. Carry literal/constructor shapes through callback return
values, arrays, and comparator parameters. Preserve live own shadows,
descriptor/prototype changes, and foreign receivers through guarded misses.

For sites that remain open, reuse the shared canonical own-data lookup with
an already classified receiver and key. Avoid repeated classification,
global-object checks, and adaptation when an ordinary data read is proven.
Keep global binding reads attached to their authoritative environment cells;
guard source-predicted callable identities before direct invocation.

These are D7.4.1 predictions/shared-kernel improvements, not mutable inline
caches. The measured group_by share provides a strong target; the microbench
speedup must not be presented as a whole-jq speedup.

### B. Extend AUTO admission in small, checked stages

First admit ordinary public classic-script methods whose bodies already
satisfy the dependency proof, then capture-free classic-script arrows such
as sorting comparators. Preserve receiver/home/source/identity under D8.4.5.

Handle nested callbacks and captures as a later whole-definition extension:
future compiled generator invocations must create their compiled closure
environment normally. Do not bridge an existing AST environment or switch
an already suspended AST instance to MIR. Merely lowering the promotion
threshold cannot fix definitions that are pinned by admission.

### C. Make activation lifetime and allocation cheaper

Investigate early destruction of **proven nonescaping** temporary generators
after a first-value intrinsic `next` consumer. This must have the same
observable abandonment behavior as collection: no implicit `return()`, no
execution of `finally`, no lost escaped generator, and correctly owned
yielded scalars. Generic/custom iterators retain the semantic path.

Track activation/image pressure in the owning collector, and compact only
surviving owners after collection where possible. Use pressure/productivity
pacing; the forced-GC experiment rules out an indiscriminate frequency hike.
Avoid increasing full-heap marking enough to consume the VM savings.

For the large simultaneously live case, prototype a Lambda-owned activation
allocator that amortizes reservations and guard setup, and investigate
smaller stack/side-region size classes where bounds are proven before entry.
Retain full-capacity fallback for unproven bodies, stable addresses, guards,
and precise ownership. Measure maps/protections/discards, physical pages,
image bytes, and peak live activations separately. Slabs alone do not remove
the live images or justify promising a particular speedup.

Ackermann's count makes this a structural backend task. The current
D6.1.1v2 explicitly uses native activations and forbids transforming functions
to suspend them. A state-machine generator backend would require a separate
design decision; it is not assumed by these tuning proposals.

### D. Apply bounded policy tuning after the structural work

A configurable/adaptive resident budget can retain the measured 97 MiB
streaming working set. The 128 MiB experiment is a useful candidate, with
an explicit memory tradeoff; 512 MiB provided no additional benefit in the
measured cases. A byte-bounded idle pool that tolerates larger completion
waves may preserve the bf gain without retaining arbitrary stack capacity.

Do not spend the next milestone on iterator-result allocation: the previous
qualification and the new group_by profile place that cost around or below
1–2%. Do not use the GC stress knob as a tuning recommendation.

Acceptance should combine canonical jq checksums/timings, the four/five-class
probe, first-value abandonment and escaped-generator cases, callback captures,
prototype/own-property overrides, cross-realm behavior, precise-root stress,
and the relevant Test262 generator/iterator/property/call coverage. Measure
runtime and compilation separately and keep memory alongside timing.

## 6. Evidence inventory

All paths below are relative to `temp/js_jq_followup/`:

- `phase-results.json`, `more-results.json`: fresh release/Node and policy comparisons.
- `count-results.json`: per-case activation counts, including 32/128/512 MiB trials.
- `gc-results.json`: reclamation-frequency counterfactuals.
- `admission-summary.json`: original-source locations and admission reasons;
  locations account for the injected CommonJS prefix.
- `sort-results.json`: exact comparison counts and tier timings.
- `node-ack-generators.stdout`: independent Node generator counts and scalar/direct-generator controls.
- `groupby-steady.sample.txt`, `sample-summary.json`: focused sample and attribution.
- `memory-*.json`, `repeat-memory-*.json`: bounded whole-process footprint snapshots.
- `prepare.py`, `prepare_more.py`, `build_diagnostic.py`, `build_counts.py`,
  and `run_*.py`: diagnostic preparation, private release builds, and runners.

No canonical benchmark, vendor file, production runtime file, or test harness
was modified for this investigation.
