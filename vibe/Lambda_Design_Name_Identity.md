# Lambda Name Identity & Name Pool Sections — Design

**Date:** 2026-07-31
**Status:** PROPOSAL — design settled in review discussion (rev 1); not implemented.
The routing cleanups in §8 that need only integer ids (W1, W2) can start
independently of the section/GOT infrastructure.
**Scope:** `lambda/core/name_pool.*`, `lib/string.c`, LJS runtime + MIR lowering
(`lambda/js/`), per-module runtime state, and (design-only) the future MIR cache
container format.
**Relation to prior docs:** builds on `vibe/Lambda_Name_Pool.md` (hierarchical
name pool, symbol ≤32 pooling — implemented Nov 2025); complements
`vibe/Lambda_Design_MIR_Cache.md` / `_L3.md` (the L2+ serialized cache becomes
section-based, §7); per-isolate ownership follows `vibe/Lambda_Js_Thread.md`
(JT1 context-thread rule) and the `vibe/Lambda_Design_Runtime_Globals.md`
invariant that no synchronization lands on repeated execution paths. Motivated
by the 2026-07-28 LJS string-dispatch census (§9).

---

## 1. Goal

> **One identity per name spelling per isolate, assigned once.** Every runtime
> routing comparison — property find, method find, builtin/sys-call routing,
> constructor dispatch — becomes a pointer or integer compare. Content
> comparison survives only at true boundaries: the first intern of dynamic
> bytes, and the seam with id-less data (Mark/Input documents, raw C-ABI keys).

Non-goals:
- No JS semantics change. Lookup order, proxies, exotic receivers unaffected.
- Mark/Input **document data stays out of the id space** (NI13). A JSON with a
  million distinct keys must not bloat session-lived structures.
- The lambda-ELF container (§7) is direction-setting only; it is NOT part of
  this proposal's implementation.

## 2. Terminology and core decisions

| ID | Decision |
|----|----------|
| **NI1** | **NameRef = canonical `String*`** — the sole runtime identity, per isolate. All hot structures (ShapeEntry, ICs, member records, helper ABIs) compare NameRefs. |
| **NI2** | **NameId = `[16-bit local slot][16-bit byte offset]`** — a *serialized positional reference*, never an identity. It appears only in sections and module name tables, is never compared, and is decoded only at load-link time — never in the instruction stream. |
| **NI3** | **Name pool section** = ELF-strtab-style immutable byte image containing records in the runtime `String` layout (§3). Blittable/mmap-able; shareable read-only across threads, processes, and sessions. The `String` layout thereby becomes a serialization ABI — the section format carries a version, gated by the cache's build-version check. |
| **NI4** | **Slot numbering, per module, fixed at transpile time:** slots `0..k` = the well-known vocabulary sections (build-generated, shipped in the runtime binary); next `n` slots = the script's own sections (`n` recorded in the module header; spillover when a section would exceed 64KB); imported modules' sections follow, in import order. Stable across sessions by construction. |
| **NI5** | **Initial reference policy: a module bakes NameIds only into well-known + its own sections.** Referencing an *import's* sections (transpile-time cross-module byte dedup) is a KIV optimization — it couples the importer's cache blob to the import's section layout (any layout change invalidates dependents). The slot-table format supports it from day one; the policy can flip later without a format change. |
| **NI6** | **Per-isolate canonicalization registry:** spelling → NameRef, **first definer wins**. Zero-copy: "canonicalize" *chooses* a pointer (into a mapped section or the dynamic pool), it never copies bytes. Implemented over the existing `NamePool` machinery — the registry is today's pool hashmap plus canonical semantics. |
| **NI7** | **Per-module GOT** (`modstate->name_refs[]`): dense array of NameRefs, one entry per name the module references, filled at load-link. **MIR bakes the dense GOT index** (not the NameId); runtime resolution is one indexed load, hoistable per function — same cost class as the existing `js_get_module_var(int)` state fetch. |
| **NI8** | **Lifetime = pool-level refcount.** Section images are immutable; the per-process `NamePool` control block (mutable, holds `ref_count` + mapping handle) governs when backing memory dies. The registry retains every pool it has drawn at least one canonical from, for isolate lifetime. A module unload drops only its own ref. |
| **NI9** | **Per-name flags are baked at build time** into section records (`is_ascii`, `is_array_index`, `is_private`, well-known-symbol encoding). Exactly **one shared classifier function** is used by the transpiler (build time) and the registry (dynamic intern) so a static `"0"` and a dynamic `"0"` can never classify differently. |
| **NI10** | **FNV `name_id` is demoted to hash-only.** It remains the bucket index for typemap tables and the reject filter at the id-less seam. Equality upgrades: canonical-vs-canonical = pointer compare, definitive both ways; memcmp survives only when either side lacks a canonical (Mark data, raw `(chars,len)` C-ABI keys). `ShapeEntry` keeps its current field pair — `name` (now definitive when canonical) + `name_id` (now hash-only) — so the migration does not change struct layout. |
| **NI11** | **Dynamic pool alignment (§6):** the isolate's runtime name pool allocates the *same record layout* via the *same classifier*, in 64KB section-format segments. Dynamic names are id-less (no NameId ever refers to them) but otherwise indistinguishable from section names — downstream code cannot tell which memory a NameRef lives in. |
| **NI12** | **eval / `new Function` / REPL** follow the same pipeline: runtime transpilation emits an in-memory name section in the standard format, registered as a pool (control block without file backing), GOT built the same way. One pipeline, no special cases. |
| **NI13** | **Documents excluded:** MarkBuilder/Input data keeps today's per-Input pools and the content-hash seam. Only script/module names, the well-known vocabulary, and dynamic-interned runtime names participate in canonical identity. |
| **NI14** | **Threading:** sections immutable-shared; registry, GOT, and dynamic pool are isolate-owned single-writer (no hot-path synchronization, per the Runtime_Globals invariant). The only locked operation is pool control-block bookkeeping (register/retain/release) — rare, load-time events. Two isolates may pick different canonicals for the same spelling; harmless, NameRefs never cross isolates (JT: no shared JS objects). |

