# Textarea caret visibility and clipping

## Milestone

SmartArt's native multiline field follows the caret when typing or navigating
between explicit lines. Text, selection, composition decoration and the caret
stay inside the field's content viewport. Verify ordinary and doubled display
scale, new blank lines, navigation back to the beginning, and an offscreen caret
during pointer selection. Keep this generic native-control geometry work below
the application editing policy boundary in **D7.2.5**; retain recordings only
while their content remains valid, consistent with **D4.5.3v2**.

## Cause and repair

`render_textarea` consumed a vertical scroll offset but only pointer-drag
autoscroll updated it. Ordinary value and selection changes did not reveal the
caret. The control also painted without a content clip. Its bitmap glyphs use
projected rectangular bounds, while the caret and decoration use vector clips;
both paths need the field's viewport.

Value changes and the canonical text-control selection writer now request a
single caret reveal. The textarea painter uses the shared caret geometry and a
scroll adjustment shared with single-line inputs. Active pointer gestures keep
their own scrolling behavior. A repaint alone does not continually recenter the
field. The painter saves and restores the inherited bounds and adds the native
content clip for all of its contents.

The pixel regression also exposed selection changes reusing a stale document
recording. The canonical selection writer now calls the existing form-state
dirty helper after publishing the complete selection, invalidating the native
caret recording even when the text value stays unchanged.

This milestone covers explicit line breaks, as used by SmartArt's one-item-per-
line editor. Existing soft-wrap caret/hit geometry and composition autoscroll
remain separate limitations; this change clips composition painting but does
not revise that geometry.

## Verification

- `RadiantTextQuery.TextareaRevealsEditingCaretAndClipsScrolledContents` passes
  at 1× and 2×. It checks visible caret pixels after navigation and value edits,
  scrolling back to the first line, a new final blank line, and zero caret/text
  pixels outside the content viewport. An active collapsed pointer gesture
  verifies that an offscreen caret is clipped even before drag autoscroll.
- The original implementation failed the caret visibility/clipping regression.
- Native SmartArt replay: eight assertions pass for multiline input, navigation,
  Enter, typing and paste; captures confirm that scrolled text stays in the box.
- Existing Word illustrations replay: all 58 assertions pass.
- Radiant dimension-cast lint and `git diff --check` pass.
- Required Radiant baseline: 6,147 passed, 141 existing partials, two skips and
  one unrelated failure. All 209 text-query tests, 29 state-store tests and 661
  native UI cases passed. The failing
  `RadiantViewTest.DemoCommandLaunchesCatalogAndPreservesViewerOptions` still
  expects `#splash` and the custom-home `#demo-home-splash`; existing workspace
  changes in `lambda/main.cpp` route the unnamed demo command to
  `test/demo/index.ls` instead. This task does not change that command or test.
  Artifacts: `temp/word/textarea/`.
