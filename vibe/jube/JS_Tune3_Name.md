# JS Tune2 Name — Implementation Plan: one property-key identity (JR2 / R1)

**Date**: 2026-08-09  **Status**: PROPOSED
**Tree anchor**: master `0ed462fe3` + Tune2-Exception change set
**Design authority**: **D4.6.1** (one address per semantic property key;
`NameRef`/`PropertyKeyRef` compare by pointer equality; `NameId` is generated
identity), **D3.4.4** (`name_hash` is a lookup hash never an identity;
`key_ref` is the canonical runtime key; Input-owned document shapes keep it
NULL), **DO16** (open: dynamic-intern growth bound, MIR-cache/GOT
reconciliation). Mechanism design: `vibe/Lambda_Design_Name_Identity.md`
(NI1–NI16, rev 5 — settled, not implemented) — this plan implements its W4.1
and W4.2 stages plus the JS-side routing consumers. Redesign phase: JR2 / R1
in `vibe/jube/JS_Runtime_Redesign.md`.
**Wave**: Tune2 = `JS_Tune2_Exception.md` (JR3, landed) + this doc (JR2).
**Predecessor evidence**: `vibe/jube/JS_Profiling_Helpers.md` (name/shape
lookup = 8.3% of working CPU; 2,425 samples across `js_find_shape_entry`,
`js_builtin_catalog_find`, `typemap_hash_lookup_*`).

---

## 0. Why

Four key representations cross the JS property APIs today (raw
`(chars,len)`, boxed string `Item`, interned `String*`, FNV
`ShapeEntry.name_id`), and canonicalization happens per lookup instead of per
name: `js_find_shape_entry`
([js_property_attrs.cpp:67](../../lambda/js/js_property_attrs.cpp)) calls
`heap_create_name` on **every raw probe**, and `heap_create_name` is
referenced **2,469×** across `lambda/js` (1,097 in `js_runtime.cpp` alone).
Special-property routing resolves names by length-guarded `strncmp` chains
(486 `strncmp` in `js_runtime.cpp`, concentrated in the
`js_property_get`/`set` special-name region). JR2's mandate: one key form —
the NamePool-canonical `String*` behind `PropertyKeyRef` — compared by
pointer, with interning moved to compile/boot time.

Per JR1 there is nothing to invent here: D4.6.1/D3.4.4 already ratify the
identity model and `Lambda_Design_Name_Identity.md` already designed the
mechanism. This plan is the LJS application, phased so `lambda/js` LOC goes
**down** while the substrate lands.

## 1. Decision ledger

Continues the redesign doc's JR2 series (the JR3.x pattern). Where an
`S#`/`D#` ruling covers the point, the entry records the application.

- **JR2.1 — The design source is `Lambda_Design_Name_Identity.md`; no
  parallel "JsName" type is introduced.** The redesign doc's `JsName` *is*
  NI1's canonical `String*`/`PropertyKeyRef`; this plan implements NI W4.1 +
  W4.2 and consumes them from LJS. Inventing a JS-private sibling would be
  the exact JR1 violation the redesign exists to delete. [JR1, NI1, D4.6.1]
- **JR2.2 — Pointer equality is the only internal key comparison; raw bytes
  survive only at the API boundary.** Every internal property path takes
  `PropertyKeyRef` (the `js_find_shape_entry_key` form generalized); the
  canonicalizing `(chars,len)` wrapper remains solely for genuinely raw
  external bytes, and is not callable from hot internal paths. The `_key`
  fast path and the wrapper are one identity, never two (the Tune1-P2b
  two-identity hazard). [D4.6.1, NI10]
