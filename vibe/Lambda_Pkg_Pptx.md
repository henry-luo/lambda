# PowerPoint import

The core milestone imports saved PPTX presentations into the existing editable
`lambda.slide` element model. Shipped modules follow **D7.2.1/D7.2.4v4**;
package reads use the existing bounded ZIP/XML readers (**D7.5.2**,
**S12.4.1v2/S14.3.1v2**). Presentation data never becomes Lambda code (**S1.8**).

## Delivery and acceptance

1. Add `lambda.doc.pptx` with DOCX-shaped conversion helpers. Follow the ordered
   slide relationship list, including relocated parts, namespace aliases and
   Strict namespace URIs. Reject broken package structure and invalid XML.
2. Preserve text runs, direct/inherited typography, solid theme colors,
   placeholders, simple shapes, embedded raster images, ordinary tables,
   group bounds and speaker notes. Reuse existing DrawingML color, crop and
   cached-column-chart interpretation instead of copying those implementations.
3. Connect `.pptx` to native `view`, `layout` and `render` document transforms,
   with retained Slides playback. `--archive` must still expose the ZIP parts.
4. Open PPTX in the Slides editor as a cancellable background import. Preserve
   the previous deck on failure; ignore replies from canceled requests. Show
   recoverable warnings in yellow and save the editable copy as `.slides`.

Core acceptance cases are `minimal.pptx`, `features.pptx`, relocated/Strict/
alternate-prefix variants, malformed and external-resource fixtures, and six
pinned independent python-pptx reference decks. The native player replay must
render inherited typography, a raster, table and group and navigate to the
second slide. The editor replay must import, edit, save and reopen a deck, keep
an earlier deck after a failed import, and display warnings without rejecting
usable content. Existing DOCX goldens qualify the shared OPC extraction.

Focused checks run on interp/auto/jit. Native UI replays cover the new paths,
neighboring Slides file handling and Word asynchronous opening. Milestone gates
are `make test-lambda-baseline`, `make test-radiant-baseline`, and the Radiant
integer-cast lint. Artifacts and the final qualification record belong under
`temp/pptx/`. No performance claim is based on a debug build.

## API and profile

`convert(source, options=null)` returns `{document,html,text,metadata,diagnostics}`.
The source is a local path or a captured ZIP root. `document` is a
`<presentation>` accepted by `lambda.slide.compile`; `html` is a complete
static handout. `to_elements`, `to_html` and `extract_text` select these fields.
`images` accepts `"data-uri"` (default) or `"omit"`; `unsupported` accepts
`"warn"` (default) or `"error"`. Diagnostics retain severity, code, owner part
and message. Public import errors use the 7700–7706 range.

The importer flattens layout/master inheritance into each slide's editable
objects and embeds image bytes, so moving the saved copy does not break those
resources. Package relationships remain scoped to the owner part. External
resources are not fetched. Unsafe or unsupported links retain their visible
text. The common OPC reader is also used by DOCX, preserving its error codes.

The core profile is partial. Numeric custom paths preserve straight, cubic and
quadratic segments, closing paths and winding holes. Shape fills support solids,
linear and point-focus circular gradients with alpha, stretched picture fills
and duotone raster images with opaque endpoints. Guide expressions, arcs, tiled
or noncircular gradients, pattern fills, SmartArt, advanced effects, embedded OLE/media, animation timing,
slide transitions and exact PowerPoint text fitting are diagnosed or outside
the profile. Tables retain saved cell content; complex merging and theme table
styles require further work. Native editor scaling can also shift table text paint
relative to its otherwise valid cell geometry; exact PowerPoint appearance is
not qualified. PPTX writing and PowerPoint-compatible round-trip
layout are separate milestones.

## Completion record — 2026-10-27

The core import milestone is implemented. The package, native document routes,
document-browser preview and asynchronous Slides Open all use the shared reader
and existing presentation model. Embedded previews use shared canvas CSS without
the standalone player's root-page rules; the player CSS remains byte-identical.
Saving an imported deck writes an editable
`.slides` copy with embedded raster data.

Focused qualification passed 26 auto-tier package/Word-open goldens and six
PPTX/Word-open goldens on each forced interpreter/JIT tier. All six independent
reference files retain their hashes and saved slide text. Six UI workflows pass
136 assertions; the archive escape hatch passes two more. Direct PPTX layout and
PNG rendering also succeed. The complete UI baseline passes 734/734 cases.

The wider Lambda/Radiant gates retain baseline failures. Exact raw gate totals,
the sandbox access failures and subsequent unrestricted native reruns, the
pre-change layout comparison, source hashes and artifact paths are recorded in
`temp/pptx/qualification.json`. These are functional checks, not performance
qualification. The unsupported/partial areas listed above remain separate work.

## Imported-deck rendering follow-up — 2026-10-27

A 27-slide presentation exposed repeated master-decoration warnings and a
custom white frame flattened to an opaque rectangle. Numeric paths now preserve
the frame opening and curves; picture fills, duotone color matrices, HSL color
modifiers and transparent radial gradients retain the master artwork. DPI
extension metadata is not misreported as an image effect. Unsupported image
operations still return diagnostics rather than disappearing silently.

Warnings start collapsed and group equal code/message pairs with occurrence
counts. The scrollable expanded list overlays the canvas so player geometry
stays stable. The standalone document retains its load/resize handlers, and
viewport fitting accounts for the notice above the player. The document browser
and Slides editor share the same disclosure and CSS. API diagnostics remain
complete, including their owner-part names.

Synthetic paint and repeated-master fixtures supply package goldens and native
pixel/disclosure/navigation replays. The reported deck is kept outside the test
corpus; all 27 slides navigate in a private replay. Its 173 original warnings
reduce to nine saved-animation-timing warnings in one group. Rendering artifacts
and current qualification evidence are under `temp/pptx-deck/`.

## Source references

- [Microsoft: PresentationML document structure](https://learn.microsoft.com/en-us/office/open-xml/presentation/structure-of-a-presentationml-document)
- [Microsoft: slide masters](https://learn.microsoft.com/en-us/office/open-xml/presentation/working-with-slide-masters)
- [Microsoft: DrawingML group transforms](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.transformgroup)
- [Microsoft: DrawingML gradient fills](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.gradientfill)
- [Microsoft: duotone effects](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.duotone)
- [Independent fixture provenance](../test/input/pptx/reference/manifest.json)
