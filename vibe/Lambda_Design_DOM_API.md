# Lambda DOM API — one native DOM layer under two realms

> **Status**: **RATIFIED — implementation in progress** (2026-09-01). Rulings **ES32–ES38 ratified in full (2026-09-01, user)**; migration stages **F22–F27 in progress** (§5 carries per-stage checkpoints). Scope direction set by user 2026-09-01 (§1.3); **ES34**: the `JubeHostDomAPI` seam is **retained** — it is the dynamic DOM ABI (AST-interpreter dispatch; dynamically-loaded Jube modules such as Python call DOM through it without static linking). **Phase-2 target** (§7): unify `radiant_module`'s DOM surface and `JubeHostDomAPI` into **one new DOM API** — dedup the API functions *and* the implementations behind them; not this pass. §2 is descriptive, verified against the tree at 2026-09-01 (post-`b9aaeca81`, F21 landed).
> **Role**: the design home for the **native DOM layer's code architecture** — where the DOM/web-platform host algorithms live (`lambda/dom/`), the realm-neutral core vs JS-adapter split, how the Jube `radiant` module and the JS runtime link to that core, and the Lambda-facing `import dom` API. Complements the behavior/dispatch docs, which own *what* the DOM layer does; this doc owns *where it lives and who may call it, how*.
> **Scope**: file/module layout, symbol namespaces, the host-table linkage question, the core/adapter boundary, and the Lambda `dom` module surface. **Out of scope**: event dispatch semantics (`Lambda_Design_DOM_Dispatch.md`), default-action placement (`Lambda_Design_DOM_Default.md`), state storage and waist semantics (`Lambda_Design_DOM_State.md`), the L3 Obscura-parity package (`Lambda_Design_DOM_Pkg.md` Phase 1+), and the DOM data structures themselves (deferred, §6).
> **Companion docs**: `vibe/Lambda_Design_Native_Module.md` §8 (POC 1 `radiant-dom` — this proposal executes its carve-out half; the host DOM API redesign is Phase 2, §7), `vibe/Lambda_Design_DOM_Pkg.md` (the four-layer stack L1–L4 and Phase-0 gate this delivers), `vibe/Lambda_Jube_DOM3.md`/`DOM4.md` (declared interfaces, ordinal dispatch), `vibe/radiant/Radiant_Design_Dom_View_Struct.md` (OQ5 — the struct move this doc defers), `doc/dev/js/JS_13_Web_DOM.md`.
> **Formal anchors**: D8 (module/runtime ownership), D3.4.7/D7.4.1–D7.4.4 (host-object metadata and the single VMap/Jube bridge), D6.2.2v2 (observable property Get + `[[Call]]`), D4.5.1v3 (the Radiant memory seam), S9.2.2 (read views are snapshots), S9.1.4 (state lives in view state), S12.1.3 (mutation only in handlers), S5.1.4 (no reference identity in Lambda).
> **Ledger series**: extends the DOM area's `ES#`/`ESO#`/`F#` per `doc/Doc_Convention.md` §4 — no new series. Decisions **ES32–ES38**; open issues **ESO72–ESO78**; migration stages **F22–F27**. (Prior: ES31/ESO71 in `Lambda_Design_DOM_Default.md`, F21 in `Lambda_Design_DOM_Dispatch.md`.)

---

## 1. What this document is

### 1.1 The problem: one mechanism behind three doors, wearing the wrong name

The web-platform host code — ~28.3k lines across 20 files — lives in `lambda/js/` under `js_*` names, yet after F17–F20 it is no longer JS-specific in any honest sense:

- The **event record** is one native record both realms read (ES24); the **author cascade** in `js_dom_events.cpp` dispatches JS listeners *and* Lambda author templates in one walk (ES23/F18).
- The **behavior package** (`lambda/package/dom/*.ls`) is the UA tier over the same mechanism (ES22), reaching it through `radiant.*` waist primitives.
- The **wrapper vocabulary** is shared: `~` in a behavior handler and `el` in page JS are the same branded `dom_node` VMap (D7.4.1–D7.4.4).

But the *code architecture* still says otherwise. The same `js_dom.cpp` bodies are reached through three different doors:

1. **JS + declared-interface dispatch**: property kernel → Jube member bindings → ordinals → `radiant_dom_bridge.cpp` → **a ~216-slot `JubeHostDomAPI` function table** → `js_dom_*` bodies.
2. **Radiant engine code**: 77 *direct* `js_dom_*` calls in `radiant/event.cpp` alone, plus ad-hoc `extern "C"` declarations scattered across ~15 files.
3. **Lambda waist**: `radiant.*` module functions, some delegating to the same ordinals, some carrying private near-duplicates.

Door 1 is the Jube architecture working as designed — the module reaches the runtime's DOM core only through the host API, so the two sides stay decoupled (D8). Doors 2 and 3 are the problem children: host-side code reaching the same bodies through scattered ad-hoc externs and private near-duplicates. And door 1, while architecturally right, has the wrong *grain*: every new DOM capability is declared up to five times (impl, header, host-table slot, table wiring, bridge `#define`) because the table exposes ~216 meticulous per-function slots instead of a small reviewable API (§3.2, Phase 2).

