# Lambda Design Note: Survey of Safe Memory Models

**Date**: 2026-07-13
**Status**: Reference survey (no decisions in this doc)
**Related**: `vibe/Memory_Safety_Review.md` (discipline-not-proof assessment, gen-handles doctrine),
`vibe/radiant/Radiant_Design_Memory.md` (R1–R7 ownership policy, S1–S12 findings),
`vibe/Lambda_Design_Concurrency.md` (K11–K27, actor/isolate model)

## 1. Purpose

Lambda/Radiant already uses four well-known memory models: **arena/pool allocation**,
**reference counting**, **tracing GC** (Lambda heap + nursery), and manual
**ownership discipline** (the C+ convention; Rust's borrow checker is the fully-static
version of this). This note catalogs the *other* established safe memory models —
each proven in a shipping language, kernel, or allocator — as a menu for future
hardening and language-design decisions. Grouped by mechanism: static (compile-time),
dynamic (runtime-checked), and concurrency-scoped.

## 2. Static / compile-time models

### 2.1 Region inference (Tofte–Talpin)

The compiler infers lexically-scoped regions and inserts allocation/deallocation
automatically — the *automated* ancestor of our manual arenas.

- **Proven in**: MLKit, Cyclone (which added region-polymorphic functions so
  libraries work over any region). Cyclone is the direct intellectual ancestor of
  Rust's lifetimes.
- **Mechanism**: every allocation is assigned to an inferred region; regions free
  wholesale at scope exit. Escape analysis decides which region an allocation
  belongs to.
- **Failure mode**: allocations that escape inference land in a global region and
  effectively leak; real MLKit programs needed profiling tools to find these.
- **Lambda relevance**: mostly historical interest. Manual arenas + discipline reach
  the same place with far less compiler machinery, and we already have per-phase
  arenas (parse, layout).

### 2.2 Linear / affine types without borrowing

Values must be consumed exactly once (linear) or at most once (affine); freeing *is*
consuming. Simpler than Rust because there is no aliasing analysis — the value is
threaded through explicitly and returned if still needed.

- **Proven in**: ATS, Austral, Linear Haskell, Clean (uniqueness types).
- **Perspective**: Rust ≈ affine types + borrowing as ergonomics. Dropping borrowing
  removes the hardest part of the checker at the cost of explicit threading.
- **Lambda relevance**: a candidate shape for `open()` scoped resources (see
  resource-cleanup decisions R1–R5): a resource handle that must be consumed by
  close-or-return is an affine value. Could be enforced as a lint/AST check rather
  than a full type-system feature.

### 2.3 Mutable value semantics + exclusivity

No first-class references at all: everything is a value, mutation happens through
`inout` parameters, and the compiler enforces exclusive access at call boundaries.

- **Proven in**: Hylo (née Val); Swift enforces the same idea as its Law of
  Exclusivity (mix of static + cheap dynamic checks).
- **Why it's attractive**: much simpler to check than borrows — no lifetime
  inference, just no-overlapping-`inout` at each call site.
- **Lambda relevance**: Lambda is already value-semantics-first (pure functional
  core, `fn` purity). The `pn` procedural layer with mutable views is where an
  exclusivity rule would matter; today that safety comes from GC, not exclusivity.

### 2.4 Static / inferred reference counting (Perceus)

Technically RC, but a distinct model worth separating from runtime RC: the compiler
inserts *precise* inc/dec at compile time, and when a count is provably 1 it reuses
the memory in place — "functional but in-place" (FBIP) updates.

- **Proven in**: Koka, Lean 4; Lobster eliminates ~95% of RC ops statically along
  the same lines. Swift's ARC optimizer is a weaker cousin.
- **Key property**: turns persistent-data-structure updates into in-place mutation
  when the old version is dead, without a tracing GC and without programmer
  annotations.
- **Caveat**: reference cycles still need a story (Koka: side effects/regions;
  Lean: cycles impossible in pure data).
