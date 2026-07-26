# Lambda Impl Plan: Tune 6 — Result12 Tail Elimination (LJS shape lookups + Lambda untyped maps + native math)

**Status: PARTIALLY IMPLEMENTED — 2026-07-26.** Track L is **complete**: L1
landed (1.15–1.54x), L2 landed (1.12–1.19x, richards 1.75x with L1), L3 landed
(~1.5% on nbody, gate missed). Track J: T0 done, J2 implemented + measured +
reverted, J1 deprioritised by the census, J3 scoped but not started.
**The T0 census overturned this plan's central premise, both L3 gates in §4.3
were wrong, and L2 exposed a silent-wrong-value hazard for any baked-in cache
cell — read §0.1 before doing any more work from it.**

---

## 0.1 Execution record and premise correction (2026-07-26)

**T0.1 census (DONE)** — release-profile build, raw data in `temp/t6_census_*.tsv`:

| bench | load_ic probes | hit mono | hit poly | miss | megamorphic |
|---|---|---|---|---|---|
| hashmap | 47,010,886 | 16,898,520 | 10,124,773 | 1,440,158 | **18,547,462** |
| richards | 1,116,046 | 838,095 | 277,842 | **109** | 0 |
| deltablue | 1,421,395 | 1,001,902 | 407,778 | 11,715 | 0 |
| crypto_sha1 | *(no load_ic rows)* | | | | property_set 424,527 / 42 sites |

**J1's premise is disproven.** The plan assumed prototype-chain misses dominate.
richards misses on **0.01%** of probes and deltablue on 0.8% — the PIC has almost
nothing to win there. What richards/deltablue actually pay is the per-hit C-call
tax on a ~100%-hit IC, which is **J3**, not J1. J1 is deprioritised: do not build
it until J3 lands and a fresh census shows proto misses actually dominating. Its
companion fix (re-key shaped ctor/devirt fast paths by TypeMap identity instead
of class-name strings, JS_07 §7.7) is independently worthwhile and can be
salvaged on its own.

**hashmap is megamorphic-dominated, but not for the reason §3.2 assumed.**
Root cause, traced with temporary instrumentation on the IC install and the
shape-detach paths:

- Structurally identical HashMap instances get **pointer-distinct TypeMaps** —
  one per `run()`. `Object.keys()` is identical across runs; the IC compares
  shape pointers, so 5+ shapes ⇒ permanent megamorphic (there is no re-warm
  once `JS_LOAD_IC_MEGAMORPHIC` is set).
- The divergence is **not** detachment: `T6_DETACH_TRACE` fired zero times.
  The shapes carry `is_shared_constructor_shape=0`, `is_transition_shared_shape=0`,
  `is_private_clone=0` — they never entered any sharing scheme at all.
- `map_put` (`lambda/input/input.cpp:474`) only consults the shared transition
  table when `map_type_is_shared_js_shape(map_type)` is *already* true. An object
  born unshaped from `js_new_object()` therefore grows a **private** TypeMap
  chain field by field, and every instance ends up with its own.

**So the real lever is a realm-owned shared root shape for plain objects**, so
that field-by-field growth flows through the existing (already correct, already
shared) transition table and structurally identical objects converge on one
TypeMap. `EmptyMap` is a process-global singleton and must NOT be used as that
root — hang it off `Input` so it dies with the realm's pool, per this plan's own
realm-ownership constraint. This is the highest-value remaining item; it is what
`hashmap` (worst LJS row), and probably `cd`/`havlak`, are actually waiting on.

**J2 (DONE → REVERTED, §3.2).** Implemented as specified
(`js_new_object_with_shape_cached` + per-site cell in `jm_transpile_object`).
It works: a literal receiver site went 199,996/200,000 megamorphic →
199,999/200,000 monomorphic. But it does not pay, because these rows'
megamorphism comes from constructed objects, not literals. Isolated A/B on
release (dedicated `LAMBDA_JS_SHARED_LITERAL_SHAPE` gate, ctor sharing on in
both arms, 3-run medians): havlak2 94,757 vs 90,976 ms (**−4%**),
jetstream/hashmap 67,549 vs 63,235 (**−7%**), splay +6%, navier +1%,
richards/deltablue/crypto_sha1 flat. Gate was "havlak ≥2x" ⇒ not met, net
negative ⇒ reverted per the plan's own demote-to-unshared rule. A comment at the
emission site records the measurement so it is not retried blindly.

Two things were kept from that work:
- `test/js/object_literal_instance_isolation.js` + `.txt` — 12-case instance
  isolation net (golden from Node), pinning what any future sharing scheme must
  preserve.
- A **real correctness fix** in `js_set_shaped_slot`: writing `null` to a slot a
  sibling had tagged `T` left the entry claiming `T`, so the null read back as a
  zero-valued `T` (e.g. `false`). Now detaches on T→NULL and retags; GC-safe
  because the detach clones before the retag.

**Pre-existing bug found, still open.** The same null-loses-its-tag defect exists
on the `fn_map_set` path and is reachable via ordinary constructor sharing with
no Tune6 code at all: `function P(v){this.x=v}` over `[false, null, false]`
yields `false,false,false` (Node: `false,null,false`). The suppression is
deliberate — `lambda/runtime/lambda-eval.cpp:6974` documents that downgrading a
shared ShapeEntry to NULL would make GC skip tracing sibling container pointers —
so the fix must detach first, exactly as the `js_set_shaped_slot` fix does.
Tracked separately; out of scope here because Tune6 is a no-semantic-change plan.

