# Radiant `contenteditable` 3 - structural Chrome editing corpus plan

**Date:** 2026-06-18
**Status:** Active implementation - CE3-0 structured artifacts and current
all-import pass set promoted to RUNNABLE; CE3-H harness parity is the next
frontier.
**Builds on:** `Radiant_Design_Content_Editable2.md`
**Scope:** Turn the imported Chromium `editing/` corpus from a mostly skipped
gauge into a staged structural conformance program for Radiant editing.

---

## 1. Measurement Baseline

The complete imported Chrome editing corpus was run with the local Chrome
editing runner in all-import mode, excluding only support/baseline/manual files
already filtered by the runner. Because several legacy pages hang, the run used
a per-file timeout.

```bash
LAMBDA_CHROME_EDITING_RUN_ALL=1 \
LAMBDA_CHROME_EDITING_TIMEOUT=5 \
LAMBDA_CHROME_EDITING_JOBS=9 \
./test/test_chrome_editing_gtest.exe --gtest_brief=1
```

**Result:** 2751 cases discovered and executed.

| Result | Count |
|---|---:|
| Passed | 113 |
| Skipped | 49 |
| Failed | 2589 |

Additional signal from the log:

| Signal | Count | Meaning |
|---|---:|---|
| `Abort trap` child exits | 189 | real engine/runtime invariant or crash surfaces |
| timeout kills | 3 | imported pages can still hang without runner isolation |
| initial default RUNNABLE gauge | 6 pass / 2745 skip | historical baseline before later RUNNABLE widening |

The all-import result is not a quality gate yet. It is a map of missing
capabilities. The failure count is dominated by harness and structural feature
gaps, not one large single bug.

### 1.1 Current CE3-0 snapshot

**Updated:** 2026-06-22

CE3-0 now emits structured artifacts for both the default RUNNABLE gauge and
the all-import local gauge. The default gauge was widened from the six-case
bootstrap allowlist to the 191-case green all-import pass-only snapshot from the
imported corpus. The linked `lambda-test` HEAD now carries a broader 641-entry
pressure `RUNNABLE` list with known failures; the table below records the last
verified pass-only snapshot.

- `temp/chrome_editing_results.jsonl`
- `temp/chrome_editing_summary.json`

The JSONL artifact records each case's relative path, bucket, flavor tags,
outcome, classifier, assertion counts, exit code, timeout/abort flags, elapsed
time, output size, and first failure line. The summary artifact records totals,
classifier counts, bucket counts, and bucket/classifier cross-counts.

Default RUNNABLE pass-only snapshot:

```bash
./test/test_chrome_editing_gtest.exe --gtest_brief=1
```

| Result | Count |
|---|---:|
| Passed | 191 |
| Skipped | 2560 |
| Failed | 0 |

Default RUNNABLE bucket coverage:

| Bucket | Passed |
|---|---:|
| `(root)` | 1 |
| caret | 4 |
| deleting | 18 |
| execCommand | 28 |
| input | 5 |
| inserting | 15 |
| pasteboard | 14 |
| selection | 93 |
| shadow | 4 |
| spelling | 2 |
| style | 2 |
| text-iterator | 2 |
| undo | 1 |
| unsupported-content | 2 |

All-import local gauge:

```bash
LAMBDA_CHROME_EDITING_RUN_ALL=1 \
LAMBDA_CHROME_EDITING_TIMEOUT=5 \
LAMBDA_CHROME_EDITING_JOBS=9 \
./test/test_chrome_editing_gtest.exe --gtest_brief=1
```

| Result | Count |
|---|---:|
| Passed | 191 |
| Skipped | 119 |
| Failed | 2441 |

Classifier summary from the all-import run:

| Classifier | Count |
|---|---:|
| assertion mismatch | 1364 |
| harness missing API | 510 |
| passed | 191 |
| unsupported internals | 176 |
| no results | 138 |
| timeout | 137 |
| process abort | 102 |
| unsupported layout/visual | 70 |
| skipped | 49 |
| unsupported shadow | 14 |

Implementation note:

- The two RUNNABLE deletion cases were failing with recursive
  `document.execCommand()` helper fallback. Native `execCommand` now honors the
  helper-fallback guard, so a JS helper can delegate back to the native command
  once instead of re-entering itself.
- Added a focused DOM JS regression for `delete` and `forwardDelete` helper
  fallback, and refreshed the justify regression to match the current
  `text-align` style mutation contract.
- The first RUNNABLE widening promoted the currently green structural smoke
  slice into `test/editing/RUNNABLE`: `assert_selection.html`, plus passing
  `deleting/`, `execCommand/`, `inserting/`, and small `selection/` cases that
  require no visual, shadow, pasteboard, or platform-only harness surface.
- The second RUNNABLE widening promoted the full current all-import pass set
  into `test/editing/RUNNABLE`: 191 cases across caret, deletion,
  `execCommand`, input, insertion, pasteboard, selection, shadow, spelling,
  style, text-iterator, undo, and unsupported-content smoke buckets.
