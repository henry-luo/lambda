# Word demo: page margin presets and facing pages

**Date:** 2026-10-23
**Authority:** **S1.4**, **S12.1.3**, **D7.2.5** for immutable document settings,
editing and projection. Existing CSS page composition supplies print geometry.

## Delivered behavior

Layout → Margins offers Normal (1″), Narrow (½″), Moderate (1″ vertically,
¾″ horizontally), Wide (1″ vertically, 2″ horizontally), and Mirrored (1″ top,
bottom and outside; 1¼″ inside). Each row previews the page and shows its values.
The current preset is selected only when all four margins and its mode match.

Custom Margins reuses Page setup, supports fractional millimetres, and switches
normal/mirrored mode while retaining unsaved fields. It applies to the whole
document through the existing single-step page transaction. Existing validation
also protects header/footer room and minimum column width. Invalid changes leave
the document untouched; history and editable/HTML round trips retain settings.

## Model and presentation

`data-word-page` gains an optional boolean `mirrored`. Omission retains existing
documents and defaults. In mirrored mode, `left` stores inside and `right`
outside. `mod_page.resolved` maps these to physical page edges using page parity;
editing never rewrites source paragraphs. Sheets, thumbnails and repeated
stories request the geometry for their page number.

The horizontal ruler follows the selected visible page, or a visible sheet when
the caret is offscreen. A gesture captures that page number, maps its physical
handle to the stored inside/outside edge, and preserves the original selection.
Live previews update every sheet/story with its own parity. Ruler labels name
the inside/outside edge; shaded margins and zero-origin ticks follow the page.

## Export

`@page:left` and `@page:right` alternate physical margins in PDF. The native
column master now inherits paper geometry from that shared page cascade; its
previous duplicate named margins would outrank generic facing-page rules.
Export resets the UA body margin at the page layer, since page settings already
own the inset. This also removes the extra body offset from ordinary PDFs.
HTML retains margin metadata and print rules, and uses the first page's margins
for its existing continuous screen presentation.

## Verification

The margin model golden covers presets, parity, physical ruler mapping, editing,
history, invalid settings, column/header compatibility, and persistence in all
three execution modes. The native replay covers the menu, custom fields and mode
changes, rejection, even-page ruler dragging, scroll-driven ruler changes,
thumbnails, stories, source editing, read mode, and save/import/export.

Independent PDF extraction checks preset edges at 72/36/54/144 points, mirrored
edges at 90/72/90 points across three sheets, and the same behavior with columns.
Neighboring layout, columns, ruler, navigation, ribbon, view and story workflows
qualify shared presentation changes. Evidence is recorded in
`temp/word/tests/margins-results.json`. No Radiant engine code changes are needed
for this milestone.
