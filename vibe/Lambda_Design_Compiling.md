# Lambda / LambdaJS — Compilation Strategy ADRs

> **Status: ACTIVE ledger (started 2026-08-01).** Top-level architecture
> decisions for how Lambda script and LambdaJS compile. This doc holds
> cross-cutting *policy*; mechanisms stay in their own design docs. On
> conflict, decisions here win over per-area docs (same convention as
> `Lambda_Design_Jube_Architecture.md` JA1–JA16).
> Related:
> [`Lambda_Design_Dual_Func_Compiling.md`](Lambda_Design_Dual_Func_Compiling.md) (DF1–DF17 — the specialization mechanism),
> [`Lambda_Tune_Typed_Vs_C2MIR.md`](Lambda_Tune_Typed_Vs_C2MIR.md) (M1–M8 measured evidence),
> [`Lambda_Design_Type_Enforcement.md`](Lambda_Design_Type_Enforcement.md),
> [`Lambda_Issue_Type_Support.md`](Lambda_Issue_Type_Support.md) (TS-1..TS-9),
> [`Lambda_Design_MIR_Cache_L3.md`](Lambda_Design_MIR_Cache_L3.md).

---

## LC1 — Specialization over caching: no inline caches in Lambda script

**Status: DECIDED 2026-08-01.**

### Decision

Stated in terms of *lanes and sites*, not functions:

1. **Specialized lowerings** — code compiled under known types, whether
   *declared* or *inferred behind a dual-func guard* (DF1/DF2) — contain no
   dynamic-dispatch sites by construction. ICs are ruled out here by absence
   of need; the policy is self-enforcing rather than enforced.
2. **Open sites in Lambda script** — in untyped functions, in the open sites
   of partially typed functions, and in boxed fallback bodies (DF3) alike —
   compile to plain runtime dispatch (`fn_add`-style closed switch) today,
   and to **multi-version compilation / guard hoisting** tomorrow
   (dual-func §10, DF16). **Never inline caches.**
3. **LambdaJS keeps its ICs.** JS has no type system to specialize against;
   the IC *is* its type feedback. Existing and future LJS IC work (Tune6
   lineage, store ICs, shape identity) is unaffected by this ADR.

### Original framing and the grey-area resolution

The decision was posed as a three-way function-granularity policy: typed
functions → no ICs; untyped functions → ICs allowed; partially typed
functions → grey area, resolved to no ICs (simpler implementation; the
correct long-term mechanism for open types is multiple compiled
versions/paths, not per-site caching).

The recorded formulation above is one notch stronger: it makes the entire
Lambda lane IC-free, including fully untyped functions. Function granularity
is ambiguous exactly where the dual-func design lives — a partially typed
function contains both closed and open *sites*, and the boxed fallback body
of a fully typed function *is* an untyped lowering, so "typed ⇒ no IC" and
"untyped ⇒ IC" would collide inside one function. **Escape condition:** if a
measured hot open site ever appears that inference cannot close, treat it as
a DF12 inference bug first; only if it survives that lens does the
untyped-Lambda IC question reopen.

### Rationale

- **R1 — The entry guard already is the cache, at better granularity.** An IC
  checks per site per execution; the DF1 guard checks once per call and buys
  the whole specialized body. For hot, type-stable code — the only case where
  ICs pay — function-level specialization strictly dominates site-level
  caching: one check amortized over N sites instead of N checks.
- **R2 — Lambda's dispatch space is closed; JS's is open.** ICs earn their
  keep on unbounded dispatch spaces (shapes, methods, prototype chains).
  Lambda's operator dispatch is a small closed type-pair switch; a
  well-predicted branch tree is within a few cycles of a monomorphic IC hit.
  The IC-vs-switch delta is small; the specialized-vs-either delta is the
  measured 9.48x (Result18 static-ceiling columns). Spend where the delta is.
- **R3 — Generated code stays immutable, which is an architectural asset.**
  ICs mutate code or cache cells; the cache-cell-vs-MIR-module-cache epoch
  problem was already encountered in the Tune6 era. Multi-version output is
  immutable — it composes cleanly with the L1/L2 module cache, the L3
  design, and the MarkPack sealed-module / AOT direction.
- **R4 — Union types make multi-versioning complete.** `int | string` has two
  members; §10 compiles one version per member with the boxed body as
  backstop. No megamorphic tail, no cache-miss cliff — a completeness JS ICs
  can never have.
- **R5 — Precedent.** Julia is exactly this policy (no ICs anywhere;
  multi-version specialization + dynamic-dispatch fallback) and is the
  best-performing system of its shape; SBCL and Static Hermes match on the
  typed lane. V8/JSC-style ICs are the workaround for a language with
  nothing to specialize against — LJS inherits that world; Lambda does not.

### Accepted costs

- **C1 — Shape dispatch is pushed onto the type system.** The one case ICs
  uniquely help that entry-level specialization does not: `m.field` in a hot
  loop on an unannotated map of unknown shape (per-iteration shape variance
  defeats an entry guard). The sanctioned answer is "declare the map type" —
  which makes the **TS-3 fix load-bearing**: today a named-map or typed-array
  annotation makes code *slower*, so the escape route this policy depends on
  must become a genuine win (Tune doc pillars T-A/T-C).
- **C2 — Genuinely per-iteration-polymorphic sites stay at dispatch cost.**
  Measured small (closed switch), and rare in Lambda's
  columnar/homogeneous-leaning workloads.

### Consequences

- No IC machinery is added to the Lambda-lane transpiler
  (`transpile-mir.cpp`); no IC state in Lambda-lane MIR modules.
- Dual-func §10 multi-version dispatch is a short **guard chain**, not a
  patchable cache.
- DF16 guard hoisting is the sanctioned intra-function "multiple paths"
  mechanism for partially typed loops.
- Optimization pressure on open sites goes to DF12 (speculative inference)
  and §10, not to caching.

---

*Future compilation-strategy ADRs (tiering, AOT/sealed-module boundaries,
lane interactions) land here as LC2+.*