- That pass-only gauge executed 191 cases cleanly with no failures, aborts, or
  timeouts before the later broader pressure-list branch.
- The inline-boundary `Selection.modify("extend", *, "lineboundary")` slice now
  canonicalizes equivalent anchors at inline element edges. The focused import
  family `selection/extend/extend_selection_01_*_lineboundary.html` passes all
  8 files / 216 assertions. A wider exploratory wildcard was stopped after it
  confirmed the next failures are a different block/bidi line-endpoint class in
  later `extend_selection_*_lineboundary` files.
- Opened a third RUNNABLE pressure batch in `test/editing/RUNNABLE`: 100 more
  structural cases, made up of 44 `deleting/` merge/delete cases, 1
  `editability/` style-with-CSS case, and 55 `execCommand/` format/indent/find
  cases. This brings the live pressure list to 641 non-comment entries. It is
  deliberately not a green gate; it is a triage surface for CE3-H and CE3-S.
- The first 641-entry pressure-fix pass cleared the process-abort bucket by
  fixing stale selection/focus cleanup around `document.open()` and detached
  textarea state. The focused `editability/empty-document-stylewithcss.html`,
  `inserting/insert-paragraph-empty-textarea.html`,
  `selection/inactive-selection.html`,
  `text_iterator/auto-expand-details-layout-shift.html`,
  `text_iterator/beforematch-layout-shift.html`, and
  `execCommand/forward-delete-no-scroll.html` cases now pass.
- Current pressure-list verification as of 2026-06-23:
  `env LAMBDA_CHROME_EDITING_TIMEOUT=5 LAMBDA_CHROME_EDITING_JOBS=9 ./test/test_chrome_editing_gtest.exe --gtest_brief=1`
  runs 2751 imported cases as 469 passed / 1665 skipped / 617 failed. The
  remaining failure surface is mostly `assertion_mismatch` (524), followed by
  `no_results` (41), `unsupported_internals` (24),
  `unsupported_layout_visual` (15), `harness_missing_api` (14), and
  `unsupported_shadow` (14). The run has 0 aborts and 0 timeouts.
- The second and third pressure-fix passes added a headless DOM geometry
  fallback for `offsetWidth`/`offsetHeight` from laid-out size, inline CSS
  dimensions, or text length, then tightened the CE3 harness click-hit model for
  uneditable inline children and `user-select: none` drag endpoints.
- The fourth pressure-fix pass corrected collapsed-whitespace character
  navigation symmetry in native `Selection.modify("move", *, "character")` and
  tightened the CE3 harness's element-boundary character shim so inline wrappers
  with text descendants are not treated as atomic children. The focused
  `selection/modify_move/move-by-character-005.html` case now passes 1/1.
- The fifth pressure-fix pass split native character extension from caret
  movement across collapsed whitespace runs, preserving Chrome's range-focus
  offset after the first source space while keeping caret movement canonical.
  It also made CE3 harness failure diagnostics first-line friendly, fixed
  right-edge clicks around `contenteditable=false` inline children, and changed
  synthetic mouse lookup to prefer the element whose horizontal box contains the
  pointer. The focused
  `selection/modify_extend/extend_by_character.html`,
  `selection/mixed-editability-10.html`, and
  `selection/mouse/drag_user_select_none.html` cases now pass. The current
  selection bucket is 112 passed / 1270 skipped / 3 no-results / 0 assertion
  mismatches; the remaining selection cases are no-result/reporting issues:
  `selection/longpress-selection-in-iframe-removed-crash.html`,
  `selection/mouse/drags_within_user-select_combos.html`, and
  `selection/selection-crash.html`.
- The sixth pressure-fix pass restored the legacy multiline
  `_chrome_assertion_message()` string expected by `assert_selection.html`,
  while keeping flattened first-line failure logging for the GTest runner. It
  also added collapsed forward-delete removal for the next sibling element and
  normalizes the serialized `class`/`contenteditable` pair order, clearing
  `deleting/backspace_after_contenteditable_false_elements.html` 2/2. The
  async no-result selection cases still need deeper event-loop/load work.
- The seventh pressure-fix pass filled in the CE3 `internals.settings`
  password-echo setters used by
  `input/selectall-in-input-with-text-security.html`, clearing the final
  `harness_missing_api` classifier. It also treats `ul`/`ol` as block
  containers in the text-dump walker, so retained list-item text inside a list
  is visible to dump-baseline comparisons; `deleting/4866671.html` now passes.
- The eighth pressure-fix pass added CE3 structural `createLink` handling for
  collapsed carets, same-text selections, inline image ranges, and selected
  descendants inside lists/tables. It also splits existing anchors when
  relinking or unlinking a selected text run, preserving `href`, `id`, and
  `name` fragments according to Chrome's command behavior. The focused
  `execCommand/createLink.html` and `execCommand/apply-style-id-name.html`
  cases now pass.
