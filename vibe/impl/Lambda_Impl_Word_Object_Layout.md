# Word demo: object layout and arrangement

**Date:** 2026-10-23  
**Authority:** **S1.4**, **S12.1.3**, **D7.2.5** for immutable commands, source
ownership and projection; **D4.5.1v4** for shared native paint/export ownership.

## Core milestone

Deliver Position, Wrap Text, Bring Forward, Send Backward, Selection Pane,
Align, Group/Ungroup and Rotate/Flip for image-backed document objects. The
Object Format contextual tab and Layout's Arrange group share the commands.
Inline, square left/right, top/bottom, front and behind are the supported text
planes. Wrapping uses CSS flow; freely placed objects remain attached to their
source paragraph, with a measured offset rather than a reconstructed document.

Acceptance includes real text reflow, page-margin placement, overlapping objects
in different paragraphs, source-preserving free dragging, cancellation/undo,
selection of covered/hidden objects, group transforms and recovery of editable
members, clipboard and snapshot/HTML round trips, and native PDF appearance.
The verification budget is the focused model golden in all execution tiers,
neighboring object/clipboard workflows, native layout interaction, independent
PNG/SVG/PDF paint parity, and the Radiant baseline for the renderer prerequisite.

## Source and gestures

`data-word-layout` records wrap, side, paragraph-relative x/y, text distance,
layer, visibility and flips. Transactions retain unrelated CSS and preserve
rotation while applying reflection. Positions apply to the current page-margin
rectangle; Align changes one axis. Free drags preview local coordinates and
commit one transaction. Inline/flow objects retain caret-based relocation.
Both gesture paths restore the original projection when canceled.

The Selection Pane enumerates source image atoms, orders their layers, selects
covered objects, hides/shows them, and lets the user check several for grouping.
Grouping accepts 2–20 objects on one explicit page, within 650 × 2000 px. It
stores each member through the shared HTML adapter and builds an embedded SVG.
The resulting image atom uses the same clipboard and transform ownership as a
picture. Proportional resizing avoids introducing shear that individual object
controls cannot represent. Ungroup composes translation, scale, rotation and
reflection into editable members and preserves all surrounding source text.

## Renderer prerequisite

The initial overlap replay exposed positive z layers being sorted within each
paragraph. The model's layer numbers were correct, but a later paragraph's
object painted and hit above an earlier one regardless of its z value.

`stacking_order.cpp` now collects signed layers at their nearest real stacking
context. Positive and negative lists share traversal and stable sorting, stop
at nested contexts, and replay intervening font/overflow/scroll scopes. Raster,
vector export and hit testing share this ancestry policy. Absolute/fixed layer
coordinates belong to the containing block; a shared offset helper removes
the intervening DOM-parent translation after replaying its clips. Positive
absolute layers are excluded from the old containing-block list to avoid double
paint. The selected view root owns the initial context even when its DOM parent
is retained; this keeps root-level dialogs and overlays painted and hittable.
Custom layouts retain their existing local signed paint sequence.

The focused fixture challenges paragraph anchors, overflow clips, a nested
numeric-zero context, and relative/absolute descendants of an ordinary block.
A flat rectangle reference independently specifies PNG/SVG/PDF pixels.

## Verification record

The model golden passes 51 checks in interpreter, automatic and JIT modes.
Eight native editor workflows pass 353 assertions; the focused engine fixture
passes 14 pixel/hit checks. Export checks recover grouped members, retain the
canonical HTML model, preserve every wrapping mode in PDF, and independently
measure text reflow and visible artwork. Both signed-layer PNG/SVG/PDF parity
tests pass. The rebuilt Radiant baseline records 6,139 passes, 141 existing
partial cases, two skips and zero failures across 6,282 cases. The layout integer
cast lint and scoped whitespace checks pass.

Evidence and binary/source hashes are in `temp/word/tests/object-layout-results.json`.
The core milestone is complete; the remaining limits below are separate work.

## Limits

Tight/through contour wrapping, floating objects that wrap on both sides at an
arbitrary vertical coordinate, page-fixed anchors, alignment/distribution of a
multi-selection, and Word/DOCX drawing-field interoperability remain outside
this milestone. Position presets capture the current paper geometry; later
page changes keep the saved paragraph-relative offset. Layer commands order
objects within the front or behind text plane. Grouping uses explicit editor
pages, and groups preserve their aspect ratio. General zero-level descendant
lifting and complete CSS stacking-context conformance remain separate work.
