# Lambda DOM Default Actions — the UA behavior ledger

> **Status**: **normative for default-action placement and status** (2026-08-28). The architecture is decided (ES5, ES10, ES15, ES20); what this document adds is the complete per-event ledger and the gap inventory that drives it.
> **Role**: this is the **single source of truth for what Radiant does after an event is dispatched** — which UA default actions exist, where each half lives, and which are still missing. Every other doc that names a default action points here rather than restating it. Absorbs and replaces Appendix B of `vibe/Lambda_Design_DOM_State.md` (deleted there 2026-08-28).
> **Scope**: default actions and activation behavior for HTML documents under Radiant — the pointer, keyboard, editing, focus, clipboard, drag, composition, form, and navigation families. Event *dispatch* mechanism is in scope only where a missing dispatch is what makes a default action unreachable.
> **Companion docs**: `vibe/Lambda_Design_DOM_State.md` (the behavior-template architecture and the ES/ESO ledgers this doc extends), `vibe/Lambda_Design_DOM_Pkg.md` (layering and placement policy), `doc/dev/radiant/RAD_15_Events_Input.md`, `RAD_17_Interaction_State.md`, `RAD_19_Form_Controls.md`.
> **Formal anchors**: S12.1.3 (reactive templates: body = pure `fn`, mutation only in `on` handlers), S12.2.2 (element mutation), S9.1.4 (state lives in view state), S7.6/S7.10 (error discharge and the sys-func contract), D4.5.1v3 (the Radiant memory seam).
> **Ledger series**: this doc extends the DOM-State area's existing `ES#` (decisions) and `ESO#` (open issues) series per `doc/Doc_Convention.md` §4 — it mints no new series. New open issues start at **ESO63** (ESO48-ESO62 are minted below).

---

## 1. What this document is

### 1.1 Purpose

Radiant's UA behavior is split across three code homes — the native engine, the `lambda/package/dom` behavior templates, and the JS realm's own dispatch layer — and until now no single place said which of them owns a given default action, or whether one exists at all. The result is a class of bug that is invisible from any one home: a behavior implemented twice in two homes that quietly disagree (the F1 double-toggle, the F11b keyboard/mouse split, `radiant_uncheck_radio_group`'s shadow copy of radio exclusivity), and a behavior implemented in *no* home that every reader assumes lives in another (form submission, `:target`, contenteditable's Enter key).

This ledger closes both. §3 lists every event class with an entry per half — dispatch and default action — and a status. §5 lists the cases where an implementation exists but does not follow the spec, which are more dangerous than the absences because they fail silently on ordinary pages. §6 carries the open issues.

**Rule going forward**: a default action is *not implemented* until it has a row here. Adding one to the code without adding its row is how the duplication class returns.

### 1.2 Three spec concepts, deliberately kept apart

The word "default" covers three different things, and conflating any two of them has already cost a debugging session each:

- **Event dispatch** — the spec-visible half: the event object JS listeners receive, its propagation path, its `cancelable` flag. Pure mechanism.
- **Default action** — what the UA does *after* dispatch unless script canceled it (DOM Standard; UI Events). Never itself observable as an event. **Must die with `preventDefault`.**
- **Activation behavior** — the DOM Standard's name for the click-triggered default actions HTML defines per element: a checkbox toggles, a radio selects and clears its group, a submit button submits, a link navigates. A *subset* of default actions with its own pre/post-activation staging and its own cancellation rule (canceling `click` reverts pre-activation).

A fourth thing looks like a default action and is not:

- **Translation** — a derivation from one event into another vocabulary, e.g. key → `inputType` (`keyintent`). It carries no side effect of its own, so it **must survive `preventDefault`**: a JS editor that cancels the keydown still relies on the intent to decide what to do. Getting this backwards silenced every JS editor in F11's first attempt; the inverse mistake (making a real default action survive cancellation) double-applies every edit a JS editor handles.

The suppression column in §3.11 is the load-bearing expression of this distinction.

### 1.3 Placement: which half is mechanism, which is policy

Per ES5 and the DOM_Pkg placement rules, the seam runs between *resolving* and *deciding*:

| Half | Home | Examples |
| --- | --- | --- |
| **Mechanism** — hit-testing, geometry, storage, buffers, paint, association lookup | **native (N)** | transform-aware hit test; `for="id"` label association; dropdown overlay geometry; the UTF-8 splice; caret geometry; clipboard read; the undo ring's storage |
| **Policy** — which key means what, what a click does to a control, what an `inputType` does to the document | **dom package (L)** | `form.ls` activation, `caret.ls` key→operation, `keymap.ls` key→intent, `editing.ls` / `dom_edit.ls` appliers, `commands.ls` command set, `menu.ls`, `ime.ls`, `validate.ls`, `aria.ls` |
| **Waist primitives** — the named, ≤4-argument operations policy drives mechanism through | **`radiant` module (M)** | `set_state`, `dispatch`, `radio_group`, `replace_range`, `dom_replace_range`, `dom_wrap_range`, `caret_operation` |

The recurring failure mode is a default action that ends up in a **fourth** home — the JS dispatch layer (`lambda/js/js_dom_events.cpp`), which implements activation behavior for `dispatchEvent`'s benefit. That home is legitimate for JS-initiated activation but becomes a second implementation when the native pointer path also routes through it (§5.2, ESO49).

---

