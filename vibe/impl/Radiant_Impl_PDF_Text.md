# PDF text export

Status: implemented and verified, 2026-10-18. Authorized by the user's
PDF-text proposal and instruction to implement it.

Contracts: functional PDF construction and frozen output resources follow
**S2.6.6v3 / D2.6.12v3**; native resource lifetimes and precise roots follow
**D4.5.1v4 / D5.3.3**. The Lambda package owns PDF syntax; native helpers expose
selected font programs, glyph identities and positions.

## Delivery

1. Export complete selected SFNT faces, including collection extraction, with
   validated table extents, checksums and embedding permissions.
2. Emit positioned PDF text with embedded fonts, ToUnicode and logical text.
   Reuse font resources across pages and preserve existing placement and clips.
3. Carry text through decorated, paged and SVG graphic visits. Affine placement
   remains PDF text; only non-affine glyph deformation admits outline fallback.
   Raster-only effects must retain text semantics.

Acceptance: independent PDF structure and extraction checks for Latin, Unicode,
fallback fonts, multiple pages, decorations and affine transforms, together
with native-render comparisons. Check unsupported font failures explicitly.
Subsetting and additional font-format optimizations follow complete embedding.

Verification budget: focused font/export tests while editing; Lambda PDF
goldens on interpreter and MIR; PDF visual checks; required Lambda and Radiant
baseline gates once the implementation is stable. Record failures with evidence.

## Implementation

Direct text callbacks and `PAINT_GLYPH_RUN` records now share positioned PDF
text serialization. Each glyph retains Radiant's selected fallback face and
placement; affine matrices and synthetic italic remain text. ToUnicode maps
character codes, while ActualText preserves logical source text, including
non-BMP characters and visually reordered runs.

`font_export_face` snapshots complete static TrueType/CFF faces, validates
embedding permissions and table extents, and rebuilds standalone SFNT offsets
and checksums for collection faces. Native, variable and color fonts use Type 3
font programs with normalized outlines or color glyph images. Those programs
are invoked through PDF text operators, rather than copied into page paths.
Restricted embedding and unsupported native glyph programs report errors.
Font snapshots are immutable; growing caches remain private and precisely
rooted under **S2.6.6v3 / D2.6.12v3 / D5.3.3**. Document resources retain faces
and isolated SVG font contexts until export finishes (**D4.5.1v4**).

Decorations, clips, paged text and solid-fill inline SVG text use this path.
Simple opacity groups become transparency forms; their bounding boxes include
overflowing glyph and decoration ink. Pixel-only effects retain invisible text
over the native raster capture. Missing characters retain the native red
replacement box and invisible Unicode text. Font resources are shared across
pages, including generated glyph-font banks.

The package emits PDF 1.5 for ActualText, or PDF 1.6 for embedded OpenType/CFF.
Native helpers expose font bytes, glyph programs and placement; Lambda still
owns object allocation, resources, streams and PDF syntax.

## Verification

- `make build` and `make build-test`: passed.
- Three focused font tests: passed (owned font/checksum/metrics, permissions
  and malformed extents, collection extraction).
- All 96 PDF goldens: passed on interpreter and MIR JIT, including a final
  coherent rerun after the last native/package changes.
- New PDF export under forced collection and freed-memory poisoning: passed.
- Seven focused PDF regressions: passed, including independent Unicode
  extraction and shared fonts across pages, fallback/missing glyphs, nested
  clips, overlapping opacity groups and overflowing glyph/decorative ink.
- Supplementary Latin/CJK/Hebrew export comparison: native and Poppler ink
  bounds match; all thresholded ink is within two pixels in both directions.
  The comparison uses the shared viewport because native capture and PDF have
  different white overflow margins.
- `make lint ARGS='--rule ^no-int-cast-radiant$'` and `git diff --check`: passed.
- Initial Lambda baseline: 7,883/7,884 passed. The new PDF test ran against a
  binary built before the matching font-cache/package changes. The affected
  PDF suite then passed all 96 cases on both interpreter and MIR JIT with the
  final build. Other baseline suites passed, including all 313 MIR GC cases.
  The complete aggregate was not repeated after that focused rerun.
- Initial sandboxed Radiant baseline could not bind the WPT loopback server.
  With socket permission, all suites passed except the Bootstrap kitchen-sink
  page load, which exceeded its 15-second limit under aggregate load.
  Three independent reruns passed (8.1, 7.6 and 7.4 seconds), following
  Developer Guide §7.4. The aggregate reports 5,891 passes, 158 partial cases,
  one skip and that one timeout; WPT input events passed 32/32, paged media
  500/500, and the layout/render baseline runners reported no regressions.