- The ninth pressure-fix pass moved deletion handling from isolated fixtures
  to shared block-boundary normalization. Selected line deletes now merge or
  preserve empty-line `<br>` placeholders through a common range-delete path,
  and collapsed/boundary deletes merge paragraphs into headings, `pre`, list
  items, or Mac table cells while unwrapping after tables for non-Mac editing
  behavior. The pass also corrected single-dump `Markup.dump()` formatting,
  normalizes legacy `font face` fragments to Chrome-style spans during merges,
  removes leading inline `<br>` nodes before typed text, and begins run-based
  whitespace collapse after deletes. The focused `deleting/delete-line-*`
  cluster, `deleting/5890684.html`,
  `deleting/delete-line-break-before-underlined-content.html`,
  `deleting/delete-line-break-between-paragraphs-with-same-style.html`,
  `execCommand/delete-line-and-insert-text-in-font-inside-blockquote.html`,
  `deleting/backspace-merge-into-block.html`,
  `deleting/backspace-merge-into-list-item.html`,
  `deleting/backspace-merge-two-paragraphs.html`, `deleting/5032066.html`, and
  `deleting/delete-leading-ws-001.html` now pass.
- The tenth pressure-fix pass added structural list outdent handling for nested
  and malformed sibling-list shapes. `execCommand("Outdent")` now promotes a
  selected nested `<li>` after its parent item, preserves the promoted item's
  contents, moves following siblings into the promoted item's child list, and
  prunes empty wrapper lists/list items. The focused
  `execCommand/4916583.html`, `execCommand/4928635.html`,
  `execCommand/5575101-1.html`, `execCommand/5575101-2.html`, and
  `execCommand/5575101-3.html` cases now pass.
- The eleventh pressure-fix pass added a shared CE3 justify/alignment command
  path. `justifyCenter`/`justifyRight` now wrap selected line fragments in
  `div style="text-align: ...;"`, reuse preceding inline style wrappers for
  selected replaced controls, and realign an already aligned paragraph without
  losing its block wrapper. The focused `execCommand/align-in-span.html`,
  `execCommand/apply-style-empty-paragraph-start-crash.html`, and
  `execCommand/5062376.html` cases now pass. The empty editable
  `execCommand/25256.html` filler-`br` case remains a separate root-cause item.
- The twelfth pressure-fix pass added shared CE3 command handling for
  structural `indent`, list creation around replaced blocks, and
  `InsertNewlineInQuotedContent`. Indenting now wraps the selected block in the
  Chrome-compatible blockquote style or nests an empty first list item into a
  same-kind child list; list creation now wraps a selected `<hr>` in a new
  `ul`/`ol` list item; quoted-content newlines now split an ordered list inside
  a blockquote and preserve the second list's `start` attribute. The focused
  `execCommand/4580583-1.html`, `execCommand/create-list-with-hr.html`,
  `execCommand/5138441.html`, and
  `execCommand/crash-indenting-list-item.html` cases now pass.
- The thirteenth pressure pass expanded the live `RUNNABLE` surface from 641
  to 1102 non-comment entries. The new batch opens 461 additional high-value
  command/input/insertion/style/undo fixtures, including the remaining
  `execCommand` insert-list, insert-paragraph, outdent, query, remove-format,
  toggle-style, input, `inserting/`, `style/`, and undo crash-redo clusters.
  The runner now also prefers the explicit non-skipped `RUNNABLE`
  entry when deduplicating against the directory scan, which unlocks the
  previously listed deletion/format pressure entries that were still being
  reported as skipped. The verified run reduced skipped cases by 552.
- The fourteenth pressure-fix pass moved structural `execCommand` families to
  helper-first dispatch with native fallback, and fixed the DOM
  `contentEditable` setter so boolean assignments such as
  `element.contentEditable = true` install the `contenteditable` attribute
  instead of leaving the element at `inherit`. The CE3 alignment helper now
  styles existing single-line blocks directly, wraps loose visual lines,
  treats `blockquote` as a block boundary, preserves serialized inline style
  attributes, restores selection to the transformed line end, and handles
  `<br>` placeholders at line starts. The focused
  `style/create-block-for-style-*`, `style/block-style-001.html`,
  `style/block-style-003.html`, `execCommand/justify.html`,
  `execCommand/justify_block_starts_with_image.html`, and
  `execCommand/apply_style/justify_right_ul_br_crash.html` cases now pass.
- The fifteenth pressure-fix pass added a shared CE3 `insertParagraph` DOM
  fallback for headless contenteditable hosts. It splits simple blocks into
  sibling paragraphs, preserves cloned inline wrappers such as `<b>` across the
  split, keeps line-edge spaces non-breaking, consumes block placeholder
  `<br>` nodes when typing into a newly split paragraph, leaves inline
  placeholders intact, and handles table edit hosts and empty paragraph
  insertion around direct host text. The representative paragraph batch moved
  to 17/24 passing, including `execCommand/5569741.html`,
  `execCommand/insert-paragraph-in-inline-list-item.html`,
  `execCommand/insert_paragraph/inside-div.html`,
  `inserting/editable-inline-element.html`, `inserting/insert-div-009.html`,
  `inserting/insert-div-010.html`, `inserting/insert-div-018.html`,
  `inserting/insert-div-019.html`, `inserting/insert-div-020.html`,
  `inserting/insert-div-022.html`, `inserting/insert-div-025.html`,
  `inserting/insert_div_with_attr.html`,
  `inserting/insert_div_with_style.html`,
  `inserting/insert_paragraph_and_style.html`,
  `inserting/insert-paragraph-copy-style.html`,
  `inserting/insertparagraph-seperator-on-non-selectable-node.html`, and
  `style/block-styles-007.html`.
