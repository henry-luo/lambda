# Radiant Design Review — Assessment, Issues & Enhancement Proposals

> A critical design review of the Radiant engine (`radiant/`, ~198k LOC), written after producing the detailed-design set [doc/dev/radiant/RAD_00–RAD_23](../../doc/dev/radiant/RAD_00_Overview.md). Every claim here is grounded in the current code (via the per-doc surveys and their verified `file:line` citations); this document adds the judgment layer the design docs deliberately keep out: how good is the design, where does it diverge from production engines (Chrome/Blink primarily, Ladybird/Servo where instructive), what are the real risks, and what should be done about them — concretely, ranked, and phased.

---

## 1. What Radiant is optimizing for

Radiant is not trying to be Chrome. Its goals, read off the code and the CLI surface, are: (a) a **document renderer/converter** (`layout`/`render` to SVG/PDF/PNG) with browser-compatible layout, (b) a **document viewer + editor host** (`view`, contenteditable, forms, the JS editor at the `beforeinput` seam), and (c) a **testable engine** (WebDriver, `event_sim` deterministic replay, layout regression suites). It runs single-process, single-UI-thread, on the shared Lambda runtime (Item model, GC, MIR JIT, MarkBuilder, `lib/font`), hosting LambdaJS for page scripts. That scope statement matters: several "divergences from Chrome" below are correct scope decisions, not defects — the review tries to be explicit about which is which.

## 2. Design strengths — what Radiant gets right

1. **The unified DOM/View tree is a coherent, defensible core decision** (deep dive in §3). For a single-process document engine it deletes an entire mirroring/diffing/sync layer that costs Blink real complexity, and it makes DOM mutation → relayout direct: the editor and JS touch exactly the structs layout measured.
2. **It borrows the right ideas from modern engines.** Grid is a C+ port of Rust **Taffy**'s track-sizing algorithm with explicit attributions (`grid_types.hpp:7` etc.); `AvailableSpace` is Ladybird-style typed availability; `RunMode`/`SizingMode` unify layout with measurement the way LayoutNG's constraint-space does; the 9-slot `LayoutCache` mirrors Taffy's cache design.
3. **The rendering pipeline is genuinely modern in shape.** A semantic Paint IR lowering to an immutable raster display list, record→replay, tiled parallel replay, retained-fragment incremental repaint, and PDF/SVG export lowering the *same* IR ([RAD_12](../../doc/dev/radiant/RAD_12_Paint_IR_Display_List.md)) — this is the same architectural direction as Blink's paint-artifact/`cc::DisplayItemList` stack, scaled down sensibly.
4. **The interaction layer has unusual test discipline for an engine this size.** A single `handle_event` funnel, a JSON-driven deterministic-replay harness (`event_sim.cpp`) feeding the same funnel, a JSONL event/state log, a declarative state schema with ~80 transition rules and ~26 invariant bindings asserted at a per-event cascade checkpoint ([RAD_17](../../doc/dev/radiant/RAD_17_Interaction_State.md)), and a W3C WebDriver server. Most hobby/indie engines have none of this.
5. **The C++/JS editing split is strategically smart.** Retiring the native rich-text engine and standardizing on `beforeinput` as the seam ([RAD_18](../../doc/dev/radiant/RAD_18_Editing_Selection_Ranges.md)) mirrors how the web platform itself divides labor (browsers provide primitives; editors like ProseMirror own semantics), and it shrinks the C++ surface that must be correct.
6. **DOM-backed everything.** Every input format (HTML, markdown, XML, SVG, images, source text, PDF, `.ls`, LaTeX) normalizes into a `DomDocument` before layout ([RAD_20](../../doc/dev/radiant/RAD_20_Application_Shell_Browsing.md)), so interaction features are written once. The retained-generated-node design (keep the producing `Runtime` alive rather than serialize/reparse) is the right call for dynamic documents.

## 3. The unified DOM/View tree vs Chrome — the deep dive

### 3.1 What Chrome does, and why

