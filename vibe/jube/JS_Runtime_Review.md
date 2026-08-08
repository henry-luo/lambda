# LJS Runtime Implementation Review — Complexity, Quality, Simplification

**Date**: 2026-08-07  **Status**: REVIEW — findings and a simplification
roadmap; no code changed
**Tree anchor**: master `b9b30f4ac`
**Companion docs**: `vibe/jube/JS_Profiling_Helpers.md` (measured time/call
data), `vibe/jube/JS_Tune1_Helpers.md` (performance proposal; overlaps §6 here)

**Method and limits.** LJS numbers are measured on this tree (`wc`, greps,
declaration-gap function lengths — marked ≈ where approximate, since C++
declaration spacing overstates a function that is followed by file-scope
statics). QuickJS is compared from its published architecture and source layout
(Bellard's `quickjs.c`, ~54k lines, plus `libregexp`/`libunicode`/`cutils` ≈
6k); it is **not vendored here**, so no line-level claims are made about it.
The two engines have different missions (§2); every comparison below says
which differences are mission and which are not.

---

## 0. Verdict

Yes — QuickJS is substantially cleaner and simpler *per feature*, and the gap
is quantifiable: **~125k lines of LJS core (runtime + compiler) versus ~60k
for all of QuickJS**, with QuickJS covering more of the spec. But the gap has
two distinct components:

1. **Essential complexity LJS chose deliberately** — a MIR JIT instead of a
   bytecode interpreter, precise-rooted GC instead of reference counting, and
   JS layered over the shared Lambda `Item`/`Map` data model instead of a
   JS-native object model. These are mission costs (§2). QuickJS's cleanliness
   partly reflects the *absence of these goals*, not only better engineering.
2. **Accidental complexity from accretion** — string-keyed builtin dispatch
   through mega-switches, a 54-entry-point property-access surface, five
   parallel type discriminators, sentinel-property conventions, fixed-capacity
   tables, and a poll-based exception side channel (§3). None of these are
   required by the mission, all of them show up in the performance profile,
   and together they are the honest answer to "is it overcomplicated".

The single most telling measurement (from the profiling pass): on the
library-heavy suite, **JIT-generated code is 0.6% of runtime** — all semantic
work happens in the C helpers, dominated by exactly the dispatch layers listed
in §3. LJS currently carries JIT-grade complexity while performing, on that
workload, like a dispatch-bound runtime. The simplifications in §6 and the
Tune1 proposal attack the same layers for the same reason.

## 1. Quantitative shape

`lambda/js/` totals **228,556 lines**. Bucketed (approximate; boundaries by
file purpose):

| Bucket | ≈ Lines | Largest members |
|---|---:|---|
| Core semantic runtime | ~75k | `js_runtime.cpp` **40,730**; `js_globals.cpp` 18,952; `js_typed_array` 3,619; regex 3,873; props/attrs/state/value/scope/early-errors ~8k |
| Compiler (AST → MIR lowering) | ~50k | `js_mir_*.cpp` **44,663**; `build_js_ast.cpp` 4,786 |
| Node/DOM/platform compat | ~85k | `js_dom*` 21.6k; streams/net/http/tls/fs/crypto/child_process ~41k; assert/util/buffer/zlib/readline/dns etc. |
| Infra (profiling, event loop, catalogs) | ~8k | `js_event_loop` 1,960; `js_exec_profile` 754; catalog 943-line `.def` |

QuickJS for scale (published layout, not measured here): one ~54k-line
`quickjs.c` containing parser, bytecode compiler, interpreter, refcount GC with
cycle collector, and all builtins; ~6k of regexp/unicode/util support; near-
complete ES2023. The **fair core-to-core comparison is ~125k vs ~60k** — the
compat bucket is excluded because QuickJS has no equivalent, and that exclusion
is generous to LJS in the other direction too (QuickJS's 60k includes full
Proxy/Reflect/BigInt and a test262 conformance level LJS's curated `js262`
gate does not claim).

