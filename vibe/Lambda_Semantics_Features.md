# Lambda Foundation & Features — Polyglot Runtime, Six-Language Comparison & Gap Analysis

**Status:** discussion input — seed for a post-semantics feature roadmap
**Date:** 2026-07-05 (Parts 2–5); Part 1 added 2026-07-06
**Context:** written after the formal-semantics review concluded (decision ledger C1–C12 in `Lambda_Semantics_Formal.md` / `Lambda_Semantics_Formal2.md`; implementation plan in `Lambda_Semantics_Impl_Plan.md`). Part 1 records the foundation discussion on Jube as a polyglot runtime — prior art (JVM, .NET, BEAM, GraalVM), the Jube design decisions (ledger J1–J6), and the runtime-level gaps. Parts 2–5 compare post-ledger Lambda against six scripting languages, catalog what Lambda lacks, what it should adopt, and assess overall completeness. Intended for detailed discussion in follow-up sessions.

**Probe evidence obtained for this comparison** (verified against `lambda.exe`):
- `set([1,2,2,3])` → **`failed to resolve native fn_set`** — `set()` is documented in the cheatsheet but **unimplemented** (A10 aspirational-docs family). No set type exists; `x in arr` membership is O(n).
- `for (x in [...] group by ...)` → syntax error — **no `group by`** in for-clauses (clauses today: `let`, `where`, `order by`, `limit`, `offset`).
- `$"a{1+1}b"` → syntax error; `"a{1+1}b"` → literal — **no string interpolation** in any form.

---

## Part 1 — The Polyglot Runtime Foundation (Jube)

### 1.1 Category: a curated polyglot runtime, not a platform

JVM, .NET, and BEAM are **platforms**: they publish a contract (bytecode format, type system, ABI) and third parties bring languages to it. Jube is a **curated polyglot binary**: five front-ends (Lambda, JS/TS, Python, Bash, Ruby), all in-tree, all transpiling onto a shared substrate — the Item value model, the MIR JIT, and one GC. Jube's external contract is *source text + host API* (`JubeHostAPI`), not a portable IR.