- The sixteenth pressure-fix pass added shared CE3 replacement and line-break
  insertion behavior. `insertHTML` and `insertText` now delete selected ranges
  before inserting, preserve selected inline wrappers when Chrome keeps them,
  serialize anchor `href` before `id`, and use a marker-based
  `insertAdjacentHTML` path so real fragments such as tables, images, and
  inline formatting survive instead of being stripped to text. `insertLineBreak`
  now inserts `<br>` nodes with Chrome-style trailing placeholders, splits
  `white-space: pre` inline spans without placing `<br>` inside tab spans, and
  normalizes spaces after breaks. `InsertNewlineInQuotedContent` now splits
  blockquotes with `Range.extractContents()`, prunes empty extracted wrappers,
  and keeps nested quoted content in the correct quote level. A follow-up
  insertHTML normalization pass routes plaintext-only hosts to text insertion,
  unwraps span-wrapped block fragments, flattens single no-attribute div
  wrappers in text runs, removes stale trailing placeholder breaks after block
  insertion, and normalizes NBSP before inserted HTML. The focused green batch
  now includes `execCommand/insertHTML.html`, `inserting/4875189-1.html`,
  `inserting/4875189-2.html`, `inserting/4959067.html`,
  `inserting/insert_br.html`, `inserting/insert_br_at_end_of_block.html`,
  `inserting/insert_br_at_tabspan.html`,
  `inserting/insert_line_break_at_end_of_anonymous_block.html`,
  `inserting/insert_line_break_with_tab_span.html`,
  `inserting/insert-br-quoted-001.html`, `inserting/5418891.html`,
  `inserting/5510537.html`, `inserting/insert_div_text_into_text.html`,
  `inserting/insert_html_at_end_of_paragraph.html`,
  `inserting/insert_input_element.html`,
  `inserting/prevent-block-nesting-01.html`, and
  `inserting/insert_div_before_br_at_end.html`.
- The seventeenth pressure-fix pass structurally widened CE3 list-command
  behavior instead of fixture-patching isolated cases. Shared
  `insertOrderedList`/`insertUnorderedList` handling now switches selected list
  ranges while preserving caret/range endpoints, unlistifies same-type list
  commands, moves malformed loose list text out of matching lists, wraps fully
  selected inline anchors as list items, splits selected `<br>`-separated
  inline runs into multiple list items, expands partial line selections to
  paragraph boundaries while restoring original marker offsets, normalizes
  select-all nested list conversion with adjacent-list/orphan-`li` merging, and
  lifts child list shells for `Outdent`. Empty-host paragraph insertion now
  preserves the first empty paragraph before the caret paragraph. The list
  family gauge moved to 27/36 fixture-level passing, with newly green coverage
  including `execCommand/insert-lists-inside-another-list.html` 12/12,
  `execCommand/insert-list-items-inside-another-list.html` 10/10,
  `execCommand/remove-list-items.html` 7/7,
  `execCommand/switch-list-type.html` 5/5,
  `execCommand/insert_list_with_underline.html` 2/2,
  `execCommand/insert-list-with-id.html`,
  `execCommand/insert-list-with-noneditable-content.html`,
  `execCommand/insert-list-with-progress-crash.html`,
  `execCommand/switch-list-type-with-orphaned-li.html`, and
  `execCommand/remove-list-item-1.html`. The full pressure verification now
  runs 2751 imported cases as 495 passed / 1665 skipped / 591 failed, reducing
  the failure surface by 26 from the previous 469 / 1665 / 617 run.

---

## 2. What Failed

### 2.1 Harness and helper compatibility

Representative failure: `editing/assert_selection.html` passes only 1/31
assertions.

Observed failures:

- marker parsing rejects or misrepresents text, element, table, and backward
  ranges;
- document-element serialization fails when `document.body` is null;
- textarea/input marker and value serialization are incomplete;
- multiple marker validation messages do not match Chromium's helper;
- `selection.setClipboardData`, `internals`, `computeLeft()`, `computeTop()`,
  flat-tree dumping, and promise-style helper behavior are missing or partial.

This means a large number of downstream failures cannot yet be interpreted as
editing-engine bugs. The imported corpus depends on Blink's `assert_selection`,
`editing.js`, `js-test.js`, `dump-as-markup.js`, `eventSender`, `testRunner`,
and `internals` surface.

### 2.2 Old layout-test harness pages

Representative failure: `caret/caret-color.html` exits with:

```text
Uncaught ReferenceError: runEditingTest is not defined
```

