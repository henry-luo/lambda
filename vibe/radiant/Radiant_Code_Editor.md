# Radiant Code Editor — CodePad: Script-Driven Source Editing, Highlighting, and Live Preview

**Date:** 2026-08-24
**Status:** Proposed — design only, no implementation started
**Scope:** A minimal VS-Code-kind source editor under Radiant/Lambda: (1) plaintext code editing of any size file; (2) source editing of the markup and script formats Lambda parses (`.md`, `.html`, `.ls`, `.json`, `.css`, `.js`, `.yaml`, `.toml`, `.xml`, `.tex`, …); (3) syntax highlighting; (4) source ↔ rendered view sync for renderable formats (markdown, HTML, wiki, …). Defines the editor architecture, the highlighting engine, the preview/sync pipeline, the small native (C++) work list, and the milestone plan. Out of scope: LSP, extensions/plugins, tabs/splits beyond the preview pane, minimap, folding, git integration, multi-cursor (v1), and two-way editing of the rendered view (§7.5).
**Builds on:**

- [Radiant_Editor_Stage4B.md](../editing/Radiant_Editor_Stage4B.md) — the three-layer model; script-driven editing on the `beforeinput` seam; co-equal JS + Lambda editors
- [Radiant_Design_Editable.md](Radiant_Design_Editable.md) — the common editing transaction gate (Implemented — first gate verified); CodeMirror 6 as the code-editor probe
- [RAD_15 — Events and Input](../../doc/dev/radiant/RAD_15_Events_Input.md), [RAD_18 — Editing, Selection and DOM Ranges](../../doc/dev/radiant/RAD_18_Editing_Selection_Ranges.md), [RAD_20 — Application Shell](../../doc/dev/radiant/RAD_20_Application_Shell_Browsing.md), [RAD_21 — JS Scripting Integration](../../doc/dev/radiant/RAD_21_JS_Scripting_Integration.md)
- [RAD_06 — Inline and Text Layout](../../doc/dev/radiant/RAD_06_Inline_and_Text_Layout.md) — text-layout constraints that force virtualization
- [JS_13 — Web DOM](../../doc/dev/js/JS_13_Web_DOM.md) — LambdaJS DOM surface the editor runs on
- `doc/Markup_Formats_Support.md`, `doc/Doc_Schema.md` — the Mark document schema the preview pipeline produces

---

## 0. TL;DR

Build **CodePad**, a small script-driven code editor, as a **third member of the Stage 4B editor family** — a plain-DOM component on the `beforeinput` seam, sibling to the rich-text editor (`test/editor-js/`), written in the same portable TS style so it can later be twinned to a Lambda `.ls` implementation by the established oracle-bridge method. It is *not* a native C++ editor (Stage 4B retired native editing behavior) and *not* an adoption of CodeMirror 6 (kept as a compat probe and behavioral oracle only, §3 CED1).

Four load-bearing choices:

1. **Script-owned line buffer; the DOM is only a render target** (CED2). Edits are `{from, to, insert}` deltas with inverse-delta undo, mirroring the rich editor's step/transaction shape.
2. **Virtualized viewport — mandatory, not an optimization** (CED3). Radiant's `layout_text` aborts after 500 line-wraps per text node and has no intra-text-node incremental layout, so "one big `<pre contenteditable>`" cannot work. Only visible lines (~60 divs) ever exist in the DOM; per-keystroke cost is independent of file size.
3. **Highlighting = tiny per-language line tokenizers** (CM5-style stream modes), hand-scanned, no regex, no new tree-sitter grammar vendoring (CED5). Each mode is ~100–300 LOC and ports cleanly to Lambda.
4. **Preview = the existing C++ batch parsers** through a small script-callable convert bridge; **position sync = opt-in source byte-spans attached by the parsers**, copying the mechanism `input-graph.cpp` already uses (CED6, CED7). The only substantive native feature in the whole design is the source-span attribution.

Milestones: M0 plaintext editor + `lambda.exe edit` → M1 highlighting → M2 live preview → M3 position sync → M4 polish + Lambda twin (§11).

---

## 1. Goals and non-goals

**Goals**

- G1 — A minimal VS-Code-kind editor: gutter + line numbers, selection, undo/redo, find/replace (M4), keybindings, load/save, usable on files far beyond what Radiant can lay out as a single text node today.
- G2 — Source editing of the formats Lambda supports. Editing works for *every* text format (it is plaintext underneath); highlighting and preview light up per language.
- G3 — Syntax highlighting with a pluggable per-language mode registry and CSS theming (light/dark).
- G4 — Source ↔ rendered sync for renderable formats: live preview pane, scroll sync, click-to-jump in both directions, caret → rendered-block highlight.

