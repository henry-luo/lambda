# Tune 24: flow proofs, borrowing, and destination construction

- Status: implemented and validated for the eligibility rules below; full
  baselines and the focused release comparison are complete.
- Scope: the three follow-ups authorized on 2026-09-10: branch-local union and
  record proofs; escape/borrow analysis and guard reuse; whole-renderer
  destination passing and record scalar replacement.
- Authority: D2.4.1–D2.4.3 (semantic contracts versus carriers), D3.2.4v3
  (record reification), D3.3.3v3 (representation certificates), D5.3.4
  (precise roots), D8.2.6 (producer-owned lowering), D8.3.1–D8.3.3
  (entry planning and boundary proofs), D8.4.2v2 (individual internal ABI
  operands), S9.1.2/S9.3.1 (snapshots).

## Implemented scope

1. Retain dominating branch-local semantic and layout evidence in MIR Direct.
   Successful `is` tests, match arms, and logically implied branch facts are
   scoped by function and control-flow region. Repeated stable projections
   reuse their admitted value and exact-layout guard. A successful
   structural type test is not a packed-layout admission. A string-valued
   discriminant does not distinguish overlapping open record arms. Facts must
   be binding-specific, scoped to their control-flow region, and invalidated
   by writes or opaque effects.
2. Prove the interval between an immutable alias and its last read. Direct
   scalar projections, length reads, and known pure scalar observers can
   borrow through that interval when the source is not written. A later
   mutation no longer forces an earlier read-only alias to take a snapshot.
   Unknown calls, captured values, replacement writes and escaping containers
   keep the existing snapshot and checked paths.
3. Lower eligible closed recursive construction into caller-owned precise
   field destinations. Field-only call-result locals and inline context
   arguments no longer allocate maps. Read-only record parameters use scalar
   cells, including forwarding and simultaneous tail-call argument swaps.
   A string prefix is drained into the destination only if it is the output
   prefix on every return path and has no other uses. A child shares that
   destination only when its string has one unconditional output use;
   later prepending, duplication, observation, and conditional dropping keep
   separate strings. This handles the unchanged typed Prettier renderer's
   recursive document/parts/result graph.

   Address-taken cells use explicit stores to pin their offsets in the shared
   root publication pass. They are unbound from register mirrors because a
   callee writes them. Tail-call parameters own separate cells from argument
   temporaries, and all next arguments are captured before any parameter is
   replaced. No native-stack GC scanning is involved (D5.3.4).

   The existing `fn_strcat` buffer supplies destination growth. Ordinary
   projection and boxed result materialization freeze the published string.
   Public callbacks still receive normal maps through `_b`; unknown record
   argument proofs select that admission boundary (D8.3.2–D8.3.3). Boxed input
   records are read by field identity, preserving correctness with reordered
   fields and open-record extras. There is one native body (D8.3.1).

   Scalar-result field contracts remain private lowering evidence even when
   a forward call leaves the public AST projection typed `any` (D8.2.6).
   Otherwise a recursive context argument unnecessarily selects `_b`, loses
   tail-call lowering, and recopies the accumulated string. A focused forward
   declaration test pins this proof. Destination sharing also requires the
   consumer to execute whenever its child is constructed: a later conditional
   cannot discard text that was already appended. Both outcomes of that
   conditional are pinned against interpreter output (S9.1.2).

## Eligibility and residual costs

- Record construction requires a noncapturing pure function, an explicit
  fixed record result of one to four plain `int`, `bool`, or `string` fields,
  and explicit parameters without defaults or `var`. Eligible tails are exact
  constructors, compatible direct calls, and branches/blocks of those forms.
  Additional result fields, captures, variadics, arbitrary escaping results,
  and unsupported contracts retain ordinary construction.
- Float and wide-number fields retain the existing record path. Out-of-band
  payloads require destination-owned number storage in addition to Item roots;
  publishing a pointer into a returning callee's number stack is invalid
  (D5.2.1v3–D5.2.2v3). The scalar destination protocol does not change the
  numeric companion-return ABI.
- Guard reuse currently requires stable local bindings in pure functions.
  Mutable procedures, captured/module storage, and views keep checks.
- The renderer still uses boxed scalar cells, MIR call/root frames, and the
  existing numeric semantics. Arbitrary record locals and escaping graphs are
  not all scalarized. Union validation of the heterogeneous document algebra
  remains substantial: `kind: string` does not establish disjoint record arms.
  None of these changes claim C-equivalent generated code for every typed
  program (D2.4.1–D2.4.3, D3.3.4).

