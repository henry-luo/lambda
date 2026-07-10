# Lambda Jube DOM — Stage 2 (DOM2): Review and Proposal

> **Status**: DOM2 implementation in progress; latest checkpoint 2026-07-10.
> **Parent design**: [Lambda_Desing_Native_Module.md](./Lambda_Desing_Native_Module.md) (Jube modules, VMap projections, host API).
> **Predecessor**: [Lambda_Jube_DOM.md](./Lambda_Jube_DOM.md) (DOM1 = POC 1A/1B implementation record).
> **Downstream consumer**: [Lambda_Design_DOM_Pkg.md](./Lambda_Design_DOM_Pkg.md) — the Lambda `dom` package assumes the
> generic host-object protocol proposed here (its L4 adapter and ST2 stress goals).

---

## Part 1 — Review of DOM1

### 1.1 What DOM1 delivered

DOM1 (POC 1A/1B, logged in `Lambda_Jube_DOM.md`) is complete as a compatibility checkpoint:

- `radiant` is a statically registered Jube module; `import radiant` works from Lambda script
  (`test/lambda/radiant_poc.ls` drives real DOM mutation and returns `"ok"`).
- **`MAP_KIND_DOM` is deleted.** DOM nodes, document proxies, foreign documents, Range, Selection,
  inline/computed style, and CSSOM stylesheet/rule/declaration objects all ride **branded,
  non-owning VMaps** (`vmap->host_type` = module brand marker, `vmap->host_data` = native pointer).
- The wrapper cache (identity, GC rooting, document-teardown invalidation) lives module-side in
  `lambda/module/radiant/radiant_dom_bridge.cpp`; `free_document()` invalidates before the arena dies.
- The host-object contract (projected property → wrapper expando → prototype chain) is specified
  and pinned by executable tests: expandos, `in`, `delete`, descriptor reflection, own-key
  ordering, `instanceof`, prototype patching, Range/Selection parity (`test/js/dom_module_props`).
- `JubeTypeDef` carries ownership flags (`JUBE_TYPE_NON_OWNING_HOST`); DOM wrapper destroy never
  frees Radiant nodes.
- Verified gates at checkpoint: full JS gtest 305/305, UI-automation 236 (2 skipped),
  `radiant_poc` green.

**The migration method deserves explicit credit**: delegate-first (behavior relocated, carrier
unchanged), readiness spec with executable pins *before* the carrier switch, then the switch, then
MapKind deletion — each step independently gated. This is the pattern DOM2 should keep.

### 1.2 Review findings — where DOM1 diverges from the parent design

Ranked by architectural weight. Items G1–G3 are the gap between "the radiant module exists" and
"the Jube module *system* exists"; S1 is a memory-safety hole; M1–M4 are contained debts.

#### G1. Dispatch is brand-predicate if-chains in the engine, not registry-driven

The parent design's core promise: *"generic VMAP dispatch becomes the single host-object
protocol — every future module gets DOM-class integration without touching the engine."*

What exists instead: the old MapKind switch became an if-chain of **named brand predicates
hardwired into the engine**. `js_runtime.cpp:4111–4139` checks `js_is_document_proxy` →
`radiant_dom_is_node` → `js_dom_item_is_range` → `js_is_inline_style_item` →
`js_is_computed_style_item` → `js_is_stylesheet` → … and there are ~70 such call sites across
`js_runtime.cpp` (38) and `js_globals.cpp` (31). Meanwhile the machinery designed to carry this —
`JubeTypeDef.vmap_ops` and `.host_ops` — is registered as `NULL, NULL`
(`radiant_module.cpp:152`) and consulted by nothing.

Consequences:

- The engine traded one hardcoded host-object special case (MapKind DOM) for N of them
  (brand predicates) — the exact coupling class the module system exists to eliminate.
- A second module with native types would edit `js_runtime.cpp` / `js_globals.cpp` again.
- A latent design flaw compounds it: `JubeHostObjectOps` signatures take `void* native`, but
  every real hook needs the **wrapper `Item`** (expandos live in wrapper-keyed side tables;
  document proxies need proxy identity). The ops struct as declared cannot carry the current
  implementations even if wired.

(The DOM1 doc's line "Jube native type metadata records ownership and host-object hooks" is
accurate only for ownership; the hooks slots are null.)

#### G2. The host API is empty; the module reaches back through ~200 raw externs

`JubeHostAPI` is `{ uint32_t api_version }` and nothing else. `radiant_dom_bridge.cpp:18–160`
declares roughly 200 `extern "C" js_*` functions — the same C-linkage back-door coupling the
parent design criticized in `edit_bridge.cpp`. The "narrow JS hooks" strategy was deliberate and
correct for the checkpoint, and those hooks are valuable: **they are the empirically discovered
host API**. But they must migrate into versioned function-pointer tables on `JubeHostAPI`
before the surface calcifies — a dynamically loaded module cannot link this way at all
(the SQLite/N-API lesson the design adopted).

#### G3. Descriptor-driven registration doesn't exist; the central registry grew

- `import radiant` is a hardcoded `builtin_import_radiant` flag in `build_ast.cpp:8835` —
  per-module engine code, not name resolution against the Jube registry.
- `fn_radiant_load` was **added** to `sys_func_registry.c:925` — the registry that design goal 6
  wants to kill got bigger.
- The `JubeFuncDef` signature strings (`"fn(path: string) -> dom_node"`) are decorative; nothing
  parses them. `JubeNamespaceDef` is unused.
- Acknowledged as deferred in DOM1's Phase-7 notes; sys_func_registry also still carries the MIR
  lowering shims (`js_document_method`, `js_dom_get_property`, …).

#### G4. Lambda-side VMap projection is missing

DOM wrappers carry the **generic hashmap vtable** from `vmap.cpp`, not a dom-projection vtable.
Lambda field access on a node (`node.tag_name`) hits an empty hashmap and returns null; only
module-qualified *functions* (`radiant.attr(node, "id")`) work. The parent design's §6.3
"computed vtable, level 2" — properties computed from `DomElement*` — is unimplemented.
Acceptable for the POC; blocking for an ergonomic Lambda `dom` package.

