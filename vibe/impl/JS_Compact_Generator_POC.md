# JS compact generator proof of concept

**Spec linkage:** D6.1.1v3, D6.1.3, D8.4.4, D8.4.5.

## Current checkpoint

- Implemented the closed MIR admission for synchronous generators with simple parameters and a final `yield` or `yield*` expression statement. Earlier statements and the yield argument must contain no other yield; the scan conservatively includes nested function literals.
- The JIT prelude runs once and exits after saving the value or delegate record. Other definition shapes retain their activation backend.
- Intrinsic compact delegation uses an iterative trampoline with traced parent links for `next`, `return` and `throw`. Opaque callback recursion can switch to an activation without repeating the JIT prelude.
- Release MIR and forced AUTO-promotion protocol fixtures pass. A 100,000-delegate intrinsic chain completes; a 2,049-frame opaque callback chain preserves one-time initialization and return propagation.
- Release jq comparisons are complete below. Native focused checks pass 36/36;
  selected Test262 generator cases pass 411 with two existing exclusions;
  the Lambda baseline passes 7,378/7,378, including 856/856 MVP native cases.
  Follow-on optimization work continues in [jq dispatch and AUTO tuning](JS_Jq_Dispatch_And_Auto_Tuning.md).

## Acceptance

Intrinsic compact chains allocate no activations and use bounded native stack depth. Observable iterator results agree with the stackful path, including a 100,000-delegate chain. Release jq measurements and the focused generator tests complete without regressions.

## Release measurements — 2026-11-11

Paired single runs with `JS_EXEC_BACKEND=mir`, identical input/checksums, before
binary from `f379c940c`, after binary with the compact backend and trampoline.
Times below measure query execution after jq compilation. These are diagnostic
one-round workloads, not the full multi-round canonical suite. Artifacts and
the repeatable driver are in `temp/js_compact_generator/benchmark.py` and
`final-benchmarks.json`.

| Query | Before | Compact + trampoline | Speedup |
|---|---:|---:|---:|
| `ack(3;5)` | 11.986 s | 6.381 s | 1.88× |
| `b_repeat(1024)` | 1.722 s | 0.494 s | 3.48× |
| records, one round | 2.924 s | 2.420 s | 1.21× |
| Brainfuck, one round | 16.754 s | 10.630 s | 1.58× |
| tree, one round | 26.482 s | 22.778 s | 1.16× |

JIT startup remains about 7.4 seconds for these jq scripts. For example,
Ackermann process wall time falls from 19.418 to 13.986 seconds. The prototype
does not address that compilation cost.

Compiled `sizeof` values on arm64: 64-byte compact frame and 216-byte generator
carrier, or 280 bytes before argument/iterator storage and allocator metadata.
The previous activation path had 1,552 bytes of fixed records plus native and
side-stack storage (see the jq follow-up analysis). The compact path still
retains one generator object per logical invocation; it removes its native
activation and native recursion along intrinsic compact delegation edges.

## Build and remaining scope

The top-level release target hit a sandbox denial while updating a sibling
build artifact. Lambda release objects and binaries were built with the existing
release sibling archives through the generated makefiles and the normal
`utils/patch_static_module_makefile.py --sibling-archives` step. No sibling or
vendor source was changed. Full compiler/link logs are under the same temp dir.

Multiple yields, yield inside loops/branches, `try/finally` around suspension,
complex parameters, and async generators remain stackful. This prototype does
not yet spill arbitrary live locals or generate general resume dispatch labels.
Custom iterator callbacks remain ordinary JavaScript calls; the trampoline
only replaces runtime-recognized intrinsic generator resume edges. General
local spilling and loop continuation lowering are the next extension if the
terminal-generator savings justify it.