**Non-goals (v1)**

- Multi-cursor (blocked on single-range native selection, §5.3; needs an own-drawn selection layer — deferred).
- Two-way editing of the preview (rich-editor + formatter territory, §7.5).
- LSP/diagnostics, extensions, workspace/file tree, tabs (the shell is single-window single-document per RAD_20 §8.4).
- Soft wrap in M0 (horizontal scroll first; wrap needs per-line height measurement, M4).
- Replacing the native `<input>`/`<textarea>` path — form controls stay native per Stage 4B.

---

## 2. Grounding — what exists today

Facts below were verified against the tree on 2026-08-24; `file:line` cites drift, symbol names are the anchor.

### 2.1 The editing seam is decided and proven

Stage 4B is implemented: the C++ substrate owns `contenteditable` classification, caret/selection + geometry (`radiant/dom_range.cpp`, `radiant/editing_geometry.cpp`), event generation and the single transaction gate (`editing_run_contenteditable_transaction`, `radiant/editing_dispatch.cpp`), and caret/selection painting; all editing *logic* lives in script behind cancelable `beforeinput`. The native rich-text engine is deleted. The plain-DOM JS editor passes 1931/1931 tests under `lambda.exe js` and its UI fixtures under headless `lambda.exe view --event-file` (`vibe/editing/Stage4C_Parity_Report.md`). `contenteditable="plaintext-only"` is first-class: `EDIT_MODE_PLAINTEXT_ONLY` normalizes paste payloads to `text/plain` and rewrites `insertParagraph` → `insertLineBreak` (`radiant/editing_dispatch.cpp:65-89`).

Gate semantics CodePad must respect (verified in `editing_dispatch.cpp`): a listener must `preventDefault()` **before** mutating the host subtree — an uncanceled mutation is recorded as a contract violation and the action + `input` event are both skipped; `format*`/`selectAll`/`historyUndo`-class intents never fire as `beforeinput` at all (bind them on `keydown`, as the rich editor already does for Cmd-Z).

### 2.2 CodeMirror 6 runs under Radiant — as a probe, not a product

Real CM6 (`@codemirror/state` 6.7.1 / `view` 6.43.6 / `commands` 6.10.4) runs headlessly under Radiant with all 47 capabilities in `test/editable-editors/capability-manifest.json` implemented. This proves the substrate (beforeinput + `inputType` + `getTargetRanges`, Selection/Range, IME composition incl. cancel, clipboard events, MutationObserver, rAF). But the tested config is a tiny single-line doc: CM6's virtualization, decorations/widgets, tooltips, coordinate mapping, and heightmap are **explicitly excluded** (`test/editable-editors/UPSTREAM_TEST_AUDIT.md`), `@codemirror/language`/`@lezer` are not even pinned, and the 610 KB bundle runs interpreter-only under `view` (§2.5).

### 2.3 Radiant layout constraints — why virtualization is mandatory

- 🚩 `layout_text` aborts after **500** wrap/newline iterations per text node (`layout_text_iterations` guard, `radiant/layout_text.cpp:3121`); a preserved newline under `white-space:pre` takes that path, so a `<pre>` text node past ~500 lines silently truncates. Today's `load_text_doc` source viewer (`radiant/cmd_layout.cpp`) emits the whole file as one `<pre>` text node and therefore hits it. *(Control flow verified by reading; not yet reproduced empirically — M0 should land a repro first, §8 item 4.)*
- `MAX_LAYOUT_NODES = 50000` (`radiant/layout.hpp:98-103`) caps total layout nodes — rules out one-element-per-token for whole files.
- Incremental layout skips only clean **block children** (Phase-16 skip, `radiant/layout_block.cpp`); there is no intra-text-node incrementality — editing one line of a shared text node relays the whole node.
- In our favor: painting is already viewport-culled at block level *and* per `TextRect` (`render_block_viewport_misses`, `render_text_rect_misses_clip`); scrolling is repaint-only with position in the StateStore; the element `scroll` event fires and `scrollTop`/`scrollLeft` are r/w from JS. Everything a script virtualizer needs exists.
- No monospace uniform-advance fast path exists; there *is* an ASCII direct-mapped advance table (`lib/font/font_glyph.c`, Font5 §4.2) and `OffscreenCanvas.measureText` is backed by the real font engine — good enough for column↔x math in script.

### 2.4 Highlighting landscape