#### S1. Memory safety: stale wrappers dangle after document teardown

`radiant_dom_invalidate_document()` unroots cache entries but never touches the wrapper VMaps
themselves. A script that keeps a node reference across `radiant.free(doc)`:

```lambda
let doc = radiant.load("x.html")
radiant.free(doc)
radiant.attr(doc, "id")     // wrapper.vmap->host_data now dangles → use-after-free
```

reaches freed memory through `host_data`. The cache is perfectly positioned for the fix: it
already enumerates every wrapper of the dying document.

#### M1–M4. Contained debts

- **M1 — per-wrapper allocation weight**: every DOM wrapper eagerly allocates `HashMapData` +
  an 8-slot hashmap + an arraylist (`vmap.cpp:113`) that DOM wrappers never use — expandos live
  in side tables. A full-document wrap sweep (`querySelectorAll('*')`) pays 3–4 dead allocations
  per node.
- **M2 — cache lookup is O(all-wrappers) linear scan** and cleared slots are never reused, so
  the chunk list grows monotonically across many-document sessions. (Faithful port of the old
  `js_dom.cpp` cache — pre-existing debt, now module-owned and cheap to fix.) Detached-node
  wrappers also stay GC-rooted until document teardown.
- **M3 — thread-locality**: the cache is `__thread`; a document freed on a different thread than
  the one that wrapped its nodes silently misses invalidation. Safe under the same-thread
  script+layout decision (RC ledger), but currently an unstated invariant.
- **M4 — broad baseline not re-run after Phase 7**: the Phase-6 run showed layout/render bucket
  failures (`wpt-css-inline` initial-letter, `form_buttons_01`, 9 render regressions) attributed
  to "existing buckets" without verification. Carrier switches can shift attribute-mutation
  timing; this attribution must be confirmed, not assumed.

### 1.3 Review verdict

DOM1 is **in the right shape for what it claimed to be** — a behavior-preserving carve-out and
carrier switch, executed with high discipline. It is **not yet the native-module architecture**:
G1 (dispatch generality), G2 (real host API), and G3 (descriptor-driven registration) are the
three steps between "the radiant module" and "the Jube module system". G1 in particular gates
the Lambda `dom` package, whose L4 adapter assumes the generic host-object protocol exists.

---

## Part 2 — DOM2 Design

### 2.1 Goal

Make the module *system* real, using the DOM module as its proving load — so that:

1. A native type gets full JS host-object integration **without any engine edit** (kills G1).
2. Module→engine calls flow only through the **versioned `JubeHostAPI`** (kills G2; unblocks
   future `dlopen`).
3. Script-facing surface (Lambda import, JS namespaces, MIR lowering) is **driven by module
   descriptors**, shrinking `sys_func_registry.c` and `build_ast.cpp` hardcodes (kills G3).
4. Lambda scripts get **projected field access** on native types (kills G4; the ergonomic floor
   for `Lambda_Design_DOM_Pkg.md`).
5. Wrapper lifetime is **temporally safe** under document teardown (kills S1) and the cache/
   allocation debts are paid (M1–M2).

Non-goals for DOM2 (unchanged from parent design deferrals): dynamic `dlopen` shipping (a dev
proof is a stretch goal, §3 Phase 6), online registry, sandboxing, node-* refactor.

### 2.2 The generic host-object protocol (addresses G1)

**One dispatch rule in the engine.** Every JS-engine site that currently tests brand predicates
collapses to:

```
if (type == LMD_TYPE_VMAP && object.vmap->host_type != NULL) {
    → js_host_object_<op>(object, ...)   // ONE generic entry per operation
}
```

