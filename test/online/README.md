# Radiant online page inventory

`pages.csv` is the shared URL list used by `test_radiant_online_view_gtest` and
the snapshot capture script. Its columns are the case label, page URL, and
whether the online-view smoke test expects linked resources.
The online GTest discovers one `Loads_<label>` test per catalog row, so labels
stay attached to their URLs when rows are removed or reordered. Generated
labels can have gaps. For example, filter one case with
`--gtest_filter=RadiantOnlineViewTest.Loads_lua_pil`.

On 2026-11-12, 94 of the 95 unresolved URLs were removed using the existing
Chromium headless-shell failures, leaving 1,067 pages. PIL remains because
Chromium loads it. The removal audit and previous results are preserved under
`snapshots/removed_pages-20261112.json` and
`snapshots/removed_capture_results-20261112.csv`.

From the repository root, run:

```sh
node test/online/capture_snapshots.cjs
node test/online/capture_snapshots.cjs --failed-only
node test/online/capture_snapshots.cjs --failed-only --renderer=lambda --labels=arduino_docs
```

The script saves matching 1200 × 800 Lambda and Chromium PNGs under
`test/online/snapshots/`. It also writes `capture_results.csv` there. Set
`CHROME_HEADLESS_SHELL` to select a specific Puppeteer headless shell, or set
`RADIANT_ONLINE_CAPTURE_JOBS` to change the default four concurrent page cases.
`RADIANT_ONLINE_CAPTURE_TIMEOUT_MS` and `RADIANT_ONLINE_RENDER_TIMEOUT_MS`
override browser navigation and Lambda render timeouts.
Each attempt records its selected timeout in `capture_results.csv`; older
attempts can have an empty timeout field. Complex pages can complete after the
default parent deadline because native layout does not count toward the script
CPU watchdog. Use the render-timeout override for a longer bounded capture.

Retries run only the failed or missing renderer side and preserve successful
images and the other manifest rows. Results are saved after each renderer
attempt. `--renderer=browser` retries only Chromium; `--labels=a,b` limits the
page cases. HTTP rejections, TLS failures, crashes and timeouts remain failures.

Lambda keeps per-page `-lambda.log`, `-lambda-output.log` and timing reports in
the snapshots directory. Its parent deadline follows the engine's maximum
script watchdog plus the online launcher's cleanup grace. The override above
can select a different wall-time budget. A successful process must also report
document-load completion and write a complete PNG before the new image is
promoted to `-lambda.png`. Failed images use `.failed.png`; they are diagnostic
artifacts. Chromium captures after the final main document loads, including
script redirects, without waiting for recurring analytics requests to stop.
Each Chromium attempt replaces its `-browser.json` diagnostic, referenced by the
manifest's `browser_log` column. It records document responses, selected server
headers, load events, failed/pending resources and a bounded visible-text
excerpt. Cookie and authorization headers are omitted. This distinguishes an
origin challenge or rejection from a page waiting on a resource.

Focused capture checks use a local HTTP server and Puppeteer's headless shell:

```sh
node --test test/online/capture_snapshots.test.cjs
```

Compare every available image pair and create a ranked review report:

```sh
node test/online/compare_snapshots.cjs
node --test test/online/compare_snapshots.test.cjs
```

The comparison writes the report and its JSON/CSV data to
`test/online/comparison/`. Rows are ordered by severity: capture or image
problems first, then automated major differences and omissions retained in
visual review, followed by pages below the major triage threshold. The report
uses `snapshots/visual_comparison_review_round2.json` for manually retained
major omissions.
It uses `pngjs` from the installed render-test corpus (or the root dependencies).
The report averages images into 120 × 80 color cells to rank broad differences
without fine glyph noise. A mean channel error of at least 12% across at least
35% changed cells marks a review candidate. A nearly uniform image against
content also qualifies at 2% mean error and 12% changed cells. These thresholds guide inspection;
they do not certify fidelity. Missing captures, dimensions and capture dates
remain explicit. Live content, theme state, animations and consent dialogs can
produce large differences that need diagnosis before any engine change.

The 2026-11-12 review compared all 1,066 available pairs (PIL still has no
Lambda capture), refreshed 250 Lambda images, and leaves 204 major review
candidates. It repaired initially disabled linked sheets and grid declarations
that prevented actual flex items from being sized (Radiant RI-020/RI-021).
Downloaded-sheet media selection improved, with remaining behavior tracked as
RI-022. Missing media regions remain RI-023; application script admission and
full shadow-tree rendering remain Lambda JS13-L1/JS13-L2. The per-page audit is
in `snapshots/visual_comparison_review.json`; the final HTML/JSON/CSV report is
in `temp/online-visual/final/`. Aggregate score changes include live-content and
capture changes; the confirmed repairs have separate pristine-source controls.
Native checks pass 74 cases and the online tools pass 14 cases. The Radiant
integration gate has 8,163 passes, 299 partials, 20 skips and one existing
forced-GC iframe failure, reproduced with current unmodified Radiant sources
and recorded as Lambda JS03-L2.

The second review inspected all 204 remaining candidates and refreshed all 204
Lambda captures successfully, with another refresh of the 63 video-containing
cases after the poster-sizing repair. It fixed multi-token stylesheet relations
(RI-024), live stylesheet media/disabled state (RI-022), dropped root theme
mutations (JS13-L3), remote image/poster discovery and video intrinsic sizing
(RI-025), transparent background resets (RI-026), and desktop viewport selection
(RI-027). The final comparison still covers all 1,066 pairs, with no dimension
mismatches or changes to Chromium references, URLs, deadlines or thresholds.

Automated major candidates fall from 204 to 186. Visual inspection retains five
additional cases whose low scores hide major omissions: Supabase customers,
Pitch, Cofounder, Banky and CMake. The audited review queue therefore has 191
remaining cases; 13 clear this round's major review. These are review candidates,
not 191 confirmed engine defects. The per-page evidence and explicit unknown
causes are in `snapshots/visual_comparison_review_round2.json`; the latest full
report is `comparison/index.html`, with its sorted data in `comparison/results.json`
and `comparison/results.csv`. Before/after examples remain in
`temp/online-visual-round2/repairs.html`. Investigation coverage includes 204
authored documents, 203 successful browser diagnostics plus one navigation
context error, render logs for all 204 and 23 focused native layout summaries.

The 118 focused native/host checks and 14 online-tool checks pass. The final
Radiant gate retains 8,163 passes, 299 partials, 20 skips and the existing
JS03-L2 iframe failure. All 56 CSS images baseline cases pass, including video
size containment. Remaining application execution, selected-frame media and
composed Shadow DOM limitations stay explicit in the owning ledgers.

`test_page_load_gtest` writes its local fixture timing and memory measurements
to `page_load_metrics.csv` in this directory.
