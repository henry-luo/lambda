# Timeline chart implementation

> **Status:** paused at the user's wrap-up request, 2026-10-12; full parity is incomplete.
> **Design:** [Timeline package](../Lambda_Pkg_Chart_Timeline.md).
> **Formal linkage:** D7.2.1–D7.2.4/D7.2.6 (source packages),
> S12.1.1v2/S12.1.3 (pure presentation and procedural handlers),
> S9.1.4/S9.3.1 (instance state and captures), S7.4.1/S7.4.2 (errors),
> D7.5.3 (native host boundary), D8.1.1v17 (execution tiers).

## Scope and acceptance

The full proposal remains the completion contract. The machine-readable
[reference inventory](../../test/timeline/reference_inventory.json) pins all four
upstream commits, option-definition hashes, declared fields, examples, and feature
families. Its host statuses remain open until acceptance evidence exists.
Upstream source snapshots are read-only research under `temp/timeline-references/`;
they are not runtime dependencies or vendored implementation code.

## Work record

- Pinned React Chrono 3.3.3, TimelineJS 3.9.13, vis-timeline commit
  `67b24bb2b80bf5fef510476b2897e0a3e32f3f63`, and amCharts 5.21.0.
- Core modules now cover canonical element/map normalization, exact astronomical
  dates and deep-time coordinates, source adapters, shared card/axis/story
  presentation, curved projection/inversion, immutable interaction state,
  slideshow timing, media descriptors, and resource request/result lifetimes.
- Animation support moved to `lambda.chart.playback`; the seven old timeline
  animation exports forward to it. Existing chart animation goldens are retained.
- dtna translates its legacy props into the core schema and supplies its skin.
  Rendering and live handlers delegate to the timeline package. Legacy dtna
  native DOM/pixel checks pass; `timeline_options` exposes core configuration.
- Hidden/compressed intervals use a shared exact piecewise mapping for events,
  ranges, ticks, and inverse picking. Overlaps, seam bias, local calendar
  recurrence, DST, leap days, recurrence limits, and hidden-event disclosure have
  golden coverage. Local-origin arithmetic follows S6.2.2v3/S6.2.3.
- Groups use stable sibling ordering, nested visibility, collapse controls,
  subgroup rows, normal offsets on curves, and constrained heights. Selection
  survives collapsing a group. Pure contracts and real native button interaction
  are covered separately.
- Pointer pan/pinch uses the shared inverse chronology mapping and reducer;
  cancellation restores the starting window, captures isolate pointers, bounds
  apply to every change, and controlled windows emit requests. Native drags and
  click-to-use pass in T0/MIR. Deep-window constraints and zoom retain exact local
  origins. Wheel modifiers and pointer-anchored zoom are wired to the native host.
- Clustering uses arc-distance runs, lane/category criteria, stable member IDs,
  count/title/render callbacks, explicit expansion limits, and native expansion
  back to every source event. Collision priority controls placement while retaining
  canonical event order.
- Time-scaled layout measures/projects only window-intersecting events. The
  accessible chronology has the same bounded window and an offscreen summary;
  activation can follow offscreen events without changing the window span.
- Point/range uncertainty, eras, and reference lines/points/bands share the same
  time mapping and lane offsets. Current-time references consume explicit input;
  pure rendering never reads a clock (S12.1.1v2).
- Chronology axes support multiple axes, two-sided major/minor ticks, grids,
  week numbers, rotation/tangent labels, titles, and measured collision omission.
  The existing chart calendar helper now accepts an explicit week start, retaining
  its Sunday default for existing consumers.
- Shared cached ASTs exposed a T0 view-state identity bug: activation looked up
  the first AST match, although imports can have distinct execution modules.
  `interp_view_template_ref` now matches both AST and module, consistent with
  D8.5.1v3 (immutable cached AST, execution-owned state), S9.1.4, and S12.1.3.
  `lambda_template_shared_import` reproduces the defect before the fix (2/5) and
  passes afterward (5/5). Timeline card expansion/search with dtna imported in
  the same document also passes after the fix.
