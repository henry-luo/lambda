# Lambda DOM Default Actions — the UA behavior ledger

> **Status**: **normative for default-action placement and status** (2026-09-01). The architecture is decided (ES5, ES10, ES15, ES20, ES30, ES31); what this document adds is the complete per-event ledger and the gap inventory that drives it.
> **Role**: this is the **single source of truth for what Radiant does after an event is dispatched** — which UA default actions exist, where each half lives, and which are still missing. Every other doc that names a default action points here rather than restating it. Absorbs and replaces Appendix B of `vibe/Lambda_Design_DOM_State.md` (deleted there 2026-08-28).
> **Scope**: default actions and activation behavior for HTML documents under Radiant — the pointer, keyboard, editing, focus, clipboard, drag, composition, form, and navigation families. Event *dispatch* mechanism is in scope only where a missing dispatch is what makes a default action unreachable.
> **Companion docs**: `vibe/Lambda_Design_DOM_State.md` (the behavior-template architecture and the ES/ESO ledgers this doc extends), `vibe/Lambda_Design_DOM_Pkg.md` (layering and placement policy), `doc/dev/radiant/RAD_15_Events_Input.md`, `RAD_17_Interaction_State.md`, `RAD_19_Form_Controls.md`.
> **Formal anchors**: S12.1.3 (reactive templates: body = pure `fn`, mutation only in `on` handlers), S12.2.2 (element mutation), S9.1.4 (state lives in view state), S7.6/S7.10 (error discharge and the sys-func contract), D4.5.1v3 (the Radiant memory seam).
> **Ledger series**: this doc extends the DOM-State area's existing `ES#` (decisions) and `ESO#` (open issues) series per `doc/Doc_Convention.md` §4 — it mints no new series. **ES30** is minted in §2.4 and **ES31** in §2.5; ESO48–ESO62 are minted below; ESO63–ESO69 (and ES22–ES29, F17–F21) are minted in `vibe/Lambda_Design_DOM_Dispatch.md`. New open issues start at **ESO70**.

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
| **Policy** — which key means what, what a click does to a control, what an `inputType` does to the document | **dom package (L)** | `form.ls` activation, `caret.ls` key→operation, `keymap.ls` key→intent, `scroll.ls` key→scroll operation, `editing.ls` / `dom_edit.ls` appliers, `commands.ls` command set, `menu.ls`, `ime.ls`, `validate.ls`, `aria.ls` |
| **Waist primitives** — the named, ≤4-argument operations policy drives mechanism through | **`radiant` module (M)** | `set_state`, `dispatch`, `radio_group`, `replace_range`, `dom_replace_range`, `dom_wrap_range`, `caret_operation`, `scroll_operation` |

The recurring failure mode was a default action in a **fourth** home — the JS
dispatch layer (`lambda/js/js_dom_events.cpp`) implementing activation only for
`dispatchEvent`. F19 retires that copy: direct DOM dispatch now enters the
native synthetic bridge and the package remains the single policy owner (§5.2,
ES25/ES26).

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

Per ES10, JS and Lambda are parallel peers over one canonical state store; neither gatekeeps the other, and **only one of them may perform the default action for a given event**. F19 makes that ownership structural for every current click activation class: trusted and synthetic entries share one package claim protocol (ES25/ES26).

### 2.3 Behavior-only hooks

Radiant-internal seams that no JS listener can observe. Each exists because the spec concept it implements has no event of its own, or must run at a moment the event pipeline does not expose. Full table with suppression semantics: §3.11.

### 2.4 Key policy and snapshot DOM navigation (ES30)

**Ruling (user direction, 2026-09-01).** The dom package owns all
**keyboard default-action policy**: sequential focus, selection/caret command
choice, keyboard scrolling, keyboard activation, and clipboard-command
choice. Native continues to receive platform input, construct and dispatch the
event, retain the canonical focus and selection state, resolve range/caret
geometry, emit DOM events, and paint. This is the same policy/mechanism seam
as the existing editing package: S12.1.3 puts the decision in an `on` handler;
S9.1.4 keeps the mutable state in the view state; native owns the coupled
geometry and paint substrate.

The default-action half is one `keydefault` behavior hook after an uncanceled
`keydown`, not a collection of native per-key branches. It has the existing UA
tier one-call ceiling and may return `'pass'` or `'prevent-default'` under
ES15. `keyintent` remains a separate context-free translation: it has no
side effect and must still reach the package when author code prevents the
`keydown`. Thus policy can choose a named action without reclassifying a
translation as a default action.

The package needs a bounded, element-only snapshot navigation surface. It
builds a Lambda array while it traverses, before it mutates the tree; no live
`children`/`childNodes` collection is exposed. That makes the package's
iteration a snapshot as required by S9.2.2, while native retains node lifetime
and generation validation.