**L1 (DONE, landed, §4.1).** See §4.1 for the measured result. L1.2's hash-first
path proved **unnecessary**: hoisting `strlen` and switching the per-field
compare to the existing `typemap_shape_name_equals_id` (int name-id test first)
captured the win with zero semantic risk, so the ordered scan, last-writer-wins
and nested/spread recursion are all preserved by construction and the plan's
duplicate-key/unnamed-entry hazard never arises.

**Gates green at this checkpoint:** `test-lambda-baseline` 3617/3617,
`test262-baseline` 40261/40261 zero regressions, `make node-baseline` 2039/3550
— identical to clean master, verified by stashing (the stored node baseline is
stale and reports ~1178 spurious `REGRESS` rows on master too), `make lint` no
new findings.

**Suggested order from here:** shared-root transition shapes (new, §0.1) → J3 →
L2. J1 only if a later census justifies it. L3 is done.

**Measurement protocol note, learned the hard way in L3:** build both binaries
first, then interleave the runs benchmark-by-benchmark. Sequential
build-A/measure-A/build-B/measure-B drifted ~10% across all rows on this host
and manufactured a 1.05x that was really 1.017x. Guard rows that the change
cannot touch (matmul, ray) are the tell — if they move, the run is invalid.

---

**Original plan status: PLANNED — 2026-07-24.**
**Successor of:** `vibe/Lambda_Impl_Tune4.md` + `vibe/Lambda_Impl_Tune5.md`
(both IMPLEMENTED, exit = Result12). Result12 moved the geo means (MIR 4.73x→3.01x,
LJS 19.3x→15.1x) and proved the remaining tail is **not** GC, typed elements, or
array stores — it is property/shape lookup cost on both engines:
- **LJS**: every row still >100x (hashmap 1138x, havlak 579x, cd 254x, navier
  142x, spectralnorm 110x) plus the OO band (crypto_sha1 72x, deltablue 51x,
  richards 33x) is dominated by per-operation C-call property lookups —
  `Lambda_Tuning_Proposal.md` **R2/R3** and the **OI-6** PIC design record.
- **MIR**: every row >25x (richards 66x, splay 43x, crypto_sha1 31x, navier 26x
  — all the *untyped* `.ls` variants) is string-keyed map field access; nbody
  15x and the float family also pay boxed `math.sqrt` — **R6**.

**Naming note:** the completed `vibe/Lambda_Impl_JS_Tune (done).md` was informally
called "Tune-6" inside `Lambda_Tuning_Proposal.md`. This file supersedes that
number under the TuneN file series; the old plan is referred to below as
*JS-Tune (done)*.

**Diagnosis provenance:** Result12 (`test/benchmark/benchmark_results_v12.json`,
2026-07-24, commit `8eaaadca2` + uncommitted Tune5) + source verification of
every mechanism below, 2026-07-24. `file:line` refs verified same day; they
drift — confirm against symbol names before editing.

**Governing invariant.** Every phase is a pure performance change: no observable
semantic change to Lambda or JS programs. The permitted observable deltas are
timing, memory footprint, and GC counts. All caches added here are
**data-driven** (no code patching, no deopt — the single-tier JIT constraint,
OI-6/G5) and **realm/module-owned** (the reverted process-global prototype cache
leaked one realm's prototype into another; that failure class is a design
input). Gates at every phase boundary: `test-lambda-baseline` 100%, test262
baseline (40261) zero regressions, `make node-baseline` (3528) zero regressions,
MT7 emission budgets lifted only deliberately, forced-GC stress on every new
cache.

---

## 0. What Result12 measured, and who owns it

| Cluster | Result12 rows (ratio vs Node) | Root cause (verified in source) | Phase |
|---|---|---|---|
| LJS method/prototype dispatch | hashmap **1138x**, crypto_sha1 72x, deltablue 51x, richards 33x; feeds havlak 579x, cd 254x | Named load/store IC caches **own properties on plain maps only** (`js_load_ic_build_entry`, `js_runtime.cpp:7558`); every method call (`map.get(...)`, `this.foo()`) misses the IC, falls to `js_property_access`, and walks the prototype chain with `js_map_get_fast`/`js_find_shape_entry` per level, per call | **J1 (PIC)** |
| LJS IC C-call overhead on hits | every OO row, ~constant tax | Even a monomorphic IC hit crosses `js_property_access_named_ic` (`:7643`) — key match, receiver classify, state dispatch, profile counters — as a full C call per access | **J3** |
| LJS literal shape churn | havlak, cd, json (object-literal-heavy) | `jm_transpile_object` (`js_mir_expression_lowering.cpp:11644`) calls `js_new_object_with_shape` with **no cache cell** — a fresh `TypeMap`/`ShapeEntry` graph per evaluation; receivers stay polymorphic, defeating the IC; T3 landed "per-instance, not shared" | **J2** |
| MIR untyped map reads | richards.ls **66x**, splay.ls 43x, crypto_sha1.ls 31x, navier.ls 26x | `fn_member` → `map_get` → `map_get_for_owner` (`lambda-data-runtime.cpp:2192`): **linear scan of every ShapeEntry with `strlen(key)` re-computed per field compared**, plus recursion into nested maps; the `typemap_hash_lookup` O(1) table exists (`lambda-data.hpp:506`) but the read path never uses it | **L1** |
| MIR untyped map access C-call + hashing | same rows | Every `.field` read/write is a boxed C call (`transpile_member`, `transpile-mir.cpp:7399`); `fn_map_set` re-hashes the key per call (`hashmap_sip` in the richards profile); a direct-field emission path exists but is **disabled** (`if (false && ...)` at `transpile-mir.cpp:7438`) | **L2** |
| MIR boxed math calls | nbody 15x, awfy/nbody2, fibfp/sumfp family, spectralnorm 17x | MIR-Direct emits `emit_call_1(fn_math_sqrt)` with boxed Item arg/result; the registry already carries `native_c_name`/`native_func_ptr` per entry (`sys_func_registry.c:654`) and `mir.c:76-91` **already registers the native imports** — C2MIR consumed them (`transpile-call.cpp:105`), MIR-Direct never did (the Tune4 M1 out-of-scope note, now due) | **L3** |

