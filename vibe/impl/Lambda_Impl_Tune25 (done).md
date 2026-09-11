# Tune 25: loop calls and numeric array mutation

- Status: implemented and validated; the measured Navier-Stokes regression
  below remains a performance follow-up.
- Scope: loop/call optimization and broader native typed-array mutation.
- Authority: D8.2.6 (producer-owned demand), D8.4.3v2 (call effects), D5.3.4
  (precise roots), D3.3.3v3 (array certificates), S7.1.1v3/S7.1.3v2
  (total reads and checked writes), S9.1.2 (snapshots).

## Implementation

The shared scalar-call loop pass now accepts initialized lexical scalar homes
from MIR Direct and runs on counted/indexed `for` loops as well as `while`.
An assignment anywhere in the loop disqualifies that home, including a cold
branch. Captured, borrowed and state homes are excluded. The pass moves only
the dependency slice of audited total scalar imports whose metadata proves
no GC, reentry, exception change, or number-stack mutation. A zero-trip loop
may execute those total calls once; effectful and pointer-producing calls
remain in place. Async loops retain their existing lowering.

An explicit terminal `return` now gives earlier procedural expressions a
discard demand before lowering. A discarded comprehension therefore runs its
collection, clauses and body without allocating an output array or pushing
each iteration's result. Comprehensions consumed as values retain their
collection protocol. This removes both allocation and helper calls under
D8.2.6, without removing source effects.