| API | Contract |
| --- | --- |
| `radiant.document_root(node)` | Return the document element owning `node`. |
| `radiant.embedding_element(node)` | Return the owning `<iframe>` element when `node` belongs to an embedded document, otherwise `null`. This is the bounded upward edge of the browsing-context tree, not a live child-document collection. **ES31 extension.** |
| `radiant.embedded_document_root(iframe)` | Return the active nested document's root for an `<iframe>`, otherwise `null`. This is the paired downward edge for navigation resolution; it validates the native document/node generation before returning the wrapper. **ES31 extension.** |
| `radiant.first_element_child(node)` | Return the first element child, or `null`. |
| `radiant.next_element_sibling(node)` | Return the next element sibling, or `null`. |
| `radiant.parent(node)` / `radiant.closest(node, selector)` | Retain the existing upward navigation operations. |
| `radiant.focus_candidates(root)` | Return a DOM-order **snapshot** of programmatically focusable elements plus each candidate's native sequential-focus eligibility; it does not apply `tabindex` ordering. |
| `radiant.focused(node)` | Return whether `node` is the document's current focus target. |
| `radiant.focus_set(node, from_keyboard)` | Commit the selected focus target atomically: canonical state, pseudo-state, iframe ownership, and text-control focus capture remain native mechanism; native emits the public focus transition at the originating event boundary. |
| `radiant.scroll_into_view(node)` | Perform the geometry-dependent scroll after package policy chooses that it is required. |
| `radiant.selection_operation(node, operation, extend)` | Generalize `caret_operation` to apply a named selection operation — character, word, line, document, page, or select-all — while native resolves the live range, updates canonical selection, emits `selectionchange`, and repaints. |

`focus.ls` uses `focus_candidates` plus `attr`/`has_attr` to order positive
`tabindex` candidates ascending before the tree-order `tabindex=0` set, then
calls `focus_set` and `scroll_into_view`. The same candidate snapshot makes
`autofocus` a tree-order package decision. `caret.ls` evolves into the
selection/key-command policy over `selection_operation`; it does not receive
raw DOM range endpoints or reproduce layout geometry.

**F11 consolidation (2026-09-01).** `keymap.ls` is the one key-to-edit-intent
table for text controls and rich editing. `caret.ls` is the one key-to-named
caret-operation table. The native key path consumes only those named results:
it applies the live text-control range/buffer edit, canonical selection,
history, clipboard, `beforeinput`/`input`, and repaint. The text-control
native path contains no second accelerator or delete-policy table.
`PageUp`/`PageDown` are named by native,
selected by `caret.ls` only for a `<textarea>`, and resolved there as native
live-buffer line geometry; `scroll.ls` receives them for document movement
only after caret policy has declined.

**ESO48 closure (2026-09-01).** `scroll.ls` now names document scroll
operations only after author `keydown`, control activation, and `caretkey`
have all declined. Plain arrows select line movement; PageUp/PageDown and
Space select page movement; Shift+Space reverses it; Home/End select the
range boundary. Native finds the focused element's nearest live scrollport
(or the viewport when there is no focused element), derives the line/page
distance from its rendered dimensions, clamps canonical scroll state, emits
the element/window `scroll` notifications, updates geometry observers, and
repaints. Thus ES30 retains the policy/mechanism seam without handing layout
measurements or scroll state to Lambda.

The generic traversal replaces the current policy-specific tree-query waists:

| Retired waist | Package replacement |
| --- | --- |
| `radiant.form_of(node)` | `tree.ls` walks ancestors and, for an explicit `form=` owner, performs a root snapshot search by form `id`. |
| `radiant.radio_group(node)` | `tree.ls` walks the relevant form or document snapshot and filters the HTML radio-group predicate. |
| `radiant.details_group(node)` | `tree.ls` walks the document snapshot and filters same-name `<details>` peers. |
| `radiant.context_menu_target(body)` | The context-menu hook reads `evt.target`; this is event addressing, not tree navigation. ES24's shared event record carries that target. |

The four waists are removed only after their package callers use the shared
`tree.ls` helpers and the behavior event map exposes `evt.target`. This keeps
one traversal implementation in Lambda rather than retaining four native
policy walkers. It does **not** move DOM node storage, focus/selection state,
range geometry, event dispatch, or painting out of Radiant.

### 2.5 Navigation policy and package target resolution (ES31)

**Ruling (user direction, 2026-09-01).** The dom package owns navigation
**policy**, including link activation, fragment-target lookup, and resolution
of `target` to a browsing context. Native owns the navigation **execution**:
URL/history commit, network or document loading, iframe replacement and
layout, lifecycle event emission, canonical `:target` state, scroll geometry,
and paint. This preserves S12.1.3's handler-owned decision while retaining
native ownership of canonical state and native node validity at the D4.5.1v3
memory seam.

`navigation.ls` is reached by a `linkactivation` behavior-only hook only
after an uncancelled `click`; it replaces the `mousedown` `new_url` shortcut.
Keyboard policy activates a focused link by dispatching that same `click`, so
pointer and keyboard follow one cancelable path. The handler reads `href` and
`target` from the matched `<a href>` and returns its ES15 verdict after it has
submitted the navigation request.

#### 2.5.1 Package target resolution

`navigation.ls` uses the ES30 element snapshots plus the two ES31 browsing-context
edges to produce this immutable resolution record:

| `target` spelling | Package resolution | Element supplied to native execution |
| --- | --- | --- |
| missing, empty, or `_self` | the source document root | that root element |
| `_parent` | `embedding_element(source_root)` followed by its `document_root`; at top level, the source root | the resolved parent root element |
| `_top` | repeatedly follow `embedding_element` then `document_root` until the top root | the resolved top root element |
| `_blank` | a **new-context** resolution | `null`; no element exists before native host execution creates the context (ESO70) |
| a non-reserved name | snapshot-walk the top browsing-context subtree's `<iframe name>` elements, crossing into active iframe documents only through `embedded_document_root`; a match resolves to that iframe and its nested root | the matched `<iframe>` element |
| unmatched non-reserved name | a **new named-context** resolution carrying that name | `null`; native host execution creates the named context (ESO70) |

