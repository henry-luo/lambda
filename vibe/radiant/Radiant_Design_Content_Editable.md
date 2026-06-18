# Radiant `contenteditable` — DOM-level interactive editable HTML

**Date:** 2026-05-19
**Status:** Proposal
**Layer:** DOM (between [Radiant_Design_Selection.md](Radiant_Design_Selection.md) / [Radiant_Design_Selection2.md](Radiant_Design_Selection2.md) below and the rich-text editor [../Radiant_Rich_Text_Editor3.md](../Radiant_Rich_Text_Editor3.md) / [../Radiant_Rich_Text_Editing.md](../Radiant_Rich_Text_Editing.md) above).
**Scope (confirmed):** editing-host (`contenteditable` attribute with modes `true`/`false`/`inherit`/`plaintext-only`) + `inputmode`/`enterkeyhint` hints + `beforeinput`/`input` (`InputEvent`) + focus in editable hosts + drag-and-drop into editable hosts. **Caret/Range/Selection are *referenced* from the Selection docs, not redefined here.** ARIA `role="textbox"`/`aria-multiline` is deferred to the Tier-D native-a11y work in [editor3 Part E.3](../Radiant_Rich_Text_Editor3.md).
**Explicitly out of scope:** `document.execCommand`, `document.queryCommand*`, `designMode`, and every other [legacy editing API](https://w3c.github.io/editing/docs/execCommand/) quirk codified in `ref/wpt/editing/run/*`.

---

## 1. Objective

Promote Radiant from "selection on a static DOM" to **interactive editable
HTML**: any element with `contenteditable="true"` (or `"plaintext-only"`)
becomes a focusable editing host whose user input — keyboard, IME, paste, drop —
is delivered as well-typed `InputEvent`s to the application, with the host's
visible caret/selection driven through the *existing* DOM Range/Selection
infrastructure.

This is the **DOM-level half** of the Radiant rich-text editor. The Lambda
`edit <…>` templates (Stage 1/2/3) and any JS page that uses
`contenteditable` are **the same two consumers of one plumbing**: this doc
specifies that plumbing.

The product north-star is *modern web-platform conformance, minus the legacy*:
we honour the W3C UI Events / Input Events Level 2 / HTML editing-host
contract, but we do not implement `execCommand` and never will.

---

## 2. What is already in the tree (verified)

The infrastructure is **substantially started**; this proposal formalises and
completes it. Concrete current state:

| Concern | Where | Status |
|---|---|---|
| Editing-host walk: `contenteditable="true"` / `""` (bool form) / `"plaintext-only"` | [`radiant/dom_range.cpp:2551–2592`](../../radiant/dom_range.cpp) | shipped (selection-side) |
| `contenteditable` recognised in focus / hit-test paths | [`radiant/event.cpp:315`, `:327`, `:342`, `:1008`](../../radiant/event.cpp) | partial — ad-hoc reads, no central host concept |
| Rich-element `beforeinput` dispatcher | [`radiant/event.cpp:1652`, `:5372`, `:6347`](../../radiant/event.cpp) (`dispatch_rich_beforeinput`) | partial — exists for `data-editable`/`contenteditable` subtree |
| `te_dispatch_beforeinput` + JS bridge | [`radiant/text_edit.cpp:450–456`](../../radiant/text_edit.cpp), [`radiant/text_edit.hpp:135–139`](../../radiant/text_edit.hpp) | shipped (textarea/input only) |
| `InputIntent` enum → `inputType` string map (`insertFromPaste` etc.) | [`radiant/event.cpp:866`](../../radiant/event.cpp) | shipped |
| `TextInputEvent` event type | [`radiant/event.hpp:96–122`](../../radiant/event.hpp) | shipped |
| Composition / IME (`te_ime_*`, `CompositionEvent`, `RDT_EVENT_COMPOSITION_*`) | [`radiant/text_edit.hpp`](../../radiant/text_edit.hpp), [`radiant/event.hpp`](../../radiant/event.hpp), [`radiant/ime_mac.mm`](../../radiant/ime_mac.mm) | shipped (macOS backend); platform-neutralisation = editor3 §3.9 |
| W3C Selection / DOM Range | [`radiant/dom_range.{hpp,cpp}`](../../radiant/dom_range.hpp), Selection design docs | shipped (large) |
| Clipboard transport (multi-MIME, sanitising, WPT-conformant) | [`radiant/clipboard.cpp`](../../radiant/clipboard.cpp), [Radiant_Clipboard_WPT_Status.md](Radiant_Clipboard_WPT_Status.md) | shipped (19/19 + 8 documented skips) |

What is **missing for full contenteditable**:

1. A **central `EditingHost` concept** (one place that owns the recognition,
   inheritance, and lookup of editable elements). The four `event.cpp` reads
   each re-derive the host ad-hoc.
2. `contenteditable="false"` islands inside an editable ancestor (the
   "non-editable inside editable" common case).
3. `plaintext-only` semantics (the schema-coerced single-line / unstyled mode
   browsers ship).
4. `isContentEditable` IDL on every `HTMLElement`, and `contentEditable` as a
   reflecting property.
5. The full **`InputEvent` Level 2 surface**: `data`, `dataTransfer`,
   `targetRanges` / `getTargetRanges()`, `isComposing`, `inputType` complete set.
6. Focus model for editable hosts (`tabindex=0` implicit, `focus()`/`blur()`,
   `activeElement`, `FocusEvent`) and click-to-focus consistency.
7. `inputmode` and `enterkeyhint` honoured by the editor3 §3.9 IME backend so
   on-screen keyboards do the right thing.
8. **Drop-into-editable** lowered to `beforeinput { inputType: "insertFromDrop" }`
   with a `DataTransfer`.
9. The JS-exposed API: `HTMLElement.contentEditable`, the `InputEvent`
   constructor, focus DOM, on-screen `dispatchEvent` parity with platform events.
10. The explicit **non-implementations** of `execCommand` / `queryCommand*` /
    `designMode` — currently absent rather than explicitly rejected; we record
    the deliberate omission so it is not re-added.

---

## 3. Architecture

```
                              ┌────────────────────────────────────────────────┐
                              │  Rich-text editor (Lambda `edit <…>` templates)│
                              │  and/or JS page using contenteditable          │
                              │  — both consume the same plumbing below —      │
                              └───────────────┬────────────────────────────────┘
                                              │  InputEvent (beforeinput/input)
                                              │  FocusEvent, DragEvent → beforeinput
                                              │  CompositionEvent
                                              ▼
                              ┌────────────────────────────────────────────────┐
                              │  Editing host (this doc)                       │
                              │  • EditingHost lookup (§4)                     │
                              │  • Focus / tab / activeElement (§5)            │
                              │  • InputEvent dispatcher (§6)                  │
                              │  • inputmode / enterkeyhint forwarding (§7)    │
                              │  • Drop-into-editable (§8)                     │
                              │  • Reject execCommand / designMode (§9)        │
                              └───────────────┬────────────────────────────────┘
                                              │  uses
        ┌─────────────────────────────────────┼─────────────────────────────────────┐
        ▼                                     ▼                                     ▼
┌─────────────────────────┐  ┌──────────────────────────────────┐  ┌──────────────────────────┐
│  Selection / DOM Range  │  │  Platform text-input             │  │  ClipboardStore          │
│  (Selection.md +        │  │  RdTextInputClient (editor3 §3.9)│  │  (radiant/clipboard.*)   │
│   Selection2.md)        │  │  TSF / NSTextInputClient / IBus  │  │  WPT-conformant 19/19    │
└─────────────────────────┘  └──────────────────────────────────┘  └──────────────────────────┘
```

**Single principle:** *the editing host has no opinion about document
semantics.* It produces well-typed `InputEvent`s; what they mean is decided
by the consumer above (Lambda editor commands, or a JS handler). The host
never mutates the document on its own — every change goes through a
consumer-issued mutation, exactly as a modern PM/Slate/Lexical editor expects.
This is the entire reason `execCommand` is rejected (§9).

---

## 4. The editing host

### 4.1 Modes (per HTML spec, in scope per Q3)

| `contenteditable` value | Meaning |
|---|---|
| `"true"` or `""` (bool form) | Element is an editing host. Subtree is editable except inside a descendant `="false"` island. |
| `"false"` | Element and its subtree are **not** editable, even inside an editable ancestor. |
| `"inherit"` (or absent) | Inherits editability from parent. The document's default is non-editable. |
| `"plaintext-only"` | Element is an editing host that accepts **text only**: paste/drop are coerced to `text/plain`, structural input types (`insertParagraph`, `formatBold`, `insertOrderedList`, …) are rejected. Backed by the existing `dom_range.cpp:2565` recognition. |

**Out:** `designMode` on `Document` — the legacy whole-document edit toggle.
Not implemented, not reflected, and the `Document.designMode` IDL getter
returns the literal string `"off"` and the setter is a silent no-op (so JS
feature detection works without enabling anything). Recorded in §9.

### 4.2 `EditingHost` lookup

One central, cached resolver replacing the four ad-hoc reads in `event.cpp`:

```cpp
// New: radiant/editing_host.hpp
struct EditingHost {
    DomElement* host;                  // nearest ancestor with ce="true"|""|"plaintext-only"
    enum Mode { Rich, PlaintextOnly };
    Mode mode;
    bool target_in_false_island;       // true iff query node sits inside ce="false" within host
};
bool editing_host_lookup(const DomNode* node, EditingHost* out);

// Reflecting IDL on HTMLElement
const char* html_element_get_contentEditable(DomElement*);   // "true"|"false"|"plaintext-only"|"inherit"
bool        html_element_get_isContentEditable(DomElement*); // computed, honours inheritance + false islands
void        html_element_set_contentEditable(DomElement*, const char*); // throws SyntaxError on bad value
```

Inheritance and `="false"` islands are evaluated lazily and cached per
layout-tick, invalidated by attribute mutation on the path to root.

### 4.3 Nested hosts and `="false"` islands

The classic "image with caption inside an editable paragraph; the image's
caption is a non-editable widget" case must just work. Concretely: the
`EditingHost` lookup returns the **nearest** host attribute and reports
`target_in_false_island=true` when the click/hit landed inside a
`="false"` subtree; all editor-side input handlers must respect this flag
(no-op insertion, but selection across the boundary still allowed per spec).

Nested `contenteditable="true"` hosts are allowed: focus and selection
naturally scope to the innermost host (matching WPT
`selection/contenteditable/cefalse-on-boundaries.html`).

---

## 5. Focus model in editable hosts

- A `contenteditable` host is **implicitly focusable** (treated as
  `tabindex=0` for sequential focus navigation), unless an explicit
  `tabindex` is set (which is honoured).
- Pointer click inside the host (and not inside a `="false"` island
  whose target intercepts the click) focuses the host and *also* updates
  the selection per the existing pointer-selection path in `dom_range`.
- `focus()` / `blur()` IDL on `HTMLElement` already exist; this doc only
  adds: focusing an editing host activates the platform IME (`§7` +
  editor3 §3.9 `RdTextInputClient`) and *de-activates* on blur.
- `Document.activeElement` reflects the focused host.
- `FocusEvent` (`focus`/`blur`/`focusin`/`focusout`) fires per UI Events 3
  on host transitions.
- `Esc` does **not** blur an editing host (matches browsers). Tab moves
  focus out (unless a consumer cancels `beforeinput` for `insertText`
  with `Tab`, in which case the consumer is opting in to tab-as-input).

---

## 6. `InputEvent` — `beforeinput` and `input`

This is the centrepiece. The existing `te_dispatch_beforeinput` and
`InputIntent → inputType` mapping ([event.cpp:866](../../radiant/event.cpp))
are generalised from textarea-only to the full editing host.

### 6.1 Event shape (Input Events Level 2, complete)

```webidl
interface InputEvent : UIEvent {
  readonly attribute DOMString? data;          // text being inserted, or null
  readonly attribute DataTransfer? dataTransfer;
  readonly attribute boolean isComposing;
  readonly attribute DOMString inputType;
  sequence<StaticRange> getTargetRanges();     // ranges that WILL be affected
};
```

`getTargetRanges()` returns a sequence of `StaticRange` (immutable snapshot,
not live `Range`) precomputed before dispatch — required for the
`input-events-get-target-ranges*` WPT subset and for IMEs / accessibility
tools that need to know *what* a backspace will delete before the model
mutates.

### 6.2 `inputType` disposition table

The spec defines ~30 `inputType` strings. We support the **structured**
ones (what a model-driven editor needs); the legacy execCommand-formatted
ones (`formatBold`, `formatItalic`, `formatJustifyCenter`, `backColor`,
`foreColor`, …) are **never dispatched by Radiant**, because we have no
execCommand. A consumer is free to *implement* bold by listening to a
keystroke and issuing its own model command; we just don't ship a
`beforeinput { inputType: "formatBold" }` of our own. **WPT
`input-events-exec-command.html` is therefore expected to fail and is
documented as a skip** (§11).

| inputType                                                                                                                                                                                                                                    | Source                                          | Disposition                                                                                                        |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `insertText`                                                                                                                                                                                                                                 | keypress (non-composition), IME commit          | dispatched                                                                                                         |
| `insertReplacementText`                                                                                                                                                                                                                      | autocorrect / suggestion accept (Tier-D-future) | dispatched when wired                                                                                              |
| `insertLineBreak`                                                                                                                                                                                                                            | `Shift+Enter`                                   | dispatched (host in plaintext-only honours; in Rich, consumer decides whether to split or `<br>`)                  |
| `insertParagraph`                                                                                                                                                                                                                            | `Enter`                                         | dispatched (suppressed in `plaintext-only` — becomes `insertLineBreak`)                                            |
| `insertOrderedList` / `insertUnorderedList`                                                                                                                                                                                                  | keystroke / toolbar (consumer-issued)           | dispatched **only when a consumer programmatically generates it** — Radiant never derives these from `execCommand` |
| `insertHorizontalRule`                                                                                                                                                                                                                       | input rule / consumer                           | dispatched on consumer request                                                                                     |
| `insertFromPaste`                                                                                                                                                                                                                            | clipboard paste                                 | dispatched; `dataTransfer` carries the read items (§ClipboardStore)                                                |
| `insertFromPasteAsQuotation`                                                                                                                                                                                                                 | shift-cmd-v variant                             | dispatched (consumer interprets)                                                                                   |
| `insertFromDrop`                                                                                                                                                                                                                             | drop into host                                  | dispatched; `dataTransfer` carries the drop items (§8)                                                             |
| `insertFromYank` (macOS Cmd-Y)                                                                                                                                                                                                               | platform                                        | dispatched on macOS                                                                                                |
| `insertCompositionText`                                                                                                                                                                                                                      | IME preedit replacement                         | dispatched (one transaction per composition session — editor3 §3.9)                                                |
| `insertLink`                                                                                                                                                                                                                                 | input rule / consumer                           | dispatched on consumer request                                                                                     |
| `deleteWordBackward` / `deleteWordForward`                                                                                                                                                                                                   | Alt/Ctrl+Backspace/Delete                       | dispatched                                                                                                         |
| `deleteSoftLineBackward` / `deleteSoftLineForward`                                                                                                                                                                                           | Cmd+Backspace / Cmd+Delete                      | dispatched                                                                                                         |
| `deleteHardLineBackward` / `deleteHardLineForward`                                                                                                                                                                                           | rare; per platform                              | dispatched when received                                                                                           |
| `deleteContentBackward` / `deleteContentForward`                                                                                                                                                                                             | Backspace / Delete                              | dispatched                                                                                                         |
| `deleteByCut`                                                                                                                                                                                                                                | Cmd-X                                           | dispatched                                                                                                         |
| `deleteByDrag`                                                                                                                                                                                                                               | drag-out completion                             | dispatched                                                                                                         |
| `historyUndo` / `historyRedo`                                                                                                                                                                                                                | Cmd-Z / Cmd-Shift-Z                             | dispatched (consumer decides which history to run)                                                                 |
| `formatBold`/`Italic`/`Underline`/`StrikeThrough`/`Superscript`/`Subscript`/`JustifyCenter`/`Left`/`Right`/`Full`/`Indent`/`Outdent`/`Remove`/`SetBlockTextDirection`/`SetInlineTextDirection`/`backColor`/`fontColor`/`fontName`/`fontSize` | execCommand only                                | **NOT dispatched.** Consumer-side keymap may implement these by issuing its own model command.                     |

`beforeinput` is **cancelable** (`preventDefault()` aborts the
default action — the actual document mutation never happens in the
host because the host doesn't mutate; cancellation signals the
consumer not to apply the edit). `input` is **not** cancelable.

### 6.3 Where Radiant's existing wiring slots in

- `event.cpp` `dispatch_rich_beforeinput` becomes the **single dispatch
  surface** for the editing host (textarea/input remain on
  `te_dispatch_beforeinput`, which itself dispatches the same
  `InputEvent` shape — one event, two source paths).
- The `InputIntent` enum becomes the *complete* input-spec coverage of the
  table in §6.2; today's enum covers the textarea subset (per `event.cpp:866`
  and surrounding entries).
