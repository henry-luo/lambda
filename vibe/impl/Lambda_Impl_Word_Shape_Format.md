# Word Shape Format

The contextual Shape Format ribbon edits selected shapes and WordArt. Source
image atoms retain parameters; SVG carries appearance. This follows S1.4,
S12.1.3, and D7.2.5. Screen and exported documents use the same graphic, with
the existing selected-view ownership under D4.5.1v4.

## Core milestone

- Expose a contextual tab, keeping Object Format and its Arrange controls.
- Change shape kind, apply styles, fill/outline colors, line weight/dashes, and
  an outer shadow. Provide a Text Box insertion command.
- Edit text style, color, font, bold/italic, alignment, vertical placement,
  inset, and direction. Reuse WordArt's existing styles and curved transforms.
- Provide a closable Format Pane with custom colors, gradient, opacity,
  dimensions, rotation, and alt text.
- Preserve placement, reflection, source selection, and surrounding text;
  support history, rich clipboard, snapshots, and HTML/PDF export.

`mod_shape_format.ls` validates partial property updates against the selected
object's settings, then uses the existing graphic transaction. Existing graphic
property dialogs also merge their exposed properties with source settings, so
they preserve new fields. Insertions use the submitted parameters directly.
The shared drawing text painter accepts optional typography and placement
options; existing callers retain their original placement.

Solid previews omit gradient definitions. Inline WordArt previews use unique
prefixes for paint servers. This prevents SVG IDs from shadowing dialog controls
or resolving into another preview. Embedded source SVGs have their own document
scope.

## Verification budget and acceptance

Use the focused model golden in interpreter, auto, and JIT modes; native
`shape_format.json`; existing shapes, transforms, Arrange, clipboard and ribbon
workflows; and nearby drawing model goldens. Verify snapshot/HTML round trips
and visible PDF output. No engine changes are required for this milestone.

The native replay checks actual source changes and control focus, opens the
existing settings dialog after advanced formatting, verifies unique DOM IDs,
exercises undo/redo and Arrange, and exports the edited document. The export
helper checks canonical model equality, text, metadata, and image sources.

Completed verification: 39 model checks in all three execution modes; 55 new
native assertions and 189 assertions in the five existing workflows; eleven
nearby Word/drawing goldens; five export round-trip checks; and independent
one-page PDF, retained-text, and visible-artwork checks. Python compilation and
scoped whitespace checks pass. Evidence is in
`temp/word/tests/shape-format-results.json`.

Color and dimension fields retain incomplete drafts independently of the
source atom. A source revision or a different object invalidates the draft;
only validated values change the document.

## Remaining scope

Custom paths/edit points, 3D, glow, soft edges, and automatic text fitting are
not implemented. Shape text wraps only at explicit newlines; WordArt remains
single line. Shape and WordArt objects use the existing inline/float/positioned
image layout and the existing dimension limits.