- The tree-sitter **runtime + full query API are already linked into `lambda.exe`** (nm-verified: `ts_query_new`, `ts_query_cursor_set_byte_range`, …) with grammars javascript, typescript, latex, latex-math compiled in; bash/python/ruby are vendored but unlinked; `tree-sitter-lambda` links only into the isolated `lambda-cst` verifier. Upstream `queries/highlights.scm` files exist for bash/js(+jsx)/python/ruby. But the formats Lambda cares most about (markdown, HTML, CSS, JSON, YAML, TOML, Lambda itself) have **no vendored grammar**, and each is a multi-MB `parser.c` vendored dep.
- highlight.js v11.9 loads under LambdaJS but emits **zero token spans** — the golden test `test/js/hljs_highlight.txt` records every `<span` assertion false; root cause is regex-engine divergence (lookaheads/backreferences silently degraded). Regex-heavy tokenizers are a trap under this runtime.
- There is no highlighting anywhere in the engine today. The markdown parser already emits a `language` attribute on `<code type="block">` (`lambda/input/markup/block/block_code.cpp`, `doc/Doc_Schema.md` §code) which flows into the DOM and is consumed by nothing.
- The complete CM6 view source (heightmap, viewstate, docview, gutter, decoration) is vendored read-only under `ref/codemirror-view/` as reference material.

### 2.5 LambdaJS capability and performance posture

- ES2024-level language (40,288 test262 passing); classes, generators, async, Map/Set, destructuring — editor-style TS compiles and runs naturally. `<script type="module">` is **not executed** by the document script runner — ship a classic IIFE bundle (established pattern: `test/editor-js/tools/build-dom-page.mjs`).
- DOM surface is broad (JS_13): createElement/innerHTML (Radiant HTML5 fragment parser), classList/style/dataset, full Selection/Range, real MutationObserver/ResizeObserver/IntersectionObserver, rAF + timers with a virtual clock for headless fixtures, clipboard events + `DataTransfer`, `getBoundingClientRect`/`getClientRects`, `elementFromPoint`, scroll APIs.
- Known gaps to design around: **single-range selection** (`DOM_SELECTION_MAX_RANGES == 1` — extra `addRange` silently ignored); **no on-read layout flush** (measure in rAF); no `caretRangeFromPoint` (a Lambda-specific `__lambda_boundary_from_point` exists; CodePad avoids needing either, §5.4); no Worker/requestIdleCallback (schedule background tokenization on timers); JS *property* sets on JS-created elements don't reflect to content attributes (use `setAttribute` — Stage 4B gotcha).
- All document JS under `layout`/`render`/`view` runs on the **MIR interpreter** (`default_render_cmd_to_interp()` sets `g_js_force_document_interp`, `lambda/main.cpp:650-658`) — roughly 1.65× slower than JIT on hot loops, and `js_dom` property/listener/wrapper lookups are linear scans. No DOM-heavy benchmark exists yet; CodePad's virtualization keeps per-keystroke work O(viewport), and M0 lands a stress fixture before anything else (§10, §12).

### 2.6 Parse/format pipeline and the missing link