Rank-one typed arrays with `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `f16`,
`f32`, `i64` or `u64` elements gain native stores. The fast path proves the
actual ArrayNum storage kind, exact representation certificate, encoded value
kind, bounds and exclusive ownership. Compact Items already contain the exact
storage bits: writes use byte, halfword or word stores, preserving f16/f32
bits without a double conversion. Wide values use an eight-byte store.
Values requiring admission, views, shared owners and failed bounds retain the
existing checked setter. Local COW replacements and `var` write-through use
their existing publication paths.

The RHS is evaluated and rooted before the key, then the current owner is
read. A key call can allocate or replace a borrowed owner; the store must use
the post-call owner with the original RHS. No interior pointer survives a
call (D5.3.4).

## Correctness defects found during implementation

The previous array guards used MIR `LTS`/`GES`, which compare only 32 bits.
An index of 2^32 passed the bounds check and crashed the cached release on a
native write. Array read/write and dense-loop extent guards now compare all
64 bits with `LT`/`GE`. Explicit 32-bit arithmetic elsewhere is unchanged.

A statically proven value also selected the untyped `fn_array_set` fallback
for some declared array stores. Its out-of-bounds error was discarded in a
procedure without an explicit error annotation, while T0 propagated it.
Declared stores now retain their checked error boundary independently of the
hot value proof (S7.1.3v2). Four existing MIR sidecars were updated to require
the corrected comparison width and checked cold setter; their native-load
and native-store assertions remain.

The baseline exposed an existing AST JavaScript call-result ownership defect:
direct calls passed an Item root as a raw scalar destination, then returned a
pointer into that temporary frame. A separate scalar home and the existing
`scalar_storage_read` boundary now preserve the payload before returning.
Forced-GC coverage additionally found that dense JavaScript array reads
returned a borrowed scalar-tail pointer that could be poisoned by later
growth/collection. Those reads now use the same transient ownership boundary
(D5.3.4). The four failing JS fixtures and four Test262 cases reproduced on
the cached control; these are baseline repairs, independent of the Lambda
loop optimization. A stale external `node-net` module caused the DNS crash;
rebuilding it fixed the crash without a source change.

## Verification

Four new MIR fixtures cover initialized-local and counted-loop hoisting,
changed-local rejection, zero-trip loops, retained comprehensions, all ten
storage widths, large positive/negative indices, accepted/rejected conversion,
snapshot detachment, sliced arrays and owner replacement during key evaluation.
They include expected output and MIR shape assertions.

- `make test-lambda-baseline`: **5,318/5,318**, including 2,104 input cases.
- `make test262-baseline`: **40,261/40,261**, zero regressions.
- Final clean release: **102/102** MIR emission and **142/142** GC stress checks.
- New Lambda fixtures: **16/16** expected-output comparisons across T0, JIT,
  forced-GC JIT and forced-GC MIR interpreter.
- New JS scalar-home regression: AST and MIR, both normal and forced-GC,
  **4/4**. It covers direct calls, native Number/Object calls, DataView reads,
  explicit collection and spread arguments.

## Release comparison

Both engines were built with `make release` and cached. Each row uses five
alternating AB/BA pairs in fresh forced-JIT processes with logging and MIR
cache reuse disabled. The table reports in-script execution medians. All 90
normalized outputs agree between engines; benchmark goldens check the terminal
expected output, including the typed Prettier checksum **56483873**. The two
new microbenchmarks are measured independently from the existing corpus.

| Workload | Before (ms) | After (ms) | Change |
|---|---:|---:|---:|
| Counted loop with invariant calls | 26.246 | 1.441 | **-94.51% (18.2x)** |
| Typed numeric stores | 72.616 | 10.205 | **-85.95% (7.1x)** |
| Typed matrix multiplication | 44.296 | 43.435 | -1.94% |
| Typed FFT | 0.344 | 0.344 | 0.00% |
| Typed N-body | 26.251 | 26.337 | +0.33% |
| Typed Navier-Stokes | 257.422 | 262.521 | **+1.98%** |
| Typed primes | 24.060 | 23.869 | -0.79% |
| Typed Richards | 710.029 | 715.917 | +0.83% |
| Typed Prettier | 2,419.560 | 2,417.260 | -0.10% |

Navier-Stokes has a reproducible small regression: seven further alternating
pairs give **256.714 -> 262.361 ms (+2.20%)**, with non-overlapping ranges
of 256.252-258.209 and 261.907-264.060 ms. It remains open; this is not a
regression-free result. Its MIR retains the same helper-call counts; these
measurements do not isolate the responsible change. N-body and Richards
have overlapping ranges in the original comparison.

The store microbenchmark's former three in-loop `fn_array_set` calls become
native stores with checked cold arms. Profiling the new release records zero
checked-store fallback calls for that workload. The old release also records
zero in that counter because it used `fn_array_set`; the counter alone is not
a measurement of calls removed. MIR shape and paired timings establish the
optimization. Per-store guards, wide-value boxing and cold-path code still
leave headroom relative to a C loop.

Samples, hashes, profiles, verification and the isolated follow-up are in
[benchmark_tune25.json](../../test/benchmark/benchmark_tune25.json).
The control is HEAD `ae8a46bcb16ab2568223ed053bf996f9e5056a60`, cached at
`test/benchmark/exe/lambda-tune25-before-a36b8bdd345e` (SHA-256
`a36b8bdd345efec2230f3d67394ee6946617ba000b4b8e0ae2606c844a5bbdda`).
The final release is `test/benchmark/exe/lambda-tune25-1f70eb3aac9d` (SHA-256
`1f70eb3aac9d11cf334ac7b89734135b0d9e07c6c3cb33c55d98c6c41a350c12`),
also installed as `lambda.exe`. Result41 is unchanged.

## Remaining scope

Loop-wide bounds/COW elimination for more induction shapes, effect-aware
specialization of user-defined calls, and smaller precise-root frames around
additional leaf calls remain separate work. This pass does not infer pure
effects for arbitrary callees or hoist memory loads. Native typed stores still
pay per-store representation, value-kind and ownership guards when MIR cannot
remove them. Nullable, refined, heterogeneous, strided and nested stores
continue through their existing supported lanes or checked fallback.
The eager tier's typed scalar `var` rebinding limitation also remains under
the DO29 family of transport gaps; this round extends array mutation and
does not introduce scalar reference parameters.

Against C/C2MIR, these changes remove repeated imported calls, dead collection
construction and checked setters on proven numeric stores. Remaining costs
include semantic admission, precise roots, cold-path joins, array certificates,
COW checks and boxed sized-scalar call arguments. D8.3.1's one native body per
function still applies; no C-text backend or new ABI has been introduced.
