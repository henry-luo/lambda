# Lambda Stack Frame — Implementation Plan, JS Container `extra` Migration (SF15-J)

**Status:** **IMPLEMENTED**
**Date:** 2026-07-15
**Design:** `vibe/Lambda_Design_Stack_Frame.md` SF15/SF15-J (reserved props tail slot; JsArray subtype rejected). Companion to phase 2 (`vibe/Lambda_Impl_Stack_Frame_JS.md`): this plan is a **prerequisite for stage J3d** (JS number watermarks on) and independent of J1/J2 (rooting) — it can run before or in parallel with them.

---

## 0. Implementation record (2026-07-15)

All JP0–JP4 code and permanent-regression work is complete. The required
Lambda and test262 baselines are green on the final candidate.

- **JP0 — census and provenance:** the checked inventory is
  `vibe/Lambda_Impl_Stack_Frame_JS2_Inventory.md`. It classifies every original
  JS `extra` consumer, the remaining `ArrayNumShape*` overload, both array-item
  allocator paths, sparse-map ownership, tracing, and compaction.
- **JP1 — access boundary:** all companion reads, writes, and presence tests use
  `js_array_has_props`, `js_array_props`, and `js_array_set_props`. Indexed
  walkers use `container_dense_capacity`; physical capacity is no longer an
  observable indexed range when a counted tail exists.
- **JP2 — representation and GC:** `CONTAINER_FLAG_JS_PROPS` gates a tagged Map
  Item at `items[capacity - 1]`; `extra` uniformly counts the complete tail.
  `expand_list` and `js_array_install_runtime_items` share
  `list_relocate_owned_tail`, while GC marks the Map slot precisely and never
  interprets `extra` as a possible pointer. Sparse growth stamps newly exposed
  dense storage as holes.
- **JP3 — ownership:** arbitrary JS stores use the shared owned-scalar path;
  slice/concat/`Array.from` clone boundaries re-home wide payloads. Permanent
  tests cover props-first and wide-first ordering, forced GC, growth, sparse
  indices, clone/concat, and a Lambda-held array that JS promotes in place.
- **JP4 — closure:** SF15-J, the JS value model, Lambda GC documentation, and
  the parent JS implementation record describe the live representation. The
  old raw-pointer wording and conservative trace rule are removed.

### 0.1 Final verification snapshot (2026-07-16)

- `make test-lambda-baseline`: **3419/3419 passed**, 0 failed (2105/2105
  input-parser tests and 1314/1314 Lambda runtime tests, including 332/332 JS
  transpiler tests).
- `make test262-baseline`: **40261/40261 fully passed**, 0 failed, 0
  non-fully-passing, 0 regressions; 2628 skipped by the maintained baseline.
- Focused permanent regressions passed in release and forced-GC/ASan runs:
  `regression_js_array_props_tail.js`, `js_array_props_tail_bridge.ls`, and the
  unchanged `proc_stack_frame` golden. The complete ASan JS gtest run passed
  **332/332**.
- `make editor-4c-js`: **1931/1931 passed** (the non-UI editor Phase A gate).
- The full Node suite and editor-view UI suite are intentionally **not closure
  gates for this run**, per user direction because of their runtime. Node was
  stopped at 2400/3528 with 2400 passed and no failure/timeout/crash. The
  editor-view run was stopped after exposing two pre-existing UI failures
  (`color-picker` and `image-resize`); no claim of a green editor-view gate is
  made here. The broader DOM UI gate was likewise skipped.

---

## 1. Why this is its own plan, and why now

Phase 2's scope note claims "JS containers are Lambda containers, so SF15/SF16 re-homing is already live for JS … container copy-in/copy-out: nothing to do." That is wrong for JS **arrays**: the shared helpers being shared is exactly what creates the hazard. Verified current state (2026-07-15):

- JS arrays store their companion props map as a raw pointer in `extra` — `arr->extra = (int64_t)(uintptr_t)obj.map` — while the SF15 tail machinery reads/writes `extra` as a **count** of wide-scalar tail entries (`lambda-eval.cpp:5641/:5651`) and shared growth checks do arithmetic on it (`arr->length + arr->extra + 2 > arr->capacity`, `lambda-data.cpp:720/:740`). A JS array flowing through any shared path treats a `Map*` as a count; a Lambda wide scalar copied into a props-bearing JS array clobbers or misreads the pointer.
- Exposure is **live today** for polyglot flows (Lambda code writing int64/DateTime into a JS-created array) and becomes systematic at phase-2 **J3d**, when JS out-of-band doubles start homing into container tails.
- The GC handles the dual meaning by guessing: the array trace marks `extra` as a *possible* Item (`gc_mark_possible_item`, `gc_heap.c:1004`), and the compactor uses `extra > 0` as "has tail entries" (`gc_heap.c:1293–1307`) — a `Map*` value (always positive) merely triggers a harmless-but-wasteful fixup scan. Both are the raw-word-as-pointer ambiguity SF12 banned.

### 1.1 Site census (verified 2026-07-15)

