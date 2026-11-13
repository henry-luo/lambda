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
node --test test/online/render_session.test.cjs
node --test test/online/render_stylesheet.test.cjs
node --test test/online/render_clip.test.cjs
node --test test/online/render_observers.test.cjs
node --test test/online/render_grid.test.cjs
node --test test/online/render_rem.test.cjs
node --test test/online/render_grid_absolute.test.cjs
node --test test/online/render_grid_nested.test.cjs
```

The session tests exercise real Lambda HTTP rendering, layout and both batch
paths against a local server, including response cookies, HttpOnly visibility,
redirects, subresource requests and profile lifetime. `LAMBDA_RENDER_EXE` selects
the renderer executable. Socket access and native rendering services are required.
The stylesheet tests check real exported PNG pixels after CSSOM rule edits and
style text replacement; they share the same executable selection.
The clipping tests use the shared `clip_fixtures.cjs` inputs to check real PNG
pixels after variable/math resolution, font sizing, zoom and empty clipping.
The observer tests check initial intersection/resize delivery and rendered
pixels without page geometry reads, including offscreen and disconnect controls.
The grid tests use `grid_fixtures.cjs` to check intrinsic percentage contributions,
solved area sizing, owner units and authored constraints. These four PNG suites
share `render_fixture.cjs` for exports and image reads.

Compare every available image pair and create a ranked review report:

```sh
node test/online/compare_snapshots.cjs
node --test test/online/compare_snapshots.test.cjs
```

The comparison writes the report and its JSON/CSV data to
`test/online/comparison/`. Rows are ordered by severity: capture or image
problems first, then automated major differences and omissions retained in
visual review, followed by pages below the major triage threshold. The report
uses the tracked `review_annotations.json` for manually retained major
omissions; these annotations do not change the image metrics or thresholds.
It uses `pngjs` from the installed render-test corpus (or the root dependencies).
The report averages images into 120 × 80 color cells to rank broad differences
without fine glyph noise. A mean channel error of at least 12% across at least
35% changed cells marks a review candidate. A nearly uniform image against
content also qualifies at 2% mean error and 12% changed cells. These thresholds guide inspection;
they do not certify fidelity. Missing captures, dimensions and capture dates
remain explicit. Live content, theme state, animations and consent dialogs can
produce large differences that need diagnosis before any engine change.

The 2026-11-12 review compared all 1,066 pairs then available (PIL had no
Lambda capture), refreshed 250 Lambda images, and left 204 major review
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
(RI-027). That comparison covered all 1,066 available pairs, with no dimension
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

A later PIL retry captured the Lambda image successfully. That checkpoint
compared all 1,067 pairs: 191 high-severity review candidates and no unavailable
captures. PIL's connection remains intermittent; see LI-014 for the investigation.

The 118 focused native/host checks and 14 online-tool checks pass. The final
Radiant gate retains 8,163 passes, 299 partials, 20 skips and the existing
JS03-L2 iframe failure. All 56 CSS images baseline cases pass, including video
size containment. Remaining application execution, selected-frame media and
composed Shadow DOM limitations stay explicit in the owning ledgers.

The third review revisited those 191 candidates and repaired three shared
implementation bugs: missing Oklab/OkLCh components were rejected (LI-017),
auto-height flex columns shrank natural content to a minimum height (RI-028),
and content-driven grid row growth left stale scroll clips (RI-030). See
[before/after examples](comparison/repairs.html). Nuxt home changes from 88.4%
to 5.9% mean error, Nuxt docs from 85.2% to 4.4%, Nightwatch from 37.5% to
8.4%, and ImageMagick from 43.3% to 14.5%; these are coarse triage scores.

All 191 Lambda captures were attempted; 190 succeeded. After the grid repair,
142 cases using that code path were refreshed again. Dock timed out at the
existing 150-second deadline on both attempts and on the previous qualified
renderer control (RI-031). The latest report therefore covers 1,066 pairs,
with one capture blocker, 177 automated major candidates and nine manually
retained major omissions: 186 high-severity pages in total. Visual review of
every threshold-cleared case retained Popcorn, Cequence, Missive and Nightwatch alongside
the previous five omissions. Four of the original 191 clear this major review;
Dock remains unresolved as a capture blocker. The current per-page audit is
`snapshots/visual_comparison_review_round3.json`. Observed script admission,
exceptions and resource errors are evidence, not automatically assigned causes.

All 112 CSS parser tests, 70 native dynamic-layout tests, five comparison-tool
tests and 3,851 recorded static layout cases pass (six layout skips); layout
integer-cast lint also passes. The current Lambda checkout build is separately
blocked by its paint pigment API mismatch with the Radiant checkout. Validation
and captures use an isolated compatible host built from Lambda `f19167dbf` plus
online commit `3d1b6230d`, current sibling sources, and these repairs. Host
hashes and the build failure are retained in `temp/online-visual-round3/`.
DigitalOcean's full content omission and modal top-layer/backdrop painting
(RI-029) remain explicit; the reduced grid repair does not establish their
complete resolution.

The fourth review repaired three more shared Radiant defects: flex replaced
items ignored their natural aspect ratio after main sizing (RI-032), propagated
body backgrounds repainted over root negative stacking layers (RI-033), and
intrinsic positioned layout leaked its available-space mode to following blocks
(RI-034). Opus's announcement now stays 32.8px tall in the full static control,
and Vogelhaus's sky and centered heading are restored. See the
[round-four examples](comparison/repairs.html#round-4).

All 186 high-severity Lambda images were refreshed successfully; a Dock retry
still exceeded the unchanged 150-second deadline. The current report has 1,066
pairs, 182 high-severity pages (172 automated candidates and ten manually
retained differences), one critical capture blocker and 884 low-severity pages.
Visual review covered all newly threshold-cleared pages, the retained manual
cases and other large score changes: 19 pages in total. Kotlin's loading spinner
and Craft's compressed hero remain major differences. CMake's stale poster
annotation was removed because the fresh image now includes it. Vogelhaus,
CMake, Mural and Designmodo clear this review; live changes and readiness can
contribute, so those outcomes are not all attributed to one isolated repair.
Opus's main content is restored, but its different selected video frame still
reaches the broad-difference threshold. The current evidence audit is
`snapshots/visual_comparison_review_round4.json`.

Qualification includes 73 passing dynamic-layout tests, 917 passing native
checks, 3,851 static and 630 JavaScript layout cases, the unchanged canvas
integration fixture's six assertions, 12 canvas-source mutation assertions,
three local SVG raster controls, five
comparison-tool tests and layout integer-cast lint. Six layout cases and one
live MIDI case are skipped. The native gate retains one unavailable-voice
provider failure, also reproduced in three isolated runs (RI-035); speech
sources and assertions were not changed. The Lambda main checkout still has
the independent paint API mismatch described above. Captures use the compatible
isolated host with the three repairs, pinned as
`temp/online-visual-round4/lambda-final.exe`; its SHA-256, full logs, controls
and before images remain in that directory. Browser references, URLs,
thresholds, admission limits and capture deadlines are unchanged.

The fifth review repaired four shared Radiant defects: column intrinsic heights
omitted main-axis gaps (RI-036), authored display bypassed scripting-enabled
noscript suppression (RI-037), ratio-derived intrinsic heights ignored inline
min/max constraints (RI-038), and recursive height queries borrowed an outer
percentage-height basis through auto ancestors (RI-039). The existing headless
service-worker API also now exposes a stable pending ready promise, removing
Greenlist's isolated startup TypeError (JS13-L4). Worker execution and the
registration lifecycle remain a major feature gap (JS13-L5); that repair does
not establish the cause of Greenlist's remaining missing content.

All 182 original high-severity Lambda images were refreshed successfully with
the final qualified host; Dock again exceeded the unchanged 150-second deadline.
The current comparison covers all 1066 available pairs: **180 high-severity
pages**, one critical capture blocker and 886 low-severity pages. It contains
168 automated candidates and 12 manually retained major omissions. Visual
review covered 16 changed/manual pairs and two fresh previous-host controls.
Marqeta and Path clear this major review. Maven and Empathy stay high because
their hero text and main portrait, respectively, remain absent despite clearing
the automated changed-area threshold. These outcomes include live/capture
state; they are not all attributed to one repair without an isolated control.

Craft's outer hero now measures 736px instead of 540px in the full frozen
control, but nested percentage-height content still obscures its heading and
product image. A fixed-height control reproduces that separate defect on the
previous renderer (RI-040), so Craft remains high severity. Its annotation and
Missive's remaining quotation omission were updated to match the fresh images.
RFC Editor's Error 500/TypeError page and PostHog's missing application window
also reproduce with the previous renderer; their geometry changes and remaining
causes are recorded explicitly. See [round-five examples](comparison/repairs.html#round-5)
and `snapshots/visual_comparison_review_round5.json` for per-page evidence.

Qualification includes 77 native dynamic-layout tests, 3851 static and 630
JavaScript layout cases, 11 assertions in two new DOM integration fixtures,
five comparison-tool tests and layout integer-cast lint. Six static layout
cases are skipped. Both new DOM fixtures fail with the previous qualified host.
The main-checkout paint API mismatch remains independent; captures use the
compatible isolated host with the exact current service-worker source change
and current sibling sources, pinned as
`temp/online-visual-round5/lambda-fivefix.exe` (SHA-256
`6993045526db37b5a4b9b1b4ce504ede4e9c6eaafa582105ac4d52572425c0a4`).
Source patches, logs, controls and before images remain in that directory.
Browser references, catalog rows, thresholds, admission limits and deadlines
are unchanged. The goal remains active for the unresolved high-severity work.

The sixth review repaired three shared implementation defects. An inactive
maximum no longer erases an explicit definite flex main size (RI-040), restoring
Craft's heading and product placement. Length-valued CSS math gaps now reach
flex, grid and multicolumn layout through the existing unit/math resolver
(RI-042). Craft's frozen heading/action wrapper has its authored 40px gap
instead of zero. Percentage-containing math gaps remain incomplete; pure
percentage handling is preserved. The AST interpreter now short-circuits
optional deletion (JS04-L1), so Vue's delete i.props?.ref no longer throws and
RFC Editor renders its homepage instead of Error 500.
Runtime parity follows D8.4.4; native/host ownership remains D9.1.1v4 / D9.1.3v2.

All 180 remaining high-severity captures, Dock and four recently cleared
controls were refreshed: 184 succeeded; Dock again exceeded the unchanged
150-second deadline (RI-031). The latest comparison covers all 1066 available
pairs: **179 high-severity pages**, one critical capture blocker and
887 low-severity pages. It contains 166 automated candidates and
13 manually retained major omissions. Visual review covered 16
changed, newly threshold-cleared or manually retained pairs; the four recent
controls were also checked. RFC Editor clears major review. Maven Central is
retained manually because its main search field is still absent and its promo
panel is compressed despite clearing the automated area threshold. Craft remains
high because URL-based image masks lack a paint consumer (RI-041); the large
paper shapes still differ. No decoration is substituted or hidden. Live score
changes alone are not assigned to a source repair without isolated evidence.
See [round-six examples](comparison/repairs.html#round-6) and
snapshots/visual_comparison_review_round6.json for the current evidence.

Qualification includes 79 native dynamic-layout tests, 3851 static and 630
JavaScript layout cases, 574 AST baseline tests, 14 MIR optional/deletion/super
neighbors, fifteen Node/AST/MIR deletion checks, sixteen independent Chromium
gap coordinates, two DOM integration fixtures with eleven assertions, five
comparison-tool tests and layout integer-cast lint. Six static layout cases
are skipped. The prior unavailable-voice native limitation remains RI-035;
the aggregate native gate was not repeated. Browser references, catalog rows,
image thresholds, admission limits and capture deadlines are unchanged.

The independent main-checkout paint API mismatch persists. Captures use the
compatible isolated host with exact current repaired JS interpreter/DOM sources
and current sibling archives, pinned as temp/online-visual-round6/lambda-final.exe
(SHA-256 60f757cadefdbc438344b5826696c6b1536e5c81f4cd777ff2d531a9529b1c60).
Source-state hashes/patches, reduced controls, logs and original images remain
in that directory. The goal stays active for the unresolved high-severity work.

The seventh review repaired two shared intrinsic-width defects in Radiant
(D9.1.1v4 / D9.1.3v2; D5.5.1). The width/max-width classification now resolves
custom properties before deciding that a percentage is cyclic (RI-043), so a
definite variable-backed maximum constrains the text column. Intrinsic image
sizing also preserves author CSS over HTML width hints when automatic or cyclic
percentage widths use natural dimensions (RI-044). Bare hints and definite CSS
sizes retain their existing behavior. The frozen Maven Central promo now has
419px/509px text/image columns and 327px height, matching Chromium's
418.97px/509.03px and 327.02px. Its live promo scale is restored, but the
client-created search input remains absent, so Maven Central stays high.

All 179 high-severity cases, Dock and five recently cleared controls were
attempted with the final renderer. The current comparison has 1066
available pairs, **179 high-severity pages**, 1 critical capture blocker
and 887 low-severity pages (166 automated candidates and
13 manually retained omissions). Visual review covered 15
changed/manual/newly threshold-cleared pairs and five recent controls.
No additional page clears the major queue this round: Maven's promo is restored,
while its search omission remains; Lokalise and Affinity also improve but stay high.
The per-page evidence is snapshots/visual_comparison_review_round7.json;
see [round-seven examples](comparison/repairs.html#round-7). Live-content and
readiness changes are not assigned to one repair from scores alone.

All 81 native dynamic tests, 3851 recorded static and 630 JavaScript layout
cases, both DOM integration fixtures (eleven assertions), five comparison-tool
tests and layout integer-cast lint pass. Six static cases are skipped. Chromium
independently verifies variable/literal constraints, the cyclic zero-percentage
neighbor and six image sizing modes before/after hint mutation. The separate
unavailable-voice native limitation remains RI-035; the aggregate native gate
was not repeated. Browser references, URLs, thresholds, script admission limits
and parent deadlines are unchanged. Dock still times out at150s (RI-031).

The main-checkout paint API mismatch remains independent. Captures use the
compatible isolated host with current sibling archives and the exact current
JS interpreter/DOM repair sources, pinned as temp/online-visual-round7/lambda-final.exe
(SHA-256 76e79482164f6192fee0b148cfa929e56b4d8cd129730ef36518b369bca67c9c).
The before images, source-state patches, reduced controls, qualification logs
and capture audit remain in that directory. The broader goal stays active.

The eighth review repaired the missing browsing profile in headless document
hosts (RI-045). Rendering and layout now use an ephemeral native session when
the caller supplies none; shared batch jobs retain it until UI cleanup, while
borrowed window profiles retain their existing owner. This restores response,
document and subresource cookie continuity. Enabling the profile exposed two
input transport bugs, also fixed: raw HTTP line terminators corrupted terminal
Path/HttpOnly attributes (LI-018), and URL ports incorrectly became part of
cookie domains (LI-019). File URLs retain their prior empty-host behavior.
Ownership follows D9.1.1v4 / D9.1.3v2, headless parity D9.2.1v2 and snapshot
policy D10.1.7. No setter exception is suppressed or synthetic cookie result used.

All 179 previous high-severity pages, Dock and five recent controls were
attempted: 184 valid PNGs and Dock's unchanged 150-second timeout (RI-031).
The updated report covers all 1066 available pairs: **178 high-severity pages**,
one critical capture blocker and 888 low-severity pages, with 165 automated
candidates and 13 manually retained omissions. All 26 previously observed
cookie-session exceptions disappear. Fifteen changed, newly threshold-cleared
or manually retained pairs and five controls were visually reviewed. Storytale
now shows its hero, illustrations and search and clears major review; its
11.5% to 1.7% live score change is not assigned to these fixes without an
isolated control. Vooma's large consent panel disappears, but its hero text
remains absent and it stays high. See [round-eight evidence](comparison/repairs.html#round-8)
and snapshots/visual_comparison_review_round8.json. Browser images and metadata,
URLs, thresholds, script admission limits and capture deadlines are unchanged.

All 83 focused native dynamic/session tests, 3851 recorded static layout cases,
630 JavaScript layout cases, 574 AST baseline tests, ten local native HTTP
cases and six real CLI session cases pass. Six static cases are skipped.
The two DOM fixtures (eleven assertions), five comparator tests and layout
integer-cast lint also pass. The Radiant native aggregate has 927 passes, one
skip and the existing unavailable-voice failure RI-035. The input aggregate has
5224 passes, one skip and the same three known failures LI-001/LI-002/LI-013.
A separate restricted-sandbox media failure reproduces on old and new renderers;
both pass with native macOS services, as does the final full AST gate. No media
implementation or test expectation changed.

The independent main-checkout paint API mismatch persists. The compatible
isolated host uses the exact current repaired JS interpreter/DOM sources and
current sibling archives, pinned as temp/online-visual-round8/lambda-final.exe
(SHA-256 ff06c819f1cfa2a8e3bc1d1a7e20ae6e2bc9e6f153e9f10b8b1b2f5cbc8ea209).
Source-state patches, reduced regressions, controls, logs and before images
remain in that directory. The goal stays active for the remaining major causes.

The ninth review repaired dynamic stylesheet synchronization (RI-046;
D9.1.1v4 / D9.1.3v2). After a script connected a style node and inserted CSSOM
rules, the final rescan reparsed its unchanged text and discarded those rules.
Pending text changes are now consumed once, before exposing the inline sheet
to CSSOM. Later text replacement still replaces the sheet. The native helper
is shared with the Lambda adapter; no rules are serialized back into DOM text,
site-specific styles applied or virtual time advanced (D10.1.7).

All 178 high-severity pages, Dock and six recent controls were attempted:
184 valid PNGs and Dock's unchanged 150-second timeout. The latest report has
1066 available pairs, **176 high-severity pages**, one critical capture blocker
and 890 low-severity pages (162 automated candidates and 14 manually retained
omissions). Sixteen changed, newly cleared or manually retained pairs and all
six controls were visually reviewed. Northstar home and about now restore
their main layout and content, clearing major review at 8.7% and 6.1% mean error,
versus 64.8% and 70.8%. Greenlist's palette/header improve, but its hero remains
absent, so it stays manually high despite its 5.6% score. These live measurements
also include consent/readiness differences; reduced regressions independently
establish the engine repair. See [round-nine examples](comparison/repairs.html#round-9)
and snapshots/visual_comparison_review_round9.json.

Four real PNG stylesheet regressions, six HTTP/session regressions, twelve
native input-value tests, 574 AST baseline tests, 3851 recorded static layout
cases and 630 JavaScript layout cases pass. Six static cases are skipped.
The new window/event fixture fails three of four assertions on the previous
renderer and passes all four after the repair; the three recent DOM fixtures
pass fifteen assertions. Five comparator tests and layout integer-cast lint
also pass. The native aggregate has 928 passes, one skip and the existing
RI-035 unavailable-voice failure. Input qualification remains the round-eight
result above; no input implementation changed in round nine. Initial AST tests
from the compatible worktree lacked current fixture/module links; the corrected
environment passes all 574 tests without crypto/media source or golden changes.

Browser PNG hashes and metadata, catalog URLs, comparison thresholds, script
admission limits and capture deadlines remain unchanged. The independent
main-checkout paint API build mismatch persists. The compatible isolated host
uses the exact current repaired JS interpreter/DOM/CSSOM sources and current
sibling archives, pinned as temp/online-visual-round9/lambda-final.exe
(SHA-256 044ca5945f52565491a82c67ad85437473eb8489ad981f4bc51b69f75875f71b).
Controls, source-state patches, before images and qualification logs remain in
that directory. The goal remains active for unresolved high-severity causes.

The tenth review repaired basic-shape clip computation (RI-047;
D9.1.1v4 / D9.1.3v2). Raw numeric parsing discarded variables and math, so Hive
Streaming's closed fixed menu covered its hero. Shapes now use the shared CSS
value/unit resolver with an explicit percentage reference. Inset shorthand
expands before resolving each physical axis; circles use their normalized
diagonal. The shared position parser handles centers, and zoom scales the
resolved geometry. Temporary substituted values do not escape their owner
(D5.5.1); capture leaves virtual time unchanged (D10.1.7).

All 176 high-severity pages, Dock and eight recent controls were refreshed:
184 valid PNGs and Dock's unchanged 150-second timeout. The latest comparison
has 1066 available pairs, **175 high-severity pages**, one critical capture
blocker and 891 low-severity pages (161 automated candidates and 14 manually
retained omissions). Fifteen changed/newly cleared/manual pairs and all eight
controls were visually checked. Hive Streaming restores its main hero and
actions, clearing major review at 1.6% mean error versus 59.7%. All fourteen
manual omissions remain high. Browser PNG hashes and metadata, catalog URLs,
thresholds, script admission limits and capture deadlines are unchanged.

The root cause has an isolated full-page control: the frozen original and a
variant with only an equivalent literal clip differ before the repair; the
repaired original matches that literal control exactly in pixels. The broader
live score can also reflect readiness/content differences. See
[round-ten examples](comparison/repairs.html#round-10),
snapshots/visual_comparison_review_round10.json and
temp/online-visual-round10/hive-frozen-comparison.json.

All eighteen new PNG cases pass, versus fifteen failures with the previous
renderer. Independent Chromium verifies all eighteen expected pixel sets;
eighteen SVG exports rasterized in Chromium pass the same checks. The new live
window/event fixture fails four of six pixel assertions before and passes all
six after variable changes and resizing. All four recent DOM fixtures pass
twenty-one assertions. Qualification also passes 83 focused native cases,
574 AST baseline tests, 3851 recorded static layout cases (six skips), 630 JS
layout cases, 28 real CLI regressions, eight existing clip-render corpus cases,
five comparator tests and geometry lint. The native aggregate retains 928
passes, one skip and the known RI-035 unavailable-voice failure. Input source
is unchanged; its round-eight qualification remains applicable.

Full inset corner/slash rounding, polygon fill rules, URL clips, geometry boxes
and full shape() grammar remain incomplete; the support matrix records them.
The independent main-checkout paint API build mismatch persists. Captures use
the compatible isolated host with exact current repaired JS interpreter/DOM/CSSOM
sources and current sibling archives, pinned as
temp/online-visual-round10/lambda-final.exe (SHA-256
bbea00e3ff61c45ba59b981ee38781c0bdb46c700d4fcb1d351cdda770965fb5).
Source-state patches, controls, before images and verification logs remain in
that directory. The goal stays active for the unresolved high-severity causes.

The eleventh review repaired two existing DOM behaviors (JS13-L6/JS13-L7;
D9.1.1v4 / D9.1.3v2). The native interface registry now exposes the distinct
`HTMLDetailsElement` constructor/prototype for parsed and created details
nodes. Initial intersection/resize sampling now obtains the first layout via
the existing shared checkpoint, even if page scripts never query geometry.
Host-driven commit guards and virtual snapshot time remain unchanged (D10.1.7).

All 175 high-severity pages, Dock and nine recent controls were refreshed:
184 valid PNGs and Dock's unchanged 150-second timeout. The latest comparison
still has 1066 pairs, **175 high-severity pages**, one critical capture blocker
and 891 low-severity pages (161 automated candidates and 14 manually retained
omissions). No page is newly cleared in this review. Sixteen changed/manual
pairs and all nine controls were visually checked. The same fourteen manual
omissions remain high. All 1,067 original browser PNG hashes and browser metadata,
catalog URLs, thresholds, script admission limits and capture deadlines remain
unchanged.

Join's company-page startup exception is gone, but its large content omission
remains. An archived trace sets its reveal attribute at virtual time zero;
the authored opacity transition is separate from the repaired interface.
The independent initial-observer defect is not assigned as Join's residual
cause. Ahrefs' score changes from 21.5% to 35.8% with another randomly selected
promotional banner. Its saved source contains the weighted selector, and both
prior and final binaries select both banner variants in controls. No exclusive
code regression is inferred from that score change. The oversized duplicated
banner remains an unresolved major difference.

Qualification passes 32 real CLI cases, including four initial-observer PNG
cases that previously failed three, seven DOM UI fixtures with 50 assertions,
574 AST baseline cases, 3,851 recorded static layout cases (six skips), 630
JavaScript layout cases and five comparator tests. The details fixture fails
all nine assertions before and passes all nine after; independent Chromium
checks its interface expectations too. The observer tests verify asynchronous,
single delivery without page geometry reads. Native Radiant/Input implementation
is unchanged in this review; their recorded aggregate qualification and known
failures remain in the prior audits.

See [round-eleven evidence](comparison/repairs.html#round-11) and
snapshots/visual_comparison_review_round11.json. The independent main-checkout
paint API build mismatch persists. The compatible integration host uses the
exact current repaired DOM/observer/CSSOM/interpreter files and current sibling
archives, pinned as temp/online-visual-round11/lambda-final.exe (SHA-256
2f61dbae9642c4a562d842ae3e1950fad3cbf8e00cbfef27457f55509b14e04e).
Source-state patches, before images, controls and verification logs remain in
that directory. The goal remains active for the unresolved high-severity causes.

The twelfth review repaired grid-item percentage sizing (RI-048;
D9.1.1v4 / D9.1.3v2). Intrinsic contributions used the whole grid as a
percentage basis before the item's area had been solved. Calculated
percentages also retained provisional preferred sizes during placement.
The existing containing-block scope and substituted size classification now
separate these stages; used math sizes resolve through the shared owner-aware
length resolver in the solved area. Query scratch stays within D5.5.1 ownership.

Nova Benefits restores its hero illustration and balanced text column, clearing
major review at 15.1% mean error and 22.2% changed area versus 22.5% mean error
before. The reference consent bar and typography still differ. Zapier's customer
stories restore the two-column heading, action and photograph, clearing major
review at 2.5% versus 15.2%. A frozen Nova original reproduces the offscreen image;
a diagnostic one-declaration width-auto control works on the old renderer, and
the repaired original matches that control exactly in pixels. A frozen Zapier
page also reproduces the displaced hero before and restores it after using the
same HTML/CSS with scripts removed. Canonical URLs and sources are unchanged.

All 175 incoming high-severity pages, Dock and nine controls were refreshed:
184 valid PNGs and Dock's unchanged 150-second timeout. The round-twelve comparison
had 1066 available pairs, 173 high-severity pages, one critical blocker
and 893 low-severity pages (159 automated candidates and 14 manually retained
omissions). Eighteen changed/newly cleared/manual pairs and all nine controls
were visually reviewed. All fourteen manually retained omissions remain high.
Ahrefs' lower score selects another random promotional banner and remains high;
prior controlled evidence separates that variation from a code regression.

Qualification passes 84 native dynamic cases, 42 real CLI cases, eight DOM UI
fixtures with 58 assertions, 574 AST cases, 3,851 recorded static layout cases
(six skips), 630 JavaScript layout cases, five comparator tests and geometry
lint. The new native case checks nine variants at two widths; independent
Chromium confirms all 18 geometry sets. Ten new PNG cases pass, versus five
failures before; independent Chromium confirms their expected pixels and
geometry. The event fixture changes from 2/8 to 8/8 assertions through resize,
font and variable changes. The native aggregate retains 929 passes, one device
skip and the known RI-035 unavailable-voice failure. Input source is unchanged.

See [round-twelve examples](comparison/repairs.html#round-12),
snapshots/visual_comparison_review_round12.json and
temp/online-visual-round12/nova-frozen-comparison.json. All 1,067 original browser
PNG hashes and browser metadata, catalog URLs, thresholds, script admission
limits, virtual time (D10.1.7) and capture deadlines remain unchanged. The
independent main-checkout paint API build mismatch persists. The compatible
integration host uses exact current repaired root sources and current sibling
archives, pinned as temp/online-visual-round12/lambda-final.exe (SHA-256
87bc401e1dda61f17e7696df71de0e8994fd3769cf31c3c5e02d7f3a624f3f1f).
Source-state patches, before images, frozen controls and qualification logs are
preserved there. The goal remains active for unresolved high-severity causes.

The thirteenth review repaired the root font's rem basis (RI-049;
D9.1.1v4 / D9.1.3v2). Root font-size evaluation had cached the initial 16px
before the actual root became 14px. Descendant fonts and geometry retained
that stale basis. The shared resolver now uses initial metrics for root
self-reference and publishes the computed, unzoomed size before descendant
layout, including cached-style and live-restyle paths.

Zammad's company page restores its one-line 56px heading and section geometry,
clearing major review at 1.1% mean error versus 20.3%. The frozen original now
matches Chromium's geometry and matches both old/new renderers of an equivalent
literal-14px diagnostic control exactly in pixels. Canonical HTML/CSS is unchanged.

All 173 incoming high-severity pages, Dock and eleven controls were refreshed:
184 valid PNGs and Dock's unchanged 150-second timeout. The round-thirteen report had
1066 available pairs, 172 high-severity pages, one critical blocker and
894 low-severity pages (157 automated candidates plus 15 manually retained
omissions). Eighteen changed/manual/newly cleared pairs and all eleven controls
were visually checked. Pivot's new gradient lowers its coarse score, but its
hero heading, copy, actions and cube remain absent; visual review retains this
major omission. The prior fourteen manually retained omissions also remain high.
Calmind's missing first-party assets are traced to CDN browser-check responses
for a valid compressed request profile (LI-020); no native transport defect is
confirmed, and no compression/header workaround is introduced.

Qualification passes 85 native dynamic cases, 50 real CLI cases, nine DOM UI
fixtures/64 assertions, 574 AST cases, 3851 static layout cases (six skips),
630 JavaScript layout cases, five comparator tests and geometry lint. Eight
new rem PNG cases change from one pass to eight, with independent Chromium
geometry/pixel checks. Nine native fixture geometries agree with Chromium;
the root-variable event fixture changes from two of six pixels to six of six.
The native aggregate has 930 passes, one device skip and the known RI-035
unavailable-voice failure. Input implementation is unchanged.

See [round-thirteen evidence](comparison/repairs.html#round-13),
snapshots/visual_comparison_review_round13.json and temp/online-visual-round13/.
All 1067 browser PNG hashes/metadata and catalog URLs remain unchanged, as do
thresholds, source admission, capture deadlines and D10.1.7 virtual time.
The independent main-checkout paint API build mismatch remains; the compatible
integration host uses exact current repaired root sources and current sibling
archives. The immutable pin is temp/online-visual-round13/lambda-final.exe,
SHA-256 99f0485ed67eb6b1c0faeebd4c1d607ee10f3e1c4b3d576e0ca26055b7b4758f.
Full source-state patches, frozen controls and qualification evidence are
preserved there. The user ended this goal after this review; unresolved
high-severity causes and the Stripe placement investigation remain documented
for a later pass.

The fourteenth review resumed the goal on 2026-11-13 and repaired two shared
grid defects (RI-058/RI-059; D9.1.1v4 / D9.1.3v2). Absolute named/numeric grid
placement now supplies its containing area before child sizing through the
ordinary positioned solver. Nested grids retain their formatter's geometry
and use natural block sizing when alignment does not assign an extent. The
existing edge and detailed alignment resolvers are shared, with borrowed
per-child state scoped under D5.5.1.

Stripe's blog restores the heading, author, copy, action, image and white card.
The same frozen input now has a 460px card, 524×428px image and row sizes that
match Chromium, with a remaining 9px global vertical offset. The fresh online
comparison clears major review at 6.0% mean error versus 31.6%. Its residual
gradient requests a WebGL1 profile outside the current bridge; that feature
gap is recorded in the [central Lambda ledger](../../vibe/Lambda_Issue_Ledger.md#dom-canvas-webgl1).
No canvas substitute or graphics-host override is used.

All 172 incoming high-severity pages, Dock and twelve controls were refreshed:
184 valid PNGs and Dock's unchanged 150-second timeout. The current report has
1066 available pairs, **169 high-severity pages**, one critical blocker and
897 low-severity pages (153 automated candidates plus 16 omissions retained
below the automatic threshold). Twenty-six changed/manual pairs and all twelve
controls were visually checked. FramerAuth and Superhuman still lack their main
hero, and Zendesk lacks its primary navigation, so their lower metrics do not
clear major review. All eighteen manual annotations remain high severity.
Bizzabo and ExactlyAI now clear visual review; their exclusive restoration
cause remains unisolated from source and live-resource changes.

The four initially missing-media cases were checked with alternating current
baseline/repaired binaries at unchanged settings. Cofounder, Banky and Amperos
restore the images in both hosts when their requests finish; this does not
isolate a grid regression. The canonical attempts retain their pending-resource
observations, and the one-second optional-resource grace is unchanged (RI-061).
Unreal's preserved browser reference itself shows an access-denied page, limiting
interpretation of that comparison. No URL or reference is removed to lower a score.

Qualification passes 89 native dynamic cases, 64 real CLI cases, eleven DOM UI
fixtures/81 assertions, 574 AST cases, 3851 static layout cases (six skips),
630 JavaScript layout cases, five comparator tests and geometry lint. Fourteen
new PNG/geometry cases agree with independent Chromium; the two live fixtures
pass all seventeen assertions. Radiant's aggregate has 1083 passes, one device
skip and the known RI-035 voice failure. The fresh Input gate has 5224 passes,
one skip and its existing graph LOC, transition admission and archive failures.

The current main checkout builds successfully against current sibling archives;
the old compatible-host fallback and paint API mismatch are obsolete for these
revisions. All three source heads advanced before this review, so unrelated
score movement is not exclusively attributed to the two grid repairs.
See [round-fourteen evidence](comparison/repairs.html#round-14),
snapshots/visual_comparison_review_round14.json and temp/online-visual-round14/.
The immutable renderer is lambda-final.exe in that evidence directory,
SHA-256 4453b420a133510ae84eea0c9bfafae8b7605786d7df270a1405c4bb55c605e3.
All 1067 browser PNG hashes/metadata and catalog URLs, thresholds, source
admission, deadlines and D10.1.7 virtual time are unchanged. The goal remained
active after that milestone.

The fifteenth review repaired DOM collection iterator installation
(**D8.4.4 / D7.2.2 / D9.5.5**). It reuses the realm's canonical
`Array.prototype.values`, including before Window publishes Array. This fixes
CSSStyleDeclaration's non-iterable error and proxy recursion. The unchanged
saved Qualified docking bundle now initializes, and both live Lattice pages
lose that startup exception. Their visual differences remain high severity:
a saved customers final-state probe finds its authored anti-flicker class
hiding the root at export's selected virtual frame. Its 4000ms timer reveals
a later Chromium frame. The reduced initial Chromium frame is hidden too;
**D10.1.7** timing and original references stay unchanged. See the
[iterator repair](<../../vibe/Lambda_Issue_Ledger (fixed).md#dom-collection-iterators>)
and [frame timing limitation](../../vibe/Lambda_Issue_Ledger.md#online-capture-frame-timing).

All 169 incoming high-severity pages, Dock and fifteen low controls were
refreshed: 184 valid PNGs and Dock's unchanged 150-second timeout. The report
still has **169 high-severity pages**, one critical blocker and 897 low-severity
pages across 1066 pairs (151 automatic candidates plus eighteen manual
omissions below the automatic threshold). Twenty-four changed/manual pairs
were visually reviewed; all fifteen low control PNGs are byte-identical to
their previous captures. Cofounder and Banky regain media but still lack main
hero text/actions, so lower scores do not clear major review. Superlist changes
from partial dark content to blank (13.7% to 76.1% mean error); its cause remains
unisolated from live source, readiness or the iterator change. No regression
exclusion is claimed. All original browser PNGs/metadata, catalog URLs,
thresholds and deadlines remain unchanged.

Qualification passes 575 AST baseline cases, 120 focused MVP cases, 69 CLI
rendering/comparison cases, thirteen DOM UI fixtures/103 assertions, and 630
recorded JavaScript layouts. Sixteen static suites retain 4979 passes and
295 recorded partials with zero regressions. The new DOM fixture improves
from 0/10 to 10/10 in AUTO, AST and MIR; Chromium independently agrees on those
ten checks and 27 generic-array controls. Those 27 controls already passed
before the repair and verify reuse of the shared implementation. No additional
Lambda-input or Radiant implementation changes were made in this review.

Evidence: [round fifteen](comparison/repairs.html#round-15),
`snapshots/visual_comparison_review_round15.json` and
`temp/online-visual-round15/`. Its immutable host is `lambda-final.exe`, SHA-256
`074755341e8a92787e9bd8b160480748053cfc954a7d604d78f32b4cddf73a4f`.
The user cleared the goal and requested wrap-up during this refresh; no further
repair cycle is active. The next investigation is checkpointed separately.

`test_page_load_gtest` writes its local fixture timing and memory measurements
to `page_load_metrics.csv` in this directory.
