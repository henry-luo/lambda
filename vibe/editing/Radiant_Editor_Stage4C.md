# Radiant Rich Editor — Stage 4C: The Editor's Test Suite Green on the Lambda Runtime (two phases)

**Date:** 2026-07-08 · **Status:** **Phase A + Phase B + Phase C C1 GREEN, integrated and re-stamped under `make editor-4c`** — Phase A **1931/1931** (`make editor-4c-js`, headless `lambda.exe js`), original Phase B **39/39**, expanded Phase B+C1 **46/46** (`make editor-4c-view`, `lambda.exe view` + `event_sim`). All 5 LambdaJS runtime bugs + all Phase-B/C1 substrate blockers fixed (arrow-caret nav, selectionchange nested-CE, Shift+Enter state_machine invariant, Tab routing, table col-resize, JS-owned select selectedness). Gestures include **HTML5 drag-reorder** (JS `DragEvent` dispatch, now riding on the retention-safe native `DragDropState` — Radiant_Issue4 fixed), **table column resizing** (`Object.assign(elem.style, ...)` now writes through the inline-style host setter), and the Phase C C1 expansion for richer paste, block type select, blockquote autoformat, and mark toolbar breadth. **Milestone 3 COMPLETE:** `make editor-4c-parity` reconciles Radiant 1931/0 group-for-group against the vitest/jsdom oracle (1960 total − 29 React `.test.tsx` = 1931), **exact parity, zero unexplained divergence** (`vibe/editing/Stage4C_Parity_Report.md`); `make editor-4c` is green with expanded Phase B+C1 view coverage; CI wired via `run-tests-linux.sh`. React `.test.tsx` stays out (§1.2).
**Scope/goal:** Prove the **plain-DOM JS editor** works on the runtime it ships on, in two phases:
- **Phase A — `lambda.exe js`:** the **full ~1931-test plain-DOM suite** runs and passes on the headless **LambdaJS** runtime — *does the editor's own test corpus pass at the JS level under Lambda?*
- **Phase B — `lambda.exe view` + `event_sim`:** a **curated subset** (§4.2) runs as true **end-to-end UI automation** under the full Radiant stack (real layout, event dispatch, caret/selection rendering) — *does real interactive editing work end-to-end?*
- **Phase C — expanded `event_sim` coverage:** promote the most important remaining Phase-A behaviors into real UI fixtures (§4.3), especially cases whose bugs only appear with Radiant geometry, DOM selection, clipboard, toolbar state, and event routing.