`_blank` and an unmatched name are deliberately explicit `new` resolutions,
not a forged DOM node and not an accidental fallback to `_self`. Thus every
**existing** target is resolved by Lambda and passed as the exact root or
iframe element; the two cases without an existing target carry `target: null`,
`target_kind: "new"`, and (for a named target) `target_name`. This is the
minimum honest representation of a browsing context that does not yet have a
DOM owner.

The snapshot walk is bounded: it never exposes an iframe's children as a live
collection and it reaches a nested document only through
`embedded_document_root`. Native validates every returned node belongs to the
resolved document/context before executing, so a detached or stale wrapper
cannot redirect a navigation.

#### 2.5.2 Fragment policy and the native execution waist

URL parsing and same-document comparison are native mechanism; the package
must not reproduce base-URL resolution or percent decoding. It receives a
small immutable result from:

```text
radiant.navigation_destination(source, raw_url, resolved_target_root)
  -> { kind: "fragment" | "document", fragment: string|null }
```

`target_kind: "new"` has no current target root and is therefore always a
document navigation. It skips this query until native execution has created
the destination context.

For a `fragment` result, `navigation.ls` snapshot-walks the resolved target
root and selects the fragment element: an `id` match first, then an `<a name>`
match, each in tree order. It passes that **resolved fragment element**, or
`null` when no match exists, in the one request below. Lambda therefore
decides which target and which fragment are meant; it never calculates scroll
coordinates or writes selector state itself.

```text
radiant.request_navigation({
  source, url: raw_url,
  target: resolved_target_element | null,
  target_kind: "existing" | "new",
  target_name: string | null,
  fragment_target: resolved_fragment_element | null
})
```

The native executor validates the supplied elements but **does not re-search
for a named iframe or a fragment id**. In one transaction it commits URL and
history state, loads/replaces the resolved document when needed, and, for a
same-document fragment navigation, clears the previous `STATE_TARGET`, writes
the one supplied target (if any), schedules selector invalidation, and queues
the existing geometry-aware scroll-into-view mechanism. This is the missing
writer for `:target`; `PSEUDO_STATE_TARGET` already reads that state. A missing
fragment supplies `null`, so the transaction can apply the defined clear/no-
scroll behavior without retaining an old target.

The current form-specific `request_navigation` remains available while forms
still migrate their target-resolution policy. Links use the common request
shape today: the lazy package loader now admits an `a[href]` target and the
document-owned evaluator boundary switches at the outer event scope. Existing
targets (`_self`, `_parent`, `_top`, and loaded named iframes) execute through
the new transaction. `_blank` and unmatched names are correctly represented as
`new`, but current `UiContext` has no host-owned browsing-context factory, so
native rejects rather than silently treating either as `_self` (ESO70).

### 2.6 Status vocabulary

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

Verified against the tree at 2026-09-01 (`event.cpp`, `lambda/package/dom/*.ls`, `lambda/js/js_dom_events.cpp`). Anchors are `file:line` at that revision — treat them as pointers to the right neighborhood, not as stable addresses.

### 3.1 Input & editing

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `beforeinput` | Input Events L1/L2; cancelable except `insertCompositionText` / `deleteCompositionText` | UA updates the DOM as described by `inputType` | dispatched through the ordinary JS/author path; the package supplies the default — `editing.ls` splices text controls (F5/ES9), `dom_edit.ls` splices contenteditable through the DOM-range waist (F13). Prevented ⇒ the package default is not invoked (ES20/F14.3–F14.4) | ✅ text controls · ✅ contenteditable (§4) |
| `input` | Input Events; not cancelable | none — reports a mutation that already happened | dispatched post-mutation from the one engine path that applied the edit; package `on input` re-derives `:valid`/`:invalid` and the ARIA mirrors | ✅ |
| `change` | HTML; not cancelable | none | the *decision* is the behavior-only `commit` hook before blur (ESO42); native fires the event so it precedes `blur` for JS and templates alike | ✅ |
| `select` | HTML; not cancelable | none | text-control selection writers queue one post-commit, noncancelable `select` task on the element; contenteditable selection remains `selectionchange` | ✅ text controls |

### 3.2 Keyboard

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `keydown` | UI Events; cancelable | text input, caret movement, scrolling, activation via Space/Enter — "the key processing model" | dispatched to JS first, then split by kind (below) | 🟡 — see the four rows |
| ↳ **`caretkey`** (default action) | — | caret movement | dispatched *with* context, so a prevented keydown suppresses it → `caret.ls`, both surfaces. Arrow / Home / End apply to both; `PageUp` / `PageDown` are package-selected for `<textarea>` and use native live-buffer line geometry | 🟡 |
| ↳ **`keyintent`** (translation) | — | key → `inputType` | dispatched context-free, deliberately (F11) → `keymap.ls` | ✅ |
| ↳ **`dropdownkey`** | — | UA handling of an open `<select>` popup | Up / Down / Enter / Escape → `form.ls`. **No typeahead** | 🟡 |
| ↳ **document scrolling** | — | Space / PageUp / PageDown / Home / End / arrows scroll the nearest scrollport | after an uncancelled keydown and a declined `caretkey`, `scrollkey` reaches `scroll.ls`; native resolves/clamps the nearest live scrollport, emits `scroll`, updates the viewport mirror/observers, and repaints. Textarea PageUp/PageDown remain caret movement | ✅ (ESO48 / ES30) |
| ↳ **Space/Enter activation** | — | activate the focused element | Enter follows the keydown click/activation path. Space arms an identity-pinned target only after uncancelled `keydown`, then dispatches its click on `keyup`; checkbox/radio and button policy remains in `form.ls`, while a declined Space reaches `scroll.ls`. `navigation.ls` claims an uncancelled link click and requests native execution | ✅ (ES30; §5.4) |
| `keyup` | UI Events; cancelable | none meaningful | dispatched; no package involvement | ✅ |
| `keypress` | legacy, deprecated | — | not dispatched, deliberately; `onkeypress` is not registered | — |