### 1.2 What this proposal does

Executes the direction the area ADRs already point at — **POC 1 "radiant-dom"** (`Lambda_Design_Native_Module.md` §8: carve the DOM bridge out of the monolith) and **DOM_Pkg Phase 0** (the hard gate: "no package code against the `js_dom.cpp` monolith") — as a concrete code carve-out:

1. **Relocate** the web-platform files from `lambda/js/` to a new **`lambda/dom/`**, renamed `js_dom*` → `dom_*` (ES32).
2. **Split** the JS semantic adapter (shape) from the realm-neutral core (mechanism) — DOM_Pkg §3.2's L4/L2 line drawn in code, not just in prose (ES33).
3. **Clean the linkage without breaking the seam**: the `JubeHostDomAPI` stays as the Jube boundary between the radiant module and the runtime's DOM core (ES34 — user direction); the *host-side* ad-hoc extern sprawl is replaced by ordinary headers (ES35/F23); the table's shape redesign is deferred to Phase 2 (§7).
4. **Open the Lambda face**: an `import dom` Jube module exposing the DOM API to Lambda scripts over the same core the JS surface uses (ES36) — the missing half of "Radiant as a headless browser" (DOM_Pkg goal 2).

### 1.3 Scope direction (user, 2026-09-01)

Four scope decisions were put to the user and answered; the rulings below implement them:

| Question | Direction |
|---|---|
| Which files move? | **All web-platform files** — `js_dom*` + `js_cssom` + `js_canvas`/`js_xhr`/`js_fetch`/`js_formdata`/`js_clipboard`/`js_history` |
| JS-shape residue? | **Split now** — extract the JS adapter in this effort, not a follow-up |
| Lambda-facing deliverable? | **Full `import dom` API slice** (not just internal unification) |
| DOM data structs (`dom_node.hpp` et al.)? | **Stay in `lambda/input/css/` for now** |

---

## 2. Current state (verified 2026-09-01)

### 2.1 File census

| Family | Files (`lambda/js/`) | Lines | JS-runtime call sites | Genuinely-JS markers |
|---|---|---|---|---|
| dom | `js_dom.{cpp,h}` | 16,501 + 361 | ~790 (property-kernel helpers 444, object/array construction 150, functions 56, coercion 44, throws 12, this/global 35, runtime-state 49) | **zero** `JS_CLASS_*` |
| events | `js_dom_events.{cpp,h}` | 2,297 + 211 | ~185 | the area's only stamp cluster: ~25 `JS_CLASS_EVENT*`/ctor sites |
| observers | `js_dom_observers.{cpp,h}` | 832 + 25 | ~101 | `js_microtask_enqueue` ×2 |
| platform | `js_dom_platform.{cpp,h}` | 303 + 19 | ~62 | none (zero throws) |
| selection | `js_dom_selection.{cpp,h}` | 1,411 + 60 | ~38 | `js_setTimeout` ×2 |
| cssom | `js_cssom.{cpp,h}` | 1,107 + 179 | ~19 | `js_object_meta`, `JS_CLASS_CSS_*` |
| **core subtotal** | 12 files | **23,306** | | |
| canvas | `js_canvas.cpp` | 422 | | JS ctor publication |
| xhr | `js_xhr.{cpp,h}` | 816 + 37 | | JS object shape |
| fetch | `js_fetch.cpp` | 658 | | Promises throughout |
| formdata | `js_formdata.cpp` | 968 | | JS class shape |
| clipboard | `js_clipboard.cpp` | 1,826 | | Promises, ClipboardItem |
| history | `js_history.{cpp,h}` | 246 + 16 | | small |
| **total** | **20 files** | **28,295** | | |

Two structural facts worth their weight:

- **Every in-scope header is already realm-neutral**: each includes only `../lambda.h` and speaks `extern "C"` over `Item`/`void*`. The JS coupling is entirely in the `.cpp` bodies.
- **The heavy coupling is value-plumbing, not JS semantics.** Of `js_dom.cpp`'s ~790 JS-runtime call sites, ~600 are map/array/string Item construction and keyed access (`js_name_item`, `js_set_key_cstr`, `js_new_object`, `js_array_new`, …). Under ES12 (one runtime per document) these helpers build plain map/array Items that both realms read — they are the shared runtime's value API wearing a `js_` prefix, not JS semantics (ESO75 tracks the honest renaming). The *genuinely* JS-only surface is small and localized: ~25 class-stamp sites in events, `js_object_meta`/CSS-namespace tagging in cssom, realm-global publication (`js_get_global_this` ×35 area-wide), and 5 scheduling calls.

### 2.2 The four access paths

**(a) JS / declared-interface path.** Property kernel → `radiant_dom_type_bindings[]` member records (`radiant_dom_iface.cpp:1609`) → ordinal handlers (`RADIANT_DOM_OPERATION_BINDING`, `radiant_dom_bridge.cpp:2556`; 83 `JubeDomElementOperation` + 38 `RadiantDocumentOperation` ordinals) → **`#define js_dom_x radiant_host_api->dom->x`** (~125 remap defines, `radiant_dom_bridge.cpp:68+`) → `JubeHostDomAPI` (~216 slots, `jube.h:676–928`; populated `jube_registry.cpp:1507–1731` from ~112 `extern "C"` prototypes at `:997+`) → the `js_dom*.cpp` bodies. Per D6.2.2v2 the member records own the observable Get + `[[Call]]`; the ordinal is the executable capability.