Confirmed healthy / owned elsewhere: puzzle collapsed to 8x (J2/Tune5 fixed it);
storage 21x is allocation churn (R7 territory); collatz 1.5s×3 engines is
`shr()` boxing (small, fold into L3 only if free). **R7 (GC frequency × live
set)** keeps its Tune4 G0.2 entry evidence and stays out of this plan.

---

## 1. Evidence appendix (verified code facts)

- **JS named IC** (`js_runtime.h:68-98`): per-call-site `JsLoadIC`/`JsStoreIC`
  cells pool-allocated at emission (`js_mir_expression_lowering.cpp:11428`,
  `:1090`), 4-way polymorphic, entries hold `{shape, ShapeEntry*, byte_offset,
  name_id, receiver_kind}`. Hit = `m->type == cached->shape` + ctor-reserved
  check + `_map_read_field`. **Own plain-map properties only** — no prototype,
  no methods, no accessors (`entry->flags != 0` rejected at `:7590`).
- **Prototype walk**: misses land in `js_property_access` which consults
  `js_map_get_fast` per prototype level — the exact symbols at the top of the
  hashmap/cd/sha1 profiles (Tune5 §1).
- **Shared-shape machinery already exists**: the constructor path
  (`js_runtime.cpp:3193-3217`) captures `m->type` into a per-call-site
  `shape_cache` cell, marks it `is_shared_constructor_shape`, and `fn_map_set`
  already detaches via `js_typemap_clone_for_mutation_pub` on incompatible
  writes (`lambda-eval.cpp:6790-6803`). R3 is "give literals the same cell",
  not new machinery.
- **TypeMap** (`lambda-data.hpp:263`): already carries the O(1) `field_index`
  hash table, `slot_entries`, transition table, shared/private-clone flags, and
  a spare-byte tail — room for a `uint32_t guard_version` without layout pain.
- **Lambda map read path**: `map_get_for_owner` computes `strlen(key)` **inside
  the field loop** and does last-writer-wins over the whole chain, recursing
  into unnamed (spread/nested) entries. `fn_map_set`'s `map_find_shape_entry`
  (`lambda-eval.cpp:6771`) already goes hash-first — the asymmetry is the bug.
- **Native math**: `SysFuncInfo` rows carry `native_c_name`("sqrt"),
  `native_func_ptr`, `native_returns_float`, `native_arg_count`; `mir.c:76-91`
  registers every one as a JIT import at startup. MIR-Direct's sys-func
  emission (`transpile-mir.cpp:8893-8963`) ignores all of it and calls the
  boxed `fn_*` wrapper.
- **Lambda per-site cells**: `mt->script_pool` is available for pool_calloc'd
  IC cells (`transpile-mir.cpp:2312` precedent), mirroring the JS `ast_pool`
  pattern.

---

## 2. Phase T0 — instrumentation and probes (½ day)

Reuse `temp/tune4_probes.sh` (extend with `t6l`/`t6j` phases; same 3-run-median,
timestamped-JSON, never-overwrite protocol).

- **T0.1 IC state census (JS):** the counters exist (`JS_EXEC_PROF_LOAD_IC_*`).
  Run hashmap/richards/deltablue/cd/havlak under `js_exec_profile` and record,
  per benchmark: probe/hit-mono/hit-poly/megamorphic/miss counts, and the top
  miss reasons. **This decides J1's scope**: if the dominant state is
  "miss — property on prototype", the PIC is confirmed as the lever; if it is
  megamorphic own-property, J2 (literal sharing) leads. Do not skip.
- **T0.2 Lambda map counters:** release-safe counters (LAMBDA_MAP_STATS env, atexit
  dump, same style as Tune5 J0.1): `fn_member` calls, `map_get_for_owner`
  fields scanned, `fn_map_set` calls, hash-lookup hits vs chain-scan falls.
  Validate on richards.ls that scanned-fields ≈ fields-per-map × accesses.
- **T0.3 probe rows:**

