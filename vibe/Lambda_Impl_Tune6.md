# Lambda Impl Plan: Tune 6 — Result12 Tail Elimination (LJS shape lookups + Lambda untyped maps + native math)

**Status: PLANNED — 2026-07-24.**
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

### 3.3 Phase J3 — inline the monomorphic IC hit in MIR (R2 stage 2, 2-3 days)

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

### 4.1 Phase L1 — O(1) untyped map reads (1-2 days)

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

### 4.2 Phase L2 — per-call-site member IC for Lambda (2-3 days)

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

### 4.3 Phase L3 — native math lowering in MIR-Direct (1-2 days)

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