**Terminology note:** NameId is *positional* ("where a spelling is written down"),
NameRef is *identity* ("which name this is"). Code that compares NameIds is a bug
by definition.

## 3. Section format

A name pool section is a pure byte image of 8-byte-aligned records:

```
section:  [record][pad][record][pad]...            (≤ 64KB, byte offsets 16-bit)
record:   String layout — { u32 len; u32 flags; char chars[len]; '\0' }
          flags carries is_ascii + baked classifier bits (NI9)
```

- A NameId's offset addresses the record start; the **NameRef points at the
  record itself** — it IS a valid `String*`. Existing `String` consumers work
  unchanged, including `s2it()` materialization as a JS value.
- Records are **static data**: immutable, outside GC, never individually
  ref-counted (the invariant `name_pool.hpp` documents). Nothing ever writes a
  mapped section page, so read-only sharing is real sharing.
- Spare `String.flags` bits are the preferred home for classifier bits; if the
  budget runs out, a fixed-size record prefix before the `String` is the
  fallback (flags then reachable from any NameRef at a fixed negative offset).
  Decide at implementation; the section version covers either.
- **Budget:** ~8B header + avg ~10B chars + NUL + alignment ≈ 24B/name → roughly
  2.5–3K names per 64KB section. Big bundles spill into additional own-slots
  (NI4); the well-known vocabulary may span several build-generated sections.

The **well-known sections (slots 0..k)** are generated at build time from a
`js_name_catalog.def`-style listing (well-known property names, builtin method
names, DOM vocabulary) and shipped as rodata in the runtime binary — always
mapped, registered with the registry **first at boot**, so well-known spellings
are deterministically first-definer and their NameRefs are compile-time-known
addresses within a process. Runtime C code reaches them through direct globals
(the `js_wk.*` singleton struct); only MIR-baked references go through the GOT
(cache blobs cross processes, so emitted code can never carry the addresses).

## 4. Load-link pipeline

```
1. Obtain section images        — mmap from cache blob, or in-memory from a fresh
                                  transpile (eval: NI12). Zero fixup: images are final.
2. Register pools               — create control blocks; build the module's
                                  slot → section-base table (link-time only).
3. Walk the module name table   — a serialized array of NameIds in GOT order:
     for k, [slot][offset]:
        record   = slot_base[slot] + offset          (a String*)
        nameref  = registry.canonicalize(record)      (first definer wins;
                                                       retains record's pool on adoption)
        got[k]   = nameref
4. Drop the slot table          — nothing at runtime decodes NameIds.
                                  (Keep behind a debug flag for dump tooling.)
5. Execute                      — code sites load got[k]; helpers receive NameRefs.
```

