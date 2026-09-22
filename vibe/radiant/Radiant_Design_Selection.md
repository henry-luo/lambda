# Radiant — Selection, Caret & DOM Range Design

**Status:** current working design, verified 2026-09-22.

**Scope:** DOM `Range` and `Selection`, document and text-control carets,
selection geometry, mutation survival, and the selection-facing editing event
contract.

**Spec linkage:** **D7.2.5** assigns user-agent editing policy to the shipped
DOM package while Radiant provides checked DOM/Selection mechanisms.
**D7.5.3** keeps the Lambda/Radiant selection bridge explicit, revisioned, and
one-way at each boundary.

This record supersedes the former Phase-8 and unified-API proposals. It states
the final architecture, not its implementation sequence. The brief history is
retained in [Appendix A](#appendix-a-consolidated-history).

---

## 1. Design decision

Selection has one logical model and several derived presentations.

- Each document has a `DomSelection` whose ranges use DOM boundaries.
- `EditingSelection` is the StateStore facade that identifies the active
  surface: none, a document range, or a text-control selection.
- A collapsed document range is the document caret. Anchor, focus, direction,
  and the observable selection revision are derived from the logical range
  state, never from paint geometry.
- Text controls keep a separate value-relative selection, because their text
  is not an ordinary child-text DOM subtree. A focused control selection can
  coexist with a non-empty document selection.
- Caret rectangles, selection rectangles, blink state, drag state, and
  event/debug snapshots are projections. They may be discarded and rebuilt;
  they have no authority over logical boundaries.

Every producer—DOM API, pointer gesture, keyboard movement, editing action,
model commit, and DOM mutation—commits through the StateStore/DOM selection
writer before presentation is refreshed. View-and-byte-offset inputs remain
valid adapters for hit testing and rendering, but they do not create a second
selection model.

The document selection has bounded range storage. The normal DOM-facing
selection exposes its primary range, while rich table editing may retain a
small bounded set of cell ranges internally. This is an editing extension;
ordinary script selection remains single-range in practice.

## 2. Boundary and range semantics

A `DomBoundary` is a `(node, offset)` pair:

- Character-data offsets are UTF-16 code-unit offsets, matching Web APIs.
- Element and fragment offsets are child positions.
- Layout, shaping, and storage may use UTF-8 byte offsets, but conversion is
  confined to the DOM/layout boundary.

A `DomRange` is live: its start and end remain ordered and follow DOM mutation
rules. Range objects may outlive their use by the document selection, while
the selection itself owns the currently selected range set. Boundary comparison
is DOM-tree order, not layout order; disconnected trees are never silently
compared as one document.

The DOM API distinguishes raw and rendered strings:

- `Range.toString()` serializes the raw range text.
- `Selection.toString()` follows rendered-selection rules, excluding text that
  is not user-selectable or not rendered as selectable text.

## 3. Selection operations and events

The DOM-facing Selection and Range surfaces operate on the same logical state
that pointer and keyboard input uses. The essential operations are range
creation and mutation, add/remove/collapse/extend, base-and-extent selection,
containment, document deletion, and stringification. Failed DOM operations
leave the prior valid selection intact and report the appropriate DOM error.

`selectionchange` is coalesced to an asynchronous document event after a
committed logical change. Text controls receive their element-scoped selection
notifications through the same task-boundary discipline. `selectstart` is the
cancelable pre-selection event for user-initiated selection starts. Selection
event delivery observes the committed selection; listeners cannot observe a
partially adjusted range.

`Selection.modify(alter, direction, granularity)` is supported as a
browser-convention navigation API. `move` collapses at the new focus and
`extend` preserves the anchor. Logical `forward`/`backward` and visual
`left`/`right` movement are distinct; the latter uses the visual bidi path for
character and word movement. Supported granularities are `character`, `word`,
`line`, `lineboundary`, `sentence`, `sentenceboundary`, `paragraph`,
`paragraphboundary`, and `documentboundary`. Empty selections are no-ops and
unknown values raise `SyntaxError`.

Selection mutation is not a contenteditable default action. Under **D7.2.5**,
the DOM package owns editing policy; Radiant exposes the checked range,
selection, geometry, and notification mechanisms that the package and page
scripts use.

## 4. Mutation, reflow, and lifecycle

Before a DOM structural or character-data mutation becomes observable, all
affected live-range boundaries are adjusted by DOM-tree position. Removing a
subtree moves endpoints inside it to the former child position in its parent;
insertion, splitting, merging, and text replacement adjust offsets according
to the same live-range rules. A selection is refreshed from its adjusted
range only after the mutation commits, so it cannot retain a poisoned or
retired node.

Reflow never defines or invalidates a logical boundary. It invalidates only
geometry projections, which are resolved again from the retained DOM boundary
and current layout. If a source-model commit regenerates a projected DOM, the
revisioned Lambda/Radiant bridge resolves and commits the replacement boundary
once; stale model revisions cannot overwrite a newer user selection
(**D7.5.3**).

## 5. Text controls and contenteditable

Text controls have an independent selection over their value string:

- offsets are UTF-16 positions in the control value;
- start, end, and direction persist across blur in the control state;
- the focused control owns its visible caret and range painting;
- its selection is not fabricated as a DOM range through anonymous text.

`EditingSelection` selects this text-control branch when it is the active
editing surface. Document selection remains valid outside the control and can
be restored when focus returns to document content. The rendered selection
string and selection events account for the control selection where Web-facing
semantics require it.

`contenteditable`, on the other hand, uses document ranges. Its ordinary
editing actions consume immutable target ranges and commit through the same
selection model. An author handler that takes ownership of an editing action
does not fall through to a hidden UA mutation.

## 6. Geometry and interaction

Hit testing maps a viewport point to the nearest DOM boundary. Layout resolves
that boundary to caret and selection fragments; the painter uses the same
glyph-aware X mapping for caret placement and range edges. This keeps rendered
selection highlights aligned with the caret across text runs and line
fragments.

Pointer drag metadata, desired X for vertical navigation, blink state, and
dirty rectangles belong to interaction or presentation state. They guide the
next movement or repaint but never replace the anchor and focus. Focus,
selection, and contenteditable-host recognition share the same editing-surface
resolution, including `contenteditable="false"` islands and non-selectable
subtrees.

## 7. Gaps to full browser-compatible selection and caret behavior

Radiant has the required architecture for DOM selection and ordinary editing,
but it does not yet claim complete browser-equivalent selection behavior. The
remaining gaps are deliberately separated from general contenteditable
support.

| Area | Current boundary | Needed for browser-level compatibility |
|---|---|---|
| Word navigation | `word` uses a simple alphanumeric-versus-other classifier. | Unicode UAX #29 word segmentation with locale tailoring, including CJK, Thai, emoji, combining marks, and script transitions. |
| Sentence navigation | `sentence` moves to text-node edges; `sentenceboundary` uses a boundary fallback, not linguistic sentence detection. | Locale-aware sentence segmentation that recognizes punctuation, abbreviations, and non-Latin sentence conventions. |
| Line navigation | Host layout stops are used when available; no-layout cases fall back to structural text boundaries. | Browser-matched visual-line and caret-affinity rules for wrapped inline content, floats, fragmentation, bidi transitions, vertical writing, transforms, and complex inline layout. |
| Character and bidi navigation | Visual left/right character and word paths are bidi-aware. Full extended-grapheme and browser caret-affinity parity is not claimed. | UAX #29 grapheme boundaries plus exhaustive UAX #9/CSS Writing Modes conformance, including ambiguous run boundaries and vertical text. |
| Composed-tree selection | Light-DOM and same-document selection are the supported model. | Shadow DOM selection scopes, composed ranges, slotting behavior, and browser-equivalent closed-root and cross-context rules. |
| Text-control target ranges | Controls correctly retain their own value selection, but target-range representation remains a control-boundary adapter. | A browser-equivalent representation for every `InputEvent.getTargetRanges()` and control-selection edge case without synthetic DOM-value boundaries. |
| Platform text services | DOM composition and selection mechanisms are present. | Full native IME, accessibility, spell-check, and platform caret-policy parity across macOS, Windows, and Linux. |
| Conformance evidence | Focused unit, WPT, and editor integration probes cover the supported surface. | A maintained, browser-compared WPT matrix for Selection, Range, text controls, writing modes, and complex-script navigation. |

These are conformance and platform-fidelity work items. They do not mean that
ordinary contenteditable typing, DOM ranges, Editor.js, CodeMirror, or
ProseMirror support is absent. New defects and prioritised work remain in the
central issue ledger rather than creating a second selection-specific ledger.

## 8. Verification contract

Selection changes require proportionate verification at three levels:

1. Boundary, live-range, and Selection API unit coverage, including UTF-16
   offsets and mutation adjustment.
2. DOM/WPT coverage for observable Selection, Range, stringification, event,
   text-control, and iframe behavior.
3. UI/editor probes for geometry, pointer selection, physical keyboard input,
   composition, and authoritative editor-state reconciliation.

Tests must assert logical boundaries and observable DOM state as well as paint
geometry. A passing rendering probe alone is not Selection API conformance;
likewise, a DOM-only test does not establish caret-placement fidelity.

---

## Appendix A. Consolidated history

| Consolidated record | Lasting result |
|---|---|
| Original range/selection design | Established DOM boundaries, live ranges, UTF-16 Web offsets, and selection geometry as separate concerns. |
| Phase-8 closure proposal | Identified the event, rendered-string, text-control, iframe, synthetic-input, and subtree-removal work needed to make the Selection surface observable and mutation-safe. |
| Unified-API proposal | Settled the final authority rule: `EditingSelection`/`DomSelection` own logical state; caret and selection presentation are derived projections, not competing writers. |

The phased plans, obsolete WPT scoreboards, proposed file layouts, and interim
migration steps are intentionally omitted. Their decisions are represented by
the final design above; repository history retains the detailed chronology.
