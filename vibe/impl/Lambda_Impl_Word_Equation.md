# Word Equation ribbon and source editor

S1.4, S12.1.3, and D7.2.5: the document's image atom owns equation source and
formatting. Draft source is UI state; a validated transaction replaces the
source SVG and retains the surrounding document. D4.5.1v4 governs selected-view
and export ownership.

## Finite milestone

Provide a contextual Equation tab with preset equations, Unicode symbol
galleries, and Fraction, Script, Radical, Integral, Large Operator, Bracket,
Function, Accent, Limit, Operator, and Matrix galleries. Structure insertion
wraps the selected LaTeX source range and selects its first editable slot.
Normal Text inserts an escaped text group. A source editor retains incomplete
drafts and applies them explicitly, with errors leaving the document intact.

Professional and linear source views, inline/display layout, left/center/right
justification, and an explicit equation label use the same source graphic and
CSS in the editor and export. Reuse the shared native text-control range APIs,
math parser/typesetter, image transactions, history, and clipboard paths.

Acceptance: every supplied structure and symbol renders; source selection
survives toolbar/menu focus; display changes actual flow; invalid drafts do not
replace equations; undo/redo, rich clipboard, save/load, HTML and PDF retain the
equation. Verify the focused model golden in three modes, native ribbon/pane
workflows, adjacent math/Word tests, and exported text/artwork. No engine change
is planned.

## Boundaries

Input is LaTeX, including Unicode characters supported by the math renderer.
This does not claim Word's UnicodeMath linear grammar. The existing AsciiMath
writer/parser pair does not preserve the supplied fraction/root/matrix probes,
so an automatic conversion is not exposed here. Labels are explicit strings;
automatic equation numbering and cross references remain separate work.
Editing occurs in source, with rendered previews; there is no structured
in-canvas math caret yet.

## Completed verification

35 focused model checks pass in interpreter, auto, and JIT modes. The new native
workflow has 63 assertions, including rendered thumbnails in all eleven
galleries, selected-source insertion, nested structures, invalid-draft recovery,
labels, actual flow/justification, undo/redo, export, and read mode. Five existing
workflows contribute 239 passing assertions. The advanced-object fixture now
expects the source editor on equation double-click and verifies subsequent
WordArt editing after it closes.

Eleven adjacent Word/math goldens pass. Five export round-trip checks preserve
source, label, formatting, SVG, and surrounding text. Independent PDF checks
verify one page, body text, matrix entries, equation label, and colored artwork.
Python compilation and scoped whitespace checks pass. Evidence is recorded in
`temp/word/tests/equation-results.json`.

An unchanged Apply/Close transaction is excluded from undo history. Ribbon
content flex styling is scoped to the direct inner container, avoiding a class
collision with the outer group named Tools. No engine changes were needed.