- **JR2.3 — W1's jump-table dispatch is deferred to R3 (JR5); only the id
  plumbing lands here.** Converting the `js_string_method`-family name
  chains to `switch (builtin_id)` builds code R3 deletes when builtins
  become function values — the same double-churn the R2/R3 sequencing
  analysis warned about (§9 item 7 of the redesign doc). Tune2-Name lands
  name→id resolution and stops the id→name→chain round-trip allocation; the
  dispatch rewrite itself is R3's. **W2 (`ctor_id`) is in scope** — small,
  independent, and R3 keeps constructor identity. [NI §9 W1/W2]
- **JR2.4 — Well-known JS names resolve to canonical refs once at isolate
  boot (`js_wk` table); the W3 generated pools later replace the *source* of
  that table without touching its consumers.** Full W3 (generated pools 0–2,
  markup/CSS/Radiant migration) is a P2 cross-engine workstream and out of
  scope here. Boot-time canonicalization through the isolate NamePool gives
  the same pointer-identity guarantee within the isolate (NI6 first-definer
  rule) and is JR1-clean — it is the existing pool, not a side table. The
  `js_wk` consumer shape (`key == js_wk.length`) is exactly what NI §6
  specifies. [NI6, NI16, JT1 per-isolate ownership]
- **JR2.5 — Input-owned document shapes keep `key_ref == NULL`; the
  hash/length/memcmp seam survives only for that boundary.** A million-key
  parsed JSON must not bloat isolate canonical state. JS property reads over
  document shapes (DOM over parsed HTML, Input data) take the seam path;
  canonical JS shapes always set `key_ref`. Deleting the seam entirely would
  be wrong, not thorough. [D3.4.4, NI13]
- **JR2.6 — Zero interning per property lookup is a standing measured gate,
  not a code-review impression.** A counter on the dynamic-intern path
  (DO16's watch-item made observable) must read 0 across the benchmark
  corpus for canonical-shape lookups. [DO16]
- **JR2.7 — Sections, isolate registry, and the property-key GOT (NI W4.3)
  are explicitly out of scope** until DO16's MIR-cache reconciliation
  settles (jointly with const pool, per the DO16 text). Interim invariant:
  any canonical `String*` baked into emitted MIR must be owned by a pool
  whose lifetime covers the module's, including L1 cache re-entry. [DO16,
  NI §8]
- **JR2.8 — `lambda/js` LOC must strictly decrease** (final gate; also
  redesign hard gate 2). Baseline frozen in §2. `lambda/core`/`lib` may grow
  to host the substrate (W4.1 is core work); that growth is reported
  honestly and separately, but the JS runtime — where the duplication lives
  — must end smaller than it started. Every phase is net-negative in
  `lambda/js` or pairs its addition with its deletion in the same phase.

## 2. Measured baseline (frozen at plan time)

| metric | value | source |
|---|---:|---|
| `lambda/js` LOC (`*.c *.cpp *.h *.hpp`) | **228,928** | `wc -l` |
| core semantic runtime bucket (runtime, globals, props/attrs, typed, regex, state, value) | 73,154 | `wc -l` (redesign §5.2 baseline "~75k") |
| `heap_create_name` references in `lambda/js` | 2,469 | grep |
| `strncmp` in `js_runtime.cpp` | 486 | grep |
| key representations in property APIs | 4 | redesign §5.1 census row |
| name/shape lookup CPU share | 8.3% (pre-Tune1 profile — **stale, N0 refreshes it**) | `JS_Profiling_Helpers.md` |
| `PropertyKeyRef` mentions in `lambda/js` headers | 33 (the `_key` fast path exists but is the minority form) | grep |

Property-name travel today: lowering boxes the name as a string `Item` and
calls `js_property_access(Item, Item)`; named-IC sites compare baked rodata
`char*` with a memcmp fallback; shape probes canonicalize per call. The
transpiler already interns *bookkeeping* names via `jm_persist_name` →
`name_pool_create_name`, so compile-time interning infrastructure exists —
it is simply not what the emitted property path carries.

## 3. Work plan