Blink maintains a chain of trees: DOM tree → a separate **layout tree** of `LayoutObject`s (attached/detached as `display` changes) → since LayoutNG an **immutable physical fragment tree** as layout *output* → paint artifacts → the compositor layer tree. The cost is enormous: tree-building/attachment code, invalidation sets between trees, and lifetime rules. Blink pays it for three loads Radiant currently does not carry:

- **1:N and 0:N node↔box mapping.** One element can produce many boxes (a block fragmented across columns/pages; an inline split around a block child; a `::first-line`) or none (`display:none`), and boxes exist with no element (anonymous table/block wrappers). An immutable fragment tree represents "this node produced these 3 fragments in these 3 columns" natively.
- **Thread separation.** The compositor thread scrolls/animates against immutable fragment/property-tree snapshots while the main thread runs script. Immutability is what makes cross-thread reads safe.
- **Invalidation precision.** Distinct trees give distinct lifecycle phases with fine-grained dirty tracking (style invalidation sets, layout subtree roots, paint invalidation rects).

### 3.2 What Radiant does

`typedef DomNode View` (`lambda/input/css/dom_node.hpp:67`). Every node carries one geometry (`x/y/width/height`), one `view_type` tag, and (on `DomElement`) lazily-allocated property-group pointers. The `View*` classes add methods only — zero fields, convention-enforced ("Do NOT add fields — views share memory with DomElement!", `view.hpp:1357`) — so "building the view tree" is `set_view`'s in-place `static_cast` + tag stamp (`view_pool.cpp:79/144`). Details in [RAD_01](../../doc/dev/radiant/RAD_01_View_and_DOM_Model.md).

### 3.3 Verdict

**For Radiant's scope, unified is the right call** — the win (no sync layer, direct mutation, small memory, simple mental model) is realized everywhere in the codebase, and the editor use-case benefits enormously from node identity being stable across relayout (`view_pool_reset_retained` keeps nodes, swaps property pools, and `DocState` anchors bind by monotonic node `id`).

**But the three loads Blink carries are exactly where Radiant shows strain today:**

- **Fragmentation is a bolt-on.** One node = one rect cannot represent one-node-many-fragments. Multicol works around this with side-structures (`ColumnGroup`/`ColumnFragment`/`FragmentedFlowCursor`) and is self-described in code as "simplified" ([RAD_11](../../doc/dev/radiant/RAD_11_Positioned_Float_Multicol_Lists.md)); inline `TextRect` chains are a second, text-only fragment mechanism; block-in-inline splitting is a third special case. Paged/print output — a natural fit for a PDF-exporting engine — will hit this wall much harder. This is the deepest architectural mismatch and the one that gets more expensive to fix the longer it waits.
- **The single mutable tree imposes a one-thread ceiling.** Layout, style, events, editing, script all share mutable state, so they all run on the UI thread. The engine's one designed concurrency seam — the immutable display list with tiled replay — is currently disabled whenever glyphs are present (`render_output.cpp:309`), i.e. on essentially every real page, so parallel replay rarely engages.
- **Invalidation is coarse.** `layout_dirty` + ancestor marking + cached `layout_height_contribution` skip-clean is a sensible v1, but the contribution invariant is unasserted (a stale value silently mis-positions siblings, [RAD_01 §6](../../doc/dev/radiant/RAD_01_View_and_DOM_Model.md)), and style changes trigger full re-resolution rather than property-scoped invalidation.

One more structural observation: the unified tree couples Radiant's node layout to `lambda/input/css/` struct definitions (`DomNode`/`DomElement` live outside `radiant/`). That is fine — but it means the zero-field ABI invariant spans two directories and two teams-of-one-file, which strengthens the case for compile-time enforcement (§5, P1).

### 3.4 Other engine comparisons in brief