Concentration is a problem independent of totals: `js_runtime.cpp` at 40,730
lines is ~two-thirds the size of *all of QuickJS* and contains promises,
property machinery, arrays, strings, maps/sets, classes, iterators, builtin
dispatch, and regex glue in one translation unit. Largest functions
(declaration-gap ≈):

| Function | ≈ Lines | Location |
|---|---:|---|
| `js_dispatch_builtin` | 2,481 | `js_runtime.cpp:11178` |
| `js_map_method_impl` | 1,597 | `js_runtime.cpp:20866` |
| `js_array_method_impl` | 1,234 | `js_runtime.cpp:1389` |
| `js_property_get` | 1,230 | `js_runtime.cpp:4693` |
| `js_create_regex` | 1,028 | — |
| `js_string_method` | 982 | `js_runtime.cpp:23581` |

## 2. Essential complexity — what the mission buys and costs

Three structural costs QuickJS avoids by *goal selection*, and LJS cannot:

- **JIT versus interpreter (D8.1.1).** The 44.7k-line lowering side (type
  inference, IC emission, scalar-home coloring, safepoint planning, exception-
  poll tracking) is the price of native-code numeric performance; the bench
  suite (Tune7–Tune12 line) shows it paying off on compute kernels. QuickJS
  reads bytecode and pays interpreter dispatch on every operation, always.
- **Precise GC versus refcounting (D5.2, D5.3).** **863**
  `RootFrame`/`Rooted` uses across `lambda/js` are ceremony QuickJS trades for
  refcount traffic on every value copy plus a cycle collector. Both are real
  costs; LJS's is louder in source, QuickJS's is invisible and paid at run
  time. Given rule 15 (no conservative stack scanning), this ceremony is
  load-bearing and correct to keep.
- **Shared data model (D2.1).** JS objects *are* Lambda `Map`s with `TypeMap`
  shapes so that documents, Lambda scripts, Radiant, and JS share one heap and
  one value representation. QuickJS's `JSValue`/`JSObject`/`JSAtom` were
  designed for JS alone. The premise is sound for this project — but §3.3
  shows where its implementation leaked.

Also structurally fine: the helper-effect catalog (D7.4.3) and the S#/D# spec
ledger have no QuickJS equivalent and are genuine engineering assets.

## 3. Accidental complexity — four findings

### 3.1 Builtin dispatch by string, through mega-switches

Builtins are not function objects installed on prototypes. They are
**(receiver kind, method-name string)** pairs: the catalog
(`js_builtin_catalog.def`, ~930 rows) maps names to ids via
`js_builtin_catalog_find` (`js_builtin_catalog.hpp:189` — string compare
search, visible in the profile), and execution flows through id switches —
`js_dispatch_builtin` (≈2,481 lines) — or per-kind name-switches
(`js_string_method`, `js_array_method_impl`, `js_map_method_impl`, together
another ≈3,800 lines). **510 `strcmp`/`strncmp` calls in `js_runtime.cpp`
alone** are mostly this.

QuickJS's model: builtins are C-function objects on prototype chains; method
dispatch *is* property lookup + call — one mechanism, already optimized,
naturally correct under monkey-patching, method extraction, `.call/.apply`,
and subclassing. LJS re-derives those behaviors with special cases inside the
switches. This is the largest single cleanliness and correctness-risk gap
versus QuickJS, and it is not mission-driven: nothing about the JIT, the GC,
or the shared data model requires name-keyed dispatch.

### 3.2 The property-access surface: 54 entry points

Grep of definitions finds **54 property get/set/access entry points** across
the runtime: `js_property_get` (itself 1,230 lines), `_get_str`, `_access`,
`_access_key_ic`, `_access_named_ic{,_impl,_slow}`, `_set`, `_set_map`,
`_set_array`, `_set_function`, `_set_strict`, `_set_key_ic`,
`_set_named_ic{,_impl,_slow}`, `_set_on_primitive_base`,
`js_map_get_fast{,_ext}`, `js_get_shaped_slot`, `js_prototype_lookup{,_ex}`,
`js_find_shape_entry{,_key}`, `js_own_shape_slot_status`, three
`typemap_hash_lookup*` variants…

