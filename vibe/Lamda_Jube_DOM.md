# Lambda Jube DOM Implementation Plan

> Status: implementation plan for POC 1.
> Parent design: [Lambda_Native_Module_Design.md](./Lambda_Native_Module_Design.md).
> Working module name: `radiant`, imported as `import radiant`.

## 1. Goal

Build the first visible Jube native-module POC by moving Radiant DOM access behind a module-owned interface while preserving current JS DOM behavior.

Implementation status:

- 2026-07-06: Phase 1 static skeleton started. Added a fixed-capacity static Jube registry, `JubeModuleDef`/descriptor scaffolding, and a statically registered `radiant` module with a stub `dom_node` type. No DOM dispatch behavior has moved yet.
- 2026-07-06 verification: `make build` passed. Runtime smoke `./lambda.exe --no-log test/lambda/value.ls` passed.
- 2026-07-06: Phase 2 compatibility bridge started. Public `js_dom_get_property()` / `js_dom_set_property()` now route through `radiant_dom_get_property()` / `radiant_dom_set_property()`, which delegate to the preserved JS DOM implementation bodies. This moves the call boundary without changing wrapper representation or per-property behavior.
- 2026-07-06 bridge verification: `make build` passed. DOM smokes `./lambda.exe js test/js/dom_style.js --document test/js/dom_style.html --no-log` and `./lambda.exe js test/js/dom_mutation.js --document test/js/dom_mutation.html --no-log` passed.
- 2026-07-06: Phase 2 first property cluster moved. `radiant_dom_get_property()` now handles simple module-owned DOM reads before falling back to `js_dom_get_property_impl()`: element `tagName`, `nodeName`, `localName`, `namespaceURI`, `prefix`, `id`, `className`, `nodeType`; text `data`, `nodeValue`, `textContent`, `length`, `nodeType`, `nodeName`; comment `data`, `nodeValue`, `textContent`, `length`, `nodeType`, `nodeName`. Range, Selection, CSSOM, style, navigation, collections, methods, layout geometry, and setters remain on the JS fallback path.
- 2026-07-06 property-cluster verification: `make build` passed. Added `test/js/dom_module_props.js` / `.html` / `.txt`; raw DOM run returned the expected output, filtered gtest `JavaScriptTests/JsFileTest.Run/dom_module_props` passed, existing DOM identity/style/mutation smokes passed, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 3 compatibility bridge started. Public `js_dom_wrap_element()`, `js_dom_unwrap_element()`, and `js_is_dom_node()` now route through `radiant_dom_wrap_node()`, `radiant_dom_unwrap_node()`, and `radiant_dom_is_node()`. The physical cache/rooting implementation is still in `js_dom.cpp`, but the module boundary now owns the wrapper factory surface.
- 2026-07-06 wrapper verification: `make build` passed. Added `test/js/dom_identity.js` / `.html` / `.txt` and verified repeated Radiant node access preserves strict JS identity through the module bridge.
- 2026-07-06: Phase 3 cache ownership moved. The wrapper cache, GC rooting, and reset/unroot cleanup now live in `lambda/module/radiant/radiant_dom_bridge.cpp`. `js_dom.cpp` keeps only the fresh compatibility wrapper constructor for `MAP_KIND_DOM` until the VMap phase.
- 2026-07-06 cache verification: `make build` passed. DOM identity/style/mutation smokes passed after the cache move.
- 2026-07-06: Phase 3 document invalidation hook added. `free_document()` now calls `radiant_dom_invalidate_document()` before the document arena is destroyed, so module-owned non-owning wrappers are unrooted before native nodes disappear. Cache entries store an owner `DomDocument*` at cache time so detached text/comment wrappers can be invalidated even when they do not carry their own document pointer.
- 2026-07-06 invalidation verification: `make build` passed. DOM identity/style/mutation smokes and `./lambda.exe --no-log test/lambda/value.ls` passed.
- 2026-07-06: Phase 4 tiny Lambda-facing sample landed. `import radiant; poc_attr("test/js/dom_identity.html")` loads a real Radiant HTML document, mutates the root `data-poc` attribute, reads it back, frees the document, and returns `"ok"`. This uses a temporary sys-func compatibility bridge (`radiant_poc_attr` -> `fn_radiant_poc_attr`) until Jube descriptors feed Lambda import metadata directly.
- 2026-07-06 sample verification: `make build` passed. `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"`. Existing built-in import smokes `builtin_import_global.ls` and `builtin_import_alias.ls` still passed.
- 2026-07-06: Phase 4 sample upgraded from the one-shot helper to the first real DOM API cluster. `test/lambda/radiant_poc.ls` now uses `radiant.load()`, `radiant.root()`, `radiant.set_attr()`, `radiant.attr()`, and `radiant.free()` over module-owned DOM wrappers. Calls are module-qualified because bare `load()` already exists as a core built-in and should not be shadowed by a global import fallback.
- 2026-07-06 API cluster verification: `make build` passed. The upgraded `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"` and forces `radiant.free(doc)` through the final expression. Focused JS DOM identity/style/mutation smokes passed. `make test-lambda-baseline` reached 3233/3234 with one `child_process_detached_kill` miss; direct rerun of that case and the full `test_node_prelim_gtest.exe` binary passed.
- 2026-07-06: Phase 2 class-name read moved. Element `className` now joins Radiant's parsed class list inside `radiant_dom_bridge.cpp`, and `test/js/dom_module_props` covers the module-owned read from a static HTML `class` attribute. Writable `id` / `className` and navigation reads still remain on the JS fallback path because mutation notifications and script-visible traversal helpers are still JS-local.
- 2026-07-06 class-name verification: `make build` passed. Direct `dom_module_props` run matched the expected output, filtered gtest `JavaScriptTests/JsFileTest.Run/dom_module_props` passed, identity/style/mutation/Radiant sample smokes passed, and full `./test/test_js_gtest.exe` passed `305/305`.