- `StaticRange[]` for `getTargetRanges()` is computed at dispatch time by
  the same code paths that compute the about-to-delete range for the
  textarea backspace path; generalised to the host.

### 6.4 Tier list of `beforeinput` cancellation handling

The host **always** cancels its own no-op default (since it doesn't mutate);
the consumer's `preventDefault()` is the *signal* that the consumer also
chose not to apply the edit (purely informational on our side, decisive on
the JS side per spec). This matches PM/Slate/Lexical behaviour and is the
contract WPT `input-events-typing.html` expects.

---

## 7. `inputmode` and `enterkeyhint`

Per [HTML spec](https://html.spec.whatwg.org/multipage/interaction.html#input-modalities%3A-the-inputmode-attribute):

- `inputmode` ∈ {`none`, `text`, `decimal`, `numeric`, `tel`, `search`,
  `email`, `url`}. Honoured on editing hosts and `<input>`/`<textarea>`.
- `enterkeyhint` ∈ {`enter`, `done`, `go`, `next`, `previous`, `search`,
  `send`}.

Both are **hints to the IME / on-screen keyboard**, not behaviour
overrides. They are read by the editor3 §3.9 `RdTextInputClient` and
forwarded to the active platform backend (`NSTextInputClient`
`-validAttributesForMarkedText`, TSF `IText­Store­ACP::Set­Input­Scope`, IBus
property channel). On platforms without an OSK they are no-ops, **but
still readable from the IDL** so applications can rely on them.

No new event surface; only attribute reflection + a one-call forward when
the host gains focus.

---

## 8. Drag-and-drop into editable hosts (in scope per Q4)

Drag-and-drop *as a generic DOM API* is not this doc's concern. The
**editable-host integration** is:

- A drop whose target is inside an editing host triggers
  `beforeinput { inputType: "insertFromDrop", dataTransfer: <drop items> }`
  with `getTargetRanges()` set to the single insertion point (or replaced
  selection if the drop replaces a selection).
- A drag *out* of an editing host that completes successfully triggers
  `beforeinput { inputType: "deleteByDrag", … }` on the source host.
- A drag *within* the same host that completes is the pair
  (`deleteByDrag`, `insertFromDrop`) — the consumer may collapse to a
  `move` op (editor3 §3.7 `move_node`) or apply them independently.
- The `DataTransfer` exposed on `beforeinput` matches what
  [Radiant_Clipboard_WPT_Status.md Phase 9](Radiant_Clipboard_WPT_Status.md)
  already shipped for clipboard — the same native `DataTransfer` object,
  full items/files/types views.

`dropEffect` honours the spec: `none` → no `beforeinput` fired; `copy`,
`move`, `link` → the consumer is told via `dataTransfer.dropEffect`.

The `plaintext-only` host filters the `DataTransfer` to `text/plain` only
before dispatch (spec §[plaintext-only paste/drop]).

---

## 9. Explicitly NOT supported (the "no legacy" line)

> **⚠️ Superseded for `execCommand`.** This section reflects the original
> "modern contract, no legacy" scope. The follow-on proposal
> [Radiant_Design_Content_Editable2.md](Radiant_Design_Content_Editable2.md)
> **reverses the execCommand decision** — `execCommand` / `queryCommand*` /
> `designMode` are now in scope, implemented as a built-in editing-command
> *consumer* layered on the `InputEvent` plumbing (the rest of this section
> still holds). See that doc §2 for the reconciliation.

This proposal **deliberately does not implement** any of the following.
Each entry says what the JS-visible behaviour is, so feature detection
fails cleanly and tests are unambiguous.

| Legacy API | Radiant behaviour |
|---|---|
| `document.execCommand(name, …)` | Returns `false`. Never mutates. Never dispatches `beforeinput`. |
| `document.queryCommandSupported(name)` | Returns `false` for every command. |
| `document.queryCommandEnabled` / `…Indeterm` / `…State` / `…Value` | Return `false`/`false`/`false`/`""` respectively. |
| `document.designMode` | Getter returns `"off"`. Setter is a silent no-op. |
| `formatBold`/`…` `inputType` values | **Never dispatched by Radiant** (§6.2). The host doesn't synthesize these; a consumer that wants Bold listens for `Cmd+B` keystrokes and issues its own model command. |
| Browser-quirk markup outputs (`<font>`, `<b>`/`<i>` vs `<strong>`/`<em>` choices, `<br>` normalisation, list-merge heuristics, etc.) | Not produced. Consumer chooses its own markup; the host is markup-agnostic. |
| `document.execCommand('insertHTML', ...)` paste path | Not exposed. Paste = `beforeinput { insertFromPaste, dataTransfer }`; consumer parses and inserts. |

The reasoning is concentrated in [editor3 §2.1](../Radiant_Rich_Text_Editor3.md):
WPT's `editing/run/*` corpus codifies browser execCommand *quirks*; enforcing
it would be an anti-goal. We pin the modern *contract* (selection +
beforeinput + InputEvent + composition) and reject the quirk surface.

---

## 10. JS API surface exposed (Q2: both Lambda + JS pages)

A JS page on Radiant gets the **web-platform-conformant** subset of the
editing surface, sufficient for any modern PM/Slate/Lexical/CKEditor-class
editor to run unmodified (modulo their own execCommand fallbacks, which
they all have a non-execCommand path for):

- `HTMLElement.contentEditable` / `.isContentEditable` (§4)
- `HTMLElement.inputMode` / `.enterKeyHint` (§7)
- `HTMLElement.focus({preventScroll})` / `.blur()` /
  `Document.activeElement` (§5)
- `InputEvent` constructor + the full Level-2 attribute set incl.
  `getTargetRanges()` (§6.1)
- `addEventListener('beforeinput' | 'input' | 'focus' | 'blur' |
  'compositionstart' | 'compositionupdate' | 'compositionend' |
  'dragenter' | 'dragover' | 'drop', …)`
- `StaticRange` constructor
- `DataTransfer` (already native per Clipboard Phase 9)
- The W3C Selection / Range API (already shipped; this doc just
  guarantees it stays consistent under editing-host edits)

The Lambda `edit <…>` template path **subscribes to the same events**
through `radiant_dispatch_rich_*` in `event.cpp` — one dispatcher, two
listeners.

---

## 11. WPT test suites to cover

Same tier model as [editor3 §2](../Radiant_Rich_Text_Editor3.md). The
runner and status-doc pattern are copied from
[Radiant_Clipboard_WPT_Status.md](Radiant_Clipboard_WPT_Status.md).

### 11.1 Tier A — Conformance baseline (must be 100% on the curated subset)

Built as an `extended` test (`test_wpt_contenteditable_gtest.exe`, registered
in `build_lambda_config.json`). **Not yet wired into
`make test-radiant-baseline`** — 14 real failures remain (see baseline below).
Lives in `test/wpt/test_wpt_contenteditable_gtest.cpp`, modelled on
`test/wpt/test_wpt_selection_gtest.cpp` and driven through the existing
`test/wpt/wpt_testharness_shim.js`. It shares the selection runner's parallel
custom `main()`; reftests / `*-manual` / testdriver tests auto-skip, and crash
tests pass by surviving the `--document` load.

**Concrete scope — this runner pulls in exactly four WPT directories** (every
`*.html`, except `html/interaction/focus/` which is filtered to its
contenteditable subset — the dir otherwise holds ~170 generic focus tests).
Everything else that mentions `contenteditable` is excluded per §11.6.

| WPT dir | Cases | What it pins / status | This doc § |
|---|---|---|---|
| `ref/wpt/input-events/` | 25 | The §6 `InputEvent` surface. **Blocked headless today:** 23/25 drive synthetic typing via `test_driver`, which the `js` runtime does not deliver, so they auto-skip (Phase 8F); `input-events-exec-command` is tracked by the CE2 execCommand/Chrome-editing workstream. Genuine conformance begins once synthetic input lands (CE-3). | §6 |
| `ref/wpt/editing/crashtests/` | 137 | Engine robustness — pass iff the runtime survives the `--document` load (no crash *signal*; a clean error exit, e.g. an uncaught exception on an unimplemented API, still counts as survived). | §4–§6 |
| `ref/wpt/html/interaction/focus/` | 7 | Focusability / `activeElement` / tabindex of editing hosts (contenteditable subset of the dir). | §5 |
| `ref/wpt/html/editing/editing-0/` | 25 | `contentEditable` / `isContentEditable` / `designMode` / `spellcheck` / `autocapitalize` IDL + editing-host basics; ~15 are reftests that auto-skip (layout-compare path). | §4 |

**Initial baseline (2026-06, debug build): 194 cases → 139 pass, 41 skip, 14
fail.** The failures are real CE-1/CE-2 conformance gaps — IDL reflection for
`contentEditable` / `designMode` / `spellcheck` / `autocapitalize` /
`writingsuggestions`, and the focus-fixup / tabindex / autofocus model — plus
**one genuine engine crash** (`editing/crashtests/insertparagraph-in-listitem-in-svg-followed-by-collapsible-spaces`
→ SIGABRT). Current progress is tracked in
[Radiant_Design_Content_Editable2.md](Radiant_Design_Content_Editable2.md).

The **selection-side** editing-host Tier-A tests
(`selection/contenteditable/`, `selection/textcontrols/`) already run under
the recursive `test_wpt_selection_gtest.cpp` and are **not duplicated**
here. Clipboard (`clipboard-apis/`, already 19/19) and composition
(`editing/event.html`, editor3 §3.9) likewise live in their own runners.

### 11.2 Tier B — Informational gauge (tracked, NEVER gates CI)

Single number in CE2; movement is reviewed, not required.

- `ref/wpt/editing/run/*` (the entire `execCommand` quirk corpus —
  `bold.html`, `delete.html`, `indent.html`, `createlink.html`,
  `forwarddelete.html`, `backcolor.html`, …): **expected to mostly fail by
  design** (§9). We track passes anyway because a subset incidentally
  tests caret-navigation behaviour the modern path *should* honour.
- `ref/wpt/input-events/*.tentative.html` (`get-target-ranges` tentative
  variants, `range-exceptions`): track; promote individually to Tier A
  once the spec stabilises.
- `ref/wpt/editing/edit-context/*`: the experimental `EditContext` API —
  candidate successor to contenteditable for advanced editors, not yet
  shipped widely. Tracked informationally; do not implement in Stage A.

### 11.3 Tier C — Scenario source (not executed as WPT)

Mined into our own model tests + UI-automation, never run as WPT:

- `ref/wpt/editing/other/*` (deletion, caret-navigation around inline
  boundaries, double-click range selection in list items) — 84 files,
  54 execCommand-dependent
- `ref/wpt/editing/whitespaces/*` (white-space normalization scenarios) —
  18 files, 12 execCommand-dependent
- `ref/wpt/selection/caret/*`, `selection/move-by-word-*` — **now executed**
  by the recursive `test_wpt_selection_gtest.cpp` (tracked there, several
  currently failing); no longer pure scenario-source.

### 11.4 Explicitly skipped (with rationale, per Clipboard-WPT-status pattern)

| Bucket | Why skipped |
|---|---|
| `contenteditable/` (top-level dir: `plaintext-only.html`, `select-text-change-crash.html`, `synthetic-height.html`/`-ref`) | Ad-hoc grab-bag, **not** a structured suite — three unrelated WPT test *types* with no coherent editing-host coverage: (1) one IDL-reflection testharness test (`plaintext-only.html`, 2 asserts on `isContentEditable`/`contentEditable === "plaintext-only"`) — already covered by the CE-1 IDL tests authored against §4.1; (2) one crash regression test (`select-text-change-crash.html`, `outerText` on an `<option>` in a `<select contenteditable>` — passes by not crashing, no asserts); (3) one layout **reftest** (`synthetic-height.html` ↔ `-ref`, visual-match that empty editable blocks get a synthesized line height) which needs the render-compare path, not the JS harness. Nothing to gain by wiring it up; the only in-scope assertion (plaintext-only reflection) is covered elsewhere. Contrast `selection/contenteditable/` (§11.1 Tier A), which *is* a structured selection-in-editing-host suite. |
| `editing/run/*` (Tier B) | execCommand corpus — superseded by CE2's execCommand/Chrome-editing gauge. |
| `editing/manual/*` | Manual gestures; out of automatable scope. |
| `editing/plaintext-only/*` that depends on legacy execCommand semantics | Replaced by `inputType` filtering tests we author against §4.1's `plaintext-only` mode. |
| `editing/edit-context/*` | Experimental successor API; revisit in a later stage. |
| `selection/shadow-dom/*` (subset) | Shadow DOM is a separate Radiant work-stream; partial-defer. |
| `uievents/keyboard/*` tests that depend on a specific OS layout | Documented per-test as platform-specific. |

### 11.5 Status tracking

[Radiant_Design_Content_Editable2.md](Radiant_Design_Content_Editable2.md)
is the living tracker for headline numbers, per-file Tier-A progress,
documented `SKIP_SUBSTRINGS` rationale, and the Chrome-editing gauge.

### 11.6 Why the other `contenteditable` WPT areas are excluded

Across WPT, ~440 HTML files reference `contenteditable`. This runner
deliberately covers only the four directories in §11.1; the rest fall into
four exclusion buckets. (Counts verified 2026-06.)

**A. Already covered by another runner — would double-run.**

| Area | Files | Runs instead under |
|---|---|---|
| `selection/contenteditable/` | 8 | `test_wpt_selection_gtest.cpp` (recursive); `cefalse-on-boundaries` already 4/4 |
| `selection/textcontrols/` | 5 | same selection runner |
| `clipboard-apis/` | — | `radiant/clipboard.cpp` + `Radiant_Clipboard_WPT_Status.md` (19/19) |

**B. execCommand corpus — Radiant rejects the API by design (§9), so these
fail on purpose.** Kept out of the must-pass runner; tier semantics in
§11.2/§11.3, skip rationale in §11.4.

| Area | Files (execCommand-dependent) | Disposition |
|---|---|---|
| `editing/run/` | 42 full corpus — `bold`, `delete`, `indent`, `createlink`, `fontname`, … (~8 literally contain `contenteditable`; the rest enable editing via `designMode`/data files) | Tier B gauge (§11.2), never gates |
| `editing/other/` | 84 (54 execCommand) | Tier C scenario source (§11.3) |
| `editing/whitespaces/` | 18 (12 execCommand) | Tier C scenario source (§11.3) |
| `editing/plaintext-only/` | 13 (11 execCommand) | Mostly expected-fail; §4.1 `plaintext-only` behaviour is verified by our own authored `inputType`-filtering tests instead (§11.4) |

**C. Tests a different / legacy feature, not the modern editing-host
contract.**

| Area | Files | Why excluded |
|---|---|---|
| `uievents/` (cE subset) | 11 | The `textInput`/`TextEvent` **legacy event** (deprecated) + one manual test — superseded by the modern `beforeinput`/`input` already covered by `input-events/`. General `KeyboardEvent` coverage is a separate UI-events concern, not this runner's. |
| `editing/edit-context/` | 2 | Experimental `EditContext` successor API; not in Stage A (§11.2) |
| `editing/manual/`, `*-manual.html` | — | Require human gestures; no headless path |
| `focus/` (top-level) | 1 | Lone iframe-scroll-into-view case; real focusability coverage is `html/interaction/focus/` (included in §11.1) |

**D. Use `contenteditable` only as a fixture to test something else —
not editing-host conformance.**

| Area | Files | What it actually tests |
|---|---|---|
| `css/` | 100 | CSS **reftests** (caret-color, `user-select`/`user-modify`, `::selection`, white-space) — belong to the layout-compare suites, not the JS harness |
| `wai-aria/` `core-aam/` `accname/` `accessibility/` `forced-colors-mode/` | ~19 | Accessibility / forced-colors behaviour |
| `inert/` `virtual-keyboard/` `custom-elements/` `shadow-dom/` `mathml/` `dom/` | ~15 | Inertness, on-screen keyboard, components, shadow trees — `contenteditable` is incidental |
| `contenteditable/` (top-level) | 4 | Ad-hoc grab-bag — see the §11.4 row |

Even within the four included dirs, much is non-assertion-bearing and
auto-skips: `editing/crashtests/` is crash-only (0 assertions, pass by
surviving); ~15 of the 25 `html/editing/editing-0/` files are reftests; and
23/25 `input-events/` tests need `test_driver` synthetic input the headless
`js` runtime does not provide (Phase 8F). So the runner's effective
assertion-bearing coverage today is the small testharness subset of focus +
editing-0, plus the 137-case crash-survival net — the InputEvent surface
stays dark until synthetic input lands.

---

## 12. Phased Plan

| Phase | Scope | Exit criterion |
|---|---|---|
| **CE-1 — `EditingHost` core** | Extract the four ad-hoc `event.cpp` reads into `radiant/editing_host.{hpp,cpp}`; implement `contentEditable`/`isContentEditable` IDL with inheritance + `="false"` islands + `plaintext-only`; cache invalidated by attribute mutation. | All `selection/contenteditable/cefalse-on-boundaries.html` and `selection/contenteditable/collapse.html` pass; existing dom_range editing-host walk delegates to the new core. |
| **CE-2 — Focus model** | Implicit `tabindex=0` for hosts; pointer/keyboard focus; `FocusEvent` on transitions; `activeElement` reflects host. | `focus/focus-event-targets` (subset) + `selection/textcontrols/focus.html` pass; host-blur deactivates IME. |
| **CE-3 — `InputEvent` complete** | Generalise `te_dispatch_beforeinput` from textarea-only to the editing host; implement the full §6.2 `inputType` table (modulo the format* exclusions); `getTargetRanges()` returning `StaticRange[]`; `dataTransfer` on paste/drop; `isComposing`. | `input-events/input-events-typing.html`, `input-events-get-target-ranges*` non-tentative, `input-events-cut-paste`, `input-events-delete-selection`, `idlharness` all pass. `input-events-exec-command.html` documented as skip. |
| **CE-4 — `inputmode` / `enterkeyhint`** | Attribute reflection on `HTMLElement`; one-call forward to `RdTextInputClient` (editor3 §3.9) on host focus. | IDL tests in `html/dom/idlharness*` for these attributes pass; macOS IME observed receiving the hint via `NSTextInputClient` validAttributes. |
| **CE-5 — Drop into editable** | Lower drop-on-editable to `beforeinput {insertFromDrop, dataTransfer}`; pair with `deleteByDrag` for completed drag-outs; `plaintext-only` filters DataTransfer to text/plain. | Drag/drop subset of `input-events-cut-paste` and a new `test/ui/rte_drop.json` UI-automation pass. |
| **CE-6 — Reject legacy explicitly** | `execCommand` returns `false`; `queryCommand*` per §9; `designMode` no-op; `format*` `inputType` never produced. | A new `test/wpt/test_wpt_contenteditable_negative_gtest.cpp` asserts each non-API: `typeof document.execCommand === 'function'` but `document.execCommand('bold')` returns `false`, no DOM mutation, no `beforeinput` fired. |
| **CE-7 — JS API surface + CE2 tracking** | Expose `contentEditable`/`isContentEditable`/`inputMode`/`enterKeyHint` IDL; `InputEvent` constructor; `StaticRange`. Track WPT progress in CE2. | A pure-JS PM minimal example runs against Radiant headlessly without touching execCommand. CE2 tracking updated. |

**Dependencies:** CE-1 → CE-2/CE-3; CE-3 → CE-4/CE-5; CE-6 and CE-7 land
last, in parallel. CE-3 unlocks editor3 §3.6 (WPT Tier A: input) — they
share the runner.

---

## 13. Acceptance Criteria

- **Editing host is a single source of truth** (`radiant/editing_host.*`);
  the four ad-hoc `event.cpp` reads are replaced.
- **`make test-radiant-baseline` green** with the new
  `test_wpt_contenteditable_gtest` Tier-A binary added.
- **Negative-API tests pass:** `document.execCommand('bold')` returns
  `false`, mutates nothing, fires no `beforeinput`; `document.designMode`
  getter returns `"off"`, setter is a no-op; no `formatBold` (etc.)
  `inputType` is ever dispatched.
- **`plaintext-only` works:** structural inputs (`insertParagraph`,
  `insertOrderedList`) become `insertLineBreak`/no-op; paste/drop coerce
  to `text/plain`.
- **`getTargetRanges()` is correct** for every dispatched `beforeinput`
  (asserted by the WPT `input-events-get-target-ranges*` subset).
- **One plumbing, two consumers:** the Stage-2 `edit <rte_doc>` template
  and a pure-JS PM minimal page both run on the same dispatcher with no
  per-consumer divergence.
- **CE2 tracking updated** with headline numbers, per-file Tier-A matrix,
  `SKIP_SUBSTRINGS` rationale, and the Chrome-editing gauge number.

---

## 14. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| WPT `editing/run/*` is huge and shows up red. | Hard tier split (§11.2): Tier B, **never** in `test-radiant-baseline`. Documented expected failure per §9. |
| `getTargetRanges()` requires knowing the about-to-delete range *before* the consumer applies the edit — extra work on the dispatch hot path. | The textarea path already computes the same range for its own delete handling; lift that computation into a shared helper. The cost is a few extra Range walks per keystroke (cheap; the keystroke path already does similar work for caret rendering). |
| `="false"` islands inside `="true"` is a notorious source of selection-boundary bugs. | The Tier-A subset is *specifically* the `cefalse-on-boundaries` family + selection `modify*` cases; gate CE-1 on those green. |
| IME integration depends on editor3 §3.9 (`RdTextInputClient`) being landed first. | CE-3 can land with the existing macOS IME (`ime_mac.mm`) unchanged; CE-4's hint-forwarding is the only piece that *needs* §3.9. Sequence accordingly. |
| Some existing `text_control.cpp` paths fire `beforeinput` only "informationally" today (`text_edit.cpp:232–234`); generalising them must not change textarea semantics. | CE-3 keeps the textarea path on `te_dispatch_beforeinput` (now a thin wrapper over the shared dispatcher); regression-guard with the existing `test_dom_range_gtest` + `test_cmdedit_gtest`. |
| Shadow-DOM hosts and editable hosts interacting (focus, selection across boundaries) is partly unspecified. | Defer (§11.4 skip). Re-evaluate when Radiant's Shadow-DOM work stabilises. |

---

## 15. Open questions

1. **`StaticRange` lifetime when the consumer mutates.** Spec says
   `StaticRange` does not update under DOM mutation. We snapshot at
   dispatch time. Confirm this matches every WPT
   `input-events-get-target-ranges-*` case (they expect pre-mutation
   ranges) — believed to but verify in CE-3.
2. **Tab-as-input vs tab-as-focus-next.** Spec is ambiguous on whether
   `Tab` in an editable host fires `beforeinput { insertText, " " }`
   that the consumer can cancel to allow focus-next. We propose: fire,
   consumer cancels for focus-next; the default is to dispatch — matches
   PM/Slate. Confirm against `uievents` Tab tests.
3. **EditContext API (`editing/edit-context/`).** Tracked in Tier B; no
   commitment in this proposal. Decide in a later stage whether to
   adopt as a parallel path for advanced editors.

---

## 16. Summary

Radiant already has the moving parts (selection, range, IME, clipboard,
ad-hoc contenteditable detection, partial `beforeinput`). This proposal
**formalises them into the W3C editing-host contract**, exposes the
contract to both Lambda `edit <…>` templates and JS pages through one
dispatcher, and explicitly rejects the legacy `execCommand` /
`designMode` surface. The web-platform compliance bar is the curated
WPT subset (§11 Tier A) at 100 %; the legacy quirk corpus is a
non-gating informational gauge. Above this layer, the
[rich-text editor proposal](../Radiant_Rich_Text_Editor3.md) is the
*editor*; this doc is the *editable surface* the editor edits on.