QuickJS: essentially `JS_GetPropertyInternal`, `JS_SetPropertyInternal`,
`find_own_property`, plus the exotic-object method table. Each LJS variant has
a birth reason (a tune phase, an IC generation, a receiver kind), but
superseded paths were never retired — the profile showed the **non-IC path
carrying 7.5× the calls of the IC path** (4.73M vs 0.64M), i.e. the newest
mechanism is not even the load-bearing one. Multiple generations coexist;
every new fast path adds an entry point without deleting one.

### 3.3 Five parallel type discriminators, and objects-by-convention

What kind of object something is, and how it behaves, is decided by up to five
stacked systems:

1. Lambda `TypeId` (`LMD_TYPE_MAP/ARRAY/FUNC/VMAP…`),
2. `map_kind` (`MAP_KIND_ITERATOR/TYPED_ARRAY/…`),
3. the `JsClass` stamp (44 entries, `js_class.h`),
4. `TypeMap` shape + `ShapeEntry` flags (including `__proto__` as a shape
   slot),
5. **sentinel properties**: `__promise_idx`, `__instance_proto__`,
   wrapper-map conventions.

QuickJS has one: `class_id` plus shape. Layers 1–2 are the shared-data-model
mission (§2). Layers 3–5 are accretion, and layer 5 is the worst. The
promise implementation is the case study (`js_runtime_state.hpp:684`):

- a **static table** capped at `JS_PROMISE_STATE_MAX` = **8,192** promises,
- **fixed `[8]` arrays** for reactions (`on_fulfilled[8]`, `on_rejected[8]`,
  `next_promise[8]`, `reaction_domain[8]`, `is_finally[8]`) — a hard cap on
  `.then` chains per promise,
- surfaced to JS as an ordinary Map carrying an `__promise_idx` integer
  property (`js_promise_to_item`), stitched to `Promise.prototype` by name,
- rooted by registering 7 ranges × 8,192 slots per heap epoch — the measured
  **7.1%-of-CPU** registration storm and Tune1's P1.

In QuickJS a promise is a heap object with growable reaction lists; the GC
sees it like anything else; there are no caps and no index-property identity.
The LJS pattern (static table + wrapper-map + sentinel property) appears
because first-class runtime objects were expensive to mint under the shared
data model — but the workaround now costs more than the capability would,
in performance, in semantic risk (9th reaction, 8,193rd promise), and in code.

### 3.4 Poll-based exceptions beside an in-band error type

The exception state is a runtime-state flag (`js_exception_pending`), checked
by **467 hand-written sites** in `lambda/js/*.cpp` and by **153,725 emitted
poll sites** in JIT code (24.9% of all dynamic helper calls), governed by
per-row catalog effects and the OE1–OE10 emission-time tracker built to elide
polls (DO15). QuickJS returns `JS_EXCEPTION` in-band; callers branch on the
return value; there is no poll and no tracker.

The measured *time* cost is now trivial (1ns/call, ≈0.2% — Profiling §4.1);
the cost is architectural: two error channels coexist (`ItemError` exists in
the value model per S-semantics error handling, and the pending flag lives
beside it), every helper's contract has an implicit side effect, and an
entire analysis (the tracker) exists to optimize a convention that in-band
signaling would delete. This is simplification debt rather than a perf lever.

## 4. Code quality — both directions honestly

**Strong, and better than QuickJS on some axes:**

- **Comment discipline is excellent.** Root-cause and invariant comments are
  the norm (`js_get_implicit_proto`'s chain contract, the promise epoch
  comment, scalar-home ownership notes). Enforced by rule 12, and it shows.