Static shape templates (object literals, classes) serialized in future blobs
reference names by NameId too and are materialized in the same pass, stamping
`ShapeEntry.name` with the canonical NameRef — which is what makes module A's
shapes matchable by module B's lookups with a pointer compare.

## 5. Runtime effect, structure by structure

| Site | Today | After |
|------|-------|-------|
| Named load/store IC key match | baked rodata `char*` ptr-compare, memcmp fallback ([js_runtime.cpp:7755](../lambda/js/js_runtime.cpp)) | NameRef compare, no fallback |
| IC hit | shape ptr compare (unchanged) | unchanged |
| Typemap lookup confirm | id reject → ptr try → **memcmp confirm** ([lambda-data.hpp:477](../lambda/lambda-data.hpp)) | canonical-vs-canonical: ptr compare definitive; memcmp only at NI13 seam |
| `js_property_get` special names | ~dozens of length-guarded strncmp per access (js_runtime.cpp:4334–5590) | `key == js_wk.length` etc. |
| String/Number/Array/Math method routing | name-string chains, ~45 branches (`js_string_method` js_runtime.cpp:22877); id path round-trips id→name→chain +alloc (js_runtime.cpp:10556) | `switch (builtin_id)` jump table end-to-end (W1) |
| Dynamic `new ctor(...)` | ~60-arm name chain + `"bound "` strip (js_runtime.cpp:2216) | `fn->ctor_id` int dispatch (W2) |
| Private / `__sym_N` keys | prefix memcmp per access | flag bit / singleton NameRef (NI9) |
| Array-index key classification | per-access numeric-string parse | baked `is_array_index` flag |
| DOM property access | `fn_to_cstr` + strcmp chains + SipHash record map (radiant_dom_bridge.cpp:3873, jube_interface.cpp:533) | VMap IC receiver kind + records keyed by NameRef (W5) |
| Module vars | `js_get_module_var(int)` (already index-based) | unchanged — the GOT follows its pattern |

## 6. Dynamic pool alignment — resolved points and open issues

Resolved by NI11:
- Same record layout, same classifier, same static/immutable discipline; a
  NameRef is uniform regardless of home.
- Dynamic segments use the 64KB section format, so a future feature (snapshot,
  hot-eval caching) could *promote* a dynamic segment to a serialized section
  without reformatting.

Open issues (accepted or KIV):

- **OI-NI1 (accepted): canonical is temporal, not preferential.** If a computed
  key interns `"foo"` before a lazily-loaded module with a static `"foo"`
  arrives, the dynamic record stays canonical and the section record's bytes go
  unused. Correctness is unaffected; well-known names never hit this (boot
  registers them first). Rebinding canonicals later is impossible by design
  (identity has already spread into shapes) — do not attempt.
- **OI-NI2 (pre-existing, KIV): unbounded dynamic intern growth.** Adversarial
  workloads minting unbounded unique computed keys grow the dynamic pool for
  isolate lifetime — exactly today's `heap_create_name` behavior, no
  regression. Weak interning/eviction is hard under the static-flag no-refcount
  design; revisit only if a real workload hits it.
- **OI-NI3 (decide at impl): flag-bit budget** in `String.flags` vs record
  prefix (§3).
- **OI-NI4 (KIV): import-slot byte dedup** (NI5) — enable only with cache
  dependency tracking keyed on import section layout hashes.
- **OI-NI5: GOT rebuild on the existing in-memory L1 cache hit.** L1 reuses a
  linked module within a process; the GOT must be (re)filled per isolate that
  adopts the module, mirroring how per-isolate module state is already
  instantiated. Cheap (one registry walk), but it must not be skipped.
- **OI-NI6: 16-bit offset headroom.** Raw byte offsets cap a section at 64KB;
  storing `offset>>3` would stretch to 512KB per slot if spillover ever proves
  annoying. Format-version bump; defer until evidence.

## 7. lambda-ELF container (design-only, NOT in this proposal)

The MIR cache blob becomes a sectioned container — "our lambda ELF":

```
header      magic, format version, build id, String-ABI version
sections    .names[0..n]   name pool sections (§3)         — mmap read-only, shared
            .nametab       module name table: NameId[] in GOT order
            .mir           serialized MIR instructions/module
            .const         const data (number literals, string literals, ...)
            (future)       .shapes (static shape templates), .imports (specifiers)
```

