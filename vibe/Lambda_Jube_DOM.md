# Lambda Jube DOM Implementation Plan

> Status: POC 1 implementation progress record, updated 2026-07-06.
> Parent design: [Lambda_Native_Module_Design.md](./Lambda_Native_Module_Design.md).
> Working module name: `radiant`, imported as `import radiant`.

## 1. Goal

Build the first visible Jube native-module POC by moving Radiant DOM access behind a module-owned interface while preserving current JS DOM behavior.

Current checkpoint:

- POC 1A is now implemented as a compatibility checkpoint. `radiant` is statically registered, JS DOM get/set/wrapper entry points cross the module boundary, the DOM wrapper cache lives under `lambda/module/radiant`, and a tiny Lambda script imports `radiant` and mutates a real Radiant DOM node.
- The current wrapper representation is still `MAP_KIND_DOM`. This is intentional: the module now owns more policy, but the VMap/native-object representation switch is deferred until the host-object semantics are specified and tested.
- Module-owned JS DOM reads currently cover the safe first cluster: element `tagName`, `nodeName`, `localName`, `namespaceURI`, `prefix`, `id`, `className`, `nodeType`; text `data`, `nodeValue`, `textContent`, `length`, `nodeType`, `nodeName`; comment `data`, `nodeValue`, `textContent`, `length`, `nodeType`, `nodeName`.
- Narrow navigation and snapshot collection reads have also moved into the module bridge: `ownerDocument`, `parentNode`, `parentElement`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, `childNodes`, `isConnected`, `attributes`, and the element-only `childElementCount`, `length`, `children`, `content` for the current `<template>` shim, `firstElementChild`, `lastElementChild`, `nextElementSibling`, and `previousElementSibling`.
- Module-owned JS DOM setters now cover writable element `id`, `className`, reflected string/integer element IDL writes, simple boolean reflected writes, `disabled` with its focus-clear invariant, `contentEditable`, editing hint writes for `inputMode` and `enterKeyHint`, and text-node `data` / `nodeValue` / `textContent`, using narrow JS hooks where existing mutation bookkeeping remains authoritative. Module-owned method dispatch also covers the narrow attribute mutation cluster `setAttribute`, `removeAttribute`, and `toggleAttribute`; the CharacterData methods `replaceData`, `insertData`, `appendData`, `deleteData`, and `substringData`; selector/descendant lookup methods `getElementsByTagName`, `getElementsByClassName`, `querySelector`, `querySelectorAll`, `matches`, and `closest`; tree-inspection methods `compareDocumentPosition` and element-scoped `getElementById`; structural methods `appendChild`, `removeChild`, `insertBefore`, `remove`, `normalize`, `cloneNode`, `replaceChild`, `replaceWith`, `insertAdjacentElement`, `insertAdjacentHTML`, `append`, and `prepend`; and reflected/read-normalized attribute clusters covering common string, boolean, integer, `contentEditable` / `isContentEditable`, `inputMode`, and `enterKeyHint` element IDL reads.
- Remaining JS fallback areas are deliberate: style/CSSOM, Range/Selection, layout methods, event/focus/click methods, true live collection wrappers, layout geometry, select/default boolean reflected setters, broader live element/form setters, and broader document proxy/window cases.
- Latest gate: `make build`, direct `dom_module_props` expected-output diff, filtered `dom_module_props` gtest, `git diff --check`, the Lambda `radiant_poc` sample, and full `./test/test_js_gtest.exe` passed `305/305`.