The first deliverable is intentionally conservative:

1. Add a statically linked `radiant` Jube module.
2. Move DOM property and method resolution tables into module-owned code.
3. Keep existing `MAP_KIND_DOM` wrappers for the first checkpoint, but make their get/set path delegate through the module-owned dispatch layer.
4. Add the host/module DOM wrapper cache required by the native-module design, without scoping it per `DomDocument`.
5. Add one tiny Lambda-facing sample early so the POC proves cross-front-end access, not only JS compatibility.

This is not the phase that deletes `MAP_KIND_DOM`. The representation migration to VMap comes after the behavior-preserving checkpoint is green.

## 2. Ground Rules

- The module name is `radiant` for now. A later rename should be a registry/name-resolution change, not a rewrite of the dispatch layer.
- DOM wrappers follow the native-module design: native pointers surface as branded module-owned native objects, eventually VMap projections.
- DOM wrapper identity is module/host scoped, not per `DomDocument`.
- DOM nodes are non-owning wrappers over Radiant-owned nodes. The wrapper finalizer must not free `DomNode` / `DomElement`.
- Dynamic loading is deferred. POC 1 starts with a static module using the same registration path intended for dynamic modules.
- The first implementation slice should not change browser-visible DOM semantics.

## 3. Current Anchors

Current relevant code paths:

- `lambda/js/js_dom.cpp`: DOM wrapper creation, property/method dispatch, DOM mutation helpers.
- `lambda/js/js_runtime.cpp`: JS property get/set routes `MAP_KIND_DOM` to `js_dom_get_property()` / `js_dom_set_property()`.
- `lambda/js/js_dom_events.cpp` and `lambda/js/js_dom_selection.cpp`: large consumers of `js_dom_wrap_element()`.
- `lambda/js/js_cssom.cpp`: CSSOM host objects that should follow the same module boundary later.
- `lambda/lambda.hpp`: current `VMapVtable` shape.
- `lambda/vmap.cpp`: current hash-backed VMap implementation and VMap access helpers.
- `lambda/sys_func_registry.c`: central native import table that the module system should shrink over time.

The important observation: DOM objects already do C-dispatch per property. Moving the dispatch tables into a module is not replacing a true JS object fast path; it is relocating an existing host-object dispatch route.

## 4. Target Architecture

The `radiant` module owns three surfaces:

1. Native types
   - `radiant.dom_node`
   - later split names if useful: `radiant.dom_element`, `radiant.dom_text`, `radiant.document`, `radiant.range`, `radiant.selection`, `radiant.css_style`