- Input: ~28 text formats parse to Mark trees via `input_from_source_n` (`lambda/input/input.cpp`). Markdown parses **directly to Mark elements with HTML tag names** (`doc/Markup_Formats_Support.md` §element mapping) — there is no md→HTML text step — and both markdown and HTML converge on the same DOM builder, `build_dom_tree_from_element` (`lambda/input/css/dom_element.cpp`), in `load_markdown_doc` / `load_html_doc` (`radiant/cmd_layout.cpp`).
- Output: `format_data` (`lambda/format/format.cpp:161`) serializes Mark back to html, markdown, rst, org, wiki, textile, xml, json, yaml, toml, css, latex, …; round-trips are covered by `test/test_input_roundtrip_gtest.cpp` and siblings.
- **No parser attaches source positions for markdown/HTML.** Three partial mechanisms exist: (a) `SourceTracker` (`lambda/input/source_tracker.hpp`) tracks `{offset, line, col}` inside `MarkupParser` but feeds only error reporting; (b) HTML5 has a dormant line-only `__source_line` chain (`html5_tokenizer.cpp` → `eb.attr("__source_line", …)` → `DomElement::source_line`), disabled at every production call site; (c) `radiant/source_pos_bridge.cpp` maps **tree paths** (the rich editor's coordinate space), not file offsets, and only for `apply()`-rendered documents. The precedent to copy is `graph_set_source_span` (`lambda/input/input-graph.cpp:8`), which stores `source-start`/`source-end`/`source-line`/`source-column` as ordinary Mark attributes.
- All parsers are batch-only; no incremental/dirty-region re-parse exists. `DomElement ↔ Mark Element` linkage does exist on every load (`element_dom_map`, `dom_element_render_source`).

---

## 3. Decision ledger

| ID | Decision | Rationale / alternative rejected |
|----|----------|----------------------------------|
| **CED1** | CodePad is a **script-driven plain-DOM editor on the Stage 4B seam** — a third member of the editor family beside the rich-text JS editor and its Lambda twin. | A native C++ editor would reverse the Stage 4B retirement of native editing behavior. Adopting CM6 as the product is rejected: its virtualization/decoration/highlighting surface is untested under Radiant (§2.2), it is 610 KB of interpreter-run third-party JS, and it cannot be twinned to a Lambda `.ls` port. CM6 stays as compat probe + behavioral oracle. |
| **CED2** | **Script-owned line buffer is the source of truth; the DOM is a render target.** Edits are `{from:{line,col}, to:{line,col}, insert:[lines]}` deltas; undo = inverse deltas with time/word grouping + selection snapshots; monotonic doc version. | Same model-first discipline as the rich editor (its `EditorViewDom` never syncs DOM→model on input). A DOM-as-truth contenteditable model breaks under virtualization and under Radiant's attribute/property split. A rope is deliberately skipped: a 100 K-line JS string array is fine when only ~60 lines touch the DOM; simplest-design-first. |
| **CED3** | **Virtualized viewport, mandatory.** One scroll container, a height spacer sized `lineCount × lineHeight`, a rendered window of one `<div>` per visible line (+ overscan), gutter as a parallel virtualized column. Fixed `line-height`, monospace, no wrap in v1. | Forced by the 500-wrap cap, the 50 K node cap, and whole-text-node relayout (§2.3). Also what makes keystroke cost file-size-independent on an interpreter-only runtime. |
| **CED4** | **Model selection is truth; the native single DOM range is its projection**, clamped to the rendered window. Native caret painting + IME anchoring are kept. Multi-cursor and an own-drawn selection layer are deferred. | `DOM_SELECTION_MAX_RANGES == 1` makes native multi-cursor impossible anyway; native caret/IME geometry is substantial free value (Layer A renders it). CM6 uses the same clamped-projection trick. |
| **CED5** | **Highlighting = per-language line-state stream tokenizers** in script: `mode(lineText, startState) → {tokens, endState}`; hand-scanned, **no regex**; tokenize only viewport + dirty lines, chaining cached line end-states. Tree-sitter is a **v2 assist only** for grammars already linked into `lambda.exe` (JS/TS/LaTeX); no new grammar vendoring without a separate decision. | Regex tokenizers are proven broken under this runtime (hljs golden, §2.4). Vendoring 7+ multi-MB grammars contradicts minimal-LOC and vendor policy (CLAUDE.md rule 16). Stream modes are ~100–300 LOC each, error-tolerant by construction, O(visible) per keystroke, and portable to Lambda. |
| **CED6** | **Preview = existing batch C++ parsers** through a new script-callable convert bridge (`input_from_source_n` + `format_data`, same machinery as `lambda.exe convert`); v1 renders via `preview.innerHTML` on a ~200 ms idle debounce. Full-document re-parse is accepted for v1; incremental block re-parse is a later optimization, not a prerequisite. | Reuses the entire tested input/format pipeline instead of duplicating renderers in script (rule 13). The innerHTML setter already runs the Radiant HTML5 fragment parser. `[TIMING]` instrumentation already exists in `load_markdown_doc` to validate the latency budget. |
| **CED7** | **Position sync via opt-in source byte-spans attached by the parsers** — `source-start`/`source-end` attributes per the `graph_set_source_span` precedent, emitted from `MarkupParser`'s existing `SourceTracker` (and the HTML5 tokenizer) behind a parse option; they flow to the preview DOM as `data-source-*` through the normal attribute path. The dormant `__source_line` chain is subsumed. | The one genuinely missing capability (§2.6). Attributes ride the existing Mark→DOM pipeline unchanged; opt-in keeps data-processing pipelines byte-identical. The tree-path `source_pos_bridge` is the wrong coordinate space and `apply()`-only. |
| **CED8** | **Two-way editing of the preview is out of scope** for this design. The composition path when wanted later: preview hosts the Stage 4B rich editor; its doc serializes via `format_markdown`/`format_html`; a span-preserving diff patches the source buffer. | Round-trip lossiness and diff design are a separate project; goal 4 (sync) does not require it. The formatter round-trip tests already de-risk the serialize leg. |
| **CED9** | Product surface is **`lambda.exe edit <file>`**: loads a bundled single-file classic-IIFE editor app (the `editor-dom.html` pattern), injects file content, exposes a **gated save binding**. Language from extension; formats without a renderable form simply get no preview pane. This path also supersedes `load_text_doc`'s truncating viewer. | Matches the shell's single-window model and the established bundle/build pipeline; no new shell machinery needed. |
| **CED10** | CodePad is written in the **portable TS subset** used by the rich editor (pure modules, no React, standards-only DOM) so the **Lambda `.ls` twin** can be produced later by the established oracle-bridge method (`export-lambda-oracle.ts` pattern). The twin is a deliverable of M4, not v1. | Keeps the Stage 4B co-equal-runtimes goal honest without paying the double-implementation cost up front. |

---

## 4. Architecture

```
┌────────────────────────── editor app document (HTML + classic-IIFE bundle) ──────────────────────────┐
│  ┌── CodePad pane ─────────────────────────┐    ┌── Preview pane (renderable formats only) ───────┐  │
│  │ gutter  │ virtualized line viewport     │    │ Radiant-rendered subtree via convert bridge     │  │
│  │ (lines) │ contenteditable=plaintext-only│    │ (read-only; carries data-source-start/end)      │  │
│  └─────────┴───────────────────────────────┘    └─────────────────────────────────────────────────┘  │
│  script (Layer B): buffer · deltas · undo · selection model · tokenizer registry ·                   │
│                    virtualizer · keymap · sync controller · find/replace (M4)                        │
└──────────────────────────────────────────────────────────────────────────────────────────────────────┘
     ▲ beforeinput / keydown / composition / scroll / Selection API   (existing Layer A, unchanged)
     ▼ new native bridges (small): convert(text, from, to) · file save · parser source-spans (opt-in)
              C++ substrate (Stage 4B Layer A)  +  lambda/input · lambda/format (existing)
```

Mapping onto Stage 4B's layers: **Layer A is used unchanged** (classification, gate, selection/caret + painting, IME, clipboard normalization). **Layer B is CodePad** — a different document model (flat text instead of a Mark-schema doc) but the same contract: preventDefault, edit own model, reconcile DOM, write caret via the Selection API. The three native bridges in §8 are Layer-A-adjacent utilities, not editing behavior; no native editing logic returns.

Source layout (mirrors `test/editor-js/`): `test/codepad/src/{model,view,modes,sync}/`, `demo/`, `tools/` (build + fixture runners), bundle → `test/html/codepad.html`.

---

## 5. Core editor design

### 5.1 Buffer, deltas, history

- `Buffer = { lines: string[], version: int }` plus a parallel per-line metadata array `{ tokState: ModeState | null }` (later: `wrapHeight`). Cols are UTF-16 code units, matching `DomBoundary` semantics at the seam.
- A **transaction** = one or more deltas + selection-after; applying returns the inverse for the undo stack. Grouping: consecutive single-char inserts merge until a word boundary / 500 ms gap / selection jump — same feel as the rich editor's history.
- Position mapping through deltas (`mapPos(pos, delta)`) serves search marks and decorations; deliberately the same shape as the rich editor's step mapping so the Lambda twin reuses known ground.
- Guardrails: lines longer than 10 K chars are stored fine but tokenized/rendered plain past the cap; file load normalizes line endings and records them for save.

### 5.2 Virtualized view and gutter

- Scroll container (`overflow:auto`) → spacer div height `lineCount × lineHeight` → absolutely positioned line window at `topLine × lineHeight`. On `scroll`: recompute `[topLine, bottomLine]`, patch only entering/leaving line divs (recycled from a pool). On edit: re-render only dirty visible lines; spacer height changes only when the line count changes.
- One `<div class="cp-line">` per line; token runs as `<span class="tok-*">` with adjacent same-class runs merged; attributes set via `setAttribute` (Radiant property/attribute gotcha). Gutter numbers virtualize identically and right-align by measured digit advance.
- Fixed line height (explicit `line-height`, single monospace family) keeps scroll↔line math arithmetic and keeps every line div its own text node — safely inside the 500-wrap cap and the Phase-16 block-child incremental-layout fast path.
- Measurement discipline: never read geometry after a mutation in the same turn (no on-read flush) — all measurement (char advance, gutter width, viewport height) happens in rAF, cached, and re-validated on `resize`/font change via ResizeObserver.

### 5.3 Input seam, selection, IME

- Host is `contenteditable="plaintext-only"`. The controller preventDefaults **every** `beforeinput` (before touching the DOM — gate contract, §2.1), maps `inputType`+`data`+`getTargetRanges()` → deltas via an `intent-from-input-event` twin, applies to the buffer, reconciles dirty lines, projects selection.
- `keydown` owns navigation (arrows/word/line/page — model-computed, not native, since native rich navigation would fight the model), undo/redo (Cmd/Ctrl-Z — `historyUndo` events are not relied on, per the rich editor), tab/shift-tab indent, Home/End, select-all (model-level; DOM projection clamped to the window).
- **Selection projection:** if both endpoints are within rendered lines, set the real DOM range (native caret + highlight paint); if an endpoint is outside, clamp the DOM range to the window edge and let the model keep truth. Own-drawn selection backgrounds for off-window correctness come with the M4 selection layer, not v1.
- **IME:** composition happens inside a single line's text node — exactly the single-text-run case the native `dom-compat` action supports. Rule (from CM6): the composing line is not reconciled until `compositionend`; the commit arrives as `insertText`/`insertCompositionText` through the normal delta path.
- **Clipboard:** copy/cut build `text/plain` from the model selection; paste arrives pre-normalized to plain text by the plaintext-only gate. Cross-process rich clipboard is out of scope (substrate limitation, RAD_18 §10.3).

### 5.4 Mouse and geometry

- Line from y-arithmetic (`(y + scrollTop - padTop) / lineHeight`); column from prefix-width binary search using `OffscreenCanvas.measureText` with a per-(font,size) advance cache — no native hit-testing, no `caretRangeFromPoint` needed. Handles tabs (CSS `tab-size` mirrored in the measurer) and wide/CJK glyphs via real measurement rather than a fixed-advance assumption.
- Drag-select, double-click word, triple-click line — all model operations reusing the same mapping.

---

## 6. Syntax highlighting

### 6.1 Engine

- Mode interface: `mode.startState() → S`, `mode.token(line: string, state: S) → { tokens: {len, cls}[], endState: S }`, plus cheap `stateEq` for convergence checks. States are small value objects (e.g. `{inString: char|null, inComment: bool, fenceLang: string|null, depth: int}`).
- Scheduling: line N's cached `endState` seeds N+1. On an edit at line k, invalidate states from k; re-tokenize forward **only as far as needed**: eagerly through the viewport, then continue in idle timer slices until the new state converges with the old cache or EOF. Scrolling into un-tokenized territory chains forward from the nearest cached state. Everything is O(viewport + changed prefix) per interaction.
- Rendering: tokens become merged class spans at line render time; theme is plain CSS (`.tok-keyword`, `.tok-string`, `.tok-comment`, `.tok-number`, `.tok-tag`, `.tok-attr`, `.tok-heading`, `.tok-link`, …) with light/dark variants — Radiant's CSS engine does all styling.

### 6.2 Language set

- **M1 modes:** `lambda`, `markdown` (host mode: fenced code delegates to the registry by the fence's language id, state carries the inner mode's state), `json`, `html`/`xml` (shared core), `css`, `javascript`, `yaml`, `toml`. **M4:** `latex`, `rst`, `org`, `ini`, `csv`, `wiki`. Each is a standalone ~100–300 LOC file with a golden token-stream test.
- All modes are hand-scanned character loops — no regex (CED5) — and written in the portable subset (CED10).

### 6.3 Synergies

- **Static fence highlighting:** the same registry can colorize `<code type="block" language=…>` in any rendered markdown — first in CodePad's own preview pane (script pass over the preview subtree), later as a general `lambda view` enhancement. This finally consumes the `language` attribute the parser has always emitted.
- **Find/replace decorations (M4):** match highlighting reuses the decoration concept already modeled in `lambda/package/editor/mod_decorations.ls` / the JS editor — as extra classed spans merged into line rendering, positions mapped through deltas.

### 6.4 Tree-sitter v2 criteria

Only if a mode proves inadequate for a language whose grammar is **already linked** (JS/TS/LaTeX): expose a minimal native binding over the existing query API (`ts_query_cursor_set_byte_range` enables viewport-bounded capture runs) driven by the vendored `highlights.scm` files. Entry criteria: a concrete correctness gap a stream mode can't close, measured binding cost, and no new grammar vendoring. Until then, tree-sitter stays untouched (rule 16 posture).

---

## 7. Preview and source ↔ rendered sync

Three stages, each independently shippable.

### 7.1 Stage 1 — live preview (M2, no native parser work)

- Split pane appears for formats with a renderable form (markdown, html, wiki, rst, org, textile; xml via its stylesheet path). On idle (~200 ms after last edit, coalesced), call the convert bridge: buffer text → `input_from_source_n(type)` → Mark → `format_data("html")` → `preview.innerHTML` (Radiant HTML5 fragment parser). Preview container is `contenteditable=false`, its own scroll context.
- Latency budget: whole-README-scale markdown parses in the existing C++ pipeline; validate with the `[TIMING]` instrumentation over `test/markdown/` corpora during M2 (no documented ms/MB numbers exist — measure, don't assume). If large docs breach budget, the debounce widens adaptively before any incremental-parse work is considered.
- Known v1 degradations, accepted: math `<math>` elements render only as far as `format_html` carries them (the `load_markdown_doc` math pre-pass is not in this path); big code fences in the preview can hit the 500-wrap cap until §8 item 4 lands. v2 option: a parse-to-DOM-subtree bridge reusing `build_dom_tree_from_element` + the math pass, skipping the HTML string round-trip.

### 7.2 Stage 2 — source spans (M3, the native feature)

- Add opt-in span attribution to the markup engine and HTML5 parser: when `Input`/parse options request it, block-level (and selected inline) elements get `source-start`/`source-end` **byte offsets into the original source**, captured from `SourceTracker` *before* normalization (`graph_set_source_span` precedent; the dormant `__source_line` chain is retired into this). Off by default; `ui_mode`/editor paths turn it on, so data-processing output stays byte-identical.
- Spans ride the existing attribute path through Mark → `format_html` → `data-source-start/end` in the preview DOM (or directly through `build_dom_tree_from_element` in the v2 subtree bridge). Golden tests per format assert span→substring fidelity against raw source.

### 7.3 Stage 3 — sync behaviors (M3, all script)

- **Editor → preview scroll:** top visible line → byte offset (line-start offsets are maintained incrementally in the buffer) → smallest enclosing `[data-source-start,end]` element → align preview scroll with proportional interpolation inside the block.
- **Preview → editor:** click resolves the nearest span ancestor → `source-start` → editor caret + centered scroll. **Caret → preview highlight:** on caret line change, outline the corresponding preview block (class toggle).
- Mapping is tolerant by construction: unmapped regions (spans are block-granular) interpolate between neighboring spans; a stale preview (mid-debounce) suspends sync until the next render lands with the matching doc version.

### 7.4 Later — incremental re-parse

Markdown is block-structured: a future optimization diffs top-level block ranges (via the spans) and re-parses only changed blocks into fragments, splicing the preview subtree. Explicitly *not* scheduled — batch parse must first be shown too slow on real corpora (CED6).

### 7.5 Explicitly out — two-way editing

Typing in the preview is the rich editor's domain (CED8). When wanted, the composition is: preview hosts the Stage 4B editor on the parsed doc; serialize via `format_markdown`; a span-preserving diff patches the CodePad buffer. Separate design doc when its time comes.

---

## 8. Native (C++) work list

Deliberately small; items 1–3 are the entire planned native surface, item 4 is an independent bug fix this project needs anyway.

| # | Item | Notes | Milestone |
|---|------|-------|-----------|
| 1 | **Parser source spans (opt-in)** — markup engine + HTML5 parser emit `source-start`/`source-end` byte offsets per CED7 | The one substantive feature; graph precedent; golden fidelity tests | M3 |
| 2 | **Convert bridge** — script-callable `parse+format` (wraps `input_from_source_n` + `format_data`), exposed to document JS like the other `radiant_dom_iface` natives | Thin; no new parsing code | M2 |
| 3 | **File read/save binding** for the editor app, gated to the `edit` command's document (document JS has no file write today) | Path fixed at launch by the CLI; no arbitrary-path FS API | M0 |
| 4 | **Fix the `layout_text` 500-iteration cap** — replace the silent truncation with a correct bound (or removal) so large `<pre>` content lays out | Standalone Radiant bug; today it truncates `load_text_doc` viewing and would truncate preview code fences; CodePad's own viewport never hits it | M2 (independent) |
| 5 | *Contingent:* `js_dom` hot-path lookups (property `strcmp` ladder, linear listener/wrapper scans) | Only if the M0 stress fixture shows them dominating; measure first | — |

---

## 9. Product surface

- **`lambda.exe edit <file>`** — new CLI verb: create window, load the bundled CodePad app document, inject file content + metadata (path, language from extension, line-ending style), enable the save binding for that path. `--headless --event-file` works exactly as for `view`, so the whole editor is drivable by `event_sim` fixtures from day one.
- Editor chrome (v1): gutter, status bar (line:col, language, dirty flag), Cmd-S save, Cmd-Z/Shift-Z, Cmd-A, Cmd-F find (M4). No tabs/file tree (single-document shell).
- The `load_text_doc` read-only source viewer becomes a thin cousin: once CodePad exists, `view` of a plain-text format can open the same component read-only (post-M1 cleanup; also moots its 500-line truncation).

---

## 10. Testing strategy

House pattern throughout, three rings per feature:

1. **Unit (vitest/jsdom oracle):** buffer/delta/undo property tests (apply∘invert = id; mapping laws), mode golden token-stream files (`*.txt` per fixture), virtualizer window math, sync mapping. Same bundles re-run under `lambda.exe js` (Stage 4C Phase-A pattern) with parity asserted against the jsdom run.
2. **UI fixtures (`event_sim`):** `test/ui/codepad/*.json` against the built `codepad.html` via `lambda.exe edit --headless --event-file` — mount, typing, **sustained-typing + scroll stress (first fixture of M0**, the interpreter-perf canary), IME composition, clipboard, selection projection at window edges, undo, save; M2+: preview render, debounce, scroll/click sync.
3. **Oracles:** CM6 (already green under Radiant) as a behavioral oracle for input edge cases (composition ordering, inputType mapping); the Lambda twin (M4) verified by the `export-lambda-oracle.ts` bridge method against CodePad's own fixtures, per CED10.

Native items get GTest coverage: span fidelity per format (§7.2), convert bridge, and a regression test that a >500-line `<pre>` lays out fully (item 4).

---

## 11. Milestones

| Milestone | Delivers | Exit gate |
|-----------|----------|-----------|
| **M0 — plaintext CodePad** | Buffer/deltas/undo, virtualized viewport + gutter, input seam (typing, IME, clipboard, navigation, select-all), `lambda.exe edit` with load/save; stress fixture | Edit + save a 50 K-line file headlessly; sustained-typing and scroll fixtures green with budgeted frame cost; unit suite green in jsdom **and** `lambda.exe js` |
| **M1 — highlighting** | Mode engine + `lambda`/`markdown`/`json`/`html`/`css`/`js`/`yaml`/`toml` + light/dark theme | Golden token streams per mode; typing latency unchanged (spans-merged rendering); mixed-mode markdown fences correct |
| **M2 — preview** | Convert bridge (native #2), split pane, debounced render for renderable formats; 500-cap fix (native #4) | Live markdown/html preview under headless fixtures; parse-latency measured on `test/markdown/` corpora and within budget |
| **M3 — sync** | Parser source spans (native #1) + scroll/click/caret sync both directions | Span-fidelity goldens per format; sync fixtures (scroll-follow, click-to-jump, caret-highlight) green; data-processing outputs proven byte-identical with spans off |
| **M4 — polish + twin** | Find/replace with decorations, soft wrap, remaining modes, static fence highlighting in rendered markdown, own-drawn selection layer (unlocks multi-cursor later), tree-sitter assist decision (§6.4), Lambda `.ls` twin via oracle bridge | Feature fixtures green; twin passes the oracle suite at parity on the shared fixtures |

---

## 12. Risks and mitigations

- **Interpreter-only DOM performance.** No DOM-heavy benchmark exists; linear `js_dom` lookups could dominate. *Mitigation:* virtualization bounds work to O(viewport); the M0 stress fixture is the first deliverable and gates everything; native item 5 is the escape hatch, `LAMBDA_JS_LARGE_INTERP=0` a diagnostic lever.
- **Selection projection at window edges** (endpoints scrolled out of the rendered window) is the fiddliest M0 logic. *Mitigation:* model-owned selection + clamped projection with dedicated fixtures; the M4 own-drawn layer removes the dependence entirely.
- **IME × virtualization ordering** (reconcile racing composition). *Mitigation:* composing line frozen until `compositionend` (CM6-proven rule); composition fixtures land in M0, not later.
- **Span fidelity vs parser normalization** (offsets drifting past escapes/expansions). *Mitigation:* capture offsets at the tokenizer/`SourceTracker` before normalization; golden span→substring tests per format (§7.2).
- **Preview latency on large documents.** *Mitigation:* measure first (existing `[TIMING]`), adaptive debounce, block-incremental re-parse held in reserve (§7.4).
- **Scope creep toward VS Code.** *Mitigation:* the non-goals list (§1) is normative; anything outside it needs a new ledger entry.

## 13. Open questions

- **OQ1** — Convert-bridge exposure: a `window.lambda.convert(...)` native on the document interface vs a Jube module import. Leaning native-on-iface (matches existing `radiant_dom_iface` additions); decide at M2.
- **OQ2** — Span granularity for M3: block-level only vs block + inline runs. Start block-level (enough for scroll/click sync); inline spans only if caret-highlight proves too coarse.
- **OQ3** — Preview fidelity path: keep `format_html` + innerHTML, or build the parse-to-DOM-subtree bridge (math pass included) in v2. Decide on M2 measurements + math importance.
- **OQ4** — `edit` on `.ls` files: is "preview" ever a run/REPL output pane, or nothing? Out of scope now; worth a note when the shell grows panes.
- **OQ5** — Where the CodePad source ultimately lives (`test/codepad/` like the rich editor, vs promotion toward `lambda/package/`): follow whatever resolution Stage 4B reaches for the rich editor's own "reference vs shipping" placement.