- **Style:** Blink caches and *shares* `ComputedStyle` objects across similar elements and diffs old/new styles to scope invalidation. Radiant stores only cascaded `specified_style` and re-materializes used values through a ~248-case switch every layout ([RAD_02](../../doc/dev/radiant/RAD_02_CSS_Style_Resolution.md)). Correct, simple, and O(elements × properties) every relayout with no sharing.
- **Text:** Chrome's text stack (ICU bidi + HarfBuzz shaping + font fallback + shape caching) is arguably its densest accumulated engineering. Radiant has a streaming line-breaker with UAX #14 break classes and real kerning, but **no UAX #9 bidi reordering and no GSUB/complex shaping** ([RAD_06](../../doc/dev/radiant/RAD_06_Inline_and_Text_Layout.md)) — fine for Latin documents, not in the game for Arabic/Hebrew/Indic. Layout and render also re-measure text independently from byte ranges with no shared shaped buffer.
- **Security:** Chrome's defining architecture is multi-process site isolation. Radiant is single-process by design — appropriate for trusted documents and an editor host; worth an explicit statement of intent the moment `view`/`browse` is pointed at arbitrary web content (script watchdog + no isolation is not a sandbox).
- **Ladybird/Servo:** Radiant is philosophically closest to Ladybird (single-process, spec-following, pragmatic) and has consciously borrowed from it (`AvailableSpace`). Servo's lesson (parallel layout via immutable style/fragment sharing) is the same lesson as Blink's — parallelism follows immutability — and is relevant only if Radiant ever wants parallel layout, which §5 does not propose.

### 3.5 Prior art — who else uses a unified DOM/View tree

No mainstream browser engine does; the pattern lives one tier down, in HTML/CSS-like UI runtimes, and the single most instructive precedent is litehtml, which started unified and migrated away.

**Mainstream engines — all separate trees.** Blink (Chrome, WebView2, Android WebView): DOM → `LayoutObject` tree → immutable LayoutNG fragment tree. WebKit (Safari, WKWebView): DOM → render tree (`RenderObject`). Gecko (Firefox): DOM → frame tree (`nsIFrame`). Servo: DOM → box/fragment trees. Ladybird (LibWeb) — the youngest from-scratch engine and philosophically closest to Radiant — chose *three* trees from day one (DOM → layout tree → paintable tree). All the major webviews are just these engines embedded, so the webview landscape is the same. The convergence is not coincidence: they all hit the same three CSS pressures — anonymous boxes (boxes with no DOM node), one-node-many-boxes (fragmentation, inline splitting, `::first-line`), and the need for immutable snapshots readable from a compositor thread. The closest a major browser ever came to Radiant's model is early Trident (IE5/6-era), which hung layout objects (`CLayout`) lazily *off* element nodes rather than mirroring a parallel tree; later IE versions added a separate display tree anyway. Presto is reported to have kept layout boxes closely tied to elements but still distinct from the DOM node.

**Where the unified model does live.**
- **litehtml** (the lightweight engine embedded in several email clients and viewers) originally did layout **directly on its element tree**, exactly like Radiant. Its ~2022 refactor introduced a separate `render_item` tree, specifically because one element can produce multiple render boxes (inline splitting, table anonymous-box fix-up). It is the one engine that ran Radiant's experiment at scale and then paid the full retrofit cost — under the very fragmentation pressure §4 Tier-1 identifies for Radiant.
- **RmlUi** (née libRocket, the game-UI HTML/CSS middleware): a single element tree; elements carry their own boxes and layout state. It works because RmlUi deliberately supports a CSS subset without fragmentation.
- **Sciter** (closed-source HTML/CSS UI engine): reportedly also a single DOM tree with layout state on elements; unverifiable against source.
- **Retained-mode UI toolkits generally** — Android's `View` tree, WPF's `UIElement`, Qt Widgets: the element tree *is* the layout tree, with measure/arrange writing geometry onto the same node. This is arguably Radiant's true lineage: it treats the DOM the way UI toolkits treat their view hierarchy. (Flutter, notably, went the browser direction — widget → element → `RenderObject`, three trees.)