### 3.3 Pointer & activation

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `mousedown` | UI Events; cancelable | begin selection, focus change, drag preparation | transform-aware hit-testing native (ESO47); `selectstart` dispatched at selection begin; focus transition via the state machine. Link navigation is the legacy **package-off** fallback only; package-enabled documents activate on uncancelled `click` | ✅ dispatch · 🟡 default (§5.1) |
| `mouseup` / `click` | UI Events; cancelable; canceling `click` cancels **activation behavior** | element-specific activation (HTML) | trusted and synthetic entries reach one package activation stage for checkbox / radio / `<select>` open-close and popover. Cancellation settles before the package write, so no cross-realm checkedness restore is needed. Label association lookup stays native (`for=` is not an ancestor walk); the dispatch is retargeted | 🟡 — per element, see §3.9 |
| dropdown option click | no spec event — the popup overlay is not DOM | — | native geometry resolves the row; behavior-only **`optioncommit`** carries the index; `form.ls` commits and closes (F2c). One commit path shared by pointer, Enter, and the test harness | ✅ |
| `dblclick` | UI Events; cancelable | UA convention: word selection | word/line/select-all selection stays native on the click count; the final primary click now emits `dblclick` (detail 2) to `ondblclick` / EventTarget listeners without a second selection action | ✅ |
| `contextmenu` | UI Events; cancelable | show the UA context menu | right-button press emits a cancelable `contextmenu` before the F10 hook. `preventDefault()` suppresses the package popup; otherwise `menu.ls` decides target + enable mask and native paints it | ✅ |
| `mousemove` / `over` / `out` / `enter` / `leave` | UI Events | none | dispatched; hover state native; hot-path gate keeps the package out of per-frame dispatch | ✅ |
| `wheel` | UI Events; cancelable | scroll | dispatched before the native scroll (`event.cpp:9527`), hot-path gated. **Ctrl+wheel zoom not implemented** (browser-chrome level; noted, not tracked) | ✅ |
| `pointerdown` / `pointerup` / `pointermove` | Pointer Events L2; cancelable | as their mouse equivalents | **dispatched** as a compatibility stream alongside the mouse events, with `preventDefault` honored (`event.cpp:8530`, `8933`, `8025`) — added because JS drag libraries select the pointer stream when `PointerEvent` exists | ✅ |
| `pointerover` / `out` / `enter` / `leave` / `cancel`; `setPointerCapture` | Pointer Events L2 | boundary + capture semantics | absent | ❌ (ESO50) |
| touch events | Touch Events | — | absent | ❌ |

### 3.4 Focus & selection

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `focus` / `blur` / `focusin` / `focusout` | UI Events; not cancelable | none (focus already moved) | focus machinery and `:focus` / `:focus-within` / `:focus-visible` native; package `on blur` revalidates; `commit` runs before the blur decision | ✅ |
| Tab / Shift+Tab | HTML sequential focus navigation | move focus in tabindex order, scroll the new target into view | `focus.ls` orders a native candidate snapshot: positive `tabindex` ascending, then zero/default DOM order; native commits canonical focus, emits focus events, and queues geometry-aware scroll | ✅ (ES30; ESO52) |
| `autofocus` processing | HTML | focus the first `autofocus` element in tree order | the behavior-only `focusinit` hook lets `focus.ls` choose the first focusable `[autofocus]` candidate in DOM order; native commits and emits the focus transition | ✅ (ES30; ESO60) |
| `selectstart` | Selection API; cancelable | begin a selection | dispatched when a pointer selection begins; selection storage is `DocState::sel` (document-scoped) | ✅ |
| `selectionchange` | Selection API; not cancelable | none | dispatched from the selection projection | ✅ |

