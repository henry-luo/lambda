# Session handoff — ES20 / F14.4 complete; F4 form submission partial

**Date:** 2026-08-28
**Design doc:** `vibe/Lambda_Design_DOM_State.md` §3.16 (ES20, staging table F14.1–F14.4)
**Status:** F14.1–F14.4 are implemented. The F4 local/package submission path is
landed and verified; POST body transport through the browsing layer remains open.
The current tree is verified below; changes remain uncommitted.

---

## 1. What ES20 asks for, and where F14.1 sits

ES20 redesigns contenteditable onto two pillars: (1) UA default contenteditable
support — `execCommand` included — lives in the dom package as the one
implementation; (2) JS editors are plain JS pages whose `preventDefault`
suppresses the package default. The editing-transaction pipeline retires
outright.

The staging table in §3.16:

| Stage | Content | State |
| --- | --- | --- |
| F14.1 | `commands.ls` + the `execcommand` dispatch; migrate `insertHTML` off the native bridge; formatting on single-text-node ranges via the wrap primitive | **implemented this session** |
| F14.2 | structural primitives; cross-node delete and replacement; adjacent-block merge; `insertParagraph`/`insertLineBreak` block handling | **implemented this session** |
| F14.3 | JS editors on the plain event path: retire the prepared-transaction wrapper, keep the live-host guard | **implemented this session** |
| F14.4 | delete the §3.16 retire list; `edit` templates take `beforeinput` as ordinary handlers; audit `text_edit.cpp` residue | **implemented this session** |

**F14.4 removes the remaining compatibility machinery.** JS pages and Lambda
`edit` templates now use ordinary `beforeinput`/default/`input` ordering. The
prepared route, ownership snapshot/result types, template compatibility file,
rich-edit state schema, and native `insertHTML` fallback are gone.

---

## 2. What landed — file by file

### 2.1 Native waist (`radiant/editing_dom_waist.cpp`)

Four new primitives plus the `execCommand` entry point. All are **mechanism**;
every decision (which tag, toggle on or off, which commands exist) is in
`commands.ls`.

| Function | Purpose |
| --- | --- |
| `editing_dom_format_ancestor(host, node, tag)` (static) | the `tag` element between `node` and the editing host. Stops at the host: a command toggles off only what it would itself have created |
| `dom_edit_range_in_format(state, tag)` | ES16 — formatting state read off the tree, never cached. Backs the toggle decision and, later, `queryCommandState` |
| `dom_edit_wrap_range_u16(state, s, e, tag)` | the first **structural** waist op. Splits the tail, splits the head, wraps the middle. Reselects the wrapped run |
| `dom_edit_unwrap_range_u16(state, s, e, tag)` | promotes a whole selected formatting run and, for a one-text-child shell, splits around a partial selection and promotes its middle; nested/multi-child/cross-node shapes still decline |
| `dom_edit_insert_html(state, html)` | calls `js_dom_exec_insert_html` (un-`static`'d in `js_dom.cpp`) |
| `radiant_dom_exec_command(document, command, value)` | the `execCommand` entry. Resolves the live selection like ordinary edit dispatch, stashes the pending range, dispatches `execcommand`, reads back epoch + verdict |

`dom_text_split_at` (in `radiant/dom_range.cpp`, pre-existing) is what makes wrap
cheap — it already does the split-and-adjust-ranges envelope.

**Bug fixed on the way (independent of F14.1):** `dom_edit_set_pending_range` did
not clear `s_dom_edit_caret_node` / `s_dom_edit_caret_u16`. A primitive that
moves the apply epoch *without* placing a caret — which is exactly what a wrap
does — would have let the dispatch collapse the selection into the **previous**
edit's node. Wrapping is the first operation that could expose it. The
clear is now in `dom_edit_set_pending_range`, where it belongs regardless.

**F14.4 ownership fix:** a synchronous package load may use the already-bound
caller `EvalContext` when the document has not retained its script realm yet.
The document never stores that stack-owned runtime. Runtime cleanup also skips a
Script-owned type-list alias and lets the owning script release it, avoiding the
teardown use-after-free found in the CLI `execCommand` golden. This preserves
**D5.4.1**'s one canonical context and **D7.2.2**'s package initialization
barrier.

**F14.2 structural range slice.** The pending edit now retains both raw
DOM endpoints in addition to the single-text-node cache. The waist exposes
`dom_replace_dom_range`, `dom_delete_dom_range`, `dom_insert_paragraph`, and
`dom_insert_line_break`; cross-node deletion uses `Range.deleteContents()`,
merges adjacent same-tag paragraph-like blocks, and replacement inserts at the
surviving original start boundary. Structural moves go through the existing
mutation envelope and JS mutation ledger (S12.2.2). Paragraph splitting keeps
the authored block tag; line breaks split a text node when needed and insert a
`<br>` at the resulting boundary. Every new module primitive has at most four
arguments.

### 2.2 Event plumbing

- `radiant/event.hpp` — `InputIntent` gains `const char* command`. Null for every
  intent but an execcommand; the legacy surface is **not** the WHATWG
  `beforeinput` vocabulary, so it does not fold into `type`. Also the new waist
  decls and `radiant_dom_exec_command`.
- `radiant/editing_intent.cpp` — `command` wired through ctor / `input_intent_clone`
  (borrowed pointer, same as `data_mime`) / `input_intent_reset`.
- `radiant/event.cpp` —
  - `radiant_dispatch_behavior_exec_command(target, intent)`: behavior-only and
    context-free, alongside `keyintent`/`domedit`. `execCommand` is a method
    call, not an event, so there is no EventContext and nothing has been
    preventDefault'd ahead of it.
  - `build_lambda_event_map` emits `command` and `value` **outside** the
    intent-type guard (same place `option_index` sits).

### 2.3 Module primitives (`lambda/module/radiant/radiant_module.cpp`)

`dom_range_format`, `dom_wrap_range`, `dom_unwrap_range`, `dom_insert_html`,
`dom_replace_dom_range`, `dom_delete_dom_range`, `dom_insert_paragraph`, and
`dom_insert_line_break` are registered in the fn table with signatures. All
**≤ 4 arguments**: §3.15's arity ceiling is real (a 5-argument registration
SIGSEGVs during MIR compilation of an unrelated handler, with no diagnostic).
`radiant_dom_format_bounds()` converts codepoint→UTF-16 against the resolved
node's text **before** any split, because the splits invalidate the offsets.

