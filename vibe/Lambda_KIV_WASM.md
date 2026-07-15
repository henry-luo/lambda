# WASM on the Lambda-MIR Stack — KIV

**Status:** KIV — informational analysis, not a roadmap item
**Date:** 2026-07-15
**Context:** Assessment of transpiling WASM to run as a Jube guest (WASM → MIR), produced during the stack-frame design discussions (`vibe/Lambda_Design_Stack_Frame.md`, whose Appendix C covers the inverse direction — Lambda compiling *to* WASM, rejected there on value-model grounds). Verdict in one line: **core WASM would be the easiest guest Jube has ever added; the long tail (SIMD, threads/atomics, exception handling, WASM-GC, perf tuning) does not align with Lambda's architecture — which is why this is KIV rather than roadmap.**

---

## 1. Why the core is easy: nearly a format conversion

Every existing Jube guest bridges a semantic gap — dynamic types, object models, closures, GC. Core WASM has none of that: a statically-typed, pre-validated bytecode whose abstraction level is nearly MIR's own.

| WASM | MIR | Gap |
|---|---|---|
| i32/i64/f32/f64 locals | virtual registers (I64 + 32-bit `*S` ops, F, D) | none |
| structured control flow (block/loop/br; reducible by construction) | labels + jumps | trivial |
| operand stack (types known at every point via validation) | single-pass abstract interpretation to registers — the standard baseline-compiler technique (what V8's Liftoff does) | mechanical |
| functions, imports/exports | MIR modules, imports, `import_resolver` | none |
| multi-value returns | native `nres` (8 int results on ARM64, 2 on x86-64) | >2 results needs a memory fallback on x86-64; rare in practice |
| `call_indirect` | function-pointer table + signature-id check | small |

The deepest simplification: **core WASM has no GC and no boxed values** — no Items, no rooting, no re-homing. And by SF5's static frame sizing, a WASM frame has zero root slots and zero number slots, so it pays **nothing** to the side-stack machinery. Pure WASM frames are free by construction — the one guest that asks nothing of the value model.

## 2. The moderate tier — mostly reuses stack-frame infrastructure

- **Linear memory**: guard-region trick (reserve a large virtual range; most bounds checks become hardware faults) = **the SF13 reserve/commit machinery** built for the side stacks, including the Windows shim. `memory.grow` = commit more.
- **Traps** (OOB, div-by-zero, `unreachable`) → Lambda errors at the boundary; signal-based OOB needs a recovery boundary = **the SF17 sigsetjmp-site pattern** (stack-guard precedent).
- **Semantics details**: trapping division (`INT_MIN/-1`, `/0`) via explicit checks; saturating/trapping float↔int conversions; `clz`/`ctz`/`popcnt`/`rotl` via short instruction sequences or leaf helpers (not native MIR insns). Tedious, well-specified, finite.
- **WASI**: maps onto libuv (already in-tree). Moderate, well-documented.
- **JS glue**: browser-targeted modules expect a JS host — **LambdaJS can be that host**, a differentiator no other small runtime has in-tree.

## 3. The long tail — architecturally misaligned (the reason this is KIV)

These are not merely harder; each one cuts against a deliberate Lambda architecture decision, so tackling them means paying for machinery Lambda chose not to have:

1. **SIMD (`v128`)** — the biggest functional gap. MIR has no vector types or instructions; scalarizing defeats the point (codecs/crypto — the modules most worth running — are SIMD-heavy), per-op helpers die on call overhead. Supporting it properly means growing a vector ISA inside MIR. v1 answer would be feature-detect-and-reject.
2. **Threads/atomics** — WASM threads mean *shared mutable linear memory*, precisely what the K11–K18 isolate model forbids by design (no shared memory between tasks). This is a values conflict, not an effort estimate.
3. **Exception handling** — WASM-EH specifies real unwinding; Lambda's convention is no-longjmp, errors-as-values (SF17). The LambdaJS transform (TLS exception value + check-and-propagate returns) would work, but it fights the spec's semantics and its cost model.
4. **WASM-GC** — structs/arrays/RTTs/subtyping mapped onto Lambda's GC is a large type-system surface. (One silver lining if ever attempted: WASM-GC is statically typed, so ref-ness of every local is known at transpile time — honest typing and precise side-stack rooting with zero inference, a cleaner fit than JS was.)
5. **Perf tuning** — single-tier MIR at opt-2 vs. Liftoff+TurboFan: competitive with baseline tiers, a few× off optimized tiers. Closing that gap means tiering/deopt — explicitly a non-goal (G5). "Run wasm modules as libraries/plugins" is attainable; "compete with V8" is not the mission.

## 4. Calibration and strategic note

A core-MVP transpiler (binary decoder + validator + single-pass lowering + linear memory + traps + WASI shim) is plausibly **smaller than the existing Python transpiler** — no dynamic-type inference, no object model, no stdlib folklore; the spec is exact. The long tail is individually skippable at first, but per §3 it is where the real cost lives, and it does not amortize against anything else Lambda needs.

If ever activated, WASM slots into the guest roadmap after Python with less semantic risk than any guest before it — arriving as a value-model-free guest that reuses the VA-reservation, trap-recovery, and multi-value machinery the stack-frame implementation builds anyway. Until a concrete need appears (running a specific wasm library in-process), it stays KIV.