**(b) Radiant engine direct calls.** `radiant/event.cpp` makes 77 `js_dom_*` references (26 distinct symbols) plus 11 ad-hoc externs (`:48–61`); `window.cpp`, `script_runner.cpp`, `layout.cpp`, `event_sim.cpp`, `cmd_layout.cpp` add more; `radiant/event.hpp:48` leaks two externs into every includer. So the "module boundary" of path (a) is already crossed freely by the engine sitting next to it.

**(c) The Lambda waist.** The Jube `radiant` static module (`radiant_module.cpp:2960`) exposes ~110+ functions (`get_state`/`set_state`/`dispatch`/tree reads/`replace_range`/`dom_edit_*`/…) to `import radiant`. Some already delegate to path (a)'s ordinals (`:1205`, `:1211`, `:1514`); others reach `js_dom` through 8 more ad-hoc externs (`:51–60`).

**(d) Weak-link hooks.** `radiant/text_edit.cpp:473`, `text_control.cpp:16`, `dom_range.cpp:645`, `lambda/input/css/dom_lifecycle.cpp:39` declare `__attribute__((weak))` `js_dom_*` notification hooks with empty defaults (+ a stub in `test/test_state_store_stubs.cpp:101`) so radiant-only binaries link without the DOM layer.

Inverted dependency worth naming: `js_event_loop.cpp` calls *into* the DOM layer at 13 sites (`js_dom_get_document`, `js_dom_commit_headless_layout`, …) — the event loop needs DOM context far more than DOM needs the loop (5 outbound scheduling calls total). This cycle is why `lambda/dom/` starts inside the `lambda-rt` link target (ESO72).

### 2.3 Build mapping

All 20 files are swept by the single glob `"lambda/js/*.cpp"` in the **`lambda-rt`** target (`build_lambda_config.json:640`); no file is named individually anywhere in the build, and `sys_func_registry.c` contains **zero** DOM registrations (its only contact is one `#include`). The `radiant` target already compiles `lambda/module/radiant/*.cpp` alongside `radiant/*.cpp` and links against `lambda-rt` symbols — which is why path (b)'s direct calls link today, and why ES34 introduces no new dependency direction.

---

## 3. Analysis

### 3.1 The name is the bug

`js_dom_events.cpp` hosting the *shared* author cascade (F18) and the *one* native event record (F17) under a `js_` name is not cosmetic debt: it steers every future contributor to treat realm-neutral mechanism as JS-private, and it hides the waist. DOM_Default §1.3's placement table says mechanism is native and policy drives it "through the waist" — but the waist's *implementation* is filed under the JS engine. The carve-out makes the architecture legible from the directory listing.

### 3.2 The seam is right; its grain is wrong

The `JubeHostDomAPI` implements P2 of the module design ("modules never import host symbols") for the radiant module — that is the Jube architecture, and it is load-bearing: it is what keeps the JS runtime and radiant code from coupling directly, and what keeps dynamic module loading an honest future option (**user direction 2026-09-01: the table is retained**). The defect is its *granularity and duplication*, not its existence. Granularity: ~216 meticulous per-function slots, ~112 duplicate prototypes, ~125 remap defines, and five-site registration for every new capability — the accreted union of every property/bridge/ordinal need since DOM3, rather than the small reviewable API the waist budget rule demands (DOM_Pkg §4.3: "its size is a design health metric"). Duplication: the tree today carries **two parallel DOM APIs over the same bodies** — the `JubeHostDomAPI` slots and the `radiant.*` fn table (~110 entries, `radiant_module.cpp:2754–2952`) — overlapping in coverage (tree reads, attributes, editing waists) with per-surface wrappers and occasional private near-duplicates in between. The Phase-2 target (user direction 2026-09-01, §7) is to **unify the two into one new DOM API** — one operation catalog, deduplicated at both the API level and the implementation level. Two secondary leaks compound the picture: the module carries a few ad-hoc `extern "C"` bypasses of its own table (`radiant_module.cpp:51–60`), and host-side engine code declares externs freely instead of including headers. This pass fixes the naming and the host-side hygiene (F23/F24) and **freezes** the module-side bypasses for Phase 2 — widening or reshaping the table now would bake the wrong grain in deeper.

### 3.3 What is genuinely JS-shape (and how little of it there is)

DOM_Pkg §3.2 already rules which surface semantics are JS-only by decision (wrapper identity, expandos, accessor properties, prototypes/`instanceof`, descriptors, live collections, `DOMException` subtypes — each foreclosed for Lambda by S5.1.4/S9.1.x/S9.2.2/S11.3.1). The §2.1 census locates them in code: ~25 stamp sites in events, cssom's object-meta tagging, realm-global publication, ctor/init-dict coercion. Everything else — tree ops, attributes, selectors (one engine, shared with layout), geometry reads, innerHTML parse/serialize, mutation notify, listener storage, the cascade, Range/Selection mechanics, Web Storage state — is mechanism both realms need. The split is therefore *small in code moved* and *large in clarity gained*.