- **Lambda relevance**: **the most interesting language-level option for Lambda.**
  A pure-functional core with immutable Items is exactly the setting where Perceus
  shines — e.g. map/element functional updates could become in-place when the
  transpiler proves uniqueness. Would slot into the MIR transpiler as an
  optimization pass; interacts with the GC nursery design and the G1 rooting work.

## 3. Dynamic-check models

### 3.1 Generational references / generational handles

Every allocation carries a generation number; every dereference checks that the
handle's generation matches the slot's current generation. Use-after-free becomes a
deterministic fault instead of undefined behavior.

- **Proven in**: Vale (whole-language model); battle-proven as *generational
  indices/handles* in game engines and ECS architectures.
- **Cost**: one compare per deref (often elided for provably-live locals); 4–8
  bytes per slot.
- **Lambda/Radiant relevance**: **already our direction.** `Memory_Safety_Review`
  names a gen-handles doctrine, and Radiant's F4 gen-stamp fix for stale `View*`
  in `DocState` (T7) is exactly this model applied locally. The open question is
  scope: doctrine for cross-frame/cross-document references only, vs. pervasive
  handle types for all View/DOM pointers.

### 3.2 Type-stable / type-segregated memory

Memory once used for type T is only ever reused for type T. A dangling pointer then
still points at a *valid instance of the right type* — use-after-free is downgraded
from arbitrary corruption / type confusion to a stale-data logic bug.

- **Proven in**: Linux kernel `SLAB_TYPESAFE_BY_RCU`; WebKit IsoHeap/libpas;
  Chrome PartitionAlloc; Apple `kalloc_type`.
- **Cost**: allocator partitioning; some fragmentation across many small type pools.
- **Lambda/Radiant relevance**: **the cheapest meaningful upgrade to pools we
  already have.** Our `mempool`/arena usage is close to this by accident; making
  it a stated invariant (never recycle a pool slab as a different type; per-type
  pools for View subtypes) buys the type-confusion downgrade almost for free and
  composes with gen-stamps (3.1): type-stability makes the generation field itself
  always-readable on a stale pointer.

### 3.3 Capability hardware (CHERI) and memory tagging (MTE)

Pointers become unforgeable 128-bit capabilities with hardware-checked bounds and
provenance (CHERI), or memory gets 4-bit tags checked on every load/store (ARM MTE,
probabilistic ~93% per-access catch rate).

- **Proven in**: CHERI shipping on Arm Morello; CHERIoT for embedded; MTE deployed
  in production on Android/Pixel and in Apple's ecosystem (EMTE / Memory Integrity
  Enforcement).
- **Software cousin**: **Fil-C** — a fully memory-safe C/C++ compiler using
  capabilities + GC, running real software (SQLite, OpenSSH, CPython) at ~1.5–4×
  overhead, no annotations.
- **Lambda/Radiant relevance**: MTE is a free hardening layer on future ARM
  targets — no code changes, just build/runtime opt-in; worth enabling in CI when
  runners support it. CHERI/Fil-C are not adoption candidates but define the
  ceiling: full spatial+temporal safety for C is possible if you pay 2×.

### 3.4 Quarantine / delayed reuse

Freed memory is held back from reuse until dangling pointers can't plausibly (or
provably) reach it — probabilistically via delayed random reuse, or verified by a
heap scan for lingering pointers before actual release.

- **Proven in**: hardened allocators in production (GrapheneOS hardened_malloc,
  Apple zone quarantines, PartitionAlloc's BRP quarantine); MarkUs (scan-verified
  release); CHERIoT heap revocation.
- **Lambda/Radiant relevance**: a debug/CI configuration more than a shipping
  policy — e.g. a quarantining mode for `pool_free` in ASan-adjacent test runs to
  make UAFs deterministic and loud. Complements the parser-fuzzing-CI item from
  `Memory_Safety_Review`.

## 4. Concurrency-scoped models

### 4.1 Epoch-based reclamation / RCU / hazard pointers

Safe memory reclamation for concurrent data structures without GC: readers announce
presence (epoch or per-pointer hazard), writers defer frees until no reader can
still hold the old version.

- **Proven in**: RCU is core Linux-kernel infrastructure; crossbeam-epoch (Rust);
  hazard pointers in Folly and the C++26 direction.
