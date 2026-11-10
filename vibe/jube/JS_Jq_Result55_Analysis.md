# LambdaJS jq performance analysis for Result55

Implementation follow-up: [jq optimization and AUTO generator promotion](<../impl/JS_MVP_Lmd_Jq_Optimization (done).md>).

**Date:** 2026-11-10. **Status:** investigation complete; implementation and
qualification are tracked in the follow-up above. The measurements and source
descriptions below describe the original control. **Authority:** D6.1.1–D6.1.4, D6.3.3, D6.3.6, D7.4.1,
D8.4.2, D8.4.4–D8.4.5. This report changes no ruling.

## Finding

Generator activation management is the leading measured bottleneck in the
sampled `jq_mix`, `jq_tree` and `jq_bf` execution windows. Deep generator chains
exceed the 16 parked-activation warm window and repeatedly copy, discard and
restore stack pages. Iterator result construction, property dispatch and
generator creation add costs at each jq interpreter node. `jq_records` has a
larger property and allocation component than the other sampled windows.

The expensive activation work includes VM system calls, allocation and cleanup.
The assembly register/stack switch is a smaller part of the observed cost.

## Published evidence

[Result55](../../test/benchmark/Overall_Result55.md) and its
[JSON](../../test/benchmark/benchmark_results_v55.json) report:

| Workload | Forced MIR execution | Node execution | LambdaJS / Node | AUTO execution / forced MIR |
|---|---:|---:|---:|---:|
| jq_records | 68.28 s | 2.01 s | 33.9x | 1.70x |
| jq_mix | 287.73 s | 3.95 s | 72.8x | 1.45x |
| jq_bf | 288.58 s | 2.93 s | 98.4x | 1.46x |
| jq_tree | 466.87 s | 5.71 s | 81.8x | 1.22x |

The AUTO ratios use the execution samples inside `lambdajs_e2e`, rather than
dividing process wall time by workload time. Forced-MIR process overhead is
about 9.1–9.4 s. The workload gap is already present after startup.

Both engines execute the same jqjs helper and filters. The
[driver](../../test/benchmark/text/jq/jq_driver.js) loads inputs and compiles
the jq filter before starting the timer. Here `compile(filter)` builds a jqjs
AST; execution walks that AST through generator methods. Native MIR compilation
that happens inside the workload would still count in its time.

Lambda and C2MIR columns run different ports of the workload. Their times are
useful references, but cannot isolate JavaScript runtime overhead as directly
as the shared Node/jqjs comparison.

## Fresh profiling and limits

The Result55 archive named in its metadata is absent in this checkout.
Profiling therefore uses a private normal release host built at `f6a13fc04`,
with the existing Lambda compiler edits preserved. There is no diff from the
Result55 commit in `lambda/js/mvp-lmd/`, `lambda/runtime/activation.cpp`,
`lambda/runtime/side_stack.c` or `lambda/runtime/gc/`. Other checkout/build
changes mean these runs are diagnostic evidence, not a replacement Result55
measurement or an attributed performance improvement.

The private host uses the generated `release_native` configuration: NDEBUG,
O3 and full LTO. A second link retains local symbols. Its complete `__text`
section is byte-identical to the normal stripped release host. The debug
validator marker is absent. No production or vendor source was changed.

Each steady profile samples the executing worker thread every 5 ms for 12 s,
starting 18 s after launch. The main thread waits in `pthread_join` and is
excluded. The windows contain 1,915–1,925 worker samples each. Canonical
`jq_records` completes; the other three canonical workloads are terminated
after their bounded sample. These are execution-window shares, not complete
workload averages, especially for the many distinct phases in `jq_mix`.

| Workload | VM syscalls | Activation management | Property dispatch | Iterator result construction | GC and finalization |
|---|---:|---:|---:|---:|---:|
| jq_records | 17.2% | 19.1% | 27.8% | 8.6% | 13.2% |
| jq_mix | 72.1% | 78.2% | 4.3% | 2.1% | 2.4% |
| jq_bf | 42.3% | 45.7% | 15.5% | 6.1% | 12.8% |
| jq_tree | 56.4% | 57.4% | 15.8% | 3.9% | 9.4% |

VM syscalls overlap the other columns: `madvise`, map/unmap and page protection
can occur during activation management or collection. The remaining columns
use disjoint precedence: collection/finalization, activation management,
iterator-result construction, generator creation, property dispatch, other.
Generator creation outside those categories and other work are omitted from
the table. Costs inside a sampled native ancestor are inclusive within that
category; they are not all self-time of its entry function.

