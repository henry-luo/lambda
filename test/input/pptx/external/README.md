# External PPTX corpus and survey

Twenty unmodified downloads, selected for distinct primary features and checked
against publisher SHA-256 values. No file duplicates another in this set or the
existing local PPTX fixtures by hash. [manifest.json](manifest.json) records each
exact URL, hash, size, license, feature and source slide count.

- The 15 `mit-` decks are PowerPoint-authored test fixtures from [pptx-ts at
  revision d82bef42c9af1cd1dcadf5132092181557366396](https://github.com/shbernal/pptx-ts/tree/d82bef42c9af1cd1dcadf5132092181557366396/test/read/fixtures).
  The author describes the fixtures as license-clean; the project MIT notice is
  included in [LICENSE.pptx-ts](LICENSE.pptx-ts). Their published hashes appear
  in the [pinned fixture README](https://github.com/shbernal/pptx-ts/blob/d82bef42c9af1cd1dcadf5132092181557366396/test/read/fixtures/README.md).
- The five `cc0-` decks are from [loremfile's CC0 PPTX samples](https://loremfile.dev/pptx/),
  verified against its [published checksums](https://loremfile.dev/sha256sums.txt).

## Baseline results, 2026-11-12 (before the follow-up)

Both `lambda.doc.pptx.convert` and Slides Open loaded 20/20 files with the
source slide count. Slides replays checked first-slide text where available,
last-slide navigation, and selected image, note, table and chart content.
The ordinary column chart rendered. The package reported 50 diagnostics across
11 decks. The detailed machine-readable counts are in [survey-results.json](survey-results.json).

An independent OOXML scan found 148 direct slide `a:t` runs; each run was found
somewhere in the package's extracted text after whitespace removal. This is only
a smoke check: it does not check run counts or slide association and does not
inspect text stored in SmartArt, charts, equation parts, or external resources.
No independent full-slide visual comparison was available, so an “opens”
result is not a PowerPoint fidelity claim. The shared OPC boundary is **D11.1.2**;
the package and demo use the document package boundary **D9.3.4**.

| Fixture | Primary feature | Package and Slides Open | Diagnostics |
|---|---|---|---|
| [mit-group-transform.pptx](mit-group-transform.pptx) | nested groups and nonuniform transforms | opens; 2 slide(s) | unsupported-shape × 14, unsupported-group-transform × 3 |
| [mit-theme-colors.pptx](mit-theme-colors.pptx) | theme colors and modifiers | opens; 1 slide(s) | unsupported-background × 1 |
| [mit-gradient-fill.pptx](mit-gradient-fill.pptx) | gradient fills | opens; 1 slide(s) | unsupported-gradient × 1, unsupported-background × 1 |
| [mit-preset-geometry.pptx](mit-preset-geometry.pptx) | preset shape geometry | opens; 1 slide(s) | unsupported-shape × 2, unsupported-background × 1 |
| [mit-custgeom.pptx](mit-custgeom.pptx) | custom geometry | opens; 1 slide(s) | none |
| [mit-rotation-flip.pptx](mit-rotation-flip.pptx) | rotation and flips | opens; 1 slide(s) | none |
| [mit-table-merge-encoding.pptx](mit-table-merge-encoding.pptx) | merged table cells | opens; 5 slide(s) | unsupported-table-merge × 5 |
| [mit-smartart-families.pptx](mit-smartart-families.pptx) | SmartArt diagrams | opens; 4 slide(s) | unsupported-graphic × 4 |
| [mit-chart-series-shapes.pptx](mit-chart-series-shapes.pptx) | scatter, bubble and pie chart caches | opens; 8 slide(s) | unsupported-chart × 8 |
| [mit-math-omml.pptx](mit-math-omml.pptx) | native mathematical equations | opens; 1 slide(s) | none |
| [mit-av-media.pptx](mit-av-media.pptx) | embedded audio and video | opens; 2 slide(s) | unsupported-animation × 2 |
| [mit-model3d.pptx](mit-model3d.pptx) | embedded 3D object with fallback | opens; 1 slide(s) | none |
| [mit-slide-transition.pptx](mit-slide-transition.pptx) | slide transitions | opens; 6 slide(s) | unsupported-transition × 4 |
| [mit-slide-animation-rich.pptx](mit-slide-animation-rich.pptx) | object animation timing | opens; 1 slide(s) | unsupported-animation × 1 |
| [mit-slide-jump-link.pptx](mit-slide-jump-link.pptx) | internal slide hyperlinks | opens; 3 slide(s) | unsupported-link × 3 |
| [cc0-aspect-4-3-5slides.pptx](cc0-aspect-4-3-5slides.pptx) | 4:3 slide size | opens; 5 slide(s) | none |
| [cc0-with-hidden-slide.pptx](cc0-with-hidden-slide.pptx) | hidden slide | opens; 3 slide(s) | none |
| [cc0-with-table.pptx](cc0-with-table.pptx) | native data table | opens; 2 slide(s) | none |
| [cc0-with-chart.pptx](cc0-with-chart.pptx) | native column chart and workbook | opens; 2 slide(s) | none |
| [cc0-1mb.pptx](cc0-1mb.pptx) | embedded images and speaker notes | opens; 5 slide(s) | none |

## Baseline feature limitations (before the follow-up)

- Sampled SmartArt and unsupported chart slides were blank in
  Slides renders; diagnostics identify omitted graphic frames and unsupported
  charts. The CC0 clustered-column chart renders as an SVG.
- Merged table cells are presented as saved grid cells. Preset geometry, some
  backgrounds/gradients and nonuniform groups are approximated and diagnosed.
- Transitions, object animation and internal slide links are omitted or inactive;
  the affected files report diagnostics.
- The CC0 hidden-slide fixture marks slide 2 `show="0"`, but native playback
  shows it after Next, with status `2 / 3`; import emits no diagnostic for this.
- The audio/video fixture shows poster images and produces no `<audio>` or
  `<video>` elements in the static handout. Its warnings concern animation timing,
  not the omitted media. The 3D fixture displays its fallback picture; interactive
  3D is not established.

## Follow-up results, 2026-11-12

The package and editor still opened 20/20 files, with 96 sampled UI assertions
passing. [survey-results-after.json](survey-results-after.json) records the new
per-deck diagnostic counts; diagnostics rose from 50 to 56 because partial
SmartArt, chart, media and 3D fallback support is reported explicitly.

- Saved SmartArt drawings now produce visible shapes and text on all four sampled
  slides, with a partial warning; the diagrams are not editable as SmartArt.
- The five valid merged-table slides now produce HTML row/column spans, and the
  editor replay checks that the merged cell occupies multiple rows.
- Numeric scatter and bubble charts, including charts without explicit X caches,
  and the pie chart display using cached data. The sample's nonnumeric X caches
  and multilevel category cache remain unsupported; chart styling is approximate.
- The hidden slide remains editable and is skipped during sequential playback.
  The editor indicates hidden slides and can toggle their visibility.
- Audio/video posters and the 3D fallback are retained with explicit warnings;
  interactive playback and 3D remain unsupported.

The native replay `test/ui/slide_pptx_external_features.json` checks these paths;
complete visual fidelity and embedded media playback are not established.

## Reproduce

From the repository root, run `python3 test/input/pptx/external/survey.py`.
The script checks local hashes, imports each file, extracts a simple independent
slide-text oracle, and drives Slides Open in headless mode. Intermediate JSON,
logs and screenshots are written to `temp/pptx-external/`. Pass `package` or
`ui` to select a phase. Interactive media, hidden-slide playback and complete
visual fidelity require additional targeted checks.