That page imports `../editing.js` and calls `runEditingTest()`. The runner
inlines relative scripts, but the current harness does not yet provide the
complete execution model those old layout tests expect:

- `editing.js` command queues and `runEditingTest()` / `runDumpAsTextEditingTest()`;
- dump modes such as `dumpAsLayoutWithPixelResults`;
- editing callback output;
- layout/caret visual dump behavior.

Many `caret/`, old `deleting/`, old `execCommand/`, and pasteboard script-test
pages fail before they reach meaningful editing assertions.

### 2.3 Selection, range, and serializer fidelity

Representative failure:
`selection/extend/extend_selection_01_ltr_backward_lineboundary.html` fails
27/27 with `Cannot set property on null or undefined`.

The underlying class is broader than that specific error:

- `assert_selection` needs a faithful sample model independent of `document.body`;
- `setBaseAndExtent` must support element boundaries, table boundaries, and
  backward selections without losing direction;
- text controls need sample markers and serialization through `value` and
  `selectionStart` / `selectionEnd`, not only DOM children;
- `Selection.modify()` needs line, lineboundary, word, visual left/right, bidi,
  and mixed-direction behavior that matches Chromium's test expectations;
- sample serialization must preserve `contenteditable="false"`, empty
  attributes, void elements, textarea values, input values, and shadow/flat-tree
  options.

This bucket is structurally important because almost every other bucket uses
selection setup and serialization to verify edits.

### 2.4 Structural edit-command gaps

Representative failure:
`deleting/delete-block-merge-contents-010.html` passes 1/3 and fails with
`execCommand failed: delete`.

The existing CE2 deletion work covers many WPT-shaped paths, but Chrome editing
exercises a broader legacy edit-command model:

- arbitrary block merges, including style-preserving block joins;
- list item merge/split/unwrapping across nested lists;
- table cell and row deletion or merge beyond the current safe subset;
- delete/forwardDelete at render-boundaries, replaced elements, hidden nodes,
  unrendered nodes, controls, and mixed editability;
- insert paragraph/line break/html/text in many normalized DOM contexts;
- selection deletion across editable roots and contenteditable=false islands.

The proposal should treat this as a general edit-plan problem, not a growing
pile of one-off delete cases.

### 2.5 Formatting and style command gaps

Representative failure:
`style/toggle-style-bold-italic-mixed-editability.html` passes 0/4 and then
hits a DocState invariant. The observed DOM wraps a
`contenteditable=false` island inside newly created formatting wrappers:

```html
<b>^abc <span contenteditable>def</span> ghi|</b>
```

The expected output splits formatting around the non-editable island:

```html
<b>^abc </b><span contenteditable="false">def</span><b> ghi|</b>
```

Structural gaps:

- format commands must split ranges at editing-boundary and
  `contenteditable=false` islands;
- inline style toggles must remove equivalent style instead of blindly nesting;
- style wrappers need normalization, merging, and cleanup;
- queryCommand state/value needs to reflect mixed selections and typing state;
- serializer must preserve `contenteditable="false"` accurately.

### 2.6 Clipboard, pasteboard, and text input events

Representative failure:
`pasteboard/paste-text-events.html` passes 6/14, misses paste mutations and
`textInput` events, then aborts with a DocState invariant.

Observed gaps:

- paste into textarea, input, and rich editing hosts does not apply the expected
  plain/rich payloads in all paths;
- `textInput` event compatibility is missing for legacy editing tests;
- `beforeinput` / `input` / paste event ordering is incomplete for Chrome
  layout-test expectations;
- DataTransfer and clipboardData semantics are partial;
- rich paste normalization, URL resolution, file/drop surfaces, and smart paste
  behavior are not modeled.

### 2.7 Undo and platform editing behavior

Representative failure: `undo/undo-delete.html` passes 3/12. The expected undo
selection differs by editing behavior (`android`, `mac`, `unix`, `win`).

Observed gaps:

- `internals.settings.setEditingBehavior(...)` is not modeled;
- delete operations are snapshotted as raw DOM mutations but not grouped like
  legacy edit commands;
- undo selection restore differs for mac and non-mac behavior;
- command coalescing, typing coalescing, paste/delete transactions, and
  cross-frame undo cases need a richer history model.

### 2.8 Synthetic input, geometry, and pointer behavior

Several selection, caret, pasteboard, and unsupported-content tests require
realistic `eventSender` behavior:

- mouse drag selection with layout coordinates;
- hit-testing into text, tables, shadow trees, and controls;
- double/triple click selection granularity;
- context click and menu selection;
- keyboard shortcuts with platform modifiers;
- caret rectangle, line navigation, and visual movement.

The current harness has safe placeholders for many methods. That is enough to
avoid immediate ReferenceErrors, but not enough for the corpus to pass.

### 2.9 Stability and invariant failures

The all-import run recorded 189 `Abort trap` child exits. Sample buckets include
pasteboard, formatting, text-iterator, undo, and some execCommand pages.

Every abort should be treated as a root-cause engine bug. However, the runner
also needs to keep them isolated and report them cleanly so the corpus can be
used as a measurement tool while the engine improves.

