# Lambda Jube DOM Implementation Plan

> Status: POC 1 implementation progress record, updated 2026-07-08.
> Parent design: [Lambda_Native_Module_Design.md](./Lambda_Native_Module_Design.md).
> Working module name: `radiant`, imported as `import radiant`.

## 1. Goal

Build the first visible Jube native-module POC by moving Radiant DOM access behind a module-owned interface while preserving current JS DOM behavior.

Current checkpoint:

- POC 1A is now implemented as a compatibility checkpoint. `radiant` is statically registered, JS DOM get/set/wrapper entry points cross the module boundary, the DOM wrapper cache lives under `lambda/module/radiant`, and a tiny Lambda script imports `radiant` and mutates a real Radiant DOM node.
- DOM node wrappers now use branded, non-owning VMaps. The stale `MAP_KIND_DOM` node shell has been removed; the remaining map-backed DOM-adjacent objects use the narrower `MAP_KIND_WEB_API_RESOURCE` carrier for Range, Selection, inline style, and computed style resources.
- Module-owned JS DOM reads currently cover the safe first cluster: element `tagName`, `nodeName`, `localName`, `namespaceURI`, `prefix`, `id`, `className`, `nodeType`; text `data`, `nodeValue`, `textContent`, `length`, `nodeType`, `nodeName`; comment `data`, `nodeValue`, `textContent`, `length`, `nodeType`, `nodeName`.
- Narrow navigation and collection reads have also moved into the module bridge: `ownerDocument`, `parentNode`, `parentElement`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, live element `childNodes` / `children`, `isConnected`, `attributes`, and the element-only `childElementCount`, `length`, `content` for the current `<template>` shim, `firstElementChild`, `lastElementChild`, `nextElementSibling`, and `previousElementSibling`, plus live `form.elements`, `form.length`, and `form[name/id]`, and live select/option reads for `select[i]`, `select.options`, `select.length`, `select.selectedOptions`, `select.selectedIndex`, `select.value`, `select.type`, and option `value`, `text`, `label`, `selected`, `index`, and `form`.
- Deterministic document proxy reads now live in the module bridge too: `documentElement`, `body`, `head`, `title`, URL/location component reads, `readyState`, `compatMode`, `characterSet`, `charset`, `contentType`, `nodeType`, `nodeName`, `ownerDocument`, document `childNodes`, `doctype`, `fonts`, `styleSheets`, `defaultView`, `implementation`, `designMode`, `activeElement`, foreign-document/window proxy aliases (`defaultView`, `document`, `window`, `self`, and `getSelection`), and live `document.forms` with named access.
- Module-owned JS DOM setters now cover writable element `id`, `className`, reflected string/integer element IDL writes, simple boolean reflected writes, `disabled` with its focus-clear invariant, select/default boolean reflected writes (`select.multiple`, `input.defaultChecked`, and `option.defaultSelected`) with their dirty-state/reset invariants, live selection setters (`input.checked`, `select.value`, `select.selectedIndex`, `select.length`, `option.selected`, and `option.text`), `option.value`, non-text `input.value`, text-control setters (`value`, `defaultValue`, `selectionStart`, `selectionEnd`, and `selectionDirection`), `contentEditable`, editing hint writes for `inputMode`, `enterKeyHint`, `autocapitalize`, `autocorrect`, `spellcheck`, and `writingSuggestions`, `iframe.srcdoc` with iframe load scheduling, foreign-document proxy property writes under active-document swap, and text-node `data` / `nodeValue` / `textContent`, using narrow JS hooks where existing mutation bookkeeping remains authoritative. Module-owned method dispatch also covers the narrow attribute mutation cluster `setAttribute`, `removeAttribute`, and `toggleAttribute`; the CharacterData methods `replaceData`, `insertData`, `appendData`, `deleteData`, and `substringData`; element selector/descendant lookup methods `getElementsByTagName`, `getElementsByClassName`, `querySelector`, `querySelectorAll`, `matches`, and `closest`; document lookup/selector methods `getElementById`, `getElementsByTagName`, `getElementsByClassName`, `getElementsByName`, `querySelector`, and `querySelectorAll`; document factory methods `createElement`, `createElementNS`, `createTextNode`, `createDocumentFragment`, `createComment`, `createProcessingInstruction`, and `importNode`; document structural methods `normalize`, `adoptNode`, and `appendChild`; document lifecycle/location methods `assign`, `replace`, `reload`, `focus`, `blur`, `open`, `close`, `write`, and `writeln`; document hit-test/legacy command methods `elementFromPoint`, `execCommand`, and `queryCommand*`; document Range/Selection entry methods `createRange` and `getSelection`; Range/Selection wrapper get/set dispatch; editor geometry helper methods `__lambdaBoundaryFromPoint`, `__lambdaTextControlCaretBounds`, and `__lambdaTextControlBoundaryFromPoint`; tree-inspection methods `compareDocumentPosition` and element-scoped `getElementById`; structural methods `appendChild`, `removeChild`, `insertBefore`, `remove`, `normalize`, `cloneNode`, `replaceChild`, `replaceWith`, `insertAdjacentElement`, `insertAdjacentHTML`, `append`, and `prepend`; element, document, and window EventTarget methods `addEventListener`, `removeEventListener`, and `dispatchEvent`; inline style methods `setProperty` and `removeProperty`; element geometry/scroll methods `getBoundingClientRect`, `getClientRects`, `scrollIntoView`, `scroll`, `scrollTo`, and `scrollBy`; text-control methods `setSelectionRange`, `setRangeText`, and `select`; focus/click methods `focus`, `blur`, and `click`; select option-list methods `namedItem`, `add`, and `remove`; foreign-document/window proxy methods under active-document swap plus window-global fallback; form methods `submit`, `requestSubmit`, `reset`, `checkValidity`, and `reportValidity`; and reflected/read-normalized attribute clusters covering common string, boolean, integer, `contentEditable` / `isContentEditable`, `inputMode`, and `enterKeyHint` element IDL reads.
- Remaining JS fallback areas are deliberate: deeper Range/Selection native-object representation cleanup and MIR lowering shim deletion are deferred until those carriers are migrated to native descriptors too.
- Phase 7 has removed DOM-node `MAP_KIND_DOM` creation/unwrap/runtime dispatch. Jube native type metadata records ownership and host-object hooks; the JS DOM adapter has executable coverage for projected-vs-expando lookup, `in`, `delete`, descriptors, own-key enumeration, prototype lookup/patching, `instanceof`, and Range/Selection expando parity.
- Latest gate: `make build`, direct `dom_module_props` expected-output diff, filtered `dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context` gtest, `./lambda.exe --no-log test/lambda/radiant_poc.ls`, full `./test/test_js_gtest.exe` (`305/305`), and full `./test/test_ui_automation_gtest.exe` (`236` passed, `2` skipped) passed.

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
- 2026-07-06: Phase 2 select/default boolean reflected setters moved. `radiant_dom_bridge.cpp` now owns `select.multiple`, `input.defaultChecked`, and `option.defaultSelected` writes while narrow JS hooks preserve checkedness dirty-flag syncing, option selectedness dirty-flag syncing, and select reset behavior when `multiple` is removed.
- 2026-07-06 select/default boolean setter verification: `make build` passed. `test/js/dom_module_props` covers attribute reflection, default readback, option selectedness, and select `multiple` set/remove behavior; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 live selection setters moved. `radiant_dom_bridge.cpp` now dispatches `input.checked`, `select.value`, `select.selectedIndex`, and `option.selected` while narrow JS hooks preserve checkedness dirty flags, select dirty state, option selectedness dirty flags, non-multiple select reset behavior, and selectedOptions cache refreshes.
- 2026-07-06 live selection setter verification: `make build` passed. `test/js/dom_module_props` covers select value transitions, selectedIndex transitions, explicit option selectedness, and checked/defaultChecked behavior; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 hint/spelling/srcdoc setters moved. `radiant_dom_bridge.cpp` now owns `autocapitalize`, `autocorrect`, `spellcheck`, `writingSuggestions`, and `iframe.srcdoc` writes while a narrow JS hook preserves iframe load scheduling after `srcdoc` changes.
- 2026-07-06 hint/spelling/srcdoc setter verification: `make build` passed. `test/js/dom_module_props` covers raw attribute writes and public readback normalization for the moved setters; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-06: Phase 2 simple value setters moved. `radiant_dom_bridge.cpp` now owns `option.value` and non-text `input.value` writes.
- 2026-07-06 simple value setter verification: `make build` passed. `test/js/dom_module_props` covers attribute writes and public readback for `option.value` and checkbox `value`; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-06: Phase 2 option subtree setters moved. `radiant_dom_bridge.cpp` now dispatches `select.length` and `option.text`, while narrow JS hooks preserve option creation/removal, child replacement, and mutation ledger behavior.
- 2026-07-06 option subtree setter verification: `make build` passed. `test/js/dom_module_props` covers option text replacement plus select length grow/shrink behavior; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-06: Phase 2 text-control setters moved. `radiant_dom_bridge.cpp` now dispatches `input`/`textarea` `value`, `defaultValue`, `selectionStart`, `selectionEnd`, and `selectionDirection`; narrow JS hooks preserve dirty-value flags, CR/LF normalization, textarea child replacement, and selection mirror updates.
- 2026-07-06 text-control setter verification: `make build` passed. `test/js/dom_module_props` covers live value normalization, dirty `defaultValue` behavior, selection setters, and textarea default propagation; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-06: Phase 2 text-control method dispatch moved. `radiant_dom_bridge.cpp` now dispatches `setSelectionRange`, `setRangeText`, and `select`; narrow JS hooks preserve range replacement, dirty-value marking, focus, and selection mirror behavior.
- 2026-07-06 text-control method verification: `make build` passed. `test/js/dom_module_props` covers selection-range writes, range text replacement, and whole-control selection; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-06: Phase 2 focus/click method dispatch moved. `radiant_dom_bridge.cpp` now dispatches `focus`, `blur`, and `click`; narrow JS hooks preserve active-element/focus-event behavior and disabled form-control click suppression.
- 2026-07-06 focus/click method verification: `make build` passed. `test/js/dom_module_props` covers detached focus no-op behavior, blur no-op behavior, click dispatch, and disabled click suppression; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-06: Phase 2 element EventTarget method dispatch moved. `radiant_dom_bridge.cpp` now dispatches `addEventListener`, `removeEventListener`, and `dispatchEvent`; narrow JS hooks preserve listener identity and the existing dispatch/bubbling implementation.
- 2026-07-06 element EventTarget verification: `make build` passed. `test/js/dom_module_props` covers element listener registration, direct dispatch, listener removal, click dispatch, and disabled click suppression; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-06: Phase 2 element geometry/scroll method dispatch moved. `radiant_dom_bridge.cpp` now dispatches `getBoundingClientRect`, `getClientRects`, `scrollIntoView`, `scroll`, `scrollTo`, and `scrollBy`; narrow JS hooks preserve layout flush behavior, pending scroll state, and the existing rect object shapes.
- 2026-07-06 element geometry/scroll verification: `make build` passed. `test/js/dom_module_props` covers rect shape, client rect collection shape, scroll position updates, clamping, relative scroll, object-argument scroll, and `scrollIntoView` callability; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-06: Phase 2 document EventTarget method dispatch moved. `radiant_dom_bridge.cpp` now dispatches document `addEventListener`, `removeEventListener`, and `dispatchEvent`; narrow JS hooks preserve singleton document wrapper identity and the existing listener dispatch implementation.
- 2026-07-06 document EventTarget verification: `make build` passed. `test/js/dom_module_props` covers document listener registration, direct dispatch, and listener removal; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-06: Phase 2 window EventTarget method dispatch moved. `radiant_dom_bridge.cpp` now owns `window`/bare global `addEventListener`, `removeEventListener`, and `dispatchEvent`; narrow JS hooks preserve the canonical global-object listener key.
- 2026-07-06 window EventTarget verification: `make build` passed. `test/js/dom_module_props` covers bare global listener registration, `window.dispatchEvent`, `window.removeEventListener`, and bare global dispatch after removal; direct expected-output diff, filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 inline style method dispatch moved. `radiant_dom_bridge.cpp` now dispatches `style.setProperty` and `style.removeProperty` for element inline styles; narrow JS hooks preserve CSS parsing, mutation classification, invalidation, and old-value serialization. CSSOM rule style declarations still fall through to the CSSOM fallback.
- 2026-07-07 inline style method verification: `make build` passed. `test/js/dom_module_props` covers inline style set/remove behavior and old-value return; direct `dom_module_props` and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_v12b`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 CSSOM method dispatch moved. `radiant_dom_bridge.cpp` now dispatches `CSS.supports`, `CSS.escape`, `CSSStyleSheet.insertRule`, `CSSStyleSheet.deleteRule`, and rule `CSSStyleDeclaration` methods while the existing CSSOM implementation remains authoritative for parsing, serialization, and rule mutation.
- 2026-07-07 CSSOM method verification: `make build` passed. `test/js/dom_module_props` covers CSS namespace methods, stylesheet insert/delete, and rule style get/set/remove behavior; direct `dom_module_props` and `css_namespace` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `css_namespace`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 document lookup/selector dispatch moved. `radiant_dom_bridge.cpp` now dispatches document `getElementById`, `getElementsByTagName`, `getElementsByClassName`, `getElementsByName`, `querySelector`, and `querySelectorAll`, preserving the existing foreign-document active-document swap before entering the module bridge and using a handled/out contract so valid null lookup misses do not fall through to the old dispatcher.
- 2026-07-07 document lookup verification: `make build` passed. `test/js/dom_module_props` covers document tag/class/name lookup plus selector match/miss behavior; direct `dom_module_props`, `dom_basic`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_v12b`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 document factory dispatch moved. `radiant_dom_bridge.cpp` now dispatches document `createElement`, `createElementNS`, `createTextNode`, `createDocumentFragment`, `createComment`, `createProcessingInstruction`, and `importNode`, using the same MarkBuilder/arena construction path and module-owned `cloneNode` path for import. `js_document_method()` also checks the module dispatcher directly because MIR lowers optimized `document.method()` calls to that entry point instead of the proxy wrapper.
- 2026-07-07 document factory verification: `make build` passed. `test/js/dom_module_props` covers namespace element creation, detached text/comment/fragment nodes, processing instructions, and deep `importNode`; direct `dom_module_props`, `dom_basic`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_v12b`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 document structural dispatch moved. `radiant_dom_bridge.cpp` now dispatches document `normalize`, `adoptNode`, and `appendChild`; `adoptNode` uses a narrow JS hook so removals still flow through live-range, focus, select, and mutation bookkeeping, while document `appendChild` reuses the existing element append bridge when a document root already exists.
- 2026-07-07 document structural verification: `make build` passed. `test/js/dom_module_props` covers document append/adopt identity, parent reflection, text-node append, and `document.normalize`; direct `dom_module_props`, `dom_basic`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 document lifecycle/location dispatch moved. `radiant_dom_bridge.cpp` now dispatches document/location `assign`, `replace`, `reload`, document `focus`, `blur`, `open`, `close`, `write`, and `writeln`; narrow JS hooks preserve pending navigation state, selection clearing, body clearing, and write insertion semantics.
- 2026-07-07 document lifecycle/location verification: `make build` passed after allowing the grammar tool fetch that sandboxed DNS blocked. `test/js/dom_module_props` covers location method return shape, document write/open/close body behavior, and post-open writes; direct `dom_module_props`, `dom_basic`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 document proxy read dispatch moved. `js_document_proxy_get_property()` now asks `radiant_dom_document_get_property()` first, and the module owns deterministic document reads (`documentElement`, `body`, `head`, `title`, URL/location components, document identity metadata, document `childNodes`, and `doctype`) while narrow JS hooks preserve canonical document/foreign-document proxy identity and synthesized document-stub construction.
- 2026-07-07 document proxy read verification: `make build` passed. `test/js/dom_module_props` covers document tree roots, title, location identity, URL equality, metadata, childNodes, and doctype; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 document form collection read moved. `radiant_dom_bridge.cpp` now owns `document.forms`, including document-order form collection plus `forms[name]` / `forms[id]` named access with the existing duplicate name/id rule.
- 2026-07-07 document forms verification: `make build` passed. `test/js/dom_module_props` covers inserted forms, collection order, named access by `name`, named access by distinct `id`, and same-name/id de-duplication; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 document hit-test/legacy command dispatch moved. `radiant_dom_bridge.cpp` now owns dispatch for `document.elementFromPoint` through a narrow JS geometry hook, plus the fixed feature-detection command methods `execCommand`, `queryCommandSupported`, `queryCommandEnabled`, `queryCommandIndeterm`, `queryCommandState`, and `queryCommandValue`.
- 2026-07-07 document hit-test/legacy command verification: `make build` passed. `test/js/dom_module_props` covers `elementFromPoint` return shape and legacy command return values; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 Range/Selection dispatch moved. `radiant_dom_bridge.cpp` now owns `document.createRange`, `document.getSelection`, the callable `document.getSelection` property, and Range/Selection wrapper get/set dispatch while keeping the existing `js_dom_selection.cpp` behavior implementation authoritative.
- 2026-07-07 Range/Selection dispatch verification: `make build` passed. `test/js/dom_module_props` covers `createRange`, Range property reads, callable `document.getSelection`, Selection singleton identity, and Selection `rangeCount`; direct `dom_module_props`, `dom_basic`, `dom_mutation`, `dom_v12b`, and `input_event_target_ranges_constructor` expected-output diffs passed; native `./test/test_dom_range_gtest.exe`, focused WPT selection `getSelection` / `getRangeAt` / `isCollapsed` / `removeAllRanges` / `type`, filtered JS gtest over `dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `input_event_target_ranges_constructor` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305. Full WPT selection remains a non-green broader conformance gate in this tree (`82` passed, `62` skipped, `15` failed) and was not used as this slice's acceptance gate.
- 2026-07-07: Phase 2 form-control collection reads moved. `radiant_dom_bridge.cpp` now owns `HTMLFormElement.elements`, `form.length`, and the form named getter `form[name/id]`, preserving listed-control filtering, descendant order, `input type=image` exclusion, and array return for duplicate control names.
- 2026-07-07 form-control collection verification: `make build` passed. `test/js/dom_module_props` covers listed-control collection length/order, `form.length`, name/id lookup, duplicate-name lookup, and image-input exclusion; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 form method dispatch moved. `radiant_dom_bridge.cpp` now owns dispatch for `form.reset`, `form.checkValidity`, and `form.reportValidity`; `js_dom.cpp` exposes narrow hooks so reset event cancellation, text-control reset, validity-state computation, and invalid event dispatch remain behavior-identical.
- 2026-07-07 form method verification: `make build` passed. `test/js/dom_module_props` covers form invalid-event dispatch, `checkValidity` / `reportValidity` return values, cancelable trusted reset event dispatch, reset cancellation, and text-control reset; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 editor geometry helper dispatch moved. `radiant_dom_bridge.cpp` now owns method dispatch for `__lambdaBoundaryFromPoint`, `__lambdaTextControlCaretBounds`, and `__lambdaTextControlBoundaryFromPoint`; `js_dom.cpp` keeps the UI-context/layout hit-test implementations behind narrow hooks.
- 2026-07-07: Phase 2 document proxy read cluster extended. `radiant_dom_bridge.cpp` now owns dispatch for document `fonts`, `styleSheets`, `defaultView`, `implementation`, `designMode`, and `activeElement`; `js_dom.cpp` exposes narrow hooks for JS-owned singleton/state behavior, and the `DOMImplementation` singleton now installs callable IDL methods so property reads such as `document.implementation.createHTMLDocument` match direct calls.
- 2026-07-07 document proxy read extension verification: `make build` passed. `test/js/dom_module_props` covers activeElement, implementation method property reads, font singleton identity, FontFaceSet ready shape, defaultView identity, and designMode read/write reflection; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 form submission method dispatch moved. `radiant_dom_bridge.cpp` now owns dispatch for `form.submit` and `form.requestSubmit`, preserving the current LambdaJS behavior where these methods are feature-detection-visible but side-effect-free until real FormData/navigation submission is implemented.
- 2026-07-07 form submission dispatch verification: `make build` passed. `test/js/dom_module_props` covers truthy method visibility, null call results, and no submit-event dispatch for the current no-op behavior; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-07: Phase 2 select/options dispatch moved. `radiant_dom_bridge.cpp` now claims HTMLSelectElement indexed/collection/value reads and HTMLOptionElement value/text/label/selected/index/form reads before generic element fallbacks, and dispatches select `namedItem`, `add`, and `remove` before generic node removal. `js_dom.cpp` also now orders the select `remove(index)` overload before `ChildNode.remove()` so delegated calls preserve option-list semantics.
- 2026-07-07 select/options dispatch verification: `make build` passed. `test/js/dom_module_props` covers `select[i]`, `options`, `selectedOptions`, `selectedIndex`, `namedItem`, `add`, `remove(index)`, and no-arg `select.remove()` preserving select attachment; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-08: Phase 2 foreign-document/window proxy dispatch moved. `radiant_dom_bridge.cpp` now owns `MAP_KIND_FOREIGN_DOC` get/set/method routing: browsing-context aliases (`defaultView`, `document`, `window`, `self`, `getSelection`), active-document-swapped document proxy reads/writes/methods, and the window-global method fallback used by iframe `contentWindow`.
- 2026-07-08 foreign-document/window proxy verification: `make build` passed. `test/js/dom_module_props` covers `createHTMLDocument` null `defaultView`, foreign `ownerDocument`, foreign active-document-swapped `createElement` / `getElementById` / `querySelector`, foreign document expandos, iframe `contentDocument === contentWindow`, window alias reads, `getSelection`, srcdoc query, expando writes, and current null `getComputedStyle` fallback behavior; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-08: Phase 2 form submission behavior implemented. Module-owned `form.submit` now enters the existing headless navigation path while bypassing validation and the cancelable `submit` event per HTML; module-owned `form.requestSubmit` resolves and validates an optional submitter, runs constraint validation unless `novalidate` / `formnovalidate` applies, dispatches a trusted cancelable `submit` event with `event.submitter`, and then performs the existing GET FormData query serialization/navigation if not canceled. Submit-button click activation now shares the same request-submit path.
- 2026-07-08 form submission behavior verification: `make build` passed. `test/js/dom_module_props` covers `submit()` no-event behavior, `requestSubmit()` event dispatch and undefined return, invalid-form blocking, explicit submitter identity, and GET navigation/query serialization; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-08: Phase 2 live element child collections implemented. Element `children` and `childNodes` now return held Array-backed compatibility wrappers that refresh from JS DOM mutation notifications and before indexed/named reads; `children` keeps the current `HTMLCollection` decoration and `namedItem` behavior.
- 2026-07-08 live element child collection verification: `make build` passed. `test/js/dom_module_props` covers held `children` / `childNodes` collections across append, including live `length`, indexed identity, and `namedItem`; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-08: Phase 2 live form collections implemented. `document.forms` and `form.elements` now return held Array-backed compatibility wrappers that refresh from JS DOM mutation notifications and before indexed/named reads; dynamic named properties are tombstoned when a matching form/control is renamed or removed.
- 2026-07-08 live form collection verification: `make build` passed. `test/js/dom_module_props` covers held `document.forms` and `form.elements` across append, rename, and remove, including live `length`, indexed identity, new named access, and stale named-slot cleanup; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, filtered `dom_basic` + `dom_mutation` + `dom_v12b`, `git diff --check`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-08: Phase 2 live lookup collections implemented. Document `getElementsByTagName`, `getElementsByClassName`, and `getElementsByName`, plus element-scoped `getElementsByTagName` and `getElementsByClassName`, now return held Array-backed compatibility wrappers that refresh on DOM/attribute mutation and before indexed reads.
- 2026-07-08 live lookup collection verification: `make build` passed. `test/js/dom_module_props` covers held document tag/class/name collections and held element tag/class collections across append, attribute mutation, remove, live `length`, and indexed identity; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props`, filtered `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery`, `git diff --check`, and full `./test/test_js_gtest.exe` passed 305/305.
- 2026-07-08: Phase 2 live select/options collections implemented. Held `select.options` now refreshes on select structural mutation and before indexed reads, keeping dense slots plus decorated companion `length` / `selectedIndex` in sync; held `select.selectedOptions` also refreshes through the mutation hook.
- 2026-07-08 live select/options verification: `make build` passed. `test/js/dom_module_props` covers a held `select.options` collection across `add()` and `remove(index)`, including live `length`, indexed identity, and `selectedIndex`; focused `JavaScriptTests/JsFileTest.Run/dom_module_props`, filtered `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery`, `git diff --check`, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-08 Radiant baseline gate: `make test-radiant-baseline` was attempted after the live select/options migration. UI Automation passed (`236` passed, `2` skipped), Radiant View and headless page-load tests passed, but the overall target remained non-green due to visual/layout baseline gates: `wpt-css-inline` reported `initial-letter-raised-sunken-caps-raise`, Render Visual reported `form_buttons_01`, and the summary listed `9` render baseline regressions.
- 2026-07-08: Phase 5 VMap readiness started. The migration contract is now written down for native host objects before changing DOM wrappers away from `MAP_KIND_DOM`: vtable projection wins, wrapper expandos fill misses, prototype chain resolves last, DOM nodes use non-owning native wrappers, owned module resources keep finalizers, and JS host-object ops must grow `has`, `delete`, descriptor, enumeration, and prototype hooks. `test/js/dom_module_props` now pins basic DOM element expando reads and `in` visibility as the first executable readiness check.
- 2026-07-08: Phase 5 executable readiness completed for the current carrier. `JubeTypeDef` now has ownership flags plus host-object ops slots, the Radiant DOM bridge provides host `has`, `delete`, own descriptor, own-key, and prototype behavior for DOM wrappers, DOM element/Range/Selection expandos delete from their side-table stores instead of stale wrapper maps, projected host properties synthesize non-configurable descriptors, own keys enumerate projected names before expandos, and `Element` / `Range` / `Selection` prototype patching and `instanceof` behavior are pinned before the Phase 6 carrier switch.
- 2026-07-08 Phase 5 readiness verification: direct `dom_module_props` expected-output refresh matched, focused `JavaScriptTests/JsFileTest.Run/dom_module_props` passed, filtered `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context` passed, `git diff --check` passed, and full `./test/test_js_gtest.exe` passed `305/305`.
- 2026-07-08: Phase 6 DOM wrapper carrier switch completed. `radiant_dom_wrap_node()` now returns branded, non-owning DOM VMaps; DOM unwrap/identity/prototype/property/method dispatch paths accept the branded carrier; VMap values have pointer-sized map metadata and shaped map read/write/first-insert support so globals and live collection named properties can store DOM wrappers without corrupting or nulling them.
- 2026-07-08 Phase 6 verification: `make build` passed, direct `test/js/dom_module_props.js --document test/js/dom_module_props.html --no-log` matched `test/js/dom_module_props.txt`, focused `JavaScriptTests/JsFileTest.Run/dom_module_props` passed, and `./lambda.exe --no-log test/lambda/radiant_poc.ls` returned `"ok"`.
- 2026-07-08 Phase 6 broad Radiant gate: `make test-radiant-baseline` was attempted and remained non-green in existing broad baseline buckets. Summary: `6154` passed, `49` failed, `358` skipped. Failing buckets were Layout Baseline (`wpt-css-inline` regression), UI Automation (`197` passed, `39` failed), and Render Visual (`197/212` passed, `13` expected failures, `1` skipped, `9` baseline regressions including `form_buttons_01`).
- 2026-07-08 Phase 6 UI Automation fix: the VMap carrier switch initially skipped `on<type>` IDL/inline handlers because event dispatch only recognized MAP/ELEMENT targets for handler lookup. `fire_listeners()` now treats branded DOM VMaps as DOM targets for that lookup, and standalone `./test/test_ui_automation_gtest.exe` passed `236` tests with `2` skipped.
- 2026-07-08 Phase 7 DOM-node map shell deletion: `radiant_dom_wrap_node()` no longer falls back to a `MAP_KIND_DOM` wrapper on VMap allocation miss, the stale `js_dom_create_wrapper_impl()` constructor and DOM-node map marker were deleted, and runtime DOM node property/method/prototype dispatch is VMap-only. The former `MAP_KIND_DOM` enum value is now `MAP_KIND_WEB_API_RESOURCE` for the remaining non-node Range/Selection/style resources. Verification: `make build`, direct `dom_module_props` expected-output diff, filtered `dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `radiant_poc`, full `./test/test_js_gtest.exe` (`305/305`), and full `./test/test_ui_automation_gtest.exe` (`236` passed, `2` skipped) passed.
- 2026-07-07 editor geometry helper verification: `make build` passed. `test/js/dom_module_props` covers callability and the headless null-return path for the moved helper dispatch; direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs passed; filtered `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full `./test/test_js_gtest.exe` passed 305/305.

POC 1A deliverable status:

1. Done: add a statically linked `radiant` Jube module.
2. In progress: move DOM property and method resolution tables into module-owned code. The safe clusters now include basic identity/value reads, narrow navigation reads, owner-document reads, live element child collections, live form collections, live lookup collections, live select/options collections, snapshot attribute reads, the current template content shim, deterministic document proxy reads including the current `fonts`, `styleSheets`, `defaultView`, `implementation`, `designMode`, and `activeElement` cluster, foreign-document/window proxy routing, form-control named reads, select/option collection and value reads, read-only inspection methods, narrow attribute mutation methods, CharacterData mutation methods, element selector/descendant lookup methods, document lookup/selector methods, document factory methods, document structural methods, document lifecycle/location methods, document hit-test/legacy command methods, document Range/Selection entry methods and wrapper get/set dispatch, editor geometry helper methods, tree-inspection methods, structural methods, element/document/window EventTarget methods, inline style methods, CSSOM methods, element geometry/scroll methods, common reflected attribute reads, reflected string/integer setters, simple boolean reflected setters, the `disabled`, `contentEditable`, select/default boolean reflected setters, live selection setters, editing/input hint setters, hint/spelling setters, `iframe.srcdoc` setter, simple `value` setters, option subtree setters, text-control setters, text-control methods, focus/click methods, select option-list methods, and form reset/validation/submission methods; deeper Range/Selection native-object representation cleanup is deferred with the VMap/native-object work.
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
- First read cluster moved 2026-07-06. The module now owns simple identity/name/value reads for elements, text nodes, and comment nodes, including element `className`, plus narrow node and element navigation reads, `ownerDocument`, live element child collections, live form collections, live lookup collections, live select/options collections, snapshot attribute reads, the current template `content` shim, deterministic document proxy reads including the current `fonts`, `styleSheets`, `defaultView`, `implementation`, `designMode`, and `activeElement` cluster, foreign-document/window proxy routing, form-control named reads, select/option collection and value reads, read-only inspection methods, narrow attribute mutation methods, CharacterData mutation methods, element selector/descendant lookup methods, document lookup/selector methods, document factory methods, document structural methods, document lifecycle/location methods, document hit-test/legacy command methods, document Range/Selection entry methods and wrapper get/set dispatch, editor geometry helper methods, tree-inspection methods, structural methods, element/document/window EventTarget methods, inline style methods, CSSOM methods, element geometry/scroll methods, common reflected attribute reads, editing/input hint reads, reflected string/integer setters, simple boolean reflected setters, the `disabled` setter, the `contentEditable` setter, select/default boolean reflected setters, live selection setters, hint/spelling setters, the `iframe.srcdoc` setter, simple `value` setters, option subtree setters, text-control setters, text-control methods, select option-list methods, and form reset/validation/submission methods.
- Writable element `id` and `className` moved 2026-07-06. The module uses `dom_element_set_attribute()` for native cache maintenance and a narrow JS mutation hook for the existing mutation ledger.
- Writable text-node `data`, `nodeValue`, and `textContent` moved 2026-07-06. The module dispatches them through a narrow JS helper that preserves the existing live range and mutation-publication invariants.
- Added `test/js/dom_module_props.js` / `.html` / `.txt` to pin the moved clusters through the normal JS DOM harness.
- Current verification: direct `dom_module_props`, `dom_basic`, `dom_mutation`, and `dom_v12b` expected-output diffs, filtered gtest `JavaScriptTests/JsFileTest.Run/dom_module_props` + `dom_basic` + `dom_mutation` + `dom_v12b` + `dom_jquery_lib` + `js_document_exit_context`, `git diff --check`, Lambda `radiant_poc`, and full JS gtest passed. `make test-radiant-baseline` was attempted and remains non-green in the visual/layout baseline gates documented above.
- Remaining Phase 2 work: deeper Range/Selection native-object representation cleanup is deferred with the VMap/native-object work.

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
- In progress overall: deeper native-object representation cleanup remains to migrate with the VMap work.

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

Status:

- Completed 2026-07-08 as a readiness checkpoint after the Phase 2 compatibility migration. No wrapper representation has changed yet; that is Phase 6.
- The host-object contract for JS-visible native objects is now specified and has current-carrier hooks for `[[HasProperty]]`, `[[Delete]]`, descriptor reflection, enumeration, prototypes, and expando storage. `[[Get]]` and `[[Set]]` still route through the existing DOM get/set bridge until the VMap carrier switch.
- Executable readiness pins live in `test/js/dom_module_props`: DOM element, Range, and Selection expandos must be readable through normal property access, visible to `in`, descriptor-reflected, enumerable through own keys, deletable through the side-table store, and able to see registered prototype patches.

Host-object lookup contract:

- Canonicalize the JS key with `ToPropertyKey` before any host-object dispatch.
- Ask the module vtable for a projected own property first. A projected hit shadows wrapper expandos and the prototype chain.
- On projected miss, consult the wrapper's JS expando store. This store carries ordinary JS descriptor bits and is the only place script-added own properties live.
- On expando miss, walk the registered prototype chain (`dom_node` -> `Node.prototype` -> `EventTarget.prototype`, `dom_element` -> `Element.prototype` -> `Node.prototype`, concrete HTML subclasses later).
- Range and Selection wrappers follow the same host-object rules, but their native payloads are internal selection/range records rather than Radiant-owned `DomNode*` values.

Ownership contract:

- `dom_node`, `dom_element`, `dom_text`, `dom_comment`, and document-proxy wrappers are non-owning host objects. Their `destroy` operation unroots wrapper-side state but never frees the Radiant node/document.
- Owned module resources still use VMap finalizers. `JubeTypeDef` therefore needs an explicit ownership flag, not a convention inferred from whether `destroy` is null.
- Wrapper caches own the `Item` identity/root, not the native pointer lifetime. Document teardown invalidates cached wrappers before Radiant frees the backing arena; future subtree/node frees need the finer-grained invalidation hook already called out in Phase 3.

JS adapter requirements before carrier switch:

- Done for current carrier: add host-object `has` so `'id' in el` and expando membership do not depend on ad hoc own-shape probes.
- Done for current carrier: add host-object `delete` so projected DOM IDL attributes reject deletion according to their descriptor, while configurable expandos delete from the wrapper side-table store.
- Done for current carrier: add descriptor synthesis for projected properties. Current projected descriptors are enumerable, writable, and non-configurable; expando descriptors remain ordinary JS descriptors.
- Done for current carrier: add stable own-key enumeration with projected enumerable keys first in module-declared order, then expando keys in insertion order, excluding tombstones.
- Done for current carrier: add prototype mapping by native type. `instanceof`, `__proto__`, and prototype patching are now pinned for Element, Range, and Selection before VMap carrier work.
- Preserve live collection wrappers. Element, form, lookup, and select/options collection carriers may stay Array-backed during the DOM wrapper switch, but their owner lookup and mutation refresh hooks must continue to work when owners are VMap host objects.
- Preserve EventTarget rooting. Any native structure that stores script callbacks must expose or root those `Item`s through the host GC API; finalizers must not run script or allocate.

Selected readiness tests:

- Already pinned: wrapper identity (`test/js/dom_identity.js`), live element/form/lookup/select collections (`test/js/dom_module_props.js`), Range/Selection callable dispatch and identity (`test/js/dom_module_props.js` plus focused WPT selection slices), and DOM element expandos with `in`.
- Now pinned before switching wrappers: `Object.getOwnPropertyDescriptor` for projected attributes and expandos, `delete` for projected attributes and expandos, `Object.keys` ordering for projected-plus-expando keys, `instanceof` / `__proto__` over the registered DOM prototype chain, `Element.prototype`, `Range.prototype`, and `Selection.prototype` patch visibility, and Range/Selection expando parity.
- Keep broader gates unchanged: focused JS DOM gtests, full `./test/test_js_gtest.exe`, native `test_dom_range_gtest`, focused WPT selection slices, Lambda `radiant_poc`, and the documented Radiant baseline gate.

Tasks:

- Complete: extend the native type descriptor with ownership:
  - owning native object
  - non-owning host object
- Complete for current carrier adapter: extend or wrap `VMapVtable` for JS host-object needs:
  - `has`
  - `delete`
  - property descriptor reflection
  - stable enumeration order
- Complete: define expando behavior:
  - vtable property first
  - on miss, wrapper expando store
  - then prototype chain
- Complete for current carrier pins: define prototype mapping:
  - `dom_node` -> `Node.prototype`
  - `dom_element` -> `Element.prototype`
  - HTML element subclasses later
- Keep a regression checklist for live collection wrappers across element, form, lookup, and select/options collections before changing wrapper representation.

Acceptance:

- Complete for readiness spec: the VMap migration rules are written down before any wrapper representation change.
- Complete for executable pins: JS behavior that depends on identity, expandos, prototype patching, descriptors, delete, and own-key enumeration has targeted current-carrier tests before Phase 6 changes the wrapper representation.

### Phase 6: Switch DOM Wrappers to VMap

Purpose: make DOM a real native module object.

Tasks:

- Done: change `radiant_dom_wrap_node()` to return branded VMap wrappers.
- Done: make `js_dom_unwrap_element()` accept the branded VMap representation.
- Done: route JS property access for `LMD_TYPE_VMAP` host objects through the generic JS native-type adapter.
- Done: keep temporary compatibility for old `MAP_KIND_DOM` only where required for staged rollout.

Acceptance:

- Done: JS DOM tests pass through the VMap path.
- Done: Lambda sample still passes.
- Done: the wrapper cache still preserves identity.
- Done: DOM node finalizers are non-owning.

### Phase 7: Delete DOM MapKind Path

Purpose: finish the POC's architectural goal.

Tasks:

- Done for DOM nodes: remove `MAP_KIND_DOM` property dispatch.
- Done for DOM nodes: remove DOM-specific map branches from JS runtime where branded VMap host dispatch is sufficient.
- Done: delete stale wrapper-map creation code.
- Deferred and explicit: remove `js_dom_*` central registry entries after MIR lowering stops emitting direct document/DOM helper calls.

Acceptance:

- Done: focused JS DOM tests pass.
- Deferred broad gate: `make test-radiant-baseline` was not rerun after this narrow Phase 7 deletion; the latest documented broad delta is the Phase 6 visual/layout baseline state plus the now-fixed UI bucket.
- Done: the tiny Lambda sample passes.
- Deferred and documented: `sys_func_registry.c` still exposes MIR lowering shims (`js_document_method`, `js_document_get_property`, `js_dom_element_method`, `js_dom_get_property`, `js_dom_set_property`, `js_dom_wrap_element`, `js_dom_unwrap_element`, `js_is_dom_node`, and style/computed-style helpers) until lowering routes those operations through generic host-object descriptors.

## 7. First Deliverable Checklist

The first visible POC deliverable is complete when:

- Done: `import radiant` is the planned script surface and the static module is registered.
- Done: old `MAP_KIND_DOM` get/set delegated to module-owned DOM dispatch for the first property clusters.
- Done: wrapper cache ownership moved behind module/host APIs and now returns branded VMap DOM-node wrappers.
- Done: one tiny Lambda sample proves real DOM read/write access.
- Done: `make build` passes.
- Done: focused JS DOM/Radiant regression gates have no behavior delta.
- Done: full `./test/test_js_gtest.exe` passed `305/305` at the current checkpoint.

The POC 1A deliverable is therefore complete as a compatibility checkpoint. POC 1B is complete through the DOM-node part of Phase 7: DOM wrappers now use branded VMap carriers, the stale DOM-node map shell is gone, and only explicitly documented MIR/resource shims remain.

## 8. Test Gates

Use the narrowest gate that covers the touched surface, then widen at phase boundaries.

Minimum gates by phase:

- Phase 1: `make build`
- Phase 2: focused JS DOM property tests plus `make build`
- Phase 3: wrapper identity tests plus `make build`
- Phase 4: new Lambda sample test plus `make build`
- Phase 5: `dom_module_props` direct diff, focused DOM gtest slice, full JS gtest, and `git diff --check`
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
- Migrating Range/Selection/style resources from `MAP_KIND_WEB_API_RESOURCE` to native descriptors.

DOM-node `MAP_KIND_DOM` deletion is complete. The remaining cleanup is to move the non-node resource carrier and MIR lowering shims onto native descriptors.
