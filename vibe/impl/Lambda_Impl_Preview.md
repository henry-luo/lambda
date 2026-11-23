# Implement `lambda preview`

The [CLI design](../../doc/Lambda_CLI.md#preview--first-page-document-preview) defines the two outputs and their latency targets. Both modes use one first-page loader. A thumbnail finishes when its image is written. A snapshot presents the same content through the viewer and keeps the first page's animation clock running.

## Current checkpoint

- Format coverage and benchmark, 2026-11-23, `lambda` checkout on `master` at `a0892901da` with local Lambda, Input and Radiant changes.
- Agreed scope covers first-page output, caller-selected thumbnail dimensions, coarse rendering for unreadably small thumbnail text, mode-specific animation, incremental parsing driven by layout, and the two latency targets.
- The CLI requires exactly one mode and a local input. A thumbnail requires a new PNG output path. Both pixel dimensions default to 256 if omitted. The renderer fits the viewport or physical page into the requested box. Snapshots use viewer geometry.
- HTML resumes the tokenizer and tree builder between batches. Markdown, MediaWiki, reStructuredText, Org, AsciiDoc and Textile read lines as the existing block parser requests them and stop between blocks. Nested directive parsing keeps its own line boundary. Radiant currently rebuilds the candidate DOM and layout for each batch. The committed first viewport stays at the top even when parser lookahead extends below it. Resize growth and incremental DOM invalidation still need implementation (D9.1.1v4, D9.1.2v2).
- HTML uses the local resource policy and inert scripts. The first implementation assumes UTF-8 input and first-page dependencies available in the loaded prefix. Markdown forward link or footnote dependencies and embedded transforms are outside the fast path. PDF selectively converts and composes page one after the existing full source parse.
- The preview loader passes `snapshot: true` to the DOCX, PPTX and LaTeX transforms (D9.3.4). PPTX selects the first slide relationship before loading slide parts. DOCX reads the ZIP and parses main XML, then expands names and normalizes only the selected body blocks. A saved or explicit break in that prefix ends the conversion. In unmarked flow the estimate uses page size, margins, paragraph indentation and leading, mean glyph advance, hard line breaks, and the heights of tables and inline drawings. One oversized block stays whole. This estimate approximates Word pagination. LaTeX still expands the source then cuts the AST before rendering adapters at forced breaks (D10.2.2).
- Safe file formats outside the incremental and first-page transforms reuse the standard viewer loaders with local resource policy (D11.1.2). XSL-FO and Radiant page thumbnails select the first composed page. EPUB starts at the beginning without reading or writing saved progress. The coverage run found an existing EPUB compile error from propagating the non-error `store.restore` return contract (S7.5.1v6). Removing that postfix restored the viewer route.
- Transform verification used the local release build. Four DOCX break variants retained the first text and omitted the second. PPTX loaded one slide with invalid XML in later slides. LaTeX kept content after `\pagebreak[1]` and stopped at a macro-expanded `\newpage`. All six CLI fixtures produced nonblank 256 × 256 PNGs and completed headless snapshot runs. Evidence is under `temp/preview/transform-fonv5r2y/`.
- Coarse raster paint handles untransformed small runs in flowing thumbnails. Layout still uses real font metrics. The default cutoff is a projected size of 3 output pixels. Fine tuning, actual-face avoidance in layout and PDF coarse paint remain open (D9.1.1v4).
- `make build-release-compile` and the Radiant integer-cast lint pass. The final arm64 macOS release build produced nonblank 256 × 192 PNGs for long HTML, short and long Markdown, and a two-page PDF. At 128 pixels, equal-length words in a monospace face produced identical strokes. At 640 pixels the glyph shapes differed.
- Long HTML and Markdown produced identical thumbnails when appending a tail increased each input from 1.9 MB to 11.6 MB. The larger files exceed the preview byte budget, so success confirms early stop. A sparse HTML file rendered a visible block after over 8 KiB of hidden content. Route evidence is under `temp/preview/routes-5jiva8ay/`.
- Warm filesystem launch-to-PNG p95 was 25.6 ms for long HTML and 28.0 ms for long Markdown across 20 fresh processes per fixture. One launch immediately after relinking took 634.3 ms. Cold startup is unqualified. Headless snapshot launch through exit was 34.8 ms for long HTML, 24.1 ms for long Markdown, and 34.1 ms for the two-page PDF. Live presentation and animation still need verification. Measurements are in `temp/preview/release-eoyf5rsc/results.txt`.
- Coverage benchmark in §6 measures 43 preview fixtures in both modes and six additional ordinary viewer inputs. All five new incremental markup routes produced identical images when source size grew from about 1.2 MB to 12.2 MB. The unmarked Word fixture retained nine paragraphs and the identical image with 3,000 and 6,000 body paragraphs. Evidence is under `temp/preview/coverage/prefix-93xnkkfo/`.
- The XML parser fix in LI-104 bounds the strict character-data terminator scan to its text run. The 2.44 MB raw Word XML parse went from about 1.82 s in the initial launch probe to 19.0 ms median across five release launches. First-page Word preview still parses the main member's XML, so indexed or resumable OPC intake remains open.
- Thumbnail sessions use the first-frame GIF decoder for local files and embedded data images. A 160-frame GIF dropped from 146.3 ms p95 to 16.2–22.3 ms in two runs. XHTML still loads through the full strict XML route defined by D10.1.4. See §7.
- Both preview modes route `.ls` to Radiant's escaped source view. The source loader reads the full file and the preview displays the first viewport. `lambda view` continues to evaluate script documents (D9.1.1v4, D9.1.2v2). See §8 for source preview measurements.
- GLB uses Input's binary model importer and Radiant's native Scene3D renderer. The thumbnail skips animation conversion and reduces embedded textures to the output scale. The snapshot keeps the first applicable clip and uses a camera fitted to the initial posed bounds (D9.1.1v4, D9.1.2v2, D10.1.7). See §9.
- GLB thumbnail texture decode caps the longest edge at the output's longest edge. Static triangle scenes can omit subpixel parts before native geometry and texture preparation. See §9.1 (D9.1.1v4, D10.1.7).
- The snapshot target is 250 ms to the first usable frame. See §9.2 for qualification of the animated GLB and the native color resolve change (D9.1.3v2, D10.1.7).
- Snapshot fixes the Lambda tier to strict T0. DOCX and PPTX viewers consume their document models directly. See §10 for the package startup and JIT measurements (D8.3.1v3, D9.3.4).
- Next work is indexed PDF acquisition and a Word body XML input cursor through OPC. Also qualify live snapshots and cold startup, and profile actual face loading for coarse text.

## 1. Integration points and ownership

| Owner | Existing path | Planned change |
|-------|---------------|----------------|
| Lambda CLI | [command dispatch](../../lambda/main.cpp), including `parse_doc_window_launch_options` and the build-profile exclusions | Add mode parsing and validation before opening the source or initializing a window. Reuse view launch and raster output facilities. Thumbnail needs offscreen Radiant. Snapshot needs the windowed build. The runtime-only CLI should report the missing capability (D9.2.1v2, D9.2.2). |
| Input | [input dispatch](../../lambda-input/lambda/input/input.cpp), [markup parsers](../../lambda-input/lambda/input/markup/), [HTML5 parser](../../lambda-input/lambda/input/html5/) | Extend the existing source service and format parsers with bounded reads and resumable state. Input owns source identity, acquisition and Mark lifetimes (D9.1.2v2, D9.1.4v2, D11.1.2). |
| Lambda document packages | [document viewer](../../lmd/package/doc/doc_viewer.ls), [PDF](../../lmd/package/doc/pdf/pdf.ls), [DOCX](../../lmd/package/doc/docx/) | Reuse the format's existing presentation and styles. Supply first-page data and reachable resources through the host interface. Preserve package ownership under D9.3.4. |
| Radiant | [document loader](../../radiant/radiant/cmd_layout.cpp), [paged layout](../../radiant/radiant/layout_paged.cpp), [raster output](../../radiant/radiant/render_img.cpp) | Retain one document and layout context across refills. Return first-page flow and break information. Reuse offscreen paint for thumbnails and window presentation for snapshots (D9.1.1v4, D9.1.3v2). |

The PDF loader already forwards `import_pages` and `max_pages` to the transform. The viewer also has an all-document PDF presentation route. Trace acquisition through [the native PDF reader](../../lambda-input/lambda/input/input-pdf.cpp) before attributing any saving to page selection. Likewise, the Markdown dispatch takes a source string, and the DOCX package uses shared OPC access. Placing a limit after these calls can leave the most expensive work intact.

## 2. One request from open to first frame

1. **Choose the page geometry before reading content.** For fixed pages, obtain the first page's box and orientation from the format. For flow, use the viewer's initial content viewport at a reproducible reading size. A tiny thumbnail fits that logical page into its output canvas with padding. Its pixel width must not cause a paragraph to wrap at icon width. Use float geometry throughout Radiant.
2. **Open a resumable input session.** Keep the source handle, offset or object cursor, decoder carry, structural parser state, resource requests, and the Input owner alive together. Each advance accepts a bounded work allowance and yields a change to the retained document, a dependency request, a real page boundary, EOF, or an error. Distinguish a temporary chunk boundary from EOF. The parser can consume input without emitting a complete block. Record this progress so the controller neither spins nor finalizes a partial token.
3. **Estimate a first batch.** For flow, start from available line width and page height, font metrics and a conservative estimate of bytes per visible line. Update the estimate from the measured advance in normal flow. Bound the next read even when a long comment, hidden section or compressed part yields little visible content. Record the estimate and actual consumption for tuning. Indexed pages use direct seeks instead.
4. **Lay out and refill on demand.** Radiant reports whether following flow can still enter page one, the remaining block space and any unresolved visible geometry. If room remains, advance the same parser and append or revise its affected tail. Invalidate layout from the earliest changed ancestor, including earlier dependents when a style, font, table width or image size changes. Repeat until overflow proves the viewport is filled, the formatter commits a page break, or Input reaches EOF. Margins, columns, transparent backgrounds and an intentionally blank first page make pixel occupancy an invalid stopping test.
5. **Resolve first-page dependencies and present.** Include page geometry, visible text, styles, fonts and images needed for the reference appearance. Apply late geometry changes and recheck the fill decision. Freeze the thumbnail timeline at initial presentation time, paint once offscreen, encode PNG and publish the completed image. Snapshot paints into the viewer and services animation, repaint and asset callbacks for the retained page. Screen clipping alone does not suppress off-page loading or animation work. A resize recomputes the first viewport and resumes the cursor only if more content fits.
6. **Bound the lifecycle.** Use the existing host resource policy for document and subresource access (D11.1.2). Propose inert document scripts and no external fetches on automatic file-manager launch. Start snapshot animation coverage with declarative animation and animated images. Qualify script-driven animations separately against viewer policy before advertising parity. Cancel pending reads, decodes and callbacks on close, replacement or failure. Release Input and DOM owners after their last consumer. Bound decoded bytes, nesting, raster allocation and per-advance work. On an unsupported dependency or exhausted budget, return a diagnostic without publishing an incomplete image as success.

### Coarse text in thumbnails

Radiant owns this optimization (D9.1.1v4). Classify each run by its projected ink height in final **output pixels**, including page fit and local transforms. Estimate height from the styled size when the face is unopened. Use ordinary text rendering for readable runs and for snapshots. Choose the cutoff by comparing thumbnails at actual file-manager sizes during release qualification.

- **Paint cheaply.** Reuse the line fragments and positions supplied by layout or the document. For a tiny run, batch simple coverage-weighted strokes across occupied word spans. Carry baseline, gaps, foreground color, opacity, clipping and writing direction through paint. This preserves paragraph density and ragged line endings even when individual letters occupy less than a pixel. Select this path before glyph lookup, atlas population or bitmap generation. Trace both [text rendering](../../radiant/radiant/render_text.cpp) and the offscreen paint path used by the PNG target.
- **Skip the face when geometry permits.** Fixed-position runs can use supplied advances directly. For ordinary flowing body text, carry the thumbnail policy into [font setup](../../radiant/radiant/font.cpp) and [text measurement](../../radiant/radiant/layout.hpp) before requesting document faces. Use inexpensive shared metrics for eligible runs with the existing line breaker. Keep exact measurement where font metrics determine layout-sensitive widths, shaping or page breaks that approximation cannot reproduce within the fidelity gate. Readable runs and meaningful symbols such as icon glyphs can request the real face independently. Request a shared face if any such run needs it.
- **Qualify quality and cost together.** Compare full-font and coarse renders of the same page at several output dimensions, including sizes on either side of the cutoff. Use mixed titles and body text, short last lines, columns, complex scripts and icon fonts. Check readable regions with the usual raster tolerance. For coarse regions compare line extents, gaps and average ink coverage at output size, plus the position of the last visible block. Include a tiny-body-only fixture with a distinct font and assert zero opens of that face and zero body glyph rasterizations. Record face opens, shaping and raster counts, paint time and end-to-end thumbnail p95 for both paths.

## 3. Incremental work by format

| Format | First-page work | Boundary and correctness checks |
|--------|-----------------|---------------------------------|
| Markdown | Resume the native markup parser at its prior offset. Preserve list, paragraph, fence, table and inline state. Publish stable blocks and revise the open block as needed. | Split reads inside UTF-8, CRLF, fences, setext headings and table delimiters. A later definition can affect a visible reference. Resolve it by targeted lookup or a bounded, measured scan. Record unresolved cases as coverage limits. |
| HTML | Feed the existing tokenizer and tree builder successive decoded chunks. Keep insertion mode, open elements and pending text. Attach parsed content to the same DOM and reuse style, resource and layout invalidation. | Split tags, entities, raw text and table content across reads. Later styles, selectors or script mutations can change earlier nodes. Define the supported first-frame dependency policy explicitly and compare with `view` on that subset. |
| PDF | Read trailer and cross-reference data as needed, walk the first page-tree branch, and resolve the page's inherited boxes, rotation, contents and resource closure. Reuse the existing single-page transform. | Indexed reads may legitimately reach the end of a file. Count bytes and resolved objects, including indirect and compressed objects. Blank page one must stay blank. Record recovery paths for damaged or unsupported indexes separately. |
| Word (.docx) | Use the ZIP directory and OPC relationships to locate the main document, styles, numbering, section settings and first-page headers or media. Incrementally inflate and parse body XML. Feed paragraphs, runs and table fragments to the existing Word presentation. | DOCX normally flows. Respect explicit page and section breaks and dependencies that occur later, including section properties. Compare against the Lambda viewer at matching geometry. Bound inflation of large entries and leave unrelated members unopened. |
| Other transformed inputs | Add an adapter when its transform can declare required context and supply a first-page result. For TeX, preserve expansion semantics (D10.2.2). | Publish support per format. A suffix match or cropping a complete render does not qualify a route as incremental. |

For giant paragraphs, tables, or tokens, a whole block can exceed the page budget. Advance at a smaller unit where the format permits, retain the state needed to revise it, and cover the case with a fixture. Some documents have global dependencies that cannot be resolved from a prefix. Record which adapter encountered one, what additional work it required and whether it met the target. Never silently claim a complete first page after dropping a visible dependency.

## 4. Delivery phases and exit checks

1. **Baseline and trace.** Select fixtures and matching reference renders. Instrument source opens/reads, parse, package startup and transform, resource decode, layout, paint, presentation and encoding. Measure the cost of launching a fresh process before choosing parser batch sizes.
2. **Implement a vertical slice on Markdown.** Connect both command modes to one Input session and Radiant refill controller. Implement coarse thumbnail text and demonstrate the font-work counters above on a mixed-size page. Demonstrate a sparse prefix that requires several reads, a short document that stops at EOF, and a long document that stops after one viewport. Trace monotonic progress, bounded read-ahead and reuse of parser state.
3. **Add HTML and animation.** Cover chunk boundaries, head dependencies and late size changes. Use a controlled clock to compare a thumbnail to the initial frame. Observe two distinct snapshot frames for visible animation and verify that a later animation never starts. Exercise close during a refill and a resize that exposes more first-page content.
4. **Add PDF and DOCX.** Prove page-selective acquisition as well as first-page rendering. Use page-one dependencies stored near the end of each file and large, unrelated later content. Check explicit breaks, blank pages, images, tables and headers. Record measured support and limitations for each route.
5. **Qualify and document.** Run the release measurements below, compare outputs and fix the largest measured costs. Verify build-profile diagnostics, invalid dimensions, write failures and interrupted input. Publish supported formats, defaults and measured results in the CLI reference when each route qualifies.

## 5. Performance and fidelity gates

- **Measure the user's wait.** For thumbnails, time from process launch to a successfully closed image file and command completion. For snapshots, time from launch to presentation of the first usable document frame, including visible content and required resources. A window-open event or placeholder paint does not count. Record a separate animation-readiness timestamp.
- **Use a reproducible release corpus.** Run `make release` in the primary checkout per the developer guide. Pin hardware, OS, commit, fonts, viewport, output size and input hashes. Use local files, fresh processes and no precomputed previews. Measure a cold-cache cohort separately from repeated filesystem-warm launches. Record sample counts and cache preparation. Keep raw traces under `temp/preview/`.
- **Set gates per fixture and route.** Qualification is p95 below 50 ms on each native thumbnail fixture and p95 below 250 ms on each snapshot fixture on the reference machine, including long Markdown, PDF and DOCX. Report cold and warm results separately, including p50, p95 and maximum. Record transformed thumbnail times separately. A fast median across formats cannot establish either target.
- **Prove work scales with the first page.** Construct paired files with identical first-page dependencies and progressively larger tails. Record bytes read/inflated, nodes and PDF objects parsed, resources decoded, refill count, pages laid out/painted and peak memory. Verify that adding independent tail content leaves body processing bounded, allowing documented index/metadata costs. Instrumentation must expose an accidental full-file read even on a fast cache.
- **Compare like for like.** Capture page one from the existing view or paged renderer with identical geometry, fonts, assets and animation time. Compare visible text and geometry, then raster output with the existing rendering tolerance. Include sparse, intentionally blank, forced-break, single oversized element, and late-dependency cases. For thumbnails also check exact output dimensions and aspect ratio. Record failures and measured exceptions in this plan before declaring a format ready.

## 6. Release benchmark at 128 × 128

Run on 2026-11-23 on arm64 macOS with the release executable from
`make build-release-compile`. Each cell is **p50 / p95 in milliseconds**
over 20 fresh processes after one excluded warmup per fixture and mode.
The filesystem cache was warm. Thumbnail measurements include process
startup, rendering and publication of an exact 128 × 128 PNG. Snapshot
measurements invoke `lambda preview --snapshot` with `LAMBDA_TEST_HEADLESS=1`
and include startup, the first render and teardown. Live window
presentation and cold startup were outside this run.

The matrix covers the standalone viewer file families plus the incremental
markup routes and paged XSL-FO/Radiant files. Suffix aliases use one
representative fixture. Archive browsers and Lambda source are listed
separately. The benchmark uses local sources and resources.

All 43 document fixtures passed in both preview modes. Thumbnail p95 ranged
from 18.5 to 142.7 ms, with 26 fixtures below 50 ms. The five newly streamed
markup formats were between 22.4 and 22.9 ms. The unmarked 6,000-paragraph DOCX
hit 110.7 ms thumbnail p95 and 141.5 ms headless snapshot p95. EPUB had the
highest headless snapshot p95 at 243.4 ms.
Among the files that need no Lambda transformation, the XHTML gallery and
GIF exceeded the 50 ms thumbnail target at p95.

| Format | Fixture | Load path | Thumbnail | Headless snapshot |
|--------|---------|-----------|-----------|-------------------|
| MediaWiki | `temp/preview/coverage/fixtures/long.wiki` | Incremental block parser | 22.1 / 22.4 | 24.3 / 24.9 |
| reStructuredText | `temp/preview/coverage/fixtures/long.rst` | Incremental block parser | 21.8 / 22.9 | 23.3 / 23.6 |
| Org | `temp/preview/coverage/fixtures/long.org` | Incremental block parser | 22.1 / 22.5 | 24.4 / 66.1 |
| AsciiDoc | `temp/preview/coverage/fixtures/long.adoc` | Incremental block parser | 22.0 / 22.5 | 23.6 / 24.0 |
| Textile | `temp/preview/coverage/fixtures/long.textile` | Incremental block parser | 22.2 / 22.6 | 23.7 / 26.4 |
| HTML | `temp/preview/long.html` | Incremental HTML | 21.1 / 21.3 | 24.1 / 24.5 |
| XHTML | `test/html/xhtml_feature_gallery.xhtml` | Full viewer loader | 118.6 / 141.8 | 150.1 / 161.8 |
| Markdown | `temp/preview/long.md` | Incremental block parser | 21.8 / 22.3 | 24.0 / 25.7 |
| DOCX without breaks | `temp/preview/coverage/fixtures/unmarked.docx` | Estimated page prefix | 102.0 / 104.8 | 122.1 / 129.6 |
| DOCX extended | `temp/preview/coverage/fixtures/unmarked-extended.docx` | Estimated page prefix | 108.5 / 110.7 | 130.8 / 141.5 |
| DOCX with a break | `temp/preview/word-run-break.docx` | Explicit page prefix | 94.6 / 102.2 | 106.8 / 110.2 |
| PDF (250 pages) | `temp/preview/coverage/fixtures/long.pdf` | Page one after full parse | 31.8 / 34.9 | 39.1 / 51.6 |
| PDF | `temp/preview/debug-u0_lalrg/two.pdf` | Page one after full parse | 30.7 / 37.8 | 34.4 / 36.0 |
| PPTX | `test/input/pptx/external/cc0-with-hidden-slide.pptx` | First slide | 104.3 / 111.3 | 123.4 / 131.7 |
| LaTeX | `temp/preview/two-pages.tex` | Expansion then first break | 110.6 / 133.5 | 113.7 / 169.6 |
| TikZ / PGF | `test/input/tikz/diagrama_venn1.pgf` | Full viewer loader | 110.8 / 128.6 | 112.4 / 126.2 |
| EPUB | `test/input/ebook/minimal.epub` | Full viewer loader | 75.7 / 82.5 | 109.8 / 243.4 |
| Fixed EPUB | `test/input/ebook/fixed.epub` | Full viewer loader | 108.1 / 142.7 | 118.1 / 179.1 |
| XML | `temp/preview/coverage/fixtures/sample.xml` | Full viewer loader | 24.1 / 33.1 | 24.5 / 29.2 |
| DocBook | `test/input/docbook/showcase.xml` | Full viewer loader | 65.3 / 88.4 | 79.2 / 91.9 |
| SVG | `test/input/image/sample.svg` | Full viewer loader | 19.7 / 23.2 | 23.9 / 26.9 |
| PNG | `test/input/image/sample.png` | Full viewer loader | 19.6 / 20.8 | 23.1 / 27.3 |
| JPEG | `test/input/image/sample.jpg` | Full viewer loader | 19.0 / 21.0 | 23.0 / 24.9 |
| GIF | `test/input/image/sample.gif` | Full viewer loader | 20.6 / 52.2 | 23.6 / 27.1 |
| PSD | `test/input/image/layered.psd` | Full viewer loader | 34.4 / 40.4 | 38.6 / 55.3 |
| Mermaid | `test/input/simple_flowchart.mmd` | Full viewer loader | 50.7 / 57.3 | 53.6 / 59.6 |
| D2 | `test/input/simple_diagram.d2` | Full viewer loader | 47.6 / 54.2 | 50.8 / 54.0 |
| Graphviz DOT | `test/input/test_graph.dot` | Full viewer loader | 59.8 / 69.1 | 65.8 / 71.5 |
| Structurizr DSL | `test/lambda/graph/structurizr/metadata.dsl` | Full viewer loader | 46.9 / 54.5 | 50.1 / 57.1 |
| EPS | `test/input/eps/circle.eps` | Full viewer loader | 92.7 / 108.8 | 112.5 / 120.5 |
| Lambda Slides | `test/demo/slides/fixtures/relative.slides` | Full viewer loader | 40.0 / 56.6 | 65.9 / 80.4 |
| Unified diff / patch | `test/input/diff/review.patch` | Full viewer loader | 35.4 / 37.8 | 49.0 / 54.0 |
| XSL-FO | `test/html/paged_media_basic.fo` | First composed page | 24.8 / 29.6 | 28.1 / 31.7 |
| Radiant page | `test/html/paged_media_expressions.rpd` | First composed page | 23.7 / 25.3 | 26.8 / 28.6 |
| Plain text | `temp/preview/coverage/fixtures/text.txt` | Viewer source surface | 18.2 / 19.2 | 21.4 / 29.2 |
| JSON | `temp/preview/coverage/fixtures/sample.json` | Viewer source surface | 18.9 / 20.3 | 21.1 / 26.6 |
| YAML | `temp/preview/coverage/fixtures/sample.yaml` | Viewer source surface | 18.9 / 19.8 | 20.5 / 22.8 |
| TOML | `temp/preview/coverage/fixtures/sample.toml` | Viewer source surface | 18.7 / 19.3 | 20.6 / 21.9 |
| CSV | `temp/preview/coverage/fixtures/sample.csv` | Viewer source surface | 18.2 / 25.1 | 20.8 / 22.8 |
| INI | `temp/preview/coverage/fixtures/sample.ini` | Viewer source surface | 18.0 / 20.5 | 20.8 / 22.3 |
| CONF | `temp/preview/coverage/fixtures/sample.conf` | Viewer source surface | 18.6 / 20.0 | 20.9 / 22.5 |
| CFG | `temp/preview/coverage/fixtures/sample.cfg` | Viewer source surface | 18.0 / 18.5 | 21.4 / 24.5 |
| Log | `temp/preview/coverage/fixtures/sample.log` | Viewer source surface | 18.1 / 20.8 | 21.6 / 22.1 |

### Ordinary viewer measurements

These measurements use ordinary `lambda view --headless`. That command
executes script documents and opens directory and archive browser surfaces.
The `.ls` entry measures script evaluation. Preview now uses source view for
`.ls` and still declines directory and archive inputs.

| Input | Fixture | Headless view |
|-------|---------|---------------|
| Lambda script | `temp/preview/coverage/fixtures/document.ls` | 30.0 / 32.3 |
| Directory | `test/input/dir` | 336.0 / 411.1 |
| ZIP | `test/input/zip/viewer.zip` | 348.9 / 385.0 |
| TAR | `test/input/archive/ustar.tar` | 235.9 / 244.3 |
| 7z | `test/input/archive/7z-solid.7z` | 226.5 / 248.1 |
| RAR | `test/input/archive/legacy.rar` | 238.8 / 250.5 |

Raw samples, fixture hashes, executable hash and TSV are in `temp/preview/coverage/run-u0yi64xo/`.
The local harness is `temp/preview/coverage/bench.py`.

## 7. GIF thumbnail follow-up

The shared Radiant image loader used to inspect and decode every frame of
an animated GIF before thumbnail paint. Input preflight scanned the GIF
records, `DGifSlurp` expanded the indexed rasters, and the animation
decoder allocated composited canvases. The thumbnail session now skips
animation acquisition for GIFs, including GIF data URLs, and uses the
existing decoder that stops at the first frame. The animation loader also
uses its preflight frame count to skip raster decoding for single-frame
GIFs. View and snapshot sessions still select the animation loader for
playback (D9.1.1v4, D10.1.7).

The release follow-up used the same machine, warm-cache protocol and
128 × 128 output as §6. Each cohort has 20 fresh processes per fixture
after one excluded warmup. Values are p50 / p95 in milliseconds.

| Fixture | Before | After | Repeat |
|---------|-------:|------:|-------:|
| Repository GIF, 1 frame, 256 × 192 | 16.1 / 17.1 | 15.6 / 17.1 | 19.0 / 20.8 |
| Synthetic still, 1 frame, 512 × 384 | 17.2 / 17.8 | 16.5 / 16.9 | 20.0 / 22.2 |
| Synthetic animation, 160 frames, 512 × 384 | 134.0 / 146.3 | 16.0 / 16.2 | 20.1 / 22.3 |
| XHTML feature gallery | 120.0 / 133.9 | 152.4 / 192.1 | 132.9 / 148.7 |

The repository GIF's earlier 52.2 ms p95 did not recur in the unmodified
release binary. The animated GIF's new thumbnails matched its still first
frame pixel for pixel. The original animated thumbnail used a different
sampling path and differed in 296 output pixels. Both still fixtures were
pixel-identical before and after. The existing `svg_animated_images` view
test passed all six assertions for file and data GIF playback. A headless
snapshot of the 160-frame GIF completed. Live snapshot presentation and
cold-cache timings remain unmeasured.

XHTML still uses the full strict XML parser and namespace validation under
D10.1.4, then displays the first viewport. The HTML5 preview loader can
stop after a sufficient prefix. The XHTML gallery timings include parsing,
styling, layout and paint. Incremental XHTML would need a resumable strict
XML route that preserves the required namespace and well-formedness checks.

Raw samples, fixtures and the harness are under `temp/preview/gif/`.

## 8. Lambda source preview

The common preview loader dispatches local `.ls` files directly to the
existing text surface with scripting disabled. This gives the same escaped,
monospace presentation as `.txt` and avoids compilation and evaluation
(D9.1.1v4, D9.1.2v2). Source intake still reads the full file.

Warm-cache release measurements follow §6. Times are p50 / p95 in
milliseconds, with 20 fresh processes per fixture and mode after warmup.

| Fixture | Thumbnail, 128 × 128 | Headless snapshot |
|---------|---------------------:|------------------:|
| `temp/preview/coverage/fixtures/document.ls` (1 line) | 17.0 / 18.3 | 19.4 / 19.8 |
| `test/demo/word/word.ls` (560 lines) | 36.6 / 37.1 | 40.5 / 60.6 |

A valid `.ls` and a malformed `.LS` both produced thumbnails pixel-identical
to their `.txt` copies and completed headless snapshots. Ordinary `view`
opened the valid script and rejected the malformed script. A local file URL
produced the same thumbnail as its path. Evidence is under
`temp/preview/source/run-trlmoj92/`.

## 9. GLB model preview

Input reads the local GLB in one binary read capped at 72 MiB, allowing
8 MiB beyond the existing 64 MiB buffer limit. It reuses the existing GLB
2.0 scene conversion. Radiant wraps the imported values with a camera
and preview lighting. It fits the camera to the selected scene's native
posed bounds and keeps the framing stable during playback. This route
uses no Lambda evaluator. Dependencies use local files and data URLs
(D5.5.2, D9.1.1v4, D9.1.2v2, D11.1.2).

The thumbnail uses the authored pose and omits animation track decoding.
It renders at output density and caps the longest raster texture edge to the
longest output edge and the runtime texture limit. The snapshot retains
the first clip with tracks for the selected scene, plays
it through the native scheduler, and caps rendering at one device pixel per
CSS pixel. Texture validation and acquisition happen once at projection time
instead of pre-reading each texture during preview import (D10.1.7).

Offscreen previews create the scene's own graphics context directly.
Windowed snapshots use the viewer's shared graphics context for presentation.
Both paths require a published model frame before returning success. The native
Scene3D format and graphics limits apply (D9.2.1v2, D9.1.3v2).

Warm-cache release measurements on arm64 macOS follow §6. Each fixture
and mode uses 20 fresh processes after one excluded warmup. Values are
p50 / p95 in milliseconds. The first GLB build used the normal texture
decode and an extra graphics host window for thumbnails. The final build
uses output-sized embedded textures and a single offscreen graphics context.

| Fixture | First thumbnail | Final thumbnail, 128 × 128 | Final headless snapshot |
|---------|----------------:|----------------------------:|------------------------:|
| Model from the existing PPTX fixture, 1.6 KiB | 117.5 / 153.7 | 102.5 / 110.1 | 110.9 / 126.5 |
| Articulated model with skinning and animation, 4.5 KiB | 108.3 / 179.3 | 96.5 / 113.4 | 103.7 / 148.8 |
| Textured sphere, 2.0 MiB, about 74,000 input triangles | 145.7 / 152.0 | 136.4 / 143.3 | 147.9 / 152.9 |

Median snapshot document load was 0.3–9.2 ms. The first render, which
includes initializing native graphics and projecting the scene, took
81.0–114.3 ms. The 50 ms thumbnail target remains open for GLB. Sharing a
graphics context across repeated jobs would address the measured startup cost. The 2,048 ×
2,048 embedded texture in the dense fixture decodes to 256 × 256 for a
128-pixel thumbnail. That change reduced the decoded pixel allocation.
The separate run with the texture change alone did not show a repeatable
launch-to-PNG latency gain.

Checks covered exact output dimensions, identical thumbnails for the
animated model and its clip-free copy, skipping a later clip, malformed
and empty GLBs, and rejection of a remote buffer. The animated GLB
completed a windowed first-render smoke test. Scene3D's existing imported
asset replay passed all 47 assertions, including animation pose changes.
Snapshot benchmark times include headless teardown. Raw measurements and
fixtures are under `temp/preview/glb/`.

### 9.1 Output-sized textures and subpixel parts

The release thumbnail profile reduced the raster texture target from two
texels per output pixel to one. The existing scaled decoders preserve image
aspect ratio and apply to both embedded and local external raster textures.
At 128 pixels the maximum decoded image area is 128 × 128. The multisample
rendering and material shading of surviving geometry use the standard
Scene3D path (D9.1.1v4, D10.1.7).

Input records local bounds from the actual triangle vertices for this
profile, including each emitted chunk's vertex subset. Radiant fits to
the complete static scene and projects each part's bounds with that same
camera. A part whose box is under half a pixel on both axes may be skipped
before preparing its geometry or referenced textures. A detail resolved on
either axis stays eligible for rendering. Scenes with skinning, morphs or
point/line drawables use regular projection. Uncertain bounds and scenes
where every part qualifies keep the full set of draws. The retained parts
keep their original topology and the camera retains the full framing.

Paired release measurements on arm64 macOS alternate the two executables
over 20 measured fresh processes per fixture after warming both. All
thumbnails are 128 × 128. Times are p50 / p95 in milliseconds.

| Fixture | Before | After |
|---------|-------:|------:|
| Small static PPTX model | 112.0 / 186.8 | 114.0 / 124.6 |
| Articulated animated GLB | 107.6 / 145.4 | 106.1 / 151.8 |
| Dense textured sphere | 147.1 / 228.2 | 145.0 / 217.6 |
| Static scene with 135 tiny parts, about 87,000 triangles | 117.5 / 128.4 | 115.0 / 128.7 |
| Same composition with about 1.1 million triangles | 288.6 / 322.3 | 240.0 / 258.3 |

The small and articulated outputs are pixel-identical to the previous
build. The textured sphere differs by at most 2 of 255 per color channel.
The 128-pixel LOD fixture matches its control with the tiny parts removed,
including the visible accent and framing. At 512 pixels the detail is
visible. The embedded and external texture variants produce identical
128-pixel output. Native graphics startup still dominates lighter models.
The smallest median was 106.1 ms, above the thumbnail target. Evidence
is under `temp/preview/glb/lod-y9qx5q6i/` and `temp/preview/glb/lod-slj0kvlm/`.

### 9.2 Snapshot target and GLB color resolve

The earlier 255.1 ms animated GLB sample spent 227.5 ms in first render and
0.4 ms loading the model. That first-render phase covers both graphics
initialization and rendering. On a new 40-launch release run the animated
fixture reached 118.3 ms p95 and 186.7 ms maximum. A static GLB in the same
run reached 307.8 ms once, so the earlier spike does not isolate animation
as the source.

GLB previews without forward blending or postprocessing now tone-map the
HDR coverage samples and average their display-encoded values directly
into a single sRGB texture. Geometry still uses the same multisampled HDR
buffer. This avoids the
extra multisampled display color and depth buffers and their resolve blit.
The texture is shared with the native presentation or readback path
(D5.5.2, D9.1.3v2, D10.1.7).

Release snapshots were timed headlessly with a warm filesystem cache.
Paired runs alternated executables, with 40 measured launches per build
and fixture. Values are p50 / p95 / maximum in milliseconds.

| GLB fixture | Before | After |
|-------------|-------:|------:|
| Animated articulated model | 103.2 / 109.2 / 129.5 | 103.2 / 119.7 / 196.2 |
| Dense textured sphere | 152.8 / 162.3 / 184.5 | 151.9 / 167.9 / 170.6 |
| Small static PPTX model | 110.2 / 119.2 / 128.5 | 111.3 / 116.0 / 122.9 |

An additional paired run measured 80 animated launches per build. Across
the two paired runs, the animated fixture was 103.6 / 128.6 / 142.9 ms before
and 104.5 / 130.2 / 196.2 ms after. All 120 tuned launches finished within
250 ms. The direct output reduces framebuffer storage. The paired data
does not establish a latency gain or identify the cause of the earlier
one-off stall. Live presentation latency and cold startup remain unqualified.

Pixel comparisons at 128 and 768 pixels show a maximum difference of one
color value per channel for the two opaque fixtures. A blended material
control stays pixel-identical. The animated GLB completed a windowed first
render. Measurements are in `temp/preview/glb/snapshot-250/`.

## 10. Script-backed snapshot startup

The snapshot command selects strict T0 before the viewer starts. Bundled
transforms and their imports run in the AST interpreter, including later
viewer callbacks. This uses the existing interpreter policy for unsupported
code and prevents both whole-module fallback and background promotion
(D8.3.1v3, D8.3.2, D8.3.4). Parallel AST import prebuild remains in use
(D8.3.5). A plain `.ls` preview enters the source viewer and showed zero
Lambda compiler work. The sample source file took about 0.5 ms to load and
11 ms to lay out.

### 10.1 Startup profile

The following are medians of five profiled, warm, fresh macOS arm64 release
processes per fixture. The left columns are summed Lambda front-end phase
times under the original packages with strict interpretation. AST work
includes building and resolving the tree, binding, validation and indexing.
The package and export columns are elapsed time. Package startup includes
waiting for import prebuild and initializing the module graph. The transform
column includes interpreter execution and the native format work it invokes,
such as ZIP/XML intake or TeX expansion. Parallel phase totals can exceed
the package's elapsed time. Import discovery is outside the parse column.

| Fixture | Module records | Parse ms | AST ms | T0 plan ms | Package startup ms | Transform export ms |
|---------|---------------:|---------:|-------:|-----------:|-------------------:|--------------------:|
| DOCX, explicit break | 135 | 26.1 | 144.2 | 7.9 | 57.1 | 6.1 |
| DOCX, 6,000 paragraphs | 135 | 25.4 | 139.5 | 8.1 | 57.0 | 23.7 |
| PPTX, first slide | 195 | 29.5 | 158.8 | 9.0 | 58.8 | 12.6 |
| LaTeX, two pages | 166 | 32.6 | 140.5 | 9.5 | 89.5 | 1.5 |
| TikZ | 84 | 18.4 | 79.5 | 5.3 | 63.2 | 31.2 |
| EPS | 19 | 2.2 | 13.3 | 0.6 | 12.9 | 19.0 |
| EPUB | 66 | 5.6 | 19.4 | 1.5 | 9.8 | 1.8 |

AST work dominated the Lambda front end for the three Office and TeX
fixtures. Building and resolving the AST accounted for about 89–109 ms of
the summed module phases. Disabling prebuild increased unprofiled median
elapsed time by 49.2 ms for long DOCX, 52.9 ms for PPTX and 61.1 ms for
LaTeX in a separate 20-run tier comparison on the original packages.
The EPUB spent about 41 ms in Radiant layout, compared with about 10 ms
in package startup.

AUTO showed zero eager whole-module MIR in these samples. It started
background compilation anyway. The median was 30 jobs for long DOCX,
39 for PPTX, 10 for LaTeX, 12 for TikZ, 21 for EPS and 26 for EPUB. The
summed elapsed time inside those overlapping jobs ranged from about
191 to 360 ms for these six rows. Strict T0 started zero such jobs.
These sums describe work on the compiler threads and do not quantify
the user's wait. The end-to-end tier comparison in §10.2 measures that
effect. The temporary probes recorded worker and export durations under
`temp/preview/script-tiers/phase-icv42lzl/`.

### 10.2 Release changes and remaining work

- Word viewing imports the reader without the exporter and consumes the
  element tree without serializing HTML or plain text. The reader and
  public conversion API share option validation, model building and
  diagnostics (D9.3.4). The profiler recorded 121 Word modules after
  the change, down from 135.
- PowerPoint viewing passes the imported model directly to the retained
  slide player. The public conversion API still generates its handout.
  LaTeX, TikZ and Slides take the path helpers directly from the I/O
  package. LaTeX and TikZ each prepared six fewer modules after that change
  (D9.3.4, D10.2.2).
- Verification passed for 93 DOCX, PPTX and LaTeX goldens in both AUTO and
  strict T0. Nine 256 × 256 preview thumbnails matched the previous output
  byte for byte. With `LAMBDA_EXEC_BACKEND=jit` set, the snapshot command
  still ran the DOCX, PPTX, LaTeX and TikZ transforms in T0. An ordinary
  script view honored the eager JIT setting. DOCX, PPTX and LaTeX also
  rendered through the native window path. See
  `temp/preview/script-tiers/check-f1x2tyxa/`.
- The next runtime candidate is reducing the import graph that first-page
  transforms prepare. Profile the largest resolver and binder paths and
  measure import discovery separately. The current prebuild discovers imports
  through a full parser pass before the compiler parses each module to build
  its AST. Reuse a verified source-identity-bound result where the existing
  import resolver and cache permit it (D8.3.5, D8.5.1, D9.3.3).
- For large Word documents the transform still inflates and parses main XML
  before selecting the first-page prefix. An incremental OPC body reader
  targets the remaining size-dependent cost. Large standalone `.ls` source
  previews have a separate opportunity to bound text reads and layout.

The paired comparison used 20 measured fresh release processes per fixture
and configuration after one warmup. The configurations ran in shuffled
order on macOS 26.3 arm64. Times are launch through headless first render
and teardown, in p50 / p95 milliseconds. The middle column isolates the
tier change by running the original executable and package tree with
`LAMBDA_EXEC_BACKEND=interp`. The final column uses the new executable and
packages with the default environment tier. Native window tests checked
first-frame success separately.

| Fixture | Original AUTO | Strict T0 only | Final snapshot |
|---------|--------------:|---------------:|---------------:|
| DOCX, explicit break | 101.8 / 104.3 | 99.6 / 103.0 | 94.4 / 174.0 |
| DOCX, 6,000 paragraphs | 127.6 / 137.2 | 124.8 / 139.8 | 119.2 / 123.7 |
| PPTX, first slide | 118.9 / 127.3 | 115.5 / 117.3 | 111.9 / 113.4 |
| LaTeX, two pages | 116.8 / 119.1 | 112.9 / 125.5 | 109.5 / 125.6 |
| TikZ | 124.3 / 152.2 | 116.6 / 120.2 | 114.8 / 116.1 |
| EPS | 92.9 / 109.6 | 55.4 / 55.9 | 55.4 / 59.0 |
| EPUB | 95.8 / 112.3 | 89.8 / 90.3 | 89.4 / 90.8 |
| Fixed EPUB | 88.1 / 91.0 | 83.5 / 85.2 | 82.7 / 84.5 |
| DocBook | 65.1 / 69.9 | 59.4 / 61.3 | 59.1 / 59.5 |
| PDF, 250 pages | 37.3 / 41.9 | 36.6 / 41.5 | 36.6 / 40.4 |
| PSD | 35.7 / 39.7 | 35.6 / 39.6 | 35.3 / 37.6 |
| Mermaid | 52.7 / 72.1 | 50.2 / 54.8 | 50.2 / 54.4 |
| D2 | 49.6 / 53.0 | 47.4 / 49.6 | 48.4 / 66.5 |
| Graphviz DOT | 62.9 / 67.6 | 55.2 / 74.7 | 55.0 / 61.1 |
| Structurizr DSL | 49.2 / 53.6 | 47.9 / 52.8 | 47.3 / 51.8 |
| Lambda Slides | 63.0 / 66.2 | 56.7 / 62.2 | 55.4 / 56.5 |
| Unified diff | 48.6 / 49.7 | 47.0 / 57.0 | 47.0 / 68.3 |
| Lambda source | 21.9 / 26.4 | 22.0 / 23.0 | 22.0 / 22.5 |

All 360 final runs completed below 250 ms. The slowest was 186.4 ms on the
explicit-break DOCX. Several fixture p95 values increased despite lower
medians. These samples leave cold startup and live-window latency
unqualified. Raw launch times are under
`temp/preview/script-tiers/run-1n8tjv_k/`. The prebuild on/off comparison
is under `temp/preview/script-tiers/run-knfx82wh/`.