### 3.4 The Lambda face is half-built

Lambda already holds `dom_node` VMaps (`~` in handlers), already calls member sets on them (`test/lambda/proc/radiant_dom_set.ls`), and already has ~110 `radiant.*` functions. What it lacks is the *general* DOM API: parse/query/mutate/serialize for scripts that are not behavior templates — the headless/Obscura use case (DOM_Pkg goal 2), today JS-only. The pieces (branded wrappers, ordinals, snapshot navigation per ES30) all exist; ES36 assembles them into a module.

---

## 4. Design & rulings (ratified 2026-09-01)

### ES32 (ratified 2026-09-01) — `lambda/dom/` is the realm-neutral DOM home

All 20 web-platform files move to `lambda/dom/`, renamed by dropping the `js_` prefix:

```
lambda/dom/
  dom.cpp / dom.h                    ← js_dom.{cpp,h}      element/document ops, selectors, geometry,
                                                           innerHTML, mutation notify, forms mechanics
  dom_events.cpp / dom_events.h      ← js_dom_events.*     F17 event record, listener store, shared cascade
  dom_observers.cpp / .h             ← js_dom_observers.*
  dom_platform.cpp / .h              ← js_dom_platform.*   Web Storage, matchMedia state
  dom_selection.cpp / .h             ← js_dom_selection.*  Range/Selection over radiant/dom_range
  dom_cssom.cpp / dom_cssom.h        ← js_cssom.*
  dom_canvas.cpp                     ← js_canvas.cpp       (peripheral — ES37)
  dom_xhr.cpp / dom_xhr.h            ← js_xhr.*            (peripheral)
  dom_fetch.cpp                      ← js_fetch.cpp        (peripheral)
  dom_formdata.cpp                   ← js_formdata.cpp     (peripheral)
  dom_clipboard.cpp                  ← js_clipboard.cpp    (peripheral)
  dom_history.cpp / dom_history.h    ← js_history.*        (peripheral)
  dom_module.cpp                     ← NEW                 the Jube "dom" module (ES36)
```

The stems `dom_node`/`dom_element`/`dom_lifecycle` are **reserved** — they belong to the data structures in `lambda/input/css/`, which stay put (user direction; `Radiant_Design_Dom_View_Struct.md` OQ5 reserves this same directory for their eventual move — coherent end state, deferred, ESO77). Build: the files stay in the `lambda-rt` target (new glob + include dirs); no new link target this pass (ESO72).

### ES33 (ratified 2026-09-01) — the core/adapter split rule

`lambda/dom/` code **may** use the shared runtime's value helpers (map/array/string Item construction and keyed access — the ES12 one-runtime value API) and runtime-state context. It **must not** contain:

- `JS_CLASS_*` stamps or `js_object_meta` tagging;
- prototype/realm-global installation (`js_get_global_this`, `js_install_native_method`, `js_set_prototype`, global `js_object_define_property`);
- JS event-class constructors and init-dictionary coercion;
- direct `js_setTimeout`/`js_microtask_enqueue` (a two-entry scheduling seam — `dom_schedule_task`/`dom_schedule_microtask`, implemented by the adapter — covers all 5 call sites).

What violates the rule moves to thin adapter files in `lambda/js/` (`js_event_adapter.cpp` for the Event classes; `js_dom_adapter.cpp` for prototype/global/meta glue; `js_globals.cpp` stays the publication home). This is DOM_Pkg §3.2's L4 ("the adapter owns the shape") drawn as a compile-time boundary, grep-enforceable.

**Boundary ruling within ES33**: the property-protocol entry points (`dom_get_property`/`dom_set_property`, named/indexed binding handlers) **stay in the core** — they are the realm-shared VMap dispatch of D7.4.4, not JS shape; Lambda member access on `dom_node` VMaps runs through them today. Camel-case spellings remain metadata on the member records (`js_name` in `JubeMemberBind`), never a second dispatch.

**Scope refinement**: the rule is *enforced* for the six core families (dom, events, observers, platform, selection, cssom). The six peripherals are governed by ES37.

### ES34 (user direction 2026-09-01) — the `JubeHostDomAPI` seam is retained; the carve-out happens behind it

The `JubeHostDomAPI` **stays**, for three reasons (the first architectural, the other two consumption-driven — user direction 2026-09-01):

1. **Decoupling** — it is the Jube architecture's seam: the radiant module reaches the runtime's DOM core only through `radiant_host_api->dom`, so the JS runtime and radiant code are never directly coupled (D8, P2 of `Lambda_Design_Native_Module.md`). The seam tracks the link-target boundary — `lambda/module/radiant/` compiles into the `radiant` target, the DOM core into `lambda-rt`.
2. **Interpreter dispatch** — a function-pointer table makes DOM directly callable from the AST interpreter tier: one stable table to indirect through, instead of per-symbol static coupling for every DOM operation an interpreted path touches.
3. **Dynamic module access** — the table is the DOM ABI for Jube modules that are *not* statically linked: a dynamically-loaded module (e.g. the Python module) receives the host API at init and can drive DOM through `host->dom` with no link-time dependency on runtime symbols — the N-API pattern.