Phases N0–N6; each is one commit (or one reviewed batch), both build modes
green, gates per §4.2. N1→N2→N3 are ordered (substrate before consumers);
N4 and N5 are independent of each other after N2.

### N0 — Instrumentation, census, refreshed baselines

1. **Key-form census tool** `utils/js_name_key_census.py` (static, no
   build): counts (a) property-API entry points by key form (raw
   `(chars,len)` / `Item` / `String*` / `PropertyKeyRef`), (b)
   `heap_create_name` references per file, (c) `strcmp`/`strncmp` chains in
   the NI §4.6 in-scope routing files with the narrow allowlist (raw text,
   diagnostics). Exits nonzero when a *new* raw-key entry point appears —
   the standing census the redesign's §5.1 gate needs.
2. **Dynamic intern counter** (JR2.6): a `js_exec_profile_count` event (the
   infra exists, `js_exec_profile.h`) on the dynamic-intern path taken from
   property lookup, plus a total-interns counter on `name_pool_create_*`
   gated by an env switch. Report per benchmark run.
3. **Profile refresh**: rerun the `JS_Profiling_Helpers.md` §6 protocol
   (release_profile, matched pair, 261-script batch) — the 8.3% lookup
   number predates Tune1-E1 and Tune2-Exception; record the current bucket
   table as this plan's real "before".
4. Freeze behavior baselines: NamePool unit tests, `make
   test-input-baseline` (Mark/Input parsers), `make test-lambda-baseline`,
   `./test/test_js_gtest.exe`, `make test262-baseline`, node-baseline
   count, DOM suites, GC-stress; record the §2 LOC numbers in this doc.

**Exit**: tools run from a fresh checkout; refreshed profile recorded in §2;
all baselines green/recorded.

### N1 — NameMeta substrate on every NamePool path (NI W4.1)

Core-side, API-preserving:

1. Add the fixed `NameMeta` prefix (NI3/NI9: predefined NameId or NONE, key
   kind, cached hash, flags, parsed array-index-or-sentinel) to every
   existing NamePool creation path; set `String.is_pooled`; keep hierarchy,
   content-interning, and Input/Mark behavior byte-identical from the
   caller's view.
2. One shared ordinary-name classifier (NI9) used by both the transpiler
   and the dynamic intern path — the cached array-index classification later
   deletes LJS's per-access numeric-string parse.
3. No LJS changes yet beyond compiling against the new header.

**Exit**: NamePool unit tests (interning, hierarchy, empty names, legacy
symbol pooling) green; `make test-input-baseline` and
`make test-lambda-baseline` green (100% on `lambda/core` touches, per NI §9
gates); parser outputs and lifetimes unchanged. `lambda/js` LOC unchanged;
`lambda/core` growth reported.

### N2 — PropertyKeyRef and the ShapeEntry migration (NI W4.2 core, STRING first)

1. Introduce `PropertyKeyRef` as NI1 defines it (opaque address-sized ref;
   STRING kind first — SYMBOL/PRIVATE keep their `__sym_N`/`__private_*`
   STRING spellings until N5, which is sound because those spellings are
   themselves interned STRING keys).
2. **Rename `ShapeEntry.name_id` → `name_hash`** (NI10 — it is an FNV hash,
   and the rename prevents every future confusion with real NameIds); add
   `NameId predefined_id` and `PropertyKeyRef key_ref`. Canonical JS shapes
   set `key_ref`; Mark/Input shapes keep it NULL (JR2.5).
3. Typemap lookup confirm order becomes: `key_ref` pointer compare
   (canonical JS) → `predefined_id` compare (predefined native) →
   hash+length+memcmp **only** for the non-predefined id-less seam
   ([lambda-data.hpp:477](../../lambda/lambda-data.hpp) region).
4. Dual `(chars,len)` helper entries stay during this phase (NI W4.2's own
   migration note) — they are deleted in N3, not here.