Properties: every section is position-independent (offsets, ids, bytes — never
addresses); `.names` pages are shareable across processes via the page cache;
the load-link pass (§4) is the only fixup. This is the container L2+ of the MIR
cache design should converge on; `Lambda_Design_MIR_Cache_L3.md` remains the
authority on invalidation/keying, this section only fixes the *layout*
direction. No implementation in this proposal.

**Refined 2026-07-31:** the data/const story and the final three-section layout
(names / data / code, serving both script caches and binary document caches)
are elaborated in `vibe/Lambda_Design_Const_Pool.md` (MarkPack) — its §5
supersedes the section sketch above.

## 8. Routing cleanups that ride on this (workstreams + phasing)

- **W1 — builtin method dispatch by id end-to-end.** Convert the
  `js_string_method` / `js_number_method` / `js_array_method` tail /
  `js_math_method` chains to `switch (builtin_id)` bodies; make
  `js_dispatch_*_builtin` call by id (deleting the id→name→`heap_create_name`
  round-trip and its per-call allocation); MIR lowering emits builtin ids for
  typed receivers. Independent of §2–§4 — pure integer scheme. **P0.**
- **W2 — `ctor_id` stamped on JsFunction** at `js_create_constructor` (the
  `JS_CTOR_*` id is already in hand); dynamic-new dispatches on it; `bind`
  copies it; fold `special_ctor_kind` name-sniffing into it. **P0.**
- **W3 — well-known singletons** (`js_wk.*`) for `js_property_get` special
  names; becomes the pool-0 vocabulary when §3 lands, can start earlier with
  boot-time `heap_create_name` pointers (interning already guarantees identity
  within the runtime pool). **P1.**
- **W4 — this design's core**: sections, GOT, registry, helper-ABI migration
  from `(char*, len)` to NameRef (dual entries during migration). **P2.**
- **W5 — DOM**: VMap IC receiver kind; finish converting legacy strcmp arms to
  member records; key records by NameRef. **P3.**
- **W6 — Node specifier index** per `Lambda_Design_Jube_Node_Hosting.md` (cold
  path; part of the JN stages, listed for completeness). **P4.**

**Gates:** js262 + node-baseline + DOM suites no-regress; MIR emission-budget
ratchet (`test/mir/mir_budgets.json`); Result-series benchmark re-run per
phase; exec-profile counters (existing `js_exec_profile` infra) on each
converted path proving the chains stop being hit; lambda-baseline 100% for any
`lambda/core` touch.

## 9. Appendix — 2026-07-28 string-dispatch census (motivation)

Where LJS routes by string comparison today (anchors verified 2026-07-28):

- `js_string_method` ~45-branch strncmp chain — `lambda/js/js_runtime.cpp:22877`
  (chain from :22936); number/array/math analogues.
- id→name round-trip + per-call `heap_create_name` in
  `js_dispatch_string_builtin` / `js_dispatch_math_builtin` —
  `lambda/js/js_runtime.cpp:10556–10605`.
- `js_property_get` special-name strncmp scatter — `lambda/js/js_runtime.cpp:4334–5590`.
- Dynamic `new` ctor-by-name chain (~60 arms, `"bound "` strip) —
  `lambda/js/js_runtime.cpp:2216`; memoized name-sniffing
  `js_function_special_ctor_kind` — `lambda/js/js_function.hpp:118`.
- `internalBinding`/require builtin-module memcmp chains —
  `lambda/js/js_runtime.cpp:39633`, `lambda/js/js_mir_entrypoints_require.cpp`.
- DOM: per-access `fn_to_cstr` + strcmp chains + SipHash member map —
  `lambda/module/radiant/radiant_dom_bridge.cpp:3179,3873`,
  `lambda/jube/jube_interface.cpp:533`, `lambda/js/js_dom.cpp:8940`
  (607 str/memcmps in file).
- Already id/IC-based (kept): named ICs (`js_runtime.cpp:7777`, receivers
  limited to MAP/ARRAY_PROPS at :7652), typemap FNV+ptr+memcmp
  (`lambda/lambda-data.hpp:397–590`), `js_get_module_var(int)`
  (`lambda/js/js_runtime_state.cpp:768`), `js_dispatch_builtin` int switch
  (`js_runtime.cpp:10596`), compile-time Math lowering
  (`lambda/js/js_mir_expression_lowering.cpp:5879`).
