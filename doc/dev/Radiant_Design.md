# Radiant Engine — Design Summary

> **One document engine, from source to pixels to interaction.** Radiant is Lambda's HTML/CSS layout, rendering, and interaction engine — a full document-presentation and browser engine (~198k LOC under `radiant/`), not just a layout box. Its defining idea: a parsed DOM node **is** its own layout view (no parallel tree), and it runs on the **shared Lambda runtime** (the tagged `Item` value model, the GC, the MIR JIT, `MarkBuilder`, and the input parsers) rather than bridging to it, hosting the LambdaJS engine for page scripts. This document is the concise map of that design — for developers, and for loading into AI-model context. Full details live in the **[RAD_00 detailed-design set](radiant/RAD_00_Overview.md)** (RAD_01–RAD_23); the runtime it sits on is in **[LR_00 (Lambda core)](lambda/LR_00_Overview.md)** and **[JS_00 (LambdaJS)](js/JS_00_Overview.md)**.

---

## 1. The unified DOM/View tree — the substrate ([RAD_01](radiant/RAD_01_View_and_DOM_Model.md))

There is **no separate view tree**. `typedef DomNode View` (`lambda/input/css/dom_node.hpp`): every `DomNode` carries its own geometry (`x/y/width/height`, border-box, all `float`), its `view_type` tag, and the incremental-layout fields; `DomElement` additionally carries the embedded Lambda `Element` and **all** the layout property-group pointers (`font`, `bound`, `in_line`, `blk`, `position`, flex/grid/table item props). The `View*` classes in `view.hpp` (`ViewSpan`/`ViewBlock`/`ViewTable…`) are **method-only wrappers that add zero fields** — enforced by convention ("Do NOT add fields — views share memory with DomElement!") and the grepable `unsafe_*` casts in `lib/tagged.hpp`.

"Building" the view tree is therefore an **in-place tag during layout**: `layout_flow_node` walks the DOM and `set_view` does a `static_cast<View*>(node)` and stamps `view_type` — no allocation. `ViewTree` owns a bump **`Arena`** (nodes) + a **`Pool`** (property groups). Incremental relayout marks `layout_dirty` up the ancestry, skips clean children by reusing their cached `layout_height_contribution`, and `view_pool_reset_retained` swaps the property pool **while keeping the nodes** so `DocState` anchors (bound by monotonic node `id`, not pointer) survive. The editor round-trips caret/selection through the `source_pos_bridge` (`render_map` source-path identity).

---

## 2. CSS resolution — used/computed values only ([RAD_02](radiant/RAD_02_CSS_Style_Resolution.md))

Radiant does **not** parse or cascade CSS. The parser and the cascade (specificity/origin/order, winning declarations) live in `lambda/input/css/` (`StyleTree`/`StyleNode`); an element reaches Radiant with `specified_style` already cascaded. Radiant performs the **used/computed-value** step: `resolve_htm_style.cpp` applies UA defaults + HTML presentational attributes, then `resolve_css_style.cpp` (the single largest file, ~726k) runs a two-pass walk — font properties first (so `em`/`ex`/`ch` metrics exist), a `color` pre-pass (for `currentColor`), the rest, then a hand-rolled inheritance loop — dispatching each property through one ~248-case `resolve_css_property` switch into concrete floats/colors/enums. Computed style is **not one struct**: it is a set of lazily-allocated sub-structs (`FontProp`, `InlineProp`, `BoundaryProp`, `BlockProp`, `FlexProp`, `PositionProp`) on the node.

---

## 3. Layout — one driver, six modes, measure-then-place ([RAD_03](radiant/RAD_03_Layout_Driver_Block_BFC.md)–[RAD_11](radiant/RAD_11_Positioned_Float_Multicol_Lists.md))

