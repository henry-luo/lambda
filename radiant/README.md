# Radiant: efficient development and testing

Radiant is Lambda's C/C++ HTML/CSS/SVG layout, rendering and interaction engine,
built into `lambda.exe`. Lambda's loaders also let it present Markdown, LaTeX,
PDF, images and Lambda-generated documents through the same document pipeline.

This guide records the planning and verification workflow for Radiant work.
Follow [AGENTS.md](../AGENTS.md); use the [test map](../test/README.md) for
required gates and fixture conventions, the
[developer guide §7](../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas)
for build/environment problems, the [engine map](#engine-map) and
[invariants](#engine-invariants) for orientation, and the
[design records](../vibe/radiant/) for subsystem details.
Read the relevant section and source symbols first; old progress totals and
architecture summaries need checking against current code.

## Prioritize common-page impact

Broad HTML/CSS support has an effectively unbounded conformance tail. Prioritize
and finish the most important features first, then work down the ordered backlog.
Choose the next milestone by user-visible impact, frequency on representative
pages, dependencies it unlocks and expected implementation/verification effort.
An easy-to-reproduce failure or a large count of failing synthetic cases alone
does not establish priority.

- **Deliver usable features.** Complete the common end-to-end path through
  computation, layout, paint and relevant mutation. Give missing everyday page
  behavior priority over polishing an already usable feature's rare contexts.
- **Fix blockers first.** Crashes, corruption and serious shared defects that
  prevent reliable delivery can preempt the order. Assess the affected usage
  and severity before promoting an incidental correctness case to P0.
- **Close the core milestone.** Set finite acceptance cases before starting.
  Once those cases and required checks are satisfied, mark that milestone done
  and take the next important feature. The broader family can remain honestly
  labeled partial, with its unsupported cases listed separately.
- **Defer the long tail deliberately.** Obscure value combinations, exhaustive
  CSSOM reflection, unusual cross-feature interactions and isolated minor pixel
  differences stay in the backlog unless they block the selected outcome or
  reveal a serious shared problem. Never expand every repair across all
  properties, writing modes, pseudo-elements and output formats by default.
- **Reassess at feature boundaries.** Compare the next edge case with the next
  missing core feature. Preserve the user's order and explicit requirements;
  explain material priority changes. A new reproducer should not silently
  replace the delivery plan.

Track core feature delivery separately from conformance residue. Full standards
coverage can remain a long-term objective while useful milestones finish.

## Plan a finite delivery

P0/P1 labels specify priority. Turn broad requests such as "finish CSS support"
into an ordered checklist of observable behaviors before implementation.
Keep one milestone active and close it before moving to the next. For a request
covering several milestones, continue through that checklist within the user's
authorized scope.

Write a short plan with:

- **Priority and outcome:** the common-page problem this milestone solves and
  why it precedes the next feature.
- **Done criteria:** named cases with expected computed values, geometry, pixels
  or interaction. Include the original failing case.
- **Owner and hypothesis:** the stage that loses the value or state, the shared
  helper to repair, and the evidence supporting that diagnosis.
- **Scope:** included contexts and explicit exclusions. Add an adjacent case
  when it challenges the cause; put independent extensions in the backlog.
- **Verification budget:** checks in the edit loop, relevant neighboring tests,
  required milestone gates, and any justified sanitizer/browser/export work.
- **Completion record:** implemented behavior, failed or pending checks, and
  the next milestone. Test counts and registered property names are evidence
  inventories, not a completion percentage.

For example, "calculated z-index retains numeric-zero identity after explicit
inheritance and reset" has a bounded result. "Audit every stacking context"
needs its own milestone. A newly found crash or lifetime defect can become a
blocking prerequisite; record that scope change instead of silently expanding
the feature forever.

## Diagnose the owning stage before editing

Trace one minimal reproducer through:

`parse → cascade/substitution → computed value → used layout → paint/hit test → capture`

Check the first disagreement. A correct computed string can hide an unused
layout value; correct geometry can be captured before a script finishes. Compare
initial state and one causal mutation where live behavior is part of the task.
Use `rg` to find an existing helper and its callers before adding another path.

| Area | Start here |
|---|---|
| CSS parsing/cascade | `../lambda/input/css/`; `resolve_css_style.cpp`, `css_prop_table.cpp` |
| View/style data | `view.hpp`; DOM element is also the layout view |
| Layout | `layout.hpp`, `layout.cpp`, `layout_*.cpp`, `intrinsic_sizing.cpp` |
| Paint/export | `render.hpp`, `render_walk.cpp`, `render_clip.cpp`, `render_*.cpp` |
| Input/state | `event.hpp`, `event.cpp`, `scroller.cpp` |
| Script readiness/shell | `radiant.hpp`, `script_runner.cpp`, `cmd_layout.cpp` |

Preserve selected-view/style ownership (**D5.5.1 / D5.1.4**), precise roots
and evaluator lifetime (**D6.3.3 / D6.4.1**), and module boundaries
(**D9.4.3–D9.4.4**) from the
[formal design](../doc/Lambda_Formal_Design.md). Keep layout coordinates and
dimensions as `float`, reuse shared computation/paint geometry, and fix causes
instead of adding fixture-specific offsets or timing delays.

If an experiment fails without narrowing the hypothesis, reduce the reproducer
or inspect the owning state before another edit. Repeating a broad gate is not
a substitute for diagnosis.

## Verify in widening steps

1. **Reproduce:** run the smallest existing case, or add a meaningful regression
   that fails for the identified cause. Confirm its selector actually runs tests.
2. **Edit loop:** rebuild the needed target and rerun that case. Batch related
   fixes to the same cause before expensive qualification.
3. **Neighboring coverage:** exercise the paths sharing the changed helper.
   Choose cases that could disprove the repair: for example auto versus zero,
   inherited versus local values, or a retained reflow.
4. **Milestone gate:** once the implementation is stable, run the required gates
   in [the test map](../test/README.md#1-which-gate-do-i-run). Radiant engine work
   requires the Radiant baseline; runtime/DOM/Jube changes can require additional
   gates. A failed required gate remains a reported qualification limit.
5. **Stop verification:** after relevant checks pass, move to the next delivery.
   Rerun only when new code, a failure or an unresolved concern gives a concrete
   reason. A merge or rebuild with relevant changes needs fresh qualification.

Do not manually replay every component already covered by a completed aggregate.
Isolate a failed component when needed. Keep passing evidence for unchanged
code/configuration; do not transfer it to a different binary or build profile.

| Change | Useful focused evidence | Broaden when |
|---|---|---|
| Documentation only | Diff, local links, command/source review | A documented command or source assumption cannot be checked statically |
| Parser/computed value | Relevant CSS parser or `test_css_animation_gtest` cases; real consumer when affected | Shared substitution/defaulting or multiple consumer paths change |
| Layout geometry | Native text/layout case, then affected layout category | Shared formatting-context, intrinsic-size or font logic changes |
| Paint/clip/sampling | Targeted vector/export test and visible coverage | Shared paint IR, backend geometry, transforms or device scale changes |
| Mutation/input | One UI fixture with causal geometry/pixel/hit checks | Dispatch, invalidation, scheduling or shared state changes |
| Ownership/retained payloads | Focused teardown/reflow/GC case; ASan where applicable | Allocation, storage, roots or lifetime boundaries change |

Choose additional verification to answer a question:

- **AST and MIR:** use both for affected JS execution/bridge/callback paths.
  A C++ geometry edit does not itself require replaying every JS fixture twice.
- **PNG/SVG/PDF:** check affected outputs; expand across backends for shared
  paint changes. Computed-only reads do not need a full export audit per edit.
- **Densities/browser capture:** use relevant device scales for sampling/rounding
  and a minimal browser reference for a disputed layout or paint result. An
  unchanged browser/reference fixture can be reused across implementation trials.
- **ASan:** use it for memory/lifetime concerns and required qualification.
  Prefer the supported `make build-debug-asan` profile (`lambda-debug-asan.exe`)
  over hand-maintained compile/link recipes. Native test instrumentation is
  configured separately; an ASan child does not instrument its parent harness.
  State partial coverage and leak settings when relevant. Do not rebuild all
  sanitizer configurations for each arithmetic or documentation edit.
- **Boundary/inventory/lint:** run boundary validation when module APIs,
  imports or ownership placement change; refresh the CSS inventory when its
  source wiring/references change. Run the required integer-cast lint for layout
  edits. Reserve unrelated expensive audits for a concrete need.

Before repeating an expensive gate, state what changed since its previous run
and what the rerun will establish. Avoid an automatic cycle of full core,
Radiant, exporter, browser and sanitizer runs after every small patch.

## Build and run the target you need

Run these commands from the repository root. Examples use existing cases;
replace filters with the cases relevant to the change.

```sh
# CLI changes or CLI-driven layout/UI checks; also regenerates build configuration.
make build

# Native tests need their current static archives, not only a rebuilt CLI.
make -C build/premake config=debug_native test_css_animation_gtest -j8
./test/test_css_animation_gtest.exe --gtest_filter='MotionCascadeTest.ZIndex*'

# Pick a single consumer instead of a whole corpus during implementation.
make layout test=baseline_301_simple_margin
./test/test_ui_automation_gtest.exe --suite baseline --test css_z_index_computed

# Required Radiant milestone gate: build and run the configured selection once.
make test-radiant-baseline

# Use this when the matching runtime and all selected native runners are current.
make run-radiant-baseline

# Cheap checks when their source areas changed.
make lint ARGS='--rule ^no-int-cast-radiant$'
python3 utils/generate_css_support_inventory.py --check
git diff --check
```

For an in-process text/layout test, build `test_radiant_document_gtest` through
the same top-level `build/premake` Makefile, then filter the binary. Build an
export test target when its native renderer is needed. `make build-test` builds
unrelated projects too; use it when the required test map calls for that scope.
See the [Make guide](../doc/dev/Make_Guide.md) for more targets.

## Build, harness and reference pitfalls

- **Match the binary to the source.** `make build` updates the CLI but does not
  refresh every native test archive. Use the top-level Premake target with its
  dependencies after production/header edits. A passing CLI and failing stale
  native executable can describe different code. `make build-input-baseline`
  covers selected parser corpora, not every CSS executable in `test-input`.
  For that full gate, get its projects with `node test/test_run.js --target=input
  --list-build-targets` and build those projects through the top-level Premake
  Makefile first.
- **Keep builds and consumers separate.** Never replace `lambda.exe`, a DSO or
  a test executable while a running check uses it. Run the long core/forced-GC
  gate without competing builds or render sweeps; use the shared runner's bounded
  scheduling instead of stacking several aggregate processes. Wait for the
  entire core gate, including package/corpus checks after forced GC, to finish.
- **Isolate page-load failures with the CLI.** The page-load runner executes
  its page batch before applying the GTest reporting filter. For one crashing
  page, use `./lambda.exe view path/to/page.html --headless` instead of rerunning
  the whole page batch under a single-test filter.
- **Canonicalize capture paths.** The companion corpus is symlinked. Compare
  canonical category and request paths when resolving missing WPT support
  files, and verify the scripts/images actually loaded before replacing a
  browser reference. Equal page filenames do not prove equal capture inputs.
- **Check setup once.** Verify corpus links, fonts, reference tools, build profile
  and native graphics access before a large run. Follow the developer guide for
  worktree links, same-second source changes and stale archive members. Rebuild
  affected Node DSOs after their shared runtime layout changes. In a restricted
  macOS session, first run one native graphics control; repeated XPC connection
  errors followed by hangs require graphics access, not longer test timeouts.
- **Inspect harness semantics.** Check suite membership, exact output names,
  output-directory setup and comparator fields (`assert_attribute` uses
  `equals`). A missing assertion or zero selected cases is not passing coverage.
  Reuse the shared layout stream reader instead of decoding binary output ad hoc.
- **Observe before reporting.** In mutation fixtures, check geometry/pixels
  before writing diagnostic DOM attributes that might trigger a second recascade.
  Assert visible/nonempty output so two empty captures cannot pass comparison.
- **Wait for the right state.** Honor explicit readiness using existing bounded
  scheduling; preserve ordinary recurring-animation sampling. Avoid fixed sleeps
  or fixture-specific frame counts to make a capture pass.
- **Classify failures once.** Preserve the raw result and distinguish engine
  defects, harness/environment errors and stale references. Compare a relevant
  predecessor when attribution is uncertain. Rerun a timeout alone before
  assigning a cause; use controlled repeats only when investigating instability.
- **Keep known debt visible.** Record the exact failing case and its evidence;
  revisit it when a relevant change or new symptom warrants it. Independent
  failures belong in the backlog. Do not weaken assertions, floors or goldens
  to obtain a green gate; legitimate reference corrections need independent
  evidence and a deliberate reviewable change.
- **Keep artifacts small and local.** Put all scratch files under `./temp/`.
  Keep one reproducer, useful before/after evidence, and milestone logs. Archive
  exact binaries/source once when comparing configurations or preserving a hard
  failure; avoid copying the whole provenance tree after every edit.
- **Measure release code.** Use an optimized build for performance tests. Never
  run `make release` in a worktree; use `make build-release-compile` and the
  appropriate runner profile as described in the developer guide.

## Record and finish

Keep the implementation checklist short: **done behavior / open behavior /
verification result / next action**. Link detailed logs instead of repeatedly
copying historical totals, hashes and failed experiments into progress prose.
Existing experimental history can stay in its archive. Maintain a single current
checkpoint that survives long-session handoffs.

At handoff, report what changed, why, the relevant checks and material limits.
Distinguish implemented-but-unqualified work from completed milestones. Preserve
the requested scope across sessions; stop investigating independent extensions
once the agreed acceptance cases and required checks are satisfied.

## Engine map

Last verified against tree: 2026-11-06. Radiant runs on the shared Lambda
runtime, not beside it: it uses the same `Item` values, MIR JIT, input parsers
and `MarkBuilder`, and page scripts run on LambdaJS. Each source format loads
into a `DomDocument` through a `load_*_doc()` function, so layout, paint and
interaction are written once.

| Stage | What happens | Start at | Design record |
|---|---|---|---|
| Load | An HTML page goes through parse, cascade, `execute_document_scripts()`, an optional recascade, then layout. | `cmd_layout.cpp`, `script_runner.cpp` | [Unified DOM](../vibe/radiant/Radiant_Design_Unified_Dom.md) |
| Style | `lambda/input/css/` parses and cascades, and leaves `specified_style` on each element. Radiant adds UA defaults and resolves used values through `resolve_css_property()`. | `resolve_htm_style.cpp`, `resolve_css_style.cpp` | [CSS cascade](../vibe/radiant/Radiant_CSS_Cascade.md) |
| Layout | `layout_html_doc()` calls `layout_flow_node()`, which selects the layout mode for each node. `RunMode` separates measuring from committing. | `layout.cpp`, `layout_*.cpp`, `intrinsic_sizing.cpp` | [Flex](../vibe/radiant/Radiant_Design_Flex_Layout.md), [grid](../vibe/radiant/Radiant_Grid_Layout4_Taffy.md), [table](../vibe/radiant/Radiant_Table_Layout_Design.md), [multicol](../vibe/radiant/Radiant_Layout_Multicol.md), [paged](../vibe/radiant/Radiant_Layout_Paged_Media.md) |
| Text | Line breaking and bidi run during layout. `lib/font/` is the font engine; `font.cpp` and `font_face.cpp` connect CSS to it. | `layout_inline.cpp`, `layout_bidi.cpp` | [Font and text](../vibe/radiant/Radiant_Design_Font_Text.md) |
| Paint | `render_walk.cpp` visits the laid-out tree. Producers emit PaintIR, which lowers to a raster DisplayList, SVG or PDF. Vector drawing goes through `RdtVector` to ThorVG. | `paint_ir.cpp`, `display_list.cpp`, `render_output.cpp` | [Rendering](../vibe/radiant/Radiant_Design_Rendering.md) |
| Present | Live windows use the GPU compositor by default; `RADIANT_GPU_COMPOSITOR=off` restores CPU presentation. Headless and export paths never composite. | `ui_context.cpp` | [GPU compositor](../vibe/radiant/Radiant_Design_GPU_Compositor.md) |
| Events | `handle_event()` is the one funnel for platform input. `event_sim.cpp` drives UI tests through it. | `event.cpp`, `event_sim.cpp`, `../lambda/dom/dom_events.cpp` | [Events](../vibe/radiant/Radiant_Design_Event.md), [DOM dispatch](../vibe/Lambda_Design_DOM_Dispatch.md) |
| State | `DocState` holds per-document interaction state. `state_end_event_cascade()` validates it at the end of each event cascade. | `state_store.cpp`, `state_machine.cpp`, `state_schema.cpp` | [State machine](../vibe/radiant/Radiant_Design_State_Machine.md), [state management](../vibe/radiant/Radiant_Design_State_Management.md) |
| Editing | The shipped DOM behavior package owns editing policy; Radiant owns the input gate and the standard mechanisms (**D10.2.1**). Form text controls edit natively. | `dom_range.cpp`, `text_edit.cpp`, `text_control.cpp` | [Editable](../vibe/radiant/Radiant_Design_Editable.md), [selection](../vibe/radiant/Radiant_Design_Selection.md), [form input](../vibe/radiant/Radiant_Design_Form_Input.md) |
| Animation | `frame_clock.cpp` wakes the main loop. CSS and media animations sample on the page thread. | `animation.cpp`, `css_animation.cpp` | [Animation repaint](../vibe/radiant/Radiant_Design_Animation_Repaint.md) |
| Shell | `UiContext` owns the window, the surface and the current document. | `window.cpp`, `ui_context.cpp` | [Web browsing](../vibe/radiant/Radiant_Web_Browse.md), [webview](../vibe/radiant/Radiant_Webview.md) |

[`doc/HTML_CSS_SVG_Support.md`](../doc/HTML_CSS_SVG_Support.md) states what the
engine supports, and the [issue ledger](../vibe/radiant/Radiant_Issue_Ledger.md)
lists its known defects.

## Engine invariants

A violation of these rules usually fails silently, as corrupt storage or stale
state. The rules on `float` dimensions and on ownership are under
[Diagnose the owning stage](#diagnose-the-owning-stage-before-editing).

- **The DOM node is the view.** `typedef DomNode View`: there is no separate
  view or render tree. `set_view()` tags a node in place during layout, and
  script and editor mutations touch what layout measured. To change geometry,
  mark `layout_dirty` and lay out again.
- **Views add methods, never fields.** View classes share memory with
  `DomNode`, `DomText` and `DomElement`. The `unsafe_view_*` casts in
  `lib/tagged.hpp` depend on this, and `static_assert`s in `view.hpp` pin the
  sizes.
- **Node identity is `id`, never a pointer.** `dom_document_alloc_node_id()`
  gives each node an ID that is monotonic within a document epoch. State that
  must outlive a pass binds to that ID.
- **A retained reset keeps the nodes.** `view_pool_reset_retained()` keeps the
  nodes and their property blocks. It resets only external payloads and
  generation-local values.
- **`specified_style` is the input; used values live on the element.** There is
  no shared computed-style object. Used values are in property groups such as
  `FontProp` and `BlockProp`, and `styles_resolved` says whether they are
  current. The cascade itself belongs to `lambda/input/css/`.
- **Measuring leaves no trace.** `RunMode::ComputeSize` and `LayoutMeasureScope`
  measure a subtree, then restore layout state.
- **Radiant keeps no GC pointer.** Storage that outlives the current call copies
  the value or holds a registered root (**D5.5.2**).
- **Page work stays on one thread; tile workers only replay.** Script, style,
  layout and display-list build share the page thread
  ([RC1](../vibe/radiant/Radiant_Design_Concurrency.md)). Replay on a tile
  worker must not resolve fonts or load resources.

## Using the engine

```sh
./lambda.exe layout test/html/index.html
./lambda.exe render test/html/index.html -o temp/radiant-example.svg
./lambda.exe view test/html/index.html
```

`layout_html_doc()` drives layout; `render_html_doc()` drives painting;
`render_html_to_svg()`, `render_html_to_pdf()`, `render_html_to_png()` and
`render_html_to_jpeg()` provide exports. `view_doc_in_window()` opens the viewer.
GLFW, native font APIs and ThorVG supply platform/window/font/vector services;
Lambda supplies parsing, runtime values and scripting. See
`./lambda.exe <command> --help` for CLI options.