### 2.4 Package (`lambda/package/dom/`)

- **`commands.ls` (new).** `canonical(name)` maps the legacy execCommand name
  (case-insensitively — the spec defines them that way) to the WHATWG intent the
  keyboard path already produces. `format_tag(intent)` picks the element.
  `toggle_format` reads state off the tree and wraps or unwraps.
  `run(host, intent, value)` is the one applier. `exec(host, evt)` is the
  execcommand entry.
  **Named `run`, not `apply`: `apply` is a reserved keyword** (template matching).
  Implemented commands: `bold`, `italic`, `underline`, `strikeThrough`,
  `insertHTML`, `insertText`. Everything else declines and `execCommand` reports
  false — the same answer `queryCommandSupported` gives.
- **`dom_edit.ls`** — one `else if (commands.is_format(t))` branch delegating to
  `commands.run`, placed after the delete branch.
- **`form.ls`** — `<body>` gains `on execcommand(evt) { commands.exec(~, evt) }`.

### 2.5 JS bridge (`lambda/js/js_dom.cpp`)

- `js_dom_exec_insert_html` un-`static`'d, `extern "C"` — it is now the mechanism
  behind `radiant.dom_insert_html`, not a private bridge helper.
- `js_dom_document_exec_command_bridge` rewritten: it converts arguments and
  calls `radiant_dom_exec_command`; no native command fallback or
  contenteditable-presence gate remains in the bridge.

### 2.6 Plain JS editor path (F14.3/F14.4)

`radiant/event.cpp` now routes JS-owned rich editing through one ordinary
dispatch sequence:

```
beforeinput → live-host guard → dom package default → input
```

The package default is offered only when `beforeinput` was not canceled. The
host is resolved again after the listener returns, so a listener that detaches
or replaces the editable host cannot cause the old DOM to be mutated. The
Lambda `edit` templates use the same ordinary handler path; their return verdict
is the author decision, with no prepared target snapshot or route/result side
channel.

Composition preserves the document-scoped native session and refreshes its
anchor from the range start after each package replacement. This matters when
the IME caret is inside the preedit or when a new composition starts after a
commit. The resulting DOM mutations still use the package waist and its
mutation envelope, consistent with **S12.2.2**.

---

## 3. The keyboard half needed no native change (the surprise worth knowing)

The formatting intents are `input_intent_is_dispatchable() == false`, so they
fire no `beforeinput`. **The ordinary event path still offers them to
`domedit` directly.** Trace:

```
keydown → input_intent_from_key_event → keymap.ls returns "formatBold"
        → dispatch_contenteditable_event
        → radiant_dispatch_behavior_dom_edit("domedit")
        → dom_edit.ls, evt.input_type == "formatBold"
```

So Cmd+B has been arriving at `dom_edit.ls` all along and falling through to
`'pass'`. One `else if` is the whole keyboard path — and it reaches the *same*
applier `execCommand` does, which is what the one-rule-set property actually
requires. **Do not make the format intents dispatchable** to achieve this; it is
unnecessary and would change `beforeinput` firing for text controls too.

---

## 4. F14.4 ownership and retirement seam

The package-less `lambda.exe js --document` golden exposed the final ownership
bug. Its caller has a live `EvalContext` during synchronous package loading, but
the document must not retain the caller's stack-owned `Runtime*`. The loader now
uses that context only for the call. During reset/cleanup, a runtime whose
`type_list` is published by a live Script skips direct release; the owning script
cleanup releases it exactly once. The native bridge can therefore be deleted
without reintroducing teardown ownership or a fallback implementation.

This follows **D5.4.1** (one canonical context) and **D7.2.2** (package init is a
transaction barrier). The page command regression remains byte-identical, while
unsupported commands continue to decline through the package verdict.

---

## 5. Verification status — current tree

**Verified focused gates:**

- `make build` — 0 errors, 0 warnings on the final incremental build (the
  earlier full rebuild only reported existing toolchain warnings).
- `make lint ARGS='--rule ^no-int-cast-radiant$'` — clean.
- **New UI test `test/ui/test_editing_contenteditable_commands.json` — 9/9 pass**
  (`test/html/editable-dom-commands.html`). Covers: execCommand('bold') wraps →
  toggles off; Cmd+B produces the identical tree through the same applier;
  insertHTML through the package; an unsupported command reports false; and
  toggling bold on the latter half of `<b>seedtext</b>` produces
  `<b>seed</b>text`, and also covers head and interior selections.
- **New UI test `test/ui/test_editing_contenteditable_structural.json` — 7/7 pass**
  (`test/html/editable-dom-structural.html`). Covers cross-paragraph
  replacement and delete/merge, `insertParagraph`, `insertLineBreak`, and
  keyboard Enter, Shift+Enter, and Backspace block joining.
- `make editable-unit` — all six focused fixtures pass, including both new
  F14.2 fixtures.
- `make editable-ui` — all seven listed contenteditable/editor fixtures pass;
  the composition fixture is 5/5 after the F14.3 anchor fix.
- **New UI test `test/ui/test_editing_contenteditable_plain_event_guard.json` —
  3/3 pass** (`test/html/editable-plain-event-guard.html`). A JS
  `beforeinput` listener replaces the host; the old host receives no package
  mutation or `input` event.
- CodeMirror, ProseMirror, and Editor.js editor fixtures pass when run as
  individual processes. The CodeMirror and ProseMirror full fixtures are 14/14
  each; all 11 Editor.js fixtures also pass individually.
- `./test/test_ui_automation_gtest.exe --gtest_filter='*editable_editors_*:*test_editing_contenteditable_*:*editable_mixed_routes:*editable_template_gate'` — 39/39 selected editor/contenteditable cases pass, including the new F14.3 guard.
- `test/js/dom_exec_command_insert_html` — byte-identical to its golden.
- `test/ui/test_editing_contenteditable_dom_action.json` (the F13 test) — 7/7.
- `./test/test_js_gtest.exe` — 356/357 pass on the current tree. The one
  failure is the existing `JavaScriptTests/JsFileTest.Run/dom_module_props`
  live-NodeList length mismatch (`length` stays stale while indexed lookup
  already sees the appended node); it reproduces in isolation and is outside
  the F14.4 edit/command path.
- `./test/test_ui_automation_gtest.exe` — 309 pass, 7 fail, 2 skip. The failures
  are the existing `editable_drawing_{jointjs,maxgraph,raphael}`,
  `js_loop_listener_bundle_contract`, `pdf_text_selection_copy`,
  `test_form_state_drag`, and `test_form_textarea_scrolled_hit_test` cases;
  all contenteditable/editor cases in the full run pass; the current focused
  command test is 9/9.
- `make test-radiant-baseline` — the dedicated DOM UI (65/65), Radiant View
  (24/24), and WPT DOM2 (20/20) pass. All recorded required layout baselines
  pass except the existing one-test CSS-text regression. The aggregate remains
  red on existing debt: layout 6917 passed / 50 failed / 329 partial / 7
  skipped, UI 309/7, page-load 104/1, and render 202/212 with 1 baseline
  regression.