- **Lambda relevance**: only if K13/K15-era tasks ever share mutable structures.
  Current design (isolates + mailboxes, K20) deliberately avoids needing this;
  keep it that way unless the shared-stream core (K27) demands a lock-free queue,
  where a bounded queue with epochs is the standard recipe.

### 4.2 Isolation + ownership transfer (actor heaps, reference capabilities)

Each actor/process owns a private heap; messages are copied or ownership-transferred,
so no cross-thread aliasing exists to get wrong. Safety comes from *isolation*, not
from checking accesses.

- **Proven in**: Erlang/BEAM per-process heaps (independently GC'd, no global
  pauses); Pony's reference capabilities (`iso`, `val`, `ref`, `box`, `trn`, `tag`)
  which statically prove data-race freedom and let actors GC independently.
- **Lambda relevance**: **already adopted in spirit.** The Jube/BEAM-style
  concurrency ledger (J1–J6) and Lambda concurrency v3 (K11–K18: tasks + child
  processes, K13 no-var-capture rule, K20 mailboxes) put us in this family.
  Pony's capabilities are the reference design if we ever want to pass mutable
  data by transfer instead of copy: K13's "no var capture ⇒ thread-agnostic" is a
  coarse-grained `iso`/`val` split.

## 5. Summary table

| # | Model | Check time | Proven in | Lambda/Radiant posture |
|---|-------|-----------|-----------|------------------------|
| 2.1 | Region inference | static | MLKit, Cyclone | Skip — manual arenas suffice |
| 2.2 | Linear/affine types | static | ATS, Austral, Clean | Candidate shape for `open()` resource rule |
| 2.3 | Mutable value semantics + exclusivity | static (+cheap dyn) | Hylo, Swift | Aligned with fn-purity; relevant to `pn` mutable views |
| 2.4 | Static RC / Perceus (FBIP) | static | Koka, Lean 4, Lobster | **Most interesting language-level option** — in-place functional updates in MIR transpiler |
| 3.1 | Generational references | dynamic | Vale, game-engine handles | **Adopted direction** (gen-handles doctrine, F4 gen-stamp) |
| 3.2 | Type-stable memory | allocator policy | Linux slab, WebKit, Chrome, Apple | **Cheapest upgrade** — make per-type pools a stated invariant |
| 3.3 | CHERI / MTE / Fil-C | hardware / compiler | Morello, Android, Fil-C | Enable MTE on ARM targets when available; CHERI = ceiling reference |
| 3.4 | Quarantine / delayed reuse | allocator policy | hardened allocators, MarkUs | Debug/CI mode for pool frees |
| 4.1 | Epochs / RCU / hazard ptrs | runtime protocol | Linux RCU, crossbeam | Avoid needing it (isolates); fallback for K27 queue |
| 4.2 | Actor isolation + transfer | architectural (+static caps) | Erlang, Pony | **Adopted in spirit** (K11–K20); Pony caps = reference for zero-copy transfer |

## 6. How the models compose here

The realistic Lambda/Radiant stack is layered, not either/or:

1. **Architecture**: isolation + mailboxes (4.2) removes cross-thread aliasing.
2. **Allocator policy**: type-stable per-type pools (3.2) + arenas; optional
   quarantine mode (3.4) in fuzz/CI builds.
3. **Reference discipline**: generational handles (3.1) for anything that outlives
   a frame/document; RAII owner classes per `Radiant_Design_Memory.md`.
4. **Language level (future)**: Perceus-style uniqueness (2.4) for in-place
   functional updates in the pure core; affine treatment (2.2) of `open()` handles.
5. **Hardware backstop**: MTE (3.3) where the platform offers it.

None of this replaces the existing GC — it narrows how much work the GC and human
discipline must do, which matches the `Memory_Safety_Review` conclusion that the
ceiling of a C++ codebase is state-not-flow safety plus clean-abort on violation.

## 7. Lambda vs Radiant: one system, two models

Radiant does **not** use the same memory model as Lambda, deliberately — that is
already the de facto architecture, and the workloads justify it. The interesting
design surface is not "which model" but the boundary contracts between the two.

