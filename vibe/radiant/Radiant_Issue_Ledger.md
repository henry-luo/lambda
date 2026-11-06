# Radiant Issue Ledger

Working issue record for Radiant. Entries are grouped by engine area
(`RAD01`–`RAD22`). Entries originating here use an `L` suffix, such as
`RAD07-L1`, to avoid collisions with the numbered items under
[Area known issues](#area-known-issues). Resolved entries retain stable anchors
and their verification evidence.

## Open issues

<a id="rad13-l1"></a>

### RAD13-L1 — Scene3D gaming profiles remain incomplete

**Updated:** 2026-10-28. The [gaming design](Radiant_Design_Scene3D.md)
stages native gameplay support while preserving pure authored values and
document-owned live services (**S12.1.1v3 / S12.1.5v2, D9.4.4–D9.4.5**).
Source relocation and the shared scene coordinator are implemented. The first
collision extensions add discrete capsule/convex contacts, mass/inertia,
point/ray tests and named-pair closest-point queries through the shared C
provider. The convex delivery also preserves owned live worlds on compatible
geometry rejection; full M2 remains partial.

Outstanding native work is the design's remaining collision/CCD profiles and
M3–M12: articulated/character movement, navigation, animation graphs, emitters,
game audio, genuine 2D, depth effects, baked/dynamic indirect lighting, fog and
soft bodies. The four Lambda Script gameplay-algorithm scopes remain KIV.
Acceptance and receipts live in the
[implementation record](../impl/Radiant_Impl_Scene3D_Gaming.md); no future
profile is advertised as implemented. Its M1 core baseline is green; Radiant
qualification retains six text/inline/sizing layout failures, the existing
header cap failure and the module-boundary import debt described there.
The M2 aggregate also records an unreproduced render-adapter idle timeout;
direct and shared-harness component reruns pass with unchanged checks. The
failed aggregate is retained in the implementation receipts.
Convex qualification exposed the live async sanitizer limit
[LR12-43](<../Lambda_Issue_Ledger (fixed).md#lr12-43>), fixed 2026-10-29 under
**D6.1.1 / D6.3.3 / D6.3.6** by reusable-stack lifetime and ASan fiber
bookkeeping. The original runtime and mesh event fixtures now pass **22/22**
and **17/17** assertions with forced/poisoned GC and stack-use-after-return
checking. The standing layout/header/boundary debt above is unchanged.

<a id="rad07-l3"></a>

### RAD07-L3 — Outlined math and SVG text lose native font rasterization — FIXED

**Investigated:** 2026-10-09, macOS 26.3, Chrome headless 154.0.8037.57.
**Fixed:** 2026-10-09; the original comparison below records the pre-fix renderer.

**Symptom and actual fonts.** The document viewer's
`test/input/comprehensive_math_test.md` looks lighter than the same public
GitHub page. Chrome's DOM and platform-font inspection found native MathML
at 16px: `STIXTwoMath-Regular` for variables and `TimesNewRomanPSMT` for
operators in the first expression. Lambda's current default selects bundled
CMU Serif, including `CMUSerif-Italic` for variables, and KaTeX symbol
fallbacks. Both the font design and the rendering path therefore differ.
This records the observed browser/platform, not a universal GitHub font.

**Controlled comparison.** The exact bundled CMU font files, Times New Roman,
Arial, and STIX Two Math were tested at 8/12/16/24/48 CSS px, black on white,
at 1× and 2× density. Each sample uses independently positioned characters
as HTML text, SVG text, and SVG paths obtained from `radiant.math_metrics`.
Chrome's platform-font inspection confirms the expected faces. Five actual
formula sizes use the same package-generated SVG in both engines. There are
160 renderer comparisons; all font requests succeeded.

Representative results for 16px CMU italic (`Emcxabni2+=()`):

| Content | Lambda ink versus Chrome, 1× | Lambda ink versus Chrome, 2× |
|---|---:|---:|
| HTML text | +4.53% | +2.25% |
| SVG text | −20.15% | −23.24% |
| Identical SVG paths | +0.88% | +0.79% |
| Actual package formula SVG | −0.39% | +0.90% |

Ink is summed `255 − luminance` over the full sample region; it is not a
perceptual percentage or a claim of identical pixels. SVG text versus the
same outlines in Lambda has under 0.004% absolute pixel error relative to
ink. In Chrome itself, converting CMU italic SVG text to paths loses 20.84%
of ink at 1× and 23.84% at 2×. Thus the path conversion reproduces the
faintness even with the same font and the Chrome rasterizer.

**Root cause.** [`svg_box.ls:glyph`](../../lmd/package/doc/math/svg_box.ls) emits
the measured glyph as `<path>`, losing its font/glyph identity. Separately,
[`svg_text_run_path`](../../radiant/render_svg_inline.cpp) converts ordinary
SVG text to paths. `svg_text_prepare_paint_geometry` supplies those paths to
`svg_text_draw_style_content`, which paints them as geometry before the
existing native-glyph fallback. Neither case uses the native font smoothing
preserved by **RAD07-L1**. The old grayscale-mask defect is not reproduced
by the normal HTML-text samples.

**Repair.** Ordinary SVG text now uses the normal font rasterizer and glyph
compositor at its final physical size. Translation and uniform viewBox/device
scale are eligible; rotation, skew, nonuniform glyph scale, text paths, strokes,
paint servers and vector export retain geometry. Owned image resources keep
retained replay independent of temporary SVG font contexts. No stroke expansion
or global gamma change is used.

Math emits `<text>` with the measured face/style. **D10.2.4** (USER,
2026-10-21) makes used bundled faces URL references to the distributed font
files; supplied bytes without a resource URL retain data-URL embedding.
Only unencoded OpenType variants/assembly pieces retain
paths; installed-font options still depend on viewer font availability.
Font selection remains independent of rasterization: GitHub's observed STIX
Two Math face has a different design from the CMU default.

The unchanged 16px CMU italic SVG-text probe now measures **+4.53%/+2.25%**
ink versus Chrome at 1×/2×, with **5.05%/2.89%** absolute pixel error relative
to Chrome ink. Thus SVG text now matches Radiant's ordinary text coverage.
Five-font, five-size captures and six actual math formulas are retained under
`temp/math-text-fix/`; the nested fraction's nonuniformly stretched fences
remain a vector-painting case.

**Validation and limits.** Ten native SVG text/export tests pass, including
coverage at 1×/2× with independent font-context density, pixel ownership after
font-context destruction, and transformed/vector fallback. All 921 math corpus
cases and the two updated document-math UI cases pass. The focused package
run passed 66/67 cases; `latex_test_latex_phase3_corpus` still exceeds its
60-second limit even when rerun alone. That original version embedded full
font resources in every formula; D10.2.4 replaces that bundled-font policy
with shared file references.

The broad Radiant gate stopped at
`SecondaryViewTest.NumberedSparseColumnsLeaveAutomaticTracksAndImplicitDeclarationsAtTheirSourceEndpoint`.
The continuation passed layout baselines/snapshot checks and exposed two
path-count UI expectations (updated and passing), then was terminated during
the view-command suite. Full baseline acceptance is not claimed.

Generated editor math is inserted after authored HTML is sanitized, preserving
its font declarations. The new projection-isolation test passes; both editor
cases pass under JIT. `edit/view_only.ls` still exposes text corruption during
interpreter/auto round trips. This also reproduces with the saved pre-change
binary and outlined math package under `LAMBDA_GC_FORCE_EVERY=100`
(`temp/math-text-fix/editor-before-native-gc.log`), so this work does not change
the expected round-trip result or claim the GC-related defect fixed.

Pixel comparisons do not establish other platforms, dark-background tone or
whole-page layout parity. Stretched fences remain vector geometry: the nested
fraction sample is 9.52%/11.25% lighter than Chrome at 1×/2×. Ordinary formula
samples have 1.72–5.62% more ink at 1× and 2.20–3.21% more at 2×, with
fractional-position/antialiasing differences in individual pixels.

**Local reproduction and evidence** (temporary, not tracked):
[`temp/math-text-fix/report.html`](../../temp/math-text-fix/report.html)
contains the fix comparison; the original audit follows.
[`temp/math-quality-audit/report.html`](../../temp/math-quality-audit/report.html)
contains interactive comparisons and reproduction commands;
[`results.json`](../../temp/math-quality-audit/results.json) contains every
measurement; [`github.json`](../../temp/math-quality-audit/github.json)
contains the actual GitHub DOM, computed styles and resolved fonts.

## Area known issues

Imported on 2026-11-06 from the retired detailed-design set and not re-verified
against the tree. Item numbers are stable IDs: `RAD06-3` is item 3 under `RAD06`.

### RAD01 — View & DOM Model

1. ~~**The zero-field invariant is convention-enforced, not compiler-enforced.**~~ Resolved: `static_assert`s in `view.hpp` pin every view class to the size of its DOM struct.
2. **`native_element` is a transitional copy, not an alias.** `DomElement::native_element` is marked `TODO(Phase 4): Remove; use dom_element_to_element()` (`dom_element.hpp:307`), and the embedded `Element` is currently *copied* from the source element in `dom_element_init` rather than aliased. Until removed, there are two notions of "the element" to keep in sync.
3. **Incremental relayout correctness is implicit.** The skip-clean optimization depends on `layout_height_contribution` being exactly recomputed on every non-skipped child (`layout_block.cpp`). There is no assertion tying the cached contribution to the actual advance, so a bug there manifests as silently mis-placed siblings rather than a crash.
4. **Source-position bridge maturity.** The header warns that functions may be no-op `return false` pending `render_map` path recording (`event.hpp`); editor round-trip fragility is concentrated here and depends on a weak/incremental `render_map` dependency.
5. **Class-name handling is still a single-token stub.** The deprecated `RDT_VIEW_MATH` enum span has been removed, but the class-name path in `view_pool.cpp` still assumes a single class ("TODO: split on whitespace for multiple classes").

### RAD02 — CSS Style Resolution & Computed Style

1. **The 726k monolith.** `resolve_css_style.cpp` (~13.9k lines) is the largest file in the repo, and the ~248-case `resolve_css_property` switch (`resolve_css_style.cpp:4632`) is navigable only through comment GROUP banners. *Improvement:* split by property group into separate translation units keyed off a shared dispatch table.
2. **Unimplemented `content:` functions.** `counter()`/`counters()`/`attr()`/`url()`/concatenation for the `content` property are all TODO stubs (`resolve_css_style.cpp:12738`,`:12743`,`:12748`,`:12753`,`:12759`), and pseudo-element content is guessed from selector context ("TODO: improve this", `:12694`).
3. **`clip: rect()` not parsed.** `resolve_css_style.cpp:9908` has a TODO to parse `rect()` values into scroller clip bounds; currently ignored.
4. **No named colors in the HTML-attr parser.** `parse_html_color` (`resolve_htm_style.cpp:90`) handles hex/`rgb()` only, with a TODO for named colors (`resolve_htm_style.cpp:100`). Because presentational attributes use this parser and CSS uses `resolve_color_value`, a named color works in a stylesheet but not in a `bgcolor` attribute.
5. **Two inheritance code paths.** Radiant hand-rolls the `inheritable_props[]` loop (`resolve_css_style.cpp:3971`) even though the CSS side exposes `style_tree_apply_inheritance` (`css_style_node.hpp`). Two implementations of "inherit unset properties" can drift apart.
6. **Two color parsers.** `parse_html_color` (htm) vs `resolve_color_value` (css) — see item 4. Consolidating would remove a class of "works one place, not the other" bugs.
7. **Silent `NAN` on var()/calc overflow.** The depth-64 guard in `resolve_length_value` (`resolve_css_style.cpp:2457`) returns `NAN` and logs a warning, but deep-but-legal chains simply drop out with no user-visible diagnostic; downstream layout must tolerate `NAN`.
8. **Presence-flag fragility.** The many per-struct `has_*` flags (e.g. `InlineProp::has_color`, `has_svg_*`) must be set by hand in each relevant case; adding a new property case and forgetting its flag silently mis-reports "not set". *Improvement:* generate the flag/set pair from a property table.

### RAD03 — Layout Driver, Block Layout & Block Formatting Context

1. **Last-baseline alignment is unimplemented.** `compute_element_last_baseline` (`layout_alignment.cpp:242`) is a `// TODO: Implement proper last baseline calculation` returning `-1`. `LayoutOutput::last_baseline` (`layout.hpp`) therefore never carries a real value, so `align-items: last baseline` in flex/grid degrades to first-baseline/edge alignment.
2. **Hard-coded float-width estimate.** `layout_block.cpp:4123` uses `float float_width = 100.0f; // Conservative estimate` in place of a real float measurement, so float space reservation can be wrong for floats whose actual width differs materially from 100px.
3. **Replaced-element 300×150 defaults are duplicated.** The HTML default replaced size appears as scattered literals — e.g. `layout_block.cpp:5849` (`... : 300.0f`) and `5864` (`given_height = 150.0f`), plus the `layout_measure.cpp` intrinsic path (RAD05). Correct per spec but duplicated rather than centralized in one constant, so a change must be made in several places.
4. **BFC line-break detection uses a hard-coded inline tag whitelist.** `layout_block.cpp:7562-7578` decides whether a block should suppress the parent line break by matching a long list of inline/inline-block tag ids (`HTM_TAG_SPAN`, `HTM_TAG_A`, …) *before* styles are resolved. This is fragile: a new inline-level element, or an unusual tag defaulting to inline via CSS, will be mis-classified. *Improvement:* defer the decision until after `dom_node_resolve_style`, or drive it from the UA stylesheet's default `display` rather than a tag list.
5. **`layout_block` is ~1600 lines mixing many concerns.** The single function (`layout_block.cpp:7486`) handles float detection, context save/restore, view tagging, style resolution, cache/measurement bailout, counters, list-items, and the deep inline-atomic float/wrap flow-into-parent logic (`layout_block.cpp:7849+`, `8600+`). It is hard to test in isolation and predates the RAII scopes it could use. *Improvement:* extract the flow-into-parent margin/collapse tail and the inline-atomic handling into named helpers.
6. **`LayoutOutput` return-struct abstraction is inconsistently threaded**: block layout writes `block->width`/`height` directly, so the measure-vs-layout separation is not carried by a uniform return type.

### RAD04 — Box Model, Containing Blocks & the Layout Cache

1. **`compute_element_last_baseline` is a stub.** `layout_alignment.cpp:237` has `// TODO: Implement proper last baseline calculation` and returns `-1`, so *last*-baseline alignment (`align-items: last baseline`) silently degrades to no-baseline for flex and grid items. *Improvement:* mirror the recursive first-baseline logic over the last in-flow child.
2. **Hard-coded float-width estimate.** `layout_block.cpp:4123` uses `float float_width = 100.0f; // Conservative estimate` instead of measuring the float, so a float's effect on available width is guessed. *Improvement:* run `layout_measure_intrinsic` for the float and use its max-content width.
3. **Hard-coded replaced defaults are duplicated.** The 300×150 (and 300×54 audio) intrinsic defaults live both in `layout_measure.cpp:105`+ and scattered through `layout_block.cpp` (SVG/iframe/hr paths). Centralizing them into one table would remove the drift risk.
4. **Cache invalidation is coarse and by-construction.** The `LayoutCache` is cleared wholesale on style re-resolution (`layout.cpp:816`) and nulled on pool swap (`view_pool.cpp:587`, `event.cpp:3845`), but there is no descendant-aware invalidation — a parent trusts that any changed descendant re-resolved its own style and thereby cleared its own cache. A mutation that changes layout inputs without triggering a child's style re-resolution could serve a stale measurement. *Improvement:* a dirty-propagation assertion tying cache validity to `layout_dirty`.
5. **`max_iterations` caps truncate silently.** The float-avoidance / find-y loops in the block driver and `block_context_find_y_for_width` cap at 100 iterations with no error logged when the cap is hit (see RAD03); a pathological float stack silently stops converging. Mentioned here because the sizing services (`compute_shrink_to_fit_width`, percentage resolution) feed those loops.
6. **`LayoutOutput` is under-threaded.** `layout.hpp` defines a clean width/height + baseline return struct, but block layout mostly writes `block->width`/`height` in place rather than returning `LayoutOutput`, so the abstraction is not consistently plumbed and the baseline fields are largely unused by the block path.
7. **`KnownDimensions` counts only positive givens as known.** `layout_known_dimensions_from_block` (`layout_pass.cpp:185`) treats a `given_width` of exactly 0 as *unknown*, so a legitimately zero-width element cannot key slot 0. This is usually benign but is an implicit contract worth documenting at the call sites.

### RAD05 — Intrinsic Sizing

1. **Dead `IntrinsicSizeCache` struct.** The header struct (`layout.hpp`), including its four-slot per-width `HeightCacheEntry` height cache, has zero references anywhere. The real cache is the four `DomElement` fields (`dom_element.hpp:434-437`) and caches **width only**. *Improvement:* delete the struct, or wire up its height cache to fix issue 2.
2. **Height is an admitted estimation, and never cached.** `calculate_max_content_height` does not run a real layout for elements (`intrinsic_sizing.cpp:5264-5265`, "for now, use a simplified estimation"), so nested flex/grid/table height can mismatch committed layout; and because there is no height cache, the expensive width-dependent walk repeats on every call.
3. **The monolith is ~3100 lines with heavy duplication.** `measure_element_intrinsic_widths` (`:1939-5060`) inlines table, grid, flex, block/inline, replaced, and pseudo logic. Box-extra/border/padding resolution is duplicated across the table block (`:3433-3465`), the generic tail (`:4900-4948`), the pseudo path (`:4751-4840`), and the grid early-return (`:3703-3714`); font setup is mirrored again in `calculate_max_content_height` (`:5290`). *Improvement:* extract a shared box-extras helper and a per-mode dispatch table.
4. **Grid intrinsic width is a fixed-track approximation with an early return.** The grid branch (`:3606-3744`) sums only fixed-length tracks and gaps, ignoring percentage/`fr` tracks, and early-returns past the generic constraint tail (duplicating the max-width clamp at `:3719`). Real track sizing is in RAD09; the two can disagree.
5. **Per-call timing and the `SLOW MEASURE` beacon signal a known hotspot.** Every element call runs a `std::chrono` timer and warns above 100ms (`:5053-5057`); text measurement loads a glyph per codepoint. The absence of height memoization (issue 2) makes repeated measurement passes (flex, grid multipass) quadratic-ish on deep subtrees.
6. **Debt is expressed as `CRITICAL FIX` / `fallback` / "for now", not `TODO`.** There are no literal TODO/FIXME/HACK tokens; instead accreted special-cases carry `CRITICAL FIX` comments (button UA styling `:1973`, monospace font-before-children `:1973`), form-control border-box flags, and anonymous-table repair — harder to grep and easy to overlook when refactoring.
7. **The re-entrancy guard returns `{0,0}` silently on a cycle.** Breaking a percentage-height ↔ aspect-ratio ↔ width cycle by returning zero (`:1951`) is safe against infinite recursion but can under-measure a legitimately complex box with no diagnostic beyond a `log_debug`.

### RAD06 — Inline & Text Layout

1. **Bidi and complex-script paint remain separate.** `layout_bidi.cpp` resolves visual order for established line fragments, using optional FriBidi or its local fallback, and publishes advance-based ranges. That geometry does not provide Arabic joining, Indic shaping or full bidi glyph substitution; the existing paint and fallback limitations still apply. This scoped refresh does not qualify the complete UAX #9 surface.
2. **No general complex-script shaping.** Only the ASCII-Latin fast path calls `font_measure_text` (`layout_text.cpp:976`); every other script is measured per-codepoint with no GSUB. Arabic joining, Indic reordering, and ligatures beyond the font's simple advance are not handled. The comment at `layout_text.cpp:884` notes the ZWJ-emoji handling is a heuristic "without HarfBuzz text shaping". *Improvement:* integrate a shaper (HarfBuzz) and persist a shaped buffer (see item 5).
3. **Emoji ZWJ sequences are heuristic.** `is_emoji_for_zwj` (`layout_text.cpp:877`) and `is_zwj_composition_base` use hardcoded codepoint ranges to zero post-ZWJ advances, rather than proper UAX #29 grapheme clustering; non-standard emoji sequences will mis-measure.
4. **`<br>` reported height is an approximation.** `layout_inline.cpp:1180` admits the rule "is not fully understood" and uses the font cell height, whereas Chrome's reported `<br>` height varies by context.
5. **Layout and render re-measure independently from byte ranges.** `TextRect` stores only `start_index`/`length`/`width`, so `render_text.cpp` re-decodes and re-loads every glyph. This duplicates work and risks layout/paint divergence. *Improvement:* a shared shaped-glyph buffer keyed by the rect's byte range.
6. **`white-space` is re-walked per text node.** `layout_text.cpp:2642` carries `todo: white-space should be put in BlockContext`; `get_white_space_value` climbs the ancestor chain on every text node. Related: `:2700` `todo: probably should still set it bounds` for fully-collapsed whitespace nodes.
7. **Hardcoded fallbacks.** tab-size default `8` (`layout_text.cpp:2983`), small-caps lowercase scale `0.7f` (multiple sites, `:3094`/`:3133`/`:3235`), and C1-control line-height synthesis ratios. These are correct for common cases but not configurable.
8. **Legacy `@font-face` cruft in the font bridge.** `font_face.cpp:341` `parse_font_face_rule_OLD` is a dead lexbor path (fonts are RAD07's domain, cross-linked here only because it is reached from the inline text path via `setup_font`).
9. **Infinite-`goto` guard.** `layout_text` caps its `goto LAYOUT_TEXT` re-entries at `layout_text_iterations > 500` (`layout_text.cpp:2733`) — an explicit admission that the goto-driven control flow can loop under degenerate layouts (extreme negative margins), aborting text layout rather than hanging.
10. **`layout_text` is a single ~1370-line function.** The overflow/wrap cascade and break-recording tail are long and deeply nested; correctness depends on many interacting `Linebox` flags. There is no unit-level test of individual branches — regressions surface only through the layout suite.

### RAD07 — Fonts

1. **Duplicated CSS→`FontStyleDesc` mapping.** The weight/slant mapping exists twice: inline in `setup_font` (`font.cpp:109-129`) and in `font_style_desc_from_prop` (`view.hpp:437`), and a third variant maps CssEnum→`FontWeight` in `register_font_face` (`font_face.cpp:438-447`). They can drift (e.g. `font.cpp` handles `bolder`/`lighter` keywords; `register_font_face` handles only `bold`/`normal` + numeric range). *Improvement:* have `setup_font` and `register_font_face` both route through one shared mapping helper.
2. **Legacy dead code in `font_face.cpp`.** `parse_font_face_rule_OLD` (`font_face.cpp:341-385`) is an entire lexbor-dependent function, including a hardcoded "Liberation Sans" registration and a `rule == NULL` "hardcoded @font-face" path, left inside a block comment. It is dead but clutters the file and encodes a stale design. *Improvement:* delete it.
3. **Platform coupling leaks into the bridge.** The macOS `"System Font"` reuse quirk (`font.cpp:64-73`, guarded `__APPLE__`) is a bridge-level compensation for how CoreText reports `system-ui`. Any second platform whose backend renames the resolved family would need the same treatment, ad hoc. *Improvement:* have the engine normalize the reported family for generic system families so the bridge's cache-match test can stay platform-agnostic.
4. **Cache-match test relies on exact float equality.** `font_handle_matches_prop` compares `handle_size == fprop->font_size` (`font.cpp:75`) with `==` on floats. This is currently sound because both come from the same resolution path, but it is a fragile invariant — any sub-pixel rounding difference silently forces a re-resolve.
5. **Hard-coded caps.** `@font-face` array initial capacity `10` (`font_face.cpp:399`); `max_cached_faces = 64` at context creation (`ui_context.cpp:161`). These are reasonable defaults but are not configurable from CSS or document scale.
6. **No variable-font or feature-control surface.** Radiant's `FontProp`/`FontStyleDesc` expose only family/size/weight/slant plus `font-kerning`. There is no bridge for `font-variation-settings` (variable-font axes) or `font-feature-settings` (arbitrary OpenType features) — only the specific `halt` adjustment (`font_get_halt_adjustment`) is surfaced. *Improvement:* extend `FontStyleDesc` and the bridge to carry axis/feature maps if variable-font support is desired.
7. **Fallback handling is fragile at the seam.** Radiant delegates per-codepoint fallback entirely to `font_load_glyph`, but special-cases only emoji (`font_load_glyph_emoji`); the fallback-metric blend for line height depends on `LoadedGlyph::font_normal_*` being populated correctly by whichever fallback face the engine picks — a dependency that is implicit and untested at the bridge level.

### RAD08 — Flexbox Layout

1. **Fixed-size global measurement cache.** `static MeasurementCacheEntry measurement_cache[1000]` with `static int cache_count` (`layout_flex_measurement.cpp:555`) is a process-global, capacity-capped array ("Fixed size for simplicity") — not per-document. Overflow logs an error and drops the entry (`:571`); a document with more than 1000 measured nodes silently loses caching, and there is a theoretical cross-document bleed risk mitigated only by the generation counter. *Improvement:* a per-`ViewTree` hash-map cache with real eviction.
2. **Hard-coded measurement fallbacks.** The default container width `366` in `measure_flex_child_content` (`:661`), the `line_init(..., 10000)` sentinel width (`:641`), the scattered `10000.0f` "unconstrained" widths (`layout_flex_multipass.cpp:2123` and elsewhere), and the **tag-based height estimates** (e.g. h4 = 20px at `:756`, and the "p=36px, h1=32px" heuristics noted at `layout_flex_multipass.cpp:2121`) are magic numbers that can mis-measure content in edge cases. *Improvement:* derive these from font metrics / actual available space.
3. **Correctness caps.** `MAX_ITERATIONS = 10` (`layout_flex.cpp:4194`), `MAX_FLEX_DEPTH = 16` (`layout.hpp`), and `MAX_MEASURE_DEPTH = 32` (`layout_flex_measurement.cpp:436`) are hard caps added for fuzzer-found blowups. They protect against pathological trees but silently truncate genuinely deep or genuinely slow-converging layouts rather than reporting a limit hit.
4. **`has_flex_item_prop` duplicated across all three files.** It is re-defined as a `static` in `layout_flex.cpp:39`, `layout_flex_measurement.cpp:438`, and `layout_flex_multipass.cpp:268`, and the measurement file additionally carries its own `flex_measurement_direction_is_row` (`:68`) parallel to `is_main_axis_horizontal`. Divergence between copies is a live risk. *Improvement:* hoist the shared predicates into `layout.hpp`.
5. **Incomplete last-baseline alignment.** `layout_alignment.cpp:242` carries `// TODO: Implement proper last baseline calculation`; `align-items:last baseline` is not fully spec-correct.
6. **space-evenly workaround.** `space-evenly` is routed through a custom `x-justify-content` property in style resolution rather than a first-class CSS value (`layout_flex_multipass.cpp:831` note). This is a resolution-layer patch that should be normalized.
7. **Heavy per-phase logging in the hot path.** Every phase and many per-item steps emit `log_info`/`log_debug` (e.g. the "Phase N" and "ITERATIVE_FLEX" lines throughout `layout_flex.cpp`). On large trees this is a real log-volume and throughput concern; the logging is not compiled out in release. *Improvement:* gate the per-item lines behind a debug-only channel.
8. **Monolithic functions mixing spec logic with fixups.** `layout_flex_container` (~1400 lines) and `layout_flex_container_with_nested_content` (~375 lines) interleave the §9 algorithm with the 4b/5b/5c/7b/7c/8b/9a/9.5 fixup branches, making the spec logic hard to test in isolation.

### RAD09 — CSS Grid Layout

1. **Broad flow, ordering and logical-axis qualification remains open.** The focused dense/sparse and both-flow capacity controls do not establish every placement combination.
2. **Track/area CSSOM and grammar remain partial.** Shared grid-definition and decoded-area grammar now supply parse-time/substituted-value validation, component cascade, owned projection and CSSOM mutation. Calculated repeat counts have owner-font, inheritance, variable/defaulting, specified/computed and mutation controls. Pseudo-element reads and broader owner-unit/track grammar remain open.
3. **Pass-3 row-height reconciliation remains heuristic.** It repairs underestimated intrinsic heights from laid-out content and applies only to non-fixed tracks.
4. **Placement counts still have a preservation guard.** Post-placement item endpoints cannot reconstruct the entire negative-implicit extent, so recomputation must preserve placement-established counts.
5. **Independent paged grid remains unqualified.** Browsing and PNG/SVG/PDF controls do not establish fragmented-grid behavior.

Focused evidence: `css_grid_auto_fit`, `css_grid_named_lines`, `css_grid_capacity`, `css_grid_extent_policy`, `GridStorage.*`, retained-clone retirement and `RenderOutputParity.Grid*` cases. The active [CSS support plan](../impl/Radiant_Impl_CSS_Support.md) records dated qualification and remaining P0–P2 work.

### RAD10 — Table Layout

1. **The 501k single-file mega-function.** `layout_table.cpp` is ~9945 LOC and dominated by `table_auto_layout` (`layout_table.cpp:6142`), a single ~2900-line function whose phases are delimited only by "Step N" comments and share a large flat set of locals. This is the strongest extraction candidate in Radiant: caption handling, structure analysis (already partly factored), border-collapse resolution, auto width distribution, fixed distribution, and cell positioning could each become their own function/file. Cognitive load and merge-conflict surface are both high. *Improvement:* split each Step into a named function taking the `TableMetadata` explicitly.
2. **Debt is structural, not marked.** A grep for `TODO`/`FIXME`/`HACK`/`XXX` across `layout_table.cpp`, `layout_table_metadata.cpp`, and `layout_table_caption.cpp` finds **none** — the tech-debt here is size and coupling, not tagged intent, so it will not surface in a marker sweep. The implementation comments themselves flag "simplified" building (`build_table_tree` at `layout_table.cpp:4463`) and a "Phase 3 optimization" without a corresponding removal plan.
3. **Caption logic is duplicated across placement and inline handling.** Caption sizing/placement appears both in the dedicated `layout_table_caption.cpp` helpers (declared in `layout.hpp`) and inline within `table_auto_layout` (top at `layout_table.cpp:7964`/`8021`, bottom at `9175`), plus the caption-only fast path at `6203`. Keeping these in sync is manual.
4. **Wide parallel-array `TableMetadata`.** The grid carries ~15 parallel arrays (`layout.hpp`) indexed by bare column/row integers; a bug that indexes one array with the wrong axis or an off-by-one against the `cols+1` edge array (`col_edge_max_border`) is silent. *Improvement:* a small accessor layer or a per-column struct-of-arrays would make misuse harder.

### RAD11 — Positioned, Floats, Multi-column, Lists & Counters

1. **Multicol is explicitly simplified.** The header comment at `layout_multicol.cpp:2292` states it uses block-level distribution and does not fully fragment within block elements. Spanner top-margin collapsing is `max(prev_margin_bottom, spanner_margin_top)` and marked "simplified" in-code (`:2716`). *Improvement:* a real fragmentation cursor that can break inside a block, not only project inline lines after the fact.
2. **Broader fixed-position contexts remain partial.** Initial viewport and transformed/contained ancestor selection have bounded sizing coverage, including percentage math and retained changes. General fixed positioning across scrolling, independent views and fragmented contexts is not fully qualified by those controls.
3. **Fragile float coordinate math guarded by `#ifndef NDEBUG`.** `layout_float_element` keeps several debug-only coordinate variables (`layout_positioned.cpp:2162`+, and `cb_x`/`cb_y` in `calculate_absolute_position` at `:704`), indicating the BFC↔local coordinate conversion has been bug-prone. The 100-iteration cap (`:2268`) and the `0.001f` fit epsilon (`:2337`) are defensive constants, not principled bounds.
4. **`layout_abs_block` is a 700-line mega-function with heavy re-correction.** The abspos path in `layout_positioned.cpp:1242`+ re-solves margins, x, and y multiple times (after image load, after shrink-to-fit, after auto height) with many mode guards (flex/grid/form/replaced). Correctness depends on these fix-ups running in exactly the right order; there is no assertion tying the final geometry to the constraint equation.
5. **Image-load failure uses a placeholder box.** Failed abspos `<img>` loads fall back to a hard-coded 40×30 placeholder (`layout_positioned.cpp:1464`); marker image-load failure silently falls through to the numeric/bullet render (`render_list.cpp:70`).
6. **Very few explicit TODO/FIXME markers.** The structural debt in this area shows up as `simplified`/`placeholder`/"Phase N" narration rather than tagged TODOs, so grep-based debt tracking under-counts it (notably the mega-functions and multicol).

### RAD12 — Paint IR, Display List & Backend Abstraction

1. **Eager per-command lowering yields no batching benefit.** The gateway records one `PaintCmd`, lowers it, then clears the `PaintList` on every op (`render.hpp`, and every `paint_record_*` inline). PaintIR is a "thin layer above DisplayList" (`render.hpp`) that today adds a copy-and-switch per op with no cross-op optimisation. The overhead is unmeasured. *Improvement:* record whole subtrees into the `PaintList` and lower in bulk, enabling coalescing/culling before raster.
2. **Triple-mirrored op and payload definitions.** `PaintOp` ↔ `DisplayOp` ↔ the `rdt_*` calls, and `Paint*` ↔ `Dl*` payload structs, mirror each other by hand (`render.hpp` vs `render.hpp`). Adding one primitive means edits in ~3 struct sites plus the lowering switch (`paint_ir.cpp:944`) and the replay switch (`display_list_replay.cpp:85`) — roughly 3 edit sites per primitive before it renders. *Improvement:* generate the mirrored trio from one op table.
3. **~18 fragmented `display_list_*` files.** Recording, replay, storage, bounds, and the per-effect replay families are split across many small files (`display_list_replay_state.cpp` ~60 lines, `..._raster.cpp` ~50, `..._shadow.cpp` ~74, several 5–24-line headers). The split is by effect family, not by a clear layering rule, so navigation cost is high. *Improvement:* consolidate the tiny replay/record units by pipeline stage.
4. **Semantic ops are not fully lowered.** Raster lowering ignores `PAINT_GLYPH_RUN` semantic runs and richer effects (`render.hpp`); SVG handles only text runs and opacity-only groups; `PAINT_SVG_SUBSCENE` is a Phase-F stub (`render.hpp`, `330-331`). Most effect groups round-trip through raster fallback rather than native vector constructs, so the "one paint algorithm for all backends" promise is only partly realised.
5. **Tiling disabled whenever glyphs are present.** `render_output_replay_display_list` forces the serial path if `dl_contains_glyphs` is true (`render_output.cpp:308-309`). Because most real pages have text, the parallel tiled path rarely engages even though tile-local glyph rendering already exists (`tile_pool.cpp:385`). *Improvement:* let glyph ops render tile-locally in the parallel path and drop the page-level gate.
6. **No `TODO/FIXME/HACK/XXX` markers, but the debt is structural.** The core files grep clean of inline debt markers; the migration is incremental-by-design (Phase C/D/F), so the incompleteness above is not flagged in-line and is easy to mistake for finished work.

### RAD13 — Render Walk, Painters & Export

1. **CSS stacking contexts are still partial.** Signed positioned block layers belong to their nearest context, including `isolation:isolate`, with shared paint/hit order. Zero-level descendant lifting, floats, negative inline spans, blend-group isolation and complete flex/grid context rules still need the full CSS stacking-context algorithm.
2. **PDF success paths emit `log_error` noise.** Multiple non-error PDF outcomes log at error level — e.g. "fallback effect group rendered as passthrough content" (`render_pdf.cpp:1033`) and the raster-fallback effect-group notice (`:598`) fire on the *success* path where a fallback is the intended behavior. This makes real PDF errors hard to spot. *Improvement:* demote intended fallbacks to `log_debug`.
3. **Duplicated body-background propagation.** `render_output_canvas_background` (`render_output.cpp:205`) and the iframe branch of `render_embed_doc` (`render.cpp:122-156`) reimplement the same `<html>`-then-`<body>` background walk. *Improvement:* extract one shared helper.
4. **File sprawl (~50 `render_*` files, ~26.8k LOC).** Painter logic, low-level pixel ops, and IO decode are interleaved; the largest are `render_svg_inline.cpp` (5633), `render_pdf.cpp` (2384), `render_background.cpp` (2107), `render_svg.cpp` (1978). No TODO/FIXME/HACK markers exist — the debt is structural (sprawl + duplication), not annotated.
5. **The raster path is only partway onto the shared abstraction.** The header comments admit it: `render.hpp` describes richer raster block state being "migrated in staged slices," and `render.hpp:15-37` labels the PaintIR routing "Phase C." The raster backend deliberately leaves `render_bound`/effect/transform callbacks null and does that work inside `RenderContext` instead of the generic walker, so the two paths are not yet fully unified.
6. **PDF/SVG re-do layout headlessly.** `render_html_to_pdf` (`render_pdf.cpp:2262`) reloads and re-lays-out the HTML rather than reusing an existing view tree, and cannot be driven from the in-memory `render_output_render_view_tree_to_target` (`render_output.cpp:497`). Two parallel entry conventions (`render_html_to_*` vs `render_output_*`) coexist. *Improvement:* allow vector export off a pre-laid view tree.
7. **Effect-group need vs. capability is decided in two places.** The walker's `render_walk_block_effect_group` (`render_walk.cpp:67`) decides an effect group is *needed*; the backend caps decide it is *supported*. A backend that opens a group it cannot honor relies on flatten/fallback correctness elsewhere — there is no single assertion tying the two together.

### RAD14 — SVG, Vector Graphics & Diagram Layout

1. **`render_svg_inline.cpp` is a 5633-line monolith.** It mixes SVG parsing, CSS style resolution, path/arc geometry, gradients/patterns, filters, clips/masks, and text glyph rendering in one file. *Improvement:* split along the natural seams — path parsing, filters/masks, gradients/patterns, and text — into separate TUs sharing `SvgInlineRenderContext`.
2. **Graph layout is Dagre-inspired, not a faithful port** (`lmd/package/graph/dagre.ls`). It still lacks network-simplex ranking, dummy nodes, Brandes-Kopf x-assignment, clusters, parallel-edge separation, and self-loop routing. *Improvement:* normalize long edges through dummy ranks before repeated crossing-reduction sweeps.
3. **Backend caps parity gaps** (`rdt_vector_tvg.cpp:774` vs `rdt_vector_cg.mm:273`). Both leave `opacity_group`, `blend_modes`, `color_matrix_filters`, and `native_text_runs` = false, so those effects silently no-op or fall back to raster. The ThorVG native `gaussian_blur` cap is `__APPLE__`-only (`rdt_vector_tvg.cpp:787-791`) — the CSS-filter blur path degrades on Linux/Windows ThorVG builds (but inline-SVG `<feGaussianBlur>` does not).
4. **Fixed clip-stack depth in the ThorVG backend.** `RDT_MAX_CLIP_DEPTH` is hard-coded to 8 (`rdt_vector_tvg.cpp:1461`); overflow is logged and the clip dropped (`:1476`). Deeply nested SVG clip paths beyond depth 8 silently lose clipping.
5. **Two directions named "SVG" are easy to confuse.** `render_svg_inline.cpp` is SVG *input* (element tree → pixels); `render_svg.cpp` is SVG *output* (view tree → SVG text). They share only the `PaintSvgSubscene` builder. *Improvement:* rename `render_svg.cpp` to something like `render_svg_output.cpp` to make the direction unmistakable.
6. **Sparse explicit debt markers.** The only "unsupported" log in the inline renderer is the SVG path parser's unknown-command branch (`render_svg_inline.cpp:2263`); `rdt_picture_load` logs unsupported formats rather than falling back. Most debt here is structural (file size, algorithm scope) rather than tagged with TODO/FIXME.
7. **Layer cache has no eviction.** `svg_layer_registry` keeps a captured layer until the document is torn down or the same element changes; an `<svg>` removed from the tree keeps its raster (bounded by the 256 MB per-document cap). A rotated, skewed, flipped or non-uniformly scaled `<svg>` never caches. *Improvement:* evict entries not painted for N frames; draw transformed layers through `DL_DRAW_IMAGE` with a premultiplied upload.
8. **Manual lifetime bookkeeping around `RdtPicture`.** The path-keyed picture cache plus its mutex (`rdt_vector_tvg.cpp:101,286`) pair with hand-managed `Pool`/`Element` ownership on `KIND_SVG_DOM` pictures — a place to watch for leaks or races under concurrent load.

### RAD15 — Events & Input Dispatch

1. *(Resolved.)* The duplicated layer-mode webview block is gone; `target_block_view` has one `WEBVIEW_MODE_LAYER` inject block.
2. **Zero-level stacking remains partial.** Signed positioned block descendants use their enclosing real stacking context and replay intervening ancestor clips. Absolute/fixed layers translate into the containing-block coordinate frame before hit testing, including their own border box. Zero-level descendant lifting and interleaving absolute/fixed children with in-flow step-8 boxes by tree order remain incomplete (RAD13). Ordinary block background/foreground and float phases share paint and reverse hit order. View-backed DOM `elementFromPoint` first delegates to the native engine target picker, with shadow and geometry fallbacks in `dom_document_element_from_point_native` for cases without an engine hit.
3. **Legacy contenteditable key path has unfinished stubs.** `event.cpp:9303/9311/9316/9488/9492` carry `TODO: delete selected text` / `TODO: insert character at caret` in a legacy key handler superseded by the editing-controller/transaction path (comment at `event.cpp:9411`). The dead path should be removed so the transaction path is the only key→edit route.
4. **`handle_event` monolith.** `event.cpp` is ~9,554 lines / 440KB and `handle_event` is a ~2,900-line switch; `event_sim.cpp` is ~5,902 lines. High cognitive load. Splitting per-`EventType` handlers into separate translation units would help without changing the single-funnel contract.
5. **`event_sim_print_results` uses `fprintf(stderr,...)`.** Scoped to the test harness, but a deviation from the project no-`fprintf` guidance.

### RAD16 — Animation & Frame Scheduling

1. **Transition support requires checking the supported property subset.** Rechecked
   2026-10-06: `css_transition_capture_before_change`,
   `css_transition_resolve_config` and `css_transition_resolve` now capture changes,
   read transition longhands and create scheduler instances. The earlier claim
   that transitions were unwired is obsolete. This does not establish support
   for every CSS property or PowerPoint effect; consult the animation value
   parser and the used-value consumer for the requested property.
2. **Hard parser caps silently truncate.** `parse_keyframes_content` caps at 64 stops and 32 declarations per stop (`css_animation.cpp:343`/`347`), with 64-byte name and 256-byte value buffers (`css_animation.cpp:413`/`434`). Overflow is dropped without a warning. *Improvement:* at least `log_warn` on overflow, or grow dynamically.
3. **`property_name_to_id` is a fixed strcmp table.** Only ~28 properties are animatable (`css_animation.cpp:122`); anything else in a keyframe is silently ignored. New animatable properties require editing three coupled tables (`property_name_to_id`, `property_value_type`, `apply_animated_value`).
4. **Heavy platform `#ifdef` in `frame_clock.cpp`.** Every function branches on `__APPLE__`/`_WIN32`/`__linux__` (`frame_clock.cpp:27`, `226`, `316`, `384`) rather than dispatching through a small vtable; each new clock operation must re-branch in several places. *Improvement:* a `RadiantFrameClockOps` function-pointer table per platform.
5. **`radiant_frame_clock_start` has dead trailing statements.** The `#else` fallthrough stores `running = 0` and returns `false`, but the two statements after the `#endif` (`frame_clock.cpp:369`-`370`) are unreachable on every platform — harmless but confusing.
6. **Registry lookup and dedup are linear.** `keyframe_registry_find` (`css_animation.cpp:561`) and the "already running?" scan in `css_animation_resolve` (`css_animation.cpp:1022`) are O(n) over all keyframes / all active animations, run per element during layout. Fine at current scale, a hazard for animation-heavy pages.
7. **Transform-dirty bounds are approximate.** `css_animation_tick` inflates bounds by a generous `max(w,h)` margin for scale/rotate (`css_animation.cpp:859`) rather than computing the true transformed AABB, over-painting to stay correct. Acceptable, but wasteful for large elements.

### RAD17 — Interaction State: DocState, State Machine & Schema

1. **`state_store.cpp` remains a large monolith** spanning interned names, dump, lifecycle, generic state, constraints, dropdowns, immutable mode, batching, dirty/reflow, caret/selection presentation, focus, and clipboard. It is the prime split candidate in this subsystem — a natural decomposition follows the function-prefix families (`doc_state_*`, `form_*`, `view_state_*`, `selection_*`, `caret_*`, `state_dump_*`).
2. **Immutable / COW mode is an unimplemented stub.** `state_set_immutable` shallow-copies and re-hashes rather than structurally sharing — `// TODO: implement proper HAMT for structural sharing` (`state_store.cpp:5142`). Related dangling TODOs: `state_end_batch` never fires deferred callbacks (`// TODO: trigger deferred callbacks`, `:5284`), and the dump element builder has a `// TODO: add attributes if needed` (`:8338`). Undo/time-travel cannot rely on this mode today.
3. **`s_in_batch` is a file-static global**, not a `DocState` field. It gates whether mutation-time asserts fire, so it is not per-document and is unsafe if multiple documents or threads mutate state concurrently. *Improvement:* move it into `DocState` alongside `active_cascade_depth`/`transition_depth`.
4. **Selectionchange coalescing spans document and text-control drains.** `selection_mutation_seq`/`selection_event_seq` plus the per-text-control `tc_selectionchange_head` linked-list drain form a subtle ordering surface with no single owner.
5. **Cached `View*`/`DomElement*` pointers require prune-after-reflow.** Numerous `DocState` fields (`hover_target`, `active_target`, `cursor->view`, drag/drop views, editing surfaces) hold raw pointers that go stale on relayout and depend on `state_store_prune_after_reflow` being called at the right time.

### RAD18 — Editing, Selection & DOM Ranges

1. **`dom_range.cpp` is a 4551-line monolith.** It mixes lifecycle, boundary comparison, mutation envelopes, extract/clone/surround, selectAll/triple-click, `Selection.modify`, and stringification. It is the single largest file in the area and the prime split candidate — a natural fission is (a) range/selection core + comparison, (b) mutation envelopes + mutators, (c) modify/selectAll/word-breaking, (d) stringification.
2. **Text-control target range is a StaticRange hack.** `compute_text_control_target_ranges` synthesizes a boundary over the control *element* carrying UTF-16 `selectionStart`/`End`, until form values can be promoted to concrete DOM text nodes. The document-vs-text-control distinction remains intentionally unified by the `EditingSelection` facade.
3. **Incomplete OS clipboard backends.** macOS uses NSPasteboard for text, HTML, images and single image-file copies. Win32/X11 rich-MIME adapters remain unimplemented; the fallback GLFW backend is plain-text-only, and `clipboard_store_sanitize` is a near-no-op.
4. **Pattern-regex validation TODO (F5).** Form constraint validation `te_validate` cannot enforce `pattern="..."` without a lazy-compiled regex. (Detail belongs to RAD19, noted here because it is a spec-coverage gap in the editing surface.)
5. **`Selection.modify` fidelity.** All accepted argument families are implemented, including bidi-aware visual left/right character and word movement. Word segmentation remains a simple alphanumeric classifier, while sentence and some layout-free line fallbacks are structural approximations; locale-sensitive and complex-script results can diverge from a browser. This is a bounded Selection-API conformance limit, not a general contenteditable-support gap.
6. **Single-range selection.** `DOM_SELECTION_MAX_RANGES == 1`; extra `addRange()` calls are silently ignored, as required by the Selection API. This is not a conformance gap.
7. **Editor matrix boundary.** CodeMirror, ProseMirror light DOM, and Editor.js have pinned offline authoritative-state probes. The capability manifest is the source of truth for their supported configurations and explicit exclusions, including Editor.js ordinary paragraph paste, which upstream delegates to browser-native structural editing and this gate intentionally does not restore. An editor render or one typing probe is not treated as blanket compatibility.

### RAD19 — Form Controls & Text-Control Editing

1. **Text-control target range is a StaticRange hack.** `editing_compute_target_ranges` builds a synthetic boundary over the control element with UTF-16 offsets instead of real DOM text nodes (`event.hpp`). It stays until form values become concrete DOM text — the same "E0 promotion" blocker referenced in RAD18. *Improvement:* promote `<input>`/`<textarea>` values to a shadow text node so DOM Range and form offsets unify.
2. **`TODO(F5)`: `pattern="…"` validation is unimplemented.** `te_validate` (`text_edit.cpp:641`) checks number/email/url but has an explicit TODO for `pattern` because it "needs lazy-compiled regex". Until then a `pattern` attribute is silently ignored — a control with only a `pattern` constraint always reports `:valid`.
3. **Projection-cache consumers remain.** Canonical `EditingSelection` (StateStore), the HTML-facing `form->selection_start/end` mirror, and caret/anchor projection cache are reconciled by hand in `tc_sync_selection_to_form` (`text_control.cpp:404`). The remaining cleanup is to port projection consumers directly to `EditingSelection` / DomRange geometry.
4. **IME edge cases.** `te_ime_commit_prepare` (`text_edit.cpp:822`) accepts a composition *session* on readonly/disabled controls but rejects the commit, and orphaned compositions are silently dropped in `te_ime_begin` (`text_edit.cpp:775`). There is no reconversion / candidate-window geometry reported back to the OS, and the preedit caret is a single codepoint offset with no segment/clause underlining.
5. **Paste sanitization is minimal.** `te_prepare_paste_replacement` handles newline normalization and `maxlength` truncation only (`text_edit.cpp:649`); it does not strip control characters, apply `inputmode`/type-specific filtering (e.g. non-numeric into `type=number`), or coordinate with the rich clipboard MIME store.
6. **Constraint validation is a v1 subset.** Beyond the missing `pattern` (item 2), `te_validate` (`text_edit.cpp:602`) does not implement `min`/`max`/`step` for number/range, `minlength`, or the full email/url grammar — the `te_value_is_*` helpers are approximate.
7. **Chrome-matched magic numbers.** `FormDefaults` (`view.hpp`) hard-codes dozens of pixel constants (153×21 text input, 57px select, per-type date/time widths in `layout_form.cpp:24-`) reverse-engineered from Chrome. These drift as Chrome's UA stylesheet changes and are unverified across platforms/DPI beyond the tuned cases.
8. **Legacy dual dispatch surface.** `te_replace_byte_range` vs `te_replace_byte_range_no_events`, and `te_ime_commit` vs the `_prepare`/`_finish` split, exist purely to straddle "legacy caller without EventContext" and "unified dispatcher". The legacy variants (`text_edit.cpp:278`, `806`) are latent duplication once all callers own an `EventContext`.

### RAD20 — Application Shell, Browsing & Document Loading

1. **Networking is borrowed, not owned, by the session — a lifetime coupling.** `BrowsingSession` holds `thread_pool`/`file_cache` pointers it explicitly does not own (`browsing_session.cpp:98`), and per-navigation `radiant_cleanup_network_support(old_doc)` is called from `session_navigate`/`go_back`/`go_forward` (`browsing_session.cpp:156`, `207`, `254`). The window creates network support only for the *initial* document if it is HTTP (`window.cpp:1100`); a later same-window navigation to an HTTP page relies on `show_html_doc`→`load_html_doc` re-initializing it, so ownership of network lifetime is split across three files. *Improvement:* give the session an explicit owned network context, or document the ownership contract in one place.
2. **History cap is a hard-coded 100 with silent eviction.** `BROWSE_HISTORY_MAX` (`radiant.hpp`) drops the oldest entry by `memmove` (`browsing_session.cpp:131`) with no user signal. `history_ensure_capacity` also silently returns on `mem_realloc` failure (`browsing_session.cpp:39`), after which the push can fail and `session_navigate` returns `NULL` (`browsing_session.cpp:141`). *Improvement:* make the cap configurable and surface allocation failure distinctly from a load failure.
3. **Windowless-by-default is a platform workaround, not a clean abstraction.** The headless branch avoids creating a window because GLFW's hidden-window path can deadlock in macOS LaunchServices (`ui_context.cpp:76-108`), reachable only via `LAMBDA_HEADLESS_GLFW_WINDOW`. Downstream code must therefore tolerate `uicon->window == NULL` everywhere (e.g. `update_window_title` early-returns, `ui_context_cleanup` guards `glfwTerminate`). A missed guard crashes only on the windowless path. *Improvement:* a null-object window wrapper would remove the scattered `if (window)` checks.
4. **Single global `ui_context` blocks multi-document/tabbed browsing.** GLFW callbacks reference the global directly, so there is no per-window state; the shell cannot host more than one document at a time.
5. **Dispatch responsibility is split and the old doc was stale.** Extension dispatch is in `load_html_doc_no_redirect` (`cmd_layout.cpp:3522`) while `load_html_doc` (`cmd_layout.cpp:3597`) only handles redirects — the former `Radiant_View_Design.md` claimed dispatch happened in `load_html_doc()`. `detect_doc_format` in `window.cpp` keeps a second extension list, now used only to name the format and to refuse unsupported extensions; every supported format loads through `load_html_doc`. *Improvement:* one extension→loader table shared by both.
6. **`render()` does network draining and title management inline.** `render` (`window.cpp:685`) both flushes network completions and owns the loading-progress/partial-load window title (`window.cpp:704-757`), overlapping with the main loop's own `resource_manager_flush_layout_updates`. The double-flush is harmless but the responsibility split (loop vs. render) makes the load-completion path hard to follow.
7. **Loader coverage gaps.** Top-level SVG and images are viewers with no zoom/pan yet (the wrapper CSS pins `img.rdt-image-document` to `max-width: none`, `cmd_layout.cpp:3714`); XML without an `<?xml-stylesheet?>` silently degrades to source text; wiki runs neither scripts nor a math pass. These are intentional current limits, not bugs, but they are incomplete relative to HTML.

### RAD21 — JS Scripting Integration

1. **No watchdog on Windows.** The entire crash/timeout guard is `#ifndef _WIN32` (`script_runner.cpp:64-134`, `1887-1982`). On Windows a hanging or crashing page script takes the process down with no recovery — there is no SEH/`__try` equivalent. *Improvement:* a SEH-based guard or a worker-thread execution model with a timeout join.
2. **The signal watchdog is coarse and leaves the JS heap inconsistent.** Recovery via `siglongjmp` (`script_runner.cpp:1913`) skips C++ destructors; the code compensates with `jm_cleanup_active_mir`/`jm_abandon_active_mir_after_signal` and the sticky `js_batch_cleanup_unsafe` flag (`:75`, `:2086`), but a caught SIGSEGV means the runtime is only safe to *tear down*, not continue. Signal handlers touch process-global state (`js_exec_old_*`), so this is not re-entrant across concurrent documents.
3. **Preamble stubs are non-functional shims.** `XMLHttpRequest`, `WebSocket`, and `Worker` are empty/no-op constructors (`script_runner.cpp:1082-1087`) — pages that depend on XHR/fetch-over-XHR, sockets, or workers silently get nothing. `matchMedia` always returns `matches:false` (`:1092`), and the observers never fire. This is enough to *parse and initialize* many real pages but not to drive dynamic data-loading. (Real `fetch`/XHR host objects do exist in `lambda/js/`, per JS13; the preamble deliberately shadows them with stubs at the window level for the static-render path.)
4. **Unsupported script types are skipped.** JavaScript modules execute through the module loader; unsupported `type` values retain `SKIPPED_UNSUPPORTED_TYPE`. Module source-fetch failures are script-element resource errors, distinct from exceptions in parsed/executed modules.
5. **Large-bundle budget silently drops JS.** `script_total_compile_limit_bytes()` and the external-size gates skip document JS beyond a byte budget and render the static DOM/CSS — correct as a fallback, but a page can render "wrong" with no user-visible error. The `s_execute_external_scripts` toggle similarly hides external-script effects in smoke renders.
6. **Inline-handler rewrite is a hand-rolled mini-parser.** `append_global_call_inline_handler` recognizes only the `name(...)` / `return name(...)` shape via ad-hoc identifier scanning; anything else falls back to inlining the raw attribute string, which then depends on the full transpiler. `func_name` is a fixed `char[64]` and the depth guard is a hard `depth > 100`.
7. **Webview IPC from JS is not wired.** `<webview>` message dispatch to the runtime is still TODO in the platform layers (RAD22); scripts cannot yet receive or drive embedded-webview messages.

### RAD22 — Media & Embedded Webview

1. **Webview IPC to the Lambda runtime is a TODO on every platform.** `window.__lambda.invoke()` messages are received and logged but never dispatched: `webview_child_mac.mm:160`, `webview_child_linux.cpp:97`, and `webview_layer_linux.cpp:113` all carry `// TODO: ... dispatch to Lambda runtime`. The macOS layer handler (`webview_layer_mac.mm:70-76`) only sets `dirty` and does not even log the payload. The entire injected bridge (`_callbacks`, `on()`) is inert. *Improvement:* wire the JSON `{command, payload}` into the RAD21 runtime.
2. **No Linux or Windows video.** `rdt_video_stub.cpp:3-4` documents that FFmpeg and Media Foundation backends are "not yet implemented, using stubs"; every getter returns 0/false and playback never leaves `RDT_VIDEO_STATE_IDLE`. Video works on macOS only; elsewhere the page shows a permanent play button.
3. **Video source is local files only.** `rdt_video_open_file` is the sole source entry (`rdt_video.h:55-58`); web URLs are explicitly deferred. A `<video src="https://...">` will not load even on macOS.
4. **Layer-mode snapshot is forced synchronous by spinning the run loop.** `webview_layer_mac.mm:379-385` and `webview_layer_linux.cpp:453` block up to 500 ms pumping the platform event loop to turn an async snapshot into a synchronous call for the render pipeline. This is fragile (nested run-loop reentrancy, and a hard 500 ms stall on a slow snapshot) and is the main reason layer mode is still "Stage 2".
5. **AVFoundation frame path is per-frame `AVAssetImageGenerator`, not a video output.** The workaround for error -11821 (`rdt_video_avf.mm:265-271`) copies a `CGImage` every frame via `CGBitmapContextCreate` — noticeably less efficient than a zero-copy `AVPlayerItemVideoOutput`/`IOSurface` path.
6. **Poster image is not plumbed.** The idle/loading branch of `render_video_frames` (`render_video.cpp:452-457`) comments that a poster "would need to be passed through" and falls back to the play button; `poster` attribute support is missing.
7. **GIF finish leaves a dangling `surface->pixels` briefly.** `gif_animation_finish` frees the frames, after which `surface->pixels` points at freed memory until it is nulled two lines later (`gif_player.cpp:55-64`) — a narrow window, but any interleaved read during teardown would be a use-after-free.
8. **No macOS child-mode compositing / z-order.** Child-window webviews are always-on-top overlays with no relationship to document z-order; only layer mode composites correctly. Mixing a child webview under other content is impossible.
9. **`WebViewProp.src`/`srcdoc` are borrowed pointers.** They are documented as borrowed DOM-attribute storage in `radiant.hpp`; their lifetime is tied to the DOM node and is not retained through a `view_pool_reset_retained` (RAD01) — a relayout that frees attribute storage while a webview holds `src` would dangle. No `PersistentFieldRef` guards them.

## Resolved issues

<a id="rad07-l2"></a>

### RAD07-L2 — LaTeX document transform omits bundled font declarations — RESOLVED

**Found, fixed and verified:** 2026-10-08, on macOS in the working tree.

**Symptom before the fix.** Rendering `test/input/enhanced_test.tex` selected Times Italic for
math variables, STIX Two Math for symbols/digits, Georgia for prose, and Times
Roman for the `KaTeX_Size1` fallback. The bundled fonts exist and load correctly
when explicitly declared; this is separate from the fixed raster coverage
defect [RAD07-L1](#rad07-l1).

**Root cause and regression origin.** Commit `7fe950bf3` (2026-09-21,
“made LaTeX and Graphs loading config driven”) replaced the dedicated LaTeX
loader with the shared document-transform loader and removed its explicit CMU
and KaTeX font-stylesheet loading. The replacement
[`wrap_standalone`](../../lmd/package/doc/latex/latex.ls) embedded layout/math CSS but
no font declarations or stylesheet links. The shared loader in
[`load_lambda_document_doc`](../../radiant/cmd_layout.cpp) collects the
generated HTML's CSS without adding those package assets.

[`local_font_families`](../../lmd/package/math/css.ls) named Computer
Modern and KaTeX families, but naming a family does not register the bundled
font files. macOS's default discovery in
[`font_platform.c`](../../lib/font/font_platform.c) searches system/user font
directories rather than the package directories. The missing declarations
therefore cause system fallback. The omission is in shared code; the fallback
faces depend on the platform and installed fonts.

**Verification.** A 64px HTML probe with identical content and font-family
rules was laid out twice. Adding only links to
[`cmu-combined.css`](../../lmd/package/doc/latex/fonts/cmu-combined.css) and
[`katex.css`](../../lmd/package/math/katex.css) changed the actual loaded faces:

| Probe | Without bundled CSS | With bundled CSS |
|---|---|---|
| Italic math variables | `Times-Italic` | `CMUSerif-Italic` from `Serif/cmunti.woff2` |
| Large integral | `Times-Roman` | `KaTeX_Size1-Regular` from `fonts/KaTeX_Size1-Regular.woff2` |

Both layouts succeeded. Before the fix, the native `.tex` layout reproduced
the system fallback and HTML conversion emitted zero font declarations or
stylesheet links. Local reproduction files and font-loading logs are in
[`temp/math-font-selection/`](../../temp/math-font-selection/).

**Fix.** The shared
[`font_stylesheets`](../../lmd/package/math/css.ls) helper emits links to both
bundled stylesheets, resolving the installation through `sys.lambda.home`
and the existing path helper, per **D9.3.4**. LaTeX, standalone math, TikZ,
the document viewer and the edit shell use it. The stylesheets retain their
own source URLs, so their relative WOFF2 URLs resolve beside the CSS assets.
Fragment renderers continue to leave stylesheet ownership to their host.

**Fix verification.** Native `.tex` layout and layout of `convert
--full-document` HTML now load `CMUSerif-Italic` and `KaTeX_Size1-Regular`
from the bundled files. Chrome 154's platform-font inspection of the exported
HTML confirms both as custom fonts and CMU Serif Roman for prose. The new
[`test_latex_bundled_fonts.ls`](../../test/lambda/latex/test_latex_bundled_fonts.ls)
regression checks native document output, serialized/reparsed standalone HTML,
standalone math, absolute/readable font paths, and fragment behavior; it fails
before the fix. The initial focused LaTeX/math/edit run passed 86/86; the final
isolated script run passed 1,242/1,244. All 141 standard-library tests, 921
MathLive comparisons, three native LaTeX view/iframe checks, and the Test262
runner-contract check pass. The font regression also passes from a different
working directory with spaces in `LAMBDA_HOME`. A rendered sample is available
locally as
[`fixed-sample.png`](../../temp/math-font-selection/fixed-sample.png).

**Baseline caveat.** Concurrent builds removed the shared executable during
the full baseline runs, so failed suites were rerun against an isolated copy.
The remaining `edit_markdown_adapter` and `edit_view_only` failures concern
Markdown block locations and footnote projection. Both reproduce against the
same executable with this font change removed from a temporary package copy;
no expectations or tolerances were changed to accommodate them.

<a id="rad07-l1"></a>

### RAD07-L1 — Faint math glyphs in the macOS grayscale rasterizer — RESOLVED

**Fixed and verified:** 2026-10-08, in the working tree.

**Symptom and font identification.** Mathematical expressions in
[`enhanced_test.tex`](../../test/input/enhanced_test.tex) had visibly faint
strokes, especially small italic variables. The sample actually resolved math
variables to **Times Italic** (`Times-Italic`), symbols/digits to **STIX Two
Math**, and surrounding prose to **Georgia**. The generated document did not
load the bundled Computer Modern fonts despite listing math families in CSS.
Chrome's platform-font inspection confirmed the same Times Italic face for
the isolated comparison.

**Root cause.** In
[`font_rasterize_ct_render`](../../lib/font/font_rasterize_ct.c), italic faces
explicitly disabled CoreGraphics font smoothing. The rasterizer also drew
white glyphs on black and unconditionally squared the grayscale values using
`(v * v + 128) / 255`. This applied an LCD-style gamma conversion to native
DeviceGray output, reducing edge coverage and erasing thin strokes.

**Fix.** Enable native smoothing for upright and italic faces, draw grayscale
glyphs as black on white, and obtain mask coverage with `255 - v` without an
additional gamma transform. Color-glyph handling and layout metrics are
unchanged. The font-pipeline record marks the earlier gamma approach as
[superseded](Radiant_Design_Font_Text.md).

**Chrome pixel comparison.** Captured the same Times Italic glyphs
(`E m c x d π e 2`) at fixed positions in Radiant and Chrome headless shell
154.0.8037.57, with black text on white. Tested 8/16/24/48/96 CSS px at 1× and
2× device scale. Representative results below require no positional alignment
or resampling. “Ink difference” is the difference in summed darkness relative
to Chrome; negative means fainter. “Pixel error” is the sum of absolute pixel
differences divided by Chrome's total ink, not by the whole image area.

| Size / scale | Ink difference before → fixed | Pixel error before → fixed |
|---|---:|---:|
| 96px / 1× | −17.55% → +0.60% | 17.55% → 0.83% |
| 16px / 1× | −44.01% → +3.30% | 44.01% → 3.99% |
| 96px / 2× | −9.36% → +0.32% | 9.36% → 0.44% |
| 16px / 2× | −34.56% → +1.63% | 34.56% → 2.20% |

**Regression coverage.**
[`FontMetricTest.RasterPreservesNativeGrayscaleCoverage`](../../lib/test_font_path_gtest.cpp)
compares production masks against independently drawn native RGB CoreText
text. All 96 comparisons pass: Times Italic and bundled CMU regular, italic,
and bold italic; four characters; 8/16/96px; 1×/2× scale. The test fails with
the original rasterizer. `make build` and all 265 view-reuse tests also pass.

**Validation limits.** `make test-radiant-baseline` reports 4,094 passed,
350 partially passing, and one failure. The remaining `pp_btn_shapes_01`
visual comparison measures 1.85% mismatch against a 1.84% limit; changing the
rasterizer increases mismatching pixels from 256 to 267 in the isolated
before/after check. References and tolerances were not changed. Small
baseline-position offsets at some sizes and color-dependent tonal differences
on dark backgrounds remain; this fix does not claim pixel-identical Chrome
rendering in every case.

**Local diagnostic artifacts** (temporary, not tracked):
[interactive pixel comparison](../../temp/math-render-quality/comparison.html),
[comparison image](../../temp/math-render-quality/comparison.png),
[corrected sample](../../temp/math-render-quality/after.png), and
[full baseline log](../../temp/math-render-quality/final-baseline.log).