- Sixteen timeline golden scripts plus the existing chart calendar control test
  pass in MIR Direct. T0/AUTO and updated native checks are being rerun. The
  acceptance inventory records verified partial families and their remaining
  work; no full feature family is declared complete yet.
- Unicode search now preserves authored code-point spans through optional
  diacritic removal and simple folding (S17.4.1/S17.7.1), with native highlighting,
  match navigation, and date/group/tag filters. Rich-tree highlighting remains open.
- The overview navigator shares chronology mapping and window constraints with
  main exploration; native range handles and pointer dragging use the same reducer.
- Quantitative overlays share the chart mark dispatcher, scale, legend, and axis
  implementations. A general pure custom-coordinate seam projects native marks
  onto timeline paths. Continuous interpolated vertex normals prevent offset
  geometry from jumping at sampled curve vertices; the native curved-overlay
  fixture passes all five checks. SVG rendering propagates projection failures.
- Chart mark `timeline` maps native chart encodings into the canonical history
  model, including element/map parity and concat composition. Schema validation
  now checks named scales, chronology domains, window constraints, callbacks,
  controlled projection shapes, and native series children before layout.
- Source export now supports JSON, CSV, XLSX, and accessible HTML table source,
  ordered fields/localized headings, original date precision, exact metadata,
  dataset scopes, and explicit rich-content policies. XLSX uses the existing ZIP
  writer and inline SpreadsheetML strings; the package structure follows the
  [Open XML workbook specification](https://learn.microsoft.com/en-us/office/open-xml/spreadsheet/structure-of-a-spreadsheetml-document).
- Frozen document export and procedural `timeline.save` produce HTML/SVG and
  native PNG/JPEG/PDF, including paginated table PDF. `radiant.export_document`
  is a general procedural host boundary (D7.5.3/S12.1.1v2); it consumes in-memory
  HTML and reuses the CLI's export sessions/encoders without intermediate files.
  Encoder/write failures propagate. Local posters, recorded image/frame bytes,
  authored fallbacks, explicit external resources, and text/omit diagnostics
  replace live media in snapshots. Font admission and full pagination policy
  remain open.
- The full chart suite passes in AUTO (107 scripts). Twenty-two timeline
  contracts plus procedural export pass in MIR Direct. All ten native timeline
  fixtures pass in T0 and MIR Direct. Focused export checks verify all eight
  formats, exact metadata/date roundtrips, Open XML parts, image/PDF signatures,
  and invalid-path propagation. Later edits require affected checks to be rerun.

- Editing now has immutable event/group/subgroup add/update/remove requests,
  stable IDs, per-item permissions and explicit overrides, numeric/calendar/custom
  snapping, range handles, cross-lane drop, keyboard adjustment, preview,
  confirmation, rejection, cancellation, and token/revision guards. `apply_edit`
  reconstructs and validates the candidate from current source values; supplied
  candidate data cannot bypass permissions (S9.1.4/S7.4.1). Forty-eight pure
  checks pass in T0; native editing passes 16/16 in T0/MIR. Remaining editing
  acceptance includes external drop creation, authoring controls, asynchronous
  validation presentation, and full pinned vis option parity.
- Retained hosts accept `timeline_action` / `source_update` with the next immutable
  spec and optional expected revision. The native application fixture publishes
  three commits through one stable model instance. Numeric date labels trim only
  insignificant fractional zeros, preserving exact deep-time values and exponent
  digits (S6.2.2v3/S6.2.3).
- Native capture now follows retained presentation nodes on incremental and full
  document rebuilds through the same correspondence helper as frame requests
  (D7.5.3). A fresh pointer press clears compatibility-click suppression so an
  intentional Apply/Cancel click works immediately after dragging. The fourteen
  native timeline fixtures pass in MIR Direct; state-store tests pass 16/16.

- Resource lifecycle now retires stale descriptor/revision tokens, emits explicit
  cancellation requests, frees concurrency slots, enforces deadlines, and bounds
  retries. Priority-only changes reuse the same request. Refresh keeps the last
  valid dataset, validates against authored groups/encoding, and replaces source
  rows without duplication. Invalid completions become resource errors; loading,
  empty, and error slots have visible default/custom presentations. Thirty-four
  pure checks pass in T0; the native lifecycle fixture passes 14/14 in T0/MIR.
  Pure resource work consumes explicit time (S12.1.1v2); application task ownership
  follows S13.1.1v2–S13.1.5. Automatic asynchronous host execution and disposal
  remain open rather than being inferred from request emission.

- Slideshow plans include an initial delay, independent transition duration,
  easing, directional per-slide progress, and overall progress. Fade, slide,
  and reveal styles consume explicit samples; reduced motion preserves timing
  while completing visual transitions. Manual activation aligns the retained
  playhead before the next tick. Thirty pure checks pass in T0; the native
  delay/fade/progress/pause/resume fixture passes 10/10 in T0. Shared playback's
  default remains zero delay, and focused legacy animation checks pass in AUTO
  (S12.1.1v2/S12.1.3). Media-aware advancement and outgoing/keyed motion remain open.

- Narrative initialization, manual controls, quick jump, keyboard movement, and
  playback now share the declared story order. Canonical introductions support
  rich content, media, explicit IDs, start-at-end, and panel ordering; TimelineJS
  title data translates into that same contract. Introductions stay outside the
  chronology axis and source event export. The 25 new pure checks pass in T0/MIR;
  native navigation and focus pass 10/10 in T0.

- Latest package gates before narrative ordering: 26 timeline/procedural export
  scripts pass in MIR, all 110 chart scripts pass in AUTO, and all 16 native
  timeline fixtures pass in MIR. The six scripts affected by narrative ordering
  pass in T0/MIR. New changes still require the broader affected gates.

- Pure media sessions now emit play/pause requests, validate completion identities,
  wait for every player, freeze wait time while paused, and continue on end,
  failure, or bounded timeout. Restart/seek/revision/cycle changes retire tokens;
  explicit stop suppresses active autoplay until a new activation. Thirty-five
  pure checks pass in T0, and the native application-player fixture passes 14/14.
  This verifies the request/controller contract, not automatic native player
  execution. Slideshow offsets now reuse `math.cumsum` rather than recomputing
  every duration prefix (S12.1.1v2/S12.1.3, S13.1.1v2–S13.1.5).

- Bookmarks share stable UTF-8 addresses, pure restoration, and committed-state
  notifications. Native external fragments and back/forward restore automatically;
  15 pure checks pass in T0/MIR, and the native fixture passes 12/12 in T0/MIR.
  Adapter-generated IDs retain their unstable provenance and cannot silently
  qualify as authored addresses. Window subscriptions reuse the document-owned
  frame-target lifecycle and follow logical owners across rebuilds. Cancellation,
  removed peers, and the page body handler are covered by a separate native
  fixture. Generic navigation queues now follow rebuilt nodes with precise
  leases and execute at the common quiescent pump, including requests from
  buttons and slideshow ticks (D4.5.1v4/D5.3.3, D5.4.1, D7.5.3).
- Package gates before retained media ownership: all 29 timeline/procedural export scripts pass in T0/MIR;
  all 114 chart/procedural export scripts pass in AUTO; all 19 native timeline
  fixtures pass in T0/MIR. Nine existing DOM history/navigation fixtures pass.
  The new JavaScript native-fragment fixture verifies hashchange URL fields.
  Full baseline gates remain outstanding after these engine changes.

- Native local audio/video commands now execute after rendering and report
  accepted clip-end/error notifications; inferred remote providers retain
  application controllers. The initial native fixture passes five assertions,
  including explicit clip end and missing-source failure. Pure media playback
  has 39 checks, including duplicate/paused notifications and provider dispatch.
  Decoder ownership moved from pass-local embed properties to document-owned
  media state, preserving volume through parent rerenders/resize and resetting
  it on source changes. Iframe media uses its own document URL/state. Twenty
  state-store tests pass; native ownership and iframe checks pass in T0.
  Actual audio-end notification and removal pause now have native fixtures;
  broader provider/platform parity remains open. The full 22-fixture timeline
  suite passes independently in T0 and explicit JIT before the outgoing-card work.
  (**D4.5.1v4/D5.3.3, D7.5.3, S12.1.3**).
- Verification correction: `LAMBDA_EXEC_BACKEND=mir` selects AUTO through the
  unknown-value fallback. Eager MIR verification uses `jit`. The latest explicit
  `jit` run passes all 29 timeline/procedural export scripts. Current native
  checks use `jit`; AUTO evidence is not treated as eager MIR.

## Engine baseline evidence

The latest focused checks pass: 25 timeline/procedural export scripts in MIR
Direct, 13 native timeline fixtures in T0, 57 editor unit tests, and 83 display-list
tests. New coverage exercises measured rich cards, toolbar settings, synchronous
callback reentry, recorded image SVG export, and supplied viewport dimensions.
The shared card module now supplies live content, measurement, and frozen export.
Notifications are delivered after template state assignment, so nested callbacks
observe and retain the committed interaction state (S9.1.4/S12.1.3).

Native toolbar initialization exposed a general editor defect: rebuilding an
element's attributes replaced a full union/nullable contract with its coarse
TypeId, then copied the wider payload into the narrower slot. Unchanged contracts
now retain their storage descriptors, and coarse transition replay declines
contracts it cannot represent (D3.4.5). The regression covers both a union and a
nullable string through attribute addition/removal; the native toolbar fixture
failed with SIGSEGV before the fix and passes 7/7 assertions afterward.

`make test-lambda-baseline` reached 4,555/4,557 runtime tests and 2,112/2,112
input tests. The failures also reproduce in isolated reruns:
`latex_test_latex_phase3_corpus` times out at 60 seconds; `edit_view_only` differs
in serialization of `disabled` versus `disabled=""`. The latter already has
matching before/after evidence in
`temp/ui_dtna/foundation_increment/lambda_failures_comparison.json`.
The expected output and timeout have not been weakened.

`make test-radiant-baseline` now builds all targets. Backend-neutral border
geometry lives in its own compilation unit; list counter scope methods live with
their list/pseudo-content dependencies. The run reached 5,062 passes of 5,423
checks, with 354 partial and one skipped. Layout and visual-render categories
passed. Remaining failures were two UI fixtures (`doc_editor_dct_image_pdf`,
`dtna_segmented_modes`), one page snapshot, native view/scene3d idle timeouts,
and an obsolete display-list assertion that dashed borders require fallback.
The latter now has a positive dashed-stroke regression and the full 83-test
display-list suite passes. Expectations/timeouts have not been weakened.
Later engine changes still require the affected baseline gates to be rerun.

## Remaining gates

1. Schema, exact dates, element/map parity, animation namespace compatibility.
2. Shared presentation and dtna compatibility, including native pixel checks.
3. Time exploration, interaction, editing, and source lifetime.
4. Narrative, providers, resources, slideshow, accessibility, localization.
5. Curved coordinates, inversion/picking, quantitative overlays.
6. Browser host, exports, performance, full pinned-option/example parity.
7. Package documentation and required Lambda/Radiant baseline gates.

- Outgoing story samples now identify the preceding card across normal/reverse
  playback and repeat cycles, retire it when the transition finishes, and suppress
  it under reduced motion. Reverse playback uses the preceding half-open interval
  at exact boundaries. Forty pure story checks pass in T0/JIT. Export and outgoing
  cards share media snapshot conversion; posters/fallbacks replace player records.
  Native fade/pause/resume/poster checks pass (10 assertions). All 23 timeline
  fixtures pass in concurrent T0 and explicit JIT runs. Motion acceptance stays
  open for keyed event changes and interrupted windows/guides.
- Native explicit `inert` attributes now use shared flat-tree ancestry for pointer
  targeting and sequential/programmatic focus, following the
  [HTML inert subtree rules](https://html.spec.whatwg.org/multipage/interaction.html#inert-subtrees)
  at the D7.2.5/D7.5.3 host boundary. Five native assertions pass; slot/host ancestry
  has a passing unit regression.
- UI runners now put machine results and phase traces in a process-specific
  directory under `temp/ui-test-results/`. Concurrent runners previously deleted
  each other's JSON before coordinator parsing. Concurrent full 23-fixture T0/JIT
  runs pass; result parsing still requires the original schema and assertion counts.

- Rich introduction redisplay exposed a native ownership defect: a retained pure
  element was used as its DOM node and poisoned by subtree retirement. All
  Lambda UI source values now receive separate mutable occurrences, including child buffers,
  attribute storage, and descendant text nodes (S9.3.1, S12.1.3, D4.5.1v4).
  The original source remains the reverse-map/template key. The element shape
  mutation helper is shared with Input writers and preserves qualified and
  nominal identity (S2.4.3v3, D3.4.3v5, D3.4.5). A new regression verifies source
  attributes, nested elements, inline strings, retirement, and redisplay.
  Both affected story fixtures also pass with AddressSanitizer. Input-owned
  value allocation and long-lived rebuild budgets still need the release
  performance/lifetime gate.
- Sanitizer diagnosis also found an uninitialized `lam::Own<FontProp>` on the
  retained list-marker path; zero initialization fixes the invalid font copy.
  The complete view reuse/secondary view runner passes 572 tests after updating
  its cascade fixture to create authored backed elements. Generated boxes retain
  their existing null-backing construction and selector exclusion.
- Introduction media now uses the shared static snapshot policy in document
  export. Poster replacement, missing snapshot rejection, and explicit text
  diagnostics join event-media coverage; the export contract has 35 checks.

- The source/DOM boundary also preserves the render map's original child graph;
  only the copied DOM content slots point to mutable descendant occurrences,
  including arbitrarily nested arrays within the DOM depth budget. A clear COW
  bit cannot transfer ownership: the render map observes the original value too
  (S12.1.3, D4.5.1v4/D4.5.2). Native source regressions cover qualified/nominal
  identity, descendant edits, unmarked arrays, retirement, and redisplay. Retiring
  an older occurrence removes its source lookup only if it still owns that key.
- Render-map retransformation publishes new result identities even when their
  values compare equal. The event dispatcher now commits the matching DOM update
  rather than leaving its next lookup keyed to the previous result. Full rebuilds
  splice the retained document parent and rebase canonical ranges before retiring
  old nodes. The former navigation failure was an always-on text-layout invariant
  reached through the old document root, not a sanitizer memory violation.
  Both affected story fixtures pass again under AddressSanitizer (20 assertions).
  The complete view reuse runner passes 574 tests; Tetris gravity/autoplay pass
  29 assertions; all 23 timeline fixtures pass in T0, and all 24 timeline/dtna
  fixtures pass in explicit JIT. These supersede the earlier focused counts.
- The intervening full Radiant baseline caught Tetris, source-graph DOM edits,
  and displayed-map handoff regressions. The first two have focused passing
  evidence above. Map frames now follow the same rebuilt-node correspondence
  as other retained owners, so queries retain the last displayed frame until
  the next paint (D4.5.2/D7.5.3). Its checks and both full engine baseline gates
  remain pending; the earlier baseline run is not a clean gate.
- Equal results with a new identity require a DOM ownership update; an exact
  same-value return preserves the existing DOM and its imperative edits. Parsed
  HTML retains its own mutable element/text storage, while Lambda presentation
  values retain immutable sources. These refinements resolve all 15 affected
  frame, slideshow, and SVG fixtures and all three affected viewer cases. The
  full native UI baseline passes 505/505, and the map suite passes 20/20. The
  broader Radiant run still has viewer network-admission failures and idle
  timeouts; neither full engine baseline is a clean gate yet.
- Rich Unicode search now marks matches across inline element boundaries while
  retaining authored text, qualified names, attributes, and immutable source
  values. The shared pure `radiant.with_content` boundary uses ordinary element
  allocation, attribute storage, content normalization, and declared-content
  validation (S2.6.2–S2.6.4, S17.4.1/S17.7.1, D3.4.5/D4.5.2). It crosses the
  additive Jube value table with an explicit minimum table size (D7.3.6), rather
  than exporting another runtime symbol. All 36 search contracts and nine native
  rich-search assertions pass in T0/JIT. Parsed-HTML ownership tests pass 7/7.
  All 60 scene3d checks and all affected viewer cases pass outside sandbox
  restrictions. The subsequent complete Radiant baseline passes, including
  506/506 native UI scenarios and map 20/20. Lambda baseline remains 4555/4557:
  the previously recorded LaTeX corpus timeout and disabled-attribute serialization
  failure remain open. Nine element-content contracts also pass with collection
  forced at every allocation in T0/JIT.
- Search/filter changes now reconcile activation immediately, retain selection,
  recover the nearest event after empty results, preserve input focus, and focus
  the first exposed group header when a nested group disappears. Controlled
  fields emit requests without replacing authoritative values. Unknown collapse
  targets return value errors. The state contract has 42 checks; the new native
  fixture has 11 assertions. All 26 timeline/dtna fixtures pass in T0/JIT before
  the subsequent localization changes.
- Locale formatting now flows through canonical dates, chronology axes, message
  interpolation, plural forms, accessibility summaries, cluster descriptions,
  and tabular export headings. English, Chinese, French, and Arabic are bundled;
  arbitrary authored message/formatter overrides remain pure. Gregorian precision,
  astronomical coordinates, exact default decimal labels, explicit offsets,
  source roundtrips, RTL overrides, and literal pattern text have acceptance
  coverage. Further pinned reference languages and browser-host coverage remain
  open. All 42 locale contracts and nine native assertions pass in T0/JIT.
- Native group/tag/date filter controls now share the reducer, preserve symbol
  tags and exact decimal/calendar timestamps, clear configured constraints, and
  restore authoritative values after held controlled requests or invalid commits.
  Eighteen pure contracts and 21 native assertions cover these controls. The
  broader chart/native suites are being rerun.
- Those date commits exposed two host defects. The focus-session baseline now
  belongs to retained form state (D4.5.2), surviving same-identity regeneration.
  Sequential-focus policy returns its chosen node and native applies the shared
  edit-commit/change/blur/focus transition (D7.2.5). The transition adapter also
  retains the keyboard-origin flag. The native Tab snapshot now records the
  document wrapper's actual HTML child index after a commit-driven rebuild;
  focus, caret, selection, and pending-paint assertions are unchanged. The form
  state unit suite passes 20/20 and Radiant dimension lint passes. Full engine
  baseline gates are pending after these host changes.
- The first subsequent full Radiant run found two more focus-sequence defects:
  `dom_pkg_commit` lost blur after a change-triggered rebuild, and dtna pagination
  consequently missed its blur commit. Pending focus events now retain original
  node leases and follow retained identities between callbacks (D4.5.2/D7.2.5).
  Frames, pointer capture, navigation, and focus reuse one reference-rebasing
  helper. Both regressions and affected focus/filter/source fixtures pass in
  T0/JIT and ASan; seven focused ASan scenarios pass in T0 and five in JIT.
  The state-store unit suite passes 21/21, including lease handoff and identity
  rejection. A fresh complete Radiant gate remains pending.
- Source replacement now uses one reducer path for pure/native updates and
  loaded-data arrivals. It notifies activation/selection changes, retains the
  nearest old chronology neighbor, recovers from empty data, preserves external
  focus, constrains/scrolls the existing window, cancels obsolete resources,
  rejects stale revisions before validation, and retires edits when source
  records change even without a revision change. Retained slides keep their
  fraction/cycle across order, duration, and direction changes; empty autoplay
  input is supported. All 28 source contracts pass in T0; 39 resource contracts
  pass in T0. The preceding stable checks passed all 31 timeline goldens and
  33 affected native scenarios; final expanded JIT and full gates are pending.
- The expanded source/resource/story/state JIT gate passes 5/5. The subsequent
  full Radiant run passes all 511 UI and 154 DOM cases but exposes a parent-link
  focus owner mismatch after entering an iframe. Pointer hit testing now binds
  the document of its tree, and capture binds its actual node owner (D4.5.2,
  D5.4.1/D7.5.3). Both iframe viewer regressions pass after the fix. The full
  Lambda baseline passes 6671/6673, with the same recorded LaTeX corpus timeout
  and disabled-attribute serialization failure; it predates the new presentation
  helper below. Full engine gates after that helper remain pending.
- Rich-array matching now uses the same text stream as highlighting, including
  scalar text and matches across nonvisual nodes. Tags remain separate labels.
  Pure render callbacks share card context and their errors remain value errors;
  predicates require boolean results. `radiant.present_content` constructs a
  structural presentation through the existing element reconstruction and private
  shape clone, leaving nominal sources and strict `with_content` validation intact
  (S2.1.4, S2.6.2–S2.6.4, D4.5.2/D7.3.6). All 25 new contracts pass in T0/JIT,
  including collection forced at every allocation. The existing search and
  element-content goldens pass in both tiers. Twelve native assertions pass in
  T0/JIT. Eight affected native scenarios pass under ASan in T0 and four in JIT,
  including source updates, resources, controlled filters, and rich content.
- Immutable source preparation now caches normalization independently of query,
  selection, and media-result changes. Source URL/revision/data and callback
  capture changes invalidate it (S5.1.4v2, S9.1.4/S9.3.1). Fourteen new pure
  contracts pass in T0/JIT. This cache does not yet make window queries indexed;
  interval indexing and clustering before measurement are being implemented.
- Faster preparation exposed a native playback scheduling defect in the existing
  end-of-media fixture. Headless assertion retries treated only loading/buffering
  as pending native work and could exhaust virtual time before AVPlayer delivered
  completion. The shared pending-work predicate now includes playing media;
  the real native completion event still drives advancement (D7.5.3). Playback,
  completion, and lifetime regressions pass in T0/JIT and ASan T0/JIT
  (23 assertions in each run).
- A pure immutable interval index now preserves canonical order, open bounds,
  inclusive endpoints, and exact decimal coordinates (S6.2.2v3/S6.2.3). Sixteen
  contracts pass in T0/JIT, including bounded leaf visits for a narrow query over
  4,096 points. Integration and release performance measurements remain pending.

## Wrap-up checkpoint

The user requested a wrap-up on 2026-10-12 before full proposal completion.
Implementation work stops at this checkpoint after recording the current gates;
the implementation is paused, not complete. No upstream feature family is
declared complete: the inventory currently has 51 partial and 215 planned host
or example records.

The next scalability step is to connect the tested interval index to retained
preparation, cache visibility by source/query/filter/group inputs, and cluster
projected candidates before rich-card measurement. The release benchmark runner
has passed Python syntax and Lambda import checks, but has not been run; there
are no new release performance claims. Further open work includes automatic
asynchronous resource execution and disposal, the interactive browser adapter,
remaining editing/media/motion/export options, additional reference locales,
and complete pinned example/option acceptance. The proposal and inventory remain
the authoritative scope for resumption.

The final full Radiant baseline passes after the presentation, pointer-document,
and native playback fixes: 512/512 UI, 154/154 DOM, 101/101 viewer, 479/479 view
reuse, and 21/21 state-store cases. The overall report records 5,258 passing,
354 partial, and one skipped test, with no new unexpected failures. Evidence is
`temp/timeline-references/presentation-cache-radiant-baseline.log`.

The final Lambda baseline passes 6,674/6,676: input parsers 2,112/2,112 and
runtime 4,562/4,564. The two failures remain
`latex_test_latex_phase3_corpus` and `edit_view_only`; no expectations were
weakened. The report is
`temp/timeline-references/presentation-cache-lambda-baseline.log`. The interval
index's 16 contracts pass through the real golden harness in both T0 and JIT;
native playback/completion/lifetime also passes ASan in both tiers. Python
benchmark syntax, Lambda benchmark-module import, inventory consistency, and
`git diff --check` pass. Release measurements and full parity acceptance remain
open.