- **The spec ledger** (S#/D# rulings, vibe decision docs) and the
  **helper-effect catalog** (GC/exception effects per row, D7.4.3) give the
  runtime a written constitution QuickJS lacks.
- **Test/golden culture**: 327-test suite, js262 gate, GC-stress gate,
  deterministic batch (byte-identical counts across runs — measured).
- **Instrumentation**: the exec-profile facility, MIR volume stats, IC site
  telemetry — the engine can explain itself.

**Weak:**

- **Accretion without consolidation.** **85 tune-phase markers** (`P0`,
  `P10f`, `A1`, `Js54…`) in `js_runtime.cpp` memorialize layers that were
  added next to, not instead of, their predecessors (§3.2 is the structural
  result). The tune docs record *what* landed; nothing records *what should
  now be deleted*.
- **Symptom patches violating the project's own rule 1.** The P0 guard in
  `js_map_get_fast` (`js_runtime.cpp:3957` region) rejects "obviously
  invalid" corrupt `TypeMap` pointers at every lookup because one callback
  path once delivered garbage — a defensive check in the hottest lookup
  function, in place of the root-cause fix.
- **Fixed caps as a design idiom.** Promise table 8,192; reactions 8; domain
  stack; unhandled queue; profiler tables (1024-slot site table measured
  saturating and silently truncating). Each cap is a latent correctness cliff
  and each "once per epoch re-register the world" pattern is a latent
  performance cliff — both fired during the profiling pass.
- **Monolith files/functions** (§1): 40.7k-line TU; 1.2–2.5k-line functions.
  This is navigability and merge-surface cost, and it also defeats
  `-ffunction-sections`-level locality reasoning.

## 5. Code flow, traced

Property read `obj.name` on the hot generic path:

```
JIT code
 └─ js_property_access(obj, key)                  [entry-point choice made at lowering]
     ├─ TypeId dispatch (MAP / ARRAY / FUNC / VMAP / primitive base…)
     ├─ js_map_get_fast(m, chars, len)            [P0 corrupt-pointer guard]
     │   └─ typemap_hash_lookup → ShapeEntry      [hash probe; fallback: shape chain]
     ├─ miss → js_prototype_lookup
     │   └─ js_get_implicit_proto
     │       ├─ js_get_prototype                  [shape probe for "__proto__" slot]
     │       ├─ "__instance_proto__" probe        [raw-name intern per §Tune1-P2]
     │       └─ JsClass stamp → intrinsic proto table
     └─ repeat per prototype level
```

Method call `str.slice(1)` adds the §3.1 layer: receiver-kind inference at
lowering selects `js_string_method(str, name_item, …)`, which resolves the
*name string* through catalog search / strcmp chains before running the
implementation; the generic path goes `js_call_function` →
`js_dispatch_builtin(id, …)` mega-switch.

QuickJS equivalent flow: opcode → `JSValue` tag check → shape hash probe →
proto chain (same idea, one entry point) → for methods, the *found value is
the function*; calling it is uniform. The per-operation depth is similar at
the shape level; LJS adds (a) entry-point multiplicity before the lookup,
(b) name-string travel and catalog resolution for builtins, (c) class-stamp
synthesis inside the chain walk, (d) exception-poll emission after the call.
That delta is §3, not the mission.

## 6. Simplification roadmap (ranked)

Ordered by structural payoff; items 1–3 also subsume most of Tune1's targets.

1. **Builtins become real function objects on prototypes.** Install catalog
   entries as callable objects at realm init (the catalog rows already carry
   everything needed); method dispatch collapses into property lookup + call.
   Deletes/shrinks `js_dispatch_builtin` and the three `*_method_impl`
   switches (≈6k lines of switch bodies become per-builtin functions), removes
   name-string travel and most of the 510 strcmps, and makes monkey-patching /
   extraction / `.call` uniform instead of special-cased. Precondition: cheap
   function-object allocation (shares machinery with item 2).
2. **Promises become first-class heap objects.** Growable reaction lists,
   GC-owned, no static table, no `[8]` caps, no `__promise_idx` wrapper, and
   the Tune1-P1 rooting storm disappears *by construction* rather than by
   lazy registration. This is the template for retiring the
   objects-by-convention idiom generally (§3.3 layer 5).
3. **One property-access API with explicit tiers.** A single internal
   contract (`get/set(obj, key-ref, ic-slot?)`) with slot/IC/generic/exotic
   tiers behind it; every superseded variant is deleted, not deprecated.
   Tune1-P2's `PropertyKeyRef` plumbing is the natural vehicle; the 54-entry
   census above is the acceptance metric (target: single digits).
4. **Collapse type discrimination toward shape+class.** Fold sentinel
   properties into real class metadata first (they are the cheapest to remove
   and the most fragile), then audit whether `JsClass` and `map_kind` can
   merge into one discriminator carried by the shape (D3.4). Layers 1–2 stay
   (mission); the target is two discriminators, not five.
5. **Split `js_runtime.cpp`** along the seams that already exist as banner
   comments (promises, arrays, strings, maps/sets, classes, property
   machinery, dispatch). Mechanical, low-risk, high navigability payoff; do it
   *after* items 1–3 delete their share so code moves once. Rule 13's "promote
   to module header" applies to the statics that currently force same-TU
   placement.
6. **In-band errors (KIV, design decision).** Replacing the pending-flag
   convention with `ItemError`-sentinel returns would delete 467 manual
   checks, 153k poll sites, and the OE tracker — but it re-contracts all ~355
   emitted helpers and touches D1's boundary rules; it is a formal-spec
   decision (new D-ruling) rather than a refactor. Record it as the stated
   long-term direction so new helpers stop deepening the old convention.
7. **Institutional: pair every tune phase with a retirement list.** The tune
   docs should name what the new mechanism *replaces* and gate landing on the
   deletion (the §3.2 pattern is process-caused, and process can prevent it).

What **not** to change: the JIT (bench-justified; D8.1.1), precise rooting
(rule 15; D5.3), the shared data model itself (the project's reason to
exist), and the catalog/ledger infrastructure — the goal is QuickJS's
*uniformity*, not QuickJS's architecture.

## 7. Answers to the questions asked

- **Is it overcomplicated?** The core is ~2× QuickJS's size for a spec
  subset. Roughly half that gap is mission (JIT + precise GC + shared data
  model); the other half is the four accidental patterns in §3, which are
  removable without touching the mission.
- **Code quality?** Discipline artifacts (comments, ledger, catalog, tests)
  are excellent — better than QuickJS's. Structural hygiene (file/function
  size, entry-point proliferation, caps, symptom patches) is the weak side,
  and it is concentrated in exactly the oldest, hottest file.