**Exit**: all suites green; census shows `key_ref` populated on canonical JS
shapes and NULL on document shapes (a fixture asserts both); no measurable
parser regression on the input baseline. Net `lambda/js` LOC ≈ neutral
(additions paired with the memcmp-confirm deletions).

### N3 — Kill per-lookup canonicalization; delete the raw probe forms

The payoff phase — where the 2,469 `heap_create_name` references collapse.

1. **Lowering emits canonical keys.** Property names in emitted code become
   canonical pooled `String*` constants (the `jm_persist_name` pipeline,
   promoted from bookkeeping to the emitted operand): `js_property_access`
   and the IC helpers receive an `Item` whose `String*` is already
   canonical; the runtime does **not** re-intern. eval/`new Function`/REPL
   take the same path via the runtime NamePool (NI12 — one pipeline).
2. **Internal callers move to the `_key` form.** Every internal caller of
   `js_find_shape_entry` and the raw-`(chars,len)` property helpers that
   holds (or can cheaply hold) a canonical ref calls the `PropertyKeyRef`
   form; repeated-probe sites (`js_has_own_property` behind `in`, descriptor
   reads, `__instance_proto__` probes) stop paying per-probe interning.
3. **Named-IC key match becomes `PropertyKeyRef` compare, no memcmp
   fallback** (NI §6 row 1) — delete the fallback path.
4. **Delete the dual raw entries** introduced/retained in N2: raw
   `(chars,len)` probe variants go; the single canonicalizing wrapper
   remains at the API boundary (JR2.2), implemented *as* "canonicalize,
   then call the `_key` form".
5. Array-index classification reads NameMeta's cached `array_index`
   (N1.2), deleting the per-access numeric-string parse.

**Exit**: dynamic intern counter (JR2.6) reads **0** for canonical-shape
property lookups across the benchmark corpus; census shows raw-key internal
entry points at 0; `heap_create_name` references in `lambda/js` reduced by
an order of magnitude (target: < 250, boundary/diagnostic sites only);
`lambda/js` LOC strictly below the §2 baseline already at this phase.

### N4 — Well-known names: `js_wk` and the special-name routing conversion

1. Build the `js_wk` table at isolate boot (JR2.4): every spec-mandated
   special property name (`length`, `name`, `prototype`, `constructor`,
   `__proto__`, `message`, `stack`, …) canonicalized once; consumers compare
   `key == js_wk.length`.
2. Convert the `js_property_get`/`set` special-name region
   (`js_runtime.cpp:4334–5590` and its set-side twin) from length-guarded
   `strncmp` chains to `js_wk` pointer compares — dispatch structure stays,
   the comparisons change; wholesale restructuring of that region is R4's
   (JR6 property path), not this plan's.
3. Sweep the remaining in-scope routing `strncmp`/`strcmp` sites found by
   the N0 census; the allowlist (raw text, diagnostics) is recorded in the
   census tool, not in reviewers' heads.

**Exit**: `strncmp` count in `js_runtime.cpp` reduced from 486 to the
allowlisted residue (target < 60); census clean; suites green; measurable
LOC reduction (chain lines collapse to single compares).

### N5 — W2 `ctor_id` + semantic SYMBOL/PRIVATE keys

1. **W2**: stamp `ctor_id` on `JsFunction` at `js_create_constructor`
   (`JS_CTOR_*` already exists); dynamic `new` dispatches on it; `bind`
   copies it; fold `special_ctor_kind` name-sniffing into it; delete the
   ~60-arm constructor name chain and the `"bound "`-prefix strip.
2. **SYMBOL/PRIVATE become semantic PropertyKeyRefs** (NI1/NI15): unique
   allocation bypassing the spelling hashmap; `Symbol.for`/well-known
   Symbols reuse their registry record; the runtime private environment
   supplies PRIVATE refs. Delete the `__sym_N` conversion and the
   `__private_<class-index>_` spelling synthesis + prefix-parsing brand
   checks. Identity hash per NI9 so same-description symbols do not collide
   into one bucket.

