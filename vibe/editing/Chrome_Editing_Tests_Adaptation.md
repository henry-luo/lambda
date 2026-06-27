# Adapting the Chromium Editing Test Suite

**Date:** 2026-06-26
**Status:** Proposal + implementation plan
**Source:** `test/editing` → `../../lambda-test/editing` = Chromium/Blink `web_tests/editing/` (~2,751 HTML tests)
**Target:** `test/editor-js/` JS reference editor

---

## 0. TL;DR

The Chromium editing tests are driven by `assert_selection(inputHTML, tester, expectedHTML)`, where the tester mutates a `contenteditable` via `document.execCommand(...)` and `selection.modify(...)`. We adopt them in **scenario-harvest** mode: convert the input + operation sequence into our fixture format, run it through our editor, and record OUR output as the expected (`output.html`). This harvests real-world editing sequences (typing simulations, delete patterns, whitespace handling) as new fixtures — passing by construction, and validated by the corpus-wide invert-roundtrip invariant.

We do **not** port Chromium's expected DOM as a conformance gate — it encodes legacy `execCommand` quirks and non-normalized inline structure that our PM/Slate-class model deliberately rejects (consistent with `Radiant_Rich_Text_Editor3.md §2`).

---

## 1. Test format

```js
assert_selection(
  '<div contenteditable><span>|XXXX</span></div>',   // input: | = caret, ^…| = range
  selection => {
    for (var i = 0; i < 3; ++i)
      selection.document.execCommand('insertText', false, 'a');
    for (var i = 0; i < 2; ++i)
      selection.modify('move', 'forward', 'character');
    // …
  },
  '<div contenteditable><span>aaaXX…|</span></div>')  // expected
```

- **Markers:** `|` = collapsed caret; `^` = anchor + `|` = focus for a range. Maps 1:1 to our `<cursor></cursor>` / `<anchor></anchor>` / `<focus></focus>`.
- **Root:** `<div contenteditable>` with arbitrary inline or block content. Maps to our `<doc>`; bare inline content is wrapped in `<p>`.
- **Driver:** `execCommand` (operations) and `selection.modify` (caret navigation).

---

## 2. Command comparison — Chromium `execCommand` vs. our commands

This is the mapping the harvester uses, and the reference for revising our command names. Columns: Chromium command, our current command/event, and a naming note.

### 2.1 Inline formatting (marks)

| Chromium `execCommand` | Our command | Note |
|---|---|---|
| `bold` | `toggleMark('bold')` (alias `formatBold`) | Chromium toggles `<b>`/`font-weight`; ours = flat `bold` mark |
| `italic` | `toggleMark('italic')` (`formatItalic`) | |
| `underline` | `toggleMark('underline')` (`formatUnderline`) | |
| `strikeThrough` | `toggleMark('strikethrough')` | name mismatch: Chromium `strikeThrough` vs our `strikethrough` |
| `subscript` | `toggleMark('subscript')` | |
| `superscript` | `toggleMark('superscript')` | |
| `foreColor` | `toggleMark('color', v)` | consider renaming our `color` → `foreColor`? (recommend keep `color`) |
| `backColor` / `hiliteColor` | `toggleMark('background', v)` | Chromium has two; we unify to `background` |
| `fontName` | `toggleMark('fontFamily', v)` | name mismatch |
| `fontSize` | `toggleMark('fontSize', v)` | Chromium uses 1–7 legacy sizes; ours uses CSS lengths |
| `createLink` | `toggleMark('link', href)` | |
| `unlink` | `toggleMark('link')` to remove | Chromium has a dedicated remove; ours toggles |
| `removeFormat` | **(none)** | clear all marks over a range — NOT implemented (proposed `cmdClearMarks`) |
| `increaseFontSize` / `decreaseFontSize` | **(none)** | relative size — not implemented |

### 2.2 Block structure