| Phase | Primary probes | Guard probes (no regression >3%) |
|---|---|---|
| J1 | LJS jetstream/hashmap, richards, deltablue, crypto_sha1 | LJS awfy/json, sieve (J2/Tune5 wins must hold) |
| J2 | LJS awfy/havlak, cd, json | LJS jetstream/splay (already 55ms) |
| J3 | LJS richards, deltablue (post-J1 residual) | full J1 set |
| L1 | richards.ls, splay.ls, crypto_sha1.ls, navier_stokes.ls | test/lambda map goldens (correctness), havlak2.ls |
| L2 | same as L1 | MT7 budget diff review |
| L3 | awfy/nbody2.ls, jetstream/nbody.ls, beng/spectralnorm.ls, r7rs/fibfp2.ls | kostya/matmul.ls (Tune4 M1 win must hold), larceny/ray.ls |

---

## 3. Track J — LambdaJS shape lookups (R2/R3 + OI-6)

### 3.1 Phase J1 — data-driven method/prototype PIC (4-6 days; the centerpiece)

**Design (settles the OI-6 open decision).** Per-call-site side-table cache for
property loads that resolve **on the prototype chain** (methods above all),
extending the existing `JsLoadIC` rather than inventing a parallel structure:

- **Entry shape:** add to `JsLoadICEntry` a `uint8_t kind` (OWN / PROTO) and,
  for PROTO entries, `{void* holder_shape, void* holder_map, uint32_t
  guard_version}` — receiver shape identifies the *start* of the chain, holder
  map + entry give the slot, and the guard version validates that no prototype
  between receiver and holder mutated since install.
- **Invalidation granularity — DECIDED: option (b), per-shape versions.** Add
  `uint32_t guard_version` to `TypeMap` (spare tail space, zero-init). Every
  structural or descriptor mutation on a map whose shape is cached-as-holder
  bumps its version: the central choke points are `fn_map_set`'s
  rebuild/detach path, `js_map_promote_descriptor_kind`,
  `js_intrinsic_note_property_mutation`, delete, and `defineProperty` /
  `seal` / `freeze` entry points — enumerate by grepping writers of
  `Map::type`/`map_kind` and route them through one
  `js_shape_note_mutation(TypeMap*)` helper so the list is auditable. Option
  (a) (one global version) is rejected: test262 mutates prototypes constantly
  and would thrash every cache.