2. Lambda-facing functions
   - small, typed functions over `dom_node` and strings/numbers
   - signatures use Lambda type syntax
   - mutating DOM functions are `pn`

3. JS-facing host-object adapter
   - property get/set tables
   - method factories
   - wrapper identity cache
   - later VMap ops and JS semantic adapter hooks

`MAP_KIND_DOM` remains a compatibility shell during POC 1A. It should contain less policy over time and become a thin call into `radiant`.

## 5. Wrapper Cache Design

The native-module design requires stable wrapper identity:

```js
el.parentNode === el.parentNode
```

POC cache policy:

- Key: raw native node pointer plus native type brand.
- Scope: module/host cache associated with the active runtime context, not with `DomDocument`.
- Value: wrapper `Item`.
- Rooting: cached wrapper `Item`s are GC-rooted while present in the cache.
- Ownership: DOM node wrappers are non-owning; cache reset/unroot releases wrapper roots but does not free DOM nodes.
- Document teardown: the cache must expose invalidation for nodes belonging to a destroyed document, but the cache itself is not stored inside the document.

The current thread-local wrapper cache in `js_dom.cpp` proves the identity requirement. The POC moves that idea toward the module/host boundary so it can later create VMap wrappers instead of `MAP_KIND_DOM` maps.

Open implementation detail: whether the first cache object lives in `lambda/js/` while still JS-only, or in a new `lambda/module/radiant/` area immediately. Prefer the latter if build wiring stays small.

## 6. Phased Implementation

### Phase 0: Baseline and Guardrails

Purpose: capture current behavior before moving dispatch.

Tasks:

- Run the smallest relevant JS DOM / Radiant baseline available in the current tree.
- Record the command and result in this doc when implementation starts.
- Confirm `make build` is green before touching module code.
- Identify the minimal DOM property cluster for the first dispatch table:
  - node identity and tree navigation: `nodeType`, `nodeName`, `parentNode`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, `childNodes`
  - text: `data`, `nodeValue`, `textContent`, `length`
  - element basics: `tagName`, `id`, `className`, `style`, attributes

Acceptance:

- No code changes yet.
- Baseline command and result are known.

### Phase 1: Static `radiant` Module Skeleton

Purpose: create the module boundary without changing behavior.

Tasks:

- Add a small static module definition for `radiant`.
- Add the minimum Jube registration surface needed by a static in-tree module:
  - module name
  - native type descriptor stubs
  - function descriptor stubs
  - init hook
- Register the module during runtime startup.
- Keep all descriptors inert until the dispatch layer is wired.

Suggested files:

- `lambda/jube/jube.h`
- `lambda/jube/jube_registry.cpp`
- `lambda/module/radiant/radiant_module.cpp`
- `lambda/module/radiant/radiant_dom_bridge.cpp`

Acceptance:

- `make build` passes.
- No JS DOM behavior changes.
- `radiant` appears in a debug/log-visible static registry path.

### Phase 2: Module-Owned DOM Dispatch Tables

Purpose: move DOM policy into the module while preserving wrappers.

Status:

- Started 2026-07-06 with the compatibility bridge: public JS DOM get/set entry points route through `radiant_dom_get_property()` / `radiant_dom_set_property()`.
- First read cluster moved 2026-07-06. The module now owns simple identity/name/value reads for elements, text nodes, and comment nodes, then falls back to the old JS implementation for everything else.
- Added `test/js/dom_module_props.js` / `.html` / `.txt` to pin the moved cluster through the normal JS DOM harness.
- Remaining Phase 2 work: move writable `id` / `className` and narrow navigation reads (`parentNode`, `firstChild`, sibling reads, `childNodes`) once mutation notification and script-visible traversal dependencies are cleanly exposed or duplicated in module-owned form.

Tasks:

- Introduce a `radiant_dom_get_property(Item receiver, Item key)` entry point.
- Introduce a `radiant_dom_set_property(Item receiver, Item key, Item value)` entry point.
- Move a narrow first property cluster from `js_dom_get_property()` / `js_dom_set_property()` into module-owned table functions.
- Leave the old JS functions as wrappers/delegates:

```c
extern "C" Item js_dom_get_property(Item elem_item, Item prop_name) {
    return radiant_dom_get_property(elem_item, prop_name);
}
```

- Keep Range, Selection, CSSOM, inline style, and document proxy special cases outside the first moved cluster unless required by tests.
- Add a temporary parity mode if useful: old path and new path can both compute selected properties and log mismatches with a distinct prefix.

Acceptance:

- Existing `MAP_KIND_DOM` property get/set still works.
- The first moved cluster has no behavior delta in targeted JS DOM tests.
- `make build` passes.

### Phase 3: Module/Host Wrapper Cache

Purpose: prepare for VMap wrappers without changing wrapper representation yet.

Status:

- Started 2026-07-06. The JS ABI entry points now delegate to module-owned wrapper, unwrap, and type-test functions.
- Cache ownership moved 2026-07-06. `radiant_dom_wrap_node()` performs module-owned lookup/cache/rooting and calls back into `js_dom_create_wrapper_impl()` only on cache miss to construct the current `MAP_KIND_DOM` compatibility carrier.
- Document invalidation hook added 2026-07-06. `radiant_dom_invalidate_document()` removes matching cache entries by stored owner document or live parent-chain ownership before `free_document()` destroys native nodes.
- Current checkpoint preserves `MAP_KIND_DOM` wrappers. The remaining Phase 3 lifecycle gap is finer-grained subtree/node invalidation if Radiant later frees individual nodes before document teardown.
- Added `test/js/dom_identity.js` to cover repeated access identity for element and text nodes.

Tasks:

- Add `radiant_dom_wrap_node(void* node)` as the module-owned wrapper factory.
- Make `js_dom_wrap_element()` delegate to it.
- Move lookup/cache/root/unroot behavior behind module-owned functions.
- Keep returned wrappers as `MAP_KIND_DOM` maps in this phase.
- Add cache invalidation/reset hooks currently handled by JS DOM cache cleanup.

Cache invariants:

- Same node pointer and type brand returns the same wrapper while cached.
- Cache is not per `DomDocument`.
- Cache roots Items, not raw DOM nodes.
- DOM wrapper destroy is non-owning.

Acceptance:

- JS identity tests still pass.
- No leaked roots after document teardown/reset paths.
- `make build` passes.

### Phase 4: Tiny Lambda-Facing Sample

Purpose: prove the module is not JS-only.

Status:

- Completed first visible sample 2026-07-06 with `test/lambda/radiant_poc.ls` and `test/lambda/radiant_poc.txt`.
- Upgraded 2026-07-06 to a first real API cluster: `radiant.load(path)`, `radiant.root(doc)`, `radiant.set_attr(node, name, value)`, `radiant.attr(node, name)`, and `radiant.free(doc)`.
- The current document handle is still the root DOM wrapper; `radiant.root(doc)` returns the owning document root from that wrapper. This is a compatibility compromise until `radiant.document` becomes a first-class native type.
- `poc_attr(path)` remains as a compatibility smoke, but new sample code should prefer the module-qualified API cluster. Bare `load()` intentionally resolves to the existing core built-in, so the sample uses `radiant.load()`.
- The sample loads a real HTML document through `load_lambda_html_doc()`, mutates `doc->root`, reads the attribute back, and forces `free_document()` through `radiant.free(doc)`.
- Next API slice should either introduce a first-class `radiant.document` wrapper or start Phase 5's VMap readiness spec before adding more DOM surface.

The first sample should be deliberately small. It should avoid needing full script-level package management or dynamic loading.

Candidate API:

```lambda
import radiant

let doc = radiant.load("test/input/simple.html")
let root = radiant.root(doc)
radiant.set_attr(root, "data-poc", "ok")
let value = radiant.attr(root, "data-poc")
[value, radiant.free(doc)][0]
```

Candidate expected result:

```text
"ok"
```

If `input('html')` does not expose the right document/root handle cleanly yet, use a tiny helper that wraps the active layout document for the POC. Do not fake the DOM with a standalone map; the sample must exercise real Radiant DOM nodes.

Acceptance:

- A real Lambda script imports `radiant`.
- It reads and mutates a real DOM node.
- If a new `*.ls` test is added, add the matching `*.txt` expected file in the same change.

### Phase 5: VMap Readiness Spec