- `->extra` references under `lambda/js/`: **248 across 10 files** — `js_runtime.cpp` 121, `js_globals.cpp` 55, `js_event_loop.cpp` 29, `js_property_attrs.cpp` 12, `js_dom.cpp` 12, `js_props.cpp` 11, `js_typed_array.cpp` 3, `js_util.cpp` 2, `js_assert.cpp` 2, `js_runtime_state.cpp` 1.
- Read casts `(Map*)(uintptr_t)…->extra`: **~95 sites**.
- Write sites `extra = (int64_t)(uintptr_t)…`: **12** — `js_globals.cpp:8944`, `js_runtime_state.cpp:1327`, `js_property_attrs.cpp:283`, `js_props.cpp:804`, `js_runtime.cpp:5272` (sparse), `:5912`, `:6000`, `:8414`, `:8566`, `:8593`, `:8657`, `:27184`.
- Truthiness tests (`extra == 0` / `!= 0` as "has companion"): e.g. `js_arguments_mapped_get` (`js_runtime.cpp:8086`), `js_globals.cpp:619` — full inventory in JP0.
- **The JS MIR lowering never touches `extra`** (no `js_mir_*` file references it) — this is a **runtime-C++-only migration; zero transpiler changes**.
- Creation/growth paths: `js_array_new` → `js_array_new_sparse_length` (`js_runtime.cpp:8046`) allocates the items buffer via `mem_alloc(MEM_CAT_JS_RUNTIME)` and installs it with `js_array_install_runtime_items` (`js_runtime.cpp:280`; growth re-installs at `:5490`, `:5848`) — **buffer is separate from the header**, so in-place promotion is available on the JS side; Lambda-born arrays grow via `expand_list`.
- Sparse arrays: `js_array_ensure_sparse_map` (`js_runtime.cpp:5253`) builds a `SparseArrayMap` (base-first `Map`, `lambda.h:767`) and stores it in `extra` the same way. Its finalizer keys off the **Map's own** `map_kind` (`lambda-mem.cpp:164`) — unaffected by where the array points to it.
- Container flag space: first flag byte uses bits 0–5 (`is_content` … `is_immortal`, `CONTAINER_FLAG_IMMORTAL = 1u << 5`, `lambda.h:663`); **bit 6 is free** for `CONTAINER_FLAG_JS_PROPS`.

---

## 2. Target invariants (from SF15-J)

1. **Props slot**: when `CONTAINER_FLAG_JS_PROPS` is set, `items[capacity-1]` holds a **tagged MAP Item** referencing the companion (`MAP_KIND_ARRAY_PROPS` or `MAP_KIND_ARRAY_SPARSE`). Flag set ⟺ slot holds a valid tagged Map Item. Wide-scalar tail entries grow down from `items[capacity-2]`.
2. **`extra` is uniformly a count** of tail entries; the props slot counts as 1 (wide count = `extra - (flag ? 1 : 0)`). All shared capacity/copy arithmetic works unchanged by construction.
3. **Flag-gated interpretation** (SF12): no code interprets any tail word as a pointer without the flag. The `+1` accounting appears only inside central accessors, never inline at use sites.
4. **Identity-preserving promotion**: attaching props to an array without a slot grows/relocates the **items buffer only** — the header (and every outstanding reference to it, on both language sides) is untouched.
5. **Precise GC**: the array trace marks the props slot as a tagged Item behind the flag; `gc_mark_possible_item(extra)` is deleted.

## 3. New API (all in `lambda.h`, next to the existing container helpers)

```c
#define CONTAINER_FLAG_JS_PROPS (1u << 6)  // items[capacity-1] holds tagged props-Map Item

bool  js_array_has_props(const Array* arr);                        // flag test
Map*  js_array_props(const Array* arr);                            // flag-gated read; NULL if unset
int64_t container_tail_reserved(const Array* arr);                 // 1 if flag set else 0 — the ONLY place the +1 lives
int64_t container_dense_capacity(const Array* arr);                // capacity - counted tail
void js_array_set_props(Array* arr, Map* props);                  // ensure slot (promote if needed), store tagged Item, set flag
```

These accessors are out-of-line because `lambda.h` is included at points where
`Array` and `Map` are still incomplete public types. The representation remains
centralized; call sites do not inspect the props slot.

`js_array_set_props` is the promotion choke point: if no slot is free (`length + extra + 1 > capacity` after accounting), grow via the array's own growth path, then write `items[capacity-1] = {.map = props}` tagged `LMD_TYPE_MAP`, `extra += 1`, set flag.

---

## 4. Stages

### Stage JP0 — Audit and inventory (no behavior change)