The aggregate `make editable-editor-e2e` target still exits with SIGSEGV after
the first three Editor.js commands even though those commands and the remaining
Editor.js fixtures pass individually; this is an order-dependent harness/runtime
failure, not a fixture assertion failure. No editor fixture is being skipped or
masked.

---

## 6. How to continue

### 6.1 F14.4 is closed

The F14.4 retire list is now implemented. Lambda `edit` templates receive
ordinary `beforeinput` events and return the normal handler verdict. The native
template handler file, prepared route/result machinery, rich-edit FSM/schema,
transaction snapshot serialization, `insertHTML` bridge fallback, and legacy
command gate are removed. `text_edit.cpp` remains only for live form-control
history, UTF conversion, word/line scanning, replacement, and IME commit
helpers. The paired false-island fixture also asserts that no
`editing.transaction` record is emitted.

### 6.2 F14.3 is closed

JS-owned contenteditable now follows `beforeinput` → live-host guard → package
default → `input`. The package default is suppressed when `beforeinput` is
prevented, and a host replaced by a listener is rejected before any package
mutation. Composition retains its document-scoped native session; its anchor is
recorded from the replaced range start, so an IME caret inside a preedit and a
new composition after commit both replace the intended run.

### 6.3 F14.2 is closed

The F14.2 package path and native waist are now verified for cross-paragraph
replacement and delete, same-tag adjacent-block merge, paragraph splitting,
line-break insertion, and the corresponding keyboard paths. The structural
range selection is used even when the live caret itself did not change: that is
what makes Backspace at the start of a block reach the cross-node target range.
The implementation does not broaden partial unwrap beyond its deliberate
single-direct-text-child shape.

### 6.4 F14.2 — the structural primitives (the real cost)

§3.16: "split node at offset, wrap/unwrap a range in an element, remove a
cross-node range, merge adjacent blocks, insert a parsed fragment — each ≤ 4
arguments, each pure mechanism with the decision of *when* in the package."

Of those, **split** (`dom_text_split_at`), **wrap**, **whole-element unwrap**,
**partial unwrap**, **cross-node range deletion/replacement**, **adjacent-block
merge**, **paragraph/line-break splitting**, and **parsed-fragment insertion**
now exist. Partial unwrap remains deliberately bounded to a formatting element
with one direct text child; nested, multi-child, and cross-node unwrap shapes
remain future work rather than being half applied.

### 6.5 Hazards to carry forward (§3.16, all previously paid for once)

- **The `domedit`/`execcommand` dispatch is a genuine default action**, so
  `default_prevented` must suppress it — unlike `keyintent`, which is a
  *translation* and must survive `preventDefault`. Getting this backwards
  silenced every JS editor in F11's first attempt; the inverse would
  double-apply every edit a JS editor handles.
- **Resolve dispatch targets from the live tree**, never from
  `active_surface.view` (superseded render generation). `radiant_dom_exec_command`
  does this via `editing_host_lookup(range->start.node, …)`.
- **The waist arity ceiling is four.** A 5-argument module primitive miscompiles
  into a SIGSEGV in an unrelated handler with no diagnostic. §3.15 notes this is
  a runtime defect worth fixing on its own (a 5-arg registration should be
  rejected at load with a message) — still open, still unfixed.
- **Two channels, not one**: apply epoch = "an edit happened"; dispatch verdict
  (`'prevent-default'` vs `'pass'`) = "handled, no change". `queryCommandState`
  and a declined toggle both need the second. Do not invent a third.

### 6.6 Useful commands

```bash
make build
./lambda.exe view test/html/editable-dom-commands.html \
  --event-file test/ui/test_editing_contenteditable_commands.json --headless --no-log
./lambda.exe view test/html/editable-dom-structural.html \
  --event-file test/ui/test_editing_contenteditable_structural.json --headless --no-log
./test/test_ui_automation_gtest.exe --gtest_filter='*contenteditable_commands*'
diff <(./lambda.exe js test/js/dom_exec_command_insert_html.js \
  --document test/js/dom_exec_command_insert_html.html 2>/dev/null) \
  test/js/dom_exec_command_insert_html.txt
```