## 2. Architecture

### 2.1 The pipeline

Dispatch order for a discrete event on a target element (ES5, conforming to DOM_Pkg decision 2):

```
[N] hit test → target path → trusted event construction
[N] 3-phase dispatch: JS listeners (capture / target / bubble)
[N] app-template handler dispatch (reverse render-map path — author level)
      │  either layer may cancel: defaultPrevented / 'prevent-default'
      ▼
[N] defaultPrevented? → done
[L] BEHAVIOR DISPATCH — at most one Lambda call per discrete event:
      walk target→root; first element whose behavior entry declares this
      event type wins (most specific template for that element)
[M] handler effects via waist primitives only
[N] cascade settle → restyle / reflow / repaint scheduling
```

Three properties hold it together:

- **One-call ceiling and hot-path guard.** A native per-event-type bitmask keeps `mousemove` / `pointermove` / `scroll` / `wheel` out of behavior dispatch unless a loaded template declares such a handler.
- **Handler verdicts are return values** (ES15): `'pass'` declines — the walk continues and the native fallback for that class stays in charge; `'prevent-default'` claims and suppresses the remaining default actions; any other return means claimed.
- **Fallback until claimed** (ES5/ES7): each native default-action block is guarded by `radiant_behavior_claims_event`, so behavior is untouched until the package registers, and `RADIANT_DOM_PKG=0` disables the package wholesale. Note the retirement direction: once a class is proven state-equivalent, the *native* half is deleted (F1b/F2b/F3), and after that `'pass'` means **nothing happens** — see §5.6.

### 2.2 Two realms, one canonical state

Per ES10, JS and Lambda are parallel peers over one canonical state store; neither gatekeeps the other, and **only one of them may perform the default action for a given event**. The migrated activation classes use the behavior claim protocol to make that ownership explicit; remaining legacy coordination and the un-migrated popover path stay in ESO49.

### 2.3 Behavior-only hooks

Radiant-internal seams that no JS listener can observe. Each exists because the spec concept it implements has no event of its own, or must run at a moment the event pipeline does not expose. Full table with suppression semantics: §3.11.

### 2.4 Status vocabulary

| Mark | Meaning |
| --- | --- |
| ✅ | implemented and believed spec-conforming |
| 🟡 | partial — one half present, or a documented subset |
| ⚠️ | implemented but **diverges** from the spec in a way that misbehaves on ordinary pages (see §5) |
| ❌ | not implemented on any path |
| — | no default action exists per spec |

"Not implemented on any path" is literal: no native code, no package template, no JS-layer implementation.

---

## 3. The ledger

Verified against the tree at 2026-08-28 (`event.cpp` 500KB, `lambda/package/dom/*.ls`, `lambda/js/js_dom_events.cpp`). Anchors are `file:line` at that revision — treat them as pointers to the right neighborhood, not as stable addresses.

### 3.1 Input & editing

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `beforeinput` | Input Events L1/L2; cancelable except `insertCompositionText` / `deleteCompositionText` | UA updates the DOM as described by `inputType` | dispatched through the ordinary JS/author path; the package supplies the default — `editing.ls` splices text controls (F5/ES9), `dom_edit.ls` splices contenteditable through the DOM-range waist (F13). Prevented ⇒ the package default is not invoked (ES20/F14.3–F14.4) | ✅ text controls · ✅ contenteditable (§4) |
| `input` | Input Events; not cancelable | none — reports a mutation that already happened | dispatched post-mutation from the one engine path that applied the edit; package `on input` re-derives `:valid`/`:invalid` and the ARIA mirrors | ✅ |
| `change` | HTML; not cancelable | none | the *decision* is the behavior-only `commit` hook before blur (ESO42); native fires the event so it precedes `blur` for JS and templates alike | ✅ |
| `select` | HTML; not cancelable | none | **never dispatched.** `script_runner.cpp:2597` registers the `onselect` content attribute, so pages install a handler that can never fire | ❌ dispatch |

### 3.2 Keyboard

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `keydown` | UI Events; cancelable | text input, caret movement, scrolling, activation via Space/Enter — "the key processing model" | dispatched to JS first, then split by kind (below) | 🟡 — see the four rows |
| ↳ **`caretkey`** (default action) | — | caret movement | dispatched *with* context, so a prevented keydown suppresses it → `caret.ls`, both surfaces. Arrow / Home / End only; **PageUp/PageDown absent** | 🟡 |
| ↳ **`keyintent`** (translation) | — | key → `inputType` | dispatched context-free, deliberately (F11) → `keymap.ls` | ✅ |
| ↳ **`dropdownkey`** | — | UA handling of an open `<select>` popup | Up / Down / Enter / Escape → `form.ls`. **No typeahead** | 🟡 |
| ↳ **document scrolling** | — | Space / PageUp / PageDown / Home / End / arrows scroll the nearest scrollport | **absent for HTML.** Scroll arrives only from wheel (`RDT_EVENT_SCROLL`), scrollbar drag, and drag-autoscroll; page keys are handled only inside `<textarea>` (`event.cpp:10151`) and the PDF viewer. `key_code_to_name` (`event.cpp:1505`) has no `PageUp`/`PageDown` case, so JS also sees `event.key === ""` for them | ❌ (ESO48) |
| ↳ **Space/Enter activation** | — | activate the focused element | `input[type=checkbox]` / `input[type=radio]` (Space), `button`, `select` — all routed through the same dispatch the mouse path uses (F1b). **`<a href>` is not covered**: Enter on a focused link does nothing | 🟡 ⚠️ (§5.4, ESO51) |
| `keyup` | UI Events; cancelable | none meaningful | dispatched; no package involvement | ✅ |
| `keypress` | legacy, deprecated | — | not dispatched, deliberately. `script_runner.cpp:2590` still registers `onkeypress`, so the attribute is inert rather than absent | — (dead attribute) |