- **JP0a. Full site inventory**: script-generated list of all 248 `->extra` sites under `lambda/js/`, classified read-cast / write / truthiness / count-arithmetic / other. The 3 `js_typed_array.cpp` sites need individual triage (typed arrays wrap `ArrayNum`, whose `extra` may be an `ArrayNumShape*` — that overload stays, SF15).
- **JP0b. Buffer-provenance decision for promotion**: Lambda-born arrays carry fused `heap_calloc` or `expand_list`-grown buffers; JS-grown buffers are `mem_alloc(MEM_CAT_JS_RUNTIME)` installed via `js_array_install_runtime_items`. Decide how `js_array_set_props` routes growth by provenance (existing `is_heap`/`is_data_migrated`-style flags vs. always routing through one growth entry point). The wrong allocator pairing is the rpmalloc-corruption class of bug (cf. LR_11 ui_mode landmine) — this decision gates JP2.
- **JP0c. GC/sparse audit**: confirm the companion Map (and `SparseArrayMap.sparse_indices` contents) is fully traced when reached via a tagged Item rather than the conservative `gc_mark_possible_item` probe; enumerate any other raw consumer of array `extra` outside `lambda/js/` (`gc_heap.c` trace `:994–1004`, compactor `:1289–1313`, and any `lambda/format/`/validator walkers).
- Exit: inventory checked in under `vibe/`, provenance decision recorded here.

### Stage JP1 — Accessor sweep over the OLD representation (mechanical, zero behavior change)

- Implement the §3 accessors **backed by today's representation** (`extra` = raw `Map*`): `js_array_has_props` → `extra != 0`, `js_array_props` → the cast, `js_array_set_props` → the store. `container_tail_reserved` returns 0 for now.
- Sweep all ~95 read casts, 12 writes, and the truthiness tests to the accessors, file by file (order by density: `js_runtime.cpp`, `js_globals.cpp`, `js_event_loop.cpp`, then the rest).
- This is the risk-isolation move: after JP1, the representation lives in **one place**, and the JP2 flip cannot miss a site.
- Gates (§5) must be **byte-identical green** — any diff means a mis-swept site.

### Stage JP2 — Representation flip (atomic)

- **JP2a.** Creation: JS array constructors leave the flag clear (lazy slot — common-case arrays pay nothing). `js_array_set_props` implements real promotion per JP0b; `js_array_ensure_sparse_map` routes through it.
- **JP2b.** Accessor internals flip to the tail slot: flag test, `items[capacity-1]` read/write, `extra` counts the slot. `container_tail_reserved` returns the real reservation.
- **JP2c.** Growth: `js_array_install_runtime_items` and `expand_list` move the props slot to the new `items[capacity-1]` when capacity changes (one central copy in each, behind `container_tail_reserved`). The existing wide-tail rebase machinery anchors at `capacity - 1 - tail_reserved`.
- **JP2d.** GC: array trace replaces `gc_mark_possible_item(extra)` with a flag-gated **precise** mark of `items[capacity-1]`; compactor keeps its `extra > 0` fixup trigger (now uniformly correct — `extra` is a real count) and the fixup scan skips/preserves the props slot (a Map reference never points into the buffer, so `gc_fixup_embedded_pointers` already leaves it alone by range check — verify, don't assume).
- **JP2e.** Delete the stale `// arr->extra is cast to Map*` comment on `SparseArrayMap` (`lambda.h:767`) and update the `extra` field comments (`lambda.h:722/:738`).
- Gates: full suite (§5) + GC-stress + ASan on JS suites; a dedicated regression exercising promotion identity (Lambda holds a ref, JS attaches a prop, Lambda still observes the same array).

### Stage JP3 — Unlock SF15 for JS arrays (the payoff)

- **JP3a.** Shared tail helpers (`array_set` copy-in, clone, the concat-rebase fix from SF15's verification item) honor `container_tail_reserved` — wide scalars and props coexist in one buffer.
- **JP3b.** Polyglot regression matrix: Lambda writes int64/DateTime/out-of-band double into a props-bearing JS array (both orders: props first, wide scalar first); JS reads them back; growth while both are present; clone/concat of mixed arrays.
- **JP3c.** Record completion in `vibe/Lambda_Impl_Stack_Frame_JS.md` — the §1 "container copy-in/copy-out: nothing to do" claim becomes true only at this point, and **J3d must not ship before JP3 lands**.

### Stage JP4 — Docs and closure

- Update `doc/dev/js/JS_03` (value model — array/props representation) and `doc/dev/lambda/LR_08` (trace walk — precise props slot); design-doc SF15-J status → implemented; synchronize the checked-in parent implementation record and inventory.

---

## 5. Gates (every stage)

- `make node-baseline` — no regression from 1492/3517; JS gtest suite green; `make editor-4c-js` 1931/1931; DOM/editor view suites (jquery/popper knowns tracked separately)
- Full **Lambda** baselines byte-identical (`make test-lambda-baseline`) — Lambda-side container behavior must be unaffected until JP3 deliberately extends it
- JP2+: GC-stress + ASan on JS suites; promotion-identity regression; `proc_stack_frame` golden unchanged
- JP3: polyglot wide-scalar matrix (JP3b) added to the permanent suite

## 6. Success criteria

`extra` means one thing everywhere — a count of tail entries; JS props ride a flag-gated, precisely-traced reserved tail slot; `gc_mark_possible_item(extra)` is deleted; promotion preserves array identity across the language boundary; and the SF15 wide-scalar invariant holds for JS arrays, clearing the prerequisite for phase-2 J3d.