To prove a package path is load-bearing rather than merely coincident, keep the
native bridge as a thin caller and verify both entry points against the same
package implementation. The command UI fixture and the JS CLI golden now do
that: page `execCommand` and keyboard commands share `commands.ls`, while the
CLI path also exercises the synchronous package-load ownership seam.

---

## 7. Changed files

```
 M lambda/js/js_dom.cpp                      exec_command bridge → package; un-static insert_html
 M lambda/module/radiant/radiant_module.cpp  4 primitives + fn-table rows
 M lambda/package/dom/dom_edit.ls            delegate format intents to commands.run
 M lambda/package/dom/form.ls                <body> on execcommand
 M radiant/editing_dom_waist.cpp             wrap/unwrap/format-state/insert-html + exec entry; caret-channel clear; partial unwrap
 M radiant/editing_intent.cpp                InputIntent::command and edit payload ownership
 M radiant/event.cpp                         execcommand dispatch + ordinary editing event path
 M radiant/event.hpp                         InputIntent, waist, and retired transaction state removal
 M radiant/editing_dispatch.cpp              live-host guard; prepared transaction path removed
 M radiant/state_machine.cpp                 retired rich-edit validators and snapshot fields removed
 M radiant/state_schema.cpp                  retired rich-edit FSM/rules/invariant bindings removed
 M test/dedup/exclude.json                   remove the deleted rich transaction phase region
 M Makefile                                  add command, structural, and plain-event fixtures
 M vibe/Lambda_Design_DOM_State.md           §3.16 F14.1–F14.4 status + formal ownership findings
?? vibe/Lambda_Session_ES20.md                this updated ES20 handoff
?? test/html/editable-plain-event-guard.html F14.3 host replacement fixture
?? test/ui/test_editing_contenteditable_plain_event_guard.json  NEW — 3 assertions
?? lambda/package/dom/commands.ls            NEW — the command set
?? test/html/editable-dom-commands.html      fixture + partial-unwrap case
?? test/ui/test_editing_contenteditable_commands.json  NEW — 9 assertions
?? test/html/editable-dom-structural.html    NEW — structural range fixture
?? test/ui/test_editing_contenteditable_structural.json  NEW — 7 assertions
```

Nothing is committed. The implementation and verification updates are all in
the current worktree; preserve unrelated worktree changes when continuing past
F14.4.

---

## 8. Current continuation: F4 form submission (2026-08-28)

**Status:** 🟡 local/package path implemented; browsing-layer POST transport remains open.

F4 now has one package-owned policy path for native pointer activation, keyboard
Space/Enter activation, implicit Enter in a single-line text control, and
script-created submit/reset clicks. `form.ls` owns activation dispatch and
`submit.ls` owns validation gating, the cancelable `submit` event, submitter
overrides, GET/urlencoded/multipart serialization, and the navigation handoff.
The native side remains the mechanism waist: form ownership, the successful
control entry walk, validity/invalid dispatch, focus-first-invalid, reset
mutation, and browsing target resolution.

The entry walk now includes controls associated with a form through
`form="..."`, not only descendants. The focused fixture proves submitter
propagation, cancelation, reset, implicit Enter, invalid/focus-first-invalid,
and the encoded GET trace:

```text
FORM_NAVIGATION_HANDOFF method=get enctype=application/x-www-form-urlencoded target=_self url=/serialized?existing=1&outside=associated&first=hello+world&second=a%2Bb&submit=go body=
```

The remaining boundary is deliberate: `radiant.request_navigation` records the
serialized request and calls the existing browsing target resolver, but that
resolver currently accepts only URL/target. POST method/body delivery therefore
is not claimed complete. This keeps policy in Lambda and transport in the
browsing layer, in line with **S12.1.3**, **S12.2.2**, **D7.2.1–D7.2.3**, and
the package/default-action placement decisions **ES5**, **ES10**, and **ESO49**.

### F4 verification

- `make build` — 0 errors.
- `make form-ui` — rebuild completes with 0 errors/0 warnings and 8/8 focused assertions pass.
- Existing `test/ui/test_form_input_typing.json` — 6/6 assertions pass.
- Existing WPT Form gate — 399 cases; 103 pass, 110 skipped, 180 tolerated
  failures, and 6 baseline regressions. Each of those six reproduces with
  `RADIANT_DOM_PKG=0` and `=1`, so none is attributable to this F4 package
  path; the run also contains existing child-runtime crashes.
- `git diff --check` — clean.

The form test still reports a small shutdown memtrack leak from the current
runtime; it does not affect the 8/8 event assertions and is not hidden by the
focused gate.