### 3.3 Pointer & activation

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `mousedown` | UI Events; cancelable | begin selection, focus change, drag preparation | transform-aware hit-testing native (ESO47); `selectstart` dispatched at selection begin; focus transition via the state machine. **Also performs link navigation, which belongs on `click`** | ✅ dispatch · ⚠️ default (§5.1) |
| `mouseup` / `click` | UI Events; cancelable; canceling `click` cancels **activation behavior** | element-specific activation (HTML) | activation is the package's for checkbox / radio / `<select>` open-close, with no native fallback (F1b/F2b). Canceled activation restores pre-click checkedness. Label association lookup stays native (`for=` is not an ancestor walk); the dispatch is retargeted | 🟡 — per element, see §3.9 |
| dropdown option click | no spec event — the popup overlay is not DOM | — | native geometry resolves the row; behavior-only **`optioncommit`** carries the index; `form.ls` commits and closes (F2c). One commit path shared by pointer, Enter, and the test harness | ✅ |
| `dblclick` | UI Events; cancelable | UA convention: word selection | word/line/select-all selection implemented natively via click counts (`event.cpp:8807`). **The `dblclick` event itself is never dispatched**, and `ondblclick` is registered at `script_runner.cpp:2582` — another inert attribute | 🟡 default · ❌ dispatch |
| `contextmenu` | UI Events; cancelable | show the UA context menu | right-click dispatches the behavior-only hook; `menu.ls` decides target + enable mask (F10), native paints the popup. **The spec-visible JS event is not dispatched** — a page cannot `preventDefault` it or build its own menu | ✅ default · ❌ dispatch |
| `mousemove` / `over` / `out` / `enter` / `leave` | UI Events | none | dispatched; hover state native; hot-path gate keeps the package out of per-frame dispatch | ✅ |
| `wheel` | UI Events; cancelable | scroll | dispatched before the native scroll (`event.cpp:9527`), hot-path gated. **Ctrl+wheel zoom not implemented** (browser-chrome level; noted, not tracked) | ✅ |
| `pointerdown` / `pointerup` / `pointermove` | Pointer Events L2; cancelable | as their mouse equivalents | **dispatched** as a compatibility stream alongside the mouse events, with `preventDefault` honored (`event.cpp:8530`, `8933`, `8025`) — added because JS drag libraries select the pointer stream when `PointerEvent` exists | ✅ |
| `pointerover` / `out` / `enter` / `leave` / `cancel`; `setPointerCapture` | Pointer Events L2 | boundary + capture semantics | absent | ❌ (ESO50) |
| touch events | Touch Events | — | absent | ❌ |

### 3.4 Focus & selection

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `focus` / `blur` / `focusin` / `focusout` | UI Events; not cancelable | none (focus already moved) | focus machinery and `:focus` / `:focus-within` / `:focus-visible` native; package `on blur` revalidates; `commit` runs before the blur decision | ✅ |
| Tab / Shift+Tab | HTML sequential focus navigation | move focus in tabindex order, scroll the new target into view | `focus_move` walks `collect_focusable` in **plain DOM order** (`state_store.cpp:7897`) — positive `tabindex` is not ordered ahead of `tabindex=0`; and focus never scrolls the target into view. JS gets `focusin` so focus traps work | ⚠️ (§5.3, ESO52) |
| `autofocus` processing | HTML | focus the first `autofocus` element in tree order | `cmd_layout.cpp:4116` finds the **first `<input>`** and tests *that one* for the attribute — `autofocus` on a `<textarea>`, `<select>`, `<button>`, or any later input is ignored | ⚠️ (§5.5) |
| `selectstart` | Selection API; cancelable | begin a selection | dispatched when a pointer selection begins; selection storage is `DocState::sel` (document-scoped) | ✅ |
| `selectionchange` | Selection API; not cancelable | none | dispatched from the selection projection | ✅ |