---

## 3. Structural Design Direction

CE3 should add four structural layers over the CE2 foundation.

### 3.1 A first-class Chrome editing harness layer

The harness should become a compatibility package rather than a small shim:

- import or faithfully port `assert_selection.js` semantics;
- implement `editing.js` command helpers and command queues;
- implement the old `js-test.js` assertion surface used by script-tests;
- implement dump-as-text and dump-as-markup comparison with normalized but
  deterministic serialization;
- model enough `testRunner`, `eventSender`, and `internals` to distinguish
  unsupported platform/layout cases from real editing failures;
- classify each test by flavor before execution:
  `assert_selection`, `editing.js`, `js-test`, dump baseline, visual/layout,
  pointer/drag, clipboard, internals, shadow, unsupported.

Exit signal:

- `editing/assert_selection.html` should pass its non-shadow, non-internals,
  non-layout assertions;
- old `editing.js` pages should no longer fail with missing
  `runEditingTest()`;
- failures should be semantic assertion failures, not helper ReferenceErrors.

### 3.2 A sample-tree selection model

Chrome's helper does not simply set `document.body.innerHTML`. It treats the
HTML fragment as a sample with selection markers, optional roots, text controls,
shadow/flat-tree modes, and carefully formatted output.

Radiant should add a test-harness sample model with:

- parsed marker validation with Chromium-compatible error messages;
- support for caret, forward range, backward range, element-boundary range, and
  no-selection samples;
- text control samples where selection lives in `value`, not child nodes;
- serialization of body, documentElement, chosen dump root, and flat-tree modes;
- stable attribute serialization for booleans/enumerated attributes;
- helpers for geometry-based selection tests (`computeLeft`, `computeTop`) that
  can use Radiant layout boxes where available.

This is harness work, but it directly protects engine work: without faithful
setup/serialization, engine diffs are noisy and misleading.

### 3.3 A general edit-operation planner

The core engine should stop growing per-shape special cases and expose a common
editing planner:

1. Resolve the active editing host and effective editing behavior.
2. Normalize the selection into an editing-position/range graph.
3. Split the range at editing boundaries, non-editable islands, table/list
   boundaries, and atomic content.
4. Produce an `EditPlan` with target ranges, DOM operations, selection result,
   history grouping, and event payloads.
5. Run the existing cancelable `beforeinput -> mutate -> input` envelope.
6. Normalize the DOM and selection after mutation.

Commands that should share this planner:

- Backspace/Delete and word/line variants;
- `insertText`, `insertHTML`, `insertParagraph`, `insertLineBreak`;
- `bold`, `italic`, `underline`, `strikethrough`, color/font commands;
- `formatBlock`, indent/outdent, ordered/unordered list;
- `createLink`, `unlink`, `insertImage`, `insertHorizontalRule`;
- cut/copy/paste and undo/redo transactions.

The planner should make target-range computation and mutation use the same
structural boundaries. This avoids the common failure where `beforeinput`
reports one thing and mutation does another.

### 3.4 Deterministic normalization passes

Chrome editing tests assert exact DOM. Radiant needs deterministic post-edit
normalization:

- merge adjacent equivalent inline wrappers;
- unwrap redundant wrappers;
- split wrappers around `contenteditable=false`;
- preserve or strip styles according to command semantics;
- normalize block wrappers after insert/delete;
- normalize list structure after indent/outdent/list insertion;
- normalize table cell content while guarding spans;
- preserve selection endpoints through all normalization steps.

Normalization must be designed as data-structure logic, not string rewrite.

---

## 4. Proposed Phases

### CE3-0 - measurement and triage infrastructure

**Goal:** make the corpus measurable every day.

Work:

- Keep default RUNNABLE mode as the CI-safe gauge.
- Keep all-import mode behind `LAMBDA_CHROME_EDITING_RUN_ALL=1`.
- Keep per-file timeout support behind `LAMBDA_CHROME_EDITING_TIMEOUT`.
- Write a structured result artifact under `temp/` for local analysis:
  rel path, top-level bucket, flavor tags, pass/fail/skip, exit code, timeout,
  abort, first failure line.
- Add a failure classifier that separates:
  harness missing API, assertion mismatch, process abort, timeout, unsupported
  layout/visual, unsupported shadow, unsupported internals.

Exit:

- all-import run completes without manual interruption;
- final result table can be regenerated from a single command;
- no failures are hidden by corrupt interleaved output.

Implementation status:

- **Landed 2026-06-22.** The runner writes `temp/chrome_editing_results.jsonl`
  and `temp/chrome_editing_summary.json`, classifies every case, and preserves
  the CI-safe default RUNNABLE mode.
- **Updated 2026-06-22.** The default RUNNABLE allowlist was widened to 191
  passing cases while keeping 0 failures in allowlist mode.
- The all-import command completes under the documented timeout/jobs settings.
  The command still exits nonzero because all-import is a red local gauge, not
  a quality gate.

### CE3-H - harness parity first

**Goal:** reduce false negatives before engine work.

Work:

- complete the portable subset of `assert_selection.js`;
- port `editing.js` helpers used by non-visual tests;
- port the older `js-test.js` assertion functions used by script-tests;
- implement dump-as-text and dump-as-markup baseline comparison with a stable
  serializer;
- implement `internals.settings.setEditingBehavior()` for the behavior values
  the corpus uses, even if it only affects editing command policy at first;
- provide explicit skip reasons for visual-only caret/layout/pixel cases.

Target buckets:

- `assert_selection.html`;
- selection/extend helper failures currently throwing setup errors;
- caret and old editing pages currently failing with missing `runEditingTest`;
- js-test pasteboard and execCommand script-test pages currently failing with
  missing helper behavior.

Exit:

- harness ReferenceErrors become rare;
- most failures become assertion mismatches or documented unsupported features.

### CE3-S - selection and navigation model

**Goal:** make selection setup, movement, and serialization reliable.

Work:

- support backward selections and direction preservation everywhere;
- support element/table boundary selection in `setBaseAndExtent`;
- implement lineboundary and line movement using layout line boxes;
- extend word/character movement for bidi and visual left/right;
- support geometry helpers used by selection tests;
- handle text-control selections in the sample model;
- stabilize range updates after DOM mutation in all direct range and selection
  APIs.

Target buckets:

- `selection/extend`;
- `selection/modify_move`;
- `selection/modify_extend`;
- text control selectionchange tests;
- the non-shadow portion of `assert_selection.html`.

Exit:

- selection failures are no longer dominated by sample setup errors;
- lineboundary and bidi movement have focused JS unit regressions before
  enabling broad Chrome selection cases.

### CE3-D - structural deletion and insertion

**Goal:** broaden CE2's WPT-shaped deletion support into Chrome's legacy
edit-command matrix.

Work:

- implement the general edit-operation planner for delete and insert commands;
- handle block merges with style preservation;
- handle list item merge/split/unwrap across nested and mixed list structures;
- handle table cell content deletion and guarded row/cell operations;
- handle replaced/atomic content and hidden/unrendered nodes;
- handle non-editable islands and editing-root boundaries;
- implement paragraph insertion and line-break insertion normalization;
- connect all paths to shared target ranges and history grouping.

Target buckets:

- `deleting/`;
- `inserting/`;
- unsupported-content list/table delete and typing cases that are not truly
  unsupported for Radiant.

Exit:

- promoted delete/insert cases cover direct, nested, list, table, atomic, and
  mixed-editability shapes;
- no process aborts in the promoted delete/insert set.

### CE3-F - formatting, style, and queryCommand

**Goal:** make inline/block formatting commands exact enough for Chrome corpus
assertions.

Work:

- split formatting ranges at non-editable islands;
- toggle existing equivalent style instead of nesting wrappers;
- preserve CSS style spans where Chrome expects `style`, not semantic tags;
- normalize adjacent equivalent wrappers;
- finish `queryCommandState`, `queryCommandValue`, `queryCommandEnabled`,
  `queryCommandIndeterm`, and mixed-selection behavior;
- extend block formatting, indent/outdent, and list formatting on top of the
  edit planner.

Target buckets:

- `style/`;
- `execCommand/apply_style`;
- `execCommand/format_block`;
- `execCommand/indent`, `outdent`, `insert_list`;
- mixed editability style tests.

Exit:

- style commands preserve non-editable islands;
- repeated toggles converge instead of nesting indefinitely.

### CE3-C - clipboard, pasteboard, and textInput

**Goal:** make clipboard-driven editing behave like a real browser editing
surface.

Work:

- model `ClipboardEvent.clipboardData` and `DataTransfer` for copy/cut/paste;
- support `selection.setClipboardData` helper compatibility;
- implement `textInput` compatibility events where the Chrome corpus expects
  them;
- implement paste into textarea/input/rich host through the same edit planner;
- normalize rich paste fragments and URL resolution;
- handle drag/drop pasteboard tests separately from simple clipboard tests.

Target buckets:

- `pasteboard/`;
- input/paste WPT cases currently skipped;
- copy/cut/paste portions of execCommand.

Exit:

- paste into text controls and rich hosts mutates values/HTML and dispatches
  the expected event sequence;
- no pasteboard cases abort DocState invariants in the promoted set.

### CE3-U - undo/history and platform editing behavior

**Goal:** move from snapshot replay to command-aware history where Chrome tests
need it.

Work:

- implement `internals.settings.setEditingBehavior(android|mac|unix|win)` as an
  editing policy input;
- group repeated typing/delete commands according to platform behavior;
- record selection-before, mutation payload, and selection-after as history
  metadata, not only host HTML snapshots;
- restore mac and non-mac undo selections according to behavior;
- handle iframe/document-removal undo cases with state invalidation guards;
- integrate clipboard and formatting transactions into history.

Target buckets:

- `undo/`;
- keyboard shortcut undo/redo;
- editability and pasteboard history interactions.

Exit:

- `undo/undo-delete.html` passes all platform-behavior variants;
- undo/redo promoted cases do not abort when frames or hosts are removed.

