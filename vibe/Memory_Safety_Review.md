# Memory Safety Review — The Template Approach vs. Rust-Completeness

**Status:** review — assessment of the direction set by `Memory_Safety_Template.md` / `Template2` / `Template3` / `Template4`
**Date:** 2026-07-08
**Question under review:** Lambda's implementation is C+ (`doc/dev/C_Plus_Convention.md`) — Rust was ruled out (Item boxing/unboxing, 2.4M-line rewrite), Zig (maturity), full C++ (simplicity, MIR interop). The template series builds compile-time safety inside C+. Is that direction applaudable? Can it reach Rust's completeness? If not, what else is needed on top?

**Verdict in one line:** applaud the direction — it is the best-modeled C++ safety layer this reviewer knows of for an arena/GC hybrid — but it is a **discipline system, not a proof system**; its precise ceiling is stated in §2, and the completeness target should be re-aimed accordingly (§3), with four additions that close the remaining security-relevant gap (§4).

---

## 1. Why the approach is genuinely good (not politeness)

Three properties elevate the series above typical "C++ hardening with smart pointers":

1. **It models the actual architecture, not generic ownership.** The three-domain scheme (GC heap / pool-arena / memtracked session) with `DomainOutlives`, `PersistentField`, and the explicit *promotion vocabulary* encodes the invariant that actually matters in this codebase — *no pool-object retention of session pointers* — instead of sprinkling `unique_ptr` where the allocator model (arenas, GC) makes per-pointer RAII a lie. Making borrowed-ness the pool default is the observation most hardening efforts miss.
2. **The witness pattern attacks the right second problem.** Tagged-union type confusion is Lambda's most idiosyncratic hazard (56-bit tagged `Item`s, the `View*` union). `ItemOf<Tag>` / `ViewTagToType` + exhaustive `visit()` with compile-failure-on-new-tag converts that whole bug class into compile errors at the C/C++ helper boundary — the boundary where it is actually addressable.
3. **The honesty is unusual and load-bearing.** `Template2` §9 and `Template3` Part 2 concede up front what most proposals hide: MIR-emitted code is unreachable; witnesses don't survive frees; Rust would not have saved the C3 animation UAF without the generational-handle discipline either. A safety proposal that states its own ceiling can be trusted about the rest. `Template4` (implemented and verified) demonstrates the approach catching a real bug class end-to-end — the direction has an existence proof, not just a design.

## 2. The precise ceiling: templates encode state, not flow

The fundamental limit is expressiveness, not effort: **C++ templates can encode *state at a point*; they cannot encode *flow over time*.** Rust's guarantee is not smart pointers — it is lifetimes as type parameters plus aliasing-XOR-mutability, enforced by flow-sensitive analysis across function boundaries. `BorrowedPtr<T, PoolDomain>` can say "this came from a pool"; it cannot say "this must not outlive *that pool's next reset*," because `'a` does not exist in C++'s type system. (Herb Sutter's C++ "lifetime profile" stalled on exactly this — it is an expressiveness gap, not an engineering one.)

The scorecard:

| Safety class | Rust | Template approach | Residual |
|---|---|---|---|
| Spatial (bounds) | checked | **matchable** — checked slices/arrays, `Template4`-style resolve-only temporaries | ≈ none, where adopted |
| Type confusion (unions) | enums | **mostly matchable** — witnesses + exhaustive `visit` (helper layer) | MIR-emitted code |
| Cross-domain retention | lifetimes | **matchable** — `PersistentField` + promotion is genuinely borrow-checker-shaped | escape hatches |
| **In-domain temporal (UAF)** | lifetimes | **not matchable** — a borrow outliving its arena's reset compiles fine | **the big one** |
| Iterator / realloc invalidation | borrow rules | API discipline only | real |
| Data races | `Send`/`Sync` | **nothing** — templates have no story | see §4.6 |
| Uninitialized data | ctors + MaybeUninit | constructors + `pool_calloc` convention | small |
| GC / JIT / FFI core | `unsafe` blocks | trusted core | **identical in both worlds** |

Three concrete cases no template will ever catch: a `BorrowedPtr` dereferenced after its pool's bulk reset; an element pointer held across a growing `ArrayList`'s realloc; and (future) a data race — all flow properties, all invisible to a type checker without lifetimes.

## 3. Re-aim the completeness target

The right target — which `Template3` Part 2 already half-states — is **not "Rust everywhere" but "Rust's failure-mode guarantee for the untrusted-input shell": corruption/RCE becomes clean abort.** That is achievable in C+. Full temporal safety *by construction* is not, in this language, ever. Two corollaries:

- Adopt Rust's real epistemology, not its checker: a small **trusted core** (`mir.c`, GC internals, tagged packing — unreachable by *any* approach, exactly Rust's `unsafe`) and a **checked shell** whose coverage is tracked and ratcheted. What made Rust's model work socially is that `unsafe` is greppable and its shrinkage is measurable.
- The threat model is untrusted *documents*: the exploitable findings in `Template3`'s audit (C1, H1–H3) are all parser-side. The shell's conversion priority is therefore `lambda/input/*` first — which is also where `Template2` §10 already pointed (`format-json`, `print`).

## 4. What is needed on top — ranked

1. **Generational handles: promote from "Part 3.6 option" to core doctrine.** For graph-shaped lifetimes (view trees, registries, animation targets — the C3 class), generation-counted handles are what Rust itself forces (`slotmap` / `generational-arena`): the borrow checker doesn't *solve* graph ownership, it *refuses to compile it* until you adopt this discipline. It works identically in C+. Rule: **every registry that today holds a raw `View*`/node pointer holds a generational handle instead.** This closes the in-domain-UAF row for the structures where it actually bites.
2. **Debug-epoch checking in the pointer wrappers.** The compile-time-unreachable case — borrow outlives pool reset — has a cheap *runtime* answer: each pool/session carries an epoch counter incremented on reset/free; debug-build `BorrowedPtr`/`OwnedPtr` capture the epoch at construction and assert on dereference. Zero release-build cost; converts the invisible class into deterministic debug crashes. (Future hardware assist on ARM — MTE — is the same idea at 16-byte granularity; worth tracking, not waiting for.)
3. **Fuzzing the input parsers, as CI — the largest gap in the current plan.** libFuzzer/AFL over `lambda/input/input-*.cpp` with ASan/UBSan (and periodic MSan) is the industry-standard complement that finds precisely what type discipline cannot. `Template3`'s own bottom line named "sanitizers + fuzzing at test time" as the other half of the strategy — it deserves a phase and a corpus, not a footnote. Priority order: the formats most exposed to hostile input (HTML, PDF, JSON, YAML, Markdown), then the rest.
4. **Mechanized gates in the existing lint engine.** The repo already has the enforcement infrastructure (`make lint`, the `no-int-cast-radiant` rule; `Template4` §7's grep-gate). Extend it: ban raw `malloc`/`free` outside `lib/`; ban untyped `Item` field access outside the typed layer; require `visit()` over hand-rolled tag switches in converted TUs; flag `BorrowedPtr` stored into structs whose domain outlives the source (belt to `PersistentField`'s suspenders). **A discipline system holds only if the discipline is machine-enforced** — the conversion boundary must be a ratchet, not a convention.
5. **Measure the shell.** A one-line metric per TU (typed-layer adopted: yes/no) rolled into CI output. Publishing the ratchet is what keeps a multi-month migration from silently stalling — the exact lesson of Rust's `unsafe`-count culture and this repo's own baseline-percentage culture.
6. **Races: answered by architecture, not templates — say so explicitly.** When Stage A/B threading lands (`Lambda_Design_Concurrency.md` K15), safety comes from the already-decided architecture: arena-per-worker fork-join, isolated heaps, immutable-only sharing, the K13 capture rule — the architecture-level equivalent of `Send`/`Sync`. The template docs should state plainly that concurrency safety is out of their scope and owned by that design, so nobody later believes the wrappers cover it.

## 5. Bottom line

The template direction is right and should continue exactly as phased — `Template3`'s own Rust analysis reached the correct pragmatic conclusion, and `Template4` proved the mechanics. But name the ceiling in the docs: **discipline system, not proof system.** Templates + generational handles + epoch checks close the *design-time* gap; fuzzing + sanitizers + lint ratchets close the *enforcement* gap; the remaining trusted core is the same residue Rust would leave in `unsafe` blocks. With §4 items 1–4 adopted, the security-relevant delta to Rust *for this codebase* is close to nil — the failure mode for hostile input becomes clean abort, which is the guarantee that actually matters. Without items 3–4, the templates risk becoming excellent advice that erodes.

---

**Cross-references:** `Memory_Safety_Template.md` (domains, lists, dispatch) · `Memory_Safety_Template2.md` (Item typed layer; §9 honest limits) · `Memory_Safety_Template3.md` (audit; Part 2 Rust analysis; Part 3.6 handles; Part 5 phasing) · `Memory_Safety_Template4.md` (implemented shorthand-lifetime fix; §7 grep-gate precedent) · `doc/dev/C_Plus_Convention.md` (the language constraints) · `Lambda_Design_Concurrency.md` K13/K15 (the race-safety architecture) · `Makefile` lint infrastructure (`no-int-cast-radiant` as the ratchet precedent).