- **Chain validation without walking:** cache `guard_version` of **each** map
  on the receiver→holder path summed (or capped at 2 intermediate levels —
  beyond that, don't cache; deep chains are rare and stay on the slow path).
  Hit check = receiver shape ptr equal + Σversions equal. This keeps the hit
  O(1) while catching insertion of a shadowing property on an intermediate
  prototype.
- **Realm safety by construction:** cells live in the compiled module's pool
  (existing IC lifetime), shapes/maps are realm-owned objects — no global
  state. Realm reset destroys the module context; stale cells cannot cross.
- **What is deliberately NOT cached:** accessors (getter must run), proxies,
  VMap/host objects, `with`-scoped lookups, arrays' exotic names, anything
  with `entry->flags != 0` — all keep the current slow path. Store-side PIC
  (prototype setters) is **out of scope**; stores to own properties already
  have the store IC.
- **Method-call fusion:** the dominant consumer is `obj.m(args)`. The member
  lowering already has the callee context; after J1 the load of `m` via PIC
  yields the JsFunction without a proto walk — no separate call-site cache
  needed in this phase (that would be code-patching territory).

**Companion fix (from OI-6, mandatory):** duplicate-class-name deopt — re-key
the shaped construction/devirtualization fast paths by constructor/`TypeMap`
identity instead of class-name strings (JS_07 §7.7). Small, and the PIC makes
the class-name keying actively dangerous.

**Fixtures (inherited from the reverted widening attempt, mandatory):**
`Object.defineProperty`/`Object.seal` on cached-shape objects; two same-named
classes exercising both fast paths; prototype mutation *after* cache warm
(method replaced, method added to intermediate proto, `__proto__` reassigned);
forced-GC stress over all of them; plus the existing test262 baseline which
mutates prototypes pervasively.

**Gates.** T0.1 census shows proto-miss → hit-mono conversion; hashmap ≥5x
(1138x → ≤250x class; its puts also pay literal/GC costs), richards ≥3x,
deltablue ≥3x, crypto_sha1 ≥2x; zero test262/node regressions.

### 3.2 Phase J2 — object-literal shape sharing per call site (R3, 1-2 days)

Mirror the constructor-shape cache onto the literal path:

- **J2.1** Add a `void** shape_cache` parameter to `js_new_object_with_shape`
  (new symbol `js_new_object_with_shape_cached`; keep the old export for ABI
  stability) implementing exactly the `js_runtime.cpp:3193-3217` pattern:
  first evaluation captures `m->type` and marks `is_shared_constructor_shape`
  (or a dedicated `is_shared_literal_shape` alias of the same discipline);
  later evaluations attach the cached TypeMap and allocate only `m->data`.
- **J2.2** Emission: `jm_transpile_object` pool_calloc's one cell per static
  data-key literal site (it already computes `static_shape` and the name
  arrays — the gate is unchanged: no computed keys, no spread, no accessors,
  no `__proto__` literal, property order preserved).
- **J2.3** Mutation safety is **already implemented**: `fn_map_set` detaches
  shared shapes via `js_typemap_clone_for_mutation_pub` before incompatible
  writes; the A2-T1 private-clone rules keep the blueprint immutable. Add a
  regression: literal evaluated in a loop, one instance mutated with a
  type-changing write, others must be unaffected; plus delete-on-one-instance.
- **J2.4** Effect to verify with T0.1: literal receivers become monomorphic →
  IC hit rates jump on havlak/cd/json without any J1/J3 change.

**Gates.** havlak ≥2x, cd measurable, json flat-or-better; per-iteration
allocation counts drop (TypeMap/ShapeEntry allocations per literal evaluation
→ 0 after warmup); same zero-regression gates.

### 3.3 Phase J3 — inline the monomorphic IC hit in MIR — **NOT STARTED (scoped 2026-07-26)**

The census still supports J3 as the right lever for richards/deltablue (~100%
IC hit rate, so what is left is purely the per-hit C-call tax). It was scoped but
deliberately not implemented, because it is emission-level JIT work whose failure
mode is a silent wrong value — the exact class of bug L2 produced and that took
real effort to catch. Landing it unverified would be worse than not landing it.

What the scoping found, so the next attempt starts further along:

- **`inline_kind` cannot be an emission-time constant.** The IC cell fills at
  runtime, so the emitted code must *load* `ic->entries[0].inline_kind` and
  branch on it. §3.3's phrasing ("immediate at emission? No — load from the
  cell") is right, but the consequence was understated: `_map_read_field`
  (`lambda-data-runtime.cpp:2105`) has ~12 repr cases, so an inline path
  covering raw-Item/BOOL/INT/FLOAT is a 4-way runtime branch plus the shape
  guard, not one compare-and-load.
- **Restricting to raw-Item only (one compare, one load) would rarely fire.**
  `js_set_shaped_slot` retags entries to the concrete value type, so live JS
  fields are INT/FLOAT/STRING/MAP — `LMD_TYPE_NULL` (the raw-Item case) is only
  the unwritten state. INT is the case that actually pays on richards.
- **Cheapest useful first cut:** guard + INT + raw-Item, everything else falling
  through to the existing helper, then measure before adding FLOAT/BOOL.
- MT7 JS budgets change materially; per §3.3/Tune4 M1.6 that is a deliberate
  lift with the dump diff quoted.
- Verification bar: `js_exec_profile` branch counters must be identical
  before/after across the fixture set (the hit *semantics* must not move, only
  who executes them).

Original plan text follows.


Only after J1+J2, and only the **mono** state — the C helper remains the
canonical path for everything else:

- **J3.1** Extend the IC install to compute an `inline_kind` per entry: eligible
  when receiver is plain MAP, entry has `flags == 0`, no ctor-reserved overlap
  (checked once at install — the per-hit `map_ctor_offset_is_reserved` re-check
  is then redundant for that entry), and the field repr is raw-Item, BOOL, or
  INT (`_map_read_field` cases that are pure bit ops, `lambda-data-runtime.cpp:2105`).
  FLOAT fields use the existing inline self-tagged-double emission helper from
  transpile-mir (Tune4 M1 machinery); anything else keeps the C call.
- **J3.2** Emission (`js_mir_expression_lowering.cpp:11436` site): load
  `ic->state`; if MONO and `object` tag == MAP: load `map->type`, compare with
  `ic->entries[0].shape` (immediate at emission? No — the cell fills at
  runtime, so load from the cell), on match load `map->data + byte_offset` and
  box per `inline_kind`; else call the existing helper. The store site
  (`:1096`) gets the symmetric treatment for same-type in-place writes only.
- **J3.3** MT7: JS budgets change materially — deliberate lift with dump diff
  quoted, per Tune4 M1.6 discipline.

**Gates.** richards/deltablue post-J1 residual shrinks measurably (target ≥1.5x
on each); `js_exec_profile` shows helper-call counts collapse while hit
semantics stay identical (branch counters equal before/after on the fixture
set); zero-regression gates.

---

## 4. Track L — Lambda MIR untyped maps + native math (R6)

### 4.1 Phase L1 — O(1) untyped map reads (1-2 days) — **DONE 2026-07-26**

**Landed as L1.1 + a name-id fast compare; L1.2/L1.3 not needed.**
`map_get_for_owner` and `_map_get` (`lambda/runtime/lambda-data-runtime.cpp`)
now compute key length and `name_id` once at the public entry and thread them
through the recursion via `*_keyed` helpers, comparing with the existing
`typemap_shape_name_equals_id`. Previously `strlen(key)` was recomputed for
*every* ShapeEntry compared, so a miss on an n-field map cost n strlens.

Measured on release, 3-run medians, same host, "before" = `git checkout` of that
one file:

| benchmark | before | after | speedup |
|---|---|---|---|
| jetstream/richards.ls | 372.3 ms | 242.0 ms | **1.54x** |
| awfy/havlak2.ls | 70.0 ms | 55.1 ms | **1.27x** |
| jetstream/splay.ls | 226.5 ms | 184.1 ms | **1.23x** |
| jetstream/crypto_sha1.ls | 245.2 ms | 208.6 ms | **1.18x** |
| jetstream/navier_stokes.ls | 1110.8 ms | 964.2 ms | **1.15x** |

No regressions. New test `test/lambda/map_duplicate_key_lookup.ls` + `.txt`
(15 cases: duplicate literal keys, type-changing duplicates, spread shadowing in
both directions, nested spread chains, absent key). Its golden was verified
**identical on pre-L1 master**, so it pins existing behaviour rather than
encoding the change.

Original plan text follows.


- **L1.1** `map_get_for_owner`/`_map_get`: hoist `strlen(key)` out of the loop
  (callers pass C strings; compute once). Mechanical, zero-risk, do first.
- **L1.2** Hash-first read path: when the TypeMap's shape chain contains **no
  unnamed entries** (no spread/nested-map fields — track with one flag set at
  shape build, or derive by checking `field_index` population), serve reads via
  `typemap_hash_lookup` + `map_read_field_for_owner` directly. **Audit first**:
  last-writer-wins with duplicate keys — verify `typemap_hash_lookup` resolves
  duplicates to the *last* entry (fn_map_set already trusts it via
  `map_find_shape_entry`, which is evidence but not proof for literals with
  duplicate keys; write the fixture: `{a:1, a:2}.a == 2` through both paths).
  Shapes with unnamed entries keep the ordered scan — semantics preserved by
  construction.
- **L1.3** Element attribute reads (`Element` extends the same shape model) get
  the same treatment through the shared helper — do not fork a second copy
  (CLAUDE rule 13).

**Gates.** T0.2 counters: scanned-fields collapses to ~0 on richards.ls;
richards.ls ≥2x, splay.ls/crypto_sha1.ls measurable; full lambda baseline
(maps are everywhere — this is the highest-blast-radius change in the plan;
the map/element golden tests are the real gate).

### 4.2 Phase L2 — per-call-site member IC for Lambda — **DONE 2026-07-26 (stage 1 only)**

`fn_member_ic(item, key, LambdaMemberIC*)` lives in `lambda-data-runtime.cpp`
beside `map_read_field_for_owner` (nothing copied — CLAUDE rule 13); the per-site
cell is `pool_calloc`'d from `mt->script_pool` at static-name member sites in
`transpile_member`. **L2.3 (the disabled direct-field inline) was deliberately
not enabled** — the helper IC already took the win and inline emission is where
this phase's risk lives.

**L2.1 audit result — the shape-pointer compare IS sufficient, for a reason
worth writing down.** Every Lambda writer that *repacks or replaces* a shape
(`map_rebuild_for_type_change`) allocates and installs a **new** TypeMap, so
identity changes and the cache self-invalidates. The writers that mutate
in place (`fn_map_set`/`js_set_shaped_slot` retags, `map_put`'s chain append)
only retag an existing entry or append a new one — and because the hit path
re-reads `entry->type`/`entry->byte_offset` through the shared helper rather
than caching a materialised offset+type, a retag is *observed*, not cached over.

**Scope: plain `LMD_TYPE_MAP` only.** `LMD_TYPE_OBJECT` reads through `_map_get`
(which uses `_map_read_field`, **not** the owner-aware variant) and falls back to
a method table on miss; `LMD_TYPE_ELEMENT` goes through `elmt_get`. Caching
either would mean reproducing a second read path, so they keep the `fn_member`
route. Install is also refused for any shape containing an unnamed (spread/
nested) entry, so last-writer-wins can never disagree with the slow path.

**A silent-wrong-value bug this phase produced and had to fix — read this before
adding any other per-call-site cell.** First run: **134 baseline failures, every
one passing standalone.** Root cause: the **L1 MIR module cache replays compiled
code across script runs**, while TypeMaps come from a per-run arena that is freed
and recycled — so a cell baked in run N saw a *different* TypeMap allocated at
the same address in run N+1 and reported a **false hit**, reading the wrong
field. Confirmed by `LAMBDA_DISABLE_MIR_CACHE=1` turning 30 failures into 0.
Fixed with a monotonic `lambda_shape_epoch()` bumped in `runner.cpp` where each
run activates its context; the cell records its install epoch and must match.
Immeasurable cost. **Any future baked-in mutable cell needs the same guard**, and
it is worth checking whether the JS load/store ICs are exposed to the same
hazard — their cells live in `ast_pool` (retained with the MIR context), but
their cached *shapes* are realm-owned, which is the same split that broke here.

**Measured (release, 5-run medians, both binaries built first then interleaved):**

| benchmark | pre-L2 | L2 | ratio |
|---|---|---|---|
| jetstream/richards.ls | 253.0 ms | 212.7 ms | **1.190x** |
| awfy/cd2.ls | 439.1 ms | 374.7 ms | **1.172x** |
| awfy/havlak2.ls | 55.9 ms | 49.2 ms | **1.136x** |
| jetstream/splay.ls | 176.8 ms | 157.5 ms | **1.123x** |
| jetstream/crypto_sha1.ls | — | — | 1.006x (flat) |

Combined with L1, richards.ls is **372.3 → 212.7 ms = 1.75x**. The plan's
"L1+L2 ≥4x" gate is **not** met — but 4x was never reachable by removing lookup
cost alone; what remains is call overhead and boxing (R4/OI-9 territory).

Gates: `test-lambda-baseline` 3619/3619, `test262-baseline` 40261/40261 zero
regressions, `make lint` no new findings.
`test/test_item_repr_gtest.cpp`'s `MirMemberAccessKeepsContainerItemUnmodified`
updated deliberately to accept `fn_member_ic` as well as `fn_member` (it is a
dump-pattern assertion, and static-name sites now lower to the IC form).

Original plan text follows.


Port the JS named-IC pattern (stage 1: helper-based, no emission risk):

- **L2.1** `LambdaMemberIC { TypeMap* shape; ShapeEntry* entry; }` cell
  pool_calloc'd from `mt->script_pool` per static-name member site;
  `fn_member_ic(Item obj, String* key, LambdaMemberIC* ic)` — hit = `obj` is
  MAP/OBJECT/ELEMENT and `map->type == ic->shape` → `_map_read_field`; miss =
  current `fn_member` + install. Lambda has no `defineProperty`/prototype
  mutation; shapes change identity on `map_rebuild` (type-changing
  `fn_map_set`), so the shape-pointer compare is the whole guard — **verify**:
  grep every writer of `Map::type`/`Object::type`/`Element::type` and confirm
  each installs a fresh TypeMap rather than mutating entries of the old one in
  place; any in-place mutator must bump-or-clone (this audit is the phase's
  main risk item).
- **L2.2** `fn_map_set_ic` symmetric cell for `obj.field = v` same-type
  in-place writes (the hot richards pattern); type-changing writes fall
  through to `fn_map_set` (which rebuilds and thereby changes shape identity,
  auto-invalidating every IC by construction).
- **L2.3** Re-evaluate the disabled direct-field block
  (`transpile-mir.cpp:7438`, `if (false && ...)`): with the IC cell in place
  the *guarded* inline load (compare `map->type` to the cell, then raw load)
  becomes safe where the static-only version was not. Enable only for
  raw-Item/INT/BOOL/FLOAT reprs, same restrictions as J3.1. This is L2's
  stage 2; land it only if the helper-IC numbers say the C call is now the
  dominant residual (measure between L2.2 and L2.3).

**Gates.** richards.ls combined L1+L2 ≥4x (357ms → ≤90ms class, vs Node 66x →
≤17x); splay.ls approaches its typed variant (`splay2.ls` ran ~104ms
pre-Tune4); MT7 lifts deliberate; forced-GC stress on the IC cells (shapes are
pool/heap-owned — confirm TypeMap lifetime spans the module, not one
collection).

### 4.3 Phase L3 — native math lowering in MIR-Direct — **DONE 2026-07-26 (gate missed)**

Landed in `transpile-mir.cpp`'s sys-func emitter: `math.*` transcendentals with
statically-numeric scalar arguments now call the libm symbol through a `d→d`
prototype (`sqrt_p: proto d, d:a`) instead of the boxed `fn_math_*` wrapper.
The imports were already registered (`mir.c` maps `native_c_name →
native_func_ptr`), so this was pure emission, as the plan said.

**Correctness is exact, not approximate.** The boxed wrappers' scalar branch is
literally `push_d(fn(item_to_double(v)))`, so the native call is bit-identical.
Verified by diffing a 22-line edge fixture (`test/lambda/math_native_lowering_edges.ls`)
against the pre-L3 binary: NaN/±inf/signed-zero/domain-error results all match
exactly, and vector arguments still go element-wise
(`math.sqrt([1.0,4.0,9.0])` → `[1, 2, 3]`).

**Two gate corrections found by measurement, both now pinned by tests:**
- §4.3's L3.2 proposal to include `abs`/`min`/`max` is **wrong** and was
  reverted mid-phase. Those are *type preserving*: `abs(-5)` is int `5`, and
  routing them through `fabs` turned it into float `5.0`. The lowering is now
  gated by an explicit **allow-list**, `mir_native_math_always_float()`, holding
  only the 23 transcendentals whose boxed impl is unconditionally `push_d`.
  Allow-list not deny-list on purpose: a missing entry costs an optimisation, a
  wrong entry is a semantic bug.
- Gating on `call_expr_tid == LMD_TYPE_FLOAT` alone disables the phase entirely
  — `math.sqrt(x)` call nodes carry static type ANY, not FLOAT.

**Measured (release, 5-run medians, base and L3 binaries built first then
interleaved run-by-run — non-interleaved runs drifted ~10% and produced a
spurious 1.05x):**

| benchmark | base | L3 | ratio |
|---|---|---|---|
| awfy/nbody2.ls | 81.97 ms | 80.64 ms | 1.017x |
| jetstream/nbody.ls | 82.13 ms | 81.01 ms | 1.014x |
| beng/spectralnorm.ls | 47.52 ms | 47.69 ms | 0.996x |
| r7rs/fibfp2.ls | 5.29 ms | 5.27 ms | 1.003x |
| kostya/matmul.ls (guard) | 42.28 ms | 42.16 ms | 1.003x |
| larceny/ray.ls (guard) | 11.54 ms | 11.48 ms | 1.005x |

**The gate ("nbody2 ≥1.5x, spectralnorm ≥1.3x") is missed by a wide margin —
the real win is ~1.5% on nbody and nothing elsewhere.** The gate was
mis-targeted: spectralnorm contains exactly one `math.sqrt`, on line 95, outside
the hot loop, so it was never going to move. nbody has two in hot loops, but
removing ~10ns of boxing from a ~600ns iteration is ~1.5%, not 50%.

Kept anyway — unlike J2 it is never negative, it is provably semantics-preserving,
and it closes a documented C2MIR/MIR-Direct parity gap. But do not expect
further float-benchmark movement from native math; the boxing was not the cost.
`test/mir/lambda/sys_func_specialization.mir-check` was updated deliberately
(it was authored to fail exactly when this landed): `import fn_math_sin/sqrt` →
`import sin/sqrt`, with `fn_abs` still expected boxed.

Original plan text follows.


The imports are already registered (`mir.c:76-91`); this is pure emission:

- **L3.1** In the sys-func call emitter (`transpile-mir.cpp:8904` family): when
  `fn_info->native_c_name && native_arg_count==1` and the argument's effective
  type is FLOAT (or INT with an `I2D` cast), emit `call native_c_name` with
  `MIR_T_D` arg/result — mirroring `can_use_native_math`
  (`transpile-call.cpp:105`). The result stays a native double in the register
  (Tune4 M1's FLOAT arithmetic consumes it unboxed); box only at ANY
  boundaries via the existing inline-double path.
- **L3.2** Two-arg family (`native_arg_count==2`): `pow`→`fn_pow_u`, `atan2`,
  `hypot`, `min`, `max` — same condition on both args. Also `fn_abs_i` for
  INT abs (the C2MIR gap list from R6 verbatim).
- **L3.3** ANY-typed args keep the boxed `fn_math_*` call unchanged (vector
  semantics live there — `fn_math_sqrt` is element-wise on arrays; the native
  path must trigger **only** on proven scalars; this is the semantic cliff,
  add an array-arg regression).
- **L3.4** Budgets: `sys_func_specialization.mir-check` expectation changes are
  the *point* of this phase (R6 said "update deliberately") — one commit per
  budget change with dump diff.

**Gates.** nbody2.ls ≥1.5x, spectralnorm ≥1.3x, fibfp/sumfp measurable;
matmul/ray guards flat; NaN/domain-error semantics identical (`sqrt(-1)`,
`log(0)`, `pow` edge table — golden fixture comparing boxed vs native results
across the edge inputs).

---

## 5. Sequencing, exit, and Result13

```
T0 → J1 → J2 → J3        (Track J, serial: each re-baselines the next)
T0 → L1 → L2 → L3        (Track L, independent of Track J, may interleave)
```

J1 before J2 because the PIC census (T0.1) may reveal literal polymorphism as
the *cause* of megamorphic sites — if so, swap J1/J2 (the plan survives either
order; the census decides). L1 first in Track L because it is smallest-risk and
re-baselines richards/splay under L2. Each phase lands independently green.

**Exit = Result13** (same protocol: clean release build, four-engine matrix,
3-run medians, 180s, fresh, raw JSON preserved, QuickJS as host-consistency
control). Success thresholds — set against Result12, honest about what shape
lookups alone can buy:

| Metric | Result12 | Target |
|---|---|---|
| LambdaJS/Node geo (dedup) | 15.1x | ≤ 8x |
| Worst LJS row | 1138x (hashmap) | ≤ 150x |
| LJS rows > 100x | 5 | ≤ 1 |
| MIR/Node geo (dedup) | 3.01x | ≤ 2.2x |
| Worst MIR row | 66x (richards) | ≤ 20x |

After Result13, the expected residuals are R7 (GC frequency × live set — its
entry evidence is already recorded), R4/R5 (call overhead + exception polls,
partly landed by the online-exception plan), and the OI-9 unboxed-slot design
for whatever boxing tax remains.

---

## 6. Risks

- **J1 invalidation completeness is the correctness cliff.** A missed
  shape-mutation choke point leaves a stale PIC entry serving a shadowed or
  deleted method. Mitigation: single `js_shape_note_mutation` funnel with a
  grep-audited caller list committed in the phase; the OI-6 fixture set;
  test262's pervasive prototype mutation as the backstop. When in doubt a site
  bumps the version — over-invalidation costs a re-fill, never wrongness.
- **J1 scope creep into code patching.** The temptation is to specialize call
  sites on the cached JsFunction. Resist: data-driven lookup only, this plan.
- **J2 literal aliasing.** Shared literal shapes must never let one instance's
  mutation appear on another. The detach machinery exists and is tested for
  constructors; the new fixtures extend it to literals. Any gap found demotes
  that literal site to unshared rather than patching semantics inline.
- **L1 last-writer-wins.** Hash lookup must agree with the ordered scan on
  duplicate keys and spread-shadowed fields; shapes with unnamed entries are
  excluded wholesale. The dual-path differential fixture (same map read via
  both paths across the goldens) is the gate.
- **L2 shape-identity assumption.** If any Lambda writer mutates a live
  TypeMap's entries in place (rather than rebuilding), the IC serves stale
  offsets. The L2.1 audit is mandatory before the cell goes live; in-place
  sites found get clone-or-version treatment.
- **L3 vector semantics.** `math.sqrt(array)` is element-wise; the native path
  must key on proven-scalar static types only. ANY stays boxed. The array-arg
  regression pins it.
- **Machine-state variance.** All phase gates are ratios against same-day
  before/after runs on one host; Result13 absolutes get the QuickJS control
  column, as Result12 did.
