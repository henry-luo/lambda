# Marble Drop debug interaction performance

## Scope and acceptance

Optimize the existing `debug_native` build for puzzle-tab hover, page scrolling
and marble movement. Keep the sepia artwork, level compositions, game rules,
sound and 280 ms rail timing. Verify normal and 2x device density, native pixel
output, cache invalidation, all sixteen puzzles and the Radiant baseline.

This work follows **D2.11.2** (external SVG shares the SVG representation while
preserving image-document style isolation) and **D4.5.1v4** (owned, current view
and resource state) in `doc/Lambda_Formal_Design.md`.

## Diagnosis and changes

The initial native debug profile separated template generation (about 0.2 ms)
and layout (12–15 ms) from paint. Hiding the sketches reduced replay by about
7 ms but left recording near 52 ms. Hiding the apparatus reduced recording to
under 1 ms: SVG images bypassed the existing inline SVG raster layer and rebuilt
an isolated style tree on each repaint.

- `render_media.cpp` now offers SVG image content to the existing bounded layer
  cache in `render_svg_inline.cpp`. Image layers key the source surface handle,
  source generation, sampled clock, font resources, image resources, viewport,
  density and fractional pixel phase. Host hover and unrelated style epochs
  cannot change an isolated image's paint. Animated or changing sources retain
  the normal invalidation behavior. The existing 48 MiB layer and 256 MiB
  document limits and document-owned teardown remain in force.
- Aligned SVG layers use the CPU copy path where possible and cached native
  image uploads under vector clips. They avoid projecting an already aligned
  bitmap merely because a rounded clip is present.
- `render_background.cpp` reuses its existing 64 MiB / 64-entry shadow cache for
  ordinary outer shadows as well as transformed shadows. Keys retain used
  geometry, fractional origin, clipping extent, color, radii and exclusion.
- `rdt_vector_tvg.cpp` intersects transformed image sampling with conservative
  clip bounds before allocation and sampling. The original clip paths still
  determine exact coverage, including the antialiasing fringe and tile offsets.
- The demo replaces its continuously animated CSS clock with an owned native
  frame request. It keeps routing on 40 ms model steps, interpolates the pose
  between steps and writes only the moving marble's presentation transform.
  Ordinary frames avoid template patches and whole-page layout; device changes
  and commands retain the current pose when they redraw. Idle, paused and solved
  boards request no frames. Undo and close cancel pending requests. Rendering
  stays pure and mutations stay in handlers (**S12.1.3**).
- The paper's repeated dots use one SVG pattern instead of hundreds of circles.

## Evidence

All temporary profiles, immutable before/after debug executables, source hashes,
event fixtures and pixel captures are under `temp/marble-drop/perf/`.
`compare.py` alternates the frozen debug executables for three pairs at each
density. It drives real hover, wheel and game events, requires native pixel and
game-state assertions, and measures native paint recording plus replay after
four warmup paints. Initial loading and file encoding are outside the measured
steady-state paint window. Device transitions during marble movement remain
inside it. This is paint timing, not a claim about total input-to-display time.

The baseline executable hash is
`af6bf36988b559b8b728fa5262733796f9f4a2442587da2e56d65d16ffd66959`.
The benchmark report records the final executable and source hashes; both
executables are verified debug builds. No release build or disabled debug
instrumentation is used.

The final three-pair comparison uses the same 1200x800 CSS viewport at both
densities (1200x800 and 2400x1600 physical pixels). Values are medians of three
run medians/95th percentiles, in milliseconds for paint recording plus replay.
All 36 processes exited successfully and passed their pixel and game assertions.

| Interaction | Density | Before median | After median | After p95 | Speedup |
| --- | --- | ---: | ---: | ---: | ---: |
| Level hover | 1x | 85.37 | 18.10 | 18.91 | 4.7x |
| Level hover | 2x | 166.56 | 49.97 | 50.47 | 3.3x |
| Page scroll | 1x | 86.34 | 17.84 | 19.06 | 4.8x |
| Page scroll | 2x | 169.97 | 49.51 | 50.82 | 3.4x |
| Marble motion | 1x | 88.02 | 1.08 | 63.81 | 81.8x |
| Marble motion | 2x | 174.87 | 1.52 | 124.57 | 114.9x |

Marble motion uses `wait` events to advance the document's native frame loop.
`advance_time` also forces an extra full repaint after that loop, so it would
mix observation work into moving-frame timings. Hover and scroll retain the
normal full-page paint path. Device transitions, the initial drop and collection
remain inside the animation window: they still rebuild apparatus SVG content
and account for the larger p95. Ordinary motion is inexpensive; a full Retina
page paint remains about 50 ms. These figures do not imply 60 fps for every
interaction or remove cold apparatus redraw costs.

The final debug executable SHA256 is
`22135690c8eacbcebbe3c2b72dc42e95e60c1b55695b887989cda22b1f035ce3`.
`comparison.json` contains every run and executable/source provenance;
`compare.py` reproduces the interleaved comparison. Both are in
`temp/marble-drop/perf/`.

Before/after native captures at 1200x800 CSS pixels had mean channel differences
of 0.0062 at 1x and 0.0025 at 2x on a 0–255 scale. Maximum differences were 8 and
6 respectively, concentrated at antialiased edges and the repeated dots.

## Qualification

- `make build`: debug-native build passed. No release build was used.
- Renderer suite: **239/239** passed, including image-layer reuse across host
  hover, scroll and layout; source, clock, viewport and resource invalidation;
  fractional placement; ordinary shadow reuse; and clipped transformed-image
  sampling at both densities and nonzero tile offsets.
- Marble Drop: **6/6** native scenarios, **243/243** assertions, including real
  native sound playback, all sixteen machines, compact layout and Retina motion.
  Motion verifies fractional movement, selective/full pixel parity, pause position
  and pending-frame cancellation after undo. The pure model golden retains **167** checks,
  including all sixteen solutions at par.
- Full Radiant gate: **0 failures**, **6204 passed**, **141 partial**, **2 skipped**
  (**6347** total). This includes **700/700** UI cases, **503/503** paged cases,
  **218/218** text-query cases and all **3294** layout cases (**3157** full and
  **137** partial). Four expected visual failures remain in the partial count.
  The final native run had access to macOS graphics and audio services; the
  restricted run stalled on graphics-service access and could not play audio.
- `make lint ARGS='--rule ^no-int-cast-radiant$'`: passed.
  `git diff --check`: passed.

The broad gate initially exposed an existing text-query test compile error: a
DOM text pointer was passed to a selected-view API. Its one-line call now uses
`lam::view_require_text`, and the complete text-query suite passes. No test
expectations or thresholds were relaxed.

Final aggregate artifacts: `temp/marble-drop/perf/radiant-summary.json` and
`radiant-baseline-access.log`. Native scenario results are in
`final-ui-access.log`.