### 3.5 Clipboard & drag-and-drop

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `copy` | Clipboard APIs; cancelable | place selection on clipboard | native — copy has no `beforeinput` intent (nothing is input), so it never enters the package | ✅ |
| `cut` | Clipboard APIs; cancelable | copy + delete selection | copy remains native; the deletion arrives as `deleteByCut`, and both `editing.ls` and `dom_edit.ls` claim the prepared range. On non-editable text: full no-op, matching browsers | ✅ |
| `paste` | Clipboard APIs; cancelable | insert clipboard content | payload fill native (clipboard read is mechanism); `editing.ls` applies control policy and `dom_edit.ls` applies the plain-text payload through the raw DOM-range waist. HTML remains an author `DataTransfer` concern, not a second package parser | ✅ plain text |
| `dragstart` / `dragover` / `drop` / `dragend` | HTML DnD; cancelable (`dragover`'s default is *rejecting* the drop — `preventDefault` enables it) | move/copy the dragged content | drag geometry and range tracking native; `<img>` and `<a href>` are draggable by default; text edits arrive as `deleteByDrag` + `insertFromDrop`, claimed by `editing.ls` / `dom_edit.ls` through their current range. Text drag-and-drop is reachable from real mouse input since F16/ES21; element DnD still requires the author `dropzone` attribute, which is not HTML5 DnD | ✅ plain text |

### 3.6 Composition (IME)

| Event | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| `compositionstart` | UI Events; cancelable | begin the composition session | session policy in `ime.ls`, bound to document-scoped IME state (ES18); the claim-without-edit is expressed through the verdict channel | ✅ |
| `compositionupdate` | UI Events; not cancelable | update the preedit | preedit storage + inline paint native; `insertCompositionText` application is the package's, caret placed inside the run via `dom_set_caret` | ✅ |
| `compositionend` | UI Events; not cancelable | commit or cancel | commit arrives as `insertFromComposition` (same rules as typing); an END with no text maps to `deleteCompositionText` — cancellation removes the preedit | ✅ |

### 3.7 Forms — submission, reset, validation

| Concept | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| **Form submission from user interaction** | HTML §4.10.21; `submit` cancelable | submit-button activation, or implicit submission (Enter in a text field), runs the submission algorithm | native pointer/keyboard activation now reaches `form.ls` → `submit.ls`; the package checks validity, fires cancelable `submit`, builds the native entry list (including `form="..."` associations), serializes GET/urlencoded/multipart data, and calls `request_navigation`. The browsing waist currently accepts URL/target only, so POST body delivery remains open | 🟡 (F4; ESO71) |
| `form.submit()` / `requestSubmit()` | HTML | as above | implemented, including `novalidate` / `formnovalidate`, the cancelable `submit` event with `submitter`, and the disconnected-form rule | ✅ |
| **Reset from user interaction** | HTML; `reset` cancelable | reset-button activation resets the form | native pointer/keyboard activation and script-created clicks use the same package claim protocol; `form.ls` calls the existing cancelable reset waist, including associated controls | ✅ |
| `form.reset()` | HTML | as above | implemented incl. `form=`-associated controls outside the subtree (`js_dom.cpp:8088`) | ✅ |
| **Interactive constraint validation** | HTML | block submission, fire `invalid`, show the validation bubble | `validate.ls` owns `:valid`/`:invalid` on `init`/`input`/`blur`; F4 calls the existing validity bridge before `submit`, fires `invalid`, and focuses the first invalid control. There is still no validation bubble | 🟡 |
| `invalid` | HTML; cancelable | suppress the UA validation message | dispatched from the JS validity bridges only | 🟡 |

### 3.8 Navigation & document

| Concept | Spec, cancelable | Default action per spec | Radiant | Status |
| --- | --- | --- | --- | --- |
| **Link activation** | HTML; `click` activation behavior | follow the hyperlink | `navigation.ls` handles `linkactivation` only after an uncancelled click; Enter on a focused link dispatches the same click. Native executes the pinned request. The mousedown `new_url` path is now package-off fallback only | 🟡 (ESO70 for new contexts; ES31) |
| Fragment navigation | HTML | scroll to the fragment **and set `:target`** | `navigation.ls` snapshot-resolves `id`, then `<a name>`; native clears/writes `STATE_TARGET`, invalidates selector state, and queues geometry-aware scroll. Tested for existing and missing fragments | ✅ (ES31) |
| `:visited` | Selectors; privacy-restricted | style visited links | same shape — readable, never written. Needs a history source and a privacy stance first (DOM_State §6.1, ESO12) | ❌ |
| `target=` / iframe navigation | HTML | navigate the named context | `navigation.ls` resolves `_self`, `_parent`, `_top`, loaded named iframes, `_blank`, and unmatched names. Native validates and executes existing root/iframe targets; `new` targets await host context creation | 🟡 (ESO70; ES31) |
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
| `a[href]` | follow the hyperlink | `navigation.ls` owns click/Enter policy and target/fragment resolution; native executes the resolved existing context. New browsing contexts remain ESO70 | 🟡 (ES31; ESO70) |
| `summary` | toggle the parent `<details>` `open` attribute | `details.ls` (F15), including the `name=` exclusive accordion via the `details_group` waist. Layout already honoured `open`; the disclosure marker was a constant and now follows it | 🟡 — activation ✅, script-write and load-time exclusivity open (ESO62) |
| `dialog` (+ `showModal`, Esc-to-cancel, focus trap, top layer) | HTML dialog behavior | absent entirely | ❌ |
| `[popovertarget]` button | toggle the popover, fire `beforetoggle`/`toggle`, light-dismiss on Esc / outside click | `form.ls` claims click through `radiant.activate_popover`; native resolves the target and transition once. **No `toggle`/`beforetoggle` events and no light dismiss** | 🟡 |
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
| `scrollkey` | keydown's document-scroll **default action** after caret/activation decline | **yes** — dispatched with context |
| `linkactivation` | HTML hyperlink activation and package navigation policy (ES31) | **yes** — runs only after an uncancelled `click` |
| `keyintent` | the key→`inputType` **translation** inside UI Events' key processing model | **no** — deliberately context-free (F11: a JS editor that prevents the keydown still relies on the intent) |
| `domedit` | `beforeinput`'s **default action** on contenteditable (Input Events: "update the DOM as described by the inputType") | **yes** — ordinary dispatch offers it only after an uncanceled `beforeinput` |
| `execcommand` | the deprecated command surface, one rule set with the keyboard path (F14.1) | per command |
| `contextmenu` (hook) | showing the UA context menu (F10) | **yes** — runs only after an uncanceled DOM `contextmenu` (§3.3) |

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
| **`insertFromPaste`**, **`deleteByCut`**, **`insertFromDrop`**, **`deleteByDrag`** | ✅ | ✅ plain text | the raw range waist carries the native text payload; HTML remains an author `DataTransfer` concern |
| **`deleteWordBackward` / `Forward`** | ✅ | ❌ | word scanners exist natively for the value buffer, not for the tree |
| **`deleteSoftLine*` / `deleteHardLine*`** | ✅ | ❌ | — |
| **`historyUndo` / `historyRedo`** | ✅ (ES17) | ❌ | the ring is text-control-scoped (ESO43) |
| **`formatIndent` / `formatOutdent`** (Tab) | n/a | ❌ | `keymap.ls` names them; nothing applies them |
| cross-text-node ranges | n/a | ✅ for delete/replacement | structural range waist (F14.2); formatting remains bounded |

The remaining gap is the history family, plus word/line deletes, indent/outdent,
and general nested formatting. The structural waist primitives are implemented;
the remaining rows are tracked as **ESO54** and **ESO43**.

---

## 5. Divergences — implemented, but not to spec

These matter more than the absences in §3, because an absence fails loudly and a divergence fails on ordinary pages while looking correct in the code.

### 5.1 Link activation is package-owned for package-enabled documents

`navigation.ls` now receives `linkactivation` only after the cancelable
`click` has settled. Its success path queues a pinned request, and native
validates then executes it; a static-page `preventDefault()` therefore blocks
navigation, and Enter dispatches that same click path. The old
`fire_inline_event` `mousedown` write to `evcon->new_url` remains only when
the package is disabled or cannot acquire a document evaluator. It is a
compatibility fallback, not a second policy for package-enabled documents.

The remaining gap is not click cancellation: `_blank` and an unmatched named
target have no `UiContext` host factory that can create a browsing context.
They resolve to an explicit `new` request and are rejected rather than
misrouted to `_self` (**ESO70**).

### 5.2 Activation behavior is single-sourced (F19)

F19 deletes the activation pass from `lambda/js/js_dom_events.cpp` and routes a
direct DOM `dispatchEvent()` / `el.click()` through
`radiant_dispatch_synthetic_dom_event`. The native bridge lets the shared
author pass settle cancellation, then calls the same `dispatch_click_default_actions`
stage as trusted pointer input. Native retains label association, target
resolution, canonical state storage, and form/link execution mechanics;
`form.ls` owns checkbox/radio transitions, group exclusivity, `input`/`change`,
submit/reset decisions, and popover policy through waist primitives (S12.1.3);
native keeps canonical state at the S9.1.4/D4.5.1v3 seam.

There is no checkedness diff, no JS-side pre-activation, and no cancellation
restore across realms: a canceled click reaches no package write, so listeners
observe the pre-default state. The one author/UA pipeline is therefore the
ES10 owner for both trusted and synthetic click activation (ES25/ES26).
`test/ui/dom_synthetic_activation.json` and
`test/ui/js_dispatch_radio_group.json` prove load-time `click()`, radio
selection, cancellation timing, exclusivity, and popover visibility. **ESO49
is resolved.**

### 5.3 Sequential focus `tabindex` order and scroll — resolved

`focus.ls` receives the native DOM-order candidate snapshot and applies HTML's
positive-`tabindex` ordering before the zero/default tree-order set. Its
selected node returns through `radiant.focus_set` and
`radiant.scroll_into_view`; native retains S9.1.4's canonical focus state,
range geometry, public focus-event emission, and paint. Negative `tabindex`
remains programmatically focusable but is explicitly excluded from the
sequential snapshot. `test/ui/dom_pkg_focus_policy.json` covers ordering,
tie order, wraparound, negative exclusion, autofocus, and scroll. **ESO52
landed 2026-09-01 (ES30).**

### 5.4 Space activation on keyup — resolved

After author `keydown` dispatch has declined, Space on a checkbox, radio, or
button arms a `DOM_NODE_PIN_STATE` identity. Native keeps that identity valid
across a handler-triggered reconciliation, dispatches `keyup`, and only then
runs the ordinary click path; the package still owns the click policy.
Cancellation on either key event suppresses the click, while an unarmed Space
continues to `scroll.ls`. This honors S12.1.3's policy/mechanism seam and
D4.5.1v3's lifetime rule without giving Lambda a raw pointer or scroll
geometry. `test/ui/dom_pkg_space_keyup.json` proves state changes only on
keyup. **Landed 2026-09-01 (ES30).**

### 5.5 `autofocus` tree-order processing — resolved

The `focusinit` behavior-only hook invokes `focus.ls`, which filters the same
focus candidate snapshot for `[autofocus]` in DOM order. It therefore covers
`textarea`, `select`, `button`, contenteditable hosts, and later inputs rather
than only the first input. Native commits the result and emits `focus` plus
`focusin`; no package code owns mutable focus state. **ESO60 landed
2026-09-01 (ES30).**

### 5.6 After a native applier is deleted, `'pass'` means "nothing happens"

Not a bug — a contract consequence worth stating once, because it changes how §3 should be read. ES5's fallback-until-claimed made `'pass'` safe: the native default action stayed in charge. F1b/F2b/F3/ES17's amendment deleted those native halves. On a migrated class, a handler that declines now produces **no default action at all**, and the same is true for a document where the package failed to load. This is why §4's `❌` rows are total absences rather than degraded fallbacks, and why "the package declines" and "the feature is missing" are the same observation on a migrated class.

---

## 6. Open issues (ESO ledger, extending `Lambda_Design_DOM_State.md` §7)

New rows start at ESO48; ESO1–ESO47 remain in DOM_State §7. Rows here are default-action gaps specifically.

| # | Issue | Direction |
| --- | --- | --- |
| ESO48 | ~~**No keyboard scrolling for HTML documents.**~~ **landed 2026-09-01 (ES30)** — `scroll.ls` selects line/page/boundary operations after author keydown, activation, and caret policy decline; an armed Space keyup click counts as claimed, while unarmed Space scrolls. Native applies the named operation to the focused element's nearest live scrollport or the viewport, preserves canonical state/notifications, and repaints. Root and nested-scrollport event simulations cover the two target cases | — |
| ESO49 | ~~**Activation behavior has two implementations; popover activation still lives only in the JS one.**~~ **Resolved — F19 / ES25–ES26.** Direct DOM clicks and trusted clicks share one package-owned activation stage; popover policy moved to `form.ls` and the JS activation/reconciliation copy is deleted | verified by `test/ui/dom_synthetic_activation.json` (6/6), `dom_pkg_prevent_default.json` (4/4), and `dom_pkg_radio.json` (7/7) |
| ESO50 | **Pointer Events are partial** — `pointerdown`/`up`/`move` dispatch, but no `pointerover`/`out`/`enter`/`leave`/`cancel` and no `setPointerCapture` | boundary events follow the existing mouse boundary logic; capture needs a target-override in the dispatch path |
| ESO51 | ~~**Link activation is on `mousedown`, and has no keyboard path.**~~ **landed 2026-09-01 (ES31)** | `navigation.ls` claims `linkactivation` after an uncancelled click; Enter dispatches that same click. It resolves fragments and existing root/iframe targets through the ES30/ES31 snapshot surface, and native executes only the pinned request |
| ESO52 | ~~**Sequential focus ignores `tabindex` ordering and never scrolls the target into view.**~~ **landed 2026-09-01 (ES30)** — `focus.ls` orders the native snapshot and native applies focus/scroll | — |
| ESO53 | ~~**`:target` is never set.**~~ **landed 2026-09-01 (ES31)** | package snapshot resolution supplies the fragment element (or `null`) to the native transaction. It clears the prior target, writes exactly one `STATE_TARGET`, invalidates selectors, and queues native geometry-aware scrolling |
| ESO54 | **contenteditable implements a strict subset of the text-control intent set.** §4 — plain clipboard/drop now claim the raw DOM range; word/line deletes, indent/outdent, and undo/redo still reach `domedit` and decline | tree-aware word/line operations remain; `historyUndo`/`historyRedo` additionally need the ring hoisted out of `FormControlProp` (ESO43) |
| ESO55 | ~~**`ondblclick` / `onselect` were inert and `contextmenu` could not be canceled.**~~ **landed 2026-09-01** — `dblclick` is emitted after the final primary click, text-control selection writes already queue `select`, and a cancelable DOM `contextmenu` now precedes the F10 hook. `onkeypress` is removed because `keypress` remains deliberately unsupported | — |
| ESO56 | ~~**`<details>` / `<summary>` has no toggle behavior**~~ **landed 2026-08-31 (F15)** — `lambda/package/dom/details.ls` claims `click` on `view <summary>`, writes the parent's `open` through `set_attr`, and dispatches `toggle` on the details. ESO3 was not in fact a blocker: `set_attr` has gone through the DOM operation path with mutation notices since F7. Two prerequisites were wrong in the tree rather than absent — the disclosure marker was pinned to `disclosure-closed` so the triangle never turned, and `set_attr`'s null-clear was dead code (see the ESO62 row). Loading the package needed one more widening of the EO4/F9 `package_governs` gate: a `<details>` in a static document has no form control and no script, so the document owned no evaluator and the toggle silently did nothing — the same shape F9 fixed for rich editing. Residues split out as **ESO62** | proved by `test/ui/dom_pkg_details.json` (12/12 with the package, 9/12 under `RADIANT_DOM_PKG=0`) `dom_pkg_details_accordion.json` (20/20), and `dom_pkg_details_noscript.json` (6/6, the static-document path) |
| ESO62 | **`<details>` openness is claimed only where the package decides it — the click.** Three gaps follow. (a) A script write (`d.open = true`, `setAttribute('open','')`) does not close the `name=` group; (b) a document that *loads* with two open members of one group keeps both; (c) activation accepts any direct summary child, not strictly the first | (a) has no cheap seam: of the ~20 `DOM_JS_MUTATION_ATTRIBUTE` notify sites most carry no attribute name, so an `openchange` hook there would hand Lambda every attribute write to filter — the same "no chokepoint" finding F7 made for `state_change`, and the identical residue radio exclusivity carries (§5.2). (b) wants the `init` hook, but that phase visits only `elem->form_control()` elements (EO4) and `<details>` is not one. (c) waits on `:first-of-type` in the selector engine, which would tighten `resolve_css_style.cpp`'s marker rule in the same change |
| ESO70 | **New browsing contexts cannot execute.** ES31 resolves `_blank` and unmatched target names to `target_kind:"new"`, but `UiContext` owns one current document and exposes no window/tab/context factory | add a host-level context creation API that returns a new browsing session/window, names it when requested, then execute the already-resolved `new` request without a DOM re-search |
| ESO71 | **Form POST transport is incomplete.** The package constructs form data and selects the request method, but the browsing waist currently accepts only URL/target | extend the native request/execution seam with method, headers, and body ownership; this is transport work, not a second activation policy |
| ESO57 | **`<dialog>` is entirely absent**, and the popover implementation fires no `beforetoggle`/`toggle` and has no light dismiss | both need a top-layer concept in the view tree and an Esc/outside-click policy; the Esc path can follow the context-menu and dropdown precedent (`event.cpp:9561`) |
| ESO58 | **Non-text `<input>` types have no interaction**: `range` (no thumb drag, no keys — `form.ls:130` says so), `number` (no spinner, no arrow-key step), and `file`/`color`/`date`-family, which `input_type_to_control()` degrades to `FORM_CONTROL_TEXT` (`view.hpp:2727`) | `range` and `number` are template-shaped and cheap; the picker types need host UI and are a separate decision |
| ESO59 | **Composite-widget keyboard policy is missing**: `<select multiple>` / listbox has no click or key handling at all, `<select>` has no typeahead, and radio groups have no arrow-key navigation | all three belong in `form.ls` next to the existing `dropdownkey` handler; the listbox additionally needs its option rows to be hit-testable |
| ESO60 | ~~**`autofocus` only inspects the first `<input>`.**~~ **landed 2026-09-01 (ES30)** — `focusinit` runs package tree-order policy over the same focus snapshot | — |
| ESO61 | **Small residue**: `accesskey` unimplemented; `beforeunload` unimplemented; `<img>` `load`/`error` not dispatched; `<area>` image-map activation absent; Ctrl+wheel zoom absent | individually cheap, none load-bearing; listed so they stop being rediscovered |

---

## 7. Priority

Ordered by how often the gap is hit by an ordinary page, not by implementation cost.

**Tier 1 — breaks ordinary pages today**

1. **contenteditable history** (ESO54/ESO43). Enter, line-break, and plain
   clipboard/drop commands use the DOM-range waist; history remains the
   visible contenteditable gap.
2. **New browsing-context execution** (ESO70 / ES31). Link click/Enter, fragment state, and existing target resolution are landed; `_blank` and unmatched names need the host window/session factory.
3. **Complete form submission transport** (ESO71 / F4). Local submit/reset and popover activation are landed; POST body delivery remains.

**Tier 2 — cheap, high visibility**

4. ~~Keyboard scrolling after `caretkey` declines (ESO48)~~ **landed**.
5. ~~`<details>` toggle (ESO56)~~ and ~~`:target` (ESO53)~~ **landed**; `:visited` remains blocked on history/privacy policy (ESO12).
6. ~~`dblclick` / `select` / cancelable `contextmenu` dispatch (ESO55)~~ **landed**.
7. ~~`tabindex` ordering, focus scroll-into-view, `autofocus` scope (ESO52 + ESO60)~~ **landed in `focus.ls`**.

**Tier 3 — bounded features**

8. `<input type=range>` and `number` interaction (ESO58).
9. Listbox, `<select>` typeahead, radio arrow keys (ESO59).
10. `<dialog>` and popover toggle events / light dismiss (ESO57).
11. Pointer boundary events and capture (ESO50).
12. The ESO61 residue.

Of these, only ESO71's submission transport residue (F4), link activation (DOM_State §6.1), and focus policy (DOM_State §6.2) were tracked anywhere before this document.

---

## Appendix A — how this ledger is verified

The status column is not maintained by inspection of the design docs, which is how the pre-absorption Appendix B acquired two stale rows (§4's clipboard claim; Pointer Events marked ❌ while three pointer types were being dispatched). It is maintained against the tree:

- **"Dispatched"** means a call site constructs the event and reaches `js_dom_dispatch_event` — in practice `radiant_dispatch_built_event` (`event.cpp:5806`) or `radiant_dispatch_window_event`. Registering an `on<type>` content attribute in `script_runner.cpp` is *not* evidence of dispatch; the ESO55 audit removed the unsupported `onkeypress` registration rather than keeping an inert attribute.
- **"Default action implemented"** means a package handler claims it (`lambda/package/dom/*.ls`), or a native block performs it and is reachable. A native block guarded by `radiant_behavior_claims_event` whose package counterpart declines is *not* implemented (§5.6).
- **A JS-layer implementation is recorded as such**, not as "implemented", because it is unreachable without a live JS dispatch scope and lives in the wrong home (§5.2).
- **A state written by nobody is not implemented** even when the selector engine matches it — the `:target` / `:visited` shape. Grep for writers of the `STATE_*` name, not for the pseudo-class.

When a row changes, change it here first. Docs that reference a default action link to this ledger rather than restating its status.

## Appendix B — provenance

This document absorbs, in full, Appendix B of `vibe/Lambda_Design_DOM_State.md` ("W3C/WHATWG ↔ Radiant: DOM events and default actions", added during F13/F14), which is deleted there as of 2026-08-28 and replaced by a pointer. The absorbed content is §1.2 (the three spec concepts), §3.1–§3.6 (the per-family tables), and §3.10 (the behavior-only hooks table). Two of its rows were corrected in the process, both in the direction of the tree rather than the doc:

- its clipboard/drag row claimed `deleteByCut` / `insertFromPaste` / `insertFromDrop` / `deleteByDrag` were handled by "`editing.ls` / `dom_edit.ls`"; that was false until the plain-text raw-range branches landed on 2026-09-01 (§4);
- its "not yet implemented" row marked Pointer Events ❌ with the note that `pointermove` existed *"solely as a hot-path guard name"*; `pointerdown`, `pointerup` and `pointermove` are all really dispatched with `preventDefault` honored (§3.3).

Everything else in this document is new: §3.7–§3.9 (forms, navigation, per-element activation), §4, §5, §6 (ESO48–ESO61) and §7.
