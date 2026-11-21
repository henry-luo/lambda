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
node test/online/capture_snapshots.cjs --labels=python,webkit_blog --settle-ms=1000
```

`--settle-ms=N` selects an explicit post-load window for both renderers, followed
by a rendering opportunity. Lambda uses its virtual timer frame; Chromium waits
the same interval after load. Chromium's animation timeline is held during
loading, then resumes at its ordinary rate for that window, matching native
frozen startup. Authored pauses and individual playback rates are preserved.
This excludes network-loading time from animation age on both sides; live
content and script startup can still differ. The online capture command defaults
to **1000 ms on both sides**. Zero selects the initial post-load frame.
`RADIANT_ONLINE_SETTLE_MS` supplies the same option through the environment.
The explicit option takes precedence over that environment default. Native
exports also honor the existing `LAMBDA_POST_LOAD_SETTLE_MS` host setting; their
unspecified initial-frame default is unchanged.

Each attempt records `lambda_settle_ms` or `browser_settle_ms`. The comparator
refuses a pair with different explicit windows or only one recorded window;
recapture both sides together when changing this setting. A `--failed-only` retry
with both renderers selected also refreshes a successful counterpart if its
recorded window differs. Explicit single-renderer runs retain that selection. Historical pairs with
both fields empty retain their legacy status. Frame-selection refreshes keep
their old images and a separate audit in `comparison/capture_frame_updates.json`;
they are distinguished from engine repairs and changed-page design refreshes.
The selected window must fit the existing browser navigation deadline. It does
not increase script CPU or network-resource budgets (**D10.1.7**).

Attempts with an explicit window also record `lambda_animation_origin` and
`browser_animation_origin` as `post-load`. Mixed or unknown recorded origins
cannot form a new comparison. Historical pairs with both fields empty retain
their historical status; a retry refreshes a successful counterpart with a
legacy origin when both renderers are selected. Animation-origin refreshes
are recorded in the same capture-frame audit, separately from source repairs.

Animation-origin calibration does not freeze Chromium's JavaScript timers
during loading. A page that waits on an external callback or a JavaScript
fallback deadline can still reach a different startup state from native's
frozen timer frame. Gladia's 1500ms feature-flag fallback is a confirmed
example. Such omissions remain under review; matching animation metadata alone
does not establish complete timer or application-state parity.
A focused delayed-resource probe confirms native and Chromium timer divergence.
The installed headless shell's virtual-time policy can align JavaScript timers,
but its CSS animation clock still consumes the resource wait; that candidate
fails the existing animation control and was removed. No clock override is
qualified by that experiment (`temp/online-visual-round123/`).

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

Failed-only retries preserve successful sides whose selected window still
matches; a changed window refreshes both sides when both renderers are selected.
Other manifest rows are preserved. Results are saved after each renderer
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
node --test test/online/render_observers.test.cjs test/online/render_observer_zero_area.test.cjs
node --test test/online/render_dataset_metadata.test.cjs
node --test test/online/render_error_stack.test.cjs test/online/render_document_title.test.cjs
node --test test/online/render_grid.test.cjs
node --test test/online/render_rem.test.cjs
node --test test/online/render_grid_absolute.test.cjs
node --test test/online/render_grid_nested.test.cjs
node --test test/online/render_flex_percentage.test.cjs
node --test test/online/render_nested_pseudo.test.cjs
node --test test/online/render_intrinsic_width_value.test.cjs
node --test test/online/render_positioned_order.test.cjs
node --test test/online/render_video_percentage.test.cjs
node --test test/online/render_video_first_frame.test.cjs
node --test test/online/render_canvas_transform.test.cjs
node --test test/online/render_html_controls.test.cjs
node --test test/online/render_html_scripts.test.cjs
node --test test/online/render_script_preparation.test.cjs
node --test test/online/render_dynamic_scripts.test.cjs
node --test test/online/render_resource_timing.test.cjs
node --test test/online/render_browser_globals.test.cjs
node --test test/online/render_node_iterator.test.cjs
node --test test/online/render_url_live.test.cjs
node --test test/online/render_readable_stream.test.cjs
node --test test/online/render_dialog_display.test.cjs
node --test test/online/render_console_groups.test.cjs
node --test test/online/render_svg_percentage_intrinsics.test.cjs
node --test test/online/render_svg_comment.test.cjs
node --test test/online/render_compact_conditions.test.cjs
node --test test/online/render_grid_flow_variable.test.cjs
node --test test/online/render_abs_percentage_height.test.cjs
node --test test/online/render_css_resource_discovery.test.cjs
node --test test/online/render_nested_ratio_height.test.cjs
node --test test/online/render_intrinsic_image_basis.test.cjs
node --test test/online/render_lab_colors.test.cjs
node --test test/online/render_text_background_ancestor.test.cjs
node --test test/online/render_intrinsic_motion_baseline.test.cjs
node --test test/online/render_svg_image_motion.test.cjs
```

The session tests exercise real Lambda HTTP rendering, layout and both batch
paths against a local server, including response cookies, HttpOnly visibility,
redirects, subresource requests and profile lifetime. Five additional PNG/request
controls cover inline CSS image discovery on div/video, custom properties,
image-set and script style edits (RI-064; D5.5.1). `LAMBDA_RENDER_EXE` selects
the renderer executable. Socket access and native rendering services are required.
The stylesheet tests check real exported PNG pixels after CSSOM rule edits and
style text replacement; they share the same executable selection.
The clipping tests use the shared `clip_fixtures.cjs` inputs to check real PNG
pixels after variable/math resolution, font sizing, zoom and empty clipping.
The observer tests check initial intersection/resize delivery and rendered
pixels without page geometry reads, including offscreen and disconnect controls.
The grid tests use `grid_fixtures.cjs` to check intrinsic percentage contributions,
solved area sizing, owner units and authored constraints. These four PNG suites
share `render_fixture.cjs` for exports and image reads. The flex percentage tests
check cyclic preferred-size contributions in nested rows, computed and logical
widths, style mutations, and authored fixed/nowrap overflow controls.
The nested pseudo-selector tests check that type-prefixed pseudo rules preserve
following rules in style, media and supports blocks, with component-block controls.

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

The seventeenth review resumed the goal and fixed **RI-062**, the cyclic
percentage contribution in nested flex rows. A child `width:100%` incorrectly
replaced its min-content contribution with its full unwrapped width. The fix
retains the existing intrinsic measurement and shares substituted percentage
facts for math, variables and logical sizes (**D9.1.1v4 / D9.1.3v2**, scratch
**D5.5.1**). In identical saved Azure HTML/CSS, four media rows now fit236px
and the titles wrap instead of clipping. Missing primary navigation/banner and
a small auto cross-height discrepancy remain separate; Azure stays high.

All185 selected attempts completed:184 valid PNGs and Dock's unchanged150s
timeout. The severity-sorted report retains **169 high-severity pages**, one
critical blocker and897 low pages across1066pairs. There are150 automatic major
candidates and19 manual omissions below threshold. Amperos crosses the coarse
threshold by only0.2 percentage points but its primary navigation remains
nearly invisible, so visual review retains it as major. Every prior manual
omission is retained. Thirty-one changed/manual/control pairs were reviewed;
11of15 low control PNGs are byte-identical, and the four changed controls remain
visually below major severity. No exclusive repair is assigned to live-only
metric movements: Mailchimp and Pelago lose media, while Ahrefs changes its
weighted authored banner.

WebKit's article omission reduces to its authored400ms fade-in: native export
selects0ms and independent Chromium is also transparent at0ms, opaque at400ms.
The earlier paint suspicion is not confirmed. Superlist's same saved current
HTML is blank on both round14 and15 qualified hosts; the iterator change alone
does not explain the prior movement. External navigation/resources were not
fully frozen, so source/readiness causality remains open. **D10.1.7** frame
timing, original browser PNGs/metadata,URLs,admission,thresholds and deadlines
remain unchanged.

Qualification:89 native dynamic cases,91 online CLI/capture/comparator cases,
630 recorded JS layouts, and16 static suites with4979passes/295recordedpartials,
zero regressions and skips; required layout integer-cast lint passes. The13
new flex PNG cases improve8/13 to13/13 and Chromium independently agrees on
all13 pixel witnesses, including style mutation after a layout read.