Where the runtime falls short, **enhance DOM fidelity (`js_dom`) and `event_sim`** rather than weaken the tests (CLAUDE.md #1).
**Builds on:** [Radiant_Editor_Stage4B.md](Radiant_Editor_Stage4B.md) (the plain-DOM editor + common C++ substrate + the `view --event-file` testdriver), [JS_13_Web_DOM.md](../../doc/dev/js/JS_13_Web_DOM.md) (LambdaJS DOM surface + known issues).

---

## 0. TL;DR

The editor's **1960-test vitest suite is green under `jsdom` (Node)** — a browser-API emulation — and **Stage 4C now makes the shipping LambdaJS/Radiant runtime a first-class green test target**. The full non-React plain-DOM corpus runs under LambdaJS, and the high-fidelity interaction lane runs through Radiant's real layout/event/caret stack.

- **Phase A (breadth, cheap, fast):** the **whole plain-DOM suite (1931 tests; React excluded)** is green headless under **`lambda.exe js`**. This proves the editor's logic + DOM-level behavior on **LambdaJS**, with fixture inlining and deterministic tier chunking handled by the Phase A runner.
- **Phase B (depth, slower, high-fidelity):** the curated interaction subset is green under **`lambda.exe view` + `event_sim`** — real events in, real DOM/selection out — proving true end-to-end editing (typing, keys, selection, paste, drag, toolbar, indent, caret preservation, image resize, table col-resize) through the full Radiant stack.
- **Phase C (expanded depth):** add a second UI lane over high-value Phase-A cases that Phase B intentionally sampled lightly. C1 is green for richer paste, pasted image HTML, block type select, blockquote autoformat, and mark toolbar breadth; C2 keeps table merge/split, gap cursor/block atoms, inline atom deletion, advanced caret/selection, and source-path/reconcile invariants as optional deeper work.

**Why two vehicles:** `lambda.exe js` is fast and covers breadth (all ~1931) but only exercises the JS engine + a minimal DOM — no layout, no real event routing, no caret rendering. `lambda.exe view` exercises the *full* stack but is slower to set up per case, so it validates a representative **subset** deeply. Together: **breadth on A, depth on B.**

---

## Progress (2026-07-01)

**Phase 0 — harness foundation: DONE & proven.**
- `test/editor-js/harness/inengine.ts` — a vitest-compatible in-engine shim (`describe`/`it`/`it.skip`/`expect` + the matchers the suite uses + `beforeEach`/`afterEach` + `vi.fn`). Prints `HARNESS pass=N fail=M skip=K` + one `FAIL <name>: <msg>` per failure. Sync-only (async tests not yet supported).
- `test/editor-js/tools/build-conformance.mjs` — esbuild-bundles chosen `.test.ts` files to a classic IIFE, aliases `vitest`→the shim, appends `__harnessRun()`. Rejects `.tsx`.
- **Verified:** the editor's real `.test.ts` files run **unmodified** under `lambda.exe js`. Core editor (commands+model+input, 209 tests) → **198 pass / 11 fail**; a 94-test slice → 92/94. The pipeline works end-to-end (bundle → run → machine-readable report).

**Phase A gap inventory (empirical, from the 11 core failures — all in tight clusters):**
1. **`Intl` not defined** (2) — `src/commands/caret.ts:97` uses `new Intl.Segmenter(…,{granularity:'word'})` for word-boundary nav. → implement `Intl.Segmenter` in LambdaJS, or a word-seg fallback.
2. ~~**`parseHtmlToDoc` DOM-parse fidelity** — drawing tests fail ("null is not iterable")~~ **RESOLVED / mis-attributed.** The drawing "null is not iterable" failures were actually the **numeric-inference bug (#3)**, not a parser gap — after the #3 fix, **drawing runs 50/50** and the tiers (which also use `parseHtmlToDoc`) are 1601/1601. `parseHtmlToDoc` fidelity under `--document` is fine for editing markup.
3. **LambdaJS numeric type-inference bug in JS→MIR lowering — ROOT-CAUSED & FIXED (2026-07-01)** (fixed `selMap` 1 + gap-cursor 4 + `cmdMoveNode` 3 → core bundle **198→207/209**).
   - **Fix:** `lambda/js/js_mir_statement_lowering.cpp` — for a var/const whose initializer is a `CALL_EXPRESSION` inferred INT/FLOAT, keep it **boxed** (widen to ANY) unless it's a guaranteed-numeric `Math.*` builtin. A user function's numeric return type is speculative; the native var path unboxed it with an unchecked `it2d`/`d2i` that collapsed the boxed object to 0.
   - **Regression check: ZERO.** node-baseline's 2 flagged "regressions" + `test_js_gtest`'s 1 fail all fail identically on the pre-fix binary (pre-existing/stale-baseline), verified by git-stash + rebuild. (`vm_runincontext_cross_unit` shows the same object→undefined signature — a same-class case my narrow fix doesn't reach; follow-up candidate.)
   - *(original root-cause detail below)*
   - **Symptom:** `const m = stepMap(step, p)` inside a `for...of` loop → `m === 0` (should be the boxed `SourcePos`). Inline `stepMap(...)` works; the `const` binding fails.
   - **Root cause (from `temp/js_mir_dump.txt`):** the lowering **statically mis-infers `const m` as numeric** and emits `js_profiled_it2d` (item→double unbox) + `d2i` on the **boxed-object** call result before binding `m` (top-level `_js_anon0_165`, immediately after `js_call_function_1462` = the `stepMap` result). Unboxing an object as a double gives `0`. Same class as the Lambda-core *unsound speculative-int inference*, but in the JS transpiler.
   - **Layer:** MIR **lowering**, not JIT codegen — reproduces identically under `JS_MIR_INTERP=1` and default. Fix domain: `lambda/js/js_mir_expression_lowering.cpp` numeric inference / the `it2d` emission for a call-result binding.
   - **Module-state dependent** (fragile to minimize). Reliable repros: `temp/4c-spikes/min.js` (real bundled `stepMap`) + `temp/4c-spikes/depth.js` (self-contained). Diagnostics: `JS_MIR_DUMP=1 ./lambda.exe js <bundle>` → `temp/js_mir_dump.txt`; `JS_MIR_INTERP=1` = interpreter (JIT-vs-lowering bisection).
   - **⚠️ Fix caution:** this numeric/int inference is load-bearing (GC rooting, deltablue); scope the fix to call-result bindings and run the full lambda + node baselines.

**Phase A logic + tier bulk — GREEN (2026-07-01).** After the inference fix (#3) + tier fixture-inlining (`tools/build-tier.mjs` — virtual FS shimming `node:fs`/`node:path`/`node:url` + inlined fixtures, run under `--document`), the DOM-independent surface is fully green:
- commands+model+input **207/209** (2 = `Intl.Segmenter`) · helpers+smoke **8/9** (1 = custom-element parse) · **drawing 50/50** · **all tiers 1601/1601** (Slate 293 · PM 212 · HTML 520 · Chromium 163 · structural 22 · drawing 391).

**Phase A view lane — GREEN (the hard part is closed).** The 7 plain-DOM `view/*.ts` files exercise the real mount → event → command → reconcile → DOM/selection loop, and surfaced a **chain of LambdaJS runtime bugs** (all documented with symptom / root-cause / minimal-repro / fix in **[vibe/Lambda_Bug.md](../Lambda_Bug.md)**):

| # | Bug (JS runtime) | Status | Effect |
|---|---|---|---|
| — | **`window.document` was `undefined`** (bare `document` is transpiler-special-cased, never a real global prop) → `window.document.createRange()` threw | **FIXED** (`js_dom_selection.cpp`) | unblocked dom-bridge/render-vnode/etc. |
| 1 | **class field-arrow loses `this` in a nested function scope** (esbuild wraps bundles in an IIFE) → dispatched handler `this` = undefined → BUS crash | **FIXED** | `editor-view-dom` **crash → runs** |
| 2 | **residual: field-arrow with MIXED captures** (`this` + a called import) still lost `this` — Bug-1 guard too narrow | **FIXED** | removed the remaining crash class |
| 3 | **`heap_create_name` fails inside dispatched handler** — CLI queries `process.exitCode` after the EvalContext is restored to NULL | **FIXED** (`js_globals.cpp`, guarded the intern) | eliminated spurious interning errors *(was a **red herring** for the editor)* |
| 4 | **`onChange is not a function`** — NOT a `this` bug: a hoisted **IIFE-scope function declaration** (esbuild-inlined import) is never written to its **module-var slot**, so a nested inline-`new` field-arrow that captures it reads garbage → `intentFromInputEvent is not a function` aborts `handleBeforeInput` before `dispatch`/`onChange` run | **FIXED** (`js_mir_function_class_lowering.cpp` — `jm_hoisted_func_modvar_write_through` at both inner-func-decl hoist sites) | `editor-view-dom` **3/9 → 9/9** |
| 5 | **`full-editor-dom` stack overflow** — a deeply-nested closure (`method → open → EMOJI.map → arrow → arrow`) captures the IIFE-scope import `cmdInsertText`. The direct sync-IIFE function-declaration promotion path registered it as `MCONST_MODVAR` but didn't distinguish from ordinary wrapper-local decls in capture/shadow analysis, so the inner arrow snapshotted the wrapper's callable → self-recursive resolution → stack blows | **FIXED** (`js_mir_context.hpp` + `js_mir_module_batch_lowering.cpp` — new `is_iife_func_decl` marker + `jm_modvar_is_iife_scope_binding()` filter) | `full-editor-dom` **crash → 13/14** |

Current view-lane tally: `dom-bridge` 7/7 · `render-vnode` 9/9 · `html-parser` 12/12 · `use-editor-state` 5/5 · **`editor-view-dom` 9/9** (Bug 4 fixed; the last case also needed `toHaveBeenCalled*` mock matchers added to the in-engine harness) · **`full-editor-dom` 14/14** (Bug 5 fixed; Shift+Tab outdent unblocked once `Element.attributes` was implemented) · **`reconcile` 6/6** (SVG `createElementNS` namespace + `isConnected` implemented).

**Overall: 1931/1931 green under `lambda.exe js`.** Phase A closed, re-stamped, and reconciled against the jsdom oracle.

**Phase A closing fixes (2026-07-02, all in `lambda/js/js_dom.cpp` + `lambda/js/js_runtime.cpp`):**
- **SVG `createElementNS` namespace** — the constructed URI was hard-coded xhtml. `createElementNS` now records the requested namespace on the element (as a reserved internal `__lambda_ns_uri` attribute) and `namespaceURI` reads it back. All JS-facing attribute enumeration (`getAttribute`/`hasAttribute`/`getAttributeNames`/`attributes`/inner-html serialization) filters names starting with `__lambda_` so the marker doesn't leak.
- **`Node.isConnected`** — was missing on text / comment / element nodes; wired to the existing `js_dom_node_is_connected()` helper so `t0.isConnected` reflects tree membership after a reconcile patch.
- **`Element.attributes`** — was `undefined`, so `reconcile.applyAttrs`'s stale-attribute removal loop (`for (i=0; i<elem.attributes.length; …) removeAttribute(name)`) silently did nothing. Now returns a NamedNodeMap-like array of `{name, value}` pairs (internal attrs filtered). This ALSO unblocked the Shift+Tab outdent case (`marginInlineStart` style attribute was never being cleared → `1.75em` sticky).
- **DOM method optional-chaining calls** — `elem?.querySelector(sel)` threw "is not a function" because the property accessor returned the ITEM_TRUE feature-detection sentinel; a direct `elem.querySelector(sel)` shortcuts to `js_dom_element_method` but the optional-chain path evaluates the property first. Added callable trampolines for `querySelector`, `querySelectorAll`, `closest`, `getAttribute`, `hasAttribute`, `contains` so property access returns a real function, keeping feature-detection intact.
- **`HTMLTemplateElement.content`** — was `undefined`, so `template.content.querySelector('doc')` threw. Shimmed to return the template element itself (children attach directly to the template on `innerHTML` set, so this is sufficient for fixture parsing; documented fidelity gap only for callers that mutate via `.content.appendChild`).
- **`Intl.Segmenter`** — was undefined. Added a minimal `Intl` namespace with `Segmenter` as a native constructor. Only implements word granularity via a byte-level heuristic (`[A-Za-z0-9_]` plus any high-bit byte, so UTF-8 continuations stay in the current segment). `.segment(text)` returns an array of `{index, segment, isWordLike}` — for-of iterable via the array itself. Higher-fidelity Unicode word breaking is a follow-up.

**Bug 4 fix (2026-07-01):** root-caused via `temp/4c-spikes/t3.js` (minimal: nested inline-`new` of a class whose field-arrow captures an IIFE-scope function decl). The hoist of an inner function declaration wrote the closure only to its local reg + shared scope env, never to the **module-var slot** that nested-closure captures resolve through (`js_get_module_var`). Fix: `jm_hoisted_func_modvar_write_through()` mirrors the write-through the non-direct statement path already does. Regression: `test/js/class_field_decl_capture_nested_new.{js,txt}`. Validated: `test_js_gtest` 302/303, lambda baseline 3227/3228 (only pre-existing `vm_runincontext_cross_unit`), zero regression. Full detail in [Lambda_Bug.md](../Lambda_Bug.md) §"Bug 4".

**Bug 5 fix (2026-07-02):** `full-editor-dom` stack overflow — a deeply-nested closure captured the IIFE-scope import `cmdInsertText`, which was registered as `MCONST_MODVAR` but not distinguished from ordinary wrapper-local decls in capture/shadow analysis. The inner arrow snapshotted the wrapper's callable → self-recursive resolution → stack blows. Fix: new `is_iife_func_decl` marker + `jm_modvar_is_iife_scope_binding()` filter in `js_mir_context.hpp` + `js_mir_module_batch_lowering.cpp`. Full detail in [Lambda_Bug.md](../Lambda_Bug.md) §"Bug 5".

**Phase B — 28/28 (blockers) → 32/32 (gestures) → 39/39 (final re-stamp), all green (2026-07-08):** `make editor-4c-view` originally ran the 4B baseline + the Stage-4C fixture set (`test/ui/editor4c/`) under `lambda.exe view --event-file` + `event_sim`. **39/39 pass**. No `_open_` blockers remain — all were root-caused and fixed. Phase C C1 now extends the same make target to **46/46** by adding `test/ui/editor4c_phase_c/`.

New Stage-4C fixtures:
- typing/editing: `type-in-list`, `delete-forward`, `enter-in-list`, `backspace-join`, `multi-typing`, `select-all-delete`, `replace-then-mark`, `shift-enter-linebreak`
- marks/toolbar: `italic-toolbar`, `bold-italic-combo`, `mark-toggle-off` (uses `not_contains`)
- history/nav: `undo-redo-typing` (Cmd+Z/Cmd+Shift+Z), `arrow-caret-nav` (Home + ArrowRight), `tab-indent-list`, `shift-tab-outdent`

**Blocker fixes (all substrate root-causes, not test adaptations):**
1. **Shift+Enter state_machine invariant** — the geometry resolver mapped a caret at end-of-block-after-`<br>` onto the void `<br>` at offset 1 (out of bounds), tripping `radiant/state_machine.cpp`'s caret-projection invariant → SIGABRT. Fixed by guarding both caret-projection paths (`state_store_refresh_caret_projection` + `expected_legacy_caret_projection`) to keep the valid boundary projection when the geometry endpoint's offset exceeds the target's limit. New helper `caret_target_offset_valid` mirrors `legacy_view_offset_limit`.
2. **Tab not reaching the JS handler** — Radiant consumed Tab for focus rotation / translated it into a `formatIndent` beforeinput the editor (which handles Tab via keydown) never asked for. Fixed in `radiant/event.cpp`: Tab is now dispatched as a keydown first (browser-faithful — Tab fires no beforeinput), with focus-nav only as the not-prevented fallback. This routes Tab → `FullEditorDom.handleKeyDown` → indent/outdent for both plain and rich/script-owned surfaces (native rich editing is retired).
3. **arrow-caret-nav** — the fixture used the DOM key name `"ArrowRight"`, which `event_sim` didn't map (→ key -1). Added `Arrow*` aliases in `radiant/event_sim.cpp`. The underlying rich caret nav (`editing_controller_handle_rich_navigation`) + caret→DOM-selection sync (`state_store_commit_collapsed_caret` → `state_store_set_selection` → `selectionchange`) already worked.
4. **selectionchange-delivery** — was already resolved for the JS editor; the runner just ran it against the wrong page. The `make editor-4c-view` runner now honors each fixture's `"html"` field, so it runs against `test/html/editor4b-selectionchange.html` and passes.

**event_sim enhancements:** `not_contains` predicate on `assert_attribute` (`radiant/event_sim.cpp` + `.hpp` + `assert_not_contains` field — passes when the attribute is missing OR does not include the substring); `Arrow{Left,Right,Up,Down}` accepted alongside the short `{left,right,up,down}` names. The runner reads each fixture's `"html"` (default `test/html/editor-dom.html`).

Structured-model assertion (`assert_editor_doc`) still not needed: existing `assert_attribute` on `[data-source-path]` + `assert_text` + `not_contains` cover every case authored so far.

**Phase B gesture expansion (2026-07-02) — 28 → 32/32:** added `color-picker`, `paste-plain`, `paste-html`, `copy-image`.
- **Clipboard for rich surfaces (B0 prerequisite, substrate feature):** the script-owned editor uses `addEventListener('paste'|'copy')`, but under `lambda.exe view` Radiant fired no JS ClipboardEvent for rich/contenteditable surfaces (Cmd+V went through the rich beforeinput intent; Cmd+C through native selection-copy) — so paste/copy silently did nothing. Added `js_dispatch_clipboard_event_to_element()` (`lambda/js/js_clipboard.cpp`): builds a `clipboardData` (DataTransfer) backed by the C clipboard store — pre-populated from the store for `paste`, written back to the store after dispatch for `copy`/`cut` — and dispatches a cancelable JS ClipboardEvent. Wired in `radiant/event.cpp` (`radiant_dispatch_clipboard_event` + a Cmd/Ctrl+V/C block in the keydown path): fire the JS event first; if the handler `preventDefault`s, stop; else fall through to the native default. (Bug fixed en route: `clipboard_store_read_mime` returns a single reused buffer, so text/plain must be copied before reading text/html — use-after-free otherwise.)
- **color-picker** needed no new infra (toolbar swatch → `cmdSetMark('color')`, like the bold/italic fixtures).

**§5.2 revisited — geometry was NOT the blocker; window mouse-event delivery was (2026-07-02):** the §5.2 hypothesis (Phase-16 incremental-skip orphaning JS-subtree geometry → coordinate clicks miss) did not hold up empirically — toolbar/color/emoji selector-clicks all land, and the image-resize overlay's 4 corner handles lay out correctly (`assert_count .rdt-img-handle == 4` passes; `mouse_drag` resolves the handle center and its mousedown fires). The real gap: **`mouseup` was never dispatched as a JS EventTarget event** (only `mousedown` + `click` were), and mouse events weren't reaching `window`-level listeners. The editor's `startImageResize` (and block drag) use `window.addEventListener('mouseup', …)`, so the resize/reorder commit never ran. Minimal probes (`temp/4c-spikes/winmouse*.{html,json}`) confirmed: window `mousedown`/`click` fire, window `mouseup` did not.
- **Fix (`radiant/event.cpp`):** dispatch a JS `mouseup` through `radiant_dispatch_mouse_event` at the top of the `RDT_EVENT_MOUSE_UP` handler (browsers fire mouseup before click; it bubbles element→document→window like mousedown). Low-risk, browser-faithful. → **`image-resize` fixture now passes** (click image → drag SE handle → `cmdResizeImage` commits width/height on the window `mouseup`).
- **`event_sim` enhancement:** `mouse_drag` accepts a relative `dx`/`dy` delta (destination = resolved-start + delta) so a fixture can drag a resolved element (e.g. a resize handle whose absolute position isn't known statically).
- Note: per-move JS `mousemove` to `window` is still not dispatched (deliberately — high frequency, broad blast radius); image-resize doesn't need it because the editor's `onUp` commits with `last` defaulting to the handle's start box. A gesture that needs live intermediate `mousemove` on `window` would need that added.

**drag-reorder — HTML5 drag-and-drop, DONE (2026-07-02):** the editor reorders blocks with `addEventListener('dragstart'|'dragover'|'drop')` + a `DataTransfer`, but Radiant's native drag machinery (`DragDropState`) dispatched only Lambda-template handlers and only on `dropzone`-attributed elements — so the editor's JS handlers never fired. Fixed by adding a **JS `DragEvent` dispatch path** parallel to the clipboard-event work:
- **JS side** (`js_dom_events.cpp` `js_create_native_drag_event` — a MouseEvent with a `dataTransfer`; `js_clipboard.cpp` a session `DataTransfer` held in a GC root + `js_dispatch_drag_event_to_element`). The one `DataTransfer` persists across the whole gesture (dragstart→dragover→drop) so `setData()` round-trips to `getData()`, browser-faithful. Session allocation happens **inside** the JS ctx (js_new_object needs the active heap) — on `dragstart`.
- **Radiant side** (`radiant/event.cpp`): `radiant_dispatch_drag_event` (enters the JS ctx like `radiant_dispatch_clipboard_event`); `<img>` / `<a href>` are treated as **draggable by default** (browser-faithful) at the mousedown drag-initiation walk; JS `dragstart`/`dragover`/`drop`/`dragend` are wired to the element under the cursor (independent of the dropzone-based native `drop_target`). Drop is gated on the last `dragover` being `preventDefault`'d (HTML5 semantics).
- **Root-cause subtlety (now fixed at the source):** the editor mutates the DOM inside `dragover` (`showDropLine`), which used to trigger Radiant's "force full relayout by clearing view tree" → `doc_state_clear_drag_drop`, wiping the **native** drag mid-gesture (confirmed: `active` flipped to 0 after the first dragover). This was filed as `vibe/radiant/Radiant_Issue4.md` and **fixed** by the DOM-mutation retention work (`vibe/radiant/Radiant_DOM_Mutation.md`): the fallback now retains `DragDropState` (`active`/`pending` + `source_node_id`) across a rebuild instead of clearing it, and `source_view` is a DOM element that survives (only layout views are freed).
- **Workaround retired (2026-07-02):** the initial Stage-4C shim (file-static `g_jsdnd_active`/`g_jsdnd_drop_allowed`/`g_jsdnd_source`) has been **removed**; the JS DnD dispatch now rides directly on the retained native `dd->active`/`dd->source_view`, with drop gated on the mouseup's final `dragover` return (HTML5). `event_sim`'s `drag_and_drop` draggable check was relaxed to match the img/a default. `drag-reorder.json` passes without the shim.
- **Fixture:** `drag-reorder.json` drags the seed `<figure><img>` (7th block) to the top → `cmdMoveNode` makes it the 1st block; asserts it's no longer after the "Image" `<h2>`, is now `.rdt-editor > figure:nth-child(1)`, and its `<img>` survives.

**§4.2 gestures — autoformat + link/mention DONE (2026-07-02):**
- **markdown autoformat** ✅ — the doc's earlier "not wired" note was **stale**: `onBeforeInput`→`dispatchIntent`→`cmdInputRule` (src/input/intent.ts) already runs on every 1-char insert, so autoformat fires with no editor change. Fixtures: `autoformat-inline.json` (`*hi*`→italic span; delimiters consumed) and `autoformat-heading.json` (`## `→`<h2>`; marker consumed).
- **link / mention** ✅ — the toolbar handlers call `window.prompt(...)`, which didn't exist under headless `view`. Added **`window.prompt`** with a harness-settable response queue (`lambda/js/js_dom.cpp` `js_window_prompt` + `js_window_dialog_push_response`/`_reset`, installed on global+window in `js_dom_set_document`) and an **`event_sim` `set_prompt` event** (`radiant/event_sim.{hpp,cpp}` SIM_EVENT_SET_PROMPT; `response` absent → Cancel/null). Fixtures: `link-toolbar.json` (dblclick word → seed URL → Link button → `<a href>`) and `mention-insert.json` (seed name → mention button → `.rdt-mention` atom).
- **table col-resize** ✅ — the earlier blocker was not table `getBoundingClientRect()` after all. The resizer DOM existed, but `Object.assign(e.style, { left, top, height })` boxed the native inline-style host object, so the style setters never ran and the absolute-positioned handles were laid out at the wrong geometry. Fixed in `Object.assign` target handling: inline-style VMAPs stay native so assignments write through `js_dom_set_style_property`, while ordinary VMAPs keep normal object semantics. Fixture: `table-col-resize.json` drags a column handle and asserts explicit cell width; green both standalone and under `make editor-4c-view`.

**Regression check:** `test_js_gtest` 302/303 (pre-existing `vm_runincontext_cross_unit`; `dom_jquery_lib`+`lib_popper` also pre-existing — verified by stash-rebuild); `test_ui_automation_gtest` 219/219; `test_radiant_view_gtest` 20/20; `make test-radiant-baseline` 5934/5934 (fully green; the DnD changes to the core mouse path in `event.cpp` verified non-regressing).

**Milestone 3 — integration (2026-07-08 re-stamp):** the whole Stage-4C suite is reproducible via `make`:
- **`make editor-4c-js`** → `test/editor-js/tools/run-phase-a.mjs` bundles each group (core, view, drawing, and the 6 populated tier corpora) to an IIFE, runs it under `lambda.exe js` (`--document` where DOM is needed), and aggregates the in-engine `HARNESS pass=/fail=/skip=` lines. React `.test.tsx` excluded by construction; the README-only `tier_c_wpt` placeholder is skipped. The runner now regenerates its `temp/4c-spikes/{page,dom_page}.html` harness pages and chunks the largest fixture tiers (`tier_e_html`, `tier_0_drawing`) into deterministic sub-bundles, then reports the same logical group totals. **PHASE-A TOTAL 1931/1931.**
- **`make editor-4c-view`** → the Phase-B fixtures plus Phase C C1 under `lambda.exe view` + `event_sim`. **46/46.**
- **`make editor-4c-parity`** → `test/editor-js/tools/parity-report.mjs` runs the vitest/jsdom oracle (the editor's own suite under Node) AND Phase A under `lambda.exe js`, then reconciles **per run-phase-a group** (the oracle's per-file counts summed into the same core/view/drawing/tier_* groups vs. Radiant's per-group pass counts). Writes `vibe/editing/Stage4C_Parity_Report.md`; exits non-zero on any unexplained divergence.
- **`make editor-4c`** → now `editor-4c-parity` + `editor-4c-view` (parity subsumes the Phase-A run); exits non-zero on any failure. Confirmed green end-to-end.

**Milestone 3 COMPLETE (2026-07-08 re-stamp) — parity + CI wired:**
- **Parity report ✅ EXACT.** Oracle = **1960** tests (all green under jsdom); of these **29** are React `*.test.tsx` (4 files: editor-view 8, paste-integration 10, render 7, tab-keybinding 4) — excluded from Phase A by construction (§1.2). The remaining **1931** non-React tests reconcile **group-for-group** against Radiant's 1931/0: `core 218 · view 62 · drawing 50 · tier_a 293 · tier_b 212 · tier_d 22 · tier_e 520 · tier_f 163 · tier_0 391`. `1931 + 29 = 1960` — exact, zero unexplained divergence. Report: `vibe/editing/Stage4C_Parity_Report.md`.
- **CI wired.** `.github/workflows/run-tests-linux.sh` now installs the editor-js npm deps and runs `make editor-4c` after `make test`, folding its exit into the overall result (headless: Phase A/parity need only node + esbuild + `lambda.exe js` + jsdom; Phase B uses `lambda.exe view --headless`).

**Phase C C1 expansion (2026-07-08) — 39 → 46/46:** added `test/ui/editor4c_phase_c/*.json` and wired the directory into `make editor-4c-view`. The active C1 set covers blockquote autoformat, block type select, code/strikethrough/highlight toolbar commands, multi-paragraph paste, and pasted image HTML. Direct C1 run: **7/7**. Integrated `make editor-4c-view`: **46/46**.
- **JS-owned select selectedness fix:** `event_sim` changed the native select index before firing JS events, but the editor's `change` handler reads `target.value` through the LambdaJS DOM wrapper. The wrapper still had stale option selectedness, so block type changes observed the old value. `radiant_dispatch_event_sim_select_change` now mirrors the selected index into `js_dom` before dispatching `input`/`change`, matching browser event ordering.

**Future optional Lane-B expansion (C2):** broader fixture translation remains possible (table merge/split, gap cursor around images, mention atom delete, HR autoformat, deeper table cell controls, source-path/model-selection assertions, and reconcile/history nuance), but it is no longer blocking Stage 4C acceptance. The original `_open_` blockers have been diagnosed and fixed.

**Bug 4 fix (2026-07-01):** root-caused via `temp/4c-spikes/t3.js` (minimal: nested inline-`new` of a class whose field-arrow captures an IIFE-scope function decl). The hoist of an inner function declaration wrote the closure only to its local reg + shared scope env, never to the **module-var slot** that nested-closure captures resolve through (`js_get_module_var`). Fix: `jm_hoisted_func_modvar_write_through()` mirrors the write-through the non-direct statement path already does. Regression: `test/js/class_field_decl_capture_nested_new.{js,txt}`. Validated: `test_js_gtest` 301/302, lambda baseline 3227/3228 (only pre-existing `vm_runincontext_cross_unit`), zero regression. Full detail in [Lambda_Bug.md](../Lambda_Bug.md) §"Bug 4".

**Spikes/bundles** live in `./temp/4c-spikes/` (ephemeral; regenerate via `tools/build-conformance.mjs` / `tools/build-tier.mjs`).

---

## 1. Goal, scope, target number

### 1.1 Goals
- **Phase A:** `make editor-4c-js` runs the ~1931 plain-DOM tests under `lambda.exe js` and reports **0 failed**, at parity with the jsdom oracle.
- **Phase B/C:** `make editor-4c-view` runs the Lane B subset (§4.2) plus the Phase C C1 expansion (§4.3) under `lambda.exe view` + `event_sim` and reports **0 failed**, exercising every distinct editing interaction end-to-end plus the promoted high-value Phase-A UI cases.

### 1.2 Target number — ~1931 (plain-DOM, React excluded)

The user directive is explicit: **the plain JS DOM version, not React.** The **29 React `.test.tsx`** tests (`editor-view`, `paste-integration`, `render`, `tab-keybinding`) need a React host and are **excluded from both phases**; each already has a plain-DOM twin that *is* in scope (`editor-view-dom` / `full-editor-dom` / `render-vnode` / `tab keybinding via structural-commands`), so no behavioral coverage is lost. React-under-Radiant remains the separately-deferred milestone (4B §3.2).

Corpus reconciliation (verified counts):

| Bucket | Tests | Phase A | Phase B |
|---|---:|:--:|:--:|
| Unit — `commands/*` (10 files) | 124 | ✅ all | subset |
| Unit — `model/*` (6) · `input/*` (1) · `helpers` · `smoke` | 77+8+6+3 = 94 | ✅ all | — |
| Unit — `drawing/*` (5) | 50 | ✅ all | — (Stage 5 scope) |
| Unit — `view/*.ts` (7 plain-DOM) | 62 | ✅ all | subset |
| Tier corpora — Slate 293 · PM 212 · HTML 520 · Chromium 163 · structural 22 · drawing 391 | 1601 | ✅ all | — |
| **React `.test.tsx`** (4) | 29 | ❌ excluded | ❌ |
| **Phase A total** | **≈1931** | | |

(Exact count pinned at Phase 0 when the harness enumerates the suite. 124+94+50+62+1601 = **1931**.)

### 1.3 Non-goals
- React under Radiant (deferred; separate milestone).
- Retiring the vitest/jsdom suite — it **stays** as the fast dev loop and the parity oracle; the Radiant lanes must agree with it.
- A real-*browser* (Chrome/Playwright) run — orthogonal; jsdom remains the Node reference.

---

## 2. Why this matters

The editor is validated only against **jsdom**. Its shipping runtime — LambdaJS + the Radiant DOM/layout/event stack — is proven for a **13-fixture** subset only, so any LambdaJS/`js_dom` divergence from browser semantics is invisible today. Phase A closes the **breadth** gap (does the whole corpus pass on LambdaJS?); Phase B closes the **depth/fidelity** gap (does real interaction work through the full stack?). Every divergence becomes a failing test with a clear owner: engine / `js_dom` / substrate vs. a legitimate editor difference.

---

## 3. Empirical gap analysis (spikes run this session)

Spikes under `./temp/4c-spikes/` against the current `lambda.exe`. **They substantially de-risk Phase A:**

### 3.1 What already works under `lambda.exe js`
| Probe | Result |
|---|---|
| Modern JS (spread, destructuring, classes, generators, `Map`/`Set`, optional chaining, `??`, `structuredClone`, template literals) | ✅ 13/13 |
| **Real editor logic** — `cmdInsertText`/`cmdDeleteBackward`/list-join, `src/` bundled → IIFE via esbuild (36 kB, 12 ms) | ✅ 3/3 assertions green |
| **DOM APIs the view tests use** (`js --document`): `createElement`, `appendChild`, `textContent`, `setAttribute`/`getAttribute`, `classList`, `dataset`, `querySelector(All)`, `getSelection`, `createRange`, `Range.setStart/setEnd/toString`, `selection.removeAllRanges/addRange/rangeCount`, `getBoundingClientRect`, `addEventListener` | ✅ all present |

**Implication:** no language or major-API blocker. The editor engine + DOM surface are already runnable under LambdaJS.

### 3.2 Confirmed gaps
| Gap | Evidence | Nature / phase |
|---|---|---|
| **No in-engine test harness** | vitest can't run under LambdaJS | Phase A: a ~150-line `describe`/`it`/`expect` shim + runner |
| **`fs`/`require`/`module` = undefined** | probe: `typeof fs === 'undefined'` | Phase A: tier corpora load fixtures via Node `fs` (`tier_*/run.test.ts`, `helpers/fixture-runner.ts`) → **inline fixtures at build time** (the one real infra lift) |
| **TS ESM, no loader** | `require`/`module` undefined | Phase A: bundle to a classic **IIFE** (4B constraint) |
| **DOM behavioral fidelity** (APIs present, *semantics* imperfect) | 4B known issues | Phase A (js_dom) + Phase B (full stack): property↔attribute reflection, stale geometry for JS subtrees post-edit, `selectionchange`→nested-CE — §5 |
| **`MutationObserver` = undefined** | probe | **Not needed** — neither `src/` nor the view tests use it (verified) |

---

## 4. The two phases

### 4.1 Phase A — full suite under `lambda.exe js` (breadth)

Run the whole 1931-test plain-DOM suite headless on LambdaJS. Vehicle: **`lambda.exe js <bundle.js>`** (and/or `js --document <page.html>` for the `view/*.ts` DOM tests), the same headless runner `test_js_gtest` already uses.

**Harness/bundling pipeline** (`tools/build-conformance.mjs`, `tools/build-tier.mjs`, `tools/run-phase-a.mjs`):
1. **esbuild** compiles `src/` + the vitest `.test.ts` files (**excluding `*.test.tsx`**) to JS.
2. Swap the vitest imports (`describe`/`it`/`expect`/`vi`) for a **~150-line in-engine shim** that records results and prints `PASS n / FAIL m` + per-failure detail to stdout.
3. Replace Node **`fs`/`path`** fixture reads with **build-time inlined** data (walk the tier fixture dirs → emit `fixtures.generated.ts`) — the one real infra lift (~1601 tier cases depend on it).
4. Bundle to **classic IIFE** module(s) (no `type="module"`). For the ~7 `view/*.ts` DOM tests, run under `js --document <minimal.html>` so `js_dom` is present.
5. `make editor-4c-js`: build → run the bundle(s) → parse the `HARNESS pass=/fail=/skip=` summaries → fail on any non-zero fail count. The largest fixture tiers (`tier_e_html`, `tier_0_drawing`) are split into deterministic sub-bundles and aggregated back to the same logical group totals.

**What Phase A proves:** the editor's logic + DOM-level behavior are correct on LambdaJS. The ~7 `view/*.ts` tests will surface any `js_dom` *semantic* gaps (property/attr reflection, `Range`/text-node/live-collection edge cases) — fixed root-cause in `js_dom` here (§5).

**Phase A exit:** ~1931 green under `lambda.exe js`; pass set matches the jsdom oracle (parity report).

### 4.2 Phase B — curated subset under `lambda.exe view` + `event_sim` (depth)

Run a representative subset as **event-driven UI automation** through the full Radiant stack — the `test/ui/editor4b` path generalized. This validates what `lambda.exe js` cannot: real event routing (`beforeinput` → intent → command → reconcile → caret), layout-driven geometry, coordinate hit-testing, caret/selection rendering, `selectionchange` delivery, clipboard, IME.

**Lane B coverage target (implemented as representative event fixtures — covers every distinct editing interaction; expand optionally later):**

| File | Tests | Interactions exercised end-to-end |
|---|---:|---|
| `view/full-editor-dom.ts` | 14 | toolbar (bold/italic/color, block type), **Tab/Shift-Tab** indent, **paste/copy**, **drag-reorder**, image-resize, gap caret, link popover, table col-resize |
| `view/editor-view-dom.ts` | 9 | `beforeinput` → command → **reconcile** pipeline; mount/render |
| `view/reconcile.ts` | 6 | keyed reconcile + **caret/selection preservation** across edits |
| `commands/text-commands.ts` | 41 | **type**, **Enter** (split), **Shift+Enter**, **Backspace/Delete** (+joins), **Cmd+A**, block type |
| `commands/structural-commands.ts` | 26 | Enter-in-list, list indent/outdent, split/join |
| `commands/caret.ts` | 11 | **Arrow-key** caret navigation |
| `commands/paste.ts` | 10 | **Paste** slices *(candidate to drop — overlaps full-editor-dom)* |
| `commands/input-rules.ts` | 10 | **Markdown autoformat** (typing `- `/`# ` → list/heading) |

**Interaction coverage:** typing ✓ · Enter/Shift+Enter ✓ · Backspace/Delete + joins ✓ · Cmd+A ✓ · arrows ✓ · indent/outdent ✓ · bold/italic/color ✓ · paste/copy ✓ · drag-reorder ✓ · image/link/table ✓ · autoformat ✓ · caret preservation ✓. **Expansion candidates** (add when wanted): `gap-cursor` + `inline-atom` (block/inline atoms), `dom-bridge` (selection↔path mapping), `table-cells` (deeper table ops), IME (via `event_sim ime_compose`; no dedicated file).

**How the command-file cases become event fixtures:** each command test's *(initial doc, operation, expected model)* is re-expressed as *(load/type initial content, drive the real gesture, assert on DOM/selection)*. A generated fixture per case, or a data-driven harness page that the fixture steps through — decided at Phase-B start. The `view/*.ts` files map most directly (they already mount + dispatch).

**Phase B exit:** the subset green under `lambda.exe view` + `event_sim`; `make test-radiant-baseline` unchanged.

### 4.3 Phase C — expanded Phase-A cases under `lambda.exe view` + `event_sim`

Phase C does **not** attempt to replay the full 1931-test Phase-A corpus through UI automation. Instead, it promotes the highest-risk Phase-A behaviors whose failures are most likely to require the real Radiant stack: geometry, hit testing, DOM selection projection, clipboard payloads, toolbar state, and reconciled DOM identity.

| Priority | Area | Phase A source tests | Phase C coverage |
|---|---|---|---|
| P0 | **Tables beyond resize** | `commands/table-cells.test.ts`, `commands/structural-commands.test.ts` | merge contiguous cells, split a colspan cell, retain col-resize, later add row/column buttons if the demo exposes them |
| P0 | **Gap cursor / block atoms** | `commands/gap-cursor.test.ts` | click/select block atom, show gap caret, type at gap, Enter/Delete/Backspace at gap |
| P0 | **Inline atom / mention behavior** | `commands/inline-atom.test.ts` | insert mention, delete mention as one unit, caret movement across mention as an atomic stop |
| P0 | **Paste matrix** | `commands/paste.test.ts`, `view/full-editor-dom.test.ts` | marked inline HTML, paste into empty block, selected-range replacement, multi-paragraph plain text, pasted image HTML |
| P0 | **Advanced caret movement** | `commands/caret.test.ts` | Shift+Arrow extension, word movement, cross-block movement, document start/end shortcuts where supported |
| P1 | **Block/mark toolbar breadth** | `commands/text-commands.test.ts`, `commands/set-mark.test.ts`, `commands/input-rules.test.ts` | block type select, blockquote/hr autoformat, code, strikethrough, highlight active state |
| P1 | **DOM bridge / source-path assertions** | `view/dom-bridge.test.ts` | click/select by `data-source-path`, assert editor selection path/offset once `event_sim` has a model-selection assertion |
| P1 | **Reconcile + selection preservation** | `view/reconcile.test.ts` | caret survives updates elsewhere, empty-block `<br>` transitions, sibling deletion preserves focused selection |
| P1 | **History nuance** | `model/history.test.ts` | undo/redo after multi-step paste and toolbar commands; caret-only moves stay out of history |

**C1 implementation:** `test/ui/editor4c_phase_c/*.json` is wired into `make editor-4c-view` and kept intentionally small/high-signal: richer paste, block type select, code/strikethrough/highlight, pasted image HTML, and blockquote autoformat. The active C1 fixture set is **7/7 green** standalone and green in the integrated **46/46** `editor-4c-view` run. Larger C2 work can add row/column table UI, table merge/split, gap cursor insertion/deletion, mention atom deletion, HR autoformat, and structured editor-model assertions once the demo/harness exposes those paths cleanly.

**Phase C C1 exit:** met. C1 fixtures are green under `lambda.exe view --event-file`; `make editor-4c-view` includes Phase B + Phase C C1 and remains green at **46/46**.

---

## 5. DOM-fidelity enhancements (`js_dom` / substrate)

APIs exist (§3.1); **semantics** need hardening. Surfaced by Phase A's `view/*.ts` tests (js_dom) and required by Phase B (full stack):

1. **Property ↔ content-attribute reflection.** JS *property* sets (`el.className`, `el.title`, `classList.toggle`) don't reflect to the content attribute under Radiant, so `[title=…]`/`.is-active` selectors miss (4B worked around editor-side with `setAttribute`). Make property/attribute stay in sync in `js_dom`.
2. **Layout geometry for JS-created subtrees after an edit.** Open 4B follow-up: the Phase-16 incremental-skip (`radiant/layout_block.cpp:4363-4389`) bypasses JS subtrees post-edit → orphaned toolbar geometry → coordinate-resolved `click` misses. Required for Phase B chrome clicks; needs a build+trace to reconcile "incremental-skip" vs. the "full reflow also fails" note before fixing.
3. **`selectionchange` → nested contenteditable.** Proven for *plain* CE, drops for the editor's nested CE (runtime topology; 4B chose an editor-side just-in-time sync over the native fix). Phase B cases relying on live `selectionchange` need the topology fix (retain runtime pointers on `DocState`) or the editor-side sync.
4. **`innerHTML`/parser round-trip fidelity** for `html-parser.test.ts` (already has an `innerHTML`-setter fallback) — verify round-trip parity.
5. **Whatever the `view/*.ts` tests surface** (Range boundary math, `childNodes` live-ness, text-node splitting) — discovered by running them under Phase A, fixed minimally in `js_dom`.

**Principle:** fix root causes in `js_dom`/substrate; do not weaken assertions. A *documented* per-case test-side adaptation (as 4B did for `.title`/`is-active`) is the fallback only where a faithful C++ fix is disproportionate (§11).

## 6. `event_sim` enhancements (Phase B)

`event_sim` is already rich (verified in `radiant/event_sim.cpp`): `type`, `key_press`/`key_down`/`key_up`/**`key_combo`** (modifiers), **`ime_compose`**, **`paste_text`**, `mouse_*`/`drag_and_drop`, `set_editing_selection`, `click`/`dblclick`/`tripleclick`, and assertions `assert_text`/`assert_caret`/`assert_selection`/`assert_editing_selection`/`assert_clipboard`/`assert_value`/`assert_checked`/`assert_visible`/`assert_focus`/`assert_count`. Gaps for the editor subset:

1. **Structured-model assertion.** Vitest asserts on the **serialized doc structure** (tags + marks + nesting); no current assertion covers it (`assert_text` is text-only). Add **`assert_editor_doc`** (compare a page-exposed `editor.serialize()` / `data-doc-json` against `equals`/JSON) and/or **`assert_html`** (compare surface `innerHTML`). Interim: the harness page exposes the serialized model in a DOM node and Lane B uses `assert_text`.
2. **Selection addressed by source-path.** `assert_caret` is view-tree-addressed (`view_type`+`char_offset`); the editor model is source-path-addressed. Add an assertion/harness hook to assert `{path, offset}` in editor terms.
3. **Drag-reorder & image-resize gestures** for `full-editor-dom` — verify `mouse_drag`/`drag_and_drop` produce the editor's expected reorder/resize (may need refinement).
4. **Timer/microtask draining** between an action and its assert — 4B added `js_event_loop_pump_nowait`; verify it suffices for reconcile-then-assert sequences.

---

## 7. Corpus → phase mapping

| vitest group | Tests | Phase A (`js`) | Phase B (`view`+sim) |
|---|---:|:--:|:--:|
| `commands/text-commands` · `structural-commands` · `caret` · `paste` · `input-rules` | 41+26+11+10+10 | ✅ | ✅ (pick) |
| `commands/{gap-cursor,inline-atom,move-node,set-mark,table-cells}` | 26 | ✅ | expansion candidates |
| `model/*` · `input/*` · `helpers` · `smoke` | 94 | ✅ | — |
| `drawing/*` | 50 | ✅ | — (Stage 5) |
| `view/{full-editor-dom,editor-view-dom,reconcile}` | 29 | ✅ | ✅ (pick) |
| `view/{dom-bridge,render-vnode,html-parser,use-editor-state}` | 33 | ✅ | dom-bridge is a candidate |
| tier corpora (Slate/PM/HTML/Chromium/structural/drawing) | 1601 | ✅ | — (data-driven; not gesture-expressible at scale) |
| React `.test.tsx` | 29 | ❌ | ❌ |

---

## 8. Phases & milestones

| Milestone | Status | Deliverable | Exit gate |
|---|---|---|---|
| **0 — Harness foundation** | ✅ **done** | `build-conformance.mjs` (bundle + `describe/it/expect` shim + fixture inlining); a **10-test slice** green under `lambda.exe js` | Slice green; exact ~1931 count enumerated; pipeline reproducible |
| **A1 — Logic/tier bulk under `js`** | ✅ **done** | model/commands/input/drawing + 6 tier corpora green under `lambda.exe js` | ~1866 green (207 core + 8 misc + 50 drawing + 1601 tier); jsdom parity |
| **A2 — `view/*.ts` under `js`** | ✅ **done** | the 7 plain-DOM view tests green under `js --document`; the JS-runtime bug chain (Bugs 1-5 all fixed) + tail DOM-fidelity & `Intl.Segmenter` fixes | 63/63 across the 7 view files + caret 11/11 + smoke 3/3; Phase A total 1931/1931 |
| **B0 — event_sim + geometry prerequisites** | ✅ **done for Stage 4C** | structured-model assertion (§6.1) remains DEFERRED (existing `assert_attribute` on `[data-source-path]` + `assert_text` cover the pilot); the suspected JS-subtree geometry blocker was disproven/replaced by concrete fixes for window mouseup and inline-style assignment | editor-4c-view **39/39** without structured-model assertion; add only when a future fixture needs model-level comparison |
| **B1 — Lane B subset** | ✅ **done** | `make editor-4c-view` runs `test/ui/editor4b/*.json` (13) + `test/ui/editor4c/*.json` (26, incl. drag-reorder, autoformat, link/mention, table col-resize); all former `_open_` blockers root-caused + fixed in the Radiant substrate/runtime (`state_machine` caret-projection guard, Tab keydown-first routing, `event_sim` Arrow* aliases + symbolic `view_type`, per-fixture `html`, inline-style `Object.assign`) | **39/39 pass**; table col-resize green standalone and in-suite |
| **C1 — Phase-A-to-UI expansion** | ✅ **done** | `test/ui/editor4c_phase_c/*.json` promotes high-value Phase-A cases into `event_sim`: richer paste, pasted image HTML, block/mark toolbar breadth, blockquote autoformat, block type select | **7/7 C1 pass** standalone; included in `make editor-4c-view` **46/46 pass** |
| **C2 — optional deeper Lane-B files** | ⬚ optional | table merge/split, gap cursor, mention atom delete, HR autoformat, row/column table controls if exposed by the demo, model-selection assertions for `dom-bridge`, and deeper reconcile/history fixtures | Not required for Stage 4C exit; current Lane-B subset is green under `lambda.exe view` + `event_sim` |
| **3 — Integration & parity** | ✅ **done, re-stamped 2026-07-08** | `make editor-4c-js` (Phase A), `make editor-4c-view` (Phase B + Phase C C1), `make editor-4c-parity` (Radiant↔jsdom oracle reconciliation → `Stage4C_Parity_Report.md`), `make editor-4c` (parity + view); CI wired in `run-tests-linux.sh` | **`make editor-4c` green: Phase A 1931/1931 + parity EXACT + expanded view 46/46** (1931 non-React reconcile group-for-group; 29 React `.test.tsx` excluded → 1960 oracle total) |

**Guard (every milestone):** never regress `make test-radiant-baseline` or the jsdom suite; triage every divergence to a cause (LambdaJS/`js_dom`/substrate **or** a legitimate editor difference) before accepting or fixing it.

---

## 9. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Fixture-inlining bloats the bundle / strains the retained runtime | mitigated | Phase A chunks the largest tier corpora (`tier_e_html`, `tier_0_drawing`) into deterministic sub-bundles and aggregates them back to the same parity groups |
| `js_dom` semantic gaps deeper than expected (Range/text-node/live-collection) | medium | Surfaced early by Phase A2's `view/*.ts`; fix root-cause; §11 governs the fallback |
| Geometry/orphaned-subtree (4B follow-up) blocks Phase B chrome clicks | mitigated | §5.2 was re-tested; the real blockers were window mouseup delivery and inline-style assignment, both fixed |
| Command-test → event-fixture translation is lossy (model assert vs. gesture) | medium | Use structured-model assertion (§6.1); keep those cases' authoritative form in Phase A; Phase B asserts the observable e2e result |
| Harness drifts from vitest (two sources of truth) | medium | Generate the harness *from* the same `.test.ts` files (compile, don't re-author); parity report is the gate |
| `selectionchange`-nested-CE limits some Phase B cases | low–med | editor-side just-in-time sync (shipped) or the `DocState` runtime-retention fix |
| React-under-`js` temptation | low | Explicitly excluded (§1.2) |

---

## 10. Acceptance criteria

- **Phase A:** `make editor-4c-js` runs the ~1931 plain-DOM tests under `lambda.exe js` → **0 failed**, pass set == jsdom oracle.
- **Phase B/C:** `make editor-4c-view` runs the Lane B subset plus Phase C C1 under `lambda.exe view` + `event_sim` → **46/46 passed**, covering every distinct editing interaction (§4.2 checklist) and the promoted Phase-A UI cases (§4.3).
- Both use the **real LambdaJS/Radiant stack** (no jsdom).
- `make test-radiant-baseline` and the vitest/jsdom suite remain green (no regressions from the `js_dom`/`event_sim`/substrate enhancements).
- A **parity report** cross-checks Radiant vs. jsdom pass sets; matches, or each difference is a recorded, intentional distinction. ✅ **MET** — `make editor-4c-parity` (`Stage4C_Parity_Report.md`): exact group-for-group match, the only distinction being the 29 React `.test.tsx` tests excluded by construction (§1.2).

---

## 11. Open Items

No Stage-4C-blocking open items remain.

**DOM-fidelity bar (§5).** Settled by implementation: fix `js_dom`/substrate root causes wherever feasible, keep the tests exercising real editor behavior, and reserve documented test-side adaptation only for disproportionate fidelity gaps. The latest re-stamp needed root-cause fixes instead of adaptations: deterministic Phase-A tier chunking, generated DOM harness pages, symbolic `event_sim` view-type assertions, and native inline-style `Object.assign` preservation.

---

## 12. Summary

Stage 4C has promoted the editor's own suite onto the runtime it ships on. **Phase A** is green: **1931/1931** non-React plain-DOM tests run headless under `lambda.exe js`, with exact group-for-group parity against the jsdom oracle. **Phase B + Phase C C1** is green: **46/46** curated interaction fixtures run under `lambda.exe view` + `event_sim`, including paste/copy, toolbar commands, autoformat, image resize, drag-reorder, table col-resize, block type select, blockquote autoformat, richer paste, pasted image HTML, code, strikethrough, and highlight. React remains explicitly out of scope. `make editor-4c` is green as of the 2026-07-08 Phase C C1 re-stamp.
