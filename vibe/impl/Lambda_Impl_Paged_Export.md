# Lambda paged export integration

**Status:** script export controls implemented; focused checks pass, aggregate
qualification running, 2026-11-12.
**Authority:** D9.1.1v4 / D9.1.3v2 assign the script adapter to Lambda and native
layout/rendering to Radiant. D5.5.1 / D5.1.4 govern retained resources and caller
lifetimes. Page selection follows [paged-media design §3.2.6](../radiant/Radiant_Layout_Paged_Media.md#326-page-filtering-and-two-page-book-preview).

## Current checkpoint

- Workspace: `/Users/henryluo/Projects/Lambda-opus`, branch `master`.
- Goal: expose physical-page preview selection, PDF export selection and page
  thumbnails through `radiant.export_document` using the existing native API.
- Reproduced: `export_pages:"2"` silently exports all three sheets; a requested
  page-two thumbnail paints the complete 100×360 preview instead of 100×120.
- Scope: Lambda adapter, Lambda-owned integration tests and documentation.
- Implementation: pass `pages`, `export_pages` and `thumbnail_page` into
  `RenderPagedOptions`; reuse the native selection parser and export pipeline.
  Copy range strings into native storage before layout can call Lambda/GC.
- Acceptance: selections preserve original physical order and page geometry;
  preview filtering never filters PDF output; thumbnails ignore preview
  selection; bad types/ranges/formats reject without replacing destinations.
- Verification budget: focused script checks in interpreter/JIT, CLI/API export
  parity and paged controls; final Lambda and reduced Radiant baselines.
- Focused results: debug build passes; the final script golden passes in
  interpreter and JIT; four export GTests pass, including CLI/API pixel equality
  and independent PDF text extraction. All 16 rejected requests preserve their
  destination pixels. The existing exact integer helper accepts integer-valued
  decimals and rejects fractional decimals without binary64 rounding.
- Initial Lambda baseline: 7,440/7,440, before the final exact-integer refinement.
- Next: finish the reduced Radiant baseline and final Lambda baseline refresh.

## API

All three optional keys require `pagination:true`. `pages` and `export_pages`
accept the native range grammar (`all`, `none`, or one-based physical indexes
and inclusive ranges, e.g. `3,1-2,2`). Ranges normalize into original page order.
`pages` affects preview PNG/JPEG/SVG; `export_pages` requires PDF. Omission keeps
all physical PDF pages. `thumbnail_page` is a positive integral physical index
for PNG/JPEG/SVG, independent of `pages`; output density still uses `scale`.
Malformed selections and out-of-range indexes return the existing string error
result. A full TeX frontend, engine fragmentation and P7 performance work remain
in the Radiant implementation record.