Evidence: [round seventeen](comparison/repairs.html#round-17),
`snapshots/visual_comparison_review_round17.json` and
`temp/online-visual-round17/`. Immutable host SHA256
`10fae5cc038c9f09a06c06b189b5f604bb648b6e30a628e9f2c52edc96c3da25`.
The goal remains active. The next confirmed cause is the CSS nesting parser
mistaking type-prefixed pseudo selectors for declarations on DigitalOcean.

The eighteenth review repaired **LI-028**, CSS nesting ambiguity in
lambda-input. A type-prefixed pseudo selector such as `body::before` was
mistaken for a declaration and consumed later nested rules. One shared
rule-start decision now preserves the following rules in style, media and
supports blocks, including when the type name is also a property name.
Custom property and function blocks retain their declaration context.
Ownership follows **D9.1.1v4 / D9.1.3v2**, parsed state **D5.5.1**; no authored
CSS or page-specific behavior is substituted.

Identical saved DigitalOcean HTML/CSS isolates the restoration of the
lighthouse hero, paragraph width and hero spacing. Its fresh primary content
is reviewed against the unchanged original reference; cookie overlay,
background and minor navigation differences remain separately visible.

All185 attempts completed:184 valid Lambda PNGs and Dock's unchanged150s
timeout. The updated severity-sorted report has **169 high-severity pages**,
1 critical blocker and897 low pages across1066pairs.
There are151 automatic candidates and18 manual
omissions below the threshold. 32 changed/manual/control pairs were
visually reviewed; 12of15 low controls are byte-identical. Live-only
metric changes are not assigned exclusively to the parser repair. DigitalOcean
clears major review, while Bizzabo's fresh control loses its main hero text and
becomes high severity. Its source/readiness versus parser-change cause remains
unisolated; no regression exclusion is claimed. Patreon now renders a feed
after an observed redirect/navigation, so its large movement also stays high.
Original
browser PNGs/metadata, catalog URLs, thresholds, admission, deadlines and
**D10.1.7** virtual time stay unchanged.

Qualification:114 native parser cases,111 online CLI/capture/comparator cases,
74 existing live CSS nesting/CSSOM UI assertions,630 recorded JS layouts and
16 static suites with4979passes/295recordedpartials,zero regressions/skips.
The20 new PNG controls improve2/20 to20/20; independent Chromium agrees on
all20. Full Input aggregate records5223passes and one emoji fallback skip,
with the same GraphParser LOC budget, external-parent admissibility and
sample.tar.gz failures (**LI-001 / LI-002 / LI-012**). The aggregate is not
reported green. The separate deferred single-block width discrepancy is
recorded as LI-029 with an unconfirmed cause; it is outside this major fix.

Evidence: [round eighteen](comparison/repairs.html#round-18),
`snapshots/visual_comparison_review_round18.json` and
`temp/online-visual-round18/`. Immutable host SHA256
`2119303b51366ffd876710921373c88ea244d4cfe84f159572136e6cd1fe37fc`.
The goal remains active.

The nineteenth review repaired **RI-063**, decoded video painting over
foreground content. Both full-frame and cached video paths previously blitted
frames after the completed page, covering Bizzabo's hero heading/copy/action
and header despite the video's negative z-index. Immutable video/controls
snapshots now enter the ordinary CSS paint stream; playback wakes repaint
ordered page content. Shared fit, positioning, clipping, transformations and
effect scopes apply. Ownership follows **D5.5.1** and **D9.1.1v4 / D9.1.3v2**;
**D10.1.7** clocks and authored page content stay unchanged.

A native decoded-frame foreground witness fails before the fix and passes
after it. The same saved complete Bizzabo HTML restores the main foreground;
external resources remain live, so this is not a fully frozen-source/time
comparison. Its fresh canonical difference improves17.8% to5.5% and it clears
major review. Logo/media, banner and small text differences remain separate.

The latest severity-sorted report has **168 high-severity pages**,1 critical
Dock capture blocker and898 low pages across1066 pairs. There are149 automatic
candidates and19 retained manual omissions below threshold. All185 selected
pages have fresh attempts; PNPM's first DNS failure is archived and one
unchanged retry succeeds, giving186 attempts/184 valid PNGs plus Dock's
unchanged150s timeout. All19 manual annotations remain. Twenty-nine changed,
manual and control triples were visually reviewed;14of15 low controls are
byte-identical. Patreon returns to marketing content after the previous feed,
Ahrefs changes its weighted banner, and Unreal loses media while its original
browser reference itself shows403. Those live/reference variations are not
assigned exclusively to the video repair. URLs, original browser data,
admission, deadlines and thresholds stay unchanged.

Qualification:34 native video cases,247 raster/paint/ownership cases,
111 online tests,117 existing media/CSSOM UI assertions (10 runner cases),
and17 layout categories with5609passes (630JS+4979static),295recordedpartials,
0regressions/0skips. Four Node modules and native rendering/UI clients were
refreshed after the document-state ABI change. Input's aggregate was not
repeated; round18's independent LI-001/002/012 failures remain recorded.
Playback now replays the page rather than only blitting video placements;
release playback cost is unmeasured. This does not qualify a new platform
backend, GPU video composition or SVG/PDF frame parity.

Evidence: [round nineteen](comparison/repairs.html#round-19),
`snapshots/visual_comparison_review_round19.json` and
`temp/online-visual-round19/`. Qualified host SHA256
`b18598069c746fb40190437aa5d1af1d4d300bc303a11529df2194ae7f8898dc`.
The goal remains active; Maki's missing hero media is the next investigation.

The twentieth review repaired **RI-064**, missing inline CSS image discovery.
Headless network-managed paint only consumes ready bytes, but resource discovery
queued stylesheet URLs before inline attributes were parsed and never requested
those inline dependencies. It now walks initial inline declarations and CSSOM
edits using the existing recursive URL walker. Untouched attributes parse in
traversal-owned scratch; cached CSSOM blocks keep their own owner (**D5.5.1**,
**D9.1.1v4 / D9.1.3v2**). No page-specific fetch or blocking paint transfer is added.

Five HTTP PNG/request controls improve0/5 to5/5: div/video background,
custom-property URL, image-set and script style mutation. Both identical saved
Maki controls regain their hero background. Its canonical difference improves
51.3% to7.2%; the heading/copy/actions are readable and it clears major review.
Remaining sizing and video-frame details are separate. External resources are
live, so a fully frozen-source/time comparison is not claimed.

The latest report has **167 high-severity pages**,1 critical Dock blocker and
899 low pages across1066 pairs. There are148 automatic candidates and19 manual
omissions below threshold. All19 previous manual annotations remain, with a
new twentieth annotation for Origin: its blue background returns, but the hero
heading/copy/action and product image remain absent. Its automatic clearance
is rejected after visual review. All185 attempts finish with184 valid PNGs and
Dock's unchanged150s timeout. 29 changed/manual/control triples were
reviewed;14of16 low controls are byte-identical. Ahrefs again changes
its weighted banner; live-only changes are not exclusively attributed to the
inline-resource repair. Original browser PNGs/metadata, URLs, admission,
thresholds, finite resource grace and **D10.1.7** virtual time remain unchanged.

Qualification:116 online cases,247 refreshed raster/paint/ownership cases,
117 existing media/CSSOM UI assertions (10 runner cases), and17 layout categories
with5609passes (630JS+4979static)/295recordedpartials,0regressions/0skips.
Input's aggregate was not repeated; the independent LI-001/002/012 failures
from round18 remain recorded. This repair covers dependency discovery; broader
network readiness, missing mask consumers and new media backends remain outside
its scope.

Evidence: [round twenty](comparison/repairs.html#round-20),
`snapshots/visual_comparison_review_round20.json` and
`temp/online-visual-round20/`. Qualified host SHA256
`4d256d4cdd80441451d1309ebb2195d3ddeb225f6f256ae60ca1570b1281733e`.
The goal remains active.

The twenty-first review repairs **RI-065** and companion **RI-066**. Structured
inline text/BR height estimation discarded forced lines when headings contained
spans. Eligible horizontal runs now use the existing line formatter and restore
selected fragments and caller/counter state (**D5.5.1**). Comand's static headline
moves from y463.2 to y333.6, agreeing with Chromium within 0.02px; its copy and
action are visible again. Its background video remains absent, so it stays high.

Visual review rejected the first pin because Exactly's headline clipped upward.
Auto column sizing walked zero-height contents wrappers and then recursed over
unmeasured text boxes for auto bases. It now uses the existing collected-item
walker and constrained flex-basis calculation. The same static Exactly headline
returns from y210 to Chromium's y298. The ineffective cache experiment and all
diagnostic logging were removed before final qualification.

The latest report has **167 high-severity pages**, 1 critical Dock
blocker and 899 low pages across 1066 pairs: 147
automatic candidates and 20 retained manual omissions
below threshold. All 20 prior annotations remain; a new Panxo annotation
rejects its automatic clearance because major hero placement/media gaps persist. Two passes
make 370 attempts across 185 pages; the final pass produces 184 valid PNGs and
Dock's unchanged 150s timeout. 31 final triples were reviewed; 15
of 17 low controls are byte-identical. Original browser PNGs/metadata, URLs,
thresholds, admission, finite resource grace and **D10.1.7** virtual clocks remain
unchanged. Live-only movements, including Ahrefs' varying banner, are not
exclusively attributed to these repairs.

Qualification: 11 PNG and independent Chromium controls, 91 native dynamic
cases, 127 online cases, 247 refreshed raster/ownership cases, and 117 existing
UI assertions (10 runner cases). The 17 layout categories retain 5609 full
passes (630 JS + 4979 static) plus 295 recorded partials, with zero baseline
regressions, skips or runner errors. Input's aggregate was not repeated; the
previous independent LI-001/002/012 failures remain recorded. Release performance
and broader vertical/paged/atomic inline estimation are outside this milestone.

Evidence: [round twenty-one](comparison/repairs.html#round-21),
`snapshots/visual_comparison_review_round21.json` and
`temp/online-visual-round21/`. Qualified host SHA256 `48f7bfd4a632cc934b8171c95133d6697b1da9d15a6128a68eebe891e9f892a0`.
The goal remains active.

The twenty-second review repairs **RI-067**. Row-flex cross-height queries reused
the preferred CSS width after flex allocation, and intrinsic height keywords and
clamps kept their pre-flex measurements. Queries now distinguish available width
from assigned content width, include that mode in the cache key, and refresh
intrinsic cross sizes at the assigned width (**D5.5.1**). Descendant percentage
resolution and authored styles retain their existing behavior.

In the same saved script-free Panxo HTML, the hero row shrinks from 508px to
172px (Chromium 163.19px), returning the heading, copy, actions and product panel
to the viewport. Its canonical difference falls from 9.1% to 3.0%.
The final image confirms the major repair, so its manual annotation is retained
with a resolution rather than an active omission. Small text and spacing
differences remain outside this major-review milestone.

The latest report has **166 high-severity pages**, 1 critical Dock
blocker and 900 low pages across 1066 pairs: 147
automatic candidates and 19 active manual omissions
below threshold. All 21 annotation records remain: 20 active and Panxo resolved.
All 185 fresh attempts finished with 184 valid PNGs and Dock's unchanged 150s
timeout. 30 final triples were reviewed; 15/17 low controls are
byte-identical. Original browser PNGs/metadata, URLs, thresholds, admission,
finite resource grace and **D10.1.7** virtual clocks remain unchanged. Live-only
source/readiness movements are not exclusively attributed to this repair.

Qualification: 22 PNG and independent Chromium controls, 92 native dynamic cases
(including resize and query-cache separation), 149 online cases, 247 refreshed
raster/ownership cases, 117 existing UI assertions (10 runner cases), and no
layout integer-cast lint violations. All 17 layout categories retain 5609 full
passes (630 JS + 4979 static) plus 295 recorded partials, with zero regressions,
skips or runner errors. Input's aggregate was not repeated; previous independent
LI-001/002/012 failures remain recorded. Release performance and broader vertical
or atomic intrinsic measurement are outside this milestone.

Evidence: [round twenty-two](comparison/repairs.html#round-22),
`snapshots/visual_comparison_review_round22.json` and
`temp/online-visual-round22/`. Qualified host SHA256 `910ce492b2448433eac314b50e79156e2d0e14c08cf1f04300c2efe959685df5`.
The goal remains active.

The twenty-third review repairs **RI-068**. Grid row contributions preferred a
percentage width resolved against the whole grid over the solved column area.
The generic enclosing height query also estimated equal-width columns, keeping
the inflated percentage minimum. Row measurement now shares placement's size
resolver, and horizontal grid height queries reuse the existing track algorithm
under geometry rollback (**D5.5.1**). No page conditions or authored-style
changes are used.

In unchanged saved script-free Monese HTML, the hero shrinks from 1392px to
800px, matching Chromium; its heading moves from y533.90 to y237.90 (Chromium
y237.90625). Smaller side-image height/spacing differences remain. This static
repair is separate from the live page's unresolved hidden-state initialization;
it does not clear the blank canonical capture. Agiloft's saved script-free page
is blank in both engines, and Leaflet's modal remains the recorded RI-029
top-layer/backdrop feature gap.

The latest report has **166 high-severity pages**, 1 critical Dock
blocker and 900 low pages across 1066 pairs: 146
automatic candidates and 20 active manual omissions
below threshold. All 21 earlier annotation records remain. Superhuman's
wording now records that the footer overlap is gone while its main hero remains
absent; the earlier reason is preserved in review history. A new Cape annotation
rejects automatic clearance because its main hero text, action and illustration
remain absent. There are 22 annotation records, 21 active.
All 185 attempts finished with 184 valid PNGs and
Dock's unchanged 150s timeout. 38 final triples were reviewed; 12/18
low controls are byte-identical. Original browser PNGs/metadata, URLs,
thresholds, admission, finite resource grace and **D10.1.7** virtual clocks stay
unchanged. Live-only source/readiness movement is not exclusively attributed
to this sizing repair.

Qualification: 12 PNG and independent Chromium controls, 93 native dynamic
cases including live resize and query rollback, 161 online cases, 247 refreshed
raster/ownership cases, 117 UI assertions (10 runner cases), and integer-cast
lint. The 17 layout categories retain 5609 full passes (630 JS + 4979 static)
plus 295 recorded partials, with zero regressions/skips/errors. Four Node DSOs
were refreshed. Input's aggregate was not repeated; prior LI-001/002/012 failures
remain recorded. Vertical intrinsic height and release performance are outside
this milestone.

Evidence: [round twenty-three](comparison/repairs.html#round-23),
`snapshots/visual_comparison_review_round23.json` and
`temp/online-visual-round23/`. Qualified host SHA256 `d0aae7d46c8943d80bc05f1445aee9bd13d01b2c78380560297193b0b88aef2c`.
The goal remains active.

The twenty-fourth review repairs **RI-069, RI-071 and RI-072**. Height queries
now use resolved preferred widths, width constraints and proper box edges;
block-owned zero-level relative/sticky and absolute siblings paint in DOM order,
with the inverse hit sequence; atomic inline alignment retains relative-inline
ancestor offsets (**D5.5.1**).

The unchanged saved Maple hero shrinks from 843.4px to 605.77px (Chromium about
600px), restoring the statistics section to the viewport. Its large title is
separately confirmed as unsupported animated WebP (**LI-030**). The unchanged
saved Pivot source regains its headline, copy, actions and customer logos.
Missive also regains the large right-card quotation and logo in the same saved source; its manual major omission is resolved. Pivot still lacks the large cube illustration and stays high severity. Background/header and smaller spacing differences remain. No page shim,
URL substitution, capture-clock change, major new feature or exact parity is
claimed. **RI-070** records a pre-existing inherited-percentage-width discrepancy
whose source cause remains unisolated.

The latest report has **165 high-severity pages**, 1 critical Dock blocker
and 901 low pages across 1066 pairs: 146 automatic
candidates and 19 active manual omissions below threshold.
2 earlier annotation records were updated with the full previous record preserved in review history; 1 resolved. All other earlier records are unchanged. 20 records remain active. The final 185 attempts produce
184 valid PNGs and Dock's unchanged 150s timeout. The preceding 185-attempt
RI-069-only pass is preserved under `temp/online-visual-round24/first-qualified-pass/`.
36 final triples were reviewed; 15/18 low controls are byte-identical.
Original browser PNGs/metadata, URLs, thresholds, admission, finite resource
grace and **D10.1.7** clocks remain unchanged. Live-only movements are not
exclusively attributed to these repairs.

Qualification: 96 dynamic and 13 custom native cases, 193 online cases, 247 rebuilt paint/ownership cases, 117 UI assertions (10 runner cases), 16 width-value plus 16 positioned PNG/independent Chromium controls, 32 independent SVG/PDF controls and integer-cast lint. All 17 layout categories retain 5609 full passes and 295 recorded partials with zero regressions/skips/errors.
Two child-walk signatures extend with an explicit phase flag; native clients
and all four Node module build targets are refreshed. Structure/Node state
layout is unchanged. Final layout/UI checks use the exact pin after correcting
an incidental host relink by the native test target. Input source is unchanged;
its aggregate was not repeated and prior LI-001/002/012 failures remain recorded.
Full zero-level descendant lifting, stacking conformance and release performance
are outside this milestone.

Evidence: [round twenty-four](comparison/repairs.html#round-24),
`snapshots/visual_comparison_review_round24.json` and
`temp/online-visual-round24/`. Qualified host SHA256 `4648e3a3abaec9cd3eb443e4962ddf164bd43c37d39cdf8d65272e6dfe6a69eb`.
The goal remains active.

The twenty-fifth review repairs **RI-073**. Replaced natural/default sizing now
follows cyclic percentage-height invalidation. Videos in auto-height containers
retain their natural or default height instead of collapsing to zero; definite
and zero-height parents keep their specified behavior. Natural ratios describe
content through border-box decorations. The existing helpers are shared; no
page condition or new media/codec feature is added (**D5.5.1** ownership).

In identical saved script-free Pivot HTML, the video/immediate wrapper grows
from 0 to 619.72888px (Chromium 619.71875px), while the hero stays 984.699px.
Its latest canonical full-page capture now displays the large animated artwork,
so its missing-artwork annotation is resolved. The remaining shape difference
reflects movie phase, with smaller background/header/spacing residue. The quick
script-free diagnostic captured the blank initial poster/frame; the full page
advances during loading. Native video uses wall-clock playback, which
**D10.1.7** does not freeze. Capture clocks are unchanged and no diagnostic
frame is substituted into a capture. Playback synchronization is unqualified.

Two Northstar pages became high severity as their vendor bundle changed to
6,043,094 bytes, exceeding the unchanged 5,242,880-byte single-script limit.
Prior/current hosts produce identical blank PNGs for the same current source;
the prior host also reproduces this at both live URLs. Scripted Chromium renders
the page; its script-free capture matches native pixels. This external-source/
admission-profile limit is recorded in the
[Lambda ledger](../../vibe/Lambda_Issue_Ledger.md#online-northstar-source-admission).
No budget, URL, bundle or readiness exception is adopted.

The latest report has **166 high-severity pages**, 1 critical Dock blocker
and 900 low pages across 1066 pairs: 148 automatic
candidates and 18 active manual omissions below threshold.
Pivot clears and Northstar adds two, a net increase of one high page from round 24.
1 earlier annotation record was updated with its full previous record preserved in review history; 1 resolved. All other earlier records are unchanged. 19 records remain active. All 185 attempts produce 184 valid
PNGs and Dock's unchanged 150s timeout. 38 final triples were reviewed;
15/19 low controls are byte-identical. Original browser images/metadata,
URLs, thresholds, admission, finite resource grace and capture clocks stay
unchanged. Live-only movements are not attributed exclusively to this repair.

Qualification: 97 dynamic and 13 refreshed custom native cases, 206 online cases, 247 rebuilt paint/ownership cases, 117 UI assertions (10 runner cases), 13 PNG/independent Chromium controls, 26 independent SVG/PDF rasters, 4 iframe neighbor controls and integer-cast lint. All 17 layout categories retain 5609 full passes and 295 recorded partials with zero regressions/skips/errors.
No public API/header/structure/state layout changes; Node module ABI is unchanged.
Input source is unchanged; its aggregate was not repeated and prior independent
LI-001/002/012 failures remain recorded. Broader replaced-size conformance,
playback synchronization and release performance are outside this milestone.

Evidence: [round twenty-five](comparison/repairs.html#round-25),
`snapshots/visual_comparison_review_round25.json` and
`temp/online-visual-round25/`. Qualified host SHA256 `3fb7114ad21abb7a6c90ce36ee6bfb3573d94370694c7817df60d2a20643e882`.
Work stopped after this qualified round at the user’s request; the remaining high-severity pages are unresolved.

The twenty-sixth review fixes **LI-030**, **RI-074** and **RI-070**: animated
WebP initial-frame decoding, min-content minimum widths and computed axis-size
inheritance. Maple’s title now renders; the independent form and image-bearing
inheritance reductions match Chromium. Playback, cookie-panel and remaining
Python slider/header differences stay unresolved. No page-wide clearance is
claimed: **166 high-severity pages**, 1 critical Dock blocker and 900 low remain.

Only **two online pages** were refreshed; other captures retain their earlier
dates. Focused checks pass: **99 native layout**, **37 image I/O** and **42 related
online render cases**, plus integer lint. The 185-page recapture, full layout
baseline, Input aggregate and unrelated paint/export checks were deferred.
Browser references, annotations, URLs, thresholds, admission and clocks are
unchanged (**D5.5.1 / D10.1.7**). See [round twenty-six](comparison/repairs.html#round-26)
and `snapshots/visual_comparison_review_round26.json`. The goal remains active.

The twenty-seventh review fixes **RI-075** (Canvas general matrices) and missing
**console group host bindings**. Origin’s particle initializer and Maven’s
slider initializer now complete. Their independent hero visibility gaps remain;
there are **166 high-severity pages**, 1 critical Dock blocker and 900 low.

Only **Maven and Origin** were refreshed. Focused checks pass: **11 render
controls**, 10 independent Chromium matrix controls, 8 AST/MIR console goldens,
2 existing Canvas goldens and 1 rebuilt provider contract; integer lint has zero
unmarked casts. Broad baselines and catalog recapture were deferred. Stellar’s
saved callback fails identically in Chromium; WebKit’s blank grid agrees with
Chromium at the native zero-time CSS fade frame. No renderer change is justified
by those observations. URLs, references, annotations and capture clocks are
unchanged (**D10.1.7**). See [round twenty-seven](comparison/repairs.html#round-27)
and `snapshots/visual_comparison_review_round27.json`. The goal remains active.

The twenty-eighth review fixes **RI-076**: percentage SVG viewport attributes
no longer feed a 300px fallback into height-at-width queries. Definite zero
containing widths also propagate correctly. The saved Contentsquare lead card
shrinks from **757.65px to 502.05px**, matching Chromium’s 501.61px; the refreshed
canonical image restores its hero image position and following card row.
The large lead-card sizing omission clears this review. Minor navigation wrapping, sidebar text and consent-state differences remain. The report now has **165 high-severity pages**,
1 critical Dock blocker and 901 low; 1 major-review clearance.

Only **Contentsquare blog** was recaptured. Focused checks pass: **7 PNG controls**,
7 independent Chromium controls and **98 native layout/cache units**; integer
lint has zero unmarked casts. Full layout/paint/JS/Input baselines and catalog
recapture were deferred. URLs, browser references, annotations, thresholds,
budgets and D10.1.7 capture clocks remain unchanged. See
[round twenty-eight](comparison/repairs.html#round-28) and
`snapshots/visual_comparison_review_round28.json`. The goal remains active.

The twenty-ninth review fixes **RI-077**, repeated shared-ancestor declaration
queries in SVG transition snapshots. It reuses the existing property cache with
an iterative walk and unchanged ownership/invalidation guards (**D5.5.1**).
Two focused native cases and five existing SVG cache/lifetime cases pass. The
saved Dock output is byte-identical; single release controls observe 9.83→9.12
CPU seconds, a limited result without a broad performance claim.

**Dock remains unresolved (RI-031):** matched scripted release profiles both
reach 150 seconds, with 23/25 completed full layouts. No canonical image or
comparison score is refreshed this round. **165 high-severity pages**, 1 critical
blocker and 901 low remain at their recorded capture dates. Thirty-four stored
pairs were inspected; full baselines and catalog recapture were deferred. URLs,
references, annotations, admission, budgets and **D10.1.7** clocks remain unchanged.
See [round twenty-nine](comparison/repairs.html#round-29) and
`snapshots/visual_comparison_review_round29.json`. The goal remains active.

The thirtieth review repairs **LI-034**, compact logical-condition token boundaries
and nested supports groups; **LI-035**, pending grid-auto-flow variables; and
**RI-078**, absolute percentage math after an auto-height container completes.
Ahrefs’ duplicated/tall promotion and over-wide hero are corrected. Statamic’s
large missing landscape returns. Only these two Lambda captures were refreshed;
**1 pages clear major review**, leaving **164 high-severity pages**,
1 critical Dock blocker and 902 low.

Focused checks pass: **17 PNG controls**, 17 independent Chromium controls,
32 Input media/supports units and four native absolute/grid cases. Integer lint
has zero unmarked casts. Full baselines and catalog recapture were deferred.
Other captures retain their dates; URLs, browser references, annotations,
thresholds, admission, budgets and D10.1.7 clocks are unchanged. See
[round thirty](comparison/repairs.html#round-30) and
`snapshots/visual_comparison_review_round30.json`. The goal remains active.

The thirty-first review fixes **RI-079**, stylesheet prefetch of unmatched/inactive
icon rules, and **LI-036**, rejection of existing consumers at resource capacity.
Discovery follows selected element/pseudo cascades; the existing **500-resource
limit remains unchanged**. Conductor's large hero and logos return, and
Storetasker imagery loads. Their remaining wrapping/state and heading gaps keep
both pages high. Only these two Lambda captures were refreshed; **164 high**,
1 critical Dock blocker and 902 low remain, with no page clearance.

Focused verification passes **ten rendering/request controls** and three native
capacity/cache units; integer lint has zero unmarked casts. The new HTTP controls
improve from 0/5 to 5/5; five existing inline controls pass. Full baselines and
catalog recapture were deferred. Other captures keep their dates. URLs, browser
references, annotations, thresholds, admission, limits and D10.1.7 clocks remain
unchanged. See [round thirty-one](comparison/repairs.html#round-31) and
`snapshots/visual_comparison_review_round31.json`. The goal remains active.

The thirty-second review fixes **RI-080**: preferred width now precedes ratio
height contributions, and SVG percentage height consumes a definite parent
basis before cached/natural fallbacks. Ratio-transferred heights are not treated
as cyclic. Isomeet's launch badge, button height and large hero placement return;
its error falls **20.46% → 8.75%**, clearing major review. Only Isomeet was
recaptured, leaving **163 high-severity pages**, 1 critical Dock blocker and 903 low.

Focused checks pass: **14 PNG controls**, seven independent Chromium controls,
and four native sizing cases; integer lint has zero unmarked casts. The first
6/7 candidate was held for its non-square failure. No ABI/structure layout change.
Full baselines and catalog recapture were deferred; other captures keep their
dates. Browser references, URLs, annotations, thresholds, admission, limits and
D10.1.7 clocks are unchanged. See [round thirty-two](comparison/repairs.html#round-32)
and `snapshots/visual_comparison_review_round32.json`. The goal remains active.

The thirty-third review fixes **RI-081**: wrapped flex height queries now include
item cross margins. Betterway's complete hero returns to its intended position;
its error falls **19.22% → 0.55%**, clearing major review. Only Betterway was
recaptured, leaving **162 high-severity pages**, 1 critical Dock blocker and904 low.

Four focused native cases pass, including signed/percentage/live margin changes
and read-only query geometry. Chromium confirms all four margin states; layout
integer lint passes. No ABI/structure change. Broad baselines and catalog
recapture were deferred. Browser references, URLs, annotations, thresholds,
admission, limits and D10.1.7 clocks are unchanged. See
[round thirty-three](comparison/repairs.html#round-33) and
`snapshots/visual_comparison_review_round33.json`. The goal remains active.

The thirty-fourth review fixes **RI-082**: Gaussian work accounting now follows
executed convolutions, and exact transparent runs cross zero-extension image
boundaries. Five focused native tests pass, including pixel equality, edge and
memory guards, a wide zero kernel, and noisy-input rejection. The public API,
limits and Gaussian math are unchanged.

Overflow's complete filter graph still fails; its two candidate PNGs are
identical. No canonical images, capture metadata or scores were refreshed.
The report remains **162 high**, 1 critical Dock blocker and904 low. No comparator
rerun or broad baseline was needed. See
[round thirty-four](comparison/repairs.html#round-34),
`snapshots/visual_comparison_review_round34.json`, and Radiant's RI-082 ledger.
The goal remains active.

The thirty-fifth review fixes **RI-083**: early flex intrinsic widths now include
variable/calculated scalar gaps and resolve em lengths against the owner's font.
Affinity's actions share a row and its navigation labels stop clipping at the top;
error falls **18.13% → 14.66%**. It remains high, leaving **162 high-severity pages**,
1 critical Dock blocker and904 low. Only Affinity was recaptured.

Four focused native tests and five independent Chromium gap states pass;
integer lint passes. No ABI/structure change. Broad baselines and catalog recapture
were deferred. Initial animation/anti-flicker states were confirmed on Agiloft,
Ebury, WebKit and Python; their clocks and references were retained. See
[round thirty-five](comparison/repairs.html#round-35) and
`snapshots/visual_comparison_review_round35.json`. The goal remains active.

The thirty-sixth review fixes **RI-084**: positioned descendants retain their
static ancestor's opacity group while keeping their containing-block geometry.
Zero-height parents now defer their backdrop save until descendant ink bounds
are known. Paint, export and native hit testing share the ownership traversal;
ordinary overflow below an external containing block preserves its CSS escape.

Eight PNG controls, four native paint/hit tests, three focused display-list tests
and eight Chromium controls pass; integer lint passes. The previous qualified
binary fails six of the final eight PNG controls. No ABI/structure change.
Saved Metronome and Highnote pages are pixel-identical before/after, so no page
clearance or canonical refresh is claimed. The report remains **162 high**,
1 critical Dock blocker and 904 low. Broad baselines and catalog capture were
deferred. See [round thirty-six](comparison/repairs.html#round-36) and
`snapshots/visual_comparison_review_round36.json`. The goal remains active.

The thirty-seventh review fixes **RI-085**: wide SVG Gaussian blurs use admitted
sliding box passes, preserving original-input edge extension and independent axes.
The shared window core also retains CSS shadow truncation/normalization. Small
kernels keep exact convolution. Memory/work limits and admission are unchanged.

Twenty-one focused filter/shadow/budget tests pass, with eighteen independent
exact-Gaussian quality/axis/edge states and four Chromium controls (maximum channel
difference3); integer lint passes. Only four Lambda captures were refreshed.
Overflow's error falls **20.40% → 1.86%**, Calendly **20.04% → 6.42%**, FramerAuth
**16.08% → 14.20%**, and Pitch **6.26% → 3.53%**. Their filter errors are gone.

Large independent omissions remain: Overflow's navigation, Calendly's hero text
and signup actions, and FramerAuth/Pitch main content. Two new manual annotations
retain Overflow and Calendly as high despite their lower coarse scores; all prior
reviews are preserved. Thus the report stays **162 high** (142 automatic +20
manual), 1 critical Dock blocker and904 low. Original browser references, URLs,
thresholds, budgets and clocks are unchanged. Broad baselines and catalog capture
were deferred. See [round thirty-seven](comparison/repairs.html#round-37),
`snapshots/visual_comparison_review_round37.json`, and Radiant's RI-085 ledger.
The goal remains active.

The thirty-eighth review fixes **RI-086/RI-087**: the root retains its resolved
minimum after empty-child collapse, and viewport-source body overflow no longer
clips its own zero-height box. One source selector is shared across viewport,
paint, native hit and coordinate scopes, with root/body containment exceptions.
Six focused native tests and eight PNG controls pass; Chromium independently
confirms seven geometry states and all eight pixel controls. Integer lint passes.

The identical saved hydrated Mymind source restores the main heading, copy and
actions. Its live canonical capture remains blank, so no page clearance is
claimed; that initialization cause is separate and unconfirmed. Only Mymind was
retried, with unchanged pixels/metrics. The report remains **162 high**, 1 critical
Dock blocker and904 low. RI-088 records an independent flow-restoration error.
See [round thirty-eight](comparison/repairs.html#round-38) and
`snapshots/visual_comparison_review_round38.json`. The user cleared the goal
during wrap-up; no further work is scheduled. Broad baselines and catalog capture
were deferred; references, URLs, annotations, budgets and clocks are retained.

The thirty-ninth review (2026-11-16) fixes **LI-066 / RI-094 / RI-095**:
individual translation calculations reuse typed transform validation and preserve
affine reference-box percentages; flex-assigned grids retain their allocated
height for tracks and percentage children. Draftbit's navigation is restored and
the hero no longer overlaps its header. Its phone preview renders in the identical
saved hydrated source; the live preview still reports a load failure, with cause
unisolated. Unsupported composite-mask tint remains under RI-041.

Only Draftbit's live Lambda capture was refreshed: mean error **21.33% →17.75%**,
load complete9.12s. It remains high; the severity-sorted report is **162 high**,
1 critical Dock blocker and904 low. Seventeen focused checks pass (seven PNG,
four native sizing, four transform animation/snapshot, two input validation),
plus independent Chromium geometry and integer lint. Broad baselines are deferred.
The goal is active again. See [round thirty-nine](comparison/repairs.html#round-39)
and `snapshots/visual_comparison_review_round39.json`. References, URLs,
annotations, thresholds, budgets, admission and capture clocks are unchanged.

The fortieth review fixes **RI-096**: static flex/grid items honor signed and
explicit-zero stack levels through the shared paint/hit predicates. Actual
formatting-parent checks preserve ordinary blocks, including those with authored
flex properties. PostHog's primary navigation is restored; its main window remains
absent with a separate unisolated cause. Only its live Lambda side was refreshed:
mean error **25.69% →25.02%**. No page clearance is claimed.

Fifteen focused PNG checks and three native paint/hit tests pass, plus seven
independent Chromium pixel controls and integer lint. The report remains
**162 high**, 1 critical Dock blocker and904 low. Broad baselines are deferred;
references, unselected rows, URLs, annotations, budgets and clocks are unchanged.
See [round forty](comparison/repairs.html#round-40) and
`snapshots/visual_comparison_review_round40.json`. The goal remains active.

The forty-first review fixes **RI-097**: real block stacking contexts collect
zero-level positioned descendants across ordinary wrappers while retaining
ancestor clips and reverse hits. PostHog’s main window, heading, signup panel,
actions and tabs render again. Its live mean error improves **25.02% →5.42%**;
visual review clears that major omission. The report is **161 high**, 1 critical
Dock blocker and905 low. Only PostHog’s live Lambda side was refreshed.

Twenty-two focused PNG controls and four native tests pass, plus seven independent
Chromium controls and integer lint. The standard release Radiant baseline ran
8,488 cases:8,167 passed,299 partial,20 skipped and two CSS3D tests failed.
The computed preserve-3d context boundary was then repaired; both failures now
pass, with all26 focused controls still green. The full baseline was not repeated
after that correction. No expected pixels or reference files were weakened.
See [round forty-one](comparison/repairs.html#round-41) and
`snapshots/visual_comparison_review_round41.json`. Original browser metadata,
references, unselected rows, URLs, annotations, thresholds and clocks are retained.
The goal remains active.

The forty-second review fixes **RI-098** (2026-11-17): rounded inset shapes now
publish constrained radii shared by vector paint, glyph scanlines and native
hits. Oversized circular radii no longer discard button labels. Twenty focused
PNG checks, three native tests and two independent Chromium equivalence controls
pass; integer lint passes. No broad baseline was repeated after round41.

The unchanged saved hydrated AndOpen source restores its navigation labels.
Its live capture still has separate initial hidden content, with unchanged
canonical pixels and metrics; no clearance is claimed. The report remains
**161 high**, 1 critical Dock blocker and905 low. Original browser data,
unselected rows, catalog, annotations, thresholds, admission and clocks remain.
See [round forty-two](comparison/repairs.html#round-42) and
`snapshots/visual_comparison_review_round42.json`. The goal remains active.

The forty-third review fixes **LI-067 / RI-099** (2026-11-17): numeric flex
shorthands retain their interoperable omitted0% basis, and native inline
backgrounds reuse the shared constrained rounded path. Explicit zero lengths
remain distinct. The current specification/browser disagreement is documented
in [CSSWG5742](https://github.com/w3c/csswg-drafts/issues/5742).

Cleanup.pictures improves **73.9942%→24.2868%**, restoring its live lower-section
heading and background. The identical saved hydrated page also restores its
hero. Its live client app remains absent and the page remains high; no
clearance is claimed. The inventory remains **161 high, 1 critical,905 low**.
Thirty-three focused PNG tests, one Input unit test and two native tests pass,
as do eleven independent Chromium controls and integer lint. No broad baseline
was repeated after round41. Original browser data, unselected rows, catalog,
annotations, thresholds and clocks are retained. See
[round forty-three](comparison/repairs.html#round-43) and
`snapshots/visual_comparison_review_round43.json`. The goal remains active.

The forty-fourth review fixes **RI-100** (2026-11-17): intrinsic button
classification and outer line-fill lookahead share the existing atomic
used-display context. Internal label indentation no longer wraps a fitting
search row. No width, margin, tolerance or site condition was changed.
Python.org improves **13.6723%→11.5233%**, but its live carousel content remains
absent. A manual high annotation preserves that large omission despite the
coarse threshold crossing. The inventory remains **161 high, 1 critical,905
low** (140 automatic plus21 manual); no page clearance is claimed.

Fifteen focused PNG checks, five native tests, four independent Chromium
controls and integer lint pass. No broad baseline was repeated after round41.
Only Python's Lambda capture and its new manual review annotation changed;
all prior annotations, browser data, other rows, catalog, thresholds and clocks
are retained. Cleanup's remaining absent app is now traced to its712,933-byte
deferred bundle exceeding the unchanged128KiB admission limit. Empathy's
server-side hero placeholder also requires client initialization; its hydrated
source already renders the real image. See [round forty-four](comparison/repairs.html#round-44)
and `snapshots/visual_comparison_review_round44.json`. The goal remains active.

The forty-fifth review fixes **LI-068** (2026-11-17): automatic/default
font-face weight now preserves the requested variable axis, and instance
caches include weight. Explicit fixed descriptors retain their clamp.
Affinity's headline weight is repaired; mean error improves from **14.6559%
to 8.8302%**. Four large corner images are still absent, so a manual
annotation retains its high severity. The inventory remains **161 high,
1 critical, 905 low** (139 automatic and 22 manual); no clearance is claimed.

Twenty-seven focused tests and four Chromium controls pass. A broad Radiant
milestone ran 8,488 checks: 8,167 passed, two failed, 299 partial and 20 skipped.
The two failures were addressed with a retained-box correction to RI-100 and
flex shorthand expectations aligned with LI-067's browser-compatible 0%.
The existing twelve text-overflow tests, flex cascade test, live button
transitions and eight font/button PNG controls pass afterward. The full
baseline was not repeated. Only Affinity's Lambda capture and its new review
annotation change; browser data, previous annotations, other rows, inventory,
thresholds and clocks are retained. See [round forty-five](comparison/repairs.html#round-45)
and `snapshots/visual_comparison_review_round45.json`. The goal remains active.

The forty-sixth review fixes **LI-069 / RI-101** (2026-11-17). Borrowed
pseudo-style refreshes now invalidate the generated box locally instead of
expiring unchanged authored host inputs during measurement. Real authored
mutations still invalidate ancestors; replay validity checks are unchanged.
Unregistered custom-layout names also retain ordered generated effects;
registered callbacks keep their deferred path.

The original saved Mymind source has **zero restoration errors instead of
three**. Its live capture remains blank and high severity, so the inventory
remains **161 high, 1 critical, 905 low** (139 automatic and 22 manual).
Seven PNG and four native controls pass on the final executable. Two existing
owned-flow/pseudo-font tests passed during implementation; three independent
Chromium equivalences and integer lint pass. No baseline was repeated after
round45. Only Mymind's Lambda capture was refreshed; browser data, annotations,
other rows, catalog, thresholds and clocks are retained. See
[round forty-six](comparison/repairs.html#round-46) and
`snapshots/visual_comparison_review_round46.json`. The goal remains active.

The forty-seventh review fixes **RI-102 / LI-070** and shared DOM comment
handling (2026-11-17). Stylesheet completion now reaches DOM listeners before
window load, and chained CSS discovery keeps the bootstrap mutation scope.
Clone/import and fragment parsing retain comments. Backed insertion preserves
the native position before a DOM-only comment reference.

Crates.io now renders its main page instead of a blank canvas. Its image error
falls from **54.2222% to 2.7368%**, clearing one major candidate. The inventory
is **160 high, 1 critical, 906 low** (138 automatic and 22 manual). Icons and
API/loading-state differences remain. Twenty focused rendering tests pass after the ownership correction; four
document/iframe tests, three Input tests and one native observer test passed
during implementation.
One release Radiant baseline ran: 8,140 passed, 10 failed, 299 partial and 8 skipped (8,457 total). The fragment refactor had borrowed parser text instead of retaining independent occurrence storage. Restoring owned copies clears all nine persistent failures in focused checks; all eight affected layout fixtures return to 100% element/text matches. The initial jQuery failure also passes a final direct check. The whole baseline was not repeated after correction, so a clean full gate is not claimed.
Only crates.io's Lambda capture was refreshed; browser data, annotations,
other rows, catalog, thresholds and clocks are retained. See
[round forty-seven](comparison/repairs.html#round-47) and
`snapshots/visual_comparison_review_round47.json`. The goal remains active.

The forty-eighth review restores the isolated MVP host's missing **console.table
and console.dir** bindings (2026-11-17). Existing single-value output is shared;
full tabular formatting is not added. Both new controls fail before and all
three focused console rendering tests pass after the repair.

Squarespace's startup exception disappears, but its canonical PNG is
**byte-identical** and its hero remains missing. Current diagnostics show visible computed target styles and
observer callbacks; the remaining painted-frame cause is not proven.
The inventory remains **160 high, 1 critical, 906 low**. No baseline was repeated
after the round47 milestone. Browser data, pixels, annotations, other rows,
catalog, thresholds and D10.1.7 clocks are unchanged. See
[round forty-eight](comparison/repairs.html#round-48) and
`snapshots/visual_comparison_review_round48.json`. The goal remains active.

Round forty-nine fixes **RI-103**, a final flex-content pass that discarded a
grid-assigned height before resolving absolute child percentages. The unchanged
saved Bonsai page improves from **12.6736% to 0.6932%** image error, with full-height
portrait cards. All **35 focused grid/flex/absolute-height tests pass**; integer
lint passes. No baseline was repeated after the round47 milestone.

Bonsai's live main content still remains absent, so an explicit manual review
retains it as high severity despite crossing the coarse threshold. The inventory
remains **160 high, 1 critical, 906 low** (137 automatic and 23 manual). Browser
references, other rows, catalog, thresholds and D10.1.7 clocks are preserved.
See [round forty-nine](comparison/repairs.html#round-49) and
`snapshots/visual_comparison_review_round49.json`. The goal remains active.

Round fifty repairs **zero-area and edge-contact IntersectionObserver sampling**
(2026-11-17). Empty rendered containers, collapsed transforms and viewport-edge
contacts now notify correctly while hidden, detached and offscreen controls
remain false. **16 focused tests pass**; Chromium confirms all twelve new fixture
expectations, including content populated after an empty container intersects.
No baseline was repeated after round47. The separate percentage-margin
hypothesis was rejected and reverted after Chromium contradicted it.

Calendly's latest capture restores its heading, copy and signup actions. A
previous-pin control is byte-identical, so this clearance predates the current
observer repair. Its stale manual omission is removed. Sanity's site content
has changed from its original browser reference, and Primer remains major.
The inventory is **159 high, 1 critical, 907 low** (137 automatic, 22 manual).
Only these three Lambda captures changed; browser references, other rows,
catalog, thresholds, budgets and D10.1.7 time are retained. See
[round fifty](comparison/repairs.html#round-50) and
`snapshots/visual_comparison_review_round50.json`. The goal remains active.

Round fifty-one fixes **RI-092**: both intrinsic measurement paths now retain
definite child flex bases, and nested content preserves non-growing and zero
parent allocations. The original 100px-versus-300px failure now matches its
saved browser geometry at **100% elements/text**. All **36 focused native tests
pass**, including thirteen Chromium-confirmed sizing states; integer lint passes.
One release Radiant baseline passes: 8,169 passed, zero unexpected failures, 299 partial and 20 skipped (8,488 total). Existing expected visual failures remain recorded; the gate was not repeated.

Six earlier pin50 captures were refreshed (Monese, Mode, Infermedica, Fable, Dia,
Openunit): four PNGs are unchanged and two move slightly. None clears a major
flag, and no improvement is attributed to the subsequent layout repair.
The inventory remains **159 high, 1 critical, 907 low**. Browser references,
annotations, other rows, catalog, tolerances, budgets and D10.1.7 time are kept.
A confirmed [dynamic script preparation gap](../../vibe/Lambda_Issue_Ledger.md#online-dynamic-script-preparation)
can leave a completed-XHR startup cover in place. The full insertion lifecycle
is a larger missing feature; no partial evaluator or unhide is introduced.
See [round fifty-one](comparison/repairs.html#round-51) and
`snapshots/visual_comparison_review_round51.json`. The goal remains active.

Round fifty-two adds the missing **StorageEvent** interface that aborted the
Coda/Superhuman consent bootstrap. Five focused native checks and the three new
Chromium headless-shell controls pass. Both fresh Lambda captures load; a later
bootstrap TypeError remains, so neither page is cleared. The report remains
**159 high, 1 critical and 907 low**. No broad baseline is repeated after round51.
Qualification and the next reduced exception are in
`temp/online-visual-round52/`; see
[the repair record](comparison/repairs.html#round-52) and
[the runtime ledger](../../vibe/Lambda_Issue_Ledger.md#online-storage-event).

Round fifty-three fixes Window.document's own readonly accessor. Six focused
native checks and the Chromium descriptor control pass; the consent bootstrap
now initializes on Coda/Superhuman. Their refreshed images remain unchanged,
so the inventory remains **159 high, 1 critical and 907 low**. No broad baseline
is repeated. See [the repair record](comparison/repairs.html#round-53) and
[the runtime ledger](../../vibe/Lambda_Issue_Ledger.md#online-window-document-accessor).

Round fifty-four refreshes **two stale live-site reference pairs** after
archived/current HTTP HTML proves changed primary headings and designs. Sanity
and Storetasker now compare fresh Lambda captures with fresh Chromium headless
shell captures using the same procedure. Their image errors are **9.85%** and
**6.11%**; both leave the major queue. This is comparison maintenance, with no
renderer repair attributed. Prior references, source hashes and the rationale
are retained in [reference_updates.json](comparison/reference_updates.json) and
[the review gallery](comparison/repairs.html#round-54). The inventory is now
**157 high, 1 critical and 909 low**. Other pairs, annotations, thresholds and
capture clocks are unchanged; no build or baseline was rerun.

Round fifty-five fixes the existing resource-timing interface: fetch/XHR
entries now have the PerformanceResourceTiming/PerformanceEntry hierarchy,
branded getters and private metadata. Six native checks and the real HTTP
fixture in AST/MIR pass; Chromium passes the same HTTP fixture. Clay's captured
bootstrap now initializes, but both refreshed live images remain unchanged.
The inventory stays **157 high, 1 critical and 909 low**. Separate startup
errors remain; no visual clearance is claimed. No broad baseline was repeated.
See [the repair record](comparison/repairs.html#round-55) and
[the runtime ledger](../../vibe/Lambda_Issue_Ledger.md#online-resource-timing-brand).

Round fifty-six fixes **RI-104**, intrinsic margins resolving variables/font
units on the parent instead of the measured child. Astra's collapsed hero is
restored, reducing mean image error **26.63% → 5.27%**. The inventory is now
**156 high, 1 critical and 910 low**. Five focused native cases, seven Chromium
geometry states and layout integer-cast lint pass. Only Astra's Lambda capture
is refreshed; browser references, all other records, catalog and thresholds
are retained. No broad baseline was rerun. The user cleared the goal during
the build; this repair is wrapped up and further goal work has stopped.
See [the before/after gallery](comparison/repairs.html#round-56).

Round fifty-seven fixes **LI-072**, pending substitutions mixed with other
font-family entries. Literal family validation now follows substitution and
both parser entry points share existing grouping. All **205 Input tests** and
**three native font/margin cases** pass; Chromium confirms admission, font
selection, invalid variables and live changes. Four selected live captures
remain major: **156 high, 1 critical and 910 low**. Cofounder's first attempt
lacks media; matching later base/fixed controls are byte-identical, and the
first attempt is retained with that readiness limitation recorded. Browser
references, other rows and thresholds are unchanged. No broad baseline was
rerun. See [the repair record](comparison/repairs.html#round-57).

Round fifty-eight fixes **LI-073 / RI-107**, initial stylesheet completion
lost when byte-only prefetch was reused without its element consumer.
Contentsquare's hero now renders, moving high to low: mean difference
**35.11% → 15.38%**. Four selected live captures leave **155 high, 1 critical
and 911 low**. Six native resource-manager controls and four final HTTP
regressions pass; the first candidate also passes all fourteen adjacent HTTP
resource cases. Chromium confirms the four fixture outcomes. No broad baseline
was repeated. Browser references, other rows, annotations and thresholds are
preserved. See [the repair record](comparison/repairs.html#round-58).

Round fifty-nine fixes **RI-108**, final grid percentage-height definiteness
and stale intrinsic image-ratio constraints. The Cape's saved hydrated page now
paints its large illustration; the live hero still remains absent, retaining
**155 high, 1 critical and 911 low**. Five focused native tests pass, including
**54 retained states** independently confirmed by Chromium. One configured
layout sweep exposed three sizing regressions; after correction, the affected
**776-case sizing category passes**. Other categories passed the initial sweep;
the full sweep was not repeated. No full native Radiant/Lambda baseline is
claimed. Browser references, other rows, annotations and thresholds are
preserved. See [the repair record](comparison/repairs.html#round-59).

Round sixty fixes **JS07-L1, JS13-L8 and JS13-L9**: native superclass static
writes retain the public class receiver, `Element.hasAttributes()` supports
live namespaced attributes, and imported fragments initialize nested custom
elements before property binding. Text/comment imports also reach their
existing clone support. **Five focused runtime and four focused document tests
pass**, with Chromium controls covering 16 attribute and 18 import/lifecycle
states. Archive.org's module and root template now complete, but its shadow
content remains unpainted. The inventory stays **155 high, 1 critical and
911 low**. Only its Lambda attempt is refreshed; browser references, other
rows, catalog, annotations and thresholds are preserved. No broad baseline
was run. See [the repair record](comparison/repairs.html#round-60).

Round sixty-one confirms **LI-026's Calmind visual repair** in the inventory.
The current renderer restores the first-party stylesheet and hero; mean error
falls from **34.26% to 0.80%**, clearing high severity. The existing fresh capture
is promoted without a duplicate render. The inventory is now **154 high,
1 critical and 912 low**. Its separate generic script exception remains open.
This round adds no engine source change or repeated baseline; round-60 source
and focused-test qualification is preserved. Browser references, other rows,
annotations and thresholds remain unchanged. See [the repair record](comparison/repairs.html#round-61).

Round sixty-two fixes **RI-109** (relative document bases and URL identity),
**LI-074** (head-template closure) and **RI-110 / JS13-L10** (inert template
styles, scripts and dependencies). Nine focused native document tests, three
Input parser tests, two persistent HTTP regressions and integer lint pass.
Chromium agrees on five base fixtures and the inert HTML/active SVG control;
its malformed-base fallback discrepancy is retained explicitly. No broad
baseline was repeated. MySQL clears high severity (24.46% to **1.24%** mean
error). Unreal now displays styled documentation, but Chromium shows a 403
application route; its content discrepancy remains high by manual review.
The inventory is **153 high, 1 critical and 913 low**. Browser references,
unselected captures, catalog, timing and resource budgets and thresholds are
preserved. See [the repair record](comparison/repairs.html#round-62).

Round sixty-three fixes **RI-111**: canonical inline-style sharing omitted
isolation, ruby position and explicit line-strut state. The shared identity
field list now preserves these distinctions. Five focused style-sharing unit
tests and eleven related paint fixtures pass; two new isolation fixtures fail
before the fix, and three Chromium cases confirm the expected behavior.
Operate's main interface and gray panels return, clearing high severity:
**60.27% to 2.33%** mean error. The inventory is **152 high, 1 critical and
914 low**. Browser references, other rows, catalog, annotations and capture
budgets remain unchanged. No broad baseline was repeated. The persistent goal
was cleared by the user; this confirmed fix was qualified and wrapped up.
See [the repair record](comparison/repairs.html#round-63).

Round sixty-four fixes **RI-112**: fixed/absolute document roots were deferred
by the raster normal-flow walker with no parent stacking pass to paint them.
The tree entry now calls the existing direct stacking dispatcher. Four new
cases fail before the fix; all **24 focused paint/overflow fixtures** pass
after it, and Chromium confirms the five new controls. The exact saved Dayos
source improves **99.01% to 0.77%** mean error. Dayos's live capture still
shows its authored yellow startup overlay, so it remains high severity.
The inventory stays **152 high, 1 critical, 914 low**. Browser references,
other comparison rows, catalog, annotations and capture budgets are preserved.
Broad baselines were deferred; the goal remains active.
See [the repair record](comparison/repairs.html#round-64).

Round sixty-five fixes **RI-113**: preferred aspect ratios ignored dimensionless
CSS math. The shared resolver now evaluates number components using the existing
math engine, including calculated numerator/denominator values. Remote's large
SVG logo returns on the exact saved source: **11.27% to 0.75%** mean error.
Two new pixel cases fail before the fix; all nine related fixtures, two focused
native tests and integer lint pass afterward. Remote's live capture still lacks
its hydrated hero, so it remains high. The sorted inventory stays **152 high,
1 critical, 914 low**. Browser references, other rows, catalog, annotations,
thresholds and capture clocks remain unchanged. Broad baselines were deferred.
See [the repair record](comparison/repairs.html#round-65). The goal remains active.

Round sixty-six fixes **RI-114**: the shared flex/grid positioned-child layout
pass missed boxes beneath display:contents wrappers. It now reuses the existing
flattened-box collector. Superlist's fixed navigation returns. Four new cases
fail before the fix; all **29 related paint fixtures** and integer lint pass.
The accumulated three repairs pass one broad Radiant milestone: **8,169 passed,
299 partial, 20 skipped; no failures out of 8,488 checks**.

Superlist's current HTTP heading/title and light design differ from the archived
dark site. Its browser reference is refreshed under the existing audit policy;
prior images, source hashes and both attempts are retained. This is reference
maintenance, separate from the engine repair. A large app image still has a
zero-height ratio container, so a new manual annotation keeps Superlist high.
The inventory remains **152 high, 1 critical, 914 low** (129 automatic and 23
manual candidates). Other rows, previous annotations, catalog, clocks, budgets
and thresholds remain unchanged. See [the repair record](comparison/repairs.html#round-66)
and [the reference audit](comparison/reference_updates.json). The goal is active.

Round sixty-seven fixes **RI-115**: flex refinement saved heights from flattened
items but restored them using direct DOM children, losing ratio-box heights
through display:contents. Both phases now use the existing item iterator.
Superlist's large app image returns in its saved source and live capture,
clearing its temporary manual high annotation. Its current error is **1.79%**;
headline wrapping and minor rendering details remain. Two wrapper cases fail
before the fix while the direct control passes; all **25 focused ratio and
positioning fixtures** and integer lint pass afterward. The broad round66
baseline passed before this follow-up and was not repeated.

The inventory is **151 high, 1 critical, 915 low**. Other rows, previous
annotations, browser references, the round66 reference audit, catalog, clocks,
budgets and thresholds remain unchanged. See [the repair record](comparison/repairs.html#round-67).
The goal remains active.

Round sixty-eight fixes **RI-116**: lifted paint layers replayed intermediate
absolute ancestors using the normal DOM parent's origin. Raster and shared
vector replay now apply the existing containing-block offset helper at those
ancestors, including the raster clip origin. One new absolute case fails before
the fix, its fixed stacking-context control passes, and Chromium confirms both.
All **38 related paint fixtures** pass afterward; SVG output moves the witness
from y200 to y0. Exact-source Readymag error improves **41.50% to 9.87%**.
Its current live capture still has a blank startup state, so it remains high.
The inventory stays **151 high, 1 critical, 915 low**. Browser references,
other rows, annotations, catalog, clocks and budgets are preserved. The broad
round66 baseline preceded these local follow-ups and was not repeated.
See [the repair record](comparison/repairs.html#round-68). The goal is active.

Round sixty-nine diagnoses **RI-117**, an invalid-mask interoperability
difference. Readymag's SVG references a nonexistent mask. Native transparent
coverage follows CSS Masking §7.1; Chromium paints the card. Four controls
separate missing-mask behavior from drop-shadow effects. The native rule is
retained; no renderer, canonical capture, metric, budget or reference changes.
The inventory remains **151 high, 1 critical, 915 low**.
See [the diagnosis](comparison/repairs.html#round-69). The goal is active.

Round seventy refreshes the **eight remaining high-severity Framer pages**
with the qualified round68 renderer and two workers. All eight load and capture
successfully. Visual review retains all eight: large headings, applications,
media or background video remain missing. Pitch News crosses below the coarse
threshold without restoring its main content, so a new manual annotation keeps
it high. No additional engine repair or page clearance is claimed.

The inventory remains **151 high, 1 critical, 915 low** (128 automatic, 23 manual).
Only the eight Lambda attempts and that annotation change; browser references,
previous annotations, catalog, reference audit, clocks and budgets remain intact.
No build or baseline was repeated. See [the targeted review](comparison/repairs.html#round-70).
The goal remains active.

Round seventy-one fixes **RI-118**: flex content-only intrinsic measurements
resolved descendant percentages against a preferred width that the query
should ignore. The shared percentage-basis helper now reads the existing query
mode before cache lookup. Orum's saved hero columns and right image recover.
Three reduced cases fail before; a smaller fixed-width control passes. All
**55 focused flex/intrinsic PNG cases**, the new warm-cache native regression
and integer lint pass afterward; Chromium confirms the allocation. Two older
selected core tests fail identically before and after and remain unchanged.

The accumulated follow-ups pass a second broad Radiant milestone: **8,169
passed, 299 partial, 20 skipped, no failures out of 8,488**. Both old and new
renderers produce blank current live Orum captures, so its independent startup
remains unresolved. Inventory: **151 high, 1 critical, 915 low**. Browser
references, other rows, annotations, catalog, audit, clocks and budgets remain
unchanged. See [the repair record](comparison/repairs.html#round-71).
The goal remains active.

Round seventy-two fixes **RI-119**: an absolute underline made intrinsic
height classification stack Overpass's inline letters as blocks. Classification
now excludes out-of-flow children using the existing helper. Independent flow
roots also use the existing inline-child classification without requiring line
clamping. Three reduced cases fail before, two controls pass, and Chromium
confirms all five. All **42 focused flex/inline PNG cases** and integer lint
pass afterward. Exact-source Overpass error improves **18.22% to 0.47%**.

Its live hero stays blank, as before the repair, so Overpass remains high.
Proof/Podia image omissions vanish in unchanged warm captures and do not
justify a layout patch. Inventory remains **151 high, 1 critical, 915 low**.
Browser references, other rows, annotations, catalog, audit, clocks and budgets
are unchanged. The passing round71 broad checkpoint is not repeated.
See [the repair record](comparison/repairs.html#round-72). The goal is active.

Round seventy-three fixes **RI-120**: SVG height="auto" was marked as a
natural 150px height after parsing failed. The metadata reader now uses the
existing parse-success contract; nonnumeric dimensions do not invent natural
axes. Wispr's large path regions recover, improving saved-page error
**15.30% to 2.43%**. Three new cases fail before; two controls pass. Chromium
confirms all five; all **34 focused SVG/ratio/flex PNG cases** and integer
lint pass afterward. The preceding broad checkpoint is not repeated.

The current live capture still lacks its heading, paragraph, navigation and
path text. A new manual annotation retains this major omission despite a
lower coarse score. Inventory remains **151 high, 1 critical, 915 low**
(127 automatic, 24 manual). Browser references, other rows, prior annotations,
catalog, audit, clocks and budgets are preserved.
See [the repair record](comparison/repairs.html#round-73). The goal is active.

Round seventy-four fixes **LR12-55** in the shared DOM: TreeWalker discarded
its filter, so Wispr's quote-cleanup script deleted every text node. Function
and object filters now run after the mask, distinguish rejection from skipping,
and retain proper receivers, traversal bounds, exception cleanup and reentrancy.
Precise roots protect callbacks and wrappers. The readonly filter preserves
identity. A new filtered native entry is appended; all **461 published catalog
entries** keep their signatures and positions.

Three new semantic regressions and an existing unfiltered control pass on the
final runner; Chromium confirms the three new scripts. The fast baseline keeps
its curated 201 document cases; the new tests run separately.
The accumulated fixes pass the Radiant milestone: **8,169 passed, 299
partial, 20 skipped, no failures out of 8,488**. Wispr's live text
returns and its error is **0.85%**, clearing its temporary manual annotation.
Final capture metrics are checked directly against the unchanged comparison
row, avoiding another full image recomputation.

Inventory: **150 high, 1 critical, 916 low** (127 automatic, 23 manual).
Other rows, prior annotations, browser references, catalog, audit, clocks and
budgets remain intact. See [the repair record](comparison/repairs.html#round-74).
The goal remains active.

Round seventy-five refreshes **six other high-severity pages with filtered
TreeWalker calls** using the qualified round74 renderer: Agiloft, Xolo Teams,
Swag, Orum, Circle and Armadillo. All six load and capture. Five remain largely
blank; Swag retains substantial consent/page-state differences. These captures
do not establish filtering as their remaining root cause, and no further page
clearance or engine repair is claimed.

Inventory remains **150 high, 1 critical, 916 low**. Only the six native
attempts change. Wispr's existing comparison timestamp is synchronized with
its final attempt; its image, score and cleared severity are unchanged. Other
rows, browser references, annotations, catalog, audit, clocks and budgets remain
intact. No build or baseline is repeated.
See [the targeted review](comparison/repairs.html#round-75). The goal is active.

Round seventy-six fixes **RI-121**: media source requests were created during
layout after snapshot resource waiting. Discovery now reuses existing media
preparation before that wait, including local HTML's first remote-source manager.
Source selection, preload:none/autoplay and audio permission policy remain
shared. No timeout, clock or site condition changes. Four new cases fail before,
one defer control passes; all **34 focused CSS-resource/video checks** and
integer lint pass afterward. The preceding broad checkpoint is not repeated.

Five current native captures load. Going's large video remains absent; complete
downloaded-file playback is supported, progressive/range playback remains open.
Origin's existing manual annotation keeps its missing main content high.
Optimizely and Pitch remain high. No further engine-driven clearance is claimed.

Holocene's changed HTTP title/heading and white design require a separately
audited browser-reference refresh. Its current pair is **0.52%** error and
leaves high through reference maintenance. Old images and source hashes remain
in [the reference audit](comparison/reference_updates.json).
Inventory: **149 high, 1 critical, 917 low** (126 automatic, 23 manual).
Other rows/references, all annotations, catalog, clocks and budgets are preserved.
See [the repair record](comparison/repairs.html#round-76). The goal is active.

Round seventy-seven fixes **RI-123**: intrinsic height discarded the enclosing
line strut when atomic inline content could not be flattened as text. The failed
collection now reuses the existing inline formatter and fragment restoration.
Optimizely's 222.3px heading no longer receives a 117px parent, correcting the
105.3px shift in its copy, background and following section.

Six reduced cases cover short/tall line height in block/flex/grid parents;
Chromium confirms all six and the old release fails the tall-grid case.
All **22 focused inline-height/break checks** and integer lint pass. The
preceding broad checkpoint is retained without another full baseline run.
Optimizely's identical-source error falls **13.56% → 1.44%**; its normal live
error falls **14.43% → 2.53%** and it leaves high severity. Smaller letter
styling differences remain.

Nolla's named view-progress timeline/ranges are a major missing subsystem,
recorded as **RI-122**; its qualified same-source error is 29.85% and it remains
high. No timeline suppression or timing exception is introduced.

Inventory: **148 high, 1 critical, 918 low** (125 automatic, 23 manual).
All other report rows, browser references, annotations, URL list, clocks and
budgets remain intact. See [the repair record](comparison/repairs.html#round-77).
The goal remains active.

Round seventy-eight fixes **RI-124**: a strict atomic-inline fit test treated
about 0.0000305px of float roundoff as real overflow. Python's search form/social
group added a line and shifted the page by about 46px. The existing float-edge
comparator is now shared by atomic fit decisions; no pixel allowance or site
condition is added. Six reduced cases cover inline-block/flex/grid fits and
genuine overflow. Before: three failures and three controls; Chromium confirms
all six. After: **32 focused checks** and integer lint pass.

Python's saved-source difference falls **10.8% → 0.27%**. Its normal capture
loads and scores **4.20%**, but the live carousel content remains absent; the
existing manual high review is retained. This does not claim a page clearance.

A temporary optimized trace ruled out Jetty's responsive-source mismatch:
the correct image is queued but still pending at the existing wait. Jetty,
Squarespace and Pelago show incomplete resources at that point. Trace source
changes were removed before the final build. No capture budget or priority
exception is introduced. Known Leaflet top-layer, Draftbit composite-mask and
WebKit initial-fade differences are retained. Twenty-six saved pairs were
reviewed; most other saved layouts are already close.

Inventory remains **148 high, 1 critical, 918 low** (125 automatic, 23 manual).
All other report rows, browser references, every annotation, catalog and capture
policy remain intact. The preceding broad checkpoint is retained without
another full baseline run. See [the repair record](comparison/repairs.html#round-78).
The goal remains active.

Round seventy-nine fixes **RI-127** (render export ignored the existing selected
post-load window, including script-free CSS frame preparation) and **RI-128 /
LR12-56** (background keyframes discarded the supported color component before
native parsing). Variables, omitted-color reset, source order and important
exclusion use the shared parser; Web Animations explicit seeks use the same
target admission. Other background components and automatic Web playback remain
outside this repair. Defaults still select the initial export frame (D10.1.7).

All **29 focused checks** and integer lint pass. A saved pre-repair renderer
fails the shorthand explicit seek while passing its longhand control. The
Radiant milestone gate passes **8,169 / 8,488, 299 partial,
20 skipped, zero failures**. No full Lambda language baseline is repeated.

Python and WebKit are paired at the new opt-in **1000ms** window. Python's
carousel returns in both captures (**0.65%**), resolving its manual review while
preserving its history. WebKit's grid returns, but large article images remain
missing (**7.11%**); a manual review keeps it high. This is separately audited
[capture-frame calibration](comparison/capture_frame_updates.json), not an
unchanged-policy engine-driven clearance. Live content can also change.
Previous pairs are archived; the design-change audit is unchanged.

Inventory: **147 high, 1 critical, 919 low** (124 automatic, 23 manual).
All other 1,065 report rows, catalog, default clocks, deadlines and thresholds
remain intact. See [the repair record](comparison/repairs.html#round-79).
The goal remains active.

Round eighty fixes **RI-129**: intrinsic inline-image flow discarded the measured
box's resolved width and ratio-derived percentage-height basis. Clay's 95px
logo strip became 1494.8px and pushed its pricing cards below the viewport.
The existing formatter now retains those axes without a ratio clamp or site
condition. Six reduced cases cover two ratios, fixed/block-image controls,
automatic height and content growth. Before: three fail/three pass; Chromium
confirms all six. After: **50 focused checks** and integer lint pass. The
preceding round79 broad gate is retained without another full suite.

The identical saved source restores the logos and four pricing cards
(**12.54% → 10.37%**). Smaller heading/spacing details remain. Clay's normal live
capture stays blank/high (**12.61%**): an authored anti-flicker class waits
4000ms, while the unchanged export frame is zero. Separate script TypeErrors
are observed but not attributed to this sizing bug. No timing/style exception,
reference refresh or page clearance is claimed.

Inventory remains **147 high, 1 critical, 919 low** (124 automatic, 23 manual).
All other 1,066 rows, browser images, annotations, catalog, audits and capture
policy remain intact. See [the repair record](comparison/repairs.html#round-80).
The goal remains active.

Round eighty-one completes **LI-078** in the existing shared color engine:
absolute Lab and rectangular Lab mixing now retain their computed model through
transport/serialization and used sRGB conversion. Weight/alpha/currentColor
handling is shared; Bradford adaptation serves both conversion directions.
Rogo's authored Lab feature query can now select its subtle sRGB gradient
instead of its bright white fallback. No feature-test or site exception is used.
LCH, relative colors, wider spaces, gamut mapping and Lab gradient interpolation
remain outside this increment.

Before: all five new paint cases fail; Chromium confirms all five. After:
**15 focused render/condition/stylesheet checks, 10 input parser color checks
and 10 shared color-library checks pass**. Existing assertions/tolerances are
unchanged. The preceding round79 milestone is retained without another full
baseline run.

Rogo's identical-source error falls **13.28% → 1.90%**; the live background is
repaired (**14.64% → 3.97%**). Navigation, most of the headline, copy and action
still do not appear, so a new manual review keeps it high. No clearance is
claimed. Inventory remains **147 high, 1 critical, 919 low** (123 automatic,
24 manual). All other 1,066 rows, prior reviews, browser images, catalog,
audits, clocks and budgets remain intact. See
[the repair record](comparison/repairs.html#round-81). The goal remains active.

Round eighty-two fixes **RI-130**: the text-background painter checked only the
immediate text parent, so transparent text lost a gradient owned by an ancestor.
Raster/vector recording now shares an owner traversal with owner-space offsets
and outer-to-inner paint order. It reuses existing glyph masks and layer paint;
no fallback color or page condition is introduced.

Eight reduced cases have **five failures / three passing controls before** and
independent Chromium confirmation. After: **31 focused raster checks, four SVG
checks and integer lint pass**. The preceding round79 broad gate is retained;
PDF and wider fragment/descendant-transform behavior are not independently
qualified.

Wiza's identical saved-source heading and copy return (**6.23% → 4.77%**).
Its normal live capture remains blank/high, so no page clearance is claimed.
Inventory remains **147 high, 1 critical, 919 low** (123 automatic, 24 manual).
Other rows, references, reviews, catalog, audits and capture policy are intact.
See [the repair record](comparison/repairs.html#round-82). The goal stays active.

Round eighty-three fixes **LR12-58**: positive selected windows establish CSS
timelines before draining, and initial/pending loader geometry layouts share a
temporary document binding. A later reflow had restored authored values while
CSS effect sampling saw no host document. Color, width and opacity regressions
are repaired; default/zero frames and no-fill restoration are preserved
(**D9.5.6v2 / D10.1.7**).

**RI-131** fixes native stack exhaustion when a balance line-count probe
reentered balancing for its own atomic inline-flow owner. Nested/sibling owners
remain independent. **LR12-59** makes host termination unwind event dispatch
(**D7.4.3**), while ordinary listener exceptions still allow later listeners.
Vooma and Squarespace now write PNGs; Squarespace remains visually high and its
admitted app still reaches the existing CPU budget. **RI-132** corrects three
baseline regressions from the preceding intrinsic-width change: callers that
already selected their content width now use the existing assigned-width flag.
All three frozen reference cases pass without reference/threshold changes.

**45 focused checks pass**: twenty selected-frame checks with fourteen Chromium
controls, four atomic-balance checks, two interrupt controls, sixteen harness
checks and three lifecycle checks. Integer lint passes. One fresh milestone
Radiant gate passes **8,169/8,488, 299 partial, 20 skipped,
zero failures**, with its curated selection unchanged. The first milestone run
exposed the three regressions above; its evidence is preserved, and the gate
is repeated once after fixing them and relinking the runners.

The online command now defaults to **1000 ms on both renderers**. Changed-window
failed-only retries refresh the counterpart when both renderers are selected.
Native export defaults, deadlines, admission budgets and thresholds are intact.

All **147 prior high pages** were attempted: **145 paired refreshes** are
published, **2 calibrations remain pending** after a bounded retry and
retain their previous successful pairs. Failed attempts are recorded separately.
Old images/hashes and review history remain in the frame-calibration audit;
the URL list and design-change audit are unchanged.

**19 calibration clearances** follow visual review. They are not counted as
unchanged-policy engine clearances. Major omissions remain high even below the
coarse threshold, and rechecked review text reflects the latest pair while
preserving earlier assessments. Inventory: **128 high, 1 critical,
938 low** (102 automatic, 26 manual). The 920 unselected report
rows remain intact; earlier low pairs retain historical timing.
See [the repair record](comparison/repairs.html#round-83). The goal stays active.

Round eighty-four fixes **RI-133**: valid SVG images beginning with XML comments
were rejected by shared signature detection. Bounded comment skipping now admits
them through the existing loader. **18 focused checks pass** (six prefix cases,
twelve intrinsic/percentage controls); two valid-prefix cases fail on the saved
renderer. Live WebKit's three SVG signature rejections are gone.

Its two large article photos remain high: they are queued correctly, but valid
HTTP 200 downloads take 2.45/2.71 seconds, beyond the existing one-second resource
window. One native WebKit capture is refreshed at its existing 1000 ms frame;
its browser reference is retained. No waiting/admission policy is changed.
Superhuman's unsupported color paths and Nightwatch's remaining animation/hero
mismatch are investigated without site overrides or claimed clearances.

Inventory remains **128 high, 1 critical, 938 low**. The 1066 unselected report
rows, URL list and both reference audits are unchanged. The fresh round83 broad
gate is retained; only focused checks run for this narrow repair. See the
[repair record](comparison/repairs.html#round-84). The goal remains active.

Round eighty-five fixes **LR12-60**: dataset writes now reuse setAttribute's
CSS-dependency-aware metadata helper. Ten unrelated metadata writes cause one
layout instead of eleven, retaining observer records/old values and normal
CSS-dependent updates. **Seven focused checks pass**; the saved renderer fails
two reduced metadata cases. Temporary traces are removed, and no budget,
threshold or admission policy changes. The recent round83 broad gate is retained.

Dock writes a PNG in **149.0 seconds** within its unchanged 150-second deadline,
but still reaches the script CPU limit. This single success has about one second
of headroom; capture reliability is unproven. Its main content remains absent,
so it moves from critical to high. A matched 1000 ms browser reference and the
old failed attempt/reference are recorded in the frame audit.

Inventory: **129 high, zero critical, 938 low**, with **1067 comparisons**. The
extra high case is the previously critical Dock page; all 128 prior high rows and
1066 unselected report rows are unchanged. No high-severity clearance is claimed.
URL/review/design audits and other references stay intact. See the
[repair record](comparison/repairs.html#round-85). The goal stays active.

Dock’s recorded 1000 ms window is requested on both sides; script interruption
means native completion of that interval is not guaranteed. Its partial capture
remains high and is not used as a timing-parity clearance.

Round eighty-six fixes **LR12-61**: rewriting an identical data/aria value now
retains geometry even when CSS references that attribute. Changed values still
invalidate normally, and observers/custom-element callbacks remain active.
**Nine focused checks pass**. Ten unchanged writes drop from eleven layouts to
one; callback-driven width changes remain observable from 110 to 200 pixels.

The single current-renderer Dock retry times out at the unchanged 150-second
limit. Its prior successful round85 comparison is retained, with the new failure
and renderer pin [recorded separately](snapshots/comparison_review/round86-dock-retry.json).
No reliable capture-time improvement or visual clearance is claimed. Inventory
remains **129 high, zero critical, 938 low**, based on last successful captures;
all 1067 rows/pairs, manifest fields and audits are unchanged. No comparator or
broad baseline is repeated for this focused adapter change. See the
[repair record](comparison/repairs.html#round-86). The goal stays active.

Round eighty-seven fixes **RI-134**: video snapshots rejected an asynchronous
player's LOADING state before asking the existing decoder for an available
first frame. Complete-file first frames now paint while playback opens, keeping
poster precedence, immutable pixels and existing clocks. **21 focused checks
pass**; both paused/autoplay pixel regressions fail on the saved renderer.
No codec, wait, streaming implementation or resource policy is added.

Mymind's large halo is restored against its preserved 1000 ms browser reference.
Its major review is resolved; minor spacing/playback-overlay differences remain.
Inventory: **128 high, zero critical, 939 low**. Only its native capture/review
changes; all 1066 unselected rows, browser references, URL inventory and both
reference audits are preserved. The recent round83 broad gate is retained.
Dock's last tested retry remains failed, with its older successful pair retained
under round86's explicit qualification. See the
[repair record](comparison/repairs.html#round-87). The goal remains active.

Round eighty-eight fixes **RI-135**: retained descendants did not refresh
inherited text shadows after an early layout. Implicit inheritance and explicit
`inherit`/`unset` now copy the parent's computed shadows into owned storage,
preserving parent-relative offsets and child overrides (**D5.5.1**).
**17 focused native checks pass**; six new cases fail on the saved renderer.
Independent Chromium controls agree with all nine new cases; layout lint passes.

Boldare's entire outlined headline returns in an unmodified live capture at
its existing 1000 ms request. The major manual review is resolved with history
retained. Mean error is **1.13%**, changed area **2.32%**; small animated
decorations remain outside this major-only pass. Inventory: **127 high, zero
critical, 940 low**. All 1066 other rows, browser images/metadata, URLs and both
reference audits are preserved. No admission budget or capture policy changes.
The recent round83 broad gate is retained. Dock's last tested retry still
failed; its older successful pair remains under round86's explicit qualification.
See the [repair record](comparison/repairs.html#round-88). The goal remains active.

Round eighty-nine fixes **RI-136**: flex items flattened through boxless DOM
wrappers now use the actual flex box's padding/border offsets. Affinity's
navigation is readable and aligned. **20 focused native checks pass**; all
eight new controls fail on the saved renderer and agree with Chromium.
Layout integer-cast lint passes; the round83 broad gate is retained.

Affinity remains high because four large artwork panels are still absent.
Their cached files do not reveal the initially opacity-zero wrapper; required
deferred bundles exceed current admission (LR12-57). No unhide, admission or
capture-policy change is adopted. Its current native capture uses the existing
1000ms request; browser data and all1066 other rows remain unchanged. Inventory:
**127 high, zero critical, 940 low**. Both reference audits are preserved;
Dock's latest tested failure remains explicitly qualified under round86.
See the [repair record](comparison/repairs.html#round-89).
Autonomous work stopped when the user cleared the goal.

Round ninety fixes **LR12-64 / RI-145 / LI-083**: standard captured Node
content accessors, registry-owned detached text allocation, correct observer
pinning for detached foreign nodes, stable oldValue strings and backed Comment
writes. textContent shares the existing replace-all notification operation.
**21 focused native checks pass**, including four new AST/MIR regressions that
fail on the clean current release. Chromium agrees with the behavioral controls;
two existing CLI fixtures retain their goldens and layout integer lint passes.
No full baseline is repeated; this does not resolve independently logged
baseline-runner failures (LR12-63).

Two Monese pages now match their preserved browser references below0.4% mean
error and clear the major inventory. These requalify the prior inline-script
lifecycle repair and current integrated source; the new accessor repair is not
claimed as their visual cause. Final native captures retain1000ms settling.
Inventory: **125 high, zero critical,942 low**. Browser data, both audits and all
1065 unselected report rows remain unchanged. Dock's latest failed retry remains
qualified under round86. Coda's current API403 diagnostic is not promoted.
See the [repair record](comparison/repairs.html#round-90). The goal remains active.

Round ninety-one fixes **LI-084 / RI-146**: light-dark() and relative RGB/HSL
use shared typed color computation, scoped channel math, retained context and
authored color-scheme offers. Paint and CSSOM share cascade selection; implicit
foreground inheritance now copies the parent's computed color rather than
replaying its expression. Name IDs are appended perD5.6.1. **32 focused checks
pass** (seven parser and25 PNG checks);20 new controls agree with Chromium,
including19 computed-style mirrors. Integer lint passes; no full baseline is
repeated. Broader relative models and automatic UA dark palettes remain open.

Superhuman's white/light foreground is restored. Mean error drops10.73% to8.79%
and changed area23.88% to19.56%, but portrait/overlay placement and clipped
sign-in still retain its major review (RI-148). No new high-page clearance is
claimed. The1000ms request, browser references, both audits, URLs, thresholds
and all1066 unselected rows remain unchanged. Inventory: **125 high, zero
critical,942 low**. Dock's latest failed retry remains qualified under round86.
See the [repair record](comparison/repairs.html#round-91). The goal remains active.

Round ninety-two qualifies **RI-148** hero repairs: flex min/max bounds remain
constraints, content-only intrinsic queries exclude their own box limits, and
horizontal flex items enter shared paragraph setup. Intrinsic flex height uses
existing sizing stages with selected-flow/counter rollback (D5.5.1), avoiding
final descendant flow. Intrinsic separator advances share authored tracking;
balanced line-fit queries reuse contextual inline flow. No page condition,
width fudge, authored-source edit or capture-window increase is introduced.

**49 focused PNG checks pass**, including five new tracking cases that fail
before repair. The actual variable-font and break-word reductions now have two
lines; Chromium independently confirms that result. Integer lint passes.
The980-case text baseline remains895 pass/84 partial/one pre-existing failure
(boundary-shaping-006); no full baseline is claimed.

Superhuman clears its major review: mean error8.79% to4.06%, changed area19.56%
to6.27%. The headline, portrait and major overlays align. Its1000ms live capture
loads in15.47s with513MB peak, removing the trial's41.61s/1452MB regression.
Small sign-in clipping and a nonfatal stale-flow restoration warning remain
open. Inventory: **124 high, zero critical,943 low**. All1066 other rows,
browser references, URLs, thresholds and both audits remain unchanged.
Agiloft's blank diagnostic recheck is not imported; Dock retains the older pair
qualified under round86. Evidence is archived under temp/online-visual-round93.
See the [repair record](comparison/repairs.html#round-92). The goal remains active.

Round ninety-three requalifies **Leaflet** with the current integrated modal
and backdrop painting (D9.1.1v4 / D9.1.3v2). The opaque top-layer modal now
occludes the document and has the expected gray backdrop: mean error 25.61% to 0.47% and changed area 68.50% to 0.06%.
The1000ms native capture loads in22.18s. This is a requalification of the
already tested round92 renderer; no new renderer code or baseline repeat.

Inventory: **123 high, zero critical,944 low**, across1067 historical pairs.
All1066 other comparison rows, browser references, annotations, URLs,
thresholds and both audits remain unchanged. Current LOC HTTP403 and blank
NVD diagnostics are not imported; their historical high pairs remain. Dock's
latest timeout still retains the older pair qualified under round86.
See the [repair record](comparison/repairs.html#round-93). The goal remains active.

Round ninety-four repairs **nested inline whitespace in intrinsic sizing**
(RI-150; D9.1.1v4 / D9.1.3v2, D5.5.1). Rogo's headline now fits one line;
its missing navigation and patterned background keep the page high severity.
The unchanged 1000ms native capture loads in 5.54s: mean error 7.32% to 5.14%; changed area 13.84% to 9.71%.

All 34 focused PNG checks and five native whitespace tests pass. Four of the
ten new controls fail before repair; two independent Chromium controls confirm
the line width. Integer lint passes. No broad baseline is repeated for this
increment. The qualified RI-149 hidden-backdrop fix is also present, without
claiming a live Metronome clearance.

Inventory remains **123 high, zero critical, 944 low**, across 1067 historical
pairs. All 1066 other rows, browser references, URLs, thresholds and both audits
are preserved. Rogo's manual review retains its history and remaining omissions.
See the [repair record](comparison/repairs.html#round-94). The goal remains active.

Round ninety-five repairs **auto-column flex constraints and authored flex-basis
box sizing** (RI-151; D9.1.1v4 / D9.1.3v2, D5.5.1). Circle's same saved hero
returns from 600px to its resolved 720px height. Against the same current browser
capture, mean error falls12.46% to 4.70%; its main heading, copy and form align.
The old browser reference ended at /a with a different design; current Chromium
ends at / and serves the matching new hero. That reference maintenance is audited
separately from the source repair. Small bottom brand logos remain absent.

**Usersnap and Armadillo also clear their historical blank reviews**: Usersnap: 10.65% to 1.96%; Armadillo: 9.37% to 2.33%.
Their main content and Armadillo's large illustration are present. These are
capture requalifications; no unconfirmed startup/network cause is credited to
the code changes. All selected online frames remain 1000ms. Superhuman's final
control image exactly matches its retained qualified PNG.

Four sizing regressions already present in the prior renderer are repaired by
reusing the intrinsic aspect-ratio cross-bound transfer helper (RI-152).
All 36 focused native tests and 52 PNG checks pass; sizing passes 776/776.
The broader 17-category layout check records 5609 passes, 294 partials and the
same pre-existing boundary-shaping-006 failure, independently reproduced with
the previous binary. The separate native input-event executable was not rebuilt,
so this is not a full green Radiant gate. RI-153 records the remaining
unconfirmed automatic-minimum/percentage-cycle observation.

Inventory: **120 high, zero critical, 947 low**, across 1067 historical pairs.
All 1064 other rows, 1066 other browser references, annotations, URLs, thresholds
and the capture-frame audit are preserved; only Circle is added to the reference
update audit. See the [repair record](comparison/repairs.html#round-95).
The goal remains active.


Round ninety-six fixes **RI-154**: comment-separated SSR text lost inter-word
spaces during intrinsic measurement. Mode's three hero headings now fit one
line; coarse error improves from **27.15% to 19.53%**. Missing hero media and
navigation styling remain major, so Mode stays high.

**RI-153** also fixes growing-column automatic minima that borrowed an ancestor's
definite height or persisted percentage used sizes. The new auto/400px/auto/300px
regression passes without an authored zero minimum. No live clearance is attributed.

Focused checks: **47/47 PNG pass**, including four new before-failing checks;
**49/50 native pass**. The old forced-break allocation failure has identical
static view trees with the previous and final renderer, unchanged expectations,
and an unconfirmed root cause under RI-148. Chromium agrees on the reduced
comment-spacing heading's 724.3px by 108px geometry. Integer lint passes; broad
baselines were not repeated.

The reduced Metronome navbar reaches the same incomplete initialization in
Chromium when performance.now() is forced to zero. No clock offset, vendor edit,
budget increase or visibility override was used.

Inventory remains **120 high, zero critical, 947 low**, across 1067 historical
pairs. All 1066 other rows, all browser references, annotations, URLs, thresholds
and both capture audits are preserved. See the
[repair record](comparison/repairs.html#round-96). The goal remains active.


Round ninety-seven completes the supported native image profile (**JS13-L16 /
RI-155**): branded loading reflection, currentSrc, complete and decode promises,
with real pixel/SVG readiness, pending-request replacement rejection and precise
callback ownership. Parsed images now receive the same completion notifications
as scripted source writes, including local/data images.

All **11 new image checks pass**. The full HTTP session group passes **14/14**;
the local image/API/script group has **15/16 passes**. Its image-growth failure
(95px instead of 432px) independently reproduces with the prior renderer.
The exact Gatsby reveal-script reduction matches Chromium. Integer lint passes;
broad baselines were not repeated.

Mode's live image is unchanged at **19.53%** error. Its custom hero does not use
the generic Gatsby reveal nodes; that initial attribution was incorrect.
Its 142,207-byte async framework exceeds the unchanged 131,072-byte pre-layout
async/defer budget. No complete client-startup repair is claimed.

Orum's current hero photo and cards are present, improving its retained error
from **26.38% to 9.41%**. The prior renderer gives identical pixels on the same
current page, so this clearance is **capture requalification only**. Typelane
remains high with a large browser consent-modal state difference.

Inventory: **119 high, zero critical, 948 low**, across 1067 historical pairs.
All 1065 other comparison rows, all browser references, annotations, URLs,
thresholds and both capture audits are preserved. See the
[repair record](comparison/repairs.html#round-97). The goal remains active.


Round ninety-eight repairs **RI-156 / RI-157**: intrinsic flex collection now
retains display-contents descendants; ratio-dependent automatic minima retain
content and distinguish clip from scrolling. Natural flex probes keep the
ratio-derived percentage basis without limiting natural extent, and SVG
percentage geometry takes precedence over cached natural contributions.

The existing nested-column control improves **226px → 334px** and the existing
image-growth control **95px → 432px**. All **53 focused native tests**, **28
focused PNG checks** and **776 sizing baseline cases pass**. References,
thresholds and expectations are unchanged; layout integer lint passes. Nine
other inline-break PNG controls still fail in both prior and final renderers,
and RI-153's separate definite-height shrinking observation remains open.

Live Dribbble Pro and Framerauth captures remain blank, byte-identical with the
prior renderer. Dribbble's current response executes Framer startup and reports
a TypeError; the remaining startup cause is not established. These diagnostics
are not imported over historical canonical pairs. No high-severity clearance
is claimed. Inventory remains **119 high, zero critical, 948 low** across 1067
historical pairs. All comparison rows, snapshots, annotations and capture
audits are preserved. See the [repair record](comparison/repairs.html#round-98).
The qualified release renderer includes the fixes; the goal remains active.


Round ninety-nine repairs **RI-158**. Non-stretched column flex items now
measure content height at the same fit-content cross width used by final
sizing. The affected heading retains its wrapped **108px** contribution and
its column **334px**, restoring the action's placement inside the hero.

All **nine previously failing inline-break PNG controls now pass** without
changing expectations. The full focused PNG selection passes **56/56**, the
expanded native selection **53/53**, and integer lint passes. The preceding
776-case sizing milestone baseline was not repeated for this focused change.

Popcorn's current native capture is refreshed at **2.60%** error (previously
3.34%), but its device illustration remains mostly below the viewport. The
prior renderer produces identical pixels on the same current page, so this
capture difference is not attributed to the source fix. Its manual high
review remains. Inventory stays **119 high, zero critical, 948 low** across
1067 pairs. All 1066 other rows, all browser references, annotations and
capture audits are preserved. See the [repair record](comparison/repairs.html#round-99).
The qualified release renderer includes the fix; the goal remains active.


Round one hundred repairs **RI-159**: content-based column minima now use an
indefinite percentage basis even with a definite preferred height, and exclude
cached used heights, including for zero content. Clip retains a non-scrolling
content minimum. The shrinking control now measures **120px instead of 400px**.
Seven independent Chromium controls agree; the expanded native test includes
constraint and empty/content mutations. All **53 native tests**, **56 PNG
checks** and layout integer lint pass. Broad baselines were not repeated.

The capture harness also corrects an animation-origin mismatch. Chromium now
holds its animation timeline during loading, then resumes normally for the
same post-load window as native frozen startup. Authored delays, pauses and
individual playback rates are preserved; no endpoint seek or budget increase
is used. Captures record the origin, mixed origins are rejected, and retries
refresh legacy counterparts when needed. All **18 capture/comparator checks
pass**, including delayed-resource and authored-timing controls.

Increase improves **18.43% → 5.98%** with matching animation ages and clears
its major comparison as **capture-frame calibration only**, not an RI-159
clearance. Prior pairs/hashes are retained in the frame audit. A separate prior
renderer live attempt reported script execution failure and rendered only its
header, so it cannot establish source-fix causality or stable client startup.

Inventory is **118 high, zero critical, 949 low** across 1067 pairs. All 1066
other rows and browser references, all annotations, URLs, thresholds, design
reference updates and prior frame audit entries are preserved. See the
[repair record](comparison/repairs.html#round-100). The goal remains active.


Round one hundred one applies the origin-aware capture repair to Nightwatch
and Gladia. Nightwatch's matched pair is **5.82% error / 10.83% changed area**;
Chromium also shows the early blurred hero stage. Its one-second animations
have staggered 0.4/0.5/0.7/1s delays. The old copy/actions/dashboard omission
review is resolved as **capture-frame calibration**, with its history retained.
Grain and header-spacing residue remains outside this major-difference round.

Gladia stays high at **12.31% error / 45.64% changed area**. Its headline has a
feature-flag callback or **1500ms JavaScript fallback**, beyond native's 1000ms
frame. Animation-origin calibration does not freeze Chromium's JavaScript
startup timers. A broad virtual-time pause stalled local navigation and was
not adopted. The background script also requires unsupported Canvas radial
gradient paint objects: a local native probe confirms both gradient methods
are undefined and the radial call raises TypeError. **JS13-L17** records this
larger feature gap; no substitute drawing or forced reveal is introduced.
A new manual major review protects Gladia's remaining omissions.

The existing qualified renderer is retained. Verification here consists of
two loaded native/browser pairs, authored CSS/JS inspection and the Canvas API
probe; native/PNG/baseline suites were not rerun. Inventory is **117 high,
zero critical, 950 low** across 1067 pairs. All 1065 other rows and browser
references, URLs, thresholds, design-reference audit, prior frame entries and
annotation history are preserved. See the [repair record](comparison/repairs.html#round-101).
The goal remains active.


Round one hundred two repairs **RI-160** (CSS animation restyles sampling their
own opacity as an implicit endpoint) and **RI-161** (inline text-clipped
backgrounds painting at the containing block origin). PNPM's headline, copy,
actions and terminal are restored. The same-origin live comparison improves
from **25.27% error / 37.79% changed area** to **11.45% / 27.32%** with the
unchanged 1000ms post-load window. The exact source reductions reproduce both
failures with the prior renderer. All **58 native animation tests, 46 focused
PNG controls, four SVG exports and integer lint pass**; broad baselines were
not rerun. General fragment slicing and PDF output are not claimed qualified.

PNPM remains high through a new manual review: its entire client-created
navigation is still absent and the hero shifts upward. Its server HTML has no
navigation, and the current React entry module exceeds the existing snapshot
script-admission bounds (**JS13-L1**); no cap is raised. Inventory remains
**117 high, zero critical, 950 low** (94 automated / 23 manual high), with no
page clearance claimed. The other 1066 rows and browser references, URLs,
thresholds and prior audits/reviews are preserved. See the
[repair record](comparison/repairs.html#round-102). The goal remains active.


Round one hundred three qualifies **RI-162** (isolated SVG CSS animation) and
**RI-163** (CSS background image clock startup), plus the **RI-155** initial-image
follow-up that shares resource discovery's inert-template boundary. Highnote's
missing animated orb is restored: a same-origin 1000ms post-load before/after
pair improves **25.47% to 2.56% error**, **90.25% to 3.53% changed area**.
Two header buttons remain absent, while main navigation and hero content agree.

The current release passes **39 focused image/resource tests, 27 UI assertions,
six SVG exports and integer lint**. Earlier candidate evidence includes the
foreignObject replays. No broad baseline is repeated. General CSS effect classes,
font-relative animation, use-instance effects and PDF are not claimed qualified.
AndOpen's three large shapes also render in both its unchanged markup reduction
and current loaded page; its retained review is resolved as **revalidation**, not
as a new SVG-image fix. Cookie UI and demonstration-frame differences remain.

The severity-sorted report now has **115 high, zero critical, 952 low** (93
automated / 22 manual high): one native-repair clearance and one revalidation.
The other 1065 rows/references, catalog URLs, thresholds and prior audits/reviews
are preserved. See the [repair evidence](comparison/repairs.html#round-103).
The goal remains active.


Round one hundred four fixes **LI-085**: shared URL requests retain the highest
requested priority and promote queued work without restarting active transfers
or changing resource limits/deadlines. All **13 focused native scheduler/cache
tests and 21 HTTP resource checks pass**. No cold-media improvement or online
clearance is attributed without page-level evidence. The finite readiness limit
(**RI-061**) remains open. No broad baseline is repeated. All 1067 comparison
rows and snapshot pairs are preserved; inventory remains **115 high, zero
critical, 952 low**. See the [repair record](comparison/repairs.html#round-104).
The goal remains active.


Round one hundred five fixes **JS13-L18** by removing two nonstandard loader
rewrites: library-specific global synchronization and source-text function
exports. These manufactured aliases/window properties and invoked unrelated
author accessors; the existing realm already owns global binding semantics.
Five controls fail before repair. All **11 controls pass in AST and MIR**, the
same eleven pass Chromium, and **37 related checks plus five lifecycle UI
assertions pass**. No script budgets, deadlines or capture clocks change, and
no broad baseline is repeated.

Dropbox's current diagnostic still omits its hero and hits the existing
**120-second JavaScript watchdog**; the remaining execution stall is unconfirmed.
Its canonical pair and high review are preserved. All 1067 comparison rows and
pairs remain unchanged: **115 high, zero critical, 952 low**. See the
[loader repair and diagnostic](comparison/repairs.html#round-105).
The goal remains active.


Round one hundred six fixes **JS13-L19** (repeated whole-bundle initializer
index publication) and **JS13-L20** (resource load propagation to Window).
All **276 script/ownership tests**, the new load-path controls in native and
Chromium, and existing exception/interruption controls pass. Dropbox no longer
accumulates over 131,000 window load listeners; it completes in **8.28s** with
unchanged limits and a 1000ms settling window. Its current PNG is nevertheless
blank, so it stays high. The [current diagnostic pair](comparison/repairs.html#round-106)
records this remaining failure without claiming a visual repair or replacing
the canonical pair. All 1067 rows remain severity-sorted: **115 high, zero
critical, 952 low**. Broad baselines were deferred; the goal remains active.


Round one hundred seven isolates **JS13-L21**: Dropbox's DOMPurify sanitizer
calls the missing `Document.createNodeIterator` on a parsed document. Direct
main/parsed-document probes and the actual AST failure span confirm the gap;
AST-only execution also fails. A complete live iterator is a new stateful DOM
interface, so no TreeWalker alias or sanitizer bypass is introduced. The fresh
WebKit Blog pair still lacks large PNGs; independent transfers remain incomplete
after 15 seconds. Library of Congress currently serves a Cloudflare 403 challenge.
No production runtime change or visual clearance is claimed. All temporary
diagnostics are removed. The [evidence update](comparison/repairs.html#round-107)
retains all 1067 canonical pairs and rows: **115 high, zero critical, 952 low**.
The goal remains active.


Round one hundred eight repairs **JS13-L22**: fetch and both XHR modes now
reuse the document/resource HTTP client identity. The server-observed regression
fails before and passes after; **all 15 focused HTTP session tests pass**.
Aircall's current 1000ms post-load pair remains high (**37.89% error / 83.70%
changed area**). Its native before/after pixels are identical: configuration and
geolocation complete, but the appended external consent SDK reaches the existing
dynamic-script preparation gap. No consent bypass or capture-budget change is
introduced. Only Aircall's canonical pair is refreshed; its historical images
and frame audit are retained. The other 1066 rows, catalog and review history
remain unchanged. The severity-sorted inventory stays **115 high, zero critical,
952 low**. See [repair evidence](comparison/repairs.html#round-108).
The goal remains active.


Round one hundred nine completes **JS13-L21**'s qualified NodeIterator surface.
Dropbox's parsed-document sanitizer can now traverse and remove nodes, restoring
its main heading, copy, action and product image. The implementation reuses
TreeWalker's filter and existing weak collections, with separate bidirectional
reference/candidate pointers and removal adjustment. No page workaround,
sanitizer bypass or capture-budget change is introduced.

**36 focused native tests and seven Chromium fixture groups pass**. All four
native Node modules were rebuilt for the new traced realm root. Full traversal
WPT conformance is not claimed; broad baselines were not repeated.

Dropbox remains high: the fresh native pair shows a pale split hero while
Chromium shows a blue centered hero (**42.59% error / 88.50% changed area**).
Its remaining cause is unconfirmed; the repaired omission is distinguished from
visual parity. Only its pair/row and retained manual review are refreshed, with
historical images and prior audits preserved. The other 1066 rows are unchanged.
Inventory remains **115 high, zero critical, 952 low** (94 automatic / 21
additional manual). See [the repair evidence](comparison/repairs.html#round-109).
The goal remains active.


Round one hundred ten repairs **JS13-L23 / JS13-L24 / Input LI-086**.
Fetch converts coercible string inputs and retains full URLs; URLSearchParams
mutations and search/href replacement share a live query record; Input clears
raw default ports when parsing. Existing serializers and scheme tables are
reused with precise private ownership. Request-valued fetch and full URL/IDL
conformance remain unqualified (**D8.4.4 / D9.1.3v2 / D6.3.3 / D9.5.4**).

**27 focused AST tests, 12 MIR cases, five Chromium groups and 37 Input URL
tests pass.** Coframe's exact SDK request now receives its project configuration
(HTTP 200 / 692816 bytes instead of 400 / 53 bytes), and its watchers execute
on the actual page. Broad baselines and capture policies are unchanged.

Dropbox remains high: pale split native hero versus blue centered Chromium
hero (**42.43% error / 88.47% changed area**). Native gRPC EOF errors remain
unconfirmed leads. Only its pair, row and retained review are refreshed; the
other 1066 rows and existing history remain intact. Inventory stays **115 high,
zero critical, 952 low** (94 automatic / 21 additional manual). See
[repair evidence](comparison/repairs.html#round-110). The goal remains active.


Round one hundred eleven fixes **JS13-L25**'s pending default-read lifecycle.
An empty queue no longer reports EOF while an asynchronous pull is producing
its chunk. Private traced requests wait for enqueue, close or error; startup
and pull completion serialize delivery (**D8.4.4 / D5.5.1 / D6.3.3**).

**34 focused native checks, seven MIR stream cases and seven Chromium groups
pass.** Full stream conformance remains unqualified. Node-module golden probes
could not resolve assert/stream on either prior or current hosts and are not
counted as passes. Broad baselines were not repeated.

The fresh Dropbox run has **zero gRPC request/EOF failures**, versus eight
previously. Its native PNG remains pixel-identical, so the large hero difference
is still unresolved (**42.63% error / 88.46% changed area**). Only that pair,
row and retained review are refreshed; all other 1066 rows and histories remain
preserved. Inventory stays **115 high, zero critical, 952 low** (94 automatic /
21 additional manual). See [repair evidence](comparison/repairs.html#round-111).
The goal remains active.


Round one hundred twelve fixes **RI-166/RI-167/RI-168**: CSS-wide display
resets use the initial role, empty effect records do not create stacking
contexts, and clip-path resets resolve before shape parsing. **81 focused PNG
checks and 21 Chromium groups pass**, plus integer-cast lint. Getsafe's frozen
cookie panel/backdrop return; its hero remains absent. Its current live PNG is
blank on both prior and intermediate renderers with the same currency formatter
exception (**JS10-L1**, locale/currency feature gap). No live clearance is claimed.
All 1067 canonical rows/pairs, reviews, URLs, thresholds and audits are preserved:
**115 high, zero critical, 952 low**. No baseline, admission or frame-policy
changes. [Repair evidence](comparison/repairs.html#round-112). Goal remains active.


Round one hundred thirteen fixes **JS13-L26**: removing an absent attribute
no longer publishes mutation records, custom-element callbacks or layout
invalidation. Present empty attributes retain their normal removal behavior.
**13 focused native checks pass**; the ordinary-removal control fails before
repair. Primer's absent-removal warnings drop from 14 to zero, but both live
runs still hit the script watchdog and produce identical orange PNGs. Dock's
current diagnostic hits the outer capture limit. No page is cleared and no
performance improvement is claimed. All 1067 canonical rows/pairs, reviews,
URLs, thresholds, manifest and audits are preserved: **115 high, zero critical,
952 low**. Broad baselines are not repeated.
[Repair evidence](comparison/repairs.html#round-113). Goal remains active.

Round113 also records **RI-169**: cross-origin iframe exports lack a deferred
network-navigation path. Limitless remains high; its local frozen diagnostic
is not imported. The support matrix now states this limit.


Round one hundred fourteen fixes **JS03-L3, JS13-L27 and JS13-L28**: nominal
errors now have lazy native stacks, Document.title has a real DOM setter, and
createDocument retains its root namespace. **17 final focused native controls,
ten additional AST/MIR stack checks and ten Chromium groups pass.** Full
baselines are not repeated. Temporary tracing is removed.

**Pitch News leaves major review**, with its heading and article layout restored
(2.48% mean error, 0.85% changed area). A current prior-renderer control at the
same explicit 1000ms post-load origin remains blank with a fatal TypeError.
Hera restores the blue background/logo but keeps a manual major headline
omission; Comand restores its headline/card but keeps a major video omission.
FramerAuth's current attempt times out; its prior canonical pair is retained.

The three selected origins were previously unrecorded. Their fresh matched
pairs and retained prior images are recorded in the frame audit; historical
score movement includes the reference/frame refresh. The design-reference audit
is unchanged. Other 1064 pairs/rows and unrelated review history remain intact.
**114 high, zero critical, 953 low** (92 automatic plus 22 additional manual).
[Repair evidence](comparison/repairs.html#round-114). Goal remains active.


Round one hundred fifteen fixes **JS13-L29**: unrelated preload/icon link
metadata no longer forces stylesheet recascade and layout. Associated sheets
and selector-dependent values retain invalidation. Two prior controls need ten
layouts versus one; the repaired controls preserve observer delivery and geometry.
**35 focused controls pass** (23 native metadata/title/stack, 11 stylesheet
lifecycle, one existing form-validity render). Full baselines are not repeated.

Current-source merge repairs restore the extracted geometry declaration's
closing brace (**LI-082**) and the form-validity scan's pointer access. Build
files are regenerated from JSON, with the standard sibling-archive generator.
FramerAuth still times out without a PNG; its canonical high pair is retained.
No live speed or clearance claim. All 1067 rows/pairs, catalog, reviews, manifest
and audits are unchanged: **114 high, zero critical, 953 low**.
[Repair evidence](comparison/repairs.html#round-115). Goal remains active.


Round one hundred sixteen fixes **JS13-L30 / RI-170 / JS13-L31**: authored
CSSOM/direct style writes preserve attribute observations and property identity;
retained paint publication consumes its own geometry invalidation; unrelated
keyframes no longer force attribute recascade. Real and escaped attribute
functions remain conservative. **23 focused controls pass**, with established
transform updates requiring **one layout instead of eleven** and preserving
geometry/pixels. Integer lint passes. Shared behavior follows **D8.4.4 /
D9.5.6v2 / D9.1.3v2**; capture keeps **D10.1.7**.

The 17-category layout checkpoint reports **5,609 passed, 294 partial, one
pre-existing required-case failure**, display inheritance (**RI-171**). That
case exports identical layout trees with the qualified pre-CSSOM, intermediate
and final renderers. No baseline/reference entry is weakened; the checkpoint
is not reported clean. Full native and Lambda baselines are not repeated.

FramerAuth still times out at 150 seconds without a PNG. Ebury loads/captures
in 11.6 seconds but remains blank; its authored anti-flicker timer and dynamic
external snippet remain diagnostic leads. No live clearance or speed claim.
All 1067 canonical rows/pairs, catalog, reviews, manifest and audits are
preserved: **114 high, zero critical, 953 low**.
[Repair evidence](comparison/repairs.html#round-116). Goal remains active.


Round one hundred seventeen fixes **RI-173 / RI-172**. SVG stroke-phase
updates no longer force HTML layout; selector and motion dependencies retain
their guards, and ordinary observer/cache epochs are preserved. Transform
substitutions and CSS-wide resets retain layout when they can remove a
containing block. **30 native checks, seven comparison-tool controls and
integer lint pass**. SVG updates use **one layout instead of eleven** while
preserving geometry/pixels; all sixteen reset combinations pass.

The milestone 17-category layout checkpoint reports **5,609 passed, 294 partial
and the same existing RI-171 failure**. No reference is weakened. The final
release now loads/captures FramerAuth in **70.04s**, inside its unchanged
150-second deadline at 1000ms post-load. The matched Chromium frame takes
3.12s. The hero remains absent; browser opacity effects use Web Animations,
whose automatic native playback remains a feature gap. No visibility override.

FramerAuth's qualified pair/manifest/row are refreshed, retaining prior images
and frame history. Its **11.45% mean error / 29.43% changed area** does not clear
the manual major review. All other 1066 rows, reviews, catalog, thresholds and
design-reference audit are preserved: **114 high, zero critical, 953 low**
(91 automatic plus 23 additional manual). Shared ownership follows **D9.1.3v2 /
D9.5.6v2 / D5.5.1**; capture retains **D10.1.7**.
[Repair evidence](comparison/repairs.html#round-117). Goal remains active.


The subsequent focused mutation repair (**LI-087 / JS13-L32**) shares metadata
handling with null-namespace setters and already-present inert reflection.
Repeated writes retain observers and callbacks but use **one layout instead of
eleven**. All **38 focused native checks** and integer lint pass, including
focus, CSS-value and namespace-identity guards. Ownership follows **D9.1.3v2 /
D9.5.6v2**. No baseline is repeated immediately after the preceding milestone.

Dock reaches completed load but still has no PNG at its unchanged150-second
deadline. Its previous pair remains authoritative; no clearance is claimed.
The comparison remains **114 high, zero critical, 953 low**. Evidence is
`temp/online-visual-round126/`; the goal remains active.


Round118 publishes Dock's first current successful PNG after **LI-087 /
JS13-L32 / LI-088**: stable inert and null-namespace metadata preserve geometry,
and cascade selection stops after the first eligible candidate in each sorted
source. **46 style-node and38 native rendering controls pass**, plus integer
lint. The milestone remains **5609 passed,294 partial,one existing RI-171
failure**; no full Lambda/native baseline is repeated.

Dock captures in **144.78s**, within its unchanged150-second deadline at1000ms
post-load. Its matching Chromium frame completes in2.94s. Major background,
text/media and header differences remain: **44.87% mean error /70.07% changed
area**. No clearance or pixel override. Dock's pair/row/manifest are refreshed,
retaining old images and frame history;1066 other rows, all reviews, catalog
and design-reference audit are preserved. Severity order remains **114 high,
zero critical,953 low**. Ownership: **D9.1.3v2 / D9.5.6v2**; capture: **D10.1.7**.
[Repair evidence](comparison/repairs.html#round-118). Goal remains active.


Round 119 fixes **LI-089 / LI-090**. Shared relative Oklab/Oklch computation
restores Dock's authored blue background, white heading and copy. Absent
properties reuse the existing all-only rollback cache, preserving ordinary
invalidation. **66 native rendering and 47 style-node checks pass**; the
color-only release also passes 16 parser/color/condition controls.

Dock now captures in **140.11s**, within the unchanged150-second/1000ms policy.
Against the preserved browser reference, mean error improves **44.87%→12.12%**
and changed area70.07%→26.53%. Its product screen and side photos remain
absent; the header remains unstyled. A new manual review retains high severity.
The intermediate timed-out capture is excluded. No color/visibility workaround,
capture/admission change, new cache storage or vendor patch.

The milestone remains **5609 passed,294 partial,one existing RI-171 failure**.
All 1066 other rows, browser reference, catalog and design-reference audit are
preserved. Inventory remains **114 high,zero critical,953 low** (90 automatic
plus24 additional manual). Ownership: **D9.1.3v2 / D5.5.1**; capture: **D10.1.7**.
[Repair evidence](comparison/repairs.html#round-119). Goal remains active.


Round120 fixes **LI-091 / RI-174 / RI-175**: an invalid shorthand-source marker
could override valid CSS dimensions; display rollback reapplied a discarded
ALL reset; internal table display roles serialized incorrectly. These are
shared engine repairs, with no page-specific dimensions or visibility rules.
Ownership: **D9.1.3v2 / D9.1.1v4**.

**78 focused native rendering checks and48 Input style-node tests pass**.
Integer layout lint passes. The17-category milestone remains **5609 passed,
294 partial,one existing RI-171 failure**. No full Lambda/native baseline
was repeated. The unchanged color parser has16 preceding focused checks.

The identical saved Dock DOM now paints side photographs, while its central
product panel remains unresolved. The current live attempt timed out before
completed load and is excluded. **No canonical rows or captures are updated**;
the last qualified Round119 Dock pair remains. All1067 rows, catalog, reviews,
manifest and both audits are preserved. **114 high,zero critical,953 low**
remain, severity sorted. Capture policy remains **D10.1.7**.
[Repair evidence](comparison/repairs.html#round-120). Goal remains active.


Round121 fixes **RI-171**. The layout-only WPT harness lacked an inequality
assertion, so inheritance setup threw before restoring temporary display
styles. A truthful SameValue assertion restores cleanup. The original case
now matches **4/4 elements**, up from2/4; no layout code, reference, fixture,
tolerance or required-pass entry changed (**D9.1.1v4 / D10.1.7**).
The new regression and both existing HTML script tests pass. The preceding
17-category milestone remains the last full run; its sole failing case is
now repaired in isolation. No broad baseline or capture sweep was repeated.

Dock's remaining product-panel occlusion is the existing **RI-041 URL-mask
gap**. Stellar's hidden loading state is isolated to missing native completion
for **FontFaceSet.load (JS13-L33)**. No page workaround or fake completion is
applied. All1067 canonical rows, captures, references, catalog, annotations and
both audits are preserved: **114 high,zero critical,953 low** remain.
[Repair evidence](comparison/repairs.html#round-121). Goal remains active.


Round122 fixes **JS13-L34**: fetch ignored HTML base, and XHR retained a stale
base after mutation/removal. Both now reuse the existing live document-base
resolver, preserving local-file handling, standalone fallback and permissions
(**D8.4.4 / D9.5.6v2 / D9.1.3v2**). No page-specific URL rewrite.

**Five focused native fixtures, four existing local JS goldens and four
Chromium controls pass.** All four new native cases failed before repair.
No broad baseline or online sweep was repeated. This shared defect was
exposed by a saved-page investigation; Dribbble's live blank state is not
claimed repaired. No diagnostic capture is imported. All1067 canonical rows,
manifest, references, catalog, reviews and audits remain unchanged:
**114 high,zero critical,953 low**.
[Repair evidence](comparison/repairs.html#round-122). Goal remains active.


Round123 fixes **JS13-L35**: missing inherited Node constants made Ramp's
scroll-parent walk treat Document as a styled element and throw during React
commit. Shared constant publication now gives Node/CSSRule correct readonly
WebIDL descriptors (**D8.4.4 / D9.5.6v2 / D6.3.3**). No page-specific rule.

**Ramp is restored**: full interface, **0.49% mean error / 0.30% changed area**.
Two native/Chromium controls, five existing request/base fixtures and the real
React exact golden pass. No broad baseline or catalog sweep. The matched
1000ms post-load pair replaces an old pair with unrecorded animation origins;
old images are retained in the frame audit (**D10.1.7**). Historical metric
movement includes a live reference refresh. All1066 other rows, catalog,
reviews and design audit are preserved. **113 high,zero critical,954 low**.
[Repair evidence](comparison/repairs.html#round-123). Goal remains active.


Round124 repairs **RI-176/177**: flexible multi-track spans now have the
correct zero automatic minimum, and grid alignment preserves a nested flex
formatter's completed height (**D9.1.1v4 / D9.1.3v2**). Explicit item and
intrinsic track minima remain effective. The redundant height estimator is
removed; no page-specific geometry or capture policy changes.

The unchanged saved Payload source improves **9.33% to3.28%** mean error and
**14.17% to6.01%** changed area. Main content placement and leaked hover
content are repaired. Native button appearance remains separate; live startup
is not claimed restored. **36 native cases,9 Chromium controls and integer
lint pass**. Six native regressions fail before repair. One17-category
milestone records **5608 pass /294 partial /2 fail /0 skip**. Both form
failures reproduce with the previous qualified renderer, so no new baseline
regression is observed; references/assertions stay unchanged. No Lambda
baseline repeated for native-only changes.

All1067 canonical rows, manifest, references, catalog, reviews and both audits
remain unchanged: **113 high,zero critical,954 low**. No diagnostic capture
is imported. [Repair evidence](comparison/repairs.html#round-124). Goal active.


Round125 repairs **RI-178/179 and LI-092**: loader-time font component reads
select/project the winning shorthand, and native inheritance preserves local
shorthand and inherits computed parent scalars. Native heading/emphasis
defaults remain local through existing UA metadata (**D9.1.1v4 / D9.1.3v2**).
Coda's saved-source heading retains **700 after layout**, matching Chromium.
**23 focused native cases,15 Chromium controls and integer lint pass**.
An intermediate UA-default regression was reproduced and fixed before
publication. Final relevant categories: **1526 pass /84 partial /0 fail**.
No full baseline or catalog sweep repeated; the prior17-category milestone's
two existing form failures remain recorded.

**Coda's current live capture remains high:** blank after external-cms and
load-listener TypeErrors; a missing IndexedDB diagnostic is also observed,
without establishing its causal link to the blank frame (JS13-L36). Chromium
loads with cookie consent. The matched1000ms post-load pair records **9.11%
mean error /30.04% changed area and blank mismatch**. No clearance is claimed.
Old images remain in the frame audit (**D10.1.7**); changed live/cookie state
prevents attributing historical metric movement to the font repair. All1066
other rows, catalog, reviews and design-reference audit remain unchanged.
**113 high,zero critical,954 low**. [Evidence](comparison/repairs.html#round-125).
Goal remains active.


Round126 completes focused **Slack/Swag** review without a new engine patch.
Slack's first scratch browser pair had navigated through a noscript refresh;
the corrected helper asserts the exact saved URL. Its missing40MB hero media
and layout shift reproduce the existing complete-file streaming gap RAD22-3;
a local16:9 diagnostic restores geometry. No substitution is shipped.
Swag's suspected backdrop defect disappears when its400ms fade is frozen
before navigation: saved-source **3.71% mean /7.74% changed area**.

**Current live Swag remains high** at the shared1000ms post-load window:
**26.88% mean /49.94% area**. Native lacks client-created consent, region and
assistant UI; the startup source/API cause is unisolated. No clearance or
paint fix is claimed. Historical images remain in the frame audit
(**D10.1.7**). All1066 other rows, catalog, reviews and design-reference audit
are unchanged: **113 high,zero critical,954 low**. The qualified round137
font repairs remain installed; no rebuild or baseline repeat for diagnosis.
[Review evidence](comparison/repairs.html#round-126). Goal remains active.


Round127 fixes **JS13-L37 / LI-093**: the missing captured
`Node.prototype.lookupNamespaceURI` caused Coda's consent wrapper to throw
while handling DOM mutations. The shared DOM catalog now publishes the method;
node-kind dispatch and rooted conversion use the Input namespace resolver,
which preserves expanded-name precedence, declarations and fragment isolation
(**D8.4.4 / D9.5.4 / D9.5.6v2 / D9.1.3v2 / D6.3.3**).
**17 native cases and four Chromium fixtures pass**; all four new cases fail
before repair. The single relevant baseline-js category passes **630/630**.
No full baseline or catalog sweep repeated.

Coda's wrapper TypeErrors are gone. It now renders its own explicit
**missing IndexedDB error page**, confirming JS13-L36 as the remaining blocker.
The main site is still absent, so a new manual review keeps it high despite
a lower coarse score (**9.46% mean /31.36% area**). Coda's qualified browser
reference is preserved. Current Clay pricing stays blank/high (**12.61%**);
Patreon still differs in hero media and navigation (**18.67%**). Their matched
1000ms post-load pairs replace legacy pairs with unrecorded origins, retaining
history in the frame audit (**D10.1.7**). Metric movement includes live state,
and no whole-page clearance is attributed to this repair.

All1064 other rows, catalog, prior reviews and design-reference audit remain
unchanged. **113 high,zero critical,954 low** (88 automatic plus25 additional
manual), severity sorted. [Repair evidence](comparison/repairs.html#round-127).
Goal remains active.


Round128 fixes **RI-180 / RI-181**: authored image/link/script fetchpriority
now reaches the existing scheduler, including early prefetch and dynamic
images. Passive decoding preserves low hints; shared URLs retain promotion.
Local HTML's first admitted remote image/poster now initializes its network
manager before the bounded snapshot wait (**D9.1.1v4 / D9.1.3v2**).

**Three native GTests and three HTTP controls pass**. Coverage includes18
priority cases plus dynamic/passive/dedup controls, the unchanged five-case
export regression, live base resolution, inert templates and selected CSS
variable resources. Integer lint passes. The existing export check fails
before repair; a cached73×41 SVG becomes visible afterward. No broad baseline
or catalog capture sweep repeated; budgets and capture clocks are unchanged.

Fresh matched1000ms post-load pairs keep Aircall (**37.89% mean error**) and
Dayos (**62.33%, startup overlay**) high; whole-page differences are unchanged.
Hints on61 saved high-page DOMs indicate exposure, not61 fixable pages.
Old pairs remain archived (**D10.1.7**). Other1065 rows, catalog and annotations
are unchanged. **113 high,zero critical,954 low**, severity sorted.
[Repair evidence](comparison/repairs.html#round-128). Goal remains active.


Round129 fixes **RI-182**: a local document containing only CSS backgrounds
can now initialize its network manager through selected-resource discovery,
using the existing declaration-base resolution, admission and queue helper
(**D9.1.1v4 / D9.1.3v2**). Four inline/stylesheet/variable/pseudo background
cases fail before repair and pass afterward. Five existing image/poster cases,
the priority matrix and stylesheet-token guard still pass. **Three native
GTests and three HTTP controls pass**; integer lint passes. A cached73×41 CSS
background changes from0 to2993 red pixels in an independent export control.
No broad baseline, catalog sweep, budget or capture-clock change.

The fresh matched1000ms Overpass pair stays high (**24.49% mean error**): its
live hero is still absent. Earlier repairs restore the header and hero text
in its saved source, but its portrait remains absent (**2.51% diagnostic
error**, unchanged by this repair). No whole-page clearance is claimed.
Only that pair is refreshed; its history is archived (**D10.1.7**). Other1066
rows, catalog and annotations remain unchanged. **113 high,zero critical,
954 low**, severity sorted. [Repair evidence](comparison/repairs.html#round-129).
Goal remains active.


Round130 fixes **RI-183**: image-set string sources now enter native resource
loading through the existing option parser and shared URL visitor. type()
strings remain metadata; existing source selection and declaration-relative
URL handling are unchanged (**D9.1.1v4 / D9.1.3v2**). The new HTTP regression
fails before repair, then passes with the correct pixels and request URL.
**Three native GTests and six HTTP controls pass**; integer lint passes.
No broad baseline, catalog sweep, budget or capture-clock change.

Overpass's saved-source portrait is a readiness observation: a read-only
1000ms capture exactly matches the self-assignment diagnostic (**0.78098%
mean error**), and its PNG paints locally. No CSS publication or decoder
change is justified. Its separate absent live hero remains unresolved.
The single high-page string-source exposure found in saved CSS belongs to
Conductor's below-fold webinar poster, not a confirmed major-page cause.
No page cleared. This repair-only update preserves all1067 comparison rows,
capture entries, references, catalog and annotations. **113 high,zero
critical,954 low**. [Repair evidence](comparison/repairs.html#round-130).
Goal remains active.


Round131 fixes **RI-184**: admitted external scripts now dispatch completion
after evaluation and microtasks, with restored currentScript and a rooted
original target. The shared event scope preserves bootstrap's pending
stylesheet writes (**D6.4.1 / D6.3.3**, ownership **D9.1.1v4 / D9.1.3v2**).
Empty sources and runtime throws complete loading; fetch failures retain error;
inline scripts do not receive resource load events.

Both Youtrust major differences clear against **current matched references**:
home **30.49% → 1.02%** mean error, pricing **31.18% → 2.74%**. Current Chromium
controls agree. Minor typography, focus and background geometry remain.
**Ten focused HTTP cases, six existing script/stylesheet tests and the630-case
JavaScript layout baseline pass.** No catalog sweep or capture/budget change.
Both pairs are refreshed because their older animation origins were unrecorded;
the old pairs are archived. Repaired images also clear against the older
references. All1065 other rows, catalog and review annotations stay unchanged. **111 high,zero critical,956 low**, severity sorted.
[Repair evidence](comparison/repairs.html#round-131). Goal remains active.


Round132 fixes **JS13-L38**: programmatic click now suppresses reentry on the
same element throughout dispatch and activation. Teachable's tab initializer
recursively clicked its own target; its two maximum-call-stack errors are now
absent. Native per-element state and rooted wrappers preserve removal/layout
lifetime; other targets, clones, later microtasks and direct dispatch remain
available (**D8.4.4 / D9.5.6v2 / D6.3.3 / D5.5.1**).

**Seven focused controls agree with Chromium and pass** (six failed before;
the microtask control already passed), plus **six existing form/label tests**.
The current Teachable capture loads, but its independent anti-flicker hidden
state remains. No page clearance or performance claim, broad baseline,
admission change, capture-clock change or page override.
This repair-only update preserves all 1067 rows, capture entries, browser
references, catalog and annotations. **111 high, zero critical, 956 low**.
[Repair evidence](comparison/repairs.html#round-132). Goal remains active.


Round133 fixes **JS13-L39**: Node now inherits EventTarget, native event methods
honor prototype and instance wrappers, Window retires only untouched standalone
lazy slots, and Document reflection/deletion reuse the node expando helpers
(**D8.4.4 / D9.5.4 / D9.5.6v2**).

**All31 focused checks pass.** All five new controls fail before repair and
agree with Chromium afterward; they also pass separately in AST and MIR.
Attentive's remaining consent/script-preparation mismatch and Agiloft's
unisolated callback exception remain under review. No whole-page clearance,
broad baseline, online sweep, source-limit or capture-clock change is claimed.
This repair update preserves all1067 rows, capture entries, browser references,
catalog and annotations at **111 high, zero critical, 956 low**, severity sorted.
[Repair evidence](comparison/repairs.html#round-133). Goal remains active.


Round134 fixes **RI-185**: GIF sampling follows elapsed frame deadlines across
missed ticks, distinguishes repeat counts from total plays, and holds a finite
animation's final frame. Completion releases earlier frame buffers; cancellation
releases the remaining player and restores the cache-owned still
(**D9.1.3v2 / D10.1.7 / D5.5.1**).

**26 native GIF tests, seven GIF PNG controls, ten existing SVG image controls,
and integer-cast lint pass.** All seven shared PNG cases agree with Chromium;
three failed with the prior qualified renderer. Five sparse-tick variants and
two repeat/completion unit tests also fail before repair.
Maple's headline uses animated WebP, whose playback gap remains; no whole-page
clearance is claimed. No broad baseline, catalog sweep, capture-clock or limit
change. All1067 rows, capture entries, browser references, catalog and
annotations are preserved at **111 high, zero critical, 956 low**.
[Repair evidence](comparison/repairs.html#round-134). Goal remains active.


Round135 fixes **LI-096 / RI-186**: a sole color with two gradient positions
counts as two stops and retains its fill through the existing resolver
(**D9.1.1v4 / D9.1.3v2 / D5.5.1**). Linear, radial, conic and repeating forms
work with and without geometry preludes; malformed leading hints stay rejected.

**124 native parser tests,12 new PNG cases,25 existing color controls and12
Chromium controls pass.** All12 new PNG cases fail before repair. Hera's
identical saved-source comparison improves from13.69% mean /30.86% changed
area to2.05% /2.42%; its independent live text reveal remains open. No canonical
page is cleared. All1067 rows, capture entries, browser references, catalog and
annotations remain at **111 high,zero critical,956 low**. No broad baseline,
catalog sweep, clock or admission-limit change.
[Repair evidence](comparison/repairs.html#round-135). Goal remains active.


Round136 fixes **RI-187**: snapshot preparation polls completed native image
state and drains retained-realm callbacks through the existing host seam,
without advancing the selected virtual clock (**D9.1.3v2 / D5.5.1 / D6.4.1 /
D10.1.7**). The1000ms resource wait,250ms load boundary and source limits stay
unchanged. **56 focused image/resource tests and nine Chromium controls pass**;
five initial cases fail before repair. Integer-cast lint passes. No broad
baseline or catalog sweep is repeated.

Jetty and Pelago have current matched1000ms pairs and archived historical
frames. Both remain high: Jetty lacks navigation/media; Pelago's hero media is
present but its large consent dialog remains absent. Its new manual review
prevents a coarse-score false clearance. Current native pixels are identical
with the prior renderer on both pages, so no live improvement is attributed
to this repair. **111 high,zero critical,956 low** remain across1067 pairs.
The1065 other rows/captures, all URLs and previous review history are preserved.
[Repair evidence](comparison/repairs.html#round-136). Goal remains active.


Round137 repairs parser-task preparation and foreign-document adoption teardown
(**D8.4.4 / D9.5.6v2 / D5.5.1 / D6.3.3 / D6.4.1**). Live task metadata is
snapshotted once before scheduling; head arrivals reach observers before that
turn. Prepared sources survive later attribute changes/removal. Foreign native
documents retire before the main pool backing their adopted nodes disappears.
**64 focused host tests,13 additional MIR cases and13 Chromium controls pass**;
the JavaScript layout baseline passes **630/630** once for this batch. No source
cap, clock, public state layout or transfer policy changes. No catalog sweep or
full multi-category baseline is repeated.

Attentive has a current matched1000ms pair, with its two old frames archived.
It remains blank and high at **67.46% mean error /91.47% area**. Current native
pixels are identical with the prior renderer. A small body-arrival control
still differs from Chromium; the broader parser progression gap remains open.
No whole-page improvement or clearance is claimed. **111 high,zero critical,
956 low** remain across1067 pairs; the1066 other rows/captures, all URLs and
review history are preserved. [Repair evidence](comparison/repairs.html#round-137).
Goal remains active.


Round138 completes five shared repairs: dynamic classic external preparation,
first remote stylesheet discovery, live enumerable Location components, finite
requests created after a checkpoint, and virtual immediate continuations
(**D8.4.4 / D9.1.3v2 / D9.5.6v2 / D6.3.3 / D6.4.1 / D6.4.4 / D10.1.7**).
**60 focused host checks,17 MIR checks and29 Chromium controls pass.** The
JavaScript layout baseline passes **630/630** once at this milestone. Source
limits, watchdogs and the selected1000ms capture window are unchanged; no
catalog capture sweep or full multi-category baseline is repeated.

Kotlin now renders its documentation article, cards and current version.
Sidebar navigation and the browser announcement banner remain absent, so it
stays high severity at **10.62% mean error /22.60% changed area**. The old
canonical pair is archived and both current frames are recorded. The1066
other rows/captures, all URLs, review history and reference-design audit are
preserved. **111 high,zero critical,956 low** remain across1067 pairs.
[Repair evidence](comparison/repairs.html#round-138). Goal remains active.


Round139 reviews two remaining limits using the unchanged, qualified round153
renderer. No native code or baseline rerun. Kotlin's TOC exists with correct
geometry but starts a300ms fade when its fetch is delivered at virtual1016.67ms.
An80ms fetch-created fade reproduces the difference; a100ms timer control
finishes in both engines. Coordinating native I/O and virtual progression before
frame selection remains open as **JS13-L43** (**D6.4.1 / D6.4.4 / D10.1.7**).
No visibility override or extra frame advance is applied.

AWS now executes26 dynamic classic chunks. Four mandatory chunks still exceed
the unchanged128KiB async/defer cap; its prior/current native pixels remain
identical and blank. The fresh AWS pair is high at **12.86% mean error /38.60%
changed area**. Its previous canonical pair is archived. The1066 other rows
and captures, all URLs, reviews and reference-design audit are preserved.
**111 high,zero critical,956 low** remain.
[Follow-up evidence](comparison/repairs.html#round-139). Goal remains active.


Round140 fixes three shared defects: finite I/O delivered after virtual-frame
selection (JS13-L43), undeclared opacity retained after no-fill animation
completion (RI-189), and supported background keyframes rejected by the host's
property filter (JS13-L44). The existing frame slices, native request ownership
and parser normalizer are reused (**D6.4.1 / D6.4.4 / D10.1.7 / D9.1.3v2 /
D8.4.4 / D9.5.6v2**). No visibility override, extra capture interval or source
budget increase. Late completions and explicit zero windows remain frozen.

**44 focused checks,7 MIR checks,23 Chromium controls and630/630 JavaScript
layout baseline checks pass.** The baseline ran once at this milestone; no
full multi-category baseline or catalog capture sweep was repeated.

Kotlin's full sidebar now renders. The smaller44px announcement banner and
resulting spacing offset remain, but header, navigation, article, version and
cards are present. Its inspected pair is below the automated major threshold
at11.14% mean error /24.38% changed area; the prior manual major omission is
resolved with its history preserved. Both final frames are refreshed and the
old canonical pair is archived. The1066 other rows/captures, all URLs, other
reviews and reference-design audit remain intact. **110 high,zero critical,
957 low** remain across1067 pairs.
[Repair evidence](comparison/repairs.html#round-140). Goal remains active.