### 3.5 Clipboard & drag-and-drop

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `copy` | Clipboard APIs; cancelable | place selection on clipboard | native — copy has no `beforeinput` intent (nothing is input), so it never enters the package | ✅ |
| `cut` | Clipboard APIs; cancelable | copy + delete selection | copy half native; the deletion arrives as `deleteByCut`, claimed by `editing.ls`. On non-editable text: full no-op, matching browsers. **Not claimed on contenteditable** (§4) | ✅ text controls · ❌ contenteditable |
| `paste` | Clipboard APIs; cancelable | insert clipboard content | payload fill native (clipboard read is mechanism); insertion arrives as `insertFromPaste` — newline sanitization and `maxlength` in `editing.ls`. **Not claimed on contenteditable** (§4) | ✅ text controls · ❌ contenteditable |
| `dragstart` / `dragover` / `drop` / `dragend` | HTML DnD; cancelable (`dragover`'s default is *rejecting* the drop — `preventDefault` enables it) | move/copy the dragged content | drag geometry and range tracking native; `<img>` and `<a href>` are draggable by default; edits arrive as `deleteByDrag` + `insertFromDrop`, claimed by `editing.ls`. **Text drag-and-drop is reachable from real mouse input since F16/ES21** — a press inside a text control's selection drags it, a text control is an implicit drop target, and the drop calls the same applier the harness drives. Element DnD still requires the author `dropzone` attribute, which is not HTML5 DnD. **Not claimed on contenteditable** (§4) | ✅ text controls · ❌ contenteditable |

### 3.6 Composition (IME)

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `compositionstart` | UI Events; cancelable | begin the composition session | session policy in `ime.ls`, bound to document-scoped IME state (ES18); the claim-without-edit is expressed through the verdict channel | ✅ |
| `compositionupdate` | UI Events; not cancelable | update the preedit | preedit storage + inline paint native; `insertCompositionText` application is the package's, caret placed inside the run via `dom_set_caret` | ✅ |
| `compositionend` | UI Events; not cancelable | commit or cancel | commit arrives as `insertFromComposition` (same rules as typing); an END with no text maps to `deleteCompositionText` — cancellation removes the preedit | ✅ |

### 3.7 Forms — submission, reset, validation

| Concept | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| **Form submission from user interaction** | HTML §4.10.21; `submit` cancelable | submit-button activation, or implicit submission (Enter in a text field), runs the submission algorithm | native pointer/keyboard activation now reaches `form.ls` → `submit.ls`; the package checks validity, fires cancelable `submit`, builds the native entry list (including `form="..."` associations), serializes GET/urlencoded/multipart data, and calls `request_navigation`. The browsing waist currently accepts URL/target only, so POST body delivery remains open | 🟡 (F4; ESO49) |
| `form.submit()` / `requestSubmit()` | HTML | as above | implemented, including `novalidate` / `formnovalidate`, the cancelable `submit` event with `submitter`, and the disconnected-form rule | ✅ |
| **Reset from user interaction** | HTML; `reset` cancelable | reset-button activation resets the form | native pointer/keyboard activation and script-created clicks use the same package claim protocol; `form.ls` calls the existing cancelable reset waist, including associated controls | ✅ |
| `form.reset()` | HTML | as above | implemented incl. `form=`-associated controls outside the subtree (`js_dom.cpp:8088`) | ✅ |
| **Interactive constraint validation** | HTML | block submission, fire `invalid`, show the validation bubble | `validate.ls` owns `:valid`/`:invalid` on `init`/`input`/`blur`; F4 calls the existing validity bridge before `submit`, fires `invalid`, and focuses the first invalid control. There is still no validation bubble | 🟡 |
| `invalid` | HTML; cancelable | suppress the UA validation message | dispatched from the JS validity bridges only | 🟡 |

### 3.8 Navigation & document

| Concept | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| **Link activation** | HTML; `click` activation behavior | follow the hyperlink | implemented, but on **`mousedown`** and gated on the *mousedown*'s `preventDefault` (`event.cpp:1409`, the only writer of `evcon->new_url`) | ⚠️ (§5.1, ESO51) |
| Fragment navigation | HTML | scroll to the fragment **and set `:target`** | the scroll half works (`event.cpp:9329`). The `:target` half does not: `PSEUDO_STATE_TARGET` is matched by the selector engine and readable from the store, but **nothing ever writes `STATE_TARGET`** | 🟡 (ESO53) |
| `:visited` | Selectors; privacy-restricted | style visited links | same shape — readable, never written. Needs a history source and a privacy stance first (DOM_State §6.1, ESO12) | ❌ |
| `target=` / iframe navigation | HTML | navigate the named context | implemented (`event.cpp:9367`) | ✅ |
| `accesskey` | HTML | activate the element | absent — zero occurrences repo-wide | ❌ |
| `beforeunload` | HTML; cancelable | prompt before leaving | absent | ❌ |
| `load` / `DOMContentLoaded` | HTML | none | `DOMContentLoaded` and window `load` dispatched (`script_runner.cpp:1751`); `<iframe>` `load` dispatched (`js_dom.cpp:3370`). **`<img>` `load`/`error` are not** | 🟡 |
| window `resize` / `scroll` | — | none | dispatched via `radiant_dispatch_window_event` (`event.cpp:5761`), which also drives `matchMedia` notification | ✅ |
| `animationstart` / `animationend` / `transitionend` | CSS Animations / Transitions | none | dispatched from `css_animation.cpp` | ✅ |

### 3.9 Per-element activation behavior (HTML)

The `click` row of §3.3, expanded. This is the table to check before claiming "clicking X works".

| Element | Activation behavior per HTML | Radiant | Status |
| --- | --- | --- | --- |
| `input[type=checkbox]` | toggle checkedness, clear indeterminate, fire `input` + `change` | `form.ls` (F1) | ✅ |
| `input[type=radio]` | select, clear the group, fire `input` + `change` | `form.ls` incl. the group walk (F1) | ✅ |
| `label` | retarget activation to the labeled control | native association lookup + retargeted dispatch (F1b) | ✅ |
| `select` | open/close the picker | `form.ls` open/close (F2) + native overlay + `optioncommit` (F2c) | ✅ |
| `option` in a listbox (`size>1` or `multiple`) | select / extend / toggle selection | laid out and painted (`layout_form.cpp:609`) but **no event code** — `event.cpp` never reads `select_size` or `multiple` | ❌ |
| `input[type=submit]` / `input[type=image]`, `button[type=submit]` (and bare `<button>`) | submit the form owner | `form.ls` activation handlers → `submit.ls`; native click/keyboard and JS click share the claim protocol | 🟡 (POST transport open) |
| `input[type=reset]`, `button[type=reset]` | reset the form owner | `form.ls` activation handlers → cancelable reset waist | ✅ |
| `input[type=range]` | thumb drag; arrow / Home / End / Page keys | laid out and painted; **zero interaction**. `form.ls:130` states this explicitly — the template exists only for the ARIA value mirrors | ❌ |
| `input[type=file]` | open the file picker | `input_type_to_control()` falls file/color/date through to `FORM_CONTROL_TEXT` (`view.hpp:2727`) | ❌ |
| `input[type=color]`, `date`/`time`/`datetime-local`/`month`/`week` | open the respective picker | as above | ❌ |
| `input[type=number]` | spinner buttons; arrow-key increment by `step` | text control only | ❌ |
| `a[href]` | follow the hyperlink | on `mousedown`, not `click`; no keyboard activation | ⚠️ (§5.1) |
| `summary` | toggle the parent `<details>` `open` attribute | `details.ls` (F15), including the `name=` exclusive accordion via the `details_group` waist. Layout already honoured `open`; the disclosure marker was a constant and now follows it | 🟡 — activation ✅, script-write and load-time exclusivity open (ESO62) |
| `dialog` (+ `showModal`, Esc-to-cancel, focus trap, top layer) | HTML dialog behavior | absent entirely | ❌ |
| `[popovertarget]` button | toggle the popover, fire `beforetoggle`/`toggle`, light-dismiss on Esc / outside click | toggling implemented in the JS layer (`js_dom.cpp:4483`); **no `toggle`/`beforetoggle` events and no light dismiss** | 🟡 |
| `video` / `audio` controls | play/pause, seek, volume, mute; media events | play/pause + volume by **hard-coded pixel geometry** (`event.cpp:9193`); no seek drag, no keyboard, no mute state ("TODO: track muted state properly"), no media events | 🟡 |
| `area` (image map) | follow the hyperlink | `<area>` is recognized as a void/metadata element only | ❌ |

### 3.10 Behavior-only hooks

Radiant-internal seams. No JS listener can observe them; each exists because the spec concept it implements has no event of its own, or must run at a moment the event pipeline does not expose.

| Hook | Spec concept implemented | Suppressed by `preventDefault`? |
| --- | --- | --- |
| `init` | steady-state constraint validation + ARIA reflection at control creation (ESO31) | n/a — runs as a pipeline phase between layout and render (ES19), not in event dispatch |
| `commit` | HTML's "when the value is committed" decision behind `change` | n/a — pre-event, no JS has run yet (ESO42) |
| `optioncommit` | activation of a `<select>` option — the popup overlay is not DOM, so no event exists | follows the click that carried it |
| `dropdownkey` | UA keyboard handling of an open popup | follows its keydown |
| `caretkey` | keydown's caret-movement **default action** | **yes** — dispatched with context |
| `keyintent` | the key→`inputType` **translation** inside UI Events' key processing model | **no** — deliberately context-free (F11: a JS editor that prevents the keydown still relies on the intent) |
| `domedit` | `beforeinput`'s **default action** on contenteditable (Input Events: "update the DOM as described by the inputType") | **yes** — ordinary dispatch offers it only after an uncanceled `beforeinput` |
| `execcommand` | the deprecated command surface, one rule set with the keyboard path (F14.1) | per command |
| `contextmenu` (hook) | showing the UA context menu (F10) | n/a — no spec event is dispatched today, so nothing can cancel it (§3.3) |

The suppression column is the §1.2 distinction made operational: a **default action** must die with `preventDefault` (or edits double-apply), a **translation** must survive it (or JS editors go deaf). Each direction of that mistake has been made once and is recorded in DOM_State §3.15/§3.16.

---

## 4. contenteditable coverage — the package applier

F13 gave contenteditable its own applier, `dom_edit.ls`, as "the DOM twin of `editing.ls`". `keymap.ls` produces the full intent vocabulary for both surfaces, so every unimplemented row below reaches `domedit`, returns `'pass'`, and — because the native rich applier is retired (ES20) — **the key does nothing at all**.

| `inputType` | text control (`editing.ls`) | contenteditable (`dom_edit.ls`) | Note |
| --- | --- | --- | --- |
| `insertText`, `insertReplacementText` | ✅ | ✅ | single-node fast path plus structural replacement |
| `insertCompositionText`, `insertFromComposition`, `deleteCompositionText` | ✅ | ✅ | caret placed inside the run (F13.3) |
| `deleteContentBackward` / `Forward` | ✅ | ✅ | codepoint-correct on both sides since F13.2 |
| `formatBold` / `Italic` / `Underline` / `StrikeThrough` | n/a | ✅ | via `commands.ls` (F14.1); bounded unwrap (F14.2 partial) |
| `insertHTML`, `insertText` (command form) | n/a | ✅ | `commands.ls` |
| **`insertParagraph`** (Enter) | ✅ | ✅ | package block-split primitive (F14.2) |
| **`insertLineBreak`** (Shift+Enter) | ✅ | ✅ | package line-break primitive (F14.2) |
| **`insertFromPaste`**, **`deleteByCut`**, **`insertFromDrop`**, **`deleteByDrag`** | ✅ | ❌ | contradicts the pre-absorption Appendix B, which claimed both appliers |
| **`deleteWordBackward` / `Forward`** | ✅ | ❌ | word scanners exist natively for the value buffer, not for the tree |
| **`deleteSoftLine*` / `deleteHardLine*`** | ✅ | ❌ | — |
| **`historyUndo` / `historyRedo`** | ✅ (ES17) | ❌ | the ring is text-control-scoped (ESO43) |
| **`formatIndent` / `formatOutdent`** (Tab) | n/a | ❌ | `keymap.ls` names them; nothing applies them |
| cross-text-node ranges | n/a | ✅ for delete/replacement | structural range waist (F14.2); formatting remains bounded |

The remaining gap is the clipboard/drop and history family, plus word/line deletes,
indent/outdent, and general nested formatting. The structural waist primitives
are implemented; the remaining rows are tracked as **ESO54** and **ESO43**.

---

## 5. Divergences — implemented, but not to spec

These matter more than the absences in §3, because an absence fails loudly and a divergence fails on ordinary pages while looking correct in the code.

### 5.1 Link activation runs on `mousedown`

`fire_inline_event` (`event.cpp:1409`) writes `evcon->new_url` when `MARKUP_NAME_A` sees a `RDT_EVENT_MOUSE_DOWN`, gated on the *mousedown*'s `default_prevented`. It is the only writer of `new_url` in the tree.

Per HTML, following a hyperlink is `click` activation behavior. The consequence is that the single most common idiom on the web —

```js
a.addEventListener('click', e => e.preventDefault())   // every SPA router
```

— **does not suppress navigation** under Radiant. Cancellation only works if the page happens to cancel `mousedown`. Moving the trigger to the `click` path also puts it where §3.9's other activation behaviors already live, which is what would let it become a `form.ls`-style template (DOM_State §6.1) rather than a special case in the inline-event walk. **ESO51.**

### 5.2 Activation behavior has two implementations

`lambda/js/js_dom_events.cpp:2376–2650` implements a complete activation-behavior pass inside `dispatchEvent`: checkbox/radio pre-activation with canceled-activation restore, submit-button activation, reset-button activation, popover activation, and the post-activation `input`/`change` pair. `form.ls` implements the first of those for the native path. They are reconciled at `event.cpp:9271` by observing whether the JS realm actually changed checkedness:

```cpp
bool js_did_activation = js_click_dispatched && click_check_radio && click_check_radio_changed;
```

Two consequences follow, and both are already visible:

1. **They have diverged.** The JS copy sets a clicked radio checked but its own comment concedes *"group exclusion not implemented headlessly"* — the exclusivity walk `form.ls` performs is absent there. This is exactly the class of defect that made `radiant_uncheck_radio_group` a shadow implementation of radio policy until it was retired in F1b.
2. **Submit and reset used to exist only in the JS copy.** F4 moved their policy into `form.ls`/`submit.ls`; native and JS click paths now consult the same behavior claim protocol, while the native browsing waist still owns only navigation execution. Popover activation remains JS-only.

ES10's rule ("only one of them may perform the default action for a given event") is now structural for submit/reset: both paths consult one claim protocol, and the package owns the local policy. Popover remains the un-migrated half of **ESO49**.

### 5.3 Sequential focus ignores `tabindex` order and never scrolls

`focus_move` (`state_store.cpp:7881`) collects focusables and steps through the list in **plain DOM order**. HTML's sequential focus navigation order places elements with positive `tabindex` first, in ascending value, before the `tabindex=0` set in tree order. Radiant's focusability *query* reads `tabindex` correctly (`event.cpp:7077`) — only the ordering ignores it.

Separately, focusing an element never scrolls it into view; `scroll_into_view` exists (`layout.cpp:338`) but is reached only from the JS API and fragment navigation. Tabbing through a long form walks focus off-screen. **ESO52.**

### 5.4 Space activation fires on keydown

`event.cpp:9746` documents the choice: *"browsers fire click on key-up for Space and key-down for Enter; we fire on key-down for both for simplicity so HTML form submission works without a mouse."* The stated motivation no longer holds (submission does not work without a mouse either, §3.7), and the shortcut forecloses Space-to-scroll (§3.2), which needs Space to reach the document when no activatable element is focused. Worth revisiting together with ESO48.

### 5.5 `autofocus` looks at one element

`cmd_layout.cpp:4116` calls `find_matching_input(root, "input", nullptr)` and then tests *that* element for the `autofocus` attribute, rather than searching the document for the attribute. `autofocus` on a `<textarea>`, `<select>`, `<button>`, contenteditable host, or any non-first `<input>` is silently ignored. The fix is a tree-order search for the attribute over the focusable set — it belongs with the `focus.ls` work in DOM_State §6.2.

### 5.6 After a native applier is deleted, `'pass'` means "nothing happens"

Not a bug — a contract consequence worth stating once, because it changes how §3 should be read. ES5's fallback-until-claimed made `'pass'` safe: the native default action stayed in charge. F1b/F2b/F3/ES17's amendment deleted those native halves. On a migrated class, a handler that declines now produces **no default action at all**, and the same is true for a document where the package failed to load. This is why §4's `❌` rows are total absences rather than degraded fallbacks, and why "the package declines" and "the feature is missing" are the same observation on a migrated class.

---

## 6. Open issues (ESO ledger, extending `Lambda_Design_DOM_State.md` §7)

New rows start at ESO48; ESO1–ESO47 remain in DOM_State §7. Rows here are default-action gaps specifically.

| # | Issue | Direction |
| --- | --- | --- |
| ESO48 | **No keyboard scrolling for HTML documents.** Space / PageUp / PageDown / Home / End / arrows do not scroll the nearest scrollport; scroll arrives only from wheel, scrollbar drag, and drag-autoscroll. `key_code_to_name` also lacks `PageUp`/`PageDown`, so JS sees `event.key === ""` for them | fix the key naming first (one-line, unblocks JS pages immediately); then a scroll default action on the keydown path, ordered after `caretkey` declines. Interacts with §5.4 |
| ESO49 | **Activation behavior has two implementations; popover activation still lives only in the JS one.** §5.2 | F4 moved submit/reset into `form.ls`/`submit.ls` and gave native/JS click paths the same claim protocol. Remaining work: migrate popover activation, and give the browsing layer a real POST body/method transport |
| ESO50 | **Pointer Events are partial** — `pointerdown`/`up`/`move` dispatch, but no `pointerover`/`out`/`enter`/`leave`/`cancel` and no `setPointerCapture` | boundary events follow the existing mouse boundary logic; capture needs a target-override in the dispatch path |
| ESO51 | **Link activation is on `mousedown`, and has no keyboard path.** §5.1; and Enter on a focused `<a href>` does nothing (`event.cpp:9751` covers only `input`/`button`/`select`) | move to the `click` path, then to a template per DOM_State §6.1; add `<a>` to the Space/Enter activation set at the same time |
| ESO52 | **Sequential focus ignores `tabindex` ordering and never scrolls the target into view.** §5.3 | both belong to the `focus.ls` work (DOM_State §6.2), together with the ESO60 autofocus fix |
| ESO53 | **`:target` is never set.** Fragment navigation scrolls but writes no `STATE_TARGET`; the selector engine and store already support reading it. `:visited` has the same shape but is blocked on a history source and the privacy stance (ESO12) | write the state at fragment navigation and clear the previous target; document-scoped, one element at a time |
| ESO54 | **contenteditable implements a strict subset of the text-control intent set.** §4 — paste, cut, drop, word/line deletes, indent/outdent, and undo/redo still reach `domedit` and decline | clipboard/drop policy and tree-aware word/line operations remain; `historyUndo`/`historyRedo` additionally need the ring hoisted out of `FormControlProp` (ESO43) |
| ESO55 | **Three content-attribute handlers are registered but can never fire.** `ondblclick`, `onkeypress`, `onselect` are installed by `script_runner.cpp:2582`, `:2590`, `:2597`; `dblclick`, `keypress` and `select` are dispatched nowhere. `contextmenu` is the inverse — the default action runs but no JS event is dispatched, so a page cannot cancel it or build its own menu | dispatch `dblclick` (the click-count machinery already exists), `select`, and a cancelable `contextmenu` ahead of the `menu.ls` hook; leave `keypress` undispatched per the deliberate legacy decision but stop registering the attribute |
| ESO56 | ~~**`<details>` / `<summary>` has no toggle behavior**~~ **landed 2026-08-31 (F15)** — `lambda/package/dom/details.ls` claims `click` on `view <summary>`, writes the parent's `open` through `set_attr`, and dispatches `toggle` on the details. ESO3 was not in fact a blocker: `set_attr` has gone through the DOM operation path with mutation notices since F7. Two prerequisites were wrong in the tree rather than absent — the disclosure marker was pinned to `disclosure-closed` so the triangle never turned, and `set_attr`'s null-clear was dead code (see the ESO62 row). Loading the package needed one more widening of the EO4/F9 `package_governs` gate: a `<details>` in a static document has no form control and no script, so the document owned no evaluator and the toggle silently did nothing — the same shape F9 fixed for rich editing. Residues split out as **ESO62** | proved by `test/ui/dom_pkg_details.json` (12/12 with the package, 9/12 under `RADIANT_DOM_PKG=0`) `dom_pkg_details_accordion.json` (20/20), and `dom_pkg_details_noscript.json` (6/6, the static-document path) |
| ESO62 | **`<details>` openness is claimed only where the package decides it — the click.** Three gaps follow. (a) A script write (`d.open = true`, `setAttribute('open','')`) does not close the `name=` group; (b) a document that *loads* with two open members of one group keeps both; (c) activation accepts any direct summary child, not strictly the first | (a) has no cheap seam: of the ~20 `DOM_JS_MUTATION_ATTRIBUTE` notify sites most carry no attribute name, so an `openchange` hook there would hand Lambda every attribute write to filter — the same "no chokepoint" finding F7 made for `state_change`, and the identical residue radio exclusivity carries (§5.2). (b) wants the `init` hook, but that phase visits only `elem->form_control()` elements (EO4) and `<details>` is not one. (c) waits on `:first-of-type` in the selector engine, which would tighten `resolve_css_style.cpp`'s marker rule in the same change |
| ESO57 | **`<dialog>` is entirely absent**, and the popover implementation fires no `beforetoggle`/`toggle` and has no light dismiss | both need a top-layer concept in the view tree and an Esc/outside-click policy; the Esc path can follow the context-menu and dropdown precedent (`event.cpp:9561`) |
| ESO58 | **Non-text `<input>` types have no interaction**: `range` (no thumb drag, no keys — `form.ls:130` says so), `number` (no spinner, no arrow-key step), and `file`/`color`/`date`-family, which `input_type_to_control()` degrades to `FORM_CONTROL_TEXT` (`view.hpp:2727`) | `range` and `number` are template-shaped and cheap; the picker types need host UI and are a separate decision |
| ESO59 | **Composite-widget keyboard policy is missing**: `<select multiple>` / listbox has no click or key handling at all, `<select>` has no typeahead, and radio groups have no arrow-key navigation | all three belong in `form.ls` next to the existing `dropdownkey` handler; the listbox additionally needs its option rows to be hit-testable |
| ESO60 | **`autofocus` only inspects the first `<input>`.** §5.5 | tree-order search over the focusable set; land with ESO52 |
| ESO61 | **Small residue**: `accesskey` unimplemented; `beforeunload` unimplemented; `<img>` `load`/`error` not dispatched; `<area>` image-map activation absent; Ctrl+wheel zoom absent | individually cheap, none load-bearing; listed so they stop being rediscovered |

---

## 7. Priority

Ordered by how often the gap is hit by an ordinary page, not by implementation cost.

**Tier 1 — breaks ordinary pages today**

1. **contenteditable clipboard and history** (ESO54/ESO43). Enter and line-break
   commands now use the F14.2 structural primitives; paste, cut, drop, and
   history remain the visible contenteditable gaps.
2. **Link activation on `click`** (ESO51). One-line-shaped change in trigger point, and it makes `preventDefault` on `click` work — the assumption behind essentially every JS router.
3. **Complete form submission transport and popover activation** (ESO49 / F4). Local submit/reset activation is landed; POST body delivery and popover remain.

**Tier 2 — cheap, high visibility**

4. `PageUp`/`PageDown` key naming, then keyboard scrolling (ESO48).
5. ~~`<details>` toggle (ESO56)~~ **landed**; `:target` (ESO53) remains — small, and frequently exercised by static documents.
6. `dblclick` / `select` / cancelable `contextmenu` dispatch (ESO55).
7. `tabindex` ordering, focus scroll-into-view, `autofocus` scope (ESO52 + ESO60) — one `focus.ls` pass.

**Tier 3 — bounded features**

8. `<input type=range>` and `number` interaction (ESO58).
9. Listbox, `<select>` typeahead, radio arrow keys (ESO59).
10. `<dialog>` and popover toggle events / light dismiss (ESO57).
11. Pointer boundary events and capture (ESO50).
12. The ESO61 residue.

Of these, only ESO49's submit half (F4), link activation (DOM_State §6.1), and focus policy (DOM_State §6.2) were tracked anywhere before this document.

---

## Appendix A — how this ledger is verified

The status column is not maintained by inspection of the design docs, which is how the pre-absorption Appendix B acquired two stale rows (§4's clipboard claim; Pointer Events marked ❌ while three pointer types were being dispatched). It is maintained against the tree:

- **"Dispatched"** means a call site constructs the event and reaches `js_dom_dispatch_event` — in practice `radiant_dispatch_built_event` (`event.cpp:5806`) or `radiant_dispatch_window_event`. Registering an `on<type>` content attribute in `script_runner.cpp` is *not* evidence of dispatch; three attributes are registered for types nothing dispatches (ESO55).
- **"Default action implemented"** means a package handler claims it (`lambda/package/dom/*.ls`), or a native block performs it and is reachable. A native block guarded by `radiant_behavior_claims_event` whose package counterpart declines is *not* implemented (§5.6).
- **A JS-layer implementation is recorded as such**, not as "implemented", because it is unreachable without a live JS dispatch scope and lives in the wrong home (§5.2).
- **A state written by nobody is not implemented** even when the selector engine matches it — the `:target` / `:visited` shape. Grep for writers of the `STATE_*` name, not for the pseudo-class.

When a row changes, change it here first. Docs that reference a default action link to this ledger rather than restating its status.

## Appendix B — provenance

This document absorbs, in full, Appendix B of `vibe/Lambda_Design_DOM_State.md` ("W3C/WHATWG ↔ Radiant: DOM events and default actions", added during F13/F14), which is deleted there as of 2026-08-28 and replaced by a pointer. The absorbed content is §1.2 (the three spec concepts), §3.1–§3.6 (the per-family tables), and §3.10 (the behavior-only hooks table). Two of its rows were corrected in the process, both in the direction of the tree rather than the doc:

- its clipboard/drag row claimed `deleteByCut` / `insertFromPaste` / `insertFromDrop` / `deleteByDrag` were handled by "`editing.ls` / `dom_edit.ls`"; `dom_edit.ls` has no branch for any of them (§4);
- its "not yet implemented" row marked Pointer Events ❌ with the note that `pointermove` existed *"solely as a hot-path guard name"*; `pointerdown`, `pointerup` and `pointermove` are all really dispatched with `preventDefault` honored (§3.3).

Everything else in this document is new: §3.7–§3.9 (forms, navigation, per-element activation), §4, §5, §6 (ESO48–ESO61) and §7.