The closest real peer is therefore not the JVM but **GraalVM** (many front-ends, one JIT, one binary — see 1.5), or in spirit even Busybox: one small executable, many tools. The dividing line between "runtime" and "platform" is thin — but it is thin *from the platform side*: the platforms keep converging toward what a small coherent runtime already is (static binary, curated language set, data-level interop; GraalVM's Native Image is years of effort spent reaching the static closed world where Jube starts). Jube stays on the runtime side; its only platform-like surfaces are the native module ABI and an eventual embedding API (decision J6).

### 1.2 Prior art — JVM: compilation depth and the IR contract

The JVM's superpower is the classfile contract plus 25 years of tiered, speculative, deoptimizing JIT (C1/C2/Graal, profile-guided). MIR is deliberately the opposite bet: tiny, fast-compiling, ~70–80% of GCC -O2 performance, no tiering, no deoptimization, no OSR, no profile-guided speculation.

For Lambda-the-language that trade is fine — the transpiler emits fairly honest typed code. For the *guest dynamic languages* it bites: the JVM answered "dynamic language on a static VM" with `invokedynamic` and polymorphic inline caches; the LambdaJS performance analysis independently found IC-as-C-call and the fat call path as top bottlenecks. Without a deoptimization mechanism, the speculative tricks (assume shape, bail on miss) that make dynamic guests fast on JVM/V8 are unavailable. This is a **structural ceiling, not a tuning issue** — accepted under J5, recorded as gap G5.

The flip side: Jube starts in milliseconds at ~16 MB static, which the JVM still cannot do without AOT gymnastics.

**Lessons taken:**
- Invest in the runtime's *honesty* before its features — the JVM's precise GC (stack maps) is what everything else stands on. Jube's equivalent debt is gap G1.
- Do **not** envy the speculative JIT. Under the J5 positioning, guests are on-ramps, not production hot paths; chasing V8/HotSpot for them is a non-goal.

### 1.3 Prior art — .NET/CLR: the shared type system

The CLR's underrated idea is not CIL, it is the **CTS** (Common Type System): a neutral type system all languages must map onto, so interop is *by construction* rather than by marshalling.

Jube has a real analog, and it is the most platform-like thing in the design: the **Item/Mark data model as the lingua franca**, reinforced by the native-module decisions — signatures written in Lambda type syntax (`fn`/`pn` prefixed), native C structs crossing as VMap projections. That *is* a CTS — one defined by the C1–C12 ledger rather than by ECMA-335.

The difference is telling: the CTS is an **object** model (identity, methods, inheritance); Items are a **data** model (values, elements, symbols, datetime, decimal — document-native). For Lambda's domain that is a better interop currency: passing a parsed HTML element between JS and Lambda is richer than any `System.Object` interop story.

The cost is the one F# pays on the CLR: guest languages whose semantics don't match the substrate have permanent impedance. Python and Ruby have reference semantics (aliasing, in-place mutation); Lambda has value semantics (C4). That impedance lives in-tree forever and needs documented projection rules per guest (gap G7).

**Lessons taken:**
- Guard the coherence of the Items "CTS" — the C1–C12 ledger is that guardrail; every interop feature must map onto it, never around it.
- Specify the projection rules at the boundary explicitly, per guest language, rather than leaving them as emergent transpiler behavior.

### 1.4 Prior art — BEAM: concurrency as the owned property

BEAM is proof that a small, unglamorous VM (its JIT arrived only in OTP 24 and is template-based — no speculative optimization) can dominate a domain by owning **one architectural property**: cheap isolated processes with per-process heaps, preemptive scheduling, supervision trees, hot code reload, and distribution.

The precondition for that property is **immutable values with no shared mutable state** — exactly what C4 gives Lambda. Lambda has already paid the semantic cost that makes BEAM-style concurrency safe and cheap (per-"process" arenas, copy-or-COW message passing, no global locks on the value heap) and currently collects zero benefit, because there is no concurrency at all (Part 3's biggest structural absence).

BEAM also validates the polyglot-on-small-VM model: Elixir, Gleam, and LFE thrive on BEAM precisely because the value model is small and immutable, making new front-ends tractable. Jube has the same property. And Gleam supplies the positioning lesson behind J5: it never claimed to *be* Erlang — and thrived.

**Lessons taken:**
- BEAM's way **is** the concurrency model Lambda is designed after — now stated explicitly as decision J4 rather than left implicit.
- The "one owned property" strategy: Jube's domain analog of BEAM's telecom niche is document/data processing; concurrency-on-values is the property no peer can match (see 1.5 — GraalVM structurally cannot).

### 1.5 Prior art — GraalVM/Truffle: the opposite bet

GraalVM is the only other serious attempt this decade at "many languages, one runtime, one binary" — and it made the *opposite* choice from Jube on almost every axis. It is two ideas, often conflated:

1. **Truffle** — each language is written as a self-optimizing AST interpreter; Graal partially evaluates *interpreter + program* into machine code (the first Futamura projection, industrialized). Every language gets speculative optimization, inline caches, and deoptimization for free from one JIT.
2. **The interop protocol** — languages do **not** share a data model. A Ruby string stays a Ruby string; when Python touches it, it sends standardized messages (`READ_MEMBER`, `EXECUTE`, `IS_STRING`, …) that the JIT specializes away. Interop is a *protocol between foreign objects* — a small mandatory core plus optional capabilities — not a common type system.

The philosophical split is clean: **GraalVM optimizes for fidelity** — run the *real* language, full semantics, C extensions and all, at any engineering cost. **Jube optimizes for coherence** — project every language onto one value model (Items/VMap + C ABI) and keep the runtime small enough for one team to own.

**GraalVM as evidence for J5:** GraalPy and TruffleRuby are the best-funded, best-engineered guest-fidelity attempts ever made — Oracle Labs, world-class compiler engineers, a decade-plus each — and they remain at "mostly compatible, some C extensions, forever chasing." If fidelity costs that much *with* Truffle's machinery, a transpile-onto-Items architecture should not even gesture at it.

**GraalVM's weakest area is Jube's opening:** concurrency. Truffle guests inherit JVM threads, then GraalPy/TruffleRuby re-impose global locks to protect guest semantics. Nobody in the polyglot space has isolated-heap, message-passing concurrency — because nobody else has value semantics as the substrate. "Five languages, one binary, BEAM-style actors over immutable values" is a sentence no one else can say.

**Worth stealing (none requires Truffle's machinery):**
1. **Capability-style interop ops for VMap.** Truffle's protocol works because it has a small mandatory core plus optional capabilities. The VMap vtable's known gaps (has/delete/enumeration order) should be designed as an extensible capability set, not a fixed struct.
2. **The embedding API as a first-class product.** GraalVM's `Context` API (create context → eval source → get/put values, with resource limits and sandboxing) is the cleanest thing they built. Jube's equivalent — a small C embedding API — deserves a page-one contract, because the Electron-replacement story (J5) *is* an embedding story: Radiant hosting a guest-language backend.
3. **The closed-world confirmation.** Native Image took years to escape JVM startup and reach a static, closed-world binary. Jube starts there. Do not drift toward platform-hood to imitate the side that is converging toward you.

**Explicitly not taken:** the compat chase (see J5) and speculative-JIT envy (see 1.2, G5).

### 1.6 Cross-runtime scorecard

| Axis | JVM | .NET | BEAM | GraalVM | Jube |
|---|---|---|---|---|---|
| Interop contract | bytecode + object model | CIL + CTS | BEAM files + terms | message protocol between foreign objects | **source + Items/VMap + C ABI** (J1/J2) |
| Guest semantics | full (via bytecode) | full (via CTS mapping) | full (compile to BEAM) | full fidelity (goal: real PyPI/npm) | **dialect, projected onto Items** (J5) |
| JIT | tiered, speculative, deopt | tiered + AOT (R2R/NativeAOT) | template JIT (OTP 24+) | partial evaluation + deopt | single-tier MIR, no deopt |
| GC | precise, generational, stack maps | precise, generational | per-process heaps | JVM GCs | conservative-ish rooting, open issues (G1) |
| Concurrency | threads + Loom | threads + async/await | **the whole point** | JVM threads + guest GILs | none yet; BEAM-style is the model (J4) |
| Startup / footprint | poor / huge | medium | good | dreadful (JIT mode) / Native Image at 100s-MB | **~16 MB static, instant** |
| Cost to add a language | high (third-party) | high (third-party) | moderate (third-party) | enormous (TruffleRuby ≈ a decade) | weeks–months (in-tree transpiler) |
| Identity across languages | preserved (objects) | preserved (objects) | copied (terms) | preserved (zero-copy foreign refs) | projection/conversion at boundary |
| Interop currency | objects | objects | terms | protocol messages | **typed documents/values — unique** |
| Governance | Oracle/OpenJDK | Microsoft/.NET Foundation | Ericsson/OTP team | Oracle (GFTC) | in-house |

### 1.7 Jube design decisions (ledger J1–J6)

**J1 — No portable intermediate opcode format.** Jube specifies no distributable IR. MIR is a private, in-memory IR free to evolve; scripts and packages distribute as **source text only** (reaffirms the native-module-design decision; local compiled caches keyed by runtime build ID are fine). Consequence accepted: no binary ecosystem; cross-language linking is in-memory (`register_dynamic_import`).

**J2 — Interop = common data model + C ABI at function level.** The .NET-CTS analog is the Item/Mark data model: values cross languages as Items, native structs as VMap projections, and functions interop over a plain C ABI with signatures declared in Lambda type syntax (mandatory `fn`/`pn` prefix). **Each language front-end does its own transpiling** onto this substrate — no shared bytecode, no shared object model, no interpreter framework.

**J3 — Errors are return values at the C ABI; no unwinding crosses the boundary.** No `longjmp`, no C++ exceptions, no guest-exception propagation across the C API. **You cannot `raise` in Lambda and catch in JS**: an error crossing the ABI arrives as an error value (ItemError / `T^E`-shaped), and each front-end maps it to its native surface strictly on its own side — JS may rethrow it as a JS exception *inside* the JS runtime; Lambda receives guest failures as `T^E`. Rationale: unwinding across JIT'd MIR frames and mixed-language native frames is the classic source of leaks and UB; error-as-value keeps the ABI total, matches Lambda's own error model, and keeps the safety analyzer honest. (Per-language mapping tables still needed — gap G3.)

**J4 — Concurrency model is BEAM's, stated explicitly.** Isolated heaps (actor/process-per-arena) over immutable values; message passing by copy or COW; no shared mutable state; supervision as the error-handling frame at the process level. Not yet implemented — recorded now so that every intervening design (streaming pipes, atoms/ref-cells, module ABI, embedding API) must remain compatible with per-actor isolation.

**J5 — Guest languages are dialects: limited legacy support by design.** No 100%-compatibility claim for Python, Ruby, Bash, or JS/Node — GraalPy/TruffleRuby (1.5) prove that claim is a bottomless pit even with far better machinery. Intended uses:
- **POC / disposable vibe-coded environments** before a user formally embarks on Lambda.
- **Legacy on-ramp** — e.g. a project with an existing Python backend and a web frontend that wants a desktop UI: Radiant instead of Electron, with the Python code running in-process.

Every compat bug is triaged against this positioning (current honest datum: Node baseline 1492/3517 ≈ 42%). Precedent: Gleam never claimed to be Erlang.

**J6 — Stay a curated runtime, not a platform.** Language front-ends are in-tree only; there is no third-party language-front-end contract. The only public contracts are the native module ABI (`JubeHostAPI`, versioned additive-only, N-API model) and an eventual C embedding API.

### 1.8 Gaps in Jube as a polyglot runtime

Ranked; G1 gates everything below it.

- **G1 — GC rooting precision (the foundation crack).** JVM/.NET solved dynamic-language-with-JIT via precise stack maps; Jube's open rooting issue (`vibe/Lambda_GC_Root_Issue.md`: blanket `MIR_T_I64` rooting that is simultaneously load-bearing and wrong; BUG-001's use-after-free across `array_end`) is exactly the class of bug stack maps eliminate. Concurrency (J4), streaming, and more guest surface all stack on GC correctness. **The single highest-priority foundation work item, ahead of the Part 5 feature agenda.**
- **G2 — Rooting across the C ABI.** Who roots an Item held by a guest-language or native-module frame? The same crack as G1 one level up. Must be an explicit clause of `JubeHostAPI` v1, not emergent behavior.
- **G3 — Error-mapping tables unspecified.** J3 fixes the *mechanism* (return values); the per-language *mapping* does not exist yet: how ItemError surfaces in JS/Python/Ruby (exception class, code/message fidelity), how a guest exception converts to an error value, and what survives a round trip. One table per front-end, in the module/ABI spec.
- **G4 — Concurrency unimplemented.** The model is stated (J4); nothing is built. The value-semantics dividend — and the differentiation against GraalVM (1.5) — remains unclaimed. **Design now drafted:** `Lambda_Concurrency_Design.md` (four-level model, ledger K1–K9).
- **G5 — Dynamic-guest performance ceiling.** Single-tier MIR, no deopt, no speculation (1.2). Accepted under J5, but should be documented as a **non-goal** so no one burns time chasing V8 for guest code.
- **G6 — Embedding API absent.** No page-one C embedding contract (create runtime → eval source → exchange Items → resource limits). The Radiant-desktop / Electron-replacement story of J5 depends on it.
- **G7 — Reference-vs-value semantic impedance.** Python/Ruby aliasing and in-place mutation projected onto C4 value semantics needs documented projection rules per guest: what aliases, what copies, when — currently emergent transpiler behavior.
- **G8 — Compat scope undeclared per guest.** J5 needs concrete per-language "supported subset" statements users can read before choosing Jube for legacy code (the Node baseline number is the model: measured, published, honest).

---

## Part 2 — Per-language comparison

### 2.1 R (functional, statistical)

**Lambda lacks:**
- **The data frame with a verb grammar.** R's enduring contribution is not the tabular type but dplyr's *algebra* of operations (`group_by → summarize → arrange → join`). Lambda has vectorized arithmetic, the stats functions (`mean/median/variance/quantile`), and `Lambda_DataFrame.md` on the roadmap — but no grouped aggregation anywhere in the language.
- Distribution/RNG family beyond `math.random` (densities, quantile functions, samplers).

**Adopt:** the data-frame verbs, expressed over Lambda pipes — they map beautifully:
`df |> where(...) |> group('dept') |> {dept: ~#, avg_pay: avg(~.salary)}`.

**Do NOT adopt:** vector recycling (wraparound reuse of shorter vectors — a famous silent-bug source; Lambda's stricter broadcasting is right); per-type NA (`NA_integer_` etc. — Lambda's single null + C5 absence model is cleaner); R's scoping/NSE quirks.

**Lambda is ahead on:** semantic coherence (C1–C12 vs. R's accreted semantics), performance (JIT vs. R's interpreter), the document stack. R wins purely on statistical library depth.

### 2.2 Clojure (functional, immutable)

Lambda's closest philosophical relative post-review: both chose immutable values, code-as-data (C9), errors-as-values.

**Lambda lacks:**
1. **Lazy sequences** — infinite/streaming computation; pipes that don't materialize.
2. **A real set type** — first-class sets with O(1) membership (Lambda's is vapor, see probes).
3. **Atoms** — and this is a gift waiting: when Lambda eventually needs the ref cells C4 deferred, Clojure's atom (`swap!(cell, pure_fn)`) is *the* proven design compatible with value semantics — immutable values, identity confined to cells, mutation as pure-function application. Record as the presumptive future ref-cell design.
4. Multimethods/protocols (dispatch polymorphism) — probably fine to skip at Lambda's size; `match` covers most of it.

**Already covered:** clojure.spec → Lambda's validator (better-integrated: same type language as the runtime); EDN → Mark format; macros → deliberately deferred (C9: runtime construction first, compile-time later if ever).

**Interesting but niche:** metadata-on-values (`with-meta` — annotations that don't affect equality; could serve document provenance/lineage in pipelines); transducers.

### 2.3 XQuery (functional, document-native)

The spiritual kin — document model, set orientation, FLWOR.

**Lambda lacks:**
- **`group by`** — Lambda's for-clauses *are* FLWOR (`let`/`where`/`order by`/`limit`/`offset`) except this one clause, which XQuery 3.0 added because set-oriented processing is crippled without it. **The single most glaring omission given Lambda's C5 set-oriented identity.**
- **Axes** — Lambda's queries (`?`, `.?`, `[T]`) navigate *downward only*; XPath's `parent::`, `ancestor::`, `following-sibling::` have no equivalent. **Design tension to resolve before implementing:** parent axes conflict with C4 value semantics (a child value doesn't know its parent). The principled fix is query results that carry *paths* (or zipper values), never parent pointers. Needs a design note before someone hacks pointers in.

**Already covered:** typeswitch → `match`; schema awareness → validator; XQuery Update → `edit`/MarkEditor; node comparison semantics → C8/C8.6-R (Lambda's is more rigorously specified).

### 2.4 Bash (procedural, process-oriented)

**Nothing to adopt semantically** — Lambda's typed pipes are the correction Bash needs, and Nushell (below) is Bash-corrected. But Bash defines the completeness bar for "scripting" = **process orchestration**, where Lambda is thin:
- Ergonomic process invocation with structured capture (`{stdout, stderr, exit}` as a map/element, not text).
- Environment-variable access.
- Globbing.
- Script arguments (`$1`, flags).

Lambda has `cmd(c, args)` and `io.*` — the primitives exist; the ergonomic layer doesn't.

### 2.5 Nushell (procedural, structured-data shell)

The most instructive comparison: Nushell *is* "set-oriented scripting over structured, typed pipes" — Lambda's cousin, built shell-first.

**Adopt:**
1. **Streaming pipelines** — Nushell pipes are lazy: `open big.json | where size > 1mb | first 10` never materializes the file. Lambda pipes materialize arrays. For the set-oriented, document-scale identity, streaming evaluation is the architecturally right upgrade (see Part 3 — this is the same gap as Clojure's lazy seqs and Python's generators, surfacing a third time).
2. **Commands return structured data** — `ls` yields a table, not text. Lambda's `cmd()`/`io.*`/sysinfo returning elements/maps (`Lambda_Sysinfo_Impl.md` is already in this spirit) completes the "everything is data" loop.
3. **Typed command signatures → CLI for free** — a script's `pn main(...)` typed params becoming flags/`--help`/completions automatically.
4. `par-each` — parallel pipelines (see concurrency, Part 3).
5. Span-rich error rendering (miette-style) — Lambda has structured error codes; the *presentation* ergonomics are worth stealing.

### 2.6 Python (procedural, the completeness benchmark)

**Lambda already equal or better:** comprehensions (for-expr), slicing, named/default/variadic params, closures, error model (`T^E` with compile-time enforcement beats exceptions), equality/truthiness sanity (post-C1–C12, decisively — Python still has `0 == False`, `[] or default` traps, dict-order folklore), document/format stack, raw speed.

**Lambda lacks:**
1. **Generators (`yield`)** — laziness, third appearance.
2. **`try/finally` / `with` context managers** — a real hole: `^`-propagation has **no cleanup hook**, so a function that opens a resource and propagates an error leaks it. Some `defer`- or `with`-shaped construct is required for serious I/O scripting.
3. **f-strings** — confirmed absent; `"Hello " ++ name ++ "!"` fatigue is real and daily.
4. **User-level assert/testing** — nothing exists for user scripts. Note: the DSL proposal's `// expect:` formalization could become exactly this (a checked `expect` statement) — two birds.
5. **Stdlib breadth + package ecosystem** — a time problem, not a design problem; noted for completeness.

**Skip:** decorators (HOFs cover), operator overloading via dunder (deliberate non-goal), inheritance-heavy OO.

---

## Part 3 — Synthesis

### 3.1 Structural gaps (multiple languages converge; Lambda has none of each)

| Gap | Pointed at by | Notes |
|---|---|---|
| **Concurrency / parallelism** — no spawn, async, channels, or parallel pipes at all | Python, Clojure, Nushell, Bash | The biggest architectural absence. Good news: C4 value semantics is the *ideal* substrate — no shared mutable state to race on (the Clojure lesson). Coherent minimal design: a `par` pipe variant + Clojure-style atoms as the deferred ref cells. The runtime-level model is now recorded as J4 (Part 1): BEAM-style isolated heaps over immutable values; full design in `Lambda_Concurrency_Design.md` (levels 0–3, ledger K1–K9). |
| **Lazy / streaming sequences** | Clojure (lazy seqs), Python (generators), Nushell (streaming pipes) | Three independent designs converge. Natural fit for set-oriented, document-scale processing; pipes are the obvious surface. Design care needed with `pn` effects and C4. |
| **Resource cleanup** — no `finally` / `defer` / `with` | Python + every systems language | `T^E` + `^` propagation leaks resources on the error path. Genuinely missing, not stylistic. |
| **`group by`** | XQuery 3.0, R (dplyr), SQL heritage | Confirmed absent. The glaring hole in an otherwise-complete FLWOR clause set, and prerequisite for the data-frame verbs. |

### 3.2 Ergonomic gaps (surface-level, individually small, collectively the "feels incomplete" layer)

- **String interpolation** (confirmed absent) — `$"..."` or equivalent.
- **Set type** (confirmed vapor — `set()` documented, unimplemented; O(1) membership needed).
- **Process/env/args ergonomics** — the Bash/Nushell bar: structured `cmd()` results, env access, globbing, script flags.
- **User-level `assert`/test blocks** — possibly unified with the `// expect:` formalization.
- **Upward/lateral document axes** — via path-carrying query results or zippers, never parent pointers (C4 tension).
- **Distribution/RNG library** (R bar), broader stdlib (Python bar) — accretion over time.

### 3.3 Best ideas to steal — one per language

| From | Idea | Fit |
|---|---|---|
| R | Data-frame verb grammar over pipes | rides `\|>` + `group by`; `Lambda_DataFrame.md` exists |
| Clojure | **Atoms** as the future ref-cell design | the C4-compatible concurrency/identity primitive |
| XQuery | `group by` for-clause | completes FLWOR; unlocks the R verbs |
| Nushell | Streaming pipes + structured command output | the set-oriented identity, made scalable + shell-capable |
| Python | `with`/`defer` resource management | closes the `^`-propagation leak |
| Bash | (only the lesson Nushell already learned) | typed pipes over text pipes — Lambda already has it |

### 3.4 What Lambda has that none of the six have

For fairness and morale: the document-native element model with multi-format I/O (13+ input formats, validator, formatters); schema validation in the same type language as the runtime; `T^E` compile-enforced error values; mutable value semantics in a scripting language (only Swift-family compiled languages otherwise); type/value pattern composition (`[1, int, "str"]` — only TypeScript comes close); a JIT; a CSS layout/rendering engine attached; and — after C1–C12 — a semantic core with a written decision ledger, which none of the six possesses (Python has PEPs; nobody has the losing arguments recorded).

---

## Part 4 — Completeness verdict

**As a data/document-processing language: complete, and ahead of every peer here.** None of the six combines Lambda's data model, format coverage, validation, typed pipes, performance, and (now) semantic rigor.

**As a general scripting language: not yet — and the missing pieces are nameable and finite:**
- Structural: concurrency · laziness/streaming · resource cleanup · `group by`.
- Surface: interpolation · sets · process/CLI/env ergonomics · assert.

The key strategic observation: **none of the four structural gaps conflicts with the C1–C12 core** — and value semantics actively *enables* the two hard ones (concurrency and laziness have no shared-mutable-state hazards to fight; Clojure proved this a decade ago). The core was built in the right order: semantics first, then features that inherit its guarantees.

---

## Part 5 — Discussion agenda for the follow-up session

1. **Prioritize the four structural gaps** — suggested order: `group by` (small, unlocks data frames) → resource cleanup (`defer` vs `with` vs error-path hooks on `^`) → streaming pipes (design: lazy `|>` by default vs explicit `stream` source vs generator functions) → concurrency (a `par` pipe + atoms; scope: data parallelism first, async I/O later?). Note the runtime-side ordering from Part 1: G1 (GC rooting precision) precedes all of these.
2. **`group by` syntax sketch** to react to: `for (x in sales group by x.region into g) {region: g.key, total: sum(g |> ~.amount)}` — or the XQuery style with implicit grouping variables.
3. **Cleanup construct**: `defer expr` (Go-style, pn-only) vs `with resource as r { ... }` (Python-style, auto-close protocol) — interaction with `^` propagation and `T^E` must be specified either way.
4. **Streaming vs C4**: lazy values crossing `var` mutation boundaries; are streams values (COW-able?) or a distinct non-value resource type?
5. **String interpolation syntax**: `$"..."` vs `` `...` `` vs `"...{}"` — grammar-conflict check needed (`$` is also the proposed quote-splice marker from C9a — same sigil, two contexts, decide compatibility).
6. **Set type**: distinct container rank (C11 order impact!) vs map-with-unit-values; literal syntax or constructor-only.
7. Whether `assert`/`expect` unifies with the DSL proposal's `// expect:` formalization.