Local evidence: `temp/pdf_text_{interp,jit}_final.log`,
`temp/pdf_text_font_final.log`, `temp/pdf_text_gc_final.log`,
`temp/pdf_text_visual_final.log`, `temp/pdf_text_lambda_baseline.log`,
`temp/pdf_text_radiant_baseline.log` and
`temp/pdf_text_page_load_rerun{,_independent}.log`.

## Export correctness and runtime follow-up (2026-10-19)

The complete PDF suite exposed shared export defects and outdated controls.
Calculated corner radii now resolve against the final box before choosing a
rectangular boundary shortcut. PDF text uses the fragment iterator's selected
font/color, including RTL ellipses. Invisible text scopes its rendering mode
so later visible siblings paint normally. Native graphic images retain their
source samples and alpha; pixel-preserving image sampling uses the shared
sampler. Decoration subdivision uses local coordinates before placement.
PDF numeric syntax now retains round-trip precision under **S2.6.6v3**, rather
than `string()`'s six-significant-digit display spelling.

Export comparisons use Poppler Cairo with the same grayscale antialias mode
at both densities. Byte-exact PNG/SVG/PDF comparisons remain, with positive
color-coverage assertions for outline controls. Sharp-effect references use
independently positioned outline copies: ordinary embedded PDF text is
font-hinted by the reader. AVIF is a supported codec, so the unsupported-type
control names an unregistered MIME type. Structural controls now check the
embedded Unicode text and isolated transparency forms required by the PDF
output design §4.4, while retaining Unicode extraction and pixel assertions.

The companion corpus's `PrinceCatalogue_page_01` reference was last updated in
`lambda-test` commit `2150ea571` (2026-05-14), before intrinsic page framing was
corrected in `04bbbfb2a53` (2026-10-15). Its measurement changes from
2.30256065% to 2.92722372%; other entries, the 32-level pixel threshold, and the
0.2-point regression tolerance stay unchanged. A new intrinsic-page control
independently compares the same source with Cairo: 0.14240791% mismatch,
below the existing 0.75% intrinsic-geometry limit. The one-row correction and
source hashes are recorded in `temp/pdf_repair_baseline_correction.json`.

The PDF target now uses the existing shared GTest case queue with four bounded
workers and isolated scratch directories. Its four WebGL context combinations
are separate cases; all combinations still run. Tool availability is cached
per process. `LAMBDA_PDF_TEST_JOBS` controls concurrency, and
`PDF_TEST_CONFIG=release_native` selects an optimized harness for timing.
Shared workspace creation also serves the layout/snapshot adapters. Runner
contract checks cover complete case accounting, isolated fixed filenames,
the selected host, failure propagation, worker limits and exclusive checks.

The complete macOS suite passes **314/314**, with no skips, versus the original
**280 pass / 30 fail** of 310. The four existing WebGL context modes became
separate cases, and one intrinsic-image geometry case was added. A paired run
with frozen release hosts and harnesses (`NDEBUG`, `-O3`, thin LTO) measures
**633.38 → 184.24 seconds**, or **3.44× faster / 71% less wall time**. This is
one before/after pair on the same machine; source/binary hashes, host load and
raw results are retained in `temp/pdf_repair_release_{before,after}_timing.json`.

All **96 PDF package goldens pass on both interpreter and JIT**. The **59 shared
runner contract checks**, integer-cast lint and strict module-boundary check
pass. The Radiant aggregate retains its raw result: **5,891 pass, 158 accepted
partial, one skip and one Bootstrap page-load timeout** of 6,051, including
**500/500 paged cases**, with no layout/render baseline regressions. That same
aggregate-load timeout is recorded in the earlier receipt above. Three isolated
runs of the exact page CLI, with the same host and unchanged 15-second limit,
pass in **3.74, 3.54 and 3.64 seconds**, following Developer Guide §7.4. These
controls do not replace or rescore the aggregate result.

Consolidated evidence is in `temp/pdf_repair_qualification.json`; full PDF
results are in `temp/pdf_repair_release_after_results/`, the Radiant summary in
`temp/pdf_repair_radiant_results/test_summary.json`, and the independent page
controls in `temp/pdf_repair_bootstrap_isolated.json`. Qualification covers
macOS; sanitizer and other-platform runs remain separate work.

## Remaining limits

Complete static fonts are embedded; static-font subsetting is a later size
optimization. Complex SVG paint/filter combinations retain the existing
fallback; the semantic SVG capture currently covers solid, unstroked,
non-warped text. Warped glyph outlines do not yet carry an ActualText overlay.
Indexed glyph runs without source Unicode cannot reconstruct logical text.
Paged flow independently omits the inline SVG replaced box in the new fixture;
continuous export covers its searchable text, while paged export checks normal
text, fallback and font reuse on both pages.