Most VM samples belong to activation resume, creation or destruction. For
example, 1,361 of mix's 1,388 VM samples are under `activation_resume`; tree has
936 of 1,082, and bf has 539 of 812. This ties the leading syscall cost to
activation lifecycle operations rather than to input loading.

Two complete release records runs pass checksum `878885883`, at 60.05 s and
61.65 s. The fresh Node control passes the same checksum at 1.85 s. This is a
small diagnostic capture, not a matched repeated performance qualification.
The release GC counters report 329 collections and 0.68–0.69 s of marking.
The sample's larger GC share includes sweeping and generator finalization:
`gc_finalize_js_native_map -> activation_destroy -> madvise/munmap` is visible.
Marking time alone understates collection cost.

Peak RSS is 3,892–4,434 MiB (about 3.8–4.3 GiB). RSS includes reusable pages on macOS; it is not a
measurement of live retained data or the charged memory footprint. The
[activation design](../Lambda_Design_Runtime_Async.md) already documents this
distinction. Any policy experiment must measure charged footprint as well.

## Root causes in the implementation

### 1. Immediate compaction of frequently resumed chains

Under D6.1.1, each generator uses an activation with its own native stack and
precise root/number segments. In
[activation.cpp](../../lambda/runtime/activation.cpp), `activation_note_parked`
immediately compacts the oldest eligible activation when `warm_count > 16`.
`activation_compact` allocates an image, copies the used state and discards its
pages. `activation_resume` calls `activation_restore` before every resume;
restoration reclaims pages, copies state back and frees the image.