- **Code flow?** Sound at the shape level; muddied above it by entry-point
  multiplicity and name-keyed builtin dispatch (§5's trace). The profile
  confirms the mud is where the time goes.
- **Can it be simplified overall?** Yes — §6 items 1–3 are the big ones, they
  overlap the performance plan (Tune1) rather than competing with it, and
  they move LJS toward QuickJS's one genuinely superior property: *one
  mechanism per concept*.

## 8. Open questions

1. Item 1 precondition: how cheap can a callable builtin object be under the
   shared data model — is a shared-shape function map with a C-pointer slot
   acceptable, or does D6.2's closure representation need a lighter builtin
   variant first?
2. Item 2: do wrapper identities leak today (`obj === promiseLikeMap`
   observable anywhere), i.e. is the migration observable-behavior-neutral?
3. Item 4: is `map_kind` reachable from user-visible semantics anywhere, or
   is it purely an internal storage discriminator that shape metadata could
   absorb?
4. For §6 item 6: does the Lambda-side `T^E` / `ItemError` semantics (S-layer
   error model) already define the sentinel contract JS helpers would need,
   or would JS require a distinct sentinel to keep pending-exception JS
   semantics separate per D7.4.3's "JS pending-exception behavior is not
   reused as another guest's exception model"?
5. What is the actual current test262 pass surface? A measured conformance
   number would anchor the "coverage subset" claim in §1 and size the risk of
   items 1–2.