### 7.1 Why the workloads demand different models

**Lambda values** are immutable, heavily shared, and have *unpredictable*
lifetimes — a functional evaluator cannot know statically when the last reference
to a map dies. That is the textbook case for tracing GC (with the nursery for
short-lived numerics), and it is also why Perceus (§2.4) is the natural *upgrade*
path: it exploits exactly that immutability.

**Radiant structures** are the opposite: big, mutable, retained trees whose
lifetimes are *knowable* and phase-structured. The lifetime-role matrix in
`Radiant_Design_Memory.md` §2 is the proof — every allocation maps to one of eight
roles (per-document DOM, per-document view tree, per-layout-pass scratch,
per-render-frame, retained cross-frame, ...). When lifetimes are that legible, GC
buys nothing and costs plenty: tracing pauses in the frame loop, no deterministic
teardown (fonts, GPU surfaces, media players need prompt release), and float-heavy
hot loops that don't want barriers. Radiant's model is effectively **manual
region-based management** (arenas keyed to phases, §2.1 done by hand) +
**type-stable pools** (§3.2) + **gen-handles** (§3.1) + RAII owner classes.

### 7.2 Browser precedent cuts both ways — but only about the DOM

Blink moved its DOM *into* a tracing GC (Oilpan); WebKit stayed with
ref-counting + IsoHeaps; Servo allocates the DOM in the JS engine's GC heap while
layout uses Rust ownership. So "GC the engine" is not absurd — but note *why*
Blink did it: cross-heap cycles, where a JS closure holds a DOM node and the
node's event handler holds the closure. RC leaks that cycle; Oilpan collects it.

Radiant has a cheaper answer to the same problem: **the document arena is the
cycle collector.** A Lambda/JS-closure ↔ DomNode cycle can, at worst, keep memory
alive until document unload — at which point `DomDocument.pool/.arena` teardown
reclaims it wholesale, cycles and all. Leak-until-navigation is bounded and
acceptable in a way it wasn't for Chrome's long-lived single-page apps at Google
scale. That is the strongest argument that Radiant does *not* need to import
Lambda's GC.

### 7.3 The three boundary contracts

Where the two models meet is where the real design lives; each direction has a
named mechanism:

1. **Radiant holding Lambda data — pin, never borrow.** Retained Radiant structs
   referencing Lambda-pool memory must pin it: `PersistentFieldRef`
   (`radiant/retained_fields.hpp`). Finding S11 (webview `src/srcdoc`) is
   precisely a missing-pin bug; S12's network-ctx borrow contract is the
   documented scoped exception.
2. **Lambda/JS holding Radiant objects — generation-checked handles.**
   Script-visible DOM/View references must survive Radiant mutations safely:
   gen-stamps (robustness fix F4) make stale `View*` a deterministic fault, and
   the radiant-dom module exposes nodes as projections rather than raw pointers.
   The G1 GC-rooting work is the same contract seen from the JIT side.
3. **Cross-thread — copy as value.** Per concurrency decisions RC1–RC8, script
   and layout share a thread (so contracts 1–2 never race); the only thing that
   crosses threads is **display lists as immutable values** to the compositor.
   Value semantics is the memory model at that boundary, sidestepping §4.1-style
   reclamation protocols entirely.

### 7.4 Bottom line

One system, two models, shared substrate: both sides sit on the same
`lib/mempool`/arena primitives and the memory-context factory registry — same
*mechanisms*, different *policies*.

- **Lambda**: tracing GC now; Perceus-style uniqueness (§2.4) later.
- **Radiant**: arenas-as-regions + type-stable pools + gen-handles + RAII;
  never GC'd.
- **The seam**: pin (7.3.1), gen-check (7.3.2), copy-as-value (7.3.3).

Notably, every memory-safety finding at the seam so far (S1, S11, T7) is a
violation of one of the three boundary contracts, not of either side's internal
model — decent evidence the split is right and that enforcement effort (lints,
owner classes, stress tests) should concentrate on the contracts.