Nested `yield*` and `for...of` repeatedly traverse the same generator chains.
With a working set larger than the warm window, an activation classified as
cold can be resumed almost immediately. The fixed window then produces page
management work throughout a hot computation. The earlier jq tuning in
[JS MVP §36.6](JS_MVP_Lmd.md#366-jq-generator-stack-reuse) batches trimming of
**idle pooled stacks**; this separate **parked live-stack** policy remains.

A temporary standalone generator probe confirms the threshold effect. It
passes 10,000 values through nested `yield*` wrappers. The second complete pass
in each process gives:

| Wrapper depth | LambdaJS ms | Node ms |
|---|---:|---:|
| 8 | 15.87 | 1.02 |
| 15 | 31.60 | 1.66 |
| 16 | 58.30 | 1.97 |
| 17 | 87.82 | 1.82 |
| 24 | 279.71 | 3.20 |
| 32 | 491.48 | 6.00 |

There is also a source generator, so 16 wrappers exceed the 16-activation
window. Every row returns checksum `49995000`. This is a mechanism probe, not
a canonical benchmark score; no threshold or runtime source was changed.

### 2. Generator lifetime creates additional map/unmap work

An activation reserves 2 MiB of native stack and two 512 KiB side segments in
one mapping. `stacks_release` retains at most 32 idle mappings. Waves beyond
that bound map and unmap stacks. Abandoned generators are finalized by GC.
`activation_destroy` restores a compacted image before walking its recovery
chain, even when that activation will never run again. The profile shows
these cleanup paths, including restore and unmap operations during sweep.

The recovery-chain ownership requirement is real (D6.3.6); skipping restoration
without replacing the cleanup ownership mechanism would be incorrect.

### 3. Every iterator hop performs a general object protocol

In [mvp_lmd_generators.cpp](../../lambda/js/mvp-lmd/mvp_lmd_generators.cpp),
`mvp_lmd_iterator_result` allocates an ordinary object and inserts `value` and
`done` through two named setters. The MIR compiler's `iterator_value` in
[mvp_lmd_mir.cpp](../../lambda/js/mvp-lmd/mvp_lmd_mir.cpp) invokes `next`, then
reads `done` and `value` through property access. Acquiring an iterator also
allocates a private two-slot record and resolves its methods.

The runtime already has `mvp_lmd_iterator_step_read_value` and a value-only
fast path for intrinsic sequence iterators. General MIR `for...of` lowering
does not use that helper, and its fast case does not cover generator next.
`yield*` must additionally preserve observable delegate result-object identity.
This makes arbitrary result-object elision unsafe.

### 4. Generator creation and dynamic calls repeat small costs

`mvp_lmd_generator_call` copies arguments into an array, allocates the carrier
and activation, and resumes it immediately to initialize parameters. The MIR
prologue unconditionally parks before the body. `generator_body` forwards the
retained argument array through general sequence/property reads and another
rooted argument span. Each new activation installs a heap-backed recovery
frame and executes `sigsetjmp(..., 1)`; signal-mask system calls are visible.

jqjs's small `apply`/`paths` methods make these operations frequent. General
class/property/native-adapter dispatch, key lookup, shape growth and precise
root/realm bookkeeping then repeat around each jq node operation. Source
examples include `FilterNode.apply`, `PipeNode.apply`, `IndexNode.apply` and
`RecursiveDescent` in the generated jq helper.

### 5. AUTO cannot promote the central generator definitions

D8.4.2 selects AST-first AUTO and forced MIR separately. In
[js_interp.cpp](../../lambda/js/js_interp.cpp),
`js_interp_p2_admission_reason` returns `"suspended definition"` for generator
or async definitions. That is a concrete admission barrier for jqjs's central
generator methods. It explains why expecting hotness alone to turn AUTO into
the forced-MIR path is unsound. It is consistent with the additional
22–70% published AUTO execution penalty, although this investigation does not
attribute every part of that penalty to this one barrier.

## Proposed improvements, in order

1. **Revisit the parked-stack compaction policy first (D6.1.1).** Test deferred
   compaction at collection/memory-pressure boundaries with an explicit
   resident-byte budget and hysteresis. Separate unreachable cleanup from
   genuinely parked live state. Preserve exact roots, scalar adoption,
   same-address restoration and bounded memory. A blanket larger warm-window
   constant can move the cliff without resolving the policy mismatch. The
   current 16-entry policy is explicitly ratified in the linked activation
   record's RA13; changing that policy needs a design decision and an updated
   working record. This report only proposes it.
2. **Reduce activation teardown and startup costs (D6.1.1, D6.3.6).** Give
   abandoned compacted activations a durable cleanup inventory so recovery
   frames can be freed without restoring dead stack pages. Evaluate a bounded
   stack-pool policy using committed bytes and workload lifecycle evidence.
   For provably simple generator parameters, defer stack start until first
   resume while retaining argument snapshots and call-time prototype behavior.
   Complex defaults/destructuring must retain their call-time effects/errors.
   Reuse the existing argument and recovery helpers rather than duplicating
   their semantic paths.
3. **Fuse intrinsic iterator stepping and build required results directly
   (D7.4.1, D8.4.4).** Extend the existing value-only helper to intrinsic
   generator next and use it in MIR consumers. Guard the cached callable
   identity, receiver brand/context and result visibility; keep the shared
   protocol fallback for custom methods, getters and delegated results.
   Construct observable results with a prepared ordinary shape and direct
   owned stores. Share the mechanism with `js_make_iter_result`; avoid a new
   independent AST/MIR semantic implementation.
4. **Improve static dispatch and then AUTO admission (D7.4.1, D8.4.5).** Carry
   nominal receiver and method facts through jq-node fields and closures; use
   compile-time predictions with inline guards and shared-kernel misses.
   General dynamic sites remain correct. Extend promotion only with a closed
   proof for supported generator shapes. This is a separate improvement to
   the AUTO column after the shared activation costs are reduced.

No per-site inline caches or feedback vectors are proposed (D7.4.1). A
stackless generator transformation would change D6.1.1 and is outside these
recommendations. Precise ownership remains mandatory throughout (D6.3.3).

## Verification budget for implementation

For each independent change: use focused activation/generator/iterator tests,
the depth probe and forced-GC/poison checks, including abandonment, wide
scalars, defaults, `.return`/`.throw`, custom next/getters and delegated result
identity. Pair release controls and candidates in alternating order, compare
all four canonical checksums, and collect both execution time and charged
footprint. Include a large population of genuinely suspended activations to
bound the memory tradeoff. Run the full JS/Test262 and required Lambda gates
at the completed milestone. Do not change harness expectations or vendor code.

The profiling windows do not justify an overall speedup forecast. Removing
page-management overhead alone leaves substantial per-hop JavaScript runtime
work. Reprofile after that first milestone before expanding optimization scope.

## Evidence artifacts

All artifacts are under [temp/jq_result55_analysis](../../temp/jq_result55_analysis/):

- `provenance.json`, `text-comparison.json`: source/build identity, binary and
  workload hashes, identical release code sections.
- `*-steady.sample.txt`, `sample-summary.json`, `summarize_samples.py`: raw
  profiles, worker-only aggregation and disjoint attribution method.
- `records*.stdout`, `records*.stderr`, `*-steady.status.json`: completed
  checksum runs, counters and explicit bounded-profile termination status.
- `generator_probe*.js`, `generator-*.stdout`: independent threshold probe
  and both engines' results. These are temporary diagnostics.
- `profile_workloads.py`, `profiles.log`, `build-*.log`: commands and outcomes.

No production change was made, so this investigation does not claim a runtime
fix or qualify the current source tree against the regression suites.