Implementation log:

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
- 2026-07-06: Phase 2 narrow navigation reads moved. `radiant_dom_bridge.cpp` now owns script-visible `parentNode`, `parentElement`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, and `childNodes` reads for element/text/comment DOM wrappers, including the pseudo-node and anonymous-table-wrapper visibility rules previously local to `js_dom.cpp`. Setters, methods, style/CSSOM, document proxy cases, live collections, Range/Selection, and geometry stay on fallback.
- 2026-07-06 navigation verification: `make build` passed. `test/js/dom_module_props` now covers the moved navigation cluster and matched its expected output; filtered gtest `JavaScriptTests/JsFileTest.Run/dom_module_props` passed; identity/style/mutation JS smokes passed; `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"`.
- 2026-07-06: Phase 2 writable `id` / `className` moved. `radiant_dom_set_property()` now handles those two element setters in module-owned code, delegates cache maintenance to `dom_element_set_attribute()`, and publishes attribute mutations through a narrow `js_dom_notify_mutation()` hook so existing reflow/mutation bookkeeping remains authoritative. Other setters remain on fallback.
- 2026-07-06 setter verification: `make build` passed. `test/js/dom_module_props` now covers property writes, `getAttribute()` reflection, and `getElementById()` cache visibility; direct expected-output diff and filtered gtest passed. Identity/style/mutation JS smokes passed, `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 element-navigation reads extended. `radiant_dom_bridge.cpp` now owns `isConnected`, `childElementCount`, `firstElementChild`, `lastElementChild`, `nextElementSibling`, and `previousElementSibling`, reusing the module-owned script-visible traversal helpers. Live `children` arrays and document proxy reads still remain on fallback.
- 2026-07-06 element-navigation verification: `make build` passed. `test/js/dom_module_props` covers connected/detached nodes and element-only sibling/child reads; direct expected-output diff and filtered gtest passed. Identity/style/mutation JS smokes passed, `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 snapshot child collection reads moved. `radiant_dom_bridge.cpp` now owns element `children` and `length` reads using the same script-visible traversal rules as `childElementCount`. This preserves the current snapshot array behavior; true live collection wrappers remain deferred to the native-object/spec phase.
- 2026-07-06 snapshot child collection verification: `make build` passed. `test/js/dom_module_props` now covers `children`, `length`, and post-append ordering; direct expected-output diff and filtered gtest passed. Identity/style/mutation JS smokes passed, `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 simple method dispatch moved. Public `js_dom_element_method()` now routes through `radiant_dom_element_method()`, and the module owns the read-only inspection methods `getAttribute`, `hasAttribute`, `getAttributeNames`, `contains`, and `hasChildNodes` before falling back to the preserved JS implementation for mutation, selector, style, layout, form, and editing methods.
- 2026-07-06 simple method verification: `make build` passed. `test/js/dom_module_props` now covers the module-owned inspection methods and internal-attribute filtering; direct expected-output diff and filtered gtest passed. Identity/style/mutation JS smokes passed, `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 snapshot `attributes` read moved. `radiant_dom_bridge.cpp` now owns the NamedNodeMap-like `attributes` snapshot array, including `{name, value}` pair construction and internal `__lambda_*` attribute filtering.
- 2026-07-06 attributes verification: `make build` passed. `test/js/dom_module_props` covers snapshot length, id/class pair visibility, and internal-attribute hiding; direct expected-output diff and filtered gtest passed. Identity/style/mutation JS smokes passed, `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 owner-document and template-content reads moved. `radiant_dom_bridge.cpp` now owns `ownerDocument` for element/text/comment wrappers through a narrow JS helper that preserves cached foreign-document wrapper identity, and owns the current `<template>.content` compatibility shim.
- 2026-07-06 owner-document/template verification: `make build` passed. `test/js/dom_module_props` now covers element/text/comment `ownerDocument` identity and detached template `.content` identity/child traversal; direct run, filtered gtest, identity/style/mutation smokes, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 attribute mutation methods moved. `radiant_dom_bridge.cpp` now owns `setAttribute`, `removeAttribute`, and `toggleAttribute`, while narrow JS hooks preserve existing event-handler expando compilation, select reset behavior, selectedOptions cache refreshes, and mutation notifications.
- 2026-07-06 attribute mutation verification: `make build` passed. `test/js/dom_module_props` now covers string coercion, set/remove/toggle return values, `hasAttribute` reflection, and internal attribute hiding; direct expected-output diff, filtered gtest, identity/style/mutation smokes, `dom_v12b`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 CharacterData mutation dispatch moved. `radiant_dom_bridge.cpp` now owns text-node `data` / `nodeValue` / `textContent` setters and the CharacterData methods `replaceData`, `insertData`, `appendData`, `deleteData`, and `substringData`; narrow JS helpers preserve live-range offset updates, DOMException behavior, string coercion, and mutation publication.
- 2026-07-06 CharacterData verification: `make build` passed. `test/js/dom_module_props` now covers text data writes and CharacterData method mutations; direct expected-output diff, filtered gtest, identity/style/mutation smokes, `dom_v12b`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 selector/descendant lookup methods moved. `radiant_dom_bridge.cpp` now owns `getElementsByTagName`, `getElementsByClassName`, `querySelector`, `querySelectorAll`, `matches`, and `closest`, using module-local descendant walkers plus the existing CSS parser and selector matcher on the owning document pool.
- 2026-07-06 selector lookup verification: `make build` passed. `test/js/dom_module_props` now covers tag/class lookup and selector matching; direct expected-output diff, filtered gtest, identity/style/mutation smokes, `dom_v12b`, `dom_jquery_lib`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 tree-inspection methods moved. `radiant_dom_bridge.cpp` now owns `compareDocumentPosition` for element/text/comment wrappers and element-scoped `getElementById`, preserving the existing bitmask behavior and recursive id lookup.
- 2026-07-06 tree-inspection verification: `make build` passed. `test/js/dom_module_props` now covers equality, contains/contained-by, following/preceding, and subtree id lookup; direct expected-output diff, filtered gtest, identity/style/mutation smokes, `dom_v12b`, `dom_jquery_lib`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 structural mutation first cluster moved. `radiant_dom_bridge.cpp` now owns `appendChild`, `removeChild`, and `insertBefore` dispatch; narrow JS helpers preserve live-range pre-remove/post-insert bookkeeping, select reset and selectedOptions refresh behavior, document-fragment move semantics, iframe load scheduling, and mutation publication.
- 2026-07-06 structural mutation verification: `make build` passed. `test/js/dom_module_props` now covers return identity, insertion order, detached parent reflection, and post-remove child snapshots; direct expected-output diff, filtered gtest, identity/style/mutation smokes, `dom_v12b`, `dom_jquery_lib`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 remaining structural methods moved. `radiant_dom_bridge.cpp` now owns `remove`, `normalize`, `cloneNode`, `replaceChild`, `replaceWith`, `insertAdjacentElement`, `insertAdjacentHTML`, `append`, and `prepend` dispatch; narrow JS helpers preserve live-range updates, clone attribute/expando copy behavior, HTML-fragment parsing, select option reset behavior, and mutation publication.
- 2026-07-06 remaining structural verification: `make build` passed. `test/js/dom_module_props` now covers replacement order, adjacent element/HTML insertion, variadic text insertion, text normalization, deep clone metadata, and self-removal; direct expected-output diff, filtered gtest, mutation/`dom_v12b`/jQuery/identity/style smokes, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 reflected read cluster moved. `radiant_dom_bridge.cpp` now owns common reflected attribute reads (`src`, `href`, `alt`, form/button string mappings, boolean attributes such as `required`/`disabled`/`open`, and integer attributes such as `maxLength`, `rows`, `cols`, and `size`) while leaving live form-control values and focus/select dirty-state paths on JS fallback.
- 2026-07-06 reflected-read verification: `make build` passed. `test/js/dom_module_props` covers the moved string/boolean/integer reflected reads; direct run, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, Lambda `radiant_poc`, `git diff --check`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 reflected string/integer setters moved. `radiant_dom_bridge.cpp` now owns common reflected string writes such as `href`, `src`, `alt`, `placeholder`, `wrap`, and `formTarget`, plus integer writes such as `maxLength`, `rows`, `cols`, and `size`. Boolean reflections and live value/selection setters remain on fallback because they carry focus, selectedness, and dirty-flag side effects.
- 2026-07-06 reflected setter verification: `make build` passed. `test/js/dom_module_props` covers property-write-to-attribute reflection and readback; direct run, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 editing/input hint reads moved. `radiant_dom_bridge.cpp` now owns canonical `inputMode` and `enterKeyHint` reads, `contentEditable` normalization, and inherited `isContentEditable` computation. The bridge also owns `inputMode` / `enterKeyHint` setters.
- 2026-07-06 editing/input hint verification: `make build` passed. `test/js/dom_module_props` covers canonical hint reads, hint setter attribute reflection, and contenteditable inheritance; direct run, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 simple boolean reflected setters moved. `radiant_dom_bridge.cpp` now owns side-effect-free boolean IDL writes (`required`, input `multiple`, `readOnly`, `noValidate`, `formNoValidate`, `open`, and global `autofocus`) with ToBoolean set/remove semantics. Live-state boolean setters such as `disabled`, select `multiple`, `defaultChecked`, and `defaultSelected` remain on fallback until their focus/select/dirty-state invariants move with them.
- 2026-07-06 simple boolean setter verification: `make build` passed. `test/js/dom_module_props` covers truthy/falsy boolean property writes and attribute reflection; direct run, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 `disabled` setter moved. `radiant_dom_bridge.cpp` now owns `disabled` IDL writes for form controls and uses a narrow `js_dom_after_disabled_attribute_set()` hook so the existing focus-clearing invariant remains JS-authoritative when disabling a focused subtree. Select `multiple`, `defaultChecked`, and `defaultSelected` stay on fallback for their select/dirty-state side effects.
- 2026-07-06 disabled setter verification: `make build` passed. `test/js/dom_module_props` covers `disabled` set/remove attribute reflection; direct run, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 `contentEditable` setter moved. `radiant_dom_bridge.cpp` now owns valid setter normalization (`true`, `false`, `plaintext-only`, `inherit`, boolean values, and empty-string removal) while delegating the invalid-value `SyntaxError` throw through a narrow JS hook.
- 2026-07-06 contentEditable setter verification: `make build` passed. `test/js/dom_module_props` covers normalized string, boolean, inherit, and empty-string setter behavior; direct run, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.

POC 1A deliverable status:

1. Done: add a statically linked `radiant` Jube module.
2. In progress: move DOM property and method resolution tables into module-owned code. The safe clusters now include basic identity/value reads, narrow navigation reads, owner-document reads, snapshot child-list/attribute reads, the current template content shim, read-only inspection methods, narrow attribute mutation methods, CharacterData mutation methods, selector/descendant lookup methods, tree-inspection methods, structural methods, common reflected attribute reads, reflected string/integer setters, simple boolean reflected setters, the `disabled` setter, `contentEditable` setter, and editing/input hint reads plus `inputMode` / `enterKeyHint` setters; layout/style/event/focus/form/document-window paths remain on JS fallback.
3. Done for checkpoint: keep existing `MAP_KIND_DOM` wrappers, but make their get/set and wrapper factory surface delegate through the module-owned bridge.
4. Done: add the host/module DOM wrapper cache required by the native-module design, without scoping it per `DomDocument`.
5. Done: add one tiny Lambda-facing sample early so the POC proves cross-front-end access, not only JS compatibility.

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

- `lambda/js/js_dom.cpp`: compatibility wrapper constructor, fallback property/method dispatch, DOM mutation helpers.
- `lambda/module/radiant/radiant_dom_bridge.cpp`: module-owned JS DOM wrapper cache, wrapper entry points, simple DOM read dispatch, and delegation to preserved JS fallback.
- `lambda/module/radiant/radiant_module.cpp`: Lambda-facing `radiant` functions over real Radiant DOM wrappers.
- `lambda/jube/jube.h` and `lambda/jube/jube_registry.cpp`: static Jube registry and module descriptor scaffolding.
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

Current implementation:

- The cache lives in `lambda/module/radiant/radiant_dom_bridge.cpp`.
- `js_dom_wrap_element()`, `js_dom_unwrap_element()`, and `js_is_dom_node()` delegate through `radiant_dom_wrap_node()`, `radiant_dom_unwrap_node()`, and `radiant_dom_is_node()`.
- Cache entries root wrapper `Item`s and store an owner `DomDocument*` for teardown invalidation.
- `free_document()` calls `radiant_dom_invalidate_document()` before the document arena is destroyed.
- On cache miss, the bridge still calls `js_dom_create_wrapper_impl()` to create the current `MAP_KIND_DOM` compatibility carrier.

## 6. Phased Implementation

### Phase 0: Baseline and Guardrails

Purpose: capture current behavior before moving dispatch.

Status: complete for the current checkpoint.

Tasks:

- Run the smallest relevant JS DOM / Radiant baseline available in the current tree.
- Record the command and result in this doc when implementation starts.
- Confirm `make build` is green before touching module code.
- Identify the minimal DOM property cluster for the first dispatch table:
  - node identity and tree navigation: `nodeType`, `nodeName`, `parentNode`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, `childNodes`
  - text: `data`, `nodeValue`, `textContent`, `length`
  - element basics: `tagName`, `id`, `className`, `style`, attributes

Acceptance:

- Complete: baseline command and result were recorded in the implementation log.
- Complete: later phase gates kept the compatibility checkpoint green.

### Phase 1: Static `radiant` Module Skeleton

Purpose: create the module boundary without changing behavior.

Status: complete.

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

- Complete: `make build` passed.
- Complete: no JS DOM behavior moved in this phase.
- Complete: `radiant` is statically registered with Jube descriptors.

### Phase 2: Module-Owned DOM Dispatch Tables

Purpose: move DOM policy into the module while preserving wrappers.

Status:

- Started 2026-07-06 with the compatibility bridge: public JS DOM get/set entry points route through `radiant_dom_get_property()` / `radiant_dom_set_property()`.
- First read cluster moved 2026-07-06. The module now owns simple identity/name/value reads for elements, text nodes, and comment nodes, including element `className`, plus narrow node and element navigation reads, `ownerDocument`, snapshot child/attribute collection reads, the current template `content` shim, read-only inspection methods, narrow attribute mutation methods, CharacterData mutation methods, selector/descendant lookup methods, tree-inspection methods, structural methods, common reflected attribute reads, editing/input hint reads, reflected string/integer setters, simple boolean reflected setters, the `disabled` setter, and the `contentEditable` setter.
- Writable element `id` and `className` moved 2026-07-06. The module uses `dom_element_set_attribute()` for native cache maintenance and a narrow JS mutation hook for the existing mutation ledger.
- Writable text-node `data`, `nodeValue`, and `textContent` moved 2026-07-06. The module dispatches them through a narrow JS helper that preserves the existing live range and mutation-publication invariants.
- Added `test/js/dom_module_props.js` / `.html` / `.txt` to pin the moved clusters through the normal JS DOM harness.
- Current verification: direct `dom_module_props`, filtered gtest `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full JS gtest passed.
- Remaining Phase 2 work: layout/style methods, event/focus/click methods, style/CSSOM, broader document proxy/window cases, true live collection wrappers, Range/Selection, geometry, select/default boolean reflected setters, and broader live element/form setters remain on JS fallback.

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