**Exit**: constructor-dispatch and symbol/private fixtures green (js262
Symbol/private-field sections are the gate); prefix-parsing code deleted;
grep for `__sym_` / `__private_` synthesis returns only the
compatibility-diagnostic residue; LOC strictly reduced again.

### N6 — Final validation, measurement, and record

1. Full gate run (§4.3), profile rerun, benchmark A/B.
2. Update the redesign doc: §5.1 census row (4 → achieved value), JR2
   section gains the forward pointer to this doc's JR2.1–JR2.8 (the JR3
   precedent); `Lambda_Design_Name_Identity.md` gets a status note per
   stage (W4.1 ✓, W4.2 ✓ STRING+SYMBOL/PRIVATE, W4.3 still gated on DO16,
   W1 deferred to R3, W2 ✓, W3 unchanged).
3. Closing table in this doc: LOC, census, intern counter, profile bucket,
   before/after — same units as the §2 baseline.

## 4. Verification — what to check and how

### 4.1 The instruments

**(a) Key-form census** (static, no build):

```bash
python3 utils/js_name_key_census.py --violations
```

**(b) Dynamic intern counter** (JR2.6) — run the benchmark corpus with the
profile counter enabled; the property-lookup intern event must be 0:

```bash
JS_EXEC_PROFILE=1 ./lambda.exe js test/benchmark/jetstream/richards.js --no-log
```

**(c) Emission** — MIR ratchet plus the existing error-lane budgets must not
drift (name changes must not add calls or checks):

```bash
./test/test_mir_ratchet_gtest.exe && ./test/test_js_mir_emission_gtest.exe
```

**(d) Behavior**:

```bash
make test-input-baseline && make test-lambda-baseline
./test/test_js_gtest.exe && make test262-baseline
make test-jube-node-error-lane
```

**(e) LOC** (JR2.8):

```bash
wc -l lambda/js/*.c lambda/js/*.cpp lambda/js/*.h lambda/js/*.hpp | tail -1
```

### 4.2 Per-phase exit gates

| gate | check | pass condition |
|---|---|---|
| **G1 census** | instrument (a) | no new raw-key entry point ever; N3+: internal raw-key entries = 0 |
| **G2 interning** | instrument (b) | N3+: 0 interns per canonical-shape lookup on the corpus |
| **G3 identity** | fixture: canonical JS shape has `key_ref`, document shape has NULL `key_ref`, both resolve correctly | green from N2 on (JR2.5) |
| **G4 behavior** | instrument (d) | green; goldens byte-identical; input-baseline proves Mark/Input untouched |
| **G5 emission** | instrument (c) | ratchet green; intended growth re-baselined in the same commit |
| **G6 LOC** | instrument (e) | net-negative in `lambda/js` per phase from N3 on; N1/N2 ≈ neutral with additions itemized |

Additionally: every phase that touches `lambda/core` reruns the NamePool
unit tests and `make test-lambda-baseline` (NI §9 gate), and any commit
converting a routing chain cites the census before/after counts in its
message.

### 4.3 Final exit gates (N6)

1. **G1–G6** green on the full tree.
2. **Census row**: key representations in property APIs = **1** internal
   (+ the boundary wrapper), reported by instrument (a) and mirrored into
   the redesign doc's §5.1 table.
3. **Interning**: instrument (b) = 0 on the full benchmark corpus; dynamic
   pool size before/after the corpus reported (DO16 growth-bound evidence).
4. **Profile** (release_profile, §6 protocol): name/shape lookup bucket
   from the N0-refreshed baseline to **≤3%** (the redesign's own §5.3
   target); `name_pool_create_strview` out of the top-20 tail symbols
   (Tune1-P2b acceptance, inherited).
5. **Performance** (release builds only, per CLAUDE rule 10): Result-series
   benchmark A/B non-regressing; wall time reported. Identity wins that
   cost wall time are not wins.