This pass does not move or reshape the seam.

What this pass changes *behind* the seam:

- The table's slots repoint to the relocated, renamed implementations as F22/F24 land (`js_dom_*` → `dom_*` in the `jube_registry.cpp` prototypes and initializer — mechanical, the struct layout untouched, no `JUBE_HOST_API_VERSION` bump).
- The bridge's `#define js_dom_x radiant_host_api->dom->x` remaps stay (they *are* proper table usage) and simply rename with F24.
- The `host->dom` requirement in `radiant_module_init` stays.
- The module's own ad-hoc `extern "C"` bypasses of the table (`radiant_module.cpp:51–60` and kin) are **frozen as-is** — neither converted to header includes (which would entrench a coupling the architecture forbids) nor grown into new table slots now (which would extend the wrong grain). They are inventory for the Phase-2 redesign (ESO78).

What is explicitly **deferred to DOM API Phase 2** (§7): the table's redesign. It exposes too many meticulous per-function slots; Phase 2 reshapes it into a narrow, reviewable host DOM API. Redesigning it now, in the middle of a 28k-line relocation, would couple two risky changes and pre-empt what the `import dom` surface (ES36) will teach about the right shape.

### ES35 (ratified 2026-09-01) — one symbol namespace: `dom_*`

All moved symbols rename `js_dom_*` → `dom_*`; the un-prefixed browser accessors in the moved headers (`js_get_computed_style`, `js_classlist_*`, `js_dataset_*`, `js_document_proxy_*`, `js_storage_*`, `js_match_media*`, `js_cssom_*`, `js_xhr_*`, …) join the `dom_*` family. `js_` remains reserved for the adapter layer (functions whose *contract* is JS shape). Two disciplines ride along:

- **Weak pairs rename atomically** — declaration, empty default, strong definition, and test stub in one change, or radiant-only binaries silently lose their hooks.
- **String-table sweep** — name-registered tables (JIT import tables, Jube descriptors) resolve by string; a missed rename is a runtime lookup failure, not a link error. The rename stage greps string literals for every renamed symbol before it is called done.

### ES36 (ratified 2026-09-01) — the Lambda `import dom` module

A new Jube **static module `"dom"`** (`lambda/dom/dom_module.cpp`, registered beside `radiant` in `jube_register_builtin_modules()`) exposes the DOM API to Lambda scripts. Bare `import dom` resolves through the Jube registry; no collision with the behavior package, which is only ever imported by full path (`lambda.package.dom.dom`).

**Surface** (~28 functions, snake_case, ≤4 args per the DOM_Pkg §4.3 waist budget; signatures in Lambda type syntax per the native-module convention):