| Chromium `execCommand` | Our command | Note |
|---|---|---|
| `insertParagraph` | `insertParagraph` | ✓ same name |
| `insertLineBreak` | `insertLineBreak` | ✓ same name |
| `formatBlock` (e.g. `<h1>`) | `setBlockType('h1')` | name mismatch: `formatBlock` vs `setBlockType` |
| `heading` | `setBlockType('hN')` | |
| `insertOrderedList` | `wrapInList('ol')` | name mismatch |
| `insertUnorderedList` | `wrapInList('ul')` | name mismatch |
| `indent` | `indentListItem` (lists only) | Chromium also indents via blockquote — partial |
| `outdent` | `outdentListItem` | partial (lists only) |
| `insertHorizontalRule` | **(none)** | proposed `cmdInsertHR` |
| `justifyLeft/Center/Right/Full` | **(none)** | text-align — not implemented (proposed `cmdSetAlign`) |
| `defaultParagraphSeparator` | **(N/A)** | config, not an edit |

### 2.3 Text & deletion

| Chromium `execCommand` | Our command | Note |
|---|---|---|
| `insertText` | `insertText(txt)` | ✓ same name |
| `insertHTML` | **(none)** | insert arbitrary HTML fragment — NOT implemented (proposed `cmdInsertHTML` / paste) |
| `delete` | `deleteContentBackward` | name mismatch |
| `forwardDelete` | `deleteContentForward` | name mismatch |
| `insertNewlineInQuotedContent` | **(none)** | blockquote-specific |
| `deleteToEndOfParagraph` etc. | **(none)** | granular deletes — not implemented |

### 2.4 Media / nodes

| Chromium `execCommand` | Our command | Note |
|---|---|---|
| `insertImage` | `insertImage(src, alt)` | ✓ |
| (image resize — N/A in execCommand) | `cmdResizeImage` | ours adds resize |
| (no table execCommand) | `insertTable` / `addTableRow` / … | ours adds table ops |

### 2.5 History & selection

| Chromium `execCommand` | Our command | Note |
|---|---|---|
| `undo` | history undo | |
| `redo` | history redo | |
| `selectAll` | `selectAll` | ✓ |
| `copy` / `cut` / `paste` | **(none)** | clipboard — see §4.2 |
| `styleWithCSS` | **(N/A)** | we always emit CSS marks |
| `contentReadOnly` | **(N/A)** | |

### 2.6 Naming-revision shortlist (for later)

Mismatches worth reconciling if we want closer Chromium/W3C alignment:

| Ours | Chromium / W3C name | Recommendation |
|---|---|---|
| `deleteContentBackward` | `delete` (execCommand) / `deleteContentBackward` (InputEvent) | keep — matches **InputEvent** `inputType`, the modern spec |
| `deleteContentForward` | `forwardDelete` / `deleteContentForward` | keep — matches InputEvent |
| `setBlockType` | `formatBlock` | consider — but `setBlockType` is clearer |
| `wrapInList('ul'/'ol')` | `insertUnorderedList` / `insertOrderedList` | keep `wrapInList` (one command, param'd) |
| `toggleMark('strikethrough')` | `strikeThrough` | align casing if we expose a `formatStrikethrough` alias |
| `toggleMark('background')` | `backColor` / `hiliteColor` | keep `background` (CSS-aligned) |
| `toggleMark('fontFamily')` | `fontName` | keep `fontFamily` (CSS-aligned) |

**Observation:** our command names already track the **modern InputEvent `inputType` / W3C** vocabulary (`insertText`, `insertParagraph`, `deleteContentBackward`, `formatBold`) rather than the legacy `execCommand` names — which is the right call. The mark commands track CSS property names (`color`, `background`, `fontFamily`, `fontSize`), also a deliberate, defensible choice. The main gaps are missing commands (`removeFormat`, `insertHTML`/paste, `insertHorizontalRule`, justify/align), not naming.

---

## 3. Scenario-harvest approach (chosen)

1. Parse each Chromium test file; extract every `assert_selection(input, testerBody, expected, name)` call.
2. Parse the tester body into an operation list (execCommand calls, `for`-loop unrolling, `selection.modify` calls).
3. **Filter** to convertible tests (see §3.1).
4. Convert: input markers → `<cursor>/<anchor>/<focus>`; `<div contenteditable>` → `<doc>` (wrap bare inline in `<p>`); operations → our `events.json`.
5. **Run** through our editor; record OUR output as `output.html`.
6. Write the triple to `test/tier_f_chromium/<category>/<name>/`.
7. The existing lenient runner picks them up and additionally enforces the **invert-roundtrip** invariant — so even though expected is self-derived, the editor is validated for consistency + reversibility on real browser sequences.

### 3.1 Convertibility filter (v1)

Convert a test iff:
- It uses `assert_selection` (not `selection_test`, reftests, or pixel tests).
- Every operation is in the supported set (§2) — `insertText`, `insertParagraph`, `insertLineBreak`, `delete`, `forwardDelete`, `bold`, `italic`, `underline`, `strikeThrough`, `subscript`, `superscript`, `foreColor`, `backColor`/`hiliteColor`, `fontName`, `fontSize`, `formatBlock`, `insertOrderedList`, `insertUnorderedList`, `createLink`, `unlink`, `selectAll`, `undo`, `redo`, `indent`, `outdent`.
- The tester body is a flat sequence (optionally with simple `for (i<N)` loops we unroll). Complex JS (closures, conditionals, helper fns) → skip in v1.
- No `selection.modify` and no clipboard in v1 (unlocked in v2/v3 — see §4).

### 3.2 What "passing" means here

Harvested fixtures pass by construction (expected = our output). Their value:
- **Coverage** of real browser editing sequences not in our hand-written corpus.
- **Invert-roundtrip** validation on those sequences (a genuine assertion).
- **Regression lock** — if a future change alters our behavior on these sequences, the fixture's recorded output breaks, surfacing it for review.

A separate **divergence report** (optional, §5) compares our output to Chromium's expected at text-content level — the conformance gauge.

---

## 4. Why `selection.modify` and pasteboard are *partly* adaptable (correction)

My initial assessment said these "can't be adapted." That was too absolute. The accurate picture:

### 4.1 `selection.modify(alter, direction, granularity)`

`alter` ∈ {`move`, `extend`}; `direction` ∈ {`forward`, `backward`, `left`, `right`}; `granularity` ∈ {`character`, `word`, `sentence`, `line`, `lineboundary`, `paragraph`, `paragraphboundary`, `documentboundary`}.

| Granularity | Adaptable headless? | How |
|---|---|---|
| `character` | **Yes** | move the caret one position over the document model (prev/next text offset, crossing leaf/block boundaries) |
| `word` | **Yes** | `Intl.Segmenter('…', {granularity:'word'})` over the block's text — no layout needed |
| `sentence` | **Yes** | `Intl.Segmenter({granularity:'sentence'})` |
| `paragraph` / `paragraphboundary` | **Yes** | block boundaries — we already model paragraphs as blocks |
| `documentboundary` | **Yes** | doc start / end |
| `line` / `lineboundary` | **No (needs layout)** | a *visual* line is defined by where text wraps, which requires rendering/measurement we don't have. Could *approximate* line ≈ block for single-line content, but that's wrong for wrapped text — so we skip `line`-granularity tests rather than fake them |

So `selection.modify` is **mostly adaptable** by implementing a caret-navigation command (`cmdMoveCaret(granularity, direction, extend)`); only `line`/`lineboundary` are blocked by the absence of layout. This unlocks a large share of `inserting/`, `deleting/`, and many `selection/` tests.

**v2 plan:** implement `cmdMoveCaret` for character/word/sentence/paragraph/document; map `selection.modify` → it; skip only `line`-granularity tests.

### 4.2 Pasteboard / clipboard

The Chromium pasteboard tests **provide the clipboard data inside the test** via `setClipboardData(html, text)` before calling `execCommand('paste')` (or dispatching a paste event). So there is **no OS clipboard dependency** — the data is in the test. Paste is fundamentally:

> parse the provided HTML fragment → schema-coerce it → replace the current selection with the resulting nodes.

We have the building blocks (HTML parser, slice insertion via `replace` step). What's missing is a **paste command** (`cmdPasteHTML(html)`): parse the fragment, coerce to schema (drop disallowed tags, lift inlines into blocks, flatten marks), and splice at the selection. The Lambda side already specs this (`mod_html_paste.ls`); we haven't ported it.

So pasteboard is **adaptable in scenario-harvest mode** once a paste command exists. The fidelity of fragment *sanitization* differs from Chrome's (Chrome has elaborate paste-cleanup heuristics), but harvest mode records our behavior rather than asserting Chrome's — so divergence is acceptable, and the divergence report (§5) quantifies it.

**v3 — DONE.** Implemented as [`cmdPasteSlice(state, slice)`](../test/editor-js/src/commands/paste.ts): a clean layering where the **view/event boundary** parses HTML→slice (it needs `DOMParser`) and the **command** does the pure model surgery — a pragmatic PM-style *open paste*:
- inline slice → splice the inline leaves at the caret (adjacent same-mark leaves merged so the result round-trips);
- single block → merge its content inline (no split);
- multi-block → split the current block; first pasted block merges into the head, middle blocks insert as siblings, trailing content merges into the last pasted block.

A range within one leaf is deleted first; multi-leaf ranges and the doc-root position return null. The harvester maps both `execCommand('insertHTML', false, html)` and `setClipboardData(html)` + `execCommand('paste')` → a `paste` event (distinguishing the `setClipboardData` *helper* from `event.clipboardData` *event access* by case/dot; `copy`+`paste` and event-handler pastes are still skipped). Paste is also wired into the real demo editor (Cmd/Ctrl+V → `text/html`, falling back to `text/plain`), verified by a `FullEditor` render test. 10 `cmdPasteSlice` unit tests + 3 integration tests.

### 4.3 Spelling — deferred

`spelling/` (57 tests) needs a spellchecker (red-squiggle ranges, suggestion menus). Out of scope; deferred indefinitely.

---

## 5. Divergence report (conformance gauge) — DONE

For convertible tests, [`tools/divergence-report.ts`](../test/editor-js/tools/divergence-report.ts) compares OUR output to Chromium's **expected** at the text-content level (both stripped identically). Output: [`vibe/Chromium_Divergence_Report.md`](Chromium_Divergence_Report.md) — per-category match-rate + a categorized sample. Informational only, never a gate.

**Headline (248 comparable tests): 71% text-conformant, 0 errors** (up from 66% after the two fixes below). By category: `selection` 94%, `execCommand` 84%, `style` 71%, `pasteboard` 70%, `inserting` 64%, **`deleting` 61%** (was 41%). The report distils the content divergences into five stable classes:

1. **Cross-block backspace-merge (`joinBackward`) — IMPLEMENTED this round.** Backspace at a block start now merges into the previous block (descending into a previous list’s last item / heading, keeping the target tag). Also fixed a tree-corruption bug where a selection spanning only a block boundary was treated as a range over the doc root.
2. **Select-all + delete — IMPLEMENTED this round.** A multi-block text range now deletes, leaving a single (often empty) block.
3. Whitespace/`&nbsp;` fixup — intentional PM-class difference; now the *dominant* remaining divergence (informational, not a bug).
4. `insertHTML` of `<div>` — structural (schema doesn't model `<div>`).
5. Caret-movement landing points / `joinForward` (forward-delete at block end) — the next actionable item.

Together (1)+(2) moved `deleting` 41% → 61% and overall 66% → 71%. The shared conversion logic lives in [`tools/chromium-adapt.ts`](../test/editor-js/tools/chromium-adapt.ts), imported by both the harvester and this report so they never drift. Regenerate with `npx vite-node tools/divergence-report.ts`.

---

## 6. Results (actual — v1 + v2 implemented)

The harvester (`tools/harvest-chromium.ts`), caret navigation (`src/commands/caret.ts`, mapped from `selection.modify`) and paste (`src/commands/paste.ts`, mapped from `insertHTML` + `setClipboardData`/`paste`) were implemented this round. Run over `inserting/ deleting/ execCommand/ style/ input/ selection/ pasteboard/` (2,524 files, 614 extracted `assert_selection` calls):

| Outcome | Count | Notes |
|---|---:|---|
| **Harvested fixtures written** | **135** | under `test/tier_f_chromium/`, all green (doc + invert-roundtrip) |
| Skipped — tester | 178 | 61 helper-based bodies (no recognized op), 57 copy/undo/unsupported-exec, 22 `selection.collapse/extend` setup, 18 line/unsupported-granularity, … |
| Skipped — input | 188 | structures our schema doesn't model (`<div>` nesting, `<hr>`, bare tables) |
| Skipped — run | 113 | converted, but our editor's result isn't invert- or serialize-round-trippable (correctly excluded; a few may flag real editor gaps) |

(v1 alone — no caret nav, no paste — yields just **8**; caret nav lifted it to 103; paste/insertHTML added the final **+32**.)

**What made v2 the unlock:** with `selection.modify` rejected (v1-only), the clean `assert_selection` format yields just **8** fixtures — 97% of those tests interleave caret movement. Implementing character/word/document caret navigation lifted the yield to **103**. The `selection.modify` granularities in the corpus are dominated by `word` and `character` (no `line` in the top usage), so the no-layout subset captures the bulk.

**Parser coverage that mattered:** the corpus uses three `assert_selection` shapes — a function tester, a **string tester** (`assert_selection(input, 'insertParagraph', expected)`), and **array-join** inputs (`['<div>', …].join('')`). Supporting all three roughly doubled the extracted asserts (246 → 542).

Remaining headroom: `<div>`-structured inputs (178) are a genuine model mismatch, not parser gaps. `insertHTML`/clipboard (v3) and helper-based bodies are the next-largest untapped buckets.

---

## 7. Phased plan

- **v1 — DONE:** harvester for flat/string/array-join testers, supported-op subset → `test/tier_f_chromium/`.
- **v2 — DONE:** `cmdMoveCaret` (character / word / documentboundary; `left`/`right` alias forward/backward; `move` + `extend`) wired from `selection.modify`; `line`/`lineboundary`/`sentence`/`paragraph` skipped (need layout / not modeled). 11 focused unit tests in `test/commands/caret.test.ts`.
- **v3 — DONE:** `cmdPasteSlice` (HTML→slice parsed at the view boundary); mapped `insertHTML` + `setClipboardData`/`paste`; harvested `pasteboard/`; wired into the demo (Cmd/Ctrl+V). 10 unit + 3 integration tests.
- **Ongoing — optional:** divergence report (§5) as a conformance gauge; `removeFormat`, justify/align, and `copy`→clipboard (serialize the selected slice) to widen the supported-op set.

Re-run anytime with: `npx vite-node tools/harvest-chromium.ts <categories…>` (regenerates `test/tier_f_chromium/`).

---

## 8. Summary

The Chromium editing suite is adaptable in **scenario-harvest** mode. Our command names already track the modern InputEvent/W3C and CSS vocabularies rather than legacy `execCommand` — the gaps are missing *commands* (`removeFormat`, HR, align), not naming. `selection.modify` is mostly adaptable (only `line`-granularity needs layout) and was implemented as `cmdMoveCaret`; paste was implemented as `cmdPasteSlice` (and wired into the demo); spelling is deferred. This round harvested **135 fixtures** of real browser editing sequences, each validated by the corpus-wide invert-roundtrip invariant. Two genuine editor bugs surfaced and were fixed along the way (`cmdInsertLineBreak` phantom empty leaf; schema-aware text insertion), plus a round-trip-correct `serializeDocToHtml`. New this round: caret navigation and paste are real editor features, not just test scaffolding.