## Validation

Pin output and MIR behavior for successful proofs and rejected candidates;
compare JIT/interpreter output under forced collection and freed-memory poison.
Run `make test-lambda-baseline` and `make test262-baseline`. Use a cached
release built from the starting tree for paired timing; keep source scripts
unchanged and record individual samples, checksums and binary provenance.
These are focused comparisons, not an update to Result41.

The six `test/mir/lambda/tune24_*` fixtures cover branch dominance, reordered
layouts, read-only versus escaping aliases, recursive destinations, Unicode,
long tail loops, retained strings, boxed callbacks, simultaneous record swaps,
extra result fields, captured functions, forward-call projections, conditionally
discarded children, and subnormal-float fallback.
All 24 focused interpreter/JIT/MIR-interpreter/GC-mode comparisons pass.

The full Lambda baseline passes **5,309/5,309** and Test262 passes
**40,261/40,261**, with zero regressions. The Lambda socket tests require local
loopback binding; the complete gate passed with that access. Test262 initially
stopped at the exception catalog audit because its parser omitted the existing
`JIT_IMPORT_PURE_SCALAR` initializer. Recognizing its explicit scalar/PRESERVES
contract fixes that audit without changing runtime helpers or the test harness.

The final clean release also passes **98/98** MIR emission checks, **138/138**
GC stress checks, and all **24/24** focused mode comparisons. No runtime source
changed after these release checks.

## Release comparison

Both binaries were built with `make release`, cached, and measured in five
alternating AB/BA pairs per unchanged script. Each sample uses a fresh forced-JIT
process with logging and retained MIR imports disabled. Timings below are the
script's execution median, in milliseconds; profiling runs are separate.
All 90 timed outputs agree within each workload, and the typed Prettier checksum
is **56483873** for both binaries and both profiles.

| Benchmark | Before (ms) | After (ms) | Change |
|---|---:|---:|---:|
| Typed Prettier | 2,648.180 | 2,453.600 | **−7.35%** |
| Untyped Prettier | 1,116.030 | 1,120.330 | +0.39% |
| Typed three-way merge | 6,259.590 | 6,225.190 | −0.55% |
| Typed Richards | 714.177 | 714.989 | +0.11% |
| Untyped Richards | 425.744 | 426.673 | +0.22% |
| Typed DeltaBlue | 47.976 | 47.605 | −0.77% |
| Typed Splay | 384.261 | 385.236 | +0.25% |
| Typed N-body | 26.328 | 26.350 | +0.08% |
| Typed JSON generation | 11.587 | 11.435 | −1.31% |

Typed Prettier's measured ranges do not overlap: 2,613–2,721 ms before and
2,449–2,534 ms after. The other median increases are at most 0.39% and remain
within the observed run variation; this focused comparison detects no material
regression. It does not establish a performance result for the full benchmark
matrix.

| Typed Prettier counter | Before | After |
|---|---:|---:|
| Map admissions | 2,224,640 | **0** |
| String bytes copied | 38,547,762 | **5,345,842** |
| String append calls | 1,140,226 | 783,106 |
| String freezes | 295,424 | 256 |
| Union admissions | 6,692,099 | 6,615,043 |

String copying falls **86.13%**, but recursive union validation remains large.
The heterogeneous `Doc` contract has overlapping `kind: string` arms, so a
discriminant comparison cannot certify every child field (D2.4.1–D2.4.3,
D3.3.3v3). Boxed field cells, precise root frames, and generic append calls also
remain relative to a C renderer with concrete structs and one buffer.

Provenance and individual samples are in
[`benchmark_tune24.json`](../../test/benchmark/benchmark_tune24.json).
The control is commit `2c4eca5f56946cbbd17291c45357fb47a974fc4b`, cached as
`test/benchmark/exe/lambda-tune24-before-a86ec5378453`
(SHA-256 `a86ec537845375026ba089918e6105c00dcdcf92d5691cbf4fe3a0846b03f0f7`).
The completed release is `test/benchmark/exe/lambda-tune24-86fef0bcfe4c`
(SHA-256 `86fef0bcfe4c97bbd081fd4cbfabd08e01c0993f8cc6f3c451e72361cf823a18`).
The artifact records the runtime patch digest and source hashes; `lambda.exe`
is the same completed release.