Purpose: answer the questions that block deleting `MAP_KIND_DOM`.

Tasks:

- Extend the native type descriptor with ownership:
  - owning native object
  - non-owning host object
- Extend or wrap `VMapVtable` for JS host-object needs:
  - `has`
  - `delete`
  - property descriptor reflection
  - stable enumeration order
- Define expando behavior:
  - vtable property first
  - on miss, wrapper expando store
  - then prototype chain
- Define prototype mapping:
  - `dom_node` -> `Node.prototype`
  - `dom_element` -> `Element.prototype`
  - HTML element subclasses later
- Define live collection wrappers for `children`, `childNodes`, and indexed select/options behavior.

Acceptance:

- The VMap migration rules are written down before any wrapper representation change.
- JS behavior that depends on identity, expandos, prototype patching, and descriptors has targeted tests selected.

### Phase 6: Switch DOM Wrappers to VMap

Purpose: make DOM a real native module object.

Tasks:

- Change `radiant_dom_wrap_node()` to return branded VMap wrappers.
- Make `js_dom_unwrap_element()` accept the branded VMap representation.
- Route JS property access for `LMD_TYPE_VMAP` host objects through the generic JS native-type adapter.
- Keep temporary compatibility for old `MAP_KIND_DOM` only if required for staged rollout.

Acceptance:

- JS DOM tests pass through the VMap path.
- Lambda sample still passes.
- The wrapper cache still preserves identity.
- DOM node finalizers are non-owning.

### Phase 7: Delete DOM MapKind Path

Purpose: finish the POC's architectural goal.

Tasks:

- Remove `MAP_KIND_DOM` property dispatch.
- Remove DOM-specific branches from JS runtime where the generic native-type adapter is sufficient.
- Delete stale wrapper-map creation code.
- Remove `js_dom_*` central registry entries that have moved to module descriptors.

Acceptance:

- JS DOM tests pass.
- `make test-radiant-baseline` passes or has a documented unchanged baseline delta.
- The tiny Lambda sample passes.
- No DOM bridge entries remain in `sys_func_registry.c` except compatibility shims explicitly documented for later removal.

## 7. First Deliverable Checklist

The first visible POC deliverable is complete when:

- `import radiant` is the planned script surface and the static module is registered.
- `MAP_KIND_DOM` get/set delegates to module-owned DOM dispatch for at least the first property cluster.
- Wrapper cache ownership has moved behind module/host APIs, still returning current map wrappers.
- One tiny Lambda sample proves real DOM read/write access.
- `make build` passes.
- A focused JS DOM/Radiant regression gate has no behavior delta.

## 8. Test Gates

Use the narrowest gate that covers the touched surface, then widen at phase boundaries.

Minimum gates by phase:

- Phase 1: `make build`
- Phase 2: focused JS DOM property tests plus `make build`
- Phase 3: wrapper identity tests plus `make build`
- Phase 4: new Lambda sample test plus `make build`
- Phase 6: JS DOM baseline slice, Lambda sample, `make test-radiant-baseline`
- Phase 7: full JS DOM/Radiant gate selected from the current tree, plus `git diff --check`

Do not use a debug build for performance comparisons. Performance parity claims wait until release builds and a stable benchmark target exist.

## 9. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| DOM semantics regress during refactor | Hold Phase 2 as a long-lived checkpoint: old representation, new dispatch tables |
| Wrapper identity breaks | Move cache as its own phase and test identity before VMap migration |
| VMap lacks JS host-object operations | Specify and add missing ops before switching wrappers |
| DOM node lifetime confusion | Mark DOM native type non-owning; finalizer never frees Radiant nodes |
| The module boundary grows an internal side door | Keep in-tree module code on the same Jube host API planned for dynamic modules |
| Lambda sample becomes fake | Require the sample to touch real Radiant DOM nodes, not a standalone test map |

## 10. Deferred Work

- Dynamic `dlopen` / manifest loading.
- Full CSSOM migration.
- Full Range/Selection migration.
- Node module refactor.
- Online registry and version resolution.
- Deleting `MAP_KIND_DOM`.

`MAP_KIND_DOM` deletion is deferred, not abandoned. The point of this plan is to make that deletion boring when it happens.