- **Driver & block/BFC ([RAD_03](radiant/RAD_03_Layout_Driver_Block_BFC.md)):** `layout_html_doc` → recursive `layout_flow_node` dispatches by `display.outer` into block/inline/flex/grid/table. Block layout is a four-function pipeline (`layout_block` → `_content` → `_inner_content` → `finalize_block_flow`); `BlockContext` unifies block state + the block formatting context + float linked-lists; margin-collapse and CSS §9.5 float avoidance live here.
- **Box model & sizing services ([RAD_04](radiant/RAD_04_Box_Model_Containing_Blocks.md)):** all dimensions are `float` (lint rule 11). A Taffy-style `RunMode`/`SizingMode`/`AvailableSpace` vocabulary unifies real layout with measurement; a 9-slot `LayoutCache` memoizes sizing; `LayoutMeasureScope` snapshots/restores a subtree for side-effect-free measurement; containing-block/percentage resolution and the shared callback-based abs-child driver live here.
- **Intrinsic sizing ([RAD_05](radiant/RAD_05_Intrinsic_Sizing.md)):** min/max/fit-content measured **without committing a layout** (a `ComputeSize` run mode reading `specified_style`); feeds flex, grid, table, and block shrink-to-fit. Width is cached; height is an admitted estimation.
- **Inline & text ([RAD_06](radiant/RAD_06_Inline_and_Text_Layout.md)):** a streaming line-breaker (`layout_text`, single break-cursor rewind on overflow), `Linebox` + `BreakKind` (UAX #14 opportunities), an ASCII-Latin fast path vs per-codepoint glyph loads. **No bidi reordering (UAX #9) and no complex-script/GSUB shaping.**
- **Fonts ([RAD_07](radiant/RAD_07_Fonts.md)):** the real font engine (rasterization, cmap, GPOS kerning, fallback, WOFF2, COLR/CBDT) is **`lib/font/`, not `radiant/`**; Radiant's `font.cpp`/`font_face.cpp` are a thin CSS→engine bridge (`FontProp` → `FontHandle`) + `@font-face` handling.
- **Flex / Grid / Table / Positioned:** flex is a three-file measurement/multipass pipeline implementing the CSS §9 ten-phase algorithm incl. §9.7 flexible-length resolution ([RAD_08](radiant/RAD_08_Flexbox_Layout.md)); grid is a **C+ port of Rust Taffy** (enhanced layer + legacy layer bridged by a per-pass adapter) running the §11 track-sizing algorithm, and grid items reuse flex item layout ([RAD_09](radiant/RAD_09_Grid_Layout.md)); tables keep all mutable grid state in a scratch-arena `TableMetadata` and run a §17.5 `table_auto_layout` orchestrator ([RAD_10](radiant/RAD_10_Table_Layout.md)); relative/sticky/absolute/fixed positioning, floats in the BFC, simplified multi-column fragmentation, list markers, and the CSS counter engine are in [RAD_11](radiant/RAD_11_Positioned_Float_Multicol_Lists.md).

---

## 4. Rendering — two-level retained IR, one walk, many targets ([RAD_12](radiant/RAD_12_Paint_IR_Display_List.md)–[RAD_14](radiant/RAD_14_SVG_Vector_Graph.md))

Rendering is **record → lower → replay**. A shared `render_walk` traverses the laid-out tree once and dispatches through a `RenderBackend` vtable. Painters draw through `rc_*` wrappers into a semantic, backend-neutral **Paint IR** (`PaintOp`/`PaintList`), which lowers to a raster-only **display list** (`DisplayOp`) recorded in a scratch arena and replayed — serially, in **parallel tiles**, or from a **retained cache** for incremental repaint. PDF and SVG export walk the *same* tree and lower the *same* Paint IR directly, bypassing the display list ([RAD_12](radiant/RAD_12_Paint_IR_Display_List.md)). The per-feature painters (background/border/text/image/clip/effects/filter/composite; `render_bound` orders shadow→bg→inset-shadow→border), the raster surface, and PDF export are in [RAD_13](radiant/RAD_13_Render_Walk_Painters.md). Vector graphics go through the immediate-mode `RdtVector` abstraction over a **dual backend** (ThorVG all-platforms / CoreGraphics, selected at compile time); the inline-SVG renderer walks a Radiant-parsed SVG element tree, `render_svg.cpp` is the opposite direction (view tree → SVG text), and a **Dagre-inspired** graph layout emits SVG that re-enters the renderer ([RAD_14](radiant/RAD_14_SVG_Vector_Graph.md)).

---

## 5. Interaction — one event funnel, one UI thread ([RAD_15](radiant/RAD_15_Events_Input.md)–[RAD_19](radiant/RAD_19_Form_Controls.md))

- **Events ([RAD_15](radiant/RAD_15_Events_Input.md)):** a single `handle_event` funnel hit-tests platform input against the view tree, builds a root→target view stack, and fans out to native default actions, the JS bridge, and editing controllers. Dispatch is **stack-down only (no capture phase)**; `event_sim.cpp` is a JSON-driven deterministic-replay + UI-test harness feeding the same funnel.
- **Animation & frame scheduling ([RAD_16](radiant/RAD_16_Animation_Frame_Scheduling.md)):** `frame_clock` is a vsync **wake source**; all animation sampling (CSS `@keyframes`, GIF/Lottie) runs on the UI thread inside the render loop. **CSS transitions are declared but currently unwired.**
- **Interaction state ([RAD_17](radiant/RAD_17_Interaction_State.md)):** `DocState` is the per-document mutable state (focus/hover/active/selection/IME/drag/overlays/scroll/dirty). The "state machine" holds **no state** — it derives FSM states on demand and asserts ~21 invariants at a single per-event **cascade checkpoint** (begin → settle → end); a Mark-tree dump gives diffable snapshots.
- **Editing & forms ([RAD_18](radiant/RAD_18_Editing_Selection_Ranges.md), [RAD_19](radiant/RAD_19_Form_Controls.md)):** spec `DomRange`/`DomSelection` (UTF-16 at the API boundary, UTF-8 internally, single-range), 50+ WHATWG `inputType` intents. For rich (`contenteditable`) hosts Radiant fires `beforeinput` to script and **returns without native mutation** — the native rich-text engine is **retired** in favor of the JS editor (Stage 4B seam). **Form controls stay native** (value/selection IDL, undo ring, IME preedit, validation).

---

## 6. Shell & integration ([RAD_20](radiant/RAD_20_Application_Shell_Browsing.md)–[RAD_23](radiant/RAD_23_WebDriver_Automation.md))

A single global `UiContext` owns the GLFW window, the `ImageSurface`, fonts, the document, the browsing session, and the webview manager. A `RadiantFrameClock`-paced main loop on one thread pumps GLFW + libuv + the JS event loop, drains network completions, ticks animation/caret/video, polls dirty webviews, and repaints. Every input format — HTML, markdown, XML, SVG, images, source text, PDF, `.ls`, LaTeX — normalizes into a **`DomDocument`** so layout/render/interaction are shared regardless of source ([RAD_20](radiant/RAD_20_Application_Shell_Browsing.md)). Page scripts run through `script_runner` (browser-global preamble + inline/external sources → LambdaJS MIR under an `alarm`+`sigsetjmp` signal watchdog, **before CSS cascade**; cross-refs [JS_13](js/JS_13_Web_DOM.md)) ([RAD_21](radiant/RAD_21_JS_Scripting_Integration.md)). Media players (GIF/Lottie; video is macOS AVFoundation only) and embedded native webviews (WKWebView/WebKitGTK; runtime IPC is TODO) composite into the surface via a shared frame-ready wake pattern ([RAD_22](radiant/RAD_22_Media_Webview.md)). A W3C **WebDriver** server locates elements via Radiant's CSS matcher over the view tree and drives them with synthesized events ([RAD_23](radiant/RAD_23_WebDriver_Automation.md)).

---

## 7. Invariants & gotchas (the contract)

- **Layout dimensions are `float`, always** (CLAUDE.md rule 11) — never `int` for position/size in `radiant/`; mark a genuine cast `// INT_CAST_OK`.
- **Views add methods, never fields** — the `View*` classes must stay ABI-identical to `DomElement`/`DomText`, or the `static_cast`/`unsafe_*` casts corrupt sibling storage.
- **Computed style is recomputed, not stored** — Radiant keeps `specified_style` (cascaded input) and materializes computed values into property structs each layout; the cascade seam is `lambda/input/css/`, not `radiant/`.
- **Node identity is the monotonic `id`, not the pointer** — `DocState`/`ViewState` anchors bind by `id` so they survive `view_pool_reset_retained`; data-buffer pointers that must outlive a pool swap use `retained_fields.hpp` ownership refs.
- **The DOM node is the render tree** — mutations from JS/editor directly touch what layout measured; mark `layout_dirty` and relayout, don't rebuild.
- **Text is not fully internationalized** — no bidi reordering, no complex-script shaping; RTL only affects alignment.
- **`log_*` only** (`lib/log.h` → `./log.txt`), `./temp/` for scratch, C+ convention (no `std::` containers — use `lib/` `Str`/`ArrayList`/`HashMap`).

---

## 8. Entry points & where to start

| Command | Path exercised |
|---|---|
| `./lambda.exe layout page.html` | load → DOM → CSS resolve → layout → view-tree dump |
| `./lambda.exe render page.html -o out.svg` | layout → paint IR → raster/PDF/SVG export |
| `./lambda.exe view page.html` | layout + render + interaction in a window (scripts run on the MIR interpreter) |
| `./lambda.exe webdriver` | W3C WebDriver automation server |
| `make layout suite=baseline` / `make test-radiant-baseline` | layout regression + Radiant core tests |

| Area | Start here |
|---|---|
| View/DOM model & memory | `radiant/view.hpp`, `radiant/view_pool.cpp`, `lib/tagged.hpp` ([RAD_01](radiant/RAD_01_View_and_DOM_Model.md)) |
| CSS computed style | `radiant/resolve_css_style.cpp`, `radiant/resolve_htm_style.cpp` ([RAD_02](radiant/RAD_02_CSS_Style_Resolution.md)) |
| Layout core & modes | `radiant/layout.cpp`, `radiant/layout_block.cpp`, `radiant/layout_{flex,grid,table}.cpp` ([RAD_03](radiant/RAD_03_Layout_Driver_Block_BFC.md)–[RAD_11](radiant/RAD_11_Positioned_Float_Multicol_Lists.md)) |
| Rendering | `radiant/paint_ir.cpp`, `radiant/display_list.*`, `radiant/render_*.cpp` ([RAD_12](radiant/RAD_12_Paint_IR_Display_List.md)–[RAD_14](radiant/RAD_14_SVG_Vector_Graph.md)) |
| Events / editing / state | `radiant/event.cpp`, `radiant/dom_range.cpp`, `radiant/state_store.cpp` ([RAD_15](radiant/RAD_15_Events_Input.md)–[RAD_19](radiant/RAD_19_Form_Controls.md)) |
| Shell / loading / scripting | `radiant/window.cpp`, `radiant/cmd_layout.cpp`, `radiant/script_runner.cpp` ([RAD_20](radiant/RAD_20_Application_Shell_Browsing.md)–[RAD_23](radiant/RAD_23_WebDriver_Automation.md)) |

---

## 9. Maturity in one paragraph

Layout coverage is broad and browser-shaped — block, inline, flex, grid, table, positioned, floats, multi-column, lists — with the notable gaps being text-level internationalization (no bidi, no complex shaping) and "simplified" multi-column fragmentation. Rendering supports window/PNG/PDF/SVG with tiled and incremental repaint. The interaction stack is real (events, editing, state, WebDriver) but mid-migration in places: the rich-text editor is moving C++→JS at the `beforeinput` seam, CSS transitions are unwired, WebDriver has stub endpoints, and there is no Linux/Windows video. The recurring structural debt is a handful of very large files (`resolve_css_style.cpp`, `layout_table.cpp`, `layout_block.cpp`, `event.cpp`, `state_store.cpp`), duplicated representations (grid's legacy/Taffy layers, canonical vs legacy selection, triple-mirrored paint ops), and silent fixed-capacity caps. Each detail doc's **Known Issues** section is the grounded, per-area catalog; the synthesized view is in [RAD_00 §6](radiant/RAD_00_Overview.md).