| Cluster | Functions |
|---|---|
| load/parse | `load(path)`, `parse(html)`, `root(doc)`, `document_element(doc)` |
| query | `query(node, sel)`, `query_all(node, sel)`, `matches(node, sel)`, `closest(node, sel)`, `element_by_id(doc, id)` |
| read | `parent(n)`, `children(n)`, `first_element_child(n)`, `next_element_sibling(n)`, `node_name(n)`, `text(n)`, `attr(n, name)`, `has_attr(n, name)` |
| write | `set_attr(n, name, v)`, `remove_attr(n, name)`, `set_text(n, t)`, `create_element(doc, tag)`, `create_text(doc, t)`, `append(parent, child)`, `insert_before(parent, child, ref)`, `remove(n)`, `clone(n, deep)` |
| html | `inner_html(n)`, `set_inner_html(n, html)`, `outer_html(n)` |
| geometry | `bounding_rect(n)` (post-layout; ES30's staleness contract applies) |

**Semantics**: collections are **snapshot Lambda arrays** (S9.2.2 — "read views are first-class values with snapshot semantics"; the ES30 precedent). Live collections remain a JS-adapter concern (DOM_Pkg Q4). Nodes are the existing branded `dom_node` VMaps — the same value `~` binds in behavior handlers, so module results compose with the waist. Errors are error Items per the Jube function convention, consumable by `T^E`/postfix `^` (S7.6/S7.10 discharge applies at the sys-func boundary). Every function is a thin delegation to the ES32 core — the module adds no second implementation of anything.

**Linkage note (vs ES34)**: `dom_module.cpp` calls the core directly — it lives in the same `lambda-rt` target as the core and *is* the core's own Lambda-facing module face, not a foreign module reaching across the seam. The `JubeHostDomAPI` seam governs cross-target and dynamic consumers; it is not violated by the core presenting itself.

**Catalog note (vs §7)**: this function list is designed as the **first tranche of the Phase-2 operation catalog** — names, signatures, and clusters chosen so the unification re-homes the registrations without changing the script-visible surface. F26 must not invent operations the catalog would reject (the budget rule applies from day one).

### ES37 (ratified 2026-09-01) — peripherals move-and-rename only, for now

`dom_canvas`/`dom_xhr`/`dom_fetch`/`dom_formdata`/`dom_clipboard`/`dom_history` relocate and rename but keep their JS residue: their surfaces (Promises, JS class objects, ctor publication) are inherently L4-shaped until the async/Promise seam is designed (ESO74). Applying ES33 to them now would either fake a split or stall the carve-out. They are *in* `lambda/dom/` because that is where the web platform lives; they are *exempt* from the purity grep until ESO74 resolves.

### ES38 (ratified 2026-09-01) — one implementation per DOM operation

Extends CLAUDE.md rule 13 to this area as a standing invariant: for any DOM operation, the `dom.*` module function, the `radiant.*` waist function, and the JS declared-interface ordinal must bottom out in **the same core function** in `lambda/dom/`. One core implementation — not necessarily one call path: `dom.*` reaches it directly (same target, ES36 linkage note), while `radiant.*` reaches it **through the ES34 seam** (existing table slots/ordinals, as `set_attr`/`closest` already do at `radiant_module.cpp:1205,1211,1514`). The radiant module's overlapping tree functions (`attr`, `set_attr`, `first_element_child`, `next_element_sibling`, `closest`, `parent`, `document_root`, …) keep their behavior-waist role and their `.ls` callers unchanged. A private near-duplicate of core logic found during migration is a defect to fold, not a variant to preserve — folded on the core side, behind the seam. This ruling is the implementation-level half of the §7 unification, enforced from F22 onward so Phase 2 inherits a deduplicated core rather than having to create one.

---

## 5. Migration stages

Each stage is independently landable, behavior-neutral, and gated on: `make build`, `make test-radiant-baseline` + `make test-lambda-baseline` (both 100%), the JS Event/DOM gtests, the dom UI fixtures (`dom_synthetic_activation` 6/6, `dom_pkg_prevent_default` 4/4, `dom_pkg_radio` 7/7, `js_dispatch_radio_group`), and `make lint`. One stage per commit, never combined.

| Stage | Contents | Exit criterion |
|---|---|---|
| **F22 — relocate** | `git mv` the 20 files to `lambda/dom/` per ES32 (content untouched except `#include` paths); update the ~18 includers; build config: add `lambda/dom/*.cpp` glob to `lambda-rt` + `lambda/dom` to both include lists; drop the two duplicated mid-file includes found in census | tree builds; all gates green; `git log --follow` preserves file history |
| **F23 — host-side extern hygiene** | Replace ad-hoc `extern "C"` declarations in **host-side** code with includes of the new `lambda/dom/` headers (CLAUDE.md rule 13): `radiant/event.cpp:48–61,6937`, `window.cpp:44–47`, `editing_dom_waist.cpp`, `cmd_layout.cpp:79`, `lambda/runtime/runner.cpp:43`, `lambda/js/js_runtime_state.cpp:16–18,75–78`, the moved peripherals' internal externs; move `radiant/event.hpp:48` externs into the dom header. **Untouched per ES34**: the `JubeHostDomAPI` table + bridge remaps (proper seam usage), the module-side bypasses (`radiant_module.cpp:51–60`, frozen for ESO78), the weak pairs | no ad-hoc `js_dom_*`/`dom_*` externs outside the Jube table wiring, the frozen module bypasses, and the weak pairs; gates green |
| **F24 — rename** | ES35: `js_dom_*` → `dom_*` + un-prefixed accessors, **including** the `JubeHostDomAPI` prototype/initializer references in `jube_registry.cpp` and the bridge remap defines (table layout untouched, ES34); weak pairs atomic; string-literal sweep of name-registered tables | `grep -rn "js_dom" lambda/ radiant/ test/ --include=*.{c,cpp,h,hpp}` → empty; gates green |
| **F25 — adapter split** | ES33: extract Event-class stamps/ctors/init-dicts → `js_event_adapter.cpp`; prototype/global/meta glue → `js_dom_adapter.cpp`; scheduling seam (5 sites); throw-convention audit on Lambda-reachable paths (ESO76); fix the duplicated `js_ctor_static_range_fn` declaration | purity grep clean over the six core families: no `JS_CLASS_`, `js_object_meta`, `js_get_global_this`, `js_install_native_method`, `js_setTimeout` under `lambda/dom/{dom,dom_events,dom_observers,dom_platform,dom_selection,dom_cssom}.*`; gates green |
| **F26 — `import dom`** | ES36 module (its fn list doubling as the first tranche of the §7 catalog) + ES38 dedupe of the radiant tree functions; tests `test/lambda/dom_api.ls` + `dom_api_read.ls` (+ `.txt` goldens, rule 8): parse/load → query → mutate → re-query → serialize round-trip; snapshot-stability check (mutation during iteration does not perturb a taken snapshot) | POC-1 exit test passes from a pure Lambda script; `make test-lambda-baseline` 100% with the new tests in the glob |
| **F27 — docs & closure** | Path/name refresh + verified-against stamps: `JS_13`, `JS_00/10/14/15`, `RAD_15`, DOM_Dispatch Appendix A, DOM_Default §3 anchor note; `Lambda_Design_Native_Module.md` §8 POC-1 status (carve-out landed; host DOM API redesign deferred to Phase 2/ESO78); DOM_Pkg Phase-0 status; CLAUDE.md Key Entry Points row for `lambda/dom/` mirrored in AGENTS.md | grep for `lambda/js/js_dom` in `doc/`+`vibe/` returns only historical/superseded contexts |

Sequencing rationale: F22 before F23 so the hygiene diff is against stable paths; F23 before F24 so the rename sweeps headers, not scattered ad-hoc declarations; F25 after F24 so extracted adapter functions are named against the final namespace; F26 last because ES38's dedupe wants the split core as its target. The `JubeHostDomAPI` is renamed-through in F24 and otherwise untouched until Phase 2 (§7).

## 6. Open issues

| ID | Issue |
|---|---|
| ESO72 | **`js_event_loop ↔ dom` cycle / no `lambda-dom` link target yet.** The loop calls into DOM context at 13 sites; DOM's 5 outbound scheduling calls are seamed by ES33, but the inbound 13 keep `lambda/dom/` inside `lambda-rt`. A dedicated static lib (and a boundary DSO for it) becomes possible once the loop reaches DOM context through a narrower interface. Not load-bearing now. |
| ESO73 | **`dom.cpp` is ~16.5k lines** (~14k post-split) and JS_13 known-issue 5 already flags its size and per-access `strcmp` ladders. Split by domain (attrs/tree/selectors/geometry/forms/html) as a follow-up — after F24, so history survives the move+rename first. |
| ESO74 | **Peripheral de-JS-ification** (ES37's exemption): fetch/xhr/clipboard need an async/Promise seam design before their mechanism (HTTP via `input_http`, clipboard I/O, form-entry construction) can split from their JS shape. |
| ESO75 | **The shared value helpers wear a `js_` prefix** (`js_name_item`, `js_set_key_cstr`, `js_new_object`, …, ~600 call sites in the core). Under ES12 they are the one runtime's value API. Decide: neutral aliases (`rt_*`?), wholesale rename, or accept the prefix as historical. Deliberately *not* bundled into F24 — it touches the whole JS runtime, not just the DOM layer. |
| ESO76 | **Throw-convention audit tail**: ~33 `js_throw_*` sites in the core. F25 converts Lambda-reachable ones to error Items and moves JS-protocol ones to the adapter, but the general rule for *shared* validation paths (one core check, two realm-specific error presentations) deserves a stated pattern once `import dom` has real users. |
| ESO77 | **Struct co-location** (user-deferred): moving `dom_node`/`dom_element`/`dom_lifecycle` from `lambda/input/css/` into `lambda/dom/` — reconcile with `Radiant_Design_Dom_View_Struct.md` OQ5 when taken up; the stems are already reserved (ES32). |
| ESO78 | **The new DOM API — Phase 2 unification** (user direction 2026-09-01: table retained now; then `radiant_module`'s DOM surface + `JubeHostDomAPI` unify into one DOM API, deduped at the API level *and* the implementation level). One operation catalog derives the table slots (the dynamic ABI for the AST interpreter and non-statically-linked Jube modules such as Python), the script registrations, and the ordinal wiring; the frozen module-side bypasses (`radiant_module.cpp:51–60` and kin) fold in. Design in §7. |

Cross-referenced, not duplicated here: live collections (DOM_Pkg Q4), the L3 Obscura-parity package phases (DOM_Pkg §7 — unblocked by this carve-out), package state model (DOM_Pkg Q1).

---

## 7. DOM API Phase 2 (deferred) — the new DOM API: one catalog, one table, one implementation

**Target set by user 2026-09-01**: `radiant_module`'s DOM surface and the `JubeHostDomAPI` **unify into one new DOM API** — "dedup and unify, not only the API functions, but also the impl behind." Recorded here as ratified *direction*; the detailed catalog design is Phase-2 work, **not part of F22–F27**, started when the user schedules it (ESO78).

### 7.1 The shape

One **operation catalog** is the single source of truth for the DOM API. From it derive every consumption surface, so an operation is declared once and implemented once:

```
                     ┌── one operation catalog (name, signature, cluster, ordinal) ──┐
                     │                                                               │
   JubeHostDomAPI slots (the dynamic ABI)                    script-facing fn registrations
                     │                                          (`dom.*`; `radiant.*` DOM entries
                     │                                           delegate or alias for compat)
                     ▼                                                               ▼
              one core implementation per operation, in lambda/dom/  ◄── declared-interface
                                                                         ordinals (bridge/iface)
```

- **One implementation** per operation lives in `lambda/dom/` (the ES32 core). No wrapper-level reimplementation, no per-surface variants — the ES38 invariant made total.
- **The table is the dynamic ABI** (ES34 reasons 2–3): the AST interpreter dispatches through it; dynamically-loaded Jube modules (Python, and any future guest) receive it via `host->dom` and call DOM with no static linking. It is therefore versioned and shaped for stability, not for the bridge's convenience.
- **Grain**: clustered per the DOM_Pkg §4.3 waist taxonomy (tree read / tree write / parse-serialize / match-query / geometry / mutation-ring / scheduling / event ops / action primitives), sized to be reviewable at a glance, each addition budget-reviewed ("if a single API needs more than one *new* primitive cluster, its placement is wrong"). The ordinal-dispatch pattern (one executor slot + a capability enum, as `dom_element_operation_impl` + `JubeDomElementOperation` already do) is the proven compression for the WebIDL-shaped long tail — candidates: keep the per-function slots only for the true waist verbs, route the long tail through ordinals.
- **Script surfaces are registrations, not APIs of their own**: `dom.*` (ES36) exposes the catalog to Lambda; the `radiant.*` DOM-flavored entries become aliases/delegations for compatibility (the behavior package's `.ls` callers stay unchanged), with `radiant.*` retaining only the engine-specific non-DOM primitives (state waist, layout registration, velmt, UI). Whether the aliases eventually retire is a migration question for Phase 2, decided against the package's needs — not silently.

### 7.2 What unification dissolves

- The two parallel APIs of §3.2 (host table vs `radiant.*` fn table) and their in-between wrappers.
- The module-side ad-hoc bypasses (`radiant_module.cpp:51–60`) — each becomes a catalog operation or proves it belongs module-side.
- The five-site registration cost — declaring an operation in the catalog yields the slot, the registration, and the prototype from one place.
- The iface direct entries (`RADIANT_GETTER`/`RADIANT_METHOD_*` range/selection slots) — re-derived from the catalog like everything else.

### 7.3 Inputs Phase 2 waits for (why not now)

- the **`import dom` surface in real use** (ES36/F26) — the best evidence for which operations are the true waist and which are per-property plumbing the declared-interface records (D7.4.4) should own outright;
- the **adapter split settled** (ES33/F25) — so the table is not asked to carry JS-shape traffic that belongs to the adapter;
- the **frozen bypass inventory** — collected, not grown, during F22–F27.

Exit shape (to be ratified then): one catalog; a `JubeHostDomAPI` small enough to review at a glance; zero module-side externs into `lambda-rt`; the AST interpreter and at least one guest module (Python) driving DOM through the table in tests; dynamic loadability of DOM-consuming modules a testable property rather than an aspiration.

---

## Appendix A — source map (anchors at 2026-09-01; treat as neighborhoods)

| Where | What (this doc) |
|---|---|
| `lambda/js/js_dom*.{cpp,h}`, `js_cssom.*`, `js_canvas.cpp`, `js_xhr.*`, `js_fetch.cpp`, `js_formdata.cpp`, `js_clipboard.cpp`, `js_history.*` | the 20 files ES32 relocates |
| `lambda/jube/jube.h:676–928` | `JubeHostDomAPI` (~216 slots) — **retained** (ES34); renamed-through in F24; reshaped in Phase 2 (ESO78) |
| `lambda/jube/jube_registry.cpp:997+`, `:1507–1731` | prototype block + table wiring — renamed in F24, layout untouched |
| `lambda/module/radiant/radiant_dom_bridge.cpp:68+` | ~125 `#define js_x radiant_host_api->dom->x` remaps — proper seam usage; renamed in F24 |
| `lambda/module/radiant/radiant_dom_iface.cpp:18–308`, `:1609` | declared interfaces + member-record tables — unchanged by this proposal (D7.4.4) |
| `lambda/module/radiant/radiant_module.cpp:2754–2952`, `:2960` | the `radiant` module fn table + `JubeModuleDef` — ES38 dedupe target, ES36 template |
| `radiant/event.cpp` (77 refs), `window.cpp`, `script_runner.cpp`, `layout.cpp`, `event_sim.cpp`, `cmd_layout.cpp`, `radiant/event.hpp:48` | direct-call consumers — includes replace externs (F23) |
| `radiant/text_edit.cpp:473`, `text_control.cpp:16`, `dom_range.cpp:645`, `lambda/input/css/dom_lifecycle.cpp:39`, `test/test_state_store_stubs.cpp:101` | weak-pair hooks (ES35 atomic rename) |
| `lambda/js/js_event_loop.cpp` (13 inbound sites) | the ESO72 cycle |
| `build_lambda_config.json:640`, `:36`, `:1137` | the three build edits F22 needs |
| `test/lambda/radiant_dom_read.ls`, `radiant_dom_mutate.ls`, `proc/radiant_dom_set.ls` | existing Lambda-DOM test patterns F26 follows |

## Appendix B — implementation notes (brief, per convention)

- **Preflight**: record baseline pass counts for every gate before F22; any later "regression" is verified standalone first (parallel-run flakiness is a known false positive).
- **F22 mechanics**: `git mv` per family; include fixes are path-only; `make` regenerates premake from `build_lambda_config.json` (never edit the `.lua`).
- **F24 sweep**: grep string literals (`"js_dom`, `"js_cssom`, `"js_xhr`, each renamed un-prefixed name) across `lambda/`+`radiant/`; suspects are JIT import tables and Jube descriptor strings; `sys_func_registry.c` verified clean.
- **F25 enforcement**: prefer a lint rule beside `no-int-cast-radiant` if the linter supports it; otherwise the documented grep in the stage's exit criterion.
- **F26 smoke**: a scratch script under `./temp/` exercising `import dom` end-to-end, plus `RADIANT_DOM_PKG=0` to confirm the package-off fallback is unaffected.