**Implication for Radiant.** The unified design is not unprecedented — it is the standard UI-toolkit model applied to a DOM, a good fit for a document-viewer/editor scope. But the empirical record is one-directional: every engine that needed full CSS (especially fragmentation) either started with separate trees or, like litehtml, retrofitted one; none went the other way. That record argues *for* P9's incremental path (the optional `FragmentList` side-table) rather than against the unified tree: litehtml demonstrates the cost of a full separate-tree retrofit, while the side-table adds fragment capability only where needed and keeps the unified fast path — a middle road none of the precedents took, fitting an engine whose primary workload (documents, editing) is mostly unfragmented. *Caveat: Presto and Sciter are closed-source; those characterizations come from published writeups and leaked-source commentary, not code inspection.*

## 4. Issue catalog — ranked

### Tier 1 — architectural (hard to retrofit, decide direction now)

1. **Fragmentation-hostile geometry model.** One mutable rect per node; multicol/inline/pagination each have private workarounds. Blocks paged media and clean multicol. (§3.3; [RAD_11](../../doc/dev/radiant/RAD_11_Positioned_Float_Multicol_Lists.md))
2. **Single-thread coupling with the escape hatch off.** All work on the UI thread; tiled replay — the designed parallelism seam — is gated off whenever a page has glyphs (`render_output.cpp:309`). ([RAD_12](../../doc/dev/radiant/RAD_12_Paint_IR_Display_List.md))
3. **No computed-style caching or sharing.** Full used-value re-resolution per layout through the giant switch; two color parsers; duplicated inheritance machinery (css-side `style_tree_apply_inheritance` vs Radiant's `inheritable_props[]` loop). ([RAD_02](../../doc/dev/radiant/RAD_02_CSS_Style_Resolution.md))
4. **Text internationalization gap.** No bidi, no complex shaping, no shared shaped buffer between layout and render. ([RAD_06](../../doc/dev/radiant/RAD_06_Inline_and_Text_Layout.md), [RAD_07](../../doc/dev/radiant/RAD_07_Fonts.md))

### Tier 2 — structural debt (fixable in place, compounding cost)

5. **Monolith files.** `resolve_css_style.cpp` (~726k), `layout_table.cpp` (~501k with a ~2900-line `table_auto_layout`), `layout_block.cpp` (~488k), `event.cpp` (~440k), `state_store.cpp` (~338k), `intrinsic_sizing.cpp`'s ~3100-line `measure_element_intrinsic_widths`, `dom_range.cpp` (~4551 lines). Debt is structural, not TODO-marked — grep finds almost no TODO/FIXME in the worst files.
6. **Duplicated representations.** Grid's legacy layer + Taffy layer bridged by a per-pass converting adapter, with a "pure" §11 driver that skips §11.5 and is effectively dead (`grid_sizing_algorithm.hpp:1516`); paint op/payload definitions triple-mirrored across `PaintOp`/`DisplayOp`/`rdt_*` (~3 edit sites per new primitive); a legacy list-bullet render path alongside the `::marker` path (`render_list.cpp:217`). `DomSelection` is now the canonical selection write source, and view teardown is now one table-driven visitor; keep future consolidation work focused on the remaining mirrors.
7. **Intrinsic-sizing height is an estimation, and the width cache is width-only** — height recomputed every call; the header's `IntrinsicSizeCache` is dead code. ([RAD_05](../../doc/dev/radiant/RAD_05_Intrinsic_Sizing.md))
8. **Script/cascade ordering has been normalized.** Initial CSS cascade now runs before document scripts, and script DOM/style mutations trigger a post-script recascade; the legacy scripts-before-cascade order remains opt-in via `RADIANT_SCRIPT_BEFORE_CASCADE=1` while old baselines are audited. ([RAD_21](../../doc/dev/radiant/RAD_21_JS_Scripting_Integration.md))

### Tier 3 — latent bugs & silent failures (cheap to fix, high blast radius)

9. **The zero-field invariant is convention-only.** No `static_assert`; one added field on any `View*` class silently corrupts memory through `unsafe_*`/`static_cast` paths. (`lib/tagged.hpp:184-241`)
10. **Table-height state used to be smuggled through a global workaround.** The file-scope `g_layout_table_height` save/restore path has been removed; table layout now relies on the table content path's final dimensions directly, with targeted table layout tests passing. Keep table-height regressions under normal layout tests instead of reintroducing side-channel state.
11. **Silent fixed caps that drop content.** The first hardening sweep made the content-dropping render z-order arrays and ThorVG/CoreGraphics clip stacks growable, and added `RAD_CAP_*` logs to grid, clip-shape, flex-cache, and float/grid iteration-cap fallbacks. Remaining fixed caps should follow the same rule: either grow, or log before falling back.
12. **Unasserted incremental-relayout invariant.** `layout_height_contribution` staleness silently mis-positions siblings; no debug verification mode.
13. **Duplicated webview hit-test block** in `event.cpp` (~`1029-1051` vs `1053-1075`) — likely copy-paste; z-index not honored in positioned hit-testing (`event.cpp:1113`).
14. **CSS transitions declared but unwired.** `CssTransitionProp`/`ANIM_CSS_TRANSITION` exist; nothing creates them — only `@keyframes` animate. ([RAD_16](../../doc/dev/radiant/RAD_16_Animation_Frame_Scheduling.md))
15. **Magic-number fallbacks standing in for computation.** 366px flex measurement width fallback (`layout_flex_measurement.cpp:661`), tag-based height estimates, `float_width = 100.0f` (`layout_block.cpp:4123`), 300×150 replaced defaults duplicated ~7 places.

### Tier 4 — capability gaps (scope decisions to make explicitly)

16. Clipboard: in-memory + GLFW plain text only; no OS rich-text/image backends. 17. WebDriver: many stub endpoints, `strstr`-based JSON parsing. 18. No Linux/Windows video; webview↔runtime IPC TODO on all platforms. 19. `content:` counters/attr/url, `clip: rect()`, named colors unimplemented in resolution. 20. No capture phase in event dispatch.

## 5. Enhancement proposals

Ordered by leverage (impact ÷ effort), grouped into four phases. Effort scale: S (< a day), M (days), L (weeks), XL (a quarter-scale project).

### Phase A — harden what exists (S/M items, do immediately)

_Status as of 2026-07-06: ✅ = fixed/landed; ◐ = partially landed, follow-up still required._

- ✅ **P1 (S): Compile-time ABI guard for the zero-field invariant.** Add `static_assert(sizeof(ViewBlock) == sizeof(DomElement))` (and for `ViewText`/`ViewTable`/all wrappers) next to the class definitions in `view.hpp`, plus a comment pointing at `lib/tagged.hpp`. Turns Tier-3 issue #9 from silent corruption into a compile error. Zero risk. Landed in `radiant/view.hpp`; the pass also removed `ViewMarker`'s stray overlay fields and routed marker caret geometry through `MarkerProp`.
- ✅ **P2 (M): Root-cause `g_layout_table_height`.** The workaround has known save/restore choreography, which is itself a reproduction recipe: instrument with ASan + a watchpoint on the corrupted field under the layout test suite, find the writer, delete the global. Until then, at minimum document the workaround at all four sites with a shared comment tag so it can't grow new tendrils. The global save/restore path is now deleted; table layout keeps the dimensions written by the table content path directly, and `table_simple` plus `table_014_width_height` pass without the side channel.
- ✅ **P3 (S): Verification mode for incremental relayout.** Debug-build flag: when a child is skipped as clean, *also* lay it out and assert the cached `layout_height_contribution` matches. Run in CI on the layout suite. Catches Tier-3 #12 class bugs permanently. Landed behind `RADIANT_VERIFY_INCREMENTAL_LAYOUT=1` and smoke-tested through an incremental DOM mutation event simulation.
- ✅ **P4 (M): Make silent caps loud, then growable.** One sweep: every fixed-capacity drop site (z-order 256, grid caps, clip depth 8, flex cache, iteration caps) gets a `log_warn` with a distinct prefix; then convert the two content-dropping ones (z-order arrays, clip stack) to arena-allocated growth. The render z-order arrays are growable, ThorVG and CoreGraphics clip stacks grow, flex measurement/original-height scratch storage grows, and grid/clip-shape/float-iteration fallbacks now emit `RAD_CAP_*` logs before falling back.
- ✅ **P5 (S): Delete the duplicated webview hit-test block** in `event.cpp` and honor z-index in positioned hit-testing (or file it explicitly against the paint-order refactor in P11). Current code no longer has the duplicate webview block, and positioned-child hit-testing now walks topmost z-index entries first.
- ✅ **P6 (M): Kill the dead code found by the survey.** The dead "pure" grid §11 driver (or make the adapter call it — either way remove the trap), the dead `IntrinsicSizeCache` header struct, `parse_font_face_rule_OLD` (already fully commented out), the stale `table_auto_layout_algorithm`/`table_fixed_layout_algorithm` header decls, `RDT_VIEW_MATH`. Landed removals: `IntrinsicSizeCache`, `parse_font_face_rule_OLD`, stale table-layout declarations, and `RDT_VIEW_MATH`; the pure grid driver was already absent in the current tree.

### Phase B — performance architecture (the two highest-leverage L items)

- **P7 (L): Computed-style caching & sharing.** Two steps. (1) *Memoize per element*: keep the resolved property groups when `style_version` and inherited inputs are unchanged — the fields (`style_version`, `needs_style_recompute`) already exist on `DomElement`; today they under-deliver because resolution is re-run per layout pass. (2) *Share across elements*: key a style-sharing cache on (tag, class list, cascaded-declaration identity, parent computed hash) the way Blink's style-sharing candidate list works; siblings in lists/tables are the win case for documents. Do step 1 first; measure; step 2 only if profiles justify it. Prerequisite for making `getComputedStyle` cheap and for transitions (P10).
- **P8 (L): Unblock tiled parallel replay for glyph-bearing pages.** The gate exists because glyph rasterization/caching isn't safe to touch from tile workers. Fix by splitting the glyph path the way the rest of the pipeline already works: rasterize/atlas glyphs at *record/lowering* time on the UI thread (the display list already deep-copies into a `ScratchArena`), so replay workers only blit from an immutable atlas. This makes the engine's one designed concurrency seam actually engage on real pages, and it is far cheaper than any layout-parallelism ambition.

### Phase C — capability architecture (L/XL, decide direction now, execute incrementally)

- **P9 (XL): A real fragment model — the paged-media bet.** Do not rebuild the tree; extend the geometry model. Proposal: keep the node's `x/y/w/h` as the *union/primary* box (fast path unchanged for the 99% of unfragmented nodes), and add an optional side-table `FragmentList` (arena-allocated, keyed by node, owned by `ViewTree`) that multicol, paged output, and block-in-inline all share — replacing today's three private mechanisms (`ColumnFragment`, `TextRect`-only chains, block-in-inline special cases) with one. Render walk and hit-testing learn one new concept ("iterate fragments if present"); everything else keeps the fast path. This is LayoutNG's insight scaled to Radiant's architecture: fragments as *layout output*, without giving up the unified tree. Sequence it behind a concrete driver feature — `@page`/print-to-PDF is the natural one for an engine whose PDF backend already exists.
- **P10 (M): Wire CSS transitions.** The runtime scaffolding exists (`CssTransitionProp`, `ANIM_CSS_TRANSITION`, the scheduler). What's missing is the trigger: on computed-style change (delivered by P7's memoization diff), compare old/new values of transitionable properties and create instances. Do after P7, which provides the old-vs-new diff for free.
- **P11 (L): Paint-order/stacking-context cleanup.** Replace the fixed z-order stack arrays with a proper stacking-context sort (this also fixes hit-testing z-order, Tier-3 #13b) and consider generating paint order once and sharing it between render walk and hit-testing so the two can't diverge.
- **P12 (L/XL): Text internationalization, staged.** (1) *Shared shaped buffer*: make layout produce a run/glyph buffer render consumes — removes the duplicated measurement and is a prerequisite for everything after. (2) *Bidi*: integrate a UAX #9 implementation (SheenBidi is small, C, and fits the no-STL rule) at the paragraph level, reordering runs before line-breaking. (3) *Shaping*: optional HarfBuzz behind the existing `lib/font` seam for complex scripts, keeping the current fast path for simple Latin runs — the engine already has exactly the right two-tier structure (`measure_shaped_simple_latin_run` vs per-codepoint) to slot this in. Ship each stage independently.

### Phase D — consolidation (M/L, schedule opportunistically alongside feature work)

- **P13 (L): Monolith decomposition, mechanically.** Split along the seams the design docs already mapped: `resolve_css_style.cpp` by property GROUP banner (13 groups → ~13 files + the dispatch switch); `table_auto_layout` by its own "Step N" phases; `event.cpp` by input domain (mouse/keyboard/IME/scroll/drag); `state_store.cpp` by its ~20 concern sections; `dom_range.cpp` into range-core/mutation/stringify/selection. Rule: no behavior change, one file per PR, `make test-*-baseline` green each step. The RAD docs' source maps are the decomposition spec.
- ✅ **P14 (M): Finish the selection/caret migration.** `DomSelection` / `EditingSelection` are the single write sources; the former legacy-named StateStore write API is deleted, and `CaretState` / `SelectionState` remain only as projection caches refreshed from canonical selection state. Sequenced with the Stage-4B editor work, which is already forcing clarity here.
- **P15 (M): Single-definition paint ops.** Generate the `PaintOp`/`DisplayOp`/`rdt_*` mirrors from one X-macro table so adding a primitive is one edit site, not three.
- ✅ **P16 (M): One teardown walk.** `free_view`, retained reset, detached-subtree release, and tree destroy now share `view_teardown_visit_node`, driven by `VIEW_PROP_TEARDOWN`; new pooled pointers register their release/clear/free hooks in that table instead of joining multiple walks.
- ✅ **P17 (S/M): Spec-ordering fix for scripts.** Initial stylesheet cascade now runs before load-time document scripts; if scripts mutate DOM/classes/inline styles or inject `<style>`, the loader re-collects style sheets, clears stylesheet-origin declarations while preserving JS inline writes, and recascades. Legacy order is behind `RADIANT_SCRIPT_BEFORE_CASCADE=1`.

### Non-goals (explicitly)

- **Process isolation / sandboxing** — wrong cost/benefit for a document engine; instead, state the trust model in the docs and keep the script watchdog.
- **Parallel layout** (Servo-style) — the unified mutable tree makes this a rewrite, and documents rarely need it; P8's parallel *raster* is the better dollar.
- **A separate layout tree** — the unified-tree decision is sound for this engine; P9 deliberately extends it rather than abandoning it.

## 6. Suggested sequencing

| Order | Items | Rationale |
|---|---|---|
| Now (1–2 weeks) | P1, P3, P5, P6, P4-logging | All S/M, all de-risking; no behavior change |
| Next (this quarter) | P2, P7 step 1, P8, P14 | Root-cause the landmine; the two big perf levers; finish the migration already in flight |
| Then | P10, P11, P13 (rolling), P17 | Transitions ride on P7; stacking-context fixes hit-test + render together |
| Strategic (decide, then fund) | P9 (fragments/paged media), P12 (i18n text) | The two Tier-1 architecture bets; each should be driven by an explicit product decision (print/PDF fidelity; non-Latin documents) |

## 7. Closing assessment

Radiant is a coherent, honestly-scoped engine whose core bet — the unified DOM/View tree — is right for what it is: a document renderer, viewer, and editor host on a shared runtime. Its architecture borrows well (Taffy, LayoutNG-style constraint vocabulary, record/replay painting) and its interaction layer is unusually testable. The two genuinely architectural gaps are the fragment model and the single-thread coupling; both have incremental paths (P9, P8) that preserve the unified tree rather than abandoning it. The rest — monoliths, dual representations, silent caps, the table-height landmine — is ordinary structural debt that the detailed-design docs have now mapped precisely enough to retire mechanically. The highest-leverage next actions are the cheap hardening items (P1–P6) and computed-style memoization (P7), which together remove most of the silent-failure risk and the largest recurring CPU cost without touching the engine's shape.