6. **LOC — the mandated final gate (JR2.8)**: `lambda/js` total strictly
   below **228,928**. Reported alongside: the core-bucket number (73,154
   baseline), and `lambda/core`+`lib` growth attributable to N1/N2, stated
   plainly. The gate is on `lambda/js`; the report is on everything.
7. **GC/lifetime**: full suites under `LAMBDA_GC_FORCE_EVERY=1
   LAMBDA_GC_POISON_FREED=1` for the JS gtest + error-lane fixtures —
   canonical refs are pool-owned, and a use-after-pool-release would look
   exactly like a name-identity bug.

## 5. Risks and tripwires

- **A wrong canonicalization is silent property aliasing** — two spellings
  mapping to one ref, or one spelling to two refs. The two-identity hazard
  (JR2.2) is checked by construction (the wrapper *is* canonicalize +
  `_key` call), and G3's dual-shape fixture covers the NULL-`key_ref`
  boundary. js262's property-key sections are the behavioral backstop.
- **Document-shape reads through JS** (JR2.5): DOM over parsed HTML hits
  `key_ref == NULL` shapes; the seam path must stay correct and must not
  quietly intern document keys into isolate state (that would be the DO16
  growth bug). The intern counter (G2) catches it: document-heavy fixtures
  must also read 0.
- **Baked canonical pointers vs the L1 MIR cache** (JR2.7): a cached module
  re-entered after its pool died would dereference freed NameRecords. DO16
  already flags "GOT rebuild on L1 hits must not be skipped" for the future
  design; the interim invariant is pool lifetime ≥ module lifetime, and the
  forced-GC/poison run (G7-final) plus an L1-cache-hit fixture guard it.
- **NameMeta prefix ABI** (N1): every String allocation path must agree on
  the prefix or `is_pooled` recovery reads garbage. The NamePool unit
  tests + input-baseline are the gate; land N1 alone, never folded into a
  consumer phase.
- **Per-isolate `js_wk`** (N4): boot-time refs are isolate-owned (NI14,
  JT1); a process-shared cache would be the Runtime_Globals violation.
- **Scope creep toward R4**: N4 changes *comparisons*, not the property
  path's structure. The 54-entry-point consolidation and elements kinds
  are JR6. If a phase finds itself redesigning dispatch, it has left JR2.

## 6. Rule-17 landing checklist

- D4.6/D3.4 need no revision — this is conformance work against them. If
  N2 discovers a representation the rulings do not cover (e.g. key_ref on
  a shape kind D3.4.4 does not mention), revise the ruling in place
  (`v2` + semver bump) and mirror here.
- `Lambda_Design_Name_Identity.md`: per-stage status notes (N6.2).
- `JS_Runtime_Redesign.md`: JR2 forward pointer to JR2.1–JR2.8; §5.1 row
  updated with the measured value.
- The DO16 entry gains a line when JR2.7's interim invariant lands, so the
  eventual W4.3 design knows what it must preserve.

## 7. Open items

- **The refreshed lookup-bucket number** (N0.3) may re-rank N3/N4 — if
  post-Tune2 profiles show the special-name chains outweigh probe
  interning, N4 can land before N3; both depend only on N2.
- **W1's residual round-trip cost** (id→name→chain + per-call
  `heap_create_name` in builtin dispatch) is accepted until R3; if R3
  slips, the round-trip alone (not the jump table) can be cut under JR2.3
  without building R3's dispatch.
- **SYMBOL/PRIVATE (N5.2) is the most behavior-sensitive slice**; if js262
  fallout exceeds a review cycle, N5.1 (W2) lands alone and N5.2 moves to
  its own follow-on — the LOC gate must then be met without it.
- **65,535-per-pool bound** (NI2) is generous for `js_wk` but the census
  should report pool occupancy so growth is visible.