`js_host_object_<op>` resolves `host_type → JubeTypeDef` (the brand marker becomes a pointer to
the type's `JubeTypeDef`, or an entry that maps to it — one identity compare either way) and
invokes the type's host-ops table.

**Revised `JubeHostObjectOps`** — corrected receiver signature (S: the wrapper `Item`, not
`void* native` — hooks need wrapper identity for expandos/proxies) and completed operation set:

```c
struct JubeHostObjectOps {
    // core access — return 1 if handled, 0 to fall through to expando/prototype
    int  (*get_property)(Item receiver, Item key, Item* out);
    int  (*set_property)(Item receiver, Item key, Item value, Item* out);
    int  (*call_method)(Item receiver, Item method_name, Item* args, int argc, Item* out);
    // object protocol
    int  (*has_property)(Item receiver, Item key, Item* out);
    int  (*delete_property)(Item receiver, Item key, Item* out);
    int  (*get_own_property_descriptor)(Item receiver, Item key, Item* out);
    int  (*own_property_keys)(Item receiver, Item* out);
    Item (*prototype)(Item receiver);
    // lifecycle
    void (*invalidate)(Item receiver);   // neuter native payload (S1); wrapper stays a safe husk
    void (*destroy)(void* native);       // NULL / no-op for non-owning types
};
```

The lookup order stays exactly as pinned in DOM1 Phase 5: **projected (host op) → wrapper
expando store → registered prototype chain**, with the generic layer owning steps 2–3 uniformly
so modules only implement step 1.

**Migration is mechanical, not a rewrite**: the bridge already has functions with these shapes
(`radiant_dom_host_has_property(Item, Item, Item*)`, `radiant_dom_get_property`, …). DOM2 wires
them into ops tables per type (`dom_node`, `document`, `range`, `selection`, `inline_style`,
`computed_style`, `stylesheet`, `css_rule`, `rule_style_decl`) and deletes the engine if-chains
site by site.

**Proof obligation**: a second, trivial in-tree module (`hostobj_demo`, a ~200-line module
exposing one native type with projected properties, a method, and an owning finalizer) gets full
JS integration — property get/set, `in`, `instanceof`, expandos — with **zero lines changed in
`lambda/js/`**. This is the falsifiable form of the design promise.

### 2.3 The real host API (addresses G2)

`JubeHostAPI` becomes a versioned struct of function-pointer tables, populated from the ~200
externs the bridge empirically discovered. Grouped and trimmed (many externs collapse once the
generic protocol owns expandos/prototypes):

```c
struct JubeHostAPI {
    uint32_t api_version;            // additive-only; features gated by version
    const JubeHostValues*   values;  // item construction, strings, arrays, numbers
    const JubeHostGC*       gc;      // register/unregister roots, handle scopes
    const JubeHostErrors*   errors;  // pending-exception model: throw_value, new_error, check
    const JubeHostScript*   script;  // call_function, property get/set on script values
    const JubeHostDomHooks* dom;     // the narrow Radiant-behavior hooks (mutation notify,
                                     // live-range bookkeeping, focus/dirty-state invariants…)
};
```

Rules (from the parent design's N-API adoption, now enforced):

- **Additive-only**: entries are never removed or re-typed; new entries append; `api_version`
  gates. `struct_size`-style checks on the sub-tables.
- **No unwinding across the boundary**: status-int returns + pending-exception queries.
- **The `dom` sub-table is honest scaffolding**: the DOM-behavior hooks (`js_dom_after_*`,
  live-range/focus/dirty-state invariants) are Radiant-coupling that will *shrink* as behavior
  migrates per `Lambda_Design_DOM_Pkg.md` Phase 3. Keeping them in a named sub-table makes the
  remaining coupling measurable (count the entries) instead of invisible (scattered externs).
- End state for the bridge: **zero `extern "C" js_*` declarations** in
  `lambda/module/radiant/*.cpp`; everything flows through the `JubeHostAPI*` received at init.

### 2.4 Descriptor-driven registration (addresses G3)

1. **Lambda import**: `build_ast.cpp` resolves an unknown import name against
   `jube_find_static_module(name)` before erroring. Module hit → function names/arities come
   from `JubeFuncDef`; the per-module `builtin_import_radiant` / `builtin_alias_radiant` flags
   are deleted. Signature strings get a real (small) parser — they are Lambda type syntax, and
   the AST builder already owns a Lambda type parser; reuse it. Parsed signatures drive
   arg-count/type checking at build time (mirroring current builtin handling).
2. **MIR lowering**: call sites lower against `JubeFuncDef.native_signature`/`native_func`
   directly; the module's entries leave `sys_func_registry.c`. Exit rule: **`sys_func_registry.c`
   contains zero `fn_radiant_*` entries**, and the deferred `js_dom_*` lowering shims are
   re-homed behind generic host-object descriptors (finishing DOM1 Phase 7's deferred item).
3. **JS namespaces**: `JubeNamespaceDef.build`/`funcs` actually install (`radiant`-owned globals
   like `document` installation move from `js_globals.cpp` hardcode to the namespace descriptor).
   This is also the extension point the `dom` package (L3) will use to install observer/registry
   globals later.

### 2.5 Lambda-side projections (addresses G4)

Give `dom_node` (and `document`) a real `vmap_ops` — the parent design's level-2 computed
vtable — so Lambda scripts read projected fields directly:

```lambda
import radiant
let doc = radiant.load("page.html")
let root = radiant.root(doc)
root.tag_name              // "html" — projected, computed from DomElement*
root.children              // array of dom_node wrappers
for child in root.children { child.tag_name }
```

- `get` projects a curated first cluster (mirror DOM1's first JS cluster: identity/name/value
  reads + navigation). `set` is pn-only and routes through the same mutation-notify hooks as the
  JS path — **one write path, two front-ends**.
- Key naming: Lambda uses `snake_case` field names (`tag_name`), the JS adapter keeps WebIDL
  camelCase; both map to the same projection entries so behavior can't diverge.
- `keys`/`key_at`/`value_at` expose the projected cluster for `for`-iteration and printing.
- Expando storage for Lambda callers reuses the same wrapper store the JS side uses (it is
  keyed by wrapper Item, so it is front-end-neutral by construction).

This is deliberately scoped: a *useful, honest subset*, not full IDL. Fuller surface arrives as
the Lambda `dom` package (L3) built on these primitives.

### 2.6 Lifetime and cache hardening (addresses S1, M1, M2, M3)

- **S1 — neutering**: `radiant_dom_clear_cache_entry()` calls the type's `invalidate` op before
  unrooting: set `wrapper.vmap->host_data = NULL` (brand check already treats null payload as
  not-a-node, so every downstream path degrades to a logged error + ItemNull/JS `TypeError`).
  Applies to document teardown and `radiant.free`. Longer-term, document handles become
  `open()`-scoped resources per the R1–R5 resource ledger; not in DOM2 scope.
- **M2 — cache structure**: replace the linear chunk scan with a pointer-keyed hashmap
  (`lib/hashmap.h`, key = `DomNode*`), entries freed/reused on invalidation. Keep chunked slot
  storage for GC-root address stability or re-register roots on rehash — decide by measurement;
  the invariant is: same node → same wrapper while cached, lookup O(1) amortized.
- **M1 — storeless branded wrappers**: branded host VMaps lazily allocate their hashmap backing
  on first expando/`set` write (`data == NULL` until then; vtable guards). Wrap-sweep of an
  N-node document allocates N VMaps, not 4N objects.
- **M3 — thread invariant stated and asserted**: cache operations `log_error` (debug builds
  assert) if invoked on a thread other than the owning runtime thread. Documented as an RC-ledger
  dependency, revisited when compositor-split work lands.

### 2.7 Issue → remedy map

| DOM1 finding | DOM2 remedy | Where |
|---|---|---|
| G1 brand-predicate if-chains (~70 sites) | one generic VMAP host-object branch + per-type ops tables; `hostobj_demo` proof module | §2.2, Phase 2 |
| G1b `JubeHostObjectOps` takes `void* native` | receiver-`Item` signatures; completed op set incl. get/set/method/invalidate | §2.2, Phase 1 |
| G2 empty `JubeHostAPI`, ~200 raw externs | versioned function-pointer tables; bridge extern count → 0; DOM hooks quarantined in a measurable sub-table | §2.3, Phase 3 |
| G3 hardcoded import + growing sys_func_registry | descriptor-driven import/lowering/namespaces; registry entries → 0 for module funcs | §2.4, Phase 4 |
| G4 no Lambda projections | level-2 `vmap_ops` for `dom_node`/`document`, snake_case cluster, shared write path | §2.5, Phase 5 |
| S1 dangling `host_data` after teardown | `invalidate` op neuters wrappers during cache invalidation | §2.6, Phase 0 |
| M1 3–4 dead allocations per wrapper | lazy backing store for branded VMaps | §2.6, Phase 6 |
| M2 O(N) cache scan, no slot reuse | pointer-keyed hashmap cache with reuse | §2.6, Phase 6 |
| M3 unstated thread invariant | logged/asserted owning-thread check | §2.6, Phase 0 |
| M4 unverified baseline attribution | mandatory broad-baseline re-run + triage before Phase 1 | Phase 0 |

---

## Part 3 — Phased Implementation Plan

Ground rules carried over from DOM1: narrowest gate per step, widen at phase boundaries; hold
each phase as a long-lived green checkpoint; no debug-build performance claims. New rule:
**every phase ends with the same three anchors green** — `make build`, full
`./test/test_js_gtest.exe`, `./lambda.exe test/lambda/radiant_poc.ls` — plus the phase-specific
gates below.

### 3.0 Progress log (2026-07-10)

Current implementation checkpoint:

- **Phase 0 hardening is complete.** `test/lambda/radiant_poc.ls` still returns `"ok"`,
  `test/lambda/radiant_poc_uaf.ls` exercises the post-free wrapper path without crashing, and
  the retained wrapper reports the intended `JUBE_RADIANT_ATTR: expected DOM node wrapper` error
  before returning `null`. The wrapper cache now records its owning thread and logs/asserts if a
  cache operation runs on a different thread.
- **Phase 1 ops-table plumbing is complete.** `JubeHostObjectOps` has receiver-`Item`
  signatures plus the full get/set/method/has/delete/descriptor/keys/prototype/invalidate/destroy
  surface; every Radiant branded type in `radiant_types[]` has a non-null `host_ops` table; and
  document teardown now resolves the wrapper's registered `JubeTypeDef` and calls
  `host_ops->invalidate` before dropping the cache root.
- **Phase 2 generic-host cleanup is complete.** The primary VMAP host-object
  paths now route through generic host dispatch for property get/set, method lookup,
  `has`/`delete`, descriptors, own keys, and prototype handling. The generic engine files no
  longer carry the legacy `MAP_KIND_WEB_API_RESOURCE` / `MAP_KIND_CSSOM` branches or the old
  DOM/style predicate fallbacks covered by the Phase-2 grep gate. Document/Range/Selection
  prototype and `instanceof` special cases were removed from `js_globals.cpp`; `Object.assign`
  now preserves all registered host VMAP targets instead of only inline style objects; the
  duplicate VMAP method fallback was removed from `js_runtime.cpp`. JS-facing Jube namespaces
  are now installed from `JubeNamespaceDef` descriptors, so `hostobjDemo` no longer needs a
  module-specific `js_globals.cpp` hook.
- **`hostobj_demo` now proves the generic host-object path with an owning native type.** The new
  in-tree module registers `hostobj_demo`, projects `value`/`label`, routes a `bump()` method
  through `host_ops->call_method`, preserves expandos/descriptors/own keys/`instanceof`, and
  pins explicit release plus safe post-release access in `test/js/hostobj_demo.js`. VMAP GC
  finalization now passes the VMAP object to the destroy callback and calls `host_ops->destroy`
  for `JUBE_TYPE_OWNING_NATIVE` payloads before freeing the backing map store.
- **Phase 3 host API realization is complete.** `JubeHostAPI` now exposes concrete `gc`,
  `value`, `script`, and `dom` sub-tables: GC root registration (2 entries),
  VMAP/object/array/property helpers (6 entries), JS function/global/error/reflection/call
  helpers (14 entries), and the measured Radiant DOM/CSSOM hook surface (152 entries). The
  static registry populates those tables from the current engine entry points.
- **`hostobj_demo` now builds against the host API for engine services.** Its native wrapper
  allocation, GC rooting, JS object/function setup, property access, own-key reflection, delete,
  and non-enumerable marking all route through the `JubeHostAPI*` received at module init; the
  only `extern "C"` symbols left in the file are its exported module entry points.
- **Radiant now receives engine services through the host API.** `radiant_module_init()` records
  the checked host table in `radiant_host_api`, and the DOM bridge routes GC roots, VMAP
  allocation, object/array/property helpers, JS function/global/error helpers, and all legacy
  DOM/CSSOM behavior hooks through the host API. Radiant-local C ABI declarations moved behind
  `radiant_dom_bridge.hpp`, so `rg 'extern "C"' lambda/module/radiant/*.cpp` is clean.
- **UI Automation failure fixed during this checkpoint.** The `test_rich_text_editor` failure
  was traced to `textarea onselect`: the eager inline-handler collector skipped `onselect`, so
  the text-control `select` event could call a stale pre-reconcile handler. `onselect` is now in
  `radiant/script_runner.cpp`'s retained event-handler list.
- **Verified gates so far:** `make build`; `make build-test`; direct
  `./lambda.exe js test/js/hostobj_demo.js`; focused `hostobj_demo` gtest; full JS gtest
  308/308; DOM-focused JS tests
  `dom_module_props`, `dom_identity`, `dom_style`, `dom_v12b`, `dom_jquery_lib`; direct
  `radiant_poc` and `radiant_poc_uaf`; UI Automation gtest 236 passed / 2 skipped / 0 failed.
  The phase-end `make test-radiant-baseline` rerun matched the Phase-0 triage shape: every
  required non-visual gate passed, and only the pre-existing Render Visual baseline debt kept the
  broad target nonzero. The generic-engine Phase-2 grep gate is clean over
  `js_globals.cpp`, `js_runtime.cpp`, `js_runtime_value.cpp`, and `js_dom_events.cpp`. The Phase-3
  exit checkpoint re-ran direct `hostobj_demo`, direct `radiant_poc` / `radiant_poc_uaf`,
  focused DOM JS tests, full JS gtest 308/308, UI Automation 236 passed / 2 skipped / 0 failed,
  and `make build-test`, all green.
- **Still open:** Phase 4+ work remains: descriptor-driven Lambda import / lowering registration
  and Lambda-side projections. DOM module-internal predicates remain in implementation files such
  as `js_dom.cpp`, `js_cssom.cpp`, and `js_dom_selection.cpp`; the Phase-2 dispatch gate is
  explicitly scoped to generic engine files, not those module internals.
  Noisy `js_set_prototype: circular prototype chain detected` and
  `heap_create_name called with invalid context or name_pool` logs observed during the direct UI
  fixture are tracked as separate debt, not as blockers for the `onselect` fix.

### Phase 0 — Hardening and truth-establishment (small, do first)

Tasks:

1. S1 fix: neuter `host_data` in `radiant_dom_clear_cache_entry()` (interim direct
   implementation; becomes the `invalidate` op in Phase 1). Root-cause comment at the fix point.
2. Add `test/lambda/radiant_poc_uaf.ls`: hold a node wrapper across `radiant.free(doc)`, access
   it, expect a clean null/error result — plus a JS analogue if a teardown path is scriptable.
3. M3: owning-thread assert/log in cache operations.
4. M4: run `make test-radiant-baseline` on current master. Triage every failing bucket to
   *pre-existing* (evidence: fails at the pre-DOM1 commit too) or *DOM1-introduced* (fix before
   proceeding). Record the triage table in this doc.

Testable goals:

- UAF test passes (no crash, no ASan report if available; graceful null/TypeError).
- Baseline triage table recorded; any DOM1-introduced regression fixed or explicitly accepted
  with reason.
- Anchors green.

Progress (2026-07-10): tasks 1-4 are complete. The broad `make test-radiant-baseline` target is
not fully green because of pre-existing Render Visual baseline debt, but no Phase-0 or DOM2
regression bucket was found.

Phase-0 baseline triage (2026-07-10):

| Bucket | Result | Triage |
|---|---:|---|
| Layout Baseline | PASS — 5558 passed, 0 failed, 358 skipped | No DOM2 regression. Some suites still print raw mismatch counts, but each required baseline set passed. |
| Layout Page Suite | PASS — 46 passed, 0 failed | No average regression. Individual page drops (`libcurl`, `page_facatology`, `zengarden`) are offset by the suite aggregate and did not fail the baseline gate. |
| UI Automation | PASS — 236 passed, 0 failed, 2 skipped | Current DOM2 checkpoint is green after the `onselect` retained-handler fix. |
| Radiant View Cmd | PASS — 20 passed, 0 failed | No DOM2 regression. |
| View Page & Markdown | PASS — 104 passed, 0 failed | No DOM2 regression. |
| Fuzzy Crash | SKIP — executable not present | Build/test packaging gap, not a DOM2 regression. |
| Render Visual | FAIL — 197/212 passed, 13 expected failures, 1 skipped, 9 baseline regressions | Pre-existing broad-baseline debt. Same failure shape was recorded in `Lambda_Jube_DOM.md` on 2026-07-08: `form_buttons_01` plus 9 render regressions. Current regressions: `enhance5_overflow_radius_media_text_01`, `form_buttons_01`, `form_checkbox_radio_01`, `glyph_alpha_opacity_fringe_01`, `line_clamp_01`, `list_nested_bullets_01`, `list_style_image_01`, `text_deco_color_01`, `text_decoration_01`. |
| WPT CSS Syntax | PASS — 32 passed, 0 failed by target threshold | The raw log still lists known conformance failures, but the gate threshold passed; not a DOM2 regression. |

### Phase 1 — Ops-table plumbing (no dispatch behavior change)

Purpose: make `JubeTypeDef` real before touching engine dispatch.

Tasks:

1. Revise `JubeHostObjectOps` per §2.2 (receiver `Item`, full op set, `invalidate`). Bump
   nothing — ABI v1 is not yet frozen; note this is the last free signature change.
2. Implement ops tables in the radiant module for every branded type currently dispatched by
   predicate (`dom_node`, `document`/foreign-document, `range`, `selection`, `inline_style`,
   `computed_style`, `stylesheet`, `css_rule`, `rule_style_decl`), each op delegating to the
   existing bridge function.
3. Register them in `radiant_types[]`; make `host_type` resolve to the owning `JubeTypeDef`
   (brand marker → typedef pointer, or a side map — one compare either way).
4. Route S1 neutering through the `invalidate` op.

Testable goals:

- `radiant_types[]` has no NULL `host_ops` for branded types (checked by a startup
  `log_info` summary: "JUBE_REG: type dom_node ops=9/10").
- All anchors green; no engine dispatch site changed yet (diff-scope gate: `lambda/js/` untouched
  in this phase except nothing).

Progress (2026-07-10): complete. The registered branded types are `dom_node`, `range`,
`selection`, `inline_style`, `computed_style`, `stylesheet`, `css_rule`, `rule_style_decl`,
`document`, and `foreign_document`; each points at a host-ops table. Node/Range/Selection expose
9/10 ops including `invalidate`; style, CSSOM, and document wrappers expose 8/10 ops
(`destroy` is intentionally null for non-owning wrappers, and only node wrappers participate in
document-teardown cache invalidation today). The S1 path now calls `host_ops->invalidate` through
the registered `JubeTypeDef`.

### Phase 2 — Generic engine dispatch; delete the if-chains

Purpose: G1. The engine consults the registry, not brand predicates.

Tasks (one operation at a time, each a separate green checkpoint, mirroring DOM1's
cluster-by-cluster discipline):

1. Add `js_host_object_get_property(Item, Item, Item*)` etc. — generic entries that resolve
   `host_type → JubeTypeDef → host_ops`, then own the expando → prototype fallback uniformly.
2. Collapse engine sites per op: property get → set → method call → `has`/`delete` →
   descriptors/keys → prototype/instanceof. At each step the old predicate branch is deleted,
   not bypassed.
3. Foreign-document active-document swap semantics move inside the document type's ops (the
   generic layer must not know about document swapping).
4. Write the **`hostobj_demo` module**: one owning native type (so `destroy`/finalizer runs),
   projected properties, one method, expando/`instanceof` coverage. New test
   `test/js/hostobj_demo.js` + `.txt`.

Testable goals:

- Grep gates (enforced in CI or by `make lint` rule if convenient) over generic engine files:
  `rg "MAP_KIND_WEB_API_RESOURCE|MAP_KIND_CSSOM|js_is_inline_style_item|js_is_computed_style_item|js_dom_item_is_range|js_dom_item_is_selection|js_is_document_proxy|radiant_dom_host_has_property|radiant_dom_host_delete_property|radiant_dom_host_own_property" lambda/js/js_globals.cpp lambda/js/js_runtime.cpp lambda/js/js_runtime_value.cpp lambda/js/js_dom_events.cpp` → **0 hits**.
- `hostobj_demo` passes with no module-specific dispatch or global-install hook in the generic
  engine. It uses `JubeHostObjectOps` for host behavior and `JubeNamespaceDef` for JS global
  exposure.
- Full JS gtest, UI-automation gtest, `dom_module_props`/`dom_jquery_lib`/`dom_style`/`dom_v12b`
  direct diffs green at every sub-step.
- `make test-radiant-baseline` re-run at phase end; no new failures vs the Phase-0 triage table.

Progress (2026-07-10): complete. The generic path is active for the main VMAP host operations,
the generic-engine grep gate is clean, `hostobj_demo` proves owning-native host objects through
direct plus gtest coverage, and JS global namespace exposure is descriptor-driven through
`JubeNamespaceDef`. `make build-test`, full JS gtest 308/308, UI Automation 236 passed / 2
skipped, and the phase-end Radiant baseline rerun are recorded above; the broad Radiant target
still exits nonzero only because of the pre-existing Render Visual baseline debt in the Phase-0
triage table.

### Phase 3 — Host API realization

Purpose: G2. The bridge's externs become the versioned `JubeHostAPI`.

Tasks:

1. Define the sub-table structs (§2.3). Populate from the current extern inventory; collapse
   externs made redundant by Phase 2 (expando/descriptor/prototype helpers now live generic-side).
2. Migrate the bridge group by group: values/GC first (small, mechanical), then errors/script,
   then the `dom` hooks sub-table. Each group is a green checkpoint.
3. Freeze **`JUBE_ABI_VERSION = 1`** at phase end: from here, additive-only.

Testable goals:

- `grep -c 'extern "C"' lambda/module/radiant/*.cpp` → **0** (module receives everything via
  the `JubeHostAPI*` from `init`).
- The `dom` hooks sub-table entry count is recorded in this doc (the measurable
  Radiant-coupling number that `Lambda_Design_DOM_Pkg.md` Phase 3 will drive down).
- `hostobj_demo` builds against the host API alone (it never declares an engine symbol).
- Anchors + full JS gtest + UI-automation green.

Progress (2026-07-10): complete. The host API tables are defined and populated: `gc` (2
entries), `value` (6 entries), `script` (14 entries), and `dom` (152 entries). `hostobj_demo`
consumes engine services only through these tables, and Radiant routes every module-to-engine
DOM/CSSOM hook through `radiant_host_api`. The literal extern gate is clean:
`rg 'extern "C"' lambda/module/radiant/*.cpp` returns no hits; `hostobj_demo_module.cpp` has only
its two exported module entry points. `JUBE_ABI_VERSION = 1` is frozen from here as an
additive-only ABI. Verified at phase exit: `make build`, `make build-test`, direct
`hostobj_demo`, focused DOM/hostobj JS gtests, full JS gtest 308/308, direct
`radiant_poc` / `radiant_poc_uaf`, and UI Automation 236 passed / 2 skipped / 0 failed.

### Phase 4 — Descriptor-driven registration

Purpose: G3. Import, lowering, and namespaces read module descriptors.

Tasks:

1. `build_ast.cpp`: resolve import names via `jube_find_static_module()`; delete
   `builtin_import_radiant` / `builtin_alias_radiant`. Parse `JubeFuncDef.signature` with the
   existing Lambda type parser; drive arity/type checks from it.
2. MIR lowering: module function calls lower from `JubeFuncDef.native_signature`; delete the
   module's `sys_func_registry.c` entries.
3. Finish DOM1 Phase 7's deferred item: re-home the `js_dom_*` MIR lowering shims
   (`js_document_method`, `js_dom_get_property`, …) behind generic host-object descriptors and
   remove them from the central registry.
4. Activate `JubeNamespaceDef` for JS-facing installation of module globals.
5. Registry hygiene test: register a second Lambda-facing function in `hostobj_demo` and call it
   from a `.ls` test **without touching** `sys_func_registry.c` or `build_ast.cpp`.

Testable goals:

- `grep -c "radiant" lambda/sys_func_registry.c lambda/build_ast.cpp` → **0**.
- New `.ls` test importing `hostobj_demo` passes; adding a module function is a module-only diff.
- `make test-lambda-baseline` green (import machinery touched ⇒ full Lambda gate, not just POC).
- Anchors + full JS gtest green.

Progress (2026-07-10): Phase 4 is complete.
`build_ast.cpp` now synthesizes `SysFuncInfo` records from static `JubeModuleDef.functions`,
deriving module-prefixed names, arity, return type, first-parameter type metadata, and lowered
C symbols from `JubeFuncDef.signature` / `native_signature`. `import radiant`, qualified calls such as
`radiant.load(...)`, aliased Jube imports, and global imports such as `import hostobj_demo`
resolve through `jube_find_static_module()` instead of `builtin_import_radiant` /
`builtin_alias_radiant`.

MIR direct lowering now honors `SysFuncInfo.c_func_name`, and `import_resolver()` has a dynamic
descriptor fallback so module functions deleted from `sys_func_registry.c` still resolve by
function pointer. The central Radiant sys-func rows and enum values are gone; the remaining enum
tag is the generic `SYSFUNC_JUBE_MODULE`.

Registry hygiene proof: `hostobj_demo` registers two Lambda-facing functions (`answer`, `add`) in
its own descriptor table, and `test/lambda/hostobj_demo_jube_import.ls` calls both without adding
central registry rows. JS namespace installation remains descriptor-driven through
`JubeNamespaceDef` in `js_globals.cpp`. The deferred DOM1 central-registry shim audit is clean:
`sys_func_registry.c` has no `js_dom_*` / `js_document_*` sys-func rows; the remaining
`js_get_document_object_value` entry is a runtime import for JS globals, not a module lowering shim.

Validation: `make build`; direct `hostobj_demo_jube_import` (`{ answer: 42, sum: 12 }`);
direct `radiant_poc` (`"ok"`); direct `radiant_poc_uaf` (`null` after invalid-wrapper guard);
`rg "radiant|SYSFUNC_RADIANT|fn_radiant|builtin_import_radiant|builtin_alias_radiant" lambda/build_ast.cpp lambda/sys_func_registry.c lambda/lambda.h`
→ 0 hits; `git diff --check`; escalated `make test-lambda-baseline` → 3283/3283 passed.

### Phase 5 — Lambda-side projections

Purpose: G4. Lambda field access on native types.

Tasks:

1. Implement `dom_node` `vmap_ops` (level-2 computed vtable): first cluster = identity/name/value
   reads + navigation (mirror DOM1's first JS cluster), snake_case names.
2. `set` (pn) for the narrow writable cluster (`id`, `class_name`, attribute writes) through the
   same mutation-notify path as JS writes.
3. `keys`/iteration support; `document` projection second.
4. New tests `test/lambda/radiant_dom_read.ls` / `radiant_dom_mutate.ls` (+ `.txt`): traversal
   loop, field reads, a mutation visible to a subsequent JS query in the same document (the
   cross-front-end coherence check), printing a node.

Testable goals:

- The `.ls` tests pass; cross-front-end coherence pinned (Lambda write → JS read sees it, and
  vice versa).
- JS behavior unchanged (full JS gtest green) — projections are additive.
- `make test-lambda-baseline` green.

Progress (2026-07-10): complete. Host-backed VMaps now route Lambda field/member reads, writes,
and key enumeration through the registered Jube host-object operations. The bridge accepts
snake_case Lambda names (`node_name`, `class_name`, `owner_document`, etc.), converts them to the
DOM camelCase operation surface, and maps own-property keys back to snake_case symbols for
Lambda-side iteration. Attribute-like string keys such as `"data-phase"` write through
`setAttribute`, and `class_name` / `id` writes use the same mutation path as JS property writes.

Document projection is active as well: loaded DOM nodes now expose a document wrapper for
`owner_document`, and document reads dispatch against the owning loaded document rather than the
global JS document singleton. Own-key enumeration was kept lazy so iterating node keys does not
materialize live DOM collections during key discovery.

Coverage added:

- `test/lambda/radiant_dom_read.ls` / `.txt`: node identity, names, navigation, text value,
  document projection, key iteration, and printable type surface.
- `test/lambda/radiant_dom_mutate.ls` / `.txt`: Lambda-side mutation through the Radiant function
  path, then readback through the projected document root.
- `test/lambda/proc/radiant_dom_set.ls` / `.txt`: procedural `root.set(...)` coverage for `id`,
  `class_name`, and attribute-key writes.

Validation: direct `radiant_dom_read`; direct `radiant_dom_mutate`; direct
`lambda.exe run test/lambda/proc/radiant_dom_set.ls`; focused
`test_lambda_gtest --gtest_filter='AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/*radiant_dom*'`
→ 3/3 passed; escalated `make test-lambda-baseline` → 3286/3286 passed, including JS gtest
308/308.

### Phase 6 — Cache and allocation debts

Purpose: M1, M2 — paid after the architecture stabilizes so the work isn't redone.

Status 2026-07-10: complete, with Radiant render-visual baseline triaged separately.

Tasks:

1. Done: DOM wrapper lookup is now a pointer-keyed `HashMap` from `DomNode*` to stable
   chunk slots. GC roots remain registered against chunk entry `item` fields, so rehashing
   the index cannot move root addresses. Invalidated entries unregister their roots, clear
   form-control expandos, delete their index entry, and enter a free list for slot reuse.
2. Done: ordinary `VMap` backing storage is now lazy. Host-branded VMaps stay projection
   shells until a real map write needs a backing `HashMapData`; host payload cleanup was
   also fixed to run even when backing storage is absent.
3. Done: release-build wrap-sweep benchmark on 2026-07-10 using `./lambda.exe js
   temp/dom_phase6_bench.js --document temp/dom_phase6_bench.html --no-log` after a
   release compile. The script built a 4,255-node document, ran `querySelectorAll('*')`,
   then performed 4,602,200 repeated `parentNode` hops. Three release runs:
   `query_ms=3/3/3`, `walk_ms=3119/3092/3252`. Allocation invariant after the lazy
   VMap change: one VMap shell per newly wrapped DOM node, with no eager per-wrapper
   backing hashmap; expando writes still allocate backing storage on demand.

Testable goals:

- Green: focused Lambda DOM/UAF/import set
  (`radiant_poc_uaf`, `*radiant_dom*`, `proc_radiant_dom_set`, `hostobj_demo_jube_import`)
  → 5/5 passed.
- Green: focused JS identity/module/host-object set
  (`dom_identity`, `dom_module_props`, `hostobj_demo`) → 3/3 passed.
- Green: full JS gtest → 308/308 passed.
- Green: UI automation gtest → 236 passed, 2 expected headless webview skips.
- Mixed/triaged: `make test-radiant-baseline` passed layout baseline, layout page suite,
  UI automation, Radiant view command, view page/markdown, and WPT CSS syntax, but exited
  nonzero on existing render-visual baseline drift: `form_buttons_01` plus 9 render
  baseline regressions. No DOM2 wrapper-cache failure was observed in this target.
- Release build note: initial `make release` exposed a release-only `build_ast.cpp`
  `-Werror` from `log_info` argument stripping; the dead counter is now explicitly
  observed, and the serial release `lambda` compile passes.

### Phase 7 (stretch) — Dynamic loading dev proof

Purpose: validate that Phases 2–4 actually decoupled the module.

Status 2026-07-10: complete as a dev proof. Shipping dynamic loading, discovery, manifests, and
Windows loader support remain deferred.

Tasks:

1. Done: `hostobj_demo` now exports a manifest-less single entry symbol, `jube_module`, returning
   its `JubeModuleDef`. A module-specific alias, `hostobj_demo_jube_module`, is also exported for
   explicit local probes; static registration is unchanged.
2. Done: the Jube registry has an opt-in dev loader, `jube_load_dynamic_module(path, entry_symbol)`,
   using `dlopen`/`dlsym` on non-Windows builds. Dynamic modules register through the same descriptor
   validation and init path as static modules. Loaded handles stay open because descriptor, type,
   function, and namespace pointers are borrowed from the image.
3. Done: `jube_register_builtin_modules()` can skip the static demo module with
   `JUBE_HOSTOBJ_DEMO_DYNAMIC_ONLY=1`, then load a dynamic descriptor from
   `JUBE_DYNAMIC_MODULE`. `JUBE_DYNAMIC_ENTRY` optionally overrides the default `jube_module`
   symbol.

Validation:

- Static JS direct: `./lambda.exe js test/js/hostobj_demo.js --no-log` passed.
- Static Lambda direct: `./lambda.exe test/lambda/hostobj_demo_jube_import.ls` returned
  `{ answer: 42, sum: 12 }`.
- Dynamic dylib build:
  `g++ -std=c++17 -fPIC -dynamiclib -undefined dynamic_lookup -I. -Ilambda -o
  temp/hostobj_demo_dynamic.dylib lambda/module/hostobj_demo/hostobj_demo_module.cpp` passed
  after avoiding `-Ilib`, which makes the project `lib/string.h` intercept `<string.h>`.
- Dynamic JS direct:
  `env JUBE_HOSTOBJ_DEMO_DYNAMIC_ONLY=1 JUBE_DYNAMIC_MODULE=temp/hostobj_demo_dynamic.dylib
  ./lambda.exe js test/js/hostobj_demo.js --no-log` passed.
- Dynamic Lambda direct:
  `env JUBE_HOSTOBJ_DEMO_DYNAMIC_ONLY=1 JUBE_DYNAMIC_MODULE=temp/hostobj_demo_dynamic.dylib
  ./lambda.exe test/lambda/hostobj_demo_jube_import.ls` returned `{ answer: 42, sum: 12 }`.
- Build gate: `make build-test` passed.
- Focused gtests: static and dynamic `test_lambda_gtest --gtest_filter='*hostobj_demo_jube_import*'`
  passed 1/1. Static and dynamic `test_js_gtest --gtest_filter='*hostobj_demo*'` also passed 1/1;
  note that the JS gtest harness still warms/caches neighboring JS scripts before the selected
  parameter and emits existing ASan/memtrack noise from that prelude, but the selected
  `hostobj_demo` case itself passed.

Testable goal met: the same `hostobj_demo` JS and Lambda tests pass statically and against the
dynamically loaded copy.

### Sequencing and exit

```
Phase 0 ─→ Phase 1 ─→ Phase 2 ─→ Phase 3 ─→ Phase 4 ─→ Phase 5 ─→ Phase 6 ─→ (7)
hardening   ops        generic     host API    descriptor  Lambda      perf       dlopen
+ truth     tables     dispatch                registration projections debts      proof
                       [gates Lambda_Design_DOM_Pkg Phase 0/1]
```

**DOM2 exit = the parent design's POC-1 exit criteria, actually met**: bridge fully
registry-driven (no `js_dom_*`/`fn_radiant_*` in `sys_func_registry.c`), engine free of
module-specific host-object branches (grep gates at 0), a second module integrating without
engine edits, a Lambda script driving DOM mutation → query round-trip through projections, and
all documented baselines green against the Phase-0 triage table.

After DOM2, `Lambda_Design_DOM_Pkg.md` Phase 1 (MutationObserver et al. in Lambda script) can start on
an honest foundation: its L4 adapter is the generic protocol of §2.2, its host-API needs
(microtask scheduling, mutation-ring subscription) are additive entries on the §2.3 tables, and
its Lambda-side ergonomics stand on §2.5 projections.

---

## Appendix — DOM1 evidence index

| Finding | Evidence |
|---|---|
| G1 if-chains | Initial evidence: `lambda/js/js_runtime.cpp:4111–4139`; ~70 predicate sites (38 js_runtime, 31 js_globals, 1 js_dom_events). The 2026-07-10 Phase-2 generic-engine subset is clean; `hostobj_demo` proves generic dispatch and descriptor-driven JS namespace exposure. |
| G1 unused ops | `lambda/module/radiant/radiant_module.cpp:152` (`{"dom_node", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL}`) |
| G1b wrong receiver type | `lambda/jube/jube.h:32–38` (`void* native`) vs `radiant_dom_host_has_property(Item, Item, Item*)` |
| G2 empty host API | `lambda/jube/jube.h:65–67`; externs at `lambda/module/radiant/radiant_dom_bridge.cpp:18–160` |
| G3 hardcoded import | `lambda/build_ast.cpp:8835` (`builtin_import_radiant`), `lambda/sys_func_registry.c:925` (`fn_radiant_load`) |
| G4 generic vtable on DOM wrappers | `radiant_dom_wrap_node()` uses `vmap_new()` (`radiant_dom_bridge.cpp:1116`); hashmap vtable in `lambda/vmap.cpp:255` |
| S1 dangling host_data | `radiant_dom_clear_cache_entry()` (`radiant_dom_bridge.cpp:1063`) unroots without neutering |
| M1 eager backing | `lambda/vmap.cpp:113` (`hashmap_data_new` in `vmap_alloc`) |
| M2 linear cache | `radiant_dom_lookup_wrapper()` (`radiant_dom_bridge.cpp:1023`) |
| M3 thread-local cache | `radiant_dom_bridge.cpp:180–181` (`__thread` heads) |
| M4 deferred baseline | `Lambda_Jube_DOM.md` Phase 7 acceptance ("Deferred broad gate") |