- Complete for first cluster: existing `MAP_KIND_DOM` property get/set still works.
- Complete for first cluster: moved reads have no behavior delta in targeted JS DOM tests.
- Complete: `make build` passed.
- In progress overall: writable properties, navigation, methods, style/CSSOM, collections, Range/Selection, and geometry remain to migrate.

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

- Complete for checkpoint: JS identity tests still pass.
- Complete for document teardown: document invalidation unroots matching cached wrappers before arena destruction.
- Complete: `make build` passed.
- Remaining lifecycle gap: finer-grained subtree/node invalidation if future Radiant code frees individual nodes before document teardown.

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

- Complete: a real Lambda script imports `radiant`.
- Complete: it reads and mutates a real Radiant DOM node.
- Complete: `test/lambda/radiant_poc.ls` has the matching expected `test/lambda/radiant_poc.txt`.

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

- Done: `import radiant` is the planned script surface and the static module is registered.
- Done: `MAP_KIND_DOM` get/set delegates to module-owned DOM dispatch for at least the first property cluster.
- Done: wrapper cache ownership has moved behind module/host APIs, still returning current map wrappers.
- Done: one tiny Lambda sample proves real DOM read/write access.
- Done: `make build` passes.
- Done: focused JS DOM/Radiant regression gates have no behavior delta.
- Done: full `./test/test_js_gtest.exe` passed `305/305` at the current checkpoint.

The POC 1A deliverable is therefore complete as a compatibility checkpoint. The next work is POC 1B: finish the dependency cleanup that lets setters/navigation move, then write the VMap readiness spec before changing wrapper representation.

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
