# Radiant Issue Ledger

**Historical Lambda-hosted ledger.** New Radiant issues belong in the
[Radiant project ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md)
under the ownership boundary in D9.1.1v4 and D9.1.3v2. Native records link
to their new location; mixed and resolved records remain here. The historical
claims require re-verification.

The historical entries are grouped by engine area
(`RAD01`–`RAD22`). Entries originating here use an `L` suffix, such as
`RAD07-L1`, to avoid collisions with the numbered items under
[Area known issues](#area-known-issues). Resolved entries retain stable anchors
and their verification evidence.

## Open issues

<a id="rad13-l1"></a>

### RAD13-L1 — Scene3D gaming profiles remain incomplete

The native gaming profile issue moved to [RAD13-L1 in the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad13-l1). The four Lambda Script gameplay-algorithm scopes remain Lambda work and are still KIV in the historical record.

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
against the tree. Item numbers are stable IDs: `RAD06-3` is item 3 under `RAD06`. Native Radiant items link to their moved records; Lambda-owned, input-owned, mixed, resolved and non-issue entries remain here.

### RAD01 — View & DOM Model

1. ~~**The zero-field invariant is convention-enforced, not compiler-enforced.**~~ Resolved: `static_assert`s in `view.hpp` pin every view class to the size of its DOM struct.
2. <a id="rad01-2"></a> **`native_element` is a transitional copy, not an alias.** `DomElement::native_element` is marked `TODO(Phase 4): Remove; use dom_element_to_element()` (`dom_element.hpp:307`), and the embedded `Element` is currently *copied* from the source element in `dom_element_init` rather than aliased. Until removed, there are two notions of "the element" to keep in sync. **Ownership review, 2026-11-09:** the implementation is input-owned and has changed; see [LI-003](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-003) for the unverified current question.
3. **[RAD01-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad01-3).**

4. **Source-position bridge maturity.** The header warns that functions may be no-op `return false` pending `render_map` path recording (`event.hpp`); editor round-trip fragility is concentrated here and depends on a weak/incremental `render_map` dependency.
5. **[RAD01-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad01-5).**

### RAD02 — CSS Style Resolution & Computed Style

1. **[RAD02-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad02-1).**

2. **[RAD02-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad02-2).**

3. **[RAD02-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad02-3).**

4. **[RAD02-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad02-4).**

5. **Two inheritance code paths.** Radiant hand-rolls the `inheritable_props[]` loop (`resolve_css_style.cpp:3971`) even though the CSS side exposes `style_tree_apply_inheritance` (`css_style_node.hpp`). Two implementations of "inherit unset properties" can drift apart.
6. **[RAD02-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad02-6).**

7. **[RAD02-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad02-7).**

8. **[RAD02-8 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad02-8).**

### RAD03 — Layout Driver, Block Layout & Block Formatting Context

1. **[RAD03-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad03-1).**

2. **[RAD03-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad03-2).**

3. **[RAD03-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad03-3).**

4. **[RAD03-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad03-4).**

5. **[RAD03-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad03-5).**

6. **[RAD03-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad03-6).**

### RAD04 — Box Model, Containing Blocks & the Layout Cache

1. **[RAD04-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad04-1).**

2. **[RAD04-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad04-2).**

3. **[RAD04-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad04-3).**

4. <a id="rad04-4"></a> **Cache invalidation is coarse and by-construction.** The `LayoutCache` is cleared wholesale on style re-resolution (`layout.cpp:816`) and nulled on pool swap (`view_pool.cpp:587`, `event.cpp:3845`), but there is no descendant-aware invalidation — a parent trusts that any changed descendant re-resolved its own style and thereby cleared its own cache. A mutation that changes layout inputs without triggering a child's style re-resolution could serve a stale measurement. *Improvement:* a dirty-propagation assertion tying cache validity to `layout_dirty`. The native portion is recorded as [RAD04-4R](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad04-4r); the event-dispatch boundary remains mixed.
5. **[RAD04-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad04-5).**

6. **[RAD04-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad04-6).**

7. **[RAD04-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad04-7).**

### RAD05 — Intrinsic Sizing

1. **[RAD05-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad05-1).**

2. **[RAD05-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad05-2).**

3. **[RAD05-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad05-3).**

4. **[RAD05-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad05-4).**

5. **[RAD05-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad05-5).**

6. **[RAD05-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad05-6).**

7. **[RAD05-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad05-7).**

### RAD06 — Inline & Text Layout

1. **[RAD06-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-1).**

2. **[RAD06-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-2).**

3. **[RAD06-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-3).**

4. **[RAD06-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-4).**

5. **[RAD06-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-5).**

6. **[RAD06-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-6).**

7. **[RAD06-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-7).**

8. **[RAD06-8 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-8).**

9. **[RAD06-9 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-9).**

10. **[RAD06-10 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad06-10).**

### RAD07 — Fonts

1. **[RAD07-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad07-1).**

2. **[RAD07-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad07-2).**

3. **[RAD07-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad07-3).**

4. **[RAD07-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad07-4).**

5. **[RAD07-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad07-5).**

6. **[RAD07-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad07-6).**

7. **[RAD07-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad07-7).**

### RAD08 — Flexbox Layout

1. **[RAD08-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad08-1).**

2. **[RAD08-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad08-2).**

3. **[RAD08-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad08-3).**

4. **[RAD08-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad08-4).**

5. **[RAD08-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad08-5).**

6. **[RAD08-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad08-6).**

7. **[RAD08-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad08-7).**

8. **[RAD08-8 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad08-8).**

### RAD09 — CSS Grid Layout

1. **[RAD09-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad09-1).**

2. **Track/area CSSOM and grammar remain partial.** Shared grid-definition and decoded-area grammar now supply parse-time/substituted-value validation, component cascade, owned projection and CSSOM mutation. Calculated repeat counts have owner-font, inheritance, variable/defaulting, specified/computed and mutation controls. Pseudo-element reads and broader owner-unit/track grammar remain open.
3. **[RAD09-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad09-3).**

4. **[RAD09-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad09-4).**

5. **[RAD09-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad09-5).**

### RAD10 — Table Layout

1. **[RAD10-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad10-1).**

2. **[RAD10-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad10-2).**

3. **[RAD10-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad10-3).**

4. **[RAD10-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad10-4).**

### RAD11 — Positioned, Floats, Multi-column, Lists & Counters

1. **[RAD11-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad11-1).**

2. **[RAD11-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad11-2).**

3. **[RAD11-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad11-3).**

4. **[RAD11-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad11-4).**

5. **[RAD11-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad11-5).**

6. **[RAD11-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad11-6).**

### RAD12 — Paint IR, Display List & Backend Abstraction

1. **[RAD12-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad12-1).**

2. **[RAD12-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad12-2).**

3. **[RAD12-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad12-3).**

4. **[RAD12-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad12-4).**

5. **[RAD12-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad12-5).**

6. **[RAD12-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad12-6).**

### RAD13 — Render Walk, Painters & Export

1. **[RAD13-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad13-1).**

2. **[RAD13-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad13-2).**

3. **[RAD13-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad13-3).**

4. **[RAD13-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad13-4).**

5. **[RAD13-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad13-5).**

6. **[RAD13-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad13-6).**

7. **[RAD13-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad13-7).**

### RAD14 — SVG, Vector Graphics & Diagram Layout

1. **[RAD14-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad14-1).**

2. **Graph layout is Dagre-inspired, not a faithful port** (`lmd/package/graph/dagre.ls`). It still lacks network-simplex ranking, dummy nodes, Brandes-Kopf x-assignment, clusters, parallel-edge separation, and self-loop routing. *Improvement:* normalize long edges through dummy ranks before repeated crossing-reduction sweeps.
3. **[RAD14-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad14-3).**

4. **[RAD14-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad14-4).**

5. **[RAD14-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad14-5).**

6. **[RAD14-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad14-6).**

7. **[RAD14-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad14-7).**

8. **[RAD14-8 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad14-8).**

### RAD15 — Events & Input Dispatch

1. *(Resolved.)* The duplicated layer-mode webview block is gone; `target_block_view` has one `WEBVIEW_MODE_LAYER` inject block.
2. **[RAD15-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad15-2).**

3. **Legacy contenteditable key path has unfinished stubs.** `event.cpp:9303/9311/9316/9488/9492` carry `TODO: delete selected text` / `TODO: insert character at caret` in a legacy key handler superseded by the editing-controller/transaction path (comment at `event.cpp:9411`). The dead path should be removed so the transaction path is the only key→edit route.
4. <a id="rad15-4"></a> **`handle_event` monolith.** `event.cpp` is ~9,554 lines / 440KB and `handle_event` is a ~2,900-line switch; `event_sim.cpp` is ~5,902 lines. High cognitive load. Splitting per-`EventType` handlers into separate translation units would help without changing the single-funnel contract. `event.cpp` is Lambda-owned; the native event-simulation portion is recorded as [RAD15-4R](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad15-4r).
5. **[RAD15-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad15-5).**

### RAD16 — Animation & Frame Scheduling

1. **[RAD16-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad16-1).**

2. **[RAD16-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad16-2).**

3. **[RAD16-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad16-3).**

4. **[RAD16-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad16-4).**

5. **[RAD16-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad16-5).**

6. **[RAD16-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad16-6).**

7. **[RAD16-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad16-7).**

### RAD17 — Interaction State: DocState, State Machine & Schema

1. **[RAD17-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad17-1).**

2. **[RAD17-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad17-2).**

3. **[RAD17-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad17-3).**

4. **Selectionchange coalescing spans document and text-control drains.** `selection_mutation_seq`/`selection_event_seq` plus the per-text-control `tc_selectionchange_head` linked-list drain form a subtle ordering surface with no single owner.
5. **[RAD17-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad17-5).**

### RAD18 — Editing, Selection & DOM Ranges

1. **[RAD18-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad18-1).**

2. **[RAD18-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad18-2).**

3. **[RAD18-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad18-3).**

4. **[RAD18-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad18-4).**

5. **[RAD18-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad18-5).**

6. **Single-range selection.** `DOM_SELECTION_MAX_RANGES == 1`; extra `addRange()` calls are silently ignored, as required by the Selection API. This is not a conformance gap. **Ownership review, 2026-11-09:** the current Radiant header defines this internal capacity as 4, so the historical capacity claim is stale; public Selection behavior was not reverified.
7. **Editor matrix boundary.** CodeMirror, ProseMirror light DOM, and Editor.js have pinned offline authoritative-state probes. The capability manifest is the source of truth for their supported configurations and explicit exclusions, including Editor.js ordinary paragraph paste, which upstream delegates to browser-native structural editing and this gate intentionally does not restore. An editor render or one typing probe is not treated as blanket compatibility.

### RAD19 — Form Controls & Text-Control Editing

1. **[RAD19-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad19-1).**

2. **[RAD19-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad19-2).**

3. **[RAD19-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad19-3).**

4. **[RAD19-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad19-4).**

5. **[RAD19-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad19-5).**

6. **[RAD19-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad19-6).**

7. **[RAD19-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad19-7).**

8. **[RAD19-8 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad19-8).**

### RAD20 — Application Shell, Browsing & Document Loading

1. **[RAD20-1 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad20-1).**

2. **[RAD20-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad20-2).**

3. **[RAD20-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad20-3).**

4. **[RAD20-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad20-4).**

5. **[RAD20-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad20-5).**

6. **[RAD20-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad20-6).**

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
2. **[RAD22-2 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad22-2).**

3. **[RAD22-3 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad22-3).**

4. **[RAD22-4 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad22-4).**

5. **[RAD22-5 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad22-5).**

6. **[RAD22-6 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad22-6).**

7. **[RAD22-7 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad22-7).**

8. **[RAD22-8 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad22-8).**

9. **[RAD22-9 moved to the Radiant ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#rad22-9).**

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