### CE3-G - geometry, caret, and visual/layout cases

**Goal:** separate structural editing conformance from visual caret/layout
coverage.

Work:

- classify visual-only tests and keep them out of structural pass-rate gates;
- implement geometry APIs needed by structural selection tests:
  caret rectangles, line boxes, hit testing, `computeLeft`, `computeTop`;
- improve `eventSender` pointer drag/click using Radiant layout hit testing;
- add dump-as-layout support only after structural editing stabilizes.

Target buckets:

- `caret/`;
- geometry-heavy `selection/mouse`;
- text-iterator find/hit-testing pages;
- visual dump tests.

Exit:

- structural caret/selection tests can run headlessly;
- visual-only tests are tracked separately and not mixed into edit-model pass
  rates.

### CE3-I - invariant and crash hardening

**Goal:** make all-import runs robust even while many tests fail.

Work:

- every DocState invariant abort from the corpus gets a focused repro and a
  root-cause note;
- runner records aborting rel path, exit code, and first invariant line;
- mutation paths repair or clear stale active surfaces after DOM replacement,
  frame removal, host removal, and type/value changes;
- selection shadow/caret projection repair covers direct Range, Selection,
  form control, and mutation-observer paths;
- add focused JS regressions before promoting any crash-fix bucket.

Exit:

- all-import run reports failures without killing child processes in the
  promoted buckets;
- crash count is tracked as a first-class metric independent of assertion
  failures.

---

## 5. Promotion Strategy

The corpus should not move from 6 runnable cases to thousands at once. Promote
by capability and by failure flavor.

### 5.1 Suggested promotion order

1. Harness self-tests:
   `assert_selection.html`, helper-only `js-test.js` pages, small `editing.js`
   pages.
2. Non-visual selection setup:
   simple `selection/extend`, `selection/collapse`, `selection/set_base...`.
3. Deleting and inserting:
   start with direct rich-host cases, then nested/list/table/mixed editability.
4. Formatting:
   inline style, mixed editability, queryCommand, block/list commands.
5. Clipboard:
   simple text paste/copy/cut before drag/drop and files.
6. Undo:
   same-host command undo before iframe/document removal and platform variants.
7. Geometry/caret:
   structural geometry helpers before visual/pixel-style expectations.

### 5.2 Metrics to track in CE3

Each all-import run should record:

| Metric | Why |
|---|---|
| total / pass / fail / skip | high-level corpus movement |
| crash count | engine stability independent of assertions |
| timeout count | runner isolation and hangs |
| harness-missing count | false negatives to eliminate early |
| assertion-mismatch count | real semantic engine gaps |
| pass rate by top-level bucket | directs phase priorities |
| newly promoted RUNNABLE cases | CI-safe progress |

### 5.3 Acceptance gates

Default CI gate:

- `./test/test_chrome_editing_gtest.exe --gtest_brief=1`
- only RUNNABLE allowlist cases execute;
- no unexpected failures.

Local all-import gauge:

- `LAMBDA_CHROME_EDITING_RUN_ALL=1`
- timeout required;
- failure expected;
- used for trend tracking and phase planning.

Phase gate:

- add focused Lambda JS regressions for every newly fixed structural behavior;
- promote Chrome cases only after the underlying behavior has focused local
  coverage;
- keep visual-only and unsupported platform cases out of structural gates until
  their infrastructure lands.

---

## 6. Immediate Next Work

1. **CE3-H:** refresh the all-import local gauge, then drive the next harness
   work from `temp/chrome_editing_summary.json`, starting with the 510
   `harness_missing_api` cases and the 138 `no_results` cases from the
   2026-06-22 all-import snapshot.
2. **CE3-H:** continue the portable `assert_selection` subset from the
   remaining block/bidi `lineboundary` endpoint failures. The inline-boundary
   marker-canonicalization slice is green; the next slice is real line endpoint
   movement across block children and newline text.
3. **CE3-H:** port enough `editing.js` and `js-test.js` to stop simple old
   layout-test pages from failing with missing helper functions.
4. **CE3-S:** fix sample selection for documentElement/body-null, text controls,
   element/table boundaries, and backward selections.
5. **CE3-I:** pick the top recurring DocState invariant from the 102 aborts and
   reduce it to a focused JS regression before promoting additional pressure
   cases into the pass-only green gauge.

---

## 7. Summary

The Chrome editing corpus is now fully imported and measurable, but the first
all-import run shows that Radiant needs structural work in layers, not a long
tail of isolated test patches. The most valuable order is:

1. make the harness faithful enough to trust failures;
2. make selection/sample serialization exact;
3. route all edit commands through a shared planner;
4. add deterministic DOM normalization;
5. deepen clipboard/history/geometry;
6. harden invariants until all-import runs are stable.

That sequence turns the current 191-case pass-only gauge and the broader
641-entry pressure surface into a staged path
toward a large, meaningful Chrome editing pass rate while preserving the CE2
contract: every edit still flows through the modern cancelable
`beforeinput -> mutation -> input` pipeline.
